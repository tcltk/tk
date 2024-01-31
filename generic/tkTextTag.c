/*
 * tkTextTag.c --
 *
 *	This module implements the "tag" subcommand of the widget command for
 *	text widgets, plus most of the other high-level functions related to
 *	tags.
 *
 * Copyright © 1992-1994 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
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
#include "default.h"
#include <assert.h>
#include <stdlib.h>

#ifndef MAX
# define MAX(a,b) (((int) a) < ((int) b) ? b : a)
#endif

#ifdef NDEBUG
# define DEBUG(expr)
#else
# define DEBUG(expr) expr
#endif

/*
 * The 'Tk_Justify' enum in tk.h is used to define a type for the -justify option of
 * the Text widget. These values are used as indices into the string table below.
 */

static const char *const justifyStrings[] = {
    "left", "right", "center", "full", NULL
};


static const Tk_OptionSpec tagOptionSpecs[] = {
    {TK_OPTION_BORDER, "-background", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, attrs.border), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BITMAP, "-bgstipple", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, bgStipple), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_PIXELS, "-borderwidth", NULL, NULL,
	NULL, offsetof(TkTextTag, attrs.borderWidthObj), offsetof(TkTextTag, attrs.borderWidth),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BOOLEAN, "-elide", NULL, NULL,
	NULL, offsetof(TkTextTag, elidePtr), offsetof(TkTextTag, elide),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_COLOR, "-eolcolor", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, eolColor), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BITMAP, "-fgstipple", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, fgStipple), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_FONT, "-font", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, tkfont), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_COLOR, "-foreground", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, attrs.fgColor), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_COLOR, "-hyphencolor", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, hyphenColor), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-hyphenrules", NULL, NULL,
	NULL, offsetof(TkTextTag, hyphenRulesPtr), TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BORDER, "-inactivebackground", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, attrs.inactiveBorder), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_COLOR, "-inactiveforeground", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, attrs.inactiveFgColor), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BORDER, "-inactiveselectbackground", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, inactiveSelBorder), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_COLOR, "-inactiveselectforeground", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, inactiveSelFgColor), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BOOLEAN, "-indentbackground", NULL, NULL,
	NULL, offsetof(TkTextTag, indentBgPtr), offsetof(TkTextTag, indentBg),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING_TABLE, "-justify", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, justify), TK_OPTION_NULL_OK|TK_OPTION_ENUM_VAR, justifyStrings, 0},
    {TK_OPTION_STRING, "-lang", NULL, NULL,
	NULL, offsetof(TkTextTag, langPtr), TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_PIXELS, "-lmargin1", NULL, NULL,
	NULL, offsetof(TkTextTag, lMargin1Obj), offsetof(TkTextTag, lMargin1), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_PIXELS, "-lmargin2", NULL, NULL,
	NULL, offsetof(TkTextTag, lMargin2Obj), offsetof(TkTextTag, lMargin2), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BORDER, "-lmargincolor", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, lMarginColor), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_PIXELS, "-offset", NULL, NULL,
	NULL, offsetof(TkTextTag, offsetObj), offsetof(TkTextTag, offset), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BOOLEAN, "-overstrike", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, overstrike), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_COLOR, "-overstrikecolor", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, overstrikeColor), TK_OPTION_NULL_OK, 0, 0},
#if SUPPORT_DEPRECATED_TAG_OPTIONS
    {TK_OPTION_SYNONYM, "-overstrikefg", NULL, NULL,
	NULL, 0, TCL_INDEX_NONE, TK_OPTION_NULL_OK, "-overstrikecolor", TK_TEXT_DEPRECATED_OVERSTRIKE_FG},
#endif /* SUPPORT_DEPRECATED_TAG_OPTIONS */
    {TK_OPTION_RELIEF, "-relief", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, relief), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_PIXELS, "-rmargin", NULL, NULL,
	NULL, offsetof(TkTextTag, rMarginObj), offsetof(TkTextTag, rMargin), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BORDER, "-rmargincolor", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, rMarginColor), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BORDER, "-selectbackground", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, selBorder), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_COLOR, "-selectforeground", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, selFgColor), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_PIXELS, "-spacing1", NULL, NULL,
	NULL, offsetof(TkTextTag, spacing1Obj), offsetof(TkTextTag, spacing1), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_PIXELS, "-spacing2", NULL, NULL,
	NULL, offsetof(TkTextTag, spacing2Obj), offsetof(TkTextTag, spacing2), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_PIXELS, "-spacing3", NULL, NULL,
	NULL, offsetof(TkTextTag, spacing3Obj), offsetof(TkTextTag, spacing3), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-tabs", NULL, NULL,
	NULL, offsetof(TkTextTag, tabStringPtr), TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING_TABLE, "-tabstyle", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, tabStyle), TK_OPTION_NULL_OK, tkTextTabStyleStrings, 0},
    {TK_OPTION_BOOLEAN, "-underline", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, underline), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_COLOR, "-underlinecolor", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, underlineColor), TK_OPTION_NULL_OK, 0, 0},
#if SUPPORT_DEPRECATED_TAG_OPTIONS
    {TK_OPTION_SYNONYM, "-underlinefg", NULL, NULL,
	NULL, 0, TCL_INDEX_NONE, TK_OPTION_NULL_OK, "-underlinecolor", 0},
#endif /* SUPPORT_DEPRECATED_TAG_OPTIONS */
    {TK_OPTION_BOOLEAN, "-undo", NULL, NULL,
	"1", TCL_INDEX_NONE, offsetof(TkTextTag, undo), 0, 0, 0},
    {TK_OPTION_STRING_TABLE, "-wrap", NULL, NULL,
	NULL, TCL_INDEX_NONE, offsetof(TkTextTag, wrapMode), TK_OPTION_NULL_OK|TK_OPTION_ENUM_VAR, tkTextWrapStrings, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0}
};

DEBUG_ALLOC(extern unsigned tkTextCountNewTag);
DEBUG_ALLOC(extern unsigned tkTextCountDestroyTag);
DEBUG_ALLOC(extern unsigned tkTextCountNewUndoToken);
DEBUG_ALLOC(extern unsigned tkTextCountDestroyUndoToken);

/*
 * Forward declarations for functions defined later in this file:
 */

static int		ChangeTagPriority(TkSharedText *sharedTextPtr, TkTextTag *tagPtr,
			    unsigned newPriority, int undo);
static int		TagAddRemove(TkText *textPtr, const TkTextIndex *index1Ptr,
			    const TkTextIndex *index2Ptr, TkTextTag *tagPtr, int add);
static void		TagBindEvent(TkText *textPtr, XEvent *eventPtr, TkTextTagSet *tagInfoPtr,
			    unsigned epoch);
static void		AppendTags(Tcl_Interp *interp, unsigned numTags, TkTextTag **tagArray);
static TkTextTag *	FindTag(Tcl_Interp *interp, const TkText *textPtr, Tcl_Obj *tagName);
static int		EnumerateTags(Tcl_Interp *interp, TkText *textPtr, int objc,
			    Tcl_Obj *const *objv);
static void		GrabSelection(TkText *textPtr, const TkTextTag *tagPtr, int add, int changed);

/*
 * We need some private undo/redo stuff.
 */

static void UndoChangeTagPriorityPerform(TkSharedText *, TkTextUndoInfo *, TkTextUndoInfo *, int);
static void UndoChangeTagPriorityDestroy(TkSharedText *, TkTextUndoToken *, int);
static Tcl_Obj *UndoChangeTagPriorityGetCommand(const TkSharedText *, const TkTextUndoToken *);
static Tcl_Obj *UndoChangeTagPriorityInspect(const TkSharedText *, const TkTextUndoToken *);

static const Tk_UndoType undoTokenTagPriorityType = {
    TK_TEXT_UNDO_TAG_PRIORITY,		/* action */
    UndoChangeTagPriorityGetCommand,	/* commandProc */
    UndoChangeTagPriorityPerform,	/* undoProc */
    UndoChangeTagPriorityDestroy,	/* destroyProc */
    NULL,				/* rangeProc */
    UndoChangeTagPriorityInspect	/* inspectProc */
};

static const Tk_UndoType redoTokenTagPriorityType = {
    TK_TEXT_REDO_TAG_PRIORITY,		/* action */
    UndoChangeTagPriorityGetCommand,	/* commandProc */
    UndoChangeTagPriorityPerform,	/* undoProc */
    UndoChangeTagPriorityDestroy,	/* destroyProc */
    NULL,				/* rangeProc */
    UndoChangeTagPriorityInspect	/* inspectProc */
};

typedef struct UndoTokenTagPriority {
    const Tk_UndoType *undoType;
    TkTextTag *tagPtr;
    uint32_t priority;
} UndoTokenTagPriority;

static Tcl_Obj *
UndoChangeTagPriorityGetCommand(
    TCL_UNUSED(const TkSharedText *),
    const TkTextUndoToken *item)
{
    const UndoTokenTagPriority *token = (const UndoTokenTagPriority *) item;
    Tcl_Obj *objPtr = Tcl_NewObj();

    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj("tag", TCL_INDEX_NONE));
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj("priority", TCL_INDEX_NONE));
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(token->tagPtr->name, TCL_INDEX_NONE));
    return objPtr;
}

static Tcl_Obj *
UndoChangeTagPriorityInspect(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item)
{
    const UndoTokenTagPriority *token = (const UndoTokenTagPriority *) item;
    Tcl_Obj *objPtr = UndoChangeTagPriorityGetCommand(sharedTextPtr, item);

    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewIntObj(token->priority));
    return objPtr;
}

static void
UndoChangeTagPriorityPerform(
    TkSharedText *sharedTextPtr,
    TkTextUndoInfo *undoInfo,
    TkTextUndoInfo *redoInfo,
    TCL_UNUSED(int))
{
    UndoTokenTagPriority *token = (UndoTokenTagPriority *) undoInfo->token;
    unsigned oldPriority = token->tagPtr->priority;

    ChangeTagPriority(sharedTextPtr, token->tagPtr, token->priority, 1);

    if (redoInfo) {
	redoInfo->token = undoInfo->token;
	redoInfo->token->undoType = &redoTokenTagPriorityType;
	token->priority = oldPriority;
    }
}

static void
UndoChangeTagPriorityDestroy(
    TkSharedText *sharedTextPtr,
    TkTextUndoToken *item,
    TCL_UNUSED(int))
{
    UndoTokenTagPriority *token = (UndoTokenTagPriority *) item;

    TkTextReleaseTag(sharedTextPtr, token->tagPtr, NULL);
}

