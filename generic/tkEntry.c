/*
 * tkEntry.c --
 *
 *	This module implements entry and spinbox widgets for the Tk toolkit.
 *	An entry displays a string and allows the string to be edited. A
 *	spinbox expands on the entry by adding up/down buttons that control
 *	the value of the entry widget.
 *
 * Copyright © 1990-1994 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 * Copyright © 2000 Ajuba Solutions.
 * Copyright © 2002 ActiveState Corporation.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkEntry.h"
#include "default.h"

#ifdef _WIN32
#include "tkWinInt.h"
#endif

/*
 * The following macro defines how many extra pixels to leave on each side of
 * the text in the entry.
 */

#define XPAD 1
#define YPAD 1

/*
 * A comparison function for double values. For Spinboxes.
 */

#define MIN_DBL_VAL		1E-9
#define DOUBLES_EQ(d1, d2)	(fabs((d1) - (d2)) < MIN_DBL_VAL)


static const char *const stateStrings[] = {
    "disabled", "normal", "readonly", NULL
};

/*
 * Definitions for -validate option values:
 */

static const char *const validateStrings[] = {
    "all", "key", "focus", "focusin", "focusout", "none", NULL
};
enum validateType {
    VALIDATE_ALL, VALIDATE_KEY, VALIDATE_FOCUS,
    VALIDATE_FOCUSIN, VALIDATE_FOCUSOUT, VALIDATE_NONE,
    /*
     * These extra enums are for use with EntryValidateChange
     */
    VALIDATE_FORCED, VALIDATE_DELETE, VALIDATE_INSERT, VALIDATE_BUTTON
};
#define DEF_ENTRY_VALIDATE	"none"
#define DEF_ENTRY_INVALIDCMD	""

/*
 * Information used for Entry objv parsing.
 */

