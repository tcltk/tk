/*
 * tkText.c --
 *
 *	This module provides a big chunk of the implementation of multi-line
 *	editable text widgets for Tk. Among other things, it provides the Tcl
 *	command interfaces to text widgets. The B-tree representation of text
 *	and its actual display are implemented elsewhere.
 *
 * Copyright © 1992-1994 The Regents of the University of California.
 * Copyright © 1994-1996 Sun Microsystems, Inc.
 * Copyright © 1999 Scriptics Corporation.
 * Copyright © 2015-2018 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkText.h"
#include "tkTextUndo.h"
#include "tkTextTagSet.h"
#include "tkBitField.h"
#if TCL_MAJOR_VERSION > 8 || (TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION >= 7)
#include "tkFont.h"
#endif
#include <stdlib.h>
#include <assert.h>
#include "default.h"

/* needed for strncasecmp */
#if defined(_WIN32) && !defined(__GNUC__)
# define strncasecmp _strnicmp
#else
# include <strings.h>
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

#if 0
# define FORCE_DISPLAY(winPtr) TkpDisplayWindow(winPtr)
#else
# define FORCE_DISPLAY(winPtr)
#endif

/*
 * For compatibility with Tk 4.0 through 8.4.x, we allow tabs to be
 * mis-specified with non-increasing values. These are converted into tabs
 * which are the equivalent of at least a character width apart.
 */

#if TK_MAJOR_VERSION < 9
# define _TK_ALLOW_DECREASING_TABS
#endif

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

static const char *const stateStrings[] = {
    "disabled", "normal", "readonly", NULL
};

/*
 * The 'TkTextTagging' enum in tkText.h is used to define a type for the -tagging
 * option of the Text widget. These values are used as indices into the string table below.
 */

static const char *const taggingStrings[] = {
    "within", "gravity", "none", NULL
};

/*
 * The 'TkTextJustify' enum in tkText.h is used to define a type for the -justify option of
 * the Text widget. These values are used as indices into the string table below.
 */

static const char *const justifyStrings[] = {
    "left", "right", "full", "center", NULL
};

/*
 * The 'TkWrapMode' enum in tkText.h is used to define a type for the -wrap
 * option of the Text widget. These values are used as indices into the string
 * table below.
 */

const char *const tkTextWrapStrings[] = {
    "char", "none", "word", "codepoint", NULL
};

/*
 * The 'TkSpacing' enum in tkText.h is used to define a type for the -spacing
 * option of the Text widget. These values are used as indices into the string
 * table below.
 */

static const char *const spaceModeStrings[] = {
    "none", "exact", "trim", NULL
};

/*
 * The 'TkTextTabStyle' enum in tkText.h is used to define a type for the
 * -tabstyle option of the Text widget. These values are used as indices into
 * the string table below.
 */

const char *const tkTextTabStyleStrings[] = {
    "tabular", "wordprocessor", NULL
};

/*
 * The 'TkTextInsertUnfocussed' enum in tkText.h is used to define a type for
 * the -insertunfocussed option of the Text widget. These values are used as
 * indice into the string table below.
 */

static const char *const insertUnfocussedStrings[] = {
    "hollow", "none", "solid", NULL
};

/*
 * The 'TkTextHyphenRule' enum in tkText.h is used to define a type for the
 * -hyphenrules option of the Text widget. These values are used for applying
 * hyphen rules to soft hyphens.
 *
 * NOTE: Don't forget to update function ParseHyphens() if this array will be
 * modified.
 */

static const char *const hyphenRuleStrings[] = {
    "ck", "doubledigraph", "doublevowel", "gemination", "repeathyphen", "trema",
    "tripleconsonant" /* don't append a trailing NULL */
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

static int		SetLineStartEnd(void *clientData, Tcl_Interp *interp, Tk_Window tkwin,
			    Tcl_Obj **value, char *recordPtr, Tcl_Size internalOffset, char *oldInternalPtr,
			    int flags);
static Tcl_Obj *	GetLineStartEnd(void *clientData, Tk_Window tkwin, char *recordPtr,
			    Tcl_Size internalOffset);
static void		RestoreLineStartEnd(void *clientData, Tk_Window tkwin, char *internalPtr,
			    char *oldInternalPtr);

static const Tk_ObjCustomOption lineOption = {
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

static int		SetTextStartEnd(void *clientData, Tcl_Interp *interp, Tk_Window tkwin,
			    Tcl_Obj **value, char *recordPtr, Tcl_Size internalOffset, char *oldInternalPtr,
			    int flags);
static Tcl_Obj *	GetTextStartEnd(void *clientData, Tk_Window tkwin, char *recordPtr,
			    Tcl_Size internalOffset);
static void		RestoreTextStartEnd(void *clientData, Tk_Window tkwin, char *internalPtr,
			    char *oldInternalPtr);
static void		FreeTextStartEnd(void *clientData, Tk_Window tkwin, char *internalPtr);

static const Tk_ObjCustomOption startEndMarkOption = {
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
	"AutoSeparators", DEF_TEXT_AUTO_SEPARATORS, TCL_INDEX_NONE, offsetof(TkText, autoSeparators),
	TK_OPTION_DONT_SET_DEFAULT, 0, 0},
    {TK_OPTION_BORDER, "-background", "background", "Background",
	DEF_TEXT_BG_COLOR, TCL_INDEX_NONE, offsetof(TkText, border), 0, DEF_TEXT_BG_MONO, TK_TEXT_LINE_REDRAW},
    {TK_OPTION_SYNONYM, "-bd", NULL, NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-borderwidth", TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_SYNONYM, "-bg", NULL, NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-background", TK_TEXT_LINE_REDRAW},
    {TK_OPTION_BOOLEAN, "-blockcursor", "blockCursor",
	"BlockCursor", DEF_TEXT_BLOCK_CURSOR, TCL_INDEX_NONE, offsetof(TkText, blockCursorType), 0, 0, 0},
    {TK_OPTION_PIXELS, "-borderwidth", "borderWidth", "BorderWidth",
	DEF_TEXT_BORDER_WIDTH, TCL_INDEX_NONE, offsetof(TkText, borderWidth), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_CURSOR, "-cursor", "cursor", "Cursor",
	DEF_TEXT_CURSOR, TCL_INDEX_NONE, offsetof(TkText, cursor), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_CUSTOM, "-endindex", NULL, NULL,
	 NULL, TCL_INDEX_NONE, offsetof(TkText, newEndIndex), TK_OPTION_NULL_OK, &startEndMarkOption, TK_TEXT_INDEX_RANGE},
#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE
    {TK_OPTION_CUSTOM, "-endline", NULL, NULL,
	 NULL, TCL_INDEX_NONE, offsetof(TkText, endLine), TK_OPTION_NULL_OK, &lineOption, TK_TEXT_LINE_RANGE},
#endif
    {TK_OPTION_STRING, "-eolchar", "eolChar", "EolChar",
	NULL, offsetof(TkText, eolCharPtr), TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_COLOR, "-eolcolor", "eolColor", "EolColor",
	NULL, TCL_INDEX_NONE, offsetof(TkText, eolColor), TK_OPTION_NULL_OK, 0, TK_TEXT_LINE_REDRAW},
    {TK_OPTION_STRING, "-eotchar", "eotChar", "EotChar",
	NULL, offsetof(TkText, eotCharPtr), TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_COLOR, "-eotcolor", "eotColor", "EotColor",
	NULL, TCL_INDEX_NONE, offsetof(TkText, eotColor), TK_OPTION_NULL_OK, 0, TK_TEXT_LINE_REDRAW},
    {TK_OPTION_BOOLEAN, "-exportselection", "exportSelection",
	"ExportSelection", DEF_TEXT_EXPORT_SELECTION, TCL_INDEX_NONE, offsetof(TkText, exportSelection), 0, 0, 0},
    {TK_OPTION_SYNONYM, "-fg", "foreground", NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-foreground", TK_TEXT_LINE_REDRAW},
    {TK_OPTION_FONT, "-font", "font", "Font",
	DEF_TEXT_FONT, TCL_INDEX_NONE, offsetof(TkText, tkfont), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_COLOR, "-foreground", "foreground", "Foreground",
	DEF_TEXT_FG, TCL_INDEX_NONE, offsetof(TkText, fgColor), 0, 0, TK_TEXT_LINE_REDRAW},
    {TK_OPTION_PIXELS, "-height", "height", "Height",
	DEF_TEXT_HEIGHT, TCL_INDEX_NONE, offsetof(TkText, height), 0, 0, 0},
    {TK_OPTION_COLOR, "-highlightbackground", "highlightBackground", "HighlightBackground",
	DEF_TEXT_HIGHLIGHT_BG, TCL_INDEX_NONE, offsetof(TkText, highlightBgColorPtr), 0, 0, 0},
    {TK_OPTION_COLOR, "-highlightcolor", "highlightColor", "HighlightColor",
	DEF_TEXT_HIGHLIGHT, TCL_INDEX_NONE, offsetof(TkText, highlightColorPtr), 0, 0, 0},
    {TK_OPTION_PIXELS, "-highlightthickness", "highlightThickness", "HighlightThickness",
	DEF_TEXT_HIGHLIGHT_WIDTH, TCL_INDEX_NONE, offsetof(TkText, highlightWidth), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_STRING, "-hyphenrules", NULL, NULL,
	NULL, offsetof(TkText, hyphenRulesPtr), TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_COLOR, "-hyphencolor", "hyphenColor", "HyphenColor",
	DEF_TEXT_FG, TCL_INDEX_NONE, offsetof(TkText, hyphenColor), TK_OPTION_NULL_OK, 0, TK_TEXT_LINE_REDRAW},
    {TK_OPTION_BOOLEAN, "-hyphens", "hyphens", "Hyphens",
	"0", TCL_INDEX_NONE, offsetof(TkText, useHyphenSupport), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_BORDER, "-inactiveselectbackground", "inactiveSelectBackground", "Foreground",
	DEF_TEXT_INACTIVE_SELECT_BG_COLOR, TCL_INDEX_NONE, offsetof(TkText, selAttrs.inactiveBorder),
	TK_OPTION_NULL_OK, DEF_TEXT_SELECT_MONO, 0},
    {TK_OPTION_COLOR, "-inactiveselectforeground", "inactiveSelectForeground", "Background",
	DEF_TEXT_INACTIVE_SELECT_FG_COLOR, TCL_INDEX_NONE, offsetof(TkText, selAttrs.inactiveFgColor),
	TK_OPTION_NULL_OK, DEF_TEXT_SELECT_FG_MONO, 0},
    {TK_OPTION_BORDER, "-insertbackground", "insertBackground", "Foreground",
	DEF_TEXT_INSERT_BG, TCL_INDEX_NONE, offsetof(TkText, insertBorder), 0, 0, 0},
    {TK_OPTION_PIXELS, "-insertborderwidth", "insertBorderWidth",
	"BorderWidth", DEF_TEXT_INSERT_BD_COLOR, TCL_INDEX_NONE, offsetof(TkText, insertBorderWidth), 0,
	DEF_TEXT_INSERT_BD_MONO, 0},
    {TK_OPTION_COLOR, "-insertforeground", "insertForeground", "InsertForeground",
	DEF_TEXT_BG_COLOR, TCL_INDEX_NONE, offsetof(TkText, insertFgColor), 0, 0, 0},
    {TK_OPTION_INT, "-insertofftime", "insertOffTime", "OffTime",
	DEF_TEXT_INSERT_OFF_TIME, TCL_INDEX_NONE, offsetof(TkText, insertOffTime), 0, 0, 0},
    {TK_OPTION_INT, "-insertontime", "insertOnTime", "OnTime",
	DEF_TEXT_INSERT_ON_TIME, TCL_INDEX_NONE, offsetof(TkText, insertOnTime), 0, 0, 0},
    {TK_OPTION_STRING_TABLE,
	"-insertunfocussed", "insertUnfocussed", "InsertUnfocussed",
	DEF_TEXT_INSERT_UNFOCUSSED, TCL_INDEX_NONE, offsetof(TkText, insertUnfocussed),
	TK_OPTION_ENUM_VAR, insertUnfocussedStrings, 0},
    {TK_OPTION_PIXELS, "-insertwidth", "insertWidth", "InsertWidth",
	DEF_TEXT_INSERT_WIDTH, TCL_INDEX_NONE, offsetof(TkText, insertWidth), 0, 0, 0},
    {TK_OPTION_STRING_TABLE, "-justify", "justify", "Justify",
	"left", TCL_INDEX_NONE, offsetof(TkText, justify), TK_OPTION_ENUM_VAR, justifyStrings, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_STRING, "-lang", "lang", "Lang",
	 NULL, offsetof(TkText, langPtr), TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_INT, "-maxundo", "maxUndo", "MaxUndo",
	DEF_TEXT_MAX_UNDO, TCL_INDEX_NONE, offsetof(TkText, maxUndoDepth), TK_OPTION_DONT_SET_DEFAULT, 0, 0},
    {TK_OPTION_INT, "-maxundosize", "maxUndoSize", "MaxUndoSize",
	DEF_TEXT_MAX_UNDO, TCL_INDEX_NONE, offsetof(TkText, maxUndoSize), TK_OPTION_DONT_SET_DEFAULT, 0, 0},
    {TK_OPTION_INT, "-maxredo", "maxRedo", "MaxRedo",
	"TCL_INDEX_NONE", TCL_INDEX_NONE, offsetof(TkText, maxRedoDepth), TK_OPTION_DONT_SET_DEFAULT, 0, 0},
    {TK_OPTION_PIXELS, "-padx", "padX", "Pad",
	DEF_TEXT_PADX, TCL_INDEX_NONE, offsetof(TkText, padX), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_PIXELS, "-pady", "padY", "Pad",
	DEF_TEXT_PADY, TCL_INDEX_NONE, offsetof(TkText, padY), 0, 0, 0},
    {TK_OPTION_RELIEF, "-relief", "relief", "Relief",
	DEF_TEXT_RELIEF, TCL_INDEX_NONE, offsetof(TkText, relief), 0, 0, 0},
    {TK_OPTION_INT, "-responsiveness", "responsiveness", "Responsiveness",
	"50", TCL_INDEX_NONE, offsetof(TkText, responsiveness), 0, 0, 0},
    {TK_OPTION_BORDER, "-selectbackground", "selectBackground", "Foreground",
	DEF_TEXT_SELECT_COLOR, TCL_INDEX_NONE, offsetof(TkText, selAttrs.border),
	0, DEF_TEXT_SELECT_MONO, 0},
    {TK_OPTION_PIXELS, "-selectborderwidth", "selectBorderWidth", "BorderWidth",
	DEF_TEXT_SELECT_BD_COLOR, offsetof(TkText, selAttrs.borderWidthPtr),
	offsetof(TkText, selAttrs.borderWidth), TK_OPTION_NULL_OK, DEF_TEXT_SELECT_BD_MONO, 0},
    {TK_OPTION_COLOR, "-selectforeground", "selectForeground", "Background",
	DEF_TEXT_SELECT_FG_COLOR, TCL_INDEX_NONE, offsetof(TkText, selAttrs.fgColor),
	TK_OPTION_NULL_OK, DEF_TEXT_SELECT_FG_MONO, 0},
    {TK_OPTION_BOOLEAN, "-setgrid", "setGrid", "SetGrid",
	DEF_TEXT_SET_GRID, TCL_INDEX_NONE, offsetof(TkText, setGrid), 0, 0, 0},
    {TK_OPTION_BOOLEAN, "-showendofline", "showEndOfLine", "ShowEndOfLine",
	"0", TCL_INDEX_NONE, offsetof(TkText, showEndOfLine), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_BOOLEAN, "-showendoftext", "showEndOfText", "ShowEndOfText",
	"0", TCL_INDEX_NONE, offsetof(TkText, showEndOfText), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_BOOLEAN, "-showinsertforeground", "showInsertForeground", "ShowInsertForeground",
	"0", TCL_INDEX_NONE, offsetof(TkText, showInsertFgColor), 0, 0, 0},
    {TK_OPTION_STRING_TABLE, "-spacemode", "spaceMode", "SpaceMode",
	"none", TCL_INDEX_NONE, offsetof(TkText, spaceMode), TK_OPTION_ENUM_VAR, spaceModeStrings, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_PIXELS, "-spacing1", "spacing1", "Spacing",
	DEF_TEXT_SPACING1, TCL_INDEX_NONE, offsetof(TkText, spacing1), 0, 0 , TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_PIXELS, "-spacing2", "spacing2", "Spacing",
	DEF_TEXT_SPACING2, TCL_INDEX_NONE, offsetof(TkText, spacing2), 0, 0 , TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_PIXELS, "-spacing3", "spacing3", "Spacing",
	DEF_TEXT_SPACING3, TCL_INDEX_NONE, offsetof(TkText, spacing3), 0, 0 , TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_CUSTOM, "-startindex", NULL, NULL,
	 NULL, TCL_INDEX_NONE, offsetof(TkText, newStartIndex), TK_OPTION_NULL_OK, &startEndMarkOption, TK_TEXT_INDEX_RANGE},
#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE
    {TK_OPTION_CUSTOM, "-startline", NULL, NULL,
	 NULL, TCL_INDEX_NONE, offsetof(TkText, startLine), TK_OPTION_NULL_OK, &lineOption, TK_TEXT_LINE_RANGE},
#endif
    {TK_OPTION_STRING_TABLE, "-state", "state", "State",
	DEF_TEXT_STATE, TCL_INDEX_NONE, offsetof(TkText, state), TK_OPTION_ENUM_VAR, stateStrings, 0},
    {TK_OPTION_BOOLEAN, "-steadymarks", "steadyMarks", "SteadyMarks",
	"0", TCL_INDEX_NONE, offsetof(TkText, steadyMarks), TK_OPTION_DONT_SET_DEFAULT, 0, 0},
    {TK_OPTION_INT, "-synctime", "syncTime", "SyncTime",
	"150", TCL_INDEX_NONE, offsetof(TkText, syncTime), 0, 0, TK_TEXT_SYNCHRONIZE},
    {TK_OPTION_STRING, "-tabs", "tabs", "Tabs",
	DEF_TEXT_TABS, offsetof(TkText, tabOptionPtr), TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_STRING_TABLE, "-tabstyle", "tabStyle", "TabStyle",
	DEF_TEXT_TABSTYLE, TCL_INDEX_NONE, offsetof(TkText, tabStyle), 0, tkTextTabStyleStrings, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_STRING_TABLE, "-tagging", "tagging", "Tagging",
	"within", TCL_INDEX_NONE, offsetof(TkText, tagging), TK_OPTION_ENUM_VAR, taggingStrings, 0},
    {TK_OPTION_STRING, "-takefocus", "takeFocus", "TakeFocus",
	DEF_TEXT_TAKE_FOCUS, TCL_INDEX_NONE, offsetof(TkText, takeFocus), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BOOLEAN, "-undo", "undo", "Undo",
	DEF_TEXT_UNDO, TCL_INDEX_NONE, offsetof(TkText, undo), TK_OPTION_DONT_SET_DEFAULT, 0 ,0},
    {TK_OPTION_BOOLEAN, "-undotagging", "undoTagging", "UndoTagging",
	"1", TCL_INDEX_NONE, offsetof(TkText, undoTagging), 0, 0 ,0},
    {TK_OPTION_BOOLEAN, "-useunibreak", "useUniBreak", "UseUniBreak",
	"0", TCL_INDEX_NONE, offsetof(TkText, useUniBreak), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_INT, "-width", "width", "Width",
	DEF_TEXT_WIDTH, TCL_INDEX_NONE, offsetof(TkText, width), 0, 0, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_STRING_TABLE, "-wrap", "wrap", "Wrap",
	DEF_TEXT_WRAP, TCL_INDEX_NONE, offsetof(TkText, wrapMode), TK_OPTION_ENUM_VAR, tkTextWrapStrings, TK_TEXT_LINE_GEOMETRY},
    {TK_OPTION_STRING, "-xscrollcommand", "xScrollCommand", "ScrollCommand",
	DEF_TEXT_XSCROLL_COMMAND, TCL_INDEX_NONE, offsetof(TkText, xScrollCmd), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-yscrollcommand", "yScrollCommand", "ScrollCommand",
	DEF_TEXT_YSCROLL_COMMAND, TCL_INDEX_NONE, offsetof(TkText, yScrollCmd), TK_OPTION_NULL_OK, 0, 0},
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

typedef void *SearchAddLineProc(int lineNum, struct SearchSpec *searchSpecPtr,
			    Tcl_Obj *theLine, int *lenPtr, int *extraLinesPtr);
typedef int		SearchMatchProc(int lineNum, struct SearchSpec *searchSpecPtr,
			    void *clientData, Tcl_Obj *theLine, int matchOffset, int matchLength);
typedef int		SearchLineIndexProc(Tcl_Interp *interp, Tcl_Obj *objPtr,
			    struct SearchSpec *searchSpecPtr, int *linePosPtr, int *offsetPosPtr);

typedef struct SearchSpec {
    TkText *textPtr;		/* Information about widget. */
    int exact;			/* Whether search is exact or regexp. */
    int noCase;		/* Case-insenstivive? */
    int noLineStop;		/* If not set, a regexp search will use the TCL_REG_NLSTOP flag. */
    int overlap;		/* If set, results from multiple searches (-all) are allowed to
    				 * overlap each other. */
    int strictLimits;		/* If set, matches must be completely inside the from,to range.
    				 * Otherwise the limits only apply to the start of each match. */
    int all;			/* Whether all or the first match should be reported. */
    int backwards;		/* Searching forwards or backwards. */
    int searchElide;		/* Search in hidden text as well. */
    int searchHyphens;		/* Search in soft hyhens as well. */
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
    void *clientData;	/* Information about structure being searched, in this case a text
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

int tkTextDebug = 0;

typedef const TkTextUndoAtom * (*InspectUndoStackProc)(TkTextUndoStack stack);

/*
 * Forward declarations for functions defined later in this file:
 */

static int		DeleteIndexRange(TkSharedText *sharedTextPtr, TkText *textPtr,
			    const TkTextIndex *indexPtr1, const TkTextIndex *indexPtr2, int flags,
			    int viewUpdate, int triggerWatchDelete, int triggerWatchInsert,
			    int userFlag, int final);
static int		CountIndices(const TkText *textPtr, const TkTextIndex *indexPtr1,
			    const TkTextIndex *indexPtr2, TkTextCountType type);
static void		DestroyText(TkText *textPtr);
static void		ClearText(TkText *textPtr, int clearTags);
static void		FireWidgetViewSyncEvent(void *clientData);
static void		FreeEmbeddedWindows(TkText *textPtr);
static void		InsertChars(TkText *textPtr, TkTextIndex *index1Ptr, TkTextIndex *index2Ptr,
			    char const *string, unsigned length, int viewUpdate,
			    TkTextTagSet *tagInfoPtr, TkTextTag *hyphenTagPtr, int parseHyphens);
static void		TextBlinkProc(void *clientData);
static void		TextCmdDeletedProc(void *clientData);
static int		CreateWidget(TkSharedText *sharedTextPtr, Tk_Window tkwin, Tcl_Interp *interp,
			    const TkText *parent, int objc, Tcl_Obj *const objv[]);
static void		TextEventProc(void *clientData, XEvent *eventPtr);
static void		ProcessConfigureNotify(TkText *textPtr, int updateLineGeometry);
static Tcl_Size		TextFetchSelection(void *clientData, Tcl_Size offset, char *buffer,
			    Tcl_Size maxBytes);
static int		TextIndexSortProc(const void *first, const void *second);
static int		TextInsertCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[], const TkTextIndex *indexPtr,
			    int viewUpdate, int triggerWatchDelete, int triggerWatchInsert,
			    int userFlag, int parseHyphens);
static int		TextReplaceCmd(TkText *textPtr, Tcl_Interp *interp,
			    const TkTextIndex *indexFromPtr, const TkTextIndex *indexToPtr,
			    int objc, Tcl_Obj *const objv[], int viewUpdate, int triggerWatch,
			    int userFlag, int parseHyphens);
static int		TextSearchCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static int		TextEditCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static int		TextWidgetObjCmd(void *clientData,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static void		TextWorldChangedCallback(void *instanceData);
static void		TextWorldChanged(TkText *textPtr, int mask);
static void		UpdateLineMetrics(TkText *textPtr, unsigned lineNum, unsigned endLine);
static int		TextChecksumCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static int		TextDumpCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static int		TextInspectCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static int		DumpLine(Tcl_Interp *interp, TkText *textPtr,
			    int what, TkTextLine *linePtr, int start, int end,
			    int lineno, Tcl_Obj *command, TkTextTag **prevTagPtr);
static int		DumpSegment(TkText *textPtr, Tcl_Interp *interp, const char *key,
			    const char *value, Tcl_Obj *command, const TkTextIndex *index, int what);
static void		InspectUndoStack(const TkSharedText *sharedTextPtr,
			    InspectUndoStackProc firstAtomProc, InspectUndoStackProc nextAtomProc,
			    Tcl_Obj *objPtr);
static void		InspectRetainedUndoItems(const TkSharedText *sharedTextPtr, Tcl_Obj *objPtr);
static Tcl_Obj *	TextGetText(TkText *textPtr, const TkTextIndex *index1,
			    const TkTextIndex *index2, TkTextIndex *lastIndexPtr, Tcl_Obj *resultPtr,
			    unsigned maxBytes, int visibleOnly, int includeHyphens);
static void		GenerateEvent(TkSharedText *sharedTextPtr, const char *type);
static void		RunAfterSyncCmd(void *clientData);
static void		UpdateModifiedFlag(TkSharedText *sharedTextPtr, int flag);
static Tcl_Obj *	MakeEditInfo(Tcl_Interp *interp, TkText *textPtr, Tcl_Obj *arrayPtr);
static Tcl_Obj *	GetEditInfo(Tcl_Interp *interp, TkText *textPtr, Tcl_Obj *option);
static unsigned		TextSearchIndexInLine(const SearchSpec *searchSpecPtr, TkTextLine *linePtr,
			    int byteIndex);
static int		TextPeerCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static int		TextWatchCmd(TkText *textPtr, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static int		TriggerWatchEdit(TkText *textPtr, int userFlag, const char *operation,
			    const TkTextIndex *indexPtr1, const TkTextIndex *indexPtr2,
			    const char *info, int final);
static void		TriggerUndoStackEvent(TkSharedText *sharedTextPtr);
static void		PushRetainedUndoTokens(TkSharedText *sharedTextPtr);
static void		PushUndoSeparatorIfNeeded(TkSharedText *sharedTextPtr, int autoSeparators,
			    TkTextEditMode currentEditMode);
static int		IsEmpty(const TkSharedText *sharedTextPtr, const TkText *textPtr);
static int		IsClean(const TkSharedText *sharedTextPtr, const TkText *textPtr,
			    int discardSelection);
static TkTextUndoPerformProc TextUndoRedoCallback;
static TkTextUndoFreeProc TextUndoFreeCallback;
static TkTextUndoStackContentChangedProc TextUndoStackContentChangedCallback;

/*
 * Some definitions for controlling "dump", "inspect", and "checksum".
 */

enum {
    TK_DUMP_TEXT                    = SEG_GROUP_CHAR,
    TK_DUMP_CHARS                   = TK_DUMP_TEXT|SEG_GROUP_HYPHEN,
    TK_DUMP_MARK                    = SEG_GROUP_MARK,
    TK_DUMP_ELIDE                   = SEG_GROUP_BRANCH,
    TK_DUMP_TAG                     = SEG_GROUP_TAG,
    TK_DUMP_WIN                     = SEG_GROUP_WINDOW,
    TK_DUMP_IMG                     = SEG_GROUP_IMAGE,
    TK_DUMP_NODE                    = 1 << 18,
    TK_DUMP_DUMP_ALL                = TK_DUMP_TEXT|TK_DUMP_CHARS|TK_DUMP_MARK|TK_DUMP_TAG|
                                      TK_DUMP_WIN|TK_DUMP_IMG,

    TK_DUMP_DISPLAY                 = 1 << 19,
    TK_DUMP_DISPLAY_CHARS           = TK_DUMP_CHARS|TK_DUMP_DISPLAY,
    TK_DUMP_DISPLAY_TEXT            = TK_DUMP_TEXT|TK_DUMP_DISPLAY,
    TK_DUMP_CRC_DFLT                = TK_DUMP_TEXT|SEG_GROUP_WINDOW|SEG_GROUP_IMAGE,
    TK_DUMP_CRC_ALL                 = TK_DUMP_TEXT|TK_DUMP_CHARS|TK_DUMP_DISPLAY_TEXT|SEG_GROUP_WINDOW|
			              SEG_GROUP_IMAGE|TK_DUMP_MARK|TK_DUMP_TAG,

    TK_DUMP_NESTED                  = 1 << 20,
    TK_DUMP_TEXT_CONFIGS            = 1 << 21,
    TK_DUMP_TAG_CONFIGS             = 1 << 22,
    TK_DUMP_TAG_BINDINGS            = 1 << 23,
    TK_DUMP_INSERT_MARK             = 1 << 24,
    TK_DUMP_INCLUDE_SEL             = 1 << 25,
    TK_DUMP_DONT_RESOLVE_COLORS     = 1 << 26,
    TK_DUMP_DONT_RESOLVE_FONTS      = 1 << 27,
    TK_DUMP_INCLUDE_DATABASE_CONFIG = 1 << 28,
    TK_DUMP_INCLUDE_SYSTEM_CONFIG   = 1 << 29,
    TK_DUMP_INCLUDE_DEFAULT_CONFIG  = 1 << 30,
    TK_DUMP_INCLUDE_SYSTEM_COLORS   = 1U << 31,
    TK_DUMP_INSPECT_DFLT            = TK_DUMP_DUMP_ALL,
    TK_DUMP_INSPECT_COMPLETE        = TK_DUMP_INSPECT_DFLT|TK_DUMP_TAG_BINDINGS|TK_DUMP_TEXT_CONFIGS|
    			              TK_DUMP_TAG_CONFIGS|TK_DUMP_INCLUDE_SEL|TK_DUMP_INSERT_MARK|
				      TK_DUMP_INCLUDE_DATABASE_CONFIG|TK_DUMP_INCLUDE_SYSTEM_CONFIG|
				      TK_DUMP_INCLUDE_DEFAULT_CONFIG|TK_DUMP_ELIDE|
				      TK_DUMP_INCLUDE_SYSTEM_COLORS,
    TK_DUMP_INSPECT_ALL             = TK_DUMP_INSPECT_COMPLETE|TK_DUMP_DISPLAY_TEXT|
    			              TK_DUMP_DONT_RESOLVE_COLORS|TK_DUMP_DONT_RESOLVE_FONTS|
				      TK_DUMP_NESTED,
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

static const Tk_ClassProcs textClass = {
    sizeof(Tk_ClassProcs),	/* size */
    TextWorldChangedCallback,	/* worldChangedProc */
    NULL,			/* createProc */
    NULL			/* modalProc */
};

#ifdef TK_CHECK_ALLOCS

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
	    fprintf(stderr, "Unreleased text widget %d\n", peer->widgetNumber);
	}
    }

    fprintf(stderr, "---------------------------------\n");
    fprintf(stderr, "ALLOCATION:        new    destroy\n");
    fprintf(stderr, "---------------------------------\n");
    fprintf(stderr, "Shared:       %8u - %8u\n", tkTextCountNewShared, tkTextCountDestroyShared);
    fprintf(stderr, "Peer:         %8u - %8u\n", tkTextCountNewPeer, tkTextCountDestroyPeer);
    fprintf(stderr, "Segment:      %8u - %8u\n", tkTextCountNewSegment, tkTextCountDestroySegment);
    fprintf(stderr, "Tag:          %8u - %8u\n", tkTextCountNewTag, tkTextCountDestroyTag);
    fprintf(stderr, "UndoToken:    %8u - %8u\n", tkTextCountNewUndoToken, tkTextCountDestroyUndoToken);
    fprintf(stderr, "Node:         %8u - %8u\n", tkTextCountNewNode, tkTextCountDestroyNode);
    fprintf(stderr, "Line:         %8u - %8u\n", tkTextCountNewLine, tkTextCountDestroyLine);
    fprintf(stderr, "Section:      %8u - %8u\n", tkTextCountNewSection, tkTextCountDestroySection);
    fprintf(stderr, "PixelInfo:    %8u - %8u\n", tkTextCountNewPixelInfo, tkTextCountDestroyPixelInfo);
    fprintf(stderr, "BitField:     %8u - %8u\n", tkBitCountNew, tkBitCountDestroy);
    fprintf(stderr, "IntSet:       %8u - %8u\n", tkIntSetCountNew, tkIntSetCountDestroy);
    fprintf(stderr, "--------------------------------\n");

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
	    || tkIntSetCountNew != tkIntSetCountDestroy)  {
	fprintf(stderr, "*** memory leak detected ***\n");
	fprintf(stderr, "----------------------------\n");
	/* TkBitCheckAllocs(); */
    }
}
#endif /* TK_CHECK_ALLOCS */

#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE

/*
 * Some helpers.
 */

static void WarnAboutDeprecatedStartLineOption() {
    static int printWarning = 1;
    if (printWarning) {
	fprintf(stderr, "tk::text: Option \"-startline\" is deprecated, "
		"please use option \"-startindex\".\n");
	printWarning = 0;
    }
}
static void WarnAboutDeprecatedEndLineOption() {
    static int printWarning = 1;
    if (printWarning) {
	fprintf(stderr, "tk::text: Option \"-endline\" is deprecated, "
		"please use option \"-endindex\".\n");
	printWarning = 0;
    }
}

#endif /* SUPPORT_DEPRECATED_STARTLINE_ENDLINE */

/*
 * Helper for guarded release of objects.
 */

static void
Tcl_GuardedDecrRefCount(Tcl_Obj *objPtr)
{
#ifndef NDEBUG
    /*
     * Tcl does not provide any function for querying the reference count.
     * So we need a work-around. Why does Tcl not provide a guarded version
     * for such a dangerous function?
     */
    assert(objPtr);
    Tcl_IncrRefCount(objPtr);
    assert(Tcl_IsShared(objPtr));
    Tcl_DecrRefCount(objPtr);
#endif
    Tcl_DecrRefCount(objPtr);
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

static Tcl_Size
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
    void *clientData,	/* Main window associated with interpreter. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Tk_Window tkwin = (Tk_Window)clientData;

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
# define TK_TEXT_SET_MAX_BIT_SIZE (((512 + TK_BIT_NBITS - 1)/TK_BIT_NBITS)*TK_BIT_NBITS)

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
	sharedTextPtr = (TkSharedText *)ckalloc(sizeof(TkSharedText));
	memset(sharedTextPtr, 0, sizeof(TkSharedText));

	Tcl_InitHashTable(&sharedTextPtr->tagTable, TCL_STRING_KEYS);
	Tcl_InitHashTable(&sharedTextPtr->markTable, TCL_STRING_KEYS);
	Tcl_InitHashTable(&sharedTextPtr->windowTable, TCL_STRING_KEYS);
	Tcl_InitHashTable(&sharedTextPtr->imageTable, TCL_STRING_KEYS);
	sharedTextPtr->usedTags = TkBitResize(NULL, TK_TEXT_SET_MAX_BIT_SIZE);
	sharedTextPtr->elisionTags = TkBitResize(NULL, TK_TEXT_SET_MAX_BIT_SIZE);
	sharedTextPtr->selectionTags = TkBitResize(NULL, TK_TEXT_SET_MAX_BIT_SIZE);
	sharedTextPtr->dontUndoTags = TkBitResize(NULL, TK_TEXT_SET_MAX_BIT_SIZE);
	sharedTextPtr->affectDisplayTags = TkBitResize(NULL, TK_TEXT_SET_MAX_BIT_SIZE);
	sharedTextPtr->notAffectDisplayTags = TkBitResize(NULL, TK_TEXT_SET_MAX_BIT_SIZE);
	sharedTextPtr->affectDisplayNonSelTags = TkBitResize(NULL, TK_TEXT_SET_MAX_BIT_SIZE);
	sharedTextPtr->affectGeometryTags = TkBitResize(NULL, TK_TEXT_SET_MAX_BIT_SIZE);
	sharedTextPtr->affectGeometryNonSelTags = TkBitResize(NULL, TK_TEXT_SET_MAX_BIT_SIZE);
	sharedTextPtr->affectLineHeightTags = TkBitResize(NULL, TK_TEXT_SET_MAX_BIT_SIZE);
	sharedTextPtr->tagLookup = (TkTextTag **)ckalloc(TK_TEXT_SET_MAX_BIT_SIZE*sizeof(TkTextTag *));
	sharedTextPtr->emptyTagInfoPtr = TkTextTagSetResize(NULL, 0);
	sharedTextPtr->maxRedoDepth = -1;
	sharedTextPtr->autoSeparators = 1;
	sharedTextPtr->undoTagging = 1;
	sharedTextPtr->lastEditMode = TK_TEXT_EDIT_OTHER;
	sharedTextPtr->lastUndoTokenType = -1;
	sharedTextPtr->startMarker = TkTextMakeStartEndMark(NULL, &tkTextLeftMarkType);
	sharedTextPtr->endMarker = TkTextMakeStartEndMark(NULL, &tkTextRightMarkType);
	sharedTextPtr->protectionMark[0] = TkTextMakeMark(NULL, NULL);
	sharedTextPtr->protectionMark[1] = TkTextMakeMark(NULL, NULL);
	sharedTextPtr->protectionMark[0]->typePtr = &tkTextProtectionMarkType;
	sharedTextPtr->protectionMark[1]->typePtr = &tkTextProtectionMarkType;

	DEBUG(memset(sharedTextPtr->tagLookup, 0, TK_TEXT_SET_MAX_BIT_SIZE*sizeof(TkTextTag *)));

	sharedTextPtr->mainPeer = (TkText *)ckalloc(sizeof(TkText));
	memset(sharedTextPtr->mainPeer, 0, sizeof(TkText));
	sharedTextPtr->mainPeer->startMarker = sharedTextPtr->startMarker;
	sharedTextPtr->mainPeer->endMarker = sharedTextPtr->endMarker;
	sharedTextPtr->mainPeer->sharedTextPtr = sharedTextPtr;

#ifdef TK_CHECK_ALLOCS
	if (tkTextCountNewShared++ == 0) {
	    atexit(AllocStatistic);
	}
	/*
	 * Add this shared resource to global list.
	 */
	{
	    WatchShared *wShared = ckalloc(sizeof(WatchShared));
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

    textPtr = (TkText *)ckalloc(sizeof(TkText));
    memset(textPtr, 0, sizeof(TkText));
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

    textPtr->state = TK_TEXT_STATE_NORMAL;
    textPtr->relief = TK_RELIEF_FLAT;
    textPtr->cursor = NULL;
    textPtr->charWidth = 1;
    textPtr->spaceWidth = 1;
    textPtr->lineHeight = -1;
    textPtr->prevWidth = Tk_Width(newWin);
    textPtr->prevHeight = Tk_Height(newWin);
    textPtr->useHyphenSupport = -1;
    textPtr->hyphenRules = TK_TEXT_HYPHEN_MASK;
    textPtr->prevSyncState = -1;
    textPtr->lastLineY = TK_TEXT_NEARBY_IS_UNDETERMINED;
    TkTextTagSetIncrRefCount(textPtr->curTagInfoPtr = sharedTextPtr->emptyTagInfoPtr);

    /*
     * This will add refCounts to textPtr.
     */

    TkTextCreateDInfo(textPtr);
    TkTextIndexSetupToStartOfText(&startIndex, textPtr, sharedTextPtr->tree);
    TkTextSetYView(textPtr, &startIndex, 0);
    textPtr->exportSelection = 1;
    textPtr->pickEvent.type = LeaveNotify;
    textPtr->steadyMarks = sharedTextPtr->steadyMarks;
    textPtr->undo = sharedTextPtr->undo;
    textPtr->maxUndoDepth = sharedTextPtr->maxUndoDepth;
    textPtr->maxRedoDepth = sharedTextPtr->maxRedoDepth;
    textPtr->maxUndoSize = sharedTextPtr->maxUndoSize;
    textPtr->autoSeparators = sharedTextPtr->autoSeparators;
    textPtr->undoTagging = sharedTextPtr->undoTagging;

    /*
     * Create the "sel" tag and the "current" and "insert" marks.
     * Note: it is important that textPtr->selTagPtr is NULL before this
     * initial call.
     */

    textPtr->selTagPtr = TkTextCreateTag(textPtr, "sel", NULL);
    textPtr->insertMarkPtr = TkrTextSetMark(textPtr, "insert", &startIndex);
    textPtr->currentMarkPtr = TkrTextSetMark(textPtr, "current", &startIndex);
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

    if (Tk_InitOptions(interp, textPtr, optionTable, textPtr->tkwin) != TCL_OK) {
	Tk_DestroyWindow(textPtr->tkwin);
	return TCL_ERROR;
    }
    textPtr->textConfigAttrs = textPtr->selAttrs;
    textPtr->selTagPtr->attrs = textPtr->selAttrs;

    if (TkConfigureText(interp, textPtr, objc - 2, objv + 2) != TCL_OK) {
	Tk_DestroyWindow(textPtr->tkwin);
	return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tk_NewWindowObj(textPtr->tkwin));
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
	ProcessConfigureNotify(textPtr, 1);
    }
    TkTextUpdateLineMetrics(textPtr, startLine, endLine);
}

/*
 *--------------------------------------------------------------
 *
 * TkTextAttemptToModifyDisabledWidget --
 *
 *	The GUI tries to modify a disabled text widget, so an
 *	error will be thrown.
 *
 * Results:
 *	Returns TCL_ERROR.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static void
ErrorNotAllowed(
    Tcl_Interp *interp,
    const char *text)
{
    Tcl_SetObjResult(interp, Tcl_NewStringObj(text, TCL_INDEX_NONE));
    Tcl_SetErrorCode(interp, "TK", "TEXT", "NOT_ALLOWED", NULL);
}

int
TkTextAttemptToModifyDisabledWidget(
    TCL_UNUSED(Tcl_Interp *))
{
#if SUPPORT_DEPRECATED_MODS_OF_DISABLED_WIDGET
    static int showWarning = 1;
    if (showWarning) {
	fprintf(stderr, "tk::text: Attempt to modify a disabled widget is deprecated.\n");
	showWarning = 0;
    }
    return TCL_OK;
#else /* if !SUPPORT_DEPRECATED_MODS_OF_DISABLED_WIDGET */
    ErrorNotAllowed(interp, "attempt to modify disabled widget");
    return TCL_ERROR;
#endif
}

/*
 *--------------------------------------------------------------
 *
 * TkTextAttemptToModifyDeadWidget --
 *
 *	The GUI tries to modify a dead text widget, so an
 *	error will be thrown.
 *
 * Results:
 *	Returns TCL_ERROR.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

int
TkTextAttemptToModifyDeadWidget(
    Tcl_Interp *interp)
{
#if SUPPORT_DEPRECATED_MODS_OF_DISABLED_WIDGET
    static int showWarning = 1;
    (void)interp;
    if (showWarning) {
	fprintf(stderr, "tk::text: Attempt to modify a dead widget is deprecated.\n");
	showWarning = 0;
    }
    return TCL_OK;
#else /* if !SUPPORT_DEPRECATED_MODS_OF_DISABLED_WIDGET */
    ErrorNotAllowed(interp, "attempt to modify dead widget");
    return TCL_ERROR;
#endif
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

static int
TestIfTriggerUserMod(
    TkSharedText *sharedTextPtr,
    Tcl_Obj *indexObjPtr)
{
    return sharedTextPtr->triggerWatchCmd && strcmp(Tcl_GetString(indexObjPtr), "insert") == 0;
}

static int
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
	return 1;
    }
    return 0;
}

