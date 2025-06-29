/*
 * tkWinDialog.c --
 *
 *	Contains the Windows implementation of the common dialog boxes.
 *
 * Copyright © 1996-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkWinInt.h"
#include "tkFileFilter.h"
#include "tkFont.h"

#include <commdlg.h>		/* includes common dialog functionality */
#include <dlgs.h>		/* includes common dialog template defines */
#include <cderr.h>		/* includes the common dialog error codes */

#include <shlobj.h>		/* includes SHBrowseForFolder */

#ifdef _MSC_VER
#   pragma comment (lib, "shell32.lib")
#   pragma comment (lib, "comdlg32.lib")
#   pragma comment (lib, "uuid.lib")
#endif

/* These needed for compilation with VC++ 5.2 */
/* XXX - remove these since need at least VC 6 */
#ifndef BIF_EDITBOX
#define BIF_EDITBOX 0x10
#endif

#ifndef BIF_VALIDATE
#define BIF_VALIDATE 0x0020
#endif

/* This "new" dialog style is now actually the "old" dialog style post-Vista */
#ifndef BIF_NEWDIALOGSTYLE
#define BIF_NEWDIALOGSTYLE 0x0040
#endif

#ifndef BFFM_VALIDATEFAILEDW
#define BFFM_VALIDATEFAILEDW 4
#endif /* BFFM_VALIDATEFAILEDW */

