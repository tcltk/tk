/*
 * DERIVED FROM: tk/generic/tkEntry.c r1.35.
 *
 * Copyright © 1990-1994 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 * Copyright © 2000 Ajuba Solutions.
 * Copyright © 2002 ActiveState Corporation.
 * Copyright © 2004 Joe English
 */

#include "tkInt.h"
#include "ttkTheme.h"
#include "ttkWidget.h"

#ifdef _WIN32
#include "tkWinInt.h"
#endif

/*
 * Extra bits for core.flags:
 */
#define GOT_SELECTION		(WIDGET_USER_FLAG<<1)
#define SYNCING_VARIABLE	(WIDGET_USER_FLAG<<2)
#define VALIDATING		(WIDGET_USER_FLAG<<3)
#define VALIDATION_SET_VALUE	(WIDGET_USER_FLAG<<4)

/*
 * Definitions for -validate option values:
 */
typedef enum validateMode {
    VMODE_ALL, VMODE_KEY, VMODE_FOCUS, VMODE_FOCUSIN, VMODE_FOCUSOUT, VMODE_NONE
} VMODE;

static const char *const validateStrings[] = {
    "all", "key", "focus", "focusin", "focusout", "none", NULL
};

/*
 * Validation reasons:
 */
typedef enum validateReason {
    VALIDATE_INSERT, VALIDATE_DELETE,
    VALIDATE_FOCUSIN, VALIDATE_FOCUSOUT,
    VALIDATE_FORCED
} VREASON;

static const char *const validateReasonStrings[] = {
    "key", "key", "focusin", "focusout", "forced", NULL
};

/*------------------------------------------------------------------------
 * +++ Entry widget record.
 *
 * Dependencies:
 *
 * textVariableTrace	: textVariableObj
 *
 * numBytes,numChars	: string
 * displayString	: numChars, showChar
 * layoutHeight,
 * layoutWidth,
 * textLayout		: fontObj, displayString
 * layoutX, layoutY	: textLayout, justify, xscroll.first
 *
 * Invariants:
 *
 * 0 <= insertPos <= numChars
 * 0 <= selectFirst < selectLast <= numChars || selectFirst == selectLast == -1
 * displayString points to string if showChar == NULL,
 * or to malloc'ed storage if showChar != NULL.
 */

/* Style parameters:
 */
typedef struct {
    Tcl_Obj *placeholderForegroundObj;/* Foreground color for placeholder text */
    Tcl_Obj *foregroundObj;	/* Foreground color for normal text */
    Tcl_Obj *backgroundObj;	/* Entry widget background color */
    Tcl_Obj *selBorderObj;	/* Border and background for selection */
    Tcl_Obj *selBorderWidthObj;	/* Width of selection border */
    Tcl_Obj *selForegroundObj;	/* Foreground color for selected text */
    Tcl_Obj *insertColorObj;	/* Color of insertion cursor */
    Tcl_Obj *insertWidthObj;	/* Insert cursor width */
} EntryStyleData;

typedef struct {
    /*
     * Internal state:
     */
    char *string;		/* Storage for string (malloced) */
    Tcl_Size numBytes;		/* Length of string in bytes. */
    Tcl_Size numChars;		/* Length of string in characters. */

    Tcl_Size insertPos;		/* Insert index */
    Tcl_Size selectFirst;		/* Index of start of selection, or TCL_INDEX_NONE */
    Tcl_Size selectLast;		/* Index of end of selection, or TCL_INDEX_NONE */

    Scrollable xscroll;		/* Current scroll position */
    ScrollHandle xscrollHandle;

    /*
     * Options managed by Tk_SetOptions:
     */
    Tcl_Obj *textVariableObj;	/* Name of linked variable */
    int exportSelection;	/* Tie internal selection to X selection? */

    VMODE validate;		/* Validation mode */
    Tcl_Obj *validateCmdObj;	/* Validation script template */
    Tcl_Obj *invalidCmdObj;		/* Invalid callback script template */

    Tcl_Obj *showCharObj;		/* Used to derive displayString */

    Tcl_Obj *fontObj;		/* Text font to use */
    Tcl_Obj *widthObj;		/* Desired width of window (in avgchars) */
    Tk_Justify justify;		/* Text justification */

    EntryStyleData styleData;	/* Display style data (widget options) */
    EntryStyleData styleDefaults;/* Style defaults (fallback values) */

    Tcl_Obj *stateObj;		/* Compatibility option -- see CheckStateObj */

    Tcl_Obj *placeholderObj;	/* Text to display for placeholder text */

    /*
     * Derived resources:
     */
    Ttk_TraceHandle *textVariableTrace;

    char *displayString;	/* String to use when displaying */
    Tk_TextLayout textLayout;	/* Cached text layout information. */
    int layoutWidth;		/* textLayout width */
    int layoutHeight;		/* textLayout height */

    int layoutX, layoutY;	/* Origin for text layout. */

} EntryPart;

typedef struct {
    WidgetCore	core;
    EntryPart	entry;
} Entry;

/*
 * Extra mask bits for Tk_SetOptions()
 */
#define STATE_CHANGED		(0x100)	/* -state option changed */
#define TEXTVAR_CHANGED		(0x200)	/* -textvariable option changed */
#define SCROLLCMD_CHANGED	(0x400)	/* -xscrollcommand option changed */

/*
 * Default option values:
 */
#define DEF_SELECT_BG		"#000000"
#define DEF_SELECT_FG		"#ffffff"
#define DEF_PLACEHOLDER_FG	"#b3b3b3"
#define DEF_INSERT_BG		"black"
#define DEF_ENTRY_WIDTH		"20"
#define DEF_ENTRY_FONT		"TkTextFont"
#define DEF_LIST_HEIGHT		"10"