static int
TestIfDisabled(
    Tcl_Interp *interp,
    const TkText *textPtr,
    int *result)
{
    assert(result);

    if (textPtr->state != TK_TEXT_STATE_DISABLED) {
	return 0;
    }
    *result = TkTextAttemptToModifyDisabledWidget(interp);
    return 1;
}

static int
TestIfDead(
    Tcl_Interp *interp,
    const TkText *textPtr,
    int *result)
{
    assert(result);

    if (!TkTextIsDeadPeer(textPtr)) {
	return 0;
    }
    *result = TkTextAttemptToModifyDeadWidget(interp);
    return 1;
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
	newScript = (char *)ckalloc(totalLen + 1);
    }

    memcpy(newScript, oldScript, lenOfOld);
    newScript[lenOfOld] = '\n';
    memcpy(newScript + lenOfOld + 1, script, lenOfNew + 1);
    newScriptObj = Tcl_NewStringObj(newScript, totalLen);
    if (newScript != buffer) { ckfree(newScript); }
    return newScriptObj;
}

#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE
static int
MatchOpt(
    const char *opt,
    const char *pattern,
    unsigned minMatchLen)
{
    if (strncmp(opt, pattern, minMatchLen) != 0) {
	return 0;
    }
    opt += minMatchLen;
    pattern += minMatchLen;
    while (1) {
	if (*opt == '\0') {
	    return 1;
	}
	if (*pattern == '\0') {
	    return 0;
	}
	if (*opt != *pattern) {
	    return 0;
	}
	opt += 1;
	pattern += 1;
    }
    return 0; /* never reached */
}
#endif /* SUPPORT_DEPRECATED_STARTLINE_ENDLINE */

