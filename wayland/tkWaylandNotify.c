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
#include <xkbcommon/xkbcommon.h>
#include <GLFW/glfw3.h>
//#include <sys/eventfd.h>
#include <unistd.h>
#include <errno.h>
#include <GLES3/gl3.h>
#include "nanovg_gl_utils.h"

/*
 * Forward declarations for IBus integration (implemented in tkWaylandKey.c).
 * These wrappers accept Tk_Window so this file never needs to see IbusContext*.
 */
extern int  TkWaylandIbusCreateContext(Tcl_Interp *interp, Tk_Window tkwin);
extern void TkWaylandIbusFocusIn(Tk_Window tkwin);
extern void TkWaylandIbusFocusOut(Tk_Window tkwin);
extern int  TkWaylandIbusProcessKey(Tk_Window tkwin, uint32_t keyval,
                                    uint32_t keycode, uint32_t state);
extern void RemoveIbusContext(Tk_Window tkwin);
extern TkXKBState xkbState;


/*
 * Direct reference to the IBus bus so the notifier can drain it without
 * going through the file-handler path.  The bus is private to
 * tkWaylandKey.c; we declare it extern here rather than exposing it in a
 * header because only the notifier needs it.
 */
#include <systemd/sd-bus.h>
extern sd_bus *ibus_bus;      /* defined in tkWaylandKey.c */
                                   


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

