/*
 * tkMacOSXWindowEvent.c --
 *
 *	This file defines the routines for both creating and handling Window
 *	Manager class events for Tk.
 *
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2015 Kevin Walzer/WordTech Communications LLC.
 * Copyright © 2015 Marc Culler.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkMacOSXWm.h"
#include "tkMacOSXInt.h"
#include "tkMacOSXDebug.h"
#include "tkMacOSXConstants.h"

/*
#ifdef TK_MAC_DEBUG
#define TK_MAC_DEBUG_EVENTS
#define TK_MAC_DEBUG_DRAWING
#endif
*/

/*
 * Declaration of functions used only in this file
 */

static int		GenerateUpdates(
			    CGRect *updateBounds, TkWindow *winPtr);
static int		GenerateActivateEvents(TkWindow *winPtr,
			    int activeFlag);

#pragma mark TKApplication(TKWindowEvent)

extern NSString *NSWindowDidOrderOnScreenNotification;
extern NSString *NSWindowWillOrderOnScreenNotification;

#ifdef TK_MAC_DEBUG_NOTIFICATIONS
extern NSString *NSWindowDidOrderOffScreenNotification;
#endif


@implementation TKApplication(TKWindowEvent)

- (void) windowActivation: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), notification);
#endif
    NSWindow *w = [notification object];
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);
    NSString *name = [notification name];
    Bool flag = [name isEqualToString:NSWindowDidBecomeKeyNotification];
    if (winPtr && flag) {
	NSPoint location = [NSEvent mouseLocation];
	int x = location.x;
	int y = floor(TkMacOSXZeroScreenHeight() - location.y);
	/*
	 * The Tk event target persists when there is no key window but
	 * gets reset when a new window becomes the key window.
	 */

	[NSApp setTkEventTarget: winPtr];

	/*
	 * Call Tk_UpdatePointer if the pointer is in the window.
	 */

	NSView *view = [w contentView];
	NSPoint viewLocation = [view convertPoint:location fromView:nil];
	if (NSPointInRect(viewLocation, NSInsetRect([view bounds], 2, 2))) {
	    Tk_UpdatePointer((Tk_Window) winPtr, x, y, [NSApp tkButtonState]);
	}
    }
    if (winPtr && Tk_IsMapped(winPtr)) {
	GenerateActivateEvents(winPtr, flag);
    }
}

- (void) windowBoundsChanged: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), notification);
#endif
    BOOL movedOnly = [[notification name]
	    isEqualToString:NSWindowDidMoveNotification];
    NSWindow *w = [notification object];
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);

    if (winPtr) {
	WmInfo *wmPtr = winPtr->wmInfoPtr;
	NSRect bounds = [w frame];
	int x, y, width = -1, height = -1, flags = 0;

	x = bounds.origin.x;
	y = TkMacOSXZeroScreenHeight() - (bounds.origin.y + bounds.size.height);
	if (winPtr->changes.x != x || winPtr->changes.y != y) {
	    flags |= TK_LOCATION_CHANGED;
	} else {
	    x = y = -1;
	}
	if (!movedOnly && (winPtr->changes.width != bounds.size.width ||
		winPtr->changes.height !=  bounds.size.height)) {
	    width = bounds.size.width - wmPtr->xInParent;
	    height = bounds.size.height - wmPtr->yInParent;
	    flags |= TK_SIZE_CHANGED;
	}
	/*
	 * Propagate geometry changes immediately.
	 */

	flags |= TK_MACOSX_HANDLE_EVENT_IMMEDIATELY;
	TkGenWMConfigureEvent((Tk_Window)winPtr, x, y, width, height, flags);
    }

}

- (void) windowExpanded: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), notification);
#endif
    NSWindow *w = [notification object];
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);

    if (winPtr) {
	winPtr->wmInfoPtr->hints.initial_state =
		TkMacOSXIsWindowZoomed(winPtr) ? ZoomState : NormalState;
	Tk_MapWindow((Tk_Window)winPtr);

	/*
	 * NSWindowDidDeminiaturizeNotification is received after
	 * NSWindowDidBecomeKeyNotification, so activate manually
	 */

	GenerateActivateEvents(winPtr, 1);
    }
}

- (NSRect)windowWillUseStandardFrame:(NSWindow *)window
			defaultFrame:(NSRect)newFrame
{
    (void)window;

    /*
     * This method needs to be implemented in order for [NSWindow isZoomed] to
     * give the correct answer. But it suffices to always validate every
     * request.
     */

    return newFrame;
}

- (NSSize)window:(NSWindow *)window
  willUseFullScreenContentSize:(NSSize)proposedSize
{
    (void)window;

    /*
     * We don't need to change the proposed size, but we do need to implement
     * this method.  Otherwise the full screen window will be sized to the
     * screen's visibleFrame, leaving black bands at the top and bottom.
     */

    return proposedSize;
}

- (void) windowEnteredFullScreen: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), notification);
#endif
    if (![[notification object] respondsToSelector: @selector (tkLayoutChanged)]) {
	return;
    }
    [(TKWindow *)[notification object] tkLayoutChanged];
}

