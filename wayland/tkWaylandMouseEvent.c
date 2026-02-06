/*
 * tkWaylandMouseEvent.c --
 *
 *	This file implements functions that decode & handle mouse events on
 *	Wayland using GLFW.
 *
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Ported to Wayland/GLFW in 2024
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkGlfwint.h"
#include <GLFW/glfw3.h>

typedef struct {
    unsigned int state;
    long delta;
    Window window;
    Point global;
    Point local;
} MouseEventData;

static Tk_Window captureWinPtr = NULL;	/* Current capture window; may be
					 * NULL. */

static int		GenerateButtonEvent(MouseEventData *medPtr);

/* GLFW callback functions. */
static void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
static void glfwCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
static void glfwCursorEnterCallback(GLFWwindow* window, int entered);
static void glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset);


static WindowMapping* windowMappings = NULL;
static int numWindowMappings = 0;

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandInitializeMouseHandling --
 *
 *	Initialize GLFW mouse callbacks for a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets up GLFW callbacks.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandInitializeMouseHandling(
    GLFWwindow* glfwWindow,
    TkWindow* tkWindow)
{
    /* Store the mapping. */
    windowMappings = (WindowMapping*)realloc(windowMappings, 
        (numWindowMappings + 1) * sizeof(WindowMapping));
    windowMappings[numWindowMappings].glfwWindow = glfwWindow;
    windowMappings[numWindowMappings].tkWindow = tkWindow;
    numWindowMappings++;
    
    /* Set up GLFW callbacks. */
    glfwSetMouseButtonCallback(glfwWindow, glfwMouseButtonCallback);
    glfwSetCursorPosCallback(glfwWindow, glfwCursorPosCallback);
    glfwSetCursorEnterCallback(glfwWindow, glfwCursorEnterCallback);
    glfwSetScrollCallback(glfwWindow, glfwScrollCallback);
    
    /* Store user pointer to Tk window. */
    glfwSetWindowUserPointer(glfwWindow, tkWindow);
}



/* GLFW Callback Implementations */

/*
 *----------------------------------------------------------------------
 *
 * glfwMouseButtonCallback --
 *
 *	GLFW mouse button callback.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Processes mouse button events.
 *
 *----------------------------------------------------------------------
 */

static void
glfwMouseButtonCallback(
    GLFWwindow* window,
    int button,
    int action,
    int mods)
{
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    
    WaylandProcessMouseEvent(window, GLFW_MOUSE_BUTTON, button, action, mods, 
                           xpos, ypos, 0.0, 0.0);
}

/*
 *----------------------------------------------------------------------
 *
 * glfwCursorPosCallback --
 *
 *	GLFW cursor position callback.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Processes mouse motion events.
 *
 *----------------------------------------------------------------------
 */

static void
glfwCursorPosCallback(
    GLFWwindow* window,
    double xpos,
    double ypos)
{
    /* Get current mouse button states. */
    int mods = 0;
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        mods |= GLFW_MOD_MOUSE_BUTTON_LEFT;
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        mods |= GLFW_MOD_MOUSE_BUTTON_RIGHT;
    }
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
        mods |= GLFW_MOD_MOUSE_BUTTON_MIDDLE;
    }
    
    /* Get keyboard modifiers. */
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
        mods |= GLFW_MOD_SHIFT;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) {
        mods |= GLFW_MOD_CONTROL;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS) {
        mods |= GLFW_MOD_ALT;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS) {
        mods |= GLFW_MOD_SUPER;
    }
    
    WaylandProcessMouseEvent(window, GLFW_CURSOR_MOVED, 0, 0, mods, 
                           xpos, ypos, 0.0, 0.0);
}

/*
 *----------------------------------------------------------------------
 *
 * glfwCursorEnterCallback --
 *
 *	GLFW cursor enter/leave callback.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Processes enter/leave events.
 *
 *----------------------------------------------------------------------
 */

static void
glfwCursorEnterCallback(
    GLFWwindow* window,
    int entered)
{
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    
    int mods = 0;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
        mods |= GLFW_MOD_SHIFT;
    }
    
    if (entered) {
        WaylandProcessMouseEvent(window, GLFW_CURSOR_ENTER, 0, 0, mods, 
                               xpos, ypos, 0.0, 0.0);
    } else {
        WaylandProcessMouseEvent(window, GLFW_CURSOR_LEAVE, 0, 0, mods, 
                               xpos, ypos, 0.0, 0.0);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * glfwScrollCallback --
 *
 *	GLFW scroll callback.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Processes scroll events.
 *
 *----------------------------------------------------------------------
 */

static void
glfwScrollCallback(
    GLFWwindow* window,
    double xoffset,
    double yoffset)
{
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    
    int mods = 0;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
        mods |= GLFW_MOD_SHIFT;
    }
    
    WaylandProcessMouseEvent(window, GLFW_SCROLL, 0, 0, mods, 
                           xpos, ypos, xoffset, yoffset);
}

/*
 *----------------------------------------------------------------------
 *
 * WaylandProcessMouseEvent --
 *
 *	Process mouse events from GLFW and convert them to Tk events.
 *
 * Results:
 *	Processes the event and generates Tk events.
 *
 * Side effects:
 *	May generate Tk events and update internal state.
 *
 *----------------------------------------------------------------------
 */

