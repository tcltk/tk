/*
 * tkGlfwInit.c --
 *
 *	GLFW/Wayland-specific interpreter initialization: context
 *	management, window mapping, drawing context lifecycle, color
 *	conversion, and platform init/cleanup.
 *
 *	Window architecture
 *	-------------------
 *	GLFW + libdecor own:
 *	  - xdg_surface / xdg_toplevel creation and lifecycle
 *	  - Window decorations (SSD or CSD depending on compositor)
 *	  - OpenGL ES context creation and buffer swapping
 *	  - Keyboard input, clipboard, IME, and the event loop
 *	    (glfwPollEvents drives both GLFW callbacks and our own
 *	     wl_pointer listener via the shared wl_display)
 *
 *	Tk-Wayland owns:
 *	  - wl_seat / wl_pointer (bound from the same registry GLFW uses)
 *	  - The pointer button serial required by xdg_toplevel_move/resize
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 2026      Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#define GL_GLEXT_PROTOTYPES

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <GLES2/gl2.h>

#include <wayland-client.h>
#include <libdecor.h>

#define NANOVG_GLES2_IMPLEMENTATION
#include "nanovg_gl.h"
#include "nanovg.h"

/* -----------------------------------------------------------------------
 * Module-level state
 * -------------------------------------------------------------------- 
 */

static TkGlfwContext  glfwContext       = {NULL, NULL, 0, 0, 0, NULL, 0, 0};
static WindowMapping *windowMappingList = NULL;
static Drawable       nextDrawableId   = 1000;

/* Wayland globals — only wl_seat is bound by us; everything else is
 * owned by GLFW / libdecor. */
static struct wl_display  *waylandDisplay  = NULL;
static struct wl_registry *waylandRegistry = NULL;
static struct wl_seat     *waylandSeat     = NULL;
static struct wl_pointer  *waylandPointer  = NULL;

/*
 * Serial of the most-recent wl_pointer button-press.
 * xdg_toplevel_move() / xdg_toplevel_resize() require the serial of
 * the triggering input event; we capture it in PointerButton().
 */
static uint32_t lastButtonSerial = 0;

/* -----------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------- *
 */
 
extern void  TkWaylandSetNVGContext(NVGcontext *);
extern void  TkWaylandCleanupPixmapStore(void);
extern int   TkWaylandGetGCValues(GC, unsigned long, XGCValues *);
extern void  TkWaylandMenuInit(void);
extern void  Tk_WaylandSetupTkNotifier(void);
extern int   Tktray_Init(Tcl_Interp *);
extern int   SysNotify_Init(Tcl_Interp *);
extern int   Cups_Init(Tcl_Interp *);
extern int   TkAtkAccessibility_Init(Tcl_Interp *);
extern void  TkGlfwSetupCallbacks(GLFWwindow *, TkWindow *);

WindowMapping *FindMappingByGLFW(GLFWwindow *);
WindowMapping *FindMappingByTk(TkWindow *);
WindowMapping *FindMappingByDrawable(Drawable);
void           RemoveMapping(WindowMapping *);
void           CleanupAllMappings(void);

/* -----------------------------------------------------------------------
 * wl_pointer listener callbacks
 *
 *	We register our own pointer listener solely to capture button-press
 *	serials for xdg_toplevel_move / xdg_toplevel_resize.  All other
 *	pointer events are handled by GLFW through its own listener.
 * --------------------------------------------------------------------
 */

static void
PointerEnter(void *data, struct wl_pointer *wl_pointer,
             uint32_t serial, struct wl_surface *surface,
             wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    (void)data; (void)wl_pointer; (void)serial;
    (void)surface; (void)surface_x; (void)surface_y;
}

static void
PointerLeave(void *data, struct wl_pointer *wl_pointer,
             uint32_t serial, struct wl_surface *surface)
{
    (void)data; (void)wl_pointer; (void)serial; (void)surface;
}

static void
PointerMotion(void *data, struct wl_pointer *wl_pointer,
              uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    (void)data; (void)wl_pointer; (void)time;
    (void)surface_x; (void)surface_y;
}

static void
PointerButton(void *data, struct wl_pointer *wl_pointer,
              uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    (void)data; (void)wl_pointer; (void)time; (void)button;

    /* Capture the serial for interactive move/resize. */
    if (state == WL_POINTER_BUTTON_STATE_PRESSED)
        lastButtonSerial = serial;
}

static void
PointerAxis(void *data, struct wl_pointer *wl_pointer,
            uint32_t time, uint32_t axis, wl_fixed_t value)
{
    (void)data; (void)wl_pointer; (void)time; (void)axis; (void)value;
}

static void
PointerFrame(void *data, struct wl_pointer *wl_pointer)
{
    (void)data; (void)wl_pointer;
}