- (void) windowExitedFullScreen: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), notification);
#endif
    if (![[notification object] respondsToSelector: @selector (tkLayoutChanged)]) {
	return;
    }
    [(TKWindow *)[notification object] tkLayoutChanged];
}

- (void) windowCollapsed: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), notification);
#endif
    NSWindow *w = [notification object];
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);

    if (winPtr) {
	winPtr->wmInfoPtr->hints.initial_state = IconicState;
	Tk_UnmapWindow((Tk_Window)winPtr);
    }
}

- (BOOL) windowShouldClose: (NSWindow *) w
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), w);
#endif
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);

    if (winPtr) {
	TkGenWMDestroyEvent((Tk_Window)winPtr);
    }

    /*
     * If necessary, TkGenWMDestroyEvent() handles [close]ing the window, so
     * can always return NO from -windowShouldClose: for a Tk window.
     */

    return (winPtr ? NO : YES);
}

- (void) windowBecameVisible: (NSNotification *) notification
{
    NSWindow *window = [notification object];
    TkWindow *winPtr = TkMacOSXGetTkWindow(window);
    if (winPtr) {
	TKContentView *view = [window contentView];
	// fprintf(stderr, "Window %s became visible.\n", Tk_PathName(winPtr));

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
	if (@available(macOS 10.14, *)) {
	    [view viewDidChangeEffectiveAppearance];
	}
#endif
	[view setNeedsDisplay:YES];
    }
}

- (void) windowMapped: (NSNotification *) notification
{
    NSWindow *w = [notification object];
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);

    if (winPtr) {
	// fprintf(stderr, "Window %s was ordered on screen.\n", Tk_PathName(winPtr));
    }
}

- (void) windowLiveResize: (NSNotification *) notification
{
    NSString *name = [notification name];
    if ([name isEqualToString:NSWindowWillStartLiveResizeNotification]) {
	// fprintf(stderr, "Starting live resize.\n");
    } else if ([name isEqualToString:NSWindowDidEndLiveResizeNotification]) {
	[self setTkLiveResizeEnded:YES];
	// fprintf(stderr, "Ending live resize\n");
    }
}

#ifdef TK_MAC_DEBUG_NOTIFICATIONS

- (void) windowDragStart: (NSNotification *) notification
{
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), notification);
}

- (void) windowUnmapped: (NSNotification *) notification
{
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), notification);
    NSWindow *w = [notification object];
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);

#if 0
    if (winPtr) {
	Tk_UnmapWindow((Tk_Window)winPtr);
    }
#endif
}

#endif /* TK_MAC_DEBUG_NOTIFICATIONS */

- (void) _setupWindowNotifications
{
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];

#define observe(n, s) \
	[nc addObserver:self selector:@selector(s) name:(n) object:nil]

    observe(NSWindowDidBecomeKeyNotification, windowActivation:);
    observe(NSWindowDidResignKeyNotification, windowActivation:);
    observe(NSWindowDidMoveNotification, windowBoundsChanged:);
    observe(NSWindowDidResizeNotification, windowBoundsChanged:);
    observe(NSWindowDidDeminiaturizeNotification, windowExpanded:);
    observe(NSWindowDidMiniaturizeNotification, windowCollapsed:);
    observe(NSWindowWillOrderOnScreenNotification, windowMapped:);
    observe(NSWindowDidOrderOnScreenNotification, windowBecameVisible:);
    observe(NSWindowWillStartLiveResizeNotification, windowLiveResize:);
    observe(NSWindowDidEndLiveResizeNotification, windowLiveResize:);
    observe(NSWindowDidEnterFullScreenNotification, windowEnteredFullScreen:);
    observe(NSWindowDidExitFullScreenNotification, windowExitedFullScreen:);

#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    observe(NSWindowWillMoveNotification, windowDragStart:);
    observe(NSWindowDidOrderOffScreenNotification, windowUnmapped:);
#endif
#undef observe

}
@end


/*
 * Idle task which forces focus to a particular window.
 */

static void RefocusGrabWindow(void *data) {
    TkWindow *winPtr = (TkWindow *) data;
    TkpChangeFocus(winPtr, 1);
}

#pragma mark TKApplication(TKApplicationEvent)

@implementation TKApplication(TKApplicationEvent)

- (void) applicationActivate: (NSNotification *) notification
{
    (void)notification;

#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), notification);
#endif
    [NSApp tkCheckPasteboard];

    /*
     * When the application is activated with Command-Tab it will create a
     * zombie window for every Tk window which has been withdrawn.  So iterate
     * through the list of windows and order out any withdrawn window.
     * If one of the windows is the grab window for its display we focus
     * it.  This is done as at idle, in case the app was reactivated by
     * clicking a different window.  In that case we need to wait until the
     * mouse event has been processed before focusing the grab window.
     */

    for (NSWindow *win in [NSApp windows]) {
	TkWindow *winPtr = TkMacOSXGetTkWindow(win);
	if (!winPtr || !winPtr->wmInfoPtr) {
	    continue;
	}
	if (winPtr->wmInfoPtr->hints.initial_state == WithdrawnState) {
	    [win orderOut:NSApp];
	}
	if (winPtr->dispPtr->grabWinPtr == winPtr) {
	    Tcl_DoWhenIdle(RefocusGrabWindow, winPtr);
	} else {
	    [[self keyWindow] orderFront: self];
	}
    }
}

