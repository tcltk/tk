/*
 * tkUnixFocus.c --
 *
 *	This file contains platform specific functions that manage focus for
 *	Tk.
 *
 * Copyright © 1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkUnixInt.h"
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
	
    size_t serial = 0;

    /*
     * Don't set the focus to a window that's marked override-redirect.
     * This is a hack to avoid problems with menus under olvwm: if we move
     * the focus then the focus can get lost during keyboard traversal.
     * Fortunately, we don't really need to move the focus for menus: events
     * will still find their way to the focus window, and menus aren't
     * decorated anyway so the window manager doesn't need to hear about the
     * focus change in order to redecorate the menu.
     */

    if (winPtr->atts.override_redirect) {
        return serial;
    }

    /*
     * In Wayland/GLFW, we don't have direct access to a global focus tree
     * like X11's XQueryTree. Instead, we rely on GLFW's window focus APIs.
     * The force flag is handled by directly focusing the window if allowed.
     */

    if (!force) {
        /*
         * Check if the current focused window belongs to the same application.
         * In GLFW, we can get the currently focused window and compare its
         * user pointer (which we set to TkWindow* when creating the window).
         */
        GLFWwindow *currentFocused = glfwGetWindowUserPointer(glfwGetCurrentContext());
        if (currentFocused != NULL) {
            TkWindow *currentWinPtr = (TkWindow*)glfwGetWindowUserPointer(currentFocused);
            if (currentWinPtr == NULL || currentWinPtr->mainPtr != winPtr->mainPtr) {
                return serial;
            }
        } else {
            /*
             * No current focus or it's not our window. Without force, we should
             * not change focus.
             */
            return serial;
        }
    }

    /*
     * Change focus to the target window using GLFW.
     * First, get the GLFWwindow associated with winPtr.
     * We assume that winPtr->window holds a GLFWwindow* in this port.
     */
    GLFWwindow *glfwWin = (GLFWwindow*)winPtr->window;
    if (glfwWin == NULL) {
        Tcl_Panic("ChangeFocus got null GLFW window");
    }

    /*
     * In GLFW, we request focus for the window.
     * Note: In Wayland, the compositor may choose to grant or deny focus.
     */
    glfwFocusWindow(glfwWin);

    /*
     * We don't have an exact equivalent to X11's NextRequest serial in GLFW.
     * For compatibility, we return a non-zero value to indicate a focus change.
     * Here we use a simple incrementing counter per display.
     */
     
	serial++;

    /*
     * Flush any pending GLFW events to ensure the focus request is processed.
     */
    glfwPollEvents();

    return serial;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
