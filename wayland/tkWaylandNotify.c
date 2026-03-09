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
    bool           shutdownInProgress; /* flag to prevent recursive shutdown */
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

#define TSD_INIT() ThreadSpecificData *tsdPtr = (ThreadSpecificData *) \
    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData))

static void TkWaylandNotifyExitHandler(void *clientData);
static void TkWaylandEventsSetupProc(void *clientData, int flags);
static void TkWaylandEventsCheckProc(void *clientData, int flags);
static void HeartbeatTimerProc(void *clientData);
static void TkWaylandWakeupFileProc(void *clientData, int mask);
static void TkWaylandCheckForWindowClosure(void);

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
    
        /* Wake up the event loop. */
        TkWaylandWakeupGLFW();
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
    WindowMapping *m;
    
    if (tsdPtr->wakeupFd == -1) return;
    
    /* Read and discard the eventfd value. */
    if (read(tsdPtr->wakeupFd, &u, sizeof(u)) != sizeof(u)) {
        /* Ignore errors - just clear the fd. */
    }
    
    /* Process any pending GLFW events. */
    if (glfwContext.initialized && !tsdPtr->shutdownInProgress) {
        glfwPollEvents();
        TkWaylandCheckForWindowClosure();
        
        /* Begin event cycle for main window */
        if (glfwContext.mainWindow) {
            m = FindMappingByGLFW(glfwContext.mainWindow);
            if (m && !m->frameOpen) {
                TkWaylandBeginEventCycle(m);
            }
        }
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
 *      Begins the event cycle for the main window to enable rendering.
 *
 *----------------------------------------------------------------------
 */
 
static void
TkWaylandEventsCheckProc(TCL_UNUSED(void *), int flags) 
{
    TSD_INIT();
    WindowMapping *m;
    
    if (tsdPtr->shutdownInProgress) return;
    
    if (!(flags & TCL_WINDOW_EVENTS)) return;
    if (!glfwContext.initialized)    return;
    
    /* Find the main window or first available window and begin its event cycle. */
    if (glfwContext.mainWindow) {
        m = FindMappingByGLFW(glfwContext.mainWindow);
        if (m && !m->frameOpen) {
            TkWaylandBeginEventCycle(m);
        }
    } else {
        /* If no main window, try the first window in the list. */
        m = windowMappingList;
        if (m && !m->frameOpen) {
            TkWaylandBeginEventCycle(m);
        }
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
    if (tsdPtr->shutdownInProgress && tsdPtr->shutdownInProgress != 2) {
        tsdPtr->shutdownInProgress = 2; /* Mark as final shutdown. */
    }

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
    
    /* Note: We don't call TkGlfwShutdown here. */
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
    TkWindow *childPtr;
    WindowMapping *m;
    int childCount = 0;
    
    if (!winPtr) return;
    
    
    fprintf(stderr, "QueueExposeEvent: winPtr=%p x=%d y=%d w=%d h=%d\n", 
            winPtr, x, y, width, height);
    
    /* Create expose event. */
    memset(&event, 0, sizeof(XEvent));
    event.type = Expose;
    event.xexpose.serial = LastKnownRequestProcessed(winPtr->display);
    event.xexpose.send_event = False;
    event.xexpose.display = winPtr->display;
    event.xexpose.window = Tk_WindowId(winPtr);
    event.xexpose.x = x;
    event.xexpose.y = y;
    event.xexpose.width = width;
    event.xexpose.height = height;
    event.xexpose.count = 0;
    
    /* Queue it. */
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    
    /* Recursively for children. */
    for (childPtr = winPtr->childList; childPtr != NULL;
         childPtr = childPtr->nextPtr) {
        if (!Tk_IsMapped((Tk_Window)childPtr) || Tk_IsTopLevel((Tk_Window)childPtr)) {
            continue;
        }
        TkWaylandQueueExposeEvent(childPtr, 
                                 0, 0,
                                 Tk_Width((Tk_Window)childPtr),
                                 Tk_Height((Tk_Window)childPtr));
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
    if (!m || !m->glfwWindow) return;

	if (m->frameOpen) {
        /* Already in drawing round – do NOT clear again. */
		fprintf(stderr, "BeginEventCycle: already open, continuing\n");
		return;
	}


    glfwMakeContextCurrent(m->glfwWindow);

    int fbw, fbh;
    glfwGetFramebufferSize(m->glfwWindow, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);

    /* Clear only when starting new frame. */
    glClearColor(0.92f, 0.92f, 0.92f, 1.0f);
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
	
	    if (!m || !m->frameOpen) {
        fprintf(stderr, "EndEventCycle: Frame not open, returning\n");
        return;
    }
    
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
    WindowMapping *m = (WindowMapping *)clientData;

    if (!m || !m->glfwWindow || !m->frameOpen) {
        fprintf(stderr, "DisplayProc: nothing to do (no window or frame not open)\n");
        m->needsDisplay = 0;
        return;
    }


    /* Restore from the nvgSave() we did in BeginEventCycle. */
    nvgRestore(glfwContext.vg);

    /* Finish NanoVG rendering. */
    nvgEndFrame(glfwContext.vg);

    /* Actually show the drawing. */
    glfwMakeContextCurrent(m->glfwWindow);
    glfwSwapBuffers(m->glfwWindow);

    m->frameOpen     = 0;
    m->inEventCycle  = 0;
    m->needsDisplay  = 0;
    glfwContext.activeFrame = NULL;

}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