static const Tk_OptionSpec entryOptSpec[] = {
    {TK_OPTION_BORDER, "-background", "background", "Background",
	DEF_ENTRY_BG_COLOR, TCL_INDEX_NONE, offsetof(Entry, normalBorder),
	0, DEF_ENTRY_BG_MONO, 0},
    {TK_OPTION_SYNONYM, "-bd", NULL, NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-borderwidth", 0},
    {TK_OPTION_SYNONYM, "-bg", NULL, NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-background", 0},
    {TK_OPTION_PIXELS, "-borderwidth", "borderWidth", "BorderWidth",
	DEF_ENTRY_BORDER_WIDTH, offsetof(Entry, borderWidthObj), TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_CURSOR, "-cursor", "cursor", "Cursor",
	DEF_ENTRY_CURSOR, TCL_INDEX_NONE, offsetof(Entry, cursor),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BORDER, "-disabledbackground", "disabledBackground",
	"DisabledBackground", DEF_ENTRY_DISABLED_BG_COLOR, TCL_INDEX_NONE,
	offsetof(Entry, disabledBorder), TK_OPTION_NULL_OK,
	DEF_ENTRY_DISABLED_BG_MONO, 0},
    {TK_OPTION_COLOR, "-disabledforeground", "disabledForeground",
	"DisabledForeground", DEF_ENTRY_DISABLED_FG, TCL_INDEX_NONE,
	offsetof(Entry, dfgColorPtr), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BOOLEAN, "-exportselection", "exportSelection",
	"ExportSelection", DEF_ENTRY_EXPORT_SELECTION, TCL_INDEX_NONE,
	offsetof(Entry, exportSelection), 0, 0, 0},
    {TK_OPTION_SYNONYM, "-fg", "foreground", NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-foreground", 0},
    {TK_OPTION_FONT, "-font", "font", "Font",
	DEF_ENTRY_FONT, TCL_INDEX_NONE, offsetof(Entry, tkfont), 0, 0, 0},
    {TK_OPTION_COLOR, "-foreground", "foreground", "Foreground",
	DEF_ENTRY_FG, TCL_INDEX_NONE, offsetof(Entry, fgColorPtr), 0, 0, 0},
    {TK_OPTION_COLOR, "-highlightbackground", "highlightBackground",
	"HighlightBackground", DEF_ENTRY_HIGHLIGHT_BG,
	TCL_INDEX_NONE, offsetof(Entry, highlightBgColorPtr), 0, 0, 0},
    {TK_OPTION_COLOR, "-highlightcolor", "highlightColor", "HighlightColor",
	DEF_ENTRY_HIGHLIGHT, TCL_INDEX_NONE, offsetof(Entry, highlightColorPtr), 0, 0, 0},
    {TK_OPTION_PIXELS, "-highlightthickness", "highlightThickness",
	"HighlightThickness", DEF_ENTRY_HIGHLIGHT_WIDTH, offsetof(Entry, highlightWidthObj),
	TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_BORDER, "-insertbackground", "insertBackground", "Foreground",
	DEF_ENTRY_INSERT_BG, TCL_INDEX_NONE, offsetof(Entry, insertBorder), 0, 0, 0},
    {TK_OPTION_PIXELS, "-insertborderwidth", "insertBorderWidth",
	"BorderWidth", DEF_ENTRY_INSERT_BD_COLOR, offsetof(Entry, insertBorderWidthObj),
	TCL_INDEX_NONE, 0,
	DEF_ENTRY_INSERT_BD_MONO, 0},
    {TK_OPTION_INT, "-insertofftime", "insertOffTime", "OffTime",
	DEF_ENTRY_INSERT_OFF_TIME, TCL_INDEX_NONE, offsetof(Entry, insertOffTime),
	0, 0, 0},
    {TK_OPTION_INT, "-insertontime", "insertOnTime", "OnTime",
	DEF_ENTRY_INSERT_ON_TIME, TCL_INDEX_NONE, offsetof(Entry, insertOnTime), 0, 0, 0},
    {TK_OPTION_PIXELS, "-insertwidth", "insertWidth", "InsertWidth",
	DEF_ENTRY_INSERT_WIDTH, offsetof(Entry, insertWidthObj), TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_STRING, "-invalidcommand", "invalidCommand", "InvalidCommand",
	DEF_ENTRY_INVALIDCMD, offsetof(Entry, invalidCmdObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_SYNONYM, "-invcmd", NULL, NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-invalidcommand", 0},
    {TK_OPTION_JUSTIFY, "-justify", "justify", "Justify",
	DEF_ENTRY_JUSTIFY, TCL_INDEX_NONE, offsetof(Entry, justify), TK_OPTION_ENUM_VAR, 0, 0},
    {TK_OPTION_STRING, "-placeholder", "placeHolder", "PlaceHolder",
	DEF_ENTRY_PLACEHOLDER, offsetof(Entry, placeholderObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_COLOR, "-placeholderforeground", "placeholderForeground",
	"PlaceholderForeground", DEF_ENTRY_PLACEHOLDERFG, TCL_INDEX_NONE,
	offsetof(Entry, placeholderColorPtr), 0, 0, 0},
    {TK_OPTION_BORDER, "-readonlybackground", "readonlyBackground",
	"ReadonlyBackground", DEF_ENTRY_READONLY_BG_COLOR, TCL_INDEX_NONE,
	offsetof(Entry, readonlyBorder), TK_OPTION_NULL_OK,
	DEF_ENTRY_READONLY_BG_MONO, 0},
    {TK_OPTION_RELIEF, "-relief", "relief", "Relief",
	DEF_ENTRY_RELIEF, TCL_INDEX_NONE, offsetof(Entry, relief), 0, 0, 0},
    {TK_OPTION_BORDER, "-selectbackground", "selectBackground", "Foreground",
	DEF_ENTRY_SELECT_COLOR, TCL_INDEX_NONE, offsetof(Entry, selBorder),
	0, DEF_ENTRY_SELECT_MONO, 0},
    {TK_OPTION_PIXELS, "-selectborderwidth", "selectBorderWidth",
	"BorderWidth", DEF_ENTRY_SELECT_BD_COLOR, offsetof(Entry, selBorderWidthObj),
	TCL_INDEX_NONE, 0, DEF_ENTRY_SELECT_BD_MONO, 0},
    {TK_OPTION_COLOR, "-selectforeground", "selectForeground", "Background",
	DEF_ENTRY_SELECT_FG_COLOR, TCL_INDEX_NONE, offsetof(Entry, selFgColorPtr),
	TK_OPTION_NULL_OK, DEF_ENTRY_SELECT_FG_MONO, 0},
    {TK_OPTION_STRING, "-show", "show", "Show",
	DEF_ENTRY_SHOW, offsetof(Entry, showCharObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING_TABLE, "-state", "state", "State",
	DEF_ENTRY_STATE, TCL_INDEX_NONE, offsetof(Entry, state),
	0, stateStrings, 0},
    {TK_OPTION_STRING, "-takefocus", "takeFocus", "TakeFocus",
	DEF_ENTRY_TAKE_FOCUS, offsetof(Entry, takeFocusObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-textvariable", "textVariable", "Variable",
	DEF_ENTRY_TEXT_VARIABLE, offsetof(Entry, textVarNameObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING_TABLE, "-validate", "validate", "Validate",
	DEF_ENTRY_VALIDATE, TCL_INDEX_NONE, offsetof(Entry, validate),
	0, validateStrings, 0},
    {TK_OPTION_STRING, "-validatecommand", "validateCommand","ValidateCommand",
	NULL, offsetof(Entry, validateCmdObj), TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_SYNONYM, "-vcmd", NULL, NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-validatecommand", 0},
    {TK_OPTION_INT, "-width", "width", "Width",
	DEF_ENTRY_WIDTH, TCL_INDEX_NONE, offsetof(Entry, prefWidth), 0, 0, 0},
    {TK_OPTION_STRING, "-xscrollcommand", "xScrollCommand", "ScrollCommand",
	DEF_ENTRY_SCROLL_COMMAND, offsetof(Entry, scrollCmdObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, TCL_INDEX_NONE, 0, 0, 0}
};

/*
 * Information used for Spinbox objv parsing.
 */

#define DEF_SPINBOX_REPEAT_DELAY	"400"
#define DEF_SPINBOX_REPEAT_INTERVAL	"100"

#define DEF_SPINBOX_CMD			""

#define DEF_SPINBOX_FROM		"0.0"
#define DEF_SPINBOX_TO			"0.0"
#define DEF_SPINBOX_INCREMENT		"1.0"
#define DEF_SPINBOX_FORMAT		""

#define DEF_SPINBOX_VALUES		""
#define DEF_SPINBOX_WRAP		"0"

static const Tk_OptionSpec sbOptSpec[] = {
    {TK_OPTION_BORDER, "-activebackground", "activeBackground", "Background",
	DEF_BUTTON_ACTIVE_BG_COLOR, TCL_INDEX_NONE, offsetof(Spinbox, activeBorder),
	0, DEF_BUTTON_ACTIVE_BG_MONO, 0},
    {TK_OPTION_BORDER, "-background", "background", "Background",
	DEF_ENTRY_BG_COLOR, TCL_INDEX_NONE, offsetof(Entry, normalBorder),
	0, DEF_ENTRY_BG_MONO, 0},
    {TK_OPTION_SYNONYM, "-bd", NULL, NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-borderwidth", 0},
    {TK_OPTION_SYNONYM, "-bg", NULL, NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-background", 0},
    {TK_OPTION_PIXELS, "-borderwidth", "borderWidth", "BorderWidth",
	DEF_ENTRY_BORDER_WIDTH, offsetof(Entry, borderWidthObj), TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_BORDER, "-buttonbackground", "buttonBackground", "Background",
	DEF_BUTTON_BG_COLOR, TCL_INDEX_NONE, offsetof(Spinbox, buttonBorder),
	0, DEF_BUTTON_BG_MONO, 0},
    {TK_OPTION_CURSOR, "-buttoncursor", "buttonCursor", "Cursor",
	DEF_BUTTON_CURSOR, TCL_INDEX_NONE, offsetof(Spinbox, bCursor),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_RELIEF, "-buttondownrelief", "buttonDownRelief", "Relief",
	DEF_BUTTON_RELIEF, TCL_INDEX_NONE, offsetof(Spinbox, bdRelief), 0, 0, 0},
    {TK_OPTION_RELIEF, "-buttonuprelief", "buttonUpRelief", "Relief",
	DEF_BUTTON_RELIEF, TCL_INDEX_NONE, offsetof(Spinbox, buRelief), 0, 0, 0},
    {TK_OPTION_STRING, "-command", "command", "Command",
	DEF_SPINBOX_CMD, offsetof(Spinbox, commandObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_CURSOR, "-cursor", "cursor", "Cursor",
	DEF_ENTRY_CURSOR, TCL_INDEX_NONE, offsetof(Entry, cursor),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BORDER, "-disabledbackground", "disabledBackground",
	"DisabledBackground", DEF_ENTRY_DISABLED_BG_COLOR, TCL_INDEX_NONE,
	offsetof(Entry, disabledBorder), TK_OPTION_NULL_OK,
	DEF_ENTRY_DISABLED_BG_MONO, 0},
    {TK_OPTION_COLOR, "-disabledforeground", "disabledForeground",
	"DisabledForeground", DEF_ENTRY_DISABLED_FG, TCL_INDEX_NONE,
	offsetof(Entry, dfgColorPtr), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BOOLEAN, "-exportselection", "exportSelection",
	"ExportSelection", DEF_ENTRY_EXPORT_SELECTION, TCL_INDEX_NONE,
	offsetof(Entry, exportSelection), 0, 0, 0},
    {TK_OPTION_SYNONYM, "-fg", "foreground", NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-foreground", 0},
    {TK_OPTION_FONT, "-font", "font", "Font",
	DEF_ENTRY_FONT, TCL_INDEX_NONE, offsetof(Entry, tkfont), 0, 0, 0},
    {TK_OPTION_COLOR, "-foreground", "foreground", "Foreground",
	DEF_ENTRY_FG, TCL_INDEX_NONE, offsetof(Entry, fgColorPtr), 0, 0, 0},
    {TK_OPTION_STRING, "-format", "format", "Format",
	DEF_SPINBOX_FORMAT, offsetof(Spinbox, reqFormatObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_DOUBLE, "-from", "from", "From",
	DEF_SPINBOX_FROM, TCL_INDEX_NONE, offsetof(Spinbox, fromValue), 0, 0, 0},
    {TK_OPTION_COLOR, "-highlightbackground", "highlightBackground",
	"HighlightBackground", DEF_ENTRY_HIGHLIGHT_BG,
	TCL_INDEX_NONE, offsetof(Entry, highlightBgColorPtr), 0, 0, 0},
    {TK_OPTION_COLOR, "-highlightcolor", "highlightColor", "HighlightColor",
	DEF_ENTRY_HIGHLIGHT, TCL_INDEX_NONE, offsetof(Entry, highlightColorPtr), 0, 0, 0},
    {TK_OPTION_PIXELS, "-highlightthickness", "highlightThickness",
	"HighlightThickness", DEF_ENTRY_HIGHLIGHT_WIDTH, offsetof(Entry, highlightWidthObj),
	TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_DOUBLE, "-increment", "increment", "Increment",
	DEF_SPINBOX_INCREMENT, TCL_INDEX_NONE, offsetof(Spinbox, increment), 0, 0, 0},
    {TK_OPTION_BORDER, "-insertbackground", "insertBackground", "Foreground",
	DEF_ENTRY_INSERT_BG, TCL_INDEX_NONE, offsetof(Entry, insertBorder), 0, 0, 0},
    {TK_OPTION_PIXELS, "-insertborderwidth", "insertBorderWidth",
	"BorderWidth", DEF_ENTRY_INSERT_BD_COLOR, offsetof(Entry, insertBorderWidthObj),
	TCL_INDEX_NONE, 0, DEF_ENTRY_INSERT_BD_MONO, 0},
    {TK_OPTION_INT, "-insertofftime", "insertOffTime", "OffTime",
	DEF_ENTRY_INSERT_OFF_TIME, TCL_INDEX_NONE, offsetof(Entry, insertOffTime),
	0, 0, 0},
    {TK_OPTION_INT, "-insertontime", "insertOnTime", "OnTime",
	DEF_ENTRY_INSERT_ON_TIME, TCL_INDEX_NONE, offsetof(Entry, insertOnTime), 0, 0, 0},
    {TK_OPTION_PIXELS, "-insertwidth", "insertWidth", "InsertWidth",
	DEF_ENTRY_INSERT_WIDTH, offsetof(Entry, insertWidthObj), TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_STRING, "-invalidcommand", "invalidCommand", "InvalidCommand",
	DEF_ENTRY_INVALIDCMD, offsetof(Entry, invalidCmdObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_SYNONYM, "-invcmd", NULL, NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-invalidcommand", 0},
    {TK_OPTION_JUSTIFY, "-justify", "justify", "Justify",
	DEF_ENTRY_JUSTIFY, TCL_INDEX_NONE, offsetof(Entry, justify), TK_OPTION_ENUM_VAR, 0, 0},
    {TK_OPTION_STRING, "-placeholder", "placeHolder", "PlaceHolder",
	DEF_ENTRY_PLACEHOLDER, offsetof(Entry, placeholderObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_COLOR, "-placeholderforeground", "placeholderForeground",
	"PlaceholderForeground", DEF_ENTRY_PLACEHOLDERFG, TCL_INDEX_NONE,
	offsetof(Entry, placeholderColorPtr), 0, 0, 0},
    {TK_OPTION_RELIEF, "-relief", "relief", "Relief",
	DEF_ENTRY_RELIEF, TCL_INDEX_NONE, offsetof(Entry, relief), 0, 0, 0},
    {TK_OPTION_BORDER, "-readonlybackground", "readonlyBackground",
	"ReadonlyBackground", DEF_ENTRY_READONLY_BG_COLOR, TCL_INDEX_NONE,
	offsetof(Entry, readonlyBorder), TK_OPTION_NULL_OK,
	DEF_ENTRY_READONLY_BG_MONO, 0},
    {TK_OPTION_INT, "-repeatdelay", "repeatDelay", "RepeatDelay",
	DEF_SPINBOX_REPEAT_DELAY, TCL_INDEX_NONE, offsetof(Spinbox, repeatDelay),
	0, 0, 0},
    {TK_OPTION_INT, "-repeatinterval", "repeatInterval", "RepeatInterval",
	DEF_SPINBOX_REPEAT_INTERVAL, TCL_INDEX_NONE, offsetof(Spinbox, repeatInterval),
	0, 0, 0},
    {TK_OPTION_BORDER, "-selectbackground", "selectBackground", "Foreground",
	DEF_ENTRY_SELECT_COLOR, TCL_INDEX_NONE, offsetof(Entry, selBorder),
	0, DEF_ENTRY_SELECT_MONO, 0},
    {TK_OPTION_PIXELS, "-selectborderwidth", "selectBorderWidth",
	"BorderWidth", DEF_ENTRY_SELECT_BD_COLOR, offsetof(Entry, selBorderWidthObj),
	TCL_INDEX_NONE, 0, DEF_ENTRY_SELECT_BD_MONO, 0},
    {TK_OPTION_COLOR, "-selectforeground", "selectForeground", "Background",
	DEF_ENTRY_SELECT_FG_COLOR, TCL_INDEX_NONE, offsetof(Entry, selFgColorPtr),
	TK_OPTION_NULL_OK, DEF_ENTRY_SELECT_FG_MONO, 0},
    {TK_OPTION_STRING_TABLE, "-state", "state", "State",
	DEF_ENTRY_STATE, TCL_INDEX_NONE, offsetof(Entry, state),
	0, stateStrings, 0},
    {TK_OPTION_STRING, "-takefocus", "takeFocus", "TakeFocus",
	DEF_ENTRY_TAKE_FOCUS, offsetof(Entry, takeFocusObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-textvariable", "textVariable", "Variable",
	DEF_ENTRY_TEXT_VARIABLE, offsetof(Entry, textVarNameObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_DOUBLE, "-to", "to", "To",
	DEF_SPINBOX_TO, TCL_INDEX_NONE, offsetof(Spinbox, toValue), 0, 0, 0},
    {TK_OPTION_STRING_TABLE, "-validate", "validate", "Validate",
	DEF_ENTRY_VALIDATE, TCL_INDEX_NONE, offsetof(Entry, validate),
	0, validateStrings, 0},
    {TK_OPTION_STRING, "-validatecommand", "validateCommand","ValidateCommand",
	NULL, offsetof(Entry, validateCmdObj), TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-values", "values", "Values",
	DEF_SPINBOX_VALUES, offsetof(Spinbox, valueObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_SYNONYM, "-vcmd", NULL, NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-validatecommand", 0},
    {TK_OPTION_INT, "-width", "width", "Width",
	DEF_ENTRY_WIDTH, TCL_INDEX_NONE, offsetof(Entry, prefWidth), 0, 0, 0},
    {TK_OPTION_BOOLEAN, "-wrap", "wrap", "Wrap",
	DEF_SPINBOX_WRAP, TCL_INDEX_NONE, offsetof(Spinbox, wrap), 0, 0, 0},
    {TK_OPTION_STRING, "-xscrollcommand", "xScrollCommand", "ScrollCommand",
	DEF_ENTRY_SCROLL_COMMAND, offsetof(Entry, scrollCmdObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, TCL_INDEX_NONE, 0, 0, 0}
};

/*
 * The following tables define the entry widget commands (and sub-commands)
 * and map the indexes into the string tables into enumerated types used to
 * dispatch the entry widget command.
 */

static const char *const entryCmdNames[] = {
    "bbox", "cget", "configure", "delete", "get", "icursor", "index",
    "insert", "scan", "selection", "validate", "xview", NULL
};

enum entryCmd {
    COMMAND_BBOX, COMMAND_CGET, COMMAND_CONFIGURE, COMMAND_DELETE,
    COMMAND_GET, COMMAND_ICURSOR, COMMAND_INDEX, COMMAND_INSERT,
    COMMAND_SCAN, COMMAND_SELECTION, COMMAND_VALIDATE, COMMAND_XVIEW
};

static const char *const selCmdNames[] = {
    "adjust", "clear", "from", "present", "range", "to", NULL
};

enum selCmd {
    SELECTION_ADJUST, SELECTION_CLEAR, SELECTION_FROM,
    SELECTION_PRESENT, SELECTION_RANGE, SELECTION_TO
};

/*
 * The following tables define the spinbox widget commands (and sub-commands)
 * and map the indexes into the string tables into enumerated types used to
 * dispatch the spinbox widget command.
 */

static const char *const sbCmdNames[] = {
    "bbox", "cget", "configure", "delete", "get", "icursor", "identify",
    "index", "insert", "invoke", "scan", "selection", "set",
    "validate", "xview", NULL
};

enum sbCmd {
    SB_CMD_BBOX, SB_CMD_CGET, SB_CMD_CONFIGURE, SB_CMD_DELETE,
    SB_CMD_GET, SB_CMD_ICURSOR, SB_CMD_IDENTIFY, SB_CMD_INDEX,
    SB_CMD_INSERT, SB_CMD_INVOKE, SB_CMD_SCAN, SB_CMD_SELECTION,
    SB_CMD_SET, SB_CMD_VALIDATE, SB_CMD_XVIEW
};

static const char *const sbSelCmdNames[] = {
    "adjust", "clear", "element", "from", "present", "range", "to", NULL
};

enum sbselCmd {
    SB_SEL_ADJUST, SB_SEL_CLEAR, SB_SEL_ELEMENT, SB_SEL_FROM,
    SB_SEL_PRESENT, SB_SEL_RANGE, SB_SEL_TO
};

/*
 * Extra for selection of elements
 */

/*
 * This is the string array corresponding to the enum in selelement. If you
 * modify them, you must modify the strings here.
 */

static const char *const selElementNames[] = {
    "buttondown", "buttonup", "none", NULL, "entry"
};

/*
 * Flags for GetEntryIndex function:
 */

#define ZERO_OK			1
#define LAST_PLUS_ONE_OK	2

/*
 * Forward declarations for functions defined later in this file:
 */

static int		ConfigureEntry(Tcl_Interp *interp, Entry *entryPtr,
			    Tcl_Size objc, Tcl_Obj *const objv[]);
static int		DeleteChars(Entry *entryPtr, Tcl_Size index, Tcl_Size count);
static Tcl_FreeProc	DestroyEntry;
static void		DisplayEntry(void *clientData);
static void		EntryBlinkProc(void *clientData);
static void		EntryCmdDeletedProc(void *clientData);
static void		EntryComputeGeometry(Entry *entryPtr);
static void		EntryEventProc(void *clientData,
			    XEvent *eventPtr);
static void		EntryFocusProc(Entry *entryPtr, int gotFocus);
static Tcl_Size	EntryFetchSelection(void *clientData, Tcl_Size offset,
			    char *buffer, Tcl_Size maxBytes);
static void		EntryLostSelection(void *clientData);
static void		EventuallyRedraw(Entry *entryPtr);
static void		EntryScanTo(Entry *entryPtr, int y);
static void		EntrySetValue(Entry *entryPtr, const char *value);
static void		EntrySelectTo(Entry *entryPtr, Tcl_Size index);
static char *		EntryTextVarProc(void *clientData,
			    Tcl_Interp *interp, const char *name1,
			    const char *name2, int flags);
static void		EntryUpdateScrollbar(Entry *entryPtr);
static int		EntryValidate(Entry *entryPtr, char *cmd);
static int		EntryValidateChange(Entry *entryPtr, const char *change,
			    const char *newStr, Tcl_Size index, int type);
static void		ExpandPercents(Entry *entryPtr, const char *before,
			    const char *change, const char *newStr, Tcl_Size index,
			    int type, Tcl_DString *dsPtr);
static int		EntryValueChanged(Entry *entryPtr,
			    const char *newValue);
static void		EntryVisibleRange(Entry *entryPtr,
			    double *firstPtr, double *lastPtr);
static Tcl_ObjCmdProc2 EntryWidgetObjCmd;
static void		EntryWorldChanged(void *instanceData);
static int		GetEntryIndex(Tcl_Interp *interp, Entry *entryPtr,
			    Tcl_Obj *indexObj, Tcl_Size *indexPtr);
static int		InsertChars(Entry *entryPtr, Tcl_Size index, Tcl_Obj *obj);

/*
 * These forward declarations are the spinbox specific ones:
 */

static Tcl_ObjCmdProc2 SpinboxWidgetObjCmd;
static int		GetSpinboxElement(Spinbox *sbPtr, int x, int y);
static int		SpinboxInvoke(Tcl_Interp *interp, Spinbox *sbPtr,
			    int element);
static int		ComputeFormat(Spinbox *sbPtr);

/*
 * The structure below defines widget class behavior by means of functions
 * that can be invoked from generic window code.
 */

static const Tk_ClassProcs entryClass = {
    sizeof(Tk_ClassProcs),	/* size */
    EntryWorldChanged,		/* worldChangedProc */
    NULL,			/* createProc */
    NULL			/* modalProc */
};

/*
 *--------------------------------------------------------------
 *
 * Tk_EntryObjCmd --
 *
 *	This function is invoked to process the "entry" Tcl command. See the
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
Tk_EntryObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Entry *entryPtr;
    Tk_OptionTable optionTable;
    Tk_Window tkwin;
    char *tmp;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "pathName ?-option value ...?");
	return TCL_ERROR;
    }

    tkwin = Tk_CreateWindowFromPath(interp, Tk_MainWindow(interp),
	    Tcl_GetString(objv[1]), NULL);
    if (tkwin == NULL) {
	return TCL_ERROR;
    }

    /*
     * Create the option table for this widget class. If it has already been
     * created, Tk will return the cached value.
     */

    optionTable = Tk_CreateOptionTable(interp, entryOptSpec);

    /*
     * Initialize the fields of the structure that won't be initialized by
     * ConfigureEntry, or that ConfigureEntry requires to be initialized
     * already (e.g. resource pointers). Only the non-NULL/0 data must be
     * initialized as memset covers the rest.
     */

    entryPtr = (Entry *)ckalloc(sizeof(Entry));
    memset(entryPtr, 0, sizeof(Entry));

    entryPtr->tkwin		= tkwin;
    entryPtr->display		= Tk_Display(tkwin);
    entryPtr->interp		= interp;
    entryPtr->widgetCmd		= Tcl_CreateObjCommand2(interp,
	    Tk_PathName(entryPtr->tkwin), EntryWidgetObjCmd, entryPtr,
	    EntryCmdDeletedProc);
    entryPtr->optionTable	= optionTable;
    entryPtr->type		= TK_ENTRY;
    tmp				= (char *)ckalloc(1);
    tmp[0]			= '\0';
    entryPtr->string		= tmp;
    entryPtr->selectFirst	= TCL_INDEX_NONE;
    entryPtr->selectLast	= TCL_INDEX_NONE;

    entryPtr->cursor		= NULL;
    entryPtr->exportSelection	= 1;
    entryPtr->justify		= TK_JUSTIFY_LEFT;
    entryPtr->relief		= TK_RELIEF_FLAT;
    entryPtr->state		= STATE_NORMAL;
    entryPtr->displayString	= entryPtr->string;
    entryPtr->inset		= XPAD;
    entryPtr->textGC		= NULL;
    entryPtr->selTextGC		= NULL;
    entryPtr->highlightGC	= NULL;
    entryPtr->avgWidth		= 1;
    entryPtr->validate		= VALIDATE_NONE;

    entryPtr->placeholderGC	= NULL;

    /*
     * Keep a hold of the associated tkwin until we destroy the entry,
     * otherwise Tk might free it while we still need it.
     */

    Tcl_Preserve(entryPtr->tkwin);

    Tk_SetClass(entryPtr->tkwin, "Entry");
    Tk_SetClassProcs(entryPtr->tkwin, &entryClass, entryPtr);
    Tk_CreateEventHandler(entryPtr->tkwin,
	    ExposureMask|StructureNotifyMask|FocusChangeMask,
	    EntryEventProc, entryPtr);
    Tk_CreateSelHandler(entryPtr->tkwin, XA_PRIMARY, XA_STRING,
	    EntryFetchSelection, entryPtr, XA_STRING);

    if ((Tk_InitOptions(interp, entryPtr, optionTable, tkwin)
	    != TCL_OK) ||
	    (ConfigureEntry(interp, entryPtr, objc-2, objv+2) != TCL_OK)) {
	Tk_DestroyWindow(entryPtr->tkwin);
	return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tk_NewWindowObj(entryPtr->tkwin));
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * EntryWidgetObjCmd --
 *
 *	This function is invoked to process the Tcl command that corresponds
 *	to a widget managed by this module. See the user documentation for
 *	details on what it does.
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
EntryWidgetObjCmd(
    void *clientData,	/* Information about entry widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Entry *entryPtr = (Entry *)clientData;
    int cmdIndex, selIndex, result;
    Tcl_Obj *objPtr;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
	return TCL_ERROR;
    }

    /*
     * Parse the widget command by looking up the second token in the list of
     * valid command names.
     */

    result = Tcl_GetIndexFromObj(interp, objv[1], entryCmdNames, "option", 0,
	    &cmdIndex);
    if (result != TCL_OK) {
	return result;
    }

    Tcl_Preserve(entryPtr);
    switch ((enum entryCmd) cmdIndex) {
    case COMMAND_BBOX: {
	Tcl_Size index;
	int x, y, width, height;
	Tcl_Obj *bbox[4];

	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "index");
	    goto error;
	}
	if (GetEntryIndex(interp, entryPtr, objv[2],
		&index) != TCL_OK) {
	    goto error;
	}
	if ((index == entryPtr->numChars) && (index + 1 > 1)) {
	    index--;
	}
	Tk_CharBbox(entryPtr->textLayout, index, &x, &y, &width, &height);
	bbox[0] = Tcl_NewWideIntObj(x + entryPtr->layoutX);
	bbox[1] = Tcl_NewWideIntObj(y + entryPtr->layoutY);
	bbox[2] = Tcl_NewWideIntObj(width);
	bbox[3] = Tcl_NewWideIntObj(height);
	Tcl_SetObjResult(interp, Tcl_NewListObj(4, bbox));
	break;
    }

    case COMMAND_CGET:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option");
	    goto error;
	}

	objPtr = Tk_GetOptionValue(interp, entryPtr,
		entryPtr->optionTable, objv[2], entryPtr->tkwin);
	if (objPtr == NULL) {
	    goto error;
	}
	Tcl_SetObjResult(interp, objPtr);
	break;

    case COMMAND_CONFIGURE:
	if (objc <= 3) {
	    objPtr = Tk_GetOptionInfo(interp, entryPtr,
		    entryPtr->optionTable,
		    (objc == 3) ? objv[2] : NULL,
		    entryPtr->tkwin);
	    if (objPtr == NULL) {
		goto error;
	    }
	    Tcl_SetObjResult(interp, objPtr);
	} else {
	    result = ConfigureEntry(interp, entryPtr, objc-2, objv+2);
	}
	break;

    case COMMAND_DELETE: {
	Tcl_Size first, last;
	int code;

	if ((objc < 3) || (objc > 4)) {
	    Tcl_WrongNumArgs(interp, 2, objv, "firstIndex ?lastIndex?");
	    goto error;
	}
	if (GetEntryIndex(interp, entryPtr, objv[2],
		&first) != TCL_OK) {
	    goto error;
	}
	if (objc == 3) {
	    last = first + 1;
	} else if (GetEntryIndex(interp, entryPtr, objv[3],
		&last) != TCL_OK) {
	    goto error;
	}
	if ((last >= first) && (entryPtr->state == STATE_NORMAL)) {
	    code = DeleteChars(entryPtr, first, last - first);
	    if (code != TCL_OK) {
		goto error;
	    }
	}
	break;
    }

    case COMMAND_GET:
	if (objc != 2) {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    goto error;
	}
	Tcl_SetObjResult(interp, Tcl_NewStringObj(entryPtr->string, TCL_INDEX_NONE));
	break;

    case COMMAND_ICURSOR:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "pos");
	    goto error;
	}
	if (GetEntryIndex(interp, entryPtr, objv[2],
		&entryPtr->insertPos) != TCL_OK) {
	    goto error;
	}
	EventuallyRedraw(entryPtr);
	break;

    case COMMAND_INDEX: {
	Tcl_Size index;

	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "string");
	    goto error;
	}
	if (GetEntryIndex(interp, entryPtr, objv[2],
		&index) != TCL_OK) {
	    goto error;
	}
	Tcl_SetObjResult(interp, TkNewIndexObj(index));
	break;
    }

    case COMMAND_INSERT: {
	Tcl_Size index;
	int code;

	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "index text");
	    goto error;
	}
	if (GetEntryIndex(interp, entryPtr, objv[2],
		&index) != TCL_OK) {
	    goto error;
	}
	if (entryPtr->state == STATE_NORMAL) {
	    code = InsertChars(entryPtr, index, objv[3]);
	    if (code != TCL_OK) {
		goto error;
	    }
	}
	break;
    }

    case COMMAND_SCAN: {
	int x;
	const char *minorCmd;

	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "mark|dragto x");
	    goto error;
	}
	if (Tcl_GetIntFromObj(interp, objv[3], &x) != TCL_OK) {
	    goto error;
	}

	minorCmd = Tcl_GetString(objv[2]);
	if (minorCmd[0] == 'm'
		&& (strncmp(minorCmd, "mark", strlen(minorCmd)) == 0)) {
	    entryPtr->scanMarkX = x;
	    entryPtr->scanMarkIndex = entryPtr->leftIndex;
	} else if ((minorCmd[0] == 'd')
		&& (strncmp(minorCmd, "dragto", strlen(minorCmd)) == 0)) {
	    EntryScanTo(entryPtr, x);
	} else {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "bad scan option \"%s\": must be dragto or mark",
		    minorCmd));
	    Tcl_SetErrorCode(interp, "TCL", "LOOKUP", "INDEX", "scan option",
		    minorCmd, NULL);
	    goto error;
	}
	break;
    }

    case COMMAND_SELECTION: {
	Tcl_Size index, index2;

	if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option ?index?");
	    goto error;
	}

	/*
	 * Parse the selection sub-command, using the command table
	 * "selCmdNames" defined above.
	 */

	result = Tcl_GetIndexFromObj(interp, objv[2], selCmdNames,
		"selection option", 0, &selIndex);
	if (result != TCL_OK) {
	    goto error;
	}

	/*
	 * Disabled entries don't allow the selection to be modified, but
	 * 'selection present' must return a boolean.
	 */

	if ((entryPtr->state == STATE_DISABLED)
		&& (selIndex != SELECTION_PRESENT)) {
	    goto done;
	}

	switch (selIndex) {
	case SELECTION_ADJUST:
	    if (objc != 4) {
		Tcl_WrongNumArgs(interp, 3, objv, "index");
		goto error;
	    }
	    if (GetEntryIndex(interp, entryPtr,
		    objv[3], &index) != TCL_OK) {
		goto error;
	    }
	    if (entryPtr->selectFirst != TCL_INDEX_NONE) {
		Tcl_Size half1, half2;

		half1 = (entryPtr->selectFirst + entryPtr->selectLast)/2;
		half2 = (entryPtr->selectFirst + entryPtr->selectLast + 1)/2;
		if (index < half1) {
		    entryPtr->selectAnchor = entryPtr->selectLast;
		} else if (index > half2) {
		    entryPtr->selectAnchor = entryPtr->selectFirst;
		} else {
		    /*
		     * We're at about the halfway point in the selection; just
		     * keep the existing anchor.
		     */
		}
	    }
	    EntrySelectTo(entryPtr, index);
	    break;

	case SELECTION_CLEAR:
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 3, objv, NULL);
		goto error;
	    }
	    if (entryPtr->selectFirst != TCL_INDEX_NONE) {
		entryPtr->selectFirst = TCL_INDEX_NONE;
		entryPtr->selectLast = TCL_INDEX_NONE;
		EventuallyRedraw(entryPtr);
	    }
	    goto done;

	case SELECTION_FROM:
	    if (objc != 4) {
		Tcl_WrongNumArgs(interp, 3, objv, "index");
		goto error;
	    }
	    if (GetEntryIndex(interp, entryPtr,
		    objv[3], &index) != TCL_OK) {
		goto error;
	    }
	    entryPtr->selectAnchor = index;
	    break;

	case SELECTION_PRESENT:
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 3, objv, NULL);
		goto error;
	    }
	    Tcl_SetObjResult(interp,
		    Tcl_NewBooleanObj(entryPtr->selectFirst != TCL_INDEX_NONE));
	    goto done;

	case SELECTION_RANGE:
	    if (objc != 5) {
		Tcl_WrongNumArgs(interp, 3, objv, "start end");
		goto error;
	    }
	    if (GetEntryIndex(interp, entryPtr, objv[3],
		    &index) != TCL_OK) {
		goto error;
	    }
	    if (GetEntryIndex(interp, entryPtr, objv[4],
		    &index2) != TCL_OK) {
		goto error;
	    }
	    if (index >= index2) {
		entryPtr->selectFirst = TCL_INDEX_NONE;
		entryPtr->selectLast = TCL_INDEX_NONE;
	    } else {
		entryPtr->selectFirst = index;
		entryPtr->selectLast = index2;
	    }
	    if (!(entryPtr->flags & GOT_SELECTION)
		    && (entryPtr->exportSelection)
		    && (!Tcl_IsSafe(entryPtr->interp))) {
		Tk_OwnSelection(entryPtr->tkwin, XA_PRIMARY,
			EntryLostSelection, entryPtr);
		entryPtr->flags |= GOT_SELECTION;
	    }
	    EventuallyRedraw(entryPtr);
	    break;

	case SELECTION_TO:
	    if (objc != 4) {
		Tcl_WrongNumArgs(interp, 3, objv, "index");
		goto error;
	    }
	    if (GetEntryIndex(interp, entryPtr,
		    objv[3], &index) != TCL_OK) {
		goto error;
	    }
	    EntrySelectTo(entryPtr, index);
	    break;
	}
	break;
    }

    case COMMAND_VALIDATE: {
	int code;

	if (objc != 2) {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    goto error;
	}
	selIndex = entryPtr->validate;
	entryPtr->validate = VALIDATE_ALL;
	code = EntryValidateChange(entryPtr, NULL, entryPtr->string,
		-1, VALIDATE_FORCED);
	if (entryPtr->validate != VALIDATE_NONE) {
	    entryPtr->validate = selIndex;
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(code == TCL_OK));
	break;
    }

    case COMMAND_XVIEW: {
	Tcl_Size index;

	if (objc == 2) {
	    double first, last;
	    Tcl_Obj *span[2];

	    EntryVisibleRange(entryPtr, &first, &last);
	    span[0] = Tcl_NewDoubleObj(first);
	    span[1] = Tcl_NewDoubleObj(last);
	    Tcl_SetObjResult(interp, Tcl_NewListObj(2, span));
	    goto done;
	} else if (objc == 3) {
	    if (GetEntryIndex(interp, entryPtr, objv[2],
		    &index) != TCL_OK) {
		goto error;
	    }
	} else {
	    double fraction;
	    int count;

	    index = entryPtr->leftIndex;
	    switch (Tk_GetScrollInfoObj(interp, objc, objv, &fraction,
		    &count)) {
	    case TK_SCROLL_MOVETO:
		index = (int) ((fraction * entryPtr->numChars) + 0.5);
		break;
	    case TK_SCROLL_PAGES: {
		int charsPerPage;

		charsPerPage = ((Tk_Width(entryPtr->tkwin)
			- 2 * entryPtr->inset) / entryPtr->avgWidth) - 2;
		if (charsPerPage < 1) {
		    charsPerPage = 1;
		}
		index += count * charsPerPage;
		break;
	    }
	    case TK_SCROLL_UNITS:
		index += count;
		break;
	    default:
		goto error;
	    }
	}
	if (index >= entryPtr->numChars) {
	    index = entryPtr->numChars - 1;
	}
	if ((int)index < 0) {
	    index = 0;
	}
	entryPtr->leftIndex = index;
	entryPtr->flags |= UPDATE_SCROLLBAR;
	EntryComputeGeometry(entryPtr);
	EventuallyRedraw(entryPtr);
	break;
    }
    }

  done:
    Tcl_Release(entryPtr);
    return result;

  error:
    Tcl_Release(entryPtr);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyEntry --
 *
 *	This function is invoked by Tcl_EventuallyFree or Tcl_Release to clean
 *	up the internal structure of an entry at a safe time (when no-one is
 *	using it anymore).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Everything associated with the entry is freed up.
 *
 *----------------------------------------------------------------------
 */

static void
DestroyEntry(
    void *memPtr)		/* Info about entry widget. */
{
    Entry *entryPtr = (Entry *)memPtr;

    /*
     * Free up all the stuff that requires special handling, then let
     * Tk_FreeOptions handle all the standard option-related stuff.
     */

    ckfree((void *)entryPtr->string);
    if (entryPtr->textVarNameObj != NULL) {
	Tcl_UntraceVar2(entryPtr->interp, Tcl_GetString(entryPtr->textVarNameObj),
		NULL, TCL_GLOBAL_ONLY|TCL_TRACE_WRITES|TCL_TRACE_UNSETS,
		EntryTextVarProc, entryPtr);
	entryPtr->flags &= ~ENTRY_VAR_TRACED;
    }
    if (entryPtr->textGC != NULL) {
	Tk_FreeGC(entryPtr->display, entryPtr->textGC);
    }
    if (entryPtr->selTextGC != NULL) {
	Tk_FreeGC(entryPtr->display, entryPtr->selTextGC);
    }
    Tcl_DeleteTimerHandler(entryPtr->insertBlinkHandler);
    if (entryPtr->displayString != entryPtr->string) {
	ckfree((void *)entryPtr->displayString);
    }
    if (entryPtr->type == TK_SPINBOX) {
	Spinbox *sbPtr = (Spinbox *) entryPtr;

	if (sbPtr->listObj != NULL) {
	    Tcl_DecrRefCount(sbPtr->listObj);
	    sbPtr->listObj = NULL;
	}
	if (sbPtr->formatBuf) {
	    ckfree(sbPtr->formatBuf);
	}
    }
    Tk_FreeTextLayout(entryPtr->textLayout);
    Tk_FreeConfigOptions(entryPtr, entryPtr->optionTable,
	    entryPtr->tkwin);
    Tcl_Release(entryPtr->tkwin);
    entryPtr->tkwin = NULL;

    ckfree(entryPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureEntry --
 *
 *	This function is called to process an objv/objc list, plus the Tk
 *	option database, in order to configure (or reconfigure) an entry
 *	widget.
 *
 * Results:
 *	The return value is a standard Tcl result. If TCL_ERROR is returned,
 *	then the interp's result contains an error message.
 *
 * Side effects:
 *	Configuration information, such as colors, border width, etc. get set
 *	for entryPtr; old resources get freed, if there were any.
 *
 *----------------------------------------------------------------------
 */

static int
ConfigureEntry(
    Tcl_Interp *interp,		/* Used for error reporting. */
    Entry *entryPtr,		/* Information about widget; may or may not
				 * already have values for some fields. */
    Tcl_Size objc,			/* Number of valid entries in argv. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Tk_SavedOptions savedOptions;
    Tk_3DBorder border;
    Tcl_Obj *errorResult = NULL;
    Spinbox *sbPtr = (Spinbox *) entryPtr;
				/* Only used when this widget is of type
				 * TK_SPINBOX */
    Tcl_Obj *oldValues = NULL;
    Tcl_Obj *oldFormat = NULL;
    int error;
    int oldExport = 0;
    int valuesChanged = 0;
    double oldFrom = 0.0;
    double oldTo = 0.0;
    int code;
    size_t formatSpace = TCL_DOUBLE_SPACE;
    int borderWidth, highlightWidth, insertWidth;
    int insertBorderWidth, selBorderWidth;

    /*
     * Eliminate any existing trace on a variable monitored by the entry.
     */

    if ((entryPtr->textVarNameObj != NULL)
	    && (entryPtr->flags & ENTRY_VAR_TRACED)) {
	Tcl_UntraceVar2(interp, Tcl_GetString(entryPtr->textVarNameObj), NULL,
		TCL_GLOBAL_ONLY|TCL_TRACE_WRITES|TCL_TRACE_UNSETS,
		EntryTextVarProc, entryPtr);
	entryPtr->flags &= ~ENTRY_VAR_TRACED;
    }

    /*
     * Store old values that we need to effect certain behavior if they change
     * value.
     */

    oldExport = (entryPtr->exportSelection) && (!Tcl_IsSafe(entryPtr->interp));
    if (entryPtr->type == TK_SPINBOX) {
	oldValues = sbPtr->valueObj;
	oldFormat = sbPtr->reqFormatObj;
	oldFrom = sbPtr->fromValue;
	oldTo = sbPtr->toValue;
    }

    for (error = 0; error <= 1; error++) {
	if (!error) {
	    /*
	     * First pass: set options to new values.
	     */

	    if (Tk_SetOptions(interp, entryPtr,
		    entryPtr->optionTable, objc, objv,
		    entryPtr->tkwin, &savedOptions, NULL) != TCL_OK) {
		continue;
	    }
	} else {
	    /*
	     * Second pass: restore options to old values.
	     */

	    errorResult = Tcl_GetObjResult(interp);
	    Tcl_IncrRefCount(errorResult);
	    Tk_RestoreSavedOptions(&savedOptions);
	}

	/*
	 * A few other options also need special processing, such as parsing
	 * the geometry and setting the background from a 3-D border.
	 */

	if ((entryPtr->state == STATE_DISABLED) &&
		(entryPtr->disabledBorder != NULL)) {
	    border = entryPtr->disabledBorder;
	} else if ((entryPtr->state == STATE_READONLY) &&
		(entryPtr->readonlyBorder != NULL)) {
	    border = entryPtr->readonlyBorder;
	} else {
	    border = entryPtr->normalBorder;
	}
	Tk_SetBackgroundFromBorder(entryPtr->tkwin, border);

	Tk_GetPixelsFromObj(NULL, entryPtr->tkwin, entryPtr->borderWidthObj, &borderWidth);
	Tk_GetPixelsFromObj(NULL, entryPtr->tkwin, entryPtr->highlightWidthObj, &highlightWidth);
	Tk_GetPixelsFromObj(NULL, entryPtr->tkwin, entryPtr->insertBorderWidthObj, &insertBorderWidth);
	Tk_GetPixelsFromObj(NULL, entryPtr->tkwin, entryPtr->insertWidthObj, &insertWidth);
	Tk_GetPixelsFromObj(NULL, entryPtr->tkwin, entryPtr->selBorderWidthObj, &selBorderWidth);

	if (entryPtr->type == TK_SPINBOX) {
	    if (sbPtr->fromValue > sbPtr->toValue) {
		/*
		 * Swap -from and -to values.
		 */

		double tmpFromTo = sbPtr->fromValue;

		sbPtr->fromValue = sbPtr->toValue;
		sbPtr->toValue = tmpFromTo;
	    }

	    if (sbPtr->reqFormatObj && (oldFormat != sbPtr->reqFormatObj)) {
		/*
		 * Make sure that the given format is somewhat correct, and
		 * calculate the minimum space we'll need for the values as
		 * strings.
		 */

		int min, max;
		size_t formatLen;
		char fbuf[4], *fmt = Tcl_GetString(sbPtr->reqFormatObj);

		formatLen = strlen(fmt);
		if ((fmt[0] != '%') || (fmt[formatLen-1] != 'f')) {
		badFormatOpt:
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			    "bad spinbox format specifier \"%s\"",
			    Tcl_GetString(sbPtr->reqFormatObj)));
		    Tcl_SetErrorCode(interp, "TK", "SPINBOX", "FORMAT_SANITY",
			    NULL);
		    continue;
		}
		if ((sscanf(fmt, "%%%d.%d%[f]", &min, &max, fbuf) == 3)
			&& (max >= 0)) {
		    formatSpace = min + max + 1;
		} else if (((sscanf(fmt, "%%.%d%[f]", &min, fbuf) == 2)
			|| (sscanf(fmt, "%%%d%[f]", &min, fbuf) == 2)
			|| (sscanf(fmt, "%%%d.%[f]", &min, fbuf) == 2))
			&& (min >= 0)) {
		    formatSpace = min + 1;
		} else {
		    goto badFormatOpt;
		}
		if (formatSpace < TCL_DOUBLE_SPACE) {
		    formatSpace = TCL_DOUBLE_SPACE;
		}
		sbPtr->formatBuf = (char *)ckrealloc(sbPtr->formatBuf, formatSpace);

		/*
		 * We perturb the value of oldFrom to allow us to go into the
		 * branch below that will reformat the displayed value.
		 */

		oldFrom = sbPtr->fromValue - 1;
	    }

	    /*
	     * See if we have to rearrange our listObj data.
	     */

	    if (oldValues != sbPtr->valueObj) {
		if (sbPtr->listObj != NULL) {
		    Tcl_DecrRefCount(sbPtr->listObj);
		}
		sbPtr->listObj = NULL;
		if (sbPtr->valueObj != NULL) {
		    Tcl_Obj *newObjPtr;
		    Tcl_Size nelems;

		    newObjPtr = sbPtr->valueObj;
		    if (Tcl_ListObjLength(interp, newObjPtr, &nelems)
			    != TCL_OK) {
			valuesChanged = -1;
			continue;
		    }
		    sbPtr->listObj = newObjPtr;
		    Tcl_IncrRefCount(sbPtr->listObj);
		    sbPtr->nElements = nelems;
		    sbPtr->eIndex = 0;
		    valuesChanged++;
		}
	    }
	}

	/*
	 * Restart the cursor timing sequence in case the on-time or off-time
	 * just changed. Set validate temporarily to none, so the configure
	 * doesn't cause it to be triggered.
	 */

	if (entryPtr->flags & GOT_FOCUS) {
	    int validate = entryPtr->validate;

	    entryPtr->validate = VALIDATE_NONE;
	    EntryFocusProc(entryPtr, 1);
	    entryPtr->validate = validate;
	}

	/*
	 * Claim the selection if we've suddenly started exporting it.
	 */

	if (entryPtr->exportSelection && (!oldExport)
		&& (!Tcl_IsSafe(entryPtr->interp))
		&& (entryPtr->selectFirst != TCL_INDEX_NONE)
		&& !(entryPtr->flags & GOT_SELECTION)) {
	    Tk_OwnSelection(entryPtr->tkwin, XA_PRIMARY, EntryLostSelection,
		    entryPtr);
	    entryPtr->flags |= GOT_SELECTION;
	}

	/*
	 * Recompute the window's geometry and arrange for it to be
	 * redisplayed.
	 */

	Tk_SetInternalBorder(entryPtr->tkwin,
		borderWidth + highlightWidth);
	entryPtr->inset = highlightWidth
		+ borderWidth + XPAD;
	break;
    }
    if (!error) {
	Tk_FreeSavedOptions(&savedOptions);
    }

    /*
     * If the entry is tied to the value of a variable, create the variable if
     * it doesn't exist, and set the entry's value from the variable's value.
     */

    if (entryPtr->textVarNameObj != NULL) {
	const char *value;

	value = Tcl_GetVar2(interp, Tcl_GetString(entryPtr->textVarNameObj), NULL, TCL_GLOBAL_ONLY);
	if (value == NULL) {

	    /*
	     * Since any trace on the textvariable was eliminated above,
	     * the only possible reason for EntryValueChanged to return
	     * an error is that the textvariable lives in a namespace
	     * that does not (yet) exist. Indeed, namespaces are not
	     * automatically created as needed. Don't trap this error
	     * here, better do it below when attempting to trace the
	     * variable.
	     */

	    EntryValueChanged(entryPtr, NULL);
	} else {
	    EntrySetValue(entryPtr, value);
	}
    }

    if (entryPtr->type == TK_SPINBOX) {
	ComputeFormat(sbPtr);

	if (valuesChanged > 0) {
	    Tcl_Obj *objPtr;

	    /*
	     * No check for error return, because there shouldn't be one given
	     * the check for valid list above.
	     */

	    Tcl_ListObjIndex(interp, sbPtr->listObj, 0, &objPtr);

	    /*
	     * No check for error return here as well, because any possible
	     * error will be trapped below when attempting tracing.
	     */

	    EntryValueChanged(entryPtr, Tcl_GetString(objPtr));
	} else if ((sbPtr->valueObj == NULL)
		&& !DOUBLES_EQ(sbPtr->fromValue, sbPtr->toValue)
		&& (!DOUBLES_EQ(sbPtr->fromValue, oldFrom)
			|| !DOUBLES_EQ(sbPtr->toValue, oldTo))) {
	    /*
	     * If the valueStr is empty and -from && -to are specified, check
	     * to see if the current string is within the range. If not, it
	     * will be constrained to the nearest edge. If the current string
	     * isn't a double value, we set it to -from.
	     */

	    double dvalue;

	    if (sscanf(entryPtr->string, "%lf", &dvalue) <= 0) {
		/* Scan failure */
		dvalue = sbPtr->fromValue;
	    } else if (dvalue > sbPtr->toValue) {
		dvalue = sbPtr->toValue;
	    } else if (dvalue < sbPtr->fromValue) {
		dvalue = sbPtr->fromValue;
	    }
	    snprintf(sbPtr->formatBuf, formatSpace, sbPtr->valueFormat, dvalue);

	    /*
	     * No check for error return here as well, because any possible
	     * error will be trapped below when attempting tracing.
	     */

	    EntryValueChanged(entryPtr, sbPtr->formatBuf);
	}
    }

    /*
     * Set up a trace on the variable's value after we've possibly constrained
     * the value according to new -from/-to values.
     */

    if ((entryPtr->textVarNameObj != NULL)
	    && !(entryPtr->flags & ENTRY_VAR_TRACED)) {
	code = Tcl_TraceVar2(interp, Tcl_GetString(entryPtr->textVarNameObj),
		NULL, TCL_GLOBAL_ONLY|TCL_TRACE_WRITES|TCL_TRACE_UNSETS,
		EntryTextVarProc, entryPtr);
	if (code != TCL_OK) {
	    return TCL_ERROR;
	}
	entryPtr->flags |= ENTRY_VAR_TRACED;
    }

    EntryWorldChanged(entryPtr);
    if (error) {
	Tcl_SetObjResult(interp, errorResult);
	Tcl_DecrRefCount(errorResult);
	return TCL_ERROR;
    } else {
	return TCL_OK;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * EntryWorldChanged --
 *
 *	This function is called when the world has changed in some way and the
 *	widget needs to recompute all its graphics contexts and determine its
 *	new geometry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Entry will be relayed out and redisplayed.
 *
 *---------------------------------------------------------------------------
 */

static void
EntryWorldChanged(
    void *instanceData)	/* Information about widget. */
{
    XGCValues gcValues;
    GC gc = NULL;
    unsigned long mask;
    Tk_3DBorder border;
    XColor *colorPtr;
    Entry *entryPtr = (Entry *)instanceData;

    entryPtr->avgWidth = Tk_TextWidth(entryPtr->tkfont, "0", 1);
    if (entryPtr->avgWidth == 0) {
	entryPtr->avgWidth = 1;
    }

    if (entryPtr->type == TK_SPINBOX) {
	/*
	 * Compute the button width for a spinbox
	 */

	entryPtr->xWidth = entryPtr->avgWidth + 2 * (1+XPAD);
	if (entryPtr->xWidth < 11) {
	    entryPtr->xWidth = 11; /* we want a min visible size */
	}
    }

    /*
     * Default background and foreground are from the normal state. In a
     * disabled state, both of those may be overridden; in the readonly state,
     * the background may be overridden.
     */

    border = entryPtr->normalBorder;
    colorPtr = entryPtr->fgColorPtr;
    switch (entryPtr->state) {
    case STATE_DISABLED:
	if (entryPtr->disabledBorder != NULL) {
	    border = entryPtr->disabledBorder;
	}
	if (entryPtr->dfgColorPtr != NULL) {
	    colorPtr = entryPtr->dfgColorPtr;
	}
	break;
    case STATE_READONLY:
	if (entryPtr->readonlyBorder != NULL) {
	    border = entryPtr->readonlyBorder;
	}
	break;
    }

    Tk_SetBackgroundFromBorder(entryPtr->tkwin, border);
    gcValues.foreground = colorPtr->pixel;
    gcValues.font = Tk_FontId(entryPtr->tkfont);
    gcValues.graphics_exposures = False;
    mask = GCForeground | GCFont | GCGraphicsExposures;
    gc = Tk_GetGC(entryPtr->tkwin, mask, &gcValues);
    if (entryPtr->textGC != NULL) {
	Tk_FreeGC(entryPtr->display, entryPtr->textGC);
    }
    entryPtr->textGC = gc;

    if (entryPtr->placeholderColorPtr != NULL) {
	gcValues.foreground = entryPtr->placeholderColorPtr->pixel;
    }
    mask = GCForeground | GCFont | GCGraphicsExposures;
    gc = Tk_GetGC(entryPtr->tkwin, mask, &gcValues);
    if (entryPtr->placeholderGC != NULL) {
	Tk_FreeGC(entryPtr->display, entryPtr->placeholderGC);
    }
    entryPtr->placeholderGC = gc;

    if (entryPtr->selFgColorPtr != NULL) {
	gcValues.foreground = entryPtr->selFgColorPtr->pixel;
    } else {
	gcValues.foreground = colorPtr->pixel;
    }
    gcValues.font = Tk_FontId(entryPtr->tkfont);
    mask = GCForeground | GCFont;
    gc = Tk_GetGC(entryPtr->tkwin, mask, &gcValues);
    if (entryPtr->selTextGC != NULL) {
	Tk_FreeGC(entryPtr->display, entryPtr->selTextGC);
    }
    entryPtr->selTextGC = gc;

    /*
     * Recompute the window's geometry and arrange for it to be redisplayed.
     */

    EntryComputeGeometry(entryPtr);
    entryPtr->flags |= UPDATE_SCROLLBAR;
    EventuallyRedraw(entryPtr);
}

#ifndef MAC_OSX_TK
/*
 *--------------------------------------------------------------
 *
 * TkpDrawEntryBorderAndFocus --
 *
 *	Stub function for Tk on platforms other than Aqua
 *	(Windows and X11), which do not draw native entry borders.
 *	See macosx/tkMacOSXEntry.c for function definition in Tk Aqua.
 *
 * Results:
 *	Returns 0.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

int
TkpDrawEntryBorderAndFocus(
    TCL_UNUSED(Entry *),
    TCL_UNUSED(Drawable),
    TCL_UNUSED(int))
{
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * TkpDrawSpinboxButtons --
 *
 *	Stub function for Tk on platforms other than Aqua
 *	(Windows and X11), which do not draw native spinbox buttons.
 *	See macosx/tkMacOSXEntry.c for function definition in Tk Aqua.
 *
 * Results:
 *	Returns 0.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

int
TkpDrawSpinboxButtons(
    TCL_UNUSED(Spinbox *),
    TCL_UNUSED(Pixmap))
{
    return 0;
}
#endif /* Not MAC_OSX_TK */

/*
 *--------------------------------------------------------------
 *
 * DisplayEntry --
 *
 *	This function redraws the contents of an entry window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information appears on the screen.
 *
 *--------------------------------------------------------------
 */

static void
DisplayEntry(
    void *clientData)	/* Information about window. */
{
    Entry *entryPtr = (Entry *)clientData;
    Tk_Window tkwin = entryPtr->tkwin;
    int baseY, selStartX, selEndX, cursorX;
    int showSelection, xBound;
    Tk_FontMetrics fm;
    Pixmap pixmap;
    Tk_3DBorder border;
    int borderWidth, selBorderWidth, insertWidth, highlightWidth;

    entryPtr->flags &= ~REDRAW_PENDING;
    if ((entryPtr->flags & ENTRY_DELETED) || !Tk_IsMapped(tkwin)) {
	return;
    }

    Tk_GetFontMetrics(entryPtr->tkfont, &fm);

    /*
     * Update the scrollbar if that's needed.
     */

    if (entryPtr->flags & UPDATE_SCROLLBAR) {
	entryPtr->flags &= ~UPDATE_SCROLLBAR;

	/*
	 * Preserve/Release because updating the scrollbar can have the
	 * side-effect of destroying or unmapping the entry widget.
	 */

	Tcl_Preserve(entryPtr);
	EntryUpdateScrollbar(entryPtr);

	if ((entryPtr->flags & ENTRY_DELETED) || !Tk_IsMapped(tkwin)) {
	    Tcl_Release(entryPtr);
	    return;
	}
	Tcl_Release(entryPtr);
    }

#ifndef TK_NO_DOUBLE_BUFFERING
    /*
     * In order to avoid screen flashes, this function redraws the textual
     * area of the entry into off-screen memory, then copies it back on-screen
     * in a single operation. This means there's no point in time where the
     * on-screen image has been cleared.
     */

    pixmap = Tk_GetPixmap(entryPtr->display, Tk_WindowId(tkwin),
	    Tk_Width(tkwin), Tk_Height(tkwin), Tk_Depth(tkwin));
#else
    pixmap = Tk_WindowId(tkwin);
#endif /* TK_NO_DOUBLE_BUFFERING */

    /*
     * Compute x-coordinate of the pixel just after last visible one, plus
     * vertical position of baseline of text.
     */

    xBound = Tk_Width(tkwin) - entryPtr->inset - entryPtr->xWidth;
    baseY = (Tk_Height(tkwin) + fm.ascent - fm.descent) / 2;

    /*
     * Hide the selection whenever we don't have the focus, unless we
     * always want to show selection.
     */
    if (Tk_AlwaysShowSelection(entryPtr->tkwin)) {
	showSelection = 1;
    } else {
	showSelection = (entryPtr->flags & GOT_FOCUS);
    }

    /*
     * Draw the background in three layers. From bottom to top the layers are:
     * normal background, selection background, and insertion cursor
     * background.
     */

    if ((entryPtr->state == STATE_DISABLED) &&
	    (entryPtr->disabledBorder != NULL)) {
	border = entryPtr->disabledBorder;
    } else if ((entryPtr->state == STATE_READONLY) &&
	    (entryPtr->readonlyBorder != NULL)) {
	border = entryPtr->readonlyBorder;
    } else {
	border = entryPtr->normalBorder;
    }
    Tk_Fill3DRectangle(tkwin, pixmap, border,
	    0, 0, Tk_Width(tkwin), Tk_Height(tkwin), 0, TK_RELIEF_FLAT);
    Tk_GetPixelsFromObj(NULL, tkwin, entryPtr->selBorderWidthObj, &selBorderWidth);

    if (showSelection && (entryPtr->state != STATE_DISABLED)
	    && (entryPtr->selectLast > entryPtr->leftIndex)) {
	if (entryPtr->selectFirst <= entryPtr->leftIndex) {
	    selStartX = entryPtr->leftX;
	} else {
	    Tk_CharBbox(entryPtr->textLayout, entryPtr->selectFirst,
		    &selStartX, NULL, NULL, NULL);
	    selStartX += entryPtr->layoutX;
	}
	if ((selStartX - selBorderWidth) < xBound) {
	    Tk_CharBbox(entryPtr->textLayout, entryPtr->selectLast,
		    &selEndX, NULL, NULL, NULL);
	    selEndX += entryPtr->layoutX;
	    Tk_Fill3DRectangle(tkwin, pixmap, entryPtr->selBorder,
		    selStartX - selBorderWidth,
		    baseY - fm.ascent - selBorderWidth,
		    (selEndX - selStartX) + 2 * selBorderWidth,
		    (fm.ascent + fm.descent) + 2 * selBorderWidth,
		    selBorderWidth,
#ifndef MAC_OSX_TK
		    TK_RELIEF_RAISED
#else
		    MAC_OSX_ENTRY_SELECT_RELIEF
#endif
		    );
	}
    }

    /*
     * Draw a special background for the insertion cursor, overriding even the
     * selection background. As a special hack to keep the cursor visible when
     * the insertion cursor color is the same as the color for selected text
     * (e.g., on mono displays), write background in the cursor area (instead
     * of nothing) when the cursor isn't on. Otherwise the selection would
     * hide the cursor.
     */

    Tk_GetPixelsFromObj(NULL, tkwin, entryPtr->insertWidthObj, &insertWidth);
    if ((entryPtr->state == STATE_NORMAL) && (entryPtr->flags & GOT_FOCUS)) {
	Tk_CharBbox(entryPtr->textLayout, entryPtr->insertPos, &cursorX, NULL,
		NULL, NULL);
	cursorX += entryPtr->layoutX;
	cursorX -= (insertWidth <= 1) ? 1 : (insertWidth)/2;
	Tk_SetCaretPos(entryPtr->tkwin, cursorX, baseY - fm.ascent,
		fm.ascent + fm.descent);
	if ((entryPtr->insertPos >= entryPtr->leftIndex) && cursorX < xBound) {
	    if (entryPtr->flags & CURSOR_ON) {
		/* Take minimum of given insertBorderWidth and insertWidth/2 */
		int insertBorderWidth;
		Tk_GetPixelsFromObj(NULL, entryPtr->tkwin, entryPtr->insertBorderWidthObj, &insertBorderWidth);
		if (insertBorderWidth > insertWidth/2) {
		    insertBorderWidth = insertWidth/2;
		}
		Tk_Fill3DRectangle(tkwin, pixmap, entryPtr->insertBorder,
			cursorX, baseY - fm.ascent, insertWidth,
			fm.ascent + fm.descent, insertBorderWidth,
			TK_RELIEF_RAISED);
	    } else if (entryPtr->insertBorder == entryPtr->selBorder) {
		Tk_Fill3DRectangle(tkwin, pixmap, border, cursorX,
			baseY - fm.ascent, insertWidth,
			fm.ascent + fm.descent, 0, TK_RELIEF_FLAT);
	    }
	}
    }

    if ((entryPtr->numChars == 0) && (entryPtr->placeholderChars != 0)) {

	/*
	 * Draw the placeholder text.
	 */

	Tk_DrawTextLayout(entryPtr->display, pixmap, entryPtr->placeholderGC,
	    entryPtr->placeholderLayout, entryPtr->placeholderX, entryPtr->layoutY,
	    entryPtr->placeholderLeftIndex, entryPtr->placeholderChars);

    } else {

	if (showSelection && (entryPtr->state != STATE_DISABLED)
		&& (entryPtr->selTextGC != entryPtr->textGC)
		&& (entryPtr->selectFirst < entryPtr->selectLast)) {

	    /*
	     * Draw the selected and unselected portions separately.
	     */

	    Tcl_Size selFirst;

	    if (entryPtr->selectFirst < entryPtr->leftIndex) {
		selFirst = entryPtr->leftIndex;
	    } else {
		selFirst = entryPtr->selectFirst;
	    }
	    if (entryPtr->leftIndex < selFirst) {
		Tk_DrawTextLayout(entryPtr->display, pixmap, entryPtr->textGC,
			entryPtr->textLayout, entryPtr->layoutX, entryPtr->layoutY,
			entryPtr->leftIndex, selFirst);
	    }
	    Tk_DrawTextLayout(entryPtr->display, pixmap, entryPtr->selTextGC,
		    entryPtr->textLayout, entryPtr->layoutX, entryPtr->layoutY,
		    selFirst, entryPtr->selectLast);
	    if (entryPtr->selectLast < entryPtr->numChars) {
		Tk_DrawTextLayout(entryPtr->display, pixmap, entryPtr->textGC,
			entryPtr->textLayout, entryPtr->layoutX, entryPtr->layoutY,
			entryPtr->selectLast, entryPtr->numChars);
	    }
	} else {

	    /*
	     * Draw the entire visible text
	     */

	    Tk_DrawTextLayout(entryPtr->display, pixmap, entryPtr->textGC,
		    entryPtr->textLayout, entryPtr->layoutX, entryPtr->layoutY,
		    entryPtr->leftIndex, entryPtr->numChars);
	}
    }

    if (entryPtr->type == TK_SPINBOX) {
	int startx, height, inset, pad, tHeight, xWidth;
	Spinbox *sbPtr = (Spinbox *) entryPtr;

	/*
	 * Draw the spin button controls.
	 */

	if (TkpDrawSpinboxButtons(sbPtr, pixmap) == 0) {
	    xWidth = entryPtr->xWidth;
	    pad = XPAD + 1;
	    inset = entryPtr->inset - XPAD;
	    startx = Tk_Width(tkwin) - (xWidth + inset);
	    height = (Tk_Height(tkwin) - 2*inset)/2;
#if 0
	    Tk_Fill3DRectangle(tkwin, pixmap, sbPtr->buttonBorder,
		    startx, inset, xWidth, height, 1, sbPtr->buRelief);
	    Tk_Fill3DRectangle(tkwin, pixmap, sbPtr->buttonBorder,
		    startx, inset+height, xWidth, height, 1, sbPtr->bdRelief);
#else
	    Tk_Fill3DRectangle(tkwin, pixmap, sbPtr->buttonBorder,
		    startx, inset, xWidth, height, 1,
		    (sbPtr->selElement == SEL_BUTTONUP) ?
		    TK_RELIEF_SUNKEN : TK_RELIEF_RAISED);
	    Tk_Fill3DRectangle(tkwin, pixmap, sbPtr->buttonBorder,
		    startx, inset+height, xWidth, height, 1,
		    (sbPtr->selElement == SEL_BUTTONDOWN) ?
		    TK_RELIEF_SUNKEN : TK_RELIEF_RAISED);
#endif

	    xWidth -= 2*pad;

	    /*
	     * Only draw the triangles if we have enough display space
	     */

	    if ((xWidth > 1)) {
		XPoint points[3];
		int starty, space, offset;

		space = height - 2*pad;

		/*
		 * Ensure width of triangle is odd to guarantee a sharp tip
		 */

		if (!(xWidth % 2)) {
		    xWidth++;
		}
		tHeight = (xWidth + 1) / 2;
		if (tHeight > space) {
		    tHeight = space;
		}
		space	= (space - tHeight) / 2;
		startx += pad;
		starty	= inset + height - pad - space;
		offset	= (sbPtr->selElement == SEL_BUTTONUP);

		/*
		 * The points are slightly different for the up and down
		 * arrows because (for *.x), we need to account for a bug in
		 * the way XFillPolygon draws triangles, and we want to shift
		 * the arrows differently when allowing for depressed
		 * behavior.
		 */

		points[0].x = startx + offset;
		points[0].y = starty + (offset ? 0 : -1);
		points[1].x = startx + xWidth/2 + offset;
		points[1].y = starty - tHeight + (offset ? 0 : -1);
		points[2].x = startx + xWidth + offset;
		points[2].y = points[0].y;
		XFillPolygon(entryPtr->display, pixmap, entryPtr->textGC,
			points, 3, Convex, CoordModeOrigin);

		starty = inset + height + pad + space;
		offset = (sbPtr->selElement == SEL_BUTTONDOWN);
		points[0].x = startx + 1 + offset;
		points[0].y = starty + (offset ? 1 : 0);
		points[1].x = startx + xWidth/2 + offset;
		points[1].y = starty + tHeight + (offset ? 0 : -1);
		points[2].x = startx - 1 + xWidth + offset;
		points[2].y = points[0].y;
		XFillPolygon(entryPtr->display, pixmap, entryPtr->textGC,
			points, 3, Convex, CoordModeOrigin);
	    }
	}
    }

    /*
     * Draw the border and focus highlight last, so they will overwrite any
     * text that extends past the viewable part of the window.
     */

    Tk_GetPixelsFromObj(NULL, entryPtr->tkwin, entryPtr->borderWidthObj, &borderWidth);
    Tk_GetPixelsFromObj(NULL, entryPtr->tkwin, entryPtr->highlightWidthObj, &highlightWidth);
    if (!TkpDrawEntryBorderAndFocus(entryPtr, pixmap,
	    (entryPtr->type == TK_SPINBOX))) {
	xBound = highlightWidth;
	if (entryPtr->relief != TK_RELIEF_FLAT) {
	    Tk_Draw3DRectangle(tkwin, pixmap, border, xBound, xBound,
		    Tk_Width(tkwin) - 2 * xBound,
		    Tk_Height(tkwin) - 2 * xBound,
		    borderWidth, entryPtr->relief);
	}
	if (xBound > 0) {
	    GC fgGC, bgGC;

	    bgGC = Tk_GCForColor(entryPtr->highlightBgColorPtr, pixmap);
	    if (entryPtr->flags & GOT_FOCUS) {
		fgGC = Tk_GCForColor(entryPtr->highlightColorPtr, pixmap);
		Tk_DrawHighlightBorder(tkwin, fgGC, bgGC, xBound, pixmap);
	    } else {
		Tk_DrawHighlightBorder(tkwin, bgGC, bgGC, xBound, pixmap);
	    }
	}
    }

#ifndef TK_NO_DOUBLE_BUFFERING
    /*
     * Everything's been redisplayed; now copy the pixmap onto the screen and
     * free up the pixmap.
     */

    XCopyArea(entryPtr->display, pixmap, Tk_WindowId(tkwin), entryPtr->textGC,
	    0, 0, (unsigned) Tk_Width(tkwin), (unsigned) Tk_Height(tkwin),
	    0, 0);
    Tk_FreePixmap(entryPtr->display, pixmap);
#endif /* TK_NO_DOUBLE_BUFFERING */
    entryPtr->flags &= ~BORDER_NEEDED;
}

/*
 *----------------------------------------------------------------------
 *
 * EntryComputeGeometry --
 *
 *	This function is invoked to recompute information about where in its
 *	window an entry's string will be displayed. It also computes the
 *	requested size for the window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The leftX and tabOrigin fields are recomputed for entryPtr, and
 *	leftIndex may be adjusted. Tk_GeometryRequest is called to register
 *	the desired dimensions for the window.
 *
 *----------------------------------------------------------------------
 */

static void
EntryComputeGeometry(
    Entry *entryPtr)		/* Widget record for entry. */
{
    int totalLength, overflow, rightX;
    Tcl_Size maxOffScreen;
    int height, width, i;
    Tk_FontMetrics fm;
    char *p;

    if (entryPtr->displayString != entryPtr->string) {
	ckfree((void *)entryPtr->displayString);
	entryPtr->displayString = entryPtr->string;
	entryPtr->numDisplayBytes = entryPtr->numBytes;
    }

    /*
     * If we're displaying a special character instead of the value of the
     * entry, recompute the displayString.
     */

    if (entryPtr->showCharObj != NULL) {
	int ch;
	char buf[6];
	int size;

	/*
	 * Normalize the special character so we can safely duplicate it in
	 * the display string. If we didn't do this, then two malformed
	 * characters might end up looking like one valid UTF character in the
	 * resulting string.
	 */

	Tcl_UtfToUniChar(Tcl_GetString(entryPtr->showCharObj), &ch);
	size = Tcl_UniCharToUtf(ch, buf);

	entryPtr->numDisplayBytes = entryPtr->numChars * size;
	p = (char *)ckalloc(entryPtr->numDisplayBytes + 1);
	entryPtr->displayString = p;

	for (i = entryPtr->numChars; i-- > 0; ) {
	    memcpy(p, buf, size);
	    p += size;
	}
	*p = '\0';
    }

    /* Recompute layout of placeholder text.
     * Only the placeholderX and placeholderLeftIndex value is needed.
     * We use the same font so we can use the layoutY value from below.
     */

    Tk_FreeTextLayout(entryPtr->placeholderLayout);
    if (entryPtr->placeholderObj) {
	entryPtr->placeholderChars = strlen(Tcl_GetString(entryPtr->placeholderObj));
	entryPtr->placeholderLayout = Tk_ComputeTextLayout(entryPtr->tkfont,
		Tcl_GetString(entryPtr->placeholderObj), entryPtr->placeholderChars, 0,
		entryPtr->justify, TK_IGNORE_NEWLINES, &totalLength, NULL);
	overflow = totalLength -
		(Tk_Width(entryPtr->tkwin) - 2*entryPtr->inset - entryPtr->xWidth);
	if (overflow <= 0) {
	    entryPtr->placeholderLeftIndex = 0;
	    if (entryPtr->justify == TK_JUSTIFY_LEFT) {
		entryPtr->placeholderX = entryPtr->inset;
	    } else if (entryPtr->justify == TK_JUSTIFY_RIGHT) {
		entryPtr->placeholderX = Tk_Width(entryPtr->tkwin) - entryPtr->inset
			- entryPtr->xWidth - totalLength;
	    } else {
		entryPtr->placeholderX = (Tk_Width(entryPtr->tkwin)
			- entryPtr->xWidth - totalLength)/2;
	    }
	} else {

	    /*
	     * The whole string can't fit in the window. Compute the maximum
	     * number of characters that may be off-screen to the left without
	     * leaving empty space on the right of the window, then don't let
	     * placeholderLeftIndex be any greater than that.
	     */

	    maxOffScreen = Tk_PointToChar(entryPtr->placeholderLayout, overflow, 0);
	    Tk_CharBbox(entryPtr->placeholderLayout, maxOffScreen,
		&rightX, NULL, NULL, NULL);
	    if (rightX < overflow) {
		maxOffScreen++;
	    }
	    entryPtr->placeholderLeftIndex = maxOffScreen;
	    Tk_CharBbox(entryPtr->placeholderLayout, entryPtr->placeholderLeftIndex, &rightX,
		NULL, NULL, NULL);
	    entryPtr->placeholderX = entryPtr->inset -rightX;
	}
    } else {
	entryPtr->placeholderChars = 0;
	entryPtr->placeholderLayout = Tk_ComputeTextLayout(entryPtr->tkfont,
		(entryPtr->placeholderObj ? Tcl_GetString(entryPtr->placeholderObj) : NULL), 0, 0,
		entryPtr->justify, TK_IGNORE_NEWLINES, NULL, NULL);
	entryPtr->placeholderX = entryPtr->inset;
    }

    Tk_FreeTextLayout(entryPtr->textLayout);
    entryPtr->textLayout = Tk_ComputeTextLayout(entryPtr->tkfont,
	    entryPtr->displayString, entryPtr->numChars, 0,
	    entryPtr->justify, TK_IGNORE_NEWLINES, &totalLength, &height);

    entryPtr->layoutY = (Tk_Height(entryPtr->tkwin) - height) / 2;

    /*
     * Recompute where the leftmost character on the display will be drawn
     * (entryPtr->leftX) and adjust leftIndex if necessary so that we don't
     * let characters hang off the edge of the window unless the entire window
     * is full.
     */

    overflow = totalLength -
	    (Tk_Width(entryPtr->tkwin) - 2*entryPtr->inset - entryPtr->xWidth);
    if (overflow <= 0) {
	entryPtr->leftIndex = 0;
	if (entryPtr->justify == TK_JUSTIFY_LEFT) {
	    entryPtr->leftX = entryPtr->inset;
	} else if (entryPtr->justify == TK_JUSTIFY_RIGHT) {
	    entryPtr->leftX = Tk_Width(entryPtr->tkwin) - entryPtr->inset
		    - entryPtr->xWidth - totalLength;
	} else {
	    entryPtr->leftX = (Tk_Width(entryPtr->tkwin)
		    - entryPtr->xWidth - totalLength)/2;
	}
	entryPtr->layoutX = entryPtr->leftX;
    } else {
	/*
	 * The whole string can't fit in the window. Compute the maximum
	 * number of characters that may be off-screen to the left without
	 * leaving empty space on the right of the window, then don't let
	 * leftIndex be any greater than that.
	 */

	maxOffScreen = Tk_PointToChar(entryPtr->textLayout, overflow, 0);
	Tk_CharBbox(entryPtr->textLayout, maxOffScreen,
		&rightX, NULL, NULL, NULL);
	if (rightX < overflow) {
	    maxOffScreen++;
	}
	if (entryPtr->leftIndex > maxOffScreen) {
	    entryPtr->leftIndex = maxOffScreen;
	}
	Tk_CharBbox(entryPtr->textLayout, entryPtr->leftIndex, &rightX,
		NULL, NULL, NULL);
	entryPtr->leftX = entryPtr->inset;
	entryPtr->layoutX = entryPtr->leftX - rightX;
    }

    Tk_GetFontMetrics(entryPtr->tkfont, &fm);
    height = fm.linespace + 2*entryPtr->inset + 2*(YPAD-XPAD);
    if (entryPtr->prefWidth > 0) {
	width = entryPtr->prefWidth*entryPtr->avgWidth + 2*entryPtr->inset;
    } else if (totalLength == 0) {
	width = entryPtr->avgWidth + 2*entryPtr->inset;
    } else {
	width = totalLength + 2*entryPtr->inset;
    }

    /*
     * Add one extra length for the spin buttons
     */
    width += entryPtr->xWidth;

    Tk_GeometryRequest(entryPtr->tkwin, width, height);
}

/*
 *----------------------------------------------------------------------
 *
 * InsertChars --
 *
 *	Add new characters to an entry widget.
 *
 * Results:
 *	A standard Tcl result. If an error occurred then an error message is
 *	left in the interp's result.
 *
 * Side effects:
 *	New information gets added to entryPtr; it will be redisplayed soon,
 *	but not necessarily immediately.
 *
 *----------------------------------------------------------------------
 */

static int
InsertChars(
    Entry *entryPtr,		/* Entry that is to get the new elements. */
    Tcl_Size index,			/* Add the new elements before this character
				 * index. */
    Tcl_Obj *obj)		/* New characters to add. */
{
    size_t byteIndex, byteCount, newByteCount, oldChars, charsAdded;
    const char *string;
    const char *value = Tcl_GetString(obj);
    char *newStr;

    string = entryPtr->string;
    byteIndex = Tcl_UtfAtIndex(string, index) - string;
    byteCount = strlen(value);
    if (byteCount == 0) {
	return TCL_OK;
    }

    newByteCount = entryPtr->numBytes + byteCount + 1;
    newStr = (char *)ckalloc(newByteCount);
    memcpy(newStr, string, byteIndex);
    strcpy(newStr + byteIndex, value);
    strcpy(newStr + byteIndex + byteCount, string + byteIndex);

    if ((entryPtr->validate == VALIDATE_KEY ||
	    entryPtr->validate == VALIDATE_ALL) &&
	    EntryValidateChange(entryPtr, value, newStr, index,
		    VALIDATE_INSERT) != TCL_OK) {
	ckfree(newStr);
	return TCL_OK;
    }

    ckfree((void *)string);
    entryPtr->string = newStr;

    /*
     * The following construction is used because inserting improperly formed
     * UTF-8 sequences between other improperly formed UTF-8 sequences could
     * result in actually forming valid UTF-8 sequences; the number of
     * characters added may not be Tcl_NumUtfChars(string, TCL_INDEX_NONE), because of
     * context. The actual number of characters added is how many characters
     * are in the string now minus the number that used to be there.
     */

    oldChars = entryPtr->numChars;
    entryPtr->numChars = Tcl_NumUtfChars(newStr, TCL_INDEX_NONE);
    charsAdded = entryPtr->numChars - oldChars;
    entryPtr->numBytes += byteCount;

    if (entryPtr->displayString == string) {
	entryPtr->displayString = newStr;
	entryPtr->numDisplayBytes = entryPtr->numBytes;
    }

    /*
     * Inserting characters invalidates all indexes into the string. Touch up
     * the indexes so that they still refer to the same characters (at new
     * positions). When updating the selection end-points, don't include the
     * new text in the selection unless it was completely surrounded by the
     * selection.
     */

    if (entryPtr->selectFirst >= index) {
	entryPtr->selectFirst += charsAdded;
    }
    if (entryPtr->selectLast > index) {
	entryPtr->selectLast += charsAdded;
    }
    if ((entryPtr->selectAnchor > index) || (entryPtr->selectFirst >= index)) {
	entryPtr->selectAnchor += charsAdded;
    }
    if (entryPtr->leftIndex > index) {
	entryPtr->leftIndex += charsAdded;
    }
    if (entryPtr->insertPos >= index) {
	entryPtr->insertPos += charsAdded;
    }
    return EntryValueChanged(entryPtr, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * DeleteChars --
 *
 *	Remove one or more characters from an entry widget.
 *
 * Results:
 *	A standard Tcl result. If an error occurred then an error message is
 *	left in the interp's result.
 *
 * Side effects:
 *	Memory gets freed, the entry gets modified and (eventually)
 *	redisplayed.
 *
 *----------------------------------------------------------------------
 */

static int
DeleteChars(
    Entry *entryPtr,		/* Entry widget to modify. */
    Tcl_Size index,			/* Index of first character to delete. */
    Tcl_Size count)			/* How many characters to delete. */
{
    int byteIndex, byteCount, newByteCount;
    const char *string;
    char *newStr, *toDelete;

    if (index + count > entryPtr->numChars) {
	count = entryPtr->numChars - index;
    }
    if ((int)count <= 0) {
	return TCL_OK;
    }

    string = entryPtr->string;
    byteIndex = Tcl_UtfAtIndex(string, index) - string;
    byteCount = Tcl_UtfAtIndex(string + byteIndex, count) - (string+byteIndex);

    newByteCount = entryPtr->numBytes + 1 - byteCount;
    newStr = (char *)ckalloc(newByteCount);
    memcpy(newStr, string, (size_t) byteIndex);
    strcpy(newStr + byteIndex, string + byteIndex + byteCount);

    toDelete = (char *)ckalloc(byteCount + 1);
    memcpy(toDelete, string + byteIndex, (size_t) byteCount);
    toDelete[byteCount] = '\0';

    if ((entryPtr->validate == VALIDATE_KEY ||
	    entryPtr->validate == VALIDATE_ALL) &&
	    EntryValidateChange(entryPtr, toDelete, newStr, index,
		    VALIDATE_DELETE) != TCL_OK) {
	ckfree(newStr);
	ckfree(toDelete);
	return TCL_OK;
    }

    ckfree(toDelete);
    ckfree((void *)entryPtr->string);
    entryPtr->string = newStr;
    entryPtr->numChars -= count;
    entryPtr->numBytes -= byteCount;

    if (entryPtr->displayString == string) {
	entryPtr->displayString = newStr;
	entryPtr->numDisplayBytes = entryPtr->numBytes;
    }

    /*
     * Deleting characters results in the remaining characters being
     * renumbered. Update the various indexes into the string to reflect this
     * change.
     */

    if (entryPtr->selectFirst >= index) {
	if (entryPtr->selectFirst >= index + count) {
	    entryPtr->selectFirst -= count;
	} else {
	    entryPtr->selectFirst = index;
	}
    }
    if (entryPtr->selectLast >= index) {
	if (entryPtr->selectLast >= index + count) {
	    entryPtr->selectLast -= count;
	} else {
	    entryPtr->selectLast = index;
	}
    }
    if (entryPtr->selectLast <= entryPtr->selectFirst) {
	entryPtr->selectFirst = TCL_INDEX_NONE;
	entryPtr->selectLast = TCL_INDEX_NONE;
    }
    if (entryPtr->selectAnchor >= index) {
	if (entryPtr->selectAnchor >= index + count) {
	    entryPtr->selectAnchor -= count;
	} else {
	    entryPtr->selectAnchor = index;
	}
    }
    if (entryPtr->leftIndex > index) {
	if (entryPtr->leftIndex >= index + count) {
	    entryPtr->leftIndex -= count;
	} else {
	    entryPtr->leftIndex = index;
	}
    }
    if (entryPtr->insertPos >= index) {
	if (entryPtr->insertPos >= index + count) {
	    entryPtr->insertPos -= count;
	} else {
	    entryPtr->insertPos = index;
	}
    }
    return EntryValueChanged(entryPtr, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * EntryValueChanged --
 *
 *	This function is invoked when characters are inserted into an entry or
 *	deleted from it. It updates the entry's associated variable, if there
 *	is one, and does other bookkeeping such as arranging for redisplay.
 *
 * Results:
 *	A standard Tcl result. If an error occurred then an error message is
 *	left in the interp's result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
EntryValueChanged(
    Entry *entryPtr,		/* Entry whose value just changed. */
    const char *newValue)	/* If this value is not NULL, we first force
				 * the value of the entry to this. */
{
    if (newValue != NULL) {
	EntrySetValue(entryPtr, newValue);
    }

    if (entryPtr->textVarNameObj == NULL) {
	newValue = NULL;
    } else {
	newValue = Tcl_SetVar2(entryPtr->interp, Tcl_GetString(entryPtr->textVarNameObj),
		NULL, entryPtr->string, TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
    }

    if ((newValue != NULL) && (strcmp(newValue, entryPtr->string) != 0)) {
	/*
	 * The value of the variable is different than what we asked for.
	 * This means that a trace on the variable modified it. In this case
	 * our trace function wasn't invoked since the modification came while
	 * a trace was already active on the variable. So, update our value to
	 * reflect the variable's latest value.
	 */

	EntrySetValue(entryPtr, newValue);
    } else {
	/*
	 * Arrange for redisplay.
	 */

	entryPtr->flags |= UPDATE_SCROLLBAR;
	EntryComputeGeometry(entryPtr);
	EventuallyRedraw(entryPtr);
    }

    /*
     * An error may have happened when setting the textvariable in case there
     * is a trace on that variable and the trace proc triggered an error.
     * Another possibility is that the textvariable is in a namespace that
     * does not (yet) exist.
     * Signal this error.
     */

    if ((entryPtr->textVarNameObj != NULL) && (newValue == NULL)) {
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * EntrySetValue --
 *
 *	Replace the contents of a text entry with a given value. This function
 *	is invoked when updating the entry from the entry's associated
 *	variable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The string displayed in the entry will change. The selection,
 *	insertion point, and view may have to be adjusted to keep them within
 *	the bounds of the new string. Note: this function does *not* update
 *	the entry's associated variable, since that could result in an
 *	infinite loop.
 *
 *----------------------------------------------------------------------
 */

static void
EntrySetValue(
    Entry *entryPtr,		/* Entry whose value is to be changed. */
    const char *value)		/* New text to display in entry. */
{
    const char *oldSource;
    int valueLen, malloced = 0;

    if (strcmp(value, entryPtr->string) == 0) {
	return;
    }
    valueLen = strlen(value);

    if (entryPtr->flags & VALIDATE_VAR) {
	entryPtr->flags |= VALIDATE_ABORT;
    } else {
	/*
	 * If we validate, we create a copy of the value, as it may point to
	 * volatile memory, like the value of the -textvar which may get freed
	 * during validation
	 */

	char *tmp = (char *)ckalloc(valueLen + 1);

	strcpy(tmp, value);
	value = tmp;
	malloced = 1;

	entryPtr->flags |= VALIDATE_VAR;
	(void) EntryValidateChange(entryPtr, NULL, value, TCL_INDEX_NONE,
		VALIDATE_FORCED);
	entryPtr->flags &= ~VALIDATE_VAR;

	/*
	 * If VALIDATE_ABORT has been set, then this operation should be
	 * aborted because the validatecommand did something else instead
	 */

	if (entryPtr->flags & VALIDATE_ABORT) {
	    entryPtr->flags &= ~VALIDATE_ABORT;
	    ckfree((void *)value);
	    return;
	}
    }

    oldSource = entryPtr->string;
    ckfree((void *)entryPtr->string);

    if (malloced) {
	entryPtr->string = value;
    } else {
	char *tmp = (char *)ckalloc(valueLen + 1);

	strcpy(tmp, value);
	entryPtr->string = tmp;
    }
    entryPtr->numBytes = valueLen;
    entryPtr->numChars = Tcl_NumUtfChars(value, valueLen);

    if (entryPtr->displayString == oldSource) {
	entryPtr->displayString = entryPtr->string;
	entryPtr->numDisplayBytes = entryPtr->numBytes;
    }

    if (entryPtr->selectFirst != TCL_INDEX_NONE) {
	if (entryPtr->selectFirst >= entryPtr->numChars) {
	    entryPtr->selectFirst = TCL_INDEX_NONE;
	    entryPtr->selectLast = TCL_INDEX_NONE;
	} else if (entryPtr->selectLast > entryPtr->numChars) {
	    entryPtr->selectLast = entryPtr->numChars;
	}
    }
    if (entryPtr->leftIndex >= entryPtr->numChars) {
	if (entryPtr->numChars > 0) {
	    entryPtr->leftIndex = entryPtr->numChars - 1;
	} else {
	    entryPtr->leftIndex = 0;
	}
    }
    if (entryPtr->insertPos > entryPtr->numChars) {
	entryPtr->insertPos = entryPtr->numChars;
    }

    entryPtr->flags |= UPDATE_SCROLLBAR;
    EntryComputeGeometry(entryPtr);
    EventuallyRedraw(entryPtr);
}

/*
 *--------------------------------------------------------------
 *
 * EntryEventProc --
 *
 *	This function is invoked by the Tk dispatcher for various events on
 *	entries.
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
EntryEventProc(
    void *clientData,	/* Information about window. */
    XEvent *eventPtr)		/* Information about event. */
{
    Entry *entryPtr = (Entry *)clientData;

    if ((entryPtr->type == TK_SPINBOX) && (eventPtr->type == MotionNotify)) {
	Spinbox *sbPtr = (Spinbox *)clientData;
	int elem;

	elem = GetSpinboxElement(sbPtr, eventPtr->xmotion.x,
		eventPtr->xmotion.y);
	if (elem != sbPtr->curElement) {
	    Tk_Cursor cursor;

	    sbPtr->curElement = elem;
	    if (elem == SEL_ENTRY) {
		cursor = entryPtr->cursor;
	    } else if ((elem == SEL_BUTTONDOWN) || (elem == SEL_BUTTONUP)) {
		cursor = sbPtr->bCursor;
	    } else {
		cursor = NULL;
	    }
	    if (cursor != NULL) {
		Tk_DefineCursor(entryPtr->tkwin, cursor);
	    } else {
		Tk_UndefineCursor(entryPtr->tkwin);
	    }
	}
	return;
    }

    switch (eventPtr->type) {
    case Expose:
	EventuallyRedraw(entryPtr);
	entryPtr->flags |= BORDER_NEEDED;
	break;
    case DestroyNotify:
	if (!(entryPtr->flags & ENTRY_DELETED)) {
	    entryPtr->flags |= (ENTRY_DELETED | VALIDATE_ABORT);
	    Tcl_DeleteCommandFromToken(entryPtr->interp, entryPtr->widgetCmd);
	    if (entryPtr->flags & REDRAW_PENDING) {
		Tcl_CancelIdleCall(DisplayEntry, clientData);
	    }
	    Tcl_EventuallyFree(clientData, DestroyEntry);
	}
	break;
    case ConfigureNotify:
	Tcl_Preserve(entryPtr);
	entryPtr->flags |= UPDATE_SCROLLBAR;
	EntryComputeGeometry(entryPtr);
	EventuallyRedraw(entryPtr);
	Tcl_Release(entryPtr);
	break;
    case FocusIn:
    case FocusOut:
	if (eventPtr->xfocus.detail != NotifyInferior) {
	    EntryFocusProc(entryPtr, (eventPtr->type == FocusIn));
	}
	break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * EntryCmdDeletedProc --
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
EntryCmdDeletedProc(
    void *clientData)	/* Pointer to widget record for widget. */
{
    Entry *entryPtr = (Entry *)clientData;

    /*
     * This function could be invoked either because the window was destroyed
     * and the command was then deleted (in which case tkwin is NULL) or
     * because the command was deleted, and then this function destroys the
     * widget.
     */

    if (!(entryPtr->flags & ENTRY_DELETED)) {
	Tk_DestroyWindow(entryPtr->tkwin);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * GetEntryIndex --
 *
 *	Parse an index into an entry and return either its value or an error.
 *
 * Results:
 *	A standard Tcl result. If all went well, then *indexPtr is filled in
 *	with the character index (into entryPtr) corresponding to string. The
 *	index value is guaranteed to lie between 0 and the number of
 *	characters in the string, inclusive. If an error occurs then an error
 *	message is left in the interp's result.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static int
GetEntryIndex(
    Tcl_Interp *interp,		/* For error messages. */
    Entry *entryPtr,		/* Entry for which the index is being
				 * specified. */
    Tcl_Obj *indexObj,	/* Specifies character in entryPtr. */
    Tcl_Size *indexPtr)		/* Where to store converted character index */
{
    Tcl_Size length, idx;
    const char *string;

    if (TCL_OK == TkGetIntForIndex(indexObj, entryPtr->numChars - 1, 1, &idx)) {
	if (idx < 0) {
	    idx = 0;
	} else if (idx > entryPtr->numChars) {
	    idx = entryPtr->numChars;
	}
	*indexPtr = idx;
	return TCL_OK;
    }

    string = Tcl_GetStringFromObj(indexObj, &length);

    switch (string[0]) {
    case 'a':
	if (strncmp(string, "anchor", length) != 0) {
	    goto badIndex;
	}
	*indexPtr = entryPtr->selectAnchor;
	break;
    case 'i':
	if (strncmp(string, "insert", length) != 0) {
	    goto badIndex;
	}
	*indexPtr = entryPtr->insertPos;
	break;
    case 's':
	if (entryPtr->selectFirst < 0) {
	    Tcl_ResetResult(interp);
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "selection isn't in widget %s",
		    Tk_PathName(entryPtr->tkwin)));
	    Tcl_SetErrorCode(interp, "TK",
		    (entryPtr->type == TK_ENTRY) ? "ENTRY" : "SPINBOX",
		    "NO_SELECTION", NULL);
	    return TCL_ERROR;
	}
	if (length < 5) {
	    goto badIndex;
	}
	if (strncmp(string, "sel.first", length) == 0) {
	    *indexPtr = entryPtr->selectFirst;
	} else if (strncmp(string, "sel.last", length) == 0) {
	    *indexPtr = entryPtr->selectLast;
	} else {
	    goto badIndex;
	}
	break;
    case '@': {
	int x, roundUp, maxWidth;

	if (Tcl_GetInt(NULL, string + 1, &x) != TCL_OK) {
	    goto badIndex;
	}
	if (x < entryPtr->inset) {
	    x = entryPtr->inset;
	}
	roundUp = 0;
	maxWidth = Tk_Width(entryPtr->tkwin) - entryPtr->inset
		- entryPtr->xWidth - 1;
	if (x > maxWidth) {
	    x = maxWidth;
	    roundUp = 1;
	}
	*indexPtr = Tk_PointToChar(entryPtr->textLayout,
		x - entryPtr->layoutX, 0);

	/*
	 * Special trick: if the x-position was off-screen to the right, round
	 * the index up to refer to the character just after the last visible
	 * one on the screen. This is needed to enable the last character to
	 * be selected, for example.
	 */

	if (roundUp && (*indexPtr < entryPtr->numChars)) {
	    *indexPtr += 1;
	}
	break;
    }
    default:
	  badIndex:
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad %s index \"%s\"",
		    (entryPtr->type == TK_ENTRY) ? "entry" : "spinbox", string));
	    Tcl_SetErrorCode(interp, "TK",
		    (entryPtr->type == TK_ENTRY) ? "ENTRY" : "SPINBOX",
		    "BAD_INDEX", NULL);
	    return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * EntryScanTo --
 *
 *	Given a y-coordinate (presumably of the curent mouse location) drag
 *	the view in the window to implement the scan operation.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The view in the window may change.
 *
 *----------------------------------------------------------------------
 */

static void
EntryScanTo(
    Entry *entryPtr,		/* Information about widget. */
    int x)			/* X-coordinate to use for scan operation. */
{
    Tcl_Size newLeftIndex;

    /*
     * Compute new leftIndex for entry by amplifying the difference between
     * the current position and the place where the scan started (the "mark"
     * position). If we run off the left or right side of the entry, then
     * reset the mark point so that the current position continues to
     * correspond to the edge of the window. This means that the picture will
     * start dragging as soon as the mouse reverses direction (without this
     * reset, might have to slide mouse a long ways back before the picture
     * starts moving again).
     */

    newLeftIndex = entryPtr->scanMarkIndex
	    - (10 * (x - entryPtr->scanMarkX)) / entryPtr->avgWidth;
    if (newLeftIndex >= entryPtr->numChars) {
	newLeftIndex = entryPtr->scanMarkIndex = entryPtr->numChars - 1;
	entryPtr->scanMarkX = x;
    }
    if (newLeftIndex < 0) {
	newLeftIndex = entryPtr->scanMarkIndex = 0;
	entryPtr->scanMarkX = x;
    }

    if (newLeftIndex != entryPtr->leftIndex) {
	entryPtr->leftIndex = newLeftIndex;
	entryPtr->flags |= UPDATE_SCROLLBAR;
	EntryComputeGeometry(entryPtr);
	if (newLeftIndex != entryPtr->leftIndex) {
	    entryPtr->scanMarkIndex = entryPtr->leftIndex;
	    entryPtr->scanMarkX = x;
	}
	EventuallyRedraw(entryPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * EntrySelectTo --
 *
 *	Modify the selection by moving its un-anchored end. This could make
 *	the selection either larger or smaller.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The selection changes.
 *
 *----------------------------------------------------------------------
 */

static void
EntrySelectTo(
    Entry *entryPtr,		/* Information about widget. */
    Tcl_Size index)			/* Character index of element that is to
				 * become the "other" end of the selection. */
{
    Tcl_Size newFirst, newLast;

    /*
     * Grab the selection if we don't own it already.
     */

    if (!(entryPtr->flags & GOT_SELECTION) && (entryPtr->exportSelection)
	    && (!Tcl_IsSafe(entryPtr->interp))) {
	Tk_OwnSelection(entryPtr->tkwin, XA_PRIMARY, EntryLostSelection,
		entryPtr);
	entryPtr->flags |= GOT_SELECTION;
    }

    /*
     * Pick new starting and ending points for the selection.
     */

    if (entryPtr->selectAnchor > entryPtr->numChars) {
	entryPtr->selectAnchor = entryPtr->numChars;
    }
    if (entryPtr->selectAnchor <= index) {
	newFirst = entryPtr->selectAnchor;
	newLast = index;
    } else {
	newFirst = index;
	newLast = entryPtr->selectAnchor;
	if (newLast < 0) {
	    newFirst = newLast = TCL_INDEX_NONE;
	}
    }
    if ((entryPtr->selectFirst == newFirst)
	    && (entryPtr->selectLast == newLast)) {
	return;
    }
    entryPtr->selectFirst = newFirst;
    entryPtr->selectLast = newLast;
    EventuallyRedraw(entryPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * EntryFetchSelection --
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
EntryFetchSelection(
    void *clientData,	/* Information about entry widget. */
    Tcl_Size offset,			/* Byte offset within selection of first
				 * character to be returned. */
    char *buffer,		/* Location in which to place selection. */
    Tcl_Size maxBytes)		/* Maximum number of bytes to place at buffer,
				 * not including terminating NUL character. */
{
    Entry *entryPtr = (Entry *)clientData;
    Tcl_Size byteCount;
    const char *string;
    const char *selStart, *selEnd;

    if ((entryPtr->selectFirst < 0) || (!entryPtr->exportSelection)
	    || Tcl_IsSafe(entryPtr->interp)) {
	return -1;
    }
    string = entryPtr->displayString;
    selStart = Tcl_UtfAtIndex(string, entryPtr->selectFirst);
    selEnd = Tcl_UtfAtIndex(selStart,
	    entryPtr->selectLast - entryPtr->selectFirst);
    if (selEnd <= selStart + offset) {
	return 0;
    }
    byteCount = selEnd - selStart - offset;
    if (byteCount > maxBytes) {
	byteCount = maxBytes;
    }
    memcpy(buffer, selStart + offset, byteCount);
    buffer[byteCount] = '\0';
    return byteCount;
}

/*
 *----------------------------------------------------------------------
 *
 * EntryLostSelection --
 *
 *	This function is called back by Tk when the selection is grabbed away
 *	from an entry widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The existing selection is unhighlighted, and the window is marked as
 *	not containing a selection.
 *
 *----------------------------------------------------------------------
 */

static void
EntryLostSelection(
    void *clientData)	/* Information about entry widget. */
{
    Entry *entryPtr = (Entry *)clientData;

    entryPtr->flags &= ~GOT_SELECTION;

    /*
     * On Windows and Mac systems, we want to remember the selection for the
     * next time the focus enters the window. On Unix, we need to clear the
     * selection since it is always visible.
     * This is controlled by ::tk::AlwaysShowSelection.
     */

    if (Tk_AlwaysShowSelection(entryPtr->tkwin)
	    && (entryPtr->selectFirst != TCL_INDEX_NONE) && entryPtr->exportSelection
	    && (!Tcl_IsSafe(entryPtr->interp))) {
	entryPtr->selectFirst = TCL_INDEX_NONE;
	entryPtr->selectLast = TCL_INDEX_NONE;
	EventuallyRedraw(entryPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * EventuallyRedraw --
 *
 *	Ensure that an entry is eventually redrawn on the display.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information gets redisplayed. Right now we don't do selective
 *	redisplays: the whole window will be redrawn. This doesn't seem to
 *	hurt performance noticeably, but if it does then this could be
 *	changed.
 *
 *----------------------------------------------------------------------
 */

static void
EventuallyRedraw(
    Entry *entryPtr)		/* Information about widget. */
{
    if ((entryPtr->flags & ENTRY_DELETED) || !Tk_IsMapped(entryPtr->tkwin)) {
	return;
    }

    /*
     * Right now we don't do selective redisplays: the whole window will be
     * redrawn. This doesn't seem to hurt performance noticeably, but if it
     * does then this could be changed.
     */

    if (!(entryPtr->flags & REDRAW_PENDING)) {
	entryPtr->flags |= REDRAW_PENDING;
	Tcl_DoWhenIdle(DisplayEntry, entryPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * EntryVisibleRange --
 *
 *	Return information about the range of the entry that is currently
 *	visible.
 *
 * Results:
 *	*firstPtr and *lastPtr are modified to hold fractions between 0 and 1
 *	identifying the range of characters visible in the entry.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
EntryVisibleRange(
    Entry *entryPtr,		/* Information about widget. */
    double *firstPtr,		/* Return position of first visible character
				 * in widget. */
    double *lastPtr)		/* Return position of char just after last
				 * visible one. */
{
    int charsInWindow;

    if (entryPtr->numChars == 0) {
	*firstPtr = 0.0;
	*lastPtr = 1.0;
    } else {
	charsInWindow = Tk_PointToChar(entryPtr->textLayout,
		Tk_Width(entryPtr->tkwin) - entryPtr->inset
		- entryPtr->xWidth - entryPtr->layoutX - 1, 0);
	if (charsInWindow < (int)entryPtr->numChars) {
	    charsInWindow++;
	}
	charsInWindow -= entryPtr->leftIndex;
	if (charsInWindow == 0) {
	    charsInWindow = 1;
	}

	*firstPtr = (double) entryPtr->leftIndex / entryPtr->numChars;
	*lastPtr = (double) (entryPtr->leftIndex + charsInWindow)
		/ entryPtr->numChars;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * EntryUpdateScrollbar --
 *
 *	This function is invoked whenever information has changed in an entry
 *	in a way that would invalidate a scrollbar display. If there is an
 *	associated scrollbar, then this function updates it by invoking a Tcl
 *	command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A Tcl command is invoked, and an additional command may be
 *	invoked to process errors in the command.
 *
 *----------------------------------------------------------------------
 */

static void
EntryUpdateScrollbar(
    Entry *entryPtr)			/* Information about widget. */
{
    char firstStr[TCL_DOUBLE_SPACE], lastStr[TCL_DOUBLE_SPACE];
    int code;
    double first, last;
    Tcl_Interp *interp;
    Tcl_DString buf;

    if (entryPtr->scrollCmdObj == NULL) {
	return;
    }

    interp = entryPtr->interp;
    Tcl_Preserve(interp);
    EntryVisibleRange(entryPtr, &first, &last);
    Tcl_PrintDouble(NULL, first, firstStr);
    Tcl_PrintDouble(NULL, last, lastStr);
    Tcl_DStringInit(&buf);
    Tcl_DStringAppend(&buf, Tcl_GetString(entryPtr->scrollCmdObj), TCL_INDEX_NONE);
    Tcl_DStringAppend(&buf, " ", TCL_INDEX_NONE);
    Tcl_DStringAppend(&buf, firstStr, TCL_INDEX_NONE);
    Tcl_DStringAppend(&buf, " ", TCL_INDEX_NONE);
    Tcl_DStringAppend(&buf, lastStr, TCL_INDEX_NONE);
    code = Tcl_EvalEx(interp, Tcl_DStringValue(&buf), TCL_INDEX_NONE, TCL_EVAL_GLOBAL);
    Tcl_DStringFree(&buf);
    if (code != TCL_OK) {
	Tcl_AppendObjToErrorInfo(interp, Tcl_ObjPrintf(
		"\n    (horizontal scrolling command executed by %s)",
		Tk_PathName(entryPtr->tkwin)));
	Tcl_BackgroundException(interp, code);
    }
    Tcl_ResetResult(interp);
    Tcl_Release(interp);
}

/*
 *----------------------------------------------------------------------
 *
 * EntryBlinkProc --
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
EntryBlinkProc(
    void *clientData)	/* Pointer to record describing entry. */
{
    Entry *entryPtr = (Entry *)clientData;

    if ((entryPtr->state == STATE_DISABLED) ||
	    (entryPtr->state == STATE_READONLY) ||
	    !(entryPtr->flags & GOT_FOCUS) || (entryPtr->insertOffTime == 0)) {
	return;
    }
    if (entryPtr->flags & CURSOR_ON) {
	entryPtr->flags &= ~CURSOR_ON;
	entryPtr->insertBlinkHandler = Tcl_CreateTimerHandler(
		entryPtr->insertOffTime, EntryBlinkProc, entryPtr);
    } else {
	entryPtr->flags |= CURSOR_ON;
	entryPtr->insertBlinkHandler = Tcl_CreateTimerHandler(
		entryPtr->insertOnTime, EntryBlinkProc, entryPtr);
    }
    EventuallyRedraw(entryPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * EntryFocusProc --
 *
 *	This function is called whenever the entry gets or loses the input
 *	focus. It's also called whenever the window is reconfigured while it
 *	has the focus.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The cursor gets turned on or off.
 *
 *----------------------------------------------------------------------
 */

static void
EntryFocusProc(
    Entry *entryPtr,		/* Entry that got or lost focus. */
    int gotFocus)		/* 1 means window is getting focus, 0 means
				 * it's losing it. */
{
    Tcl_DeleteTimerHandler(entryPtr->insertBlinkHandler);
    if (gotFocus) {
	entryPtr->flags |= GOT_FOCUS | CURSOR_ON;
	if (entryPtr->insertOffTime != 0) {
	    entryPtr->insertBlinkHandler = Tcl_CreateTimerHandler(
		    entryPtr->insertOnTime, EntryBlinkProc, entryPtr);
	}
	if (entryPtr->validate == VALIDATE_ALL ||
		entryPtr->validate == VALIDATE_FOCUS ||
		entryPtr->validate == VALIDATE_FOCUSIN) {
	    EntryValidateChange(entryPtr, NULL, entryPtr->string, TCL_INDEX_NONE,
		    VALIDATE_FOCUSIN);
	}
    } else {
	entryPtr->flags &= ~(GOT_FOCUS | CURSOR_ON);
	entryPtr->insertBlinkHandler = NULL;
	if (entryPtr->validate == VALIDATE_ALL ||
		entryPtr->validate == VALIDATE_FOCUS ||
		entryPtr->validate == VALIDATE_FOCUSOUT) {
	    EntryValidateChange(entryPtr, NULL, entryPtr->string, TCL_INDEX_NONE,
		    VALIDATE_FOCUSOUT);
	}
    }
    EventuallyRedraw(entryPtr);
}

/*
 *--------------------------------------------------------------
 *
 * EntryTextVarProc --
 *
 *	This function is invoked when someone changes the variable whose
 *	contents are to be displayed in an entry.
 *
 * Results:
 *	NULL is always returned.
 *
 * Side effects:
 *	The text displayed in the entry will change to match the variable.
 *
 *--------------------------------------------------------------
 */

static char *
EntryTextVarProc(
    void *clientData,	/* Information about button. */
    Tcl_Interp *interp,		/* Interpreter containing variable. */
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    int flags)			/* Information about what happened. */
{
    Entry *entryPtr = (Entry *)clientData;
    const char *value;

    if (entryPtr->flags & ENTRY_DELETED) {
	/*
	 * Just abort early if we entered here while being deleted.
	 */
	return NULL;
    }

    /*
     * If the variable is unset, then immediately recreate it unless the whole
     * interpreter is going away.
     */

    if (flags & TCL_TRACE_UNSETS) {
	if (!Tcl_InterpDeleted(interp) && entryPtr->textVarNameObj) {
	    void *probe = NULL;

	    do {
		probe = Tcl_VarTraceInfo(interp,
			Tcl_GetString(entryPtr->textVarNameObj),
			TCL_GLOBAL_ONLY|TCL_TRACE_WRITES|TCL_TRACE_UNSETS,
			EntryTextVarProc, probe);
		if (probe == entryPtr) {
		    break;
		}
	    } while (probe);
	    if (probe) {
		/*
		 * We were able to fetch the unset trace for our
		 * textVarName, which means it is not unset and not
		 * the cause of this unset trace. Instead some outdated
		 * former variable must be, and we should ignore it.
		 */
		return NULL;
	    }
	    Tcl_SetVar2(interp, Tcl_GetString(entryPtr->textVarNameObj), NULL,
		    entryPtr->string, TCL_GLOBAL_ONLY);
	    Tcl_TraceVar2(interp, Tcl_GetString(entryPtr->textVarNameObj), NULL,
		    TCL_GLOBAL_ONLY|TCL_TRACE_WRITES|TCL_TRACE_UNSETS,
		    EntryTextVarProc, clientData);
	    entryPtr->flags |= ENTRY_VAR_TRACED;
	}
	return NULL;
    }

    /*
     * Update the entry's text with the value of the variable, unless the
     * entry already has that value (this happens when the variable changes
     * value because we changed it because someone typed in the entry).
     */

    value = Tcl_GetVar2(interp, Tcl_GetString(entryPtr->textVarNameObj), NULL, TCL_GLOBAL_ONLY);
    if (value == NULL) {
	value = "";
    }
    EntrySetValue(entryPtr, value);
    return NULL;
}

/*
 *--------------------------------------------------------------
 *
 * EntryValidate --
 *
 *	This function is invoked when any character is added or removed from
 *	the entry widget, or a focus has trigerred validation.
 *
 * Results:

 *	TCL_OK if the validatecommand passes the new string. TCL_BREAK if the
 *	vcmd executed OK, but rejects the string. TCL_ERROR if an error
 *	occurred while executing the vcmd or a valid Tcl_Bool is not returned.
 *
 * Side effects:
 *	An error condition may arise
 *
 *--------------------------------------------------------------
 */

static int
EntryValidate(
     Entry *entryPtr,	/* Entry that needs validation. */
     char *cmd)	/* Validation command (NULL-terminated
				 * string). */
{
    Tcl_Interp *interp = entryPtr->interp;
    int code, isOK;

    code = Tcl_EvalEx(interp, cmd, TCL_INDEX_NONE, TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);

    /*
     * We accept TCL_OK and TCL_RETURN as valid return codes from the command
     * callback.
     */

    if (code != TCL_OK && code != TCL_RETURN) {
	Tcl_AppendObjToErrorInfo(interp, Tcl_ObjPrintf(
		"\n    (in validation command executed by %s)",
		Tk_PathName(entryPtr->tkwin)));
	Tcl_BackgroundException(interp, code);
	return TCL_ERROR;
    }

    /*
     * The command callback should return an acceptable Tcl boolean.
     */

    if (Tcl_GetBooleanFromObj(interp, Tcl_GetObjResult(interp),
	    &isOK) != TCL_OK) {
	Tcl_AddErrorInfo(interp,
		 "\n    (invalid boolean result from validation command)");
	Tcl_BackgroundException(interp, TCL_ERROR);
	Tcl_ResetResult(interp);
	return TCL_ERROR;
    }

    Tcl_ResetResult(interp);
    return (isOK ? TCL_OK : TCL_BREAK);
}

/*
 *--------------------------------------------------------------
 *
 * EntryValidateChange --
 *
 *	This function is invoked when any character is added or removed from
 *	the entry widget, or a focus has trigerred validation.
 *
 * Results:
 *	TCL_OK if the validatecommand accepts the new string, TCL_ERROR if any
 *	problems occurred with validatecommand.
 *
 * Side effects:
 *	The insertion/deletion may be aborted, and the validatecommand might
 *	turn itself off (if an error or loop condition arises).
 *
 *--------------------------------------------------------------
 */

static int
EntryValidateChange(
     Entry *entryPtr,	/* Entry that needs validation. */
     const char *change,	/* Characters to be added/deleted
				 * (NUL-terminated string). */
     const char *newValue,	/* Potential new value of entry string */
     Tcl_Size index,			/* index of insert/delete, TCL_INDEX_NONE otherwise */
     int type)			/* forced, delete, insert, focusin or
				 * focusout */
{
    int code, varValidate = (entryPtr->flags & VALIDATE_VAR);
    char *p;
    Tcl_DString script;

    if (entryPtr->validateCmdObj == NULL ||
	entryPtr->validate == VALIDATE_NONE) {
	if (entryPtr->flags & VALIDATING) {
	    entryPtr->flags |= VALIDATE_ABORT;
	}
	return (varValidate ? TCL_ERROR : TCL_OK);
    }

    /*
     * If we're already validating, then we're hitting a loop condition. Set
     * validate to none to disallow further validations, arrange for flags
     * to prevent current validation from finishing, and return.
     */

    if (entryPtr->flags & VALIDATING) {
	entryPtr->validate = VALIDATE_NONE;
	entryPtr->flags |= VALIDATE_ABORT;
	return (varValidate ? TCL_ERROR : TCL_OK);
    }

    entryPtr->flags |= VALIDATING;

    /*
     * Now form command string and run through the -validatecommand
     */

    Tcl_DStringInit(&script);
    ExpandPercents(entryPtr, (entryPtr->validateCmdObj ? Tcl_GetString(entryPtr->validateCmdObj) : NULL),
	    change, newValue, index, type, &script);
    Tcl_DStringAppend(&script, "", 1);

    p = Tcl_DStringValue(&script);
    code = EntryValidate(entryPtr, p);
    Tcl_DStringFree(&script);

    /*
     * If e->validate has become VALIDATE_NONE during the validation, or we
     * now have VALIDATE_VAR set (from EntrySetValue) and didn't before, it
     * means that a loop condition almost occurred. Do not allow this
     * validation result to finish.
     */

    if (entryPtr->validate == VALIDATE_NONE
	    || (!varValidate && (entryPtr->flags & VALIDATE_VAR))) {
	code = TCL_ERROR;
    }

    /*
     * It's possible that the user deleted the entry during validation. In
     * that case, abort future validation and return an error.
     */

    if (entryPtr->flags & ENTRY_DELETED) {
	return TCL_ERROR;
    }

    /*
     * If validate will return ERROR, then disallow further validations
     * Otherwise, if it didn't accept the new string (returned TCL_BREAK) then
     * eval the invalidCmd (if it's set)
     */

    if (code == TCL_ERROR) {
	entryPtr->validate = VALIDATE_NONE;
    } else if (code == TCL_BREAK) {
	/*
	 * If we were doing forced validation (like via a variable trace) and
	 * the command returned 0, the we turn off validation because we
	 * assume that textvariables have precedence in managing the value.
	 * We also don't call the invcmd, as it may want to do entry
	 * manipulation which the setting of the var will later wipe anyway.
	 */

	if (varValidate) {
	    entryPtr->validate = VALIDATE_NONE;
	} else if (entryPtr->invalidCmdObj != NULL) {
	    int result;

	    Tcl_DStringInit(&script);
	    ExpandPercents(entryPtr, Tcl_GetString(entryPtr->invalidCmdObj),
		    change, newValue, index, type, &script);
	    Tcl_DStringAppend(&script, "", 1);
	    p = Tcl_DStringValue(&script);
	    result = Tcl_EvalEx(entryPtr->interp, p, TCL_INDEX_NONE,
		    TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
	    if (result != TCL_OK) {
		Tcl_AddErrorInfo(entryPtr->interp,
			"\n    (in invalidcommand executed by entry)");
		Tcl_BackgroundException(entryPtr->interp, result);
		code = TCL_ERROR;
		entryPtr->validate = VALIDATE_NONE;
	    }
	    Tcl_DStringFree(&script);

	    /*
	     * It's possible that the user deleted the entry during
	     * validation. In that case, abort future validation and return an
	     * error.
	     */

	    if (entryPtr->flags & ENTRY_DELETED) {
		return TCL_ERROR;
	    }
	}
    }

    entryPtr->flags &= ~VALIDATING;

    return code;
}

/*
 *--------------------------------------------------------------
 *
 * ExpandPercents --
 *
 *	Given a command and an event, produce a new command by replacing %
 *	constructs in the original command with information from the X event.
 *
 * Results:
 *	The new expanded command is appended to the dynamic string given by
 *	dsPtr.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static void
ExpandPercents(
     Entry *entryPtr,	/* Entry that needs validation. */
     const char *before,
				/* Command containing percent expressions to
				 * be replaced. */
     const char *change,	/* Characters to added/deleted (NUL-terminated
				 * string). */
     const char *newValue,	/* Potential new value of entry string */
     Tcl_Size index,			/* index of insert/delete */
     int type,			/* INSERT or DELETE */
     Tcl_DString *dsPtr)	/* Dynamic string in which to append new
				 * command. */
{
    int spaceNeeded, cvtFlags;	/* Used to substitute string as proper Tcl
				 * list element. */
    int number, length;
    const char *string;
    int ch;
    char numStorage[2*TCL_INTEGER_SPACE];

    while (1) {
	if (*before == '\0') {
	    break;
	}
	/*
	 * Find everything up to the next % character and append it to the
	 * result string.
	 */

	string = before;

	/*
	 * No need to convert '%', as it is in ascii range.
	 */

	string = Tcl_UtfFindFirst(before, '%');
	if (string == NULL) {
	    Tcl_DStringAppend(dsPtr, before, TCL_INDEX_NONE);
	    break;
	} else if (string != before) {
	    Tcl_DStringAppend(dsPtr, before, string-before);
	    before = string;
	}

	/*
	 * There's a percent sequence here. Process it.
	 */

	before++; /* skip over % */
	if (*before != '\0') {
	    before += Tcl_UtfToUniChar(before, &ch);
	} else {
	    ch = '%';
	}
	if (type == VALIDATE_BUTTON) {
	    /*
	     * -command %-substitution
	     */

	    switch (ch) {
	    case 's':		/* Current string value of spinbox */
		string = entryPtr->string;
		break;
	    case 'd':		/* direction, up or down */
		string = change;
		break;
	    case 'W':		/* widget name */
		string = Tk_PathName(entryPtr->tkwin);
		break;
	    default:
		length = Tcl_UniCharToUtf(ch, numStorage);
		numStorage[length] = '\0';
		string = numStorage;
		break;
	    }
	} else {
	    /*
	     * -validatecommand / -invalidcommand %-substitution
	     */

	    switch (ch) {
	    case 'd':		/* Type of call that caused validation */
		switch (type) {
		case VALIDATE_INSERT:
		    number = 1;
		    break;
		case VALIDATE_DELETE:
		    number = 0;
		    break;
		default:
		    number = -1;
		    break;
		}
		snprintf(numStorage, sizeof(numStorage), "%d", number);
		string = numStorage;
		break;
	    case 'i':		/* index of insert/delete */
		snprintf(numStorage, sizeof(numStorage), "%d", (int)index);
		string = numStorage;
		break;
	    case 'P':		/* 'Peeked' new value of the string */
		string = newValue;
		break;
	    case 's':		/* Current string value of spinbox */
		string = entryPtr->string;
		break;
	    case 'S':		/* string to be inserted/deleted, if any */
		string = change;
		break;
	    case 'v':		/* type of validation currently set */
		string = validateStrings[entryPtr->validate];
		break;
	    case 'V':		/* type of validation in effect */
		switch (type) {
		case VALIDATE_INSERT:
		case VALIDATE_DELETE:
		    string = validateStrings[VALIDATE_KEY];
		    break;
		case VALIDATE_FORCED:
		    string = "forced";
		    break;
		default:
		    string = validateStrings[type];
		    break;
		}
		break;
	    case 'W': /* widget name */
		string = Tk_PathName(entryPtr->tkwin);
		break;
	    default:
		length = Tcl_UniCharToUtf(ch, numStorage);
		numStorage[length] = '\0';
		string = numStorage;
		break;
	    }
	}

	spaceNeeded = Tcl_ScanCountedElement(string, TCL_INDEX_NONE, &cvtFlags);
	length = Tcl_DStringLength(dsPtr);
	Tcl_DStringSetLength(dsPtr, length + spaceNeeded);
	spaceNeeded = Tcl_ConvertCountedElement(string, TCL_INDEX_NONE,
		Tcl_DStringValue(dsPtr) + length,
		cvtFlags | TCL_DONT_USE_BRACES);
	Tcl_DStringSetLength(dsPtr, length + spaceNeeded);
    }
}

/*
 *--------------------------------------------------------------
 *
 * Tk_SpinboxObjCmd --
 *
 *	This function is invoked to process the "spinbox" Tcl command. See the
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
Tk_SpinboxObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Entry *entryPtr;
    Spinbox *sbPtr;
    Tk_OptionTable optionTable;
    Tk_Window tkwin;
    char *tmp;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "pathName ?-option value ...?");
	return TCL_ERROR;
    }

    tkwin = Tk_CreateWindowFromPath(interp, Tk_MainWindow(interp),
	    Tcl_GetString(objv[1]), NULL);
    if (tkwin == NULL) {
	return TCL_ERROR;
    }

    /*
     * Create the option table for this widget class. If it has already been
     * created, Tk will return the cached value.
     */

    optionTable = Tk_CreateOptionTable(interp, sbOptSpec);

    /*
     * Initialize the fields of the structure that won't be initialized by
     * ConfigureEntry, or that ConfigureEntry requires to be initialized
     * already (e.g. resource pointers). Only the non-NULL/0 data must be
     * initialized as memset covers the rest.
     */

    sbPtr = (Spinbox *)ckalloc(sizeof(Spinbox));
    entryPtr			= (Entry *) sbPtr;
    memset(sbPtr, 0, sizeof(Spinbox));

    entryPtr->tkwin		= tkwin;
    entryPtr->display		= Tk_Display(tkwin);
    entryPtr->interp		= interp;
    entryPtr->widgetCmd		= Tcl_CreateObjCommand2(interp,
	    Tk_PathName(entryPtr->tkwin), SpinboxWidgetObjCmd, sbPtr,
	    EntryCmdDeletedProc);
    entryPtr->optionTable	= optionTable;
    entryPtr->type		= TK_SPINBOX;
    tmp				= (char *)ckalloc(1);
    tmp[0]			= '\0';
    entryPtr->string		= tmp;
    entryPtr->selectFirst	= TCL_INDEX_NONE;
    entryPtr->selectLast	= TCL_INDEX_NONE;

    entryPtr->cursor		= NULL;
    entryPtr->exportSelection	= 1;
    entryPtr->justify		= TK_JUSTIFY_LEFT;
    entryPtr->relief		= TK_RELIEF_FLAT;
    entryPtr->state		= STATE_NORMAL;
    entryPtr->displayString	= entryPtr->string;
    entryPtr->inset		= XPAD;
    entryPtr->textGC		= NULL;
    entryPtr->selTextGC		= NULL;
    entryPtr->highlightGC	= NULL;
    entryPtr->avgWidth		= 1;
    entryPtr->validate		= VALIDATE_NONE;

    sbPtr->selElement		= SEL_NONE;
    sbPtr->curElement		= SEL_NONE;
    sbPtr->bCursor		= NULL;
    sbPtr->repeatDelay		= 400;
    sbPtr->repeatInterval	= 100;
    sbPtr->fromValue		= 0.0;
    sbPtr->toValue		= 100.0;
    sbPtr->increment		= 1.0;
    sbPtr->formatBuf		= (char *)ckalloc(TCL_DOUBLE_SPACE);
    sbPtr->bdRelief		= TK_RELIEF_FLAT;
    sbPtr->buRelief		= TK_RELIEF_FLAT;

    entryPtr->placeholderGC	= NULL;

    /*
     * Keep a hold of the associated tkwin until we destroy the spinbox,
     * otherwise Tk might free it while we still need it.
     */

    Tcl_Preserve(entryPtr->tkwin);

    Tk_SetClass(entryPtr->tkwin, "Spinbox");
    Tk_SetClassProcs(entryPtr->tkwin, &entryClass, entryPtr);
    Tk_CreateEventHandler(entryPtr->tkwin,
	    PointerMotionMask|ExposureMask|StructureNotifyMask|FocusChangeMask,
	    EntryEventProc, entryPtr);
    Tk_CreateSelHandler(entryPtr->tkwin, XA_PRIMARY, XA_STRING,
	    EntryFetchSelection, entryPtr, XA_STRING);

    if (Tk_InitOptions(interp, sbPtr, optionTable, tkwin)
	    != TCL_OK) {
	Tk_DestroyWindow(entryPtr->tkwin);
	return TCL_ERROR;
    }
    if (ConfigureEntry(interp, entryPtr, objc-2, objv+2) != TCL_OK) {
	goto error;
    }

    Tcl_SetObjResult(interp, Tk_NewWindowObj(entryPtr->tkwin));
    return TCL_OK;

  error:
    Tk_DestroyWindow(entryPtr->tkwin);
    return TCL_ERROR;
}

/*
 *--------------------------------------------------------------
 *
 * SpinboxWidgetObjCmd --
 *
 *	This function is invoked to process the Tcl command that corresponds
 *	to a widget managed by this module. See the user documentation for
 *	details on what it does.
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
SpinboxWidgetObjCmd(
    void *clientData,	/* Information about spinbox widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Entry *entryPtr = (Entry *)clientData;
    Spinbox *sbPtr = (Spinbox *)clientData;
    int cmdIndex, selIndex, result;
    Tcl_Obj *objPtr;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
	return TCL_ERROR;
    }

    /*
     * Parse the widget command by looking up the second token in the list of
     * valid command names.
     */

    result = Tcl_GetIndexFromObj(interp, objv[1], sbCmdNames,
	    "option", 0, &cmdIndex);
    if (result != TCL_OK) {
	return result;
    }

    Tcl_Preserve(entryPtr);
    switch ((enum sbCmd) cmdIndex) {
    case SB_CMD_BBOX: {
	Tcl_Size index;
	int x, y, width, height;
	Tcl_Obj *bbox[4];

	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "index");
	    goto error;
	}
	if (GetEntryIndex(interp, entryPtr, objv[2],
		&index) != TCL_OK) {
	    goto error;
	}
	if ((index == entryPtr->numChars) && (index > 0)) {
	    index--;
	}
	Tk_CharBbox(entryPtr->textLayout, index, &x, &y, &width, &height);
	bbox[0] = Tcl_NewWideIntObj(x + entryPtr->layoutX);
	bbox[1] = Tcl_NewWideIntObj(y + entryPtr->layoutY);
	bbox[2] = Tcl_NewWideIntObj(width);
	bbox[3] = Tcl_NewWideIntObj(height);
	Tcl_SetObjResult(interp, Tcl_NewListObj(4, bbox));
	break;
    }

    case SB_CMD_CGET:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option");
	    goto error;
	}

	objPtr = Tk_GetOptionValue(interp, entryPtr,
		entryPtr->optionTable, objv[2], entryPtr->tkwin);
	if (objPtr == NULL) {
	    goto error;
	}
	Tcl_SetObjResult(interp, objPtr);
	break;

    case SB_CMD_CONFIGURE:
	if (objc <= 3) {
	    objPtr = Tk_GetOptionInfo(interp, entryPtr,
		    entryPtr->optionTable, (objc == 3) ? objv[2] : NULL,
		    entryPtr->tkwin);
	    if (objPtr == NULL) {
		goto error;
	    }
	    Tcl_SetObjResult(interp, objPtr);
	} else {
	    result = ConfigureEntry(interp, entryPtr, objc-2, objv+2);
	}
	break;

    case SB_CMD_DELETE: {
	Tcl_Size first, last;
	int code;

	if ((objc < 3) || (objc > 4)) {
	    Tcl_WrongNumArgs(interp, 2, objv, "firstIndex ?lastIndex?");
	    goto error;
	}
	if (GetEntryIndex(interp, entryPtr, objv[2],
		&first) != TCL_OK) {
	    goto error;
	}
	if (objc == 3) {
	    last = first + 1;
	} else {
	    if (GetEntryIndex(interp, entryPtr, objv[3],
		    &last) != TCL_OK) {
		goto error;
	    }
	}
	if ((last >= first) && (entryPtr->state == STATE_NORMAL)) {
	    code = DeleteChars(entryPtr, first, last - first);
	    if (code != TCL_OK) {
		goto error;
	    }
	}
	break;
    }

    case SB_CMD_GET:
	if (objc != 2) {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    goto error;
	}
	Tcl_SetObjResult(interp, Tcl_NewStringObj(entryPtr->string, TCL_INDEX_NONE));
	break;

    case SB_CMD_ICURSOR:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "pos");
	    goto error;
	}
	if (GetEntryIndex(interp, entryPtr, objv[2],
		&entryPtr->insertPos) != TCL_OK) {
	    goto error;
	}
	EventuallyRedraw(entryPtr);
	break;

    case SB_CMD_IDENTIFY: {
	int x, y, elem;

	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "x y");
	    goto error;
	}
	if ((Tcl_GetIntFromObj(interp, objv[2], &x) != TCL_OK) ||
		(Tcl_GetIntFromObj(interp, objv[3], &y) != TCL_OK)) {
	    goto error;
	}
	elem = GetSpinboxElement(sbPtr, x, y);
	if (elem != SEL_NONE) {
	    Tcl_SetObjResult(interp,
		    Tcl_NewStringObj(selElementNames[elem], TCL_INDEX_NONE));
	}
	break;
    }

    case SB_CMD_INDEX: {
	Tcl_Size index;

	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "string");
	    goto error;
	}
	if (GetEntryIndex(interp, entryPtr, objv[2],
		&index) != TCL_OK) {
	    goto error;
	}
	Tcl_SetObjResult(interp, TkNewIndexObj(index));
	break;
    }

    case SB_CMD_INSERT: {
	Tcl_Size index;
	int code;

	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "index text");
	    goto error;
	}
	if (GetEntryIndex(interp, entryPtr, objv[2],
		&index) != TCL_OK) {
	    goto error;
	}
	if (entryPtr->state == STATE_NORMAL) {
	    code = InsertChars(entryPtr, index, objv[3]);
	    if (code != TCL_OK) {
		goto error;
	    }
	}
	break;
    }

    case SB_CMD_INVOKE:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "elemName");
	    goto error;
	}
	result = Tcl_GetIndexFromObj(interp, objv[2],
		selElementNames, "element", 0, &cmdIndex);
	if (result != TCL_OK) {
	    goto error;
	}
	if (entryPtr->state != STATE_DISABLED) {
	    if (SpinboxInvoke(interp, sbPtr, cmdIndex) != TCL_OK) {
		goto error;
	    }
	}
	break;

    case SB_CMD_SCAN: {
	int x;
	const char *minorCmd;

	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "mark|dragto x");
	    goto error;
	}
	if (Tcl_GetIntFromObj(interp, objv[3], &x) != TCL_OK) {
	    goto error;
	}

	minorCmd = Tcl_GetString(objv[2]);
	if (minorCmd[0] == 'm'
		&& (strncmp(minorCmd, "mark", strlen(minorCmd)) == 0)) {
	    entryPtr->scanMarkX = x;
	    entryPtr->scanMarkIndex = entryPtr->leftIndex;
	} else if ((minorCmd[0] == 'd')
		&& (strncmp(minorCmd, "dragto", strlen(minorCmd)) == 0)) {
	    EntryScanTo(entryPtr, x);
	} else {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "bad scan option \"%s\": must be dragto or mark",
		    minorCmd));
	    Tcl_SetErrorCode(interp, "TCL", "LOOKUP", "INDEX", "scan option",
		    minorCmd, NULL);
	    goto error;
	}
	break;
    }

    case SB_CMD_SELECTION: {
	Tcl_Size index, index2;

	if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option ?index?");
	    goto error;
	}

	/*
	 * Parse the selection sub-command, using the command table
	 * "sbSelCmdNames" defined above.
	 */

	result = Tcl_GetIndexFromObj(interp, objv[2], sbSelCmdNames,
		"selection option", 0, &selIndex);
	if (result != TCL_OK) {
	    goto error;
	}

	/*
	 * Disabled entries don't allow the selection to be modified, but
	 * 'selection present' must return a boolean.
	 */

	if ((entryPtr->state == STATE_DISABLED)
		&& (selIndex != SB_SEL_PRESENT)) {
	    goto done;
	}

	switch (selIndex) {
	case SB_SEL_ADJUST:
	    if (objc != 4) {
		Tcl_WrongNumArgs(interp, 3, objv, "index");
		goto error;
	    }
	    if (GetEntryIndex(interp, entryPtr,
		    objv[3], &index) != TCL_OK) {
		goto error;
	    }
	    if (entryPtr->selectFirst != TCL_INDEX_NONE) {
		Tcl_Size half1, half2;

		half1 = (entryPtr->selectFirst + entryPtr->selectLast)/2;
		half2 = (entryPtr->selectFirst + entryPtr->selectLast + 1)/2;
		if (index < half1) {
		    entryPtr->selectAnchor = entryPtr->selectLast;
		} else if (index > half2) {
		    entryPtr->selectAnchor = entryPtr->selectFirst;
		} else {
		    /*
		     * We're at about the halfway point in the selection; just
		     * keep the existing anchor.
		     */
		}
	    }
	    EntrySelectTo(entryPtr, index);
	    break;

	case SB_SEL_CLEAR:
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 3, objv, NULL);
		goto error;
	    }
	    if (entryPtr->selectFirst != TCL_INDEX_NONE) {
		entryPtr->selectFirst = TCL_INDEX_NONE;
		entryPtr->selectLast = TCL_INDEX_NONE;
		EventuallyRedraw(entryPtr);
	    }
	    goto done;

	case SB_SEL_FROM:
	    if (objc != 4) {
		Tcl_WrongNumArgs(interp, 3, objv, "index");
		goto error;
	    }
	    if (GetEntryIndex(interp, entryPtr,
		    objv[3], &index) != TCL_OK) {
		goto error;
	    }
	    entryPtr->selectAnchor = index;
	    break;

	case SB_SEL_PRESENT:
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 3, objv, NULL);
		goto error;
	    }
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(
		    entryPtr->selectFirst != TCL_INDEX_NONE));
	    goto done;

	case SB_SEL_RANGE:
	    if (objc != 5) {
		Tcl_WrongNumArgs(interp, 3, objv, "start end");
		goto error;
	    }
	    if (GetEntryIndex(interp, entryPtr,
		    objv[3], &index) != TCL_OK) {
		goto error;
	    }
	    if (GetEntryIndex(interp, entryPtr,
		    objv[4],& index2) != TCL_OK) {
		goto error;
	    }
	    if (index >= index2) {
		entryPtr->selectFirst = TCL_INDEX_NONE;
		entryPtr->selectLast = TCL_INDEX_NONE;
	    } else {
		entryPtr->selectFirst = index;
		entryPtr->selectLast = index2;
	    }
	    if (!(entryPtr->flags & GOT_SELECTION)
		    && entryPtr->exportSelection
		    && (!Tcl_IsSafe(entryPtr->interp))) {
		Tk_OwnSelection(entryPtr->tkwin, XA_PRIMARY,
			EntryLostSelection, entryPtr);
		entryPtr->flags |= GOT_SELECTION;
	    }
	    EventuallyRedraw(entryPtr);
	    break;

	case SB_SEL_TO:
	    if (objc != 4) {
		Tcl_WrongNumArgs(interp, 3, objv, "index");
		goto error;
	    }
	    if (GetEntryIndex(interp, entryPtr,
		    objv[3], &index) != TCL_OK) {
		goto error;
	    }
	    EntrySelectTo(entryPtr, index);
	    break;

	case SB_SEL_ELEMENT:
	    if ((objc < 3) || (objc > 4)) {
		Tcl_WrongNumArgs(interp, 3, objv, "?elemName?");
		goto error;
	    }
	    if (objc == 3) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			selElementNames[sbPtr->selElement], TCL_INDEX_NONE));
	    } else {
		int lastElement = sbPtr->selElement;

		result = Tcl_GetIndexFromObj(interp, objv[3], selElementNames,
			"selection element", 0, &(sbPtr->selElement));
		if (result != TCL_OK) {
		    goto error;
		}
		if (lastElement != sbPtr->selElement) {
		    EventuallyRedraw(entryPtr);
		}
	    }
	    break;
	}
	break;
    }

    case SB_CMD_SET: {
	int code = TCL_OK;

	if (objc > 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "?string?");
	    goto error;
	}
	if (objc == 3) {
	    code = EntryValueChanged(entryPtr, Tcl_GetString(objv[2]));
	    if (code != TCL_OK) {
		goto error;
	    }
	}
	Tcl_SetObjResult(interp, Tcl_NewStringObj(entryPtr->string, TCL_INDEX_NONE));
	break;
    }

    case SB_CMD_VALIDATE: {
	int code;

	if (objc != 2) {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    goto error;
	}
	selIndex = entryPtr->validate;
	entryPtr->validate = VALIDATE_ALL;
	code = EntryValidateChange(entryPtr, NULL, entryPtr->string,
		TCL_INDEX_NONE, VALIDATE_FORCED);
	if (entryPtr->validate != VALIDATE_NONE) {
	    entryPtr->validate = selIndex;
	}

	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(code == TCL_OK));
	break;
    }

    case SB_CMD_XVIEW: {
	Tcl_Size index;

	if (objc == 2) {
	    double first, last;
	    Tcl_Obj *span[2];

	    EntryVisibleRange(entryPtr, &first, &last);
	    span[0] = Tcl_NewDoubleObj(first);
	    span[1] = Tcl_NewDoubleObj(last);
	    Tcl_SetObjResult(interp, Tcl_NewListObj(2, span));
	    goto done;
	} else if (objc == 3) {
	    if (GetEntryIndex(interp, entryPtr, objv[2],
		    &index) != TCL_OK) {
		goto error;
	    }
	} else {
	    double fraction;
	    int count;

	    index = entryPtr->leftIndex;
	    switch (Tk_GetScrollInfoObj(interp, objc, objv, &fraction,
		    &count)) {
	    case TK_SCROLL_MOVETO:
		index = ((fraction * entryPtr->numChars) + 0.5);
		break;
	    case TK_SCROLL_PAGES: {
		int charsPerPage;

		charsPerPage = ((Tk_Width(entryPtr->tkwin)
			- 2 * entryPtr->inset - entryPtr->xWidth)
			/ entryPtr->avgWidth) - 2;
		if (charsPerPage < 1) {
		    charsPerPage = 1;
		}
		index += count * charsPerPage;
		break;
	    }
	    case TK_SCROLL_UNITS:
		index += count;
		break;
	    default:
		goto error;
	    }
	}
	if (index >= entryPtr->numChars) {
	    index = entryPtr->numChars - 1;
	}
	if (index < 0) {
	    index = 0;
	}
	entryPtr->leftIndex = index;
	entryPtr->flags |= UPDATE_SCROLLBAR;
	EntryComputeGeometry(entryPtr);
	EventuallyRedraw(entryPtr);
	break;
    }
    }

  done:
    Tcl_Release(entryPtr);
    return result;

  error:
    Tcl_Release(entryPtr);
    return TCL_ERROR;
}

/*
 *---------------------------------------------------------------------------
 *
 * GetSpinboxElement --
 *
 *	Return the element associated with an x,y coord.
 *
 * Results:
 *	Element type as enum selelement.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static int
GetSpinboxElement(
    Spinbox *sbPtr,		/* Spinbox for which the index is being
				 * specified. */
    int x, int y)		/* Widget-relative coordinates. */
{
    Entry *entryPtr = (Entry *) sbPtr;

    if ((x < 0) || (y < 0) || (y > Tk_Height(entryPtr->tkwin))
	    || (x > Tk_Width(entryPtr->tkwin))) {
	return SEL_NONE;
    }

    if (x > (Tk_Width(entryPtr->tkwin) - entryPtr->inset - entryPtr->xWidth)) {
	if (y > (Tk_Height(entryPtr->tkwin) / 2)) {
	    return SEL_BUTTONDOWN;
	} else {
	    return SEL_BUTTONUP;
	}
    }
    return SEL_ENTRY;
}

/*
 *--------------------------------------------------------------
 *
 * SpinboxInvoke --
 *
 *	This function is invoked when the invoke method for the widget is
 *	called.
 *
 * Results:
 *	TCL_OK.
 *
 * Side effects:
 *	A background error condition may arise when invoking the callback.
 *	The widget value may change.
 *
 *--------------------------------------------------------------
 */

static int
SpinboxInvoke(
    Tcl_Interp *interp,/* Current interpreter. */
    Spinbox *sbPtr,	/* Spinbox to invoke. */
    int element)		/* Element to invoke, either the "up" or
				 * "down" button. */
{
    Entry *entryPtr = (Entry *) sbPtr;
    const char *type;
    int code, up;
    Tcl_DString script;

    switch (element) {
    case SEL_BUTTONUP:
	type = "up";
	up = 1;
	break;
    case SEL_BUTTONDOWN:
	type = "down";
	up = 0;
	break;
    default:
	return TCL_OK;
    }

    code = TCL_OK;
    if (fabs(sbPtr->increment) > MIN_DBL_VAL) {
	if (sbPtr->listObj != NULL) {
	    Tcl_Obj *objPtr;

	    Tcl_ListObjIndex(interp, sbPtr->listObj, sbPtr->eIndex, &objPtr);
	    if (strcmp(Tcl_GetString(objPtr), entryPtr->string)) {
		/*
		 * Somehow the string changed from what we expected, so let's
		 * do a search on the list to see if the current value is
		 * there. If not, move to the first element of the list.
		 */

		Tcl_Size i, listc;
		Tcl_Size elemLen, length = entryPtr->numChars;
		const char *bytes;
		Tcl_Obj **listv;

		Tcl_ListObjGetElements(interp, sbPtr->listObj, &listc, &listv);
		for (i = 0; i < listc; i++) {
		    bytes = Tcl_GetStringFromObj(listv[i], &elemLen);
		    if ((length == elemLen) &&
			    (memcmp(bytes, entryPtr->string,
				    length) == 0)) {
			sbPtr->eIndex = i;
			break;
		    }
		}
	    }
	    if (up) {
		if (++sbPtr->eIndex >= sbPtr->nElements) {
		    if (sbPtr->wrap) {
			sbPtr->eIndex = 0;
		    } else {
			sbPtr->eIndex = sbPtr->nElements-1;
		    }
		}
	    } else {
		if (--sbPtr->eIndex < 0) {
		    if (sbPtr->wrap) {
			sbPtr->eIndex = sbPtr->nElements-1;
		    } else {
			sbPtr->eIndex = 0;
		    }
		}
	    }
	    Tcl_ListObjIndex(interp, sbPtr->listObj, sbPtr->eIndex, &objPtr);
	    code = EntryValueChanged(entryPtr, Tcl_GetString(objPtr));
	} else if (!DOUBLES_EQ(sbPtr->fromValue, sbPtr->toValue)) {
	    double dvalue;

	    if (sscanf(entryPtr->string, "%lf", &dvalue) <= 0) {
		/*
		 * If the string doesn't scan as a double value, just
		 * use the -from value
		 */

		dvalue = sbPtr->fromValue;
	    } else if (up) {
		dvalue += sbPtr->increment;
		if (dvalue > sbPtr->toValue) {
		    if (sbPtr->wrap) {
			dvalue = sbPtr->fromValue;
		    } else {
			dvalue = sbPtr->toValue;
		    }
		} else if (dvalue < sbPtr->fromValue) {
		    /*
		     * It's possible that when pressing up, we are still less
		     * than the fromValue, because the user may have
		     * manipulated the value by hand.
		     */

		    dvalue = sbPtr->fromValue;
		}
	    } else {
		dvalue -= sbPtr->increment;
		if (dvalue < sbPtr->fromValue) {
		    if (sbPtr->wrap) {
			dvalue = sbPtr->toValue;
		    } else {
			dvalue = sbPtr->fromValue;
		    }
		} else if (dvalue > sbPtr->toValue) {
		    /*
		     * It's possible that when pressing down, we are still
		     * greater than the toValue, because the user may have
		     * manipulated the value by hand.
		     */

		    dvalue = sbPtr->toValue;
		}
	    }
	    snprintf(sbPtr->formatBuf, TCL_DOUBLE_SPACE, sbPtr->valueFormat, dvalue);
	    code = EntryValueChanged(entryPtr, sbPtr->formatBuf);
	}
    }
    if (code != TCL_OK) {
	return TCL_ERROR;
    }

    if (sbPtr->commandObj != NULL) {
	Tcl_DStringInit(&script);
	ExpandPercents(entryPtr, Tcl_GetString(sbPtr->commandObj), type, "", 0,
		VALIDATE_BUTTON, &script);
	Tcl_DStringAppend(&script, "", 1);

	code = Tcl_EvalEx(interp, Tcl_DStringValue(&script), TCL_INDEX_NONE,
		TCL_EVAL_GLOBAL | TCL_EVAL_DIRECT);
	Tcl_DStringFree(&script);

	if (code != TCL_OK) {
	    Tcl_AddErrorInfo(interp,
		    "\n    (in command executed by spinbox)");
	    Tcl_BackgroundException(interp, code);

	    /*
	     * Yes, it's an error, but a bg one, so we return OK
	     */

	    return TCL_OK;
	}

	Tcl_ResetResult(interp);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ComputeFormat --
 *
 *	This function is invoked to recompute the "format" fields of a
 *	spinbox's widget record, which determines how the value of the dial is
 *	converted to a string.
 *
 * Results:
 *	Tcl result code.
 *
 * Side effects:
 *	The format fields of the spinbox are modified.
 *
 *----------------------------------------------------------------------
 */

static int
ComputeFormat(
     Spinbox *sbPtr)		/* Information about dial widget. */
{
    double maxValue, x;
    int mostSigDigit, numDigits, leastSigDigit, afterDecimal;
    int eDigits, fDigits;

    /*
     * Compute the displacement from the decimal of the most significant digit
     * required for any number in the dial's range.
     */

    if (sbPtr->reqFormatObj) {
	sbPtr->valueFormat = Tcl_GetString(sbPtr->reqFormatObj);
	return TCL_OK;
    }

    maxValue = fabs(sbPtr->fromValue);
    x = fabs(sbPtr->toValue);
    if (x > maxValue) {
	maxValue = x;
    }
    if (maxValue == 0) {
	maxValue = 1;
    }
    mostSigDigit = (int) floor(log10(maxValue));

    if (fabs(sbPtr->increment) > MIN_DBL_VAL) {
	/*
	 * A increment was specified, so use it.
	 */

	leastSigDigit = (int) floor(log10(sbPtr->increment));
    } else {
	leastSigDigit = 0;
    }
    numDigits = mostSigDigit - leastSigDigit + 1;
    if (numDigits < 1) {
	numDigits = 1;
    }

    /*
     * Compute the number of characters required using "e" format and "f"
     * format, and then choose whichever one takes fewer characters.
     */

    eDigits = numDigits + 4;
    if (numDigits > 1) {
	eDigits++;		/* Decimal point. */
    }
    afterDecimal = numDigits - mostSigDigit - 1;
    if (afterDecimal < 0) {
	afterDecimal = 0;
    }
    fDigits = (mostSigDigit >= 0) ? mostSigDigit + afterDecimal : afterDecimal;
    if (afterDecimal > 0) {
	fDigits++;		/* Decimal point. */
    }
    if (mostSigDigit < 0) {
	fDigits++;		/* Zero to left of decimal point. */
    }
    if (fDigits <= eDigits) {
	snprintf(sbPtr->digitFormat, sizeof(sbPtr->digitFormat), "%%.%df", afterDecimal);
    } else {
	snprintf(sbPtr->digitFormat, sizeof(sbPtr->digitFormat), "%%.%de", numDigits-1);
    }
    sbPtr->valueFormat = sbPtr->digitFormat;
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
