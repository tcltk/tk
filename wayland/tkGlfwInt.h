/*
 * tkGlfwInt.h --
 *
 *	This file contains declarations that are shared among the
 *	GLFW/Wayland-specific parts of Tk but aren't used by the rest of Tk.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#ifndef _TKGLFWINT_H
#define _TKGLFWINT_H

#include "tkInt.h"
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>
#include "nanovg.h"
#include "tkIntPlatDecls.h" 

/*
 *----------------------------------------------------------------------
 *
 * Core Context Structure
 *
 *----------------------------------------------------------------------
 */

typedef struct {
    GLFWwindow *mainWindow;     /* Shared context window */
    NVGcontext *vg;             /* Global NanoVG context */
    int initialized;            /* Initialization flag */
} TkGlfwContext;

/*
 *----------------------------------------------------------------------
 *
 * Window Mapping Structures
 *
 *      These maintain the bidirectional mapping between Tk windows,
 *      GLFW windows, and Drawables.
 *
 *----------------------------------------------------------------------
 */

typedef struct WindowMapping {
    TkWindow *tkWindow;         /* Tk window pointer */
    GLFWwindow *glfwWindow;     /* Corresponding GLFW window */
    Drawable drawable;          /* X11-style drawable ID */
    int width;                  /* Current width */
    int height;                 /* Current height */
    struct WindowMapping *nextPtr; /* Next in linked list */
} WindowMapping;

/*
 *----------------------------------------------------------------------
 *
 * Drawing Context Structure
 *
 *      Temporary structure used during drawing operations to maintain
 *      state and ensure proper cleanup.
 *
 *----------------------------------------------------------------------
 */

typedef struct {
    NVGcontext *vg;             /* NanoVG context for this draw */
    Drawable drawable;          /* Target drawable */
    GLFWwindow *glfwWindow;     /* Associated GLFW window */
    int width;                  /* Drawable width */
    int height;                 /* Drawable height */
} TkWaylandDrawingContext;

/*
 *----------------------------------------------------------------------
 *
 * Global State Access
 *
 *----------------------------------------------------------------------
 */

/* Get the global GLFW/NanoVG context. */
MODULE_SCOPE TkGlfwContext* TkGlfwGetContext(void);

/*
 *----------------------------------------------------------------------
 *
 * Initialization and Cleanup
 *
 *----------------------------------------------------------------------
 */

/* Initialize GLFW and NanoVG. */
MODULE_SCOPE int TkGlfwInitialize(void);

/* Clean up all GLFW/NanoVG resources. */
MODULE_SCOPE void TkGlfwCleanup(void);

/*
 *----------------------------------------------------------------------
 *
 * Window Management
 *
 *----------------------------------------------------------------------
 */

/* Create a new GLFW window and register mapping. */
MODULE_SCOPE GLFWwindow* TkGlfwCreateWindow(
    TkWindow *tkWin,
    int width,
    int height,
    const char *title,
    Drawable *drawableOut);

/* Destroy a GLFW window and remove mapping. */
MODULE_SCOPE void TkGlfwDestroyWindow(GLFWwindow *glfwWindow);

/* Get GLFW window from Tk window. */
MODULE_SCOPE GLFWwindow* TkGlfwGetGLFWWindow(Tk_Window tkwin);

/* Get Tk window from GLFW window. */
MODULE_SCOPE TkWindow* TkGlfwGetTkWindow(GLFWwindow *glfwWindow);

/* Get GLFW window from Drawable ID. */
MODULE_SCOPE GLFWwindow* TkGlfwGetWindowFromDrawable(Drawable drawable);

/* Update window size in mapping. */
MODULE_SCOPE void TkGlfwUpdateWindowSize(GLFWwindow *glfwWindow, 
                                          int width, int height);

/*
 *----------------------------------------------------------------------
 *
 * Drawing Context Management
 *
 *----------------------------------------------------------------------
 */

/* Set up drawing context for a drawable. */
MODULE_SCOPE int TkGlfwBeginDraw(
    Drawable drawable,
    GC gc,
    TkWaylandDrawingContext *dcPtr);