static void
PointerAxisSource(void *data, struct wl_pointer *wl_pointer,
                  uint32_t axis_source)
{
    (void)data; (void)wl_pointer; (void)axis_source;
}

static void
PointerAxisStop(void *data, struct wl_pointer *wl_pointer,
                uint32_t time, uint32_t axis)
{
    (void)data; (void)wl_pointer; (void)time; (void)axis;
}

static void
PointerAxisDiscrete(void *data, struct wl_pointer *wl_pointer,
                    uint32_t axis, int32_t discrete)
{
    (void)data; (void)wl_pointer; (void)axis; (void)discrete;
}

static const struct wl_pointer_listener pointerListener = {
    .enter         = PointerEnter,
    .leave         = PointerLeave,
    .motion        = PointerMotion,
    .button        = PointerButton,
    .axis          = PointerAxis,
    .frame         = PointerFrame,
    .axis_source   = PointerAxisSource,
    .axis_stop     = PointerAxisStop,
    .axis_discrete = PointerAxisDiscrete,
};

/* -----------------------------------------------------------------------
 * SeatCapabilities --
 *
 *	Handle wl_seat capability changes. Creates a wl_pointer when the
 *	seat advertises pointer capability.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates and initializes waylandPointer if not already present.
 * -------------------------------------------------------------------- 
 */

static void
SeatCapabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    (void)data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !waylandPointer) {
        waylandPointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(waylandPointer, &pointerListener, NULL);
    }
}

static void
SeatName(void *d, struct wl_seat *s, const char *n)
{ (void)d; (void)s; (void)n; }

static const struct wl_seat_listener seatListener = {
    SeatCapabilities, SeatName,
};

/* -----------------------------------------------------------------------
 * RegistryHandleGlobal --
 *
 *	Handle global registry advertisements.  We bind only wl_seat;
 *	xdg_wm_base and decoration management are owned by libdecor/GLFW.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes waylandSeat with its listener.
 * --------------------------------------------------------------------
 */

static void
RegistryHandleGlobal(void *data, struct wl_registry *registry,
                     uint32_t name, const char *interface, uint32_t version)
{
    (void)data;

    if (strcmp(interface, wl_seat_interface.name) == 0) {
        uint32_t v = (version >= 5) ? 5 : version;
        waylandSeat = wl_registry_bind(registry, name,
                                       &wl_seat_interface, v);
        wl_seat_add_listener(waylandSeat, &seatListener, NULL);
    }
}

static void
RegistryHandleGlobalRemove(void *data, struct wl_registry *reg, uint32_t name)
{ (void)data; (void)reg; (void)name; }

static const struct wl_registry_listener registryListener = {
    RegistryHandleGlobal,
    RegistryHandleGlobalRemove,
};

/* -----------------------------------------------------------------------
 * Accessors
 * -------------------------------------------------------------------- 
 */

MODULE_SCOPE TkGlfwContext *
TkGlfwGetContext(void) { return &glfwContext; }

MODULE_SCOPE struct wl_seat *
TkGlfwGetWaylandSeat(void) { return waylandSeat; }

MODULE_SCOPE uint32_t
TkGlfwGetLastInputSerial(void) { return lastButtonSerial; }

MODULE_SCOPE uint32_t
TkGlfwGetLastButtonSerial(void) { return lastButtonSerial; }

MODULE_SCOPE void
TkGlfwSetLastInputSerial(uint32_t s) { lastButtonSerial = s; }


/* -----------------------------------------------------------------------
 * TkGlfwErrorCallback --
 *
 *	GLFW error callback that prints errors to stderr.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes error messages to stderr.
 * -------------------------------------------------------------------- 
 */

MODULE_SCOPE void
TkGlfwErrorCallback(int error, const char *desc)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, desc);
}

/* -----------------------------------------------------------------------
 * TkGlfwInitWaylandProtocols --
 *
 *	Bind wl_seat from the Wayland registry after GLFW has initialized
 *	and the display is available.  xdg_wm_base and decoration management
 *	are left entirely to libdecor/GLFW.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Populates waylandDisplay, waylandRegistry, and waylandSeat.
 *	Performs two display roundtrips.
 * -------------------------------------------------------------------- 
 */

static int
TkGlfwInitWaylandProtocols(void)
{
    if (waylandDisplay) return TCL_OK;

    waylandDisplay = glfwGetWaylandDisplay();
    if (!waylandDisplay) {
        fprintf(stderr, "TkGlfwInitWaylandProtocols: "
                "glfwGetWaylandDisplay() returned NULL\n");
        return TCL_ERROR;
    }

    waylandRegistry = wl_display_get_registry(waylandDisplay);
    if (!waylandRegistry) {
        fprintf(stderr, "TkGlfwInitWaylandProtocols: "
                "wl_display_get_registry() failed\n");
        return TCL_ERROR;
    }

    wl_registry_add_listener(waylandRegistry, &registryListener, NULL);
    wl_display_roundtrip(waylandDisplay); /* deliver global advertisements  */
    wl_display_roundtrip(waylandDisplay); /* process bind() acknowledgements */

    if (!waylandSeat) {
        fprintf(stderr, "TkGlfwInitWaylandProtocols: wl_seat missing\n");
        return TCL_ERROR;
    }

    return TCL_OK;
}

/* -----------------------------------------------------------------------
 * TkGlfwInitialize --
 *
 *	Initialize the GLFW library, create a shared context window,
 *	initialize Wayland protocols, and create the global NanoVG context.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Sets up glfwContext structure, creates a hidden shared GL window,
 *	initializes NanoVG, and binds wl_seat.
 * --------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwInitialize(void)
{
    if (glfwContext.initialized) return TCL_OK;

    glfwSetErrorCallback(TkGlfwErrorCallback);

#ifdef GLFW_PLATFORM_WAYLAND
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif

    if (!glfwInit()) {
        fprintf(stderr, "TkGlfwInitialize: glfwInit() failed\n");
        return TCL_ERROR;
    }

    /*
     * Shared context window — hidden, 1x1.  Application windows share
     * its GL context so textures and shaders are visible across them.
     */
    glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);

    glfwContext.mainWindow =
        glfwCreateWindow(1, 1, "Tk Shared Context", NULL, NULL);
    if (!glfwContext.mainWindow) {
        fprintf(stderr, "TkGlfwInitialize: failed to create shared window\n");
        glfwTerminate();
        return TCL_ERROR;
    }

    glfwMakeContextCurrent(glfwContext.mainWindow);
    glfwSwapInterval(1);
    
    if (TkGlfwInitWaylandProtocols() != TCL_OK) {
        glfwDestroyWindow(glfwContext.mainWindow);
        glfwContext.mainWindow = NULL;
        glfwTerminate();
        return TCL_ERROR;
    }
    glfwContext.decorFontId = -1; /* loaded lazily per-context */
    TkWaylandSetNVGContext(glfwContext.vg);
    glfwContext.initialized = 1;
    return TCL_OK;
}

/* -----------------------------------------------------------------------
 * TkGlfwStartInteractiveResize --
 *
 *	Initiate an interactive window resize via the xdg-shell protocol.
 *	Called from the decoration handling code.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sends a resize request to the compositor using the last button serial.
 * -------------------------------------------------------------------- 
 */

MODULE_SCOPE void
TkGlfwStartInteractiveResize(GLFWwindow *glfwWindow, uint32_t edges)
{
    WindowMapping *mapping = FindMappingByGLFW(glfwWindow);
    if (mapping && mapping->frame && waylandSeat)
        libdecor_frame_resize(mapping->frame, waylandSeat, lastButtonSerial, edges);
}

/* -----------------------------------------------------------------------
 * TkGlfwStartInteractiveMove --
 *
 *	Initiate an interactive window move via the xdg-shell protocol.
 *	Called from the decoration handling code.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sends a move request to the compositor using the last button serial.
 * -------------------------------------------------------------------- */

MODULE_SCOPE void
TkGlfwStartInteractiveMove(GLFWwindow *glfwWindow)
{
    WindowMapping *mapping = FindMappingByGLFW(glfwWindow);
    if (mapping && mapping->frame && waylandSeat)
        libdecor_frame_move(mapping->frame, waylandSeat, lastButtonSerial);
}

/* -----------------------------------------------------------------------
 * TkGlfwCreateWindow --
 *
 *	Create a new GLFW window.  libdecor (via GLFW) owns all xdg-shell
 *	and decoration objects.  We register the mapping and set up
 *	callbacks, then wait for the compositor's first configure before
 *	returning so that BeginDraw always has valid dimensions.
 *
 * Results:
 *	Returns the GLFWwindow pointer on success, NULL on failure.
 *	If drawableOut is non-NULL, it is set to the new drawable ID.
 *
 * Side effects:
 *	Creates a GLFW window, registers the window mapping, and
 *  sets up callbacks
 * --------------------------------------------------------------------
 */


