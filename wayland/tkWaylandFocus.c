/*
 * tkWaylandFocus.c --
 *
 *	This file contains platform specific functions that manage focus for
 *	Tk on Wayland/GLFW.
 *
 * Copyright © 1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkGlfwInt.h"  /* Changed from tkUnixInt.h */
#include <GLFW/glfw3.h>

/*
 *----------------------------------------------------------------------
 *
 * TkpChangeFocus --
 *
 *	This function is invoked to move the official focus from one window
 *	to another.
 *
 * Results:
 *	The return value is the serial number of the command that changed the
 *	focus. It may be needed by the caller to filter out focus change
 *	events that were queued before the command. If the function doesn't
 *	actually change the focus then it returns 0.
 *
 * Side effects:
 *	The official focus window changes; the application's focus window
 *	isn't changed by this function.
 *
 *----------------------------------------------------------------------
 */

size_t
TkpChangeFocus(
    TkWindow *winPtr,		/* Window that is to receive the focus. */
    int force)			/* Non-zero means claim the focus even if it
				 * didn't originally belong to topLevelPtr's
				 * application. */
{
    static size_t serial_counter = 0;  /* Simple counter for serial numbers */
    size_t serial = 0;

    /*
     * Don't set the focus to a window that's marked override-redirect.
     */
    if (winPtr->atts.override_redirect) {
        return serial;
    }

    /*
     * Get the GLFW window associated with this Tk window using the
     * accessor function from the header.
     */
    GLFWwindow *glfwWin = TkGlfwGetGLFWWindow((Tk_Window)winPtr);
    if (glfwWin == NULL) {
        Tcl_Panic("TkpChangeFocus: No GLFW window found for Tk window");
    }

    if (!force) {
        /*
         * Check if the current focused window belongs to the same application.
         */
        GLFWwindow *currentFocused = glfwGetCurrentContext();
        if (currentFocused != NULL) {
            TkWindow *currentWinPtr = TkGlfwGetTkWindow(currentFocused);
            if (currentWinPtr == NULL || currentWinPtr->mainPtr != winPtr->mainPtr) {
                return serial;
            }
        } else {
            /*
             * No current focus. Without force, don't change focus.
             */
            return serial;
        }
    }

    /*
     * Request focus for the target window.
     */
    glfwFocusWindow(glfwWin);

    /*
     * Generate a serial number to indicate focus changed.
     */
    serial = ++serial_counter;

    /*
     * Process pending events to ensure focus request is handled.
     */
    TkGlfwProcessEvents();  /* Use the header's event processing function */

    return serial;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */