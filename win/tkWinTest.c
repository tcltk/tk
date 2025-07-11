/*
 * tkWinTest.c --
 *
 *	Contains commands for platform specific tests for the Windows
 *	platform.
 *
 * Copyright © 1997 Sun Microsystems, Inc.
 * Copyright © 2000 Scriptics Corporation.
 * Copyright © 2001 ActiveState Corporation.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#undef USE_TCL_STUBS
#define USE_TCL_STUBS
#undef USE_TK_STUBS
#define USE_TK_STUBS
#include "tkWinInt.h"
#undef TCLBOOLWARNING
#define TCLBOOLWARNING(boolPtr) /* needed here because we compile with -Wc++-compat */

HWND tkWinCurrentDialog;

/*
 * Forward declarations of functions defined later in this file:
 */

static Tcl_ObjCmdProc2 TestclipboardObjCmd;
static Tcl_ObjCmdProc2 TestwineventObjCmd;
static Tcl_ObjCmdProc2 TestfindwindowObjCmd;
static Tcl_ObjCmdProc2 TestgetwindowinfoObjCmd;
static Tcl_ObjCmdProc2 TestwinlocaleObjCmd;
static Tk_GetSelProc SetSelectionResult;

/*
 *----------------------------------------------------------------------
 *
 * TkplatformtestInit --
 *
 *	Defines commands that test platform specific functionality for Windows
 *	platforms.
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

    Tcl_CreateObjCommand2(interp, "testclipboard", TestclipboardObjCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testwinevent", TestwineventObjCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testfindwindow", TestfindwindowObjCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testgetwindowinfo", TestgetwindowinfoObjCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testwinlocale", TestwinlocaleObjCmd,
	    Tk_MainWindow(interp), NULL);
    return TCL_OK;
}

struct TestFindControlState {
    int  id;
    HWND control;
};

/* Callback for window enumeration - used for TestFindControl */
BOOL CALLBACK TestFindControlCallback(
    HWND hwnd,
    LPARAM lParam
)
{
    struct TestFindControlState *fcsPtr = (struct TestFindControlState *)lParam;
    fcsPtr->control = GetDlgItem(hwnd, fcsPtr->id);
    /* If we have found the control, return FALSE to stop the enumeration */
    return fcsPtr->control == NULL ? TRUE : FALSE;
}

/*
 * Finds the descendent control window with the specified ID and returns
 * its HWND.
 */
HWND TestFindControl(HWND root, int id)
{
    struct TestFindControlState fcs;

    fcs.control = GetDlgItem(root, id);
    if (fcs.control == NULL) {
	/* Control is not a direct child. Look in descendents */
	fcs.id = id;
	fcs.control = NULL;
	EnumChildWindows(root, TestFindControlCallback, (LPARAM) &fcs);
    }
    return fcs.control;
}


/*
 *----------------------------------------------------------------------
 *
 * AppendSystemError --
 *
 *	This routine formats a Windows system error message and places it into
 *	the interpreter result. Originally from tclWinReg.c.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
AppendSystemError(
    Tcl_Interp *interp,		/* Current interpreter. */
    DWORD error)		/* Result code from error. */
{
    int length;
    WCHAR *wMsgPtr, **wMsgPtrPtr = &wMsgPtr;
    const char *msg;
    char id[TCL_INTEGER_SPACE], msgBuf[24 + TCL_INTEGER_SPACE];
    Tcl_DString ds;
    Tcl_Obj *resultPtr = Tcl_GetObjResult(interp);

    if (Tcl_IsShared(resultPtr)) {
	resultPtr = Tcl_DuplicateObj(resultPtr);
    }
    length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM
	    | FORMAT_MESSAGE_IGNORE_INSERTS
	    | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, error,
	    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (WCHAR *) wMsgPtrPtr,
	    0, NULL);
    if (length == 0) {
	char *msgPtr;

	length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM
		| FORMAT_MESSAGE_IGNORE_INSERTS
		| FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (char *) &msgPtr,
		0, NULL);
	if (length > 0) {
	    wMsgPtr = (WCHAR *) LocalAlloc(LPTR, (length + 1) * sizeof(WCHAR));
	    MultiByteToWideChar(CP_ACP, 0, msgPtr, length + 1, wMsgPtr,
		    length + 1);
	    LocalFree(msgPtr);
	}
    }
    if (length == 0) {
	if (error == ERROR_CALL_NOT_IMPLEMENTED) {
	    strcpy(msgBuf, "function not supported under Win32s");
	} else {
	    snprintf(msgBuf, sizeof(msgBuf), "unknown error: %ld", error);
	}
	msg = msgBuf;
    } else {
	char *msgPtr;

	Tcl_DStringInit(&ds);
	Tcl_WCharToUtfDString(wMsgPtr, wcslen(wMsgPtr), &ds);
	LocalFree(wMsgPtr);

	msgPtr = Tcl_DStringValue(&ds);
	length = Tcl_DStringLength(&ds);

	/*
	 * Trim the trailing CR/LF from the system message.
	 */

	if (msgPtr[length-1] == '\n') {
	    --length;
	}
	if (msgPtr[length-1] == '\r') {
	    --length;
	}
	msgPtr[length] = 0;
	msg = msgPtr;
    }

    snprintf(id, sizeof(id), "%ld", error);
    Tcl_SetErrorCode(interp, "WINDOWS", id, msg, (char *)NULL);
    Tcl_AppendToObj(resultPtr, msg, length);
    Tcl_SetObjResult(interp, resultPtr);

    if (length != 0) {
	Tcl_DStringFree(&ds);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TestclipboardObjCmd --
 *
 *	This function implements the testclipboard command. It provides a way
 *	to determine the actual contents of the Windows clipboard.
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
SetSelectionResult(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    const char *selection)
{
    Tcl_AppendResult(interp, selection, NULL);
    return TCL_OK;
}

static int
TestclipboardObjCmd(
    void *clientData,	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument values. */
{
    Tk_Window tkwin = (Tk_Window)clientData;

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, NULL);
	return TCL_ERROR;
    }
    return TkSelGetSelection(interp, tkwin, Tk_InternAtom(tkwin, "CLIPBOARD"),
	    XA_STRING, SetSelectionResult, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * TestwineventObjCmd --
 *
 *	This function implements the testwinevent command. It provides a way
 *	to send messages to windows dialogs.
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
TestwineventObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])		/* Argument strings. */
{
    HWND hwnd = 0;
    HWND child = 0;
    HWND control;
    int id;
    char *rest;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    LRESULT result;
    static const TkStateMap messageMap[] = {
	{WM_LBUTTONDOWN,	"WM_LBUTTONDOWN"},
	{WM_LBUTTONUP,		"WM_LBUTTONUP"},
	{WM_LBUTTONDBLCLK,		"WM_LBUTTONDBLCLK"},
	{WM_MBUTTONDOWN,	"WM_MBUTTONDOWN"},
	{WM_MBUTTONUP,		"WM_MBUTTONUP"},
	{WM_MBUTTONDBLCLK,		"WM_MBUTTONDBLCLK"},
	{WM_RBUTTONDOWN,	"WM_RBUTTONDOWN"},
	{WM_RBUTTONUP,		"WM_RBUTTONUP"},
	{WM_RBUTTONDBLCLK,		"WM_RBUTTONDBLCLK"},
	{WM_XBUTTONDOWN,	"WM_XBUTTONDOWN"},
	{WM_XBUTTONUP,		"WM_XBUTTONUP"},
	{WM_XBUTTONDBLCLK,		"WM_XBUTTONDBLCLK"},
	{WM_CHAR,		"WM_CHAR"},
	{WM_GETTEXT,		"WM_GETTEXT"},
	{WM_SETTEXT,		"WM_SETTEXT"},
	{WM_COMMAND,            "WM_COMMAND"},
	{-1,			NULL}
    };

    if ((objc == 3) && (strcmp(Tcl_GetString(objv[1]), "debug") == 0)) {
	int b;

	if (Tcl_GetBooleanFromObj(interp, objv[2], &b) != TCL_OK) {
	    return TCL_ERROR;
	}
	TkWinDialogDebug(b);
	return TCL_OK;
    }

    if (objc < 4) {
	return TCL_ERROR;
    }

    hwnd = (HWND)INT2PTR(strtol(Tcl_GetString(objv[1]), &rest, 0));
    if (rest == Tcl_GetString(objv[1])) {
	hwnd = FindWindowA(NULL, Tcl_GetString(objv[1]));
	if (hwnd == NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("no such window", TCL_INDEX_NONE));
	    return TCL_ERROR;
	}
    }
    UpdateWindow(hwnd);

    id = strtol(Tcl_GetString(objv[2]), &rest, 0);
    if (rest == Tcl_GetString(objv[2])) {
	char buf[256];

	child = GetWindow(hwnd, GW_CHILD);
	while (child != NULL) {
	    SendMessageA(child, WM_GETTEXT, (WPARAM) sizeof(buf), (LPARAM) buf);
	    if (strcasecmp(buf, Tcl_GetString(objv[2])) == 0) {
		id = GetDlgCtrlID(child);
		break;
	    }
	    child = GetWindow(child, GW_HWNDNEXT);
	}
	if (child == NULL) {
	    Tcl_AppendResult(interp, "could not find a control matching \"",
		Tcl_GetString(objv[2]), "\"", NULL);
	    return TCL_ERROR;
	}
    }

    message = TkFindStateNum(NULL, NULL, messageMap, Tcl_GetString(objv[3]));
    wParam = 0;
    lParam = 0;

    if (objc > 4) {
	wParam = strtol(Tcl_GetString(objv[4]), NULL, 0);
    }
    if (objc > 5) {
	lParam = strtol(Tcl_GetString(objv[5]), NULL, 0);
    }

    switch (message) {
    case WM_GETTEXT: {
	Tcl_DString ds;
	char buf[256];

#if 0
	GetDlgItemTextA(hwnd, id, buf, 256);
#else
	control = TestFindControl(hwnd, id);
	if (control == NULL) {
	    Tcl_SetObjResult(interp,
			     Tcl_ObjPrintf("Could not find control with id %d", id));
	    return TCL_ERROR;
	}
	buf[0] = 0;
	SendMessageA(control, WM_GETTEXT, (WPARAM)sizeof(buf),
		     (LPARAM) buf);
#endif
	Tcl_AppendResult(interp, Tcl_ExternalToUtfDString(NULL, buf, TCL_INDEX_NONE, &ds), NULL);
	Tcl_DStringFree(&ds);
	break;
    }
    case WM_SETTEXT: {
	Tcl_DString ds;

	control = TestFindControl(hwnd, id);
	if (control == NULL) {
	    Tcl_SetObjResult(interp,
		    Tcl_ObjPrintf("Could not find control with id %d", id));
	    return TCL_ERROR;
	}
	Tcl_UtfToExternalDString(NULL, Tcl_GetString(objv[4]), TCL_INDEX_NONE, &ds);
	result = SendMessageA(control, WM_SETTEXT, 0, (LPARAM)Tcl_DStringValue(&ds));
	Tcl_DStringFree(&ds);
	if (result == 0) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to send text to dialog: ", TCL_INDEX_NONE));
	    AppendSystemError(interp, GetLastError());
	    return TCL_ERROR;
	}
	break;
    }
    case WM_COMMAND: {
	char buf[TCL_INTEGER_SPACE];
	if (objc < 5) {
	    wParam = MAKEWPARAM(id, 0);
	    lParam = (LPARAM)child;
	}
	snprintf(buf, sizeof(buf), "%d", (int) SendMessageA(hwnd, message, wParam, lParam));
	Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, TCL_INDEX_NONE));
	break;
    }
    default: {
	char buf[TCL_INTEGER_SPACE];

	snprintf(buf, sizeof(buf), "%d",
		(int) SendDlgItemMessageA(hwnd, id, message, wParam, lParam));
	Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, TCL_INDEX_NONE));
	break;
    }
    }
    return TCL_OK;
}

