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

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <errno.h>

extern TkGlfwContext  glfwContext;
extern WindowMapping *windowMappingList;

typedef struct ThreadSpecificData {
    bool           initialized;
    bool           waylandInitialized;
    int            wakeupFd;           /* eventfd for waking up GLFW polling */
    Tcl_FileProc   *watchProc;         /* stored for cleanup */
    Tcl_TimerToken heartbeatTimer;     /* fallback timer */
    bool           renderPending;
    bool           shutdownInProgress; /* flag to prevent recursive shutdown */
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

#define TSD_INIT() ThreadSpecificData *tsdPtr = (ThreadSpecificData *) \
    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData))

static void TkWaylandNotifyExitHandler(void *clientData);
static void TkWaylandEventsSetupProc(void *clientData, int flags);
static void TkWaylandEventsCheckProc(void *clientData, int flags);
static void HeartbeatTimerProc(void *clientData);
static void TkWaylandRenderIdleProc(void *clientData);
static void TkWaylandSwapIdleProc(void *clientData);
static void TkWaylandWakeupFileProc(void *clientData, int mask);
static int  TkWaylandExposeEventProc(Tcl_Event *evPtr, int flags);
static void TkWaylandCheckForWindowClosure(void);

void TkWaylandScheduleRender(void);