static int
TextWidgetObjCmd(
    void *clientData,	/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    TkText *textPtr = (TkText *)clientData;
    TkSharedText *sharedTextPtr;
    int result = TCL_OK;
    int commandIndex = -1;
    int oldUndoStackEvent;

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
    sharedTextPtr->undoStackEvent = 0;

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

    if (CATCH_ASSERTION_FAILED) {
	result = TCL_ERROR;
	goto done;
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

	listPtr = (TkTextStringList *)ckalloc(sizeof(TkTextStringList));
	Tcl_IncrRefCount(listPtr->strObjPtr = objv[2]);
	listPtr->nextPtr = textPtr->varBindingList;
	textPtr->varBindingList = listPtr;
	break;
    }
    case TEXT_BBOX: {
	int x, y, width, height, argc = 2;
	int extents = 0;
	TkTextIndex index;

	if (objc == 4) {
	    const char* option = Tcl_GetString(objv[2]);

	    if (strcmp(option, "-extents") == 0) {
		extents = 1;
		argc += 1;
	    } else if (*option == '-') {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad option \"%s\": must be -extents", option));
		result = TCL_ERROR;
		goto done;
	    }
	}
	if (objc - argc + 2 != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "?-extents? index");
	    result = TCL_ERROR;
	    goto done;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[argc], &index)) {
	    result = TCL_ERROR;
	    goto done;
	}
	if (TkTextIndexBbox(textPtr, &index, extents, &x, &y, &width, &height, NULL, NULL)) {
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
#if 0 && TCL_UTF_MAX > 4
# ifdef __unix__
#  error "The use of external libraries with a proprietary pseudo UTF-8 encoding is safety-endagering and may result in invalid computationial results. This means: TCL_UTF_MAX > 4 cannot be supported here."
#endif
		ErrorNotAllowed(interp, "external library libunibreak/liblinebreak cannot "
			"be used with non-standard encodings");
#else
		ErrorNotAllowed(interp, "external library libunibreak/liblinebreak is not available");
#endif
		result = TCL_ERROR;
		goto done;
	    }
	    lang = Tcl_GetString(objv[3]);
	}
	if ((length = GetByteLength(objv[2])) < textPtr->brksBufferSize) {
	    textPtr->brksBufferSize = MAX(length, textPtr->brksBufferSize + 512);
	    textPtr->brksBuffer = (char *)ckrealloc(textPtr->brksBuffer, textPtr->brksBufferSize);
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
	    const char *opt = Tcl_GetString(objv[2]);

	    if (strcmp(opt, "-start") == 0) {
		optionObj = Tcl_NewStringObj(textPtr->startLine ? "-startline" : "-startindex", TCL_INDEX_NONE);
	    } else if (MatchOpt(opt, "-startline", 7)) {
		optionObj = Tcl_NewStringObj("-startline", TCL_INDEX_NONE);
	    } else if (strcmp(opt, "-end") == 0) {
		optionObj = Tcl_NewStringObj(textPtr->endLine ? "-endline" : "-endindex", TCL_INDEX_NONE);
	    } else if (MatchOpt(opt, "-endline", 5)) {
		optionObj = Tcl_NewStringObj("-endline", TCL_INDEX_NONE);
	    } else {
		Tcl_IncrRefCount(optionObj = objv[2]);
	    }

	    Tcl_IncrRefCount(optionObj);
	    objPtr = Tk_GetOptionValue(interp, (char *) textPtr,
		    textPtr->optionTable, optionObj, textPtr->tkwin);
	    Tcl_GuardedDecrRefCount(optionObj);

#else /* if !SUPPORT_DEPRECATED_STARTLINE_ENDLINE */

	    objPtr = Tk_GetOptionValue(interp, textPtr,
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
	ClearText(textPtr, 1);
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
	    Tcl_Obj *objPtr = Tk_GetOptionInfo(interp, textPtr,
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
	int update = 0;
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
				    ProcessConfigureNotify(textPtr, 1);
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
		    update = 1;
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
			if (from != to) {
			    if (from > to) {
				int tmp = from; from = to; to = tmp;
			    }
			    UpdateLineMetrics(textPtr, from, to);
			}
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
		"bad option \"%s\": must be -chars, -displaychars, -displayhyphens, "
		"-displayindices, -displaylines, -displaytext, -hyphens, -indices, "
		"-lines, -text, -update, -xpixels, or -ypixels", Tcl_GetString(objv[i])));
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
	    if (Tcl_GetBooleanFromObj(interp, objv[2], &tkBTreeDebug) != TCL_OK) {
		result = TCL_ERROR;
		goto done;
	    }
	    tkTextDebug = tkBTreeDebug;
	}
	break;
    case TEXT_DELETE: {
	int i, flags = 0;
	int ok = 1;

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
	    int triggerUserMod = TestIfTriggerUserMod(sharedTextPtr, objv[2]);
	    int triggerWatch = triggerUserMod || sharedTextPtr->triggerAlways;

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
	    ok = DeleteIndexRange(NULL, textPtr, &index1, index2Ptr, flags, 1,
		    triggerWatch, triggerWatch, triggerUserMod, 1);
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
	    int lastUsed;

	    objc -= 2;
	    objv += 2;
	    indices = (TkTextIndex *)ckalloc((objc + 1)*sizeof(TkTextIndex));

	    /*
	     * First pass verifies that all indices are valid.
	     */

	    for (i = 0; i < objc; i++) {
		if (!TkTextGetIndexFromObj(interp, textPtr, objv[i], &indices[i])) {
		    result = TCL_ERROR;
		    ckfree(indices);
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
	    useIdx = (char *)ckalloc(objc);
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
		    int triggerUserMod = TestIfTriggerUserMod(sharedTextPtr, objv[i]);
		    int triggerWatch = triggerUserMod || sharedTextPtr->triggerAlways;

		    if (triggerWatch) {
			TkTextSaveCursorIndex(textPtr);
		    }

		    /*
		     * We don't need to check the return value because all
		     * indices are preparsed above.
		     */

		    ok = DeleteIndexRange(NULL, textPtr, &indices[i], &indices[i + 1],
			    flags, 1, triggerWatch, triggerWatch, triggerUserMod, i == lastUsed);
		}
	    }
	    ckfree(indices);
	    ckfree(useIdx);
	}

	if (!ok) {
	    return TCL_OK; /* widget has been destroyed */
	}
	break;
    }
    case TEXT_DLINEINFO: {
	int x, y, width, height, base, argc = 2;
	int extents = 0;
	TkTextIndex index;

	if (objc == 4) {
	    const char* option = Tcl_GetString(objv[2]);

	    if (strcmp(option, "-extents") == 0) {
		extents = 1;
		argc += 1;
	    } else if (*option == '-') {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad option \"%s\": must be -extents", option));
		result = TCL_ERROR;
		goto done;
	    }
	}
	if (objc - argc + 2 != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "?-extents? index");
	    result = TCL_ERROR;
	    goto done;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[argc], &index)) {
	    result = TCL_ERROR;
	    goto done;
	}
	if (TkTextGetDLineInfo(textPtr, &index, extents, &x, &y, &width, &height, &base)) {
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
	int includeHyphens;
	int visibleOnly;
	unsigned countOptions;
	const char *option;

	if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "?-option? ?--? index1 ?index2 ...?");
	    result = TCL_ERROR;
	    goto done;
	}

	objPtr = NULL;
	found = 0;
	includeHyphens = 1;
	visibleOnly = 0;
	countOptions = 0;
	i = 2;

	while (objc > i + 1 && (option = Tcl_GetString(objv[i]))[0] == '-') {
	    int badOption = 0;

	    i += 1;

	    if (option[1] == '-') {
	    	if (option[2] == '\0') {
		    break;
		}
		badOption = 1;
	    } else if (++countOptions > 1) {
		i -= 1;
		break;
	    } else {
		switch (option[1]) {
		case 'c':
		    if (strcmp("-chars", option) != 0) {
			badOption = 1;
		    }
		    break;
		case 't':
		    if (strcmp("-text", option) != 0) {
			badOption = 1;
		    }
		    includeHyphens = 0;
		    break;
		case 'd':
		    if (strcmp("-displaychars", option) == 0) {
			visibleOnly = 1;
		    } else if (strcmp("-displaytext", option) == 0) {
			visibleOnly = 1;
			includeHyphens = 0;
		    } else {
			badOption = 1;
		    }
		    break;
		default:
		    badOption = 1;
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
		    Tcl_GuardedDecrRefCount(objPtr);
		}
		result = TCL_ERROR;
		goto done;
	    }

	    if (i + 1 == objc) {
		TkTextIndexForwChars(textPtr, &index1, 1, &index2, COUNT_INDICES);
	    } else {
		if (!TkTextGetIndexFromObj(interp, textPtr, objv[i + 1], &index2)) {
		    if (objPtr) {
			Tcl_GuardedDecrRefCount(objPtr);
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
	int triggerUserMod, triggerWatch;

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
	triggerWatch = triggerUserMod || sharedTextPtr->triggerAlways;

	if (triggerWatch) {
	    TkTextSaveCursorIndex(textPtr);
	}
	result = TextInsertCmd(textPtr, interp, objc - 3, objv + 3, &index, 1, triggerWatch,
		triggerWatch, triggerUserMod, commandIndex == TEXT_TK_TEXTINSERT);
	break;
    }
    case TEXT_INSPECT:
	result = TextInspectCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_ISCLEAN: {
	int discardSelection = 0;
	const TkText *myTextPtr = textPtr;
	int i;

	for (i = 2; i < objc; ++i) {
	    char const * opt = Tcl_GetString(objv[i]);

	    if (strcmp(opt, "-overall") == 0) {
		myTextPtr = NULL;
	    } else if (strcmp(opt, "-discardselection") == 0) {
		discardSelection = 1;
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
	int overall = 0;
	int i;

	for (i = 2; i < objc; ++i) {
	    char const * opt = Tcl_GetString(objv[i]);

	    if (strcmp(opt, "-overall") == 0) {
		overall = 1;
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
	Tcl_Obj *contentObjPtr;
	int validOptions = 0;

	if (objc != 3 && objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "textcontent");
	    result = TCL_ERROR;
	    goto done;
	}
	if (objc == 4) {
	    const char *opt = Tcl_GetString(objv[2]);

	    if (strcmp(opt, "-validconfig") != 0) {
		Tcl_SetObjResult(interp,
			Tcl_ObjPrintf("bad option \"%s\": must be -validconfig", opt));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "BAD_OPTION", NULL);
		result = TCL_ERROR;
		goto done;
	    }
	    validOptions = 1;
	    contentObjPtr = objv[3];
	} else {
	    contentObjPtr = objv[2];
	}
	if (TestIfPerformingUndoRedo(interp, sharedTextPtr, &result)) {
	    goto done;
	}
	ClearText(textPtr, 0);
	TkTextRelayoutWindow(textPtr, TK_TEXT_LINE_GEOMETRY);
	if ((result = TkBTreeLoad(textPtr, contentObjPtr, validOptions)) != TCL_OK) {
	    ClearText(textPtr, 0);
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
	    ProcessConfigureNotify(textPtr, 1);
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(TkTextPendingSync(textPtr)));
        break;
    }
    case TEXT_REPLACE:
    case TEXT_TK_TEXTREPLACE: {
	TkTextIndex indexFrom, indexTo, index;
	int triggerUserMod, triggerWatch;

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

	triggerUserMod = TestIfTriggerUserMod(sharedTextPtr, objv[2]);
	triggerWatch = triggerUserMod || sharedTextPtr->triggerAlways;

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

	if (TkTextIndexCompare(&indexFrom, &index) < 0 && TkTextIndexCompare(&index, &indexTo) <= 0) {
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

	    result = TextReplaceCmd(textPtr, interp, &indexFrom, &indexTo, objc, objv, 0,
		    triggerWatch, triggerUserMod, commandIndex == TEXT_TK_TEXTREPLACE);
	    if (textPtr->flags & DESTROYED) {
		return result;
	    }

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
	    result = TextReplaceCmd(textPtr, interp, &indexFrom, &indexTo, objc, objv, 0,
		    triggerWatch, triggerUserMod, commandIndex == TEXT_TK_TEXTREPLACE);
	    if (textPtr->flags & DESTROYED) {
		return result;
	    }
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
	int wrongNumberOfArgs = 0;

	if (objc == 3 || objc == 4) {
	    const char *option = Tcl_GetString(objv[2]);
	    if (*option != '-') {
		wrongNumberOfArgs = 1;
	    } else if (strncmp(option, "-command", objv[2]->length) != 0) {
		Tcl_AppendResult(interp, "wrong option \"", option,
			"\": should be \"-command\"", NULL);
		result = TCL_ERROR;
		goto done;
	    }
	} else if (objc != 2) {
	    wrongNumberOfArgs = 1;
	}
	if (wrongNumberOfArgs) {
	    Tcl_WrongNumArgs(interp, 2, objv, "?-command ?command??");
	    result = TCL_ERROR;
	    goto done;
	}
	if (!sharedTextPtr->allowUpdateLineMetrics) {
	    ProcessConfigureNotify(textPtr, 1);
	}
	if (objc == 3) {
	    if (textPtr->afterSyncCmd) {
		Tcl_SetObjResult(interp, textPtr->afterSyncCmd);
	    }
	} else if (objc == 4) {
	    Tcl_Obj *cmd = objv[3];
	    const char *script = Tcl_GetString(cmd);
	    int append = 0;

	    if (*script == '+') {
		script += 1;
		append = 1;
	    }

	    if (!textPtr->afterSyncCmd) {
		if (append) {
		    cmd = Tcl_NewStringObj(script, TCL_INDEX_NONE);
		}
		Tcl_IncrRefCount(textPtr->afterSyncCmd = cmd);
	    } else {
		if (!append && *script == '\0') {
		    if (textPtr->pendingAfterSync) {
			Tcl_CancelIdleCall(RunAfterSyncCmd, textPtr);
			textPtr->pendingAfterSync = 0;
		    }
		    cmd = NULL;
		} else {
		    if (append) {
			cmd = AppendScript(Tcl_GetString(textPtr->afterSyncCmd), script);
		    }
		    Tcl_IncrRefCount(cmd);
		}
		Tcl_GuardedDecrRefCount(textPtr->afterSyncCmd);
		textPtr->afterSyncCmd = cmd;
	    }
	    if (!textPtr->pendingAfterSync) {
		textPtr->pendingAfterSync = 1;
		if (!TkTextPendingSync(textPtr)) {
		    Tcl_DoWhenIdle(RunAfterSyncCmd, textPtr);
		}
	    }
	} else {
	    textPtr->sendSyncEvent = 1;

	    if (!TkTextPendingSync(textPtr)) {
		/*
		 * There is nothing to sync, so fire the <<WidgetViewSync>> event,
		 * because nobody else will do this when no update is pending.
		 */
		TkTextGenerateWidgetViewSyncEvent(textPtr, 0);
	    } else {
		UpdateLineMetrics(textPtr, 0, TkrBTreeNumLines(sharedTextPtr->tree, textPtr));
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
	    Tcl_GuardedDecrRefCount(cmd);
	}
    	break;
    }
    case TEXT_WINDOW:
	result = TkTextWindowCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_XVIEW:
	result = TkrTextXviewCmd(textPtr, interp, objc, objv);
	break;
    case TEXT_YVIEW:
	result = TkTextYviewCmd(textPtr, interp, objc, objv);
	break;
    }

  done:
    if (--textPtr->refCount == 0) {
	int sharedIsReleased = textPtr->sharedIsReleased;

	assert(textPtr->flags & MEM_RELEASED);
	ckfree(textPtr);
	DEBUG_ALLOC(tkTextCountDestroyPeer++);
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
	UpdateLineMetrics(textPtr, 0, TkrBTreeNumLines(sharedTextPtr->tree, textPtr));
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
 *	Returns 1 if the widget is empty, and false otherwise.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static int
DoesNotContainTextSegments(
    const TkTextSegment *segPtr1,
    const TkTextSegment *segPtr2)
{
    for ( ; segPtr1 != segPtr2; segPtr1 = segPtr1->nextPtr) {
	if (segPtr1->size > 0) {
	    return !segPtr1->nextPtr; /* ignore trailing newline */
	}
    }

    return 1;
}

static int
IsEmpty(
    const TkSharedText *sharedTextPtr,
    const TkText *textPtr)		/* Can be NULL. */
{
    TkTextSegment *startMarker;
    TkTextSegment *endMarker;

    assert(sharedTextPtr);

    if (TkrBTreeNumLines(sharedTextPtr->tree, textPtr) > 1) {
	return 0;
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

static int
ContainsAnySegment(
    const TkTextSegment *segPtr1,
    const TkTextSegment *segPtr2)
{
    for ( ; segPtr1 != segPtr2; segPtr1 = segPtr1->nextPtr) {
	if (segPtr1->size > 0 || segPtr1->normalMarkFlag) {
	    return !!segPtr1->nextPtr; /* ignore trailing newline */
	}
    }

    return 0;
}

static int
IsClean(
    const TkSharedText *sharedTextPtr,
    const TkText *textPtr,		/* Can be NULL. */
    int discardSelection)
{
    const TkTextTagSet *tagInfoPtr;
    const TkTextSegment *startMarker;
    const TkTextSegment *endMarker;
    const TkTextLine *endLine;

    assert(sharedTextPtr);

    if (TkrBTreeNumLines(sharedTextPtr->tree, textPtr) > 1) {
	return 0;
    }

    if (textPtr) {
	startMarker = textPtr->startMarker;
	endMarker = textPtr->endMarker;
    } else {
	startMarker = sharedTextPtr->startMarker;
	endMarker = sharedTextPtr->endMarker;
    }

    if (ContainsAnySegment(startMarker, endMarker)) {
	return 0;
    }

    endLine = endMarker->sectionPtr->linePtr;

    if (!textPtr && ContainsAnySegment(endLine->segPtr, NULL)) {
	/* This widget contains any mark on very last line. */
	return 0;
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
	    textPtr->triggerAlways = 0;
	    textPtr->watchCmd = NULL;
	}

	sharedTextPtr->triggerWatchCmd = 0; /* do not trigger recursively */
	sharedTextPtr->triggerAlways = 0;

	for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
	    if (tPtr->watchCmd) {
		sharedTextPtr->triggerWatchCmd = 1;
		if (tPtr->triggerAlways) {
		    sharedTextPtr->triggerAlways = 1;
		}
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
	    textPtr->triggerAlways = 1;
	    textPtr->sharedTextPtr->triggerAlways = 1;
	    argnum = 3;
	}

	cmd = objv[argnum];
	script = Tcl_GetString(cmd);

	if (*script == '+') {
	    script += 1;
	    if (textPtr->watchCmd) {
		cmd = AppendScript(Tcl_GetString(textPtr->watchCmd), script);
	    } else {
		cmd = Tcl_NewStringObj(script, TCL_INDEX_NONE);
	    }
	} else if (argnum == 2) {
	    TkText *tPtr;

	    textPtr->triggerAlways = 0;
	    textPtr->sharedTextPtr->triggerAlways = 0;

	    for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
		if (tPtr->triggerAlways) {
		    assert(tPtr->watchCmd);
		    sharedTextPtr->triggerWatchCmd = 1;
		}
	    }
	}

	textPtr->sharedTextPtr->triggerWatchCmd = 1;
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
		Tcl_ListObjAppendElement(NULL, peersObj, Tk_NewWindowObj(tPtr->tkwin));
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
 * PushUndoSeparatorIfNeeded --
 *
 *	Push undo separator if needed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May push a separator onto undo stack.
 *
 *----------------------------------------------------------------------
 */

static void
PushUndoSeparatorIfNeeded(
    TkSharedText *sharedTextPtr,
    int autoSeparators,
    TkTextEditMode currentEditMode)
{
    assert(sharedTextPtr->undoStack);

    if (sharedTextPtr->pushSeparator
	    || (autoSeparators && sharedTextPtr->lastEditMode != currentEditMode)) {
	PushRetainedUndoTokens(sharedTextPtr);
	TkTextUndoPushSeparator(sharedTextPtr->undoStack, 1);
	sharedTextPtr->pushSeparator = 0;
	sharedTextPtr->lastUndoTokenType = -1;
    }
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
    int viewUpdate,		/* Update vertical view if set. */
    int triggerWatch,		/* Should we trigger the watch command? */
    int userFlag,		/* Trigger due to user modification? */
    int parseHyphens)		/* Should we parse hyphens (tk_textReplace)? */
{
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    int origAutoSep = sharedTextPtr->autoSeparators;
    int result = TCL_OK;
    TkTextIndex indexTmp;
    int notDestroyed;

    assert(!TkTextIsDeadPeer(textPtr));

    textPtr->refCount += 1;

    /*
     * Perform the deletion and insertion, but ensure no undo-separator is
     * placed between the two operations. Since we are using the helper
     * functions 'DeleteIndexRange' and 'TextInsertCmd' we have to pretend
     * that the autoSeparators setting is off, so that we don't get an
     * undo-separator between the delete and insert.
     */

    if (sharedTextPtr->undoStack) {
	sharedTextPtr->autoSeparators = 0;
	PushUndoSeparatorIfNeeded(sharedTextPtr, origAutoSep, TK_TEXT_EDIT_REPLACE);
    }

    /* The line and segment storage may change when deleting. */
    indexTmp = *indexFromPtr;
    TkTextIndexSave(&indexTmp);

    notDestroyed = DeleteIndexRange(NULL, textPtr, indexFromPtr, indexToPtr,
	    0, viewUpdate, triggerWatch, 0, userFlag, 1);

    if (notDestroyed) {
	TkTextIndexRebuild(&indexTmp);
	result = TextInsertCmd(textPtr, interp, objc - 4, objv + 4, &indexTmp,
		viewUpdate, 0, triggerWatch, userFlag, parseHyphens);
    }

    if (sharedTextPtr->undoStack) {
	sharedTextPtr->lastEditMode = TK_TEXT_EDIT_REPLACE;
	sharedTextPtr->autoSeparators = origAutoSep;
    }

    TkTextDecrRefCountAndTestIfDestroyed(textPtr);
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
	TkTextSegment *ewPtr = (TkTextSegment *)Tcl_GetHashValue(hPtr);
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
    int clearTags)		/* Also clear all tags? */
{
    TkTextSegment *retainedMarks;
    TkTextIndex startIndex;
    TkText *tPtr;
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    Tcl_Size oldEpoch = TkBTreeEpoch(sharedTextPtr->tree);
    int steadyMarks = textPtr->sharedTextPtr->steadyMarks;
    int debug = tkBTreeDebug;

    tkBTreeDebug = 0; /* debugging is not wanted here */

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
	FreeEmbeddedWindows(tPtr);
	TkTextFreeDInfo(tPtr);
	textPtr->dInfoPtr = NULL;
	textPtr->dontRepick = 0;
	tPtr->abortSelections = 1;
	textPtr->lastLineY = TK_TEXT_NEARBY_IS_UNDETERMINED;
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
    retainedMarks = TkTextFreeMarks(sharedTextPtr, 1);
    Tcl_DeleteHashTable(&sharedTextPtr->imageTable);
    Tcl_DeleteHashTable(&sharedTextPtr->windowTable);

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
    sharedTextPtr->isAltered = 0;
    sharedTextPtr->isModified = 0;
    sharedTextPtr->isIrreversible = 0;
    sharedTextPtr->userHasSetModifiedFlag = 0;
    sharedTextPtr->haveToSetCurrentMark = 0;
    sharedTextPtr->undoLevel = 0;
    sharedTextPtr->pushSeparator = 0;
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
	tPtr->haveToSetCurrentMark = 0;
	TkBTreeLinkSegment(sharedTextPtr, tPtr->insertMarkPtr, &startIndex);
	TkBTreeLinkSegment(sharedTextPtr, tPtr->currentMarkPtr, &startIndex);
	tPtr->currentMarkIndex = startIndex;
    }

    sharedTextPtr->steadyMarks = 0;
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
	tkBTreeDebug = 1;
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
    int debug = tkBTreeDebug;

    tkBTreeDebug = 0; /* debugging is not wanted here */

    /*
     * Firstly, remove pending idle commands, and free the array.
     */

    if (textPtr->pendingAfterSync) {
	Tcl_CancelIdleCall(RunAfterSyncCmd, textPtr);
	textPtr->pendingAfterSync = 0;
    }
    if (textPtr->pendingFireEvent) {
	Tcl_CancelIdleCall(FireWidgetViewSyncEvent, textPtr);
	textPtr->pendingFireEvent = 0;
    }
    if (textPtr->afterSyncCmd) {
	Tcl_GuardedDecrRefCount(textPtr->afterSyncCmd);
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
    textPtr->undo = 0;

    /*
     * Always clean up the widget-specific tags first. Common tags (i.e. most)
     * will only be cleaned up when the shared structure is cleaned up.
     *
     * Firstly unset all the variables bound to this widget.
     */

    listPtr = textPtr->varBindingList;
    while (listPtr) {
	TkTextStringList *nextPtr = listPtr->nextPtr;

	Tcl_UnsetVar2(textPtr->interp, Tcl_GetString(listPtr->strObjPtr), NULL, TCL_GLOBAL_ONLY);
	Tcl_GuardedDecrRefCount(listPtr->strObjPtr);
	ckfree(listPtr);
	listPtr = nextPtr;
    }

    /*
     * Unset the watch command.
     */

    if (textPtr->watchCmd) {
	Tcl_GuardedDecrRefCount(textPtr->watchCmd);
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

	if (textPtr->refCount == 1) {
	    /* Don't forget to release the current tag info. */
	    TkTextTagSetDecrRefCount(textPtr->curTagInfoPtr);
	}
    } else {
	/* Prevent that this resource will be released too early. */
	textPtr->refCount += 1;

	ClearRetainedUndoTokens(sharedTextPtr);
	TkTextUndoDestroyStack(&sharedTextPtr->undoStack);
	ckfree(sharedTextPtr->undoTagList);
	ckfree(sharedTextPtr->undoMarkList);
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
	TkTextFreeMarks(sharedTextPtr, 0);
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
	ckfree(sharedTextPtr->mainPeer);
	ckfree(sharedTextPtr->tagLookup);

	if (sharedTextPtr->tagBindingTable) {
	    Tk_DeleteBindingTable(sharedTextPtr->tagBindingTable);
	}
	ckfree(sharedTextPtr);
	DEBUG_ALLOC(tkTextCountDestroyShared++);

	textPtr->sharedIsReleased = 1;
	textPtr->refCount -= 1;

#ifdef TK_CHECK_ALLOCS
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

	    ckfree(thisPtr);
	}
#endif
    }

    if (textPtr->tabArrayPtr) {
	ckfree(textPtr->tabArrayPtr);
    }
    if (textPtr->insertBlinkHandler) {
	Tcl_DeleteTimerHandler(textPtr->insertBlinkHandler);
    }

    textPtr->tkwin = NULL;
    Tcl_DeleteCommandFromToken(textPtr->interp, textPtr->widgetCmd);
    assert(textPtr->flags & DESTROYED);
    DEBUG(textPtr->flags |= MEM_RELEASED);
    TkTextReleaseIfDestroyed(textPtr);
    tkBTreeDebug = debug;
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

int
TkTextDecrRefCountAndTestIfDestroyed(
    TkText *textPtr)
{
    if (--textPtr->refCount == 0) {
	assert(textPtr->flags & DESTROYED);
	assert(textPtr->flags & MEM_RELEASED);
	ckfree(textPtr);
	DEBUG_ALLOC(tkTextCountDestroyPeer++);
	return 1;
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

int
TkTextReleaseIfDestroyed(
    TkText *textPtr)
{
    if (!(textPtr->flags & DESTROYED)) {
	assert(textPtr->refCount > 0);
	return 0;
    }
    if (--textPtr->refCount == 0) {
	assert(textPtr->flags & MEM_RELEASED);
	ckfree(textPtr);
	DEBUG_ALLOC(tkTextCountDestroyPeer++);
    }
    return 1;
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

int
TkTextTestLangCode(
    Tcl_Interp *interp,
    Tcl_Obj *langCodePtr)
{
    char const *lang = Tcl_GetString(langCodePtr);

    if (UCHAR(lang[0]) >= 0x80
	    || UCHAR(lang[1]) >= 0x80
	    || !isalpha(UCHAR(lang[0]))
	    || !isalpha(UCHAR(lang[1]))
	    || !islower(UCHAR(lang[0]))
	    || !islower(UCHAR(lang[1]))
	    || lang[2] != '\0') {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad lang \"%s\": "
		"must have the form of an ISO 639-1 language code, or empty", lang));
	Tcl_SetErrorCode(interp, "TK", "VALUE", "LANG", NULL);
	return 0;
    }
    return 1;
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

static int
IsNumberOrEmpty(
    const char *str)
{
    for ( ; *str; ++str) {
	if (!isdigit(UCHAR(*str))) {
	    return 0;
	}
    }
    return 1;
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
    Tcl_Size currentEpoch;
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    TkTextBTree tree = sharedTextPtr->tree;
    int copyDownFlags = 0;
    int oldExport = (textPtr->exportSelection) && (!Tcl_IsSafe(textPtr->interp));
    int oldTextDebug = tkTextDebug;
    int didHyphenate = textPtr->hyphenate;
    int oldUndoTagging = textPtr->undoTagging;
    int oldHyphenRules = textPtr->hyphenRules;
    int mask = 0;

    tkTextDebug = 0; /* debugging is not useful here */

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

	myObjv = (Tcl_Obj **)ckalloc(objc * sizeof(Tcl_Obj *));

	for (i = 0; i < objc; ++i) {
	    Tcl_Obj *obj = objv[i];

	    if (!(i & 1)) {
		if (strcmp(Tcl_GetString(objv[i]), "-start") == 0) {
		    if (i + 1 < objc && IsNumberOrEmpty(Tcl_GetString(objv[i + 1]))) {
			if (!startLineObj) {
			    Tcl_IncrRefCount(startLineObj = Tcl_NewStringObj("-startline", TCL_INDEX_NONE));
			}
			obj = startLineObj;
			WarnAboutDeprecatedStartLineOption();
		    } else {
			if (!startIndexObj) {
			    Tcl_IncrRefCount(startIndexObj = Tcl_NewStringObj("-startindex", TCL_INDEX_NONE));
			}
			obj = startIndexObj;
		    }
		} else if (MatchOpt(Tcl_GetString(objv[i]), "-startline", 7)) {
		    if (!startLineObj) {
			Tcl_IncrRefCount(startLineObj = Tcl_NewStringObj("-startline", TCL_INDEX_NONE));
		    }
		    obj = startLineObj;
		    WarnAboutDeprecatedStartLineOption();
		} else if (MatchOpt(Tcl_GetString(objv[i]), "-startindex", 7)) {
		    if (!startIndexObj) {
			Tcl_IncrRefCount(startIndexObj = Tcl_NewStringObj("-startindex", TCL_INDEX_NONE));
		    }
		    obj = startIndexObj;
		} else if (strcmp(Tcl_GetString(objv[i]), "-end") == 0) {
		    if (i + 1 < objc && IsNumberOrEmpty(Tcl_GetString(objv[i + 1]))) {
			if (!endLineObj) {
			    Tcl_IncrRefCount(endLineObj = Tcl_NewStringObj("-endline", TCL_INDEX_NONE));
			}
			obj = endLineObj;
			WarnAboutDeprecatedEndLineOption();
		    } else {
			if (!endIndexObj) {
			    Tcl_IncrRefCount(endIndexObj = Tcl_NewStringObj("-endindex", TCL_INDEX_NONE));
			}
			obj = endIndexObj;
		    }
		} else if (MatchOpt(Tcl_GetString(objv[i]), "-endline", 5)) {
		    if (!endLineObj) {
			Tcl_IncrRefCount(endLineObj = Tcl_NewStringObj("-endline", TCL_INDEX_NONE));
		    }
		    obj = endLineObj;
		    WarnAboutDeprecatedEndLineOption();
		} else if (MatchOpt(Tcl_GetString(objv[i]), "-endindex", 5)) {
		    if (!endIndexObj) {
			Tcl_IncrRefCount(endIndexObj = Tcl_NewStringObj("-endindex", TCL_INDEX_NONE));
		    }
		    obj = endIndexObj;
		}
	    }
	    myObjv[i] = obj;
	}

	textPtr->selAttrs = textPtr->textConfigAttrs;
	rc = Tk_SetOptions(interp, (char *) textPtr, textPtr->optionTable,
		objc, myObjv, textPtr->tkwin, &savedOptions, &mask);

	if (rc != TCL_OK) {
	    if (startLineObj && startIndexObj) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "cannot use both, -startindex, and deprecated -startline", TCL_INDEX_NONE));
		rc = TCL_ERROR;
	    }
	    if (endLineObj && endIndexObj) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "cannot use both, -endindex, and deprecated -endline", TCL_INDEX_NONE));
		rc = TCL_ERROR;
	    }
	}

	if (startLineObj)  { Tcl_GuardedDecrRefCount(startLineObj); }
	if (endLineObj)    { Tcl_GuardedDecrRefCount(endLineObj); }
	if (startIndexObj) { Tcl_GuardedDecrRefCount(startIndexObj); }
	if (endIndexObj)   { Tcl_GuardedDecrRefCount(endIndexObj); }

	ckfree(myObjv);

	if (rc != TCL_OK) {
	    goto error;
	}
    }

    if ((mask & TK_TEXT_INDEX_RANGE) == TK_TEXT_LINE_RANGE) {
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
		    "-startline must be less than or equal to -endline", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "TEXT", "INDEX_ORDER", NULL);
	    goto error;
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

    textPtr->selAttrs = textPtr->textConfigAttrs;
    if (Tk_SetOptions(interp, (char *) textPtr, textPtr->optionTable,
	    objc, objv, textPtr->tkwin, &savedOptions, &mask) != TCL_OK) {
	textPtr->selAttrs = textPtr->selTagPtr->attrs;
	tkTextDebug = oldTextDebug;
	return TCL_ERROR;
    }

#endif /* SUPPORT_DEPRECATED_STARTLINE_ENDLINE */

    if (sharedTextPtr->steadyMarks != textPtr->steadyMarks) {
	if (!IsClean(sharedTextPtr, NULL, 1)) {
	    ErrorNotAllowed(interp, "setting this option is possible only if the widget "
		    "is overall clean");
	    goto error;
	}
    }

    /*
     * Copy up shared flags.
     */

    /*
     * Update default value for undoing tag operations.
     */

    if (oldUndoTagging != textPtr->undoTagging) {
	sharedTextPtr->undoTagging = textPtr->undoTagging;
	copyDownFlags = 1;
    }

    /* This flag cannot alter if we have peers. */
    sharedTextPtr->steadyMarks = textPtr->steadyMarks;

    if (sharedTextPtr->autoSeparators != textPtr->autoSeparators) {
	sharedTextPtr->autoSeparators = textPtr->autoSeparators;
	copyDownFlags = 1;
    }

    if (textPtr->undo != sharedTextPtr->undo) {
	if (TestIfPerformingUndoRedo(interp, sharedTextPtr, NULL)) {
	    goto error;
	}

	assert(sharedTextPtr->undo == !!sharedTextPtr->undoStack);
	sharedTextPtr->undo = textPtr->undo;
	copyDownFlags = 1;

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
	    sharedTextPtr->pushSeparator = 0;
	    sharedTextPtr->isIrreversible = 0;
	    sharedTextPtr->isAltered = 0;
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
	    TkTextUndoSetMaxStackSize(sharedTextPtr->undoStack, textPtr->maxUndoSize, 0);
	}
	sharedTextPtr->maxUndoDepth = textPtr->maxUndoDepth;
	sharedTextPtr->maxRedoDepth = textPtr->maxRedoDepth;
	sharedTextPtr->maxUndoSize = textPtr->maxUndoSize;
	copyDownFlags = 1;
    }

    if (copyDownFlags) {
	TkText *tPtr;

	for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
	    tPtr->autoSeparators = sharedTextPtr->autoSeparators;
	    tPtr->maxUndoDepth = sharedTextPtr->maxUndoDepth;
	    tPtr->maxRedoDepth = sharedTextPtr->maxRedoDepth;
	    tPtr->maxUndoSize = sharedTextPtr->maxUndoSize;
	    tPtr->undo = sharedTextPtr->undo;
	    tPtr->undoTagging = sharedTextPtr->undoTagging;
	}
    }

    /*
     * Check soft hyphen support.
     */

    textPtr->hyphenate = textPtr->useHyphenSupport
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
	    goto error;
	}
    } else {
	textPtr->hyphenRules = TK_TEXT_HYPHEN_MASK;
    }
    if (oldHyphenRules != textPtr->hyphenRules && textPtr->hyphenate) {
	mask |= TK_TEXT_LINE_GEOMETRY;
    }

    /*
     * Parse tab stops.
     */

    if (textPtr->tabArrayPtr) {
	ckfree(textPtr->tabArrayPtr);
	textPtr->tabArrayPtr = NULL;
    }
    if (textPtr->tabOptionPtr) {
	textPtr->tabArrayPtr = TkTextGetTabs(interp, textPtr, textPtr->tabOptionPtr);
	if (!textPtr->tabArrayPtr) {
	    Tcl_AddErrorInfo(interp, "\n    (while processing -tabs option)");
	    goto error;
	}
    }

    /*
     * Check language support.
     */

    if (textPtr->langPtr) {
	if (!TkTextTestLangCode(interp, textPtr->langPtr)) {
	    goto error;
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
		goto error;
	    }
	} else {
	    TkTextIndexClear(&start, textPtr);
	    TkTextIndexSetSegment(&start, textPtr->startMarker);
	}
	if (textPtr->newEndIndex) {
	    if (!TkTextGetIndexFromObj(interp, sharedTextPtr->mainPeer, textPtr->newEndIndex, &end)) {
		goto error;
	    }
	} else {
	    TkTextIndexClear(&end, textPtr);
	    TkTextIndexSetSegment(&end, textPtr->endMarker);
	}
	if (TkTextIndexCompare(&start, &end) > 0) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "-startindex must be less than or equal to -endindex", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "TEXT", "INDEX_ORDER", NULL);
	    goto error;
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
	    Tcl_GuardedDecrRefCount(textPtr->newEndIndex);
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
	    Tcl_GuardedDecrRefCount(textPtr->newStartIndex);
	    textPtr->newStartIndex = NULL;
	}

	/*
	 * Line start and/or end have been adjusted. We need to validate the
	 * first displayed line and arrange for re-layout.
	 */

	TkBTreeClientRangeChanged(textPtr, MAX(0, textPtr->lineHeight));
	TkrTextMakeByteIndex(tree, NULL, TkTextIndexGetLineNumber(&textPtr->topIndex, NULL), 0, &current);

	if (TkTextIndexCompare(&current, &start) < 0 || TkTextIndexCompare(&end, &current) < 0) {
	    TkTextSearch search;
	    TkTextIndex first, last;
	    int selChanged = 0;

	    TkTextSetYView(textPtr, &start, 0);

	    /*
	     * We may need to adjust the selection. So we have to check
	     * whether the "sel" tag was applied to anything outside the
	     * current start,end.
	     */

	    TkrTextMakeByteIndex(tree, NULL, 0, 0, &first);
	    TkBTreeStartSearch(&first, &start, textPtr->selTagPtr, &search, SEARCH_NEXT_TAGON);
	    if (TkBTreeNextTag(&search)) {
		selChanged = 1;
	    } else {
		TkrTextMakeByteIndex(tree, NULL, TkrBTreeNumLines(tree, NULL), 0, &last);
		TkBTreeStartSearchBack(&end, &last, textPtr->selTagPtr,
			&search, SEARCH_EITHER_TAGON_TAGOFF);
		if (TkBTreePrevTag(&search)) {
		    selChanged = 1;
		}
	    }
	    if (selChanged) {
		/*
		 * Send an event that the selection has changed, and abort any
		 * partial-selections in progress.
		 */

		TkTextSelectionEvent(textPtr);
		textPtr->abortSelections = 1;
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
	    textPtr->currentMarkPtr = TkrTextSetMark(textPtr, "current", &start);
	} else if (TkTextIndexCompare(&current, &end) > 0) {
	    textPtr->currentMarkPtr = TkrTextSetMark(textPtr, "current", &end);
	}
    } else {
	currentEpoch = TkBTreeEpoch(tree);
    }

    /*
     * Don't allow negative values for specific attributes.
     */

    textPtr->spacing1 = MAX(textPtr->spacing1, 0);
    textPtr->spacing2 = MAX(textPtr->spacing2, 0);
    textPtr->spacing3 = MAX(textPtr->spacing3, 0);
    textPtr->highlightWidth = MAX(textPtr->highlightWidth, 0);
    textPtr->borderWidth = MAX(textPtr->borderWidth, 0);
    textPtr->insertWidth = MAX(textPtr->insertWidth, 0);
    textPtr->syncTime = MAX(0, textPtr->syncTime);
    textPtr->selAttrs.borderWidth = MAX(textPtr->selAttrs.borderWidth, 0);

    /*
     * Make sure that configuration options are properly mirrored between the
     * widget record and the "sel" tags.
     */

    if (textPtr->selAttrs.border != textPtr->textConfigAttrs.border) {
	textPtr->selTagPtr->attrs.border = textPtr->selAttrs.border;
    }
    if (textPtr->selAttrs.inactiveBorder != textPtr->textConfigAttrs.inactiveBorder) {
	textPtr->selTagPtr->attrs.inactiveBorder = textPtr->selAttrs.inactiveBorder;
    }
    if (textPtr->selAttrs.fgColor != textPtr->textConfigAttrs.fgColor) {
	textPtr->selTagPtr->attrs.fgColor = textPtr->selAttrs.fgColor;
    }
    if (textPtr->selAttrs.inactiveFgColor != textPtr->textConfigAttrs.inactiveFgColor) {
	textPtr->selTagPtr->attrs.inactiveFgColor = textPtr->selAttrs.inactiveFgColor;
    }
    if (textPtr->selAttrs.borderWidthPtr != textPtr->textConfigAttrs.borderWidthPtr) {
	textPtr->selTagPtr->attrs.borderWidthPtr = textPtr->selAttrs.borderWidthPtr;
	textPtr->selTagPtr->attrs.borderWidth = textPtr->selAttrs.borderWidth;
    }
    textPtr->textConfigAttrs = textPtr->selAttrs;
    textPtr->selAttrs = textPtr->selTagPtr->attrs;
    TkTextUpdateTagDisplayFlags(textPtr->selTagPtr);
    TkTextRedrawTag(NULL, textPtr, NULL, NULL, textPtr->selTagPtr, 0);

    /*
     * Claim the selection if we've suddenly started exporting it and there
     * are tagged characters.
     */

    if (textPtr->exportSelection && (!oldExport) && (!Tcl_IsSafe(textPtr->interp))) {
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

    if (textPtr->flags & GOT_FOCUS) {
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
	UpdateLineMetrics(textPtr, 0, TkrBTreeNumLines(sharedTextPtr->tree, textPtr));
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
	    textPtr->insertMarkPtr = TkrTextSetMark(textPtr, "insert", &start);
	} else if (TkTextIndexCompare(&current, &end) >= 0) {
	    textPtr->insertMarkPtr = TkrTextSetMark(textPtr, "insert", &end);
	}
    }

    tkTextDebug = oldTextDebug;
    TK_BTREE_DEBUG(TkBTreeCheck(sharedTextPtr->tree));

    return TCL_OK;