- (void) applicationDeactivate: (NSNotification *) notification
{
    (void)notification;

#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), notification);
#endif

    /*
     * To prevent zombie windows on systems with a TouchBar, set the key window
     * to nil if the current key window is not visible.  This allows a closed
     * Help or About window to be deallocated so it will not reappear as a
     * zombie when the app is reactivated.
     */

    NSWindow *keywindow = [NSApp keyWindow];
    if (keywindow && ![keywindow isVisible]) {
	[NSApp _setKeyWindow:nil];
	[NSApp _setMainWindow:nil];
    }

}

- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender
		    hasVisibleWindows:(BOOL)flag
{
    (void)sender;
    (void)flag;

    /*
     * Allowing the default response means that withdrawn windows will get
     * displayed on the screen with unresponsive title buttons.  We don't
     * really want that.  Besides, we can write our own code to handle this
     * with ::tk::mac::ReopenApplication.  So we just say NO.
     */

    return NO;
}


- (void) applicationShowHide: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), notification);
#endif
    const char *cmd = ([[notification name] isEqualToString:
	    NSApplicationDidUnhideNotification] ?
	    "::tk::mac::OnShow" : "::tk::mac::OnHide");

    if (_eventInterp && Tcl_FindCommand(_eventInterp, cmd, NULL, 0)) {
	int code = Tcl_EvalEx(_eventInterp, cmd, TCL_INDEX_NONE, TCL_EVAL_GLOBAL);

	if (code != TCL_OK) {
	    Tcl_BackgroundException(_eventInterp, code);
	}
	Tcl_ResetResult(_eventInterp);
    }
}

- (void) displayChanged: (NSNotification *) notification
{
    (void)notification;

#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), notification);
#endif
    TkDisplay *dispPtr = TkGetDisplayList();

    if (dispPtr) {
	TkMacOSXDisplayChanged(dispPtr->display);
    }
}
@end

#pragma mark -
 
/*
 *----------------------------------------------------------------------
 *
 * TkpWillDrawWidget --
 *
 *      A widget display procedure can call this to determine whether it is
 *      being run inside of the drawRect method. If not, it may be desirable
 *      for the display procedure to simply clear the REDRAW_PENDING flag
 *      and return.  The widget can be recorded in order to schedule a
 *      redraw, via an Expose event, from within drawRect.
 *
 *      This is also needed for some tests, especially of the Text widget,
 *      which record data in a global Tcl variable and assume that display
 *      procedures will be run in a predictable sequence as Tcl idle tasks.
 *
 * Results:
 *      True if called from the drawRect method of a TKContentView with
 *      tkwin NULL or pointing to a widget in the current focusView.
 *
 * Side effects:
 *	Currently none.  One day the tkwin parameter may be recorded to
 *      handle redrawing the widget later.
 *
 *----------------------------------------------------------------------
 */
// This stub is no longer used, but is expected by the stub mechanism.
int
TkpWillDrawWidget(Tk_Window tkwin) {
    (void) tkwin;
    return false;
}
 
/*
 *----------------------------------------------------------------------
 *
 * GenerateUpdates --
 *
 *	Given an update rectangle and a Tk window, this function generates
 *	an X Expose event for the window if it meets the update region. The
 *	function will then recursively have each damaged window generate Expose
 *	events for its child windows.
 *
 * Results:
 *	True if event(s) are generated - false otherwise.
 *
 * Side effects:
 *	Additional events may be placed on the Tk event queue.
 *
 *----------------------------------------------------------------------
 */

