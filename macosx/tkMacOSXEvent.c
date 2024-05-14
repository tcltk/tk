/*
 * tkMacOSXEvent.c --
 *
 *	This file contains the basic Mac OS X Event handling routines.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
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
    TKLog(@"-[%@(%p) %s] %@", [self class], self, sel_getName(_cmd), theEvent);
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
     *  calling XSync on all displays until no events are left.  On X11 the
     *  call to XSync might cause the window manager to generate more events
     *  which would then get processed. Apparently this process stabilizes on
     *  X11, leaving the window manager in a state where all events have been
     *  generated and no additional events can be genereated by updating widgets.
     *
     *  It is not clear what the Aqua port should do when XSync is called, but
     *  currently the best option seems to be to do nothing.  (See ticket
     *  [da5f2266df].)
     */

    LastKnownRequestProcessed(display)++;
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
