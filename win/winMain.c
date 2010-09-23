/*
 * winMain.c --
 *
 *	Provides a default version of the main program and Tcl_AppInit
 *	procedure for wish and other Tk-based applications.
 *
 * Copyright (c) 1993 The Regents of the University of California.
 * Copyright (c) 1994-1997 Sun Microsystems, Inc.
 * Copyright (c) 1998-1999 Scriptics Corporation.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: winMain.c,v 1.33 2010/09/23 21:45:14 nijtmans Exp $
 */

/* TODO: This file does not compile in UNICODE mode.
 * See [Freq 2965056]: Windows build with -DUNICODE
 */
#undef UNICODE
#undef _UNICODE

#include "tk.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#include <locale.h>
#include <stdlib.h>
#include <tchar.h>

#ifdef TK_TEST
extern Tcl_PackageInitProc Tktest_Init;
#endif /* TK_TEST */

#if defined(__CYGWIN__)
static void setargv(int *argcPtr, TCHAR ***argvPtr);
#endif /* __CYGWIN__ */

/*
 * Forward declarations for procedures defined later in this file:
 */

static void WishPanic(const char *format, ...);

static BOOL consoleRequired = TRUE;

/*
 * The following #if block allows you to change the AppInit function by using
 * a #define of TCL_LOCAL_APPINIT instead of rewriting this entire file. The
 * #if checks for that #define and uses Tcl_AppInit if it doesn't exist.
 */

#ifndef TK_LOCAL_APPINIT
#define TK_LOCAL_APPINIT Tcl_AppInit
#endif
extern int TK_LOCAL_APPINIT(Tcl_Interp *interp);

/*
 * The following #if block allows you to change how Tcl finds the startup
 * script, prime the library or encoding paths, fiddle with the argv, etc.,
 * without needing to rewrite Tk_Main()
 */

#ifdef TK_LOCAL_MAIN_HOOK
extern int TK_LOCAL_MAIN_HOOK(int *argc, TCHAR ***argv);
#endif

/*
 *----------------------------------------------------------------------
 *
 * WinMain --
 *
 *	Main entry point from Windows.
 *
 * Results:
 *	Returns false if initialization fails, otherwise it never returns.
 *
 * Side effects:
 *	Just about anything, since from here we call arbitrary Tcl code.
 *
 *----------------------------------------------------------------------
 */