static int
GenerateUpdates(
    CGRect *updateBounds,
    TkWindow *winPtr)
{
    TkWindow *childPtr;
    XEvent event;
    CGRect bounds, damageBounds;
    NSView *view = TkMacOSXGetNSViewForDrawable((Drawable)winPtr->privatePtr);

    TkMacOSXWinCGBounds(winPtr, &bounds);
#if 0
    if (!CGRectIntersectsRect(bounds, *updateBounds)) {
	return 0;
    }
#endif

    /*
     * Compute the bounding box of the area that the damage occurred in.
     */

    damageBounds = CGRectIntersection(bounds, *updateBounds);
    event.xany.serial = LastKnownRequestProcessed(Tk_Display(winPtr));
    event.xany.send_event = false;
    event.xany.window = Tk_WindowId(winPtr);
    event.xany.display = Tk_Display(winPtr);
    event.type = Expose;
    event.xexpose.x = damageBounds.origin.x - bounds.origin.x;
    event.xexpose.y = damageBounds.origin.y - bounds.origin.y;
    event.xexpose.width = damageBounds.size.width;
    event.xexpose.height = damageBounds.size.height;
    event.xexpose.count = 0;
    if ([view inLiveResize]) {
	Tk_HandleEvent(&event);
    } else {
	Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    }

#ifdef TK_MAC_DEBUG_DRAWING
    TKLog(@"Exposed %p {{%d, %d}, {%d, %d}}", event.xany.window, event.xexpose.x,
	event.xexpose.y, event.xexpose.width, event.xexpose.height);
#endif

    /*
     * Generate updates for the children of this window
     */

    for (childPtr = winPtr->childList; childPtr != NULL;
	    childPtr = childPtr->nextPtr) {
	if (!Tk_IsMapped(childPtr) || Tk_IsTopLevel(childPtr)) {
	    continue;
	}
	GenerateUpdates(updateBounds, childPtr);
    }

    /*
     * Generate updates for any contained windows
     */

    if (Tk_IsContainer(winPtr)) {
	childPtr = (TkWindow *)Tk_GetOtherWindow((Tk_Window)winPtr);
	if (childPtr != NULL && Tk_IsMapped(childPtr)) {
	    GenerateUpdates(updateBounds, childPtr);
	}

	/*
	 * TODO: Here we should handle out of process embedding.
	 */
    }

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXGenerateFocusEvent --
 *
 *	Given a Macintosh window activate event this function generates all
 *	the X Focus events needed by Tk.
 *
 * Results:
 *	True if event(s) are generated - false otherwise.
 *
 * Side effects:
 *	Additional events may be placed on the Tk event queue.
 *
 *----------------------------------------------------------------------
 */

static int
TkMacOSXGenerateFocusEvent(
    TkWindow *winPtr,		/* Root X window for event. */
    int activeFlag)
{
    XEvent event;

    /*
     * Don't send focus events to windows of class help or to windows with the
     * kWindowNoActivatesAttribute.
     */

    if (winPtr->wmInfoPtr && (winPtr->wmInfoPtr->macClass == kHelpWindowClass ||
	    winPtr->wmInfoPtr->attributes & kWindowNoActivatesAttribute)) {
	return false;
    }

    /*
     * Generate FocusIn and FocusOut events. This event is only sent to the
     * toplevel window.
     */

    if (activeFlag) {
	event.xany.type = FocusIn;
    } else {
	event.xany.type = FocusOut;
    }

    event.xany.serial = LastKnownRequestProcessed(Tk_Display(winPtr));
    event.xany.send_event = False;
    event.xfocus.display = Tk_Display(winPtr);
    event.xfocus.window = winPtr->window;
    event.xfocus.mode = NotifyNormal;
    event.xfocus.detail = NotifyDetailNone;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * GenerateActivateEvents --
 *
 *	Given a Macintosh window activate event this function generates all the
 *	X Activate events needed by Tk.
 *
 * Results:
 *	True if event(s) are generated - false otherwise.
 *
 * Side effects:
 *	Additional events may be placed on the Tk event queue.
 *
 *----------------------------------------------------------------------
 */

int
GenerateActivateEvents(
    TkWindow *winPtr,
    int activeFlag)
{
    TkGenerateActivateEvents(winPtr, activeFlag);
    if (activeFlag || ![NSApp isActive]) {
	TkMacOSXGenerateFocusEvent(winPtr, activeFlag);
    }
    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGenWMConfigureEvent --
 *
 *	Generate a ConfigureNotify event for Tk. Depending on the value of flag
 *	the values of width/height, x/y, or both may be changed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A ConfigureNotify event is sent to Tk.
 *
 *----------------------------------------------------------------------
 */

void
TkGenWMConfigureEvent(
    Tk_Window tkwin,
    int x, int y,
    int width, int height,
    int flags)
{
    XEvent event;
    WmInfo *wmPtr;
    TkWindow *winPtr = (TkWindow *) tkwin;

    if (tkwin == NULL) {
	return;
    }

    event.type = ConfigureNotify;
    event.xconfigure.serial = LastKnownRequestProcessed(Tk_Display(tkwin));
    event.xconfigure.send_event = False;
    event.xconfigure.display = Tk_Display(tkwin);
    event.xconfigure.event = Tk_WindowId(tkwin);
    event.xconfigure.window = Tk_WindowId(tkwin);
    event.xconfigure.border_width = winPtr->changes.border_width;
    event.xconfigure.override_redirect = winPtr->atts.override_redirect;
    if (winPtr->changes.stack_mode == Above) {
	event.xconfigure.above = winPtr->changes.sibling;
    } else {
	event.xconfigure.above = None;
    }

    if (!(flags & TK_LOCATION_CHANGED)) {
	x = Tk_X(tkwin);
	y = Tk_Y(tkwin);
    }
    if (!(flags & TK_SIZE_CHANGED)) {
	width = Tk_Width(tkwin);
	height = Tk_Height(tkwin);
    }
    event.xconfigure.x = x;
    event.xconfigure.y = y;
    event.xconfigure.width = width;
    event.xconfigure.height = height;

    if (flags & TK_MACOSX_HANDLE_EVENT_IMMEDIATELY) {
	Tk_HandleEvent(&event);
    } else {
	Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    }

    /*
     * Update window manager information.
     */

    if (Tk_IsTopLevel(winPtr)) {
	wmPtr = winPtr->wmInfoPtr;
	if (flags & TK_LOCATION_CHANGED) {
	    wmPtr->x = x;
	    wmPtr->y = y;
	}
	if ((flags & TK_SIZE_CHANGED) && !(wmPtr->flags & WM_SYNC_PENDING) &&
		((width != Tk_Width(tkwin)) || (height != Tk_Height(tkwin)))) {
	    if ((wmPtr->width == -1) && (width == winPtr->reqWidth)) {
		/*
		 * Don't set external width, since the user didn't change it
		 * from what the widgets asked for.
		 */
	    } else if (wmPtr->gridWin != NULL) {
		wmPtr->width = wmPtr->reqGridWidth
			+ (width - winPtr->reqWidth)/wmPtr->widthInc;
		if (wmPtr->width < 0) {
		    wmPtr->width = 0;
		}
	    } else {
		wmPtr->width = width;
	    }

	    if ((wmPtr->height == -1) && (height == winPtr->reqHeight)) {
		/*
		 * Don't set external height, since the user didn't change it
		 * from what the widgets asked for.
		 */
	    } else if (wmPtr->gridWin != NULL) {
		wmPtr->height = wmPtr->reqGridHeight
			+ (height - winPtr->reqHeight)/wmPtr->heightInc;
		if (wmPtr->height < 0) {
		    wmPtr->height = 0;
		}
	    } else {
		wmPtr->height = height;
	    }

	    wmPtr->configWidth = width;
	    wmPtr->configHeight = height;
	}
    }

    /*
     * Now set up the changes structure. Under X we wait for the
     * ConfigureNotify to set these values. On the Mac we know immediately that
     * this is what we want - so we just set them. However, we need to make
     * sure the windows clipping region is marked invalid so the change is
     * visible to the subwindow.
     */

    winPtr->changes.x = x;
    winPtr->changes.y = y;
    winPtr->changes.width = width;
    winPtr->changes.height = height;
    TkMacOSXInvalClipRgns(tkwin);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGenWMDestroyEvent --
 *
 *	Generate a WM Destroy event for Tk.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A WM_PROTOCOL/WM_DELETE_WINDOW event is sent to Tk.
 *
 *----------------------------------------------------------------------
 */

void
TkGenWMDestroyEvent(
    Tk_Window tkwin)
{
    XEvent event;

    event.xany.serial = LastKnownRequestProcessed(Tk_Display(tkwin));
    event.xany.send_event = False;
    event.xany.display = Tk_Display(tkwin);

    event.xclient.window = Tk_WindowId(tkwin);
    event.xclient.type = ClientMessage;
    event.xclient.message_type = Tk_InternAtom(tkwin, "WM_PROTOCOLS");
    event.xclient.format = 32;
    event.xclient.data.l[0] = Tk_InternAtom(tkwin, "WM_DELETE_WINDOW");
    Tk_HandleEvent(&event);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmProtocolEventProc --
 *
 *	This procedure is called by the Tk_HandleEvent whenever a ClientMessage
 *	event arrives whose type is "WM_PROTOCOLS". This procedure handles the
 *	message from the window manager in an appropriate fashion.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on what sort of handler, if any, was set up for the protocol.
 *
 *----------------------------------------------------------------------
 */

void
TkWmProtocolEventProc(
    TkWindow *winPtr,		/* Window to which the event was sent. */
    XEvent *eventPtr)		/* X event. */
{
    WmInfo *wmPtr;
    ProtocolHandler *protPtr;
    Tcl_Interp *interp;
    Atom protocol;
    int result;

    wmPtr = winPtr->wmInfoPtr;
    if (wmPtr == NULL) {
	return;
    }
    protocol = (Atom) eventPtr->xclient.data.l[0];
    for (protPtr = wmPtr->protPtr; protPtr != NULL;
	    protPtr = protPtr->nextPtr) {
	if (protocol == protPtr->protocol) {
	    Tcl_Preserve(protPtr);
	    interp = protPtr->interp;
	    Tcl_Preserve(interp);
	    result = Tcl_EvalEx(interp, protPtr->command, TCL_INDEX_NONE, TCL_EVAL_GLOBAL);
	    if (result != TCL_OK) {
		Tcl_AppendObjToErrorInfo(interp, Tcl_ObjPrintf(
			"\n    (command for \"%s\" window manager protocol)",
			Tk_GetAtomName((Tk_Window)winPtr, protocol)));
		Tcl_BackgroundException(interp, result);
	    }
	    Tcl_Release(interp);
	    Tcl_Release(protPtr);
	    return;
	}
    }

    /*
     * No handler was present for this protocol. If this is a WM_DELETE_WINDOW
     * message then just destroy the window.
     */

    if (protocol == Tk_InternAtom((Tk_Window)winPtr, "WM_DELETE_WINDOW")) {
	Tk_DestroyWindow((Tk_Window)winPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MacOSXIsAppInFront --
 *
 *	Returns 1 if this app is the foreground app.
 *
 * Results:
 *	1 if app is in front, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_MacOSXIsAppInFront(void)
{
    return ([NSRunningApplication currentApplication].active == true);
}

#pragma mark TKContentView

#import <ApplicationServices/ApplicationServices.h>

/*
 * Custom content view for use in Tk NSWindows.
 *
 * Since Tk handles all drawing of widgets, we only use the AppKit event loop
 * as a source of input events.  To do this, we overload the NSView drawRect
 * method with a method which generates Expose events for Tk but does no
 * drawing.  The redrawing operations are then done when Tk processes these
 * events.
 *
 * Earlier versions of Mac Tk used subclasses of NSView, e.g. NSButton, as the
 * basis for Tk widgets.  These would then appear as subviews of the
 * TKContentView.  To prevent the AppKit from redrawing and corrupting the Tk
 * Widgets it was necessary to use Apple private API calls.  In order to avoid
 * using private API calls, the NSView-based widgets have been replaced with
 * normal Tk widgets which draw themselves as native widgets by using the
 * HITheme API.
 *
 */

/*
 * Restrict event processing to Expose events.
 */

static Tk_RestrictAction
ExposeRestrictProc(
    void *arg,
    XEvent *eventPtr)
{
    return (eventPtr->type==Expose && eventPtr->xany.serial==PTR2UINT(arg)
	    ? TK_PROCESS_EVENT : TK_DEFER_EVENT);
}

@implementation TKContentView(TKWindowEvent)

- (id)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
	self.wantsLayer = YES;
	self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
	self.layer.contentsGravity = self.layer.contentsAreFlipped ?
	    kCAGravityTopLeft : kCAGravityBottomLeft;
	trackingArea = [[NSTrackingArea alloc]
			   initWithRect:[self bounds]
				options:(NSTrackingMouseEnteredAndExited |
					 NSTrackingMouseMoved |
					 NSTrackingEnabledDuringMouseDrag |
					 NSTrackingInVisibleRect |
					 NSTrackingActiveAlways)
				  owner:self
			       userInfo:nil];
	[self addTrackingArea:trackingArea];
    }
    return self;
}

- (BOOL) wantsUpdateLayer
{
    return YES;
}
- (void) updateLayer {
    CGContextRef context = self.tkLayerBitmapContext;
    if (context && ![NSApp tkWillExit]) {
	/*
	 * Create a CGImage by copying (probably using copy-on-write) the
	 * bitmap data of the CGBitmapContext that we have been using for
	 * drawing.  Then render that CGImage into the CALayer of this view by
	 * assigning a reference to the CGImage to the contents property of the
	 * layer. This will cause all drawing done since the last call to this
	 * function to become visible.
	 */

	CGImageRef newImg = CGBitmapContextCreateImage(context);
	self.layer.contents = (__bridge id) newImg;
	CGImageRelease(newImg); // will quickly leak memory if this is missing

	/*
	 * Run any pending widget display procs as part of the update.
	 * Without this there are black flashes when a window opens.
	 */

	while(Tcl_DoOneEvent(TCL_IDLE_EVENTS)){}
    }
}

- (void) viewDidChangeBackingProperties
{

    /*
     * Make sure that the layer uses a contentScale that matches the
     * backing scale factor of the screen.  This avoids blurry text when
     * the view is on a Retina display, as well as incorrect size when
     * the view is on a normal display.
     */

    self.layer.contentsScale = self.window.screen.backingScaleFactor;
    [self resetTkLayerBitmapContext];
    // need to redraw
    [self generateExposeEvents: self.bounds];
}

-(void) setFrameSize: (NSSize)newsize
{
    NSSize oldsize = self.bounds.size;
    [super setFrameSize: newsize];
    if ((newsize.width == 1 && newsize.height == 1) ||
	(oldsize.width == 0 && oldsize.height == 0)) {
	return;
    }
    NSWindow *w = [self window];
    TkWindow *winPtr = TkMacOSXGetTkWindow(w);
    Tk_Window tkwin = (Tk_Window)winPtr;

    if (winPtr) {
	unsigned int width = (unsigned int) newsize.width;
	unsigned int height= (unsigned int) newsize.height;

	/*
	 * This function can be re-entered, so we need to make sure we don't
	 * clobber any AutoreleasePool set up by the caller.
	 */

	[NSApp _lockAutoreleasePool];

	 /*
	  * Generate and handle a ConfigureNotify event for the new size.
	  */

	TkGenWMConfigureEvent(tkwin, Tk_X(tkwin), Tk_Y(tkwin), width, height,
		TK_SIZE_CHANGED | TK_MACOSX_HANDLE_EVENT_IMMEDIATELY);

	/*
	 * Update Tk's window data for the new size.
	 */

	if ([w respondsToSelector: @selector (tkLayoutChanged)]) {
	    [(TKWindow *)w tkLayoutChanged];
	}

	/*
	 * Reset the cgimage layer and redraw the entire content view.
	 */

	[self viewDidChangeBackingProperties];

	/*
	 * In live resize we seem to need to draw a second time to
	 * avoid artifacts.
	 */

	if ([self inLiveResize]) {
	    [self generateExposeEvents:self.bounds];
	}

	/*
	 * Finally, unlock the main autoreleasePool.
	 */

	[NSApp _unlockAutoreleasePool];

    }

    /*
     * Request a call to updateLayer.
     */

    [self setNeedsDisplay:YES];
}

/*
 * Core method of this class: generates expose events for redrawing.  The
 * expose events are immediately removed from the Tcl event loop and processed.
 * This causes drawing procedures to be scheduled as idle events.  Then all
 * pending idle events are processed so the drawing will actually take place.
 */

- (void) generateExposeEvents: (NSRect) rect
{
    CGRect updateBounds;
    TkWindow *winPtr = TkMacOSXGetTkWindow([self window]);
    void *oldArg;
    Tk_RestrictProc *oldProc;
    static int reentered = 0;

    if (!winPtr ||
	(winPtr->flags & (TK_ALREADY_DEAD)) ||
	!Tk_IsMapped(winPtr)) {
	return;
    }

    if (reentered) {
	/*
	 * When in liveResize an event loop gets run below to
	 * immediately process displayProcs while the resize is being
	 * done.  Those can cause calls to this function, leading to
	 * crashes or very poor performance.  The reentered flag is
	 * used to detect this.
	 */
	// fprintf(stderr, "Recursive call to generateExposeEvents\n");
	return;
    }
    reentered = 1;

    /*
     * Generate Tk Expose events.  All of these events will share the same
     * serial number.
     */
    if ([self inLiveResize]) {
	updateBounds = [self bounds];
    } else {
	updateBounds = NSRectToCGRect(rect);
    }
    updateBounds.origin.y = ([self bounds].size.height - updateBounds.origin.y
			     - updateBounds.size.height);
    if ( GenerateUpdates(&updateBounds, winPtr)) {
	/*
	 * Use the ExposeRestrictProc to process the expose events we just
	 * generated.  This will create idle drawing tasks, which we handle
	 * before we return in the case of a live resize.
	 */
	unsigned int serial = LastKnownRequestProcessed(Tk_Display(winPtr));
	oldProc = Tk_RestrictEvents(ExposeRestrictProc, UINT2PTR(serial), &oldArg);
	while (Tcl_ServiceEvent(TCL_WINDOW_EVENTS|TCL_DONT_WAIT)) {};
	Tk_RestrictEvents(oldProc, NULL, &oldArg);

	/*
	 * During a LiveResize we process all idle tasks generated by the
	 * expose events to redraw the window while it is being resized.
	 */
	if ([self inLiveResize]) {
	    while (Tcl_DoOneEvent(TCL_IDLE_EVENTS)) {}
	}
    }
    reentered = 0;
}

/*
 * In macOS 10.14 and later this method is called when a user changes between
 * light and dark mode or changes the accent color. The implementation
 * generates two virtual events.  The first is either <<LightAqua>> or
 * <<DarkAqua>>, depending on the view's current effective appearance.  The
 * second is <<AppearnceChanged>> and has a data string describing the
 * effective appearance of the view and the current accent and highlight
 * colors.
 */

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400

static const char *const accentNames[] = {
    "Graphite",
    "Red",
    "Orange",
    "Yellow",
    "Green",
    "Blue",
    "Purple",
    "Pink"
};

- (void) viewDidChangeEffectiveAppearance
{
    Tk_Window tkwin = (Tk_Window)TkMacOSXGetTkWindow([self window]);
    if (!tkwin) {
	return;
    }
    NSAppearanceName effectiveAppearanceName = [[self effectiveAppearance] name];
    NSUserDefaults *preferences = [NSUserDefaults standardUserDefaults];
    static const char *defaultColor = NULL;

    if (effectiveAppearanceName == NSAppearanceNameAqua) {
	Tk_SendVirtualEvent(tkwin, "LightAqua", NULL);
    } else if (effectiveAppearanceName == NSAppearanceNameDarkAqua) {
	Tk_SendVirtualEvent(tkwin, "DarkAqua", NULL);
    }
    if (!defaultColor) {
	defaultColor = [NSApp macOSVersion] < 110000 ? "Blue" : "Multicolor";
    }
    NSString *accent = [preferences stringForKey:@"AppleAccentColor"];
    NSArray *words = [[preferences stringForKey:@"AppleHighlightColor"]
				componentsSeparatedByString: @" "];
    NSString *highlight = [words count] > 3 ? [words objectAtIndex:3] : nil;
    const char *accentName = accent ? accentNames[1 + accent.intValue] : defaultColor;
    const char *highlightName = highlight ? highlight.UTF8String: defaultColor;
    char data[256];
    snprintf(data, 256, "Appearance %s Accent %s Highlight %s",
	     effectiveAppearanceName.UTF8String, accentName,
	     highlightName);
    Tk_SendVirtualEvent(tkwin, "AppearanceChanged", Tcl_NewStringObj(data, TCL_INDEX_NONE));
    // Force a redraw of the view.
    [self setFrameSize:self.frame.size];
}

- (void)observeValueForKeyPath:(NSString *)keyPath
		      ofObject:(id)object
			change:(NSDictionary *)change
		       context:(void *)context
{
    (void) change;
    (void) context;
    NSUserDefaults *preferences = [NSUserDefaults standardUserDefaults];
    if (object == preferences && [keyPath isEqualToString:@"AppleHighlightColor"]) {
	if (@available(macOS 10.14, *)) {
	    [self viewDidChangeEffectiveAppearance];
	}
    }
}

#endif

/*
 * This is no-op on 10.7 and up because Apple has removed this widget, but we
 * are leaving it here for backwards compatibility.
 */

- (void) tkToolbarButton: (id) sender
{
#ifdef TK_MAC_DEBUG_EVENTS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), sender);
#endif
    union {XEvent general; XVirtualEvent virt;} event;
    int x, y;
    TkWindow *winPtr = TkMacOSXGetTkWindow([self window]);
    Tk_Window tkwin = (Tk_Window)winPtr;
    (void)sender;

    if (!winPtr){
	return;
    }
    bzero(&event, sizeof(event));
    event.virt.type = VirtualEvent;
    event.virt.serial = LastKnownRequestProcessed(Tk_Display(tkwin));
    event.virt.send_event = false;
    event.virt.display = Tk_Display(tkwin);
    event.virt.event = Tk_WindowId(tkwin);
    event.virt.root = XRootWindow(Tk_Display(tkwin), 0);
    event.virt.subwindow = None;
    event.virt.time = TkpGetMS();
    XQueryPointer(NULL, winPtr->window, NULL, NULL,
	    &event.virt.x_root, &event.virt.y_root, &x, &y, &event.virt.state);
    Tk_TopCoordsToWindow(tkwin, x, y, &event.virt.x, &event.virt.y);
    event.virt.same_screen = true;
    event.virt.name = Tk_GetUid("ToolbarButton");
    Tk_QueueWindowEvent(&event.general, TCL_QUEUE_TAIL);
}

/*
 * On Catalina this is never called and drawRect clips to the rect that
 * is passed to it by AppKit.
 */

- (BOOL) wantsDefaultClipping
{
    return NO;
}

- (BOOL) acceptsFirstResponder
{
    return YES;
}

/*
 * This keyDown method does nothing, which is a huge improvement over the
 * default keyDown method which beeps every time a key is pressed.
 */

- (void) keyDown: (NSEvent *) theEvent
{
    (void)theEvent;

#ifdef TK_MAC_DEBUG_EVENTS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), theEvent);
#endif
}

/*
 * When the services menu is opened this is called for each Responder in
 * the Responder chain until a service provider is found.  The TKContentView
 * should be the first (and generally only) Responder in the chain.  We
 * return the TkServices object that was created in TkpInit.
 */

- (id)validRequestorForSendType:(NSString *)sendType
		     returnType:(NSString *)returnType
{
    if ([sendType isEqualToString:@"NSStringPboardType"] ||
	[sendType isEqualToString:@"NSPasteboardTypeString"]) {
	return [NSApp servicesProvider];
    }
    return [super validRequestorForSendType:sendType returnType:returnType];
}

-(void) resetTkLayerBitmapContext {
    static CGColorSpaceRef colorspace = NULL;
    if (colorspace == NULL) {
	colorspace = CGColorSpaceCreateDeviceRGB();
	CGColorSpaceRetain(colorspace);
    }
    CGContextRef newCtx = CGBitmapContextCreate(
	    NULL, self.layer.contentsScale * self.frame.size.width,
	    self.layer.contentsScale * self.frame.size.height, 8, 0, colorspace,
	    kCGBitmapByteOrder32Big | kCGImageAlphaNoneSkipLast // will also need to specify this when capturing
    );
    CGContextScaleCTM(newCtx, self.layer.contentsScale, self.layer.contentsScale);
#if 0
    fprintf(stderr, "rTkLBC %.1f %s %p %p %ld\n", (float)self.layer.contentsScale,
	    NSStringFromSize(self.frame.size).UTF8String, colorspace, newCtx,
	    self.tkLayerBitmapContext ?
	    (long)CFGetRetainCount(self.tkLayerBitmapContext) : INT_MIN);
    fprintf(stderr, "rTkLBC %p %ld\n", self.tkLayerBitmapContext,
	    (long)(self.tkLayerBitmapContext ?
	    CFGetRetainCount(self.tkLayerBitmapContext) : LONG_MIN));
#endif
    // The context is also released in TkWmDeadWindow.
    CGContextRelease(self.tkLayerBitmapContext);
    self.tkLayerBitmapContext = newCtx;
}

@end

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