typedef struct {
    int debugFlag;		/* Flags whether we should output debugging
				 * information while displaying a builtin
				 * dialog. */
    Tcl_Interp *debugInterp;	/* Interpreter to used for debugging. */
    UINT WM_LBSELCHANGED;	/* Holds a registered windows event used for
				 * communicating between the Directory Chooser
				 * dialog and its hook proc. */
    HHOOK hMsgBoxHook;		/* Hook proc for tk_messageBox and the */
    HICON hSmallIcon;		/* icons used by a parent to be used in */
    HICON hBigIcon;		/* the message box */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * The following structures are used by Tk_MessageBoxCmd() to parse arguments
 * and return results.
 */

static const TkStateMap iconMap[] = {
    {MB_ICONERROR,		"error"},
    {MB_ICONINFORMATION,	"info"},
    {MB_ICONQUESTION,		"question"},
    {MB_ICONWARNING,		"warning"},
    {-1,			NULL}
};

static const TkStateMap typeMap[] = {
    {MB_ABORTRETRYIGNORE,	"abortretryignore"},
    {MB_OK,			"ok"},
    {MB_OKCANCEL,		"okcancel"},
    {MB_RETRYCANCEL,		"retrycancel"},
    {MB_YESNO,			"yesno"},
    {MB_YESNOCANCEL,		"yesnocancel"},
    {-1,			NULL}
};

static const TkStateMap buttonMap[] = {
    {IDABORT,			"abort"},
    {IDRETRY,			"retry"},
    {IDIGNORE,			"ignore"},
    {IDOK,			"ok"},
    {IDCANCEL,			"cancel"},
    {IDNO,			"no"},
    {IDYES,			"yes"},
    {-1,			NULL}
};

static const int buttonFlagMap[] = {
    MB_DEFBUTTON1, MB_DEFBUTTON2, MB_DEFBUTTON3, MB_DEFBUTTON4
};

static const struct {int type; int btnIds[3];} allowedTypes[] = {
    {MB_ABORTRETRYIGNORE,	{IDABORT, IDRETRY,  IDIGNORE}},
    {MB_OK,			{IDOK,	  -1,	    -1	    }},
    {MB_OKCANCEL,		{IDOK,	  IDCANCEL, -1	    }},
    {MB_RETRYCANCEL,		{IDRETRY, IDCANCEL, -1	    }},
    {MB_YESNO,			{IDYES,	  IDNO,	    -1	    }},
    {MB_YESNOCANCEL,		{IDYES,	  IDNO,	    IDCANCEL}}
};

#define NUM_TYPES (sizeof(allowedTypes) / sizeof(allowedTypes[0]))

/*
 * Abstract trivial differences between Win32 and Win64.
 */

#define TkWinGetHInstance(from) \
	((HINSTANCE) GetWindowLongPtrW((from), GWLP_HINSTANCE))
#define TkWinGetUserData(from) \
	GetWindowLongPtrW((from), GWLP_USERDATA)
#define TkWinSetUserData(to,what) \
	SetWindowLongPtrW((to), GWLP_USERDATA, (LPARAM)(what))

/*
 * The value of TK_MULTI_MAX_PATH dictates how many files can be retrieved
 * with tk_get*File -multiple 1. It must be allocated on the stack, so make it
 * large enough but not too large. - hobbs
 *
 * The data is stored as <dir>\0<file1>\0<file2>\0...<fileN>\0\0. Since
 * MAX_PATH == 260 on Win2K/NT, *40 is ~10Kbytes.
 */

#define TK_MULTI_MAX_PATH	(MAX_PATH*40)

/*
 * The following structure is used to pass information between the directory
 * chooser function, Tk_ChooseDirectoryObjCmd(), and its dialog hook proc.
 */

typedef struct {
   WCHAR initDir[MAX_PATH];	/* Initial folder to use */
   WCHAR retDir[MAX_PATH];	/* Returned folder to use */
   Tcl_Interp *interp;
   int mustExist;		/* True if file must exist to return from
				 * callback */
} ChooseDir;

/*
 * The following structure is used to pass information between GetFileName
 * function and OFN dialog hook procedures. [Bug 2896501, Patch 2898255]
 */

typedef struct OFNData {
    Tcl_Interp *interp;		/* Interp, used only if debug is turned on,
				 * for setting the variable
				 * "::tk::test::dialog::testDialog". */
    int dynFileBufferSize;	/* Dynamic filename buffer size, stored to
				 * avoid shrinking and expanding the buffer
				 * when selection changes */
    WCHAR *dynFileBuffer;	/* Dynamic filename buffer */
} OFNData;

/*
 * The following structure is used to gather options used by various
 * file dialogs
 */
typedef struct OFNOpts {
    Tk_Window tkwin;            /* Owner window for dialog */
    Tcl_Obj *extObj;            /* Default extension */
    Tcl_Obj *titleObj;          /* Title for dialog */
    Tcl_Obj *filterObj;         /* File type filter list */
    Tcl_Obj *typeVariableObj;   /* Variable in which to store type selected */
    Tcl_Obj *initialTypeObj;    /* Initial value of above, or NULL */
    Tcl_DString utfDirString;   /* Initial dir */
    int multi;                  /* Multiple selection enabled */
    int confirmOverwrite;       /* Confirm before overwriting */
    int mustExist;              /* Used only for  */
    WCHAR file[TK_MULTI_MAX_PATH]; /* File name
				      XXX - fixed size because it was so
				      historically. Why not malloc'ed ?
				   */
} OFNOpts;

/* Define the operation for which option parsing is to be done. */
enum OFNOper {
    OFN_FILE_SAVE,              /* tk_getOpenFile */
    OFN_FILE_OPEN,              /* tk_getSaveFile */
    OFN_DIR_CHOOSE              /* tk_chooseDirectory */
};

/*
 * Definitions of functions used only in this file.
 */

static UINT CALLBACK	ColorDlgHookProc(HWND hDlg, UINT uMsg, WPARAM wParam,
			    LPARAM lParam);
static void             CleanupOFNOptions(OFNOpts *optsPtr);
static int              ParseOFNOptions(void *clientData,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[], enum OFNOper oper, OFNOpts *optsPtr);
static int GetFileNameVista(Tcl_Interp *interp, OFNOpts *optsPtr,
			    enum OFNOper oper);
static int		GetFileName(void *clientData,
				    Tcl_Interp *interp, int objc,
				    Tcl_Obj *const objv[], enum OFNOper oper);
static int MakeFilterVista(Tcl_Interp *interp, OFNOpts *optsPtr,
	       DWORD *countPtr, COMDLG_FILTERSPEC **dlgFilterPtrPtr,
	       DWORD *defaultFilterIndexPtr);
static void FreeFilterVista(DWORD count, COMDLG_FILTERSPEC *dlgFilterPtr);
static LRESULT CALLBACK MsgBoxCBTProc(int nCode, WPARAM wParam, LPARAM lParam);
static void		SetTestDialog(void *clientData);
static const char *ConvertExternalFilename(LPCWSTR, Tcl_DString *);

/*
 *-------------------------------------------------------------------------
 *
 * EatSpuriousMessageBugFix --
 *
 *	In the file open/save dialog, double clicking on a list item causes
 *	the dialog box to close, but an unwanted WM_LBUTTONUP message is sent
 *	to the window underneath. If the window underneath happens to be a
 *	windows control (eg a button) then it will be activated by accident.
 *
 *	This problem does not occur in dialog boxes, because windows must do
 *	some special processing to solve the problem. (separate message
 *	processing functions are used to cope with keyboard navigation of
 *	controls.)
 *
 *	Here is one solution. After returning, we flush all mouse events
 *      for 1/4 second. In 8.6.5 and earlier, the code used to
 *      poll the message queue consuming WM_LBUTTONUP messages.
 *	On seeing a WM_LBUTTONDOWN message, it would exit early, since the user
 *	must be doing something new. However this early exit does not work
 *      on Vista and later because the Windows sends both BUTTONDOWN and
 *      BUTTONUP after the DBLCLICK instead of just BUTTONUP as on XP.
 *      Rather than try and figure out version specific sequences, we
 *      ignore all mouse events in that interval.
 *
 *      This fix only works for the current application, so the problem will
 *	still occur if the open dialog happens to be over another applications
 *	button. However this is a fairly rare occurrance.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Consumes unwanted mouse related messages.
 *
 *-------------------------------------------------------------------------
 */

static void
EatSpuriousMessageBugFix(void)
{
    MSG msg;
    DWORD nTime = GetTickCount() + 250;

    while (GetTickCount() < nTime) {
	PeekMessageW(&msg, 0, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE);
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * TkWinDialogDebug --
 *
 *	Function to turn on/off debugging support for common dialogs under
 *	windows. The variable "::tk::test::dialog::testDialog" is set to the
 *	identifier of the dialog window when the modal dialog window pops up
 *	and it is safe to send messages to the dialog.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	This variable only makes sense if just one dialog is up at a time.
 *
 *-------------------------------------------------------------------------
 */

void
TkWinDialogDebug(
    int debug)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    tsdPtr->debugFlag = debug;
}

/*
 *-------------------------------------------------------------------------
 *
 * Tk_ChooseColorObjCmd --
 *
 *	This function implements the color dialog box for the Windows
 *	platform. See the user documentation for details on what it does.
 *
 * Results:
 *	See user documentation.
 *
 * Side effects:
 *	A dialog window is created the first time this function is called.
 *	This window is not destroyed and will be reused the next time the
 *	application invokes the "tk_chooseColor" command.
 *
 *-------------------------------------------------------------------------
 */

int
Tk_ChooseColorObjCmd(
    void *clientData,	/* Main window associated with interpreter. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Tk_Window tkwin = (Tk_Window)clientData, parent;
    HWND hWnd;
    int i, oldMode, winCode, result;
    CHOOSECOLORW chooseColor;
    static int inited = 0;
    static COLORREF dwCustColors[16];
    static long oldColor;		/* the color selected last time */
    static const char *const optionStrings[] = {
	"-initialcolor", "-parent", "-title", NULL
    };
    enum options {
	COLOR_INITIAL, COLOR_PARENT, COLOR_TITLE
    };

    result = TCL_OK;
    if (inited == 0) {
	/*
	 * dwCustColors stores the custom color which the user can modify. We
	 * store these colors in a static array so that the next time the
	 * color dialog pops up, the same set of custom colors remain in the
	 * dialog.
	 */

	for (i = 0; i < 16; i++) {
	    dwCustColors[i] = RGB(255-i * 10, i, i * 10);
	}
	oldColor = RGB(0xa0, 0xa0, 0xa0);
	inited = 1;
    }

    parent			= tkwin;
    chooseColor.lStructSize	= sizeof(CHOOSECOLORW);
    chooseColor.hwndOwner	= NULL;
    chooseColor.hInstance	= NULL;
    chooseColor.rgbResult	= oldColor;
    chooseColor.lpCustColors	= dwCustColors;
    chooseColor.Flags		= CC_RGBINIT | CC_FULLOPEN | CC_ENABLEHOOK;
    chooseColor.lCustData	= (LPARAM) NULL;
    chooseColor.lpfnHook	= (LPOFNHOOKPROC)(void *)ColorDlgHookProc;
    chooseColor.lpTemplateName	= (LPWSTR) interp;

    for (i = 1; i < objc; i += 2) {
	int index;
	const char *string;
	Tcl_Obj *optionPtr, *valuePtr;

	optionPtr = objv[i];
	valuePtr = objv[i + 1];

	if (Tcl_GetIndexFromObj(interp, optionPtr, optionStrings,
		"option", TCL_EXACT, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (i + 1 == objc) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "value for \"%s\" missing", Tcl_GetString(optionPtr)));
	    Tcl_SetErrorCode(interp, "TK", "COLORDIALOG", "VALUE", (char *)NULL);
	    return TCL_ERROR;
	}

	string = Tcl_GetString(valuePtr);
	switch ((enum options) index) {
	case COLOR_INITIAL: {
	    XColor *colorPtr;

	    colorPtr = Tk_AllocColorFromObj(interp, tkwin, valuePtr);
	    if (colorPtr == NULL) {
		return TCL_ERROR;
	    }
	    chooseColor.rgbResult = RGB(colorPtr->red / 0x100,
		    colorPtr->green / 0x100, colorPtr->blue / 0x100);
	    break;
	}
	case COLOR_PARENT:
	    parent = Tk_NameToWindow(interp, string, tkwin);
	    if (parent == NULL) {
		return TCL_ERROR;
	    }
	    break;
	case COLOR_TITLE:
	    chooseColor.lCustData = (LPARAM) string;
	    break;
	}
    }

    Tk_MakeWindowExist(parent);
    chooseColor.hwndOwner = NULL;
    hWnd = Tk_GetHWND(Tk_WindowId(parent));
    chooseColor.hwndOwner = hWnd;

    oldMode = Tcl_SetServiceMode(TCL_SERVICE_ALL);
    winCode = ChooseColorW(&chooseColor);
    (void) Tcl_SetServiceMode(oldMode);

    /*
     * Ensure that hWnd is enabled, because it can happen that we have updated
     * the wrapper of the parent, which causes us to leave this child disabled
     * (Windows loses sync).
     */

    EnableWindow(hWnd, 1);

    /*
     * Clear the interp result since anything may have happened during the
     * modal loop.
     */

    Tcl_ResetResult(interp);

    /*
     * 3. Process the result of the dialog
     */

    if (winCode) {
	/*
	 * User has selected a color
	 */

	Tcl_SetObjResult(interp, Tcl_ObjPrintf("#%02x%02x%02x",
		GetRValue(chooseColor.rgbResult),
		GetGValue(chooseColor.rgbResult),
		GetBValue(chooseColor.rgbResult)));
	oldColor = chooseColor.rgbResult;
	result = TCL_OK;
    }

    return result;
}

/*
 *-------------------------------------------------------------------------
 *
 * ColorDlgHookProc --
 *
 *	Provides special handling of messages for the Color common dialog box.
 *	Used to set the title when the dialog first appears.
 *
 * Results:
 *	The return value is 0 if the default dialog box function should handle
 *	the message, non-zero otherwise.
 *
 * Side effects:
 *	Changes the title of the dialog window.
 *
 *----------------------------------------------------------------------
 */

static UINT CALLBACK
ColorDlgHookProc(
    HWND hDlg,			/* Handle to the color dialog. */
    UINT uMsg,			/* Type of message. */
    TCL_UNUSED(WPARAM),	/* First message parameter. */
    LPARAM lParam)		/* Second message parameter. */
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    const char *title;
    CHOOSECOLORW *ccPtr;

    if (WM_INITDIALOG == uMsg) {

	/*
	 * Set the title string of the dialog.
	 */

	ccPtr = (CHOOSECOLORW *) lParam;
	title = (const char *) ccPtr->lCustData;

	if ((title != NULL) && (title[0] != '\0')) {
	    Tcl_DString ds;

	    Tcl_DStringInit(&ds);
	    SetWindowTextW(hDlg, Tcl_UtfToWCharDString(title, TCL_INDEX_NONE, &ds));
	    Tcl_DStringFree(&ds);
	}
	if (tsdPtr->debugFlag) {
	    tsdPtr->debugInterp = (Tcl_Interp *) ccPtr->lpTemplateName;
	    Tcl_DoWhenIdle(SetTestDialog, hDlg);
	}
	return TRUE;
    }
    return FALSE;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetOpenFileObjCmd --
 *
 *	This function implements the "open file" dialog box for the Windows
 *	platform. See the user documentation for details on what it does.
 *
 * Results:
 *	See user documentation.
 *
 * Side effects:
 *	A dialog window is created the first this function is called.
 *
 *----------------------------------------------------------------------
 */

int
Tk_GetOpenFileObjCmd(
    void *clientData,	/* Main window associated with interpreter. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    return GetFileName(clientData, interp, objc, objv, OFN_FILE_OPEN);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetSaveFileObjCmd --
 *
 *	Same as Tk_GetOpenFileObjCmd but opens a "save file" dialog box
 *	instead
 *
 * Results:
 *	Same as Tk_GetOpenFileObjCmd.
 *
 * Side effects:
 *	Same as Tk_GetOpenFileObjCmd.
 *
 *----------------------------------------------------------------------
 */

int
Tk_GetSaveFileObjCmd(
    void *clientData,	/* Main window associated with interpreter. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    return GetFileName(clientData, interp, objc, objv, OFN_FILE_SAVE);
}

/*
 *----------------------------------------------------------------------
 *
 * CleanupOFNOptions --
 *
 *	Cleans up any storage allocated by ParseOFNOptions
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Releases resources held by *optsPtr
 *----------------------------------------------------------------------
 */
static void CleanupOFNOptions(OFNOpts *optsPtr)
{
    Tcl_DStringFree(&optsPtr->utfDirString);
}



/*
 *----------------------------------------------------------------------
 *
 * ParseOFNOptions --
 *
 *	Option parsing for tk_get{Open,Save}File
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR otherwise
 *
 * Side effects:
 *	Returns option values in *optsPtr. Note these may include string
 *      pointers into objv[]
 *----------------------------------------------------------------------
 */

static int
ParseOFNOptions(
    void *clientData,	/* Main window associated with interpreter. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[],	/* Argument objects. */
    enum OFNOper oper,			/* 1 for Open, 0 for Save */
    OFNOpts *optsPtr)           /* Output, uninitialized on entry */
{
    int i;
    Tcl_DString ds;
    enum options {
	FILE_DEFAULT, FILE_TYPES, FILE_INITDIR, FILE_INITFILE, FILE_PARENT,
	FILE_TITLE, FILE_TYPEVARIABLE, FILE_MULTIPLE, FILE_CONFIRMOW,
	FILE_MUSTEXIST,
    };
    struct Options {
	const char *name;
	enum options value;
    };
    static const struct Options saveOptions[] = {
	{"-confirmoverwrite",	FILE_CONFIRMOW},
	{"-defaultextension",	FILE_DEFAULT},
	{"-filetypes",		FILE_TYPES},
	{"-initialdir",		FILE_INITDIR},
	{"-initialfile",	FILE_INITFILE},
	{"-parent",		FILE_PARENT},
	{"-title",		FILE_TITLE},
	{"-typevariable",	FILE_TYPEVARIABLE},
	{NULL,			FILE_DEFAULT/*ignored*/ }
    };
    static const struct Options openOptions[] = {
	{"-defaultextension",	FILE_DEFAULT},
	{"-filetypes",		FILE_TYPES},
	{"-initialdir",		FILE_INITDIR},
	{"-initialfile",	FILE_INITFILE},
	{"-multiple",		FILE_MULTIPLE},
	{"-parent",		FILE_PARENT},
	{"-title",		FILE_TITLE},
	{"-typevariable",	FILE_TYPEVARIABLE},
	{NULL,			FILE_DEFAULT/*ignored*/ }
    };
    static const struct Options dirOptions[] = {
	{"-initialdir", FILE_INITDIR},
	{"-mustexist",  FILE_MUSTEXIST},
	{"-parent",	FILE_PARENT},
	{"-title",	FILE_TITLE},
	{NULL,		FILE_DEFAULT/*ignored*/ }
    };

    const struct Options *options = NULL;

    switch (oper) {
    case OFN_FILE_SAVE: options = saveOptions; break;
    case OFN_DIR_CHOOSE: options = dirOptions; break;
    case OFN_FILE_OPEN: options = openOptions; break;
    }

    memset(optsPtr, 0, sizeof(*optsPtr));
    optsPtr->tkwin = (Tk_Window)clientData;
    optsPtr->confirmOverwrite = 1; /* By default we ask for confirmation */
    Tcl_DStringInit(&optsPtr->utfDirString);
    optsPtr->file[0] = 0;

    for (i = 1; i < objc; i += 2) {
	int index;
	const char *string;
	Tcl_Obj *valuePtr;

	if (Tcl_GetIndexFromObjStruct(interp, objv[i], options,
		sizeof(struct Options), "option", 0, &index) != TCL_OK) {
	    goto error_return;
	} else if (i + 1 == objc) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
				 "value for \"%s\" missing", options[index].name));
	    Tcl_SetErrorCode(interp, "TK", "FILEDIALOG", "VALUE", (char *)NULL);
	    goto error_return;
	}

	valuePtr = objv[i + 1];
	string = Tcl_GetString(valuePtr);
	switch (options[index].value) {
	case FILE_DEFAULT:
	    optsPtr->extObj = valuePtr;
	    break;
	case FILE_TYPES:
	    optsPtr->filterObj = valuePtr;
	    break;
	case FILE_INITDIR:
	    Tcl_DStringFree(&optsPtr->utfDirString);
	    if (Tcl_TranslateFileName(interp, string,
				      &optsPtr->utfDirString) == NULL)
		goto error_return;
	    break;
	case FILE_INITFILE:
	    if (Tcl_TranslateFileName(interp, string, &ds) == NULL)
		goto error_return;
	    Tcl_UtfToExternal(NULL, TkWinGetUnicodeEncoding(),
			      Tcl_DStringValue(&ds), Tcl_DStringLength(&ds),
			      TCL_ENCODING_PROFILE_TCL8, NULL, (char *)&optsPtr->file[0],
			      sizeof(optsPtr->file), NULL, NULL, NULL);
	    Tcl_DStringFree(&ds);
	    break;
	case FILE_PARENT:
	    optsPtr->tkwin = Tk_NameToWindow(interp, string, (Tk_Window)clientData);
	    if (optsPtr->tkwin == NULL)
		goto error_return;
	    break;
	case FILE_TITLE:
	    optsPtr->titleObj = valuePtr;
	    break;
	case FILE_TYPEVARIABLE:
	    optsPtr->typeVariableObj = valuePtr;
	    optsPtr->initialTypeObj = Tcl_ObjGetVar2(interp, valuePtr,
						     NULL, TCL_GLOBAL_ONLY);
	    break;
	case FILE_MULTIPLE:
	    if (Tcl_GetBooleanFromObj(interp, valuePtr,
				      &optsPtr->multi) != TCL_OK)
		goto error_return;
	    break;
	case FILE_CONFIRMOW:
	    if (Tcl_GetBooleanFromObj(interp, valuePtr,
				      &optsPtr->confirmOverwrite) != TCL_OK)
		goto error_return;
	    break;
	case FILE_MUSTEXIST:
	    if (Tcl_GetBooleanFromObj(interp, valuePtr,
				      &optsPtr->mustExist) != TCL_OK)
		goto error_return;
	    break;
	}
    }

    return TCL_OK;

error_return:                   /* interp should already hold error */
    /* On error, we need to clean up anything we might have allocated */
    CleanupOFNOptions(optsPtr);
    return TCL_ERROR;

}


