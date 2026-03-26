/*
 * tkWaylandNotify.c --
 *
 *	Tcl event source for integrating Wayland/GLFW event loop with Tk.
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

extern struct TkGlfwContext glfwContext;
extern WindowMapping *windowMappingList;

typedef struct ThreadSpecificData {
    int initialized;
    int waylandInitialized;
    int wakeupFd;
    Tcl_FileProc *watchProc;
    Tcl_TimerToken heartbeatTimer;
    int shutdownInProgress;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

#define TSD_INIT() \
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *) \
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData))

static void TkWaylandNotifyExitHandler(void *clientData);
static void TkWaylandEventsSetupProc(void *clientData, int flags);
static void TkWaylandEventsCheckProc(void *clientData, int flags);
static void HeartbeatTimerProc(void *clientData);
static void TkWaylandWakeupFileProc(void *clientData, int mask);
static void TkWaylandCheckForWindowClosure(void);

#define HEARTBEAT_INTERVAL 16	/* ms */

/*
 *----------------------------------------------------------------------
 *
 * Tk_WaylandSetupTkNotifier --
 *
 *	Called during Tk initialization to install the Wayland/GLFW
 *	event source.
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
	    fprintf(stderr,
		    "TkWaylandNotify: Failed to create eventfd: %s\n",
		    strerror(errno));
	    tsdPtr->wakeupFd = -1;
	}

	tsdPtr->initialized = 1;
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
    }

    if (glfwContext.initialized) {
	TkWaylandWakeupGLFW();
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWakeupFileProc --
 *
 *	Called when the wakeup file descriptor becomes readable.
 *
 *----------------------------------------------------------------------
 */

