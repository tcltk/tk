/*
 * tkWaylandMouseEvent.c --
 *
 *	This file implements functions that decode & handle mouse events on
 *	Wayland using GLFW.
 *
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2026 Kevin Walzer
 * Copyright © 2026 Marc Culler
 *
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkWaylandInt.h"
#include <GLFW/glfw3.h>

typedef struct {
    unsigned int state;
    long delta;
    Window window;
    int globalX, globalY;	/* Global screen coordinates */
    int localX, localY;		/* Local window coordinates */
    int button;			/* 1,2,3 */
    int isPress;
} MouseEventData;

static Tk_Window captureWinPtr = NULL;
int tkWaylandLastRootX = 200;
int tkWaylandLastRootY = 200;
int tkWaylandLastWinX = 0;
int tkWaylandLastWinY = 0;
TkWindow* tkWaylandLastPointerWinPtr = NULL;

void TkWaylandUpdatePointerState(int rootX, int rootY, int winX, int winY,
                                 unsigned int buttonState, TkWindow *winPtr) {
    tkWaylandLastRootX = rootX;
    tkWaylandLastRootY = rootY;
    tkWaylandLastWinX = winX;
    tkWaylandLastWinY = winY;
    if (winPtr) tkWaylandLastPointerWinPtr = winPtr;
}
	/* Current capture window; may be
					 * NULL. */

static void GenerateButtonEvent(MouseEventData *medPtr);
static void QueueButtonEvent(MouseEventData *medPtr);

/* Global state maintained by notify.c */
extern unsigned int glfwButtonState;
extern unsigned int glfwModifierState;

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
    /* Use global state updated in callbacks, not glfwGetCurrentContext()
     * which is NULL during XQueryPointer and during event generate.
     * This is why B1-Motion had state=0 and event-3.1 hung in tkTextSelectTo.
     */
    return glfwButtonState | glfwModifierState;
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
    TkWindow *winPtr = (TkWindow *)w;
    GLFWwindow* glfwWindow = NULL;
    double cursorX = 0, cursorY = 0;
    int haveGLFW = 0;

    if (winPtr) {
        glfwWindow = TkWaylandGetGLFWwindow(winPtr);
        if (glfwWindow) {
            glfwGetCursorPos(glfwWindow, &cursorX, &cursorY);
            haveGLFW = 1;
        }
    }

    int rootX, rootY;
    if (haveGLFW) {
        rootX = (int)cursorX;
        rootY = (int)cursorY;
        tkWaylandLastRootX = rootX;
        tkWaylandLastRootY = rootY;
    } else {
        rootX = tkWaylandLastRootX;
        rootY = tkWaylandLastRootY;
    }

    if (root_x_return) *root_x_return = rootX;
    if (root_y_return) *root_y_return = rootY;

    if (win_x_return) {
        if (winPtr) {
            int winRootX, winRootY;
            Tk_GetRootCoords((Tk_Window)winPtr, &winRootX, &winRootY);
            *win_x_return = rootX - winRootX;
        } else {
            *win_x_return = tkWaylandLastWinX;
        }
    }
    if (win_y_return) {
        if (winPtr) {
            int winRootX, winRootY;
            Tk_GetRootCoords((Tk_Window)winPtr, &winRootX, &winRootY);
            *win_y_return = rootY - winRootY;
        } else {
            *win_y_return = tkWaylandLastWinY;
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
 *	Events generated.
 *
 * Side effects:
 *	Additional events may be placed on the Tk event queue. Grab state may
 *	also change.
 *
 *----------------------------------------------------------------------
 */

static void
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

    GenerateButtonEvent(&med);
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

static void
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

    GenerateButtonEvent(&med);
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

static void
GenerateButtonEvent(
    MouseEventData *medPtr)
{
    Tk_Window tkwin;
    TkDisplay *dispPtr;

    dispPtr = TkGetDisplayList();
    tkwin = Tk_IdToWindow(dispPtr->display, medPtr->window);

    if (tkwin != NULL) {
	tkwin = Tk_CoordsToWindow(medPtr->localX, medPtr->localY, tkwin);
    }
    Tk_UpdatePointer(tkwin, medPtr->globalX, medPtr->globalY, medPtr->state);

    /* If this came from a real button press, also queue ButtonPress/Release */
    if (medPtr->button != 0) {
	QueueButtonEvent(medPtr);
    }
}

static void
QueueButtonEvent(MouseEventData *medPtr)
{
    TkWindow *winPtr = (TkWindow *)Tk_IdToWindow(TkGetDisplayList()->display, medPtr->window);
    if (!winPtr) return;

    TkDisplay *dispPtr = TkGetDisplayList();
    Tk_Window target = Tk_IdToWindow(dispPtr->display, medPtr->window);
    if (target) {
        Tk_Window child = Tk_CoordsToWindow(medPtr->localX, medPtr->localY, target);
        if (child) {
            target = child;
        }
        winPtr = (TkWindow *)target;
    }

    XEvent event;
    memset(&event, 0, sizeof(XEvent));
    event.type = medPtr->isPress ? ButtonPress : ButtonRelease;
    event.xbutton.serial = LastKnownRequestProcessed(winPtr->display)++;
    event.xbutton.send_event = False;
    event.xbutton.display = winPtr->display;
    event.xbutton.window = Tk_WindowId((Tk_Window)winPtr);
    event.xbutton.root = XRootWindow(winPtr->display, 0);
    event.xbutton.time = CurrentTime;
    int rootX, rootY;
    Tk_GetRootCoords((Tk_Window)winPtr, &rootX, &rootY);
    event.xbutton.x = medPtr->globalX - rootX;
    event.xbutton.y = medPtr->globalY - rootY;
    event.xbutton.x_root = medPtr->globalX;
    event.xbutton.y_root = medPtr->globalY;
    event.xbutton.state = medPtr->state;
    event.xbutton.button = medPtr->button;
    event.xbutton.same_screen = True;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

    if (medPtr->button == 1) {
        TkpSetCapture(medPtr->isPress ? winPtr : NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandHandleMouseButton --
 *
 *   GLFW mouse button callback.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Interactions with window elements.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandHandleMouseButton(
			   GLFWwindow *glfwWindow,
			   int button, /* GLFW button */
			   int action, /* GLFW_PRESS/RELEASE */
			   int mods)
{
    TkWindow *winPtr;
    double x, y;

    winPtr = TkWaylandGetTkWindow(glfwWindow);
    if (!winPtr) return;

    glfwGetCursorPos(glfwWindow, &x, &y);

    MouseEventData med;
    memset(&med, 0, sizeof(MouseEventData));
    med.globalX = (int)x;
    med.globalY = (int)y;
    med.localX = (int)x - Tk_X(winPtr);
    med.localY = (int)y - Tk_Y(winPtr);
    med.window = Tk_WindowId((Tk_Window)winPtr);

    if (button == GLFW_MOUSE_BUTTON_LEFT) med.button = 1;
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE) med.button = 2;
    else if (button == GLFW_MOUSE_BUTTON_RIGHT) med.button = 3;
    else med.button = button + 1;

    med.isPress = (action == GLFW_PRESS);

    /* Update global button state BEFORE queuing so B1-Motion sees Button1Mask */
    if (med.isPress) {
	if (med.button == 1) glfwButtonState |= Button1Mask;
	if (med.button == 2) glfwButtonState |= Button2Mask;
	if (med.button == 3) glfwButtonState |= Button3Mask;
    } else {
	if (med.button == 1) glfwButtonState &= ~Button1Mask;
	if (med.button == 2) glfwButtonState &= ~Button2Mask;
	if (med.button == 3) glfwButtonState &= ~Button3Mask;
    }
    med.state = TkWaylandButtonKeyState();

    QueueButtonEvent(&med);

    /* Keep Enter/Leave correct */
    TkDisplay *dispPtr = TkGetDisplayList();
    Tk_Window tkwin = Tk_IdToWindow(dispPtr->display, med.window);
    if (tkwin) tkwin = Tk_CoordsToWindow(med.localX, med.localY, tkwin);
    Tk_UpdatePointer(tkwin, med.globalX, med.globalY, med.state);
}


/*
 *----------------------------------------------------------------------
 *
 * TkWaylandHandleMouseMove --
 *
 *   GLFW cursor position callback.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Interactions with window elements.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandHandleMouseMove(
    GLFWwindow *glfwWindow,
    double x,
    double y)
{
    TkWindow *winPtr = TkWaylandGetTkWindow(glfwWindow);
    if (!winPtr) return;

    XEvent event;
    memset(&event, 0, sizeof(XEvent));
    event.type = MotionNotify;
    event.xmotion.serial = LastKnownRequestProcessed(winPtr->display)++;
    event.xmotion.send_event = False;
    event.xmotion.display = winPtr->display;
    event.xmotion.window = Tk_WindowId((Tk_Window)winPtr);
    event.xmotion.root = XRootWindow(winPtr->display, 0);
    event.xmotion.time = (Time)(glfwGetTime() * 1000.0);
    event.xmotion.x = (int)x - Tk_X(winPtr);
    event.xmotion.y = (int)y - Tk_Y(winPtr);
    event.xmotion.x_root = (int)x;
    event.xmotion.y_root = (int)y;
    event.xmotion.state = TkWaylandButtonKeyState();
    event.xmotion.is_hint = NotifyNormal;
    event.xmotion.same_screen = True;

    if (captureWinPtr) {
	TkWindow *cap = (TkWindow *)captureWinPtr;
	event.xmotion.window = Tk_WindowId(captureWinPtr);
	event.xmotion.x = (int)x - Tk_X(cap);
	event.xmotion.y = (int)y - Tk_Y(cap);
    }

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    Tk_UpdatePointer(captureWinPtr ? captureWinPtr : (Tk_Window)winPtr,
	(int)event.xmotion.x_root, (int)event.xmotion.y_root, event.xmotion.state);
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
    if (!dispPtr || !dispPtr->warpWindow) {
        return;
    }

    TkWindow *warpWinPtr = (TkWindow *)dispPtr->warpWindow;

    int rootX, rootY;
    Tk_GetRootCoords(dispPtr->warpWindow, &rootX, &rootY);
    int targetRootX = rootX + dispPtr->warpX;
    int targetRootY = rootY + dispPtr->warpY;

    tkWaylandLastRootX = targetRootX;
    tkWaylandLastRootY = targetRootY;
    tkWaylandLastWinX = dispPtr->warpX;
    tkWaylandLastWinY = dispPtr->warpY;
    tkWaylandLastPointerWinPtr = warpWinPtr;

    XEvent ev;
    memset(&ev, 0, sizeof(XEvent));
    ev.type = MotionNotify;
    ev.xmotion.serial = LastKnownRequestProcessed(warpWinPtr->display)++;
    ev.xmotion.send_event = False;
    ev.xmotion.display = warpWinPtr->display;
    ev.xmotion.window = Tk_WindowId(dispPtr->warpWindow);
    ev.xmotion.root = XRootWindow(warpWinPtr->display, 0);
    ev.xmotion.time = CurrentTime;
    ev.xmotion.x = dispPtr->warpX;
    ev.xmotion.y = dispPtr->warpY;
    ev.xmotion.x_root = targetRootX;
    ev.xmotion.y_root = targetRootY;
    ev.xmotion.state = TkWaylandButtonKeyState();
    ev.xmotion.is_hint = NotifyNormal;
    ev.xmotion.same_screen = True;

    Tk_QueueWindowEvent(&ev, TCL_QUEUE_TAIL);

    Tk_Window tkwin = Tk_IdToWindow(dispPtr->display, Tk_WindowId(dispPtr->warpWindow));
    if (tkwin) {
        Tk_Window child = Tk_CoordsToWindow(dispPtr->warpX, dispPtr->warpY, tkwin);
        if (child) {
            tkwin = child;
        }
    }
    Tk_UpdatePointer(tkwin ? tkwin : dispPtr->warpWindow, targetRootX, targetRootY, ev.xmotion.state);
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
TkpSetCapture(TkWindow *winPtr)
{
    while (winPtr && !Tk_IsTopLevel(winPtr)) {
        winPtr = winPtr->parentPtr;
    }
    captureWinPtr = (Tk_Window)winPtr;
    /*
     * Do not change GLFW cursor mode here. Tk grab semantics keep the
     * cursor visible and redirect events via captureWinPtr, unlike
     * GLFW_CURSOR_DISABLED which hides the cursor entirely.
     */
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
