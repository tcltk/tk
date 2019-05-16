/*
 * tkMacOSXTest.c --
 *
 *	Contains commands for platform specific tests for
 *	the Macintosh platform.
 *
 * Copyright (c) 1996 Sun Microsystems, Inc.
 * Copyright 2001-2009, Apple Inc.
 * Copyright (c) 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"

/*
 * Forward declarations of procedures defined later in this file:
 */

#if MAC_OS_X_VERSION_MAX_ALLOWED < 1080
static int		DebuggerObjCmd (ClientData dummy, Tcl_Interp *interp,
					int objc, Tcl_Obj *const objv[]);
#endif


/*
 *----------------------------------------------------------------------
 *
 * TkplatformtestInit --
 *
 *	Defines commands that test platform specific functionality for
 *	Unix platforms.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Defines new commands.
 *
 *----------------------------------------------------------------------
 */

int
TkplatformtestInit(
    Tcl_Interp *interp)		/* Interpreter to add commands to. */
{
    /*
     * Add commands for platform specific tests on MacOS here.
     */

#if MAC_OS_X_VERSION_MAX_ALLOWED < 1080
    Tcl_CreateObjCommand(interp, "debugger", DebuggerObjCmd,
	    (ClientData) 0, (Tcl_CmdDeleteProc *) NULL);
#endif

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DebuggerObjCmd --
 *
 *	This procedure simply calls the low level debugger, which was
 *      deprecated in OSX 10.8.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#if MAC_OS_X_VERSION_MAX_ALLOWED < 1080
static int
DebuggerObjCmd(
    ClientData clientData,		/* Not used. */
    Tcl_Interp *interp,			/* Not used. */
    int objc,				/* Not used. */
    Tcl_Obj *const objv[])			/* Not used. */
{
    Debugger();
    return TCL_OK;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * TkTestAppIsDrawing --
 *
 *      A test widget display procedure which records calls can use this to
 *      detect whether it is being called from within [NSView drawRect].
 *      If so, it probably should not be recording the call since it was
 *      probably generated spontaneously by the window manager rather than
 *      by an explicit call to update. This is just a wrapper for the NSApp
 *      property.
 *
 *
 * Results:
 *      Returns true if and only if called from within [NSView drawRect].
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */
MODULE_SCOPE Bool
TkTestAppIsDrawing(void) {
    return [NSApp isDrawing];
}


/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
