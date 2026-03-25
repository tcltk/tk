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
    int initialized;		/* Flag indicating initialization */
    int waylandInitialized;	/* Flag for Wayland initialization */
    int wakeupFd;		/* eventfd for waking up GLFW polling */
    Tcl_FileProc *watchProc;	/* Stored for cleanup */
    Tcl_TimerToken heartbeatTimer;	/* Fallback timer */
    int shutdownInProgress;	/* Flag to prevent recursive shutdown */
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

#define HEARTBEAT_INTERVAL 16	/* ms - fallback when file events not working */

/*
 *----------------------------------------------------------------------
 *
 * Tk_WaylandSetupTkNotifier --
 *
 *	Called during Tk initialization to install the Wayland/GLFW
 *	event source.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Installs the event source and creates the wakeup file descriptor.
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
 * Results:
 *	None.
 *
 * Side effects:
 *	Processes GLFW events and schedules displays.
 *
 *----------------------------------------------------------------------
 */

static void
TkWaylandWakeupFileProc(
    TCL_UNUSED(void *),
    TCL_UNUSED(int))		/* mask */
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
 *	Check if all Tk main windows have been destroyed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May schedule shutdown.
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
	fprintf(stderr,
		"TkWaylandCheckForWindowClosure: No windows left, shutting down\n");
	tsdPtr->shutdownInProgress = 1;
	Tcl_DoWhenIdle(TkWaylandNotifyExitHandler, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * HeartbeatTimerProc --
 *
 *	Periodic timer to keep the event loop responsive.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Processes events and reschedules itself.
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
 *	Tell Tcl how long we are willing to block.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the maximum block time.
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
 *	Process pending Wayland/GLFW events and start drawing cycles.
 *	Note: No GL work happens here - only event processing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None (drawing is scheduled via TkWaylandScheduleDisplay).
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

    /* Poll for new events. */
    if (glfwContext.initialized) {
	glfwPollEvents();
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandNotifyExitHandler --
 *
 *	Clean up at exit.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes event sources and closes file descriptors.
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
 *	Queue an Expose event.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues an X expose event and schedules display.
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

    /* Queue the actual Expose event for Tk. */
    memset(&event, 0, sizeof(XEvent));
    event.type              = Expose;
    event.xexpose.serial    = LastKnownRequestProcessed(winPtr->display);
    event.xexpose.send_event = False;
    event.xexpose.display   = winPtr->display;
    event.xexpose.window    = Tk_WindowId(winPtr);
    event.xexpose.x         = x;
    event.xexpose.y         = y;
    event.xexpose.width     = width;
    event.xexpose.height    = height;
    event.xexpose.count     = 0;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

    /* Resolve the REAL toplevel using your helper. */
    Tk_Window top = GetToplevelOfWidget((Tk_Window)winPtr);
    if (top) {
        WindowMapping *m = FindMappingByTk((TkWindow *)top);
        if (m && m->glfwWindow) {
            /* Schedule display for the toplevel window ONLY */
            TkWaylandScheduleDisplay(m);
        }
    }

    /* Recursively queue expose for mapped children. */
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
 *	Wake up the GLFW event loop.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes to the wakeup file descriptor.
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
 *	Start a frame for drawing. Called by DisplayProc only when
 *	we actually have content to display.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Makes the OpenGL context current and clears the framebuffer.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandBeginEventCycle(WindowMapping *m)
{
    int fbw, fbh;

    if (!m || !m->glfwWindow) {
    return;
    }

    if (m->frameOpen) {
    return;
    }

    glfwMakeContextCurrent(m->glfwWindow);

    glfwGetFramebufferSize(m->glfwWindow, &fbw, &fbh);

    /* Update window dimensions if changed. */
    if (fbw != m->width || fbh != m->height) {
    m->width = fbw;
    m->height = fbh;
    if (m->tkWindow) {
        m->tkWindow->changes.width = fbw;
        m->tkWindow->changes.height = fbh;
    }
    /* Resize callbacks handle surface lifecycle. */
    }

    glViewport(0, 0, fbw, fbh);

    /* Clear with light gray background. */
    glClearColor(0.92f, 0.92f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    m->frameOpen = 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandEndEventCycle --
 *
 *	End a frame after drawing. Called by DisplayProc after presenting.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears the frame open flag.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandEndEventCycle(WindowMapping *m)
{
    if (!m || !m->frameOpen) {
	return;
    }

    m->frameOpen = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandScheduleDisplay --
 *
 *	Schedules a redraw of a Tk window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Schedules a display callback.
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
 *	Completes the drawing cycle and swaps buffers.
 *	This is the ONLY place where GL work happens.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Uploads the libcg surface to a texture, renders it, and swaps buffers.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandDisplayProc(ClientData clientData)
{
    WindowMapping *m = (WindowMapping *)clientData;

    if (!m) {
        return;
    }
    if (!m->glfwWindow) {
        m->needsDisplay = 0;
        return;
    }
    
    if (TkGlfwEnsureSurface(m) != TCL_OK || !m->surface || !m->surface->pixels) {
		m->needsDisplay = 0;
		return;
	}

    /* Only do something if we actually have pixels to display. */
    if (!m->needsDisplay) {
        return;
    }

    /* Ensure we have a valid surface. */
    if (TkGlfwEnsureSurface(m) != TCL_OK || !m->surface) {
        m->needsDisplay = 0;
        return;
    }

    /* Start the frame. */
    TkWaylandBeginEventCycle(m);
    if (!m->frameOpen) {
        m->needsDisplay = 0;
        return;
    }

    /* Upload the libcg surface pixels to OpenGL texture if needed. */
    if (m->texture.needs_texture_update && m->surface && m->texture.texture_id) {
        TkGlfwUploadSurfaceToTexture(m);
    }

    /* Render the texture to screen. */
    if (m->texture.texture_id) {
        TkGlfwRenderTexture(m);
    }

    /* Swap buffers to present. */
    glfwSwapBuffers(m->glfwWindow);

    /* End the frame. */
    TkWaylandEndEventCycle(m);

    /* Clear the dirty flag. */
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
