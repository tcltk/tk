
/*
 * tkMacOSXSubwindows.c --
 *
 *	Implements subwindows for the macintosh version of Tk.
 *
 * Copyright © 2026 Marc Culler
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkMacOSXWm.h"
#include "tkMacOSXInt.h"

/*
 * The generic code in tkPointer.c maintains thread specific data which
 * stores the current screen coordinates of the mouse pointer and the
 * TkWindow which currently contains the mouse pointer.  It uses this
 * data to emulate X11 behavior for displaying the mouse cursor and
 * sending <Enter> and <Leave> events.  The latter relies on the function
 * TkInOutEvents in tkGrab.c.
 *
 * Platforms other than X11 are expected to update this data whenever
 * it changes by calling Tk_UpdatePointer.  That requires that
 * whenever the mouse moves, or a toplevel or widget window is mapped,
 * unmapped, or destroyed, or a toplevel is hidden or unhidden, there
 * needs to be a call to TkUpdatePointer passing the current
 * coordinates of the mouse pointer and a pointer to the highest Tk
 * Window in the stacking order which is mapped and not hidden and
 * contains the mouse pointer.  This module contains functions for
 * performing those chores. 
 */

/*
 *----------------------------------------------------------------------
 *
 * mouseIsInTkWindow --
 *
 *      Checks whether the mouse pointer is currently contained in the
 *      specified TkWindow.  In the case of a toplevel window this
 *      means that the mouse pointer is inside the contentView of the
 *      associated TKWindow.  A point in the title bar is not
 *      considered to be in the toplevel.
 *
 * Results:
 *	True if the pointer is in the window, false otherwise.
 *
 * Side effects:
 *      None	
 *
 *----------------------------------------------------------------------
 */

