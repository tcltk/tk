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
#include <GLES3/gl3.h>
#include "nanovg_gl_utils.h"


extern TkGlfwContext  glfwContext;
extern WindowMapping *windowMappingList;

typedef struct ThreadSpecificData {
    bool           initialized;
    bool           waylandInitialized;
    int            shutdownInProgress; /* flag to prevent recursive shutdown */
    int            callbackCount;  /* used by the setup proc to check for events. */
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

#define TSD_INIT() ThreadSpecificData *tsdPtr = (ThreadSpecificData *) \
    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData))

static void TkWaylandNotifyExitHandler(void *clientData);
static void TkWaylandEventsSetupProc(void *clientData, int flags);
static void TkWaylandEventsCheckProc(void *clientData, int flags);
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
        tsdPtr->initialized   = true;
        tsdPtr->shutdownInProgress = 0;

        /* Create the Tcl event source. */
        Tcl_CreateEventSource(TkWaylandEventsSetupProc,
                              TkWaylandEventsCheckProc, NULL);
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

/* Called by the callback functions to tell the setup proc not to block. */
MODULE_SCOPE void
recordCallback() {
    TSD_INIT();
    tsdPtr->callbackCount++;
}

/* Called by the setup proc to reset the callback counter. */
MODULE_SCOPE void
clearCallbackCount() {
    TSD_INIT();
    tsdPtr->callbackCount = 0;
}
    
static void
TkWaylandEventsSetupProc(TCL_UNUSED(void *),
			 int flags)
{
    TSD_INIT();
    Tcl_Time noBlock = {0, 0};        /* secs, microsecs */
    Tcl_Time oneRefresh = {0, 16667}; /* ~ 1/60 sec */
    
    if (tsdPtr->shutdownInProgress) {
        /* Don't block during shutdown. */
        Tcl_SetMaxBlockTime(&noBlock);
        return;
    }

    /*
     * Clear the callback counter and call glfwPollEvents.
     * If there were no events, block for one display cycle.
     * Otherwise, don't block.
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
 * TkWaylandEventsCheckProc --
 *
 *      Process pending Wayland/GLFW events and queue Tk events.
 *      Called by Tcl_DoOneEvent after calling Tcl_WaitForEvent, which
 *      will call tclNotifierHooks.waitForEventProc if it is defined.
 *      We are using the default waitForEventProc (I think).
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
TkWaylandEventsCheckProc(TCL_UNUSED(void *),
	int flags) 
{
    //// Currently we don't need to do anything here. (????)
    if (!(flags & TCL_WINDOW_EVENTS)) {
	printf("CheckProc called without WINDOW_EVENTS\n");
	return;
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
    if (tsdPtr->shutdownInProgress == 1) {
       return;
    }

    /* Remove event source. */
    Tcl_DeleteEventSource(TkWaylandEventsSetupProc,
                          TkWaylandEventsCheckProc, NULL);

    tsdPtr->initialized = false;
    
    /* Note: We don't call TkGlfwShutdown here. */
    //// Why not?
}

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
    printf("TkWaylandQueueExposeEvent: %s\n", Tk_PathName(winPtr));
    
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
    printf("Queuing Expose(%lu)\n", event.xexpose.serial);
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    
    /* Recurse through the children of this window. */
    for (childPtr = winPtr->childList; childPtr != NULL;
         childPtr = childPtr->nextPtr) {
        if (!Tk_IsMapped((Tk_Window)childPtr) ||
	    Tk_IsTopLevel((Tk_Window)childPtr)) {
	    //if (Tk_IsTopLevel((Tk_Window)childPtr)) {
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
    
    if (tsdPtr->initialized && !tsdPtr->shutdownInProgress) {
	//// This should post an empty event to GLFW!!!
        //write(tsdPtr->wakeupFd, &u, sizeof(u));
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

#if 0
    int fbw, fbh;
    glfwGetFramebufferSize(m->glfwWindow, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);

    /* Clear only when starting new frame. */
    //// Changed to purple for debugging
    glClearColor(0.92f, 0.92f, 0.92f, 0.0f);
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
#endif
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
TkWaylandEndEventCycle(TCL_UNUSED(WindowMapping *))
{
	return;
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
#if 0 //// What is this for?
    WindowMapping *m = (WindowMapping *)clientData;
    if (!m || !m->fbo) return;

    glfwMakeContextCurrent(m->glfwWindow);
    
    int fbw, fbh;
    glfwGetFramebufferSize(m->glfwWindow, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);

    /* Clear only the screen backbuffer, not our FBO! */
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    m->needsDisplay = 0;
#endif
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
