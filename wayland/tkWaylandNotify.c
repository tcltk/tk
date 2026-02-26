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
 */

#include "tkInt.h"
#include "tkGlfwInt.h"



/* Thread-specific data */
typedef struct ThreadSpecificData {
    bool initialized;
    bool waylandInitialized;    /* or glfwInitialized — depending on backend */
    Tcl_TimerToken heartbeatTimer;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

#define TSD_INIT() ThreadSpecificData *tsdPtr = (ThreadSpecificData *) \
    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData))

/* Forward declarations */
static void TkWaylandNotifyExitHandler(void *clientData);
static void TkWaylandEventsSetupProc(void *clientData, int flags);
static void TkWaylandEventsCheckProc(void *clientData, int flags);
static void HeartbeatTimerProc(void *clientData);
static void TkWaylandHandleExposeEvents(void);
static int TkWaylandExposeEventProc(Tcl_Event *evPtr, int flags);

/* Heartbeat timer constants */
#define HEARTBEAT_INTERVAL 50   /* ms */


/*
 *----------------------------------------------------------------------
 *
 * Tk_WaylandSetupTkNotifier --
 *
 *      Called during Tk initialization to install the Wayland/GLFW
 *      event source. Creates a timer handler and installs the event
 *      source callbacks.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Installs TkWaylandEventsSetupProc and TkWaylandEventsCheckProc
 *      as Tcl event sources. Creates a heartbeat timer to ensure
 *      regular polling of GLFW events. Registers an exit handler for
 *      cleanup.
 *
 *----------------------------------------------------------------------
 */
 