int
WaylandProcessMouseEvent(
    GLFWwindow* glfwWindow,
    int eventType,
    int button,
    int action,
    int mods,
    double x,
    double y,
    double scrollX,
    double scrollY)
{
    TkWindow *winPtr = NULL, *grabWinPtr, *scrollTarget = NULL;
    Tk_Window tkwin = NULL, capture, target;
    double localX, localY;
    double globalX, globalY;
    TkWindow *newFocus = NULL;
    int win_x, win_y;
    unsigned int buttonState = 0;
    int isTestingEvent = 0;
    int isMotionEvent = 0;
    int isOutside = 0;
    int firstDrag = 0;
    static int ignoreDrags = 0;
    static int ignoreUpDown = 0;
    static double timestamp = 0;
    static unsigned int lastButtonState = 0;

    /* Get the Tk window associated with this GLFW window. */
    winPtr = TkWaylandGetTkWindow(glfwWindow);
    if (!winPtr && !isTestingEvent) {
        /* If no window, ignore event. */
        return 0;
    }

    /* Check if pointer is outside window bounds. */
    int width, height;
    glfwGetWindowSize(glfwWindow, &width, &height);
    if (x < 0 || x >= width || y < 0 || y >= height) {
        isOutside = 1;
    }

    /* Map GLFW button numbers to Tk button numbers. */
    int tkButton = button + 1; /* GLFW buttons are 0-based, Tk buttons are 1-based */
    if ((tkButton & -2) == 2) {
        tkButton ^= 1; /* Swap buttons 2/3 */
    }

    /* Update button state based on event. */
    unsigned int buttonMask = Tk_GetButtonMask(tkButton);
    
    switch (eventType) {
        case GLFW_MOUSE_BUTTON:
            if (action == GLFW_PRESS) {
                buttonState = lastButtonState | buttonMask;
                
                /* Work-around for missing button up events. */
                if (tkButton == 1 && (lastButtonState & buttonMask)) {
                    int fakeState = lastButtonState & ~buttonMask;
                    Tk_UpdatePointer((Tk_Window)winPtr, x, y, fakeState);
                }
                
                /* Check for double-click. */
                double currentTime = glfwGetTime();
                if (currentTime - timestamp < 0.5) { /* 500ms double-click threshold */
                    if (ignoreUpDown == 1) {
                        return 0;
                    } else {
                        timestamp = currentTime;
                        ignoreUpDown = 1;
                    }
                } else {
                    ignoreUpDown = 0;
                }
                
                /* Ignore clicks on window edges/resize areas. */
                if (!isTestingEvent) {
                    if (x < 2 || x >= width - 2 || y < 2 || y >= height - 2) {
                        return 0;
                    }
                    /* Check for resize grip area. */
                    if (x >= width - 10 && y < 10) {
                        return 0;
                    }
                }
                
            } else if (action == GLFW_RELEASE) {
                buttonState = lastButtonState & ~buttonMask;
                if (tkButton == 1) {
                    /* Left button release. */
                    if (ignoreUpDown && ignoreDrags) {
                        ignoreDrags = 0;
                        return 0;
                    }
                }
            }
            break;
            
        case GLFW_CURSOR_ENTER:
            if (isOutside) {
                return 0;
            }
            /* Pointer entered window. */
            break;
            
        case GLFW_CURSOR_LEAVE:
            if (!isOutside) {
                return 0;
            }
            /* Pointer left window. */
            break;
            
        case GLFW_CURSOR_MOVED:
            isMotionEvent = 1;
            buttonState = lastButtonState;
            break;
            
        case GLFW_SCROLL:
            /* Scroll wheel events. */
            scrollTarget = winPtr;
            buttonState = lastButtonState;
            break;
            
        default:
            return 0;
    }
    
    /* Store the updated button state. */
    lastButtonState = buttonState;

    /* Get window position for global coordinates. */
    int winPosX, winPosY;
    glfwGetWindowPos(glfwWindow, &winPosX, &winPosY);
    
    /* Convert to global and local coordinates. */
    globalX = winPosX + x;
    globalY = winPosY + y;
    localX = x;
    localY = y;

    /* Adjust coordinates for embedded windows. */
    if (winPtr && Tk_IsEmbedded(winPtr)) {
        TkWindow *contPtr = (TkWindow *)Tk_GetOtherWindow((Tk_Window)winPtr);
        if (Tk_IsTopLevel(contPtr)) {
            localX -= contPtr->wmInfoPtr->xInParent;
            localY -= contPtr->wmInfoPtr->yInParent;
        }
    } else if (winPtr && winPtr->wmInfoPtr) {
        localX -= winPtr->wmInfoPtr->xInParent;
        localY -= winPtr->wmInfoPtr->yInParent;
    }

    /* Find the target window for the event. */
    if (eventType == GLFW_SCROLL) {
        target = (Tk_Window)scrollTarget;
    } else {
        target = Tk_TopCoordsToWindow((Tk_Window)winPtr, localX, localY, &win_x, &win_y);
    }

    /* Check grab state. */
    grabWinPtr = winPtr->dispPtr->grabWinPtr;
    
    if (grabWinPtr && !winPtr->dispPtr->grabFlags && 
        grabWinPtr->mainPtr == winPtr->mainPtr) {
        /* Local grab check. */
        if (!target) {
            return 0;
        }
        Tk_Window w;
        for (w = target; !Tk_IsTopLevel(w); w = Tk_Parent(w)) {
            if (w == (Tk_Window)grabWinPtr) {
                break;
            }
        }
        if (w != (Tk_Window)grabWinPtr) {
            return 0;
        }
    }

    if (grabWinPtr && winPtr->dispPtr->grabFlags && 
        grabWinPtr->mainPtr == winPtr->mainPtr) {
        /* Global grab check. */
        if (!target) {
            return 0;
        }
        Tk_Window w;
        for (w = target; !Tk_IsTopLevel(w); w = Tk_Parent(w)) {
            if (w == (Tk_Window)grabWinPtr) {
                break;
            }
        }
        if (w != (Tk_Window)grabWinPtr) {
            TkpChangeFocus(grabWinPtr, 1);
        }
    }

    /* Translate modifier keys. */
    unsigned int state = buttonState;
    if (mods & GLFW_MOD_CAPS_LOCK) {
        state |= LockMask;
    }
    if (mods & GLFW_MOD_SHIFT) {
        state |= ShiftMask;
    }
    if (mods & GLFW_MOD_CONTROL) {
        state |= ControlMask;
    }
    if (mods & GLFW_MOD_ALT) {
        state |= Mod1Mask;  /* Alt key. */
    }
    if (mods & GLFW_MOD_SUPER) {
        state |= Mod2Mask;  /* Super/Windows key. */
    }
    if (mods & GLFW_MOD_NUM_LOCK) {
        state |= Mod3Mask;
    }

    /* Generate XEvents. */
    if (eventType != GLFW_SCROLL) {
        if (eventType == GLFW_CURSOR_ENTER) {
            Tk_UpdatePointer(target, globalX, globalY, state);
        } else if (eventType == GLFW_CURSOR_LEAVE) {
            Tk_UpdatePointer(NULL, globalX, globalY, state);
        } else if (eventType == GLFW_CURSOR_MOVED) {
            if (target) {
                Tk_UpdatePointer(target, globalX, globalY, state);
            } else {
                /* Generate MotionNotify event for outside window. */
                XEvent xEvent = {0};
                xEvent.type = MotionNotify;
                xEvent.xany.send_event = 0;
                xEvent.xany.display = Tk_Display((Tk_Window)winPtr);
                xEvent.xany.window = Tk_WindowId((Tk_Window)winPtr);
                xEvent.xmotion.x = win_x;
                xEvent.xmotion.y = win_y;
                xEvent.xmotion.x_root = globalX;
                xEvent.xmotion.y_root = globalY;
                xEvent.xmotion.state = state;
                Tk_QueueWindowEvent(&xEvent, TCL_QUEUE_TAIL);
            }
        } else {
            Tk_UpdatePointer(target, globalX, globalY, state);
        }
    } else {
        /* Scroll wheel events. */
        XEvent xEvent = {0};
        xEvent.xbutton.x = win_x;
        xEvent.xbutton.y = win_y;
        xEvent.xbutton.x_root = globalX;
        xEvent.xbutton.y_root = globalY;
        xEvent.xany.send_event = 0;
        xEvent.xany.display = Tk_Display(target);
        xEvent.xany.window = Tk_WindowId(target);
        
        if (scrollX != 0.0 || scrollY != 0.0) {
            /* High-precision scrolling (touchpad). */
            xEvent.type = TouchpadScroll;
            xEvent.xbutton.state = state;
            /* Encode scroll deltas in keycode field.*/
            unsigned deltaX = (unsigned)(scrollX * 120); /* Convert to wheel units. */
            unsigned deltaY = (unsigned)(scrollY * 120);
            unsigned delta = (deltaX << 16) | (deltaY & 0xffff);
            xEvent.xkey.keycode = delta;
            Tk_QueueWindowEvent(&xEvent, TCL_QUEUE_TAIL);
        } else {
            /* Regular mouse wheel. */
            xEvent.type = MouseWheelEvent;
            xEvent.xbutton.state = state;
            if (scrollY > 0.0) {
                xEvent.xkey.keycode = 120;
            } else if (scrollY < 0.0) {
                xEvent.xkey.keycode = -120;
            }
            Tk_QueueWindowEvent(&xEvent, TCL_QUEUE_TAIL);
        }
    }

    /* Check capture state for button events. */
    capture = TkpGetCapture();
    if (capture && eventType == GLFW_MOUSE_BUTTON && action == GLFW_PRESS) {
        Tk_Window w;
        for (w = target; w != NULL; w = Tk_Parent(w)) {
            if (w == capture) {
                break;
            }
        }
        if (w != capture) {
            return 0;
        }
    }
    
    return 1;
}

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
    int getGlobal = (root_x_return && root_y_return);
    int getLocal = (win_x_return && win_y_return && w != None);
    
    /* Get the GLFW window for the given X window. */
    GLFWwindow* glfwWindow = TkWaylandGetGLFWWindow((Tk_Window)w);
    if (!glfwWindow) {
        return False;
    }
    
    if (getGlobal || getLocal) {
        double cursorX, cursorY;
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
    med.global.h = global_x;
    med.global.v = global_y;
    med.local.h = local_x;
    med.local.v = local_y;
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
    med.global.h = x;
    med.global.v = y;
    med.local = med.global;

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
	tkwin = Tk_TopCoordsToWindow(tkwin, medPtr->local.h, medPtr->local.v,
		&dummy, &dummy);
    }
    Tk_UpdatePointer(tkwin, medPtr->global.h, medPtr->global.v, medPtr->state);
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
    if (dispPtr->warpWindow) {
	int x, y;
	Tk_GetRootCoords(dispPtr->warpWindow, &x, &y);
	
	/* Warp cursor to new position. */
	GLFWwindow* glfwWindow = TkWaylandGetGLFWWindow(dispPtr->warpWindow);
	if (glfwWindow) {
	    int winX, winY;
	    glfwGetWindowPos(glfwWindow, &winX, &winY);
	    double targetX = x + dispPtr->warpX - winX;
	    double targetY = y + dispPtr->warpY - winY;
	    glfwSetCursorPos(glfwWindow, targetX, targetY);
	}
    } else {
	/* Global warp - not directly supported by GLFW */
	/* Would need platform-specific code for this */
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
    while (winPtr && !Tk_IsTopLevel(winPtr)) {
	winPtr = winPtr->parentPtr;
    }
    captureWinPtr = (Tk_Window)winPtr;
    
    /* Set GLFW cursor mode. */
    GLFWwindow* glfwWindow = TkWaylandGetGLFWWindow((Tk_Window)winPtr);
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