MODULE_SCOPE GLFWwindow *
TkGlfwCreateWindow(
    TkWindow   *tkWin,
    int         width,
    int         height,
    const char *title,
    Drawable   *drawableOut)
{
    WindowMapping *mapping;
    GLFWwindow    *window;

   /* Ensure NanoVG context exists. */
    if (!glfwContext.vg) {
         
        /* Try to obtain context. */
        glfwContext.vg = TkGlfwGetNVGContext();
        
        /* If still NULL, recreate it */
        if (!glfwContext.vg && glfwContext.mainWindow) {
            glfwMakeContextCurrent(glfwContext.mainWindow);
            glfwContext.vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
            if (glfwContext.vg) {
                TkWaylandSetNVGContext(glfwContext.vg);
            }
        }
    }
    
    if (!glfwContext.initialized) {
        if (TkGlfwInitialize() != TCL_OK)
            return NULL;
    }

    /* Reuse existing mapping if present. */
    if (tkWin != NULL) {
        mapping = FindMappingByTk(tkWin);
        if (mapping != NULL) {
            if (drawableOut) *drawableOut = mapping->drawable;
            return mapping->glfwWindow;
        }
    }

    if (width  <= 0) width  = 200;
    if (height <= 0) height = 200;

    /* Must set GL hints before every glfwCreateWindow call. */
    glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE,             GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW,         GLFW_TRUE);
    glfwWindowHint(GLFW_AUTO_ICONIFY,          GLFW_FALSE);

    window = glfwCreateWindow(width, height, title ? title : "",
                              NULL, glfwContext.mainWindow);
    if (!window) {
        return NULL;
    }
  
    /* Allocate and zero mapping. */
    mapping = (WindowMapping *)ckalloc(sizeof(WindowMapping));
    memset(mapping, 0, sizeof(WindowMapping));
    mapping->tkWindow   = tkWin;
    mapping->glfwWindow = window;
    mapping->drawable   = nextDrawableId++;
    mapping->width      = width;
    mapping->height     = height;
    mapping->nextPtr    = windowMappingList;
    windowMappingList   = mapping;

    glfwSetWindowUserPointer(window, mapping);

    if (tkWin != NULL)
        TkGlfwSetupCallbacks(window, tkWin);

    /* Show window — triggers libdecor configure. */
    glfwShowWindow(window);

    /* Wait for compositor to confirm real dimensions.
     * Without this BeginDraw uses stale width/height and the
     * GL viewport is wrong, producing a black window. */
    int timeout = 0;
    while ((mapping->width == 0 || mapping->height == 0) && timeout < 100) {
        wl_display_roundtrip(waylandDisplay);
        glfwPollEvents();
        timeout++;
    }

    if (mapping->width  == 0) mapping->width  = width;
    if (mapping->height == 0) mapping->height = height;

    if (drawableOut) *drawableOut = mapping->drawable;

    /* Queue expose only after dimensions are valid. */
    if (tkWin != NULL)
        TkWaylandQueueExposeEvent(tkWin, 0, 0, mapping->width, mapping->height);

    return window;
}

/* -----------------------------------------------------------------------
 * TkGlfwDestroyWindow --
 *
 *	Destroy a GLFW window and clean up associated resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys decorations, removes mapping, destroys GLFW window.
 *	xdg objects are destroyed by libdecor/GLFW during glfwDestroyWindow.
 * -------------------------------------------------------------------- 
 */

MODULE_SCOPE void
TkGlfwDestroyWindow(GLFWwindow *glfwWindow)
{
    WindowMapping *mapping;
    if (!glfwWindow) return;

    mapping = FindMappingByGLFW(glfwWindow);
    if (mapping) {
        RemoveMapping(mapping);
    }

    glfwDestroyWindow(glfwWindow);
}

/* -----------------------------------------------------------------------
 * TkGlfwCleanup --
 *
 *	Perform global cleanup of all GLFW and Wayland resources.
 *	Called during interpreter shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys all windows, frees NanoVG context, destroys Wayland
 *	objects, terminates GLFW, and resets initialization state.
 * -------------------------------------------------------------------- 
 */

MODULE_SCOPE void
TkGlfwCleanup(void)
{
    if (!glfwContext.initialized) return;

    CleanupAllMappings();
    TkWaylandCleanupPixmapStore();

    if (glfwContext.vg) {
        nvgDeleteGLES2(glfwContext.vg);
        glfwContext.vg = NULL;
    }
    if (glfwContext.mainWindow) {
        glfwDestroyWindow(glfwContext.mainWindow);
        glfwContext.mainWindow = NULL;
    }

    if (waylandPointer) { wl_pointer_destroy(waylandPointer); waylandPointer = NULL; }
    if (waylandSeat)    { wl_seat_destroy(waylandSeat);       waylandSeat    = NULL; }
    if (waylandRegistry){ wl_registry_destroy(waylandRegistry); waylandRegistry = NULL; }

    glfwTerminate();
    glfwContext.initialized = 0;
    waylandDisplay = NULL;
}

