/*
 * tkGlfwInit.c --
 *
 * GLFW/Wayland-specific interpreter initialization: context
 * management, window mapping, drawing context lifecycle, color
 * conversion, and platform init/cleanup.
 *
 * Window architecture
 * -------------------
 * GLFW + libdecor own:
 *   - xdg_surface / xdg_toplevel creation and lifecycle
 *   - Window decorations (SSD or CSD depending on compositor)
 *   - OpenGL ES context creation and buffer swapping
 *   - Keyboard input, clipboard, IME, and the event loop

 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 2026  Kevin Walzer
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

#define NANOVG_GLES2_IMPLEMENTATION
#include "nanovg_gl.h"
#include "nanovg.h"

/*
 * ---------------------------------------------------------------
 * Module-level state
 * ---------------------------------------------------------------
 */

static TkGlfwContext  glfwContext       = {NULL, NULL, 0, 0, 0, NULL, 0, 0};
static WindowMapping *windowMappingList = NULL;
static Drawable       nextDrawableId   = 1000;

/*
 * ---------------------------------------------------------------
 * Forward declarations
 * ---------------------------------------------------------------
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


/*
 * ---------------------------------------------------------------
 * Accessors
 * ---------------------------------------------------------------
 */

MODULE_SCOPE TkGlfwContext *
TkGlfwGetContext(void) { return &glfwContext; }

/*
 * ---------------------------------------------------------------
 * TkGlfwErrorCallback --
 *
 *   GLFW error callback that prints errors to stderr.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Prints error messages to stderr.
 * ---------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwErrorCallback(int error, const char *desc)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, desc);
}

/*
 * ---------------------------------------------------------------
 * TkGlfwInitialize --
 *
 *   Initialize the GLFW library, create a shared context window,
 *   initialize Wayland protocols, and create the global NanoVG context.
 *
 *   NanoVG is created exactly once here, immediately after the shared
 *   GL context is made current. All subsequent windows share this
 *   context and this NVGcontext.
 *
 * Results:
 *   TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *   Initializes GLFW, creates a hidden shared GL context window,
 *   and creates the global NanoVG context.
 * ---------------------------------------------------------------
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
     * Shared context window - hidden. All application windows
     * share its GL context so textures and shaders are visible across them.
     */
    glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);

    glfwContext.mainWindow =
        glfwCreateWindow(640, 480, "Tk Shared Context", NULL, NULL);
    if (!glfwContext.mainWindow) {
        fprintf(stderr, "TkGlfwInitialize: failed to create shared window\n");
        glfwTerminate();
        return TCL_ERROR;
    }

    glfwMakeContextCurrent(glfwContext.mainWindow);
    glfwSwapInterval(1);

    /* Create NanoVG context once, here, while the shared context is current. */
    glfwContext.vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!glfwContext.vg) {
        fprintf(stderr, "TkGlfwInitialize: nvgCreateGLES2() failed\n");
        glfwDestroyWindow(glfwContext.mainWindow);
        glfwContext.mainWindow = NULL;
        glfwTerminate();
        return TCL_ERROR;
    }
    TkWaylandSetNVGContext(glfwContext.vg);

    /* Load font for decoration text rendering. */
    glfwContext.decorFontId = nvgCreateFont(glfwContext.vg, "sans",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");

    glfwContext.initialized = 1;
    return TCL_OK;
}


/*
 * ---------------------------------------------------------------
 * TkGlfwCreateWindow --
 *
 *   Create a new GLFW window sharing the global GL context.
 *   Waits for the compositor's first configure event before returning
 *   so that BeginDraw always has valid dimensions.
 *
 * Results:
 *   Returns the GLFWwindow pointer on success, NULL on failure.
 *   If drawableOut is non-NULL, it is set to the new drawable ID.
 *
 * Side effects:
 *   Creates a new GLFW window, adds it to the window mapping list,
 *   and queues an expose event for the associated Tk window.
 * ---------------------------------------------------------------
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

    /* GL hints must be set before every glfwCreateWindow call. */
    glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE,             GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW,         GLFW_TRUE);
    glfwWindowHint(GLFW_AUTO_ICONIFY,          GLFW_FALSE);

    window = glfwCreateWindow(width, height, title ? title : "",
                              NULL, glfwContext.mainWindow);
    if (!window) return NULL;

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

    /* Show window - triggers libdecor configure. */
    glfwShowWindow(window);

    /* Wait for the compositor to confirm real dimensions. */
    int timeout = 0;
    while ((mapping->width == 0 || mapping->height == 0) && timeout < 100) {
        glfwPollEvents();
        timeout++;
    }
    if (mapping->width  == 0) mapping->width  = width;
    if (mapping->height == 0) mapping->height = height;

    if (drawableOut) *drawableOut = mapping->drawable;

    if (tkWin != NULL)
        TkWaylandQueueExposeEvent(tkWin, 0, 0, mapping->width, mapping->height);

    return window;
}

