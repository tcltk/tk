/*
 * tkMain.c --
 *
 *	This file contains a generic main program for Tk-based applications.
 *	It can be used as-is for many applications, just by supplying a
 *	different appInitProc function for each specific application. Or, it
 *	can be used as a template for creating new main programs for Tk
 *	applications.
 *
 * Copyright © 1990-1994 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"

#if defined(_WIN32) && !defined(UNICODE) && !defined(STATIC_BUILD)
MODULE_SCOPE void TkCygwinMainEx(Tcl_Size, char **, Tcl_AppInitProc *, Tcl_Interp *);
#endif

/*
 * The default prompt used when the user has not overridden it.
 */

static const char DEFAULT_PRIMARY_PROMPT[] = "% ";
static const char ENCODING_ERROR[] = "\n\t(encoding error in stderr)";

/*
 * This file can be compiled on Windows in UNICODE mode, as well as on all
 * other platforms using the native encoding. This is done by using the normal
 * Windows functions like _tcscmp, but on platforms which don't have <tchar.h>
 * we have to translate that to strcmp here.
 */
#ifdef _WIN32
#ifdef __cplusplus
extern "C" {
#endif
/*  Little hack to eliminate the need for "tclInt.h" here:
    Just copy a small portion of TclIntPlatStubs, just
    enough to make it work. See [600b72bfbc] */
typedef struct TclIntPlatStubs {
    int magic;
    void *hooks;
    void (*dummy[16]) (void); /* dummy entries 0-15, not used */
    int (*tclpIsAtty) (int fd); /* 16 */
} TclIntPlatStubs;
extern const TclIntPlatStubs *tclIntPlatStubsPtr;
#ifdef __cplusplus
}
#endif
#   include "tkWinInt.h"
#else
#   define TCHAR char
#   define TEXT(arg) arg
#   define _tcscmp strcmp
#   define _tcslen strlen
#   define _tcsncmp strncmp
#endif

#ifdef MAC_OSX_TK
#include "tkMacOSXInt.h"
#endif

static inline Tcl_Obj *
NewNativeObj(
    TCHAR *string)
{
    Tcl_Obj *obj;
    Tcl_DString ds;
    const char *str;

#if defined(_WIN32) && defined(UNICODE)
    Tcl_DStringInit(&ds);
    Tcl_WCharToUtfDString(string, wcslen(string), &ds);
    str = Tcl_DStringValue(&ds);
#else
    str = Tcl_ExternalToUtfDString(NULL, (char *)string, strlen(string), &ds);
#endif
    obj = Tcl_NewStringObj(str, Tcl_DStringLength(&ds));
    Tcl_DStringFree(&ds);
    return obj;
}

/*
 * Declarations for various library functions and variables (don't want to
 * include tclInt.h or tclPort.h here, because people might copy this file out
 * of the Tk source directory to make their own modified versions). Note: do
 * not declare "exit" here even though a declaration is really needed, because
 * it will conflict with a declaration elsewhere on some systems.
 */

#if defined(_WIN32)
#define isatty WinIsTty
static int WinIsTty(int fd) {
    HANDLE handle;

    /*
     * For now, under Windows, we assume we are not running as a console mode
     * app, so we need to use the GUI console. In order to enable this, we
     * always claim to be running on a tty. This probably isn't the right way
     * to do it.
     */

#if !defined(STATIC_BUILD)
	if (tclStubsPtr->tcl_CreateFileHandler && tclIntPlatStubsPtr->tclpIsAtty) {
	    /* We are running on Cygwin */
	    return tclIntPlatStubsPtr->tclpIsAtty(fd);
	}
#endif
    handle = GetStdHandle(STD_INPUT_HANDLE + fd);
	/*
	 * If it's a bad or closed handle, then it's been connected to a wish
	 * console window. A character file handle is a tty by definition.
	 */
    return (handle == INVALID_HANDLE_VALUE) || (handle == 0)
	     || (GetFileType(handle) == FILE_TYPE_UNKNOWN)
	     || (GetFileType(handle) == FILE_TYPE_CHAR);
}
#else
extern int		isatty(int fd);
#endif

typedef struct {
    Tcl_Channel input;		/* The standard input channel from which lines
				 * are read. */
    int tty;			/* Non-zero means standard input is a
				 * terminal-like device. Zero means it's a
				 * file. */
    Tcl_DString command;	/* Used to assemble lines of terminal input
				 * into Tcl commands. */
    Tcl_DString line;		/* Used to read the next line from the
				 * terminal input. */
    int gotPartial;
    Tcl_Interp *interp;		/* Interpreter that evaluates interactive
				 * commands. */
} InteractiveState;

