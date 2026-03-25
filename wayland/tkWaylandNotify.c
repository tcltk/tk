/*
 * tkWaylandNotify.c --
 *
 *      Tcl event source for integrating Wayland/GLFW event loop with Tk.
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
#include "tkWaylandInt.h"
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
    int            shutdownInProgress; /* flag to prevent recursive shutdown */
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
 *      event source.
 *
 *----------------------------------------------------------------------
 */
void
Tk_WaylandSetupTkNotifier(void)
{
    TSD_INIT();

    if (!tsdPtr->initialized) {
        tsdPtr->wakeupFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (tsdPtr->wakeupFd == -1) {
            fprintf(stderr, "TkWaylandNotify: Failed to create eventfd: %s\n",
                    strerror(errno));
            tsdPtr->wakeupFd = -1;
        }

        tsdPtr->initialized   = true;
        tsdPtr->shutdownInProgress = 0;

        Tcl_CreateEventSource(TkWaylandEventsSetupProc,
                              TkWaylandEventsCheckProc, NULL);

        tsdPtr->heartbeatTimer = Tcl_CreateTimerHandler(HEARTBEAT_INTERVAL,
                                                        HeartbeatTimerProc,
                                                        NULL);

        if (tsdPtr->wakeupFd != -1) {
            Tcl_CreateFileHandler(tsdPtr->wakeupFd, TCL_READABLE,
                                  TkWaylandWakeupFileProc, NULL);
        }

        TkCreateExitHandler(TkWaylandNotifyExitHandler, NULL);
        
        fprintf(stderr, "Tk_WaylandSetupTkNotifier: Initialized\n");
    }

    TkWaylandWakeupGLFW();
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWakeupFileProc --
 *
 *      Called when the wakeup file descriptor becomes readable.
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

    if (read(tsdPtr->wakeupFd, &u, sizeof(u)) != sizeof(u)) {
        /* Ignore errors */
    }

    if (glfwContext.initialized && !tsdPtr->shutdownInProgress) {
        glfwPollEvents();
        TkWaylandCheckForWindowClosure();
        
        /* Process pending displays after events */
        for (m = windowMappingList; m; m = m->nextPtr) {
            if (m->needsDisplay && m->glfwWindow) {
                TkWaylandDisplayProc(m);
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCheckForWindowClosure --
 *
 *      Check if all Tk main windows have been destroyed.
 *
 *----------------------------------------------------------------------
 */
static void
TkWaylandCheckForWindowClosure(void)
{
    TSD_INIT();

    if (tsdPtr->shutdownInProgress) return;

    if (Tk_GetNumMainWindows() == 0) {
        fprintf(stderr, "TkWaylandCheckForWindowClosure: No windows left, shutting down\n");
        tsdPtr->shutdownInProgress = 1;
        Tcl_DoWhenIdle(TkWaylandNotifyExitHandler, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * HeartbeatTimerProc --
 *
 *      Periodic timer to keep the event loop responsive.
 *
 *----------------------------------------------------------------------
 */
static void
HeartbeatTimerProc(TCL_UNUSED(void *))
{
    TSD_INIT();
    WindowMapping *m;

    if (!tsdPtr->initialized || tsdPtr->shutdownInProgress) {
        return;
    }

    if (Tk_GetNumMainWindows() == 0) {
        tsdPtr->shutdownInProgress = 1;
        Tcl_DoWhenIdle(TkWaylandNotifyExitHandler, NULL);
        return;
    }

    if (glfwContext.initialized) {
        glfwPollEvents();
        
        /* Process pending displays */
        for (m = windowMappingList; m; m = m->nextPtr) {
            if (m->needsDisplay && m->glfwWindow) {
                TkWaylandDisplayProc(m);
            }
        }
    }

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
 *      Tell Tcl how long we are willing to block.
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
        Tcl_SetMaxBlockTime(&blockTime);
        return;
    }

    if (tsdPtr->wakeupFd != -1 && glfwContext.initialized) {
        return;
    }

    if (!(flags & TCL_WINDOW_EVENTS)) {
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
 *      Process pending Wayland/GLFW events and start drawing cycles.
 *
 *----------------------------------------------------------------------
 */
static void
TkWaylandEventsCheckProc(TCL_UNUSED(void *),
	int flags)
{
    WindowMapping *m;
    
    if (!(flags & TCL_WINDOW_EVENTS)) return;

    /* Poll for new events */
    if (glfwContext.initialized) {
        glfwPollEvents();
    }
    
    /* Start drawing cycles for all mapped windows that aren't already in a frame */
    for (m = windowMappingList; m; m = m->nextPtr) {
        if (m->glfwWindow && m->tkWindow && (m->tkWindow->flags & TK_MAPPED)) {
            if (!m->frameOpen && !m->inEventCycle) {
                TkWaylandBeginEventCycle(m);
                /* Schedule display to end the frame and swap buffers */
                Tcl_DoWhenIdle(TkWaylandDisplayProc, m);
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandNotifyExitHandler --
 *
 *      Clean up at exit.
 *
 *----------------------------------------------------------------------
 */
static void
TkWaylandNotifyExitHandler(TCL_UNUSED(void *))
{
    TSD_INIT();

    if (!tsdPtr->initialized)
        return;

    if (tsdPtr->shutdownInProgress == 1) {
       return;
    }

    if (tsdPtr->heartbeatTimer) {
        Tcl_DeleteTimerHandler(tsdPtr->heartbeatTimer);
        tsdPtr->heartbeatTimer = NULL;
    }

    if (tsdPtr->wakeupFd != -1) {
        Tcl_DeleteFileHandler(tsdPtr->wakeupFd);
        close(tsdPtr->wakeupFd);
        tsdPtr->wakeupFd = -1;
    }

    Tcl_DeleteEventSource(TkWaylandEventsSetupProc,
                          TkWaylandEventsCheckProc, NULL);

    tsdPtr->initialized = false;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandQueueExposeEvent --
 *
 *      Queue an Expose event.
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

    if (!winPtr) return;

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

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

    /* Schedule display for the toplevel window */
    m = FindMappingByTk(winPtr);
    if (m && m->glfwWindow && !m->frameOpen) {
        TkWaylandBeginEventCycle(m);
        Tcl_DoWhenIdle(TkWaylandDisplayProc, m);
    }

    /* Recursively for children */
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
 *      Wake up the GLFW event loop.
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
 *      Called at the START of each event loop iteration.
 *
 *----------------------------------------------------------------------
 */
MODULE_SCOPE void
TkWaylandBeginEventCycle(WindowMapping *m)
{
    if (!m || !m->glfwWindow) return;

    if (m->frameOpen) {
        return;
    }

    glfwMakeContextCurrent(m->glfwWindow);

    int fbw, fbh;
    glfwGetFramebufferSize(m->glfwWindow, &fbw, &fbh);
    
    /* Update window dimensions if changed */
    if (fbw != m->width || fbh != m->height) {
        m->width = fbw;
        m->height = fbh;
        if (m->tkWindow) {
            m->tkWindow->changes.width = fbw;
            m->tkWindow->changes.height = fbh;
        }
    }
    
    glViewport(0, 0, fbw, fbh);

    /* Clear with light gray background */
    glClearColor(0.92f, 0.92f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    m->frameOpen = 1;
    m->inEventCycle = 1;
    glfwContext.activeFrame = m;
    
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandEndEventCycle --
 *
 *      Called at the END of each event loop iteration.
 *
 *----------------------------------------------------------------------
 */
MODULE_SCOPE void
TkWaylandEndEventCycle(WindowMapping *m)
{
    if (!m || !m->frameOpen) return;

    m->frameOpen    = 0;
    m->inEventCycle = 0;
    if (glfwContext.activeFrame == m) {
        glfwContext.activeFrame = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandScheduleDisplay --
 *
 *      Schedules a redraw of a Tk window.
 *
 *----------------------------------------------------------------------
 */
void
TkWaylandScheduleDisplay(WindowMapping *m)
{
    if (!m) return;
    
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
 *      Completes the drawing cycle and swaps buffers.
 *
 *----------------------------------------------------------------------
 */
void
TkWaylandDisplayProc(ClientData clientData)
{
    WindowMapping *m = (WindowMapping *)clientData;
    
    if (!m || !m->glfwWindow) {
        return;
    }
    
    if (!m->frameOpen) {
        /* Need to start a frame first */
        TkWaylandBeginEventCycle(m);
    }


    glfwMakeContextCurrent(m->glfwWindow);
    
    /* End the frame and swap buffers */
    TkWaylandEndEventCycle(m);
    glfwSwapBuffers(m->glfwWindow);

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