#define HEARTBEAT_INTERVAL 16   /* ms - fallback when file events not working */

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
 *      Installs TkWaylandEventsSetupProc and TkWaylandEventsCheckProc
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
        /* Create eventfd for waking up the GLFW poll. */
        tsdPtr->wakeupFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (tsdPtr->wakeupFd == -1) {
            fprintf(stderr, "TkWaylandNotify: Failed to create eventfd: %s\n",
                    strerror(errno));
            /* Fall back to timer-only mode. */
            tsdPtr->wakeupFd = -1;
        }

        tsdPtr->initialized   = true;
        tsdPtr->renderPending = false;
        tsdPtr->shutdownInProgress = false;

        /* Create the Tcl event source. */
        Tcl_CreateEventSource(TkWaylandEventsSetupProc,
                              TkWaylandEventsCheckProc, NULL);

        /* Create fallback timer. */
        tsdPtr->heartbeatTimer = Tcl_CreateTimerHandler(HEARTBEAT_INTERVAL,
                                                        HeartbeatTimerProc,
                                                        NULL);

        /* If we have a wakeup fd, create a file handler for it. */
        if (tsdPtr->wakeupFd != -1) {
            Tcl_CreateFileHandler(tsdPtr->wakeupFd, TCL_READABLE,
                                  TkWaylandWakeupFileProc, NULL);
        }

        TkCreateExitHandler(TkWaylandNotifyExitHandler, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWakeupFileProc --
 *
 *      Called when the wakeup file descriptor becomes readable.
 *      This happens when glfwPostEmptyEvent() is called from another
 *      thread or when we need to wake up the GLFW poll.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Reads from the eventfd to clear it, then triggers GLFW event
 *      processing.
 *
 *----------------------------------------------------------------------
 */

static void
TkWaylandWakeupFileProc(TCL_UNUSED(void *),
			TCL_UNUSED(int)) /* mask */
{
    TSD_INIT();
    uint64_t u;
    
    if (tsdPtr->wakeupFd == -1) return;
    
    /* Read and discard the eventfd value. */
    if (read(tsdPtr->wakeupFd, &u, sizeof(u)) != sizeof(u)) {
        /* Ignore errors - just clear the fd. */
    }
    
    /* Process any pending GLFW events. */
    if (glfwContext.initialized && !tsdPtr->shutdownInProgress) {
        glfwPollEvents();
        TkWaylandCheckForWindowClosure();
        TkWaylandScheduleRender();
    }
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
        tsdPtr->shutdownInProgress = true;
        
        /* Schedule cleanup as idle callback. */
        Tcl_DoWhenIdle(TkWaylandNotifyExitHandler, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandScheduleRender --
 *
 *      Schedules redraws of widgets at idle events.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Redraws scheduled.
 *
 *----------------------------------------------------------------------
 */
 
void
TkWaylandScheduleRender(void)
{
    TSD_INIT();

    if (!tsdPtr->renderPending && tsdPtr->initialized && 
        !tsdPtr->shutdownInProgress) {
        tsdPtr->renderPending = true;
        Tcl_DoWhenIdle(TkWaylandRenderIdleProc, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandRenderIdleProc --
 *
 *      Runs once per Tcl idle cycle. Sets clearPending on every mapped
 *      window so the first BeginDraw this tick clears the framebuffer,
 *      then drains window events so widget display procedures run.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Widget display procedures run.
 *
 *----------------------------------------------------------------------
 */
 
static void
TkWaylandRenderIdleProc(TCL_UNUSED(void *))
{
    WindowMapping *m;
    TkGlfwContext *ctx = TkGlfwGetContext();
    TSD_INIT();

    if (!ctx || !ctx->initialized || tsdPtr->shutdownInProgress) return;

    WindowMapping *list = TkGlfwGetMappingList();
    if (!list) return;

    /* Prime per-window clear flag. */
    for (m = list; m != NULL; m = m->nextPtr) {
        if (m->glfwWindow && m->width > 1 && m->height > 1) {
            m->clearPending = 1;
        }
    }
    
    /* Process widget display procedures. */
    Tcl_DoOneEvent(TCL_WINDOW_EVENTS | TCL_ALL_EVENTS | TCL_DONT_WAIT);

    /* Reset renderPending ONLY AFTER event processing is complete. */
    tsdPtr->renderPending = false;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSwapIdleProc --
 *
 *      Swaps GLFW buffers, a key part of drawing. 
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Buffers swapped and drawing can take place. 
 *
 *----------------------------------------------------------------------
 */
 
static void
TkWaylandSwapIdleProc(void *clientData)
{
    WindowMapping *m = (WindowMapping *)clientData;
    TSD_INIT();

    if (!m || !m->glfwWindow || !m->swapPending || tsdPtr->shutdownInProgress) 
        return;
        
    m->swapPending = 0;
    glfwMakeContextCurrent(m->glfwWindow);
    glfwSwapBuffers(m->glfwWindow);
    
    /* Restore context to main window if it exists. */
    if (glfwContext.mainWindow && glfwContext.initialized) {
        glfwMakeContextCurrent(glfwContext.mainWindow);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandScheduleSwap--
 *
 *      Schedules a GLFW buffer swap to run as an idle handler. 
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Process runs. 
 *
 *----------------------------------------------------------------------
 */
 
MODULE_SCOPE void
TkWaylandScheduleSwap(WindowMapping *m)
{
    if (m && m->swapPending) {
        Tcl_DoWhenIdle(TkWaylandSwapIdleProc, m);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * HeartbeatTimerProc --
 *
 *      Periodic timer to keep the event loop responsive as a fallback.
 *      Reschedules itself and pumps GLFW events. Also checks for window
 *      closure.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates a new timer handler for the next heartbeat. Calls
 *      glfwPollEvents() to process pending Wayland/GLFW events.
 *
 *----------------------------------------------------------------------
 */
 
static void
HeartbeatTimerProc(TCL_UNUSED(void *))
{
    TSD_INIT();

    if (!tsdPtr->initialized || tsdPtr->shutdownInProgress) {
        return;
    }

    /* If there are no windows left, start shutdown. */
    if (Tk_GetNumMainWindows() == 0) {
        tsdPtr->shutdownInProgress = true;
        Tcl_DoWhenIdle(TkWaylandNotifyExitHandler, NULL);
        return;
    }
    
    /* Poll GLFW events. */
    if (glfwContext.initialized) {
        glfwPollEvents();
        TkWaylandScheduleRender();
    }
    
    /* Reschedule timer if still initialized and not shutting down. */
    if (tsdPtr->initialized && !tsdPtr->shutdownInProgress) {
        tsdPtr->heartbeatTimer = Tcl_CreateTimerHandler(HEARTBEAT_INTERVAL,
                                                        HeartbeatTimerProc,
                                                        NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandEventsSetupProc --
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
TkWaylandEventsSetupProc(TCL_UNUSED(void *),
			 int flags)
{
    TSD_INIT();
    Tcl_Time blockTime = {0, 0};
    
    if (tsdPtr->shutdownInProgress) {
        /* Don't block during shutdown. */
        Tcl_SetMaxBlockTime(&blockTime);
        return;
    }
    
    /* If we have a wakeup fd, we can block indefinitely since we'll be woken. */
    if (tsdPtr->wakeupFd != -1 && glfwContext.initialized) {
        /* Check if GLFW has pending events. */
        if (glfwContext.initialized) {
            /* Let Tcl block - we'll wake up via file event. */
            return;
        }
    }
    
    /* Otherwise use timer-based approach. */
    if (!(flags & TCL_WINDOW_EVENTS)) {
        /* Not interested in window events - use timer. */
        blockTime.sec = 0;
        blockTime.usec = HEARTBEAT_INTERVAL * 1000;
        Tcl_SetMaxBlockTime(&blockTime);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandEventsCheckProc --
 *
 *      Process pending Wayland/GLFW events and queue Tk events.
 *      Called by Tcl_DoOneEvent after events are posted.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Calls TkWaylandScheduleRender() to process window events when window
 *      events are requested.
 *
 *----------------------------------------------------------------------
 */
 
static void
TkWaylandEventsCheckProc(TCL_UNUSED(void *), int flags) 
{
    TSD_INIT();
    
    if (tsdPtr->shutdownInProgress) return;
    
    if (!(flags & TCL_WINDOW_EVENTS)) return;
    if (!glfwContext.initialized)    return;
    
    /* Polling may have been done by file handler or timer. */
    TkWaylandScheduleRender();
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
    if (tsdPtr->shutdownInProgress && tsdPtr->shutdownInProgress != 2) {
        tsdPtr->shutdownInProgress = 2; /* Mark as final shutdown. */
    }

    /* Cancel all pending idle callbacks */
    Tcl_CancelIdleCall(TkWaylandRenderIdleProc, NULL);
    Tcl_CancelIdleCall(TkWaylandSwapIdleProc, NULL);

    /* Delete timer handler */
    if (tsdPtr->heartbeatTimer) {
        Tcl_DeleteTimerHandler(tsdPtr->heartbeatTimer);
        tsdPtr->heartbeatTimer = NULL;
    }

    /* Delete file handler for wakeup fd */
    if (tsdPtr->wakeupFd != -1) {
        Tcl_DeleteFileHandler(tsdPtr->wakeupFd);
        close(tsdPtr->wakeupFd);
        tsdPtr->wakeupFd = -1;
    }

    /* Remove event source. */
    Tcl_DeleteEventSource(TkWaylandEventsSetupProc,
                          TkWaylandEventsCheckProc, NULL);

    tsdPtr->initialized = false;
    tsdPtr->renderPending = false;
    
    /* Note: We don't call TkGlfwShutdown here. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandExposeEventProc --
 *
 *      Tcl event procedure that handles a single queued expose event.
 *      Installed as a Tcl event handler via Tcl_QueueEvent.
 *
 * Results:
 *      1 if event was processed.
 *
 * Side effects:
 *      Draws the window content using NanoVG.
 *
 *----------------------------------------------------------------------
 */
 
static int
TkWaylandExposeEventProc(Tcl_Event *evPtr, int flags)
{
    TkWaylandExposeEvent *exposePtr = (TkWaylandExposeEvent *)evPtr;
    WindowMapping        *mapping;

    if (!(flags & TCL_WINDOW_EVENTS)) return 0;
    if (exposePtr->winPtr == NULL)    return 1;

    mapping = FindMappingByTk(exposePtr->winPtr);
    if (mapping && mapping->width > 1 && mapping->height > 1) {
        exposePtr->winPtr->changes.width  = mapping->width;
        exposePtr->winPtr->changes.height = mapping->height;
        exposePtr->xEvent.xexpose.width   = mapping->width;
        exposePtr->xEvent.xexpose.height  = mapping->height;
    }

    Tk_HandleEvent(&exposePtr->xEvent);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandHandleExposeEvents --
 *
 *      Called after glfwPollEvents(). Routes any Expose XEvents that
 *      were queued by GLFW callbacks through the NanoVG frame lifecycle
 *      by re-queuing them as Tcl events.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Processes expose events.
 *
 *----------------------------------------------------------------------
 */
 
void
TkWaylandHandleExposeEvents(void)
{
    TkWaylandScheduleRender();
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandQueueExposeEvent --
 *
 *      Queue an Expose event as a wrapped Tcl event.
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
    WindowMapping *m;
    
    /* Create expose event */
    memset(&event, 0, sizeof(XEvent));
    event.type = Expose;
    event.xexpose.window = Tk_WindowId(winPtr);
    event.xexpose.x = x;
    event.xexpose.y = y;
    event.xexpose.width = width;
    event.xexpose.height = height;
    event.xexpose.count = 0;
    
    /* Queue the event */
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    
    /* Mark window as needing display. */
    m = FindMappingByTk(winPtr);
    if (m) {
        TkWaylandScheduleDisplay(m);
    }
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
    
    if (tsdPtr->wakeupFd != -1 && tsdPtr->initialized && 
        !tsdPtr->shutdownInProgress) {
        write(tsdPtr->wakeupFd, &u, sizeof(u));
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
    int fbWidth, fbHeight;
    float pixelRatio;
    
    if (!m || !m->glfwWindow) return;
    
    /* Already have a frame open for this window? */
    if (m->frameOpen) return;
    
    /* Close any frame open on a different window. */
    if (glfwContext.activeFrame && 
        glfwContext.activeFrame != m) {
        TkWaylandEndEventCycle(glfwContext.activeFrame);
    }
    
    /* Make context current. */
    glfwMakeContextCurrent(m->glfwWindow);
    glfwGetFramebufferSize(m->glfwWindow, &fbWidth, &fbHeight);
    
    /* Set viewport */
    glViewport(0, 0, fbWidth, fbHeight);
    pixelRatio = (float)fbWidth / (float)m->width;
    
    /* Clear framebuffer - this happens ONCE per event cycle. */
    glClearColor(0.92f, 0.92f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    /* Begin NanoVG frame - KEEP IT OPEN. */
    nvgBeginFrame(glfwContext.vg,
                  (float)m->width,
                  (float)m->height,
                  pixelRatio);
    
    /* Set up coordinate transform (Y-flip for Tk coordinates). */
    nvgSave(glfwContext.vg);
    nvgScale(glfwContext.vg, 1.0f, -1.0f);
    nvgTranslate(glfwContext.vg, 0.0f, -(float)m->height);
    nvgTranslate(glfwContext.vg, 0.5f, 0.5f);  /* Half-pixel offset */
    
    /* Mark frame as open. */
    m->frameOpen = 1;
    m->inEventCycle = 1;
    glfwContext.activeFrame = m;
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
TkWaylandEndEventCycle(WindowMapping *m)
{
    if (!m || !m->frameOpen) return;
    
    /* Restore coordinate transform. */
    nvgRestore(glfwContext.vg);
    
    /* End NanoVG frame - this renders everything to back buffer. */
    nvgEndFrame(glfwContext.vg);
    
    /* Swap buffers - make it visible NOW. */
    glfwMakeContextCurrent(m->glfwWindow);
    glfwSwapBuffers(m->glfwWindow);
    
    /* Mark frame as closed. */
    m->frameOpen = 0;
    m->inEventCycle = 0;
    m->needsDisplay = 0;
    glfwContext.activeFrame = NULL;
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
    WindowMapping *m = (WindowMapping *)clientData;
    
    /* If frame is open and display is done, close it. */
    if (m->frameOpen && m->inEventCycle) {
        TkWaylandEndEventCycle(m);
    }
    
    m->needsDisplay = 0;
}



/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
