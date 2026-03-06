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
 * Drawing pipeline
 * ----------------
 * glfwPollEvents is called ONLY from HeartbeatTimerProc and
 * TkWaylandEventsCheckProc.  It queues Tk XEvents via GLFW callbacks
 * but never opens a NanoVG frame.
 *
 * Widget display procedures own their own BeginDraw/EndDraw pairs and
 * are driven by Tk's normal event/idle mechanism.  The only thing we
 * need to do before they run is set clearPending=1 so the first
 * BeginDraw for each window each tick issues a glClear.  Subsequent
 * BeginDraw calls on the same window take the nested-frame path and
 * composite into the same cleared buffer.
 *
 * TkWaylandRenderIdleProc (a Tcl idle callback) sets clearPending and
 * then drains queued window events and idle callbacks with
 * Tcl_DoOneEvent(TCL_WINDOW_EVENTS|TCL_IDLE_EVENTS|TCL_DONT_WAIT).
 * Timer and file events are excluded to prevent re-entering the
 * heartbeat timer or the Wayland IME fd handler during a draw pass.
 */

#include "tkInt.h"
#include "tkGlfwInt.h"

extern TkGlfwContext  glfwContext;

typedef struct ThreadSpecificData {
    bool           initialized;
    bool           waylandInitialized;
    Tcl_TimerToken heartbeatTimer;
    bool           renderPending;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

#define TSD_INIT() ThreadSpecificData *tsdPtr = (ThreadSpecificData *) \
    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData))

static void TkWaylandNotifyExitHandler(void *clientData);
static void TkWaylandEventsSetupProc(void *clientData, int flags);
static void TkWaylandEventsCheckProc(void *clientData, int flags);
static void HeartbeatTimerProc(void *clientData);
static void TkWaylandRenderIdleProc(void *clientData);
static void TkWaylandScheduleRender(void);
static int  TkWaylandExposeEventProc(Tcl_Event *evPtr, int flags);

#define HEARTBEAT_INTERVAL 16   /* ms */

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
        tsdPtr->initialized   = true;
        tsdPtr->renderPending = false;

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
 
static void
TkWaylandScheduleRender(void)
{
    TSD_INIT();

    if (!tsdPtr->renderPending && tsdPtr->initialized) {
        tsdPtr->renderPending = true;
        Tcl_DoWhenIdle(TkWaylandRenderIdleProc, NULL);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TkWaylandRenderIdleProc -- --
 *
 *   Runs once per Tcl idle cycle.  Sets clearPending on every mapped
 *   window so the first BeginDraw this tick clears the framebuffer,
 *   then drains window events and idle callbacks so widget display
 *   procedures run.  Does NOT open a NanoVG frame itself.
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
TkWaylandRenderIdleProc(ClientData clientData)
{
    WindowMapping *m;
    TkGlfwContext *ctx = TkGlfwGetContext();
    TSD_INIT();
    
    tsdPtr->renderPending = false;

    /* Safety check */
    if (!ctx || !ctx->initialized) {
        fprintf(stderr, "TkWaylandRenderIdleProc: GLFW not initialized\n");
        return;
    }

    /* Prime per-window clear flag for every mapped toplevel. */
    WindowMapping *list = TkGlfwGetMappingList();
    if (!list) {
        fprintf(stderr, "TkWaylandRenderIdleProc: No window mapping list\n");
        return;
    }

    for (m = list; m != NULL; m = m->nextPtr) {
        if (m == NULL) {
            fprintf(stderr, "TkWaylandRenderIdleProc: NULL mapping in list\n");
            break;
        }
        
        if (m->glfwWindow && m->width > 1 && m->height > 1) {
            m->clearPending = 1;  /* Use 1 instead of true for portability */
        }
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
HeartbeatTimerProc(TCL_UNUSED(void *))
{
    TSD_INIT();

    if (!tsdPtr->initialized)
        return;

    tsdPtr->heartbeatTimer = Tcl_CreateTimerHandler(HEARTBEAT_INTERVAL,
                                                    HeartbeatTimerProc,
                                                    NULL);
    glfwPollEvents();
    TkWaylandScheduleRender();
}

static const Tcl_Time zeroBlockTime = { 0, 0 };

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

static void
TkWaylandEventsSetupProc(TCL_UNUSED(void *), int flags)
{
    if (flags & TCL_WINDOW_EVENTS)
        Tcl_SetMaxBlockTime(&zeroBlockTime);
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
TkWaylandEventsCheckProc(TCL_UNUSED(void *), int flags)
{
    if (!(flags & TCL_WINDOW_EVENTS)) {
        return;
    }

    if (!glfwContext.initialized) {
        return;
    }

    if (!glfwContext.mainWindow) {
        return;
    }

    glfwPollEvents();

    TkWaylandScheduleRender();
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
TkWaylandNotifyExitHandler(TCL_UNUSED(void *))
{
    TSD_INIT();

    if (!tsdPtr->initialized)
        return;

    tsdPtr->initialized = false;

    Tcl_DeleteEventSource(TkWaylandEventsSetupProc,
                          TkWaylandEventsCheckProc, NULL);

    if (tsdPtr->heartbeatTimer) {
        Tcl_DeleteTimerHandler(tsdPtr->heartbeatTimer);
        tsdPtr->heartbeatTimer = NULL;
    }

    Tcl_CancelIdleCall(TkWaylandRenderIdleProc, NULL);
    tsdPtr->renderPending = false;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandExposeEventProc --
 *
 *      Tcl event procedure that handles a single queued expose event.
 *      Installed as a Tcl event handler via Tcl_QueueProcEvent.
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
 
void
TkWaylandQueueExposeEvent(
    TkWindow *winPtr,
    int x, int y, int width, int height)
{
    TkWaylandExposeEvent *evPtr;

    if (winPtr == NULL)           return;
    if (winPtr->window == None)   return;

    if (width  <= 1) width  = winPtr->changes.width;
    if (height <= 1) height = winPtr->changes.height;
    if (width  <= 1 || height <= 1) return;

    evPtr = (TkWaylandExposeEvent *)ckalloc(sizeof(TkWaylandExposeEvent));
    evPtr->header.proc = TkWaylandExposeEventProc;
    evPtr->winPtr      = winPtr;

    memset(&evPtr->xEvent, 0, sizeof(XEvent));
    evPtr->xEvent.type                   = Expose;
    evPtr->xEvent.xexpose.serial         =
        LastKnownRequestProcessed(winPtr->display);
    evPtr->xEvent.xexpose.send_event     = False;
    evPtr->xEvent.xexpose.display        = winPtr->display;
    evPtr->xEvent.xexpose.window         = winPtr->window;
    evPtr->xEvent.xexpose.x              = x;
    evPtr->xEvent.xexpose.y              = y;
    evPtr->xEvent.xexpose.width          = width;
    evPtr->xEvent.xexpose.height         = height;
    evPtr->xEvent.xexpose.count          = 0;

    Tcl_QueueEvent((Tcl_Event *)evPtr, TCL_QUEUE_TAIL);
    TkWaylandScheduleRender();
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