static const Tk_OptionSpec EntryOptionSpecs[] = {
    {TK_OPTION_BOOLEAN, "-exportselection", "exportSelection",
	"ExportSelection", "1", TCL_INDEX_NONE, offsetof(Entry, entry.exportSelection),
	0,0,0 },
    {TK_OPTION_FONT, "-font", "font", "Font",
	DEF_ENTRY_FONT, offsetof(Entry, entry.fontObj),TCL_INDEX_NONE,
	0,0,GEOMETRY_CHANGED},
    {TK_OPTION_STRING, "-invalidcommand", "invalidCommand", "InvalidCommand",
	NULL, offsetof(Entry, entry.invalidCmdObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_JUSTIFY, "-justify", "justify", "Justify",
	"left", TCL_INDEX_NONE, offsetof(Entry, entry.justify),
	TK_OPTION_ENUM_VAR, 0, GEOMETRY_CHANGED},
    {TK_OPTION_STRING, "-placeholder", "placeHolder", "PlaceHolder",
	NULL, offsetof(Entry, entry.placeholderObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-show", "show", "Show",
	NULL, offsetof(Entry, entry.showCharObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-state", "state", "State",
	"normal", offsetof(Entry, entry.stateObj), TCL_INDEX_NONE,
	0,0,STATE_CHANGED},
    {TK_OPTION_STRING, "-textvariable", "textVariable", "Variable",
	NULL, offsetof(Entry, entry.textVariableObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,TEXTVAR_CHANGED},
    {TK_OPTION_STRING_TABLE, "-validate", "validate", "Validate",
	"none", TCL_INDEX_NONE, offsetof(Entry, entry.validate),
	TK_OPTION_ENUM_VAR, validateStrings, 0},
    {TK_OPTION_STRING, "-validatecommand", "validateCommand", "ValidateCommand",
	NULL, offsetof(Entry, entry.validateCmdObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_INT, "-width", "width", "Width",
	DEF_ENTRY_WIDTH, offsetof(Entry, entry.widthObj), TCL_INDEX_NONE,
	0,0,GEOMETRY_CHANGED},
    {TK_OPTION_STRING, "-xscrollcommand", "xScrollCommand", "ScrollCommand",
	NULL, offsetof(Entry, entry.xscroll.scrollCmdObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, SCROLLCMD_CHANGED},

    /* EntryStyleData options:
     */
    {TK_OPTION_COLOR, "-background", "windowColor", "WindowColor",
	NULL, offsetof(Entry, entry.styleData.backgroundObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0},
    {TK_OPTION_COLOR, "-foreground", "textColor", "TextColor",
	NULL, offsetof(Entry, entry.styleData.foregroundObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0},
    {TK_OPTION_COLOR, "-placeholderforeground", "placeholderForeground",
	"PlaceholderForeground", NULL,
	offsetof(Entry, entry.styleData.placeholderForegroundObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0},

    WIDGET_TAKEFOCUS_TRUE,
    WIDGET_INHERIT_OPTIONS(ttkCoreOptionSpecs)
};

/*------------------------------------------------------------------------
 * +++ EntryStyleData management.
 *	This is still more awkward than it should be;
 *	it should be able to use the Element API instead.
 */

/* EntryInitStyleDefaults --
 *	Initialize EntryStyleData record to fallback values.
 */
static void EntryInitStyleDefaults(EntryStyleData *es)
{
#define INIT(member, value) \
	es->member = Tcl_NewStringObj(value, -1); \
	Tcl_IncrRefCount(es->member);
    INIT(placeholderForegroundObj, DEF_PLACEHOLDER_FG)
    INIT(foregroundObj, DEFAULT_FOREGROUND)
    INIT(selBorderObj, DEF_SELECT_BG)
    INIT(selForegroundObj, DEF_SELECT_FG)
    INIT(insertColorObj, DEFAULT_FOREGROUND)
    INIT(selBorderWidthObj, "0")
    INIT(insertWidthObj, "1")
#undef INIT
}

static void EntryFreeStyleDefaults(EntryStyleData *es)
{
    Tcl_DecrRefCount(es->placeholderForegroundObj);
    Tcl_DecrRefCount(es->foregroundObj);
    Tcl_DecrRefCount(es->selBorderObj);
    Tcl_DecrRefCount(es->selForegroundObj);
    Tcl_DecrRefCount(es->insertColorObj);
    Tcl_DecrRefCount(es->selBorderWidthObj);
    Tcl_DecrRefCount(es->insertWidthObj);
}

/*
 * EntryInitStyleData --
 *	Look up style-specific data for an entry widget.
 */
static void EntryInitStyleData(Entry *entryPtr, EntryStyleData *es)
{
    Ttk_State state = entryPtr->core.state;
    Ttk_ResourceCache cache = Ttk_GetResourceCache(entryPtr->core.interp);
    Tk_Window tkwin = entryPtr->core.tkwin;
    Tcl_Obj *tmp;

    /* Initialize to fallback values:
     */
    *es = entryPtr->entry.styleDefaults;

#   define INIT(member, name) \
    if ((tmp=Ttk_QueryOption(entryPtr->core.layout,name,state))) \
	es->member=tmp;
    INIT(placeholderForegroundObj, "-placeholderforeground");
    INIT(foregroundObj, "-foreground");
    INIT(selBorderObj, "-selectbackground")
    INIT(selBorderWidthObj, "-selectborderwidth")
    INIT(selForegroundObj, "-selectforeground")
    INIT(insertColorObj, "-insertcolor")
    INIT(insertWidthObj, "-insertwidth")
#undef INIT

    /* Reacquire color & border resources from resource cache.
     */
    es->placeholderForegroundObj = Ttk_UseColor(cache, tkwin, es->placeholderForegroundObj);
    es->foregroundObj = Ttk_UseColor(cache, tkwin, es->foregroundObj);
    es->selForegroundObj = Ttk_UseColor(cache, tkwin, es->selForegroundObj);
    es->insertColorObj = Ttk_UseColor(cache, tkwin, es->insertColorObj);
    es->selBorderObj = Ttk_UseBorder(cache, tkwin, es->selBorderObj);
}

/*------------------------------------------------------------------------
 * +++ Resource management.
 */

/* EntryDisplayString --
 *	Return a malloc'ed string consisting of 'numChars' copies
 *	of (the first character in the string) 'showChar'.
 *	Used to compute the displayString if -show is non-NULL.
 */
static char *EntryDisplayString(const char *showChar, int numChars)
{
    char *displayString, *p;
    int size;
    int ch;
    char buf[6];

    Tcl_UtfToUniChar(showChar, &ch);
    size = Tcl_UniCharToUtf(ch, buf);
    p = displayString = (char *)ckalloc(numChars * size + 1);

    while (numChars--) {
	memcpy(p, buf, size);
	p += size;
    }
    *p = '\0';

    return displayString;
}

/* EntryUpdateTextLayout --
 *	Recompute textLayout, layoutWidth, and layoutHeight
 *	from displayString and fontObj.
 */
static void EntryUpdateTextLayout(Entry *entryPtr)
{
    Tcl_Size length;
    char *text;
    Tk_FreeTextLayout(entryPtr->entry.textLayout);
    if ((entryPtr->entry.numChars != 0) || (entryPtr->entry.placeholderObj == NULL)) {
	entryPtr->entry.textLayout = Tk_ComputeTextLayout(
	    Tk_GetFontFromObj(entryPtr->core.tkwin, entryPtr->entry.fontObj),
	    entryPtr->entry.displayString, entryPtr->entry.numChars,
	    0/*wraplength*/, entryPtr->entry.justify, TK_IGNORE_NEWLINES,
	    &entryPtr->entry.layoutWidth, &entryPtr->entry.layoutHeight);
    } else {
	text = Tcl_GetStringFromObj(entryPtr->entry.placeholderObj, &length);
	entryPtr->entry.textLayout = Tk_ComputeTextLayout(
	    Tk_GetFontFromObj(entryPtr->core.tkwin, entryPtr->entry.fontObj),
	    text, length,
	    0/*wraplength*/, entryPtr->entry.justify, TK_IGNORE_NEWLINES,
	    &entryPtr->entry.layoutWidth, &entryPtr->entry.layoutHeight);
    }
}

/* EntryEditable --
 *	Returns 1 if the entry widget accepts user changes, 0 otherwise
 */
static int
EntryEditable(Entry *entryPtr)
{
    return !(entryPtr->core.state & (TTK_STATE_DISABLED|TTK_STATE_READONLY));
}

/*------------------------------------------------------------------------
 * +++ Selection management.
 */

/* EntryFetchSelection --
 *	Selection handler for entry widgets.
 */
static Tcl_Size
EntryFetchSelection(
    void *clientData, Tcl_Size offset, char *buffer, Tcl_Size maxBytes)
{
    Entry *entryPtr = (Entry *)clientData;
    Tcl_Size byteCount;
    const char *string;
    const char *selStart, *selEnd;

    if (entryPtr->entry.selectFirst < 0 || (!entryPtr->entry.exportSelection)
	    || Tcl_IsSafe(entryPtr->core.interp)) {
	return TCL_INDEX_NONE;
    }
    string = entryPtr->entry.displayString;

    selStart = Tcl_UtfAtIndex(string, entryPtr->entry.selectFirst);
    selEnd = Tcl_UtfAtIndex(selStart,
	    entryPtr->entry.selectLast - entryPtr->entry.selectFirst);
    if (selEnd  <= selStart + offset) {
	return 0;
    }
    byteCount = selEnd - selStart - offset;
    if (byteCount > maxBytes) {
    /* @@@POSSIBLE BUG: Can transfer partial UTF-8 sequences.  Is this OK? */
	byteCount = maxBytes;
    }
    memcpy(buffer, selStart + offset, byteCount);
    buffer[byteCount] = '\0';
    return byteCount;
}

/* EntryLostSelection --
 *	Tk_LostSelProc for Entry widgets; called when an entry
 *	loses ownership of the selection.
 */
static void EntryLostSelection(void *clientData)
{
    Entry *entryPtr = (Entry *)clientData;
    entryPtr->core.flags &= ~GOT_SELECTION;
    entryPtr->entry.selectFirst = entryPtr->entry.selectLast = TCL_INDEX_NONE;
    TtkRedisplayWidget(&entryPtr->core);
}

/* EntryOwnSelection --
 *	Assert ownership of the PRIMARY selection,
 *	if -exportselection set and selection is present and interp is unsafe.
 */
static void EntryOwnSelection(Entry *entryPtr)
{
    if (entryPtr->entry.exportSelection
	&& (!Tcl_IsSafe(entryPtr->core.interp))
	&& !(entryPtr->core.flags & GOT_SELECTION)) {
	Tk_OwnSelection(entryPtr->core.tkwin, XA_PRIMARY, EntryLostSelection,
		entryPtr);
	entryPtr->core.flags |= GOT_SELECTION;
    }
}

/*------------------------------------------------------------------------
 * +++ Validation.
 */

/* ExpandPercents --
 *	Expand an entry validation script template (-validatecommand
 *	or -invalidcommand).
 */
static void
ExpandPercents(
     Entry *entryPtr,		/* Entry that needs validation. */
     const char *templ,	/* Script template */
     const char *newValue,		/* Potential new value of entry string */
     Tcl_Size index,			/* index of insert/delete */
     int count,			/* #changed characters */
     VREASON reason,		/* Reason for change */
     Tcl_DString *dsPtr)	/* Result of %-substitutions */
{
    int spaceNeeded, cvtFlags;
    int number, length;
    const char *string;
    int stringLength;
    int ch;
    char numStorage[2*TCL_INTEGER_SPACE];

    while (*templ) {
	/* Find everything up to the next % character and append it
	 * to the result string.
	 */
	string = Tcl_UtfFindFirst(templ, '%');
	if (string == NULL) {
	    /* No more %-sequences to expand.
	     * Copy the rest of the template.
	     */
	    Tcl_DStringAppend(dsPtr, templ, TCL_INDEX_NONE);
	    return;
	}
	if (string != templ) {
	    Tcl_DStringAppend(dsPtr, templ, string - templ);
	    templ = string;
	}

	/* There's a percent sequence here.  Process it.
	 */
	++templ; /* skip over % */
	if (*templ != '\0') {
	    templ += Tcl_UtfToUniChar(templ, &ch);
	} else {
	    ch = '%';
	}

	stringLength = -1;
	switch (ch) {
	    case 'd': /* Type of call that caused validation */
		if (reason == VALIDATE_INSERT) {
		    number = 1;
		} else if (reason == VALIDATE_DELETE) {
		    number = 0;
		} else {
		    number = -1;
		}
		snprintf(numStorage, sizeof(numStorage), "%d", number);
		string = numStorage;
		break;
	    case 'i': /* index of insert/delete */
		snprintf(numStorage, sizeof(numStorage), "%" TCL_SIZE_MODIFIER "d", index);
		string = numStorage;
		break;
	    case 'P': /* 'Peeked' new value of the string */
		string = newValue;
		break;
	    case 's': /* Current string value */
		string = entryPtr->entry.string;
		break;
	    case 'S': /* string to be inserted/deleted, if any */
		if (reason == VALIDATE_INSERT) {
		    string = Tcl_UtfAtIndex(newValue, index);
		    stringLength = Tcl_UtfAtIndex(string, count) - string;
		} else if (reason == VALIDATE_DELETE) {
		    string = Tcl_UtfAtIndex(entryPtr->entry.string, index);
		    stringLength = Tcl_UtfAtIndex(string, count) - string;
		} else {
		    string = "";
		    stringLength = 0;
		}
		break;
	    case 'v': /* type of validation currently set */
		string = validateStrings[entryPtr->entry.validate];
		break;
	    case 'V': /* type of validation in effect */
		string = validateReasonStrings[reason];
		break;
	    case 'W': /* widget name */
		string = Tk_PathName(entryPtr->core.tkwin);
		break;
	    default:
		length = Tcl_UniCharToUtf(ch, numStorage);
		numStorage[length] = '\0';
		string = numStorage;
		break;
	}

	spaceNeeded = Tcl_ScanCountedElement(string, stringLength, &cvtFlags);
	length = Tcl_DStringLength(dsPtr);
	Tcl_DStringSetLength(dsPtr, length + spaceNeeded);
	spaceNeeded = Tcl_ConvertCountedElement(string, stringLength,
		Tcl_DStringValue(dsPtr) + length,
		cvtFlags | TCL_DONT_USE_BRACES);
	Tcl_DStringSetLength(dsPtr, length + spaceNeeded);
    }
}

/* RunValidationScript --
 *	Build and evaluate an entry validation script.
 *	If the script raises an error, disable validation
 *	by setting '-validate none'
 */
static int RunValidationScript(
    Tcl_Interp *interp,	/* Interpreter to use */
    Entry *entryPtr,		/* Entry being validated */
    const char *templ,	/* Script template */
    const char *optionName,	/* "-validatecommand", "-invalidcommand" */
    const char *newValue,	/* Potential new value of entry string */
    Tcl_Size index,			/* index of insert/delete */
    Tcl_Size count,			/* #changed characters */
    VREASON reason)		/* Reason for change */
{
    Tcl_DString script;
    int code;

    Tcl_DStringInit(&script);
    ExpandPercents(entryPtr, templ, newValue, index, count, reason, &script);
    code = Tcl_EvalEx(interp,
		Tcl_DStringValue(&script), Tcl_DStringLength(&script),
		TCL_EVAL_GLOBAL);
    Tcl_DStringFree(&script);
    if (WidgetDestroyed(&entryPtr->core))
	return TCL_ERROR;

    if (code != TCL_OK && code != TCL_RETURN) {
	Tcl_AddErrorInfo(interp, "\n\t(in ");
	Tcl_AddErrorInfo(interp, optionName);
	Tcl_AddErrorInfo(interp, " validation command executed by ");
	Tcl_AddErrorInfo(interp, Tk_PathName(entryPtr->core.tkwin));
	Tcl_AddErrorInfo(interp, ")");
	entryPtr->entry.validate = VMODE_NONE;
	return TCL_ERROR;
    }
    return TCL_OK;
}

/* EntryNeedsValidation --
 *	Determine whether the specified VREASON should trigger validation
 *	in the current VMODE.
 */
static int EntryNeedsValidation(VMODE vmode, VREASON reason)
{
    return (reason == VALIDATE_FORCED)
	|| (vmode == VMODE_ALL)
	|| (reason == VALIDATE_FOCUSIN
	    && (vmode == VMODE_FOCUSIN || vmode == VMODE_FOCUS))
	|| (reason == VALIDATE_FOCUSOUT
	    && (vmode == VMODE_FOCUSOUT || vmode == VMODE_FOCUS))
	|| (reason == VALIDATE_INSERT && vmode == VMODE_KEY)
	|| (reason == VALIDATE_DELETE && vmode == VMODE_KEY)
	;
}

/* EntryValidateChange --
 *	Validate a proposed change to the entry widget's value if required.
 *	Call the -invalidcommand if validation fails.
 *
 * Returns:
 *	TCL_OK if the change is accepted
 *	TCL_BREAK if the change is rejected
 *	TCL_ERROR if any errors occurred
 *
 * The change will be rejected if -validatecommand returns 0,
 * or if -validatecommand or -invalidcommand modifies the value.
 */
static int
EntryValidateChange(
    Entry *entryPtr,		/* Entry that needs validation. */
    const char *newValue,	/* Potential new value of entry string */
    Tcl_Size index,			/* index of insert/delete, TCL_INDEX_NONE otherwise */
    Tcl_Size count,			/* #changed characters */
    VREASON reason)		/* Reason for change */
{
    Tcl_Interp *interp = entryPtr->core.interp;
    VMODE vmode = entryPtr->entry.validate;
    int code, change_ok;

    if ((entryPtr->entry.validateCmdObj == NULL)
	|| (entryPtr->core.flags & VALIDATING)
	|| !EntryNeedsValidation(vmode, reason))
    {
	return TCL_OK;
    }

    entryPtr->core.flags |= VALIDATING;

    /* Run -validatecommand and check return value:
     */
    code = RunValidationScript(interp, entryPtr,
	    Tcl_GetString(entryPtr->entry.validateCmdObj), "-validatecommand",
	    newValue, index, count, reason);
    if (code != TCL_OK) {
	goto done;
    }

    code = Tcl_GetBooleanFromObj(interp,Tcl_GetObjResult(interp), &change_ok);
    if (code != TCL_OK) {
	entryPtr->entry.validate = VMODE_NONE;	/* Disable validation */
	Tcl_AddErrorInfo(interp,
		"\n(validation command did not return valid boolean)");
	goto done;
    }

    /* Run the -invalidcommand if validation failed:
     */
    if (!change_ok && entryPtr->entry.invalidCmdObj != NULL) {
	code = RunValidationScript(interp, entryPtr,
		Tcl_GetString(entryPtr->entry.invalidCmdObj), "-invalidcommand",
		newValue, index, count, reason);
	if (code != TCL_OK) {
	    goto done;
	}
    }

    /* Reject the pending change if validation failed
     * or if a validation script changed the value.
     */
    if (!change_ok || (entryPtr->core.flags & VALIDATION_SET_VALUE)) {
	code = TCL_BREAK;
    }

done:
    entryPtr->core.flags &= ~(VALIDATING|VALIDATION_SET_VALUE);
    return code;
}

/* EntryRevalidate --
 *	Revalidate the current value of an entry widget,
 *	update the TTK_STATE_INVALID bit.
 *
 * Returns:
 *	TCL_OK if valid, TCL_BREAK if invalid, TCL_ERROR on error.
 */
static int EntryRevalidate(
    TCL_UNUSED(Tcl_Interp *),
    Entry *entryPtr,
    VREASON reason)
{
    int code = EntryValidateChange(
		    entryPtr, entryPtr->entry.string, -1,0, reason);

    if (code == TCL_BREAK) {
	TtkWidgetChangeState(&entryPtr->core, TTK_STATE_INVALID, 0);
    } else if (code == TCL_OK) {
	TtkWidgetChangeState(&entryPtr->core, 0, TTK_STATE_INVALID);
    }

    return code;
}

/* EntryRevalidateBG --
 *	Revalidate in the background (called from event handler).
 */
static void EntryRevalidateBG(Entry *entryPtr, VREASON reason)
{
    Tcl_Interp *interp = entryPtr->core.interp;
    VMODE vmode = entryPtr->entry.validate;

    if (EntryNeedsValidation(vmode, reason)) {
	if (EntryRevalidate(interp, entryPtr, reason) == TCL_ERROR) {
	    Tcl_BackgroundException(interp, TCL_ERROR);
	}
    }
}

/*------------------------------------------------------------------------
 * +++ Entry widget modification.
 */

/* AdjustIndex --
 *	Adjust index to account for insertion (nChars > 0)
 *	or deletion (nChars < 0) at specified index.
 */
static int AdjustIndex(int i0, int index, int nChars)
{
    if (i0 >= index) {
	i0 += nChars;
	if (i0 < index) { /* index was inside deleted range */
	    i0 = index;
	}
    }
    return i0;
}

/* AdjustIndices --
 *	Adjust all internal entry indexes to account for change.
 *	Note that insertPos, and selectFirst have "right gravity",
 *	while leftIndex (=xscroll.first) and selectLast have "left gravity".
 */
static void AdjustIndices(Entry *entryPtr, int index, int nChars)
{
    EntryPart *e = &entryPtr->entry;
    int g = nChars > 0;		/* left gravity adjustment */

    e->insertPos    = AdjustIndex(e->insertPos, index, nChars);
    e->selectFirst  = AdjustIndex(e->selectFirst, index, nChars);
    e->selectLast   = AdjustIndex(e->selectLast, index+g, nChars);
    e->xscroll.first= AdjustIndex(e->xscroll.first, index+g, nChars);

    if (e->selectLast <= e->selectFirst)
	e->selectFirst = e->selectLast = TCL_INDEX_NONE;
}

/* EntryStoreValue --
 *	Replace the contents of a text entry with a given value,
 *	recompute dependent resources, and schedule a redisplay.
 *
 *	See also: EntrySetValue().
 */
static void
EntryStoreValue(Entry *entryPtr, const char *value)
{
    size_t numBytes = strlen(value);
    Tcl_Size numChars = Tcl_NumUtfChars(value, numBytes);

    if (entryPtr->core.flags & VALIDATING)
	entryPtr->core.flags |= VALIDATION_SET_VALUE;

    /* Make sure all indices remain in bounds:
     */
    if (numChars < entryPtr->entry.numChars)
	AdjustIndices(entryPtr, numChars, numChars - entryPtr->entry.numChars);

    /* Free old value:
     */
    if (entryPtr->entry.displayString != entryPtr->entry.string)
	ckfree(entryPtr->entry.displayString);
    ckfree(entryPtr->entry.string);

    /* Store new value:
     */
    entryPtr->entry.string = (char *)ckalloc(numBytes + 1);
    strcpy(entryPtr->entry.string, value);
    entryPtr->entry.numBytes = numBytes;
    entryPtr->entry.numChars = numChars;

    entryPtr->entry.displayString
	= entryPtr->entry.showCharObj
	? EntryDisplayString(Tcl_GetString(entryPtr->entry.showCharObj), numChars)
	: entryPtr->entry.string
	;

    /* Update layout, schedule redisplay:
     */
    EntryUpdateTextLayout(entryPtr);
    TtkRedisplayWidget(&entryPtr->core);
}

/* EntrySetValue --
 *	Stores a new value in the entry widget and updates the
 *	linked -textvariable, if any.  The write trace on the
 *	text variable is temporarily disabled; however, other
 *	write traces may change the value of the variable.
 *	If so, the widget is updated again with the new value.
 *
 * Returns:
 *	TCL_OK if successful, TCL_ERROR otherwise.
 */
static int EntrySetValue(Entry *entryPtr, const char *value)
{
    EntryStoreValue(entryPtr, value);

    if (entryPtr->entry.textVariableObj) {
	const char *textVarName =
	    Tcl_GetString(entryPtr->entry.textVariableObj);
	if (textVarName && *textVarName) {
	    entryPtr->core.flags |= SYNCING_VARIABLE;
	    value = Tcl_SetVar2(entryPtr->core.interp, textVarName,
		    NULL, value, TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG);
	    entryPtr->core.flags &= ~SYNCING_VARIABLE;
	    if (!value || WidgetDestroyed(&entryPtr->core)) {
		return TCL_ERROR;
	    } else if (strcmp(value, entryPtr->entry.string) != 0) {
		/* Some write trace has changed the variable value.
		 */
		EntryStoreValue(entryPtr, value);
	    }
	}
    }

    return TCL_OK;
}

/* EntryTextVariableTrace --
 *	Variable trace procedure for entry -textvariable
 */
static void EntryTextVariableTrace(void *recordPtr, const char *value)
{
    Entry *entryPtr = (Entry *)recordPtr;

    if (WidgetDestroyed(&entryPtr->core)) {
	return;
    }

    if (entryPtr->core.flags & SYNCING_VARIABLE) {
	/* Trace was fired due to Tcl_SetVar2 call in EntrySetValue.
	 * Don't do anything.
	 */
	return;
    }

    EntryStoreValue(entryPtr, value ? value : "");
}

/*------------------------------------------------------------------------
 * +++ Insertion and deletion.
 */

/* InsertChars --
 *	Add new characters to an entry widget.
 */
static int
InsertChars(
    Entry *entryPtr,		/* Entry that is to get the new elements. */
    Tcl_Size index,			/* Insert before this index */
    Tcl_Obj *obj)			/* New characters to add */
{
    char *string = entryPtr->entry.string;
    const char *value = Tcl_GetString(obj);
    size_t byteIndex = Tcl_UtfAtIndex(string, index) - string;
    size_t byteCount = strlen(value);
    int charsAdded = Tcl_NumUtfChars(value, byteCount);
    size_t newByteCount = entryPtr->entry.numBytes + byteCount + 1;
    char *newBytes;
    int code;

    if (byteCount == 0) {
	return TCL_OK;
    }

    newBytes =  (char *)ckalloc(newByteCount);
    memcpy(newBytes, string, byteIndex);
    strcpy(newBytes + byteIndex, value);
    strcpy(newBytes + byteIndex + byteCount, string + byteIndex);

    code = EntryValidateChange(
	    entryPtr, newBytes, index, charsAdded, VALIDATE_INSERT);

    if (code == TCL_OK) {
	AdjustIndices(entryPtr, index, charsAdded);
	code = EntrySetValue(entryPtr, newBytes);
    } else if (code == TCL_BREAK) {
	code = TCL_OK;
    }

    ckfree(newBytes);
    return code;
}

/* DeleteChars --
 *	Remove one or more characters from an entry widget.
 */
static int
DeleteChars(
    Entry *entryPtr,		/* Entry widget to modify. */
    Tcl_Size index,			/* Index of first character to delete. */
    Tcl_Size count)			/* How many characters to delete. */
{
    char *string = entryPtr->entry.string;
    size_t byteIndex, byteCount, newByteCount;
    char *newBytes;
    int code;

    if (index < 0) {
	index = 0;
    }
    if (count + index  > entryPtr->entry.numChars) {
	count = entryPtr->entry.numChars - index;
    }
    if (count <= 0) {
	return TCL_OK;
    }

    byteIndex = Tcl_UtfAtIndex(string, index) - string;
    byteCount = Tcl_UtfAtIndex(string+byteIndex, count) - (string+byteIndex);

    newByteCount = entryPtr->entry.numBytes + 1 - byteCount;
    newBytes =  (char *)ckalloc(newByteCount);
    memcpy(newBytes, string, byteIndex);
    strcpy(newBytes + byteIndex, string + byteIndex + byteCount);

    code = EntryValidateChange(
	    entryPtr, newBytes, index, count, VALIDATE_DELETE);

    if (code == TCL_OK) {
	AdjustIndices(entryPtr, index, -count);
	code = EntrySetValue(entryPtr, newBytes);
    } else if (code == TCL_BREAK) {
	code = TCL_OK;
    }
    ckfree(newBytes);

    return code;
}

/*------------------------------------------------------------------------
 * +++ Event handler.
 */

/* EntryEventProc --
 *	Extra event handling for entry widgets:
 *	Triggers validation on FocusIn and FocusOut events.
 */
#define EntryEventMask (FocusChangeMask)
static void
EntryEventProc(void *clientData, XEvent *eventPtr)
{
    Entry *entryPtr = (Entry *)clientData;

    Tcl_Preserve(clientData);
    switch (eventPtr->type) {
	case DestroyNotify:
	    Tk_DeleteEventHandler(entryPtr->core.tkwin,
		    EntryEventMask, EntryEventProc, clientData);
	    break;
	case FocusIn:
	    EntryRevalidateBG(entryPtr, VALIDATE_FOCUSIN);
	    break;
	case FocusOut:
	    EntryRevalidateBG(entryPtr, VALIDATE_FOCUSOUT);
	    break;
    }
    Tcl_Release(clientData);
}

/*------------------------------------------------------------------------
 * +++ Initialization and cleanup.
 */

static void
EntryInitialize(
    TCL_UNUSED(Tcl_Interp *),
    void *recordPtr)
{
    Entry *entryPtr = (Entry *)recordPtr;

    Tk_CreateEventHandler(
	entryPtr->core.tkwin, EntryEventMask, EntryEventProc, entryPtr);
    Tk_CreateSelHandler(entryPtr->core.tkwin, XA_PRIMARY, XA_STRING,
	EntryFetchSelection, entryPtr, XA_STRING);
    TtkBlinkCursor(&entryPtr->core);

    entryPtr->entry.string		= (char *)ckalloc(1);
    *entryPtr->entry.string		= '\0';
    entryPtr->entry.displayString	= entryPtr->entry.string;
    entryPtr->entry.textVariableTrace	= 0;
    entryPtr->entry.numBytes = entryPtr->entry.numChars = 0;

    EntryInitStyleDefaults(&entryPtr->entry.styleDefaults);

    entryPtr->entry.xscrollHandle =
	TtkCreateScrollHandle(&entryPtr->core, &entryPtr->entry.xscroll);

    entryPtr->entry.insertPos		= 0;
    entryPtr->entry.selectFirst	= TCL_INDEX_NONE;
    entryPtr->entry.selectLast		= TCL_INDEX_NONE;
}

static void
EntryCleanup(void *recordPtr)
{
    Entry *entryPtr = (Entry *)recordPtr;

    if (entryPtr->entry.textVariableTrace)
	Ttk_UntraceVariable(entryPtr->entry.textVariableTrace);

    TtkFreeScrollHandle(entryPtr->entry.xscrollHandle);

    EntryFreeStyleDefaults(&entryPtr->entry.styleDefaults);

    Tk_DeleteSelHandler(entryPtr->core.tkwin, XA_PRIMARY, XA_STRING);

    Tk_FreeTextLayout(entryPtr->entry.textLayout);
    if (entryPtr->entry.displayString != entryPtr->entry.string)
	ckfree(entryPtr->entry.displayString);
    ckfree(entryPtr->entry.string);
}

/* EntryConfigure --
 *	Configure hook for Entry widgets.
 */
static int EntryConfigure(Tcl_Interp *interp, void *recordPtr, int mask)
{
    Entry *entryPtr = (Entry *)recordPtr;
    Tcl_Obj *textVarName = entryPtr->entry.textVariableObj;
    Ttk_TraceHandle *vt = 0;

    if (mask & TEXTVAR_CHANGED) {
	if (textVarName && *Tcl_GetString(textVarName) != '\0') {
	    vt = Ttk_TraceVariable(interp,
		    textVarName,EntryTextVariableTrace,entryPtr);
	    if (!vt) return TCL_ERROR;
	}
    }

    if (TtkCoreConfigure(interp, recordPtr, mask) != TCL_OK) {
	if (vt) Ttk_UntraceVariable(vt);
	return TCL_ERROR;
    }

    /* Update derived resources:
     */
    if (mask & TEXTVAR_CHANGED) {
	if (entryPtr->entry.textVariableTrace)
	    Ttk_UntraceVariable(entryPtr->entry.textVariableTrace);
	entryPtr->entry.textVariableTrace = vt;
    }

    /* Claim the selection, in case we've suddenly started exporting it.
     */
    if (entryPtr->entry.exportSelection && (entryPtr->entry.selectFirst >= 0)
	    && (!Tcl_IsSafe(entryPtr->core.interp))) {
	EntryOwnSelection(entryPtr);
    }

    /* Handle -state compatibility option:
     */
    if (mask & STATE_CHANGED) {
	TtkCheckStateOption(&entryPtr->core, entryPtr->entry.stateObj);
    }

    /* Force scrollbar update if needed:
     */
    if (mask & SCROLLCMD_CHANGED) {
	TtkScrollbarUpdateRequired(entryPtr->entry.xscrollHandle);
    }

    /* Recompute the displayString, in case showChar changed:
     */
    if (entryPtr->entry.displayString != entryPtr->entry.string)
	ckfree(entryPtr->entry.displayString);

    entryPtr->entry.displayString
	= entryPtr->entry.showCharObj
	? EntryDisplayString(Tcl_GetString(entryPtr->entry.showCharObj), entryPtr->entry.numChars)
	: entryPtr->entry.string
	;

    /* Update textLayout:
     */
    EntryUpdateTextLayout(entryPtr);
    return TCL_OK;
}

/* EntryPostConfigure --
 *	Post-configuration hook for entry widgets.
 */
static int EntryPostConfigure(
    TCL_UNUSED(Tcl_Interp *),
    void *recordPtr,
    int mask)
{
    Entry *entryPtr = (Entry *)recordPtr;
    int status = TCL_OK;

    if ((mask & TEXTVAR_CHANGED) && entryPtr->entry.textVariableTrace != NULL) {
	status = Ttk_FireTrace(entryPtr->entry.textVariableTrace);
    }

    return status;
}

/*------------------------------------------------------------------------
 * +++ Layout and display.
 */

/* EntryCharPosition --
 *	Return the X coordinate of the specified character index.
 *	Precondition: textLayout and layoutX up-to-date.
 */
static int
EntryCharPosition(Entry *entryPtr, Tcl_Size index)
{
    int xPos;
    Tk_CharBbox(entryPtr->entry.textLayout, index, &xPos, NULL, NULL, NULL);
    return xPos + entryPtr->entry.layoutX;
}

/* EntryDoLayout --
 *	Layout hook for entry widgets.
 *
 *	Determine position of textLayout based on xscroll.first, justify,
 *	and display area.
 *
 *	Recalculates layoutX, layoutY, and rightIndex,
 *	and updates xscroll accordingly.
 *	May adjust xscroll.first to ensure the maximum #characters are onscreen.
 */
static void
EntryDoLayout(void *recordPtr)
{
    Entry *entryPtr = (Entry *)recordPtr;
    WidgetCore *corePtr = &entryPtr->core;
    Tk_TextLayout textLayout = entryPtr->entry.textLayout;
    int leftIndex = entryPtr->entry.xscroll.first;
    int rightIndex;
    Ttk_Box textarea;

    Ttk_PlaceLayout(corePtr->layout,corePtr->state,Ttk_WinBox(corePtr->tkwin));
    textarea = Ttk_ClientRegion(corePtr->layout, "textarea");

    /* Center the text vertically within the available parcel:
     */
    entryPtr->entry.layoutY = textarea.y +
	(textarea.height - entryPtr->entry.layoutHeight)/2;

    /* Recompute where the leftmost character on the display will
     * be drawn (layoutX) and adjust leftIndex if necessary.
     */
    if (entryPtr->entry.layoutWidth <= textarea.width) {
	/* Everything fits.  Set leftIndex to zero (no need to scroll),
	 * and compute layoutX based on -justify.
	 */
	int extraSpace = textarea.width - entryPtr->entry.layoutWidth;
	leftIndex = 0;
	rightIndex = entryPtr->entry.numChars;
	entryPtr->entry.layoutX = textarea.x;
	if (entryPtr->entry.justify == TK_JUSTIFY_RIGHT) {
	    entryPtr->entry.layoutX += extraSpace;
	} else if (entryPtr->entry.justify == TK_JUSTIFY_CENTER) {
	    entryPtr->entry.layoutX += extraSpace / 2;
	}
    } else {
	/* The whole string doesn't fit in the window.
	 * Limit leftIndex to leave at most one character's worth
	 * of empty space on the right.
	 */
	int overflow = entryPtr->entry.layoutWidth - textarea.width;
	int maxLeftIndex = 1 + Tk_PointToChar(textLayout, overflow, 0);
	int leftX;

	if (leftIndex > maxLeftIndex) {
	    leftIndex = maxLeftIndex;
	}

	/* Compute layoutX and rightIndex.
	 * rightIndex is set to one past the last fully-visible character.
	 */
	Tk_CharBbox(textLayout, leftIndex, &leftX, NULL, NULL, NULL);
	rightIndex = Tk_PointToChar(textLayout, leftX + textarea.width, 0);
	entryPtr->entry.layoutX = textarea.x - leftX;
    }

    TtkScrolled(entryPtr->entry.xscrollHandle,
	    leftIndex, rightIndex, entryPtr->entry.numChars);
}

/* EntryGetGC -- Helper routine.
 *      Get a GC using the specified foreground color and the entry's font.
 *      Result must be freed with Tk_FreeGC().
 */
static GC EntryGetGC(Entry *entryPtr, Tcl_Obj *colorObj, TkRegion clip)
{
    Tk_Window tkwin = entryPtr->core.tkwin;
    Tk_Font font = Tk_GetFontFromObj(tkwin, entryPtr->entry.fontObj);
    XColor *colorPtr;
    unsigned long mask = 0ul;
    XGCValues gcValues;
    GC gc;

    gcValues.line_width = 1; mask |= GCLineWidth;
    gcValues.font = Tk_FontId(font); mask |= GCFont;
    if (colorObj != 0 && (colorPtr=Tk_GetColorFromObj(tkwin,colorObj)) != 0) {
	gcValues.foreground = colorPtr->pixel;
	mask |= GCForeground;
    }
    gc = Tk_GetGC(entryPtr->core.tkwin, mask, &gcValues);
    if (clip != NULL) {
	TkSetRegion(Tk_Display(entryPtr->core.tkwin), gc, clip);
    }
    return gc;
}

/* EntryDisplay --
 *	Redraws the contents of an entry window.
 */
static void EntryDisplay(void *clientData, Drawable d)
{
    Entry *entryPtr = (Entry *)clientData;
    Tk_Window tkwin = entryPtr->core.tkwin;
    Tcl_Size leftIndex = entryPtr->entry.xscroll.first,
	rightIndex = entryPtr->entry.xscroll.last + 1,
	selFirst = entryPtr->entry.selectFirst,
	selLast = entryPtr->entry.selectLast;
    EntryStyleData es;
    GC gc;
    int showSelection, showCursor;
    Ttk_Box textarea;
    TkRegion clipRegion;
    XRectangle rect;
    Tcl_Obj *foregroundObj;

    EntryInitStyleData(entryPtr, &es);

    textarea = Ttk_ClientRegion(entryPtr->core.layout, "textarea");
    showCursor =
	   (entryPtr->core.flags & CURSOR_ON)
	&& EntryEditable(entryPtr)
	&& entryPtr->entry.insertPos >= leftIndex
	&& entryPtr->entry.insertPos <= rightIndex
	;
    showSelection =
	   !(entryPtr->core.state & TTK_STATE_DISABLED)
	&& selFirst >= 0
	&& selLast > leftIndex
	&& selFirst <= rightIndex;

    /* Adjust selection range to keep in display bounds.
     */
    if (showSelection) {
	if (selFirst < leftIndex)
	    selFirst = leftIndex;
	if (selLast > rightIndex)
	    selLast = rightIndex;
    }

    /* Draw widget background & border
     */
    Ttk_DrawLayout(entryPtr->core.layout, entryPtr->core.state, d);

    /* Draw selection background
     */
    if (showSelection && es.selBorderObj) {
	Tk_3DBorder selBorder = Tk_Get3DBorderFromObj(tkwin, es.selBorderObj);
	int selStartX = EntryCharPosition(entryPtr, selFirst);
	int selEndX = EntryCharPosition(entryPtr, selLast);
	int borderWidth = 0;

	Tk_GetPixelsFromObj(NULL, tkwin, es.selBorderWidthObj, &borderWidth);

	if (selBorder) {
	    int selWidth;
	    int textareaEnd = textarea.x + textarea.width;
	    if (selEndX > textareaEnd)
		selEndX = textareaEnd;
	    selWidth = selEndX - selStartX + 2 * borderWidth;
	    if (selWidth > 0)
		Tk_Fill3DRectangle(tkwin, d, selBorder,
		selStartX - borderWidth, entryPtr->entry.layoutY - borderWidth,
		selWidth, entryPtr->entry.layoutHeight + 2*borderWidth,
		borderWidth, TK_RELIEF_RAISED);
	}
    }

    /* Initialize the clip region. Note that Xft does _not_ derive its
     * clipping area from the GC, so we have to supply that by other means.
     */

    rect.x = textarea.x;
    rect.y = textarea.y;
    rect.width = textarea.width;
    rect.height = textarea.height;
    clipRegion = TkCreateRegion();
    TkUnionRectWithRegion(&rect, clipRegion, clipRegion);
#ifdef HAVE_XFT
    TkUnixSetXftClipRegion(clipRegion);
#endif

    /* Draw cursor:
     */
    if (showCursor) {
	Ttk_Box field = Ttk_ClientRegion(entryPtr->core.layout, "field");
	int cursorX = EntryCharPosition(entryPtr, entryPtr->entry.insertPos),
	    cursorY = entryPtr->entry.layoutY,
	    cursorHeight = entryPtr->entry.layoutHeight,
	    cursorWidth = 1;

	Tk_GetPixelsFromObj(NULL, tkwin, es.insertWidthObj, &cursorWidth);
	if (cursorWidth <= 0) {
	    cursorWidth = 1;
	}

	/* @@@ should: maybe: SetCaretPos even when blinked off */
	Tk_SetCaretPos(tkwin, cursorX, cursorY, cursorHeight);

	cursorX -= cursorWidth/2;
	if (cursorX < field.x) {
	    cursorX = field.x;
	} else if (cursorX + cursorWidth > field.x + field.width) {
	    cursorX = field.x + field.width - cursorWidth;
	}

	gc = EntryGetGC(entryPtr, es.insertColorObj, NULL);
	XFillRectangle(Tk_Display(tkwin), d, gc,
	    cursorX, cursorY, cursorWidth, cursorHeight);
	Tk_FreeGC(Tk_Display(tkwin), gc);
    }

    /* Draw the text:
     */
    if ((*(entryPtr->entry.displayString) == '\0')
		&& (entryPtr->entry.placeholderObj != NULL)) {
	/* No text displayed, but -placeholder is given */
	if (Tcl_GetCharLength(es.placeholderForegroundObj) > 0) {
	    foregroundObj = es.placeholderForegroundObj;
	} else {
	    foregroundObj = es.foregroundObj;
	}
	/* Use placeholder text width */
	leftIndex = 0;
	(void)Tcl_GetStringFromObj(entryPtr->entry.placeholderObj, &rightIndex);
    } else {
	foregroundObj = es.foregroundObj;
    }
    gc = EntryGetGC(entryPtr, foregroundObj, clipRegion);
    if (showSelection) {

	/* Draw the selected and unselected portions separately.
	 */
	if (leftIndex < selFirst) {
	    Tk_DrawTextLayout(
		Tk_Display(tkwin), d, gc, entryPtr->entry.textLayout,
		entryPtr->entry.layoutX, entryPtr->entry.layoutY,
		leftIndex, selFirst);
	}
	if (selLast < rightIndex) {
	    Tk_DrawTextLayout(
		Tk_Display(tkwin), d, gc, entryPtr->entry.textLayout,
		entryPtr->entry.layoutX, entryPtr->entry.layoutY,
		selLast, rightIndex);
	}
	XSetClipMask(Tk_Display(tkwin), gc, None);
	Tk_FreeGC(Tk_Display(tkwin), gc);

	/* Draw the selected portion in the -selectforeground color:
	 */
	gc = EntryGetGC(entryPtr, es.selForegroundObj, clipRegion);
	Tk_DrawTextLayout(
	    Tk_Display(tkwin), d, gc, entryPtr->entry.textLayout,
	    entryPtr->entry.layoutX, entryPtr->entry.layoutY,
	    selFirst, selLast);
	XSetClipMask(Tk_Display(tkwin), gc, None);
	Tk_FreeGC(Tk_Display(tkwin), gc);
    } else {

	/* Draw the entire visible text
	 */
	Tk_DrawTextLayout(
	    Tk_Display(tkwin), d, gc, entryPtr->entry.textLayout,
	    entryPtr->entry.layoutX, entryPtr->entry.layoutY,
	    leftIndex, rightIndex);
	XSetClipMask(Tk_Display(tkwin), gc, None);
	Tk_FreeGC(Tk_Display(tkwin), gc);
    }

    /* Drop the region. Note that we have to manually remove the reference to
     * it from the Xft guts (if they're being used).
     */
#ifdef HAVE_XFT
    TkUnixSetXftClipRegion(NULL);
#endif
    TkDestroyRegion(clipRegion);
}

/*------------------------------------------------------------------------
 * +++ Widget commands.
 */

/* EntryIndex --
 *	Parse an index into an entry and return either its value
 *	or an error.
 *
 * Results:
 *	A standard Tcl result.  If all went well, then *indexPtr is
 *	filled in with the character index (into entryPtr) corresponding to
 *	string.  The index value is guaranteed to lie between 0 and
 *	the number of characters in the string, inclusive.  If an
 *	error occurs then an error message is left in the interp's result.
 */
static int
EntryIndex(
    Tcl_Interp *interp,		/* For error messages. */
    Entry *entryPtr,		/* Entry widget to query */
    Tcl_Obj *indexObj,		/* Symbolic index name */
    Tcl_Size *indexPtr)		/* Return value */
{
#   define EntryWidth(e) (Tk_Width(entryPtr->core.tkwin)) /* Not Right */
    Tcl_Size length, idx;
    const char *string;

    if (TCL_OK == TkGetIntForIndex(indexObj, entryPtr->entry.numChars - 1, 1, &idx)) {
	if (idx < 0) {
	    idx = 0;
	} else if (idx > entryPtr->entry.numChars) {
	    idx = entryPtr->entry.numChars;
	}
	*indexPtr = idx;
	return TCL_OK;
    }

    string = Tcl_GetStringFromObj(indexObj, &length);

    if (strncmp(string, "insert", length) == 0) {
	*indexPtr = entryPtr->entry.insertPos;
    } else if (strncmp(string, "left", length) == 0) {	/* for debugging */
	*indexPtr = entryPtr->entry.xscroll.first;
    } else if (strncmp(string, "right", length) == 0) {	/* for debugging */
	*indexPtr = entryPtr->entry.xscroll.last;
    } else if (strncmp(string, "sel.", 4) == 0) {
	if (entryPtr->entry.selectFirst < 0) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "selection isn't in widget %s",
		    Tk_PathName(entryPtr->core.tkwin)));
	    Tcl_SetErrorCode(interp, "TTK", "ENTRY", "NO_SELECTION", (char *)NULL);
	    return TCL_ERROR;
	}
	if (strncmp(string, "sel.first", length) == 0) {
	    *indexPtr = entryPtr->entry.selectFirst;
	} else if (strncmp(string, "sel.last", length) == 0) {
	    *indexPtr = entryPtr->entry.selectLast;
	} else {
	    goto badIndex;
	}
    } else if (string[0] == '@') {
	int roundUp = 0;
	int maxWidth = EntryWidth(entryPtr);
	int x;

	if (Tcl_GetInt(interp, string + 1, &x) != TCL_OK) {
	    goto badIndex;
	}
	if (x > maxWidth) {
	    x = maxWidth;
	    roundUp = 1;
	}
	*indexPtr = Tk_PointToChar(entryPtr->entry.textLayout,
		x - entryPtr->entry.layoutX, 0);

	TtkUpdateScrollInfo(entryPtr->entry.xscrollHandle);
	if (*indexPtr < entryPtr->entry.xscroll.first) {
	    *indexPtr = entryPtr->entry.xscroll.first;
	}

	/*
	 * Special trick:  if the x-position was off-screen to the right,
	 * round the index up to refer to the character just after the
	 * last visible one on the screen.  This is needed to enable the
	 * last character to be selected, for example.
	 */

	if (roundUp && (*indexPtr < entryPtr->entry.numChars)) {
	    *indexPtr += 1;
	}
    } else {
	goto badIndex;
    }
    return TCL_OK;

badIndex:
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "bad entry index \"%s\"", string));
    Tcl_SetErrorCode(interp, "TTK", "ENTRY", "INDEX", (char *)NULL);
    return TCL_ERROR;
}

/* $entry bbox $index --
 *	Return the bounding box of the character at the specified index.
 */
static int
EntryBBoxCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Entry *entryPtr = (Entry *)recordPtr;
    Ttk_Box b;
    Tcl_Size index;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "index");
	return TCL_ERROR;
    }
    if (EntryIndex(interp, entryPtr, objv[2], &index) != TCL_OK) {
	return TCL_ERROR;
    }
    if ((index == entryPtr->entry.numChars) && (index > 0)) {
	index--;
    }
    Tk_CharBbox(entryPtr->entry.textLayout, index,
	    &b.x, &b.y, &b.width, &b.height);
    b.x += entryPtr->entry.layoutX;
    b.y += entryPtr->entry.layoutY;
    Tcl_SetObjResult(interp, Ttk_NewBoxObj(b));
    return TCL_OK;
}

/* $entry delete $from ?$to? --
 *	Delete the characters in the range [$from,$to).
 *	$to defaults to $from+1 if not specified.
 */
static int
EntryDeleteCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Entry *entryPtr = (Entry *)recordPtr;
    Tcl_Size first, last;

    if ((objc < 3) || (objc > 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "firstIndex ?lastIndex?");
	return TCL_ERROR;
    }
    if (EntryIndex(interp, entryPtr, objv[2], &first) != TCL_OK) {
	return TCL_ERROR;
    }
    if (objc == 3) {
	last = first + 1;
    } else if (EntryIndex(interp, entryPtr, objv[3], &last) != TCL_OK) {
	return TCL_ERROR;
    }

    if (last >= first && EntryEditable(entryPtr)) {
	return DeleteChars(entryPtr, first, last - first);
    }
    return TCL_OK;
}

