/*
 * tkSDLEvent.c --
 *
 *	This file implements an event source for X displays for the UNIX
 *	version of Tk.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkSDLInt.h"
#ifdef _WIN32
#include <windows.h>
#endif

/*
 * The following static indicates whether this module has been initialized in
 * the current thread.
 */

typedef struct ThreadSpecificData {
    int initialized;
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * Prototypes for functions that are referenced only in this file:
 */

static void		DisplayCheckProc(ClientData clientData, int flags);
static void		DisplayExitHandler(ClientData clientData);
#ifndef _WIN32
static void		DisplayFileProc(ClientData clientData, int flags);
#endif
static void		DisplaySetupProc(ClientData clientData, int flags);
static void		TransferXEventsToTcl(Display *display);

/*
 *----------------------------------------------------------------------
 *
 * TkCreateXEventSource --
 *
 *	This function is called during Tk initialization to create the event
 *	source for X Window events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A new event source is created.
 *
 *----------------------------------------------------------------------
 */

void
TkCreateXEventSource(void)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (!tsdPtr->initialized) {
	tsdPtr->initialized = 1;
	Tcl_CreateEventSource(DisplaySetupProc, DisplayCheckProc, NULL);
	TkCreateExitHandler(DisplayExitHandler, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DisplayExitHandler --
 *
 *	This function is called during finalization to clean up the display
 *	module.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
DisplayExitHandler(
    ClientData clientData)	/* Not used. */
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    Tcl_DeleteEventSource(DisplaySetupProc, DisplayCheckProc, NULL);
    tsdPtr->initialized = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpOpenDisplay --
 *
 *	Allocates a new TkDisplay, opens the X display, and establishes the
 *	file handler for the connection.
 *
 * Results:
 *	A pointer to a Tk display structure.
 *
 * Side effects:
 *	Opens a display.
 *
 *----------------------------------------------------------------------
 */

TkDisplay *
TkpOpenDisplay(
    const char *displayNameStr)
{
    TkDisplay *dispPtr;
    Display *display;

    display  = XOpenDisplay(displayNameStr);
    if (display == NULL) {
	return NULL;
    }
    dispPtr = ckalloc(sizeof(TkDisplay));
    memset(dispPtr, 0, sizeof(TkDisplay));
    dispPtr->display = display;
#ifndef _WIN32
    if (ConnectionNumber(display) >= 0) {
	Tcl_CreateFileHandler(ConnectionNumber(display), TCL_READABLE,
		DisplayFileProc, dispPtr);
    }
#endif
    return dispPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpCloseDisplay --
 *
 *	Cancels notifier callbacks and closes a display.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deallocates the displayPtr and unix-specific resources.
 *
 *----------------------------------------------------------------------
 */

void
TkpCloseDisplay(
    TkDisplay *dispPtr)
{
    TkSendCleanup(dispPtr);

    TkWmCleanup(dispPtr);

    if (dispPtr->display != 0) {
#ifndef _WIN32
	if (ConnectionNumber(dispPtr->display) >= 0) {
	    Tcl_DeleteFileHandler(ConnectionNumber(dispPtr->display));
	}
#endif
	(void) XSync(dispPtr->display, False);
	(void) XCloseDisplay(dispPtr->display);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkClipCleanup --
 *
 *	This function is called to cleanup resources associated with claiming
 *	clipboard ownership and for receiving selection get results. This
 *	function is called in tkWindow.c. This has to be called by the display
 *	cleanup function because we still need the access display elements.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Resources are freed - the clipboard may no longer be used.
 *
 *----------------------------------------------------------------------
 */

void
TkClipCleanup(
    TkDisplay *dispPtr)		/* Display associated with clipboard */
{
    if (dispPtr->clipWindow != NULL) {
	Tk_DeleteSelHandler(dispPtr->clipWindow, dispPtr->clipboardAtom,
		dispPtr->applicationAtom);
	Tk_DeleteSelHandler(dispPtr->clipWindow, dispPtr->clipboardAtom,
		dispPtr->windowAtom);

	Tk_DestroyWindow(dispPtr->clipWindow);
	Tcl_Release(dispPtr->clipWindow);
	dispPtr->clipWindow = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DisplaySetupProc --
 *
 *	This function implements the setup part of the UNIX X display event
 *	source. It is invoked by Tcl_DoOneEvent before entering the notifier
 *	to check for events on all displays.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If data is queued on a display inside Xlib, then the maximum block
 *	time will be set to 0 to ensure that the notifier returns control to
 *	Tcl even if there is no more data on the X connection.
 *
 *----------------------------------------------------------------------
 */

static void
DisplaySetupProc(
    ClientData clientData,	/* Not used. */
    int flags)
{
    TkDisplay *dispPtr;
    static Tcl_Time blockTime = { 0, 0 };
    static Tcl_Time blockTime20 = { 0, 20000 };

    if (!(flags & TCL_WINDOW_EVENTS)) {
	return;
    }

    for (dispPtr = TkGetDisplayList(); dispPtr != NULL;
	    dispPtr = dispPtr->nextPtr) {
	/*
	 * Flush the display. If data is pending on the X queue, set the block
	 * time to zero. This ensures that we won't block in the notifier if
	 * there is data in the X queue, but not on the server socket.
	 */

	XFlush(dispPtr->display);
	if (XEventsQueued(dispPtr->display, QueuedAlready) > 0) {
	    Tcl_SetMaxBlockTime(&blockTime);
	} else {
	    Tcl_SetMaxBlockTime(&blockTime20);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TransferXEventsToTcl --
 *
 *	Transfer events from the X event queue to the Tk event queue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Moves queued X events onto the Tcl event queue.
 *
 *----------------------------------------------------------------------
 */

static void
TransferXEventsToTcl(
    Display *display)
{
    union {
	int type;
	XEvent x;
	TkKeyEvent k;
    } event;
    int numFound;

    numFound = XEventsQueued(display, QueuedAlready);

    /*
     * Transfer events from the X event queue to the Tk event queue.
     */

    while (numFound > 0) {
	memset(&event, 0, sizeof(event));
	XNextEvent(display, &event.x);
	if (event.type != PointerUpdate) {
	    Tk_QueueWindowEvent(&event.x, TCL_QUEUE_TAIL);
	}
	numFound--;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DisplayCheckProc --
 *
 *	This function checks for events sitting in the X event queue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Moves queued events onto the Tcl event queue.
 *
 *----------------------------------------------------------------------
 */

static void
DisplayCheckProc(
    ClientData clientData,	/* Not used. */
    int flags)
{
    TkDisplay *dispPtr;

    if (!(flags & TCL_WINDOW_EVENTS)) {
	return;
    }

    for (dispPtr = TkGetDisplayList(); dispPtr != NULL;
	    dispPtr = dispPtr->nextPtr) {
	XFlush(dispPtr->display);
	TransferXEventsToTcl(dispPtr->display);
    }
}

#ifndef _WIN32
/*
 *----------------------------------------------------------------------
 *
 * DisplayFileProc --
 *
 *	This function implements the file handler for the X connection.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Makes entries on the Tcl event queue for all the events available from
 *	all the displays.
 *
 *----------------------------------------------------------------------
 */

static void
DisplayFileProc(
    ClientData clientData,	/* The display pointer. */
    int flags)			/* Should be TCL_READABLE. */
{
    TkDisplay *dispPtr = (TkDisplay *) clientData;
    Display *display = dispPtr->display;
    int numFound;

    XFlush(display);
    numFound = XEventsQueued(display, QueuedAfterReading);
    if (numFound == 0) {
	/*
	 * Things are very tricky if there aren't any events readable at this
	 * point (after all, there was supposedly data available on the
	 * connection). A couple of things could have occurred:
	 *
	 * One possibility is that there were only error events in the input
	 * from the server. If this happens, we should return (we don't want
	 * to go to sleep in XNextEvent below, since this would block out
	 * other sources of input to the process).
	 *
	 * Another possibility is that our connection to the server has been
	 * closed. This will not necessarily be detected in XEventsQueued (!!)
	 * so if we just return then there will be an infinite loop. To detect
	 * such an error, generate a NoOp protocol request to exercise the
	 * connection to the server, then return. However, must disable
	 * SIGPIPE while sending the request, or else the process will die
	 * from the signal and won't invoke the X error function to print a
	 * nice (?!) message.
	 */

	XNoOp(display);
	XFlush(display);
    }

    TransferXEventsToTcl(display);
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * TkpDoOneXEvent --
 *
 *	This routine waits for an X event to be processed or for a timeout to
 *	occur. The timeout is specified as an absolute time. This routine is
 *	called when Tk needs to wait for a particular X event without letting
 *	arbitrary events be processed. The caller will typically call
 *	Tk_RestrictEvents to set up an event filter before calling this
 *	routine. This routine will service at most one event per invocation.
 *
 * Results:
 *	Returns 0 if the timeout has expired, otherwise returns 1.
 *
 * Side effects:
 *	Can invoke arbitrary Tcl scripts.
 *
 *----------------------------------------------------------------------
 */

int
TkpDoOneXEvent(
    Tcl_Time *timePtr)		/* Specifies the absolute time when the call
				 * should time out. */
{
    TkDisplay *dispPtr;
    Tcl_Time now;
    int done = 0;
#ifdef _WIN32
    HANDLE *evtPtr = NULL;
#else
    struct timeval blockTime;
    fd_mask readMask[MASK_SIZE];
    int fd, index, numFound, numFdBits = 0;
    fd_mask bit, *readMaskPtr = readMask;
#endif

    /*
     * Look for queued events first.
     */

    if (Tcl_ServiceEvent(TCL_WINDOW_EVENTS)) {
	return 1;
    }

    /*
     * Set up the select mask for all of the displays. If a display has data
     * pending, then we want to poll instead of blocking.
     */

#ifndef _WIN32
    blockTime.tv_sec = 0;
    blockTime.tv_usec = 20000;
    memset(readMask, 0, MASK_SIZE*sizeof(fd_mask));
#endif
    for (dispPtr = TkGetDisplayList(); dispPtr != NULL;
	    dispPtr = dispPtr->nextPtr) {
	XFlush(dispPtr->display);
	if (XEventsQueued(dispPtr->display, QueuedAlready) > 0) {
	    done = 1;
#ifdef _WIN32
	    goto handleEvents;
#else
	    blockTime.tv_sec = 0;
	    blockTime.tv_usec = 0;
#endif
	}
#ifdef _WIN32
	/* Should be only one Display anyway. */
	if (evtPtr == NULL) {
	    evtPtr = (HANDLE*) &dispPtr->display->fd;
	}
	continue;
#else
	fd = ConnectionNumber(dispPtr->display);
	if (fd < 0) {
	    continue;
	}
	index = fd/(NBBY*sizeof(fd_mask));
	bit = ((fd_mask)1) << (fd%(NBBY*sizeof(fd_mask)));
	readMask[index] |= bit;
	if (numFdBits <= fd) {
	    numFdBits = fd+1;
	}
#endif
    }

    do {
#ifdef _WIN32
	HANDLE evtHandle = INVALID_HANDLE_VALUE;

	if (evtPtr != NULL) {
	    evtHandle = *evtPtr;
	}
	if (evtHandle != INVALID_HANDLE_VALUE) {
	    WaitForSingleObject(evtHandle, 10);
	} else {
	    Sleep(10);
	}
handleEvents:
	for (dispPtr = TkGetDisplayList(); dispPtr != NULL;
	     dispPtr = dispPtr->nextPtr) {
	    TransferXEventsToTcl(dispPtr->display);
	}
#else
	numFound = select(numFdBits, (SELECT_MASK *) readMaskPtr, NULL, NULL,
			  &blockTime);
	if (numFound <= 0) {
	    /*
	     * Some systems don't clear the masks after an error,
	     * so we have to do it here.
	     */

	    memset(readMask, 0, MASK_SIZE*sizeof(fd_mask));
	}

	/*
	 * Process any new events on the display connections.
	 */

	for (dispPtr = TkGetDisplayList(); dispPtr != NULL;
	     dispPtr = dispPtr->nextPtr) {
	    fd = ConnectionNumber(dispPtr->display);
	    if (fd < 0) {
		if (XEventsQueued(dispPtr->display, QueuedAlready) > 0) {
		    DisplayFileProc(dispPtr, TCL_READABLE);
		    done = 1;
		    break;
		}
		continue;
	    }
	    index = fd/(NBBY*sizeof(fd_mask));
	    bit = ((fd_mask)1) << (fd%(NBBY*sizeof(fd_mask)));
	    if ((readMask[index] & bit) ||
		(XEventsQueued(dispPtr->display, QueuedAlready) > 0)) {
		DisplayFileProc(dispPtr, TCL_READABLE);
		done = 1;
		break;
	    }
	}
	if (done) {
	    break;
	}
#endif
	if (Tcl_ServiceEvent(TCL_WINDOW_EVENTS)) {
	    return 1;
	}

	/*
	 * Check to see if we timed out.
	 */

	if (timePtr) {
	    Tcl_GetTime(&now);
	    if ((now.sec > timePtr->sec) ||
		((now.sec == timePtr->sec) && (now.usec > timePtr->usec))) {
		return 0;
	    }
	}
#ifndef _WIN32
	blockTime.tv_sec = 0;
	blockTime.tv_usec = 20000;
#endif
    } while (!done);

    /*
     * We had an event but we did not generate a Tcl event from it. Behave as
     * though we dealt with it. (JYL&SS)
     */

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSync --
 *
 *	This routine ensures that all pending X requests have been seen by the
 *	server, and that any pending X events have been moved onto the Tk
 *	event queue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Places new events on the Tk event queue.
 *
 *----------------------------------------------------------------------
 */

void
TkpSync(
    Display *display)		/* Display to sync. */
{
    XSync(display, False);

    /*
     * Transfer events from the X event queue to the Tk event queue.
     */

    TransferXEventsToTcl(display);
}

void
TkpWarpPointer(
    TkDisplay *dispPtr)
{
    Window w;			/* Which window to warp relative to. */

    if (dispPtr->warpWindow != NULL) {
	w = Tk_WindowId(dispPtr->warpWindow);
    } else {
	w = RootWindow(dispPtr->display,
		Tk_ScreenNumber(dispPtr->warpMainwin));
    }
    XWarpPointer(dispPtr->display, None, w, 0, 0, 0, 0,
	    (int) dispPtr->warpX, (int) dispPtr->warpY);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