/*
 * ---------------------------------------------------------------
 * TkGlfwDestroyWindow --
 *
 *   Destroy a GLFW window and clean up associated resources.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Removes the window from the mapping list and destroys the GLFW window.
 * ---------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwDestroyWindow(GLFWwindow *glfwWindow)
{
    WindowMapping *mapping;
    if (!glfwWindow) return;

    mapping = FindMappingByGLFW(glfwWindow);
    if (mapping) RemoveMapping(mapping);

    glfwDestroyWindow(glfwWindow);
}

/*
 * ---------------------------------------------------------------
 * TkGlfwCleanup --
 *
 *   Perform global cleanup of all GLFW and Wayland resources.
 *   Called during interpreter shutdown.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Destroys all windows, frees the NanoVG context, destroys
 *   Wayland objects, and terminates GLFW.
 * ---------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwCleanup(void)
{
    if (!glfwContext.initialized) return;

    CleanupAllMappings();
    TkWaylandCleanupPixmapStore();

    if (glfwContext.vg) {
        glfwMakeContextCurrent(glfwContext.mainWindow);
        nvgDeleteGLES2(glfwContext.vg);
        glfwContext.vg = NULL;
        TkWaylandSetNVGContext(NULL);
    }
    if (glfwContext.mainWindow) {
        glfwDestroyWindow(glfwContext.mainWindow);
        glfwContext.mainWindow = NULL;
    }
}

/*
 * ---------------------------------------------------------------
 * TkGlfwBeginDraw --
 *
 *   Begin a drawing operation on a drawable.  Makes the window's GL
 *   context current, clears the framebuffer, and opens a NanoVG frame.
 *
 *   Nested calls on the same window are allowed: the inner caller
 *   receives a context marked nestedFrame=1 and must not call
 *   TkGlfwEndDraw (the outer frame owns flush/swap).
 *
 * Results:
 *   TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *   Makes the GL context current, clears the framebuffer, and begins
 *   a NanoVG frame. Updates the drawing context structure with window
 *   dimensions and NanoVG context.
 * ---------------------------------------------------------------
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

    /* Populate common fields regardless of nesting. */
    dcPtr->drawable   = drawable;
    dcPtr->glfwWindow = mapping->glfwWindow;
    dcPtr->width      = mapping->width;
    dcPtr->height     = mapping->height;
    dcPtr->vg         = glfwContext.vg;
    dcPtr->nestedFrame = 0;

    /* If a frame is already open on this window, nest inside it. */
    if (glfwContext.nvgFrameActive &&
            glfwContext.activeWindow == mapping->glfwWindow) {
        dcPtr->nestedFrame = 1;
        if (gc) TkGlfwApplyGC(glfwContext.vg, gc);
        return TCL_OK;
    }

    /*
     * A frame is active on a *different* window — flush it first.
     * This is an unusual path (drawing to two windows in the same
     * call chain) but handle it cleanly.
     */
    if (glfwContext.nvgFrameActive) {
        nvgEndFrame(glfwContext.vg);
        glfwSwapBuffers(glfwContext.activeWindow);
        glfwContext.nvgFrameActive = 0;
        glfwContext.activeWindow   = NULL;
    }

    /* Make context current and get actual framebuffer dimensions. */
    glfwMakeContextCurrent(mapping->glfwWindow);
    glfwGetFramebufferSize(mapping->glfwWindow, &fbWidth, &fbHeight);

    glViewport(0, 0, fbWidth, fbHeight);
    glClearColor(0.92f, 0.92f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    nvgBeginFrame(glfwContext.vg,
                  (float)mapping->width,
                  (float)mapping->height,
                  (float)fbWidth / (float)mapping->width);
    nvgSave(glfwContext.vg);
    nvgTranslate(glfwContext.vg, 0.5f, 0.5f);

    glfwContext.nvgFrameActive = 1;
    glfwContext.activeWindow   = mapping->glfwWindow;

    if (gc) TkGlfwApplyGC(glfwContext.vg, gc);

    return TCL_OK;
}

