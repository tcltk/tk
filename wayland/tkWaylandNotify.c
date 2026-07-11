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
 *
 * The design is a single-threaded event loop integration grafting GLFW's
 * main-thread polling mechanics and native protocols (IBus/DBus) onto
 * the standard Tcl Notifier framework (Tcl_CreateEventSource).
 *
 * Architecture and Event Flow
 * ----------------------------
 *
 * Frame Presentation
 *   Synchronized with Tcl's idle cycle via TkWaylandDisplayAllWindows
 *   at the start of SetupProc. FBO data is blitted to GLFW buffer 0 and swapped.
 *
 * IPC Message Draining
 *   IBus/DBus messages are drained inline via sd_bus_process
 *   in SetupProc/CheckProc to prevent input starvation during continuous redraw cycles.
 *
 * GLFW Event Polling
 *   SetupProc runs glfwPollEvents(). If window callbacks fire,
 *   Tcl is instructed not to block (Tcl_SetMaxBlockTime({0,0})). Otherwise, it
 *   rests for one display frame (~16.6ms) to manage CPU load.
 *
 * Inter-Thread Wakeups
 *   TkWaylandWakeupGLFW forces instant wakeups using
 *   glfwPostEmptyEvent() paired with Tcl_ThreadAlert.
 */

#include "tkInt.h"
#include "tkWaylandInt.h"

#include <GLES3/gl3.h>


#define NANOVG_GLES3 1
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

#include <xkbcommon/xkbcommon.h>
#include <GLFW/glfw3.h>
#include <unistd.h>
#include <errno.h>
#include "GLFW/glfw3native.h"
#include <poll.h>
	

/*
 * Forward declarations for IBus integration (implemented in tkWaylandKey.c).
 * These wrappers accept Tk_Window so this file never needs to see IbusContext*.
 */
extern int  TkWaylandIbusCreateContext(Tcl_Interp *interp, Tk_Window tkwin);
extern void TkWaylandIbusFocusIn(Tk_Window tkwin);
extern void TkWaylandIbusFocusOut(Tk_Window tkwin);
extern bool  TkWaylandIbusProcessKey(Tk_Window tkwin, uint32_t keyval,
                                    uint32_t keycode, uint32_t state);
extern void RemoveIbusContext(Tk_Window tkwin);
extern TkXKBState xkbState;

/*
 * Forward declarations for menu popup input routing (tkWaylandMenu.c).
 * These are called from the GLFW mouse callbacks below when the menu
 * stack is active, routing events to the menu system before Tk sees them.
 */
extern int  TkWaylandMenuPopupActive(void);
extern void TkWaylandMenuHandlePointerButton(int x, int y,
                                             int button, int state);
extern void TkWaylandMenuHandlePointerMotion(int x, int y);
extern void TkWaylandMenuRedrawActive(void);
extern void TkWaylandMenubarResize(TkWindow *winPtr);
extern int  TkWaylandMenubarHandleClick(TkWindow *winPtr, int x, int y,
                                             int button);
extern int  TkWaylandMenubarHandleMotion(TkWindow *winPtr, int x, int y);
extern int TkWaylandMenubarActivateFirst(TkWindow *winPtr);
extern void TkWaylandMenubarMove(TkWindow *winPtr, int direction);
extern int  TkWaylandMenuActive(void);
extern Tk_Window TkWaylandMenuGetTopmostWindow(void);
extern int  TkWaylandMenuGetDepth(void);
extern int  TkWaylandMenuStackRootIsMenubar(void);
extern void TkWaylandMenuPopToDepth(int depth);
extern Tk_Window TkWaylandMenuGetParentWindow(void);
extern void TkWaylandMenuOpenCascade(TkMenu *menuPtr, TkMenuEntry *mePtr);
extern void TkWaylandMenuHandleEscape(void);
extern void TkWaylandMenuDismissAll(void);

/*
 * Direct reference to the IBus bus so the notifier can drain it without
 * going through the file-handler path.  The bus is private to
 * tkWaylandKey.c; we declare it extern here rather than exposing it in a
 * header because only the notifier needs it.
 */
#include <systemd/sd-bus.h>
extern sd_bus *ibus_bus;      /* defined in tkWaylandKey.c */


/* Thread-specific data for the event loop. */

typedef struct ThreadSpecificData {
    bool           initialized;
    bool           waylandInitialized;
    int            shutdownInProgress; /* flag to prevent recursive shutdown */
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

#define TSD_INIT() ThreadSpecificData *tsdPtr = (ThreadSpecificData *) \
    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData))

/*
 * Global state for mouse buttons and modifiers.
 * These are used across callbacks to maintain consistent state.
 */
unsigned int glfwButtonState = 0;
unsigned int glfwModifierState = 0;

/* Track last window for enter/leave events */
static TkWindow *lastWinPtr = NULL;