static bool mouseIsInTkWindow(
    TkWindow *winPtr)
{
    TKWindow *macWin = (TKWindow *)TkMacOSXGetNSWindowForDrawable(winPtr->window);

    NSRect toplevelFrame = [macWin frame];
    int viewHeight = (int) [[macWin contentView] frame].size.height;
    int widgetY = viewHeight - Tk_Height(winPtr) - winPtr->privatePtr->yOff;
    NSRect widgetFrame = NSMakeRect(
	  toplevelFrame.origin.x + winPtr->privatePtr->xOff,
	  toplevelFrame.origin.y + widgetY,
	  Tk_Width(winPtr), Tk_Height(winPtr));
    return NSMouseInRect([NSEvent mouseLocation], widgetFrame, NO);
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXUpdatePointerAfterMap --
 *
 *      If the specified TkWindow should become the current pointer
 *      window, this function calls Tk_UpdatePointer.
 *
 * Results:
 *	None
 *
 * Side effects:
 *      The core pointer window may be changed.	
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkMacOSXUpdatePointerAfterMap(
    TkWindow *winPtr)
{
    NSPoint mouse = [NSEvent mouseLocation];
    int x = mouse.x, y = TkMacOSXZeroScreenHeight() - mouse.y;
    if (!mouseIsInTkWindow(winPtr)) {
	/* If the mouse is not in the new window no update is needed. */
    	return;
    }
    TKWindow *macWin = (TKWindow *)TkMacOSXGetNSWindowForDrawable(winPtr->window);
    TkWindow *toplevelPtr = TkMacOSXGetTkWindow(macWin);
    if (toplevelPtr &&
	toplevelPtr->wmInfoPtr &&
	(toplevelPtr->wmInfoPtr->flags & WM_HIDDEN)) {
	return;
    }
    Tk_UpdatePointer((Tk_Window) winPtr, x, y, [NSApp tkButtonState]);
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXUpdatePointerBeforeUnmap --
 *
 *      The specified window will be unmapped or hidden.  If it is the
 *      current pointer window, find the window which should become the
 *      next pointer window and pass it to TkUpdatePointer.
 *
 * Results:
 *	None
 *
 * Side effects:
 *      The core pointer window may be changed.	
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkMacOSXUpdatePointerBeforeUnmap(
    TkWindow *winPtr)
{
    TKWindow *macWin = (TKWindow *)TkMacOSXGetNSWindowForDrawable(winPtr->window);
    TkWindow *toplevelPtr = NULL;
    Tk_Window target = NULL;
    NSPoint mouse = [NSEvent mouseLocation];
    int root_x = floor(mouse.x),
	root_y = floor(TkMacOSXZeroScreenHeight() - mouse.y);
    if (!mouseIsInTkWindow(winPtr)) {
	/* This is not the pointer window. No update is needed. */
    	return;
    }
    if (Tk_IsTopLevel(winPtr)) {
	/*
	 * Find the highest toplevel, other than this one, which contains the
	 * mouse pointer in its contentView.
	 */
	for (NSWindow *w in [NSApp orderedWindows]) {
	    if (w == macWin) {
		continue;
	    }
	    TkWindow *winPtr2 = TkMacOSXGetTkWindow(w);
	    if (winPtr2) {
		NSRect windowFrame = [w frame];
		NSRect contentFrame = windowFrame;
		contentFrame.size.height = [[w contentView] frame].size.height;
		if (NSMouseInRect(mouse, contentFrame, NO)) {
		    toplevelPtr = winPtr2;
		    break;
		} else if (NSMouseInRect(mouse, windowFrame, NO)) {
		    break;
		}
	    }
	}
	if (toplevelPtr == NULL) {
	    /*
	     * The mouse is not in any other toplevel and this one is about
	     * to be unmapped.  Call TkUpdatePointer with a NULL window.
	     */
	    Tk_UpdatePointer(NULL, root_x, root_y, [NSApp tkButtonState]);
	    return;
	}
    } else {
	/*
	 * We are about to unmap a widget which contains the mouse pointer.  If
	 * there is a new pointer window it must be in the same toplevel as the
	 * widget.
	 */
	toplevelPtr = TkMacOSXGetTkWindow(macWin);
    }
    /*
     * We now have a toplevel containing the mouse pointer.  Find the highest
     * widget which contains the pointer.
     */
    NSPoint local = [macWin tkConvertPointFromScreen: mouse];
    int top_x = floor(local.x),
	top_y = floor(macWin.frame.size.height - local.y);
    int dummy_x, dummy_y;
    target = Tk_TopCoordsToWindow((Tk_Window) toplevelPtr,
	top_x, top_y, &dummy_x, &dummy_y);
    if (!Tk_IsTopLevel(target) && (Tk_Parent(target) == NULL)) {
	/*
	 * The parent of the Tk window is in the process of being destroyed.
	 * Don't call Tk_UpdatePointer in this case.
	 */
	return;
    } else {
	Tk_UpdatePointer(target, root_x, root_y, [NSApp tkButtonState]);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXUpdatePointerAfterDestroy --
 *
 *	Determine the new pointer window after the destruction of the old pointer
 *      window, and notify Tk of it.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	See description.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE
void TkMacOSXUpdatePointerAfterDestroy (
    NSWindow *deadNSWindow)
{
    Tk_Window target = NULL;
    TkWindow *winPtr;
    NSWindow *w;

    NSPoint mouse = [NSEvent mouseLocation];
    if (! NSPointInRect(mouse, [deadNSWindow frame])) {
	return;
    }

    /*
     * Determine the new window to contain the screen pointer (target window).
     * In case that the procedure doesn't yield a result, let the root window
     * of the screen be the new pointer window (target == NULL).
     */

    /* Step 1: Find the toplevel that will contain the screen pointer */
    winPtr = NULL;
    for (w in [NSApp orderedWindows]) {
	if (w == deadNSWindow || w == NULL) {
	    continue;
	}
	winPtr = TkMacOSXGetTkWindow(w);
	if (winPtr == NULL || ! Tk_IsMapped((Tk_Window)winPtr)) {
	    continue;
	}
	if (NSPointInRect(mouse, [w frame])) {
	    target = (Tk_Window)winPtr;
	    break;
	}
    }

    NSPoint local = [w tkConvertPointFromScreen: mouse];
    int top_x = floor(local.x),
	top_y = floor(w.frame.size.height - local.y);
    int root_x = floor(mouse.x),
	root_y = floor(TkMacOSXZeroScreenHeight() - mouse.y);

    if (target) {
	/*
	 * Step 2: Find the Tk internal window within the toplevel that will contain
	 *         the screen pointer.
	 */
	int dummy_x, dummy_y;
	target = Tk_TopCoordsToWindow(target, top_x, top_y, &dummy_x, &dummy_y);
	if (! Tk_IsTopLevel(target) && (Tk_Parent(target) == NULL)) {
	   /*
	    * The parent of the Tk internal window is in the process of being destroyed.
	    * Don't call Tk_UpdatePointer in this case.
	    */
	    return;
	} else {
	    Tk_UpdatePointer(target, root_x, root_y, [NSApp tkButtonState]);
	}
    }
}
/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * eval: (c-set-offset 'arglist-intro '+)
 * eval: (c-set-offset 'arglist-cont-nonempty '+)
 * fill-column: 79
 * coding: utf-8
 * End:
 */
