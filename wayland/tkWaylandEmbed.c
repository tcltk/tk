/*
 * tkWaylandEmbed.c --
 *
 *      This file is intentionally a no-op on platforms/builds that do not
 *      support (or want to support) classic X11-style window embedding.
 *      This includes Wayland-only environments, GLFW-based applications,
 *      and many modern desktop compositors.
 *
 *      All embedding-related functions either return failure or do nothing.
 *
 *      Copyright © 1996-1997 Sun Microsystems, Inc.
 *      Copyright © 2026 Kevin Walzer
 */

#include "tkUnixInt.h"
#include "tkBusy.h"


/*
 *----------------------------------------------------------------------
 *
 * Tk_UseWindow --
 *
 *	This function causes a Tk window to use a given X window as its parent
 *	window, rather than the root window for the screen. It is invoked by
 *	an embedded application to specify the window in which it is embedded.
 *
 * Results:
 *	The return value is normally TCL_OK. If an error occurs (such as
 *	string not being a valid window spec), then the return value is
 *	TCL_ERROR and an error message is left in the interp's result if
 *	interp is non-NULL.
 *
 * Side effects:
 *	Changes the colormap and other visual information to match that of the
 *	parent window given by "string".
 *
 *----------------------------------------------------------------------
 */

int
Tk_UseWindow(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    const char *string)
{
    TCL_UNUSED(tkwin);
    TCL_UNUSED(string);
    
    /* Embedding not supported in this build/configuration. */
    if (interp) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "window embedding is not supported in this build (Wayland/GLFW/no XEmbed)", -1));
        Tcl_SetErrorCode(interp, "TK", "EMBED", "UNSUPPORTED", (char *)NULL);
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MakeContainer --
 *
 *	This function is called to indicate that a particular window will be a
 *	container for an embedded application. This changes certain aspects of
 *	the window's behavior, such as whether it will receive events anymore.
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
Tk_MakeContainer(
    Tk_Window tkwin)
{
    TCL_UNUSED(tkwin);
    /* No-op — container mode is not implemented. */
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetOtherWindow --
 *
 *	If both the container and embedded window are in the same process,
 *	this procedure will return either one, given the other.
 *
 * Results:
 *	If tkwin is a container, the return value is the token for the
 *	embedded window, and vice versa. If the "other" window isn't in this
 *	process, NULL is returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tk_Window
Tk_GetOtherWindow(
    Tk_Window tkwin)
{
    TCL_UNUSED(tkwin);
    return NULL;   /* No other half exists. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkUnixContainerId --
 *
 *	Given an embedded window, this function returns the X window
 *	identifier for the associated container window.
 *
 * Results:
 *	The return value is the X window identifier for winPtr's container
 *	window.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Window
TkUnixContainerId(
    TkWindow *winPtr)
{
    TCL_UNUSED(winPtr);
    return None;   /* No container window. */
}


/*
 *----------------------------------------------------------------------
 *
 * TkpRedirectKeyEvent --
 *
 *	This procedure is invoked when a key press or release event arrives
 *	for an application that does not believe it owns the input focus. This
 *	can happen because of embedding; for example, X can send an event to
 *	an embedded application when the real focus window is in the container
 *	application and is an ancestor of the container. This procedure's job
 *	is to forward the event back to the application where it really
 *	belongs.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The event may get sent to a different application.
 *
 *----------------------------------------------------------------------
 */

void
TkpRedirectKeyEvent(
    TkWindow *winPtr,
    XEvent *eventPtr)
{
    TCL_UNUSED(winPtr);
    TCL_UNUSED(eventPtr);
    /* No embedding → no redirection needed. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpClaimFocus --
 *
 *	This procedure is invoked when someone asks for the input focus to be
 *	put on a window in an embedded application.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The input focus may change.
 *
 *----------------------------------------------------------------------
 */

void
TkpClaimFocus(
    TkWindow *topLevelPtr,
    int force)
{
    TCL_UNUSED(topLevelPtr);
    TCL_UNUSED(force);
    /* No embedding → cannot claim focus via container. */
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */