/*
 * tkWaylandEvent.c --
 *
 *	This file implements an event management functionality for the Wayland 
 *  backend of Tk.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 *
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

unsigned int glfwButtonState = 0;
unsigned int glfwModifierState = 0;


/*
 *----------------------------------------------------------------------
 *
 * TkGlfwSetupCallbacks --
 *
 *      Set up standard GLFW callbacks for a window.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Registers all standard callbacks.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwSetupCallbacks(
    GLFWwindow *glfwWindow,
    TkWindow *tkWin)
{
    glfwSetWindowCloseCallback(glfwWindow, TkGlfwWindowCloseCallback);
    glfwSetWindowSizeCallback(glfwWindow, TkGlfwWindowSizeCallback);
    glfwSetFramebufferSizeCallback(glfwWindow, TkGlfwFramebufferSizeCallback);
    glfwSetWindowPosCallback(glfwWindow, TkGlfwWindowPosCallback);
    glfwSetWindowFocusCallback(glfwWindow, TkGlfwWindowFocusCallback);
    glfwSetWindowIconifyCallback(glfwWindow, TkGlfwWindowIconifyCallback);
    glfwSetWindowMaximizeCallback(glfwWindow, TkGlfwWindowMaximizeCallback);
    glfwSetCursorPosCallback(glfwWindow, TkGlfwCursorPosCallback);
    glfwSetMouseButtonCallback(glfwWindow, TkGlfwMouseButtonCallback);
    glfwSetScrollCallback(glfwWindow, TkGlfwScrollCallback);
    glfwSetKeyCallback(glfwWindow, TkGlfwKeyCallback);
    glfwSetCharCallback(glfwWindow, TkGlfwCharCallback);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwWindowCloseCallback --
 *
 *      Called when user requests window close.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates WM_DELETE_WINDOW protocol event or destroys window.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwWindowCloseCallback(GLFWwindow *window)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    
    if (winPtr) {
        /* Generate destroy event - Tk will handle WM_DELETE_WINDOW protocol. */
        Tk_DestroyWindow((Tk_Window)winPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwWindowSizeCallback --
 *
 *      Called when window size changes.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates window geometry, generates ConfigureNotify event.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwWindowSizeCallback(
    GLFWwindow *window,
    int width,
    int height)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent event;
    
    if (!winPtr) {
        return;
    }

    /* Update mapping. */
    TkGlfwUpdateWindowSize(window, width, height);

    /* Update Tk geometry. */
    winPtr->changes.width = width;
    winPtr->changes.height = height;

    /* Generate ConfigureNotify event. */
    memset(&event, 0, sizeof(XEvent));
    event.type = ConfigureNotify;
    event.xconfigure.serial = LastKnownRequestProcessed(winPtr->display);
    event.xconfigure.send_event = False;
    event.xconfigure.display = winPtr->display;
    event.xconfigure.event = Tk_WindowId((Tk_Window)winPtr);
    event.xconfigure.window = Tk_WindowId((Tk_Window)winPtr);
    event.xconfigure.x = winPtr->changes.x;
    event.xconfigure.y = winPtr->changes.y;
    event.xconfigure.width = width;
    event.xconfigure.height = height;
    event.xconfigure.border_width = winPtr->changes.border_width;
    event.xconfigure.above = None;
    event.xconfigure.override_redirect = winPtr->atts.override_redirect;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwFramebufferSizeCallback --
 *
 *      Called when framebuffer size changes.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates OpenGL viewport.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwFramebufferSizeCallback(
    GLFWwindow *window,
    int width,
    int height)
{
    glViewport(0, 0, width, height);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwWindowPosCallback --
 *
 *      Called when window position changes.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates window geometry, generates ConfigureNotify event.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwWindowPosCallback(
    GLFWwindow *window,
    int xpos,
    int ypos)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent event;
    
    if (!winPtr) {
        return;
    }

    /* Update Tk geometry. */
    winPtr->changes.x = xpos;
    winPtr->changes.y = ypos;

    /* Generate ConfigureNotify event. */
    memset(&event, 0, sizeof(XEvent));
    event.type = ConfigureNotify;
    event.xconfigure.serial = LastKnownRequestProcessed(winPtr->display);
    event.xconfigure.send_event = False;
    event.xconfigure.display = winPtr->display;
    event.xconfigure.event = Tk_WindowId((Tk_Window)winPtr);
    event.xconfigure.window = Tk_WindowId((Tk_Window)winPtr);
    event.xconfigure.x = xpos;
    event.xconfigure.y = ypos;
    event.xconfigure.width = winPtr->changes.width;
    event.xconfigure.height = winPtr->changes.height;
    event.xconfigure.border_width = winPtr->changes.border_width;
    event.xconfigure.above = None;
    event.xconfigure.override_redirect = winPtr->atts.override_redirect;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwWindowFocusCallback --
 *
 *      Called when window gains or loses focus.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates FocusIn/FocusOut events.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwWindowFocusCallback(
    GLFWwindow *window,
    int focused)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent event;
    
    if (!winPtr) {
        return;
    }

    /* Generate focus event. */
    memset(&event, 0, sizeof(XEvent));
    event.type = focused ? FocusIn : FocusOut;
    event.xfocus.serial = LastKnownRequestProcessed(winPtr->display);
    event.xfocus.send_event = False;
    event.xfocus.display = winPtr->display;
    event.xfocus.window = Tk_WindowId((Tk_Window)winPtr);
    event.xfocus.mode = NotifyNormal;
    event.xfocus.detail = NotifyAncestor;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

    /* Also generate activate/deactivate events. */
    TkGenerateActivateEvents(winPtr, focused);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwWindowIconifyCallback --
 *
 *      Called when window is iconified or restored.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates Map/Unmap events.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwWindowIconifyCallback(
    GLFWwindow *window,
    int iconified)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent event;
    
    if (!winPtr) {
        return;
    }

    if (iconified) {
        /* Generate UnmapNotify. */
        memset(&event, 0, sizeof(XEvent));
        event.type = UnmapNotify;
        event.xunmap.serial = LastKnownRequestProcessed(winPtr->display);
        event.xunmap.send_event = False;
        event.xunmap.display = winPtr->display;
        event.xunmap.event = Tk_WindowId((Tk_Window)winPtr);
        event.xunmap.window = Tk_WindowId((Tk_Window)winPtr);
        event.xunmap.from_configure = False;

        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
        winPtr->flags &= ~TK_MAPPED;
    } else {
        /* Generate MapNotify. */
        memset(&event, 0, sizeof(XEvent));
        event.type = MapNotify;
        event.xmap.serial = LastKnownRequestProcessed(winPtr->display);
        event.xmap.send_event = False;
        event.xmap.display = winPtr->display;
        event.xmap.event = Tk_WindowId((Tk_Window)winPtr);
        event.xmap.window = Tk_WindowId((Tk_Window)winPtr);
        event.xmap.override_redirect = winPtr->atts.override_redirect;

        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
        winPtr->flags |= TK_MAPPED;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwWindowMaximizeCallback --
 *
 *      Called when window is maximized or restored.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates window state.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwWindowMaximizeCallback(
    GLFWwindow *window,
    int maximized)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    
    if (!winPtr || !winPtr->wmInfoPtr) {
        return;
    }

    /* Update WM state - implementation depends on WM integration */
    /* This is handled in tkWaylandWm.c */
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwCursorPosCallback --
 *
 *      Called when cursor position changes.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates MotionNotify, EnterNotify, and LeaveNotify events.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwCursorPosCallback(
    GLFWwindow *window,
    double xpos,
    double ypos)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent event;
    static GLFWwindow *lastWindow = NULL;
    static double lastX = -1, lastY = -1;

    if (!winPtr) {
        /* Mouse left all Tk windows. */
        if (lastWindow) {
            TkWindow *lastWinPtr = TkGlfwGetTkWindow(lastWindow);
            if (lastWinPtr) {
                /* Send LeaveNotify for previous window. */
                memset(&event, 0, sizeof(XEvent));
                event.type = LeaveNotify;
                event.xcrossing.serial = LastKnownRequestProcessed(lastWinPtr->display);
                event.xcrossing.send_event = False;
                event.xcrossing.display = lastWinPtr->display;
                event.xcrossing.window = Tk_WindowId((Tk_Window)lastWinPtr);
                event.xcrossing.root = RootWindow(lastWinPtr->display, lastWinPtr->screenNum);
                event.xcrossing.subwindow = None;
                event.xcrossing.time = CurrentTime;
                event.xcrossing.x = (int)lastX;
                event.xcrossing.y = (int)lastY;
                event.xcrossing.x_root = lastWinPtr->changes.x + (int)lastX;
                event.xcrossing.y_root = lastWinPtr->changes.y + (int)lastY;
                event.xcrossing.mode = NotifyNormal;
                event.xcrossing.detail = NotifyAncestor;
                event.xcrossing.same_screen = True;
                event.xcrossing.focus = False;
                event.xcrossing.state = glfwButtonState | glfwModifierState;
                Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
            }
            lastWindow = NULL;
        }
        return;
    }

    /* Check if mouse entered/exited window. */
    if (lastWindow != window) {
        /* Send LeaveNotify for previous window if any. */
        if (lastWindow) {
            TkWindow *lastWinPtr = TkGlfwGetTkWindow(lastWindow);
            if (lastWinPtr) {
                memset(&event, 0, sizeof(XEvent));
                event.type = LeaveNotify;
                event.xcrossing.serial = LastKnownRequestProcessed(lastWinPtr->display);
                event.xcrossing.send_event = False;
                event.xcrossing.display = lastWinPtr->display;
                event.xcrossing.window = Tk_WindowId((Tk_Window)lastWinPtr);
                event.xcrossing.root = RootWindow(lastWinPtr->display, lastWinPtr->screenNum);
                event.xcrossing.subwindow = None;
                event.xcrossing.time = CurrentTime;
                event.xcrossing.x = (int)lastX;
                event.xcrossing.y = (int)lastY;
                event.xcrossing.x_root = lastWinPtr->changes.x + (int)lastX;
                event.xcrossing.y_root = lastWinPtr->changes.y + (int)lastY;
                event.xcrossing.mode = NotifyNormal;
                event.xcrossing.detail = NotifyAncestor;
                event.xcrossing.same_screen = True;
                event.xcrossing.focus = False;
                event.xcrossing.state = glfwButtonState | glfwModifierState;
                Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
            }
        }
        
        /* Send EnterNotify for current window. */
        memset(&event, 0, sizeof(XEvent));
        event.type = EnterNotify;
        event.xcrossing.serial = LastKnownRequestProcessed(winPtr->display);
        event.xcrossing.send_event = False;
        event.xcrossing.display = winPtr->display;
        event.xcrossing.window = Tk_WindowId((Tk_Window)winPtr);
        event.xcrossing.root = RootWindow(winPtr->display, winPtr->screenNum);
        event.xcrossing.subwindow = None;
        event.xcrossing.time = CurrentTime;
        event.xcrossing.x = (int)xpos;
        event.xcrossing.y = (int)ypos;
        event.xcrossing.x_root = winPtr->changes.x + (int)xpos;
        event.xcrossing.y_root = winPtr->changes.y + (int)ypos;
        event.xcrossing.mode = NotifyNormal;
        event.xcrossing.detail = NotifyAncestor;
        event.xcrossing.same_screen = True;
        event.xcrossing.focus = False;
        event.xcrossing.state = glfwButtonState | glfwModifierState;
        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
        
        lastWindow = window;
    }
    
    /* Generate MotionNotify event. */
    memset(&event, 0, sizeof(XEvent));
    event.type = MotionNotify;
    event.xmotion.serial = LastKnownRequestProcessed(winPtr->display);
    event.xmotion.send_event = False;
    event.xmotion.display = winPtr->display;
    event.xmotion.window = Tk_WindowId((Tk_Window)winPtr);
    event.xmotion.root = RootWindow(winPtr->display, winPtr->screenNum);
    event.xmotion.subwindow = None;
    event.xmotion.time = CurrentTime;
    event.xmotion.x = (int)xpos;
    event.xmotion.y = (int)ypos;
    event.xmotion.x_root = winPtr->changes.x + (int)xpos;
    event.xmotion.y_root = winPtr->changes.y + (int)ypos;
    /* Critical for drag operations. */
    event.xmotion.state = glfwButtonState | glfwModifierState;
    event.xmotion.is_hint = NotifyNormal;
    event.xmotion.same_screen = True;
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    
    /* Update last position. */
    lastX = xpos;
    lastY = ypos;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwMouseButtonCallback --
 *
 *      Called when mouse button is pressed or released.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates ButtonPress/ButtonRelease event.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwMouseButtonCallback(
    GLFWwindow *window,
    int button,
    int action,
    int mods)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent event;
    double xpos, ypos;
    unsigned int buttonMask = 0;
    unsigned int xbutton = Button1;

    if (!winPtr) {
        return;
    }

    /* Get cursor position */
    glfwGetCursorPos(window, &xpos, &ypos);

    /* Update modifier state */
    glfwModifierState = 0;

    if (mods & GLFW_MOD_SHIFT)
        glfwModifierState |= ShiftMask;

    if (mods & GLFW_MOD_CONTROL)
        glfwModifierState |= ControlMask;

    if (mods & GLFW_MOD_ALT)
        glfwModifierState |= Mod1Mask;

    if (mods & GLFW_MOD_SUPER)
        glfwModifierState |= Mod4Mask;

    /* Map GLFW button → X11 button and mask */
    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT:
            xbutton = Button1;
            buttonMask = Button1Mask;
            break;

        case GLFW_MOUSE_BUTTON_MIDDLE:
            xbutton = Button2;
            buttonMask = Button2Mask;
            break;

        case GLFW_MOUSE_BUTTON_RIGHT:
            xbutton = Button3;
            buttonMask = Button3Mask;
            break;

        default:
            /* Buttons 4+ are typically scroll wheel, but map safely */
            xbutton = button + 1;
            buttonMask = 0;
            break;
    }

    /* Update button state */
    if (action == GLFW_PRESS) {
        glfwButtonState |= buttonMask;
        event.type = ButtonPress;
    } else {
        glfwButtonState &= ~buttonMask;
        event.type = ButtonRelease;
    }

    memset(&event, 0, sizeof(XEvent));

    event.type = (action == GLFW_PRESS) ? ButtonPress : ButtonRelease;

    event.xbutton.serial = LastKnownRequestProcessed(winPtr->display);
    event.xbutton.send_event = False;
    event.xbutton.display = winPtr->display;
    event.xbutton.window = Tk_WindowId((Tk_Window)winPtr);
    event.xbutton.root = RootWindow(winPtr->display, winPtr->screenNum);
    event.xbutton.subwindow = None;

    event.xbutton.time = CurrentTime;

    event.xbutton.x = (int)xpos;
    event.xbutton.y = (int)ypos;

    event.xbutton.x_root = winPtr->changes.x + (int)xpos;
    event.xbutton.y_root = winPtr->changes.y + (int)ypos;

    event.xbutton.state = glfwButtonState | glfwModifierState;

    event.xbutton.button = xbutton;

    event.xbutton.same_screen = True;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwScrollCallback --
 *
 *      Called when scroll wheel is used.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates ButtonPress/ButtonRelease events for scroll.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwScrollCallback(
    GLFWwindow *window,
    double xoffset,
    double yoffset)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent event;
    double xpos, ypos;
    int button;
    
    if (!winPtr) {
        return;
    }

    /* Get cursor position */
    glfwGetCursorPos(window, &xpos, &ypos);

    /* Map scroll direction to button */
    if (yoffset > 0) {
        button = Button4;  /* Scroll up */
    } else if (yoffset < 0) {
        button = Button5;  /* Scroll down */
    } else if (xoffset > 0) {
        button = 6;  /* Scroll right */
    } else {
        button = 7;  /* Scroll left */
    }

    /* Generate button press. */
    memset(&event, 0, sizeof(XEvent));
    event.type = ButtonPress;
    event.xbutton.serial = LastKnownRequestProcessed(winPtr->display);
    event.xbutton.send_event = False;
    event.xbutton.display = winPtr->display;
    event.xbutton.window = Tk_WindowId((Tk_Window)winPtr);
    event.xbutton.root = RootWindow(winPtr->display, winPtr->screenNum);
    event.xbutton.subwindow = None;
    event.xbutton.time = CurrentTime;
    event.xbutton.x = (int)xpos;
    event.xbutton.y = (int)ypos;
    event.xbutton.x_root = winPtr->changes.x + (int)xpos;
    event.xbutton.y_root = winPtr->changes.y + (int)ypos;
    event.xbutton.state = 0;
    event.xbutton.button = button;
    event.xbutton.same_screen = True;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

    /* Generate button release. */
    event.type = ButtonRelease;
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwKeyCallback --
 *
 *      Called when key is pressed or released.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates KeyPress/KeyRelease event.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwKeyCallback(
    GLFWwindow *window,
    int key,
    int scancode,
    int action,
    int mods)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent event;
    double xpos, ypos;
    
    if (!winPtr) {
        return;
    }

    /* Skip repeat events for now. */
    if (action == GLFW_REPEAT) {
        return;
    }

    /* Update keyboard modifiers for xkbcommon. */
    TkWaylandUpdateKeyboardModifiers(mods);

    /* Get cursor position. */
    glfwGetCursorPos(window, &xpos, &ypos);

    /* Generate key event */
    memset(&event, 0, sizeof(XEvent));
    event.type = (action == GLFW_PRESS) ? KeyPress : KeyRelease;
    event.xkey.serial = LastKnownRequestProcessed(winPtr->display);
    event.xkey.send_event = False;
    event.xkey.display = winPtr->display;
    event.xkey.window = Tk_WindowId((Tk_Window)winPtr);
    event.xkey.root = RootWindow(winPtr->display, winPtr->screenNum);
    event.xkey.subwindow = None;
    event.xkey.time = CurrentTime;
    event.xkey.x = (int)xpos;
    event.xkey.y = (int)ypos;
    event.xkey.x_root = winPtr->changes.x + (int)xpos;
    event.xkey.y_root = winPtr->changes.y + (int)ypos;
    
    /* Convert GLFW modifiers to X11 state. */
    event.xkey.state = 0;
    if (mods & GLFW_MOD_SHIFT)     event.xkey.state |= ShiftMask;
    if (mods & GLFW_MOD_CONTROL)   event.xkey.state |= ControlMask;
    if (mods & GLFW_MOD_ALT)       event.xkey.state |= Mod1Mask;
    if (mods & GLFW_MOD_SUPER)     event.xkey.state |= Mod4Mask;
    if (mods & GLFW_MOD_CAPS_LOCK) event.xkey.state |= LockMask;
    if (mods & GLFW_MOD_NUM_LOCK)  event.xkey.state |= Mod2Mask;
    
    /* Use scancode as keycode - xkbcommon will handle the translation. */
    event.xkey.keycode = scancode;
    event.xkey.same_screen = True;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwCharCallback --
 *
 *      Called when character is input.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Stores character for next TkpGetString() call.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwCharCallback(
    GLFWwindow *window,
    unsigned int codepoint)
{
    /* Store the character for retrieval by TkpGetString(). */
    TkWaylandStoreCharacterInput(codepoint);
}


/*
 *----------------------------------------------------------------------
 *
 * Xsync --
 *
 *      Supports "update" command, kept here for compatibility.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
XSync(
    Display *display,
    TCL_UNUSED(Bool))
{
    /*
     *  The main use of XSync is by the update command, which alternates
     *  between running an event loop to process all events without waiting and
     *  calling XSync on all displays until no events are left.  On X11 the
     *  call to XSync might cause the window manager to generate more events
     *  which would then get processed. Apparently this process stabilizes on
     *  X11, leaving the window manager in a state where all events have been
     *  generated and no additional events can be genereated by updating widgets.
     *
     *  It is not clear what the Wayland port should do when XSync is called, but
     *  currently the best option seems to be to do nothing.  (See ticket
     *  [da5f2266df].)
     */

    LastKnownRequestProcessed(display)++;
    return 0;
}


/* Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