/* $entry get --
 *	Return the current value of the entry widget.
 */
static int
EntryGetCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Entry *entryPtr = (Entry *)recordPtr;
    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 2, objv, NULL);
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(entryPtr->entry.string, -1));
    return TCL_OK;
}

/* $entry icursor $index --
 *	Set the insert cursor position.
 */
static int
EntryICursorCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Entry *entryPtr = (Entry *)recordPtr;
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "pos");
	return TCL_ERROR;
    }
    if (EntryIndex(interp, entryPtr, objv[2],
	    &entryPtr->entry.insertPos) != TCL_OK) {
	return TCL_ERROR;
    }
    TtkRedisplayWidget(&entryPtr->core);
    return TCL_OK;
}

/* $entry index $index --
 *	Return numeric value (0..numChars) of the specified index.
 */
static int
EntryIndexCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Entry *entryPtr = (Entry *)recordPtr;
    Tcl_Size index;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "string");
	return TCL_ERROR;
    }
    if (EntryIndex(interp, entryPtr, objv[2], &index) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, TkNewIndexObj(index));
    return TCL_OK;
}

/* $entry insert $index $text --
 *	Insert $text after position $index.
 *	Silent no-op if the entry is disabled or read-only.
 */
static int
EntryInsertCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Entry *entryPtr = (Entry *)recordPtr;
    Tcl_Size index;

    if (objc != 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "index text");
	return TCL_ERROR;
    }
    if (EntryIndex(interp, entryPtr, objv[2], &index) != TCL_OK) {
	return TCL_ERROR;
    }
    if (EntryEditable(entryPtr)) {
	return InsertChars(entryPtr, index, objv[3]);
    }
    return TCL_OK;
}

