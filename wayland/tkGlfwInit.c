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

static TkGlfwContext glfwContext = {NULL, NULL, 0, 0, NULL, 0, 0, NULL};
static int shutdownInProgress = 0;

WindowMapping *windowMappingList = NULL;
static DrawableMapping *drawableMappingList = NULL;

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

    /* First, clean up all window mappings (this destroys GLFW windows). */
    CleanupAllMappings();

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
    WindowMapping *mapping;
    GLFWwindow    *window = NULL;
    //    Tcl_Interp *interp = winPtr->mainPtr->interp

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
        window = mainGlfwWindow;
        glfwSetWindowSize(window, width, height);
        glfwSetWindowTitle(window, title ? title : "");
    } else {
	/* Hints apply to the next call to glfwCreateWindow. */
	glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_ES_API);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);
	glfwWindowHint(GLFW_RESIZABLE,             GLFW_TRUE);
	glfwWindowHint(GLFW_FOCUS_ON_SHOW,         GLFW_TRUE);
	glfwWindowHint(GLFW_AUTO_ICONIFY,          GLFW_FALSE);
        window = glfwCreateWindow(width, height, title ? title : "",
                     NULL, mainGlfwWindow); /* Share the GL contexts */
        if (!window) return NULL;
	glfwMakeContextCurrent(window);
	glfwSwapInterval(0);
	glfwShowWindow(window);
    }

    /* Create a framebuffer for the backing store of the window. */
    glfwMakeContextCurrent(window);
    winPtr->privatePtr->fbo = nvgluCreateFramebuffer(glfwContext.vg,
						     width, height, 0);
    if (winPtr->privatePtr->fbo == NULL) {
		fprintf(stderr, "Could not create NanoVG framebuffer\n");
    }
    printf("Window %s now has glfwWindow %p and framebuffer %p\n",
	   Tk_PathName(winPtr), window, winPtr->privatePtr->fbo);
    nvgluBindFramebuffer(winPtr->privatePtr->fbo);
    /* Check FBO completeness for now. */
    int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("FBO is incomplete (status=0x%x)\n", status);
    } else {
	printf("Window %s has complete framebuffer %p\n", Tk_PathName(winPtr),
	   winPtr->privatePtr->fbo);
    }


    /* Allocate and initialize mapping. */
    mapping = (WindowMapping *)ckalloc(sizeof(WindowMapping));
    memset(mapping, 0, sizeof(WindowMapping));
    mapping->tkWindow     = winPtr;
    mapping->glfwWindow   = window;
    mapping->drawable     = TkWaylandDrawableForTkWindow(winPtr);
	//nextDrawableId++;
    mapping->width        = width;
    mapping->height       = height;
    mapping->clearPending = 1;

    AddMapping(mapping);
#if 0 //// What is this for?  It gets reset in tkWaylandMenu.c
    glfwSetWindowUserPointer(window, mapping);
#endif

    if (winPtr != NULL)
        TkGlfwSetupCallbacks(window);

#if 0
    /* Wait for the compositor to confirm real dimensions. */
    int timeout = 0;
    while ((mapping->width == 0 || mapping->height == 0) && timeout < 100) {
        ////glfwPollEvents();
        if (mapping->width == 0 || mapping->height == 0) {
            int w, h;
            glfwGetWindowSize(window, &w, &h);
            if (w > 0 && h > 0) {
                mapping->width  = w;
                mapping->height = h;
                break;
            }
        }
        timeout++;
    }
#endif

    if (mapping->width  == 0) mapping->width  = width;
    if (mapping->height == 0) mapping->height = height;

    if (winPtr != NULL) {
        winPtr->changes.width  = mapping->width;
        winPtr->changes.height = mapping->height;
    }

    if (drawableOut) *drawableOut = mapping->drawable;

    if (winPtr != NULL)
        TkWaylandQueueExposeEvent(winPtr, 0, 0,
	    mapping->width, mapping->height);
#if 0
    /* Wake up the event loop to process the expose event. */
    TkWaylandWakeupGLFW();
