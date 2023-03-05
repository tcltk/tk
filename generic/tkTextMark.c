/*
 * tkTextMark.c --
 *
 *	This file contains the functions that implement marks for text
 *	widgets.
 *
 * Copyright © 1994 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 * Copyright © 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkText.h"
#include "tkAlloc.h"
#include "tk3d.h"
#include <assert.h>

#if HAVE_INTTYPES_H
# include <inttypes.h>
#elif defined(_WIN32) || defined(_WIN64)
/* work-around for ancient MSVC versions */
# define PRIx64 "I64x"
# define PRIx32 "x"
#else
# error "configure failed - can't include inttypes.h"
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
 * Forward references for functions defined in this file:
 */

static void		InsertUndisplayProc(TkText *textPtr, TkTextDispChunk *chunkPtr);
static int		MarkDeleteProc(TkSharedText *sharedTextPtr, TkTextSegment *segPtr, int flags);
static Tcl_Obj *	MarkInspectProc(const TkSharedText *sharedTextPtr, const TkTextSegment *segPtr);
static int		MarkRestoreProc(TkSharedText *sharedTextPtr, TkTextSegment *segPtr);
static void		MarkCheckProc(const TkSharedText *sharedTextPtr, const TkTextSegment *segPtr);
static int		MarkLayoutProc(const TkTextIndex *indexPtr,
			    TkTextSegment *segPtr, int offset, int maxX,
			    int maxChars, int noCharsYet, TkWrapMode wrapMode,
			    TkTextSpaceMode spaceMode, TkTextDispChunk *chunkPtr);
static int		MarkFindNext(Tcl_Interp *interp, TkText *textPtr, int discardSpecial,
			    Tcl_Obj* indexObj, const char *pattern, int forward);
static void		ChangeGravity(TkSharedText *sharedTextPtr, TkText *textPtr,
			    TkTextSegment *markPtr, const Tk_SegType *newTypePtr,
			    TkTextUndoInfo *redoInfo);
static struct TkTextSegment *SetMark(struct TkText *textPtr, const char *name,
			    const Tk_SegType *typePtr, TkTextIndex *indexPtr);
static void		UnsetMark(TkSharedText *sharedTextPtr, TkTextSegment *markPtr,
			    TkTextUndoInfo *redoInfo);
static void		ReactivateMark(TkSharedText *sharedTextPtr, TkTextSegment *markPtr);

static const TkTextDispChunkProcs layoutInsertProcs = {
    TEXT_DISP_CURSOR,		/* type */
    TkrTextInsertDisplayProc,	/* displayProc */
    InsertUndisplayProc,	/* undisplayProc */
    NULL,	                /* measureProc */
    NULL,		        /* bboxProc */
};
/*
 * We need some private undo/redo stuff.
 */

static void UndoToggleGravityPerform(TkSharedText *, TkTextUndoInfo *, TkTextUndoInfo *, int);
static void UndoSetMarkPerform(TkSharedText *, TkTextUndoInfo *, TkTextUndoInfo *, int);
static void RedoSetMarkPerform(TkSharedText *, TkTextUndoInfo *, TkTextUndoInfo *, int);
static void UndoMoveMarkPerform(TkSharedText *, TkTextUndoInfo *, TkTextUndoInfo *, int);
static void UndoToggleGravityDestroy(TkSharedText *, TkTextUndoToken *, int);
static void UndoSetMarkDestroy(TkSharedText *, TkTextUndoToken *, int);
static void RedoSetMarkDestroy(TkSharedText *, TkTextUndoToken *, int);
static void UndoMoveMarkDestroy(TkSharedText *, TkTextUndoToken *, int);
static void UndoMarkGetRange(const TkSharedText *, const TkTextUndoToken *,
	TkTextIndex *, TkTextIndex *);
static void RedoSetMarkGetRange(const TkSharedText *, const TkTextUndoToken *,
	TkTextIndex *, TkTextIndex *);
static void RedoMoveMarkGetRange(const TkSharedText *, const TkTextUndoToken *,
	TkTextIndex *, TkTextIndex *);
static Tcl_Obj *UndoToggleGravityGetCommand(const TkSharedText *, const TkTextUndoToken *);
static Tcl_Obj *UndoSetMarkGetCommand(const TkSharedText *, const TkTextUndoToken *);
static Tcl_Obj *UndoToggleGravityInspect(const TkSharedText *, const TkTextUndoToken *);
static Tcl_Obj *UndoSetMarkInspect(const TkSharedText *, const TkTextUndoToken *);

static const Tk_UndoType undoTokenToggleGravityType = {
    TK_TEXT_UNDO_MARK_GRAVITY,	/* action */
    UndoToggleGravityGetCommand,/* commandProc */
    UndoToggleGravityPerform,	/* undoProc */
    UndoToggleGravityDestroy,	/* destroyProc */
    UndoMarkGetRange,		/* rangeProc */
    UndoToggleGravityInspect	/* inspectProc */
};

static const Tk_UndoType redoTokenToggleGravityType = {
    TK_TEXT_REDO_MARK_GRAVITY,	/* action */
    UndoToggleGravityGetCommand,/* commandProc */
    UndoToggleGravityPerform,	/* undoProc */
    UndoToggleGravityDestroy,	/* destroyProc */
    UndoMarkGetRange,		/* rangeProc */
    UndoToggleGravityInspect	/* inspectProc */
};

static const Tk_UndoType undoTokenSetMarkType = {
    TK_TEXT_UNDO_MARK_SET,	/* action */
    UndoSetMarkGetCommand,	/* commandProc */
    UndoSetMarkPerform,		/* undoProc */
    UndoSetMarkDestroy,		/* destroyProc */
    UndoMarkGetRange,		/* rangeProc */
    UndoSetMarkInspect		/* inspectProc */
};

static const Tk_UndoType redoTokenSetMarkType = {
    TK_TEXT_REDO_MARK_SET,	/* action */
    UndoSetMarkGetCommand,	/* commandProc */
    RedoSetMarkPerform,		/* undoProc */
    RedoSetMarkDestroy,		/* destroyProc */
    RedoSetMarkGetRange,	/* rangeProc */
    UndoSetMarkInspect		/* inspectProc */
};

static const Tk_UndoType undoTokenMoveMarkType = {
    TK_TEXT_UNDO_MARK_MOVE,	/* action */
    UndoSetMarkGetCommand,	/* commandProc */
    UndoMoveMarkPerform,	/* undoProc */
    UndoMoveMarkDestroy,	/* destroyProc */
    RedoMoveMarkGetRange,	/* rangeProc */
    UndoSetMarkInspect		/* inspectProc */
};

static const Tk_UndoType redoTokenMoveMarkType = {
    TK_TEXT_REDO_MARK_MOVE,	/* action */
    UndoSetMarkGetCommand,	/* commandProc */
    UndoMoveMarkPerform,	/* undoProc */
    UndoMoveMarkDestroy,	/* destroyProc */
    RedoMoveMarkGetRange,	/* rangeProc */
    UndoSetMarkInspect		/* inspectProc */
};

typedef struct UndoTokenToggleMark {
    const Tk_UndoType *undoType;
    TkTextSegment *markPtr;
} UndoTokenToggleMark;

typedef struct UndoTokenToggleIndex {
    const Tk_UndoType *undoType;
    TkTextSegment *markPtr;
} UndoTokenToggleIndex;

/* derivation of UndoTokenToggleMark */
typedef struct UndoTokenToggleGravity {
    const Tk_UndoType *undoType;
    TkTextSegment *markPtr;
} UndoTokenToggleGravity;

/* derivation of UndoTokenToggleMark */
typedef struct UndoTokenSetMark {
    const Tk_UndoType *undoType;
    TkTextSegment *markPtr;
} UndoTokenSetMark;

/* derivation of UndoTokenSetMark */
typedef struct RedoTokenSetMark {
    const Tk_UndoType *undoType;
    TkTextSegment *markPtr;
    TkTextUndoIndex index;
} RedoTokenSetMark;

/* derivation of UndoTokenSetMark */
typedef struct UndoTokenMoveMark {
    const Tk_UndoType *undoType;
    TkTextSegment *markPtr;
    TkTextUndoIndex index;
} UndoTokenMoveMark;

/*
 * The following structures declare the "mark" segment types. There are
 * actually two types for marks, one with left gravity and one with right
 * gravity. They are identical except for their gravity property.
 */

const Tk_SegType tkTextRightMarkType = {
    "mark",		/* name */
    SEG_GROUP_MARK,	/* group */
    GRAVITY_RIGHT,	/* gravity */
    MarkDeleteProc,	/* deleteProc */
    MarkRestoreProc,	/* restoreProc */
    MarkLayoutProc,	/* layoutProc */
    MarkCheckProc,	/* checkProc */
    MarkInspectProc	/* inspectProc */
};

const Tk_SegType tkTextLeftMarkType = {
    "mark",		/* name */
    SEG_GROUP_MARK,	/* group */
    GRAVITY_LEFT,	/* gravity */
    MarkDeleteProc,	/* deleteProc */
    MarkRestoreProc,	/* restoreProc */
    MarkLayoutProc,	/* layoutProc */
    MarkCheckProc,	/* checkProc */
    MarkInspectProc	/* inspectProc */
};

/*
 * Pointer to int, for some portable pointer hacks - it's guaranteed that
 * 'uintptr_t' and 'void *' are convertible in both directions (C99 7.18.1.4).
 */

typedef union {
    void *ptr;
    uintptr_t flag;
} __ptr_to_int;

#define MARK_POINTER(ptr)	(((__ptr_to_int *) &ptr)->flag |= (uintptr_t) 1)
#define UNMARK_POINTER(ptr)	(((__ptr_to_int *) &ptr)->flag &= ~(uintptr_t) 1)
#define POINTER_IS_MARKED(ptr)	(((__ptr_to_int *) &ptr)->flag & (uintptr_t) 1)

#define IS_PRESERVED(seg)	POINTER_IS_MARKED(seg->body.mark.ptr)
#define MAKE_PRESERVED(seg)	MARK_POINTER(seg->body.mark.ptr)

#define GET_POINTER(ptr)	((void *) (((__ptr_to_int *) &ptr)->flag & ~(uintptr_t) 1))

#define GET_NAME(seg)		((char *) GET_POINTER(seg->body.mark.ptr))
#define GET_HPTR(seg)		((Tcl_HashEntry *) seg->body.mark.ptr)
#define PTR_TO_INT(ptr)		((uintptr_t) ptr)

#ifndef NDEBUG

# undef GET_HPTR
# undef GET_NAME

static Tcl_HashEntry *GET_HPTR(const TkTextSegment *markPtr)
{ assert(!IS_PRESERVED(markPtr)); return (Tcl_HashEntry *) markPtr->body.mark.ptr; }

static char *GET_NAME(const TkTextSegment *markPtr)
{ assert(IS_PRESERVED(markPtr)); return (char *) GET_POINTER(markPtr->body.mark.ptr); }

#endif /* NDEBUG */

DEBUG_ALLOC(extern unsigned tkTextCountNewSegment);
DEBUG_ALLOC(extern unsigned tkTextCountDestroySegment);
DEBUG_ALLOC(extern unsigned tkTextCountNewUndoToken);
DEBUG_ALLOC(extern unsigned tkTextCountDestroyUndoToken);

/*
 * Some functions for the undo/redo mechanism.
 */

static Tcl_Obj *
AppendName(
    Tcl_Obj *objPtr,
    const TkSharedText *sharedTextPtr,
    const TkTextSegment *markPtr)
{
    const char *name;

    if (IS_PRESERVED(markPtr)) {
	name = GET_NAME(markPtr);
    } else {
	name = TkTextMarkName(sharedTextPtr, NULL, markPtr);
    }
    assert(name);
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(name, TCL_INDEX_NONE));
    return objPtr;
}

static Tcl_Obj *
UndoToggleGravityGetCommand(
    TCL_UNUSED(const TkSharedText *),
    TCL_UNUSED(const TkTextUndoToken *))
{
    Tcl_Obj *objPtr = Tcl_NewObj();

    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj("mark", TCL_INDEX_NONE));
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj("gravity", TCL_INDEX_NONE));
    return objPtr;
}

static Tcl_Obj *
UndoToggleGravityInspect(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item)
{
    const UndoTokenToggleGravity *token = (const UndoTokenToggleGravity *) item;
    return AppendName(UndoToggleGravityGetCommand(sharedTextPtr, item), sharedTextPtr, token->markPtr);
}

static void
UndoToggleGravityPerform(
    TkSharedText *sharedTextPtr,
    TkTextUndoInfo *undoInfo,
    TkTextUndoInfo *redoInfo,
    int isRedo)
{
    UndoTokenToggleGravity *token = (UndoTokenToggleGravity *) undoInfo->token;
    const Tk_SegType *newTypePtr;
    const Tk_SegType *oldTypePtr;

    assert(!token->markPtr->body.mark.changePtr);

    oldTypePtr = token->markPtr->typePtr;
    newTypePtr = (oldTypePtr == &tkTextRightMarkType) ? &tkTextLeftMarkType : &tkTextRightMarkType;
    ChangeGravity(sharedTextPtr, NULL, token->markPtr, newTypePtr, NULL);

    if (redoInfo) {
	redoInfo->token = undoInfo->token;
	redoInfo->token->undoType = isRedo ? &undoTokenToggleGravityType : &redoTokenToggleGravityType;
    }
}

static void
UndoToggleGravityDestroy(
    TkSharedText *sharedTextPtr,
    TkTextUndoToken *item,
    int reused)
{
    assert(!((UndoTokenToggleGravity *) item)->markPtr->body.mark.changePtr);

    if (!reused) {
	UndoTokenToggleGravity *token = (UndoTokenToggleGravity *) item;
	MarkDeleteProc(sharedTextPtr, token->markPtr, DELETE_MARKS);
    }
}

static void
UndoMoveMarkPerform(
    TkSharedText *sharedTextPtr,
    TkTextUndoInfo *undoInfo,
    TkTextUndoInfo *redoInfo,
    int isRedo)
{
    UndoTokenMoveMark *token = (UndoTokenMoveMark *) undoInfo->token;
    TkTextUndoIndex index = token->index;

    assert(!token->markPtr->body.mark.changePtr);

    if (redoInfo) {
	TkBTreeMakeUndoIndex(sharedTextPtr, token->markPtr, &index);
	token->index = index;
	redoInfo->token = undoInfo->token;
	redoInfo->token->undoType = isRedo ? &undoTokenMoveMarkType : &redoTokenMoveMarkType;
    }

    TkBTreeUnlinkSegment(sharedTextPtr, token->markPtr);
    TkBTreeReInsertSegment(sharedTextPtr, &index, token->markPtr);
}

static void
UndoMoveMarkDestroy(
    TkSharedText *sharedTextPtr,
    TkTextUndoToken *item,
    int reused)
{
    assert(!((UndoTokenMoveMark *) item)->markPtr->body.mark.changePtr);

    if (!reused) {
	UndoTokenMoveMark *token = (UndoTokenMoveMark *) item;
	MarkDeleteProc(sharedTextPtr, token->markPtr, DELETE_MARKS);
    }
}

static Tcl_Obj *
UndoSetMarkGetCommand(
    TCL_UNUSED(const TkSharedText *),
    const TkTextUndoToken *item)
{
    const UndoTokenSetMark *token = (const UndoTokenSetMark *) item;
    const char *operation = POINTER_IS_MARKED(token->markPtr) ? "unset" : "set";
    Tcl_Obj *objPtr = Tcl_NewObj();

    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj("mark", TCL_INDEX_NONE));
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(operation, TCL_INDEX_NONE));
    return objPtr;
}

static Tcl_Obj *
UndoSetMarkInspect(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item)
{
    const UndoTokenSetMark *token = (const UndoTokenSetMark *) item;
    const TkTextSegment *markPtr = (const TkTextSegment *)GET_POINTER(token->markPtr);
    Tcl_Obj *objPtr = UndoSetMarkGetCommand(sharedTextPtr, item);

    objPtr = AppendName(objPtr, sharedTextPtr, markPtr);

    if (!POINTER_IS_MARKED(token->markPtr)) {
	const char *gravity = (markPtr->typePtr == &tkTextLeftMarkType) ? "left" : "right";
	Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(gravity, TCL_INDEX_NONE));
    }

    return objPtr;
}

static void
UndoSetMarkPerform(
    TkSharedText *sharedTextPtr,
    TkTextUndoInfo *undoInfo,
    TkTextUndoInfo *redoInfo,
    int isRedo)
{
    const UndoTokenSetMark *token = (const UndoTokenSetMark *) undoInfo->token;
    TkTextSegment *markPtr = (TkTextSegment *)GET_POINTER(token->markPtr);

    assert(!markPtr->body.mark.changePtr);
    UnsetMark(sharedTextPtr, markPtr, redoInfo);
    if (redoInfo && !isRedo) {
	UNMARK_POINTER(((RedoTokenSetMark *) redoInfo->token)->markPtr);
    }
}

static void
UndoSetMarkDestroy(
    TkSharedText *sharedTextPtr,
    TkTextUndoToken *item,
    TCL_UNUSED(int))
{
    UndoTokenSetMark *token = (UndoTokenSetMark *) item;
    TkTextSegment *markPtr = (TkTextSegment *)GET_POINTER(token->markPtr);

    assert(!markPtr->body.mark.changePtr);

    MarkDeleteProc(sharedTextPtr, markPtr, DELETE_CLEANUP);
}

static void
RedoSetMarkPerform(
    TkSharedText *sharedTextPtr,
    TkTextUndoInfo *undoInfo,
    TkTextUndoInfo *redoInfo,
    TCL_UNUSED(int))
{
    RedoTokenSetMark *token = (RedoTokenSetMark *) undoInfo->token;
    TkTextSegment *markPtr = (TkTextSegment *)GET_POINTER(token->markPtr);

    assert(!markPtr->body.mark.changePtr);
    assert(TkTextIsNormalMark(markPtr));

    if (IS_PRESERVED(markPtr)) {
	ReactivateMark(sharedTextPtr, markPtr);
	sharedTextPtr->numMarks += 1;
    }

    TkBTreeReInsertSegment(sharedTextPtr, &token->index, markPtr);
    markPtr->refCount += 1;

    if (redoInfo) {
	UndoTokenSetMark *redoToken;

	redoToken = (UndoTokenSetMark *)malloc(sizeof(UndoTokenSetMark));
	redoToken->markPtr = token->markPtr;
	redoToken->undoType = &undoTokenSetMarkType;
	redoInfo->token = (TkTextUndoToken *) redoToken;
	DEBUG_ALLOC(tkTextCountNewUndoToken++);
	markPtr->refCount += 1;
    }
}

static void
RedoSetMarkDestroy(
    TkSharedText *sharedTextPtr,
    TkTextUndoToken *item,
    TCL_UNUSED(int))
{
    RedoTokenSetMark *token = (RedoTokenSetMark *) item;
    TkTextSegment *markPtr = (TkTextSegment *)GET_POINTER(token->markPtr);

    assert(!markPtr->body.mark.changePtr);
    MarkDeleteProc(sharedTextPtr, markPtr, DELETE_MARKS);
}

static void
UndoMarkGetRange(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item,
    TkTextIndex *startIndex,
    TkTextIndex *endIndex)
{
    const UndoTokenToggleMark *token = (UndoTokenToggleMark *) item;

    TkTextIndexClear2(startIndex, NULL, sharedTextPtr->tree);
    TkTextIndexSetSegment(startIndex, (TkTextSegment *)GET_POINTER(token->markPtr));
    *endIndex = *startIndex;
}

static void
RedoSetMarkGetRange(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item,
    TkTextIndex *startIndex,
    TkTextIndex *endIndex)
{
    RedoTokenSetMark *token = (RedoTokenSetMark *) item;
    TkBTreeUndoIndexToIndex(sharedTextPtr, &token->index, startIndex);
    *endIndex = *startIndex;
}

static void
RedoMoveMarkGetRange(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item,
    TkTextIndex *startIndex,
    TkTextIndex *endIndex)
{
    UndoTokenMoveMark *token = (UndoTokenMoveMark *) item;
    TkTextSegment *markPtr = (TkTextSegment *)GET_POINTER(token->markPtr);

    TkBTreeUndoIndexToIndex(sharedTextPtr, &token->index, startIndex);
    TkTextIndexClear2(endIndex, NULL, sharedTextPtr->tree);
    TkTextIndexSetSegment(endIndex, markPtr);
}

