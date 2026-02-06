/*
 * tkGlfwInit.c --
 *
 *	This file contains GLFW/Wayland-specific interpreter initialization functions.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Wayland/GLFW Port
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>
#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg.h>
#include <nanovg_gl.h>

/*
 * Global GLFW/Wayland context
 */
static TkGlfwContext glfwContext = {NULL, NULL, 0};

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwErrorCallback --
 *
 *	GLFW error callback function for debugging and error handling.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints error messages to stderr.
 *
 *----------------------------------------------------------------------
 */

void
TkGlfwErrorCallback(
    int error,
    const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwFramebufferSizeCallback --
 *
 *	Callback for when the framebuffer size changes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates viewport and triggers redraw.
 *
 *----------------------------------------------------------------------
 */

void
TkGlfwFramebufferSizeCallback(
    GLFWwindow* window,
    int width,
    int height)
{
    glViewport(0, 0, width, height);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwInitializeContext --
 *
 *	Initialize GLFW and create the nanovg context.
 *
 * Results:
 *	Returns TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initializes GLFW, creates window and nanovg context.
 *
 *----------------------------------------------------------------------
 */

int
TkGlfwInitializeContext(void)
{
    if (glfwContext.initialized) {
        return TCL_OK;
    }

    glfwSetErrorCallback(TkGlfwErrorCallback);

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return TCL_ERROR;
    }

    /* Request OpenGL 3.3 core profile for nanovg */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    
    /* Prefer Wayland over X11 if available */
    glfwWindowHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);

    /* Create a hidden main window for context */
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwContext.mainWindow = glfwCreateWindow(640, 480, "Tk Main", NULL, NULL);
    
    if (!glfwContext.mainWindow) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return TCL_ERROR;
    }

    glfwMakeContextCurrent(glfwContext.mainWindow);
    glfwSetFramebufferSizeCallback(glfwContext.mainWindow, TkGlfwFramebufferSizeCallback);
    
    /* Enable vsync */
    glfwSwapInterval(1);

    /* Initialize nanovg */
#ifdef NANOVG_GL3
    glfwContext.vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
#else
    glfwContext.vg = nvgCreateGL2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
#endif

    if (!glfwContext.vg) {
        fprintf(stderr, "Failed to initialize nanovg\n");
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
 * TkGlfwCleanupContext --
 *
 *	Cleanup GLFW and nanovg resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys nanovg context, GLFW window, and terminates GLFW.
 *
 *----------------------------------------------------------------------
 */

void
TkGlfwCleanupContext(void)
{
    if (!glfwContext.initialized) {
        return;
    }

    if (glfwContext.vg) {
#ifdef NANOVG_GL3
        nvgDeleteGL3(glfwContext.vg);
#else
        nvgDeleteGL2(glfwContext.vg);
#endif
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
 * TkWaylandGetTkWindow --
 *
 *	Get Tk window from GLFW window.
 *
 * Results:
 *	TkWindow pointer or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkWindow*
TkWaylandGetTkWindow(
    GLFWwindow* glfwWindow)
{
    if (!glfwWindow) {
        return NULL;
    }
    
    /* First check user pointer */
    TkWindow* tkWin = (TkWindow*)glfwGetWindowUserPointer(glfwWindow);
    if (tkWin) {
        return tkWin;
    }
    
    /* Fall back to mapping table */
    for (int i = 0; i < numWindowMappings; i++) {
        if (windowMappings[i].glfwWindow == glfwWindow) {
            return windowMappings[i].tkWindow;
        }
    }
    
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetGLFWWindow --
 *
 *	Get GLFW window from Tk window.
 *
 * Results:
 *	GLFWwindow pointer or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

GLFWwindow*
TkWaylandGetGLFWWindow(
    Tk_Window tkWindow)
{
    for (int i = 0; i < numWindowMappings; i++) {
        if (windowMappings[i].tkWindow == (TkWindow*)tkWindow) {
            return windowMappings[i].glfwWindow;
        }
    }
    return NULL;
}

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
    
    /* Get library path */
    GetLibraryPath(interp);

    /* Initialize event loop. */
    Tk_WaylandSetupTkNotifier();
    
    /* Initialize subsystems */
    Tktray_Init(interp);
    (void)SysNotify_Init(interp);
    Icu_Init(interp);
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
