/*
 * tkMacOSXNotify.c --
 *
 *	This file contains the implementation of a tcl event source
 *	for the AppKit event loop.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2015 Marc Culler.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkMacOSXInt.h"
#include "tkMacOSXConstants.h"
#import <objc/objc-auto.h>

/* This is not used for anything at the moment. */
typedef struct ThreadSpecificData {
    int initialized;
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

#define TSD_INIT() ThreadSpecificData *tsdPtr = (ThreadSpecificData *) \
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData))

static void TkMacOSXNotifyExitHandler(void *clientData);
static void TkMacOSXEventsSetupProc(void *clientData, int flags);
static void TkMacOSXEventsCheckProc(void *clientData, int flags);

#ifdef TK_MAC_DEBUG_EVENTS
static const char *Tk_EventName[39] = {
    "",
    "",
    "KeyPress",		/*2*/
    "KeyRelease",	/*3*/
    "ButtonPress",	/*4*/
    "ButtonRelease",	/*5*/
    "MotionNotify",	/*6*/
    "EnterNotify",	/*7*/
    "LeaveNotify",	/*8*/
    "FocusIn",		/*9*/
    "FocusOut",		/*10*/
    "KeymapNotify",	/*11*/
    "Expose",		/*12*/
    "GraphicsExpose",	/*13*/
    "NoExpose",		/*14*/
    "VisibilityNotify",	/*15*/
    "CreateNotify",	/*16*/
    "DestroyNotify",	/*17*/
    "UnmapNotify",	/*18*/
    "MapNotify",	/*19*/
    "MapRequest",	/*20*/
    "ReparentNotify",	/*21*/
    "ConfigureNotify",	/*22*/
    "ConfigureRequest",	/*23*/
    "GravityNotify",	/*24*/
    "ResizeRequest",	/*25*/
    "CirculateNotify",	/*26*/
    "CirculateRequest",	/*27*/
    "PropertyNotify",	/*28*/
    "SelectionClear",	/*29*/
    "SelectionRequest",	/*30*/
    "SelectionNotify",	/*31*/
    "ColormapNotify",	/*32*/
    "ClientMessage",	/*33*/
    "MappingNotify",	/*34*/
    "VirtualEvent",	/*35*/
    "ActivateNotify",	/*36*/
    "DeactivateNotify",	/*37*/
    "MouseWheelEvent"	/*38*/
};

static Tk_RestrictAction
InspectQueueRestrictProc(
     void *arg,
     XEvent *eventPtr)
{
    XVirtualEvent* ve = (XVirtualEvent*) eventPtr;
    const char *name;
    long serial = ve->serial;
    long time = eventPtr->xkey.time;

    if (eventPtr->type == VirtualEvent) {
	name = ve->name;
    } else {
	name = Tk_EventName[eventPtr->type];
    }
    fprintf(stderr, "    > %s;serial = %lu; time=%lu)\n",
	    name, serial, time);
    return TK_DEFER_EVENT;
}

/*
 * Debugging tool which prints the current Tcl queue.
 */

void DebugPrintQueue(void)
{
    void *oldArg;
    Tk_RestrictProc *oldProc;

    oldProc = Tk_RestrictEvents(InspectQueueRestrictProc, NULL, &oldArg);
    fprintf(stderr, "Current queue:\n");
    while (Tcl_DoOneEvent(TCL_ALL_EVENTS|TCL_DONT_WAIT)) {};
    Tk_RestrictEvents(oldProc, oldArg, &oldArg);
}
# endif

#pragma mark TKApplication(TKNotify)

@implementation TKApplication(TKNotify)
/*
 * Earlier versions of Tk would override nextEventMatchingMask here, adding a
 * call to displayIfNeeded on all windows after calling super. This would cause
 * windows to be redisplayed (if necessary) each time that an event was
 * received.  This was intended to replace Apple's default autoDisplay
 * mechanism, which the earlier versions of Tk would disable.  When autoDisplay
 * is set to the default value of YES, the Apple event loop will call
 * displayIfNeeded on all windows at the beginning of each iteration of their
 * event loop.  Since Tk does not call the Apple event loop, it was thought
 * that the autoDisplay behavior needed to be replicated.
 *
 * However, as of OSX 10.14 (Mojave) the autoDisplay property became
 * deprecated.  Luckily it turns out that, even though we don't ever start the
 * Apple event loop, the Apple window manager still calls displayIfNeeded on
 * all windows on a regular basis, perhaps each time the queue is empty.  So we
 * no longer, and perhaps never did need to set autoDisplay to NO, nor call
 * displayIfNeeded on our windows.  We can just leave all of that to the window
 * manager.
 */