/*
 *--------------------------------------------------------------
 *
 * TkTextTagCmd --
 *
 *	This function is invoked to process the "tag" options of the widget
 *	command for text widgets. See the user documentation for details on
 *	what it does.
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
TkTextTagCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. Someone else has already parsed this command
    				 * enough to know that objv[1] is "tag". */
{
    static const char *const tagOptionStrings[] = {
	"add", "bind", "cget", "clear", "configure", "delete", "findnext", "findprev",
	"getrange", "lower", "names", "nextrange", "prevrange", "priority", "raise",
	"ranges", "remove", NULL
    };
    enum tagOptions {
	TAG_ADD, TAG_BIND, TAG_CGET, TAG_CLEAR, TAG_CONFIGURE, TAG_DELETE, TAG_FINDNEXT, TAG_FINDPREV,
	TAG_GETRANGE, TAG_LOWER, TAG_NAMES, TAG_NEXTRANGE, TAG_PREVRANGE, TAG_PRIORITY, TAG_RAISE,
	TAG_RANGES, TAG_REMOVE
    };
    int optionIndex;
    Tcl_Size i;
    TkTextTag *tagPtr;
    TkTextIndex index1, index2;
    TkSharedText *sharedTextPtr;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "option ?arg ...?");
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObjStruct(interp, objv[2], tagOptionStrings, sizeof(char *),
	    "tag option", 0, &optionIndex) != TCL_OK) {
	return TCL_ERROR;
    }

    sharedTextPtr = textPtr->sharedTextPtr;

    switch ((enum tagOptions)optionIndex) {
    case TAG_ADD:
    case TAG_REMOVE: {
	int addTag;
	int anyChanges = 0;

	addTag = ((enum tagOptions) optionIndex) == TAG_ADD;
	if (objc < 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "tagName index1 ?index2 index1 index2 ...?");
	    return TCL_ERROR;
	}
	tagPtr = TkTextCreateTag(textPtr, Tcl_GetString(objv[3]), NULL);
	if (tagPtr->elide >= 0) {
	    /*
	     * Indices are potentially obsolete after adding or removing
	     * elided character ranges, especially indices having "display"
	     * or "any" submodifier, therefore increase the epoch.
	     */
	    TkBTreeIncrEpoch(sharedTextPtr->tree);
	}
	for (i = 4; i < objc; i += 2) {
	    if (!TkTextGetIndexFromObj(interp, textPtr, objv[i], &index1)) {
		return TCL_ERROR;
	    }
	    if (objc > i + 1) {
		if (!TkTextGetIndexFromObj(interp, textPtr, objv[i + 1], &index2)) {
		    return TCL_ERROR;
		}
		if (TkTextIndexCompare(&index1, &index2) >= 0) {
		    continue;
		}
	    } else {
		TkTextIndexForwChars(textPtr, &index1, 1, &index2, COUNT_INDICES);
	    }
	    if (TagAddRemove(textPtr, &index1, &index2, tagPtr, addTag)) {
		anyChanges = 1;
	    }
	}
	if (tagPtr->isSelTag) {
	    GrabSelection(textPtr, tagPtr, addTag, anyChanges);
	}
	if (anyChanges) {
	    if (tagPtr->undo) {
		TkTextUpdateAlteredFlag(sharedTextPtr);
	    }
	    /* still need to trigger enter/leave events on tags that have changed */
	    TkTextEventuallyRepick(textPtr);
	}
	break;
    }
    case TAG_BIND:
	if (objc < 4 || objc > 6) {
	    Tcl_WrongNumArgs(interp, 3, objv, "tagName ?sequence? ?command?");
	    return TCL_ERROR;
	}
	tagPtr = TkTextCreateTag(textPtr, Tcl_GetString(objv[3]), NULL);
	return TkTextBindEvent(interp, objc - 4, objv + 4, sharedTextPtr,
		&sharedTextPtr->tagBindingTable, tagPtr->name);
    case TAG_CGET:
	if (objc != 5) {
	    Tcl_WrongNumArgs(interp, 1, objv, "tag cget tagName option");
	    return TCL_ERROR;
	} else {
	    Tcl_Obj *objPtr;

	    if (!(tagPtr = FindTag(interp, textPtr, objv[3]))) {
		return TCL_ERROR;
	    }
	    objPtr = Tk_GetOptionValue(interp, tagPtr,
		    tagPtr->optionTable, objv[4], textPtr->tkwin);
	    if (!objPtr) {
		return TCL_ERROR;
	    }
	    Tcl_SetObjResult(interp, objPtr);
	    return TCL_OK;
	}
	break;
    case TAG_CLEAR: {
	int discardSelection;
	Tcl_Size epoch, countTags;
	TkTextTag **arrayPtr;
	int anyChanges;
	int arg;

	if (objc < 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "?-discardselection? index1 ?index2 index1 index2 ...?");
	    return TCL_ERROR;
	}

	arg = 3;

	if (objc > 4 && *Tcl_GetString(objv[arg]) == '-') {
	    if (strcmp(Tcl_GetString(objv[arg++]), "-discardselection") == 0) {
		discardSelection = 1;
	    } else {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad option \"%s\": must be -discardselection", Tcl_GetString(objv[3])));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "BAD_OPTION", NULL);
		return TCL_ERROR;
	    }
	}

	discardSelection = 0;
	epoch = TkBTreeEpoch(sharedTextPtr->tree);
	arrayPtr = (TkTextTag **)ckalloc(sharedTextPtr->numEnabledTags * sizeof(TkTextTag *));
	countTags = 0;
	anyChanges = 0;

	for (i = arg; i < objc; i += 2) {
	    if (!TkTextGetIndexFromObj(interp, textPtr, objv[i], &index1)) {
		return TCL_ERROR;
	    }

	    if (objc > i + 1) {
		if (!TkTextGetIndexFromObj(interp, textPtr, objv[i + 1], &index2)) {
		    return TCL_ERROR;
		}
		if (TkTextIndexCompare(&index1, &index2) >= 0) {
		    continue;
		}
	    } else {
		TkTextIndexForwChars(textPtr, &index1, 1, &index2, COUNT_INDICES);
	    }

	    if (!discardSelection) {
		TkTextClearSelection(sharedTextPtr, &index1, &index2);
	    }

	    if ((tagPtr = TkTextClearTags(sharedTextPtr, textPtr, &index1, &index2, discardSelection))) {
		for ( ; tagPtr; tagPtr = tagPtr->nextPtr) {
		    if (tagPtr->epoch != epoch) {
			tagPtr->epoch = epoch;
			arrayPtr[countTags++] = tagPtr;

			if (tagPtr->isSelTag) {
			    GrabSelection(textPtr, tagPtr, 0, 1);
			}
			if (tagPtr->undo) {
			    anyChanges = 1;
			}
		    }
		}
	    }
	}

	if (anyChanges) {
	    TkTextUpdateAlteredFlag(sharedTextPtr);
	}
	AppendTags(interp, countTags, arrayPtr);
	ckfree(arrayPtr);
	break;
    }
    case TAG_CONFIGURE:
	if (objc < 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "tagName ?-option value ...?");
	    return TCL_ERROR;
	}
	return TkConfigureTag(interp, textPtr, Tcl_GetString(objv[3]), 1, objc - 4, objv + 4);
    case TAG_DELETE: {
	Tcl_HashEntry *hPtr;
	int anyChanges = 0;

	if (objc < 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "tagName ?tagName ...?");
	    return TCL_ERROR;
	}
	for (i = 3; i < objc; i++) {
	    int undo;

	    if (!(hPtr = Tcl_FindHashEntry(&sharedTextPtr->tagTable, Tcl_GetString(objv[i])))) {
		/*
		 * Either this tag doesn't exist or it's the 'sel' tag (which is not in
		 * the hash table). Either way we don't want to delete it.
		 */

		continue;
	    }
	    tagPtr = (TkTextTag *)Tcl_GetHashValue(hPtr);
	    undo = tagPtr->undo;
	    assert(!tagPtr->isSelTag);
	    if (TkTextDeleteTag(textPtr, tagPtr, hPtr) && undo) {
		anyChanges = 1;
	    }
	}
	if (anyChanges) {
	    TkTextUpdateAlteredFlag(sharedTextPtr);
	}
	break;
    }
    case TAG_FINDNEXT: {
	TkTextSegment *segPtr;
	const TkBitField *selTags = NULL;

	if (objc != 4 && objc != 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "?-discardselection? index");
	    return TCL_ERROR;
	}
	if (objc == 5) {
	    if (strcmp(Tcl_GetString(objv[3]), "-discardselection") == 0) {
		selTags = sharedTextPtr->selectionTags;
	    } else {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad option \"%s\": must be -discardselection", Tcl_GetString(objv[3])));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "BAD_OPTION", NULL);
		return TCL_ERROR;
	    }
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[objc - 1], &index1)) {
	    return TCL_ERROR;
	}
	TkTextIndexSetupToEndOfText(&index2, textPtr, sharedTextPtr->tree);
	if ((segPtr = TkBTreeFindNextTagged(&index1, &index2, selTags))) {
	    TkTextIndex index;
	    char buf[TK_POS_CHARS];

	    TkTextIndexClear(&index, textPtr);
	    TkTextIndexSetSegment(&index, segPtr);
	    TkrTextPrintIndex(textPtr, &index, buf);
	    Tcl_AppendElement(interp, buf);
	}
	break;
    }
    case TAG_FINDPREV: {
	int discardSelection = 0;
	TkTextSegment *segPtr;

	if (objc != 4 && objc != 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "?-discardselection? index");
	    return TCL_ERROR;
	}
	if (objc == 5) {
	    if (strcmp(Tcl_GetString(objv[3]), "-discardselection") == 0) {
		discardSelection = 1;
	    } else {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad option \"%s\": must be -discardselection", Tcl_GetString(objv[3])));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "BAD_OPTION", NULL);
		return TCL_ERROR;
	    }
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[objc - 1], &index1)) {
	    return TCL_ERROR;
	}
	TkTextIndexSetupToStartOfText(&index2, textPtr, sharedTextPtr->tree);
	if ((segPtr = TkBTreeFindPrevTagged(&index1, &index2, discardSelection))) {
	    TkTextIndex index;
	    char buf[TK_POS_CHARS];

	    TkTextIndexClear(&index, textPtr);
	    TkTextIndexSetSegment(&index, segPtr);
	    TkrTextPrintIndex(textPtr, &index, buf);
	    Tcl_AppendElement(interp, buf);
	}
	break;
    }
    case TAG_GETRANGE: {
	TkTextIndex index;

	if (objc != 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "tagName index");
	    return TCL_ERROR;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[4], &index)) {
	    return TCL_ERROR;
	}
	if (!(tagPtr = FindTag(interp, textPtr, objv[3]))) {
	    return TCL_ERROR;
	}
	if (tagPtr->rootPtr && TkBTreeCharTagged(&index, tagPtr)) {
	    TkTextIndex result;
	    char buf[TK_POS_CHARS];

	    /* point to position after index */
	    TkTextIndexForwChars(textPtr, &index, 1, &index, COUNT_INDICES);

	    TkTextTagFindStartOfRange(textPtr, tagPtr, &index, &result);
	    TkrTextPrintIndex(textPtr, &result, buf);
	    Tcl_AppendElement(interp, buf);

	    TkTextTagFindEndOfRange(textPtr, tagPtr, &index, &result);
	    TkrTextPrintIndex(textPtr, &result, buf);
	    Tcl_AppendElement(interp, buf);
	}
	break;
    }
    case TAG_LOWER: {
	TkTextTag *tagPtr2;
	unsigned newPriority;

	if (objc != 4 && objc != 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "tagName ?belowThis?");
	    return TCL_ERROR;
	}
	if (!(tagPtr = FindTag(interp, textPtr, objv[3]))) {
	    return TCL_ERROR;
	}
	if (objc == 5) {
	    if (!(tagPtr2 = FindTag(interp, textPtr, objv[4]))) {
		return TCL_ERROR;
	    }
	    newPriority = tagPtr2->priority;
	    if (tagPtr->priority < tagPtr2->priority) {
		newPriority -= 1;
	    }
	} else {
	    newPriority = 0;
	}
	if (ChangeTagPriority(sharedTextPtr, tagPtr, newPriority, 1) && tagPtr->rootPtr) {
	    if (tagPtr->undo) {
		TkTextUpdateAlteredFlag(sharedTextPtr);
	    }

	    /*
	     * If this is the 'sel' tag, then we don't actually need to call this for all peers.
	     *
	     * TODO: The current implementation is sloppy, we need only to refresh the ranges
	     * with actual changes, and not all the ranges of this tag.
	     */

	    TkTextRedrawTag(tagPtr->isSelTag ? NULL : sharedTextPtr,
		    textPtr, NULL, NULL, tagPtr, 0);
	}
	break;
    }
    case TAG_NAMES:
    	return EnumerateTags(interp, textPtr, objc, objv);
	/* not reached */
    case TAG_NEXTRANGE: {
	TkTextSearch tSearch;
	char position[TK_POS_CHARS];
	Tcl_Obj *resultObj;

	if (objc != 5 && objc != 6) {
	    Tcl_WrongNumArgs(interp, 3, objv, "tagName index1 ?index2?");
	    return TCL_ERROR;
	}
	if (!(tagPtr = FindTag(NULL, textPtr, objv[3])) || !tagPtr->rootPtr) {
	    return TCL_OK;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[4], &index1)) {
	    return TCL_ERROR;
	}
	if (objc == 5) {
	    TkTextIndexSetupToEndOfText(&index2, textPtr, sharedTextPtr->tree);
	} else if (!TkTextGetIndexFromObj(interp, textPtr, objv[5], &index2)) {
	    return TCL_ERROR;
	}

	TkBTreeStartSearch(&index1, &index2, tagPtr, &tSearch, SEARCH_NEXT_TAGON);
	if (TkBTreeNextTag(&tSearch)) {
	    assert(TkTextIndexCompare(&tSearch.curIndex, &index1) >= 0);
	    assert(TkTextIndexCompare(&tSearch.curIndex, &index2) < 0);
	    if (TkTextIndexIsEqual(&index1, &tSearch.curIndex)) {
		TkTextIndex oneBack;

		/*
		 * The first character is tagged. See if there is an on-toggle just
		 * before the character. If not, then skip to the end of this tagged range.
		 */

		if (TkTextIndexBackChars(textPtr, &index1, 1, &oneBack, COUNT_DISPLAY_INDICES)
			&& TkBTreeCharTagged(&oneBack, tagPtr)
			&& (!TkBTreeNextTag(&tSearch) || !TkBTreeNextTag(&tSearch))) {
		    return TCL_OK;
		}
		assert(TkTextIndexCompare(&tSearch.curIndex, &index2) < 0);
	    }
	    resultObj = Tcl_NewObj();
	    TkrTextPrintIndex(textPtr, &tSearch.curIndex, position);
	    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(position, TCL_INDEX_NONE));
	    TkBTreeLiftSearch(&tSearch); /* we need tagoff even if outside of the range */
	    TkBTreeNextTag(&tSearch);    /* cannot fail */
	    assert(tSearch.segPtr);      /* proof last assumption */
	    TkrTextPrintIndex(textPtr, &tSearch.curIndex, position);
	    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(position, TCL_INDEX_NONE));
	    Tcl_SetObjResult(interp, resultObj);
	}
	break;
    }
    case TAG_PREVRANGE: {
	TkTextSearch tSearch;
	char position1[TK_POS_CHARS];
	char position2[TK_POS_CHARS];
	Tcl_Obj *resultObj;

	if (objc != 5 && objc != 6) {
	    Tcl_WrongNumArgs(interp, 3, objv, "tagName index1 ?index2?");
	    return TCL_ERROR;
	}
	if (!(tagPtr = FindTag(NULL, textPtr, objv[3])) || !tagPtr->rootPtr) {
	    return TCL_OK;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[4], &index1)) {
	    return TCL_ERROR;
	}
	if (objc == 5) {
	    TkTextIndexSetupToStartOfText(&index2, textPtr, sharedTextPtr->tree);
	} else if (!TkTextGetIndexFromObj(interp, textPtr, objv[5], &index2)) {
	    return TCL_ERROR;
	}

	TkBTreeStartSearchBack(&index1, &index2, tagPtr, &tSearch, SEARCH_EITHER_TAGON_TAGOFF);

	if (TkBTreePrevTag(&tSearch)) {
	    assert(TkTextIndexCompare(&tSearch.curIndex, &index1) <= 0);
	    assert(TkTextIndexCompare(&tSearch.curIndex, &index2) >= 0);
	    index1 = tSearch.curIndex;
	    if (tSearch.tagon) {
		TkTextIndex end;

		/*
		 * We've found tagon. Now search forward for tagoff.
		 */

		TkrTextPrintIndex(textPtr, &index1, position1);
		TkTextIndexSetupToEndOfText(&end, textPtr, sharedTextPtr->tree);
		TkTextIndexForwChars(textPtr, &index1, 1, &index1, COUNT_INDICES);
		TkBTreeStartSearch(&index1, &end, tagPtr, &tSearch, SEARCH_EITHER_TAGON_TAGOFF);
		TkBTreeNextTag(&tSearch); /* cannot fail */
		assert(tSearch.segPtr);   /* proof last assumption */
		assert(!tSearch.tagon);   /* must be tagoff */
		TkrTextPrintIndex(textPtr, &tSearch.curIndex, position2);
	    } else {
		/*
		 * We've found tagoff. Now search backwards for tagon.
		 */

		if (!TkBTreePrevTag(&tSearch)) {
		    return TCL_OK;
		}
		assert(TkTextIndexCompare(&tSearch.curIndex, &index2) >= 0);
		TkrTextPrintIndex(textPtr, &tSearch.curIndex, position1);
		TkrTextPrintIndex(textPtr, &index1, position2);
	    }
	    resultObj = Tcl_NewObj();
	    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(position1, TCL_INDEX_NONE));
	    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(position2, TCL_INDEX_NONE));
	    Tcl_SetObjResult(interp, resultObj);
	}
	break;
    }
    case TAG_PRIORITY:
	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "tagName");
	    return TCL_ERROR;
	}
	if (!(tagPtr = FindTag(interp, textPtr, objv[3]))) {
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, Tcl_NewIntObj(tagPtr->priority));
	break;
    case TAG_RAISE: {
	TkTextTag *tagPtr2;
	unsigned newPriority;

	if (objc != 4 && objc != 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "tagName ?aboveThis?");
	    return TCL_ERROR;
	}
	if (!(tagPtr = FindTag(interp, textPtr, objv[3]))) {
	    return TCL_ERROR;
	}
	if (objc == 5) {
	    if (!(tagPtr2 = FindTag(interp, textPtr, objv[4]))) {
		return TCL_ERROR;
	    }
	    newPriority = tagPtr2->priority;
	    if (tagPtr->priority > tagPtr2->priority) {
		newPriority += 1;
	    }
	} else {
	    newPriority = sharedTextPtr->numEnabledTags - 1;
	}
	if (ChangeTagPriority(sharedTextPtr, tagPtr, newPriority, 1) && tagPtr->rootPtr) {
	    if (tagPtr->undo) {
		TkTextUpdateAlteredFlag(sharedTextPtr);
	    }

	    /*
	     * If this is the 'sel' tag, then we don't actually need to call this for all peers.
	     *
	     * TODO: The current implementation is sloppy, we need only to refresh the ranges
	     * with actual changes, and not all the ranges of this tag.
	     */

	    TkTextRedrawTag(tagPtr->isSelTag ? NULL : sharedTextPtr,
		    textPtr, NULL, NULL, tagPtr, 0);
	}
	break;
    }
    case TAG_RANGES: {
	TkTextIndex first, last;
	TkTextSearch tSearch;
	Tcl_Obj *listObj = Tcl_NewObj();
	DEBUG(int found = 0);

	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "tagName");
	    return TCL_ERROR;
	}
	if ((tagPtr = FindTag(NULL, textPtr, objv[3])) && tagPtr->rootPtr) {
	    TkTextIndexSetupToStartOfText(&first, textPtr, sharedTextPtr->tree);
	    TkTextIndexSetupToEndOfText(&last, textPtr, sharedTextPtr->tree);
	    TkBTreeStartSearch(&first, &last, tagPtr, &tSearch, SEARCH_NEXT_TAGON);
	    while (TkBTreeNextTag(&tSearch)) {
		Tcl_ListObjAppendElement(NULL, listObj, TkTextNewIndexObj(&tSearch.curIndex));
		DEBUG(found = 1);
	    }
	    assert(!found || !tSearch.tagon); /* search must find end of text */
	    Tcl_SetObjResult(interp, listObj);
	}
	break;
    }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextTagFindStartOfRange --
 *
 *	Find the start of the range which is marked by given tag. This
 *	functions requires that the given start index for the search
 *	is marked by this tag.
 *
 * Results:
 *	Returns the end index in '*resultPtr'.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkTextTagFindStartOfRange(
    TkText *textPtr,			/* Info about overall widget. */
    const TkTextTag *tagPtr,		/* Search for this tag. */
    const TkTextIndex *currentPtr,	/* Start search after this position. */
    TkTextIndex *resultPtr)		/* Returns end of tagged range. */
{
    TkTextSearch tSearch;
    TkTextIndex stopIndex;

    assert(textPtr);
    assert(currentPtr);
    assert(resultPtr);

    TkTextIndexSetupToStartOfText(&stopIndex, textPtr, textPtr->sharedTextPtr->tree);
    TkBTreeStartSearchBack(currentPtr, &stopIndex, tagPtr, &tSearch, SEARCH_NEXT_TAGON);
    TkBTreePrevTag(&tSearch);
    assert(tSearch.segPtr); /* last search must not fail */
    *resultPtr = tSearch.curIndex;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextTagFindEndOfRange --
 *
 *	Find the end of the range which is marked by given tag. This
 *	functions requires that the given start index for the search
 *	is marked by this tag.
 *
 * Results:
 *	Returns the end index in '*resultPtr'.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkTextTagFindEndOfRange(
    TkText *textPtr,			/* Info about overall widget. */
    const TkTextTag *tagPtr,		/* Search for this tag. */
    const TkTextIndex *currentPtr,	/* Start search at this position. */
    TkTextIndex *resultPtr)		/* Returns end of tagged range. */
{
    TkTextSearch tSearch;
    TkTextIndex stopIndex;

    assert(textPtr);
    assert(currentPtr);
    assert(resultPtr);

    TkTextIndexSetupToEndOfText(&stopIndex, textPtr, textPtr->sharedTextPtr->tree);
    TkBTreeStartSearch(currentPtr, &stopIndex, tagPtr, &tSearch, SEARCH_EITHER_TAGON_TAGOFF);
    TkBTreeNextTag(&tSearch);
    assert(tSearch.segPtr); /* last search must not fail */
    assert(!tSearch.tagon); /* must be tagoff */
    *resultPtr = tSearch.curIndex;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextClearSelection --
 *
 *	Clear the selection in specified range.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See TkBTreeTag and TkTextSelectionEvent for side effects.
 *
 *----------------------------------------------------------------------
 */

void
TkTextClearSelection(
    TkSharedText *sharedTextPtr,
    const TkTextIndex *indexPtr1,
    const TkTextIndex *indexPtr2)
{
    TkText *textPtr;

    for (textPtr = sharedTextPtr->peers; textPtr; textPtr = textPtr->next) {
	if (TkBTreeTag(sharedTextPtr, textPtr, indexPtr1, indexPtr2, textPtr->selTagPtr,
		0, NULL, TkTextRedrawTag)) {
	    /*
	     * Send an event that the selection changed. This is equivalent to:
	     *	 event generate $textWidget <<Selection>>
	     */

	    TkTextSelectionEvent(textPtr); /* <<Selection>> will be received after deletion */
	    textPtr->abortSelections = 1;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextClearTags --
 *
 *	Turn all tags off inside a given range.
 *
 * Results:
 *	Whether any tag has been removed.
 *
 * Side effects:
 *	See TkBTreeClearTags and TkTextPushUndoToken for side effects.
 *
 *----------------------------------------------------------------------
 */

TkTextTag *
TkTextClearTags(
    TkSharedText *sharedTextPtr,
    TkText *textPtr,			/* can be NULL */
    const TkTextIndex *indexPtr1,
    const TkTextIndex *indexPtr2,
    int discardSelection)
{
    TkTextTag *tagPtr;
    TkTextUndoInfo undoInfo;
    TkTextUndoInfo *undoInfoPtr;

    undoInfoPtr = TkTextUndoStackIsFull(sharedTextPtr->undoStack) ? NULL : &undoInfo;
    tagPtr = TkBTreeClearTags(sharedTextPtr, textPtr, indexPtr1, indexPtr2, undoInfoPtr,
	    discardSelection, TkTextRedrawTag);
    if (tagPtr && undoInfoPtr && undoInfo.token) {
	TkTextPushUndoToken(sharedTextPtr, undoInfo.token, undoInfo.byteSize);
    }
    return tagPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextUpdateTagDisplayFlags --
 *
 *	Update the display flags 'affectsDisplay' and 'affectsDisplayGeometry',
 *	according to the current attributes of the given tag.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The flags 'affectsDisplay' and 'affectsDisplayGeometry' may change.
 *
 *----------------------------------------------------------------------
 */

void
TkTextUpdateTagDisplayFlags(
    TkTextTag *tagPtr)
{
    tagPtr->affectsDisplay = 0;
    tagPtr->affectsDisplayGeometry = 0;

    if (tagPtr->elidePtr
	    || tagPtr->tkfont
	    || tagPtr->justify != TK_JUSTIFY_NULL
	    || tagPtr->lMargin1Obj
	    || tagPtr->lMargin2Obj
	    || tagPtr->offsetObj
	    || tagPtr->rMarginObj
	    || tagPtr->spacing1Obj
	    || tagPtr->spacing2Obj
	    || tagPtr->spacing3Obj
	    || tagPtr->tabStringPtr
	    || tagPtr->tabStyle == TK_TEXT_TABSTYLE_TABULAR
	    || tagPtr->tabStyle == TK_TEXT_TABSTYLE_WORDPROCESSOR
		|| tagPtr->wrapMode == TEXT_WRAPMODE_CHAR
		|| tagPtr->wrapMode == TEXT_WRAPMODE_NONE
		|| tagPtr->wrapMode == TEXT_WRAPMODE_WORD
		|| tagPtr->wrapMode == TEXT_WRAPMODE_CODEPOINT) {
	tagPtr->affectsDisplay = 1;
	tagPtr->affectsDisplayGeometry = 1;
    } else if (tagPtr->attrs.border
	    || tagPtr->attrs.inactiveBorder
	    || tagPtr->selBorder
	    || tagPtr->inactiveSelBorder
	    || tagPtr->relief != TK_RELIEF_NULL
	    || tagPtr->bgStipple != None
	    || tagPtr->indentBg >= 0
	    || tagPtr->attrs.fgColor
	    || tagPtr->attrs.inactiveFgColor
	    || tagPtr->selFgColor
	    || tagPtr->inactiveSelFgColor
	    || tagPtr->fgStipple != None
	    || tagPtr->eolColor
	    || tagPtr->hyphenColor
	    || tagPtr->overstrike >= 0
	    || tagPtr->overstrikeColor
	    || tagPtr->underline >= 0
	    || tagPtr->underlineColor
	    || tagPtr->lMarginColor
	    || tagPtr->rMarginColor) {
	tagPtr->affectsDisplay = 1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkConfigureTag --
 *
 *	This function is called to process an objv/objc list, plus the Tk
 *	option database, in order to configure (or reconfigure) a text tag.
 *
 * Results:
 *	Any of the standard Tcl return values.
 *
 * Side effects:
 *	A new tag will be created if required, otherwise an existing tag
 *	will be modified.
 *
 *----------------------------------------------------------------------
 */

static void
SetupDefaultRelief(
    TkText *textPtr,
    TkTextTag *tagPtr)
{
    if (tagPtr->isSelTag) {
	Tk_GetRelief(textPtr->interp, DEF_TEXT_SELECT_RELIEF, &tagPtr->relief);
    } else {
	tagPtr->relief = TK_RELIEF_FLAT;
    }
}

int
TkConfigureTag(
    Tcl_Interp *interp,		/* Current interpreter. */
    TkText *textPtr,		/* Info about overall widget. */
    char const *tagName,	/* Name of affected tag. */
    int redraw,		/* Redraw the affected text if required? */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Remaining argument objects. */
{
    int newTag;
    int mask = 0;
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    TkTextTag *tagPtr = TkTextCreateTag(textPtr, tagName, &newTag);
    int relief = tagPtr->relief;
    Tcl_Obj *elidePtr = tagPtr->elidePtr;
    int elide = tagPtr->elide;
    int undo = tagPtr->undo;
    int affectsDisplay = tagPtr->affectsDisplay;
    int affectsLineHeight = 0;
    int rc = TCL_OK;

    if (objc <= 1) {
	Tcl_Obj *objPtr = Tk_GetOptionInfo(interp, (char *) tagPtr, tagPtr->optionTable,
		objc == 1 ? objv[0] : NULL, textPtr->tkwin);

	if (!objPtr) {
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, objPtr);
	return TCL_OK;
    }

    if (tagPtr->isSelTag) {
	tagPtr->attrs = textPtr->selTagConfigAttrs;
    }
    if (Tk_SetOptions(interp, (char *) tagPtr, tagPtr->optionTable,
	    objc, objv, textPtr->tkwin, NULL, &mask) != TCL_OK) {
	if (tagPtr->isSelTag) {
	    tagPtr->attrs = textPtr->selAttrs;
	}
	return TCL_ERROR;
    }

#if SUPPORT_DEPRECATED_TAG_OPTIONS

    if (mask & (TK_TEXT_DEPRECATED_OVERSTRIKE_FG|TK_TEXT_DEPRECATED_UNDERLINE_FG)) {
	static int warnAboutOverstrikeFg = 1;
	static int warnAboutUnderlineFg = 1;

	if (mask & TK_TEXT_DEPRECATED_OVERSTRIKE_FG) {
	    if (warnAboutOverstrikeFg) {
		fprintf(stderr, "tk::text: Tag option \"-overstrikefg\" is deprecated, "
			"please use option \"-overstrikecolor\".\n");
		warnAboutOverstrikeFg = 0;
	    }
	}
	if (mask & TK_TEXT_DEPRECATED_UNDERLINE_FG) {
	    if (warnAboutUnderlineFg) {
		fprintf(stderr, "tk::text: Tag option \"-underlinefg\" is deprecated, "
			"please use option \"-underlinecolor\".\n");
		warnAboutUnderlineFg = 0;
	    }
	}
    }

#endif /* SUPPORT_DEPRECATED_TAG_OPTIONS */

    /*
      Some of the configuration options, like -underline and -justify, require
     * additional translation (this is needed because we need to distinguish a
     * particular value of an option from "unspecified").
     */

    tagPtr->attrs.borderWidth = MAX(0, tagPtr->attrs.borderWidth);

    if (tagPtr->langPtr) {
	if (!TkTextTestLangCode(interp, tagPtr->langPtr)) {
	    rc = TCL_ERROR;
	} else {
	    memcpy(tagPtr->lang, Tcl_GetString(tagPtr->langPtr), 3);
	}
    } else {
	memset(tagPtr->lang, 0, 3);
    }
    if (tagPtr->relief != TK_RELIEF_NULL) {
    } else if (relief != TK_RELIEF_NULL) {
	SetupDefaultRelief(textPtr, tagPtr);
    }
    if (tagPtr->spacing1Obj) {
	tagPtr->spacing1 = MAX(0, tagPtr->spacing1);
    }
    if (tagPtr->spacing2Obj) {
	tagPtr->spacing2 = MAX(0, tagPtr->spacing2);
    }
    if (tagPtr->spacing3Obj) {
	tagPtr->spacing3 = MAX(0, tagPtr->spacing3);
    }
    if (tagPtr->tabArrayPtr) {
	ckfree(tagPtr->tabArrayPtr);
	tagPtr->tabArrayPtr = NULL;
    }
    if (tagPtr->tabStringPtr) {
	if (!(tagPtr->tabArrayPtr = TkTextGetTabs(interp, textPtr, tagPtr->tabStringPtr))) {
	    rc = TCL_ERROR;
	}
    }
    if (tagPtr->hyphenRulesPtr) {
	int oldHyphenRules = tagPtr->hyphenRules;

	if (TkTextParseHyphenRules(textPtr, tagPtr->hyphenRulesPtr, &tagPtr->hyphenRules) != TCL_OK) {
	    rc = TCL_ERROR;
	}
	if (oldHyphenRules != tagPtr->hyphenRules && textPtr->hyphenate) {
	    affectsDisplay = 1;
	}
    }
    if (tagPtr->elidePtr) {
	if (!elidePtr) {
	    sharedTextPtr->numElisionTags += 1;
	}

	if (TkBitTest(sharedTextPtr->selectionTags, tagPtr->index)) {
	    /*
	     * It's not allowed to set the elide attribute of the special selection tag
	     * to 'true' (this would cause errors, because this case is not implemented).
	     */

	    Tcl_DecrRefCount(tagPtr->elidePtr);
	    tagPtr->elidePtr = NULL;
	    tagPtr->elide = -1;
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "not allowed to set elide option of selection tag \"%s\"", tagPtr->name));
	    Tcl_SetErrorCode(interp, "TK", "VALUE", "ELIDE", NULL);
	    rc = TCL_ERROR;
	}

	/*
	 * Indices are potentially obsolete after changing -elide,
	 * especially those computed with "display" or "any"
	 * submodifier, therefore increase the epoch.
	 */

	TkBTreeIncrEpoch(sharedTextPtr->tree);
    } else {
	if (elidePtr) {
	    sharedTextPtr->numElisionTags -= 1;
	}
	tagPtr->elide = 0;
    }
    if (tagPtr->undo != undo) {
	TkBitPut(sharedTextPtr->dontUndoTags, tagPtr->index, !tagPtr->undo);
    }

    /*
     * If the "sel" tag was changed, be sure to mirror information
     * from the tag back into the text widget record.
     */

    if (tagPtr->isSelTag) {
	if (tagPtr->attrs.border != textPtr->selTagConfigAttrs.border) {
	    textPtr->selAttrs.border = tagPtr->attrs.border;
	}
	if (tagPtr->attrs.inactiveBorder != textPtr->selTagConfigAttrs.inactiveBorder) {
	    textPtr->selAttrs.inactiveBorder = tagPtr->attrs.inactiveBorder;
	}
	if (tagPtr->attrs.fgColor != textPtr->selTagConfigAttrs.fgColor) {
	    textPtr->selAttrs.fgColor = tagPtr->attrs.fgColor;
	}
	if (tagPtr->attrs.inactiveFgColor != textPtr->selTagConfigAttrs.inactiveFgColor) {
	    textPtr->selAttrs.inactiveFgColor = tagPtr->attrs.inactiveFgColor;
	}
	if (tagPtr->attrs.borderWidthObj != textPtr->selTagConfigAttrs.borderWidthObj) {
	    textPtr->selAttrs.borderWidthObj = tagPtr->attrs.borderWidthObj;
	    textPtr->selAttrs.borderWidth = tagPtr->attrs.borderWidth;
	}
	textPtr->selTagConfigAttrs = tagPtr->attrs;
	tagPtr->attrs = textPtr->selAttrs;
    }

    TkTextUpdateTagDisplayFlags(tagPtr);
    if (tagPtr->affectsDisplay) {
	affectsDisplay = 1;
    }
    if (tagPtr->tkfont != NULL && tagPtr->tkfont != textPtr->tkfont) {
	Tk_FontMetrics fm;

	Tk_GetFontMetrics(tagPtr->tkfont, &fm);
	if (MAX(1, fm.linespace) != textPtr->lineHeight) {
	    affectsLineHeight = 1;
	}
    }

    TkBitPut(sharedTextPtr->elisionTags, tagPtr->index, !!tagPtr->elidePtr);
    TkBitPut(sharedTextPtr->affectDisplayTags, tagPtr->index, tagPtr->affectsDisplay);
    TkBitPut(sharedTextPtr->notAffectDisplayTags, tagPtr->index, !tagPtr->affectsDisplay);
    TkBitPut(sharedTextPtr->affectGeometryTags, tagPtr->index, tagPtr->affectsDisplayGeometry);
    TkBitPut(sharedTextPtr->affectLineHeightTags, tagPtr->index, affectsLineHeight);

    if (!TkBitTest(sharedTextPtr->selectionTags, tagPtr->index)) {
	TkBitPut(sharedTextPtr->affectDisplayNonSelTags, tagPtr->index, tagPtr->affectsDisplay);
	TkBitPut(sharedTextPtr->affectGeometryNonSelTags, tagPtr->index,
		tagPtr->affectsDisplayGeometry);
    }

    if (!tagPtr->elidePtr != !elidePtr || (tagPtr->elidePtr && elide != tagPtr->elide)) {
	/*
	 * Eventually we have to insert/remove branches and links according to
	 * the elide information of this tag.
	 */

	TkBTreeUpdateElideInfo(textPtr, tagPtr);
    }

    if (redraw && !newTag && affectsDisplay) {
	/*
	 * This action is not necessary if this is a new tag, since it can't have been
	 * applied to anything yet.
	 *
	 * If this is the 'sel' tag, then we don't need to call this for all peers, unless
	 * we actually want to synchronize sel-style changes across the peers.
	 */

	TkTextRedrawTag(sharedTextPtr, NULL, NULL, NULL, tagPtr, 0);
    }

    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextFontHeightChanged --
 *
 *	The font height of the text widget has changed, so we have to update
 *	textPtr->affectLineHeightTags accordingly.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	textPtr->affectLineHeightTags will be updated.
 *
 *----------------------------------------------------------------------
 */

void
TkTextFontHeightChanged(
    TkText *textPtr)		/* Info about overall widget. */
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr = NULL;
    TkBitField *affectLineHeightTags = textPtr->sharedTextPtr->affectLineHeightTags;

    TkBitClear(affectLineHeightTags);

    for (hPtr = Tcl_FirstHashEntry(&textPtr->sharedTextPtr->tagTable, &search);
	    hPtr;
	    hPtr = Tcl_NextHashEntry(&search)) {
	const TkTextTag *tagPtr = (const TkTextTag *)Tcl_GetHashValue(hPtr);

	if (tagPtr->tkfont != NULL && tagPtr->tkfont != textPtr->tkfont) {
	    Tk_FontMetrics fm;

	    Tk_GetFontMetrics(tagPtr->tkfont, &fm);
	    if (MAX(1, fm.linespace) != textPtr->lineHeight) {
		TkBitSet(affectLineHeightTags, tagPtr->index);
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * AppendTags --
 *
 *	This function is appending the given array of tags to the interpreter.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Results will be appended to the interpreter.
 *
 *----------------------------------------------------------------------
 */

static void
AppendTags(
    Tcl_Interp *interp,		/* Current interpreter. */
    unsigned numTags,		/* Size of array. */
    TkTextTag **tagArray)	/* Array of tag pointer, some pointer may be NULL. */
{
    unsigned i;
    Tcl_Obj *listObj;

    if (numTags == 0) {
	return;
    }

    TkTextSortTags(numTags, tagArray);
    listObj = Tcl_NewObj();

    for (i = 0; i < numTags; ++i) {
	if (tagArray[i]) {
	    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj(tagArray[i]->name, TCL_INDEX_NONE));
	}
    }
    Tcl_SetObjResult(interp, listObj);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextReplaceTags --
 *
 *	This function is replacing the tag information of given segment
 *	with provided list of tags.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

# define TK_TEXT_SET_MAX_BIT_SIZE (((512 + TK_BIT_NBITS - 1)/TK_BIT_NBITS)*TK_BIT_NBITS)

void
TkTextReplaceTags(
    TkText *textPtr,		/* Info about overall window. */
    TkTextSegment *segPtr,	/* Setup tag info of this segment. */
    int undoable,		/* Replacement of tags is undoable? */
    Tcl_Obj *tagListPtr)	/* List of tags. */
{
    TkTextTagSet *newTagInfoPtr;
    TkTextTagSet *oldTagInfoPtr;
    TkSharedText *sharedTextPtr;
    TkTextTag *tagArrBuf[TK_TEXT_SET_MAX_BIT_SIZE];
    TkTextTag **tagArrPtr = tagArrBuf;
    TkTextTag *tagPtr;
    TkTextUndoStack undoStack;
    TkTextIndex index[2];
    int altered = 0;
    int anyChanges = 0;
    Tcl_Obj **objs;
    Tcl_Size objn = 0, k;
    unsigned j;

    assert(textPtr);
    assert(segPtr);
    assert(segPtr->tagInfoPtr);
    assert(tagListPtr);

    Tcl_ListObjGetElements(NULL, tagListPtr, &objn, &objs);
    TkTextIndexClear(&index[0], textPtr);
    TkTextIndexSetSegment(&index[0], segPtr);
    TkrTextIndexForwBytes(textPtr, &index[0], 1, &index[1]);
    TkTextTagSetIncrRefCount(oldTagInfoPtr = segPtr->tagInfoPtr);

    if ((size_t)objn > sizeof(tagArrBuf)/sizeof(tagArrBuf[0])) {
	tagArrPtr = (TkTextTag**)ckalloc(objn*sizeof(tagArrPtr[0]));
    }

    for (k = 0; k < objn; ++k) {
	tagArrPtr[k] = TkTextCreateTag(textPtr, Tcl_GetString(objs[k]), NULL);
    }

    sharedTextPtr = textPtr->sharedTextPtr;
    newTagInfoPtr = TkTextTagSetResize(NULL, sharedTextPtr->tagInfoSize);

    for (k = 0; k < objn; ++k) {
	newTagInfoPtr = TkTextTagSetAddToThis(newTagInfoPtr, tagArrPtr[k]->index);
    }

    undoStack = sharedTextPtr->undoStack;
    if (!undoable) {
	sharedTextPtr->undoStack = NULL; /* disable undo temporarily */
    }

    /*
     * Remove the deleted tags, but ignore the "sel" tag.
     */

    for (j = TkTextTagSetFindFirst(oldTagInfoPtr);
	    j != TK_TEXT_TAG_SET_NPOS;
	    j = TkTextTagSetFindNext(oldTagInfoPtr, j)) {
	if (!TkTextTagSetTest(newTagInfoPtr, j)) {
	    tagPtr = sharedTextPtr->tagLookup[j];
	    if (!tagPtr->isSelTag && TagAddRemove(textPtr, &index[0], &index[1], tagPtr, 0)) {
		anyChanges = 1;
		if (tagPtr->undo) {
		    altered = 1;
		}
	    }
	}
    }

    /*
     * Add new tags, but ignore the "sel" tag.
     */

    for (j = TkTextTagSetFindFirst(newTagInfoPtr);
	    j != TK_TEXT_TAG_SET_NPOS;
	    j = TkTextTagSetFindNext(newTagInfoPtr, j)) {
	if (!TkTextTagSetTest(segPtr->tagInfoPtr, j)) {
	    tagPtr = sharedTextPtr->tagLookup[j];
	    if (!tagPtr->isSelTag && TagAddRemove(textPtr, &index[0], &index[1], tagPtr, 1)) {
		anyChanges = 1;
		if (tagPtr->undo) {
		    altered = 1;
		}
	    }
	}
    }

    TkTextTagSetDecrRefCount(oldTagInfoPtr);
    TkTextTagSetDecrRefCount(newTagInfoPtr);
    sharedTextPtr->undoStack = undoStack;

    if (anyChanges) {
	/* still need to trigger enter/leave events on tags that have changed */
	TkTextEventuallyRepick(textPtr);
    }
    if (altered) {
	TkTextUpdateAlteredFlag(sharedTextPtr);
    }
    if (tagArrPtr != tagArrBuf) {
	ckfree(tagArrPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextFindTags --
 *
 *	This function is appending the tags from given char segment to the
 *	interpreter.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Results will be appended to the interpreter.
 *
 *----------------------------------------------------------------------
 */

void
TkTextFindTags(
    Tcl_Interp *interp,		/* Current interpreter. */
    TkText *textPtr,		/* Info about overall widget. */
    const TkTextSegment *segPtr,/* Tags from this segment. */
    int discardSelection)	/* "sel" tag will be discarded? */
{
    TkTextTag *tagPtr;
    Tcl_Obj *listObj;

    assert(segPtr);

    tagPtr = TkBTreeGetSegmentTags(textPtr->sharedTextPtr, segPtr, textPtr,
	    TK_TEXT_SORT_ASCENDING, NULL);
    listObj = Tcl_NewObj();

    for ( ; tagPtr; tagPtr = tagPtr->nextPtr) {
	if (!discardSelection || tagPtr != textPtr->selTagPtr) {
	    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj(tagPtr->name, TCL_INDEX_NONE));
	}
    }

    Tcl_SetObjResult(interp, listObj);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextTagChangedUndoRedo --
 *
 *	This function is called when any tag range has been changed during
 *	an undo/redo operation.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See TkTextRedrawTag, and GrabSelection.
 *
 *----------------------------------------------------------------------
 */

int
TkTextTagChangedUndoRedo(
    const TkSharedText *sharedTextPtr,
    TkText *textPtr,
    const TkTextIndex *indexPtr1,
    const TkTextIndex *indexPtr2,
    const TkTextTag *tagPtr,
    int affectsDisplayGeometry)
{
    if (!TkTextRedrawTag(sharedTextPtr, textPtr, indexPtr1, indexPtr2, tagPtr, affectsDisplayGeometry)) {
	return 0;
    }
    if (textPtr && tagPtr == textPtr->selTagPtr) {
	GrabSelection(tagPtr->textPtr, tagPtr, TkTextTestTag(indexPtr1, tagPtr), 1);
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * GrabSelection --
 * 	Grab the selection if we're supposed to export it and don't already
 * 	have it.
 *
 * 	Also, invalidate partially-completed selection retrievals. We only
 *	need to check whether the tag is "sel" for this textPtr (not for
 *	other peer widget's "sel" tags) because we cannot reach this code
 *	path with a different widget's "sel" tag.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Some text segments may be modified.
 *
 *----------------------------------------------------------------------
 */

static void
GrabSelection(
    TkText *textPtr,		/* Info about overall widget. */
    TCL_UNUSED(const TkTextTag *),	/* Tag which has been modified. */
    int add,			/* 'true' means that we have added the "sel" tag;
				 * 'false' means we have removed the "sel" tag. */
    int changed)		/* 'false' means that the selection has not changed, nevertheless
    				 * the text widget should become the owner again. */
{
    int ownSelection = add && textPtr->exportSelection && !(textPtr->flags & GOT_SELECTION);

    assert(textPtr);

    if (changed || ownSelection) {
	/*
	 * Send an event that the selection changed. This is
	 * equivalent to:
	 *	   event generate $textWidget <<Selection>>
	 */

	TkTextSelectionEvent(textPtr);
    }
    if (ownSelection && (!Tcl_IsSafe(textPtr->interp))) {
	Tk_OwnSelection(textPtr->tkwin, XA_PRIMARY, TkTextLostSelection, textPtr);
	textPtr->flags |= GOT_SELECTION;
    }
    if (changed) {
	textPtr->abortSelections = 1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TagAddRemove --
 *	This functions adds or removes a tag (or all tags) from the characters
 *	between given index range.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Some text segments may be modified.
 *
 *----------------------------------------------------------------------
 */

static int
UndoTagOperation(
    const TkSharedText *sharedTextPtr,
    const TkTextTag *tagPtr)
{
    return sharedTextPtr->undoStack && (!tagPtr || tagPtr->undo);
}

static int
TagAddRemove(
    TkText *textPtr,		/* Info about overall widget. */
    const TkTextIndex *index1Ptr,
				/* Indicates first character in range. */
    const TkTextIndex *index2Ptr,
				/* Indicates character just after the last one in range. */
    TkTextTag *tagPtr,		/* Tag to add or remove. */
    int add)			/* 'true' means add tag to the given range of characters;
				 * 'false' means remove the tag from the range. */
{
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    TkTextUndoInfo *undoInfoPtr;
    TkTextUndoInfo undoInfo;

    assert(!sharedTextPtr->undoStack || !TkTextUndoIsPerformingUndo(sharedTextPtr->undoStack));
    assert(!sharedTextPtr->undoStack || !TkTextUndoIsPerformingRedo(sharedTextPtr->undoStack));

    if (!add && !tagPtr->rootPtr) {
	return 0; /* no change possible */
    }

    undoInfoPtr = UndoTagOperation(sharedTextPtr, tagPtr) ? &undoInfo : NULL;

    if (!TkBTreeTag(sharedTextPtr, textPtr, index1Ptr, index2Ptr, tagPtr, add,
	    undoInfoPtr, TkTextRedrawTag)) {
	return 0;
    }

    if (undoInfoPtr) {
	if (undoInfo.token) {
	    tagPtr->refCount += 1;
	    TkTextUndoPushItem(sharedTextPtr->undoStack, undoInfo.token, undoInfo.byteSize);
	}
	sharedTextPtr->undoStackEvent = 1;
    }

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextBindEvent --
 *
 *	Bind events to the specified resource name.
 *
 * Results:
 *	Any of the standard Tcl return values.
 *
 * Side effects:
 *	A new entry in the binding table will be inserted, or an exisiting
 *	entry will be deleted.
 *
 *----------------------------------------------------------------------
 */

int
TkTextBindEvent(
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[],	/* Remaining argument objects. */
    TkSharedText *sharedTextPtr,/* Shared text resource. */
    Tk_BindingTable *bindingTablePtr,
    				/* Pointer to binding table. */
    const char *name)		/* Bind event to this resource (tag or image). */
{
    static const unsigned motionMask = ButtonMotionMask|Button1MotionMask
		|Button2MotionMask|Button3MotionMask|Button4MotionMask
		|Button5MotionMask|PointerMotionMask;

    /*
     * Make a binding table if the widget doesn't already have one.
     */

    if (!*bindingTablePtr) {
	*bindingTablePtr = Tk_CreateBindingTable(interp);
    }

    if (objc == 2) {
	int append = 0;
	unsigned long mask;
	const char *eventString = Tcl_GetString(objv[0]);
	const char *fifth = Tcl_GetString(objv[1]);

	if (fifth[0] == '\0') {
	    return Tk_DeleteBinding(interp, *bindingTablePtr, (void *)name, eventString);
	}
	if (fifth[0] == '+') {
	    fifth += 1;
	    append = 1;
	}
	mask = Tk_CreateBinding(interp, *bindingTablePtr, (void *)name, eventString, fifth, append);
	if (mask == 0) {
	    return TCL_ERROR;
	}
	if (mask & (unsigned) ~(motionMask|ButtonPressMask|ButtonReleaseMask|EnterWindowMask
		|LeaveWindowMask|KeyPressMask|KeyReleaseMask|VirtualEventMask)) {
	    Tk_DeleteBinding(interp, *bindingTablePtr, (void *)name, eventString);
	    Tcl_ResetResult(interp);
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "requested illegal events; only key, button, motion,"
		    " enter, leave, and virtual events may be used", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "TEXT", "TAG_BIND_EVENT",NULL);
	    return TCL_ERROR;
	}
	if (mask & motionMask) {
	    /*
	     * TODO: It would be better to count tags with motion mask, but this silly
	     * binding protocol does not provide any function which helps to detect when
	     * bindings with motion masks will be deleted. So we cannot do more than
	     * to detect whether any motion mask has ever been set. This has an effect
	     * on TkTextPickCurrent, this function will be considerably faster if
	     * 'numMotionEventBindings' is zero, because in latter case only traversals
	     * between display chunks will be considered. We assume that the use of a
	     * motion mask is rather seldom, normally only the Enter/Leave events are
	     * of interest.
	     */
	    sharedTextPtr->numMotionEventBindings = 1;
	}
    } else if (objc == 1) {
	const char *command;

	command = Tk_GetBinding(interp, *bindingTablePtr, (void *)name, Tcl_GetString(objv[0]));
	if (!command) {
	    const char *string = Tcl_GetString(Tcl_GetObjResult(interp));

	    /*
	     * Ignore missing binding errors. This is a special hack that relies on the
	     * error message returned by FindSequence in tkBind.c.
	     */

	    if (string[0] != '\0') {
		return TCL_ERROR;
	    }
	    Tcl_ResetResult(interp);
	} else {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(command, TCL_INDEX_NONE));
	}
    } else {
	Tk_GetAllBindings(interp, *bindingTablePtr, (void *)name);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextCreateTag --
 *
 *	Find the record describing a tag within a given text widget, creating
 *	a new record if one doesn't already exist.
 *
 * Results:
 *	The return value is a pointer to the TkTextTag record for tagName.
 *
 * Side effects:
 *	A new tag record is created if there isn't one already defined for
 *	tagName.
 *
 *----------------------------------------------------------------------
 */

static void
MarkIndex(
    TkSharedText *sharedTextPtr,
    TkTextTag *tagPtr,
    int set)
{
    if (set && tagPtr->index >= TkBitSize(sharedTextPtr->usedTags)) {
	sharedTextPtr->tagInfoSize = TkBitAdjustSize(tagPtr->index + 1);
    }

    TkBitPut(sharedTextPtr->usedTags, tagPtr->index, set);
    assert((!sharedTextPtr->tagLookup[tagPtr->index]) == set);
    sharedTextPtr->tagLookup[tagPtr->index] = set ? tagPtr : NULL;
}

TkTextTag *
TkTextCreateTag(
    TkText *textPtr,		/* Widget in which tag is being used. */
    const char *tagName,	/* Name of desired tag. */
    int *newTag)		/* If non-NULL, then return true if new, or false if already exists. */
{
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    TkTextTag *tagPtr;
    Tcl_HashEntry *hPtr = NULL;
    int isNew;
    int isSelTag;
    const char *name;
    unsigned index;

    isSelTag = (strcmp(tagName, "sel") == 0);

    if (isSelTag) {
	if (textPtr->selTagPtr) {
	    if (newTag) {
		*newTag = 0;
	    }
	    return textPtr->selTagPtr;
	}
	if (newTag) {
	    *newTag = 1;
	}
	name = "sel";
    } else {
	hPtr = Tcl_CreateHashEntry(&sharedTextPtr->tagTable, tagName, &isNew);
	if (newTag) {
	    *newTag = isNew;
	}
	if (!isNew) {
	    return (TkTextTag*)Tcl_GetHashValue(hPtr);
	}
	name = (const char *)Tcl_GetHashKey(&sharedTextPtr->tagTable, hPtr);
    }

    if ((index = TkBitFindFirstNot(sharedTextPtr->usedTags)) == TK_BIT_NPOS) {
	unsigned oldSize = TkBitSize(sharedTextPtr->usedTags);
	unsigned newSize = TkBitAdjustSize((index = oldSize) + 1);

	sharedTextPtr->usedTags = TkBitResize(sharedTextPtr->usedTags, newSize);
	sharedTextPtr->elisionTags = TkBitResize(sharedTextPtr->elisionTags, newSize);
	sharedTextPtr->selectionTags = TkBitResize(sharedTextPtr->selectionTags, newSize);
	sharedTextPtr->dontUndoTags = TkBitResize(sharedTextPtr->dontUndoTags, newSize);
	sharedTextPtr->affectDisplayTags = TkBitResize(sharedTextPtr->affectDisplayTags, newSize);
	sharedTextPtr->notAffectDisplayTags = TkBitResize(sharedTextPtr->notAffectDisplayTags, newSize);
	sharedTextPtr->affectDisplayNonSelTags = TkBitResize(
		sharedTextPtr->affectDisplayNonSelTags, newSize);
	sharedTextPtr->affectGeometryTags = TkBitResize( sharedTextPtr->affectGeometryTags, newSize);
	sharedTextPtr->affectGeometryNonSelTags = TkBitResize(
		sharedTextPtr->affectGeometryNonSelTags, newSize);
	sharedTextPtr->affectLineHeightTags = TkBitResize(sharedTextPtr->affectLineHeightTags, newSize);
	sharedTextPtr->tagLookup = (TkTextTag **)ckrealloc(sharedTextPtr->tagLookup, newSize * sizeof(TkTextTag *));
	DEBUG(memset(sharedTextPtr->tagLookup + oldSize, 0, (newSize - oldSize) * sizeof(TkTextTag *)));
    }

    if (sharedTextPtr->tagInfoSize <= index) {
	sharedTextPtr->tagInfoSize = TkBitAdjustSize(index + 1);
    }

    /*
     * No existing entry. Create a new one, initialize it, and add a pointer
     * to it to the hash table entry.
     */

    tagPtr = (TkTextTag *)ckalloc(sizeof(TkTextTag));
    memset(tagPtr, 0, sizeof(TkTextTag));
    tagPtr->name = name;
    tagPtr->index = index;
    tagPtr->priority = sharedTextPtr->numEnabledTags;
    tagPtr->isSelTag = isSelTag;
    tagPtr->bgStipple = None;
    tagPtr->fgStipple = None;
    tagPtr->justify = TK_JUSTIFY_NULL;
    tagPtr->relief = TK_RELIEF_NULL;
    tagPtr->tabStyle = TK_TEXT_TABSTYLE_NULL;
    tagPtr->wrapMode = TEXT_WRAPMODE_NULL;
    tagPtr->undo = sharedTextPtr->undoTagging && !isSelTag;
    tagPtr->sharedTextPtr = sharedTextPtr;
    tagPtr->undoTagListIndex = -1;
    tagPtr->refCount = 1;
    tagPtr->tagEpoch = ++sharedTextPtr->tagEpoch;
    DEBUG_ALLOC(tkTextCountNewTag++);

    tagPtr->optionTable = Tk_CreateOptionTable(textPtr->interp, tagOptionSpecs);

    sharedTextPtr->numTags += 1;
    sharedTextPtr->numEnabledTags += 1;

    if (isSelTag) {
	tagPtr->textPtr = textPtr;
	textPtr->refCount += 1;
	TkBitSet(sharedTextPtr->selectionTags, index);
	TkBitSet(sharedTextPtr->dontUndoTags, index);
    } else {
	assert(hPtr);
	Tcl_SetHashValue(hPtr, tagPtr);
    }

    SetupDefaultRelief(textPtr, tagPtr);
    MarkIndex(sharedTextPtr, tagPtr, 1);
    return tagPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextFindTag --
 *
 *	See if tag is defined for a given widget.
 *
 * Results:
 *	If tagName is defined in textPtr, a pointer to its TkTextTag structure
 *	is returned. Otherwise NULL is returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkTextTag *
TkTextFindTag(
    const TkText *textPtr,	/* Widget in which tag is being used. */
    const char *tagName)	/* Name of desired tag. */
{
    Tcl_HashEntry *hPtr;

    assert(textPtr);
    assert(tagName);

    if (strcmp(tagName, "sel") == 0) {
	return textPtr->selTagPtr;
    }
    hPtr = Tcl_FindHashEntry(&textPtr->sharedTextPtr->tagTable, tagName);
    if (hPtr) {
	return (TkTextTag *)Tcl_GetHashValue(hPtr);
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FindTag --
 *
 *	See if tag is defined for a given widget.
 *
 * Results:
 *	If tagName is defined in textPtr, a pointer to its TkTextTag structure
 *	is returned. Otherwise NULL is returned and an error message is
 *	recorded in the interp's result unless interp is NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkTextTag *
FindTag(
    Tcl_Interp *interp,		/* Interpreter to use for error message; if NULL, then don't record
    				 * an error message. */
    const TkText *textPtr,	/* Widget in which tag is being used. */
    Tcl_Obj *tagName)		/* Name of desired tag. */
{
    const char *name = Tcl_GetString(tagName);
    TkTextTag *tagPtr = TkTextFindTag(textPtr, name);

    if (!tagPtr && interp) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"tag \"%s\" isn't defined in text widget", name));
	Tcl_SetErrorCode(interp, "TK", "LOOKUP", "TEXT_TAG", name, NULL);
    }

    return tagPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextEnableTag --
 *
 *	If this tag is disabled, then re-enable it.
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
TkTextEnableTag(
    TkSharedText *sharedTextPtr,/* Shared text resource. */
    TkTextTag *tagPtr)		/* Tag being deleted. */
{
    if (tagPtr->isDisabled) {
	tagPtr->isDisabled = 0;
	MarkIndex(sharedTextPtr, tagPtr, 1);
	sharedTextPtr->numEnabledTags += 1;
	ChangeTagPriority(sharedTextPtr, tagPtr, tagPtr->savedPriority, 0);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextReleaseTag --
 *
 *	Delete this tag if the reference counter is going to zero, in this
 *	case clean up the tag structure itself. This requires that the given
 *	tag is not in use.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory and other resources are freed.
 *
 *----------------------------------------------------------------------
 */

void
TkTextReleaseTag(
    TkSharedText *sharedTextPtr,/* Shared text resource. */
    TkTextTag *tagPtr,		/* Tag being deleted. */
    Tcl_HashEntry *hPtr)	/* Pointer into hash table, can be NULL. */
{
    assert(tagPtr->refCount > 1 || !tagPtr->rootPtr);

    if (--tagPtr->refCount > 0) {
	return;
    }

    assert(!tagPtr->recentTagAddRemoveToken);
    assert(!tagPtr->recentChangePriorityToken);

    MarkIndex(sharedTextPtr, tagPtr, 0);
    sharedTextPtr->numTags -= 1;

    if (!hPtr) {
	hPtr = Tcl_FindHashEntry(&sharedTextPtr->tagTable, tagPtr->name);
    }
    if (hPtr) {
	Tcl_DeleteHashEntry(hPtr);
    } else {
	assert(tagPtr->isSelTag);
    }

    /*
     * Let Tk do most of the hard work for us.
     */

    if (tagPtr->isSelTag) {
	assert(tagPtr->textPtr);
	/* Restore the original values. */
	tagPtr->attrs = tagPtr->textPtr->selTagConfigAttrs;
    }
    Tk_FreeConfigOptions((char *) tagPtr, tagPtr->optionTable, sharedTextPtr->peers->tkwin);

    /*
     * This associated information is managed by us.
     */

    if (tagPtr->tabArrayPtr) {
	ckfree(tagPtr->tabArrayPtr);
    }

    if (sharedTextPtr->tagBindingTable) {
	Tk_DeleteAllBindings(sharedTextPtr->tagBindingTable, (void *)tagPtr->name);
    }

    /*
     * If this tag is widget-specific (peer widgets) then clean up the
     * refCount it holds.
     */

    if (tagPtr->textPtr) {
	TkTextDecrRefCountAndTestIfDestroyed((TkText *) tagPtr->textPtr);
	tagPtr->textPtr = NULL;
    }

    /*
     * Finally free the tag's memory.
     */

    ckfree(tagPtr);
    DEBUG_ALLOC(tkTextCountDestroyTag++);
}
/*
 *----------------------------------------------------------------------
 *
 * TkTextDeleteTag --
 *
 *	This function is called to carry out most actions associated with the
 *	'tag delete' sub-command. It will remove all evidence of the tag from
 *	the B-tree, and then clean up the tag structure itself.
 *
 *	The only actions this doesn't carry out it to check if the deletion of
 *	the tag requires something to be re-displayed, and to remove the tag
 *	from the tagTable (hash table) if that is necessary (i.e. if it's not
 *	the 'sel' tag). It is expected that the caller carry out both of these
 *	actions.
 *
 * Results:
 *	Returns whether this tag was used in current text content.
 *
 * Side effects:
 *	Memory and other resources are freed, the B-tree is manipulated.
 *
 *----------------------------------------------------------------------
 */

int
TkTextDeleteTag(
    TkText *textPtr,		/* Info about overall widget. */
    TkTextTag *tagPtr,		/* Tag being deleted. */
    Tcl_HashEntry *hPtr)	/* Pointer into hash table, can be NULL (but only for "sel"). */
{
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    int used;

    assert(!sharedTextPtr->undoStack || !TkTextUndoIsPerformingUndo(sharedTextPtr->undoStack));
    assert(!sharedTextPtr->undoStack || !TkTextUndoIsPerformingRedo(sharedTextPtr->undoStack));
    assert(hPtr || tagPtr->isSelTag);

    used = !!tagPtr->rootPtr;

    if (used) {
	TkTextUndoInfo undoInfo;
	TkTextUndoInfo *undoInfoPtr;
	TkTextIndex startIndex;
	TkTextIndex index[2];
	TkTextSearch tSearch;
	int useUndo = !!(textPtr->flags & DESTROYED) && UndoTagOperation(sharedTextPtr, tagPtr);

	undoInfoPtr = useUndo ? &undoInfo : NULL;

	TkTextIndexSetupToStartOfText(&index[0], NULL, sharedTextPtr->tree);
	TkTextIndexSetupToEndOfText(&index[1], NULL, sharedTextPtr->tree);

	TkBTreeStartSearch(&index[0], &index[1], tagPtr, &tSearch, SEARCH_NEXT_TAGON);
	TkBTreeNextTag(&tSearch);
	assert(tSearch.segPtr); /* last search must not fail */
	startIndex = tSearch.curIndex;

	TkBTreeStartSearchBack(&index[1], &index[0], tagPtr, &tSearch, SEARCH_EITHER_TAGON_TAGOFF);
	TkBTreePrevTag(&tSearch);
	assert(tSearch.segPtr); /* last search must not fail */
	assert(!tSearch.tagon); /* we must find tagoff */

	TkBTreeTag(textPtr->sharedTextPtr, textPtr, &startIndex, &tSearch.curIndex,
		tagPtr, 0, undoInfoPtr, TkTextRedrawTag);

	if (undoInfoPtr && undoInfoPtr->token) {
	    tagPtr->refCount += 1;
	    TkTextUndoPushItem(sharedTextPtr->undoStack, undoInfo.token, undoInfo.byteSize);
	}
    }

    assert(!tagPtr->rootPtr);

    if (!(textPtr->flags & DESTROYED) && tagPtr->isSelTag) {
	/*
	 * Send an event that the selection changed. This is equivalent to:
	 *	event generate $textWidget <<Selection>>
	 */

	TkTextSelectionEvent(textPtr);
    }

    /*
     * Update the tag priorities to reflect the deletion of this tag.
     */

    tagPtr->savedPriority = tagPtr->priority;
    ChangeTagPriority(sharedTextPtr, tagPtr, sharedTextPtr->numEnabledTags - 1, 0);
    sharedTextPtr->numEnabledTags -= 1;

    /*
     * Make sure this tag isn't referenced from the 'current' tag array.
     */

    if (tagPtr->index < TkTextTagSetSize(textPtr->curTagInfoPtr)) {
	textPtr->curTagInfoPtr = TkTextTagSetErase(textPtr->curTagInfoPtr, tagPtr->index);
    }

    /*
     * Handle the retained undo tokens.
     */

    if (tagPtr->undoTagListIndex >= 0) {
	if (sharedTextPtr->undoStack) {
	    TkTextPushUndoTagTokens(sharedTextPtr, tagPtr);
	} else {
	    TkTextReleaseUndoTagToken(sharedTextPtr, tagPtr);
	}
    }

    tagPtr->isDisabled = 1;
    TkTextReleaseTag(sharedTextPtr, tagPtr, hPtr);
    return used;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextFreeAllTags --
 *
 *	This function is called when all tags are deleted to free up the memory
 *	and other resources associated with tags.
 *
 *	Note that this function is not freeing the indices
 *	('sharedTextPtr->usedTags', 'sharedTextPtr->elisionTags'), but both
 *	sets will be cleared.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory and other resources are freed.
 *
 *----------------------------------------------------------------------
 */

void
TkTextFreeAllTags(
    TkText *textPtr)		/* Info about overall widget. */
{
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;

    DEBUG(textPtr->refCount += 1);

    for (hPtr = Tcl_FirstHashEntry(&sharedTextPtr->tagTable, &search);
	    hPtr;
	    hPtr = Tcl_NextHashEntry(&search)) {
	TkTextTag *tagPtr = (TkTextTag *)Tcl_GetHashValue(hPtr);

	assert(tagPtr->refCount == 1);

	/*
	 * Let Tk do most of the hard work for us.
	 */

	if (tagPtr->isSelTag) {
	    assert(tagPtr->textPtr);
	    tagPtr->attrs = tagPtr->textPtr->selTagConfigAttrs;
	}
	Tk_FreeConfigOptions((char *) tagPtr, tagPtr->optionTable, textPtr->tkwin);

	/*
	 * This associated information is managed by us.
	 */

	if (tagPtr->tabArrayPtr) {
	    ckfree(tagPtr->tabArrayPtr);
	}

	if (tagPtr->undoTagListIndex >= 0) {
	    TkTextReleaseUndoTagToken(sharedTextPtr, tagPtr);
	}

	/*
	 * If this tag is widget-specific (peer widgets) then clean up the
	 * refCount it holds.
	 */

	if (tagPtr->textPtr) {
	    assert(textPtr == tagPtr->textPtr);
	    textPtr->refCount -= 1;
	    tagPtr->textPtr = NULL;
	}

	/*
	 * Finally free the tag's memory.
	 */

	ckfree(tagPtr);
	DEBUG_ALLOC(tkTextCountDestroyTag++);
    }

    TkTextTagSetDecrRefCount(textPtr->curTagInfoPtr);
    TkTextTagSetIncrRefCount(textPtr->curTagInfoPtr = sharedTextPtr->emptyTagInfoPtr);

    TkBitClear(sharedTextPtr->usedTags);
    TkBitClear(sharedTextPtr->elisionTags);
    TkBitClear(sharedTextPtr->affectDisplayTags);
    TkBitClear(sharedTextPtr->notAffectDisplayTags);
    TkBitClear(sharedTextPtr->affectDisplayNonSelTags);
    TkBitClear(sharedTextPtr->affectGeometryTags);
    TkBitClear(sharedTextPtr->affectGeometryNonSelTags);
    TkBitClear(sharedTextPtr->affectLineHeightTags);

    DEBUG(textPtr->refCount -= 1);
    assert(textPtr->refCount > 0);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextSortTags --
 *
 *	This function sorts an array of tag pointers in increasing order of
 *	priority, optimizing for the common case where the array is small.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TagSortProc(
    const void *first,
    const void *second)		/* Elements to be compared. */
{
    return (int) (*(TkTextTag **) first)->priority - (int) (*(TkTextTag **) second)->priority;
}

void
TkTextSortTags(
    unsigned numTags,		/* Number of tag pointers at *tagArrayPtr. */
    TkTextTag **tagArrayPtr)	/* Pointer to array of pointers. */
{
    unsigned i, j, prio;
    TkTextTag **tagPtrPtr;
    TkTextTag **maxPtrPtr;
    TkTextTag *tmp;

    if (numTags <= 1) {
	return;
    }
    if (numTags <= 20) {
	for (i = numTags - 1; i > 0; i--, tagArrayPtr++) {
	    maxPtrPtr = tagPtrPtr = tagArrayPtr;
	    prio = tagPtrPtr[0]->priority;
	    for (j = i, tagPtrPtr += 1; j > 0; --j, ++tagPtrPtr) {
		if (tagPtrPtr[0]->priority < prio) {
		    prio = tagPtrPtr[0]->priority;
		    maxPtrPtr = tagPtrPtr;
		}
	    }
	    tmp = *maxPtrPtr;
	    *maxPtrPtr = *tagArrayPtr;
	    *tagArrayPtr = tmp;
	}
    } else {
	qsort(tagArrayPtr, numTags, sizeof(TkTextTag *), TagSortProc);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextReleaseUndoTagToken --
 *
 *	Release retained undo tokens for tag operations.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Free some memory.
 *
 *----------------------------------------------------------------------
 */

void
TkTextReleaseUndoTagToken(
    TkSharedText *sharedTextPtr,
    TkTextTag *tagPtr)
{
    assert(sharedTextPtr);

    if (!tagPtr) {
	return;
    }

    assert(tagPtr->undoTagListIndex >= 0);
    assert(tagPtr->undoTagListIndex < (int) sharedTextPtr->undoTagListCount);

    if (tagPtr->recentTagAddRemoveToken) {
	ckfree(tagPtr->recentTagAddRemoveToken);
	DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
	tagPtr->recentTagAddRemoveToken = NULL;
    }
    if (tagPtr->recentChangePriorityToken) {
	ckfree(tagPtr->recentChangePriorityToken);
	DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
	tagPtr->recentChangePriorityToken = NULL;
    }

    sharedTextPtr->undoTagList[tagPtr->undoTagListIndex] = NULL;
    tagPtr->undoTagListIndex = -1;
    assert(tagPtr->refCount > 1);
    tagPtr->refCount -= 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextInspectUndoTagItem --
 *
 *	Inspect retained undo token.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is allocated for the result.
 *
 *----------------------------------------------------------------------
 */

void
TkTextInspectUndoTagItem(
    const TkSharedText *sharedTextPtr,
    const TkTextTag *tagPtr,
    Tcl_Obj* objPtr)
{
    if (tagPtr) {
	if (tagPtr->recentTagAddRemoveToken && !tagPtr->recentTagAddRemoveTokenIsNull) {
	    Tcl_ListObjAppendElement(NULL, objPtr,
		    TkBTreeUndoTagInspect(sharedTextPtr, tagPtr->recentTagAddRemoveToken));
	}
	if (tagPtr->recentChangePriorityToken) {
	    Tcl_ListObjAppendElement(NULL, objPtr,
		    UndoChangeTagPriorityInspect(sharedTextPtr, tagPtr->recentChangePriorityToken));
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextPushUndoTagTokens --
 *
 *	Push retained undo tokens for tag operations onto the undo stack.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Same as TkTextUndoPushItem.
 *
 *----------------------------------------------------------------------
 */

void
TkTextPushUndoTagTokens(
    TkSharedText *sharedTextPtr,
    TkTextTag *tagPtr)
{
    assert(sharedTextPtr);
    assert(sharedTextPtr->undoStack);

    if (!tagPtr) {
	return;
    }

    assert(tagPtr->undoTagListIndex >= 0);
    assert(tagPtr->undoTagListIndex < (int) sharedTextPtr->undoTagListCount);

    if (tagPtr->recentTagAddRemoveToken) {
	if (tagPtr->recentTagAddRemoveTokenIsNull) {
	    ckfree(tagPtr->recentTagAddRemoveToken);
	    DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
	} else {
	    TkTextUndoPushItem(sharedTextPtr->undoStack, tagPtr->recentTagAddRemoveToken, 0);
	    tagPtr->refCount += 1;
	}
	tagPtr->recentTagAddRemoveToken = NULL;
    }
    if (tagPtr->recentChangePriorityToken) {
	if (tagPtr->savedPriority != tagPtr->priority) {
	    TkTextUndoPushItem(sharedTextPtr->undoStack, tagPtr->recentChangePriorityToken, 0);
	    tagPtr->refCount += 1;
	} else {
	    ckfree(tagPtr->recentChangePriorityToken);
	    DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
	}
	tagPtr->recentChangePriorityToken = NULL;
    }

    sharedTextPtr->undoTagList[tagPtr->undoTagListIndex] = NULL;
    tagPtr->undoTagListIndex = -1;
    assert(tagPtr->refCount > 1);
    tagPtr->refCount -= 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextTagAddRetainedUndo --
 *
 *	Add given tag to undo list, because this tag has retained undo
 *	tokens.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The reference counter of the tag will be incremented.
 *
 *----------------------------------------------------------------------
 */

void
TkTextTagAddRetainedUndo(
    TkSharedText *sharedTextPtr,	/* Shared text resource. */
    TkTextTag *tagPtr)			/* Add this tag to undo list. */
{
    assert(sharedTextPtr);
    assert(tagPtr);

    if (tagPtr->undoTagListIndex >= 0) {
	return;
    }

    if (sharedTextPtr->undoTagListCount == sharedTextPtr->undoTagListSize) {
	sharedTextPtr->undoTagListSize = 2*sharedTextPtr->numEnabledTags;
	sharedTextPtr->undoTagList = (TkTextTag**)ckrealloc(sharedTextPtr->undoTagList,
		sharedTextPtr->undoTagListSize * sizeof(sharedTextPtr->undoTagList[0]));
    }
    sharedTextPtr->undoTagList[sharedTextPtr->undoTagListCount] = tagPtr;
    sharedTextPtr->undoStackEvent = 1;
    sharedTextPtr->lastUndoTokenType = -1;
    tagPtr->undoTagListIndex = sharedTextPtr->undoTagListCount++;
    tagPtr->refCount += 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextPushTagPriorityUndo --
 *
 *	This function is pushing an undo item for setting the priority
 *	of a mark (raise/lower command).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Some memory will be allocated, and see TkTextPushUndoToken.
 *
 *----------------------------------------------------------------------
 */

void
TkTextPushTagPriorityUndo(
    TkSharedText *sharedTextPtr,
    TkTextTag *tagPtr,
    unsigned priority)
{
    UndoTokenTagPriority *token;

    token = (UndoTokenTagPriority *)ckalloc(sizeof(UndoTokenTagPriority));
    token->undoType = &undoTokenTagPriorityType;
    (token->tagPtr = tagPtr)->refCount += 1;
    token->priority = priority;
    DEBUG_ALLOC(tkTextCountNewUndoToken++);

    TkTextPushUndoToken(sharedTextPtr, token, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextPushTagPriorityRedo --
 *
 *	This function is pushing a redo item for setting the priority
 *	of a mark (raise/lower command).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Some memory will be allocated, and see TkTextPushRedoToken.
 *
 *----------------------------------------------------------------------
 */

void
TkTextPushTagPriorityRedo(
    TkSharedText *sharedTextPtr,
    TkTextTag *tagPtr,
    unsigned priority)
{
    UndoTokenTagPriority *token;

    token = (UndoTokenTagPriority *)ckalloc(sizeof(UndoTokenTagPriority));
    token->undoType = &redoTokenTagPriorityType;
    (token->tagPtr = tagPtr)->refCount += 1;
    token->priority = priority;
    DEBUG_ALLOC(tkTextCountNewUndoToken++);

    TkTextPushRedoToken(sharedTextPtr, token, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * ChangeTagPriority --
 *
 *	This function changes the priority of a tag by modifying its priority
 *	and the priorities of other tags that are affected by the change.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Priorities may be changed for some or all of the tags in textPtr. The
 *	tags will be arranged so that there is exactly one tag at each
 *	priority level between 0 and textPtr->sharedTextPtr->numEnabledTags-1,
 *	with tagPtr at priority "newPriority".
 *
 *----------------------------------------------------------------------
 */

static int
ChangeTagPriority(
    TkSharedText *sharedTextPtr,/* Shared text resource. */
    TkTextTag *tagPtr,		/* Tag whose priority is to be changed. */
    unsigned newPriority,	/* New priority for tag. */
    int undo)			/* Push undo item for this action? */
{
    int delta;
    unsigned low, high;
    TkTextTag *tagPtr2;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    TkText *peer;

    assert(newPriority < sharedTextPtr->numEnabledTags);

    if (newPriority == tagPtr->priority) {
	return 0;
    }

    if (undo && tagPtr->undo && !TkTextUndoStackIsFull(sharedTextPtr->undoStack)) {
	UndoTokenTagPriority *token = (UndoTokenTagPriority *) tagPtr->recentChangePriorityToken;

	/*
	 * Don't push changes of tag priorities immediately onto the undo stack, this
	 * may blow up the stack. We save this undo token inside the tag, in this way
	 * only the relevant changes will be pushed as soon as a separator will be
	 * pushed.
	 */

	if (!tagPtr->recentChangePriorityToken) {
	    tagPtr->savedPriority = tagPtr->priority;
	    token = (UndoTokenTagPriority *)ckalloc(sizeof(UndoTokenTagPriority));
	    DEBUG_ALLOC(tkTextCountNewUndoToken++);
	    tagPtr->recentChangePriorityToken = (TkTextUndoToken *) token;
	    TkTextTagAddRetainedUndo(sharedTextPtr, tagPtr);
	}

	token->undoType = &undoTokenTagPriorityType;
	token->tagPtr = tagPtr;
	token->priority = tagPtr->priority;
    }

    if (newPriority < tagPtr->priority) {
	low = newPriority;
	high = tagPtr->priority - 1;
	delta = 1;
    } else {
	low = tagPtr->priority + 1;
	high = newPriority;
	delta = -1;
    }

    /*
     * Adjust first the 'sel' tag, then all others from the hash table
     */

    for (peer = sharedTextPtr->peers; peer; peer = peer->next) {
	if (low <= peer->selTagPtr->priority && peer->selTagPtr->priority <= high) {
	    peer->selTagPtr->priority += delta;
	}
    }

    for (hPtr = Tcl_FirstHashEntry(&sharedTextPtr->tagTable, &search);
	    hPtr;
	    hPtr = Tcl_NextHashEntry(&search)) {
	tagPtr2 = (TkTextTag *)Tcl_GetHashValue(hPtr);
	if (low <= tagPtr2->priority && tagPtr2->priority <= high) {
	    tagPtr2->priority += delta;
	}
    }

    tagPtr->priority = newPriority;

    return 1;
}

/*
 *--------------------------------------------------------------
 *
 * TkTextBindProc --
 *
 *	This function is invoked by the Tk dispatcher to handle events
 *	associated with bindings on items.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on the command invoked as part of the binding (if there was
 *	any).
 *
 *--------------------------------------------------------------
 */

void
TkTextBindProc(
    void *clientData,	/* Pointer to text widget structure. */
    XEvent *eventPtr)		/* Pointer to X event that just happened. */
{
    TkText *textPtr = (TkText *)clientData;
    int dontRepick = textPtr->dontRepick;
    int repick = 0;

    textPtr->refCount++;

    /*
     * This code simulates grabs for mouse buttons by keeping track of whether
     * a button is pressed and refusing to pick a new current character while
     * a button is pressed.
     */

    if (eventPtr->type == ButtonPress) {
	textPtr->flags |= BUTTON_DOWN;
    } else if (eventPtr->type == ButtonRelease) {
	unsigned long mask;

	mask = Tk_GetButtonMask(eventPtr->xbutton.button);
	if ((eventPtr->xbutton.state & ALL_BUTTONS) == mask) {
	    textPtr->flags &= ~BUTTON_DOWN;
	    repick = 1;
	    if (eventPtr->xbutton.state & (Button1|Button2|Button3)) {
		textPtr->dontRepick = 0; /* in case of button clicks we must repick */
	    }
	}
    } else if ((eventPtr->type == EnterNotify)
	    || (eventPtr->type == LeaveNotify)) {
	if (eventPtr->xcrossing.state & ALL_BUTTONS) {
	    textPtr->flags |= BUTTON_DOWN;
	} else {
	    textPtr->flags &= ~BUTTON_DOWN;
	}
	TkTextPickCurrent(textPtr, eventPtr);
	goto done;
    } else if (eventPtr->type == MotionNotify) {
	if (eventPtr->xmotion.state & ALL_BUTTONS) {
	    textPtr->flags |= BUTTON_DOWN;
	} else {
	    textPtr->flags &= ~BUTTON_DOWN;
	}
	TkTextPickCurrent(textPtr, eventPtr);
    }
    if (!(textPtr->flags & DESTROYED)) {
	const TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;

	if (sharedTextPtr->tagBindingTable) {
	    if (!TkTextTagSetIsEmpty(textPtr->curTagInfoPtr)) {
		/*
		 * The mouse is inside the text widget, the 'current' mark was updated.
		 */

		TagBindEvent(textPtr, eventPtr, textPtr->curTagInfoPtr, sharedTextPtr->tagEpoch);
	    } else if ((eventPtr->type == KeyPress) || (eventPtr->type == KeyRelease)) {
		 /*
		  * Key events fire independently of the 'current' mark and use the
		  * 'insert' mark.
		  */

		TkTextIndex index;
		TkTextTagSet *insertTags;

		TkTextMarkNameToIndex(textPtr, "insert", &index);
		insertTags = TkTextIndexGetContentSegment(&index, NULL)->tagInfoPtr;
		if (!TkTextTagSetIsEmpty(insertTags)) {
		    TagBindEvent(textPtr, eventPtr, insertTags, sharedTextPtr->tagEpoch);
		}
	    }
	    if (textPtr->flags & DESTROYED) {
		TkTextDecrRefCountAndTestIfDestroyed(textPtr);
		return;
	    }
	}
    }
    if (repick) {
	unsigned int oldState;

	oldState = eventPtr->xbutton.state;
	eventPtr->xbutton.state &= ~ALL_BUTTONS;
	if (!(textPtr->flags & DESTROYED)) {
	    TkTextPickCurrent(textPtr, eventPtr);
	}
	eventPtr->xbutton.state = oldState;
    }

  done:
    textPtr->dontRepick = dontRepick;
    TkTextDecrRefCountAndTestIfDestroyed(textPtr);
}

/*
 *--------------------------------------------------------------
 *
 * TkTextPickCurrent --
 *
 *	Find the character containing the coordinates in an event and place
 *	the "current" mark on that character. If the "current" mark has moved
 *	then generate a fake leave event on the old current character and a
 *	fake enter event on the new current character.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current mark for textPtr may change. If it does, then the commands
 *	associated with character entry and leave could do just about
 *	anything. For example, the text widget might be deleted. It is up to
 *	the caller to protect itself by incrementing the refCount of the text
 *	widget.
 *
 *--------------------------------------------------------------
 */

static int
DispChunkContainsX(
    TkText *textPtr,
    const TkTextDispChunk *chunkPtr,
    int x)
{
    int cx = TkTextGetXPixelFromChunk(textPtr, chunkPtr);
    return cx <= x && x < cx + chunkPtr->width;
}

void
TkTextPickCurrent(
    TkText *textPtr,		/* Text widget in which to select current character. */
    XEvent *eventPtr)		/* Event describing location of mouse cursor.
				 * Must be EnterWindow, LeaveWindow, ButtonRelease, or MotionNotify. */
{
    TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    int sameChunkWithUnchangedTags = 0;
    TkTextTagSet *newTagInfoPtr;
    TkTextTagSet *leaveTags;
    TkTextTagSet *enterTags;
    const TkTextDispChunk *newDispChunkPtr = NULL;
    int nearby = 0;
    int sentEvents = 0;
    int newLineY = TK_TEXT_IS_NEARBY;
    TkTextIndex index;
    XEvent event;
    unsigned tagEpoch;

    assert(!(textPtr->flags & DESTROYED));

    /*
     * If a button is down, then don't do anything at all; we'll be called
     * again when all buttons are up, and we can repick then. This implements
     * a form of mouse grabbing.
     */

    if (textPtr->flags & BUTTON_DOWN) {
	if ((eventPtr->type != EnterNotify && eventPtr->type != LeaveNotify)
		|| (eventPtr->xcrossing.mode != NotifyGrab
		    && eventPtr->xcrossing.mode != NotifyUngrab)) {
	    return;
	}

	/*
	 * Special case: the window is being entered or left because of a
	 * grab or ungrab. In this case, repick after all. Furthermore,
	 * clear BUTTON_DOWN to release the simulated grab.
	 */

	textPtr->flags &= ~BUTTON_DOWN;
    }

    /*
     * Save information about this event in the widget in case we have to
     * synthesize more enter and leave events later (e.g. because a character
     * was deleted, causing a new character to be underneath the mouse
     * cursor). Also translate MotionNotify events into EnterNotify events,
     * since that's what gets reported to event handlers when the current
     * character changes.
     */

    if (eventPtr != &textPtr->pickEvent) {
	if (eventPtr->type == MotionNotify || eventPtr->type == ButtonRelease) {
	    textPtr->pickEvent.xcrossing.type = EnterNotify;
	    textPtr->pickEvent.xcrossing.serial = eventPtr->xmotion.serial;
	    textPtr->pickEvent.xcrossing.send_event = eventPtr->xmotion.send_event;
	    textPtr->pickEvent.xcrossing.display = eventPtr->xmotion.display;
	    textPtr->pickEvent.xcrossing.window = eventPtr->xmotion.window;
	    textPtr->pickEvent.xcrossing.root = eventPtr->xmotion.root;
	    textPtr->pickEvent.xcrossing.subwindow = None;
	    textPtr->pickEvent.xcrossing.time = eventPtr->xmotion.time;
	    textPtr->pickEvent.xcrossing.x = eventPtr->xmotion.x;
	    textPtr->pickEvent.xcrossing.y = eventPtr->xmotion.y;
	    textPtr->pickEvent.xcrossing.x_root = eventPtr->xmotion.x_root;
	    textPtr->pickEvent.xcrossing.y_root = eventPtr->xmotion.y_root;
	    textPtr->pickEvent.xcrossing.mode = NotifyNormal;
	    textPtr->pickEvent.xcrossing.detail = NotifyNonlinear;
	    textPtr->pickEvent.xcrossing.same_screen = eventPtr->xmotion.same_screen;
	    textPtr->pickEvent.xcrossing.focus = False;
	    textPtr->pickEvent.xcrossing.state = eventPtr->xmotion.state;
	} else {
	    textPtr->pickEvent = *eventPtr;
	}
    }

    if (textPtr->dontRepick) {
	/*
	 * The widget is scrolling, so avoid repicking until the scroll operation stops,
	 * but it's important that 'pickEvent' is up-to-date (see above).
	 */
	return;
    }

    /*
     * Find the new current character, then find and sort all of the tags associated with it.
     */

    if (textPtr->pickEvent.type == LeaveNotify) {
	TkTextTagSetIncrRefCount(newTagInfoPtr = sharedTextPtr->emptyTagInfoPtr);
	TkTextTagSetIncrRefCount(leaveTags = textPtr->curTagInfoPtr);
	TkTextTagSetIncrRefCount(enterTags = sharedTextPtr->emptyTagInfoPtr);
    } else {
	newDispChunkPtr = TkTextPixelIndex(textPtr,
		textPtr->pickEvent.xcrossing.x, textPtr->pickEvent.xcrossing.y, &index, &nearby);

	if (newDispChunkPtr) {
	    if (!nearby) {
		newLineY = TkTextGetYPixelFromChunk(textPtr, newDispChunkPtr);
	    }
	    sameChunkWithUnchangedTags = (newDispChunkPtr->uniqID == textPtr->lastChunkID);
	}

	/*
	 * We want to avoid that a cursor movement is constantly splitting and
	 * joining char segments. So we postpone the insertion of the "current"
	 * mark until TextWidgetObjCmd will be executed.
	 */

	textPtr->currentMarkIndex = index;
	TkTextIndexToByteIndex(&textPtr->currentMarkIndex);
	textPtr->haveToSetCurrentMark = 1;
	sharedTextPtr->haveToSetCurrentMark = 1;

	if (textPtr->lastLineY == TK_TEXT_NEARBY_IS_UNDETERMINED
		|| (textPtr->lastLineY == TK_TEXT_IS_NEARBY) != nearby) {
	    sameChunkWithUnchangedTags = 0;
	} else if (nearby) {
	    sameChunkWithUnchangedTags = 1;
	} else if (eventPtr->type != MotionNotify || sharedTextPtr->numMotionEventBindings > 0) {
	    sameChunkWithUnchangedTags = 0;
	}

	if (nearby) {
	    TkTextTagSetIncrRefCount(newTagInfoPtr = sharedTextPtr->emptyTagInfoPtr);
	    TkTextTagSetIncrRefCount(leaveTags = textPtr->curTagInfoPtr);
	    TkTextTagSetIncrRefCount(enterTags = sharedTextPtr->emptyTagInfoPtr);
	} else if (sameChunkWithUnchangedTags) {
	    TkTextTagSetIncrRefCount(newTagInfoPtr = textPtr->curTagInfoPtr);
	    TkTextTagSetIncrRefCount(leaveTags = sharedTextPtr->emptyTagInfoPtr);
	    TkTextTagSetIncrRefCount(enterTags = sharedTextPtr->emptyTagInfoPtr);
	} else {
	    /*
	     * NOTE: the tag event handling depends on the display content, and not on
	     * the content of the B-Tree.
	     */

	    if (!(newTagInfoPtr = TkTextGetTagSetFromChunk(newDispChunkPtr))) {
	    	newTagInfoPtr = sharedTextPtr->emptyTagInfoPtr;
	    }
	    TkTextTagSetIncrRefCount(newTagInfoPtr);
	    leaveTags = TkTextTagSetCopy(textPtr->curTagInfoPtr);
	    leaveTags = TkTextTagSetRemoveFromThis(leaveTags, newTagInfoPtr);
	    enterTags = TkTextTagSetRemoveFromThis(TkTextTagSetCopy(newTagInfoPtr), leaveTags);
	    enterTags = TkTextTagSetRemoveFromThis(enterTags, textPtr->curTagInfoPtr);
	}
    }

    if (newLineY != TK_TEXT_IS_NEARBY
	    && textPtr->lastLineY != TK_TEXT_IS_NEARBY
	    && !sameChunkWithUnchangedTags
	    && sharedTextPtr->tagBindingTable) {
	if (textPtr->lastLineY == newLineY) {
	    /*
	     * We have to work-around a severe problem: per default the event handler is
	     * collapsing mouse motion events. This must not happen, a collapse of motion
	     * events has to be done on window level. For the text widget this means that
	     * we sometimes miss the transition from tagged region to tagged region. So we
	     * have to use a work-around for the x-direction: if the display line has not
	     * changed, then call TkTextPixelIndex for every intermediate x position. Of
	     * course this can be handled a bit more clever: traverse all the chunks
	     * between new chunk and old chunk, and send the first leave event, provided
	     * that this happens.
	     *
	     *     leaveTags = old.tags
	     *     for chunk = old.successor to new.predecessor
	     *         foreach t in (leaveTags - chunk.tags)
	     *             t.sendLeave
	     *             leaveTags -= t # do not send anymore
	     *         rof
	     *     rof
	     */

	    const TkTextDispChunk *nextDispChunkPtr;
	    int lastX = textPtr->lastX;
	    int movedToLeft;
	    int sx, sy; /* translation to current scroll position */

	    TkTextGetViewOffset(textPtr, &sx, &sy);
	    movedToLeft = textPtr->pickEvent.xcrossing.x + sx <= lastX;
	    nextDispChunkPtr = newDispChunkPtr;

	    if (movedToLeft) {
		/*
		 * Setup nextDispChunkPtr to predecessor of last chunk.
		 */

		/* find last chunk */
		while (nextDispChunkPtr->nextPtr
			&& !DispChunkContainsX(textPtr, nextDispChunkPtr, lastX)) {
		    nextDispChunkPtr = nextDispChunkPtr->nextPtr;
		}
		if (nextDispChunkPtr != newDispChunkPtr
			&& DispChunkContainsX(textPtr, nextDispChunkPtr, lastX)) {
		    /* move to predecessor of last chunk */
		    nextDispChunkPtr = nextDispChunkPtr->prevPtr;
		}
	    } else {
		/*
		 * Setup nextDispChunkPtr to successor of last chunk.
		 */

		/* find last chunk */
		while (nextDispChunkPtr->prevPtr
			&& !DispChunkContainsX(textPtr, nextDispChunkPtr, lastX)) {
		    nextDispChunkPtr = nextDispChunkPtr->prevPtr;
		}
		if (nextDispChunkPtr != newDispChunkPtr
			&& DispChunkContainsX(textPtr, nextDispChunkPtr, lastX)) {
		    /* move to successor of last chunk */
		    nextDispChunkPtr = nextDispChunkPtr->nextPtr;
		}
	    }

	    if (nextDispChunkPtr != newDispChunkPtr) {
		if (textPtr->curTagInfoPtr != sharedTextPtr->emptyTagInfoPtr) {
		    /*
		     * TkTextTagSetJoinComplementTo() requires this:
		     */
		    if (TkTextTagSetSize(textPtr->curTagInfoPtr) < sharedTextPtr->tagInfoSize) {
			textPtr->curTagInfoPtr = TkTextTagSetResize(
				textPtr->curTagInfoPtr, sharedTextPtr->tagInfoSize);
		    }
		    if (TkTextTagSetSize(leaveTags) < sharedTextPtr->tagInfoSize) {
			leaveTags = TkTextTagSetResize(leaveTags, sharedTextPtr->tagInfoSize);
		    }
		    if (TkTextTagSetSize(enterTags) < sharedTextPtr->tagInfoSize) {
			enterTags = TkTextTagSetResize(enterTags, sharedTextPtr->tagInfoSize);
		    }

		    do {
			const TkTextTagSet *chunkTagInfoPtr;

			if ((chunkTagInfoPtr = TkTextGetTagSetFromChunk(nextDispChunkPtr))) {
			    leaveTags = TkTextTagSetJoinComplementTo(leaveTags,
				    chunkTagInfoPtr, textPtr->curTagInfoPtr);
			    enterTags = TkTextTagSetJoinComplementTo(enterTags,
				    chunkTagInfoPtr, textPtr->curTagInfoPtr);
			}

			nextDispChunkPtr = movedToLeft ?
				nextDispChunkPtr->prevPtr : nextDispChunkPtr->nextPtr;
		    } while (nextDispChunkPtr != newDispChunkPtr);
		}

		/* Delete intermediate enter/leave pairs. */
		leaveTags = TkTextTagSetIntersect(leaveTags, textPtr->curTagInfoPtr);
		enterTags = TkTextTagSetIntersect(enterTags, newTagInfoPtr);
	    }
	} else if (textPtr->lastLineY != TK_TEXT_NEARBY_IS_UNDETERMINED) {
	    const TkTextDispChunk *chunkPtr, *cPtr;
	    const TkTextTagSet *tPtr;
	    TkTextTagSet *commonTags = TkTextTagSetCopy(newTagInfoPtr);

	    /*
	     * The display line has changed, so we have to send leave/enter events
	     * for all the affected tags, otherwise the event handling would depend
	     * on the contingencies of the layout, and this must not happen.
	     *
	     * But do not track a change of the display line if the new display chunk
	     * belongs to the same region as old display chunk.
	     */

	    if (newLineY < textPtr->lastLineY) {
		/*
		 * Mouse pointer has moved to any predecessing display line.
		 */

		for (cPtr = chunkPtr = newDispChunkPtr;
			chunkPtr && textPtr->lastLineY > TkTextGetYPixelFromChunk(textPtr, chunkPtr);
			cPtr = chunkPtr = TkTextGetFirstChunkOfNextDispLine(chunkPtr)) {
		    for ( ; cPtr; cPtr = cPtr->nextPtr) {
			if ((tPtr = TkTextGetTagSetFromChunk(cPtr))) {
			    commonTags = TkTextTagSetIntersectThis(commonTags, tPtr);
			}
		    }
		}
		if (cPtr) {
		    int x = textPtr->lastX;

		    for ( ; cPtr; cPtr = cPtr->nextPtr) {
			if ((tPtr = TkTextGetTagSetFromChunk(cPtr))) {
			    commonTags = TkTextTagSetIntersectThis(commonTags, tPtr);
			}
			if (DispChunkContainsX(textPtr, cPtr, x)) {
			    break;
			}
		    }
		}
	    } else {
		/*
		 * Mouse pointer has moved to any successing display line.
		 */

		for (cPtr = chunkPtr = newDispChunkPtr;
			chunkPtr && textPtr->lastLineY < TkTextGetYPixelFromChunk(textPtr, chunkPtr);
			cPtr = chunkPtr = TkTextGetLastChunkOfPrevDispLine(chunkPtr)) {
		    for ( ; cPtr; cPtr = cPtr->prevPtr) {
			if ((tPtr = TkTextGetTagSetFromChunk(cPtr))) {
			    commonTags = TkTextTagSetIntersectThis(commonTags, tPtr);
			}
		    }
		}
		if (cPtr) {
		    int x = textPtr->lastX;

		    for ( ; cPtr; cPtr = cPtr->prevPtr) {
			if ((tPtr = TkTextGetTagSetFromChunk(cPtr))) {
			    commonTags = TkTextTagSetIntersectThis(commonTags, tPtr);
			}
			if (DispChunkContainsX(textPtr, cPtr, x)) {
			    break;
			}
		    }
		}
	    }

	    TkTextTagSetDecrRefCount(enterTags);
	    TkTextTagSetDecrRefCount(leaveTags);
	    enterTags = TkTextTagSetRemoveFromThis(TkTextTagSetCopy(newTagInfoPtr), commonTags);
	    leaveTags = TkTextTagSetRemoveFromThis(TkTextTagSetCopy(textPtr->curTagInfoPtr), commonTags);
	    TkTextTagSetDecrRefCount(commonTags);
	}
    }

    tagEpoch = sharedTextPtr->tagEpoch;

    if (sharedTextPtr->tagBindingTable && !TkTextTagSetIsEmpty(leaveTags)) {
	/*
	 * Invoke the binding system with a LeaveNotify event for all of the tags
	 * that have gone away.
	 *
	 * Always use a detail of NotifyAncestor. Besides being
	 * consistent, this avoids problems where the binding code will
	 * discard NotifyInferior events.
	 */

	event = textPtr->pickEvent;
	event.type = LeaveNotify;
	event.xcrossing.detail = NotifyAncestor;
	TagBindEvent(textPtr, &event, leaveTags, tagEpoch);
	sentEvents = 1;
    }

    if (!(textPtr->flags & DESTROYED)) {
	int sx, sy; /* translation to current scroll position */

	if (sentEvents) {
	    /*
	     * Reset the "current" mark (be careful to recompute its location, since
	     * it might have changed during an event binding).
	     *
	     * We want to avoid that a cursor movement is constantly splitting and
	     * joining char segments. So we postpone the insertion of the "current"
	     * mark until TextWidgetObjCmd will be executed.
	     */

	    newDispChunkPtr = TkTextPixelIndex(textPtr,
		    textPtr->pickEvent.xcrossing.x, textPtr->pickEvent.xcrossing.y, &index, &nearby);

	    newLineY = nearby ? TK_TEXT_IS_NEARBY : TkTextGetYPixelFromChunk(textPtr, newDispChunkPtr);
	    textPtr->currentMarkIndex = index;
	    TkTextIndexToByteIndex(&textPtr->currentMarkIndex);
	    textPtr->haveToSetCurrentMark = 1;
	    sharedTextPtr->haveToSetCurrentMark = 1;
	}

	if (sharedTextPtr->tagBindingTable && !TkTextTagSetIsEmpty(enterTags)) {
	    /*
	     * Invoke the binding system with a EnterNotify event for all of the tags
	     * that have just appeared.
	     *
	     * Always use a detail of NotifyAncestor. Besides being
	     * consistent, this avoids problems where the binding code will
	     * discard NotifyInferior events.
	     */

	    event = textPtr->pickEvent;
	    event.type = EnterNotify;
	    event.xcrossing.detail = NotifyAncestor;
	    TagBindEvent(textPtr, &event, enterTags, tagEpoch);
	}

	TkTextTagSetDecrRefCount(textPtr->curTagInfoPtr);
	TkTextTagSetIncrRefCount(textPtr->curTagInfoPtr = TkTextTagSetIsEmpty(newTagInfoPtr) ?
		sharedTextPtr->emptyTagInfoPtr : newTagInfoPtr);

	TkTextGetViewOffset(textPtr, &sx, &sy);
	textPtr->lastLineY = newLineY;
	textPtr->lastX = textPtr->pickEvent.xcrossing.x + sx;
	if (newDispChunkPtr) {
	    textPtr->lastChunkID = newDispChunkPtr->uniqID;
	}
    }

    TkTextTagSetDecrRefCount(leaveTags);
    TkTextTagSetDecrRefCount(enterTags);
    TkTextTagSetDecrRefCount(newTagInfoPtr);
}

/*
 *--------------------------------------------------------------
 *
 * TagBindEvent --
 *
 *	Trigger given events for all tags that match the relevant bindings.
 *	To handle the "sel" tag correctly in all peer widgets, we must use the
 *	name of the tags as the binding table element.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Almost anything can be triggered by tag bindings, including deletion
 *	of the text widget.
 *
 *--------------------------------------------------------------
 */

static void
TagBindEvent(
    TkText *textPtr,		/* Text widget to fire bindings in. */
    XEvent *eventPtr,		/* What actually happened. */
    TkTextTagSet *tagInfoPtr,	/* Set of relevant tags. */
    unsigned epoch)		/* Last epoch of tag creation. */
{
    TkTextTag *tagArrayBuf[TK_TEXT_SET_MAX_BIT_SIZE];
    TkTextTag **tagArrPtr = tagArrayBuf;
    const TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    unsigned maxTags = sharedTextPtr->numTags;
    unsigned countTags = 0;
    unsigned i;

    assert(textPtr->sharedTextPtr->tagBindingTable);

    for (i = TkTextTagSetFindFirst(tagInfoPtr);
	    i != TK_TEXT_TAG_SET_NPOS;
	    i = TkTextTagSetFindNext(tagInfoPtr, i)) {
	TkTextTag *tagPtr;

	/*
	 * Take into account that some tags have been gone in the meanwhile.
	 */

	if (i >= maxTags) {
	    break;
	}
	if ((tagPtr = sharedTextPtr->tagLookup[i]) && tagPtr->tagEpoch <= epoch) {
	    if (countTags == sizeof(tagArrayBuf)/sizeof(tagArrayBuf[0])) {
		/* It's quite unexpected that this case happens. */
		unsigned count = TkTextTagSetCount(tagInfoPtr);
		tagArrPtr = (TkTextTag **)ckalloc(count*sizeof(tagArrayBuf[0]));
		memcpy(tagArrPtr, tagArrayBuf, countTags*sizeof(tagArrayBuf[0]));
	    }
	    tagArrPtr[countTags++] = tagPtr;
	}
    }

    if (countTags) {
	TkTextSortTags(countTags, tagArrPtr);
	for (i = 0; i < countTags; ++i) {
	    tagArrPtr[i] = (TkTextTag *) tagArrPtr[i]->name;
	}
	Tk_BindEvent(textPtr->sharedTextPtr->tagBindingTable, eventPtr,
		textPtr->tkwin, countTags, (void **) tagArrPtr);

	if (tagArrPtr != tagArrayBuf) {
	    ckfree(tagArrPtr);
	}
    }
}

/*
 *--------------------------------------------------------------
 *
 * EnumerateTags --
 *
 *	Implements the "tag enumerate" command, see documentation.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Memory is allocated for the result, if needed (standard Tcl result
 *	side effects).
 *
 *--------------------------------------------------------------
 */

static TkBitField *
AddBits(
    TkBitField *dst,		/* can be NULL */
    const TkBitField *src)
{
    if (!dst) {
	dst = TkBitResize(NULL, TkBitSize(src));
    }
    TkBitJoin(dst, src);
    return dst;
}

static TkBitField *
AddComplementBits(
    TkBitField *dst,		/* can be NULL */
    const TkBitField *src)
{
    if (!dst) {
	dst = TkBitResize(NULL, TkBitSize(src));
    }
    TkBitComplementTo(dst, src);
    return dst;
}

static TkBitField *
AddSet(
    const TkSharedText *sharedTextPtr,
    TkBitField *dst,		/* can be NULL */
    const TkTextTagSet *src)
{
    TkBitField *cmpl = TkTextTagSetToBits(src, TkBitSize(sharedTextPtr->usedTags));

    dst = AddBits(dst, cmpl);
    TkBitDecrRefCount(cmpl);
    return dst;
}

static TkBitField *
AddComplementSet(
    const TkSharedText *sharedTextPtr,
    TkBitField *dst,		/* can be NULL */
    const TkTextTagSet *src)
{
    TkBitField *cmpl = TkTextTagSetToBits(src, TkBitSize(sharedTextPtr->usedTags));

    dst = AddComplementBits(dst, cmpl);
    TkBitDecrRefCount(cmpl);
    return dst;
}

static int
EnumerateTags(
    Tcl_Interp *interp,
    TkText *textPtr,
    int objc,
    Tcl_Obj *const *objv)
{
    static const char *const optStrings[] = {
	"-all", "-discardselection", "-display", "-elide", "-geometry", "-lineheight",
	"-nodisplay", "-noelide", "-nogeometry", "-nolineheight", "-noselection",
	"-noundo", "-noused", "-selection", "-undo", "-unused", "-used", NULL
    };
    enum opts {
	ENUM_ALL, ENUM_DISCARD_SELECTION, ENUM_DISPLAY, ENUM_ELIDE, ENUM_GEOEMTRY, ENUM_LINEHEIGHT,
	ENUM_NO_DISPLAY, ENUM_NO_ELIDE, ENUM_NO_GEOMETRY, ENUM_NO_LINEHEIGHT, ENUM_NO_SELECTION,
	ENUM_NO_UNDO, ENUM_NO_USED, ENUM_SELECTION, ENUM_UNDO, ENUM_UNUSED, ENUM_USED
    };

    const TkSharedText *sharedTextPtr = textPtr->sharedTextPtr;
    TkBitField *includeBits = NULL;
    TkBitField *discardBits = NULL;
    int discardSelection = 0;
    TkTextTag **arrayPtr;
    int index, countTags, i;
    unsigned k;

    for (i = 3; i < objc; ++i) {
	const char *option = Tcl_GetString(objv[i]);

	if (*option != '-') {
	    break;
	}

	if (Tcl_GetIndexFromObjStruct(interp, objv[i], optStrings, sizeof(char *),
		"tag option", 0, &index) != TCL_OK) {
	    if (includeBits) { TkBitDecrRefCount(includeBits); }
	    if (discardBits) { TkBitDecrRefCount(discardBits); }
	    return TCL_ERROR;
	}

	switch ((enum opts) index) {
	case ENUM_ALL:
	case ENUM_DISCARD_SELECTION:
	    discardSelection = 1;
	    break;
	case ENUM_DISPLAY:
	    includeBits = AddBits(includeBits, sharedTextPtr->affectDisplayTags);
	    break;
	case ENUM_ELIDE:
	    includeBits = AddBits(includeBits, sharedTextPtr->elisionTags);
	    break;
	case ENUM_GEOEMTRY:
	    includeBits = AddBits(includeBits, sharedTextPtr->affectGeometryTags);
	    break;
	case ENUM_LINEHEIGHT:
	    includeBits = AddBits(includeBits, sharedTextPtr->affectLineHeightTags);
	    break;
	case ENUM_NO_DISPLAY:
	    discardBits = AddBits(discardBits, sharedTextPtr->affectDisplayTags);
	    break;
	case ENUM_NO_ELIDE:
	    discardBits = AddBits(discardBits, sharedTextPtr->elisionTags);
	    break;
	case ENUM_NO_GEOMETRY:
	    discardBits = AddBits(discardBits, sharedTextPtr->affectGeometryTags);
	    break;
	case ENUM_NO_LINEHEIGHT:
	    discardBits = AddBits(discardBits, sharedTextPtr->affectLineHeightTags);
	    break;
	case ENUM_NO_SELECTION:
	    discardSelection = 1;
	    break;
	case ENUM_NO_UNDO:
	    discardBits = AddComplementBits(discardBits, sharedTextPtr->dontUndoTags);
	    break;
	case ENUM_NO_USED:
	    discardBits = AddComplementSet(sharedTextPtr, discardBits,
		    TkBTreeRootTagInfo(sharedTextPtr->tree));
	    break;
	case ENUM_SELECTION:
	    includeBits = AddBits(includeBits, sharedTextPtr->selectionTags);
	    break;
	case ENUM_UNDO:
	    includeBits = AddComplementBits(includeBits, sharedTextPtr->dontUndoTags);
	    break;
	case ENUM_UNUSED:
	    includeBits = AddComplementSet(sharedTextPtr, includeBits,
		    TkBTreeRootTagInfo(sharedTextPtr->tree));
	    break;
	case ENUM_USED:
	    includeBits = AddSet(sharedTextPtr, includeBits, TkBTreeRootTagInfo(sharedTextPtr->tree));
	    break;
	}
    }

    if (objc == i + 1) {
	TkTextIndex index1;
	TkTextSegment *segPtr;
	TkTextTagSet *tagInfoPtr;

	if (!TkTextGetIndexFromObj(interp, textPtr, objv[i], &index1)) {
	    return TCL_ERROR;
	}

	segPtr = TkTextIndexGetContentSegment(&index1, NULL);

	if (!includeBits && !discardBits) {
	    TkTextFindTags(interp, textPtr, segPtr, discardSelection);
	    return TCL_OK;
	}

	TkTextTagSetIncrRefCount(tagInfoPtr = segPtr->tagInfoPtr);
	if (includeBits) {
	    tagInfoPtr = TkTextTagSetIntersectBits(tagInfoPtr, includeBits);
	    TkBitDecrRefCount(includeBits);
	}
	includeBits = TkTextTagSetToBits(tagInfoPtr, TkBitSize(sharedTextPtr->usedTags));
	TkTextTagSetDecrRefCount(tagInfoPtr);
    } else if (objc > i) {
	Tcl_WrongNumArgs(interp, 3, objv, "?options? ?index?");
	return TCL_ERROR;
    }

    if (discardSelection) {
	discardBits = AddBits(discardBits, sharedTextPtr->selectionTags);
    }
    if (!includeBits) {
	if (discardBits) {
	    includeBits = TkBitCopy(sharedTextPtr->usedTags, -1);
	} else {
	    TkBitIncrRefCount(includeBits = sharedTextPtr->usedTags);
	}
    }
    if (discardBits) {
	TkBitRemove(includeBits, discardBits);
    }

    arrayPtr = (TkTextTag **)ckalloc(sharedTextPtr->numEnabledTags * sizeof(TkTextTag *));
    countTags = 0;

    for (k = TkBitFindFirst(includeBits); k != TK_BIT_NPOS; k = TkBitFindNext(includeBits, k)) {
	arrayPtr[countTags++] = sharedTextPtr->tagLookup[k];
    }

    AppendTags(interp, countTags, arrayPtr);
    ckfree(arrayPtr);

    TkBitDecrRefCount(includeBits);
    if (discardBits) {
	TkBitDecrRefCount(discardBits);
    }

    return TCL_OK;
}

#ifndef NDEBUG
/*
 *--------------------------------------------------------------
 *
 * TkpTextPrintTagSet --
 *
 *	This function is for debugging only, printing the content of
 *	the given tag set on stdout.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

void
TkpTextPrintTagSet(
    const TkSharedText *sharedTextPtr,
    const TkTextTagSet *tagInfoPtr)
{
    const char *comma = "";
    unsigned i;

    printf("{");
    for (i = TkTextTagSetFindFirst(tagInfoPtr);
	    i != TK_TEXT_TAG_SET_NPOS;
	    i = TkTextTagSetFindNext(tagInfoPtr, i)) {
	printf("%s%s", comma, sharedTextPtr->tagLookup[i]->name);
	comma = ", ";
    }
    printf("}\n");
}
#endif /* !NDEBUG */

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 105
 * End:
 * vi:set ts=8 sw=4:
 */
