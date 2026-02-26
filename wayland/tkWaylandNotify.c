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
 * TkWaylandHandleExposeEvents --
 *
 *      Drain all pending Expose events from the Tcl event queue and
 *      route each through the NanoVG begin/end frame lifecycle.
 *      Called from TkWaylandEventsCheckProc after GLFW has been polled.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Each pending Expose event causes a full NanoVG frame to be
 *      rendered and swapped for the target window.
 *
 *----------------------------------------------------------------------
 */

static void
TkWaylandHandleExposeEvents(void)
{
    TkDisplay   *dispPtr;
    TkWindow    *winPtr;
    Drawable     drawable;
    XEvent       event;
    TkWaylandDrawingContext dc;

    /*
     * Walk every known Tk display. In practice there is only one
     * for a Wayland application, but we follow Tk convention.
     */
    for (dispPtr = TkGetDisplayList(); dispPtr != NULL;
         dispPtr = dispPtr->nextPtr) {

        /*
         * XCheckTypedEvent pulls one Expose event at a time from
         * Xlib's queue. On Wayland we have a synthetic queue, so
         * we keep draining until there are none left.
         */
        while (XCheckTypedEvent(dispPtr->display, Expose, &event)) {

            /*
             * Locate the Tk window for this event. Skip if the
             * window has been destroyed in the interim.
             */
            winPtr = (TkWindow *)
                Tk_IdToWindow(dispPtr->display, event.xexpose.window);
            if (winPtr == NULL) {
                continue;
            }

            drawable = Tk_WindowId((Tk_Window)winPtr);

            /*
             * Open a NanoVG frame for this window.  If we cannot
             * (window not yet mapped, no GLFW backing, etc.) fall
             * back to letting Tk handle the event normally so that
             * at least the event is consumed.
             */
            if (TkGlfwBeginDraw(drawable, NULL, &dc) != TCL_OK) {
                Tk_HandleEvent(&event);
                continue;
            }

            /*
             * Let Tk's generic machinery dispatch the Expose event.
             * This calls the widget's display procedure, which in
             * turn calls XFillRectangle, XDrawLines, etc. — all of
             * which are now safe because nvgFrameActive is 1.
             */
            Tk_HandleEvent(&event);

            /*
             * Close the frame and swap buffers.
             */
            TkGlfwEndDraw(&dc);
        }
    }

    /*
     * Flush any auto-opened NanoVG frame that was opened by a draw
     * call that occurred outside TkGlfwBeginDraw (e.g., during
     * widget configuration). This must come after the expose loop
     * so it does not interfere with the frame just swapped above.
     */
    TkGlfwFlushAutoFrame();
}


 
 /*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