/*
 * Since the contentView is the first responder for a Tk Window, it is
 * responsible for sending events up the responder chain.  We also check the
 * pasteboard here.
 */
- (void) sendEvent: (NSEvent *) theEvent
{

    /*
     * Workaround for an Apple bug.  When an accented character is selected
     * from an NSTextInputClient popup character viewer with the mouse, Apple
     * sends an event of type NSAppKitDefined and subtype 21. If that event is
     * sent up the responder chain it causes Apple to print a warning to the
     * console log and, extremely obnoxiously, also to stderr, which says
     * "Window move completed without beginning."  Apparently they are sending
     * the "move completed" event without having sent the "move began" event of
     * subtype 20, and then announcing their error on our stderr.  Also, of
     * course, no movement is occurring.  The popup is not movable and is just
     * being closed.  The bug has been reported to Apple.  If they ever fix it,
     * this block should be removed.
     */

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101500
    if ([theEvent type] == NSAppKitDefined) {
	static Bool aWindowIsMoving = NO;
	switch([theEvent subtype]) {
	case 20:
	    aWindowIsMoving = YES;
	    break;
	case 21:
	    if (aWindowIsMoving) {
		aWindowIsMoving = NO;
		break;
	    } else {
		// printf("Bug!!!!\n");
		return;
	    }
	default:
	    break;
	}
    }
#endif

    [super sendEvent:theEvent];
    [NSApp tkCheckPasteboard];

#ifdef TK_MAC_DEBUG_EVENTS
    fprintf(stderr, "Sending event of type %d\n", (int)[theEvent type]);
    DebugPrintQueue();
#endif

}

- (void) _runBackgroundLoop
{
    while(Tcl_DoOneEvent(TCL_IDLE_EVENTS|TCL_TIMER_EVENTS|TCL_DONT_WAIT)){
    }
}
@end

#pragma mark -

/*
 *----------------------------------------------------------------------
 *
 * GetRunLoopMode --
 *
 * Results:
 *	RunLoop mode that should be passed to -nextEventMatchingMask:
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static NSString *
GetRunLoopMode(NSModalSession modalSession)
{
    NSString *runLoopMode = nil;

    if (modalSession) {
	runLoopMode = NSModalPanelRunLoopMode;
    }
    if (!runLoopMode) {
	runLoopMode = [[NSRunLoop currentRunLoop] currentMode];
    }
    if (!runLoopMode) {
	runLoopMode = NSDefaultRunLoopMode;
    }
    return runLoopMode;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MacOSXSetupTkNotifier --
 *
 *	This procedure is called during Tk initialization to create the event
 *	source for TkAqua events.
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
Tk_MacOSXSetupTkNotifier(void)
{
    TSD_INIT();

    if (!tsdPtr->initialized) {
	tsdPtr->initialized = 1;

	/*
	 * Install TkAqua event source in main event loop thread.
	 */

	if (CFRunLoopGetMain() == CFRunLoopGetCurrent()) {
	    if (![NSThread isMainThread]) {
		/*
		 * Panic if main runloop is not on the main application thread.
		 */

		Tcl_Panic("Tk_MacOSXSetupTkNotifier: %s",
		    "first [load] of TkAqua has to occur in the main thread!");
	    }
	    Tcl_CreateEventSource(TkMacOSXEventsSetupProc,
		    TkMacOSXEventsCheckProc, NULL);
	    TkCreateExitHandler(TkMacOSXNotifyExitHandler, NULL);
	    Tcl_MacOSXNotifierAddRunLoopMode(NSEventTrackingRunLoopMode);
	    Tcl_MacOSXNotifierAddRunLoopMode(NSModalPanelRunLoopMode);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXNotifyExitHandler --
 *
 *	This function is called during finalization to clean up the
 *	TkMacOSXNotify module.
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
TkMacOSXNotifyExitHandler(
    TCL_UNUSED(void *))	/* Not used. */
{
    TSD_INIT();

    Tcl_DeleteEventSource(TkMacOSXEventsSetupProc,
	    TkMacOSXEventsCheckProc, NULL);
    tsdPtr->initialized = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXEventsSetupProc --
 *
 *	This procedure implements the setup part of the MacOSX event source. It
 *	is invoked by Tcl_DoOneEvent before calling TkMacOSXEventsCheckProc to
 *	process all queued NSEvents.  In our case, all we need to do is to set
 *	the Tcl MaxBlockTime to 0 before starting the loop to process all
 *	queued NSEvents.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *
 *	If NSEvents are queued, or if there is any drawing that needs to be
 *      done, then the maximum block time will be set to 0 to ensure that
 *      Tcl_WaitForEvent returns immediately.
 *
 *----------------------------------------------------------------------
 */

#define TICK 200
static Tcl_TimerToken ticker = NULL;

static void
Heartbeat(
    TCL_UNUSED(void *))
{

    if (ticker) {
	ticker = Tcl_CreateTimerHandler(TICK, Heartbeat, NULL);
    }
}

static const Tcl_Time zeroBlockTime = { 0, 0 };

static void
TkMacOSXEventsSetupProc(
    TCL_UNUSED(void *),
    int flags)
{
    NSString *runloopMode = [[NSRunLoop currentRunLoop] currentMode];

    /*
     * runloopMode will be nil if we are in a Tcl event loop.
     */

    if (flags & TCL_WINDOW_EVENTS && !runloopMode) {

	[NSApp _resetAutoreleasePool];

	/*
	 * After calling this setup proc, Tcl_DoOneEvent will call
	 * Tcl_WaitForEvent.  Then it will call check proc to collect the
	 * events and translate them into XEvents.
	 *
	 * If we have any events waiting or if there is any drawing to be done
	 * we want Tcl_WaitForEvent to return immediately.  So we set the block
	 * time to 0 and stop the heartbeat.
	 */

	NSEvent *currentEvent =
		[NSApp nextEventMatchingMask:NSAnyEventMask
			untilDate:[NSDate distantPast]
			inMode:GetRunLoopMode(TkMacOSXGetModalSession())
			dequeue:NO];
	if ((currentEvent)) {
	    Tcl_SetMaxBlockTime(&zeroBlockTime);
	    Tcl_DeleteTimerHandler(ticker);
	    ticker = NULL;
	} else if (ticker == NULL) {

	    /*
	     * When the user is not generating events we schedule a "heartbeat"
	     * TimerHandler to fire every 200 milliseconds.  The handler does
	     * nothing, but when its timer fires it causes Tcl_WaitForEvent to
	     * return.  This helps avoid hangs when calling vwait during the
	     * non-regression tests.
	     */

	    ticker = Tcl_CreateTimerHandler(TICK, Heartbeat, NULL);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXEventsCheckProc --
 *
 *	This procedure loops through all NSEvents waiting in the TKApplication
 *      event queue, generating X events from them.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	NSevents are used to generate X events, which are added to the Tcl
 *      event queue.
 *
 *----------------------------------------------------------------------
 */
static void
TkMacOSXEventsCheckProc(
    TCL_UNUSED(void *),
    int flags)
{
    NSString *runloopMode = [[NSRunLoop currentRunLoop] currentMode];

    /*
     * runloopMode will be nil if we are in a Tcl event loop.
     */

    if (flags & TCL_WINDOW_EVENTS && !runloopMode) {
	NSEvent *currentEvent = nil;
	NSEvent *testEvent = nil;
	NSModalSession modalSession;

	/*
	 * It is possible for the SetupProc to be called before this function
	 * returns.  This happens, for example, when we process an event which
	 * opens a modal window.  To prevent premature release of our
	 * application-wide autorelease pool by a nested call to the SetupProc,
	 * we must lock it here.
	 */

	[NSApp _lockAutoreleasePool];
	do {
	    modalSession = TkMacOSXGetModalSession();
	    testEvent = [NSApp nextEventMatchingMask:NSAnyEventMask
		    untilDate:[NSDate distantPast]
		    inMode:GetRunLoopMode(modalSession)
		    dequeue:NO];

	    /*
	     * We must not steal any events during LiveResize.
	     */

	    if (testEvent && [[testEvent window] inLiveResize]) {
		break;
	    }
	    currentEvent = [NSApp nextEventMatchingMask:NSAnyEventMask
		    untilDate:[NSDate distantPast]
		    inMode:GetRunLoopMode(modalSession)
		    dequeue:YES];
	    if (currentEvent) {

		/*
		 * Generate Xevents.
		 */

		NSEvent *processedEvent = [NSApp tkProcessEvent:currentEvent];
		if (processedEvent) {

#ifdef TK_MAC_DEBUG_EVENTS
		    TKLog(@"   event: %@", currentEvent);
#endif

		    if (modalSession) {
			[NSApp _modalSession:modalSession sendEvent:currentEvent];
		    } else {
			[NSApp sendEvent:currentEvent];
		    }
		}
	    } else {
		break;
	    }
	} while (1);

	/*
	 * Now we can unlock the pool.
	 */

	[NSApp _unlockAutoreleasePool];
    }
}

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
