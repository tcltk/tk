/* 
 * tkText.c --
 *
 *	This module provides a big chunk of the implementation of
 *	multi-line editable text widgets for Tk.  Among other things,
 *	it provides the Tcl command interfaces to text widgets and
 *	the display code.  The B-tree representation of text is
 *	implemented elsewhere.
 *
 * Copyright (c) 1992-1994 The Regents of the University of California.
 * Copyright (c) 1994-1996 Sun Microsystems, Inc.
 * Copyright (c) 1999 by Scriptics Corporation.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkText.c,v 1.38 2003/05/27 15:35:52 vincentdarley Exp $
 */

#include "default.h"
#include "tkPort.h"
#include "tkInt.h"
#include "tkUndo.h"

#if defined(MAC_TCL) || defined(MAC_OSX_TK)
#define Style TkStyle
#define DInfo TkDInfo
#endif

#include "tkText.h"

/*
 * The 'TkTextState' enum in tkText.h is used to define a type for the
 * -state option of the Text widget.  These values are used as indices
 * into the string table below.
 */

static char *stateStrings[] = {
    "disabled", "normal", (char *) NULL
};

/*
 * The 'TkWrapMode' enum in tkText.h is used to define a type for the
 * -wrap option of the Text widget.  These values are used as indices
 * into the string table below.
 */

static char *wrapStrings[] = {
    "char", "none", "word", (char *) NULL
};

/*
 * Information used to parse text configuration options:
 */

static Tk_OptionSpec optionSpecs[] = {
    {TK_OPTION_BOOLEAN, "-autoseparators", "autoSeparators",
        "AutoSeparators", DEF_TEXT_AUTO_SEPARATORS, -1,
        Tk_Offset(TkText, autoSeparators), 0, 0, 0},
    {TK_OPTION_BORDER, "-background", "background", "Background",
	DEF_TEXT_BG_COLOR, -1, Tk_Offset(TkText, border),
	0, (ClientData) DEF_TEXT_BG_MONO, 0},
    {TK_OPTION_SYNONYM, "-bd", (char *) NULL, (char *) NULL,
	(char *) NULL, 0, -1, 0, (ClientData) "-borderwidth", 0},
    {TK_OPTION_SYNONYM, "-bg", (char *) NULL, (char *) NULL,
	(char *) NULL, 0, -1, 0, (ClientData) "-background", 0},
    {TK_OPTION_PIXELS, "-borderwidth", "borderWidth", "BorderWidth",
	DEF_TEXT_BORDER_WIDTH, -1, Tk_Offset(TkText, borderWidth), 
	0, 0, 0},
    {TK_OPTION_CURSOR, "-cursor", "cursor", "Cursor",
	DEF_TEXT_CURSOR, -1, Tk_Offset(TkText, cursor),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BOOLEAN, "-exportselection", "exportSelection",
	"ExportSelection", DEF_TEXT_EXPORT_SELECTION, -1, 
	Tk_Offset(TkText, exportSelection), 0, 0, 0},
    {TK_OPTION_SYNONYM, "-fg", "foreground", (char *) NULL,
	(char *) NULL, 0, -1, 0, (ClientData) "-foreground", 0},
    {TK_OPTION_FONT, "-font", "font", "Font",
	DEF_TEXT_FONT, -1, Tk_Offset(TkText, tkfont), 0, 0, 0},
    {TK_OPTION_COLOR, "-foreground", "foreground", "Foreground",
	DEF_TEXT_FG, -1, Tk_Offset(TkText, fgColor), 0, 
	0, 0},
    {TK_OPTION_PIXELS, "-height", "height", "Height",
	DEF_TEXT_HEIGHT, -1, Tk_Offset(TkText, height), 0, 0, 0},
    {TK_OPTION_COLOR, "-highlightbackground", "highlightBackground",
	"HighlightBackground", DEF_TEXT_HIGHLIGHT_BG,
	-1, Tk_Offset(TkText, highlightBgColorPtr), 
	0, 0, 0},
    {TK_OPTION_COLOR, "-highlightcolor", "highlightColor", "HighlightColor",
	DEF_TEXT_HIGHLIGHT, -1, Tk_Offset(TkText, highlightColorPtr),
	0, 0, 0},
    {TK_OPTION_PIXELS, "-highlightthickness", "highlightThickness",
	"HighlightThickness", DEF_TEXT_HIGHLIGHT_WIDTH, -1, 
	Tk_Offset(TkText, highlightWidth), 0, 0, 0},
    {TK_OPTION_BORDER, "-insertbackground", "insertBackground", "Foreground",
	DEF_TEXT_INSERT_BG,
	-1, Tk_Offset(TkText, insertBorder), 
	0, 0, 0},
    {TK_OPTION_PIXELS, "-insertborderwidth", "insertBorderWidth", 
	"BorderWidth", DEF_TEXT_INSERT_BD_COLOR, -1, 
	Tk_Offset(TkText, insertBorderWidth), 0, 
	(ClientData) DEF_TEXT_INSERT_BD_MONO, 0},
    {TK_OPTION_INT, "-insertofftime", "insertOffTime", "OffTime",
	DEF_TEXT_INSERT_OFF_TIME, -1, Tk_Offset(TkText, insertOffTime), 
	0, 0, 0},
    {TK_OPTION_INT, "-insertontime", "insertOnTime", "OnTime",
	DEF_TEXT_INSERT_ON_TIME, -1, Tk_Offset(TkText, insertOnTime), 
	0, 0, 0},
    {TK_OPTION_PIXELS, "-insertwidth", "insertWidth", "InsertWidth",
	DEF_TEXT_INSERT_WIDTH, -1, Tk_Offset(TkText, insertWidth), 
	0, 0, 0},
    {TK_OPTION_INT, "-maxundo", "maxUndo", "MaxUndo",
	DEF_TEXT_MAX_UNDO, -1, Tk_Offset(TkText, maxUndo), 0, 0, 0},
    {TK_OPTION_PIXELS, "-padx", "padX", "Pad",
	DEF_TEXT_PADX, -1, Tk_Offset(TkText, padX), 0, 0, 0},
    {TK_OPTION_PIXELS, "-pady", "padY", "Pad",
	DEF_TEXT_PADY, -1, Tk_Offset(TkText, padY), 0, 0, 0},
    {TK_OPTION_RELIEF, "-relief", "relief", "Relief",
	DEF_TEXT_RELIEF, -1, Tk_Offset(TkText, relief), 0, 0, 0},
    {TK_OPTION_BORDER, "-selectbackground", "selectBackground", "Foreground",
        DEF_TEXT_SELECT_COLOR, -1, Tk_Offset(TkText, selBorder),
	0, (ClientData) DEF_TEXT_SELECT_MONO, 0},
    {TK_OPTION_PIXELS, "-selectborderwidth", "selectBorderWidth", 
	"BorderWidth", DEF_TEXT_SELECT_BD_COLOR, 
	Tk_Offset(TkText, selBorderWidthPtr), 
	Tk_Offset(TkText, selBorderWidth), 
	TK_OPTION_NULL_OK, (ClientData) DEF_TEXT_SELECT_BD_MONO, 0},
    {TK_OPTION_COLOR, "-selectforeground", "selectForeground", "Background",
	DEF_TEXT_SELECT_FG_COLOR, -1, Tk_Offset(TkText, selFgColorPtr),
	0, (ClientData) DEF_TEXT_SELECT_FG_MONO, 0},
    {TK_OPTION_BOOLEAN, "-setgrid", "setGrid", "SetGrid",
	DEF_TEXT_SET_GRID, -1, Tk_Offset(TkText, setGrid), 0, 0, 0},
    {TK_OPTION_PIXELS, "-spacing1", "spacing1", "Spacing",
	DEF_TEXT_SPACING1, -1, Tk_Offset(TkText, spacing1),
	TK_OPTION_DONT_SET_DEFAULT, 0 , 0 },
    {TK_OPTION_PIXELS, "-spacing2", "spacing2", "Spacing",
	DEF_TEXT_SPACING2, -1, Tk_Offset(TkText, spacing2),
	TK_OPTION_DONT_SET_DEFAULT, 0 , 0 },
    {TK_OPTION_PIXELS, "-spacing3", "spacing3", "Spacing",
	DEF_TEXT_SPACING3, -1, Tk_Offset(TkText, spacing3),
	TK_OPTION_DONT_SET_DEFAULT, 0 , 0 },
    {TK_OPTION_STRING_TABLE, "-state", "state", "State",
        DEF_TEXT_STATE, -1, Tk_Offset(TkText, state), 
	0, (ClientData) stateStrings, 0},
    {TK_OPTION_STRING, "-tabs", "tabs", "Tabs",
	DEF_TEXT_TABS, Tk_Offset(TkText, tabOptionPtr), -1,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-takefocus", "takeFocus", "TakeFocus",
	DEF_TEXT_TAKE_FOCUS, -1, Tk_Offset(TkText, takeFocus), 
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BOOLEAN, "-undo", "undo", "Undo",
        DEF_TEXT_UNDO, -1, Tk_Offset(TkText, undo), 0, 0 , 0},
    {TK_OPTION_INT, "-width", "width", "Width",
        DEF_TEXT_WIDTH, -1, Tk_Offset(TkText, width), 0, 0, 0},
    {TK_OPTION_STRING_TABLE, "-wrap", "wrap", "Wrap",
        DEF_TEXT_WRAP, -1, Tk_Offset(TkText, wrapMode), 
	0, (ClientData) wrapStrings, 0},
    {TK_OPTION_STRING, "-xscrollcommand", "xScrollCommand", "ScrollCommand",
        DEF_TEXT_XSCROLL_COMMAND, -1, Tk_Offset(TkText, xScrollCmd),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-yscrollcommand", "yScrollCommand", "ScrollCommand",
        DEF_TEXT_YSCROLL_COMMAND, -1, Tk_Offset(TkText, yScrollCmd),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_END}
};

/*
 * These three typedefs, the structure and the SearchPerform, SearchCore
 * functions below are used for line-based searches of the text widget,
 * and, in particular, to handle multi-line matching even though the text
 * widget is a single-line based data structure.  They are completely
 * abstracted away from the Text widget internals, however, so could
 * easily be re-used with any line-based entity to provide multi-line
 * matching.
 * 
 * We have abstracted this code away from the text widget to try to
 * keep Tk as modular as possible.
 */

struct SearchSpec;	/* Forward declaration. */

typedef ClientData      SearchAddLineProc _ANSI_ARGS_((int lineNum,
			    struct SearchSpec *searchSpecPtr, 
			    Tcl_Obj *theLine, int *lenPtr));
typedef int             SearchMatchProc _ANSI_ARGS_((int lineNum, 
			    struct SearchSpec *searchSpecPtr, 
			    ClientData clientData, Tcl_Obj *theLine, 
			    int matchOffset, int matchLength));
typedef int             SearchLineIndexProc _ANSI_ARGS_((Tcl_Interp *interp,
			    Tcl_Obj *objPtr, struct SearchSpec *searchSpecPtr,
			    int *linePosPtr, int *offsetPosPtr));

typedef struct SearchSpec {
    int exact;                       /* Whether search is exact or regexp */
    int noCase;                      /* Case-insenstivive? */
    int noLineStop;                  /* If not set, a regexp search will
				      * use the TCL_REG_NLSTOP flag */
    int all;                         /* Whether all or the first match should 
				      * be reported */
    int startLine;                   /* First line to examine */
    int startOffset;                 /* Index in first line to start at */
    int stopLine;                    /* Last line to examine, or -1 when we
				      * search all available text */
    int stopOffset;                  /* Index to stop at, provided stopLine
				      * is not -1 */
    int numLines;                    /* Total lines which are available */
    int backwards;                   /* Searching forwards or backwards */
    Tcl_Obj *varPtr;                 /* If non-NULL, store length(s) of 
				      * match(es) in this variable */
    Tcl_Obj *countPtr;               /* Keeps track of currently found 
				      * lengths */
    Tcl_Obj *resPtr;                 /* Keeps track of currently found 
				      * locations */
    int searchElide;                 /* Search in hidden text as well */
    SearchAddLineProc *addLineProc;  /* Function to call when we need to
                                      * add another line to the search string
                                      * so far */
    SearchMatchProc *foundMatchProc; /* Function to call when we have
				      * found a match */
    SearchLineIndexProc *lineIndexProc;/* Function to call when we have
				      * found a match */
    ClientData clientData;	     /* Information about structure being
                          	      * searched, in this case a text 
                          	      * widget. */
} SearchSpec;

/* 
 * The text-widget-independent functions which actually perform
 * the search, handling both regexp and exact searches.
 */
static int SearchCore    _ANSI_ARGS_((Tcl_Interp *interp,
			    SearchSpec *searchSpecPtr, Tcl_Obj *patObj));
static int SearchPerform _ANSI_ARGS_((Tcl_Interp *interp,
			    SearchSpec *searchSpecPtr, Tcl_Obj *patObj,
			    Tcl_Obj *fromPtr, Tcl_Obj *toPtr));

/*
 * Boolean variable indicating whether or not special debugging code
 * should be executed.
 */

int tkTextDebug = 0;

/*
 * Forward declarations for procedures defined later in this file:
 */

static int		ConfigureText _ANSI_ARGS_((Tcl_Interp *interp,
			    TkText *textPtr, int objc, Tcl_Obj *CONST objv[]));
static int		DeleteChars _ANSI_ARGS_((TkText *textPtr,
			    Tcl_Obj *index1Obj, Tcl_Obj *index2Obj,
			    CONST TkTextIndex *indexPtr1, 
			    CONST TkTextIndex *indexPtr2));
static void		DestroyText _ANSI_ARGS_((char *memPtr));
static int		InsertChars _ANSI_ARGS_((TkText *textPtr,
			    TkTextIndex *indexPtr, Tcl_Obj *stringPtr));
static void		TextBlinkProc _ANSI_ARGS_((ClientData clientData));
static void		TextCmdDeletedProc _ANSI_ARGS_((
			    ClientData clientData));
static void		TextEventProc _ANSI_ARGS_((ClientData clientData,
			    XEvent *eventPtr));
static int		TextFetchSelection _ANSI_ARGS_((ClientData clientData,
			    int offset, char *buffer, int maxBytes));
static int		TextIndexSortProc _ANSI_ARGS_((CONST VOID *first,
			    CONST VOID *second));
static int		TextSearchCmd _ANSI_ARGS_((TkText *textPtr,
			    Tcl_Interp *interp, 
			    int objc, Tcl_Obj *CONST objv[]));
static int		TextEditCmd _ANSI_ARGS_((TkText *textPtr,
			    Tcl_Interp *interp, 
			    int objc, Tcl_Obj *CONST objv[]));
static int		TextWidgetObjCmd _ANSI_ARGS_((ClientData clientData,
			    Tcl_Interp *interp, 
			    int objc, Tcl_Obj *CONST objv[]));
static void		TextWorldChanged _ANSI_ARGS_((
			    ClientData instanceData));
static int		TextDumpCmd _ANSI_ARGS_((TkText *textPtr,
			    Tcl_Interp *interp, 
			    int objc, Tcl_Obj *CONST objv[]));
static void		DumpLine _ANSI_ARGS_((Tcl_Interp *interp, 
			    TkText *textPtr, int what, TkTextLine *linePtr,
			    int start, int end, int lineno,
			    CONST char *command));
static int		DumpSegment _ANSI_ARGS_((Tcl_Interp *interp, char *key,
			    char *value, CONST char * command,
			    CONST TkTextIndex *index, int what));
static int		TextEditUndo _ANSI_ARGS_((TkText *textPtr));
static int		TextEditRedo _ANSI_ARGS_((TkText *textPtr));
static Tcl_Obj*		TextGetText _ANSI_ARGS_((CONST TkTextIndex * index1,
			    CONST TkTextIndex * index2));
static void		UpdateDirtyFlag _ANSI_ARGS_((TkText *textPtr));
static void             TextPushUndoAction _ANSI_ARGS_((TkText *textPtr, 
			    Tcl_Obj *undoString, int insert, 
			    CONST TkTextIndex *index1Ptr,
			    CONST TkTextIndex *index2Ptr));
static int              TextSearchIndexInLine _ANSI_ARGS_((
			    CONST SearchSpec *searchSpecPtr, 
			    TkTextLine *linePtr, int byteIndex));

/* 
 * Declarations of the three search procs required by
 * the multi-line search routines
 */
