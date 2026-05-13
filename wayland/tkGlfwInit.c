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
 * Tk info per GLFWwindow
 *----------------------------------------------------------------------
 *
 *
 * Each GLFWwindow has its WindowUserPointer set to the address of one of the
 * following structs.  This allows finding the TkWindow which wraps a given
 * GLFWWindow, as well as accessing other Tk specific data about the window.
 * The structs are also stored in a linked list so the setupProc or checkProc
 * can iterate through all GLFW windows in the application.
 */

/* Flag values */
#define needsDisplay 1
#define dontSwap     2

typedef struct glfwTkInfo {
    GLFWwindow* glfwWindow;
    TkWindow *winPtr;
    unsigned int flags;
    struct glfwTkInfo *nextPtr;
} glfwTkInfo;

glfwTkInfo* glfwTkInfoList = NULL;

static glfwTkInfo* createGlfwTkInfo(
    GLFWwindow* glfwWindow,
    TkWindow* winPtr)
{
    glfwTkInfo *glfwTkInfoPtr = Tcl_Alloc(sizeof(glfwTkInfo));
    *glfwTkInfoPtr = (glfwTkInfo) {
	.glfwWindow = glfwWindow,
	.winPtr = winPtr,
	.flags = 0,
	.nextPtr = glfwTkInfoList};
    glfwTkInfoList = glfwTkInfoPtr;
    return glfwTkInfoPtr;
}

static void destroyGlfwTkInfo(
    GLFWwindow* glfwWindow)
{
    glfwTkInfo* prev = NULL;
    glfwTkInfo *glfwTkInfoPtr = glfwTkInfoList;
    while(glfwTkInfoPtr) {
	if (glfwTkInfoPtr->glfwWindow == glfwWindow) {
	    if (glfwTkInfoPtr == glfwTkInfoList) {
		glfwTkInfoList = glfwTkInfoPtr->nextPtr;
	    } else {
		prev->nextPtr = glfwTkInfoPtr->nextPtr;
	    }
	    Tcl_Free(glfwTkInfoPtr);
	    return;
	}
	prev = glfwTkInfoPtr;
	glfwTkInfoPtr = glfwTkInfoPtr->nextPtr;
    }
    Tcl_Panic("DestroyGlfwTkInfo received unknown window");
}

static glfwTkInfo*
getGlfwTkInfo(
    GLFWwindow *glfwWindow)
{
    for (glfwTkInfo* glfwTkInfoPtr = glfwTkInfoList;
	 glfwTkInfoPtr != NULL;
	 glfwTkInfoPtr = glfwTkInfoPtr->nextPtr) {
	if (glfwTkInfoPtr->glfwWindow == glfwWindow) {
	    return glfwTkInfoPtr;
	}
    }
    Tcl_Panic("GetGlfwTkInfo received unknown window");
}    

/*
 *----------------------------------------------------------------------
 *
 * renderFBO --
 *
 *      This static function is called to draw the current contents of the
 *      backing store framebuffer of a glfwWindow on the screen.  It uses the
 *      glBlitFramebuffer to blit the framebuffer to the back buffer in the
 *      window's OpenGL context and then calls glfwSwapBuffers to swap the
 *      back buffer to the screen.  The backing store FBO is left unchanged
 *      for subsequent drawing functions to modify.
 * 
 * Results:
 *      None.
 *
 * Side effects:
 *      The current state of the window's backing store framebuffer
 *      is rendered on the screen.
 */