/* -----------------------------------------------------------------------
 * TkGlfwBeginDraw --
 *
 *	Begin a drawing operation on a drawable. Sets up the NanoVG frame,
 *	clears the buffer, and applies GC settings if provided.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Makes the window's GL context current, clears the framebuffer,
 *	begins a NanoVG frame, and updates frame tracking state.
 * -------------------------------------------------------------------- 
 */

MODULE_SCOPE int
TkGlfwBeginDraw(
    Drawable                drawable,
    GC                      gc,
    TkWaylandDrawingContext *dcPtr)
{
    WindowMapping *mapping;
    int fbWidth, fbHeight;

    if (!dcPtr) return TCL_ERROR;

    mapping = FindMappingByDrawable(drawable);
    if (!mapping || !mapping->glfwWindow) return TCL_ERROR;

    /* Handle nested frames */
    if (glfwContext.nvgFrameActive) {
        if (glfwContext.activeWindow != mapping->glfwWindow) {
            nvgEndFrame(glfwContext.vg);
            if (glfwContext.activeWindow) {
                glfwSwapBuffers(glfwContext.activeWindow);
            }
            glfwContext.nvgFrameActive = 0;
            glfwContext.activeWindow = NULL;
        } else {
            dcPtr->drawable = drawable;
            dcPtr->glfwWindow = mapping->glfwWindow;
            dcPtr->width = mapping->width;
            dcPtr->height = mapping->height;
            dcPtr->vg = glfwContext.vg;
            dcPtr->nestedFrame = 1;
            
            if (gc) TkGlfwApplyGC(glfwContext.vg, gc);
            return TCL_OK;
        }
    }

    /* Make context current and get actual framebuffer size */
    glfwMakeContextCurrent(mapping->glfwWindow);
    glfwGetFramebufferSize(mapping->glfwWindow, &fbWidth, &fbHeight);
    
    /* Store dimensions in context */
    dcPtr->drawable = drawable;
    dcPtr->glfwWindow = mapping->glfwWindow;
    dcPtr->width = mapping->width;
    dcPtr->height = mapping->height;
    dcPtr->vg = glfwContext.vg;
    dcPtr->nestedFrame = 0;

    /* Clear the buffer BEFORE starting NanoVG frame */
    glViewport(0, 0, fbWidth, fbHeight);
    glClearColor(0.92f, 0.92f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Start NanoVG frame with correct dimensions */
    nvgBeginFrame(glfwContext.vg, 
                  (float)mapping->width, 
                  (float)mapping->height, 
                  (float)fbWidth / (float)mapping->width);  /* Pixel ratio for HiDPI */
    
    nvgSave(glfwContext.vg);
    nvgTranslate(glfwContext.vg, 0.5f, 0.5f);

    glfwContext.nvgFrameActive = 1;
    glfwContext.activeWindow = mapping->glfwWindow;

    if (gc) TkGlfwApplyGC(glfwContext.vg, gc);

    return TCL_OK;
}

/* -----------------------------------------------------------------------
 * TkGlfwEndDraw --
 *
 *	End a drawing operation. Ends the NanoVG frame and swaps buffers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Ends the NanoVG frame, swaps buffers, and
 *	clears frame tracking state if this is the outer frame.
 * -------------------------------------------------------------------- 
 */

MODULE_SCOPE void
TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr)
{
    if (!dcPtr || !dcPtr->vg) return;

    if (dcPtr->nestedFrame) return;

    nvgRestore(dcPtr->vg);
    
    nvgEndFrame(dcPtr->vg);
    glfwContext.nvgFrameActive    = 0;
    glfwContext.nvgFrameAutoOpened = 0;
    glfwContext.activeWindow       = NULL;

    if (dcPtr->glfwWindow) {
        glfwSwapBuffers(dcPtr->glfwWindow);
    }
}



/* -----------------------------------------------------------------------
 * TkGlfwFlushAutoFrame --
 *
 *	Flush an auto-opened NanoVG frame if one is active.
 *	Used by the measurement code to clean up after font operations.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Ends the current frame and swaps buffers if not on the main window.
 * --------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwFlushAutoFrame(void)
{
    GLFWwindow *cur;
    if (!glfwContext.nvgFrameActive || !glfwContext.nvgFrameAutoOpened) return;
    nvgEndFrame(glfwContext.vg);
    glfwContext.nvgFrameActive     = 0;
    glfwContext.nvgFrameAutoOpened = 0;
    cur = glfwGetCurrentContext();
    if (cur && cur != glfwContext.mainWindow)
        glfwSwapBuffers(cur);
}

/* -----------------------------------------------------------------------
 * TkGlfwGetNVGContext --
 *
 *	Get the global NanoVG context, initializing if necessary and
 *	automatically beginning a frame if none is active.
 *
 * Results:
 *	Returns the NanoVG context pointer, or NULL on failure.
 *
 * Side effects:
 *	May initialize GLFW and NanoVG, and may begin a new frame.
 * -------------------------------------------------------------------- 
 */

MODULE_SCOPE NVGcontext *
TkGlfwGetNVGContext(void)
{
    GLFWwindow *cur;
    int w, h;

    /* First, ensure GLFW is initialized */
    if (!glfwContext.initialized) {
        if (TkGlfwInitialize() != TCL_OK) {
            fprintf(stderr, "TkGlfwGetNVGContext: Failed to initialize\n");
            return NULL;
        }
    }

    /* If NanoVG context is NULL, try to recreate it */
    if (!glfwContext.vg) {
        fprintf(stderr, "TkGlfwGetNVGContext: NanoVG context NULL, recreating...\n");
        
        if (!glfwContext.mainWindow) {
            fprintf(stderr, "TkGlfwGetNVGContext: No main window!\n");
            return NULL;
        }
        
        glfwMakeContextCurrent(glfwContext.mainWindow);
        glfwContext.vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
        
        if (!glfwContext.vg) {
            fprintf(stderr, "TkGlfwGetNVGContext: Failed to recreate NanoVG context\n");
            return NULL;
        }
        
        fprintf(stderr, "TkGlfwGetNVGContext: Recreated NanoVG context: %p\n", 
                glfwContext.vg);
        TkWaylandSetNVGContext(glfwContext.vg);
    }

    /* Get current context or use main window. */
    cur = glfwGetCurrentContext();
    if (!cur) {
        glfwMakeContextCurrent(glfwContext.mainWindow);
        cur = glfwContext.mainWindow;
    }

    /* Handle frame management. */
    if (!glfwContext.nvgFrameActive) {
        glfwGetFramebufferSize(cur, &w, &h);
        nvgBeginFrame(glfwContext.vg, (float)w, (float)h, 1.0f);
        glfwContext.nvgFrameActive = 1;
        glfwContext.nvgFrameAutoOpened = 1;
        glfwContext.activeWindow = cur;
    }

    return glfwContext.vg;
}
/* -----------------------------------------------------------------------
 * TkGlfwGetNVGContextForMeasure --
 *
 *	Get the NanoVG context for font measurement operations,
 *	ensuring a context is current.
 *
 * Results:
 *	Returns the NanoVG context or NULL on failure.
 *
 * Side effects:
 *	May make the shared context current.
 * -------------------------------------------------------------------- 
 */

MODULE_SCOPE NVGcontext *
TkGlfwGetNVGContextForMeasure(void)
{
    TkGlfwContext *ctx = TkGlfwGetContext();
    if (!ctx || !ctx->initialized || !ctx->vg) return NULL;
    if (!glfwGetCurrentContext()) glfwMakeContextCurrent(ctx->mainWindow);
    return ctx->vg;
}

/* -----------------------------------------------------------------------
 * TkGlfwProcessEvents --
 *
 *	Process pending GLFW events. Called from the Tk event loop.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Dispatches GLFW callbacks for input, window, and other events.
 * --------------------------------------------------------------------
 */


MODULE_SCOPE void
TkGlfwProcessEvents(void)
{
    if (glfwContext.initialized) {
        glfwPollEvents();
        if (waylandDisplay)
            wl_display_flush(waylandDisplay);
    }
}

/* -----------------------------------------------------------------------
 * Color / GC utilities
 * -------------------------------------------------------------------- */

/* -----------------------------------------------------------------------
 * TkGlfwXColorToNVG --
 *
 *	Convert an XColor structure to an NVGcolor.
 *
 * Results:
 *	Returns the NVGcolor corresponding to the XColor.
 *
 * Side effects:
 *	None.
 * -------------------------------------------------------------------- 
 */

MODULE_SCOPE NVGcolor
TkGlfwXColorToNVG(XColor *xcolor)
{
    if (!xcolor) return nvgRGBA(0, 0, 0, 255);
    return nvgRGBA(xcolor->red >> 8, xcolor->green >> 8, xcolor->blue >> 8, 255);
}

/* -----------------------------------------------------------------------
 * TkGlfwPixelToNVG --
 *
 *	Convert a 24-bit RGB pixel value to an NVGcolor.
 *
 * Results:
 *	Returns the NVGcolor corresponding to the pixel value.
 *
 * Side effects:
 *	None.
 * --------------------------------------------------------------------
 */

MODULE_SCOPE NVGcolor
TkGlfwPixelToNVG(unsigned long pixel)
{
    return nvgRGBA((pixel>>16)&0xFF, (pixel>>8)&0xFF, pixel&0xFF, 255);
}

/* -----------------------------------------------------------------------
 * TkGlfwApplyGC --
 *
 *	Apply settings from a graphics context to the NanoVG context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets NanoVG fill color, stroke color, line width, line caps,
 *	and line joins based on GC values.
 * -------------------------------------------------------------------- */

