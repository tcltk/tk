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

/* -------------------------------------------------------------------------
 *                          Public API stubs
 * ------------------------------------------------------------------------- */

int
Tk_UseWindow(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    const char *string)
{
    /* Embedding not supported in this build/configuration */
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
    /* No-op — container mode is not implemented */
}

Tk_Window
Tk_GetOtherWindow(
    Tk_Window tkwin)
{
    return NULL;   /* no other half exists */
}

Window
TkUnixContainerId(
    TkWindow *winPtr)
{
    return None;   /* no container window */
}

/* -------------------------------------------------------------------------
 *                   GLFW / Wayland related stubs (if needed)
 * ------------------------------------------------------------------------- */

void
TkpRedirectKeyEvent(
    TkWindow *winPtr,
    XEvent *eventPtr)
{
    /* No embedding → no redirection needed */
}

void
TkpClaimFocus(
    TkWindow *topLevelPtr,
    int force)
{
    /* No embedding → cannot claim focus via container */
}

/* -------------------------------------------------------------------------
 *                      Busy window support remains (non-embedding)
 * ------------------------------------------------------------------------- */

void
TkpShowBusyWindow(
    TkBusy busy)
{
    /* This part is usually still useful → keep original or minimal impl */
    /* (you can keep the original implementation from tkBusy.c if desired) */
}

void
TkpHideBusyWindow(
    TkBusy busy)
{
    /* same as above */
}

void
TkpMakeTransparentWindowExist(
    Tk_Window tkwin,
    Window parent)
{
    /* Usually not needed when embedding is disabled */
}

void
TkpCreateBusy(
    Tk_FakeWin *winPtr,
    Tk_Window tkRef,
    Window *parentPtr,
    Tk_Window tkParent,
    TkBusy busy)
{
    /* Minimal or no-op depending on your needs */
    *parentPtr = None;
}

/* -------------------------------------------------------------------------
 *                      Obsolete / test commands — disabled
 * ------------------------------------------------------------------------- */

int
TkpTestembedCmd(
    void *dummy,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    Tcl_SetObjResult(interp, Tcl_NewStringObj(
        "embedding support is disabled in this build", -1));
    return TCL_ERROR;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */