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
 * RCS: @(#) $Id: tkWin32Dll.c,v 1.6.2.2 2004/10/28 20:11:20 mdejong Exp $
 */

#include "tkWinInt.h"
#ifndef STATIC_BUILD

#if defined(HAVE_NO_SEH) && defined(TCL_MEM_DEBUG)
static void *INITIAL_ESP,
            *INITIAL_EBP,
            *INITIAL_HANDLER,
            *RESTORED_ESP,
            *RESTORED_EBP,
            *RESTORED_HANDLER;
#endif /* HAVE_NO_SEH && TCL_MEM_DEBUG */

#ifdef HAVE_NO_SEH
static
__attribute__ ((cdecl))
EXCEPTION_DISPOSITION
_except_dllmain_detach_handler(
    struct _EXCEPTION_RECORD *ExceptionRecord,
    void *EstablisherFrame,
    struct _CONTEXT *ContextRecord,
    void *DispatcherContext);
#endif /* HAVE_NO_SEH */

#ifdef HAVE_NO_SEH

/* Need to add noinline flag to DllMain declaration so that gcc -O3
 * does not inline asm code into DllEntryPoint and cause a
 * compile time error because of redefined local labels.
 */

BOOL WINAPI		DllMain(HINSTANCE hInst, DWORD reason, 
				LPVOID reserved)
                        __attribute__ ((noinline));

#else

/*
 * The following declaration is for the VC++ DLL entry point.
 */

BOOL WINAPI		DllMain _ANSI_ARGS_((HINSTANCE hInst,
			    DWORD reason, LPVOID reserved));
#endif /* HAVE_NO_SEH */

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

BOOL WINAPI
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

BOOL WINAPI
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

#ifdef HAVE_NO_SEH
# ifdef TCL_MEM_DEBUG
    __asm__ __volatile__ (
            "movl %%esp,  %0" "\n\t"
            "movl %%ebp,  %1" "\n\t"
            "movl %%fs:0, %2" "\n\t"
            : "=m"(INITIAL_ESP),
              "=m"(INITIAL_EBP),
              "=r"(INITIAL_HANDLER) );
# endif /* TCL_MEM_DEBUG */
            
    __asm__ __volatile__ (
            "pushl %%ebp" "\n\t"
            "pushl %0" "\n\t"
            "pushl %%fs:0" "\n\t"
            "movl  %%esp, %%fs:0"
            :
            : "r" (_except_dllmain_detach_handler) );
#else
	__try {
#endif /* HAVE_NO_SEH */
	    /*
	     * Run and remove our exit handlers, if they haven't already
	     * been run.  Just in case we are being unloaded prior to
	     * Tcl (it can happen), we won't leave any dangling pointers
	     * hanging around for when Tcl gets unloaded later.
	     */

	    TkFinalize(NULL);

#ifdef HAVE_NO_SEH
    __asm__ __volatile__ (
            "jmp  dllmain_detach_pop" "\n"
        "dllmain_detach_reentry:" "\n\t"
            "movl %%fs:0, %%eax" "\n\t"
            "movl 0x8(%%eax), %%esp" "\n\t"
            "movl 0x8(%%esp), %%ebp" "\n"
        "dllmain_detach_pop:" "\n\t"
            "movl (%%esp), %%eax" "\n\t"
            "movl %%eax, %%fs:0" "\n\t"
            "add  $12, %%esp" "\n\t"
            :
            :
            : "%eax");

# ifdef TCL_MEM_DEBUG
    __asm__ __volatile__ (
            "movl  %%esp,  %0" "\n\t"
            "movl  %%ebp,  %1" "\n\t"
            "movl  %%fs:0, %2" "\n\t"
            : "=m"(RESTORED_ESP),
              "=m"(RESTORED_EBP),
              "=r"(RESTORED_HANDLER) );

    if (INITIAL_ESP != RESTORED_ESP)
        Tcl_Panic("ESP restored incorrectly");
    if (INITIAL_EBP != RESTORED_EBP)
        Tcl_Panic("EBP restored incorrectly");
    if (INITIAL_HANDLER != RESTORED_HANDLER)
        Tcl_Panic("HANDLER restored incorrectly");
# endif /* TCL_MEM_DEBUG */
#else
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	    /* empty handler body */
	}
#endif /* HAVE_NO_SEH */
	break;
    }
    return TRUE;
}
/*
 *----------------------------------------------------------------------
 *
 * _except_dllmain_detach_handler --
 *
 *	SEH exception handler for DllMain.
 *
 * Results:
 *	See DllMain.
 *
 * Side effects:
 *	See DllMain.
 *
 *----------------------------------------------------------------------
 */
#ifdef HAVE_NO_SEH
static
__attribute__ ((cdecl))
EXCEPTION_DISPOSITION
_except_dllmain_detach_handler(
    struct _EXCEPTION_RECORD *ExceptionRecord,
    void *EstablisherFrame,
    struct _CONTEXT *ContextRecord,
    void *DispatcherContext)
{
    __asm__ __volatile__ (
            "jmp dllmain_detach_reentry");
    return 0; /* Function does not return */
}
#endif /* HAVE_NO_SEH */

#endif /* !STATIC_BUILD */
