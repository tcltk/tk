/*
 * tkWaylandEvent.c --
 *
 *	This file implements event management functionality for the Wayland 
 *      backend of Tk (GLFW adaptation).
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2026 Kevin Walzer/WordTech Communications LLC
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>
#include <GLES2/gl2.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

/*
 * Global state for mouse buttons and modifiers.
 * These are used across callbacks to maintain consistent state.
 */
unsigned int glfwButtonState = 0;
unsigned int glfwModifierState = 0;

/*
 * Simple single-codepoint buffer for character input.
 * In a production implementation, this should be a queue or per-window buffer.
 */
static unsigned int pendingCodepoint = 0;

/* Track last window and position for enter/leave events */
static GLFWwindow *lastWindow = NULL;
static double lastX = -1, lastY = -1;


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
    glfwSetKeyCallback             (glfwWindow, TkGlfwKeyCallback);
    glfwSetCharCallback            (glfwWindow, TkGlfwCharCallback);
	glfwSetWindowRefreshCallback   (glfwWindow, TkGlfwWindowRefreshCallback);
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
 *      Destroys the Tk window.
 *
 *----------------------------------------------------------------------
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
 *----------------------------------------------------------------------
 *
 * TkGlfwWindowSizeCallback --
 *
 *      Called when window size changes. Updates window geometry,
 *      generates ConfigureNotify event, and queues an expose for redraw.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates window geometry, generates ConfigureNotify event,
 *      queues Expose event for redraw.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwWindowSizeCallback(
    GLFWwindow *window,
    int width,           /* Total window width including decorations. */
    int height)          /* Total window height including decorations. */
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    TkWaylandDecoration *decor;
    XEvent event;
    int clientWidth, clientHeight;

    if (!winPtr) {
        return;
    }

    /* Update the mapping with new total window size. */
    TkGlfwUpdateWindowSize(window, width, height);

    /* Calculate client area size (inset from decorations if enabled). */
    decor = TkWaylandGetDecoration(winPtr);
    if (decor && decor->enabled) {
        /* Client area is inset by border width on sides and
         * title bar height + border width on top. */
        clientWidth = width - (2 * BORDER_WIDTH);
        clientHeight = height - TITLE_BAR_HEIGHT - BORDER_WIDTH;

        /* Ensure minimum size. */
        if (clientWidth < 1) clientWidth = 1;
        if (clientHeight < 1) clientHeight = 1;
    } else {
        /* No decorations - client area equals total window. */
        clientWidth = width;
        clientHeight = height;
    }

    /* Update Tk's window dimensions. */
    winPtr->changes.width = clientWidth;
    winPtr->changes.height = clientHeight;

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
    event.xconfigure.width = clientWidth;
    event.xconfigure.height = clientHeight;
    event.xconfigure.border_width = winPtr->changes.border_width;
    event.xconfigure.above = None;
    event.xconfigure.override_redirect = winPtr->atts.override_redirect;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

    TkWaylandQueueExposeEvent(winPtr, 0, 0, clientWidth, clientHeight);

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
    TCL_UNUSED(GLFWwindow *),
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

    winPtr->changes.x = xpos;
    winPtr->changes.y = ypos;

    memset(&event, 0, sizeof(XEvent));
    event.type = ConfigureNotify;
    event.xconfigure.serial          = LastKnownRequestProcessed(winPtr->display);
    event.xconfigure.send_event       = False;
    event.xconfigure.display          = winPtr->display;
    event.xconfigure.event            = Tk_WindowId((Tk_Window)winPtr);
    event.xconfigure.window           = Tk_WindowId((Tk_Window)winPtr);
    event.xconfigure.x                = xpos;
    event.xconfigure.y                = ypos;
    event.xconfigure.width            = winPtr->changes.width;
    event.xconfigure.height           = winPtr->changes.height;
    event.xconfigure.border_width     = winPtr->changes.border_width;
    event.xconfigure.above            = None;
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

    memset(&event, 0, sizeof(XEvent));
    event.type = focused ? FocusIn : FocusOut;
    event.xfocus.serial     = LastKnownRequestProcessed(winPtr->display);
    event.xfocus.send_event  = False;
    event.xfocus.display     = winPtr->display;
    event.xfocus.window      = Tk_WindowId((Tk_Window)winPtr);
    event.xfocus.mode        = NotifyNormal;
    event.xfocus.detail      = NotifyAncestor;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

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
        memset(&event, 0, sizeof(XEvent));
        event.type = UnmapNotify;
        event.xunmap.serial     = LastKnownRequestProcessed(winPtr->display);
        event.xunmap.send_event  = False;
        event.xunmap.display     = winPtr->display;
        event.xunmap.event       = Tk_WindowId((Tk_Window)winPtr);
        event.xunmap.window      = Tk_WindowId((Tk_Window)winPtr);
        event.xunmap.from_configure = False;

        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
        winPtr->flags &= ~TK_MAPPED;
    } else {
        memset(&event, 0, sizeof(XEvent));
        event.type = MapNotify;
        event.xmap.serial       = LastKnownRequestProcessed(winPtr->display);
        event.xmap.send_event    = False;
        event.xmap.display       = winPtr->display;
        event.xmap.event         = Tk_WindowId((Tk_Window)winPtr);
        event.xmap.window        = Tk_WindowId((Tk_Window)winPtr);
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
 *      Updates window state in WmInfo.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwWindowMaximizeCallback(
    GLFWwindow *window,
    int maximized)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    
    if (!winPtr) {
        return;
    }
    
    /* Update WmInfo zoomed state if WM info exists. */
    if (winPtr->wmInfoPtr) {
        WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
        wmPtr->attributes.zoomed = maximized;
    }
    
    /* Note: No X event needed for maximize state changes. */
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

    TkWaylandDecoration *decor = TkWaylandGetDecoration(winPtr);
    if (decor) {
	TkWaylandDecorationMouseMove(decor, xpos, ypos);
	/* Don't return — Tk still needs MotionNotify for cursor updates. */
    }

    if (!winPtr) {
        if (lastWindow) {
            TkWindow *lastWinPtr = TkGlfwGetTkWindow(lastWindow);
            if (lastWinPtr) {
                memset(&event, 0, sizeof(XEvent));
                event.type = LeaveNotify;
                event.xcrossing.serial     = LastKnownRequestProcessed(lastWinPtr->display);
                event.xcrossing.send_event  = False;
                event.xcrossing.display     = lastWinPtr->display;
                event.xcrossing.window      = Tk_WindowId((Tk_Window)lastWinPtr);
                event.xcrossing.root        = RootWindow(lastWinPtr->display, lastWinPtr->screenNum);
                event.xcrossing.subwindow   = None;
                event.xcrossing.time        = CurrentTime;
                event.xcrossing.x           = (int)lastX;
                event.xcrossing.y           = (int)lastY;
                event.xcrossing.x_root      = lastWinPtr->changes.x + (int)lastX;
                event.xcrossing.y_root      = lastWinPtr->changes.y + (int)lastY;
                event.xcrossing.mode        = NotifyNormal;
                event.xcrossing.detail      = NotifyAncestor;
                event.xcrossing.same_screen = True;
                event.xcrossing.focus       = False;
                event.xcrossing.state       = glfwButtonState | glfwModifierState;
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
 *      Generates ButtonPress/ButtonRelease events.
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

    glfwGetCursorPos(window, &xpos, &ypos);

    TkWaylandDecoration *decor = TkWaylandGetDecoration(winPtr);
    if (decor && TkWaylandDecorationMouseButton(decor, button, action,
                                                xpos, ypos)) {
        return;  /* Decoration consumed the event. */
    }

    /* Update modifier state. */
    glfwModifierState = 0;

    if (mods & GLFW_MOD_SHIFT)
        glfwModifierState |= ShiftMask;

    if (mods & GLFW_MOD_CONTROL)
        glfwModifierState |= ControlMask;

    if (mods & GLFW_MOD_ALT)
        glfwModifierState |= Mod1Mask;

    if (mods & GLFW_MOD_SUPER)
        glfwModifierState |= Mod4Mask;

    /* Map GLFW button to X11 button and mask. */
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
            /* Buttons 4+ are typically scroll wheel, but map safely. */
            xbutton = button + 1;
            buttonMask = 0;
            break;
    }

    memset(&event, 0, sizeof(XEvent));

    /* Update button state. */
    if (action == GLFW_PRESS) {
        glfwButtonState |= buttonMask;
        event.type = ButtonPress;
    } else {
        glfwButtonState &= ~buttonMask;
        event.type = ButtonRelease;
    }

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

    /* Get cursor position. */
    glfwGetCursorPos(window, &xpos, &ypos);

    /* Map scroll direction to button. */
    if (yoffset > 0) {
        button = Button4;  /* Scroll up */
    } else if (yoffset < 0) {
        button = Button5;  /* Scroll down */
    } else if (xoffset > 0) {
        button = 6;  /* Scroll right */
    } else {
        button = 7;  /* Scroll left */
    }

    /* Generate button press */
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
    TCL_UNUSED(int), /* key */
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

    if (action == GLFW_REPEAT) {
        return;
    }

    TkWaylandUpdateKeyboardModifiers(mods);

    glfwGetCursorPos(window, &xpos, &ypos);

    memset(&event, 0, sizeof(XEvent));
    event.type = (action == GLFW_PRESS) ? KeyPress : KeyRelease;
    event.xkey.serial     = LastKnownRequestProcessed(winPtr->display);
    event.xkey.send_event  = False;
    event.xkey.display     = winPtr->display;
    event.xkey.window      = Tk_WindowId((Tk_Window)winPtr);
    event.xkey.root        = RootWindow(winPtr->display, winPtr->screenNum);
    event.xkey.subwindow   = None;
    event.xkey.time        = CurrentTime;
    event.xkey.x           = (int)xpos;
    event.xkey.y           = (int)ypos;
    event.xkey.x_root      = winPtr->changes.x + (int)xpos;
    event.xkey.y_root      = winPtr->changes.y + (int)ypos;
    
    event.xkey.state = 0;
    if (mods & GLFW_MOD_SHIFT)     event.xkey.state |= ShiftMask;
    if (mods & GLFW_MOD_CONTROL)   event.xkey.state |= ControlMask;
    if (mods & GLFW_MOD_ALT)       event.xkey.state |= Mod1Mask;
    if (mods & GLFW_MOD_SUPER)     event.xkey.state |= Mod4Mask;
    if (mods & GLFW_MOD_CAPS_LOCK) event.xkey.state |= LockMask;
    if (mods & GLFW_MOD_NUM_LOCK)  event.xkey.state |= Mod2Mask;
    
    event.xkey.keycode     = scancode;
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
    TCL_UNUSED(GLFWwindow *), /* window */
    unsigned int codepoint)
{
    TkWaylandStoreCharacterInput(codepoint);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandUpdateKeyboardModifiers --
 *
 *      Update the internal modifier state from GLFW modifier bits.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates the global glfwModifierState variable.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandUpdateKeyboardModifiers(int glfw_mods)
{
    glfwModifierState = 0;

    if (glfw_mods & GLFW_MOD_SHIFT)    glfwModifierState |= ShiftMask;
    if (glfw_mods & GLFW_MOD_CONTROL)  glfwModifierState |= ControlMask;
    if (glfw_mods & GLFW_MOD_ALT)      glfwModifierState |= Mod1Mask;
    if (glfw_mods & GLFW_MOD_SUPER)    glfwModifierState |= Mod4Mask;
    if (glfw_mods & GLFW_MOD_CAPS_LOCK) glfwModifierState |= LockMask;
    if (glfw_mods & GLFW_MOD_NUM_LOCK) glfwModifierState |= Mod2Mask;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandStoreCharacterInput --
 *
 *      Store Unicode codepoint from character input callback.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Stores codepoint for later retrieval.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandStoreCharacterInput(unsigned int codepoint)
{
    pendingCodepoint = codepoint;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetPendingCharacter --
 *
 *      Retrieves and clears the stored Unicode codepoint.
 *
 * Results:
 *      Returns the stored codepoint, or 0 if none pending.
 *
 * Side effects:
 *      Clears the pending codepoint.
 *
 *----------------------------------------------------------------------
 */

unsigned int
TkWaylandGetPendingCharacter(void)
{
    unsigned int codepoint = pendingCodepoint;
    pendingCodepoint = 0;
    return codepoint;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwWindowRefreshCallback --
 *
 *      Called by GLFW when window needs redraw. Generates Expose event.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Queues Expose event for client area.
 *
 *----------------------------------------------------------------------
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
