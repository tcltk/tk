/*
 * tkWaylandNotify.c --
 *
 *      Tcl event source for integrating Wayland/GLFW event loop with Tk.
 *      Now uses file events for proper integration rather than just a timer.
 *
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2015 Marc Culler.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution.
 *
 */

/*
 * This file contains the implementation of a Tcl Notifier for Wayland.
 * The design is .... WRITE THIS.
 */

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <errno.h>
#include <GLES3/gl3.h>
#include "nanovg_gl_utils.h"

extern TkGlfwContext  glfwContext;
extern WindowMapping *windowMappingList;

/* ========================= Thread Specific Data  ========================= */

typedef struct ThreadSpecificData {
    bool           initialized;
    bool           waylandInitialized;
    int            shutdownInProgress; /* flag to prevent recursive shutdown */
    int            callbackCount;  /* used by the setup proc to check for events. */
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

#define TSD_INIT() ThreadSpecificData *tsdPtr = (ThreadSpecificData *) \
    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData))

/* Called by the callback functions to tell the setup proc not to block. */
static void
recordCallback() {
    TSD_INIT();
    tsdPtr->callbackCount++;
}

/* Called by the setup proc to reset the callback counter. */
static void
clearCallbackCount() {
    TSD_INIT();
    tsdPtr->callbackCount = 0;
}

/* ========================== Global State Data  ========================== */

/*
 * Global state for mouse buttons and modifiers.
 * These are used across callbacks to maintain consistent state.
 //// This should be thread local!
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

    if (glfw_mods & GLFW_MOD_SHIFT)     glfwModifierState |= ShiftMask;
    if (glfw_mods & GLFW_MOD_CONTROL)   glfwModifierState |= ControlMask;
    if (glfw_mods & GLFW_MOD_ALT)       glfwModifierState |= Mod1Mask;
    if (glfw_mods & GLFW_MOD_SUPER)     glfwModifierState |= Mod4Mask;
    if (glfw_mods & GLFW_MOD_CAPS_LOCK) glfwModifierState |= LockMask;
    if (glfw_mods & GLFW_MOD_NUM_LOCK)  glfwModifierState |= Mod2Mask;
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

/* ============================== Notifier  ============================== */

static void TkWaylandNotifyExitHandler(void *clientData);
static void TkWaylandSetupProc(void *clientData, int flags);
static void TkWaylandCheckProc(void *clientData, int flags);
static void TkWaylandCheckForWindowClosure(void);

/*
 *----------------------------------------------------------------------
 *
 * Tk_WaylandSetupTkNotifier --
 *
 *      Called during Tk initialization to install the Wayland/GLFW
 *      event source. Creates an eventfd for wakeup and registers it
 *      with Tcl's event loop for proper integration.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Installs TkWaylandSetupProc and TkWaylandCheckProc
 *      as Tcl event sources. Creates a wakeup file descriptor and
 *      registers it with Tcl. Creates a heartbeat timer as fallback.
 *      Registers an exit handler for cleanup.
 *
 *----------------------------------------------------------------------
 */
 
