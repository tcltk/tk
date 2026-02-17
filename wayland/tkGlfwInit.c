/*
 * tkGlfwInit.c --
 *
 *	This file contains GLFW/Wayland-specific interpreter initialization 
 *      functions. This file centralizes initialization, window management, 
 *      and context handling.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
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

extern void TkpFontPkgInit(TkMainInfo *);

/*
 *----------------------------------------------------------------------
 *
 * Global State
 *
 *----------------------------------------------------------------------
 */

static TkGlfwContext glfwContext = {NULL, NULL, 0};
static WindowMapping *windowMappingList = NULL;
static Drawable nextDrawableId = 1000;  /* Start from 1000 to avoid conflicts. */

/*
 *----------------------------------------------------------------------
 *
 * Forward Declarations
 *
 *----------------------------------------------------------------------
 */

static WindowMapping* FindMappingByGLFW(GLFWwindow *glfwWindow);
static WindowMapping* FindMappingByTk(TkWindow *tkWin);
static WindowMapping* FindMappingByDrawable(Drawable drawable);
static void RemoveMapping(WindowMapping *mapping);
static void CleanupAllMappings(void);

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetContext --
 *
 *      Get the global GLFW context structure.
 *
 * Results:
 *      Pointer to global context.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkGlfwContext*
TkGlfwGetContext(void)
{
    return &glfwContext;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwErrorCallback --
 *
 *      GLFW error callback for debugging.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Prints error messages to stderr.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwErrorCallback(
    int error,
    const char *description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwInitialize --
 *
 *      Initialize GLFW and create global NanoVG context.
 *
 * Results:
 *      TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *      Initializes GLFW, creates shared context window and NanoVG context.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwInitialize(void)
{
    if (glfwContext.initialized) {
        return TCL_OK;
    }

    if (!glfwInit()) {
        fprintf(stderr, "GLFW init failed\n");
        return TCL_ERROR;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

#ifdef GLFW_PLATFORM_WAYLAND
    glfwWindowHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif

    glfwContext.mainWindow =
        glfwCreateWindow(640, 480, "Tk Shared Context", NULL, NULL);

    if (!glfwContext.mainWindow) {
        fprintf(stderr, "Failed to create shared context window\n");
        glfwTerminate();
        return TCL_ERROR;
    }

    glfwMakeContextCurrent(glfwContext.mainWindow);

    glfwSwapInterval(1);

    glfwContext.vg = nvgCreateGLES2(
        NVG_ANTIALIAS |
        NVG_STENCIL_STROKES
    );

    if (!glfwContext.vg) {
        fprintf(stderr, "Failed to create NanoVG context\n");
        glfwDestroyWindow(glfwContext.mainWindow);
        glfwTerminate();
        return TCL_ERROR;
    }

    glfwContext.initialized = 1;

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * TkGlfwCleanup --
 *
 *      Cleanup GLFW and NanoVG resources.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Destroys all windows, NanoVG context, and terminates GLFW.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwCleanup(void)
{
    if (!glfwContext.initialized) {
        return;
    }

    /* Clean up all window mappings. */
    CleanupAllMappings();

    /* Clean up NanoVG. */
    if (glfwContext.vg) {
        nvgDeleteGLES2(glfwContext.vg);
        glfwContext.vg = NULL;
    }

    /* Clean up GLFW. */
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
 *      Create a GLFW window and register mapping.
 *
 * Results:
 *      GLFWwindow pointer, or NULL on failure.
 *      If drawableOut is non-NULL, stores the allocated drawable ID.
 *
 * Side effects:
 *      Creates window, adds to mapping list.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow*
TkGlfwCreateWindow(
    TkWindow *tkWin,
    int width,
    int height,
    const char *title,
    Drawable *drawableOut)
{
    WindowMapping *mapping;
    GLFWwindow *window;

    if (!glfwContext.initialized) {
        if (TkGlfwInitialize() != TCL_OK) {
            return NULL;
        }
    }

    /* Check if mapping already exists. */
    mapping = FindMappingByTk(tkWin);
    if (mapping) {
        if (drawableOut) {
            *drawableOut = mapping->drawable;
        }
        return mapping->glfwWindow;
    }

    /* Create new GLFW window sharing context. */
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    window = glfwCreateWindow(width, height, title, 
                               NULL, glfwContext.mainWindow);
    
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        return NULL;
    }

    /* Create and register mapping. */
    mapping = (WindowMapping*)ckalloc(sizeof(WindowMapping));
    mapping->tkWindow = tkWin;
    mapping->glfwWindow = window;
    mapping->drawable = nextDrawableId++;
    mapping->width = width;
    mapping->height = height;
    mapping->nextPtr = windowMappingList;
    windowMappingList = mapping;

    /* Store mapping in GLFW user pointer. */
    glfwSetWindowUserPointer(window, mapping);

    /* Set up callbacks. */
    TkGlfwSetupCallbacks(window, tkWin);

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
 *      Destroy a GLFW window and remove mapping.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Destroys window, removes from mapping list.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwDestroyWindow(GLFWwindow *glfwWindow)
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
 *      Get GLFW window from Tk window.
 *
 * Results:
 *      GLFWwindow pointer or NULL.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow*
TkGlfwGetGLFWWindow(Tk_Window tkwin)
{
    WindowMapping *mapping = FindMappingByTk((TkWindow*)tkwin);
    return mapping ? mapping->glfwWindow : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetTkWindow --
 *
 *      Get Tk window from GLFW window.
 *
 * Results:
 *      TkWindow pointer or NULL.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkWindow*
TkGlfwGetTkWindow(GLFWwindow *glfwWindow)
{
    WindowMapping *mapping = FindMappingByGLFW(glfwWindow);
    return mapping ? mapping->tkWindow : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetWindowFromDrawable --
 *
 *      Get GLFW window from drawable ID.
 *
 * Results:
 *      GLFWwindow pointer or NULL.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow*
TkGlfwGetWindowFromDrawable(Drawable drawable)
{
    WindowMapping *mapping = FindMappingByDrawable(drawable);
    return mapping ? mapping->glfwWindow : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwUpdateWindowSize --
 *
 *      Update window size in mapping.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates stored size.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwUpdateWindowSize(
    GLFWwindow *glfwWindow,
    int width,
    int height)
{
    WindowMapping *mapping = FindMappingByGLFW(glfwWindow);
    if (mapping) {
        mapping->width = width;
        mapping->height = height;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwBeginDraw --
 *
 *      Set up drawing context for a drawable.
 *
 * Results:
 *      TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *      Makes GLFW window current, begins NanoVG frame, applies GC settings.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwBeginDraw(
    Drawable drawable,
    GC gc,
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

    dcPtr->drawable = drawable;
    dcPtr->glfwWindow = mapping->glfwWindow;
    dcPtr->width = mapping->width;
    dcPtr->height = mapping->height;
    dcPtr->vg = glfwContext.vg;

    /* Make window current. */
    glfwMakeContextCurrent(mapping->glfwWindow);

    /* Begin NanoVG frame. */
    nvgBeginFrame(dcPtr->vg, dcPtr->width, dcPtr->height, 1.0f);

    /* Apply GC settings */
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
 *      Clean up and present drawing context.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Ends NanoVG frame, swaps buffers.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr)
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
 *      Get global NanoVG context.
 *
 * Results:
 *      NVGcontext pointer or NULL.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext*
TkGlfwGetNVGContext(void)
{
    if (!glfwContext.initialized) {
        if (TkGlfwInitialize() != TCL_OK) {
            return NULL;
        }
    }

    /* Ensure shared context is current. */
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
 *      Process pending GLFW events.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Processes all pending events.
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
 * TkGlfwXColorToNVG --
 *
 *      Convert XColor to NVGcolor.
 *
 * Results:
 *      NVGcolor structure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcolor
TkGlfwXColorToNVG(XColor *xcolor)
{
    if (!xcolor) {
        return nvgRGBA(0, 0, 0, 255);
    }
    
    return nvgRGBA(
        xcolor->red >> 8,
        xcolor->green >> 8,
        xcolor->blue >> 8,
        255
    );
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwPixelToNVG --
 *
 *      Convert pixel value to NVGcolor.
 *
 * Results:
 *      NVGcolor structure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcolor
TkGlfwPixelToNVG(unsigned long pixel)
{
    return nvgRGBA(
        (pixel >> 16) & 0xFF,
        (pixel >> 8) & 0xFF,
        pixel & 0xFF,
        255
    );
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwApplyGC --
 *
 *      Apply GC settings to NanoVG context.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Sets NanoVG state based on GC.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwApplyGC(NVGcontext *vg, GC gc)
{
    XGCValues values;
    NVGcolor color;

    if (!vg || !gc) {
        return;
    }

    /* Get GC values. */
    XGetGCValues(NULL, gc, 
                 GCForeground | GCLineWidth | GCLineStyle | 
                 GCCapStyle | GCJoinStyle,
                 &values);

    /* Set colors. */
    color = TkGlfwPixelToNVG(values.foreground);
    nvgFillColor(vg, color);
    nvgStrokeColor(vg, color);

    /* Set line width. */
    nvgStrokeWidth(vg, values.line_width > 0 ? values.line_width : 1.0f);

    /* Set cap style. */
    switch (values.cap_style) {
    case CapButt:
        nvgLineCap(vg, NVG_BUTT);
        break;
    case CapRound:
        nvgLineCap(vg, NVG_ROUND);
        break;
    case CapProjecting:
        nvgLineCap(vg, NVG_SQUARE);
        break;
    }

    /* Set join style. */
    switch (values.join_style) {
    case JoinMiter:
        nvgLineJoin(vg, NVG_MITER);
        break;
    case JoinRound:
        nvgLineJoin(vg, NVG_ROUND);
        break;
    case JoinBevel:
        nvgLineJoin(vg, NVG_BEVEL);
        break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Helper Functions for Window Mapping
 *
 *----------------------------------------------------------------------
 */

static WindowMapping*
FindMappingByGLFW(GLFWwindow *glfwWindow)
{
    WindowMapping *current = windowMappingList;
    
    while (current) {
        if (current->glfwWindow == glfwWindow) {
            return current;
        }
        current = current->nextPtr;
    }
    
    return NULL;
}

static WindowMapping*
FindMappingByTk(TkWindow *tkWin)
{
    WindowMapping *current = windowMappingList;
    
    while (current) {
        if (current->tkWindow == tkWin) {
            return current;
        }
        current = current->nextPtr;
    }
    
    return NULL;
}

static WindowMapping*
FindMappingByDrawable(Drawable drawable)
{
    WindowMapping *current = windowMappingList;
    
    while (current) {
        if (current->drawable == drawable) {
            return current;
        }
        current = current->nextPtr;
    }
    
    return NULL;
}

static void
RemoveMapping(WindowMapping *mapping)
{
    WindowMapping **prevPtr = &windowMappingList;
    WindowMapping *current = windowMappingList;
    
    while (current) {
        if (current == mapping) {
            *prevPtr = current->nextPtr;
            ckfree((char*)current);
            return;
        }
        prevPtr = &current->nextPtr;
        current = current->nextPtr;
    }
}

static void
CleanupAllMappings(void)
{
    WindowMapping *current = windowMappingList;
    WindowMapping *next;
    
    while (current) {
        next = current->nextPtr;
        if (current->glfwWindow) {
            glfwDestroyWindow(current->glfwWindow);
        }
        ckfree((char*)current);
        current = next;
    }
    
    windowMappingList = NULL;
}


/* Initialization functions. */

/*
 *----------------------------------------------------------------------
 *
 * TkpInit --
 *
 *	Performs GLFW/Wayland-specific interpreter initialization related to the
 *      tk_library variable.
 *
 * Results:
 *	Returns a standard Tcl result. Leaves an error message or result in
 *	the interp's result.
 *
 * Side effects:
 *	Sets "tk_library" Tcl variable, runs "tk.tcl" script.
 *	Initializes GLFW and nanovg.
 *
 *----------------------------------------------------------------------
 */

int
TkpInit(
    Tcl_Interp *interp)
{

    /* Initialize GLFW/NanoVG */
    if (TkGlfwInitialize() != TCL_OK) {
        return TCL_ERROR;
    }
    
    /* Initialize fonts. */
	TkpFontPkgInit(NULL);

    /* Initialize menu. */
    TkWaylandMenuInit();
       
    /* Initialize event loop. */
    Tk_WaylandSetupTkNotifier();
    
    /* Initialize subsystems. */
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
 *	Retrieves the name of the current application from a platform specific
 *	location. For GLFW/Wayland, the application name is the tail of the path
 *	contained in the tcl variable argv0.
 *
 * Results:
 *	Returns the application name in the given Tcl_DString.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetAppName(
    Tcl_Interp *interp,
    Tcl_DString *namePtr)	/* A previously initialized Tcl_DString. */
{
    const char *p, *name;

    name = Tcl_GetVar2(interp, "argv0", NULL, TCL_GLOBAL_ONLY);
    if ((name == NULL) || (*name == 0)) {
	name = "tk";
    } else {
	p = strrchr(name, '/');
	if (p != NULL) {
	    name = p+1;
	}
    }
    Tcl_DStringAppend(namePtr, name, TCL_INDEX_NONE);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayWarning --
 *
 *	This routines is called from Tk_Main to display warning messages that
 *	occur during startup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Generates messages on stdout.
 *
 *----------------------------------------------------------------------
 */

void
TkpDisplayWarning(
    const char *msg,		/* Message to be displayed. */
    const char *title)		/* Title of warning. */
{
    Tcl_Channel errChannel = Tcl_GetStdChannel(TCL_STDERR);

    if (errChannel) {
	Tcl_WriteChars(errChannel, title, TCL_INDEX_NONE);
	Tcl_WriteChars(errChannel, ": ", 2);
	Tcl_WriteChars(errChannel, msg, TCL_INDEX_NONE);
	Tcl_WriteChars(errChannel, "\n", 1);
    }
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