/*
 *----------------------------------------------------------------------
 *
 * GetFileNameVista --
 *
 *	Displays the new file dialogs on Vista and later.
 *
 * Results:
 *	TCL_OK - dialog was successfully displayed, results returned in interp
 *      TCL_ERROR - error return
 *
 * Side effects:
 *      Dialogs is displayed
 *----------------------------------------------------------------------
 */
static int GetFileNameVista(Tcl_Interp *interp, OFNOpts *optsPtr,
			    enum OFNOper oper)
{
    HRESULT hr;
    HWND hWnd;
    DWORD flags, nfilters, defaultFilterIndex;
    COMDLG_FILTERSPEC *filterPtr = NULL;
    IFileDialog *fdlgIf = NULL;
    IShellItem *dirIf = NULL;
    LPWSTR wstr;
    Tcl_Obj *resultObj = NULL;
    int oldMode;

    /*
     * At this point new interfaces are supposed to be available.
     * fdlgIf is actually a IFileOpenDialog or IFileSaveDialog
     * both of which inherit from IFileDialog. We use the common
     * IFileDialog interface for the most part, casting only for
     * type-specific calls.
     */
    Tk_MakeWindowExist(optsPtr->tkwin);
    hWnd = Tk_GetHWND(Tk_WindowId(optsPtr->tkwin));

    /*
     * The only validation we need to do w.r.t caller supplied data
     * is the filter specification so do that before creating
     */
    if (MakeFilterVista(interp, optsPtr, &nfilters, &filterPtr,
			&defaultFilterIndex) != TCL_OK)
	return TCL_ERROR;

    /*
     * Beyond this point, do not just return on error as there will be
     * resources that need to be released/freed.
     */