static void renderFBO(
    GLFWwindow *glfwWindow)
{
    glfwTkInfo *glfwInfoPtr = glfwGetWindowUserPointer(glfwWindow);
    NVGLUframebuffer *fbo = glfwInfoPtr->winPtr->privatePtr->fbo;
    int fbWidth, fbHeight;
    glfwMakeContextCurrent(glfwWindow);
    glfwGetFramebufferSize(glfwWindow, &fbWidth, &fbHeight);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, fbWidth, fbHeight,
		      0, 0, fbWidth, fbHeight,
		      GL_COLOR_BUFFER_BIT,
		      GL_NEAREST);
    glfwSwapBuffers(glfwWindow);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_ClipDrawableToRect --
 *
 *      There are a number of places in the generic code where a complex
 *      drawing operation is "double-buffered" copying a rectangle in
 *      a window to a pixmap, drawing into the pixmap, and then copying
 *      the pixmap back onto the original screen rectangle.  Platforms
 *      such macOS and Wayland, for which drawing to a window is already
 *      double-buffered can opt out of this behavior by defining
 *      NO_DOUBLE_BUFFERING.  The alternative code first calls this
 *      function with arguments describing the rectangle, then draws
 *      directly to the screen (i.e. to the backing store for the window)
 *      and then calls this function again with an infinite rectangle
 *      having width and height -1.
 *
 *      To make this work correctly in this port we avoid calling
 *      glfwSwapBuffers between the two calls.  In the second call
 *      we blit the rectange from our backing store framebuffer and
 *      the call glfwSwapBuffers.  We don't bother clipping the
 *      drawing operations.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Calls to glfwSwapBuffers are blocked when a finite rectangle
 *      is passed, and when an infinite rectangle is passed the original
 *      rectangle is blitted to the backing store framebuffer and
 *      glfwSwapBuffers is called.
 *
 *----------------------------------------------------------------------
 */