MODULE_SCOPE void
TkGlfwApplyGC(NVGcontext *vg, GC gc)
{
    XGCValues v;
    NVGcolor  c;
    if (!vg || !gc) return;
    TkWaylandGetGCValues(gc,
        GCForeground|GCLineWidth|GCLineStyle|GCCapStyle|GCJoinStyle, &v);
    c = TkGlfwPixelToNVG(v.foreground);
    nvgFillColor(vg, c); nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, v.line_width > 0 ? (float)v.line_width : 1.0f);
    switch (v.cap_style)  { case CapRound:      nvgLineCap(vg, NVG_ROUND);  break;
                            case CapProjecting: nvgLineCap(vg, NVG_SQUARE); break;
                            default:            nvgLineCap(vg, NVG_BUTT);   break; }
    switch (v.join_style) { case JoinRound: nvgLineJoin(vg, NVG_ROUND); break;
                            case JoinBevel: nvgLineJoin(vg, NVG_BEVEL); break;
                            default:        nvgLineJoin(vg, NVG_MITER); break; }
}

/* -----------------------------------------------------------------------
 * Tk platform entry points
 * -------------------------------------------------------------------- 
 */

/* -----------------------------------------------------------------------
 * TkpInit --
 *
 *	Initialize the Tk platform-specific layer for Wayland/GLFW.
 *	Called during interpreter initialization.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initializes GLFW, Wayland protocols, menu system,
 *	notifier, and various extension modules.
 * -------------------------------------------------------------------- */

int
TkpInit(Tcl_Interp *interp)
{
    if (TkGlfwInitialize() != TCL_OK) return TCL_ERROR;
    TkWaylandMenuInit();
    Tk_WaylandSetupTkNotifier();
    Tktray_Init(interp);
    SysNotify_Init(interp);
    Cups_Init(interp);
    TkAtkAccessibility_Init(interp);
    return TCL_OK;
}

/* -----------------------------------------------------------------------
 * TkpGetAppName --
 *
 *	Extract the application name from argv0 for use in window titles.
 *
 * Results:
 *	Appends the application name to namePtr.
 *
 * Side effects:
 *	None.
 * -------------------------------------------------------------------- */

void
TkpGetAppName(Tcl_Interp *interp, Tcl_DString *namePtr)
{
    const char *p, *name = Tcl_GetVar2(interp, "argv0", NULL, TCL_GLOBAL_ONLY);
    if (!name || !*name) name = "tk";
    else { p = strrchr(name, '/'); if (p) name = p+1; }
    Tcl_DStringAppend(namePtr, name, TCL_INDEX_NONE);
}

/* -----------------------------------------------------------------------
 * TkpDisplayWarning --
 *
 *	Display a warning message to stderr.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes the warning to the standard error channel.
 * -------------------------------------------------------------------- 
 */

void
TkpDisplayWarning(const char *msg, const char *title)
{
    Tcl_Channel ch = Tcl_GetStdChannel(TCL_STDERR);
    if (ch) {
        Tcl_WriteChars(ch, title, TCL_INDEX_NONE);
        Tcl_WriteChars(ch, ": ", 2);
        Tcl_WriteChars(ch, msg,  TCL_INDEX_NONE);
        Tcl_WriteChars(ch, "\n", 1);
    }
}

/* -----------------------------------------------------------------------
 * Window mapping list
 * -------------------------------------------------------------------- */

/* -----------------------------------------------------------------------
 * FindMappingByGLFW --
 *
 *	Find a window mapping by GLFW window pointer.
 *
 * Results:
 *	Returns the WindowMapping or NULL if not found.
 *
 * Side effects:
 *	None.
 * -------------------------------------------------------------------- 
 */

WindowMapping *
FindMappingByGLFW(GLFWwindow *w)
{
    WindowMapping *c = windowMappingList;
    while (c) { if (c->glfwWindow == w) return c; c = c->nextPtr; }
    return NULL;
}

/* -----------------------------------------------------------------------
 * FindMappingByTk --
 *
 *	Find a window mapping by Tk window pointer.
 *
 * Results:
 *	Returns the WindowMapping or NULL if not found.
 *
 * Side effects:
 *	None.
 * -------------------------------------------------------------------- 
 */

WindowMapping *
FindMappingByTk(TkWindow *w)
{
    WindowMapping *c = windowMappingList;
    while (c) { if (c->tkWindow == w) return c; c = c->nextPtr; }
    return NULL;
}

/* -----------------------------------------------------------------------
 * FindMappingByDrawable --
 *
 *	Find a window mapping by drawable ID.
 *
 * Results:
 *	Returns the WindowMapping or NULL if not found.
 *
 * Side effects:
 *	None.
 * -------------------------------------------------------------------- 
 */

WindowMapping *
FindMappingByDrawable(Drawable d)
{
    WindowMapping *c = windowMappingList;
    while (c) { if (c->drawable == d) return c; c = c->nextPtr; }
    return NULL;
}

/* -----------------------------------------------------------------------
 * RemoveMapping --
 *
 *	Remove a single mapping from the linked list and free it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees the mapping structure.
 * -------------------------------------------------------------------- 
 */

void
RemoveMapping(WindowMapping *m)
{
    WindowMapping **pp = &windowMappingList;
    while (*pp) {
        if (*pp == m) { *pp = m->nextPtr; ckfree((char *)m); return; }
        pp = &(*pp)->nextPtr;
    }
}

/* -----------------------------------------------------------------------
 * CleanupAllMappings --
 *
 *	Destroy all window mappings and associated resources.
 *	Called during global cleanup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys all GLFW windows, then frees all mapping
 *	structures.  xdg objects are destroyed by libdecor/GLFW during
 *	glfwDestroyWindow.
 * -------------------------------------------------------------------- 
 */

void
CleanupAllMappings(void)
{
    WindowMapping *c = windowMappingList, *n;
    while (c) {
        n = c->nextPtr;
        if (c->glfwWindow)  glfwDestroyWindow(c->glfwWindow);
        ckfree((char *)c);
        c = n;
    }
    windowMappingList = NULL;
}

/* -----------------------------------------------------------------------
 * Miscellaneous accessors
 * -------------------------------------------------------------------- */

/* -----------------------------------------------------------------------
 * TkGlfwGetGLFWWindow --
 *
 *	Get the GLFW window associated with a Tk window.
 *
 * Results:
 *	Returns the GLFWwindow pointer or NULL.
 *
 * Side effects:
 *	None.
 * --------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *
TkGlfwGetGLFWWindow(Tk_Window tkwin)
{
    WindowMapping *m = FindMappingByTk((TkWindow *)tkwin);
    return m ? m->glfwWindow : NULL;
}

/* -----------------------------------------------------------------------
 * TkGlfwGetDrawable --
 *
 *	Get the drawable ID associated with a GLFW window.
 *
 * Results:
 *	Returns the drawable ID or 0.
 *
 * Side effects:
 *	None.
 * --------------------------------------------------------------------
 */

MODULE_SCOPE Drawable
TkGlfwGetDrawable(GLFWwindow *w)
{
    WindowMapping *m = FindMappingByGLFW(w);
    return m ? m->drawable : 0;
}

/* -----------------------------------------------------------------------
 * TkGlfwResizeWindow --
 *
 *	Update the stored dimensions for a window in the mapping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates mapping width and height.
 * -------------------------------------------------------------------- 
 */

MODULE_SCOPE void
TkGlfwResizeWindow(GLFWwindow *w, int width, int height)
{
    WindowMapping *m = FindMappingByGLFW(w);
    if (m) { m->width = width; m->height = height; }
}

/* -----------------------------------------------------------------------
 * TkGlfwUpdateWindowSize --
 *
 *	Updates the stored dimensions for a GLFW window in the mapping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The width and height fields in the window mapping are updated.
 * -------------------------------------------------------------------- 
 */

MODULE_SCOPE void
TkGlfwUpdateWindowSize(GLFWwindow *glfwWindow, int width, int height)
{
    WindowMapping *m = FindMappingByGLFW(glfwWindow);
    if (m) { m->width = width; m->height = height; }
}

/* -----------------------------------------------------------------------
 * TkGlfwGetWindowFromDrawable --
 *
 *	Returns the GLFW window associated with a drawable ID.
 *
 * Results:
 *	GLFWwindow pointer, or NULL if no mapping exists.
 *
 * Side effects:
 *	None.
 * -------------------------------------------------------------------- 
 */

MODULE_SCOPE GLFWwindow *
TkGlfwGetWindowFromDrawable(Drawable drawable)
{
    WindowMapping *m = FindMappingByDrawable(drawable);
    return m ? m->glfwWindow : NULL;
}

/* -----------------------------------------------------------------------
 * TkGlfwGetTkWindow --
 *
 *	Returns the Tk window associated with a GLFW window.
 *
 * Results:
 *	TkWindow pointer, or NULL if no mapping exists.
 *
 * Side effects:
 *	None.
 * -------------------------------------------------------------------- 
 */

MODULE_SCOPE TkWindow *
TkGlfwGetTkWindow(GLFWwindow *glfwWindow)
{
    WindowMapping *m = FindMappingByGLFW(glfwWindow);
    return m ? m->tkWindow : NULL;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