#endif

    return window;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwDestroyWindow --
 *
 *	Destroy a GLFW window and clean up associated resources.
 *	Now safely handles destruction during shutdown.
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
    WindowMapping *mapping;

    if (!glfwWindow) return;
    if (shutdownInProgress) return;  /* Let CleanupAllMappings handle it */

    mapping = FindMappingByGLFW(glfwWindow);
    if (mapping) {
        /* Mark window as being destroyed */
        mapping->glfwWindow = NULL;
        RemoveMapping(mapping);
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
 * SyncWindowSize--
 *
 * Helper function to synchronize Tk window size when
 * the framebuffer dimensions change.
 *
 * Results:
 *	Tk window size updated.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */


MODULE_SCOPE
void SyncWindowSize(WindowMapping *m)
{

    if (!m || !m->glfwWindow || shutdownInProgress) return;
    int w, h;
    int fbw, fbh;
    glfwGetWindowSize(m->glfwWindow, &w, &h);
    glfwGetFramebufferSize(m->glfwWindow, &fbw, &fbh);

    m->width  = w;
    m->height = h;

    if (m->tkWindow) {
        m->tkWindow->changes.width  = w;
        m->tkWindow->changes.height = h;
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
#if 0
    WindowMapping *m = FindMappingByDrawable(drawable);
    printf("Drawable is %s with glfwWindow %p and framebuffer %p\n",
	   Tk_PathName(winPtr), m->glfwWindow, fbo);
    glfwMakeContextCurrent(m->glfwWindow);
#endif
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
    WindowMapping *m = FindMappingByDrawable(dcPtr->drawable);
    printf("TkGlfwEndDraw: %s\n", Tk_PathName(m->tkWindow));
    NVGLUframebuffer *fbo = TkWaylandFBOForTkWindow(winPtr, NULL, NULL);
    if (!dcPtr || !dcPtr->vg) {
	printf("No drawing context!\n");
    }
    nvgRestore(dcPtr->vg); // nvgBeginFrame called nvgSave!
    printf("Binding backing store at %p\n", fbo);
    nvgluBindFramebuffer(fbo);
    int fbwidth, fbheight;
    glfwGetFramebufferSize(m->glfwWindow, &fbwidth, &fbheight);
    if (fbwidth == m->width && fbheight == m->height) {
    } else {
	printf("Buffer size mismatch\n");
	return;
    }
    glViewport(0, 0, m->width, m->height);
    // All nvg drawing happens here.
    nvgEndFrame(dcPtr->vg);
    printf("Blitting backingStore to the back buffer\n");
    blitFBOToBack(fbo, m->width, m->height);
    //glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT);
    printf("Sending window image to the compositor.\n");
    fprintf(stderr, "Calling glfwSwapBuffers\n");
    glfwSwapBuffers(m->glfwWindow);
    nvgluBindFramebuffer(NULL);
    printf("Sent\n");

    /* Signal that the screen needs to be updated with the FBO's content. */
    //// This does not make sense anymore.
    if (m) {
	m->needsDisplay = 1;
    }
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
 * TkGlfwProcessEvents --
 *
 *	Process pending GLFW events. Called from the Tk event loop.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Polls and dispatches GLFW events.
 *
 *----------------------------------------------------------------------
 */
////XXXX Why is this used sometimes and glfwPollEvents used directly
//// other times????  (It is NOT called from the event loop.)
#if 0
MODULE_SCOPE void
TkGlfwProcessEvents(void)
{
    if (GlfwIsInitialized && !shutdownInProgress) {
        ////glfwPollEvents();
    }
}
#endif

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
 * FindMappingbyGLFW --
 *
 *	Searches the windowMappingList by native GLFW window handle.
 *
 * Results:
 *	Retrieves mapping entry.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

WindowMapping *
FindMappingByGLFW(GLFWwindow *w)
{
    WindowMapping *c = windowMappingList;
    while (c) { if (c->glfwWindow == w) return c; c = c->nextPtr; }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FindMappingbyTk --
 *
 *	Searches the windowMappingList by Tk window pointer.
 *
 * Results:
 *	Retrieves mapping entry.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

WindowMapping *
FindMappingByTk(TkWindow *w)
{
    WindowMapping *c = windowMappingList;
    while (c) { if (c->tkWindow == w) return c; c = c->nextPtr; }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FindMappingbyDrawable --
 *
 *	Searches the windowMappingList by Drawable.
 *
 * Results:
 *	Retrieves mapping entry.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

WindowMapping *
FindMappingByDrawable(Drawable d)
{
    DrawableMapping *dm;
    WindowMapping   *m;

    if (d == 0 || d == None) return NULL;

    /* Fast path: explicit registrations. */
    for (dm = drawableMappingList; dm; dm = dm->next) {
        if (dm->drawable == d)
            return dm->mapping;
    }

    /* Toplevel whose Tk window ID matches. */
    for (m = windowMappingList; m; m = m->nextPtr) {
        if (m->tkWindow && (Drawable)m->tkWindow->window == d)
            return m;
    }

    /* Drawable is a TkWindow* passed directly. */
    for (m = windowMappingList; m; m = m->nextPtr) {
        if (!m->tkWindow) continue;
        TkWindow *stack[256];
        int top = 0;
        stack[top++] = m->tkWindow;
        while (top > 0) {
            TkWindow *cur = stack[--top];
            if ((Drawable)cur == d || (Drawable)cur->window == d) {
                RegisterDrawableForMapping(d, m);
                return m;
            }
            TkWindow *child;
            for (child = cur->childList; child && top < 255;
                 child = child->nextPtr)
                stack[top++] = child;
        }
    }

    /*
     * Drawable is a TkWaylandPixmap* that was never
     * registered. Validate by checking that its fields look sane,
     * then bind it to the first available mapping.
     */
    TkWaylandPixmap *pix = (TkWaylandPixmap *)d;
    if (pix != NULL) {
        /* Sanity check: type must be 0 or 1, dimensions must be
         * plausible. This guards against random pointer misinterpretation. */
        if ((pix->type == 0 || pix->type == 1) &&
            pix->width  >= 0 && pix->width  < 32768 &&
            pix->height >= 0 && pix->height < 32768) {
            m = TkGlfwGetMappingList();
            if (m) {
                RegisterDrawableForMapping(d, m);
                return m;
            }
        }
    }

    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetMappingList --
 *
 *	Retrieves the entire windowMappingList.
 *
 * Results:
 *	Returns list.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

WindowMapping *
TkGlfwGetMappingList(void)
{
    return windowMappingList;
}

/*
 *----------------------------------------------------------------------
 *
 * AddMapping  --
 *
 *	Add an entry to the windowMappingList.
 *
 * Results:
 *	Entry is added.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
AddMapping(WindowMapping *m)
{
    if (!m) return;
    m->nextPtr  = windowMappingList;
    windowMappingList = m;
}


/*
 *----------------------------------------------------------------------
 *
 * RemoveMappings --
 *
 *  Remove an entry from the windowMappingList.
 *
 * Results:
 *	Entry is added.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
void
RemoveMapping(WindowMapping *m)
{
    WindowMapping **pp = &windowMappingList;

    if (!m) {
        fprintf(stderr, "RemoveMapping: Called with NULL mapping\n");
        return;
    }

    while (*pp) {
        if (*pp == m) {
            *pp = m->nextPtr;

            /* Clear the mapping before freeing to detect use-after-free. */
            memset(m, 0, sizeof(WindowMapping));
            ckfree((char *)m);
            return;
        }
        pp = &(*pp)->nextPtr;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CleanupAllMappings --
 *
 *	Destroy all GLFW windows and free mapping structures.
 *	Called during shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All GLFW windows are destroyed and mappings freed.
 *
 *----------------------------------------------------------------------
 */

void
CleanupAllMappings(void)
{
    WindowMapping *c = windowMappingList, *n;

    while (c) {
        n = c->nextPtr;
        if (c->glfwWindow) {
            glfwDestroyWindow(c->glfwWindow);
        }
        memset(c, 0, sizeof(WindowMapping));
        ckfree((char *)c);
        c = n;
    }
    windowMappingList = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * RegisterDrawableForMapping --
 *
 *	Adds a Drawable to the windowMappingList.
 *
 * Results:
 *	Entry added.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
RegisterDrawableForMapping(Drawable d, WindowMapping *m)
{
    DrawableMapping *dm = ckalloc(sizeof(DrawableMapping));
    dm->drawable = d;
    dm->mapping  = m;
    dm->next     = drawableMappingList;
    drawableMappingList = dm;
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
    return toplevelPtr->privatePtr->glfwWindow;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetDrawable --
 *
 *	Retrieves the Drawable associated with a GLFW window.
 *
 * Results:
 *	Window pointer returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE Drawable
TkGlfwGetDrawable(GLFWwindow *w)
{
    WindowMapping *m = FindMappingByGLFW(w);
    return m ? m->drawable : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwResizeWindow --
 *
 *	Resizes a GLFW window.
 *
 * Results:
 *	Window dimensions updated.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwResizeWindow(GLFWwindow *w, int width, int height)
{
    WindowMapping *m = FindMappingByGLFW(w);
    if (m) { m->width = width; m->height = height; }
}


/*
 *----------------------------------------------------------------------
 *
 * TkGlfwUpdateWindowSize --
 *
 *	Updates the side of a GLFW window.
 *
 * Results:
 *	Window dimensions updated.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwUpdateWindowSize(GLFWwindow *glfwWindow, int width, int height)
{
    WindowMapping *m = FindMappingByGLFW(glfwWindow);
    if (m) { m->width = width; m->height = height; }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetWindowFromDrawable --
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
TkGlfwGetWindowFromDrawable(Drawable drawable)
{
    WindowMapping *m = FindMappingByDrawable(drawable);
    return m ? m->glfwWindow : NULL;
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
