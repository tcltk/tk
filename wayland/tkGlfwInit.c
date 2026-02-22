/*
 * tkGlfwInit.c --
 *
 *	GLFW/Wayland-specific interpreter initialisation: context
 *	management, window mapping, drawing context lifecycle, color
 *	conversion, and platform init/cleanup.
 *
 *	This file owns the global TkGlfwContext and the WindowMapping
 *	linked list.  It provides all TkGlfw* entry points declared in
 *	tkGlfwInt.h.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026      Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#define GL_GLEXT_PROTOTYPES

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>
#include <GLES2/gl2.h>

#define NANOVG_GLES2_IMPLEMENTATION
#include "nanovg_gl.h"
#include "nanovg.h"

/*
 *----------------------------------------------------------------------
 *
 * Module-level state
 *
 *----------------------------------------------------------------------
 */

static TkGlfwContext  glfwContext        = {NULL, NULL, 0};
static WindowMapping *windowMappingList  = NULL;
static Drawable       nextDrawableId     = 1000; /* avoid zero/conflicts */

/*
 *----------------------------------------------------------------------
 *
 * Static helpers – window mapping list
 *
 *----------------------------------------------------------------------
 */

static WindowMapping *FindMappingByGLFW(GLFWwindow *glfwWindow);
static WindowMapping *FindMappingByTk(TkWindow *tkWin);
static WindowMapping *FindMappingByDrawable(Drawable drawable);
static void           RemoveMapping(WindowMapping *mapping);
static void           CleanupAllMappings(void);

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetContext --
 *
 *	Returns a pointer to the global GLFW context structure.
 *
 * Results:
 *	Pointer to the global TkGlfwContext structure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkGlfwContext *
TkGlfwGetContext(void)
{
    return &glfwContext;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwErrorCallback --
 *
 *	Callback function for GLFW error reporting. Prints error
 *	messages to stderr.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes error messages to stderr.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwErrorCallback(
    int         error,
    const char *description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwInitialize --
 *
 *	Initialize GLFW and create the shared NanoVG context.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initialises GLFW; creates shared context window and NanoVG context.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwInitialize(void)
{
    if (glfwContext.initialized) {
        return TCL_OK;
    }

	glfwSetErrorCallback(TkGlfwErrorCallback);
	
	#ifdef GLFW_PLATFORM_WAYLAND
		glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
	#endif
	
	if (!glfwInit()) {
	    fprintf(stderr, "GLFW init failed\n");
	    return TCL_ERROR;
	}

    glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);


    glfwContext.mainWindow =
        glfwCreateWindow(640, 480, "Tk Shared Context", NULL, NULL);

    if (!glfwContext.mainWindow) {
        fprintf(stderr, "Failed to create shared context window\n");
        glfwTerminate();
        return TCL_ERROR;
    }

    glfwMakeContextCurrent(glfwContext.mainWindow);
    glfwSwapInterval(1);

    glfwContext.vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!glfwContext.vg) {
        fprintf(stderr, "Failed to create NanoVG context\n");
        glfwDestroyWindow(glfwContext.mainWindow);
        glfwContext.mainWindow = NULL;
        glfwTerminate();
        return TCL_ERROR;
    }

    /* Register the NanoVG context for pixmap operations. */
    TkWaylandSetNVGContext(glfwContext.vg);

    glfwContext.initialized = 1;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwCleanup --
 *
 *	Clean up all GLFW resources and the NanoVG context. Destroys all
 *	windows and terminates GLFW.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All GLFW windows are destroyed; GLFW terminated; NanoVG context freed.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwCleanup(void)
{
    if (!glfwContext.initialized) {
        return;
    }

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

    glfwTerminate();
    glfwContext.initialized = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwCreateWindow --
 *
 *	Create a GLFW window and register a mapping entry.
 *
 * Results:
 *	GLFWwindow pointer, or NULL on failure.
 *	If drawableOut is non-NULL, stores the allocated Drawable ID.
 *
 * Side effects:
 *	Allocates a WindowMapping entry; sets up GLFW callbacks.
 *
 *----------------------------------------------------------------------
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
        if (TkGlfwInitialize() != TCL_OK) {
            return NULL;
        }
    }

    /* Re-use an existing mapping for this TkWindow if present. */
    if (tkWin != NULL) {
        mapping = FindMappingByTk(tkWin);
        if (mapping != NULL) {
            if (drawableOut) {
                *drawableOut = mapping->drawable;
            }
            return mapping->glfwWindow;
        }
    }

    /* Ensure sensible minimum dimensions. */
    if (width  <= 0) width  = 200;
    if (height <= 0) height = 200;

    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    window = glfwCreateWindow(width, height, title ? title : "",
                               NULL, glfwContext.mainWindow);
    //debug
    fprintf(stderr, "TkGlfwCreateWindow: created window %p for %s, visible=%d\n", 
        window, title ? title : "unknown", 
        glfwGetWindowAttrib(window, GLFW_VISIBLE));
        if (window) {
    /* Initial event pump to ensure window is properly initialized */
    glfwPollEvents();
}                           
    if (!window) {
        fprintf(stderr, "TkGlfwCreateWindow: glfwCreateWindow failed\n");
        return NULL;
    }

    mapping               = (WindowMapping *)ckalloc(sizeof(WindowMapping));
    mapping->tkWindow     = tkWin;
    mapping->glfwWindow   = window;
    mapping->drawable     = nextDrawableId++;
    mapping->width        = width;
    mapping->height       = height;
    mapping->nextPtr      = windowMappingList;
    windowMappingList     = mapping;

    glfwSetWindowUserPointer(window, mapping);

    if (tkWin != NULL) {
        TkGlfwSetupCallbacks(window, tkWin);
    }

    if (drawableOut) {
        *drawableOut = mapping->drawable;
    }

    return window;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwDestroyWindow --
 *
 *	Destroy a GLFW window and remove its mapping entry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The GLFW window is destroyed; its mapping entry is freed.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwDestroyWindow(
    GLFWwindow *glfwWindow)
{
    WindowMapping *mapping;

    if (!glfwWindow) {
        return;
    }

    mapping = FindMappingByGLFW(glfwWindow);
    if (mapping) {
        RemoveMapping(mapping);
    }

    glfwDestroyWindow(glfwWindow);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetGLFWWindow --
 *
 *	Returns the GLFW window associated with a Tk window.
 *
 * Results:
 *	GLFWwindow pointer, or NULL if no mapping exists.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *
TkGlfwGetGLFWWindow(
    Tk_Window tkwin)
{
    WindowMapping *m = FindMappingByTk((TkWindow *)tkwin);
    return m ? m->glfwWindow : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetTkWindow --
 *
 *	Returns the Tk window associated with a GLFW window.
 *
 * Results:
 *	TkWindow pointer, or NULL if no mapping exists.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkWindow *
TkGlfwGetTkWindow(
    GLFWwindow *glfwWindow)
{
    WindowMapping *m = FindMappingByGLFW(glfwWindow);
    return m ? m->tkWindow : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetWindowFromDrawable --
 *
 *	Returns the GLFW window associated with a drawable ID.
 *
 * Results:
 *	GLFWwindow pointer, or NULL if no mapping exists.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *
TkGlfwGetWindowFromDrawable(
    Drawable drawable)
{
    WindowMapping *m = FindMappingByDrawable(drawable);
    return m ? m->glfwWindow : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwUpdateWindowSize --
 *
 *	Updates the stored dimensions for a GLFW window in the mapping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The width and height fields in the window mapping are updated.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwUpdateWindowSize(
    GLFWwindow *glfwWindow,
    int         width,
    int         height)
{
    WindowMapping *m = FindMappingByGLFW(glfwWindow);
    if (m) {
        m->width  = width;
        m->height = height;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwBeginDraw --
 *
 *	Set up a drawing context for the given drawable.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR if the drawable has no mapping.
 *
 * Side effects:
 *	Makes the GLFW window current; opens a NanoVG frame; applies GC.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwBeginDraw(
    Drawable                drawable,
    GC                      gc,
    TkWaylandDrawingContext *dcPtr)
{
    WindowMapping *mapping;

    if (!dcPtr) {
        return TCL_ERROR;
    }

    mapping = FindMappingByDrawable(drawable);
    if (!mapping || !mapping->glfwWindow) {
        return TCL_ERROR;
    }

    dcPtr->drawable   = drawable;
    dcPtr->glfwWindow = mapping->glfwWindow;
    dcPtr->width      = mapping->width;
    dcPtr->height     = mapping->height;
    dcPtr->vg         = glfwContext.vg;

    glfwMakeContextCurrent(mapping->glfwWindow);

    nvgBeginFrame(dcPtr->vg, (float)dcPtr->width, (float)dcPtr->height, 1.0f);

    if (gc) {
        TkGlfwApplyGC(dcPtr->vg, gc);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwEndDraw --
 *
 *	Completes the drawing operation for a drawing context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Ends the NanoVG frame; swaps buffers for the associated GLFW window.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwEndDraw(
    TkWaylandDrawingContext *dcPtr)
{
    if (!dcPtr || !dcPtr->vg) {
        return;
    }

    nvgEndFrame(dcPtr->vg);

    if (dcPtr->glfwWindow) {
        glfwSwapBuffers(dcPtr->glfwWindow);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetNVGContext --
 *
 *	Returns the shared NanoVG context, initialising if necessary.
 *
 * Results:
 *	NVGcontext pointer, or NULL on initialization failure.
 *
 * Side effects:
 *	May initialise GLFW and the NanoVG context if not already done.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext *
TkGlfwGetNVGContext(void)
{
    if (!glfwContext.initialized) {
        if (TkGlfwInitialize() != TCL_OK) {
            return NULL;
        }
    }

    if (glfwGetCurrentContext() == NULL) {
        glfwMakeContextCurrent(glfwContext.mainWindow);
    }

    return glfwContext.vg;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwProcessEvents --
 *
 *	Processes pending GLFW events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls glfwPollEvents() to process window events.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwProcessEvents(void)
{
    if (glfwContext.initialized) {
        glfwPollEvents();
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Color conversion utilities
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwXColorToNVG --
 *
 *	Converts an XColor structure to an NVGcolor.
 *
 * Results:
 *	NVGcolor value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcolor
TkGlfwXColorToNVG(
    XColor *xcolor)
{
    if (!xcolor) {
        return nvgRGBA(0, 0, 0, 255);
    }
    return nvgRGBA(
        xcolor->red   >> 8,
        xcolor->green >> 8,
        xcolor->blue  >> 8,
        255);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwPixelToNVG --
 *
 *	Converts an X pixel value to an NVGcolor.
 *
 * Results:
 *	NVGcolor value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcolor
TkGlfwPixelToNVG(
    unsigned long pixel)
{
    return nvgRGBA(
        (pixel >> 16) & 0xFF,
        (pixel >>  8) & 0xFF,
         pixel        & 0xFF,
        255);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwApplyGC --
 *
 *	Translate a GC into NanoVG state.
 *
 *	Uses the canonical TkWaylandGetGCValues entry point; never
 *	casts the GC pointer directly.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets fill/stroke colour, line width, cap, and join on vg.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwApplyGC(
    NVGcontext *vg,
    GC          gc)
{
    XGCValues values;
    NVGcolor  color;

    if (!vg || !gc) {
        return;
    }

    /* Use the canonical getter – never cast gc directly. */
    TkWaylandGetGCValues(gc,
        GCForeground | GCLineWidth | GCLineStyle |
        GCCapStyle   | GCJoinStyle,
        &values);

    color = TkGlfwPixelToNVG(values.foreground);
    nvgFillColor(vg, color);
    nvgStrokeColor(vg, color);

    nvgStrokeWidth(vg, values.line_width > 0
        ? (float)values.line_width : 1.0f);

    switch (values.cap_style) {
    case CapRound:       nvgLineCap(vg, NVG_ROUND);  break;
    case CapProjecting:  nvgLineCap(vg, NVG_SQUARE); break;
    default:             nvgLineCap(vg, NVG_BUTT);   break;
    }

    switch (values.join_style) {
    case JoinRound:  nvgLineJoin(vg, NVG_ROUND); break;
    case JoinBevel:  nvgLineJoin(vg, NVG_BEVEL); break;
    default:         nvgLineJoin(vg, NVG_MITER); break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Platform initialisation entry points (called from TkpInit)
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * TkpInit --
 *
 *	Platform-specific initialisation for Tk on Wayland/GLFW.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initialises GLFW, Wayland menu system, notifier, and various
 *	Tk extensions (tray, system notification, CUPS, accessibility).
 *
 *----------------------------------------------------------------------
 */

int
TkpInit(
    Tcl_Interp *interp)
{
    if (TkGlfwInitialize() != TCL_OK) {
        return TCL_ERROR;
    }

    TkWaylandMenuInit();
    Tk_WaylandSetupTkNotifier();

    Tktray_Init(interp);
    SysNotify_Init(interp);
    Cups_Init(interp);
    TkAtkAccessibility_Init(interp);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetAppName --
 *
 *	Extracts the application name from argv0.
 *
 * Results:
 *	None. The application name is appended to namePtr.
 *
 * Side effects:
 *	Modifies the Tcl_DString.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetAppName(
    Tcl_Interp  *interp,
    Tcl_DString *namePtr)
{
    const char *p, *name;

    name = Tcl_GetVar2(interp, "argv0", NULL, TCL_GLOBAL_ONLY);
    if ((name == NULL) || (*name == 0)) {
        name = "tk";
    } else {
        p = strrchr(name, '/');
        if (p != NULL) {
            name = p + 1;
        }
    }
    Tcl_DStringAppend(namePtr, name, TCL_INDEX_NONE);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayWarning --
 *
 *	Displays a warning message on the standard error channel.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes the warning message to stderr.
 *
 *----------------------------------------------------------------------
 */

void
TkpDisplayWarning(
    const char *msg,
    const char *title)
{
    Tcl_Channel errChannel = Tcl_GetStdChannel(TCL_STDERR);

    if (errChannel) {
        Tcl_WriteChars(errChannel, title,  TCL_INDEX_NONE);
        Tcl_WriteChars(errChannel, ": ",   2);
        Tcl_WriteChars(errChannel, msg,    TCL_INDEX_NONE);
        Tcl_WriteChars(errChannel, "\n",   1);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Static helpers – window mapping list implementation
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * FindMappingByGLFW --
 *
 *	Finds a window mapping entry by GLFW window pointer.
 *
 * Results:
 *	WindowMapping pointer, or NULL if not found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static WindowMapping *
FindMappingByGLFW(
    GLFWwindow *glfwWindow)
{
    WindowMapping *cur = windowMappingList;
    while (cur) {
        if (cur->glfwWindow == glfwWindow) return cur;
        cur = cur->nextPtr;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FindMappingByTk --
 *
 *	Finds a window mapping entry by Tk window pointer.
 *
 * Results:
 *	WindowMapping pointer, or NULL if not found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static WindowMapping *
FindMappingByTk(
    TkWindow *tkWin)
{
    WindowMapping *cur = windowMappingList;
    while (cur) {
        if (cur->tkWindow == tkWin) return cur;
        cur = cur->nextPtr;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FindMappingByDrawable --
 *
 *	Finds a window mapping entry by drawable ID.
 *
 * Results:
 *	WindowMapping pointer, or NULL if not found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static WindowMapping *
FindMappingByDrawable(
    Drawable drawable)
{
    WindowMapping *cur = windowMappingList;
    while (cur) {
        if (cur->drawable == drawable) return cur;
        cur = cur->nextPtr;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * RemoveMapping --
 *
 *	Removes a window mapping entry from the linked list and frees it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The mapping is removed from the list and its memory is freed.
 *
 *----------------------------------------------------------------------
 */

static void
RemoveMapping(
    WindowMapping *mapping)
{
    WindowMapping **prevPtr = &windowMappingList;
    WindowMapping  *cur    = windowMappingList;

    while (cur) {
        if (cur == mapping) {
            *prevPtr = cur->nextPtr;
            ckfree((char *)cur);
            return;
        }
        prevPtr = &cur->nextPtr;
        cur     = cur->nextPtr;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CleanupAllMappings --
 *
 *	Destroys all GLFW windows and frees all window mapping entries.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All GLFW windows are destroyed; all mapping memory is freed.
 *
 *----------------------------------------------------------------------
 */

static void
CleanupAllMappings(void)
{
    WindowMapping *cur  = windowMappingList;
    WindowMapping *next;

    while (cur) {
        next = cur->nextPtr;
        if (cur->glfwWindow) {
            glfwDestroyWindow(cur->glfwWindow);
        }
        ckfree((char *)cur);
        cur = next;
    }

    windowMappingList = NULL;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