/* Clean up and present drawing context. */
MODULE_SCOPE void TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr);

/* Get NanoVG context for current drawing. */
MODULE_SCOPE NVGcontext* TkGlfwGetNVGContext(void);

/*
 *----------------------------------------------------------------------
 *
 * Event Processing
 *
 *----------------------------------------------------------------------
 */

/* Process pending GLFW events. */
MODULE_SCOPE void TkGlfwProcessEvents(void);

/* Set up standard GLFW callbacks for a window. */
MODULE_SCOPE void TkGlfwSetupCallbacks(GLFWwindow *glfwWindow, 
                                        TkWindow *tkWin);

/* Set up event loop notifier. */										
void Tk_WaylandSetupTkNotifier(void);

/*
 *----------------------------------------------------------------------
 *
 * Utility Functions
 *
 *----------------------------------------------------------------------
 */

/* Convert XColor to NVGcolor. */
MODULE_SCOPE NVGcolor TkGlfwXColorToNVG(XColor *xcolor);

/* Convert pixel value to NVGcolor. */
MODULE_SCOPE NVGcolor TkGlfwPixelToNVG(unsigned long pixel);

/* Apply GC settings to NanoVG context. */
MODULE_SCOPE void TkGlfwApplyGC(NVGcontext *vg, GC gc);

/*
 *----------------------------------------------------------------------
 *
 * GLFW Callback Functions
 *
 *      These are the standard callbacks that should be registered
 *      for all GLFW windows. They translate GLFW events into
 *      appropriate Tk events.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void TkGlfwWindowCloseCallback(GLFWwindow *window);
MODULE_SCOPE void TkGlfwWindowSizeCallback(GLFWwindow *window, 
                                            int width, int height);
MODULE_SCOPE void TkGlfwFramebufferSizeCallback(GLFWwindow *window, 
                                                 int width, int height);
MODULE_SCOPE void TkGlfwWindowPosCallback(GLFWwindow *window, 
                                           int xpos, int ypos);
MODULE_SCOPE void TkGlfwWindowFocusCallback(GLFWwindow *window, 
                                             int focused);
MODULE_SCOPE void TkGlfwWindowIconifyCallback(GLFWwindow *window, 
                                               int iconified);
MODULE_SCOPE void TkGlfwWindowMaximizeCallback(GLFWwindow *window, 
                                                int maximized);
MODULE_SCOPE void TkGlfwCursorPosCallback(GLFWwindow *window, 
                                           double xpos, double ypos);
MODULE_SCOPE void TkGlfwMouseButtonCallback(GLFWwindow *window, 
                                             int button, int action, int mods);
MODULE_SCOPE void TkGlfwScrollCallback(GLFWwindow *window, 
                                        double xoffset, double yoffset);
MODULE_SCOPE void TkGlfwKeyCallback(GLFWwindow *window, 
                                     int key, int scancode, 
                                     int action, int mods);
MODULE_SCOPE void TkGlfwCharCallback(GLFWwindow *window, 
                                      unsigned int codepoint);
/*
 *----------------------------------------------------------------------
 *
 * Keyboard Handling (xkbcommon integration)
 *
 *----------------------------------------------------------------------
 */

/* Update keyboard modifiers for xkbcommon state. */
MODULE_SCOPE void TkWaylandUpdateKeyboardModifiers(int glfw_mods);

/* Store character input from GLFW character callback. */
MODULE_SCOPE void TkWaylandStoreCharacterInput(unsigned int codepoint);


/*
 *----------------------------------------------------------------------
 *
 * Error Handling
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void TkGlfwErrorCallback(int error, const char *description);


/*
 *----------------------------------------------------------------------
 *
 * Functions from the tkUnix source tree
 *
 *----------------------------------------------------------------------
 */
MODULE_SCOPE  int       Tktray_Init (Tcl_Interp* interp);
MODULE_SCOPE  int       SysNotify_Init (Tcl_Interp* interp);
MODULE_SCOPE  int       Cups_Init (Tcl_Interp* interp);
MODULE_SCOPE  int       TkAtkAccessibility_Init (Tcl_Interp *interp) ;


#endif /* _TKGLFWINT_H */

/* Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