void
Tk_ClipDrawableToRect(
    TCL_UNUSED(Display *),
    Drawable drawable,
    int x, int y,
    int width, int height)
{
    (void) x; (void) y; (void) width; (void) height;
#if 0  // This experiment seems to have failed.
       // I don't know why.
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindowFromDrawable(drawable);
    glfwTkInfo *glfwInfoPtr = glfwGetWindowUserPointer(glfwWindow);
    if (width == -1 || height == -1) {
	printf("Finished double buffer section\n");
	renderFBO(glfwWindow);
	glfwInfoPtr->flags &= ~dontSwap;
	glfwInfoPtr->flags |= needsDisplay;
    } else {
	printf("Starting double buffer section ====> \n");
	glfwInfoPtr->flags |= dontSwap;
	glfwInfoPtr->flags &= ~needsDisplay;
    }
#else
    (void) drawable;
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDisplayAllWindows --
 *
 *	Called by TkWaylandSetupProc to display any "dirty" windows whose
 *      backing store framebuffer has been changed by a display proc run by
 *      Tcl_DoOneEvent since the last call to the SetupProc.  The framebuffer
 *      is blitted to the GL back buffer and then gflwSwapBuffers is called.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates windows on the screen. 
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandDisplayAllWindows()
{
    for (glfwTkInfo* infoPtr = glfwTkInfoList;
	 infoPtr != NULL;
	 infoPtr = infoPtr->nextPtr) {
	if (infoPtr->flags & needsDisplay) {
	    GLFWwindow *glfwWindow = infoPtr->glfwWindow;
	    printf("Displaying %s\n", Tk_PathName(infoPtr->winPtr));
	    renderFBO(glfwWindow);
	    infoPtr->flags &= ~needsDisplay;
	}
    }
}
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

    /*
     * A positive swap interval causes glfwSwapBuffers to wait for
     * the end of a display cycle before swapping the buffers,
     * and that causes artifacts when resizing windows.
     */
    glfwMakeContextCurrent(mainGlfwWindow);
    glfwSwapInterval(1);

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

    glfwMakeContextCurrent(NULL);
    TkGlfwClearCallbacks(mainGlfwWindow);
    glfwSetErrorCallback(NULL);
    mainGlfwWindow = NULL;
    if (GlfwIsInitialized) {
        glfwTerminate();
        GlfwIsInitialized = 0;
    }

    TkWaylandKeyCleanup();
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

    /* Set the initial pixel ratio for this window. */
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(glfwWindow, &fbWidth, &fbHeight);
    winPtr->privatePtr->pixelRatio = ((float) fbWidth) / ((float) width);
    
    /* Create a framebuffer for the backing store of the window. */
    glfwMakeContextCurrent(glfwWindow);
    winPtr->privatePtr->fbo = nvgluCreateFramebuffer(glfwContext.vg,
						     fbWidth, fbHeight, 0);
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
	printf("Window %s has a complete framebuffer @ %p\n",
	       Tk_PathName(winPtr), winPtr->privatePtr->fbo);
    }

    if (winPtr != NULL) {
	glfwSetWindowUserPointer(glfwWindow,
	    (void *) createGlfwTkInfo(glfwWindow, winPtr));
        TkGlfwSetupCallbacks(glfwWindow);
	winPtr->privatePtr->glfwWindow = glfwWindow;
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
    destroyGlfwTkInfo(glfwWindow);
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
    //// This is the case where the drawable is a window.
    TkWindow *childPtr = TkWaylandTkWindowFromDrawable(drawable);
    TkWindow *winPtr = childPtr;
    float x = 0, y = 0;
    while (!Tk_IsTopLevel(winPtr)) {
        x += winPtr->changes.x;
	y += winPtr->changes.y;
    	winPtr = winPtr->parentPtr;
    }

    /*
     * Now winPtr is the containing toplevel and the offsets of
     * the child are given by x and y.
     */
    GLFWwindow *glfwWindow = winPtr->privatePtr->glfwWindow;
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(glfwWindow, &fbWidth, &fbHeight);

    /* Set up the nanoVG drawing context for this nvgFrame */
    dcPtr->vg = glfwContext.vg;
    dcPtr->drawable = drawable;

    /* Start a NanoVG frame for drawing on the backing store. */
    nvgBeginFrame(glfwContext.vg, (float)fbWidth, (float)fbHeight,
		  winPtr->privatePtr->pixelRatio);

    /*
     * Import our graphics context and translate to the position
     * of the window we are drawing into.
     */
    
    TkGlfwApplyGC(dcPtr->vg, gc);
    nvgTranslate(dcPtr->vg, x, y);
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

MODULE_SCOPE void
TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr)
{
    if (!dcPtr || !dcPtr->vg) {
	printf("No drawing context!\n");
	return;
    }
    //// This is the case where the drawable is a window.
    TkWindow *winPtr = TkWaylandTkWindowFromDrawable(dcPtr->drawable);
    TkWindow *toplevelPtr = winPtr;
    while (!Tk_IsTopLevel(toplevelPtr)) {
	toplevelPtr = toplevelPtr->parentPtr;
    }
    GLFWwindow *glfwWindow = toplevelPtr->privatePtr->glfwWindow;

    /*
     * All nvg drawing since the call to nvgBeginFrame happens when we call
     * nvgEndFrame.  The drawing commands have just been queued.  Now they
     * actually get executed.
     */

    /* Make our GL context current. */
    glfwMakeContextCurrent(glfwWindow);

    /* Bind our backing store framebuffer. */
    nvgluBindFramebuffer(toplevelPtr->privatePtr->fbo);

    /* Check FBO completeness (for now). */
    int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("FBO is incomplete (status=0x%x)\n", status);
    }

    /* Set the viewport */
    int fbwidth, fbheight;
    glfwGetFramebufferSize(glfwWindow, &fbwidth, &fbheight);
    glViewport(0, 0, fbwidth, fbheight);

    printf("Drawing on %s\n", Tk_PathName(winPtr));
    nvgEndFrame(dcPtr->vg);
    nvgluBindFramebuffer(NULL);

    /*
     * nvgBeginFrame calls nvgSave, but nvgEndFrame does not
     * call nvgRestore.  Maybe it is not important to balance
     * those, but we call nvgRestore here just in case.
     */    

    nvgRestore(dcPtr->vg);

    /* Mark the window as needing display unless we are
     * in the middle of a Tk double-buffer section.
     */
    glfwTkInfo *infoPtr = getGlfwTkInfo(glfwWindow);
    ////if (!(infoPtr->flags & dontSwap)) {
    infoPtr->flags |= needsDisplay;
    ////}
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
    TkWaylandKeyInit();
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
    glfwTkInfo *info = glfwGetWindowUserPointer(glfwWindow);
    return info-> winPtr;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
