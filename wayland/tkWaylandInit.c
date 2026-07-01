/*
 * tkWaylandInit.c --
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
#define NANOVG_GLES3_IMPLEMENTATION

#include "tkInt.h"
#include "tkWaylandInt.h"
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_EGL
#include <GLFW/glfw3native.h>
#include <wayland-egl.h>


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
 * Global Wayland objects - defined here and shared with other modules.
 */
struct wl_display *waylandDisplay = NULL;
struct wl_compositor *waylandCompositor = NULL;
struct wl_subcompositor *waylandSubcompositor = NULL;
struct xdg_wm_base *waylandWmBase = NULL;
struct wl_seat *waylandSeat = NULL;
EGLDisplay eglDisplay = EGL_NO_DISPLAY;
EGLContext eglContext = EGL_NO_CONTEXT;
EGLConfig  eglConfig  = NULL;


/*
 * Font data for popup rendering - shared with tkWaylandPopup.c
 */
size_t sans_size = 0, bold_size = 0, mono_size = 0;
unsigned char *sans_data = NULL, *bold_data = NULL, *mono_data = NULL;

/*
 * The glfwWindow for the root window.
 */

GLFWwindow *mainGlfwWindow;
static TkWaylandContext mainGlfwContext = {0};
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


static unsigned char* readFont(
    const char* fontPath,
    size_t* size)
{
    size_t fileSize;
    FILE* file = fopen(fontPath, "rb");
    if (!file) {
        fprintf(stderr, "Could not open font file %s\n", fontPath);
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
        sans_size = 0;
    }
    if (bold_data) {
        free(bold_data);
        bold_data = NULL;
        bold_size = 0;
    }
    if (mono_data) {
        free(mono_data);
        mono_data = NULL;
        mono_size = 0;
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
    fprintf(stderr, "destroyGlfwTkInfo\n");
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
        fprintf(stderr, "renderFBO: No UserPointer\n");
        return;
    }
    
    /* Check if winPtr or privatePtr is NULL. */
    if (!infoPtr->winPtr) {
        fprintf(stderr, "renderFBO: winPtr is NULL\n");
        return;
    }
    
    if (!infoPtr->winPtr->privatePtr) {
        fprintf(stderr, "renderFBO: privatePtr is NULL\n");
        return;
    }
    
    NVGLUframebuffer *fb = infoPtr->winPtr->privatePtr->fb;
    
    /* Check if framebuffer exists. */
    if (!fb) {
        fprintf(stderr, "renderFBO: framebuffer is NULL - creating one\n");
        
        /* Try to create a framebuffer. */
        int fbWidth, fbHeight;
        glfwMakeContextCurrent(glfwWindow);
        glfwGetFramebufferSize(glfwWindow, &fbWidth, &fbHeight);
        
        if (fbWidth <= 0) fbWidth = 200;
        if (fbHeight <= 0) fbHeight = 200;
        
        fb = nvgluCreateFramebuffer(infoPtr->context.vg, fbWidth, fbHeight, 0);
        if (!fb) {
            fprintf(stderr, "renderFBO: failed to create framebuffer\n");
            return;
        }
        infoPtr->winPtr->privatePtr->fb = fb;
        fprintf(stderr, "renderFBO: created framebuffer %p (%dx%d)\n", 
                fb, fbWidth, fbHeight);
    }
    
    int fbWidth, fbHeight;
    glfwMakeContextCurrent(glfwWindow);
    glfwGetFramebufferSize(glfwWindow, &fbWidth, &fbHeight);
    
    /* Ensure framebuffer is valid by checking its fbo ID. */
    if (fb->fbo == 0) {
        fprintf(stderr, "renderFBO: framebuffer has invalid fbo ID - recreating\n");
        nvgluDeleteFramebuffer(fb);
        fb = nvgluCreateFramebuffer(infoPtr->context.vg, fbWidth, fbHeight, 0);
        if (!fb) {
            fprintf(stderr, "renderFBO: failed to recreate framebuffer\n");
            return;
        }
        infoPtr->winPtr->privatePtr->fb = fb;
    }
    
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb->fbo);
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
    //// Check for NULL
    if (width == -1 || height == -1) {
	fprintf(stderr, "Finished double buffer section\n");
	renderFBO(glfwWindow);
	glfwInfoPtr->flags &= ~dontSwap;
	glfwInfoPtr->flags |= needsDisplay;
    } else {
	fprintf(stderr, "Starting double buffer section ====> \n");
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
            /* Skip if window or framebuffer is not ready. */
            if (!infoPtr->winPtr || !infoPtr->winPtr->privatePtr || 
                !infoPtr->winPtr->privatePtr->fb) {
                /* Clear the flag to avoid repeated attempts. */
                infoPtr->flags &= ~needsDisplay;
                fprintf(stderr, "TkWaylandDisplayAllWindows: skipping %s (no FBO)\n",
                        infoPtr->winPtr ? Tk_PathName(infoPtr->winPtr) : "unknown");
                continue;
            }
            
            GLFWwindow *glfwWindow = infoPtr->glfwWindow;
            fprintf(stderr, "Displaying %s\n", Tk_PathName(infoPtr->winPtr));
            renderFBO(glfwWindow);
            infoPtr->flags &= ~needsDisplay;
        }
    }
}
/*
 *----------------------------------------------------------------------
 *
 * TkWaylandErrorCallback --
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
TkWaylandErrorCallback(int error, const char *desc)
{
    /* Don't print errors during shutdown. */
    if (shutdownInProgress) return;

    fprintf(stderr, "GLFW Error %d: %s\n", error, desc);
}


/*
 *----------------------------------------------------------------------
 *
 * TkWaylandInitialize --
 *
 *	Initializes the GLFW library, and the Wayland protocols.
 *  Creates a GFLWWindow to be used for the root window and its
 *  NanoVG context.
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
TkWaylandInitialize(void)
{
    if (GlfwIsInitialized) return TCL_OK;

    glfwSetErrorCallback(TkWaylandErrorCallback);

#ifdef GLFW_PLATFORM_WAYLAND
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif

    if (!glfwInit()) {
        fprintf(stderr, "TkWaylandInitialize: glfwInit() failed\n");
        return TCL_ERROR;
    }

    /*
     * IMPORTANT: do NOT force EGL here.
     * GLFW on Wayland will choose EGL automatically.
     */

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);   /* must be TRUE on Wayland init */
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);

    mainGlfwWindow = glfwCreateWindow(200, 200, "Tk", NULL, NULL);
    if (!mainGlfwWindow) {
        fprintf(stderr, "TkWaylandInitialize: failed to create root window\n");
        glfwTerminate();
        return TCL_ERROR;
    }

    /*
     * CRITICAL: make context current BEFORE any GL/EGL assumptions
     */
    glfwMakeContextCurrent(mainGlfwWindow);
    glfwPollEvents();

    /*
     * Now GL is guaranteed valid
     */
    fprintf(stderr, "GL_VENDOR   = %s\n", glGetString(GL_VENDOR));
    fprintf(stderr, "GL_RENDERER = %s\n", glGetString(GL_RENDERER));
    fprintf(stderr, "GL_VERSION  = %s\n", glGetString(GL_VERSION));

    /*
     * We hide AFTER context is fully initialized (Wayland-safe pattern).
     */
    glfwHideWindow(mainGlfwWindow);

    /*
     * Wayland display (only for xdg/wl_egl_window popups if needed)
     */
    waylandDisplay = glfwGetWaylandDisplay();
    if (!waylandDisplay) {
        fprintf(stderr, "TkWaylandInitialize: glfwGetWaylandDisplay() failed\n");
        glfwTerminate();
        return TCL_ERROR;
    }


    /*
     * Popup system only needs:
     * - wl_display (from GLFW)
     * - current GL context (implicit via GLFW)
     */
    TkWaylandPopupSetMainWindow(mainGlfwWindow);

    /*
     * Load fonts
     */
    sans_data = readFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                         &sans_size);
    bold_data = readFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                         &bold_size);
    mono_data = readFont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                         &mono_size);

    glfwSwapInterval(0);

    GlfwIsInitialized = 1;
    shutdownInProgress = 0;

    Tcl_CreateExitHandler(TkWaylandShutdown, NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandShutdown --
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
TkWaylandShutdown(TCL_UNUSED(void *))
{
    if (shutdownInProgress) return;
    shutdownInProgress = 1;

    if (!GlfwIsInitialized) {
        shutdownInProgress = 0;
        return;
    }

    /* Remove the IBus file handler first so the event loop stops
     * delivering stale signals after windows begin to be destroyed. */
    extern int ibus_fd;          /* defined in tkWaylandKey.c */
    extern sd_bus *ibus_bus;
    if (ibus_fd >= 0) {
        Tcl_DeleteFileHandler(ibus_fd);
        ibus_fd = -1;
    }
    if (ibus_bus) {
        sd_bus_unref(ibus_bus);
        ibus_bus = NULL;
    }

    glfwMakeContextCurrent(NULL);
    TkWaylandClearCallbacks(mainGlfwWindow);
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
 * TkWaylandCreateWindow --
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
TkWaylandCreateWindow(
    TkWindow   *winPtr,
    int         width,
    int         height,
    const char *title,
    Drawable   *drawableOut)
{
    fprintf(stderr, "TkWaylandCreateWindow\n");
    if (winPtr == NULL) {
        Tcl_Panic("TkWaylandCreateWindow called with null winPtr\n");
    }

    /* Don't create windows during shutdown. */
    if (shutdownInProgress) {
        return NULL;
    }
    if (!GlfwIsInitialized) {
        if (TkWaylandInitialize() != TCL_OK) {
            return NULL;
        }
    }

    if (width  <= 1) width  = 200;
    if (height <= 1) height = 200;

    GLFWwindow *glfwWindow = NULL;

    if (winPtr == (TkWindow *) Tk_MainWindow(winPtr->mainPtr->interp)) {
        /*
         * Root window: ensure we have a GL ES context and that it is current.
         * If this is the first time, create mainGlfwWindow here.
         */
        if (mainGlfwWindow == NULL) {
            glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_ES_API);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
            glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);
            glfwWindowHint(GLFW_RESIZABLE,             GLFW_TRUE);
            glfwWindowHint(GLFW_FOCUS_ON_SHOW,         GLFW_TRUE);
            glfwWindowHint(GLFW_AUTO_ICONIFY,          GLFW_FALSE);
            glfwWindowHint(GLFW_SCALE_FRAMEBUFFER,     GLFW_TRUE);

            mainGlfwWindow = glfwCreateWindow(width, height,
                                              title ? title : "",
                                              NULL, NULL);
            if (!mainGlfwWindow) {
                return NULL;
            }
        }

        glfwWindow = mainGlfwWindow;

        /* Make sure the root window’s context is current and configured. */
        glfwMakeContextCurrent(glfwWindow);
        glfwSwapInterval(0);
        glfwSetWindowSize(glfwWindow, width, height);
        glfwSetWindowTitle(glfwWindow, title ? title : "");
        glfwShowWindow(glfwWindow);
        glfwSwapBuffers(glfwWindow);
    } else {
        /*
         * A toplevel other than the root.
         * Share the GL context with the main window for efficient rendering.
         */
        glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE,             GLFW_TRUE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW,         GLFW_TRUE);
        glfwWindowHint(GLFW_AUTO_ICONIFY,          GLFW_FALSE);
        glfwWindowHint(GLFW_SCALE_FRAMEBUFFER,     GLFW_TRUE);

        glfwWindow = glfwCreateWindow(width, height,
                                      title ? title : "",
                                      NULL, mainGlfwWindow);
        if (!glfwWindow) {
            return NULL;
        }

        glfwMakeContextCurrent(glfwWindow);
        glfwSwapInterval(0);
        glfwShowWindow(glfwWindow);
        glfwSwapBuffers(glfwWindow);
    }

    glfwTkInfo *infoPtr = createGlfwTkInfo(glfwWindow, winPtr);
    fprintf(stderr, "nvgContext for %s is at %p\n",
            Tk_PathName(winPtr), infoPtr);

    if (glfwWindow == mainGlfwWindow) {
        mainGlfwContext = infoPtr->context;
    }

    glfwSetWindowUserPointer(glfwWindow, infoPtr);
    TkWaylandSetupCallbacks(glfwWindow);

    winPtr->privatePtr->glfwWindow = glfwWindow;
    winPtr->changes.width  = width;
    winPtr->changes.height = height;

    /* Set the initial pixel ratio for this window. */
    int fbWidth, fbHeight;
    float scale;
    glfwGetWindowContentScale(glfwWindow, &scale, NULL);
    fprintf(stderr, "Initial pixel ratio for %s is %f\n",
            Tk_PathName(winPtr), scale);

    /* Create a framebuffer for the backing store of the window. */
    glfwMakeContextCurrent(glfwWindow);
    glfwGetFramebufferSize(glfwWindow, &fbWidth, &fbHeight);
    winPtr->privatePtr->fb = nvgluCreateFramebuffer(infoPtr->context.vg,
                                                     fbWidth, fbHeight, 0);
    if (winPtr->privatePtr->fb == NULL) {
        fprintf(stderr, "Could not create NanoVG framebuffer\n");
    }

    fprintf(stderr, "Window %s has glfwWindow %p and framebuffer %p\n",
            Tk_PathName(winPtr), glfwWindow, winPtr->privatePtr->fb);

    nvgluBindFramebuffer(winPtr->privatePtr->fb);

    /* Check FBO completeness for now. */
    int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FBO is incomplete (status=0x%x)\n", status);
    } else {
        fprintf(stderr, "Window %s has a complete framebuffer @ %p\n",
                Tk_PathName(winPtr), winPtr->privatePtr->fb);
    }

    if (drawableOut) {
        *drawableOut = TkWaylandDrawableForTkWindow(winPtr);
    }

    TkWaylandQueueExposeEvent(winPtr, 0, 0, width, height);

    return glfwWindow;
}




/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDestroyWindow --
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
TkWaylandDestroyWindow(GLFWwindow *glfwWindow)
{
    fprintf(stderr, "TkWaylandDestroyWindow\n");
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
        Tcl_DoWhenIdle(TkWaylandShutdown, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandBeginDraw --
 *
 *	Prepares the NanoVG context for drawing. Uses the provided
 *      dcPtr to store context-specific state.
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
TkWaylandBeginDraw(
    Drawable drawable,
    GC gc,
    TkWaylandDrawingContext *dcPtr)
{
    if (TkWaylandDrawableIsPixmap(drawable)) {
	TkWaylandPixmap *pixmap = TkWaylandPixmapFromDrawable(drawable);
	fprintf(stderr, "BeginDraw: received pixmap %p\n", pixmap);
	return TCL_OK;
    }
    TkWindow *childPtr = TkWaylandTkWindowFromDrawable(drawable);
    TkWindow *winPtr = childPtr;
    float x = 0, y = 0;
    while (!Tk_IsTopLevel(winPtr)) {
        x += winPtr->changes.x;
	y += winPtr->changes.y;
    	winPtr = winPtr->parentPtr;
    }
    fprintf(stderr, "BeginDraw: %s in toplevel %s with offset (%d, %d)\n",
	    Tk_PathName(childPtr), Tk_PathName(winPtr), (int)x, (int)y);

    /*
     * Now winPtr is the containing toplevel and the offsets of
     * the child are given by x and y.
     */
    GLFWwindow *glfwWindow = winPtr->privatePtr->glfwWindow;
    glfwTkInfo *infoPtr = getGlfwTkInfo(glfwWindow);

    /* Set up the nanoVG drawing context for this nvgFrame */
    dcPtr->vg = infoPtr->context.vg; 
    dcPtr->drawable = drawable;

    nvgResetTransform(dcPtr->vg);

    /*
     * Start a NanoVG frame for drawing on the backing store.
     * The width and height here should be the window dimensions,
     * not the framebuffer dimensions.
     */
    float scale;
    glfwGetWindowContentScale(glfwWindow, &scale, NULL);

    fprintf(stderr, "BeginFrame for toplevel %s with size %dx%d and pixel ratio %f\n",
	    Tk_PathName(winPtr), Tk_Width(winPtr), Tk_Height(winPtr), scale);
    nvgBeginFrame(dcPtr->vg, Tk_Width(winPtr), Tk_Height(winPtr), scale);

    /*
     * Import our graphics context and translate to the origin
     * of the window we are drawing into.
     */

    TkWaylandApplyGC(dcPtr->vg, gc);
    nvgTranslate(dcPtr->vg, x, y);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandEndDraw --
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
TkWaylandEndDraw(TkWaylandDrawingContext *dcPtr)
{
    if (!dcPtr || !dcPtr->vg) {
	fprintf(stderr, "No drawing context!\n");
	return;
    }
    //// This is the case where the drawable is a window.
    TkWindow *childPtr = TkWaylandTkWindowFromDrawable(dcPtr->drawable);
    fprintf(stderr, "EndDraw for %s\n", Tk_PathName(childPtr));
    TkWindow *winPtr = childPtr;
    while (!Tk_IsTopLevel(winPtr)) {
	winPtr = winPtr->parentPtr;
    }
    /* winPtr is the toplevel containing our drawable. */
    GLFWwindow *glfwWindow = winPtr->privatePtr->glfwWindow;

    /*
     * All nvg drawing since the call to nvgBeginFrame happens when we call
     * nvgEndFrame.  The drawing commands have just been queued.  Now they
     * actually get executed.  I think the viewport size should be the same as
     * the framebuffer size and the FBO size (the latter equality being
     * enforced in the FramebufferSizeCallback. But that size may be a
     * multiple of the window size, and the multiplier should be the pixel
     * ratio.
     */

    /* Make our GL context current and set the viewport. */
    glfwMakeContextCurrent(glfwWindow);
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(glfwWindow, &fbWidth, &fbHeight);
    fprintf(stderr, "Framebuffer size is now %d x %d\n", fbWidth, fbHeight);
    /* Bind our backing store framebuffer. */
    nvgluBindFramebuffer(winPtr->privatePtr->fb);

    fprintf(stderr, "EndDraw: setting viewport to %dx%d\n", fbWidth, fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);

    /* Check FBO completeness (for now). */

    int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {

        fprintf(stderr, "FBO is incomplete! (status=0x%x)\n", status);
    }
    nvgEndFrame(dcPtr->vg);
    fprintf(stderr, "EndFrame: drew %s in toplevel %s\n",
	   Tk_PathName(childPtr), Tk_PathName(winPtr));

    nvgluBindFramebuffer(NULL);

    /*
     * nvgBeginFrame calls nvgSave, but nvgEndFrame does not
     * call nvgRestore.  Maybe it is not important to balance
     * those, but we call nvgRestore here just in case.
     */

    nvgRestore(dcPtr->vg);

    /*
     * Drawing this widget covered up all of the widgets that it contains.  If
     * we generate expose events for the children of this widget and for its
     * siblings which are higher in the stacking order then we should have
     * redrawn all of the widgets that we damaged.
     */

#if 1
    /* Children */
    for (TkWindow *childPtr2 = childPtr->childList;
	 childPtr2 != NULL;
         childPtr2 = childPtr2->nextPtr) {
        if (!Tk_IsMapped(childPtr2)) {


            continue;
        }

        TkWaylandQueueExposeEvent(childPtr2, 0, 0, Tk_Width(childPtr2),
                                  Tk_Height(childPtr2));
    }
    /* Higher siblings. */
    for (TkWindow *childPtr2 = childPtr->nextPtr;
	 childPtr2 != NULL;
         childPtr2 = childPtr2->nextPtr) {
        if (!Tk_IsMapped(childPtr2)) {
            continue;
        }
        TkWaylandQueueExposeEvent(childPtr2, 0, 0, Tk_Width(childPtr2),
                                  Tk_Height(childPtr2));
    }
#endif

    /*

     * Mark the toplevel as needing display (unless we are in the middle of a
     * Tk double-buffer section).  This triggers a call to glfwSwapBuffers.
     */

    glfwTkInfo *infoPtr = getGlfwTkInfo(glfwWindow);
    ////if (!(infoPtr->flags & dontSwap)) {
    infoPtr->flags |= needsDisplay;
    ////}
}


/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetNVGContext --
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
TkWaylandGetNVGContext(
    Drawable drawable)
{
    if (TkWaylandDrawableIsPixmap(drawable)) {
	fprintf(stderr, "Contexts not available for pixmaps yet.\n");
	return NULL;
    }
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindowFromDrawable(drawable);
    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(glfwWindow);
    if (!infoPtr || shutdownInProgress) {
	fprintf(stderr, "TkWaylandGetNVContext: No UserPointer\n");
	return NULL;
    }
    return infoPtr->context.vg;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetNVGContextForMeasure --
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
TkWaylandGetNVGContextForMeasure(void)
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
 * TkWaylandXColorToNVG --
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
TkWaylandXColorToNVG(XColor *xcolor)
{
    if (!xcolor) return nvgRGBA(0, 0, 0, 255);
    return nvgRGBA(xcolor->red >> 8, xcolor->green >> 8, xcolor->blue >> 8, 255);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPixelToNVG --
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
TkWaylandPixelToNVG(unsigned long pixel)
{
    return nvgRGBA((pixel>>16)&0xFF, (pixel>>8)&0xFF, pixel&0xFF, 255);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandApplyGC --
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
TkWaylandApplyGC(NVGcontext *vg, GC gc)
{
    XGCValues v;
    NVGcolor  c;

    if (!vg || !gc || shutdownInProgress) return;

    TkWaylandGetGCValues(gc,
        GCForeground|GCLineWidth|GCLineStyle|GCCapStyle|GCJoinStyle, &v);
    c = TkWaylandPixelToNVG(v.foreground);
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
    if (TkWaylandInitialize() != TCL_OK) return TCL_ERROR;
    Tk_WaylandSetupTkNotifier();
    Tktray_Init(interp);
    SysNotify_Init(interp);
    Cups_Init(interp);
    TkWaylandAccessibility_Init(interp);
    TkWaylandKeyInit();
    TkWaylandMenuInit();

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
 * TkWaylandGetGLFWwindow --
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
 * TkWaylandGetTkWindow --
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
TkWaylandGetTkWindow(GLFWwindow *glfwWindow)
{
    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(glfwWindow);
    if (!infoPtr) {
	fprintf(stderr, "TkWaylandGetTkWindow: No UserPointer.\n");
	return NULL;
    }
    if (!infoPtr->winPtr) {
	fprintf(stderr, "TkWaylandGetTkWindow: No winPtr in User data.\n");
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