/* $entry selection clear --
 *	Clear selection.
 */
static int EntrySelectionClearCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Entry *entryPtr = (Entry *)recordPtr;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 3, objv, NULL);
	return TCL_ERROR;
    }
    entryPtr->entry.selectFirst = entryPtr->entry.selectLast = TCL_INDEX_NONE;
    TtkRedisplayWidget(&entryPtr->core);
    return TCL_OK;
}

/* $entry selection present --
 *	Returns 1 if any characters are selected, 0 otherwise.
 */
static int EntrySelectionPresentCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Entry *entryPtr = (Entry *)recordPtr;
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 3, objv, NULL);
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp,
	    Tcl_NewBooleanObj(entryPtr->entry.selectFirst >= 0));
    return TCL_OK;
}

/* $entry selection range $start $end --
 *	Explicitly set the selection range.
 */
static int EntrySelectionRangeCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Entry *entryPtr = (Entry *)recordPtr;
    Tcl_Size start, end;
    if (objc != 5) {
	Tcl_WrongNumArgs(interp, 3, objv, "start end");
	return TCL_ERROR;
    }
    if (EntryIndex(interp, entryPtr, objv[3], &start) != TCL_OK
	 || EntryIndex(interp, entryPtr, objv[4], &end) != TCL_OK) {
	return TCL_ERROR;
    }
    if (entryPtr->core.state & TTK_STATE_DISABLED) {
	return TCL_OK;
    }

    if (start >= end) {
	entryPtr->entry.selectFirst = entryPtr->entry.selectLast = TCL_INDEX_NONE;
    } else {
	entryPtr->entry.selectFirst = start;
	entryPtr->entry.selectLast = end;
	EntryOwnSelection(entryPtr);
    }
    TtkRedisplayWidget(&entryPtr->core);
    return TCL_OK;
}

