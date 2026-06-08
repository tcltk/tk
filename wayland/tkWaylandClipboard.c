/*
 * tkWaylandClipboard.c --
 *
 *	This file manages the clipboard for the Tk toolkit when using GLFW 
 *      on Wayland. It syncs Tk’s clipboard with the native Wayland clipboard
 *      using wl-copy and wl-paste subprocess calls.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution.
 */

#include "tkInt.h"
#include "tkSelect.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int clipboardChangeCount = 0;
static Tk_Window tkClipboardOwner = NULL;

/*
 *----------------------------------------------------------------------
 *
 * TkSelGetSelection --
 *
 *	Retrieve the CLIPBOARD selection (only XA_STRING / UTF8_STRING supported).
 *	Uses wl-paste for native Wayland clipboard.
 *
 * Results:
 *	TCL_OK or TCL_ERROR, with error message in interp if failed.
 *
 * Side effects:
 *	None.
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

    /* Check if we're running under Wayland. */
    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    
    /* Check Wayland system clipboard first. */
    if (wayland_display && wayland_display[0]) {
        FILE *fp = popen("wl-paste --no-newline --clipboard 2>/dev/null", "r");
        if (fp) {
            Tcl_DString ds;
            Tcl_DStringInit(&ds);
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
                Tcl_DStringAppend(&ds, buf, (int)n);
            }
            int pclose_result = pclose(fp);
            
            if (pclose_result == 0 && Tcl_DStringLength(&ds) > 0) {
                int result = proc(clientData, interp, Tcl_DStringValue(&ds));
                fprintf(stderr, "clipboard read value is %s\n", Tcl_DStringValue(&ds));
                Tcl_DStringFree(&ds);
                return result;
            }
            Tcl_DStringFree(&ds);
        } else {
            fprintf(stderr, "tkWaylandClipboard: Failed to run wl-paste: %s\n", 
                    strerror(errno));
        }
    }

    /* Fall back to Tk's internal clipboard. */
    if (!dispPtr->clipTargetPtr) {
        return proc(clientData, interp, "");
    }

    for (TkClipboardTarget *targetPtr = dispPtr->clipTargetPtr;
         targetPtr; targetPtr = targetPtr->nextPtr) {
        if (targetPtr->type != XA_STRING &&
                targetPtr->type != dispPtr->utf8Atom) continue;

        Tcl_DString ds;
        Tcl_DStringInit(&ds);
        for (TkClipboardBuffer *buf = targetPtr->firstBufferPtr;
             buf; buf = buf->nextPtr) {
            Tcl_DStringAppend(&ds, buf->buffer, buf->length);
        }
        if (Tcl_DStringLength(&ds) > 0) {
            int result = proc(clientData, interp, Tcl_DStringValue(&ds));
            Tcl_DStringFree(&ds);
            return result;
        }
        Tcl_DStringFree(&ds);
    }

    return proc(clientData, interp, "");
}
 

/*
 *----------------------------------------------------------------------
 *
 * XSetSelectionOwner --
 *
 *	Claim ownership of the CLIPBOARD selection.
 *	We only track ownership internally.
 *
 * Results:
 *	Returns Success (0) if successful, otherwise an error code.
 *
 * Side effects:
 *	Updates internal clipboard owner tracking and change counter.
 *
 *----------------------------------------------------------------------
 */
 
int
XSetSelectionOwner(
    Display *display,
    Atom selection,
    Window owner,
    TCL_UNUSED(Time))
{
    TkDisplay *dispPtr = TkGetDisplayList();
    if (!dispPtr || selection != dispPtr->clipboardAtom) {
        return Success;
    }

    tkClipboardOwner = owner ? Tk_IdToWindow(display, owner) : NULL;

    /* Any time ownership changes, treat as clipboard content change. */
    if (tkClipboardOwner || owner == None) {
        clipboardChangeCount++;
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelUpdateClipboard --
 *
 *	Push Tk clipboard content -> Wayland system clipboard using wl-copy.
 *	(called after clipboard append/clear operations).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the system clipboard via wl-copy.
 *
 *----------------------------------------------------------------------
 */
 
#ifdef TkSelUpdateClipboard
#undef TkSelUpdateClipboard
#endif

void
TkSelUpdateClipboard(TkWindow *winPtr, clipboardOption option)
{
    TkDisplay *dispPtr = winPtr ? winPtr->dispPtr : TkGetDisplayList();
    if (!dispPtr) return;

    if (option == CLIPBOARD_CLEAR) {
        const char *wayland_display = getenv("WAYLAND_DISPLAY");
        if (wayland_display && wayland_display[0]) {
            int result = system("wl-copy --clipboard --clear");
            if (result != 0) {
                fprintf(stderr, "tkWaylandClipboard: wl-copy --clear failed with code %d\n", 
                        result);
            }
        }
        return;
    }

    if (!dispPtr->clipTargetPtr) return;

    for (TkClipboardTarget *targetPtr = dispPtr->clipTargetPtr;
         targetPtr; targetPtr = targetPtr->nextPtr) {
        if (targetPtr->type != XA_STRING &&
                targetPtr->type != dispPtr->utf8Atom) continue;

        Tcl_DString ds;
        Tcl_DStringInit(&ds);
        for (TkClipboardBuffer *buf = targetPtr->firstBufferPtr;
             buf; buf = buf->nextPtr) {
            Tcl_DStringAppend(&ds, buf->buffer, buf->length);
        }

        if (Tcl_DStringLength(&ds) > 0) {
            fprintf(stderr, "clipboard write value is %s\n", Tcl_DStringValue(&ds));
            
            /* Check if we're running under Wayland. */
            const char *wayland_display = getenv("WAYLAND_DISPLAY");
            if (wayland_display && wayland_display[0]) {
                /* Use popen with stdin to avoid shell escaping issues. */
                FILE *fp = popen("wl-copy --clipboard", "w");
                if (fp) {
                    size_t written = fwrite(Tcl_DStringValue(&ds), 1, 
                                            Tcl_DStringLength(&ds), fp);
                    if (written != (size_t)Tcl_DStringLength(&ds)) {
                        fprintf(stderr, "tkWaylandClipboard: Failed to write all data to wl-copy\n");
                    }
                    int result = pclose(fp);
                    if (result != 0) {
                        fprintf(stderr, "tkWaylandClipboard: wl-copy failed with code %d\n", 
                                result);
                    }
                } else {
                    fprintf(stderr, "tkWaylandClipboard: Failed to run wl-copy: %s\n", 
                            strerror(errno));
                }
            }
        }

        Tcl_DStringFree(&ds);
        return;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelEventProc --
 *
 *	Handle SelectionClear events (ownership lost)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears internal clipboard owner and notifies Tk of selection loss.
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
 *	Stub — not needed with GLFW/Wayland backend
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
