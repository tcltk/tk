/*
 * tkSDLSelect.c --
 *
 *	This file contains X specific routines for manipulating selections.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkSelect.h"

#include <SDL.h>


/*
 *----------------------------------------------------------------------
 *
 * TkSelGetSelection --
 *
 *	Retrieve the specified selection from another process.
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
    Tk_GetSelProc *proc,	/* Function to call to process the selection,
				 * once it has been retrieved. */
    ClientData clientData)	/* Arbitrary value to pass to proc. */
{
    int result;
    char *data;
    Tcl_Encoding encoding;
    Tcl_DString buffer;

    if (!SDL_HasClipboardText()) {
empty:
	Tcl_SetResult(interp, "empty selection", TCL_VOLATILE);
	return TCL_ERROR;
    }
    data = SDL_GetClipboardText();
    if (data == NULL) {
	goto empty;
    }
    encoding = Tcl_GetEncoding(NULL, "utf-8");
    Tcl_ExternalToUtfDString(encoding, data, -1, &buffer);
    result = proc(clientData, interp, Tcl_DStringValue(&buffer));
    Tcl_FreeEncoding(encoding);
    Tcl_DStringFree(&buffer);
    SDL_free(data);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelPropProc --
 *
 *	This function is invoked when property-change events occur on windows
 *	not known to the toolkit. Its function is to implement the sending
 *	side of the INCR selection retrieval protocol when the selection
 *	requestor deletes the property containing a part of the selection.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If the property that is receiving the selection was just deleted, then
 *	a new piece of the selection is fetched and placed in the property,
 *	until eventually there's no more selection to fetch.
 *
 *----------------------------------------------------------------------
 */

void
TkSelPropProc(
    register XEvent *eventPtr)	/* X PropertyChange event. */
{
}

/*
 *--------------------------------------------------------------
 *
 * TkSelEventProc --
 *
 *	This function is invoked whenever a selection-related event occurs.
 *	It does the lion's share of the work in implementing the selection
 *	protocol.
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
    register XEvent *eventPtr)	/* X event: either SelectionClear,
				 * SelectionRequest, or SelectionNotify. */
{
    /*
     * Case #1: SelectionClear events.
     */

    if (eventPtr->type == SelectionClear) {
	TkSelClearSelection(tkwin, eventPtr);
    }

    /*
     * Case #2: SelectionNotify events.
     */

    if (eventPtr->type == SelectionNotify) {
    }

    /*
     * Case #3: SelectionRequest events.
     */

    if (eventPtr->type == SelectionRequest) {
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelUpdateClipboard --
 *
 *      This function is called to force the clipboard to be updated
 *      after new data is added.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
TkSelUpdateClipboard(
    TkWindow *winPtr,           /* Window associated with clipboard. */
    TkClipboardTarget *targetPtr)
                                /* Info about the content. */
{
    if ((targetPtr != NULL) && (targetPtr->format == XA_STRING) &&
	(targetPtr->firstBufferPtr != NULL)) {
	Tcl_DString buffer, buffer2;
	Tcl_Encoding encoding;
	TkClipboardBuffer *bufPtr = targetPtr->firstBufferPtr;

        Tcl_DStringInit(&buffer);
	while (bufPtr != NULL) {
	    Tcl_DStringAppend(&buffer, bufPtr->buffer, bufPtr->length);
	    bufPtr = bufPtr->nextPtr;
	}
	encoding = Tcl_GetEncoding(NULL, "utf-8");
	Tcl_UtfToExternalDString(encoding, Tcl_DStringValue(&buffer),
		Tcl_DStringLength(&buffer), &buffer2);
	SDL_SetClipboardText(Tcl_DStringValue(&buffer2));
	Tcl_FreeEncoding(encoding);
	Tcl_DStringFree(&buffer);
	Tcl_DStringFree(&buffer2);
    }
}
/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
