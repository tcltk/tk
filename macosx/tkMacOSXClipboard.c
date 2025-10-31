/*
 * tkMacOSXClipboard.c --
 *
 *	This file manages the clipboard for the Tk toolkit.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2006-2009 Daniel A. Steffen <das@users.sourceforge.net>
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkMacOSXConstants.h"
#include "tkSelect.h"

static NSInteger changeCount = -1;
static Tk_Window tkClipboardOwner = NULL;

#pragma mark TKApplication(TKClipboard)

@implementation TKApplication(TKClipboard)
- (void) tkProvidePasteboard: (TkDisplay *) dispPtr
	pasteboard: (NSPasteboard *) sender
	provideDataForType: (NSString *) type
{
    NSMutableString *string = [NSMutableString new];
    if (dispPtr && dispPtr->clipboardActive &&
	    [type isEqualToString:NSStringPboardType]) {
	for (TkClipboardTarget *targetPtr = dispPtr->clipTargetPtr; targetPtr;
		targetPtr = targetPtr->nextPtr) {
	    if (targetPtr->type == XA_STRING ||
		    targetPtr->type == dispPtr->utf8Atom) {
		for (TkClipboardBuffer *cbPtr = targetPtr->firstBufferPtr;
			cbPtr; cbPtr = cbPtr->nextPtr) {
		    NSString *s = [[TKNSString alloc]
			initWithTclUtfBytes:cbPtr->buffer
				     length:(NSUInteger)cbPtr->length];
		    [string appendString:s];
		    [s release];
		}
		break;
	    }
	}
    }
    [sender setString:string forType:type];
    changeCount = [sender changeCount];
    [string release];
}

- (void) tkProvidePasteboard: (TkDisplay *) dispPtr
{
    if (dispPtr && dispPtr->clipboardActive) {
	[self tkProvidePasteboard:dispPtr
		pasteboard:[NSPasteboard generalPasteboard]
		provideDataForType:NSStringPboardType];
    }
}

- (void) pasteboard: (NSPasteboard *) sender
	provideDataForType: (NSString *) type
{
    TkDisplay *dispPtr = TkGetDisplayList();
    [self tkProvidePasteboard:dispPtr
		   pasteboard:[NSPasteboard generalPasteboard]
	   provideDataForType:NSStringPboardType];
}

- (void) tkCheckPasteboard
{
    if (tkClipboardOwner && [[NSPasteboard generalPasteboard] changeCount] !=
	    changeCount) {
	TkDisplay *dispPtr = TkGetDisplayList();
	if (dispPtr) {
	    XEvent event;
	    event.xany.type = SelectionClear;
	    event.xany.serial = NextRequest(Tk_Display(tkClipboardOwner));
	    event.xany.send_event = False;
	    event.xany.window = Tk_WindowId(tkClipboardOwner);
	    event.xany.display = Tk_Display(tkClipboardOwner);
	    event.xselectionclear.selection = dispPtr->clipboardAtom;
	    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
	}
	tkClipboardOwner = NULL;
    }
}
@end

#pragma mark -

/*
 *----------------------------------------------------------------------
 *
 * TkSelGetSelection --
 *
 *	Retrieve the specified selection from another process. For now, only
 *	fetching XA_STRING from CLIPBOARD is supported. Eventually other types
 *	should be allowed.
 *
 * Results:
 *	The return value is a standard Tcl return value. If an error occurs
 *	(such as no selection exists) then an error message is left in the
 *	interp's result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkSelGetSelection(
    Tcl_Interp *interp,		/* Interpreter to use for reporting errors. */
    Tk_Window tkwin,		/* Window on whose behalf to retrieve the
				 * selection (determines display from which to
				 * retrieve). */
    Atom selection,		/* Selection to retrieve. */
    Atom target,		/* Desired form in which selection is to be
				 * returned. */
    Tk_GetSelProc *proc,	/* Procedure to call to process the selection,
				 * once it has been retrieved. */
    void *clientData)	/* Arbitrary value to pass to proc. */
{
    int result = TCL_ERROR;
    TkDisplay *dispPtr = ((TkWindow *) tkwin)->dispPtr;
    int haveExternalClip =
	    ([[NSPasteboard generalPasteboard] changeCount] != changeCount);

    if (dispPtr && (haveExternalClip || dispPtr->clipboardActive)
	    && selection == dispPtr->clipboardAtom
	    && (target == XA_STRING || target == dispPtr->utf8Atom)) {
	NSString *string = nil;
	NSPasteboard *pb = [NSPasteboard generalPasteboard];
	NSString *type = [pb availableTypeFromArray:[NSArray arrayWithObject:
		NSStringPboardType]];

	if (type) {
	    string = [pb stringForType:type];
	}
	if (string) {
	    result = proc(clientData, interp, string.UTF8String);
	}
    } else {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	     "%s selection doesn't exist or form \"%s\" not defined",
	     Tk_GetAtomName(tkwin, selection),
	     Tk_GetAtomName(tkwin, target)));
	Tcl_SetErrorCode(interp, "TK", "SELECTION", "EXISTS", (char *)NULL);
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetSelectionOwner --
 *
 *	This function claims ownership of the specified selection. If the
 *	selection is CLIPBOARD, then we empty the system clipboard.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetSelectionOwner(
    Display *display,		/* X Display. */
    Atom selection,		/* What selection to own. */
    Window owner,		/* Window to be the owner. */
    TCL_UNUSED(Time))			/* The current time? */
{
    TkDisplay *dispPtr = TkGetDisplayList();

    if (dispPtr && selection == dispPtr->clipboardAtom) {
	tkClipboardOwner = owner ? Tk_IdToWindow(display, owner) : NULL;
	if (!dispPtr->clipboardActive) {
	    NSPasteboard *pb = [NSPasteboard generalPasteboard];
	    changeCount = [pb declareTypes:[NSArray array] owner:NSApp];
	}
    }
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXSelDeadWindow --
 *
 *	This function is invoked just before a TkWindow is deleted. It performs
 *	selection-related cleanup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	tkClipboardOwner is cleared.
 *
 *----------------------------------------------------------------------
 */

void
TkMacOSXSelDeadWindow(
    TkWindow *winPtr)
{
    if (winPtr && winPtr == (TkWindow *)tkClipboardOwner) {
	tkClipboardOwner = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelUpdateClipboard --
 *
 *	This function is called to force the clipboard to be updated after new
 *	data is added or the clipboard has been cleared.
 *
 *      The nil Object is declared to be the owner.  This is done in a way
 *      which triggers an incremeent of the pasteboard's changeCount property,
 *      notifying clipboard managers that the value has changed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Ownership contents and attributes of the general NSPasteboard
 *      may change.
 *
 *----------------------------------------------------------------------
 */

/*
 * Apple says that the changeCount is incremented whenever the ownership
 * of a pasteboard type changes.  They actually mean that the changeCount
 * is incremented when declareTypes is called, but is left unchanged when
 * addTypes is called.  (Both methods can change ownership in some sense
 * and both return the new changeCount.)
 *
 * Apple also says that addTypes "promises" that the owner object (if not nil)
 * will provide data of the specified type, while declareTypes "prepares" the
 * pasteboard.  Maybe that explains something.
 */

void
TkSelUpdateClipboard(
    TCL_UNUSED(TkWindow*),		/* Window associated with clipboard. */
    clipboardOption option)	/* option passed to clipboard command */
{
    NSPasteboard *pb = [NSPasteboard generalPasteboard];
    switch (option) {
    case CLIPBOARD_APPEND:
	/*
	 * This increments the changeCount so that clipboard managers will be
	 * able to see and manage the clip.
	 */

	changeCount = [pb declareTypes:[NSArray arrayWithObject:NSStringPboardType]
				 owner:nil];
	[NSApp tkProvidePasteboard: TkGetDisplayList()
			pasteboard: (NSPasteboard *) pb
		provideDataForType: (NSString *) NSStringPboardType];
	break;
    case CLIPBOARD_CLEAR:
	changeCount = [pb declareTypes:[NSArray arrayWithObject:NSStringPboardType]
				 owner:nil];
	[NSApp tkProvidePasteboard: TkGetDisplayList()
			pasteboard: (NSPasteboard *) pb
		provideDataForType: (NSString *) NSStringPboardType];
	break;
    default:
	break;
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkSelEventProc --
 *
 *	This procedure is invoked whenever a selection-related event occurs.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Lots: depends on the type of event.
 *
 *--------------------------------------------------------------
 */

void
TkSelEventProc(
    Tk_Window tkwin,		/* Window for which event was targeted. */
    XEvent *eventPtr)	/* X event: either SelectionClear,
				 * SelectionRequest, or SelectionNotify. */
{
    if (eventPtr->type == SelectionClear) {
	tkClipboardOwner = NULL;
	TkSelClearSelection(tkwin, eventPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelPropProc --
 *
 *	This procedure is invoked when property-change events occur on windows
 *	not known to the toolkit. This is a stub function under Windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkSelPropProc(
    TCL_UNUSED(XEvent *))	/* X PropertyChange event. */
{
}

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
