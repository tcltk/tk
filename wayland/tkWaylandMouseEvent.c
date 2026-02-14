/*
 * tkWaylandMouseEvent.c --
 *
 *	This file implements functions that decode & handle mouse events on
 *	Wayland using GLFW.
 *
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>

typedef struct {
    unsigned int state;
    long delta;
    Window window;
    int globalX, globalY;	/* Global screen coordinates */
    int localX, localY;		/* Local window coordinates */
} MouseEventData;

static Tk_Window captureWinPtr = NULL;	/* Current capture window; may be
					 * NULL. */

static int GenerateButtonEvent(MouseEventData *medPtr);

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandButtonKeyState --
 *
 *	Returns the current state of the button & modifier keys.
 *
 * Results:
 *	A bitwise inclusive OR of a subset of the following: Button1Mask,
 *	ShiftMask, LockMask, ControlMask, Mod*Mask.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned int
TkWaylandButtonKeyState(void)
{
    unsigned int state = 0;
    
    /* Get current focused GLFW window. */
    GLFWwindow* window = glfwGetCurrentContext();
    if (!window) {
        return 0;
    }
    
    /* Check mouse buttons. */
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        state |= Tk_GetButtonMask(Button1);
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        state |= Tk_GetButtonMask(Button3);
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
        state |= Tk_GetButtonMask(Button2);
    }
    
    /* Check keyboard modifiers. */
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
        state |= ShiftMask;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) {
        state |= ControlMask;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS) {
        state |= Mod1Mask;
    }
    if (glfwGetKey(window, GLFW_KEY_CAPS_LOCK) == GLFW_PRESS) {
        state |= LockMask;
    }
    
    return state;
}

/*
 *----------------------------------------------------------------------
 *
 * XQueryPointer --
 *
 *	Check the current state of the mouse. This is not a complete
 *	implementation of this function. It only computes the root coordinates
 *	and the current mask.
 *
 * Results:
 *	Sets root_x_return, root_y_return, and mask_return. Returns true on
 *	success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Bool
XQueryPointer(
    TCL_UNUSED(Display *),
    Window w,
    TCL_UNUSED(Window *),
    TCL_UNUSED(Window *),
    int *root_x_return,
    int *root_y_return,
    int *win_x_return,
    int *win_y_return,
    unsigned int *mask_return)
{
    GLFWwindow* glfwWindow;
    double cursorX, cursorY;
    int getGlobal = (root_x_return && root_y_return);
    int getLocal = (win_x_return && win_y_return && w != None);
    TkWindow *winPtr = (TkWindow *)w;
    
    if (!winPtr) {
        return False;
    }
    
    /* Get the GLFW window using unified architecture. */
    glfwWindow = TkGlfwGetGLFWWindow((Tk_Window)winPtr);
    if (!glfwWindow) {
        return False;
    }
    
    if (getGlobal || getLocal) {
        glfwGetCursorPos(glfwWindow, &cursorX, &cursorY);
        
        if (getGlobal) {
            /* Get window position for global coordinates. */
            int winX, winY;
            glfwGetWindowPos(glfwWindow, &winX, &winY);
            *root_x_return = winX + (int)cursorX;
            *root_y_return = winY + (int)cursorY;
        }
        
        if (getLocal) {
            *win_x_return = (int)cursorX;
            *win_y_return = (int)cursorY;
        }
    }
    
    if (mask_return) {
        *mask_return = TkWaylandButtonKeyState();
    }
    
    return True;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGenerateButtonEventForXPointer --
 *
 *	This procedure generates an X button event for the current pointer
 *	state as reported by XQueryPointer().
 *
 * Results:
 *	True if event(s) are generated - false otherwise.
 *
 * Side effects:
 *	Additional events may be placed on the Tk event queue. Grab state may
 *	also change.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGenerateButtonEventForXPointer(
    Window window)		/* X Window containing button event. */
{
    MouseEventData med;
    int global_x, global_y, local_x, local_y;

    memset(&med, 0, sizeof(MouseEventData));
    XQueryPointer(NULL, window, NULL, NULL, &global_x, &global_y,
	    &local_x, &local_y, &med.state);
    med.globalX = global_x;
    med.globalY = global_y;
    med.localX = local_x;
    med.localY = local_y;
    med.window = window;

    return GenerateButtonEvent(&med);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGenerateButtonEvent --
 *
 *	Given a global x & y position and the button key status this procedure
 *	generates the appropriate X button event. It also handles the state
 *	changes needed to implement implicit grabs.
 *
 * Results:
 *	True if event(s) are generated, false otherwise.
 *
 * Side effects:
 *	Additional events may be placed on the Tk event queue. Grab state may
 *	also change.
 *
 *----------------------------------------------------------------------
 */

int
TkGenerateButtonEvent(
    int x,			/* X location of mouse, */
    int y,			/* Y location of mouse. */
    Window window,		/* X Window containing button event. */
    unsigned int state)		/* Button Key state suitable for X event. */
{
    MouseEventData med;

    memset(&med, 0, sizeof(MouseEventData));
    med.state = state;
    med.window = window;
    med.globalX = x;
    med.globalY = y;
    med.localX = x;
    med.localY = y;

    return GenerateButtonEvent(&med);
}

/*
 *----------------------------------------------------------------------
 *
 * GenerateButtonEvent --
 *
 *	Generate an X button event from a MouseEventData structure. Handles
 *	the state changes needed to implement implicit grabs.
 *
 * Results:
 *	True if event(s) are generated - false otherwise.
 *
 * Side effects:
 *	Additional events may be placed on the Tk event queue. Grab state may
 *	also change.
 *
 *----------------------------------------------------------------------
 */

static int
GenerateButtonEvent(
    MouseEventData *medPtr)
{
    Tk_Window tkwin;
    int dummy;
    TkDisplay *dispPtr;

    dispPtr = TkGetDisplayList();
    tkwin = Tk_IdToWindow(dispPtr->display, medPtr->window);

    if (tkwin != NULL) {
	tkwin = Tk_CoordsToWindow(medPtr->localX, medPtr->localY, tkwin);
    }
    Tk_UpdatePointer(tkwin, medPtr->globalX, medPtr->globalY, medPtr->state);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpWarpPointer --
 *
 *	Move the mouse cursor to the screen location specified by the warpX and
 *	warpY fields of a TkDisplay.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	The mouse cursor is moved.
 *
 *----------------------------------------------------------------------
 */

void
TkpWarpPointer(
    TkDisplay *dispPtr)
{
    GLFWwindow* glfwWindow;
    int x, y;
    int winX, winY;
    double targetX, targetY;
    
    if (dispPtr->warpWindow) {
	Tk_GetRootCoords(dispPtr->warpWindow, &x, &y);
	
	/* Warp cursor to new position using unified architecture. */
	glfwWindow = TkGlfwGetGLFWWindow(dispPtr->warpWindow);
	if (glfwWindow) {
	    glfwGetWindowPos(glfwWindow, &winX, &winY);
	    targetX = x + dispPtr->warpX - winX;
	    targetY = y + dispPtr->warpY - winY;
	    glfwSetCursorPos(glfwWindow, targetX, targetY);
	}
    } else {
	/* Global warp - not directly supported by GLFW. */
    }

    if (dispPtr->warpWindow) {
	TkGenerateButtonEventForXPointer(Tk_WindowId(dispPtr->warpWindow));
    } else {
	TkGenerateButtonEventForXPointer(None);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetCapture --
 *
 *	This function captures the mouse so that all future events will be
 *	reported to this window, even if the mouse is outside the window. If
 *	the specified window is NULL, then the mouse is released.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the capture flag and captures the mouse.
 *
 *----------------------------------------------------------------------
 */

void
TkpSetCapture(
    TkWindow *winPtr)		/* Capture window, or NULL. */
{
    GLFWwindow* glfwWindow;
    
    while (winPtr && !Tk_IsTopLevel(winPtr)) {
	winPtr = winPtr->parentPtr;
    }
    captureWinPtr = (Tk_Window)winPtr;
    
    /* Set GLFW cursor mode using unified architecture. */
    glfwWindow = TkGlfwGetGLFWWindow((Tk_Window)winPtr);
    if (glfwWindow) {
	if (winPtr) {
	    glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	} else {
	    glfwSetInputMode(glfwWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetCapture --
 *
 * Results:
 *	Returns the current grab window
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tk_Window
TkpGetCapture(void)
{
    return captureWinPtr;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
