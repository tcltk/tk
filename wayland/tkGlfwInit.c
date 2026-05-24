/*
 * tkGlfwInit.c --
 *
 *   GLFW/Wayland-specific interpreter initialization: context
 *   management, window mapping, drawing context lifecycle, color
 *   conversion, and platform init/cleanup. GLFW, NanoVG and libdecor
 *   provide the native platform on which Tk's widget set and event loop
 *   are deployed.
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
#define GLFW_EXPOSE_NATIVE_EGL
#include <GLFW/glfw3native.h>

#define NANOVG_GLES3_IMPLEMENTATION
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

#define GLFW_EXPOSE_NATIVE_EGL
#include <GLFW/glfw3native.h>


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
 * The glfwWindow for the root window.
 */

GLFWwindow *mainGlfwWindow;
static TkGlfwContext mainGlfwContext = {0};
static int shutdownInProgress = 0;

#if 0
static void GLtest(GLFWwindow *window) {
    int fbWidth = 0, fbHeight = 0;
    glfwGetWindowSize(window, &fbWidth, &fbHeight);
    glfwMakeContextCurrent(window);
    glViewport(0, 0, fbWidth, fbHeight); // Your expected new size
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Disable any potential state traps
    glDisable(GL_SCISSOR_TEST); 
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);

    // Draw a solid color screen-filling triangle
    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.0f, 0.0f); // Bright Red
    glVertex2f(-1.0f, -1.0f);    // Bottom-Left
    glVertex2f( 3.0f, -1.0f);    // Far Bottom-Right (extends past screen)
    glVertex2f(-1.0f,  3.0f);    // Far Top-Left (extends past screen)
    glEnd();
}
#endif

/*
 * Buffers for font files needed for window decorations.
 */

static size_t sans_size, bold_size, mono_size;
static unsigned char *sans_data, *bold_data, *mono_data;

static unsigned char* readFont(
    const char* fontPath,
    size_t* size)
{
    size_t fileSize;
    FILE* file = fopen(fontPath, "rb");
    if (!file) {
        printf("Could not open font file %s\n", fontPath);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char* buffer = malloc(fileSize);
    if (!buffer) {
        fclose(file);
        Tcl_Panic("Could not allocate memory for font data.\n");
    }
    if (fileSize != fread(buffer, 1, fileSize, file)) {
	Tcl_Panic("Read failed on font file");
    }
    fclose(file);
    *size = fileSize;
    return buffer;
}

static void freeFonts()
{
    if (sans_data) {
	free(sans_data);
	sans_data = NULL;
    }
    if (bold_data) {
	free(bold_data);
	bold_data = NULL;
    }
    if (mono_data) {
	free(mono_data);
	mono_data = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 * Tk info per GLFWwindow
 *----------------------------------------------------------------------
 */

glfwTkInfo* glfwTkInfoList = NULL;

static glfwTkInfo* createGlfwTkInfo(
    GLFWwindow* glfwWindow,
    TkWindow* winPtr)
{
    glfwTkInfo *infoPtr = Tcl_Alloc(sizeof(glfwTkInfo));
    *infoPtr = (glfwTkInfo) {
	.glfwWindow = glfwWindow,
	.winPtr = winPtr,
	.flags = 0,
	.nextPtr = glfwTkInfoList};
    glfwTkInfoList = infoPtr;

    infoPtr->context.vg = nvgCreateGLES3(NVG_ANTIALIAS
				  | NVG_STENCIL_STROKES
				  | NVG_DEBUG);
    if (!infoPtr->context.vg) {
        fprintf(stderr, "createGlfwTkInfo: nvgCreateGLES3() failed\n");
        glfwDestroyWindow(glfwWindow);
        glfwTerminate();
        return NULL;
    }
    nvgCreateFontMem(infoPtr->context.vg, "sans", sans_data,
		     (int)sans_size, 0);    
    nvgCreateFontMem(infoPtr->context.vg, "sans-bold", bold_data,
		     (int)bold_size, 0);    
    nvgCreateFontMem(infoPtr->context.vg, "mono", mono_data,
		     (int)mono_size, 0);    
    return infoPtr;
}

static void destroyGlfwTkInfo(
    GLFWwindow* glfwWindow)
{
    printf("destroyGlfwTkInfo\n");
    glfwTkInfo* prev = NULL;
    glfwTkInfo *infoPtr = glfwTkInfoList;
    while(infoPtr) {
	if (infoPtr->glfwWindow == glfwWindow) {
	    if (infoPtr == glfwTkInfoList) {
		glfwTkInfoList = infoPtr->nextPtr;
	    } else {
		prev->nextPtr = infoPtr->nextPtr;
	    }
	    glfwMakeContextCurrent(glfwWindow);
	    glfwSetWindowUserPointer(glfwWindow, NULL);
	    nvgDeleteGLES3(infoPtr->context.vg);
	    Tcl_Free(infoPtr);
	    return;
	}
	prev = infoPtr;
	infoPtr = infoPtr->nextPtr;
    }
    Tcl_Panic("DestroyGlfwTkInfo received unknown window");
}

static glfwTkInfo*
getGlfwTkInfo(
    GLFWwindow *glfwWindow)
{
    for (glfwTkInfo* infoPtr = glfwTkInfoList;
	 infoPtr != NULL;
	 infoPtr = infoPtr->nextPtr) {
	if (infoPtr->glfwWindow == glfwWindow) {
	    return infoPtr;
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
    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(glfwWindow);
    if (!infoPtr) {
	printf("renderFBO: No UserPointer\n");
	return;
    } 
    NVGLUframebuffer *fbo = infoPtr->winPtr->privatePtr->fbo;
    int fbWidth, fbHeight;
    glfwMakeContextCurrent(glfwWindow);
    glfwGetFramebufferSize(glfwWindow, &fbWidth, &fbHeight);
    /*
     * This is an attempted workaround for some strange Wayland behavior.
     * The framebuffer size reported by GLFW can be different from the actual
     * framebuffer size provided by Wayland. This has been observed for the
     * root toplevel when a second toplevel is on the screen and the root gets
     * resized by a call to glfwSetWindowSize.  The behavior happens only for
     * the root (which Wayland considers to be the "main" window by virtue of
     * having been created first). We have to query GL directly to get the
     * the actual size.
     */
     glBindFramebuffer(GL_FRAMEBUFFER, fbo->fbo);
    //glViewport(0, 0, fbWidth, fbHeight);
    GLint glRect[4] = {0};
    glGetIntegerv(GL_VIEWPORT, glRect);
    printf("GLFW size: %dx%d; GL size %dx%d\n",
    fbWidth, fbHeight, glRect[2], glRect[3]);
    glBindVertexArray(0);
    
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, fbWidth, fbHeight,
		      0, 0, glRect[2], glRect[3],
		      GL_COLOR_BUFFER_BIT,
		      GL_NEAREST);
    glfwSwapBuffers(glfwWindow);
    /*
     * If the sizes differ we will need to redraw the window later
     * because the blit will rescale, causing artifacts.  Using the
     * actual size for the target makes things the right size,
     * but blurry.
     */
#if 1
    if (glRect[2] != fbWidth || glRect[3] != fbHeight) {
	printf("================================= Size mismatch\n");
	TkWaylandQueueExposeEvent(infoPtr->winPtr, 0, 0, fbWidth, fbHeight);	
    }
#endif
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
    //// Check for NULL
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
 *	Initializes the GLFW library, and the Wayland protocols.
 *      Creates a GFLWWindow to be used for the root window and its
 *      NanoVG context.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initializes GLFW, creates a GFLWwindow for the root,
 *	and its NanoVG context.
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
    //glfwWindowHint(GLFW_OPENGL_COMPAT_PROFILE, GLFW_TRUE);
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

    /* Load fonts used in window decorations. */
    sans_data = readFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	 &sans_size);
    bold_data = readFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
	 &bold_size);
    mono_data = readFont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
	 &mono_size);

    /*
     * A positive swap interval causes glfwSwapBuffers to wait for
     * the end of a display cycle before swapping the buffers.
     */
    glfwMakeContextCurrent(mainGlfwWindow);
    glfwSwapInterval(0);
    
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
#if 0
    if (mainGlfwContext.vg) {
        /* Make the GL context of the root current if it still exists. */
        if (mainGlfwWindow) {
            glfwMakeContextCurrent(mainGlfwWindow);
            nvgDeleteGLES3(mainGlfwContext.vg);
        }
        mainGlfwContext.vg = NULL;
    }
#endif
    glfwMakeContextCurrent(NULL);
    TkGlfwClearCallbacks(mainGlfwWindow);
    glfwSetErrorCallback(NULL);
    mainGlfwWindow = NULL;
    if (GlfwIsInitialized) {
        glfwTerminate();
        GlfwIsInitialized = 0;
    }
    freeFonts();
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
 *	Creates a new GLFW window and its associated GlfwTkInfo.
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
				      NULL, NULL);
	//mainGlfwWindow); /* Share the GL contexts */
        if (!glfwWindow) {
	    return NULL;
	}
	glfwMakeContextCurrent(glfwWindow);
	glfwSwapInterval(0);
	glfwShowWindow(glfwWindow);
	glfwSwapBuffers(glfwWindow);
    }
    glfwTkInfo *infoPtr = createGlfwTkInfo(glfwWindow, winPtr);
    printf("nvgContext for %s is at %p\n", Tk_PathName(winPtr),
	   infoPtr);
    if (glfwWindow == mainGlfwWindow) {
	mainGlfwContext = infoPtr->context;
    }
    glfwSetWindowUserPointer(glfwWindow, infoPtr);
    TkGlfwSetupCallbacks(glfwWindow);
    winPtr->privatePtr->glfwWindow = glfwWindow;
    winPtr->changes.width  = width;
    winPtr->changes.height = height;

    /* Set the initial pixel ratio for this window. */
    int fbWidth, fbHeight;
    float xscale, yscale;
    glfwGetWindowContentScale(glfwWindow, &xscale, &yscale);
    winPtr->privatePtr->pixelRatio = xscale;
    printf("Initial pixel ratio for %s is %f\n",
	   Tk_PathName(winPtr), winPtr->privatePtr->pixelRatio);
    
    /* Create a framebuffer for the backing store of the window. */
    glfwMakeContextCurrent(glfwWindow);
    glfwGetFramebufferSize(glfwWindow, &fbWidth, &fbHeight); 
    winPtr->privatePtr->fbo = nvgluCreateFramebuffer(infoPtr->context.vg,
						     fbWidth, fbHeight, 0);
    if (winPtr->privatePtr->fbo == NULL) {
		fprintf(stderr, "Could not create NanoVG framebuffer\n");
    }
    printf("Window %s has glfwWindow %p and framebuffer %p\n",
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
    printf("TkGflwDestroyWindow\n");
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
 * Prepares the NanoVG context for drawing. Fully preserves the existing
 * Window tracking logic to prevent text and coordinate regression.
 *
 * Results:
 * TCL_OK if drawing can proceed, TCL_ERROR otherwise.
 *
 * Side effects:
 * Changes nvg and gl state.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwBeginDraw(
    Drawable drawable,
    GC gc,
    TkWaylandDrawingContext *dcPtr)
{
    if (!dcPtr) {
        return TCL_ERROR;
    }

	/* Pixmap drawing block. */
    if (TkWaylandDrawableIsPixmap(drawable)) {
        TkWaylandPixmap *pixmapImpl = TkWaylandPixmapFromDrawable(drawable);
        if (!pixmapImpl || !pixmapImpl->glfwWindow) {
            return TCL_ERROR;
        }

        glfwMakeContextCurrent(pixmapImpl->glfwWindow);
        glBindFramebuffer(GL_FRAMEBUFFER, pixmapImpl->fbo);
        glViewport(0, 0, pixmapImpl->width, pixmapImpl->height);

        glfwTkInfo *infoPtr = getGlfwTkInfo(pixmapImpl->glfwWindow);
        dcPtr->vg = infoPtr->context.vg;
        dcPtr->drawable = drawable;

        nvgResetTransform(dcPtr->vg);
        nvgBeginFrame(dcPtr->vg, pixmapImpl->width, pixmapImpl->height, 1.0f);
        TkGlfwApplyGC(dcPtr->vg, gc);
        
        return TCL_OK;
    }

	/* Window drawing block. */
    TkWindow *childPtr = TkWaylandTkWindowFromDrawable(drawable);
  //  printf("BeginDraw for %s\n", Tk_PathName(childPtr));
    TkWindow *winPtr = childPtr;
    float x = 0, y = 0;
    while (!Tk_IsTopLevel(winPtr)) {
        x += winPtr->changes.x;
        y += winPtr->changes.y;
        winPtr = winPtr->parentPtr;
    }

    /* Now winPtr is the containing toplevel and the offsets of the child are given by x and y. */
    GLFWwindow *glfwWindow = winPtr->privatePtr->glfwWindow;
    glfwTkInfo *infoPtr = getGlfwTkInfo(glfwWindow);

    /* Set up the nanoVG drawing context for this nvgFrame */
    dcPtr->vg = infoPtr->context.vg;
    dcPtr->drawable = drawable;

    nvgResetTransform(dcPtr->vg);

    printf("BeginFrame for toplevel %s with size %dx%d and pixel ratio %f\n",
           Tk_PathName(winPtr), Tk_Width(winPtr), Tk_Height(winPtr),
           winPtr->privatePtr->pixelRatio);

    nvgBeginFrame(dcPtr->vg, Tk_Width(winPtr), Tk_Height(winPtr),
                  winPtr->privatePtr->pixelRatio);

    /* Import our graphics context and translate to the origin of the window we are drawing into. */
    TkGlfwApplyGC(dcPtr->vg, gc);
    printf("translating to (%f,%f)\n", x, y);
    nvgTranslate(dcPtr->vg, x, y);
    
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwEndDraw --
 *
 * End a drawing operation.
 *
 * Results:
 * None.
 *
 * Side effects:
 * Pops NanoVG state.
 * Unbinds pixmap FBO if drawing to pixmap.
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

	/* Pixmap termination block. */
    if (TkWaylandDrawableIsPixmap(dcPtr->drawable)) {
        nvgEndFrame(dcPtr->vg);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        nvgRestore(dcPtr->vg);
        return;
    }

	/* Windows termination block. */
    TkWindow *childPtr = TkWaylandTkWindowFromDrawable(dcPtr->drawable);
    printf("EndDraw for %s\n", Tk_PathName(childPtr));
    TkWindow *winPtr = childPtr;
    while (!Tk_IsTopLevel(winPtr)) {
        winPtr = winPtr->parentPtr;
    }
    /* winPtr is the toplevel containing our drawable. */
    GLFWwindow *glfwWindow = winPtr->privatePtr->glfwWindow;

    /* Make our GL context current and set the viewport. */
    glfwMakeContextCurrent(glfwWindow);
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(glfwWindow, &fbWidth, &fbHeight);
    
    /* Bind our backing store framebuffer. */
    nvgluBindFramebuffer(winPtr->privatePtr->fbo);

    printf("EndDraw: setting viewport to %dx%d\n", fbWidth, fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);

    /* Check FBO completeness (for now). */
    int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("FBO is incomplete (status=0x%x)\n", status);
    }
    
    printf("EndFrame: drawing %s in toplevel %s with viewport %dx%d\n",
           Tk_PathName(childPtr), Tk_PathName(winPtr), fbWidth, fbHeight);
    
    nvgEndFrame(dcPtr->vg);
    nvgluBindFramebuffer(NULL);

    nvgRestore(dcPtr->vg);

    /* Mark the window as needing display unless we are in the middle of a Tk double-buffer section. */
    glfwTkInfo *infoPtr = getGlfwTkInfo(glfwWindow);
    infoPtr->flags |= needsDisplay;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetNVGContext --
 *
 *	Return the NanoVG context for a drawable.
 *
 * Results:
 *	The NVGcontext pointer, or NULL if shutting down.
 *      
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext*
TkGlfwGetNVGContext(
    Drawable drawable)
{
    if (TkWaylandDrawableIsPixmap(drawable)) {
        printf("Contexts not available for pixmaps yet.\n");
        return NULL;
    }
    
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindowFromDrawable(drawable);
    if (!glfwWindow) {
        /* If no valid toplevel window context is bound yet, borrow the root context. */
        glfwWindow = mainGlfwWindow;
    }
    
    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(glfwWindow);
    if (!infoPtr) {
        /* Fall back to the main/global framework context if this specific window isn't fully registered */
        if (glfwWindow == mainGlfwWindow && mainGlfwContext.vg != NULL) {
            return mainGlfwContext.vg;
        }
        printf("TkGlfwGetNVContext: No UserPointer available for window %p\n", (void*)glfwWindow);
        return NULL;
    }
    
    if (shutdownInProgress) {
        return NULL;
    }
    
    return infoPtr->context.vg;
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
 *	Makes the GL context for the root window current.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext *
TkGlfwGetNVGContextForMeasure(void)
{
    if (!GlfwIsInitialized || !mainGlfwContext.vg || shutdownInProgress)
        return NULL;
    glfwMakeContextCurrent(mainGlfwWindow);
    return mainGlfwContext.vg;
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
    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(glfwWindow);
    if (!infoPtr) {
	printf("TkGlfwGetTkWindow: No UserPointer.\n");
	return NULL;
    }
    if (!infoPtr->winPtr) {
	printf("TkGlfwGetTkWindow: No winPtr in User data.\n");
	return NULL;
    }
    
    return infoPtr->winPtr;
} 

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