static void
TkWaylandWakeupFileProc(
    TCL_UNUSED(void *),
    TCL_UNUSED(int))
{
    TSD_INIT();
    uint64_t u;

    if (tsdPtr->wakeupFd == -1) {
	return;
    }

    if (read(tsdPtr->wakeupFd, &u, sizeof(u)) != sizeof(u)) {
	/* Ignore errors. */
    }

    if (glfwContext.initialized && !tsdPtr->shutdownInProgress) {
	glfwPollEvents();
	TkWaylandCheckForWindowClosure();
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCheckForWindowClosure --
 *
 *----------------------------------------------------------------------
 */

static void
TkWaylandCheckForWindowClosure(void)
{
    TSD_INIT();

    if (tsdPtr->shutdownInProgress) {
	return;
    }

    if (Tk_GetNumMainWindows() == 0) {
	tsdPtr->shutdownInProgress = 1;
	Tcl_DoWhenIdle(TkWaylandNotifyExitHandler, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * HeartbeatTimerProc --
 *
 *	Periodic timer to keep the event loop alive and poll GLFW events.
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

    if (Tk_GetNumMainWindows() == 0) {
	tsdPtr->shutdownInProgress = 1;
	Tcl_DoWhenIdle(TkWaylandNotifyExitHandler, NULL);
	return;
    }

    if (glfwContext.initialized) {
	glfwPollEvents();
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
 *----------------------------------------------------------------------
 */

static void
TkWaylandEventsSetupProc(
    TCL_UNUSED(void *),
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
 *----------------------------------------------------------------------
 */

static void
TkWaylandEventsCheckProc(
    TCL_UNUSED(void *),
    int flags)
{
    if (!(flags & TCL_WINDOW_EVENTS)) {
	return;
    }

    if (glfwContext.initialized) {
	glfwPollEvents();
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandNotifyExitHandler --
 *
 *----------------------------------------------------------------------
 */

static void
TkWaylandNotifyExitHandler(TCL_UNUSED(void *))
{
    TSD_INIT();

    if (!tsdPtr->initialized) {
	return;
    }

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

    tsdPtr->initialized = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandQueueExposeEvent --
 *
 *	Queue an Expose event and clear the surface for the expose cycle.
 *
 *	IMPORTANT: We clear the surface HERE, once per expose cycle,
 *	before any widget redraws begin.  This is the correct place because:
 *
 *	  1. All widget draws in response to this Expose will accumulate on
 *	     the freshly-cleared surface.
 *	  2. TkGlfwBeginDraw must NOT clear the surface (that would erase
 *	     everything drawn by prior primitives in the same cycle).
 *	  3. TkWaylandDisplayProc (via Tcl_DoWhenIdle) fires after all
 *	     idle widget draws complete, uploading the fully-composited
 *	     surface exactly once.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandQueueExposeEvent(
    TkWindow *winPtr,
    int x,
    int y,
    int width,
    int height)
{
    XEvent event;
    TkWindow *childPtr;

    if (!winPtr) {
        return;
    }

    /* ------------------------------------------------------------------
     * Clear the surface for this toplevel at the START of each expose
     * cycle.  We find the mapping before queuing the event so that
     * widget redraws triggered by the event find a clean surface.
     * ------------------------------------------------------------------ */
    Tk_Window top = GetToplevelOfWidget((Tk_Window)winPtr);
    if (top) {
        WindowMapping *m = FindMappingByTk((TkWindow *)top);
        if (m) {
            TkGlfwClearSurface(m);
        }
    }

    /* Queue the Expose event. */
    memset(&event, 0, sizeof(XEvent));
    event.type               = Expose;
    event.xexpose.serial     = LastKnownRequestProcessed(winPtr->display);
    event.xexpose.send_event = False;
    event.xexpose.display    = winPtr->display;
    event.xexpose.window     = Tk_WindowId(winPtr);
    event.xexpose.x          = x;
    event.xexpose.y          = y;
    event.xexpose.width      = width;
    event.xexpose.height     = height;
    event.xexpose.count      = 0;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

    /* Schedule display for the toplevel. */
    if (top) {
        WindowMapping *m = FindMappingByTk((TkWindow *)top);
        if (m && m->glfwWindow) {
            TkWaylandScheduleDisplay(m);
        }
    }

    /* Recurse into mapped non-toplevel children. */
    for (childPtr = winPtr->childList;
         childPtr != NULL;
         childPtr = childPtr->nextPtr)
    {
        if (!Tk_IsMapped((Tk_Window)childPtr) ||
            Tk_IsTopLevel((Tk_Window)childPtr)) {
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
 *	Wake up the GLFW event loop via the eventfd.
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
 * TkWaylandScheduleDisplay --
 *
 *	Schedule a single deferred redraw for the window.
 *	Uses needsDisplay as a guard so that multiple calls within one
 *	event-loop iteration queue exactly one DisplayProc invocation.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandScheduleDisplay(WindowMapping *m)
{
    if (!m) {
	return;
    }

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
 *	The SINGLE site where GPU work is performed for a window:
 *	  1. Upload the software-rendered libcg surface to the GL texture.
 *	  2. Set the viewport and clear the colour buffer.
 *	  3. Render the full-screen textured quad.
 *	  4. Swap buffers.
 *
 *	This fires via Tcl_DoWhenIdle, which means it runs after all
 *	widget draw procs for the current event have completed.  The
 *	surface therefore contains the fully-composited frame when we
 *	arrive here, eliminating partial-frame flicker.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandDisplayProc(ClientData clientData)
{
    WindowMapping *m = (WindowMapping *)clientData;

    if (!m || !m->glfwWindow) {
        m->needsDisplay = 0;
        return;
    }

    /* Always clear the guard first so new redraws can be scheduled
     * while we are doing GPU work below. */
    m->needsDisplay = 0;

    /* Ensure a valid surface. */
    if (TkGlfwEnsureSurface(m) != TCL_OK || !m->surface || !m->surface->pixels) {
        return;
    }

    /* Make the context current for all GL operations. */
    glfwMakeContextCurrent(m->glfwWindow);

    /* Upload the software surface to GPU if any draw happened. */
    if (m->texture.needs_texture_update) {
        TkGlfwUploadSurfaceToTexture(m);
        /* needs_texture_update is cleared inside Upload. */
    }

    if (!m->texture.texture_id) {
        return;
    }

    /*
     * Set the viewport to the full window.
     * Clear the colour buffer to the default background so that any
     * region not covered by the texture quad (e.g. letterboxing) shows
     * a neutral colour rather than undefined framebuffer contents.
     */
    glViewport(0, 0, m->width, m->height);
    glClearColor(0.92f, 0.92f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Render the full-screen texture quad. */
    TkGlfwRenderTexture(m);

    /* Present to the compositor. */
    glfwSwapBuffers(m->glfwWindow);

    /* frameOpen is no longer used for anything critical but keep it tidy. */
    m->frameOpen = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandBeginEventCycle / TkWaylandEndEventCycle --
 *
 *	These stubs are retained for API compatibility with any callers
 *	that may have been wired up.  All real GL work has been
 *	consolidated into TkWaylandDisplayProc.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandBeginEventCycle(WindowMapping *m)
{
    (void)m;
    /* No-op: GL work is done in TkWaylandDisplayProc. */
}

MODULE_SCOPE void
TkWaylandEndEventCycle(WindowMapping *m)
{
    if (m) {
        m->frameOpen = 0;
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
