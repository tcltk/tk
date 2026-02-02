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


int
Tk_UseWindow(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    const char *string)
{
    /* Embedding not supported in this build/configuration. */
    if (interp) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "window embedding is not supported in this build (Wayland/GLFW/no XEmbed)", -1));
        Tcl_SetErrorCode(interp, "TK", "EMBED", "UNSUPPORTED", (char *)NULL);
    }
    return TCL_ERROR;
}

void
Tk_MakeContainer(
    Tk_Window tkwin)
{
    /* No-op — container mode is not implemented. */
}

Tk_Window
Tk_GetOtherWindow(
    Tk_Window tkwin)
{
    return NULL;   /* No other half exists. */
}

Window
TkUnixContainerId(
    TkWindow *winPtr)
{
    return None;   /* No container window. */
}


void
TkpRedirectKeyEvent(
    TkWindow *winPtr,
    XEvent *eventPtr)
{
    /* No embedding → no redirection needed. */
}

void
TkpClaimFocus(
    TkWindow *topLevelPtr,
    int force)
{
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
