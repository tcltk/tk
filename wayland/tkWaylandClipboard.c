/*
 * tkWaylandClipboard.c --
 *
 *	This file manages the clipboard for the Tk toolkit when using GLFW on Wayland.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution.
 */

#include "tkSelect.h"
#include <GLFW/glfw3.h>

static int clipboardChangeCount = 0;
static Tk_Window tkClipboardOwner = NULL;


/*
 *----------------------------------------------------------------------
 *
 * TkSelGetSelection --
 *
 *	Retrieve the CLIPBOARD selection (only XA_STRING / UTF8_STRING supported).
 *
 * Results:
 *	TCL_OK or TCL_ERROR, with error message in interp if failed.
 *
 *----------------------------------------------------------------------
 */
int
TkSelGetSelection(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    Atom selection,
    Atom target,
    Tk_GetSelProc *proc,
    void *clientData)
{
    TkDisplay *dispPtr = ((TkWindow *)tkwin)->dispPtr;

    if (!dispPtr || selection != dispPtr->clipboardAtom ||
        (target != XA_STRING && target != dispPtr->utf8Atom)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf(
            "%s selection doesn't exist or form \"%s\" not supported",
            Tk_GetAtomName(tkwin, selection),
            Tk_GetAtomName(tkwin, target)));
        Tcl_SetErrorCode(interp, "TK", "SELECTION", "BAD_FORM", NULL);
        return TCL_ERROR;
    }

    const char *text = glfwGetClipboardString(NULL);
    if (!text || *text == '\0') {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Clipboard is empty", -1));
        Tcl_SetErrorCode(interp, "TK", "SELECTION", "EMPTY", NULL);
        return TCL_ERROR;
    }

    return proc(clientData, interp, text);
}

/*
 *----------------------------------------------------------------------
 *
 * XSetSelectionOwner --
 *
 *	Claim ownership of the CLIPBOARD selection.
 *	We only track ownership internally — GLFW doesn't need clearing here.
 *
 *----------------------------------------------------------------------
 */
int
XSetSelectionOwner(
    Display *display,
    Atom selection,
    Window owner,
    Time time TCL_UNUSED)
{
    TkDisplay *dispPtr = TkGetDisplayList();
    if (!dispPtr || selection != dispPtr->clipboardAtom) {
        return Success;
    }

    tkClipboardOwner = owner ? Tk_IdToWindow(display, owner) : NULL;

    /* Any time ownership changes → treat as clipboard content change. */
    if (tkClipboardOwner || owner == None) {
        clipboardChangeCount++;
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXSelDeadWindow --
 *
 *	Cleanup when a window that owned the clipboard is being destroyed.
 *
 *----------------------------------------------------------------------
 */
void
TkMacOSXSelDeadWindow(TkWindow *winPtr)
{
    if (winPtr && (Tk_Window)winPtr == tkClipboardOwner) {
        tkClipboardOwner = NULL;
        clipboardChangeCount++;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelUpdateClipboard --
 *
 *	Push Tk clipboard content → GLFW system clipboard
 *	(called after clipboard append/clear operations)
 *
 *----------------------------------------------------------------------
 */
void
TkSelUpdateClipboard(
    TkWindow *winPtr,
    clipboardOption option)
{
    TkDisplay *dispPtr = winPtr ? winPtr->dispPtr : TkGetDisplayList();
    if (!dispPtr) {
        return;
    }

    /* We only care about string-like targets. */
    if (option != CLIPBOARD_APPEND && option != CLIPBOARD_CLEAR) {
        return;
    }

    clipboardChangeCount++;

    if (option == CLIPBOARD_CLEAR) {
        glfwSetClipboardString(NULL, "");
        return;
    }

    /* Try to find string/utf8 content in Tk's clipboard buffers. */
    if (!dispPtr->clipTargetPtr) {
        return;
    }

    for (TkClipboardTarget *targetPtr = dispPtr->clipTargetPtr;
         targetPtr;
         targetPtr = targetPtr->nextPtr)
    {
        if (targetPtr->type != XA_STRING && targetPtr->type != dispPtr->utf8Atom) {
            continue;
        }

        Tcl_DString ds;
        Tcl_DStringInit(&ds);

        for (TkClipboardBuffer *buf = targetPtr->firstBufferPtr;
             buf;
             buf = buf->nextPtr)
        {
            Tcl_DStringAppend(&ds, buf->buffer, buf->length);
        }

        int len = Tcl_DStringLength(&ds);
        if (len > 0) {
            glfwSetClipboardString(NULL, Tcl_DStringValue(&ds));
        }

        Tcl_DStringFree(&ds);
        return;   /* Only push the first matching string target. */
    }

    /* If we get here and it was CLEAR, already handled above. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelEventProc --
 *
 *	Handle SelectionClear events (ownership lost)
 *
 *----------------------------------------------------------------------
 */
void
TkSelEventProc(
    Tk_Window tkwin,
    XEvent *eventPtr)
{
    if (eventPtr->type == SelectionClear) {
        tkClipboardOwner = NULL;
        clipboardChangeCount++;
        TkSelClearSelection(tkwin, eventPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelPropProc --
 *
 *	Stub — not needed with GLFW backend
 *
 *----------------------------------------------------------------------
 */
void
TkSelPropProc(XEvent *eventPtr TCL_UNUSED)
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