static SearchMatchProc         TextSearchFoundMatch;
static SearchAddLineProc       TextSearchAddNextLine;
static SearchLineIndexProc     TextSearchGetLineIndex;



/*
 * The structure below defines text class behavior by means of procedures
 * that can be invoked from generic window code.
 */

static Tk_ClassProcs textClass = {
    sizeof(Tk_ClassProcs),	/* size */
    TextWorldChanged,		/* worldChangedProc */
};


/*
 *--------------------------------------------------------------
 *
 * Tk_TextObjCmd --
 *
 *	This procedure is invoked to process the "text" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

int
Tk_TextObjCmd(clientData, interp, objc, objv)
    ClientData clientData;	/* Main window associated with
				 * interpreter. */
    Tcl_Interp *interp;		/* Current interpreter. */
    int objc;			/* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. */
{
    Tk_Window tkwin = (Tk_Window) clientData;
    Tk_Window new;
    Tk_OptionTable optionTable;
    register TkText *textPtr;
    TkTextIndex startIndex;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "pathName ?options?");
	return TCL_ERROR;
    }

    /*
     * Create the window.
     */

    new = Tk_CreateWindowFromPath(interp, tkwin, Tcl_GetString(objv[1]), 
				  (char *) NULL);
    if (new == NULL) {
	return TCL_ERROR;
    }

    /*
     * Create the text widget and initialize everything to zero,
     * then set the necessary initial (non-NULL) values.
     */

    textPtr = (TkText *) ckalloc(sizeof(TkText));
    memset((VOID *) textPtr, 0, sizeof(TkText));

    textPtr->tkwin = new;
    textPtr->display = Tk_Display(new);
    textPtr->interp = interp;
    textPtr->widgetCmd = Tcl_CreateObjCommand(interp,
	    Tk_PathName(textPtr->tkwin), TextWidgetObjCmd,
	    (ClientData) textPtr, TextCmdDeletedProc);
    textPtr->tree = TkBTreeCreate(textPtr);
    Tcl_InitHashTable(&textPtr->tagTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&textPtr->markTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&textPtr->windowTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&textPtr->imageTable, TCL_STRING_KEYS);
    textPtr->state = TK_TEXT_STATE_NORMAL;
    textPtr->relief = TK_RELIEF_FLAT;
    textPtr->cursor = None;
    textPtr->charWidth = 1;
    textPtr->wrapMode = TEXT_WRAPMODE_CHAR;
    textPtr->prevWidth = Tk_Width(new);
    textPtr->prevHeight = Tk_Height(new);
    TkTextCreateDInfo(textPtr);
    TkTextMakeByteIndex(textPtr->tree, 0, 0, &startIndex);
    TkTextSetYView(textPtr, &startIndex, 0);
    textPtr->exportSelection = 1;
    textPtr->pickEvent.type = LeaveNotify;
    textPtr->undoStack = TkUndoInitStack(interp,0);
    textPtr->undo = 1;
    textPtr->isDirtyIncrement = 1;
    textPtr->autoSeparators = 1;
    textPtr->lastEditMode = TK_TEXT_EDIT_OTHER;
    textPtr->tabOptionPtr = NULL;
    textPtr->stateEpoch = 0;
    textPtr->refCount = 1;
    
    /*
     * Create the "sel" tag and the "current" and "insert" marks.
     */

    textPtr->selBorder = NULL;
    textPtr->selBorderWidth = 0;
    textPtr->selBorderWidthPtr = NULL;
    textPtr->selFgColorPtr = NULL;
    textPtr->selTagPtr = TkTextCreateTag(textPtr, "sel");
    textPtr->selTagPtr->reliefString =
	    (char *) ckalloc(sizeof(DEF_TEXT_SELECT_RELIEF));
    strcpy(textPtr->selTagPtr->reliefString, DEF_TEXT_SELECT_RELIEF);
    textPtr->selTagPtr->relief = TK_RELIEF_RAISED;
    textPtr->currentMarkPtr = TkTextSetMark(textPtr, "current", &startIndex);
    textPtr->insertMarkPtr = TkTextSetMark(textPtr, "insert", &startIndex);

    /*
     * Create the option table for this widget class.  If it has already
     * been created, the cached pointer will be returned.
     */

    optionTable = Tk_CreateOptionTable(interp, optionSpecs);

    Tk_SetClass(textPtr->tkwin, "Text");
    Tk_SetClassProcs(textPtr->tkwin, &textClass, (ClientData) textPtr);
    textPtr->optionTable	= optionTable;

    Tk_CreateEventHandler(textPtr->tkwin,
	    ExposureMask|StructureNotifyMask|FocusChangeMask,
	    TextEventProc, (ClientData) textPtr);
    Tk_CreateEventHandler(textPtr->tkwin, KeyPressMask|KeyReleaseMask
	    |ButtonPressMask|ButtonReleaseMask|EnterWindowMask
	    |LeaveWindowMask|PointerMotionMask|VirtualEventMask,
	    TkTextBindProc, (ClientData) textPtr);
    Tk_CreateSelHandler(textPtr->tkwin, XA_PRIMARY, XA_STRING,
	    TextFetchSelection, (ClientData) textPtr, XA_STRING);
    
    if (Tk_InitOptions(interp, (char *) textPtr, optionTable, textPtr->tkwin)
	    != TCL_OK) {
	Tk_DestroyWindow(textPtr->tkwin);
	return TCL_ERROR;
    }
    if (ConfigureText(interp, textPtr, objc-2, objv+2) != TCL_OK) {
	Tk_DestroyWindow(textPtr->tkwin);
	return TCL_ERROR;
    }

    Tcl_SetStringObj(Tcl_GetObjResult(interp), Tk_PathName(textPtr->tkwin),
	    -1);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * TextWidgetObjCmd --
 *
 *	This procedure is invoked to process the Tcl command
 *	that corresponds to a text widget.  See the user
 *	documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