static const Ttk_Ensemble EntrySelectionCommands[] = {
    { "clear",   EntrySelectionClearCommand,0 },
    { "present", EntrySelectionPresentCommand,0 },
    { "range",   EntrySelectionRangeCommand,0 },
    { 0,0,0 }
};

/* $entry set $value
 *	Sets the value of an entry widget.
 */
static int EntrySetCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Entry *entryPtr = (Entry *)recordPtr;
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "value");
	return TCL_ERROR;
    }
    EntrySetValue(entryPtr, Tcl_GetString(objv[2]));
    return TCL_OK;
}

/* $entry validate --
 *	Trigger forced validation.  Returns 1/0 if validation succeeds/fails
 *	or error status from -validatecommand / -invalidcommand.
 */
static int EntryValidateCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Entry *entryPtr = (Entry *)recordPtr;
    int code;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 2, objv, NULL);
	return TCL_ERROR;
    }

    code = EntryRevalidate(interp, entryPtr, VALIDATE_FORCED);

    if (code == TCL_ERROR)
	return code;

    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(code == TCL_OK));
    return TCL_OK;
}

/* $entry xview	-- horizontal scrolling interface
 */
static int EntryXViewCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Entry *entryPtr = (Entry *)recordPtr;
    if (objc == 3) {
	Tcl_Size newFirst;
	if (EntryIndex(interp, entryPtr, objv[2], &newFirst) != TCL_OK) {
	    return TCL_ERROR;
	}
	TtkScrollTo(entryPtr->entry.xscrollHandle, newFirst, 1);
	return TCL_OK;
    }
    return TtkScrollviewCommand(interp, objc, objv, entryPtr->entry.xscrollHandle);
}

static const Ttk_Ensemble EntryCommands[] = {
    { "bbox",		EntryBBoxCommand,0 },
    { "cget",		TtkWidgetCgetCommand,0 },
    { "configure",	TtkWidgetConfigureCommand,0 },
    { "delete",	EntryDeleteCommand,0 },
    { "get",		EntryGetCommand,0 },
    { "icursor",	EntryICursorCommand,0 },
    { "identify",	TtkWidgetIdentifyCommand,0 },
    { "index",		EntryIndexCommand,0 },
    { "insert",	EntryInsertCommand,0 },
    { "instate",	TtkWidgetInstateCommand,0 },
    { "selection",	0,EntrySelectionCommands },
    { "state",	TtkWidgetStateCommand,0 },
    { "style",		TtkWidgetStyleCommand,0 },
    { "validate",	EntryValidateCommand,0 },
    { "xview",		EntryXViewCommand,0 },
    { 0,0,0 }
};

/*------------------------------------------------------------------------
 * +++ Entry widget definition.
 */

static const WidgetSpec EntryWidgetSpec = {
    "TEntry",			/* className */
    sizeof(Entry),		/* recordSize */
    EntryOptionSpecs,		/* optionSpecs */
    EntryCommands,		/* subcommands */
    EntryInitialize,	/* initializeProc */
    EntryCleanup,		/* cleanupProc */
    EntryConfigure,		/* configureProc */
    EntryPostConfigure,	/* postConfigureProc */
    TtkWidgetGetLayout,	/* getLayoutProc */
    TtkWidgetSize,		/* sizeProc */
    EntryDoLayout,		/* layoutProc */
    EntryDisplay		/* displayProc */
};