/*
 * Utility functions for keyboard/input method support. 
 */

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
 * TkWaylandStoreTextInput --
 *
 *      Converts a codepoint to UTF-8 and appends the Utf-8 string to the
 *      pendingText DString stored in the window's TkWindowPrivate struct.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Stores text for later retrieval.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandStoreText(TkWindow *winPtr, unsigned int codepoint)
{
    char buffer[7];
    int length = Tcl_UniCharToUtf(codepoint, buffer);
    Tcl_DStringAppend(&winPtr->privatePtr->pendingText, buffer, length);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSetStoredText --
 *
 *      Stores pending text on a toplevel window for retrieval by
 *      TkpGetString.  Replaces any previously stored text.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates the pendingText DString on winPtr->privatePtr.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandSetStoredText(TkWindow *winPtr, const char *text)
{
    Tcl_DStringFree(&winPtr->privatePtr->pendingText);
    Tcl_DStringAppend(&winPtr->privatePtr->pendingText, text, TCL_INDEX_NONE);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetStoredText --
 *
 *      Retrieves a pointer to the stored pending text for a toplevel.
 *
 * Results:
 *      Returns a pointer to the pending text.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char*
TkWaylandGetStoredText(TkWindow *winPtr)
{
    return Tcl_DStringValue(&winPtr->privatePtr->pendingText);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandClearStoredTextInput --
 *
 *      Clears the DString holding pending text for a window.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      Clears the pending text.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandClearStoredText(TkWindow *winPtr)
{
    Tcl_DStringSetLength(&winPtr->privatePtr->pendingText, 0);
}

/*
 * Notifier / event loop functions. 
 */

static void TkWaylandNotifyExitHandler(void *clientData);
static void TkWaylandSetupProc(void *clientData, int flags);
static void TkWaylandCheckProc(void *clientData, int flags);
static void TkWaylandCheckForWindowClosure(void);

/* Idle loop presentation architecture. */

static int displayIdleQueued = 0;

static void
TkWaylandDisplayIdleCallback(TCL_UNUSED(void *))
{
    displayIdleQueued = 0;
    TkWaylandDisplayAllWindows();
}

/*
 * Dummy function for pending redraw check – can be extended later.
 */
static inline int TkWaylandHasPendingRedraw(void) {
    return 0;  /* currently we redraw synchronously in every iteration */
}

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
 *      Tell Tcl how long it should block before calling TkWaylandCheckProc.
 *      Called by Tcl_DoOneEvent if it has processed all events, run
 *      all pending idle tasks, and run all expired timer tasks.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Processes all queued GLFW events, displays windows as
 *      needed and sets a block time if there are no GLFW events.
 *
 *----------------------------------------------------------------------
 */

static void
TkWaylandSetupProc(TCL_UNUSED(void *), int flags)
{
    if (!(flags & TCL_WINDOW_EVENTS)) {
        return;
    }

    static Tcl_Time noBlock   = {0, 0};
    static Tcl_Time tinyBlock = {0, 1000};   /* 1ms fallback */

    struct wl_display *display = glfwGetWaylandDisplay();
    if (!display) {
        /* No Wayland display: poll GLFW but do NOT block Tcl */
        glfwPollEvents();
        Tcl_SetMaxBlockTime(&noBlock);
        return;
    }

    int fd = wl_display_get_fd(display);

    /* Always drain pending redraw before deciding block time */
    TkWaylandDisplayAllWindows();

    /* Drain IME/IBus messages without blocking */
    if (ibus_bus) {
        while (sd_bus_process(ibus_bus, NULL) > 0) {}
    }

    /* Poll GLFW once per cycle — never inside CheckProc */
    glfwPollEvents();

    /* Schedule display idle only when needed */
    if (TkWaylandHasPendingRedraw()) {
        Tcl_SetMaxBlockTime(&noBlock);
        return;
    }

    /* Check Wayland fd readiness */
    struct pollfd pfd = {
        .fd      = fd,
        .events  = POLLIN,
        .revents = 0
    };

    int r = poll(&pfd, 1, 0);

    if (r > 0 && (pfd.revents & POLLIN)) {
        /* Wayland has events — do not block */
        Tcl_SetMaxBlockTime(&noBlock);
    } else {
        /* Nothing pending — allow a tiny sleep */
        Tcl_SetMaxBlockTime(&tinyBlock);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCheckProc --
 *
 *      Called by Tcl_DoOneEvent after calling Tcl_WaitForEvent, which
 *      will call tclNotifierHooks.waitForEventProc if it is defined.
 *      We are using the default waitForEventProc (I think).
 *
 *      The SetupProc already processed all events that were in the
 *      queue.  If there were none then it will have requested a block
 *      for a few milliseconds.  So there could be some events in the
 *      queue by now.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May generate some X events when callbacks are called.
 *
 *----------------------------------------------------------------------
 */
 
static void
TkWaylandCheckProc(TCL_UNUSED(void *), int flags)
{
    if (!(flags & TCL_WINDOW_EVENTS)) {
        return;
    }

    /* Drain IME/IBus messages — never block */
    if (ibus_bus) {
        while (sd_bus_process(ibus_bus, NULL) > 0) {}
    }

    /* Drain Wayland events */
    struct wl_display *display = glfwGetWaylandDisplay();
    if (display) {
        wl_display_dispatch_pending(display);
    }

    /* Drain redraw */
    TkWaylandDisplayAllWindows();
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
    
    /* Note: We don't call TkWaylandShutdown here. */
    //// Why not?
}

/* XEvents. */

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

/*
 * The rules for geometry managers are designed so that drawing all widgets in
 * the stacking order, which by definition is the depth-first order of the
 * pathname hierarchy determined by the ordering of children, is guaranteed to
 * produce a correctly rendered window, even though the containment hierarchy
 * can be different from the pathname hierarchy.  The default convention is
 * that the children of a widget are displayed inside of it and have higher
 * stacking order.  The -in option allows deviations from this, but they are
 * limited.  First, a container specified with the -in option must be a
 * descendant of the parent of the content.  That is enforced at the API level
 * but does not guarantee correct display because it allows a container to be
 * a descendent of a sibling of the content's parent which appears later in
 * the parent's list of children.  However, an additional unenforced rule is
 * that the container must actually be lower in the stacking order than its
 * content.  The following function generates expose events for a widget
 * and its descendents in the stacking order.
 */

void
TkWaylandQueueExposeEvent(
    TkWindow *winPtr,
    int       x, int y,
    int       width, int height)
{
    XEvent event;
    TkWindow *childPtr;
    fprintf(stderr, "TkWaylandQueueExposeEvent: %s\n", Tk_PathName(winPtr));
    
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
    event.xexpose.count = 0;    /* This forces ttk to handle the event. */
    
    /* Queue it. */
    printf("Queuing Expose(%lu) for %s in %dx%d\n",
		event.xexpose.serial, Tk_PathName(winPtr), width, height);
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

#if 0
    /* Recurse through the children of this window. */
    #if 1
    for (childPtr = winPtr->childList; childPtr != NULL;
         childPtr = childPtr->nextPtr) {
        if (!Tk_IsMapped(childPtr) || Tk_IsTopLevel(childPtr)) {
            continue;
        }
        TkWaylandQueueExposeEvent(childPtr, 0, 0, Tk_Width(childPtr),
				  Tk_Height(childPtr));
    }
    #endif
#endif
}

/* 
 * GLFW callbacks. These functions integrate the native GFLW events
 * with Tk's event loop.
 */

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSetupCallbacks --
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

static void TkWaylandWindowCloseCallback(GLFWwindow *window);
static void TkWaylandFramebufferSizeCallback(GLFWwindow *window,
					  int width, int height);
static void TkWaylandWindowPosCallback(GLFWwindow *window, int xpos, int ypos);
static void TkWaylandWindowFocusCallback(GLFWwindow *window, int focused);
static void TkWaylandWindowIconifyCallback(GLFWwindow *window, int iconified);
static void TkWaylandWindowMaximizeCallback(GLFWwindow *window, int maximized);
static void TkWaylandCursorPosCallback(GLFWwindow *window,
				    double xpos, double ypos);
static void TkWaylandMouseButtonCallback(GLFWwindow *window,
				      int button, int action, int mods);
static void TkWaylandScrollCallback(GLFWwindow *window,
				 double xoffset, double yoffset);
static void TkWaylandKeyCallback(GLFWwindow *window, int key,
			      int scancode, int action, int mods);
static void TkWaylandCharCallback(GLFWwindow *window, unsigned int codepoint);
static void TkWaylandWindowRefreshCallback(GLFWwindow *window);
static void TkWaylandCursorEnterCallback(GLFWwindow *window, int entered);

MODULE_SCOPE void
TkWaylandSetupCallbacks(
    GLFWwindow *glfwWindow)
{
    glfwSetWindowCloseCallback     (glfwWindow, TkWaylandWindowCloseCallback);
    glfwSetFramebufferSizeCallback (glfwWindow, TkWaylandFramebufferSizeCallback);
    glfwSetWindowPosCallback       (glfwWindow, TkWaylandWindowPosCallback);
    glfwSetWindowFocusCallback     (glfwWindow, TkWaylandWindowFocusCallback);
    glfwSetWindowIconifyCallback   (glfwWindow, TkWaylandWindowIconifyCallback);
    glfwSetWindowMaximizeCallback  (glfwWindow, TkWaylandWindowMaximizeCallback);
    glfwSetCursorPosCallback       (glfwWindow, TkWaylandCursorPosCallback);
    glfwSetCursorEnterCallback     (glfwWindow, TkWaylandCursorEnterCallback);
    glfwSetMouseButtonCallback     (glfwWindow, TkWaylandMouseButtonCallback);
    glfwSetScrollCallback          (glfwWindow, TkWaylandScrollCallback);
    glfwSetWindowRefreshCallback   (glfwWindow, TkWaylandWindowRefreshCallback);
    glfwSetKeyCallback             (glfwWindow, TkWaylandKeyCallback);
    glfwSetCharCallback            (glfwWindow, TkWaylandCharCallback);
}

MODULE_SCOPE void
TkWaylandClearCallbacks(
    GLFWwindow *glfwWindow)
{
    glfwSetWindowCloseCallback        (glfwWindow, NULL);
    glfwSetFramebufferSizeCallback    (glfwWindow, NULL);
    glfwSetWindowPosCallback          (glfwWindow, NULL);
    glfwSetWindowFocusCallback        (glfwWindow, NULL);
    glfwSetWindowIconifyCallback      (glfwWindow, NULL);
    glfwSetWindowMaximizeCallback     (glfwWindow, NULL);
    glfwSetCursorPosCallback          (glfwWindow, NULL);
    glfwSetCursorEnterCallback        (glfwWindow, NULL);
    glfwSetMouseButtonCallback        (glfwWindow, NULL);
    glfwSetScrollCallback             (glfwWindow, NULL);
    glfwSetWindowRefreshCallback      (glfwWindow, NULL);
    glfwSetKeyCallback                (glfwWindow, NULL);
    glfwSetCharCallback               (glfwWindow, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWindowCloseCallback --
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
 
/* Helper function to avoid type mismatch in callback. */
static void DestroyWindowIdleProc(void *clientData)
{
    Tk_DestroyWindow((Tk_Window)clientData);
}
 
static void
TkWaylandWindowCloseCallback(GLFWwindow *window)
{
    TkWindow *winPtr = TkWaylandGetTkWindow(window);
    if (winPtr) {
	Tcl_DoWhenIdle(DestroyWindowIdleProc, winPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandFramebufferSizeCallback --
 *
 *      Called when framebuffer size changes.  Note that this is always called
 *      when a window changes size, whether by interactive resizing with the
 *      mouse or programatic resizing with wm geometry. This generates a
 *      ConfigureNotify event for the window.  The subsequent
 *      WindowRefreshCallback generates ExposeNotify events for the window and
 *      all of its children.
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
TkWaylandFramebufferSizeCallback(
    GLFWwindow *window,
    int width,
    int height)
{
    
    /* Validate parameters. */
    if (width <= 0 || height <= 0) {
        fprintf(stderr, "FramebufferSizeCallback: invalid size %dx%d\n", width, height);
        return;
    }
    
    TkWindow *winPtr = TkWaylandGetTkWindow(window);
    if (!winPtr) {
        fprintf(stderr, "FramebufferSizeCallback: No Tk window!\n");
        return;
    }
    
    if (!winPtr->privatePtr) {
        fprintf(stderr, "FramebufferSizeCallback: privatePtr is NULL for %s\n", 
                Tk_PathName(winPtr));
        return;
    }
    
    fprintf(stderr, "TkWaylandFramebufferSizeCallback: %s %dx%d\n", 
            Tk_PathName(winPtr), width, height);
    
    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(window);
    if (!infoPtr) {
        fprintf(stderr, "FramebufferSizeCallback: infoPtr is NULL\n");
        return;
    }
    
    NVGcontext *vg = infoPtr->context.vg;
    if (vg == NULL) {
        fprintf(stderr, "FramebufferSizeCallback: No NVG context!\n");
        return;
    }
    
    /* Delete old FBO if it exists. */
    if (winPtr->privatePtr->fb) {
        nvgluDeleteFramebuffer(winPtr->privatePtr->fb);
        winPtr->privatePtr->fb = NULL;
    }
    
    /* Create new FBO with error checking. */
    winPtr->privatePtr->fb = nvgluCreateFramebuffer(vg, width, height, 0);
    if (!winPtr->privatePtr->fb) {
        fprintf(stderr, "FramebufferSizeCallback: Failed to create FBO\n");
        return;
    }
    
    /* Check FBO completeness. */
    nvgluBindFramebuffer(winPtr->privatePtr->fb);
    int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FBO incomplete (status=0x%x)\n", status);
        nvgluDeleteFramebuffer(winPtr->privatePtr->fb);
        winPtr->privatePtr->fb = NULL;
        return;
    }
    
    fprintf(stderr, "FBO created successfully: %dx%d\n", width, height);
    
    /* Update window size in Tk. */
    winPtr->changes.width = width;
    winPtr->changes.height = height;

    /*
     * Notify the menubar subsurface to recreate itself at the new width.
     * This must happen before TkDoConfigureNotify so that internalBorderTop
     * is already accurate when Tk re-lays out children in response to the
     * configure event.  MenubarResizeIdleProc defers the actual EGL
     * surface recreate to the next idle pass to avoid corrupting the GL
     * state mid-resize.
     */
    TkWaylandMenubarResize(winPtr);

    TkDoConfigureNotify(winPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWindowPosCallback --
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
TkWaylandWindowPosCallback(
    GLFWwindow *window,
    int xpos,
    int ypos)
{
    TkWindow *winPtr = TkWaylandGetTkWindow(window);
    if (!winPtr) {
	fprintf(stderr, "TkWaylandWindowPosCallback: no Tk window\n");
        return;
    }
    fprintf(stderr, "TkWaylandWindowPosCallback: %s -> to %d+%d\n",
	   Tk_PathName(winPtr), xpos, ypos);

    winPtr->changes.x = xpos;
    winPtr->changes.y = ypos;
    TkDoConfigureNotify(winPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWindowFocusCallback --
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
TkWaylandWindowFocusCallback(
    GLFWwindow *window,
    int focused)
{
    fprintf(stderr, "TkWaylandWindowFocusCallback\n");
    TkWindow *winPtr = TkWaylandGetTkWindow(window);
    XEvent event;
    
    if (!winPtr) {
        return;
    }

    if (focused) {
        /*
         * Create an IBus input context for this toplevel if one does not
         * exist yet.  This is the correct point to do it: the window is
         * mapped and the compositor has given it focus, so the IBus daemon
         * can associate the context with the correct surface.
         * TkWaylandIbusCreateContext is idempotent (returns TCL_OK if the
         * context already exists).
         */
        TkMainInfo *info = TkGetMainInfoList();
        if (info) {
            TkWaylandIbusCreateContext(info->interp, (Tk_Window)winPtr);
        }
        TkWaylandIbusFocusIn((Tk_Window)winPtr);
    } else {
        TkWaylandIbusFocusOut((Tk_Window)winPtr);
    }

    memset(&event, 0, sizeof(XEvent));
    event.type = focused ? FocusIn : FocusOut;
    event.xfocus.serial      = LastKnownRequestProcessed(winPtr->display)++;
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
 * TkWaylandWindowIconifyCallback --
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
TkWaylandWindowIconifyCallback(
    GLFWwindow *window,
    int iconified)
{
    TkWindow *winPtr = TkWaylandGetTkWindow(window);
    fprintf(stderr, "TkWaylandWindowIconifyCallback: %s\n", Tk_PathName(winPtr));
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
 * TkWaylandWindowMaximizeCallback --
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
TkWaylandWindowMaximizeCallback(
    GLFWwindow *window,
    int maximized)
{
    TkWindow *winPtr = TkWaylandGetTkWindow(window);
    
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
 * TkWaylandCursorEnterCallback --
 *
 *      Called by GLFW when the cursor enters or leaves the GLFW window
 *      client area.  Synthesizes an EnterNotify or LeaveNotify event
 *      targeted at the toplevel TkWindow so that Tk's generic cursor
 *      machinery (tkCursor.c) applies the correct cursor for the window
 *      being entered and resets it on leave.
 *
 *      This is distinct from the widget-level crossing logic in
 *      TkWaylandCursorPosCallback, which tracks transitions between child
 *      widgets while the pointer is already inside the GLFW window.
 *      This callback handles the coarser, compositor-level event that
 *      GLFW delivers when the pointer crosses the window border.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Queues an EnterNotify or LeaveNotify XEvent.
 *      Resets lastWinPtr to NULL on leave so that TkWaylandCursorPosCallback
 *      re-fires an EnterNotify for the correct child widget on re-entry.
 *
 *----------------------------------------------------------------------
 */

static void
TkWaylandCursorEnterCallback(
    GLFWwindow *window,
    int entered)		/* GLFW_TRUE if entered, GLFW_FALSE if left */
{
    TkWindow *winPtr = TkWaylandGetTkWindow(window);
    XEvent event;
    double xpos, ypos;
    int winX, winY;

    if (!winPtr) {
        return;
    }

    glfwGetCursorPos(window, &xpos, &ypos);
    glfwGetWindowPos(window, &winX, &winY);
    
    /*
     * Menubar intercept: the always-visible menubar strip is not part of
     * the popup stack and has no window of its own to receive motion
     * events, so it must be hit-tested explicitly here, regardless of
     * whether a menu popup is currently active.
     */
    TkWaylandMenubarHandleMotion(winPtr, (int)xpos, (int)ypos);

    /*
     * Menu intercept: while a popup menu stack is active, cursor enter/leave
     * events are forwarded to the menu hit-test logic. This ensures that
     * menu highlights update even if the cursor briefly leaves the GLFW
     * window and re-enters.
     */
    if (TkWaylandMenuPopupActive()) {
        TkWaylandMenuHandlePointerMotion((int)xpos, (int)ypos);
        /* Force immediate redraw of the menu to show highlight changes */
        TkWaylandMenuRedrawActive();
        
        /*
         * Even though we handled the menu event, still process Enter/Leave
         * events for Tk widgets below the menu so they don't get stuck.
         */
    }

    memset(&event, 0, sizeof(XEvent));
    event.type = entered ? EnterNotify : LeaveNotify;
    event.xcrossing.serial      = LastKnownRequestProcessed(winPtr->display)++;
    event.xcrossing.send_event  = False;
    event.xcrossing.display     = winPtr->display;
    event.xcrossing.window      = Tk_WindowId((Tk_Window)winPtr);
    event.xcrossing.root        = RootWindow(winPtr->display, winPtr->screenNum);
    event.xcrossing.subwindow   = None;
    event.xcrossing.time        = (Time)(glfwGetTime() * 1000.0);
    event.xcrossing.x           = (int)xpos;
    event.xcrossing.y           = (int)ypos;
    event.xcrossing.x_root      = winX + (int)xpos;
    event.xcrossing.y_root      = winY + (int)ypos;
    event.xcrossing.mode        = NotifyNormal;
    event.xcrossing.detail      = NotifyAncestor;
    event.xcrossing.same_screen = True;
    event.xcrossing.focus       = True;
    event.xcrossing.state       = glfwButtonState | glfwModifierState;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

    /*
     * On leave, clear lastWinPtr so TkWaylandCursorPosCallback generates a
     * fresh EnterNotify for the correct child widget when the pointer
     * re-enters, rather than suppressing it because lastWinPtr still
     * matches the stale target from before the pointer left.
     */
    if (!entered) {
        lastWinPtr = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCursorPosCallback --
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
TkWaylandCursorPosCallback(
    GLFWwindow *window,
    double xpos,
    double ypos)
{
    TkWindow *winPtr = TkWaylandGetTkWindow(window);
    XEvent event;

    /*
     * Menubar intercept: same reasoning as TkWaylandCursorEnterCallback --
     * the menubar strip needs hover updates regardless of whether a menu
     * popup is currently posted, and it isn't reachable through
     * TkWaylandMenuHandlePointerMotion() below (that only hit-tests
     * menuStack[] entries, and the menubar itself is never pushed onto
     * that stack).
     */
    TkWaylandMenubarHandleMotion(winPtr, (int)xpos, (int)ypos);

    /*
     * Menu intercept: while a popup menu stack is active all pointer motion
     * is forwarded to the menu hit-test logic.  The menu subsurfaces have no
     * GLFW window and receive no direct input, so this callback is their sole
     * source of motion events.
     */
    if (TkWaylandMenuPopupActive()) {
        TkWaylandMenuHandlePointerMotion((int)xpos, (int)ypos);
        /* Force immediate redraw of the menu to show highlight changes */
        TkWaylandMenuRedrawActive();
        return;
    }
    
    TkWindow *target = (TkWindow *) Tk_CoordsToWindow((int) xpos, (int) ypos,
			    (Tk_Window) winPtr);

    /* Check if mouse entered or left the target widget. */
    if (lastWinPtr != target) {
        if (lastWinPtr) {
	    memset(&event, 0, sizeof(XEvent));
	    event.type = LeaveNotify;
	    event.xcrossing.serial = LastKnownRequestProcessed(lastWinPtr->display)++;
	    event.xcrossing.send_event = False;
	    event.xcrossing.display = lastWinPtr->display;
	    event.xcrossing.window = Tk_WindowId((Tk_Window) lastWinPtr);
	    event.xcrossing.root = RootWindow(lastWinPtr->display, lastWinPtr->screenNum);
	    event.xcrossing.subwindow = None;
	    event.xcrossing.time = CurrentTime;
	    event.xcrossing.x = (int) xpos - Tk_X(lastWinPtr);
	    event.xcrossing.y = (int) ypos - Tk_Y(lastWinPtr);
	    event.xcrossing.x_root = (int) xpos;
	    event.xcrossing.y_root = (int) ypos;
	    event.xcrossing.mode = NotifyNormal;
	    event.xcrossing.detail = NotifyAncestor;
	    event.xcrossing.same_screen = True;
	    event.xcrossing.focus = False;
	    event.xcrossing.state = glfwButtonState | glfwModifierState;
	    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
        }

        /* Send EnterNotify for the newly entered widget. */
        memset(&event, 0, sizeof(XEvent));
        event.type = EnterNotify;
        event.xcrossing.serial = LastKnownRequestProcessed(winPtr->display)++;
        event.xcrossing.send_event = False;
        event.xcrossing.display = winPtr->display;
        event.xcrossing.window = Tk_WindowId((Tk_Window) target);
        event.xcrossing.root = RootWindow(target->display, target->screenNum);
        event.xcrossing.subwindow = None;
        event.xcrossing.time = CurrentTime;
        event.xcrossing.x = (int) xpos - Tk_X(target);
        event.xcrossing.y = (int) ypos - Tk_Y(target);
        event.xcrossing.x_root = (int) xpos;
        event.xcrossing.y_root = (int) ypos;
        event.xcrossing.mode = NotifyNormal;
        event.xcrossing.detail = NotifyAncestor;
        event.xcrossing.same_screen = True;
        event.xcrossing.focus = False;
        event.xcrossing.state = glfwButtonState | glfwModifierState;
        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

        lastWinPtr = target;

        /*
         * Update the pointer with root-relative coordinates so that
         * cursorWinPtr in tkPointer.c is set correctly. This single call,
         * after lastWinPtr is updated, uses root coords so that
         * XDefineCursor's guard condition (cursorWinPtr == winPtr) passes
         * when a cursor change is pending.
         */
        Tk_UpdatePointer((Tk_Window) target,
            (int) xpos, (int) ypos,
            glfwButtonState | glfwModifierState);
    }

    /* Generate MotionNotify event targeted at the widget under the cursor. */
    memset(&event, 0, sizeof(XEvent));
    event.type = MotionNotify;
    event.xmotion.serial = LastKnownRequestProcessed(winPtr->display)++;
    event.xmotion.send_event = False;
    event.xmotion.display = winPtr->display;
    event.xmotion.window = Tk_WindowId(target);
    event.xmotion.root = RootWindow(winPtr->display, winPtr->screenNum);
    event.xmotion.subwindow = None;
    event.xmotion.time = CurrentTime;
    event.xmotion.x = (int) xpos - Tk_X(target);
    event.xmotion.y = (int) ypos - Tk_Y(target);
    /*
     * The toplevel coordinates are the same as the root coordinates since
     * every toplevel is treated as having position (0, 0).
     */
    event.xmotion.x_root = (int) xpos;
    event.xmotion.y_root = (int) ypos;
    event.xmotion.state = glfwButtonState | glfwModifierState;
    event.xmotion.is_hint = NotifyNormal;
    event.xmotion.same_screen = True;
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandMouseButtonCallback --
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
TkWaylandMouseButtonCallback(
    GLFWwindow *window,
    int button,
    int action,
    int mods)
{
    TkWindow *winPtr = TkWaylandGetTkWindow(window);
    if (!winPtr) {
	return;
    }
    XEvent event;
    double xpos, ypos;
    unsigned int buttonMask = 0;
    unsigned int xbutton = Button1;

    if (!winPtr) {
        return;
    }

    /* Get cursor position. */
    glfwGetCursorPos(window, &xpos, &ypos);

    /*
     * Menubar intercept: a click on the menubar strip itself is handled
     * here first, *before* the popup-stack check below. The menubar is
     * always-visible toplevel chrome, not a posted popup, so it is never
     * part of the menu popup stack and TkWaylandMenuPopupActive() is false
     * the first time a user clicks e.g. "File" (no stack posted yet). If
     * we only checked TkWaylandMenuPopupActive() here, that first click
     * would fall straight through to Tk_CoordsToWindow() below and be
     * delivered as an ordinary click to whatever widget is underneath the
     * menubar, and no cascade would ever post.
     */
    if (action == GLFW_PRESS &&
        TkWaylandMenubarHandleClick(winPtr, (int)xpos, (int)ypos, 1)) {
        return;
    }

    /*
     * Menu intercept: while a popup menu stack is active, button presses
     * are routed exclusively to the menu hit-test / dismiss logic.
     * TkWaylandMenuHandlePointerButton only acts on press (not release);
     * releases are swallowed so they don't also activate a widget under
     * the now-dismissed stack.
     *
     * evdev button codes: left=0x110, right=0x111, middle=0x112.
     * wl_pointer delivers these directly; we map from GLFW here.
     */
    if (TkWaylandMenuPopupActive()) {
        if (action == GLFW_PRESS) {
            int evdevBtn = (button == GLFW_MOUSE_BUTTON_LEFT)   ? 0x110 :
                           (button == GLFW_MOUSE_BUTTON_RIGHT)  ? 0x111 : 0x112;
            TkWaylandMenuHandlePointerButton(
                (int)xpos, (int)ypos,
                evdevBtn,
                WL_POINTER_BUTTON_STATE_PRESSED);
            /* Force redraw after button click to update state */
            TkWaylandMenuRedrawActive();
        }
        /* Swallow both press and release — do not deliver to Tk widgets. */
        return;
    }
    Tk_Window target = Tk_CoordsToWindow((int) xpos, (int) ypos,
			    (Tk_Window) winPtr);

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
	/* Clicking on a widget should give it focus. */
	TkSetFocusWin((TkWindow *)target, 0);
    } else {
        glfwButtonState &= ~buttonMask;
        event.type = ButtonRelease;
    }

    event.xbutton.serial = LastKnownRequestProcessed(winPtr->display)++;
    event.xbutton.send_event = False;
    event.xbutton.display = winPtr->display;
    event.xbutton.window = Tk_WindowId(target);
    event.xbutton.root = RootWindow(winPtr->display, winPtr->screenNum);
    event.xbutton.subwindow = None;
    event.xbutton.time = CurrentTime;
    event.xbutton.x = (int)xpos - Tk_X(target);
    event.xbutton.y = (int)ypos - Tk_Y(target);
    event.xbutton.x_root = (int)xpos;
    event.xbutton.y_root = (int)ypos;
    event.xbutton.state = glfwButtonState | glfwModifierState;
    event.xbutton.button = xbutton;
    event.xbutton.same_screen = True;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandScrollCallback --
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
TkWaylandScrollCallback(
    GLFWwindow *window,
    double xoffset,
    double yoffset)
{
    TkWindow *winPtr = TkWaylandGetTkWindow(window);
    if (!winPtr) {
	return;
    }
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

    /* Generate button press. */
    memset(&event, 0, sizeof(XEvent));
    event.type = ButtonPress;
    event.xbutton.serial = LastKnownRequestProcessed(winPtr->display)++;
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
 * TkWaylandKeyCallback --
 *
 *      Called whenever a key is pressed or released.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates KeyPress/KeyRelease events + full menu keyboard navigation.
 *
 *----------------------------------------------------------------------
 */

static void
TkWaylandKeyCallback(GLFWwindow *window,
                     int key,
                     int scancode,
                     int action,
                     int mods)
{
    TkWindow *winPtr = TkWaylandGetTkWindow(window);
    if (!winPtr) return;

    TkWaylandUpdateKeyboardModifiers(mods);

    fprintf(stderr,
        "GLFW key=%d scancode=%d action=%d mods=%d\n",
        key, scancode, action, mods);

    uint32_t xkb_keycode = (uint32_t)(scancode + 8);
    KeySym keysym = xkb_state_key_get_one_sym(xkbState.state, xkb_keycode);

    char name[64];
    xkb_keysym_get_name(keysym, name, sizeof(name));
    fprintf(stderr, "keysym = 0x%lx (%s)\n",
            (unsigned long)keysym, name);
	
    /* IME handling. */
    if (action == GLFW_PRESS) {
        uint32_t keyval = (uint32_t)keysym;

        uint32_t state = 0;
        if (mods & GLFW_MOD_SHIFT)     state |= ShiftMask;
        if (mods & GLFW_MOD_CONTROL)   state |= ControlMask;
        if (mods & GLFW_MOD_ALT)       state |= Mod1Mask;
        if (mods & GLFW_MOD_SUPER)     state |= Mod4Mask;
        if (mods & GLFW_MOD_CAPS_LOCK) state |= LockMask;
        if (mods & GLFW_MOD_NUM_LOCK)  state |= Mod2Mask;

        /* IME must NOT swallow function keys. */
        if (!(keysym >= XKB_KEY_F1 && keysym <= XKB_KEY_F35)) {
            if (TkWaylandIbusProcessKey((Tk_Window)winPtr, keyval, xkb_keycode, state)) {
                return;
            }
        }
    }

    /* F10 - toggle or activate menubar. */
    if (action == GLFW_PRESS && keysym == XKB_KEY_F10) {
	if (!TkWaylandMenuActive()) {
	    TkWaylandMenubarActivateFirst(winPtr);
	 
	} else {
	    TkWaylandMenuDismissAll();
	}
	return;
    }
    /* Full keyboard navigation if menu active. */
    if (TkWaylandMenuActive()) {
        Tk_Window menuWin = TkWaylandMenuGetTopmostWindow();
        if (!menuWin) return;

        TkWindow *tkwin = (TkWindow *)menuWin;
        TkMenu *menuPtr = (TkMenu *)tkwin->instanceData;
        if (!menuPtr) return;

        int stackDepth = TkWaylandMenuGetDepth();
        bool isMenubar = (menuPtr->menuType == MENUBAR);

        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            /* Escape dismisses everything. */
            if (keysym == XKB_KEY_Escape) {
                TkWaylandMenuHandleEscape();
                return;
            }

            switch (keysym) {
            /* Up / down. */
	    case XKB_KEY_Up:
	    case XKB_KEY_KP_Up:
	    case XKB_KEY_Down:
	    case XKB_KEY_KP_Down: {
		int dir = (keysym == XKB_KEY_Up || keysym == XKB_KEY_KP_Up) ? -1 : 1;
		int current = menuPtr->active;
		int count = menuPtr->numEntries;
		int newIdx = current;

		for (int i = 1; i <= count; i++) {
		    int idx = (current + dir * i + count) % count;
		    TkMenuEntry *me = menuPtr->entries[idx];
		    if (me && me->type != SEPARATOR_ENTRY && me->state != ENTRY_DISABLED) {
			newIdx = idx;
			break;
		    }
		}

		if (newIdx != current) {
		    TkActivateMenuEntry(menuPtr, newIdx);
		    TkWaylandMenuRedrawActive();
		}
		break;
	    }

	     /* Left. */
	    case XKB_KEY_Left:
	    case XKB_KEY_KP_Left:
		if (stackDepth == 0) break;

		if (isMenubar) {
		    /* Pure menubar navigation. */
		    TkWaylandMenubarMove(winPtr, -1);
		} 
		else {
		    /* We are in a submenu. */
		    if (stackDepth > 1) {
			/* Go back one submenu level. */
			TkWaylandMenuPopToDepth(stackDepth - 1);
			TkWaylandMenuRedrawActive();
		    } else if (TkWaylandMenuStackRootIsMenubar()) {
			/*
			 * Top-level popup menu -> go back to the menubar.
			 * The menubar itself is never pushed onto menuStack[], so
			 * TkWaylandMenuGetParentWindow() (which only finds cascade-
			 * of-cascade parents at depth>=2) can't locate it here.
			 * winPtr is already the toplevel that owns this GLFW window
			 * and its menubar, so use it directly.
			 */
			TkWaylandMenubarMove(winPtr, -1);
		    } else {
			/*
			 * This chain is rooted at a menubutton (or a context menu),
			 * not the real menubar. Navigation here must stay
			 * self-contained -- do nothing rather than hand off to a
			 * menubar the popup was never part of.
			 */
		    }
		}
		break;

             /* Right. */
	    case XKB_KEY_Right:
	    case XKB_KEY_KP_Right:
		if (stackDepth == 0) break;

		if (isMenubar) {
		    TkWaylandMenubarMove(winPtr, +1);
		} 
		else {
		    TkMenuEntry *mePtr = (menuPtr->active >= 0) ? 
			menuPtr->entries[menuPtr->active] : NULL;

		    if (mePtr && mePtr->type == CASCADE_ENTRY && mePtr->namePtr) {
			/* Open submenu if possible. */
			TkWaylandMenuOpenCascade(menuPtr, mePtr);
		    } else if (stackDepth == 1 && TkWaylandMenuStackRootIsMenubar()) {
			/*
			 * In top-level menu, no cascade -> move to next menubar
			 * item. Same fix as Left: use winPtr directly instead of
			 * TkWaylandMenuGetParentWindow(), which returns NULL when
			 * only a top-level dropdown (depth==1) is posted.
			 *
			 * Gated on TkWaylandMenuStackRootIsMenubar(): if this chain
			 * is rooted at a menubutton (or context menu) instead of
			 * the real menubar, there is no "next menubar item" to go
			 * to -- stay self-contained and do nothing.
			 */
			TkWaylandMenubarMove(winPtr, +1);
		    }
		}
		break;

            /* Enter / space. */
	    case XKB_KEY_Return:
	    case XKB_KEY_KP_Enter:
	    case XKB_KEY_space:
		if (menuPtr->active >= 0) {
		    TkMenuEntry *mePtr = menuPtr->entries[menuPtr->active];
		    if (mePtr) {
			if (mePtr->type == CASCADE_ENTRY && mePtr->namePtr) {
			    TkWaylandMenuOpenCascade(menuPtr, mePtr);
			} else if (mePtr->type != SEPARATOR_ENTRY) {
			    TkInvokeMenu(menuPtr->interp, menuPtr, menuPtr->active);
			    TkWaylandMenuDismissAll();
			}
		    }
		}
		break;

	    default:
		/* Forward other keys to Tk. */
		{
		    XEvent event = {0};
		    event.type = KeyPress;
		    event.xkey.serial = LastKnownRequestProcessed(winPtr->display)++;
		    event.xkey.send_event = False;
		    event.xkey.display = winPtr->display;
		    event.xkey.window = Tk_WindowId(menuWin);
		    event.xkey.root = RootWindow(winPtr->display, winPtr->screenNum);
		    event.xkey.time = CurrentTime;
		    event.xkey.state = glfwModifierState;
		    event.xkey.keycode = (KeyCode)xkb_keycode;
		    event.xkey.same_screen = True;

		    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
		}
		break;
            }
        }
        return;
    }
    
    /* Normal Tk keypress events. */
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        TkWindow *focusWin = winPtr->dispPtr ? winPtr->dispPtr->focusPtr : winPtr;
        if (!focusWin) focusWin = winPtr;

        XEvent event;
        memset(&event, 0, sizeof(XEvent));
        event.type = KeyPress;
        event.xkey.serial      = LastKnownRequestProcessed(winPtr->display)++;
        event.xkey.send_event  = False;
        event.xkey.display     = winPtr->display;
        event.xkey.window      = Tk_WindowId((Tk_Window)focusWin);
        event.xkey.root        = RootWindow(winPtr->display, winPtr->screenNum);
        event.xkey.time        = CurrentTime;

        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        event.xkey.x           = (int)xpos;
        event.xkey.y           = (int)ypos;
        event.xkey.x_root      = winPtr->changes.x + (int)xpos;
        event.xkey.y_root      = winPtr->changes.y + (int)ypos;
        event.xkey.state       = glfwModifierState;
        event.xkey.keycode     = (KeyCode)xkb_keycode;
        event.xkey.same_screen = True;

        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    }

	/* Normal Tk keyrelease events. */
    if (action == GLFW_RELEASE) {
        TkWindow *focusWin = winPtr->dispPtr ? winPtr->dispPtr->focusPtr : winPtr;
        if (!focusWin) focusWin = winPtr;

        XEvent event;
        memset(&event, 0, sizeof(XEvent));
        event.type = KeyRelease;
        event.xkey.serial      = LastKnownRequestProcessed(winPtr->display)++;
        event.xkey.send_event  = False;
        event.xkey.display     = winPtr->display;
        event.xkey.window      = Tk_WindowId((Tk_Window)focusWin);
        event.xkey.root        = RootWindow(winPtr->display, winPtr->screenNum);
        event.xkey.time        = CurrentTime;

        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        event.xkey.x           = (int)xpos;
        event.xkey.y           = (int)ypos;
        event.xkey.x_root      = winPtr->changes.x + (int)xpos;
        event.xkey.y_root      = winPtr->changes.y + (int)ypos;
        event.xkey.state       = glfwModifierState;
        event.xkey.keycode     = (KeyCode)xkb_keycode;
        event.xkey.same_screen = True;

        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCharCallback --
 *
 *      Called with a 32-bit unicode codepoint when a composition
 *      sequence has been completed and produced a unicode codepoint.
 *      (This is not called for dead keys, or action keys like Return
 *      or Backspace, or modifiers.)
 *
 *      The codepoint is passed to TkWaylandStoreText to be converted
 *      to UTF-8 and stored for use by TkpGetString.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Stores codepoint for the next TkpGetString() call.
 *
 *----------------------------------------------------------------------
 */

static void
TkWaylandCharCallback(GLFWwindow *window, unsigned int codepoint)
{
    TkWindow *winPtr = TkWaylandGetTkWindow(window);
    if (!winPtr) return;

    /* Do not store text if a menu is active. */
    if (TkWaylandMenuActive()) {
        return;
    }

    /* Skip if IBus is likely handling composition. */
    if (xkbState.state) {
        /* Optional: check if any compose state is active, or just always let IBus win. */
        fprintf(stderr, "CharCallback: codepoint U+%04X (may be ignored if IBus active)\n", codepoint);
    }

    TkWaylandStoreText(winPtr, codepoint);
}


/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWindowRefreshCallback --
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
TkWaylandWindowRefreshCallback(GLFWwindow *window)
{
    TkWindow *winPtr = TkWaylandGetTkWindow(window);
    if (!winPtr) {
	return;
    }
    fprintf(stderr, "TkGlWindowRefreshCallback Exposing %s\n",
	    Tk_PathName(winPtr));
	    	
    TkWaylandQueueExposeEvent(winPtr,
        0, 0, Tk_Width(winPtr), Tk_Height(winPtr));
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
    ////uint64_t u = 1;
    
    if (tsdPtr->initialized && !tsdPtr->shutdownInProgress) {
	//// This should post an empty event to GLFW!!!
        ////write(tsdPtr->wakeupFd, &u, sizeof(u));
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