/*
 * ---------------------------------------------------------------
 * TkGlfwEndDraw --
 *
 *   End a drawing operation. Ends the NanoVG frame and swaps buffers.
 *   No-ops for nested frames.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Ends the NanoVG frame and swaps buffers for non-nested frames.
 * ---------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr)
{
    if (!dcPtr || !dcPtr->vg || dcPtr->nestedFrame) return;

    nvgRestore(dcPtr->vg);
    nvgEndFrame(dcPtr->vg);

    glfwContext.nvgFrameActive = 0;
    glfwContext.activeWindow   = NULL;

    if (dcPtr->glfwWindow)
        glfwSwapBuffers(dcPtr->glfwWindow);
}

/*
 * ---------------------------------------------------------------
 * TkGlfwGetNVGContext --
 *
 *   Return the global NanoVG context.  Callers that need to do font
 *   measurement outside a BeginDraw/EndDraw pair must ensure a GL
 *   context is current themselves (use TkGlfwGetNVGContextForMeasure).
 *
 * Results:
 *   The NVGcontext pointer, or NULL if not yet initialized.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext *
TkGlfwGetNVGContext(void)
{
    return glfwContext.initialized ? glfwContext.vg : NULL;
}

/*
 * ---------------------------------------------------------------
 * TkGlfwGetNVGContextForMeasure --
 *
 *   Return the NanoVG context with the shared GL context current,
 *   suitable for font measurement outside a draw frame.
 *
 * Results:
 *   Returns the NanoVG context or NULL on failure.
 *
 * Side effects:
 *   Makes the shared GL context current if no context is current.
 * ---------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext *
TkGlfwGetNVGContextForMeasure(void)
{
    if (!glfwContext.initialized || !glfwContext.vg) return NULL;
    if (!glfwGetCurrentContext())
        glfwMakeContextCurrent(glfwContext.mainWindow);
    return glfwContext.vg;
}

/*
 * ---------------------------------------------------------------
 * TkGlfwProcessEvents --
 *
 *   Process pending GLFW events. Called from the Tk event loop.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Polls and dispatches GLFW events, flushes the Wayland display.
 * ---------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwProcessEvents(void)
{
    if (glfwContext.initialized) {
        glfwPollEvents();
    }
}

/*
 * ---------------------------------------------------------------
 * Color / GC utilities
 * ---------------------------------------------------------------
 */

/*
 * ---------------------------------------------------------------
 * TkGlfwXColorToNVG --
 *
 *   Convert an XColor structure to an NVGcolor.
 *
 * Results:
 *   Returns an NVGcolor value.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

MODULE_SCOPE NVGcolor
TkGlfwXColorToNVG(XColor *xcolor)
{
    if (!xcolor) return nvgRGBA(0, 0, 0, 255);
    return nvgRGBA(xcolor->red >> 8, xcolor->green >> 8, xcolor->blue >> 8, 255);
}

/*
 * ---------------------------------------------------------------
 * TkGlfwPixelToNVG --
 *
 *   Convert a 24-bit RGB pixel value to an NVGcolor.
 *
 * Results:
 *   Returns an NVGcolor value.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

MODULE_SCOPE NVGcolor
TkGlfwPixelToNVG(unsigned long pixel)
{
    return nvgRGBA((pixel>>16)&0xFF, (pixel>>8)&0xFF, pixel&0xFF, 255);
}

/*
 * ---------------------------------------------------------------
 * TkGlfwApplyGC --
 *
 *   Apply settings from a graphics context to the NanoVG context.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Modifies the NanoVG context's fill color, stroke color, line
 *   width, line caps, and line joins based on the GC values.
 * ---------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwApplyGC(NVGcontext *vg, GC gc)
{
    XGCValues v;
    NVGcolor  c;
    if (!vg || !gc) return;
    TkWaylandGetGCValues(gc,
                         GCForeground|GCLineWidth|GCLineStyle|GCCapStyle|GCJoinStyle, &v);
    c = TkGlfwPixelToNVG(v.foreground);
    nvgFillColor(vg, c);
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, v.line_width > 0 ? (float)v.line_width : 1.0f);
    switch (v.cap_style) {
        case CapRound:      nvgLineCap(vg, NVG_ROUND);  break;
        case CapProjecting: nvgLineCap(vg, NVG_SQUARE); break;
        default:            nvgLineCap(vg, NVG_BUTT);   break;
    }
    switch (v.join_style) {
        case JoinRound: nvgLineJoin(vg, NVG_ROUND); break;
        case JoinBevel: nvgLineJoin(vg, NVG_BEVEL); break;
        default:        nvgLineJoin(vg, NVG_MITER); break;
    }
}

/*
 * ---------------------------------------------------------------
 * Tk platform entry points
 * ---------------------------------------------------------------
 */

