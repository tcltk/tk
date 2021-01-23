/*
 * tkMacOSXEvent.c --
 *
 *	This file contains the basic Mac OS X Event handling routines.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009, Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkMacOSXInt.h"
#include "tkMacOSXDebug.h"
#include "tkMacOSXConstants.h"

#pragma mark TKApplication(TKEvent)

enum {
    NSWindowWillMoveEventType = 20
};

@implementation TKApplication(TKEvent)
/* TODO: replace by +[addLocalMonitorForEventsMatchingMask ? */
- (NSEvent *) tkProcessEvent: (NSEvent *) theEvent
{
#ifdef TK_MAC_DEBUG_EVENTS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, theEvent);
#endif
    NSEvent	    *processedEvent = theEvent;
    NSEventType	    type = [theEvent type];
    NSInteger	    subtype;

    switch ((NSInteger)type) {
    case NSAppKitDefined:
        subtype = [theEvent subtype];

	switch (subtype) {
	    /* Ignored at the moment. */
	case NSApplicationActivatedEventType:
	    break;
	case NSApplicationDeactivatedEventType:
	    break;
	case NSWindowExposedEventType:
	    break;
	case NSScreenChangedEventType:
	    break;
	case NSWindowMovedEventType:
	    break;
        case NSWindowWillMoveEventType:
            break;

        default:
            break;
	}
	break; /* AppkitEvent. Return theEvent */
    case NSKeyUp:
    case NSKeyDown:
    case NSFlagsChanged:
	processedEvent = [self tkProcessKeyEvent:theEvent];
	break; /* Key event.  Return the processed event. */
    case NSLeftMouseDown:
    case NSLeftMouseUp:
    case NSRightMouseDown:
    case NSRightMouseUp:
    case NSLeftMouseDragged:
    case NSRightMouseDragged:
    case NSMouseMoved:
    case NSMouseEntered:
    case NSMouseExited:
    case NSScrollWheel:
    case NSOtherMouseDown:
    case NSOtherMouseUp:
    case NSOtherMouseDragged:
    case NSTabletPoint:
    case NSTabletProximity:
	processedEvent = [self tkProcessMouseEvent:theEvent];
	break; /* Mouse event.  Return the processed event. */
#if 0
    case NSSystemDefined:
        subtype = [theEvent subtype];
	break;
    case NSApplicationDefined: {
	id win;
	win = [theEvent window];
	break;
	}
    case NSCursorUpdate:
        break;
    case NSEventTypeGesture:
    case NSEventTypeMagnify:
    case NSEventTypeRotate:
    case NSEventTypeSwipe:
    case NSEventTypeBeginGesture:
    case NSEventTypeEndGesture:
        break;
#endif

    default:
	break; /* return theEvent */
    }
    return processedEvent;
}
@end
#pragma mark -
int
XSync(
    Display *display,
    TCL_UNUSED(Bool))
{
    /*
     *  The main use of XSync is by the update command, which alternates
     *  between running an event loop to process all events without waiting and
     *  calling XSync on all displays until no events are left.  There is
     *  nothing for the mac to do with respect to syncing its one display but
     *  it can (and, during regression testing, frequently does) happen that
     *  timer events fire during the event loop. Processing those here seems
     *  to make the update command work in a way that is more consistent with
     *  its behavior on other platforms.
     */

    while (Tcl_DoOneEvent(TCL_TIMER_EVENTS|TCL_DONT_WAIT)){}
    display->request++;
    return 0;
}

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
