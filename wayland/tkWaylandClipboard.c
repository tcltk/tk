/*
 * tkWaylandClipboard.c --
 *
 *	Clipboard stubs for the Tk/GLFW Wayland backend.
 *
 *	All clipboard clear/append/get operations are handled at the Tcl
 *	script level by redefined [clipboard] commands that exec wl-copy
 *	and wl-paste directly.  This file retains only the stub symbols
 *	required by Tk's generic selection machinery. This setup is required
 *      because GFLW's clipboard supports is essentially broken on Wayland.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution.
 */

#include "tkInt.h"
#include "tkSelect.h"
#include <GLFW/glfw3.h>


/*
 *----------------------------------------------------------------------
 *
 * TkSelGetSelection --
 *
 *	Stub.  Clipboard retrieval is handled by the Tcl-level [clipboard get]
 *	redefinition (via wl-paste).  This function should never be reached in
 *	normal operation; if it is, return an error so the problem is visible.
 *
 * Results:
 *	Always TCL_ERROR.
 *
 * Side effects:
 *	Sets an error message in interp.
 *
 *----------------------------------------------------------------------
 */

int
TkSelGetSelection(
    Tcl_Interp *interp,
    TCL_UNUSED(Tk_Window),
    TCL_UNUSED(Atom),
    TCL_UNUSED(Atom),
    TCL_UNUSED(Tk_GetSelProc *),
    TCL_UNUSED(void *))
{
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetSelectionOwner --
 *
 *	Stub.  Ownership tracking is not required when clipboard operations
 *	are managed entirely by wl-copy/wl-paste at the script level.
 *
 * Results:
 *	Always Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetSelectionOwner(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Atom),
    TCL_UNUSED(Window),
    TCL_UNUSED(Time))
{
    return Success;
}


/*
 *----------------------------------------------------------------------
 *
 * TkSelEventProc --
 *
 *	Handle SelectionClear events (ownership lost).  Called by Tk's
 *	generic event dispatch; must remain functional.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Notifies Tk of selection loss via TkSelClearSelection.
 *
 *----------------------------------------------------------------------
 */

void
TkSelEventProc(
    Tk_Window tkwin,
    XEvent *eventPtr)
{
    if (eventPtr->type == SelectionClear) {
        TkSelClearSelection(tkwin, eventPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelPropProc --
 *
 *	Stub — not needed with GLFW/Wayland backend.
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
    TCL_UNUSED(XEvent *))
{
    /* No-op */
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