static int
TextWidgetObjCmd(clientData, interp, objc, objv)
    ClientData clientData;	/* Information about text widget. */
    Tcl_Interp *interp;		/* Current interpreter. */
    int objc;			/* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. */
{
    register TkText *textPtr = (TkText *) clientData;
    int result = TCL_OK;
    int index;
    
    static CONST char *optionStrings[] = {
	"bbox", "cget", "compare", "configure", "debug", "delete", 
	"dlineinfo", "dump", "edit", "get", "image", "index", 
	"insert", "mark", "scan", "search", "see", "tag", 
	"window", "xview", "yview", (char *) NULL 
    };
    enum options {
	TEXT_BBOX, TEXT_CGET, TEXT_COMPARE, TEXT_CONFIGURE, TEXT_DEBUG,
	TEXT_DELETE, TEXT_DLINEINFO, TEXT_DUMP, TEXT_EDIT, TEXT_GET,
	TEXT_IMAGE, TEXT_INDEX, TEXT_INSERT, TEXT_MARK, TEXT_SCAN,
	TEXT_SEARCH, TEXT_SEE, TEXT_TAG, TEXT_WINDOW, TEXT_XVIEW, TEXT_YVIEW
    };
    
    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg arg ...?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], optionStrings, "option", 0,
	    &index) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_Preserve((ClientData) textPtr);

    switch ((enum options) index) {
	case TEXT_BBOX: {
	    int x, y, width, height;
	    CONST TkTextIndex *indexPtr;
	    
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "index");
		result = TCL_ERROR;
		goto done;
	    }
	    indexPtr = TkTextGetIndexFromObj(interp, textPtr, objv[2]);
	    if (indexPtr == NULL) {
		result = TCL_ERROR;
		goto done;
	    }
	    if (TkTextCharBbox(textPtr, indexPtr, &x, &y, 
			       &width, &height) == 0) {
		char buf[TCL_INTEGER_SPACE * 4];
		
		sprintf(buf, "%d %d %d %d", x, y, width, height);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	    }
	    break;
	}
	case TEXT_CGET: {
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "option");
		result = TCL_ERROR;
		goto done;
	    } else {
		Tcl_Obj *objPtr = Tk_GetOptionValue(interp, (char *) textPtr,
		  textPtr->optionTable, objv[2], textPtr->tkwin);
		if (objPtr == NULL) {
		    result = TCL_ERROR;
		    goto done;
		} else {
		    Tcl_SetObjResult(interp, objPtr);
		    result = TCL_OK;
		}
	    }
	    break;
	}
	case TEXT_COMPARE: {
	    int relation, value;
	    CONST char *p;
	    CONST TkTextIndex *index1Ptr, *index2Ptr;
	    
	    if (objc != 5) {
		Tcl_WrongNumArgs(interp, 2, objv, "index1 op index2");
		result = TCL_ERROR;
		goto done;
	    }
	    index1Ptr = TkTextGetIndexFromObj(interp, textPtr, objv[2]);
	    index2Ptr = TkTextGetIndexFromObj(interp, textPtr, objv[4]);
	    if (index1Ptr == NULL || index2Ptr == NULL) {
		result = TCL_ERROR;
		goto done;
	    }
	    relation = TkTextIndexCmp(index1Ptr, index2Ptr);
	    p = Tcl_GetString(objv[3]);
	    if (p[0] == '<') {
		    value = (relation < 0);
		if ((p[1] == '=') && (p[2] == 0)) {
		    value = (relation <= 0);
		} else if (p[1] != 0) {
		    compareError:
		    Tcl_AppendResult(interp, "bad comparison operator \"",
			Tcl_GetString(objv[3]), 
			"\": must be <, <=, ==, >=, >, or !=",
			(char *) NULL);
		    result = TCL_ERROR;
		    goto done;
		}
	    } else if (p[0] == '>') {
		    value = (relation > 0);
		if ((p[1] == '=') && (p[2] == 0)) {
		    value = (relation >= 0);
		} else if (p[1] != 0) {
		    goto compareError;
		}
	    } else if ((p[0] == '=') && (p[1] == '=') && (p[2] == 0)) {
		value = (relation == 0);
	    } else if ((p[0] == '!') && (p[1] == '=') && (p[2] == 0)) {
		value = (relation != 0);
	    } else {
		goto compareError;
	    }
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(value));
	    break;
	}
	case TEXT_CONFIGURE: {
	    if (objc <= 3) {
		Tcl_Obj* objPtr = Tk_GetOptionInfo(interp, (char *) textPtr,
					  textPtr->optionTable,
			(objc == 3) ? objv[2] : (Tcl_Obj *) NULL,
					  textPtr->tkwin);
		if (objPtr == NULL) {
		    result = TCL_ERROR;
		    goto done;
		} else {
		    Tcl_SetObjResult(interp, objPtr);
		}
	    } else {
		result = ConfigureText(interp, textPtr, objc-2, objv+2);
	    }
	    break;
	}
	case TEXT_DEBUG: {
	    if (objc > 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "boolean");
		result = TCL_ERROR;
		goto done;
	    }
	    if (objc == 2) {
		Tcl_SetObjResult(interp, Tcl_NewBooleanObj(tkBTreeDebug));
	    } else {
		if (Tcl_GetBooleanFromObj(interp, objv[2], 
					  &tkBTreeDebug) != TCL_OK) {
		    result = TCL_ERROR;
		    goto done;
		}
		tkTextDebug = tkBTreeDebug;
	    }
	    break;
	}
	case TEXT_DELETE: {
	    if (objc < 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "index1 ?index2 ...?");
		result = TCL_ERROR;
		goto done;
	    }
	    if (textPtr->state == TK_TEXT_STATE_NORMAL) {
		if (objc < 5) {
		    /*
		     * Simple case requires no predetermination of indices.
		     */
		    result = DeleteChars(textPtr, objv[2],
			    (objc == 4) ? objv[3] : NULL, NULL, NULL);
		} else {
		    int i;
		    /*
		     * Multi-index pair case requires that we prevalidate
		     * the indices and sort from last to first so that
		     * deletes occur in the exact (unshifted) text.  It
		     * also needs to handle partial and fully overlapping
		     * ranges.  We have to do this with multiple passes.
		     */
		    TkTextIndex *indices, *ixStart, *ixEnd, *lastStart;
		    char *useIdx;

		    objc -= 2;
		    objv += 2;
		    indices = (TkTextIndex *)
			ckalloc((objc + 1) * sizeof(TkTextIndex));

		    /*
		     * First pass verifies that all indices are valid.
		     */
		    for (i = 0; i < objc; i++) {
			CONST TkTextIndex *indexPtr = 
			  TkTextGetIndexFromObj(interp, textPtr, objv[i]);
			
			if (indexPtr == NULL) {
			    result = TCL_ERROR;
			    ckfree((char *) indices);
			    goto done;
			}
			indices[i] = *indexPtr;
		    }
		    /*
		     * Pad out the pairs evenly to make later code easier.
		     */
		    if (objc & 1) {
			indices[i] = indices[i-1];
			TkTextIndexForwChars(&indices[i], 1, &indices[i]);
			objc++;
		    }
		    useIdx = (char *) ckalloc((unsigned) objc);
		    memset(useIdx, 0, (unsigned) objc);
		    /*
		     * Do a decreasing order sort so that we delete the end
		     * ranges first to maintain index consistency.
		     */
		    qsort((VOID *) indices, (unsigned) (objc / 2),
			    2 * sizeof(TkTextIndex), TextIndexSortProc);
		    lastStart = NULL;
		    /*
		     * Second pass will handle bogus ranges (end < start) and
		     * overlapping ranges.
		     */
		    for (i = 0; i < objc; i += 2) {
			ixStart = &indices[i];
			ixEnd   = &indices[i+1];
			if (TkTextIndexCmp(ixEnd, ixStart) <= 0) {
			    continue;
			}
			if (lastStart) {
			    if (TkTextIndexCmp(ixStart, lastStart) == 0) {
				/*
				 * Start indices were equal, and the sort
				 * placed the longest range first, so
				 * skip this one.
				 */
				continue;
			    } else if (TkTextIndexCmp(lastStart, ixEnd) < 0) {
				/*
				 * The next pair has a start range before
				 * the end point of the last range.
				 * Constrain the delete range, but use
				 * the pointer values.
				 */
				*ixEnd = *lastStart;
				if (TkTextIndexCmp(ixEnd, ixStart) <= 0) {
				    continue;
				}
			    }
			}
			lastStart = ixStart;
			useIdx[i]   = 1;
		    }
		    /*
		     * Final pass take the input from the previous and
		     * deletes the ranges which are flagged to be
		     * deleted.
		     */
		    for (i = 0; i < objc; i += 2) {
			if (useIdx[i]) {
			    /*
			     * We don't need to check the return value
			     * because all indices are preparsed above.
			     */
			    DeleteChars(textPtr, NULL, NULL,
				    &indices[i], &indices[i+1]);
			}
		    }
		    ckfree((char *) indices);
		}
	    }
	    break;
	}
	case TEXT_DLINEINFO: {
	    int x, y, width, height, base;
	    CONST TkTextIndex *indexPtr;
	    
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "index");
		result = TCL_ERROR;
		goto done;
	    }
	    indexPtr = TkTextGetIndexFromObj(interp, textPtr, objv[2]);
	    if (indexPtr == NULL) {
		result = TCL_ERROR;
		goto done;
	    }
	    if (TkTextDLineInfo(textPtr, indexPtr, &x, &y, &width, 
				&height, &base) == 0) {
		char buf[TCL_INTEGER_SPACE * 5];
		
		sprintf(buf, "%d %d %d %d %d", x, y, width, height, base);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	    }
	    break;
	}
	case TEXT_DUMP: {
	    result = TextDumpCmd(textPtr, interp, objc, objv);
	    break;
	}
	case TEXT_EDIT: {
	    result = TextEditCmd(textPtr, interp, objc, objv);
	    break;
	}
	case TEXT_GET: {
	    Tcl_Obj *objPtr = NULL;
	    int i, found = 0;

	    if (objc < 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "index1 ?index2 ...?");
		result = TCL_ERROR;
		goto done;
	    }
	    for (i = 2; i < objc; i += 2) {
		CONST TkTextIndex *index1Ptr, *index2Ptr;
		TkTextIndex index2;
		
		index1Ptr = TkTextGetIndexFromObj(interp, textPtr, objv[i]);
		if (index1Ptr == NULL) {
		    if (objPtr) {
			Tcl_DecrRefCount(objPtr);
		    }
		    result = TCL_ERROR;
		    goto done;
		}
		if (i+1 == objc) {
		    TkTextIndexForwChars(index1Ptr, 1, &index2);
		    index2Ptr = &index2;
		} else {
		    index2Ptr = TkTextGetIndexFromObj(interp, textPtr, 
						      objv[i+1]);
		    if (index2Ptr == NULL) {
			if (objPtr) {
			    Tcl_DecrRefCount(objPtr);
			}
			result = TCL_ERROR;
			goto done;
		    }
		}
		if (TkTextIndexCmp(index1Ptr, index2Ptr) < 0) {
		    /* 
		     * We want to move the text we get from the window
		     * into the result, but since this could in principle
		     * be a megabyte or more, we want to do it
		     * efficiently!
		     */
		    Tcl_Obj *get = TextGetText(index1Ptr, index2Ptr);
		    found++;
		    if (found == 1) {
			Tcl_SetObjResult(interp, get);
		    } else {
			if (found == 2) {
			    /*
			     * Move the first item we put into the result into
			     * the first element of the list object.
			     */
			    objPtr = Tcl_NewObj();
			    Tcl_ListObjAppendElement(NULL, objPtr,
						     Tcl_GetObjResult(interp));
			}
			Tcl_ListObjAppendElement(NULL, objPtr, get);
		    }
		}
	    }
	    if (found > 1) {
		Tcl_SetObjResult(interp, objPtr);
	    }
	    break;
	}
	case TEXT_IMAGE: {
	    result = TkTextImageCmd(textPtr, interp, objc, objv);
	    break;
	}
	case TEXT_INDEX: {
	    CONST TkTextIndex *indexPtr;

	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "index");
		result = TCL_ERROR;
		goto done;
	    }
	    
	    indexPtr = TkTextGetIndexFromObj(interp, textPtr, objv[2]);
	    if (indexPtr == NULL) {
		result = TCL_ERROR;
		goto done;
	    }
	    Tcl_SetObjResult(interp, TkTextNewIndexObj(textPtr, indexPtr));
	    break;
	}
	case TEXT_INSERT: {
	    CONST TkTextIndex *indexPtr;

	    if (objc < 4) {
		Tcl_WrongNumArgs(interp, 2, objv, 
				 "index chars ?tagList chars tagList ...?");
		result = TCL_ERROR;
		goto done;
	    }
	    indexPtr = TkTextGetIndexFromObj(interp, textPtr, objv[2]);
	    if (indexPtr == NULL) {
		result = TCL_ERROR;
		goto done;
	    }
	    if (textPtr->state == TK_TEXT_STATE_NORMAL) {
		TkTextIndex index1, index2;
		int j;

		index1 = *indexPtr;
		for (j = 3;  j < objc; j += 2) {
		    /*
		     * Here we rely on this call to modify index1 if
		     * it is outside the acceptable range.  In particular,
		     * if index1 is "end", it must be set to the last
		     * allowable index for insertion, otherwise 
		     * subsequent tag insertions will fail.
		     */
		    int length = InsertChars(textPtr, &index1, objv[j]);
		    if (objc > (j+1)) {
			Tcl_Obj **tagNamePtrs;
			TkTextTag **oldTagArrayPtr;
			int numTags;
			
			TkTextIndexForwBytes(&index1, length, &index2);
			oldTagArrayPtr = TkBTreeGetTags(&index1, &numTags);
			if (oldTagArrayPtr != NULL) {
			    int i;
			    for (i = 0; i < numTags; i++) {
				TkBTreeTag(&index1, &index2, 
					   oldTagArrayPtr[i], 0);
			    }
			    ckfree((char *) oldTagArrayPtr);
			}
			if (Tcl_ListObjGetElements(interp, objv[j+1], 
						   &numTags, &tagNamePtrs)
				!= TCL_OK) {
			    result = TCL_ERROR;
			    goto done;
			} else {
			    int i;
			    
			    for (i = 0; i < numTags; i++) {
				TkBTreeTag(&index1, &index2,
				  TkTextCreateTag(textPtr, 
				  Tcl_GetString(tagNamePtrs[i])), 1);
			    }
			    index1 = index2;
			}
		    }
		}
	    }
	    break;
	}
	case TEXT_MARK: {
	    result = TkTextMarkCmd(textPtr, interp, objc, objv);
	    break;
	}
	case TEXT_SCAN: {
	    result = TkTextScanCmd(textPtr, interp, objc, objv);
	    break;
	}
	case TEXT_SEARCH: {
 	    result = TextSearchCmd(textPtr, interp, objc, objv);
	    break;
	}
	case TEXT_SEE: {
	    result = TkTextSeeCmd(textPtr, interp, objc, objv);
	    break;
	}
	case TEXT_TAG: {
	    result = TkTextTagCmd(textPtr, interp, objc, objv);
	    break;
	}
	case TEXT_WINDOW: {
	    result = TkTextWindowCmd(textPtr, interp, objc, objv);
	    break;
	}
	case TEXT_XVIEW: {
	    result = TkTextXviewCmd(textPtr, interp, objc, objv);
	    break;
	}
	case TEXT_YVIEW: {
	    result = TkTextYviewCmd(textPtr, interp, objc, objv);
	    break;
	}
    }
    
    done:
    Tcl_Release((ClientData) textPtr);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TextIndexSortProc --
 *
 *	This procedure is called by qsort when sorting an array of
 *	indices in *decreasing* order (last to first).
 *
 * Results:
 *	The return value is -1 if the first argument should be before
 *	the second element, 0 if it's equivalent, and 1 if it should be
 *	after the second element.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TextIndexSortProc(first, second)
    CONST VOID *first, *second;		/* Elements to be compared. */
{
    TkTextIndex *pair1 = (TkTextIndex *) first;
    TkTextIndex *pair2 = (TkTextIndex *) second;
    int cmp = TkTextIndexCmp(&pair1[1], &pair2[1]);

    if (cmp == 0) {
	/*
	 * If the first indices were equal, we want the second index of the
	 * pair also to be the greater.  Use pointer magic to access the
	 * second index pair.
	 */
	cmp = TkTextIndexCmp(&pair1[0], &pair2[0]);
    }
    if (cmp > 0) {
	return -1;
    } else if (cmp < 0) {
	return 1;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyText --
 *
 *	This procedure is invoked by Tcl_EventuallyFree or Tcl_Release
 *	to clean up the internal structure of a text at a safe time
 *	(when no-one is using it anymore).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Everything associated with the text is freed up.
 *
 *----------------------------------------------------------------------
 */

static void
DestroyText(memPtr)
    char *memPtr;		/* Info about text widget. */
{
    register TkText *textPtr = (TkText *) memPtr;
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    TkTextTag *tagPtr;

    /*
     * Free up all the stuff that requires special handling.  We have
     * already called let Tk_FreeConfigOptions to handle all the standard
     * option-related stuff (and so none of that exists when we are
     * called).  Special note: free up display-related information before
     * deleting the B-tree, since display-related stuff may refer to
     * stuff in the B-tree.
     */

    TkTextFreeDInfo(textPtr);
    TkBTreeDestroy(textPtr->tree);
    for (hPtr = Tcl_FirstHashEntry(&textPtr->tagTable, &search);
	    hPtr != NULL; hPtr = Tcl_NextHashEntry(&search)) {
	tagPtr = (TkTextTag *) Tcl_GetHashValue(hPtr);
	TkTextFreeTag(textPtr, tagPtr);
    }
    Tcl_DeleteHashTable(&textPtr->tagTable);
    for (hPtr = Tcl_FirstHashEntry(&textPtr->markTable, &search);
	    hPtr != NULL; hPtr = Tcl_NextHashEntry(&search)) {
	ckfree((char *) Tcl_GetHashValue(hPtr));
    }
    Tcl_DeleteHashTable(&textPtr->markTable);
    if (textPtr->tabArrayPtr != NULL) {
	ckfree((char *) textPtr->tabArrayPtr);
    }
    if (textPtr->insertBlinkHandler != NULL) {
	Tcl_DeleteTimerHandler(textPtr->insertBlinkHandler);
    }
    if (textPtr->bindingTable != NULL) {
	Tk_DeleteBindingTable(textPtr->bindingTable);
    }
    TkUndoFreeStack(textPtr->undoStack);

    textPtr->tkwin = NULL;
    textPtr->refCount--;
    Tcl_DeleteCommandFromToken(textPtr->interp,
	    textPtr->widgetCmd);
    if (textPtr->refCount == 0) {
        ckfree((char *) textPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureText --
 *
 *	This procedure is called to process an objv/objc list, plus
 *	the Tk option database, in order to configure (or
 *	reconfigure) a text widget.
 *
 * Results:
 *	The return value is a standard Tcl result.  If TCL_ERROR is
 *	returned, then the interp's result contains an error message.
 *
 * Side effects:
 *	Configuration information, such as text string, colors, font,
 *	etc. get set for textPtr;  old resources get freed, if there
 *	were any.
 *
 *----------------------------------------------------------------------
 */

static int
ConfigureText(interp, textPtr, objc, objv)
    Tcl_Interp *interp;		/* Used for error reporting. */
    register TkText *textPtr;	/* Information about widget;  may or may
				 * not already have values for some fields. */
    int objc;			/* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. */
{
    Tk_SavedOptions savedOptions;
    int oldExport = textPtr->exportSelection;

    if (Tk_SetOptions(interp, (char*)textPtr, textPtr->optionTable,
	    objc, objv, textPtr->tkwin, &savedOptions, NULL) != TCL_OK) {
	return TCL_ERROR;
    }

    TkUndoSetDepth(textPtr->undoStack, textPtr->maxUndo);

    /*
     * A few other options also need special processing, such as parsing
     * the geometry and setting the background from a 3-D border.
     */

    Tk_SetBackgroundFromBorder(textPtr->tkwin, textPtr->border);

    /*
     * Don't allow negative spacings.
     */

    if (textPtr->spacing1 < 0) {
	textPtr->spacing1 = 0;
    }
    if (textPtr->spacing2 < 0) {
	textPtr->spacing2 = 0;
    }
    if (textPtr->spacing3 < 0) {
	textPtr->spacing3 = 0;
    }

    /*
     * Parse tab stops.
     */

    if (textPtr->tabArrayPtr != NULL) {
	ckfree((char *) textPtr->tabArrayPtr);
	textPtr->tabArrayPtr = NULL;
    }
    if (textPtr->tabOptionPtr != NULL) {
	textPtr->tabArrayPtr = TkTextGetTabs(interp, textPtr->tkwin,
		textPtr->tabOptionPtr);
	if (textPtr->tabArrayPtr == NULL) {
	    Tcl_AddErrorInfo(interp,"\n    (while processing -tabs option)");
	    Tk_RestoreSavedOptions(&savedOptions);
	    return TCL_ERROR;
	}
    }

    /*
     * Make sure that configuration options are properly mirrored
     * between the widget record and the "sel" tags.  NOTE: we don't
     * have to free up information during the mirroring;  old
     * information was freed when it was replaced in the widget
     * record.
     */

    textPtr->selTagPtr->border = textPtr->selBorder;
    if (textPtr->selTagPtr->borderWidthPtr != textPtr->selBorderWidthPtr) {
	textPtr->selTagPtr->borderWidthPtr = textPtr->selBorderWidthPtr;
	textPtr->selTagPtr->borderWidth = textPtr->selBorderWidth;
    }
    textPtr->selTagPtr->fgColor = textPtr->selFgColorPtr;
    textPtr->selTagPtr->affectsDisplay = 0;
    if ((textPtr->selTagPtr->border != NULL)
	    || (textPtr->selTagPtr->borderWidth != 0)
	    || (textPtr->selTagPtr->reliefString != NULL)
	    || (textPtr->selTagPtr->bgStipple != None)
	    || (textPtr->selTagPtr->fgColor != NULL)
	    || (textPtr->selTagPtr->tkfont != None)
	    || (textPtr->selTagPtr->fgStipple != None)
	    || (textPtr->selTagPtr->justifyString != NULL)
	    || (textPtr->selTagPtr->lMargin1String != NULL)
	    || (textPtr->selTagPtr->lMargin2String != NULL)
	    || (textPtr->selTagPtr->offsetString != NULL)
	    || (textPtr->selTagPtr->overstrikeString != NULL)
	    || (textPtr->selTagPtr->rMarginString != NULL)
	    || (textPtr->selTagPtr->spacing1String != NULL)
	    || (textPtr->selTagPtr->spacing2String != NULL)
	    || (textPtr->selTagPtr->spacing3String != NULL)
	    || (textPtr->selTagPtr->tabStringPtr != NULL)
	    || (textPtr->selTagPtr->underlineString != NULL)
	    || (textPtr->selTagPtr->elideString != NULL)
	    || (textPtr->selTagPtr->wrapMode != TEXT_WRAPMODE_NULL)) {
	textPtr->selTagPtr->affectsDisplay = 1;
    }
    TkTextRedrawTag(textPtr, (TkTextIndex *) NULL, (TkTextIndex *) NULL,
	    textPtr->selTagPtr, 1);

    /*
     * Claim the selection if we've suddenly started exporting it and there
     * are tagged characters.
     */

    if (textPtr->exportSelection && (!oldExport)) {
	TkTextSearch search;
	TkTextIndex first, last;

	TkTextMakeByteIndex(textPtr->tree, 0, 0, &first);
	TkTextMakeByteIndex(textPtr->tree,
		TkBTreeNumLines(textPtr->tree), 0, &last);
	TkBTreeStartSearch(&first, &last, textPtr->selTagPtr, &search);
	if (TkBTreeCharTagged(&first, textPtr->selTagPtr)
		|| TkBTreeNextTag(&search)) {
	    Tk_OwnSelection(textPtr->tkwin, XA_PRIMARY, TkTextLostSelection,
		    (ClientData) textPtr);
	    textPtr->flags |= GOT_SELECTION;
	}
    }

    /*
     * Account for state changes that would reenable blinking cursor state.
     */

    if (textPtr->flags & GOT_FOCUS) {
	Tcl_DeleteTimerHandler(textPtr->insertBlinkHandler);
	textPtr->insertBlinkHandler = (Tcl_TimerToken) NULL;
	TextBlinkProc((ClientData) textPtr);
    }

    /*
     * Register the desired geometry for the window, and arrange for
     * the window to be redisplayed.
     */

    if (textPtr->width <= 0) {
	textPtr->width = 1;
    }
    if (textPtr->height <= 0) {
	textPtr->height = 1;
    }
    Tk_FreeSavedOptions(&savedOptions);
    TextWorldChanged((ClientData) textPtr);
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * TextWorldChanged --
 *
 *      This procedure is called when the world has changed in some
 *      way and the widget needs to recompute all its graphics contexts
 *	and determine its new geometry.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Configures all tags in the Text with a empty objc/objv, for
 *	the side effect of causing all the items to recompute their
 *	geometry and to be redisplayed.
 *
 *---------------------------------------------------------------------------
 */
 
static void
TextWorldChanged(instanceData)
    ClientData instanceData;	/* Information about widget. */
{
    TkText *textPtr;
    Tk_FontMetrics fm;

    textPtr = (TkText *) instanceData;

    textPtr->charWidth = Tk_TextWidth(textPtr->tkfont, "0", 1);
    if (textPtr->charWidth <= 0) {
	textPtr->charWidth = 1;
    }
    Tk_GetFontMetrics(textPtr->tkfont, &fm);
    Tk_GeometryRequest(textPtr->tkwin,
	    textPtr->width * textPtr->charWidth + 2*textPtr->borderWidth
		    + 2*textPtr->padX + 2*textPtr->highlightWidth,
	    textPtr->height * (fm.linespace + textPtr->spacing1
		    + textPtr->spacing3) + 2*textPtr->borderWidth
		    + 2*textPtr->padY + 2*textPtr->highlightWidth);
    Tk_SetInternalBorder(textPtr->tkwin,
	    textPtr->borderWidth + textPtr->highlightWidth);
    if (textPtr->setGrid) {
	Tk_SetGrid(textPtr->tkwin, textPtr->width, textPtr->height,
		textPtr->charWidth, fm.linespace);
    } else {
	Tk_UnsetGrid(textPtr->tkwin);
    }

    TkTextRelayoutWindow(textPtr);
}

/*
 *--------------------------------------------------------------
 *
 * TextEventProc --
 *
 *	This procedure is invoked by the Tk dispatcher on
 *	structure changes to a text.  For texts with 3D
 *	borders, this procedure is also invoked for exposures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	When the window gets deleted, internal structures get
 *	cleaned up.  When it gets exposed, it is redisplayed.
 *
 *--------------------------------------------------------------
 */

static void
TextEventProc(clientData, eventPtr)
    ClientData clientData;	/* Information about window. */
    register XEvent *eventPtr;	/* Information about event. */
{
    register TkText *textPtr = (TkText *) clientData;
    TkTextIndex index, index2;

    if (eventPtr->type == Expose) {
	TkTextRedrawRegion(textPtr, eventPtr->xexpose.x,
		eventPtr->xexpose.y, eventPtr->xexpose.width,
		eventPtr->xexpose.height);
    } else if (eventPtr->type == ConfigureNotify) {
	if ((textPtr->prevWidth != Tk_Width(textPtr->tkwin))
		|| (textPtr->prevHeight != Tk_Height(textPtr->tkwin))) {
	    TkTextRelayoutWindow(textPtr);
	    textPtr->prevWidth = Tk_Width(textPtr->tkwin);
	    textPtr->prevHeight = Tk_Height(textPtr->tkwin);
	}
    } else if (eventPtr->type == DestroyNotify) {
	/*
	 * NOTE: we must zero out selBorder, selBorderWidthPtr and
	 * selFgColorPtr: they are duplicates of information in the
	 * "sel" tag, which will be freed up when we delete all tags.
	 * Hence we don't want the automatic config options freeing
	 * process to delete them as well.
	 */

	textPtr->selBorder = NULL;
	textPtr->selBorderWidthPtr = NULL;
	textPtr->selBorderWidth = 0;
	textPtr->selFgColorPtr = NULL;
	if (textPtr->setGrid) {
	    Tk_UnsetGrid(textPtr->tkwin);
	    textPtr->setGrid = 0;
	}
	if (!(textPtr->flags & OPTIONS_FREED)) {
	    Tk_FreeConfigOptions((char *) textPtr, textPtr->optionTable,
				 textPtr->tkwin);
	    textPtr->flags |= OPTIONS_FREED;
	}
	textPtr->flags |= DESTROYED;
	
	/* 
	 * We don't delete the associated Tcl command yet, because
	 * that will cause textPtr->tkWin to be nulled out, and that
	 * is needed inside DestroyText to clean up certain tags
	 * which might have been created (e.g. in the text widget
	 * styles demo).
	 */
	Tcl_EventuallyFree((ClientData) textPtr, DestroyText);
    } else if ((eventPtr->type == FocusIn) || (eventPtr->type == FocusOut)) {
	if (eventPtr->xfocus.detail != NotifyInferior) {
	    Tcl_DeleteTimerHandler(textPtr->insertBlinkHandler);
	    if (eventPtr->type == FocusIn) {
		textPtr->flags |= GOT_FOCUS | INSERT_ON;
		if (textPtr->insertOffTime != 0) {
		    textPtr->insertBlinkHandler = Tcl_CreateTimerHandler(
			    textPtr->insertOnTime, TextBlinkProc,
			    (ClientData) textPtr);
		}
	    } else {
		textPtr->flags &= ~(GOT_FOCUS | INSERT_ON);
		textPtr->insertBlinkHandler = (Tcl_TimerToken) NULL;
	    }
#ifndef ALWAYS_SHOW_SELECTION
	    TkTextRedrawTag(textPtr, NULL, NULL, textPtr->selTagPtr, 1);
#endif
	    TkTextMarkSegToIndex(textPtr, textPtr->insertMarkPtr, &index);
	    TkTextIndexForwChars(&index, 1, &index2);
	    TkTextChanged(textPtr, &index, &index2);
	    if (textPtr->highlightWidth > 0) {
		TkTextRedrawRegion(textPtr, 0, 0, textPtr->highlightWidth,
			textPtr->highlightWidth);
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TextCmdDeletedProc --
 *
 *	This procedure is invoked when a widget command is deleted.  If
 *	the widget isn't already in the process of being destroyed,
 *	this command destroys it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The widget is destroyed.
 *
 *----------------------------------------------------------------------
 */

static void
TextCmdDeletedProc(clientData)
    ClientData clientData;	/* Pointer to widget record for widget. */
{
    TkText *textPtr = (TkText *) clientData;
    Tk_Window tkwin = textPtr->tkwin;

    /*
     * This procedure could be invoked either because the window was
     * destroyed and the command was then deleted (in which this flag is
     * already set) or because the command was deleted, and then this
     * procedure destroys the widget.
     */

    if (!(textPtr->flags & DESTROYED)) {
	if (textPtr->setGrid) {
	    Tk_UnsetGrid(textPtr->tkwin);
	    textPtr->setGrid = 0;
	}
	textPtr->flags |= DESTROYED;
	Tk_DestroyWindow(tkwin);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * InsertChars --
 *
 *	This procedure implements most of the functionality of the
 *	"insert" widget command.
 *
 * Results:
 *	The length of the inserted string.
 *
 * Side effects:
 *	The characters in "stringPtr" get added to the text just before
 *	the character indicated by "indexPtr".
 *
 *----------------------------------------------------------------------
 */

static int
InsertChars(textPtr, indexPtr, stringPtr)
    TkText *textPtr;		/* Overall information about text widget. */
    TkTextIndex *indexPtr;      /* Where to insert new characters.  May be
				 * modified if the index is not valid
				 * for insertion (e.g. if at "end"). */
    Tcl_Obj *stringPtr;		/* Null-terminated string containing new
				 * information to add to text. */
{
    int lineIndex, resetView, offset, length;
    
    CONST char *string = Tcl_GetStringFromObj(stringPtr, &length);
    
    /*
     * Don't allow insertions on the last (dummy) line of the text.
     * This is the only place in this function where the indexPtr is
     * modified.
     */

    lineIndex = TkBTreeLineIndex(indexPtr->linePtr);
    if (lineIndex == TkBTreeNumLines(textPtr->tree)) {
	lineIndex--;
	TkTextMakeByteIndex(textPtr->tree, lineIndex, 1000000, indexPtr);
    }
    
    /*
     * Notify the display module that lines are about to change, then do
     * the insertion.  If the insertion occurs on the top line of the
     * widget (textPtr->topIndex), then we have to recompute topIndex
     * after the insertion, since the insertion could invalidate it.
     */

    resetView = offset = 0;
    if (indexPtr->linePtr == textPtr->topIndex.linePtr) {
	resetView = 1;
	offset = textPtr->topIndex.byteIndex;
	if (offset > indexPtr->byteIndex) {
	    offset += length;
	}
    }
    TkTextChanged(textPtr, indexPtr, indexPtr);
    textPtr->stateEpoch ++;
    TkBTreeInsertChars(indexPtr, string);

    /*
     * Push the insertion on the undo stack
     */

    if (textPtr->undo) {
        TkTextIndex toIndex;
	
        if (textPtr->autoSeparators &&
            textPtr->lastEditMode != TK_TEXT_EDIT_INSERT) {
            TkUndoInsertUndoSeparator(textPtr->undoStack);
        }
        
        textPtr->lastEditMode = TK_TEXT_EDIT_INSERT;

	TkTextIndexForwBytes(indexPtr, length, &toIndex);
	TextPushUndoAction(textPtr, stringPtr, 1, indexPtr, &toIndex);
    }
    
    UpdateDirtyFlag(textPtr);

    if (resetView) {
	TkTextIndex newTop;
	TkTextMakeByteIndex(textPtr->tree, lineIndex, 0, &newTop);
	TkTextIndexForwBytes(&newTop, offset, &newTop);
	TkTextSetYView(textPtr, &newTop, 0);
    }

    /*
     * Invalidate any selection retrievals in progress.
     */

    textPtr->abortSelections = 1;
    
    /* For convenience, return the length of the string */
    return length;
}

/*
 *----------------------------------------------------------------------
 *
 * TextPushUndoAction --
 *
 *	Shared by insert and delete actions.  Stores the appropriate
 *	scripts into our undo stack.  We will add a single refCount to
 *	the 'undoString' object, so, if it previously had a refCount of
 *	zero, the caller should not free it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Items pushed onto stack.
 *
 *----------------------------------------------------------------------
 */

static void
TextPushUndoAction (textPtr, undoString, insert, index1Ptr, index2Ptr)
    TkText *textPtr;		 /* Overall information about text widget. */
    Tcl_Obj *undoString;	 /* New text */
    int insert;                  /* 1 if insert, else delete */
    CONST TkTextIndex *index1Ptr;/* Index describing first location */
    CONST TkTextIndex *index2Ptr;/* Index describing second location */
{
    /* Create the helpers */
    Tcl_Obj *cmdNameObj = Tcl_NewObj();
    Tcl_Obj *seeInsertObj = Tcl_NewObj();
    Tcl_Obj *markSet1InsertObj = Tcl_NewObj();
    Tcl_Obj *markSet2InsertObj = Tcl_NewObj();
    Tcl_Obj *insertCmdObj = Tcl_NewObj();
    Tcl_Obj *deleteCmdObj = Tcl_NewObj();
    
    Tcl_Obj *insertCmd = Tcl_NewObj();
    Tcl_Obj *deleteCmd = Tcl_NewObj();
    
    /* Get the index positions */
    Tcl_Obj *index1Obj = TkTextNewIndexObj(textPtr, index1Ptr);
    Tcl_Obj *index2Obj = TkTextNewIndexObj(textPtr, index2Ptr);

    /* Get the fully qualified name */
    Tcl_GetCommandFullName(textPtr->interp, textPtr->widgetCmd, cmdNameObj);

    /* These need refCounts, because they are used more than once below */
    Tcl_IncrRefCount(cmdNameObj);
    Tcl_IncrRefCount(seeInsertObj);
    Tcl_IncrRefCount(index1Obj);
    Tcl_IncrRefCount(index2Obj);

    Tcl_ListObjAppendElement(NULL, seeInsertObj, cmdNameObj);
    Tcl_ListObjAppendElement(NULL, seeInsertObj, Tcl_NewStringObj("see",3));
    Tcl_ListObjAppendElement(NULL, seeInsertObj, Tcl_NewStringObj("insert",6));

    Tcl_ListObjAppendElement(NULL, markSet1InsertObj, cmdNameObj);
    Tcl_ListObjAppendElement(NULL, markSet1InsertObj, 
			     Tcl_NewStringObj("mark",4));
    Tcl_ListObjAppendElement(NULL, markSet1InsertObj, 
			     Tcl_NewStringObj("set",3));
    Tcl_ListObjAppendElement(NULL, markSet1InsertObj, 
			     Tcl_NewStringObj("insert",6));
    markSet2InsertObj = Tcl_DuplicateObj(markSet1InsertObj);
    Tcl_ListObjAppendElement(NULL, markSet1InsertObj, index1Obj);
    Tcl_ListObjAppendElement(NULL, markSet2InsertObj, index2Obj);

    Tcl_ListObjAppendElement(NULL, insertCmdObj, cmdNameObj);
    Tcl_ListObjAppendElement(NULL, insertCmdObj, Tcl_NewStringObj("insert",6));
    Tcl_ListObjAppendElement(NULL, insertCmdObj, index1Obj);
    /* Only use of 'undoString' */
    Tcl_ListObjAppendElement(NULL, insertCmdObj, undoString);

    Tcl_ListObjAppendElement(NULL, deleteCmdObj, cmdNameObj);
    Tcl_ListObjAppendElement(NULL, deleteCmdObj, Tcl_NewStringObj("delete",6));
    Tcl_ListObjAppendElement(NULL, deleteCmdObj, index1Obj);
    Tcl_ListObjAppendElement(NULL, deleteCmdObj, index2Obj);

    Tcl_ListObjAppendElement(NULL, insertCmd, insertCmdObj);
    Tcl_ListObjAppendElement(NULL, insertCmd, markSet2InsertObj);
    Tcl_ListObjAppendElement(NULL, insertCmd, seeInsertObj);
    Tcl_ListObjAppendElement(NULL, deleteCmd, deleteCmdObj);
    Tcl_ListObjAppendElement(NULL, deleteCmd, markSet1InsertObj);
    Tcl_ListObjAppendElement(NULL, deleteCmd, seeInsertObj);
    
    Tcl_DecrRefCount(cmdNameObj);
    Tcl_DecrRefCount(seeInsertObj);
    Tcl_DecrRefCount(index1Obj);
    Tcl_DecrRefCount(index2Obj);

    /* 
     * Depending whether the action is to insert or delete, we provide
     * the appropriate second and third arguments to TkUndoPushAction.
     * (The first is the 'actionCommand', and the second the
     * 'revertCommand').  The final '1' says we are providing a list
     * of scripts to execute rather than a single script.
     */
    if (insert) {
	TkUndoPushAction(textPtr->undoStack, insertCmd, deleteCmd, 1);
    } else {
	TkUndoPushAction(textPtr->undoStack, deleteCmd, insertCmd, 1);
    }
    
}

/*
 *----------------------------------------------------------------------
 *
 * DeleteChars --
 *
 *	This procedure implements most of the functionality of the
 *	"delete" widget command.
 *
 * Results:
 *	Returns a standard Tcl result, and leaves an error message
 *	in textPtr->interp if there is an error.
 *
 * Side effects:
 *	Characters get deleted from the text.
 *
 *----------------------------------------------------------------------
 */

static int
DeleteChars(textPtr, index1Obj, index2Obj, indexPtr1, indexPtr2)
    TkText *textPtr;		 /* Overall information about text widget. */
    Tcl_Obj *index1Obj;	         /* Object describing location of first
				  * character to delete. */
    Tcl_Obj *index2Obj;	         /* Object describing location of last
				  * character to delete.  NULL means just
				  * delete the one character given by
				  * index1Obj. */
    CONST TkTextIndex *indexPtr1;/* Index describing location of first
				  * character to delete. */
    CONST TkTextIndex *indexPtr2;/* Index describing location of last
				  * character to delete.  NULL means just
				  * delete the one character given by
				  * indexPtr1. */
{
    int line1, line2, line, byteIndex, resetView;
    TkTextIndex index1, index2;

    /*
     * Parse the starting and stopping indices.
     */

    if (index1Obj != NULL) {
	indexPtr1 = TkTextGetIndexFromObj(textPtr->interp, textPtr, index1Obj);
	if (indexPtr1 == NULL) {
	    return TCL_ERROR;
	}
	index1 = *indexPtr1;
	if (index2Obj != NULL) {
	    indexPtr2 = TkTextGetIndexFromObj(textPtr->interp, textPtr, 
					      index2Obj);
	    if (indexPtr2 == NULL) {
		return TCL_ERROR;
	    }
	    index2 = *indexPtr2;
	} else {
	    index2 = index1;
	    TkTextIndexForwChars(&index2, 1, &index2);
	}
    } else {
	index1 = *indexPtr1;
	if (indexPtr2 != NULL) {
	    index2 = *indexPtr2;
	} else {
	    index2 = index1;
	    TkTextIndexForwChars(&index2, 1, &index2);
	}
    }

    /*
     * Make sure there's really something to delete.
     */

    if (TkTextIndexCmp(&index1, &index2) >= 0) {
	return TCL_OK;
    }

    /*
     * The code below is ugly, but it's needed to make sure there
     * is always a dummy empty line at the end of the text.  If the
     * final newline of the file (just before the dummy line) is being
     * deleted, then back up index to just before the newline.  If
     * there is a newline just before the first character being deleted,
     * then back up the first index too, so that an even number of lines
     * gets deleted.  Furthermore, remove any tags that are present on
     * the newline that isn't going to be deleted after all (this simulates
     * deleting the newline and then adding a "clean" one back again).
     */

    line1 = TkBTreeLineIndex(index1.linePtr);
    line2 = TkBTreeLineIndex(index2.linePtr);
    if (line2 == TkBTreeNumLines(textPtr->tree)) {
	TkTextTag **arrayPtr;
	int arraySize, i;
	TkTextIndex oldIndex2;

	oldIndex2 = index2;
	TkTextIndexBackChars(&oldIndex2, 1, &index2);
	line2--;
	if ((index1.byteIndex == 0) && (line1 != 0)) {
	    TkTextIndexBackChars(&index1, 1, &index1);
	    line1--;
	}
	arrayPtr = TkBTreeGetTags(&index2, &arraySize);
	if (arrayPtr != NULL) {
	    for (i = 0; i < arraySize; i++) {
		TkBTreeTag(&index2, &oldIndex2, arrayPtr[i], 0);
	    }
	    ckfree((char *) arrayPtr);
	}
    }

    /*
     * Tell the display what's about to happen so it can discard
     * obsolete display information, then do the deletion.  Also,
     * if the deletion involves the top line on the screen, then
     * we have to reset the view (the deletion will invalidate
     * textPtr->topIndex).  Compute what the new first character
     * will be, then do the deletion, then reset the view.
     */

    TkTextChanged(textPtr, &index1, &index2);
    resetView = 0;
    line = 0;
    byteIndex = 0;
    if (TkTextIndexCmp(&index2, &textPtr->topIndex) >= 0) {
	if (TkTextIndexCmp(&index1, &textPtr->topIndex) <= 0) {
	    /*
	     * Deletion range straddles topIndex: use the beginning
	     * of the range as the new topIndex.
	     */

	    resetView = 1;
	    line = line1;
	    byteIndex = index1.byteIndex;
	} else if (index1.linePtr == textPtr->topIndex.linePtr) {
	    /*
	     * Deletion range starts on top line but after topIndex.
	     * Use the current topIndex as the new one.
	     */

	    resetView = 1;
	    line = line1;
	    byteIndex = textPtr->topIndex.byteIndex;
	}
    } else if (index2.linePtr == textPtr->topIndex.linePtr) {
	/*
	 * Deletion range ends on top line but before topIndex.
	 * Figure out what will be the new character index for
	 * the character currently pointed to by topIndex.
	 */

	resetView = 1;
	line = line2;
	byteIndex = textPtr->topIndex.byteIndex;
	if (index1.linePtr != index2.linePtr) {
	    byteIndex -= index2.byteIndex;
	} else {
	    byteIndex -= (index2.byteIndex - index1.byteIndex);
	}
    }

    /*
     * Push the deletion on the undo stack
     */

    if (textPtr->undo) {
	Tcl_Obj *get;
	
	if (textPtr->autoSeparators
		&& (textPtr->lastEditMode != TK_TEXT_EDIT_DELETE)) {
	   TkUndoInsertUndoSeparator(textPtr->undoStack);
	}

	textPtr->lastEditMode = TK_TEXT_EDIT_DELETE;

	get = TextGetText(&index1, &index2);
	TextPushUndoAction(textPtr, get, 0, &index1, &index2);
    }
    UpdateDirtyFlag(textPtr);

    textPtr->stateEpoch ++;
    TkBTreeDeleteChars(&index1, &index2);
    if (resetView) {
	TkTextMakeByteIndex(textPtr->tree, line, byteIndex, &index1);
	TkTextSetYView(textPtr, &index1, 0);
    }

    /*
     * Invalidate any selection retrievals in progress.
     */

    textPtr->abortSelections = 1;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TextFetchSelection --
 *
 *	This procedure is called back by Tk when the selection is
 *	requested by someone.  It returns part or all of the selection
 *	in a buffer provided by the caller.
 *
 * Results:
 *	The return value is the number of non-NULL bytes stored
 *	at buffer.  Buffer is filled (or partially filled) with a
 *	NULL-terminated string containing part or all of the selection,
 *	as given by offset and maxBytes.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TextFetchSelection(clientData, offset, buffer, maxBytes)
    ClientData clientData;		/* Information about text widget. */
    int offset;				/* Offset within selection of first
					 * character to be returned. */
    char *buffer;			/* Location in which to place
					 * selection. */
    int maxBytes;			/* Maximum number of bytes to place
					 * at buffer, not including terminating
					 * NULL character. */
{
    register TkText *textPtr = (TkText *) clientData;
    TkTextIndex eof;
    int count, chunkSize, offsetInSeg;
    TkTextSearch search;
    TkTextSegment *segPtr;

    if (!textPtr->exportSelection) {
	return -1;
    }

    /*
     * Find the beginning of the next range of selected text.  Note:  if
     * the selection is being retrieved in multiple pieces (offset != 0)
     * and some modification has been made to the text that affects the
     * selection then reject the selection request (make 'em start over
     * again).
     */

    if (offset == 0) {
	TkTextMakeByteIndex(textPtr->tree, 0, 0, &textPtr->selIndex);
	textPtr->abortSelections = 0;
    } else if (textPtr->abortSelections) {
	return 0;
    }
    TkTextMakeByteIndex(textPtr->tree, TkBTreeNumLines(textPtr->tree), 0, &eof);
    TkBTreeStartSearch(&textPtr->selIndex, &eof, textPtr->selTagPtr, &search);
    if (!TkBTreeCharTagged(&textPtr->selIndex, textPtr->selTagPtr)) {
	if (!TkBTreeNextTag(&search)) {
	    if (offset == 0) {
		return -1;
	    } else {
		return 0;
	    }
	}
	textPtr->selIndex = search.curIndex;
    }

    /*
     * Each iteration through the outer loop below scans one selected range.
     * Each iteration through the inner loop scans one segment in the
     * selected range.
     */

    count = 0;
    while (1) {
	/*
	 * Find the end of the current range of selected text.
	 */

	if (!TkBTreeNextTag(&search)) {
	    panic("TextFetchSelection couldn't find end of range");
	}

	/*
	 * Copy information from character segments into the buffer
	 * until either we run out of space in the buffer or we get
	 * to the end of this range of text.
	 */

	while (1) {
	    if (maxBytes == 0) {
		goto fetchDone;
	    }
	    segPtr = TkTextIndexToSeg(&textPtr->selIndex, &offsetInSeg);
	    chunkSize = segPtr->size - offsetInSeg;
	    if (chunkSize > maxBytes) {
		chunkSize = maxBytes;
	    }
	    if (textPtr->selIndex.linePtr == search.curIndex.linePtr) {
		int leftInRange;

		leftInRange = search.curIndex.byteIndex
			- textPtr->selIndex.byteIndex;
		if (leftInRange < chunkSize) {
		    chunkSize = leftInRange;
		    if (chunkSize <= 0) {
			break;
		    }
		}
	    }
	    if ((segPtr->typePtr == &tkTextCharType)
		    && !TkTextIsElided(textPtr, &textPtr->selIndex)) {
		memcpy((VOID *) buffer, (VOID *) (segPtr->body.chars
			+ offsetInSeg), (size_t) chunkSize);
		buffer += chunkSize;
		maxBytes -= chunkSize;
		count += chunkSize;
	    }
	    TkTextIndexForwBytes(&textPtr->selIndex, chunkSize,
		    &textPtr->selIndex);
	}

	/*
	 * Find the beginning of the next range of selected text.
	 */

	if (!TkBTreeNextTag(&search)) {
	    break;
	}
	textPtr->selIndex = search.curIndex;
    }

    fetchDone:
    *buffer = 0;
    return count;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextLostSelection --
 *
 *	This procedure is called back by Tk when the selection is
 *	grabbed away from a text widget.  On Windows and Mac systems, we
 *	want to remember the selection for the next time the focus
 *	enters the window.  On Unix, just remove the "sel" tag from
 *	everything in the widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The "sel" tag is cleared from the window.
 *
 *----------------------------------------------------------------------
 */

void
TkTextLostSelection(clientData)
    ClientData clientData;		/* Information about text widget. */
{
    register TkText *textPtr = (TkText *) clientData;
    XEvent event;
#ifdef ALWAYS_SHOW_SELECTION
    TkTextIndex start, end;

    if (!textPtr->exportSelection) {
	return;
    }

    /*
     * On Windows and Mac systems, we want to remember the selection
     * for the next time the focus enters the window.  On Unix, 
     * just remove the "sel" tag from everything in the widget.
     */

    TkTextMakeByteIndex(textPtr->tree, 0, 0, &start);
    TkTextMakeByteIndex(textPtr->tree, TkBTreeNumLines(textPtr->tree), 0, &end);
    TkTextRedrawTag(textPtr, &start, &end, textPtr->selTagPtr, 1);
    TkBTreeTag(&start, &end, textPtr->selTagPtr, 0);
#endif

    /*
     * Send an event that the selection changed.  This is equivalent to
     * "event generate $textWidget <<Selection>>"
     */

    memset((VOID *) &event, 0, sizeof(event));
    event.xany.type = VirtualEvent;
    event.xany.serial = NextRequest(Tk_Display(textPtr->tkwin));
    event.xany.send_event = False;
    event.xany.window = Tk_WindowId(textPtr->tkwin);
    event.xany.display = Tk_Display(textPtr->tkwin);
    ((XVirtualEvent *) &event)->name = Tk_GetUid("Selection");
    Tk_HandleEvent(&event);

    textPtr->flags &= ~GOT_SELECTION;
}

/*
 *----------------------------------------------------------------------
 *
 * TextBlinkProc --
 *
 *	This procedure is called as a timer handler to blink the
 *	insertion cursor off and on.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The cursor gets turned on or off, redisplay gets invoked,
 *	and this procedure reschedules itself.
 *
 *----------------------------------------------------------------------
 */

static void
TextBlinkProc(clientData)
    ClientData clientData;	/* Pointer to record describing text. */
{
    register TkText *textPtr = (TkText *) clientData;
    TkTextIndex index;
    int x, y, w, h;

    if ((textPtr->state == TK_TEXT_STATE_DISABLED) ||
	    !(textPtr->flags & GOT_FOCUS) || (textPtr->insertOffTime == 0)) {
	return;
    }
    if (textPtr->flags & INSERT_ON) {
	textPtr->flags &= ~INSERT_ON;
	textPtr->insertBlinkHandler = Tcl_CreateTimerHandler(
		textPtr->insertOffTime, TextBlinkProc, (ClientData) textPtr);
    } else {
	textPtr->flags |= INSERT_ON;
	textPtr->insertBlinkHandler = Tcl_CreateTimerHandler(
		textPtr->insertOnTime, TextBlinkProc, (ClientData) textPtr);
    }
    TkTextMarkSegToIndex(textPtr, textPtr->insertMarkPtr, &index);
    if (TkTextCharBbox(textPtr, &index, &x, &y, &w, &h) == 0) {
	TkTextRedrawRegion(textPtr, x - textPtr->insertWidth / 2, y,
		textPtr->insertWidth, h);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TextSearchCmd --
 *
 *	This procedure is invoked to process the "search" widget command
 *	for text widgets.  See the user documentation for details on what
 *	it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
TextSearchCmd(textPtr, interp, objc, objv)
    TkText *textPtr;		/* Information about text widget. */
    Tcl_Interp *interp;		/* Current interpreter. */
    int objc;			/* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. */
{
    int i, argsLeft, code;
    SearchSpec searchSpec;

    static CONST char *switchStrings[] = {
	"--", "-all", "-backwards", "-count", "-elide", "-exact",
	"-forwards", "-hidden", "-nocase", "-nolinestop", "-regexp", NULL
    };
    enum SearchSwitches {
	SEARCH_END, SEARCH_ALL, SEARCH_BACK, SEARCH_COUNT, SEARCH_ELIDE,
	SEARCH_EXACT, SEARCH_FWD, SEARCH_HIDDEN, SEARCH_NOCASE,
	SEARCH_NOLINE, SEARCH_REGEXP
    };

    /* 
     * Set up the search specification, including
     * the last 4 fields which are text widget specific
     */
    searchSpec.exact = 1;
    searchSpec.noCase = 0;
    searchSpec.all = 0;
    searchSpec.backwards = 0;
    searchSpec.varPtr = NULL;
    searchSpec.countPtr = NULL;
    searchSpec.resPtr = NULL;
    searchSpec.searchElide = 0;
    searchSpec.noLineStop = 0;
    searchSpec.numLines = TkBTreeNumLines(textPtr->tree);
    searchSpec.clientData = (ClientData)textPtr;
    searchSpec.addLineProc = &TextSearchAddNextLine;
    searchSpec.foundMatchProc = &TextSearchFoundMatch;
    searchSpec.lineIndexProc = &TextSearchGetLineIndex;
    
    /*
     * Parse switches and other arguments.
     */

    for (i=2 ; i<objc ; i++) {
	int index;
	if (Tcl_GetString(objv[i])[0] != '-') {
	    break;
	}

	if (Tcl_GetIndexFromObj(interp, objv[i], switchStrings, "switch", 0,
		&index) != TCL_OK) {
	    /*
	     * Hide the -hidden option
	     */
	    Tcl_ResetResult(interp);
	    Tcl_AppendResult(interp, "bad switch \"", Tcl_GetString(objv[i]),
		    "\": must be --, -all, -backward, -count, -elide, ",
		    "-exact, -forward, -nocase, -nolinestop, or -regexp",
		    (char *) NULL);
	    return TCL_ERROR;
	}

	switch ((enum SearchSwitches) index) {
	case SEARCH_END:
	    i++;
	    goto endOfSwitchProcessing;
	case SEARCH_ALL:
	    searchSpec.all = 1;
	    break;
	case SEARCH_BACK:
	    searchSpec.backwards = 1;
	    break;
	case SEARCH_COUNT:
	    if (i >= objc-1) {
		Tcl_SetResult(interp, "no value given for \"-count\" option",
			TCL_STATIC);
		return TCL_ERROR;
	    }
	    i++;
	    /* 
	     * Assumption objv[i] isn't going to disappear on us during
	     * this procedure, which is fair.
	     */
	    searchSpec.varPtr = objv[i];
	    break;
	case SEARCH_ELIDE:
	case SEARCH_HIDDEN:
	    searchSpec.searchElide = 1;
	    break;
	case SEARCH_EXACT:
	    searchSpec.exact = 1;
	    break;
	case SEARCH_FWD:
	    searchSpec.backwards = 0;
	    break;
	case SEARCH_NOCASE:
	    searchSpec.noCase = 1;
	    break;
	case SEARCH_NOLINE:
	    searchSpec.noLineStop = 1;
	    break;
	case SEARCH_REGEXP:
	    searchSpec.exact = 0;
	    break;
	default:
	    panic("unexpected switch fallthrough");
	}
    }
  endOfSwitchProcessing:

    argsLeft = objc - (i+2);
    if ((argsLeft != 0) && (argsLeft != 1)) {
	Tcl_WrongNumArgs(interp, 2, objv,
		"?switches? pattern index ?stopIndex?");
	return TCL_ERROR;
    }

    if (searchSpec.noLineStop && searchSpec.exact) {
	Tcl_SetResult(interp, "the \"-nolinestop\" option requires the "
		"\"-regexp\" option to be present", TCL_STATIC);
	return TCL_ERROR;
    }

    /*
     * Scan through all of the lines of the text circularly, starting
     * at the given index.  'objv[i]' is the pattern which may be an
     * exact string or a regexp pattern depending on the flags set
     * above.
     */

    code = SearchPerform(interp, &searchSpec, objv[i], objv[i+1], 
	    (argsLeft == 1 ? objv[i+2] : NULL));
    if (code != TCL_OK) {
	goto cleanup;
    }

    /*
     * Set the '-count' variable, if given.
     */
    if (searchSpec.varPtr != NULL && searchSpec.countPtr != NULL) {
	Tcl_IncrRefCount(searchSpec.countPtr);
	if (Tcl_ObjSetVar2(interp, searchSpec.varPtr, NULL, 
		searchSpec.countPtr, TCL_LEAVE_ERR_MSG) == NULL) {
	    code = TCL_ERROR;
	    goto cleanup;
	}
    }

    /*
     * Set the result
     */
    if (searchSpec.resPtr != NULL) {
	Tcl_SetObjResult(interp, searchSpec.resPtr);
	searchSpec.resPtr = NULL;
    }

    cleanup:
    if (searchSpec.countPtr != NULL) {
	Tcl_DecrRefCount(searchSpec.countPtr);
    }
    if (searchSpec.resPtr != NULL) {
	Tcl_DecrRefCount(searchSpec.resPtr);
    }
    return code;
}

/*
 *----------------------------------------------------------------------
 *
 * TextSearchGetLineIndex --
 *
 *	Extract a row, text offset index position from an objPtr
 *	
 *	This means we ignore any embedded windows/images and
 *	elidden text (unless we are searching that).
 *
 * Results:
 *      Standard Tcl error code (with a message in the interpreter
 *      on error conditions).
 *      
 *	The offset placed in offsetPosPtr is a utf-8 char* byte index for
 *	exact searches, and a Unicode character index for regexp
 *	searches.
 *	
 *	The line number should start at zero (searches which wrap
 *	around assume the first line is numbered 0).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
TextSearchGetLineIndex(interp, objPtr, searchSpecPtr, linePosPtr, offsetPosPtr)
    Tcl_Interp *interp;                /* For error messages */
    Tcl_Obj *objPtr;                   /* Contains a textual index 
                                        * like "1.2" */
    SearchSpec *searchSpecPtr;         /* Contains other search 
                                        * parameters */
    int *linePosPtr;                   /* For returning the line number */
    int *offsetPosPtr;                 /* For returning the text offset in 
                                        * the line */
{
    CONST TkTextIndex *indexPtr;
    int line;
    TkText *textPtr = (TkText*)(searchSpecPtr->clientData);
    
    indexPtr = TkTextGetIndexFromObj(interp, textPtr, objPtr);
    if (indexPtr == NULL) {
	return TCL_ERROR;
    }
    
    line = TkBTreeLineIndex(indexPtr->linePtr);
    if (line >= searchSpecPtr->numLines) {
	TkTextLine *linePtr;
	line = searchSpecPtr->numLines-1;
	linePtr = TkBTreeFindLine(textPtr->tree, line);
	*offsetPosPtr = TextSearchIndexInLine(searchSpecPtr, linePtr, 
					      TkBTreeBytesInLine(linePtr));
    } else {
	*offsetPosPtr = TextSearchIndexInLine(searchSpecPtr, 
			  indexPtr->linePtr, indexPtr->byteIndex);
    }
    
    *linePosPtr = line;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TextSearchIndexInLine --
 *
 *	Find textual index of 'byteIndex' in the searchable
 *	characters of 'linePtr'.
 *	
 *	This means we ignore any embedded windows/images and
 *	elidden text (unless we are searching that).
 *
 * Results:
 *	The returned index is a utf-8 char* byte index for exact
 *	searches, and a Unicode character index for regexp searches.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TextSearchIndexInLine(searchSpecPtr, linePtr, byteIndex)
    CONST SearchSpec *searchSpecPtr; /* Search parameters */
    TkTextLine *linePtr;             /* The line we're looking at */
    int byteIndex;                   /* Index into the line */
{
    TkTextSegment *segPtr;
    TkTextIndex curIndex;
    int index, leftToScan;
    TkText *textPtr = (TkText*)(searchSpecPtr->clientData);

    index = 0;
    curIndex.tree = textPtr->tree;
    curIndex.linePtr = linePtr; curIndex.byteIndex = 0;
    for (segPtr = linePtr->segPtr, leftToScan = byteIndex;
	 leftToScan > 0; 
	 curIndex.byteIndex += segPtr->size, segPtr = segPtr->nextPtr) {
	if ((segPtr->typePtr == &tkTextCharType)
	    && (searchSpecPtr->searchElide || 
		!TkTextIsElided(textPtr, &curIndex))) {
	    if (leftToScan < segPtr->size) {
		if (searchSpecPtr->exact) {
		    index += leftToScan;
		} else {
		    index += Tcl_NumUtfChars(segPtr->body.chars, leftToScan);
		}
	    } else {
		if (searchSpecPtr->exact) {
		    index += segPtr->size;
		} else {
		    index += Tcl_NumUtfChars(segPtr->body.chars, -1);
		}
	    }
	}
	leftToScan -= segPtr->size;
    }
    return index;
}

/*
 *----------------------------------------------------------------------
 *
 * TextSearchAddNextLine --
 *
 *	Adds a line from the text widget to the object 'theLine'.
 *
 * Results:
 *	A pointer to the TkTextLine corresponding to the given line,
 *	or NULL if there was no available line.
 *	
 *	Also 'lenPtr' (if non-NULL) is filled in with the total length of
 *	'theLine' (not just what we added to it, but the length including
 *	what was already in there).  This is in bytes for an exact search
 *	and in chars for a regexp search.
 *
 * Side effects:
 *	Memory may be allocated or re-allocated for theLine's string
 *	representation.
 *
 *----------------------------------------------------------------------
 */

static ClientData 
TextSearchAddNextLine(lineNum, searchSpecPtr, theLine, lenPtr)
    int lineNum;                     /* Line we must add */
    SearchSpec *searchSpecPtr;       /* Search parameters */
    Tcl_Obj *theLine;                /* Object to append to */
    int *lenPtr;                     /* For returning the total length */
{
    TkTextLine *linePtr;
    TkTextIndex curIndex;
    TkTextSegment *segPtr;
    TkText *textPtr = (TkText*)(searchSpecPtr->clientData);
    /*
     * Extract the text from the line. 
     */

    linePtr = TkBTreeFindLine(textPtr->tree, lineNum);
    if (linePtr == NULL) {
	return NULL;
    }
    curIndex.tree = textPtr->tree;
    curIndex.linePtr = linePtr; curIndex.byteIndex = 0;
    for (segPtr = linePtr->segPtr; segPtr != NULL;
	    curIndex.byteIndex += segPtr->size, segPtr = segPtr->nextPtr) {
	if ((segPtr->typePtr != &tkTextCharType)
	  || (!searchSpecPtr->searchElide 
	      && TkTextIsElided(textPtr, &curIndex))) {
	    continue;
	}
	Tcl_AppendToObj(theLine, segPtr->body.chars, segPtr->size);
    }
    
    /*
     * If we're ignoring case, convert the line to lower case.
     * There is no need to do this for regexp searches, since
     * they handle a flag for this purpose.
     */
    if (searchSpecPtr->exact && searchSpecPtr->noCase) {
	Tcl_SetObjLength(theLine, Tcl_UtfToLower(Tcl_GetString(theLine)));
    }
    
    if (lenPtr != NULL) {
        if (searchSpecPtr->exact) {
	    Tcl_GetStringFromObj(theLine, lenPtr);
	} else {
	    *lenPtr = Tcl_GetCharLength(theLine);
	}
    }
    return (ClientData)linePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TextSearchFoundMatch --
 *
 *	Stores information from a successful search.
 *
 * Results:
 *	1 if the information was stored, 0 if the position at which
 *	the match was found actually falls outside the allowable 
 *	search region (and therefore the search is actually
 *	complete).
 *
 * Side effects:
 *	Memory may be allocated in the 'countPtr' and 'resPtr' fields
 *	of 'searchSpecPtr'.  Each of those objects will have refCount
 *	zero and must eventually be freed or stored elsewhere as 
 *	appropriate.
 *
 *----------------------------------------------------------------------
 */

static int 
TextSearchFoundMatch(lineNum, searchSpecPtr, clientData, theLine, 
		 matchOffset, matchLength)
    int lineNum;                    /* Line on which match was found */
    SearchSpec *searchSpecPtr;      /* Search parameters */
    ClientData clientData;          /* Token returned by the 'addNextLineProc',
                                     * TextSearchAddNextLine */
    Tcl_Obj *theLine;               /* Text from current line */
    int matchOffset;                /* Offset of found item in utf-8 bytes
                                     * for exact search, Unicode chars 
                                     * for regexp */
    int matchLength;                /* Length also in bytes/chars as per
                                     * search type. */
{
    int numChars;
    int leftToScan;
    TkTextIndex curIndex, foundIndex;
    TkTextSegment *segPtr;
    TkTextLine *linePtr;
    TkText *textPtr = (TkText*)(searchSpecPtr->clientData);

    if (lineNum == searchSpecPtr->stopLine) {
	/* 
	 * If the current index is on the wrong side of the stopIndex,
	 * then the item we just found is actually outside the acceptable
	 * range, and the search is over.
	 */
	if (searchSpecPtr->backwards ^ 
	  (matchOffset >= searchSpecPtr->stopOffset)) {
	    return 0;
	}
    }
    
    /*
     * Calculate the character count, which may need augmenting
     * if there are embedded windows or elidden text.
     */

    if (searchSpecPtr->exact) {
	CONST char *startOfLine = Tcl_GetString(theLine);
	numChars = Tcl_NumUtfChars(startOfLine + matchOffset, matchLength);
    } else {
	numChars = matchLength;
    }
    
    /*
     * The index information returned by the regular expression
     * parser only considers textual information:  it doesn't
     * account for embedded windows, elided text (when we are not
     * searching elided text) or any other non-textual info.
     * Scan through the line's segments again to adjust both
     * matchChar and matchCount.
     *
     * We will walk through the segments of this line until we
     * have either reached the end of the match or we have
     * reached the end of the line.
     */

    linePtr = (TkTextLine *)clientData;
    curIndex.tree = textPtr->tree;
    curIndex.linePtr = linePtr; curIndex.byteIndex = 0;
    /* Find the starting point */
    for (segPtr = linePtr->segPtr, leftToScan = matchOffset;
	    leftToScan >= 0 && segPtr; segPtr = segPtr->nextPtr) {
	if (segPtr->typePtr != &tkTextCharType) {
	    matchOffset += segPtr->size;
	} else if (!searchSpecPtr->searchElide 
		   && TkTextIsElided(textPtr, &curIndex)) {
	    if (searchSpecPtr->exact) {
		matchOffset += segPtr->size;
	    } else {
		matchOffset += Tcl_NumUtfChars(segPtr->body.chars, -1);
	    }
	} else {
	    leftToScan -= segPtr->size;
	}
	curIndex.byteIndex += segPtr->size;
    }
    /* Calculate and store the found index in the result */
    if (searchSpecPtr->exact) {
	TkTextMakeByteIndex(textPtr->tree, lineNum, 
			    matchOffset, &foundIndex);
    } else {
	TkTextMakeCharIndex(textPtr->tree, lineNum, 
			    matchOffset, &foundIndex);
    }
    if (searchSpecPtr->all) {
	if (searchSpecPtr->resPtr == NULL) {
	    searchSpecPtr->resPtr = Tcl_NewObj();
	}
	Tcl_ListObjAppendElement(NULL, searchSpecPtr->resPtr, 
		TkTextNewIndexObj(textPtr, &foundIndex));
    } else {
	searchSpecPtr->resPtr = 
	        TkTextNewIndexObj(textPtr, &foundIndex);
    }
    /* 
     * Find the end point.  Here 'leftToScan' could be negative already
     * as a result of the above loop if the segment we reached spanned
     * the start of the string.  When we add matchLength it will become
     * non-negative.
     */
    for (leftToScan += matchLength; leftToScan > 0;
	 curIndex.byteIndex += segPtr->size, segPtr = segPtr->nextPtr) {
	if (segPtr == NULL) {
	    /* 
	     * We are on the next line -- this of course should only
	     * ever happen with searches which have matched across
	     * multiple lines 
	     */
	    linePtr = TkBTreeNextLine(linePtr);
	    segPtr = linePtr->segPtr;
	    curIndex.linePtr = linePtr; curIndex.byteIndex = 0;
	}
	if (segPtr->typePtr != &tkTextCharType) {
	    /* Anything we didn't count in the search needs adding */
	    numChars += segPtr->size;
	    continue;
	} else if (!searchSpecPtr->searchElide 
		 && TkTextIsElided(textPtr, &curIndex)) {
	    numChars += Tcl_NumUtfChars(segPtr->body.chars, -1);
	    continue;
	}
	if (searchSpecPtr->exact) {
	    leftToScan -= segPtr->size;
	} else {
	    leftToScan -= Tcl_NumUtfChars(segPtr->body.chars, -1);
	}
    }
    /* 
     * Now store the count result, if it is wanted
     */
    if (searchSpecPtr->varPtr != NULL) {
	Tcl_Obj *tmpPtr = Tcl_NewIntObj(numChars);
	if (searchSpecPtr->all) {
	    if (searchSpecPtr->countPtr == NULL) {
		searchSpecPtr->countPtr = Tcl_NewObj();
	    }
	    Tcl_ListObjAppendElement(NULL, searchSpecPtr->countPtr, tmpPtr);
	} else {
	    searchSpecPtr->countPtr = tmpPtr;
	}
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextGetTabs --
 *
 *	Parses a string description of a set of tab stops.
 *
 * Results:
 *	The return value is a pointer to a malloc'ed structure holding
 *	parsed information about the tab stops.  If an error occurred
 *	then the return value is NULL and an error message is left in
 *	the interp's result.
 *
 * Side effects:
 *	Memory is allocated for the structure that is returned.  It is
 *	up to the caller to free this structure when it is no longer
 *	needed.
 *
 *----------------------------------------------------------------------
 */

TkTextTabArray *
TkTextGetTabs(interp, tkwin, stringPtr)
    Tcl_Interp *interp;			/* Used for error reporting. */
    Tk_Window tkwin;			/* Window in which the tabs will be
					 * used. */
    Tcl_Obj *stringPtr;			/* Description of the tab stops.  
                       			 * See the text manual entry for 
                       			 * details. */
{
    int objc, i, count;
    Tcl_Obj **objv;
    TkTextTabArray *tabArrayPtr;
    TkTextTab *tabPtr;
    Tcl_UniChar ch;

    /* Map these strings to TkTextTabAlign values */
    
    static CONST char *tabOptionStrings[] = {
	"left", "right", "center", "numeric", (char *) NULL 
    };
    
    if (Tcl_ListObjGetElements(interp, stringPtr, &objc, &objv) != TCL_OK) {
	return NULL;
    }

    /*
     * First find out how many entries we need to allocate in the
     * tab array.
     */

    count = 0;
    for (i = 0; i < objc; i++) {
	char c = Tcl_GetString(objv[i])[0];
	if ((c != 'l') && (c != 'r') && (c != 'c') && (c != 'n')) {
	    count++;
	}
    }

    /*
     * Parse the elements of the list one at a time to fill in the
     * array.
     */

    tabArrayPtr = (TkTextTabArray *) ckalloc((unsigned)
	    (sizeof(TkTextTabArray) + (count-1)*sizeof(TkTextTab)));
    tabArrayPtr->numTabs = 0;
    for (i = 0, tabPtr = &tabArrayPtr->tabs[0]; i  < objc; i++, tabPtr++) {
	int index;
	
	if (Tk_GetPixelsFromObj(interp, tkwin, objv[i], &tabPtr->location)
		!= TCL_OK) {
	    goto error;
	}
	tabArrayPtr->numTabs++;

	/*
	 * See if there is an explicit alignment in the next list
	 * element.  Otherwise just use "left".
	 */

	tabPtr->alignment = LEFT;
	if ((i+1) == objc) {
	    continue;
	}
	/* There may be a more efficient way of getting this */
	Tcl_UtfToUniChar(Tcl_GetString(objv[i+1]), &ch);
	if (!Tcl_UniCharIsAlpha(ch)) {
	    continue;
	}
	i += 1;
	
	if (Tcl_GetIndexFromObj(interp, objv[i], tabOptionStrings, 
				"tab alignment", 0, &index) != TCL_OK) {
	    goto error;
	}
	tabPtr->alignment = ((TkTextTabAlign)index);
    }
    return tabArrayPtr;

    error:
    ckfree((char *) tabArrayPtr);
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TextDumpCmd --
 *
 *	Return information about the text, tags, marks, and embedded windows
 *	and images in a text widget.  See the man page for the description
 *	of the text dump operation for all the details.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Memory is allocated for the result, if needed (standard Tcl result
 *	side effects).
 *
 *----------------------------------------------------------------------
 */

static int
TextDumpCmd(textPtr, interp, objc, objv)
    register TkText *textPtr;	/* Information about text widget. */
    Tcl_Interp *interp;		/* Current interpreter. */
    int objc;			/* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. Someone else has already
				 * parsed this command enough to know that
				 * objv[1] is "dump". */
{
    TkTextIndex index1, index2;
    int arg;
    int lineno;			/* Current line number */
    int what = 0;		/* bitfield to select segment types */
    int atEnd;			/* True if dumping up to logical end */
    TkTextLine *linePtr;
    CONST char *command = NULL;	/* Script callback to apply to segments */
#define TK_DUMP_TEXT	0x1
#define TK_DUMP_MARK	0x2
#define TK_DUMP_TAG	0x4
#define TK_DUMP_WIN	0x8
#define TK_DUMP_IMG	0x10
#define TK_DUMP_ALL	(TK_DUMP_TEXT|TK_DUMP_MARK|TK_DUMP_TAG| \
	TK_DUMP_WIN|TK_DUMP_IMG)
    static CONST char *optStrings[] = {
	"-all", "-command", "-image", "-mark", "-tag", "-text", "-window",
	NULL
    };
    enum opts {
	DUMP_ALL, DUMP_CMD, DUMP_IMG, DUMP_MARK, DUMP_TAG, DUMP_TXT, DUMP_WIN
    };

    for (arg=2 ; arg < objc ; arg++) {
	int index;
	if (Tcl_GetString(objv[arg])[0] != '-') {
	    break;
	}
	if (Tcl_GetIndexFromObj(interp, objv[arg], optStrings, "option", 0,
		&index) != TCL_OK) {
	    return TCL_ERROR;
	}
	switch ((enum opts) index) {
	case DUMP_ALL:
	    what = TK_DUMP_ALL;
	    break;
	case DUMP_TXT:
	    what |= TK_DUMP_TEXT;
	    break;
	case DUMP_TAG:
	    what |= TK_DUMP_TAG;
	    break;
	case DUMP_MARK:
	    what |= TK_DUMP_MARK;
	    break;
	case DUMP_IMG:
	    what |= TK_DUMP_IMG;
	    break;
	case DUMP_WIN:
	    what |= TK_DUMP_WIN;
	    break;
	case DUMP_CMD:
	    arg++;
	    if (arg >= objc) {
		Tcl_AppendResult(interp, "Usage: ", Tcl_GetString(objv[0]), 
			" dump ?-all -image -text -mark -tag -window? ",
			"?-command script? index ?index2?", NULL);
		return TCL_ERROR;
	    }
	    command = Tcl_GetString(objv[arg]);
	    break;
	default:
	    panic("unexpected switch fallthrough");
	}
    }
    if (arg >= objc || arg+2 < objc) {
	Tcl_AppendResult(interp, "Usage: ", Tcl_GetString(objv[0]), 
		" dump ?-all -image -text -mark -tag -window? ",
		"?-command script? index ?index2?", NULL);
	return TCL_ERROR;
    }
    if (what == 0) {
	what = TK_DUMP_ALL;
    }
    if (TkTextGetObjIndex(interp, textPtr, objv[arg], &index1) != TCL_OK) {
	return TCL_ERROR;
    }
    lineno = TkBTreeLineIndex(index1.linePtr);
    arg++;
    atEnd = 0;
    if (objc == arg) {
	TkTextIndexForwChars(&index1, 1, &index2);
    } else {
	int length;
	char *str;
	if (TkTextGetObjIndex(interp, textPtr, objv[arg], &index2) != TCL_OK) {
	    return TCL_ERROR;
	}
	str = Tcl_GetStringFromObj(objv[arg], &length);
	if (strncmp(str, "end", (unsigned)length) == 0) {
	    atEnd = 1;
	}
    }
    if (TkTextIndexCmp(&index1, &index2) >= 0) {
	return TCL_OK;
    }
    if (index1.linePtr == index2.linePtr) {
	DumpLine(interp, textPtr, what, index1.linePtr,
		index1.byteIndex, index2.byteIndex, lineno, command);
    } else {
	DumpLine(interp, textPtr, what, index1.linePtr,
		index1.byteIndex, 32000000, lineno, command);
	linePtr = index1.linePtr;
	while ((linePtr = TkBTreeNextLine(linePtr)) != (TkTextLine *)NULL) {
	    lineno++;
	    if (linePtr == index2.linePtr) {
		break;
	    }
	    DumpLine(interp, textPtr, what, linePtr, 0, 32000000,
		    lineno, command);
	}
	DumpLine(interp, textPtr, what, index2.linePtr, 0,
		index2.byteIndex, lineno, command);
    }
    /*
     * Special case to get the leftovers hiding at the end mark.
     */
    if (atEnd) {
	DumpLine(interp, textPtr, what & ~TK_DUMP_TEXT, index2.linePtr,
		0, 1, lineno, command);			    
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DumpLine
 * 
 * 	Return information about a given text line from character
 *	position "start" up to, but not including, "end".
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None, but see DumpSegment.
 *	
 *----------------------------------------------------------------------
 */
static void
DumpLine(interp, textPtr, what, linePtr, startByte, endByte, lineno, command)
    Tcl_Interp *interp;
    TkText *textPtr;
    int what;			/* bit flags to select segment types */
    TkTextLine *linePtr;	/* The current line */
    int startByte, endByte;	/* Byte range to dump */
    int lineno;			/* Line number for indices dump */
    CONST char *command;	/* Script to apply to the segment */
{
    int offset;
    TkTextSegment *segPtr;
    TkTextIndex index;
    /*
     * Must loop through line looking at its segments.
     * character
     * toggleOn, toggleOff
     * mark
     * image
     * window
     */

    for (offset = 0, segPtr = linePtr->segPtr ;
	    (offset < endByte) && (segPtr != (TkTextSegment *)NULL) ;
	    offset += segPtr->size, segPtr = segPtr->nextPtr) {
	if ((what & TK_DUMP_TEXT) && (segPtr->typePtr == &tkTextCharType) &&
		(offset + segPtr->size > startByte)) {
	    char savedChar;		/* Last char used in the seg */
	    int last = segPtr->size;	/* Index of savedChar */
	    int first = 0;		/* Index of first char in seg */
	    if (offset + segPtr->size > endByte) {
		last = endByte - offset;
	    }
	    if (startByte > offset) {
		first = startByte - offset;
	    }
	    savedChar = segPtr->body.chars[last];
	    segPtr->body.chars[last] = '\0';
	    
	    TkTextMakeByteIndex(textPtr->tree, lineno, offset + first, &index);
	    DumpSegment(interp, "text", segPtr->body.chars + first,
		    command, &index, what);
	    segPtr->body.chars[last] = savedChar;
	} else if ((offset >= startByte)) {
	    if ((what & TK_DUMP_MARK) && (segPtr->typePtr->name[0] == 'm')) {
		TkTextMark *markPtr = (TkTextMark *)&segPtr->body;
		char *name = Tcl_GetHashKey(&textPtr->markTable, markPtr->hPtr);

		TkTextMakeByteIndex(textPtr->tree, lineno, offset, &index);
		DumpSegment(interp, "mark", name, command, &index, what);
	    } else if ((what & TK_DUMP_TAG) &&
			(segPtr->typePtr == &tkTextToggleOnType)) {
		TkTextMakeByteIndex(textPtr->tree, lineno, offset, &index);
		DumpSegment(interp, "tagon",
			segPtr->body.toggle.tagPtr->name,
			command, &index, what);
	    } else if ((what & TK_DUMP_TAG) && 
			(segPtr->typePtr == &tkTextToggleOffType)) {
		TkTextMakeByteIndex(textPtr->tree, lineno, offset, &index);
		DumpSegment(interp, "tagoff",
			segPtr->body.toggle.tagPtr->name,
			command, &index, what);
	    } else if ((what & TK_DUMP_IMG) && 
			(segPtr->typePtr->name[0] == 'i')) {
		TkTextEmbImage *eiPtr = (TkTextEmbImage *)&segPtr->body;
		char *name = (eiPtr->name ==  NULL) ? "" : eiPtr->name;
		TkTextMakeByteIndex(textPtr->tree, lineno, offset, &index);
		DumpSegment(interp, "image", name,
			command, &index, what);
	    } else if ((what & TK_DUMP_WIN) && 
			(segPtr->typePtr->name[0] == 'w')) {
		TkTextEmbWindow *ewPtr = (TkTextEmbWindow *)&segPtr->body;
		char *pathname;
		if (ewPtr->tkwin == (Tk_Window) NULL) {
		    pathname = "";
		} else {
		    pathname = Tk_PathName(ewPtr->tkwin);
		}
		TkTextMakeByteIndex(textPtr->tree, lineno, offset, &index);
		DumpSegment(interp, "window", pathname,
			command, &index, what);
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DumpSegment
 * 
 *	Either append information about the current segment to the result,
 *	or make a script callback with that information as arguments.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Either evals the callback or appends elements to the result string.
 *	
 *----------------------------------------------------------------------
 */
static int
DumpSegment(interp, key, value, command, index, what)
    Tcl_Interp *interp;
    char *key;			/* Segment type key */
    char *value;		/* Segment value */
    CONST char *command;	/* Script callback */
    CONST TkTextIndex *index;   /* index with line/byte position info */
    int what;			/* Look for TK_DUMP_INDEX bit */
{
    char buffer[TK_POS_CHARS];
    TkTextPrintIndex(index, buffer);
    if (command == NULL) {
	Tcl_AppendElement(interp, key);
	Tcl_AppendElement(interp, value);
	Tcl_AppendElement(interp, buffer);
	return TCL_OK;
    } else {
	CONST char *argv[4];
	char *list;
	int result;
	argv[0] = key;
	argv[1] = value;
	argv[2] = buffer;
	argv[3] = NULL;
	list = Tcl_Merge(3, argv);
	result = Tcl_VarEval(interp, command, " ", list, (char *) NULL);
	ckfree(list);
	return result;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TextEditUndo --
 * 
 *    undo the last change.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None.
 *    
 *----------------------------------------------------------------------
 */

static int
TextEditUndo(textPtr)
    TkText     *textPtr;    /* Overall information about text widget. */
{
    int status;

    if (!textPtr->undo) {
       return TCL_OK;
    }

    /* Turn off the undo feature */
    textPtr->undo = 0;

    /* The dirty counter should count downwards as we are undoing things */
    textPtr->isDirtyIncrement = -1;

    /* revert one compound action */
    status = TkUndoRevert(textPtr->undoStack);

    /* Restore the isdirty increment */
    textPtr->isDirtyIncrement = 1;

    /* Turn back on the undo feature */
    textPtr->undo = 1;

    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * TextEditRedo --
 * 
 *    redo the last undone change.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None.
 *    
 *----------------------------------------------------------------------
 */

static int
TextEditRedo(textPtr)
    TkText     *textPtr;     /* Overall information about text widget. */
{
    int status;

    if (!textPtr->undo) {
       return TCL_OK;
    }

    /* Turn off the undo feature temporarily */
    textPtr->undo = 0;

    /* reapply one compound action */
    status = TkUndoApply(textPtr->undoStack);

    /* Turn back on the undo feature */
    textPtr->undo = 1;

    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * TextEditCmd --
 *
 *    Handle the subcommands to "$text edit ...".
 *    See documentation for details.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None.
 *    
 *----------------------------------------------------------------------
 */

static int
TextEditCmd(textPtr, interp, objc, objv)
    TkText *textPtr;          /* Information about text widget. */
    Tcl_Interp *interp;       /* Current interpreter. */
    int objc;			/* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. */
{
    int index;
    
    static CONST char *editOptionStrings[] = {
	"modified", "redo", "reset", "separator", "undo", (char *) NULL 
    };
    enum editOptions {
	EDIT_MODIFIED, EDIT_REDO, EDIT_RESET, EDIT_SEPARATOR, EDIT_UNDO
    };
    
    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "option ?arg arg ...?");
	return TCL_ERROR;
    }
    
    if (Tcl_GetIndexFromObj(interp, objv[2], editOptionStrings, 
			    "edit option", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }

    switch ((enum editOptions)index) {
	case EDIT_MODIFIED: {
	    if (objc == 3) {
		Tcl_SetObjResult(interp, Tcl_NewBooleanObj(textPtr->isDirty));
	    } else if (objc != 4) {
		Tcl_WrongNumArgs(interp, 3, objv, "?boolean?");
		return TCL_ERROR;
	    } else {
		int setModified;
		XEvent event;
		if (Tcl_GetBooleanFromObj(interp, objv[3], &setModified) 
		  != TCL_OK) {
		    return TCL_ERROR;
		}
		/*
		 * Set or reset the dirty info and trigger a Modified event.
		 */

		if (setModified) {
		    textPtr->isDirty     = 1;
		    textPtr->modifiedSet = 1;
		} else {
		    textPtr->isDirty     = 0;
		    textPtr->modifiedSet = 0;
		}

		/*
		 * Send an event that the text was modified.  This is
		 * equivalent to "event generate $textWidget <<Modified>>"
		 */

		memset((VOID *) &event, 0, sizeof(event));
		event.xany.type = VirtualEvent;
		event.xany.serial = NextRequest(Tk_Display(textPtr->tkwin));
		event.xany.send_event = False;
		event.xany.window = Tk_WindowId(textPtr->tkwin);
		event.xany.display = Tk_Display(textPtr->tkwin);
		((XVirtualEvent *) &event)->name = Tk_GetUid("Modified");
		Tk_HandleEvent(&event);
	    }
	    break;
	}
	case EDIT_REDO: {
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 3, objv, NULL);
		return TCL_ERROR;
	    }
	    if (TextEditRedo(textPtr)) {
		Tcl_AppendResult(interp, "nothing to redo", (char *) NULL);
		return TCL_ERROR;
	    }
	    break;
	}
	case EDIT_RESET: {
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 3, objv, NULL);
		return TCL_ERROR;
	    }
	    TkUndoClearStacks(textPtr->undoStack);
	    break;
	}
	case EDIT_SEPARATOR: {
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 3, objv, NULL);
		return TCL_ERROR;
	    }
	    TkUndoInsertUndoSeparator(textPtr->undoStack);
	    break;
	}
	case EDIT_UNDO: {
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 3, objv, NULL);
		return TCL_ERROR;
	    }
	    if (TextEditUndo(textPtr)) {
		Tcl_AppendResult(interp, "nothing to undo",
			(char *) NULL);
		return TCL_ERROR;
	    }
	    break;
	}	
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TextGetText --
 * 
 *    Returns the text from indexPtr1 to indexPtr2, placing that text
 *    in a string object which is returned with a refCount of zero.
 *    
 *    Since the amount of text may potentially be several megabytes (e.g.
 *    in text editors built on the text widget), efficiency is very
 *    important.  We may want to investigate the efficiency of the
 *    Tcl_AppendToObj more carefully (e.g. if we know we are going to be
 *    appending several thousand lines, we could attempt to pre-allocate
 *    a larger space).
 *    
 *    Also the result is built up as a utf-8 string, but, if we knew
 *    we wanted it as Unicode, we could potentially save a huge
 *    conversion by building it up as Unicode directly.  This could
 *    be as simple as replacing Tcl_NewObj by Tcl_NewUnicodeObj.
 *
 * Results:
 *    Tcl_Obj of string type containing the specified text.
 *
 * Side effects:
 *    Memory will be allocated for the new object.  Remember to free it if
 *    it isn't going to be stored appropriately.
 *    
 *----------------------------------------------------------------------
 */

static Tcl_Obj* 
TextGetText(indexPtr1,indexPtr2)
    CONST TkTextIndex *indexPtr1;
    CONST TkTextIndex *indexPtr2;
{
    TkTextIndex tmpIndex;
    Tcl_Obj *resultPtr = Tcl_NewObj();
    
    TkTextMakeByteIndex(indexPtr1->tree, TkBTreeLineIndex(indexPtr1->linePtr),
	    indexPtr1->byteIndex, &tmpIndex);

    if (TkTextIndexCmp(indexPtr1, indexPtr2) < 0) {
	while (1) {
	    int offset, last;
	    TkTextSegment *segPtr;

	    segPtr = TkTextIndexToSeg(&tmpIndex, &offset);
	    last = segPtr->size;
	    if (tmpIndex.linePtr == indexPtr2->linePtr) {
		/* 
		 * The last line that was requested must be handled
		 * carefully, because we may need to break out of this
		 * loop in the middle of the line
		 */
		if (indexPtr2->byteIndex == tmpIndex.byteIndex) {
		    break;
		} else {
		    int last2;
		    last2 = indexPtr2->byteIndex - tmpIndex.byteIndex + offset;
		    if (last2 < last) {
			last = last2;
		    }
		}
	    }
	    if (segPtr->typePtr == &tkTextCharType) {
		Tcl_AppendToObj(resultPtr, segPtr->body.chars + offset,
				last - offset);
	    }
	    TkTextIndexForwBytes(&tmpIndex, last-offset, &tmpIndex);
	}
    }
    return resultPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateDirtyFlag --
 * 
 *    Increases the dirtyness of the text widget
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None.
 *    
 *----------------------------------------------------------------------
 */

static void 
UpdateDirtyFlag (textPtr)
    TkText *textPtr;          /* Information about text widget. */
{
    int oldDirtyFlag;

    if (textPtr->modifiedSet) {
        return;
    }
    oldDirtyFlag = textPtr->isDirty;
    textPtr->isDirty += textPtr->isDirtyIncrement;
    if (textPtr->isDirty == 0 || oldDirtyFlag == 0) {
	XEvent event;
	/*
	 * Send an event that the text was modified.  This is equivalent to
	 * "event generate $textWidget <<Modified>>"
	 */

	memset((VOID *) &event, 0, sizeof(event));
	event.xany.type = VirtualEvent;
	event.xany.serial = NextRequest(Tk_Display(textPtr->tkwin));
	event.xany.send_event = False;
	event.xany.window = Tk_WindowId(textPtr->tkwin);
	event.xany.display = Tk_Display(textPtr->tkwin);
	((XVirtualEvent *) &event)->name = Tk_GetUid("Modified");
	Tk_HandleEvent(&event);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SearchPerform --
 *
 *	Overall control of search process.  Is given a pattern, a
 *	starting index and an ending index, and attempts to perform a
 *	search.  This procedure is actually completely independent of Tk,
 *	and could in the future be split off.
 *	
 * Results:
 *	Standard Tcl result code.  In particular, if fromPtr or toPtr
 *	are not considered valid by the 'lineIndexProc', an error
 *	will be thrown and no search performed.
 *
 * Side effects:
 *	See 'SearchCore'.
 *	
 *----------------------------------------------------------------------
 */

static int
SearchPerform(interp, searchSpecPtr, patObj, fromPtr, toPtr)
    Tcl_Interp *interp;             /* For error messages */
    SearchSpec *searchSpecPtr;      /* Search parameters */
    Tcl_Obj *patObj;                /* Contains an exact string or a
				     * regexp pattern.  Must have a 
				     * refCount > 0 */
    Tcl_Obj *fromPtr;               /* Contains information describing
                                     * the first index */
    Tcl_Obj *toPtr;                 /* NULL or information describing
                                     * the last index */
{
    /* 
     * Find the starting line and starting offset (measured in Unicode
     * chars for regexp search, utf-8 bytes for exact search)
     */
    if ((*searchSpecPtr->lineIndexProc)(interp, fromPtr, searchSpecPtr, 
	    &searchSpecPtr->startLine,
	    &searchSpecPtr->startOffset) != TCL_OK) {
	return TCL_ERROR;
    }

    /* 
     * Find the optional end location, similarly.
     */
    if (toPtr != NULL) {
	if ((*searchSpecPtr->lineIndexProc)(interp, toPtr, searchSpecPtr, 
		&searchSpecPtr->stopLine,
		&searchSpecPtr->stopOffset) != TCL_OK) {
	    return TCL_ERROR;
	}
    } else {
	searchSpecPtr->stopLine = -1;
    }

    /*
     * Scan through all of the lines of the text circularly, starting
     * at the given index.  'objv[i]' is the pattern which may be an
     * exact string or a regexp pattern depending on the flags set
     * above.
     */

    return SearchCore(interp, searchSpecPtr, patObj);
}

/*
 *----------------------------------------------------------------------
 *
 * SearchCore --
 *
 *	The core of the search procedure.  This procedure is actually
 *	completely independent of Tk, and could in the future be split
 *	off.
 *
 *	The function assumes regexp-based searches operate on Unicode
 *	strings, and exact searches on utf-8 strings.  Therefore the
 *	'foundMatchProc' and 'addLineProc' need to be aware of this
 *	distinction.
 *
 * Results:
 *	Standard Tcl result code.
 *
 * Side effects:
 *	Only those of the 'searchSpecPtr->foundMatchProc' which is called
 *	whenever a match is found.
 *
 *	Note that the way matching across multiple lines is implemented,
 *	we start afresh with each line we have available, even though we
 *	may already have examined the contents of that line (and further
 *	ones) if we were attempting a multi-line match using the previous
 *	line.  This means there may be ways to speed this up a lot by not
 *	throwing away all the multi-line information one has accumulated.
 *	Profiling should be done to see where the bottlenecks lie before
 *	attempting this, however.  We would also need to be very careful
 *	such optimisation keep within the specified search bounds.
 *
 *----------------------------------------------------------------------
 */

static int
SearchCore(interp, searchSpecPtr, patObj)
    Tcl_Interp *interp;			/* For error messages */
    SearchSpec *searchSpecPtr;		/* Search parameters */
    Tcl_Obj *patObj;			/* Contains an exact string or a
					 * regexp pattern.  Must have a
					 * refCount > 0 */
{
    int passes;
    /*
     * For exact searches these are utf-8 char* offsets, for regexp
     * searches they are Unicode char offsets
     */
    int firstOffset, lastOffset, matchOffset, matchLength;
    int lineNum = searchSpecPtr->startLine;
    int code = TCL_OK;
    Tcl_Obj *theLine;

    Tcl_RegExp regexp = NULL;		/* For regexp searches only */
    CONST char *pattern = NULL;		/* For exact searches only */
    int firstNewLine = -1;		/* For exact searches only */

    if (searchSpecPtr->exact) {
	/*
	 * Convert the pattern to lower-case if we're supposed to ignore
	 * case.
	 */
	if (searchSpecPtr->noCase) {
	    patObj = Tcl_DuplicateObj(patObj);
	    /*
	     * This can change the length of the string behind the
	     * object's back, so ensure it is correctly synchronised.
	     */
	    Tcl_SetObjLength(patObj, Tcl_UtfToLower(Tcl_GetString(patObj)));
	}
    } else {
	/*
	 * Compile the regular expression.  We want '^$' to match after and
	 * before \n respectively, so use the TCL_REG_NLANCH flag.
	 */
	regexp = Tcl_GetRegExpFromObj(interp, patObj,
		(searchSpecPtr->noCase ? TCL_REG_NOCASE : 0)
		| (searchSpecPtr->noLineStop ? 0 : TCL_REG_NLSTOP)
	        | TCL_REG_ADVANCED | TCL_REG_CANMATCH | TCL_REG_NLANCH);
	if (regexp == NULL) {
	    return TCL_ERROR;
	}
    }

    /*
     * For exact strings, we want to know where the first newline is,
     * and we will also use this as a flag to test whether it is even
     * possible to match the pattern on a single line.  If not we
     * will have to search across multiple lines.
     */
    if (searchSpecPtr->exact) {
	char *nl;

	/*
	 * We only need to set the matchLength once for exact searches,
	 * and we do it here.  It is also used below as the actual
	 * pattern length, so it has dual purpose.
	 */
	pattern = Tcl_GetStringFromObj(patObj, &matchLength);
	nl = strchr(pattern, '\n');
	/*
	 * If there is no newline, or it is the very end of the string,
	 * then we don't need any special treatment, since single-line
	 * matching will work fine.
	 */
	if (nl != NULL && nl[1] != '\0') {
	    firstNewLine = (nl - pattern);
	}
    } else {
	matchLength = 0;  /* Only needed to prevent compiler warnings. */
    }

    /*
     * Keep a reference here, so that we can be sure the object
     * doesn't disappear behind our backs and invalidate its
     * contents which we are using.
     */
    Tcl_IncrRefCount(patObj);

    /*
     * For building up the current line being checked
     */
    theLine = Tcl_NewObj();
    Tcl_IncrRefCount(theLine);

    for (passes = 0; passes < 2; ) {
	ClientData lineInfo;
	int linesSearched = 1;

	if (lineNum >= searchSpecPtr->numLines) {
	    /*
	     * Don't search the dummy last line of the text.
	     */
	    goto nextLine;
	}

	/*
	 * Extract the text from the line, storing its length in
	 * 'lastOffset' (in bytes if exact, chars if regexp), since
	 * obviously the length is the maximum offset at which
	 * it is possible to find something on this line, which is
	 * what we 'lastOffset' represents.
	 */

	lineInfo = (*searchSpecPtr->addLineProc)(lineNum,
		searchSpecPtr, theLine, &lastOffset);

	firstOffset = 0;
	if (lineNum == searchSpecPtr->startLine) {
	    /*
	     * The starting line is tricky: the first time we see it
	     * we check one part of the line, and the second pass through
	     * we check the other part of the line.
	     */
	    passes++;
	    if ((passes == 1) ^ searchSpecPtr->backwards) {
		/*
		 * Forward search and first pass, or backward
		 * search and second pass.
		 *
		 * Only use the last part of the line.
		 */

		if ((searchSpecPtr->startOffset >= lastOffset)
			&& ((lastOffset != 0) || searchSpecPtr->exact)) {
		    goto nextLine;
		}

		firstOffset = searchSpecPtr->startOffset;
	    } else {
		/*
		 * Use only the first part of the line.
		 */

		lastOffset = searchSpecPtr->startOffset;
	    }
	}

	/*
	 * Check for matches within the current line 'lineNum'.  If so,
	 * and if we're searching backwards or for all matches, repeat
	 * the search until we find the last match in the line.  The
	 * 'lastOffset' is one beyond the last position in the line at
	 * which a match is allowed to begin.
	 */

	matchOffset = -1;

	if (searchSpecPtr->exact) {
	    int maxExtraLines = 0;
	    CONST char *startOfLine = Tcl_GetString(theLine);

	    do {
		Tcl_UniChar ch;
		CONST char *p;

		p = strstr(startOfLine + firstOffset, pattern);
		if (p == NULL) {
		    if (firstNewLine == -1 ||
			    firstNewLine >= (lastOffset - firstOffset)) {
			break;
		    }
		    p = startOfLine + lastOffset - firstNewLine - 1;
		    if (strncmp(p, pattern, (unsigned)(firstNewLine + 1))) {
			break;
		    } else {
			int extraLines = 1;
			int skipFirst = lastOffset - firstNewLine -1;
			/*
			 * We may be able to match if given more text.
			 * The following 'while' block handles multi-line
			 * exact searches.
			 */
			while (1) {
			    int len;

			    if (lineNum+extraLines>=searchSpecPtr->numLines) {
				p = NULL;
				break;
			    }

			    /*
			     * Only add the line if we haven't already
			     * done so already.
			     */
			    if (extraLines > maxExtraLines) {
				if ((*searchSpecPtr->addLineProc)(lineNum
					+ extraLines, searchSpecPtr, theLine,
					&len) == NULL) {
				    p = NULL;
				    break;
				}
				maxExtraLines = extraLines;
			    }

			    startOfLine = Tcl_GetString(theLine);
			    p = startOfLine + skipFirst;
			    /*
			     * Use the fact that 'matchLength = patLength'
			     * for exact searches
			     */
			    if ((len - skipFirst) >= matchLength) {
			        /*
			         * We now have enough text to match, so
			         * we make a final test and break
			         * whatever the result
			         */
				if (strncmp(p, pattern, (unsigned)matchLength)) {
				    p = NULL;
				}
				break;
			    } else {
				/*
				 * Not enough text yet, but check the prefix
				 */
				if (strncmp(p, pattern,
					(unsigned)(len - skipFirst))) {
				    p = NULL;
				    break;
				}
				/*
				 * The prefix matches, so keep looking
				 */
			    }
			    extraLines++;
			}
			/*
			 * If we reach here, with p != NULL, we've found a
			 * multi-line match, else we started a multi-match
			 * but didn't finish it off, so we go to the next line.
			 */
			if (p == NULL) {
			    break;
			}
		    }
		}
		firstOffset = p - startOfLine;
		if (firstOffset >= lastOffset) {
		    break;
		}

		/*
		 * Remember the match
		 */
		matchOffset = firstOffset;

		/*
		 * Move the starting point one character on from the
		 * previous match, in case we are doing repeated or
		 * backwards searches (for the latter, we actually do
		 * repeated forward searches).
		 */
		firstOffset += Tcl_UtfToUniChar(startOfLine+matchOffset, &ch);
		if (searchSpecPtr->all &&
			!(*searchSpecPtr->foundMatchProc)(lineNum,
			searchSpecPtr, lineInfo, theLine, matchOffset,
			matchLength)) {
		    /*
		     * We reached the end of the search
		     */
		    goto searchDone;
		}
	    } while (searchSpecPtr->backwards || searchSpecPtr->all);

	} else {

	    int maxExtraLines = 0;

	    do {
		Tcl_RegExpInfo info;
		int match;

		match = Tcl_RegExpExecObj(interp, regexp, theLine, firstOffset,
			1, ((firstOffset > 0) ? TCL_REG_NOTBOL : 0));
		if (match < 0) {
		    code = TCL_ERROR;
		    goto searchDone;
		}
		Tcl_RegExpGetInfo(regexp, &info);

		if (!match) {
		    int extraLines = 1;
		    int curLen = 0;

		    if (info.extendStart < 0) {
			break;
		    }

		    /*
		     * We may be able to match if given more text.
		     * The following 'while' block handles multi-line
		     * exact searches.
		     */
		    while (1) {
			/*
			 * Move firstOffset to first possible start
			 */
			firstOffset += info.extendStart;
			if (firstOffset >= lastOffset) {
			    /*
			     * We're being told that the only possible
			     * new match is starting after the end of
			     * the line. But, that is the next line which
			     * we will handle when we look at that line.
			     */
			    if (!searchSpecPtr->backwards
				    && (firstOffset == curLen)) {
				linesSearched = extraLines + 1;
			    }
			    break;
			}

			if (lineNum + extraLines >= searchSpecPtr->numLines) {
			    break;
			}
			/*
			 * Add next line, provided we haven't already done so
			 */
			if (extraLines > maxExtraLines) {
			    if ((*searchSpecPtr->addLineProc)(lineNum
				    + extraLines, searchSpecPtr, theLine,
				    NULL) == NULL) {
				/*
				 * There are no more acceptable lines, so
				 * we can say we have searched all of these
				 */
				if (!searchSpecPtr->backwards) {
				    linesSearched = extraLines + 1;
				}
				break;
			    }
			    maxExtraLines = extraLines;
			}

			match = Tcl_RegExpExecObj(interp, regexp, theLine,
				  firstOffset, 1,
				  ((firstOffset > 0) ? TCL_REG_NOTBOL : 0));
			if (match < 0) {
			    code = TCL_ERROR;
			    goto searchDone;
			}
			Tcl_RegExpGetInfo(regexp, &info);
			if (match || info.extendStart < 0) {
			    break;
			}
			/*
			 * The prefix matches, so keep looking
			 */
			extraLines++;
		    }
		    /*
		     * If we reach here, with match == 1, we've found a
		     * multi-line match, which we will record in the code
		     * which follows directly else we started a
		     * multi-line match but didn't finish it off, so we
		     * go to the next line.
		     *
		     * Here is where we could perform an optimisation,
		     * since we have already retrieved the contents of
		     * the next line (and many more), so we shouldn't
		     * really throw it all away and start again.  This
		     * could be particularly important for complex regexp
		     * searches.
		     */
		    if (!match) {
			/*
			 * This 'break' will take us to just before
			 * the 'nextLine:' below.
			 */
			break;
		    }
		}

		firstOffset += info.matches[0].start;
		if (firstOffset >= lastOffset) {
		    break;
		}

		/*
		 * Remember the match
		 */
		matchOffset = firstOffset;
		matchLength = info.matches[0].end - info.matches[0].start;

		/*
		 * Move the starting point one character on, in case
		 * we are doing repeated or backwards searches (for the
		 * latter, we actually do repeated forward searches).
		 */
		firstOffset++;
		if (searchSpecPtr->all &&
			!(*searchSpecPtr->foundMatchProc)(lineNum,
			searchSpecPtr, lineInfo, theLine, matchOffset,
			matchLength)) {
		    /*
		     * We reached the end of the search
		     */
		    goto searchDone;
		}
	    } while (searchSpecPtr->backwards || searchSpecPtr->all);
	}

	/*
	 * If the 'all' flag is set, we will already have stored all
	 * matches, so we just proceed to the next line.
	 *
	 * If not, and there is a match we need to store that information
	 * and we are done.
	 */

	if ((matchOffset >= 0) && !searchSpecPtr->all) {
		(*searchSpecPtr->foundMatchProc)(lineNum, searchSpecPtr,
		lineInfo, theLine, matchOffset, matchLength);
	    goto searchDone;
	}

	/*
	 * Go to the next (or previous) line;
	 */

      nextLine:

	for (; linesSearched>0 ; linesSearched--) {
	    /*
	     * If we have just completed the 'stopLine', we are done
	     */
	    if (lineNum == searchSpecPtr->stopLine) {
		goto searchDone;
	    }

	    if (searchSpecPtr->backwards) {
		lineNum--;
		if (lineNum < 0) {
		    lineNum = searchSpecPtr->numLines-1;
		}
	    } else {
		lineNum++;
		if (lineNum >= searchSpecPtr->numLines) {
		    lineNum = 0;
		}
	    }
	}

	Tcl_SetObjLength(theLine, 0);
    }
  searchDone:

    /*
     * Free up the cached line and pattern
     */
    Tcl_DecrRefCount(theLine);
    Tcl_DecrRefCount(patObj);

    return code;
}