/*
 * ---------------------------------------------------------------
 * TkpInit --
 *
 *   Initialize the Tk platform-specific layer for Wayland/GLFW.
 *   Called during interpreter initialization.
 *
 * Results:
 *   TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *   Initializes GLFW, Wayland protocols, NanoVG, and various
 *   Tk extensions (tray, system notifications, printing, accessibility).
 * ---------------------------------------------------------------
 */

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

/*
 * ---------------------------------------------------------------
 * TkpGetAppName --
 *
 *   Extract the application name from argv0 for use in window titles.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Appends the application name to the Tcl_DString.
 * ---------------------------------------------------------------
 */

void
TkpGetAppName(Tcl_Interp *interp, Tcl_DString *namePtr)
{
    const char *p, *name = Tcl_GetVar2(interp, "argv0", NULL, TCL_GLOBAL_ONLY);
    if (!name || !*name) name = "tk";
    else { p = strrchr(name, '/'); if (p) name = p+1; }
    Tcl_DStringAppend(namePtr, name, TCL_INDEX_NONE);
}

/*
 * ---------------------------------------------------------------
 * TkpDisplayWarning --
 *
 *   Display a warning message to stderr.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Writes the warning message to the standard error channel.
 * ---------------------------------------------------------------
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

/*
 * ---------------------------------------------------------------
 * Window mapping list
 * ---------------------------------------------------------------
 */

WindowMapping *
FindMappingByGLFW(GLFWwindow *w)
{
    WindowMapping *c = windowMappingList;
    while (c) { if (c->glfwWindow == w) return c; c = c->nextPtr; }
    return NULL;
}

WindowMapping *
FindMappingByTk(TkWindow *w)
{
    WindowMapping *c = windowMappingList;
    while (c) { if (c->tkWindow == w) return c; c = c->nextPtr; }
    return NULL;
}

WindowMapping *
FindMappingByDrawable(Drawable d)
{
    WindowMapping *c = windowMappingList;
    while (c) { if (c->drawable == d) return c; c = c->nextPtr; }
    return NULL;
}

void
RemoveMapping(WindowMapping *m)
{
    WindowMapping **pp = &windowMappingList;
    while (*pp) {
        if (*pp == m) { *pp = m->nextPtr; ckfree((char *)m); return; }
        pp = &(*pp)->nextPtr;
    }
}

void
CleanupAllMappings(void)
{
    WindowMapping *c = windowMappingList, *n;
    while (c) {
        n = c->nextPtr;
        if (c->glfwWindow) glfwDestroyWindow(c->glfwWindow);
        ckfree((char *)c);
        c = n;
    }
    windowMappingList = NULL;
}

/*
 * ---------------------------------------------------------------
 * Miscellaneous accessors
 * ---------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *
TkGlfwGetGLFWWindow(Tk_Window tkwin)
{
    WindowMapping *m = FindMappingByTk((TkWindow *)tkwin);
    return m ? m->glfwWindow : NULL;
}

MODULE_SCOPE Drawable
TkGlfwGetDrawable(GLFWwindow *w)
{
    WindowMapping *m = FindMappingByGLFW(w);
    return m ? m->drawable : 0;
}

MODULE_SCOPE void
TkGlfwResizeWindow(GLFWwindow *w, int width, int height)
{
    WindowMapping *m = FindMappingByGLFW(w);
    if (m) { m->width = width; m->height = height; }
}

MODULE_SCOPE void
TkGlfwUpdateWindowSize(GLFWwindow *glfwWindow, int width, int height)
{
    WindowMapping *m = FindMappingByGLFW(glfwWindow);
    if (m) { m->width = width; m->height = height; }
}

MODULE_SCOPE GLFWwindow *
TkGlfwGetWindowFromDrawable(Drawable drawable)
{
    WindowMapping *m = FindMappingByDrawable(drawable);
    return m ? m->glfwWindow : NULL;
}

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
