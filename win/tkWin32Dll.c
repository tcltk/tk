/* 
 * tkWin32Dll.c --
 *
 *	This file contains a stub dll entry point.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkWin32Dll.c,v 1.7 2003/12/21 23:50:13 davygrvy Exp $
 */

#include "tkWinInt.h"
#ifndef STATIC_BUILD

/*
 * The following declaration is for the VC++ DLL entry point.
 */

BOOL APIENTRY		DllMain _ANSI_ARGS_((HINSTANCE hInst,
			    DWORD reason, LPVOID reserved));

/*
 *----------------------------------------------------------------------
 *
 * DllEntryPoint --
 *
 *	This wrapper function is used by Borland to invoke the
 *	initialization code for Tk.  It simply calls the DllMain
 *	routine.
 *
 * Results:
 *	See DllMain.
 *
 * Side effects:
 *	See DllMain.
 *
 *----------------------------------------------------------------------
 */

BOOL APIENTRY
DllEntryPoint(hInst, reason, reserved)
    HINSTANCE hInst;		/* Library instance handle. */
    DWORD reason;		/* Reason this function is being called. */
    LPVOID reserved;		/* Not used. */
{
    return DllMain(hInst, reason, reserved);
}

/*
 *----------------------------------------------------------------------
 *
 * DllMain --
 *
 *	DLL entry point.  It is only necessary to specify our dll here so
 *	that resources are found correctly.  Otherwise Tk will initialize
 *	and clean up after itself through other methods, in order to be
 *	consistent whether the build is static or dynamic.
 *
 * Results:
 *	Always TRUE.
 *
 * Side effects:
 *	This might call some sycronization functions, but MSDN
 *	documentation states: "Waiting on synchronization objects in
 *	DllMain can cause a deadlock."
 *
 *----------------------------------------------------------------------
 */

BOOL APIENTRY
DllMain(hInstance, reason, reserved)
    HINSTANCE hInstance;
    DWORD reason;
    LPVOID reserved;
{
    /*
     * If we are attaching to the DLL from a new process, tell Tk about
     * the hInstance to use.
     */

    switch (reason) {
    case DLL_PROCESS_ATTACH:
	DisableThreadLibraryCalls(hInstance);
	TkWinSetHINSTANCE(hInstance);
	break;

    case DLL_PROCESS_DETACH:
	/*
	 * Protect the call to TkFinalize in an SEH block.  We can't
	 * be guarenteed Tk is always being unloaded from a stable
	 * condition.
	 */

	__try {
	    /*
	     * Run and remove our exit handlers, if they haven't already
	     * been run.  Just in case we are being unloaded prior to
	     * Tcl (it can happen), we won't leave any dangling pointers
	     * hanging around for when Tcl gets unloaded later.
	     */

	    TkFinalize(NULL);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	    /* empty handler body */
	}
	break;
    }
    return TRUE;
}
#endif /* !STATIC_BUILD */