/*
 *  testfindwindow title ?class?
 *	Find a Windows window using the FindWindow API call. This takes the window
 *	title and optionally the window class and if found returns the HWND and
 *	raises an error if the window is not found.
 *	eg: testfindwindow Console TkTopLevel
 *	    Can find the console window if it is visible.
 *	eg: testfindwindow "TkTest #10201" "#32770"
 *	    Can find a messagebox window with this title.
 */

static int
TestfindwindowObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument values. */
{
    LPCWSTR title = NULL, windowClass = NULL;
    Tcl_DString titleString, classString;
    HWND hwnd = NULL;
    int r = TCL_OK;
    DWORD myPid;

    Tcl_DStringInit(&classString);
    Tcl_DStringInit(&titleString);

    if (objc < 2 || objc > 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "title ?class?");
	return TCL_ERROR;
    }

    Tcl_DStringInit(&titleString);
    title = Tcl_UtfToWCharDString(Tcl_GetString(objv[1]), TCL_INDEX_NONE, &titleString);
    if (objc == 3) {
	Tcl_DStringInit(&classString);
	windowClass = Tcl_UtfToWCharDString(Tcl_GetString(objv[2]), TCL_INDEX_NONE, &classString);
    }
    if (title[0] == 0)
	title = NULL;
    /* We want find a window the belongs to us and not some other process */
    hwnd = NULL;
    myPid = GetCurrentProcessId();
    while (1) {
	DWORD pid, tid;
	hwnd = FindWindowExW(NULL, hwnd, windowClass, title);
	if (hwnd == NULL)
	    break;
	tid = GetWindowThreadProcessId(hwnd, &pid);
	if (tid == 0) {
	    /* Window has gone */
	    hwnd = NULL;
	    break;
	}
	if (pid == myPid)
	    break;              /* Found it */
    }

    if (hwnd == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to find window: ", TCL_INDEX_NONE));
	AppendSystemError(interp, GetLastError());
	r = TCL_ERROR;
    } else {
	Tcl_SetObjResult(interp, Tcl_NewWideIntObj(PTR2INT(hwnd)));
    }

    Tcl_DStringFree(&titleString);
    Tcl_DStringFree(&classString);
    return r;

}

static BOOL CALLBACK
EnumChildrenProc(
    HWND hwnd,
    LPARAM lParam)
{
    Tcl_Obj *listObj = (Tcl_Obj *) lParam;

    Tcl_ListObjAppendElement(NULL, listObj, Tcl_NewWideIntObj(PTR2INT(hwnd)));
    return TRUE;
}

static int
TestgetwindowinfoObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    Tcl_WideInt hwnd;
    Tcl_Obj *dictObj = NULL, *classObj = NULL, *textObj = NULL;
    Tcl_Obj *childrenObj = NULL;
    WCHAR buf[512];
    int cch, cchBuf = 256;
    Tcl_DString ds;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "hwnd");
	return TCL_ERROR;
    }

    if (Tcl_GetWideIntFromObj(interp, objv[1], &hwnd) != TCL_OK)
	return TCL_ERROR;

    cch = GetClassNameW((HWND)INT2PTR(hwnd), buf, cchBuf);
    if (cch == 0) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to get class name: ", TCL_INDEX_NONE));
	AppendSystemError(interp, GetLastError());
	return TCL_ERROR;
    } else {
	Tcl_DStringInit(&ds);
	Tcl_WCharToUtfDString(buf, wcslen(buf), &ds);
	classObj = Tcl_NewStringObj(Tcl_DStringValue(&ds), Tcl_DStringLength(&ds));
	Tcl_DStringFree(&ds);
    }

    dictObj = Tcl_NewDictObj();
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("class", 5), classObj);
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("id", 2),
	    Tcl_NewWideIntObj(GetWindowLongPtr((HWND)(size_t)hwnd, GWL_ID)));

    cch = GetWindowTextW((HWND)INT2PTR(hwnd), buf, cchBuf);
	Tcl_DStringInit(&ds);
    Tcl_WCharToUtfDString(buf, cch, &ds);
    textObj = Tcl_NewStringObj(Tcl_DStringValue(&ds), Tcl_DStringLength(&ds));
    Tcl_DStringFree(&ds);

    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("text", 4), textObj);
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("parent", 6),
	    Tcl_NewWideIntObj(PTR2INT(GetParent((HWND)(size_t)hwnd))));

    childrenObj = Tcl_NewListObj(0, NULL);
    EnumChildWindows((HWND)(size_t)hwnd, EnumChildrenProc, (LPARAM)childrenObj);
    Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj("children", TCL_INDEX_NONE), childrenObj);

    Tcl_SetObjResult(interp, dictObj);
    return TCL_OK;
}

static int
TestwinlocaleObjCmd(
    TCL_UNUSED(void *),	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument values. */
{
    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, NULL);
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(GetThreadLocale()));
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