int APIENTRY
_tWinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPTSTR lpszCmdLine,
    int nCmdShow)
{
    TCHAR **argv;
    int argc;
    TCHAR *p;

    (Tcl_SetPanicProc)(WishPanic);

    /*
     * Create the console channels and install them as the standard channels.
     * All I/O will be discarded until Tk_CreateConsoleWindow is called to
     * attach the console to a text widget.
     */

    consoleRequired = TRUE;

    /*
     * Set up the default locale to be standard "C" locale so parsing is
     * performed correctly.
     */

    setlocale(LC_ALL, "C");

    /*
     * Get our args from the c-runtime. Ignore lpszCmdLine.
     */

#if defined(__CYGWIN__)
    setargv(&argc, &argv);
#else
    argc = __argc;
    argv = __targv;
#endif

    /*
     * Forward slashes substituted for backslashes.
     */

    for (p = argv[0]; *p != TEXT('\0'); p++) {
	if (*p == TEXT('\\')) {
	    *p = TEXT('/');
	}
    }

#ifdef TK_LOCAL_MAIN_HOOK
    TK_LOCAL_MAIN_HOOK(&argc, &argv);
#endif

    Tk_Main(argc, argv, TK_LOCAL_APPINIT);
    return 0;			/* Needed only to prevent compiler warning. */
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_AppInit --
 *
 *	This procedure performs application-specific initialization. Most
 *	applications, especially those that incorporate additional packages,
 *	will have their own version of this procedure.
 *
 * Results:
 *	Returns a standard Tcl completion code, and leaves an error message in
 *	the interp's result if an error occurs.
 *
 * Side effects:
 *	Depends on the startup script.
 *
 *----------------------------------------------------------------------
 */

int
Tcl_AppInit(
    Tcl_Interp *interp)		/* Interpreter for application. */
{
    if ((Tcl_Init)(interp) == TCL_ERROR) {
	goto error;
    }
    if (Tk_Init(interp) == TCL_ERROR) {
	goto error;
    }
    Tcl_StaticPackage(interp, "Tk", Tk_Init, Tk_SafeInit);

    /*
     * Initialize the console only if we are running as an interactive
     * application.
     */

    if (consoleRequired) {
	if (Tk_CreateConsoleWindow(interp) == TCL_ERROR) {
	    goto error;
	}
    }
#if defined(STATIC_BUILD) && TCL_USE_STATIC_PACKAGES
    {
	extern Tcl_PackageInitProc Registry_Init;
	extern Tcl_PackageInitProc Dde_Init;
	extern Tcl_PackageInitProc Dde_SafeInit;

	if (Registry_Init(interp) == TCL_ERROR) {
	    goto error;
	}
	Tcl_StaticPackage(interp, "registry", Registry_Init, NULL);

	if (Dde_Init(interp) == TCL_ERROR) {
	    goto error;
	}
	Tcl_StaticPackage(interp, "dde", Dde_Init, Dde_SafeInit);
   }
#endif

#ifdef TK_TEST
    if (Tktest_Init(interp) == TCL_ERROR) {
	goto error;
    }
    Tcl_StaticPackage(interp, "Tktest", Tktest_Init, 0);
#endif /* TK_TEST */

    /*
     * Call the init procedures for included packages. Each call should look
     * like this:
     *
     * if (Mod_Init(interp) == TCL_ERROR) {
     *     return TCL_ERROR;
     * }
     *
     * where "Mod" is the name of the module. (Dynamically-loadable packages
     * should have the same entry-point name.)
     */

    /*
     * Call Tcl_CreateCommand for application-specific commands, if they
     * weren't already created by the init procedures called above.
     */

    /*
     * Specify a user-specific startup file to invoke if the application is
     * run interactively. Typically the startup file is "~/.apprc" where "app"
     * is the name of the application. If this line is deleted then no user-
     * specific startup file will be run under any conditions.
     */

    (Tcl_SetVar)(interp, "tcl_rcFileName", "~/wishrc.tcl", TCL_GLOBAL_ONLY);
    return TCL_OK;

error:
    MessageBeep(MB_ICONEXCLAMATION);
    MessageBoxA(NULL, (Tcl_GetStringResult)(interp), "Error in Wish",
	    MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
    ExitProcess(1);

    /*
     * We won't reach this, but we need the return.
     */

    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * WishPanic --
 *
 *	Display a message and exit.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Exits the program.
 *
 *----------------------------------------------------------------------
 */

void
WishPanic(
    const char *format, ...)
{
    va_list argList;
    char buf[1024];

    va_start(argList, format);
    vsprintf(buf, format, argList);

    MessageBeep(MB_ICONEXCLAMATION);
    MessageBoxA(NULL, buf, "Fatal Error in Wish",
	    MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
#ifdef _MSC_VER
    DebugBreak();
#endif
    ExitProcess(1);
}

#if !defined(__GNUC__) || defined(TK_TEST)
/*
 *----------------------------------------------------------------------
 *
 * main --
 *
 *	Main entry point from the console.
 *
 * Results:
 *	None: Tk_Main never returns here, so this procedure never returns
 *	either.
 *
 * Side effects:
 *	Whatever the applications does.
 *
 *----------------------------------------------------------------------
 */

int
_tmain(
    int argc,
    TCHAR **argv)
{
    (Tcl_SetPanicProc)(WishPanic);

    /*
     * Set up the default locale to be standard "C" locale so parsing is
     * performed correctly.
     */

    setlocale(LC_ALL, "C");

    /*
     * Console emulation widget not required as this entry is from the
     * console subsystem, thus stdin,out,err already have end-points.
     */

    consoleRequired = FALSE;

#ifdef TK_LOCAL_MAIN_HOOK
    TK_LOCAL_MAIN_HOOK(&argc, &argv);
#endif

    Tk_Main(argc, argv, Tcl_AppInit);
    return 0;
}
#endif /* !__GNUC__ || TK_TEST */


/*
 *-------------------------------------------------------------------------
 *
 * setargv --
 *
 *	Parse the Windows command line string into argc/argv. Done here
 *	because we don't trust the builtin argument parser in crt0. Windows
 *	applications are responsible for breaking their command line into
 *	arguments.
 *
 *	2N backslashes + quote -> N backslashes + begin quoted string
 *	2N + 1 backslashes + quote -> literal
 *	N backslashes + non-quote -> literal
 *	quote + quote in a quoted string -> single quote
 *	quote + quote not in quoted string -> empty string
 *	quote -> begin quoted string
 *
 * Results:
 *	Fills argcPtr with the number of arguments and argvPtr with the array
 *	of arguments.
 *
 * Side effects:
 *	Memory allocated.
 *
 *--------------------------------------------------------------------------
 */

#if defined(__CYGWIN__)
static void
setargv(
    int *argcPtr,		/* Filled with number of argument strings. */
    TCHAR ***argvPtr)		/* Filled with argument strings (malloc'd). */
{
    TCHAR *cmdLine, *p, *arg, *argSpace;
    TCHAR **argv;
    int argc, size, inquote, copy, slashes;

    cmdLine = GetCommandLine();

    /*
     * Precompute an overly pessimistic guess at the number of arguments in
     * the command line by counting non-space spans.
     */

    size = 2;
    for (p = cmdLine; *p != TEXT('\0'); p++) {
	if ((*p == TEXT(' ')) || (*p == TEXT('\t'))) {	/* INTL: ISO space. */
	    size++;
	    while ((*p == TEXT(' ')) || (*p == TEXT('\t'))) { /* INTL: ISO space. */
		p++;
	    }
	    if (*p == TEXT('\0')) {
		break;
	    }
	}
    }
    argSpace = (TCHAR *) ckalloc(
	    (unsigned) (size * sizeof(TCHAR *) + (_tcslen(cmdLine) * sizeof(TCHAR)) + 1));
    argv = (TCHAR **) argSpace;
    argSpace += size * sizeof(TCHAR *);
    size--;

    p = cmdLine;
    for (argc = 0; argc < size; argc++) {
	argv[argc] = arg = argSpace;
	while ((*p == TEXT(' ')) || (*p == TEXT('\t'))) {	/* INTL: ISO space. */
	    p++;
	}
	if (*p == TEXT('\0')) {
	    break;
	}

	inquote = 0;
	slashes = 0;
	while (1) {
	    copy = 1;
	    while (*p == TEXT('\\')) {
		slashes++;
		p++;
	    }
	    if (*p == TEXT('"')) {
		if ((slashes & 1) == 0) {
		    copy = 0;
		    if ((inquote) && (p[1] == TEXT('"'))) {
			p++;
			copy = 1;
		    } else {
			inquote = !inquote;
		    }
		}
		slashes >>= 1;
	    }

	    while (slashes) {
		*arg = TEXT('\\');
		arg++;
		slashes--;
	    }

	    if ((*p == TEXT('\0')) || (!inquote &&
		    ((*p == TEXT(' ')) || (*p == TEXT('\t'))))) {	/* INTL: ISO space. */
		break;
	    }
	    if (copy != 0) {
		*arg = *p;
		arg++;
	    }
	    p++;
	}
	*arg = TEXT('\0');
	argSpace = arg + 1;
    }
    argv[argc] = NULL;

    *argcPtr = argc;
    *argvPtr = argv;
}
#endif /* __CYGWIN__ */

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
