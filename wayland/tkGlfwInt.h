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

#ifndef _TKGLFWINT
#define _TKGLFWINT

#ifndef _TKINT
#include "tkInt.h"
#endif

#include <GLFW/glfw3.h>
#include <nanovg.h>

/*
 * Platform-specific data structures for GLFW/Wayland
 */

typedef struct TkGlfwContext {
    GLFWwindow *mainWindow;
    NVGcontext *vg;
    int initialized;
} TkGlfwContext;



/*
 * GLFW/Wayland-specific internal functions
 */

MODULE_SCOPE  void      TkGlfwErrorCallback(int error, const char* description);
MODULE_SCOPE  void      TkGlfwFramebufferSizeCallback(GLFWwindow* window, int width, int height);
MODULE_SCOPE  int       TkGlfwInitializeContext(void);
MODULE_SCOPE  void      TkGlfwCleanupContext(void);

#endif /* _TKGLFWINT */


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