/*
 * Forward declarations for functions defined later in this file.
 */

static void		Prompt(Tcl_Interp *interp, InteractiveState *isPtr);
static void		StdinProc(void *clientData, int mask);

/*
 *----------------------------------------------------------------------
 *
 * Tk_MainEx --
 *
 *	Main program for Wish and most other Tk-based applications.
 *
 * Results:
 *	None. This function never returns (it exits the process when it's
 *	done).
 *
 * Side effects:
 *	This function initializes the Tk world and then starts interpreting
 *	commands; almost anything could happen, depending on the script being
 *	interpreted.
 *
 *----------------------------------------------------------------------
 */

void
Tk_MainEx(
    Tcl_Size argc,			/* Number of arguments. */
    TCHAR **argv,		/* Array of argument strings. */
    Tcl_AppInitProc *appInitProc,
				/* Application-specific initialization
				 * function to call after most initialization
				 * but before starting to execute commands. */
    Tcl_Interp *interp)
{
    int i=0;			/* argv[i] index */
    Tcl_Obj *path, *argvPtr, *appName;
    const char *encodingName;
    int code, nullStdin = 0;
    Tcl_Channel chan;
    InteractiveState is;

    if (0 < argc) {
	--argc;			/* "consume" argv[0] */
	++i;
    }

    /*
     * Ensure that we are getting a compatible version of Tcl.
     */

    if (Tcl_InitStubs(interp, "9.0", 0) == NULL) {
	Tcl_Panic("%s", Tcl_GetString(Tcl_GetObjResult(interp)));
    }

#if defined(_WIN32) && !defined(UNICODE) && !defined(STATIC_BUILD)

    if (tclStubsPtr->tcl_CreateFileHandler) {
	/* We are running win32 Tk under Cygwin, so let's check
	 * whether the env("DISPLAY") variable or the -display
	 * argument is set. If so, we really want to run the
	 * Tk_MainEx function of libtcl9tk9.?.dll, not this one. */
	if (Tcl_GetVar2(interp, "env", "DISPLAY", TCL_GLOBAL_ONLY)) {
	loadCygwinTk:
	    TkCygwinMainEx(argc, argv, appInitProc, interp);
	    /* Only returns when Tk_MainEx() was not found */
	} else {
	    Tcl_Size j;

	    for (j = 1; j < argc; ++j) {
		if (!strcmp(argv[j], "-display")) {
		    goto loadCygwinTk;
		}
	    }
	}
    }
#endif

    Tcl_InitMemory(interp);

    is.interp = interp;
    is.gotPartial = 0;
    Tcl_Preserve(interp);

#if defined(_WIN32)
#if !defined(STATIC_BUILD)
    /* If compiled for Win32 but running on Cygwin, don't use console */
    if (!tclStubsPtr->tcl_CreateFileHandler)
#endif
    Tk_InitConsoleChannels(interp);
#endif

#ifdef MAC_OSX_TK
    if (Tcl_GetStartupScript(NULL) == NULL) {
	TkMacOSXDefaultStartupScript();
    }
#endif

    /*
     * If the application has not already set a startup script, parse the
     * first few command line arguments to determine the script path and
     * encoding.
     */

    if (NULL == Tcl_GetStartupScript(NULL)) {

	/*
	 * Check whether first 3 args (argv[1] - argv[3]) look like
	 *  -encoding ENCODING FILENAME
	 * or like
	 *  FILENAME
	 */

	/* mind argc is being adjusted as we proceed */
	if ((argc >= 3) && (0 == _tcscmp(TEXT("-encoding"), argv[1]))
		&& ('-' != argv[3][0])) {
	    Tcl_Obj *value = NewNativeObj(argv[2]);
	    Tcl_SetStartupScript(NewNativeObj(argv[3]), Tcl_GetString(value));
	    Tcl_DecrRefCount(value);
	    argc -= 3;
	    i += 3;
	} else if ((argc >= 1) && ('-' != argv[1][0])) {
	    Tcl_SetStartupScript(NewNativeObj(argv[1]), NULL);
	    argc--;
	    i++;
	}
    }

    path = Tcl_GetStartupScript(&encodingName);
    if (path == NULL) {
	appName = NewNativeObj(argv[0]);
    } else {
	appName = path;
    }
    Tcl_SetVar2Ex(interp, "argv0", NULL, appName, TCL_GLOBAL_ONLY);

    Tcl_SetVar2Ex(interp, "argc", NULL, Tcl_NewWideIntObj(argc), TCL_GLOBAL_ONLY);

    argvPtr = Tcl_NewListObj(0, NULL);
    while (argc--) {
	Tcl_ListObjAppendElement(NULL, argvPtr, NewNativeObj(argv[i++]));
    }
    Tcl_SetVar2Ex(interp, "argv", NULL, argvPtr, TCL_GLOBAL_ONLY);

    /*
     * Set the "tcl_interactive" variable.
     */

    is.tty = isatty(0);
#if defined(MAC_OSX_TK)
    /*
     * On TkAqua, if we don't have a TTY and stdin is a special character file
     * of length 0, (e.g. /dev/null, which is what Finder sets when double
     * clicking Wish) then use the GUI console.
     */

    if (!is.tty) {
	struct stat st;

	nullStdin = fstat(0, &st) || (S_ISCHR(st.st_mode) && !st.st_blocks);
    }
#endif
    Tcl_SetVar2Ex(interp, "tcl_interactive", NULL,
	    Tcl_NewBooleanObj(!path && (is.tty || nullStdin)), TCL_GLOBAL_ONLY);

    /*
     * Invoke application-specific initialization.
     */

    if (appInitProc(interp) != TCL_OK) {
	TkpDisplayWarning(Tcl_GetString(Tcl_GetObjResult(interp)),
		"application-specific initialization failed");
    }

    /*
     * Invoke the script specified on the command line, if any. Must fetch it
     * again, as the appInitProc might have reset it.
     */

    path = Tcl_GetStartupScript(&encodingName);
    if (path != NULL) {
	Tcl_ResetResult(interp);
	code = Tcl_FSEvalFileEx(interp, path, encodingName);
	if (code != TCL_OK) {
	    /*
	     * The following statement guarantees that the errorInfo variable
	     * is set properly.
	     */

	    Tcl_AddErrorInfo(interp, "");
	    TkpDisplayWarning(Tcl_GetVar2(interp, "errorInfo", NULL,
		    TCL_GLOBAL_ONLY), "Error in startup script");
	    Tcl_DeleteInterp(interp);
	    Tcl_Exit(1);
	}
	is.tty = 0;
    } else {

	/*
	 * Evaluate the .rc file, if one has been specified.
	 */

	Tcl_SourceRCFile(interp);

	/*
	 * Establish a channel handler for stdin.
	 */

	is.input = Tcl_GetStdChannel(TCL_STDIN);
	if (is.input) {
	    Tcl_CreateChannelHandler(is.input, TCL_READABLE, StdinProc, &is);
	}
	if (is.tty) {
	    Prompt(interp, &is);
	}
    }

    chan = Tcl_GetStdChannel(TCL_STDOUT);
    if (chan) {
	Tcl_Flush(chan);
    }
    Tcl_DStringInit(&is.command);
    Tcl_DStringInit(&is.line);
    Tcl_ResetResult(interp);

    /*
     * Loop infinitely, waiting for commands to execute. When there are no
     * windows left, Tk_MainLoop returns and we exit.
     */

    Tk_MainLoop();
    Tcl_DeleteInterp(interp);
    Tcl_Release(interp);
    Tcl_SetStartupScript(NULL, NULL);
    Tcl_Exit(0);
}