void
Tk_WaylandSetupTkNotifier(void)
{
    TSD_INIT();

    if (!tsdPtr->initialized) {
        tsdPtr->initialized = true;

        Tcl_CreateEventSource(TkWaylandEventsSetupProc,
                              TkWaylandEventsCheckProc, NULL);

        tsdPtr->heartbeatTimer = Tcl_CreateTimerHandler(HEARTBEAT_INTERVAL,
                                                        HeartbeatTimerProc,
                                                        NULL);

        TkCreateExitHandler(TkWaylandNotifyExitHandler, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * HeartbeatTimerProc --
 *
 *      Periodic timer to keep the event loop responsive. Reschedules
 *      itself and pumps GLFW events.
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
HeartbeatTimerProc(
    TCL_UNUSED(void *))
{
    TSD_INIT();

    tsdPtr->heartbeatTimer = Tcl_CreateTimerHandler(HEARTBEAT_INTERVAL,
                                                    HeartbeatTimerProc,
                                                    NULL);

    glfwPollEvents();

    /* Flush any expose or auto-opened frames from the poll above. */
    TkWaylandHandleExposeEvents();
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandEventsSetupProc --
 *
 *      Tell Tcl how long we are willing to block. Called by Tcl_DoOneEvent
 *      before blocking.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Sets the maximum block time to zero when window events are
 *      requested, ensuring Tcl doesn't block for extended periods.
 *
 *----------------------------------------------------------------------
 */
 
static const Tcl_Time zeroBlockTime  = { 0, 0 };

static void
TkWaylandEventsSetupProc(
    TCL_UNUSED(void *), /* clientData */
    int flags)
{
    if (flags & TCL_WINDOW_EVENTS) {
        /* For now we always want quick response when windows exist. */
        Tcl_SetMaxBlockTime(&zeroBlockTime);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandEventsCheckProc --
 *
 *      Process pending Wayland/GLFW events and queue synthetic Tk events.
 *      Called by Tcl_DoOneEvent after events are posted.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Calls glfwPollEvents() to process window events when window
 *      events are requested.
 *
 *----------------------------------------------------------------------
 */
 
static void
TkWaylandEventsCheckProc(
    TCL_UNUSED(void *),
    int flags)
{
    if (flags & TCL_WINDOW_EVENTS) {
        /*
         * Poll GLFW first so that window resize, refresh, and input
         * callbacks fire and queue their synthetic XEvents.
         */
        TkGlfwProcessEvents();

        /*
         * Now drain any Expose events that were just queued, routing
         * them through the NanoVG frame lifecycle.
         */
        TkWaylandHandleExposeEvents();
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandNotifyExitHandler --
 *
 *      Clean up at exit. Removes the event source, deletes the
 *      heartbeat timer, and marks the notifier as uninitialized.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Removes the Wayland event source from Tcl's event loop.
 *      Cancels the heartbeat timer if active.
 *
 *----------------------------------------------------------------------
 */
 
static void
TkWaylandNotifyExitHandler(
    TCL_UNUSED(void *)) /* clientData */
{
    TSD_INIT();

    if (!tsdPtr->initialized) {
        return;
    }

    Tcl_DeleteEventSource(TkWaylandEventsSetupProc,
                          TkWaylandEventsCheckProc, NULL);

    if (tsdPtr->heartbeatTimer) {
        Tcl_DeleteTimerHandler(tsdPtr->heartbeatTimer);
        tsdPtr->heartbeatTimer = NULL;
    }

    tsdPtr->initialized = false;
}


/*
 *----------------------------------------------------------------------
 *
 * TkWaylandExposeEventProc -- --
 *
 *      Tcl event procedure that handles a single queued expose event.
 *      Installed as a Tcl event handler via Tcl_QueueProcEvent.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Processes expose events.
 *
 *----------------------------------------------------------------------
 */

static int
TkWaylandExposeEventProc(
    Tcl_Event *evPtr,
    int        flags)
{
    TkWaylandExposeEvent    *exposePtr = (TkWaylandExposeEvent *)evPtr;
    TkWaylandDrawingContext  dc;
    Drawable                 drawable;

    if (!(flags & TCL_WINDOW_EVENTS)) {
        return 0;   /* Leave in queue, not processing window events now. */
    }

    if (exposePtr->winPtr == NULL) {
        return 1;   /* Window was destroyed; discard. */
    }

    drawable = Tk_WindowId((Tk_Window)exposePtr->winPtr);

    if (TkGlfwBeginDraw(drawable, NULL, &dc) != TCL_OK) {
        /* Window not ready to draw yet; let Tk handle it normally. */
        Tk_HandleEvent(&exposePtr->xEvent);
        return 1;
    }

    Tk_HandleEvent(&exposePtr->xEvent);

    TkWaylandDecoration *decoration =
        TkWaylandGetDecoration(exposePtr->winPtr);
    if (decoration && decoration->enabled) {
        TkWaylandDrawDecoration(decoration, dc.vg);
    }

    TkGlfwEndDraw(&dc);
    return 1;   /* Event handled; remove from queue. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandHandleExposeEvents --
 *
 *      Called after glfwPollEvents(). Routes any Expose XEvents that
 *      were queued by GLFW callbacks through the NanoVG frame lifecycle
 *      by re-queuing them as Tcl events processed by
 *      TkWaylandExposeEventProc.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Processes expose events.
 *
 *----------------------------------------------------------------------
 */

static void
TkWaylandHandleExposeEvents(void)
{
    /*
     * Service any pending Tcl window events now. This causes Tcl to
     * dispatch queued XEvents (including the Expose events queued by
     * TkGlfwWindowRefreshCallback and TkGlfwWindowSizeCallback) through
     * Tk's normal event handling path.
     *
     * We call Tcl_ServiceEvent in a loop until no more window events
     * are pending, ensuring all expose events queued in this poll
     * cycle are handled before returning.
     */
    while (Tcl_ServiceEvent(TCL_WINDOW_EVENTS)) {
        /* Keep draining until no more window events are ready. */
    }

    /*
     * Flush any auto-opened NanoVG frame from draw calls that occurred
     * outside TkGlfwBeginDraw (e.g., during widget configuration).
     */
    TkGlfwFlushAutoFrame();
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandQueueExposeEvent --
 *
 *      Queue an Expose event as a wrapped Tcl event that will open
 *      a NanoVG frame before dispatching, instead of using
 *      Tk_QueueWindowEvent which puts a raw XEvent into Tcl's queue
 *      with no NanoVG framing.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Opens a NanoVG frame as part of expose event processing.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 *  --
 *
 *      Queue an Expose event as a wrapped Tcl event that will open
 *      a NanoVG frame before dispatching, instead of using
 *      Tk_QueueWindowEvent which puts a raw XEvent into Tcl's queue
 *      with no NanoVG framing.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandQueueExposeEvent(
    TkWindow *winPtr,
    int       x,
    int       y,
    int       width,
    int       height)
{
    TkWaylandExposeEvent *evPtr;

    if (winPtr == NULL) return;

    evPtr = (TkWaylandExposeEvent *)
                ckalloc(sizeof(TkWaylandExposeEvent));

    evPtr->header.proc = TkWaylandExposeEventProc;
    evPtr->winPtr      = winPtr;

    memset(&evPtr->xEvent, 0, sizeof(XEvent));
    evPtr->xEvent.type               = Expose;
    evPtr->xEvent.xexpose.serial     =
        LastKnownRequestProcessed(winPtr->display);
    evPtr->xEvent.xexpose.send_event = False;
    evPtr->xEvent.xexpose.display    = winPtr->display;
    evPtr->xEvent.xexpose.window     = Tk_WindowId((Tk_Window)winPtr);
    evPtr->xEvent.xexpose.x          = x;
    evPtr->xEvent.xexpose.y          = y;
    evPtr->xEvent.xexpose.width      = width;
    evPtr->xEvent.xexpose.height     = height;
    evPtr->xEvent.xexpose.count      = 0;

    Tcl_QueueEvent((Tcl_Event *)evPtr, TCL_QUEUE_TAIL);
}

 
 /*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
