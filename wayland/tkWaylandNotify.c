/*
 * tkWaylandNotify.c --
 *
 *      Tcl event source for integrating Wayland/GLFW event loop with Tk.
 *
 *  Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution.
 */

#include "tkInt.h"
#include "tkGlfwint.h"
#include <GLFW/glfw3.h>

#ifdef TK_WAYLAND_DEBUG_EVENTS
static const char *Tk_EventName[] = { /* ... same as before ... */ };
#endif

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
static void WaylandErrorCallback(int error, const char* description);
static void HeartbeatTimerProc(void *clientData);

/* Heartbeat timer constants */
#define HEARTBEAT_INTERVAL 50   /* ms */



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
        tsdPtr->initialized = true;

        if (TkWayland_Initialize() != TCL_OK) {
            Tcl_Panic("Tk_WaylandSetupTkNotifier: backend init failed");
        }

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
 *      Periodic timer to keep the event loop responsive.
 *
 *----------------------------------------------------------------------
 */
static void
HeartbeatTimerProc(
    TCL_UNUSED(void *clientData))
{
    TSD_INIT();

    /* Reschedule ourselves */
    tsdPtr->heartbeatTimer = Tcl_CreateTimerHandler(HEARTBEAT_INTERVAL,
                                                    HeartbeatTimerProc,
                                                    NULL);

    /* Pump Wayland/GLFW events */
    glfwPollEvents();   s
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
static const Tcl_Time zeroBlockTime  = { 0, 0 };
static const Tcl_Time shortBlockTime = { 0, 10000 };  /* 10 ms */

static void
TkWaylandEventsSetupProc(
    TCL_UNUSED(void *clientData),
    int flags)
{
    if (flags & TCL_WINDOW_EVENTS) {
        /* For now we always want quick response when windows exist */
        Tcl_SetMaxBlockTime(&zeroBlockTime);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandEventsCheckProc --
 *
 *      Process pending Wayland/GLFW events and queue synthetic Tk events.
 *
 *----------------------------------------------------------------------
 */
static void
TkWaylandEventsCheckProc(
    TCL_UNUSED(void *clientData),
    int flags)
{
    if (flags & TCL_WINDOW_EVENTS) {
        glfwPollEvents();  
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
TkWaylandNotifyExitHandler(
    TCL_UNUSED(void *clientData))
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

    if (tsdPtr->waylandInitialized) {
        glfwTerminate();    // or wayland cleanup
        tsdPtr->waylandInitialized = false;
    }

    tsdPtr->initialized = false;
}

/*
 *----------------------------------------------------------------------
 *
 * WaylandErrorCallback --
 *
 *      GLFW/Wayland error handler.
 *
 *----------------------------------------------------------------------
 */
static void
WaylandErrorCallback(int error, const char* description)
{
    fprintf(stderr, "Wayland/GLFW Error %d: %s\n", error, description);
}

 
 /*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