error:
    Tk_RestoreSavedOptions(&savedOptions);
    textPtr->selAttrs = textPtr->selTagPtr->attrs;
    tkTextDebug = oldTextDebug;
    return TCL_ERROR;
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
    Tcl_Size argc, i;
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
    void *instanceData)	/* Information about widget. */
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
    int updateLineGeometry)
{
    int mask = updateLineGeometry ? TK_TEXT_LINE_GEOMETRY : 0;

    /*
     * Do not allow line height computations before we accept the first
     * ConfigureNotify event. The problem is the very poor performance
     * in CalculateDisplayLineHeight() with very small widget width.
     */

    if (!textPtr->sharedTextPtr->allowUpdateLineMetrics) {
	textPtr->sharedTextPtr->allowUpdateLineMetrics = 1;
	updateLineGeometry = 1;
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
}

static void
ProcessDestroyNotify(
    TkText *textPtr)
{
    if (textPtr->setGrid) {
	Tk_UnsetGrid(textPtr->tkwin);
	textPtr->setGrid = 0;
    }
    if (!(textPtr->flags & OPTIONS_FREED)) {
	/* Restore the original attributes. */
	textPtr->selAttrs = textPtr->textConfigAttrs;
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
	    textPtr->flags |= GOT_FOCUS | INSERT_ON;
	} else {
	    textPtr->flags &= ~(GOT_FOCUS | INSERT_ON);
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
	    TkrTextChanged(NULL, textPtr, &index, &index2);
	}
	if (textPtr->selAttrs.inactiveBorder != textPtr->selAttrs.border
		|| textPtr->selAttrs.inactiveFgColor != textPtr->selAttrs.fgColor) {
	    TkTextRedrawTag(NULL, textPtr, NULL, NULL, textPtr->selTagPtr, 0);
	}
	if (textPtr->highlightWidth > 0) {
	    TkTextRedrawRegion(textPtr, 0, 0, textPtr->highlightWidth, textPtr->highlightWidth);
	}
    }
}