/* Track last window and position for enter/leave events */
static TkWindow *lastWinPtr = NULL;
//static double lastX = -1, lastY = -1;
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
TkWaylandSetupProc(TCL_UNUSED(void *), /* clientData */
		   int flags)
{
    TSD_INIT();

    /*
     * Event source contract: if TCL_WINDOW_EVENTS is not set, do nothing.
     * Do NOT call Tcl_SetMaxBlockTime in that case.
     */
    if (!(flags & TCL_WINDOW_EVENTS)) return;

    Tcl_Time noBlock    = {0, 0};
    Tcl_Time oneRefresh = {0, 16667}; /* ~16 ms, one display frame */

    /*
     * Swap GL buffers for any window that finished drawing.
     */
    TkWaylandDisplayAllWindows();

    /*
     * Drain any pending IBus D-Bus messages directly here.
     *
     * We cannot rely on the Tcl file handler for the IBus fd because
     * this notifier is almost always busy: continuous expose/redraw
     * events keep callbackCount nonzero so Tcl_SetMaxBlockTime is set
     * to {0,0}, Tcl_WaitForEvent never actually blocks, and the file
     * handler registered via Tcl_CreateFileHandler never fires.
     *
     * Draining here is safe: sd_bus_process is non-blocking when there
     * is nothing to read, and the signal callbacks (OnCommitText,
     * OnUpdatePreedit) only call Tk_QueueWindowEvent / Tk_SendVirtualEvent,
     * which is legal from any context.
     */
    if (ibus_bus) {
        while (sd_bus_process(ibus_bus, NULL) > 0) { /* drain */ }
    }

    /*
     * Poll GLFW for new events.  If any callback fired, don't block so
     * those events are processed immediately.  Otherwise allow a short
     * sleep so the CPU isn't pegged at 100 % between frames.
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
TkWaylandCheckProc(TCL_UNUSED(void *),
                   int flags)
{
    if (!(flags & TCL_WINDOW_EVENTS)) {
        return;
    }
    
    if (ibus_bus) {
        int r;

        /* Check if there are bytes waiting on the socket. 
         * Passing 0 timeout makes this completely non-blocking for the Tcl loop,
         * but forces sd-bus to look at the kernel socket buffer and read 
         * incoming 'CommitText' or 'UpdatePreedit' bytes into internal memory.
         */
        sd_bus_wait(ibus_bus, 0);

        /* Process and dispatch the read signals to your callbacks.
         * Loop until all cached messages have been routed to OnCommitText/OnUpdatePreedit.
         */
        do {
            r = sd_bus_process(ibus_bus, NULL);
        } while (r > 0);
    }

    glfwPollEvents();
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
    event.xexpose.count = 0;
    
    /* Queue it. */
    fprintf(stderr, "Queuing Expose(%lu) for %s with count %d\n",
	   event.xexpose.serial,
	   Tk_PathName(winPtr),
	   event.xexpose.count);
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    
    /* Recurse through the children of this window. */
    for (childPtr = winPtr->childList; childPtr != NULL;
         childPtr = childPtr->nextPtr) {
        if (!Tk_IsMapped(childPtr) || Tk_IsTopLevel(childPtr)) {
            continue;
        }
        TkWaylandQueueExposeEvent(childPtr, 0, 0, Tk_Width(childPtr),
				  Tk_Height(childPtr));
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
static void TkGlfwWindowContentScaleCallback(GLFWwindow *window,
					     float xscale, float yscale);
static void TkGlfwFramebufferSizeCallback(GLFWwindow *window,
					  int width, int height);
static void TkGlfwWindowPosCallback(GLFWwindow *window, int xpos, int ypos);
static void TkGlfwWindowFocusCallback(GLFWwindow *window, int focused);
static void TkGlfwWindowIconifyCallback(GLFWwindow *window, int iconified);
static void TkGlfwWindowMaximizeCallback(GLFWwindow *window, int maximized);
static void TkGlfwCursorPosCallback(GLFWwindow *window,
				    double xpos, double ypos);
static void TkGlfwMouseButtonCallback(GLFWwindow *window,
				      int button, int action, int mods);
static void TkGlfwScrollCallback(GLFWwindow *window,
				 double xoffset, double yoffset);
static void TkGlfwKeyCallback(GLFWwindow *window, int key,
			      int scancode, int action, int mods);
static void TkGlfwCharCallback(GLFWwindow *window, unsigned int codepoint);
static void TkGlfwWindowRefreshCallback(GLFWwindow *window);
static void TkGlfwCursorEnterCallback(GLFWwindow *window, int entered);

MODULE_SCOPE void
TkGlfwSetupCallbacks(
    GLFWwindow *glfwWindow)
{
    glfwSetWindowCloseCallback     (glfwWindow, TkGlfwWindowCloseCallback);
    glfwSetWindowContentScaleCallback (glfwWindow,
				       TkGlfwWindowContentScaleCallback);
    glfwSetFramebufferSizeCallback (glfwWindow, TkGlfwFramebufferSizeCallback);
    glfwSetWindowPosCallback       (glfwWindow, TkGlfwWindowPosCallback);
    glfwSetWindowFocusCallback     (glfwWindow, TkGlfwWindowFocusCallback);
    glfwSetWindowIconifyCallback   (glfwWindow, TkGlfwWindowIconifyCallback);
    glfwSetWindowMaximizeCallback  (glfwWindow, TkGlfwWindowMaximizeCallback);
    glfwSetCursorPosCallback       (glfwWindow, TkGlfwCursorPosCallback);
    glfwSetCursorEnterCallback     (glfwWindow, TkGlfwCursorEnterCallback);
    glfwSetMouseButtonCallback     (glfwWindow, TkGlfwMouseButtonCallback);
    glfwSetScrollCallback          (glfwWindow, TkGlfwScrollCallback);
    glfwSetWindowRefreshCallback   (glfwWindow, TkGlfwWindowRefreshCallback);
    glfwSetKeyCallback             (glfwWindow, TkGlfwKeyCallback);
    glfwSetCharCallback            (glfwWindow, TkGlfwCharCallback);
}

MODULE_SCOPE void
TkGlfwClearCallbacks(
    GLFWwindow *glfwWindow)
{
    glfwSetWindowCloseCallback        (glfwWindow, NULL);
    glfwSetWindowContentScaleCallback (glfwWindow, NULL);
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
 
/* Helper function to avoid type mismatch in callback. */
static void DestroyWindowIdleProc(void *clientData)
{
	Tk_Window tkwin =  (Tk_Window)clientData;
	if (Tk_IsTopLevel(tkwin)) {
        RemoveIbusContext(tkwin);
    }
    Tk_DestroyWindow(tkwin);
}
 
static void
TkGlfwWindowCloseCallback(GLFWwindow *window)
{
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    recordCallback();

    if (winPtr) {
	Tcl_DoWhenIdle(DestroyWindowIdleProc, winPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwWindowContentScaleCallback --
 *
 *      Called when window is moved between monitors with different
 *      pixel densities.  This updates the pixelRatio stored in
 *      the Tk Window struct.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates the pixelRatio field in the gflwData of the
 *      associated Tk window.
 *
 *----------------------------------------------------------------------
 */

static void
TkGlfwWindowContentScaleCallback(
    GLFWwindow *window,
    float xscale,
    TCL_UNUSED(float)) /*yscale*/
{
    recordCallback();
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    if (winPtr && winPtr->privatePtr) {
	winPtr->privatePtr->pixelRatio = xscale;
    }
    
    fprintf(stderr, "TkGlfWindowContentScaleCallback: set pixelRatio to %f\n",
	   winPtr->privatePtr->pixelRatio);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwFramebufferSizeCallback --
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
TkGlfwFramebufferSizeCallback(
    GLFWwindow *window,
    int width,
    int height)
{
    recordCallback();
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    if (!winPtr) {
	fprintf(stderr, "============================ No Tk window!\n");
	return;
    }
    fprintf(stderr, "TkGlfwFramebufferSizeCallback: %s\n", Tk_PathName(winPtr));
    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(window);

    /*
     * This is a workaround for a Wayland/Mesa bug. (see
     * https://github.com/alacritty/alacritty/issues/6069 and
     * https://github.com/servo/servo/issues/43050.)
     *
     * When this callback is called, GLFW has requested and been granted a new
     * framebuffer of the given size from wayland, but the Mesa driver has not
     * actually allocated it yet.  If the size change was requested by a
     * geometry manager the window will be reconfigured and redrawn as soon as
     * possible.  The drawing will be done assuming the new window size
     * reported in the callback arguments, but the driver will still be using
     * the old framebuffer.  This causes serious artifacts.  At the expense of
     * creating minor artifacts, we can force the new framebuffer to be
     * allocated by triggering an immediate display of the window using its
     * old backing store framebuffer.  This is not needed when the window is
     * being resized with the mouse.  The sizeChanged flag is used to
     * distinguish the cases when the workaround is needed from those when it
     * is not.
     */

    if (infoPtr->flags & sizeChanged) {
	/* Mark this window as needing display and display all windows. */
	infoPtr->flags |= needsDisplay;
	TkWaylandDisplayAllWindows();
	infoPtr->flags &= ~sizeChanged;
    }

    float pixelRatio = winPtr->privatePtr->pixelRatio;
    
    NVGcontext *vg = infoPtr->context.vg;
    if (vg == NULL) {
	fprintf(stderr, "============================ No Context!\n");
	return;
    }

    /* Rebuild the backing store FBO. */
    nvgluDeleteFramebuffer(winPtr->privatePtr->fb);
    winPtr->privatePtr->fb = nvgluCreateFramebuffer(vg, width, height, 0);
    fprintf(stderr, "New framebuffer %p for %s with id %d\n", winPtr->privatePtr->fb,
	   Tk_PathName(winPtr), winPtr->privatePtr->fb->fbo);

#if 0
    /* Check for FBO completeness. */
    nvgluBindFramebuffer(winPtr->privatePtr->fb);
    int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FBO %p is incomplete (status=0x%x)\n", winPtr->privatePtr->fb,
	       status);
    } else {
	fprintf(stderr, "FBO is complete.\n");
    }
#endif

    /* Inform Tk about the size change, taking into account the
     * window's current pixel ratio.
     */
    
    winPtr->changes.width = (int) (((float) width) / pixelRatio);
    winPtr->changes.height = (int) (((float) height) / pixelRatio);

    /* Reconfigure the Tk window. */
    TkDoConfigureNotify(winPtr);
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
	fprintf(stderr, "TkGlfwWindowPosCallback: no Tk window\n");
        return;
    }
    fprintf(stderr, "TkGlfwWindowPosCallback: %s -> to %d+%d\n",
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
 *      Generates FocusIn/FocusOut events and manages IBus context.
 *
 *----------------------------------------------------------------------
 */
 
static void
TkGlfwWindowFocusCallback(GLFWwindow *window, int focused)
{
    recordCallback();
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent event;
    
    if (!winPtr) {
        return;
    }

    fprintf(stderr, "TkGlfwWindowFocusCallback: %s %s\n",
            Tk_PathName(winPtr), focused ? "FocusIn" : "FocusOut");

    /* Initialize the entire event lifecycle structure */
    memset(&event, 0, sizeof(XEvent));
    
    /* Base XAny Lifecycle Setup */
    event.xany.type        = focused ? FocusIn : FocusOut;
    event.xany.serial      = LastKnownRequestProcessed(winPtr->display)++;
    event.xany.send_event  = False;
    event.xany.display     = winPtr->display;
    event.xany.window      = Tk_WindowId((Tk_Window)winPtr);

    /* Specific XEvent lifecycle setup. */
    event.xfocus.type      = event.xany.type;
    event.xfocus.serial    = event.xany.serial;
    event.xfocus.send_event = event.xany.send_event;
    event.xfocus.display   = event.xany.display;
    event.xfocus.window    = event.xany.window;
    event.xfocus.mode      = NotifyNormal;
    event.xfocus.detail    = NotifyAncestor;

    /* Push the fully formed lifecycle event to the tail of the event queue */
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    TkGenerateActivateEvents(winPtr, focused);

    /* IBus IME integration. */
    if (focused) {
        TkMainInfo *infoPtr = TkGetMainInfoList();
        if (infoPtr) {
            TkWaylandIbusCreateContext(infoPtr->interp, (Tk_Window)winPtr);
        }
        TkWaylandIbusFocusIn((Tk_Window)winPtr);
        /*
         * Drain the IBus bus immediately after FocusIn + Enable so any
         * engine-activation signals are processed before the first keypress.
         * Without this drain the signals sit in the socket until the next
         * TkWaylandSetupProc pass, by which time a fast typist has already
         * sent keys that bypass IBus.
         */
        if (ibus_bus) {
            while (sd_bus_process(ibus_bus, NULL) > 0) { /* drain */ }
        }
    } else {
        TkWaylandIbusFocusOut((Tk_Window)winPtr);
    }
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
    fprintf(stderr, "TkGlfwWindowIconifyCallback: %s\n", Tk_PathName(winPtr));
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
 * TkGlfwCursorEnterCallback --
 *
 *      Called by GLFW when the cursor enters or leaves the GLFW window
 *      client area.  Synthesizes an EnterNotify or LeaveNotify event
 *      targeted at the toplevel TkWindow so that Tk's generic cursor
 *      machinery (tkCursor.c) applies the correct cursor for the window
 *      being entered and resets it on leave.
 *
 *      This is distinct from the widget-level crossing logic in
 *      TkGlfwCursorPosCallback, which tracks transitions between child
 *      widgets while the pointer is already inside the GLFW window.
 *      This callback handles the coarser, compositor-level event that
 *      GLFW delivers when the pointer crosses the window border.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Queues an EnterNotify or LeaveNotify XEvent.
 *      Resets lastWinPtr to NULL on leave so that TkGlfwCursorPosCallback
 *      re-fires an EnterNotify for the correct child widget on re-entry.
 *
 *----------------------------------------------------------------------
 */

static void
TkGlfwCursorEnterCallback(
    GLFWwindow *window,
    int entered)		/* GLFW_TRUE if entered, GLFW_FALSE if left */
{
    recordCallback();
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    XEvent event;
    double xpos, ypos;
    int winX, winY;

    if (!winPtr) {
        return;
    }

    glfwGetCursorPos(window, &xpos, &ypos);
    glfwGetWindowPos(window, &winX, &winY);

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
     * On leave, clear lastWinPtr so TkGlfwCursorPosCallback generates a
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

    /* Find the widget containing the mouse cursor. */
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

    /* Find the widget where the event occurred. */
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
 * TkGlfwKeyCallback --
 *
 *      Called whenever a key is pressed or released.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates KeyPress/KeyRelease events. Gives IBus first chance
 *      to handle the key for IME composition.
 *
 *----------------------------------------------------------------------
 */

static void
TkGlfwKeyCallback(GLFWwindow *window,
                  int key,           /* keep this parameter */
                  int scancode,
                  int action,
                  int mods)
{
    recordCallback();
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    if (!winPtr) return;

    TkWaylandUpdateKeyboardModifiers(mods);

    /* Route to focused widget (important for text widgets) */
    TkWindow *focusWin = winPtr->dispPtr ? winPtr->dispPtr->focusPtr : winPtr;
    if (!focusWin) focusWin = winPtr;

    fprintf(stderr, "KeyCallback: scancode=%d key=%d action=%s mods=0x%x\n",
            scancode, key, 
            (action == GLFW_PRESS) ? "PRESS" : 
            (action == GLFW_REPEAT) ? "REPEAT" : "RELEASE", 
            mods);

    /* IBus IME handling. */
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        uint32_t xkb_keycode = (uint32_t)(scancode + 8);
        
        /* Get keysym using the *live* XKB state — this is more reliable */
        uint32_t keyval = (uint32_t) xkb_state_key_get_one_sym(
                                xkbState.state, xkb_keycode);

        uint32_t state = 0;
        if (mods & GLFW_MOD_SHIFT)     state |= ShiftMask;
        if (mods & GLFW_MOD_CONTROL)   state |= ControlMask;
        if (mods & GLFW_MOD_ALT)       state |= Mod1Mask;
        if (mods & GLFW_MOD_SUPER)     state |= Mod4Mask;
        if (mods & GLFW_MOD_CAPS_LOCK) state |= LockMask;
        if (mods & GLFW_MOD_NUM_LOCK)  state |= Mod2Mask;

        fprintf(stderr, "  → IBus: keyval=0x%04x keycode=%u state=0x%x\n",
                keyval, xkb_keycode, state);

        /*
         * Pass winPtr (the GLFW toplevel) to TkWaylandIbusProcessKey, NOT
         * focusWin.  FindContext compares ctx->tkwin by pointer; ctx->tkwin
         * was stored from the winPtr passed to TkWaylandIbusCreateContext in
         * TkGlfwWindowFocusCallback, which is always the GLFW toplevel.
         * Using focusWin (.t) causes GetToplevelOfWidget to walk the parent
         * chain, which may differ after a resize/remap cycle, returning a
         * pointer that does not match ctx->tkwin → FindContext returns NULL →
         * IBus is silently bypassed for every keypress.
         */
        if (TkWaylandIbusProcessKey((Tk_Window)winPtr, keyval, xkb_keycode, state)) {
            fprintf(stderr, "  → IBus HANDLED key (composition active)\n");
            return;                    /* Do NOT generate normal Tk Key event */
        }
    }

    /*  Normal Tk key event. */
    if (action == GLFW_RELEASE) {
        /* Optional: you can also forward releases, but many IMEs ignore them */
        return;
    }

    XEvent event;
    memset(&event, 0, sizeof(XEvent));
    event.type = KeyPress;   /* GLFW_RELEASE is mostly ignored for text */
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
    event.xkey.keycode     = (KeyCode)scancode;   /* raw evdev scancode */
    event.xkey.same_screen = True;

    fprintf(stderr, "  → Queuing normal Tk KeyPress (scancode %d)\n", scancode);
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwCharCallback --
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
TkGlfwCharCallback(GLFWwindow *window, unsigned int codepoint)
{
    recordCallback();
    TkWindow *winPtr = TkGlfwGetTkWindow(window);
    if (!winPtr) return;

    /* Skip if IBus is likely handling composition */
    if (xkbState.state) {
        /* Optional: check if any compose state is active, or just always let IBus win */
        fprintf(stderr, "CharCallback: codepoint U+%04X (may be ignored if IBus active)\n", codepoint);
    }

    TkWaylandStoreText(winPtr, codepoint);
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
    if (!winPtr) {
	return;
    }
    fprintf(stderr, "TkGlWindowRefreshCallback Expose\n");
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
