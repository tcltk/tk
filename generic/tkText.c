/*
 * tkText.c --
 *
 *	This module provides a big chunk of the implementation of multi-line
 *	editable text widgets for Tk. Among other things, it provides the Tcl
 *	command interfaces to text widgets. The B-tree representation of text
 *	and its actual display are implemented elsewhere.
 *
 * Copyright (c) 1992-1994 The Regents of the University of California.
 * Copyright (c) 1994-1996 Sun Microsystems, Inc.
 * Copyright (c) 1999 by Scriptics Corporation.
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#if defined(_MSC_VER ) && _MSC_VER < 1500
/* suppress wrong warnings to support ancient compilers */
#pragma warning (disable : 4305)
#endif

#include "default.h"
#include "tkInt.h"
#include "tkText.h"
#include "tkTextUndo.h"
#include "tkTextTagSet.h"
#include "tkBitField.h"
#include "tkAlloc.h"
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

#ifndef TK_C99_INLINE_SUPPORT
# define _TK_NEED_IMPLEMENTATION
# include "tkTextPriv.h"
#endif

#ifndef MAX
# define MAX(a,b) ((a) < (b) ? b : a)
#endif
#ifndef MIN
# define MIN(a,b) ((a) < (b) ? a : b)
#endif

#ifdef NDEBUG
# define DEBUG(expr)
#else
# define DEBUG(expr) expr
#endif

/*
 * Support of tk8.5.
 */
#ifdef CONST
# undef CONST
#endif
#if TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION == 5
# define CONST
#else
# define CONST const
#endif

/*
 * For compatibility with Tk 4.0 through 8.4.x, we allow tabs to be
 * mis-specified with non-increasing values. These are converted into tabs
 * which are the equivalent of at least a character width apart.
 */

#if TK_MAJOR_VERSION < 9
#define _TK_ALLOW_DECREASING_TABS
#endif

#include "tkText.h"

/*
 * Used to avoid having to allocate and deallocate arrays on the fly for
 * commonly used functions. Must be > 0.
 */

#define PIXEL_CLIENTS 8

/*
 * The 'TkTextState' enum in tkText.h is used to define a type for the -state
 * option of the Text widget. These values are used as indices into the string
 * table below.
 */

static const char *CONST stateStrings[] = {
    "disabled", "normal", "readonly", NULL
};

/*
 * The 'TkTextTagging' enum in tkText.h is used to define a type for the -tagging
 * option of the Text widget. These values are used as indices into the string table below.
 */

static const char *CONST taggingStrings[] = {
    "within", "gravity", "none", NULL
};

/*
 * The 'TkTextJustify' enum in tkText.h is used to define a type for the -justify option of
 * the Text widget. These values are used as indices into the string table below.
 */

static const char *CONST justifyStrings[] = {
    "left", "right", "full", "center", NULL
};

/*
 * The 'TkWrapMode' enum in tkText.h is used to define a type for the -wrap
 * option of the Text widget. These values are used as indices into the string
 * table below.
 */

static const char *CONST wrapStrings[] = {
    "char", "none", "word", "codepoint", NULL
};

/*
 * The 'TkSpacing' enum in tkText.h is used to define a type for the -spacing
 * option of the Text widget. These values are used as indices into the string
 * table below.
 */

static const char *CONST spaceModeStrings[] = {
    "none", "exact", "trim", NULL
};

/*
 * The 'TkTextTabStyle' enum in tkText.h is used to define a type for the
 * -tabstyle option of the Text widget. These values are used as indices into
 * the string table below.
 */

static const char *CONST tabStyleStrings[] = {
    "tabular", "wordprocessor", NULL
};

/*
 * The 'TkTextInsertUnfocussed' enum in tkText.h is used to define a type for
 * the -insertunfocussed option of the Text widget. These values are used as
 * indice into the string table below.
 */

static const char *CONST insertUnfocussedStrings[] = {
    "hollow", "none", "solid", NULL
};

/*
 * The 'TkTextHyphenRule' enum in tkText.h is used to define a type for the
 * -hyphenrules option of the Text widget. These values are used for applying
 * hyphen rules to soft hyphens.
 */

static const char *const hyphenRuleStrings[] = {
    "ck", "doubledigraph", "doublevowel", "gemination", "repeathyphen", "trema",
    "tripleconsonant" /* don't appebd a trailing NULL */
};

#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE

/*
 * The following functions and custom option type are used to define the
 * "line" option type, and thereby handle the text widget '-startline',
 * '-endline' configuration options which are of that type.
 *
 * We do not need a 'freeProc' because all changes to these two options are
 * handled through the TK_TEXT_LINE_RANGE flag in the optionSpecs list, and
 * the internal storage is just a pointer, which therefore doesn't need
 * freeing.
 */

static int		SetLineStartEnd(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin,
			    Tcl_Obj **value, char *recordPtr, int internalOffset, char *oldInternalPtr,
			    int flags);
static Tcl_Obj *	GetLineStartEnd(ClientData clientData, Tk_Window tkwin, char *recordPtr,
			    int internalOffset);
static void		RestoreLineStartEnd(ClientData clientData, Tk_Window tkwin, char *internalPtr,
			    char *oldInternalPtr);

static CONST Tk_ObjCustomOption lineOption = {
    "line",			/* name */
    SetLineStartEnd,		/* setProc */
    GetLineStartEnd,		/* getProc */
    RestoreLineStartEnd,	/* restoreProc */
    NULL,			/* freeProc */
    0
};

#endif /* SUPPORT_DEPRECATED_STARTLINE_ENDLINE */

/*
 * The following functions and custom option type are used to define the
 * "index" option type, and thereby handle the text widget '-startindex',
 * '-endindex' configuration options which are of that type.
 */

static int		SetTextStartEnd(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin,
			    Tcl_Obj **value, char *recordPtr, int internalOffset, char *oldInternalPtr,
			    int flags);
static Tcl_Obj *	GetTextStartEnd(ClientData clientData, Tk_Window tkwin, char *recordPtr,
			    int internalOffset);
static void		RestoreTextStartEnd(ClientData clientData, Tk_Window tkwin, char *internalPtr,
			    char *oldInternalPtr);
static void		FreeTextStartEnd(ClientData clientData, Tk_Window tkwin, char *internalPtr);

static CONST Tk_ObjCustomOption startEndMarkOption = {
    "index",			/* name */
    SetTextStartEnd,		/* setProc */
    GetTextStartEnd,		/* getProc */
    RestoreTextStartEnd,	/* restoreProc */
    FreeTextStartEnd,		/* freeProc */
    0
};

/*
 * Information used to parse text configuration options:
 */

static const Tk_OptionSpec optionSpecs[] = {
    {TK_OPTION_BOOLEAN, "-autoseparators", "autoSeparators",
	"AutoSeparators", DEF_TEXT_AUTO_SEPARATORS, -1, Tk_Offset(TkText, autoSeparators),
	TK_OPTION_DONT_SET_DEFAULT, 0, 0},
    {TK_OPTION_BORDER, "-background", "background", "Background",
	DEF_TEXT_BG_COLOR, -1, Tk_Offset(TkText, border), 0, DEF_TEXT_BG_MONO, TK_TEXT_LINE_REDRAW},
    {TK_OPTION_SYNONYM, "-bd", NULL, NULL,
	NULL, 0, -1, 0, "-borderwidth", TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_SYNONYM, "-bg", NULL, NULL,
	NULL, 0, -1, 0, "-background", TK_TEXT_LINE_REDRAW},
    {TK_OPTION_BOOLEAN, "-blockcursor", "blockCursor",
	"BlockCursor", DEF_TEXT_BLOCK_CURSOR, -1, Tk_Offset(TkText, blockCursorType), 0, 0, 0},
    {TK_OPTION_PIXELS, "-borderwidth", "borderWidth", "BorderWidth",
	DEF_TEXT_BORDER_WIDTH, -1, Tk_Offset(TkText, borderWidth), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_CURSOR, "-cursor", "cursor", "Cursor",
	DEF_TEXT_CURSOR, -1, Tk_Offset(TkText, cursor), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_CUSTOM, "-endindex", NULL, NULL,
	 NULL, -1, Tk_Offset(TkText, newEndIndex), 0, &startEndMarkOption, TK_TEXT_INDEX_RANGE},
#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE
    {TK_OPTION_CUSTOM, "-endline", NULL, NULL,
	 NULL, -1, Tk_Offset(TkText, endLine), TK_OPTION_NULL_OK, &lineOption, TK_TEXT_LINE_RANGE},
#endif
    {TK_OPTION_STRING, "-eolchar", "eolChar", "EolChar",
	NULL, Tk_Offset(TkText, eolCharPtr), -1, TK_OPTION_NULL_OK, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_COLOR, "-eolcolor", "eolColor", "EolColor",
	DEF_TEXT_FG, -1, Tk_Offset(TkText, eolColor), TK_OPTION_NULL_OK, 0, TK_TEXT_LINE_REDRAW},
    {TK_OPTION_BOOLEAN, "-exportselection", "exportSelection",
	"ExportSelection", DEF_TEXT_EXPORT_SELECTION, -1, Tk_Offset(TkText, exportSelection), 0, 0, 0},
    {TK_OPTION_SYNONYM, "-fg", "foreground", NULL,
	NULL, 0, -1, 0, "-foreground", TK_TEXT_LINE_REDRAW},
    {TK_OPTION_FONT, "-font", "font", "Font",
	DEF_TEXT_FONT, -1, Tk_Offset(TkText, tkfont), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_COLOR, "-foreground", "foreground", "Foreground",
	DEF_TEXT_FG, -1, Tk_Offset(TkText, fgColor), 0, 0, TK_TEXT_LINE_REDRAW},
    {TK_OPTION_PIXELS, "-height", "height", "Height",
	DEF_TEXT_HEIGHT, -1, Tk_Offset(TkText, height), 0, 0, 0},
    {TK_OPTION_COLOR, "-highlightbackground", "highlightBackground", "HighlightBackground",
	DEF_TEXT_HIGHLIGHT_BG, -1, Tk_Offset(TkText, highlightBgColorPtr), 0, 0, 0},
    {TK_OPTION_COLOR, "-highlightcolor", "highlightColor", "HighlightColor",
	DEF_TEXT_HIGHLIGHT, -1, Tk_Offset(TkText, highlightColorPtr), 0, 0, 0},
    {TK_OPTION_PIXELS, "-highlightthickness", "highlightThickness", "HighlightThickness",
	DEF_TEXT_HIGHLIGHT_WIDTH, -1, Tk_Offset(TkText, highlightWidth), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_STRING, "-hyphenrules", NULL, NULL,
	NULL, Tk_Offset(TkText, hyphenRulesPtr), -1, TK_OPTION_NULL_OK, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_COLOR, "-hyphencolor", "hyphenColor", "HyphenColor",
	DEF_TEXT_FG, -1, Tk_Offset(TkText, hyphenColor), TK_OPTION_NULL_OK, 0, TK_TEXT_LINE_REDRAW},
    {TK_OPTION_BOOLEAN, "-hyphens", "hyphens", "Hyphens",
	"0", -1, Tk_Offset(TkText, hyphens), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_BORDER, "-inactiveselectbackground", "inactiveSelectBackground", "Foreground",
	DEF_TEXT_INACTIVE_SELECT_BG_COLOR, -1, Tk_Offset(TkText, inactiveSelBorder),
	TK_OPTION_NULL_OK, DEF_TEXT_SELECT_MONO, 0},
    {TK_OPTION_COLOR, "-inactiveselectforeground", "inactiveSelectForeground",
	"Background", DEF_TEXT_INACTIVE_SELECT_FG_COLOR, -1,
	Tk_Offset(TkText, inactiveSelFgColorPtr), TK_OPTION_NULL_OK, DEF_TEXT_SELECT_FG_MONO, 0},
    {TK_OPTION_BORDER, "-insertbackground", "insertBackground", "Foreground",
	DEF_TEXT_INSERT_BG, -1, Tk_Offset(TkText, insertBorder), 0, 0, 0},
    {TK_OPTION_PIXELS, "-insertborderwidth", "insertBorderWidth",
	"BorderWidth", DEF_TEXT_INSERT_BD_COLOR, -1, Tk_Offset(TkText, insertBorderWidth), 0,
	(ClientData) DEF_TEXT_INSERT_BD_MONO, 0},
    {TK_OPTION_COLOR, "-insertforeground", "insertForeground", "InsertForeground",
	DEF_TEXT_BG_COLOR, -1, Tk_Offset(TkText, insertFgColorPtr), 0, 0, 0},
    {TK_OPTION_INT, "-insertofftime", "insertOffTime", "OffTime",
	DEF_TEXT_INSERT_OFF_TIME, -1, Tk_Offset(TkText, insertOffTime), 0, 0, 0},
    {TK_OPTION_INT, "-insertontime", "insertOnTime", "OnTime",
	DEF_TEXT_INSERT_ON_TIME, -1, Tk_Offset(TkText, insertOnTime), 0, 0, 0},
    {TK_OPTION_STRING_TABLE,
	"-insertunfocussed", "insertUnfocussed", "InsertUnfocussed",
	DEF_TEXT_INSERT_UNFOCUSSED, -1, Tk_Offset(TkText, insertUnfocussed),
	0, insertUnfocussedStrings, 0},
    {TK_OPTION_PIXELS, "-insertwidth", "insertWidth", "InsertWidth",
	DEF_TEXT_INSERT_WIDTH, -1, Tk_Offset(TkText, insertWidth), 0, 0, 0},
    {TK_OPTION_STRING_TABLE, "-justify", "justify", "Justify",
	"left", -1, Tk_Offset(TkText, justify), 0, justifyStrings, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_STRING, "-lang", "lang", "Lang",
	 NULL, Tk_Offset(TkText, langPtr), -1, TK_OPTION_NULL_OK, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_INT, "-maxundo", "maxUndo", "MaxUndo",
	DEF_TEXT_MAX_UNDO, -1, Tk_Offset(TkText, maxUndoDepth), TK_OPTION_DONT_SET_DEFAULT, 0, 0},
    {TK_OPTION_INT, "-maxundosize", "maxUndoSize", "MaxUndoSize",
	DEF_TEXT_MAX_UNDO, -1, Tk_Offset(TkText, maxUndoSize), TK_OPTION_DONT_SET_DEFAULT, 0, 0},
    {TK_OPTION_INT, "-maxredo", "maxRedo", "MaxRedo",
	"-1", -1, Tk_Offset(TkText, maxRedoDepth), TK_OPTION_DONT_SET_DEFAULT, 0, 0},
    {TK_OPTION_PIXELS, "-padx", "padX", "Pad",
	DEF_TEXT_PADX, -1, Tk_Offset(TkText, padX), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_PIXELS, "-pady", "padY", "Pad",
	DEF_TEXT_PADY, -1, Tk_Offset(TkText, padY), 0, 0, 0},
    {TK_OPTION_RELIEF, "-relief", "relief", "Relief",
	DEF_TEXT_RELIEF, -1, Tk_Offset(TkText, relief), 0, 0, 0},
    {TK_OPTION_INT, "-responsiveness", "responsiveness", "Responsiveness",
	"50", -1, Tk_Offset(TkText, responsiveness), 0, 0, 0},
    {TK_OPTION_BORDER, "-selectbackground", "selectBackground", "Foreground",
	DEF_TEXT_SELECT_COLOR, -1, Tk_Offset(TkText, selBorder), 0, DEF_TEXT_SELECT_MONO, 0},
    {TK_OPTION_PIXELS, "-selectborderwidth", "selectBorderWidth", "BorderWidth",
	DEF_TEXT_SELECT_BD_COLOR, Tk_Offset(TkText, selBorderWidthPtr),
	Tk_Offset(TkText, selBorderWidth), TK_OPTION_NULL_OK, DEF_TEXT_SELECT_BD_MONO, 0},
    {TK_OPTION_COLOR, "-selectforeground", "selectForeground", "Background",
	DEF_TEXT_SELECT_FG_COLOR, -1, Tk_Offset(TkText, selFgColorPtr),
	TK_OPTION_NULL_OK, DEF_TEXT_SELECT_FG_MONO, 0},
    {TK_OPTION_BOOLEAN, "-setgrid", "setGrid", "SetGrid",
	DEF_TEXT_SET_GRID, -1, Tk_Offset(TkText, setGrid), 0, 0, 0},
    {TK_OPTION_BOOLEAN, "-showendofline", "showEndOfLine", "ShowEndOfLine",
	"0", -1, Tk_Offset(TkText, showEndOfLine), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_BOOLEAN, "-showinsertforeground", "showInsertForeground", "ShowInsertForeground",
	"0", -1, Tk_Offset(TkText, showInsertFgColor), 0, 0, 0},
    {TK_OPTION_STRING_TABLE, "-spacemode", "spaceMode", "SpaceMode",
	"none", -1, Tk_Offset(TkText, spaceMode), 0, spaceModeStrings, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_PIXELS, "-spacing1", "spacing1", "Spacing",
	DEF_TEXT_SPACING1, -1, Tk_Offset(TkText, spacing1), 0, 0 , TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_PIXELS, "-spacing2", "spacing2", "Spacing",
	DEF_TEXT_SPACING2, -1, Tk_Offset(TkText, spacing2), 0, 0 , TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_PIXELS, "-spacing3", "spacing3", "Spacing",
	DEF_TEXT_SPACING3, -1, Tk_Offset(TkText, spacing3), 0, 0 , TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_CUSTOM, "-startindex", NULL, NULL,
	 NULL, -1, Tk_Offset(TkText, newStartIndex), 0, &startEndMarkOption, TK_TEXT_INDEX_RANGE},
#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE
    {TK_OPTION_CUSTOM, "-startline", NULL, NULL,
	 NULL, -1, Tk_Offset(TkText, startLine), TK_OPTION_NULL_OK, &lineOption, TK_TEXT_LINE_RANGE},
#endif
    {TK_OPTION_STRING_TABLE, "-state", "state", "State",
	DEF_TEXT_STATE, -1, Tk_Offset(TkText, state), 0, stateStrings, 0},
    {TK_OPTION_BOOLEAN, "-steadymarks", "steadyMarks", "SteadyMarks",
	"0", -1, Tk_Offset(TkText, steadyMarks), TK_OPTION_DONT_SET_DEFAULT, 0, 0},
    {TK_OPTION_INT, "-synctime", "syncTime", "SyncTime", "150", -1, Tk_Offset(TkText, syncTime),
	0, 0, TK_TEXT_SYNCHRONIZE},
    {TK_OPTION_STRING, "-tabs", "tabs", "Tabs",
	DEF_TEXT_TABS, Tk_Offset(TkText, tabOptionPtr), -1, TK_OPTION_NULL_OK, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_STRING_TABLE, "-tabstyle", "tabStyle", "TabStyle",
	DEF_TEXT_TABSTYLE, -1, Tk_Offset(TkText, tabStyle), 0, tabStyleStrings, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_STRING_TABLE, "-tagging", "tagging", "Tagging",
	"within", -1, Tk_Offset(TkText, tagging), 0, taggingStrings, 0},
    {TK_OPTION_STRING, "-takefocus", "takeFocus", "TakeFocus",
	DEF_TEXT_TAKE_FOCUS, -1, Tk_Offset(TkText, takeFocus), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BOOLEAN, "-undo", "undo", "Undo",
	DEF_TEXT_UNDO, -1, Tk_Offset(TkText, undo), TK_OPTION_DONT_SET_DEFAULT, 0 ,0},
    {TK_OPTION_BOOLEAN, "-useunibreak", "useUniBreak", "UseUniBreak",
	"0", -1, Tk_Offset(TkText, useUniBreak), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_INT, "-width", "width", "Width",
	DEF_TEXT_WIDTH, -1, Tk_Offset(TkText, width), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_STRING_TABLE, "-wrap", "wrap", "Wrap",
	DEF_TEXT_WRAP, -1, Tk_Offset(TkText, wrapMode), 0, wrapStrings, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_STRING, "-xscrollcommand", "xScrollCommand", "ScrollCommand",
	DEF_TEXT_XSCROLL_COMMAND, -1, Tk_Offset(TkText, xScrollCmd), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-yscrollcommand", "yScrollCommand", "ScrollCommand",
	DEF_TEXT_YSCROLL_COMMAND, -1, Tk_Offset(TkText, yScrollCmd), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_END, NULL, NULL, NULL, 0, 0, 0, 0, 0, 0}
};

/*
 * These three typedefs, the structure and the SearchPerform, SearchCore
 * functions below are used for line-based searches of the text widget, and,
 * in particular, to handle multi-line matching even though the text widget is
 * a single-line based data structure. They are completely abstracted away
 * from the Text widget internals, however, so could easily be re-used with
 * any line-based entity to provide multi-line matching.
 *
 * We have abstracted this code away from the text widget to try to keep Tk as
 * modular as possible.
 */

struct SearchSpec;	/* Forward declaration. */

typedef ClientData	SearchAddLineProc(int lineNum, struct SearchSpec *searchSpecPtr,
			    Tcl_Obj *theLine, int *lenPtr, int *extraLinesPtr);
typedef bool		SearchMatchProc(int lineNum, struct SearchSpec *searchSpecPtr,
			    ClientData clientData, Tcl_Obj *theLine, int matchOffset, int matchLength);
typedef int		SearchLineIndexProc(Tcl_Interp *interp, Tcl_Obj *objPtr,
			    struct SearchSpec *searchSpecPtr, int *linePosPtr, int *offsetPosPtr);

typedef struct SearchSpec {
    TkText *textPtr;		/* Information about widget. */
    bool exact;			/* Whether search is exact or regexp. */
    bool noCase;		/* Case-insenstivive? */
    bool noLineStop;		/* If not set, a regexp search will use the TCL_REG_NLSTOP flag. */
    bool overlap;		/* If set, results from multiple searches (-all) are allowed to
    				 * overlap each other. */
    bool strictLimits;		/* If set, matches must be completely inside the from,to range.
    				 * Otherwise the limits only apply to the start of each match. */
    bool all;			/* Whether all or the first match should be reported. */
    bool backwards;		/* Searching forwards or backwards. */
    bool searchElide;		/* Search in hidden text as well. */
    bool searchHyphens;		/* Search in soft hyhens as well. */
    int startLine;		/* First line to examine. */
    int startOffset;		/* Index in first line to start at. */
    int stopLine;		/* Last line to examine, or -1 when we search all available text. */
    int stopOffset;		/* Index to stop at, provided stopLine is not -1. */
    int numLines;		/* Total lines which are available. */
    Tcl_Obj *varPtr;		/* If non-NULL, store length(s) of match(es) in this variable. */
    Tcl_Obj *countPtr;		/* Keeps track of currently found lengths. */
    Tcl_Obj *resPtr;		/* Keeps track of currently found locations */
    SearchAddLineProc *addLineProc;
				/* Function to call when we need to add another line to the search
				 * string so far */
    SearchMatchProc *foundMatchProc;
				/* Function to call when we have found a match. */
    SearchLineIndexProc *lineIndexProc;
				/* Function to call when we have found a match. */
    ClientData clientData;	/* Information about structure being searched, in this case a text
    				 * widget. */
} SearchSpec;

/*
 * The text-widget-independent functions which actually perform the search,
 * handling both regexp and exact searches.
 */

static int	SearchCore(Tcl_Interp *interp, SearchSpec *searchSpecPtr, Tcl_Obj *patObj);
static int	SearchPerform(Tcl_Interp *interp, SearchSpec *searchSpecPtr, Tcl_Obj *patObj,
		    Tcl_Obj *fromPtr, Tcl_Obj *toPtr);

/*
 * We need a simple linked list for strings:
 */

typedef struct TkTextStringList {
    struct TkTextStringList *nextPtr;
    Tcl_Obj *strObjPtr;
} TkTextStringList;

/*
 * Boolean variable indicating whether or not special debugging code should be executed.
 */

bool tkTextDebug = false;

typedef const TkTextUndoAtom * (*InspectUndoStackProc)(TkTextUndoStack stack);

/*
 * Forward declarations for functions defined later in this file:
 */

static bool		DeleteIndexRange(TkSharedText *sharedTextPtr, TkText *textPtr,
			    const TkTextIndex *indexPtr1, const TkTextIndex *indexPtr2, int flags,
			    bool viewUpdate, bool triggerWatchDelete, bool triggerWatchInsert,
			    bool userFlag, bool final);
static int		CountIndices(const TkText *textPtr, const TkTextIndex *indexPtr1,
			    const TkTextIndex *indexPtr2, TkTextCountType type);
static void		DestroyText(TkText *textPtr);
static void		ClearText(TkText *textPtr, bool clearTags);
static void		FireWidgetViewSyncEvent(ClientData clientData);
static void		FreeEmbeddedWindows(TkText *textPtr);
static void		InsertChars(TkText *textPtr, TkTextIndex *index1Ptr, TkTextIndex *index2Ptr,
			    char const *string, unsigned length, bool viewUpdate,
			    TkTextTagSet *tagInfoPtr, TkTextTag *hyphenTagPtr, bool parseHyphens);
static void		TextBlinkProc(ClientData clientData);
static void		TextCmdDeletedProc(ClientData clientData);
static int		CreateWidget(TkSharedText *sharedTextPtr, Tk_Window tkwin, Tcl_Interp *interp,
			    const TkText *parent, int objc, Tcl_Obj *const objv[]);
static void		TextEventProc(ClientData clientData, XEvent *eventPtr);
static void		ProcessConfigureNotify(TkText *textPtr, bool updateLineGeometry);
static int		TextFetchSelection(ClientData clientData, int offset, char *buffer,
			    int maxBytes);
static int		TextIndexSortProc(const void *first, const void *second);
static int		TextInsertCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[], const TkTextIndex *indexPtr,
			    bool viewUpdate, bool triggerWatchDelete, bool triggerWatchInsert,
			    bool userFlag, bool *destroyed, bool parseHyphens);
static int		TextReplaceCmd(TkText *textPtr, Tcl_Interp *interp,
			    const TkTextIndex *indexFromPtr, const TkTextIndex *indexToPtr,
			    int objc, Tcl_Obj *const objv[], bool viewUpdate, bool triggerWatch,
			    bool userFlag, bool *destroyed, bool parseHyphens);
static int		TextSearchCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static int		TextEditCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static int		TextWidgetObjCmd(ClientData clientData,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static void		TextWorldChangedCallback(ClientData instanceData);
static void		TextWorldChanged(TkText *textPtr, int mask);
static void		UpdateLineMetrics(TkText *textPtr, unsigned lineNum, unsigned endLine);
static int		TextChecksumCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static int		TextDumpCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static int		TextInspectCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static bool		DumpLine(Tcl_Interp *interp, TkText *textPtr,
			    int what, TkTextLine *linePtr, int start, int end,
			    int lineno, Tcl_Obj *command, TkTextTag **prevTagPtr);
static bool		DumpSegment(TkText *textPtr, Tcl_Interp *interp, const char *key,
			    const char *value, Tcl_Obj *command, const TkTextIndex *index, int what);
static void		InspectUndoStack(const TkSharedText *sharedTextPtr,
			    InspectUndoStackProc firstAtomProc, InspectUndoStackProc nextAtomProc,
			    Tcl_Obj *objPtr);
static void		InspectRetainedUndoItems(const TkSharedText *sharedTextPtr, Tcl_Obj *objPtr);
static Tcl_Obj *	TextGetText(TkText *textPtr, const TkTextIndex *index1,
			    const TkTextIndex *index2, TkTextIndex *lastIndexPtr, Tcl_Obj *resultPtr,
			    unsigned maxBytes, bool visibleOnly, bool includeHyphens);
static void		GenerateEvent(TkSharedText *sharedTextPtr, const char *type);
static void		RunAfterSyncCmd(ClientData clientData);
static void		UpdateModifiedFlag(TkSharedText *sharedTextPtr, bool flag);
static Tcl_Obj *	MakeEditInfo(Tcl_Interp *interp, TkText *textPtr, Tcl_Obj *arrayPtr);
static unsigned		TextSearchIndexInLine(const SearchSpec *searchSpecPtr, TkTextLine *linePtr,
			    int byteIndex);
static int		TextPeerCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static int		TextWatchCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static bool		TriggerWatchEdit(TkText *textPtr, const char *operation,
			    const TkTextIndex *indexPtr1, const TkTextIndex *indexPtr2,
			    const char *info, bool final);
static void		TriggerUndoStackEvent(TkSharedText *sharedTextPtr);
static void		PushRetainedUndoTokens(TkSharedText *sharedTextPtr);
static bool		IsEmpty(const TkSharedText *sharedTextPtr, const TkText *textPtr);
static bool		IsClean(const TkSharedText *sharedTextPtr, const TkText *textPtr,
			    bool discardSelection);
static TkTextUndoPerformProc TextUndoRedoCallback;
static TkTextUndoFreeProc TextUndoFreeCallback;
static TkTextUndoStackContentChangedProc TextUndoStackContentChangedCallback;

/*
 * Some definitions for controlling "dump", "inspect", and "checksum".
 */

enum {
    TK_DUMP_TEXT           = SEG_GROUP_CHAR,
    TK_DUMP_CHARS          = TK_DUMP_TEXT|SEG_GROUP_HYPHEN,
    TK_DUMP_MARK           = SEG_GROUP_MARK,
    TK_DUMP_ELIDE          = SEG_GROUP_BRANCH,
    TK_DUMP_TAG            = SEG_GROUP_TAG,
    TK_DUMP_WIN            = SEG_GROUP_WINDOW,
    TK_DUMP_IMG            = SEG_GROUP_IMAGE,
    TK_DUMP_NODE           = 1 << 20,
    TK_DUMP_DUMP_ALL       = TK_DUMP_TEXT|TK_DUMP_CHARS|TK_DUMP_MARK|TK_DUMP_TAG|TK_DUMP_WIN|TK_DUMP_IMG,

    TK_DUMP_DISPLAY        = 1 << 21,
    TK_DUMP_DISPLAY_CHARS  = TK_DUMP_CHARS|TK_DUMP_DISPLAY,
    TK_DUMP_DISPLAY_TEXT   = TK_DUMP_TEXT|TK_DUMP_DISPLAY,
    TK_DUMP_CRC_DFLT       = TK_DUMP_TEXT|SEG_GROUP_WINDOW|SEG_GROUP_IMAGE,
    TK_DUMP_CRC_ALL        = TK_DUMP_TEXT|TK_DUMP_CHARS|TK_DUMP_DISPLAY_TEXT|SEG_GROUP_WINDOW|
			     SEG_GROUP_IMAGE|TK_DUMP_MARK|TK_DUMP_TAG,

    TK_DUMP_NESTED         = 1 << 22,
    TK_DUMP_TEXT_CONFIGS   = 1 << 23,
    TK_DUMP_TAG_CONFIGS    = 1 << 24,
    TK_DUMP_TAG_BINDINGS   = 1 << 25,
    TK_DUMP_INSERT_MARK    = 1 << 26,
    TK_DUMP_DISCARD_SEL    = 1 << 27,
    TK_DUMP_DONT_RESOLVE   = 1 << 28,
    TK_DUMP_INSPECT_DFLT   = TK_DUMP_DUMP_ALL|TK_DUMP_TEXT_CONFIGS|TK_DUMP_TAG_CONFIGS,
    TK_DUMP_INSPECT_ALL    = TK_DUMP_INSPECT_DFLT|TK_DUMP_CHARS|TK_DUMP_TAG_BINDINGS|
			     TK_DUMP_DISPLAY_TEXT|TK_DUMP_DISCARD_SEL|TK_DUMP_INSERT_MARK|
			     TK_DUMP_DONT_RESOLVE|TK_DUMP_NESTED|TK_DUMP_ELIDE
};

/*
 * Declarations of the three search procs required by the multi-line search routines.
 */

static SearchMatchProc		TextSearchFoundMatch;
static SearchAddLineProc	TextSearchAddNextLine;
static SearchLineIndexProc	TextSearchGetLineIndex;

/*
 * The structure below defines text class behavior by means of functions that
 * can be invoked from generic window code.
 */

static CONST Tk_ClassProcs textClass = {
    sizeof(Tk_ClassProcs),	/* size */
    TextWorldChangedCallback,	/* worldChangedProc */
    NULL,			/* createProc */
    NULL			/* modalProc */
};

#if TK_CHECK_ALLOCS

/*
 * Some stuff for memory checks, and allocation statistic.
 */

unsigned tkTextCountNewShared = 0;
unsigned tkTextCountDestroyShared = 0;
unsigned tkTextCountNewPeer = 0;
unsigned tkTextCountDestroyPeer = 0;
unsigned tkTextCountNewPixelInfo = 0;
unsigned tkTextCountDestroyPixelInfo = 0;
unsigned tkTextCountNewSegment = 0;
unsigned tkTextCountDestroySegment = 0;
unsigned tkTextCountNewTag = 0;
unsigned tkTextCountDestroyTag = 0;
unsigned tkTextCountNewUndoToken = 0;
unsigned tkTextCountDestroyUndoToken = 0;
unsigned tkTextCountNewNode = 0;
unsigned tkTextCountDestroyNode = 0;
unsigned tkTextCountNewLine = 0;
unsigned tkTextCountDestroyLine = 0;
unsigned tkTextCountNewSection = 0;
unsigned tkTextCountDestroySection = 0;

extern unsigned tkIntSetCountDestroy;
extern unsigned tkIntSetCountNew;
extern unsigned tkBitCountNew;
extern unsigned tkBitCountDestroy;
extern unsigned tkQTreeCountNewTree;
extern unsigned tkQTreeCountDestroyTree;
extern unsigned tkQTreeCountNewNode;
extern unsigned tkQTreeCountDestroyNode;
extern unsigned tkQTreeCountNewItem;
extern unsigned tkQTreeCountDestroyItem;
extern unsigned tkQTreeCountNewElement;
extern unsigned tkQTreeCountDestroyElement;

typedef struct WatchShared {
    TkSharedText *sharedTextPtr; 
    struct WatchShared *nextPtr;
} WatchShared;

static unsigned widgetNumber = 0;
static WatchShared *watchShared;

static void
AllocStatistic()
{
    const WatchShared *wShared;

    if (!tkBTreeDebug) {
	return;
    }

    for (wShared = watchShared; wShared; wShared = wShared->nextPtr) {
	const TkText *peer;

	for (peer = wShared->sharedTextPtr->peers; peer; peer = peer->next) {
	    printf("Unreleased text widget %d\n", peer->widgetNumber);
	}
    }

    printf("---------------------------------\n");
    printf("ALLOCATION:        new    destroy\n");
    printf("---------------------------------\n");
    printf("Shared:       %8u - %8u\n", tkTextCountNewShared, tkTextCountDestroyShared);
    printf("Peer:         %8u - %8u\n", tkTextCountNewPeer, tkTextCountDestroyPeer);
    printf("Segment:      %8u - %8u\n", tkTextCountNewSegment, tkTextCountDestroySegment);
    printf("Tag:          %8u - %8u\n", tkTextCountNewTag, tkTextCountDestroyTag);
    printf("UndoToken:    %8u - %8u\n", tkTextCountNewUndoToken, tkTextCountDestroyUndoToken);
    printf("Node:         %8u - %8u\n", tkTextCountNewNode, tkTextCountDestroyNode);
    printf("Line:         %8u - %8u\n", tkTextCountNewLine, tkTextCountDestroyLine);
    printf("Section:      %8u - %8u\n", tkTextCountNewSection, tkTextCountDestroySection);
    printf("PixelInfo:    %8u - %8u\n", tkTextCountNewPixelInfo, tkTextCountDestroyPixelInfo);
    printf("BitField:     %8u - %8u\n", tkBitCountNew, tkBitCountDestroy);
    printf("IntSet:       %8u - %8u\n", tkIntSetCountNew, tkIntSetCountDestroy);
    printf("Tree:         %8u - %8u\n", tkQTreeCountNewTree, tkQTreeCountDestroyTree);
    printf("Tree-Node:    %8u - %8u\n", tkQTreeCountNewNode, tkQTreeCountDestroyNode);
    printf("Tree-Item:    %8u - %8u\n", tkQTreeCountNewItem, tkQTreeCountDestroyItem);
    printf("Tree-Element: %8u - %8u\n", tkQTreeCountNewElement, tkQTreeCountDestroyElement);
    printf("--------------------------------\n");

    if (tkTextCountNewShared != tkTextCountDestroyShared
	    || tkTextCountNewPeer != tkTextCountDestroyPeer
	    || tkTextCountNewSegment != tkTextCountDestroySegment
	    || tkTextCountNewTag != tkTextCountDestroyTag
	    || tkTextCountNewUndoToken != tkTextCountDestroyUndoToken
	    || tkTextCountNewNode != tkTextCountDestroyNode
	    || tkTextCountNewLine != tkTextCountDestroyLine
	    || tkTextCountNewSection != tkTextCountDestroySection
	    || tkTextCountNewPixelInfo != tkTextCountDestroyPixelInfo
	    || tkBitCountNew != tkBitCountDestroy
	    || tkIntSetCountNew != tkIntSetCountDestroy
	    || tkQTreeCountNewTree != tkQTreeCountDestroyTree
	    || tkQTreeCountNewElement != tkQTreeCountDestroyElement
	    || tkQTreeCountNewNode != tkQTreeCountDestroyNode
	    || tkQTreeCountNewItem != tkQTreeCountDestroyItem) {
	printf("*** memory leak detected ***\n");
	printf("----------------------------\n");
	/* TkBitCheckAllocs(); */
    }
}
#endif /* TK_CHECK_ALLOCS */

#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE

/*
 * Some helpers.
 */

static void WarnAboutDeprecatedStartLineOption() {
    static bool printWarning = true;
    if (printWarning) {
	fprintf(stderr, "Option \"-startline\" is deprecated, please use option \"-startindex\"\n");
	printWarning = false;
    }
}
static void WarnAboutDeprecatedEndLineOption() {
    static bool printWarning = true;
    if (printWarning) {
	fprintf(stderr, "Option \"-endline\" is deprecated, please use option \"-endindex\"\n");
	printWarning = false;
    }
}

#endif /* SUPPORT_DEPRECATED_STARTLINE_ENDLINE */

/*
 * Wee need a helper for sending virtual events, because in newer Tk version
 * the footprint of TkSendVirtualEvent has changed. (Note that this source has
 * backports for 8.5, and older versions of 8.6).
 */

static void
SendVirtualEvent(
    Tk_Window tkwin,
    char const *eventName,
    Tcl_Obj *detail)
{
#if TK_MAJOR_VERSION > 8 \
	|| (TK_MAJOR_VERSION == 8 \
	    && (TK_MINOR_VERSION > 6 || (TK_MINOR_VERSION == 6 && TK_RELEASE_SERIAL >= 6)))
    /* new footprint since 8.6.6 */
    TkSendVirtualEvent(tkwin, eventName, detail);
#else
# if TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION >= 6
    if (!detail) {
	/* new function since 8.6.0, and valid until 8.6.5 */
	TkSendVirtualEvent(tkwin, eventName);
	return;
    }
# endif
    {
	/* backport to 8.5 */
	union { XEvent general; XVirtualEvent virtual; } event;

	memset(&event, 0, sizeof(event));
	event.general.xany.type = VirtualEvent;
	event.general.xany.serial = NextRequest(Tk_Display(tkwin));
	event.general.xany.send_event = False;
	event.general.xany.window = Tk_WindowId(tkwin);
	event.general.xany.display = Tk_Display(tkwin);
	event.virtual.name = Tk_GetUid(eventName);
	event.virtual.user_data = detail;
	Tk_HandleEvent(&event.general);
    }
#endif
}

/*
 *--------------------------------------------------------------
 *
 * GetByteLength --
 *
 *	This function should be defined by Tcl, but it isn't defined,
 *	so we are doing this.
 *
 * Results:
 *	The length of the string.
 *
 * Side effects:
 *	Calls Tcl_GetString(objPtr) if objPtr->bytes is not yet resolved.
 *
 *--------------------------------------------------------------
 */

static int
GetByteLength(
    Tcl_Obj *objPtr)
{
    assert(objPtr);

    if (!objPtr->bytes) {
	Tcl_GetString(objPtr);
    }
    return objPtr->length;
}

/*
 *--------------------------------------------------------------
 *
 * Tk_TextObjCmd --
 *
 *	This function is invoked to process the "text" Tcl command. See the
 *	user documentation for details on what it does.
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
Tk_TextObjCmd(
    ClientData clientData,	/* Main window associated with interpreter. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Tk_Window tkwin = clientData;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "pathName ?-option value ...?");
	return TCL_ERROR;
    }

    if (!tkwin) {
	tkwin = Tk_MainWindow(interp);
    }
    return CreateWidget(NULL, tkwin, interp, NULL, objc, objv);
}

/*
 *----------------------------------------------------------------------
 *
 * PushRetainedUndoTokens --
 *
 *	Push the retained undo tokens onto the stack.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Same as TkTextPushUndoToken, additionaly 'undoTagList' and
 *	'undoMarkList' will be cleared.
 *
 *----------------------------------------------------------------------
 */

static void
PushRetainedUndoTokens(
    TkSharedText *sharedTextPtr)
{
    unsigned i;

    assert(sharedTextPtr);
    assert(sharedTextPtr->undoStack);

    for (i = 0; i < sharedTextPtr->undoTagListCount; ++i) {
	TkTextPushUndoTagTokens(sharedTextPtr, sharedTextPtr->undoTagList[i]);
    }

    for (i = 0; i < sharedTextPtr->undoMarkListCount; ++i) {
	TkTextPushUndoMarkTokens(sharedTextPtr, &sharedTextPtr->undoMarkList[i]);
    }

    sharedTextPtr->undoTagListCount = 0;
    sharedTextPtr->undoMarkListCount = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextPushUndoToken --
 *
 *	This function is pushing the given undo/redo token. Don't use
 *	TkTextUndoPushItem, because some of the prepared undo tokens
 *	are retained.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Same as TkTextUndoPushItem, furthermore all retained items
 *	will be pushed.
 *
 *----------------------------------------------------------------------
 */

void
TkTextPushUndoToken(
    TkSharedText *sharedTextPtr,
    void *token,
    unsigned byteSize)
{
    TkTextUndoAction action;

    assert(sharedTextPtr);
    assert(sharedTextPtr->undoStack);
    assert(token);

    action = ((TkTextUndoToken *) token)->undoType->action;

    if (action == TK_TEXT_UNDO_INSERT || action == TK_TEXT_UNDO_DELETE) {
	sharedTextPtr->insertDeleteUndoTokenCount += 1;
    }

    PushRetainedUndoTokens(sharedTextPtr);
    TkTextUndoPushItem(sharedTextPtr->undoStack, token, byteSize);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextPushRedoToken --
 *
 *	This function is pushing the given redo token. This function
 *	is useful only for the reconstruction of the undo stack.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Same as TkTextUndoPushRedoItem.
 *
 *----------------------------------------------------------------------
 */

void
TkTextPushRedoToken(
    TkSharedText *sharedTextPtr,
    void *token,
    unsigned byteSize)
{
    assert(sharedTextPtr);
    assert(sharedTextPtr->undoStack);
    assert(token);

    TkTextUndoPushRedoItem(sharedTextPtr->undoStack, token, byteSize);
}

/*
 *--------------------------------------------------------------
 *
 * CreateWidget --
 *
 *	This function is invoked to process the "text" Tcl command, (when
 *	called by Tk_TextObjCmd) and the "$text peer create" text widget
 *	sub-command (called from TextPeerCmd).
 *
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result, places the name of the widget created into the
 *	interp's result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

static int
CreateWidget(
    TkSharedText *sharedTextPtr,/* Shared widget info, or NULL. */
    Tk_Window tkwin,		/* Main window associated with interpreter. */
    Tcl_Interp *interp,		/* Current interpreter. */
    const TkText *parent,	/* If non-NULL then take default start, end
				 * from this parent. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    TkText *textPtr;
    Tk_OptionTable optionTable;
    TkTextIndex startIndex;
    Tk_Window newWin;

    /*
     * Create the window.
     */

    if (!(newWin = Tk_CreateWindowFromPath(interp, tkwin, Tcl_GetString(objv[1]), NULL))) {
	return TCL_ERROR;
    }

    if (!sharedTextPtr) {
	sharedTextPtr = memset(malloc(sizeof(TkSharedText)), 0, sizeof(TkSharedText));

	Tcl_InitHashTable(&sharedTextPtr->tagTable, TCL_STRING_KEYS);
	Tcl_InitHashTable(&sharedTextPtr->markTable, TCL_STRING_KEYS);
	Tcl_InitHashTable(&sharedTextPtr->windowTable, TCL_STRING_KEYS);
	Tcl_InitHashTable(&sharedTextPtr->imageTable, TCL_STRING_KEYS);
	sharedTextPtr->usedTags = TkBitResize(NULL, 256);
	sharedTextPtr->elisionTags = TkBitResize(NULL, 256);
	sharedTextPtr->selectionTags = TkBitResize(NULL, 256);
	sharedTextPtr->dontUndoTags = TkBitResize(NULL, 256);
	sharedTextPtr->affectDisplayTags = TkBitResize(NULL, 256);
	sharedTextPtr->notAffectDisplayTags = TkBitResize(NULL, 256);
	sharedTextPtr->affectDisplayNonSelTags = TkBitResize(NULL, 256);
	sharedTextPtr->affectGeometryTags = TkBitResize(NULL, 256);
	sharedTextPtr->affectGeometryNonSelTags = TkBitResize(NULL, 256);
	sharedTextPtr->affectLineHeightTags = TkBitResize(NULL, 256);
	sharedTextPtr->tagLookup = malloc(256*sizeof(TkTextTag *));
	sharedTextPtr->emptyTagInfoPtr = TkTextTagSetResize(NULL, 0);
	sharedTextPtr->maxRedoDepth = -1;
	sharedTextPtr->autoSeparators = true;
	sharedTextPtr->lastEditMode = TK_TEXT_EDIT_OTHER;
	sharedTextPtr->lastUndoTokenType = -1;
	sharedTextPtr->startMarker = TkTextMakeStartEndMark(NULL, &tkTextLeftMarkType);
	sharedTextPtr->endMarker = TkTextMakeStartEndMark(NULL, &tkTextRightMarkType);
	sharedTextPtr->protectionMark[0] = TkTextMakeMark(NULL, NULL);
	sharedTextPtr->protectionMark[1] = TkTextMakeMark(NULL, NULL);
	sharedTextPtr->protectionMark[0]->typePtr = &tkTextProtectionMarkType;
	sharedTextPtr->protectionMark[1]->typePtr = &tkTextProtectionMarkType;

	DEBUG(memset(sharedTextPtr->tagLookup, 0, 256*sizeof(TkTextTag *)));

	sharedTextPtr->mainPeer = memset(malloc(sizeof(TkText)), 0, sizeof(TkText));
	sharedTextPtr->mainPeer->startMarker = sharedTextPtr->startMarker;
	sharedTextPtr->mainPeer->endMarker = sharedTextPtr->endMarker;
	sharedTextPtr->mainPeer->sharedTextPtr = sharedTextPtr;
	DEBUG_ALLOC(tkTextCountNewPeer++);

#if TK_CHECK_ALLOCS
	if (tkTextCountNewShared++ == 0) {
	    atexit(AllocStatistic);
	}
	/*
	 * Add this shared resource to global list.
	 */
	{
	    WatchShared *wShared = malloc(sizeof(WatchShared));
	    wShared->sharedTextPtr = sharedTextPtr;
	    wShared->nextPtr = watchShared;
	    watchShared = wShared;
	}
#endif

	/*
	 * The construction of the tree requires a valid setup of the shared resource.
	 */

	sharedTextPtr->tree = TkBTreeCreate(sharedTextPtr, 1);
    }

    DEBUG_ALLOC(tkTextCountNewPeer++);

    /*
     * Create the text widget and initialize everything to zero, then set the
     * necessary initial (non-NULL) values. It is important that the 'set' tag
     * and 'insert', 'current' mark pointers are all NULL to start.
     */

    textPtr = memset(malloc(sizeof(TkText)), 0, sizeof(TkText));
    textPtr->tkwin = newWin;
    textPtr->display = Tk_Display(newWin);
    textPtr->interp = interp;
    textPtr->widgetCmd = Tcl_CreateObjCommand(interp, Tk_PathName(textPtr->tkwin),
	    TextWidgetObjCmd, textPtr, TextCmdDeletedProc);
    DEBUG_ALLOC(textPtr->widgetNumber = ++widgetNumber);

    /*
     * Add the new widget to the shared list.
     */

    textPtr->sharedTextPtr = sharedTextPtr;
    sharedTextPtr->refCount += 1;
    textPtr->next = sharedTextPtr->peers;
    sharedTextPtr->peers = textPtr;

    /*
     * Clear the indices, do this after the shared widget is created.
     */

    TkTextIndexClear(&textPtr->topIndex, textPtr);
    TkTextIndexClear(&textPtr->selIndex, textPtr);

    /*
     * This refCount will be held until DestroyText is called. Note also that
     * the later call to 'TkTextCreateDInfo' will add more refCounts.
     */

    textPtr->refCount = 1;

    /*
     * Specify start and end lines in the B-tree. The default is the same as
     * the parent, but this can be adjusted to display more or less if the
     * start, end where given as configuration options.
     */

    if (parent) {
	(textPtr->startMarker = parent->startMarker)->refCount += 1;
	(textPtr->endMarker = parent->endMarker)->refCount += 1;
#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE
	textPtr->startLine = parent->startLine;
	textPtr->endLine = parent->endLine;
#endif
    } else {
	(textPtr->startMarker = sharedTextPtr->startMarker)->refCount += 1;
	(textPtr->endMarker = sharedTextPtr->endMarker)->refCount += 1;
    }

    /*
     * Register with the B-tree. In some sense it would be best if we could do
     * this later (after configuration options), so that any changes to
     * start,end do not require a total recalculation.
     */

    TkBTreeAddClient(sharedTextPtr->tree, textPtr, textPtr->lineHeight);

    /*
     * Also the image binding support has to be enabled if required.
     * This has to be done after TkBTreeAddClient has been called.
     */

    TkTextImageAddClient(sharedTextPtr, textPtr);

    textPtr->state = TK_TEXT_STATE_NORMAL;
    textPtr->relief = TK_RELIEF_FLAT;
    textPtr->cursor = None;
    textPtr->charWidth = 1;
    textPtr->spaceWidth = 1;
    textPtr->lineHeight = -1;
    textPtr->prevWidth = Tk_Width(newWin);
    textPtr->prevHeight = Tk_Height(newWin);
    textPtr->hyphens = -1;
    textPtr->currNearbyFlag = -1;
    textPtr->prevSyncState = -1;

    /*
     * This will add refCounts to textPtr.
     */

    TkTextCreateDInfo(textPtr);
    TkTextIndexSetupToStartOfText(&startIndex, textPtr, sharedTextPtr->tree);
    TkTextSetYView(textPtr, &startIndex, 0);
    textPtr->exportSelection = true;
    textPtr->pickEvent.type = LeaveNotify;
    textPtr->steadyMarks = textPtr->sharedTextPtr->steadyMarks;
    textPtr->undo = textPtr->sharedTextPtr->undo;
    textPtr->maxUndoDepth = textPtr->sharedTextPtr->maxUndoDepth;
    textPtr->maxRedoDepth = textPtr->sharedTextPtr->maxRedoDepth;
    textPtr->maxUndoSize = textPtr->sharedTextPtr->maxUndoSize;
    textPtr->autoSeparators = textPtr->sharedTextPtr->autoSeparators;

    /*
     * Create the "sel" tag and the "current" and "insert" marks.
     * Note: it is important that textPtr->selTagPtr is NULL before this
     * initial call.
     */

    textPtr->selTagPtr = TkTextCreateTag(textPtr, "sel", NULL);
    textPtr->insertMarkPtr = TkTextSetMark(textPtr, "insert", &startIndex);
    textPtr->currentMarkPtr = TkTextSetMark(textPtr, "current", &startIndex);
    textPtr->currentMarkIndex = startIndex;

    sharedTextPtr->numPeers += 1;

    /*
     * Create the option table for this widget class. If it has already been
     * created, the cached pointer will be returned.
     */

    optionTable = Tk_CreateOptionTable(interp, optionSpecs);

    Tk_SetClass(textPtr->tkwin, "Text");
    Tk_SetClassProcs(textPtr->tkwin, &textClass, textPtr);
    textPtr->optionTable = optionTable;

    Tk_CreateEventHandler(textPtr->tkwin,
	    ExposureMask|StructureNotifyMask|FocusChangeMask, TextEventProc, textPtr);
    Tk_CreateEventHandler(textPtr->tkwin, KeyPressMask|KeyReleaseMask
	    |ButtonPressMask|ButtonReleaseMask|EnterWindowMask
	    |LeaveWindowMask|PointerMotionMask|VirtualEventMask,
	    TkTextBindProc, textPtr);
    Tk_CreateSelHandler(textPtr->tkwin, XA_PRIMARY, XA_STRING, TextFetchSelection, textPtr, XA_STRING);

    if (Tk_InitOptions(interp, (char *) textPtr, optionTable, textPtr->tkwin) != TCL_OK) {
	Tk_DestroyWindow(textPtr->tkwin);
	return TCL_ERROR;
    }
    if (TkConfigureText(interp, textPtr, objc - 2, objv + 2) != TCL_OK) {
	Tk_DestroyWindow(textPtr->tkwin);
	return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, TkNewWindowObj(textPtr->tkwin));
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * UpdateLineMetrics --
 *
 *	This function updates the pixel height calculations of a range of
 *	lines in the widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Line heights may be recalculated.
 *
 *--------------------------------------------------------------
 */

static void
UpdateLineMetrics(
    TkText *textPtr,		/* Information about widget. */
    unsigned startLine,		/* Start at this line. */
    unsigned endLine)		/* Go no further than this line. */
{
    if (!textPtr->sharedTextPtr->allowUpdateLineMetrics) {
	ProcessConfigureNotify(textPtr, true);
    }
    TkTextUpdateLineMetrics(textPtr, startLine, endLine);
}

/*
 *--------------------------------------------------------------
 *
 * TextWidgetObjCmd --
 *
 *	This function is invoked to process the Tcl command that corresponds
 *	to a text widget. See the user documentation for details on what it
 *	does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

static void
ErrorNotAllowed(
    Tcl_Interp *interp,
    const char *text)
{
    Tcl_SetObjResult(interp, Tcl_NewStringObj(text, -1));
    Tcl_SetErrorCode(interp, "TK", "TEXT", "NOT_ALLOWED", NULL);
}

static bool
TestIfTriggerUserMod(
    TkSharedText *sharedTextPtr,
    Tcl_Obj *indexObjPtr)
{
    return sharedTextPtr->triggerWatchCmd && strcmp(Tcl_GetString(indexObjPtr), "insert") == 0;
}

static bool
TestIfPerformingUndoRedo(
    Tcl_Interp *interp,
    const TkSharedText *sharedTextPtr,
    int *result)
{
    if (sharedTextPtr->undoStack && TkTextUndoIsPerformingUndoRedo(sharedTextPtr->undoStack)) {
	/*
	 * It's possible that this command command will be invoked inside the "watch" callback,
	 * but this is not allowed when performing undo/redo.
	 */

	ErrorNotAllowed(interp, "cannot modify inside undo/redo operation");
	if (result) {
	    *result = TCL_ERROR;
	}
	return true;
    }
    return false;
}

static bool
TestIfDisabled(
    Tcl_Interp *interp,
    const TkText *textPtr,
    int *result)
{
    assert(result);

    if (textPtr->state == TK_TEXT_STATE_DISABLED) {
#if !SUPPORT_DEPRECATED_MODS_OF_DISABLED_WIDGET
	ErrorNotAllowed(interp, "attempt to modify disabled widget");
	*result = TCL_ERROR;
#endif
	return true;
    }
    return false;
}

static bool
TestIfDead(
    Tcl_Interp *interp,
    const TkText *textPtr,
    int *result)
{
    assert(result);

    if (TkTextIsDeadPeer(textPtr)) {
#if !SUPPORT_DEPRECATED_MODS_OF_DISABLED_WIDGET
	ErrorNotAllowed(interp, "attempt to modify dead widget");
	*result = TCL_ERROR;
#endif
	return true;
    }
    return false;
}

static Tcl_Obj *
AppendScript(
    const char *oldScript,
    const char *script)
{
    char buffer[1024];
    int lenOfNew = strlen(script);
    int lenOfOld = strlen(oldScript);
    size_t totalLen = lenOfOld + lenOfNew + 1;
    char *newScript = buffer;
    Tcl_Obj *newScriptObj;

    if (totalLen + 2 > sizeof(buffer)) {
	newScript = malloc(totalLen + 1);
    }

    memcpy(newScript, oldScript, lenOfOld);
    newScript[lenOfOld] = '\n';
    memcpy(newScript + lenOfOld + 1, script, lenOfNew + 1);
    newScriptObj = Tcl_NewStringObj(newScript, totalLen);
    if (newScript != buffer) { free(newScript); }
    return newScriptObj;
}

static int
TextWidgetObjCmd(
    ClientData clientData,	/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    TkText *textPtr = clientData;
    TkSharedText *sharedTextPtr;
    int result = TCL_OK;
    int commandIndex = -1;
    bool oldUndoStackEvent;

    static const char *const optionStrings[] = {
	"tk_bindvar", "tk_textInsert", "tk_textReplace",
	"bbox", "brks", "checksum", "cget", "clear", "compare", "configure",
	"count", "debug", "delete", "dlineinfo", "dump", "edit", "get", "image",
	"index", "insert", "inspect", "isclean", "isdead", "isempty", "lineno",
	"load", "mark", "peer", "pendingsync", "replace", "scan", "search",
	"see", "sync", "tag", "watch", "window", "xview", "yview", NULL
    };
    enum options {
	TEXT_TK_BINDVAR, TEXT_TK_TEXTINSERT, TEXT_TK_TEXTREPLACE,
	TEXT_BBOX, TEXT_BRKS, TEXT_CHECKSUM, TEXT_CGET, TEXT_CLEAR, TEXT_COMPARE, TEXT_CONFIGURE,
	TEXT_COUNT, TEXT_DEBUG, TEXT_DELETE, TEXT_DLINEINFO, TEXT_DUMP, TEXT_EDIT, TEXT_GET, TEXT_IMAGE,
	TEXT_INDEX, TEXT_INSERT, TEXT_INSPECT, TEXT_ISCLEAN, TEXT_ISDEAD, TEXT_ISEMPTY, TEXT_LINENO,
	TEXT_LOAD, TEXT_MARK, TEXT_PEER, TEXT_PENDINGSYNC, TEXT_REPLACE, TEXT_SCAN, TEXT_SEARCH,
	TEXT_SEE, TEXT_SYNC, TEXT_TAG, TEXT_WATCH, TEXT_WINDOW, TEXT_XVIEW, TEXT_YVIEW
    };

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[1], optionStrings,
	    sizeof(char *), "option", 0, &commandIndex) != TCL_OK) {
	/*
	 * Hide the first three options, generating the error description with
	 * the side effects of Tcl_GetIndexFromObjStruct.
	 */

	(void) Tcl_GetIndexFromObjStruct(interp, objv[1], optionStrings + 3,
		sizeof(char *), "option", 0, &commandIndex);
	return TCL_ERROR;
    }

    textPtr->refCount += 1;
    sharedTextPtr = textPtr->sharedTextPtr;
    oldUndoStackEvent = sharedTextPtr->undoStackEvent;
    sharedTextPtr->undoStackEvent = false;

    /*
     * Clear saved insert cursor position.
     */

    TkTextIndexClear(&textPtr->insertIndex, textPtr);

    /*
     * Check if we need to update the "current" mark segment.
     */

    if (sharedTextPtr->haveToSetCurrentMark) {
	TkTextUpdateCurrentMark(sharedTextPtr);
    }

    switch ((enum options) commandIndex) {
    case TEXT_TK_BINDVAR: {
	TkTextStringList *listPtr;

	/*
	 * Bind a variable to this widget, this variable will be released (Tcl_UnsetVar2)
	 * when the widget will be destroyed.
	 *
	 * I suggest to provide a general support for binding variables to widgets in a
	 * future Tk version.
	 */

	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "varname");
	    result = TCL_ERROR;
	    goto done;
	}

	listPtr = malloc(sizeof(TkTextStringList));
	Tcl_IncrRefCount(listPtr->strObjPtr = objv[2]);
	listPtr->nextPtr = textPtr->varBindingList;
	textPtr->varBindingList = listPtr;
	break;
    }
    case TEXT_BBOX: {
	int x, y, width, height, argc = 2;
	bool discardPartial = false;
	TkTextIndex index;

	if (objc == 4) {
	    const char* option = Tcl_GetString(objv[2]);

	    if (strcmp(option, "-discardpartial") == 0) {
		discardPartial = true;
		argc += 1;
	    } else if (*option != '-') {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad option \"%s\": must be -discardpartial", option));
		result = TCL_ERROR;
		goto done;
	    }
	}
	if (objc - argc + 2 != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "?-discardpartial? index");
	    result = TCL_ERROR;
	    goto done;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[argc], &index)) {
	    result = TCL_ERROR;
	    goto done;
	}
	if (TkTextIndexBbox(textPtr, &index, discardPartial, &x, &y, &width, &height, NULL)) {
	    Tcl_Obj *listObj = Tcl_NewObj();

	    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(x));
	    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(y));
	    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(width));
	    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(height));

	    Tcl_SetObjResult(interp, listObj);
	}
	break;
    }
    case TEXT_BRKS: {
	Tcl_Obj *arrPtr;
	unsigned length, i;
	char const *lang = NULL;
	char buf[1];

	if (objc != 3 && objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "index");
	    result = TCL_ERROR;
	    goto done;
	}
	if (objc == 4) {
	    if (!TkTextTestLangCode(interp, objv[3])) {
		result = TCL_ERROR;
		goto done;
	    }
	    if (!TkTextComputeBreakLocations(interp, "", 0, "en", buf)) {
		ErrorNotAllowed(interp, "external library libunibreak/liblinebreak is not available");
		result = TCL_ERROR;
		goto done;
	    }
	    lang = Tcl_GetString(objv[3]);
	}
	if ((length = GetByteLength(objv[2])) < textPtr->brksBufferSize) {
	    textPtr->brksBufferSize = MAX(length, textPtr->brksBufferSize + 512);
	    textPtr->brksBuffer = realloc(textPtr->brksBuffer, textPtr->brksBufferSize);
	}
	TkTextComputeBreakLocations(interp, Tcl_GetString(objv[2]), length, lang, textPtr->brksBuffer);
	arrPtr = Tcl_NewObj();

	for (i = 0; i < length; ++i) {
	    int value;

	    switch (textPtr->brksBuffer[i]) {
	    case LINEBREAK_INSIDEACHAR: continue;
	    case LINEBREAK_MUSTBREAK:   value = 2; break;
	    case LINEBREAK_ALLOWBREAK:  value = 1; break;
	    default:                    value = 0; break;
	    }
	    Tcl_ListObjAppendElement(interp, arrPtr, Tcl_NewIntObj(value));
	}

	Tcl_SetObjResult(interp, arrPtr);
	break;
    }
    case TEXT_CHECKSUM:
	result = TextChecksumCmd(textPtr, interp, objc, objv);
    	break;
    case TEXT_CGET:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option");
	    result = TCL_ERROR;
	    goto done;
	} else {
#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE

	    Tcl_Obj *objPtr, *optionObj = NULL;

	    if (strcmp(Tcl_GetString(objv[2]), "-start") == 0) {
		optionObj = Tcl_NewStringObj(textPtr->startLine ? "-startline" : "-startindex", -1);
	    } else if (strncmp(Tcl_GetString(objv[2]), "-startl", 7) == 0) {
		optionObj = Tcl_NewStringObj("-startline", -1);
	    } else if (strcmp(Tcl_GetString(objv[2]), "-end") == 0) {
		optionObj = Tcl_NewStringObj(textPtr->endLine ? "-endline" : "-endindex", -1);
	    } else if (strncmp(Tcl_GetString(objv[2]), "-endl", 5) == 0) {
		optionObj = Tcl_NewStringObj("-endline", -1);
	    } else {
		Tcl_IncrRefCount(optionObj = objv[2]);
	    }

	    Tcl_IncrRefCount(optionObj);
	    objPtr = Tk_GetOptionValue(interp, (char *) textPtr,
		    textPtr->optionTable, optionObj, textPtr->tkwin);
	    Tcl_DecrRefCount(optionObj);

#else /* if !SUPPORT_DEPRECATED_STARTLINE_ENDLINE */

	    objPtr = Tk_GetOptionValue(interp, (char *) textPtr,
		    textPtr->optionTable, objv[2], textPtr->tkwin);

#endif /* SUPPORT_DEPRECATED_STARTLINE_ENDLINE */

	    if (!objPtr) {
		result = TCL_ERROR;
		goto done;
	    }
	    Tcl_SetObjResult(interp, objPtr);
	    result = TCL_OK;
	}
	break;
    case TEXT_CLEAR:
	if (TestIfPerformingUndoRedo(interp, sharedTextPtr, &result)) {
	    goto done;
	}
	ClearText(textPtr, true);
	TkTextRelayoutWindow(textPtr, TK_TEXT_LINE_GEOMETRY);
	TK_BTREE_DEBUG(TkBTreeCheck(sharedTextPtr->tree));
	break;
    case TEXT_COMPARE: {
	int relation, value;
	TkTextIndex index1, index2;

	if (objc != 5) {
	    Tcl_WrongNumArgs(interp, 2, objv, "index1 op index2");
	    result = TCL_ERROR;
	    goto done;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[2], &index1)
		|| !TkTextGetIndexFromObj(interp, textPtr, objv[4], &index2)) {
	    result = TCL_ERROR;
	    goto done;
	}
	relation = TkTextIndexCompare(&index1, &index2);
	value = TkTextTestRelation(interp, relation, Tcl_GetString(objv[3]));
	if (value == -1) {
	    result = TCL_ERROR;
	} else {
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(value));
	}
	break;
    }
    case TEXT_CONFIGURE:
	if (objc <= 3) {
	    Tcl_Obj *objPtr = Tk_GetOptionInfo(interp, (char *) textPtr,
		    textPtr->optionTable, objc == 3 ? objv[2] : NULL, textPtr->tkwin);

	    if (!objPtr) {
		result = TCL_ERROR;
		goto done;
	    }
	    Tcl_SetObjResult(interp, objPtr);
	} else {
	    result = TkConfigureText(interp, textPtr, objc - 2, objv + 2);
	}
	break;
    case TEXT_COUNT: {
	TkTextIndex indexFrom, indexTo;
	Tcl_Obj *objPtr = NULL;
	bool update = false;
	int i, found = 0;

	if (objc < 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "?-option value ...? index1 index2");
	    result = TCL_ERROR;
	    goto done;
	}

	if (!TkTextGetIndexFromObj(interp, textPtr, objv[objc - 2], &indexFrom)
		|| !TkTextGetIndexFromObj(interp, textPtr, objv[objc - 1], &indexTo)) {
	    result = TCL_ERROR;
	    goto done;
	}

	for (i = 2; i < objc - 2; i++) {
	    int length;
	    int value = INT_MIN;
	    const char *option = Tcl_GetString(objv[i]);

	    length = GetByteLength(objv[i]);
	    if (length < 2 || option[0] != '-') {
		goto badOption;
	    }
	    switch (option[1]) {
	    case 'c':
		if (strncmp("-chars", option, length) == 0) {
		    value = CountIndices(textPtr, &indexFrom, &indexTo, COUNT_CHARS);
		}
		break;
	    case 'd':
	    	if (length > 8 && strncmp("-display", option, 8) == 0) {
		    switch (option[8]) {
		    case 'c':
			if (strcmp("chars", option + 8) == 0) {
			    value = CountIndices(textPtr, &indexFrom, &indexTo, COUNT_DISPLAY_CHARS);
			}
			break;
		    case 'h':
			if (strcmp("hyphens", option + 8) == 0) {
			    value = CountIndices(textPtr, &indexFrom, &indexTo, COUNT_DISPLAY_HYPHENS);
			}
			break;
		    case 'i':
			if (strcmp("indices", option + 8) == 0) {
			    value = CountIndices(textPtr, &indexFrom, &indexTo, COUNT_DISPLAY_INDICES);
			}
			break;
		    case 'l':
			if (strcmp("lines", option + 8) == 0) {
			    int compare = TkTextIndexCompare(&indexFrom, &indexTo);

			    if (compare == 0) {
				value = 0;
			    } else {
				const TkTextIndex *indexPtr1;
				const TkTextIndex *indexPtr2;

				if (compare < 0) {
				    indexPtr1 = &indexFrom;
				    indexPtr2 = &indexTo;
				} else {
				    indexPtr1 = &indexTo;
				    indexPtr2 = &indexFrom;
				}
				if (!sharedTextPtr->allowUpdateLineMetrics) {
				    ProcessConfigureNotify(textPtr, true);
				}
				value = TkTextCountDisplayLines(textPtr, indexPtr1, indexPtr2);
				if (compare > 0) {
				    value = -value;
				}
			    }
			}
			break;
		    case 't':
			if (strcmp("text", option + 8) == 0) {
			    value = CountIndices(textPtr, &indexFrom, &indexTo, COUNT_DISPLAY_TEXT);
			}
			break;
		    }
		}
		break;
	    case 'h':
		if (strncmp("-hyphens", option, length) == 0) {
		    value = CountIndices(textPtr, &indexFrom, &indexTo, COUNT_HYPHENS);
		}
		break;
	    case 'i':
		if (strncmp("-indices", option, length) == 0) {
		    value = CountIndices(textPtr, &indexFrom, &indexTo, COUNT_INDICES);
		}
		break;
	    case 'l':
	    	if (strncmp("-lines", option, length) == 0) {
		    TkTextBTree tree = sharedTextPtr->tree;
		    value = TkBTreeLinesTo(tree, textPtr, TkTextIndexGetLine(&indexTo), NULL)
			    - TkBTreeLinesTo(tree, textPtr, TkTextIndexGetLine(&indexFrom), NULL);
		}
		break;
	    case 't':
		if (strncmp("-text", option, length) == 0) {
		    value = CountIndices(textPtr, &indexFrom, &indexTo, COUNT_TEXT);
		}
		break;
	    case 'u':
		if (strncmp("-update", option, length) == 0) {
		    update = true;
		    continue;
		}
		break;
	    case 'x':
		if (strncmp("-xpixels", option, length) == 0) {
		    int x1, x2;
		    TkTextIndex index;

		    index = indexFrom;
		    TkTextFindDisplayIndex(textPtr, &index, 0, &x1);
		    index = indexTo;
		    TkTextFindDisplayIndex(textPtr, &index, 0, &x2);
		    value = x2 - x1;
		}
		break;
	    case 'y':
		if (strncmp("-ypixels", option, length) == 0) {
		    int from, to;

		    if (update) {
			from = TkTextIndexGetLineNumber(&indexFrom, textPtr);
			to = TkTextIndexGetLineNumber(&indexTo, textPtr);
			UpdateLineMetrics(textPtr, from, to);
		    }
		    from = TkTextIndexYPixels(textPtr, &indexFrom);
		    to = TkTextIndexYPixels(textPtr, &indexTo);
		    value = to - from;
		}
		break;
	    }
	    if (value == INT_MIN) {
		goto badOption;
	    }

	    found += 1;
	    if (found == 1) {
		Tcl_SetObjResult(interp, Tcl_NewIntObj(value));
	    } else {
		if (found == 2) {
		    /*
		     * Move the first item we put into the result into the
		     * first element of the list object.
		     */

		    objPtr = Tcl_NewObj();
		    Tcl_ListObjAppendElement(NULL, objPtr,
			    Tcl_GetObjResult(interp));
		}
		Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewIntObj(value));
	    }
	}

	if (found == 0) {
	    /*
	     * Use the default '-indices'.
	     */

	    int value = CountIndices(textPtr, &indexFrom, &indexTo, COUNT_INDICES);
	    Tcl_SetObjResult(interp, Tcl_NewIntObj(value));
	} else if (found > 1) {
	    Tcl_SetObjResult(interp, objPtr);
	}
	break;

    badOption:
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"bad option \"%s\": must be -chars, -displaychars, -displayhyphens, -displayindices, "
		"-displaylines, -displaytext, -hyphens, -indices, -lines, -text, -update, -xpixels, "
		"or -ypixels", Tcl_GetString(objv[i])));
	Tcl_SetErrorCode(interp, "TK", "TEXT", "INDEX_OPTION", NULL);
	result = TCL_ERROR;
	goto done;
    }
    case TEXT_DEBUG:
	if (objc > 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "boolean");
	    result = TCL_ERROR;
	    goto done;
	}
	if (objc == 2) {
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(tkBTreeDebug));
	} else {
	    if (Tcl_GetBooleanFromObj(interp, objv[2], (int *) &tkBTreeDebug) != TCL_OK) {
		result = TCL_ERROR;
		goto done;
	    }
	    tkTextDebug = tkBTreeDebug;
	}
	break;
    case TEXT_DELETE: {
	int i, flags = 0;
	bool ok = true;

	for (i = 2; i < objc - 1; i++) {
	    const char *option = Tcl_GetString(objv[i]);
	    int length;

	    if (option[0] != '-') {
		break;
	    }
	    length = GetByteLength(objv[i]);
	    if (strncmp("-marks", option, length) == 0) {
		flags |= DELETE_MARKS;
	    } else if (strncmp("-inclusive", option, length) == 0) {
		flags |= DELETE_INCLUSIVE;
	    } else {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad option \"%s\": must be -marks, or -inclusive", Tcl_GetString(objv[i])));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "INDEX_OPTION", NULL);
		result = TCL_ERROR;
		goto done;
	    }
	}

	objv += i - 2;
	objc -= i - 2;

	if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "?-marks? ?-inclusive? index1 ?index2 ...?");
	    result = TCL_ERROR;
	    goto done;
	}
	if (TestIfDisabled(interp, textPtr, &result)
		|| TestIfDead(interp, textPtr, &result)
		|| TestIfPerformingUndoRedo(interp, sharedTextPtr, &result)) {
	    goto done;
	}
	if (objc < 5) {
	    /*
	     * Simple case requires no predetermination of indices.
	     */

	    TkTextIndex index1, index2, *index2Ptr;
	    bool triggerUserMod = TestIfTriggerUserMod(sharedTextPtr, objv[2]);
	    bool triggerWatch = triggerUserMod || textPtr->triggerAlways;

	    if (triggerWatch) {
		TkTextSaveCursorIndex(textPtr);
	    }

	    /*
	     * Parse the starting and stopping indices.
	     */

	    if (!TkTextGetIndexFromObj(textPtr->interp, textPtr, objv[2], &index1)) {
		result = TCL_ERROR;
		goto done;
	    }
	    if (objc == 4) {
		if (!TkTextGetIndexFromObj(textPtr->interp, textPtr, objv[3], index2Ptr = &index2)) {
		    result = TCL_ERROR;
		    goto done;
		}
	    } else {
		index2Ptr = NULL;
	    }
	    ok = DeleteIndexRange(NULL, textPtr, &index1, index2Ptr, flags, true,
		    triggerWatch, triggerWatch, triggerUserMod, true);
	} else {
	    /*
	     * Multi-index pair case requires that we prevalidate the
	     * indices and sort from last to first so that deletes occur
	     * in the exact (unshifted) text. It also needs to handle
	     * partial and fully overlapping ranges. We have to do this
	     * with multiple passes.
	     */

	    TkTextIndex *indices, *ixStart, *ixEnd, *lastStart;
	    char *useIdx;
	    int lastUsed, i;

	    objc -= 2;
	    objv += 2;
	    indices = malloc((objc + 1)*sizeof(TkTextIndex));

	    /*
	     * First pass verifies that all indices are valid.
	     */

	    for (i = 0; i < objc; i++) {
		if (!TkTextGetIndexFromObj(interp, textPtr, objv[i], &indices[i])) {
		    result = TCL_ERROR;
		    free(indices);
		    goto done;
		}
	    }

	    /*
	     * Pad out the pairs evenly to make later code easier.
	     */

	    if (objc & 1) {
		indices[i] = indices[i - 1];
		TkTextIndexForwChars(textPtr, &indices[i], 1, &indices[i], COUNT_INDICES);
		objc += 1;
	    }
	    useIdx = malloc(objc);
	    memset(useIdx, 0, (unsigned) objc);

	    /*
	     * Do a decreasing order sort so that we delete the end ranges
	     * first to maintain index consistency.
	     */

	    qsort(indices, (unsigned) objc/2, 2*sizeof(TkTextIndex), TextIndexSortProc);
	    lastStart = NULL;
	    lastUsed = 0; /* otherwise GCC complains */

	    /*
	     * Second pass will handle bogus ranges (end < start) and
	     * overlapping ranges.
	     */

	    for (i = 0; i < objc; i += 2) {
		ixStart = &indices[i];
		ixEnd = &indices[i + 1];
		if (TkTextIndexCompare(ixEnd, ixStart) <= 0) {
		    continue;
		}
		if (lastStart) {
		    if (TkTextIndexCompare(ixStart, lastStart) == 0) {
			/*
			 * Start indices were equal, and the sort placed
			 * the longest range first, so skip this one.
			 */

			continue;
		    } else if (TkTextIndexCompare(lastStart, ixEnd) < 0) {
			/*
			 * The next pair has a start range before the end
			 * point of the last range. Constrain the delete
			 * range, but use the pointer values.
			 */

			*ixEnd = *lastStart;
			if (TkTextIndexCompare(ixEnd, ixStart) <= 0) {
			    continue;
			}
		    }
		}
		lastStart = ixStart;
		useIdx[i] = 1;
		lastUsed = i;
	    }

	    /*
	     * Final pass take the input from the previous and deletes the
	     * ranges which are flagged to be deleted.
	     */

	    for (i = 0; i < objc && ok; i += 2) {
		if (useIdx[i]) {
		    bool triggerUserMod = TestIfTriggerUserMod(sharedTextPtr, objv[i]);
		    bool triggerWatch = triggerUserMod || textPtr->triggerAlways;

		    if (triggerWatch) {
			TkTextSaveCursorIndex(textPtr);
		    }

		    /*
		     * We don't need to check the return value because all
		     * indices are preparsed above.
		     */

		    ok = DeleteIndexRange(NULL, textPtr, &indices[i], &indices[i + 1],
			    flags, true, triggerWatch, triggerWatch, triggerUserMod, i == lastUsed);
		}
	    }
	    free(indices);
	    free(useIdx);
	}

	if (!ok) {
	    return TCL_OK; /* widget has been destroyed */
	}
	break;
    }
    case TEXT_DLINEINFO: {
	int x, y, width, height, base;
	TkTextIndex index;

	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "index");
	    result = TCL_ERROR;
	    goto done;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[2], &index)) {
	    result = TCL_ERROR;
	    goto done;
	}
	if (TkTextGetDLineInfo(textPtr, &index, &x, &y, &width, &height, &base)) {
	    Tcl_Obj *listObj = Tcl_NewObj();

	    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(x));
	    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(y));
	    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(width));
	    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(height));
	    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(base));

	    Tcl_SetObjResult(interp, listObj);
	}
	break;
    }
    case TEXT_DUMP:
	result = TextDumpCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_EDIT:
	result = TextEditCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_GET: {
	Tcl_Obj *objPtr;
	int i, found;
	bool includeHyphens;
	bool visibleOnly;
	unsigned countOptions;
	const char *option;

	if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "?-option? ?--? index1 ?index2 ...?");
	    result = TCL_ERROR;
	    goto done;
	}

	objPtr = NULL;
	found = 0;
	includeHyphens = true;
	visibleOnly = false;
	countOptions = 0;
	i = 2;

	while (objc > i + 1 && (option = Tcl_GetString(objv[i]))[0] == '-') {
	    bool badOption = false;

	    i += 1;

	    if (option[1] == '-') {
	    	if (option[2] == '\0') {
		    break;
		}
		badOption = true;
	    } else if (++countOptions > 1) {
		i -= 1;
		break;
	    } else {
		switch (option[1]) {
		case 'c':
		    if (strcmp("-chars", option) != 0) {
			badOption = true;
		    }
		    break;
		case 't':
		    if (strcmp("-text", option) != 0) {
			badOption = true;
		    }
		    includeHyphens = false;
		    break;
		case 'd':
		    if (strcmp("-displaychars", option) == 0) {
			visibleOnly = true;
		    } else if (strcmp("-displaytext", option) == 0) {
			visibleOnly = true;
			includeHyphens = false;
		    } else {
			badOption = true;
		    }
		    break;
		default:
		    badOption = true;
		    break;
		}
	    }

	    if (badOption) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad option \"%s\": "
			"must be -chars, -displaychars, -displaytext, or -text", option));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "INDEX_OPTION", NULL);
		result = TCL_ERROR;
		goto done;
	    }
	}

	for (; i < objc; i += 2) {
	    TkTextIndex index1, index2;
	    Tcl_Obj *get;

	    if (!TkTextGetIndexFromObj(interp, textPtr, objv[i], &index1)) {
		if (objPtr) {
		    Tcl_DecrRefCount(objPtr);
		}
		result = TCL_ERROR;
		goto done;
	    }

	    if (i + 1 == objc) {
		TkTextIndexForwChars(textPtr, &index1, 1, &index2, COUNT_INDICES);
	    } else {
		if (!TkTextGetIndexFromObj(interp, textPtr, objv[i + 1], &index2)) {
		    if (objPtr) {
			Tcl_DecrRefCount(objPtr);
		    }
		    result = TCL_ERROR;
		    goto done;
		}
		if (TkTextIndexCompare(&index1, &index2) >= 0) {
		    goto done;
		}
	    }

	    /*
	     * We want to move the text we get from the window into the
	     * result, but since this could in principle be a megabyte or
	     * more, we want to do it efficiently!
	     */

	    get = TextGetText(textPtr, &index1, &index2, NULL, NULL, UINT_MAX,
		    visibleOnly, includeHyphens);

	    if (++found == 1) {
		Tcl_SetObjResult(interp, get);
	    } else {
		if (found == 2) {
		    /*
		     * Move the first item we put into the result into the
		     * first element of the list object.
		     */

		    objPtr = Tcl_NewObj();
		    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_GetObjResult(interp));
		}
		Tcl_ListObjAppendElement(NULL, objPtr, get);
	    }
	}
	if (found > 1) {
	    Tcl_SetObjResult(interp, objPtr);
	}
	break;
    }
    case TEXT_IMAGE:
	result = TkTextImageCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_INDEX: {
	TkTextIndex index;

	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "index");
	    result = TCL_ERROR;
	    goto done;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[2], &index)) {
	    result = TCL_ERROR;
	    goto done;
	}
	Tcl_SetObjResult(interp, TkTextNewIndexObj(&index));
	break;
    }
    case TEXT_INSERT:
    case TEXT_TK_TEXTINSERT: {
	TkTextIndex index;
	bool triggerUserMod, triggerWatch;
	bool destroyed;

	if (objc < 4) {
	    const char *args = (commandIndex == TEXT_TK_TEXTINSERT) ?
		"?-hyphentags tags? index chars ?tagList chars tagList ...?" :
		"index chars ?tagList chars tagList ...?";
	    Tcl_WrongNumArgs(interp, 2, objv, args);
	    result = TCL_ERROR;
	    goto done;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[2], &index)) {
	    result = TCL_ERROR;
	    goto done;
	}
	if (TestIfDisabled(interp, textPtr, &result)
		|| TestIfDead(interp, textPtr, &result)
		|| TestIfPerformingUndoRedo(interp, sharedTextPtr, &result)) {
	    goto done;
	}

	triggerUserMod = TestIfTriggerUserMod(sharedTextPtr, objv[2]);
	triggerWatch = triggerUserMod || textPtr->triggerAlways;

	if (triggerWatch) {
	    TkTextSaveCursorIndex(textPtr);
	}
	result = TextInsertCmd(textPtr, interp, objc - 3, objv + 3, &index, true, triggerWatch,
		triggerWatch, triggerUserMod, &destroyed, commandIndex == TEXT_TK_TEXTINSERT);
	if (destroyed) {
	    return result; /* widget has been destroyed */
	}
	break;
    }
    case TEXT_INSPECT:
	result = TextInspectCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_ISCLEAN: {
	bool discardSelection = false;
	const TkText *myTextPtr = textPtr;
	int i;

	for (i = 2; i < objc; ++i) {
	    char const * opt = Tcl_GetString(objv[i]);

	    if (strcmp(opt, "-overall") == 0) {
		myTextPtr = NULL;
	    } else if (strcmp(opt, "-discardselection") == 0) {
		discardSelection = true;
	    } else {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad option \"%s\": must be -overall", opt));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "BAD_OPTION", NULL);
		result = TCL_ERROR;
		goto done;
	    }
	}

	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(IsClean(sharedTextPtr, myTextPtr, discardSelection)));
	break;
    }
    case TEXT_ISDEAD:
    	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(TkTextIsDeadPeer(textPtr)));
	break;
    case TEXT_ISEMPTY: {
	bool overall = false;
	int i;

	for (i = 2; i < objc; ++i) {
	    char const * opt = Tcl_GetString(objv[i]);

	    if (strcmp(opt, "-overall") == 0) {
		overall = true;
	    } else {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad option \"%s\": must be -overall", opt));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "BAD_OPTION", NULL);
		result = TCL_ERROR;
		goto done;
	    }
	}

	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(IsEmpty(sharedTextPtr, overall ? NULL : textPtr)));
	break;
    }
    case TEXT_LINENO: {
	TkTextIndex index;
	int lineno;

	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "index");
	    result = TCL_ERROR;
	    goto done;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[2], &index)) {
	    result = TCL_ERROR;
	    goto done;
	}
	lineno = TkTextIsDeadPeer(textPtr) ? 0 : TkTextIndexGetLineNumber(&index, textPtr) + 1;
	Tcl_SetObjResult(interp, Tcl_NewIntObj(lineno));
	break;
    }
    case TEXT_LOAD: {
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "textcontent");
	    result = TCL_ERROR;
	    goto done;
	}
	if (TestIfPerformingUndoRedo(interp, sharedTextPtr, &result)) {
	    goto done;
	}
	ClearText(textPtr, false);
	TkTextRelayoutWindow(textPtr, TK_TEXT_LINE_GEOMETRY);
	if ((result = TkBTreeLoad(textPtr, objv[2])) != TCL_OK) {
	    ClearText(textPtr, false);
	}
	break;
    }
    case TEXT_MARK:
	result = TkTextMarkCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_PEER:
	result = TextPeerCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_PENDINGSYNC: {
        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
            result = TCL_ERROR;
            goto done;
        }
	if (!sharedTextPtr->allowUpdateLineMetrics) {
	    ProcessConfigureNotify(textPtr, true);
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(TkTextPendingSync(textPtr)));
        break;
    }
    case TEXT_REPLACE:
    case TEXT_TK_TEXTREPLACE: {
	TkTextIndex indexFrom, indexTo, index;
	bool triggerUserMod, triggerWatch;
	bool destroyed;

	if (objc < 5) {
	    Tcl_WrongNumArgs(interp, 2, objv, "index1 index2 chars ?tagList chars tagList ...?");
	    result = TCL_ERROR;
	    goto done;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[2], &indexFrom)
		|| !TkTextGetIndexFromObj(interp, textPtr, objv[3], &indexTo)) {
	    result = TCL_ERROR;
	    goto done;
	}
	if (TkTextIndexCompare(&indexFrom, &indexTo) > 0) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "index \"%s\" before \"%s\" in the text",
		    Tcl_GetString(objv[3]), Tcl_GetString(objv[2])));
	    Tcl_SetErrorCode(interp, "TK", "TEXT", "INDEX_ORDER", NULL);
	    result = TCL_ERROR;
	    goto done;
	}
	if (TestIfDisabled(interp, textPtr, &result) || TestIfDead(interp, textPtr, &result)) {
	    goto done;
	}

	destroyed = false;
	triggerUserMod = TestIfTriggerUserMod(sharedTextPtr, objv[2]);
	triggerWatch = triggerUserMod || textPtr->triggerAlways;

	/*
	 * The 'replace' operation is quite complex to do correctly,
	 * because we want a number of criteria to hold:
	 *
	 * 1.  The insertion point shouldn't move, unless it is within the
	 *	   deleted range. In this case it should end up after the new
	 *	   text.
	 *
	 * 2.  The window should not change the text it shows - should not
	 *	   scroll vertically - unless the result of the replace is
	 *	   that the insertion position which used to be on-screen is
	 *	   now off-screen.
	 */

	TkTextIndexSave(&textPtr->topIndex);
	if (triggerWatch) {
	    TkTextSaveCursorIndex(textPtr);
	}

	TkTextMarkSegToIndex(textPtr, textPtr->insertMarkPtr, &index);
	if (TkTextIndexCompare(&indexFrom, &index) < 0
		&& TkTextIndexCompare(&index, &indexTo) <= 0) {
	    /*
	     * The insertion point is inside the range to be replaced, so
	     * we have to do some calculations to ensure it doesn't move
	     * unnecessarily.
	     */

	    int deleteInsertOffset, insertLength, j;

	    insertLength = 0;
	    for (j = 4; j < objc; j += 2) {
		insertLength += Tcl_GetCharLength(objv[j]);
	    }

	    /*
	     * Calculate 'deleteInsertOffset' as an offset we will apply
	     * to the insertion point after this operation.
	     */

	    deleteInsertOffset = CountIndices(textPtr, &indexFrom, &index, COUNT_CHARS);
	    if (deleteInsertOffset > insertLength) {
		deleteInsertOffset = insertLength;
	    }

	    result = TextReplaceCmd(textPtr, interp, &indexFrom, &indexTo, objc, objv, false,
		    triggerWatch, triggerUserMod, &destroyed, commandIndex == TEXT_TK_TEXTREPLACE);
	    if (destroyed) { return result; /* widget has been destroyed */ }

	    if (result == TCL_OK) {
		/*
		 * Move the insertion position to the correct place.
		 */

		TkTextIndexForwChars(textPtr, &indexFrom, deleteInsertOffset, &index, COUNT_INDICES);
		TkBTreeUnlinkSegment(sharedTextPtr, textPtr->insertMarkPtr);
		TkBTreeLinkSegment(sharedTextPtr, textPtr->insertMarkPtr, &index);
		textPtr->insertIndex = index;
	    }
	} else {
	    result = TextReplaceCmd(textPtr, interp, &indexFrom, &indexTo, objc, objv, false,
		    triggerWatch, triggerUserMod, &destroyed, commandIndex == TEXT_TK_TEXTREPLACE);
	    if (destroyed) { return result; /* widget has been destroyed */ }
	}
	if (result == TCL_OK) {
	    /*
	     * Now ensure the top-line is in the right place.
	     */

	    if (!TkTextIndexRebuild(&textPtr->topIndex)) {
		TkTextSetYView(textPtr, &textPtr->topIndex, TK_TEXT_NOPIXELADJUST);
	    }
	}
	break;
    }
    case TEXT_SCAN:
	result = TkTextScanCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_SEARCH:
	result = TextSearchCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_SEE:
	result = TkTextSeeCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_SYNC: {
	bool wrongNumberOfArgs = false;

	if (objc == 3 || objc == 4) {
	    const char *option = Tcl_GetString(objv[2]);
	    if (*option != '-') {
		wrongNumberOfArgs = true;
	    } else if (strncmp(option, "-command", objv[2]->length) != 0) {
		Tcl_AppendResult(interp, "wrong option \"", option,
			"\": should be \"-command\"", NULL);
		result = TCL_ERROR;
		goto done;
	    }
	} else if (objc != 2) {
	    wrongNumberOfArgs = true;
	}
	if (wrongNumberOfArgs) {
	    Tcl_WrongNumArgs(interp, 2, objv, "?-command ?command??");
	    result = TCL_ERROR;
	    goto done;
	}
	if (!sharedTextPtr->allowUpdateLineMetrics) {
	    ProcessConfigureNotify(textPtr, true);
	}
	if (objc == 3) {
	    if (textPtr->afterSyncCmd) {
		Tcl_SetObjResult(interp, textPtr->afterSyncCmd);
	    }
	} else if (objc == 4) {
	    Tcl_Obj *cmd = objv[3];
	    const char *script = Tcl_GetString(cmd);
	    bool append = false;

	    if (*script == '+') {
		script += 1;
		append = true;
	    }

	    if (!textPtr->afterSyncCmd) {
		if (append) {
		    cmd = Tcl_NewStringObj(script, -1);
		}
		Tcl_IncrRefCount(textPtr->afterSyncCmd = cmd);
	    } else {
		if (!append && *script == '\0') {
		    if (textPtr->pendingAfterSync) {
			Tcl_CancelIdleCall(RunAfterSyncCmd, (ClientData) textPtr);
			textPtr->pendingAfterSync = false;
		    }
		    cmd = NULL;
		} else {
		    if (append) {
			cmd = AppendScript(Tcl_GetString(textPtr->afterSyncCmd), script);
		    }
		    Tcl_IncrRefCount(cmd);
		}
		Tcl_DecrRefCount(textPtr->afterSyncCmd);
		textPtr->afterSyncCmd = cmd;
	    }
	    if (!textPtr->pendingAfterSync) {
		textPtr->pendingAfterSync = true;
		if (!TkTextPendingSync(textPtr)) {
		    Tcl_DoWhenIdle(RunAfterSyncCmd, (ClientData) textPtr);
		}
	    }
	} else {
	    textPtr->sendSyncEvent = true;

	    if (!TkTextPendingSync(textPtr)) {
		/*
		 * There is nothing to sync, so fire the <<WidgetViewSync>> event,
		 * because nobody else will do this when no update is pending.
		 */
		TkTextGenerateWidgetViewSyncEvent(textPtr, false);
	    } else {
		UpdateLineMetrics(textPtr, 0, TkBTreeNumLines(sharedTextPtr->tree, textPtr));
	    }
	}
	break;
    }
    case TEXT_TAG:
	result = TkTextTagCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_WATCH: {
	Tcl_Obj *cmd = textPtr->watchCmd;

	result = TextWatchCmd(textPtr, interp, objc, objv);
	if (cmd) {
	    Tcl_SetObjResult(interp, cmd);
	    Tcl_DecrRefCount(cmd);
	}
    	break;
    }
    case TEXT_WINDOW:
	result = TkTextWindowCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_XVIEW:
	result = TkTextXviewCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_YVIEW:
	result = TkTextYviewCmd(textPtr, interp, objc, objv);
	break;
    }

  done:
    if (--textPtr->refCount == 0) {
	bool sharedIsReleased = textPtr->sharedIsReleased;

	free(textPtr);
	if (sharedIsReleased) {
	    return result;
	}
	textPtr = NULL;
    } else if (textPtr->watchCmd) {
	TkTextTriggerWatchCursor(textPtr);
    }
    if (sharedTextPtr->undoStackEvent) {
	TriggerUndoStackEvent(sharedTextPtr);
    }
    sharedTextPtr->undoStackEvent = oldUndoStackEvent;

    if (textPtr && textPtr->syncTime == 0) {
	UpdateLineMetrics(textPtr, 0, TkBTreeNumLines(sharedTextPtr->tree, textPtr));
	TK_BTREE_DEBUG(TkBTreeCheck(sharedTextPtr->tree));
    }

    return result;
}

/*
 *--------------------------------------------------------------
 *
 * IsEmpty --
 *
 *	Test whether this widget is empty. The widget is empty
 *	if it contains exact two single newline characters.
 *
 * Results:
 *	Returns true if the widget is empty, and false otherwise.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static bool
DoesNotContainTextSegments(
    const TkTextSegment *segPtr1,
    const TkTextSegment *segPtr2)
{
    for ( ; segPtr1 != segPtr2; segPtr1 = segPtr1->nextPtr) {
	if (segPtr1->size > 0) {
	    return !segPtr1->nextPtr; /* ignore trailing newline */
	}
    }

    return true;
}

static bool
IsEmpty(
    const TkSharedText *sharedTextPtr,
    const TkText *textPtr)		/* Can be NULL. */
{
    TkTextSegment *startMarker;
    TkTextSegment *endMarker;

    assert(sharedTextPtr);

    if (TkBTreeNumLines(sharedTextPtr->tree, textPtr) > 1) {
	return false;
    }

    if (textPtr) {
	startMarker = textPtr->startMarker;
	endMarker = textPtr->endMarker;
    } else {
	startMarker = sharedTextPtr->startMarker;
	endMarker = sharedTextPtr->endMarker;
    }

    return DoesNotContainTextSegments(startMarker, endMarker);
}

/*
 *--------------------------------------------------------------
 *
 * IsClean --
 *
 *	Test whether this widget is clean. The widget is clean
 *	if it is empty, if no mark is set, and if the solely
 *	newline of this widget is untagged.
 *
 * Results:
 *	Returns true if the widget is clean, and false otherwise.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static bool
ContainsAnySegment(
    const TkTextSegment *segPtr1,
    const TkTextSegment *segPtr2)
{
    for ( ; segPtr1 != segPtr2; segPtr1 = segPtr1->nextPtr) {
	if (segPtr1->size > 0 || segPtr1->normalMarkFlag) {
	    return !!segPtr1->nextPtr; /* ignore trailing newline */
	}
    }

    return false;
}

static bool
IsClean(
    const TkSharedText *sharedTextPtr,
    const TkText *textPtr,		/* Can be NULL. */
    bool discardSelection)
{
    const TkTextTagSet *tagInfoPtr;
    const TkTextSegment *startMarker;
    const TkTextSegment *endMarker;
    const TkTextLine *endLine;

    assert(sharedTextPtr);

    if (TkBTreeNumLines(sharedTextPtr->tree, textPtr) > 1) {
	return false;
    }

    if (textPtr) {
	startMarker = textPtr->startMarker;
	endMarker = textPtr->endMarker;
    } else {
	startMarker = sharedTextPtr->startMarker;
	endMarker = sharedTextPtr->endMarker;
    }

    if (ContainsAnySegment(startMarker, endMarker)) {
	return false;
    }

    endLine = endMarker->sectionPtr->linePtr;

    if (!textPtr && ContainsAnySegment(endLine->segPtr, NULL)) {
	/* This widget contains any mark on very last line. */
	return false;
    }

    tagInfoPtr = endLine->prevPtr->lastPtr->tagInfoPtr;

    return discardSelection ?
	    TkTextTagBitContainsSet(sharedTextPtr->selectionTags, tagInfoPtr) :
	    tagInfoPtr == sharedTextPtr->emptyTagInfoPtr;
}

/*
 *--------------------------------------------------------------
 *
 * TkTextTestRelation --
 *
 *	Given a relation (>0 for greater, =0 for equal, and <0 for
 *	less), this function computes whether the given operator
 *	satisfies this relation.
 *
 * Results:
 *	Returns 1 if the relation will be satsified, 0 if it will
 *	not be satisifed, and -1 if the operator is invalid.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static int
BadComparisonOperator(
    Tcl_Interp *interp,
    char const *op)
{
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "bad comparison operator \"%s\": must be <, <=, ==, >=, >, or !=", op));
    Tcl_SetErrorCode(interp, "TK", "VALUE", "COMPARISON", NULL);
    return -1;
}

int
TkTextTestRelation(
    Tcl_Interp *interp,		/* Current interpreter. */
    int relation,		/* Test this relation... */
    char const *op)		/* ...whether it will be satisifed by this operator. */
{
    int value;

    if (op[0] == '<') {
	value = (relation < 0);
	if (op[1] == '=' && op[2] == 0) {
	    value = (relation <= 0);
	} else if (op[1] != 0) {
	    return BadComparisonOperator(interp, op);
	}
    } else if (op[0] == '>') {
	value = (relation > 0);
	if (op[1] == '=' && op[2] == 0) {
	    value = (relation >= 0);
	} else if (op[1] != 0) {
	    return BadComparisonOperator(interp, op);
	}
    } else if (op[0] == '=' && op[1] == '=' && op[2] == 0) {
	value = (relation == 0);
    } else if (op[0] == '!' && op[1] == '=' && op[2] == 0) {
	value = (relation != 0);
    } else {
	return BadComparisonOperator(interp, op);
    }

    return value;
}

/*
 *--------------------------------------------------------------
 *
 * TextWatchCmd --
 *
 *	This function is invoked to process the "text watch" Tcl command. See
 *	the user documentation for details on what it does.
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
TextWatchCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    TkSharedText *sharedTextPtr;

    if (objc > 4) {
	/* NOTE: avoid trigraph "??-" in string. */
	Tcl_WrongNumArgs(interp, 4, objv, "\?\?-always? commandPrefix?");
	return TCL_ERROR;
    }

    sharedTextPtr = textPtr->sharedTextPtr;

    if (objc <= 2) {
	TkText *tPtr;

	if (textPtr->watchCmd) {
	    textPtr->triggerAlways = false;
	    textPtr->watchCmd = NULL;
	}
	sharedTextPtr->triggerWatchCmd = false;
	for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
	    if (tPtr->watchCmd) {
		sharedTextPtr->triggerWatchCmd = true;
	    }
	}
    } else {
	const char *script;
	Tcl_Obj *cmd;
	int argnum = 2;

	if (objc == 4) {
	    if (strcmp(Tcl_GetString(objv[2]), "-always") != 0) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad option \"%s\": must be -always", Tcl_GetString(objv[2])));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "WATCH_OPTION", NULL);
		return TCL_ERROR;
	    }
	    textPtr->triggerAlways = true;
	    argnum = 3;
	}

	cmd = objv[argnum];
	script = Tcl_GetString(cmd);

	if (*script == '+') {
	    script += 1;
	    if (textPtr->watchCmd) {
		cmd = AppendScript(Tcl_GetString(textPtr->watchCmd), script);
	    } else {
		cmd = Tcl_NewStringObj(script, -1);
	    }
	} else if (argnum == 2) {
	    textPtr->triggerAlways = false;
	}

	textPtr->sharedTextPtr->triggerWatchCmd = true;
	Tcl_IncrRefCount(textPtr->watchCmd = cmd);
    }

    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * TextPeerCmd --
 *
 *	This function is invoked to process the "text peer" Tcl command. See
 *	the user documentation for details on what it does.
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
TextPeerCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Tk_Window tkwin = textPtr->tkwin;
    int index;

    static const char *const peerOptionStrings[] = { "create", "names", NULL };
    enum peerOptions { PEER_CREATE, PEER_NAMES };

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "option ?arg ...?");
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObjStruct(interp, objv[2], peerOptionStrings,
	    sizeof(char *), "peer option", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }

    switch ((enum peerOptions) index) {
    case PEER_CREATE:
	if (objc < 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "pathName ?-option value ...?");
	    return TCL_ERROR;
	}
	return CreateWidget(textPtr->sharedTextPtr, tkwin, interp, textPtr, objc - 2, objv + 2);
    case PEER_NAMES: {
	TkText *tPtr = textPtr->sharedTextPtr->peers;
	Tcl_Obj *peersObj;

	if (objc > 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, NULL);
	    return TCL_ERROR;
	}
	peersObj = Tcl_NewObj();
	while (tPtr) {
	    if (tPtr != textPtr) {
		Tcl_ListObjAppendElement(NULL, peersObj, TkNewWindowObj(tPtr->tkwin));
	    }
	    tPtr = tPtr->next;
	}
	Tcl_SetObjResult(interp, peersObj);
    }
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TextReplaceCmd --
 *
 *	This function is invoked to process part of the "replace" widget
 *	command for text widgets.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *	If 'viewUpdate' is false, then textPtr->topIndex may no longer be a
 *	valid index after this function returns. The caller is responsible for
 *	ensuring a correct index is in place.
 *
 *----------------------------------------------------------------------
 */

static int
TextReplaceCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    const TkTextIndex *indexFromPtr,
				/* Index from which to replace. */
    const TkTextIndex *indexToPtr,
				/* Index to which to replace. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[],	/* Argument objects. */
    bool viewUpdate,		/* Update vertical view if set. */
    bool triggerWatch,		/* Should we trigger the watch command? */
    bool userFlag,		/* Trigger due to user modification? */
    bool *destroyed,		/* Store whether the widget has been destroyed. */
    bool parseHyphens)		/* Should we parse hyphens (tk_textReplace)? */
{
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    bool *stillExisting = sharedTextPtr->stillExisting;
    int origAutoSep = sharedTextPtr->autoSeparators;
    int result = TCL_OK;
    TkTextIndex indexTmp;
    bool notDestroyed;
    bool existing;

    assert(destroyed);
    assert(!TkTextIsDeadPeer(textPtr));

    if (!stillExisting) {
	sharedTextPtr->stillExisting = &existing;
	existing = true;
    }

    /*
     * Perform the deletion and insertion, but ensure no undo-separator is
     * placed between the two operations. Since we are using the helper
     * functions 'DeleteIndexRange' and 'TextInsertCmd' we have to pretend
     * that the autoSeparators setting is off, so that we don't get an
     * undo-separator between the delete and insert.
     */

    if (sharedTextPtr->undoStack) {
	sharedTextPtr->autoSeparators = false;
	if (origAutoSep && sharedTextPtr->lastEditMode != TK_TEXT_EDIT_REPLACE) {
	    PushRetainedUndoTokens(sharedTextPtr);
	    TkTextUndoPushSeparator(sharedTextPtr->undoStack, true);
	    sharedTextPtr->lastUndoTokenType = -1;
	}
    }

    /* The line and segment storage may change when deleting. */
    indexTmp = *indexFromPtr;
    TkTextIndexSave(&indexTmp);

    notDestroyed = DeleteIndexRange(NULL, textPtr, indexFromPtr, indexToPtr,
	    0, viewUpdate, triggerWatch, false, userFlag, true);

    if (notDestroyed) {
	TkTextIndexRebuild(&indexTmp);
	result = TextInsertCmd(textPtr, interp, objc - 4, objv + 4, &indexTmp,
		viewUpdate, false, triggerWatch, userFlag, destroyed, parseHyphens);
	if (*destroyed) {
	    notDestroyed = false;
	}
    } else {
	*destroyed = true;
    }

    if (*sharedTextPtr->stillExisting) {
	if (sharedTextPtr->undoStack) {
	    sharedTextPtr->lastEditMode = TK_TEXT_EDIT_REPLACE;
	    sharedTextPtr->autoSeparators = origAutoSep;
	}
	if (sharedTextPtr->stillExisting == &existing) {
	    sharedTextPtr->stillExisting = NULL;
	}
    }

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TextIndexSortProc --
 *
 *	This function is called by qsort when sorting an array of indices in
 *	*decreasing* order (last to first).
 *
 * Results:
 *	The return value is less than zero if the first argument should be before
 *	the second element, 0 if it's equivalent, and greater than zero if it should
 *	be after the second element.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TextIndexSortProc(
    const void *first,		/* Elements to be compared. */
    const void *second)
{
    TkTextIndex *pair1 = (TkTextIndex *) first;
    TkTextIndex *pair2 = (TkTextIndex *) second;
    int cmp = TkTextIndexCompare(&pair1[1], &pair2[1]);

    if (cmp == 0) {
	/*
	 * If the first indices were equal, we want the second index of the
	 * pair also to be the greater. Use pointer magic to access the second
	 * index pair.
	 */

	cmp = TkTextIndexCompare(&pair1[0], &pair2[0]);
    }

    return -cmp;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeEmbeddedWindows --
 *
 *	Free up any embedded windows which belong to this widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All embedded windows of this widget will be freed.
 *
 *----------------------------------------------------------------------
 */

static void
FreeEmbeddedWindows(
    TkText *textPtr)	/* The concerned text widget. */
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;

    for (hPtr = Tcl_FirstHashEntry(&sharedTextPtr->windowTable, &search);
	    hPtr;
	    hPtr = Tcl_NextHashEntry(&search)) {
	TkTextSegment *ewPtr = Tcl_GetHashValue(hPtr);
	TkTextEmbWindowClient *client = ewPtr->body.ew.clients;
	TkTextEmbWindowClient **prev = &ewPtr->body.ew.clients;

	while (client) {
	    TkTextEmbWindowClient *next = client->next;
	    if (client->textPtr == textPtr && client->hPtr == hPtr) {
		TkTextWinFreeClient(hPtr, client);
		*prev = next;
	    } else {
		prev = &client->next;
	    }
	    client = next;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ClearText --
 *
 *	This function is invoked when we reset a text widget to it's intitial
 *	state, but without resetting options. We will free up many of the
 *	internal structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Almost everything associated with the text content is cleared.
 *	Note that all the peers of the shared structure will be cleared.
 *
 *----------------------------------------------------------------------
 */

static void
ClearRetainedUndoTokens(
    TkSharedText *sharedTextPtr)
{
    unsigned i;

    assert(sharedTextPtr);

    for (i = 0; i < sharedTextPtr->undoTagListCount; ++i) {
	TkTextReleaseUndoTagToken(sharedTextPtr, sharedTextPtr->undoTagList[i]);
    }

    for (i = 0; i < sharedTextPtr->undoMarkListCount; ++i) {
	TkTextReleaseUndoMarkTokens(sharedTextPtr, &sharedTextPtr->undoMarkList[i]);
    }

    sharedTextPtr->undoTagListCount = 0;
    sharedTextPtr->undoMarkListCount = 0;
}

static void
ClearText(
    TkText *textPtr,		/* Clean up this text widget. */
    bool clearTags)		/* Also clear all tags? */
{
    TkTextSegment *retainedMarks;
    TkTextIndex startIndex;
    TkText *tPtr;
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    unsigned oldEpoch = TkBTreeEpoch(sharedTextPtr->tree);
    bool steadyMarks = textPtr->sharedTextPtr->steadyMarks;
    bool debug = tkBTreeDebug;

    tkBTreeDebug = false; /* debugging is not wanted here */

    for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
	/*
	 * Always clean up the widget-specific tags first. Common tags (i.e. most)
	 * will only be cleaned up when the shared structure is cleaned up.
	 *
	 * We also need to clean up widget-specific marks ('insert', 'current'),
	 * since otherwise marks will never disappear from the B-tree.
	 *
	 * Do not clear the after sync commands, otherwise the widget may hang.
	 */

	tPtr->refCount += 1;
	TkBTreeUnlinkSegment(sharedTextPtr, tPtr->insertMarkPtr);
	TkBTreeUnlinkSegment(sharedTextPtr, tPtr->currentMarkPtr);
	if (clearTags) {
	    TkTextFreeAllTags(tPtr);
	}
	TkQTreeDestroy(&tPtr->imageBboxTree);
	FreeEmbeddedWindows(tPtr);
	TkTextFreeDInfo(tPtr);
	textPtr->dInfoPtr = NULL;
	textPtr->dontRepick = false;
	tPtr->abortSelections = true;
	tPtr->configureBboxTree = false;
	tPtr->hoveredImageArrSize = 0;
	tPtr->currNearbyFlag = -1;
	tPtr->refCount -= 1;
	tPtr->startLine = NULL;
	tPtr->endLine = NULL;

	if (tPtr->startMarker->refCount == 1) {
	    assert(textPtr->startMarker != textPtr->sharedTextPtr->startMarker);
	    TkBTreeUnlinkSegment(sharedTextPtr, tPtr->startMarker);
	    FREE_SEGMENT(tPtr->startMarker);
	    DEBUG_ALLOC(tkTextCountDestroySegment++);
	    (tPtr->startMarker = sharedTextPtr->startMarker)->refCount += 1;
	}
	if (tPtr->endMarker->refCount == 1) {
	    assert(textPtr->endMarker != textPtr->sharedTextPtr->endMarker);
	    TkBTreeUnlinkSegment(sharedTextPtr, tPtr->endMarker);
	    FREE_SEGMENT(tPtr->endMarker);
	    DEBUG_ALLOC(tkTextCountDestroySegment++);
	    (tPtr->endMarker = sharedTextPtr->endMarker)->refCount += 1;
	}
    }

    ClearRetainedUndoTokens(sharedTextPtr);
    TkBTreeUnlinkSegment(sharedTextPtr, sharedTextPtr->startMarker);
    TkBTreeUnlinkSegment(sharedTextPtr, sharedTextPtr->endMarker);
    sharedTextPtr->startMarker->nextPtr = sharedTextPtr->startMarker->prevPtr = NULL;
    sharedTextPtr->endMarker->nextPtr = sharedTextPtr->endMarker->prevPtr = NULL;
    TkBTreeDestroy(sharedTextPtr->tree);
    retainedMarks = TkTextFreeMarks(sharedTextPtr, true);
    Tcl_DeleteHashTable(&sharedTextPtr->imageTable);
    Tcl_DeleteHashTable(&sharedTextPtr->windowTable);

    if (sharedTextPtr->imageBindingTable) {
	Tk_DeleteBindingTable(sharedTextPtr->imageBindingTable);
    }

    if (clearTags) {
	Tcl_DeleteHashTable(&sharedTextPtr->tagTable);
	if (sharedTextPtr->tagBindingTable) {
	    Tk_DeleteBindingTable(sharedTextPtr->tagBindingTable);
	}
	sharedTextPtr->numMotionEventBindings = 0;
	sharedTextPtr->numElisionTags = 0;
    }

    /*
     * Rebuild the internal structures.
     */

    Tcl_InitHashTable(&sharedTextPtr->windowTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&sharedTextPtr->imageTable, TCL_STRING_KEYS);
    TkTextUndoResetStack(sharedTextPtr->undoStack);
    TkBitClear(sharedTextPtr->elisionTags);
    TkBitClear(sharedTextPtr->selectionTags);
    TkBitClear(sharedTextPtr->dontUndoTags);
    TkBitClear(sharedTextPtr->affectDisplayTags);
    TkBitClear(sharedTextPtr->notAffectDisplayTags);
    TkBitClear(sharedTextPtr->affectDisplayNonSelTags);
    TkBitClear(sharedTextPtr->affectGeometryTags);
    TkBitClear(sharedTextPtr->affectGeometryNonSelTags);
    TkBitClear(sharedTextPtr->affectLineHeightTags);
    sharedTextPtr->imageBindingTable = NULL;
    sharedTextPtr->isAltered = false;
    sharedTextPtr->isModified = false;
    sharedTextPtr->isIrreversible = false;
    sharedTextPtr->userHasSetModifiedFlag = false;
    sharedTextPtr->haveToSetCurrentMark = false;
    sharedTextPtr->undoLevel = 0;
    sharedTextPtr->imageCount = 0;
    sharedTextPtr->tree = TkBTreeCreate(sharedTextPtr, oldEpoch + 1);
    sharedTextPtr->insertDeleteUndoTokenCount = 0;

    if (clearTags) {
	sharedTextPtr->tagInfoSize = 0;
	sharedTextPtr->tagBindingTable = NULL;
	sharedTextPtr->numTags = 0;
	sharedTextPtr->numEnabledTags = sharedTextPtr->numPeers; /* because the "sel" tag will survive */
	Tcl_InitHashTable(&sharedTextPtr->tagTable, TCL_STRING_KEYS);
	TkBitClear(sharedTextPtr->usedTags);
	DEBUG(memset(sharedTextPtr->tagLookup, 0,
		TkBitSize(sharedTextPtr->usedTags)*sizeof(TkTextTag *)));
    }

    for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
	TkTextCreateDInfo(tPtr);
	TkBTreeAddClient(sharedTextPtr->tree, tPtr, tPtr->lineHeight);
	TkTextIndexSetupToStartOfText(&startIndex, tPtr, sharedTextPtr->tree);
	TkTextSetYView(tPtr, &startIndex, 0);
	sharedTextPtr->tagLookup[tPtr->selTagPtr->index] = tPtr->selTagPtr;
	TkBitSet(sharedTextPtr->usedTags, tPtr->selTagPtr->index);
	tPtr->haveToSetCurrentMark = false;
	TkBTreeLinkSegment(sharedTextPtr, tPtr->insertMarkPtr, &startIndex);
	TkBTreeLinkSegment(sharedTextPtr, tPtr->currentMarkPtr, &startIndex);
	tPtr->currentMarkIndex = startIndex;
    }

    sharedTextPtr->steadyMarks = false;
    while (retainedMarks) {
	TkTextSegment *nextPtr = retainedMarks->nextPtr;
	TkTextIndexSetupToStartOfText(&startIndex, NULL, sharedTextPtr->tree);
	TkBTreeLinkSegment(sharedTextPtr, retainedMarks, &startIndex);
	retainedMarks = nextPtr;
    }
    sharedTextPtr->steadyMarks = steadyMarks;

    TkTextResetDInfo(textPtr);
    sharedTextPtr->lastEditMode = TK_TEXT_EDIT_OTHER;
    sharedTextPtr->lastUndoTokenType = -1;

    if (debug) {
	tkBTreeDebug = true;
	TkBTreeCheck(sharedTextPtr->tree);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyText --
 *
 *	This function is invoked when we receive a destroy event to clean up
 *	the internal structure of a text widget. We will free up most of the
 *	internal structure and delete the associated Tcl command. If there are
 *	no outstanding references to the widget, we also free up the textPtr
 *	itself.
 *
 *	The widget has already been flagged as deleted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Either everything or almost everything associated with the text is
 *	freed up.
 *
 *----------------------------------------------------------------------
 */

static void
DestroyText(
    TkText *textPtr)		/* Info about text widget. */
{
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    TkTextStringList *listPtr;
    bool debug = tkBTreeDebug;

    tkBTreeDebug = false; /* debugging is not wanted here */

    /*
     * Firstly, remove pending idle commands, and free the array.
     */

    if (textPtr->pendingAfterSync) {
	Tcl_CancelIdleCall(RunAfterSyncCmd, (ClientData) textPtr);
	textPtr->pendingAfterSync = false;
    }
    if (textPtr->pendingFireEvent) {
	Tcl_CancelIdleCall(FireWidgetViewSyncEvent, (ClientData) textPtr);
	textPtr->pendingFireEvent = false;
    }
    if (textPtr->afterSyncCmd) {
	Tcl_DecrRefCount(textPtr->afterSyncCmd);
    }

    /*
     * Free up all the stuff that requires special handling. We have already
     * called let Tk_FreeConfigOptions to handle all the standard
     * option-related stuff (and so none of that exists when we are called).
     *
     * Special note: free up display-related information before deleting the
     * B-tree, since display-related stuff may refer to stuff in the B-tree.
     */

    TkTextFreeDInfo(textPtr);
    textPtr->dInfoPtr = NULL;
    textPtr->undo = false;

    /*
     * Always clean up the widget-specific tags first. Common tags (i.e. most)
     * will only be cleaned up when the shared structure is cleaned up.
     *
     * At first clean up the array of bounding boxes for the images.
     */

    TkQTreeDestroy(&textPtr->imageBboxTree);

    /*
     * Unset all the variables bound to this widget.
     */

    listPtr = textPtr->varBindingList;
    while (listPtr) {
	TkTextStringList *nextPtr = listPtr->nextPtr;

	Tcl_UnsetVar2(textPtr->interp, Tcl_GetString(listPtr->strObjPtr), NULL, TCL_GLOBAL_ONLY);
	Tcl_DecrRefCount(listPtr->strObjPtr);
	free(listPtr);
	listPtr = nextPtr;
    }

    /*
     * Unset the watch command.
     */

    if (textPtr->watchCmd) {
	Tcl_DecrRefCount(textPtr->watchCmd);
    }
    TextWatchCmd(textPtr, NULL, 0, NULL);

    /*
     * We also need to clean up widget-specific marks ('insert', 'current'),
     * since otherwise marks will never disappear from the B-tree.
     */

    TkTextDeleteTag(textPtr, textPtr->selTagPtr, NULL);
    TkBTreeUnlinkSegment(sharedTextPtr, textPtr->insertMarkPtr);
    FREE_SEGMENT(textPtr->insertMarkPtr);
    DEBUG_ALLOC(tkTextCountDestroySegment++);
    TkBTreeUnlinkSegment(sharedTextPtr, textPtr->currentMarkPtr);
    FREE_SEGMENT(textPtr->currentMarkPtr);
    DEBUG_ALLOC(tkTextCountDestroySegment++);
    FreeEmbeddedWindows(textPtr);

    /*
     * Clean up the -start/-end markers, do this after cleanup of other segments (not before).
     */

    if (textPtr->startMarker->refCount == 1) {
	assert(textPtr->startMarker != sharedTextPtr->startMarker);
	TkBTreeUnlinkSegment(sharedTextPtr, textPtr->startMarker);
	FREE_SEGMENT(textPtr->startMarker);
	DEBUG_ALLOC(tkTextCountDestroySegment++);
    } else {
	DEBUG(textPtr->startMarker->refCount -= 1);
    }
    if (textPtr->endMarker->refCount == 1) {
	assert(textPtr->endMarker != sharedTextPtr->endMarker);
	TkBTreeUnlinkSegment(sharedTextPtr, textPtr->endMarker);
	FREE_SEGMENT(textPtr->endMarker);
	DEBUG_ALLOC(tkTextCountDestroySegment++);
    } else {
	DEBUG(textPtr->endMarker->refCount -= 1);
    }

    /*
     * Now we've cleaned up everything of relevance to us in the B-tree, so we
     * disassociate ourselves from it.
     *
     * When the refCount reaches zero, it's time to clean up the shared
     * portion of the text widget.
     */

    sharedTextPtr->refCount -= 1;

    if (sharedTextPtr->refCount > 0) {
	sharedTextPtr->numPeers -= 1;

	/*
	 * No need to call 'TkBTreeRemoveClient' first, since this will do
	 * everything in one go, more quickly.
	 */

	TkBTreeRemoveClient(sharedTextPtr->tree, textPtr);

	/*
	 * Remove ourselves from the peer list.
	 */

	if (sharedTextPtr->peers == textPtr) {
	    sharedTextPtr->peers = textPtr->next;
	} else {
	    TkText *nextPtr = sharedTextPtr->peers;
	    while (nextPtr) {
		if (nextPtr->next == textPtr) {
		    nextPtr->next = textPtr->next;
		    break;
		}
		nextPtr = nextPtr->next;
	    }
	}
    } else {
	/* Prevent that this resource will be released too early. */
	textPtr->refCount += 1;

	ClearRetainedUndoTokens(sharedTextPtr);
	TkTextUndoDestroyStack(&sharedTextPtr->undoStack);
	free(sharedTextPtr->undoTagList);
	free(sharedTextPtr->undoMarkList);
	TkBTreeDestroy(sharedTextPtr->tree);
	assert(sharedTextPtr->startMarker->refCount == 1);
	FREE_SEGMENT(sharedTextPtr->startMarker);
	DEBUG_ALLOC(tkTextCountDestroySegment++);
	assert(sharedTextPtr->endMarker->refCount == 1);
	FREE_SEGMENT(sharedTextPtr->endMarker);
	DEBUG_ALLOC(tkTextCountDestroySegment++);
	FREE_SEGMENT(sharedTextPtr->protectionMark[0]);
	DEBUG_ALLOC(tkTextCountDestroySegment++);
	FREE_SEGMENT(sharedTextPtr->protectionMark[1]);
	DEBUG_ALLOC(tkTextCountDestroySegment++);
	TkTextFreeAllTags(textPtr);
	Tcl_DeleteHashTable(&sharedTextPtr->tagTable);
	TkTextFreeMarks(sharedTextPtr, false);
	TkBitDestroy(&sharedTextPtr->usedTags);
	TkBitDestroy(&sharedTextPtr->elisionTags);
	TkBitDestroy(&sharedTextPtr->selectionTags);
	TkBitDestroy(&sharedTextPtr->dontUndoTags);
	TkBitDestroy(&sharedTextPtr->affectDisplayTags);
	TkBitDestroy(&sharedTextPtr->notAffectDisplayTags);
	TkBitDestroy(&sharedTextPtr->affectDisplayNonSelTags);
	TkBitDestroy(&sharedTextPtr->affectGeometryTags);
	TkBitDestroy(&sharedTextPtr->affectGeometryNonSelTags);
	TkBitDestroy(&sharedTextPtr->affectLineHeightTags);
	TkTextTagSetDestroy(&sharedTextPtr->emptyTagInfoPtr);
	Tcl_DeleteHashTable(&sharedTextPtr->windowTable);
	Tcl_DeleteHashTable(&sharedTextPtr->imageTable);
	TkTextDeleteBreakInfoTableEntries(&sharedTextPtr->breakInfoTable);
	Tcl_DeleteHashTable(&sharedTextPtr->breakInfoTable);
	free(sharedTextPtr->mainPeer);
	DEBUG_ALLOC(tkTextCountDestroyPeer++);

	if (sharedTextPtr->tagBindingTable) {
	    Tk_DeleteBindingTable(sharedTextPtr->tagBindingTable);
	}
	if (sharedTextPtr->imageBindingTable) {
	    Tk_DeleteBindingTable(sharedTextPtr->imageBindingTable);
	}
	if (sharedTextPtr->stillExisting) {
	    *sharedTextPtr->stillExisting = false;
	}
	free(sharedTextPtr);
	DEBUG_ALLOC(tkTextCountDestroyShared++);

	textPtr->sharedIsReleased = true;
	textPtr->refCount -= 1;

#if TK_CHECK_ALLOCS
	/*
	 * Remove this shared resource from global list.
	 */
	{
	    WatchShared *thisPtr = watchShared;
	    WatchShared *prevPtr = NULL;

	    while (thisPtr->sharedTextPtr != sharedTextPtr) {
		prevPtr = thisPtr;
		thisPtr = thisPtr->nextPtr;
		assert(thisPtr);
	    }

	    if (prevPtr) {
		prevPtr->nextPtr = thisPtr->nextPtr;
	    } else {
		watchShared = thisPtr->nextPtr;
	    }

	    free(thisPtr);
	}
#endif
    }

    if (textPtr->tabArrayPtr) {
	free(textPtr->tabArrayPtr);
    }
    if (textPtr->insertBlinkHandler) {
	Tcl_DeleteTimerHandler(textPtr->insertBlinkHandler);
    }

    textPtr->tkwin = NULL;
    Tcl_DeleteCommandFromToken(textPtr->interp, textPtr->widgetCmd);
    if (--textPtr->refCount == 0) {
	free(textPtr);
    }

    tkBTreeDebug = debug;
    DEBUG_ALLOC(tkTextCountDestroyPeer++);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextDecrRefCountAndTestIfDestroyed --
 *
 *	This function is decrementing the reference count of the text
 *	widget and destroys the widget if the reference count has been
 *	gone to zero.
 *
 * Results:
 *	Returns whether the widget has been destroyed.
 *
 * Side effects:
 *	Memory might be freed.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextDecrRefCountAndTestIfDestroyed(
    TkText *textPtr)
{
    if (--textPtr->refCount == 0) {
	assert(textPtr->flags & DESTROYED);
	free(textPtr);
	return true;
    }
    return !!(textPtr->flags & DESTROYED);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextReleaseIfDestroyed --
 *
 *	This function is decrementing the reference count of the text
 *	widget if it has been destroyed. In this case also the memory
 *	will be released.
 *
 * Results:
 *	Returns whether the widget was already destroyed.
 *
 * Side effects:
 *	Memory might be freed.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextReleaseIfDestroyed(
    TkText *textPtr)
{
    if (!(textPtr->flags & DESTROYED)) {
	return false;
    }
    if (--textPtr->refCount == 0) {
	free(textPtr);
    }
    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextTestLangCode --
 *
 *	Test the given language code, whether it satsifies ISO 539-1,
 *	and set an error message if the code is invalid.
 *
 * Results:
 *	The return value is 'tue' if given language code will be accepted,
 *	otherwise 'false' will be returned.
 *
 * Side effects:
 *	An error message in the interpreter may be set.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextTestLangCode(
    Tcl_Interp *interp,
    Tcl_Obj *langCodePtr)
{
    char const *lang = Tcl_GetString(langCodePtr);

    if (UCHAR(lang[0]) >= 0x80
	    || UCHAR(lang[1]) >= 0x80
	    || !isalpha(lang[0])
	    || !isalpha(lang[1])
	    || !islower(lang[0])
	    || !islower(lang[1])
	    || lang[2] != '\0') {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad lang \"%s\": "
		"must have the form of an ISO 639-1 language code, or empty", lang));
	Tcl_SetErrorCode(interp, "TK", "VALUE", "LANG", NULL);
	return false;
    }
    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TkConfigureText --
 *
 *	This function is called to process an objv/objc list, plus the Tk
 *	option database, in order to configure (or reconfigure) a text widget.
 *
 * Results:
 *	The return value is a standard Tcl result. If TCL_ERROR is returned,
 *	then the interp's result contains an error message.
 *
 * Side effects:
 *	Configuration information, such as text string, colors, font, etc. get
 *	set for textPtr; old resources get freed, if there were any.
 *
 *----------------------------------------------------------------------
 */

static bool
IsNumberOrEmpty(
    const char *str)
{
    for ( ; *str; ++str) {
	if (!isdigit(*str)) {
	    return false;
	}
    }
    return true;
}

int
TkConfigureText(
    Tcl_Interp *interp,		/* Used for error reporting. */
    TkText *textPtr,		/* Information about widget; may or may not
				 * already have values for some fields. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Tk_SavedOptions savedOptions;
    TkTextIndex start, end, current;
    unsigned currentEpoch;
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    TkTextBTree tree = sharedTextPtr->tree;
    bool oldExport = textPtr->exportSelection;
    bool oldTextDebug = tkTextDebug;
    bool didHyphenate = textPtr->hyphenate;
    int oldHyphenRules = textPtr->hyphenRules;
    int mask = 0;
    bool copyDownFlags = false;

    tkTextDebug = false; /* debugging is not useful here */

#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE

    /*
     * We want also to support the "-start", and "-end" abbreviations. The thing that
     * Tcl supports abbreviated options is a real crux.
     */

    {
	Tcl_Obj **myObjv;
	Tcl_Obj *startLineObj = NULL;
	Tcl_Obj *endLineObj = NULL;
	Tcl_Obj *startIndexObj = NULL;
	Tcl_Obj *endIndexObj = NULL;
	int i, rc;

	myObjv = malloc(objc * sizeof(Tcl_Obj *));

	for (i = 0; i < objc; ++i) {
	    Tcl_Obj *obj = objv[i];

	    if (!(i & 1)) {
		if (strcmp(Tcl_GetString(objv[i]), "-start") == 0) {
		    if (i + 1 < objc && IsNumberOrEmpty(Tcl_GetString(objv[i + 1]))) {
			if (!startLineObj) {
			    Tcl_IncrRefCount(startLineObj = Tcl_NewStringObj("-startline", -1));
			}
			obj = startLineObj;
			WarnAboutDeprecatedStartLineOption();
		    } else {
			if (!startIndexObj) {
			    Tcl_IncrRefCount(startIndexObj = Tcl_NewStringObj("-startindex", -1));
			}
			obj = startIndexObj;
		    }
		} else if (strncmp(Tcl_GetString(objv[i]), "-startl", 7) == 0) {
		    if (!startLineObj) {
			Tcl_IncrRefCount(startLineObj = Tcl_NewStringObj("-startline", -1));
		    }
		    obj = startLineObj;
		    WarnAboutDeprecatedStartLineOption();
		} else if (strncmp(Tcl_GetString(objv[i]), "-starti", 7) == 0) {
		    if (!startIndexObj) {
			Tcl_IncrRefCount(startIndexObj = Tcl_NewStringObj("-startindex", -1));
		    }
		    obj = startIndexObj;
		} else if (strcmp(Tcl_GetString(objv[i]), "-end") == 0) {
		    if (i + 1 < objc && IsNumberOrEmpty(Tcl_GetString(objv[i + 1]))) {
			if (!endLineObj) {
			    Tcl_IncrRefCount(endLineObj = Tcl_NewStringObj("-endline", -1));
			}
			obj = endLineObj;
			WarnAboutDeprecatedEndLineOption();
		    } else {
			if (!endIndexObj) {
			    Tcl_IncrRefCount(endIndexObj = Tcl_NewStringObj("-endindex", -1));
			}
			obj = endIndexObj;
		    }
		} else if (strncmp(Tcl_GetString(objv[i]), "-endl", 5) == 0) {
		    if (!endLineObj) {
			Tcl_IncrRefCount(endLineObj = Tcl_NewStringObj("-endline", -1));
		    }
		    obj = endLineObj;
		    WarnAboutDeprecatedEndLineOption();
		} else if (strncmp(Tcl_GetString(objv[i]), "-endi", 5) == 0) {
		    if (!endIndexObj) {
			Tcl_IncrRefCount(endIndexObj = Tcl_NewStringObj("-endindex", -1));
		    }
		    obj = endIndexObj;
		}
	    }
	    myObjv[i] = obj;
	}

	rc = Tk_SetOptions(interp, (char *) textPtr, textPtr->optionTable,
		objc, myObjv, textPtr->tkwin, &savedOptions, &mask);

	if (rc != TCL_OK) {
	    if (startLineObj && startIndexObj) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "cannot use both, -startindex, and deprecated -startline", -1));
		Tk_RestoreSavedOptions(&savedOptions);
		rc = TCL_ERROR;
	    }
	    if (endLineObj && endIndexObj) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "cannot use both, -endindex, and deprecated -endline", -1));
		Tk_RestoreSavedOptions(&savedOptions);
		rc = TCL_ERROR;
	    }
	}

	if (startLineObj)  { Tcl_DecrRefCount(startLineObj); }
	if (endLineObj)    { Tcl_DecrRefCount(endLineObj); }
	if (startIndexObj) { Tcl_DecrRefCount(startIndexObj); }
	if (endIndexObj)   { Tcl_DecrRefCount(endIndexObj); }

	free(myObjv);

	if (rc != TCL_OK) {
	    tkTextDebug = oldTextDebug;
	    return rc;
	}
    }

    if ((mask & TK_TEXT_LINE_RANGE) == TK_TEXT_LINE_RANGE) {
	TkTextIndexClear2(&start, NULL, tree);
	TkTextIndexClear2(&end, NULL, tree);
	TkTextIndexSetToStartOfLine2(&start, textPtr->startLine ?
		textPtr->startLine : TkBTreeGetStartLine(textPtr));
	TkTextIndexSetToStartOfLine2(&end, textPtr->endLine ?
		textPtr->endLine : TkBTreeGetLastLine(textPtr));
	if (textPtr->endLine && textPtr->startLine != textPtr->endLine) {
	    TkTextIndexBackChars(textPtr, &end, 1, &end, COUNT_INDICES);
	}

	if (TkTextIndexCompare(&start, &end) > 0) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "-startline must be less than or equal to -endline", -1));
	    Tcl_SetErrorCode(interp, "TK", "TEXT", "INDEX_ORDER", NULL);
	    Tk_RestoreSavedOptions(&savedOptions);
	    tkTextDebug = oldTextDebug;
	    return TCL_ERROR;
	}

	if (textPtr->endLine && textPtr->endLine != sharedTextPtr->endMarker->sectionPtr->linePtr) {
	    if (textPtr->endMarker->refCount > 1) {
		textPtr->endMarker->refCount -= 1;
		textPtr->endMarker = TkTextMakeStartEndMark(textPtr, &tkTextRightMarkType);
	    } else {
		TkBTreeUnlinkSegment(sharedTextPtr, textPtr->endMarker);
	    }
	    TkBTreeLinkSegment(sharedTextPtr, textPtr->endMarker, &end);
	} else if (textPtr->endMarker != sharedTextPtr->endMarker) {
	    if (--textPtr->endMarker->refCount == 0) {
		TkBTreeUnlinkSegment(sharedTextPtr, textPtr->endMarker);
		FREE_SEGMENT(textPtr->endMarker);
		DEBUG_ALLOC(tkTextCountDestroySegment++);
	    }
	    (textPtr->endMarker = sharedTextPtr->endMarker)->refCount += 1;
	}
	if (textPtr->startLine
		&& textPtr->startLine != sharedTextPtr->startMarker->sectionPtr->linePtr) {
	    if (textPtr->startMarker->refCount > 1) {
		textPtr->startMarker->refCount -= 1;
		textPtr->startMarker = TkTextMakeStartEndMark(textPtr, &tkTextLeftMarkType);
	    } else {
		TkBTreeUnlinkSegment(sharedTextPtr, textPtr->startMarker);
	    }
	    TkBTreeLinkSegment(sharedTextPtr, textPtr->startMarker, &start);
	} else if (textPtr->startMarker != sharedTextPtr->startMarker) {
	    if (--textPtr->startMarker->refCount == 0) {
		TkBTreeUnlinkSegment(sharedTextPtr, textPtr->startMarker);
		FREE_SEGMENT(textPtr->startMarker);
		DEBUG_ALLOC(tkTextCountDestroySegment++);
	    }
	    (textPtr->startMarker = sharedTextPtr->startMarker)->refCount += 1;
	}
    }

#else /* if !SUPPORT_DEPRECATED_STARTLINE_ENDLINE */

    if (Tk_SetOptions(interp, (char *) textPtr, textPtr->optionTable,
	    objc, objv, textPtr->tkwin, &savedOptions, &mask) != TCL_OK) {
	tkTextDebug = oldTextDebug;
	return TCL_ERROR;
    }

#endif /* SUPPORT_DEPRECATED_STARTLINE_ENDLINE */

    if (sharedTextPtr->steadyMarks != textPtr->steadyMarks) {
	if (!IsClean(sharedTextPtr, NULL, true)) {
	    ErrorNotAllowed(interp, "setting this option is possible only if the widget "
		    "is overall clean");
	    Tk_RestoreSavedOptions(&savedOptions);
	    tkTextDebug = oldTextDebug;
	    return TCL_ERROR;
	}
    }

    /*
     * Copy up shared flags.
     */

    /* This flag cannot alter if we have peers. */
    sharedTextPtr->steadyMarks = textPtr->steadyMarks;

    if (sharedTextPtr->autoSeparators != textPtr->autoSeparators) {
	sharedTextPtr->autoSeparators = textPtr->autoSeparators;
	copyDownFlags = true;
    }

    if (textPtr->undo != sharedTextPtr->undo) {
	if (TestIfPerformingUndoRedo(interp, sharedTextPtr, NULL)) {
	    Tk_RestoreSavedOptions(&savedOptions);
	    tkTextDebug = oldTextDebug;
	    return TCL_ERROR;
	}

	assert(sharedTextPtr->undo == !!sharedTextPtr->undoStack);
	sharedTextPtr->undo = textPtr->undo;
	copyDownFlags = true;

	if (sharedTextPtr->undo) {
	    sharedTextPtr->undoStack = TkTextUndoCreateStack(
		    sharedTextPtr->maxUndoDepth,
		    sharedTextPtr->maxRedoDepth,
		    sharedTextPtr->maxUndoSize,
		    TextUndoRedoCallback,
		    TextUndoFreeCallback,
		    TextUndoStackContentChangedCallback);
	    TkTextUndoSetContext(sharedTextPtr->undoStack, sharedTextPtr);
	    sharedTextPtr->undoLevel = 0;
	    sharedTextPtr->isIrreversible = false;
	    sharedTextPtr->isAltered = false;
	} else {
	    sharedTextPtr->isIrreversible = TkTextUndoContentIsModified(sharedTextPtr->undoStack);
	    ClearRetainedUndoTokens(sharedTextPtr);
	    TkTextUndoDestroyStack(&sharedTextPtr->undoStack);
	}
    }

    /* normalize values */
    textPtr->maxUndoDepth = MAX(textPtr->maxUndoDepth, 0);
    textPtr->maxRedoDepth = MAX(-1, textPtr->maxRedoDepth);
    textPtr->maxUndoSize = MAX(textPtr->maxUndoSize, 0);

    if (sharedTextPtr->maxUndoDepth != textPtr->maxUndoDepth
	    || sharedTextPtr->maxRedoDepth != textPtr->maxRedoDepth
	    || sharedTextPtr->maxUndoSize != textPtr->maxUndoSize) {
	if (sharedTextPtr->undoStack) {
	    TkTextUndoSetMaxStackDepth(sharedTextPtr->undoStack,
		    textPtr->maxUndoDepth, textPtr->maxRedoDepth);
	    TkTextUndoSetMaxStackSize(sharedTextPtr->undoStack, textPtr->maxUndoSize, false);
	}
	sharedTextPtr->maxUndoDepth = textPtr->maxUndoDepth;
	sharedTextPtr->maxRedoDepth = textPtr->maxRedoDepth;
	sharedTextPtr->maxUndoSize = textPtr->maxUndoSize;
	copyDownFlags = true;
    }

    if (copyDownFlags) {
	TkText *tPtr;

	for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
	    tPtr->autoSeparators = sharedTextPtr->autoSeparators;
	    tPtr->maxUndoDepth = sharedTextPtr->maxUndoDepth;
	    tPtr->maxRedoDepth = sharedTextPtr->maxRedoDepth;
	    tPtr->maxUndoSize = sharedTextPtr->maxUndoSize;
	    tPtr->undo = sharedTextPtr->undo;
	}
    }

    /*
     * Check soft hyphen support.
     */

    textPtr->hyphenate = textPtr->hyphens
	    && textPtr->state != TK_TEXT_STATE_NORMAL
	    && (textPtr->wrapMode == TEXT_WRAPMODE_WORD || textPtr->wrapMode == TEXT_WRAPMODE_CODEPOINT);
    if (didHyphenate != textPtr->hyphenate) {
	mask |= TK_TEXT_LINE_GEOMETRY;
    }

    /*
     * Parse hyphen rules.
     */

    if (textPtr->hyphenRulesPtr) {
	if (TkTextParseHyphenRules(textPtr, textPtr->hyphenRulesPtr, &textPtr->hyphenRules) != TCL_OK) {
	    Tk_RestoreSavedOptions(&savedOptions);
	    tkTextDebug = oldTextDebug;
	    return TCL_ERROR;
	}
    } else {
	textPtr->hyphenRules = 0;
    }
    if (oldHyphenRules != textPtr->hyphenRules && textPtr->hyphenate) {
	mask |= TK_TEXT_LINE_GEOMETRY;
    }

    /*
     * Parse tab stops.
     */

    if (textPtr->tabArrayPtr) {
	free(textPtr->tabArrayPtr);
	textPtr->tabArrayPtr = NULL;
    }
    if (textPtr->tabOptionPtr) {
	textPtr->tabArrayPtr = TkTextGetTabs(interp, textPtr, textPtr->tabOptionPtr);
	if (!textPtr->tabArrayPtr) {
	    Tcl_AddErrorInfo(interp, "\n    (while processing -tabs option)");
	    Tk_RestoreSavedOptions(&savedOptions);
	    tkTextDebug = oldTextDebug;
	    return TCL_ERROR;
	}
    }

    /*
     * Check language support.
     */

    if (textPtr->langPtr) {
	if (!TkTextTestLangCode(interp, textPtr->langPtr)) {
	    Tk_RestoreSavedOptions(&savedOptions);
	    tkTextDebug = oldTextDebug;
	    return TCL_ERROR;
	}
	memcpy(textPtr->lang, Tcl_GetString(textPtr->langPtr), 3);
    } else {
	memset(textPtr->lang, 0, 3);
    }

    /*
     * A few other options also need special processing, such as parsing the
     * geometry and setting the background from a 3-D border.
     */

    Tk_SetBackgroundFromBorder(textPtr->tkwin, textPtr->border);

    /*
     * Now setup the -startindex/-setindex range. This step cannot be restored,
     * so this function must not return with an error code after this processing.
     */

    if (mask & TK_TEXT_INDEX_RANGE) {
	if (textPtr->newStartIndex) {
	    if (!TkTextGetIndexFromObj(interp, sharedTextPtr->mainPeer,
		    textPtr->newStartIndex, &start)) {
		Tk_RestoreSavedOptions(&savedOptions);
		tkTextDebug = oldTextDebug;
		return TCL_ERROR;
	    }
	}
	if (textPtr->newEndIndex) {
	    if (!TkTextGetIndexFromObj(interp, sharedTextPtr->mainPeer, textPtr->newEndIndex, &end)) {
		Tk_RestoreSavedOptions(&savedOptions);
		tkTextDebug = oldTextDebug;
		return TCL_ERROR;
	    }
	}
	if (TkTextIndexCompare(&start, &end) > 0) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "-startindex must be less than or equal to -endindex", -1));
	    Tcl_SetErrorCode(interp, "TK", "TEXT", "INDEX_ORDER", NULL);
	    Tk_RestoreSavedOptions(&savedOptions);
	    tkTextDebug = oldTextDebug;
	    return TCL_ERROR;
	}

	start.textPtr = NULL;
	end.textPtr = NULL;

	if (textPtr->newEndIndex) {
	    if (TkTextIndexIsEndOfText(&end)) {
		if (--textPtr->endMarker->refCount == 0) {
		    assert(textPtr->endMarker != sharedTextPtr->endMarker);
		    TkBTreeUnlinkSegment(sharedTextPtr, textPtr->endMarker);
		    FREE_SEGMENT(textPtr->endMarker);
		    DEBUG_ALLOC(tkTextCountDestroySegment++);
		}
		(textPtr->endMarker = sharedTextPtr->endMarker)->refCount += 1;
	    } else {
		if (textPtr->endMarker->refCount > 1) {
		    textPtr->endMarker->refCount -= 1;
		    textPtr->endMarker = TkTextMakeStartEndMark(textPtr, &tkTextRightMarkType);
		} else {
		    assert(textPtr->endMarker != sharedTextPtr->endMarker);
		    TkBTreeUnlinkSegment(sharedTextPtr, textPtr->endMarker);
		}
		TkBTreeLinkSegment(sharedTextPtr, textPtr->endMarker, &end);
	    }
	    Tcl_DecrRefCount(textPtr->newEndIndex);
	    textPtr->newEndIndex = NULL;
	}

	if (textPtr->newStartIndex) {
	    if (TkTextIndexIsStartOfText(&start)) {
		if (--textPtr->startMarker->refCount == 0) {
		    assert(textPtr->startMarker != sharedTextPtr->startMarker);
		    TkBTreeUnlinkSegment(sharedTextPtr, textPtr->startMarker);
		    FREE_SEGMENT(textPtr->startMarker);
		    DEBUG_ALLOC(tkTextCountDestroySegment++);
		}
		(textPtr->startMarker = sharedTextPtr->startMarker)->refCount += 1;
	    } else {
		if (textPtr->startMarker->refCount > 1) {
		    textPtr->startMarker->refCount -= 1;
		    textPtr->startMarker = TkTextMakeStartEndMark(textPtr, &tkTextLeftMarkType);
		} else {
		    TkBTreeUnlinkSegment(sharedTextPtr, textPtr->startMarker);
		}
		TkBTreeLinkSegment(sharedTextPtr, textPtr->startMarker, &start);
	    }
	    Tcl_DecrRefCount(textPtr->newStartIndex);
	    textPtr->newStartIndex = NULL;
	}

	/*
	 * Line start and/or end have been adjusted. We need to validate the
	 * first displayed line and arrange for re-layout.
	 */

	TkBTreeClientRangeChanged(textPtr, textPtr->lineHeight);
	TkTextMakeByteIndex(tree, NULL, TkTextIndexGetLineNumber(&textPtr->topIndex, NULL), 0, &current);

	if (TkTextIndexCompare(&current, &start) < 0 || TkTextIndexCompare(&end, &current) < 0) {
	    TkTextSearch search;
	    TkTextIndex first, last;
	    bool selChanged = false;

	    TkTextSetYView(textPtr, &start, 0);

	    /*
	     * We may need to adjust the selection. So we have to check
	     * whether the "sel" tag was applied to anything outside the
	     * current start,end.
	     */

	    TkTextMakeByteIndex(tree, NULL, 0, 0, &first);
	    TkBTreeStartSearch(&first, &start, textPtr->selTagPtr, &search, SEARCH_NEXT_TAGON);
	    if (TkBTreeNextTag(&search)) {
		selChanged = true;
	    } else {
		TkTextMakeByteIndex(tree, NULL, TkBTreeNumLines(tree, NULL), 0, &last);
		TkBTreeStartSearchBack(&end, &last, textPtr->selTagPtr,
			&search, SEARCH_EITHER_TAGON_TAGOFF);
		if (TkBTreePrevTag(&search)) {
		    selChanged = true;
		}
	    }
	    if (selChanged) {
		/*
		 * Send an event that the selection has changed, and abort any
		 * partial-selections in progress.
		 */

		TkTextSelectionEvent(textPtr);
		textPtr->abortSelections = true;
	    }
	}

	/* Indices are potentially obsolete after changing -start and/or
	 * -end, therefore increase the epoch.
	 * Also, clamp the insert and current (unshared) marks to the new
	 * -start/-end range limits of the widget. All other (shared)
	 * marks are unchanged.
         * The return value of TkTextMarkNameToIndex does not need to be
         * checked: "insert" and "current" marks always exist, and the
         * purpose of the code below precisely is to move them inside the
         * -start/-end range.
	 */

	currentEpoch = TkBTreeIncrEpoch(tree);
	start.textPtr = textPtr;
	end.textPtr = textPtr;

	TkTextMarkNameToIndex(textPtr, "current", &current);
	if (TkTextIndexCompare(&current, &start) < 0) {
	    textPtr->currentMarkPtr = TkTextSetMark(textPtr, "current", &start);
	} else if (TkTextIndexCompare(&current, &end) > 0) {
	    textPtr->currentMarkPtr = TkTextSetMark(textPtr, "current", &end);
	}
    } else {
	currentEpoch = TkBTreeEpoch(tree);
    }

    /*
     * Don't allow negative spacings.
     */

    textPtr->spacing1 = MAX(textPtr->spacing1, 0);
    textPtr->spacing2 = MAX(textPtr->spacing2, 0);
    textPtr->spacing3 = MAX(textPtr->spacing3, 0);

    /*
     * Also the following widths shouldn't be negative.
     */

    textPtr->highlightWidth = MAX(textPtr->highlightWidth, 0);
    textPtr->selBorderWidth = MAX(textPtr->selBorderWidth, 0);
    textPtr->borderWidth = MAX(textPtr->borderWidth, 0);
    textPtr->insertWidth = MAX(textPtr->insertWidth, 0);

    /*
     * Don't allow negative sync timeout.
     */

    textPtr->syncTime = MAX(0, textPtr->syncTime);

    /*
     * Make sure that configuration options are properly mirrored between the
     * widget record and the "sel" tags. NOTE: we don't have to free up
     * information during the mirroring; old information was freed when it was
     * replaced in the widget record.
     */

    if (textPtr->selTagPtr->selBorder) {
	textPtr->selTagPtr->selBorder = textPtr->selBorder;
    } else {
	textPtr->selTagPtr->border = textPtr->selBorder;
    }
    if (textPtr->selTagPtr->borderWidthPtr != textPtr->selBorderWidthPtr) {
	textPtr->selTagPtr->borderWidthPtr = textPtr->selBorderWidthPtr;
	textPtr->selTagPtr->borderWidth = textPtr->selBorderWidth;
    }
    if (textPtr->selTagPtr->selFgColor) {
	textPtr->selTagPtr->selFgColor = textPtr->selFgColorPtr;
    } else {
	textPtr->selTagPtr->fgColor = textPtr->selFgColorPtr;
    }
    TkTextUpdateTagDisplayFlags(textPtr->selTagPtr);
    TkTextRedrawTag(NULL, textPtr, NULL, NULL, textPtr->selTagPtr, false);

    /*
     * Claim the selection if we've suddenly started exporting it and there
     * are tagged characters.
     */

    if (textPtr->exportSelection && !oldExport) {
	TkTextSearch search;
	TkTextIndex first, last;

	TkTextIndexSetupToStartOfText(&first, textPtr, tree);
	TkTextIndexSetupToEndOfText(&last, textPtr, tree);
	TkBTreeStartSearch(&first, &last, textPtr->selTagPtr, &search, SEARCH_NEXT_TAGON);
	if (TkBTreeNextTag(&search)) {
	    Tk_OwnSelection(textPtr->tkwin, XA_PRIMARY, TkTextLostSelection, textPtr);
	    textPtr->flags |= GOT_SELECTION;
	}
    }

    /*
     * Account for state changes that would reenable blinking cursor state.
     */

    if (textPtr->flags & HAVE_FOCUS) {
	Tcl_DeleteTimerHandler(textPtr->insertBlinkHandler);
	textPtr->insertBlinkHandler = NULL;
	TextBlinkProc(textPtr);
    }

    /*
     * Register the desired geometry for the window, and arrange for the
     * window to be redisplayed.
     */

    textPtr->width = MAX(textPtr->width, 1);
    textPtr->height = MAX(textPtr->height, 1);

    Tk_FreeSavedOptions(&savedOptions);
    TextWorldChanged(textPtr, mask);

    if (textPtr->syncTime == 0 && (mask & TK_TEXT_SYNCHRONIZE)) {
	UpdateLineMetrics(textPtr, 0, TkBTreeNumLines(sharedTextPtr->tree, textPtr));
    }

    /*
     * At least handle the "watch" command, and set the insert cursor.
     */

    if (mask & TK_TEXT_INDEX_RANGE) {
	/*
	 * Setting the "insert" mark must be done at the end, because the "watch" command
	 * will be triggered. Be sure to use the actual range, mind the epoch.
	 */

	TkTextMarkNameToIndex(textPtr, "insert", &current);

	if (start.stateEpoch != currentEpoch) {
	    /*
	     * The "watch" command did change the content.
	     */
	    TkTextIndexSetupToStartOfText(&start, textPtr, tree);
	    TkTextIndexSetupToEndOfText(&end, textPtr, tree);
	}

	start.textPtr = textPtr;
	end.textPtr = textPtr;

	if (TkTextIndexCompare(&current, &start) < 0) {
	    textPtr->insertMarkPtr = TkTextSetMark(textPtr, "insert", &start);
	} else if (TkTextIndexCompare(&current, &end) >= 0) {
	    textPtr->insertMarkPtr = TkTextSetMark(textPtr, "insert", &end);
	}
    }

    tkTextDebug = oldTextDebug;
    TK_BTREE_DEBUG(TkBTreeCheck(sharedTextPtr->tree));

    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextParseHyphenRules --
 *
 *	This function is parsing the object containing the hyphen rules.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

int
TkTextParseHyphenRules(
    TkText *textPtr,
    Tcl_Obj *objPtr,
    int *rulesPtr)
{
    int rules = 0;
    Tcl_Obj **argv;
    int argc, i;
    unsigned k;

    assert(rulesPtr);

    if (Tcl_ListObjGetElements(textPtr->interp, objPtr, &argc, &argv) != TCL_OK) {
	return TCL_ERROR;
    }
    for (i = 0; i < argc; ++i) {
	char const *rule = Tcl_GetString(argv[i]);
	int r = rules;

	for (k = 0; k < sizeof(hyphenRuleStrings)/sizeof(hyphenRuleStrings[0]); ++k) {
	    if (strcmp(rule, hyphenRuleStrings[k]) == 0) {
		rules |= (1 << k);
	    }
	}
	if (r == rules) {
	    Tcl_SetObjResult(textPtr->interp, Tcl_ObjPrintf("unknown hyphen rule \"%s\"", rule));
	    Tcl_SetErrorCode(textPtr->interp, "TK", "TEXT", "VALUE", NULL);
	    return TCL_ERROR;
	}
    }
    *rulesPtr = rules;
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * TextWorldChangedCallback --
 *
 *	This function is called when the world has changed in some way and the
 *	widget needs to recompute all its graphics contexts and determine its
 *	new geometry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Configures all tags in the Text with a empty objc/objv, for the side
 *	effect of causing all the items to recompute their geometry and to be
 *	redisplayed.
 *
 *---------------------------------------------------------------------------
 */

static void
TextWorldChangedCallback(
    ClientData instanceData)	/* Information about widget. */
{
    TextWorldChanged((TkText *) instanceData, TK_TEXT_LINE_GEOMETRY);
}

/*
 *---------------------------------------------------------------------------
 *
 * TextWorldChanged --
 *
 *	This function is called when the world has changed in some way and the
 *	widget needs to recompute all its graphics contexts and determine its
 *	new geometry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Configures all tags in the Text with a empty objc/objv, for the side
 *	effect of causing all the items to recompute their geometry and to be
 *	redisplayed.
 *
 *---------------------------------------------------------------------------
 */

static void
TextWorldChanged(
    TkText *textPtr,		/* Information about widget. */
    int mask)			/* OR'd collection of bits showing what has changed. */
{
    Tk_FontMetrics fm;
    int border;
    int oldLineHeight = textPtr->lineHeight;

    Tk_GetFontMetrics(textPtr->tkfont, &fm);
    textPtr->lineHeight = MAX(1, fm.linespace);
    textPtr->charWidth = MAX(1, Tk_TextWidth(textPtr->tkfont, "0", 1));
    textPtr->spaceWidth = MAX(1, Tk_TextWidth(textPtr->tkfont, " ", 1));

    if (oldLineHeight != textPtr->lineHeight) {
	TkTextFontHeightChanged(textPtr);
    }

    border = textPtr->borderWidth + textPtr->highlightWidth;
    Tk_GeometryRequest(textPtr->tkwin,
	    textPtr->width*textPtr->charWidth + 2*textPtr->padX + 2*border,
	    textPtr->height*(fm.linespace + textPtr->spacing1 + textPtr->spacing3)
		    + 2*textPtr->padY + 2*border);

    Tk_SetInternalBorderEx(textPtr->tkwin,
	    border + textPtr->padX, border + textPtr->padX,
	    border + textPtr->padY, border + textPtr->padY);
    if (textPtr->setGrid) {
	Tk_SetGrid(textPtr->tkwin, textPtr->width, textPtr->height,
		textPtr->charWidth, textPtr->lineHeight);
    } else {
	Tk_UnsetGrid(textPtr->tkwin);
    }

    TkTextRelayoutWindow(textPtr, mask);
    TK_BTREE_DEBUG(TkBTreeCheck(textPtr->sharedTextPtr->tree));
}

/*
 *--------------------------------------------------------------
 *
 * TextEventProc --
 *
 *	This function is invoked by the Tk dispatcher on structure changes to
 *	a text. For texts with 3D borders, this function is also invoked for
 *	exposures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	When the window gets deleted, internal structures get cleaned up.
 *	When it gets exposed, it is redisplayed.
 *
 *--------------------------------------------------------------
 */

static void
ProcessConfigureNotify(
    TkText *textPtr,
    bool updateLineGeometry)
{
    int mask = updateLineGeometry ? TK_TEXT_LINE_GEOMETRY : 0;

    /*
     * Do not allow line height computations before we accept the first
     * ConfigureNotify event. The problem is the very poor performance
     * in CalculateDisplayLineHeight() with very small widget width.
     */

    if (!textPtr->sharedTextPtr->allowUpdateLineMetrics) {
	textPtr->sharedTextPtr->allowUpdateLineMetrics = true;
	updateLineGeometry = true;
	TkTextEventuallyRepick(textPtr);
    }

    if (textPtr->prevHeight != Tk_Height(textPtr->tkwin)
	    || textPtr->prevWidth != Tk_Width(textPtr->tkwin)) {
	mask |= TK_TEXT_LINE_REDRAW_BOTTOM_LINE;
    }
    TkTextRelayoutWindow(textPtr, mask);
    TK_BTREE_DEBUG(TkBTreeCheck(textPtr->sharedTextPtr->tree));

    textPtr->prevWidth = Tk_Width(textPtr->tkwin);
    textPtr->prevHeight = Tk_Height(textPtr->tkwin);

    if (textPtr->imageBboxTree) {
	textPtr->configureBboxTree = true;
    }
}

static void
ProcessDestroyNotify(
    TkText *textPtr)
{
    /*
     * NOTE: we must zero out selBorder, selBorderWidthPtr and
     * selFgColorPtr: they are duplicates of information in the "sel" tag,
     * which will be freed up when we delete all tags. Hence we don't want
     * the automatic config options freeing process to delete them as
     * well.
     */

    textPtr->selBorder = NULL;
    textPtr->selBorderWidthPtr = NULL;
    textPtr->selBorderWidth = 0;
    textPtr->selFgColorPtr = NULL;
    if (textPtr->setGrid) {
	Tk_UnsetGrid(textPtr->tkwin);
	textPtr->setGrid = false;
    }
    if (!(textPtr->flags & OPTIONS_FREED)) {
	Tk_FreeConfigOptions((char *) textPtr, textPtr->optionTable, textPtr->tkwin);
	textPtr->flags |= OPTIONS_FREED;
    }
    textPtr->flags |= DESTROYED;

    /*
     * Call 'DestroyTest' to handle the deletion for us. The actual
     * textPtr may still exist after this, if there are some outstanding
     * references. But we have flagged it as DESTROYED just above, so
     * nothing will try to make use of it very extensively.
     */

    DestroyText(textPtr);
}

static void
ProcessFocusInOut(
    TkText *textPtr,
    XEvent *eventPtr)
{
    TkTextIndex index, index2;

    if (eventPtr->xfocus.detail == NotifyInferior
	    || eventPtr->xfocus.detail == NotifyAncestor
	    || eventPtr->xfocus.detail == NotifyNonlinear) {
	if (eventPtr->type == FocusIn) {
	    textPtr->flags |= HAVE_FOCUS | INSERT_ON;
	} else {
	    textPtr->flags &= ~(HAVE_FOCUS | INSERT_ON);
	}
	if (textPtr->state == TK_TEXT_STATE_NORMAL) {
	    if (eventPtr->type == FocusOut) {
		if (textPtr->insertBlinkHandler) {
		    Tcl_DeleteTimerHandler(textPtr->insertBlinkHandler);
		    textPtr->insertBlinkHandler = NULL;
		}
	    } else if (textPtr->insertOffTime && !textPtr->insertBlinkHandler) {
		textPtr->insertBlinkHandler =
			Tcl_CreateTimerHandler(textPtr->insertOnTime, TextBlinkProc, textPtr);
	    }
	    TkTextMarkSegToIndex(textPtr, textPtr->insertMarkPtr, &index);
	    TkTextIndexForwChars(textPtr, &index, 1, &index2, COUNT_INDICES);
	    TkTextChanged(NULL, textPtr, &index, &index2);
	}
	if (textPtr->inactiveSelBorder != textPtr->selBorder
		|| textPtr->inactiveSelFgColorPtr != textPtr->selFgColorPtr) {
	    TkTextRedrawTag(NULL, textPtr, NULL, NULL, textPtr->selTagPtr, false);
	}
	if (textPtr->highlightWidth > 0) {
	    TkTextRedrawRegion(textPtr, 0, 0, textPtr->highlightWidth, textPtr->highlightWidth);
	}
    }
}

static void
TextEventProc(
    ClientData clientData,	/* Information about window. */
    XEvent *eventPtr)		/* Information about event. */
{
    TkText *textPtr = clientData;

    switch (eventPtr->type) {
    case ConfigureNotify:
	if (textPtr->prevWidth != Tk_Width(textPtr->tkwin)
		|| textPtr->prevHeight != Tk_Height(textPtr->tkwin)) {
	    /*
	     * We don't need display computations until the widget is mapped
	     * or as long as the width seems to be unrealistic (not yet expanded
	     * by the geometry manager), see ProcessConfigureNotify() for more
	     * information.
	     */

	    if (Tk_IsMapped(textPtr->tkwin)
		    || (Tk_Width(textPtr->tkwin) >
			MAX(1, 2*(textPtr->highlightWidth + textPtr->borderWidth + textPtr->padX)))) {
		ProcessConfigureNotify(textPtr, textPtr->prevWidth != Tk_Width(textPtr->tkwin));
	    }
	}
	break;
    case DestroyNotify:
	ProcessDestroyNotify(textPtr);
	break;
    default:
	if (!textPtr->sharedTextPtr->allowUpdateLineMetrics) {
	    /*
	     * I don't know whether this can happen, but we want to be sure,
	     * probably we have rejected all ConfigureNotify events before
	     * first Expose arrives.
	     */
	    ProcessConfigureNotify(textPtr, true);
	}
	switch (eventPtr->type) {
	case Expose:
	    TkTextRedrawRegion(textPtr, eventPtr->xexpose.x, eventPtr->xexpose.y,
		    eventPtr->xexpose.width, eventPtr->xexpose.height);
	    break;
	case FocusIn:
	case FocusOut:
	    ProcessFocusInOut(textPtr, eventPtr);
	    break;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TextCmdDeletedProc --
 *
 *	This function is invoked when a widget command is deleted. If the
 *	widget isn't already in the process of being destroyed, this command
 *	destroys it.
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
TextCmdDeletedProc(
    ClientData clientData)	/* Pointer to widget record for widget. */
{
    TkText *textPtr = clientData;
    Tk_Window tkwin = textPtr->tkwin;

    /*
     * This function could be invoked either because the window was destroyed
     * and the command was then deleted (in which this flag is already set) or
     * because the command was deleted, and then this function destroys the
     * widget.
     */

    if (!(textPtr->flags & DESTROYED)) {
	if (textPtr->setGrid) {
	    Tk_UnsetGrid(textPtr->tkwin);
	    textPtr->setGrid = false;
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
 *	This function implements most of the functionality of the "insert"
 *	widget command.
 *
 * Results:
 *	The length of the inserted string.
 *
 * Side effects:
 *	The characters in "stringPtr" get added to the text just before the
 *	character indicated by "indexPtr".
 *
 *	If 'viewUpdate' is true, we may adjust the window contents'
 *	y-position, and scrollbar setting.
 *
 *----------------------------------------------------------------------
 */

static void
InitPosition(
    TkSharedText *sharedTextPtr,	/* Shared portion of peer widgets. */
    TkTextPosition *positions)		/* Initialise this position array. */
{
    unsigned i;

    for (i = 0; i < sharedTextPtr->numPeers; ++i, ++positions) {
	positions->lineIndex = -1;
	positions->byteIndex = 0;
    }
}

static void
FindNewTopPosition(
    TkSharedText *sharedTextPtr,	/* Shared portion of peer widgets. */
    TkTextPosition *positions,		/* Fill this position array. */
    const TkTextIndex *index1Ptr,	/* Start position of this insert/delete. */
    const TkTextIndex *index2Ptr,	/* End position of this delete, is NULL in case of insert. */
    unsigned lengthOfInsertion)		/* Length of inserted string, is zero in case of delete. */
{
    TkTextBTree tree = sharedTextPtr->tree;
    TkText *tPtr;

    for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next, ++positions) {
	int lineIndex = -1;
	int byteIndex = 0;

	if (index2Ptr == NULL) {
	    if (TkTextIndexGetLine(index1Ptr) == TkTextIndexGetLine(&tPtr->topIndex)) {
		lineIndex = TkBTreeLinesTo(tree, NULL, TkTextIndexGetLine(index1Ptr), NULL);
		byteIndex = TkTextIndexGetByteIndex(&tPtr->topIndex);
		if (byteIndex > TkTextIndexGetByteIndex(index1Ptr)) {
		    byteIndex += lengthOfInsertion;
		}
	    }
	} else if (TkTextIndexCompare(index2Ptr, &tPtr->topIndex) >= 0) {
	    if (TkTextIndexCompare(index1Ptr, &tPtr->topIndex) <= 0) {
		/*
		 * Deletion range straddles topIndex: use the beginning of the
		 * range as the new topIndex.
		 */

		lineIndex = TkBTreeLinesTo(tree, NULL, TkTextIndexGetLine(index1Ptr), NULL);
		byteIndex = TkTextIndexGetByteIndex(index1Ptr);
	    } else if (TkTextIndexGetLine(index1Ptr) == TkTextIndexGetLine(&tPtr->topIndex)) {
		/*
		 * Deletion range starts on top line but after topIndex. Use
		 * the current topIndex as the new one.
		 */

		lineIndex = TkBTreeLinesTo(tree, NULL, TkTextIndexGetLine(index1Ptr), NULL);
		byteIndex = TkTextIndexGetByteIndex(&tPtr->topIndex);
            } else {
                /*
                 * Deletion range starts after the top line. This peers's view
                 * will not need to be reset. Nothing to do.
                 */
	    }
	} else if (TkTextIndexGetLine(index2Ptr) == TkTextIndexGetLine(&tPtr->topIndex)) {
	    /*
	     * Deletion range ends on top line but before topIndex. Figure out
	     * what will be the new character index for the character
	     * currently pointed to by topIndex.
	     */

	    lineIndex = TkBTreeLinesTo(tree, NULL, TkTextIndexGetLine(index2Ptr), NULL);
	    byteIndex = TkTextIndexGetByteIndex(&tPtr->topIndex) - TkTextIndexGetByteIndex(index2Ptr);
	    if (TkTextIndexGetLine(index1Ptr) == TkTextIndexGetLine(index2Ptr)) {
		byteIndex += TkTextIndexGetByteIndex(index1Ptr);
	    }
        } else {
            /*
             * Deletion range ends before the top line. This peers's view
             * will not need to be reset. Nothing to do.
             */
	}

	if (lineIndex != -1) {
	    if (lineIndex == positions->lineIndex) {
		positions->byteIndex = MAX(positions->byteIndex, byteIndex);
	    } else {
		positions->lineIndex = MAX(positions->lineIndex, lineIndex);
		positions->byteIndex = byteIndex;
	    }
	}
    }
}

static void
SetNewTopPosition(
    TkSharedText *sharedTextPtr,	/* Shared portion of peer widgets. */
    TkText *textPtr,			/* Current peer widget, can be NULL. */
    const TkTextPosition *positions,	/* New top positions. */
    bool viewUpdate)			/* Update the view of current widget if set. */
{
    TkText *tPtr;

    for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next, ++positions) {
	if (positions->lineIndex != -1) {
	    TkTextIndex index;

	    if (tPtr == textPtr && !viewUpdate) {
		continue;
	    }

	    TkTextMakeByteIndex(sharedTextPtr->tree, NULL, positions->lineIndex, 0, &index);
	    TkTextIndexForwBytes(tPtr, &index, positions->byteIndex, &index);

	    if (tPtr == textPtr) {
		/*
		 * Line cannot be before -startindex of textPtr because this line
		 * corresponds to an index which is necessarily between "begin"
		 * and "end" relative to textPtr. Therefore no need to clamp line
		 * to the -start/-end range.
		 */
	    } else {
		TkTextIndex start;

                /*
                 * Line may be before -startindex of tPtr and must be clamped to -startindex
		 * before providing it to TkTextSetYView otherwise lines before -startindex
                 * would be displayed. There is no need to worry about -endline however,
                 * because the view will only be reset if the deletion involves the TOP
		 * line of the screen.
                 */

		TkTextIndexClear2(&start, tPtr, sharedTextPtr->tree);
		TkTextIndexSetSegment(&start, tPtr->startMarker);
		if (TkTextIndexCompare(&index, &start) < 0) {
		    index = start;
		}
	    }

	    TkTextSetYView(tPtr, &index, 0);
	}
    }
}

static void
ParseHyphens(
    const char *string,
    const char *end,
    char *buffer)
{
    /*
     * Preparing a string for hyphenation support. Note that 0xff is not allowed in
     * UTF-8 strings, so we can use this value for special purposes.
     */

    while (string != end) {
	if (*string == '\\') {
	    switch (*++string) {
	    case '\0':
		*buffer++ = '\\';
		break;
	    case '-':
		*buffer++ = 0xff;
		*buffer++ = '-';
		string += 1;
		break;
	    case '+':
	    	*buffer++ = 0xff;
		*buffer++ = '+';
		string += 1;
		break;
	    case ':':
		switch (string[1]) {
		case 'c':
		    if (strncmp(string, ":ck:", 4) == 0) {
			*buffer++ = 0xff;
			*buffer++ = 1 << TK_TEXT_HYPHEN_CK;
			string += 4;
			break;
		    }
		    *buffer++ = *string++;
		    break;
		case 'd':
		    if (strncmp(string, ":dd:", 4) == 0) {
			*buffer++ = 0xff;
			*buffer++ = 1 << TK_TEXT_HYPHEN_DOUBLE_DIGRAPH;
			string += 4;
			break;
		    }
		    if (strncmp(string, ":dv:", 4) == 0) {
			*buffer++ = 0xff;
			*buffer++ = 1 << TK_TEXT_HYPHEN_DOUBLE_VOWEL;
			string += 4;
			break;
		    }
		    if (strncmp(string, ":doubledigraph:", 15) == 0) {
			*buffer++ = 0xff;
			*buffer++ = 1 << TK_TEXT_HYPHEN_DOUBLE_DIGRAPH;
			string += 15;
			break;
		    }
		    if (strncmp(string, ":doublevowel:", 13) == 0) {
			*buffer++ = 0xff;
			*buffer++ = 1 << TK_TEXT_HYPHEN_DOUBLE_VOWEL;
			string += 13;
			break;
		    }
		    *buffer++ = *string++;
		    break;
		case 'g':
		    if (strncmp(string, ":ge:", 4) == 0) {
			*buffer++ = 0xff;
			*buffer++ = 1 << TK_TEXT_HYPHEN_GEMINATION;
			string += 4;
			break;
		    }
		    if (strncmp(string, ":gemination:", 12) == 0) {
			*buffer++ = 0xff;
			*buffer++ = 1 << TK_TEXT_HYPHEN_GEMINATION;
			string += 12;
			break;
		    }
		    *buffer++ = *string++;
		    break;
		case 'r':
		    if (strncmp(string, ":rh:", 4) == 0) {
			*buffer++ = 0xff;
			*buffer++ = 1 << TK_TEXT_HYPHEN_REPEAT;
			string += 4;
			break;
		    }
		    if (strncmp(string, ":repeathyphen:", 14) == 0) {
			*buffer++ = 0xff;
			*buffer++ = 1 << TK_TEXT_HYPHEN_REPEAT;
			string += 14;
			break;
		    }
		    *buffer++ = *string++;
		    break;
		case 't':
		    if (strncmp(string, ":tr:", 4) == 0) {
			*buffer++ = 0xff;
			*buffer++ = 1 << TK_TEXT_HYPHEN_TREMA;
			string += 4;
			break;
		    }
		    if (strncmp(string, ":tc:", 4) == 0) {
			*buffer++ = 0xff;
			*buffer++ = 1 << TK_TEXT_HYPHEN_TRIPLE_CONSONANT;
			string += 4;
			break;
		    }
		    if (strncmp(string, ":trema:", 7) == 0) {
			*buffer++ = 0xff;
			*buffer++ = 1 << TK_TEXT_HYPHEN_TREMA;
			string += 7;
			break;
		    }
		    if (strncmp(string, ":tripleconsonant:", 17) == 0) {
			*buffer++ = 0xff;
			*buffer++ = 1 << TK_TEXT_HYPHEN_TRIPLE_CONSONANT;
			string += 17;
			break;
		    }
		    *buffer++ = *string++;
		    break;
	    default:
		*buffer++ = *string++;
		break;
	    }
	    }
	} else {
	    *buffer++ = *string++;
	}
    }
    *buffer = '\0';
}

static void
InsertChars(
    TkText *textPtr,		/* Overall information about text widget. */
    TkTextIndex *index1Ptr,	/* Where to insert new characters. May be modified if the index
    				 * is not valid for insertion (e.g. if at "end"). */
    TkTextIndex *index2Ptr,	/* Out: Index at the end of the inserted text. */
    char const *string,		/* Null-terminated string containing new information to add to text. */
    unsigned length,		/* Length of string content. */
    bool viewUpdate,		/* Update the view if set. */
    TkTextTagSet *tagInfoPtr,	/* Add these tags to the inserted text, can be NULL. */
    TkTextTag *hyphenTagPtr,	/* Associate this tag with soft hyphens, can be NULL. */
    bool parseHyphens)		/* Should we parse hyphens (tk_textInsert)? */
{
    TkSharedText *sharedTextPtr;
    TkText *tPtr;
    TkTextPosition *textPosition;
    TkTextPosition textPosBuf[PIXEL_CLIENTS];
    TkTextUndoInfo undoInfo;
    TkTextUndoInfo *undoInfoPtr;
    TkTextIndex startIndex;
    const char *text = string;
    char textBuf[4096];

    assert(textPtr);
    assert(length > 0);
    assert(!TkTextIsDeadPeer(textPtr));

    sharedTextPtr = textPtr->sharedTextPtr;

    /*
     * Don't allow insertions on the last (dummy) line of the text. This is
     * the only place in this function where the index1Ptr is modified.
     */

    if (TkTextIndexGetLine(index1Ptr) == TkBTreeGetLastLine(textPtr)) {
	TkTextIndexBackChars(textPtr, index1Ptr, 1, index1Ptr, COUNT_INDICES);
    }

    /*
     * Notify the display module that lines are about to change, then do the
     * insertion. If the insertion occurs on the top line of the widget
     * (textPtr->topIndex), then we have to recompute topIndex after the
     * insertion, since the insertion could invalidate it.
     */

    if (sharedTextPtr->numPeers > sizeof(textPosition)/sizeof(textPosition[0])) {
	textPosition = malloc(sizeof(textPosition[0])*sharedTextPtr->numPeers);
    } else {
	textPosition = textPosBuf;
    }
    InitPosition(sharedTextPtr, textPosition);
    FindNewTopPosition(sharedTextPtr, textPosition, index1Ptr, NULL, length);

    TkTextChanged(sharedTextPtr, NULL, index1Ptr, index1Ptr);
    undoInfoPtr = TkTextUndoStackIsFull(sharedTextPtr->undoStack) ? NULL : &undoInfo;
    startIndex = *index1Ptr;
    TkTextIndexToByteIndex(&startIndex); /* we need the byte position after insertion */

    if (parseHyphens) {
	text = (length >= sizeof(textBuf)) ? malloc(length + 1) : textBuf;
	ParseHyphens(string, string + length, (char *) text);
    }

    TkBTreeInsertChars(sharedTextPtr->tree, index1Ptr, text, tagInfoPtr, hyphenTagPtr, undoInfoPtr);

    /*
     * Push the insertion on the undo stack, and update the modified status of the widget.
     * Try to join with previously pushed undo token, if possible.
     */

    if (undoInfoPtr) {
	const TkTextUndoSubAtom *subAtom;
	bool triggerStackEvent = false;
	bool pushToken;

	assert(undoInfo.byteSize == 0);

	if (sharedTextPtr->autoSeparators && sharedTextPtr->lastEditMode != TK_TEXT_EDIT_INSERT) {
	    PushRetainedUndoTokens(sharedTextPtr);
	    TkTextUndoPushSeparator(sharedTextPtr->undoStack, true);
	    sharedTextPtr->lastUndoTokenType = -1;
	}

	pushToken = sharedTextPtr->lastUndoTokenType != TK_TEXT_UNDO_INSERT
		|| !((subAtom = TkTextUndoGetLastUndoSubAtom(sharedTextPtr->undoStack))
			&& (triggerStackEvent = TkBTreeJoinUndoInsert(
				subAtom->item, subAtom->size, undoInfo.token, undoInfo.byteSize)));

	assert(undoInfo.token->undoType->rangeProc);
	sharedTextPtr->prevUndoStartIndex = ((TkTextUndoTokenRange *) undoInfo.token)->startIndex;
	sharedTextPtr->prevUndoEndIndex = ((TkTextUndoTokenRange *) undoInfo.token)->endIndex;
	sharedTextPtr->lastUndoTokenType = TK_TEXT_UNDO_INSERT;
	sharedTextPtr->lastEditMode = TK_TEXT_EDIT_INSERT;

	if (pushToken) {
	    TkTextPushUndoToken(sharedTextPtr, undoInfo.token, undoInfo.byteSize);
	} else {
	    assert(!undoInfo.token->undoType->destroyProc);
	    free(undoInfo.token);
	    DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
	}
	if (triggerStackEvent) {
	    sharedTextPtr->undoStackEvent = true; /* TkBTreeJoinUndoInsert didn't trigger */
	}
    }

    *index2Ptr = *index1Ptr;
    *index1Ptr = startIndex;
    UpdateModifiedFlag(sharedTextPtr, true);
    TkTextUpdateAlteredFlag(sharedTextPtr);
    SetNewTopPosition(sharedTextPtr, textPtr, textPosition, viewUpdate);
    if (textPosition != textPosBuf) {
	free(textPosition);
    }

    /*
     * Invalidate any selection retrievals in progress.
     */

    for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
	tPtr->abortSelections = true;
    }

    if (parseHyphens && text != textBuf) {
	free((char *) text);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TextUndoRedoCallback --
 *
 *	This function is registered with the generic undo/redo code to handle
 *	'insert' and 'delete' actions on all text widgets. We cannot perform
 *	those actions on any particular text widget, because that text widget
 *	might have been deleted by the time we get here.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will change anything, depending on the undo token.
 *
 *----------------------------------------------------------------------
 */

static void
TriggerWatchUndoRedo(
    TkSharedText *sharedTextPtr,
    TkTextUndoToken *token,
    bool isRedo,
    bool isFinal,
    TkText **peers,
    int numPeers)
{
    TkTextIndex index1, index2;
    Tcl_Obj *cmdPtr;
    char arg[100];
    int i;

    assert(sharedTextPtr->triggerWatchCmd);
    assert(token->undoType->rangeProc);
    assert(token->undoType->commandProc);

    sharedTextPtr->triggerWatchCmd = false;
    token->undoType->rangeProc(sharedTextPtr, token, &index1, &index2);
    Tcl_IncrRefCount(cmdPtr = token->undoType->commandProc(sharedTextPtr, token));
    snprintf(arg, sizeof(arg), "{%s} %s", Tcl_GetString(cmdPtr), isFinal ? "yes" : "no");
    Tcl_DecrRefCount(cmdPtr);

    for (i = 0; i < numPeers; ++i) {
	TkText *tPtr = peers[i];

	if (!(tPtr->flags & DESTROYED)) {
	    char idx[2][TK_POS_CHARS];
	    const char *info = isRedo ? "redo" : "undo";

	    TkTextPrintIndex(tPtr, &index1, idx[0]);
	    TkTextPrintIndex(tPtr, &index2, idx[1]);
	    TkTextTriggerWatchCmd(tPtr, info, idx[0], idx[1], arg, false);
	}
    }

    sharedTextPtr->triggerWatchCmd = true;
}

void
TextUndoRedoCallback(
    TkTextUndoStack stack,
    const TkTextUndoAtom *atom)
{
    TkSharedText *sharedTextPtr = (TkSharedText *) TkTextUndoGetContext(stack);
    TkTextUndoInfo undoInfo;
    TkTextUndoInfo redoInfo;
    TkTextUndoInfo *redoInfoPtr;
    TkTextPosition *textPosition = NULL;
    TkTextPosition textPosBuf[PIXEL_CLIENTS];
    bool eventuallyRepick = false;
    TkText *peerArr[20];
    TkText **peers = peerArr;
    TkText *tPtr;
    int i, k, countPeers = 0;

    assert(stack);

    if (sharedTextPtr->triggerWatchCmd) {
	if (sharedTextPtr->numPeers > sizeof(peerArr) / sizeof(peerArr[0])) {
	    peers = malloc(sharedTextPtr->numPeers * sizeof(peerArr[0]));
	}
	for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
	    if (tPtr->watchCmd) {
		TkTextSaveCursorIndex(tPtr);
		peers[countPeers++] = tPtr;
		tPtr->refCount += 1;
	    }
	}
    }

    memset(&undoInfo, 0, sizeof(undoInfo));
    redoInfoPtr = TkTextUndoStackIsFull(stack) ? NULL : &redoInfo;

    for (i = atom->arraySize - 1; i >= 0; --i) {
	TkTextIndex index1, index2;
	const TkTextUndoSubAtom *subAtom = atom->array + i;
	TkTextUndoToken *token = subAtom->item;
	bool isDelete = token->undoType->action == TK_TEXT_UNDO_INSERT
		|| token->undoType->action == TK_TEXT_REDO_DELETE;
	bool isInsert = token->undoType->action == TK_TEXT_UNDO_DELETE
		|| token->undoType->action == TK_TEXT_REDO_INSERT;

	if (!isInsert) {
	    token->undoType->rangeProc(sharedTextPtr, token, &index1, &index2);
	}

	if (isInsert || isDelete) {
	    const TkTextUndoTokenRange *range = (const TkTextUndoTokenRange *) token;

	    if (isDelete && sharedTextPtr->triggerWatchCmd) {
		TriggerWatchUndoRedo(sharedTextPtr, token, subAtom->redo, i == 0, peers, countPeers);
	    }
	    if (!textPosition) {
		if (sharedTextPtr->numPeers > sizeof(textPosBuf)/sizeof(textPosBuf[0])) {
		    textPosition = malloc(sizeof(textPosition[0])*sharedTextPtr->numPeers);
		} else {
		    textPosition = textPosBuf;
		}
		InitPosition(sharedTextPtr, textPosition);
	    }
	    if (isInsert) {
		TkBTreeUndoIndexToIndex(sharedTextPtr, &range->startIndex, &index1);
		TkTextChanged(sharedTextPtr, NULL, &index1, &index1);
		FindNewTopPosition(sharedTextPtr, textPosition, &index1, NULL, subAtom->size);
	    } else {
		TkTextChanged(sharedTextPtr, NULL, &index1, &index2);
		FindNewTopPosition(sharedTextPtr, textPosition, &index1, &index2, 0);
	    }
	    for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
		if (!tPtr->abortSelections) {
		    if (isInsert) {
			tPtr->abortSelections = true;
		    } else {
			if (range->startIndex.lineIndex < range->endIndex.lineIndex
				&& TkBTreeTag(sharedTextPtr, NULL, &index1, &index2,
					tPtr->selTagPtr, false, NULL, TkTextRedrawTag)) {
			    TkTextSelectionEvent(tPtr);
			    tPtr->abortSelections = true;
			}
		    }
		}
	    }
	}

	/*
	 * Now perform the undo/redo action.
	 */

	if (redoInfoPtr) {
	    memset(redoInfoPtr, 0, sizeof(redoInfo));
	}
	undoInfo.token = token;
	undoInfo.byteSize = atom->size;
	token->undoType->undoProc(sharedTextPtr, &undoInfo, redoInfoPtr, atom->redo);

	if (token->undoType->action == TK_TEXT_UNDO_TAG) {
	    eventuallyRepick = true;
	}
	if (isInsert) {
	    token->undoType->rangeProc(sharedTextPtr, token, &index1, &index2);
	}
	if (redoInfoPtr) {
	    if (redoInfo.token == token) {
		/*
		 * We are re-using a token, this is possible because the current undo token
		 * will expire after this action.
		 */
		if (!subAtom->redo) {
		    if (token->undoType->action == TK_TEXT_UNDO_INSERT
			    || token->undoType->action == TK_TEXT_UNDO_DELETE) {
			assert(sharedTextPtr->insertDeleteUndoTokenCount > 0);
			sharedTextPtr->insertDeleteUndoTokenCount -= 1;
		    }
		}
		if (token->undoType->destroyProc) {
		    /* We need a balanced call of perform/destroy. */
		    token->undoType->destroyProc(sharedTextPtr, subAtom->item, true);
		}
		/*
		 * Do not free this item.
		 */
		((TkTextUndoSubAtom *) subAtom)->item = NULL;
	    }
	    TkTextPushUndoToken(sharedTextPtr, redoInfo.token, redoInfo.byteSize);
	}
	if (textPosition) {
	    /*
	     * Take into account that the cursor position may change, we have to
	     * update the old cursor position, otherwise some artefacts may remain.
	     */

	    for (k = 0; k < countPeers; ++k) {
		TkText *tPtr = peers[k];

		if (tPtr->state == TK_TEXT_STATE_NORMAL) {
		    TkTextIndex insIndex[2];

		    TkTextMarkSegToIndex(tPtr, tPtr->insertMarkPtr, &insIndex[0]);
		    if (TkTextIndexForwChars(tPtr, &insIndex[0], 1, &insIndex[1], COUNT_INDICES)) {
			/*
			 * TODO: this will do too much, but currently the implementation
			 * lacks on an efficient redraw functioniality especially designed
			 * for cursor updates.
			 */
			TkTextChanged(NULL, tPtr, &insIndex[0], &insIndex[1]);
		    }
		}
	    }
	}
	if (!isDelete && sharedTextPtr->triggerWatchCmd) {
	    TriggerWatchUndoRedo(sharedTextPtr, token, subAtom->redo, i == 0, peers, countPeers);
	}
    }

    if (eventuallyRepick) {
	for (k = 0; k < countPeers; ++k) {
	    TkText *tPtr = peers[k];

	    if (!(tPtr->flags & DESTROYED)) {
		TkTextEventuallyRepick(tPtr);
	    }
	}
    }

    sharedTextPtr->lastEditMode = TK_TEXT_EDIT_OTHER;
    sharedTextPtr->lastUndoTokenType = -1;
    UpdateModifiedFlag(sharedTextPtr, false);
    TkTextUpdateAlteredFlag(sharedTextPtr);

    if (textPosition) {
	SetNewTopPosition(sharedTextPtr, NULL, textPosition, true);
	if (textPosition != textPosBuf) {
	    free(textPosition);
	}
    }

    if (sharedTextPtr->triggerWatchCmd) {
	for (i = 0; i < countPeers; ++i) {
	    TkText *tPtr = peers[i];

	    if (!(tPtr->flags & DESTROYED)) {
		TkTextIndexClear(&tPtr->insertIndex, tPtr);
		TkTextTriggerWatchCursor(tPtr);
	    }
	    if (--tPtr->refCount == 0) {
		free(tPtr);
	    }
	}
    }

    /*
     * Freeing the peer array has to be done even if sharedTextPtr->triggerWatchCmd
     * is false, possibly the user has cleared the watch command inside the trigger
     * callback.
     */

    if (peers != peerArr) {
	free(peers);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TextUndoStackContentChangedCallback --
 *
 *	This function is registered with the generic undo/redo code to handle
 *	undo/redo stack changes.
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
TextUndoStackContentChangedCallback(
    const TkTextUndoStack stack)
{
    ((TkSharedText *) TkTextUndoGetContext(stack))->undoStackEvent = true;
}

/*
 *----------------------------------------------------------------------
 *
 * TriggerUndoStackEvent --
 *
 *	This function is triggering the <<UndoStack>> event for all peers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May force the text window (and all peers) into existence.
 *
 *----------------------------------------------------------------------
 */

static void
TriggerUndoStackEvent(
    TkSharedText *sharedTextPtr)
{
    TkText *textPtr;

    assert(sharedTextPtr->undoStackEvent);
    sharedTextPtr->undoStackEvent = false;

    for (textPtr = sharedTextPtr->peers; textPtr; textPtr = textPtr->next) {
	if (!(textPtr->flags & DESTROYED)) {
	    Tk_MakeWindowExist(textPtr->tkwin);
	    SendVirtualEvent(textPtr->tkwin, "UndoStack", NULL);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TextUndoFreeCallback --
 *
 *	This function is registered with the generic undo/redo code to handle
 *	the freeing operation of undo/redo items.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Some memory will be freed.
 *
 *----------------------------------------------------------------------
 */

static void
TextUndoFreeCallback(
    const TkTextUndoStack stack,
    const TkTextUndoSubAtom *subAtom)	/* Destroy this token. */
{
    TkTextUndoToken *token = (TkTextUndoToken *) subAtom->item;

    /*
     * Consider that the token is possibly null.
     */

    if (token) {
	TkTextUndoAction action = token->undoType->action;

	if (action == TK_TEXT_UNDO_INSERT || action == TK_TEXT_UNDO_DELETE) {
	    TkSharedText *sharedTextPtr = (TkSharedText *) TkTextUndoGetContext(stack);
	    assert(sharedTextPtr->insertDeleteUndoTokenCount > 0);
	    sharedTextPtr->insertDeleteUndoTokenCount -= 1;
	}
	if (token->undoType->destroyProc) {
	    token->undoType->destroyProc(TkTextUndoGetContext(stack), subAtom->item, false);
	}
	free(subAtom->item);
	DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CountIndices --
 *
 *	This function implements most of the functionality of the "count"
 *	widget command.
 *
 *	Note that 'textPtr' is only used if we need to check for elided
 *	attributes, i.e. if type is COUNT_DISPLAY_INDICES or
 *	COUNT_DISPLAY_CHARS
 *
 * Results:
 *	Returns the number of characters in the range.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
CountIndices(
    const TkText *textPtr,	/* Overall information about text widget. */
    const TkTextIndex *indexPtr1,
				/* Index describing location of first character to delete. */
    const TkTextIndex *indexPtr2,
				/* Index describing location of last character to delete. NULL means
				 * just delete the one character given by indexPtr1. */
    TkTextCountType type)	/* The kind of indices to count. */
{
    /*
     * Order the starting and stopping indices.
     */

    int compare = TkTextIndexCompare(indexPtr1, indexPtr2);

    if (compare == 0) {
	return 0;
    }
    if (compare > 0) {
	return -((int) TkTextIndexCount(textPtr, indexPtr2, indexPtr1, type));
    }
    return TkTextIndexCount(textPtr, indexPtr1, indexPtr2, type);
}

/*
 *----------------------------------------------------------------------
 *
 * DeleteIndexRange --
 *
 *	This function implements most of the functionality of the "delete"
 *	widget command.
 *
 * Results:
 *	Returns whether the widget hasn't been destroyed.
 *
 * Side effects:
 *	Characters and other entities (windows, images) get deleted from the
 *	text.
 *
 *	If 'viewUpdate' is true, we may adjust the window contents'
 *	y-position, and scrollbar setting.
 *
 *	If 'viewUpdate' is true, true we can guarantee that textPtr->topIndex
 *	points to a valid TkTextLine after this function returns. However, if
 *	'viewUpdate' is false, then there is no such guarantee (since
 *	topIndex.linePtr can be garbage). The caller is expected to take
 *	actions to ensure the topIndex is validated before laying out the
 *	window again.
 *
 *----------------------------------------------------------------------
 */

static bool
DetectUndoTag(
    const TkTextTag *tagPtr)
{
    for ( ; tagPtr; tagPtr = tagPtr->nextPtr) {
	if (!TkBitTest(tagPtr->sharedTextPtr->dontUndoTags, tagPtr->index)) {
	    return true;
	}
    }
    return false;
}

static bool
HaveMarksInRange(
    const TkTextIndex *indexPtr1,
    const TkTextIndex *indexPtr2,
    int flags)
{
    const TkTextSegment *segPtr1;
    const TkTextSegment *segPtr2;

    assert(TkTextIndexIsEqual(indexPtr1, indexPtr2));

    segPtr2 = TkTextIndexGetSegment(indexPtr2);
    if (!segPtr2) {
	return false;
    }
    if (flags & DELETE_INCLUSIVE) {
	segPtr2 = segPtr2->nextPtr;
	assert(segPtr2);
    }

    segPtr1 = TkTextIndexGetSegment(indexPtr1);
    if (!segPtr1 || !TkTextIsStableMark(segPtr1)) {
	segPtr1 = TkTextIndexGetFirstSegment(indexPtr1, NULL);
    } else if (!(flags & DELETE_INCLUSIVE)) {
	segPtr1 = segPtr1->nextPtr;
	assert(segPtr1);
    }

    for ( ; segPtr1 && segPtr1->size == 0; segPtr1 = segPtr1->nextPtr) {
	if (TkTextIsNormalMark(segPtr1)) {
	    return true;
	}
	if (segPtr1 == segPtr2) {
	    return false;
	}
    }
    return false;
}

static bool
DeleteMarksOnLastLine(
    TkText *textPtr,
    TkTextSegment *segPtr,
    TkTextSegment *endPtr,
    int flags)
{
    bool rc;

    assert(endPtr);

    if (flags & DELETE_INCLUSIVE) {
	endPtr = endPtr->nextPtr;
	assert(endPtr);
    }

    if (!segPtr) {
	segPtr = endPtr->sectionPtr->linePtr->segPtr;
    } else if (!(flags & DELETE_INCLUSIVE)) {
	segPtr = segPtr->nextPtr;
    }

    rc = false;
    while (segPtr != endPtr) {
	TkTextSegment *nextPtr = segPtr->nextPtr;

	if (TkTextIsNormalMark(segPtr)) {
	    TkTextUnsetMark(textPtr, segPtr);
	    rc = true;
	}

	segPtr = nextPtr;
    }

    return rc;
}

static bool
DeleteIndexRange(
    TkSharedText *sharedTextPtr,/* Shared portion of peer widgets. */
    TkText *textPtr,		/* Overall information about text widget. */
    const TkTextIndex *indexPtr1,
				/* Index describing location of first character (or other entity)
				 * to delete. */
    const TkTextIndex *indexPtr2,
				/* Index describing location of last character (or other entity)
				 * to delete. NULL means just delete the one character given by
				 * indexPtr1. */
    int flags,			/* Flags controlling the deletion. */
    bool viewUpdate,		/* Update vertical view if set. */
    bool triggerWatchDelete,	/* Should we trigger the watch command for deletion? */
    bool triggerWatchInsert,	/* Should we trigger the watch command for insertion? */
    bool userFlag,		/* Trigger user modification? */
    bool final)			/* This is the final call in a sequence of ranges. */
{
    TkTextIndex index1, index2, index3;
    TkTextPosition *textPosition;
    TkTextPosition textPosBuf[PIXEL_CLIENTS];
    TkTextUndoInfo undoInfo;
    TkTextUndoInfo *undoInfoPtr;
    bool deleteOnLastLine;
    bool altered;
    int cmp;

    if (!sharedTextPtr) {
	sharedTextPtr = textPtr->sharedTextPtr;
    }

    if (triggerWatchInsert) {
	TkTextIndexToByteIndex((TkTextIndex *) indexPtr1); /* mutable due to concept */
    }

    /*
     * Prepare the starting and stopping indices.
     */

    index1 = *indexPtr1;

    if (indexPtr2) {
	if ((cmp = TkTextIndexCompare(&index1, indexPtr2)) > 0) {
	    return true; /* there is nothing to delete */
	}
	index2 = *indexPtr2;
    } else if (!TkTextIndexForwChars(textPtr, &index1, 1, &index2, COUNT_INDICES)) {
	cmp = 0;
    } else {
	cmp = -1;
    }

    if (cmp == 0) {
	bool isTagged;

	deleteOnLastLine = (flags & DELETE_MARKS) && HaveMarksInRange(&index1, &index2, flags);
	isTagged = TkTextIndexBackChars(textPtr, &index2, 1, &index3, COUNT_INDICES)
		&& TkBTreeCharTagged(&index3, NULL);

	if (!deleteOnLastLine && !isTagged) {
	    return true; /* there is nothing to delete */
	}
    } else if (TkTextIndexIsEndOfText(&index1)) {
	if (!TkTextIndexBackChars(textPtr, &index2, 1, &index3, COUNT_INDICES)
		|| TkTextIndexIsStartOfText(&index3)
		|| !TkBTreeCharTagged(&index3, NULL)) {
	    return true; /* there is nothing to delete */
	}
	deleteOnLastLine = (flags & DELETE_MARKS) && HaveMarksInRange(&index1, &index2, flags);
    } else {
	deleteOnLastLine = false;
    }

    /*
     * Call the "watch" command for deletion. Take into account that the
     * receiver might change the text content inside the callback, although
     * he shouldn't do this.
     */

    if (triggerWatchDelete) {
	Tcl_Obj *delObj = TextGetText(textPtr, &index1, &index2, NULL, NULL, UINT_MAX, false, true);
	char const *deleted = Tcl_GetString(delObj);
	bool unchanged;
	bool rc;

	TkTextIndexSave(&index1);
	TkTextIndexSave(&index2);
	Tcl_IncrRefCount(delObj);
	rc = TriggerWatchEdit(textPtr, "delete", &index1, &index2, deleted, final);
	Tcl_DecrRefCount(delObj);
	unchanged = TkTextIndexRebuild(&index1) && TkTextIndexRebuild(&index2);

	if (!rc) { return false; } /* the receiver has destroyed this widget */

	if (!unchanged && TkTextIndexCompare(&index1, &index2) >= 0) {
	    /* This can only happen if the receiver of the trigger command did any modification. */
	    return true;
	}
    }

    if (cmp < 0) {
	TkTextClearSelection(sharedTextPtr, &index1, &index2);
    }

    altered = (cmp < 0);

    if (deleteOnLastLine) {
	/*
	 * Some marks on last line have to be deleted. We are doing this separately,
	 * because we won't delete the last line.
	 *
	 * The alternative is to insert a newly last newline instead, so we can remove
	 * the last line, but this is more complicated than doing this separate removal
	 * (consider undo, or the problem with end markers).
	 */

	if (DeleteMarksOnLastLine(textPtr, TkTextIndexGetSegment(&index1),
		TkTextIndexGetSegment(&index2), flags)) {
	    altered = true;
	}
    }

    if (TkTextIndexIsEndOfText(&index2)) {
	TkTextIndexGetByteIndex(&index2);
	index3 = index2;

	TkTextIndexBackChars(textPtr, &index2, 1, &index3, COUNT_INDICES);

	if (!textPtr->endMarker->sectionPtr->linePtr->nextPtr) {
	    /*
	     * We're about to delete the very last (empty) newline, and this must not
	     * happen. Instead of deleting the newline we will remove all tags from
	     * this newline character (as if we delete this newline, and afterwards
	     * a fresh newline will be appended).
	     */

	    if (DetectUndoTag(TkTextClearTags(sharedTextPtr, textPtr, &index3, &index2, false))) {
		altered = true;
	    }
	    assert(altered);
	}
    } else {
	index3 = index2;
    }

    if (cmp < 0 && !TkTextIndexIsEqual(&index1, &index3)) {
	/*
	 * Tell the display what's about to happen, so it can discard obsolete
	 * display information, then do the deletion. Also, if the deletion
	 * involves the top line on the screen, then we have to reset the view
	 * (the deletion will invalidate textPtr->topIndex). Compute what the new
	 * first character will be, then do the deletion, then reset the view.
	 */

	TkTextChanged(sharedTextPtr, NULL, &index1, &index3);

	if (sharedTextPtr->numPeers > sizeof(textPosBuf)/sizeof(textPosBuf[0])) {
	    textPosition = malloc(sizeof(textPosition[0])*sharedTextPtr->numPeers);
	} else {
	    textPosition = textPosBuf;
	}
	InitPosition(sharedTextPtr, textPosition);
	FindNewTopPosition(sharedTextPtr, textPosition, &index1, &index2, 0);

	undoInfoPtr = TkTextUndoStackIsFull(sharedTextPtr->undoStack) ? NULL : &undoInfo;
	TkBTreeDeleteIndexRange(sharedTextPtr, &index1, &index3, flags, undoInfoPtr);

	/*
	 * Push the deletion onto the undo stack, and update the modified status of the widget.
	 * Try to join with previously pushed undo token, if possible.
	 */

	if (undoInfoPtr) {
	    const TkTextUndoSubAtom *subAtom;

	    if (sharedTextPtr->autoSeparators && sharedTextPtr->lastEditMode != TK_TEXT_EDIT_DELETE) {
		PushRetainedUndoTokens(sharedTextPtr);
		TkTextUndoPushSeparator(sharedTextPtr->undoStack, true);
		sharedTextPtr->lastUndoTokenType = -1;
	    }

	    if (TkTextUndoGetMaxSize(sharedTextPtr->undoStack) == 0
		    || TkTextUndoGetCurrentSize(sharedTextPtr->undoStack) + undoInfo.byteSize
			    <= TkTextUndoGetMaxSize(sharedTextPtr->undoStack)) {
		if (sharedTextPtr->lastUndoTokenType != TK_TEXT_UNDO_DELETE
			|| !((subAtom = TkTextUndoGetLastUndoSubAtom(sharedTextPtr->undoStack))
				&& TkBTreeJoinUndoDelete(subAtom->item, subAtom->size,
					undoInfo.token, undoInfo.byteSize))) {
		    TkTextPushUndoToken(sharedTextPtr, undoInfo.token, undoInfo.byteSize);
		}
		sharedTextPtr->lastUndoTokenType = TK_TEXT_UNDO_DELETE;
		sharedTextPtr->prevUndoStartIndex =
			((TkTextUndoTokenRange *) undoInfo.token)->startIndex;
		sharedTextPtr->prevUndoEndIndex = ((TkTextUndoTokenRange *) undoInfo.token)->endIndex;
		/* stack has changed anyway, but TkBTreeJoinUndoDelete didn't trigger */
		sharedTextPtr->undoStackEvent = true;
	    } else {
		assert(undoInfo.token->undoType->destroyProc);
		undoInfo.token->undoType->destroyProc(sharedTextPtr, undoInfo.token, false);
		free(undoInfo.token);
		DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
	    }

	    sharedTextPtr->lastEditMode = TK_TEXT_EDIT_DELETE;
	}

	UpdateModifiedFlag(sharedTextPtr, true);

	SetNewTopPosition(sharedTextPtr, textPtr, textPosition, viewUpdate);
	if (textPosition != textPosBuf) {
	    free(textPosition);
	}
    }

    if (altered) {
	TkTextUpdateAlteredFlag(sharedTextPtr);
    }

    /*
     * Lastly, trigger the "watch" command for insertion. This must be the last action,
     * probably the receiver is calling some widget commands inside the callback.
     */

    if (triggerWatchInsert) {
	if (!TriggerWatchEdit(textPtr, "insert", indexPtr1, indexPtr1, NULL, final)) {
	    return false; /* widget has been destroyed */
	}
    }

    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TextFetchSelection --
 *
 *	This function is called back by Tk when the selection is requested by
 *	someone. It returns part or all of the selection in a buffer provided
 *	by the caller.
 *
 * Results:
 *	The return value is the number of non-NULL bytes stored at buffer.
 *	Buffer is filled (or partially filled) with a NULL-terminated string
 *	containing part or all of the selection, as given by offset and
 *	maxBytes.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TextFetchSelection(
    ClientData clientData,	/* Information about text widget. */
    int offset,			/* Offset within selection of first character to be returned. */
    char *buffer,		/* Location in which to place selection. */
    int maxBytes)		/* Maximum number of bytes to place at buffer, not including
    				 * terminating NULL character. */
{
    TkText *textPtr = clientData;
    TkTextSearch *searchPtr;
    Tcl_Obj *selTextPtr;
    int numBytes;

    if (!textPtr->exportSelection) {
	return -1;
    }

    /*
     * Find the beginning of the next range of selected text. Note: if the
     * selection is being retrieved in multiple pieces (offset != 0) and some
     * modification has been made to the text that affects the selection then
     * reject the selection request (make 'em start over again).
     */

    if (offset == 0) {
	TkTextIndexSetupToStartOfText(&textPtr->selIndex, textPtr, textPtr->sharedTextPtr->tree);
	textPtr->abortSelections = false;
    } else if (textPtr->abortSelections) {
	return 0;
    }

    searchPtr = &textPtr->selSearch;

    if (offset == 0 || !TkBTreeCharTagged(&textPtr->selIndex, textPtr->selTagPtr)) {
	TkTextIndex eof;

	TkTextIndexSetupToEndOfText(&eof, textPtr, textPtr->sharedTextPtr->tree);
	TkBTreeStartSearch(&textPtr->selIndex, &eof, textPtr->selTagPtr, searchPtr, SEARCH_NEXT_TAGON);
	if (!TkBTreeNextTag(searchPtr)) {
	    return offset == 0 ? -1 : 0;
	}
	textPtr->selIndex = searchPtr->curIndex;

	/*
	 * Find the end of the current range of selected text.
	 */

	if (!TkBTreeNextTag(searchPtr)) {
	    assert(!"TextFetchSelection couldn't find end of range");
	}
    } else {
	/* we are still inside tagged range */
    }

    /*
     * Iterate through the the selected ranges and collect the text content.
     *
     * NOTE:
     * The crux with TextFetchSelection is the old interface of this callback function,
     * it does not fit with the object design (Tcl_Obj), otherwise it would expect an
     * object as the result. Thus the actual "natural" implementation is a bit
     * ineffecient, because we are collecting the data with an object (we are using the
     * "get" mechanism), and afterwards the content of this object will be copied into
     * the buffer, and the object will be destroyed. Hopefully some day function
     * TextFetchSelection will be changed to new object design.
     */

    Tcl_IncrRefCount(selTextPtr = Tcl_NewObj());

    while (true) {
	TextGetText(textPtr, &textPtr->selIndex, &searchPtr->curIndex, &textPtr->selIndex,
		selTextPtr, maxBytes - GetByteLength(selTextPtr), true, false);

	if (GetByteLength(selTextPtr) == maxBytes) {
	    break;
	}

	/*
	 * Find the beginning of the next range of selected text.
	 */

	if (!TkBTreeNextTag(searchPtr)) {
	    break;
	}

	textPtr->selIndex = searchPtr->curIndex;

	/*
	 * Find the end of the current range of selected text.
	 */

	if (!TkBTreeNextTag(searchPtr)) {
	    assert(!"TextFetchSelection couldn't find end of range");
	}
    }

    numBytes = GetByteLength(selTextPtr);
    memcpy(buffer, Tcl_GetString(selTextPtr), numBytes);
    Tcl_DecrRefCount(selTextPtr);
    return numBytes;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextSelectionEvent --
 *
 *	When anything relevant to the "sel" tag has been changed, call this
 *	function to generate a <<Selection>> event.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If <<Selection>> bindings are present, they will trigger.
 *
 *----------------------------------------------------------------------
 */

void
TkTextSelectionEvent(
    TkText *textPtr)
{
    /*
     * Send an event that the selection changed. This is equivalent to:
     *     event generate $textWidget <<Selection>>
     */

    SendVirtualEvent(textPtr->tkwin, "Selection", NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextLostSelection --
 *
 *	This function is called back by Tk when the selection is grabbed away
 *	from a text widget. On Windows and Mac systems, we want to remember
 *	the selection for the next time the focus enters the window. On Unix,
 *	just remove the "sel" tag from everything in the widget.
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
TkTextLostSelection(
    ClientData clientData)	/* Information about text widget. */
{
    TkText *textPtr = clientData;

    if (TkpAlwaysShowSelection(textPtr->tkwin)) {
	TkTextIndex start, end;

	if (!textPtr->exportSelection) {
	    return;
	}

	/*
	 * On Windows and Mac systems, we want to remember the selection for
	 * the next time the focus enters the window. On Unix, just remove the
	 * "sel" tag from everything in the widget.
	 */

	TkTextIndexSetupToStartOfText(&start, textPtr, textPtr->sharedTextPtr->tree);
	TkTextIndexSetupToEndOfText(&end, textPtr, textPtr->sharedTextPtr->tree);
	TkBTreeTag(textPtr->sharedTextPtr, textPtr, &start, &end, textPtr->selTagPtr,
		false, NULL, TkTextRedrawTag);
    }

    /*
     * Send an event that the selection changed. This is equivalent to:
     *	   event generate $textWidget <<Selection>>
     */

    TkTextSelectionEvent(textPtr);

    textPtr->flags &= ~GOT_SELECTION;
}

/*
 *----------------------------------------------------------------------
 *
 * TextBlinkProc --
 *
 *	This function is called as a timer handler to blink the insertion
 *	cursor off and on.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The cursor gets turned on or off, redisplay gets invoked, and this
 *	function reschedules itself.
 *
 *----------------------------------------------------------------------
 */

static void
TextBlinkProc(
    ClientData clientData)	/* Pointer to record describing text. */
{
    TkText *textPtr = clientData;
    unsigned oldFlags = textPtr->flags;

    if (textPtr->state == TK_TEXT_STATE_DISABLED
	    || !(textPtr->flags & HAVE_FOCUS)
	    || textPtr->insertOffTime == 0) {
	if (!(textPtr->flags & HAVE_FOCUS) && textPtr->insertUnfocussed != TK_TEXT_INSERT_NOFOCUS_NONE) {
	    /*
	     * The widget doesn't have the focus yet it is configured to
	     * display the cursor when it doesn't have the focus. Act now!
	     */

	    textPtr->flags |= INSERT_ON;
	} else if (textPtr->insertOffTime == 0) {
	    /*
	     * The widget was configured to have zero offtime while the
	     * insertion point was not displayed. We have to display it once.
	     */

	    textPtr->flags |= INSERT_ON;
	}
    } else {
	if (textPtr->flags & INSERT_ON) {
	    textPtr->flags &= ~INSERT_ON;
	    textPtr->insertBlinkHandler = Tcl_CreateTimerHandler(
		    textPtr->insertOffTime, TextBlinkProc, textPtr);
	} else {
	    textPtr->flags |= INSERT_ON;
	    textPtr->insertBlinkHandler = Tcl_CreateTimerHandler(
		    textPtr->insertOnTime, TextBlinkProc, textPtr);
	}
    }

    if (oldFlags != textPtr->flags) {
	TkTextIndex index;
	int x, y, w, h, charWidth;

	TkTextMarkSegToIndex(textPtr, textPtr->insertMarkPtr, &index);

	if (TkTextIndexBbox(textPtr, &index, false, &x, &y, &w, &h, &charWidth)) {
	    if (textPtr->blockCursorType) { /* Block cursor */
		x -= textPtr->width/2;
		w = charWidth + textPtr->insertWidth/2;
	    } else { /* I-beam cursor */
		x -= textPtr->insertWidth/2;
		w = textPtr->insertWidth;
	    }
	    TkTextRedrawRegion(textPtr, x, y, w, h);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TextInsertCmd --
 *
 *	This function is invoked to process the "insert" and "replace" widget
 *	commands for text widgets.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *	If 'viewUpdate' is true, we may adjust the window contents'
 *	y-position, and scrollbar setting.
 *
 *----------------------------------------------------------------------
 */

static int
TextInsertCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[],	/* Argument objects. */
    const TkTextIndex *indexPtr,/* Index at which to insert. */
    bool viewUpdate,		/* Update the view if set. */
    bool triggerWatchDelete,	/* Should we trigger the watch command for deletion? */
    bool triggerWatchInsert,	/* Should we trigger the watch command for insertion? */
    bool userFlag,		/* Trigger user modification? */
    bool *destroyed,		/* Store whether the widget has been destroyed. */
    bool parseHyphens)		/* Should we parse hyphens? (tk_textInsert) */
{
    TkTextIndex index1, index2;
    TkSharedText *sharedTextPtr;
    TkTextTag *hyphenTagPtr = NULL;
    int rc = TCL_OK;
    int j;

    assert(textPtr);
    assert(destroyed);
    assert(!TkTextIsDeadPeer(textPtr));

    sharedTextPtr = textPtr->sharedTextPtr;
    *destroyed = false;

    if (parseHyphens && objc > 1 && *Tcl_GetString(objv[0]) == '-') {
	int argc;
	Tcl_Obj **argv;

	if (strcmp(Tcl_GetString(objv[0]), "-hyphentags") != 0) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "bad option \"%s\": must be -hyphentags", Tcl_GetString(objv[0])));
	    Tcl_SetErrorCode(interp, "TK", "TEXT", "INDEX_OPTION", NULL);
	    return TCL_ERROR;
	}
	if (Tcl_ListObjGetElements(interp, objv[1], &argc, &argv) != TCL_OK) {
	    return TCL_ERROR;
	}
	for (j = 0; j < argc; ++j) {
	    TkTextTag *tagPtr = TkTextCreateTag(textPtr, Tcl_GetString(argv[j]), NULL);
	    tagPtr->nextPtr = hyphenTagPtr;
	    hyphenTagPtr = tagPtr;
	}
	objc -= 2;
	objv += 2;
    }

    for (j = 0; j < objc && GetByteLength(objv[j]) == 0; j += 2) {
	/* empty loop body */
    }
    index1 = *indexPtr;

    while (j < objc) {
	Tcl_Obj *stringPtr = objv[j];
	Tcl_Obj *tagPtr = (j + 1 < objc) ? objv[j + 1] : NULL;
	char const *string = Tcl_GetString(stringPtr);
	unsigned length = GetByteLength(stringPtr);
	int k = j + 2;
	bool final;

	while (k < objc && GetByteLength(objv[k]) == 0) {
	    k += 2;
	}
	final = objc <= k;

	if (length > 0) {
	    int numTags = 0;
	    Tcl_Obj **tagNamePtrs = NULL;
	    TkTextTagSet *tagInfoPtr = NULL;

	    /*
	     * Call the "watch" command for deletion. Take into account that the
	     * receiver might change the text content, although he shouldn't do this.
	     */

	    if (triggerWatchDelete) {
		TkTextIndexSave(&index1);
		if (!TriggerWatchEdit(textPtr, "delete", &index1, &index1, NULL, final)) {
		    *destroyed = true;
		    return rc;
		}
		TkTextIndexRebuild(&index1);
	    }

	    if (tagPtr) {
		int i;

		if (Tcl_ListObjGetElements(interp, tagPtr, &numTags, &tagNamePtrs) != TCL_OK) {
		    rc = TCL_ERROR;
		} else if (numTags > 0) {
		    TkTextTag *tagPtr;

		    tagInfoPtr = TkTextTagSetResize(NULL, sharedTextPtr->tagInfoSize);

		    for (i = 0; i < numTags; ++i) {
			tagPtr = TkTextCreateTag(textPtr, Tcl_GetString(tagNamePtrs[i]), NULL);
#if !TK_TEXT_DONT_USE_BITFIELDS
			if (tagPtr->index >= TkTextTagSetSize(tagInfoPtr)) {
			    tagInfoPtr = TkTextTagSetResize(NULL, sharedTextPtr->tagInfoSize);
			}
#endif
			tagInfoPtr = TkTextTagSetAdd(tagInfoPtr, tagPtr->index);
		    }
		}
	    }

	    InsertChars(textPtr, &index1, &index2, string, length,
		    viewUpdate, tagInfoPtr, hyphenTagPtr, parseHyphens);
	    if (tagInfoPtr) {
		TkTextTagSetDecrRefCount(tagInfoPtr);
	    }

	    /*
	     * Lastly, trigger the "watch" command for insertion. This must be the last action,
	     * probably the receiver is calling some widget commands inside the callback.
	     */

	    if (triggerWatchInsert) {
		if (!TriggerWatchEdit(textPtr, "insert", &index1, &index2, string, final)) {
		    *destroyed = true;
		    return rc;
		}
	    }

	    if (rc != TCL_OK) {
		return rc;
	    }
	    index1 = index2;
	}

	j = k;
    }

    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * TextSearchCmd --
 *
 *	This function is invoked to process the "search" widget command for
 *	text widgets. See the user documentation for details on what it does.
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
TextSearchCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    int i, argsLeft, code;
    SearchSpec searchSpec;

    static const char *const switchStrings[] = {
	"-hidden",
	"--", "-all", "-backwards", "-count", "-discardhyphens", "-elide",
	"-exact", "-forwards", "-nocase", "-nolinestop", "-overlap", "-regexp",
	"-strictlimits", NULL
    };
    enum SearchSwitches {
	SEARCH_HIDDEN,
	SEARCH_END, SEARCH_ALL, SEARCH_BACK, SEARCH_COUNT, SEARCH_DISCARDHYPHENS, SEARCH_ELIDE,
	SEARCH_EXACT, SEARCH_FWD, SEARCH_NOCASE, SEARCH_NOLINESTOP, SEARCH_OVERLAP, SEARCH_REGEXP,
	SEARCH_STRICTLIMITS
    };

    /*
     * Set up the search specification, including the last 4 fields which are
     * text widget specific.
     */

    searchSpec.textPtr = textPtr;
    searchSpec.exact = true;
    searchSpec.noCase = false;
    searchSpec.all = false;
    searchSpec.backwards = false;
    searchSpec.varPtr = NULL;
    searchSpec.countPtr = NULL;
    searchSpec.resPtr = NULL;
    searchSpec.searchElide = false;
    searchSpec.searchHyphens = true;
    searchSpec.noLineStop = false;
    searchSpec.overlap = false;
    searchSpec.strictLimits = false;
    searchSpec.numLines = TkBTreeNumLines(textPtr->sharedTextPtr->tree, textPtr);
    searchSpec.clientData = textPtr;
    searchSpec.addLineProc = &TextSearchAddNextLine;
    searchSpec.foundMatchProc = &TextSearchFoundMatch;
    searchSpec.lineIndexProc = &TextSearchGetLineIndex;

    /*
     * Parse switches and other arguments.
     */

    for (i = 2; i < objc; ++i) {
	int index;

	if (Tcl_GetString(objv[i])[0] != '-') {
	    break;
	}

	if (Tcl_GetIndexFromObjStruct(NULL, objv[i], switchStrings,
		sizeof(char *), "switch", 0, &index) != TCL_OK) {
	    /*
	     * Hide the -hidden option, generating the error description with
	     * the side effects of T_GIFO.
	     */

	    (void) Tcl_GetIndexFromObjStruct(interp, objv[i], switchStrings + 1,
		    sizeof(char *), "switch", 0, &index);
	    return TCL_ERROR;
	}

	switch ((enum SearchSwitches) index) {
	case SEARCH_END:
	    i += 1;
	    goto endOfSwitchProcessing;
	case SEARCH_ALL:
	    searchSpec.all = true;
	    break;
	case SEARCH_BACK:
	    searchSpec.backwards = true;
	    break;
	case SEARCH_COUNT:
	    if (i >= objc - 1) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("no value given for \"-count\" option", -1));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "VALUE", NULL);
		return TCL_ERROR;
	    }
	    i += 1;

	    /*
	     * Assumption objv[i] isn't going to disappear on us during this
	     * function, which is fair.
	     */

	    searchSpec.varPtr = objv[i];
	    break;
	case SEARCH_DISCARDHYPHENS:
	    searchSpec.searchHyphens = false;
	    break;
	case SEARCH_ELIDE:
	case SEARCH_HIDDEN:
	    searchSpec.searchElide = true;
	    break;
	case SEARCH_EXACT:
	    searchSpec.exact = true;
	    break;
	case SEARCH_FWD:
	    searchSpec.backwards = false;
	    break;
	case SEARCH_NOCASE:
	    searchSpec.noCase = true;
	    break;
	case SEARCH_NOLINESTOP:
	    searchSpec.noLineStop = true;
	    break;
	case SEARCH_OVERLAP:
	    searchSpec.overlap = true;
	    break;
	case SEARCH_STRICTLIMITS:
	    searchSpec.strictLimits = true;
	    break;
	case SEARCH_REGEXP:
	    searchSpec.exact = false;
	    break;
	default:
	    assert(!"unexpected switch fallthrough");
	}
    }
  endOfSwitchProcessing:

    argsLeft = objc - (i + 2);
    if (argsLeft != 0 && argsLeft != 1) {
	Tcl_WrongNumArgs(interp, 2, objv, "?switches? pattern index ?stopIndex?");
	return TCL_ERROR;
    }

    if (searchSpec.noLineStop && searchSpec.exact) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"the \"-nolinestop\" option requires the \"-regexp\" option to be present", -1));
	Tcl_SetErrorCode(interp, "TK", "TEXT", "SEARCH_USAGE", NULL);
	return TCL_ERROR;
    }

    if (searchSpec.overlap && !searchSpec.all) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"the \"-overlap\" option requires the \"-all\" option to be present", -1));
	Tcl_SetErrorCode(interp, "TK", "TEXT", "SEARCH_USAGE", NULL);
	return TCL_ERROR;
    }

    /*
     * Scan through all of the lines of the text circularly, starting at the
     * given index. 'objv[i]' is the pattern which may be an exact string or a
     * regexp pattern depending on the flags set above.
     */

    code = SearchPerform(interp, &searchSpec, objv[i], objv[i + 1], argsLeft == 1 ? objv[i + 2] : NULL);
    if (code != TCL_OK) {
	goto cleanup;
    }

    /*
     * Set the '-count' variable, if given.
     */

    if (searchSpec.varPtr && searchSpec.countPtr) {
	Tcl_IncrRefCount(searchSpec.countPtr);
	if (!Tcl_ObjSetVar2(interp, searchSpec.varPtr, NULL, searchSpec.countPtr, TCL_LEAVE_ERR_MSG)) {
	    code = TCL_ERROR;
	    goto cleanup;
	}
    }

    /*
     * Set the result.
     */

    if (searchSpec.resPtr) {
	Tcl_SetObjResult(interp, searchSpec.resPtr);
	searchSpec.resPtr = NULL;
    }

  cleanup:
    if (searchSpec.countPtr) {
	Tcl_DecrRefCount(searchSpec.countPtr);
    }
    if (searchSpec.resPtr) {
	Tcl_DecrRefCount(searchSpec.resPtr);
    }
    return code;
}

/*
 *----------------------------------------------------------------------
 *
 * TextSearchGetLineIndex --
 *
 *	Extract a row, text offset index position from an objPtr.
 *
 *	This means we ignore any embedded windows/images and elidden text
 *	(unless we are searching that).
 *
 * Results:
 *	Standard Tcl error code (with a message in the interpreter on error
 *	conditions).
 *
 *	The offset placed in offsetPosPtr is a utf-8 char* byte index for
 *	exact searches, and a Unicode character index for regexp searches.
 *
 *	The line number should start at zero (searches which wrap around
 *	assume the first line is numbered 0).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TextSearchGetLineIndex(
    Tcl_Interp *interp,		/* For error messages. */
    Tcl_Obj *objPtr,		/* Contains a textual index like "1.2" */
    SearchSpec *searchSpecPtr,	/* Contains other search parameters. */
    int *linePosPtr,		/* For returning the line number. */
    int *offsetPosPtr)		/* For returning the text offset in the line. */
{
    TkTextIndex index;
    int line, byteIndex;
    TkText *textPtr = searchSpecPtr->clientData;
    TkTextLine *linePtr;

    if (!TkTextGetIndexFromObj(interp, textPtr, objPtr, &index)) {
	return TCL_ERROR;
    }

    assert(textPtr);
    line = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, textPtr, TkTextIndexGetLine(&index), NULL);
    if (line >= searchSpecPtr->numLines) {
	line = searchSpecPtr->numLines - 1;
	linePtr = TkBTreeFindLine(textPtr->sharedTextPtr->tree, textPtr, line);
	assert(linePtr); /* this may only fail with dead peers */
	if (textPtr->endMarker == textPtr->sharedTextPtr->endMarker
		|| textPtr->endMarker->sectionPtr->linePtr != TkTextIndexGetLine(&index)) {
	    byteIndex = linePtr->size;
	} else {
	    byteIndex = TkTextSegToIndex(textPtr->endMarker);
	}
    } else {
	linePtr = TkTextIndexGetLine(&index);
	byteIndex = TkTextIndexGetByteIndex(&index);
    }

    *offsetPosPtr = TextSearchIndexInLine(searchSpecPtr, linePtr, byteIndex);
    *linePosPtr = line;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TextSearchIndexInLine --
 *
 *	Find textual index of 'byteIndex' in the searchable characters of
 *	'linePtr'.
 *
 *	This means we ignore any embedded windows/images and elidden text
 *	(unless we are searching that).
 *
 * Results:
 *	The returned index is a utf-8 char* byte index for exact searches, and
 *	a Unicode character index for regexp searches.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static unsigned
CountCharsInSeg(
    const TkTextSegment *segPtr)
{
    assert(segPtr->typePtr == &tkTextCharType);
    return Tcl_NumUtfChars(segPtr->body.chars, segPtr->size);
}

static unsigned
TextSearchIndexInLine(
    const SearchSpec *searchSpecPtr,
				/* Search parameters. */
    TkTextLine *linePtr,	/* The line we're looking at. */
    int byteIndex)		/* Index into the line. */
{
    TkTextSegment *segPtr;
    int leftToScan;
    unsigned index = 0;
    TkText *textPtr = searchSpecPtr->clientData;
    TkTextLine *startLinePtr = textPtr->startMarker->sectionPtr->linePtr;
    bool isCharSeg;

    index = 0;
    segPtr = (startLinePtr == linePtr) ? textPtr->startMarker : linePtr->segPtr;

    /*
     * TODO: Use new elide structure, but this requires a redesign of the whole
     * search algorithm.
     */

    for (leftToScan = byteIndex; leftToScan > 0; segPtr = segPtr->nextPtr) {
	if ((isCharSeg = segPtr->typePtr == &tkTextCharType)
		|| (searchSpecPtr->searchHyphens && segPtr->typePtr == &tkTextHyphenType)) {
	    if (searchSpecPtr->searchElide || !TkTextSegmentIsElided(textPtr, segPtr)) {
		if (leftToScan < segPtr->size) {
		    if (searchSpecPtr->exact) {
			index += leftToScan;
		    } else {
			index += isCharSeg ? Tcl_NumUtfChars(segPtr->body.chars, leftToScan) : 1;
		    }
		} else if (searchSpecPtr->exact) {
		    index += isCharSeg ? segPtr->size : 2;
		} else {
		    index += isCharSeg ? CountCharsInSeg(segPtr) : 1;
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
 *	A pointer to the TkTextLine corresponding to the given line, or NULL
 *	if there was no available line.
 *
 *	Also 'lenPtr' (if non-NULL) is filled in with the total length of
 *	'theLine' (not just what we added to it, but the length including what
 *	was already in there). This is in bytes for an exact search and in
 *	chars for a regexp search.
 *
 *	Also 'extraLinesPtr' (if non-NULL) will have its value incremented by
 *	1 for each additional logical line we have added because a newline is
 *	elided (this will only ever happen if we have chosen not to search
 *	elided text, of course).
 *
 * Side effects:
 *	Memory may be allocated or re-allocated for theLine's string
 *	representation.
 *
 *----------------------------------------------------------------------
 */

static ClientData
TextSearchAddNextLine(
    int lineNum,		/* Line we must add. */
    SearchSpec *searchSpecPtr,	/* Search parameters. */
    Tcl_Obj *theLine,		/* Object to append to. */
    int *lenPtr,		/* For returning the total length. */
    int *extraLinesPtr)		/* If non-NULL, will have its value
				 * incremented by the number of additional
				 * logical lines which are merged into this
				 * one by newlines being elided. */
{
    TkTextLine *linePtr, *thisLinePtr;
    TkTextSegment *segPtr, *lastPtr;
    TkText *textPtr = searchSpecPtr->clientData;
    TkTextLine *startLinePtr = textPtr->startMarker->sectionPtr->linePtr;
    TkTextLine *endLinePtr = textPtr->endMarker->sectionPtr->linePtr;
    bool nothingYet = true;

    /*
     * Extract the text from the line.
     */

    if (!(linePtr = TkBTreeFindLine(textPtr->sharedTextPtr->tree, textPtr, lineNum))) {
	return NULL;
    }
    thisLinePtr = linePtr;

    while (thisLinePtr) {
	bool elideWraps = false;

	segPtr = (startLinePtr == thisLinePtr) ? textPtr->startMarker : thisLinePtr->segPtr;
	lastPtr = (endLinePtr == thisLinePtr) ? textPtr->endMarker : NULL;

	/*
	 * TODO: Use new elide structure, but this requires a redesign of the whole
	 * search algorithm.
	 */

	for ( ; segPtr != lastPtr; segPtr = segPtr->nextPtr) {
	    if (segPtr->typePtr == &tkTextCharType
		    || (searchSpecPtr->searchHyphens && segPtr->typePtr == &tkTextHyphenType)) {
		if (!searchSpecPtr->searchElide && TkTextSegmentIsElided(textPtr, segPtr)) {
		    /*
		     * If we reach the end of the logical line, and if we have at
		     * least one character in the string, then we continue
		     * wrapping to the next logical line. If there are no
		     * characters yet, then the entire line of characters is
		     * elided and there's no need to complicate matters by
		     * wrapping - we'll look at the next line in due course.
		     */

		    if (!segPtr->nextPtr && !nothingYet) {
			elideWraps = true;
		    }
		} else if (segPtr->typePtr == &tkTextCharType) {
		    Tcl_AppendToObj(theLine, segPtr->body.chars, segPtr->size);
		    nothingYet = false;
		} else {
		    Tcl_AppendToObj(theLine, "\xc2\xad", 2); /* U+002D */
		    nothingYet = false;
		}
	    }
	}
	if (!elideWraps) {
	    break;
	}
	lineNum += 1;
	if (lineNum >= searchSpecPtr->numLines) {
	    break;
	}
	thisLinePtr = TkBTreeNextLine(textPtr, thisLinePtr);
	if (thisLinePtr && extraLinesPtr) {
	    /*
	     * Tell our caller we have an extra line merged in.
	     */

	    *extraLinesPtr = *extraLinesPtr + 1;
	}
    }

    /*
     * If we're ignoring case, convert the line to lower case. There is no
     * need to do this for regexp searches, since they handle a flag for this
     * purpose.
     */

    if (searchSpecPtr->exact && searchSpecPtr->noCase) {
	Tcl_SetObjLength(theLine, Tcl_UtfToLower(Tcl_GetString(theLine)));
    }

    if (lenPtr) {
	*lenPtr = searchSpecPtr->exact ? GetByteLength(theLine) : Tcl_GetCharLength(theLine);
    }
    return linePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TextSearchFoundMatch --
 *
 *	Stores information from a successful search.
 *
 * Results:
 *	'true' if the information was stored, 'false' if the position at
 *	which the match was found actually falls outside the allowable
 *	search region (and therefore the search is actually complete).
 *
 * Side effects:
 *	Memory may be allocated in the 'countPtr' and 'resPtr' fields of
 *	'searchSpecPtr'. Each of those objects will have refCount zero and
 *	must eventually be freed or stored elsewhere as appropriate.
 *
 *----------------------------------------------------------------------
 */

static bool
TextSearchFoundMatch(
    int lineNum,		/* Line on which match was found. */
    SearchSpec *searchSpecPtr,	/* Search parameters. */
    ClientData clientData,	/* Token returned by the 'addNextLineProc', TextSearchAddNextLine.
    				 * May be NULL, in which we case we must generate it (from lineNum). */
    Tcl_Obj *theLine,		/* Text from current line, only accessed for exact searches, and
    				 * is allowed to be NULL for regexp searches. */
    int matchOffset,		/* Offset of found item in utf-8 bytes for exact search, Unicode
    				 * chars for regexp. */
    int matchLength)		/* Length also in bytes/chars as per search type. */
{
    int numChars;
    int leftToScan;
    TkTextIndex foundIndex;
    TkTextSegment *segPtr;
    TkTextLine *linePtr, *startLinePtr;
    TkText *textPtr = searchSpecPtr->clientData;
    int byteIndex;

    if (lineNum == searchSpecPtr->stopLine) {
	/*
	 * If the current index is on the wrong side of the stopIndex, then
	 * the item we just found is actually outside the acceptable range,
	 * and the search is over.
	 */

	if (searchSpecPtr->backwards ^ (matchOffset >= searchSpecPtr->stopOffset)) {
	    return false;
	}
    }

    /*
     * Calculate the character count, which may need augmenting if there are
     * embedded windows or elidden text.
     */

    if (searchSpecPtr->exact) {
	numChars = Tcl_NumUtfChars(Tcl_GetString(theLine) + matchOffset, matchLength);
    } else {
	numChars = matchLength;
    }

    /*
     * If we're using strict limits checking, ensure that the match with its
     * full length fits inside the given range.
     */

    if (searchSpecPtr->strictLimits && lineNum == searchSpecPtr->stopLine) {
	if (searchSpecPtr->backwards ^ (matchOffset + numChars > searchSpecPtr->stopOffset)) {
	    return false;
	}
    }

    /*
     * The index information returned by the regular expression parser only
     * considers textual information: it doesn't account for embedded windows,
     * elided text (when we are not searching elided text) or any other
     * non-textual info. Scan through the line's segments again to adjust both
     * matchChar and matchCount.
     *
     * We will walk through the segments of this line until we have either
     * reached the end of the match or we have reached the end of the line.
     */

    linePtr = clientData;
    if (!linePtr) {
	linePtr = TkBTreeFindLine(textPtr->sharedTextPtr->tree, textPtr, lineNum);
    }
    startLinePtr = textPtr->startMarker->sectionPtr->linePtr;

    /*
     * Find the starting point.
     */

    leftToScan = matchOffset;
    while (true) {
	/*
	 * Note that we allow leftToScan to be zero because we want to skip
	 * over any preceding non-textual items.
	 */

	segPtr = (linePtr == startLinePtr) ? textPtr->startMarker : linePtr->segPtr;
	byteIndex = TkTextSegToIndex(segPtr);

	/*
	 * TODO: Use new elide structure, but this requires a redesign of the whole
	 * search algorithm.
	 */

	for ( ; leftToScan >= 0 && segPtr; segPtr = segPtr->nextPtr) {
	    if (segPtr->typePtr == &tkTextCharType) {
		int size = searchSpecPtr->exact ? segPtr->size : (int) CountCharsInSeg(segPtr);

		if (!searchSpecPtr->searchElide && TkTextSegmentIsElided(textPtr, segPtr)) {
		    matchOffset += size;
		} else {
		    leftToScan -= size;
		}
	    } else if (searchSpecPtr->searchHyphens && segPtr->typePtr == &tkTextHyphenType) {
		int size = searchSpecPtr->exact ? 2 : 1;

		if (!searchSpecPtr->searchElide && TkTextSegmentIsElided(textPtr, segPtr)) {
		    matchOffset += size;
		} else {
		    leftToScan -= size;
		}
	    } else {
		assert(segPtr->size <= 1);
		matchOffset += segPtr->size;
	    }
	    byteIndex += segPtr->size;
	}

	assert(!segPtr || leftToScan < 0 || TkBTreeNextLine(textPtr, linePtr));

	if (segPtr || leftToScan < 0) {
	    break;
	}

	/*
	 * This will only happen if we are eliding newlines.
	 *
	 * We've wrapped to the beginning of the next logical line, which
	 * has been merged with the previous one whose newline was elided.
	 */

	linePtr = linePtr->nextPtr;
	lineNum += 1;
	matchOffset = 0;
    }

    /*
     * Calculate and store the found index in the result.
     */

    if (searchSpecPtr->exact) {
	TkTextMakeByteIndex(textPtr->sharedTextPtr->tree, textPtr, lineNum, matchOffset, &foundIndex);
    } else {
	TkTextMakeCharIndex(textPtr->sharedTextPtr->tree, textPtr, lineNum, matchOffset, &foundIndex);
    }

    if (searchSpecPtr->all) {
	if (!searchSpecPtr->resPtr) {
	    searchSpecPtr->resPtr = Tcl_NewObj();
	}
	Tcl_ListObjAppendElement(NULL, searchSpecPtr->resPtr, TkTextNewIndexObj(&foundIndex));
    } else {
	searchSpecPtr->resPtr = TkTextNewIndexObj(&foundIndex);
    }

    /*
     * Find the end point. Here 'leftToScan' could be negative already as a
     * result of the above loop if the segment we reached spanned the start of
     * the string. When we add matchLength it will become non-negative.
     */

    /*
     * TODO: Use new elide structure, but this requires a redesign of the whole
     * search algorithm.
     */

    for (leftToScan += matchLength; leftToScan > 0; segPtr = segPtr->nextPtr) {
	bool isCharSeg;

	if (!segPtr) {
	    /*
	     * We are on the next line - this of course should only ever
	     * happen with searches which have matched across multiple lines.
	     */

	    assert(TkBTreeNextLine(textPtr, linePtr));
	    linePtr = linePtr->nextPtr;
	    segPtr = linePtr->segPtr;
	    byteIndex = 0;
	}

	isCharSeg = (segPtr->typePtr == &tkTextCharType);

	if (!isCharSeg && (!searchSpecPtr->searchHyphens || segPtr->typePtr != &tkTextHyphenType)) {
	    /*
	     * Anything we didn't count in the search needs adding.
	     */

	    assert(segPtr->size <= 1);
	    numChars += segPtr->size;
	} else if (!searchSpecPtr->searchElide && TkTextSegmentIsElided(textPtr, segPtr)) {
	    numChars += isCharSeg ? CountCharsInSeg(segPtr) : 1;
	} else if (searchSpecPtr->exact) {
	    leftToScan -= isCharSeg ? segPtr->size : 2;
	} else {
	    leftToScan -= isCharSeg ? CountCharsInSeg(segPtr) : 1;
	}
    }

    /*
     * Now store the count result, if it is wanted.
     */

    if (searchSpecPtr->varPtr) {
	Tcl_Obj *tmpPtr = Tcl_NewIntObj(numChars);
	if (searchSpecPtr->all) {
	    if (!searchSpecPtr->countPtr) {
		searchSpecPtr->countPtr = Tcl_NewObj();
	    }
	    Tcl_ListObjAppendElement(NULL, searchSpecPtr->countPtr, tmpPtr);
	} else {
	    searchSpecPtr->countPtr = tmpPtr;
	}
    }

    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextGetTabs --
 *
 *	Parses a string description of a set of tab stops.
 *
 * Results:
 *	The return value is a pointer to a malloc'ed structure holding parsed
 *	information about the tab stops. If an error occurred then the return
 *	value is NULL and an error message is left in the interp's result.
 *
 * Side effects:
 *	Memory is allocated for the structure that is returned. It is up to
 *	the caller to free this structure when it is no longer needed.
 *
 *----------------------------------------------------------------------
 */

TkTextTabArray *
TkTextGetTabs(
    Tcl_Interp *interp,		/* Used for error reporting. */
    TkText *textPtr,		/* Information about the text widget. */
    Tcl_Obj *stringPtr)		/* Description of the tab stops. See the text
				 * manual entry for details. */
{
    int objc, i, count;
    Tcl_Obj **objv;
    TkTextTabArray *tabArrayPtr;
    TkTextTab *tabPtr;
    int ch;
    double prevStop, lastStop;
    /*
     * Map these strings to TkTextTabAlign values.
     */
    static const char *const tabOptionStrings[] = {
	"left", "right", "center", "numeric", NULL
    };

    if (Tcl_ListObjGetElements(interp, stringPtr, &objc, &objv) != TCL_OK) {
	return NULL;
    }

    /*
     * First find out how many entries we need to allocate in the tab array.
     */

    count = 0;
    for (i = 0; i < objc; i++) {
	char c = Tcl_GetString(objv[i])[0];

	if (c != 'l' && c != 'r' && c != 'c' && c != 'n') {
	    count += 1;
	}
    }

    /*
     * Parse the elements of the list one at a time to fill in the array.
     */

    tabArrayPtr = malloc(sizeof(TkTextTabArray) + (count - 1)*sizeof(TkTextTab));
    tabArrayPtr->numTabs = 0;
    prevStop = 0.0;
    lastStop = 0.0;
    for (i = 0, tabPtr = &tabArrayPtr->tabs[0]; i < objc; i++, tabPtr++) {
	int index;

	/*
	 * This will round fractional pixels above 0.5 upwards, and otherwise
	 * downwards, to find the right integer pixel position.
	 */

	if (Tk_GetPixelsFromObj(interp, textPtr->tkwin, objv[i], &tabPtr->location) != TCL_OK) {
	    goto error;
	}

	if (tabPtr->location <= 0) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "tab stop \"%s\" is not at a positive distance", Tcl_GetString(objv[i])));
	    Tcl_SetErrorCode(interp, "TK", "VALUE", "TAB_STOP", NULL);
	    goto error;
	}

	prevStop = lastStop;
	if (Tk_GetDoublePixelsFromObj(interp, textPtr->tkwin, objv[i], &lastStop) != TCL_OK) {
	    goto error;
	}

	if (i > 0 && tabPtr->location <= (tabPtr - 1)->location) {
	    /*
	     * This tab is actually to the left of the previous one, which is
	     * illegal.
	     */

#ifdef _TK_ALLOW_DECREASING_TABS
	    /*
	     * Force the tab to be a typical character width to the right of
	     * the previous one, and update the 'lastStop' with the changed
	     * position.
	     */

	    tabPtr->location = (tabPtr - 1)->location;
	    tabPtr->location += (textPtr->charWidth > 0 ? textPtr->charWidth : 8);
	    lastStop = tabPtr->location;
#else
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "tabs must be monotonically increasing, but \"%s\" is "
		    "smaller than or equal to the previous tab",
		    Tcl_GetString(objv[i])));
	    Tcl_SetErrorCode(interp, "TK", "VALUE", "TAB_STOP", NULL);
	    goto error;
#endif /* _TK_ALLOW_DECREASING_TABS */
	}

	tabArrayPtr->numTabs += 1;

	/*
	 * See if there is an explicit alignment in the next list element.
	 * Otherwise just use "left".
	 */

	tabPtr->alignment = LEFT;
	if (i + 1 == objc) {
	    continue;
	}

	/*
	 * There may be a more efficient way of getting this.
	 */

	TkUtfToUniChar(Tcl_GetString(objv[i + 1]), &ch);
	if (!Tcl_UniCharIsAlpha(ch)) {
	    continue;
	}
	i += 1;

	if (Tcl_GetIndexFromObjStruct(interp, objv[i], tabOptionStrings,
		sizeof(char *), "tab alignment", 0, &index) != TCL_OK) {
	    goto error;
	}
	tabPtr->alignment = (TkTextTabAlign) index;
    }

    /*
     * For when we need to interpolate tab stops, store these two so we know
     * the tab stop size to very high precision. With the above checks, we can
     * guarantee that tabIncrement is strictly positive here.
     */

    tabArrayPtr->lastTab = lastStop;
    tabArrayPtr->tabIncrement = lastStop - prevStop;

    return tabArrayPtr;

  error:
    free(tabArrayPtr);
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TextDumpCmd --
 *
 *	Return information about the text, tags, marks, and embedded windows
 *	and images in a text widget. See the man page for the description of
 *	the text dump operation for all the details.
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

static void
AppendOption(
    char *result,
    const char *str,
    const char *delim)
{
    unsigned len = strlen(result);

    if (delim && len > 0 && result[len - 1] != ' ' && result[len - 1] != '?') {
	strcpy(result + len, delim);
	len += strlen(delim);
    }
    strcpy(result + len, str);
}

static int
GetDumpFlags(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[],	/* Argument objects. */
    unsigned allowed,		/* Which options are allowed? */
    unsigned dflt,		/* Default options (-all) */
    unsigned *what,		/* Store flags here. */
    int *lastArg,		/* Store index of last used argument, can be NULL. */
    TkTextIndex *index1,	/* Store first index here. */
    TkTextIndex *index2,	/* Store second index here. */
    Tcl_Obj **command)		/* Store command here, can be NULL. */
{
    static const char *const optStrings[] = {
	"-all", "-bindings", "-chars", "-command", "-configurations", "-discardselection",
	"-displaychars", "-displaytext", "-dontresolve", "-elide", "-image",
	"-insertmark", "-mark", "-nested", "-node", "-setup",
	"-tag", "-text", "-window", NULL
    };
    enum opts {
	DUMP_ALL, DUMP_TAG_BINDINGS, DUMP_CHARS, DUMP_CMD, DUMP_TAG_CONFIGS, DUMP_DISCARD_SEL,
	DUMP_DISPLAY_CHARS, DUMP_DISPLAY_TEXT, DUMP_DONT_RESOLVE, DUMP_ELIDE, DUMP_IMG,
	DUMP_INSERT_MARK, DUMP_MARK, DUMP_NESTED, DUMP_NODE, DUMP_TEXT_CONFIGS,
	DUMP_TAG, DUMP_TEXT, DUMP_WIN
    };
    static const unsigned dumpFlags[] = {
	0, TK_DUMP_TAG_BINDINGS, TK_DUMP_CHARS, 0, TK_DUMP_TAG_CONFIGS, TK_DUMP_DISCARD_SEL,
	TK_DUMP_DISPLAY_CHARS, TK_DUMP_DISPLAY_TEXT, TK_DUMP_DONT_RESOLVE, TK_DUMP_ELIDE, TK_DUMP_IMG,
	TK_DUMP_INSERT_MARK, TK_DUMP_MARK, TK_DUMP_NESTED, TK_DUMP_NODE, TK_DUMP_TEXT_CONFIGS,
	TK_DUMP_TAG, TK_DUMP_TEXT, TK_DUMP_WIN
    };

    int arg;
    unsigned i;
    unsigned flags = 0;
    const char *myOptStrings[sizeof(optStrings)/sizeof(optStrings[0])];
    int myOptIndices[sizeof(optStrings)/sizeof(optStrings[0])];
    int myOptCount;

    assert(what);
    assert(!index1 == !index2);
    assert(DUMP_ALL == 0); /* otherwise next loop is wrong */

    /* We know that option -all is allowed in any case. */
    myOptStrings[0] = optStrings[DUMP_ALL];
    myOptIndices[0] = DUMP_ALL;
    myOptCount = 1;

    for (i = 1; i < sizeof(optStrings)/sizeof(optStrings[0]) - 1; ++i) {
	if (i == DUMP_CMD ? !!command : (allowed & dumpFlags[i]) == dumpFlags[i]) {
	    myOptStrings[myOptCount] = optStrings[i];
	    myOptIndices[myOptCount] = i;
	    myOptCount += 1;
	}
    }
    myOptStrings[myOptCount] = NULL;

    if (lastArg) {
	*lastArg = 0;
    }
    *what = 0;

    for (arg = 2; arg < objc && Tcl_GetString(objv[arg])[0] == '-'; ++arg) {
	int index;

	if (Tcl_GetString(objv[arg])[1] == '-'
		&& Tcl_GetString(objv[arg])[2] == '\0'
		&& (arg < objc - 1 || Tcl_GetString(objv[arg + 1])[0] != '-')) {
	    continue;
	}

	if (Tcl_GetIndexFromObjStruct(interp, objv[arg], myOptStrings,
		sizeof(char *), "option", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}

	switch ((enum opts) myOptIndices[index]) {
#define CASE(Flag) case DUMP_##Flag: *what |= TK_DUMP_##Flag; flags |= TK_DUMP_##Flag; break;
	CASE(CHARS);
	CASE(TEXT);
	CASE(DISPLAY_CHARS);
	CASE(DISPLAY_TEXT);
	CASE(TAG);
	CASE(MARK);
	CASE(ELIDE);
	CASE(NESTED);
	CASE(NODE);
	CASE(DISCARD_SEL);
	CASE(INSERT_MARK);
	CASE(TEXT_CONFIGS);
	CASE(TAG_BINDINGS);
	CASE(TAG_CONFIGS);
	CASE(DONT_RESOLVE);
	CASE(IMG);
	CASE(WIN);
#undef CASE
	case DUMP_ALL:
	    *what = dflt;
	    break;
	case DUMP_CMD:
	    arg += 1;
	    if (!command || arg >= objc) {
		goto wrongArgs;
	    }
	    *command = objv[arg];
	    break;
	}
	if (~allowed & flags) {
	    goto wrongArgs;
	}
    }
    if (!(*what & dflt)) {
	*what |= dflt;
    }
    if (!index1) {
	if (arg < objc) {
	    goto wrongArgs;
	}
	return TCL_OK;
    }
    if (arg >= objc || arg + 2 < objc) {
	goto wrongArgs;
    }
    if (!TkTextGetIndexFromObj(interp, textPtr, objv[arg], index1)) {
	return TCL_ERROR;
    }
    arg += 1;
    if (lastArg) {
	*lastArg = arg;
    }
    if (objc == arg) {
	TkTextIndexForwChars(textPtr, index1, 1, index2, COUNT_INDICES);
    } else if (!TkTextGetIndexFromObj(interp, textPtr, objv[arg], index2)) {
	return TCL_ERROR;
    }
    return TCL_OK;

wrongArgs:
    {
	char result[500];
	unsigned i;

	result[0] = 0;
	AppendOption(result, "?", NULL);

	for (i = 0; myOptStrings[i]; ++i) {
	    if (myOptIndices[i] != DUMP_CMD) {
		AppendOption(result, myOptStrings[i], " ");
	    }
	}
	AppendOption(result, "? ?", NULL);
	if (command) { AppendOption(result, "-command script", NULL); }
	AppendOption(result, "?", NULL);
	if (index1)  { AppendOption(result, " index ?index2?", NULL); }

	Tcl_SetObjResult(interp, Tcl_ObjPrintf("Usage: %s %s %s",
		Tcl_GetString(objv[0]), Tcl_GetString(objv[1]), result));
	Tcl_SetErrorCode(interp, "TCL", "WRONGARGS", NULL);
    }
    return TCL_ERROR;
}

static int
TextDumpCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. Someone else has already parsed this command
    				 * enough to know that objv[1] is "dump". */
{
    TkTextIndex index1, index2;
    TkTextBTree tree;
    TkTextTag *tagPtr, *tPtr;
    int lineno;			/* Current line number. */
    unsigned what;		/* bitfield to select segment types. */
    int lastArg;		/* Index of last argument. */
    TkTextLine *linePtr;
    TkTextIndex prevByteIndex;
    Tcl_Obj *command = NULL;	/* Script callback to apply to segments. */
    TkTextTag *prevTagPtr = NULL;
    int result;

    assert(textPtr);

    result = GetDumpFlags(textPtr, interp, objc, objv, TK_DUMP_DUMP_ALL|TK_DUMP_NODE, TK_DUMP_DUMP_ALL,
	    &what, &lastArg, &index1, &index2, &command);
    if (result != TCL_OK) {
	return result;
    }
    if (TkTextIndexCompare(&index1, &index2) >= 0) {
	return TCL_OK;
    }
    tree = textPtr->sharedTextPtr->tree;
    textPtr->sharedTextPtr->inspectEpoch += 1;
    lineno = TkBTreeLinesTo(tree, textPtr, TkTextIndexGetLine(&index1), NULL);
    prevByteIndex = index1;
    if (TkTextIndexBackBytes(textPtr, &index1, 1, &prevByteIndex) == 0) {
	unsigned epoch = textPtr->sharedTextPtr->inspectEpoch + 1;
	tagPtr = TkBTreeGetTags(&prevByteIndex);
	for (tPtr = tagPtr; tPtr; tPtr = tPtr->nextPtr) { tPtr->epoch = epoch; }
    } else {
	tagPtr = NULL;
    }
    if (TkTextIndexGetLine(&index1) == TkTextIndexGetLine(&index2)) {
	/* we are at the end, so we can ignore the return code of DumpLine */
	DumpLine(interp, textPtr, what, TkTextIndexGetLine(&index1),
		TkTextIndexGetByteIndex(&index1), TkTextIndexGetByteIndex(&index2),
		lineno, command, &prevTagPtr);
    } else {
	int lineend = TkBTreeLinesTo(tree, textPtr, TkTextIndexGetLine(&index2), NULL);
	int endByteIndex = TkTextIndexGetByteIndex(&index2);

	if (!DumpLine(interp, textPtr, what, TkTextIndexGetLine(&index1),
		TkTextIndexGetByteIndex(&index1), INT_MAX, lineno, command, &prevTagPtr)) {
	    if (textPtr->flags & DESTROYED) {
		return TCL_OK;
	    }
	    if (!(linePtr = TkBTreeFindLine(textPtr->sharedTextPtr->tree, textPtr, lineno))) {
		goto textChanged;
	    }
	} else {
	    linePtr = TkTextIndexGetLine(&index1);
	}
	while ((linePtr = TkBTreeNextLine(textPtr, linePtr))) {
	    if (++lineno == lineend) {
		break;
	    }
	    if (!DumpLine(interp, textPtr, what, linePtr, 0, INT_MAX, lineno, command, &prevTagPtr)) {
		if (textPtr->flags & DESTROYED) {
		    return TCL_OK;
		}
		if (!(linePtr = TkBTreeFindLine(textPtr->sharedTextPtr->tree, textPtr, lineno))) {
		    goto textChanged;
		}
	    }
	}
	if (linePtr) {
	    /* we are at the end, so we can ignore the return code of DumpLine */
	    DumpLine(interp, textPtr, what, linePtr, 0, endByteIndex, lineno, command, &prevTagPtr);
	}
    }

  textChanged:

    /*
     * Special case to get the leftovers hiding at the end mark.
     */

    if (!(textPtr->flags & DESTROYED)) {
	if (lastArg < objc
		&& strncmp(Tcl_GetString(objv[lastArg]), "end", GetByteLength(objv[lastArg])) == 0) {
	    /*
	     * Re-get the end index, in case it has changed.
	     */

	    if (!TkTextGetIndexFromObj(interp, textPtr, objv[lastArg], &index2)) {
		return TCL_ERROR;
	    }
	    if (!DumpLine(interp, textPtr, what & ~TK_DUMP_TEXT, TkTextIndexGetLine(&index2), 0, 1,
		    lineno, command, &prevTagPtr)) {
		prevTagPtr = NULL; /* the tags are no longer valid */
	    }
	}

	if (prevTagPtr && TkTextIndexIsEndOfText(&index2)) {
	    /*
	     * Finally print "tagoff" information, if at end of text.
	     */

	    for ( ; prevTagPtr; prevTagPtr = prevTagPtr->succPtr) {
		if (!DumpSegment(textPtr, interp, "tagoff", prevTagPtr->name, command, &index2, what)) {
		    break;
		}
	    }
	}
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DumpLine
 *
 *	Return information about a given text line from character position
 *	"start" up to, but not including, "end".
 *
 * Results:
 *	Returns false if the command callback made any changes to the text widget
 *	which will have invalidated internal structures such as TkTextSegment,
 *	TkTextIndex, pointers. Our caller can then take action to recompute
 *	such entities, or he aborts with an error. Returns true otherwise.
 *
 * Side effects:
 *	None, but see DumpSegment which can have arbitrary side-effects
 *
 *----------------------------------------------------------------------
 */

static bool
DumpLine(
    Tcl_Interp *interp,
    TkText *textPtr,
    int what,			/* Bit flags to select segment types. */
    TkTextLine *linePtr,	/* The current line. */
    int startByte, int endByte,	/* Byte range to dump. */
    int lineno,			/* Line number for indices dump. */
    Tcl_Obj *command,		/* Script to apply to the segment. */
    TkTextTag **prevTagPtr)	/* Tag information from previous segment. */
{
    TkSharedText *sharedTextPtr;
    TkTextSegment *sPtr;
    TkTextSegment *segPtr;
    TkTextSegment *endPtr;
    TkTextSegment *newSegPtr;
    TkTextIndex index;
    int offset = 0;
    int currentSize = 0;
    int bufSize = 0;
    bool textChanged = false;
    char *buffer = NULL;
    bool eol;

    sharedTextPtr = textPtr->sharedTextPtr;

    if (!*prevTagPtr && (startByte > 0 || linePtr != TkBTreeGetStartLine(textPtr))) {
	/*
	 * If this is the first line to dump, and we are not at start of line,
	 * then we need the preceding tag information.
	 */

	TkTextIndex index;
	TkTextTag *tagPtr, *tPtr;
	unsigned epoch = sharedTextPtr->inspectEpoch;

	TkTextIndexClear(&index, textPtr);
	TkTextIndexSetByteIndex2(&index, linePtr, startByte);
	TkBTreeMoveBackward(&index, 1);
	segPtr = TkTextIndexGetContentSegment(&index, NULL);
	assert(segPtr);
	tagPtr = TkBTreeGetSegmentTags(textPtr->sharedTextPtr, segPtr, textPtr, NULL);
	for (tPtr = tagPtr; tPtr; tPtr = tPtr->nextPtr) {
	    tPtr->flag = epoch; /* mark as open */
	}
    }

    /*
     * Must loop through line looking at its segments: character, hyphen, mark, image, window.
     */

    segPtr = linePtr->segPtr;
    endPtr = textPtr->endMarker;
    eol = !segPtr->nextPtr;

    if ((what & TK_DUMP_NODE)
	    && startByte == 0
	    && (!linePtr->prevPtr || linePtr->prevPtr->parentPtr != linePtr->parentPtr)) {
	char buf[20];
	unsigned depth, number;

	TkTextIndexClear(&index, textPtr);
	TkTextIndexSetToStartOfLine2(&index, linePtr);
	number = TkBTreeChildNumber(sharedTextPtr->tree, linePtr, &depth);
	snprintf(buf, sizeof(buf), "%d:%d", number, depth);

	if (!DumpSegment(textPtr, interp, "node", buf, command, &index, what)) {
	    goto textChanged;
	}
    }

    while (segPtr && offset < endByte) {
	currentSize = segPtr->size;

	if (offset + MAX(1, currentSize) > startByte) {
	    if ((what & TK_DUMP_TAG) && segPtr->tagInfoPtr) {
		TkTextTag *tagPtr = TkBTreeGetSegmentTags(sharedTextPtr, segPtr, textPtr, NULL);
		unsigned epoch = sharedTextPtr->inspectEpoch;
		unsigned nextEpoch = epoch + 1;
		TkTextTag *tPtr;

		for (tPtr = tagPtr; tPtr; tPtr = tPtr->nextPtr) {
		    if (tPtr->flag == epoch) {
			tPtr->flag = nextEpoch; /* mark as still open */
		    }
		}

		if (*prevTagPtr) {
		    /*
		     * Print "tagoff" information.
		     */

		    for (tPtr = *prevTagPtr; tPtr; tPtr = tPtr->succPtr) {
			if (tPtr->flag == epoch) { /* should be closed? */
			    TkTextMakeByteIndex(sharedTextPtr->tree, textPtr, lineno, offset, &index);
			    if (!DumpSegment(textPtr, interp, "tagoff",
				    tPtr->name, command, &index, what)) {
				goto textChanged;
			    }
			    tPtr->flag = 0; /* mark as closed */
			}
		    }
		}

		/*
		 * Print "tagon" information.
		 */

		sharedTextPtr->inspectEpoch = ++epoch;

		for (tPtr = tagPtr; tPtr; tPtr = tPtr->nextPtr) {
		    if (tPtr->flag != epoch) {
			TkTextMakeByteIndex(sharedTextPtr->tree, textPtr, lineno, offset, &index);
			if (!DumpSegment(textPtr, interp, "tagon", tPtr->name, command, &index, what)) {
			    goto textChanged;
			}
			tPtr->flag = epoch; /* mark as open */
		    }
		    tPtr->succPtr = tPtr->nextPtr;
		}

		*prevTagPtr = tagPtr;
	    }

	    if (what & segPtr->typePtr->group) {
		assert(segPtr->typePtr->group != SEG_GROUP_BRANCH);

		if (segPtr->typePtr->group == SEG_GROUP_CHAR) {
		    int last = currentSize;	/* Index of last char in seg. */
		    int first = 0;		/* Index of first char in seg. */

		    if (offset + currentSize > endByte) {
			last = endByte - offset;
		    }
		    if (startByte > offset) {
			first = startByte - offset;
		    }
		    if (last != currentSize) {
			/*
			 * To avoid modifying the string in place we copy over just
			 * the segment that we want. Since DumpSegment can modify the
			 * text, we could not confidently revert the modification here.
			 */

			int length = last - first;

			if (length >= bufSize) {
			    bufSize = MAX(length + 1, 2*length);
			    buffer = realloc(buffer, bufSize);
			}

			memcpy(buffer, segPtr->body.chars + first, length);
			buffer[length] = '\0';

			TkTextMakeByteIndex(sharedTextPtr->tree, textPtr, lineno,
				offset + first, &index);
			if (!DumpSegment(textPtr, interp, "text", buffer, command, &index, what)) {
			    goto textChanged;
			}
		    } else {
			TkTextMakeByteIndex(sharedTextPtr->tree, textPtr, lineno,
				offset + first, &index);
			if (!DumpSegment(textPtr, interp, "text",
				segPtr->body.chars + first, command, &index, what)) {
			    goto textChanged;
			}
		    }
		} else if (segPtr == endPtr) {
		    if (linePtr == TkBTreeGetLastLine(textPtr)) {
			break; /* finished */
		    }
		    /* print final newline in next iteration */
		    currentSize = linePtr->size - offset - 1;
		    startByte = offset + currentSize + linePtr->lastPtr->size - 1;
		    segPtr = linePtr->lastPtr->prevPtr;
		} else {
		    char const *value = NULL;

		    switch ((int) segPtr->typePtr->group) {
		    case SEG_GROUP_MARK:
			value = TkTextMarkName(sharedTextPtr, textPtr, segPtr);
			break;
		    case SEG_GROUP_IMAGE: {
			TkTextEmbImage *eiPtr = &segPtr->body.ei;
			value = eiPtr->name ? eiPtr->name : "";
			break;
		    }
		    case SEG_GROUP_WINDOW: {
			TkTextEmbWindow *ewPtr = &segPtr->body.ew;
			value = ewPtr->tkwin ? Tk_PathName(ewPtr->tkwin) : "";
			break;
		    }
		    case SEG_GROUP_HYPHEN:
			value = "";
			break;
		    }
		    if (value) {
			TkTextMakeByteIndex(sharedTextPtr->tree, textPtr, lineno, offset, &index);
			if (!DumpSegment(textPtr, interp, segPtr->typePtr->name, value, command,
				&index, what)) {
			    goto textChanged;
			}
		    }
		}
	    }
	}

	offset += currentSize;
	segPtr = segPtr->nextPtr;
	continue;

  textChanged:

	/*
	 * Our indices, segments, and tag chains are no longer valid. It's a bad
	 * idea to do changes while the dump is running, it's impossible to
	 * synchronize in any case, but we will try the best.
	 */

	*prevTagPtr = NULL;
	textChanged = true;

	if (eol || (textPtr->flags & DESTROYED)) {
	    break;
	}

	offset += currentSize;
	if (!(linePtr = TkBTreeFindLine(textPtr->sharedTextPtr->tree, textPtr, lineno))) {
	    break;
	}
	TkTextIndexClear(&index, textPtr);
	TkTextIndexSetByteIndex2(&index, linePtr, MIN(offset, linePtr->size - 1));

	sPtr = newSegPtr = TkTextIndexGetFirstSegment(&index, NULL);
	while (sPtr && sPtr != segPtr) {
	    sPtr = sPtr->nextPtr;
	}
	if (sPtr != segPtr) {
	    segPtr = newSegPtr;
	} else if (offset >= segPtr->size) {
	    segPtr = segPtr->nextPtr;
	}
    }

    free(buffer);
    return !textChanged;
}

/*
 *----------------------------------------------------------------------
 *
 * TextChecksumCmd --
 *
 *	Return the checksum over the whole content.
 *	About the format see documentation.
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

static uint32_t
ComputeChecksum(
    uint32_t crc,
    const char *buf,
    unsigned len)
{
    static const uint32_t crcTable[256] = {
      0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
      0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
      0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
      0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
      0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
      0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
      0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
      0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
      0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
      0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
      0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
      0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
      0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
      0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
      0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
      0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
      0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
      0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
      0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
      0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
      0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
      0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
      0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
      0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
      0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
      0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
      0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
      0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
      0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
      0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
      0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
      0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
    };

    assert(buf);

    /* basic algorithm stolen from zlib/crc32.c (public domain) */

#define DO1(buf) crc = crcTable[((int)crc ^ (*buf++)) & 0xff] ^ (crc >> 8);
#define DO2(buf) DO1(buf); DO1(buf);
#define DO4(buf) DO2(buf); DO2(buf);
#define DO8(buf) DO4(buf); DO4(buf);

    crc = crc ^ 0xffffffff;

    if (len == 0) {
	while (*buf) {
	    DO1(buf);
	}
    } else {
	while (len >= 8) {
	    DO8(buf);
	    len -= 8;
	}
	while (len--) {
	    DO1(buf);
	}
    }
    return crc ^ 0xffffffff;
}

static int
TextChecksumCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. Someone else has already parsed this command
    				 * enough to know that objv[1] is "checksum". */
{
    const TkSharedText *sharedTextPtr;
    const TkTextSegment *segPtr;
    const TkTextSegment *endPtr;
    const TkTextLine *linePtr;
    TkTextTag **tagArrPtr = NULL; /* avoid compiler warning */
    unsigned what;
    unsigned crc;
    int result;

    assert(textPtr);

    result = GetDumpFlags(textPtr, interp, objc, objv, TK_DUMP_CRC_ALL, TK_DUMP_CRC_DFLT,
	    &what, NULL, NULL, NULL, NULL);

    if (result != TCL_OK) {
	return result;
    }

    sharedTextPtr = textPtr->sharedTextPtr;
    segPtr = sharedTextPtr->startMarker;
    endPtr = sharedTextPtr->endMarker;
    linePtr = segPtr->sectionPtr->linePtr;
    if (endPtr->sectionPtr->linePtr != linePtr) {
	endPtr = NULL;
    }
    crc = 0;

    if ((what & SEG_GROUP_TAG)) {
	tagArrPtr = malloc(sizeof(tagArrPtr[0])*sharedTextPtr->numTags);
    }

    /*
     * Note that 0xff cannot occur in UTF-8 strings, so we can use this value as a separator.
     */

    while (segPtr != endPtr) {
	if (segPtr->tagInfoPtr
		&& (what & SEG_GROUP_TAG)
		&& segPtr->tagInfoPtr != sharedTextPtr->emptyTagInfoPtr) {
	    unsigned i = TkTextTagSetFindFirst(segPtr->tagInfoPtr);
	    unsigned n = 0;

	    for ( ; i != TK_TEXT_TAG_SET_NPOS; i = TkTextTagSetFindNext(segPtr->tagInfoPtr, i)) {
		assert(sharedTextPtr->tagLookup[i]);
		tagArrPtr[n++] = sharedTextPtr->tagLookup[i];
	    }

	    TkTextSortTags(n, tagArrPtr);

	    for (i = 0; i < n; ++i) {
		crc = ComputeChecksum(crc, "\xff\x00", 2);
		crc = ComputeChecksum(crc, tagArrPtr[i]->name, 0);
	    }
	}
	switch ((int) segPtr->typePtr->group) {
	case SEG_GROUP_CHAR:
	    if (what & SEG_GROUP_CHAR) {
		crc = ComputeChecksum(crc, "\xff\x01", 0);
		crc = ComputeChecksum(crc, segPtr->body.chars, segPtr->size);
	    }
	    break;
	case SEG_GROUP_HYPHEN:
	    if (what & SEG_GROUP_HYPHEN) {
		crc = ComputeChecksum(crc, "\xff\x02", 0);
	    }
	    break;
	case SEG_GROUP_WINDOW:
	    if ((what & SEG_GROUP_WINDOW)) {
		crc = ComputeChecksum(crc, "\xff\x03", 0);
		crc = ComputeChecksum(crc, Tk_PathName(segPtr->body.ew.tkwin), 0);
	    }
	    break;
	case SEG_GROUP_IMAGE:
	    if ((what & SEG_GROUP_IMAGE) && segPtr->body.ei.name) {
		crc = ComputeChecksum(crc, "\xff\x04", 0);
		crc = ComputeChecksum(crc, segPtr->body.ei.name, 0);
	    }
	    break;
	case SEG_GROUP_MARK:
	    if ((what & SEG_GROUP_MARK) && TkTextIsNormalMark(segPtr)) {
		const char *name;
		const char *signature;

		name = TkTextMarkName(sharedTextPtr, NULL, segPtr);
		signature = (segPtr->typePtr == &tkTextRightMarkType) ? "\xff\x05" : "\xff\x06";
		crc = ComputeChecksum(crc, signature, 0);
		crc = ComputeChecksum(crc, name, 0);
	    }
	    break;
	case SEG_GROUP_BRANCH:
	    if (segPtr->typePtr == &tkTextBranchType && (what & TK_DUMP_DISPLAY)) {
		segPtr = segPtr->body.branch.nextPtr;
	    }
	    break;
	}
	if (!(segPtr = segPtr->nextPtr)) {
	    linePtr = linePtr->nextPtr;
	    segPtr = linePtr->segPtr;
	}
    }

    if ((what & SEG_GROUP_TAG)) {
	free(tagArrPtr);
    }
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(crc));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DumpSegment
 *
 *	Either append information about the current segment to the result, or
 *	make a script callback with that information as arguments.
 *
 * Results:
 *	Returns 'false' if the command callback made any changes to the text widget
 *	which will have invalidated internal structures such as TkTextSegment,
 *	TkTextIndex, pointers. Our caller can then take action to recompute
 *	such entities, or he aborts with an error. Returns 'true' otherwise.
 *
 * Side effects:
 *	Either evals the callback or appends elements to the result string.
 *	The callback can have arbitrary side-effects.
 *
 *----------------------------------------------------------------------
 */

static bool
DumpSegment(
    TkText *textPtr,
    Tcl_Interp *interp,
    const char *key,		/* Segment type key. */
    const char *value,		/* Segment value. */
    Tcl_Obj *command,		/* Script callback. */
    const TkTextIndex *index,	/* index with line/byte position info. */
    int what)			/* Look for TK_DUMP_INDEX bit. */
{
    char buffer[TK_POS_CHARS];
    Tcl_Obj *values[3], *tuple;

    TkTextPrintIndex(textPtr, index, buffer);
    values[0] = Tcl_NewStringObj(key, -1);
    values[1] = Tcl_NewStringObj(value, -1);
    values[2] = Tcl_NewStringObj(buffer, -1);
    tuple = Tcl_NewListObj(3, values);
    if (!command) {
	Tcl_ListObjAppendList(NULL, Tcl_GetObjResult(interp), tuple);
	Tcl_DecrRefCount(tuple);
	return true;
    } else {
	unsigned oldStateEpoch = TkBTreeEpoch(textPtr->sharedTextPtr->tree);
	Tcl_DString buf;
	int code;

	Tcl_DStringInit(&buf);
	Tcl_DStringAppend(&buf, Tcl_GetString(command), -1);
	Tcl_DStringAppend(&buf, " ", -1);
	Tcl_DStringAppend(&buf, Tcl_GetString(tuple), -1);
	code = Tcl_EvalEx(interp, Tcl_DStringValue(&buf), -1, 0);
	Tcl_DStringFree(&buf);
	if (code != TCL_OK) {
	    Tcl_AddErrorInfo(interp, "\n    (segment dumping command executed by text)");
	    Tcl_BackgroundException(interp, code);
	}
	Tcl_DecrRefCount(tuple);
	return !(textPtr->flags & DESTROYED)
		&& TkBTreeEpoch(textPtr->sharedTextPtr->tree) == oldStateEpoch;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextInspectOptions --
 *
 *	Build information from option table for "inspect".
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is allocated for the result, if needed (standard Tcl result
 *	side effects).
 *
 *----------------------------------------------------------------------
 */

static bool
ObjIsEqual(
    Tcl_Obj *obj1,
    Tcl_Obj *obj2)
{
    char const *b1, *b2;
    int i, length;

    assert(obj1);
    assert(obj2);

    b1 = Tcl_GetString(obj1);
    b2 = Tcl_GetString(obj2);

    if (strcmp(b1, "#ffffff") == 0) {
	return strcmp(b2, "#ffffff") == 0 || strcmp(b2, "white") == 0;
    }

    if (strcmp(b1, "#000000") == 0) {
	return strcmp(b2, "#000000") == 0 || strcmp(b2, "black") == 0;
    }

    length = GetByteLength(obj1);

    if (length != GetByteLength(obj2)) {
	return false;
    }

    for (i = 0; i < length; ++i) {
	if (b1[i] != b2[i]) {
	    return false;
	}
    }

    return true;
}

/*
 * NOTE: This function should be moved to tkFont.c. I will not do this,
 * because I won't touch any file not belonging to text widget implementation.
 * So the Tk Team has to do this.
 */
static Tcl_Obj *
GetFontAttrs(
    TkText *textPtr,
    int argc,
    Tcl_Obj **args)
{
    Tcl_Interp *interp = textPtr->interp;
    Tcl_Obj *objPtr = NULL;

    if (Tk_FontObjCmd(textPtr->tkwin, interp, argc, args) == TCL_OK) {
	Tcl_Obj *result = Tcl_GetObjResult(interp);
	Tcl_Obj *family = NULL;
	Tcl_Obj *size = NULL;
	Tcl_Obj *slant = NULL;
	Tcl_Obj *weight = NULL;
	Tcl_Obj *underline = NULL;
	Tcl_Obj *overstrike = NULL;
	Tcl_Obj **objv;
	int objc, i;

	if (Tcl_ListObjGetElements(interp, result, &objc, &objv) == TCL_OK) {
	    for (i = 0; i < objc - 1; ++i) {
		if (Tcl_GetString(objv[i])[0] == '-') {
		    switch (Tcl_GetString(objv[i])[1]) {
		    case 'f': /* -family     */
		    	family = objv[i + 1];
			break;
		    case 'o': /* -overstrike */
		    	overstrike = objv[i + 1];
			break;
		    case 's':
		    	switch (Tcl_GetString(objv[i])[2]) {
			case 'i': /* -size   */
			    size = objv[i + 1];
			    break;
			case 'l': /* -slant  */
			    slant = objv[i + 1];
			    break;
			}
			break;
		    case 'u': /* -underline  */
		    	underline = objv[i + 1];
			break;
		    case 'w': /* -weight     */
		    	weight = objv[i + 1];
			break;
		    }
		}
	    }
	}

	if (family && size) {
	    Tcl_DString str;
	    int boolean;

	    Tcl_DStringInit(&str);
	    Tcl_DStringAppendElement(&str, Tcl_GetString(family));
	    Tcl_DStringAppendElement(&str, Tcl_GetString(size));
	    if (weight && strcmp(Tcl_GetString(weight), "normal") != 0) {
		Tcl_DStringAppendElement(&str, Tcl_GetString(weight));
	    }
	    if (slant && strcmp(Tcl_GetString(slant), "roman") != 0) {
		Tcl_DStringAppendElement(&str, Tcl_GetString(slant));
	    }
	    if (underline && Tcl_GetBooleanFromObj(NULL, underline, &boolean) == TCL_OK && boolean) {
		Tcl_DStringAppendElement(&str, "underline");
	    }
	    if (overstrike && Tcl_GetBooleanFromObj(NULL, overstrike, &boolean) == TCL_OK && boolean) {
		Tcl_DStringAppendElement(&str, "overstrike");
	    }

	    objPtr = Tcl_NewStringObj(Tcl_DStringValue(&str), Tcl_DStringLength(&str));
	    Tcl_DStringFree(&str);
	}

	Tcl_ResetResult(interp);
    }

    return objPtr;
}

void
TkTextInspectOptions(
    TkText *textPtr,
    const void *recordPtr,
    Tk_OptionTable optionTable,
    Tcl_DString *result,	/* should be already initialized */
    bool resolveFontNames,
    bool discardDefaultValues)
{
    Tcl_Obj *objPtr;
    Tcl_Interp *interp = textPtr->interp;

    Tcl_DStringTrunc(result, 0);

    if ((objPtr = Tk_GetOptionInfo(interp, (char *) recordPtr, optionTable, NULL, textPtr->tkwin))) {
	Tcl_Obj **objv;
	Tcl_Obj *font = NULL;   /* shut up compiler */
	Tcl_Obj *actual = NULL; /* shut up compiler */
	int objc = 0;
	int i;

	Tcl_ListObjGetElements(interp, objPtr, &objc, &objv);

	if (resolveFontNames) {
	    Tcl_IncrRefCount(font = Tcl_NewStringObj("font", -1));
	    Tcl_IncrRefCount(actual = Tcl_NewStringObj("actual", -1));
	}

	for (i = 0; i < objc; ++i) {
	    Tcl_Obj **argv;
	    int argc = 0;

	    Tcl_ListObjGetElements(interp, objv[i], &argc, &argv);

	    if (argc >= 5) { /* only if this option has a non-default value */
		Tcl_Obj *val = argv[4];

		if (GetByteLength(val) > 0) {
		    Tcl_Obj *value = val;
		    Tcl_Obj *name;
		    int len;

		    if (discardDefaultValues) {
			Tcl_Obj *dflt = argv[3];

			if (ObjIsEqual(dflt, val)) {
			    continue;
			}
		    }

		    name = argv[0];
		    if (Tcl_DStringLength(result) > 0) {
			Tcl_DStringAppend(result, " ", 1);
		    }
		    Tcl_DStringAppend(result, Tcl_GetString(name), GetByteLength(name));
		    Tcl_DStringAppend(result, " ", 1);

		    if (resolveFontNames
			    && strcmp(Tcl_GetString(name), "-font") == 0
			    && (Tcl_ListObjLength(interp, val, &len) != TCL_OK || len == 1)) {
			const char *s = Tcl_GetString(val);
			unsigned len = GetByteLength(val);

			/*
			 * Don't resolve font names like TkFixedFont, TkTextFont, etc.
			 */

			if (len < 7
				|| strncmp(s, "Tk", 2) != 0
				|| strncmp(s + len - 4, "Font", 4) != 0) {
			    Tcl_Obj *args[3];
			    Tcl_Obj *result;

			    /*
			     * Try to resolve the font name to the actual font attributes.
			     */

			    args[0] = font;
			    args[1] = actual;
			    args[2] = val;

			    if ((result = GetFontAttrs(textPtr, 3, args))) {
				value = result;
			    }
			}
		    }

		    Tcl_DStringAppendElement(result, Tcl_GetString(value));

		    if (value != val) {
			Tcl_DecrRefCount(value);
		    }
		}
	    }
	}

	if (resolveFontNames) {
	    Tcl_DecrRefCount(actual);
	    Tcl_DecrRefCount(font);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TextInspectCmd --
 *
 *	Return information about text and the associated tags.
 *	About the format see documentation.
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

static void
GetBindings(
    TkText *textPtr,
    const char *name,
    Tk_BindingTable bindingTable,
    Tcl_DString *str)
{
    Tcl_Interp *interp = textPtr->interp;
    Tcl_DString str2;
    Tcl_Obj **argv;
    int argc, i;

    Tk_GetAllBindings(interp, bindingTable, (ClientData) name);
    Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp), &argc, &argv);
    Tcl_DStringInit(&str2);

    for (i = 0; i < argc; ++i) {
	const char *event = Tcl_GetString(argv[i]);
	const char *binding = Tk_GetBinding(interp, bindingTable, (ClientData) name, event);
	char *p;

	Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp), &argc, &argv);

	Tcl_DStringStartSublist(str);
	Tcl_DStringAppendElement(str, "bind");
	Tcl_DStringAppendElement(str, name);
	Tcl_DStringAppendElement(str, event);

	Tcl_DStringTrunc(&str2, 0);
	p = strchr(binding, '\n');
	while (p) {
	    Tcl_DStringAppend(&str2, binding, p - binding);
	    Tcl_DStringAppend(&str2, "; ", 2);
	    binding = p + 1;
	    p = strchr(binding, '\n');
	}
	Tcl_DStringAppend(&str2, binding, -1);

	Tcl_DStringAppendElement(str, Tcl_DStringValue(&str2));
	Tcl_DStringEndSublist(str);
    }

    Tcl_DStringFree(&str2);
    Tcl_ResetResult(interp);
}

static int
TextInspectCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    TkSharedText *sharedTextPtr;
    TkTextTag *prevTagPtr;
    TkTextSegment *nextPtr;
    TkTextSegment *prevPtr;
    Tcl_DString buf[2];
    Tcl_DString *str = &buf[0];
    Tcl_DString *opts = &buf[1];
    TkTextTag **tagArray;
    TkTextTag *tagPtr;
    TkTextTag *tPtr;
    unsigned tagArrSize;
    unsigned epoch;
    unsigned what;
    bool closeSubList;
    int result;

    result = GetDumpFlags(textPtr, interp, objc, objv, TK_DUMP_INSPECT_ALL, TK_DUMP_INSPECT_DFLT,
	    &what, NULL, NULL, NULL, NULL);
    if (result != TCL_OK) {
	return result;
    }

    Tcl_DStringInit(str);
    Tcl_DStringInit(opts);
    sharedTextPtr = textPtr->sharedTextPtr;
    epoch = sharedTextPtr->inspectEpoch;
    tagPtr = textPtr->selTagPtr; /* any non-null value */
    nextPtr = textPtr->startMarker;
    closeSubList = false;
    prevTagPtr = NULL;
    prevPtr = NULL;
    tagArrSize = 128;
    tagArray = malloc(tagArrSize * sizeof(tagArray[0]));

    assert(textPtr->selTagPtr->textPtr == textPtr);
    if (what & TK_DUMP_DISCARD_SEL) {
	/* this little trick is discarding the "sel" tag */
	textPtr->selTagPtr->textPtr = (TkText *) textPtr->selTagPtr;
    }

    if (what & TK_DUMP_TEXT_CONFIGS) {
	TkTextInspectOptions(textPtr, textPtr, textPtr->optionTable, opts,
		!(what & TK_DUMP_DONT_RESOLVE), false);
	Tcl_DStringStartSublist(str);
	Tcl_DStringAppendElement(str, "setup");
	Tcl_DStringAppendElement(str, Tk_PathName(textPtr->tkwin));
	Tcl_DStringAppendElement(str, Tcl_DStringValue(opts));
	Tcl_DStringEndSublist(str);
    }

    if (what & TK_DUMP_TAG_CONFIGS) {
	TkTextTag **tags = textPtr->sharedTextPtr->tagLookup;
	unsigned n = textPtr->sharedTextPtr->numTags;
	unsigned i;

	for (i = 0; i < n; ++i) {
	    TkTextTag *tagPtr = tags[i];

	    if (tagPtr && (!(what & TK_DUMP_DISCARD_SEL) || tagPtr != textPtr->selTagPtr)) {
		TkTextInspectOptions(textPtr, tagPtr, tagPtr->optionTable, opts,
			!(what & TK_DUMP_DONT_RESOLVE), true);
		    Tcl_DStringStartSublist(str);
		    Tcl_DStringAppendElement(str, "configure");
		    Tcl_DStringAppendElement(str, tagPtr->name);
		    if (Tcl_DStringLength(opts) > 2) {
			Tcl_DStringAppendElement(str, Tcl_DStringValue(opts));
		    }
		    Tcl_DStringEndSublist(str);
	    }
	}
    }

    if (what & TK_DUMP_TAG_BINDINGS) {
	TkTextTag **tags = textPtr->sharedTextPtr->tagLookup;
	unsigned n = textPtr->sharedTextPtr->numTags;
	unsigned i;

	for (i = 0; i < n; ++i) {
	    TkTextTag *tagPtr = tags[i];

	    if (tagPtr && (!(what & TK_DUMP_DISCARD_SEL) || tagPtr != textPtr->selTagPtr)) {
		GetBindings(textPtr, tagPtr->name, sharedTextPtr->tagBindingTable, str);
	    }
	}
    }

    do {
	TkTextSegment *segPtr = nextPtr;
	unsigned group = segPtr->typePtr->group;
	const char *value = NULL;
	const char *type = NULL;
	bool printTags = false;

	nextPtr = segPtr->nextPtr;

	switch (group) {
	case SEG_GROUP_BRANCH:
	    if (segPtr->typePtr == &tkTextBranchType && (what & TK_DUMP_DISPLAY)) {
		segPtr = segPtr->body.branch.nextPtr;
		nextPtr = segPtr->nextPtr;
	    }
	    if (!(what & SEG_GROUP_BRANCH)) {
		continue;
	    }
	    type = "elide";
	    value = (segPtr->typePtr == &tkTextBranchType) ? "on" : "off";
	    break;
	case SEG_GROUP_IMAGE:
	    if (!(what & SEG_GROUP_IMAGE) || !segPtr->body.ei.name) {
		continue;
	    }
	    type = "image";
	    TkTextInspectOptions(textPtr, &segPtr->body.ei, segPtr->body.ei.optionTable, opts,
		    false, false);
	    value = Tcl_DStringValue(opts);
	    printTags = !!(what & TK_DUMP_TAG);
	    break;
	case SEG_GROUP_WINDOW:
	    if (!(what & SEG_GROUP_WINDOW) || !segPtr->body.ew.tkwin) {
		continue;
	    }
	    type = "window";
	    TkTextInspectOptions(textPtr, &segPtr->body.ew, segPtr->body.ew.optionTable, opts,
		    false, false);
	    value = Tcl_DStringValue(opts);
	    printTags = !!(what & TK_DUMP_TAG);
	    break;
	case SEG_GROUP_MARK:
	    if (segPtr == textPtr->endMarker) {
		if (prevPtr != segPtr
		    	&& (what & SEG_GROUP_CHAR)
			&& segPtr->sectionPtr->linePtr != TkBTreeGetLastLine(textPtr)) {
		    /* print newline before finishing */
		    type = "break";
		    printTags = !!(what & TK_DUMP_TAG);
		    tagPtr = TkBTreeGetSegmentTags(sharedTextPtr,
			    segPtr->sectionPtr->linePtr->lastPtr, textPtr, NULL);
		    nextPtr = segPtr; /* repeat this mark */
		} else {
		    nextPtr = NULL; /* finished */
		}
	    } else if (!(what & SEG_GROUP_MARK)) {
		continue;
	    } else if (!TkTextIsNormalMark(segPtr)
		    && (!(what & TK_DUMP_INSERT_MARK) || segPtr != textPtr->insertMarkPtr)) {
		continue;
	    } else {
		type = (segPtr->typePtr == &tkTextLeftMarkType ? "left" : "right");
		value = TkTextMarkName(sharedTextPtr, textPtr, segPtr);
	    }
	    break;
	case SEG_GROUP_HYPHEN:
	    if (!(what & SEG_GROUP_HYPHEN)) {
		continue;
	    }
	    printTags = !!(what & TK_DUMP_TAG);
	    type = "hyphen";
	    break;
	case SEG_GROUP_CHAR:
	    if (what & SEG_GROUP_CHAR) {
		printTags = !!(what & TK_DUMP_TAG);
		if (prevPtr == segPtr || *segPtr->body.chars == '\n') {
		    type = "break";
		    nextPtr = segPtr->sectionPtr->linePtr->nextPtr->segPtr;
		    if (prevPtr == segPtr) {
			tagPtr = prevTagPtr;
			segPtr->body.chars[segPtr->size - 1] = '\n';
		    } else if (type && printTags) {
			tagPtr = TkBTreeGetSegmentTags(sharedTextPtr, segPtr, textPtr, NULL);
		    }
		} else {
		    type = "text";
		    if (segPtr->size > 1 && segPtr->body.chars[segPtr->size - 1] == '\n') {
			nextPtr = segPtr; /* repeat this char segment */
			segPtr->body.chars[segPtr->size - 1] = '\0';
		    }
		    value = segPtr->body.chars;
		    if (printTags) {
			tagPtr = TkBTreeGetSegmentTags(sharedTextPtr, segPtr, textPtr, NULL);
		    }
		}
	    } else if (!nextPtr) {
		nextPtr = segPtr->sectionPtr->linePtr->nextPtr->segPtr;
	    }
	    break;
	default:
	    continue;
	}

	if (closeSubList) {
	    if (what & TK_DUMP_NESTED) {
		unsigned nextEpoch = epoch + 1;
		unsigned numTags = 0;
		unsigned i;

		for (tPtr = tagPtr; tPtr; tPtr = tPtr->nextPtr) {
		    if (tPtr->flag == epoch) {
			tPtr->flag = nextEpoch; /* mark as still open */
		    }
		}

		for ( ; prevTagPtr; prevTagPtr = prevTagPtr->succPtr) {
		    if (prevTagPtr->flag == epoch) { /* should be closed? */
			if (numTags == tagArrSize) {
			    tagArrSize *= 2;
			    tagArray = realloc(tagArray, tagArrSize * sizeof(tagArray[0]));
			}
			tagArray[numTags++] = prevTagPtr;
			prevTagPtr->flag = 0; /* mark as closed */
		    }
		}

		Tcl_DStringStartSublist(str);
		TkTextSortTags(numTags, tagArray);
		for (i = 0; i < numTags; ++i) {
		    Tcl_DStringAppendElement(str, tagArray[i]->name);
		}
		Tcl_DStringEndSublist(str);
	    }

	    prevTagPtr = NULL;
	    closeSubList = false;
	    Tcl_DStringEndSublist(str);
	}

	if (type) {
	    Tcl_DStringStartSublist(str);
	    Tcl_DStringAppendElement(str, type);
	    if (value) {
		Tcl_DStringAppendElement(str, value);
	    }
	    closeSubList = true;

	    if (printTags) {
		unsigned numTags = 0;
		unsigned i;

		prevTagPtr = tagPtr;

		if (what & TK_DUMP_NESTED) {
		    epoch += 1;

		    for (tPtr = tagPtr; tPtr; tPtr = tPtr->nextPtr) {
			if (tPtr->flag != epoch) { /* should be opened? */
			    if (numTags == tagArrSize) {
				tagArrSize *= 2;
				tagArray = realloc(tagArray, tagArrSize * sizeof(tagArray[0]));
			    }
			    tagArray[numTags++] = tPtr;
			    tPtr->flag = epoch; /* mark as open */
			}
			tPtr->succPtr = tPtr->nextPtr;
		    }
		} else {
		    for (tPtr = tagPtr; tPtr; tPtr = tPtr->nextPtr) {
			if (numTags == tagArrSize) {
			    tagArrSize *= 2;
			    tagArray = realloc(tagArray, tagArrSize * sizeof(tagArray[0]));
			}
			tagArray[numTags++] = tPtr;
		    }
		}

		Tcl_DStringStartSublist(str);
		TkTextSortTags(numTags, tagArray);
		for (i = 0; i < numTags; ++i) {
		    Tcl_DStringAppendElement(str, tagArray[i]->name);
		}
		Tcl_DStringEndSublist(str);
	    }
	}

	prevPtr = segPtr;
    } while (nextPtr);

    Tcl_SetObjResult(interp, Tcl_NewStringObj(Tcl_DStringValue(str), Tcl_DStringLength(str)));
    Tcl_DStringFree(str);
    Tcl_DStringFree(opts);
    free(tagArray);

    textPtr->selTagPtr->textPtr = textPtr; /* restore */
    sharedTextPtr->inspectEpoch = epoch;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * InspectRetainedUndoItems --
 *
 *	Return information about content of retained undo items, these
 *	items are not yet pushed onto undo stack.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is allocated for the result.
 *
 *----------------------------------------------------------------------
 */

static void
InspectRetainedUndoItems(
    const TkSharedText *sharedTextPtr,
    Tcl_Obj *objPtr)
{
    if (sharedTextPtr->undoTagListCount > 0 || sharedTextPtr->undoMarkListCount > 0) {
	Tcl_Obj *resultPtr = Tcl_NewObj();
	unsigned i;
	int len;

	for (i = 0; i < sharedTextPtr->undoTagListCount; ++i) {
	    TkTextInspectUndoTagItem(sharedTextPtr, sharedTextPtr->undoTagList[i], resultPtr);
	}

	for (i = 0; i < sharedTextPtr->undoMarkListCount; ++i) {
	    TkTextInspectUndoMarkItem(sharedTextPtr, &sharedTextPtr->undoMarkList[i], resultPtr);
	}

	Tcl_ListObjLength(NULL, resultPtr, &len);
	if (len == 0) {
	    Tcl_IncrRefCount(resultPtr);
	    Tcl_DecrRefCount(resultPtr);
	} else {
	    Tcl_ListObjAppendElement(NULL, objPtr, resultPtr);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * InspectUndoStack --
 *
 *	Return information about content of undo/redo stack.
 *
 * Results:
 *	A Tcl object.
 *
 * Side effects:
 *	Memory is allocated for the result.
 *
 *----------------------------------------------------------------------
 */

static void
InspectUndoStack(
    const TkSharedText *sharedTextPtr,
    InspectUndoStackProc firstAtomProc,
    InspectUndoStackProc nextAtomProc,
    Tcl_Obj *objPtr)
{
    TkTextUndoStack undoStack;
    const TkTextUndoAtom *atom;
    Tcl_Obj *atomPtr;
    unsigned i;

    assert(sharedTextPtr->undoStack);

    undoStack = sharedTextPtr->undoStack;

    for (atom = firstAtomProc(undoStack); atom; atom = nextAtomProc(undoStack)) {
	atomPtr = Tcl_NewObj();

	for (i = 0; i < atom->arraySize; ++i) {
	    const TkTextUndoToken *token = (const TkTextUndoToken *) atom->array[i].item;
	    Tcl_Obj *subAtomPtr = token->undoType->inspectProc(sharedTextPtr, token);
	    Tcl_ListObjAppendElement(NULL, atomPtr, subAtomPtr);
	}

	Tcl_ListObjAppendElement(NULL, objPtr, atomPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TextEditCmd --
 *
 *	Handle the subcommands to "$text edit ...". See documentation for
 *	details.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *
GetCommand(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *token)
{
    assert(token->undoType->commandProc);
    return token->undoType->commandProc(sharedTextPtr, token);
}

static int
TextEditCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    int index;
    bool setModified, oldModified;
    TkSharedText *sharedTextPtr;
    static const char *const editOptionStrings[] = {
	"altered",
#if SUPPORT_DEPRECATED_CANUNDO_REDO
	"canredo", "canundo",
#endif /* SUPPORT_DEPRECATED_CANUNDO_REDO */
	"info", "inspect", "irreversible", "modified", "recover", "redo", "reset",
	"separator", "undo", NULL
    };
    enum editOptions {
	EDIT_ALTERED,
#if SUPPORT_DEPRECATED_CANUNDO_REDO
	EDIT_CANREDO, EDIT_CANUNDO,
#endif /* SUPPORT_DEPRECATED_CANUNDO_REDO */
	EDIT_INFO, EDIT_INSPECT, EDIT_IRREVERSIBLE, EDIT_MODIFIED, EDIT_RECOVER, EDIT_REDO, EDIT_RESET,
	EDIT_SEPARATOR, EDIT_UNDO
    };

    sharedTextPtr = textPtr->sharedTextPtr;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "option ?arg ...?");
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObjStruct(interp, objv[2], editOptionStrings,
	    sizeof(char *), "edit option", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }

    switch ((enum editOptions) index) {
    case EDIT_ALTERED:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, "?boolean?");
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(sharedTextPtr->isAltered));
	return TCL_OK;
	break;
#if SUPPORT_DEPRECATED_CANUNDO_REDO
    case EDIT_CANREDO: {
	static bool warnDeprecated = true;
	bool canRedo = false;

	if (warnDeprecated) {
	    warnDeprecated = false;
	    fprintf(stderr, "Command \"edit canredo\" is deprecated, please use \"edit info\"\n");
	}
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, NULL);
	     return TCL_ERROR;
	}
	if (textPtr->sharedTextPtr->undoStack) {
	    canRedo = TkTextUndoGetCurrentRedoStackDepth(textPtr->sharedTextPtr->undoStack) > 0;
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(canRedo));
	break;
    }
    case EDIT_CANUNDO: {
	static bool warnDeprecated = true;
	bool canUndo = false;

	if (warnDeprecated) {
	    warnDeprecated = false;
	    fprintf(stderr, "Command \"edit canundo\" is deprecated, please use \"edit info\"\n");
	}
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, NULL);
	     return TCL_ERROR;
	}
	if (textPtr->sharedTextPtr->undo) {
	    canUndo = TkTextUndoGetCurrentUndoStackDepth(textPtr->sharedTextPtr->undoStack) > 0;
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(canUndo));
	break;
    }
#endif /* SUPPORT_DEPRECATED_CANUNDO_REDO */
    case EDIT_INFO:
	if (objc != 3 && objc != 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "?array?");
	    return TCL_ERROR;
	} else {
	    Tcl_SetObjResult(textPtr->interp, MakeEditInfo(interp, textPtr, objc == 4 ? objv[3] : NULL));
	}
    	break;
    case EDIT_INSPECT:
	if (objc != 3 && objc != 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "?stack?");
	    return TCL_ERROR;
	} else {
	    char const *stack = (objc == 4) ? Tcl_GetString(objv[3]) : NULL;

	    if (stack && strcmp(stack, "undo") != 0 && strcmp(stack, "redo") != 0) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad stack argument \"%s\": must be \"undo\" or \"redo\"", stack));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "STACK_VALUE", NULL);
		return TCL_ERROR;
	    }
	    if (sharedTextPtr->undoStack) {
		Tcl_Obj *undoResultPtr = NULL;
		Tcl_Obj *redoResultPtr = NULL;

		if (!stack || stack[0] == 'u') {
		    undoResultPtr = Tcl_NewObj();
		    InspectRetainedUndoItems(sharedTextPtr, undoResultPtr);
		    InspectUndoStack(sharedTextPtr, TkTextUndoFirstUndoAtom,
			    TkTextUndoNextUndoAtom, undoResultPtr);
		}
		if (!stack || stack[0] == 'r') {
		    redoResultPtr = Tcl_NewObj();
		    InspectUndoStack(sharedTextPtr, TkTextUndoFirstRedoAtom,
			    TkTextUndoNextRedoAtom, redoResultPtr);
		}
		if (!stack) {
		    Tcl_Obj *objPtr = Tcl_NewObj();
		    Tcl_ListObjAppendElement(NULL, objPtr, undoResultPtr);
		    Tcl_ListObjAppendElement(NULL, objPtr, redoResultPtr);
		    Tcl_SetObjResult(interp, objPtr);
		} else if (stack[0] == 'u') {
		    Tcl_SetObjResult(interp, undoResultPtr);
		} else {
		    Tcl_SetObjResult(interp, redoResultPtr);
		}
	    }
	}
	break;
    case EDIT_IRREVERSIBLE:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, "?boolean?");
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(sharedTextPtr->isIrreversible));
	break;
    case EDIT_MODIFIED:
	if (objc == 3) {
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(sharedTextPtr->isModified));
	    return TCL_OK;
	} else if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "?boolean?");
	    return TCL_ERROR;
	} else if (Tcl_GetBooleanFromObj(interp, objv[3], (int *) &setModified) != TCL_OK) {
	    return TCL_ERROR;
	}

	/*
	 * Set or reset the modified status, and trigger a <<Modified>> event.
	 */

	oldModified = sharedTextPtr->isModified;
	sharedTextPtr->isModified = setModified;

	/*
	 * Setting the flag to 'false' is clearing the user's decision.
	 */

	sharedTextPtr->userHasSetModifiedFlag = setModified;
	if (sharedTextPtr->undoStack) {
	    sharedTextPtr->undoLevel = TkTextUndoGetCurrentUndoStackDepth(sharedTextPtr->undoStack);
	}

	/*
	 * Only issue the <<Modified>> event if the flag actually changed.
	 * However, degree of modified-ness doesn't matter. [Bug 1799782]
	 */

	assert(setModified == true || setModified == false);

	if (oldModified != setModified) {
	    GenerateEvent(textPtr->sharedTextPtr, "Modified");
	}
	break;
    case EDIT_RECOVER:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, NULL);
	    return TCL_ERROR;
	}
	if (sharedTextPtr->undoStack) {
	    int redoDepth;

	    if (TkTextUndoIsPerformingUndoRedo(sharedTextPtr->undoStack)) {
		ErrorNotAllowed(interp, "cannot recover inside undo/redo operation");
		return TCL_ERROR;
	    }

	    redoDepth = TkTextUndoGetMaxRedoDepth(sharedTextPtr->undoStack);
	    PushRetainedUndoTokens(sharedTextPtr);
	    TkTextUndoSetMaxStackDepth(sharedTextPtr->undoStack, textPtr->maxUndoDepth, 0);

	    while (TkTextUndoGetCurrentUndoStackDepth(sharedTextPtr->undoStack) > 0) {
		TkTextUndoDoUndo(sharedTextPtr->undoStack);
	    }

	    TkTextUndoSetMaxStackDepth(sharedTextPtr->undoStack, textPtr->maxUndoDepth, redoDepth);
	}
    	break;
    case EDIT_REDO:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, NULL);
	    return TCL_ERROR;
	}
	if (sharedTextPtr->undoStack) {
	    /*
	     * It's possible that this command command will be invoked inside the "watch" callback,
	     * but this is not allowed when performing undo/redo.
	     */

	    if (TkTextUndoIsPerformingUndoRedo(sharedTextPtr->undoStack)) {
		ErrorNotAllowed(interp, "cannot redo inside undo/redo operation");
		return TCL_ERROR;
	    }

	    if (TkTextUndoGetCurrentRedoStackDepth(sharedTextPtr->undoStack) == 0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("nothing to redo", -1));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "NO_REDO", NULL);
		return TCL_ERROR;
	    }

	    PushRetainedUndoTokens(sharedTextPtr);
	    TkTextUndoDoRedo(sharedTextPtr->undoStack);
	}
	break;
    case EDIT_RESET:
	if (objc == 3) {
	    if (sharedTextPtr->undoStack) {
		/*
		 * It's possible that this command command will be invoked inside the "watch" callback,
		 * but this is not allowed when performing undo/redo.
		 */

		if (TkTextUndoIsPerformingUndoRedo(sharedTextPtr->undoStack)) {
		    ErrorNotAllowed(interp, "cannot reset stack inside undo/redo operation");
		    return TCL_ERROR;
		}

		TkTextUndoClearStack(sharedTextPtr->undoStack);
		sharedTextPtr->undoLevel = 0;
		sharedTextPtr->isAltered = false;
		sharedTextPtr->isIrreversible = false;
		TkTextUpdateAlteredFlag(sharedTextPtr);
	    }
	    return TCL_OK;
	} else if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "?stack?");
	    return TCL_ERROR;
	} else {
	    char const *stack = Tcl_GetString(objv[3]);

	    if (strcmp(stack, "undo") != 0 && strcmp(stack, "redo") != 0) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad stack argument \"%s\": must be \"undo\" or \"redo\"", stack));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "STACK_VALUE", NULL);
		return TCL_ERROR;
	    }
	    if (sharedTextPtr->undoStack) {
		if (TkTextUndoIsPerformingUndoRedo(sharedTextPtr->undoStack)) {
		    /*
		     * It's possible that this command command will be invoked inside
		     * the "watch" callback, but this is not allowed when performing
		     * undo/redo.
		     */

		    ErrorNotAllowed(interp, "cannot reset stack inside undo/redo operation");
		    return TCL_ERROR;
		}
		if (stack[0] == 'u') {
		    TkTextUndoClearUndoStack(sharedTextPtr->undoStack);
		    sharedTextPtr->undoLevel = 0;
		    sharedTextPtr->isAltered = false;
		    sharedTextPtr->isIrreversible = false;
		    TkTextUpdateAlteredFlag(sharedTextPtr);
		} else {
		    TkTextUndoClearRedoStack(sharedTextPtr->undoStack);
		}
	    }
	    return TCL_ERROR;
	}
	break;
    case EDIT_SEPARATOR: {
	bool immediately = false;

	if (objc == 4) {
	    if (strcmp(Tcl_GetString(objv[3]), "-immediately")) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad option \"%s\": must be -immediately", Tcl_GetString(objv[3])));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "INDEX_OPTION", NULL);
		return TCL_ERROR;
	    }
	    immediately = true;
	} else if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, NULL);
	    return TCL_ERROR;
	}
	if (sharedTextPtr->undoStack) {
	    if (immediately) {
		PushRetainedUndoTokens(sharedTextPtr);
	    }
	    TkTextUndoPushSeparator(sharedTextPtr->undoStack, immediately);
	    sharedTextPtr->lastUndoTokenType = -1;
	}
	break;
    }
    case EDIT_UNDO:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, NULL);
	    return TCL_ERROR;
	}
	if (sharedTextPtr->undoStack) {
	    /*
	     * It's possible that this command command will be invoked inside the "watch" callback,
	     * but this is not allowed when performing undo/redo.
	     */

	    if (TkTextUndoIsPerformingUndoRedo(sharedTextPtr->undoStack)) {
		ErrorNotAllowed(interp, "cannot undo inside undo/redo operation");
		return TCL_ERROR;
	    }

	    PushRetainedUndoTokens(sharedTextPtr);

	    if (TkTextUndoGetCurrentUndoStackDepth(sharedTextPtr->undoStack) == 0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("nothing to undo", -1));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "NO_UNDO", NULL);
		return TCL_ERROR;
	    }

	    TkTextUndoDoUndo(sharedTextPtr->undoStack);
	}
	break;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * MakeEditInfo --
 *
 *	Returns the array containing the "edit info" information.
 *
 * Results:
 *	Tcl_Obj of list type containing the required information.
 *
 * Side effects:
 *	Some memory will be allocated:
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *
MakeEditInfo(
    Tcl_Interp *interp,		/* Current interpreter. */
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Obj *arrayPtr)		/* Name of array, may be NULL. */
{
    enum {
	INFO_UNDOSTACKSIZE, INFO_REDOSTACKSIZE, INFO_UNDODEPTH, INFO_REDODEPTH,
	INFO_UNDOBYTESIZE, INFO_REDOBYTESIZE, INFO_UNDOCOMMANDS, INFO_REDOCOMMANDS,
	INFO_BYTESIZE, INFO_TOTALBYTESIZE, INFO_LINES, INFO_TOTALLINES, INFO_IMAGES,
	INFO_WINDOWS, INFO_DISPIMAGES, INFO_DISPWINDOWS, INFO_TAGS, INFO_USEDTAGS,
	INFO_MARKS, INFO_GENERATEDMARKS, INFO_LINESPERNODE,
	INFO_LAST /* must be last item */
    };

    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    TkTextUndoStack st = sharedTextPtr->undoStack;
    Tcl_Obj *var = arrayPtr ? arrayPtr : Tcl_NewStringObj("", 0);
    Tcl_Obj *name[INFO_LAST];
    Tcl_Obj *value[INFO_LAST];
    int usedTags, i;
    unsigned k;

    name[INFO_UNDOSTACKSIZE ] = Tcl_NewStringObj("undostacksize", -1);
    name[INFO_REDOSTACKSIZE ] = Tcl_NewStringObj("redostacksize", -1);
    name[INFO_UNDODEPTH     ] = Tcl_NewStringObj("undodepth", -1);
    name[INFO_REDODEPTH     ] = Tcl_NewStringObj("redodepth", -1);
    name[INFO_UNDOBYTESIZE  ] = Tcl_NewStringObj("undobytesize", -1);
    name[INFO_REDOBYTESIZE  ] = Tcl_NewStringObj("redobytesize", -1);
    name[INFO_UNDOCOMMANDS  ] = Tcl_NewStringObj("undocommands", -1);
    name[INFO_REDOCOMMANDS  ] = Tcl_NewStringObj("redocommands", -1);
    name[INFO_BYTESIZE      ] = Tcl_NewStringObj("bytesize", -1);
    name[INFO_TOTALBYTESIZE ] = Tcl_NewStringObj("totalbytesize", -1);
    name[INFO_LINES         ] = Tcl_NewStringObj("lines", -1);
    name[INFO_TOTALLINES    ] = Tcl_NewStringObj("totallines", -1);
    name[INFO_IMAGES        ] = Tcl_NewStringObj("images", -1);
    name[INFO_WINDOWS       ] = Tcl_NewStringObj("windows", -1);
    name[INFO_DISPIMAGES    ] = Tcl_NewStringObj("visibleimages", -1);
    name[INFO_DISPWINDOWS   ] = Tcl_NewStringObj("visiblewindows", -1);
    name[INFO_TAGS          ] = Tcl_NewStringObj("tags", -1);
    name[INFO_USEDTAGS      ] = Tcl_NewStringObj("usedtags", -1);
    name[INFO_MARKS         ] = Tcl_NewStringObj("marks", -1);
    name[INFO_GENERATEDMARKS] = Tcl_NewStringObj("generatedmarks", -1);
    name[INFO_LINESPERNODE  ] = Tcl_NewStringObj("linespernode", -1);

    if (st) {
	const TkTextUndoAtom *atom;
	Tcl_Obj *listPtr;

	value[INFO_UNDOSTACKSIZE] = Tcl_NewIntObj(TkTextUndoCountUndoItems(st));
	value[INFO_REDOSTACKSIZE] = Tcl_NewIntObj(TkTextUndoCountRedoItems(st));
	value[INFO_UNDODEPTH    ] = Tcl_NewIntObj(TkTextUndoGetCurrentUndoStackDepth(st));
	value[INFO_REDODEPTH    ] = Tcl_NewIntObj(TkTextUndoGetCurrentRedoStackDepth(st));
	value[INFO_UNDOBYTESIZE ] = Tcl_NewIntObj(TkTextUndoGetCurrentUndoSize(st));
	value[INFO_REDOBYTESIZE ] = Tcl_NewIntObj(TkTextUndoGetCurrentRedoSize(st));
	value[INFO_UNDOCOMMANDS ] = Tcl_NewObj();
	value[INFO_REDOCOMMANDS ] = Tcl_NewObj();

	listPtr = value[TkTextUndoIsPerformingUndo(st) ? INFO_REDOCOMMANDS : INFO_UNDOCOMMANDS];

	for (i = sharedTextPtr->undoTagListCount - 1; i >= 0; --i) {
	    const TkTextTag *tagPtr = sharedTextPtr->undoTagList[i];

	    if (tagPtr->recentTagAddRemoveToken && !tagPtr->recentTagAddRemoveTokenIsNull) {
		Tcl_ListObjAppendElement(interp, listPtr,
			GetCommand(sharedTextPtr, tagPtr->recentTagAddRemoveToken));
	    }
	    if (tagPtr->recentChangePriorityToken && tagPtr->savedPriority != tagPtr->priority) {
		Tcl_ListObjAppendElement(interp, listPtr,
			GetCommand(sharedTextPtr, tagPtr->recentTagAddRemoveToken));
	    }
	}

	for (i = sharedTextPtr->undoMarkListCount - 1; i >= 0; --i) {
	    const TkTextMarkChange *changePtr = &sharedTextPtr->undoMarkList[i];

	    if (changePtr->setMark) {
		Tcl_ListObjAppendElement(interp, listPtr,
			GetCommand(sharedTextPtr, changePtr->setMark));
	    }
	    if (changePtr->moveMark) {
		Tcl_ListObjAppendElement(interp, listPtr,
			GetCommand(sharedTextPtr, changePtr->moveMark));
	    }
	    if (changePtr->toggleGravity) {
		Tcl_ListObjAppendElement(interp, listPtr,
			GetCommand(sharedTextPtr, changePtr->toggleGravity));
	    }
	}

	atom = TkTextUndoIsPerformingUndo(st) ?
		TkTextUndoCurrentRedoAtom(st) : TkTextUndoCurrentUndoAtom(st);

	if (atom) {
	    for (i = atom->arraySize - 1; i >= 0; --i) {
		const TkTextUndoSubAtom *subAtom = atom->array + i;
		TkTextUndoToken *token = subAtom->item;

		Tcl_ListObjAppendElement(interp, listPtr, GetCommand(sharedTextPtr, token));
	    }
	}
    } else {
	value[INFO_UNDOSTACKSIZE] =
		value[INFO_REDOSTACKSIZE] =
		value[INFO_UNDODEPTH] =
		value[INFO_REDODEPTH] =
		value[INFO_UNDOBYTESIZE] =
		value[INFO_REDOBYTESIZE] = Tcl_NewIntObj(0);
	value[INFO_UNDOCOMMANDS] = value[INFO_REDOCOMMANDS] = Tcl_NewObj();
    }

    usedTags = TkTextTagSetCount(TkBTreeRootTagInfo(sharedTextPtr->tree));

    /* Related to this widget */

    value[INFO_BYTESIZE      ] = Tcl_NewIntObj(TkBTreeSize(sharedTextPtr->tree, textPtr));
    value[INFO_LINES         ] = Tcl_NewIntObj(TkBTreeNumLines(sharedTextPtr->tree, textPtr));
    value[INFO_DISPIMAGES    ] = Tcl_NewIntObj(TkTextCountVisibleImages(textPtr));
    value[INFO_DISPWINDOWS   ] = Tcl_NewIntObj(TkTextCountVisibleWindows(textPtr));

    /* Related to shared resource */

    value[INFO_TOTALBYTESIZE ] = Tcl_NewIntObj(TkBTreeSize(sharedTextPtr->tree, NULL));
    value[INFO_TOTALLINES    ] = Tcl_NewIntObj(TkBTreeNumLines(sharedTextPtr->tree, NULL));
    value[INFO_IMAGES        ] = Tcl_NewIntObj(sharedTextPtr->numImages);
    value[INFO_WINDOWS       ] = Tcl_NewIntObj(sharedTextPtr->numWindows);
    value[INFO_TAGS          ] = Tcl_NewIntObj(sharedTextPtr->numTags);
    value[INFO_USEDTAGS      ] = Tcl_NewIntObj(usedTags);
    value[INFO_MARKS         ] = Tcl_NewIntObj(sharedTextPtr->numMarks);
    value[INFO_GENERATEDMARKS] = Tcl_NewIntObj(sharedTextPtr->numPrivateMarks);
    value[INFO_LINESPERNODE  ] = Tcl_NewIntObj(TkBTreeLinesPerNode(sharedTextPtr->tree));

    Tcl_UnsetVar(interp, Tcl_GetString(var), 0);
    for (k = 0; k < sizeof(name)/sizeof(name[0]); ++k) {
	Tcl_ObjSetVar2(interp, var, name[k], value[k], 0);
    }

    return var;
}

/*
 *----------------------------------------------------------------------
 *
 * TextGetText --
 *
 *	Returns the text from indexPtr1 to indexPtr2, placing that text in a
 *	string object which is returned with a refCount of zero.
 *
 *	Since the amount of text may potentially be several megabytes (e.g.
 *	in text editors built on the text widget), efficiency is very
 *	important. We may want to investigate the efficiency of the
 *	Tcl_AppendToObj more carefully (e.g. if we know we are going to be
 *	appending several thousand lines, we could attempt to pre-allocate a
 *	larger space).
 *
 *	Also the result is built up as a utf-8 string, but, if we knew we
 *	wanted it as Unicode, we could potentially save a huge conversion by
 *	building it up as Unicode directly. This could be as simple as
 *	replacing Tcl_NewObj by Tcl_NewUnicodeObj.
 *
 * Results:
 *	Tcl_Obj of string type containing the specified text. If the
 *	visibleOnly flag is set to true, then only those characters which are not
 *	elided will be returned. Otherwise (flag is false) all characters in the
 *	given range are returned.
 *
 * Side effects:
 *	Memory will be allocated for the new object. Remember to free it if it
 *	isn't going to be stored appropriately.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *
TextGetText(
    TkText *textPtr,		/* Information about text widget. */
    const TkTextIndex *indexPtr1,
				/* Get text from this index... */
    const TkTextIndex *indexPtr2,
				/* ...to this index. */
    TkTextIndex *lastIndexPtr,	/* Position before last character of the result, can be NULL. */
    Tcl_Obj *resultPtr,		/* Append text to this object, can be NULL. */
    unsigned maxBytes,		/* Maximal number of bytes. */
    bool visibleOnly,		/* If true, then only return non-elided characters. */
    bool includeHyphens)	/* If true, then also include soft hyphens. */
{
    TkTextSegment *segPtr, *lastPtr;
    TkTextIndex index;
    int offset1, offset2;

    assert(textPtr);
    assert(TkTextIndexCompare(indexPtr1, indexPtr2) <= 0);

    if (!resultPtr) {
	resultPtr = Tcl_NewObj();
    }

    segPtr = TkTextIndexGetContentSegment(indexPtr1, &offset1);
    if (lastIndexPtr) {
	*lastIndexPtr = *indexPtr2;
    }

    if (visibleOnly && TkTextSegmentIsElided(textPtr, segPtr)) {
	index = *indexPtr1;
	if (!TkTextSkipElidedRegion(&index) || TkTextIndexCompare(&index, indexPtr2) >= 0) {
	    return resultPtr; /* end of text reached */
	}
	segPtr = TkTextIndexGetContentSegment(&index, &offset1);
    }

    lastPtr = TkTextIndexGetContentSegment(indexPtr2, &offset2);

    if (segPtr == lastPtr) {
	if (segPtr->typePtr == &tkTextCharType) {
	    Tcl_AppendToObj(resultPtr, segPtr->body.chars + offset1,
		    MIN(maxBytes, (unsigned) (offset2 - offset1)));
	}
    } else {
	TkTextLine *linePtr = segPtr->sectionPtr->linePtr;

	TkTextIndexClear(&index, textPtr);

	if (segPtr->typePtr == &tkTextCharType) {
	    unsigned nbytes = MIN(maxBytes, (unsigned) segPtr->size - offset1);
	    Tcl_AppendToObj(resultPtr, segPtr->body.chars + offset1, nbytes);
	    if ((maxBytes -= nbytes) == 0) {
		return resultPtr;
	    }
	} else if (segPtr->typePtr == &tkTextHyphenType) {
	    if (includeHyphens) {
		Tcl_AppendToObj(resultPtr, "\xc2\xad", 2); /* U+002D */
		if ((maxBytes -= MIN(maxBytes, 2u)) == 0) {
		    return resultPtr;
		}
	    }
	} else if (segPtr->typePtr == &tkTextBranchType) {
	    if (visibleOnly) {
		TkTextIndexSetSegment(&index, segPtr = segPtr->body.branch.nextPtr);
		if (TkTextIndexRestrictToEndRange(&index) >= 0) {
		    return resultPtr; /* end of text reached */
		}
		linePtr = segPtr->sectionPtr->linePtr;
	    }
	}
	if (!(segPtr = segPtr->nextPtr)) {
	    assert(linePtr->nextPtr);
	    linePtr = linePtr->nextPtr;
	    segPtr = linePtr->segPtr;
	}
	while (segPtr != lastPtr) {
	    if (segPtr->typePtr == &tkTextCharType) {
		unsigned nbytes = MIN(maxBytes, (unsigned) segPtr->size);
		Tcl_AppendToObj(resultPtr, segPtr->body.chars, nbytes);
		if ((maxBytes -= nbytes) == 0) {
		    if (lastIndexPtr) {
			TkTextIndexSetSegment(lastIndexPtr, segPtr);
			TkTextIndexAddToByteIndex(lastIndexPtr, nbytes);
		    }
		    return resultPtr; /* end of text reached */
		}
	    } else if (segPtr->typePtr == &tkTextHyphenType) {
		if (includeHyphens) {
		    Tcl_AppendToObj(resultPtr, "\xc2\xad", 2); /* U+002D */
		    if ((maxBytes -= MIN(maxBytes, 2u)) == 0) {
			return resultPtr;
		    }
		}
	    } else if (segPtr->typePtr == &tkTextBranchType) {
		if (visibleOnly) {
		    TkTextIndexSetSegment(&index, segPtr = segPtr->body.branch.nextPtr);
		    if (TkTextIndexRestrictToEndRange(&index) >= 0) {
			return resultPtr; /* end of text reached */
		    }
		    linePtr = segPtr->sectionPtr->linePtr;
		}
	    }
	    if (!(segPtr = segPtr->nextPtr)) {
		assert(linePtr->nextPtr);
		linePtr = linePtr->nextPtr;
		segPtr = linePtr->segPtr;
	    }
	}
	if (offset2 > 0) {
	    Tcl_AppendToObj(resultPtr, segPtr->body.chars, MIN(maxBytes, (unsigned) offset2));
	}
    }

    return resultPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TriggerWatchEdit --
 *
 *	Trigger the watch command for delete/insert operations, see the
 *	documentation for details on what it does.
 *
 * Results:
 *	Returns 'false' if the referenced widget has been destroyed, otherwise
 *	'true' will be returned.
 *
 * Side effects:
 *	It might happen that the receiver of the "watch" command is destroying the widget.
 *
 *----------------------------------------------------------------------
 */

static void
AppendTags(
    Tcl_DString *buf,
    TkTextTag *tagPtr)
{
    Tcl_DStringStartSublist(buf);
    for ( ; tagPtr; tagPtr = tagPtr->nextPtr) {
	Tcl_DStringAppendElement(buf, tagPtr->name);
    }
    Tcl_DStringEndSublist(buf);
}

static bool
TriggerWatchEdit(
    TkText *textPtr,			/* Information about text widget. */
    const char *operation,		/* The triggering operation. */
    const TkTextIndex *indexPtr1,	/* Start index for deletion / insert. */
    const TkTextIndex *indexPtr2,	/* End index after insert / before deletion. */
    const char *string,			/* Deleted/inserted chars. */
    bool final)				/* Flag indicating whether this is a final part. */
{
    TkSharedText *sharedTextPtr;
    TkText *peerArr[20];
    TkText **peers = peerArr;
    TkText *tPtr;
    unsigned i, n = 0;
    bool rc = true;

    assert(textPtr->sharedTextPtr->triggerWatchCmd);
    assert(!indexPtr1 == !indexPtr2);
    assert(strcmp(operation, "insert") == 0 || strcmp(operation, "delete") == 0);

    sharedTextPtr = textPtr->sharedTextPtr;
    sharedTextPtr->triggerWatchCmd = false;

    if (sharedTextPtr->numPeers > sizeof(peerArr) / sizeof(peerArr[0])) {
	peers = malloc(sharedTextPtr->numPeers * sizeof(peerArr[0]));
    }

    /*
     * Firstly save all peers, we have to take into account that the list of
     * peers is changing when executing the "watch" command.
     */

    peers[n++] = textPtr;
    for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
	if (tPtr != textPtr) {
	    peers[n++] = tPtr;
	}
	tPtr->refCount += 1;
    }

    for (i = 0; i < sharedTextPtr->numPeers; ++i) {
	TkText *tPtr = peers[i];

	if (!(tPtr->flags & DESTROYED) && tPtr->watchCmd) {
	    TkTextIndex index[4];

	    if (indexPtr1) {
		TkTextSegment *startMarker;
		TkTextSegment *endMarker;
		int cmp;

		index[0] = *indexPtr1;
		index[1] = *indexPtr2;

		startMarker = tPtr->startMarker;
		endMarker = tPtr->endMarker;

		if (startMarker != sharedTextPtr->startMarker) {
		    TkTextIndex start;
		    TkTextIndexClear(&start, tPtr);
		    TkTextIndexSetSegment(&start, startMarker);
		    if (TkTextIndexCompare(&start, &index[0]) > 0) {
			index[0] = start;
		    }
		}
		if (endMarker != sharedTextPtr->endMarker) {
		    TkTextIndex end;
		    TkTextIndexClear(&end, tPtr);
		    TkTextIndexSetSegment(&end, endMarker);
		    if (TkTextIndexCompare(&end, &index[1]) < 0) {
			index[1] = end;
		    }
		}

		if ((cmp = TkTextIndexCompare(&index[0], &index[1])) <= 0) {
		    TkTextTag *tagPtr;
		    TkTextIndex myIndex;
		    Tcl_DString buf;
		    char idx[2][TK_POS_CHARS];
		    char const *arg;

		    TkTextPrintIndex(tPtr, &index[0], idx[0]);
		    TkTextPrintIndex(tPtr, &index[1], idx[1]);

		    Tcl_DStringInit(&buf);
		    Tcl_DStringAppendElement(&buf, string);

		    tagPtr = NULL;
		    if (TkTextIndexBackChars(tPtr, &index[0], 1, &myIndex, COUNT_CHARS)) {
			tagPtr = TkBTreeGetTags(&myIndex);
		    }
		    AppendTags(&buf, tagPtr);
		    AppendTags(&buf, TkBTreeGetTags(&index[1]));
		    AppendTags(&buf, cmp == 0 ? NULL : TkBTreeGetTags(&index[0]));
		    if (*operation == 'd') {
			tagPtr = NULL;
			if (cmp && TkTextIndexBackChars(tPtr, &index[1], 1, &myIndex, COUNT_CHARS)) {
			    tagPtr = TkBTreeGetTags(&myIndex);
			}
			AppendTags(&buf, tagPtr);
		    }
		    Tcl_DStringAppendElement(&buf, final ? "yes" : "no");
		    arg = Tcl_DStringValue(&buf);

		    if (!TkTextTriggerWatchCmd(tPtr, operation, idx[0], idx[1], arg, true)
			    && tPtr == textPtr) {
			rc = false; /* this widget has been destroyed */
		    }

		    Tcl_DStringFree(&buf);
		}
	    } else {
		if (!TkTextTriggerWatchCmd(textPtr, operation, NULL, NULL, NULL, true)
			&& tPtr == textPtr) {
		    rc = false; /* this widget has been destroyed */
		}
	    }
	}

	if (--tPtr->refCount == 0) {
	    free(tPtr);
	}
    }

    if (peers != peerArr) {
	free(peers);
    }

    sharedTextPtr->triggerWatchCmd = true;
    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextTriggerWatchCmd --
 *
 *	Trigger the watch command, see the documentation for details on
 *	what it does.
 *
 * Results:
 *	Returns 'false' if this peer has been destroyed, otherwise 'true'
 *	will be returned.
 *
 * Side effects:
 *	It might happen that the receiver of the "watch" command is destroying the widget.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextTriggerWatchCmd(
    TkText *textPtr,		/* Information about text widget. */
    const char *operation,	/* The trigger operation. */
    const char *index1,		/* 1st argument for watch command. */
    const char *index2,		/* 2nd argument for watch command. */
    const char *arg,		/* 3rd argument for watch command. */
    bool userFlag)		/* 4rd argument for watch command. */
{
    Tcl_DString cmd;

    assert(textPtr);
    assert(textPtr->watchCmd);
    assert(operation);

    Tcl_DStringInit(&cmd);
    Tcl_DStringAppend(&cmd, Tcl_GetString(textPtr->watchCmd), -1);
    Tcl_DStringAppendElement(&cmd, Tk_PathName(textPtr->tkwin));
    Tcl_DStringAppendElement(&cmd, operation);
    Tcl_DStringAppendElement(&cmd, index1 ? index1 : "");
    Tcl_DStringAppendElement(&cmd, index2 ? index2 : "");
    Tcl_DStringAppendElement(&cmd, arg ? arg : "");
    Tcl_DStringAppendElement(&cmd, userFlag ? "1" : "0");

    textPtr->refCount += 1;

    Tcl_Preserve((ClientData) textPtr->interp);
    if (Tcl_EvalEx(textPtr->interp, Tcl_DStringValue(&cmd), Tcl_DStringLength(&cmd), 0) != TCL_OK) {
	Tcl_AddErrorInfo(textPtr->interp, "\n    (triggering the \"watch\" command failed)");
	Tcl_BackgroundException(textPtr->interp, TCL_ERROR);
    }
    Tcl_Release((ClientData) textPtr->interp);

    Tcl_DStringFree(&cmd);
    return !TkTextDecrRefCountAndTestIfDestroyed(textPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * GenerateEvent --
 *
 *	Send an event about a new state. This is equivalent to:
 *	   event generate $textWidget <<TYPE>>
 *	for all peers of this text widget.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	May force the text window into existence.
 *
 *----------------------------------------------------------------------
 */

static void
GenerateEvent(
    TkSharedText *sharedTextPtr,
    const char *type)
{
    TkText *textPtr;

    for (textPtr = sharedTextPtr->peers; textPtr; textPtr = textPtr->next) {
	Tk_MakeWindowExist(textPtr->tkwin);
	SendVirtualEvent(textPtr->tkwin, type, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateModifiedFlag --
 *
 *	Updates the modified flag of the text widget.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateModifiedFlag(
    TkSharedText *sharedTextPtr,
    bool flag)
{
    bool oldModifiedFlag = sharedTextPtr->isModified;

    if (flag) {
	sharedTextPtr->isModified = true;
    } else if (sharedTextPtr->undoStack && !sharedTextPtr->userHasSetModifiedFlag) {
	if (sharedTextPtr->insertDeleteUndoTokenCount > 0) {
	    sharedTextPtr->isModified = true;
	} else {
	    unsigned undoDepth = TkTextUndoGetCurrentUndoStackDepth(sharedTextPtr->undoStack);
	    sharedTextPtr->isModified = (undoDepth > 0 && undoDepth == sharedTextPtr->undoLevel);
	}
    }

    if (oldModifiedFlag != sharedTextPtr->isModified) {
	sharedTextPtr->userHasSetModifiedFlag = false;
	GenerateEvent(sharedTextPtr, "Modified");
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextUpdateAlteredFlag --
 *
 *	Updates the "altered" flag of the text widget.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkTextUpdateAlteredFlag(
    TkSharedText *sharedTextPtr)/* Information about text widget. */
{
    bool oldIsAlteredFlag = sharedTextPtr->isAltered;
    bool oldIsIrreversibleFlag = sharedTextPtr->isIrreversible;

    if (sharedTextPtr->undoStack) {
	if (TkTextUndoContentIsIrreversible(sharedTextPtr->undoStack)) {
	    sharedTextPtr->isIrreversible = true;
	}
	if (!sharedTextPtr->isIrreversible) {
	    sharedTextPtr->isAltered = sharedTextPtr->undoTagListCount > 0
		    || sharedTextPtr->undoMarkListCount > 0
		    || TkTextUndoGetCurrentUndoStackDepth(sharedTextPtr->undoStack) > 0;
	}
    } else {
	sharedTextPtr->isIrreversible = true;
    }
    if (sharedTextPtr->isIrreversible) {
	sharedTextPtr->isAltered = true;
    }
    if (oldIsAlteredFlag != sharedTextPtr->isAltered) {
	GenerateEvent(sharedTextPtr, "Altered");
    }
    if (oldIsIrreversibleFlag != sharedTextPtr->isIrreversible) {
	GenerateEvent(sharedTextPtr, "Irreversible");
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextRunAfterSyncCmd --
 *
 *	This function executes the command scheduled by
 *	[.text sync -command $cmd], if any.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Anything may happen, depending on $cmd contents.
 *
 *----------------------------------------------------------------------
 */

void
TkTextRunAfterSyncCmd(
    TkText *textPtr)	/* Information about text widget. */
{
    int code;
    bool error = false;
    Tcl_Obj *afterSyncCmd;

    assert(!TkTextPendingSync(textPtr));

    textPtr->pendingAfterSync = false;
    afterSyncCmd = textPtr->afterSyncCmd;

    if (!afterSyncCmd) {
	return;
    }

    /*
     * We have to expect nested calls, futhermore the receiver might destroy the widget.
     */

    textPtr->afterSyncCmd = NULL;
    textPtr->refCount += 1;

    Tcl_Preserve((ClientData) textPtr->interp);
    if (!(textPtr->flags & DESTROYED)) {
	code = Tcl_EvalObjEx(textPtr->interp, afterSyncCmd, TCL_EVAL_GLOBAL);
	if (code == TCL_ERROR && !error) {
	    Tcl_AddErrorInfo(textPtr->interp, "\n    (text sync)");
	    Tcl_BackgroundError(textPtr->interp);
	    error = true;
	}
    }
    Tcl_DecrRefCount(afterSyncCmd);
    Tcl_Release((ClientData) textPtr->interp);
    TkTextReleaseIfDestroyed(textPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * RunAfterSyncCmd --
 *
 *	This function is called by the event loop and executes the command
 *      scheduled by [.text sync -command $cmd].
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Anything may happen, depending on $cmd contents.
 *
 *----------------------------------------------------------------------
 */

static void
RunAfterSyncCmd(
    ClientData clientData)	/* Information about text widget. */
{
    TkText *textPtr = (TkText *) clientData;

    if (!(textPtr->flags & DESTROYED)) {
	if (TkTextPendingSync(textPtr)) {
	    /* Too late here, the widget is not in sync, so we have to wait. */
	} else {
	    TkTextRunAfterSyncCmd(textPtr);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextGenerateWidgetViewSyncEvent --
 *
 *      Send the <<WidgetViewSync>> event related to the text widget
 *      line metrics asynchronous update.
 *      This is equivalent to:
 *         event generate $textWidget <<WidgetViewSync>> -detail $s
 *      where $s is the sync status: true (when the widget view is in
 *      sync with its internal data) or false (when it is not).
 *
 *	Note that this has to be done in the idle loop, otherwise vwait
 *	will not return.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      If corresponding bindings are present, they will trigger.
 *
 *----------------------------------------------------------------------
 */

static void
FireWidgetViewSyncEvent(
    ClientData clientData)	/* Information about text widget. */
{
    TkText *textPtr = (TkText *) clientData;
    Tcl_Interp *interp;
    bool syncState;

    textPtr->pendingFireEvent = false;

    if (textPtr->flags & DESTROYED) {
	return;
    }

    syncState = !TkTextPendingSync(textPtr);

    if (textPtr->sendSyncEvent && syncState) {
	/*
	 * The user is waiting for sync state 'true', so we must send it.
	 */

	textPtr->prevSyncState = false;
    }

    if (textPtr->prevSyncState == syncState) {
	/*
	 * Do not send "WidgetViewSync" with same sync state as before
	 * (except if we must send it because the user is waiting for it).
	 */

	return;
    }

    if ((textPtr->sendSyncEvent || textPtr->pendingAfterSync) && !syncState) {
	/*
	 * Do not send "WidgetViewSync" with sync state "false" as long as
	 * we have a pending sync command.
	 */

	return;
    }

    if (syncState) {
	textPtr->sendSyncEvent = false;
    }
    textPtr->prevSyncState = syncState;

    interp = textPtr->interp;
    Tcl_Preserve((ClientData) interp);
    SendVirtualEvent(textPtr->tkwin, "WidgetViewSync", Tcl_NewBooleanObj(syncState));
    Tcl_Release((ClientData) interp);
}

void
TkTextGenerateWidgetViewSyncEvent(
    TkText *textPtr,		/* Information about text widget. */
    bool sendImmediately)
{
    if (!textPtr->pendingFireEvent) {
	textPtr->pendingFireEvent = true;
	if (sendImmediately) {
	    FireWidgetViewSyncEvent((ClientData) textPtr);
	} else {
	    Tcl_DoWhenIdle(FireWidgetViewSyncEvent, (ClientData) textPtr);
	}
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextPrintIndex --
 *
 *	This function generates a string description of an index, suitable for
 *	reading in again later.
 *
 * Results:
 *	The characters pointed to by string are modified. Returns the number
 *	of characters in the string.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

/*
 * NOTE: this function has external linkage (declared in a common header file)
 * and cannot be inlined.
 */

int
TkTextPrintIndex(
    const TkText *textPtr,
    const TkTextIndex *indexPtr,/* Pointer to index. */
    char *string)		/* Place to store the position. Must have at least TK_POS_CHARS
    				 * characters. */
{
    assert(textPtr);
    return TkTextIndexPrint(textPtr->sharedTextPtr, textPtr, indexPtr, string);
}

/*
 *----------------------------------------------------------------------
 *
 * SearchPerform --
 *
 *	Overall control of search process. Is given a pattern, a starting
 *	index and an ending index, and attempts to perform a search. This
 *	function is actually completely independent of Tk, and could in the
 *	future be split off.
 *
 * Results:
 *	Standard Tcl result code. In particular, if fromPtr or toPtr are not
 *	considered valid by the 'lineIndexProc', an error will be thrown and
 *	no search performed.
 *
 * Side effects:
 *	See 'SearchCore'.
 *
 *----------------------------------------------------------------------
 */

static int
SearchPerform(
    Tcl_Interp *interp,		/* For error messages. */
    SearchSpec *searchSpecPtr,	/* Search parameters. */
    Tcl_Obj *patObj,		/* Contains an exact string or a regexp
				 * pattern. Must have a refCount > 0. */
    Tcl_Obj *fromPtr,		/* Contains information describing the first index. */
    Tcl_Obj *toPtr)		/* NULL or information describing the last index. */
{
    TkText *textPtr = searchSpecPtr->clientData;

    if (TkTextIsDeadPeer(textPtr)) {
	return TCL_OK;
    }

    /*
     * Find the starting line and starting offset (measured in Unicode chars
     * for regexp search, utf-8 bytes for exact search).
     */

    if (searchSpecPtr->lineIndexProc(interp, fromPtr, searchSpecPtr,
	    &searchSpecPtr->startLine, &searchSpecPtr->startOffset) != TCL_OK) {
	return TCL_ERROR;
    }

    /*
     * Find the optional end location, similarly.
     */

    if (toPtr) {
	TkTextIndex indexTo, indexFrom;

	if (!TkTextGetIndexFromObj(interp, textPtr, toPtr, &indexTo)
		|| !TkTextGetIndexFromObj(interp, textPtr, fromPtr, &indexFrom)) {
	    return TCL_ERROR;
	}

	/*
	 * Check for any empty search range here. It might be better in the
	 * future to embed that in SearchCore (whose default behaviour is to
	 * wrap when given a negative search range).
	 */

	if (TkTextIndexCompare(&indexFrom, &indexTo) == (searchSpecPtr->backwards ? -1 : 1)) {
	    return TCL_OK;
	}

	if (searchSpecPtr->lineIndexProc(interp, toPtr, searchSpecPtr,
		&searchSpecPtr->stopLine, &searchSpecPtr->stopOffset) != TCL_OK) {
	    return TCL_ERROR;
	}
    } else {
	searchSpecPtr->stopLine = -1;
    }

    /*
     * Scan through all of the lines of the text circularly, starting at the
     * given index. 'patObj' is the pattern which may be an exact string or a
     * regexp pattern depending on the flags in searchSpecPtr.
     */

    return SearchCore(interp, searchSpecPtr, patObj);
}

/*
 *----------------------------------------------------------------------
 *
 * SearchCore --
 *
 *	The core of the search function. This function is actually completely
 *	independent of Tk, and could in the future be split off.
 *
 *	The function assumes regexp-based searches operate on Unicode strings,
 *	and exact searches on utf-8 strings. Therefore the 'foundMatchProc'
 *	and 'addLineProc' need to be aware of this distinction.
 *
 * Results:
 *	Standard Tcl result code.
 *
 * Side effects:
 *	Only those of the 'searchSpecPtr->foundMatchProc' which is called
 *	whenever a match is found.
 *
 *	Note that the way matching across multiple lines is implemented, we
 *	start afresh with each line we have available, even though we may
 *	already have examined the contents of that line (and further ones) if
 *	we were attempting a multi-line match using the previous line. This
 *	means there may be ways to speed this up a lot by not throwing away
 *	all the multi-line information one has accumulated. Profiling should
 *	be done to see where the bottlenecks lie before attempting this,
 *	however. We would also need to be very careful such optimisation keep
 *	within the specified search bounds.
 *
 *----------------------------------------------------------------------
 */

static int
SearchCore(
    Tcl_Interp *interp,		/* For error messages. */
    SearchSpec *searchSpecPtr,	/* Search parameters. */
    Tcl_Obj *patObj)		/* Contains an exact string or a regexp
				 * pattern. Must have a refCount > 0. */
{
    /*
     * For exact searches these are utf-8 char* offsets, for regexp searches
     * they are Unicode char offsets.
     */

    int firstOffset, lastOffset, matchOffset, matchLength;
    int passes;
    int lineNum = searchSpecPtr->startLine;
    int code = TCL_OK;
    Tcl_Obj *theLine;
    int alreadySearchOffset = -1;

    const char *pattern = NULL;	/* For exact searches only. */
    int firstNewLine = -1; 	/* For exact searches only. */
    Tcl_RegExp regexp = NULL;	/* For regexp searches only. */

    /*
     * These items are for backward regexp searches only. They are for two
     * purposes: to allow us to report backwards matches in the correct order,
     * even though the implementation uses repeated forward searches; and to
     * provide for overlap checking between backwards matches on different
     * text lines.
     */

#define LOTS_OF_MATCHES 20
    int matchNum = LOTS_OF_MATCHES;
    int32_t smArray[2 * LOTS_OF_MATCHES];
    int32_t *storeMatch = smArray;
    int32_t *storeLength = smArray + LOTS_OF_MATCHES;
    int lastBackwardsLineMatch = -1;
    int lastBackwardsMatchOffset = -1;

    if (searchSpecPtr->exact) {
	/*
	 * Convert the pattern to lower-case if we're supposed to ignore case.
	 */

	if (searchSpecPtr->noCase) {
	    patObj = Tcl_DuplicateObj(patObj);

	    /*
	     * This can change the length of the string behind the object's
	     * back, so ensure it is correctly synchronised.
	     */

	    Tcl_SetObjLength(patObj, Tcl_UtfToLower(Tcl_GetString(patObj)));
	}
    } else {
	/*
	 * Compile the regular expression. We want '^$' to match after and
	 * before \n respectively, so use the TCL_REG_NLANCH flag.
	 */

	regexp = Tcl_GetRegExpFromObj(interp, patObj,
		(searchSpecPtr->noCase ? TCL_REG_NOCASE : 0)
		| (searchSpecPtr->noLineStop ? 0 : TCL_REG_NLSTOP)
		| TCL_REG_ADVANCED | TCL_REG_CANMATCH | TCL_REG_NLANCH);
	if (!regexp) {
	    return TCL_ERROR;
	}
    }

    /*
     * For exact strings, we want to know where the first newline is, and we
     * will also use this as a flag to test whether it is even possible to
     * match the pattern on a single line. If not we will have to search
     * across multiple lines.
     */

    if (searchSpecPtr->exact) {
	const char *nl;

	/*
	 * We only need to set the matchLength once for exact searches, and we
	 * do it here. It is also used below as the actual pattern length, so
	 * it has dual purpose.
	 */

	pattern = Tcl_GetString(patObj);
	matchLength = GetByteLength(patObj);
	nl = strchr(pattern, '\n');

	/*
	 * If there is no newline, or it is the very end of the string, then
	 * we don't need any special treatment, since single-line matching
	 * will work fine.
	 */

	if (nl && nl[1] != '\0') {
	    firstNewLine = (nl - pattern);
	}
    } else {
	matchLength = 0;	/* Only needed to prevent compiler warnings. */
    }

    /*
     * Keep a reference here, so that we can be sure the object doesn't
     * disappear behind our backs and invalidate its contents which we are
     * using.
     */

    Tcl_IncrRefCount(patObj);

    /*
     * For building up the current line being checked.
     */

    theLine = Tcl_NewObj();
    Tcl_IncrRefCount(theLine);

    for (passes = 0; passes < 2; ) {
	ClientData lineInfo;
	int linesSearched = 1;
	int extraLinesSearched = 0;

	if (lineNum >= searchSpecPtr->numLines) {
	    /*
	     * Don't search the dummy last line of the text.
	     */

	    goto nextLine;
	}

	/*
	 * Extract the text from the line, storing its length in 'lastOffset'
	 * (in bytes if exact, chars if regexp), since obviously the length is
	 * the maximum offset at which it is possible to find something on
	 * this line, which is what 'lastOffset' represents.
	 */

	lineInfo = searchSpecPtr->addLineProc(lineNum, searchSpecPtr, theLine,
		&lastOffset, &linesSearched);

	if (!lineInfo) {
	    /*
	     * This should not happen, since 'lineNum' should be valid in the
	     * call above. However, let's try to be flexible and not cause a
	     * crash below.
	     */

	    goto nextLine;
	}

	if (lineNum == searchSpecPtr->stopLine && searchSpecPtr->backwards) {
	    firstOffset = searchSpecPtr->stopOffset;
	} else {
	    firstOffset = 0;
	}

	if (alreadySearchOffset != -1) {
	    if (searchSpecPtr->backwards) {
		if (alreadySearchOffset < lastOffset) {
		    lastOffset = alreadySearchOffset;
		}
	    } else {
		if (alreadySearchOffset > firstOffset) {
		    firstOffset = alreadySearchOffset;
		}
	    }
	    alreadySearchOffset = -1;
	}

	if (lineNum == searchSpecPtr->startLine) {
	    /*
	     * The starting line is tricky: the first time we see it we check
	     * one part of the line, and the second pass through we check the
	     * other part of the line.
	     */

	    passes += 1;
	    if ((passes == 1) ^ searchSpecPtr->backwards) {
		/*
		 * Forward search and first pass, or backward search and
		 * second pass.
		 *
		 * Only use the last part of the line.
		 */

		if (searchSpecPtr->startOffset > firstOffset) {
		    firstOffset = searchSpecPtr->startOffset;
		}
		if (firstOffset >= lastOffset && (lastOffset != 0 || searchSpecPtr->exact)) {
		    goto nextLine;
		}
	    } else {
		/*
		 * Use only the first part of the line.
		 */

		if (searchSpecPtr->startOffset < lastOffset) {
		    lastOffset = searchSpecPtr->startOffset;
		}
	    }
	}

	/*
	 * Check for matches within the current line 'lineNum'. If so, and if
	 * we're searching backwards or for all matches, repeat the search
	 * until we find the last match in the line. The 'lastOffset' is one
	 * beyond the last position in the line at which a match is allowed to
	 * begin.
	 */

	matchOffset = -1;

	if (searchSpecPtr->exact) {
	    int maxExtraLines = 0;
	    const char *startOfLine = Tcl_GetString(theLine);

	    assert(pattern);
	    do {
		int ch;
		const char *p;
		int lastFullLine = lastOffset;

		if (firstNewLine == -1) {
		    if (searchSpecPtr->strictLimits && (firstOffset + matchLength > lastOffset)) {
			/*
			 * Not enough characters to match.
			 */

			break;
		    }

		    /*
		     * Single line matching. We want to scan forwards or
		     * backwards as appropriate.
		     */

		    if (searchSpecPtr->backwards) {
			/*
			 * Search back either from the previous match or from
			 * 'startOfLine + lastOffset - 1' until we find a
			 * match.
			 */

			const char c = pattern[0];

			p = startOfLine;
			if (alreadySearchOffset != -1) {
			    p += alreadySearchOffset;
			    alreadySearchOffset = -1;
			} else {
			    p += lastOffset - 1;
			}
			while (p >= startOfLine + firstOffset) {
			    if (p[0] == c && strncmp(p, pattern, matchLength) == 0) {
				goto backwardsMatch;
			    }
			    p -= 1;
			}
			break;
		    } else {
			p = strstr(startOfLine + firstOffset, pattern);
		    }
		    if (!p) {
			/*
			 * Single line match failed.
			 */

			break;
		    }
		} else if (firstNewLine >= lastOffset - firstOffset) {
		    /*
		     * Multi-line match, but not enough characters to match.
		     */

		    break;
		} else {
		    /*
		     * Multi-line match has only one possible match position,
		     * because we know where the '\n' is.
		     */

		    p = startOfLine + lastOffset - firstNewLine - 1;
		    if (strncmp(p, pattern, firstNewLine + 1) != 0) {
			/*
			 * No match.
			 */

			break;
		    } else {
			int extraLines = 1;

			/*
			 * If we find a match that overlaps more than one
			 * line, we will use this value to determine the first
			 * allowed starting offset for the following search
			 * (to avoid overlapping results).
			 */

			int lastTotal = lastOffset;
			int skipFirst = lastOffset - firstNewLine - 1;

			/*
			 * We may be able to match if given more text. The
			 * following 'while' block handles multi-line exact
			 * searches.
			 */

			while (1) {
			    lastFullLine = lastTotal;

			    if (lineNum + extraLines >= searchSpecPtr->numLines) {
				p = NULL;
				break;
			    }

			    /*
			     * Only add the line if we haven't already done so
			     * already.
			     */

			    if (extraLines > maxExtraLines) {
				if (!searchSpecPtr->addLineProc(lineNum + extraLines, searchSpecPtr,
					theLine, &lastTotal, &extraLines)) {
				    p = NULL;
				    if (!searchSpecPtr->backwards) {
					extraLinesSearched = extraLines;
				    }
				    break;
				}
				maxExtraLines = extraLines;
			    }

			    startOfLine = Tcl_GetString(theLine);
			    p = startOfLine + skipFirst;

			    /*
			     * Use the fact that 'matchLength = patLength' for
			     * exact searches.
			     */

			    if (lastTotal - skipFirst >= matchLength) {
				/*
				 * We now have enough text to match, so we
				 * make a final test and break whatever the
				 * result.
				 */

				if (strncmp(p, pattern, matchLength) != 0) {
				    p = NULL;
				}
				break;
			    } else {
				/*
				 * Not enough text yet, but check the prefix.
				 */

				if (strncmp(p, pattern, lastTotal - skipFirst) != 0) {
				    p = NULL;
				    break;
				}

				/*
				 * The prefix matches, so keep looking.
				 */
			    }
			    extraLines += 1;
			}
			/*
			 * If we reach here, with p != NULL, we've found a
			 * multi-line match, else we started a multi-match but
			 * didn't finish it off, so we go to the next line.
			 */

			if (!p) {
			    break;
			}

			/*
			 * We've found a multi-line match.
			 */

			if (extraLines > 0) {
			    extraLinesSearched = extraLines - 1;
			}
		    }
		}

	    backwardsMatch:
		if (p - startOfLine >= lastOffset) {
		    break;
		}

		/*
		 * Remember the match.
		 */

		matchOffset = p - startOfLine;

		if (searchSpecPtr->all &&
			!searchSpecPtr->foundMatchProc(lineNum, searchSpecPtr,
			lineInfo, theLine, matchOffset, matchLength)) {
		    /*
		     * We reached the end of the search.
		     */

		    goto searchDone;
		}

		if (!searchSpecPtr->overlap) {
		    if (searchSpecPtr->backwards) {
			alreadySearchOffset = p - startOfLine;
			if (firstNewLine != -1) {
			    break;
			} else {
			    alreadySearchOffset -= matchLength;
			}
		    } else {
			firstOffset = p - startOfLine + matchLength;
			if (firstOffset >= lastOffset) {
			    /*
			     * Now, we have to be careful not to find
			     * overlapping matches either on the same or
			     * following lines. Assume that if we did find
			     * something, it goes until the last extra line we
			     * added.
			     *
			     * We can break out of the loop, since we know no
			     * more will be found.
			     */

			    if (!searchSpecPtr->backwards) {
				alreadySearchOffset = firstOffset - lastFullLine;
				break;
			    }
			}
		    }
		} else {
		    if (searchSpecPtr->backwards) {
			alreadySearchOffset = p - startOfLine - 1;
			if (alreadySearchOffset < 0) {
			    break;
			}
		    } else {
			firstOffset = p - startOfLine + TkUtfToUniChar(startOfLine + matchOffset, &ch);
		    }
		}
	    } while (searchSpecPtr->all);
	} else {
	    int maxExtraLines = 0;
	    int matches = 0;
	    int lastNonOverlap = -1;

	    do {
		Tcl_RegExpInfo info;
		int match;
		int lastFullLine = lastOffset;

		match = Tcl_RegExpExecObj(interp, regexp, theLine,
			firstOffset, 1, firstOffset > 0 ? TCL_REG_NOTBOL : 0);
		if (match < 0) {
		    code = TCL_ERROR;
		    goto searchDone;
		}
		Tcl_RegExpGetInfo(regexp, &info);

		/*
		 * If we don't have a match, or if we do, but it extends to
		 * the end of the line, we must try to add more lines to get a
		 * full greedy match.
		 */

		if (!match
			|| (info.extendStart == info.matches[0].start
			    && info.matches[0].end == lastOffset - firstOffset)) {
		    int extraLines = 0;
		    int prevFullLine;

		    /*
		     * If we find a match that overlaps more than one line, we
		     * will use this value to determine the first allowed
		     * starting offset for the following search (to avoid
		     * overlapping results).
		     */

		    int lastTotal = lastOffset;

		    if (lastBackwardsLineMatch != -1 && lastBackwardsLineMatch == lineNum + 1) {
			lastNonOverlap = lastTotal;
		    }

		    if (info.extendStart < 0) {
			/*
			 * No multi-line match is possible.
			 */

			break;
		    }

		    /*
		     * We may be able to match if given more text. The
		     * following 'while' block handles multi-line regexp
		     * searches.
		     */

		    while (1) {
			prevFullLine = lastTotal;

			/*
			 * Move firstOffset to first possible start.
			 */

			if (!match) {
			    firstOffset += info.extendStart;
			}
			if (firstOffset >= lastOffset) {
			    /*
			     * We're being told that the only possible new
			     * match is starting after the end of the line.
			     * But, that is the next line which we will handle
			     * when we look at that line.
			     */

			    if (!match && !searchSpecPtr->backwards && firstOffset == 0) {
				extraLinesSearched = extraLines;
			    }
			    break;
			}

			if (lineNum + extraLines >= searchSpecPtr->numLines) {
			    break;
			}

			/*
			 * Add next line, provided we haven't already done so.
			 */

			if (extraLines > maxExtraLines) {
			    if (!searchSpecPtr->addLineProc(lineNum + extraLines, searchSpecPtr,
				    theLine, &lastTotal, &extraLines)) {
				/*
				 * There are no more acceptable lines, so we
				 * can say we have searched all of these.
				 */

				if (!match && !searchSpecPtr->backwards) {
				    extraLinesSearched = extraLines;
				}
				break;
			    }

			    maxExtraLines = extraLines;
			    if (lastBackwardsLineMatch != -1
				    && lastBackwardsLineMatch == lineNum + extraLines + 1) {
				lastNonOverlap = lastTotal;
			    }
			}

			match = Tcl_RegExpExecObj(interp, regexp, theLine,
				firstOffset, 1, firstOffset > 0 ? TCL_REG_NOTBOL : 0);
			if (match < 0) {
			    code = TCL_ERROR;
			    goto searchDone;
			}
			Tcl_RegExpGetInfo(regexp, &info);

			/*
			 * Unfortunately there are bugs in Tcl's regexp
			 * library, which tells us that info.extendStart is
			 * zero when it should not be (should be -1), which
			 * makes our task a bit more complicated here. We
			 * check if there was a match, and the end of the
			 * match leaves an entire extra line unmatched, then
			 * we stop searching. Clearly it still might sometimes
			 * be possible to add more text and match again, but
			 * Tcl's regexp library doesn't tell us that.
			 *
			 * This means we often add and search one more line
			 * than might be necessary if Tcl were able to give us
			 * a correct value of info.extendStart under all
			 * circumstances.
			 */

			if ((match  && firstOffset + info.matches[0].end != lastTotal
				    && firstOffset + info.matches[0].end < prevFullLine)
				|| info.extendStart < 0) {
			    break;
			}

			/*
			 * If there is a match, but that match starts after
			 * the end of the first line, then we'll handle that
			 * next time around, when we're actually looking at
			 * that line.
			 */

			if (match && info.matches[0].start >= lastOffset) {
			    break;
			}
			if (match && firstOffset + info.matches[0].end >= prevFullLine) {
			    if (extraLines > 0) {
				extraLinesSearched = extraLines - 1;
			    }
			    lastFullLine = prevFullLine;
			}

			/*
			 * The prefix matches, so keep looking.
			 */

			extraLines += 1;
		    }

		    /*
		     * If we reach here with 'match == 1', we've found a
		     * multi-line match, which we will record in the code
		     * which follows directly else we started a multi-line
		     * match but didn't finish it off, so we go to the next
		     * line.
		     */

		    if (!match) {
			/*
			 * Here is where we could perform an optimisation,
			 * since we have already retrieved the contents of the
			 * next line (perhaps many more), so we shouldn't
			 * really throw it all away and start again. This
			 * could be particularly important for complex regexp
			 * searches.
			 *
			 * This 'break' will take us to just before the
			 * 'nextLine:' below.
			 */

			break;
		    }

		    if (lastBackwardsLineMatch != -1) {
			if (lineNum + linesSearched + extraLinesSearched == lastBackwardsLineMatch) {
			    /*
			     * Possible overlap or inclusion.
			     */

			    int thisOffset = firstOffset + info.matches[0].end - info.matches[0].start;

			    if (lastNonOverlap != -1) {
				/*
				 * Possible overlap or enclosure.
				 */

				if (thisOffset - lastNonOverlap >=
					lastBackwardsMatchOffset + matchLength) {
				    /*
				     * Totally encloses previous match, so
				     * forget the previous match.
				     */

				    lastBackwardsLineMatch = -1;
				} else if (thisOffset - lastNonOverlap > lastBackwardsMatchOffset) {
				    /*
				     * Overlap. Previous match is ok, and the
				     * current match is only ok if we are
				     * searching with -overlap.
				     */

				    if (searchSpecPtr->overlap) {
					goto recordBackwardsMatch;
				    } else {
					match = 0;
					break;
				    }
				} else {
				    /*
				     * No overlap, although the same line was
				     * reached.
				     */

				    goto recordBackwardsMatch;
				}
			    } else {
				/*
				 * No overlap.
				 */

				goto recordBackwardsMatch;
			    }
			} else if (lineNum + linesSearched + extraLinesSearched
				< lastBackwardsLineMatch) {
			    /*
			     * No overlap.
			     */

			    goto recordBackwardsMatch;
			} else {
			    /*
			     * Totally enclosed.
			     */

			    lastBackwardsLineMatch = -1;
			}
		    }

		} else {
		    /*
		     * Matched in a single line.
		     */

		    if (lastBackwardsLineMatch != -1) {
		    recordBackwardsMatch:
			searchSpecPtr->foundMatchProc(lastBackwardsLineMatch,
				searchSpecPtr, NULL, NULL, lastBackwardsMatchOffset, matchLength);
			lastBackwardsLineMatch = -1;
			if (!searchSpecPtr->all) {
			    goto searchDone;
			}
		    }
		}

		firstOffset += info.matches[0].start;
		if (firstOffset >= lastOffset) {
		    break;
		}

		/*
		 * Update our local variables with the match, if we haven't
		 * yet found anything, or if we're doing '-all' or
		 * '-backwards' _and_ this match isn't fully enclosed in the
		 * previous match.
		 */

		if (matchOffset == -1 ||
			((searchSpecPtr->all || searchSpecPtr->backwards)
			    && (firstOffset < matchOffset
				|| firstOffset + info.matches[0].end - info.matches[0].start
				    > matchOffset + matchLength))) {

		    matchOffset = firstOffset;
		    matchLength = info.matches[0].end - info.matches[0].start;

		    if (searchSpecPtr->backwards) {
			/*
			 * To get backwards searches in the correct order, we
			 * must store them away here.
			 */

			if (matches == matchNum) {
			    /*
			     * We've run out of space in our normal store, so
			     * we must allocate space for these backwards
			     * matches on the heap.
			     */

			    int matchNumSize = matchNum * sizeof(int32_t);
			    int32_t *newArray = malloc(4*matchNumSize);
			    memcpy(newArray, storeMatch, matchNumSize);
			    memcpy(newArray + 2*matchNum, storeLength, matchNumSize);
			    if (storeMatch != smArray) {
				free((char *) storeMatch);
			    }
			    matchNum *= 2;
			    storeMatch = newArray;
			    storeLength = newArray + matchNum;
			}
			storeMatch[matches] = matchOffset;
			storeLength[matches] = matchLength;
			matches += 1;
		    } else {
			/*
			 * Now actually record the match, but only if we are
			 * doing an '-all' search.
			 */

			if (searchSpecPtr->all &&
				!searchSpecPtr->foundMatchProc(lineNum,
				    searchSpecPtr, lineInfo, theLine, matchOffset, matchLength)) {
			    /*
			     * We reached the end of the search.
			     */

			    goto searchDone;
			}
		    }

		    /*
		     * For forward matches, unless we allow overlaps, we move
		     * this on by the length of the current match so that we
		     * explicitly disallow overlapping matches.
		     */

		    if (matchLength > 0 && !searchSpecPtr->overlap && !searchSpecPtr->backwards) {
			firstOffset += matchLength;
			if (firstOffset >= lastOffset) {
			    /*
			     * Now, we have to be careful not to find
			     * overlapping matches either on the same or
			     * following lines. Assume that if we did find
			     * something, it goes until the last extra line we
			     * added.
			     *
			     * We can break out of the loop, since we know no
			     * more will be found.
			     */

			    alreadySearchOffset = firstOffset - lastFullLine;
			    break;
			}

			/*
			 * We'll add this on again just below.
			 */

			firstOffset -= 1;
		    }
		}

		/*
		 * Move the starting point on, in case we are doing repeated
		 * or backwards searches (for the latter, we actually do
		 * repeated forward searches).
		 */

		firstOffset += 1;
	    } while (searchSpecPtr->backwards || searchSpecPtr->all);

	    if (matches > 0) {
		/*
		 * Now we have all the matches in our array, but not stored
		 * with 'foundMatchProc' yet.
		 */

		matches -= 1;
		matchOffset = storeMatch[matches];
		matchLength = storeLength[matches];
		while (--matches >= 0) {
		    if (lineNum == searchSpecPtr->stopLine) {
			/*
			 * It appears as if a condition like:
			 *
			 * if (storeMatch[matches]<searchSpecPtr->stopOffset)
			 *	break;
			 *
			 * might be needed here, but no test case has been
			 * found which would exercise such a problem.
			 */
		    }
		    if (storeMatch[matches] + storeLength[matches] >= matchOffset + matchLength) {
			/*
			 * The new match totally encloses the previous one, so
			 * we overwrite the previous one.
			 */

			matchOffset = storeMatch[matches];
			matchLength = storeLength[matches];
			continue;
		    }
		    if (!searchSpecPtr->overlap) {
			if (storeMatch[matches] + storeLength[matches] > matchOffset) {
			    continue;
			}
		    }
		    searchSpecPtr->foundMatchProc(lineNum, searchSpecPtr,
			    lineInfo, theLine, matchOffset, matchLength);
		    if (!searchSpecPtr->all) {
			goto searchDone;
		    }
		    matchOffset = storeMatch[matches];
		    matchLength = storeLength[matches];
		}
		if (searchSpecPtr->all && matches > 0) {
		    /*
		     * We only need to do this for the '-all' case, because
		     * just below we will call the foundMatchProc for the
		     * non-all case.
		     */

		    searchSpecPtr->foundMatchProc(lineNum, searchSpecPtr,
			    lineInfo, theLine, matchOffset, matchLength);
		} else {
		    lastBackwardsLineMatch = lineNum;
		    lastBackwardsMatchOffset = matchOffset;
		}
	    }
	}

	/*
	 * If the 'all' flag is set, we will already have stored all matches,
	 * so we just proceed to the next line.
	 *
	 * If not, and there is a match we need to store that information and
	 * we are done.
	 */

	if (lastBackwardsLineMatch == -1 && matchOffset >= 0 && !searchSpecPtr->all) {
	    searchSpecPtr->foundMatchProc(lineNum, searchSpecPtr, lineInfo,
		    theLine, matchOffset, matchLength);
	    goto searchDone;
	}

	/*
	 * Go to the next (or previous) line;
	 */

    nextLine:
	linesSearched += extraLinesSearched;

	while (linesSearched-- > 0) {
	    /*
	     * If we have just completed the 'stopLine', we are done.
	     */

	    if (lineNum == searchSpecPtr->stopLine) {
		goto searchDone;
	    }

	    if (searchSpecPtr->backwards) {
		lineNum -= 1;

		if (lastBackwardsLineMatch != -1
			&& (lineNum < 0 || lineNum + 2 < lastBackwardsLineMatch)) {
		    searchSpecPtr->foundMatchProc(lastBackwardsLineMatch,
			    searchSpecPtr, NULL, NULL, lastBackwardsMatchOffset, matchLength);
		    lastBackwardsLineMatch = -1;
		    if (!searchSpecPtr->all) {
			goto searchDone;
		    }
		}

		if (lineNum < 0) {
		    lineNum = searchSpecPtr->numLines - 1;
		}
		if (!searchSpecPtr->exact) {
		    /*
		     * The 'exact' search loops above are designed to give us
		     * an accurate picture of the number of lines which we can
		     * skip here. For 'regexp' searches, on the other hand,
		     * which can match potentially variable lengths, we cannot
		     * skip multiple lines when searching backwards. Therefore
		     * we only allow one line to be skipped here.
		     */

		    break;
		}
	    } else {
		lineNum += 1;
		if (lineNum >= searchSpecPtr->numLines) {
		    lineNum = 0;
		}
	    }
	    if (lineNum == searchSpecPtr->startLine && linesSearched > 0) {
		/*
		 * We've just searched all the way round and have gone right
		 * through the start line without finding anything in the last
		 * attempt.
		 */

		break;
	    }
	}

	Tcl_SetObjLength(theLine, 0);
    }
  searchDone:

    if (lastBackwardsLineMatch != -1) {
	searchSpecPtr->foundMatchProc(lastBackwardsLineMatch, searchSpecPtr,
		NULL, NULL, lastBackwardsMatchOffset, matchLength);
    }

    /*
     * Free up the cached line and pattern.
     */

    Tcl_DecrRefCount(theLine);
    Tcl_DecrRefCount(patObj);

    /*
     * Free up any extra space we allocated.
     */

    if (storeMatch != smArray) {
	free((char *) storeMatch);
    }

    return code;
}

/*
 *----------------------------------------------------------------------
 *
 * GetTextStartEnd -
 *
 *	Converts an internal TkTextSegment ptr into a Tcl string obj containing
 *	the representation of the index. (Handler for the 'startEndMark' configuration
 *	option type.)
 *
 * Results:
 *	Tcl_Obj containing the string representation of the index position.
 *
 * Side effects:
 *	Creates a new Tcl_Obj.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *
GetTextStartEnd(
    ClientData clientData,
    Tk_Window tkwin,
    char *recordPtr,		/* Pointer to widget record. */
    int internalOffset)		/* Offset within *recordPtr containing the start object. */
{
    TkTextIndex index;
    char buf[TK_POS_CHARS] = { '\0' };
    const TkText *textPtr = (const TkText *) recordPtr;
    const TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    Tcl_Obj **objPtr = (Tcl_Obj **) (recordPtr + internalOffset);
    const TkTextSegment *sharedMarker;
    TkTextSegment *marker;

    if (objPtr == &textPtr->newStartIndex) {
	marker = textPtr->startMarker;
	sharedMarker = sharedTextPtr->startMarker;
    } else {
	marker = textPtr->endMarker;
	sharedMarker = sharedTextPtr->endMarker;
    }
    if (marker != sharedMarker) {
	TkTextIndexClear2(&index, NULL, sharedTextPtr->tree);
	TkTextIndexSetSegment(&index, marker);
	TkTextIndexPrint(sharedTextPtr, NULL, &index, buf);
    }
    return Tcl_NewStringObj(buf, -1);
}

/*
 *----------------------------------------------------------------------
 *
 * SetTextStartEnd --
 *
 *	Converts a Tcl_Obj representing a widget's (start or end) index into a
 *	TkTextSegment* value. (Handler for the 'startEndMark' configuration option type.)
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	May store the TkTextSegment* value into the internal representation
 *	pointer. May change the pointer to the Tcl_Obj to NULL to indicate
 *	that the specified string was empty and that is acceptable.
 *
 *----------------------------------------------------------------------
 */

static int
ObjectIsEmpty(
    Tcl_Obj *objPtr)		/* Object to test. May be NULL. */
{
    return objPtr ? GetByteLength(objPtr) == 0 : true;
}

static int
SetTextStartEnd(
    ClientData clientData,
    Tcl_Interp *interp,		/* Current interp; may be used for errors. */
    Tk_Window tkwin,		/* Window for which option is being set. */
    Tcl_Obj **value,		/* Pointer to the pointer to the value object.
				 * We use a pointer to the pointer because we
				 * may need to return a value (NULL). */
    char *recordPtr,		/* Pointer to storage for the widget record. */
    int internalOffset,		/* Offset within *recordPtr at which the
				 * internal value is to be stored. */
    char *oldInternalPtr,	/* Pointer to storage for the old value. */
    int flags)			/* Flags for the option, set Tk_SetOptions. */
{
    Tcl_Obj **objPtr = (Tcl_Obj **) (recordPtr + internalOffset);
    Tcl_Obj **oldObjPtr = (Tcl_Obj **) oldInternalPtr;
    const TkText *textPtr = (const TkText *) recordPtr;

    assert(!*objPtr);
    *oldObjPtr = NULL;

    if ((flags & TK_OPTION_NULL_OK) && ObjectIsEmpty(*value)) {
	*value = NULL;
	*objPtr = Tcl_NewStringObj((objPtr == &textPtr->newStartIndex) ? "begin" : "end", -1);
    } else {
	*objPtr = *value;
    }
    Tcl_IncrRefCount(*objPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RestoreTextStartEnd --
 *
 *	Restore an index option value from a saved value. (Handler for the
 *	'index' configuration option type.)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Restores the old value.
 *
 *----------------------------------------------------------------------
 */

static void
RestoreTextStartEnd(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr,		/* Pointer to storage for value. */
    char *oldInternalPtr)	/* Pointer to old value. */
{
    Tcl_Obj **newValue = (Tcl_Obj **) internalPtr;
    Tcl_Obj **oldValue = (Tcl_Obj **) oldInternalPtr;

    if (*oldValue) {
	Tcl_IncrRefCount(*oldValue);
    }
    *newValue = *oldValue;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeTextStartEnd --
 *
 *	Free an index option value from a saved value. (Handler for the
 *	'index' configuration option type.)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Releases some memory.
 *
 *----------------------------------------------------------------------
 */

static void
FreeTextStartEnd(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr)
{
    Tcl_Obj *objPtr = *(Tcl_Obj **) internalPtr;

    if (objPtr) {
	Tcl_DecrRefCount(objPtr);
    }
}

#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE
/*
 *----------------------------------------------------------------------
 *
 * GetLineStartEnd -
 *
 *	Converts an internal TkTextLine ptr into a Tcl string obj containing
 *	the line number. (Handler for the 'line' configuration option type.)
 *
 * Results:
 *	Tcl_Obj containing the string representation of the line value.
 *
 * Side effects:
 *	Creates a new Tcl_Obj.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *
GetLineStartEnd(
    ClientData clientData,
    Tk_Window tkwin,
    char *recordPtr,		/* Pointer to widget record. */
    int internalOffset)		/* Offset within *recordPtr containing the line value. */
{
    TkText *textPtr;
    TkTextLine *linePtr = *(TkTextLine **)(recordPtr + internalOffset);

    if (!linePtr) {
	return Tcl_NewObj();
    }
    textPtr = (TkText *) recordPtr;
    return Tcl_NewIntObj(1 + TkBTreeLinesTo(textPtr->sharedTextPtr->tree, NULL, linePtr, NULL));
}

/*
 *----------------------------------------------------------------------
 *
 * SetLineStartEnd --
 *
 *	Converts a Tcl_Obj representing a widget's (start or end) line into a
 *	TkTextLine* value. (Handler for the 'line' configuration option type.)
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	May store the TkTextLine* value into the internal representation
 *	pointer. May change the pointer to the Tcl_Obj to NULL to indicate
 *	that the specified string was empty and that is acceptable.
 *
 *----------------------------------------------------------------------
 */

static int
SetLineStartEnd(
    ClientData clientData,
    Tcl_Interp *interp,		/* Current interp; may be used for errors. */
    Tk_Window tkwin,		/* Window for which option is being set. */
    Tcl_Obj **value,		/* Pointer to the pointer to the value object.
				 * We use a pointer to the pointer because we
				 * may need to return a value (NULL). */
    char *recordPtr,		/* Pointer to storage for the widget record. */
    int internalOffset,		/* Offset within *recordPtr at which the
				 * internal value is to be stored. */
    char *oldInternalPtr,	/* Pointer to storage for the old value. */
    int flags)			/* Flags for the option, set Tk_SetOptions. */
{
    TkTextLine *linePtr = NULL;
    char *internalPtr;
    TkText *textPtr = (TkText *) recordPtr;

    internalPtr = internalOffset >= 0 ? recordPtr + internalOffset : NULL;

    if ((flags & TK_OPTION_NULL_OK) && ObjectIsEmpty(*value)) {
	*value = NULL;
    } else {
	int line;

	if (Tcl_GetIntFromObj(interp, *value, &line) != TCL_OK) {
	    return TCL_ERROR;
	}
	linePtr = TkBTreeFindLine(textPtr->sharedTextPtr->tree, NULL, line - 1);
    }

    if (internalPtr) {
	*((TkTextLine **) oldInternalPtr) = *((TkTextLine **) internalPtr);
	*((TkTextLine **) internalPtr) = linePtr;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RestoreLineStartEnd --
 *
 *	Restore a line option value from a saved value. (Handler for the
 *	'line' configuration option type.)
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Restores the old value.
 *
 *----------------------------------------------------------------------
 */

static void
RestoreLineStartEnd(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr,		/* Pointer to storage for value. */
    char *oldInternalPtr)	/* Pointer to old value. */
{
    *(TkTextLine **) internalPtr = *(TkTextLine **) oldInternalPtr;
}

#endif /* SUPPORT_DEPRECATED_STARTLINE_ENDLINE */

/*
 *----------------------------------------------------------------------
 *
 * TkpTesttextCmd --
 *
 *	This function implements the "testtext" command. It provides a set of
 *	functions for testing text widgets and the associated functions in
 *	tkText*.c.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Depends on option; see below.
 *
 *----------------------------------------------------------------------
 */

#if TK_MAJOR_VERSION > 8 || (TK_MAJOR_VERSION == 8 && TK_MINOR_VERSION > 5)

int
TkpTesttextCmd(
    ClientData clientData,	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument strings. */
{
    TkText *textPtr;
    size_t len;
    int lineIndex, byteIndex, byteOffset;
    TkTextIndex index, insIndex;
    char buf[TK_POS_CHARS];
    Tcl_CmdInfo info;
    Tcl_Obj *watchCmd;

    if (objc < 3) {
	return TCL_ERROR;
    }

    if (Tcl_GetCommandInfo(interp, Tcl_GetString(objv[1]), &info) == 0) {
	return TCL_ERROR;
    }
    textPtr = info.objClientData;
    len = strlen(Tcl_GetString(objv[2]));
    if (strncmp(Tcl_GetString(objv[2]), "byteindex", len) == 0) {
	if (objc != 5) {
	    return TCL_ERROR;
	}
	lineIndex = atoi(Tcl_GetString(objv[3])) - 1;
	byteIndex = atoi(Tcl_GetString(objv[4]));

	TkTextMakeByteIndex(textPtr->sharedTextPtr->tree, textPtr, lineIndex, byteIndex, &index);
    } else if (strncmp(Tcl_GetString(objv[2]), "forwbytes", len) == 0) {
	if (objc != 5) {
	    return TCL_ERROR;
	}
	if (TkTextGetIndex(interp, textPtr, Tcl_GetString(objv[3]), &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	byteOffset = atoi(Tcl_GetString(objv[4]));
	TkTextIndexForwBytes(textPtr, &index, byteOffset, &index);
    } else if (strncmp(Tcl_GetString(objv[2]), "backbytes", len) == 0) {
	if (objc != 5) {
	    return TCL_ERROR;
	}
	if (TkTextGetIndex(interp, textPtr, Tcl_GetString(objv[3]), &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	byteOffset = atoi(Tcl_GetString(objv[4]));
	TkTextIndexBackBytes(textPtr, &index, byteOffset, &index);
    } else {
	return TCL_ERROR;
    }

    /*
     * Avoid triggering of the "watch" command.
     */

    watchCmd = textPtr->watchCmd;
    textPtr->watchCmd = NULL;
    insIndex = index; /* because TkTextSetMark may modify position */
    TkTextSetMark(textPtr, "insert", &insIndex);
    textPtr->watchCmd = watchCmd;

    TkTextPrintIndex(textPtr, &index, buf);
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s %d", buf, TkTextIndexGetByteIndex(&index)));
    return TCL_OK;
}

#else /* backport to Tk 8.5 */

int
TkpTesttextCmd(
    ClientData clientData,	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int argc,			/* Number of arguments. */
    const char **argv)		/* Argument strings. */
{
    TkText *textPtr;
    size_t len;
    int lineIndex, byteIndex, byteOffset;
    TkTextIndex index;
    char buf[64];
    unsigned offs;
    Tcl_CmdInfo info;

    if (argc < 3) {
	return TCL_ERROR;
    }

    if (Tcl_GetCommandInfo(interp, argv[1], &info) == 0) {
	return TCL_ERROR;
    }
    if (info.isNativeObjectProc) {
	textPtr = (TkText *) info.objClientData;
    } else {
	textPtr = (TkText *) info.clientData;
    }
    len = strlen(argv[2]);
    if (strncmp(argv[2], "byteindex", len) == 0) {
	if (argc != 5) {
	    return TCL_ERROR;
	}
	lineIndex = atoi(argv[3]) - 1;
	byteIndex = atoi(argv[4]);

	TkTextMakeByteIndex(textPtr->sharedTextPtr->tree, textPtr, lineIndex, byteIndex, &index);
    } else if (strncmp(argv[2], "forwbytes", len) == 0) {
	if (argc != 5) {
	    return TCL_ERROR;
	}
	if (TkTextGetIndex(interp, textPtr, argv[3], &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	byteOffset = atoi(argv[4]);
	TkTextIndexForwBytes(textPtr, &index, byteOffset, &index);
    } else if (strncmp(argv[2], "backbytes", len) == 0) {
	if (argc != 5) {
	    return TCL_ERROR;
	}
	if (TkTextGetIndex(interp, textPtr, argv[3], &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	byteOffset = atoi(argv[4]);
	TkTextIndexBackBytes(textPtr, &index, byteOffset, &index);
    } else {
	return TCL_ERROR;
    }

    TkTextSetMark(textPtr, "insert", &index);
    TkTextPrintIndex(textPtr, &index, buf);
    offs = strlen(buf);
    snprintf(buf + offs, sizeof(buf) - offs, " %d", TkTextIndexGetByteIndex(&index));
    Tcl_AppendResult(interp, buf, NULL);

    return TCL_OK;
}

#endif /* TCL_MAJOR_VERSION > 8 || TCL_MINOR_VERSION > 5 */

#ifndef NDEBUG
/*
 *----------------------------------------------------------------------
 *
 * TkpTextInspect --
 *
 *	This function is for debugging only, printing the text content
 *	on stdout.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkpTextInspect(
    TkText *textPtr)
{
    Tcl_Obj *resultPtr;
    Tcl_Obj *objv[9];
    Tcl_Obj **argv;
    int argc, i;

    Tcl_IncrRefCount(resultPtr = Tcl_GetObjResult(textPtr->interp));
    Tcl_ResetResult(textPtr->interp);
    Tcl_IncrRefCount(objv[0] = Tcl_NewStringObj(Tk_PathName(textPtr->tkwin), -1));
    Tcl_IncrRefCount(objv[1] = Tcl_NewStringObj("inspect", -1));
    Tcl_IncrRefCount(objv[2] = Tcl_NewStringObj("-discardselection", -1));
    Tcl_IncrRefCount(objv[3] = Tcl_NewStringObj("-elide", -1));
    Tcl_IncrRefCount(objv[4] = Tcl_NewStringObj("-chars", -1));
    Tcl_IncrRefCount(objv[5] = Tcl_NewStringObj("-image", -1));
    Tcl_IncrRefCount(objv[6] = Tcl_NewStringObj("-window", -1));
    Tcl_IncrRefCount(objv[7] = Tcl_NewStringObj("-mark", -1));
    Tcl_IncrRefCount(objv[8] = Tcl_NewStringObj("-tag", -1));
    TextInspectCmd(textPtr, textPtr->interp, sizeof(objv)/sizeof(objv[0]), objv);
    for (i = 0; i < (int) (sizeof(objv)/sizeof(objv[0])); ++i) {
	Tcl_DecrRefCount(objv[i]);
    }
    Tcl_ListObjGetElements(textPtr->interp, Tcl_GetObjResult(textPtr->interp), &argc, &argv);
    for (i = 0; i < argc; ++i) {
	printf("%s\n", Tcl_GetString(argv[i]));
    }
    Tcl_SetObjResult(textPtr->interp, resultPtr);
    Tcl_DecrRefCount(resultPtr);
}

#endif /* NDEBUG */

/*
 *----------------------------------------------------------------------
 *
 * TkpTextDump --
 *
 *	This function is for debugging only, printing the text content
 *	on stdout.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
#ifndef NDEBUG

void
TkpTextDump(
    TkText *textPtr)
{
    Tcl_Obj *resultPtr;
    Tcl_Obj *objv[4];
    Tcl_Obj **argv;
    int argc, i;

    Tcl_IncrRefCount(resultPtr = Tcl_GetObjResult(textPtr->interp));
    Tcl_ResetResult(textPtr->interp);

    Tcl_IncrRefCount(objv[0] = Tcl_NewStringObj(Tk_PathName(textPtr->tkwin), -1));
    Tcl_IncrRefCount(objv[1] = Tcl_NewStringObj("dump", -1));
    Tcl_IncrRefCount(objv[2] = Tcl_NewStringObj("begin", -1));
    Tcl_IncrRefCount(objv[3] = Tcl_NewStringObj("end", -1));
    TextDumpCmd(textPtr, textPtr->interp, sizeof(objv)/sizeof(objv[0]), objv);
    for (i = 0; i < (int) (sizeof(objv)/sizeof(objv[0])); ++i) {
	Tcl_DecrRefCount(objv[i]);
    }

    Tcl_ListObjGetElements(textPtr->interp, Tcl_GetObjResult(textPtr->interp), &argc, &argv);
    for (i = 0; i < argc; i += 3) {
	char const *type = Tcl_GetString(argv[i]);
	char const *text = Tcl_GetString(argv[i + 1]);
	char const *indx = Tcl_GetString(argv[i + 2]);

	printf("%s ", indx);
	printf("%s ", type);

	if (strcmp(type, "text") == 0) {
	    int len = strlen(text), i;

	    printf("\"");
	    for (i = 0; i < len; ++i) {
		char c = text[i];

		switch (c) {
		case '\t': printf("\\t"); break;
		case '\n': printf("\\n"); break;
		case '\v': printf("\\v"); break;
		case '\f': printf("\\f"); break;
		case '\r': printf("\\r"); break;

		default:
		    if (UCHAR(c) < 0x80 && isprint(c)) {
			printf("%c", c);
		    } else {
			printf("\\x%02u", (unsigned) UCHAR(c));
		    }
		    break;
		}
	    }
	    printf("\"\n");
	} else if (strcmp(type, "mark") == 0) {
	    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(&textPtr->sharedTextPtr->markTable, text);
	    const TkTextSegment *markPtr = NULL;

	    if (hPtr) {
		markPtr = Tcl_GetHashValue(hPtr);
	    } else {
		if (strcmp(text, "insert") == 0)  { markPtr = textPtr->insertMarkPtr; }
		if (strcmp(text, "current") == 0) { markPtr = textPtr->currentMarkPtr; }
	    }
	    if (markPtr) {
		printf("%s (%s)\n", text, markPtr->typePtr == &tkTextLeftMarkType ? "left" : "right");
	    }
	} else {
	    printf("%s\n", text);
	}
    }

    Tcl_SetObjResult(textPtr->interp, resultPtr);
    Tcl_DecrRefCount(resultPtr);
}

#endif /* NDEBUG */


#ifdef TK_C99_INLINE_SUPPORT
/* Additionally we need stand-alone object code. */
extern TkSharedText *	TkBTreeGetShared(TkTextBTree tree);
extern int		TkBTreeGetNumberOfDisplayLines(const TkTextPixelInfo *pixelInfo);
extern TkTextPixelInfo *TkBTreeLinePixelInfo(const TkText *textPtr, TkTextLine *linePtr);
extern unsigned		TkBTreeEpoch(TkTextBTree tree);
extern unsigned		TkBTreeIncrEpoch(TkTextBTree tree);
extern struct Node	*TkBTreeGetRoot(TkTextBTree tree);
extern TkTextLine *	TkBTreePrevLogicalLine(const TkSharedText *sharedTextPtr,
			    const TkText *textPtr, TkTextLine *linePtr);
extern TkTextTag *	TkBTreeGetTags(const TkTextIndex *indexPtr);
extern TkTextLine *	TkBTreeGetStartLine(const TkText *textPtr);
extern TkTextLine *	TkBTreeGetLastLine(const TkText *textPtr);
extern TkTextLine *	TkBTreeNextLine(const TkText *textPtr, TkTextLine *linePtr);
extern TkTextLine *	TkBTreePrevLine(const TkText *textPtr, TkTextLine *linePtr);
extern unsigned		TkBTreeCountLines(const TkTextBTree tree, const TkTextLine *linePtr1,
			    const TkTextLine *linePtr2);
extern bool		TkTextIsDeadPeer(const TkText *textPtr);
extern bool		TkTextIsStartEndMarker(const TkTextSegment *segPtr);
extern bool		TkTextIsSpecialMark(const TkTextSegment *segPtr);
extern bool		TkTextIsPrivateMark(const TkTextSegment *segPtr);
extern bool		TkTextIsSpecialOrPrivateMark(const TkTextSegment *segPtr);
extern bool		TkTextIsNormalOrSpecialMark(const TkTextSegment *segPtr);
extern bool		TkTextIsNormalMark(const TkTextSegment *segPtr);
extern bool		TkTextIsStableMark(const TkTextSegment *segPtr);
extern void		TkTextIndexSetEpoch(TkTextIndex *indexPtr, unsigned epoch);
extern void		TkTextIndexUpdateEpoch(TkTextIndex *indexPtr, unsigned epoch);
extern void		TkTextIndexSetPeer(TkTextIndex *indexPtr, TkText *textPtr);
extern void		TkTextIndexSetToLastChar2(TkTextIndex *indexPtr, TkTextLine *linePtr);
extern void		TkTextIndexInvalidate(TkTextIndex *indexPtr);
extern TkTextLine *	TkTextIndexGetLine(const TkTextIndex *indexPtr);
extern TkTextSegment *	TkTextIndexGetSegment(const TkTextIndex *indexPtr);
extern TkSharedText *	TkTextIndexGetShared(const TkTextIndex *indexPtr);
extern bool		TkTextIndexSameLines(const TkTextIndex *indexPtr1, const TkTextIndex *indexPtr2);
extern void		TkTextIndexSave(TkTextIndex *indexPtr);
# if TK_MAJOR_VERSION == 8 && TK_MINOR_VERSION < 7 && TCL_UTF_MAX <= 4
extern int		TkUtfToUniChar(const char *src, int *chPtr);
# endif
#endif /* __STDC_VERSION__ >= 199901L */


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 105
 * End:
 * vi:set ts=8 sw=4:
 */
