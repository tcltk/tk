/*
 * tkWaylandEvent.c --
 *
 *	This file implements event management functionality for the Wayland
 *      backend of Tk (GLFW adaptation).
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkWaylandInt.h"
#include <GLFW/glfw3.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

/*
 * Global state for mouse buttons and modifiers.
 */
unsigned int glfwButtonState   = 0;
unsigned int glfwModifierState = 0;

/*
 * Simple single-codepoint buffer for character input.
 */
static unsigned int pendingCodepoint = 0;

/* Track last window and position for enter/leave events. */
static GLFWwindow *lastWindow = NULL;
static double      lastX = -1, lastY = -1;

/*
 *---------------------------------------------------------------------------
 *
 * TkGlfwSetupCallbacks --
 *
 *	Set up standard GLFW callbacks for a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Registers all GLFW event callbacks for the given window.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkGlfwSetupCallbacks(
    GLFWwindow *glfwWindow,
    TCL_UNUSED(TkWindow *))
{
    glfwSetWindowCloseCallback     (glfwWindow, TkGlfwWindowCloseCallback);
    glfwSetWindowSizeCallback      (glfwWindow, TkGlfwWindowSizeCallback);
    glfwSetFramebufferSizeCallback (glfwWindow, TkGlfwFramebufferSizeCallback);
    glfwSetWindowPosCallback       (glfwWindow, TkGlfwWindowPosCallback);
    glfwSetWindowFocusCallback     (glfwWindow, TkGlfwWindowFocusCallback);
    glfwSetWindowIconifyCallback   (glfwWindow, TkGlfwWindowIconifyCallback);
    glfwSetWindowMaximizeCallback  (glfwWindow, TkGlfwWindowMaximizeCallback);
    glfwSetCursorPosCallback       (glfwWindow, TkGlfwCursorPosCallback);
    glfwSetMouseButtonCallback     (glfwWindow, TkGlfwMouseButtonCallback);
    glfwSetScrollCallback          (glfwWindow, TkGlfwScrollCallback);
    glfwSetWindowRefreshCallback   (glfwWindow, TkGlfwWindowRefreshCallback);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkGlfwWindowCloseCallback --
 *
 *	Called when the user attempts to close the window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys the corresponding Tk window.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkGlfwWindowCloseCallback(GLFWwindow *window)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    if (winPtr) {
        Tk_DestroyWindow((Tk_Window)winPtr);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkGlfwWindowSizeCallback --
 *
 *	Called when window size changes. Recreates the cg surface to
 *	match the new dimensions, updates Tk geometry, and queues a
 *	redraw.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Resizes the cg surface, updates Tk window dimensions, queues expose.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkGlfwWindowSizeCallback(GLFWwindow *window, int width, int height)
{
    WindowMapping *m = FindMappingByGLFW(window);
    if (!m) return;

    /* Recreate the cg surface at the new size. */
    SyncWindowSize(m);

    if (m->tkWindow) {
        m->tkWindow->changes.width  = width;
        m->tkWindow->changes.height = height;
        TkWaylandQueueExposeEvent(m->tkWindow, 0, 0, width, height);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkGlfwFramebufferSizeCallback --
 *
 *	Called when framebuffer size changes. The GL viewport is
 *	managed by TkWaylandDisplayProc; just queue a redraw here.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues expose event for the window.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkGlfwFramebufferSizeCallback(
    GLFWwindow *window,
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    TkWindow      *winPtr = TkGlfwGetTkWindow(window);
    WindowMapping *mapping;
    int            w, h;

    if (!winPtr) return;

    mapping = FindMappingByTk(winPtr);
    if (!mapping) return;

    w = mapping->width  > 0 ? mapping->width  : winPtr->changes.width;
    h = mapping->height > 0 ? mapping->height : winPtr->changes.height;

    TkWaylandQueueExposeEvent(winPtr, 0, 0, w, h);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkGlfwWindowPosCallback --
 *
 *	Called when window position changes. Updates Tk geometry and queues
 *	a ConfigureNotify event.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates window position in Tk data structures and queues event.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkGlfwWindowPosCallback(GLFWwindow *window, int xpos, int ypos)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent    event;

    if (!winPtr) return;

    winPtr->changes.x = xpos;
    winPtr->changes.y = ypos;

    memset(&event, 0, sizeof(XEvent));
    event.type                          = ConfigureNotify;
    event.xconfigure.serial             = LastKnownRequestProcessed(winPtr->display);
    event.xconfigure.send_event         = False;
    event.xconfigure.display            = winPtr->display;
    event.xconfigure.event              = Tk_WindowId((Tk_Window)winPtr);
    event.xconfigure.window             = Tk_WindowId((Tk_Window)winPtr);
    event.xconfigure.x                  = xpos;
    event.xconfigure.y                  = ypos;
    event.xconfigure.width              = winPtr->changes.width;
    event.xconfigure.height             = winPtr->changes.height;
    event.xconfigure.border_width       = winPtr->changes.border_width;
    event.xconfigure.above              = None;
    event.xconfigure.override_redirect  = winPtr->atts.override_redirect;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkGlfwWindowFocusCallback --
 *
 *	Called when window gains or loses focus.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues FocusIn/FocusOut events and generates activate events.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkGlfwWindowFocusCallback(GLFWwindow *window, int focused)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent    event;

    if (!winPtr) return;

    memset(&event, 0, sizeof(XEvent));
    event.type              = focused ? FocusIn : FocusOut;
    event.xfocus.serial     = LastKnownRequestProcessed(winPtr->display);
    event.xfocus.send_event = False;
    event.xfocus.display    = winPtr->display;
    event.xfocus.window     = Tk_WindowId((Tk_Window)winPtr);
    event.xfocus.mode       = NotifyNormal;
    event.xfocus.detail     = NotifyAncestor;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    TkGenerateActivateEvents(winPtr, focused);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkGlfwWindowIconifyCallback --
 *
 *	Called when window is iconified or restored.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues Map/Unmap events and updates mapped flag.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkGlfwWindowIconifyCallback(GLFWwindow *window, int iconified)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent    event;

    if (!winPtr) return;

    memset(&event, 0, sizeof(XEvent));
    if (iconified) {
        event.type               = UnmapNotify;
        event.xunmap.serial      = LastKnownRequestProcessed(winPtr->display);
        event.xunmap.send_event  = False;
        event.xunmap.display     = winPtr->display;
        event.xunmap.event       = Tk_WindowId((Tk_Window)winPtr);
        event.xunmap.window      = Tk_WindowId((Tk_Window)winPtr);
        event.xunmap.from_configure = False;
        winPtr->flags &= ~TK_MAPPED;
    } else {
        event.type                   = MapNotify;
        event.xmap.serial            = LastKnownRequestProcessed(winPtr->display);
        event.xmap.send_event        = False;
        event.xmap.display           = winPtr->display;
        event.xmap.event             = Tk_WindowId((Tk_Window)winPtr);
        event.xmap.window            = Tk_WindowId((Tk_Window)winPtr);
        event.xmap.override_redirect = winPtr->atts.override_redirect;
        winPtr->flags |= TK_MAPPED;
    }
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkGlfwWindowMaximizeCallback --
 *
 *	Called when window is maximized or unmaximized.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates zoomed state in window manager info.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkGlfwWindowMaximizeCallback(GLFWwindow *window, int maximized)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    if (!winPtr) return;
    if (winPtr->wmInfoPtr) {
        WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
        wmPtr->attributes.zoomed = maximized;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkGlfwCursorPosCallback --
 *
 *	Called when mouse cursor moves. Generates enter/leave and motion events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues EnterNotify, LeaveNotify, and MotionNotify events.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkGlfwCursorPosCallback(GLFWwindow *window, double xpos, double ypos)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent    event;

    if (!winPtr) {
        if (lastWindow) {
            TkWindow *lastWinPtr = TkGlfwGetTkWindow(lastWindow);
            if (lastWinPtr) {
                memset(&event, 0, sizeof(XEvent));
                event.type                  = LeaveNotify;
                event.xcrossing.serial      = LastKnownRequestProcessed(lastWinPtr->display);
                event.xcrossing.display     = lastWinPtr->display;
                event.xcrossing.window      = Tk_WindowId((Tk_Window)lastWinPtr);
                event.xcrossing.root        = RootWindow(lastWinPtr->display, lastWinPtr->screenNum);
                event.xcrossing.time        = CurrentTime;
                event.xcrossing.x           = (int)lastX;
                event.xcrossing.y           = (int)lastY;
                event.xcrossing.x_root      = lastWinPtr->changes.x + (int)lastX;
                event.xcrossing.y_root      = lastWinPtr->changes.y + (int)lastY;
                event.xcrossing.mode        = NotifyNormal;
                event.xcrossing.detail      = NotifyAncestor;
                event.xcrossing.same_screen = True;
                event.xcrossing.state       = glfwButtonState | glfwModifierState;
                Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
            }
            lastWindow = NULL;
        }
        return;
    }

    if (lastWindow != window) {
        if (lastWindow) {
            TkWindow *lastWinPtr = TkGlfwGetTkWindow(lastWindow);
            if (lastWinPtr) {
                memset(&event, 0, sizeof(XEvent));
                event.type                  = LeaveNotify;
                event.xcrossing.serial      = LastKnownRequestProcessed(lastWinPtr->display);
                event.xcrossing.display     = lastWinPtr->display;
                event.xcrossing.window      = Tk_WindowId((Tk_Window)lastWinPtr);
                event.xcrossing.root        = RootWindow(lastWinPtr->display, lastWinPtr->screenNum);
                event.xcrossing.time        = CurrentTime;
                event.xcrossing.x           = (int)lastX;
                event.xcrossing.y           = (int)lastY;
                event.xcrossing.x_root      = lastWinPtr->changes.x + (int)lastX;
                event.xcrossing.y_root      = lastWinPtr->changes.y + (int)lastY;
                event.xcrossing.mode        = NotifyNormal;
                event.xcrossing.detail      = NotifyAncestor;
                event.xcrossing.same_screen = True;
                event.xcrossing.state       = glfwButtonState | glfwModifierState;
                Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
            }
        }
        memset(&event, 0, sizeof(XEvent));
        event.type                  = EnterNotify;
        event.xcrossing.serial      = LastKnownRequestProcessed(winPtr->display);
        event.xcrossing.display     = winPtr->display;
        event.xcrossing.window      = Tk_WindowId((Tk_Window)winPtr);
        event.xcrossing.root        = RootWindow(winPtr->display, winPtr->screenNum);
        event.xcrossing.time        = CurrentTime;
        event.xcrossing.x           = (int)xpos;
        event.xcrossing.y           = (int)ypos;
        event.xcrossing.x_root      = winPtr->changes.x + (int)xpos;
        event.xcrossing.y_root      = winPtr->changes.y + (int)ypos;
        event.xcrossing.mode        = NotifyNormal;
        event.xcrossing.detail      = NotifyAncestor;
        event.xcrossing.same_screen = True;
        event.xcrossing.state       = glfwButtonState | glfwModifierState;
        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
        lastWindow = window;
    }

    memset(&event, 0, sizeof(XEvent));
    event.type               = MotionNotify;
    event.xmotion.serial     = LastKnownRequestProcessed(winPtr->display);
    event.xmotion.display    = winPtr->display;
    event.xmotion.window     = Tk_WindowId((Tk_Window)winPtr);
    event.xmotion.root       = RootWindow(winPtr->display, winPtr->screenNum);
    event.xmotion.time       = CurrentTime;
    event.xmotion.x          = (int)xpos;
    event.xmotion.y          = (int)ypos;
    event.xmotion.x_root     = winPtr->changes.x + (int)xpos;
    event.xmotion.y_root     = winPtr->changes.y + (int)ypos;
    event.xmotion.state      = glfwButtonState | glfwModifierState;
    event.xmotion.is_hint    = NotifyNormal;
    event.xmotion.same_screen = True;
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

    lastX = xpos;
    lastY = ypos;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkGlfwMouseButtonCallback --
 *
 *	Called when mouse button is pressed or released.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues ButtonPress or ButtonRelease events, updates button state.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkGlfwMouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
{
    TkWindow    *winPtr = TkGlfwGetTkWindow(window);
    XEvent       event;
    double       xpos, ypos;
    unsigned int buttonMask = 0;
    unsigned int xbutton    = Button1;

    if (!winPtr) return;

    glfwGetCursorPos(window, &xpos, &ypos);

    glfwModifierState = 0;
    if (mods & GLFW_MOD_SHIFT)   glfwModifierState |= ShiftMask;
    if (mods & GLFW_MOD_CONTROL) glfwModifierState |= ControlMask;
    if (mods & GLFW_MOD_ALT)     glfwModifierState |= Mod1Mask;
    if (mods & GLFW_MOD_SUPER)   glfwModifierState |= Mod4Mask;

    switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT:   xbutton = Button1; buttonMask = Button1Mask; break;
    case GLFW_MOUSE_BUTTON_MIDDLE: xbutton = Button2; buttonMask = Button2Mask; break;
    case GLFW_MOUSE_BUTTON_RIGHT:  xbutton = Button3; buttonMask = Button3Mask; break;
    default:                       xbutton = button + 1; break;
    }

    memset(&event, 0, sizeof(XEvent));
    if (action == GLFW_PRESS) {
        glfwButtonState |= buttonMask;
        event.type = ButtonPress;
    } else {
        glfwButtonState &= ~buttonMask;
        event.type = ButtonRelease;
    }

    event.xbutton.serial      = LastKnownRequestProcessed(winPtr->display);
    event.xbutton.display     = winPtr->display;
    event.xbutton.window      = Tk_WindowId((Tk_Window)winPtr);
    event.xbutton.root        = RootWindow(winPtr->display, winPtr->screenNum);
    event.xbutton.time        = CurrentTime;
    event.xbutton.x           = (int)xpos;
    event.xbutton.y           = (int)ypos;
    event.xbutton.x_root      = winPtr->changes.x + (int)xpos;
    event.xbutton.y_root      = winPtr->changes.y + (int)ypos;
    event.xbutton.state       = glfwButtonState | glfwModifierState;
    event.xbutton.button      = xbutton;
    event.xbutton.same_screen = True;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkGlfwScrollCallback --
 *
 *	Called when scroll wheel is used.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues ButtonPress/ButtonRelease events for scroll buttons (4-7).
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkGlfwScrollCallback(GLFWwindow *window, double xoffset, double yoffset)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent    event;
    double    xpos, ypos;
    int       button;

    if (!winPtr) return;

    glfwGetCursorPos(window, &xpos, &ypos);

    if      (yoffset > 0) button = Button4;
    else if (yoffset < 0) button = Button5;
    else if (xoffset > 0) button = 6;
    else                  button = 7;

    memset(&event, 0, sizeof(XEvent));
    event.type               = ButtonPress;
    event.xbutton.serial     = LastKnownRequestProcessed(winPtr->display);
    event.xbutton.display    = winPtr->display;
    event.xbutton.window     = Tk_WindowId((Tk_Window)winPtr);
    event.xbutton.root       = RootWindow(winPtr->display, winPtr->screenNum);
    event.xbutton.time       = CurrentTime;
    event.xbutton.x          = (int)xpos;
    event.xbutton.y          = (int)ypos;
    event.xbutton.x_root     = winPtr->changes.x + (int)xpos;
    event.xbutton.y_root     = winPtr->changes.y + (int)ypos;
    event.xbutton.button     = button;
    event.xbutton.same_screen = True;
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

    event.type = ButtonRelease;
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkGlfwKeyCallback --
 *
 *	Called when a key is pressed or released.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues KeyPress or KeyRelease events.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkGlfwKeyCallback(GLFWwindow *window, int key,
                  TCL_UNUSED(int), int action, int mods)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    TkWindow *focusWin;
    XEvent    event;
    double    xpos, ypos;

    if (!winPtr || action == GLFW_REPEAT) return;

    TkWaylandUpdateKeyboardModifiers(mods);
    glfwGetCursorPos(window, &xpos, &ypos);

    focusWin = winPtr;
    if (winPtr->dispPtr->focusPtr != NULL)
        focusWin = winPtr->dispPtr->focusPtr;

    memset(&event, 0, sizeof(XEvent));
    event.type             = (action == GLFW_PRESS) ? KeyPress : KeyRelease;
    event.xkey.serial      = LastKnownRequestProcessed(winPtr->display);
    event.xkey.display     = winPtr->display;
    event.xkey.window      = Tk_WindowId((Tk_Window)focusWin);
    event.xkey.root        = RootWindow(winPtr->display, winPtr->screenNum);
    event.xkey.time        = CurrentTime;
    event.xkey.x           = (int)xpos;
    event.xkey.y           = (int)ypos;
    event.xkey.x_root      = winPtr->changes.x + (int)xpos;
    event.xkey.y_root      = winPtr->changes.y + (int)ypos;
    event.xkey.state       = 0;
    if (mods & GLFW_MOD_SHIFT)      event.xkey.state |= ShiftMask;
    if (mods & GLFW_MOD_CONTROL)    event.xkey.state |= ControlMask;
    if (mods & GLFW_MOD_ALT)        event.xkey.state |= Mod1Mask;
    if (mods & GLFW_MOD_SUPER)      event.xkey.state |= Mod4Mask;
    if (mods & GLFW_MOD_CAPS_LOCK)  event.xkey.state |= LockMask;
    if (mods & GLFW_MOD_NUM_LOCK)   event.xkey.state |= Mod2Mask;
    event.xkey.keycode     = key;
    event.xkey.same_screen = True;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkGlfwCharCallback --
 *
 *	Called when a Unicode character is input.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores the character for later retrieval by Tk.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkGlfwCharCallback(TCL_UNUSED(GLFWwindow *), unsigned int codepoint)
{
    TkWaylandStoreCharacterInput(codepoint);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandUpdateKeyboardModifiers --
 *
 *	Updates global modifier state from GLFW modifier flags.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets glfwModifierState.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkWaylandUpdateKeyboardModifiers(int glfw_mods)
{
    glfwModifierState = 0;
    if (glfw_mods & GLFW_MOD_SHIFT)     glfwModifierState |= ShiftMask;
    if (glfw_mods & GLFW_MOD_CONTROL)   glfwModifierState |= ControlMask;
    if (glfw_mods & GLFW_MOD_ALT)       glfwModifierState |= Mod1Mask;
    if (glfw_mods & GLFW_MOD_SUPER)     glfwModifierState |= Mod4Mask;
    if (glfw_mods & GLFW_MOD_CAPS_LOCK) glfwModifierState |= LockMask;
    if (glfw_mods & GLFW_MOD_NUM_LOCK)  glfwModifierState |= Mod2Mask;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandStoreCharacterInput --
 *
 *	Stores a Unicode character for later processing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates pendingCodepoint.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkWaylandStoreCharacterInput(unsigned int codepoint)
{
    pendingCodepoint = codepoint;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandGetPendingCharacter --
 *
 *	Retrieves and clears the pending Unicode character.
 *
 * Results:
 *	Returns the stored codepoint, or 0 if none.
 *
 * Side effects:
 *	Clears the pending character.
 *
 *---------------------------------------------------------------------------
 */
unsigned int
TkWaylandGetPendingCharacter(void)
{
    unsigned int cp = pendingCodepoint;
    pendingCodepoint = 0;
    return cp;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkGlfwWindowRefreshCallback --
 *
 *	Called when the window needs to be refreshed (e.g., after exposure).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues expose event for the window.
 *
 *---------------------------------------------------------------------------
 */
MODULE_SCOPE void
TkGlfwWindowRefreshCallback(GLFWwindow *window)
{
    TkWindow      *winPtr = TkGlfwGetTkWindow(window);
    WindowMapping *mapping;
    int            w, h;

    if (!winPtr) return;

    mapping = FindMappingByTk(winPtr);
    if (!mapping) return;

    w = mapping->width  > 0 ? mapping->width  : winPtr->changes.width;
    h = mapping->height > 0 ? mapping->height : winPtr->changes.height;

    TkWaylandQueueExposeEvent(winPtr, 0, 0, w, h);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */