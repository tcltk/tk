/*
 * tkWin32Dll.c --
 *
 *	This file contains a stub dll entry point.
 *
 * Copyright © 1995 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkWinInt.h"
#ifndef STATIC_BUILD

#if defined(__GNUC__)

/*
 * Need to add noinline flag to DllMain declaration so that gcc -O3 does not
 * inline asm code into DllEntryPoint and cause a compile time error because
 * of redefined local labels.
 */

BOOL APIENTRY		DllMain(HINSTANCE hInst, DWORD reason,
			    LPVOID reserved) __attribute__ ((noinline));

#else

/*
 * The following declaration is for the VC++ DLL entry point.
 */

BOOL APIENTRY		DllMain(HINSTANCE hInst, DWORD reason,
			    LPVOID reserved);
#endif

/*
 *----------------------------------------------------------------------
 *
 * DllEntryPoint --
 *
 *	This wrapper function is used by Borland to invoke the initialization
 *	code for Tk. It simply calls the DllMain routine.
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
DllEntryPoint(
    HINSTANCE hInst,		/* Library instance handle. */
    DWORD reason,		/* Reason this function is being called. */
    LPVOID reserved)		/* Not used. */
{
    return DllMain(hInst, reason, reserved);
}

/*
 *----------------------------------------------------------------------
 *
 * DllMain --
 *
 *	DLL entry point. It is only necessary to specify our dll here so that
 *	resources are found correctly. Otherwise Tk will initialize and clean
 *	up after itself through other methods, in order to be consistent
 *	whether the build is static or dynamic.
 *
 * Results:
 *	Always TRUE.
 *
 * Side effects:
 *	This might call some synchronization functions, but MSDN documentation
 *	states: "Waiting on synchronization objects in DllMain can cause a
 *	deadlock."
 *
 *----------------------------------------------------------------------
 */

BOOL APIENTRY
DllMain(
    HINSTANCE hInstance,
    DWORD reason,
    LPVOID reserved)
{
    (void)reserved;

    /*
     * If we are attaching to the DLL from a new process, tell Tk about the
     * hInstance to use.
     */

    switch (reason) {
    case DLL_PROCESS_ATTACH:
	DisableThreadLibraryCalls(hInstance);
	TkWinSetHINSTANCE(hInstance);
	break;

    case DLL_PROCESS_DETACH:
	TkFinalize(NULL);
	break;
    }
    return TRUE;
}

#endif /* !STATIC_BUILD */

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