    if (oper == OFN_FILE_OPEN || oper == OFN_DIR_CHOOSE)
	hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL,
			      CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void **) &fdlgIf);
    else
	hr = CoCreateInstance(&CLSID_FileSaveDialog, NULL,
			      CLSCTX_INPROC_SERVER, &IID_IFileSaveDialog, (void **) &fdlgIf);

    if (FAILED(hr))
	goto vamoose;

    /*
     * Get current settings first because we want to preserve existing
     * settings like whether to show hidden files etc. based on the
     * user's existing preference
     */
    hr = fdlgIf->lpVtbl->GetOptions(fdlgIf, &flags);
    if (FAILED(hr))
	goto vamoose;

    if (filterPtr) {
	/*
	 * Causes -filetypes {{All *}} -defaultextension ext to return
	 * foo.ext.ext when foo is typed into the entry box
	 *     flags |= FOS_STRICTFILETYPES;
	 */
	hr = fdlgIf->lpVtbl->SetFileTypes(fdlgIf, nfilters, filterPtr);
	if (FAILED(hr))
	    goto vamoose;
	hr = fdlgIf->lpVtbl->SetFileTypeIndex(fdlgIf, defaultFilterIndex);
	if (FAILED(hr))
	    goto vamoose;
    }

    /* Flags are equivalent to those we used in the older API */

    /*
     * Following flags must be set irrespective of original setting
     * XXX - should FOS_NOVALIDATE be there ? Note FOS_NOVALIDATE has different
     * semantics than OFN_NOVALIDATE in the old API.
     */
    flags |=
	FOS_FORCEFILESYSTEM | /* Only want files, not other shell items */
	FOS_NOVALIDATE |           /* Don't check for access denied etc. */
	FOS_PATHMUSTEXIST;           /* The *directory* path must exist */


    if (oper == OFN_DIR_CHOOSE) {
	flags |= FOS_PICKFOLDERS;
	if (optsPtr->mustExist)
	    flags |= FOS_FILEMUSTEXIST; /* XXX - check working */
    } else
	flags &= ~ FOS_PICKFOLDERS;

    if (optsPtr->multi)
	flags |= FOS_ALLOWMULTISELECT;
    else
	flags &= ~FOS_ALLOWMULTISELECT;

    if (optsPtr->confirmOverwrite)
	flags |= FOS_OVERWRITEPROMPT;
    else
	flags &= ~FOS_OVERWRITEPROMPT;

    hr = fdlgIf->lpVtbl->SetOptions(fdlgIf, flags);
    if (FAILED(hr))
	goto vamoose;

    if (optsPtr->extObj != NULL) {
	Tcl_DString ds;
	const char *src;

	src = Tcl_GetString(optsPtr->extObj);
	Tcl_DStringInit(&ds);
	wstr = Tcl_UtfToWCharDString(src, optsPtr->extObj->length, &ds);
	if (wstr[0] == '.')
	    ++wstr;
	hr = fdlgIf->lpVtbl->SetDefaultExtension(fdlgIf, wstr);
	Tcl_DStringFree(&ds);
	if (FAILED(hr))
	    goto vamoose;
    }

    if (optsPtr->titleObj != NULL) {
	Tcl_DString ds;
	const char *src;

	src = Tcl_GetString(optsPtr->titleObj);
	Tcl_DStringInit(&ds);
	wstr = Tcl_UtfToWCharDString(src, optsPtr->titleObj->length, &ds);
	hr = fdlgIf->lpVtbl->SetTitle(fdlgIf, wstr);
	Tcl_DStringFree(&ds);
	if (FAILED(hr))
	    goto vamoose;
    }

    if (optsPtr->file[0]) {
	hr = fdlgIf->lpVtbl->SetFileName(fdlgIf, optsPtr->file);
	if (FAILED(hr))
	    goto vamoose;
    }

    if (Tcl_DStringValue(&optsPtr->utfDirString)[0] != '\0') {
	Tcl_Obj *normPath, *iniDirPath;
	iniDirPath = Tcl_NewStringObj(Tcl_DStringValue(&optsPtr->utfDirString), TCL_INDEX_NONE);
	Tcl_IncrRefCount(iniDirPath);
	normPath = Tcl_FSGetNormalizedPath(interp, iniDirPath);
	/* XXX - Note on failures do not raise error, simply ignore ini dir */
	if (normPath) {
	    LPCWSTR nativePath;
	    Tcl_IncrRefCount(normPath);
	    nativePath = (LPCWSTR)Tcl_FSGetNativePath(normPath); /* Points INTO normPath*/
	    if (nativePath) {
		hr = SHCreateItemFromParsingName(
		    nativePath, NULL,
		    &IID_IShellItem, (void **) &dirIf);
		if (SUCCEEDED(hr)) {
		    /* Note we use SetFolder, not SetDefaultFolder - see MSDN */
		    fdlgIf->lpVtbl->SetFolder(fdlgIf, dirIf); /* Ignore errors */
		}
	    }
	    Tcl_DecrRefCount(normPath); /* ALSO INVALIDATES nativePath !! */
	}
	Tcl_DecrRefCount(iniDirPath);
    }

    oldMode = Tcl_SetServiceMode(TCL_SERVICE_ALL);
    hr = fdlgIf->lpVtbl->Show(fdlgIf, hWnd);
    Tcl_SetServiceMode(oldMode);
    EatSpuriousMessageBugFix();

    /*
     * Ensure that hWnd is enabled, because it can happen that we have updated
     * the wrapper of the parent, which causes us to leave this child disabled
     * (Windows loses sync).
     */

    if (hWnd)
	EnableWindow(hWnd, 1);

    /*
     * Clear interp result since it might have been set during the modal loop.
     * https://core.tcl-lang.org/tk/tktview/4a0451f5291b3c9168cc560747dae9264e1d2ef6
     */
    Tcl_ResetResult(interp);

    if (SUCCEEDED(hr)) {
	if ((oper == OFN_FILE_OPEN) && optsPtr->multi) {
	    IShellItemArray *multiIf;
	    DWORD dw, count;
	    IFileOpenDialog *fodIf = (IFileOpenDialog *) fdlgIf;
	    hr = fodIf->lpVtbl->GetResults(fodIf, &multiIf);
	    if (SUCCEEDED(hr)) {
		Tcl_Obj *multiObj;
		hr = multiIf->lpVtbl->GetCount(multiIf, &count);
		multiObj = Tcl_NewListObj(count, NULL);
		if (SUCCEEDED(hr)) {
		    IShellItem *itemIf;
		    for (dw = 0; dw < count; ++dw) {
			hr = multiIf->lpVtbl->GetItemAt(multiIf, dw, &itemIf);
			if (FAILED(hr))
			    break;
			hr = itemIf->lpVtbl->GetDisplayName(itemIf,
					SIGDN_FILESYSPATH, &wstr);
			if (SUCCEEDED(hr)) {
			    Tcl_DString fnds;

			    ConvertExternalFilename(wstr, &fnds);
			    CoTaskMemFree(wstr);
			    Tcl_ListObjAppendElement(
				interp, multiObj,
				Tcl_NewStringObj(Tcl_DStringValue(&fnds),
						 Tcl_DStringLength(&fnds)));
			    Tcl_DStringFree(&fnds);
			}
			itemIf->lpVtbl->Release(itemIf);
			if (FAILED(hr))
			    break;
		    }
		}
		multiIf->lpVtbl->Release(multiIf);
		if (SUCCEEDED(hr))
		    resultObj = multiObj;
		else
		    Tcl_DecrRefCount(multiObj);
	    }
	} else {
	    IShellItem *resultIf;
	    hr = fdlgIf->lpVtbl->GetResult(fdlgIf, &resultIf);
	    if (SUCCEEDED(hr)) {
		hr = resultIf->lpVtbl->GetDisplayName(resultIf, SIGDN_FILESYSPATH,
						      &wstr);
		if (SUCCEEDED(hr)) {
		    Tcl_DString fnds;

		    ConvertExternalFilename(wstr, &fnds);
		    resultObj = Tcl_NewStringObj(Tcl_DStringValue(&fnds),
						 Tcl_DStringLength(&fnds));
		    CoTaskMemFree(wstr);
		    Tcl_DStringFree(&fnds);
		}
		resultIf->lpVtbl->Release(resultIf);
	    }
	}
	if (SUCCEEDED(hr)) {
	    if (filterPtr && optsPtr->typeVariableObj) {
		UINT ftix;

		hr = fdlgIf->lpVtbl->GetFileTypeIndex(fdlgIf, &ftix);
		if (SUCCEEDED(hr)) {
		    /* Note ftix is a 1-based index */
		    if (ftix > 0 && ftix <= nfilters) {
			Tcl_DString ftds;
			Tcl_Obj *ftobj;

			Tcl_DStringInit(&ftds);
			Tcl_WCharToUtfDString(filterPtr[ftix-1].pszName, wcslen(filterPtr[ftix-1].pszName), &ftds);
			ftobj = Tcl_NewStringObj(Tcl_DStringValue(&ftds),
				Tcl_DStringLength(&ftds));
			Tcl_ObjSetVar2(interp, optsPtr->typeVariableObj, NULL,
				ftobj, TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
			Tcl_DStringFree(&ftds);
		    }
		}
	    }
	}
    } else {
	if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
	    hr = 0;             /* User cancelled, return empty string */
    }

vamoose: /* (hr != 0) => error */
    if (dirIf)
	dirIf->lpVtbl->Release(dirIf);
    if (fdlgIf)
	fdlgIf->lpVtbl->Release(fdlgIf);

    if (filterPtr)
	FreeFilterVista(nfilters, filterPtr);

    if (hr == 0) {
	if (resultObj)          /* May be NULL if user cancelled */
	    Tcl_SetObjResult(interp, resultObj);
	return TCL_OK;
    } else {
	if (resultObj)
	    Tcl_DecrRefCount(resultObj);
	Tcl_SetObjResult(interp, TkWin32ErrorObj(hr));
	return TCL_ERROR;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * GetFileName --
 *
 *	Calls GetOpenFileName() or GetSaveFileName().
 *
 * Results:
 *	See user documentation.
 *
 * Side effects:
 *	See user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
GetFileName(
    void *clientData,	/* Main window associated with interpreter. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[],	/* Argument objects. */
    enum OFNOper oper)	/* 1 to call GetOpenFileName(), 0 to call
				 * GetSaveFileName(). */
{
    OFNOpts ofnOpts;
    int result;

    result = ParseOFNOptions(clientData, interp, objc, objv, oper, &ofnOpts);
    if (result != TCL_OK)
	return result;

    result = GetFileNameVista(interp, &ofnOpts, oper);

    CleanupOFNOptions(&ofnOpts);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeFilterVista
 *
 *      Frees storage previously allocated by MakeFilterVista.
 *      count is the number of elements in dlgFilterPtr[]
 */
static void FreeFilterVista(DWORD count, COMDLG_FILTERSPEC *dlgFilterPtr)
{
    if (dlgFilterPtr != NULL) {
	DWORD dw;
	for (dw = 0; dw < count; ++dw) {
	    if (dlgFilterPtr[dw].pszName != NULL)
		ckfree((void *)dlgFilterPtr[dw].pszName);
	    if (dlgFilterPtr[dw].pszSpec != NULL)
		ckfree((void *)dlgFilterPtr[dw].pszSpec);
	}
	ckfree(dlgFilterPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MakeFilterVista --
 *
 *	Returns file type filters in a format required
 *	by the Vista file dialogs.
 *
 * Results:
 *	A standard TCL return value.
 *
 * Side effects:
 *      Various values are returned through the parameters as
 *      described in the comments below.
 *----------------------------------------------------------------------
 */
static int MakeFilterVista(
    Tcl_Interp *interp,		/* Current interpreter. */
    OFNOpts *optsPtr,           /* Caller specified options */
    DWORD *countPtr,            /* Will hold number of filters */
    COMDLG_FILTERSPEC **dlgFilterPtrPtr, /* Will hold pointer to filter array.
					 Set to NULL if no filters specified.
					 Must be freed by calling
					 FreeFilterVista */
    DWORD *initialIndexPtr)     /* Will hold index of default type */
{
    COMDLG_FILTERSPEC *dlgFilterPtr;
    const char *initial = NULL;
    FileFilterList flist;
    FileFilter *filterPtr;
    DWORD initialIndex = 0;
    Tcl_DString ds, patterns;
    int       i;

    if (optsPtr->filterObj == NULL) {
	*dlgFilterPtrPtr = NULL;
	*countPtr = 0;
	return TCL_OK;
    }

    if (optsPtr->initialTypeObj)
	initial = Tcl_GetString(optsPtr->initialTypeObj);

    TkInitFileFilters(&flist);
    if (TkGetFileFilters(interp, &flist, optsPtr->filterObj, 1) != TCL_OK)
	return TCL_ERROR;

    if (flist.filters == NULL) {
	*dlgFilterPtrPtr = NULL;
	*countPtr = 0;
	return TCL_OK;
    }

    Tcl_DStringInit(&ds);
    Tcl_DStringInit(&patterns);
    dlgFilterPtr = (COMDLG_FILTERSPEC *)ckalloc(flist.numFilters * sizeof(*dlgFilterPtr));

    for (i = 0, filterPtr = flist.filters;
	 filterPtr;
	 filterPtr = filterPtr->next, ++i) {
	const char *sep;
	FileFilterClause *clausePtr;
	size_t nbytes;

	/* Check if this entry should be shown as the default */
	if (initial && strcmp(initial, filterPtr->name) == 0)
	    initialIndex = i+1; /* Windows filter indices are 1-based */

	/* First stash away the text description of the pattern */
	Tcl_DStringInit(&ds);
	Tcl_UtfToWCharDString(filterPtr->name, TCL_INDEX_NONE, &ds);
	nbytes = Tcl_DStringLength(&ds); /* # bytes, not Unicode chars */
	nbytes += sizeof(WCHAR);         /* Terminating \0 */
	dlgFilterPtr[i].pszName = (LPCWSTR)ckalloc(nbytes);
	memmove((void *) dlgFilterPtr[i].pszName, Tcl_DStringValue(&ds), nbytes);
	Tcl_DStringFree(&ds);

	/*
	 * Loop through and join patterns with a ";" Each "clause"
	 * corresponds to a single textual description (called typename)
	 * in the tk_getOpenFile docs. Each such typename may occur
	 * multiple times and all these form a single filter entry
	 * with one clause per occurence. Further each clause may specify
	 * multiple patterns. Hence the nested loop here.
	 */
	sep = "";
	for (clausePtr=filterPtr->clauses ; clausePtr;
	     clausePtr=clausePtr->next) {
	    GlobPattern *globPtr;
	    for (globPtr = clausePtr->patterns; globPtr;
		    globPtr = globPtr->next) {
		Tcl_DStringAppend(&patterns, sep, TCL_INDEX_NONE);
		Tcl_DStringAppend(&patterns, globPtr->pattern, TCL_INDEX_NONE);
		sep = ";";
	    }
	}

	/* Again we need a Unicode form of the string */
	Tcl_DStringInit(&ds);
	Tcl_UtfToWCharDString(Tcl_DStringValue(&patterns), TCL_INDEX_NONE, &ds);
	nbytes = Tcl_DStringLength(&ds); /* # bytes, not Unicode chars */
	nbytes += sizeof(WCHAR);         /* Terminating \0 */
	dlgFilterPtr[i].pszSpec = (LPCWSTR)ckalloc(nbytes);
	memmove((void *)dlgFilterPtr[i].pszSpec, Tcl_DStringValue(&ds), nbytes);
	Tcl_DStringFree(&ds);
	Tcl_DStringSetLength(&patterns, 0);
    }
    Tcl_DStringFree(&patterns);

    if (initialIndex == 0) {
	initialIndex = 1;       /* If no default, show first entry */
    }
    *initialIndexPtr = initialIndex;
    *dlgFilterPtrPtr = dlgFilterPtr;
    *countPtr = flist.numFilters;

    TkFreeFileFilters(&flist);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Tk_ChooseDirectoryObjCmd --
 *
 *	This function implements the "tk_chooseDirectory" dialog box for the
 *	Windows platform. See the user documentation for details on what it
 *	does. Uses the newer SHBrowseForFolder explorer type interface.
 *
 * Results:
 *	See user documentation.
 *
 * Side effects:
 *	A modal dialog window is created. Tcl_SetServiceMode() is called to
 *	allow background events to be processed
 *
 *----------------------------------------------------------------------
 *
 * The function tk_chooseDirectory pops up a dialog box for the user to select
 * a directory. The following option-value pairs are possible as command line
 * arguments:
 *
 * -initialdir dirname
 *
 * Specifies that the directories in directory should be displayed when the
 * dialog pops up. If this parameter is not specified, then the directories in
 * the current working directory are displayed. If the parameter specifies a
 * relative path, the return value will convert the relative path to an
 * absolute path. This option may not always work on the Macintosh. This is
 * not a bug. Rather, the General Controls control panel on the Mac allows the
 * end user to override the application default directory.
 *
 * -parent window
 *
 * Makes window the logical parent of the dialog. The dialog is displayed on
 * top of its parent window.
 *
 * -title titleString
 *
 * Specifies a string to display as the title of the dialog box. If this
 * option is not specified, then a default title will be displayed.
 *
 * -mustexist boolean
 *
 * Specifies whether the user may specify non-existant directories. If this
 * parameter is true, then the user may only select directories that already
 * exist. The default value is false.
 *
 * New Behaviour:
 *
 * - If mustexist = 0 and a user entered folder does not exist, a prompt will
 *   pop-up asking if the user wants another chance to change it. The old
 *   dialog just returned the bogus entry. On mustexist = 1, the entries MUST
 *   exist before exiting the box with OK.
 *
 *   Bugs:
 *
 * - If valid abs directory name is entered into the entry box and Enter
 *   pressed, the box will close returning the name. This is inconsistent when
 *   entering relative names or names with forward slashes, which are
 *   invalidated then corrected in the callback. After correction, the box is
 *   held open to allow further modification by the user.
 *
 * - Not sure how to implement localization of message prompts.
 *
 * - -title is really -message.
 *
 *----------------------------------------------------------------------
 */

int
Tk_ChooseDirectoryObjCmd(
    void *clientData,	/* Main window associated with interpreter. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    int result;
    OFNOpts ofnOpts;

    result = ParseOFNOptions(clientData, interp, objc, objv,
		 OFN_DIR_CHOOSE, &ofnOpts);
    if (result != TCL_OK)
	return result;

    result = GetFileNameVista(interp, &ofnOpts, OFN_DIR_CHOOSE);
    CleanupOFNOptions(&ofnOpts);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MessageBoxObjCmd --
 *
 *	This function implements the MessageBox window for the Windows
 *	platform. See the user documentation for details on what it does.
 *
 * Results:
 *	See user documentation.
 *
 * Side effects:
 *	None. The MessageBox window will be destroyed before this function
 *	returns.
 *
 *----------------------------------------------------------------------
 */

int
Tk_MessageBoxObjCmd(
    void *clientData,	/* Main window associated with interpreter. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Tk_Window tkwin = (Tk_Window)clientData, parent;
    HWND hWnd;
    Tcl_Obj *messageObj, *titleObj, *detailObj, *tmpObj;
    int defaultBtn, icon, type;
    int i, oldMode, winCode;
    UINT flags;
    static const char *const optionStrings[] = {
	"-default",	"-detail",	"-icon",	"-message",
	"-parent",	"-title",	"-type",	NULL
    };
    enum options {
	MSG_DEFAULT,	MSG_DETAIL,	MSG_ICON,	MSG_MESSAGE,
	MSG_PARENT,	MSG_TITLE,	MSG_TYPE
    };
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    Tcl_DString titleBuf, tmpBuf;
    LPCWSTR titlePtr, tmpPtr;
    const char *src;

    defaultBtn = -1;
    detailObj = NULL;
    icon = MB_ICONINFORMATION;
    messageObj = NULL;
    parent = tkwin;
    titleObj = NULL;
    type = MB_OK;

    for (i = 1; i < objc; i += 2) {
	int index;
	Tcl_Obj *optionPtr, *valuePtr;

	optionPtr = objv[i];
	valuePtr = objv[i + 1];

	if (Tcl_GetIndexFromObj(interp, optionPtr, optionStrings,
		"option", TCL_EXACT, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (i + 1 == objc) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "value for \"%s\" missing", Tcl_GetString(optionPtr)));
	    Tcl_SetErrorCode(interp, "TK", "MSGBOX", "VALUE", (char *)NULL);
	    return TCL_ERROR;
	}

	switch ((enum options) index) {
	case MSG_DEFAULT:
	    defaultBtn = TkFindStateNumObj(interp, optionPtr, buttonMap,
		    valuePtr);
	    if (defaultBtn < 0) {
		return TCL_ERROR;
	    }
	    break;

	case MSG_DETAIL:
	    detailObj = valuePtr;
	    break;

	case MSG_ICON:
	    icon = TkFindStateNumObj(interp, optionPtr, iconMap, valuePtr);
	    if (icon < 0) {
		return TCL_ERROR;
	    }
	    break;

	case MSG_MESSAGE:
	    messageObj = valuePtr;
	    break;

	case MSG_PARENT:
	    parent = Tk_NameToWindow(interp, Tcl_GetString(valuePtr), tkwin);
	    if (parent == NULL) {
		return TCL_ERROR;
	    }
	    break;

	case MSG_TITLE:
	    titleObj = valuePtr;
	    break;

	case MSG_TYPE:
	    type = TkFindStateNumObj(interp, optionPtr, typeMap, valuePtr);
	    if (type < 0) {
		return TCL_ERROR;
	    }
	    break;
	}
    }

    while (!Tk_IsTopLevel(parent)) {
	parent = Tk_Parent(parent);
    }
    Tk_MakeWindowExist(parent);
    hWnd = Tk_GetHWND(Tk_WindowId(parent));

    flags = 0;
    if (defaultBtn >= 0) {
	int defaultBtnIdx = -1;

	for (i = 0; i < (int) NUM_TYPES; i++) {
	    if (type == allowedTypes[i].type) {
		int j;

		for (j = 0; j < 3; j++) {
		    if (allowedTypes[i].btnIds[j] == defaultBtn) {
			defaultBtnIdx = j;
			break;
		    }
		}
		if (defaultBtnIdx < 0) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			    "invalid default button \"%s\"",
			    TkFindStateString(buttonMap, defaultBtn)));
		    Tcl_SetErrorCode(interp, "TK", "MSGBOX", "DEFAULT", (char *)NULL);
		    return TCL_ERROR;
		}
		break;
	    }
	}
	flags = buttonFlagMap[defaultBtnIdx];
    }

    flags |= icon | type | MB_TASKMODAL | MB_SETFOREGROUND;

    tmpObj = messageObj ? Tcl_DuplicateObj(messageObj) : Tcl_NewObj();
    Tcl_IncrRefCount(tmpObj);
    if (detailObj) {
	Tcl_AppendStringsToObj(tmpObj, "\n\n", NULL);
	Tcl_AppendObjToObj(tmpObj, detailObj);
    }

    oldMode = Tcl_SetServiceMode(TCL_SERVICE_ALL);

    /*
     * MessageBoxW exists for all platforms. Use it to allow unicode error
     * message to be displayed correctly where possible by the OS.
     *
     * In order to have the parent window icon reflected in a MessageBox, we
     * have to create a hook that will trigger when the MessageBox is being
     * created.
     */

    tsdPtr->hSmallIcon = TkWinGetIcon(parent, ICON_SMALL);
    tsdPtr->hBigIcon   = TkWinGetIcon(parent, ICON_BIG);
    tsdPtr->hMsgBoxHook = SetWindowsHookExW(WH_CBT, MsgBoxCBTProc, NULL,
	    GetCurrentThreadId());
    src = Tcl_GetString(tmpObj);
    Tcl_DStringInit(&tmpBuf);
    tmpPtr = Tcl_UtfToWCharDString(src, tmpObj->length, &tmpBuf);
    if (titleObj != NULL) {
	src = Tcl_GetString(titleObj);
	Tcl_DStringInit(&titleBuf);
	titlePtr = Tcl_UtfToWCharDString(src, titleObj->length, &titleBuf);
    } else {
	titlePtr = L"";
	Tcl_DStringInit(&titleBuf);
    }
    winCode = MessageBoxW(hWnd, tmpPtr, titlePtr, flags);
    Tcl_DStringFree(&titleBuf);
    Tcl_DStringFree(&tmpBuf);
    UnhookWindowsHookEx(tsdPtr->hMsgBoxHook);
    (void) Tcl_SetServiceMode(oldMode);

    /*
     * Ensure that hWnd is enabled, because it can happen that we have updated
     * the wrapper of the parent, which causes us to leave this child disabled
     * (Windows loses sync).
     */

    EnableWindow(hWnd, 1);

    Tcl_DecrRefCount(tmpObj);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(
	    TkFindStateString(buttonMap, winCode), TCL_INDEX_NONE));
    return TCL_OK;
}

static LRESULT CALLBACK
MsgBoxCBTProc(
    int nCode,
    WPARAM wParam,
    LPARAM lParam)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (nCode == HCBT_CREATEWND) {
	/*
	 * Window owned by our task is being created. Since the hook is
	 * installed just before the MessageBox call and removed after the
	 * MessageBox call, the window being created is either the message box
	 * or one of its controls. Check that the class is WC_DIALOG to ensure
	 * that it's the one we want.
	 */

	LPCBT_CREATEWND lpcbtcreate = (LPCBT_CREATEWND) lParam;

	if (WC_DIALOG == lpcbtcreate->lpcs->lpszClass) {
	    HWND hwnd = (HWND) wParam;

	    SendMessageW(hwnd, WM_SETICON, ICON_SMALL,
		    (LPARAM) tsdPtr->hSmallIcon);
	    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM) tsdPtr->hBigIcon);
	}
    }

    /*
     * Call the next hook proc, if there is one
     */

    return CallNextHookEx(tsdPtr->hMsgBoxHook, nCode, wParam, lParam);
}

/*
 * ----------------------------------------------------------------------
 *
 * SetTestDialog --
 *
 *	Records the HWND for a native dialog in the variable
 *	"::tk::test::dialog::testDialog" so that the test-suite can operate
 *	on the correct dialog window. Use of this is enabled when a test
 *	program calls TkWinDialogDebug by calling the test command
 *	'testwinevent debug 1'.
 *
 * ----------------------------------------------------------------------
 */

static void
SetTestDialog(
    void *clientData)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    char buf[32];

    snprintf(buf, sizeof(buf), "0x%" TCL_Z_MODIFIER "x", (size_t)clientData);
    Tcl_SetVar2(tsdPtr->debugInterp, "::tk::test::dialog::testDialog", NULL,
		buf, TCL_GLOBAL_ONLY);
}

/*
 * Factored out a common pattern in use in this file.
 */

static const char *
ConvertExternalFilename(
    LPCWSTR  filename,
    Tcl_DString *dsPtr)
{
    char *p;

    Tcl_DStringInit(dsPtr);
    Tcl_WCharToUtfDString(filename, wcslen(filename), dsPtr);
    for (p = Tcl_DStringValue(dsPtr); *p != '\0'; p++) {
	/*
	 * Change the pathname to the Tcl "normalized" pathname, where back
	 * slashes are used instead of forward slashes
	 */

	if (*p == '\\') {
	    *p = '/';
	}
    }
    return Tcl_DStringValue(dsPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * GetFontObj --
 *
 *	Convert a windows LOGFONT into a Tk font description.
 *
 * Result:
 *	A list containing a Tk font description.
 *
 * ----------------------------------------------------------------------
 */

static Tcl_Obj *
GetFontObj(
    HDC hdc,
    LOGFONTW *plf)
{
    Tcl_DString ds;
    Tcl_Obj *resObj;
    int pt = 0;

    resObj = Tcl_NewListObj(0, NULL);
    Tcl_DStringInit(&ds);
    Tcl_WCharToUtfDString(plf->lfFaceName, wcslen(plf->lfFaceName), &ds);
    Tcl_ListObjAppendElement(NULL, resObj,
	    Tcl_NewStringObj(Tcl_DStringValue(&ds), TCL_INDEX_NONE));
    Tcl_DStringFree(&ds);
    pt = -MulDiv(plf->lfHeight, 72, GetDeviceCaps(hdc, LOGPIXELSY));
    Tcl_ListObjAppendElement(NULL, resObj, Tcl_NewWideIntObj(pt));
    if (plf->lfWeight >= 700) {
	Tcl_ListObjAppendElement(NULL, resObj, Tcl_NewStringObj("bold", TCL_INDEX_NONE));
    }
    if (plf->lfItalic) {
	Tcl_ListObjAppendElement(NULL, resObj,
		Tcl_NewStringObj("italic", TCL_INDEX_NONE));
    }
    if (plf->lfUnderline) {
	Tcl_ListObjAppendElement(NULL, resObj,
		Tcl_NewStringObj("underline", TCL_INDEX_NONE));
    }
    if (plf->lfStrikeOut) {
	Tcl_ListObjAppendElement(NULL, resObj,
		Tcl_NewStringObj("overstrike", TCL_INDEX_NONE));
    }
    return resObj;
}

static void
ApplyLogfont(
    Tcl_Interp *interp,
    Tcl_Obj *cmdObj,
    HDC hdc,
    LOGFONTW *logfontPtr)
{
    Tcl_Size objc;
    Tcl_Obj **objv, **tmpv;

    Tcl_ListObjGetElements(NULL, cmdObj, &objc, &objv);
    tmpv = (Tcl_Obj **)ckalloc(sizeof(Tcl_Obj *) * (objc + 2));
    memcpy(tmpv, objv, sizeof(Tcl_Obj *) * objc);
    tmpv[objc] = GetFontObj(hdc, logfontPtr);
    TkBackgroundEvalObjv(interp, objc+1, tmpv, TCL_EVAL_GLOBAL);
    ckfree(tmpv);
}

/*
 * ----------------------------------------------------------------------
 *
 * HookProc --
 *
 *	Font selection hook. If the user selects Apply on the dialog, we call
 *	the applyProc script with the currently selected font as arguments.
 *
 * ----------------------------------------------------------------------
 */

typedef struct HookData {
    Tcl_Interp *interp;
    Tcl_Obj *titleObj;
    Tcl_Obj *cmdObj;
    Tcl_Obj *parentObj;
    Tcl_Obj *fontObj;
    HWND hwnd;
    Tk_Window parent;
} HookData;

static UINT_PTR CALLBACK
HookProc(
    HWND hwndDlg,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    CHOOSEFONTW *pcf = (CHOOSEFONTW *) lParam;
    HWND hwndCtrl;
    static HookData *phd = NULL;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (WM_INITDIALOG == msg && lParam != 0) {
	phd = (HookData *) pcf->lCustData;
	phd->hwnd = hwndDlg;
	if (tsdPtr->debugFlag) {
	    tsdPtr->debugInterp = phd->interp;
	    Tcl_DoWhenIdle(SetTestDialog, hwndDlg);
	}
	if (phd->titleObj != NULL) {
	    Tcl_DString title;

	    Tcl_DStringInit(&title);
	    Tcl_UtfToWCharDString(Tcl_GetString(phd->titleObj), TCL_INDEX_NONE, &title);
	    if (Tcl_DStringLength(&title) > 0) {
		SetWindowTextW(hwndDlg, (LPCWSTR) Tcl_DStringValue(&title));
	    }
	    Tcl_DStringFree(&title);
	}

	/*
	 * Disable the colour combobox (0x473) and its label (0x443).
	 */

	hwndCtrl = GetDlgItem(hwndDlg, 0x443);
	if (IsWindow(hwndCtrl)) {
	    EnableWindow(hwndCtrl, FALSE);
	}
	hwndCtrl = GetDlgItem(hwndDlg, 0x473);
	if (IsWindow(hwndCtrl)) {
	    EnableWindow(hwndCtrl, FALSE);
	}
	Tk_SendVirtualEvent(phd->parent, "TkFontchooserVisibility", NULL);
	return 1; /* we handled the message */
    }

    if (WM_DESTROY == msg) {
	phd->hwnd = NULL;
	Tk_SendVirtualEvent(phd->parent, "TkFontchooserVisibility", NULL);
	return 0;
    }

    /*
     * Handle apply button by calling the provided command script as a
     * background evaluation (ie: errors dont come back here).
     */

    if (WM_COMMAND == msg && LOWORD(wParam) == 1026) {
	LOGFONTW lf = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0, 0}};
	HDC hdc = GetDC(hwndDlg);

	SendMessageW(hwndDlg, WM_CHOOSEFONT_GETLOGFONT, 0, (LPARAM) &lf);
	if (phd && phd->cmdObj) {
	    ApplyLogfont(phd->interp, phd->cmdObj, hdc, &lf);
	}
	if (phd && phd->parent) {
	    Tk_SendVirtualEvent(phd->parent, "TkFontchooserFontChanged", NULL);
	}
	return 1;
    }
    return 0; /* pass on for default processing */
}

/*
 * Helper for the FontchooserConfigure command to return the current value of
 * any of the options (which may be NULL in the structure)
 */

enum FontchooserOption {
    FontchooserCmd, FontchooserFont, FontchooserParent, FontchooserTitle,
    FontchooserVisible
};

static Tcl_Obj *
FontchooserCget(
    HookData *hdPtr,
    int optionIndex)
{
    Tcl_Obj *resObj = NULL;

    switch(optionIndex) {
    case FontchooserParent:
	if (hdPtr->parentObj) {
	    resObj = hdPtr->parentObj;
	} else {
	    resObj = Tcl_NewStringObj(".", 1);
	}
	break;
    case FontchooserTitle:
	if (hdPtr->titleObj) {
	    resObj = hdPtr->titleObj;
	} else {
	    resObj =  Tcl_NewStringObj("", 0);
	}
	break;
    case FontchooserFont:
	if (hdPtr->fontObj) {
	    resObj = hdPtr->fontObj;
	} else {
	    resObj = Tcl_NewStringObj("", 0);
	}
	break;
    case FontchooserCmd:
	if (hdPtr->cmdObj) {
	    resObj = hdPtr->cmdObj;
	} else {
	    resObj = Tcl_NewStringObj("", 0);
	}
	break;
    case FontchooserVisible:
	resObj = Tcl_NewBooleanObj((hdPtr->hwnd != NULL) && IsWindow(hdPtr->hwnd));
	break;
    default:
	resObj = Tcl_NewStringObj("", 0);
    }
    return resObj;
}

/*
 * ----------------------------------------------------------------------
 *
 * FontchooserConfigureCmd --
 *
 *	Implementation of the 'tk fontchooser configure' ensemble command. See
 *	the user documentation for what it does.
 *
 * Results:
 *	See the user documentation.
 *
 * Side effects:
 *	Per-interp data structure may be modified
 *
 * ----------------------------------------------------------------------
 */

static int
FontchooserConfigureCmd(
    void *clientData,	/* Main window */
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    Tk_Window tkwin = (Tk_Window)clientData;
    HookData *hdPtr = NULL;
    Tcl_Size i;
    int r = TCL_OK;
    static const char *const optionStrings[] = {
	"-command", "-font", "-parent", "-title", "-visible", NULL
    };

    hdPtr = (HookData *)Tcl_GetAssocData(interp, "::tk::fontchooser", NULL);

    /*
     * With no arguments we return all the options in a dict.
     */

    if (objc == 1) {
	Tcl_Obj *keyObj, *valueObj;
	Tcl_Obj *dictObj = Tcl_NewDictObj();

	for (i = 0; r == TCL_OK && optionStrings[i] != NULL; ++i) {
	    keyObj = Tcl_NewStringObj(optionStrings[i], TCL_INDEX_NONE);
	    valueObj = FontchooserCget(hdPtr, i);
	    r = Tcl_DictObjPut(interp, dictObj, keyObj, valueObj);
	}
	if (r == TCL_OK) {
	    Tcl_SetObjResult(interp, dictObj);
	}
	return r;
    }

    for (i = 1; i < objc; i += 2) {
	int optionIndex;

	if (Tcl_GetIndexFromObj(interp, objv[i], optionStrings,
		"option", 0, &optionIndex) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (objc == 2) {
	    /*
	     * If one option and no arg - return the current value.
	     */

	    Tcl_SetObjResult(interp, FontchooserCget(hdPtr, optionIndex));
	    return TCL_OK;
	}
	if (i + 1 == objc) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "value for \"%s\" missing", Tcl_GetString(objv[i])));
	    Tcl_SetErrorCode(interp, "TK", "FONTDIALOG", "VALUE", (char *)NULL);
	    return TCL_ERROR;
	}
	switch (optionIndex) {
	case FontchooserVisible: {
	    static const char *msg = "cannot change read-only option "
		    "\"-visible\": use the show or hide command";

	    Tcl_SetObjResult(interp, Tcl_NewStringObj(msg, TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "FONTDIALOG", "READONLY", (char *)NULL);
	    return TCL_ERROR;
	}
	case FontchooserParent: {
	    Tk_Window parent = Tk_NameToWindow(interp,
		    Tcl_GetString(objv[i+1]), tkwin);

	    if (parent == NULL) {
		return TCL_ERROR;
	    }
	    if (hdPtr->parentObj) {
		Tcl_DecrRefCount(hdPtr->parentObj);
	    }
	    hdPtr->parentObj = objv[i+1];
	    if (Tcl_IsShared(hdPtr->parentObj)) {
		hdPtr->parentObj = Tcl_DuplicateObj(hdPtr->parentObj);
	    }
	    Tcl_IncrRefCount(hdPtr->parentObj);
	    break;
	}
	case FontchooserTitle:
	    if (hdPtr->titleObj) {
		Tcl_DecrRefCount(hdPtr->titleObj);
	    }
	    hdPtr->titleObj = objv[i+1];
	    if (Tcl_IsShared(hdPtr->titleObj)) {
		hdPtr->titleObj = Tcl_DuplicateObj(hdPtr->titleObj);
	    }
	    Tcl_IncrRefCount(hdPtr->titleObj);
	    break;
	case FontchooserFont:
	    if (hdPtr->fontObj) {
		Tcl_DecrRefCount(hdPtr->fontObj);
	    }
	    Tcl_GetString(objv[i+1]);
	    if (objv[i+1]->length) {
		hdPtr->fontObj = objv[i+1];
		if (Tcl_IsShared(hdPtr->fontObj)) {
		    hdPtr->fontObj = Tcl_DuplicateObj(hdPtr->fontObj);
		}
		Tcl_IncrRefCount(hdPtr->fontObj);
	    } else {
		hdPtr->fontObj = NULL;
	    }
	    break;
	case FontchooserCmd:
	    if (hdPtr->cmdObj) {
		Tcl_DecrRefCount(hdPtr->cmdObj);
	    }
	    Tcl_GetString(objv[i+1]);
	    if (objv[i+1]->length) {
		hdPtr->cmdObj = objv[i+1];
		if (Tcl_IsShared(hdPtr->cmdObj)) {
		    hdPtr->cmdObj = Tcl_DuplicateObj(hdPtr->cmdObj);
		}
		Tcl_IncrRefCount(hdPtr->cmdObj);
	    } else {
		hdPtr->cmdObj = NULL;
	    }
	    break;
	}
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * FontchooserShowCmd --
 *
 *	Implements the 'tk fontchooser show' ensemble command. The per-interp
 *	configuration data for the dialog is held in an interp associated
 *	structure.
 *
 *	Calls the Win32 FontChooser API which provides a modal dialog. See
 *	HookProc where we make a few changes to the dialog and set some
 *	additional state.
 *
 * ----------------------------------------------------------------------
 */

static int
FontchooserShowCmd(
    void *clientData,	/* Main window */
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size),
    TCL_UNUSED(Tcl_Obj *const *))
{
    Tcl_DString ds;
    Tk_Window tkwin = (Tk_Window)clientData, parent;
    CHOOSEFONTW cf;
    LOGFONTW lf;
    HDC hdc;
    HookData *hdPtr;
    int r = TCL_OK, oldMode = 0;

    hdPtr = (HookData *)Tcl_GetAssocData(interp, "::tk::fontchooser", NULL);

    parent = tkwin;
    if (hdPtr->parentObj) {
	parent = Tk_NameToWindow(interp, Tcl_GetString(hdPtr->parentObj),
		tkwin);
	if (parent == NULL) {
	    return TCL_ERROR;
	}
    }

    Tk_MakeWindowExist(parent);

    memset(&cf, 0, sizeof(CHOOSEFONTW));
    memset(&lf, 0, sizeof(LOGFONTW));
    lf.lfCharSet = DEFAULT_CHARSET;
    cf.lStructSize = sizeof(CHOOSEFONTW);
    cf.hwndOwner = Tk_GetHWND(Tk_WindowId(parent));
    cf.lpLogFont = &lf;
    cf.nFontType = SCREEN_FONTTYPE;
    cf.Flags = CF_SCREENFONTS | CF_EFFECTS | CF_ENABLEHOOK;
    cf.rgbColors = RGB(0,0,0);
    cf.lpfnHook = HookProc;
    cf.lCustData = (INT_PTR) hdPtr;
    hdPtr->interp = interp;
    hdPtr->parent = parent;
    hdc = GetDC(cf.hwndOwner);

    if (hdPtr->fontObj != NULL) {
	TkFont *fontPtr;
	Tk_Font f = Tk_AllocFontFromObj(interp, tkwin, hdPtr->fontObj);

	if (f == NULL) {
	    return TCL_ERROR;
	}
	fontPtr = (TkFont *) f;
	cf.Flags |= CF_INITTOLOGFONTSTRUCT;
	Tcl_DStringInit(&ds);
	wcsncpy(lf.lfFaceName, Tcl_UtfToWCharDString(fontPtr->fa.family, TCL_INDEX_NONE, &ds),
		LF_FACESIZE-1);
	Tcl_DStringFree(&ds);
	lf.lfFaceName[LF_FACESIZE-1] = 0;
	lf.lfHeight = -MulDiv((int)(TkFontGetPoints(tkwin, fontPtr->fa.size) + 0.5),
	    GetDeviceCaps(hdc, LOGPIXELSY), 72);
	if (fontPtr->fa.weight == TK_FW_BOLD) {
	    lf.lfWeight = FW_BOLD;
	}
	if (fontPtr->fa.slant != TK_FS_ROMAN) {
	    lf.lfItalic = TRUE;
	}
	if (fontPtr->fa.underline) {
	    lf.lfUnderline = TRUE;
	}
	if (fontPtr->fa.overstrike) {
	    lf.lfStrikeOut = TRUE;
	}
	Tk_FreeFont(f);
    }

    if (TCL_OK == r && hdPtr->cmdObj != NULL) {
	Tcl_Size len = 0;

	r = Tcl_ListObjLength(interp, hdPtr->cmdObj, &len);
	if (len > 0) {
	    cf.Flags |= CF_APPLY;
	}
    }

    if (TCL_OK == r) {
	oldMode = Tcl_SetServiceMode(TCL_SERVICE_ALL);
	if (ChooseFontW(&cf)) {
	    if (hdPtr->cmdObj) {
		ApplyLogfont(hdPtr->interp, hdPtr->cmdObj, hdc, &lf);
	    }
	    if (hdPtr->parent) {
		Tk_SendVirtualEvent(hdPtr->parent, "TkFontchooserFontChanged", NULL);
	    }
	}
	Tcl_SetServiceMode(oldMode);
	EnableWindow(cf.hwndOwner, 1);
    }

    ReleaseDC(cf.hwndOwner, hdc);
    return r;
}

/*
 * ----------------------------------------------------------------------
 *
 * FontchooserHideCmd --
 *
 *	Implementation of the 'tk fontchooser hide' ensemble. See the user
 *	documentation for details.
 *	As the Win32 FontChooser function is always modal all we do here is
 *	destroy the dialog
 *
 * ----------------------------------------------------------------------
 */

static int
FontchooserHideCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size),
    TCL_UNUSED(Tcl_Obj *const *))
{
    HookData *hdPtr = (HookData *)Tcl_GetAssocData(interp, "::tk::fontchooser", NULL);

    if (hdPtr->hwnd && IsWindow(hdPtr->hwnd)) {
	EndDialog(hdPtr->hwnd, 0);
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * DeleteHookData --
 *
 *	Clean up the font chooser configuration data when the interp is
 *	destroyed.
 *
 * ----------------------------------------------------------------------
 */

static void
DeleteHookData(
    void *clientData,
    TCL_UNUSED(Tcl_Interp *))
{
    HookData *hdPtr = (HookData *)clientData;

    if (hdPtr->parentObj) {
	Tcl_DecrRefCount(hdPtr->parentObj);
    }
    if (hdPtr->fontObj) {
	Tcl_DecrRefCount(hdPtr->fontObj);
    }
    if (hdPtr->titleObj) {
	Tcl_DecrRefCount(hdPtr->titleObj);
    }
    if (hdPtr->cmdObj) {
	Tcl_DecrRefCount(hdPtr->cmdObj);
    }
    ckfree(hdPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * TkInitFontchooser --
 *
 *	Associate the font chooser configuration data with the Tcl
 *	interpreter. There is one font chooser per interp.
 *
 * ----------------------------------------------------------------------
 */

MODULE_SCOPE const TkEnsemble tkFontchooserEnsemble[];
const TkEnsemble tkFontchooserEnsemble[] = {
    { "configure", FontchooserConfigureCmd, NULL },
    { "show", FontchooserShowCmd, NULL },
    { "hide", FontchooserHideCmd, NULL },
    { NULL, NULL, NULL }
};

int
TkInitFontchooser(
    Tcl_Interp *interp,
    TCL_UNUSED(void *))
{
    HookData *hdPtr = (HookData *)ckalloc(sizeof(HookData));

    memset(hdPtr, 0, sizeof(HookData));
    Tcl_SetAssocData(interp, "::tk::fontchooser", DeleteHookData, hdPtr);
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
