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
    Tcl_CreateObjCommand(interp, "debugger", DebuggerObjCmd, NULL, NULL);
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
 * TkTestLogDisplay --
 *
 *      The test image display procedure calls this to determine whether it
 *      should write a log message recording that it has being run.  On OSX
 *      10.14 and later, only calls to the display procedure which occur inside
 *      of the drawRect method should be logged, since those are the only ones
 *      which actually draw anything.  On earlier systems the opposite is true.
 *      The calls from within the drawRect method are redundant, since the
 *      first time the display procedure is run it will do the drawing and that
 *      first call will usually not occur inside of drawRect.
 *
 * Results:
 *      On OSX 10.14 and later, returns true if and only if called from
 *      within [NSView drawRect].  On earlier systems returns false if
 *      and only if called from with [NSView drawRect].
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */
MODULE_SCOPE Bool
TkTestLogDisplay(void) {
    if ([NSApp macMinorVersion] >= 14) {
	return [NSApp isDrawing];
    } else {
	return ![NSApp isDrawing];
    }
}


/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
