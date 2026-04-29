/*
 * tkGlfwInit.c --
 *
 *   GLFW/Wayland-specific interpreter initialization: context
 *   management, window mapping, drawing context lifecycle, color
 *   conversion, and platform init/cleanup. GLFW, NanoVG and libdecor
 *   provide the native platform on which Tk's widget set and event loop
1*   are deployed.
 *
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

#define NANOVG_GLES3_IMPLEMENTATION
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

/*
 *----------------------------------------------------------------------
 *
 * Module-level state
 *
 *----------------------------------------------------------------------
 */

/*
 * GLFW requires all initialization and event polling to be done
 * on the main thread.
 */

static int GlfwIsInitialized = 0;

/*
  The glfwWindow for the root window
*/

GLFWwindow *mainGlfwWindow;

static TkGlfwContext glfwContext = {NULL, NULL, 0, 0, NULL, 0, 0};
static int shutdownInProgress = 0;

/*
 *----------------------------------------------------------------------
 *
 * Accessors
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
 * Backing Store framebuffers
 *
 *----------------------------------------------------------------------
 */

static NVGLUframebuffer* TkWaylandFBOForTkWindow(
    TkWindow *winPtr,
    int *width,
    int *height) {
    TkWindow *toplevelPtr = winPtr;
    while (!Tk_IsTopLevel(toplevelPtr)) {
	toplevelPtr = toplevelPtr->parentPtr;
    }
    if (width) {
	*width = Tk_Width(toplevelPtr);
    }
    if (height) {
	*height = Tk_Height(toplevelPtr);
    }
    return toplevelPtr->privatePtr->fbo;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwErrorCallback --
 *
 *	GLFW error callback that prints errors to stderr.
 *	Silences errors during shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints error messages to stderr.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwErrorCallback(int error, const char *desc)
{
    /* Don't print errors during shutdown. */
    if (shutdownInProgress) return;

    fprintf(stderr, "GLFW Error %d: %s\n", error, desc);
}


/*
 *----------------------------------------------------------------------
 *
 * TkGlfwInitialize --
 *
 *	Initialize the GLFW library, create a shared context window,
 *	initialize Wayland protocols, and create the global NanoVG context.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initializes GLFW, creates a hidden shared GL context window,
 *	and creates the global NanoVG context.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwInitialize(void)
{
    if (GlfwIsInitialized) return TCL_OK;

    glfwSetErrorCallback(TkGlfwErrorCallback);

#ifdef GLFW_PLATFORM_WAYLAND
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif

    if (!glfwInit()) {
        fprintf(stderr, "TkGlfwInitialize: glfwInit() failed\n");
        return TCL_ERROR;
    }

    /*
     * The glfwWindow for the Tk root is created here.
     * For all other toplevels the glfwWindow shares the GL context
     * of the root.  The window is created hidden.  It will be
     * shown in Tk_MakeWindow.
     */

    /* Hints apply to the next call to glfwCreateWindow. */
    glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE,             GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW,         GLFW_TRUE);
    glfwWindowHint(GLFW_AUTO_ICONIFY,          GLFW_FALSE);
    mainGlfwWindow = glfwCreateWindow(200, 200, "Tk", NULL, NULL);
    if (!mainGlfwWindow) {
        fprintf(stderr, "TkGlfwInitialize: failed to create root window\n");
        glfwTerminate();
        return TCL_ERROR;
    }

    // A positive swap interval causes glfwSwapBuffers to wait for
    // the end of a display cycle before swapping the buffers,
    // and that causes artifacts when resizing windows.
    glfwMakeContextCurrent(mainGlfwWindow);
    glfwSwapInterval(0);

    /* Create one NanoVG context which is shared by all toplevels.  */
    glfwContext.vg = nvgCreateGLES3(NVG_ANTIALIAS
				  | NVG_STENCIL_STROKES
				  | NVG_DEBUG);
    if (!glfwContext.vg) {
        fprintf(stderr, "TkGlfwInitialize: nvgCreateGLES3() failed\n");
        glfwDestroyWindow(mainGlfwWindow);
        mainGlfwWindow = NULL;
        glfwTerminate();
        return TCL_ERROR;
    }

    /* Load core fonts for decoration text rendering. */
    nvgCreateFont(glfwContext.vg, "sans",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    nvgCreateFont(glfwContext.vg, "sans-bold",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
    nvgCreateFont(glfwContext.vg, "mono",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");

    GlfwIsInitialized = 1;
    shutdownInProgress = 0;

    Tcl_CreateExitHandler(TkGlfwShutdown, NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwShutdown --
 *
 *	Orderly cleanup of GLFW resources on app shutdown.
 *	Now safely handles both exit command and root window closure.
 *
 * Results:
 *	GLFW is closed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwShutdown(TCL_UNUSED(void *))
{
    /* Prevent recursive shutdown. */
    if (shutdownInProgress) return;
    shutdownInProgress = 1;

    if (!GlfwIsInitialized) {
        shutdownInProgress = 0;
        return;
    }
    
    /* Delete NanoVG while a context still exists. */
    if (glfwContext.vg) {
        /* Make the GL context of the root current if it still exists. */
        if (mainGlfwWindow) {
            glfwMakeContextCurrent(mainGlfwWindow);
            nvgDeleteGLES3(glfwContext.vg);
        }
        glfwContext.vg = NULL;
    }

    /* Poll one last time to let GLFW clean up internal state. */
    //// Does not really seem to be needed.
    glfwPollEvents();

    glfwMakeContextCurrent(NULL);
    TkGlfwClearCallbacks(mainGlfwWindow);
    glfwSetErrorCallback(NULL);
    if (mainGlfwWindow) {
	// This seems like a good idea but it segfaults!
        //glfwDestroyWindow(mainGlfwWindow);
        mainGlfwWindow = NULL;
    }
    if (GlfwIsInitialized) {
        glfwTerminate();
        GlfwIsInitialized = 0;
    }

    shutdownInProgress = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwCreateWindow --
 *
 *	Create a new GLFW window sharing the global GL context.
 *	Waits for the compositor's first configure event before returning
 *	so that BeginDraw always has valid dimensions.
 *
 * Results:
 *	Returns the GLFWwindow pointer on success, NULL on failure.
 *
 * Side effects:
 *	Creates a new GLFW window, adds it to the window mapping list.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *
TkGlfwCreateWindow(
    TkWindow   *winPtr,
    int         width,
    int         height,
    const char *title,
    Drawable   *drawableOut)
{
    printf("TkGlfwCreateWindow\n");
    if (winPtr == NULL) {
	Tcl_Panic("TkGlfwCreateWindow called with null winPtr\n");
    }
    GLFWwindow    *glfwWindow = NULL;

    /* Don't create windows during shutdown. */
    if (shutdownInProgress) return NULL;

    if (!GlfwIsInitialized) {
	printf("****************************** TkGlfwCreateWindow called before TkGlfwInitialize !\n");
        if (TkGlfwInitialize() != TCL_OK)
            return NULL;
    }

    if (width  <= 1) width  = 200;
    if (height <= 1) height = 200;

    if (winPtr == (TkWindow *) Tk_MainWindow(winPtr->mainPtr->interp)) {
	/* This is the root window. */
        glfwWindow = mainGlfwWindow;
        glfwSetWindowSize(glfwWindow, width, height);
        glfwSetWindowTitle(glfwWindow, title ? title : "");
    } else {
	/* Hints apply to the next call to glfwCreateWindow. */
	glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_ES_API);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);
	glfwWindowHint(GLFW_RESIZABLE,             GLFW_TRUE);
	glfwWindowHint(GLFW_FOCUS_ON_SHOW,         GLFW_TRUE);
	glfwWindowHint(GLFW_AUTO_ICONIFY,          GLFW_FALSE);
        glfwWindow = glfwCreateWindow(width, height, title ? title : "",
                     NULL, mainGlfwWindow); /* Share the GL contexts */
        if (!glfwWindow) {
	    return NULL;
	}
	glfwMakeContextCurrent(glfwWindow);
	glfwSwapInterval(0);
	glfwShowWindow(glfwWindow);
    }

    /* Create a framebuffer for the backing store of the window. */
    glfwMakeContextCurrent(glfwWindow);
    winPtr->privatePtr->fbo = nvgluCreateFramebuffer(glfwContext.vg,
						     width, height, 0);
    if (winPtr->privatePtr->fbo == NULL) {
		fprintf(stderr, "Could not create NanoVG framebuffer\n");
    }
    printf("Window %s now has glfwWindow %p and framebuffer %p\n",
	   Tk_PathName(winPtr), glfwWindow, winPtr->privatePtr->fbo);
    nvgluBindFramebuffer(winPtr->privatePtr->fbo);
    /* Check FBO completeness for now. */
    int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("FBO is incomplete (status=0x%x)\n", status);
    } else {
	printf("Window %s has complete framebuffer %p\n", Tk_PathName(winPtr),
	   winPtr->privatePtr->fbo);
    }

    if (winPtr != NULL) {
        TkGlfwSetupCallbacks(glfwWindow);
	glfwSetWindowUserPointer(glfwWindow, winPtr);
        winPtr->changes.width  = width;
        winPtr->changes.height = height;
    }

    if (drawableOut) {
	*drawableOut = TkWaylandDrawableForTkWindow(winPtr);
    }
    if (winPtr != NULL) {
        TkWaylandQueueExposeEvent(winPtr, 0, 0, width, height);
    }
    return glfwWindow;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwDestroyWindow --
 *
 *	Destroy a GLFW window and clean up associated resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes the window from the mapping list and destroys the GLFW window.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwDestroyWindow(GLFWwindow *glfwWindow)
{
    if (!glfwWindow) {
	return;
    }
    if (shutdownInProgress) {
	return;
    }
    glfwDestroyWindow(glfwWindow);

    /* Check if this was the last window. */
    if (Tk_GetNumMainWindows() == 0 && !shutdownInProgress) {
        /* Schedule shutdown via idle callback. */
        Tcl_DoWhenIdle(TkGlfwShutdown, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwBeginDraw --
 *
 *	Prepares the NanoVG context for drawing. Uses the provided
 * dcPtr to store context-specific state.
 *
 * Results:
 *	TCL_OK if drawing can proceed, TCL_ERROR otherwise.
 *
 * Side effects:
 *      Changes nvg and gl state.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwBeginDraw(
    Drawable drawable,
    GC gc,
    TkWaylandDrawingContext *dcPtr)
{
    printf("TkGlfwBeginDraw: ");
    int width, height;
    TkWindow *winPtr = TkWaylandTkWindowFromDrawable(drawable);
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    NVGLUframebuffer *fbo = TkWaylandFBOForTkWindow(winPtr, &width, &height);
    // Make our GL context current.
    printf("Drawable is %s with glfwWindow %p and framebuffer %p\n",
	   Tk_PathName(winPtr), glfwWindow, fbo);
    glfwMakeContextCurrent(glfwWindow);

    // Bind our backing store framebuffer.
    nvgluBindFramebuffer(fbo);

    /* Check FBO completeness for now. */
    int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("FBO is incomplete (status=0x%x)\n", status);
    }

    /* Set viewport to the FBO size. */

    /* Start a NanoVG frame drawing on the backing store. */
    ////XXXX Watch out for hi-res displays
    nvgBeginFrame(glfwContext.vg, (float) width, (float) height, 1.0f);

    // Set up the drawing context for EndDraw
    dcPtr->vg = glfwContext.vg;
    dcPtr->drawable = drawable;

    /* Compute offsets. */
    if (!Tk_IsTopLevel(winPtr)) {
        float x = 0, y = 0;
        while (winPtr && !Tk_IsTopLevel(winPtr)) {
            x += winPtr->changes.x;
            y += winPtr->changes.y;
            winPtr = winPtr->parentPtr;
        }
        nvgTranslate(dcPtr->vg, x, y);
    }
    TkGlfwApplyGC(dcPtr->vg, gc);
    return TCL_OK;
    }

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwEndDraw --
 *
 *      End a drawing operation.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Pops NanoVG state.
 *      Unbinds pixmap FBO if drawing to pixmap.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void blitFBOToBack(NVGLUframebuffer *fbo, int width, int height) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, width, height,
		      0, 0, width, height,
		      GL_COLOR_BUFFER_BIT,
		      GL_NEAREST);
    glFlush();
    glFinish();
}

MODULE_SCOPE void
TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr)
{
    TkWindow *winPtr = TkWaylandTkWindowFromDrawable(dcPtr->drawable);
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    printf("TkGlfwEndDraw: %s\n", Tk_PathName(winPtr));
    NVGLUframebuffer *fbo = TkWaylandFBOForTkWindow(winPtr, NULL, NULL);
    if (!dcPtr || !dcPtr->vg) {
	printf("No drawing context!\n");
    }
    nvgRestore(dcPtr->vg); // nvgBeginFrame called nvgSave!
    printf("Binding backing store at %p\n", fbo);
    nvgluBindFramebuffer(fbo);
    int fbwidth, fbheight;
    glfwGetFramebufferSize(glfwWindow, &fbwidth, &fbheight);
    glViewport(0, 0, fbwidth, fbheight);
    // All nvg drawing happens here.
    nvgEndFrame(dcPtr->vg);
    blitFBOToBack(fbo, fbwidth, fbheight);
    glfwSwapBuffers(glfwWindow);
    nvgluBindFramebuffer(NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetNVGContext --
 *
 *	Return the global NanoVG context.
 *
 * Results:
 *	The NVGcontext pointer, or NULL if not yet initialized.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext *
TkGlfwGetNVGContext(void)
{
    return (GlfwIsInitialized && !shutdownInProgress) ?
            glfwContext.vg : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetNVGContextForMeasure --
 *
 *	Return the NanoVG context with the shared GL context current,
 *	suitable for font measurement outside a draw frame.
 *
 * Results:
 *	Returns the NanoVG context or NULL on failure.
 *
 * Side effects:
 *	Makes the shared GL context current if no context is current.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext *
TkGlfwGetNVGContextForMeasure(void)
{
    if (!GlfwIsInitialized || !glfwContext.vg || shutdownInProgress)
        return NULL;
    glfwMakeContextCurrent(mainGlfwWindow);
    return glfwContext.vg;
}

/*
 *----------------------------------------------------------------------
 *
 * Color / GC utilities
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwXColorToNVG --
 *
 *	Convert an XColor structure to an NVGcolor.
 *
 * Results:
 *	Returns an NVGcolor value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcolor
TkGlfwXColorToNVG(XColor *xcolor)
{
    if (!xcolor) return nvgRGBA(0, 0, 0, 255);
    return nvgRGBA(xcolor->red >> 8, xcolor->green >> 8, xcolor->blue >> 8, 255);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwPixelToNVG --
 *
 *	Convert a 24-bit RGB pixel value to an NVGcolor.
 *
 * Results:
 *	Returns an NVGcolor value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcolor
TkGlfwPixelToNVG(unsigned long pixel)
{
    return nvgRGBA((pixel>>16)&0xFF, (pixel>>8)&0xFF, pixel&0xFF, 255);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwApplyGC --
 *
 *	Apply settings from a graphics context to the NanoVG context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the NanoVG context's fill color, stroke color, line
 *	width, line caps, and line joins based on the GC values.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwApplyGC(NVGcontext *vg, GC gc)
{
    XGCValues v;
    NVGcolor  c;

    if (!vg || !gc || shutdownInProgress) return;

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
 *----------------------------------------------------------------------
 *
 * Tk platform entry points
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * TkpInit --
 *
 *	Initialize the Tk platform-specific layer for Wayland/GLFW.
 *	Called during interpreter initialization.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initializes GLFW, Wayland protocols, NanoVG, and various
 *	Tk extensions (tray, system notifications, printing, accessibility).
 *
 *----------------------------------------------------------------------
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
    TkWaylandAccessibility_Init(interp);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetAppName --
 *
 *	Extract the application name from argv0 for use in window titles.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Appends the application name to the Tcl_DString.
 *
 *----------------------------------------------------------------------
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
 *----------------------------------------------------------------------
 *
 * TkpDisplayWarning --
 *
 *	Display a warning message to stderr.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes the warning message to the standard error channel.
 *
 *----------------------------------------------------------------------
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
 *----------------------------------------------------------------------
 *
 * TkGlfwGetGLFWwindow --
 *
 *	Retrieves the GLFW window associated with a Tk window.
 *
 * Results:
 *	Window pointer returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow*
TkWaylandGetGLFWwindow(
    TkWindow *winPtr)
{
    TkWindow *toplevelPtr = winPtr;
    while (!Tk_IsTopLevel(toplevelPtr)) {
	toplevelPtr = toplevelPtr->parentPtr;
    }
    if (toplevelPtr->privatePtr) {
	return toplevelPtr->privatePtr->glfwWindow;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetGLFWwindowFromDrawable --
 *
 *	Retrieves the GLFW window associated with a Drawable.
 *
 * Results:
 *	Window pointer returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *
TkWaylandGetGLFWwindowFromDrawable(Drawable drawable)
{
    TkWindow *winPtr = TkWaylandTkWindowFromDrawable(drawable);
    return TkWaylandGetGLFWwindow(winPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetTkWindow --
 *
 *	Retrieves the Tk window associated with a GLFW window.
 *
 * Results:
 *	Window pointer returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkWindow*
TkGlfwGetTkWindow(GLFWwindow *glfwWindow)
{
    return (TkWindow*) glfwGetWindowUserPointer(glfwWindow);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