void
Tk_WaylandSetupTkNotifier(void)
{
    TSD_INIT();

    if (!tsdPtr->initialized) {
        tsdPtr->initialized   = true;
        tsdPtr->shutdownInProgress = 0;

        /* Create the Tcl event source. */
        Tcl_CreateEventSource(TkWaylandSetupProc,
                              TkWaylandCheckProc, NULL);
        TkCreateExitHandler(TkWaylandNotifyExitHandler, NULL);
    }
    
    /* Wake up the event loop. */
    TkWaylandWakeupGLFW();
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCheckForWindowClosure --
 *
 *      Check if all Tk main windows have been destroyed and initiate
 *      graceful shutdown if needed.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May trigger cleanup of GLFW resources if no windows remain.
 *
 *----------------------------------------------------------------------
 */

static void
TkWaylandCheckForWindowClosure(void)
{
    TSD_INIT();
    
    if (tsdPtr->shutdownInProgress) return;
    
    /* If there are no Tk main windows left, start shutdown. */
    if (Tk_GetNumMainWindows() == 0) {
        tsdPtr->shutdownInProgress = 1;
        
        /* Schedule cleanup as idle callback. */
        Tcl_DoWhenIdle(TkWaylandNotifyExitHandler, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSetupProc --
 *
 *      Tell Tcl how long we are willing to block. Called by Tcl_DoOneEvent
 *      before blocking. Uses our wakeup fd to determine block time.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Sets the maximum block time based on pending events.
 *
 *----------------------------------------------------------------------
 */
    
static void
TkWaylandSetupProc(TCL_UNUSED(void *),
			 int flags)
{
    TSD_INIT();
    Tcl_Time noBlock = {0, 0};        /* secs, microsecs */
    Tcl_Time oneRefresh = {0, 16667}; /* ~ 1/60 sec */
    
    if (tsdPtr->shutdownInProgress) {
        /* Don't block during shutdown. */
        Tcl_SetMaxBlockTime(&noBlock);
        return;
    }

    /*
     * Clear the callback counter and call glfwPollEvents.
     * If there were no events, block for one display cycle.
     * Otherwise, don't block.
     */

    clearCallbackCount();
    glfwPollEvents();
    if (tsdPtr->callbackCount) {
	Tcl_SetMaxBlockTime(&noBlock);
    } else {
	Tcl_SetMaxBlockTime(&oneRefresh);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCheckProc --
 *
 *      Process pending Wayland/GLFW events and queue Tk events.
 *      Called by Tcl_DoOneEvent after calling Tcl_WaitForEvent, which
 *      will call tclNotifierHooks.waitForEventProc if it is defined.
 *      We are using the default waitForEventProc (I think).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Begins the event cycle for the main window to enable rendering.
 *
 *----------------------------------------------------------------------
 */
 
static void
TkWaylandCheckProc(TCL_UNUSED(void *),
	int flags) 
{
    //// Currently we don't need to do anything here. (????)
    if (!(flags & TCL_WINDOW_EVENTS)) {
	printf("CheckProc called without WINDOW_EVENTS\n");
	return;
    }
} 
/*
 *----------------------------------------------------------------------
 *
 * TkWaylandNotifyExitHandler --
 *
 *      Clean up at exit. Removes the event source, deletes the
 *      heartbeat timer and file handler, and marks the notifier as
 *      uninitialized. Called both on normal exit and when last window
 *      is closed.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Removes the Wayland event source from Tcl's event loop.
 *      Cancels the heartbeat timer and file handler if active.
 *
 *----------------------------------------------------------------------
 */
 
static void
TkWaylandNotifyExitHandler(TCL_UNUSED(void *))
{
    TSD_INIT();

    if (!tsdPtr->initialized)
        return;

    /* Prevent re-entrancy. */
    if (tsdPtr->shutdownInProgress == 1) {
       return;
    }

    /* Remove event source. */
    Tcl_DeleteEventSource(TkWaylandSetupProc,
                          TkWaylandCheckProc, NULL);

    tsdPtr->initialized = false;
    
    /* Note: We don't call TkGlfwShutdown here. */
    //// Why not?
}

/* ========================== XEvents  ========================== */

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandQueueExposeEvent --
 *
 *      Queue Expose events for a window and all of its children.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Queues an expose event for processing.
 *
 *----------------------------------------------------------------------
 */
 
void
TkWaylandQueueExposeEvent(
    TkWindow *winPtr,
    int       x, int y,
    int       width, int height)
{
    XEvent event;
    TkWindow *childPtr;
    printf("TkWaylandQueueExposeEvent: %s\n", Tk_PathName(winPtr));
    
    if (!winPtr) return;
    
    
    /* Create expose event. */
    memset(&event, 0, sizeof(XEvent));
    event.type = Expose;
    event.xexpose.serial = LastKnownRequestProcessed(winPtr->display)++;
    event.xexpose.send_event = False;
    event.xexpose.display = winPtr->display;
    event.xexpose.window = Tk_WindowId(winPtr);
    event.xexpose.x = x;
    event.xexpose.y = y;
    event.xexpose.width = width;
    event.xexpose.height = height;
    event.xexpose.count = 0;
    
    /* Queue it. */
    printf("Queuing Expose(%lu)\n", event.xexpose.serial);
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    
    /* Recurse through the children of this window. */
    for (childPtr = winPtr->childList; childPtr != NULL;
         childPtr = childPtr->nextPtr) {
        if (!Tk_IsMapped((Tk_Window)childPtr) ||
	    Tk_IsTopLevel((Tk_Window)childPtr)) {
	    //if (Tk_IsTopLevel((Tk_Window)childPtr)) {
            continue;
        }
        TkWaylandQueueExposeEvent(childPtr, 
                                 0, 0,
                                 Tk_Width((Tk_Window)childPtr),
                                 Tk_Height((Tk_Window)childPtr));
    }
}

/* ========================== GLFW Callbacks ========================== */

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwSetupCallbacks --
 *
 *      Register the standard GLFW callbacks for a window.  Called by
 *      glfwCreateWindow.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Registers all standard callbacks.
 *
 *----------------------------------------------------------------------
 */

static void TkGlfwWindowCloseCallback(GLFWwindow *window);
static void TkGlfwWindowSizeCallback(GLFWwindow *window, int width, int height);
static void TkGlfwFramebufferSizeCallback(GLFWwindow *window, int width, int height);
static void TkGlfwWindowPosCallback(GLFWwindow *window, int xpos, int ypos);
static void TkGlfwWindowFocusCallback(GLFWwindow *window, int focused);
static void TkGlfwWindowIconifyCallback(GLFWwindow *window, int iconified);
static void TkGlfwWindowMaximizeCallback(GLFWwindow *window, int maximized);
static void TkGlfwCursorPosCallback(GLFWwindow *window, double xpos, double ypos);
static void TkGlfwMouseButtonCallback(GLFWwindow *window, int button, int action, int mods);
static void TkGlfwScrollCallback(GLFWwindow *window, double xoffset, double yoffset);
static void TkGlfwKeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods);
static void TkGlfwCharCallback(GLFWwindow *window, unsigned int codepoint);
static void TkGlfwWindowRefreshCallback(GLFWwindow *window);

MODULE_SCOPE void
TkGlfwSetupCallbacks(
    GLFWwindow *glfwWindow)
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
    glfwSetKeyCallback             (glfwWindow, TkGlfwKeyCallback);
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
 
static void
TkGlfwWindowCloseCallback(GLFWwindow *window)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    recordCallback();
    
    if (winPtr) {
        Tk_DestroyWindow((Tk_Window)winPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwWindowSizeCallback --
 *
 *      Called when window size changes due to user resizing.
 *      The size reported may be smaller than the framebuffer
 *      size on a high resolution monitor when a screen logical
 *      pixel is composed of more than one monitor pixel.  This
 *      scaling is managed internally by Tk, so all drawing should
 *      be done with framebuffer pixels, not with the size provided
 *      here.  The only value to the call back is to compare
 *      the window size with the framebuffer size in order to
 *      measure the size of a logical pixel.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates window geometry, generates ConfigureNotify event.
 *
 *----------------------------------------------------------------------
 */

static void
TkGlfwWindowSizeCallback(GLFWwindow *window, int width, int height)
{
    recordCallback();
    printf("TkGlfWindowSizeCallback\n");
    int fbwidth, fbheight;
    glfwGetFramebufferSize(window, &fbwidth, &fbheight);
    float pixelRatio = (float) fbwidth / (float) width;
    printf("This window has pixel ratio %.1f.\n", pixelRatio);
    
    WindowMapping *mapping = FindMappingByGLFW(window);
    if (!mapping) {
	return;
    }
    
    /* Close any open frame. */
    //// can this ever happen?  I think this can be removed.
    if (mapping->frameOpen) {
	printf("TkGlfwWindowSizeCallback: size changed with frame open\n");
        TkWaylandEndEventCycle(mapping);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwFramebufferSizeCallback --
 *
 *      Called when framebuffer size changes.  Note that this is always called
 *      when a window changes size, whether by interactive resizing with the
 *      mouse or programatic resizing with wm geometry.  The
 *      WindowSizeCallback is only called for interactive resizes.  This
 *      generates a ConfigureNotify event for the window.  The subsequent
 *      WindowRefreshCallback generates ExposeNotify events for the
 *      window and all of its children.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Rebuilds the backing store framebuffer. 
 *
 *----------------------------------------------------------------------
 */

static void
TkGlfwFramebufferSizeCallback(
    GLFWwindow *window,
    int width,
    int height)
{
    recordCallback();
    printf("TkGlfwFramebufferSizeCallback ");
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    if (!winPtr) {
	printf("No Tk window!\n");
	return;
    }
    WindowMapping *mapping;
    mapping = FindMappingByTk(winPtr);
    if (!mapping) {
	return;
    }
    printf("Rebuilding framebuffer for %s to size %dx%d\n",
	   Tk_PathName(mapping->tkWindow), width, height);
    nvgluDeleteFramebuffer(mapping->fbo);
    // Should be in window private struct.
    extern TkGlfwContext glfwContext;
    // Rebuild the backing store
    mapping->fbo = nvgluCreateFramebuffer(glfwContext.vg, width, height, 0);

    /* Notify Tk */
    mapping->width = width;
    mapping->height = height;
    winPtr->changes.width = width;
    winPtr->changes.height = height;
    printf("TkGlfwFramebufferSizeCallback: Configuring\n");
    TkDoConfigureNotify(winPtr);
    // Wayland wants a call to glfwSwapBuffers very soon after
    // it sends a configure notification.  Some people (or bots)
    // recommend redrawing the window before this callback
    // returns.  This is a test of that concept.
    //printf("TkGlfwFramebufferSizeCallback: Exposing\n");
    //TkWaylandQueueExposeEvent(winPtr, 0, 0, width, height);
    //while (Tcl_DoOneEvent(TCL_ALL_EVENTS|TCL_DONT_WAIT)) {};

    /* Update ViewPort */
    glViewport(0, 0, width, height);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwWindowPosCallback --
 *
 *      This is never called on Wayland, according to GLFW.
 *      Wayland hides the position of an app on the screen.
 *      The only way to move (or focus) a window is to do
 *      it with the mouse.  This will be a major limitation
 *      when it comes time to run the Tk tests.
 *      See https://www.glfw.org/docs/3.4/group__window.html#ga08bdfbba88934f9c4f92fd757979ac74
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates window geometry, generates ConfigureNotify event.
 *
 *----------------------------------------------------------------------
 */
 
static void
TkGlfwWindowPosCallback(
    GLFWwindow *window,
    int xpos,
    int ypos)
{
    recordCallback();
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    if (!winPtr) {
	printf("TkGlfwWindowPosCallback: no Tk window\n");
        return;
    }
    printf("TkGlfwWindowPosCallback: %s -> to %d+%d\n",
	   Tk_PathName(winPtr), xpos, ypos);

    winPtr->changes.x = xpos;
    winPtr->changes.y = ypos;
    TkDoConfigureNotify(winPtr);
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
 
static void
TkGlfwWindowFocusCallback(
    GLFWwindow *window,
    int focused)
{
    recordCallback();
    printf("TkGlfwWindowFocusCallback\n");
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent event;
    
    if (!winPtr) {
        return;
    }

    memset(&event, 0, sizeof(XEvent));
    event.type = focused ? FocusIn : FocusOut;
    event.xfocus.serial     = LastKnownRequestProcessed(winPtr->display)++;
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

static void
TkGlfwWindowIconifyCallback(
    GLFWwindow *window,
    int iconified)
{
    recordCallback();
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    printf("TkGlfwWindowIconifyCallback: %s\n", Tk_PathName(winPtr));
    XEvent event;
    
    if (!winPtr) {
        return;
    }

    if (iconified) {
        memset(&event, 0, sizeof(XEvent));
        event.type = UnmapNotify;
        event.xunmap.serial     = LastKnownRequestProcessed(winPtr->display)++;
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
        event.xmap.serial       = LastKnownRequestProcessed(winPtr->display)++;
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

static void
TkGlfwWindowMaximizeCallback(
    GLFWwindow *window,
    int maximized)
{
    recordCallback();
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
 
static void
TkGlfwCursorPosCallback(
    GLFWwindow *window,
    double xpos,
    double ypos)
{
    recordCallback();
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent event;

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
 
static void
TkGlfwMouseButtonCallback(
    GLFWwindow *window,
    int button,
    int action,
    int mods)
{
    recordCallback();
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent event;
    double xpos, ypos;
    unsigned int buttonMask = 0;
    unsigned int xbutton = Button1;

    if (!winPtr) {
        return;
    }

    /* Get cursor position. */
    glfwGetCursorPos(window, &xpos, &ypos);

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

static void
TkGlfwScrollCallback(
    GLFWwindow *window,
    double xoffset,
    double yoffset)
{
    recordCallback();
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

static void
TkGlfwKeyCallback(GLFWwindow *window,
		  int key,
		  TCL_UNUSED(int), /* scancode */
		  int action,
		  int mods)
{
    recordCallback();
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    TkWindow *focusWin;
    XEvent event;
    double xpos, ypos;
    int x, y;

    if (!winPtr || action == GLFW_REPEAT) return;

    TkWaylandUpdateKeyboardModifiers(mods);
    glfwGetCursorPos(window, &xpos, &ypos);
    x = floor(xpos);
    y = floor(ypos);

    /* Route to focused child widget if any. */
    focusWin = winPtr;
    if (winPtr->dispPtr->focusPtr != NULL) {
	focusWin = winPtr->dispPtr->focusPtr;
    }

    memset(&event, 0, sizeof(XEvent));
    event.type = (action == GLFW_PRESS) ? KeyPress : KeyRelease;
    event.xkey.serial      = LastKnownRequestProcessed(winPtr->display);
    event.xkey.send_event  = False;
    event.xkey.display     = winPtr->display;
    event.xkey.window      = Tk_WindowId((Tk_Window)focusWin);
    event.xkey.root        = RootWindow(winPtr->display, winPtr->screenNum);
    event.xkey.time        = CurrentTime;
    event.xkey.x           = x;
    event.xkey.y           = y;
    event.xkey.x_root      = winPtr->changes.x + x;
    event.xkey.y_root      = winPtr->changes.y + y;
    event.xkey.state       = 0;
    if (mods & GLFW_MOD_SHIFT)     event.xkey.state |= ShiftMask;
    if (mods & GLFW_MOD_CONTROL)   event.xkey.state |= ControlMask;
    if (mods & GLFW_MOD_ALT)       event.xkey.state |= Mod1Mask;
    if (mods & GLFW_MOD_SUPER)     event.xkey.state |= Mod4Mask;
    if (mods & GLFW_MOD_CAPS_LOCK) event.xkey.state |= LockMask;
    if (mods & GLFW_MOD_NUM_LOCK)  event.xkey.state |= Mod2Mask;
    event.xkey.keycode     = key;   /* GLFW key constant, not raw scancode */
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
 
static void
TkGlfwCharCallback(
    TCL_UNUSED(GLFWwindow *), /* window */
    unsigned int codepoint)
{
    recordCallback();
    printf("TkGlfwCharCallback\n");
    TkWaylandStoreCharacterInput(codepoint);
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

static void
TkGlfwWindowRefreshCallback(GLFWwindow *window)
{
    recordCallback();
    TkWindow      *winPtr = TkGlfwGetTkWindow(window);
    WindowMapping *mapping;
    int            w, h;

    if (!winPtr) return;

    mapping = FindMappingByTk(winPtr);
    if (!mapping) return;

    w = mapping->width  > 0 ? mapping->width  : winPtr->changes.width;
    h = mapping->height > 0 ? mapping->height : winPtr->changes.height;

    printf("TkGlWindowRefreshCallback Expose\n");
    TkWaylandQueueExposeEvent(winPtr, 0, 0, w, h);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWakeupGLFW --
 *
 *      Public function to wake up the GLFW event loop from another
 *      thread or context. Used by GLFW callbacks to ensure Tcl
 *      processes events promptly.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Writes to the eventfd, causing the file handler to trigger.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandWakeupGLFW(void)
{
    TSD_INIT();
    uint64_t u = 1;
    
    if (tsdPtr->initialized && !tsdPtr->shutdownInProgress) {
	//// This should post an empty event to GLFW!!!
        //write(tsdPtr->wakeupFd, &u, sizeof(u));
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandBeginEventCycle --
 *
 *      Called at the START of each event loop iteration, before
 *      processing any expose events. Opens the NanoVG frame.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Opens NanoVG frame, clears framebuffer, sets up viewport.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandBeginEventCycle(WindowMapping *m)
{
    if (!m || !m->glfwWindow) return;

	if (m->frameOpen) {
        /* Already in drawing round – do NOT clear again. */
		fprintf(stderr, "BeginEventCycle: already open, continuing\n");
		return;
	}


    glfwMakeContextCurrent(m->glfwWindow);

#if 0
    int fbw, fbh;
    glfwGetFramebufferSize(m->glfwWindow, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);

    /* Clear only when starting new frame. */
    //// Changed to purple for debugging
    glClearColor(0.92f, 0.92f, 0.92f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    nvgBeginFrame(glfwContext.vg,
                  (float)m->width,
                  (float)m->height,
                  (float)fbw / (float)m->width);

    nvgSave(glfwContext.vg);
    nvgScale(glfwContext.vg, 1.0f, -1.0f);
    nvgTranslate(glfwContext.vg, 0.0f, -(float)m->height);
    nvgTranslate(glfwContext.vg, 0.5f, 0.5f);

    m->frameOpen = 1;
    m->inEventCycle = 1;
    glfwContext.activeFrame = m;
#endif
}
/*
 *----------------------------------------------------------------------
 *
 * TkWaylandEndEventCycle --
 *
 *      Called at the END of each event loop iteration, after all
 *      widgets have drawn. Closes the NanoVG frame and swaps buffers.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Ends NanoVG frame, swaps GL buffers.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandEndEventCycle(TCL_UNUSED(WindowMapping *))
{
	return;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandScheduleDisplay --
 *
 *      Schedules a redraw of a Tk window as an idle handler.
 *
 * Results:
 *      Redraw scheduled.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandScheduleDisplay(WindowMapping *m)
{
    if (!m->needsDisplay) {
        m->needsDisplay = 1;
        /* Queue idle to close frame after widgets draw. */
        Tcl_DoWhenIdle(TkWaylandDisplayProc, m);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDisplayProc --
 *
 *      Completes NanoVG drawing cycle.
 *
 * Results:
 *      Drawing complete.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandDisplayProc(ClientData clientData)
{
#if 0 //// What is this for?
    WindowMapping *m = (WindowMapping *)clientData;
    if (!m || !m->fbo) return;

    glfwMakeContextCurrent(m->glfwWindow);
    
    int fbw, fbh;
    glfwGetFramebufferSize(m->glfwWindow, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);

    /* Clear only the screen backbuffer, not our FBO! */
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    m->needsDisplay = 0;
#endif
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