/*------------------------------------------------------------------------
 * +++ Combobox widget record.
 */

typedef struct {
    Tcl_Obj	*postCommandObj;
    Tcl_Obj	*valuesObj;
    Tcl_Obj	*heightObj;
    Tcl_Size	currentIndex;
} ComboboxPart;

typedef struct {
    WidgetCore core;
    EntryPart entry;
    ComboboxPart combobox;
} Combobox;

static const Tk_OptionSpec ComboboxOptionSpecs[] = {
    {TK_OPTION_STRING, "-height", "height", "Height",
	DEF_LIST_HEIGHT, offsetof(Combobox, combobox.heightObj), TCL_INDEX_NONE,
	0,0,0 },
    {TK_OPTION_STRING, "-postcommand", "postCommand", "PostCommand",
	"", offsetof(Combobox, combobox.postCommandObj), TCL_INDEX_NONE,
	0,0,0 },
    {TK_OPTION_STRING, "-values", "values", "Values",
	"", offsetof(Combobox, combobox.valuesObj), TCL_INDEX_NONE,
	0,0,0 },
    WIDGET_INHERIT_OPTIONS(EntryOptionSpecs)
};

/* ComboboxInitialize --
 *	Initialization hook for combobox widgets.
 */
static void
ComboboxInitialize(Tcl_Interp *interp, void *recordPtr)
{
    Combobox *cb = (Combobox *)recordPtr;

    cb->combobox.currentIndex = TCL_INDEX_NONE;
    TtkTrackElementState(&cb->core);
    EntryInitialize(interp, recordPtr);
}

/* ComboboxConfigure --
 *	Configuration hook for combobox widgets.
 */
static int
ComboboxConfigure(Tcl_Interp *interp, void *recordPtr, int mask)
{
    Combobox *cbPtr = (Combobox *)recordPtr;
    Tcl_Size unused;

    /* Make sure -values is a valid list:
     */
    if (Tcl_ListObjLength(interp,cbPtr->combobox.valuesObj,&unused) != TCL_OK)
	return TCL_ERROR;

    return EntryConfigure(interp, recordPtr, mask);
}

/* $cb current ?newIndex? -- get or set current index.
 *	Setting the current index updates the combobox value,
 *	but the value and -values may be changed independently
 *	of the index.  Instead of trying to keep currentIndex
 *	in sync at all times, [$cb current] double-checks
 */
static int ComboboxCurrentCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Combobox *cbPtr = (Combobox *)recordPtr;
    Tcl_Size currentIndex = cbPtr->combobox.currentIndex;
    const char *currentValue = cbPtr->entry.string;
    Tcl_Size nValues;
    Tcl_Obj **values;

    Tcl_ListObjGetElements(interp, cbPtr->combobox.valuesObj, &nValues, &values);

    if (objc == 2) {
	/* Check if currentIndex still valid:
	 */
	if (currentIndex < 0
	     || currentIndex >= nValues
	     || strcmp(currentValue,Tcl_GetString(values[currentIndex]))
	   )
	{
	    /* Not valid.  Check current value against each element in -values:
	     */
	    for (currentIndex = 0; currentIndex < nValues; ++currentIndex) {
		if (!strcmp(currentValue,Tcl_GetString(values[currentIndex]))) {
		    break;
		}
	    }
	    if (currentIndex >= nValues) {
		/* Not found */
		currentIndex = TCL_INDEX_NONE;
	    }
	}
	cbPtr->combobox.currentIndex = currentIndex;
	Tcl_SetObjResult(interp, TkNewIndexObj(currentIndex));
	return TCL_OK;
    } else if (objc == 3) {
	Tcl_Size idx;

	if (TCL_OK == TkGetIntForIndex(objv[2], nValues - 1, 0, &idx)) {
	    if (idx < 0 || idx >= nValues) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"index \"%s\" out of range", Tcl_GetString(objv[2])));
		Tcl_SetErrorCode(interp, "TTK", "COMBOBOX", "IDX_RANGE", (char *)NULL);
		return TCL_ERROR;
	    }
	    currentIndex = idx;
	} else {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "bad index \"%s\"", Tcl_GetString(objv[2])));
	    Tcl_SetErrorCode(interp, "TTK", "COMBOBOX", "IDX_VALUE", (char *)NULL);
	    return TCL_ERROR;
	}

	cbPtr->combobox.currentIndex = currentIndex;

	return EntrySetValue((Entry *)recordPtr, Tcl_GetString(values[currentIndex]));
    } else {
	Tcl_WrongNumArgs(interp, 2, objv, "?newIndex?");
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*------------------------------------------------------------------------
 * +++ Combobox widget definition.
 */
static const Ttk_Ensemble ComboboxCommands[] = {
    { "bbox",		EntryBBoxCommand,0 },
    { "cget",		TtkWidgetCgetCommand,0 },
    { "configure",	TtkWidgetConfigureCommand,0 },
    { "current",	ComboboxCurrentCommand,0 },
    { "delete",	EntryDeleteCommand,0 },
    { "get",		EntryGetCommand,0 },
    { "icursor",	EntryICursorCommand,0 },
    { "identify",	TtkWidgetIdentifyCommand,0 },
    { "index",		EntryIndexCommand,0 },
    { "insert",	EntryInsertCommand,0 },
    { "instate",	TtkWidgetInstateCommand,0 },
    { "selection",	0,EntrySelectionCommands },
    { "set",		EntrySetCommand,0 },
    { "state",	TtkWidgetStateCommand,0 },
    { "style",		TtkWidgetStyleCommand,0 },
    { "validate",	EntryValidateCommand,0 },
    { "xview",		EntryXViewCommand,0 },
    { 0,0,0 }
};

static const WidgetSpec ComboboxWidgetSpec = {
    "TCombobox",		/* className */
    sizeof(Combobox),		/* recordSize */
    ComboboxOptionSpecs,	/* optionSpecs */
    ComboboxCommands,		/* subcommands */
    ComboboxInitialize,	/* initializeProc */
    EntryCleanup,		/* cleanupProc */
    ComboboxConfigure,		/* configureProc */
    EntryPostConfigure,	/* postConfigureProc */
    TtkWidgetGetLayout,	/* getLayoutProc */
    TtkWidgetSize,		/* sizeProc */
    EntryDoLayout,		/* layoutProc */
    EntryDisplay		/* displayProc */
};

/*------------------------------------------------------------------------
 * +++ Spinbox widget.
 */

typedef struct {
    Tcl_Obj	*valuesObj;

    Tcl_Obj	*fromObj;
    Tcl_Obj	*toObj;
    Tcl_Obj	*incrementObj;
    Tcl_Obj	*formatObj;

    Tcl_Obj	*wrapObj;
    Tcl_Obj	*commandObj;
} SpinboxPart;

typedef struct {
    WidgetCore core;
    EntryPart entry;
    SpinboxPart spinbox;
} Spinbox;

static const Tk_OptionSpec SpinboxOptionSpecs[] = {
    {TK_OPTION_STRING, "-values", "values", "Values",
	"", offsetof(Spinbox, spinbox.valuesObj), TCL_INDEX_NONE,
	0,0,0 },

    {TK_OPTION_DOUBLE, "-from", "from", "From",
	"0.0", offsetof(Spinbox,spinbox.fromObj), TCL_INDEX_NONE,
	0,0,0 },
    {TK_OPTION_DOUBLE, "-to", "to", "To",
	"0.0", offsetof(Spinbox,spinbox.toObj), TCL_INDEX_NONE,
	0,0,0 },
    {TK_OPTION_DOUBLE, "-increment", "increment", "Increment",
	"1.0", offsetof(Spinbox,spinbox.incrementObj), TCL_INDEX_NONE,
	0,0,0 },
    {TK_OPTION_STRING, "-format", "format", "Format",
	"", offsetof(Spinbox, spinbox.formatObj), TCL_INDEX_NONE,
	0,0,0 },

    {TK_OPTION_STRING, "-command", "command", "Command",
	"", offsetof(Spinbox, spinbox.commandObj), TCL_INDEX_NONE,
	0,0,0 },
    {TK_OPTION_BOOLEAN, "-wrap", "wrap", "Wrap",
	"0", offsetof(Spinbox,spinbox.wrapObj), TCL_INDEX_NONE,
	0,0,0 },

    WIDGET_INHERIT_OPTIONS(EntryOptionSpecs)
};

/* SpinboxInitialize --
 *	Initialization hook for spinbox widgets.
 */
static void
SpinboxInitialize(Tcl_Interp *interp, void *recordPtr)
{
    Spinbox *sb = (Spinbox *)recordPtr;
    TtkTrackElementState(&sb->core);
    EntryInitialize(interp, recordPtr);
}

/* SpinboxConfigure --
 *	Configuration hook for spinbox widgets.
 */
static int
SpinboxConfigure(Tcl_Interp *interp, void *recordPtr, int mask)
{
    Spinbox *sb = (Spinbox *)recordPtr;
    Tcl_Size unused;

    /* Make sure -values is a valid list:
     */
    if (Tcl_ListObjLength(interp,sb->spinbox.valuesObj,&unused) != TCL_OK)
	return TCL_ERROR;

    return EntryConfigure(interp, recordPtr, mask);
}

static const Ttk_Ensemble SpinboxCommands[] = {
    { "bbox",		EntryBBoxCommand,0 },
    { "cget",		TtkWidgetCgetCommand,0 },
    { "configure",	TtkWidgetConfigureCommand,0 },
    { "delete",	EntryDeleteCommand,0 },
    { "get",		EntryGetCommand,0 },
    { "icursor",	EntryICursorCommand,0 },
    { "identify",	TtkWidgetIdentifyCommand,0 },
    { "index",		EntryIndexCommand,0 },
    { "insert",	EntryInsertCommand,0 },
    { "instate",	TtkWidgetInstateCommand,0 },
    { "selection",	0,EntrySelectionCommands },
    { "set",		EntrySetCommand,0 },
    { "state",	TtkWidgetStateCommand,0 },
    { "style",		TtkWidgetStyleCommand,0 },
    { "validate",	EntryValidateCommand,0 },
    { "xview",		EntryXViewCommand,0 },
    { 0,0,0 }
};

static const WidgetSpec SpinboxWidgetSpec = {
    "TSpinbox",			/* className */
    sizeof(Spinbox),		/* recordSize */
    SpinboxOptionSpecs,		/* optionSpecs */
    SpinboxCommands,		/* subcommands */
    SpinboxInitialize,	/* initializeProc */
    EntryCleanup,		/* cleanupProc */
    SpinboxConfigure,		/* configureProc */
    EntryPostConfigure,	/* postConfigureProc */
    TtkWidgetGetLayout,	/* getLayoutProc */
    TtkWidgetSize,		/* sizeProc */
    EntryDoLayout,		/* layoutProc */
    EntryDisplay		/* displayProc */
};

/*------------------------------------------------------------------------
 * +++ Textarea element.
 *
 * Text display area for Entry widgets.
 * Just computes requested size; display is handled by the widget itself.
 */

typedef struct {
    Tcl_Obj	*fontObj;
    Tcl_Obj	*widthObj;
} TextareaElement;

static const Ttk_ElementOptionSpec TextareaElementOptions[] = {
    { "-font", TK_OPTION_FONT,
	offsetof(TextareaElement,fontObj), DEF_ENTRY_FONT },
    { "-width", TK_OPTION_INT,
	offsetof(TextareaElement,widthObj), "20" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void TextareaElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    TextareaElement *textarea = (TextareaElement *)elementRecord;
    Tk_Font font = Tk_GetFontFromObj(tkwin, textarea->fontObj);
    int avgWidth = Tk_TextWidth(font, "0", 1);
    Tk_FontMetrics fm;
    int prefWidth = 1;

    Tk_GetFontMetrics(font, &fm);
    Tcl_GetIntFromObj(NULL, textarea->widthObj, &prefWidth);
    if (prefWidth <= 0)
	prefWidth = 1;

    *heightPtr = fm.linespace;
    *widthPtr = prefWidth * avgWidth;
}

static const Ttk_ElementSpec TextareaElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(TextareaElement),
    TextareaElementOptions,
    TextareaElementSize,
    TtkNullElementDraw
};

/*------------------------------------------------------------------------
 * +++ Widget layouts.
 */

TTK_BEGIN_LAYOUT(EntryLayout)
    TTK_GROUP("Entry.field", TTK_FILL_BOTH|TTK_BORDER,
	TTK_GROUP("Entry.padding", TTK_FILL_BOTH,
	    TTK_NODE("Entry.textarea", TTK_FILL_BOTH)))
TTK_END_LAYOUT

TTK_BEGIN_LAYOUT(ComboboxLayout)
    TTK_GROUP("Combobox.field", TTK_FILL_BOTH,
	TTK_NODE("Combobox.downarrow", TTK_PACK_RIGHT|TTK_FILL_Y)
	TTK_GROUP("Combobox.padding", TTK_FILL_BOTH,
	    TTK_NODE("Combobox.textarea", TTK_FILL_BOTH)))
TTK_END_LAYOUT

TTK_BEGIN_LAYOUT(SpinboxLayout)
    TTK_GROUP("Spinbox.field", TTK_PACK_TOP|TTK_FILL_X,
	TTK_GROUP("null", TTK_PACK_RIGHT,
	    TTK_NODE("Spinbox.uparrow", TTK_PACK_TOP|TTK_STICK_E)
	    TTK_NODE("Spinbox.downarrow", TTK_PACK_BOTTOM|TTK_STICK_E))
	TTK_GROUP("Spinbox.padding", TTK_FILL_BOTH,
	    TTK_NODE("Spinbox.textarea", TTK_FILL_BOTH)))
TTK_END_LAYOUT

/*------------------------------------------------------------------------
 * +++ Initialization.
 */

MODULE_SCOPE void
TtkEntry_Init(Tcl_Interp *interp)
{
    Ttk_Theme themePtr =  Ttk_GetDefaultTheme(interp);

    Ttk_RegisterElement(interp, themePtr, "textarea", &TextareaElementSpec, 0);

    Ttk_RegisterLayout(themePtr, "TEntry", EntryLayout);
    Ttk_RegisterLayout(themePtr, "TCombobox", ComboboxLayout);
    Ttk_RegisterLayout(themePtr, "TSpinbox", SpinboxLayout);

    RegisterWidget(interp, "ttk::entry", &EntryWidgetSpec);
    RegisterWidget(interp, "ttk::combobox", &ComboboxWidgetSpec);
    RegisterWidget(interp, "ttk::spinbox", &SpinboxWidgetSpec);
}

/*EOF*/
