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

static int		DebuggerObjCmd (ClientData dummy, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);

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

    Tcl_CreateObjCommand(interp, "debugger", DebuggerObjCmd,
	    (ClientData) 0, (Tcl_CmdDeleteProc *) NULL);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DebuggerObjCmd --
 *
 *	This procedure simply calls the low level debugger.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 *
 * TkTestAppIsDrawing --
 *
 *      A widget display procedure can call this to determine whether it
 *      is being run inside of the drawRect method.  This is needed for
 *      some tests, especially of the Text widget, which record data in
 *      a global Tcl variable and assume that display procedures will be
 *      run in a predictable sequence as Tcl idle tasks.
 *
 * Results:
 *	True only while running the drawRect method of a TKContentView;
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