/*
 *----------------------------------------------------------------------
 *
 * StdinProc --
 *
 *	This function is invoked by the event dispatcher whenever standard
 *	input becomes readable. It grabs the next line of input characters,
 *	adds them to a command being assembled, and executes the command if
 *	it's complete.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Could be almost arbitrary, depending on the command that's typed.
 *
 *----------------------------------------------------------------------
 */

static void
StdinProc(
    void *clientData,	/* The state of interactive cmd line */
    TCL_UNUSED(int) /*mask*/)
{
    char *cmd;
    int code;
    Tcl_Size length;
    InteractiveState *isPtr = (InteractiveState *)clientData;
    Tcl_Channel chan = isPtr->input;
    Tcl_Interp *interp = isPtr->interp;

    length = Tcl_Gets(chan, &isPtr->line);

    if ((length < 0) && !isPtr->gotPartial) {
	if (isPtr->tty) {
	    /*
	     * Would be better to find a way to exit the mainLoop? Or perhaps
	     * evaluate [exit]? Leaving as is for now due to compatibility
	     * concerns.
	     */

	    Tcl_Exit(0);
	}
	Tcl_DeleteChannelHandler(chan, StdinProc, isPtr);
	return;
    }

    Tcl_DStringAppend(&isPtr->command, Tcl_DStringValue(&isPtr->line), TCL_INDEX_NONE);
    cmd = Tcl_DStringAppend(&isPtr->command, "\n", TCL_INDEX_NONE);
    Tcl_DStringFree(&isPtr->line);
    if (!Tcl_CommandComplete(cmd)) {
	isPtr->gotPartial = 1;
	goto prompt;
    }
    isPtr->gotPartial = 0;

    /*
     * Disable the stdin channel handler while evaluating the command;
     * otherwise if the command re-enters the event loop we might process
     * commands from stdin before the current command is finished. Among other
     * things, this will trash the text of the command being evaluated.
     */

    Tcl_CreateChannelHandler(chan, 0, StdinProc, isPtr);
    code = Tcl_RecordAndEval(interp, cmd, TCL_EVAL_GLOBAL);
    isPtr->input = chan = Tcl_GetStdChannel(TCL_STDIN);
    if (chan != NULL) {
	Tcl_CreateChannelHandler(chan, TCL_READABLE, StdinProc, isPtr);
    }
    Tcl_DStringFree(&isPtr->command);
    if (code != TCL_OK) {
	chan = Tcl_GetStdChannel(TCL_STDERR);

	if (chan != NULL) {
	    if (Tcl_WriteObj(chan, Tcl_GetObjResult(interp)) < 0) {
		Tcl_WriteChars(chan, ENCODING_ERROR, -1);
	    }
	    Tcl_WriteChars(chan, "\n", 1);
	}
    } else if (isPtr->tty) {
	Tcl_Obj *resultPtr = Tcl_GetObjResult(interp);
	chan = Tcl_GetStdChannel(TCL_STDOUT);

	Tcl_IncrRefCount(resultPtr);
	(void)Tcl_GetStringFromObj(resultPtr, &length);
	if ((length > 0) && (chan != NULL)) {
	    if (Tcl_WriteObj(chan, resultPtr) < 0) {
		Tcl_WriteChars(chan, "\n\t(encoding error in stdout)", -1);
	    }
	    Tcl_WriteChars(chan, "\n", 1);
	}
	Tcl_DecrRefCount(resultPtr);
    }

    /*
     * If a tty stdin is still around, output a prompt.
     */

  prompt:
    if (isPtr->tty && (isPtr->input != NULL)) {
	Prompt(interp, isPtr);
    }
    Tcl_ResetResult(interp);
}

/*
 *----------------------------------------------------------------------
 *
 * Prompt --
 *
 *	Issue a prompt on standard output, or invoke a script to issue the
 *	prompt.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A prompt gets output, and a Tcl script may be evaluated in interp.
 *
 *----------------------------------------------------------------------
 */

static void
Prompt(
    Tcl_Interp *interp,		/* Interpreter to use for prompting. */
    InteractiveState *isPtr) /* InteractiveState. */
{
    Tcl_Obj *promptCmdPtr;
    int code;
    Tcl_Channel chan;

    promptCmdPtr = Tcl_GetVar2Ex(interp,
	isPtr->gotPartial ? "tcl_prompt2" : "tcl_prompt1", NULL, TCL_GLOBAL_ONLY);
    if (promptCmdPtr == NULL) {
    defaultPrompt:
	if (!isPtr->gotPartial) {
	    chan = Tcl_GetStdChannel(TCL_STDOUT);
	    if (chan != NULL) {
		Tcl_WriteChars(chan, DEFAULT_PRIMARY_PROMPT,
			sizeof(DEFAULT_PRIMARY_PROMPT) - 1);
	    }
	}
    } else {
	code = Tcl_EvalObjEx(interp, promptCmdPtr, TCL_EVAL_GLOBAL);
	if (code != TCL_OK) {
	    Tcl_AddErrorInfo(interp,
		    "\n    (script that generates prompt)");
	    chan = Tcl_GetStdChannel(TCL_STDERR);
	    if (chan != NULL) {
	    if (Tcl_WriteObj(chan, Tcl_GetObjResult(interp)) < 0) {
		Tcl_WriteChars(chan, ENCODING_ERROR, -1);
	    }
		Tcl_WriteChars(chan, "\n", 1);
	    }
	    goto defaultPrompt;
	}
    }

    chan = Tcl_GetStdChannel(TCL_STDOUT);
    if (chan != NULL) {
	Tcl_Flush(chan);
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