static void
TextEventProc(
    void *clientData,	/* Information about window. */
    XEvent *eventPtr)		/* Information about event. */
{
    TkText *textPtr = (TkText *)clientData;

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
	    ProcessConfigureNotify(textPtr, 1);
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
    void *clientData)	/* Pointer to widget record for widget. */
{
    TkText *textPtr = (TkText *)clientData;
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
 *	If 'viewUpdate' is 1, we may adjust the window contents'
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
    int viewUpdate)			/* Update the view of current widget if set. */
{
    TkText *tPtr;

    for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next, ++positions) {
	if (positions->lineIndex != -1) {
	    TkTextIndex index;

	    if (tPtr == textPtr && !viewUpdate) {
		continue;
	    }

	    TkrTextMakeByteIndex(sharedTextPtr->tree, NULL, positions->lineIndex, 0, &index);
	    TkrTextIndexForwBytes(tPtr, &index, positions->byteIndex, &index);

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
#if 0 && TCL_UTF_MAX > 4
# error "The text widget is designed for UTF-8, this applies also to the legacy code. Undocumented pseudo UTF-8 strings cannot be processed with this function, because it relies on the UTF-8 specification."
#endif

    assert(TK_TEXT_HYPHEN_MASK < 256); /* otherwise does not fit into char */

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
			*buffer++ = (char) (1 << TK_TEXT_HYPHEN_CK);
			string += 4;
			break;
		    }
		    *buffer++ = *string++;
		    break;
		case 'd':
		    if (strncmp(string, ":dd:", 4) == 0) {
			*buffer++ = 0xff;
			*buffer++ = (char) (1 << TK_TEXT_HYPHEN_DOUBLE_DIGRAPH);
			string += 4;
			break;
		    }
		    if (strncmp(string, ":dv:", 4) == 0) {
			*buffer++ = 0xff;
			*buffer++ = (char) (1 << TK_TEXT_HYPHEN_DOUBLE_VOWEL);
			string += 4;
			break;
		    }
		    if (strncmp(string, ":doubledigraph:", 15) == 0) {
			*buffer++ = 0xff;
			*buffer++ = (char) (1 << TK_TEXT_HYPHEN_DOUBLE_DIGRAPH);
			string += 15;
			break;
		    }
		    if (strncmp(string, ":doublevowel:", 13) == 0) {
			*buffer++ = 0xff;
			*buffer++ = (char) (1 << TK_TEXT_HYPHEN_DOUBLE_VOWEL);
			string += 13;
			break;
		    }
		    *buffer++ = *string++;
		    break;
		case 'g':
		    if (strncmp(string, ":ge:", 4) == 0) {
			*buffer++ = 0xff;
			*buffer++ = (char) (1 << TK_TEXT_HYPHEN_GEMINATION);
			string += 4;
			break;
		    }
		    if (strncmp(string, ":gemination:", 12) == 0) {
			*buffer++ = 0xff;
			*buffer++ = (char) (1 << TK_TEXT_HYPHEN_GEMINATION);
			string += 12;
			break;
		    }
		    *buffer++ = *string++;
		    break;
		case 'r':
		    if (strncmp(string, ":rh:", 4) == 0) {
			*buffer++ = 0xff;
			*buffer++ = (char) (1 << TK_TEXT_HYPHEN_REPEAT);
			string += 4;
			break;
		    }
		    if (strncmp(string, ":repeathyphen:", 14) == 0) {
			*buffer++ = 0xff;
			*buffer++ = (char) (1 << TK_TEXT_HYPHEN_REPEAT);
			string += 14;
			break;
		    }
		    *buffer++ = *string++;
		    break;
		case 't':
		    if (strncmp(string, ":tr:", 4) == 0) {
			*buffer++ = 0xff;
			*buffer++ = (char) (1 << TK_TEXT_HYPHEN_TREMA);
			string += 4;
			break;
		    }
		    if (strncmp(string, ":tc:", 4) == 0) {
			*buffer++ = 0xff;
			*buffer++ = (char) (1 << TK_TEXT_HYPHEN_TRIPLE_CONSONANT);
			string += 4;
			break;
		    }
		    if (strncmp(string, ":trema:", 7) == 0) {
			*buffer++ = 0xff;
			*buffer++ = (char) (1 << TK_TEXT_HYPHEN_TREMA);
			string += 7;
			break;
		    }
		    if (strncmp(string, ":tripleconsonant:", 17) == 0) {
			*buffer++ = 0xff;
			*buffer++ = (char) (1 << TK_TEXT_HYPHEN_TRIPLE_CONSONANT);
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
    int viewUpdate,		/* Update the view if set. */
    TkTextTagSet *tagInfoPtr,	/* Add these tags to the inserted text, can be NULL. */
    TkTextTag *hyphenTagPtr,	/* Associate this tag with soft hyphens, can be NULL. */
    int parseHyphens)		/* Should we parse hyphens (tk_textInsert)? */
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

    if (sharedTextPtr->numPeers > sizeof(textPosBuf)/sizeof(textPosBuf[0])) {
	textPosition = (TkTextPosition *)ckalloc(sizeof(TkTextPosition)*sharedTextPtr->numPeers);
    } else {
	textPosition = textPosBuf;
    }
    InitPosition(sharedTextPtr, textPosition);
    FindNewTopPosition(sharedTextPtr, textPosition, index1Ptr, NULL, length);

    TkrTextChanged(sharedTextPtr, NULL, index1Ptr, index1Ptr);
    undoInfoPtr = TkTextUndoStackIsFull(sharedTextPtr->undoStack) ? NULL : &undoInfo;
    startIndex = *index1Ptr;
    TkTextIndexToByteIndex(&startIndex); /* we need the byte position after insertion */

    if (parseHyphens) {
	text = (length >= sizeof(textBuf)) ? (char *)ckalloc(length + 1) : textBuf;
	ParseHyphens(string, string + length, (char *) text);
    }

    TkBTreeInsertChars(sharedTextPtr->tree, index1Ptr, text, tagInfoPtr, hyphenTagPtr, undoInfoPtr);

    /*
     * Push the insertion on the undo stack, and update the modified status of the widget.
     * Try to join with previously pushed undo token, if possible.
     */

    if (undoInfoPtr) {
	const TkTextUndoSubAtom *subAtom;
	int triggerStackEvent = 0;
	int pushToken;

	assert(undoInfo.byteSize == 0);

	PushUndoSeparatorIfNeeded(sharedTextPtr, sharedTextPtr->autoSeparators, TK_TEXT_EDIT_INSERT);

	pushToken = sharedTextPtr->lastUndoTokenType != TK_TEXT_UNDO_INSERT
		|| !((subAtom = TkTextUndoGetLastUndoSubAtom(sharedTextPtr->undoStack))
			&& (triggerStackEvent = TkBTreeJoinUndoInsert(
				(TkTextUndoToken *)subAtom->item, subAtom->size, undoInfo.token, undoInfo.byteSize)));

	assert(undoInfo.token->undoType->rangeProc);
	sharedTextPtr->prevUndoStartIndex = ((TkTextUndoTokenRange *) undoInfo.token)->startIndex;
	sharedTextPtr->prevUndoEndIndex = ((TkTextUndoTokenRange *) undoInfo.token)->endIndex;
	sharedTextPtr->lastUndoTokenType = TK_TEXT_UNDO_INSERT;
	sharedTextPtr->lastEditMode = TK_TEXT_EDIT_INSERT;

	if (pushToken) {
	    TkTextPushUndoToken(sharedTextPtr, undoInfo.token, undoInfo.byteSize);
	} else {
	    assert(!undoInfo.token->undoType->destroyProc);
	    ckfree(undoInfo.token);
	    DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
	}
	if (triggerStackEvent) {
	    sharedTextPtr->undoStackEvent = 1; /* TkBTreeJoinUndoInsert didn't trigger */
	}
    }

    *index2Ptr = *index1Ptr;
    *index1Ptr = startIndex;
    UpdateModifiedFlag(sharedTextPtr, 1);
    TkTextUpdateAlteredFlag(sharedTextPtr);
    SetNewTopPosition(sharedTextPtr, textPtr, textPosition, viewUpdate);

    if (textPosition != textPosBuf) {
	ckfree(textPosition);
    }

    /*
     * Invalidate any selection retrievals in progress, and send an event
     * that the selection changed if that is the case.
     */

    for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
        if (TkBTreeCharTagged(index1Ptr, tPtr->selTagPtr)) {
            TkTextSelectionEvent(tPtr);
        }
	tPtr->abortSelections = 1;
    }

    if (parseHyphens && text != textBuf) {
	ckfree((char *) text);
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
    int isRedo,
    int isFinal,
    TkText **peers,
    int numPeers)
{
    TkTextIndex index1, index2;
    Tcl_Obj *cmdPtr;
    char buf[100];
    int i;

    assert(sharedTextPtr->triggerWatchCmd);
    assert(token->undoType->rangeProc);
    assert(token->undoType->commandProc);

    sharedTextPtr->triggerWatchCmd = 0; /* do not trigger recursively */
    token->undoType->rangeProc(sharedTextPtr, token, &index1, &index2);
    Tcl_IncrRefCount(cmdPtr = token->undoType->commandProc(sharedTextPtr, token));
    snprintf(buf, sizeof(buf), "%s", isFinal ? "yes" : "no");

    for (i = 0; i < numPeers; ++i) {
	TkText *tPtr = peers[i];

	if (tPtr->watchCmd && !(tPtr->flags & DESTROYED)) {
	    char idx[2][TK_POS_CHARS];
	    const char *info = isRedo ? "redo" : "undo";

	    TkrTextPrintIndex(tPtr, &index1, idx[0]);
	    TkrTextPrintIndex(tPtr, &index2, idx[1]);
	    TkTextTriggerWatchCmd(tPtr, info, idx[0], idx[1], Tcl_GetString(cmdPtr), buf, NULL, 0);
	}
    }

    Tcl_GuardedDecrRefCount(cmdPtr);
    sharedTextPtr->triggerWatchCmd = 1;
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
    int eventuallyRepick = 0;
    TkText *peerArr[20];
    TkText **peers = peerArr;
    TkText *tPtr;
    int i, k, countPeers = 0;

    assert(stack);

    if (sharedTextPtr->triggerWatchCmd) {
	if (sharedTextPtr->numPeers > sizeof(peerArr) / sizeof(peerArr[0])) {
	    peers = (TkText **)ckalloc(sharedTextPtr->numPeers * sizeof(peerArr[0]));
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
	TkTextUndoToken *token = (TkTextUndoToken *)subAtom->item;
	int isDelete = token->undoType->action == TK_TEXT_UNDO_INSERT
		|| token->undoType->action == TK_TEXT_REDO_DELETE;
	int isInsert = token->undoType->action == TK_TEXT_UNDO_DELETE
		|| token->undoType->action == TK_TEXT_REDO_INSERT;

	if (isInsert || isDelete) {
	    const TkTextUndoTokenRange *range = (const TkTextUndoTokenRange *) token;

	    if (isDelete && sharedTextPtr->triggerWatchCmd) {
		TriggerWatchUndoRedo(sharedTextPtr, token, subAtom->redo, i == 0, peers, countPeers);
	    }
	    if (!textPosition) {
		if (sharedTextPtr->numPeers > sizeof(textPosBuf)/sizeof(textPosBuf[0])) {
		    textPosition = (TkTextPosition *)ckalloc(sizeof(textPosition[0])*sharedTextPtr->numPeers);
		} else {
		    textPosition = textPosBuf;
		}
		InitPosition(sharedTextPtr, textPosition);
	    }
	    if (isInsert) {
		TkBTreeUndoIndexToIndex(sharedTextPtr, &range->startIndex, &index1);
		TkrTextChanged(sharedTextPtr, NULL, &index1, &index1);
		FindNewTopPosition(sharedTextPtr, textPosition, &index1, NULL, subAtom->size);
	    } else {
		token->undoType->rangeProc(sharedTextPtr, token, &index1, &index2);
		TkrTextChanged(sharedTextPtr, NULL, &index1, &index2);
		FindNewTopPosition(sharedTextPtr, textPosition, &index1, &index2, 0);
	    }
	    for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
		if (!tPtr->abortSelections) {
		    if (isInsert) {
			tPtr->abortSelections = 1;
		    } else {
			if (range->startIndex.lineIndex < range->endIndex.lineIndex
				&& TkBTreeTag(sharedTextPtr, NULL, &index1, &index2,
					tPtr->selTagPtr, 0, NULL, TkTextRedrawTag)) {
			    TkTextSelectionEvent(tPtr);
			    tPtr->abortSelections = 1;
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
	    eventuallyRepick = 1;
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
		    token->undoType->destroyProc(sharedTextPtr, (TkTextUndoToken *)subAtom->item, 1);
		}
		/*
		 * Do not free this item.
		 */
		((TkTextUndoSubAtom *) subAtom)->item = NULL;
	    }
	    TkTextPushUndoToken(sharedTextPtr, redoInfo.token, redoInfo.byteSize);
	}
	if (!isDelete && sharedTextPtr->triggerWatchCmd) {
	    TriggerWatchUndoRedo(sharedTextPtr, token, subAtom->redo, i == 0, peers, countPeers);
	}
    }

    if (eventuallyRepick) {
	for (k = 0; k < countPeers; ++k) {
	    tPtr = peers[k];

	    if (!(tPtr->flags & DESTROYED)) {
		TkTextEventuallyRepick(tPtr);
	    }
	}
    }

    sharedTextPtr->lastEditMode = TK_TEXT_EDIT_OTHER;
    sharedTextPtr->lastUndoTokenType = -1;
    UpdateModifiedFlag(sharedTextPtr, 0);
    TkTextUpdateAlteredFlag(sharedTextPtr);

    if (textPosition) {
	SetNewTopPosition(sharedTextPtr, NULL, textPosition, 1);
	if (textPosition != textPosBuf) {
	    ckfree(textPosition);
	}
    }

    if (sharedTextPtr->triggerWatchCmd) {
	for (i = 0; i < countPeers; ++i) {
	    tPtr = peers[i];

	    if (!(tPtr->flags & DESTROYED)) {
		TkTextIndexClear(&tPtr->insertIndex, tPtr);
		TkTextTriggerWatchCursor(tPtr);
	    }
	    TkTextDecrRefCountAndTestIfDestroyed(tPtr);
	}
    }

    /*
     * Freeing the peer array has to be done even if sharedTextPtr->triggerWatchCmd
     * is false, possibly the user has cleared the watch command inside the trigger
     * callback.
     */

    if (peers != peerArr) {
	ckfree(peers);
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
    ((TkSharedText *) TkTextUndoGetContext(stack))->undoStackEvent = 1;
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
    sharedTextPtr->undoStackEvent = 0;

    for (textPtr = sharedTextPtr->peers; textPtr; textPtr = textPtr->next) {
	if (!(textPtr->flags & DESTROYED)) {
	    Tk_MakeWindowExist(textPtr->tkwin);
	    Tk_SendVirtualEvent(textPtr->tkwin, "UndoStack", NULL);
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
	    token->undoType->destroyProc((TkSharedText *)TkTextUndoGetContext(stack), (TkTextUndoToken *)subAtom->item, 0);
	}
	ckfree(subAtom->item);
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
 * TkTextGetUndeletableNewline --
 *
 *	Return pointer to undeletable newline. The search will start at
 *	start of deletion. See comments in function about the properties
 *	of an undeletable newline.
 *
 *	Note that this functions expects that the deletions end on very
 *	last line in B-Tree, otherwise the newline is always deletable.
 *
 * Results:
 *	Returns the undeletable newline, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const TkTextSegment *
TkTextGetUndeletableNewline(
    const TkTextLine *lastLinePtr)	/* last line of deletion, must be last line of B-Tree */
{
    assert(lastLinePtr);
    assert(!lastLinePtr->nextPtr);

#if 0 /* THIS IS OLD IMPLEMENTATION */
    const TkTextSegment *segPtr = TkTextIndexGetContentSegment(&index1, NULL);

    /*
     * Advance to next character.
     */

    while (segPtr->size == 0) {
	segPtr = segPtr->nextPtr;
	assert(segPtr);
    }

    /*
     * Assume the following text content:
     *
     *    {"1" "\n"} {"\n"} {"2" "\n"} {"\n"}
     *      A   B      C      D   E      F
     *
     * Segment E is the last newline (F belongs to addtional empty line).
     * We have two cases where the last newline has to be preserved.
     *
     * 1. Deletion is starting in first line, then we have to preserve the
     *    last newline, return segment E.
     *
     * 2. Deletion is not starting at the first character in this line, then
     *    we have to preserve the last newline, return segment E.
     *
     * In all other cases return NULL.
     */

    if (segPtr->sectionPtr->linePtr->prevPtr && SegIsAtStartOfLine(segPtr)) {
	return NULL;
    }
#endif

    /*
     * The old implementation is erroneous, and has been changed:
     *
     * 1. Test the following script with old implementation:
     *	    text .t
     *      .t insert end "1\n2"
     *      .t delete 2.0 end
     *      .t insert end "2"
     *    The result of [.t get begin end] -> "12\n" is unexpected, the expected result is "1\n2\n".
     *
     * 2. The mathematical consistency now will be preserved:
     *      - The newly created text widget is clean and contains "\e"
     *        (\e is the always existing final newline in last line).
     *      - After insertion of "1\n2" at 'begin' we have "1\n2\e".
     *      - After [.t delete 2.0 end] the deletion starts with inserted character "2",
     *        and not with the inserted newline. Thus from mathematical point of view
     *        the result must be "1\n\e" (this means: the always existing final newline
     *        will never be deleted).
     *      - After [.t insert end "2"] the string "2" has been inserted at end, this means
     *        before "\e", so the new result is "1\n2\e".
     *
     * 3. It's a clean concept if the artificial newline is undeletable, the old concept is
     *    hard to understand for a user, and error-prone.
     */

    assert(lastLinePtr->prevPtr);
    return lastLinePtr->prevPtr->lastPtr; /* return final newline \e */
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
 *	If 'viewUpdate' is 1, we may adjust the window contents'
 *	y-position, and scrollbar setting.
 *
 *	If 'viewUpdate' is 1, true we can guarantee that textPtr->topIndex
 *	points to a valid TkTextLine after this function returns. However, if
 *	'viewUpdate' is false, then there is no such guarantee (since
 *	topIndex.linePtr can be garbage). The caller is expected to take
 *	actions to ensure the topIndex is validated before laying out the
 *	window again.
 *
 *----------------------------------------------------------------------
 */

static int
DeleteOnLastLine(
    TCL_UNUSED(TkSharedText *),
    const TkTextLine *lastLinePtr,
    int flags) /* deletion flags */
{
    assert(lastLinePtr);
    assert(!lastLinePtr->nextPtr);

    if (flags & DELETE_MARKS) {
	const TkTextSegment *segPtr = lastLinePtr->segPtr;

	while (segPtr->size == 0) {
	    if ((flags & DELETE_MARKS) && TkTextIsNormalMark(segPtr)) {
		return 1;
	    }
	    segPtr = segPtr->nextPtr;
	}
    }

    return 0;
}

static int
DeleteEndMarker(
    const TkTextIndex *indexPtr,
    int flags)
{
    const TkTextSegment *segPtr;

    return (flags & DELETE_MARKS)
	    && (segPtr = TkTextIndexGetSegment(indexPtr))
	    && TkTextIsNormalMark(segPtr);
}

static int
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
    int viewUpdate,		/* Update vertical view if set. */
    int triggerWatchDelete,	/* Should we trigger the watch command for deletion? */
    int triggerWatchInsert,	/* Should we trigger the watch command for insertion? */
    int userFlag,		/* Trigger user modification? */
    int final)			/* This is the final call in a sequence of ranges. */
{
    TkTextIndex index1, index2, index3;
    TkTextPosition *textPosition;
    TkTextPosition textPosBuf[PIXEL_CLIENTS];
    TkTextUndoInfo undoInfo;
    TkTextUndoInfo *undoInfoPtr;
    TkTextLine *lastLinePtr;

    if (!sharedTextPtr) {
	sharedTextPtr = textPtr->sharedTextPtr;
    }

    if (triggerWatchInsert) {
	TkTextIndexToByteIndex((TkTextIndex *) indexPtr1); /* mutable due to concept */
    }

    if (TkTextIndexIsEndOfText(indexPtr1)) {
	return 1; /* nothing to delete */
    }

    /*
     * Prepare the starting and stopping indices.
     */

    if (indexPtr2) {
	if (TkTextIndexCompare(indexPtr1, indexPtr2) >= 0) {
	    return 1; /* there is nothing to delete */
	}
	index1 = *indexPtr1;
	index2 = *indexPtr2;
    } else if (!TkTextIndexForwChars(textPtr, indexPtr1, 1, &index2, COUNT_INDICES)) {
	return 1;
    } else {
	index1 = *indexPtr1;
    }

    index3 = index2;

    if (!TkTextIndexGetLine(&index2)->nextPtr
	    && !DeleteEndMarker(&index2, flags)
	    && TkTextGetUndeletableNewline(lastLinePtr = TkTextIndexGetLine(&index2))
	    && !DeleteOnLastLine(sharedTextPtr, lastLinePtr, flags)) {
	/*
	 * This is a very special case. If the last newline is undeletable, we do not
	 * have a deletable marker at end of range, and there is no deletable mark on
	 * last line, then decrement the end of range.
	 */

	TkrTextIndexBackBytes(textPtr, &index2, 1, &index2);

	if (TkTextIndexIsEqual(&index1, &index2)) {
	    if (lastLinePtr->prevPtr) {
		if (lastLinePtr->prevPtr->lastPtr->tagInfoPtr != sharedTextPtr->emptyTagInfoPtr) {
		    /* we have to delete tags on previous newline, that's all */
		    TkTextClearSelection(sharedTextPtr, &index1, &index3);
		    TkTextClearTags(sharedTextPtr, textPtr, &index1, &index3, 0);
		} else {
		    assert(TkTextTagSetIsEmpty(lastLinePtr->prevPtr->lastPtr->tagInfoPtr));
		}
	    }
	    return 1; /* nothing to do */
	}

	if (lastLinePtr->prevPtr->lastPtr->tagInfoPtr != sharedTextPtr->emptyTagInfoPtr) {
	    if (!TkTextTagBitContainsSet(sharedTextPtr->selectionTags,
		    lastLinePtr->prevPtr->lastPtr->tagInfoPtr)) {
		/*
		 * Last newline is tagged with any non-selection tag, so we have to
		 * re-include this character.
		 */
		flags |= DELETE_LASTLINE;
		index2 = index3;
	    }
	}
    }

    /*
     * Call the "watch" command for deletion. Take into account that the
     * receiver might change the text content inside the callback, although
     * he shouldn't do this.
     */

    if (triggerWatchDelete) {
	Tcl_Obj *delObj = TextGetText(textPtr, &index1, &index2, NULL, NULL, UINT_MAX, 0, 1);
	char const *deleted = Tcl_GetString(delObj);
	int unchanged;
	int rc;

	TkTextIndexSave(&index1);
	TkTextIndexSave(&index2);
	Tcl_IncrRefCount(delObj);
	rc = TriggerWatchEdit(textPtr, userFlag, "delete", &index1, &index2, deleted, final);
	Tcl_GuardedDecrRefCount(delObj);
	unchanged = TkTextIndexRebuild(&index1) && TkTextIndexRebuild(&index2);

	if (!rc) { return 0; } /* the receiver has destroyed this widget */

	if (!unchanged && TkTextIndexCompare(&index1, &index2) >= 0) {
	    /* This can only happen if the receiver of the trigger command did any modification. */
	    return 1;
	}
    }

    TkTextClearSelection(sharedTextPtr, &index1, &index3);

    /*
     * Tell the display what's about to happen, so it can discard obsolete
     * display information, then do the deletion. Also, if the deletion
     * involves the top line on the screen, then we have to reset the view
     * (the deletion will invalidate textPtr->topIndex). Compute what the new
     * first character will be, then do the deletion, then reset the view.
     */

    TkrTextChanged(sharedTextPtr, NULL, &index1, &index2);

    if (sharedTextPtr->numPeers > sizeof(textPosBuf)/sizeof(textPosBuf[0])) {
	textPosition = (TkTextPosition *)ckalloc(sizeof(textPosition[0])*sharedTextPtr->numPeers);
    } else {
	textPosition = textPosBuf;
    }
    InitPosition(sharedTextPtr, textPosition);
    FindNewTopPosition(sharedTextPtr, textPosition, &index1, &index2, 0);

    undoInfoPtr = TkTextUndoStackIsFull(sharedTextPtr->undoStack) ? NULL : &undoInfo;
    TkBTreeDeleteIndexRange(sharedTextPtr, &index1, &index2, flags, undoInfoPtr);

    /*
     * Push the deletion onto the undo stack, and update the modified status of the widget.
     * Try to join with previously pushed undo token, if possible.
     */

    if (undoInfoPtr) {
	const TkTextUndoSubAtom *subAtom;

	PushUndoSeparatorIfNeeded(sharedTextPtr, sharedTextPtr->autoSeparators, TK_TEXT_EDIT_DELETE);

	if (TkTextUndoGetMaxSize(sharedTextPtr->undoStack) == 0
		|| TkTextUndoGetCurrentSize(sharedTextPtr->undoStack) + undoInfo.byteSize
			<= TkTextUndoGetMaxSize(sharedTextPtr->undoStack)) {
	    if (sharedTextPtr->lastUndoTokenType != TK_TEXT_UNDO_DELETE
		    || !((subAtom = TkTextUndoGetLastUndoSubAtom((TkTextUndoStack)sharedTextPtr->undoStack))
			    && TkBTreeJoinUndoDelete((TkTextUndoToken *)subAtom->item, subAtom->size,
				    undoInfo.token, undoInfo.byteSize))) {
		TkTextPushUndoToken(sharedTextPtr, undoInfo.token, undoInfo.byteSize);
	    }
	    sharedTextPtr->lastUndoTokenType = TK_TEXT_UNDO_DELETE;
	    sharedTextPtr->prevUndoStartIndex =
		    ((TkTextUndoTokenRange *) undoInfo.token)->startIndex;
	    sharedTextPtr->prevUndoEndIndex = ((TkTextUndoTokenRange *) undoInfo.token)->endIndex;
	    /* stack has changed anyway, but TkBTreeJoinUndoDelete didn't trigger */
	    sharedTextPtr->undoStackEvent = 1;
	} else {
	    assert(undoInfo.token->undoType->destroyProc);
	    undoInfo.token->undoType->destroyProc(sharedTextPtr, undoInfo.token, 0);
	    ckfree(undoInfo.token);
	    DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
	}

	sharedTextPtr->lastEditMode = TK_TEXT_EDIT_DELETE;
    }

    UpdateModifiedFlag(sharedTextPtr, 1);
    TkTextUpdateAlteredFlag(sharedTextPtr);
    SetNewTopPosition(sharedTextPtr, textPtr, textPosition, viewUpdate);

    if (textPosition != textPosBuf) {
	ckfree(textPosition);
    }

    /*
     * Lastly, trigger the "watch" command for insertion. This must be the last action,
     * probably the receiver is calling some widget commands inside the callback.
     */

    if (triggerWatchInsert) {
	if (!TriggerWatchEdit(textPtr, userFlag, "insert", indexPtr1, indexPtr1, NULL, final)) {
	    return 0; /* widget has been destroyed */
	}
    }

    return 1;
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

static Tcl_Size
TextFetchSelection(
    void *clientData,	/* Information about text widget. */
    Tcl_Size offset,			/* Offset within selection of first character to be returned. */
    char *buffer,		/* Location in which to place selection. */
    Tcl_Size maxBytes)		/* Maximum number of bytes to place at buffer, not including
    				 * terminating NULL character. */
{
    TkText *textPtr = (TkText *)clientData;
    TkTextSearch *searchPtr;
    Tcl_Obj *selTextPtr;
    int numBytes;

    if ((!textPtr->exportSelection) || Tcl_IsSafe(textPtr->interp)) {
	return TCL_INDEX_NONE;
    }

    /*
     * Find the beginning of the next range of selected text. Note: if the
     * selection is being retrieved in multiple pieces (offset != 0) and some
     * modification has been made to the text that affects the selection then
     * reject the selection request (make 'em start over again).
     */

    if (offset == 0) {
	TkTextIndexSetupToStartOfText(&textPtr->selIndex, textPtr, textPtr->sharedTextPtr->tree);
	textPtr->abortSelections = 0;
    } else if (textPtr->abortSelections) {
	return 0;
    }

    searchPtr = &textPtr->selSearch;

    if (offset == 0 || !TkBTreeCharTagged(&textPtr->selIndex, textPtr->selTagPtr)) {
	TkTextIndex eof;

	TkTextIndexSetupToEndOfText(&eof, textPtr, textPtr->sharedTextPtr->tree);
	TkBTreeStartSearch(&textPtr->selIndex, &eof, textPtr->selTagPtr, searchPtr, SEARCH_NEXT_TAGON);
	if (!TkBTreeNextTag(searchPtr)) {
	    return offset == 0 ? TCL_INDEX_NONE : 0;
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

    while (1) {
	TextGetText(textPtr, &textPtr->selIndex, &searchPtr->curIndex, &textPtr->selIndex,
		selTextPtr, maxBytes - GetByteLength(selTextPtr), 1, 0);

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
    Tcl_GuardedDecrRefCount(selTextPtr);
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

    Tk_SendVirtualEvent(textPtr->tkwin, "Selection", NULL);
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
    void *clientData)	/* Information about text widget. */
{
    TkText *textPtr = (TkText *)clientData;

    if (Tk_AlwaysShowSelection(textPtr->tkwin)) {
	TkTextIndex start, end;

	if ((!textPtr->exportSelection) || Tcl_IsSafe(textPtr->interp)) {
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
		0, NULL, TkTextRedrawTag);
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
    void *clientData)	/* Pointer to record describing text. */
{
    TkText *textPtr = (TkText *)clientData;
    unsigned oldFlags = textPtr->flags;

    if (textPtr->state == TK_TEXT_STATE_DISABLED
	    || !(textPtr->flags & GOT_FOCUS)
	    || textPtr->insertOffTime == 0) {
	if (!(textPtr->flags & GOT_FOCUS) && textPtr->insertUnfocussed != TK_TEXT_INSERT_NOFOCUS_NONE) {
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
	int x, y, w, h;

	if (TkTextGetCursorBbox(textPtr, &x, &y, &w, &h)) {
	    int inset = textPtr->borderWidth + textPtr->highlightWidth;
	    TkTextRedrawRegion(textPtr, x + inset, y + inset, w, h);
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
    int viewUpdate,		/* Update the view if set. */
    int triggerWatchDelete,	/* Should we trigger the watch command for deletion? */
    int triggerWatchInsert,	/* Should we trigger the watch command for insertion? */
    int userFlag,		/* Trigger user modification? */
    int parseHyphens)		/* Should we parse hyphens? (tk_textInsert) */
{
    TkTextIndex index1, index2;
    TkSharedText *sharedTextPtr;
    TkTextTag *hyphenTagPtr = NULL;
    int rc = TCL_OK;
    Tcl_Size j;

    assert(textPtr);
    assert(!TkTextIsDeadPeer(textPtr));

    sharedTextPtr = textPtr->sharedTextPtr;

    if (parseHyphens && objc > 1 && *Tcl_GetString(objv[0]) == '-') {
	Tcl_Size argc;
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

    for (j = 0; j < (Tcl_Size)objc && GetByteLength(objv[j]) == 0; j += 2) {
	/* empty loop body */
    }
    index1 = *indexPtr;

    while (j < (Tcl_Size)objc) {
	Tcl_Obj *stringPtr = objv[j];
	Tcl_Obj *tagPtr = (j + 1 < (Tcl_Size)objc) ? objv[j + 1] : NULL;
	char const *string = Tcl_GetString(stringPtr);
	unsigned length = GetByteLength(stringPtr);
	size_t k = j + 2;
	int final;

	while (k < (size_t)objc && GetByteLength(objv[k]) == 0) {
	    k += 2;
	}
	final = (size_t)objc <= k;

	if (length > 0) {
	    Tcl_Size numTags = 0;
	    Tcl_Obj **tagNamePtrs = NULL;
	    TkTextTagSet *tagInfoPtr = NULL;

	    /*
	     * Call the "watch" command for deletion. Take into account that the
	     * receiver might change the text content, although he shouldn't do this.
	     */

	    if (triggerWatchDelete) {
		TkTextIndexSave(&index1);
		if (!TriggerWatchEdit(textPtr, userFlag, "delete", &index1, &index1, NULL, final)) {
		    return rc;
		}
		TkTextIndexRebuild(&index1);
	    }

	    if (tagPtr) {
		Tcl_Size i;

		if (Tcl_ListObjGetElements(interp, tagPtr, &numTags, &tagNamePtrs) != TCL_OK) {
		    rc = TCL_ERROR;
		} else if (numTags > 0) {
		    TkTextTag *tTagPtr;

		    tagInfoPtr = TkTextTagSetResize(NULL, sharedTextPtr->tagInfoSize);

		    for (i = 0; i < numTags; ++i) {
			tTagPtr = TkTextCreateTag(textPtr, Tcl_GetString(tagNamePtrs[i]), NULL);
			if (tTagPtr->index >= TkTextTagSetSize(tagInfoPtr)) {
			    tagInfoPtr = TkTextTagSetResize(tagInfoPtr, sharedTextPtr->tagInfoSize);
			}
			tagInfoPtr = TkTextTagSetAddToThis(tagInfoPtr, tTagPtr->index);
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
		if (!TriggerWatchEdit(textPtr, userFlag, "insert", &index1, &index2, string, final)) {
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
	TK_TEXT_SEARCH_HIDDEN, TK_TEXT_SEARCH_END, TK_TEXT_SEARCH_ALL, TK_TEXT_SEARCH_BACK,
	TK_TEXT_SEARCH_COUNT, TK_TEXT_SEARCH_DISCARDHYPHENS, TK_TEXT_SEARCH_ELIDE, TK_TEXT_SEARCH_EXACT,
	TK_TEXT_SEARCH_FWD, TK_TEXT_SEARCH_NOCASE, TK_TEXT_SEARCH_NOLINESTOP, TK_TEXT_SEARCH_OVERLAP,
	TK_TEXT_SEARCH_REGEXP, TK_TEXT_SEARCH_STRICTLIMITS
    };

    /*
     * Set up the search specification, including the last 4 fields which are
     * text widget specific.
     */

    searchSpec.textPtr = textPtr;
    searchSpec.exact = 1;
    searchSpec.noCase = 0;
    searchSpec.all = 0;
    searchSpec.backwards = 0;
    searchSpec.varPtr = NULL;
    searchSpec.countPtr = NULL;
    searchSpec.resPtr = NULL;
    searchSpec.searchElide = 0;
    searchSpec.searchHyphens = 1;
    searchSpec.noLineStop = 0;
    searchSpec.overlap = 0;
    searchSpec.strictLimits = 0;
    searchSpec.numLines = TkrBTreeNumLines(textPtr->sharedTextPtr->tree, textPtr);
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
	case TK_TEXT_SEARCH_END:
	    i += 1;
	    goto endOfSwitchProcessing;
	case TK_TEXT_SEARCH_ALL:
	    searchSpec.all = 1;
	    break;
	case TK_TEXT_SEARCH_BACK:
	    searchSpec.backwards = 1;
	    break;
	case TK_TEXT_SEARCH_COUNT:
	    if (i >= objc - 1) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("no value given for \"-count\" option", TCL_INDEX_NONE));
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
	case TK_TEXT_SEARCH_DISCARDHYPHENS:
	    searchSpec.searchHyphens = 0;
	    break;
	case TK_TEXT_SEARCH_ELIDE:
	case TK_TEXT_SEARCH_HIDDEN:
	    searchSpec.searchElide = 1;
	    break;
	case TK_TEXT_SEARCH_EXACT:
	    searchSpec.exact = 1;
	    break;
	case TK_TEXT_SEARCH_FWD:
	    searchSpec.backwards = 0;
	    break;
	case TK_TEXT_SEARCH_NOCASE:
	    searchSpec.noCase = 1;
	    break;
	case TK_TEXT_SEARCH_NOLINESTOP:
	    searchSpec.noLineStop = 1;
	    break;
	case TK_TEXT_SEARCH_OVERLAP:
	    searchSpec.overlap = 1;
	    break;
	case TK_TEXT_SEARCH_STRICTLIMITS:
	    searchSpec.strictLimits = 1;
	    break;
	case TK_TEXT_SEARCH_REGEXP:
	    searchSpec.exact = 0;
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
		"the \"-nolinestop\" option requires the \"-regexp\" option to be present", TCL_INDEX_NONE));
	Tcl_SetErrorCode(interp, "TK", "TEXT", "SEARCH_USAGE", NULL);
	return TCL_ERROR;
    }

    if (searchSpec.overlap && !searchSpec.all) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"the \"-overlap\" option requires the \"-all\" option to be present", TCL_INDEX_NONE));
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
    }

  cleanup:
    if (searchSpec.countPtr) {
	Tcl_GuardedDecrRefCount(searchSpec.countPtr);
    }
    if (searchSpec.resPtr) {
	Tcl_GuardedDecrRefCount(searchSpec.resPtr);
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
 *	This means we ignore any embedded windows/images and elided text
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
    TkText *textPtr = (TkText *)searchSpecPtr->clientData;
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
 *	This means we ignore any embedded windows/images and elided text
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
    TkText *textPtr = (TkText *)searchSpecPtr->clientData;
    TkTextLine *startLinePtr = textPtr->startMarker->sectionPtr->linePtr;
    int isCharSeg;

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

static void *
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
    TkText *textPtr = (TkText *)searchSpecPtr->clientData;
    TkTextLine *startLinePtr = textPtr->startMarker->sectionPtr->linePtr;
    TkTextLine *endLinePtr = textPtr->endMarker->sectionPtr->linePtr;
    int nothingYet = 1;

    /*
     * Extract the text from the line.
     */

    if (!(linePtr = TkBTreeFindLine(textPtr->sharedTextPtr->tree, textPtr, lineNum))) {
	return NULL;
    }
    thisLinePtr = linePtr;

    while (thisLinePtr) {
	int elideWraps = 0;

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
			elideWraps = 1;
		    }
		} else if (segPtr->typePtr == &tkTextCharType) {
		    Tcl_AppendToObj(theLine, segPtr->body.chars, segPtr->size);
		    nothingYet = 0;
		} else {
		    Tcl_AppendToObj(theLine, "\xc2\xad", 2); /* U+00AD */
		    nothingYet = 0;
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

static int
TextSearchFoundMatch(
    int lineNum,		/* Line on which match was found. */
    SearchSpec *searchSpecPtr,	/* Search parameters. */
    void *clientData,	/* Token returned by the 'addNextLineProc', TextSearchAddNextLine.
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
    TkText *textPtr = (TkText *)searchSpecPtr->clientData;

    if (lineNum == searchSpecPtr->stopLine) {
	/*
	 * If the current index is on the wrong side of the stopIndex, then
	 * the item we just found is actually outside the acceptable range,
	 * and the search is over.
	 */

	if (searchSpecPtr->backwards ^ (matchOffset >= searchSpecPtr->stopOffset)) {
	    return 0;
	}
    }

    /*
     * Calculate the character count, which may need augmenting if there are
     * embedded windows or elided text.
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
	    return 0;
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

    linePtr = (TkTextLine *)clientData;
    if (!linePtr) {
	linePtr = TkBTreeFindLine(textPtr->sharedTextPtr->tree, textPtr, lineNum);
    }
    startLinePtr = textPtr->startMarker->sectionPtr->linePtr;

    /*
     * Find the starting point.
     */

    leftToScan = matchOffset;
    while (1) {
	/*
	 * Note that we allow leftToScan to be zero because we want to skip
	 * over any preceding non-textual items.
	 */

	segPtr = (linePtr == startLinePtr) ? textPtr->startMarker : linePtr->segPtr;

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
	TkrTextMakeByteIndex(textPtr->sharedTextPtr->tree, textPtr, lineNum, matchOffset, &foundIndex);
    } else {
	TkTextMakeCharIndex(textPtr->sharedTextPtr->tree, textPtr, lineNum, matchOffset, &foundIndex);
    }

    if (searchSpecPtr->all) {
	if (!searchSpecPtr->resPtr) {
	    Tcl_IncrRefCount(searchSpecPtr->resPtr = Tcl_NewObj());
	}
	Tcl_ListObjAppendElement(NULL, searchSpecPtr->resPtr, TkTextNewIndexObj(&foundIndex));
    } else {
	Tcl_IncrRefCount(searchSpecPtr->resPtr = TkTextNewIndexObj(&foundIndex));
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
	int isCharSeg;

	if (!segPtr) {
	    /*
	     * We are on the next line - this of course should only ever
	     * happen with searches which have matched across multiple lines.
	     */

	    assert(TkBTreeNextLine(textPtr, linePtr));
	    linePtr = linePtr->nextPtr;
	    segPtr = linePtr->segPtr;
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
    Tcl_Size objc, i, count;
    Tcl_Obj **objv;
    TkTextTabArray *tabArrayPtr;
    TkTextTab *tabPtr;
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

    tabArrayPtr = (TkTextTabArray *)ckalloc(sizeof(TkTextTabArray) + (count - 1)*sizeof(TkTextTab));
    tabArrayPtr->numTabs = 0;
    prevStop = 0.0;
    lastStop = 0.0;
    for (i = 0, tabPtr = &tabArrayPtr->tabs[0]; i < (Tcl_Size)objc; i++, tabPtr++) {
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

	{ /* local scope */
#if 0 && TCL_UTF_MAX > 4
	    /*
	     * HACK: Support of pseudo UTF-8 strings. Needed because of this
	     * bad hack with TCL_UTF_MAX > 4, the whole thing is amateurish.
	     * (See function GetLineBreakFunc() about the very severe problems
	     * with TCL_UTF_MAX > 4).
	     */

	    int ch;
	    TkUtfToUniChar(Tcl_GetString(objv[i + 1]), &ch);
#else
	    /*
	     * Proper implementation for UTF-8 strings:
	     */

	    Tcl_UniChar ch;
	    Tcl_UtfToUniChar(Tcl_GetString(objv[i + 1]), &ch);
#endif
	    if (!Tcl_UniCharIsAlpha(ch)) {
		continue;
	    }
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
    ckfree(tabArrayPtr);
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
    unsigned complete,		/* Complete options (-complete) */
    unsigned *what,		/* Store flags here. */
    int *lastArg,		/* Store index of last used argument, can be NULL. */
    TkTextIndex *index1,	/* Store first index here. */
    TkTextIndex *index2,	/* Store second index here. */
    Tcl_Obj **command)		/* Store command here, can be NULL. */
{
    static const char *const optStrings[] = {
	"-all", "-bindings", "-chars", "-command", "-complete", "-configurations",
	"-displaychars", "-displaytext", "-dontresolvecolors",
	"-dontresolvefonts", "-elide", "-image", "-includedbconfig",
	"-includedefaultconfig", "-includeselection", "-includesyscolors",
	"-includesysconfig", "-insertmark", "-mark", "-nested", "-node",
	"-setup", "-tag", "-text", "-window",
	NULL
    };
    enum opts {
	DUMP_ALL, DUMP_TAG_BINDINGS, DUMP_CHARS, DUMP_CMD, DUMP_COMPLETE, DUMP_TAG_CONFIGS,
	DUMP_DISPLAY_CHARS, DUMP_DISPLAY_TEXT, DUMP_DONT_RESOLVE_COLORS,
	DUMP_DONT_RESOLVE_FONTS, DUMP_ELIDE, DUMP_IMG, DUMP_INCLUDE_DATABASE_CONFIG,
	DUMP_INCLUDE_DEFAULT_CONFIG, DUMP_INCLUDE_SEL, DUMP_INCLUDE_SYSTEM_COLORS,
	DUMP_INCLUDE_SYSTEM_CONFIG, DUMP_INSERT_MARK, DUMP_MARK, DUMP_NESTED, DUMP_NODE,
	DUMP_TEXT_CONFIGS, DUMP_TAG, DUMP_TEXT, DUMP_WIN
    };
    static const unsigned dumpFlags[] = {
	0, TK_DUMP_TAG_BINDINGS, TK_DUMP_CHARS, 0, TK_DUMP_INSPECT_COMPLETE, TK_DUMP_TAG_CONFIGS,
	TK_DUMP_DISPLAY_CHARS, TK_DUMP_DISPLAY_TEXT, TK_DUMP_DONT_RESOLVE_COLORS,
	TK_DUMP_DONT_RESOLVE_FONTS, TK_DUMP_ELIDE, TK_DUMP_IMG, TK_DUMP_INCLUDE_DATABASE_CONFIG,
	TK_DUMP_INCLUDE_DEFAULT_CONFIG, TK_DUMP_INCLUDE_SEL, TK_DUMP_INCLUDE_SYSTEM_COLORS,
	TK_DUMP_INCLUDE_SYSTEM_CONFIG, TK_DUMP_INSERT_MARK, TK_DUMP_MARK, TK_DUMP_NESTED, TK_DUMP_NODE,
	TK_DUMP_TEXT_CONFIGS, TK_DUMP_TAG, TK_DUMP_TEXT, TK_DUMP_WIN
    };

    int arg;
    size_t i;
    unsigned flags = 0;
    const char *myOptStrings[sizeof(optStrings)/sizeof(optStrings[0])];
    int myOptIndices[sizeof(optStrings)/sizeof(optStrings[0])];
    int myOptCount;

    assert(what);
    assert(!index1 == !index2);
    assert(DUMP_ALL == 0); /* otherwise next loop is wrong */
    assert(!complete || (complete & dflt) == dflt);

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
		sizeof(char *), "option", TCL_INDEX_TEMP_TABLE, &index) != TCL_OK) {
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
	CASE(INCLUDE_SEL);
	CASE(INSERT_MARK);
	CASE(TEXT_CONFIGS);
	CASE(TAG_BINDINGS);
	CASE(TAG_CONFIGS);
	CASE(DONT_RESOLVE_COLORS);
	CASE(DONT_RESOLVE_FONTS);
	CASE(INCLUDE_DEFAULT_CONFIG);
	CASE(INCLUDE_DATABASE_CONFIG);
	CASE(INCLUDE_SYSTEM_CONFIG);
	CASE(INCLUDE_SYSTEM_COLORS);
	CASE(IMG);
	CASE(WIN);
#undef CASE
	case DUMP_ALL:
	    *what = dflt;
	    break;
	case DUMP_COMPLETE:
	    if (!complete)
		goto wrongArgs;
	    *what = complete;
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
	    0, &what, &lastArg, &index1, &index2, &command);
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
    if (TkrTextIndexBackBytes(textPtr, &index1, 1, &prevByteIndex) == 0) {
	unsigned epoch = textPtr->sharedTextPtr->inspectEpoch + 1;
	tagPtr = TkBTreeGetTags(&prevByteIndex, TK_TEXT_SORT_NONE, NULL);
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

static int
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
    int textChanged = 0;
    char *buffer = NULL;
    int eol;

    sharedTextPtr = textPtr->sharedTextPtr;

    if (!*prevTagPtr && (startByte > 0 || linePtr != TkBTreeGetStartLine(textPtr))) {
	/*
	 * If this is the first line to dump, and we are not at start of line,
	 * then we need the preceding tag information.
	 */

	TkTextTag *tagPtr, *tPtr;
	unsigned epoch = sharedTextPtr->inspectEpoch;

	TkTextIndexClear(&index, textPtr);
	TkTextIndexSetByteIndex2(&index, linePtr, startByte);
	TkBTreeMoveBackward(&index, 1);
	segPtr = TkTextIndexGetContentSegment(&index, NULL);
	assert(segPtr);
	tagPtr = TkBTreeGetSegmentTags(textPtr->sharedTextPtr, segPtr, textPtr, TK_TEXT_SORT_NONE, NULL);
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
		TkTextTag *tagPtr = TkBTreeGetSegmentTags(sharedTextPtr, segPtr, textPtr,
			TK_TEXT_SORT_ASCENDING, NULL);
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
			    TkrTextMakeByteIndex(sharedTextPtr->tree, textPtr, lineno, offset, &index);
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
			TkrTextMakeByteIndex(sharedTextPtr->tree, textPtr, lineno, offset, &index);
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
			    buffer = (char *)ckrealloc(buffer, bufSize);
			}

			memcpy(buffer, segPtr->body.chars + first, length);
			buffer[length] = '\0';

			TkrTextMakeByteIndex(sharedTextPtr->tree, textPtr, lineno,
				offset + first, &index);
			if (!DumpSegment(textPtr, interp, "text", buffer, command, &index, what)) {
			    goto textChanged;
			}
		    } else {
			TkrTextMakeByteIndex(sharedTextPtr->tree, textPtr, lineno,
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
			TkrTextMakeByteIndex(sharedTextPtr->tree, textPtr, lineno, offset, &index);
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
	textChanged = 1;

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

    ckfree(buffer);
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

    result = GetDumpFlags(textPtr, interp, objc, objv, TK_DUMP_CRC_ALL, TK_DUMP_CRC_DFLT, 0,
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
	tagArrPtr = (TkTextTag **)ckalloc(sizeof(tagArrPtr[0])*sharedTextPtr->numTags);
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
		crc = ComputeChecksum(crc, "\xff\x01", 2);
		crc = ComputeChecksum(crc, segPtr->body.chars, segPtr->size);
	    }
	    break;
	case SEG_GROUP_HYPHEN:
	    if (what & SEG_GROUP_HYPHEN) {
		crc = ComputeChecksum(crc, "\xff\x02", 2);
	    }
	    break;
	case SEG_GROUP_WINDOW:
	    if ((what & SEG_GROUP_WINDOW)) {
		crc = ComputeChecksum(crc, "\xff\x03", 2);
		crc = ComputeChecksum(crc, Tk_PathName(segPtr->body.ew.tkwin), 0);
	    }
	    break;
	case SEG_GROUP_IMAGE:
	    if ((what & SEG_GROUP_IMAGE) && segPtr->body.ei.name) {
		crc = ComputeChecksum(crc, "\xff\x04", 2);
		crc = ComputeChecksum(crc, segPtr->body.ei.name, 0);
	    }
	    break;
	case SEG_GROUP_MARK:
	    if ((what & SEG_GROUP_MARK) && TkTextIsNormalMark(segPtr)) {
		const char *name;
		const char *signature;

		name = TkTextMarkName(sharedTextPtr, NULL, segPtr);
		signature = (segPtr->typePtr == &tkTextRightMarkType) ? "\xff\x05" : "\xff\x06";
		crc = ComputeChecksum(crc, signature, 2);
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
	ckfree(tagArrPtr);
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

static int
DumpSegment(
    TkText *textPtr,
    Tcl_Interp *interp,
    const char *key,		/* Segment type key. */
    const char *value,		/* Segment value. */
    Tcl_Obj *command,		/* Script callback. */
    const TkTextIndex *index,	/* index with line/byte position info. */
    TCL_UNUSED(int))			/* Look for TK_DUMP_INDEX bit. */
{
    char buffer[TK_POS_CHARS];
    Tcl_Obj *values[3], *tuple;

    TkrTextPrintIndex(textPtr, index, buffer);
    values[0] = Tcl_NewStringObj(key, TCL_INDEX_NONE);
    values[1] = Tcl_NewStringObj(value, TCL_INDEX_NONE);
    values[2] = Tcl_NewStringObj(buffer, TCL_INDEX_NONE);
    Tcl_IncrRefCount(tuple = Tcl_NewListObj(3, values));
    if (!command) {
	Tcl_ListObjAppendList(NULL, Tcl_GetObjResult(interp), tuple);
	Tcl_GuardedDecrRefCount(tuple);
	return 1;
    } else {
	Tcl_Size oldStateEpoch = TkBTreeEpoch(textPtr->sharedTextPtr->tree);
	Tcl_DString buf;
	int code;

	Tcl_DStringInit(&buf);
	Tcl_DStringAppend(&buf, Tcl_GetString(command), TCL_INDEX_NONE);
	Tcl_DStringAppend(&buf, " ", TCL_INDEX_NONE);
	Tcl_DStringAppend(&buf, Tcl_GetString(tuple), TCL_INDEX_NONE);
	code = Tcl_EvalEx(interp, Tcl_DStringValue(&buf), TCL_INDEX_NONE, 0);
	Tcl_DStringFree(&buf);
	if (code != TCL_OK) {
	    Tcl_AddErrorInfo(interp, "\n    (segment dumping command executed by text)");
	    Tcl_BackgroundException(interp, code);
	}
	Tcl_GuardedDecrRefCount(tuple);
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

static int
MatchColors(
    const char *name,
    int len,
    const char *hexColor,
    const char *colorName)
{
    assert(strlen(hexColor) == 13);
    assert(strlen(colorName) == 5);

    switch (len) {
    case 5:  return strncasecmp(name, colorName, 5) == 0;
    case 7:  return strncasecmp(name, hexColor,  7) == 0;
    case 13: return strncasecmp(name, hexColor, 13) == 0;
    }

    return 0;
}

static int
TestIfEqual(
    const char *opt1,
    int opt1Len,
    const char *opt2,
    int opt2Len)
{
    int i;

    if (MatchColors(opt1, opt1Len, "#ffffffffffff", "white")) {
	return MatchColors(opt2, opt2Len, "#ffffffffffff", "white");
    }
    if (MatchColors(opt1, opt1Len, "#000000000000", "black")) {
	return MatchColors(opt2, opt2Len, "#000000000000", "black");
    }
    if (opt1Len != opt2Len) {
	return 0;
    }
    for (i = 0; i < opt1Len; ++i) {
	if (opt1[i] != opt2[i]) {
	    return 0;
	}
    }
    return 1;
}

static int
IsPossibleColorOption(
    const char *s)
{
    unsigned len = strlen(s);

    assert(s[0] == '-');

    return (len >= 6 && strcmp(s + len - 5, "color") == 0)
	    || (len >= 7 && strcmp(s + len - 6, "ground") == 0);
}

void
TkTextInspectOptions(
    TkText *textPtr,
    const void *recordPtr,
    Tk_OptionTable optionTable,
    Tcl_DString *result,	/* should be already initialized */
    int flags)
{
    Tcl_Obj *objPtr;
    Tcl_Interp *interp = textPtr->interp;

    Tcl_DStringSetLength(result, 0);

    if ((objPtr = Tk_GetOptionInfo(interp, (char *) recordPtr, optionTable, NULL, textPtr->tkwin))) {
	Tcl_Obj **objv;
	Tcl_Size i, objc = 0;

	Tcl_ListObjGetElements(interp, objPtr, &objc, &objv);


	for (i = 0; i < objc; ++i) {
	    Tcl_Obj **argv;
	    Tcl_Size argc = 0;

	    Tcl_ListObjGetElements(interp, objv[i], &argc, &argv);

	    if (argc >= 5) { /* only if this option has a non-default value */
		Tcl_Obj *valObj = argv[4];
		Tcl_Obj *myValObj;
		Tcl_Obj *nameObj;
		int myFlags = flags;

		if (GetByteLength(valObj) == 0) {
		    continue;
		}

		if (!(myFlags & INSPECT_INCLUDE_DATABASE_CONFIG)
			|| myFlags & (INSPECT_INCLUDE_SYSTEM_CONFIG|INSPECT_INCLUDE_DEFAULT_CONFIG)) {
		    const char *name = Tcl_GetString(argv[1]);
		    const char *cls = Tcl_GetString(argv[2]);
		    Tk_Uid dfltUid = Tk_GetOption(textPtr->tkwin, name, cls);

		    if (dfltUid) {
			const char *value = Tcl_GetString(valObj);
			int valueLen = GetByteLength(valObj);

			if (TestIfEqual(dfltUid, strlen(dfltUid), value, valueLen)) {
			    if (!(myFlags & INSPECT_INCLUDE_DATABASE_CONFIG)) {
				continue;
			    }
			    myFlags |= INSPECT_INCLUDE_SYSTEM_CONFIG|INSPECT_INCLUDE_DEFAULT_CONFIG;
			}
		    }
		}

		if (!(myFlags & INSPECT_INCLUDE_SYSTEM_CONFIG)
			|| myFlags & INSPECT_INCLUDE_DEFAULT_CONFIG) {
		    const char *name = Tcl_GetString(argv[1]);
		    const char *cls = Tcl_GetString(argv[2]);
		    Tcl_Obj *dfltObj;

		    dfltObj = Tk_GetSystemDefault(textPtr->tkwin, name, cls);

		    if (dfltObj) {
			const char *dflt = Tcl_GetString(dfltObj);
			const char *value = Tcl_GetString(valObj);
			int dfltLen = GetByteLength(dfltObj);
			int valueLen = GetByteLength(valObj);

			if (TestIfEqual(dflt, dfltLen, value, valueLen)) {
			    if (!(myFlags & INSPECT_INCLUDE_SYSTEM_CONFIG)) {
				continue;
			    }
			    myFlags |= INSPECT_INCLUDE_DEFAULT_CONFIG;
			}
		    }
		}

		if (!(myFlags & INSPECT_INCLUDE_DEFAULT_CONFIG)) {
		    const char *dflt = Tcl_GetString(argv[3]);
		    const char *value = Tcl_GetString(valObj);
		    int dfltLen = GetByteLength(argv[3]);
		    int valueLen = GetByteLength(valObj);

		    if (TestIfEqual(dflt, dfltLen, value, valueLen)) {
			continue;
		    }
		}

		myValObj = valObj;
		nameObj = argv[0];
		if (Tcl_DStringLength(result) > 0) {
		    Tcl_DStringAppend(result, " ", 1);
		}
		Tcl_DStringAppend(result, Tcl_GetString(nameObj), GetByteLength(nameObj));
		Tcl_DStringAppend(result, " ", 1);

		if (!(flags & INSPECT_DONT_RESOLVE_FONTS)
			&& strcmp(Tcl_GetString(nameObj), "-font") == 0) {
		    const char *s = Tcl_GetString(valObj);
		    unsigned len = GetByteLength(valObj);

		    /*
		     * Don't resolve font names like TkFixedFont, TkTextFont, etc.
		     */

		    if (len < 7
			    || strncmp(s, "Tk", 2) != 0
			    || strncmp(s + len - 4, "Font", 4) != 0) {
			Tk_Font tkfont = Tk_AllocFontFromObj(interp, textPtr->tkwin, valObj);

			if (tkfont) {
			    Tcl_IncrRefCount(myValObj = Tk_FontGetDescription(tkfont));
			    Tk_FreeFont(tkfont);
			}
		    }
		} else if ((flags & (INSPECT_DONT_RESOLVE_COLORS|INSPECT_INCLUDE_SYSTEM_COLORS)) !=
			    (INSPECT_DONT_RESOLVE_COLORS|INSPECT_INCLUDE_SYSTEM_COLORS)
			&& IsPossibleColorOption(Tcl_GetString(nameObj))) {
		    const char *colorName = Tcl_GetString(valObj);

		    if (strncasecmp(colorName, "system", 6) == 0) {
			XColor *col;

			if (!(flags & INSPECT_INCLUDE_SYSTEM_COLORS)) {
			    continue;
			}

			/*
			 * The color lookup expects a lowercase "system", but the defaults
			 * are providing the uppercase form "System", so we need to build
			 * a lowercase form.
			 */

			col = Tk_GetColor(interp, textPtr->tkwin, colorName);

			if (col) {
			    myValObj = Tcl_ObjPrintf("#%02x%02x%02x", col->red, col->green, col->blue);
			    Tcl_IncrRefCount(myValObj);
			    Tk_FreeColor(col);
			} else {
			    /*
			     * This should not happen. We will clear the error result, and
			     * print a warning.
			     */
			    Tcl_SetObjResult(interp, Tcl_NewObj());
			    Tcl_SetObjErrorCode(interp, Tcl_NewObj());
			    fprintf(stderr, "tk::text: couldn't resolve system color '%s'\n", colorName);
			}
		    }
		}

		Tcl_DStringAppendElement(result, Tcl_GetString(myValObj));

		if (myValObj != valObj) {
		    Tcl_GuardedDecrRefCount(myValObj);
		}
	    }
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
    Tcl_Size argc, i;

    Tk_GetAllBindings(interp, bindingTable, (void *)name);
    Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp), &argc, &argv);
    Tcl_DStringInit(&str2);

    for (i = 0; i < argc; ++i) {
	const char *event = Tcl_GetString(argv[i]);
	const char *binding = Tk_GetBinding(interp, bindingTable, (void *)name, event);
	char *p;

	Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp), &argc, &argv);

	Tcl_DStringStartSublist(str);
	Tcl_DStringAppendElement(str, "bind");
	Tcl_DStringAppendElement(str, name);
	Tcl_DStringAppendElement(str, event);

	Tcl_DStringSetLength(&str2, 0);
	p = (char *)strchr(binding, '\n');
	while (p) {
	    Tcl_DStringAppend(&str2, binding, p - binding);
	    Tcl_DStringAppend(&str2, "; ", 2);
	    binding = p + 1;
	    p = (char *)strchr(binding, '\n');
	}
	Tcl_DStringAppend(&str2, binding, TCL_INDEX_NONE);

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
    int closeSubList;
    int result;
    int flags;

    result = GetDumpFlags(textPtr, interp, objc, objv, TK_DUMP_INSPECT_ALL, TK_DUMP_INSPECT_DFLT,
	    TK_DUMP_INSPECT_COMPLETE, &what, NULL, NULL, NULL, NULL);
    if (result != TCL_OK) {
	return result;
    }

    Tcl_DStringInit(str);
    Tcl_DStringInit(opts);
    sharedTextPtr = textPtr->sharedTextPtr;
    epoch = sharedTextPtr->inspectEpoch;
    tagPtr = textPtr->selTagPtr; /* any non-null value */
    nextPtr = textPtr->startMarker;
    closeSubList = 0;
    prevTagPtr = NULL;
    prevPtr = NULL;
    tagArrSize = 128;
    tagArray = (TkTextTag **)ckalloc(tagArrSize * sizeof(tagArray[0]));
    flags = 0;

    if (what & TK_DUMP_DONT_RESOLVE_FONTS)      { flags |= INSPECT_DONT_RESOLVE_FONTS; }
    if (what & TK_DUMP_DONT_RESOLVE_COLORS)     { flags |= INSPECT_DONT_RESOLVE_COLORS; }
    if (what & TK_DUMP_INCLUDE_DATABASE_CONFIG) { flags |= INSPECT_INCLUDE_DATABASE_CONFIG; }
    if (what & TK_DUMP_INCLUDE_SYSTEM_CONFIG)   { flags |= INSPECT_INCLUDE_SYSTEM_CONFIG; }
    if (what & TK_DUMP_INCLUDE_DEFAULT_CONFIG)  { flags |= INSPECT_INCLUDE_DEFAULT_CONFIG; }
    if (what & TK_DUMP_INCLUDE_SYSTEM_COLORS)   { flags |= INSPECT_INCLUDE_SYSTEM_COLORS; }

    assert(textPtr->selTagPtr->textPtr == textPtr);

    if (!(what & TK_DUMP_INCLUDE_SEL)) {
	/* this little trick is discarding the "sel" tag */
	textPtr->selTagPtr->textPtr = (TkText *) textPtr->selTagPtr;
    }

    if (what & TK_DUMP_TEXT_CONFIGS) {
	assert(textPtr->optionTable);
	TkTextInspectOptions(textPtr, textPtr, textPtr->optionTable, opts, flags);
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
	    tagPtr = tags[i];

	    if (tagPtr && ((what & TK_DUMP_INCLUDE_SEL) || !tagPtr->isSelTag)) {
		assert(tagPtr->optionTable);
		TkTextInspectOptions(textPtr, tagPtr, tagPtr->optionTable, opts, flags);
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
	    tagPtr = tags[i];

	    if (tagPtr
		    && sharedTextPtr->tagBindingTable
		    && ((what & TK_DUMP_INCLUDE_SEL) || !tagPtr->isSelTag)) {
		GetBindings(textPtr, tagPtr->name, sharedTextPtr->tagBindingTable, str);
	    }
	}
    }

    do {
	TkTextSegment *segPtr = nextPtr;
	unsigned group = segPtr->typePtr->group;
	const char *value = NULL;
	const char *type = NULL;
	int printTags = 0;

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
	    assert(segPtr->body.ei.optionTable);
	    TkTextInspectOptions(textPtr, &segPtr->body.ei, segPtr->body.ei.optionTable, opts, 0);
	    value = Tcl_DStringValue(opts);
	    printTags = !!(what & TK_DUMP_TAG);
	    break;
	case SEG_GROUP_WINDOW:
	    if (!(what & SEG_GROUP_WINDOW)) {
		continue;
	    }
	    type = "window";
	    assert(segPtr->body.ew.optionTable);
	    TkTextInspectOptions(textPtr, &segPtr->body.ew, segPtr->body.ew.optionTable, opts, 0);
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
		    tagPtr = TkBTreeGetSegmentTags(sharedTextPtr, segPtr->sectionPtr->linePtr->lastPtr,
			    textPtr, TK_TEXT_SORT_ASCENDING, NULL);
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
			tagPtr = TkBTreeGetSegmentTags(sharedTextPtr, segPtr, textPtr,
				TK_TEXT_SORT_ASCENDING, NULL);
		    }
		} else {
		    type = "text";
		    if (segPtr->size > 1 && segPtr->body.chars[segPtr->size - 1] == '\n') {
			nextPtr = segPtr; /* repeat this char segment */
			segPtr->body.chars[segPtr->size - 1] = '\0';
		    }
		    value = segPtr->body.chars;
		    if (printTags) {
			tagPtr = TkBTreeGetSegmentTags(sharedTextPtr, segPtr, textPtr,
				TK_TEXT_SORT_ASCENDING, NULL);
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
			    tagArray = (TkTextTag **)ckrealloc(tagArray, tagArrSize * sizeof(tagArray[0]));
			}
			tagArray[numTags++] = prevTagPtr;
			prevTagPtr->flag = 0; /* mark as closed */
		    }
		}

		Tcl_DStringStartSublist(str);
		for (i = 0; i < numTags; ++i) {
		    Tcl_DStringAppendElement(str, tagArray[i]->name);
		}
		Tcl_DStringEndSublist(str);
	    }

	    prevTagPtr = NULL;
	    closeSubList = 0;
	    Tcl_DStringEndSublist(str);
	}

	if (type) {
	    Tcl_DStringStartSublist(str);
	    Tcl_DStringAppendElement(str, type);
	    if (value) {
		Tcl_DStringAppendElement(str, value);
	    }
	    closeSubList = 1;

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
				tagArray = (TkTextTag **)ckrealloc(tagArray, tagArrSize * sizeof(tagArray[0]));
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
			    tagArray = (TkTextTag **)ckrealloc(tagArray, tagArrSize * sizeof(tagArray[0]));
			}
			tagArray[numTags++] = tPtr;
		    }
		}

		Tcl_DStringStartSublist(str);
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
    ckfree(tagArray);

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
	size_t i;
	Tcl_Size len;

	for (i = 0; i < sharedTextPtr->undoTagListCount; ++i) {
	    TkTextInspectUndoTagItem(sharedTextPtr, sharedTextPtr->undoTagList[i], resultPtr);
	}

	for (i = 0; i < sharedTextPtr->undoMarkListCount; ++i) {
	    TkTextInspectUndoMarkItem(sharedTextPtr, &sharedTextPtr->undoMarkList[i], resultPtr);
	}

	Tcl_ListObjLength(NULL, resultPtr, &len);
	if (len == 0) {
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
    assert(token);
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
    int setModified;
    int oldModified;
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
	static int warnDeprecated = 1;
	int canRedo = 0;

	if (warnDeprecated) {
	    warnDeprecated = 0;
	    fprintf(stderr, "tk::text: Command \"edit canredo\" is deprecated, "
		    "please use \"edit info\".\n");
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
	static int warnDeprecated = 1;
	int canUndo = 0;

	if (warnDeprecated) {
	    warnDeprecated = 0;
	    fprintf(stderr, "tk::text: Command \"edit canundo\" is deprecated, "
		    "please use \"edit info\".\n");
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
	if (objc != 3 && objc != 4 && (objc != 5 || strcmp(Tcl_GetString(objv[3]), "--") != 0)) {
	    /* NOTE: avoid trigraph */
	    Tcl_WrongNumArgs(interp, 3, objv, "\?\?--\? array? | ?-option?");
	    return TCL_ERROR;
	} else if (objc == 4 && *Tcl_GetString(objv[3]) == '-') {
	    Tcl_Obj* infoObj = GetEditInfo(interp, textPtr, objv[3]);
	    if (!infoObj) {
		return TCL_ERROR;
	    }
	    Tcl_SetObjResult(textPtr->interp, infoObj);
	} else {
	    Tcl_Obj* arrObj = (objc == 5 ? objv[4] : (objc == 4 ? objv[3] : NULL));
	    Tcl_SetObjResult(textPtr->interp, MakeEditInfo(interp, textPtr, arrObj));
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
	} else if (Tcl_GetBooleanFromObj(interp, objv[3], &setModified) != TCL_OK) {
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

	assert(setModified == 1 || setModified == 0);

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
    case EDIT_REDO: {
	int result;

	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, NULL);
	    return TCL_ERROR;
	}

	if (TestIfDisabled(interp, textPtr, &result))
	    return result;

	if (sharedTextPtr->undoStack) {
	    /*
	     * It's possible that this command command will be invoked inside the "watch" callback,
	     * but this is not allowed when performing undo/redo.
	     */

	    if (TestIfPerformingUndoRedo(interp, sharedTextPtr, NULL))
		return TCL_ERROR;

	    PushRetainedUndoTokens(sharedTextPtr);

	    if (TkTextUndoGetCurrentRedoStackDepth(sharedTextPtr->undoStack) == 0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("nothing to redo", TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "NO_REDO", NULL);
		return TCL_ERROR;
	    }

	    TkTextUndoDoRedo(sharedTextPtr->undoStack);
	}
	break;
    }
    case EDIT_RESET:
	if (objc == 3) {
	    if (sharedTextPtr->undoStack) {
		/*
		 * It's possible that this command command will be invoked inside the "watch" callback,
		 * but this is not allowed when performing undo/redo.
		 */
		if (TestIfPerformingUndoRedo(interp, sharedTextPtr, NULL))
		    return TCL_ERROR;

		TkTextUndoClearStack(sharedTextPtr->undoStack);
		sharedTextPtr->undoLevel = 0;
		sharedTextPtr->pushSeparator = 0;
		sharedTextPtr->isAltered = 0;
		sharedTextPtr->isIrreversible = 0;
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
		if (TestIfPerformingUndoRedo(interp, sharedTextPtr, NULL))
		    return TCL_ERROR;

		if (stack[0] == 'u') {
		    TkTextUndoClearUndoStack(sharedTextPtr->undoStack);
		    sharedTextPtr->undoLevel = 0;
		    sharedTextPtr->pushSeparator = 0;
		    sharedTextPtr->isAltered = 0;
		    sharedTextPtr->isIrreversible = 0;
		    TkTextUpdateAlteredFlag(sharedTextPtr);
		} else {
		    TkTextUndoClearRedoStack(sharedTextPtr->undoStack);
		}
	    }
	    return TCL_ERROR;
	}
	break;
    case EDIT_SEPARATOR: {
	int immediately = 0;

	if (objc == 4) {
	    if (strcmp(Tcl_GetString(objv[3]), "-immediately")) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad option \"%s\": must be -immediately", Tcl_GetString(objv[3])));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "INDEX_OPTION", NULL);
		return TCL_ERROR;
	    }
	    immediately = 1;
	} else if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, NULL);
	    return TCL_ERROR;
	}
	if (sharedTextPtr->undoStack) {
	    sharedTextPtr->pushSeparator = 1;
	    if (immediately) {
		/* last two args are meaningless here */
		PushUndoSeparatorIfNeeded(sharedTextPtr, sharedTextPtr->autoSeparators,
			TK_TEXT_EDIT_OTHER);
	    }
	}
	break;
    }
    case EDIT_UNDO: {
	int result;

	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, NULL);
	    return TCL_ERROR;
	}

	if (TestIfDisabled(interp, textPtr, &result))
	    return result;

	if (sharedTextPtr->undoStack) {
	    /*
	     * It's possible that this command command will be invoked inside the "watch" callback,
	     * but this is not allowed when performing undo/redo.
	     */

	    if (TestIfPerformingUndoRedo(interp, sharedTextPtr, &result))
		return result;

	    PushRetainedUndoTokens(sharedTextPtr);

	    if (TkTextUndoGetCurrentUndoStackDepth(sharedTextPtr->undoStack) == 0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("nothing to undo", TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "NO_UNDO", NULL);
		return TCL_ERROR;
	    }

	    TkTextUndoDoUndo(sharedTextPtr->undoStack);
	}
	break;
    }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GetEditInfo --
 *
 *	Returns the value containing the "edit info -option" information.
 *
 * Results:
 *	Tcl_Obj containing the required information.
 *
 * Side effects:
 *	Some memory will be allocated.
 *
 *----------------------------------------------------------------------
 */
enum {
    INFO_BYTESIZE, INFO_GENERATEDMARKS, INFO_IMAGES, INFO_LINES, INFO_LINESPERNODE, INFO_MARKS,
    INFO_REDOBYTESIZE, INFO_REDOCOMMANDS, INFO_REDODEPTH, INFO_REDOSTACKSIZE, INFO_TAGS,
    INFO_TOTALBYTESIZE, INFO_TOTALLINES, INFO_UNDOBYTESIZE, INFO_UNDOCOMMANDS, INFO_UNDODEPTH,
    INFO_UNDOSTACKSIZE, INFO_USEDTAGS, INFO_VISIBLEIMAGES, INFO_VISIBLEWINDOWS, INFO_WINDOWS,
    INFO_LAST /* must be last item */
};
static const char *const editInfoStrings[] = {
    "-bytesize", "-generatedmarks", "-images", "-lines", "-linespernode", "-marks",
    "-redobytesize", "-redocommands", "-redodepth", "-redostacksize", "-tags",
    "-totalbytesize", "-totallines", "-undobytesize", "-undocommands", "-undodepth",
    "-undostacksize", "-usedtags", "-visibleimages", "-visiblewindows", "-windows", NULL
};

static void
MakeStackInfoValue(
    Tcl_Interp *interp,
    TkSharedText *sharedTextPtr,
    Tcl_Obj* resultPtr)
{
    TkTextUndoStack st = sharedTextPtr->undoStack;
    const TkTextUndoAtom *atom;
    int i;

    for (i = sharedTextPtr->undoTagListCount - 1; i >= 0; --i) {
	const TkTextTag *tagPtr = sharedTextPtr->undoTagList[i];

	if (tagPtr->recentTagAddRemoveToken && !tagPtr->recentTagAddRemoveTokenIsNull) {
	    Tcl_ListObjAppendElement(interp, resultPtr,
		    GetCommand(sharedTextPtr, tagPtr->recentTagAddRemoveToken));
	}
	if (tagPtr->recentChangePriorityToken && tagPtr->savedPriority != tagPtr->priority) {
	    Tcl_ListObjAppendElement(interp, resultPtr,
		    GetCommand(sharedTextPtr, tagPtr->recentChangePriorityToken));
	}
    }

    for (i = sharedTextPtr->undoMarkListCount - 1; i >= 0; --i) {
	const TkTextMarkChange *changePtr = &sharedTextPtr->undoMarkList[i];

	if (changePtr->setMark) {
	    Tcl_ListObjAppendElement(interp, resultPtr,
		    GetCommand(sharedTextPtr, changePtr->setMark));
	}
	if (changePtr->moveMark) {
	    Tcl_ListObjAppendElement(interp, resultPtr,
		    GetCommand(sharedTextPtr, changePtr->moveMark));
	}
	if (changePtr->toggleGravity) {
	    Tcl_ListObjAppendElement(interp, resultPtr,
		    GetCommand(sharedTextPtr, changePtr->toggleGravity));
	}
    }

    atom = TkTextUndoIsPerformingUndo(st) ?
	    TkTextUndoCurrentRedoAtom(st) : TkTextUndoCurrentUndoAtom(st);

    if (atom) {
	for (i = atom->arraySize - 1; i >= 0; --i) {
	    const TkTextUndoSubAtom *subAtom = atom->array + i;
	    TkTextUndoToken *token = (TkTextUndoToken *)subAtom->item;

	    Tcl_ListObjAppendElement(interp, resultPtr, GetCommand(sharedTextPtr, token));
	}
    }
}

static Tcl_Obj *
MakeEditInfoValue(
    Tcl_Interp *interp,
    TkText *textPtr,
    int optionIndex)
{
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    TkTextUndoStack st = sharedTextPtr->undoStack;

    assert(optionIndex >= 0);
    assert(optionIndex < INFO_LAST);

    switch (optionIndex) {
    case INFO_UNDOSTACKSIZE:
	return Tcl_NewIntObj(st ? TkTextUndoCountUndoItems(st) : 0);
    case INFO_REDOSTACKSIZE:
	return Tcl_NewIntObj(st ? TkTextUndoCountRedoItems(st) : 0);
    case INFO_UNDODEPTH:
	return Tcl_NewIntObj(st ? TkTextUndoGetCurrentUndoStackDepth(st) : 0);
    case INFO_REDODEPTH:
	return Tcl_NewIntObj(st ? TkTextUndoGetCurrentRedoStackDepth(st) : 0);
    case INFO_UNDOBYTESIZE:
	return Tcl_NewIntObj(st ? TkTextUndoGetCurrentUndoSize(st) : 0);
    case INFO_REDOBYTESIZE:
	return Tcl_NewIntObj(st ? TkTextUndoGetCurrentRedoSize(st) : 0);
    case INFO_BYTESIZE:
	return Tcl_NewIntObj(TkBTreeSize(sharedTextPtr->tree, textPtr));
    case INFO_TOTALBYTESIZE:
	return Tcl_NewIntObj(TkBTreeSize(sharedTextPtr->tree, NULL));
    case INFO_LINES:
	return Tcl_NewIntObj(TkrBTreeNumLines(sharedTextPtr->tree, textPtr));
    case INFO_TOTALLINES:
	return Tcl_NewIntObj(TkrBTreeNumLines(sharedTextPtr->tree, NULL));
    case INFO_IMAGES:
	return Tcl_NewIntObj(sharedTextPtr->numImages);
    case INFO_WINDOWS:
	return Tcl_NewIntObj(sharedTextPtr->numWindows);
    case INFO_VISIBLEIMAGES:
	return Tcl_NewIntObj(TkTextCountVisibleImages(textPtr));
    case INFO_VISIBLEWINDOWS:
	return Tcl_NewIntObj(TkTextCountVisibleWindows(textPtr));
    case INFO_TAGS:
	return Tcl_NewIntObj(sharedTextPtr->numTags);
    case INFO_USEDTAGS:
	return Tcl_NewIntObj(TkTextTagSetCount(TkBTreeRootTagInfo(sharedTextPtr->tree)));
    case INFO_MARKS:
	return Tcl_NewIntObj(sharedTextPtr->numMarks);
    case INFO_GENERATEDMARKS:
	return Tcl_NewIntObj(sharedTextPtr->numPrivateMarks);
    case INFO_LINESPERNODE:
	return Tcl_NewIntObj(TkBTreeLinesPerNode(sharedTextPtr->tree));
    case INFO_UNDOCOMMANDS: {
	Tcl_Obj* obj = Tcl_NewObj();
	if (st && !TkTextUndoIsPerformingUndo(st)) {
	    MakeStackInfoValue(interp, sharedTextPtr, obj);
	}
	return obj;
    }
    case INFO_REDOCOMMANDS: {
	Tcl_Obj* obj = Tcl_NewObj();
	if (st && TkTextUndoIsPerformingUndo(st)) {
	    MakeStackInfoValue(interp, sharedTextPtr, obj);
	}
	return obj;
    }
    }

    return NULL; /* never reached */
}

static Tcl_Obj *
GetEditInfo(
    Tcl_Interp *interp,		/* Current interpreter. */
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Obj *option)		/* Name of resource. */
{
    int optionIndex;

    if (Tcl_GetIndexFromObjStruct(interp, option, editInfoStrings,
	    sizeof(char *), "option", 0, &optionIndex) != TCL_OK) {
	return NULL;
    }

    return MakeEditInfoValue(interp, textPtr, optionIndex);
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
 *	Some memory will be allocated.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *
MakeEditInfo(
    Tcl_Interp *interp,		/* Current interpreter. */
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Obj *arrayPtr)		/* Name of array, may be NULL. */
{
    Tcl_Obj *var = arrayPtr ? arrayPtr : Tcl_NewStringObj("", 0);
    int i;

    Tcl_UnsetVar(interp, Tcl_GetString(var), 0);
    for (i = 0; i < INFO_LAST; ++i) {
	Tcl_ObjSetVar2(interp, var, Tcl_NewStringObj(editInfoStrings[i] + 1, TCL_INDEX_NONE),
		MakeEditInfoValue(interp, textPtr, i), 0);
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
    int visibleOnly,		/* If true, then only return non-elided characters. */
    int includeHyphens)	/* If true, then also include soft hyphens. */
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

    if (visibleOnly && TkTextSegmentIsElided(textPtr, lastPtr)) {
	index = *indexPtr2;
	TkTextSkipElidedRegion(&index);
	lastPtr = TkTextIndexGetContentSegment(&index, &offset2);
    }

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
		if (maxBytes < 2) {
		    return resultPtr;
		}
		Tcl_AppendToObj(resultPtr, "\xc2\xad", 2); /* U+00AD */
		if ((maxBytes -= 2u) == 0) {
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
		    if (maxBytes < 2) {
			return resultPtr;
		    }
		    Tcl_AppendToObj(resultPtr, "\xc2\xad", 2); /* U+00AD */
		    if ((maxBytes -= 2) == 0) {
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

static int
TriggerWatchEdit(
    TkText *textPtr,			/* Information about text widget. */
    int userFlag,			/* Trigger due to user modification? */
    const char *operation,		/* The triggering operation. */
    const TkTextIndex *indexPtr1,	/* Start index for deletion / insert. */
    const TkTextIndex *indexPtr2,	/* End index after insert / before deletion. */
    const char *string,			/* Deleted/inserted chars. */
    int final)				/* Flag indicating whether this is a final part. */
{
    TkSharedText *sharedTextPtr;
    TkText *peerArr[20];
    TkText **peers = peerArr;
    TkText *tPtr;
    unsigned i, n = 0;
    unsigned numPeers;
    int rc = 1;

    assert(textPtr->sharedTextPtr->triggerWatchCmd);
    assert(!indexPtr1 == !indexPtr2);
    assert(strcmp(operation, "insert") == 0 || strcmp(operation, "delete") == 0);

    sharedTextPtr = textPtr->sharedTextPtr;
    sharedTextPtr->triggerWatchCmd = 0; /* do not trigger recursively */
    numPeers = sharedTextPtr->numPeers;

    if (sharedTextPtr->numPeers > sizeof(peerArr) / sizeof(peerArr[0])) {
	peers = (TkText **)ckalloc(sharedTextPtr->numPeers * sizeof(peerArr[0]));
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
	tPtr = peers[i];

	if (tPtr->watchCmd && (userFlag || tPtr->triggerAlways) && !(tPtr->flags & DESTROYED)) {
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

		    TkrTextPrintIndex(tPtr, &index[0], idx[0]);
		    TkrTextPrintIndex(tPtr, &index[1], idx[1]);

		    Tcl_DStringInit(&buf);
		    Tcl_DStringAppendElement(&buf, string);

		    tagPtr = NULL;
		    if (TkTextIndexBackChars(tPtr, &index[0], 1, &myIndex, COUNT_CHARS)) {
			tagPtr = TkBTreeGetTags(&myIndex, TK_TEXT_SORT_ASCENDING, NULL);
		    }
		    AppendTags(&buf, tagPtr);
		    AppendTags(&buf, TkBTreeGetTags(&index[1], TK_TEXT_SORT_ASCENDING, NULL));
		    AppendTags(&buf, cmp == 0 ? NULL :
			    TkBTreeGetTags(&index[0], TK_TEXT_SORT_ASCENDING, NULL));
		    if (*operation == 'd') {
			tagPtr = NULL;
			if (cmp && TkTextIndexBackChars(tPtr, &index[1], 1, &myIndex, COUNT_CHARS)) {
			    tagPtr = TkBTreeGetTags(&myIndex, TK_TEXT_SORT_ASCENDING, NULL);
			}
			AppendTags(&buf, tagPtr);
		    }
		    Tcl_DStringAppendElement(&buf, final ? "yes" : "no");
		    arg = Tcl_DStringValue(&buf);

		    if (!TkTextTriggerWatchCmd(tPtr, operation, idx[0], idx[1],
				arg, NULL, NULL, userFlag)
			    && tPtr == textPtr) {
			rc = 0; /* this widget has been destroyed */
		    }

		    Tcl_DStringFree(&buf);
		}
	    } else {
		if (!TkTextTriggerWatchCmd(textPtr, operation, NULL, NULL, NULL, NULL, NULL, userFlag)
			&& tPtr == textPtr) {
		    rc = 0; /* this widget has been destroyed */
		}
	    }
	}

	if (TkTextDecrRefCountAndTestIfDestroyed(tPtr)) {
	    numPeers -= 1;
	}
    }

    if (peers != peerArr) {
	ckfree(peers);
    }
    if (numPeers > 0) { /* otherwise sharedTextPtr is not valid anymore */
	sharedTextPtr->triggerWatchCmd = 1;
    }

    return rc;
}

/*
 *--------------------------------------------------------------
 *
 * TkTextPerformWatchCmd --
 *
 *	This function is performs triggering of the watch
 *	command for all peers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See function TkTextTriggerWatchCmd.
 *
 *--------------------------------------------------------------
 */

void
TkTextPerformWatchCmd(
    TkSharedText *sharedTextPtr,
    TkText *textPtr,			/* Firstly trigger watch command of this peer, can be NULL. */
    const char *operation,		/* The trigger operation. */
    TkTextWatchGetIndexProc index1Proc,	/* Function pointer for fst index, can be NULL. */
    void *index1ProcData,		/* Client data for index1Proc. */
    TkTextWatchGetIndexProc index2Proc,	/* Function pointer for snd index, can be NULL. */
    void *index2ProcData,		/* Client data for index2Proc. */
    const char *arg1,			/* 3rd argument for watch command, can be NULL. */
    const char *arg2,			/* 3rd argument for watch command, can be NULL. */
    const char *arg3,			/* 3rd argument for watch command, can be NULL. */
    TCL_UNUSED(int))			/* 4rd argument for watch command. */
{
    TkText *peerArr[20];
    TkText **peers = peerArr;
    TkText *tPtr;
    unsigned numPeers = 0;
    unsigned i;

    assert(sharedTextPtr);
    assert(sharedTextPtr->triggerWatchCmd);
    assert(operation);
    assert(!index2Proc || index1Proc);

    sharedTextPtr->triggerWatchCmd = 0; /* do not trigger recursively */

    if (sharedTextPtr->numPeers > sizeof(peerArr) / sizeof(peerArr[0])) {
	peers = (TkText **)ckalloc(sharedTextPtr->numPeers * sizeof(peerArr[0]));
    }
    if (textPtr) {
	peers[numPeers++] = textPtr;
	textPtr->refCount += 1;
    }
    for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
	if (tPtr != textPtr && tPtr->watchCmd) {
	    peers[numPeers++] = tPtr;
	    tPtr->refCount += 1;
	}
    }
    for (i = 0; i < numPeers; ++i) {
	tPtr = peers[i];

	if (!(tPtr->flags & DESTROYED)) {
	    char idx[2][TK_POS_CHARS];
	    TkTextIndex index[2];

	    if (index1Proc) {
		index1Proc(tPtr, &index[0], index1ProcData);
		TkrTextPrintIndex(tPtr, &index[0], idx[0]);

		if (index2Proc) {
		    index2Proc(tPtr, &index[1], index2ProcData);
		    TkrTextPrintIndex(tPtr, &index[1], idx[1]);
		} else {
		    memcpy(idx[1], idx[0], TK_POS_CHARS);
		}
	    }

	    TkTextTriggerWatchCmd(tPtr, operation, idx[0], idx[1], arg1, arg2, arg3, 0);
	}
    }

    sharedTextPtr->triggerWatchCmd = 1;

    for (i = 0; i < numPeers; ++i) {
	TkTextDecrRefCountAndTestIfDestroyed(peers[i]);
    }
    if (peers != peerArr) {
	ckfree(peers);
    }
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

int
TkTextTriggerWatchCmd(
    TkText *textPtr,		/* Information about text widget. */
    const char *operation,	/* The trigger operation. */
    const char *index1,		/* 1st argument for watch command, can be NULL. */
    const char *index2,		/* 2nd argument for watch command, can be NULL. */
    const char *arg1,		/* 3rd argument for watch command, can be NULL. */
    const char *arg2,		/* 3rd argument for watch command, can be NULL. */
    const char *arg3,		/* 3rd argument for watch command, can be NULL. */
    int userFlag)		/* 4rd argument for watch command. */
{
    Tcl_DString cmd;

    assert(textPtr);
    assert(textPtr->watchCmd);
    assert(operation);

    Tcl_DStringInit(&cmd);
    Tcl_DStringAppend(&cmd, Tcl_GetString(textPtr->watchCmd), TCL_INDEX_NONE);
    Tcl_DStringAppendElement(&cmd, Tk_PathName(textPtr->tkwin));
    Tcl_DStringAppendElement(&cmd, operation);
    Tcl_DStringAppendElement(&cmd, index1 ? index1 : "");
    Tcl_DStringAppendElement(&cmd, index2 ? index2 : "");
    Tcl_DStringStartSublist(&cmd);
    if (arg1) { Tcl_DStringAppendElement(&cmd, arg1); }
    if (arg2) { Tcl_DStringAppendElement(&cmd, arg2); }
    if (arg3) { Tcl_DStringAppendElement(&cmd, arg3); }
    Tcl_DStringEndSublist(&cmd);
    Tcl_DStringAppendElement(&cmd, userFlag ? "yes" : "no");

    textPtr->refCount += 1;

    Tcl_Preserve(textPtr->interp);
    if (Tcl_EvalEx(textPtr->interp, Tcl_DStringValue(&cmd), Tcl_DStringLength(&cmd), 0) != TCL_OK) {
	Tcl_AddErrorInfo(textPtr->interp, "\n    (triggering the \"watch\" command failed)");
	Tcl_BackgroundException(textPtr->interp, TCL_ERROR);
    }
    Tcl_Release(textPtr->interp);

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
	Tk_SendVirtualEvent(textPtr->tkwin, type, NULL);
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
    int flag)
{
    int oldModifiedFlag = sharedTextPtr->isModified;

    if (flag) {
	sharedTextPtr->isModified = 1;
    } else if (sharedTextPtr->undoStack && !sharedTextPtr->userHasSetModifiedFlag) {
	if (sharedTextPtr->insertDeleteUndoTokenCount > 0) {
	    sharedTextPtr->isModified = 1;
	} else {
	    unsigned undoDepth = TkTextUndoGetCurrentUndoStackDepth(sharedTextPtr->undoStack);
	    sharedTextPtr->isModified = (undoDepth > 0 && undoDepth == sharedTextPtr->undoLevel);
	}
    }

    if (oldModifiedFlag != sharedTextPtr->isModified) {
	sharedTextPtr->userHasSetModifiedFlag = 0;
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
    int oldIsAlteredFlag = sharedTextPtr->isAltered;
    int oldIsIrreversibleFlag = sharedTextPtr->isIrreversible;

    if (sharedTextPtr->undoStack) {
	if (TkTextUndoContentIsIrreversible(sharedTextPtr->undoStack)) {
	    sharedTextPtr->isIrreversible = 1;
	}
	if (!sharedTextPtr->isIrreversible) {
	    sharedTextPtr->isAltered = sharedTextPtr->undoTagListCount > 0
		    || sharedTextPtr->undoMarkListCount > 0
		    || TkTextUndoGetCurrentUndoStackDepth(sharedTextPtr->undoStack) > 0;
	}
    } else {
	sharedTextPtr->isIrreversible = 1;
    }
    if (sharedTextPtr->isIrreversible) {
	sharedTextPtr->isAltered = 1;
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
    int error = 0;
    Tcl_Obj *afterSyncCmd;

    assert(!TkTextPendingSync(textPtr));

    textPtr->pendingAfterSync = 0;
    afterSyncCmd = textPtr->afterSyncCmd;

    if (!afterSyncCmd) {
	return;
    }

    /*
     * We have to expect nested calls, futhermore the receiver might destroy the widget.
     */

    textPtr->afterSyncCmd = NULL;
    textPtr->refCount += 1;

    Tcl_Preserve(textPtr->interp);
    if (!(textPtr->flags & DESTROYED)) {
	code = Tcl_EvalObjEx(textPtr->interp, afterSyncCmd, TCL_EVAL_GLOBAL);
	if (code == TCL_ERROR && !error) {
	    Tcl_AddErrorInfo(textPtr->interp, "\n    (text sync)");
	    Tcl_BackgroundException(textPtr->interp, TCL_ERROR);
	    error = 1;
	}
    }
    Tcl_GuardedDecrRefCount(afterSyncCmd);
    Tcl_Release(textPtr->interp);
    TkTextDecrRefCountAndTestIfDestroyed(textPtr);
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
    void *clientData)	/* Information about text widget. */
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
    void *clientData)	/* Information about text widget. */
{
    TkText *textPtr = (TkText *) clientData;
    Tcl_Interp *interp;
    int syncState;

    textPtr->pendingFireEvent = 0;

    if (textPtr->flags & DESTROYED) {
	return;
    }

    syncState = !TkTextPendingSync(textPtr);

    if (textPtr->sendSyncEvent && syncState) {
	/*
	 * The user is waiting for sync state 'true', so we must send it.
	 */

	textPtr->prevSyncState = 0;
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
	textPtr->sendSyncEvent = 0;
    }
    textPtr->prevSyncState = syncState;

    interp = textPtr->interp;
    Tcl_Preserve(interp);
    /*
     * OSX 10.14 needs to be told to display the window when the Text Widget
     * is in sync.  (That is, to run DisplayText inside of the drawRect
     * method.)  Otherwise the screen might not get updated until an event
     * like a mouse click is received.  But that extra drawing corrupts the
     * data that the test suite is trying to collect.
     */

    if (!tkTextDebug) {
	FORCE_DISPLAY(textPtr->tkwin);
    }

    Tk_SendVirtualEvent(textPtr->tkwin, "WidgetViewSync", Tcl_NewBooleanObj(syncState));
    Tcl_Release(interp);
}

void
TkTextGenerateWidgetViewSyncEvent(
    TkText *textPtr,		/* Information about text widget. */
    int sendImmediately)
{
    if (!textPtr->pendingFireEvent) {
	textPtr->pendingFireEvent = 1;
	if (sendImmediately) {
	    FireWidgetViewSyncEvent(textPtr);
	} else {
	    Tcl_DoWhenIdle(FireWidgetViewSyncEvent, textPtr);
	}
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkrTextPrintIndex --
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

Tcl_Size
TkrTextPrintIndex(
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
    TkText *textPtr = (TkText *)searchSpecPtr->clientData;

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
	void *lineInfo;
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

	if (alreadySearchOffset >= 0) {
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

			const char c = matchLength ? pattern[0] : '\0';

			p = startOfLine;
			if (alreadySearchOffset >= 0) {
			    p += alreadySearchOffset;
			    alreadySearchOffset = -1;
			} else {
			    p += lastOffset - 1;
			}
			while (p >= startOfLine + firstOffset) {
			    if (matchLength == 0 || (p[0] == c && !strncmp(
				     p, pattern, (size_t) matchLength))) {
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
			    alreadySearchOffset -= (matchLength ? matchLength : 1);
                            if (alreadySearchOffset < 0) {
                                break;
                            }
			}
		    } else {
                        firstOffset = matchLength ? p - startOfLine + matchLength
                                                  : p - startOfLine + 1;
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
			int len;
			const char *s = startOfLine + matchOffset;

#if 0 && TCL_UTF_MAX > 4
			/*
			 * HACK: Support of pseudo UTF-8 strings. Needed because of this
			 * bad hack with TCL_UTF_MAX > 4, the whole thing is amateurish.
			 * (See function GetLineBreakFunc() about the very severe problems
			 * with TCL_UTF_MAX > 4).
			 */

			int ch;
			len = TkUtfToUniChar(s, &ch);
#else
			/*
			 * Proper implementation for UTF-8 strings:
			 */

			Tcl_UniChar ch;
			len = Tcl_UtfToUniChar(s, &ch);
#endif
			firstOffset = (p - startOfLine) + len;
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
			    && (int)info.matches[0].end == lastOffset - firstOffset)) {
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

		    if (info.extendStart == TCL_INDEX_NONE) {
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

			if ((match  && firstOffset + (int)info.matches[0].end != lastTotal
				    && firstOffset + (int)info.matches[0].end < prevFullLine)
				|| info.extendStart == TCL_INDEX_NONE) {
			    break;
			}

			/*
			 * If there is a match, but that match starts after
			 * the end of the first line, then we'll handle that
			 * next time around, when we're actually looking at
			 * that line.
			 */

			if (match && info.matches[0].start + 1 >= (Tcl_Size)lastOffset + 1) {
			    break;
			}
			if (match && firstOffset + (int)info.matches[0].end >= prevFullLine) {
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
				|| firstOffset + info.matches[0].end
				    > info.matches[0].start + matchOffset + matchLength))) {

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
			    int32_t *newArray = (int32_t *)ckalloc(4*matchNumSize);
			    memcpy(newArray, storeMatch, matchNumSize);
			    memcpy(newArray + 2*matchNum, storeLength, matchNumSize);
			    if (storeMatch != smArray) {
				ckfree((char *) storeMatch);
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

    Tcl_GuardedDecrRefCount(theLine);
    Tcl_GuardedDecrRefCount(patObj);

    /*
     * Free up any extra space we allocated.
     */

    if (storeMatch != smArray) {
	ckfree((char *) storeMatch);
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
    TCL_UNUSED(void *),
    TCL_UNUSED(Tk_Window),
    char *recordPtr,		/* Pointer to widget record. */
    Tcl_Size internalOffset)		/* Offset within *recordPtr containing the start object. */
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
    return Tcl_NewStringObj(buf, TCL_INDEX_NONE);
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
    return objPtr ? GetByteLength(objPtr) == 0 : 1;
}

static int
SetTextStartEnd(
    TCL_UNUSED(void *),
    TCL_UNUSED(Tcl_Interp *),		/* Current interp; may be used for errors. */
    TCL_UNUSED(Tk_Window),		/* Window for which option is being set. */
    Tcl_Obj **value,		/* Pointer to the pointer to the value object.
				 * We use a pointer to the pointer because we
				 * may need to return a value (NULL). */
    char *recordPtr,		/* Pointer to storage for the widget record. */
    Tcl_Size internalOffset,		/* Offset within *recordPtr at which the
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
	*objPtr = Tcl_NewStringObj((objPtr == &textPtr->newStartIndex) ? "begin" : "end", TCL_INDEX_NONE);
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
    TCL_UNUSED(void *),
    TCL_UNUSED(Tk_Window),
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
    TCL_UNUSED(void *),
    TCL_UNUSED(Tk_Window),
    char *internalPtr)
{
    Tcl_Obj *objPtr = *(Tcl_Obj **) internalPtr;

    if (objPtr) {
	Tcl_GuardedDecrRefCount(objPtr);
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
    TCL_UNUSED(void *),
    TCL_UNUSED(Tk_Window),
    char *recordPtr,		/* Pointer to widget record. */
    Tcl_Size internalOffset)		/* Offset within *recordPtr containing the line value. */
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
    TCL_UNUSED(void *),
    Tcl_Interp *interp,		/* Current interp; may be used for errors. */
    TCL_UNUSED(Tk_Window),		/* Window for which option is being set. */
    Tcl_Obj **value,		/* Pointer to the pointer to the value object.
				 * We use a pointer to the pointer because we
				 * may need to return a value (NULL). */
    char *recordPtr,		/* Pointer to storage for the widget record. */
    Tcl_Size internalOffset,	/* Offset within *recordPtr at which the
				 * internal value is to be stored. */
    char *oldInternalPtr,	/* Pointer to storage for the old value. */
    int flags)			/* Flags for the option, set Tk_SetOptions. */
{
    TkTextLine *linePtr = NULL;
    char *internalPtr;
    TkText *textPtr = (TkText *)recordPtr;

    if (internalOffset != TCL_INDEX_NONE) {
	internalPtr = (char *)recordPtr + internalOffset;
    } else {
	internalPtr = NULL;
    }

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
    TCL_UNUSED(void *),
    TCL_UNUSED(Tk_Window),
    char *internalPtr,		/* Pointer to storage for value. */
    char *oldInternalPtr)	/* Pointer to old value. */
{
    *(TkTextLine **) internalPtr = *(TkTextLine **) oldInternalPtr;
}

#endif /* SUPPORT_DEPRECATED_STARTLINE_ENDLINE */

/*
 *----------------------------------------------------------------------
 *
 * TkrTesttextCmd --
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

int
TkrTesttextCmd(
    TCL_UNUSED(void *),	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument strings. */
{
    TkText *textPtr;
    size_t len;
    int lineIndex, byteIndex, byteOffset;
    TkTextIndex index, insIndex;
    char buf[TK_POS_CHARS];
    Tcl_CmdInfo info;
    Tcl_Obj *watchCmd;

    if (objc + 1 < 4) {
	return TCL_ERROR;
    }

    if (Tcl_GetCommandInfo(interp, Tcl_GetString(objv[1]), &info) == 0) {
	return TCL_ERROR;
    }
    textPtr = (TkText *)info.objClientData;
    len = strlen(Tcl_GetString(objv[2]));
    if (strncmp(Tcl_GetString(objv[2]), "byteindex", len) == 0) {
	if (objc != 5) {
	    return TCL_ERROR;
	}
	lineIndex = atoi(Tcl_GetString(objv[3])) - 1;
	byteIndex = atoi(Tcl_GetString(objv[4]));

	TkrTextMakeByteIndex(textPtr->sharedTextPtr->tree, textPtr, lineIndex, byteIndex, &index);
    } else if (strncmp(Tcl_GetString(objv[2]), "forwbytes", len) == 0) {
	if (objc != 5) {
	    return TCL_ERROR;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[3], &index)) {
	    return TCL_ERROR;
	}
	byteOffset = atoi(Tcl_GetString(objv[4]));
	TkrTextIndexForwBytes(textPtr, &index, byteOffset, &index);
    } else if (strncmp(Tcl_GetString(objv[2]), "backbytes", len) == 0) {
	if (objc != 5) {
	    return TCL_ERROR;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[3], &index)) {
	    return TCL_ERROR;
	}
	byteOffset = atoi(Tcl_GetString(objv[4]));
	TkrTextIndexBackBytes(textPtr, &index, byteOffset, &index);
    } else {
	return TCL_ERROR;
    }

    /*
     * Avoid triggering of the "watch" command.
     */

    watchCmd = textPtr->watchCmd;
    textPtr->watchCmd = NULL;
    insIndex = index; /* because TkrTextSetMark may modify position */
    TkrTextSetMark(textPtr, "insert", &insIndex);
    textPtr->watchCmd = watchCmd;

    TkrTextPrintIndex(textPtr, &index, buf);
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s %d", buf, TkTextIndexGetByteIndex(&index)));
    return TCL_OK;
}


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
    Tcl_Obj *objv[8];
    Tcl_Obj **argv;
    Tcl_Size argc, i;

    Tcl_IncrRefCount(resultPtr = Tcl_GetObjResult(textPtr->interp));
    Tcl_ResetResult(textPtr->interp);
    Tcl_IncrRefCount(objv[0] = Tcl_NewStringObj(Tk_PathName(textPtr->tkwin), TCL_INDEX_NONE));
    Tcl_IncrRefCount(objv[1] = Tcl_NewStringObj("inspect", TCL_INDEX_NONE));
    Tcl_IncrRefCount(objv[2] = Tcl_NewStringObj("-elide", TCL_INDEX_NONE));
    Tcl_IncrRefCount(objv[3] = Tcl_NewStringObj("-chars", TCL_INDEX_NONE));
    Tcl_IncrRefCount(objv[4] = Tcl_NewStringObj("-image", TCL_INDEX_NONE));
    Tcl_IncrRefCount(objv[5] = Tcl_NewStringObj("-window", TCL_INDEX_NONE));
    Tcl_IncrRefCount(objv[6] = Tcl_NewStringObj("-mark", TCL_INDEX_NONE));
    Tcl_IncrRefCount(objv[7] = Tcl_NewStringObj("-tag", TCL_INDEX_NONE));
    TextInspectCmd(textPtr, textPtr->interp, sizeof(objv)/sizeof(objv[0]), objv);
    for (i = 0; i < (int) (sizeof(objv)/sizeof(objv[0])); ++i) {
	Tcl_GuardedDecrRefCount(objv[i]);
    }
    Tcl_ListObjGetElements(textPtr->interp, Tcl_GetObjResult(textPtr->interp), &argc, &argv);
    for (i = 0; i < argc; ++i) {
	fprintf(stdout, "%s\n", Tcl_GetString(argv[i]));
    }
    Tcl_SetObjResult(textPtr->interp, resultPtr);
    Tcl_GuardedDecrRefCount(resultPtr);
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
    Tcl_Size argc, i;

    Tcl_IncrRefCount(resultPtr = Tcl_GetObjResult(textPtr->interp));
    Tcl_ResetResult(textPtr->interp);

    Tcl_IncrRefCount(objv[0] = Tcl_NewStringObj(Tk_PathName(textPtr->tkwin), TCL_INDEX_NONE));
    Tcl_IncrRefCount(objv[1] = Tcl_NewStringObj("dump", TCL_INDEX_NONE));
    Tcl_IncrRefCount(objv[2] = Tcl_NewStringObj("begin", TCL_INDEX_NONE));
    Tcl_IncrRefCount(objv[3] = Tcl_NewStringObj("end", TCL_INDEX_NONE));
    TextDumpCmd(textPtr, textPtr->interp, sizeof(objv)/sizeof(objv[0]), objv);
    for (i = 0; i < (int) (sizeof(objv)/sizeof(objv[0])); ++i) {
	Tcl_GuardedDecrRefCount(objv[i]);
    }

    Tcl_ListObjGetElements(textPtr->interp, Tcl_GetObjResult(textPtr->interp), &argc, &argv);
    for (i = 0; i < argc; i += 3) {
	char const *type = Tcl_GetString(argv[i]);
	char const *text = Tcl_GetString(argv[i + 1]);
	char const *indx = Tcl_GetString(argv[i + 2]);

	fprintf(stdout, "%s ", indx);
	fprintf(stdout, "%s ", type);

	if (strcmp(type, "text") == 0) {
	    int len = strlen(text), j;

	    fprintf(stdout, "\"");
	    for (j = 0; j < len; ++j) {
		char c = text[j];

		switch (c) {
		case '\t': fprintf(stdout, "\\t"); break;
		case '\n': fprintf(stdout, "\\n"); break;
		case '\v': fprintf(stdout, "\\v"); break;
		case '\f': fprintf(stdout, "\\f"); break;
		case '\r': fprintf(stdout, "\\r"); break;

		default:
		    if (UCHAR(c) < 0x80 && isprint(c)) {
			fprintf(stdout, "%c", c);
		    } else {
			fprintf(stdout, "\\x%02u", (unsigned) UCHAR(c));
		    }
		    break;
		}
	    }
	    fprintf(stdout, "\"\n");
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
		fprintf(stdout, "%s (%s)\n", text,
			markPtr->typePtr == &tkTextLeftMarkType ? "left" : "right");
	    }
	} else {
	    fprintf(stdout, "%s\n", text);
	}
    }

    Tcl_SetObjResult(textPtr->interp, resultPtr);
    Tcl_GuardedDecrRefCount(resultPtr);
}

#endif /* NDEBUG */


/* Additionally we need stand-alone object code. */
extern TkSharedText *	TkBTreeGetShared(TkTextBTree tree);
extern int		TkBTreeGetNumberOfDisplayLines(const TkTextPixelInfo *pixelInfo);
extern TkTextPixelInfo *TkBTreeLinePixelInfo(const TkText *textPtr, TkTextLine *linePtr);
extern Tcl_Size		TkBTreeEpoch(TkTextBTree tree);
extern Tcl_Size		TkBTreeIncrEpoch(TkTextBTree tree);
extern struct Node	*TkBTreeGetRoot(TkTextBTree tree);
extern TkTextLine *	TkBTreePrevLogicalLine(const TkSharedText *sharedTextPtr,
			    const TkText *textPtr, TkTextLine *linePtr);
extern TkTextTag *	TkBTreeGetTags(const TkTextIndex *indexPtr, TkTextSortMethod sortMeth,
			    int *flags);
extern TkTextLine *	TkBTreeGetStartLine(const TkText *textPtr);
extern TkTextLine *	TkBTreeGetLastLine(const TkText *textPtr);
extern TkTextLine *	TkBTreeNextLine(const TkText *textPtr, TkTextLine *linePtr);
extern TkTextLine *	TkBTreePrevLine(const TkText *textPtr, TkTextLine *linePtr);
extern unsigned		TkBTreeCountLines(const TkTextBTree tree, const TkTextLine *linePtr1,
			    const TkTextLine *linePtr2);
extern int		TkTextGetIndexFromObj(Tcl_Interp *interp, TkText *textPtr, Tcl_Obj *objPtr,
			    TkTextIndex *indexPtr);
extern int		TkTextIsDeadPeer(const TkText *textPtr);
extern int		TkTextIsMark(const TkTextSegment *segPtr);
extern int		TkTextIsStartEndMarker(const TkTextSegment *segPtr);
extern int		TkTextIsSpecialMark(const TkTextSegment *segPtr);
extern int		TkTextIsPrivateMark(const TkTextSegment *segPtr);
extern int		TkTextIsSpecialOrPrivateMark(const TkTextSegment *segPtr);
extern int		TkTextIsNormalOrSpecialMark(const TkTextSegment *segPtr);
extern int		TkTextIsNormalMark(const TkTextSegment *segPtr);
extern int		TkTextIsStableMark(const TkTextSegment *segPtr);
extern const TkTextDispChunk *TkTextGetFirstChunkOfNextDispLine(const TkTextDispChunk *chunkPtr);
extern const TkTextDispChunk *TkTextGetLastChunkOfPrevDispLine(const TkTextDispChunk *chunkPtr);
extern void		TkTextIndexSetEpoch(TkTextIndex *indexPtr, size_t epoch);
extern void		TkTextIndexSetPeer(TkTextIndex *indexPtr, TkText *textPtr);
extern void		TkTextIndexSetToLastChar2(TkTextIndex *indexPtr, TkTextLine *linePtr);
extern void		TkTextIndexInvalidate(TkTextIndex *indexPtr);
extern void		TkTextIndexMakePersistent(TkTextIndex *indexPtr);
extern TkTextLine *	TkTextIndexGetLine(const TkTextIndex *indexPtr);
extern TkTextSegment *	TkTextIndexGetSegment(const TkTextIndex *indexPtr);
extern TkSharedText *	TkTextIndexGetShared(const TkTextIndex *indexPtr);
extern int		TkTextIndexSameLines(const TkTextIndex *indexPtr1, const TkTextIndex *indexPtr2);
extern void		TkTextIndexSave(TkTextIndex *indexPtr);
# if TK_MAJOR_VERSION == 8 && TK_MINOR_VERSION < 7 && TCL_UTF_MAX <= 4
extern int		TkUtfToUniChar(const char *src, int *chPtr);
# endif


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 105
 * End:
 * vi:set ts=8 sw=4:
 */