/*
 *--------------------------------------------------------------
 *
 * TkTextMarkCmd --
 *
 *	This function is invoked to process the "mark" options of the widget
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

static int
SetResultNoMarkNamed(Tcl_Interp *interp, const char *name) {
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("there is no mark named \"%s\"", name));
    Tcl_SetErrorCode(interp, "TK", "LOOKUP", "TEXT_MARK", name, NULL);
    return TCL_ERROR;
}

int
TkTextMarkCmd(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. Someone else has already parsed this command
    				 * enough to know that objv[1] is "mark". */
{
    Tcl_HashEntry *hPtr;
    TkTextSegment *markPtr;
    Tcl_HashSearch search;
    TkTextIndex index;
    const Tk_SegType *newTypePtr;
    int optionIndex;
    static const char *const markOptionStrings[] = {
	"compare", "exists", "generate", "gravity", "names", "next", "previous",
	"set", "unset", NULL
    };
    enum markOptions {
	MARK_COMPARE, MARK_EXISTS, MARK_GENERATE, MARK_GRAVITY, MARK_NAMES, MARK_NEXT, MARK_PREVIOUS,
	MARK_SET, MARK_UNSET
    };

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "option ?arg ...?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[2], markOptionStrings,
	    sizeof(char *), "mark option", 0, &optionIndex) != TCL_OK) {
	return TCL_ERROR;
    }

    switch ((enum markOptions) optionIndex) {
    case MARK_COMPARE: {
	TkTextSegment *markPtr1, *markPtr2;
	int relation, value;

	if (objc != 5) {
	    Tcl_WrongNumArgs(interp, 2, objv, "markName1 op markName2");
	    return TCL_ERROR;
	}
	if (!(markPtr1 = TkTextFindMark(textPtr, Tcl_GetString(objv[2])))) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad comparison operand \"%s\": "
		    "must be an existing mark", Tcl_GetString(objv[2])));
	    Tcl_SetErrorCode(interp, "TK", "VALUE", "MARK_COMPARISON", NULL);
	    return TCL_ERROR;
	}
	if (!(markPtr2 = TkTextFindMark(textPtr, Tcl_GetString(objv[4])))) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad comparison operand \"%s\": "
		    "must be an existing mark", Tcl_GetString(objv[4])));
	    Tcl_SetErrorCode(interp, "TK", "VALUE", "MARK_COMPARISON", NULL);
	    return TCL_ERROR;
	}

	if (markPtr1 == markPtr2) {
	    relation = 0;
	} else {
	    TkTextIndex index1, index2;

	    TkTextIndexClear(&index1, textPtr);
	    TkTextIndexClear(&index2, textPtr);
	    TkTextIndexSetSegment(&index1, markPtr1);
	    TkTextIndexSetSegment(&index2, markPtr2);
	    relation = TkTextIndexCompare(&index1, &index2);

	    if (relation == 0) {
		TkTextSegment *segPtr = markPtr1->nextPtr;

		while (segPtr && segPtr != markPtr2 && segPtr->size == 0) {
		    segPtr = segPtr->nextPtr;
		}
		relation = (segPtr == markPtr2) ? -1 : +1;
	    }
	}

	value = TkTextTestRelation(interp, relation, Tcl_GetString(objv[3]));
	if (value == -1) {
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(value));
	break;
    }
    case MARK_EXISTS: {
	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "markName");
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(!!TkTextFindMark(textPtr, Tcl_GetString(objv[3]))));
	break;
    }
    case MARK_GENERATE: {
	TkTextSegment *markPtr1;
	TkTextIndex index1;
	char uniqName[100];

	TkTextIndexClear(&index1, textPtr);
	TkTextIndexSetSegment(&index1, textPtr->startMarker);
	/* IMPORTANT NOTE: ensure fixed length (depending on pointer size) */
	snprintf(uniqName, sizeof(uniqName),
#ifdef TK_IS_64_BIT_ARCH
	    "##ID##0x%016" PRIx64 "##0x%016" PRIx64 "##%08" TCL_Z_MODIFIER "u##", /* we're on a real 64-bit system */
	    (uint64_t) textPtr, (uint64_t) textPtr->sharedTextPtr, ++textPtr->uniqueIdCounter
#else /* if defined(TK_IS_32_BIT_ARCH) */
	    "##ID##0x%08" PRIx32 "##0x%08" PRIx32 "##%08" TCL_Z_MODIFIER "u##",   /* we're most likely on a 32-bit system */
	    (uint32_t) textPtr, (uint32_t) textPtr->sharedTextPtr, ++textPtr->uniqueIdCounter
#endif /* TK_IS_64_BIT_ARCH */
	);
	assert(!TkTextFindMark(textPtr, uniqName));
    	markPtr1 = TkTextMakeMark(textPtr, uniqName);
    	markPtr1->privateMarkFlag = 1;
	textPtr->sharedTextPtr->numMarks -= 1; /* take back counting */
	textPtr->sharedTextPtr->numPrivateMarks += 1;
	TkBTreeLinkSegment(textPtr->sharedTextPtr, markPtr1, &index1);
	Tcl_SetObjResult(textPtr->interp, Tcl_NewStringObj(uniqName, TCL_INDEX_NONE));
	break;
    }
    case MARK_GRAVITY: {
	Tcl_Size length;
	const char *str;

	if (objc < 4 || objc > 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "markName ?gravity?");
	    return TCL_ERROR;
	}
	str = Tcl_GetStringFromObj(objv[3], &length);
	if (strcmp(str, "insert") == 0) {
	    markPtr = textPtr->insertMarkPtr;
	} else if (strcmp(str, "current") == 0) {
	    markPtr = textPtr->currentMarkPtr;
	} else {
	    if (!(hPtr = Tcl_FindHashEntry(&textPtr->sharedTextPtr->markTable, str))) {
		return SetResultNoMarkNamed(interp, Tcl_GetString(objv[3]));
	    }
	    markPtr = (TkTextSegment *)Tcl_GetHashValue(hPtr);
	}
	if (objc == 4) {
	    const char *typeStr;
	    typeStr = markPtr->typePtr == &tkTextRightMarkType ? "right" : "left";
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(typeStr, TCL_INDEX_NONE));
	    return TCL_OK;
	}
	str = Tcl_GetStringFromObj(objv[4], &length);
	if (strncmp(str, "left", length) == 0) {
	    newTypePtr = &tkTextLeftMarkType;
	} else if (strncmp(str, "right", length) == 0) {
	    newTypePtr = &tkTextRightMarkType;
	} else {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "bad mark gravity \"%s\": must be left or right", str));
	    Tcl_SetErrorCode(interp, "TK", "VALUE", "MARK_GRAVITY", NULL);
	    return TCL_ERROR;
	}
	/*
	 * We have to force the re-insertion of the mark when steadyMarks is not enabled.
	 */

	if (markPtr->typePtr != newTypePtr || !textPtr->sharedTextPtr->steadyMarks) {
	    TkTextUndoInfo undoInfo;
	    TkTextUndoInfo *undoInfoPtr = NULL;

	    if (textPtr->sharedTextPtr->steadyMarks
		    && TkTextIsNormalMark(markPtr)
		    && !TkTextUndoUndoStackIsFull(textPtr->sharedTextPtr->undoStack)) {
		undoInfoPtr = &undoInfo;
	    }
	    ChangeGravity(textPtr->sharedTextPtr, textPtr, markPtr, newTypePtr, undoInfoPtr);
	}
	break;
    }
    case MARK_NAMES: {
	int discardSpecial = 0;
	Tcl_Size numArgs = 3;
	const char *pattern;
	Tcl_Obj *resultObj;

	if (objc > 4 && *Tcl_GetString(objv[3]) == '-') {
	    if (strcmp(Tcl_GetString(objv[3]), "-discardspecial") == 0) {
		discardSpecial = 1;
		numArgs = 4;
	    } else {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad option \"%s\": must be -discardspecial", Tcl_GetString(objv[3])));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "BAD_OPTION", NULL);
		return TCL_ERROR;
	    }
	}

	if (objc != numArgs && objc != numArgs + 1) {
	    Tcl_WrongNumArgs(interp, 3, objv, "?-discardspecial? ?pattern?");
	    return TCL_ERROR;
	}

	pattern = objc > numArgs ? Tcl_GetString(objv[numArgs]) : NULL;
	resultObj = Tcl_NewObj();

	if (!discardSpecial && (!pattern || Tcl_StringMatch("insert", pattern))) {
	    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("insert", TCL_INDEX_NONE));
	}
	if (!discardSpecial && (!pattern || Tcl_StringMatch("current", pattern))) {
	    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("current", TCL_INDEX_NONE));
	}

	for (hPtr = Tcl_FirstHashEntry(&textPtr->sharedTextPtr->markTable, &search);
		hPtr;
		hPtr = Tcl_NextHashEntry(&search)) {
	    TkTextSegment *markPtr1 = (TkTextSegment *)Tcl_GetHashValue(hPtr);

	    if (!markPtr1->privateMarkFlag && !markPtr1->startEndMarkFlag) {
		const char *name = (const char *)Tcl_GetHashKey(&textPtr->sharedTextPtr->markTable, hPtr);

		if (!pattern || Tcl_StringMatch(name, pattern)) {
		    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(name, TCL_INDEX_NONE));
		}
	    }
	}

	Tcl_SetObjResult(interp, resultObj);
	break;
    }
    case MARK_NEXT: {
	int discardSpecial = 0;
	Tcl_Size numArgs = 4;
	const char *pattern;

	if (objc > 4 && *Tcl_GetString(objv[3]) == '-') {
	    if (strcmp(Tcl_GetString(objv[3]), "-discardspecial") == 0) {
		discardSpecial = 1;
		numArgs = 5;
	    } else {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad option \"%s\": must be -discardspecial", Tcl_GetString(objv[3])));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "BAD_OPTION", NULL);
		return TCL_ERROR;
	    }
	}

	if (objc != numArgs && objc != numArgs + 1) {
	    Tcl_WrongNumArgs(interp, 3, objv, "?-discardspecial? index ?pattern?");
	    return TCL_ERROR;
	}

	pattern = objc > numArgs ? Tcl_GetString(objv[numArgs]) : NULL;
	return MarkFindNext(interp, textPtr, discardSpecial, objv[numArgs - 1], pattern, 1);
    }
    case MARK_PREVIOUS: {
	int discardSpecial = 0;
	Tcl_Size numArgs = 4;
	const char *pattern;

	if (objc > 4 && *Tcl_GetString(objv[3]) == '-') {
	    if (strcmp(Tcl_GetString(objv[3]), "-discardspecial") == 0) {
		discardSpecial = 1;
		numArgs = 5;
	    } else {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad option \"%s\": must be -discardspecial", Tcl_GetString(objv[3])));
		Tcl_SetErrorCode(interp, "TK", "TEXT", "BAD_OPTION", NULL);
		return TCL_ERROR;
	    }
	}

	if (objc != numArgs && objc != numArgs + 1) {
	    Tcl_WrongNumArgs(interp, 3, objv, "?-discardspecial? index ?pattern?");
	    return TCL_ERROR;
	}

	pattern = objc > numArgs ? Tcl_GetString(objv[numArgs]) : NULL;
	return MarkFindNext(interp, textPtr, discardSpecial, objv[numArgs - 1], pattern, 0);
    }
    case MARK_SET: {
	const Tk_SegType *typePtr = NULL;
	const char *name;

	if (objc != 5 && objc != 6) {
	    Tcl_WrongNumArgs(interp, 3, objv, "markName index ?direction?");
	    return TCL_ERROR;
	}
	if (!TkTextGetIndexFromObj(interp, textPtr, objv[4], &index)) {
	    return TCL_ERROR;
	}
	if (objc == 6) {
	    const char *direction = Tcl_GetString(objv[5]);

	    if (strcmp(direction, "left") == 0) {
		typePtr = &tkTextLeftMarkType;
	    } else if (strcmp(direction, "right") == 0) {
		typePtr = &tkTextRightMarkType;
	    } else {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad mark gravity \"%s\": must be left or right", direction));
		Tcl_SetErrorCode(interp, "TK", "VALUE", "MARK_GRAVITY", NULL);
		return TCL_ERROR;
	    }
	}

	name = Tcl_GetString(objv[3]);

#if BEGIN_DOES_NOT_BELONG_TO_BASE

	if (*name == 'b' && strcmp(name, "begin") == 0) {
	    static int printWarning = 1;

	    if (printWarning) {
		fprintf(stderr, "tk::text: \"begin\" is a reserved index identifier and shouldn't "
			"be used for mark names anymore.\n");
		printWarning = 0;
	    }
	}

#else /* if !BEGIN_DOES_NOT_BELONG_TO_BASE */

	/*
	 * TODO:
	 * Probably we should print a warning if the mark name is matching any of the
	 * following forms:
	 *	- "begin"|"end"
	 *	- <integer> "." (<integer>|"begin"|"end")
	 *	- "@" (<integer>|"first"|"last") "," (<integer>|"first"|"last")
	 *	- "##ID##" .*
	 */

#endif /* BEGIN_DOES_NOT_BELONG_TO_BASE */

	if (!SetMark(textPtr, name, typePtr, &index)) {
	    Tcl_Obj *msgPtr;

	    if (strcmp(name, "insert") == 0) {
		return TCL_OK; /* the "watch" command did destroy the widget */
	    }
	    msgPtr = Tcl_ObjPrintf("\"%s\" is an expired generated mark", name);
	    Tcl_SetObjResult(interp, msgPtr);
	    Tcl_SetErrorCode(interp, "TK", "SET", "TEXT_MARK", name, NULL);
	    return TCL_ERROR;
	}
	break;
    }
    case MARK_UNSET: {
	TkTextUndoInfo undoInfo;
	TkTextUndoInfo *undoInfoPtr = NULL;
	Tcl_Size i;

	if (textPtr->sharedTextPtr->steadyMarks
		&& !TkTextUndoUndoStackIsFull(textPtr->sharedTextPtr->undoStack)) {
	    undoInfoPtr = &undoInfo;
	}

	for (i = 3; i < objc; i++) {
	    if ((hPtr = Tcl_FindHashEntry(&textPtr->sharedTextPtr->markTable, Tcl_GetString(objv[i])))) {
		TkTextSegment *markPtr3 = (TkTextSegment *)Tcl_GetHashValue(hPtr);

		if (TkTextIsPrivateMark(markPtr3)) {
		    UnsetMark(textPtr->sharedTextPtr, markPtr3, NULL);
		} else if (!TkTextIsSpecialMark(markPtr3)) {
		    UnsetMark(textPtr->sharedTextPtr, markPtr3, undoInfoPtr);
		    if (undoInfoPtr && undoInfo.token) {
			TkTextPushUndoToken(textPtr->sharedTextPtr, undoInfo.token, 0);
		    }
		}
	    }
	}
	break;
    }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextFindMark --
 *
 *	Return mark segment of given name, if exisiting.
 *
 * Results:
 *	The mark with this name, or NULL if not existing.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkTextSegment *
TkTextFindMark(
    const TkText *textPtr,
    const char *name)
{
    Tcl_HashEntry *hPtr;

    assert(textPtr);
    assert(name);

    switch (name[0]) {
    case 'i':
	if (strcmp(name, "insert") == 0) {
	    return textPtr->insertMarkPtr;
	}
	break;
    case 'c':
	if (strcmp(name, "current") == 0) {
	    return textPtr->currentMarkPtr;
	}
	break;
    }
    hPtr = Tcl_FindHashEntry(&textPtr->sharedTextPtr->markTable, name);
    return hPtr ? (TkTextSegment *)Tcl_GetHashValue(hPtr) : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * ReactivateMark --
 *
 *	Reactivate a preserved mark.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates some memory for hash table entry, and release memory
 *	of preserved name.
 *
 *----------------------------------------------------------------------
 */

static void
ReactivateMark(
    TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    TkTextSegment *markPtr)		/* Reactivate this mark. */
{
    Tcl_HashEntry *hPtr;
    char *name;
    int isNew;

    assert(IS_PRESERVED(markPtr));

    name = GET_NAME(markPtr);
    hPtr = Tcl_CreateHashEntry(&sharedTextPtr->markTable, name, &isNew);
    assert(isNew);
    free(name);
    Tcl_SetHashValue(hPtr, markPtr);
    markPtr->body.mark.ptr = PTR_TO_INT(hPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextFreeMarks --
 *
 *	Free all used marks, also the hash table for marks will be
 *	destroyed. But do not free private marks if 'retainPrivateMarks'
 *	is true, in this case a new hash table will be built, only
 *	with the remaining private marks.
 *
 * Results:
 *	If 'retainPrivateMarks' is false, then return NULL. Otherwise
 *	the chain of retained private marks will be returned.
 *
 * Side effects:
 *	Free some memory, the old hash table for marks will be destroyed,
 *	and a new one will be created if 'retainPrivateMarks' is true.
 *
 *----------------------------------------------------------------------
 */

TkTextSegment *
TkTextFreeMarks(
    TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    int retainPrivateMarks)		/* Priate marks will be retained? */
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr = Tcl_FirstHashEntry(&sharedTextPtr->markTable, &search);
    TkTextSegment *deletePtr = NULL;
    TkTextSegment *retainedPtr = NULL;
    TkTextSegment *markPtr;

    for ( ; hPtr; hPtr = Tcl_NextHashEntry(&search)) {
	markPtr = (TkTextSegment *)Tcl_GetHashValue(hPtr);

	assert(markPtr->body.mark.changePtr != (void *) 0xdeadbeef);
	assert(markPtr->refCount > 0);

	if (!retainPrivateMarks || !TkTextIsPrivateMark(markPtr)) {
	    markPtr->nextPtr = deletePtr;
	    markPtr->prevPtr = NULL;
	    deletePtr = markPtr;
	} else {
	    const char *name = (const char *)Tcl_GetHashKey(&sharedTextPtr->markTable, hPtr);
	    char *dup;

	    markPtr->sectionPtr = NULL;
	    markPtr->prevPtr = NULL;
	    markPtr->nextPtr = retainedPtr;
	    dup = (char *)malloc(strlen(name) + 1);
	    markPtr->body.mark.ptr = PTR_TO_INT(strcpy(dup, name));
	    MAKE_PRESERVED(markPtr);
	    retainedPtr = markPtr;
	}
    }

    markPtr = deletePtr;
    while (markPtr) {
	TkTextSegment *nextPtr = markPtr->nextPtr;
	assert(TkTextIsSpecialMark(markPtr) || markPtr->refCount == 1);
	MarkDeleteProc(sharedTextPtr, markPtr, DELETE_CLEANUP|TREE_GONE);
	markPtr = nextPtr;
    }

    Tcl_DeleteHashTable(&sharedTextPtr->markTable);
    sharedTextPtr->numMarks = 0;

    if (retainPrivateMarks) {
	Tcl_InitHashTable(&sharedTextPtr->markTable, TCL_STRING_KEYS);

	for (markPtr = retainedPtr; markPtr; markPtr = markPtr->nextPtr) {
	    ReactivateMark(sharedTextPtr, markPtr);
	}
    } else {
	sharedTextPtr->numPrivateMarks = 0;
    }

    return retainedPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextUpdateCurrentMark --
 *
 *	If a position change of the "current" mark has been postponed
 *	we will do now the update.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The "current" mark will change the position.
 *
 *----------------------------------------------------------------------
 */

void
TkTextUpdateCurrentMark(
    TkSharedText *sharedTextPtr)	/* Shared text resource. */
{
    TkText *tPtr;

    assert(sharedTextPtr->haveToSetCurrentMark);

    sharedTextPtr->haveToSetCurrentMark = 0;

    for (tPtr = sharedTextPtr->peers; tPtr; tPtr = tPtr->next) {
	if (tPtr->haveToSetCurrentMark) {
	    tPtr->haveToSetCurrentMark = 0;
	    TkBTreeUnlinkSegment(sharedTextPtr, tPtr->currentMarkPtr);
	    TkBTreeLinkSegment(sharedTextPtr, tPtr->currentMarkPtr, &tPtr->currentMarkIndex);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextMakeStartEndMark --
 *
 *	Make (allocate) a new start/end mark.
 *
 * Results:
 *	The return value is a pointer to the new mark.
 *
 * Side effects:
 *	A new mark is created.
 *
 *----------------------------------------------------------------------
 */

TkTextSegment *
TkTextMakeStartEndMark(
    TkText *textPtr,		/* can be NULL */
    Tk_SegType const *typePtr)
{
    TkTextSegment *markPtr = TkTextMakeMark(NULL, NULL);

    assert(typePtr == &tkTextLeftMarkType || typePtr == &tkTextRightMarkType);

    markPtr->typePtr = typePtr;
    markPtr->startEndMarkFlag = 1;
    markPtr->privateMarkFlag = 1;
    markPtr->body.mark.textPtr = textPtr;
    return markPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextMakeMark --
 *
 *	Make (allocate) a new mark, the gravity default to right.
 *
 * Results:
 *	The return value is a pointer to the new mark.
 *
 * Side effects:
 *	A new mark is created.
 *
 *----------------------------------------------------------------------
 */

static TkTextSegment *
MakeMark(
    TkText *textPtr)		/* Text widget in which to create mark. */
{
    TkTextSegment *markPtr;

    markPtr = (TkTextSegment *)calloc(1, SEG_SIZE(TkTextMark));
    NEW_SEGMENT(markPtr);
    markPtr->typePtr = &tkTextRightMarkType;
    markPtr->refCount = 1;
    markPtr->body.mark.textPtr = textPtr;
    DEBUG_ALLOC(tkTextCountNewSegment++);
    return markPtr;
}

TkTextSegment *
TkTextMakeMark(
    TkText *textPtr,		/* Text widget in which to create mark, can be NULL. */
    const char *name)		/* Name of this mark, can be NULL. */
{
    TkTextSegment *markPtr;
    Tcl_HashEntry *hPtr;
    int isNew;

    assert(!name || textPtr);
    assert(!name || strcmp(name, "insert") != 0);
    assert(!name || strcmp(name, "current") != 0);

    if (!name) {
	return MakeMark(textPtr);
    }

    hPtr = Tcl_CreateHashEntry(&textPtr->sharedTextPtr->markTable, name, &isNew);

    if (isNew) {
	markPtr = MakeMark(textPtr);
	markPtr->body.mark.ptr = PTR_TO_INT(hPtr);
	Tcl_SetHashValue(hPtr, markPtr);
	textPtr->sharedTextPtr->numMarks += 1;
    } else {
	markPtr = (TkTextSegment *)Tcl_GetHashValue(hPtr);
    }

    return markPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextMakeNewMark --
 *
 *	Make (allocate) a new mark, the gravity default to right. This
 *	function will return NULL if the mark name already exists.
 *
 * Results:
 *	The return value is a pointer to the new mark, and will be NULL
 *	if the mark already exists.
 *
 * Side effects:
 *	A new mark is created.
 *
 *----------------------------------------------------------------------
 */

TkTextSegment *
TkTextMakeNewMark(
    TkSharedText *sharedTextPtr,	/* Shared text resource. */
    const char *name)			/* Name of this mark. */
{
    TkTextSegment *markPtr;
    Tcl_HashEntry *hPtr;
    int isNew;

    assert(name);

    hPtr = Tcl_CreateHashEntry(&sharedTextPtr->markTable, name, &isNew);

    if (!isNew) {
	return NULL;
    }

    markPtr = MakeMark(NULL);
    markPtr->body.mark.ptr = PTR_TO_INT(hPtr);
    markPtr->normalMarkFlag = 1;
    Tcl_SetHashValue(hPtr, markPtr);
    sharedTextPtr->numMarks += 1;

    return markPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * ChangeGravity --
 *
 *	Change the gravity of a given mark.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Reset the type pointer of the mark, and set the undo information.
 *
 *----------------------------------------------------------------------
 */

static TkTextMarkChange *
MakeChangeItem(
    TkSharedText *sharedTextPtr,
    TkTextSegment *markPtr)
{
    TkTextMarkChange *changePtr = markPtr->body.mark.changePtr;

    assert(TkTextIsNormalMark(markPtr));

    if (!changePtr) {
	if (sharedTextPtr->undoMarkListCount == sharedTextPtr->undoMarkListSize) {
	    sharedTextPtr->undoMarkListSize = MAX(20u, 2*sharedTextPtr->undoMarkListSize);
	    sharedTextPtr->undoMarkList = (TkTextMarkChange *)realloc(sharedTextPtr->undoMarkList,
		    sharedTextPtr->undoMarkListSize * sizeof(sharedTextPtr->undoMarkList[0]));
	}
	changePtr = &sharedTextPtr->undoMarkList[sharedTextPtr->undoMarkListCount++];
	memset(changePtr, 0, sizeof(*changePtr));
	markPtr->body.mark.changePtr = changePtr;
	(changePtr->markPtr = markPtr)->refCount += 1;
    }

    return changePtr;
}

static TkTextUndoToken *
MakeUndoToggleGravity(
    TkSharedText *sharedTextPtr,
    TkTextSegment *markPtr,
    const Tk_SegType *oldTypePtr)
{
    assert(TkTextIsNormalMark(markPtr));

    sharedTextPtr->undoStackEvent = 1;

    if (!markPtr->body.mark.changePtr
	    || (!markPtr->body.mark.changePtr->setMark
		&& !markPtr->body.mark.changePtr->toggleGravity)) {
	TkTextMarkChange *changePtr = MakeChangeItem(sharedTextPtr, markPtr);
	UndoTokenToggleGravity *token;

	token = (UndoTokenToggleGravity *)calloc(1, sizeof(UndoTokenToggleGravity));
	token->undoType = &undoTokenToggleGravityType;
	(token->markPtr = markPtr)->refCount += 1;
	DEBUG_ALLOC(tkTextCountNewUndoToken++);
	changePtr->toggleGravity = (TkTextUndoToken *) token;
	changePtr->savedMarkType = oldTypePtr;
	sharedTextPtr->lastUndoTokenType = -1;
	return (TkTextUndoToken *) token;
    }

    return NULL;
}

static void
ChangeGravity(
    TkSharedText *sharedTextPtr,	/* Shared text resource. */
    TkText *textPtr,			/* The text widget, can be NULL. */
    TkTextSegment *markPtr,		/* Change toggle of this mark. */
    const Tk_SegType *newTypePtr,	/* This is the new toggle type. */
    TkTextUndoInfo *undoInfo)		/* Undo information, can be NULL */
{
    const Tk_SegType *oldTypePtr;
    int isNormalMark;

    assert(markPtr);
    assert(markPtr->typePtr->group == SEG_GROUP_MARK);
    assert(sharedTextPtr);
    assert(!undoInfo || TkTextIsNormalMark(markPtr));

    oldTypePtr = markPtr->typePtr;
    markPtr->typePtr = newTypePtr;
    isNormalMark = TkTextIsNormalMark(markPtr);

    if (!sharedTextPtr->steadyMarks) {
	if (!textPtr || markPtr != textPtr->insertMarkPtr) {
	    /*
	     * We must re-insert the mark, the old rules of gravity may force
	     * a shuffle of the existing marks.
	     */

	    TkTextIndex index;

	    if (textPtr) {
		TkTextIndexClear(&index, textPtr);
	    } else {
		TkTextIndexClear2(&index, NULL, sharedTextPtr->tree);
	    }
	    TkTextIndexSetSegment(&index, markPtr);
	    TkTextIndexToByteIndex(&index);
	    TkBTreeUnlinkSegment(sharedTextPtr, markPtr);
	    TkBTreeLinkSegment(sharedTextPtr, markPtr, &index);
	}

	if (isNormalMark) {
	    TkTextUpdateAlteredFlag(sharedTextPtr);
	}
    }

    if (undoInfo && isNormalMark) {
	undoInfo->token = MakeUndoToggleGravity(sharedTextPtr, markPtr, oldTypePtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UnsetMark --
 *
 *	Unset given mark.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Free some memory, and add a token to the undo/redo stack.
 *
 *----------------------------------------------------------------------
 */

static void
UnsetMark(
    TkSharedText *sharedTextPtr,
    TkTextSegment *markPtr,
    TkTextUndoInfo *redoInfo)
{
    assert(markPtr);
    assert(markPtr->typePtr->group == SEG_GROUP_MARK);
    assert(!TkTextIsSpecialMark(markPtr));
    assert(!TkTextIsPrivateMark(markPtr) || !redoInfo);

    if (redoInfo) {
	RedoTokenSetMark *token;
	TkTextMarkChange *changePtr;

	if ((changePtr = markPtr->body.mark.changePtr)) {
	    if (changePtr->toggleGravity) {
		TkTextUndoPushItem(sharedTextPtr->undoStack, changePtr->toggleGravity, 0);
		changePtr->toggleGravity = NULL;
	    }
	    if (changePtr->moveMark) {
		free(changePtr->moveMark);
		changePtr->moveMark = NULL;
		DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
		assert(markPtr->refCount > 1);
		markPtr->refCount -= 1;
	    }
	    if (changePtr->setMark) {
		free(changePtr->setMark);
		changePtr->setMark = NULL;
		DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
		assert(markPtr->refCount > 1);
		markPtr->refCount -= 1;
	    }
	}

	memset(redoInfo, 0, sizeof(*redoInfo));
	token = (RedoTokenSetMark *)malloc(sizeof(RedoTokenSetMark));
	token->undoType = &redoTokenSetMarkType;
	markPtr->refCount += 1;
	token->markPtr = markPtr;
	MARK_POINTER(token->markPtr);
	TkBTreeMakeUndoIndex(sharedTextPtr, markPtr, &token->index);
	DEBUG_ALLOC(tkTextCountNewUndoToken++);
	redoInfo->token = (TkTextUndoToken *) token;
	redoInfo->byteSize = 0;
    }

    sharedTextPtr->undoStackEvent = 1;
    sharedTextPtr->lastUndoTokenType = -1;
    TkBTreeUnlinkSegment(sharedTextPtr, markPtr);
    MarkDeleteProc(sharedTextPtr, markPtr, DELETE_CLEANUP);
}

/*
 *----------------------------------------------------------------------
 *
 * TriggerWatchCursor --
 *
 *	Trigger the watch command for movements of the insert cursor.
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

static int
TriggerWatchCursor(
    TkText *textPtr,
    const TkTextIndex *oldCursorIndexPtr,
    const TkTextIndex *newCursorIndexPtr)	/* NULL is allowed. */
{
    TkTextIndex index, newIndex;
    char idx[2][TK_POS_CHARS];
    TkTextTag *tagPtr;
    TkTextTag *tagArrayBuffer[32];
    TkTextTag **tagArrayPtr;
    unsigned tagArraySize;
    unsigned numTags, i;
    Tcl_DString buf;
    int rc;

    assert(oldCursorIndexPtr);
    assert(!TkTextIndexIsEmpty(oldCursorIndexPtr));
    assert(!newCursorIndexPtr || !TkTextIndexIsEmpty(newCursorIndexPtr));

    if (!newCursorIndexPtr) {
	TkTextIndexClear(&newIndex, textPtr);
	TkTextIndexSetSegment(&newIndex, textPtr->insertMarkPtr);
	newCursorIndexPtr = &newIndex;
    }

    if (TkTextIndexIsEqual(oldCursorIndexPtr, newCursorIndexPtr)) {
	return 1;
    }

    Tcl_DStringInit(&buf);
    if (TkTextIndexIsEmpty(oldCursorIndexPtr)) {
	idx[0][0] = '\0';
    } else {
	TkrTextPrintIndex(textPtr, oldCursorIndexPtr, idx[0]);
    }
    TkrTextPrintIndex(textPtr, newCursorIndexPtr, idx[1]);
    if (textPtr->insertMarkPtr->typePtr == &tkTextLeftMarkType) {
	index = *newCursorIndexPtr;
    } else {
	TkTextIndexBackChars(textPtr, newCursorIndexPtr, 1, &index, COUNT_INDICES);
    }

    numTags = 0;
    tagArrayPtr = tagArrayBuffer;
    tagArraySize = sizeof(tagArrayBuffer)/sizeof(tagArrayBuffer[0]);
    tagPtr = TkBTreeGetTags(&index, TK_TEXT_SORT_ASCENDING, NULL);
    for ( ; tagPtr; tagPtr = tagPtr->nextPtr) {
	if (numTags == tagArraySize) {
	    tagArraySize *= 2;
	    tagArrayPtr = (TkTextTag **)realloc(tagArrayPtr == tagArrayBuffer ? NULL : tagArrayPtr, tagArraySize);
	}
	tagArrayPtr[numTags++] = tagPtr;
    }
    for (i = 0; i < numTags; ++i) {
	Tcl_DStringAppendElement(&buf, tagArrayPtr[i]->name);
    }
    if (tagArrayPtr != tagArrayBuffer) {
	free(tagArrayPtr);
    }

    rc = TkTextTriggerWatchCmd(textPtr, "cursor", idx[0], idx[1], Tcl_DStringValue(&buf),
	    NULL, NULL, 0);
    Tcl_DStringFree(&buf);
    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextReleaseUndoMarkTokens --
 *
 *	Release retained undo tokens for mark operations.
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
TkTextReleaseUndoMarkTokens(
    TCL_UNUSED(TkSharedText *),
    TkTextMarkChange *changePtr)
{
    assert(changePtr);

    if (!changePtr->markPtr) {
	return; /* already released */
    }

    assert(changePtr->markPtr->body.mark.changePtr);

    if (changePtr->toggleGravity) {
	free(changePtr->toggleGravity);
	changePtr->toggleGravity = NULL;
	DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
	assert(changePtr->markPtr->refCount > 1);
	changePtr->markPtr->refCount -= 1;
    }
    if (changePtr->moveMark) {
	free(changePtr->moveMark);
	changePtr->moveMark = NULL;
	DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
	assert(changePtr->markPtr->refCount > 1);
	changePtr->markPtr->refCount -= 1;
    }
    if (changePtr->setMark) {
	free(changePtr->setMark);
	changePtr->setMark = NULL;
	DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
	assert(changePtr->markPtr->refCount > 1);
	changePtr->markPtr->refCount -= 1;
    }

    assert(changePtr->markPtr->refCount > 1);
    changePtr->markPtr->refCount -= 1;
    changePtr->markPtr->body.mark.changePtr = NULL;
    changePtr->markPtr = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextPushUndoMarkTokens --
 *
 *	Push retained undo tokens for mark operations onto the undo stack.
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
TkTextPushUndoMarkTokens(
    TkSharedText *sharedTextPtr,
    TkTextMarkChange *changePtr)
{
    assert(sharedTextPtr);
    assert(sharedTextPtr->undoStack);
    assert(changePtr);
    assert(changePtr->markPtr);
    assert(changePtr->markPtr->body.mark.changePtr == changePtr);

    if (changePtr->toggleGravity) {
	UndoTokenToggleGravity *token = (UndoTokenToggleGravity *) changePtr->toggleGravity;

	if (changePtr->savedMarkType != token->markPtr->typePtr) {
	    TkTextUndoPushItem(sharedTextPtr->undoStack, (TkTextUndoToken *) token, 0);
	} else {
	    free(token);
	    DEBUG_ALLOC(tkTextCountDestroyUndoToken++);
	    assert(changePtr->markPtr->refCount > 1);
	    changePtr->markPtr->refCount -= 1;
	}
	changePtr->toggleGravity = NULL;
    }
    if (changePtr->moveMark) {
	TkTextUndoPushItem(sharedTextPtr->undoStack, changePtr->moveMark, 0);
	changePtr->moveMark = NULL;
    }
    if (changePtr->setMark) {
	TkTextUndoPushItem(sharedTextPtr->undoStack, changePtr->setMark, 0);
	changePtr->setMark = NULL;
    }

    assert(changePtr->markPtr->refCount > 1);
    changePtr->markPtr->refCount -= 1;
    changePtr->markPtr->body.mark.changePtr = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkrTextSetMark -- SetMark --
 *
 *	Set a mark to a particular position, creating a new mark if one
 *	doesn't already exist.
 *
 *	Take care when setting the "insert" mark. In this case it might
 *	happen that the receiver of the "watch" command is destroying the
 *	widget. In this case this function will return NULL (otherwise
 *	this function will always return non-NULL in case of setting the
 *	"insert" mark).
 *
 *	Note that parameter indexPtr may be adjusted if the position
 *	is outside of visible text, and we are setting the "insert"
 *	mark.
 *
 * Results:
 *	The return value is a pointer to the mark that was just set.
 *
 * Side effects:
 *	A new mark is created, or an existing mark is moved.
 *
 *----------------------------------------------------------------------
 */

static TkTextSegment *
SetMark(
    TkText *textPtr,		/* Text widget in which to create mark. */
    const char *name,		/* Name of mark to set. */
    const Tk_SegType *typePtr,	/* Sepcifies the gravity, either tkTextLeftMarkType,
    				 * tkTextRightMarkType, or NULL. */
    TkTextIndex *indexPtr)	/* Where to set mark. */
{
    Tcl_HashEntry *hPtr = NULL;
    TkSharedText *sharedTextPtr;
    TkTextSegment *markPtr;
    TkTextIndex oldIndex;
    TkTextUndoIndex undoIndex;
    int pushUndoToken;
    int widgetSpecific;
    const Tk_SegType *oldTypePtr = NULL;

    assert(textPtr);
    assert(indexPtr->textPtr == textPtr);

    widgetSpecific = 0;
    markPtr = NULL;

    switch (*name) {
    case 'i':
	if (strcmp(name, "insert") == 0) {
	    widgetSpecific = 1;
	    markPtr = textPtr->insertMarkPtr;
	    if (TkTextIsElided(indexPtr)) {
		TkTextSkipElidedRegion(indexPtr);
	    }
	}
	break;
    case 'c':
	if (strcmp(name, "current") == 0) {
	    widgetSpecific = 1;
	    markPtr = textPtr->currentMarkPtr;
	}
	break;
    }

    sharedTextPtr = textPtr->sharedTextPtr;
    TkTextIndexClear(&oldIndex, textPtr);

    if (!widgetSpecific) {
	int dummy;
	hPtr = Tcl_CreateHashEntry(&sharedTextPtr->markTable, name, &dummy);
	markPtr = (TkTextSegment *)Tcl_GetHashValue(hPtr);
    }

    if (!markPtr) {
	if (name[0] == '#' && name[1] == '#' && name[2] == 'I') {
#ifdef TK_IS_64_BIT_ARCH
	    static const size_t length = 32 + 2*sizeof(uint64_t);
#else /* if defined(TK_IS_32_BIT_ARCH) */
	    static const size_t length = 32 + 2*sizeof(uint32_t);
#endif /* TK_IS_64_BIT_ARCH */

	    void *sPtr, *tPtr;
	    unsigned num;

	    if (strlen(name) == length && sscanf(name, "##ID##%p##%p##%u##", &sPtr, &tPtr, &num) == 3) {
		assert(hPtr);
		Tcl_DeleteHashEntry(hPtr);
		return NULL; /* this is an expired generated mark */
	    }
	}

	if (widgetSpecific) {
	    /*
	     * This is a special mark.
	     */
	    markPtr = MakeMark(textPtr);
	    if (*name == 'i') { /* "insert" */
		textPtr->insertMarkPtr = markPtr;
		markPtr->insertMarkFlag = 1;
	    } else { /* "current" */
		textPtr->currentMarkPtr = markPtr;
		markPtr->currentMarkFlag = 1;
	    }
	    pushUndoToken = 0;
	} else {
	    markPtr = MakeMark(NULL);
	    markPtr->body.mark.ptr = PTR_TO_INT(hPtr);
	    markPtr->normalMarkFlag = 1;
	    Tcl_SetHashValue(hPtr, markPtr);
	    textPtr->sharedTextPtr->numMarks += 1;
	    pushUndoToken = sharedTextPtr->steadyMarks && sharedTextPtr->undoStack;
	}
    } else {
	const TkTextSegment *segPtr;

	TkTextMarkSegToIndex(textPtr, markPtr, &oldIndex);

	if (markPtr == textPtr->insertMarkPtr && TkTextIndexIsEndOfText(indexPtr)) {
	    /*
	     * The index is outside of visible text, so backup one char.
	     */
	    TkTextIndexBackChars(textPtr, indexPtr, 1, indexPtr, COUNT_INDICES);
	}

	if (!sharedTextPtr->steadyMarks
		&& (!typePtr || typePtr == markPtr->typePtr)
		&& TkTextIndexIsEqual(&oldIndex, indexPtr)) {
	    return markPtr; /* this mark did not change the position */
	}

	TkTextIndexToByteIndex(&oldIndex);
	pushUndoToken = sharedTextPtr->steadyMarks
		&& sharedTextPtr->undoStack
		&& TkTextIsNormalMark(markPtr);

	/*
	 * If this is the insertion point that's being moved, be sure to force
	 * a display update at the old position. Also, don't let the insertion
	 * cursor be after the final newline of the file.
	 */

	if (markPtr == textPtr->insertMarkPtr) {
	    TkTextIndex index2;

	    TkTextIndexToByteIndex(indexPtr);

	    if (textPtr->state == TK_TEXT_STATE_NORMAL) {
		/*
		 * Test whether cursor is inside the actual range.
		 */
		if (TkTextIndexRestrictToStartRange(&oldIndex) >= 0
			&& TkTextIndexRestrictToEndRange(&oldIndex) <= 0
			&& TkTextIndexForwChars(textPtr, &oldIndex, 1, &index2, COUNT_INDICES)) {
		    /*
		     * While we wish to redisplay, no heights have changed, so no need
		     * to call TkTextInvalidateLineMetrics.
		     */

		    /*
		     * TODO: this will do too much, but currently the implementation
		     * lacks on an efficient redraw functionality especially designed
		     * for cursor updates.
		     */
		    TkrTextChanged(NULL, textPtr, &oldIndex, &index2);
		}
	    }
	} else if (markPtr == textPtr->currentMarkPtr) {
	    textPtr->haveToSetCurrentMark = 0;
	} else if (pushUndoToken) {
	    TkBTreeMakeUndoIndex(sharedTextPtr, markPtr, &undoIndex);
	}

	if ((segPtr = TkTextIndexGetSegment(indexPtr)) == markPtr) {
	    return markPtr;
	}

	if (segPtr && segPtr->size > 1) {
	    /* because TkBTreeUnlinkSegment may invalidate this index */
	    TkTextIndexToByteIndex(indexPtr);
	}

	TkBTreeUnlinkSegment(sharedTextPtr, markPtr);
    }

    if (typePtr && typePtr != markPtr->typePtr) {
	oldTypePtr = markPtr->typePtr;
	markPtr->typePtr = typePtr;
    }

    /* this function will also update 'sectionPtr' */
    TkBTreeLinkSegment(sharedTextPtr, markPtr, indexPtr);

    if (pushUndoToken) {
	TkTextMarkChange *changePtr;

	changePtr = MakeChangeItem(sharedTextPtr, markPtr);

	if (!changePtr->setMark && !changePtr->moveMark) {
	    if (TkTextIndexIsEmpty(&oldIndex)) {
		UndoTokenSetMark *token;

		token = (UndoTokenSetMark *)malloc(sizeof(UndoTokenSetMark));
		token->undoType = &undoTokenSetMarkType;
		(token->markPtr = markPtr)->refCount += 1;
		DEBUG_ALLOC(tkTextCountNewUndoToken++);
		changePtr->setMark = (TkTextUndoToken *) token;
		sharedTextPtr->undoStackEvent = 1;
		sharedTextPtr->lastUndoTokenType = -1;
		oldTypePtr = NULL;
	    } else {
		UndoTokenMoveMark *token;

		token = (UndoTokenMoveMark *)malloc(sizeof(UndoTokenMoveMark));
		token->undoType = &undoTokenMoveMarkType;
		(token->markPtr = markPtr)->refCount += 1;
		token->index = undoIndex;
		DEBUG_ALLOC(tkTextCountNewUndoToken++);
		changePtr->moveMark = (TkTextUndoToken *) token;
		sharedTextPtr->undoStackEvent = 1;
		sharedTextPtr->lastUndoTokenType = -1;
	    }
	}

	if (oldTypePtr) {
	    MakeUndoToggleGravity(sharedTextPtr, markPtr, oldTypePtr);
	}
    }

    if (sharedTextPtr->steadyMarks && TkTextIsNormalMark(markPtr)) {
	TkTextUpdateAlteredFlag(sharedTextPtr);
    }

    if (textPtr->state == TK_TEXT_STATE_NORMAL) {
	/*
	 * If the mark is the insertion cursor, then update the screen at the mark's new location.
	 */

	if (markPtr == textPtr->insertMarkPtr) {
	    TkTextIndex index2;

	    TkTextIndexForwChars(textPtr, indexPtr, 1, &index2, COUNT_INDICES);

	    /*
	     * While we wish to redisplay, no heights have changed, so no need to
	     * call TkTextInvalidateLineMetrics.
	     */

	    /* TODO: this is very inefficient, it would be more appopriate to trigger
	     * a special cursor redraw function (see DisplayDLine in tkTextDisp).
	     * Instead of inserting a cursor chunk (not needed) we want to overlay
	     * with a cursor. This would speed up cursor movement.
	     */

	    TkrTextChanged(NULL, textPtr, indexPtr, &index2);

	    /*
	     * Finally trigger the "watch" command for the "insert" cursor,
	     * this must be the last action.
	     */

	    if (textPtr->watchCmd && !TriggerWatchCursor(textPtr, &oldIndex, indexPtr)) {
		return NULL; /* the receiver did destroy the widget */
	    }
	}
    }

    return markPtr;
}

TkTextSegment *
TkrTextSetMark(
    TkText *textPtr,		/* Text widget in which to create mark. */
    const char *name,		/* Name of mark to set. */
    TkTextIndex *indexPtr)	/* Where to set mark. */
{
    return SetMark(textPtr, name, NULL, indexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextUnsetMark --
 *
 *	Unset (delete) given mark.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A mark will be deleted.
 *
 *----------------------------------------------------------------------
 */

void
TkTextUnsetMark(
    TkText *textPtr,		/* Text widget in which to create mark. */
    TkTextSegment *markPtr)	/* Delete this mark. */
{
    TkTextUndoInfo undoInfo;
    TkTextUndoInfo *undoInfoPtr = NULL;
    int isNormalMark = TkTextIsNormalMark(markPtr);

    assert(TkTextIsNormalMark(markPtr));

    if (isNormalMark
	    && textPtr->sharedTextPtr->steadyMarks
	    && !TkTextUndoUndoStackIsFull(textPtr->sharedTextPtr->undoStack)) {
	undoInfoPtr = &undoInfo;
    }
    UnsetMark(textPtr->sharedTextPtr, markPtr, undoInfoPtr);
    if (isNormalMark) {
	if (undoInfoPtr && undoInfo.token) {
	    TkTextPushUndoToken(textPtr->sharedTextPtr, undoInfo.token, 0);
	}
	if (textPtr->sharedTextPtr->steadyMarks) {
	    TkTextUpdateAlteredFlag(textPtr->sharedTextPtr);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextSaveCursorIndex --
 *
 *	Save the current position of the insert cursor, but only if
 *	it is not yet saved. Use this function only if a trigger of
 *	the "watch" command is wanted in case of cursor movement.
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
TkTextSaveCursorIndex(
    TkText *textPtr)
{
    if (TkTextIndexIsEmpty(&textPtr->insertIndex)) {
	TkTextIndexSetSegment(&textPtr->insertIndex, textPtr->insertMarkPtr);
	TkTextIndexSave(&textPtr->insertIndex);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextTriggerWatchCursor --
 *
 *	Trigger the watch command for movements of the insert cursor.
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

int
TkTextTriggerWatchCursor(
    TkText *textPtr)
{
    assert(textPtr->watchCmd);

    if (TkTextIndexIsEmpty(&textPtr->insertIndex)) {
	return 1;
    }

    TkTextIndexRebuild(&textPtr->insertIndex);
    return TriggerWatchCursor(textPtr, &textPtr->insertIndex, NULL);
}

/*
 *--------------------------------------------------------------
 *
 * TkTextMarkSegToIndex --
 *
 *	Given a segment that is a mark, create an index that refers to the
 *	next text character (or other text segment with non-zero size) after
 *	the mark.
 *
 * Results:
 *	*IndexPtr is filled in with index information.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

void
TkTextMarkSegToIndex(
    TkText *textPtr,		/* Text widget containing mark, can be NULL. */
    TkTextSegment *markPtr,	/* Mark segment. */
    TkTextIndex *indexPtr)	/* Index information gets stored here.  */
{
    assert(textPtr);
    assert(markPtr);
    assert(markPtr->sectionPtr); /* otherwise not linked */

    TkTextIndexClear(indexPtr, textPtr);
    /* disable range checks, because here it's is allowed that the index is out of range. */
    DEBUG(indexPtr->discardConsistencyCheck = 1);
    TkTextIndexSetSegment(indexPtr, markPtr);
}

/*
 *--------------------------------------------------------------
 *
 * TkTextMarkNameToIndex --
 *
 *	Given the name of a mark, return an index corresponding to the mark
 *	name.
 *
 * Results:
 *	The return value is 'true' if "name" exists as a mark in the text
 *	widget and is located within its -start/-end range. In this
 *	case *indexPtr is filled in with the next segment who is after the
 *	mark whose size is non-zero. 'false' is returned if the mark
 *	doesn't exist in the text widget, or if it is out of its -start/
 *	-end range. In this latter case *indexPtr still contains valid
 *	information, in particular TkTextMarkNameToIndex called with the
 *	"insert" or "current" mark name may return TCL_ERROR, but *indexPtr
 *	contains the correct index of this mark before -start or after
 *	-end.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static int
MarkToIndex(
    TkText *textPtr,		/* Text widget containing mark. */
    TkTextSegment *markPtr,	/* Pointer to mark segment. */
    TkTextIndex *indexPtr)	/* Index information gets stored here. */
{
    assert(textPtr);
    TkTextMarkSegToIndex(textPtr, markPtr, indexPtr);
    indexPtr->textPtr = textPtr;

    if (TkTextIndexOutsideStartEnd(indexPtr)) {
	return 0;
    }

    return 1;
}

int
TkTextMarkNameToIndex(
    TkText *textPtr,		/* Text widget containing mark. */
    const char *name,		/* Name of mark. */
    TkTextIndex *indexPtr)	/* Index information gets stored here. */
{
    TkTextSegment *segPtr;

    assert(textPtr);

    if (strcmp(name, "insert") == 0) {
	segPtr = textPtr->insertMarkPtr;
    } else if (strcmp(name, "current") == 0) {
	segPtr = textPtr->currentMarkPtr;
    } else {
	Tcl_HashEntry *hPtr = Tcl_FindHashEntry(&textPtr->sharedTextPtr->markTable, name);

	if (hPtr == NULL) {
	    return 0;
	}
	segPtr = (TkTextSegment *)Tcl_GetHashValue(hPtr);
    }

    return MarkToIndex(textPtr, segPtr, indexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextInspectUndoMarkItem --
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
TkTextInspectUndoMarkItem(
    const TkSharedText *sharedTextPtr,
    const TkTextMarkChange *changePtr,
    Tcl_Obj* objPtr)
{
    assert(changePtr);

    if (changePtr->setMark) {
	Tcl_ListObjAppendElement(NULL, objPtr,
		changePtr->setMark->undoType->inspectProc(sharedTextPtr, changePtr->setMark));
    }
    if (changePtr->moveMark) {
	Tcl_ListObjAppendElement(NULL, objPtr,
		changePtr->moveMark->undoType->inspectProc(sharedTextPtr, changePtr->moveMark));
    }
    if (changePtr->toggleGravity) {
	Tcl_ListObjAppendElement(NULL, objPtr,
		changePtr->toggleGravity->undoType->inspectProc(sharedTextPtr,
		changePtr->toggleGravity));
    }
}

/*
 *--------------------------------------------------------------
 *
 * MarkInspectProc --
 *
 *	This function is invoked to build the information for
 *	"inspect".
 *
 * Results:
 *	Return a TCL object containing the information for
 *	"inspect".
 *
 * Side effects:
 *	Storage is allocated.
 *
 *--------------------------------------------------------------
 */

static Tcl_Obj *
MarkInspectProc(
    const TkSharedText *sharedTextPtr,
    const TkTextSegment *segPtr)
{
    Tcl_Obj *objPtr = Tcl_NewObj();
    const char *gravity = (segPtr->typePtr == &tkTextLeftMarkType) ? "left" : "right";
    const char *name;

    assert(!TkTextIsPrivateMark(segPtr));
    assert(!IS_PRESERVED(segPtr));

    name = TkTextMarkName(sharedTextPtr, NULL, segPtr);
    assert(name);

    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(gravity, TCL_INDEX_NONE));
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(name, TCL_INDEX_NONE));
    return objPtr;
}

/*
 *--------------------------------------------------------------
 *
 * MarkDeleteProc --
 *
 *	This function is invoked by the text B-tree code whenever a mark lies
 *	in a range being deleted.
 *
 * Results:
 *	Returns false to indicate that deletion has been rejected. Otherwise, if
 *	deletion has been done (virtually) because DELETE_MARKS is set, true
 *	will be returned. If the reference count of this segment is not going
 *	to zero then this segment will be preserved for undo.
 *
 * Side effects:
 *	None when this functions returns false (even if the whole tree is being
 *	deleted we don't free up the mark; it will be done elsewhere). But
 *	if a deletion has been done the hash entry of this mark will be
 *	removed.
 *
 *--------------------------------------------------------------
 */

static int
MarkDeleteProc(
    TkSharedText *sharedTextPtr,/* Handle to shared text resource. */
    TkTextSegment *segPtr,	/* Segment being deleted. */
    int flags)			/* The deletion flags. */
{
    assert(segPtr->refCount > 0);

    if (TkTextIsSpecialMark(segPtr)) {
	assert(!segPtr->body.mark.changePtr);
	return 0;
    }

    if (TkTextIsPrivateMark(segPtr)) {
	assert(!segPtr->body.mark.changePtr);
	if (!(flags & DELETE_CLEANUP)) {
	    return 0;
	}
	if (--segPtr->refCount == 0) {
	    DEBUG(segPtr->body.mark.changePtr = (void *) 0xdeadbeef);
	    if (!(flags & TREE_GONE)) {
		Tcl_DeleteHashEntry(GET_HPTR(segPtr));
	    }
	    FREE_SEGMENT(segPtr);
	    DEBUG_ALLOC(tkTextCountDestroySegment++);
	}
	return 1;
    }

    if (!(flags & (DELETE_CLEANUP|DELETE_MARKS|TREE_GONE))) {
	return 0;
    }

    assert(segPtr->body.mark.ptr);

    if (segPtr->body.mark.changePtr) {
	unsigned index;

	assert(sharedTextPtr->steadyMarks);
	index = segPtr->body.mark.changePtr - sharedTextPtr->undoMarkList;
	TkTextReleaseUndoMarkTokens(sharedTextPtr, segPtr->body.mark.changePtr);
	memmove(sharedTextPtr->undoMarkList + index, sharedTextPtr->undoMarkList + index + 1,
		--sharedTextPtr->undoMarkListCount - index);
	assert(!segPtr->body.mark.changePtr);
    }

    if (--segPtr->refCount == 0) {
	if (IS_PRESERVED(segPtr)) {
	    assert(sharedTextPtr->steadyMarks);
	    free(GET_NAME(segPtr));
	} else {
	    Tcl_DeleteHashEntry(GET_HPTR(segPtr));
	    sharedTextPtr->numMarks -= 1;
	}
	DEBUG(segPtr->body.mark.changePtr = (void *) 0xdeadbeef);
	FREE_SEGMENT(segPtr);
	DEBUG_ALLOC(tkTextCountDestroySegment++);
    } else if (!IS_PRESERVED(segPtr)) {
	if (sharedTextPtr->steadyMarks) {
	    /*
	     * This case should only happen if this mark belongs to undo/redo stack.
	     * We have to preserve the mark if not already preserved.
	     */

	    Tcl_HashEntry *hPtr = GET_HPTR(segPtr);
	    const char *name = (const char *)Tcl_GetHashKey(&sharedTextPtr->markTable, hPtr);
	    size_t size = strlen(name) + 1;

	    assert(sharedTextPtr->steadyMarks);
	    segPtr->body.mark.ptr = PTR_TO_INT(memcpy(malloc(size), name, size));
	    MAKE_PRESERVED(segPtr);
	    Tcl_DeleteHashEntry(hPtr);
	    sharedTextPtr->numMarks -= 1;
	} else {
	    /*
	     * It seems that we have a bug with reference counting. So print a warning
	     * and delete it anyway.
	     */

	    fprintf(stderr, "reference count of mark '%s' is %d (should be zero)\n",
		    TkTextMarkName(sharedTextPtr, NULL, segPtr), segPtr->refCount);
	    Tcl_DeleteHashEntry(GET_HPTR(segPtr));
	    sharedTextPtr->numMarks -= 1;
	    FREE_SEGMENT(segPtr);
	    DEBUG_ALLOC(tkTextCountDestroySegment++);
	}
    }

    return 1;
}

/*
 *--------------------------------------------------------------
 *
 * MarkRestoreProc --
 *
 *	This function is called when a mark segment will be resused
 *	from the undo chain. But a re-use is possible only if option
 *	-steadymarks is enabled, otherwise the segment will be
 *	deleted instead if this is a preserved mark.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Either the name of the mark will be freed, and the mark
 *	will be re-entered into the hash table, or it will be
 *	deleted.
 *
 *--------------------------------------------------------------
 */

static int
MarkRestoreProc(
    TkSharedText *sharedTextPtr,/* Handle to shared text resource. */
    TkTextSegment *segPtr)	/* Segment to restore. */
{
    assert(TkTextIsNormalMark(segPtr));

    if (IS_PRESERVED(segPtr)) {
	Tcl_HashEntry *hPtr;
	int isNew;

	if (!sharedTextPtr->steadyMarks) {
	    MarkDeleteProc(sharedTextPtr, segPtr, DELETE_CLEANUP);
	    return 0;
	}

	hPtr = Tcl_CreateHashEntry(&sharedTextPtr->markTable, GET_NAME(segPtr), &isNew);
	assert(isNew);
	Tcl_SetHashValue(hPtr, segPtr);
	sharedTextPtr->numMarks += 1;
	free(GET_NAME(segPtr));
	segPtr->body.mark.ptr = PTR_TO_INT(hPtr);
    }

    return 1;
}

/*
 *--------------------------------------------------------------
 *
 * MarkCheckProc --
 *
 *	This function is invoked by the B-tree code to perform consistency
 *	checks on mark segments.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The function panics if it detects anything wrong with
 *	the mark.
 *
 *--------------------------------------------------------------
 */

static void
MarkCheckProc(
    const TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    const TkTextSegment *markPtr)	/* Segment to check. */
{
    if (!markPtr->nextPtr) {
	Tcl_Panic("MarkCheckProc: mark is last segment in line");
    }
    if (markPtr->size != 0) {
	Tcl_Panic("MarkCheckProc: mark has size %d", markPtr->size);
    }
    if (!markPtr->insertMarkFlag
	    && !markPtr->currentMarkFlag
	    && !markPtr->privateMarkFlag
	    && !markPtr->normalMarkFlag) {
	Tcl_Panic("MarkCheckProc: mark is not specialized");
    }
    if (markPtr->insertMarkFlag +
	    markPtr->currentMarkFlag +
	    markPtr->privateMarkFlag +
	    markPtr->normalMarkFlag > 1) {
	Tcl_Panic("MarkCheckProc: mark has more than one specialization");
    }
    if (markPtr->startEndMarkFlag && !markPtr->privateMarkFlag) {
	Tcl_Panic("MarkCheckProc: start/end marks have to be private");
    }

    if (markPtr->body.mark.changePtr) {
	/*
	 * Private marks and special marks will not have undo/redo data.
	 */

	if (TkTextIsSpecialOrPrivateMark(markPtr)) {
	    Tcl_Panic("MarkCheckProc: private/special marks should not have undo/redo data");
	}
    }

    /*
     * The special marks ("insert", "current") are not in the hash table,
     * the same with the start/end markers.
     */

    if (markPtr->body.mark.ptr) {
	if (IS_PRESERVED(markPtr)) {
	    if (!sharedTextPtr->steadyMarks) {
		Tcl_Panic("MarkCheckProc: preserved mark detected, though we don't have steady marks");
	    }
	    else
	    {
		Tcl_Panic("MarkCheckProc: detected preserved mark '%s' outside of the undo chain",
			GET_NAME(markPtr));
	    }
	} else {
	    void *hPtr;
	    hPtr = Tcl_GetHashKey(&sharedTextPtr->markTable, (Tcl_HashEntry *) markPtr->body.mark.ptr);
	    if (!hPtr) {
		Tcl_Panic("MarkCheckProc: couldn't find hash table entry for mark");
	    }
	}
    } else if (!markPtr->insertMarkFlag && !markPtr->currentMarkFlag && !markPtr->startEndMarkFlag) {
	Tcl_Panic("MarkCheckProc: mark is not in hash table, though it's not a special mark");
    }

    if (markPtr->startEndMarkFlag) {
	if (markPtr->typePtr == &tkTextLeftMarkType) {
	    if (markPtr->prevPtr
		    && markPtr->prevPtr->typePtr->group == SEG_GROUP_MARK
		    && (!markPtr->prevPtr->startEndMarkFlag
			|| markPtr->prevPtr->typePtr != &tkTextLeftMarkType)) {
		Tcl_Panic("MarkCheckProc: start marker must be leftmost mark");
	    }
	} else {
	    if (markPtr->nextPtr
		    && markPtr->nextPtr->typePtr->group == SEG_GROUP_MARK
		    && (!markPtr->nextPtr->startEndMarkFlag
			|| markPtr->nextPtr->typePtr != &tkTextRightMarkType)) {
		Tcl_Panic("MarkCheckProc: end marker must be rightmost mark");
	    }
	}
    }
}

/*
 *--------------------------------------------------------------
 *
 * MarkLayoutProc --
 *
 *	This function is the "layoutProc" for mark segments.
 *
 * Results:
 *	If the mark isn't the insertion cursor then the return value is -1 to
 *	indicate that this segment shouldn't be displayed. If the mark is the
 *	insertion character then 1 is returned and the chunkPtr structure is
 *	filled in.
 *
 * Side effects:
 *	None, except for filling in chunkPtr.
 *
 *--------------------------------------------------------------
 */

static int
MarkLayoutProc(
    const TkTextIndex *indexPtr,/* Identifies first character in chunk. */
    TkTextSegment *segPtr,	/* Segment corresponding to indexPtr. */
    TCL_UNUSED(int),			/* Offset within segPtr corresponding to indexPtr (always 0). */
    TCL_UNUSED(int),			/* Chunk must not occupy pixels at this position or higher. */
    TCL_UNUSED(int),		/* Chunk must not include more than this many characters. */
    TCL_UNUSED(int),		/* 'true' means no characters have been assigned to this line yet. */
    TCL_UNUSED(TkWrapMode),	/* Not used. */
    TCL_UNUSED(TkTextSpaceMode),	/* Not used. */
    TkTextDispChunk *chunkPtr)	/* Structure to fill in with information about this chunk. The x
    				 * field has already been set by the caller. */
{
    TkText *textPtr = indexPtr->textPtr;

    assert(indexPtr->textPtr);

    if (segPtr != textPtr->insertMarkPtr) {
	return -1;
    }

    chunkPtr->layoutProcs = &layoutInsertProcs;
    chunkPtr->numBytes = 0;
    chunkPtr->minAscent = 0;
    chunkPtr->minDescent = 0;
    chunkPtr->minHeight = 0;
    chunkPtr->width = 0;

    /*
     * Note: can't break a line after the insertion cursor: this prevents the
     * insertion cursor from being stranded at the end of a line.
     */

    chunkPtr->breakIndex = -1;
    chunkPtr->clientData = textPtr;
    return 1;
}

/*
 *--------------------------------------------------------------
 *
 * TkTextDrawBlockCursor --
 *
 *	This function returns whether a block will be drawn, which covers
 *	characters.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

int
TkTextDrawBlockCursor(
    TkText *textPtr)		/* The current text widget. */
{
    if (textPtr->blockCursorType) {
	if (textPtr->flags & GOT_FOCUS) {
	    if ((textPtr->flags & INSERT_ON) || textPtr->selAttrs.border == textPtr->insertBorder) {
		return 1;
	    }
	} else if (textPtr->insertUnfocussed == TK_TEXT_INSERT_NOFOCUS_SOLID) {
	    return 1;
	}
    }
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * TkTextGetCursorBbox --
 *
 *	This function computes the cursor dimensions.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

int
TkTextGetCursorBbox(
    TkText *textPtr,		/* The current text widget. */
    int *x, int *y,		/* X/Y coordinate. */
    int *w, int *h)		/* Width/height of cursor. */
{
    TkTextIndex index;
    Tcl_UniChar thisChar;
    int cursorExtent;
    int charWidth = -1;

    assert(textPtr);
    assert(x);
    assert(y);
    assert(w);
    assert(h);

    cursorExtent = MAX(1, textPtr->insertWidth/2);
    TkTextMarkSegToIndex(textPtr, textPtr->insertMarkPtr, &index);

    if (!TkTextIndexBbox(textPtr, &index, 0, x, y, w, h, &charWidth, &thisChar)) {
	int base, ix, iw;

	/*
	 * Testing whether the cursor is visible is not as trivial at it seems,
	 * see this example:
	 *
	 * ~~~~~~~~~~~~~~~~
	 * |   +-----+
	 * |   |     |
	 * |~~~|~~~~~|~~~~~
	 * |   |     |
	 * | a |     |
	 * |   |     |
	 * |   +-----+
	 *
	 * At left side we have the visible cursor, then char "a", then a window.
	 * The region between the tilde bars is the visible screen. The cursor
	 * is positioned before char "a", and the bbox of char "a" is outside of
	 * the visible screen, so a simple test with TkTextIndexBbox() at char
	 * position "a" fails here. Now we have to test with the display line.
	 */

	if (!TkTextGetDLineInfo(textPtr, &index, 0, &ix, y, &iw, h, &base)) {
	    return 0; /* cursor is not visible at all */
	}

	if (charWidth == -1) {
	    /* This char is outside of the screen, so use a default. */
	    charWidth = textPtr->charWidth;
	}
    }

    /*
     * Don't draw the full lengh of a tab, in this case we are drawing
     * a cursor at the right boundary with a standard width.
     */

    if (thisChar == '\t') {
	*x += charWidth;
	charWidth = MIN(textPtr->charWidth, charWidth);
	*x -= charWidth;
    }

    if (textPtr->blockCursorType) {
	/* NOTE: the block cursor extent is always rounded towards zero. */
	*w = charWidth + 2*cursorExtent;
    } else {
	*w = textPtr->insertWidth;
    }

    *x -= cursorExtent;
    return 1;
}

/*
 *--------------------------------------------------------------
 *
 * TkTextGetCursorWidth --
 *
 *	This function computes the cursor dimensions.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

unsigned
TkTextGetCursorWidth(
    TkText *textPtr,		/* The current text widget. */
    TCL_UNUSED(int *),			/* Shift x coordinate, can be NULL. */
    int *extent)		/* Extent of cursor to left side, can be NULL. */
{
    int width;
    int cursorExtent = MAX(1, textPtr->insertWidth/2);

    if (extent) {
	*extent = -cursorExtent;
    }

    if (textPtr->blockCursorType) {
	int ix, iy, ih;

	if (!TkTextGetCursorBbox(textPtr, &ix, &iy, &width, &ih)) {
	    return 0; /* cursor is not visible at all */
	}
    } else {
	width = textPtr->insertWidth;
    }

    return width;
}

/*
 *--------------------------------------------------------------
 *
 * TkrTextInsertDisplayProc --
 *
 *	This function is called to display the insertion cursor.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Graphics are drawn.
 *
 *--------------------------------------------------------------
 */

void
TkrTextInsertDisplayProc(
    TkText *textPtr,		/* The current text widget. */
    TkTextDispChunk *chunkPtr,	/* Chunk that is to be drawn. */
    int x,			/* X-position in dst at which to draw this chunk (may differ
    				 * from the x-position in the chunk because of scrolling). */
    int y,			/* Y-position at which to draw this chunk in dst (x-position
    				 * is in the chunk itself). */
    int height,			/* Total height of line. */
    TCL_UNUSED(int),		/* Offset of baseline from y. */
    TCL_UNUSED(Display *),		/* Display to use for drawing. */
    Drawable dst,		/* Pixmap or window in which to draw chunk. */
    int screenY)		/* Y-coordinate in text window that corresponds to y. */
{
    int halfWidth = textPtr->insertWidth/2;
    int width = TkTextGetCursorWidth(textPtr, &x, NULL);
    int rightSideWidth = MAX(1, width + halfWidth - textPtr->insertWidth);

    if ((x + rightSideWidth) < 0) {
	/*
	 * The insertion cursor is off-screen. Indicate caret at 0,0 and return.
	 */

	Tk_SetCaretPos(textPtr->tkwin, 0, 0, height);
	return;
    }

    x -= halfWidth;
    if (halfWidth == 0 && x >= TkTextGetLastXPixel(textPtr)) {
	/* Ensure visibility of at least 1 pixel */
	x -= 1;
    }

    Tk_SetCaretPos(textPtr->tkwin, x, screenY, height);

    if (POINTER_IS_MARKED(chunkPtr)) {
	/*
	 * HACK: We are drawing into a tailored pixmap, because Tk has no clipping;
	 * see DisplayDLine().
	 */

	x = y = 0;
    }

    /*
     * As a special hack to keep the cursor visible on mono displays (or
     * anywhere else that the selection and insertion cursors have the same
     * color) write the default background in the cursor area (instead of
     * nothing) when the cursor isn't on. Otherwise the selection might hide
     * the cursor.
     */

    if (textPtr->flags & GOT_FOCUS) {
	if (textPtr->flags & INSERT_ON) {
	    Tk_Fill3DRectangle(textPtr->tkwin, dst, textPtr->insertBorder, x, y,
		    width, height, textPtr->insertBorderWidth, TK_RELIEF_RAISED);
	} else if (textPtr->selAttrs.border == textPtr->insertBorder) {
	    Tk_Fill3DRectangle(textPtr->tkwin, dst, textPtr->border, x, y,
		    width, height, 0, TK_RELIEF_FLAT);
	}
    } else if (textPtr->insertUnfocussed == TK_TEXT_INSERT_NOFOCUS_HOLLOW) {
	if (textPtr->insertBorderWidth < 1) {
	    /*
	     * Hack to work around the fact that a "solid" border always paints in black.
	     */

	    TkBorder *borderPtr = (TkBorder *) textPtr->insertBorder;

	    XDrawRectangle(Tk_Display(textPtr->tkwin), dst, borderPtr->bgGC, x, y,
		    width - 1, height - 1);
	} else {
	    Tk_Draw3DRectangle(textPtr->tkwin, dst, textPtr->insertBorder, x, y,
		    width, height, textPtr->insertBorderWidth, TK_RELIEF_RAISED);
	}
    } else if (textPtr->insertUnfocussed == TK_TEXT_INSERT_NOFOCUS_SOLID) {
	Tk_Fill3DRectangle(textPtr->tkwin, dst, textPtr->insertBorder, x, y,
		width, height, textPtr->insertBorderWidth, TK_RELIEF_RAISED);
    }
}

/*
 *--------------------------------------------------------------
 *
 * InsertUndisplayProc --
 *
 *	This function is called when the insertion cursor is no longer at a
 *	visible point on the display. It does nothing right now.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static void
InsertUndisplayProc(
    TCL_UNUSED(TkText *),		/* Overall information about text widget. */
    TkTextDispChunk *chunkPtr)	/* Chunk that is about to be freed. */
{
    chunkPtr->clientData = NULL;
    return;
}

/*
 *--------------------------------------------------------------
 *
 * MarkFindNext --
 *
 *	This function searches forward for the next mark.
 *
 * Results:
 *	A standard Tcl result, which is a mark name or an empty string.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static int
MarkFindNext(
    Tcl_Interp *interp,		/* For error reporting */
    TkText *textPtr,		/* The widget */
    int discardSpecial,	/* Discard marks "insert" and "current" when searching= */
    Tcl_Obj* indexObj,		/* Start search at this index. */
    const char *pattern,	/* Result must match this pattern, can be NULL. */
    int forward)		/* Search forward. */
{
    TkTextIndex index;
    Tcl_HashEntry *hPtr;
    TkTextSegment *segPtr;
    TkTextLine *linePtr;
    const char *indexStr;

    assert(textPtr);
    assert(indexObj);

    if (TkTextIsDeadPeer(textPtr)) {
	return TCL_OK;
    }

    indexStr = Tcl_GetString(indexObj);

    if (strcmp(indexStr, "insert") == 0) {
	segPtr = textPtr->insertMarkPtr;
	linePtr = segPtr->sectionPtr->linePtr;
	segPtr = forward ? segPtr->nextPtr : segPtr->prevPtr;
    } else if (strcmp(indexStr, "current") == 0) {
	segPtr = textPtr->currentMarkPtr;
	linePtr = segPtr->sectionPtr->linePtr;
	segPtr = forward ? segPtr->nextPtr : segPtr->prevPtr;
    } else {
	if ((hPtr = Tcl_FindHashEntry(&textPtr->sharedTextPtr->markTable, indexStr))) {
	    /*
	     * If given a mark name, return the next/prev mark in the list of segments, even
	     * if it happens to be at the same character position.
	     */

	    segPtr = (TkTextSegment *)Tcl_GetHashValue(hPtr);
	    if (!MarkToIndex(textPtr, segPtr, &index)) {
		return SetResultNoMarkNamed(interp, indexStr);
	    }
	    linePtr = segPtr->sectionPtr->linePtr;
	    segPtr = forward ? segPtr->nextPtr : segPtr->prevPtr;
	} else {
	    /*
	     * For non-mark name indices we want to return any marks that are
	     * right at the index, when searching forward, otherwise we do not
	     * return any marks that are right at the index.
	     */

	    if (!TkTextGetIndexFromObj(interp, textPtr, indexObj, &index)) {
		return TCL_ERROR;
	    }
	    segPtr = TkTextIndexGetFirstSegment(&index, NULL);
	    linePtr = segPtr->sectionPtr->linePtr;

	    if (!forward) {
		while (segPtr && segPtr->size == 0) {
		    segPtr = segPtr->prevPtr;
		}
	    }
	}
    }

    if (forward) {
	TkTextSegment *lastPtr = textPtr->endMarker;

	/*
	 * Ensure that we can reach 'lastPtr'.
	 */

	while (lastPtr->size == 0) {
	    lastPtr = lastPtr->nextPtr;
	}

	while (1) {
	    /*
	     * 'segPtr' points at the first possible candidate, or NULL if we ran
	     * off the end of the line.
	     */

	    for ( ; segPtr; segPtr = segPtr->nextPtr) {
		if (segPtr == lastPtr) {
		    return TCL_OK;
		}
		if (TkTextIsNormalOrSpecialMark(segPtr)) {
		    const char *name = TkTextMarkName(textPtr->sharedTextPtr, textPtr, segPtr);

		    if (name /* otherwise it's a special mark not belonging to this widget */
			    && (!discardSpecial || !TkTextIsSpecialMark(segPtr))
			    && (!pattern || Tcl_StringMatch(name, pattern))) {
			Tcl_SetObjResult(interp, Tcl_NewStringObj(name, TCL_INDEX_NONE));
			return TCL_OK;
		    }
		}
	    }

	    if (!(linePtr = linePtr->nextPtr)) {
		return TCL_OK;
	    }
	    segPtr = linePtr->segPtr;
	}
    } else {
	TkTextSegment *firstPtr = textPtr->startMarker;

	/*
	 * Ensure that we can reach 'firstPtr'.
	 */

	while (firstPtr->prevPtr && firstPtr->prevPtr->size == 0) {
	    firstPtr = firstPtr->prevPtr;
	}

	while (1) {
	    /*
	     * 'segPtr' points at the first possible candidate, or NULL if we ran
	     * off the start of the line.
	     */

	    for ( ; segPtr; segPtr = segPtr->prevPtr) {
		if (segPtr == firstPtr) {
		    return TCL_OK;
		}
		if (TkTextIsNormalOrSpecialMark(segPtr)) {
		    const char *name = TkTextMarkName(textPtr->sharedTextPtr, textPtr, segPtr);

		    if (name /* otherwise it's a special mark not belonging to this widget */
			    && (!discardSpecial || !TkTextIsSpecialMark(segPtr))
			    && (!pattern || Tcl_StringMatch(name, pattern))) {
			Tcl_SetObjResult(interp, Tcl_NewStringObj(name, TCL_INDEX_NONE));
			return TCL_OK;
		    }
		}
	    }

	    if (!(linePtr = linePtr->prevPtr)) {
		return TCL_OK;
	    }
	    segPtr = linePtr->lastPtr;
	}
    }

    return TCL_OK; /* never reached */
}

/*
 * ------------------------------------------------------------------------
 *
 * TkTextMarkName --
 *
 *	Returns the name of the mark that is the given text segment, or NULL
 *	if it is unnamed (i.e., a widget-specific mark that isn't "current" or
 *	"insert").
 *
 * Results:
 *	The name of the mark, or NULL.
 *
 * Side effects:
 *	None.
 *
 * ------------------------------------------------------------------------
 */

const char *
TkTextMarkName(
    const TkSharedText *sharedTextPtr,
    const TkText *textPtr,		/* can be NULL */
    const TkTextSegment *markPtr)
{
    assert(!IS_PRESERVED(markPtr));

    if (markPtr->insertMarkFlag) {
	return !textPtr || textPtr == markPtr->body.mark.textPtr ? "insert" : NULL;
    }
    if (markPtr->currentMarkFlag) {
	return !textPtr || textPtr == markPtr->body.mark.textPtr ? "current" : NULL;
    }
    if (!markPtr->body.mark.ptr) {
	return NULL;
    }
    return (const char *)Tcl_GetHashKey(&sharedTextPtr->markTable, (Tcl_HashEntry *) markPtr->body.mark.ptr);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 105
 * End:
 * vi:set ts=8 sw=4:
 */
