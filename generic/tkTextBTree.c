/*
 * tkTextBTree.c --
 *
 * This file contains code that manages the B-tree representation of text
 * for Tk's text widget and implements the character, hyphen, branch and
 * link segment types.
 *
 * Copyright (c) 1992-1994 The Regents of the University of California.
 * Copyright (c) 1994-1995 Sun Microsystems, Inc.
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#if defined(_MSC_VER ) && _MSC_VER < 1500
/* suppress wrong warnings to support ancient compilers */
#pragma warning (disable : 4018)
#endif

#include "tkInt.h"
#include "tkText.h"
#include "tkTextPriv.h"
#include "tkTextTagSet.h"
#include "tkAlloc.h"
#include <assert.h>

#ifndef MIN
# define MIN(a,b) (((int) a) < ((int) b) ? a : b)
#endif
#ifndef MAX
# define MAX(a,b) (((int) a) < ((int) b) ? b : a)
#endif
#ifndef ABS
# define ABS(a)   (a < 0 ? -a : a)
#endif

#ifdef NDEBUG
# define DEBUG(expr)
#else
# define DEBUG(expr) expr
#endif

/*
 * Implementation notes:
 *
 * Most of this file is independent of the text widget implementation and
 * representation now. Without much effort this could be developed further
 * into a new Tcl object type of which the Tk text widget is one example of a
 * client.
 * Note by GC: this independency is not useful, any sophisticated implementation
 * is specialised and in general not sharable. The independency has been broken
 * with the revised implementation (TkTextRedrawTag will be called here).
 *
 * The B-tree is set up with a dummy last line of text which must not be
 * displayed, and must _never_ have a non-zero pixel count. This dummy line is
 * a historical convenience to avoid other code having to deal with NULL
 * TkTextLines. Since Tk 8.5, with pixel line height calculations and peer
 * widgets, this dummy line is becoming somewhat of a liability, and special
 * case code has been required to deal with it. It is probably a good idea to
 * investigate removing the dummy line completely. This could result in an
 * overall simplification (although it would require new special case code to
 * deal with the fact that '.text index end' would then not really point to a
 * valid line, rather it would point to the beginning of a non-existent line
 * one beyond all current lines - we could perhaps define that as a
 * TkTextIndex with a NULL TkTextLine ptr).
 * Note by GC: the dummy line is quite useful, for instance it contains
 * mark segments.
 */

/*
 * Upper and lower bounds on how many children a node may have: rebalance when
 * either of these limits is exceeded. MAX_CHILDREN should be twice
 * MIN_CHILDREN, and MIN_CHILDREN must be >= 2.
 */

#define MIN_CHILDREN 16u
#define MAX_CHILDREN (2u*MIN_CHILDREN)

/*
 * The data structure below defines a node in the B-tree.
 */

typedef struct TkBTreeNodePixelInfo {
    uint32_t pixels;		/* Number of vertical display pixels. */
    uint32_t numDispLines;	/* NUmber of display lines. */
} NodePixelInfo;

typedef struct Node {
    struct Node *parentPtr;	/* Pointer to parent node, or NULL if this is the root. */
    struct Node *nextPtr;	/* Next in list of siblings with the same parent node, or
    				 * NULL for end of list. */
    struct Node *childPtr;	/* List of children (used if level > 0). */
    TkTextLine *linePtr;	/* Level > 0: first line in leftmost leaf; else first line
    				 * in children. */
    TkTextLine *lastPtr;	/* Level > 0: Last line in rightmost leaf; else last line
    				 * in children. */
    TkTextTagSet *tagonPtr;	/* The union of tagonPtr over all childrens/lines. */
    TkTextTagSet *tagoffPtr;	/* The union of tagoffPtr over all childrens/lines. */
    NodePixelInfo *pixelInfo;	/* Array containing pixel information in the subtree rooted here,
    				 * one entry for each peer widget. */
    uint32_t level;		/* Level of this node in the B-tree. 0 refers to the bottom of
    				 * the tree (children are lines, not nodes). */
    uint32_t size;		/* Sum of size over all lines belonging to this node. */
    uint32_t numChildren;	/* Number of children of this node. */
    uint32_t numLines;		/* Total number of lines (leaves) in the subtree rooted here. */
    uint32_t numLogicalLines;	/* Total number of logical lines (a line whose predecessing line
    				 * don't have an elided newline). */
    uint32_t numBranches;	/* Counting the number of branches in this node. */
} Node;

/*
 * Used to avoid having to allocate and deallocate arrays on the fly for
 * commonly used functions. Must be > 0.
 */

#define PIXEL_CLIENTS 8

/*
 * Number of segments inside a section of segments. MAX_TEXT_SEGS must be
 * greater than MIN_TEXT_SEGS. Also take into account that the sum
 * (MAX_TEXT_SEGS + NUM_TEXT_SEGS) should not exceed the bit length of
 * 'length' in struct TkTextSection.
 */

#define MIN_TEXT_SEGS 20
#define MAX_TEXT_SEGS 60
#define NUM_TEXT_SEGS (MAX_TEXT_SEGS - MIN_TEXT_SEGS)

/*
 * Definition of flags for UpdateElideInfo.
 */

enum { ELISION_WILL_BE_REMOVED, ELISION_HAS_BEEN_ADDED, ELISION_HAS_BEEN_CHANGED };

typedef struct TkTextMyBTree BTree; /* see TkTextPriv.h */

/*
 * Variable that indicates whether to enable consistency checks for debugging.
 */

bool tkBTreeDebug = false;

/*
 * Macros that determine how much space to allocate for new segments:
 */

/* Computer math magic: (k/8)*8 == k & -8 */
#define CSEG_CAPACITY(chars) ((int) (chars + 8) & -8)
#define CSEG_SIZE(capacity) ((unsigned) (Tk_Offset(TkTextSegment, body) + capacity))

/*
 * Helper struct for SplitSeg.
 */

typedef struct SplitInfo {
    int offset;		/* Out: Offset for insertion, -1 if SplitSeg
    			 * did not increase/decrease the segment. */
    int increase;	/* In: Additional bytes required for the insertion of new chars.
    			 * Can be negative, in this case the size will be decreased.
			 */
    bool splitted;	/* Out: Flag whether a split has been done. */
    bool forceSplit;	/* In: The char segment must be split after offset, because a
    			 * newline will be inserted, and we shift the content after
			 * offset into the new line. */
    TkTextTagSet *tagInfoPtr;
    			/* in: Tag information of new segment, can be NULL.
			 * Out: Tag information of char segment, when inserting. */
} SplitInfo;

/*
 * Forward declarations for functions defined in this file:
 */

struct UndoTokenInsert;

static unsigned		AdjustPixelClient(BTree *treePtr, unsigned defaultHeight, Node *nodePtr,
			    TkTextLine *startLine, TkTextLine *endLine, unsigned useReference,
			    unsigned newPixelReferences, unsigned *numDispLinesPtr);
static TkTextSegment *	JoinCharSegments(const TkSharedText *sharedTextPtr, TkTextSegment *segPtr);
static void		CleanupSplitPoint(TkTextSegment *segPtr, TkSharedText *sharedTextPtr);
static void		CharCheckProc(const TkSharedText *sharedTextPtr, const TkTextSegment *segPtr);
static bool		CharDeleteProc(TkTextBTree tree, TkTextSegment *segPtr, int flags);
static Tcl_Obj *	CharInspectProc(const TkSharedText *sharedTextPtr, const TkTextSegment *segPtr);
static TkTextSegment *	CleanupCharSegments(const TkSharedText *sharedTextPtr, TkTextSegment *segPtr);
static bool		HyphenDeleteProc(TkTextBTree tree, TkTextSegment *segPtr, int flags);
static void		HyphenCheckProc(const TkSharedText *sharedTextPtr, const TkTextSegment *segPtr);
static Tcl_Obj *	HyphenInspectProc(const TkSharedText *sharedTextPtr,
			    const TkTextSegment *segPtr);
static TkTextSegment *	IncreaseCharSegment(TkTextSegment *segPtr, unsigned offset, int chunkSize);
static void		FreeLine(const BTree *treePtr, TkTextLine *linePtr);
static void		LinkSegment(TkTextLine *linePtr, TkTextSegment *predPtr, TkTextSegment *succPtr);
static void		LinkMark(const TkSharedText *sharedTextPtr, TkTextLine *linePtr,
			    TkTextSegment *prevPtr, TkTextSegment *segPtr);
static void		LinkSwitch(TkTextLine *linePtr, TkTextSegment *predPtr, TkTextSegment *succPtr);
static TkTextSegment *	MakeCharSeg(TkTextSection *sectionPtr, TkTextTagSet *tagInfoPtr,
			    unsigned newSize, const char *string, unsigned length);
static TkTextSegment *	CopyCharSeg(TkTextSegment *segPtr, unsigned offset,
			    unsigned length, unsigned newSize);
static TkTextSegment *	SplitCharSegment(TkTextSegment *segPtr, unsigned index);
static void		CheckNodeConsistency(const TkSharedText *sharedTextPtr, const Node *nodePtr,
			    const Node *rootPtr, unsigned references);
static void		RebuildSections(TkSharedText *sharedTextPtr, TkTextLine *linePtr,
			    bool propagateChangeOfNumBranches);
static bool		CheckSegments(const TkSharedText *sharedTextPtr, const TkTextLine *linePtr);
static bool		CheckSections(const TkTextLine *linePtr);
static bool		CheckSegmentItems(const TkSharedText *sharedTextPtr, const TkTextLine *linePtr);
static void		FreeNode(Node *nodePtr);
static void		DestroyNode(TkTextBTree tree, Node *nodePtr);
static void		DeleteEmptyNode(BTree *treePtr, Node *nodePtr);
static TkTextSegment *	FindTagStart(TkTextSearch *searchPtr, const TkTextIndex *stopIndex);
static TkTextSegment *	FindTagEnd(TkTextSearch *searchPtr, const TkTextIndex *stopIndex);
static void		Rebalance(BTree *treePtr, Node *nodePtr);
static void		RemovePixelClient(BTree *treePtr, Node *nodePtr, unsigned useReference,
			    int overwriteWithLast);
static TkTextTagSet *	MakeTagInfo(TkText *textPtr, TkTextSegment *segPtr);
static TkTextLine *	InsertNewLine(TkSharedText *sharedTextPtr, Node *nodePtr,
			    TkTextLine *prevLinePtr, TkTextSegment *segPtr);
static TkTextSegment *	SplitSeg(const TkTextIndex *indexPtr, SplitInfo *splitInfo);
static TkTextSegment *	PrepareInsertIntoCharSeg(TkTextSegment *segPtr,
			    unsigned offset, SplitInfo *splitInfo);
static void		SplitSection(TkTextSection *sectionPtr);
static void		JoinSections(TkTextSection *sectionPtr);
static void		FreeSections(TkTextSection *sectionPtr);
static TkTextSegment *	UnlinkSegment(TkTextSegment *segPtr);
static void		UnlinkSegmentAndCleanup(const TkSharedText *sharedTextPtr,
			    TkTextSegment *segPtr);
static unsigned		CountSegments(const TkTextSection *sectionPtr);
static unsigned		ComputeSectionSize(const TkTextSegment *segPtr);
static void		BranchCheckProc(const TkSharedText *sharedTextPtr, const TkTextSegment *segPtr);
static bool		BranchDeleteProc(TkTextBTree tree, TkTextSegment *segPtr, int flags);
static void		BranchRestoreProc(TkTextSegment *segPtr);
static Tcl_Obj *	BranchInspectProc(const TkSharedText *sharedTextPtr,
			    const TkTextSegment *segPtr);
static void		LinkCheckProc(const TkSharedText *sharedTextPtr, const TkTextSegment *segPtr);
static bool		LinkDeleteProc(TkTextBTree tree, TkTextSegment *segPtr, int flags);
static void		LinkRestoreProc(TkTextSegment *segPtr);
static Tcl_Obj *	LinkInspectProc(const TkSharedText *sharedTextPtr, const TkTextSegment *segPtr);
static bool		ProtectionMarkDeleteProc(TkTextBTree tree, TkTextSegment *segPtr, int flags);
static void		ProtectionMarkCheckProc(const TkSharedText *sharedTextPtr,
			    const TkTextSegment *segPtr);
static void		AddPixelCount(BTree *treePtr, TkTextLine *linePtr,
			    const TkTextLine *refLinePtr, NodePixelInfo *changeToPixels);
static void		SubtractPixelInfo(BTree *treePtr, TkTextLine *linePtr);
static void		SubtractPixelCount2(BTree *treePtr, Node *nodePtr, int changeToLineCount,
			    int changeToLogicalLineCount, int changeToBranchCount, int changeToSize,
			    const NodePixelInfo *changeToPixelInfo);
static void		DeleteIndexRange(TkSharedText *sharedTextPtr,
			    TkTextIndex *indexPtr1, TkTextIndex *indexPtr2, int flags,
			    const struct UndoTokenInsert *undoToken, TkTextUndoInfo *redoInfo);
static void		DeleteRange(TkSharedText *sharedTextPtr,
			    TkTextSegment *firstSegPtr, TkTextSegment *lastSegPtr,
			    int flags, TkTextUndoInfo *redoInfo);
static void		UpdateNodeTags(const TkSharedText *sharedTextPtr, Node *nodePtr);
static void		MakeUndoIndex(const TkSharedText *sharedTextPtr, const TkTextIndex *indexPtr,
			    TkTextUndoIndex *undoIndexPtr, int gravity);
static bool		UndoIndexIsEqual(const TkTextUndoIndex *indexPtr1,
			    const TkTextUndoIndex *indexPtr2);
static void		AddTagToNode(Node *nodePtr, TkTextTag *tag, bool setTagoff);
static void		RemoveTagFromNode(Node *nodePtr, TkTextTag *tag);
static void		UpdateElideInfo(TkSharedText *sharedTextPtr, TkTextTag *tagPtr,
			    TkTextSegment **firstSegPtr, TkTextSegment **lastSegPtr, unsigned reason);
static bool		SegmentIsElided(const TkSharedText *sharedTextPtr, const TkTextSegment *segPtr,
			    const TkText *textPtr);
static TkTextLine *	GetStartLine(const TkSharedText *sharedTextPtr, const TkText *textPtr);
static TkTextLine *	GetLastLine(const TkSharedText *sharedTextPtr, const TkText *textPtr);
static void		ReInsertSegment(const TkSharedText *sharedTextPtr,
			    const TkTextUndoIndex *indexPtr, TkTextSegment *segPtr, bool updateNode);

/*
 * Type record for character segments:
 */

const Tk_SegType tkTextCharType = {
    "character",		/* name */
    SEG_GROUP_CHAR,		/* group */
    GRAVITY_NEUTRAL,		/* gravity */
    CharDeleteProc,		/* deleteProc */
    NULL,			/* restoreProc */
    TkTextCharLayoutProc,	/* layoutProc */
    CharCheckProc,		/* checkProc */
    CharInspectProc		/* inspectProc */
};

/*
 * Type record for hyphenation support.
 */

const Tk_SegType tkTextHyphenType = {
    "hyphen",			/* name */
    SEG_GROUP_HYPHEN,		/* group */
    GRAVITY_NEUTRAL,		/* gravity */
    HyphenDeleteProc,		/* deleteProc */
    NULL,			/* restoreProc */
    TkTextCharLayoutProc,	/* layoutProc */
    HyphenCheckProc,		/* checkProc */
    HyphenInspectProc		/* inspectProc */
};

/*
 * Type record for segments marking a branch for normal/elided text:
 */

const Tk_SegType tkTextBranchType = {
    "branch",			/* name */
    SEG_GROUP_BRANCH,		/* group */
    GRAVITY_RIGHT,		/* gravity */
    BranchDeleteProc,		/* deleteProc */
    BranchRestoreProc,		/* restoreProc */
    NULL,			/* layoutProc */
    BranchCheckProc,		/* checkProc */
    BranchInspectProc		/* inspectProc */
};

/*
 * Type record for segments marking a link for a switched chain:
 */

const Tk_SegType tkTextLinkType = {
    "connection",		/* name */
    SEG_GROUP_BRANCH,		/* group */
    GRAVITY_LEFT,		/* gravity */
    LinkDeleteProc,		/* deleteProc */
    LinkRestoreProc,		/* restoreProc */
    NULL,			/* layoutProc */
    LinkCheckProc,		/* checkProc */
    LinkInspectProc		/* inspectProc */
};

/*
 * Type record for the deletion marks.
 */

const Tk_SegType tkTextProtectionMarkType = {
    "protection",		/* name */
    SEG_GROUP_PROTECT,		/* group */
    GRAVITY_NEUTRAL,		/* gravity */
    ProtectionMarkDeleteProc,	/* deleteProc */
    NULL,			/* restoreProc */
    NULL,			/* layoutProc */
    ProtectionMarkCheckProc,	/* checkProc */
    NULL			/* inspectProc */
};

/*
 * We need some private undo/redo stuff.
 */

typedef struct UndoTagChange {
    TkTextTagSet *tagInfoPtr;
    uint32_t skip;
    uint32_t size;
} UndoTagChange;

static void UndoTagPerform(TkSharedText *, TkTextUndoInfo *, TkTextUndoInfo *, bool);
static void UndoTagDestroy(TkSharedText *, TkTextUndoToken *token, bool);
static Tcl_Obj *UndoTagGetCommand(const TkSharedText *, const TkTextUndoToken *);

static void UndoClearTagsPerform(TkSharedText *, TkTextUndoInfo *, TkTextUndoInfo *, bool);
static void RedoClearTagsPerform(TkSharedText *, TkTextUndoInfo *, TkTextUndoInfo *, bool);
static void UndoClearTagsDestroy(TkSharedText *, TkTextUndoToken *token, bool);
static Tcl_Obj *UndoClearTagsGetCommand(const TkSharedText *, const TkTextUndoToken *);
static Tcl_Obj *UndoClearTagsInspect(const TkSharedText *, const TkTextUndoToken *);

static void UndoDeletePerform(TkSharedText *, TkTextUndoInfo *, TkTextUndoInfo *, bool);
static void RedoDeletePerform(TkSharedText *, TkTextUndoInfo *, TkTextUndoInfo *, bool);
static void UndoDeleteDestroy(TkSharedText *, TkTextUndoToken *token, bool);
static Tcl_Obj *UndoDeleteGetCommand(const TkSharedText *, const TkTextUndoToken *);
static Tcl_Obj *UndoDeleteInspect(const TkSharedText *, const TkTextUndoToken *);
static Tcl_Obj *RedoDeleteInspect(const TkSharedText *, const TkTextUndoToken *);
static Tcl_Obj *RedoInsertInspect(const TkSharedText *, const TkTextUndoToken *);

static void UndoInsertPerform(TkSharedText *, TkTextUndoInfo *, TkTextUndoInfo *, bool);
static Tcl_Obj *UndoInsertGetCommand(const TkSharedText *, const TkTextUndoToken *);

static void UndoGetRange(const TkSharedText *, const TkTextUndoToken *, TkTextIndex *, TkTextIndex *);

static const Tk_UndoType undoTokenTagType = {
    TK_TEXT_UNDO_TAG,		/* action */
    UndoTagGetCommand,		/* commandProc */
    UndoTagPerform,		/* undoProc */
    UndoTagDestroy,		/* destroyProc */
    UndoGetRange,		/* rangeProc */
    TkBTreeUndoTagInspect	/* inspectProc */
};

static const Tk_UndoType redoTokenTagType = {
    TK_TEXT_REDO_TAG,		/* action */
    UndoTagGetCommand,		/* commandProc */
    UndoTagPerform,		/* undoProc */
    UndoTagDestroy,		/* destroyProc */
    UndoGetRange,		/* rangeProc */
    TkBTreeUndoTagInspect	/* inspectProc */
};

static const Tk_UndoType undoTokenClearTagsType = {
    TK_TEXT_UNDO_TAG_CLEAR,	/* action */
    UndoClearTagsGetCommand,	/* commandProc */
    UndoClearTagsPerform,	/* undoProc */
    UndoClearTagsDestroy,	/* destroyProc */
    UndoGetRange,		/* rangeProc */
    UndoClearTagsInspect	/* inspectProc */
};

static const Tk_UndoType redoTokenClearTagsType = {
    TK_TEXT_REDO_TAG_CLEAR,	/* action */
    UndoClearTagsGetCommand,	/* commandProc */
    RedoClearTagsPerform,	/* undoProc */
    NULL,			/* destroyProc */
    UndoGetRange,		/* rangeProc */
    UndoClearTagsGetCommand	/* inspectProc */
};

static const Tk_UndoType undoTokenDeleteType = {
    TK_TEXT_UNDO_DELETE,	/* action */
    UndoDeleteGetCommand,	/* commandProc */
    UndoDeletePerform,		/* undoProc */
    UndoDeleteDestroy,		/* destroyProc */
    UndoGetRange,		/* rangeProc */
    UndoDeleteInspect		/* inspectProc */
};

static const Tk_UndoType redoTokenDeleteType = {
    TK_TEXT_REDO_DELETE,	/* action */
    UndoDeleteGetCommand,	/* commandProc */
    RedoDeletePerform,		/* undoProc */
    NULL,			/* destroyProc */
    UndoGetRange,		/* rangeProc */
    RedoDeleteInspect		/* inspectProc */
};

static const Tk_UndoType undoTokenInsertType = {
    TK_TEXT_UNDO_INSERT,	/* action */
    UndoInsertGetCommand,	/* commandProc */
    UndoInsertPerform,		/* undoProc */
    NULL,			/* destroyProc */
    UndoGetRange,		/* rangeProc */
    UndoInsertGetCommand	/* inspectProc */
};

static const Tk_UndoType redoTokenInsertType = {
    TK_TEXT_REDO_INSERT,	/* action */
    UndoInsertGetCommand,	/* commandProc */
    UndoDeletePerform,		/* undoProc */
    UndoDeleteDestroy,		/* destroyProc */
    UndoGetRange,		/* rangeProc */
    RedoInsertInspect		/* inspectProc */
};

/* Derivation of TkTextUndoTokenRange */
typedef struct UndoTokenDelete {
    const Tk_UndoType *undoType;
    TkTextUndoIndex startIndex;	/* Start of deletion range. */
    TkTextUndoIndex endIndex;	/* End of deletion range. */
    TkTextSegment **segments;	/* Array containing the deleted segments. */
    uint32_t numSegments:31;	/* Number of segments. */
    uint32_t inclusive:1;	/* Inclusive bounds? */
} UndoTokenDelete;

/* Derivation of TkTextUndoTokenRange */
typedef struct UndoTokenInsert {
    const Tk_UndoType *undoType;
    TkTextUndoIndex startIndex;	/* Start of insertion range. */
    TkTextUndoIndex endIndex;	/* End of insertion range. */
} UndoTokenInsert;

/* Derivation of TkTextUndoTokenRange */
typedef struct UndoTokenTagChange {
    const Tk_UndoType *undoType;
    TkTextUndoIndex startIndex;	/* Start of insertion range. */
    TkTextUndoIndex endIndex;	/* End of insertion range. */
    TkTextTag *tagPtr;		/* Added/removed tag. */
    int32_t *lengths;		/* Array of tagged lengths (in byte size): if negative: skip this part;
    				 * if positive: tag/untag this part. Last entry is 0 (zero). This
				 * attribute can be NULL. Any part outside of this array will be
				 * tagged/untagged. */
} UndoTokenTagChange;

/* Derivation of TkTextUndoTokenRange */
typedef struct UndoTokenTagClear {
    const Tk_UndoType *undoType;
    TkTextUndoIndex startIndex;	/* Start of clearing range. */
    TkTextUndoIndex endIndex;	/* End of clearing range. */
    UndoTagChange *changeList;
    unsigned changeListSize;
} UndoTokenTagClear;

/* Derivation of TkTextUndoTokenRange */
typedef struct RedoTokenClearTags {
    const Tk_UndoType *undoType;
    TkTextUndoIndex startIndex;	/* Start of clearing range. */
    TkTextUndoIndex endIndex;	/* End of clearing range. */
} RedoTokenClearTags;

/*
 * Pointer to int, for some portable pointer hacks - it's guaranteed that
 * 'uintptr_'t and 'void *' are convertible in both directions (C99 7.18.1.4).
 */

typedef union {
    void *ptr;
    uintptr_t flag;
} __ptr_to_int;

#define POINTER_IS_MARKED(ptr)	((bool)(((__ptr_to_int *) &ptr)->flag & (uintptr_t) 1))
#define MARK_POINTER(ptr)	(((__ptr_to_int *) &ptr)->flag |= (uintptr_t) 1)
#define UNMARK_POINTER(ptr)	(((__ptr_to_int *) &ptr)->flag &= ~(uintptr_t) 1)
#define UNMARKED_INT(ptr)	(((__ptr_to_int *) &ptr)->flag & ~(uintptr_t) 1)

DEBUG_ALLOC(extern unsigned tkTextCountNewSegment);
DEBUG_ALLOC(extern unsigned tkTextCountDestroySegment);
DEBUG_ALLOC(extern unsigned tkTextCountNewNode);
DEBUG_ALLOC(extern unsigned tkTextCountDestroyNode);
DEBUG_ALLOC(extern unsigned tkTextCountNewPixelInfo);
DEBUG_ALLOC(extern unsigned tkTextCountDestroyPixelInfo);
DEBUG_ALLOC(extern unsigned tkTextCountNewLine);
DEBUG_ALLOC(extern unsigned tkTextCountDestroyLine);
DEBUG_ALLOC(extern unsigned tkTextCountNewSection);
DEBUG_ALLOC(extern unsigned tkTextCountDestroySection);
DEBUG_ALLOC(extern unsigned tkTextCountNewUndoToken);
DEBUG_ALLOC(extern unsigned tkTextCountDestroyDispInfo);

/*
 * Some helpers, especially for tag set operations.
 */

static unsigned
GetByteLength(
    Tcl_Obj *objPtr)
{
    assert(objPtr);

    if (!objPtr->bytes) {
	Tcl_GetString(objPtr);
    }
    return objPtr->length;
}

static bool
SegIsAtStartOfLine(
    const TkTextSegment *segPtr)
{
    while (segPtr && segPtr->size == 0) {
	segPtr = segPtr->prevPtr;
    }
    return !segPtr;
}

static bool
SegIsAtEndOfLine(
    const TkTextSegment *segPtr)
{
    while (segPtr && segPtr->size == 0) {
	segPtr = segPtr->nextPtr;
    }
    return !segPtr->nextPtr;
}

static TkTextSegment *
GetPrevTagInfoSegment(
    TkTextSegment *segPtr)
{
    TkTextLine *linePtr;

    assert(segPtr);

    linePtr = segPtr->sectionPtr->linePtr;

    for (segPtr = segPtr->prevPtr; segPtr; segPtr = segPtr->prevPtr) {
	if (segPtr->tagInfoPtr) {
	    return segPtr;
	}
    }

    return (linePtr = linePtr->prevPtr) ? linePtr->lastPtr : NULL;
}

static TkTextSegment *
GetNextTagInfoSegment(
    TkTextSegment *segPtr)
{
    assert(segPtr);

    for ( ; !segPtr->tagInfoPtr; segPtr = segPtr->nextPtr) {
	assert(segPtr);
    }
    return segPtr;
}

static TkTextSegment *
GetFirstTagInfoSegment(
    const TkText *textPtr,	/* can be NULL */
    const TkTextLine *linePtr)
{
    TkTextSegment *segPtr;

    assert(linePtr);

    if (textPtr && linePtr == textPtr->startMarker->sectionPtr->linePtr) {
	segPtr = textPtr->startMarker;
    } else {
	segPtr = linePtr->segPtr;
    }

    return GetNextTagInfoSegment(segPtr);
}

static bool
TagSetTestBits(
    const TkTextTagSet *tagInfoPtr,
    const TkBitField *bitField)		/* can be NULL */
{
    assert(tagInfoPtr);

    if (TkTextTagSetIsEmpty(tagInfoPtr)) {
	return false;
    }
    return !bitField || !TkTextTagBitContainsSet(bitField, tagInfoPtr);
}

static bool
TagSetTestDisjunctiveBits(
    const TkTextTagSet *tagInfoPtr,
    const TkBitField *bitField)		/* can be NULL */
{
    assert(tagInfoPtr);

    if (bitField) {
	return TkTextTagSetDisjunctiveBits(tagInfoPtr, bitField);
    }
    return !TkTextTagSetIsEmpty(tagInfoPtr);
}

static bool
TagSetTestDontContainsAny(
    const TkTextTagSet *tagonPtr,
    const TkTextTagSet *tagoffPtr,
    const TkBitField *bitField)		/* can be NULL */
{
    assert(tagonPtr);
    assert(tagoffPtr);

    return !TagSetTestDisjunctiveBits(tagonPtr, bitField)
	    || TagSetTestDisjunctiveBits(tagoffPtr, bitField);
}

static bool
TestTag(
    const TkTextTagSet *tagInfoPtr,
    const TkTextTag *tagPtr)	/* can be NULL */
{
    return tagPtr ? TkTextTagSetTest(tagInfoPtr, tagPtr->index) : TkTextTagSetAny(tagInfoPtr);
}

static void
TagSetAssign(
    TkTextTagSet **dstRef,
    TkTextTagSet *srcPtr)
{
    if (*dstRef != srcPtr) {
	TkTextTagSetDecrRefCount(*dstRef);
	TkTextTagSetIncrRefCount(srcPtr);
	*dstRef = srcPtr;
    }
}

static void
TagSetReplace(
    TkTextTagSet **dstRef,
    TkTextTagSet *srcPtr)
{
    TkTextTagSetDecrRefCount(*dstRef);
    *dstRef = srcPtr;
}

static TkTextTagSet *
TagSetAdd(
    TkTextTagSet *tagInfoPtr,
    const TkTextTag *tagPtr)
{
#if !TK_TEXT_DONT_USE_BITFIELDS
    if (tagPtr->index >= TkTextTagSetSize(tagInfoPtr)) {
	assert(tagPtr->index < tagPtr->sharedTextPtr->tagInfoSize);
	tagInfoPtr = TkTextTagSetResize(tagInfoPtr, tagPtr->sharedTextPtr->tagInfoSize);
    }
#endif /* !TK_TEXT_DONT_USE_BITFIELDS */

    return TkTextTagSetAdd(tagInfoPtr, tagPtr->index);
}

static TkTextTagSet *
TagSetErase(
    TkTextTagSet *tagInfoPtr,
    const TkTextTag *tagPtr)
{
    if (tagPtr->index >= TkTextTagSetSize(tagInfoPtr)) {
	return tagInfoPtr;
    }
    if (TkTextTagSetIsEmpty(tagInfoPtr = TkTextTagSetErase(tagInfoPtr, tagPtr->index))) {
	TagSetAssign(&tagInfoPtr, tagPtr->sharedTextPtr->emptyTagInfoPtr);
    }
    return tagInfoPtr;
}

static TkTextTagSet *
TagSetAddOrErase(
    TkTextTagSet *tagInfoPtr,
    const TkTextTag *tagPtr,
    bool add)
{
    return add ? TagSetAdd(tagInfoPtr, tagPtr) : TagSetErase(tagInfoPtr, tagPtr);
}

static TkTextTagSet *
TagSetRemove(
    TkTextTagSet *tagInfoPtr,
    const TkTextTagSet *otherInfoPtr,
    const TkSharedText *sharedTextPtr)
{
    if (TkTextTagSetIsEmpty(tagInfoPtr = TkTextTagSetRemove(tagInfoPtr, otherInfoPtr))) {
	TagSetAssign(&tagInfoPtr, sharedTextPtr->emptyTagInfoPtr);
    }
    return tagInfoPtr;
}

static TkTextTagSet *
TagSetRemoveBits(
    TkTextTagSet *tagInfoPtr,
    const TkBitField *otherInfoPtr,
    const TkSharedText *sharedTextPtr)
{
    if (TkTextTagSetIsEmpty(tagInfoPtr = TkTextTagSetRemoveBits(tagInfoPtr, otherInfoPtr))) {
	TagSetAssign(&tagInfoPtr, sharedTextPtr->emptyTagInfoPtr);
    }
    return tagInfoPtr;
}

static TkTextTagSet *
TagSetJoin(
    TkTextTagSet *tagInfoPtr,		/* can be NULL */
    const TkTextTagSet *otherInfoPtr)
{
    if (!tagInfoPtr) {
	TkTextTagSetIncrRefCount(tagInfoPtr = (TkTextTagSet *) otherInfoPtr);
    } else {
	tagInfoPtr = TkTextTagSetJoin(tagInfoPtr, otherInfoPtr);
    }
    return tagInfoPtr;
}

static TkTextTagSet *
TagSetJoinNonIntersection(
    TkTextTagSet *tagInfoPtr,
    const TkTextTagSet *otherInfoPtr1,
    const TkTextTagSet *otherInfoPtr2,
    const TkSharedText *sharedTextPtr)
{
    assert(tagInfoPtr);
    assert(otherInfoPtr1);
    assert(otherInfoPtr2);

    if (otherInfoPtr1 == otherInfoPtr2) {
	/* This is especially catching the case that both otherInfoPtr are empty. */
	return tagInfoPtr;
    }

#if !TK_TEXT_DONT_USE_BITFIELDS
    if (TkTextTagSetSize(tagInfoPtr) < sharedTextPtr->tagInfoSize) {
	unsigned size = MAX(TkTextTagSetSize(otherInfoPtr1), TkTextTagSetSize(otherInfoPtr2));
	tagInfoPtr = TkTextTagSetResize(tagInfoPtr, MAX(size, sharedTextPtr->tagInfoSize));
    }
#endif /* !TK_TEXT_DONT_USE_BITFIELDS */

    tagInfoPtr = TkTextTagSetJoinNonIntersection(tagInfoPtr, otherInfoPtr1, otherInfoPtr2);

    if (TkTextTagSetIsEmpty(tagInfoPtr)) {
	TagSetAssign(&tagInfoPtr, sharedTextPtr->emptyTagInfoPtr);
    }

    return tagInfoPtr;
}

static TkTextTagSet *
TagSetIntersect(
    TkTextTagSet *tagInfoPtr,		/* can be NULL */
    const TkTextTagSet *otherInfoPtr,
    const TkSharedText *sharedTextPtr)
{
    if (!tagInfoPtr) {
	TkTextTagSetIncrRefCount(tagInfoPtr = (TkTextTagSet *) otherInfoPtr);
    } else if (TkTextTagSetIsEmpty(tagInfoPtr = TkTextTagSetIntersect(tagInfoPtr, otherInfoPtr))) {
	TagSetAssign(&tagInfoPtr, sharedTextPtr->emptyTagInfoPtr);
    }
    return tagInfoPtr;
}

static TkTextTagSet *
TagSetIntersectBits(
    TkTextTagSet *tagInfoPtr,
    const TkBitField *otherInfoPtr,
    const TkSharedText *sharedTextPtr)
{
    if (TkTextTagSetIsEmpty(tagInfoPtr = TkTextTagSetIntersectBits(tagInfoPtr, otherInfoPtr))) {
	TagSetAssign(&tagInfoPtr, sharedTextPtr->emptyTagInfoPtr);
    }
    return tagInfoPtr;
}

static TkTextTagSet *
TagSetComplementTo(
    TkTextTagSet *tagInfoPtr,
    const TkTextTagSet *otherInfoPtr,
    const TkSharedText *sharedTextPtr)
{
    if (TkTextTagSetIsEmpty(tagInfoPtr = TkTextTagSetComplementTo(tagInfoPtr, otherInfoPtr))) {
	TagSetAssign(&tagInfoPtr, sharedTextPtr->emptyTagInfoPtr);
    }
    return tagInfoPtr;
}

static TkTextTagSet *
TagSetJoinComplementTo(
    TkTextTagSet *tagInfoPtr,
    const TkTextTagSet *otherInfoPtr1,
    const TkTextTagSet *otherInfoPtr2,
    const TkSharedText *sharedTextPtr)
{
    if (otherInfoPtr2 == sharedTextPtr->emptyTagInfoPtr) {
	return tagInfoPtr;
    }

#if !TK_TEXT_DONT_USE_BITFIELDS
    if (TkTextTagSetSize(tagInfoPtr) < sharedTextPtr->tagInfoSize) {
	tagInfoPtr = TkTextTagSetResize(tagInfoPtr, sharedTextPtr->tagInfoSize);
    }
#endif /* !TK_TEXT_DONT_USE_BITFIELDS */

    if (TkTextTagSetIsEmpty(
	    tagInfoPtr = TkTextTagSetJoinComplementTo(tagInfoPtr, otherInfoPtr1, otherInfoPtr2))) {
	TagSetAssign(&tagInfoPtr, sharedTextPtr->emptyTagInfoPtr);
    }
    return tagInfoPtr;
}

static TkTextTagSet *
TagSetJoinOfDifferences(
    TkTextTagSet *tagInfoPtr,
    const TkTextTagSet *otherInfoPtr1,
    const TkTextTagSet *otherInfoPtr2,
    const TkSharedText *sharedTextPtr)
{
#if !TK_TEXT_DONT_USE_BITFIELDS
    if (TkTextTagSetSize(tagInfoPtr) < sharedTextPtr->tagInfoSize) {
	tagInfoPtr = TkTextTagSetResize(tagInfoPtr, sharedTextPtr->tagInfoSize);
    }
#endif /* !TK_TEXT_DONT_USE_BITFIELDS */

    return TkTextTagSetJoinOfDifferences(tagInfoPtr, otherInfoPtr1, otherInfoPtr2);
}

static TkTextTagSet *
TagSetTestAndSet(
    TkTextTagSet *tagInfoPtr,
    const TkTextTag *tagPtr)
{
    unsigned tagIndex = tagPtr->index;

#if !TK_TEXT_DONT_USE_BITFIELDS
    if (tagPtr->index >= TkTextTagSetSize(tagInfoPtr)) {
	tagInfoPtr = TkTextTagSetResize(tagInfoPtr, tagPtr->sharedTextPtr->tagInfoSize);
	return TkTextTagSetAdd(tagInfoPtr, tagIndex);
    }
#endif /* !TK_TEXT_DONT_USE_BITFIELDS */

    return TkTextTagSetTestAndSet(tagInfoPtr, tagIndex);
}

static bool
LineTestAllSegments(
    const TkTextLine *linePtr,
    const TkTextTag *tagPtr,
    bool tagged)
{
    unsigned tagIndex = tagPtr->index;

    return TkTextTagSetTest(linePtr->tagonPtr, tagIndex) == tagged
	    && (!tagged || !TkTextTagSetTest(linePtr->tagoffPtr, tagIndex));
}

static bool
LineTestIfAnyIsTagged(
    TkTextSegment *firstPtr,
    TkTextSegment *lastPtr,
    unsigned tagIndex)
{
    assert(firstPtr || !lastPtr);

    for ( ; firstPtr != lastPtr; firstPtr = firstPtr->nextPtr) {
	if (firstPtr->tagInfoPtr && TkTextTagSetTest(firstPtr->tagInfoPtr, tagIndex)) {
	    return true;
	}
    }

    return false;
}

static bool
LineTestIfAnyIsUntagged(
    TkTextSegment *firstSegPtr,
    TkTextSegment *lastSegPtr,
    unsigned tagIndex)
{
    assert(firstSegPtr);

    for ( ; firstSegPtr != lastSegPtr; firstSegPtr = firstSegPtr->nextPtr) {
	if (firstSegPtr->tagInfoPtr) {
	    if (!TkTextTagSetTest(firstSegPtr->tagInfoPtr, tagIndex)) {
		return true;
	    }
	}
    }

    return false;
}

static bool
LineTestIfToggleIsOpen(
    const TkTextLine *linePtr,	/* can be NULL */
    unsigned tagIndex)
{
    return linePtr && TkTextTagSetTest(linePtr->lastPtr->tagInfoPtr, tagIndex);
}

static bool
LineTestIfToggleIsClosed(
    const TkTextLine *linePtr,	/* can be NULL */
    unsigned tagIndex)
{
    return !linePtr || !TkTextTagSetTest(GetFirstTagInfoSegment(NULL, linePtr)->tagInfoPtr, tagIndex);
}

static bool
LineTestToggleFwd(
    const TkTextLine *linePtr,
    unsigned tagIndex,
    bool testTagon)
{
    assert(linePtr);

    /*
     * testTagon == true: Test whether given tag is starting a range inside this line.
     * In this case this function assumes that this tag is not open at end of previous line.
     */

    if (testTagon) {
	return TkTextTagSetTest(linePtr->tagonPtr, tagIndex);
    }

    /*
     * testTagon == false: Test whether given tag is ending a range inside this line.
     * In this case this function assumes that this tag is open at end of previous line.
     */

    return TkTextTagSetTest(linePtr->tagoffPtr, tagIndex)
	    || !TkTextTagSetTest(linePtr->tagonPtr, tagIndex);
}

static bool
LineTestToggleBack(
    const TkTextLine *linePtr,
    unsigned tagIndex,
    bool testTagon)
{
    assert(linePtr);

    /*
     * testTagon == true: Test whether given tag is starting a range inside this line.
     * In this case this function assumes that this tag is already open at start of next line.
     */

    if (testTagon) {
	return TkTextTagSetTest(linePtr->tagonPtr, tagIndex)
	    && (TkTextTagSetTest(linePtr->tagoffPtr, tagIndex)
		|| !LineTestIfToggleIsOpen(linePtr->prevPtr, tagIndex));
    }

    /*
     * testTagon == false: Test whether given tag is ending a range inside this line.
     * In this case this function assumes that this tag is not open at start of next line.
     */

    return TkTextTagSetTest(linePtr->tagoffPtr, tagIndex)
	    || LineTestIfToggleIsOpen(linePtr->prevPtr, tagIndex)
	    || TkTextTagSetTest(GetFirstTagInfoSegment(NULL, linePtr)->tagInfoPtr, tagIndex);
}

static bool
NodeTestAnySegment(
    const Node *nodePtr,
    unsigned tagIndex,
    bool tagged)
{
    /*
     * tagged == true:  test whether any segments is tagged with specified tag.
     * tagged == false: test whether any segments is not tagged with specified tag.
     */

    return TkTextTagSetTest(nodePtr->tagonPtr, tagIndex) == tagged
	    && (tagged || TkTextTagSetTest(nodePtr->tagoffPtr, tagIndex));
}

static bool
NodeTestAllSegments(
    const Node *nodePtr,
    unsigned tagIndex,
    bool tagged)
{
    return TkTextTagSetTest(nodePtr->tagonPtr, tagIndex) == tagged
	    && (!tagged || !TkTextTagSetTest(nodePtr->tagoffPtr, tagIndex));
}

static bool
NodeTestToggleFwd(
    const Node *nodePtr,
    unsigned tagIndex,
    bool testTagon)
{
    assert(nodePtr);

    /*
     * testTagon == true: Test whether given tag is starting a range inside this node.
     * In this case this function assumes that this tag is not open at end of previous line
     * (line before first line of this node).
     */

    if (testTagon) {
	return TkTextTagSetTest(nodePtr->tagonPtr, tagIndex);
    }

    /*
     * testTagon == false: Test whether given tag is ending a range inside this node.
     * In this case this function assumes that this tag is open at end of previous line
     * (line before first line of this node).
     */

    return TkTextTagSetTest(nodePtr->tagoffPtr, tagIndex)
	    || !TkTextTagSetTest(nodePtr->tagonPtr, tagIndex);
}

static bool
NodeTestToggleBack(
    const Node *nodePtr,
    unsigned tagIndex,
    bool testTagon)
{
    assert(nodePtr);

    /*
     * testTagon == true: Test whether given tag is starting a range inside this node.
     * In this case this function assumes that this tag is already open at start of next line
     * (line after last line of this node).
     */

    if (testTagon) {
	return TkTextTagSetTest(nodePtr->tagonPtr, tagIndex)
	    && (TkTextTagSetTest(nodePtr->tagoffPtr, tagIndex)
		|| !LineTestIfToggleIsOpen(nodePtr->linePtr->prevPtr, tagIndex));
    }

    /*
     * testTagon == false: Test whether given tag is ending a range inside this node.
     * In this case this function assumes that this tag is not already open at start of next line
     * (line after last line of this node).
     */

    return TkTextTagSetTest(nodePtr->tagoffPtr, tagIndex)
	|| LineTestIfToggleIsOpen(nodePtr->linePtr->prevPtr, tagIndex);
}

static void
RecomputeLineTagInfo(
    TkTextLine *linePtr,
    const TkTextSegment *lastSegPtr,
    const TkSharedText *sharedTextPtr)
{
    const TkTextSegment *segPtr;
    TkTextTagSet *tagonPtr = NULL;
    TkTextTagSet *tagoffPtr = NULL;

    assert(linePtr);
    assert(!lastSegPtr || lastSegPtr->sectionPtr->linePtr == linePtr);

    /*
     * Update the line tag information after inserting tagged characters.
     * This function is not updating the tag information of the B-Tree.
     */

    for (segPtr = linePtr->segPtr; segPtr != lastSegPtr; segPtr = segPtr->nextPtr) {
	if (segPtr->tagInfoPtr) {
	    tagonPtr = TagSetJoin(tagonPtr, segPtr->tagInfoPtr);
	    tagoffPtr = TagSetIntersect(tagoffPtr, segPtr->tagInfoPtr, sharedTextPtr);
	}
    }

    if (!tagonPtr) {
	TkTextTagSetIncrRefCount(tagonPtr = sharedTextPtr->emptyTagInfoPtr);
	TkTextTagSetIncrRefCount(tagoffPtr = sharedTextPtr->emptyTagInfoPtr);
    } else {
	tagoffPtr = TagSetComplementTo(tagoffPtr, tagonPtr, sharedTextPtr);
    }

    TagSetReplace(&linePtr->tagonPtr, tagonPtr);
    TagSetReplace(&linePtr->tagoffPtr, tagoffPtr);
}

static unsigned
GetDisplayLines(
    const TkTextLine *linePtr,
    unsigned ref)
{
    return TkBTreeGetNumberOfDisplayLines(linePtr->pixelInfo + ref);
}

static void
SetLineHasChanged(
    const TkSharedText *sharedTextPtr,
    TkTextLine *linePtr)
{
    if (!linePtr->logicalLine) {
	 linePtr = TkBTreeGetLogicalLine(sharedTextPtr, NULL, linePtr);
    }
    linePtr->changed = true;
}

/*
 * Some helpers for segment creation and testing.
 */

static TkTextSegment *
MakeSegment(
    unsigned segByteSize,
    unsigned contentSize,
    const Tk_SegType *segType)
{
    TkTextSegment *segPtr;

    assert(segType != &tkTextCharType);

    segPtr = memset(malloc(segByteSize), 0, segByteSize);
    segPtr->typePtr = segType;
    segPtr->size = contentSize;
    segPtr->refCount = 1;
    DEBUG_ALLOC(tkTextCountNewSegment++);
    return segPtr;
}

static TkTextSegment * MakeBranch() { return MakeSegment(SEG_SIZE(TkTextBranch), 0, &tkTextBranchType); }
static TkTextSegment * MakeLink()   { return MakeSegment(SEG_SIZE(TkTextLink),   0, &tkTextLinkType);   }
static TkTextSegment * MakeHyphen() { return MakeSegment(SEG_SIZE(TkTextHyphen), 1, &tkTextHyphenType); }

static bool
IsBranchSection(
    const TkTextSection *sectionPtr)
{
    assert(sectionPtr);
    return sectionPtr->nextPtr && sectionPtr->nextPtr->segPtr->prevPtr->typePtr == &tkTextBranchType;
}

static bool
IsLinkSection(
    const TkTextSection *sectionPtr)
{
    assert(sectionPtr);
    return sectionPtr->segPtr->typePtr == &tkTextLinkType;
}

/*
 * Some functions for the undo/redo mechanism.
 */

static void
SetNodeLastPointer(
    Node *nodePtr,
    TkTextLine *linePtr)
{
    nodePtr->lastPtr = linePtr;
    while (!nodePtr->nextPtr && (nodePtr = nodePtr->parentPtr)) {
	nodePtr->lastPtr = linePtr;
    }
}

static Tcl_Obj *
MakeTagInfoObj(
    const TkSharedText *sharedTextPtr,
    const TkTextTagSet *tagInfoPtr)
{
    Tcl_Obj *objPtr = Tcl_NewObj();
    TkTextTag **tagLookup = sharedTextPtr->tagLookup;
    unsigned i = TkTextTagSetFindFirst(tagInfoPtr);

    for ( ; i != TK_TEXT_TAG_SET_NPOS; i = TkTextTagSetFindNext(tagInfoPtr, i)) {
	const TkTextTag *tagPtr = tagLookup[i];
	Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(tagPtr->name, -1));
    }

    return objPtr;
}

static void
UndoGetRange(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item,
    TkTextIndex *startIndex,
    TkTextIndex *endIndex)
{
    const TkTextUndoTokenRange *token = (const TkTextUndoTokenRange *) item;
    TkBTreeUndoIndexToIndex(sharedTextPtr, &token->startIndex, startIndex);
    TkBTreeUndoIndexToIndex(sharedTextPtr, &token->endIndex, endIndex);
}

/* DELETE ********************************************************************/

static Tcl_Obj *
UndoDeleteGetCommand(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item)
{
    Tcl_Obj *objPtr = Tcl_NewObj();
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj("delete", -1));
    return objPtr;
}

static Tcl_Obj *
UndoDeleteInspect(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item)
{
    Tcl_Obj *objPtr = UndoDeleteGetCommand(sharedTextPtr, item);
    TkTextSegment **segments = ((const UndoTokenDelete *) item)->segments;
    unsigned numSegments = ((const UndoTokenDelete *) item)->numSegments;
    const TkTextSegment *segPtr;

    for (segPtr = *segments++; numSegments > 0; segPtr = *segments++, --numSegments) {
	assert(segPtr->typePtr->inspectProc);
	Tcl_ListObjAppendElement(NULL, objPtr, segPtr->typePtr->inspectProc(sharedTextPtr, segPtr));
    }

    return objPtr;
}

static void
UndoDeletePerform(
    TkSharedText *sharedTextPtr,
    TkTextUndoInfo *undoInfo,
    TkTextUndoInfo *redoInfo,
    bool isRedo)
{
    TkTextLine *linePtr, *startLinePtr, *newLinePtr;
    TkTextSegment *segPtr, *prevPtr, *nextPtr;
    TkTextSegment *firstPtr, *lastPtr;
    TkTextSegment *prevSegPtr;
    Node *nodePtr;
    NodePixelInfo *changeToPixelInfo;
    BTree *treePtr = (BTree *) sharedTextPtr->tree;
    UndoTokenDelete *undoToken = (UndoTokenDelete *) undoInfo->token;
    TkTextSegment * const *segments = undoToken->segments;
    TkTextTagSet *tagonPtr;
    TkTextTagSet *tagoffPtr;
    TkTextTagSet *additionalTagoffPtr;
    unsigned numSegments = undoToken->numSegments - 1;
    unsigned changeToLineCount = 0;
    unsigned changeToLogicalLineCount = 0;
    unsigned changeToBranchCount = 0;
    unsigned size = 0;
    bool reinsertFirstSegment = true;
    unsigned i;

    assert(segments);
    assert(segments[0]);

    changeToPixelInfo = treePtr->pixelInfoBuffer;
    memset(changeToPixelInfo, 0, sizeof(changeToPixelInfo[0])*treePtr->numPixelReferences);
    prevPtr = lastPtr = NULL;

    if (undoToken->startIndex.lineIndex == -1) {
	prevPtr = undoToken->startIndex.u.markPtr;
	linePtr = prevPtr->sectionPtr->linePtr;
	reinsertFirstSegment = false;
    } else {
	linePtr = TkBTreeFindLine(sharedTextPtr->tree, NULL, undoToken->startIndex.lineIndex);
    }

    startLinePtr = linePtr;
    nodePtr = startLinePtr->parentPtr;
    firstPtr = segPtr = *segments++;
    firstPtr->protectionFlag = true;
    prevSegPtr = NULL;

    if (numSegments > 0) {
	nextPtr = *segments++;
	numSegments -= 1;
    } else {
	nextPtr = NULL;
    }

    TkTextTagSetIncrRefCount(tagonPtr = sharedTextPtr->emptyTagInfoPtr);
    TkTextTagSetIncrRefCount(tagoffPtr = sharedTextPtr->emptyTagInfoPtr);
    additionalTagoffPtr = NULL;

    while (segPtr) {
	if (POINTER_IS_MARKED(segPtr)) {
	    TkTextSection *sectionPtr;

	    UNMARK_POINTER(segPtr);

	    assert(segPtr->typePtr != &tkTextCharType);
	    assert(segPtr->sectionPtr);

	    /*
	     * This is a re-inserted segment, it will move.
	     */

	    sectionPtr = segPtr->sectionPtr;
	    UNMARK_POINTER(segPtr);
	    UnlinkSegment(segPtr);
	    JoinSections(sectionPtr);
	} else {
	    size += segPtr->size;
	}
	lastPtr = segPtr;
	DEBUG(segPtr->sectionPtr = NULL);
	if (reinsertFirstSegment) {
	    ReInsertSegment(sharedTextPtr, &undoToken->startIndex, segPtr, false);
	    reinsertFirstSegment = false;
	} else {
	    LinkSegment(linePtr, prevPtr, segPtr);
	}
	if (segPtr->typePtr == &tkTextCharType) {
	    assert(!segPtr->typePtr->restoreProc);

	    if (prevSegPtr) {
		if ((prevSegPtr = CleanupCharSegments(sharedTextPtr, prevSegPtr))->nextPtr != segPtr) {
		    segPtr = prevSegPtr;
		    lastPtr = lastPtr->nextPtr;
		}
	    }

	    if (segPtr->body.chars[segPtr->size - 1] == '\n') {
		newLinePtr = InsertNewLine(sharedTextPtr, linePtr->parentPtr, linePtr, segPtr->nextPtr);
		AddPixelCount(treePtr, newLinePtr, linePtr, changeToPixelInfo);
		changeToLineCount += 1;
		changeToLogicalLineCount += linePtr->logicalLine;
		RecomputeLineTagInfo(linePtr, NULL, sharedTextPtr);
		tagonPtr = TkTextTagSetJoin(tagonPtr, linePtr->tagonPtr);
		tagoffPtr = TkTextTagSetJoin(tagoffPtr, linePtr->tagoffPtr);
		additionalTagoffPtr = TagSetIntersect(additionalTagoffPtr,
			linePtr->tagonPtr, sharedTextPtr);
		linePtr = newLinePtr;
		segPtr = NULL;
	    }

	    prevSegPtr = segPtr;
	} else {
	    if (segPtr->typePtr->restoreProc) {
		if (segPtr->typePtr == &tkTextBranchType) {
		    changeToBranchCount += 1;
		}
		segPtr->typePtr->restoreProc(segPtr);
	    }
	    prevSegPtr = NULL;
	}
	prevPtr = segPtr;
	if ((segPtr = nextPtr)) {
	    if (numSegments > 0) {
		nextPtr = *segments++;
		numSegments -= 1;
	    } else {
		nextPtr = NULL;
	    }
	}
    }

    RecomputeLineTagInfo(linePtr, NULL, sharedTextPtr);
    tagonPtr = TkTextTagSetJoin(tagonPtr, linePtr->tagonPtr);
    tagoffPtr = TkTextTagSetJoin(tagoffPtr, linePtr->tagoffPtr);
    additionalTagoffPtr = TagSetIntersect(additionalTagoffPtr, linePtr->tagonPtr, sharedTextPtr);
    tagoffPtr = TagSetJoinComplementTo(tagoffPtr, additionalTagoffPtr, tagonPtr, sharedTextPtr);
    tagoffPtr = TkTextTagSetRemove(tagoffPtr, nodePtr->tagoffPtr);
    tagonPtr = TkTextTagSetRemove(tagonPtr, nodePtr->tagonPtr);
    tagonPtr = TkTextTagSetRemove(tagonPtr, tagoffPtr);

    /*
     * Update the B-Tree tag information.
     */

    for (i = TkTextTagSetFindFirst(tagoffPtr);
	    i != TK_TEXT_TAG_SET_NPOS;
	    i = TkTextTagSetFindNext(tagoffPtr, i)) {
	if (!TkTextTagSetTest(nodePtr->tagoffPtr, i)) {
	    AddTagToNode(nodePtr, sharedTextPtr->tagLookup[i], true);
	}
    }

    for (i = TkTextTagSetFindFirst(tagonPtr);
	    i != TK_TEXT_TAG_SET_NPOS;
	    i = TkTextTagSetFindNext(tagonPtr, i)) {
	AddTagToNode(nodePtr, sharedTextPtr->tagLookup[i], false);
    }

    TkTextTagSetDecrRefCount(tagonPtr);
    TkTextTagSetDecrRefCount(tagoffPtr);
    TkTextTagSetDecrRefCount(additionalTagoffPtr);

    /*
     * Rebuild sections, and increase the epoch.
     */

    RebuildSections(sharedTextPtr, linePtr, true);
    TkBTreeIncrEpoch(sharedTextPtr->tree);

    /*
     * Cleanup char segments.
     */

    CleanupSplitPoint(firstPtr, sharedTextPtr);
    if (firstPtr != lastPtr) {
	CleanupSplitPoint(lastPtr, sharedTextPtr);
    }

    /*
     * Prevent that the destroy function will delete these segments.
     * This also makes the token reusable.
     */

    free(undoToken->segments);
    undoToken->segments = NULL;
    undoToken->numSegments = 0;

    /*
     * Update the redo information.
     */

    if (redoInfo) {
	undoToken->undoType = &redoTokenDeleteType;
	redoInfo->token = (TkTextUndoToken *) undoToken;
	redoInfo->byteSize = 0;
    }

    /*
     * Increment the line and pixel counts in all the parent nodes of the
     * insertion point, then rebalance the tree if necessary.
     */

	/* MSVC cannot implicitly convert unsigned to signed. */
    SubtractPixelCount2(treePtr, nodePtr, -((int) changeToLineCount),
	    -((int) changeToLogicalLineCount), -((int) changeToBranchCount),
	    -((int) size), changeToPixelInfo);
    linePtr->parentPtr->numChildren += changeToLineCount;

    if (nodePtr->numChildren > MAX_CHILDREN) {
	Rebalance(treePtr, nodePtr);
    }

    /*
     * This line now needs to have its height recalculated. This has to be done after Rebalance.
     */

    TkTextInvalidateLineMetrics(sharedTextPtr, NULL,
	    startLinePtr, changeToLineCount, TK_TEXT_INVALIDATE_INSERT);

    TK_BTREE_DEBUG(TkBTreeCheck((TkTextBTree) treePtr));
}

static void
UndoDeleteDestroy(
    TkSharedText *sharedTextPtr,
    TkTextUndoToken *token,
    bool reused)
{
    TkTextSegment **segments = ((UndoTokenDelete *) token)->segments;
    unsigned numSegments = ((UndoTokenDelete *) token)->numSegments;

    assert(!reused);

    if (numSegments > 0) {
	TkTextSegment *segPtr;

	for (segPtr = *segments++; numSegments > 0; segPtr = *segments++, --numSegments) {
	    UNMARK_POINTER(segPtr);
	    assert(segPtr->typePtr);
	    assert(segPtr->typePtr->deleteProc);
	    segPtr->typePtr->deleteProc(sharedTextPtr->tree, segPtr, DELETE_BRANCHES | DELETE_MARKS);
	}

	free(((UndoTokenDelete *) token)->segments);
    }
}

static Tcl_Obj *
RedoDeleteInspect(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item)
{
    Tcl_Obj *objPtr = UndoDeleteGetCommand(sharedTextPtr, item);

#if 0 /* not possible to inspect the range, because this range may be deleted */
    TkTextIndex startIndex, endIndex;
    char buf[TK_POS_CHARS];

    UndoGetRange(sharedTextPtr, item, &startIndex, &endIndex);
    TkTextIndexPrint(sharedTextPtr, NULL, &startIndex, buf);
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(buf, -1));
    TkTextIndexPrint(sharedTextPtr, NULL, &endIndex, buf);
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(buf, -1));
#endif

    return objPtr;
}

static void
RedoDeletePerform(
    TkSharedText *sharedTextPtr,
    TkTextUndoInfo *undoInfo,
    TkTextUndoInfo *redoInfo,
    bool isRedo)
{
    const UndoTokenDelete *token = (const UndoTokenDelete *) undoInfo->token;

    if (token->startIndex.lineIndex == -1 && token->endIndex.lineIndex == -1) {
	TkTextSegment *segPtr1 = token->startIndex.u.markPtr;
	TkTextSegment *segPtr2 = token->endIndex.u.markPtr;
	int flags = token->inclusive ? DELETE_INCLUSIVE : 0;

	DeleteRange(sharedTextPtr, segPtr1, segPtr2, flags, redoInfo);

	assert(segPtr1 != segPtr2);
	segPtr1->protectionFlag = true;
	segPtr2->protectionFlag = true;
	CleanupSplitPoint(segPtr1, sharedTextPtr);
	CleanupSplitPoint(segPtr2, sharedTextPtr);
	TkBTreeIncrEpoch(sharedTextPtr->tree);

	TK_BTREE_DEBUG(TkBTreeCheck(sharedTextPtr->tree));
    } else {
	TkTextIndex index1, index2;

	TkBTreeUndoIndexToIndex(sharedTextPtr, &token->startIndex, &index1);
	TkBTreeUndoIndexToIndex(sharedTextPtr, &token->endIndex, &index2);
	DeleteIndexRange(sharedTextPtr, &index1, &index2, 0, (UndoTokenInsert *) token, redoInfo);
    }
}

/* INSERT ********************************************************************/

static Tcl_Obj *
UndoInsertGetCommand(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item)
{
    Tcl_Obj *objPtr = Tcl_NewObj();
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj("insert", -1));
    return objPtr;
}

static void
UndoInsertPerform(
    TkSharedText *sharedTextPtr,
    TkTextUndoInfo *undoInfo,
    TkTextUndoInfo *redoInfo,
    bool isRedo)
{
    struct UndoTokenInsert *token = (UndoTokenInsert *) undoInfo->token;
    TkTextIndex index1, index2;

    TkBTreeUndoIndexToIndex(sharedTextPtr, &token->startIndex, &index1);
    TkBTreeUndoIndexToIndex(sharedTextPtr, &token->endIndex, &index2);
    DeleteIndexRange(sharedTextPtr, &index1, &index2, 0, token, redoInfo);
    if (redoInfo && redoInfo->token) {
	redoInfo->token->undoType = &redoTokenInsertType;
    }
}

static Tcl_Obj *
RedoInsertInspect(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item)
{
    Tcl_Obj *objPtr = Tcl_NewObj();
    TkTextSegment **segments = ((const UndoTokenDelete *) item)->segments;
    unsigned numSegments = ((const UndoTokenDelete *) item)->numSegments;
    const TkTextSegment *segPtr;

    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj("insert", -1));

    for (segPtr = *segments++; numSegments > 0; segPtr = *segments++, --numSegments) {
	UNMARK_POINTER(segPtr);
	assert(segPtr->typePtr->inspectProc);
	Tcl_ListObjAppendElement(NULL, objPtr, segPtr->typePtr->inspectProc(sharedTextPtr, segPtr));
    }

    return objPtr;
}

/* TAG ADD/REMOVE ************************************************************/

static Tcl_Obj *
UndoTagGetCommand(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item)
{
    const UndoTokenTagChange *token = (const UndoTokenTagChange *) item;
    bool isRedo = (item->undoType == &redoTokenTagType);
    bool add = (isRedo == POINTER_IS_MARKED(token->tagPtr));
    Tcl_Obj *objPtr = Tcl_NewObj();

    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj("tag", -1));
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(add ? "add" : "remove", -1));
    return objPtr;
}

Tcl_Obj *
TkBTreeUndoTagInspect(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item)
{
    const UndoTokenTagChange *token = (const UndoTokenTagChange *) item;
    Tcl_Obj *objPtr = UndoTagGetCommand(sharedTextPtr, item);
    TkTextTag *tagPtr = token->tagPtr;

    UNMARK_POINTER(tagPtr);
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(tagPtr->name, -1));
    return objPtr;
}

static void
UndoTagPerform(
    TkSharedText *sharedTextPtr,
    TkTextUndoInfo *undoInfo,
    TkTextUndoInfo *redoInfo,
    bool isRedo)
{
    UndoTokenTagChange *token = (UndoTokenTagChange *) undoInfo->token;
    TkTextTag *tagPtr = token->tagPtr;
    bool remove = POINTER_IS_MARKED(tagPtr);
    bool add = (isRedo != remove);
    TkTextIndex index1, index2;

    UNMARK_POINTER(tagPtr);
    TkTextEnableTag(sharedTextPtr, tagPtr);
    TkBTreeUndoIndexToIndex(sharedTextPtr, &token->startIndex, &index1);
    TkBTreeUndoIndexToIndex(sharedTextPtr, &token->endIndex, &index2);

    if (token->lengths) {
	TkTextIndex nextIndex = index1;
	const int32_t *len;

	for (len = token->lengths; *len; ++len) {
	    int length = *len;

	    TkTextIndexForwBytes(NULL, &nextIndex, ABS(length), &nextIndex);

	    if (length > 0) {
		TkBTreeTag(sharedTextPtr, NULL, &index1, &nextIndex, tagPtr, add, NULL,
			TkTextTagChangedUndoRedo);
	    }

	    index1 = nextIndex;
	}

	TkBTreeTag(sharedTextPtr, NULL, &index1, &index2, tagPtr, add, NULL, TkTextTagChangedUndoRedo);
    } else {
	TkBTreeTag(sharedTextPtr, NULL, &index1, &index2, tagPtr, add, NULL, TkTextTagChangedUndoRedo);
    }

    if (redoInfo) {
	redoInfo->token = undoInfo->token;
	redoInfo->token->undoType = isRedo? &undoTokenTagType : &redoTokenTagType;
    }
}

static void
UndoTagDestroy(
    TkSharedText *sharedTextPtr,
    TkTextUndoToken *item,
    bool reused)
{
    if (!reused) {
	UndoTokenTagChange *token = (UndoTokenTagChange *) item;

	UNMARK_POINTER(token->tagPtr);
	TkTextReleaseTag(sharedTextPtr, token->tagPtr, NULL);
	free(token->lengths);
	token->lengths = NULL;
    }
}

/* TAG CLEAR *****************************************************************/

static Tcl_Obj *
UndoClearTagsGetCommand(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item)
{
    Tcl_Obj *objPtr = Tcl_NewObj();

    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj("tag", -1));
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj("clear", -1));
    return objPtr;
}

static Tcl_Obj *
UndoClearTagsInspect(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoToken *item)
{
    const UndoTokenTagClear *token = (const UndoTokenTagClear *) item;
    Tcl_Obj *objPtr = UndoClearTagsGetCommand(sharedTextPtr, item);
    Tcl_Obj *objPtr2 = Tcl_NewObj();
    unsigned i;

    for (i = 0; i < token->changeListSize; ++i) {
	const UndoTagChange *change = token->changeList + i;

	Tcl_ListObjAppendElement(NULL, objPtr2, MakeTagInfoObj(sharedTextPtr, change->tagInfoPtr));
	Tcl_ListObjAppendElement(NULL, objPtr2, Tcl_NewIntObj(change->skip));
	Tcl_ListObjAppendElement(NULL, objPtr2, Tcl_NewIntObj(change->size));
    }

    Tcl_ListObjAppendElement(NULL, objPtr, objPtr2);
    return objPtr;
}

static void
UndoClearTagsPerform(
    TkSharedText *sharedTextPtr,
    TkTextUndoInfo *undoInfo,
    TkTextUndoInfo *redoInfo,
    bool isRedo)
{
    UndoTokenTagClear *token = (UndoTokenTagClear *) undoInfo->token;
    const UndoTagChange *entry = token->changeList;
    TkTextSegment *firstSegPtr = NULL, *lastSegPtr = NULL;
    TkTextIndex startIndex, endIndex;
    unsigned n = token->changeListSize;
    bool anyChanges = false;
    bool affectsDisplayGeometry = false;
    bool updateElideInfo = false;
    TkTextSegment *segPtr;
    TkTextLine *linePtr;
    Node *nodePtr;
    int offs = 0;
    unsigned i;

    assert(token->changeListSize > 0);

    TkBTreeUndoIndexToIndex(sharedTextPtr, &token->startIndex, &startIndex);
    TkBTreeUndoIndexToIndex(sharedTextPtr, &token->endIndex, &endIndex);

    linePtr = TkTextIndexGetLine(&startIndex);
    segPtr = linePtr->segPtr;
    nodePtr = linePtr->parentPtr;

    for (i = 0; i < n; ++i, ++entry) {
	int skip = entry->skip;
	int size = entry->size;

	while (size > 0) {
	    while (linePtr->size - offs <= skip) {
		assert(linePtr->nextPtr);
		skip -= linePtr->size - offs;
		if (anyChanges) {
		    RecomputeLineTagInfo(linePtr, NULL, sharedTextPtr);
		    if (nodePtr != linePtr->nextPtr->parentPtr) {
			UpdateNodeTags(sharedTextPtr, nodePtr);
			nodePtr = linePtr->nextPtr->parentPtr;
		    }
		    anyChanges = false;
		}
		linePtr = linePtr->nextPtr;
		segPtr = linePtr->segPtr;
		offs = 0;
	    }
	    if (segPtr == segPtr->sectionPtr->segPtr) {
		TkTextSection *sectionPtr = segPtr->sectionPtr;

		while (sectionPtr->size <= skip) {
		    skip -= sectionPtr->size;
		    offs += sectionPtr->size;
		    if (!(sectionPtr = sectionPtr->nextPtr)) {
			if (anyChanges) {
			    RecomputeLineTagInfo(linePtr, NULL, sharedTextPtr);
			    if (nodePtr != linePtr->nextPtr->parentPtr) {
				UpdateNodeTags(sharedTextPtr, nodePtr);
				nodePtr = linePtr->nextPtr->parentPtr;
			    }
			    anyChanges = false;
			}
			linePtr = linePtr->nextPtr;
			assert(linePtr);
			sectionPtr = linePtr->segPtr->sectionPtr;
			offs = 0;
		    }
		    segPtr = sectionPtr->segPtr;
		}
	    }
	    while (segPtr->size <= skip) {
		skip -= segPtr->size;
		offs += segPtr->size;
		if (!(segPtr = segPtr->nextPtr)) {
		    if (anyChanges) {
			RecomputeLineTagInfo(linePtr, NULL, sharedTextPtr);
			if (nodePtr != linePtr->nextPtr->parentPtr) {
			    UpdateNodeTags(sharedTextPtr, nodePtr);
			    nodePtr = linePtr->nextPtr->parentPtr;
			}
			anyChanges = false;
		    }
		    linePtr = linePtr->nextPtr;
		    assert(linePtr);
		    segPtr = linePtr->segPtr;
		    offs = 0;
		}
	    }
	    while (size > 0 && segPtr) {
		while (segPtr->size == 0) {
		    segPtr = segPtr->nextPtr;
		}
		if (size != segPtr->size) {
		    if (skip > 0) {
			assert(skip < segPtr->size);
			offs += skip;
			segPtr = SplitCharSegment(segPtr, skip)->nextPtr;
		    }
		    if (size < segPtr->size) {
			segPtr = SplitCharSegment(segPtr, size);
		    }
		}
		assert(segPtr->size <= size);
		size -= segPtr->size;
		offs += segPtr->size;
		if (TkTextTagSetIntersectsBits(entry->tagInfoPtr, sharedTextPtr->affectGeometryTags)) {
		    affectsDisplayGeometry = true;
		}
		if (TkTextTagSetIntersectsBits(entry->tagInfoPtr, sharedTextPtr->elisionTags)) {
		    updateElideInfo = true;
		}
		TkTextTagSetDecrRefCount(segPtr->tagInfoPtr);
		TkTextTagSetIncrRefCount(segPtr->tagInfoPtr = entry->tagInfoPtr);
		if (!firstSegPtr) { firstSegPtr = segPtr; }
		lastSegPtr = segPtr;
		segPtr = segPtr->nextPtr;
		anyChanges = true;
		skip = 0;
	    }
	}
    }

    RecomputeLineTagInfo(linePtr, NULL, sharedTextPtr);
    UpdateNodeTags(sharedTextPtr, linePtr->parentPtr);
    if (updateElideInfo) {
	UpdateElideInfo(sharedTextPtr, NULL, &firstSegPtr, &lastSegPtr, ELISION_HAS_BEEN_CHANGED);
    }
    firstSegPtr->protectionFlag = true;
    lastSegPtr->protectionFlag = true;
    CleanupSplitPoint(firstSegPtr, sharedTextPtr);
    if (firstSegPtr != lastSegPtr) {
	CleanupSplitPoint(lastSegPtr, sharedTextPtr);
    }
    TkBTreeIncrEpoch(sharedTextPtr->tree);
    TkTextRedrawTag(sharedTextPtr, NULL, &startIndex, &endIndex, NULL, affectsDisplayGeometry);

    if (redoInfo) {
	RedoTokenClearTags *redoToken = malloc(sizeof(RedoTokenClearTags));
	redoToken->undoType = &redoTokenClearTagsType;
	redoToken->startIndex = token->startIndex;
	redoToken->endIndex = token->endIndex;
	redoInfo->token = (TkTextUndoToken *) redoToken;
	DEBUG_ALLOC(tkTextCountNewUndoToken++);
    }

    TK_BTREE_DEBUG(TkBTreeCheck(sharedTextPtr->tree));
}

static void
UndoClearTagsDestroy(
    TkSharedText *sharedTextPtr,
    TkTextUndoToken *token,
    bool reused)
{
    UndoTokenTagClear *myToken = (UndoTokenTagClear *) token;
    UndoTagChange *changeList = myToken->changeList;
    unsigned i, n = myToken->changeListSize;

    assert(!reused);

    for (i = 0; i < n; ++i) {
	TkTextTagSetDecrRefCount(changeList[i].tagInfoPtr);
    }

    free(changeList);
}

static void
RedoClearTagsPerform(
    TkSharedText *sharedTextPtr,
    TkTextUndoInfo *undoInfo,
    TkTextUndoInfo *redoInfo,
    bool isRedo)
{
    RedoTokenClearTags *token = (RedoTokenClearTags *) undoInfo->token;
    TkTextIndex index1, index2;

    TkBTreeUndoIndexToIndex(sharedTextPtr, &token->startIndex, &index1);
    TkBTreeUndoIndexToIndex(sharedTextPtr, &token->endIndex, &index2);
    TkBTreeClearTags(sharedTextPtr, NULL, &index1, &index2, redoInfo, true, TkTextTagChangedUndoRedo);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeCreate --
 *
 *	This function is called to create a new text B-tree.
 *
 * Results:
 *	The return value is a pointer to a new B-tree containing one line with
 *	nothing but a newline character.
 *
 * Side effects:
 *	Memory is allocated and initialized.
 *
 *----------------------------------------------------------------------
 */

TkTextBTree
TkBTreeCreate(
    TkSharedText *sharedTextPtr,
    unsigned epoch)
{
    BTree *treePtr;
    Node *rootPtr;
    TkTextLine *linePtr, *linePtr2;
    TkTextSegment *segPtr;

    /*
     * The tree will initially have two empty lines. The first line contains
     * the start marker, this marker will never move. The second line isn't
     * actually part of the tree's contents, but its presence makes several
     * operations easier. The second line contains the end marker. The tree
     * will have one node, which is also the root of the tree.
     *
     * The tree currently has no registered clients, so all pixel count
     * pointers are simply NULL.
     */

    rootPtr = memset(malloc(sizeof(Node)), 0, sizeof(Node));
    DEBUG_ALLOC(tkTextCountNewNode++);

    treePtr = memset(malloc(sizeof(BTree)), 0, sizeof(BTree));
    treePtr->rootPtr = rootPtr;
    treePtr->sharedTextPtr = sharedTextPtr;
    treePtr->stateEpoch = epoch;
    sharedTextPtr->tree = (TkTextBTree) treePtr;

    assert(!sharedTextPtr->startMarker->nextPtr);
    linePtr = InsertNewLine(sharedTextPtr, rootPtr, NULL, sharedTextPtr->startMarker);
    segPtr = MakeCharSeg(NULL, sharedTextPtr->emptyTagInfoPtr, 1, "\n", 1);
    LinkSegment(linePtr, sharedTextPtr->startMarker, segPtr);

    assert(!sharedTextPtr->endMarker->nextPtr);
    linePtr2 = InsertNewLine(sharedTextPtr, rootPtr, linePtr, sharedTextPtr->endMarker);
    segPtr = MakeCharSeg(NULL, sharedTextPtr->emptyTagInfoPtr, 1, "\n", 1);
    LinkSegment(linePtr2, sharedTextPtr->endMarker, segPtr);

    rootPtr->linePtr = linePtr;
    rootPtr->lastPtr = linePtr2;
    rootPtr->size = 2;
    rootPtr->numLines = 2;
    rootPtr->numLogicalLines = 2;
    rootPtr->numChildren = 2;
    TkTextTagSetIncrRefCount(rootPtr->tagonPtr = sharedTextPtr->emptyTagInfoPtr);
    TkTextTagSetIncrRefCount(rootPtr->tagoffPtr = sharedTextPtr->emptyTagInfoPtr);

    if (tkBTreeDebug) {
	sharedTextPtr->refCount += 1;
	TkBTreeCheck((TkTextBTree) treePtr);
	sharedTextPtr->refCount -= 1;
    }

    return (TkTextBTree) treePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * GetStartLine --
 *
 *	This function returns the first line for given text widget, if
 *	NULL it returns the first line of shared resource.
 *
 * Results:
 *	The first line in this widget (or shared resource).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkTextLine *
GetStartLine(
    const TkSharedText *sharedTextPtr,
    const TkText *textPtr)
{
    return textPtr ? TkBTreeGetStartLine(textPtr) : sharedTextPtr->startMarker->sectionPtr->linePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * GetLastLine --
 *
 *	This function returns the last line for given text widget.
 *
 * Results:
 *	The last line in this widget.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkTextLine *
GetLastLine(
    const TkSharedText *sharedTextPtr,
    const TkText *textPtr)
{
    TkTextLine *endLine;

    assert(sharedTextPtr || textPtr);

    if (!textPtr) {
	return sharedTextPtr->endMarker->sectionPtr->linePtr;
    }

    endLine = textPtr->endMarker->sectionPtr->linePtr;
    return endLine->nextPtr ? endLine->nextPtr : endLine;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeNumLines --
 *
 *	This function returns a count of the number of lines of text
 *	present in a given B-tree.
 *
 * Results:
 *	The return value is a count of the number of usable lines in tree
 *	(i.e. it doesn't include the dummy line that is just used to mark the
 *	end of the tree).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkBTreeNumLines(
    TkTextBTree tree,		/* Information about tree. */
    const TkText *textPtr)	/* Relative to this client of the B-tree. */
{
    int count;

    if (textPtr) {
	count = TkBTreeLinesTo(tree, NULL, TkBTreeGetLastLine(textPtr), NULL);
	count -= TkBTreeLinesTo(tree, NULL, TkBTreeGetStartLine(textPtr), NULL);
    } else {
	count = TkBTreeGetRoot(tree)->numLines - 1;
    }

    return count;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeAddClient --
 *
 *	This function is called to provide a client with access to a given
 *	B-tree. If the client wishes to make use of the B-tree's pixel height
 *	storage, caching and calculation mechanisms, then a non-negative
 *	'defaultHeight' must be provided. In this case the return value is a
 *	pixel tree reference which must be provided in all of the B-tree API
 *	which refers to or modifies pixel heights:
 *
 *	TkBTreeAdjustPixelHeight,
 *	TkBTreeFindPixelLine,
 *	TkBTreeNumPixels,
 *	TkBTreePixelsTo,
 *	(and two private functions AdjustPixelClient, RemovePixelClient).
 *
 *	If this is not provided, then the above functions must never be called
 *	for this client.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory may be allocated and initialized.
 *
 *----------------------------------------------------------------------
 */

void
TkBTreeAddClient(
    TkTextBTree tree,		/* B-tree to add a client to. */
    TkText *textPtr,		/* Client to add. */
    int defaultHeight)		/* Default line height for the new client, or
				 * -1 if no pixel heights are to be kept. */
{
    BTree *treePtr = (BTree *) tree;

    assert(treePtr);

    if (defaultHeight >= 0) {
	unsigned useReference = treePtr->numPixelReferences;

	AdjustPixelClient(treePtr, defaultHeight, treePtr->rootPtr, TkBTreeGetStartLine(textPtr),
		TkBTreeGetLastLine(textPtr), useReference, useReference + 1, NULL);

	textPtr->pixelReference = useReference;
	treePtr->numPixelReferences += 1;
	treePtr->pixelInfoBuffer = realloc(treePtr->pixelInfoBuffer,
		sizeof(treePtr->pixelInfoBuffer[0])*treePtr->numPixelReferences);
    } else {
	textPtr->pixelReference = -1;
    }

    treePtr->clients += 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeClientRangeChanged --
 *
 *	Called when the -startindex or -endindex options of a text widget client
 *	of the B-tree have changed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Lots of processing of the B-tree is done, with potential for memory to
 *	be allocated and initialized for the pixel heights of the widget.
 *
 *----------------------------------------------------------------------
 */

void
TkBTreeClientRangeChanged(
    TkText *textPtr,		/* Client whose start, end have changed. */
    unsigned defaultHeight)	/* Default line height for the new client, or
				 * zero if no pixel heights are to be kept. */
{
    BTree *treePtr = (BTree *) textPtr->sharedTextPtr->tree;
    TkTextLine *startLine = TkBTreeGetStartLine(textPtr);
    TkTextLine *endLine = TkBTreeGetLastLine(textPtr);

    AdjustPixelClient(treePtr, defaultHeight, treePtr->rootPtr, startLine,
	    endLine, textPtr->pixelReference, treePtr->numPixelReferences, NULL);

}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeRemoveClient --
 *
 *	Remove a client widget from its B-tree, cleaning up the pixel arrays
 *	which it uses if necessary. If this is the last such widget, we also
 *	destroy the whole tree.
 *
 * Results:
 *	All tree-specific aspects of the given client are deleted. If no more
 *	references exist, then the given tree is also deleted (in which case
 *	'tree' must not be used again).
 *
 * Side effects:
 *	Memory may be freed.
 *
 *----------------------------------------------------------------------
 */

void
TkBTreeRemoveClient(
    TkTextBTree tree,		/* Tree to remove client from. */
    TkText *textPtr)		/* Client to remove. */
{
    BTree *treePtr = (BTree *) tree;
    int pixelReference = textPtr->pixelReference;

    if (treePtr->clients == 1) {
	/*
	 * The last reference to the tree.
	 */

	DestroyNode(tree, treePtr->rootPtr);
	free(treePtr);
	return;
    }

    if (pixelReference == -1) {
	/*
	 * A client which doesn't care about pixels.
	 */

	treePtr->clients -= 1;
    } else {
	/*
	 * Clean up pixel data for the given reference.
	 */

	if (pixelReference == (int) (treePtr->numPixelReferences - 1)) {
	    /*
	     * The widget we're removing has the last index, so deletion is easier.
	     */

	    RemovePixelClient(treePtr, treePtr->rootPtr, pixelReference, -1);
	} else {
	    TkText *adjustPtr;

	    RemovePixelClient(treePtr, treePtr->rootPtr, pixelReference, pixelReference);

	    /*
	     * Now we need to adjust the 'pixelReference' of the peer widget
	     * whose storage we've just moved.
	     */

	    adjustPtr = treePtr->sharedTextPtr->peers;
	    while (adjustPtr) {
		if (adjustPtr->pixelReference == (int) treePtr->numPixelReferences - 1) {
		    adjustPtr->pixelReference = pixelReference;
		    break;
		}
		adjustPtr = adjustPtr->next;
	    }
	    assert(adjustPtr);
	}

	treePtr->numPixelReferences -= 1;
	treePtr->clients -= 1;
	treePtr->pixelInfoBuffer = realloc(treePtr->pixelInfoBuffer,
		sizeof(treePtr->pixelInfoBuffer[0])*treePtr->numPixelReferences);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * AdjustPixelClient --
 *
 *	Utility function used to update all data structures for the existence
 *	of a new peer widget based on this B-tree, or for the modification of
 *	the start, end lines of an existing peer widget.
 *
 *	Immediately _after_ calling this, treePtr->numPixelReferences and
 *	treePtr->clients should be adjusted if needed (i.e. if this is a new
 *	peer).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All the storage for Nodes and TkTextLines in the tree may be adjusted.
 *
 *----------------------------------------------------------------------
 */

static unsigned
AdjustPixelClient(
    BTree *treePtr,		/* Pointer to tree. */
    unsigned defaultHeight,	/* Default pixel line height, which can be zero. */
    Node *nodePtr,		/* Adjust from this node downwards. */
    TkTextLine *startLine,	/* First line for this pixel client. */
    TkTextLine *endLine,	/* Last line for this pixel client. */
    unsigned useReference,	/* Pixel reference for the client we are adding or changing. */
    unsigned newPixelReferences,/* New number of pixel references to this B-tree. */
    unsigned *numDispLinesPtr)	/* Number of display lines in this sub-tree, can be NULL. */
{
    unsigned pixelCount = 0;
    unsigned numDispLines = 0;

    assert(startLine);
    assert(endLine);
    assert(!nodePtr->parentPtr == !numDispLinesPtr);

    /*
     * Traverse entire tree down from nodePtr, reallocating pixel structures
     * for each Node and TkTextLine, adding room for the new peer's pixel
     * information (1 extra int per Node, 2 extra ints per TkTextLine). Also
     * copy the information from the last peer into the new space (so it
     * contains something sensible).
     */

    if (nodePtr->level > 0) {
	Node *loopPtr = nodePtr->childPtr;

	while (loopPtr) {
	    pixelCount += AdjustPixelClient(treePtr, defaultHeight, loopPtr,
		    startLine, endLine, useReference, newPixelReferences, &numDispLines);
	    loopPtr = loopPtr->nextPtr;
	}
    } else {
	TkTextLine *linePtr = nodePtr->linePtr;
	TkTextLine *lastPtr = nodePtr->lastPtr->nextPtr;
	unsigned height = 0;
	unsigned epoch = 1;

	for ( ; linePtr != lastPtr; linePtr = linePtr->nextPtr) {
	    if (linePtr == startLine) {
		height = defaultHeight;
		epoch = 0;
	    } else if (linePtr == endLine) {
		height = 0;
		epoch = 1;
	    }

	    /*
	     * Notice that for the very last line, we are never counting and
	     * therefore this always has a height of 0 and an epoch of 1.
	     */

	    if (newPixelReferences > treePtr->numPixelReferences) {
		DEBUG_ALLOC(if (!linePtr->pixelInfo) tkTextCountNewPixelInfo++);
		linePtr->pixelInfo = realloc(linePtr->pixelInfo,
			sizeof(linePtr->pixelInfo[0])*newPixelReferences);
		memset(&linePtr->pixelInfo[useReference], 0, sizeof(TkTextPixelInfo));
	    } else if (linePtr->pixelInfo[useReference].dispLineInfo) {
		free(linePtr->pixelInfo[useReference].dispLineInfo);
		linePtr->pixelInfo[useReference].dispLineInfo = NULL;
		DEBUG_ALLOC(tkTextCountDestroyDispInfo++);
	    }

	    linePtr->pixelInfo[useReference].epoch = epoch;
	    pixelCount += (linePtr->pixelInfo[useReference].height = height);
	    numDispLines += GetDisplayLines(linePtr, useReference);
	}
    }

    if (newPixelReferences > treePtr->numPixelReferences) {
	DEBUG_ALLOC(if (!nodePtr->pixelInfo) tkTextCountNewPixelInfo++);
	nodePtr->pixelInfo = realloc(nodePtr->pixelInfo,
		sizeof(nodePtr->pixelInfo[0])*newPixelReferences);
    }
    nodePtr->pixelInfo[useReference].pixels = pixelCount;
    nodePtr->pixelInfo[useReference].numDispLines = numDispLines;
    if (numDispLinesPtr) {
	*numDispLinesPtr += numDispLines;
    }
    return pixelCount;
}

/*
 *----------------------------------------------------------------------
 *
 * RemovePixelClient --
 *
 *	Utility function used to update all data structures for the removal of
 *	a peer widget which used to be based on this B-tree.
 *
 *	Immediately _after_ calling this, treePtr->clients should be
 *	decremented.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All the storage for Nodes and TkTextLines in the tree may be adjusted.
 *
 *----------------------------------------------------------------------
 */

static void
RemovePixelClient(
    BTree *treePtr,		/* Pointer to tree. */
    Node *nodePtr,		/* Adjust from this node downwards. */
    unsigned useReference,	/* Pixel reference for the client we are removing. */
    int overwriteWithLast)	/* Over-write this peer widget's information with the last one. */
{
    /*
     * Traverse entire tree down from nodePtr, reallocating pixel structures
     * for each Node and TkTextLine, removing space allocated for one peer. If
     * 'overwriteWithLast' is not -1, then copy the information which was in
     * the last slot on top of one of the others (i.e. it's not the last one
     * we're deleting).
     */

    if (overwriteWithLast != -1) {
	nodePtr->pixelInfo[overwriteWithLast] = nodePtr->pixelInfo[treePtr->numPixelReferences - 1];
    }
    if (treePtr->numPixelReferences == 1) {
	free(nodePtr->pixelInfo);
	nodePtr->pixelInfo = NULL;
	DEBUG_ALLOC(tkTextCountDestroyPixelInfo++);
    } else {
	nodePtr->pixelInfo = realloc(nodePtr->pixelInfo,
		sizeof(nodePtr->pixelInfo[0])*(treePtr->numPixelReferences - 1));
    }
    if (nodePtr->level != 0) {
	nodePtr = nodePtr->childPtr;
	while (nodePtr) {
	    RemovePixelClient(treePtr, nodePtr, useReference, overwriteWithLast);
	    nodePtr = nodePtr->nextPtr;
	}
    } else {
	TkTextLine *linePtr = nodePtr->linePtr;
	TkTextLine *lastPtr = nodePtr->lastPtr->nextPtr;

	while (linePtr != lastPtr) {
	    if (linePtr->pixelInfo[useReference].dispLineInfo) {
		free(linePtr->pixelInfo[useReference].dispLineInfo);
		DEBUG_ALLOC(tkTextCountDestroyDispInfo++);
	    }
	    if (overwriteWithLast != -1) {
		linePtr->pixelInfo[overwriteWithLast] =
			linePtr->pixelInfo[treePtr->numPixelReferences - 1];
	    }
	    if (treePtr->numPixelReferences == 1) {
		free(linePtr->pixelInfo);
		linePtr->pixelInfo = NULL;
		DEBUG_ALLOC(tkTextCountDestroyPixelInfo++);
	    } else {
		linePtr->pixelInfo = realloc(linePtr->pixelInfo,
			sizeof(linePtr->pixelInfo[0])*(treePtr->numPixelReferences - 1));
	    }
	    linePtr = linePtr->nextPtr;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeJoinUndoInsert --
 *
 *	Joins an undo token with another token.
 *
 * Results:
 *	Return whether the join was possible.
 *
 * Side effects:
 *	The first given will be modified.
 *
 *----------------------------------------------------------------------
 */

bool
TkBTreeJoinUndoInsert(
    TkTextUndoToken *token1,
    unsigned byteSize1,
    TkTextUndoToken *token2,
    unsigned byteSize2)
{
    struct UndoTokenInsert *myToken1 = (UndoTokenInsert *) token1;
    struct UndoTokenInsert *myToken2 = (UndoTokenInsert *) token2;

    if (UndoIndexIsEqual(&myToken1->endIndex, &myToken2->startIndex)) {
	/* append to first token */
	myToken1->endIndex = myToken2->endIndex;
    } else if (UndoIndexIsEqual(&myToken1->startIndex, &myToken2->endIndex)) {
	/* prepend to first token */
	myToken1->startIndex = myToken2->startIndex;
    } else {
	return false;
    }

    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeJoinUndoDelete --
 *
 *	Joins an undo token with another token.
 *
 * Results:
 *	Return whether the join was possible.
 *
 * Side effects:
 *	The first given will be modified.
 *
 *----------------------------------------------------------------------
 */

bool
TkBTreeJoinUndoDelete(
    TkTextUndoToken *token1,
    unsigned byteSize1,
    TkTextUndoToken *token2,
    unsigned byteSize2)
{
    struct UndoTokenDelete *myToken1 = (UndoTokenDelete *) token1;
    struct UndoTokenDelete *myToken2 = (UndoTokenDelete *) token2;

    if (myToken1->inclusive != myToken2->inclusive) {
	return false;
    }

    if (UndoIndexIsEqual(&myToken1->startIndex, &myToken2->startIndex)) {
	unsigned numSegments1 = myToken1->numSegments;

	if (myToken2->endIndex.lineIndex == -1) {
	    myToken1->endIndex = myToken2->endIndex;
	} else if (myToken1->endIndex.lineIndex != -1) {
	    myToken1->endIndex.u.byteIndex += byteSize2;
	} else if (myToken2->endIndex.lineIndex != -1) {
	    myToken1->endIndex.u.byteIndex = myToken2->endIndex.u.byteIndex + byteSize1;
	    myToken1->endIndex.lineIndex = myToken2->endIndex.lineIndex;
	} else if (myToken2->startIndex.lineIndex != -1) {
	    myToken1->endIndex.u.byteIndex = myToken2->startIndex.u.byteIndex + byteSize1 + byteSize2;
	    myToken1->endIndex.lineIndex = myToken2->startIndex.lineIndex;
	} else {
	    myToken1->endIndex.u.byteIndex = myToken1->startIndex.u.byteIndex + byteSize1 + byteSize2;
	    myToken1->endIndex.lineIndex = myToken1->startIndex.lineIndex;
	}

	myToken1->numSegments += myToken2->numSegments;
	myToken1->segments = realloc(myToken1->segments,
		myToken1->numSegments*sizeof(myToken1->segments[0]));
	memcpy(myToken1->segments + numSegments1, myToken2->segments,
		myToken2->numSegments*sizeof(myToken2->segments[0]));
	free(myToken2->segments);
	myToken2->numSegments = 0;
    } else if (UndoIndexIsEqual(&myToken1->startIndex, &myToken2->endIndex)) {
	unsigned numSegments1 = myToken1->numSegments;
	TkTextSegment **segments;

	if (myToken2->startIndex.lineIndex == -1) {
	    myToken1->startIndex = myToken2->startIndex;
	} else if (myToken2->endIndex.lineIndex != -1) {
	    myToken1->startIndex.u.byteIndex = myToken2->endIndex.u.byteIndex - byteSize1;
	    myToken1->startIndex.lineIndex = myToken2->endIndex.lineIndex;
	} else if (myToken1->endIndex.lineIndex != -1) {
	    myToken1->startIndex.u.byteIndex = myToken1->endIndex.u.byteIndex - byteSize1 - byteSize2;
	    myToken1->startIndex.lineIndex = myToken1->endIndex.lineIndex;
	} else {
	    myToken1->startIndex.u.byteIndex = myToken1->startIndex.u.byteIndex + byteSize1 + byteSize2;
	    myToken1->startIndex.lineIndex = myToken1->startIndex.lineIndex;
	}

	myToken1->numSegments += myToken2->numSegments;
	segments = malloc(myToken1->numSegments*sizeof(segments[0]));
	memcpy(segments, myToken2->segments, myToken2->numSegments*sizeof(myToken2->segments[0]));
	memcpy(segments + myToken2->numSegments, myToken1->segments,
		numSegments1*sizeof(myToken1->segments[0]));
	free(myToken1->segments);
	free(myToken2->segments);
	myToken1->segments = segments;
	myToken2->numSegments = 0;
    } else {
	return false;
    }

    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeDestroy --
 *
 *	Delete a B-tree, recycling all of the storage it contains.
 *
 * Results:
 *	The tree is deleted, so 'tree' should never again be used.
 *
 * Side effects:
 *	Memory is freed.
 *
 *----------------------------------------------------------------------
 */

void
TkBTreeDestroy(
    TkTextBTree tree)		/* Tree to clean up. */
{
    BTree *treePtr = (BTree *) tree;

    /*
     * There's no need to loop over each client of the tree, calling
     * 'TkBTreeRemoveClient', since the 'DestroyNode' will clean everything up
     * itself.
     */

    DestroyNode(tree, treePtr->rootPtr);
    free(treePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeHaveElidedSegments --
 *
 *	Return whether this tree contains elided segments.
 *
 * Results:
 *	'true' if this tree contains elided segments, otherwise 'false'.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkBTreeHaveElidedSegments(
    const TkSharedText *sharedTextPtr)
{
    return TkBTreeGetRoot(sharedTextPtr->tree)->numBranches > 0;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeNode --
 *
 *	Free the storage of a node.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The storage of given node will be freed.
 *
 *----------------------------------------------------------------------
 */

static void
FreeNode(
    Node *nodePtr)	/* Free storage of this node. */
{
    assert(nodePtr->level > 0 || nodePtr->linePtr);
    TkTextTagSetDecrRefCount(nodePtr->tagonPtr);
    TkTextTagSetDecrRefCount(nodePtr->tagoffPtr);
    free(nodePtr->pixelInfo);
    DEBUG(nodePtr->linePtr = NULL); /* guarded deallocation */
    free(nodePtr);
    DEBUG_ALLOC(tkTextCountDestroyPixelInfo++);
    DEBUG_ALLOC(tkTextCountDestroyNode++);
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyNode --
 *
 *	This is a recursive utility function used during the deletion of a
 *	B-tree.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All the storage for nodePtr and its descendants is freed.
 *
 *----------------------------------------------------------------------
 */

static void
DestroyNode(
    TkTextBTree tree,
    Node *nodePtr)	/* Destroy from this node downwards. */
{
    if (nodePtr->level == 0) {
	TkTextLine *linePtr;
	TkTextLine *lastPtr;
	TkTextSegment *segPtr;
	TkTextSection *sectionPtr;

	lastPtr = nodePtr->lastPtr->nextPtr;
	linePtr = nodePtr->linePtr;

	while (linePtr != lastPtr) {
	    TkTextLine *nextPtr = linePtr->nextPtr;
	    segPtr = linePtr->segPtr;
	    sectionPtr = segPtr->sectionPtr;
	    while (segPtr) {
		TkTextSegment *nextPtr = segPtr->nextPtr;
		assert(segPtr->typePtr); /* still existing? */
		assert(segPtr->sectionPtr->linePtr == linePtr);
		assert(segPtr->typePtr->deleteProc);
		segPtr->typePtr->deleteProc(tree, segPtr, TREE_GONE);
		segPtr = nextPtr;
	    }
	    FreeSections(sectionPtr);
	    FreeLine((const BTree *) tree, linePtr);
	    linePtr = nextPtr;
	}
    } else {
	Node *childPtr = nodePtr->childPtr;

	while (childPtr) {
	    Node *nextPtr = childPtr->nextPtr;
	    DestroyNode(tree, childPtr);
	    childPtr = nextPtr;
	}
    }
    FreeNode(nodePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeResetDisplayLineCounts --
 *
 *	Reset the display line counts for given line range.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates overall data structures so display line count is consistent.
 *
 *----------------------------------------------------------------------
 */

static void
PropagateDispLineChange(
    Node *nodePtr,
    unsigned pixelReference,
    int subtractFromDispLines,
    int subtractFromPixels)
{
    if (subtractFromDispLines != 0 || subtractFromPixels != 0) {
	for ( ; nodePtr; nodePtr = nodePtr->parentPtr) {
	    NodePixelInfo *pixelInfo = nodePtr->pixelInfo + pixelReference;
	    pixelInfo->numDispLines -= subtractFromDispLines;
	    pixelInfo->pixels -= subtractFromPixels;
	}
    }
}

void
TkBTreeResetDisplayLineCounts(
    TkText *textPtr,
    TkTextLine *linePtr,	/* Start at this line. */
    unsigned numLines)		/* Number of succeeding lines to reset (includes the start line). */
{
    Node *nodePtr = linePtr->parentPtr;
    unsigned pixelReference = textPtr->pixelReference;
    int changeToDispLines = 0;
    int changeToPixels = 0;

    assert(textPtr->pixelReference != -1);

    for ( ; numLines > 0; --numLines) {
	TkTextPixelInfo *pixelInfo = TkBTreeLinePixelInfo(textPtr, linePtr);

	changeToDispLines += (int) GetDisplayLines(linePtr, pixelReference);
	changeToPixels += pixelInfo->height;
	pixelInfo->epoch = 0;
	pixelInfo->height = 0;
	linePtr = linePtr->nextPtr;

	if (pixelInfo->dispLineInfo) {
	    free(pixelInfo->dispLineInfo);
	    pixelInfo->dispLineInfo = NULL;
	    DEBUG_ALLOC(tkTextCountDestroyDispInfo++);
	}

	if (nodePtr != linePtr->parentPtr) {
	    PropagateDispLineChange(nodePtr, pixelReference, changeToDispLines, changeToPixels);
	    changeToDispLines = 0;
	    changeToPixels = 0;
	    nodePtr = linePtr->parentPtr;
	}
    }

    PropagateDispLineChange(nodePtr, pixelReference, changeToDispLines, changeToPixels);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeAdjustPixelHeight --
 *
 *	Adjust the pixel height of a given logical line to the specified
 *	value.
 *
 * Results:
 *	Total number of valid pixels currently known in the tree.
 *
 * Side effects:
 *	Updates overall data structures so pixel height count is consistent.
 *
 *----------------------------------------------------------------------
 */

static void
PropagatePixelCountChange(
    Node *nodePtr,
    unsigned pixelReference,
    int changeToPixels,
    int changeToDispLines)
{
    /*
     * Increment the pixel counts also in all the parent nodes.
     */

    for ( ; nodePtr; nodePtr = nodePtr->parentPtr) {
	NodePixelInfo *pixelInfo = nodePtr->pixelInfo + pixelReference;

	pixelInfo->pixels += changeToPixels;
	pixelInfo->numDispLines += changeToDispLines;
    }
}

void
TkBTreeAdjustPixelHeight(
    const TkText *textPtr,	/* Client of the B-tree. */
    TkTextLine *linePtr,	/* The logical line to update. */
    int newPixelHeight,		/* The line's known height in pixels. */
    unsigned mergedLines,	/* The number of extra lines which have been merged with this one
    				 * (due to elided eols). They will have their pixel height set to
				 * zero, and the total pixel height associated with the given linePtr. */
    unsigned numDispLines)	/* The new number of display lines for this logical line. */
{
    Node *nodePtr = linePtr->parentPtr;
    unsigned pixelReference = textPtr->pixelReference;
    int changeToPixels = 0;
    int changeToDispLines = 0;

    assert(textPtr->pixelReference != -1);
    assert(linePtr->logicalLine || linePtr == GetStartLine(textPtr->sharedTextPtr, textPtr));

    while (true) {
	/*
	 * Do this before updating the line height.
	 */

	changeToDispLines += (int) numDispLines - (int) GetDisplayLines(linePtr, pixelReference);
	changeToPixels += newPixelHeight - linePtr->pixelInfo[pixelReference].height;

	linePtr->pixelInfo[pixelReference].height = newPixelHeight;

	if (mergedLines == 0) {
	    if (changeToPixels || changeToDispLines) {
		PropagatePixelCountChange(nodePtr, pixelReference, changeToPixels, changeToDispLines);
	    }
	    return;
	}

	/*
	 * Any merged logical lines must have their height set to zero.
	 */

	linePtr = linePtr->nextPtr;
	newPixelHeight = 0;
	mergedLines -= 1;
	numDispLines = 0;

	if (nodePtr != linePtr->parentPtr) {
	    if (changeToPixels || changeToDispLines) {
		PropagatePixelCountChange(nodePtr, pixelReference, changeToPixels, changeToDispLines);
	    }
	    changeToPixels = 0;
	    changeToDispLines = 0;
	    nodePtr = linePtr->parentPtr;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeUpdatePixelHeights --
 *
 *	Update the pixel heights, starting with given line. This function
 *	assumes that all logical lines will have monospaced line heights.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates overall data structures so pixel height count is consistent.
 *
 *----------------------------------------------------------------------
 */

void
TkBTreeUpdatePixelHeights(
    const TkText *textPtr,	/* Client of the B-tree. */
    TkTextLine *linePtr,	/* Start with this logical line. */
    int numLines,		/* Number of lines for update (inclusively start line). If negative,
    				 * this is the number of deleted lines. */
    unsigned epoch)		/* Current line metric epoch. */
{
    Node *nodePtr = linePtr->parentPtr;
    unsigned pixelReference = textPtr->pixelReference;
    int lineHeight = textPtr->lineHeight;
    int changeToDispLines = 0;
    int changeToPixels = 0;
    int nlines = ABS(numLines);

    assert(textPtr->pixelReference >= 0);
    assert(textPtr->wrapMode == TEXT_WRAPMODE_NONE);
    assert(lineHeight > 0);

    for ( ; nlines > 0; --nlines) {
	TkTextPixelInfo *pixelInfo = TkBTreeLinePixelInfo(textPtr, linePtr);

	if (pixelInfo->dispLineInfo) {
	    changeToDispLines -= (int) GetDisplayLines(linePtr, pixelReference);
	    if (pixelInfo->height > 0) {
		changeToDispLines += 1;
	    }
	    if (pixelInfo->dispLineInfo) {
		free(pixelInfo->dispLineInfo);
		pixelInfo->dispLineInfo = NULL;
		DEBUG_ALLOC(tkTextCountDestroyDispInfo++);
	    }
	}

	pixelInfo->epoch = epoch;
	changeToPixels -= pixelInfo->height;

	if (pixelInfo->height == 0) {
	    changeToDispLines += 1;
	}

	pixelInfo->height = lineHeight;

	if (numLines > 0) {
	    changeToPixels += lineHeight;
	}

	linePtr = linePtr->nextPtr;

	if (nodePtr != linePtr->parentPtr) {
	    if (changeToPixels || changeToDispLines) {
		PropagatePixelCountChange(nodePtr, pixelReference, changeToPixels, changeToDispLines);
	    }
	    changeToDispLines = 0;
	    changeToPixels = 0;
	    nodePtr = linePtr->parentPtr;
	}
    }

    if (changeToPixels || changeToDispLines) {
	PropagatePixelCountChange(nodePtr, pixelReference, changeToPixels, changeToDispLines);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SubtractPixelInfo --
 *
 *	Decrement the line and pixel counts in all the parent nodes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The pixel counts in the B-tree will be adjusted.
 *
 *----------------------------------------------------------------------
 */

static void
SubtractPixelInfo(
    BTree *treePtr,		/* Tree that is being affected. */
    TkTextLine *linePtr)	/* This line will be deleted. */
{
    Node *nodePtr = linePtr->parentPtr;
    unsigned ref;

    for ( ; nodePtr; nodePtr = nodePtr->parentPtr) {
	NodePixelInfo *dst = nodePtr->pixelInfo;

	nodePtr->numLines -= 1;
	nodePtr->numLogicalLines -= linePtr->logicalLine;
	nodePtr->size -= linePtr->size;

	for (ref = 0; ref < treePtr->numPixelReferences; ++ref, ++dst) {
	    dst->pixels -= linePtr->pixelInfo[ref].height;
	    dst->numDispLines -= GetDisplayLines(linePtr, ref);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SubtractPixelCount2 --
 *
 *	Decrement the line and pixel counts in all the parent nodes.
 *	This function can also be used for incrementation, simply negate
 *	the values 'changeToLineCount' and 'changeToPixelInfo'.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The pixel counts in the B-tree will be adjusted.
 *
 *----------------------------------------------------------------------
 */

static void
SubtractPixelCount2(
    BTree *treePtr,			/* Tree that is being affected. */
    Node *nodePtr,			/* Node that will be adjusted. */
    int changeToLineCount,		/* Number of lines removed. */
    int changeToLogicalLineCount,	/* Number of logical lines removed. */
    int changeToBranchCount,		/* Number of branches removed. */
    int changeToSize,			/* Subtract this size. */
    const NodePixelInfo *changeToPixelInfo)
					/* Values for pixel info adjustment. */
{
    unsigned ref;

    assert(changeToLineCount != 0 || changeToLogicalLineCount == 0);
    assert(changeToLineCount != 0 || changeToBranchCount == 0);

    if (changeToLineCount != 0) {
	for ( ; nodePtr; nodePtr = nodePtr->parentPtr) {
	    NodePixelInfo *dst = nodePtr->pixelInfo;
	    const NodePixelInfo *src = changeToPixelInfo;

	    nodePtr->numLines -= changeToLineCount;
	    nodePtr->numLogicalLines -= changeToLogicalLineCount;
	    nodePtr->numBranches -= changeToBranchCount;
	    nodePtr->size -= changeToSize;

	    for (ref = 0; ref < treePtr->numPixelReferences; ++ref, ++dst, ++src) {
		dst->pixels -= src->pixels;
		dst->numDispLines -= src->numDispLines;
	    }
	}
    } else if (changeToSize != 0) {
	for ( ; nodePtr; nodePtr = nodePtr->parentPtr) {
	    nodePtr->size -= changeToSize;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * AddPixelCount --
 *
 *	Set up a starting default height, which will be re-adjusted later.
 *	We need to do this for each referenced widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory will be allocated.
 *
 *----------------------------------------------------------------------
 */

static void
AddPixelCount(
    BTree *treePtr,
    TkTextLine *linePtr,
    const TkTextLine *refLinePtr,
    NodePixelInfo *changeToPixelInfo)
{
    unsigned ref;

    /*
     * Set up a starting default height, which will be re-adjusted later.
     * We need to do this for each referenced widget.
     */

    linePtr->pixelInfo = malloc(sizeof(TkTextPixelInfo)*treePtr->numPixelReferences);
    DEBUG_ALLOC(tkTextCountNewPixelInfo++);

    for (ref = 0; ref < treePtr->numPixelReferences; ++ref) {
	TkTextPixelInfo *pixelInfo = linePtr->pixelInfo + ref;
	const TkTextPixelInfo *refPixelInfo = refLinePtr->pixelInfo + ref;
	NodePixelInfo *pixelInfoChange = changeToPixelInfo + ref;
	int height = refPixelInfo->height;
	int numDispLines = height > 0;

	pixelInfo->dispLineInfo = NULL;
	pixelInfo->height = height;
	pixelInfo->epoch = 0;
	pixelInfoChange->pixels -= height;
	pixelInfoChange->numDispLines -= numDispLines;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextTestTag --
 *
 *	Return whether the segment at specified position is tagged with
 *	specified tag.
 *
 * Results:
 *	Returns whether this text is tagged with specified tag.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextTestTag(
    const TkTextIndex *indexPtr,/* The character in the text for which display information is wanted. */
    const TkTextTag *tagPtr)	/* Test for this tag. */
{
    assert(tagPtr);
    return TkTextTagSetTest(TkTextIndexGetContentSegment(indexPtr, NULL)->tagInfoPtr, tagPtr->index);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIsElided --
 *
 *	Special case to just return information about elided attribute.
 *	Just need to keep track of invisibility settings for each priority,
 *	pick highest one active at end.
 *
 * Results:
 *	Returns whether this text should be elided or not.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static bool
TestIfElided(
    const TkTextTag *tagPtr)
{
    int highestPriority = -1;
    bool elide = false;

    for ( ; tagPtr; tagPtr = tagPtr->nextPtr) {
	if (tagPtr->elideString && (int) tagPtr->priority > highestPriority) {
	    elide = tagPtr->elide;
	    highestPriority = tagPtr->priority;
	}
    }

    return elide;
}

bool
TkTextIsElided(
    const TkTextIndex *indexPtr)/* The character in the text for which display information is wanted. */
{
    return TkBTreeGetRoot(indexPtr->tree)->numBranches > 0 && TestIfElided(TkBTreeGetTags(indexPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * SegmentIsElided --
 *
 *	Return information about elided attribute of this segment.
 *
 * Results:
 *	Returns whether this component should be elided or not.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static bool
SegmentIsElided(
    const TkSharedText *sharedTextPtr,
    const TkTextSegment *segPtr,
    const TkText *textPtr)		/* can be NULL */
{
    assert(segPtr->tagInfoPtr);

    return TkTextTagSetIntersectsBits(segPtr->tagInfoPtr, sharedTextPtr->elisionTags)
	    && TestIfElided(TkBTreeGetSegmentTags(sharedTextPtr, segPtr, textPtr, NULL));
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextSegmentIsElided --
 *
 *	Return information about elided attribute of this segment.
 *
 * Results:
 *	Returns whether this component should be elided or not.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextSegmentIsElided(
    const TkText *textPtr,
    const TkTextSegment *segPtr)
{
    TkSharedText *sharedTextPtr;

    assert(segPtr->tagInfoPtr);
    assert(textPtr);

    sharedTextPtr = textPtr->sharedTextPtr;
    return TkBTreeHaveElidedSegments(sharedTextPtr) && SegmentIsElided(sharedTextPtr, segPtr, textPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * InsertNewLine --
 *
 *	This function makes a new line, and inserts the given segment as
 *	the first segment in new line. All the required actions to fulfill
 *	the consistency of the B-Tree will be done. But this function is
 *	not rebalancing the B-Tree, nor is it changing the pixel count of
 *	the peers.
 *
 * Results:
 *	The return value is the new line.
 *
 * Side effects:
 *	All the required changes to fulfill the consistency of the
 *	B-Tree, except rebalancing.
 *
 *----------------------------------------------------------------------
 */

static bool
HasElidedNewline(
    const TkSharedText *sharedTextPtr,
    const TkTextLine *linePtr)
{
    return TkBTreeHaveElidedSegments(sharedTextPtr)
	    && SegmentIsElided(sharedTextPtr, linePtr->lastPtr, NULL);
}

static TkTextLine *
InsertNewLine(
    TkSharedText *sharedTextPtr,/* Handle to shared text resource. */
    Node *nodePtr,		/* The node which will contain the new line. */
    TkTextLine *prevLinePtr,	/* Predecessor of the new line, can be NULL. */
    TkTextSegment *segPtr)	/* First segment of this line. */
{
    TkTextLine *newLinePtr;
    TkTextSegment *prevPtr;
    TkTextSegment *lastPtr = segPtr;

    assert(segPtr);
    assert(nodePtr);
    assert(segPtr->sectionPtr || !segPtr->prevPtr);
    assert(!segPtr->prevPtr || segPtr->prevPtr->sectionPtr->linePtr == prevLinePtr);
    assert(!segPtr->prevPtr || prevLinePtr);
    assert(!prevLinePtr || prevLinePtr->parentPtr == nodePtr);

    prevPtr = segPtr->prevPtr;

    if (prevPtr) {
	prevPtr->nextPtr = NULL;
	lastPtr = prevLinePtr->lastPtr;
	prevLinePtr->lastPtr = prevPtr;
	segPtr->prevPtr = NULL;
    }

    newLinePtr = memset(malloc(sizeof(TkTextLine)), 0, sizeof(TkTextLine));
    newLinePtr->parentPtr = nodePtr;
    newLinePtr->prevPtr = prevLinePtr;
    newLinePtr->segPtr = segPtr;
    newLinePtr->lastPtr = lastPtr;
    newLinePtr->logicalLine = true;
    newLinePtr->changed = true;
    DEBUG_ALLOC(tkTextCountNewLine++);

    TkTextTagSetIncrRefCount(newLinePtr->tagonPtr = sharedTextPtr->emptyTagInfoPtr);
    TkTextTagSetIncrRefCount(newLinePtr->tagoffPtr = sharedTextPtr->emptyTagInfoPtr);

    if (prevLinePtr) {
	newLinePtr->logicalLine = !HasElidedNewline(sharedTextPtr, prevLinePtr);
	if ((newLinePtr->nextPtr = prevLinePtr->nextPtr)) {
	    newLinePtr->nextPtr->prevPtr = newLinePtr;
	}
	prevLinePtr->nextPtr = newLinePtr;
    }

    if (segPtr->sectionPtr) {
	if (prevPtr && prevPtr->sectionPtr == segPtr->sectionPtr) {
	    if ((segPtr->sectionPtr = segPtr->sectionPtr->nextPtr)) {
		segPtr->sectionPtr->prevPtr = NULL;
	    }
	    prevPtr->sectionPtr->nextPtr = NULL;
	} else {
	    if (segPtr->sectionPtr->prevPtr) {
		segPtr->sectionPtr->prevPtr->nextPtr = NULL;
	    }
	    segPtr->sectionPtr->prevPtr = NULL;
	}
    }

    RebuildSections(sharedTextPtr, newLinePtr, false);

    if (newLinePtr->numBranches > 0 || newLinePtr->numLinks > 0) {
	assert(prevLinePtr);
	assert(prevLinePtr->numBranches >= newLinePtr->numBranches);
	assert(prevLinePtr->numLinks >= newLinePtr->numLinks);
	prevLinePtr->numBranches -= newLinePtr->numBranches;
	prevLinePtr->numLinks -= newLinePtr->numLinks;
    }

    if (prevPtr) {
	prevPtr->sectionPtr->size = ComputeSectionSize(prevPtr->sectionPtr->segPtr);
	prevPtr->sectionPtr->length = CountSegments(prevPtr->sectionPtr);
	assert(prevPtr->sectionPtr->length == CountSegments(prevPtr->sectionPtr)); /* checks overflow */
	prevPtr->sectionPtr->linePtr->size -= newLinePtr->size;
    }

    if (nodePtr->lastPtr == prevLinePtr) {
	SetNodeLastPointer(nodePtr, newLinePtr);
    }

    assert(!prevLinePtr || CheckSections(prevLinePtr));
    return newLinePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * MakeTagInfo --
 *
 *	Find the associated tag information of the adjacent segment
 *	depending on the current tagging mode. This function is
 *	incrementing the reference count of the returned tag information
 *	set.
 *
 * Results:
 *	The associated tag information.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkTextTagSet *
GetPrevLineTagSet(
    TkText *textPtr,
    TkTextSegment *segPtr)
{
    TkTextLine *linePtr = segPtr->sectionPtr->linePtr->prevPtr;

    if (!linePtr) {
	return textPtr->sharedTextPtr->emptyTagInfoPtr;
    }

    /*
     * Didn't find any tag information in this line, so try the last segment of the
     * previous line, this segment must have a tag information.
     */

    segPtr = linePtr->lastPtr;
    assert(segPtr->tagInfoPtr);
    return segPtr->tagInfoPtr;
}

static TkTextTagSet *
MakeTagInfo(
    TkText *textPtr,
    TkTextSegment *segPtr)	/* The first inserted text segment. */
{
    TkTextTagSet *tagInfoPtr = textPtr->sharedTextPtr->emptyTagInfoPtr;

    assert(segPtr);
    assert(textPtr);
    assert(textPtr->insertMarkPtr);

    switch (textPtr->tagging) {
    case TK_TEXT_TAGGING_WITHIN: {
	/*
	 * This is the traditional tagging mode. Search for the tags on both sides
	 * of the inserted text.
	 */

	TkTextSegment *segPtr2;
	TkTextTagSet *tagInfoPtr2 = NULL;

	for (segPtr2 = segPtr->nextPtr; !segPtr2->tagInfoPtr; segPtr2 = segPtr2->nextPtr) {
	    assert(segPtr2);
	}
	TkTextTagSetIncrRefCount(tagInfoPtr = segPtr2->tagInfoPtr);
	segPtr2 = segPtr;
	while (!tagInfoPtr2) {
	    segPtr2 = segPtr2->prevPtr;
	    if (!segPtr2) {
		tagInfoPtr2 = GetPrevLineTagSet(textPtr, segPtr);
	    } else if (segPtr2->tagInfoPtr) {
		tagInfoPtr2 = segPtr2->tagInfoPtr;
	    }
	}
	return TagSetIntersect(tagInfoPtr, tagInfoPtr2, textPtr->sharedTextPtr);
    }
    case TK_TEXT_TAGGING_GRAVITY:
	/*
	 * Search for a adjcacent content segment which will provide the appropriate tag
	 * information, the direction of the search depends on the gravity of the "insert"
	 * mark. If we cannot find a segment, then the tag information will be empty.
	 */

	if (textPtr->insertMarkPtr->typePtr == &tkTextLeftMarkType) {
	    if ((segPtr = segPtr->nextPtr)) {
		while (segPtr->typePtr->gravity != GRAVITY_LEFT || segPtr->typePtr == &tkTextLinkType) {
		    if (segPtr->tagInfoPtr) {
			if (segPtr->typePtr == &tkTextCharType) {
			    tagInfoPtr = segPtr->tagInfoPtr;
			}
			TkTextTagSetIncrRefCount(tagInfoPtr);
			return tagInfoPtr;
		    }
		    segPtr = segPtr->nextPtr;
		    assert(segPtr);
		}
	    }
	} else {
	    if (!segPtr->prevPtr) {
		TkTextTagSetIncrRefCount(tagInfoPtr = GetPrevLineTagSet(textPtr, segPtr));
		return tagInfoPtr;
	    }
	    while (segPtr->typePtr->gravity != GRAVITY_RIGHT || segPtr->typePtr == &tkTextBranchType) {
		if (segPtr->tagInfoPtr) {
		    if (segPtr->typePtr == &tkTextCharType) {
			tagInfoPtr = segPtr->tagInfoPtr;
		    }
		    TkTextTagSetIncrRefCount(tagInfoPtr);
		    return tagInfoPtr;
		}
		if (!segPtr->prevPtr) {
		    TkTextTagSetIncrRefCount(tagInfoPtr = GetPrevLineTagSet(textPtr, segPtr));
		    return tagInfoPtr;
		}
		segPtr = segPtr->prevPtr;
	    }
	}
	break;
    case TK_TEXT_TAGGING_NONE:
    	/*
	 * The new text will not be tagged.
	 */
	break;
    }

    return tagInfoPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeLoad --
 *
 *	Load the given content into the widget. The content must be the
 *	result of the "inspect" command.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	The B-Tree structure will change, and some segments will be
 *	added.
 *
 *----------------------------------------------------------------------
 */

static int
LoadError(
    Tcl_Interp *interp,		/* Current interpreter. */
    const char *msg,		/* Error message, can be NULL. */
    int index0,			/* List index at level 0. */
    int index1,			/* List index at level 1, is -1 if undefined. */
    int index2,			/* List index at level 2, is -1 if undefined. */
    TkTextTagSet *tagInfoPtr)	/* Decrement reference count if not NULL. */
{
    char buf[100] = { '\0' };
    Tcl_Obj *errObjPtr = NULL;

    if (!msg) {
	Tcl_IncrRefCount(errObjPtr = Tcl_GetObjResult(interp));
	msg = Tcl_GetString(errObjPtr);
    }
    if (tagInfoPtr) {
	TkTextTagSetDecrRefCount(tagInfoPtr);
    }
    if (index0 >= 0) {
	if (index1 >= 0) {
	    if (index2 >= 0) {
		snprintf(buf, sizeof(buf), " (at index %d %d %d)", index0, index1, index2);
	    } else {
		snprintf(buf, sizeof(buf), " (at index %d %d)", index0, index1);
	    }
	} else {
	    snprintf(buf, sizeof(buf), " (at index %d)", index0);
	}
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("error while loading%s: %s", buf, msg));
    Tcl_SetErrorCode(interp, "TK", "TEXT", "LOAD", NULL);
    if (errObjPtr) {
	Tcl_DecrRefCount(errObjPtr);
    }
    return TCL_ERROR;
}

static bool
LoadMakeTagInfo(
    TkText *textPtr,
    TkTextTagSet **tagInfoPtr,
    Tcl_Obj *obj)
{
    int objc, i;
    Tcl_Obj **objv;

    if (Tcl_ListObjGetElements(textPtr->interp, obj, &objc, &objv) != TCL_OK) {
	return false;
    }
    if (!*tagInfoPtr) {
	TkTextTagSetIncrRefCount(*tagInfoPtr = textPtr->sharedTextPtr->emptyTagInfoPtr);
    }
    for (i = 0; i < objc; ++i) {
	TkTextTag *tagPtr = TkTextCreateTag(textPtr, Tcl_GetString(objv[i]), NULL);
	*tagInfoPtr = TkTextTagSetAddToThis(*tagInfoPtr, tagPtr->index);
    }
    return true;
}

static bool
LoadRemoveTags(
    TkText *textPtr,
    TkTextTagSet **tagInfoPtr,
    Tcl_Obj *obj)
{
    int objc, i;
    Tcl_Obj **objv;

    assert(*tagInfoPtr);

    if (Tcl_ListObjGetElements(textPtr->interp, obj, &objc, &objv) != TCL_OK) {
	return false;
    }
    for (i = 0; i < objc; ++i) {
	TkTextTag *tagPtr = TkTextCreateTag(textPtr, Tcl_GetString(objv[i]), NULL);
	*tagInfoPtr = TkTextTagSetEraseFromThis(*tagInfoPtr, tagPtr->index);
    }
    return true;
}

static TkTextSegment *
LoadPerformElision(
    TkText *textPtr,
    TkTextSegment *segPtr,	/* newly inserted segment */
    TkTextSegment **branchPtr,	/* last inserted branch segment */
    TkTextSegment *contentPtr,	/* last char/hyphen/image/window segment in current line */
    bool *isElided)		/* elided state of last inserted segment */
{
    TkTextSegment *nextPtr = segPtr; /* next segment to insert into line */
    bool elide = SegmentIsElided(textPtr->sharedTextPtr, segPtr, textPtr);

    if (elide != *isElided) {
	TkTextSegment *linkPtr;

	if (elide) {
	    nextPtr = *branchPtr = MakeBranch();
	    (*branchPtr)->nextPtr = segPtr;
	    segPtr->prevPtr = *branchPtr;
	} else {
	    assert(*branchPtr);
	    linkPtr = MakeLink();
	    linkPtr->body.link.prevPtr = *branchPtr;
	    (*branchPtr)->body.branch.nextPtr = linkPtr;
	    if (contentPtr) {
		linkPtr->nextPtr = contentPtr->nextPtr;
		linkPtr->prevPtr = contentPtr;
		contentPtr->nextPtr = linkPtr;
	    } else {
		linkPtr->nextPtr = segPtr;
		segPtr->prevPtr = linkPtr;
		nextPtr = linkPtr;
	    }
	}

	*isElided = elide;
    }

    return nextPtr;
}

int
TkBTreeLoad(
    TkText *textPtr,		/* Information about text widget. */
    Tcl_Obj *content)		/* New content of this text widget. */
{
    enum {
	STATE_START        = 1 << 0,
	STATE_SETUP        = 1 << 1,
	STATE_CONFIG       = 1 << 2,
	STATE_LEFT         = 1 << 3,
	STATE_RIGHT        = 1 << 4,
	STATE_LEFT_INSERT  = 1 << 5,
	STATE_RIGHT_INSERT = 1 << 6,
	STATE_TEXT         = 1 << 7,
	STATE_BREAK        = 1 << 8
    };

    Tcl_Obj **objv;
    int objc, i;
    int byteLength;
    TkTextTagSet *tagInfoPtr;
    TkSharedText *sharedTextPtr;
    TkTextSegment *segPtr;
    TkTextSegment *charSegPtr;
    TkTextSegment *nextSegPtr;
    TkTextSegment *branchPtr;
    TkTextSegment *hyphPtr;
    TkTextSegment *embPtr;
    TkTextSegment *contentPtr;
    TkTextLine *linePtr;
    TkTextLine *newLinePtr;
    TkTextLine *startLinePtr;
    BTree *treePtr;
    NodePixelInfo *changeToPixelInfo;
    Tcl_Interp *interp = textPtr->interp;
    TkTextState textState;
    const char *name;
    const char *s;
    unsigned tagInfoCount;
    unsigned state;
    int changeToLineCount;
    int changeToLogicalLineCount;
    int changeToBranchCount;
    int size;
    bool isElided;
    bool isInsert;

    if (Tcl_ListObjGetElements(interp, content, &objc, &objv) != TCL_OK) {
	return LoadError(interp, "list of items expected", -1, -1, -1, NULL);
    }

    sharedTextPtr = textPtr->sharedTextPtr;
    treePtr = (BTree *) sharedTextPtr->tree;
    linePtr = startLinePtr = treePtr->rootPtr->linePtr;
    segPtr = linePtr->segPtr;
    contentPtr = NULL;
    branchPtr = NULL;
    hyphPtr = NULL;
    tagInfoPtr = NULL;
    changeToLineCount = 0;
    changeToLogicalLineCount = 0;
    changeToBranchCount = 0;
    tagInfoCount = 0;
    textState = textPtr->state;
    textPtr->state = TK_TEXT_STATE_NORMAL;
    isElided = false;
    state = STATE_START;
    size = 0;

    assert(segPtr->typePtr != &tkTextCharType);

    changeToPixelInfo = treePtr->pixelInfoBuffer;
    memset(changeToPixelInfo, 0, sizeof(changeToPixelInfo[0])*treePtr->numPixelReferences);

    while (segPtr->nextPtr->typePtr != &tkTextCharType) {
	segPtr = segPtr->nextPtr;
    }
    charSegPtr = NULL;

    for (i = 0; i < objc; ++i) {
	const char *type;
	Tcl_Obj **argv;
	int argc;

	if (Tcl_ListObjGetElements(interp, objv[i], &argc, &argv) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (argc == 0) {
	    return LoadError(interp, "empty item", i, 0, -1, tagInfoPtr);
	}

	type = Tcl_GetString(argv[0]);

	switch (type[0]) {
	case 's': {
	    /*
	     * {"setup" pathname configuration}
	     */

	    Tcl_Obj **objv;
	    int objc;

	    if (strcmp(type, "setup") != 0) {
		return LoadError(interp, "invalid item identifier", i, 0, -1, tagInfoPtr);
	    }
	    if (state != STATE_START) {
		return LoadError(interp, "unexpected \"setup\" item", i, -1, -1, tagInfoPtr);
	    }
	    if (argc != 3) {
		return LoadError(interp, "wrong number of items", i, -1, -1, tagInfoPtr);
	    }
	    if (Tcl_ListObjGetElements(interp, argv[2], &objc, &objv) != TCL_OK
		    || TkConfigureText(interp, textPtr, objc, objv) != TCL_OK) {
		return LoadError(interp, NULL, i, 2, -1, tagInfoPtr);
	    }
	    textState = textPtr->state;
	    textPtr->state = TK_TEXT_STATE_READONLY;
	    state = STATE_SETUP;
	    break;
	}
	case 'b':
	    switch (type[1]) {
	    case 'r':
		/*
		 * {"break" taginfo ?taginfo?}
		 */

		if (strcmp(type, "break") != 0) {
		    return LoadError(interp, "invalid item identifier", i, 0, -1, tagInfoPtr);
		}
		if (tagInfoCount == 0) {
		    tagInfoCount = argc - 1;
		}
		if (argc < 2 || 3 < argc || argc - tagInfoCount != 1) {
		    return LoadError(interp, "wrong number of items", i, -1, -1, tagInfoPtr);
		}
		if (!LoadMakeTagInfo(textPtr, &tagInfoPtr, argv[1])) {
		    return LoadError(interp, "list of tag names expected", i, 1, -1, tagInfoPtr);
		}
		if (charSegPtr && TkTextTagSetIsEqual(tagInfoPtr, charSegPtr->tagInfoPtr)) {
		    charSegPtr = IncreaseCharSegment(charSegPtr, charSegPtr->size, 1);
		    charSegPtr->body.chars[charSegPtr->size - 1] = '\n';
		    linePtr->lastPtr = charSegPtr;
		    RebuildSections(sharedTextPtr, linePtr, true);
		} else {
		    nextSegPtr = charSegPtr = MakeCharSeg(NULL, tagInfoPtr, 1, "\n", 1);
		    if (sharedTextPtr->numElisionTags > 0) {
			nextSegPtr = LoadPerformElision(textPtr, charSegPtr, &branchPtr, contentPtr,
				&isElided);
		    }
		    if (segPtr) {
			segPtr->nextPtr = nextSegPtr;
			nextSegPtr->prevPtr = segPtr;
			linePtr->lastPtr = charSegPtr;
			RebuildSections(sharedTextPtr, linePtr, true);
		    } else {
			newLinePtr = InsertNewLine(sharedTextPtr, linePtr->parentPtr,
				linePtr, nextSegPtr);
			AddPixelCount(treePtr, newLinePtr, linePtr, changeToPixelInfo);
			linePtr = newLinePtr;
		    }
		}
		changeToLineCount += 1;
		if (!isElided) {
		    changeToLogicalLineCount += 1;
		}
		size += 1;
		contentPtr = charSegPtr;
		segPtr = charSegPtr = NULL;
		state = STATE_BREAK;
		RecomputeLineTagInfo(linePtr, NULL, sharedTextPtr);
		if (argc != 3) {
		    TkTextTagSetDecrRefCount(tagInfoPtr);
		    tagInfoPtr = NULL;
		} else if (!LoadRemoveTags(textPtr, &tagInfoPtr, argv[2])) {
		    return LoadError(interp, "list of tag names expected", i, 2, -1, tagInfoPtr);
		}
		break;
	    case 'i': {
		TkTextTag *tagPtr;

		/*
		 * {"bind" tagname event script}
		 */

		if (strcmp(type, "bind") != 0) {
		    return LoadError(interp, "invalid item identifier", i, 0, -1, tagInfoPtr);
		}
		if (argc != 4) {
		    return LoadError(interp, "wrong number of items", i, -1, -1, tagInfoPtr);
		}
		tagPtr = TkTextCreateTag(textPtr, Tcl_GetString(argv[1]), NULL);
		if (TkTextBindEvent(interp, argc - 2, argv + 2, textPtr->sharedTextPtr,
			&sharedTextPtr->tagBindingTable, tagPtr->name) != TCL_OK) {
		    return LoadError(interp, NULL, i, 2, -1, tagInfoPtr);
		}
		state = STATE_TEXT;
		break;
	    }
	    }
	    break;
	case 'c': {
	    /*
	     * {"configure" tagname ?configuration?}
	     */

	    Tcl_Obj **objv;
	    int objc;

	    if (strcmp(type, "configure") != 0) {
		return LoadError(interp, "invalid item identifier", i, 0, -1, tagInfoPtr);
	    }
	    if (!(state & (STATE_START|STATE_SETUP|STATE_CONFIG))) {
		return LoadError(interp, "unexpected \"configure\" item", i, -1, -1, tagInfoPtr);
	    }
	    if (argc == 2) {
		TkTextCreateTag(textPtr, Tcl_GetString(argv[1]), NULL);
	    } else if (argc != 3) {
		return LoadError(interp, "wrong number of items", i, -1, -1, tagInfoPtr);
	    } else if (Tcl_ListObjGetElements(interp, argv[2], &objc, &objv) != TCL_OK
		    && TkConfigureTag(interp, textPtr, Tcl_GetString(argv[1]), objc, objv) != TCL_OK) {
		return LoadError(interp, NULL, i, 2, -1, tagInfoPtr);
	    }
	    state = STATE_CONFIG;
	    break;
	}
	case 't':
	    /*
	     * {"text" content taginfo ?taginfo?}
	     */

	    if (strcmp(type, "text") != 0) {
		return LoadError(interp, "invalid item identifier", i, 0, -1, tagInfoPtr);
	    }
	    if (tagInfoCount == 0) {
		tagInfoCount = argc - 2;
	    }
	    if (argc < 3 || 4 < argc || argc - tagInfoCount != 2) {
		return LoadError(interp, "wrong number of items", i, -1, -1, tagInfoPtr);
	    }
	    if (!LoadMakeTagInfo(textPtr, &tagInfoPtr, argv[2])) {
		return LoadError(interp, "list of tag names expected", i, 2, -1, tagInfoPtr);
	    }
	    for (s = Tcl_GetString(argv[1]); *s; ++s) {
		switch (UCHAR(*s)) {
		case 0x0a:
		    return LoadError(interp, "newline not allowed in text content",
			    i, 1, -1, tagInfoPtr);
		case 0xc2:
		    if (UCHAR(s[1]) == 0xad) {
			return LoadError(interp, "soft hyphen (U+002D) not allowed in text content",
				i, 1, -1, tagInfoPtr);
		    }
		    break;
		}
	    }
	    byteLength = GetByteLength(argv[1]);
	    if (charSegPtr && TkTextTagSetIsEqual(tagInfoPtr, charSegPtr->tagInfoPtr)) {
		int size = charSegPtr->size;
		charSegPtr = IncreaseCharSegment(charSegPtr, size, byteLength);
		memcpy(charSegPtr->body.chars + size, Tcl_GetString(argv[1]), byteLength);
	    } else {
		nextSegPtr = charSegPtr = MakeCharSeg(NULL, tagInfoPtr,
			byteLength, Tcl_GetString(argv[1]), byteLength);
		if (sharedTextPtr->numElisionTags > 0) {
		    nextSegPtr = LoadPerformElision(textPtr, charSegPtr, &branchPtr, contentPtr,
			    &isElided);
		}
		if (segPtr) {
		    segPtr->nextPtr = nextSegPtr;
		    nextSegPtr->prevPtr = segPtr;
		} else {
		    newLinePtr = InsertNewLine(sharedTextPtr, linePtr->parentPtr, linePtr, nextSegPtr);
		    AddPixelCount(treePtr, newLinePtr, linePtr, changeToPixelInfo);
		    linePtr = newLinePtr;
		}
	    }
	    size += byteLength;
	    contentPtr = segPtr = charSegPtr;
	    state = STATE_TEXT;
	    if (argc != 4) {
		TkTextTagSetDecrRefCount(tagInfoPtr);
		tagInfoPtr = NULL;
	    } else if (!LoadRemoveTags(textPtr, &tagInfoPtr, argv[3])) {
		return LoadError(interp, "list of tag names expected", i, 3, -1, tagInfoPtr);
	    }
	    break;
	case 'h':
	    /*
	     * {"hyphen" taginfo ?taginfo?}
	     */

	    if (strcmp(type, "hyphen") != 0) {
		return LoadError(interp, "invalid item identifier", i, 0, -1, tagInfoPtr);
	    }
	    if (tagInfoCount == 0) {
		tagInfoCount = argc - 1;
	    }
	    if (argc < 2 || 3 < argc || argc - tagInfoCount != 1) {
		return LoadError(interp, "wrong number of items", i, -1, -1, tagInfoPtr);
	    }
	    if (!LoadMakeTagInfo(textPtr, &tagInfoPtr, argv[1])) {
		return LoadError(interp, "list of tag names expected", i, 1, -1, tagInfoPtr);
	    }
	    nextSegPtr = hyphPtr = MakeHyphen();
	    TkTextTagSetIncrRefCount(hyphPtr->tagInfoPtr = tagInfoPtr);
	    if (sharedTextPtr->numElisionTags > 0) {
		nextSegPtr = LoadPerformElision(textPtr, charSegPtr, &branchPtr, contentPtr, &isElided);
	    }
	    if (segPtr) {
		segPtr->nextPtr = nextSegPtr;
		nextSegPtr->prevPtr = segPtr;
	    } else {
		newLinePtr = InsertNewLine(sharedTextPtr, linePtr->parentPtr, linePtr, nextSegPtr);
		AddPixelCount(treePtr, newLinePtr, linePtr, changeToPixelInfo);
		linePtr = newLinePtr;
	    }
	    size += 1;
	    contentPtr = segPtr = hyphPtr;
	    state = STATE_TEXT;
	    if (argc != 3) {
		TkTextTagSetDecrRefCount(tagInfoPtr);
		tagInfoPtr = NULL;
	    } else if (!LoadRemoveTags(textPtr, &tagInfoPtr, argv[2])) {
		return LoadError(interp, "list of tag names expected", i, 2, -1, tagInfoPtr);
	    }
	    break;
	case 'l':
	    /*
	     * {"left" markname}
	     */

	    if (strcmp(type, "left") != 0) {
		return LoadError(interp, "invalid item identifier", i, 0, -1, tagInfoPtr);
	    }
	    name = Tcl_GetString(argv[1]);
	    isInsert = (strcmp(name, "insert") == 0);
	    if (sharedTextPtr->steadyMarks
		    ? state == STATE_RIGHT_INSERT || (isInsert && state == STATE_LEFT)
		    : state == STATE_RIGHT) {
		return LoadError(interp, "unexpected \"left\" item", i, -1, -1, tagInfoPtr);
	    }
	    if (argc != 2) {
		return LoadError(interp, "wrong number of items", i, -1, -1, tagInfoPtr);
	    }
	    if (isInsert) {
		UnlinkSegment(nextSegPtr = textPtr->insertMarkPtr);
	    } else if (!(nextSegPtr = TkTextMakeNewMark(textPtr, name))) {
		return LoadError(interp, "mark already exists", i, 1, -1, tagInfoPtr);
	    }
	    nextSegPtr->typePtr = &tkTextLeftMarkType;
	    if (segPtr) {
		segPtr->nextPtr = nextSegPtr;
		nextSegPtr->prevPtr = segPtr;
	    } else {
		newLinePtr = InsertNewLine(sharedTextPtr, linePtr->parentPtr, linePtr, nextSegPtr);
		AddPixelCount(treePtr, newLinePtr, linePtr, changeToPixelInfo);
		linePtr = newLinePtr;
	    }
	    segPtr = nextSegPtr;
	    contentPtr = NULL;
	    state = isInsert ? STATE_LEFT_INSERT : STATE_LEFT;
	    break;
	case 'r':
	    /*
	     * {"right" markname}
	     */

	    if (strcmp(type, "right") != 0) {
		return LoadError(interp, "invalid item identifier", i, 0, -1, tagInfoPtr);
	    }
	    if (argc != 2) {
		return LoadError(interp, "wrong number of items", i, -1, -1, tagInfoPtr);
	    }
	    name = Tcl_GetString(argv[1]);
	    isInsert = (strcmp(name, "insert") == 0);
	    if (isInsert
		    && sharedTextPtr->steadyMarks
		    && (state & (STATE_LEFT|STATE_RIGHT))) {
		return LoadError(interp, "unexpected \"insert\" mark", i, -1, -1, tagInfoPtr);
	    }
	    if (isInsert) {
		UnlinkSegment(nextSegPtr = textPtr->insertMarkPtr);
	    } else if (!(nextSegPtr = TkTextMakeNewMark(textPtr, name))) {
		return LoadError(interp, "mark already exists", i, 1, -1, tagInfoPtr);
	    }
	    assert(nextSegPtr->typePtr == &tkTextRightMarkType);
	    if (segPtr) {
		segPtr->nextPtr = nextSegPtr;
		nextSegPtr->prevPtr = segPtr;
	    } else {
		newLinePtr = InsertNewLine(sharedTextPtr, linePtr->parentPtr, linePtr, nextSegPtr);
		AddPixelCount(treePtr, newLinePtr, linePtr, changeToPixelInfo);
		linePtr = newLinePtr;
	    }
	    segPtr = nextSegPtr;
	    contentPtr = NULL;
	    state = isInsert ? STATE_RIGHT_INSERT : STATE_RIGHT;
	    break;
	case 'e':
	    /*
	     * {"elide" "on"}, {"elide" "off"}
	     * These elements will be skipped, nevertheless we check the syntax.
	     */

	    if (strcmp(type, "elide") != 0) {
		return LoadError(interp, "invalid item identifier", i, 0, -1, tagInfoPtr);
	    }
	    if (argc != 2) {
		return LoadError(interp, "wrong number of items", i, -1, -1, tagInfoPtr);
	    }
	    if (strcmp(Tcl_GetString(argv[1]), "on") != 0
		    && strcmp(Tcl_GetString(argv[1]), "off") != 0) {
		return LoadError(interp, "\"on\" or \"off\" expected", i, 0, -1, tagInfoPtr);
	    }
	    state = STATE_TEXT;
	    break;
	case 'i':
	    /*
	     * {"image" options tagInfo ?tagInfo?}
	     */

	    if (strcmp(type, "image") != 0) {
		return LoadError(interp, "invalid item identifier", i, 0, -1, tagInfoPtr);
	    }
	    if (tagInfoCount == 0) {
		tagInfoCount = argc - 2;
	    }
	    if (argc < 3 || 4 < argc || argc - tagInfoCount != 2) {
		return LoadError(interp, "wrong number of items", i, -1, -1, tagInfoPtr);
	    }
	    if (!(embPtr = TkTextMakeImage(textPtr, argv[1]))) {
		return LoadError(interp, Tcl_GetString(Tcl_GetObjResult(interp)), i, 1, -1, tagInfoPtr);
	    }
	    if (!LoadMakeTagInfo(textPtr, &tagInfoPtr, argv[2])) {
		return LoadError(interp, "list of tag names expected", i, 2, -1, tagInfoPtr);
	    }
	    TkTextTagSetIncrRefCount((nextSegPtr = embPtr)->tagInfoPtr = tagInfoPtr);
	    if (sharedTextPtr->numElisionTags > 0) {
		nextSegPtr = LoadPerformElision(textPtr, embPtr, &branchPtr, contentPtr, &isElided);
	    }
	    if (segPtr) {
		segPtr->nextPtr = nextSegPtr;
		nextSegPtr->prevPtr = segPtr;
	    } else {
		newLinePtr = InsertNewLine(sharedTextPtr, linePtr->parentPtr, linePtr, nextSegPtr);
		AddPixelCount(treePtr, newLinePtr, linePtr, changeToPixelInfo);
		linePtr = newLinePtr;
	    }
	    size += 1;
	    contentPtr = segPtr = embPtr;
	    state = STATE_TEXT;
	    if (argc != 4) {
		TkTextTagSetDecrRefCount(tagInfoPtr);
		tagInfoPtr = NULL;
	    } else if (!LoadRemoveTags(textPtr, &tagInfoPtr, argv[3])) {
		return LoadError(interp, "list of tag names expected", i, 3, -1, tagInfoPtr);
	    }
	    break;
	case 'w':
	    /*
	     * {"window" options tagInfo ?tagInfo?}
	     */

	    if (strcmp(type, "window") != 0) {
		return LoadError(interp, "invalid item identifier", i, 0, -1, tagInfoPtr);
	    }
	    if (tagInfoCount == 0) {
		tagInfoCount = argc - 2;
	    }
	    if (argc < 3 || 4 < argc || argc - tagInfoCount != 2) {
		return LoadError(interp, "wrong number of items", i, -1, -1, tagInfoPtr);
	    }
	    if (!(embPtr = TkTextMakeImage(textPtr, argv[1]))) {
		return LoadError(interp, Tcl_GetString(Tcl_GetObjResult(interp)), i, 1, -1, tagInfoPtr);
	    }
	    if (!LoadMakeTagInfo(textPtr, &tagInfoPtr, argv[2])) {
		return LoadError(interp, "list of tag names expected", i, 2, -1, tagInfoPtr);
	    }
	    TkTextTagSetIncrRefCount((nextSegPtr = embPtr)->tagInfoPtr = tagInfoPtr);
	    if (sharedTextPtr->numElisionTags > 0) {
		nextSegPtr = LoadPerformElision(textPtr, embPtr, &branchPtr, contentPtr, &isElided);
	    }
	    if (segPtr) {
		segPtr->nextPtr = nextSegPtr;
		nextSegPtr->prevPtr = segPtr;
	    } else {
		newLinePtr = InsertNewLine(sharedTextPtr, linePtr->parentPtr, linePtr, nextSegPtr);
		AddPixelCount(treePtr, newLinePtr, linePtr, changeToPixelInfo);
		linePtr = newLinePtr;
	    }
	    size += 1;
	    contentPtr = segPtr = embPtr;
	    state = STATE_TEXT;
	    if (argc != 4) {
		TkTextTagSetDecrRefCount(tagInfoPtr);
		tagInfoPtr = NULL;
	    } else if (!LoadRemoveTags(textPtr, &tagInfoPtr, argv[3])) {
		return LoadError(interp, "list of tag names expected", i, 3, -1, tagInfoPtr);
	    }
	    break;
	default:
	    return LoadError(interp, "invalid item identifier", i, 0, -1, tagInfoPtr);
	}
    }

    /*
     * Possible we have to add last newline.
     */

    if (state != STATE_BREAK) {
	if (charSegPtr && TkTextTagSetIsEmpty(charSegPtr->tagInfoPtr)) {
	    charSegPtr = IncreaseCharSegment(charSegPtr, charSegPtr->size, 1);
	    charSegPtr->body.chars[charSegPtr->size - 1] = '\n';
	    linePtr->lastPtr = charSegPtr;
	    RebuildSections(sharedTextPtr, linePtr, true);
	} else {
	    nextSegPtr = charSegPtr = MakeCharSeg(NULL, sharedTextPtr->emptyTagInfoPtr, 1, "\n", 1);
	    if (segPtr) {
		segPtr->nextPtr = nextSegPtr;
		nextSegPtr->prevPtr = segPtr;
		linePtr->lastPtr = charSegPtr;
		RebuildSections(sharedTextPtr, linePtr, true);
	    } else {
		newLinePtr = InsertNewLine(sharedTextPtr, linePtr->parentPtr,
			linePtr, nextSegPtr);
		AddPixelCount(treePtr, newLinePtr, linePtr, changeToPixelInfo);
		linePtr = newLinePtr;
	    }
	}
	size += 1;
	RecomputeLineTagInfo(linePtr, NULL, sharedTextPtr);
    } else {
	changeToLineCount -= 1;
	if (!isElided) {
	    changeToLogicalLineCount -= 1;
	}
    }

    textPtr->state = textState;

    if (tagInfoPtr) {
	TkTextTagSetDecrRefCount(tagInfoPtr);
    }

    SubtractPixelCount2(treePtr, startLinePtr->parentPtr, -changeToLineCount,
	    -changeToLogicalLineCount, -changeToBranchCount, -size, changeToPixelInfo);
    startLinePtr->parentPtr->numChildren += changeToLineCount;
    UpdateNodeTags(sharedTextPtr, startLinePtr->parentPtr);

    if (startLinePtr->parentPtr->numChildren > MAX_CHILDREN) {
	Rebalance(treePtr, startLinePtr->parentPtr);
    }

    TK_BTREE_DEBUG(TkBTreeCheck(sharedTextPtr->tree));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeInsertChars --
 *
 *	Insert characters at a given position in a B-tree.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Characters are added to the B-tree at the given position. If the
 *	string contains newlines, new lines will be added, which could cause
 *	the structure of the B-tree to change.
 *
 *----------------------------------------------------------------------
 */

void
TkBTreeInsertChars(
    TkTextBTree tree,		/* Tree to insert into. */
    TkTextIndex *indexPtr,	/* Indicates where to insert text. When the function returns,
    				 * this index contains the new position. */
    const char *string,		/* Pointer to bytes to insert (may contain newlines, must be
    				 * null-terminated). */
    TkTextTagSet *tagInfoPtr,	/* Tag information for the new segments, can be NULL. */
    TkTextTag *hyphenTagPtr,	/* Tag information for hyphen segments, can be NULL. If not NULL
    				 * this is a list of tags connected via 'nextPtr'. */
    TkTextUndoInfo *undoInfo)	/* Undo information, can be NULL. */
{
    TkSharedText *sharedTextPtr;
    TkTextSegment *prevPtr;	/* The segment just before the first new segment (NULL means new
    				 * segment is at beginning of line). */
    TkTextLine *linePtr;	/* Current line (new segments are added to this line). */
    int changeToLineCount;	/* Counts change to total number of lines in file. */
    int changeToLogicalLineCount;
				/* Counts change to total number of logical lines in file. */
    NodePixelInfo *changeToPixelInfo;
    TkTextSegment *segPtr = NULL;
    TkTextSegment *firstSegPtr;
    TkTextSegment *lastSegPtr;
    TkTextLine *newLinePtr;
    TkTextLine *firstLinePtr;
    TkTextTagSet *emptyTagInfoPtr;
    TkTextTagSet *hyphenTagInfoPtr = NULL;
    TkTextTagSet *myTagInfoPtr;
    TkTextTag *tagPtr;
    TkTextTag *hyphenElideTagPtr = NULL;
    UndoTokenInsert *undoToken = NULL;
    BTree *treePtr = (BTree *) tree;
    bool split = true;
    SplitInfo info;
    unsigned chunkSize = 0; /* satisifies the compiler */
    unsigned size = 0;
    int hyphenRules = 0;

    assert(*string); /* otherwise tag information might become erroneous */
    assert(indexPtr->textPtr);

    sharedTextPtr = treePtr->sharedTextPtr;

    if (undoInfo) {
	undoToken = malloc(sizeof(UndoTokenInsert));
	undoToken->undoType = &undoTokenInsertType;
	undoInfo->token = (TkTextUndoToken *) undoToken;
	undoInfo->byteSize = 0;
	MakeUndoIndex(sharedTextPtr, indexPtr, &undoToken->startIndex, GRAVITY_LEFT);
	DEBUG_ALLOC(tkTextCountNewUndoToken++);
    }

    emptyTagInfoPtr = sharedTextPtr->emptyTagInfoPtr;
    firstSegPtr = lastSegPtr = NULL;
    prevPtr = NULL;
    memset(&info, 0, sizeof(SplitInfo));
    info.offset = -1;
    info.tagInfoPtr = tagInfoPtr;
    firstLinePtr = linePtr = TkTextIndexGetLine(indexPtr);
    TkTextIndexGetByteIndex(indexPtr); /* we need byte offset */
    changeToLineCount = 0;
    changeToLogicalLineCount = 0;
    changeToPixelInfo = treePtr->pixelInfoBuffer;
    SetLineHasChanged(sharedTextPtr, linePtr);

    if (tagInfoPtr && !TkTextTagSetContains(linePtr->parentPtr->tagonPtr, tagInfoPtr)) {
	unsigned i;

	/*
	 * Update the tag information of the B-Tree. Because the content of
	 * the node cannot be empty (it contains at least one newline char)
	 * we have also to add all new tags, not yet used inside this node,
	 * to the tagoff information.
	 */

	for (i = TkTextTagSetFindFirst(tagInfoPtr);
		i != TK_TEXT_TAG_SET_NPOS;
		i = TkTextTagSetFindNext(tagInfoPtr, i)) {
	    if (!TkTextTagSetTest(linePtr->parentPtr->tagonPtr, i)) {
		AddTagToNode(linePtr->parentPtr, sharedTextPtr->tagLookup[i], true);
	    }
	}
    }

    if (hyphenTagPtr) {
	int highestPriority = -1;
	TkText *textPtr = indexPtr->textPtr;

	for (tagPtr = hyphenTagPtr; tagPtr; tagPtr = tagPtr->nextPtr) {
	    if (!TkTextTagSetTest(linePtr->parentPtr->tagonPtr, tagPtr->index)) {
		AddTagToNode(linePtr->parentPtr, tagPtr, true);
	    }
	    if (tagPtr->elideString
		    && (int) tagPtr->priority > highestPriority
		    && (!tagPtr->textPtr || tagPtr->textPtr == textPtr)) {
		highestPriority = (hyphenElideTagPtr = tagPtr)->priority;
	    }
	}
    }

    DEBUG(indexPtr->discardConsistencyCheck = true);

    /*
     * Chop the string up into lines and create a new segment for each line,
     * plus a new line for the leftovers from the previous line.
     */

    while (*string) {
	bool isNewline = false;
	const char *strEnd = NULL;
	const char *s;

	for (s = string; !strEnd; ++s) {
	    switch (UCHAR(*s)) {
	    case 0x00:
		/* nul */
	    	strEnd = s;
		break;
	    case 0x0a:
		/* line feed */
		strEnd = s + 1;
		isNewline = true;
		break;
	    case 0xc2:
		if (UCHAR(s[1]) == 0xad) {
		    /* soft hyphen (U+002D) */
		    strEnd = s;
		    hyphenRules = 0;
		}
	    	break;
	    case 0xff:
	    	/*
		 * Hyphen support (0xff is not allowed in UTF-8 strings, it's a private flag
		 * denoting a soft hyphen, see ParseHyphens [tkText.c]).
		 */

		strEnd = s;

		switch (*++s) {
		case '-': hyphenRules = 0; break;
		case '+': hyphenRules = TK_TEXT_HYPHEN_MASK; break;
		default:  hyphenRules = UCHAR(*s); break;
		}
		break;
	    }
	}

	chunkSize = strEnd - string;

	if (chunkSize == 0) {
	    TkTextTag *tagPtr;

	    prevPtr = SplitSeg(indexPtr, NULL);
	    segPtr = MakeHyphen();
	    segPtr->body.hyphen.rules = hyphenRules;
	    LinkSegment(linePtr, prevPtr, segPtr);
	    SplitSection(segPtr->sectionPtr);
	    TkBTreeIncrEpoch(tree);
	    if (hyphenTagInfoPtr) {
		assert(firstSegPtr);
		TkTextTagSetIncrRefCount(segPtr->tagInfoPtr = hyphenTagInfoPtr);
	    } else {
		if (tagInfoPtr) {
		    assert(tagInfoPtr == info.tagInfoPtr);
		    TkTextTagSetIncrRefCount(segPtr->tagInfoPtr = tagInfoPtr);
		    if (!firstSegPtr) {
			firstSegPtr = segPtr;
		    }
		} else {
		    assert(!firstSegPtr);
		    assert(!info.tagInfoPtr);
		    tagInfoPtr = segPtr->tagInfoPtr = MakeTagInfo(indexPtr->textPtr, segPtr);
		    info.tagInfoPtr = tagInfoPtr;
		}
		for (tagPtr = hyphenTagPtr; tagPtr; tagPtr = tagPtr->nextPtr) {
		    segPtr->tagInfoPtr = TagSetAdd(segPtr->tagInfoPtr, tagPtr);
		}
		hyphenTagInfoPtr = segPtr->tagInfoPtr;
	    }
	    info.offset = -1;
	    prevPtr = segPtr;
	    split = false;
	    size += segPtr->size;
	} else {
	    size += chunkSize;

	    if (split) {
		info.increase = chunkSize;
		info.forceSplit = isNewline;
		prevPtr = SplitSeg(indexPtr, &info);
	    }
	    if (info.offset >= 0) {
		/*
		 * Fill increased/decreased char segment.
		 */
		segPtr = prevPtr;
		assert(segPtr->size >= (int) (info.offset + chunkSize));
		memcpy(segPtr->body.chars + info.offset, string, chunkSize);
		segPtr->sectionPtr->size += chunkSize;
		linePtr->size += chunkSize;
		assert(!tagInfoPtr || TkTextTagSetIsEqual(tagInfoPtr, segPtr->tagInfoPtr));
		tagInfoPtr = segPtr->tagInfoPtr;
	    } else {
		/*
		 * Insert new segment.
		 */

		segPtr = MakeCharSeg(NULL, tagInfoPtr, chunkSize, string, chunkSize);
		LinkSegment(linePtr, prevPtr, segPtr);
		SplitSection(segPtr->sectionPtr);
		TkBTreeIncrEpoch(tree);
	    }
	    prevPtr = segPtr;

	    assert(!firstSegPtr || tagInfoPtr);

	    if (!firstSegPtr) {
		firstSegPtr = segPtr;

		if (!tagInfoPtr) {
		    if (segPtr->tagInfoPtr) {
			tagInfoPtr = segPtr->tagInfoPtr;
		    } else {
			tagInfoPtr = MakeTagInfo(indexPtr->textPtr, segPtr);
		    }
		    info.tagInfoPtr = tagInfoPtr;
		}
	    }

	    if (!segPtr->tagInfoPtr) {
		TkTextTagSetIncrRefCount(segPtr->tagInfoPtr = tagInfoPtr);
	    } else {
		assert(TkTextTagSetIsEqual(tagInfoPtr, segPtr->tagInfoPtr));
	    }
	}

	assert(prevPtr);
	lastSegPtr = segPtr;
	string = strEnd + (chunkSize == 0 ? 2 : 0);
	TkTextIndexAddToByteIndex(indexPtr, MAX(chunkSize, 1u));

	if (!isNewline) {
	    continue;
	}

	/*
	 * Update line tag information.
	 */

	if (changeToLineCount == 0
		&& (hyphenTagInfoPtr
		    || (tagInfoPtr && linePtr->tagonPtr != tagInfoPtr)
		    || linePtr->tagoffPtr != emptyTagInfoPtr)) {
	    /*
	     * In this case we have to recompute the line tag information, because
	     * the line will be split before segPtr->nextPtr.
	     */
	    RecomputeLineTagInfo(linePtr, segPtr->nextPtr, sharedTextPtr);
	}

	assert(segPtr->nextPtr);

	split = info.splitted;
	info.splitted = false;
	info.offset = -1;

	/*
	 * This chunk ended with a newline, so create a new text line and move
	 * the remainder of the old line to it.
	 */

	if (changeToLineCount == 0) {
	    memset(changeToPixelInfo, 0, sizeof(changeToPixelInfo[0])*treePtr->numPixelReferences);
	}

	newLinePtr = InsertNewLine(sharedTextPtr, linePtr->parentPtr, linePtr, segPtr->nextPtr);
	AddPixelCount(treePtr, newLinePtr, linePtr, changeToPixelInfo);
	if (hyphenTagInfoPtr) {
	    assert(TkTextTagSetContains(hyphenTagInfoPtr, tagInfoPtr));
	    assert(linePtr->tagoffPtr == emptyTagInfoPtr);
	    TagSetAssign(&newLinePtr->tagonPtr, hyphenTagInfoPtr);
	    TagSetAssign(&newLinePtr->tagoffPtr, hyphenTagInfoPtr);
	    newLinePtr->tagoffPtr = TagSetRemove(newLinePtr->tagoffPtr, tagInfoPtr, sharedTextPtr);
	} else if (tagInfoPtr) {
	    TagSetAssign(&newLinePtr->tagonPtr, tagInfoPtr);
	}
	TkTextIndexSetByteIndex2(indexPtr, newLinePtr, 0);
	prevPtr = NULL;
	linePtr = newLinePtr;
	changeToLineCount += 1;
	changeToLogicalLineCount += linePtr->logicalLine;
    }

    /*
     * Update line tag information of last line.
     */

    assert(tagInfoPtr || hyphenTagInfoPtr);

    if (changeToLineCount == 0) {
	if (hyphenTagInfoPtr) {
	    assert(TkTextTagSetContains(hyphenTagInfoPtr, tagInfoPtr));
	    linePtr->tagoffPtr = TagSetJoinNonIntersection(
		    linePtr->tagoffPtr, linePtr->tagonPtr, hyphenTagInfoPtr, sharedTextPtr);
	    linePtr->tagonPtr = TkTextTagSetJoin(linePtr->tagonPtr, hyphenTagInfoPtr);
	    myTagInfoPtr = hyphenTagInfoPtr;
	} else if (linePtr->tagonPtr != tagInfoPtr || linePtr->tagoffPtr != emptyTagInfoPtr) {
	    linePtr->tagoffPtr = TagSetJoinNonIntersection(
		    linePtr->tagoffPtr, linePtr->tagonPtr, tagInfoPtr, sharedTextPtr);
	    linePtr->tagonPtr = TkTextTagSetJoin(linePtr->tagonPtr, tagInfoPtr);
	}
    } else {
	SetLineHasChanged(sharedTextPtr, linePtr);
	RecomputeLineTagInfo(linePtr, NULL, sharedTextPtr);
    }

    myTagInfoPtr = hyphenTagInfoPtr ? hyphenTagInfoPtr : tagInfoPtr;

    if (myTagInfoPtr) {
	Node *nodePtr = linePtr->parentPtr;

	if (nodePtr->tagonPtr != emptyTagInfoPtr) {
	    unsigned i;

	    /*
	     * Update the tag information of the B-Tree. Any tag in tagon of this
	     * node, which is not contained in myTagInfoPtr, has to be added to the
	     * tagoff information of this node.
	     */

#if 1 /* This is much faster with integer sets. */

	    TkTextTagSet *newTagonPtr = nodePtr->tagonPtr;

	    TkTextTagSetIncrRefCount(newTagonPtr);
	    newTagonPtr = TkTextTagSetRemove(newTagonPtr, nodePtr->tagoffPtr);

	    for (i = TkTextTagSetFindFirst(newTagonPtr);
		    i != TK_TEXT_TAG_SET_NPOS;
		    i = TkTextTagSetFindNext(newTagonPtr, i)) {
		if (!TkTextTagSetTest(myTagInfoPtr, i)) {
		    AddTagToNode(nodePtr, sharedTextPtr->tagLookup[i], true);
		}
	    }

	    TkTextTagSetDecrRefCount(newTagonPtr);

#else /* In general very slow with integer sets. */

	    for (i = TkTextTagSetFindFirst(nodePtr->tagonPtr);
		    i != TK_TEXT_TAG_SET_NPOS;
		    i = TkTextTagSetFindNext(nodePtr->tagonPtr, i)) {
		if (!TkTextTagSetTest(myTagInfoPtr, i)) {
		    AddTagToNode(nodePtr, sharedTextPtr->tagLookup[i], true);
		}
	    }

#endif
	}
    }

    if (undoInfo) {
	MakeUndoIndex(sharedTextPtr, indexPtr, &undoToken->endIndex, GRAVITY_LEFT);
    }

    /*
     * Increment the line and pixel counts in all the parent nodes of the
     * insertion point, then rebalance the tree if necessary.
     */

	/* MSVC cannot implicitly convert unsigned to signed. */
    SubtractPixelCount2(treePtr, linePtr->parentPtr, -((int) changeToLineCount),
	    -((int) changeToLogicalLineCount), 0, -((int) size), changeToPixelInfo);

    if ((linePtr->parentPtr->numChildren += changeToLineCount) > MAX_CHILDREN) {
	Rebalance(treePtr, linePtr->parentPtr);
    }

    /*
     * This line now needs to have its height recalculated. This has to be done after Rebalance.
     */

    TkTextInvalidateLineMetrics(sharedTextPtr, NULL, firstLinePtr,
	    changeToLineCount, TK_TEXT_INVALIDATE_INSERT);

    /*
     * Next step: update elision states if needed.
     */

    if (tagInfoPtr
	    && tagInfoPtr != emptyTagInfoPtr
	    && TkTextTagSetIntersectsBits(tagInfoPtr, sharedTextPtr->elisionTags)) {
	int highestPriority = -1;
	TkTextTag *tagPtr = NULL;
	TkText *textPtr = indexPtr->textPtr;
	unsigned i = TkTextTagSetFindFirst(tagInfoPtr);

	/*
	 * We have to update the elision info, but only for the tag with the highest
	 * elide priority. This has to be done after TkTextInvalidateLineMetrics.
	 */

	for ( ; i != TK_TEXT_TAG_SET_NPOS; i = TkTextTagSetFindNext(tagInfoPtr, i)) {
	    TkTextTag *tPtr = sharedTextPtr->tagLookup[i];

	    assert(tPtr);
	    assert(!tPtr->isDisabled);

	    if (tPtr->elideString
		    && (int) tPtr->priority > highestPriority
		    && (!tPtr->textPtr || tPtr->textPtr == textPtr)) {
		highestPriority = (tagPtr = tPtr)->priority;
	    }
	}

	if (tagPtr) {
	    firstSegPtr->protectionFlag = true;
	    lastSegPtr->protectionFlag = true;

	    UpdateElideInfo(sharedTextPtr, tagPtr, &firstSegPtr, &lastSegPtr, ELISION_HAS_BEEN_ADDED);

	    if (!hyphenElideTagPtr) {
		CleanupSplitPoint(firstSegPtr, sharedTextPtr);
		if (firstSegPtr != lastSegPtr) {
		    CleanupSplitPoint(lastSegPtr, sharedTextPtr);
		}
	    }

	    if (hyphenElideTagPtr == tagPtr) {
		hyphenElideTagPtr = NULL;
	    }
	}
    }

    if (hyphenElideTagPtr) {
	firstSegPtr->protectionFlag = true;
	lastSegPtr->protectionFlag = true;

	UpdateElideInfo(sharedTextPtr, hyphenElideTagPtr, &firstSegPtr, &lastSegPtr,
		ELISION_HAS_BEEN_ADDED);

	CleanupSplitPoint(firstSegPtr, sharedTextPtr);
	if (firstSegPtr != lastSegPtr) {
	    CleanupSplitPoint(lastSegPtr, sharedTextPtr);
	}
    }

    TkTextIndexSetEpoch(indexPtr, TkBTreeIncrEpoch(tree));

    TK_BTREE_DEBUG(TkBTreeCheck(indexPtr->tree));
}

/*
 *----------------------------------------------------------------------
 *
 * MakeUndoIndex --
 *
 *	Find undo/redo index of given segment. We prefer a predecessing
 *	mark segment at the same byte index, because such a mark is stable
 *	enough to work as a predecessor segment (e.g. for insertion),
 *	but alternatively, if no predecessing mark segments exists, we
 *	will store the line index, byte index, and possible offset inside
 *	a chain of (splitted) char segments. The gravity is specifiying
 *	the direction where we are searching for a mark, either at left
 *	side (for a starting index), or at right side (for an ending index).
 *
 * Results:
 *	'indexPtr' will be filled appropriately.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
MakeUndoIndex(
    const TkSharedText *sharedTextPtr,
    const TkTextIndex *indexPtr,	/* Convert this index. */
    TkTextUndoIndex *undoIndexPtr,	/* Pointer to resulting index. */
    int gravity)			/* +1 = right gravity, -1 = left gravity */
{
    TkTextSegment *segPtr;

    assert(indexPtr);
    assert(gravity == GRAVITY_LEFT || gravity == GRAVITY_RIGHT);

    /*
     * At first, try to find a neighboring mark segment at the same byte
     * index, but we cannot use the special marks "insert" and "current",
     * and we cannot not use private marks.
     */

    if (sharedTextPtr->steadyMarks
	    && (segPtr = TkTextIndexGetSegment(indexPtr))
	    && segPtr->typePtr->group == SEG_GROUP_MARK) {
	TkTextSegment *searchPtr = (gravity == GRAVITY_LEFT) ? segPtr->prevPtr : segPtr->nextPtr;

	while (searchPtr && TkTextIsSpecialOrPrivateMark(searchPtr)) {
	    searchPtr = (gravity == GRAVITY_LEFT) ? searchPtr->prevPtr : searchPtr->nextPtr;
	}

	if (searchPtr && TkTextIsStableMark(searchPtr)) {
	    undoIndexPtr->u.markPtr = searchPtr;
	    undoIndexPtr->lineIndex = -1;
	    return;
	}
    }

    undoIndexPtr->lineIndex = TkTextIndexGetLineNumber(indexPtr, NULL);
    undoIndexPtr->u.byteIndex = TkTextIndexGetByteIndex(indexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeMakeUndoIndex --
 *
 *	Find undo/redo index of given segment. We prefer a predecessing
 *	mark segment at the same byte index, because such a mark is stable
 *	enough to work as a predecessor segment (e.g. for insertion),
 *	but alternatively, if no predecessing mark segments exists, we
 *	will store the line index, byte index, and possible offset inside
 *	a chain of (splitted) char segments. The search for the mark segment
 *	will be done at the left side of the specified segment.
 *
 * Results:
 *	'indexPtr' will be filled appropriately.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkBTreeMakeUndoIndex(
    const TkSharedText *sharedTextPtr,
    TkTextSegment *segPtr,	/* Find index of this segment. */
    TkTextUndoIndex *indexPtr)	/* Pointer to resulting index. */
{
    TkTextIndex index;

    assert(segPtr);
    assert(segPtr->typePtr);    /* expired? */
    assert(segPtr->sectionPtr); /* linked? */
    assert(segPtr->typePtr != &tkTextCharType);

    TkTextIndexClear2(&index, NULL, sharedTextPtr->tree);
    TkTextIndexSetSegment(&index, segPtr);
    MakeUndoIndex(sharedTextPtr, &index, indexPtr, GRAVITY_LEFT);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeUndoIndexToIndex --
 *
 *	Convert an undo/redo index to a normal index.
 *
 * Results:
 *	'dstPtr' will be filled appropriately.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkBTreeUndoIndexToIndex(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoIndex *srcPtr,
    TkTextIndex *dstPtr)
{
    TkTextIndexClear2(dstPtr, NULL, sharedTextPtr->tree);

    if (srcPtr->lineIndex == -1) {
	TkTextIndexSetSegment(dstPtr, srcPtr->u.markPtr);
    } else {
	TkTextLine *linePtr = TkBTreeFindLine(sharedTextPtr->tree, NULL, srcPtr->lineIndex);
	assert(linePtr);
	TkTextIndexSetByteIndex2(dstPtr, linePtr, srcPtr->u.byteIndex);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UndoIndexIsEqual --
 *
 *	Test whether both indices are equal. Note that this test
 *	may return false even if both indices are referring the
 *	same position.
 *
 * Results:
 *	Return whether both indices are (probably) equal.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static bool
UndoIndexIsEqual(
    const TkTextUndoIndex *indexPtr1,
    const TkTextUndoIndex *indexPtr2)
{
    if (indexPtr1->lineIndex == -1) {
	return indexPtr2->u.markPtr && indexPtr1->u.markPtr == indexPtr2->u.markPtr;
    }

    if (indexPtr2->lineIndex == -1) {
	return indexPtr1->u.markPtr && indexPtr1->u.markPtr == indexPtr2->u.markPtr;
    }

    return indexPtr1->lineIndex == indexPtr2->lineIndex
	    && indexPtr1->u.byteIndex == indexPtr2->u.byteIndex;
}

/*
 *----------------------------------------------------------------------
 *
 * ReInsertSegment --
 *
 *	Re-insert a previously removed segment at the given index.
 *	This function is not handling the special cases when a
 *	char segment will be inserted (join with neighbors, handling
 *	of newline char, updating the line tag information), the caller
 *	is responsible for this.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A segment will be inserted into a segment chain.
 *
 *----------------------------------------------------------------------
 */

static void
ReInsertSegment(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoIndex *indexPtr,
    TkTextSegment *segPtr,
    bool updateNode)
{
    TkTextSegment *prevPtr;
    TkTextLine *linePtr;

    assert(sharedTextPtr);
    assert(indexPtr);
    assert(segPtr);
    assert(!TkTextIsSpecialOrPrivateMark(segPtr));

    if (indexPtr->lineIndex == -1) {
	prevPtr = indexPtr->u.markPtr;
	linePtr = prevPtr->sectionPtr->linePtr;

	if (updateNode) {
	    TkTextIndex index;

	    linePtr = TkBTreeFindLine(sharedTextPtr->tree, NULL, indexPtr->lineIndex);
	    TkTextIndexClear2(&index, NULL, sharedTextPtr->tree);
	    TkTextIndexSetByteIndex2(&index, linePtr, indexPtr->u.byteIndex);
	    TkBTreeLinkSegment(sharedTextPtr, segPtr, &index);
	    return;
	}
    } else {
	TkTextIndex index;

	assert(indexPtr->lineIndex >= 0);
	assert(indexPtr->u.byteIndex >= 0);

	linePtr = TkBTreeFindLine(sharedTextPtr->tree, NULL, indexPtr->lineIndex);
	TkTextIndexClear2(&index, NULL, sharedTextPtr->tree);
	TkTextIndexSetByteIndex2(&index, linePtr, indexPtr->u.byteIndex);

	if (updateNode) {
	    TkBTreeLinkSegment(sharedTextPtr, segPtr, &index);
	    return;
	}

	prevPtr = SplitSeg(&index, NULL);
    }

    LinkSegment(linePtr, prevPtr, segPtr);
    SplitSection(segPtr->sectionPtr);
    TkBTreeIncrEpoch(sharedTextPtr->tree);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeReInsertSegment --
 *
 *	Re-insert a previously removed segment at the given index.
 *	This function is not handling the special cases when a
 *	char segment will be inserted (join with neighbors, handling
 *	of newline char, updating the line tag information), the caller
 *	is responsible for this.
 *
 *	This function is updating the B-Tree.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A segment will be inserted into a segment chain.
 *
 *----------------------------------------------------------------------
 */

void
TkBTreeReInsertSegment(
    const TkSharedText *sharedTextPtr,
    const TkTextUndoIndex *indexPtr,
    TkTextSegment *segPtr)
{
    ReInsertSegment(sharedTextPtr, indexPtr, segPtr, true);
}

/*
 *----------------------------------------------------------------------
 *
 * LinkMark --
 *
 *	This function adds a mark segment to a B-tree at given location.
 *	It takes into account some rules about positions of marks and
 *	switches.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	'succPtr' will be linked into its tree.
 *
 *----------------------------------------------------------------------
 */

static void
LinkMark(
    const TkSharedText *sharedTextPtr,
    TkTextLine *linePtr,
    TkTextSegment *prevPtr,
    TkTextSegment *segPtr)
{
    assert(segPtr->typePtr->group == SEG_GROUP_MARK);

    /*
     * Start markers will be the left most mark.
     * End markers will be the right most mark.
     */

    if (segPtr->startEndMarkFlag) {
	if (segPtr->typePtr == &tkTextLeftMarkType) {
	    /* This is a start marker. */
	    while (prevPtr
		    && prevPtr->typePtr->group == SEG_GROUP_MARK
		    && !prevPtr->startEndMarkFlag) {
		prevPtr = prevPtr->prevPtr;
	    }
	} else {
	    /* This is an end marker. */
	    if (!prevPtr
		    && linePtr->segPtr->typePtr->group == SEG_GROUP_MARK
		    && !linePtr->segPtr->startEndMarkFlag) {
		prevPtr = linePtr->segPtr;
	    }
	    if (prevPtr) {
		while (prevPtr->nextPtr
			&& prevPtr->nextPtr->typePtr->group == SEG_GROUP_MARK
			&& !prevPtr->nextPtr->startEndMarkFlag) {
		    prevPtr = prevPtr->nextPtr;
		}
	    }
	}
    } else {
	if (!prevPtr
		&& linePtr->segPtr->startEndMarkFlag
		&& linePtr->segPtr->typePtr == &tkTextLeftMarkType) {
	    prevPtr = linePtr->segPtr;
	}
	if (prevPtr) {
	    while (prevPtr->nextPtr
		    && prevPtr->nextPtr->startEndMarkFlag
		    && prevPtr->nextPtr->typePtr == &tkTextLeftMarkType) {
		prevPtr = prevPtr->nextPtr;
	    }
	}
	while (prevPtr
		&& prevPtr->startEndMarkFlag
		&& prevPtr->typePtr == &tkTextRightMarkType) {
	    prevPtr = prevPtr->prevPtr;
	}
    }

    /*
     * We have to ensure that a branch will not be followed by marks,
     * and a link will not be preceded by marks.
     */

    assert(!prevPtr || prevPtr->nextPtr);	/* mark cannot be last segment */
    assert(linePtr->segPtr);			/* dito */

    if (TkBTreeHaveElidedSegments(sharedTextPtr)) {
	if (prevPtr) {
	    if (prevPtr->typePtr == &tkTextBranchType) {
		prevPtr = prevPtr->prevPtr;
	    } else if (prevPtr->nextPtr->typePtr == &tkTextLinkType) {
		prevPtr = prevPtr->nextPtr;
	    }
	} else if (linePtr->segPtr->typePtr == &tkTextLinkType) {
	    prevPtr = linePtr->segPtr;
	}
    }

    LinkSegment(linePtr, prevPtr, segPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * LinkSwitch --
 *
 *	This function adds a new branch/link segment to a B-tree at given
 *	location. It takes into account that a branch will never be
 *	followed by marks, and a link will never by preceded by marks.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	'succPtr' will be linked into its tree.
 *
 *----------------------------------------------------------------------
 */

static void
LinkSwitch(
    TkTextLine *linePtr,	/* Pointer to line. */
    TkTextSegment *predPtr,	/* Pointer to segment within this line, can be NULL. */
    TkTextSegment *succPtr)	/* Link this segment after predPtr. */
{
    assert(predPtr || linePtr);
    assert(succPtr);
    assert(succPtr->typePtr->group == SEG_GROUP_BRANCH);

    /*
     * Note that the (temporary) protected segments are transparent.
     */

    if (succPtr->typePtr == &tkTextBranchType) {
	if (!predPtr && (linePtr->segPtr->typePtr->group & (SEG_GROUP_MARK|SEG_GROUP_PROTECT))) {
	    predPtr = linePtr->segPtr;
	}
	if (predPtr) {
	    while (predPtr->nextPtr
		    && (predPtr->nextPtr->typePtr->group & (SEG_GROUP_MARK|SEG_GROUP_PROTECT))) {
		predPtr = predPtr->nextPtr;
		assert(predPtr); /* mark cannot be last segment */
	    }
	}
    } else { /* if (succPtr->typePtr == &tkTextLinkType) */
	while (predPtr && (predPtr->typePtr->group & (SEG_GROUP_MARK|SEG_GROUP_PROTECT))) {
	    predPtr = predPtr->prevPtr;
	}
    }

    LinkSegment(linePtr, predPtr, succPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * LinkSegment --
 *
 *	This function adds a new segment to a B-tree at given location.
 *	Note that this function is not updating the tag information of
 *	the line.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	'succPtr' will be linked into its tree after 'predPtr', or
 *	at start of given line if 'predPtr' is NULL.
 *
 *----------------------------------------------------------------------
 */

static void
LinkSegment(
    TkTextLine *linePtr,	/* Pointer to line. */
    TkTextSegment *predPtr,	/* Pointer to segment within this line, can be NULL. */
    TkTextSegment *succPtr)	/* Link this segment after predPtr. */
{
    assert(predPtr || linePtr);
    assert(succPtr);
    assert(!succPtr->sectionPtr); /* unlinked? */

    if (predPtr) {
	if (predPtr->typePtr == &tkTextBranchType) {
	    succPtr->sectionPtr = predPtr->nextPtr->sectionPtr;
	    succPtr->sectionPtr->segPtr = succPtr;
	} else {
	    succPtr->sectionPtr = predPtr->sectionPtr;
	}
	succPtr->nextPtr = predPtr->nextPtr;
	succPtr->prevPtr = predPtr;
	predPtr->nextPtr = succPtr;
	if (linePtr->lastPtr == predPtr) {
	    linePtr->lastPtr = succPtr;
	}
    } else {
	assert(linePtr->segPtr);
	if (linePtr->segPtr->typePtr == &tkTextLinkType) {
	    TkTextSection *newSectionPtr;

	    newSectionPtr = malloc(sizeof(TkTextSection));
	    newSectionPtr->linePtr = linePtr;
	    newSectionPtr->segPtr = succPtr;
	    newSectionPtr->nextPtr = linePtr->segPtr->sectionPtr->nextPtr;
	    newSectionPtr->prevPtr = NULL;
	    newSectionPtr->size = 0;
	    newSectionPtr->length = 0;
	    linePtr->segPtr->sectionPtr->prevPtr = newSectionPtr;
	} else {
	    succPtr->sectionPtr = linePtr->segPtr->sectionPtr;
	    succPtr->sectionPtr->segPtr = succPtr;
	}
	succPtr->nextPtr = linePtr->segPtr;
	succPtr->prevPtr = NULL;
	linePtr->segPtr = succPtr;
    }
    if (succPtr->nextPtr) {
	succPtr->nextPtr->prevPtr = succPtr;
    }
    linePtr->size += succPtr->size;
    succPtr->sectionPtr->size += succPtr->size;
    succPtr->sectionPtr->length += 1;
    assert(succPtr->sectionPtr->length != 0); /* test for overflow */

}

/*
 *----------------------------------------------------------------------
 *
 * UnlinkSegmentAndCleanup --
 *
 *	This function removes a segment from a B-tree. Furthermore
 *	it will do a cleanup with the predecessing segment.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	'segPtr' will be unlinked from its tree, possibly a cleanup will
 *	be done.
 *
 *----------------------------------------------------------------------
 */

static void
UnlinkSegmentAndCleanup(
    const TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    TkTextSegment *segPtr)		/* Unlink this segment. */
{
    TkTextSegment *prevPtr;

    assert(segPtr);

    prevPtr = segPtr->prevPtr;
    UnlinkSegment(segPtr);

    if (prevPtr && prevPtr->typePtr == &tkTextCharType) {
	CleanupCharSegments(sharedTextPtr, prevPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UnlinkSegment --
 *
 *	This function removes a segment from a B-tree.
 *
 * Results:
 *	The predecessor of the unlinked segment.
 *
 * Side effects:
 *	'segPtr' will be unlinked from its tree.
 *
 *----------------------------------------------------------------------
 */

static void
FreeSection(
    TkTextSection *sectionPtr)
{
    assert(sectionPtr->linePtr);
    assert(!(sectionPtr->linePtr = NULL));
    free(sectionPtr);
    DEBUG_ALLOC(tkTextCountDestroySection++);
}

static TkTextSegment *
UnlinkSegment(
    TkTextSegment *segPtr)	/* Unlink this segment. */
{
    TkTextSegment *prevPtr = segPtr->prevPtr;

    if (prevPtr) {
	prevPtr->nextPtr = segPtr->nextPtr;
    } else {
	segPtr->sectionPtr->linePtr->segPtr = segPtr->nextPtr;
    }
    if (segPtr->nextPtr) {
	segPtr->nextPtr->prevPtr = prevPtr;
    }
    if (segPtr->sectionPtr->segPtr == segPtr) {
	segPtr->sectionPtr->segPtr = segPtr->nextPtr;
    }
    if (segPtr->sectionPtr->linePtr->lastPtr == segPtr) {
	segPtr->sectionPtr->linePtr->lastPtr = prevPtr;
    }
    segPtr->sectionPtr->linePtr->size -= segPtr->size;
    if (--segPtr->sectionPtr->length == 0) {
	/*
	 * This can happen in rare cases, e.g. the line is starting with a Branch.
	 * We have to free the unused section.
	 */
	FreeSection(segPtr->sectionPtr);
	segPtr->nextPtr->sectionPtr->prevPtr = NULL;
    } else {
	segPtr->sectionPtr->size -= segPtr->size;
    }
    segPtr->sectionPtr = NULL;
    return prevPtr;
}

/*
 *--------------------------------------------------------------
 *
 * ComputeSectionSize --
 *
 *	Count the sum of all sizes in current section starting at
 *	given section.
 *
 * Results:
 *	The return value is the sum.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static unsigned
ComputeSectionSize(
    const TkTextSegment *segPtr)	/* Start counting at this segment. */
{
    const TkTextSection *sectionPtr = segPtr->sectionPtr;
    unsigned size = 0;

    for ( ; segPtr && segPtr->sectionPtr == sectionPtr; segPtr = segPtr->nextPtr) {
	size += segPtr->size;
    }

    return size;
}

/*
 *--------------------------------------------------------------
 *
 * CountSegments --
 *
 *	Count the number of segments belonging to the given section.
 *
 * Results:
 *	The return value is the count.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static unsigned
CountSegments(
    const TkTextSection *sectionPtr)	/* Pointer to section of text segments. */
{
    const TkTextSegment *segPtr;
    unsigned count = 0;

    for (segPtr = sectionPtr->segPtr;
	    segPtr && segPtr->sectionPtr == sectionPtr;
	    segPtr = segPtr->nextPtr, ++count) {
	/* empty body */
    }

    return count;
}

/*
 *--------------------------------------------------------------
 *
 * SplitSection --
 *
 *	This function is called after new segments has been added to a
 *	section. It ensures that no more than MAX_TEXT_SEGS segments will
 *	belong to this section, by reducing the number of segments. If
 *	necessary a new section will be created.
 *
 *	It is guaranteed that a split operation will be performed in
 *	constant time.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The section referred to by sectionPtr may change, and also the
 *	the neighboring sections may change.
 *
 *--------------------------------------------------------------
 */

static void
SplitSection(
    TkTextSection *sectionPtr)	/* Pointer to section of text segments. */
{
    TkTextSegment *segPtr, *splitSegPtr;
    TkTextSection *newSectionPtr, *prevPtr, *nextPtr;
    int length;
    int lengthLHS, lengthRHS;
    int shiftLHS, shiftRHS;
    int capacityLHS, capacityRHS;

    assert(!sectionPtr->prevPtr || sectionPtr->prevPtr->length <= MAX_TEXT_SEGS);
    assert(!sectionPtr->nextPtr || sectionPtr->nextPtr->length <= MAX_TEXT_SEGS);

    if ((length = sectionPtr->length) <= NUM_TEXT_SEGS) {
	return;
    }

    /*
     * The correctness of this implementation depends on the fact that
     * a section can never contain more than MAX_TEXT_SEGS+NUM_TEXT_SEGS
     * segments.
     */
    assert(length <= MAX_TEXT_SEGS + NUM_TEXT_SEGS);

    segPtr = sectionPtr->nextPtr ? sectionPtr->nextPtr->segPtr->prevPtr : sectionPtr->linePtr->lastPtr;
    for (lengthLHS = length - 1; lengthLHS > NUM_TEXT_SEGS; --lengthLHS) {
	segPtr = segPtr->prevPtr;
    }
    splitSegPtr = segPtr;

    /*
     * We have to take into account that a branch segment must be
     * the last segment inside a section, and a link segment must
     * be the first segment inside a section.
     */

    prevPtr = sectionPtr->prevPtr;
    nextPtr = sectionPtr->nextPtr;

    if (prevPtr && IsBranchSection(prevPtr)) {
	prevPtr = NULL; /* we cannot shift to the left */
    }
    if (nextPtr && IsLinkSection(nextPtr)) {
	nextPtr = NULL; /* we cannot shift to the right */
    }

    lengthLHS = prevPtr ? prevPtr->length : 0;
    lengthRHS = nextPtr ? nextPtr->length : 0;

    capacityLHS = lengthLHS ? MAX(0, NUM_TEXT_SEGS - lengthLHS) : 0;
    capacityRHS = lengthRHS ? MAX(0, NUM_TEXT_SEGS - lengthRHS) : 0;

    /*
     * We have to consider two cases:
     *
     * =====================================================================
     * (capacityLHS + capacityRHS < length - MAX_TEXT_SEGS) OR
     * (lengthRHS == 0 AND capacityLHS < length - NUM_TEXT_SEGS):
     * =====================================================================
     *
     * 1. Shift as many segments as possible to the left segment (if
     *    exisiting), but the length of NUM_TEXT_SEGS should not be
     *    exceeded.
     *
     * 2. We have to insert a new section at the right. Shift segments into
     *    this new segment, until this section has NUM_TEXT_SEGS segments.
     *
     * =====================================================================
     * otherwise:
     * =====================================================================
     *
     * In this case this section will reduced while shifting to left and
     * right neighbors, so that each neighbor will not exceed NUM_TEXT_SEGS
     * segments with this operation.
     */

    if (capacityLHS + capacityRHS < length - MAX_TEXT_SEGS
	    || (lengthRHS == 0 && capacityLHS < length - NUM_TEXT_SEGS)) {
	if (capacityLHS) {
	    TkTextSegment *segPtr = sectionPtr->segPtr;
	    int i;

	    for (i = capacityLHS; i < capacityLHS; ++i) {
		sectionPtr->size -= segPtr->size;
		sectionPtr->length -= 1;
		sectionPtr->prevPtr->size += segPtr->size;
		sectionPtr->prevPtr->length += 1;
		assert(sectionPtr->prevPtr->length != 0); /* test for overflow */
		segPtr->sectionPtr = sectionPtr->prevPtr;
		segPtr = segPtr->nextPtr;
		splitSegPtr = splitSegPtr->nextPtr;
	    }
	    sectionPtr->segPtr = segPtr;
	}

	assert(splitSegPtr);
	assert(lengthRHS == 0 || length - capacityLHS >= MIN_TEXT_SEGS);

	newSectionPtr = malloc(sizeof(TkTextSection));
	newSectionPtr->linePtr = sectionPtr->linePtr;
	newSectionPtr->segPtr = splitSegPtr;
	newSectionPtr->nextPtr = sectionPtr->nextPtr;
	newSectionPtr->prevPtr = sectionPtr;
	newSectionPtr->size = 0;
	newSectionPtr->length = 0;
	if (sectionPtr->nextPtr) {
	    sectionPtr->nextPtr->prevPtr = newSectionPtr;
	}
	sectionPtr->nextPtr = newSectionPtr;
	DEBUG_ALLOC(tkTextCountNewSection++);

	for ( ; splitSegPtr && splitSegPtr->sectionPtr == sectionPtr;
		splitSegPtr = splitSegPtr->nextPtr) {
	    newSectionPtr->size += splitSegPtr->size;
	    newSectionPtr->length += 1;
	    assert(newSectionPtr->length != 0); /* test for overflow */
	    sectionPtr->size -= splitSegPtr->size;
	    sectionPtr->length -= 1;
	    splitSegPtr->sectionPtr = newSectionPtr;
	}
    } else {
	int exceed;

	shiftLHS = MIN(capacityLHS, MAX(0, length - NUM_TEXT_SEGS));
	shiftRHS = MIN(capacityRHS, length - NUM_TEXT_SEGS - shiftLHS);

	if (shiftLHS > 0) {
	    TkTextSegment *segPtr = sectionPtr->segPtr;

	    for ( ; shiftLHS > 0; --shiftLHS) {
		sectionPtr->size -= segPtr->size;
		sectionPtr->length -= 1;
		sectionPtr->prevPtr->size += segPtr->size;
		sectionPtr->prevPtr->length += 1;
		assert(sectionPtr->prevPtr->length != 0); /* test for overflow */
		segPtr->sectionPtr = sectionPtr->prevPtr;
		segPtr = segPtr->nextPtr;
	    }
	    sectionPtr->segPtr = segPtr;
	}

	if (shiftRHS > 0) {
	    /*
	     * Reduce the split until it fits the capacity of the neighbor.
	     */

	    exceed = length - NUM_TEXT_SEGS - shiftLHS - shiftRHS;
	    for ( ; exceed > 0; splitSegPtr = splitSegPtr->nextPtr, --exceed) {
		/* empty loop body */
	    }

	    assert(splitSegPtr);
	    sectionPtr->nextPtr->segPtr = splitSegPtr;
	    while (splitSegPtr && splitSegPtr->sectionPtr == sectionPtr) {
		sectionPtr->size -= splitSegPtr->size;
		sectionPtr->length -= 1;
		sectionPtr->nextPtr->size += splitSegPtr->size;
		sectionPtr->nextPtr->length += 1;
		assert(sectionPtr->nextPtr->length != 0); /* test for overflow */
		splitSegPtr->sectionPtr = sectionPtr->nextPtr;
		splitSegPtr = splitSegPtr->nextPtr;
	    }
	}
    }
}

/*
 *--------------------------------------------------------------
 *
 * JoinSections --
 *
 *	This function is called after segments has been removed from a
 *	section. It ensures that either at least MIN_TEXT_SEGS will belong
 *	to this section, or that this section will be removed. Of course
 *	this must be ensured only if this section is not the rightmost
 *	section of this line.
 *
 *	It is guaranteed that a join operation will be constant in constant
 *	time.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The section referred to by sectionPtr may change, and also the
 *	the neighboring sections may change. The section referred to by
 *	sectionPtr will be destroyed if not needed anymore.
 *
 *--------------------------------------------------------------
 */

static void
JoinSections(
    TkTextSection *sectionPtr)	/* Pointer to section of text segments. */
{
    TkTextSegment *segPtr;
    bool isBranchSegment, isLinkSegment;
    int length;

    assert(!sectionPtr->prevPtr || sectionPtr->prevPtr->length <= MAX_TEXT_SEGS);
    assert(!sectionPtr->nextPtr || sectionPtr->nextPtr->length <= MAX_TEXT_SEGS);

    length = sectionPtr->length;

    if (length == 0) {
	/*
	 * This section is empty, so remove it. Note that this
	 * cannot happen if the line contains only one section.
	 */
	assert(sectionPtr->prevPtr);
	assert(sectionPtr->length == 0);
	sectionPtr->prevPtr->nextPtr = sectionPtr->nextPtr;
	if (sectionPtr->nextPtr) {
	    sectionPtr->nextPtr->prevPtr = sectionPtr->prevPtr;
	}
	FreeSection(sectionPtr);
	return;
    }

    isBranchSegment = IsBranchSection(sectionPtr);
    isLinkSegment = IsLinkSection(sectionPtr);

    if (sectionPtr->nextPtr
	    && !isBranchSegment
	    && !IsLinkSection(sectionPtr->nextPtr)
	    && length < MIN_TEXT_SEGS) {
	/*
	 * This section does not end with a Branch, we have a right
	 * neighbor, and the length of this section has undershot
	 * MIN_TEXT_SEGS segments. We have to remove this section,
	 * while shifting the content to the neighbors.
	 */

	int lengthRHS = 0, capacity, shift;

	if (sectionPtr->prevPtr && !isLinkSegment && !IsBranchSection(sectionPtr->prevPtr)) {
	    int lengthLHS = sectionPtr->prevPtr->length;
	    assert(lengthLHS > 0);

	    /*
	     * Move segments to left neighbor, but regard that the
	     * neighbor will not exceed NUM_TEXT_SEGS segments with
	     * this operation.
	     */

	    if ((capacity = MAX(0, NUM_TEXT_SEGS - lengthLHS)) > 0) {
		shift = MIN(capacity, length);
		segPtr = sectionPtr->segPtr;
		for ( ; lengthLHS < NUM_TEXT_SEGS && 0 < shift; --shift) {
		    length -= 1;
		    sectionPtr->prevPtr->size += segPtr->size;
		    sectionPtr->prevPtr->length += 1;
		    assert(sectionPtr->prevPtr->length != 0); /* test for overflow */
		    sectionPtr->size -= segPtr->size;
		    sectionPtr->length -= 1;
		    segPtr->sectionPtr = sectionPtr->prevPtr;
		    segPtr = segPtr->nextPtr;
		}
		sectionPtr->segPtr = segPtr;
	    }
	}

	if (length > 0) {
	    lengthRHS = sectionPtr->nextPtr->length;
	    assert(lengthRHS > 0);

	    /*
	     * Move the remaining segments to right neighbor. Here
	     * it may happen that MAX_TEXT_SEGS will be exceeded.
	     */

	    sectionPtr->nextPtr->segPtr = sectionPtr->segPtr;
	    sectionPtr->nextPtr->size += sectionPtr->size;
	    sectionPtr->nextPtr->length += sectionPtr->length;
	    assert(sectionPtr->nextPtr->length >= sectionPtr->length); /* test for overflow */
	    for (segPtr = sectionPtr->segPtr;
		    segPtr && segPtr->sectionPtr == sectionPtr;
		    segPtr = segPtr->nextPtr) {
		segPtr->sectionPtr = sectionPtr->nextPtr;
	    }
	}

	if (sectionPtr->prevPtr) {
	    sectionPtr->prevPtr->nextPtr = sectionPtr->nextPtr;
	}
	sectionPtr->nextPtr->prevPtr = sectionPtr->prevPtr;
	FreeSection(sectionPtr);

	if (lengthRHS + length > MAX_TEXT_SEGS) {
	    /*
	     * Right shift operation has exceeded MAX_TEXT_SEGS, so we
	     * have to split the right neighbor.
	     */
	    SplitSection(sectionPtr->nextPtr);
	}
    } else if (length > NUM_TEXT_SEGS) {
	int lengthRHS, shift;

	/*
	 * Move some segments to the neighbors for a better dstribution,
	 * but do not exceed NUM_TEXT_SEGS segments of the neighbors
	 * with this operation. Also do not undershot NUM_TEXT_SEGS of
	 * current section.
	 */

	if (sectionPtr->prevPtr
		&& !isLinkSegment
	    	&& !IsBranchSection(sectionPtr->prevPtr)) {
	    int lengthLHS = sectionPtr->prevPtr->length;

	    if (lengthLHS < NUM_TEXT_SEGS) {
		shift = MIN(length - NUM_TEXT_SEGS, NUM_TEXT_SEGS - lengthLHS);
		assert(shift < length);
		if (shift > 0) {
		    segPtr = sectionPtr->segPtr;
		    for ( ; shift > 0; --shift, --length) {
			sectionPtr->prevPtr->size += segPtr->size;
			sectionPtr->prevPtr->length += 1;
			assert(sectionPtr->prevPtr->length != 0); /* test for overflow */
			sectionPtr->size -= segPtr->size;
			sectionPtr->length -= 1;
			segPtr->sectionPtr = sectionPtr->prevPtr;
			segPtr = segPtr->nextPtr;
		    }
		    sectionPtr->segPtr = segPtr;
		}
	    }
	}

	if (sectionPtr->nextPtr && !isBranchSegment && !IsLinkSection(sectionPtr->nextPtr)) {
	    lengthRHS = sectionPtr->nextPtr->length;

	    if (lengthRHS < NUM_TEXT_SEGS) {
		shift = MIN(length - NUM_TEXT_SEGS, NUM_TEXT_SEGS - lengthRHS);
		assert(shift < length);
		if (shift > 0) {
		    int i;
		    segPtr = sectionPtr->segPtr;
		    for (i = length - shift; i > 0; --i) {
			segPtr = segPtr->nextPtr;
		    }
		    sectionPtr->nextPtr->segPtr = segPtr;
		    for ( ; shift > 0; --shift) {
			sectionPtr->nextPtr->size += segPtr->size;
			sectionPtr->nextPtr->length += 1;
			assert(sectionPtr->nextPtr->length != 0); /* test for overflow */
			sectionPtr->size -= segPtr->size;
			sectionPtr->length -= 1;
			segPtr->sectionPtr = sectionPtr->nextPtr;
			segPtr = segPtr->nextPtr;
		    }
		    assert(segPtr->sectionPtr != sectionPtr);
		}
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RebuildSections --
 *
 *	The line has massively changed, so we have to rebuild all the sections
 *	in this line. This function will also recompute the total char size in
 *	this line. Furthermore superfluous sections will be freed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Possibly new sections will be allocated, some sections may be freed,
 *	many sections will be modified, and the char size of the line will be
 *	modified.
 *
 *----------------------------------------------------------------------
 */

static void
PropagateChangeOfNumBranches(
    Node *nodePtr,
    int changeToNumBranches)
{
    for ( ; nodePtr; nodePtr = nodePtr->parentPtr) {
	nodePtr->numBranches += changeToNumBranches;
	assert((int) nodePtr->numBranches >= 0);
    }
}

static void
RebuildSections(
    TkSharedText *sharedTextPtr,
    TkTextLine *linePtr,		/* Pointer to existing line */
    bool propagateChangeOfNumBranches)	/* Should we propagate a change in number of branches
    					 * to B-Tree? */
{
    TkTextSection *sectionPtr, *prevSectionPtr;
    TkTextSegment *segPtr;
    unsigned length;
    int changeToNumBranches;

    prevSectionPtr = NULL;
    sectionPtr = linePtr->segPtr->sectionPtr;

    assert(!sectionPtr || !sectionPtr->prevPtr);
    assert(!linePtr->lastPtr->nextPtr);
    assert(!propagateChangeOfNumBranches
	    || TkBTreeGetRoot(sharedTextPtr->tree)->numBranches >= linePtr->numBranches);

    changeToNumBranches = -((int) linePtr->numBranches);
    linePtr->numBranches = 0;
    linePtr->numLinks = 0;
    linePtr->size = 0;

    for (segPtr = linePtr->segPtr; segPtr; ) {
	if (!sectionPtr) {
	    TkTextSection *newSectionPtr;

	    newSectionPtr = memset(malloc(sizeof(TkTextSection)), 0, sizeof(TkTextSection));
	    if (prevSectionPtr) {
		prevSectionPtr->nextPtr = newSectionPtr;
	    } else {
		linePtr->segPtr->sectionPtr = newSectionPtr;
	    }
	    newSectionPtr->prevPtr = prevSectionPtr;
	    sectionPtr = newSectionPtr;
	    DEBUG_ALLOC(tkTextCountNewSection++);
	} else {
	    sectionPtr->size = 0;
	    sectionPtr->length = 0;
	}

	sectionPtr->segPtr = segPtr;
	sectionPtr->linePtr = linePtr;

	if (segPtr->typePtr == &tkTextLinkType) {
	    linePtr->numLinks += 1;
	}

	/*
	 * It is important to consider that a Branch is always at the end
	 * of a section, and a Link is always at the start of a section.
	 */

	for (length = 0; length < NUM_TEXT_SEGS; ++length) {
	    TkTextSegment *prevPtr = segPtr;

	    sectionPtr->size += segPtr->size;
	    sectionPtr->length += 1;
	    assert(sectionPtr->length != 0); /* test for overflow */
	    segPtr->sectionPtr = sectionPtr;
	    segPtr = segPtr->nextPtr;

	    if (prevPtr->typePtr == &tkTextBranchType) {
		linePtr->numBranches += 1;
		break;
	    }
	    if (!segPtr || segPtr->typePtr == &tkTextLinkType) {
		break;
	    }
	}

	linePtr->size += sectionPtr->size;
	prevSectionPtr = sectionPtr;
	sectionPtr = sectionPtr->nextPtr;
    }

    if (propagateChangeOfNumBranches && (changeToNumBranches += linePtr->numBranches) != 0) {
	PropagateChangeOfNumBranches(linePtr->parentPtr, changeToNumBranches);
    }

    if (sectionPtr) {
	/*
	 * Free unused sections.
	 */
	if (sectionPtr->prevPtr) {
	    sectionPtr->prevPtr->nextPtr = NULL;
	}
	FreeSections(sectionPtr);
    }

    assert(CheckSections(linePtr));
}

/*
 *--------------------------------------------------------------
 *
 * FreeSections --
 *
 *	This function is freeing all sections belonging to the text line
 *	starting at sectionPtr.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All the section structures in this line starting at sectionPtr will
 *	be freed.
 *
 *--------------------------------------------------------------
 */

static void
FreeSections(
    TkTextSection *sectionPtr)	/* Pointer to first section to be freed. */
{
    TkTextSection *nextPtr;

    while (sectionPtr) {
	assert(sectionPtr->linePtr); /* otherwise already freed */
	nextPtr = sectionPtr->nextPtr;
	FreeSection(sectionPtr);
	sectionPtr = nextPtr;
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkBTreeFreeSegment --
 *
 *	Decrement reference counter and free the segment if not
 *	referenced anymore.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The reference counter will be decrement, and if zero,
 *	then the storage for this segment will be freed.
 *
 *--------------------------------------------------------------
 */

void
TkBTreeFreeSegment(
    TkTextSegment *segPtr)
{
    assert(segPtr->refCount > 0);

    if (--segPtr->refCount == 0) {
	if (segPtr->tagInfoPtr) {
	    TkTextTagSetDecrRefCount(segPtr->tagInfoPtr);
	}
	FREE_SEGMENT(segPtr);
	DEBUG_ALLOC(tkTextCountDestroySegment++);
    }
}

/*
 *--------------------------------------------------------------
 *
 * FreeLine --
 *
 *	Free all resources of the given line.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Some storage will be freed.
 *
 *--------------------------------------------------------------
 */

static void
FreeLine(
    const BTree *treePtr,
    TkTextLine *linePtr)
{
    unsigned i;

    assert(linePtr->parentPtr);
    DEBUG(linePtr->parentPtr = NULL);

    for (i = 0; i < treePtr->numPixelReferences; ++i) {
	TkTextDispLineInfo *dispLineInfo = linePtr->pixelInfo[i].dispLineInfo;

	if (dispLineInfo) {
	    free(dispLineInfo);
	    DEBUG_ALLOC(tkTextCountDestroyDispInfo++);
	}
    }

    TkTextTagSetDecrRefCount(linePtr->tagoffPtr);
    TkTextTagSetDecrRefCount(linePtr->tagonPtr);
    free(linePtr->pixelInfo);
    DEBUG(linePtr->pixelInfo = NULL);
    free(linePtr);
    DEBUG_ALLOC(tkTextCountDestroyPixelInfo++);
    DEBUG_ALLOC(tkTextCountDestroyLine++);
}

/*
 *--------------------------------------------------------------
 *
 * MakeCharSeg --
 *
 *	Make new char segment with given text.
 *
 * Results:
 *	The return value is a pointer to the new segment.
 *
 * Side effects:
 *	Storage for new segment will be allocated.
 *
 *--------------------------------------------------------------
 */

static TkTextSegment *
MakeCharSeg(
    TkTextSection *sectionPtr,	/* Section of new segment, can be NULL. */
    TkTextTagSet *tagInfoPtr,	/* Tga information for new segment, can be NULL. */
    unsigned newSize,		/* Character size of the new segment. */
    const char *string,		/* New text content. */
    unsigned length)		/* Number of characters to copy. */
{
    unsigned capacity;
    TkTextSegment *newPtr;

    assert(length <= newSize);

    capacity = CSEG_CAPACITY(newSize);
    newPtr = memset(malloc(CSEG_SIZE(capacity)), 0, SEG_SIZE(0));
    newPtr->typePtr = &tkTextCharType;
    newPtr->sectionPtr = sectionPtr;
    newPtr->size = newSize;
    newPtr->refCount = 1;
    memcpy(newPtr->body.chars, string, length);
    memset(newPtr->body.chars + length, 0, capacity - length);
    if ((newPtr->tagInfoPtr = tagInfoPtr)) {
	TkTextTagSetIncrRefCount(tagInfoPtr);
    }
    DEBUG_ALLOC(tkTextCountNewSegment++);
    return newPtr;
}

/*
 *--------------------------------------------------------------
 *
 * CopyCharSeg --
 *
 *	Make new char segment, and copy text from given segment.
 *
 * Results:
 *	The return value is a pointer to the new segment.
 *
 * Side effects:
 *	Storage for new segment will be allocated.
 *
 *--------------------------------------------------------------
 */

static TkTextSegment *
CopyCharSeg(
    TkTextSegment *segPtr,	/* Copy text from this segment. */
    unsigned offset,		/* Copy text starting at this offset. */
    unsigned length,		/* Number of characters to copy. */
    unsigned newSize)		/* Character size of the new segment. */
{
    assert(segPtr);
    assert(segPtr->typePtr == &tkTextCharType);
    assert(segPtr->size >= (int) (offset + length));

    return MakeCharSeg(segPtr->sectionPtr, segPtr->tagInfoPtr, newSize,
	    segPtr->body.chars + offset, length);
}

/*
 *--------------------------------------------------------------
 *
 * SplitCharSegment --
 *
 *	This function implements splitting for character segments.
 *
 * Results:
 *	The return value is a pointer to a chain of two segments that have the
 *	same characters as segPtr except split among the two segments.
 *
 * Side effects:
 *	Storage for segPtr is freed.
 *
 *--------------------------------------------------------------
 */

static TkTextSegment *
SplitCharSegment(
    TkTextSegment *segPtr,	/* Pointer to segment to split. */
    unsigned index)		/* Position within segment at which to split. */
{
    TkTextSegment *newPtr1, *newPtr2;

    assert(segPtr);
    assert(segPtr->typePtr == &tkTextCharType); /* still unfreed? */
    assert(segPtr->sectionPtr); /* still hooked? */
    assert(index > 0);
    assert((int) index < segPtr->size);

    newPtr1 = CopyCharSeg(segPtr, 0, index, index);
    newPtr2 = CopyCharSeg(segPtr, index, segPtr->size - index, segPtr->size - index);

    newPtr1->nextPtr = newPtr2;
    newPtr1->prevPtr = segPtr->prevPtr;
    newPtr2->nextPtr = segPtr->nextPtr;
    newPtr2->prevPtr = newPtr1;

    if (segPtr->prevPtr) {
	segPtr->prevPtr->nextPtr = newPtr1;
    } else {
	segPtr->sectionPtr->linePtr->segPtr = newPtr1;
    }
    if (segPtr->nextPtr) {
	segPtr->nextPtr->prevPtr = newPtr2;
    }
    if (segPtr->sectionPtr->segPtr == segPtr) {
	segPtr->sectionPtr->segPtr = newPtr1;
    }
    if (segPtr->sectionPtr->linePtr->lastPtr == segPtr) {
	segPtr->sectionPtr->linePtr->lastPtr = newPtr2;
    }
    newPtr1->sectionPtr->length += 1;
    assert(newPtr1->sectionPtr->length != 0); /* test for overflow */
    TkBTreeFreeSegment(segPtr);
    return newPtr1;
}

/*
 *--------------------------------------------------------------
 *
 * IncreaseCharSegment --
 *
 *	This function make a larger (or smaller) char segment, the new
 *	segment will replace the old one.
 *
 * Results:
 *	The return value is a pointer to the new segment.
 *
 * Side effects:
 *	Storage for old segment is freed.
 *
 *--------------------------------------------------------------
 */

static TkTextSegment *
IncreaseCharSegment(
    TkTextSegment *segPtr,	/* Pointer to segment. */
    unsigned offset,		/* Split point in char segment. */
    int chunkSize)		/* Add/subtract this size to the new segment. */
{
    TkTextSegment *newPtr;

    assert(chunkSize != 0);

    newPtr = CopyCharSeg(segPtr, 0, offset, segPtr->size + chunkSize);
    if (chunkSize > 0) {
	memcpy(newPtr->body.chars + offset + chunkSize,
		segPtr->body.chars + offset,
		segPtr->size - offset);
    }
    newPtr->nextPtr = segPtr->nextPtr;
    newPtr->prevPtr = segPtr->prevPtr;

    if (segPtr->prevPtr) {
	segPtr->prevPtr->nextPtr = newPtr;
    } else {
	segPtr->sectionPtr->linePtr->segPtr = newPtr;
    }
    if (segPtr->nextPtr) {
	segPtr->nextPtr->prevPtr = newPtr;
    }
    if (segPtr->sectionPtr) {
	if (segPtr->sectionPtr->segPtr == segPtr) {
	    segPtr->sectionPtr->segPtr = newPtr;
	}
	if (segPtr->sectionPtr->linePtr->lastPtr == segPtr) {
	    segPtr->sectionPtr->linePtr->lastPtr = newPtr;
	}
    }
    TkBTreeFreeSegment(segPtr);
    return newPtr;
}

/*
 *--------------------------------------------------------------
 *
 * PrepareInsertIntoCharSeg --
 *
 *	This function is called within SplitSeg() to finalize the work:
 *
 *	a) We want to insert chars, and segPtr is a char segment, so
 *	   we will change the size of the segment (this may require a
 *	   replacement with a newly segment). If 'splitInfo->forceSplit'
 *	   is set, and offset is not zero, then we must split because
 *	   in this case the caller will insert chars with a trailing
 *	   newline.
 *
 *	b) We want to insert chars, and segPtr is not a char segment,
 *	   just return segPtr, the caller will insert a new segment.
 *
 *	c) We want to insert a non-char segment, so we must split
 *	   anyway if offset > 0, and the latter case only happens
 *	   in case of char segments.
 *
 * Results:
 *	The return value is a pointer to a segment, probably NULL if the
 *	given segment is NULL. 'splitInfo->offset' will be updated with
 *	offset (insertion point) in increased/decreased segment, or with
 *	-1 if we didn't increase/decrease the segment.
 *
 * Side effects:
 *	The segment referred by 'segPtr' may become modified or replaced.
 *	Pobably a new char segment will be inserted.
 *
 *--------------------------------------------------------------
 */

static TkTextSegment *
PrepareInsertIntoCharSeg(
    TkTextSegment *segPtr,	/* Split or modify this segment. */
    unsigned offset,		/* Offset in segment. */
    SplitInfo *splitInfo)	/* Additional arguments. */
{
    unsigned oldCapacity, newCapacity;

    assert(splitInfo);
    assert(!splitInfo->splitted);
    assert(splitInfo->increase != 0);
    assert(segPtr);
    assert(segPtr->typePtr == &tkTextCharType);
    assert((int) offset <= segPtr->size);
    assert((int) offset < segPtr->size || segPtr->body.chars[segPtr->size - 1] != '\n');

    /*
     * We must not split if the new char content will be appended
     * to the current content (i.e. offset == segPtr->size).
     */

    if (splitInfo->forceSplit && (int) offset < segPtr->size) {
	unsigned newSize, decreasedSize;
	TkTextSegment *newPtr;

	splitInfo->splitted = true;

	if (offset == 0 && segPtr == segPtr->sectionPtr->linePtr->segPtr) {
	    /*
	     * This is a bit tricky: we are not doing a split here, because inserting
	     * a newline at start of line is an implicit split (the callee inserts a
	     * new line), and the callee has to know that he can join the next content
	     * part into this char segment. Note that 'splitInfo->offset' is still
	     * negative, this has the effect that this segment will be shifted to the
	     * next line until an insertion of chars will be done.
	     */
	    return NULL;
	}

	/*
	 * We must split after offset for the new line.
	 */

	newSize = segPtr->size - offset;
	decreasedSize = segPtr->size - newSize;
	newPtr = CopyCharSeg(segPtr, offset, newSize, newSize);
	DEBUG(newPtr->sectionPtr = NULL);
	memset(segPtr->body.chars + decreasedSize, 0, segPtr->size - decreasedSize);
	segPtr->size = decreasedSize;
	newPtr->size = 0; /* temporary; LinkSegment() should not change total size */
	LinkSegment(segPtr->sectionPtr->linePtr, segPtr, newPtr);
	newPtr->size = newSize;
	SplitSection(segPtr->sectionPtr);
    }

    oldCapacity = CSEG_CAPACITY(segPtr->size);
    newCapacity = CSEG_CAPACITY(segPtr->size + splitInfo->increase);

    if (oldCapacity != newCapacity) {
	/*
	 * We replace this segment by a larger (or smaller) one.
	 */
	segPtr = IncreaseCharSegment(segPtr, offset, splitInfo->increase);
    } else {
	/*
	 * This segment has the right capacity for new content, so it's just
	 * an insertion/replacement. We did consider the trailing nul byte.
	 */
	if (splitInfo->increase > 0) {
	    memmove(segPtr->body.chars + offset + splitInfo->increase,
		    segPtr->body.chars + offset,
		    segPtr->size - offset);
	} else {
	    memset(segPtr->body.chars + offset, 0, newCapacity - offset);
	}
	segPtr->size += splitInfo->increase;
    }

    splitInfo->offset = offset;
    return segPtr;
}

/*
 *--------------------------------------------------------------
 *
 * SplitSeg --
 *
 *	This function is called before adding or deleting segments. It does
 *	three things: (a) it finds the segment containing indexPtr; (b) if
 *	there are several such segments (because some segments have zero
 *	length) then it picks the first segment that does not have left
 *	gravity; (c) if the index refers to the middle of a segment and we
 *	want to insert a segment without chars (splitInfo is NULL), then it
 *	splits the segment so that the index now refers to the beginning of
 *	a segment.
 *
 * Results:
 *	The return value is a pointer to the segment just before the segment
 *	corresponding to indexPtr (as described above). If the segment
 *	corresponding to indexPtr is the first in its line then the return
 *	value is NULL.
 *
 * Side effects:
 *	The segment referred to by indexPtr may be either split or replaced
 *	by a larger one.
 *
 *--------------------------------------------------------------
 */

static bool
CanInsertLeft(
    const TkText *textPtr,
    int offset,
    TkTextSegment *segPtr)
{
    TkTextSegment *prevPtr;

    assert(segPtr->tagInfoPtr);

    if (!TkTextTagSetIsEmpty(segPtr->tagInfoPtr)) {
	switch (textPtr->tagging) {
	    case TK_TEXT_TAGGING_GRAVITY:
		return offset > 0 || textPtr->insertMarkPtr->typePtr == &tkTextLeftMarkType;
	    case TK_TEXT_TAGGING_WITHIN:
		if (offset > 0) {
		    return true; /* inserting into a char segment */
		}
		prevPtr = GetPrevTagInfoSegment(segPtr);
		return prevPtr && TkTextTagSetContains(prevPtr->tagInfoPtr, segPtr->tagInfoPtr);
	    case TK_TEXT_TAGGING_NONE:
		if (offset == 0) {
		    return false;
		}
		prevPtr = GetPrevTagInfoSegment(segPtr);
		return !prevPtr || TkTextTagSetIsEmpty(prevPtr->tagInfoPtr);
	}
    }
    return true;
}

static bool
CanInsertRight(
    const TkText *textPtr,
    TkTextSegment *prevPtr,
    TkTextSegment *segPtr)
{
    assert(prevPtr->tagInfoPtr);

    switch (textPtr->tagging) {
	case TK_TEXT_TAGGING_GRAVITY:
	    return textPtr->insertMarkPtr->typePtr == &tkTextRightMarkType;
	case TK_TEXT_TAGGING_WITHIN:
	    return TkTextTagSetContains(GetNextTagInfoSegment(segPtr)->tagInfoPtr, prevPtr->tagInfoPtr);
	case TK_TEXT_TAGGING_NONE:
	    return TkTextTagSetIsEmpty(prevPtr->tagInfoPtr);
    }
    return false; /* never reached */
}

static TkTextSegment *
SplitSeg(
    const TkTextIndex *indexPtr,/* Index identifying position at which to split a segment. */
    SplitInfo *splitInfo)	/* Additional arguments for split, only given when inserting chars. */
{
    TkTextSegment *segPtr;
    int count;

    if (splitInfo) {
	/*
	 * We assume that 'splitInfo' is already initialized.
	 */

	assert(splitInfo->offset == -1);
	assert(splitInfo->increase != 0);
	assert(!splitInfo->splitted);
    }

    assert(indexPtr->textPtr || !splitInfo);

    if (TkTextIndexGetShared(indexPtr)->steadyMarks) {
	/*
	 * With steadymarks we need the exact position, if given by a mark.
	 */

	segPtr = TkTextIndexGetSegment(indexPtr);
	if (segPtr && segPtr->typePtr->group == SEG_GROUP_MARK) {
	    count = 0;
	} else {
	    segPtr = TkTextIndexGetFirstSegment(indexPtr, &count);
	    TkTextIndexToByteIndex((TkTextIndex *) indexPtr); /* mutable due to concept */
	}
    } else {
	segPtr = TkTextIndexGetFirstSegment(indexPtr, &count);
	TkTextIndexToByteIndex((TkTextIndex *) indexPtr); /* mutable due to concept */
    }

    for ( ; segPtr; segPtr = segPtr->nextPtr) {
	if (segPtr->size > count) {
	    if (splitInfo && segPtr->typePtr == &tkTextCharType) {
		TkTextSegment *prevPtr;

		if (splitInfo->tagInfoPtr
		    	? TkTextTagSetIsEqual(segPtr->tagInfoPtr, splitInfo->tagInfoPtr)
			: CanInsertLeft(indexPtr->textPtr, count, segPtr)) {
		    /*
		     * Insert text into this char segment.
		     */
		    splitInfo->tagInfoPtr = segPtr->tagInfoPtr;
		    return PrepareInsertIntoCharSeg(segPtr, count, splitInfo);
		}
		if (count > 0) {
		    /*
		     * We have different tags for the new char segment, so we need a split.
		     */
		    return SplitCharSegment(segPtr, count);
		}
		if ((prevPtr = segPtr->prevPtr)
			&& prevPtr->typePtr == &tkTextCharType
			&& (splitInfo->tagInfoPtr
			    ? TkTextTagSetIsEqual(prevPtr->tagInfoPtr, splitInfo->tagInfoPtr)
			    : CanInsertRight(indexPtr->textPtr, prevPtr, segPtr))) {
		    /*
		     * Append more content at the end of the preceding char segment.
		     */
		    splitInfo->tagInfoPtr = prevPtr->tagInfoPtr;
		    return PrepareInsertIntoCharSeg(prevPtr, prevPtr->size, splitInfo);
		}
	    }
	    if (count == 0) {
		/*
		 * We are one segment too far ahead. This case must
		 * also catch hyphens, embedded images, and windows.
		 */
		return segPtr->prevPtr;
	    }
	    /*
	     * Actually a split of the char segment is necessary.
	     */
	    segPtr = SplitCharSegment(segPtr, count);
	    TkTextIndexToByteIndex((TkTextIndex *) indexPtr); /* mutable due to concept */
	    return segPtr;
	}
	if (count == 0 && segPtr->typePtr->gravity == GRAVITY_RIGHT) {
	    TkTextSegment *prevPtr = segPtr->prevPtr;
	    assert(segPtr->size == 0);
	    if (splitInfo
		    && prevPtr
		    && prevPtr->typePtr == &tkTextCharType
		    && (splitInfo->tagInfoPtr
			? TkTextTagSetIsEqual(prevPtr->tagInfoPtr, splitInfo->tagInfoPtr)
			: CanInsertRight(indexPtr->textPtr, prevPtr, segPtr))) {
		/*
		 * Append more content at the end of the preceding char segment.
		 */
		splitInfo->tagInfoPtr = prevPtr->tagInfoPtr;
		return PrepareInsertIntoCharSeg(prevPtr, prevPtr->size, splitInfo);
	    }
	    /*
	     * Right gravity is inserting at left side.
	     */
	    return prevPtr;
	}
	count -= segPtr->size;
    }
    assert(!"SplitSeg reached end of line!");
    return NULL;
}

/*
 *--------------------------------------------------------------
 *
 * TkBTreeMakeCharSegment --
 *
 *	Make new char segment with given text.
 *
 * Results:
 *	The return value is a pointer to the new segment.
 *
 * Side effects:
 *	Storage for new segment will be allocated.
 *
 *--------------------------------------------------------------
 */

TkTextSegment *
TkBTreeMakeCharSegment(
    const char *string,
    unsigned length,
    TkTextTagSet *tagInfoPtr)	/* can be NULL */
{
    TkTextSegment *newPtr;
    unsigned memsize = CSEG_SIZE(length + 1);

    assert(string);
    assert(tagInfoPtr);

    newPtr = memset(malloc(memsize), 0, memsize);
    newPtr->typePtr = &tkTextCharType;
    newPtr->size = length;
    newPtr->refCount = 1;
    TkTextTagSetIncrRefCount(newPtr->tagInfoPtr = tagInfoPtr);
    memcpy(newPtr->body.chars, string, length);
    newPtr->body.chars[length] = '\0';
    DEBUG_ALLOC(tkTextCountNewSegment++);
    return newPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateNodeTags --
 *
 *	Update the node tag information after the tag information in any
 *	line of this node has been changed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information is deleted/added from/to the B-tree.
 *
 *----------------------------------------------------------------------
 */

static void
RemoveTagoffFromNode(
    Node *nodePtr,
    TkTextTag *tagPtr)
{
    Node *parentPtr;
    unsigned tagIndex = tagPtr->index;

    assert(tagPtr);
    assert(!tagPtr->isDisabled);
    assert(nodePtr->level == 0);
    assert(TkTextTagSetTest(nodePtr->tagoffPtr, tagIndex));

    nodePtr->tagoffPtr = TagSetErase(nodePtr->tagoffPtr, tagPtr);

    while ((parentPtr = nodePtr->parentPtr)) {
	for (nodePtr = parentPtr->childPtr; nodePtr; nodePtr = nodePtr->nextPtr) {
	    if (TkTextTagSetTest(nodePtr->tagonPtr, tagIndex)) {
		return; /* still referenced in this node */
	    }
	}
	parentPtr->tagoffPtr = TagSetErase(nodePtr->tagoffPtr, tagPtr);
    }
}

static void
AddTagoffToNode(
    Node *nodePtr,
    const TkTextTagSet *tagoffPtr)
{
    assert(nodePtr->level == 0);

    do {
	nodePtr->tagoffPtr = TkTextTagSetJoin(nodePtr->tagoffPtr, tagoffPtr);
    } while ((nodePtr = nodePtr->parentPtr));
}

static void
UpdateNodeTags(
    const TkSharedText *sharedTextPtr,
    Node *nodePtr)
{
    const TkTextLine *linePtr = nodePtr->linePtr;
    const TkTextLine *lastPtr = nodePtr->lastPtr->nextPtr;
    TkTextTagSet *tagonPtr;
    TkTextTagSet *tagoffPtr;
    TkTextTagSet *additionalTagoffPtr;
    TkTextTagSet *nodeTagonPtr;
    TkTextTagSet *nodeTagoffPtr;
    unsigned i;

    assert(nodePtr->level == 0);
    assert(linePtr);

    TkTextTagSetIncrRefCount(tagonPtr = linePtr->tagonPtr);
    TkTextTagSetIncrRefCount(tagoffPtr = linePtr->tagoffPtr);
    TkTextTagSetIncrRefCount(additionalTagoffPtr = tagonPtr);
    TkTextTagSetIncrRefCount(nodeTagonPtr = nodePtr->tagonPtr);
    TkTextTagSetIncrRefCount(nodeTagoffPtr = nodePtr->tagoffPtr);

    if (linePtr != lastPtr) {
	for (linePtr = linePtr->nextPtr; linePtr != lastPtr; linePtr = linePtr->nextPtr) {
	    tagonPtr = TkTextTagSetJoin(tagonPtr, linePtr->tagonPtr);
	    tagoffPtr = TkTextTagSetJoin(tagoffPtr, linePtr->tagoffPtr);
	    additionalTagoffPtr = TagSetIntersect(additionalTagoffPtr, linePtr->tagonPtr, sharedTextPtr);
	}
    }

    if (!TkTextTagSetIsEqual(tagonPtr, nodeTagonPtr) || !TkTextTagSetIsEqual(tagoffPtr, nodeTagoffPtr)) {
	if (additionalTagoffPtr) {
	    tagoffPtr = TagSetJoinComplementTo(tagoffPtr, additionalTagoffPtr, tagonPtr, sharedTextPtr);
	    TkTextTagSetDecrRefCount(additionalTagoffPtr);
	} else {
	    TagSetAssign(&tagoffPtr, tagonPtr);
	}

	for (i = TkTextTagSetFindFirst(nodeTagonPtr);
		i != TK_TEXT_TAG_SET_NPOS;
		i = TkTextTagSetFindNext(nodeTagonPtr, i)) {
	    if (!TkTextTagSetTest(tagonPtr, i)) {
		RemoveTagFromNode(nodePtr, sharedTextPtr->tagLookup[i]);
	    }
	}

	for (i = TkTextTagSetFindFirst(tagonPtr);
		i != TK_TEXT_TAG_SET_NPOS;
		i = TkTextTagSetFindNext(tagonPtr, i)) {
	    if (!TkTextTagSetTest(nodeTagonPtr, i)) {
		AddTagToNode(nodePtr, sharedTextPtr->tagLookup[i], false);
	    }
	}

	if (!TkTextTagSetContains(tagoffPtr, nodeTagoffPtr)) {
	    for (i = TkTextTagSetFindFirst(nodeTagoffPtr);
		    i != TK_TEXT_TAG_SET_NPOS;
		    i = TkTextTagSetFindNext(nodeTagoffPtr, i)) {
		if (!TkTextTagSetTest(tagoffPtr, i) && TkTextTagSetTest(tagonPtr, i)) {
		    RemoveTagoffFromNode(nodePtr, sharedTextPtr->tagLookup[i]);
		}
	    }
	}

	AddTagoffToNode(nodePtr, tagoffPtr);

	assert(TkTextTagSetIsEqual(tagonPtr, nodePtr->tagonPtr));
	assert(TkTextTagSetIsEqual(tagoffPtr, nodePtr->tagoffPtr));
    } else if (additionalTagoffPtr) {
	TkTextTagSetDecrRefCount(additionalTagoffPtr);
    }

    TkTextTagSetDecrRefCount(tagonPtr);
    TkTextTagSetDecrRefCount(tagoffPtr);
    TkTextTagSetDecrRefCount(nodeTagonPtr);
    TkTextTagSetDecrRefCount(nodeTagoffPtr);
}
/*
 *----------------------------------------------------------------------
 *
 * DeleteRange --
 *
 *	Delete a range of segments from a B-tree. The caller must make sure
 *	that the final newline of the B-tree will not be affected.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information is deleted from the B-tree. This can cause the internal
 *	structure of the B-tree to change.
 *
 *----------------------------------------------------------------------
 */

static void
SetNodeFirstPointer(
    Node *nodePtr,
    TkTextLine *linePtr)
{
    TkTextLine *oldLinePtr = nodePtr->linePtr;

    nodePtr->linePtr = linePtr;
    while ((nodePtr = nodePtr->parentPtr) && nodePtr->linePtr == oldLinePtr) {
	nodePtr->linePtr = linePtr;
    }
}

static void
MoveSegmentToLeft(
    TkTextSegment *branchPtr,
    TkTextSegment *movePtr)	/* movePtr will become a predecessor of branchPtr */
{
    assert(movePtr);
    assert(branchPtr);
    assert(branchPtr->sectionPtr->linePtr == movePtr->sectionPtr->linePtr);
    assert(movePtr->nextPtr != branchPtr);
    assert(branchPtr->nextPtr);
    assert(movePtr->prevPtr);

    movePtr->prevPtr->nextPtr = movePtr->nextPtr;
    if (movePtr->nextPtr) {
	movePtr->nextPtr->prevPtr = movePtr->prevPtr;
    }
    movePtr->nextPtr = branchPtr;

    if (branchPtr->prevPtr) {
	branchPtr->prevPtr->nextPtr = movePtr;
    }
    branchPtr->prevPtr = movePtr;

    /*
     * We don't care about the sections, they will be rebuilt later,
     * but ensure that the order of the sections will not change.
     */

    if (--movePtr->sectionPtr->length == 0) {
	FreeSection(movePtr->sectionPtr);
    }
    movePtr->sectionPtr = branchPtr->sectionPtr;
}

static void
MoveSegmentToRight(
    TkTextSegment *linkPtr,
    TkTextSegment *movePtr)	/* movePtr will become a successor of linkPtr */
{
    assert(movePtr);
    assert(linkPtr);
    assert(linkPtr->sectionPtr->linePtr == movePtr->sectionPtr->linePtr);
    assert(movePtr->prevPtr != linkPtr);
    assert(linkPtr->prevPtr);
    assert(movePtr->nextPtr);

    if (movePtr->prevPtr) {
	movePtr->prevPtr->nextPtr = movePtr->nextPtr;
    }
    movePtr->nextPtr->prevPtr = movePtr->prevPtr;
    movePtr->prevPtr = linkPtr;

    if (linkPtr->nextPtr) {
	linkPtr->nextPtr->prevPtr = movePtr;
    }
    linkPtr->nextPtr = movePtr;

    /*
     * We don't care about the sections, they will be rebuilt later,
     * but ensure that the order of the sections will not change.
     */

    if (--linkPtr->sectionPtr->length == 0) {
	FreeSection(linkPtr->sectionPtr);
    }
    linkPtr->sectionPtr = movePtr->sectionPtr;
}

static void
DeleteRange(
    TkSharedText *sharedTextPtr,/* Handle to shared text resource. */
    TkTextSegment *firstSegPtr,	/* Indicates the segment just before where the deletion starts. */
    TkTextSegment *lastSegPtr,	/* Indicates the last segment where the deletion stops (exclusive
    				 * this segment). FirstSegPtr and lastSegPtr may belong to
				 * different lines. */
    int flags,			/* Flags controlling the deletion. If DELETE_INCLUSIVE is set then
    				 * also firstSegPtr and lastSegPtr will be deleted. */
    TkTextUndoInfo *undoInfo)	/* Store undo information, can be NULL. */
{
    BTree *treePtr;
    TkTextSegment *prevPtr;
    TkTextSegment *nextPtr;
    TkTextSegment *segPtr;
    TkTextSegment **segments;
    TkTextSegment *prevLinkPtr;
    TkTextSection *firstSectionPtr;
    TkTextSection *prevSectionPtr;
    TkTextSection *lastSectionPtr;
    TkTextSection *sectionPtr;
    TkTextLine *linePtr1;
    TkTextLine *linePtr2;
    TkTextLine *nextLinePtr;
    TkTextLine *curLinePtr;
    Node *curNodePtr;
    Node *nodePtr1;
    Node *nodePtr2;
    unsigned numSegments;
    unsigned maxSegments;
    unsigned byteSize;
    unsigned lineDiff;
    bool steadyMarks;
    unsigned lineNo1;
    unsigned lineNo2;

    assert(firstSegPtr);
    assert(lastSegPtr);
    assert(!undoInfo || undoInfo->token);

    assert(!(flags & DELETE_INCLUSIVE)
	    || firstSegPtr->typePtr->group & (SEG_GROUP_MARK|SEG_GROUP_PROTECT));
    assert(!(flags & DELETE_INCLUSIVE)
	    || lastSegPtr->typePtr->group & (SEG_GROUP_MARK|SEG_GROUP_PROTECT));
    assert((firstSegPtr->typePtr->group == SEG_GROUP_PROTECT) ==
	    (lastSegPtr->typePtr->group == SEG_GROUP_PROTECT));

    assert(firstSegPtr->nextPtr);

    if (TkBTreeHaveElidedSegments(sharedTextPtr)) {
	/*
	 * Include the surrounding branches and links into the deletion range.
	 */

	assert(firstSegPtr->typePtr != &tkTextBranchType);
	assert(lastSegPtr->typePtr != &tkTextLinkType);

	if (!sharedTextPtr->steadyMarks || !TkTextIsStableMark(firstSegPtr)) {
	    for (segPtr = firstSegPtr->prevPtr; segPtr && segPtr->size == 0; segPtr = segPtr->prevPtr) {
		if (segPtr->typePtr == &tkTextBranchType) {
		    /* firstSegPtr will become predecessor of this branch */
		    MoveSegmentToLeft(segPtr, firstSegPtr);
		    segPtr = firstSegPtr;
		}
	    }
	}

	if (!sharedTextPtr->steadyMarks || !TkTextIsStableMark(lastSegPtr)) {
	    for (segPtr = lastSegPtr->nextPtr; segPtr && segPtr->size == 0; segPtr = segPtr->nextPtr) {
		if (segPtr->typePtr == &tkTextLinkType) {
		    /* lastSegPtr will become successor of this link */
		    MoveSegmentToRight(segPtr, lastSegPtr);
		    segPtr = lastSegPtr;
		}
	    }
	}
    }

    treePtr = (BTree *) sharedTextPtr->tree;
    curLinePtr = firstSegPtr->sectionPtr->linePtr;
    sectionPtr = curLinePtr->segPtr->sectionPtr;
    prevSectionPtr = curLinePtr->lastPtr->sectionPtr;
    prevPtr = firstSegPtr;
    segPtr = firstSegPtr->nextPtr;
    steadyMarks = sharedTextPtr->steadyMarks;
    numSegments = 0;
    segments = NULL;

    linePtr1 = sectionPtr->linePtr;
    linePtr2 = lastSegPtr->sectionPtr->linePtr;
    nodePtr1 = linePtr1->parentPtr;
    nodePtr2 = linePtr2->parentPtr;
    lineNo1 = TkBTreeLinesTo(sharedTextPtr->tree, NULL, linePtr1, NULL);
    lineNo2 = linePtr1 == linePtr2 ? lineNo1 : TkBTreeLinesTo(sharedTextPtr->tree, NULL, linePtr2, NULL);
    lineDiff = linePtr1->size;

    SetLineHasChanged(sharedTextPtr, linePtr1);
    if (linePtr1 != linePtr2) {
	SetLineHasChanged(sharedTextPtr, linePtr2);
    }

    if (undoInfo) {
	/* reserve the first entry if needed */
	numSegments = (flags & DELETE_INCLUSIVE) && TkTextIsStableMark(firstSegPtr) ? 1 : 0;
	maxSegments = 100;
	segments = malloc(maxSegments * sizeof(TkTextSegment *));
	DEBUG(segments[0] = NULL);
    } else {
	flags |= DELETE_BRANCHES;
    }

    /*
     * This line now needs to have its height recalculated. This has to be done
     * before the lines will be removed.
     */

    TkTextInvalidateLineMetrics(treePtr->sharedTextPtr, NULL,
	    linePtr1, lineNo2 - lineNo1, TK_TEXT_INVALIDATE_DELETE);

    /*
     * Connect start and end point.
     */

    firstSegPtr->nextPtr = lastSegPtr;
    lastSegPtr->prevPtr = firstSegPtr;

    if (nodePtr1 != nodePtr2 && nodePtr2->lastPtr == linePtr2) {
	/*
	 * This node is going to be deleted.
	 */
	nodePtr2 = NULL;
    }

    /*
     * Delete all of the segments between firstSegPtr (exclusive) and lastSegPtr (exclusive).
     */

    curNodePtr = curLinePtr->parentPtr;
    assert(curLinePtr->nextPtr);
    prevLinkPtr = NULL;
    firstSectionPtr = NULL;
    lastSectionPtr = NULL;
    byteSize = 0;

    while (segPtr != lastSegPtr) {
	if (!segPtr) {
	    /*
	     * We just ran off the end of a line.
	     */

	    if (curLinePtr != linePtr1) {
		/*
		 * Join unused section, RebuildSections will reuse/delete those sections.
		 */

		prevSectionPtr->nextPtr = firstSectionPtr;
		firstSectionPtr->prevPtr = prevSectionPtr;
		prevSectionPtr = lastSectionPtr;

		if (curNodePtr == nodePtr1 || curNodePtr == nodePtr2) {
		    /*
		     * Update only those nodes which will not be deleted,
		     * because DeleteEmptyNode will do a faster update.
		     */
		    SubtractPixelInfo(treePtr, curLinePtr);
		    if (curLinePtr->numBranches) {
			PropagateChangeOfNumBranches(curLinePtr->parentPtr, -(int) curLinePtr->numBranches);
		    }
		}

		if (--curNodePtr->numChildren == 0) {
		    DeleteEmptyNode(treePtr, curNodePtr);
		}
	    }
	    curLinePtr = curLinePtr->nextPtr;
	    curNodePtr = curLinePtr->parentPtr;
	    segPtr = curLinePtr->segPtr;
	    firstSectionPtr = curLinePtr->segPtr->sectionPtr;
	    lastSectionPtr = curLinePtr->lastPtr->sectionPtr;
	} else {
	    assert(segPtr->sectionPtr->linePtr == curLinePtr);
	    assert(segPtr->typePtr->deleteProc);
	    nextPtr = segPtr->nextPtr;
	    byteSize += segPtr->size;
	    if (undoInfo && !TkTextIsSpecialOrPrivateMark(segPtr)) {
		if (numSegments == maxSegments) {
		    maxSegments = MAX(50u, numSegments * 2u);
		    segments = realloc(segments, maxSegments * sizeof(TkTextSegment *));
		}
		if (segPtr->tagInfoPtr) {
		    segPtr->tagInfoPtr = TagSetRemoveBits(segPtr->tagInfoPtr,
			    sharedTextPtr->dontUndoTags, sharedTextPtr);
		}
		segments[numSegments++] = segPtr;
		segPtr->refCount += 1;
	    }
	    if (!segPtr->typePtr->deleteProc((TkTextBTree) treePtr, segPtr, flags)) {
		assert(segPtr->typePtr); /* really still living? */
		assert(segPtr->typePtr->group == SEG_GROUP_MARK
			|| segPtr->typePtr->group == SEG_GROUP_BRANCH);

		if (prevLinkPtr && segPtr->typePtr == &tkTextBranchType) {
		    /*
		     * This is a superfluous link/branch pair, delete both.
		     */

		    /* make new relationship (old one is already saved) */
		    prevLinkPtr->body.link.prevPtr->body.branch.nextPtr = segPtr->body.branch.nextPtr;
		    segPtr->body.branch.nextPtr->body.link.prevPtr = prevLinkPtr->body.link.prevPtr;
		    /* remove this pair from chain */
		    nextPtr = segPtr->nextPtr;
		    UnlinkSegment(segPtr);
		    TkBTreeFreeSegment(segPtr);
		    UnlinkSegmentAndCleanup(sharedTextPtr, prevLinkPtr);
		    TkBTreeFreeSegment(prevLinkPtr);
		    if (nextPtr->prevPtr && nextPtr->prevPtr->typePtr == &tkTextCharType) {
			TkTextSegment *sPtr = CleanupCharSegments(sharedTextPtr, nextPtr);
			if (sPtr != nextPtr) { nextPtr = nextPtr->nextPtr; }
		    }
		    prevLinkPtr = NULL;
		} else {
		    /*
		     * This segment refuses to die, it's either a switch with a counterpart
		     * outside of the deletion range, or it's a mark. Link this segment
		     * after prevPtr.
		     */

		    assert(prevPtr);
		    DEBUG(segPtr->sectionPtr = NULL);

		    if (segPtr->typePtr == &tkTextLinkType) {
			assert(!prevLinkPtr);
			prevLinkPtr = segPtr;
			LinkSwitch(linePtr1, prevPtr, segPtr);

			if (prevPtr->typePtr->group != SEG_GROUP_MARK) {
			    prevPtr = segPtr;
			}
		    } else {
			assert(segPtr->typePtr->group == SEG_GROUP_MARK);
			LinkMark(sharedTextPtr, linePtr1, prevPtr, segPtr);

			/*
			 * Option 'steadymarks' is off:
			 * 'prevPtr' will be advanced only if the segment don't has right gravity.
			 *
			 * Option 'steadymarks' is on:
			 * 'prevPtr' will always be advanced, because we keep the order of the marks.
			 */

			if (steadyMarks || segPtr->typePtr->gravity != GRAVITY_RIGHT) {
			    prevPtr = segPtr;
			}
		    }

		    assert(segPtr->prevPtr);
		    segPtr->sectionPtr = segPtr->prevPtr->sectionPtr;

		    if (segments && !TkTextIsSpecialOrPrivateMark(segPtr)) {
			/* Mark this segment as re-inserted. */
			MARK_POINTER(segments[numSegments - 1]);
		    }

		    /*
		     * Prevent an overflow of the section length, because this may happen
		     * when deleting segments. The section length doesn't matter here,
		     * because the section structure will be rebuilt later. But LinkSegment
		     * will trap into an assertion if we do not prevent this.
		     */
		    DEBUG(segPtr->sectionPtr->length = 0);
		}
	    }
	    segPtr = nextPtr;
	}
    }

    nextLinePtr = linePtr1->nextPtr;

    if (linePtr1 != linePtr2) {
	/*
	 * Finalize update of B-tree (children and pixel count).
	 */

	nodePtr2 = linePtr2->parentPtr;
	if (nodePtr1 != nodePtr2) {
	    SetNodeLastPointer(nodePtr1, linePtr1);
	}

	if (--nodePtr2->numChildren == 0) {
	    assert(nodePtr2->lastPtr == linePtr2);
	    DeleteEmptyNode(treePtr, nodePtr2);
	    nodePtr2 = NULL;
	} else {
	    SubtractPixelInfo(treePtr, linePtr2);
	    assert(nodePtr2->lastPtr != linePtr2 || nodePtr1 == nodePtr2);
	    if (nodePtr1 != nodePtr2) {
		SetNodeFirstPointer(nodePtr2, linePtr2->nextPtr);
	    } else if (nodePtr2->lastPtr == linePtr2) {
		SetNodeLastPointer(nodePtr2, linePtr1);
	    }
	    assert(nodePtr2->numLines == nodePtr2->numChildren);
	}

	/*
	 * The beginning and end of the deletion range are in different lines,
	 * so join the two lines and discard the ending line.
	 */

	linePtr1->lastPtr = linePtr2->lastPtr;
	if ((linePtr1->nextPtr = linePtr2->nextPtr)) {
	    linePtr1->nextPtr->prevPtr = linePtr1;
	}
	prevSectionPtr->nextPtr = firstSectionPtr;
	firstSectionPtr->prevPtr = prevSectionPtr;
    }

    if (TkBTreeHaveElidedSegments(sharedTextPtr)) {
	/*
	 * We have moved surrounding branches and links into the deletion range,
	 * now we have to revert this (for remaining switches) before RebuildSections
	 * will be invoked.
	 */

	if (firstSegPtr->size == 0 && firstSegPtr->nextPtr->typePtr == &tkTextBranchType) {
	    TkTextSegment *leftSegPtr = firstSegPtr;
	    TkTextSegment *branchPtr = firstSegPtr;

	    while (leftSegPtr && leftSegPtr->prevPtr && leftSegPtr->prevPtr->size == 0) {
		leftSegPtr = leftSegPtr->prevPtr;
	    }
	    do {
		TkTextSegment *nextPtr = branchPtr->nextPtr;
		/* branchPtr will become a predecessor of leftSegPtr */
		MoveSegmentToLeft(leftSegPtr, branchPtr);
		branchPtr = nextPtr;
	    } while (branchPtr->typePtr == &tkTextBranchType);
	}

	if (lastSegPtr->size == 0
		&& lastSegPtr->prevPtr
		&& lastSegPtr->prevPtr->typePtr == &tkTextLinkType) {
	    TkTextSegment *rightPtr = lastSegPtr;
	    TkTextSegment *linkPtr = lastSegPtr->prevPtr;

	    while (rightPtr && rightPtr->nextPtr->size == 0) {
		rightPtr = rightPtr->nextPtr;
	    }
	    do {
		TkTextSegment *prevPtr = linkPtr->prevPtr;
		/* linkPtr will become a successor of rightPtr */
		MoveSegmentToRight(rightPtr, linkPtr);
		linkPtr = prevPtr;
	    } while (linkPtr && linkPtr->typePtr == &tkTextLinkType);
	}
    }

    /*
     * Rebuild the sections in the new line. This must be done before other
     * cleanups will be done. Be sure that the first segment really points
     * to the first section, because LinkSegment may have changed this pointer.
     */

    linePtr1->segPtr->sectionPtr = sectionPtr;
    RebuildSections(sharedTextPtr, linePtr1, true);

    /*
     * Recompute the line tag information of first line.
     */

    RecomputeLineTagInfo(linePtr1, NULL, sharedTextPtr);

    /*
     * Update the size of the node which holds the first line.
     */

    lineDiff -= linePtr1->size;
    for (curNodePtr = nodePtr1; curNodePtr; curNodePtr = curNodePtr->parentPtr) {
	curNodePtr->size -= lineDiff;
    }

    /*
     * Finally delete the bounding segments if necessary. This cannot be
     * done before RebuildSections has been performed. We are doing this
     * as a separate step, this is avoiding special cases in the main
     * deletion loop. Also consider that only marks (including protection
     * marks) are allowed as deletable boundaries.
     */

    if (flags & DELETE_INCLUSIVE) {
	unsigned countChanges = 0;

	assert(firstSegPtr->typePtr->group & (SEG_GROUP_MARK|SEG_GROUP_PROTECT));
	assert(lastSegPtr->typePtr->group & (SEG_GROUP_MARK|SEG_GROUP_PROTECT));

	/*
	 * Do not unlink the special/private marks.
	 * And don't forget the undo chain.
	 */

	if (!TkTextIsSpecialOrPrivateMark(firstSegPtr)) {
	    UnlinkSegment(firstSegPtr);
	    assert(firstSegPtr->typePtr->deleteProc);
	    if (!firstSegPtr->typePtr->deleteProc((TkTextBTree) treePtr, firstSegPtr, flags)) {
		assert(!"mark refuses to die"); /* this should not happen */
	    } else if (segments && TkTextIsStableMark(firstSegPtr)) {
		firstSegPtr->refCount += 1;
		assert(!segments[0]); /* this slot must be reserved */
		segments[0] = firstSegPtr;
	    }
	    countChanges += 1;
	}
	if (!TkTextIsSpecialOrPrivateMark(lastSegPtr)) {
	    UnlinkSegment(lastSegPtr);
	    assert(lastSegPtr->typePtr->deleteProc);
	    if (!lastSegPtr->typePtr->deleteProc((TkTextBTree) treePtr, lastSegPtr, flags)) {
		assert(!"mark refuses to die"); /* this should not happen */
	    } else if (segments && TkTextIsStableMark(lastSegPtr)) {
		if (numSegments == maxSegments) {
		    maxSegments += 2;
		    segments = realloc(segments, maxSegments * sizeof(TkTextSegment *));
		}
		segments[numSegments++] = lastSegPtr;
		lastSegPtr->refCount += 1;
	    }
	    countChanges += 1;
	}
	if (countChanges == 0) {
	    flags &= ~DELETE_INCLUSIVE;
	}
    }

    /*
     * Do final update of nodes which contains the first/last line. This
     * has to be performed before any rebalance of the B-Tree will be done.
     */

    if (nodePtr2 && nodePtr2 != nodePtr1) {
	assert(nodePtr2 == linePtr2->nextPtr->parentPtr);
	UpdateNodeTags(sharedTextPtr, nodePtr2);
    }
    UpdateNodeTags(sharedTextPtr, nodePtr1);

    /*
     * Now its time to deallocate all unused lines.
     */

    curLinePtr = nextLinePtr;
    nextLinePtr = linePtr2->nextPtr;
    while (curLinePtr != nextLinePtr) {
	TkTextLine *nextLinePtr = curLinePtr->nextPtr;
	FreeLine(treePtr, curLinePtr);
	curLinePtr = nextLinePtr;
    }

    /*
     * Finish the setup of the redo information.
     */

    if (undoInfo) {
	UndoTokenDelete *undoToken = (UndoTokenDelete *) undoInfo->token;

	assert(numSegments == 0 || segments[0]);

	if (numSegments + 1 != maxSegments) {
	    segments = realloc(segments, (numSegments + 1)*sizeof(TkTextSegment *));
	}
	undoToken->segments = segments;
	undoToken->numSegments = numSegments;
	undoToken->inclusive = !!(flags & DELETE_INCLUSIVE);
	undoInfo->byteSize = byteSize;
    }

#if SUPPORT_DEPRECATED_STARTLINE_ENDLINE
    {
	TkText *peer;
	bool oldBTreeDebug = tkBTreeDebug;

	tkBTreeDebug = false;

	/*
	 * We have to adjust startline/endline.
	 */

	for (peer = sharedTextPtr->peers; peer; peer = peer->next) {
	    if (peer->startLine) {
		peer->startLine = peer->startMarker->sectionPtr->linePtr;
		if (!SegIsAtStartOfLine(peer->startMarker)) {
		    TkTextIndex index;
		    TkTextIndexClear2(&index, NULL, sharedTextPtr->tree);
		    TkTextIndexSetToStartOfLine2(&index, peer->startLine);
		    TkBTreeUnlinkSegment(sharedTextPtr, peer->startMarker);
		    TkBTreeLinkSegment(sharedTextPtr, peer->startMarker, &index);
		}
	    }
	    if (peer->endLine) {
		TkTextLine *endLinePtr = peer->endMarker->sectionPtr->linePtr;
		bool atEndOfLine = SegIsAtEndOfLine(peer->endMarker);
		bool atStartOfLine = SegIsAtStartOfLine(peer->endMarker);

		if ((!atEndOfLine || atStartOfLine) && peer->startLine != endLinePtr) {
		    TkTextIndex index;

		    assert(endLinePtr->prevPtr);
		    TkTextInvalidateLineMetrics(NULL, peer, endLinePtr->prevPtr, 1,
			    TK_TEXT_INVALIDATE_DELETE);
		    peer->endLine = endLinePtr;
		    TkTextIndexClear2(&index, NULL, sharedTextPtr->tree);
		    TkTextIndexSetToLastChar2(&index, endLinePtr->prevPtr);
		    TkBTreeUnlinkSegment(sharedTextPtr, peer->endMarker);
		    TkBTreeLinkSegment(sharedTextPtr, peer->endMarker, &index);
		} else {
		    assert(endLinePtr->nextPtr);
		    peer->endLine = endLinePtr->nextPtr;
		}
	    }
	}

	tkBTreeDebug = oldBTreeDebug;
    }
#endif

    /*
     * Don't forget to increase the epoch.
     */

    TkBTreeIncrEpoch(sharedTextPtr->tree);

    /*
     * Rebalance the node of the last deleted line, but only if the start line is
     * not contained in same node.
     */

    if (nodePtr2 && nodePtr2 != nodePtr1) {
	Rebalance(treePtr, nodePtr2);
	nodePtr1 = linePtr1->parentPtr; /* may have changed during rebalancing */
    }

    /*
     * Lastly, rebalance the first node of the range.
     */

    if (linePtr1 != linePtr2) {
	Rebalance(treePtr, nodePtr1);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeDeleteIndexRange --
 *
 *	Delete a range of characters from a B-tree. The caller must make sure
 *	that the final newline of the B-tree is never deleted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information is deleted from the B-tree. This can cause the internal
 *	structure of the B-tree to change. Note: because of changes to the
 *	B-tree structure, the indices pointed to by indexPtr1 and indexPtr2
 *	should not be used after this function returns.
 *
 *----------------------------------------------------------------------
 */

static void
DeleteIndexRange(
    TkSharedText *sharedTextPtr,/* Handle to shared text resource. */
    TkTextIndex *indexPtr1,	/* Indicates first character that is to be deleted. */
    TkTextIndex *indexPtr2,	/* Indicates character just after the last one that is to be deleted. */
    int flags,			/* Flags controlling the deletion. */
    const UndoTokenInsert *undoToken,
    				/* Perform undo, can be NULL. */
    TkTextUndoInfo *redoInfo)	/* Store undo information, can be NULL. */
{
    TkTextSegment *segPtr1;	/* The segment just before the start of the deletion range. */
    TkTextSegment *segPtr2;	/* The segment just after the end of the deletion range. */
    TkTextSegment *firstPtr;
    TkTextSegment *lastPtr;
    TkTextLine *linePtr1 = TkTextIndexGetLine(indexPtr1);
    TkTextLine *linePtr2 = TkTextIndexGetLine(indexPtr2);
    int myFlags = flags;

    assert(sharedTextPtr);
    assert(indexPtr1->tree == indexPtr2->tree);
    assert(indexPtr1->textPtr == indexPtr2->textPtr);
    assert((flags & DELETE_MARKS)
	    ? TkTextIndexCompare(indexPtr1, indexPtr2) <= 0
	    : TkTextIndexCompare(indexPtr1, indexPtr2) < 0);

    /*
     * Take care when doing the splits, none of the resulting segment pointers
     * should become invalid, so we will use protection marks to avoid this.
     */

    segPtr1 = TkTextIndexGetSegment(indexPtr1);
    segPtr2 = TkTextIndexGetSegment(indexPtr2);

    assert(!sharedTextPtr->protectionMark[0]->sectionPtr); /* this protection mark must be unused */
    assert(!sharedTextPtr->protectionMark[1]->sectionPtr); /* this protection mark must be unused */

    if (segPtr1 && TkTextIsStableMark(segPtr1)) {
	firstPtr = segPtr1;
	if (!(flags & DELETE_INCLUSIVE) && !(segPtr2 && TkTextIsStableMark(segPtr2))) {
	    LinkSegment(linePtr1, segPtr1->prevPtr, firstPtr = sharedTextPtr->protectionMark[0]);
	    myFlags |= DELETE_INCLUSIVE;
	}
    } else {
	segPtr1 = SplitSeg(indexPtr1, NULL);
	if (segPtr1) { segPtr1->protectionFlag = true; }
	LinkSegment(linePtr1, segPtr1, firstPtr = sharedTextPtr->protectionMark[0]);
	myFlags |= DELETE_INCLUSIVE;
    }

    if (segPtr2 && TkTextIsStableMark(segPtr2)) {
	lastPtr = segPtr2;
	if (!(flags & DELETE_INCLUSIVE) && (myFlags & DELETE_INCLUSIVE)) {
	    LinkSegment(linePtr2, segPtr2, lastPtr = sharedTextPtr->protectionMark[1]);
	}
    } else {
	segPtr2 = SplitSeg(indexPtr2, NULL);
	LinkSegment(linePtr2, segPtr2, lastPtr = sharedTextPtr->protectionMark[1]);
	segPtr2 = lastPtr->nextPtr;
	segPtr2->protectionFlag = true;
	myFlags |= DELETE_INCLUSIVE;
    }

    TkBTreeIncrEpoch(sharedTextPtr->tree);

    if (redoInfo) {
	UndoTokenDelete *redoToken;

	redoToken = malloc(sizeof(UndoTokenDelete));
	redoToken->undoType = &undoTokenDeleteType;
	redoToken->segments = NULL;
	redoToken->numSegments = 0;
	if (undoToken) {
	    redoToken->startIndex = undoToken->startIndex;
	    redoToken->endIndex = undoToken->endIndex;
	} else {
	    if (segPtr1 && TkTextIsStableMark(segPtr1) && !(flags & DELETE_MARKS)) {
		redoToken->startIndex.u.markPtr = segPtr1;
		redoToken->startIndex.lineIndex = -1;
	    } else {
		TkTextIndex index = *indexPtr1;
		TkTextIndexSetSegment(&index, firstPtr);
		MakeUndoIndex(sharedTextPtr, &index, &redoToken->startIndex, GRAVITY_LEFT);
	    }
	    if (segPtr2 && TkTextIsStableMark(segPtr2) && !(flags & DELETE_MARKS)) {
		redoToken->endIndex.u.markPtr = segPtr2;
		redoToken->endIndex.lineIndex = -1;
	    } else {
		TkTextIndex index = *indexPtr2;
		TkTextIndexSetSegment(&index, lastPtr);
		MakeUndoIndex(sharedTextPtr, &index, &redoToken->endIndex, GRAVITY_RIGHT);
	    }
	}
	redoInfo->token = (TkTextUndoToken *) redoToken;
	redoInfo->byteSize = 0;
	DEBUG_ALLOC(tkTextCountNewUndoToken++);
    }

    DeleteRange(sharedTextPtr, firstPtr, lastPtr, myFlags, redoInfo);

    assert(segPtr1 != segPtr2);
    CleanupSplitPoint(segPtr1, sharedTextPtr);
    CleanupSplitPoint(segPtr2, sharedTextPtr);

    /*
     * The indices are no longer valid.
     */

    DEBUG(TkTextIndexInvalidate(indexPtr1));
    DEBUG(TkTextIndexInvalidate(indexPtr2));

    TK_BTREE_DEBUG(TkBTreeCheck(sharedTextPtr->tree));
}

void
TkBTreeDeleteIndexRange(
    TkSharedText *sharedTextPtr,/* Handle to shared text resource. */
    TkTextIndex *indexPtr1,	/* Indicates first character that is to be deleted. */
    TkTextIndex *indexPtr2,	/* Indicates character just after the last one that is to be deleted. */
    int flags,			/* Flags controlling the deletion. */
    TkTextUndoInfo *undoInfo)	/* Store undo information, can be NULL. */
{
    DeleteIndexRange(sharedTextPtr, indexPtr1, indexPtr2, flags, NULL, undoInfo);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeFindLine --
 *
 *	Find a particular line in a B-tree based on its line number.
 *
 * Results:
 *	The return value is a pointer to the line structure for the line whose
 *	index is "line", or NULL if no such line exists.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkTextLine *
TkBTreeFindLine(
    TkTextBTree tree,		/* B-tree in which to find line. */
    const TkText *textPtr,	/* Relative to this client of the B-tree. */
    unsigned line)		/* Index of desired line. */
{
    BTree *treePtr = (BTree *) tree;
    Node *nodePtr;
    TkTextLine *linePtr;

    assert(tree || textPtr);

    if (!treePtr) {
	tree = textPtr->sharedTextPtr->tree;
	treePtr = (BTree *) tree;
    }

    nodePtr = treePtr->rootPtr;
    if (nodePtr->numLines <= line) {
	return NULL;
    }

    /*
     * Check for any start/end offset for this text widget.
     */

    if (textPtr) {
	line += TkBTreeLinesTo(tree, NULL, TkBTreeGetStartLine(textPtr), NULL);
	if (line >= nodePtr->numLines) {
	    return NULL;
	}
	if (line > TkBTreeLinesTo(tree, NULL, TkBTreeGetLastLine(textPtr), NULL)) {
	    return NULL;
	}
    }

    if (line == 0) {
	return nodePtr->linePtr;
    }
    if (line == nodePtr->numLines - 1) {
	return nodePtr->lastPtr;
    }

    /*
     * Work down through levels of the tree until a node is found at level 0.
     */

    while (nodePtr->level > 0) {
	for (nodePtr = nodePtr->childPtr;
		nodePtr && nodePtr->numLines <= line;
		nodePtr = nodePtr->nextPtr) {
	    line -= nodePtr->numLines;
	}
	assert(nodePtr);
    }

    /*
     * Work through the lines attached to the level-0 node.
     */

    for (linePtr = nodePtr->linePtr; line > 0; linePtr = linePtr->nextPtr, --line) {
	assert(linePtr != nodePtr->lastPtr->nextPtr);
    }
    return linePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeFindPixelLine --
 *
 *	Find a particular line in a B-tree based on its pixel count.
 *
 * Results:
 *	The return value is a pointer to the line structure for the line which
 *	contains the pixel "pixels", or NULL if no such line exists. If the
 *	first line is of height 20, then pixels 0-19 will return it, and
 *	pixels = 20 will return the next line.
 *
 *	If pixelOffset is non-NULL, it is set to the amount by which 'pixels'
 *	exceeds the first pixel located on the returned line. This should
 *	always be non-negative.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkTextLine *
TkBTreeFindPixelLine(
    TkTextBTree tree,		/* B-tree to use. */
    const TkText *textPtr,	/* Relative to this client of the B-tree. */
    int pixels,			/* Pixel index of desired line. */
    int32_t *pixelOffset)	/* Used to return offset. */
{
    BTree *treePtr = (BTree *) tree;
    Node *nodePtr;
    TkTextLine *linePtr;
    unsigned pixelReference;

    assert(textPtr);
    assert(textPtr->pixelReference != -1);

    pixelReference = textPtr->pixelReference;
    nodePtr = treePtr->rootPtr;

    if (0 > pixels) {
	return NULL;
    }
    if (pixels >= (int)nodePtr->pixelInfo[pixelReference].pixels) {
	return TkBTreeGetLastLine(textPtr);
    }

    /*
     * Work down through levels of the tree until a node is found at level 0.
     */

    while (nodePtr->level != 0) {
	for (nodePtr = nodePtr->childPtr;
		(int)nodePtr->pixelInfo[pixelReference].pixels <= pixels;
		nodePtr = nodePtr->nextPtr) {
	    assert(nodePtr);
	    pixels -= nodePtr->pixelInfo[pixelReference].pixels;
	}
    }

    /*
     * Work through the lines attached to the level-0 node.
     */

    for (linePtr = nodePtr->linePtr;
	    (int)linePtr->pixelInfo[pixelReference].height <= pixels;
	    linePtr = linePtr->nextPtr) {
	assert(linePtr != nodePtr->lastPtr->nextPtr);
	pixels -= linePtr->pixelInfo[pixelReference].height;
    }

    assert(linePtr);

    if (textPtr->endMarker != textPtr->sharedTextPtr->endMarker) {
	TkTextLine *endLinePtr = textPtr->endMarker->sectionPtr->linePtr;

	if (TkBTreeLinesTo(tree, textPtr, linePtr, NULL) >
		TkBTreeLinesTo(tree, textPtr, endLinePtr, NULL)) {
	    linePtr = endLinePtr;
	}
    }

    if (pixelOffset) {
	*pixelOffset = pixels;
    }

    return linePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreePixelsTo --
 *
 *	Given a pointer to a line in a B-tree, return the numerical pixel
 *	index of the top of that line (i.e. the result does not include the
 *	height of the logical line for given line).
 *
 *	Since the last line of text (the artificial one) has zero height by
 *	defintion, calling this with the last line will return the total
 *	number of pixels in the widget.
 *
 * Results:
 *	The result is the pixel height of the top of the logical line which
 *	belongs to given line.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned
TkBTreePixelsTo(
    const TkText *textPtr,	/* Relative to this client of the B-tree. */
    const TkTextLine *linePtr)	/* Pointer to existing line in B-tree. */
{
    const TkSharedText *sharedTextPtr;
    Node *nodePtr, *parentPtr;
    unsigned pixelReference;
    unsigned index;

    assert(textPtr);
    assert(textPtr->pixelReference != -1);

    if (linePtr == TkBTreeGetStartLine(textPtr)) {
	return 0;
    }

    pixelReference = textPtr->pixelReference;
    sharedTextPtr = textPtr->sharedTextPtr;

    if (linePtr == TkBTreeGetLastLine(textPtr)) {
	index = ((BTree *) sharedTextPtr->tree)->rootPtr->pixelInfo[pixelReference].pixels;
    } else {
	linePtr = TkBTreeGetLogicalLine(sharedTextPtr, textPtr, (TkTextLine *) linePtr);

	/*
	 * First count how many pixels precede this line in its level-0 node.
	 */

	nodePtr = linePtr->parentPtr;
	index = 0;

	if (linePtr == nodePtr->lastPtr->nextPtr) {
	    index = nodePtr->pixelInfo[pixelReference].pixels;
	} else {
	    TkTextLine *linePtr2;

	    for (linePtr2 = nodePtr->linePtr; linePtr2 != linePtr; linePtr2 = linePtr2->nextPtr) {
		assert(linePtr2);
		assert(linePtr2->pixelInfo);
		index += linePtr2->pixelInfo[pixelReference].height;
	    }
	}

	/*
	 * Now work up through the levels of the tree one at a time, counting how
	 * many pixels are in nodes preceding the current node.
	 */

	for (parentPtr = nodePtr->parentPtr;
		parentPtr;
		nodePtr = parentPtr, parentPtr = parentPtr->parentPtr) {
	    Node *nodePtr2;

	    for (nodePtr2 = parentPtr->childPtr; nodePtr2 != nodePtr; nodePtr2 = nodePtr2->nextPtr) {
		assert(nodePtr2);
		index += nodePtr2->pixelInfo[pixelReference].pixels;
	    }
	}
    }

    return index;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeLinesTo --
 *
 *	Given a pointer to a line in a B-tree, return the numerical index of
 *	that line.
 *
 * Results:
 *	The result is the index of linePtr within the tree, where zero
 *	corresponds to the first line in the tree. also the derivation
 *	will be set (if given), in case that the given line is before
 *	first line in this widget the deviation will be positive, and
 *	if given line is after last line in this client then the deviation
 *	will be negative.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned
TkBTreeLinesTo(
    TkTextBTree tree,
    const TkText *textPtr,	/* Relative to this client of the B-tree, can be NULL. */
    const TkTextLine *linePtr,	/* Pointer to existing line in B-tree. */
    int *deviation)		/* Deviation to existing line, can be NULL. */
{
    const TkTextLine *linePtr2;
    const Node *nodePtr;
    const Node *parentPtr;
    const Node *nodePtr2;
    unsigned index;

    assert(linePtr);

    if (textPtr) {
	if (linePtr == textPtr->startMarker->sectionPtr->linePtr) {
	    if (deviation) { *deviation = 0; }
	    return 0;
	}
	if (!linePtr->nextPtr && textPtr->endMarker == textPtr->sharedTextPtr->endMarker) {
	    if (deviation) { *deviation = 0; }
	    return TkBTreeGetRoot(tree)->numLines - 1;
	}
    } else {
	if (!linePtr->prevPtr) {
	    if (deviation) { *deviation = 0; }
	    return 0;
	}
	if (!linePtr->nextPtr) {
	    if (deviation) { *deviation = 0; }
	    return TkBTreeGetRoot(tree)->numLines - 1;
	}
    }

    /*
     * First count how many lines precede this one in its level-0 node.
     */

    nodePtr = linePtr->parentPtr;
    index = 0;
    for (linePtr2 = nodePtr->linePtr; linePtr2 != linePtr; linePtr2 = linePtr2->nextPtr) {
	assert(linePtr2);
	index += 1;
    }

    /*
     * Now work up through the levels of the tree one at a time, counting how
     * many lines are in nodes preceding the current node.
     */

    for (parentPtr = nodePtr->parentPtr;
	    parentPtr;
	    nodePtr = parentPtr, parentPtr = parentPtr->parentPtr) {
	for (nodePtr2 = parentPtr->childPtr; nodePtr2 != nodePtr; nodePtr2 = nodePtr2->nextPtr) {
	    assert(nodePtr2);
	    index += nodePtr2->numLines;
	}
    }

    if (textPtr) {
        /*
         * The index to return must be relative to textPtr, not to the entire
         * tree. Take care to never return a negative index when linePtr
         * denotes a line before -startindex, or an index larger than the
         * number of lines in textPtr when linePtr is a line past -endindex.
         */

        unsigned indexStart, indexEnd;

	indexStart = TkBTreeLinesTo(tree, NULL, TkBTreeGetStartLine(textPtr), NULL);
	indexEnd = TkBTreeLinesTo(tree, NULL, TkBTreeGetLastLine(textPtr), NULL);

        if (index < indexStart) {
	    if (deviation) { *deviation = indexStart - index; }
            index = 0;
        } else if (index > indexEnd) {
	    if (deviation) { *deviation = indexEnd - index; }
            index = indexEnd;
        } else {
	    if (deviation) { *deviation = 0; }
            index -= indexStart;
        }
    } else if (deviation) {
	*deviation = 0;
    }

    return index;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeLinkSegment --
 *
 *	This function adds a new segment to a B-tree at a given location.
 *	This function cannot be used for char segments, or for switches.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	'segPtr' will be linked into its tree.
 *
 *----------------------------------------------------------------------
 */

void
TkBTreeLinkSegment(
    const TkSharedText *sharedTextPtr,
    				/* Handle to shared text resource. */
    TkTextSegment *segPtr,	/* Pointer to new segment to be added to
				 * B-tree. Should be completely initialized by caller except for
				 * nextPtr field. */
    TkTextIndex *indexPtr)	/* Where to add segment: it gets linked in just before the segment
    				 * indicated here. */
{
    TkTextSegment *prevPtr;
    TkTextLine *linePtr;

    assert(!segPtr->sectionPtr); /* otherwise still in use */
    assert(segPtr->typePtr->group != SEG_GROUP_CHAR);
    assert(segPtr->typePtr->group != SEG_GROUP_PROTECT);
    assert(segPtr->typePtr->group != SEG_GROUP_BRANCH);
    assert(segPtr->size == 0 || segPtr->tagInfoPtr || indexPtr->textPtr);

    linePtr = TkTextIndexGetLine(indexPtr);

    if (sharedTextPtr->steadyMarks) {
	prevPtr = TkTextIndexGetSegment(indexPtr);

	if (prevPtr && prevPtr->typePtr->group == SEG_GROUP_MARK) {
	    /*
	     * We have steady marks, and the insertion point is a mark segment,
	     * so insert the new segment according to the gravity of this mark.
	     */

	    if (prevPtr->typePtr == &tkTextRightMarkType) {
		prevPtr = prevPtr->prevPtr;
	    }
	} else {
	    prevPtr = SplitSeg(indexPtr, NULL);
	}
    } else {
	prevPtr = SplitSeg(indexPtr, NULL);
    }

    if (segPtr->typePtr->group == SEG_GROUP_MARK) {
	LinkMark(sharedTextPtr, linePtr, prevPtr, segPtr);
    } else {
	LinkSegment(linePtr, prevPtr, segPtr);
    }
    SplitSection(segPtr->sectionPtr);
    TkBTreeIncrEpoch(indexPtr->tree);

    if (segPtr->size > 0) {
	TkTextSegment *prevPtr = segPtr->prevPtr;
	TkTextSegment *nextPtr = segPtr->nextPtr;
	TkTextTagSet *tagoffPtr;
	Node *nodePtr;

	SetLineHasChanged(sharedTextPtr, linePtr);

	/*
	 * We have to update the tag information of the line and the related node.
	 */

	while (prevPtr && !prevPtr->tagInfoPtr) {
	    prevPtr = prevPtr->prevPtr;
	}
	while (nextPtr && !nextPtr->tagInfoPtr) {
	    nextPtr = nextPtr->nextPtr;
	}

	if (segPtr->tagInfoPtr) {
	    linePtr->tagonPtr = TkTextTagSetJoin(linePtr->tagonPtr, segPtr->tagInfoPtr);
	} else {
	    segPtr->tagInfoPtr = MakeTagInfo(indexPtr->textPtr, segPtr);
	}

	TkTextTagSetIncrRefCount(tagoffPtr = sharedTextPtr->emptyTagInfoPtr);
	if (prevPtr) { tagoffPtr = TkTextTagSetJoin(tagoffPtr, prevPtr->tagInfoPtr); }
	if (nextPtr) { tagoffPtr = TkTextTagSetJoin(tagoffPtr, nextPtr->tagInfoPtr); }
	tagoffPtr = TkTextTagSetRemove(tagoffPtr, segPtr->tagInfoPtr);

	if (!TkTextTagSetContains(linePtr->tagoffPtr, tagoffPtr)) {
	    linePtr->tagoffPtr = TkTextTagSetJoin(linePtr->tagoffPtr, tagoffPtr);
	    AddTagoffToNode(linePtr->parentPtr, tagoffPtr);
	}
	TkTextTagSetDecrRefCount(tagoffPtr);

	/*
	 * Propagate change of size in B-Tree.
	 */

	for (nodePtr = linePtr->parentPtr; nodePtr; nodePtr = nodePtr->parentPtr) {
	    nodePtr->size += segPtr->size;
	}
    }

    TK_BTREE_DEBUG(TkBTreeCheck(indexPtr->tree));
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeUnlinkSegment --
 *
 *	This function unlinks a segment from its line in a B-tree.
 *	This function cannot be used for char segments, or for switches.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	SegPtr will be unlinked from linePtr. The segment itself isn't
 *	modified by this function, but the section containing this
 *	segment will be modified.
 *
 *----------------------------------------------------------------------
 */

void
TkBTreeUnlinkSegment(
    const TkSharedText *sharedTextPtr,
    TkTextSegment *segPtr)	/* Segment to be unlinked. */
{
    TkTextSegment *prevPtr;
    TkTextSection *sectionPtr;
    TkTextLine *linePtr;

    assert(segPtr->typePtr != &tkTextCharType);
    assert(segPtr->typePtr != &tkTextLinkType);
    assert(segPtr->typePtr != &tkTextBranchType);
    assert(segPtr->typePtr != &tkTextHyphenType);
    assert(segPtr->typePtr->group != SEG_GROUP_PROTECT);
    assert(segPtr->typePtr->group != SEG_GROUP_BRANCH);

    prevPtr = segPtr->prevPtr;
    sectionPtr = segPtr->sectionPtr;
    assert(sectionPtr); /* segment is already freed? */
    assert(sectionPtr->linePtr); /* section is already freed? */
    UnlinkSegment(segPtr);
    linePtr = sectionPtr->linePtr;
    JoinSections(sectionPtr);
    if (prevPtr && prevPtr->typePtr == &tkTextCharType) {
	CleanupCharSegments(sharedTextPtr, prevPtr);
    }
    TkBTreeIncrEpoch(sharedTextPtr->tree);

    assert((segPtr->size == 0) == !segPtr->tagInfoPtr);

    if (segPtr->size > 0) {
	Node *nodePtr;

	SetLineHasChanged(sharedTextPtr, linePtr);

	if (!TkTextTagSetIsEmpty(linePtr->tagoffPtr)) {
	    RecomputeLineTagInfo(sectionPtr->linePtr, NULL, sharedTextPtr);
	    UpdateNodeTags(sharedTextPtr, sectionPtr->linePtr->parentPtr);
	}

	/*
	 * Propagate change of size in B-Tree.
	 */

	for (nodePtr = linePtr->parentPtr; nodePtr; nodePtr = nodePtr->parentPtr) {
	    nodePtr->size -= segPtr->size;
	}
    }

    TK_BTREE_DEBUG(if (!segPtr->startEndMarkFlag) TkBTreeCheck(sharedTextPtr->tree));
}

/*
 *----------------------------------------------------------------------
 *
 * AddTagToNode --
 *
 *	Add the specified tag to the given node, so we can check whether
 *	any segment in this node contains this tag.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the tag information of some nodes in the B-Tree.
 *
 *----------------------------------------------------------------------
 */

static unsigned
CountChildsWithTag(
    const Node *nodePtr,
    unsigned tagIndex)
{
    unsigned count = 0;

    if (nodePtr->level == 0) {
	const TkTextLine *linePtr;
	const TkTextLine *lastPtr = nodePtr->lastPtr->nextPtr;

	for (linePtr = nodePtr->linePtr; linePtr != lastPtr; linePtr = linePtr->nextPtr) {
	    if (TkTextTagSetTest(linePtr->tagonPtr, tagIndex)) {
		count += 1;
	    }
	}
    } else {
	const Node *childPtr;

	for (childPtr = nodePtr->childPtr; childPtr; childPtr = childPtr->nextPtr) {
	    if (TkTextTagSetTest(childPtr->tagonPtr, tagIndex)) {
		count += 1;
	    }
	}
    }

    return count;
}

static void
AddTagToNode(
    Node *nodePtr,
    TkTextTag *tagPtr,
    bool setTagoff)
{
    unsigned rootLevel;

    assert(tagPtr);
    assert(!tagPtr->isDisabled);
    assert(nodePtr->level == 0);

    if (!tagPtr->rootPtr) {
	tagPtr->rootPtr = nodePtr;
    }

    rootLevel = tagPtr->rootPtr->level;

    do {
	TkTextTagSet *tagInfoPtr = TagSetTestAndSet(nodePtr->tagonPtr, tagPtr);

	if (!tagInfoPtr) {
	    Node *rootPtr = nodePtr;

	    /*
	     * This tag is already included, but possibly we have to push up the tag root.
	     */

	    while (rootLevel < rootPtr->level) {
		rootLevel = (tagPtr->rootPtr = tagPtr->rootPtr->parentPtr)->level;
	    }
	    while (rootLevel == rootPtr->level && rootPtr != tagPtr->rootPtr) {
		rootLevel = (tagPtr->rootPtr = tagPtr->rootPtr->parentPtr)->level;
		rootPtr = rootPtr->parentPtr;
	    }

	    if (setTagoff) {
		/*
		 * And still we have to propagate the tagoff information.
		 */

		do {
		    if (!(tagInfoPtr = TagSetTestAndSet(nodePtr->tagoffPtr, tagPtr))) {
			return;
		    }
		    nodePtr->tagoffPtr = tagInfoPtr;
		} while ((nodePtr = nodePtr->parentPtr));
	    }

	    return;
	}

	nodePtr->tagonPtr = tagInfoPtr;

	if (setTagoff) {
	    nodePtr->tagoffPtr = TagSetAdd(nodePtr->tagoffPtr, tagPtr);
	} else {
	    unsigned nchilds = CountChildsWithTag(nodePtr, tagPtr->index);

	    if (nchilds == 0) {
		nodePtr->tagoffPtr = TagSetErase(nodePtr->tagoffPtr, tagPtr);
		assert(!nodePtr->parentPtr || nodePtr->parentPtr->numChildren > 1);
		setTagoff = true; /* but parent now has tagoff */
	    } else if (nchilds < nodePtr->numLines) {
		nodePtr->tagoffPtr = TagSetAdd(nodePtr->tagoffPtr, tagPtr);
		setTagoff = true; /* propagate to parent */
	    }
	}

	if (rootLevel == nodePtr->level && nodePtr != tagPtr->rootPtr) {
	    /*
	     * The old tag root is at the same level in the tree as this node,
	     * but it isn't at this node. Move the tag root up one level.
	     */
	    rootLevel = (tagPtr->rootPtr = tagPtr->rootPtr->parentPtr)->level;
	}
    } while ((nodePtr = nodePtr->parentPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * RemoveTagFromNode --
 *
 *	Remove the specified tag from the given node, so we can check whether
 *	any segment in this node contains this tag.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the tag information of some nodes in the B-Tree.
 *
 *----------------------------------------------------------------------
 */

static void
RemoveTagFromNode(
    Node *nodePtr,
    TkTextTag *tagPtr)
{
    Node *parentPtr;

    assert(tagPtr);
    assert(!tagPtr->isDisabled);
    assert(nodePtr->level == 0);
    assert(TkTextTagSetTest(nodePtr->tagonPtr, tagPtr->index));

    nodePtr->tagonPtr = TagSetErase(nodePtr->tagonPtr, tagPtr);
    nodePtr->tagoffPtr = TagSetErase(nodePtr->tagoffPtr, tagPtr);

    if (nodePtr == tagPtr->rootPtr) {
	tagPtr->rootPtr = NULL;

	while ((nodePtr = nodePtr->parentPtr)) {
	    nodePtr->tagonPtr = TagSetErase(nodePtr->tagonPtr, tagPtr);
	    nodePtr->tagoffPtr = TagSetErase(nodePtr->tagoffPtr, tagPtr);
	};
    } else if ((parentPtr = nodePtr->parentPtr)) {
	unsigned tagIndex = tagPtr->index;
	Node *childPtr = NULL;

	tagPtr->rootPtr = NULL;

	do {
	    unsigned count = 0;

	    /*
	     * Test if any of the children is still referencing the tag.
	     */

	    for (nodePtr = parentPtr->childPtr; nodePtr; nodePtr = nodePtr->nextPtr) {
		if (TkTextTagSetTest(nodePtr->tagonPtr, tagIndex)) {
		    if (!childPtr) { childPtr = nodePtr; }
		    count += 1;
		}
	    }

	    if (count == 0) {
		parentPtr->tagonPtr = TagSetErase(parentPtr->tagonPtr, tagPtr);
		parentPtr->tagoffPtr = TagSetErase(parentPtr->tagoffPtr, tagPtr);
	    } else {
		if (count > 1) {
		    /* this is now the best candidate for pushing down the root */
		    tagPtr->rootPtr = parentPtr;
		}
		parentPtr->tagoffPtr = TagSetAdd(parentPtr->tagoffPtr, tagPtr);
	    }
	} while ((parentPtr = parentPtr->parentPtr));

	if (childPtr && !tagPtr->rootPtr) {
	    /*
	     * We have to search down for a new tag root.
	     */

	    tagPtr->rootPtr = childPtr;

	    while (childPtr->level > 0) {
		unsigned count = 0;

		for (nodePtr = childPtr->childPtr; nodePtr; nodePtr = nodePtr->nextPtr) {
		    if (TkTextTagSetTest(nodePtr->tagonPtr, tagIndex)) {
			childPtr = nodePtr;
			count += 1;
		    }
		}

		assert(count > 0);

		if (count > 1) {
		    break;
		}

		tagPtr->rootPtr = childPtr;
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeUpdateElideInfo --
 *
 *	This function will be called if the elide info of any tag has been
 *	changed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Some branches and links may be inserted, or removed.
 *
 *----------------------------------------------------------------------
 */

static void
PropagateChangeToLineCount(
    Node *nodePtr,
    int changeToLogicalLineCount)
{
    if (changeToLogicalLineCount) {
	for ( ; nodePtr; nodePtr = nodePtr->parentPtr) {
	    nodePtr->numLogicalLines += changeToLogicalLineCount;
	}
    }
}

static TkTextSegment *
FindNextLink(
    const TkSharedText *sharedTextPtr,
    TkTextSegment *segPtr)
{
    TkTextSection *sectionPtr = segPtr->sectionPtr;
    TkTextLine *linePtr = sectionPtr->linePtr;

    if (linePtr->numLinks > 0) {
	sectionPtr = sectionPtr->nextPtr;
	while (sectionPtr) {
	    if (sectionPtr->segPtr->typePtr == &tkTextLinkType) {
		return sectionPtr->segPtr;
	    }
	}
    }

    linePtr = TkBTreeNextLogicalLine(sharedTextPtr, NULL, linePtr)->prevPtr;
    assert(linePtr);
    if (linePtr->numLinks == 0) {
	linePtr = linePtr->nextPtr;
	assert(linePtr);
    }
    sectionPtr = linePtr->segPtr->sectionPtr;

    while (true) {
	if (sectionPtr->segPtr->typePtr == &tkTextLinkType) {
	    return sectionPtr->segPtr;
	}
	sectionPtr = sectionPtr->nextPtr;
	assert(sectionPtr);
    }

    return NULL; /* never reached */
}

static void
UpdateElideInfo(
    TkSharedText *sharedTextPtr,
    TkTextTag *tagPtr,		/* can be NULL */
    TkTextSegment **firstSegPtr,
    TkTextSegment **lastSegPtr,
    unsigned reason)
{
    TkTextSegment *prevBranchPtr;
    TkTextSegment *lastBranchPtr;
    TkTextSegment *prevLinkPtr;
    TkTextSegment *lastLinkPtr;
    TkTextSegment *newBranchPtr;
    TkTextSegment *startSegPtr;
    TkTextSegment *deletedBranchPtr;
    TkTextSegment *deletedLinkPtr;
    TkTextSegment *endSegPtr;
    TkTextSegment *segPtr;
    TkTextLine *linePtr;
    TkTextLine *lastLinePtr;
    TkTextLine *startLinePtr;
    TkTextLine *endLinePtr;
    TkText *oldTextPtr;
    TkText *textPtr;
    Node *nodePtr;
    bool anyChanges;
    bool actualElidden;
    int changeToLogicalLineCount;

    /*
     * --------------------------------------------------------------------------
     * This function will be called in four cases:
     * --------------------------------------------------------------------------
     * 1. A tag with elide information has been added to the specified region.
     *
     * 2. A tag with elide information will be removed from the specified region.
     *
     *		Because the removal has not yet been done, we will temporarily
     *		disable this tag for further processing of the region.
     *
     * 3. The elide state of a tag has been changed.
     *
     *		Here we need the elide state of the predecessing segment before
     *		the tag has been changed, thus we will temporarily reset the elide
     *		state as long as we are computing this predecessing elide state.
     *
     * 4. All tags will be removed from the specified region.
     */

    assert(tagPtr || reason == ELISION_WILL_BE_REMOVED);
    assert(tagPtr || TkBTreeHaveElidedSegments(sharedTextPtr));

    /*
     * This function assumes that the start/end points are already protected.
     */

    assert((*firstSegPtr)->protectionFlag);
    assert((*lastSegPtr)->protectionFlag);

    linePtr = (*firstSegPtr)->sectionPtr->linePtr;
    prevBranchPtr = lastBranchPtr = newBranchPtr = NULL;
    deletedBranchPtr = deletedLinkPtr = NULL;
    prevLinkPtr = lastLinkPtr = NULL;
    anyChanges = false;
    oldTextPtr = textPtr = NULL;
    lastLinePtr = (*lastSegPtr)->sectionPtr->linePtr;
    changeToLogicalLineCount = 0;
    nodePtr = NULL;
    startLinePtr = NULL;
    endLinePtr = NULL;

    /*
     * Ensure that the range will include final branches.
     */

    endSegPtr = *lastSegPtr;
    while (endSegPtr->size == 0) {
	endSegPtr = endSegPtr->nextPtr;
	assert(endSegPtr);
    }
    if (!(endSegPtr = endSegPtr->nextPtr)) {
	endSegPtr = lastLinePtr->nextPtr ? lastLinePtr->nextPtr->segPtr : lastLinePtr->segPtr;
    }
    while (endSegPtr->size == 0) {
	endSegPtr = endSegPtr->nextPtr;
	assert(endSegPtr);
    }

    /*
     * Prepare the tag for finding the actual elide state.
     */

    if (tagPtr && reason == ELISION_HAS_BEEN_CHANGED) {
	tagPtr->elide = !tagPtr->elide;
    }

    /*
     * At first find the elide state of the segment which is predecessing the
     * specified region.
     */

    startSegPtr = *firstSegPtr;
    do {
	if (!(startSegPtr = startSegPtr->prevPtr) && linePtr->prevPtr) {
	    startSegPtr = linePtr->prevPtr->lastPtr;
	}
    } while (startSegPtr && !startSegPtr->tagInfoPtr);

    actualElidden = startSegPtr && SegmentIsElided(sharedTextPtr, startSegPtr, NULL);

    /*
     * Now find next segment for start of range.
     */

    if (startSegPtr) {
	startSegPtr = startSegPtr->nextPtr;
    }
    if (!startSegPtr) {
	startSegPtr = (*firstSegPtr)->sectionPtr->linePtr->segPtr;
    }

    /*
     * We have found the predecessing elide state, now reset/prepare the tag
     * for further processing.
     */

    if (tagPtr) {
	if (reason == ELISION_HAS_BEEN_CHANGED) {
	    tagPtr->elide = !tagPtr->elide;
	} else if (reason == ELISION_WILL_BE_REMOVED) {
	    oldTextPtr = tagPtr->textPtr;
	    /* this little trick is disabling the tag */
	    tagPtr->textPtr = (TkText *) tagPtr;
	    textPtr = sharedTextPtr->peers;
	}
    }

    endSegPtr->protectionFlag = true;
    linePtr = startSegPtr->sectionPtr->linePtr;
    lastLinePtr = (*lastSegPtr)->sectionPtr->linePtr;
    segPtr = startSegPtr;
    SetLineHasChanged(sharedTextPtr, linePtr);

    while (true) {
	if (!segPtr) {
	    if (anyChanges) {
		/*
		 * The branches and links are influencing the section structure.
		 */
		RebuildSections(sharedTextPtr, linePtr, true);
		TkBTreeIncrEpoch(sharedTextPtr->tree);
	    }

	    anyChanges = false;
	    if (linePtr->logicalLine) {
		linePtr->changed = true;
	    }
	    linePtr = linePtr->nextPtr;
	    assert(linePtr);

	    if (linePtr != endSegPtr->sectionPtr->linePtr) {
		while (linePtr != lastLinePtr
			&& linePtr->numLinks == 0
			&& linePtr->numBranches == 0
			&& !TestTag(linePtr->tagonPtr, tagPtr)) {
		    /* Skip (nearly) unaffected line. */
		    if (linePtr->logicalLine == actualElidden) {
			if (nodePtr && linePtr->parentPtr != nodePtr) {
			    PropagateChangeToLineCount(nodePtr, changeToLogicalLineCount);
			    changeToLogicalLineCount = 0;
			}
			changeToLogicalLineCount += linePtr->logicalLine ? -1 : +1;
			linePtr->logicalLine = !actualElidden;
			nodePtr = linePtr->parentPtr;
			endLinePtr = linePtr;
		    }
		    if (linePtr->logicalLine) {
			linePtr->changed = true;
		    }
		    linePtr = linePtr->nextPtr;
		}
	    }

	    if (linePtr->logicalLine == actualElidden) {
		if (nodePtr && linePtr->parentPtr != nodePtr) {
		    PropagateChangeToLineCount(nodePtr, changeToLogicalLineCount);
		    changeToLogicalLineCount = 0;
		}
		changeToLogicalLineCount += linePtr->logicalLine ? -1 : +1;
		linePtr->logicalLine = !actualElidden;
		nodePtr = linePtr->parentPtr;
		endLinePtr = linePtr;
	    }

	    segPtr = linePtr->segPtr;
	}
	if (segPtr->tagInfoPtr) {
	    bool shouldBeElidden = tagPtr ? SegmentIsElided(sharedTextPtr, segPtr, textPtr) : false;
	    bool somethingHasChanged = false;

	    if (prevBranchPtr) {
		if (!shouldBeElidden || actualElidden) {
		    /*
		     * Remove expired branch.
		     */

		    assert(TkBTreeHaveElidedSegments(sharedTextPtr));
		    assert(prevBranchPtr->sectionPtr->linePtr->numBranches > 0);

		    if (prevBranchPtr == *firstSegPtr) {
			(*firstSegPtr = (*firstSegPtr)->nextPtr)->protectionFlag = true;
		    }
		    if (prevBranchPtr == *lastSegPtr) {
			(*lastSegPtr = (*lastSegPtr)->nextPtr)->protectionFlag = true;
		    }
		    UnlinkSegmentAndCleanup(sharedTextPtr, prevBranchPtr);
		    if (deletedBranchPtr) {
			TkBTreeFreeSegment(prevBranchPtr);
		    } else {
			deletedBranchPtr = prevBranchPtr;
		    }
		    lastBranchPtr = NULL;
		    somethingHasChanged = true;
		}
	    } else if (prevLinkPtr) {
		if (shouldBeElidden || !actualElidden) {
		    /*
		     * Remove expired link.
		     */

		    if (prevLinkPtr == *firstSegPtr) {
			(*firstSegPtr = (*firstSegPtr)->nextPtr)->protectionFlag = true;
		    }
		    if (prevLinkPtr == *lastSegPtr) {
			(*lastSegPtr = (*lastSegPtr)->nextPtr)->protectionFlag = true;
		    }
		    UnlinkSegmentAndCleanup(sharedTextPtr, prevLinkPtr);
		    if (deletedLinkPtr) {
			TkBTreeFreeSegment(prevLinkPtr);
		    } else {
			deletedLinkPtr = prevLinkPtr;
		    }
		    lastBranchPtr = NULL;
		    somethingHasChanged = true;
		}
	    } else if (actualElidden != shouldBeElidden) {
		if (shouldBeElidden) {
		    /*
		     * We have to insert a branch.
		     */

		    if (deletedBranchPtr) {
			lastBranchPtr = deletedBranchPtr;
			deletedBranchPtr = NULL;
		    } else {
			lastBranchPtr = MakeBranch();
		    }
		    LinkSwitch(linePtr, segPtr->prevPtr, lastBranchPtr);
		    newBranchPtr = lastBranchPtr;
		    somethingHasChanged = true;
		} else { /* if (!actualElidden) */
		    /*
		     * We have to insert a link.
		     */

		    if (!lastBranchPtr) {
			/*
			 * The related branch is starting outside of this range,
			 * so we have to search for it.
			 */
			lastBranchPtr = TkBTreeFindStartOfElidedRange(sharedTextPtr, NULL, *firstSegPtr);
			assert(lastBranchPtr->typePtr == &tkTextBranchType);
		    }

		    if (deletedLinkPtr) {
			lastLinkPtr = deletedLinkPtr;
			deletedLinkPtr = NULL;
		    } else {
			lastLinkPtr = MakeLink();
		    }

		    /* connect the branches */
		    lastBranchPtr->body.branch.nextPtr = lastLinkPtr;
		    lastLinkPtr->body.link.prevPtr = lastBranchPtr;
		    /* finally link new segment */
		    LinkSwitch(linePtr, segPtr->prevPtr, lastLinkPtr);
		    newBranchPtr = lastBranchPtr = NULL;
		    somethingHasChanged = true;
		}
	    }

	    if (somethingHasChanged) {
		if (!startLinePtr) { startLinePtr = linePtr; }
		endLinePtr = linePtr;
		lastLinkPtr = NULL;
		anyChanges = true;
	    }

	    actualElidden = shouldBeElidden;
	    prevBranchPtr = prevLinkPtr = NULL;
	} else if (segPtr->typePtr == &tkTextBranchType) {
	    lastBranchPtr = prevBranchPtr = segPtr;
	    lastLinkPtr = prevLinkPtr = NULL;
	} else if (segPtr->typePtr == &tkTextLinkType) {
	    lastBranchPtr = prevBranchPtr = NULL;
	    lastLinkPtr = prevLinkPtr = segPtr;
	}
	if (segPtr == endSegPtr) {
	    break;
	}
	segPtr = segPtr->nextPtr;
    }

    if (newBranchPtr) {
	/*
	 * Connect the inserted branch.
	 */

	if (!lastLinkPtr) {
	    if (reason == ELISION_HAS_BEEN_CHANGED) { tagPtr->elide = !tagPtr->elide; }
	    actualElidden = SegmentIsElided(sharedTextPtr, endSegPtr, NULL);
	    if (reason == ELISION_HAS_BEEN_CHANGED) { tagPtr->elide = !tagPtr->elide; }

	    if (actualElidden) {
		/*
		 * In this case the related link is outside of the range,
		 * so we have to search for it.
		 */

		lastLinkPtr = FindNextLink(sharedTextPtr, *lastSegPtr);
		assert(lastLinkPtr);
	    } else {
		if (deletedLinkPtr) {
		    lastLinkPtr = deletedLinkPtr;
		    deletedLinkPtr = NULL;
		} else {
		    lastLinkPtr = MakeLink();
		}
		lastLinePtr = endSegPtr->sectionPtr->linePtr;
		LinkSwitch(lastLinePtr, endSegPtr->prevPtr, lastLinkPtr);
		if (linePtr == lastLinePtr) {
		    anyChanges = true;
		} else {
		    RebuildSections(sharedTextPtr, lastLinePtr, true);
		}
	    }
	}

	newBranchPtr->body.branch.nextPtr = lastLinkPtr;
	lastLinkPtr->body.link.prevPtr = newBranchPtr;
    }

    if (deletedBranchPtr) { TkBTreeFreeSegment(deletedBranchPtr); }
    if (deletedLinkPtr) { TkBTreeFreeSegment(deletedLinkPtr); }

    if (linePtr->logicalLine) {
	linePtr->changed = true;
    }

    if (anyChanges) {
	/* The branches and links are influencing the section structure. */
	RebuildSections(sharedTextPtr, linePtr, true);
    }

    if (endSegPtr != *lastSegPtr) {
	CleanupSplitPoint(endSegPtr, sharedTextPtr);
    }

    if (nodePtr) {
	PropagateChangeToLineCount(nodePtr, changeToLogicalLineCount);
    }

    if (startLinePtr) {
	unsigned lineNo1 = TkBTreeLinesTo(sharedTextPtr->tree, NULL, startLinePtr, NULL);
	unsigned lineNo2 = TkBTreeLinesTo(sharedTextPtr->tree, NULL, endLinePtr, NULL);

	if (!endLinePtr->nextPtr) {
	    assert(lineNo1 < lineNo2);
	    lineNo2 -= 1; /* don't invalidate very last line */
	}

	TkTextInvalidateLineMetrics(sharedTextPtr, NULL, startLinePtr,
		lineNo2 - lineNo1, TK_TEXT_INVALIDATE_ELIDE);
    }

    if (tagPtr && reason == ELISION_WILL_BE_REMOVED) {
	/* Re-enable the tag. */
	tagPtr->textPtr = oldTextPtr;
    }
}

void
TkBTreeUpdateElideInfo(
    TkText *textPtr,
    TkTextTag *tagPtr)
{
    TkSharedText *sharedTextPtr;
    TkTextIndex index1, index2;
    TkTextSearch search;

    assert(textPtr);
    assert(tagPtr);

    sharedTextPtr = textPtr->sharedTextPtr;

    if (!tagPtr->elide && !TkBTreeHaveElidedSegments(sharedTextPtr)) {
	return;
    }

    TkTextIndexSetupToStartOfText(&index1, textPtr, sharedTextPtr->tree);
    TkTextIndexSetupToEndOfText(&index2, textPtr, sharedTextPtr->tree);
    TkBTreeStartSearch(&index1, &index2, tagPtr, &search, SEARCH_NEXT_TAGON);

    while (TkBTreeNextTag(&search)) {
	TkTextSegment *firstSegPtr;

	firstSegPtr = search.segPtr;
	TkBTreeNextTag(&search);
	assert(search.segPtr);
	firstSegPtr->protectionFlag = true;
	search.segPtr->protectionFlag = true;

	UpdateElideInfo(sharedTextPtr, tagPtr, &firstSegPtr, &search.segPtr, ELISION_HAS_BEEN_CHANGED);

	CleanupSplitPoint(firstSegPtr, sharedTextPtr);
	if (firstSegPtr != search.segPtr) {
	    CleanupSplitPoint(search.segPtr, sharedTextPtr);
	}
    }

    TkBTreeIncrEpoch(sharedTextPtr->tree);
    TK_BTREE_DEBUG(TkBTreeCheck(sharedTextPtr->tree));
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeTag --
 *
 *	Turn a given tag on or off for a given range of characters in a B-tree
 *	of text.
 *
 * Results:
 *	True if the tags on any characters in the range were changed, and false
 *	otherwise (i.e. if the tag was already absent (add = false) or present
 *	(add = true) on the index range in question).
 *
 * Side effects:
 *	The given tag is added to the given range of characters in the tree or
 *	removed from all those characters, depending on the "add" argument.
 *	Furthermore some branches and links may be inserted, or removed.
 *
 *----------------------------------------------------------------------
 */

enum {
    HAS_TAGON  = (1 << 0),
    HAS_TAGOFF = (1 << 1),
    DID_SKIP   = (1 << 2)
};

enum {
    UNDO_NEEDED,
    UNDO_MERGED,
    UNDO_ANNIHILATED,
};

typedef struct {
    TkText *textPtr;
    unsigned lineNo1;
    unsigned lineNo2;
    TkTextTag *tagPtr;
    bool add;
    TkTextUndoInfo *undoInfo;
    TkTextTagChangedProc *changedProc;
    const TkTextTagSet *tagonPtr;
    const TkTextTagSet *addTagoffPtr;
    const TkTextTagSet *eraseTagoffPtr;
    const TkTextTagSet *tagInfoPtr;
    TkTextTagSet *newTagonPtr;
    TkTextTagSet *newAddTagoffPtr;
    TkTextTagSet *newEraseTagoffPtr;
    TkTextTagSet *newTagInfoPtr;
    TkTextSegment *firstSegPtr;
    TkTextSegment *lastSegPtr;
    int32_t firstOffset;
    int32_t lastOffset;
    int32_t lengthsBuf[200];
    int32_t *lengths;
    unsigned sizeOfLengths;
    unsigned capacityOfLengths;
    int32_t currLength;
} TreeTagData;

static void
SaveLength(
    TreeTagData *data)
{
    if (++data->sizeOfLengths == data->capacityOfLengths) {
	unsigned newCapacity = 2*data->capacityOfLengths;
	data->lengths = realloc(data->lengths == data->lengthsBuf ? NULL : data->lengths, newCapacity);
	data->capacityOfLengths = newCapacity;
    }

    data->lengths[data->sizeOfLengths - 1] = data->currLength;
    data->currLength = 0;
}

static void
AddLength(
    TreeTagData *data,
    int length)
{
    if (data->currLength < 0) {
	SaveLength(data);
    }
    data->currLength += length;
}

static void
SubLength(
    TreeTagData *data,
    int length)
{
    if (data->currLength > 0) {
	SaveLength(data);
    }
    if (data->sizeOfLengths > 0) {
	data->currLength -= length;
    }
}

static int
CompareIndices(
    const TkTextIndex *indexPtr1,
    const TkTextUndoIndex *indexPtr2)
{
    int cmp;

    if (indexPtr2->lineIndex == -1) {
	TkTextIndex index = *indexPtr1;
	TkTextIndexSetSegment(&index, indexPtr2->u.markPtr);
	return TkTextIndexCompare(indexPtr1, &index);
    }

    if ((cmp = TkTextIndexGetLineNumber(indexPtr1, NULL) - indexPtr2->lineIndex) == 0) {
	cmp = TkTextIndexGetByteIndex(indexPtr1) - indexPtr2->u.byteIndex;
    }

    return cmp;
}

static int
MergeTagUndoToken(
    TkSharedText *sharedTextPtr,
    const TkTextIndex *indexPtr1,
    const TkTextIndex *indexPtr2,
    const TreeTagData *data)
{
    UndoTokenTagChange *prevToken;
    TkTextTag *tagPtr = data->tagPtr;
    int cmp1, cmp2;
    bool remove;
    bool wholeRange;

    if (!tagPtr->recentTagAddRemoveToken || tagPtr->recentTagAddRemoveTokenIsNull) {
	return UNDO_NEEDED;
    }

    prevToken = (UndoTokenTagChange *) tagPtr->recentTagAddRemoveToken;

    assert(prevToken);
    assert(UNMARKED_INT(((UndoTokenTagChange *) prevToken)->tagPtr) == UNMARKED_INT(tagPtr));

    remove = POINTER_IS_MARKED(prevToken->tagPtr);
    cmp1 = CompareIndices(indexPtr1, &prevToken->startIndex);
    cmp2 = CompareIndices(indexPtr2, &prevToken->endIndex);
    wholeRange = data->sizeOfLengths == 0
	    && !((UndoTokenTagChange *) tagPtr->recentTagAddRemoveToken)->lengths;

    if (data->add == remove) {
	if (cmp1 <= 0 && cmp2 >= 0) {
	    if (!data->add || wholeRange) {
		free(prevToken->lengths);
		prevToken->lengths = NULL;
		return UNDO_ANNIHILATED;
	    }
	    return UNDO_NEEDED;
	}
	if (!wholeRange) {
	    return UNDO_NEEDED;
	}
	if (cmp1 < 0 && cmp2 <= 0 && CompareIndices(indexPtr2, &prevToken->startIndex) >= 0) {
	    MakeUndoIndex(sharedTextPtr, indexPtr1, &prevToken->startIndex, GRAVITY_LEFT);
	    if (cmp2 > 0) {
		MakeUndoIndex(sharedTextPtr, indexPtr2, &prevToken->endIndex, GRAVITY_RIGHT);
	    }
	    if (data->add) {
		UNMARK_POINTER(prevToken->tagPtr);
	    } else {
		MARK_POINTER(prevToken->tagPtr);
	    }
	    return UNDO_MERGED;
	}
	if (cmp2 > 0 && cmp1 >= 0 && CompareIndices(indexPtr1, &prevToken->endIndex) <= 0) {
	    if (cmp1 > 0) {
		MakeUndoIndex(sharedTextPtr, indexPtr1, &prevToken->startIndex, GRAVITY_LEFT);
	    }
	    MakeUndoIndex(sharedTextPtr, indexPtr2, &prevToken->endIndex, GRAVITY_RIGHT);
	    if (data->add) {
		UNMARK_POINTER(prevToken->tagPtr);
	    } else {
		MARK_POINTER(prevToken->tagPtr);
	    }
	    return UNDO_MERGED;
	}
    } else if (wholeRange) {
	int cmp3 = CompareIndices(indexPtr2, &prevToken->startIndex);
	int cmp4 = CompareIndices(indexPtr1, &prevToken->endIndex);

	if (cmp3 == 0 || cmp4 == 0 || (cmp1 <= 0 && cmp2 >= 0) || (cmp1 >= 0 && cmp2 <= 0)) {
	    if (cmp1 < 0) {
		MakeUndoIndex(sharedTextPtr, indexPtr1, &prevToken->startIndex, GRAVITY_LEFT);
	    }
	    if (cmp2 > 0) {
		MakeUndoIndex(sharedTextPtr, indexPtr2, &prevToken->endIndex, GRAVITY_RIGHT);
	    }
	    return UNDO_MERGED;
	}
    }

    return UNDO_NEEDED;
}

static unsigned
AddRemoveTag(
    TreeTagData *data,
    TkTextLine *linePtr,
    TkTextSegment *firstPtr,
    TkTextSegment *lastPtr,
    TkTextTagSet *(*addRemoveFunc)(TkTextTagSet *, const TkTextTag *))
{
    const TkTextTag *tagPtr = data->tagPtr;
    const TkSharedText *sharedTextPtr = tagPtr->sharedTextPtr;
    TkTextSegment *segPtr = firstPtr ? firstPtr : linePtr->segPtr;
    TkTextSegment *prevPtr = NULL;
    unsigned flags = 0;

    assert(tagPtr);

    while (segPtr != lastPtr) {
	TkTextSegment *nextPtr = segPtr->nextPtr;

	if (segPtr->tagInfoPtr) {
	    if (data->undoInfo) {
		if (TkTextTagSetTest(segPtr->tagInfoPtr, tagPtr->index) != data->add) {
		    AddLength(data, segPtr->size);
		    if (!data->firstSegPtr) {
			data->firstSegPtr = segPtr;
		    }
		    data->lastSegPtr = segPtr;
		    data->lastOffset = segPtr->size;
		} else {
		    SubLength(data, segPtr->size);
		}
	    } else if (!data->firstSegPtr) {
		if (TkTextTagSetTest(segPtr->tagInfoPtr, tagPtr->index) != data->add) {
		    /* needed for test whether modifications have been done */
		    data->firstSegPtr = segPtr;
		}
	    }
	    if (segPtr->tagInfoPtr == data->tagInfoPtr) {
		assert(TkTextTagSetRefCount(data->newTagInfoPtr) > 0);
		TagSetAssign(&segPtr->tagInfoPtr, data->newTagInfoPtr);
	    } else {
		data->tagInfoPtr = segPtr->tagInfoPtr;
		segPtr->tagInfoPtr = addRemoveFunc(segPtr->tagInfoPtr, tagPtr);
		data->newTagInfoPtr = segPtr->tagInfoPtr;
	    }
	    if (segPtr->typePtr == &tkTextCharType && !segPtr->protectionFlag) {
		if (prevPtr && TkTextTagSetIsEqual(segPtr->tagInfoPtr, prevPtr->tagInfoPtr)) {
		    TkTextSegment *pPtr = prevPtr;

		    segPtr->refCount += 1; /* delay possible destruction */
		    prevPtr = JoinCharSegments(sharedTextPtr, prevPtr);
		    if (data->firstSegPtr == segPtr) {
			data->firstOffset += prevPtr->size - segPtr->size;
			data->firstSegPtr = prevPtr;
		    } else if (data->firstSegPtr == pPtr) {
			data->firstSegPtr = prevPtr;
		    }
		    if (data->lastSegPtr == segPtr) {
			data->lastOffset += prevPtr->size - segPtr->size;
			data->lastSegPtr = prevPtr;
		    } else if (data->lastSegPtr == pPtr) {
			data->lastSegPtr = prevPtr;
		    }
		    if (data->newTagInfoPtr == segPtr->tagInfoPtr
			    || data->newTagInfoPtr == pPtr->tagInfoPtr) {
			data->newTagInfoPtr = prevPtr->tagInfoPtr;
		    }
		    TkBTreeFreeSegment(segPtr);
		} else {
		    prevPtr = segPtr;
		}
	    } else {
		prevPtr = NULL;
	    }
	} else {
	    prevPtr = NULL;
	}

	segPtr = nextPtr;
    }

    return flags;
}

static unsigned
TreeTagLine(
    TreeTagData *data,
    TkTextLine *linePtr,
    TkTextSegment *segPtr1,
    TkTextSegment *segPtr2)
{
    unsigned flags = 0;
    const TkTextTag *tagPtr = data->tagPtr;
    unsigned tagIndex = tagPtr->index;
    TkTextSegment *segPtr = segPtr1 ? segPtr1 : linePtr->segPtr;
    bool add = data->add;

    while (segPtr->size == 0 && segPtr1 != segPtr2) {
	segPtr = segPtr->nextPtr;
    }
    while (segPtr2 && segPtr2->prevPtr && segPtr2->prevPtr->size == 0 && segPtr2 != segPtr1) {
	segPtr2 = segPtr2->prevPtr;
    }
    if (segPtr == segPtr2) {
	flags = DID_SKIP;
    } else if (add) {
	if (linePtr->tagonPtr == data->tagonPtr) {
	    assert(TkTextTagSetRefCount(data->newTagInfoPtr) > 0);
	    TagSetAssign(&linePtr->tagonPtr, data->newTagonPtr);
	} else {
	    data->tagonPtr = linePtr->tagonPtr;
	    linePtr->tagonPtr = TagSetAdd(linePtr->tagonPtr, tagPtr);
	    data->newTagonPtr = linePtr->tagonPtr;
	}
	flags |= HAS_TAGON;
	if (LineTestIfAnyIsUntagged(linePtr->segPtr, segPtr, tagIndex)
		|| (segPtr2 && LineTestIfAnyIsUntagged(segPtr2, NULL, tagIndex))) {
	    if (linePtr->tagoffPtr == data->addTagoffPtr) {
		assert(TkTextTagSetRefCount(data->newAddTagoffPtr) > 0);
		TagSetAssign(&linePtr->tagoffPtr, data->newAddTagoffPtr);
	    } else {
		data->addTagoffPtr = linePtr->tagoffPtr;
		linePtr->tagoffPtr = TagSetAdd(linePtr->tagoffPtr, tagPtr);
		data->newAddTagoffPtr = linePtr->tagoffPtr;
	    }
	    flags |= HAS_TAGOFF;
	} else {
	    linePtr->tagoffPtr = TagSetErase(linePtr->tagoffPtr, tagPtr);
	}
	flags |= AddRemoveTag(data, linePtr, segPtr1, segPtr2, TagSetAdd);
    } else {
	if (LineTestIfAnyIsTagged(linePtr->segPtr, segPtr, tagIndex)
		|| (segPtr2 && LineTestIfAnyIsTagged(segPtr2, NULL, tagIndex))) {
	    linePtr->tagoffPtr = TagSetAdd(linePtr->tagoffPtr, tagPtr);
	    flags |= HAS_TAGON | HAS_TAGOFF;
	} else {
	    if (linePtr->tagonPtr == data->tagonPtr) {
		assert(TkTextTagSetRefCount(data->newTagonPtr) > 0);
		TagSetAssign(&linePtr->tagonPtr, data->newTagonPtr);
	    } else {
		data->tagonPtr = linePtr->tagonPtr;
		linePtr->tagonPtr = TagSetErase(linePtr->tagonPtr, tagPtr);
		data->newTagonPtr = linePtr->tagonPtr;
	    }
	    if (linePtr->tagoffPtr == data->eraseTagoffPtr) {
		assert(TkTextTagSetRefCount(data->newEraseTagoffPtr) > 0);
		TagSetAssign(&linePtr->tagoffPtr, data->newEraseTagoffPtr);
	    } else {
		data->eraseTagoffPtr = linePtr->tagoffPtr;
		linePtr->tagoffPtr = TagSetErase(linePtr->tagoffPtr, tagPtr);
		data->newEraseTagoffPtr = linePtr->tagoffPtr;
	    }
	}
	flags |= AddRemoveTag(data, linePtr, segPtr1, segPtr2, TagSetErase);
    }

    return flags;
}

static unsigned
TreeTagNode(
    Node *nodePtr,
    TreeTagData *data,
    unsigned firstLineNo,
    TkTextSegment *segPtr1,
    TkTextSegment *segPtr2,
    bool redraw)
{
    TkTextTag *tagPtr;
    bool add;
    unsigned flags;
    unsigned nchilds;
    unsigned endLineNo = firstLineNo + nodePtr->numLines - 1;

    if (endLineNo < data->lineNo1 || data->lineNo2 < firstLineNo) {
	return DID_SKIP;
    }

    tagPtr = data->tagPtr;
    add = data->add;

    assert(tagPtr);

    if (NodeTestAllSegments(nodePtr, tagPtr->index, add)) {
	if (!data->firstSegPtr) {
	    data->firstSegPtr = nodePtr->linePtr->segPtr;
	}
	data->lastSegPtr = nodePtr->lastPtr->prevPtr->lastPtr;
	data->lastOffset = data->lastSegPtr->size;
	return add ? HAS_TAGON : 0;
    }

    flags = nchilds = 0;

    if ((segPtr1 ? data->lineNo1 < firstLineNo : data->lineNo1 <= firstLineNo)
	    && (segPtr2 ? endLineNo < data->lineNo2 : endLineNo <= data->lineNo2)) {
	const TkSharedText *sharedTextPtr = tagPtr->sharedTextPtr;
	bool delegateRedraw = redraw && NodeTestAnySegment(nodePtr, tagPtr->index, add);
	TkTextIndex index1, index2;

	if (delegateRedraw) {
	    redraw = false;
	}

	TkTextIndexClear2(&index1, NULL, sharedTextPtr->tree);
	TkTextIndexClear2(&index2, NULL, sharedTextPtr->tree);

	/*
	 * Whole node is affected.
	 */

	if (nodePtr->level > 0) {
	    Node *childPtr;

	    for (childPtr = nodePtr->childPtr; childPtr; childPtr = childPtr->nextPtr) {
		flags |= TreeTagNode(childPtr, data, firstLineNo, NULL, NULL, delegateRedraw);
		firstLineNo += childPtr->numLines;
	    }
	} else {
	    TkTextLine *linePtr = nodePtr->linePtr;
	    TkTextLine *lastPtr = nodePtr->lastPtr->nextPtr;

	    for ( ; linePtr != lastPtr; linePtr = linePtr->nextPtr) {
		if (!LineTestAllSegments(linePtr, tagPtr, add)) {
		    if (add) {
			flags |= AddRemoveTag(data, linePtr, NULL, NULL, TagSetAdd);
			if (linePtr->tagonPtr == data->tagonPtr) {
			    assert(TkTextTagSetRefCount(data->newTagonPtr) > 0);
			    TagSetAssign(&linePtr->tagonPtr, data->newTagonPtr);
			} else {
			    data->tagonPtr = linePtr->tagonPtr;
			    linePtr->tagonPtr = TagSetAdd(linePtr->tagonPtr, tagPtr);
			    data->newTagonPtr = linePtr->tagonPtr;
			}
		    } else {
			flags |= AddRemoveTag(data, linePtr, NULL, NULL, TagSetErase);
			if (linePtr->tagonPtr == data->tagonPtr) {
			    assert(TkTextTagSetRefCount(data->newTagonPtr) > 0);
			    TagSetAssign(&linePtr->tagonPtr, data->newTagonPtr);
			} else {
			    data->tagonPtr = linePtr->tagonPtr;
			    linePtr->tagonPtr = TagSetErase(linePtr->tagonPtr, tagPtr);
			    data->newTagonPtr = linePtr->tagonPtr;
			}
		    }
		    if (linePtr->tagoffPtr == data->eraseTagoffPtr) {
			assert(TkTextTagSetRefCount(data->newEraseTagoffPtr) > 0);
			TagSetAssign(&linePtr->tagoffPtr, data->newEraseTagoffPtr);
		    } else {
			data->eraseTagoffPtr = linePtr->tagoffPtr;
			linePtr->tagoffPtr = TagSetErase(linePtr->tagoffPtr, tagPtr);
			data->newEraseTagoffPtr = linePtr->tagoffPtr;
		    }
		    if (delegateRedraw) {
			TkTextIndexSetToStartOfLine2(&index1, linePtr);
			TkTextIndexSetToEndOfLine2(&index2, linePtr);
			data->changedProc(sharedTextPtr, data->textPtr, &index1, &index2,
				tagPtr, false);
		    }
		    if (!data->firstSegPtr) {
			data->firstSegPtr = linePtr->segPtr;
		    }
		    data->lastSegPtr = linePtr->lastPtr;
		    data->lastOffset = linePtr->lastPtr->size;
		} else if (data->undoInfo) {
		    SubLength(data, linePtr->size);
		}
	    }
	}

	if (redraw) {
	    TkTextIndexSetToStartOfLine2(&index1, nodePtr->linePtr);
	    TkTextIndexSetToEndOfLine2(&index2, nodePtr->lastPtr);
	    data->changedProc(sharedTextPtr, data->textPtr, &index1, &index2, tagPtr, false);
	}

	if (add) {
	    flags = HAS_TAGON;
	    nchilds = nodePtr->numChildren;
	}
    } else {
	unsigned tagIndex = tagPtr->index;
	unsigned myFlags;

	if (nodePtr->level > 0) {
	    Node *childPtr;

	    for (childPtr = nodePtr->childPtr; childPtr; childPtr = childPtr->nextPtr) {
		myFlags = TreeTagNode(childPtr, data, firstLineNo, segPtr1, segPtr2, redraw);
		if (myFlags == DID_SKIP) {
		    if (TkTextTagSetTest(childPtr->tagonPtr, tagIndex)) {
			if (!tagPtr->rootPtr) {
			    tagPtr->rootPtr = childPtr;
			}
			myFlags |= HAS_TAGON;
		    }
		    if (TkTextTagSetTest(childPtr->tagoffPtr, tagIndex)) {
			myFlags |= HAS_TAGOFF;
		    }
		}
		if (myFlags & HAS_TAGON) { nchilds += 1; }
		flags |= myFlags;
		firstLineNo += childPtr->numLines;
	    }
	} else {
	    const TkSharedText *sharedTextPtr = tagPtr->sharedTextPtr;
	    TkTextLine *linePtr = nodePtr->linePtr;
	    TkTextLine *lastPtr = nodePtr->lastPtr->nextPtr;
	    TkTextIndex index1, index2;

	    if (redraw) {
		TkTextIndexClear2(&index1, NULL, sharedTextPtr->tree);
		TkTextIndexClear2(&index2, NULL, sharedTextPtr->tree);
	    }

	    for ( ; firstLineNo < data->lineNo1; ++firstLineNo, linePtr = linePtr->nextPtr) {
		assert(linePtr);
		myFlags = 0;
		if (TkTextTagSetTest(linePtr->tagonPtr, tagIndex)) { myFlags |= HAS_TAGON; }
		if (TkTextTagSetTest(linePtr->tagoffPtr, tagIndex)) { myFlags |= HAS_TAGOFF; }
		if (myFlags & HAS_TAGON) { nchilds += 1; }
		flags |= myFlags;
		if (data->undoInfo) {
		    SubLength(data, linePtr->size);
		}
	    }
	    for ( ; firstLineNo <= data->lineNo2 && linePtr != lastPtr;
		    linePtr = linePtr->nextPtr, ++firstLineNo) {
		if (!LineTestAllSegments(linePtr, tagPtr, add)) {
		    TkTextSegment *startSegPtr, *stopSegPtr;

		    startSegPtr = (firstLineNo == data->lineNo1) ? segPtr1 : NULL;
		    stopSegPtr = (firstLineNo == data->lineNo2) ? segPtr2 : NULL;
		    myFlags = TreeTagLine(data, linePtr, startSegPtr, stopSegPtr);

		    if (myFlags == DID_SKIP) {
			if (TkTextTagSetTest(linePtr->tagonPtr, tagIndex)) { myFlags |= HAS_TAGON; }
			if (TkTextTagSetTest(linePtr->tagoffPtr, tagIndex)) { myFlags |= HAS_TAGOFF; }
		    }
		    if (myFlags & HAS_TAGON) { nchilds += 1; }
		    flags |= myFlags;

		    if (redraw) {
			TkTextIndexSetToStartOfLine2(&index1, linePtr);
			TkTextIndexSetToEndOfLine2(&index2, linePtr);
			data->changedProc(sharedTextPtr, data->textPtr, &index1, &index2, tagPtr, false);
		    }
		} else {
		    if (add) {
			flags |= HAS_TAGON;
			nchilds += 1;
		    }
		    if (data->undoInfo) {
			SubLength(data, linePtr->size);
		    }
		}
	    }
	    for ( ; linePtr != lastPtr; linePtr = linePtr->nextPtr) {
		assert(linePtr);
		myFlags = 0;
		if (TkTextTagSetTest(linePtr->tagonPtr, tagIndex)) { myFlags |= HAS_TAGON; }
		if (TkTextTagSetTest(linePtr->tagoffPtr, tagIndex)) { myFlags |= HAS_TAGOFF; }
		if (myFlags & HAS_TAGON) { nchilds += 1; }
		flags |= myFlags;
		if (data->undoInfo) {
		    SubLength(data, linePtr->size);
		}
	    }
	}
    }

    if (!(flags & HAS_TAGON)) {
	flags &= ~HAS_TAGOFF;
    } else if (nchilds < nodePtr->numChildren) {
	flags |= HAS_TAGOFF;
    }
    if (nchilds > (nodePtr->level > 0 ? 1u : 0u)) {
	tagPtr->rootPtr = nodePtr;
    }

    nodePtr->tagonPtr = TagSetAddOrErase(nodePtr->tagonPtr, tagPtr, !!(flags & HAS_TAGON));
    nodePtr->tagoffPtr = TagSetAddOrErase(nodePtr->tagoffPtr, tagPtr, !!(flags & HAS_TAGOFF));

    return flags;
}

static bool
FindSplitPoints(
    TkSharedText *sharedTextPtr,
    const TkTextIndex *indexPtr1,
    const TkTextIndex *indexPtr2,
    const TkTextTag *tagPtr,	/* can be NULL */
    bool add,
    TkTextSegment **segPtr1,
    TkTextSegment **segPtr2)
{
    TkTextLine *linePtr1 = TkTextIndexGetLine(indexPtr1);
    TkTextLine *linePtr2 = TkTextIndexGetLine(indexPtr2);
    TkTextIndex end;
    bool needSplit1;
    bool needSplit2;

    assert(tagPtr || !add);

    TkTextIndexBackChars(NULL, indexPtr2, 1, &end, COUNT_INDICES);

    needSplit1 = (TkBTreeCharTagged(indexPtr1, tagPtr) != add);
    needSplit2 = (TkBTreeCharTagged(&end, tagPtr) != add);

    if (!needSplit1 && !needSplit2) {
	if (tagPtr) {
	    TkTextSearch search;

	    TkBTreeStartSearch(indexPtr1, indexPtr2, tagPtr, &search, SEARCH_EITHER_TAGON_TAGOFF);
	    if (!TkBTreeNextTag(&search)) {
		return false; /* whole range is already tagged/untagged */
	    }
	} else {
	    if (!TkBTreeFindNextTagged(indexPtr1, indexPtr2, NULL)) {
		return false; /* whole range is already untagged */
	    }
	}
    }

    if (needSplit1) {
	TkTextIndexToByteIndex((TkTextIndex *) indexPtr1); /* mutable due to concept */
	TkTextIndexToByteIndex((TkTextIndex *) indexPtr2); /* mutable due to concept */
	if ((*segPtr1 = SplitSeg(indexPtr1, NULL))) {
	    SplitSection((*segPtr1)->sectionPtr);
	}
    } else {
	*segPtr1 = NULL;
    }
    if (!*segPtr1) {
	*segPtr1 = TkTextIndexGetContentSegment(indexPtr1, NULL);
    } else if (!(*segPtr1 = (*segPtr1)->nextPtr)) {
	assert((*segPtr1)->sectionPtr->linePtr->nextPtr);
	linePtr1 = (*segPtr1)->sectionPtr->linePtr->nextPtr;
	*segPtr1 = linePtr1->segPtr;
    }

    /*
     * The next split may invalidate '*segPtr1', so we are inserting temporarily
     * a protection mark, this avoids the invalidation.
     */

    assert(!sharedTextPtr->protectionMark[0]->sectionPtr); /* this protection mark is unused? */
    LinkSegment(linePtr1, (*segPtr1)->prevPtr, sharedTextPtr->protectionMark[0]);

    if (needSplit2) {
	TkTextIndexToByteIndex((TkTextIndex *) indexPtr1); /* mutable due to concept */
	TkTextIndexToByteIndex((TkTextIndex *) indexPtr2); /* mutable due to concept */
	if ((*segPtr2 = SplitSeg(indexPtr2, NULL))) {
	    SplitSection((*segPtr2)->sectionPtr);
	}
    } else {
	*segPtr2 = NULL;
    }
    if (!*segPtr2) {
	*segPtr2 = TkTextIndexGetContentSegment(indexPtr2, NULL);
    } else if (!(*segPtr2 = (*segPtr2)->nextPtr)) {
	assert((*segPtr2)->sectionPtr->linePtr->nextPtr);
	linePtr2 = (*segPtr2)->sectionPtr->linePtr->nextPtr;
	*segPtr2 = linePtr2->segPtr;
    }

    *segPtr1 = sharedTextPtr->protectionMark[0]->nextPtr;
    UnlinkSegment(sharedTextPtr->protectionMark[0]);

    return true;
}

bool
TkBTreeTag(
    TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    TkText *textPtr,			/* Information about text widget, can be NULL. */
    const TkTextIndex *indexPtr1,	/* Indicates first character in range. */
    const TkTextIndex *indexPtr2,	/* Indicates character just after the last one in range. */
    TkTextTag *tagPtr,			/* Tag to add or remove. */
    bool add,				/* 'true' means add tag to the given range of characters;
					 * 'false' means remove the tag from the range. */
    TkTextUndoInfo *undoInfo,		/* Store undo information, can be NULL. */
    TkTextTagChangedProc changedProc)	/* Trigger this callback when any tag will be added/removed. */
{
    TkTextLine *linePtr1;
    TkTextLine *linePtr2;
    TkTextSegment *segPtr1, *segPtr2;
    TkTextSegment *firstPtr, *lastPtr;
    TreeTagData data;
    Node *rootPtr;

    assert(tagPtr);
    assert(indexPtr1);
    assert(indexPtr2);
    assert(TkTextIndexCompare(indexPtr1, indexPtr2) <= 0);
    /* assert(changedProc); */	/* MSVC erroneously triggers warning C4550: expression
				 * evaluates to a function which is missing an argument list
				 */

    if (!add && !tagPtr->rootPtr) {
	return false;
    }
    if (TkTextIndexIsEqual(indexPtr1, indexPtr2)) {
	return false;
    }
    if (!add) {
	if (!tagPtr->rootPtr) {
	    return false;
	}
	if (TkBTreeGetRoot(sharedTextPtr->tree)->tagonPtr == sharedTextPtr->emptyTagInfoPtr) {
	    return false;
	}
    }
    if (!FindSplitPoints(sharedTextPtr, indexPtr1, indexPtr2, tagPtr, add, &segPtr1, &segPtr2)) {
	return false;
    }

    segPtr1->protectionFlag = true;
    segPtr2->protectionFlag = true;

    if (!add && tagPtr->elideString) {
	/*
	 * In case of elision we have to inspect each segment, because a
	 * Branch or a Link segment has to be inserted/removed if required.
	 *
	 * NOTE: Currently, when using elision (tag option -elide), TkBTreeTag
	 * can be considerably slower than without. In return the lookup, whether
	 * a segment is elided, is super-fast now, and this has more importance -
	 * in general inserting/removing an elided range will be done only once,
	 * but the lookup for the elision option is a frequent use case.
	 *
	 * Note that UpdateElideInfo needs the old state when removing the tag,
	 * so we are doing this before eliminating the tag.
	 */

	UpdateElideInfo(sharedTextPtr, tagPtr, &segPtr1, &segPtr2, ELISION_WILL_BE_REMOVED);
    }

    if (undoInfo) {
	memset(undoInfo, 0, sizeof(*undoInfo));
    }

    firstPtr = TkTextIndexIsStartOfLine(indexPtr1) ? NULL : segPtr1;
    lastPtr = TkTextIndexIsStartOfLine(indexPtr2) ? NULL : segPtr2;
    linePtr1 = segPtr1->sectionPtr->linePtr;
    linePtr2 = segPtr2->sectionPtr->linePtr;
    rootPtr = TkBTreeGetRoot(sharedTextPtr->tree); /* we must start at top level */
    tagPtr->rootPtr = NULL; /* will be recomputed */

    memset(&data, 0, sizeof(data));
    data.tagPtr = tagPtr;
    data.add = add;
    data.changedProc = changedProc;
    data.undoInfo = tagPtr->undo ? undoInfo : NULL;
    data.firstSegPtr = NULL;
    data.lastSegPtr = NULL;
    data.textPtr = textPtr;
    data.lineNo1 = TkTextIndexGetLineNumber(indexPtr1, NULL);
    data.lineNo2 = linePtr1 == linePtr2 ?
	    data.lineNo1 : TkTextIndexGetLineNumber(indexPtr2, NULL) - (lastPtr ? 0 : 1);
    data.lengths = data.lengthsBuf;
    data.capacityOfLengths = sizeof(data.lengthsBuf)/sizeof(data.lengthsBuf[0]);

    /*
     * NOTE: the display must be redrawn even if this tag is not affecting the
     * display, otherwise the triggering of tag events may not work properly.
     * This means that we cannot use 'tagPtr->affectsDisplay' here for the
     * decision.
     */

    TreeTagNode(rootPtr, &data, 0, firstPtr, lastPtr, true);

    if (add && tagPtr->elideString) {
	/*
	 * In case of elision we have to inspect each segment, because a
	 * Branch or a Link segment has to be inserted/removed if required.
	 *
	 * NOTE: Currently, when using elision (tag option -elide), TkBTreeTag
	 * can be considerably slower than without. In return the lookup, whether
	 * a segment is elided, is super-fast now, and this has more importance -
	 * in general inserting/removing an elided range will be done only once,
	 * but the lookup for the elision option is a frequent use case.
	 *
	 * Note that UpdateElideInfo needs the new state when adding the tag,
	 * so we are doing this after the tag has been added.
	 */

	UpdateElideInfo(sharedTextPtr, tagPtr, &segPtr1, &segPtr2, ELISION_HAS_BEEN_ADDED);
    }

    if (undoInfo && (data.sizeOfLengths > 0 || data.currLength > 0)) {
	TkTextIndex index1 = *indexPtr1;
	TkTextIndex index2 = *indexPtr2;

	assert(data.firstSegPtr);
	assert(data.lastSegPtr);

	/*
	 * Setup the undo information.
	 */

	assert(data.lastSegPtr->size >= data.lastOffset);
	data.lastOffset = data.lastSegPtr->size - data.lastOffset;

	if (data.lastSegPtr->nextPtr) {
	    data.lastSegPtr = data.lastSegPtr->nextPtr;
	} else if (data.lastSegPtr->sectionPtr->linePtr->nextPtr) {
	    data.lastSegPtr = data.lastSegPtr->sectionPtr->linePtr->nextPtr->segPtr;
	}
	if (data.lastSegPtr->sectionPtr->linePtr == GetLastLine(sharedTextPtr, textPtr)) {
	    data.lastSegPtr = textPtr->endMarker;
	}
	TkTextIndexSetSegment(&index1, data.firstSegPtr);
	TkTextIndexSetSegment(&index2, data.lastSegPtr);
	TkTextIndexForwBytes(textPtr, &index1, data.firstOffset, &index1);
	TkTextIndexBackBytes(textPtr, &index2, data.lastOffset, &index2);
	assert(TkTextIndexCompare(&index1, &index2) < 0);

	if (data.sizeOfLengths > 0) {
	    assert(data.currLength != 0);
	    if (data.currLength > 0 && data.sizeOfLengths > 1) {
		SaveLength(&data);
	    }
	    if (data.sizeOfLengths == 1) {
		data.sizeOfLengths = 0;
	    } else if (data.lengths[data.sizeOfLengths - 1] > 0) {
		data.lengths[data.sizeOfLengths - 1] = 0;
	    } else {
		data.currLength = 0;
		SaveLength(&data);
	    }
	}

	switch (MergeTagUndoToken(sharedTextPtr, &index1, &index2, &data)) {
	case UNDO_NEEDED: {
	    UndoTokenTagChange *undoToken;

	    if (tagPtr->recentTagAddRemoveToken && !tagPtr->recentTagAddRemoveTokenIsNull) {
		undoInfo->token = (TkTextUndoToken *) tagPtr->recentTagAddRemoveToken;
		undoInfo->byteSize = 0;
		tagPtr->recentTagAddRemoveToken = NULL;
	    }
	    if (!tagPtr->recentTagAddRemoveToken) {
		tagPtr->recentTagAddRemoveToken = malloc(sizeof(UndoTokenTagChange));
		DEBUG_ALLOC(tkTextCountNewUndoToken++);
	    }

	    tagPtr->recentTagAddRemoveTokenIsNull = false;
	    undoToken = (UndoTokenTagChange *) tagPtr->recentTagAddRemoveToken;
	    undoToken->undoType = &undoTokenTagType;
	    undoToken->tagPtr = tagPtr;
	    if (!add) {
		MARK_POINTER(undoToken->tagPtr);
	    }
	    MakeUndoIndex(sharedTextPtr, &index1, &undoToken->startIndex, GRAVITY_LEFT);
	    MakeUndoIndex(sharedTextPtr, &index2, &undoToken->endIndex, GRAVITY_RIGHT);
	    if (data.sizeOfLengths > 0) {
		if (data.lengths == data.lengthsBuf) {
		    data.lengths = malloc(data.sizeOfLengths * sizeof(data.lengths[0]));
		    memcpy(data.lengths, data.lengthsBuf, data.sizeOfLengths * sizeof(data.lengths[0]));
		} else {
		    data.lengths = realloc(data.lengths, data.sizeOfLengths * sizeof(data.lengths[0]));
		}
		undoToken->lengths = data.lengths;
		data.lengths = data.lengthsBuf;
	    } else {
		undoToken->lengths = NULL;
	    }
	    TkTextTagAddRetainedUndo(sharedTextPtr, tagPtr);
	    break;
	}
	case UNDO_MERGED:
	    /* no action required */
	    break;
	case UNDO_ANNIHILATED:
	    tagPtr->recentTagAddRemoveTokenIsNull = true;
	    break;
	}

	if (data.lengths != data.lengthsBuf) {
	    free(data.lengths);
	}
    }

    assert(data.lengths == data.lengthsBuf);

    CleanupSplitPoint(segPtr1, sharedTextPtr);
    if (segPtr1 != segPtr2) {
	CleanupSplitPoint(segPtr2, sharedTextPtr);
    }
    TkBTreeIncrEpoch(sharedTextPtr->tree);

    TK_BTREE_DEBUG(TkBTreeCheck(indexPtr1->tree));

    return !!data.firstSegPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeClearTags --
 *
 *	Turn all tags off inside a given range. Note that the special
 *	selection tag is an exception, and may not be removed if not
 *	wanted.
 *
 * Results:
 *	True if the tags on any characters were changed, and false otherwise.
 *
 * Side effects:
 *	Some branches and links may be removed.
 *
 *----------------------------------------------------------------------
 */

typedef struct ClearTagsData {
    unsigned skip;
    unsigned capacity;
    TkTextTagSet *tagonPtr;
    TkTextTagSet *tagoffPtr;
    TkTextTagSet *newTagonPtr;
    TkTextTagSet *newTagoffPtr;
    UndoTagChange *tagChangePtr;
    TkTextSegment *firstSegPtr;
    TkTextSegment *lastSegPtr;
} ClearTagsData;

static Node *
FindCommonParent(
    Node *nodePtr1,
    Node *nodePtr2)
{
    while (nodePtr1->level > nodePtr2->level) {
	nodePtr1 = nodePtr1->parentPtr;
    }
    while (nodePtr2->level > nodePtr1->level) {
	nodePtr2 = nodePtr2->parentPtr;
    }
    return nodePtr2;
}

static bool
TestIfAnySegmentIsAffected(
    TkSharedText *sharedTextPtr,
    const TkTextTagSet *tagInfoPtr,
    bool discardSelection)
{
    if (discardSelection) {
	return !TkTextTagBitContainsSet(sharedTextPtr->selectionTags, tagInfoPtr);
    }
    return tagInfoPtr != sharedTextPtr->emptyTagInfoPtr;
}

static bool
TestIfDisplayGeometryIsAffected(
    TkSharedText *sharedTextPtr,
    const TkTextTagSet *tagInfoPtr,
    bool discardSelection)
{
    unsigned i;

    i = TkTextTagSetFindFirstInIntersection(
	    tagInfoPtr, discardSelection ? sharedTextPtr->affectGeometryNonSelTags
	    : sharedTextPtr->affectGeometryTags);
    return i != TK_TEXT_TAG_SET_NPOS && sharedTextPtr->tagLookup[i]->affectsDisplayGeometry;
}

static TkTextTagSet *
ClearTagsFromLine(
    TkSharedText *sharedTextPtr,
    TkTextLine *linePtr,
    TkTextSegment *firstPtr,
    TkTextSegment *lastPtr,
    TkTextTagSet *affectedTagInfoPtr,
    UndoTokenTagClear *undoToken,
    ClearTagsData *data,
    bool discardSelection,
    bool redraw,
    TkTextTagChangedProc changedProc,
    TkText *textPtr)
{
    TkTextTagSet *emptyTagInfoPtr = sharedTextPtr->emptyTagInfoPtr;
    TkTextTagSet *myAffectedTagInfoPtr;
    TkTextSegment *segPtr;
    TkTextSegment *prevPtr;
    bool anyChanges;

    if (linePtr->tagonPtr == emptyTagInfoPtr) {
	/*
	 * Nothing to do.
	 */
	if (undoToken) {
	    data->skip += linePtr->size;
	}
	return affectedTagInfoPtr;
    }

    if (discardSelection || redraw) {
	TkTextTagSetIncrRefCount(myAffectedTagInfoPtr = emptyTagInfoPtr);
    } else {
	myAffectedTagInfoPtr = affectedTagInfoPtr;
    }

    segPtr = firstPtr ? firstPtr : linePtr->segPtr;
    prevPtr = NULL;
    anyChanges = false;

    if (undoToken && firstPtr) {
	TkTextIndex index;
	TkTextIndexClear2(&index, NULL, sharedTextPtr->tree);
	TkTextIndexSetSegment(&index, firstPtr);
	data->skip = TkTextSegToIndex(firstPtr);
    }

    while (segPtr != lastPtr) {
	TkTextSegment *nextPtr = segPtr->nextPtr;

	if (segPtr->tagInfoPtr) {
	    if (segPtr->tagInfoPtr != emptyTagInfoPtr
		    && (!discardSelection
			|| !TkTextTagBitContainsSet(sharedTextPtr->selectionTags, segPtr->tagInfoPtr))) {
		if (!data->firstSegPtr) {
		    data->firstSegPtr = segPtr;
		}
		data->lastSegPtr = segPtr;

		if (myAffectedTagInfoPtr) {
		    myAffectedTagInfoPtr = TkTextTagSetJoin(myAffectedTagInfoPtr, segPtr->tagInfoPtr);
		}

		if (undoToken) {
		    TkTextTagSet *tagInfoPtr;

		    TkTextTagSetIncrRefCount(tagInfoPtr = segPtr->tagInfoPtr);
		    tagInfoPtr = TagSetRemoveBits(segPtr->tagInfoPtr,
			    sharedTextPtr->dontUndoTags, sharedTextPtr);

		    if (tagInfoPtr == sharedTextPtr->emptyTagInfoPtr) {
			TkTextTagSetDecrRefCount(tagInfoPtr);
			data->skip += segPtr->size;
			if (data->firstSegPtr == segPtr) {
			    data->firstSegPtr = data->lastSegPtr = NULL;
			}
		    } else {
			UndoTagChange *tagChangePtr;

			if (data->skip == 0
				&& data->tagChangePtr
				&& TkTextTagSetIsEqual(data->tagChangePtr->tagInfoPtr, tagInfoPtr)) {
			    data->tagChangePtr->size += segPtr->size;
			    TkTextTagSetDecrRefCount(tagInfoPtr);
			} else {
			    if (undoToken->changeListSize == data->capacity) {
				data->capacity = MAX(2u*data->capacity, 50u);
				undoToken->changeList = realloc(undoToken->changeList,
					data->capacity * sizeof(undoToken->changeList[0]));
			    }
			    tagChangePtr = undoToken->changeList + undoToken->changeListSize++;
			    tagChangePtr->tagInfoPtr = tagInfoPtr;
			    tagChangePtr->size = segPtr->size;
			    tagChangePtr->skip = data->skip;
			    data->tagChangePtr = tagChangePtr;
			    data->skip = 0;
			}
		    }
		}

		if (discardSelection) {
		    segPtr->tagInfoPtr = TagSetIntersectBits(segPtr->tagInfoPtr,
			    sharedTextPtr->selectionTags, sharedTextPtr);
		} else {
		    TagSetAssign(&segPtr->tagInfoPtr, sharedTextPtr->emptyTagInfoPtr);
		}
		anyChanges = true;
	    } else if (undoToken) {
		data->skip += segPtr->size;
	    }
	    if (segPtr->typePtr == &tkTextCharType && !segPtr->protectionFlag) {
		if (prevPtr && TkTextTagSetIsEqual(segPtr->tagInfoPtr, prevPtr->tagInfoPtr)) {
		    TkTextSegment *pPtr = prevPtr;

		    prevPtr = JoinCharSegments(sharedTextPtr, prevPtr);
		    if (data->firstSegPtr == pPtr || data->firstSegPtr == segPtr) {
			data->firstSegPtr = prevPtr;
		    }
		    if (data->lastSegPtr == pPtr || data->lastSegPtr == segPtr) {
			data->lastSegPtr = prevPtr;
		    }
		} else {
		    prevPtr = segPtr;
		}
	    } else {
		prevPtr = NULL;
	    }
	} else {
	    prevPtr = NULL;
	}

	segPtr = nextPtr;
    }

    if (anyChanges) {
	if (redraw
		&& TkTextTagSetIntersectsBits(myAffectedTagInfoPtr,
			discardSelection
			    ? sharedTextPtr->affectDisplayNonSelTags
			    : sharedTextPtr->affectDisplayTags)) {
	    bool affectsDisplayGeometry = TestIfDisplayGeometryIsAffected(
		    sharedTextPtr, myAffectedTagInfoPtr, discardSelection);
	    TkTextIndex index1, index2;

	    TkTextIndexClear2(&index1, NULL, sharedTextPtr->tree);
	    TkTextIndexClear2(&index2, NULL, sharedTextPtr->tree);
	    TkTextIndexSetToStartOfLine2(&index1, linePtr);
	    TkTextIndexSetToEndOfLine2(&index2, linePtr);
	    changedProc(sharedTextPtr, textPtr, &index1, &index2, NULL, affectsDisplayGeometry);
	}

	if (discardSelection) {
	    myAffectedTagInfoPtr = TagSetRemoveBits(myAffectedTagInfoPtr,
		    sharedTextPtr->selectionTags, sharedTextPtr);
	}

	if (firstPtr || lastPtr) {
	    TkTextTagSet *tagonPtr, *tagoffPtr;

	    if (linePtr->tagonPtr == data->tagonPtr && linePtr->tagoffPtr == data->tagoffPtr) {
		TagSetReplace(&linePtr->tagonPtr, data->newTagonPtr);
		TagSetReplace(&linePtr->tagoffPtr, data->newTagoffPtr);
	    } else {
		data->tagonPtr = linePtr->tagonPtr;
		data->tagoffPtr = linePtr->tagoffPtr;

		TkTextTagSetIncrRefCount(tagonPtr = sharedTextPtr->emptyTagInfoPtr);
		tagoffPtr = NULL;

		for (segPtr = linePtr->segPtr; segPtr; segPtr = segPtr->nextPtr) {
		    if (segPtr->tagInfoPtr) {
			tagonPtr = TkTextTagSetJoin(tagonPtr, segPtr->tagInfoPtr);
			tagoffPtr = TagSetIntersect(tagoffPtr, segPtr->tagInfoPtr, sharedTextPtr);
		    }
		}

		TagSetReplace(&linePtr->tagonPtr, tagonPtr);

		if (tagoffPtr) {
		    tagoffPtr = TagSetComplementTo(tagoffPtr, linePtr->tagonPtr, sharedTextPtr);
		    TagSetReplace(&linePtr->tagoffPtr, tagoffPtr);
		} else {
		    TagSetAssign(&linePtr->tagoffPtr, linePtr->tagonPtr);
		}

		data->newTagonPtr = linePtr->tagonPtr;
		data->newTagoffPtr = linePtr->tagoffPtr;
	    }
	} else if (discardSelection) {
	    linePtr->tagonPtr = TagSetRemove(linePtr->tagonPtr, myAffectedTagInfoPtr, sharedTextPtr);
	    linePtr->tagoffPtr = TagSetRemove(linePtr->tagoffPtr, myAffectedTagInfoPtr, sharedTextPtr);
	} else {
	    TagSetAssign(&linePtr->tagonPtr, sharedTextPtr->emptyTagInfoPtr);
	    TagSetAssign(&linePtr->tagoffPtr, sharedTextPtr->emptyTagInfoPtr);
	}

	if (discardSelection) {
	    if (affectedTagInfoPtr) {
		affectedTagInfoPtr = TkTextTagSetJoin(affectedTagInfoPtr, myAffectedTagInfoPtr);
	    }
	    TkTextTagSetDecrRefCount(myAffectedTagInfoPtr);
	} else if (redraw && affectedTagInfoPtr) {
	    affectedTagInfoPtr = TkTextTagSetJoin(affectedTagInfoPtr, myAffectedTagInfoPtr);
	    TkTextTagSetDecrRefCount(myAffectedTagInfoPtr);
	}
    }

    return affectedTagInfoPtr;
}

static void
ClearTagRoots(
    const TkSharedText *sharedTextPtr,
    const TkTextTagSet *affectedTags)
{
    unsigned i;

    for (i = TkTextTagSetFindFirst(affectedTags);
	    i != TK_TEXT_TAG_SET_NPOS;
	    i = TkTextTagSetFindNext(affectedTags, i)) {
	TkTextTag *tagPtr = sharedTextPtr->tagLookup[i];

	assert(tagPtr);
	tagPtr->rootPtr = NULL;
    }
}

static void
ClearTagsFromAllNodes(
    TkSharedText *sharedTextPtr,
    Node *nodePtr,
    ClearTagsData *data,
    bool discardSelection,
    TkTextTagChangedProc changedProc,
    TkText *textPtr)
{
    /*
     * This is a very fast way to clear all tags, but this function only works
     * if all the tags in the widget will be cleared.
     */

    if (!TestIfAnySegmentIsAffected(sharedTextPtr, nodePtr->tagonPtr, discardSelection)) {
	return; /* nothing to do */
    }

    if (nodePtr->level > 0) {
	Node *childPtr;

	for (childPtr = nodePtr->childPtr; childPtr; childPtr = childPtr->nextPtr) {
	    ClearTagsFromAllNodes(sharedTextPtr, childPtr, data, discardSelection, changedProc, textPtr);
	}
    } else {
	TkTextLine *linePtr = nodePtr->linePtr;
	TkTextLine *lastPtr = nodePtr->lastPtr->nextPtr;

	for ( ; linePtr != lastPtr; linePtr = linePtr->nextPtr) {
	    if (TestIfAnySegmentIsAffected(sharedTextPtr, linePtr->tagonPtr, discardSelection)) {
		ClearTagsFromLine(sharedTextPtr, linePtr, NULL, NULL, NULL, NULL, data,
			discardSelection, false, changedProc, textPtr);
	    } else if (data->firstSegPtr) {
		data->skip += linePtr->size;
	    }
	}
    }

    if (discardSelection) {
	nodePtr->tagonPtr = TagSetIntersectBits(nodePtr->tagonPtr,
		sharedTextPtr->selectionTags, sharedTextPtr);
	nodePtr->tagoffPtr = TagSetIntersectBits(nodePtr->tagoffPtr,
		sharedTextPtr->selectionTags, sharedTextPtr);
    } else {
	TagSetAssign(&nodePtr->tagonPtr, sharedTextPtr->emptyTagInfoPtr);
	TagSetAssign(&nodePtr->tagoffPtr, sharedTextPtr->emptyTagInfoPtr);
    }
}

static TkTextTagSet *
ClearTagsFromNode(
    TkSharedText *sharedTextPtr,
    Node *nodePtr,
    unsigned firstLineNo,
    unsigned lineNo1,
    unsigned lineNo2,
    TkTextSegment *segPtr1,	/* will not be free'd! */
    TkTextSegment *segPtr2,	/* will not be free'd! */
    TkTextTagSet *affectedTagInfoPtr,
    UndoTokenTagClear *undoToken,
    ClearTagsData *data,
    bool discardSelection,
    bool redraw,
    TkTextTagChangedProc changedProc,
    TkText *textPtr)
{
    TkTextTagSet *emptyTagInfoPtr = sharedTextPtr->emptyTagInfoPtr;
    unsigned endLineNo = firstLineNo + nodePtr->numLines - 1;
    TkTextTagSet *additionalTagoffPtr, *tagInfoPtr, *tagRootInfoPtr;
    unsigned i;

    if (endLineNo < lineNo1
	    || lineNo2 < firstLineNo
	    || !TestIfAnySegmentIsAffected(sharedTextPtr, nodePtr->tagonPtr, discardSelection)) {
	/*
	 * Nothing to do for this node.
	 */

	if (undoToken) {
	    data->skip += nodePtr->size;
	}
	return affectedTagInfoPtr;
    }

    additionalTagoffPtr = NULL;
    tagRootInfoPtr = NULL;
    TkTextTagSetIncrRefCount(tagInfoPtr = nodePtr->tagonPtr);

    if ((segPtr1 ? lineNo1 < firstLineNo : lineNo1 <= firstLineNo)
	    && (segPtr2 ? endLineNo < lineNo2 : endLineNo <= lineNo2)) {
	bool delegateRedraw = redraw
		&& (discardSelection
			? TkTextTagSetIntersectionIsEqual(nodePtr->tagonPtr, nodePtr->tagoffPtr,
				sharedTextPtr->selectionTags)
			: !TkTextTagSetIsEqual(nodePtr->tagonPtr, nodePtr->tagoffPtr));
	TkTextIndex index1, index2;

	TkTextIndexClear2(&index1, NULL, sharedTextPtr->tree);
	TkTextIndexClear2(&index2, NULL, sharedTextPtr->tree);

	if (delegateRedraw) {
	    redraw = false;
	}

	/*
	 * Whole node is affected.
	 */

	if (affectedTagInfoPtr) {
	    affectedTagInfoPtr = TkTextTagSetJoin(affectedTagInfoPtr, nodePtr->tagonPtr);
	    affectedTagInfoPtr = TagSetRemoveBits(affectedTagInfoPtr,
		    sharedTextPtr->selectionTags, sharedTextPtr);
	}

	if (discardSelection) {
	    nodePtr->tagonPtr = TagSetIntersectBits(
		    nodePtr->tagonPtr, sharedTextPtr->selectionTags, sharedTextPtr);
	    nodePtr->tagoffPtr = TagSetIntersectBits(
		    nodePtr->tagoffPtr, sharedTextPtr->selectionTags, sharedTextPtr);
	} else {
	    TagSetAssign(&nodePtr->tagonPtr, emptyTagInfoPtr);
	    TagSetAssign(&nodePtr->tagoffPtr, emptyTagInfoPtr);
	}

	if (nodePtr->level > 0) {
	    Node *childPtr;

	    for (childPtr = nodePtr->childPtr; childPtr; childPtr = childPtr->nextPtr) {
		ClearTagsFromNode(sharedTextPtr, childPtr, firstLineNo, lineNo1, lineNo2,
			NULL, NULL, NULL, undoToken, data, discardSelection, delegateRedraw,
			changedProc, textPtr);
		firstLineNo += childPtr->numLines;
	    }
	} else {
	    TkTextLine *linePtr = nodePtr->linePtr;
	    TkTextLine *lastPtr = nodePtr->lastPtr->nextPtr;

	    for ( ; linePtr != lastPtr; linePtr = linePtr->nextPtr) {
		if (TestIfAnySegmentIsAffected(sharedTextPtr, linePtr->tagonPtr, discardSelection)) {
		    ClearTagsFromLine(sharedTextPtr, linePtr, NULL, NULL, NULL, undoToken, data,
			    discardSelection, delegateRedraw, changedProc, textPtr);
		} else if (data->firstSegPtr) {
		    data->skip += linePtr->size;
		}
	    }
	}

	if (redraw) {
	    bool affectsDisplayGeometry = TestIfDisplayGeometryIsAffected(sharedTextPtr,
		    nodePtr->tagonPtr, discardSelection);
	    TkTextIndexSetToStartOfLine2(&index1, nodePtr->linePtr);
	    TkTextIndexSetToEndOfLine2(&index2,
		    nodePtr->lastPtr->nextPtr ? nodePtr->lastPtr: nodePtr->lastPtr->prevPtr);
	    changedProc(sharedTextPtr, textPtr, &index1, &index2, NULL, affectsDisplayGeometry);
	}
    } else {
	TagSetAssign(&nodePtr->tagonPtr, emptyTagInfoPtr);
	TagSetAssign(&nodePtr->tagoffPtr, emptyTagInfoPtr);

	if (nodePtr->level > 0) {
	    Node *childPtr;

	    TkTextTagSetIncrRefCount(tagRootInfoPtr = emptyTagInfoPtr);

	    for (childPtr = nodePtr->childPtr; childPtr; childPtr = childPtr->nextPtr) {
		affectedTagInfoPtr = ClearTagsFromNode(sharedTextPtr, childPtr, firstLineNo,
			lineNo1, lineNo2, segPtr1, segPtr2, affectedTagInfoPtr, undoToken, data,
			discardSelection, redraw, changedProc, textPtr);
		tagRootInfoPtr = TagSetJoinOfDifferences(
			tagRootInfoPtr, childPtr->tagonPtr, nodePtr->tagonPtr, sharedTextPtr);
		nodePtr->tagonPtr = TkTextTagSetJoin(nodePtr->tagonPtr, childPtr->tagonPtr);
		nodePtr->tagoffPtr = TkTextTagSetJoin(nodePtr->tagoffPtr, childPtr->tagoffPtr);
		additionalTagoffPtr = TagSetIntersect(additionalTagoffPtr,
			childPtr->tagonPtr, sharedTextPtr);
		firstLineNo += childPtr->numLines;
	    }

	    tagRootInfoPtr = TkTextTagSetComplementTo(tagRootInfoPtr, nodePtr->tagonPtr);
	} else {
	    TkTextLine *linePtr = nodePtr->linePtr;
	    TkTextLine *lastPtr = nodePtr->lastPtr->nextPtr;
	    TkTextIndex index1, index2;

	    TkTextIndexClear2(&index1, NULL, sharedTextPtr->tree);
	    TkTextIndexClear2(&index2, NULL, sharedTextPtr->tree);

	    for ( ; linePtr != lastPtr; linePtr = linePtr->nextPtr, ++firstLineNo) {
		if (firstLineNo >= lineNo1 && firstLineNo <= lineNo2) {
		    if (TestIfAnySegmentIsAffected(sharedTextPtr, linePtr->tagonPtr,
				discardSelection)) {
			TkTextSegment *startSegPtr = (firstLineNo == lineNo1) ? segPtr1 : NULL;
			TkTextSegment *stopSegPtr = (firstLineNo == lineNo2) ? segPtr2 : NULL;

			affectedTagInfoPtr = ClearTagsFromLine(sharedTextPtr, linePtr, startSegPtr,
				stopSegPtr, affectedTagInfoPtr, undoToken, data, discardSelection,
				redraw, changedProc, textPtr);
		    } else if (data->firstSegPtr) {
			data->skip += linePtr->size;
		    }
		}

		nodePtr->tagonPtr = TkTextTagSetJoin(nodePtr->tagonPtr, linePtr->tagonPtr);
		nodePtr->tagoffPtr = TkTextTagSetJoin(nodePtr->tagoffPtr, linePtr->tagoffPtr);
		additionalTagoffPtr = TagSetIntersect(additionalTagoffPtr,
			linePtr->tagonPtr, sharedTextPtr);
	    }
	}
    }

    if (additionalTagoffPtr) {
	nodePtr->tagoffPtr = TagSetJoinComplementTo(
		nodePtr->tagoffPtr, additionalTagoffPtr, nodePtr->tagonPtr, sharedTextPtr);
	TkTextTagSetDecrRefCount(additionalTagoffPtr);
    } else {
	TagSetAssign(&nodePtr->tagoffPtr, nodePtr->tagonPtr);
    }

    /*
     * Update tag roots.
     */

    if (tagRootInfoPtr) {
	for (i = TkTextTagSetFindFirst(tagInfoPtr);
		i != TK_TEXT_TAG_SET_NPOS;
		i = TkTextTagSetFindNext(tagInfoPtr, i)) {
	    TkTextTag *tagPtr = sharedTextPtr->tagLookup[i];

	    assert(tagPtr);
	    assert(!tagPtr->isDisabled);

	    if (TkTextTagSetTest(tagRootInfoPtr, i)) {
		tagPtr->rootPtr = nodePtr;
	    } else if (tagPtr->rootPtr == nodePtr) {
		tagPtr->rootPtr = NULL;
	    }
	}

	TkTextTagSetDecrRefCount(tagRootInfoPtr);
    } else {
	tagInfoPtr = TkTextTagSetRemove(tagInfoPtr, nodePtr->tagonPtr);

	for (i = TkTextTagSetFindFirst(tagInfoPtr);
		i != TK_TEXT_TAG_SET_NPOS;
		i = TkTextTagSetFindNext(tagInfoPtr, i)) {
	    TkTextTag *tagPtr = sharedTextPtr->tagLookup[i];

	    assert(tagPtr);
	    assert(!tagPtr->isDisabled);
	    tagPtr->rootPtr = NULL;
	}
    }

    TkTextTagSetDecrRefCount(tagInfoPtr);
    return affectedTagInfoPtr;
}

static bool
CheckIfAnyTagIsAffected(
    TkSharedText *sharedTextPtr,
    const TkTextTagSet *tagInfoPtr,
    bool discardSelection)
{
    unsigned i;

    for (i = TkTextTagSetFindFirst(tagInfoPtr);
	    i != TK_TEXT_TAG_SET_NPOS;
	    i = TkTextTagSetFindNext(tagInfoPtr, i)) {
	TkTextTag *tagPtr = sharedTextPtr->tagLookup[i];

	assert(tagPtr);
	assert(!tagPtr->isDisabled);

	if (!discardSelection || !TkBitTest(sharedTextPtr->selectionTags, tagPtr->index)) {
	    return true;
	}
    }

    return false;
}

TkTextTag *
TkBTreeClearTags(
    TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    TkText *textPtr,			/* Information about text widget, can be NULL. */
    const TkTextIndex *indexPtr1,	/* Start clearing tags here. */
    const TkTextIndex *indexPtr2,	/* Stop clearing tags here. */
    TkTextUndoInfo *undoInfo,		/* Store undo information, can be NULL. */
    bool discardSelection,		/* Discard the special selection tag (do not delete)? */
    TkTextTagChangedProc changedProc)	/* Trigger this callback when any tag will be added/removed. */
{
    TkTextTag *chainPtr;
    UndoTokenTagClear *undoToken;
    TkTextSegment *segPtr1, *segPtr2;
    TkTextTagSet *affectedTagInfoPtr;
    TkTextLine *linePtr1, *linePtr2;
    TkTextIndex startIndex, endIndex;
    Node *rootPtr;
    bool wholeText;
    unsigned i;

    assert(TkTextIndexCompare(indexPtr1, indexPtr2) <= 0);
    /* assert(changedProc); */	/* MSVC erroneously triggers warning C4550: expression
				 * evaluates to a function which is missing an argument list
				 */

    if (TkTextIndexIsEqual(indexPtr1, indexPtr2)) {
	return NULL;
    }

    linePtr1 = TkTextIndexGetLine(indexPtr1);
    linePtr2 = TkTextIndexGetLine(indexPtr2);
    rootPtr = FindCommonParent(linePtr1->parentPtr, linePtr2->parentPtr);

    if (discardSelection
	    ? TkTextTagBitContainsSet(sharedTextPtr->selectionTags, rootPtr->tagonPtr)
	    : rootPtr->tagonPtr == sharedTextPtr->emptyTagInfoPtr) {
	return NULL; /* there is nothing to do */
    }

    /*
     * Try to restrict the range, because in general we have to process all the segments
     * inside the range, and this is a bit expensive. The search for smaller bounds is
     * quite fast because it uses the B-Tree. But if the range is small, then it's not
     * worth to search for smaller bounds.
     */

    if (linePtr1->parentPtr != linePtr2->parentPtr) {
	const TkTextSegment *segPtr;
	TkTextIndex oneBack;

	segPtr = TkBTreeFindNextTagged(indexPtr1, indexPtr2,
		discardSelection ? sharedTextPtr->selectionTags : NULL);
	if (!segPtr) {
	    return NULL;
	}
	TkTextIndexClear2(&startIndex, NULL, sharedTextPtr->tree);
	TkTextIndexSetSegment(&startIndex, (TkTextSegment *) segPtr);
	TkTextIndexBackChars(textPtr, indexPtr1, 1, &oneBack, COUNT_DISPLAY_INDICES);
	segPtr = TkBTreeFindPrevTagged(&oneBack, indexPtr1, discardSelection);
	assert(segPtr);
	TkTextIndexClear2(&endIndex, NULL, sharedTextPtr->tree);
	TkTextIndexSetSegment(&endIndex, (TkTextSegment *) segPtr);
	assert(TkTextIndexCompare(&startIndex, &endIndex) <= 0);
    } else {
	startIndex = *indexPtr1;
	endIndex = *indexPtr2;
    }

    if (!FindSplitPoints(sharedTextPtr, &startIndex, &endIndex, NULL, false, &segPtr1, &segPtr2)) {
	return NULL;
    }

    linePtr1 = TkTextIndexGetLine(&startIndex);
    linePtr2 = TkTextIndexGetLine(&endIndex);
    segPtr1->protectionFlag = true;
    segPtr2->protectionFlag = true;
    undoToken = NULL;
    chainPtr = NULL;
    wholeText = false;

    /*
     * Now we will test whether we can accelerate a frequent case: all tagged segments
     * will be cleared. But if the range is small, then it's not worth to test for this
     * case.
     */

    if (!undoInfo) {
	if (TkTextIndexIsStartOfText(indexPtr1) && TkTextIndexIsEndOfText(indexPtr2)) {
	    wholeText = true;
	} else if (linePtr1->parentPtr != linePtr2->parentPtr) {
	    TkTextIndex index1, index2;

	    wholeText = true;

	    if (TkTextIndexBackChars(textPtr, indexPtr1, 1, &index1, COUNT_DISPLAY_INDICES)) {
		TkTextIndexSetupToStartOfText(&index2, textPtr, sharedTextPtr->tree);
		if (TkBTreeFindPrevTagged(&index1, &index2, discardSelection)) {
		    wholeText = false;
		}
	    }

	    if (wholeText && !TkTextIndexIsEndOfText(indexPtr2)) {
		TkTextIndexSetupToEndOfText(&index2, textPtr, sharedTextPtr->tree);
		if (TkBTreeFindNextTagged(indexPtr2, &index2,
			discardSelection ? sharedTextPtr->selectionTags : NULL)) {
		    wholeText = false;
		}
	    }
	}
    }

    TkTextTagSetIncrRefCount(affectedTagInfoPtr = sharedTextPtr->emptyTagInfoPtr);

    if (!wholeText || CheckIfAnyTagIsAffected(sharedTextPtr, rootPtr->tagonPtr, discardSelection)) {
	bool anyChanges = wholeText; /* already checked for this case */
	ClearTagsData data;

	memset(&data, 0, sizeof(data));
	rootPtr = TkBTreeGetRoot(sharedTextPtr->tree); /* we must start at top level */

	if (TkBTreeHaveElidedSegments(sharedTextPtr)) {
	    UpdateElideInfo(sharedTextPtr, NULL, &segPtr1, &segPtr2, ELISION_WILL_BE_REMOVED);
	}

	if (wholeText) {
	    assert(!undoInfo);
	    TagSetAssign(&affectedTagInfoPtr, rootPtr->tagonPtr);
	    ClearTagsFromAllNodes(sharedTextPtr, rootPtr, &data, discardSelection, changedProc, textPtr);
	    ClearTagRoots(sharedTextPtr, affectedTagInfoPtr);
	    if (TkTextTagSetIntersectsBits(affectedTagInfoPtr, sharedTextPtr->affectDisplayTags)) {
		/* TODO: probably it's better to search for all affected ranges. */
		/* TODO: probably it's better to delegate the redraw to ClearTagsFromAllNodes,
		 *       especially because of 'affectsDisplayGeometry'. */
		bool affectsDisplayGeometry = TestIfDisplayGeometryIsAffected(sharedTextPtr,
			affectedTagInfoPtr, discardSelection);
		changedProc(sharedTextPtr, textPtr, &startIndex, &endIndex,
			NULL, affectsDisplayGeometry);
	    }
	} else {
	    TkTextSegment *firstPtr, *lastPtr;
	    unsigned lineNo1, lineNo2;

	    if (undoInfo) {
		undoToken = malloc(sizeof(UndoTokenTagClear));
		undoInfo->token = (TkTextUndoToken *) undoToken;
		undoInfo->byteSize = 0;
		undoToken->undoType = &undoTokenClearTagsType;
		undoToken->changeList = NULL;
		undoToken->changeListSize = 0;
		DEBUG_ALLOC(tkTextCountNewUndoToken++);
	    }

	    firstPtr = segPtr1;
	    if (TkTextIndexIsStartOfLine(&endIndex)) {
		lastPtr = NULL;
		linePtr2 = linePtr2->prevPtr;
	    } else {
		lastPtr = segPtr2;
	    }
	    lineNo1 = TkBTreeLinesTo(sharedTextPtr->tree, NULL, linePtr1, NULL);
	    lineNo2 = (linePtr1 == linePtr2) ?
		    lineNo1 : TkBTreeLinesTo(sharedTextPtr->tree, NULL, linePtr2, NULL);

	    affectedTagInfoPtr = ClearTagsFromNode(sharedTextPtr, rootPtr, 0, lineNo1, lineNo2,
	    		firstPtr, lastPtr, affectedTagInfoPtr, undoToken, &data, discardSelection,
			true, changedProc, textPtr);
	    anyChanges = CheckIfAnyTagIsAffected(sharedTextPtr, affectedTagInfoPtr, discardSelection);

	    if (undoToken) {
		if (anyChanges
			&& !TkTextTagBitContainsSet(sharedTextPtr->selectionTags, affectedTagInfoPtr)) {
		    TkTextIndex index1 = startIndex;
		    TkTextIndex index2 = endIndex;

		    assert(data.lastSegPtr);
		    TkTextIndexSetSegment(&index1, data.firstSegPtr);
		    if (data.lastSegPtr->nextPtr) {
			data.lastSegPtr = data.lastSegPtr->nextPtr;
		    } else if (data.lastSegPtr->sectionPtr->linePtr->nextPtr) {
			data.lastSegPtr = data.lastSegPtr->sectionPtr->linePtr->nextPtr->segPtr;
		    }
		    if (data.lastSegPtr->sectionPtr->linePtr == GetLastLine(sharedTextPtr, textPtr)) {
			data.lastSegPtr = textPtr->endMarker;
		    }
		    MakeUndoIndex(sharedTextPtr, &index1, &undoToken->startIndex, GRAVITY_LEFT);
		    MakeUndoIndex(sharedTextPtr, &index2, &undoToken->endIndex, GRAVITY_RIGHT);
		} else {
		    undoToken->changeListSize = 0;
		}
	    }
	}

	if (anyChanges) {
	    if (!wholeText) {
		if (!TkTextIndexIsStartOfLine(&startIndex)) {
		    RecomputeLineTagInfo(linePtr1, NULL, sharedTextPtr);
		    if (linePtr1 == linePtr2) {
			linePtr2 = NULL;
		    }
		}
		if (linePtr2 && !TkTextIndexIsStartOfLine(&endIndex)) {
		    RecomputeLineTagInfo(linePtr2, NULL, sharedTextPtr);
		}
	    }

	    /*
	     * Build a chain of all affected tags.
	     */

	    for (i = TkTextTagSetFindFirst(affectedTagInfoPtr);
		    i != TK_TEXT_TAG_SET_NPOS;
		    i = TkTextTagSetFindNext(affectedTagInfoPtr, i)) {
		TkTextTag *tagPtr = sharedTextPtr->tagLookup[i];

		assert(tagPtr);
		assert(!tagPtr->isDisabled);

		tagPtr->nextPtr = chainPtr;
		tagPtr->epoch = 0;
		chainPtr = tagPtr;
	    }
	    TkTextTagSetDecrRefCount(affectedTagInfoPtr);
	    TkBTreeIncrEpoch(sharedTextPtr->tree);
	}
    }

    if (undoToken) {
	if (undoToken->changeListSize == 0) {
	    free(undoToken->changeList);
	    free(undoToken);
	    undoInfo->token = NULL;
	    DEBUG_ALLOC(tkTextCountNewUndoToken--);
	} else {
	    undoToken->changeList = realloc(undoToken->changeList,
		    undoToken->changeListSize * sizeof(undoToken->changeList[0]));
	}
    }

    assert(segPtr1 != segPtr2);
    CleanupSplitPoint(segPtr1, sharedTextPtr);
    CleanupSplitPoint(segPtr2, sharedTextPtr);

    TK_BTREE_DEBUG(TkBTreeCheck(indexPtr1->tree));
    return chainPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * FindTagStart --
 *
 *	Find the start of the next range of a tag.
 *
 * Results:
 *	The return value is a pointer to the first segment which is associated
 *	with given tag when searching forward. The values of 'searchPtr' will
 *	be set according to the search result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkTextSegment *
FindTagStartInLine(
    TkTextSearch *searchPtr,
    TkTextLine *linePtr,
    TkTextSegment *segPtr,
    bool testTagon)
{
    TkTextIndex *indexPtr = &searchPtr->curIndex;
    const TkTextTag *tagPtr = searchPtr->tagPtr;
    const TkTextSegment *lastPtr;
    int byteOffset;

    assert(tagPtr);

    if (LineTestAllSegments(linePtr, tagPtr, testTagon)) {
	if (!segPtr) {
	    TkTextIndexSetToStartOfLine2(indexPtr, linePtr);
	} else {
	    TkTextIndexSetSegment(indexPtr, segPtr);
	}
	segPtr = TkTextIndexGetContentSegment(indexPtr, NULL);
	return segPtr;
    }

    if (segPtr) {
	byteOffset = TkTextIndexGetByteIndex(indexPtr);
    } else {
	assert(!searchPtr->textPtr || linePtr != searchPtr->textPtr->startMarker->sectionPtr->linePtr);
	segPtr = linePtr->segPtr;
	byteOffset = 0;
    }
    lastPtr = (linePtr == searchPtr->lastLinePtr) ? searchPtr->lastPtr : NULL;

    while (segPtr != lastPtr) {
	if (segPtr->tagInfoPtr) {
	    if (TkTextTagSetTest(segPtr->tagInfoPtr, tagPtr->index) == testTagon) {
		TkTextIndexSetByteIndex2(indexPtr, linePtr, byteOffset);
		return segPtr;
	    }
	    byteOffset += segPtr->size;
	}
	segPtr = segPtr->nextPtr;
    }

    return NULL;
}

static const Node *
FindTagStartInSubtree(
    const Node *nodePtr,
    unsigned startLineNo,
    unsigned endLineNo,
    unsigned lineNumber,
    const Node *excludePtr,	/* we don't want this result */
    unsigned tagIndex)
{
    assert(nodePtr->level > 0);

    for (nodePtr = nodePtr->childPtr; nodePtr; nodePtr = nodePtr->nextPtr) {
	if (nodePtr != excludePtr && startLineNo < lineNumber + nodePtr->numLines) {
	    bool testTagon = !LineTestIfToggleIsOpen(nodePtr->linePtr->prevPtr, tagIndex);

	    if (NodeTestToggleFwd(nodePtr, tagIndex, testTagon)) {
		const Node *nPtr;

		if (nodePtr->level == 0) {
		    return nodePtr;
		}
		nPtr = FindTagStartInSubtree(
			nodePtr, startLineNo, endLineNo, lineNumber, excludePtr, tagIndex);
		if (nPtr) {
		    return nPtr;
		}
	    }
	}
	if ((lineNumber += nodePtr->numLines) > endLineNo) {
	    return NULL;
	}
    }

    return NULL;
}

static TkTextSegment *
FindTagStart(
    TkTextSearch *searchPtr,
    const TkTextIndex *stopIndex)
{
    TkTextIndex *indexPtr = &searchPtr->curIndex;
    const TkTextTag *tagPtr = searchPtr->tagPtr;
    TkTextLine *linePtr;
    const TkTextLine *lastLinePtr;
    const TkTextLine *lastPtr;
    TkTextSegment *segPtr;
    bool testTagon;
    const Node *nodePtr;
    const Node *rootPtr;
    unsigned startLineNumber;
    unsigned endLineNumber;
    unsigned lineNumber;
    unsigned tagIndex;

    assert(tagPtr);

    if (!tagPtr->rootPtr) {
	return NULL;
    }

    tagIndex = tagPtr->index;
    linePtr = TkTextIndexGetLine(indexPtr);
    lastLinePtr = searchPtr->lastLinePtr;
    testTagon = !LineTestIfToggleIsOpen(linePtr->prevPtr, tagIndex);

    if (LineTestToggleFwd(linePtr, tagIndex, testTagon)) {
	TkTextSegment *sPtr;

	segPtr = TkTextIndexGetContentSegment(&searchPtr->curIndex, NULL);

	if (!TkTextTagSetTest(testTagon ? linePtr->tagoffPtr : linePtr->tagonPtr, tagIndex)) {
	    return segPtr;
	}
	if (searchPtr->mode == SEARCH_EITHER_TAGON_TAGOFF) {
	    sPtr = GetFirstTagInfoSegment(searchPtr->textPtr, linePtr);
	    for ( ; sPtr != segPtr; sPtr = sPtr->nextPtr) {
		if (sPtr->tagInfoPtr && TkTextTagSetTest(sPtr->tagInfoPtr, tagIndex) == testTagon) {
		    testTagon = !testTagon;
		}
	    }
	}
	if ((segPtr = FindTagStartInLine(searchPtr, linePtr, segPtr, testTagon))) {
	    return segPtr;
	}
	if (linePtr == lastLinePtr) {
	    return NULL;
	}
	testTagon = !LineTestIfToggleIsOpen(linePtr, tagIndex);
    } else if (linePtr == lastLinePtr) {
	return NULL;
    }

    nodePtr = linePtr->parentPtr;
    if (TkTextTagSetTest(testTagon ? nodePtr->tagonPtr : nodePtr->tagoffPtr, tagIndex)) {
	lastPtr = nodePtr->lastPtr->nextPtr;

	while ((linePtr = linePtr->nextPtr) != lastPtr) {
	    if (LineTestToggleFwd(linePtr, tagIndex, testTagon)) {
		return FindTagStartInLine(searchPtr, linePtr, NULL, testTagon);
	    }
	    if (linePtr == lastLinePtr) {
		return NULL;
	    }
	}
    }

    rootPtr = tagPtr->rootPtr;
    if (rootPtr == nodePtr) {
	if (!nodePtr->nextPtr) {
	    Node *parentPtr = nodePtr->parentPtr;

	    while (parentPtr && !parentPtr->nextPtr) {
		parentPtr = parentPtr->parentPtr;
	    }
	    if (!parentPtr) {
		return NULL;
	    }
	    nodePtr = parentPtr->nextPtr;
	}
	linePtr = nodePtr->nextPtr->linePtr;
	lineNumber = TkBTreeLinesTo(indexPtr->tree, NULL, linePtr, NULL);
	if (lineNumber > TkTextIndexGetLineNumber(stopIndex, NULL)) {
	    return NULL;
	}
	segPtr = linePtr->segPtr;
	while (!segPtr->tagInfoPtr && segPtr != searchPtr->lastPtr) {
	    segPtr = segPtr->nextPtr;
	}
	return segPtr == searchPtr->lastPtr ? NULL : segPtr;
    }

    startLineNumber = TkTextIndexGetLineNumber(indexPtr, NULL);
    endLineNumber = TkTextIndexGetLineNumber(stopIndex, NULL);
    lineNumber = TkBTreeLinesTo(indexPtr->tree, NULL, rootPtr->linePtr, NULL);

    if (lineNumber > endLineNumber || startLineNumber >= lineNumber + rootPtr->numLines) {
	return NULL;
    }

    if (rootPtr->level == 0) {
	nodePtr = rootPtr;
    } else {
	nodePtr = FindTagStartInSubtree(
		rootPtr, startLineNumber, endLineNumber, lineNumber, nodePtr, tagPtr->index);
	if (!nodePtr) {
	    return NULL;
	}
	lineNumber = TkBTreeLinesTo(indexPtr->tree, NULL, nodePtr->linePtr, NULL);
    }

    assert(nodePtr->level == 0);
    assert(lineNumber >= startLineNumber);

    lastPtr = nodePtr->lastPtr->nextPtr;
    testTagon = !LineTestIfToggleIsOpen(linePtr->prevPtr, tagIndex);

    for (linePtr = nodePtr->linePtr; linePtr != lastPtr; linePtr = linePtr->nextPtr) {
	if (LineTestToggleFwd(linePtr, tagIndex, testTagon)) {
	    return FindTagStartInLine(searchPtr, linePtr, NULL, testTagon);
	}
	if (linePtr == lastLinePtr) {
	    return NULL;
	}
    }

    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FindTagEnd --
 *
 *	Find the start of the current range of a tag.
 *
 * Results:
 *	The return value is a pointer to the first segment which is associated
 *	with given tag when searching backward. The values of 'searchPtr' will
 *	be set according to the search result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static bool
HasLeftNode(
    const Node *nodePtr)
{
    assert(nodePtr);
    return nodePtr->parentPtr && nodePtr->parentPtr->childPtr != nodePtr;
}

static TkTextSegment *
FindTagEndInLine(
    TkTextSearch *searchPtr,
    TkTextLine *linePtr,
    TkTextSegment *segPtr,
    bool testTagon)
{
    TkTextIndex *indexPtr = &searchPtr->curIndex;
    const TkTextTag *tagPtr = searchPtr->tagPtr;
    TkTextSegment *lastPtr;
    TkTextSegment *firstPtr;
    TkTextSegment *prevPtr;
    int byteOffset, offset = 0;

    assert(tagPtr);

    if (LineTestAllSegments(linePtr, tagPtr, testTagon)) {
	if (!segPtr || linePtr != searchPtr->lastLinePtr) {
	    TkTextIndexSetToStartOfLine2(indexPtr, linePtr);
	} else {
	    lastPtr = searchPtr->lastPtr;
	    while (segPtr && segPtr != lastPtr) {
		segPtr = segPtr->prevPtr;
	    }
	    TkTextIndexSetSegment(indexPtr, segPtr);
	}
	segPtr = TkTextIndexGetContentSegment(indexPtr, NULL);
	return segPtr;
    }

    if (segPtr) {
	byteOffset = TkTextIndexGetByteIndex(indexPtr);
    } else if (searchPtr->textPtr && linePtr == searchPtr->textPtr->endMarker->sectionPtr->linePtr) {
	segPtr = searchPtr->textPtr->endMarker;
        byteOffset = TkTextSegToIndex(segPtr);
    } else {
	segPtr = linePtr->lastPtr;
	byteOffset = linePtr->size - segPtr->size;
    }
    lastPtr = (linePtr == searchPtr->lastLinePtr) ? searchPtr->lastPtr : NULL;
    firstPtr = prevPtr = NULL;

    while (segPtr) {
	if (segPtr->tagInfoPtr) {
	    if (TkTextTagSetTest(segPtr->tagInfoPtr, tagPtr->index)) {
		if (prevPtr) {
		    TkTextIndexSetByteIndex2(indexPtr, linePtr, offset);
		    return prevPtr;
		}
		firstPtr = segPtr;
	    } else if (firstPtr) {
		TkTextIndexSetByteIndex2(indexPtr, linePtr, offset);
		return firstPtr;
	    } else if (!testTagon) {
		prevPtr = segPtr;
	    }
	    offset = byteOffset;
	}
	if (segPtr == lastPtr) {
	    break;
	}
	if ((segPtr = segPtr->prevPtr)) {
	    byteOffset -= segPtr->size;
	}
    }

    if (firstPtr
	    && firstPtr == GetFirstTagInfoSegment(searchPtr->textPtr, linePtr)
	    && !LineTestIfToggleIsOpen(linePtr->prevPtr, tagPtr->index)) {
	TkTextIndexSetByteIndex2(&searchPtr->curIndex, linePtr, offset);
	return firstPtr;
    }

    return NULL;
}

static const Node *
FindTagEndInSubtree(
    const Node *nodePtr,
    unsigned startLineNo,	/* start of search interval */
    unsigned endLineNo,		/* end of search interval */
    unsigned lineNumber,	/* line number of last line in this node */
    const Node *excludePtr,	/* we don't want this result */
    unsigned tagIndex)
{
    const Node *stack[MAX_CHILDREN];
    unsigned count = 0;

    assert(nodePtr->level > 0);

    lineNumber -= nodePtr->numLines - 1; /* now it's the line number of first line in this node */

    for (nodePtr = nodePtr->childPtr; nodePtr; nodePtr = nodePtr->nextPtr) {
	stack[count++] = nodePtr;
	lineNumber += nodePtr->numLines;
	if (startLineNo < lineNumber) {
	    break;
	}
    }

    lineNumber -= 1; /* now it's the line number of the last line in last node */

    while (count > 0) {
	nodePtr = stack[--count];
	if (nodePtr != excludePtr && startLineNo >= lineNumber - nodePtr->numLines + 1) {
	    bool testTagon = !LineTestIfToggleIsClosed(nodePtr->lastPtr->nextPtr, tagIndex);

	    if (NodeTestToggleBack(nodePtr, tagIndex, testTagon)) {
		const Node *nPtr;

		if (nodePtr->level == 0) {
		    return nodePtr;
		}
		nPtr = FindTagEndInSubtree(
			nodePtr, startLineNo, endLineNo, lineNumber, excludePtr, tagIndex);
		if (nPtr) {
		    return nPtr;
		}
	    }
	}
	if ((lineNumber -= nodePtr->numLines) + 1 <= endLineNo) {
	    return NULL;
	}
    }

    return NULL;
}

static TkTextSegment *
FindTagEnd(
    TkTextSearch *searchPtr,
    const TkTextIndex *stopIndex)
{
    TkTextIndex *indexPtr = &searchPtr->curIndex;
    const TkTextTag *tagPtr = searchPtr->tagPtr;
    TkTextLine *linePtr;
    const TkTextLine *lastLinePtr, *lastPtr;
    TkTextSegment *segPtr;
    bool testTagon;
    const Node *nodePtr;
    const Node *rootPtr;
    unsigned startLineNumber;
    unsigned endLineNumber;
    unsigned lineNumber;
    unsigned tagIndex;

    assert(tagPtr);

    if (!tagPtr->rootPtr) {
	return NULL;
    }

    tagIndex = tagPtr->index;
    linePtr = TkTextIndexGetLine(indexPtr);
    lastLinePtr = searchPtr->lastLinePtr;
    testTagon = !LineTestIfToggleIsClosed(linePtr->nextPtr, tagIndex);

    /*
     * Here testTagon == true means: test for the segment which starts the tagged region.
     */

    if (LineTestToggleBack(linePtr, tagIndex, testTagon)) {
	TkTextSegment *sPtr;

	segPtr = TkTextIndexGetContentSegment(&searchPtr->curIndex, NULL);

	for (sPtr = linePtr->lastPtr; sPtr != segPtr; sPtr = sPtr->prevPtr) {
	    if (sPtr->tagInfoPtr && TkTextTagSetTest(sPtr->tagInfoPtr, tagIndex) != testTagon) {
		testTagon = !testTagon;
	    }
	}
	if ((segPtr = FindTagEndInLine(searchPtr, linePtr, segPtr, testTagon))) {
	    return segPtr;
	}
	if (linePtr == lastLinePtr) {
	    return NULL;
	}
	testTagon = !LineTestIfToggleIsClosed(linePtr, tagIndex);
    } else if (linePtr == lastLinePtr) {
	return NULL;
    }

    nodePtr = linePtr->parentPtr;
    if (TkTextTagSetTest(testTagon ? nodePtr->tagonPtr : nodePtr->tagoffPtr, tagIndex)) {
	lastPtr = nodePtr->linePtr->prevPtr;

	while ((linePtr = linePtr->prevPtr) != lastPtr) {
	    if (LineTestToggleBack(linePtr, tagIndex, testTagon)) {
		return FindTagEndInLine(searchPtr, linePtr, NULL, testTagon);
	    }
	    if (linePtr == lastLinePtr) {
		return NULL;
	    }
	}
    }

    rootPtr = tagPtr->rootPtr;
    if (rootPtr == nodePtr) {
	const Node *nPtr, *prevPtr = NULL;

	if (!HasLeftNode(nodePtr)) {
	    Node *parentPtr = nodePtr->parentPtr;

	    while (parentPtr && !HasLeftNode(parentPtr)) {
		parentPtr = parentPtr->parentPtr;
	    }
	    if (!parentPtr) {
		return NULL;
	    }
	    nodePtr = parentPtr;
	}
	for (nPtr = nodePtr->parentPtr->childPtr; nPtr != nodePtr; nPtr = nPtr->nextPtr) {
	    prevPtr = nPtr;
	}
	if (!prevPtr || !(linePtr = prevPtr->lastPtr->prevPtr)) {
	    return NULL;
	}
	lineNumber = TkBTreeLinesTo(indexPtr->tree, NULL, linePtr, NULL);
	if (lineNumber < TkTextIndexGetLineNumber(stopIndex, NULL)) {
	    return NULL;
	}
	return linePtr->lastPtr == searchPtr->lastPtr ? NULL : linePtr->lastPtr;
    }

    startLineNumber = TkTextIndexGetLineNumber(indexPtr, NULL);
    endLineNumber = TkTextIndexGetLineNumber(stopIndex, NULL);
    lineNumber = TkBTreeLinesTo(indexPtr->tree, NULL, rootPtr->lastPtr, NULL);

    if (endLineNumber > lineNumber || lineNumber >= startLineNumber + rootPtr->numLines) {
	return NULL;
    }

    if (rootPtr->level == 0) {
	nodePtr = rootPtr;
    } else {
	nodePtr = FindTagEndInSubtree(rootPtr, startLineNumber, endLineNumber,
		lineNumber, nodePtr, tagPtr->index);
	if (!nodePtr) {
	    return NULL;
	}
	lineNumber = TkBTreeLinesTo(indexPtr->tree, NULL, nodePtr->lastPtr, NULL);
    }

    assert(nodePtr->level == 0);
    assert(lineNumber <= startLineNumber);

    if (!testTagon && NodeTestAllSegments(nodePtr, tagIndex, true)) {
	linePtr = nodePtr->lastPtr;
	if (linePtr->nextPtr) { linePtr = linePtr->nextPtr; }
	TkTextIndexSetToStartOfLine2(&searchPtr->curIndex, linePtr);
	return linePtr->segPtr;
    }

    lastPtr = nodePtr->linePtr->prevPtr;
    testTagon = !LineTestIfToggleIsClosed(linePtr, tagIndex);

    for (linePtr = nodePtr->lastPtr; linePtr != lastPtr; linePtr = linePtr->prevPtr) {
	if (LineTestToggleBack(linePtr, tagIndex, testTagon)) {
	    return FindTagEndInLine(searchPtr, linePtr, NULL, testTagon);
	}
	if (linePtr == lastLinePtr) {
	    return NULL;
	}
    }

    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeStartSearch --
 *
 *	This function sets up a search for tag transitions involving a given
 *	tag in a given range of the text.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The information at *searchPtr is set up so that subsequent calls to
 *	TkBTreeNextTag or TkBTreePrevTag will return information about the
 *	locations of tag transitions. Note that TkBTreeNextTag or
 *	TkBTreePrevTag must be called to get the first transition. Note:
 *	unlike TkBTreeNextTag and TkBTreePrevTag, this routine does not
 *	guarantee that searchPtr->curIndex is equal to *indexPtr1. It may be
 *	greater than that if *indexPtr1 is less than the first tag transition.
 *
 *----------------------------------------------------------------------
 */

static bool
TestPrevSegmentIsTagged(
    const TkTextIndex *indexPtr,
    const TkTextTag *tagPtr)
{
    const TkTextLine *linePtr = TkTextIndexGetLine(indexPtr);
    const TkTextLine *startLinePtr = indexPtr->textPtr ? TkBTreeGetStartLine(indexPtr->textPtr) : NULL;
    const TkTextSegment *segPtr = NULL; /* avoid compiler warning */

    if (linePtr == startLinePtr) {
	if (!(segPtr = GetPrevTagInfoSegment(indexPtr->textPtr->startMarker))) {
	    return false;
	}
    } else if (linePtr->prevPtr) {
	const TkTextLine *endLinePtr = indexPtr->textPtr ? TkBTreeGetStartLine(indexPtr->textPtr) : NULL;

	if (linePtr->prevPtr == endLinePtr) {
	    if (TkTextIsDeadPeer(indexPtr->textPtr)) {
		return false;
	    }
	    segPtr = GetPrevTagInfoSegment(indexPtr->textPtr->endMarker);
	} else {
	    segPtr = linePtr->prevPtr->lastPtr;
	}
    }

    return TkTextTagSetTest(segPtr->tagInfoPtr, tagPtr->index);
}

void
TkBTreeStartSearch(
    const TkTextIndex *indexPtr1,
    				/* Search starts here. Tag toggles at this position will be returned. */
    const TkTextIndex *indexPtr2,
    				/* Search stops here. Tag toggles at this position *will* not be
				 * returned. */
    const TkTextTag *tagPtr,	/* Tag to search for. */
    TkTextSearch *searchPtr,	/* Where to store information about search's progress. */
    TkTextSearchMode mode)	/* The search mode, see definition of TkTextSearchMode. */
{
    TkTextSegment *segPtr;
    int offset, nlines, lineNo;

    assert(tagPtr);

    /*
     * Find the segment that contains the first toggle for the tag. This may
     * become the starting point in the search.
     */

    searchPtr->textPtr = indexPtr1->textPtr;
    searchPtr->curIndex = *indexPtr1;
    searchPtr->tagPtr = tagPtr;
    searchPtr->segPtr = NULL;
    searchPtr->tagon = true;
    searchPtr->endOfText = false;
    searchPtr->linesLeft = 0;
    searchPtr->resultPtr = NULL;
    searchPtr->mode = mode;

    if (TkTextIndexCompare(indexPtr1, indexPtr2) >= 0) {
	return;
    }

    segPtr = TkTextIndexGetContentSegment(indexPtr1, &offset);
    if (offset > 0) {
	if (segPtr->nextPtr) {
	    int byteOffset = TkTextIndexGetByteIndex(indexPtr1);

	    TkTextIndexSetPosition(&searchPtr->curIndex,
		    byteOffset + segPtr->size - offset, segPtr->nextPtr);
	    segPtr = segPtr->nextPtr;
	} else {
	    TkTextLine *linePtr = segPtr->sectionPtr->linePtr;

	    if (linePtr == TkTextIndexGetLine(indexPtr2)
		    || (linePtr = linePtr->nextPtr) == TkTextIndexGetLine(indexPtr2)) {
		return;
	    }
	    TkTextIndexSetToStartOfLine2(&searchPtr->curIndex, linePtr);
	    segPtr = GetFirstTagInfoSegment(NULL, linePtr);
	}
    }

    if (indexPtr2->textPtr && TkTextIndexIsEndOfText(indexPtr2)) {
	/* In this case indexPtr2 points to start of last line, but we need end marker. */
	searchPtr->lastPtr = indexPtr2->textPtr->endMarker;
	offset = 0;
    } else {
	searchPtr->lastPtr = TkTextIndexGetContentSegment(indexPtr2, &offset);
    }
    searchPtr->lastLinePtr = searchPtr->lastPtr->sectionPtr->linePtr;
    if (offset > 0) {
	searchPtr->lastPtr = searchPtr->lastPtr->nextPtr;
    }
    if (segPtr == searchPtr->lastPtr) {
	return;
    }
    if (TkTextIndexIsEndOfText(indexPtr2)) {
	searchPtr->endOfText = true;
    }

    if (mode == SEARCH_NEXT_TAGON
	    && TkTextIndexIsStartOfText(indexPtr1)
	    && TkTextTagSetTest(segPtr->tagInfoPtr, tagPtr->index)) {
	/*
	 * We must find start of text.
	 */
	searchPtr->segPtr = segPtr;
	searchPtr->resultPtr = segPtr;
    } else if (!(searchPtr->resultPtr = FindTagStart(searchPtr, indexPtr2))) {
	if (mode == SEARCH_EITHER_TAGON_TAGOFF
	    	&& searchPtr->endOfText
		&& TestPrevSegmentIsTagged(indexPtr2, tagPtr)) {
	    /*
	     * We must find end of text.
	     */
	    searchPtr->resultPtr = TkTextIndexGetContentSegment(indexPtr2, NULL);
	    searchPtr->curIndex = *indexPtr2;
	    searchPtr->segPtr = NULL;
	    searchPtr->linesLeft = 0;
	    searchPtr->tagon = false;
	}
	return;
    } else if (!TkTextTagSetTest(searchPtr->resultPtr->tagInfoPtr, tagPtr->index)) {
	searchPtr->tagon = false;
	if (mode == SEARCH_NEXT_TAGON) {
	    /*
	     * We have found tagoff, but we are searching tagon, so we have no
	     * result yet: force TkBTreeNextTag to continue the search.
	     */
	    searchPtr->segPtr = searchPtr->resultPtr;
	    TkTextIndexSetSegment(&searchPtr->curIndex, searchPtr->segPtr);
	    searchPtr->resultPtr = NULL;
	}
    }

    indexPtr1 = &searchPtr->curIndex;
    lineNo = TkTextIndexGetLineNumber(indexPtr2, indexPtr1->textPtr);
    searchPtr->linesLeft = lineNo - TkTextIndexGetLineNumber(indexPtr1, indexPtr1->textPtr) + 1;
    nlines = TkBTreeNumLines(indexPtr1->tree, indexPtr1->textPtr);
    searchPtr->linesToEndOfText = nlines - lineNo + 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeStartSearchBack --
 *
 *	This function sets up a search backwards for tag transitions involving
 *	a given tag (or all tags) in a given range of the text. In the normal
 *	case the first index (*indexPtr1) is beyond the second index
 *	(*indexPtr2).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The information at *searchPtr is set up so that subsequent calls to
 *	TkBTreePrevTag will return information about the locations of tag
 *	transitions. Note that TkBTreePrevTag must be called to get the first
 *	transition. Note: unlike TkBTreeNextTag and TkBTreePrevTag, this
 *	routine does not guarantee that searchPtr->curIndex is equal to
 *	*indexPtr1. It may be less than that if *indexPtr1 is greater than the
 *	last tag transition.
 *
 *----------------------------------------------------------------------
 */

void
TkBTreeStartSearchBack(
    const TkTextIndex *indexPtr1,
				/* Search starts here. Tag toggles at this position will not be
				 * returned iff mode is SEARCH_NEXT_TAGON. */
    const TkTextIndex *indexPtr2,
    				/* Search stops here. Tag toggles at this position *will* be returned. */
    const TkTextTag *tagPtr,	/* Tag to search for. */
    TkTextSearch *searchPtr,	/* Where to store information about search's progress. */
    TkTextSearchMode mode)	/* The search mode, see definition of TkTextSearchMode. */
{
    TkTextSegment *segPtr;
    TkTextSegment *lastPtr;
    int offset;
    int lineNo;

    assert(tagPtr);

    /*
     * Find the segment that contains the last toggle for the tag. This may
     * become the starting point in the search.
     */

    searchPtr->textPtr = indexPtr1->textPtr;
    searchPtr->curIndex = *indexPtr1;
    searchPtr->tagPtr = tagPtr;
    searchPtr->segPtr = NULL;
    searchPtr->tagon = true;
    searchPtr->endOfText = false;
    searchPtr->linesLeft = 0;
    searchPtr->resultPtr = NULL;
    searchPtr->mode = mode;

    if (TkTextIndexCompare(indexPtr1, indexPtr2) <= 0) {
	return;
    }

    if (indexPtr1->textPtr && TkTextIndexIsEndOfText(indexPtr1)) {
	/*
	 * In this case indexPtr2 points to start of last line, but we need
	 * next content segment after end marker.
	 */
	segPtr = GetNextTagInfoSegment(indexPtr1->textPtr->endMarker);
	offset = 0;
    } else {
	segPtr = TkTextIndexGetContentSegment(indexPtr1, &offset);
    }

    if (offset == 0) {
	segPtr = GetPrevTagInfoSegment(segPtr);
	TkTextIndexSetSegment(&searchPtr->curIndex, segPtr);
    } else {
	TkTextIndexAddToByteIndex(&searchPtr->curIndex, -offset);
    }

    lastPtr = searchPtr->lastPtr = TkTextIndexGetContentSegment(indexPtr2, &offset);
    if (offset == 0) {
	if (searchPtr->lastPtr->prevPtr) {
	    searchPtr->lastPtr = searchPtr->lastPtr->prevPtr;
	} else {
	    assert(searchPtr->lastPtr->sectionPtr->linePtr->prevPtr);
	    searchPtr->lastPtr = searchPtr->lastPtr->sectionPtr->linePtr->prevPtr->lastPtr;
	}
    } else if (segPtr == searchPtr->lastPtr) {
	return;
    }
    searchPtr->lastLinePtr = searchPtr->lastPtr->sectionPtr->linePtr;
    if (TkTextIndexIsStartOfText(indexPtr2)) {
	searchPtr->endOfText = true;
    }

    if (mode == SEARCH_EITHER_TAGON_TAGOFF
	    && TkTextIndexIsEndOfText(indexPtr1)
	    && TestPrevSegmentIsTagged(indexPtr1, tagPtr)) {
	/*
	 * We must find end of text.
	 */
	searchPtr->curIndex = *indexPtr1;
	searchPtr->segPtr = TkTextIndexGetContentSegment(indexPtr1, NULL);
	searchPtr->resultPtr = segPtr;
	searchPtr->tagon = false;
    } else if (!(searchPtr->resultPtr = FindTagEnd(searchPtr, indexPtr2))) {
	if (searchPtr->endOfText
		&& TkTextTagSetTest(lastPtr->tagInfoPtr, tagPtr->index)
		&& TestPrevSegmentIsTagged(indexPtr2, tagPtr)) {
	    /*
	     * We must find start of text.
	     */
	    searchPtr->resultPtr = TkTextIndexGetContentSegment(indexPtr2, NULL);
	    searchPtr->curIndex = *indexPtr2;
	    searchPtr->segPtr = NULL;
	    searchPtr->linesLeft = 0;
	    searchPtr->tagon = true;
	}
	return;
    } else if (!TkTextTagSetTest(searchPtr->resultPtr->tagInfoPtr, tagPtr->index)) {
	searchPtr->tagon = false;
	if (mode == SEARCH_NEXT_TAGON) {
	    /*
	     * We have found tagoff, but we are searching tagon, so we have no
	     * result yet: force TkBTreePrevTag to continue the search.
	     */
	    searchPtr->segPtr = searchPtr->resultPtr;
	    TkTextIndexSetSegment(&searchPtr->curIndex, searchPtr->segPtr);
	    searchPtr->resultPtr = NULL;
	}
    }

    indexPtr1 = &searchPtr->curIndex;
    searchPtr->linesToEndOfText = TkTextIndexGetLineNumber(indexPtr2, indexPtr1->textPtr);
    lineNo = TkTextIndexGetLineNumber(indexPtr1, indexPtr1->textPtr);
    searchPtr->linesLeft = lineNo - searchPtr->linesToEndOfText + 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeLiftSearch --
 *
 *	This function "lifts" the search, next TkBTreeNextTag (or TkBTreePrevTag)
 *	will search without a limitation of the range, this is especially required
 *	if we search for tagoff of a corresponding tagon.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The information at *searchPtr is set up so that subsequent calls to
 *	TkBTreeNextTag/TkBTreePrevTag will search outside of the specified
 *	range.
 *
 *----------------------------------------------------------------------
 */

void
TkBTreeLiftSearch(
    TkTextSearch *searchPtr)
{
    TkText *textPtr = searchPtr->curIndex.textPtr;

    searchPtr->lastPtr = textPtr ?
	    textPtr->endMarker : TkTextIndexGetShared(&searchPtr->curIndex)->endMarker;
    searchPtr->linesLeft += searchPtr->linesToEndOfText;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeNextTag --
 *
 *	Once a tag search has begun, successive calls to this function return
 *	successive tag toggles. Note: it is NOT SAFE to call this function if
 *	characters have been inserted into or deleted from the B-tree since
 *	the call to TkBTreeStartSearch.
 *
 * Results:
 *	The return value is 'true' if another toggle was found that met the
 *	criteria specified in the call to TkBTreeStartSearch; in this case
 *	searchPtr->curIndex gives the toggle's position and
 *	searchPtr->segPtr points to its segment. 'false' is returned if no
 *	more matching tag transitions were found; in this case
 *	searchPtr->curIndex is the same as searchPtr->stopIndex.
 *
 * Side effects:
 *	Information in *searchPtr is modified to update the state of the
 *	search and indicate where the next tag toggle is located.
 *
 *----------------------------------------------------------------------
 */

static const Node *
NextTagFindNextNode(
    const Node *nodePtr,
    TkTextSearch *searchPtr,
    bool tagon)
{
    const Node *parentPtr;
    const TkTextTag *tagPtr = searchPtr->tagPtr;

    assert(tagPtr);

    /*
     * Search forward across and up through the B-tree's node hierarchy looking for the
     * next node that has a relevant tag transition somewhere in its subtree. Be sure to
     * update linesLeft as we skip over large chunks of lines.
     */

    parentPtr = nodePtr->parentPtr;

    while (true) {
	if (!parentPtr || nodePtr == tagPtr->rootPtr) {
	    if (tagon) {
		return NULL;
	    }
	    searchPtr->linesLeft = 0;
	    return nodePtr;
	}
	if (!(nodePtr = nodePtr->nextPtr)) {
	    nodePtr = parentPtr;
	    parentPtr = nodePtr->parentPtr;
	} else if (NodeTestToggleFwd(nodePtr, tagPtr->index, tagon)) {
	    return nodePtr;
	} else if ((searchPtr->linesLeft -= nodePtr->numLines) <= 0) {
	    return NULL;
	}
    }

    return NULL; /* never reached */
}

static bool
NextTag(
    TkTextSearch *searchPtr)	/* Information about search in progress; must
				 * have been set up by call to TkBTreeStartSearch. */
{
    TkTextSegment *segPtr;
    const TkTextTag *tagPtr;
    const Node *nodePtr;
    TkTextLine *linePtr;
    bool tagon;

    assert(searchPtr->tagPtr);
    assert(searchPtr->segPtr);

    TkTextIndexAddToByteIndex(&searchPtr->curIndex, searchPtr->segPtr->size);
    linePtr = searchPtr->segPtr->sectionPtr->linePtr;
    tagPtr = searchPtr->tagPtr;
    segPtr = searchPtr->segPtr->nextPtr;
    searchPtr->segPtr = NULL;
    tagon = !searchPtr->tagon;

    /*
     * The outermost loop iterates over lines that may potentially contain a relevant
     * tag transition, starting from the current segment in the current line.
     */

    while (true) {
	const TkTextLine *lastPtr;

	if (segPtr) {
	    bool wholeLine;

	    /*
	     * Check for more tags on the current line.
	     */

	    wholeLine = LineTestAllSegments(linePtr, tagPtr, tagon);

	    while (segPtr) {
		if (segPtr == searchPtr->lastPtr) {
		    searchPtr->linesLeft = 0;
		    return false;
		}
		if (segPtr->tagInfoPtr) {
		    if (wholeLine || TkTextTagSetTest(segPtr->tagInfoPtr, tagPtr->index) == tagon) {
			searchPtr->segPtr = segPtr;
			searchPtr->tagon = tagon;
			return true;
		    }
		    if (!TkTextIndexAddToByteIndex(&searchPtr->curIndex, segPtr->size)) {
			segPtr = TkTextIndexGetFirstSegment(&searchPtr->curIndex, NULL);
		    } else {
			segPtr = segPtr->nextPtr;
		    }
		} else {
		    segPtr = segPtr->nextPtr;
		}
	    }
	}

	/*
	 * See if there are more lines associated with the current parent
	 * node. If so, go back to the top of the loop to search the next one.
	 */

	nodePtr = linePtr->parentPtr;
	lastPtr = nodePtr->lastPtr->nextPtr;

	do {
	    if (--searchPtr->linesLeft == 0) {
		return false;
	    }
	    linePtr = linePtr->nextPtr;
	} while (linePtr != lastPtr && !LineTestToggleFwd(linePtr, tagPtr->index, tagon));

	if (linePtr != lastPtr) {
	    segPtr = linePtr->segPtr;
	    TkTextIndexSetToStartOfLine2(&searchPtr->curIndex, linePtr);
	    continue; /* go back to outer loop */
	}

	if (!(nodePtr = NextTagFindNextNode(nodePtr, searchPtr, tagon))) {
	    searchPtr->linesLeft = 0;
	    return false;
	}

	if (searchPtr->linesLeft == 0) {
	    assert(nodePtr->lastPtr->nextPtr);
	    TkTextIndexSetToStartOfLine2(&searchPtr->curIndex, nodePtr->lastPtr->nextPtr);
	    searchPtr->segPtr = TkTextIndexGetContentSegment(&searchPtr->curIndex, NULL);
	    searchPtr->tagon = tagon;
	    return true;
	}

	/*
	 * At this point we've found a subtree that has a relevant tag
	 * transition. Now search down (and across) through that subtree to
	 * find the first level-0 node that has a relevant tag transition.
	 */

	while (nodePtr->level > 0) {
	    nodePtr = nodePtr->childPtr;
	    while (!NodeTestToggleFwd(nodePtr, tagPtr->index, tagon)) {
		if ((searchPtr->linesLeft -= nodePtr->numLines) <= 0) {
		    return false;
		}
		nodePtr = nodePtr->nextPtr;
		assert(nodePtr);
	    }
	}

	/*
	 * Now we're down to a level-0 node that contains a line that contains
	 * a relevant tag transition.
	 */

	linePtr = nodePtr->linePtr;
	DEBUG(lastPtr = nodePtr->lastPtr->nextPtr);

	/*
	 * Now search through the lines.
	 */

	while (!LineTestToggleFwd(linePtr, tagPtr->index, tagon)) {
	    if (--searchPtr->linesLeft == 0) {
		return false;
	    }
	    linePtr = linePtr->nextPtr;
	    assert(linePtr != lastPtr);
	}

	TkTextIndexSetToStartOfLine2(&searchPtr->curIndex, linePtr);
	segPtr = linePtr->segPtr;
    }

    return false; /* never reached */
}

bool
TkBTreeNextTag(
    TkTextSearch *searchPtr)	/* Information about search in progress; must
				 * have been set up by call to TkBTreeStartSearch. */
{
    if (searchPtr->resultPtr) {
	searchPtr->segPtr = searchPtr->resultPtr;
	searchPtr->resultPtr = NULL;
	return true;
    }

    if (searchPtr->linesLeft <= 0) {
	searchPtr->segPtr = NULL;
	return false;
    }

    if (NextTag(searchPtr)) {
	return true;
    }

    if (searchPtr->endOfText && searchPtr->tagon) {
	/* we must find end of text in this case */
	TkTextIndexSetupToEndOfText(&searchPtr->curIndex,
		searchPtr->curIndex.textPtr, searchPtr->curIndex.tree);
	searchPtr->segPtr = TkTextIndexGetContentSegment(&searchPtr->curIndex, NULL);
	searchPtr->tagon = false;
	return true;
    }

    return false;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreePrevTag --
 *
 *	Once a tag search has begun, successive calls to this function return
 *	successive tag toggles in the reverse direction. Note: it is NOT SAFE
 *	to call this function if characters have been inserted into or deleted
 *	from the B-tree since the call to TkBTreeStartSearch.
 *
 * Results:
 *	The return value is 'true' if another toggle was found that met the
 *	criteria specified in the call to TkBTreeStartSearch; in this case
 *	searchPtr->curIndex gives the toggle's position and
 *	searchPtr->segPtr points to its segment. 'false' is returned if no
 *	more matching tag transitions were found; in this case
 *	'searchPtr->curIndex' is the same as 'searchPtr->stopIndex'.
 *
 * Side effects:
 *	Information in *searchPtr is modified to update the state of the
 *	search and indicate where the next tag toggle is located.
 *
 *----------------------------------------------------------------------
 */

static const Node *
PrevTagFindPrevNode(
    const Node *nodePtr,
    TkTextSearch *searchPtr,
    bool tagon)
{
    const TkTextTag *tagPtr = searchPtr->tagPtr;
    const Node *parentPtr;
    const Node *rootPtr;

    assert(tagPtr);

    /*
     * Search backward across and up through the B-tree's node hierarchy looking for the
     * next node that has a relevant tag transition somewhere in its subtree. Be sure to
     * update linesLeft as we skip over large chunks of lines.
     */

    if (nodePtr == tagPtr->rootPtr) {
	return NULL;
    }

    parentPtr = nodePtr->parentPtr;
    rootPtr = tagPtr->rootPtr;

    do {
	const Node *nodeStack[MAX_CHILDREN];
	const Node *lastPtr = nodePtr;
	int idx = 0;

	for (nodePtr = parentPtr->childPtr; nodePtr != lastPtr; nodePtr = nodePtr->nextPtr) {
	    if (nodePtr == rootPtr) {
		if (!tagon) {
		    return NULL;
		}
		return nodePtr;
	    }
	    nodeStack[idx++] = nodePtr;
	}
	for (--idx; idx >= 0; --idx) {
	    if (NodeTestToggleBack(nodePtr = nodeStack[idx], tagPtr->index, tagon)) {
		return nodePtr;
	    }
	    if ((searchPtr->linesLeft -= nodePtr->numLines) <= 0) {
		return NULL;
	    }
	}
	nodePtr = parentPtr;
	parentPtr = parentPtr->parentPtr;
    } while (parentPtr);

    searchPtr->linesLeft = 0;
    return NULL;
}

static bool
PrevTag(
    TkTextSearch *searchPtr)	/* Information about search in progress; must
				 * have been set up by call to TkBTreeStartSearch. */
{
    TkTextSegment *segPtr;
    const TkTextTag *tagPtr;
    const Node *nodePtr;
    bool tagon;

    assert(searchPtr->tagPtr);
    assert(searchPtr->segPtr);

    tagPtr = searchPtr->tagPtr;
    segPtr = searchPtr->segPtr->prevPtr;
    searchPtr->segPtr = NULL;
    tagon = !searchPtr->tagon;

    if (segPtr) {
	TkTextIndexAddToByteIndex(&searchPtr->curIndex, -segPtr->size);
    }

    /*
     * The outermost loop iterates over lines that may potentially contain a relevant
     * tag transition, starting from the current segment in the current line.
     */

    while (true) {
	TkTextLine *linePtr;
	const TkTextLine *lastPtr;

	if (segPtr) {
	    TkTextSegment *prevPtr;
	    TkTextSegment *firstPtr;
	    int byteOffset, offset = 0;

	    /*
	     * Check for more tags in the current line.
	     */

	    linePtr = segPtr->sectionPtr->linePtr;

	    if (LineTestAllSegments(linePtr, tagPtr, tagon)) {
		if (searchPtr->lastPtr->sectionPtr->linePtr == linePtr) {
		    TkTextIndexSetSegment(&searchPtr->curIndex, searchPtr->lastPtr);
		    searchPtr->segPtr = searchPtr->lastPtr;
		} else {
		    TkTextIndexSetToStartOfLine2(&searchPtr->curIndex, linePtr);
		    searchPtr->segPtr = linePtr->segPtr;
		}
		searchPtr->tagon = tagon;
		return true;
	    }

	    prevPtr = firstPtr = NULL;
	    byteOffset = TkTextIndexGetByteIndex(&searchPtr->curIndex);

	    while (true) {
		if (segPtr->tagInfoPtr) {
		    if (TkTextTagSetTest(segPtr->tagInfoPtr, tagPtr->index)) {
			if (prevPtr) {
			    TkTextIndexSetByteIndex(&searchPtr->curIndex, offset);
			    searchPtr->tagon = tagon;
			    return true;
			}
			firstPtr = segPtr;
		    } else if (firstPtr) {
			TkTextIndexSetByteIndex(&searchPtr->curIndex, offset);
			searchPtr->segPtr = firstPtr;
			searchPtr->tagon = tagon;
			return true;
		    } else if (!tagon) {
			prevPtr = segPtr;
		    }
		    offset = byteOffset;
		}
		if (segPtr == searchPtr->lastPtr) {
		    if (firstPtr
			    && firstPtr == GetFirstTagInfoSegment(searchPtr->textPtr, linePtr)
			    && !LineTestIfToggleIsOpen(linePtr->prevPtr, tagPtr->index)) {
			TkTextIndexSetByteIndex(&searchPtr->curIndex, offset);
			searchPtr->segPtr = firstPtr;
			searchPtr->tagon = tagon;
			return true;
		    }
		    searchPtr->linesLeft = 0;
		    return false;
		}
		if (!(segPtr = segPtr->prevPtr)) {
		    break;
		}
		byteOffset -= segPtr->size;
	    }
	    if (firstPtr && !LineTestIfToggleIsOpen(linePtr->prevPtr, tagPtr->index)) {
		TkTextIndexSetByteIndex(&searchPtr->curIndex, offset);
		searchPtr->segPtr = firstPtr;
		searchPtr->tagon = tagon;
		return true;
	    }
	} else {
	    linePtr = TkTextIndexGetLine(&searchPtr->curIndex);
	}

	/*
	 * See if there are more lines associated with the current parent
	 * node. If so, go back to the top of the loop to search the previous one.
	 */

	nodePtr = linePtr->parentPtr;
	lastPtr = nodePtr->linePtr->prevPtr;

	do {
	    if (--searchPtr->linesLeft == 0) {
		return false;
	    }
	    linePtr = linePtr->prevPtr;
	} while (linePtr != lastPtr && !LineTestToggleBack(linePtr, tagPtr->index, tagon));

	if (linePtr != lastPtr) {
	    TkTextIndexSetSegment(&searchPtr->curIndex, segPtr = linePtr->lastPtr);
	    continue; /* go back to outer loop */
	}

	if (!(nodePtr = PrevTagFindPrevNode(nodePtr, searchPtr, tagon))) {
	    searchPtr->linesLeft = 0;
	    return false;
	}

	/*
	 * At this point we've found a subtree that has a relevant tag
	 * transition. Now search down (and across) through that subtree to
	 * find the first level-0 node that has a relevant tag transition.
	 */

	while (nodePtr->level > 0) {
	    const Node *nodeStack[MAX_CHILDREN];
	    int idx = 0;

	    for (nodePtr = nodePtr->childPtr; nodePtr; nodePtr = nodePtr->nextPtr) {
		nodeStack[idx++] = nodePtr;
	    }
	    assert(idx > 0);
	    nodePtr = nodeStack[--idx];
	    while (!NodeTestToggleBack(nodePtr, tagPtr->index, tagon)) {
		if ((searchPtr->linesLeft -= nodePtr->numLines) <= 0) {
		    return false;
		}
		assert(idx > 0);
		nodePtr = nodeStack[--idx];
	    }
	}

	/*
	 * We're down to a level-0 node that contains a line that has a relevant tag transition.
	 */

	linePtr = nodePtr->lastPtr;
	DEBUG(lastPtr = nodePtr->linePtr->prevPtr);

	/*
	 * Now search through the lines.
	 */

	while (!LineTestToggleBack(linePtr, tagPtr->index, tagon)) {
	    if (--searchPtr->linesLeft == 0) {
		return false;
	    }
	    linePtr = linePtr->prevPtr;
	    assert(linePtr != lastPtr);
	}

	TkTextIndexSetSegment(&searchPtr->curIndex, segPtr = linePtr->lastPtr);
    }

    return false; /* never reached */
}

bool
TkBTreePrevTag(
    TkTextSearch *searchPtr)	/* Information about search in progress; must
				 * have been set up by call to TkBTreeStartSearch. */
{
    if (searchPtr->resultPtr) {
	searchPtr->segPtr = searchPtr->resultPtr;
	searchPtr->resultPtr = NULL;
	return true;
    }

    if (searchPtr->linesLeft <= 0) {
	searchPtr->segPtr = NULL;
	return false;
    }

    if (PrevTag(searchPtr)) {
	return true;
    }

    if (searchPtr->endOfText && !searchPtr->tagon) {
	/* we must find start of text in this case */
	TkTextIndexSetupToStartOfText(&searchPtr->curIndex,
		searchPtr->curIndex.textPtr, searchPtr->curIndex.tree);
	searchPtr->segPtr = TkTextIndexGetContentSegment(&searchPtr->curIndex, NULL);
	searchPtr->tagon = true;
	return true;
    }

    return false;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeFindNextTagged --
 *
 *	Find next segment which contains any tag inside given range.
 *
 * Results:
 *	The return value is the next segment containing any tag.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkTextSegment *
FindNextTaggedSegInLine(
    TkTextSegment *segPtr,
    const TkTextSegment *lastPtr,
    const TkBitField *discardTags)
{
    if (lastPtr->sectionPtr->linePtr != segPtr->sectionPtr->linePtr) {
	lastPtr = NULL;
    }

    for ( ; segPtr != lastPtr; segPtr = segPtr->nextPtr) {
	const TkTextTagSet *tagInfoPtr = segPtr->tagInfoPtr;

	if (tagInfoPtr) {
	    if (TagSetTestBits(tagInfoPtr, discardTags)) {
		return segPtr;
	    }
	}
    }

    return NULL;
}

TkTextSegment *
FindNextTaggedSegInNode(
    const TkTextSegment *lastPtr,
    const TkTextLine *linePtr,
    const TkBitField *discardTags)	/* can be NULL */
{
    const TkTextLine *lastLinePtr = lastPtr->sectionPtr->linePtr;
    const TkTextLine *endLinePtr = linePtr->parentPtr->lastPtr;

    while (linePtr) {
	if (TagSetTestBits(linePtr->tagonPtr, discardTags)) {
	    return FindNextTaggedSegInLine(linePtr->segPtr, lastPtr, discardTags);
	}
	if (linePtr == lastLinePtr || linePtr == endLinePtr) {
	    return NULL;
	}
	linePtr = linePtr->nextPtr;
    }

    return NULL;
}

static const Node *
FindNextTaggedNode(
    const Node *nodePtr,
    const TkBitField *discardTags)	/* can be NULL */
{
    while (nodePtr) {
	const Node *startNodePtr = nodePtr;

	for (nodePtr = nodePtr->nextPtr; nodePtr; nodePtr = nodePtr->nextPtr) {
	    if (TagSetTestBits(nodePtr->tagonPtr, discardTags)) {
		while (nodePtr->level > 0) {
		    for (nodePtr = nodePtr->childPtr; nodePtr; nodePtr = nodePtr->nextPtr) {
			if (TagSetTestBits(nodePtr->tagonPtr, discardTags)) {
			    return nodePtr;
			}
		    }
		}
		return nodePtr;
	    }
	}

	nodePtr = startNodePtr->parentPtr;
    }

    return NULL;
}

TkTextSegment *
TkBTreeFindNextTagged(
    const TkTextIndex *indexPtr1,
    				/* Search starts here. Tag toggles at this position will be returned. */
    const TkTextIndex *indexPtr2,
    				/* Search stops here. Tag toggles at this position will not be
				 * returned. */
    const struct TkBitField *discardTags)
				/* Discard these tags when searching, can be NULL. */
{
    const TkSharedText *sharedTextPtr = TkTextIndexGetShared(indexPtr1);
    const TkTextLine *linePtr = TkTextIndexGetLine(indexPtr1);
    const TkTextSegment *lastPtr = TkTextIndexGetFirstSegment(indexPtr2, NULL);
    const TkText *textPtr;
    const Node *nodePtr;

    /*
     * At first, search for next segment in first line.
     */

    if (TagSetTestBits(linePtr->tagonPtr, discardTags)) {
	TkTextSegment *segPtr = TkTextIndexGetContentSegment(indexPtr1, NULL);

	if ((segPtr = FindNextTaggedSegInLine(segPtr, lastPtr, discardTags))) {
	    return segPtr;
	}
    }

    /*
     * At second, search for line containing any tag in current node.
     */

    textPtr = indexPtr1->textPtr;
    nodePtr = linePtr->parentPtr;

    if (linePtr != nodePtr->lastPtr && TagSetTestBits(nodePtr->tagonPtr, discardTags)) {
	TkTextSegment *segPtr = FindNextTaggedSegInNode(lastPtr, linePtr->nextPtr, discardTags);

	if (segPtr) {
	    return segPtr;
	}
    }

    /*
     * We couldn't find a line, so search inside B-Tree for next level-0
     * node which contains any tag.
     */

    if (!(nodePtr = FindNextTaggedNode(nodePtr, discardTags))) {
	return NULL;
    }

    if (textPtr && textPtr->startMarker != textPtr->sharedTextPtr->startMarker) {
	int lineNo1 = TkBTreeLinesTo(sharedTextPtr->tree, NULL, nodePtr->linePtr, NULL);
	int lineNo2 = TkTextIndexGetLineNumber(indexPtr2, NULL);

	if (lineNo1 > lineNo2) {
	    /* We've found a node after text end, so return NULL. */
	    return NULL;
	}
    }

    /*
     * Final search of segment containing any tag.
     */

    return FindNextTaggedSegInNode(lastPtr, nodePtr->linePtr, discardTags);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeFindNextUntagged --
 *
 *	Find next segment which does not contain any tag.
 *
 * Results:
 *	The return value is the next segment not containing any tag.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkTextSegment *
FindNextUntaggedSegInLine(
    TkTextSegment *segPtr,
    const TkTextSegment *lastPtr,
    const TkBitField *discardTags)
{
    if (lastPtr->sectionPtr->linePtr != segPtr->sectionPtr->linePtr) {
	lastPtr = NULL;
    }

    for ( ; segPtr != lastPtr; segPtr = segPtr->nextPtr) {
	const TkTextTagSet *tagInfoPtr = segPtr->tagInfoPtr;

	if (tagInfoPtr) {
	    if (!TagSetTestDisjunctiveBits(tagInfoPtr, discardTags)) {
		return segPtr;
	    }
	}
    }

    return NULL;
}

TkTextSegment *
FindNextUntaggedSegInNode(
    const TkTextSegment *lastPtr,
    const TkTextLine *linePtr,
    const TkBitField *discardTags)	/* can be NULL */
{
    const TkTextLine *lastLinePtr = lastPtr->sectionPtr->linePtr;
    const TkTextLine *endLinePtr = linePtr->parentPtr->lastPtr;

    while (linePtr) {
	if (TagSetTestDontContainsAny(linePtr->tagonPtr, linePtr->tagoffPtr, discardTags)) {
	    return FindNextUntaggedSegInLine(linePtr->segPtr, lastPtr, discardTags);
	}
	if (linePtr == lastLinePtr || linePtr == endLinePtr) {
	    return NULL;
	}
	linePtr = linePtr->nextPtr;
    }

    return NULL;
}

static const Node *
FindNextUntaggedNode(
    const Node *nodePtr,
    const TkBitField *discardTags)	/* can be NULL */
{
    while (nodePtr) {
	const Node *startNodePtr = nodePtr;

	for (nodePtr = nodePtr->nextPtr; nodePtr; nodePtr = nodePtr->nextPtr) {
	    if (TagSetTestDontContainsAny(nodePtr->tagonPtr, nodePtr->tagoffPtr, discardTags)) {
		while (nodePtr->level > 0) {
		    for (nodePtr = nodePtr->childPtr; nodePtr; nodePtr = nodePtr->nextPtr) {
			if (TagSetTestDontContainsAny(nodePtr->tagonPtr, nodePtr->tagoffPtr,
				discardTags)) {
			    return nodePtr;
			}
		    }
		}
		return nodePtr;
	    }
	}

	nodePtr = startNodePtr->parentPtr;
    }

    return NULL;
}

TkTextSegment *
TkBTreeFindNextUntagged(
    const TkTextIndex *indexPtr1,
    				/* Search starts here. Tag toggles at this position will be
				 * returned. */
    const TkTextIndex *indexPtr2,
    				/* Search stops here. Tag toggles at this position will not be
				 * returned. */
    const struct TkBitField *discardTags)
				/* Discard these tags when searching, can be NULL. */
{
    const TkSharedText *sharedTextPtr = TkTextIndexGetShared(indexPtr1);
    const TkTextLine *linePtr = TkTextIndexGetLine(indexPtr1);
    const TkTextSegment *lastPtr = TkTextIndexGetFirstSegment(indexPtr2, NULL);
    const TkText *textPtr;
    const Node *nodePtr;

    /*
     * At first, search for next segment in first line.
     */

    if (TagSetTestDontContainsAny(linePtr->tagonPtr, linePtr->tagoffPtr, discardTags)) {
	TkTextSegment *segPtr = TkTextIndexGetContentSegment(indexPtr1, NULL);

	if ((segPtr = FindNextUntaggedSegInLine(segPtr, lastPtr, discardTags))) {
	    return segPtr;
	}
    }

    /*
     * At second, search for line containing any tag in current node.
     */

    textPtr = indexPtr1->textPtr;
    nodePtr = linePtr->parentPtr;

    if (linePtr != nodePtr->lastPtr
	    && (TagSetTestDontContainsAny(nodePtr->tagonPtr, nodePtr->tagoffPtr, discardTags))) {
	TkTextSegment *segPtr = FindNextUntaggedSegInNode(lastPtr, linePtr->nextPtr, discardTags);

	if (segPtr) {
	    return segPtr;
	}
    }

    /*
     * We couldn't find a line, so search inside B-Tree for next level-0
     * node which don't contains any tag.
     */

    if (!(nodePtr = FindNextUntaggedNode(nodePtr, discardTags))) {
	return NULL;
    }

    if (textPtr && textPtr->startMarker != textPtr->sharedTextPtr->startMarker) {
	int lineNo1 = TkBTreeLinesTo(sharedTextPtr->tree, NULL, nodePtr->linePtr, NULL);
	int lineNo2 = TkTextIndexGetLineNumber(indexPtr2, NULL);

	if (lineNo1 > lineNo2) {
	    /* We've found a node after text end, so return NULL. */
	    return NULL;
	}
    }

    /*
     * Final search of segment not containing any tag.
     */

    return FindNextUntaggedSegInNode(lastPtr, nodePtr->linePtr, discardTags);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeFindPrevTagged --
 *
 *	Starting at given index, find previous segment which contains any tag.
 *
 * Results:
 *	The return value is the previous segment containing any tag.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkTextSegment *
FindPrevTaggedSegInLine(
    TkTextSegment *segPtr,
    const TkTextSegment *firstPtr,
    const TkBitField *selTags)
{
    const TkTextLine *linePtr = segPtr->sectionPtr->linePtr;

    firstPtr = (linePtr == firstPtr->sectionPtr->linePtr) ? firstPtr->prevPtr : NULL;

    for ( ; segPtr != firstPtr; segPtr = segPtr->prevPtr) {
	const TkTextTagSet *tagInfoPtr = segPtr->tagInfoPtr;

	if (tagInfoPtr) {
	    if (TagSetTestBits(tagInfoPtr, selTags)) {
		return segPtr;
	    }
	}
    }

    return NULL;
}

TkTextSegment *
FindPrevTaggedSegInNode(
    TkTextSegment *firstPtr,
    const TkTextLine *linePtr,
    const TkBitField *selTags)		/* can be NULL */
{
    const TkTextLine *firstLinePtr = firstPtr->sectionPtr->linePtr;
    const TkTextLine *startLinePtr = linePtr->parentPtr->linePtr;

    while (true) {
	if (TagSetTestBits(linePtr->tagonPtr, selTags)) {
	    return FindPrevTaggedSegInLine(linePtr->lastPtr, firstPtr, selTags);
	}
	if (linePtr == startLinePtr || linePtr == firstLinePtr) {
	    return NULL;
	}
	linePtr = linePtr->prevPtr;
    };

    return NULL;
}

static const Node *
FindPrevTaggedNode(
    const Node *nodePtr,
    const TkBitField *selTags)	/* can be NULL */
{
    assert(nodePtr);

    while (nodePtr->parentPtr) {
	const Node *startNodePtr = nodePtr;
	const Node *lastNodePtr = NULL;

	nodePtr = nodePtr->parentPtr->childPtr;

	for ( ; nodePtr != startNodePtr; nodePtr = nodePtr->nextPtr) {
	    if (TagSetTestBits(nodePtr->tagonPtr, selTags)) {
		lastNodePtr = nodePtr;
	    }
	}
	if (lastNodePtr) {
	    nodePtr = lastNodePtr;

	    while (nodePtr->level > 0) {
		DEBUG(lastNodePtr = NULL);

		for (nodePtr = nodePtr->childPtr; nodePtr; nodePtr = nodePtr->nextPtr) {
		    if (TagSetTestBits(nodePtr->tagonPtr, selTags)) {
			lastNodePtr = nodePtr;
		    }
		}

		assert(lastNodePtr);
		nodePtr = lastNodePtr;
	    }

	    return lastNodePtr;
	}

	nodePtr = startNodePtr->parentPtr;
    }

    return NULL;
}

TkTextSegment *
TkBTreeFindPrevTagged(
    const TkTextIndex *indexPtr1,
    				/* Search starts here. Tag toggles at this position will be returned. */
    const TkTextIndex *indexPtr2,
    				/* Search stops here. Tag toggles at this position will be returned. */
    bool discardSelection)	/* Discard selection tags? */
{
    const TkSharedText *sharedTextPtr = TkTextIndexGetShared(indexPtr1);
    const TkBitField *selTags = discardSelection ? sharedTextPtr->selectionTags : NULL;
    const TkTextLine *linePtr = TkTextIndexGetLine(indexPtr1);
    TkTextSegment *firstPtr = TkTextIndexGetFirstSegment(indexPtr2, NULL);
    const TkText *textPtr;
    const Node *nodePtr;

    /*
     * At first, search for previous segment in first line.
     */

    if (TagSetTestBits(linePtr->tagonPtr, selTags)) {
	TkTextSegment *segPtr = TkTextIndexGetContentSegment(indexPtr1, NULL);

	if ((segPtr = FindPrevTaggedSegInLine(segPtr, firstPtr, selTags))) {
	    return segPtr;
	}
    }

    /*
     * At second, search for line containing any tag in current node.
     */

    textPtr = indexPtr1->textPtr;
    nodePtr = linePtr->parentPtr;

    if (linePtr != nodePtr->linePtr && TagSetTestBits(nodePtr->tagonPtr, selTags)) {
	TkTextSegment *segPtr = FindPrevTaggedSegInNode(firstPtr, linePtr->prevPtr, selTags);

	if (segPtr) {
	    return segPtr;
	}
    }

    /*
     * We couldn't find a line, so search inside B-Tree for previous level-0
     * node which contains any tag.
     */

    if (!(nodePtr = FindPrevTaggedNode(nodePtr, selTags))) {
	return NULL;
    }

    if (textPtr && textPtr->startMarker != textPtr->sharedTextPtr->startMarker) {
	int lineNo1 = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, NULL, nodePtr->lastPtr, NULL);
	int lineNo2 = TkTextIndexGetLineNumber(indexPtr2, NULL);

	if (lineNo1 < lineNo2) {
	    /* We've found a node before text start, so return NULL. */
	    return NULL;
	}
    }

    /*
     * Final search of segment containing any tag.
     */

    return FindPrevTaggedSegInNode(firstPtr, nodePtr->lastPtr, selTags);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeCharTagged --
 *
 *	Determine whether a particular character has a particular tag.
 *
 * Results:
 *	The return value is 1 if the given tag is in effect at the character
 *	given by linePtr and ch, and 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkBTreeCharTagged(
    const TkTextIndex *indexPtr,/* Indicates a character position at which to check for a tag. */
    const TkTextTag *tagPtr)	/* Tag of interest, can be NULL. */
{
    const TkTextTagSet *tagInfoPtr = TkTextIndexGetContentSegment(indexPtr, NULL)->tagInfoPtr;
    return tagPtr ? TkTextTagSetTest(tagInfoPtr, tagPtr->index) : !TkTextTagSetIsEmpty(tagInfoPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeGetSegmentTags --
 *
 *	Return information about all of the tags that are associated with a
 *	particular char segment in a B-tree of text.
 *
 * Results:
 *      The return value is the root of the tag chain, containing all tags
 *	associated with the given char segment. If there are no tags in this
 *	segment, then a NULL pointer is returned.
 *
 * Side effects:
 *	The attribute nextPtr of TkTextTag will be modified for any tag.
 *
 *----------------------------------------------------------------------
 */

TkTextTag *
TkBTreeGetSegmentTags(
    const TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    const TkTextSegment *segPtr,	/* Get tags from this segment. */
    const TkText *textPtr,		/* If non-NULL, then only return tags for this text widget
    					 * (when there are peer widgets). */
    bool *containsSelection)		/* If non-NULL, return whether this chain contains the
    					 * "sel" tag. */
{
    const TkTextTagSet *tagInfoPtr;
    TkTextTag *chainPtr = NULL;

    assert(segPtr->tagInfoPtr);

    tagInfoPtr = segPtr->tagInfoPtr;

    if (containsSelection) {
	*containsSelection = false;
    }

    if (tagInfoPtr != sharedTextPtr->emptyTagInfoPtr) {
	unsigned i = TkTextTagSetFindFirst(tagInfoPtr);

	for ( ; i != TK_TEXT_TAG_SET_NPOS; i = TkTextTagSetFindNext(tagInfoPtr, i)) {
	    TkTextTag *tagPtr = sharedTextPtr->tagLookup[i];

	    assert(tagPtr);
	    assert(!tagPtr->isDisabled);

	    if (!textPtr || !tagPtr->textPtr) {
		tagPtr->nextPtr = chainPtr;
		tagPtr->epoch = 0;
		chainPtr = tagPtr;
	    } else if (tagPtr->textPtr == textPtr) {
		tagPtr->nextPtr = chainPtr;
		tagPtr->epoch = 0;
		chainPtr = tagPtr;

		if (tagPtr == textPtr->selTagPtr && containsSelection) {
		    *containsSelection = true;
		}
	    }
	}
    }

    return chainPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeGetLang --
 *
 *	Return the language information of given segment.
 *
 * Results:
 *      The return value is the language string belonging to given segment.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
TkBTreeGetLang(
    const TkText *textPtr,		/* Relative to this client of the B-tree. */
    const TkTextSegment *segPtr)	/* Get tags from this segment. */
{
    const TkTextTagSet *tagInfoPtr;
    const TkSharedText *sharedTextPtr;
    const char *langPtr;

    assert(textPtr);
    assert(segPtr->tagInfoPtr);
    assert(segPtr->sectionPtr->linePtr->nextPtr);

    sharedTextPtr = textPtr->sharedTextPtr;
    tagInfoPtr = segPtr->tagInfoPtr;
    langPtr = textPtr->lang;

    if (tagInfoPtr != sharedTextPtr->emptyTagInfoPtr) {
	unsigned i = TkTextTagSetFindFirst(tagInfoPtr);
	int highestPriority = -1;

	for ( ; i != TK_TEXT_TAG_SET_NPOS; i = TkTextTagSetFindNext(tagInfoPtr, i)) {
	    const TkTextTag *tagPtr = sharedTextPtr->tagLookup[i];

	    assert(tagPtr);
	    assert(!tagPtr->isDisabled);

	    if (tagPtr->lang[0] && (int) tagPtr->priority > highestPriority) {
		langPtr = tagPtr->lang;
		highestPriority = tagPtr->priority;
	    }
	}
    }

    return langPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeCheck --
 *
 *	This function runs a set of consistency checks over a B-tree and
 *	panics if any inconsistencies are found.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If a structural defect is found, the function panics with an error
 *	message.
 *
 *----------------------------------------------------------------------
 */

void
TkBTreeCheck(
    TkTextBTree tree)		/* Tree to check. */
{
    BTree *treePtr = (BTree *) tree;
    const Node *nodePtr;
    const TkTextLine *linePtr, *prevLinePtr;
    const TkTextSegment *segPtr;
    const TkText *peer;
    unsigned numBranches = 0;
    unsigned numLinks = 0;
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch search;
    const char *s;

    if (treePtr->sharedTextPtr->refCount == 0) {
	Tcl_Panic("TkBTreeCheck: tree is destroyed");
    }

    nodePtr = treePtr->rootPtr;
    while (nodePtr->level > 0) {
	nodePtr = nodePtr->childPtr;
	if (!nodePtr) {
	    Tcl_Panic("TkBTreeCheck: no level 0 node in tree");
	}
    }

    /*
     * Check line pointers.
     */

    prevLinePtr = NULL;
    for (linePtr = nodePtr->linePtr;
	    linePtr;
	    prevLinePtr = linePtr, linePtr = linePtr->nextPtr) {
	if (!linePtr->segPtr) {
	    Tcl_Panic("TkBTreeCheck: line has no segments");
	}
	if (linePtr->size == 0) {
	    Tcl_Panic("TkBTreeCheck: line has size zero");
	}
	if (!linePtr->lastPtr) {
	    Tcl_Panic("TkBTreeCheck: line has no last pointer");
	}
	if (linePtr->prevPtr != prevLinePtr) {
	    Tcl_Panic("TkBTreeCheck: line has wrong predecessor");
	}
	if (!linePtr->tagoffPtr || !linePtr->tagonPtr) {
	    Tcl_Panic("TkBTreeCheck: line tag information is incomplete");
	}
	if (TkTextTagSetRefCount(linePtr->tagonPtr) == 0) {
	    Tcl_Panic("TkBTreeCheck: unreferenced tag info (tagon)");
	}
	if (TkTextTagSetRefCount(linePtr->tagonPtr) > 0x3fffffff) {
	    Tcl_Panic("TkBTreeCheck: negative reference count in tagon info");
	}
	if (TkTextTagSetRefCount(linePtr->tagoffPtr) == 0) {
	    Tcl_Panic("TkBTreeCheck: unreferenced tag info (tagoff)");
	}
	if (TkTextTagSetRefCount(linePtr->tagoffPtr) > 0x3fffffff) {
	    Tcl_Panic("TkBTreeCheck: negative reference count in tagoff info");
	}
	if (!TkTextTagSetContains(linePtr->tagonPtr, linePtr->tagoffPtr)) {
	    Tcl_Panic("TkBTreeCheck: line tagoff not included in tagon");
	}
	if (TkTextTagSetIsEmpty(linePtr->tagonPtr)
		&& linePtr->tagonPtr != treePtr->sharedTextPtr->emptyTagInfoPtr) {
	    Tcl_Panic("TkBTreeCheck: should use shared resource if tag info is empty");
	}
	if (TkTextTagSetIsEmpty(linePtr->tagoffPtr)
		&& linePtr->tagoffPtr != treePtr->sharedTextPtr->emptyTagInfoPtr) {
	    Tcl_Panic("TkBTreeCheck: should use shared resource if tag info is empty");
	}
	if (TkTextTagSetRefCount(linePtr->tagonPtr) == 0) {
	    Tcl_Panic("TkBTreeCheck: reference count of line tagon is zero");
	}
	if (TkTextTagSetRefCount(linePtr->tagoffPtr) == 0) {
	    Tcl_Panic("TkBTreeCheck: reference count of line tagoff is zero");
	}
	if (linePtr->logicalLine ==
		(linePtr->prevPtr && HasElidedNewline(treePtr->sharedTextPtr, linePtr->prevPtr))) {
	    Tcl_Panic("TkBTreeCheck: wrong logicalLine flag");
	}
	numBranches += linePtr->numBranches;
	numLinks += linePtr->numLinks;
    }

    if (numBranches != treePtr->rootPtr->numBranches) {
	Tcl_Panic("TkBTreeCheck: wrong branch count %u (expected is %u)",
		numBranches, treePtr->rootPtr->numBranches);
    }
    if (numLinks != numBranches) {
	Tcl_Panic("TkBTreeCheck: mismatch in number of links (%d) and branches (%d)",
		numLinks, numBranches);
    }

    /*
     * Check the special markers.
     */

    if (!treePtr->sharedTextPtr->startMarker->sectionPtr) {
	Tcl_Panic("TkBTreeCheck: start marker of shared resource is not linked");
    }
    if (!treePtr->sharedTextPtr->endMarker->sectionPtr) {
	Tcl_Panic("TkBTreeCheck: end marker of shared resource is not linked");
    }
    if (treePtr->sharedTextPtr->startMarker->sectionPtr->linePtr->prevPtr) {
	Tcl_Panic("TkBTreeCheck: start marker of shared resource is not in first line");
    }
    if (treePtr->sharedTextPtr->endMarker->sectionPtr->linePtr->nextPtr) {
	Tcl_Panic("TkBTreeCheck: end marker of shared resource is not in last line");
    }
    if (!SegIsAtStartOfLine(treePtr->sharedTextPtr->startMarker)) {
	Tcl_Panic("TkBTreeCheck: start marker of shared resource is not at start of line");
    }
    if (!SegIsAtStartOfLine(treePtr->sharedTextPtr->endMarker)) {
	Tcl_Panic("TkBTreeCheck: end marker of shared resource is not at start of line");
    }

    for (peer = treePtr->sharedTextPtr->peers; peer; peer = peer->next) {
	if (peer->currentMarkPtr && peer->currentMarkPtr->sectionPtr) {
	    if ((peer->currentMarkPtr->prevPtr && !peer->currentMarkPtr->prevPtr->typePtr)
		|| (peer->currentMarkPtr->nextPtr && !peer->currentMarkPtr->nextPtr->typePtr)
		|| (peer->currentMarkPtr->sectionPtr
		    && (!peer->currentMarkPtr->sectionPtr->linePtr
			|| !peer->currentMarkPtr->sectionPtr->linePtr->parentPtr))) {
		Tcl_Panic("TkBTreeCheck: current mark is expired");
	    }
	}
	if (peer->insertMarkPtr && peer->insertMarkPtr->sectionPtr) {
	    if ((peer->insertMarkPtr->prevPtr && !peer->insertMarkPtr->prevPtr->typePtr)
		|| (peer->insertMarkPtr->nextPtr && !peer->insertMarkPtr->nextPtr->typePtr)
		|| (peer->insertMarkPtr->sectionPtr
		    && (!peer->insertMarkPtr->sectionPtr->linePtr
			|| !peer->insertMarkPtr->sectionPtr->linePtr->parentPtr))) {
		Tcl_Panic("TkBTreeCheck: insert mark is expired");
	    }
	}
#if 0 /* cannot be used, because also TkBTreeUnlinkSegment is calling TreeCheck */
	if (peer->startMarker != treePtr->sharedTextPtr->startMarker) {
	    if (!peer->startMarker->sectionPtr) {
		Tcl_Panic("TkBTreeCheck: start marker is not linked");
	    }
	    if (!peer->endMarker->sectionPtr) {
		Tcl_Panic("TkBTreeCheck: end marker is not linked");
	    }
	}
#endif
	if (!peer->startMarker->sectionPtr) {
	    Tcl_Panic("TkBTreeCheck: start marker of is not linked");
	}
	if (!peer->endMarker->sectionPtr) {
	    Tcl_Panic("TkBTreeCheck: end marker of is not linked");
	}
	if (!peer->startMarker->sectionPtr->linePtr->nextPtr) {
	    Tcl_Panic("TkBTreeCheck: start marker is on very last line");
	}
	if (peer->startMarker->sectionPtr->linePtr == peer->endMarker->sectionPtr->linePtr) {
	    const TkTextSegment *segPtr = peer->startMarker;
	    while (segPtr && segPtr != peer->endMarker) {
		segPtr = segPtr->prevPtr;
	    }
	    if (segPtr == peer->endMarker) {
		Tcl_Panic("TkBTreeCheck: end marker segment is before start marker segment");
	    }
	} else {
	    int startLineNo = TkBTreeLinesTo(tree, NULL, peer->startMarker->sectionPtr->linePtr, NULL);
	    int endLineNo = TkBTreeLinesTo(tree, NULL, peer->endMarker->sectionPtr->linePtr, NULL);

	    if (startLineNo > endLineNo) {
		Tcl_Panic("TkBTreeCheck: end marker line is before start marker line");
	    }
	}
    }

    /*
     * Call a recursive function to do the main body of checks.
     */

    CheckNodeConsistency(treePtr->sharedTextPtr, treePtr->rootPtr,
	    treePtr->rootPtr, treePtr->numPixelReferences);

    /*
     * Make sure that there are at least two lines in the text and that the
     * last line has no characters except a newline.
     */

    nodePtr = treePtr->rootPtr;
    if (nodePtr->numLines < 2) {
	Tcl_Panic("TkBTreeCheck: less than 2 lines in tree");
    }
    if (!nodePtr->linePtr->logicalLine) {
	Tcl_Panic("TkBTreeCheck: first line must be a logical line");
    }
#if 0 /* TODO: is it really allowed that the last line is not a logical line? */
    if (!nodePtr->lastPtr->logicalLine) {
	Tcl_Panic("TkBTreeCheck: last line must be a logical line");
    }
#endif
    while (nodePtr->level > 0) {
	nodePtr = nodePtr->childPtr;
	while (nodePtr->nextPtr) {
	    nodePtr = nodePtr->nextPtr;
	}
    }
    linePtr = nodePtr->lastPtr;
    segPtr = linePtr->segPtr;
    if (segPtr->typePtr == &tkTextLinkType) {
	/* It's OK to have one link in the last line. */
	segPtr = segPtr->nextPtr;
    }
    while (segPtr->typePtr->group == SEG_GROUP_MARK) {
	/* It's OK to have marks or breaks in the last line. */
	segPtr = segPtr->nextPtr;
    }
    if (segPtr->typePtr != &tkTextCharType) {
	Tcl_Panic("TkBTreeCheck: last line has bogus segment type");
    }
    if (segPtr->nextPtr) {
	Tcl_Panic("TkBTreeCheck: last line has too many segments");
    }
    if (segPtr->size != 1) {
	Tcl_Panic("TkBTreeCheck: last line has wrong # characters: %d", segPtr->size);
    }

    s = segPtr->body.chars; /* this avoids warnings */
    if (s[0] != '\n' || s[1] != '\0') {
	Tcl_Panic("TkBTreeCheck: last line had bad value: %s", segPtr->body.chars);
    }

    for (entryPtr = Tcl_FirstHashEntry(&treePtr->sharedTextPtr->tagTable, &search);
	    entryPtr;
	    entryPtr = Tcl_NextHashEntry(&search)) {
	const TkTextTag *tagPtr = Tcl_GetHashValue(entryPtr);

	assert(tagPtr->index < treePtr->sharedTextPtr->tagInfoSize);

	if (TkBitTest(treePtr->sharedTextPtr->selectionTags, tagPtr->index) && tagPtr->elideString) {
	    Tcl_Panic("TkBTreeCheck: the selection tag '%s' is not allowed to elide (or un-elide)",
		    tagPtr->name);
	}

	if ((nodePtr = tagPtr->rootPtr)) {
	    assert(nodePtr->linePtr); /* still unfree'd? */

	    if (!TkTextTagSetTest(nodePtr->tagonPtr, tagPtr->index)) {
		if (nodePtr->level == 0) {
		    Tcl_Panic("TkBTreeCheck: level zero node is not root for tag '%s'",
			    tagPtr->name);
		} else {
		    Tcl_Panic("TkBTreeCheck: node is not root for tag '%s'", tagPtr->name);
		}
	    }

	    if (nodePtr->level > 0 && CountChildsWithTag(nodePtr, tagPtr->index) < 2) {
		Tcl_Panic("TkBTreeCheck: node is not root for tag '%s', it has less "
			"than two childs containing this tag", tagPtr->name);
	    }

	    while ((nodePtr = nodePtr->parentPtr)) {
		if (CountChildsWithTag(nodePtr, tagPtr->index) > 1) {
		    Tcl_Panic("TkBTreeCheck: found higher node as root for tag '%s'", tagPtr->name);
		}
	    }
	} else if (TkTextTagSetTest(treePtr->rootPtr->tagonPtr, tagPtr->index)) {
	    Tcl_Panic("TkBTreeCheck: tag '%s' is used, but has no root", tagPtr->name);
	}
    }

    if (tkTextDebug) {
	for (peer = treePtr->sharedTextPtr->peers; peer; peer = peer->next) {
	    /*
	     * Check display stuff.
	     */
	    TkTextCheckDisplayLineConsistency(peer);
	    TkTextCheckLineMetricUpdate(peer);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CheckNodeConsistency --
 *
 *	This function is called as part of consistency checking for B-trees:
 *	it checks several aspects of a node and also runs checks recursively
 *	on the node's children.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If anything suspicious is found in the tree structure, the function
 *	panics.
 *
 *----------------------------------------------------------------------
 */

static void
CheckNodeConsistency(
    const TkSharedText *sharedTextPtr,/* Handle to shared text resource. */
    const Node *rootPtr,	/* The root node. */
    const Node *nodePtr,	/* Node whose subtree should be checked. */
    unsigned references)	/* Number of referring widgets which have pixel counts. */
{
    const Node *childNodePtr;
    const TkTextLine *linePtr;
    const TkTextLine *prevLinePtr;
    unsigned numLines, numLogicalLines, numBranches, numChildren, minChildren, size, i;
    NodePixelInfo *pixelInfo = NULL;
    NodePixelInfo pixelInfoBuf[PIXEL_CLIENTS];
    TkTextTagSet *tagonPtr = NULL;
    TkTextTagSet *tagoffPtr = NULL;
    TkTextTagSet *additionalTagoffPtr = NULL;
    unsigned memsize;

    if (nodePtr->level == 0 && !nodePtr->linePtr) {
	Tcl_Panic("CheckNodeConsistency: this node is freed");
    }

    minChildren = nodePtr->parentPtr ? MIN_CHILDREN : (nodePtr->level > 0 ? 2 : 1);
    if (nodePtr->numChildren < minChildren || nodePtr->numChildren > MAX_CHILDREN) {
	Tcl_Panic("CheckNodeConsistency: bad child count (%d)", nodePtr->numChildren);
    }

    if (!nodePtr->linePtr) {
	Tcl_Panic("CheckNodeConsistency: first pointer is NULL");
    }
    if (!nodePtr->lastPtr) {
	Tcl_Panic("CheckNodeConsistency: last pointer is NULL");
    }
    if (!nodePtr->tagonPtr || !nodePtr->tagoffPtr) {
	Tcl_Panic("CheckNodeConsistency: tag information is NULL");
    }
    if (TkTextTagSetRefCount(nodePtr->tagonPtr) == 0) {
	Tcl_Panic("CheckNodeConsistency: unreferenced tag info (tagon)");
    }
    if (TkTextTagSetRefCount(nodePtr->tagonPtr) > 0x3fffffff) {
	Tcl_Panic("CheckNodeConsistency: negative reference count in tagon info");
    }
    if (TkTextTagSetRefCount(nodePtr->tagoffPtr) == 0) {
	Tcl_Panic("CheckNodeConsistency: unreferenced tag info (tagoff)");
    }
    if (TkTextTagSetRefCount(nodePtr->tagoffPtr) > 0x3fffffff) {
	Tcl_Panic("CheckNodeConsistency: negative reference count in tagoff info");
    }
    if (TkTextTagSetIsEmpty(nodePtr->tagonPtr)
	    && nodePtr->tagonPtr != sharedTextPtr->emptyTagInfoPtr) {
	Tcl_Panic("CheckNodeConsistency: should use shared resource if tag info is empty");
    }
    if (TkTextTagSetIsEmpty(nodePtr->tagoffPtr)
	    && nodePtr->tagoffPtr != sharedTextPtr->emptyTagInfoPtr) {
	Tcl_Panic("CheckNodeConsistency: should use shared resource if tag info is empty");
    }
    if (!TkTextTagSetContains(nodePtr->tagonPtr, nodePtr->tagoffPtr)) {
	Tcl_Panic("CheckNodeConsistency: node tagoff not included in tagon");
    }
    if (!TkTextTagSetContains(rootPtr->tagonPtr, nodePtr->tagonPtr)) {
	Tcl_Panic("CheckNodeConsistency: tagon not propagated to root");
    }
    if (!TkTextTagSetContains(rootPtr->tagoffPtr, nodePtr->tagoffPtr)) {
	Tcl_Panic("CheckNodeConsistency: tagoff not propagated to root");
    }

    numChildren = numLines = numLogicalLines = numBranches = size = 0;

    memsize = sizeof(pixelInfo[0])*references;
    pixelInfo = (references > PIXEL_CLIENTS) ? (NodePixelInfo *) malloc(memsize) : pixelInfoBuf;
    memset(pixelInfo, 0, memsize);

    TkTextTagSetIncrRefCount(tagonPtr = sharedTextPtr->emptyTagInfoPtr);
    TkTextTagSetIncrRefCount(tagoffPtr = sharedTextPtr->emptyTagInfoPtr);
    additionalTagoffPtr = NULL;

    if (nodePtr->level == 0) {
	prevLinePtr = NULL;
	linePtr = nodePtr->linePtr;
	for (linePtr = nodePtr->linePtr;
		numChildren < nodePtr->numChildren;
		++numChildren, ++numLines, linePtr = linePtr->nextPtr) {
	    if (!linePtr) {
		Tcl_Panic("CheckNodeConsistency: unexpected end of line chain");
	    }
	    if (linePtr->parentPtr != nodePtr) {
		Tcl_Panic("CheckNodeConsistency: line has wrong parent pointer");
	    }
	    CheckSegments(sharedTextPtr, linePtr);
	    CheckSegmentItems(sharedTextPtr, linePtr);
	    CheckSections(linePtr);
	    for (i = 0; i < references; ++i) {
		pixelInfo[i].pixels += linePtr->pixelInfo[i].height;
		pixelInfo[i].numDispLines += GetDisplayLines(linePtr, i);
	    }
	    if (tagonPtr) {
		tagonPtr = TkTextTagSetJoin(tagonPtr, linePtr->tagonPtr);
		tagoffPtr = TkTextTagSetJoin(tagoffPtr, linePtr->tagoffPtr);
		if (additionalTagoffPtr) {
		    additionalTagoffPtr = TkTextTagSetIntersect(additionalTagoffPtr, linePtr->tagonPtr);
		} else {
		    TkTextTagSetIncrRefCount(additionalTagoffPtr = linePtr->tagonPtr);
		}
	    }
	    prevLinePtr = linePtr;
	    numLogicalLines += linePtr->logicalLine;
	    numBranches += linePtr->numBranches;
	    size += linePtr->size;
	}
	if (prevLinePtr != nodePtr->lastPtr) {
	    Tcl_Panic("CheckNodeConsistency: wrong pointer to last line");
	}
    } else {
	TkTextLine *startLinePtr = nodePtr->linePtr;

	for (childNodePtr = nodePtr->childPtr; childNodePtr; childNodePtr = childNodePtr->nextPtr) {
	    if (childNodePtr->parentPtr != nodePtr) {
		Tcl_Panic("CheckNodeConsistency: node doesn't point to parent");
	    }
	    if (childNodePtr->level != nodePtr->level - 1) {
		Tcl_Panic("CheckNodeConsistency: level mismatch (%d %d)",
			nodePtr->level, childNodePtr->level);
	    }
	    if (childNodePtr->linePtr != startLinePtr) {
		const Node *nodePtr = childNodePtr;
		while (nodePtr->level > 0) {
		    nodePtr = nodePtr->childPtr;
		}
		if (nodePtr->linePtr != startLinePtr) {
		    Tcl_Panic("CheckNodeConsistency: pointer to first line is wrong");
		} else {
		    Tcl_Panic("CheckNodeConsistency: pointer to last line is wrong");
		}
	    }
	    startLinePtr = childNodePtr->lastPtr->nextPtr;
	    CheckNodeConsistency(sharedTextPtr, rootPtr, childNodePtr, references);
	    numChildren += 1;
	    numLines += childNodePtr->numLines;
	    numLogicalLines += childNodePtr->numLogicalLines;
	    numBranches += childNodePtr->numBranches;
	    size += childNodePtr->size;
	    if (tagonPtr) {
		tagonPtr = TkTextTagSetJoin(tagonPtr, nodePtr->tagonPtr);
		tagoffPtr = TkTextTagSetJoin(tagoffPtr, nodePtr->tagoffPtr);
		if (additionalTagoffPtr) {
		    additionalTagoffPtr = TkTextTagSetIntersect(additionalTagoffPtr, nodePtr->tagonPtr);
		} else {
		    TkTextTagSetIncrRefCount(additionalTagoffPtr = nodePtr->tagonPtr);
		}
	    }
	    for (i = 0; i < references; i++) {
		pixelInfo[i].pixels += childNodePtr->pixelInfo[i].pixels;
		pixelInfo[i].numDispLines += childNodePtr->pixelInfo[i].numDispLines;
	    }
	}
    }
    if (size != nodePtr->size) {
	Tcl_Panic("CheckNodeConsistency: sum of size (%d) at level %d is wrong (%d is expected)",
		nodePtr->size, nodePtr->level, size);
    }
    if (numChildren != nodePtr->numChildren) {
	Tcl_Panic("CheckNodeConsistency: mismatch in numChildren (expected: %d, counted: %d)",
		numChildren, nodePtr->numChildren);
    }
    if (numLines != nodePtr->numLines) {
	Tcl_Panic("CheckNodeConsistency: mismatch in numLines (expected: %d, counted: %d)",
		numLines, nodePtr->numLines);
    }
    if (numLogicalLines != nodePtr->numLogicalLines) {
	Tcl_Panic("CheckNodeConsistency: mismatch in numLogicalLines (expected: %d, counted: %d)",
		numLogicalLines, nodePtr->numLogicalLines);
    }
    if (numBranches != nodePtr->numBranches) {
	Tcl_Panic("CheckNodeConsistency: mismatch in numBranches (expected: %d, counted: %d)",
		numLogicalLines, nodePtr->numLogicalLines);
    }
    if (tagonPtr) {
	if (!TkTextTagSetIsEqual(tagonPtr, nodePtr->tagonPtr)) {
	    Tcl_Panic("CheckNodeConsistency: sum of node tag information is wrong (tagon)");
	}
	assert(additionalTagoffPtr);
	additionalTagoffPtr = TkTextTagSetComplementTo(additionalTagoffPtr, tagonPtr);
	tagoffPtr = TkTextTagSetJoin(tagoffPtr, additionalTagoffPtr);
	if (!TkTextTagSetIsEqual(tagoffPtr, nodePtr->tagoffPtr)) {
	    Tcl_Panic("CheckNodeConsistency: sum of node tag information is wrong (tagoff)");
	}
	for (i = TkTextTagSetFindFirst(tagonPtr);
		i != TK_TEXT_TAG_SET_NPOS;
		i = TkTextTagSetFindNext(tagonPtr, i)) {
	    if (!sharedTextPtr->tagLookup[i]) {
		Tcl_Panic("CheckNodeConsistency: node tagon contains deleted tag %d", i);
	    }
	    if (sharedTextPtr->tagLookup[i]->isDisabled) {
		Tcl_Panic("CheckNodeConsistency: node tagon contains disabled tag %d", i);
	    }
	}

	TkTextTagSetDecrRefCount(tagonPtr);
	TkTextTagSetDecrRefCount(tagoffPtr);
	TkTextTagSetDecrRefCount(additionalTagoffPtr);
    }
    for (i = 0; i < references; i++) {
	if (pixelInfo[i].pixels != nodePtr->pixelInfo[i].pixels) {
	    Tcl_Panic("CheckNodeConsistency: mismatch in pixel count "
		    "(expected: %d, counted: %d) for widget (%d) at level %d",
		    pixelInfo[i].pixels, nodePtr->pixelInfo[i].pixels, i, nodePtr->level);
	}
	if (pixelInfo[i].numDispLines != nodePtr->pixelInfo[i].numDispLines) {
	    Tcl_Panic("CheckNodeConsistency: mismatch in number of display lines "
		    "(expected: %d, counted: %d) for widget (%d) at level %d",
		    pixelInfo[i].numDispLines, nodePtr->pixelInfo[i].numDispLines,
		    i, nodePtr->level);
	}
    }
    if (pixelInfo != pixelInfoBuf) {
	free(pixelInfo);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DeleteEmptyNode --
 *
 *	This function is deleting a level-0 node from the B-tree.
 *	It is also deleting the parents recursively upwards until
 *	a non-empty node is found.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The internal structure of treePtr will change. The pixel counts
 *	will be updated.
 *
 *----------------------------------------------------------------------
 */

static void
DeleteEmptyNode(
    BTree *treePtr,	/* B-tree that is being modified. */
    Node *nodePtr)	/* Level-0 node that will be deleted. */
{
    TkTextLine *linePtr, *lastPtr, *nextPtr, *prevPtr;
    Node *parentPtr;
    NodePixelInfo *changeToPixelInfo;
    unsigned ref;

    assert(nodePtr->level == 0);
    assert(nodePtr->numChildren == 0);
    assert(nodePtr->linePtr);

    changeToPixelInfo = treePtr->pixelInfoBuffer;
    memset(changeToPixelInfo, 0, treePtr->numPixelReferences * sizeof(changeToPixelInfo[0]));

    /*
     * The pixel count of this node is going to zero.
     */

    for (linePtr = nodePtr->linePtr, lastPtr = nodePtr->lastPtr->nextPtr;
	    linePtr != lastPtr;
	    linePtr = linePtr->nextPtr) {
	NodePixelInfo *dst = changeToPixelInfo;

	for (ref = 0; ref < treePtr->numPixelReferences; ++ref, ++dst) {
	    dst->pixels += linePtr->pixelInfo[ref].height;
	    dst->numDispLines += GetDisplayLines(linePtr, ref);
	}
    }
    SubtractPixelCount2(treePtr, nodePtr->parentPtr, nodePtr->numLines, nodePtr->numLogicalLines,
	    nodePtr->numBranches, nodePtr->size, changeToPixelInfo);

    lastPtr = nodePtr->lastPtr;
    prevPtr = nodePtr->linePtr->prevPtr;
    parentPtr = nodePtr->parentPtr;
    for ( ; parentPtr && parentPtr->lastPtr == lastPtr; parentPtr = parentPtr->parentPtr) {
	parentPtr->lastPtr = prevPtr;
    }

    linePtr = nodePtr->linePtr;
    nextPtr = nodePtr->lastPtr->nextPtr;
    parentPtr = nodePtr->parentPtr;
    for ( ; parentPtr && parentPtr->linePtr == linePtr; parentPtr = parentPtr->parentPtr) {
	parentPtr->linePtr = nextPtr;
    }

    do {
	TkTextTagSet *tagonPtr;
	unsigned i;

	parentPtr = nodePtr->parentPtr;

	if (parentPtr->childPtr == nodePtr) {
	    parentPtr->childPtr = nodePtr->nextPtr;
	} else {
	    Node *prevNodePtr = parentPtr->childPtr;

	    while (prevNodePtr->nextPtr != nodePtr) {
		prevNodePtr = prevNodePtr->nextPtr;
	    }
	    prevNodePtr->nextPtr = nodePtr->nextPtr;
	}
	parentPtr->numChildren -= 1;

	/*
	 * Remove all tags from this node.
	 */

	tagonPtr = nodePtr->tagonPtr;
	TkTextTagSetIncrRefCount(tagonPtr);
	for (i = TkTextTagSetFindFirst(tagonPtr);
		i != TK_TEXT_TAG_SET_NPOS;
		i = TkTextTagSetFindNext(tagonPtr, i)) {
	    RemoveTagFromNode(nodePtr, treePtr->sharedTextPtr->tagLookup[i]);
	}
	TkTextTagSetDecrRefCount(tagonPtr);

	FreeNode(nodePtr);
	nodePtr = parentPtr;
    } while (nodePtr->numChildren == 0);
}

/*
 *----------------------------------------------------------------------
 *
 * Rebalance --
 *
 *	This function is called when a node of a B-tree appears to be out of
 *	balance (too many children, or too few). It rebalances that node and
 *	all of its ancestors in the tree.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The internal structure of treePtr may change.
 *
 *----------------------------------------------------------------------
 */

static void
RebalanceAssignNewParentToChildren(
    Node *nodePtr)
{
    if (nodePtr->level == 0) {
	TkTextLine *lastPtr = nodePtr->lastPtr->nextPtr;
	TkTextLine *linePtr;

	for (linePtr = nodePtr->linePtr; linePtr != lastPtr; linePtr = linePtr->nextPtr) {
	    linePtr->parentPtr = nodePtr;
	}
    } else {
	Node *childPtr = nodePtr->childPtr;

	for (childPtr = nodePtr->childPtr; childPtr; childPtr = childPtr->nextPtr) {
	    childPtr->parentPtr = nodePtr;
	}
    }
}

static void
RebalanceAddLinePixels(
    NodePixelInfo *dstPixels,
    const TkTextLine *linePtr,
    unsigned numRefs)
{
    const TkTextPixelInfo *srcPixelInfo = linePtr->pixelInfo;
    const TkTextPixelInfo *e = srcPixelInfo + numRefs;

    for ( ; srcPixelInfo < e; ++srcPixelInfo, ++dstPixels) {
	dstPixels->pixels += srcPixelInfo->height;
	dstPixels->numDispLines += TkBTreeGetNumberOfDisplayLines(srcPixelInfo);
    }
}

static void
RebalanceAddNodePixels(
    NodePixelInfo *dstPixels,
    const NodePixelInfo *srcPixels,
    unsigned numRefs)
{
    const NodePixelInfo *e = srcPixels + numRefs;

    for ( ; srcPixels < e; ++srcPixels, ++dstPixels) {
	dstPixels->pixels += srcPixels->pixels;
	dstPixels->numDispLines += srcPixels->numDispLines;
    }
}

static void
RebalanceSubtractNodePixels(
    NodePixelInfo *dstPixels,
    const NodePixelInfo *srcPixels,
    unsigned numRefs)
{
    const NodePixelInfo *e = srcPixels + numRefs;

    for ( ; srcPixels < e; ++srcPixels, ++dstPixels) {
	dstPixels->pixels -= srcPixels->pixels;
	dstPixels->numDispLines -= srcPixels->numDispLines;
    }
}

static void
RebalanceRecomputeNodeTagInfo(
    Node *nodePtr,
    TkSharedText *sharedTextPtr)
{
    TkTextTagSet *additionalTagoffPtr = NULL;

    assert(TkTextTagSetIsEmpty(nodePtr->tagonPtr));
    assert(TkTextTagSetIsEmpty(nodePtr->tagoffPtr));

    if (nodePtr->level == 0) {
	TkTextLine *linePtr = nodePtr->linePtr;
	TkTextLine *lastPtr = nodePtr->lastPtr->nextPtr;

	for ( ; linePtr != lastPtr; linePtr = linePtr->nextPtr) {
	    nodePtr->tagonPtr = TkTextTagSetJoin(nodePtr->tagonPtr, linePtr->tagonPtr);
	    nodePtr->tagoffPtr = TkTextTagSetJoin(nodePtr->tagoffPtr, linePtr->tagoffPtr);
	    if (additionalTagoffPtr) {
		additionalTagoffPtr = TkTextTagSetIntersect(additionalTagoffPtr, linePtr->tagonPtr);
	    } else {
		TkTextTagSetIncrRefCount(additionalTagoffPtr = linePtr->tagonPtr);
	    }
	}
    } else {
	Node *childPtr = nodePtr->childPtr;

	for ( ; childPtr; childPtr = childPtr->nextPtr) {
	    nodePtr->tagonPtr = TkTextTagSetJoin(nodePtr->tagonPtr, childPtr->tagonPtr);
	    nodePtr->tagoffPtr = TkTextTagSetJoin(nodePtr->tagoffPtr, childPtr->tagoffPtr);
	    if (additionalTagoffPtr) {
		additionalTagoffPtr = TkTextTagSetIntersect(additionalTagoffPtr, nodePtr->tagonPtr);
	    } else {
		TkTextTagSetIncrRefCount(additionalTagoffPtr = nodePtr->tagonPtr);
	    }
	}
    }

    assert(additionalTagoffPtr);

    /*
     * Finally add any tag to tagoff, if it is contained in at least one child, but not in all.
     */

    nodePtr->tagoffPtr = TagSetJoinComplementTo(
	    nodePtr->tagoffPtr, additionalTagoffPtr, nodePtr->tagonPtr, sharedTextPtr);
    TkTextTagSetDecrRefCount(additionalTagoffPtr);
}

static Node *
RebalanceFindSiblingForTag(
    Node *parentPtr,
    unsigned tagIndex)
{
    Node *childPtr;
    Node *nodePtr = NULL;

    for (childPtr = parentPtr->childPtr; childPtr; childPtr = childPtr->nextPtr) {
	if (TkTextTagSetTest(childPtr->tagonPtr, tagIndex)) {
	    if (nodePtr) {
		return NULL;
	    }
	    nodePtr = childPtr;
	}
    }

    return nodePtr;
}

static void
RebalanceRecomputeTagRootsAfterSplit(
    Node *parentPtr,
    TkSharedText *sharedTextPtr)
{
    const TkTextTagSet *tagInfoPtr = parentPtr->tagonPtr;
    unsigned childLevel = parentPtr->level - 1;
    unsigned i;

    for (i = TkTextTagSetFindFirst(tagInfoPtr);
	    i != TK_TEXT_TAG_SET_NPOS;
	    i = TkTextTagSetFindNext(tagInfoPtr, i)) {
	TkTextTag *tagPtr = sharedTextPtr->tagLookup[i];
	const Node *rootPtr;

	assert(tagPtr);
	assert(!tagPtr->isDisabled);

	rootPtr = tagPtr->rootPtr;

	if (rootPtr == parentPtr || rootPtr->level == childLevel) {
	    Node *nodePtr;

	    /*
	     * Either we have a sibling which has collected all occurrences, so move
	     * the root to this node, or more than one sibling contains this tag,
	     * so the parent is the root.
	     */

	    nodePtr = RebalanceFindSiblingForTag(parentPtr, i);
	    tagPtr->rootPtr = nodePtr ? nodePtr : parentPtr;
	}
    }
}

static bool
RebalanceHasCollectedAll(
    const Node *nodePtr,
    const Node *excludePtr,	/* don't test this node */
    unsigned tagIndex)
{
    for ( ; nodePtr; nodePtr = nodePtr->nextPtr) {
	if (nodePtr != excludePtr && TkTextTagSetTest(nodePtr->tagonPtr, tagIndex)) {
	    return false;
	}
    }
    return true;
}

static void
RebalanceRecomputeTagRootsAfterMerge(
    Node *resultPtr,		/* The node as the result of the merge. */
    const Node *mergePtr,	/* The node which has been merged into resultPtr. */
    TkSharedText *sharedTextPtr)
{
    unsigned i;

    assert(resultPtr->parentPtr);

    for (i = TkTextTagSetFindFirst(resultPtr->tagonPtr);
	    i != TK_TEXT_TAG_SET_NPOS;
	    i = TkTextTagSetFindNext(resultPtr->tagonPtr, i)) {
	TkTextTag *tagPtr = sharedTextPtr->tagLookup[i];
	const Node *tagRootPtr;

	assert(tagPtr);
	assert(!tagPtr->isDisabled);

	tagRootPtr = tagPtr->rootPtr;

	/*
	 * We have three cases:
	 *
	 * 1. mergePtr is the root of this tag; simply move the root to resultPtr.
	 *
	 * 2. The parent of these nodes is root of this tag, and resultPtr now has
	 *    collected all occurrences of this tag; simply move the root one level
	 *    down to resultPtr.
	 *
	 * 3. Otherwise, simply do nothing.
	 */

	if (tagRootPtr == mergePtr) {
	    tagPtr->rootPtr = resultPtr;
	} else if (tagRootPtr == resultPtr->parentPtr) {
	    if (RebalanceHasCollectedAll(resultPtr->parentPtr->childPtr, resultPtr, i)) {
		tagPtr->rootPtr = resultPtr;
	    }
	}
    }
}

static Node *
RebalanceDivideChildren(
    Node *nodePtr,
    Node *otherPtr,		/* can be NULL */
    unsigned minChildren,	/* split after this number of children */
    unsigned numRefs)
{
    Node *childPtr = nodePtr->childPtr;
    Node *divideChildPtr = NULL;

    assert(nodePtr->level > 0);
    assert(minChildren > 0);

    nodePtr->numLines = 0;
    nodePtr->numLogicalLines = 0;
    nodePtr->numBranches = 0;
    nodePtr->size = 0;

    for ( ; childPtr->nextPtr; childPtr = childPtr->nextPtr) {
	if (!divideChildPtr) {
	    nodePtr->numLines += childPtr->numLines;
	    nodePtr->numLogicalLines += childPtr->numLogicalLines;
	    nodePtr->numBranches += childPtr->numBranches;
	    nodePtr->size += childPtr->size;
	    RebalanceAddNodePixels(nodePtr->pixelInfo, childPtr->pixelInfo, numRefs);
	}
	if (--minChildren == 0) {
	    if (!otherPtr) {
		return childPtr;
	    }
	    divideChildPtr = childPtr;
	}
    }

    assert(otherPtr);

    childPtr->nextPtr = otherPtr->childPtr;

    if (!divideChildPtr) {
	assert(minChildren > 1);
	nodePtr->numLines += childPtr->numLines;
	nodePtr->numLogicalLines += childPtr->numLogicalLines;
	nodePtr->size += childPtr->size;
	RebalanceAddNodePixels(nodePtr->pixelInfo, childPtr->pixelInfo, numRefs);
	for ( ; minChildren > 1; --minChildren) {
	    childPtr = childPtr->nextPtr;
	    nodePtr->numLines += childPtr->numLines;
	    nodePtr->numLogicalLines += childPtr->numLogicalLines;
	    nodePtr->numBranches += childPtr->numBranches;
	    nodePtr->size += childPtr->size;
	    RebalanceAddNodePixels(nodePtr->pixelInfo, childPtr->pixelInfo, numRefs);
	}
	assert(childPtr);
	divideChildPtr = childPtr;
    }

    return divideChildPtr;
}

static TkTextLine *
RebalanceDivideLines(
    Node *nodePtr,
    unsigned minLines,
    unsigned numRefs)
{
    TkTextLine *divideLinePtr = nodePtr->linePtr;

    assert(nodePtr->level == 0);
    assert(minLines > 0);

    RebalanceAddLinePixels(nodePtr->pixelInfo, divideLinePtr, numRefs);
    nodePtr->size = divideLinePtr->size;
    nodePtr->numLogicalLines = divideLinePtr->logicalLine;
    nodePtr->numBranches = divideLinePtr->numBranches;

    for ( ; minLines > 1; --minLines) {
	divideLinePtr = divideLinePtr->nextPtr;
	nodePtr->size += divideLinePtr->size;
	nodePtr->numLogicalLines += divideLinePtr->logicalLine;
	nodePtr->numBranches += divideLinePtr->numBranches;
	RebalanceAddLinePixels(nodePtr->pixelInfo, divideLinePtr, numRefs);
    }

    return divideLinePtr;
}

static void
RebalanceFinalizeNodeSplits(
    Node **firstNodePtr,
    Node *lastNodePtr,			/* inclusive this node */
    TkSharedText *sharedTextPtr)
{
    Node *nodePtr;

    if (!*firstNodePtr) {
	return;
    }

    lastNodePtr = lastNodePtr->nextPtr;

    for (nodePtr = *firstNodePtr; nodePtr != lastNodePtr; nodePtr = nodePtr->nextPtr) {
	TagSetAssign(&nodePtr->tagonPtr, sharedTextPtr->emptyTagInfoPtr);
	TagSetAssign(&nodePtr->tagoffPtr, sharedTextPtr->emptyTagInfoPtr);
	RebalanceAssignNewParentToChildren(nodePtr);
	RebalanceRecomputeNodeTagInfo(nodePtr, sharedTextPtr);
    }

    RebalanceRecomputeTagRootsAfterSplit((*firstNodePtr)->parentPtr, sharedTextPtr);
    *firstNodePtr = NULL;
}

static void
RebalanceNodeJoinTagInfo(
    Node *dstPtr,
    Node *srcPtr,
    const TkSharedText *sharedTextPtr)
{
    assert(dstPtr);
    assert(srcPtr);
    assert(sharedTextPtr);

    if (srcPtr->tagonPtr == dstPtr->tagonPtr && srcPtr->tagoffPtr == dstPtr->tagoffPtr) {
	return;
    }

    if (dstPtr->tagonPtr == sharedTextPtr->emptyTagInfoPtr) {
	dstPtr->tagoffPtr = TkTextTagSetJoin2(dstPtr->tagoffPtr, srcPtr->tagoffPtr, srcPtr->tagonPtr);
    } else if (srcPtr->tagonPtr == sharedTextPtr->emptyTagInfoPtr) {
	dstPtr->tagoffPtr = TkTextTagSetJoin2(dstPtr->tagoffPtr, srcPtr->tagoffPtr, dstPtr->tagonPtr);
    } else {
#if !TK_TEXT_DONT_USE_BITFIELDS
	unsigned size1 = TkTextTagSetSize(dstPtr->tagonPtr);
	unsigned size2 = TkTextTagSetSize(srcPtr->tagonPtr);
	unsigned minSize = MAX(TkTextTagSetSize(srcPtr->tagoffPtr), MAX(size1, size2));

	if (TkTextTagSetSize(dstPtr->tagoffPtr) < minSize) {
	    dstPtr->tagoffPtr = TkTextTagSetResize(dstPtr->tagoffPtr, sharedTextPtr->tagInfoSize);
	}
	if (size1 < size2) {
	    dstPtr->tagonPtr = TkTextTagSetResize(dstPtr->tagonPtr, size2);
	} else if (size2 < size1) {
	    srcPtr->tagonPtr = TkTextTagSetResize(srcPtr->tagonPtr, size1);
	}
#endif /* !TK_TEXT_DONT_USE_BITFIELDS */

	dstPtr->tagoffPtr = TkTextTagSetJoin2ComplementToIntersection(
		dstPtr->tagoffPtr, srcPtr->tagoffPtr, dstPtr->tagonPtr, srcPtr->tagonPtr);
    }
    if (TkTextTagSetIsEmpty(dstPtr->tagoffPtr)) {
	TagSetAssign(&dstPtr->tagoffPtr, sharedTextPtr->emptyTagInfoPtr);
    }
    dstPtr->tagonPtr = TkTextTagSetJoin(dstPtr->tagonPtr, srcPtr->tagonPtr);
}

static void
Rebalance(
    BTree *treePtr,	/* Tree that is being rebalanced. */
    Node *nodePtr)	/* Node that may be out of balance. */
{
    unsigned numRefs = treePtr->numPixelReferences;
    unsigned pixelSize = sizeof(nodePtr->pixelInfo[0])*numRefs;

    /*
     * Loop over the entire ancestral chain of the node, working up through
     * the tree one node at a time until the root node has been processed.
     */

    for ( ; nodePtr; nodePtr = nodePtr->parentPtr) {
	Node *firstNodePtr = NULL;
	Node *lastNodePtr = NULL;

	/*
	 * Check to see if the node has too many children. If it does, then split off
	 * all but the first MIN_CHILDREN into a separate node following the original
	 * one. Then repeat until the node has a decent size.
	 */

	if (nodePtr->numChildren > MAX_CHILDREN) {
	    firstNodePtr = nodePtr;

	    do {
		Node *newPtr;

		/*
		 * If the node being split is the root node, then make a new root node above it first.
		 */

		if (!nodePtr->parentPtr) {
		    Node *newRootPtr = malloc(sizeof(Node));
		    newRootPtr->parentPtr = NULL;
		    newRootPtr->nextPtr = NULL;
		    newRootPtr->childPtr = nodePtr;
		    newRootPtr->linePtr = nodePtr->linePtr;
		    newRootPtr->lastPtr = nodePtr->lastPtr;
		    TkTextTagSetIncrRefCount(newRootPtr->tagonPtr = nodePtr->tagonPtr);
		    TkTextTagSetIncrRefCount(newRootPtr->tagoffPtr = nodePtr->tagoffPtr);
		    newRootPtr->numChildren = 1;
		    newRootPtr->numLines = nodePtr->numLines;
		    newRootPtr->numLogicalLines = nodePtr->numLogicalLines;
		    newRootPtr->numBranches = nodePtr->numBranches;
		    newRootPtr->level = nodePtr->level + 1;
		    newRootPtr->size = nodePtr->size;
		    newRootPtr->pixelInfo = memcpy(malloc(pixelSize), nodePtr->pixelInfo, pixelSize);
		    nodePtr->parentPtr = newRootPtr;
		    treePtr->rootPtr = newRootPtr;
		    DEBUG_ALLOC(tkTextCountNewNode++);
		    DEBUG_ALLOC(tkTextCountNewPixelInfo++);
		}

		newPtr = malloc(sizeof(Node));
		newPtr->parentPtr = nodePtr->parentPtr;
		newPtr->nextPtr = nodePtr->nextPtr;
		newPtr->lastPtr = nodePtr->lastPtr;
		newPtr->tagonPtr = treePtr->sharedTextPtr->emptyTagInfoPtr;
		newPtr->tagoffPtr = treePtr->sharedTextPtr->emptyTagInfoPtr;
		TkTextTagSetIncrRefCount(newPtr->tagonPtr);
		TkTextTagSetIncrRefCount(newPtr->tagoffPtr);
		newPtr->numChildren = nodePtr->numChildren - MIN_CHILDREN;
		newPtr->level = nodePtr->level;
		newPtr->size = nodePtr->size;
		newPtr->pixelInfo = nodePtr->pixelInfo;
		newPtr->numLines = nodePtr->numLines;
		newPtr->numLogicalLines = nodePtr->numLogicalLines;
		newPtr->numBranches = nodePtr->numBranches;
		nodePtr->nextPtr = newPtr;
		nodePtr->numChildren = MIN_CHILDREN;
		nodePtr->pixelInfo = memset(malloc(pixelSize), 0, pixelSize);
		TagSetAssign(&nodePtr->tagonPtr, treePtr->sharedTextPtr->emptyTagInfoPtr);
		TagSetAssign(&nodePtr->tagoffPtr, treePtr->sharedTextPtr->emptyTagInfoPtr);
		DEBUG_ALLOC(tkTextCountNewNode++);
		DEBUG_ALLOC(tkTextCountNewPixelInfo++);
		if (nodePtr->level == 0) {
		    TkTextLine *linePtr = RebalanceDivideLines(nodePtr, MIN_CHILDREN, numRefs);
		    assert(linePtr->nextPtr);
		    newPtr->childPtr = NULL;
		    newPtr->linePtr = linePtr->nextPtr;
		    newPtr->numLines = newPtr->numChildren;
		    nodePtr->lastPtr = linePtr;
		    nodePtr->numLines = MIN_CHILDREN;
		} else {
		    Node *childPtr = RebalanceDivideChildren(nodePtr, NULL, MIN_CHILDREN, numRefs);
		    newPtr->childPtr = childPtr->nextPtr;
		    newPtr->linePtr = childPtr->nextPtr->linePtr;
		    newPtr->numLines -= nodePtr->numLines;
		    nodePtr->lastPtr = childPtr->lastPtr;
		    childPtr->nextPtr = NULL;
		}
		RebalanceSubtractNodePixels(newPtr->pixelInfo, nodePtr->pixelInfo, numRefs);
		newPtr->size -= nodePtr->size;
		newPtr->numLogicalLines -= nodePtr->numLogicalLines;
		newPtr->numBranches -= nodePtr->numBranches;
		nodePtr->parentPtr->numChildren += 1;
		lastNodePtr = nodePtr = newPtr;
	    } while (nodePtr->numChildren > MAX_CHILDREN);
	}

	while (nodePtr->numChildren < MIN_CHILDREN) {
	    Node *otherPtr;
	    unsigned totalChildren;

	    /*
	     * Too few children for this node. If this is the root then, it's OK
	     * for it to have less than MIN_CHILDREN children as long as it's got
	     * at least two. If it has only one (and isn't at level 0), then chop
	     * the root node out of the tree and use its child as the new root.
	     */

	    if (!nodePtr->parentPtr) {
		if (nodePtr->numChildren == 1 && nodePtr->level > 0) {
		    treePtr->rootPtr = nodePtr->childPtr;
		    treePtr->rootPtr->parentPtr = NULL;
		    FreeNode(nodePtr);
		}
		return;
	    }

	    /*
	     * Not the root. Make sure that there are siblings to balance with.
	     */

	    if (nodePtr->parentPtr->numChildren < 2) {
		/* Do the finalization of previous splits. */
		RebalanceFinalizeNodeSplits(&firstNodePtr, lastNodePtr, treePtr->sharedTextPtr);
		Rebalance(treePtr, nodePtr->parentPtr);
		continue;
	    }

	    /*
	     * Find a sibling neighbor to borrow from, and arrange for nodePtr
	     * to be the earlier of the pair.
	     */

	    if (!nodePtr->nextPtr) {
		for (otherPtr = nodePtr->parentPtr->childPtr;
			otherPtr->nextPtr != nodePtr;
			otherPtr = otherPtr->nextPtr) {
		    /* Empty loop body. */
		}
		nodePtr = otherPtr;
	    }
	    otherPtr = nodePtr->nextPtr;

	    /*
	     * We're going to either merge the two siblings together into one
	     * node or redivide the children among them to balance their loads.
	     */

	    totalChildren = nodePtr->numChildren + otherPtr->numChildren;

	    /*
	     * The successor node will contain the sum of both pixel counts.
	     */

	    RebalanceAddNodePixels(otherPtr->pixelInfo, nodePtr->pixelInfo, numRefs);

	    if (!nodePtr->childPtr) {
		nodePtr->childPtr = otherPtr->childPtr;
		otherPtr->childPtr = NULL;
	    }

	    if (totalChildren <= MAX_CHILDREN) {
		NodePixelInfo *pixelInfo;
		Node *childPtr;

		/*
		 * Do the finalization of previous splits.
		 */

		RebalanceFinalizeNodeSplits(&firstNodePtr, lastNodePtr, treePtr->sharedTextPtr);

		/*
		 * Simply merge the two siblings. At first join their two child
		 * lists into a single list.
		 */

		if (nodePtr->level > 0) {
		    for (childPtr = nodePtr->childPtr; childPtr->nextPtr; childPtr = childPtr->nextPtr) {
			/* empty loop body */
		    }
		    childPtr->nextPtr = otherPtr->childPtr;
		}

		nodePtr->lastPtr = otherPtr->lastPtr;
		nodePtr->nextPtr = otherPtr->nextPtr;
		nodePtr->numChildren = totalChildren;
		nodePtr->numLines += otherPtr->numLines;
		nodePtr->numLogicalLines += otherPtr->numLogicalLines;
		nodePtr->numBranches += otherPtr->numBranches;
		nodePtr->parentPtr->numChildren -= 1;
		nodePtr->size += otherPtr->size;
		/* swap pixel count */
		pixelInfo = nodePtr->pixelInfo;
		nodePtr->pixelInfo = otherPtr->pixelInfo;
		otherPtr->pixelInfo = pixelInfo;

		RebalanceAssignNewParentToChildren(nodePtr);
		RebalanceNodeJoinTagInfo(nodePtr, otherPtr, treePtr->sharedTextPtr);
		RebalanceRecomputeTagRootsAfterMerge(nodePtr, otherPtr, treePtr->sharedTextPtr);
		FreeNode(otherPtr);
	    } else {
		/*
		 * The siblings can't be merged, so just divide their children evenly between them.
		 */

		unsigned firstChildren = totalChildren/2;

		/*
		 * Remember this node for finalization.
		 */

		if (!firstNodePtr) {
		    firstNodePtr = nodePtr;
		}
		lastNodePtr = otherPtr;

		otherPtr->size += nodePtr->size;
		otherPtr->numLogicalLines += nodePtr->numLogicalLines;
		otherPtr->numBranches += nodePtr->numBranches;

		/* Prepare pixel count in nodePtr, DivideLines/DivideChildren will do the count. */
		memset(nodePtr->pixelInfo, 0, pixelSize);

		nodePtr->numChildren = firstChildren;
		otherPtr->numChildren = totalChildren - firstChildren;

		if (nodePtr->level == 0) {
		    TkTextLine *halfwayLinePtr = RebalanceDivideLines(nodePtr, firstChildren, numRefs);

		    nodePtr->numLines = nodePtr->numChildren;
		    nodePtr->lastPtr = halfwayLinePtr;
		    otherPtr->linePtr = halfwayLinePtr->nextPtr;
		    otherPtr->numLines = otherPtr->numChildren;
		} else {
		    unsigned totalLines = nodePtr->numLines + otherPtr->numLines;
		    Node *halfwayNodePtr;

		    halfwayNodePtr = RebalanceDivideChildren(nodePtr, otherPtr, firstChildren, numRefs);
		    nodePtr->lastPtr = halfwayNodePtr->lastPtr;
		    otherPtr->numLines = totalLines - nodePtr->numLines;
		    otherPtr->linePtr = halfwayNodePtr->nextPtr->linePtr;
		    otherPtr->childPtr = halfwayNodePtr->nextPtr;
		    halfwayNodePtr->nextPtr = NULL;
		}

		otherPtr->size -= nodePtr->size;
		otherPtr->numLogicalLines -= nodePtr->numLogicalLines;
		otherPtr->numBranches -= nodePtr->numBranches;
		RebalanceSubtractNodePixels(otherPtr->pixelInfo, nodePtr->pixelInfo, numRefs);
	    }
	}

	/*
	 * Do the finalization of previous splits.
	 */

	RebalanceFinalizeNodeSplits(&firstNodePtr, lastNodePtr, treePtr->sharedTextPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeGetLogicalLine --
 *
 *	Given a line, this function is searching in B-Tree for the first
 *	line which belongs logically to given line due to elided newlines.
 *
 * Results:
 *	The return value is the first logical line belonging to given
 *	line, in most cases this will be the given line itself.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static const Node *
PrevLogicalNode(
    const Node *nodePtr)
{
    assert(nodePtr);

    while (nodePtr->parentPtr) {
	const Node *startNodePtr = nodePtr;
	const Node *lastNodePtr = NULL;

	nodePtr = nodePtr->parentPtr->childPtr;

	for ( ; nodePtr != startNodePtr; nodePtr = nodePtr->nextPtr) {
	    if (nodePtr->numLogicalLines > 0) {
		lastNodePtr = nodePtr;
	    }
	}
	if (lastNodePtr) {
	    nodePtr = lastNodePtr;

	    while (nodePtr->level > 0) {
		DEBUG(lastNodePtr = NULL);

		for (nodePtr = nodePtr->childPtr; nodePtr; nodePtr = nodePtr->nextPtr) {
		    if (nodePtr->numLogicalLines > 0) {
			lastNodePtr = nodePtr;
		    }
		}

		assert(lastNodePtr);
		nodePtr = lastNodePtr;
	    }

	    return lastNodePtr;
	}

	nodePtr = startNodePtr->parentPtr;
    }

    return NULL;
}

TkTextLine *
TkBTreeGetLogicalLine(
    const TkSharedText *sharedTextPtr,
    const TkText *textPtr,		/* can be NULL */
    TkTextLine *linePtr)
{
    const Node *nodePtr;
    TkTextLine *startLinePtr;

    assert(linePtr);

    if (linePtr->logicalLine || linePtr == GetStartLine(sharedTextPtr, textPtr)) {
	return linePtr;
    }

    nodePtr = linePtr->parentPtr;
    startLinePtr = GetStartLine(sharedTextPtr, textPtr);

    /*
     * At first, search for logical line in current node.
     */

    while (linePtr->parentPtr == nodePtr) {
	if (linePtr->logicalLine || linePtr == startLinePtr) {
	    return linePtr;
	}
	linePtr = linePtr->prevPtr;
    }

    /*
     * We couldn't find a line, so search inside B-Tree for next level-0
     * node which contains the logical line.
     */

    if (!(nodePtr = PrevLogicalNode(nodePtr))) {
	return startLinePtr;
    }

    if (textPtr && textPtr->startMarker != textPtr->sharedTextPtr->startMarker) {
	int lineNo1 = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, NULL, nodePtr->lastPtr, NULL);
	int lineNo2 = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, NULL, startLinePtr, NULL);

	if (lineNo1 <= lineNo2) {
	    /*
	     * We've found a node before text start, so return text start.
	     */
	    return startLinePtr;
	}
    }

    /*
     * Final search of logical line.
     */

    linePtr = nodePtr->lastPtr;
    while (!linePtr->logicalLine && linePtr != startLinePtr) {
	linePtr = linePtr->prevPtr;
    }
    return linePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeNextLogicalLine --
 *
 *	Given a line, this function is searching in the B-Tree for the
 *	next logical line, which don't has a predecessing line with
 *	elided newline. If the search reaches the end of the text, then
 *	the last line will be returned, even if it's not a logical line
 *	(the latter can only happen in peers with restricted ranges).
 *
 * Results:
 *	The return value is the next logical line, in most cases this
 *	will be simply the next line.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static const Node *
NextLogicalNode(
    const Node *nodePtr)
{
    while (nodePtr) {
	const Node *startNodePtr = nodePtr;

	for (nodePtr = nodePtr->nextPtr; nodePtr; nodePtr = nodePtr->nextPtr) {
	    if (nodePtr->numLogicalLines > 0) {
		while (nodePtr->level > 0) {
		    for (nodePtr = nodePtr->childPtr; nodePtr; nodePtr = nodePtr->nextPtr) {
			if (nodePtr->numLogicalLines > 0) {
			    return nodePtr;
			}
		    }
		}
		return nodePtr;
	    }
	}

	nodePtr = startNodePtr->parentPtr;
    }

    return NULL;
}

TkTextLine *
TkBTreeNextLogicalLine(
    const TkSharedText *sharedTextPtr,
    const TkText *textPtr,	/* can be NULL */
    TkTextLine *linePtr)
{
    const Node *nodePtr;
    TkTextLine *endLinePtr;

    assert(linePtr);
    assert(linePtr->nextPtr);
    assert(linePtr != GetLastLine(sharedTextPtr, textPtr));

    if (linePtr->nextPtr->logicalLine) {
	return linePtr->nextPtr;
    }

    /*
     * At first, search for logical line in current node.
     */

    nodePtr = linePtr->parentPtr;
    linePtr = linePtr->nextPtr;
    endLinePtr = GetLastLine(sharedTextPtr, textPtr);

    while (linePtr && linePtr->parentPtr == nodePtr) {
	if (linePtr->logicalLine || linePtr == endLinePtr) {
	    return linePtr;
	}
	linePtr = linePtr->nextPtr;
    }

    /*
     * We couldn't find a line, so search inside B-Tree for next level-0
     * node which contains the logical line.
     */

    if (!(nodePtr = NextLogicalNode(nodePtr))) {
	return endLinePtr;
    }

    if (textPtr && textPtr->startMarker != textPtr->sharedTextPtr->startMarker) {
	int lineNo1 = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, NULL, nodePtr->linePtr, NULL);
	int lineNo2 = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, NULL, endLinePtr, NULL);

	if (lineNo1 >= lineNo2) {
	    /*
	     * We've found a node after text end, so return text end.
	     */
	    return endLinePtr;
	}
    }

    /*
     * Final search of logical line.
     */

    linePtr = nodePtr->linePtr;
    while (!linePtr->logicalLine && linePtr != endLinePtr) {
	linePtr = linePtr->nextPtr;
    }
    return linePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeNextDisplayLine --
 *
 *	Given a logical line, and a display line number belonging to
 *	this logical line, find next display line 'offset' display lines
 *	ahead.
 *
 * Results:
 *	Returns the logcial line of the requested display line, and stores
 *	the display line number in 'dispLineNo'.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkTextLine *
GetLastDisplayLine(
    TkText *textPtr,
    unsigned *displayLineNo)
{
    TkTextLine *linePtr;

    linePtr = textPtr->endMarker->sectionPtr->linePtr;
    linePtr = TkBTreeGetLogicalLine(textPtr->sharedTextPtr, textPtr, linePtr);
    *displayLineNo = GetDisplayLines(linePtr, textPtr->pixelReference);
    return linePtr;
}

TkTextLine *
TkBTreeNextDisplayLine(
    TkText *textPtr,		/* Information about text widget. */
    TkTextLine *linePtr,	/* Start at this logical line. */
    unsigned *displayLineNo,	/* IN: Start at this display line number in given logical line.
    				 * OUT: Store display line number of requested display line. */
    unsigned offset)		/* Offset to requested display line. */
{
    const Node *nodePtr;
    const Node *parentPtr;
    int lineNo, numLines;
    unsigned numDispLines;
    unsigned ref;

    assert(textPtr);
    assert(linePtr->logicalLine || linePtr == TkBTreeGetStartLine(textPtr));
    assert(*displayLineNo >= 0);
    assert(*displayLineNo < GetDisplayLines(linePtr, textPtr->pixelReference));

    if (offset == 0) {
	return linePtr;
    }

    ref = textPtr->pixelReference;
    nodePtr = linePtr->parentPtr;
    parentPtr = nodePtr->parentPtr;
    offset += *displayLineNo;

    if (linePtr != nodePtr->linePtr || !parentPtr || HasLeftNode(nodePtr)) {
	TkTextLine *lastPtr;

	/*
	 * At first, search for display line in current node.
	 */

	lastPtr = nodePtr->lastPtr->nextPtr;

	while (linePtr != lastPtr) {
	    numDispLines = GetDisplayLines(linePtr, ref);
	    if (numDispLines > offset) {
		assert(linePtr->logicalLine);
		*displayLineNo = offset;
		return linePtr;
	    }
	    offset -= numDispLines;
	    if (!(linePtr = TkBTreeNextLine(textPtr, linePtr))) {
		return GetLastDisplayLine(textPtr, displayLineNo);
	    }
	}

	nodePtr = nodePtr->nextPtr;
    }

    /*
     * We couldn't find a line, so search inside B-Tree for next level-0
     * node which contains the display line.
     */

    lineNo = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, NULL, linePtr, NULL);
    numLines = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, NULL, TkBTreeGetLastLine(textPtr), NULL);

    while (parentPtr) {
	if (!nodePtr || (!HasLeftNode(nodePtr) && offset >= parentPtr->pixelInfo[ref].numDispLines)) {
	    offset -= parentPtr->pixelInfo[ref].numDispLines;
	    nodePtr = parentPtr->nextPtr;
	    parentPtr = parentPtr->parentPtr;
	} else {
	    while (nodePtr) {
		numDispLines = nodePtr->pixelInfo[ref].numDispLines;
		if (offset < numDispLines) {
		    if (nodePtr->level > 0) {
			nodePtr = nodePtr->childPtr;
			continue;
		    }
		    /*
		     * We've found the right node, now search for the line.
		     */
		    linePtr = nodePtr->linePtr;
		    while (true) {
			numDispLines = GetDisplayLines(linePtr, ref);
			if (offset < numDispLines) {
			    *displayLineNo = offset;
			    assert(linePtr->logicalLine);
			    return linePtr;
			}
			offset -= numDispLines;
			if (!(linePtr = TkBTreeNextLine(textPtr, linePtr))) {
			    return GetLastDisplayLine(textPtr, displayLineNo);
			}
		    }
		}
		if ((lineNo += nodePtr->numLines) >= numLines) {
		    parentPtr = NULL;
		    break;
		}
		offset -= numDispLines;
		nodePtr = nodePtr->nextPtr;
	    }
	}
    }

    return GetLastDisplayLine(textPtr, displayLineNo);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreePrevDisplayLine --
 *
 *	Given a logical line, and a display line number belonging to
 *	this logical line, find previous display line 'offset' display lines
 *	back.
 *
 * Results:
 *	Returns the logcial line of the requested display line, and stores
 *	the display line number in 'dispLineNo'.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkTextLine *
GetFirstDisplayLine(
    TkText *textPtr,
    unsigned *displayLineNo)
{
    *displayLineNo = 0;
    return textPtr->startMarker->sectionPtr->linePtr;
}

TkTextLine *
TkBTreePrevDisplayLine(
    TkText *textPtr,		/* Information about text widget. */
    TkTextLine *linePtr,	/* Start at this logical line. */
    unsigned *displayLineNo,	/* IN: Start at this display line number in given logical line.
    				 * OUT: Store display line number of requested display line. */
    unsigned offset)		/* Offset to requested display line. */
{
    const Node *nodeStack[MAX_CHILDREN];
    const Node *nodePtr;
    const Node *parentPtr;
    const Node *nPtr;
    unsigned numDispLines;
    unsigned ref;
    unsigned idx;
    int lineNo;

    assert(textPtr);
    assert(linePtr->logicalLine || linePtr == TkBTreeGetStartLine(textPtr));
    assert(*displayLineNo >= 0);
    assert(*displayLineNo < GetDisplayLines(linePtr, textPtr->pixelReference));

    if (offset == 0) {
	return linePtr;
    }

    ref = textPtr->pixelReference;
    nodePtr = linePtr->parentPtr;
    parentPtr = nodePtr->parentPtr;
    numDispLines = GetDisplayLines(linePtr, ref);
    offset += numDispLines - *displayLineNo - 1;

    if (linePtr != nodePtr->lastPtr || !parentPtr || nodePtr->nextPtr) {
	TkTextLine *lastPtr;

	/*
	 * At first, search for display line in current node.
	 */

	lastPtr = nodePtr->linePtr->prevPtr;

	while (linePtr != lastPtr) {
	    numDispLines = GetDisplayLines(linePtr, ref);
	    if (offset < numDispLines) {
		assert(linePtr->logicalLine);
		*displayLineNo = numDispLines - offset - 1;
		return linePtr;
	    }
	    offset -= numDispLines;
	    if (!(linePtr = TkBTreePrevLine(textPtr, linePtr))) {
		return GetFirstDisplayLine(textPtr, displayLineNo);
	    }
	}
    } else {
	nodePtr = nodePtr->nextPtr;
    }

    for (nPtr = parentPtr->childPtr, idx = 0; nPtr != nodePtr; nPtr = nPtr->nextPtr) {
	nodeStack[idx++] = nPtr;
    }
    nodePtr = idx ? nodeStack[--idx] : NULL;

    /*
     * We couldn't find a line, so search inside B-Tree for next level-0
     * node which contains the display line.
     */

    lineNo = TkBTreeLinesTo(textPtr->sharedTextPtr->tree, NULL, linePtr, NULL);

    while (parentPtr) {
	if (!nodePtr || (!nodePtr->nextPtr && offset >= parentPtr->pixelInfo[ref].numDispLines)) {
	    nodePtr = parentPtr;
	    if ((parentPtr = parentPtr->parentPtr)) {
		for (nPtr = parentPtr->childPtr, idx = 0; nPtr != nodePtr; nPtr = nPtr->nextPtr) {
		    nodeStack[idx++] = nPtr;
		}
		nodePtr = idx ? nodeStack[--idx] : NULL;
	    }
	} else {
	    while (nodePtr) {
		numDispLines = nodePtr->pixelInfo[ref].numDispLines;
		if (offset < numDispLines) {
		    if (nodePtr->level > 0) {
			parentPtr = nodePtr;
			idx = 0;
			for (nPtr = nodePtr->childPtr; nPtr; nPtr = nPtr->nextPtr) {
			    nodeStack[idx++] = nPtr;
			}
			nodePtr = idx ? nodeStack[--idx] : NULL;
			continue;
		    }
		    /*
		     * We've found the right node, now search for the line.
		     */
		    linePtr = nodePtr->lastPtr;
		    while (true) {
			numDispLines = GetDisplayLines(linePtr, ref);
			if (offset < numDispLines) {
			    assert(linePtr->logicalLine);
			    *displayLineNo = numDispLines - offset - 1;
			    return linePtr;
			}
			offset -= numDispLines;
			if (!(linePtr = TkBTreePrevLine(textPtr, linePtr))) {
			    return GetFirstDisplayLine(textPtr, displayLineNo);
			}
		    }
		}
		if ((lineNo -= nodePtr->numLines) < 0) {
		    parentPtr = NULL;
		    break;
		}
		offset -= numDispLines;
		nodePtr = idx ? nodeStack[--idx] : NULL;
	    }
	}
    }

    return GetFirstDisplayLine(textPtr, displayLineNo);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeFindStartOfElidedRange --
 *
 *	Given an elided segment, this function is searching for the
 *	first segment which is spanning the range containing the
 *	given segment. Normally this is a branch segment, but in
 *	case of restricted peers it may be a start marker.
 *
 * Results:
 *	The return value is a corresponding branch segment (or the
 *	start marker of this peer).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkTextSegment *
SearchBranchInLine(
    TkTextSegment *segPtr,
    TkTextSegment *startMarker)
{
    TkTextSection *sectionPtr = segPtr->sectionPtr;
    TkTextSection *startSectionPtr;

    /*
     * Note that a branch is always at the end of a section.
     */

    while (segPtr->nextPtr && segPtr->size == 0 && segPtr->nextPtr->sectionPtr == sectionPtr) {
	segPtr = segPtr->nextPtr;
    }

    if (segPtr->typePtr == &tkTextBranchType) {
	return segPtr;
    }

    startSectionPtr = startMarker ? startMarker->sectionPtr : NULL;

    if (sectionPtr == startSectionPtr) {
	return startMarker;
    }

    for ( ; sectionPtr->prevPtr; sectionPtr = sectionPtr->prevPtr) {
	if (sectionPtr->segPtr->prevPtr->typePtr == &tkTextBranchType) {
	    return sectionPtr->segPtr->prevPtr;
	}
	if (sectionPtr == startSectionPtr) {
	    return startMarker;
	}
    }

    return NULL;
}

static const Node *
FindNodeWithBranch(
    const TkSharedText *sharedTextPtr,
    const TkText *textPtr,		/* can be NULL */
    const Node *nodePtr)
{
    const Node *parentPtr;

    assert(nodePtr);

    for (parentPtr = nodePtr->parentPtr; parentPtr; parentPtr = parentPtr->parentPtr) {
	const Node *resultPtr = NULL;
	const Node *childPtr;

	if (parentPtr->numBranches > 0) {
	    for (childPtr = parentPtr->childPtr; childPtr != nodePtr; childPtr = childPtr->nextPtr) {
		if (childPtr->numBranches > 0) {
		    resultPtr = childPtr;
		}
	    }
	    if (resultPtr) {
		while (resultPtr->level > 0) {
		    for (childPtr = resultPtr->childPtr; childPtr; childPtr = childPtr->nextPtr) {
			if (childPtr->numBranches > 0) {
			    resultPtr = childPtr;
			}
		    }
		}
		return resultPtr;
	    }
	}
	nodePtr = parentPtr;
    }

    return TkBTreeGetRoot(sharedTextPtr->tree)->linePtr->parentPtr;
}

static TkTextSegment *
FindBranchSegment(
    const TkSharedText *sharedTextPtr,
    const TkText *textPtr,		/* can be NULL */
    const TkTextSegment *segPtr,
    TkTextSegment *startMarker)
{
    const Node *nodePtr;
    TkTextLine *firstLinePtr;
    TkTextLine *linePtr;

    assert(segPtr);
    assert(segPtr->tagInfoPtr);
    assert(TkBTreeHaveElidedSegments(sharedTextPtr));
    assert(SegmentIsElided(sharedTextPtr, segPtr, textPtr));

    linePtr = segPtr->sectionPtr->linePtr;
    nodePtr = linePtr->parentPtr;
    firstLinePtr = startMarker ? GetStartLine(sharedTextPtr, textPtr) : NULL;

    /*
     * At first, search for branch in current line.
     */

    if (linePtr->numBranches > 0) {
	TkTextSegment *branchPtr = SearchBranchInLine((TkTextSegment *) segPtr, startMarker);

	if (branchPtr) {
	    return branchPtr;
	}
    }

    /*
     * At second, search for line with a branch in current node.
     */

    linePtr = linePtr->prevPtr;
    while (linePtr && linePtr->parentPtr == nodePtr) {
	TkTextLine *prevPtr = linePtr->prevPtr;

	if (linePtr->numBranches > 0) {
	    return SearchBranchInLine(linePtr->lastPtr, startMarker);
	}
	if (prevPtr == firstLinePtr) {
	    return startMarker;
	}
	linePtr = prevPtr;
    }

    /*
     * We couldn't find a line, so search inside B-Tree for next level-0
     * node which contains a branch.
     */

    nodePtr = FindNodeWithBranch(sharedTextPtr, textPtr, nodePtr);

    if (startMarker && startMarker != sharedTextPtr->startMarker) {
	int lineNo1 = TkBTreeLinesTo(sharedTextPtr->tree, NULL, nodePtr->lastPtr, NULL);
	int lineNo2 = TkBTreeLinesTo(sharedTextPtr->tree, NULL, startMarker->sectionPtr->linePtr, NULL);

	if (lineNo1 <= lineNo2) {
	    /*
	     * We've found a node before text start, so return text start.
	     */
	    return startMarker;
	}
    }

    /*
     * Final search of branch segment.
     */

    linePtr = nodePtr->lastPtr;
    while (linePtr->numBranches == 0) {
	if (linePtr == firstLinePtr) {
	    return startMarker;
	}
	linePtr = linePtr->prevPtr;
	assert(linePtr);
    }

    return SearchBranchInLine(linePtr->lastPtr, startMarker);
}

TkTextSegment *
TkBTreeFindStartOfElidedRange(
    const TkSharedText *sharedTextPtr,
    const TkText *textPtr,		/* can be NULL */
    const TkTextSegment *segPtr)
{
    assert(segPtr);
    assert(TkBTreeHaveElidedSegments(sharedTextPtr));
    assert(SegmentIsElided(sharedTextPtr, segPtr, textPtr));

    return FindBranchSegment(sharedTextPtr, textPtr, segPtr,
	    textPtr ? textPtr->startMarker : sharedTextPtr->startMarker);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeFindEndOfElidedRange --
 *
 *	Given an elided segment, this function is searching for the
 *	last segment which is spanning the range containing the
 *	given segment. Normally this is a link segment, but in
 *	case of restricted peers it may be an end marker.
 *
 * Results:
 *	The return value is a corresponding link segment (or the end
 *	marker of this peer).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkTextSegment *
SearchLinkInLine(
    const TkSharedText *sharedTextPtr,
    const TkText *textPtr,		/* can be NULL */
    TkTextSegment *segPtr)
{
    TkTextSegment *endMarker = textPtr ? textPtr->endMarker : sharedTextPtr->endMarker;
    TkTextSection *sectionPtr = segPtr->sectionPtr;
    TkTextSection *endSectionPtr;

    assert(endMarker);

    /*
     * Note that a link is always at the start of a section.
     */

    if (segPtr->typePtr == &tkTextLinkType) {
	return segPtr;
    }

    endSectionPtr = endMarker->sectionPtr;

    if (sectionPtr == endSectionPtr) {
	return endMarker;
    }

    for (sectionPtr = sectionPtr->nextPtr; sectionPtr; sectionPtr = sectionPtr->nextPtr) {
	if (sectionPtr->segPtr->typePtr == &tkTextLinkType) {
	    return sectionPtr->segPtr;
	}
	if (sectionPtr == endSectionPtr) {
	    return endMarker;
	}
    }

    return NULL;
}

TkTextSegment *
TkBTreeFindEndOfElidedRange(
    const TkSharedText *sharedTextPtr,
    const TkText *textPtr,		/* can be NULL */
    const TkTextSegment *segPtr)
{
    TkTextSegment *branchPtr;
    TkTextSegment *linkPtr;

    assert(segPtr);
    assert(SegmentIsElided(sharedTextPtr, segPtr, textPtr));

    if (segPtr->sectionPtr->linePtr->numLinks > 0) {
	if ((linkPtr = SearchLinkInLine(sharedTextPtr, textPtr, (TkTextSegment *) segPtr))) {
	    return linkPtr;
	}
    }

    branchPtr = FindBranchSegment(sharedTextPtr, textPtr, segPtr, NULL);

    assert(branchPtr);
    assert(branchPtr->typePtr == &tkTextBranchType);

    linkPtr = branchPtr->body.branch.nextPtr;

    if (textPtr && textPtr->endMarker != sharedTextPtr->endMarker) {
	TkTextLine *lastLinePtr = textPtr->endMarker->sectionPtr->linePtr;
	TkTextLine *linePtr = linkPtr->sectionPtr->linePtr;
	int lineNo1, lineNo2;

	if (linePtr == lastLinePtr) {
	    return SearchLinkInLine(sharedTextPtr, textPtr, linePtr->segPtr);
	}

	lineNo1 = TkBTreeLinesTo(sharedTextPtr->tree, NULL, linkPtr->sectionPtr->linePtr, NULL);
	lineNo2 = TkBTreeLinesTo(sharedTextPtr->tree, NULL, lastLinePtr, NULL);

	if (lineNo1 > lineNo2) {
	    /* we've found a node after text end, so return text end */
	    return textPtr->endMarker;
	}
    }

    return linkPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeSize --
 *
 *	This function returns the byte size over all lines in given client.
 *	If the client is NULL then count over all lines in the B-Tree.
 *
 * Results:
 *	The return value is either the total number of bytes in given client,
 *	or the total number of bytes in the B-Tree if the client is NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned
TkBTreeSize(
    const TkTextBTree tree,	/* The B-tree. */
    const TkText *textPtr)	/* Relative to this client of the B-tree, can be NULL. */
{
    assert(tree);

    if (!textPtr) {
	return TkBTreeGetRoot(tree)->size - 1;
    }
    return TkBTreeCountSize(tree, textPtr, TkBTreeGetStartLine(textPtr), TkBTreeGetLastLine(textPtr));

}
/*
 *----------------------------------------------------------------------
 *
 * TkBTreeCountSize --
 *
 *	This function returns the byte size over all lines in given range.
 *
 * Results:
 *	The return value is the total number of bytes in given line range.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static unsigned
CountSize(
    const Node *nodePtr,
    unsigned lineNo,
    unsigned firstLineNo,
    unsigned lastLineNo)
{
    unsigned endLineNo = lineNo + nodePtr->numLines - 1;
    unsigned size;

    if (firstLineNo <= lineNo && endLineNo <= lastLineNo) {
	return nodePtr->size;
    }

    if (endLineNo < firstLineNo || lastLineNo < lineNo) {
	return 0;
    }

    size = 0;

    if (nodePtr->level == 0) {
	const TkTextLine *linePtr = nodePtr->linePtr;
	const TkTextLine *lastPtr = nodePtr->lastPtr->nextPtr;

	endLineNo = MIN(endLineNo, lastLineNo);

	for ( ; lineNo < firstLineNo; ++lineNo, linePtr = linePtr->nextPtr) {
	    assert(linePtr);
	}
	for ( ; lineNo <= endLineNo && linePtr != lastPtr; ++lineNo, linePtr = linePtr->nextPtr) {
	    size += linePtr->size;
	}
    } else {
	const Node *childPtr;

	for (childPtr = nodePtr->childPtr; childPtr; childPtr = childPtr->nextPtr) {
	    size += CountSize(childPtr, lineNo, firstLineNo, lastLineNo);
	    lineNo += childPtr->numLines;
	}
    }

    return size;
}

unsigned
TkBTreeCountSize(
    const TkTextBTree tree,
    const TkText *textPtr,	/* Relative to this client, can be NULL. */
    const TkTextLine *linePtr1,	/* Start counting at this line. */
    const TkTextLine *linePtr2)	/* Stop counting at this line (don't count this line). */
{
    const BTree *treePtr = (const BTree *) tree;
    unsigned numBytes;

    if (linePtr1 == linePtr2) {
	return 0;
    }

    assert(tree);
    assert(linePtr1);
    assert(linePtr2);
    assert(TkBTreeLinesTo(tree, NULL, linePtr1, NULL) <= TkBTreeLinesTo(tree, NULL, linePtr2, NULL));

    if (linePtr1 == treePtr->rootPtr->linePtr && linePtr2 == treePtr->rootPtr->lastPtr) {
	numBytes = treePtr->rootPtr->size - 1;
    } else {
	unsigned firstLineNo = TkBTreeLinesTo(tree, NULL, linePtr1, NULL);
	unsigned lastLineNo = TkBTreeLinesTo(tree, NULL, linePtr2, NULL) - 1;

	numBytes = CountSize(treePtr->rootPtr, 0, firstLineNo, lastLineNo);
    }

    if (textPtr) {
	const TkSharedText *sharedTextPtr = treePtr->sharedTextPtr;

	if (textPtr->startMarker != sharedTextPtr->startMarker) {
	    if (linePtr1 == textPtr->startMarker->sectionPtr->linePtr) {
		assert(TkTextSegToIndex(textPtr->startMarker) <= (int) numBytes);
		numBytes -= TkTextSegToIndex(textPtr->startMarker);
	    }
	}
	if (textPtr->endMarker != sharedTextPtr->endMarker) {
	    if (!SegIsAtStartOfLine(textPtr->endMarker)) {
		const TkTextLine *linePtr = textPtr->endMarker->sectionPtr->linePtr;
		assert(linePtr->size - TkTextSegToIndex(textPtr->endMarker) - 1 <= (int) numBytes);
		numBytes -= linePtr->size - TkTextSegToIndex(textPtr->endMarker) - 1;
	    }
	}
    }

    return numBytes;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeMoveForward --
 *
 *	Given an index for a text widget, this function creates a new index
 *	that points 'byteCount' bytes ahead of the source index.
 *
 * Results:
 *	'dstPtr' is modified to refer to the character 'byteCount' bytes after
 *	'srcPtr', or to the last character in the TkText if there aren't 'byteCount'
 *	bytes left.
 *
 *	In this latter case, the function returns 'false' to indicate that not all
 *	of 'byteCount' could be used.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkBTreeMoveForward(
    TkTextIndex *indexPtr,
    unsigned byteCount)
{
    TkTextLine *linePtr;
    const Node *nodePtr;
    const Node *parentPtr;
    int byteIndex;

    if (byteCount == 0) {
	return true;
    }

    byteIndex = byteCount + TkTextIndexGetByteIndex(indexPtr);
    linePtr = TkTextIndexGetLine(indexPtr);
    nodePtr = linePtr->parentPtr;
    parentPtr = nodePtr->parentPtr;

    if (linePtr != nodePtr->linePtr || !parentPtr || HasLeftNode(nodePtr)) {
	TkTextLine *lastPtr;

	/*
	 * At first, search for byte offset in current node.
	 */

	lastPtr = nodePtr->lastPtr->nextPtr;

	while (linePtr != lastPtr) {
	    if (byteIndex < linePtr->size) {
		TkTextIndexSetByteIndex2(indexPtr, linePtr, byteIndex);
		return TkTextIndexRestrictToEndRange(indexPtr) <= 0;
	    }
	    byteIndex -= linePtr->size;
	    if (!(linePtr = TkBTreeNextLine(indexPtr->textPtr, linePtr))) {
		TkTextIndexSetupToEndOfText(indexPtr, indexPtr->textPtr, indexPtr->tree);
		return false;
	    }
	}

	nodePtr = nodePtr->nextPtr;
    }

    /*
     * We couldn't find a line, so search inside B-Tree for next level-0
     * node which contains the byte offset.
     */

    while (parentPtr) {
	if (!nodePtr || (!HasLeftNode(nodePtr) && byteIndex >= (int) parentPtr->size)) {
	    nodePtr = parentPtr->nextPtr;
	    parentPtr = parentPtr->parentPtr;
	} else {
	    while (nodePtr) {
		if (byteIndex < (int) nodePtr->size) {
		    if (nodePtr->level > 0) {
			nodePtr = nodePtr->childPtr;
			continue;
		    }
		    /*
		     * We've found the right node, now search for the line.
		     */
		    linePtr = nodePtr->linePtr;
		    while (true) {
			if (byteIndex < linePtr->size) {
			    TkTextIndexSetByteIndex2(indexPtr, linePtr, byteIndex);
			    return TkTextIndexRestrictToEndRange(indexPtr) <= 0;
			}
			byteIndex -= linePtr->size;
			if (!(linePtr = TkBTreeNextLine(indexPtr->textPtr, linePtr))) {
			    TkTextIndexSetupToEndOfText(indexPtr, indexPtr->textPtr, indexPtr->tree);
			    return false;
			}
		    }
		}
		byteIndex -= nodePtr->size;
		nodePtr = nodePtr->nextPtr;
	    }
	}
    }

    TkTextIndexSetupToEndOfText(indexPtr, indexPtr->textPtr, indexPtr->tree);
    return false;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeMoveBackward --
 *
 *	Given an index for a text widget, this function creates a new index
 *	that points 'byteCount' bytes earlier of the source index.
 *
 * Results:
 *	'dstPtr' is modified to refer to the character 'byteCount' bytes before
 *	'srcPtr', or to the first character in the TkText if there aren't 'byteCount'
 *	bytes earlier.
 *
 *	In this latter case, the function returns true to indicate that not all
 *	of 'byteCount' could be used.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkBTreeMoveBackward(
    TkTextIndex *indexPtr,
    unsigned byteCount)
{
    const Node *nodeStack[MAX_CHILDREN];
    const Node *nodePtr;
    const Node *parentPtr;
    const Node *nPtr;
    TkTextLine *linePtr;
    unsigned idx;
    int byteIndex;

    if (byteCount == 0) {
	return true;
    }

    linePtr = TkTextIndexGetLine(indexPtr);
    nodePtr = linePtr->parentPtr;
    parentPtr = nodePtr->parentPtr;
    byteIndex = byteCount + (linePtr->size - TkTextIndexGetByteIndex(indexPtr));

    if (linePtr != nodePtr->lastPtr || !parentPtr || nodePtr->nextPtr) {
	TkTextLine *lastPtr;

	/*
	 * At first, search for byte offset in current node.
	 */

	lastPtr = nodePtr->linePtr->prevPtr;

	while (linePtr != lastPtr) {
	    if ((byteIndex -= linePtr->size) <= 0) {
		TkTextIndexSetByteIndex2(indexPtr, linePtr, -byteIndex);
		return TkTextIndexRestrictToStartRange(indexPtr) >= 0;
	    }
	    if (!(linePtr = TkBTreePrevLine(indexPtr->textPtr, linePtr))) {
		TkTextIndexSetupToStartOfText(indexPtr, indexPtr->textPtr, indexPtr->tree);
		return false;
	    }
	}
    } else {
	nodePtr = NULL;
    }

    /*
     * We couldn't find a line, so search inside B-Tree for next level-0
     * node which contains the byte offset.
     */

    for (nPtr = parentPtr->childPtr, idx = 0; nPtr != nodePtr; nPtr = nPtr->nextPtr) {
	nodeStack[idx++] = nPtr;
    }
    nodePtr = idx ? nodeStack[--idx] : NULL;

    while (parentPtr) {
	if (!nodePtr || (!nodePtr->nextPtr && byteIndex >= (int) parentPtr->size)) {
	    nodePtr = parentPtr;
	    if ((parentPtr = parentPtr->parentPtr)) {
		for (nPtr = parentPtr->childPtr, idx = 0; nPtr != nodePtr; nPtr = nPtr->nextPtr) {
		    nodeStack[idx++] = nPtr;
		}
		nodePtr = idx ? nodeStack[--idx] : NULL;
	    }
	} else {
	    while (nodePtr) {
		if (byteIndex < (int) nodePtr->size) {
		    if (nodePtr->level > 0) {
			parentPtr = nodePtr;
			idx = 0;
			for (nPtr = nodePtr->childPtr; nPtr; nPtr = nPtr->nextPtr) {
			    nodeStack[idx++] = nPtr;
			}
			nodePtr = idx ? nodeStack[--idx] : NULL;
			continue;
		    }
		    /*
		     * We've found the right node, now search for the line.
		     */
		    linePtr = nodePtr->lastPtr;
		    while (true) {
			if ((byteIndex -= linePtr->size) <= 0) {
			    TkTextIndexSetByteIndex2(indexPtr, linePtr, -byteIndex);
			    return TkTextIndexRestrictToStartRange(indexPtr) >= 0;
			}
			if (!(linePtr = TkBTreePrevLine(indexPtr->textPtr, linePtr))) {
			    TkTextIndexSetupToStartOfText(indexPtr, indexPtr->textPtr, indexPtr->tree);
			    return false;
			}
		    }
		}
		byteIndex -= nodePtr->size;
		nodePtr = idx ? nodeStack[--idx] : NULL;
	    }
	}
    }

    TkTextIndexSetupToStartOfText(indexPtr, indexPtr->textPtr, indexPtr->tree);
    return false;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeRootTagInfo --
 *
 *	This function returns the tag information of root node.
 *
 * Results:
 *	The tag information of root node.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const TkTextTagSet *
TkBTreeRootTagInfo(
    const TkTextBTree tree)
{
    return ((BTree *) tree)->rootPtr->tagonPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeLinesPerNode --
 *
 *	This function returns the minimal number of lines per node.
 *
 * Results:
 *	The minimal number of lines per node.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned
TkBTreeLinesPerNode(
    const TkTextBTree tree)
{
    return MIN_CHILDREN;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeChildNumber --
 *
 *	This function returns the number of level-0 node which contains the
 *	given line. If 'depth' is given then also the depth of this node
 *	will be returned (in 'depth').
 *
 * Results:
 *	The number of the child for given line, and also the depth of this
 *	child if 'depth' is given.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned
TkBTreeChildNumber(
    const TkTextBTree tree,
    const TkTextLine *linePtr,
    unsigned *depth)
{
    const Node *childPtr;
    const Node *nodePtr;
    unsigned number = 0;

    assert(linePtr);

    nodePtr = linePtr->parentPtr;
    childPtr = nodePtr->parentPtr->childPtr;

    for ( ; childPtr != nodePtr; childPtr = childPtr->nextPtr) {
	number += 1;
    }

    if (depth) {
	*depth = 0;

	while (nodePtr) {
	    nodePtr = nodePtr->parentPtr;
	    *depth += 1;
	}
    }

    return number;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeNumPixels --
 *
 *	This function returns a count of the number of pixels of text present
 *	in a given widget's B-tree representation.
 *
 * Results:
 *	The return value is a count of the number of usable pixels in tree
 *	(since the dummy line used to mark the end of the B-tree is maintained
 *	with zero height, as are any lines that are before or after the
 *	'-startindex -endindex' range of the text widget in question, the number
 *	stored at the root is the number we want).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned
TkBTreeNumPixels(
    const TkText *textPtr)	/* Relative to this client of the B-tree. */
{
    assert(textPtr);
    assert(textPtr->pixelReference != -1);

    return TkBTreeGetRoot(textPtr->sharedTextPtr->tree)->pixelInfo[textPtr->pixelReference].pixels;
}

/*
 *--------------------------------------------------------------
 *
 * CleanupSplitPoint --
 *
 *	This function merges adjacent character segments into a single
 *	character segment, if possible, and removes the protection flag.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Storage for the segments may be allocated and freed.
 *
 *--------------------------------------------------------------
 */

static void
CleanupSplitPoint(
    TkTextSegment *segPtr,
    TkSharedText *sharedTextPtr)
{
    if (!segPtr || !segPtr->protectionFlag) {
	return;
    }

    segPtr->protectionFlag = false;

    if (segPtr->typePtr == &tkTextCharType) {
	if (segPtr->prevPtr && segPtr->prevPtr->typePtr == &tkTextCharType) {
	    TkTextSegment *prevPtr = segPtr->prevPtr;
	    if ((segPtr = CleanupCharSegments(sharedTextPtr, prevPtr)) == prevPtr) {
		segPtr = segPtr->nextPtr;
	    }
	}
	if (segPtr->nextPtr && segPtr->nextPtr->typePtr == &tkTextCharType) {
	    CleanupCharSegments(sharedTextPtr, segPtr);
	}
    }
}

/*
 *--------------------------------------------------------------
 *
 * JoinCharSegments --
 *
 *	This function merges adjacent character segments into a single
 *	character segment.
 *
 *	This functions assumes that the successor of given segment is
 *	a joinable char segment.
 *
 * Results:
 *	The return value is a pointer to the first segment in the (new) list
 *	of segments that used to start with segPtr.
 *
 * Side effects:
 *	Storage for the segments may be allocated and freed.
 *
 *--------------------------------------------------------------
 */

static TkTextSegment *
JoinCharSegments(
    const TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    TkTextSegment *segPtr)		/* Pointer to first of two adjacent segments to join. */
{
    TkTextSegment *nextPtr, *newPtr;

    assert(segPtr);
    assert(segPtr->typePtr == &tkTextCharType);
    assert(!segPtr->protectionFlag);
    assert(segPtr->nextPtr);
    assert(!segPtr->nextPtr->protectionFlag);
    assert(segPtr->nextPtr->typePtr == &tkTextCharType);
    assert(TkTextTagSetIsEqual(segPtr->tagInfoPtr, segPtr->nextPtr->tagInfoPtr));

    nextPtr = segPtr->nextPtr;
    newPtr = CopyCharSeg(segPtr, 0, segPtr->size, segPtr->size + nextPtr->size);
    memcpy(newPtr->body.chars + segPtr->size, nextPtr->body.chars, nextPtr->size);
    newPtr->nextPtr = nextPtr->nextPtr;
    newPtr->prevPtr = segPtr->prevPtr;

    if (segPtr->prevPtr) {
	segPtr->prevPtr->nextPtr = newPtr;
    } else {
	segPtr->sectionPtr->linePtr->segPtr = newPtr;
    }
    if (nextPtr->nextPtr) {
	nextPtr->nextPtr->prevPtr = newPtr;
    }
    if (segPtr->sectionPtr->segPtr == segPtr) {
	segPtr->sectionPtr->segPtr = newPtr;
    }
    if (nextPtr->sectionPtr->segPtr == nextPtr) {
	nextPtr->sectionPtr->segPtr = nextPtr->nextPtr;
    }
    if (newPtr->sectionPtr->linePtr->lastPtr == nextPtr) {
	newPtr->sectionPtr->linePtr->lastPtr = newPtr;
    }
    nextPtr->sectionPtr->length -= 1;
    if (segPtr->sectionPtr != nextPtr->sectionPtr) {
	segPtr->sectionPtr->size += nextPtr->size;
	nextPtr->sectionPtr->size -= nextPtr->size;
	JoinSections(nextPtr->sectionPtr);
    }
    JoinSections(segPtr->sectionPtr);
    TkBTreeFreeSegment(segPtr);
    TkBTreeFreeSegment(nextPtr);

    return newPtr;
}

/*
 *--------------------------------------------------------------
 *
 * CleanupCharSegments --
 *
 *	This function merges adjacent character segments into a single
 *	character segment, if possible.
 *
 * Results:
 *	The return value is a pointer to the first segment in the (new) list
 *	of segments that used to start with segPtr.
 *
 * Side effects:
 *	Storage for the segments may be allocated and freed.
 *
 *--------------------------------------------------------------
 */

static TkTextSegment *
CleanupCharSegments(
    const TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    TkTextSegment *segPtr)		/* Pointer to first of two adjacent segments to join. */
{
    TkTextSegment *nextPtr;

    assert(segPtr);
    assert(segPtr->typePtr == &tkTextCharType);

    if (segPtr->protectionFlag) {
	return segPtr;
    }
    nextPtr = segPtr->nextPtr;
    if (!nextPtr
	    || nextPtr->protectionFlag
	    || nextPtr->typePtr != &tkTextCharType
	    || !TkTextTagSetIsEqual(segPtr->tagInfoPtr, nextPtr->tagInfoPtr)) {
	return segPtr;
    }
    return JoinCharSegments(sharedTextPtr, segPtr);
}

/*
 *--------------------------------------------------------------
 *
 * CharDeleteProc --
 *
 *	This function is invoked to delete a character segment.
 *
 * Results:
 *	Always returns true to indicate that the segment was
 *	(virtually) deleted.
 *
 * Side effects:
 *	Storage for the segment is freed.
 *
 *--------------------------------------------------------------
 */

static bool
CharDeleteProc(
    TkTextBTree tree,
    TkTextSegment *segPtr,	/* Segment to delete. */
    int flags)			/* Flags controlling the deletion. */
{
    TkBTreeFreeSegment(segPtr);
    return true;
}

/*
 *--------------------------------------------------------------
 *
 * CharInspectProc --
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
CharInspectProc(
    const TkSharedText *sharedTextPtr,
    const TkTextSegment *segPtr)
{
    Tcl_Obj *objPtr = Tcl_NewObj();

    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(segPtr->typePtr->name, -1));
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(segPtr->body.chars, segPtr->size));
    Tcl_ListObjAppendElement(NULL, objPtr, MakeTagInfoObj(sharedTextPtr, segPtr->tagInfoPtr));
    return objPtr;
}

/*
 *--------------------------------------------------------------
 *
 * CharCheckProc --
 *
 *	This function is invoked to perform consistency checks on character
 *	segments.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If the segment isn't inconsistent then the function panics.
 *
 *--------------------------------------------------------------
 */

static void
CharCheckProc(
    const TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    const TkTextSegment *segPtr)	/* Segment to check. */
{
    /*
     * Make sure that the segment contains the number of characters indicated
     * by its header, and that the last segment in a line ends in a newline.
     * Also make sure that there aren't ever two character segments with same
     * tagging adjacent to each other: they should be merged together.
     */

    if (segPtr->size <= 0) {
	Tcl_Panic("CharCheckProc: segment has size <= 0");
    }
    if (strlen(segPtr->body.chars) != (size_t) segPtr->size) {
	Tcl_Panic("CharCheckProc: segment has wrong size");
    }
    if (!segPtr->nextPtr) {
	if (segPtr->body.chars[segPtr->size - 1] != '\n') {
	    Tcl_Panic("CharCheckProc: line doesn't end with newline");
	}
    } else if (segPtr->nextPtr->typePtr == &tkTextCharType
	    && TkTextTagSetIsEqual(segPtr->tagInfoPtr, segPtr->nextPtr->tagInfoPtr)) {
	Tcl_Panic("CharCheckProc: adjacent character segments weren't merged");
    }
}

/*
 *--------------------------------------------------------------
 *
 * HyphenDeleteProc --
 *
 *	This function is invoked to delete hyphen segments.
 *
 * Results:
 *	Returns always true to indicate that the segment has been
 *	deleted (virtually).
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static bool
HyphenDeleteProc(
    TkTextBTree tree,
    TkTextSegment *segPtr,	/* Segment to check. */
    int flags)			/* Flags controlling the deletion. */
{
    TkBTreeFreeSegment(segPtr);
    return true;
}

/*
 *--------------------------------------------------------------
 *
 * HyphenInspectProc --
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
HyphenInspectProc(
    const TkSharedText *sharedTextPtr,
    const TkTextSegment *segPtr)
{
    Tcl_Obj *objPtr = Tcl_NewObj();

    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(segPtr->typePtr->name, -1));
    Tcl_ListObjAppendElement(NULL, objPtr, MakeTagInfoObj(sharedTextPtr, segPtr->tagInfoPtr));
    return objPtr;
}

/*
 *--------------------------------------------------------------
 *
 * HyphenCheckProc --
 *
 *	This function is invoked to perform consistency checks on hyphen
 *	segments.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If a consistency problem is found the function panics.
 *
 *--------------------------------------------------------------
 */

static void
HyphenCheckProc(
    const TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    const TkTextSegment *segPtr)	/* Segment to check. */
{
    if (segPtr->size != 1) {
	Tcl_Panic("HyphenCheckProc: hyphen has size %d", segPtr->size);
    }
}

/*
 *--------------------------------------------------------------
 *
 * BranchDeleteProc --
 *
 *	This function is invoked to delete branch segments.
 *
 * Results:
 *	Returns always true to indicate that the segment has been
 *	deleted (virtually).
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static bool
BranchDeleteProc(
    TkTextBTree tree,
    TkTextSegment *segPtr,	/* Segment to check. */
    int flags)			/* Flags controlling the deletion. */
{
    if (flags & TREE_GONE) {
	FREE_SEGMENT(segPtr);
	DEBUG_ALLOC(tkTextCountDestroySegment++);
	return true;
    }

    if (flags & DELETE_BRANCHES) {
	TkBTreeFreeSegment(segPtr);
	return true;
    }

    /* Save old relationships for undo (we misuse an unused pointer). */
    segPtr->tagInfoPtr = (TkTextTagSet *) segPtr->body.branch.nextPtr;
    return false;
}

/*
 *--------------------------------------------------------------
 *
 * BranchRestoreProc --
 *
 *	This function is called when a branch will be restored from the
 *	undo chain.
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
BranchRestoreProc(
    TkTextSegment *segPtr)	/* Segment to reuse. */
{
    /* Restore old relationship. */
    segPtr->body.branch.nextPtr = (TkTextSegment *) segPtr->tagInfoPtr;
    assert(segPtr->body.branch.nextPtr->typePtr == &tkTextLinkType);
    segPtr->tagInfoPtr = NULL;
}

/*
 *--------------------------------------------------------------
 *
 * BranchInspectProc --
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
BranchInspectProc(
    const TkSharedText *sharedTextPtr,
    const TkTextSegment *segPtr)
{
    Tcl_Obj *objPtr = Tcl_NewObj();
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(segPtr->typePtr->name, -1));
    return objPtr;
}

/*
 *--------------------------------------------------------------
 *
 * BranchCheckProc --
 *
 *	This function is invoked to perform consistency checks on branch
 *	segments.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If a consistency problem is found the function panics.
 *
 *--------------------------------------------------------------
 */

static void
BranchCheckProc(
    const TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    const TkTextSegment *segPtr)	/* Segment to check. */
{
    const TkTextSegment *prevPtr, *nextPtr;
    const TkTextLine *linePtr;

    if (segPtr->size != 0) {
	Tcl_Panic("BranchCheckProc: branch has size %d", segPtr->size);
    }
    if (!segPtr->nextPtr) {
	Tcl_Panic("BranchCheckProc: branch cannot be at end of line");
    }
    if (segPtr->sectionPtr->nextPtr
	    ? segPtr->sectionPtr->nextPtr->segPtr->prevPtr != segPtr
	    : !!segPtr->nextPtr) {
	Tcl_Panic("BranchCheckProc: branch is not at end of section");
    }
    if (!segPtr->body.branch.nextPtr) {
	Tcl_Panic("BranchCheckProc: missing fork");
    }
    if (segPtr->nextPtr == segPtr->body.branch.nextPtr) {
	Tcl_Panic("BranchCheckProc: bad fork");
    }
    if (!segPtr->body.branch.nextPtr->sectionPtr) {
	Tcl_Panic("BranchCheckProc: connection is not linked");
    }
    if (segPtr->nextPtr->typePtr->group == SEG_GROUP_MARK) {
	Tcl_Panic("BranchCheckProc: branch shouldn't be followed by marks");
    }

    assert(segPtr->body.branch.nextPtr);
    assert(segPtr->body.branch.nextPtr->typePtr);

    if (segPtr->body.branch.nextPtr->typePtr != &tkTextLinkType) {
	Tcl_Panic("BranchCheckProc: branch is not pointing to a link");
    }
    if (segPtr->body.branch.nextPtr->body.link.prevPtr != segPtr) {
	Tcl_Panic("BranchCheckProc: related link is not pointing to this branch");
    }
    if (segPtr->nextPtr->typePtr == &tkTextLinkType) {
	Tcl_Panic("BranchCheckProc: elided section is empty");
    }

    linePtr = segPtr->sectionPtr->linePtr;
    if (!(prevPtr = segPtr->prevPtr) && linePtr->prevPtr) {
	prevPtr = linePtr->prevPtr->lastPtr;
    }
    while (prevPtr && !prevPtr->tagInfoPtr) {
	if (prevPtr->typePtr->group == SEG_GROUP_BRANCH) {
	    Tcl_Panic("BranchCheckProc: invalid branch/link structure (%s before branch)",
		    prevPtr->typePtr->name);
	}
	if (!(prevPtr = prevPtr->prevPtr) && linePtr->prevPtr) {
	    prevPtr = linePtr->prevPtr->lastPtr;
	}
    }
    nextPtr = segPtr->nextPtr;
    while (nextPtr && !nextPtr->tagInfoPtr) {
	if (nextPtr->typePtr->group == SEG_GROUP_BRANCH) {
	    Tcl_Panic("BranchCheckProc: invalid branch/link structure (%s after branch)",
		    nextPtr->typePtr->name);
	}
	nextPtr = nextPtr->nextPtr;
    }

    if (prevPtr && SegmentIsElided(sharedTextPtr, prevPtr, NULL)) {
	Tcl_Panic("BranchCheckProc: branch not at start of elided range");
    }
    if (nextPtr && !SegmentIsElided(sharedTextPtr, nextPtr, NULL)) {
	Tcl_Panic("BranchCheckProc: misplaced branch");
    }
}

/*
 *--------------------------------------------------------------
 *
 * LinkDeleteProc --
 *
 *	This function is invoked to delete link segments.
 *
 * Results:
 *	Returns always 'true' to indicate that the segment has been
 *	deleted (virtually).
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static bool
LinkDeleteProc(
    TkTextBTree tree,
    TkTextSegment *segPtr,	/* Segment to check. */
    int flags)			/* Flags controlling the deletion. */
{
    if (flags & TREE_GONE) {
	FREE_SEGMENT(segPtr);
	DEBUG_ALLOC(tkTextCountDestroySegment++);
	return true;
    }

    if (flags & DELETE_BRANCHES) {
	TkBTreeFreeSegment(segPtr);
	return true;
    }

    /* Save old relationships for undo (we have misused an unused pointer). */
    segPtr->tagInfoPtr = (TkTextTagSet *) segPtr->body.link.prevPtr;
    return false;
}

/*
 *--------------------------------------------------------------
 *
 * LinkRestoreProc --
 *
 *	This function is called when a branch will be restored from the
 *	undo chain.
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
LinkRestoreProc(
    TkTextSegment *segPtr)	/* Segment to reuse. */
{
    /* Restore old relationship (misuse of an unused pointer). */
    segPtr->body.link.prevPtr = (TkTextSegment *) segPtr->tagInfoPtr;
    assert(segPtr->body.link.prevPtr->typePtr == &tkTextBranchType);
    segPtr->tagInfoPtr = NULL;
}

/*
 *--------------------------------------------------------------
 *
 * LinkInspectProc --
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
LinkInspectProc(
    const TkSharedText *sharedTextPtr,
    const TkTextSegment *segPtr)
{
    Tcl_Obj *objPtr = Tcl_NewObj();
    Tcl_ListObjAppendElement(NULL, objPtr, Tcl_NewStringObj(segPtr->typePtr->name, -1));
    return objPtr;
}

/*
 *--------------------------------------------------------------
 *
 * LinkCheckProc --
 *
 *	This function is invoked to perform consistency checks on link
 *	segments.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If a consistency problem is found the function panics.
 *
 *--------------------------------------------------------------
 */

static void
LinkCheckProc(
    const TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    const TkTextSegment *segPtr)	/* Segment to check. */
{
    const TkTextSegment *prevPtr, *nextPtr;
    const TkTextLine *linePtr;

    if (segPtr->size != 0) {
	Tcl_Panic("LinkCheckProc: link has size %d", segPtr->size);
    }
    if (segPtr->sectionPtr->segPtr != segPtr) {
	Tcl_Panic("LinkCheckProc: link is not at start of section");
    }
    if (!segPtr->body.link.prevPtr) {
	Tcl_Panic("LinkCheckProc: missing connection");
    }
    if (!segPtr->body.link.prevPtr->sectionPtr) {
	Tcl_Panic("LinkCheckProc: connection is not linked");
    }
    if (segPtr->prevPtr == segPtr->body.link.prevPtr) {
	Tcl_Panic("LinkCheckProc: bad link");
    }

    assert(segPtr->body.link.prevPtr);
    assert(segPtr->body.link.prevPtr->typePtr);

    if (segPtr->body.link.prevPtr->typePtr != &tkTextBranchType) {
	Tcl_Panic("LinkCheckProc: link is not pointing to a branch");
    }
    if (segPtr->body.link.prevPtr->body.branch.nextPtr != segPtr) {
	Tcl_Panic("LinkCheckProc: related branch is not pointing to this link");
    }
    if (segPtr->prevPtr && segPtr->prevPtr->typePtr->group == SEG_GROUP_MARK) {
	Tcl_Panic("LinkCheckProc: link shouldn't be preceded by marks");
    }

    linePtr = segPtr->sectionPtr->linePtr;
    if (!(prevPtr = segPtr->prevPtr) && linePtr->prevPtr) {
	prevPtr = linePtr->prevPtr->lastPtr;
    }
    while (prevPtr && !prevPtr->tagInfoPtr) {
	if (prevPtr->typePtr->group == SEG_GROUP_BRANCH) {
	    Tcl_Panic("LinkCheckProc: invalid branch/link structure (%s after link)",
		    prevPtr->typePtr->name);
	}
	if (!(prevPtr = prevPtr->prevPtr) && linePtr->prevPtr) {
	    prevPtr = linePtr->prevPtr->lastPtr;
	}
    }
    nextPtr = segPtr->nextPtr;
    while (nextPtr && !nextPtr->tagInfoPtr) {
	if (nextPtr->typePtr->group == SEG_GROUP_BRANCH) {
	    Tcl_Panic("LinkCheckProc: invalid branch/link structure (%s after link)",
		    nextPtr->typePtr->name);
	}
	nextPtr = nextPtr->nextPtr;
    }

    if (prevPtr && !SegmentIsElided(sharedTextPtr, prevPtr, NULL)) {
	Tcl_Panic("LinkCheckProc: misplaced link");
    }
    if (nextPtr && SegmentIsElided(sharedTextPtr, nextPtr, NULL)) {
	Tcl_Panic("LinkCheckProc: link is not at end of elided range");
    }
}

/*
 *--------------------------------------------------------------
 *
 * ProtectionMarkCheckProc --
 *
 *	This function is invoked to perform consistency checks on the
 *	protection mark.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The function panics because the deletion markers are only
 *	temporary.
 *
 *--------------------------------------------------------------
 */

static void
ProtectionMarkCheckProc(
    const TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    const TkTextSegment *segPtr)	/* Segment to check. */
{
    Tcl_Panic("ProtectionMarkCheckProc: protection mark detected");
}

/*
 *--------------------------------------------------------------
 *
 * ProtectionMarkDeleteProc --
 *
 *	This function is invoked to delete the protection markers.
 *
 * Results:
 *	Returns 'true' to indicate that the segment is (virtually) deleted.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static bool
ProtectionMarkDeleteProc(
    TkTextBTree tree,
    TkTextSegment *segPtr,	/* Segment to check. */
    int flags)			/* Flags controlling the deletion. */
{
    return true;
}

/*
 *--------------------------------------------------------------
 *
 * CheckSegmentItems --
 *
 *	This function is invoked to perform consistency checks on the
 *	segment items.
 *
 * Results:
 *	Returns always true for successful, because in case of an detected
 *	error the panic function will be called.
 *
 * Side effects:
 *	If a consistency problem is found the function panics.
 *
 *--------------------------------------------------------------
 */

static bool
CheckSegmentItems(
    const TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    const TkTextLine *linePtr)	/* Pointer to line in B-tree */
{
    const TkTextSegment *segPtr;

    for (segPtr = linePtr->segPtr; segPtr; segPtr = segPtr->nextPtr) {
	if (segPtr->typePtr->checkProc) {
	    segPtr->typePtr->checkProc(sharedTextPtr, segPtr);
	}
    }

    return true;
}

/*
 *--------------------------------------------------------------
 *
 * CheckSegments --
 *
 *	This function is invoked to perform consistency checks on the
 *	chain of segments.
 *
 * Results:
 *	Returns always true for successful, because in case of an detected
 *	error the panic function will be called.
 *
 * Side effects:
 *	If a consistency problem is found the function panics.
 *
 *--------------------------------------------------------------
 */

static bool
CheckSegments(
    const TkSharedText *sharedTextPtr,	/* Handle to shared text resource. */
    const TkTextLine *linePtr)		/* Pointer to line in B-tree */
{
    const TkTextSegment *segPtr;
    TkTextTagSet *tagonPtr;
    TkTextTagSet *tagoffPtr;
    unsigned count = 0;
    unsigned numBranches = 0;
    unsigned numLinks = 0;
    bool startsWithBranch = false;
    bool startsWithLink = false;
    bool endsWithBranch = false;
    bool endsWithLink = false;

    TkTextTagSetIncrRefCount(tagonPtr = sharedTextPtr->emptyTagInfoPtr);
    TkTextTagSetIncrRefCount(tagoffPtr = linePtr->tagonPtr);

    if (!linePtr->segPtr) {
	Tcl_Panic("CheckSegments: line has no segments");
    }
    if (linePtr->segPtr->prevPtr) {
	Tcl_Panic("CheckSegments: first segment has predecessor");
    }

    for (segPtr = linePtr->segPtr; segPtr; segPtr = segPtr->nextPtr) {
	if (!segPtr->typePtr) {
	    Tcl_Panic("CheckSegments: segment has null type");
	}
	if (segPtr->refCount <= 0) {
	    Tcl_Panic("CheckSegments: reference count <= 0");
	}
	if (segPtr->protectionFlag) {
	    Tcl_Panic("CheckSegments: segment is protected");
	}
	if (segPtr != linePtr->segPtr && !segPtr->prevPtr) {
	    Tcl_Panic("CheckSegments: missing predecessor in segment");
	}
	if (segPtr->nextPtr && segPtr->nextPtr->prevPtr != segPtr) {
	    Tcl_Panic("CheckSegments: wrong successor in segment");
	}
	if (segPtr->prevPtr ? segPtr->prevPtr->nextPtr != segPtr
		: segPtr != linePtr->segPtr) {
	    Tcl_Panic("CheckSegments: wrong predecessor in segment");
	}
	if (segPtr->typePtr->group != SEG_GROUP_MARK) {
	    if (segPtr->normalMarkFlag
		    || segPtr->privateMarkFlag
		    || segPtr->currentMarkFlag
		    || segPtr->insertMarkFlag
		    || segPtr->startEndMarkFlag) {
		Tcl_Panic("CheckSegments: wrong mark flag in segment");
	    }
	}
	if (!sharedTextPtr->steadyMarks
	    	&& segPtr->typePtr->gravity == GRAVITY_RIGHT
		&& segPtr->nextPtr
		&& segPtr->nextPtr->typePtr->gravity == GRAVITY_LEFT) {
	    if (segPtr->typePtr == &tkTextBranchType && segPtr->nextPtr->typePtr == &tkTextLinkType) {
		Tcl_Panic("CheckSegments: empty branch");
	    } else {
		Tcl_Panic("CheckSegments: wrong segment order for gravity");
	    }
	}
	if (!segPtr->nextPtr && segPtr->typePtr != &tkTextCharType) {
	    Tcl_Panic("CheckSegments: line ended with wrong type");
	}
	if (!segPtr->sectionPtr) {
	    Tcl_Panic("CheckSegments: segment has no section");
	}
	if (segPtr->size > 0) {
	    if (!segPtr->tagInfoPtr) {
		Tcl_Panic("CheckSegments: segment '%s' has no tag information", segPtr->typePtr->name);
	    }
	    if (TkTextTagSetIsEmpty(segPtr->tagInfoPtr)
		    && segPtr->tagInfoPtr != sharedTextPtr->emptyTagInfoPtr) {
		Tcl_Panic("CheckSegments: should use shared resource if tag info is empty");
	    }
	    if (TkTextTagSetRefCount(segPtr->tagInfoPtr) == 0) {
		Tcl_Panic("CheckSegments: unreferenced tag info");
	    }
	    if (TkTextTagSetRefCount(segPtr->tagInfoPtr) > 0x3fffffff) {
		Tcl_Panic("CheckSegments: negative reference count in tag info");
	    }
	    tagonPtr = TkTextTagSetJoin(tagonPtr, segPtr->tagInfoPtr);
	    tagoffPtr = TkTextTagSetIntersect(tagoffPtr, segPtr->tagInfoPtr);
	} else if (segPtr->tagInfoPtr) {
	    Tcl_Panic("CheckSegments: segment '%s' should not have tag information",
		    segPtr->typePtr->name);
	}
	if (segPtr->sectionPtr->linePtr != linePtr) {
	    Tcl_Panic("CheckSegments: segment has wrong line pointer");
	}
	if (!segPtr->nextPtr && linePtr->lastPtr != segPtr) {
	    Tcl_Panic("CheckSegments: wrong pointer to last segment");
	}
	if (segPtr->typePtr == &tkTextBranchType) {
	    numBranches += 1;
	    if (numLinks == 0) {
		startsWithBranch = true;
	    }
	    endsWithBranch = true;
	    endsWithLink = false;
	} else if (segPtr->typePtr == &tkTextLinkType) {
	    numLinks += 1;
	    if (numBranches == 0) {
		startsWithLink = true;
	    }
	    endsWithBranch = false;
	    endsWithLink = true;
	}
	if (++count > 100000) {
	   Tcl_Panic("CheckSegments: infinite chain of segments");
	}
    }

    tagoffPtr = TagSetComplementTo(tagoffPtr, tagonPtr, sharedTextPtr);

    if (!TkTextTagSetIsEqual(linePtr->tagonPtr, tagonPtr)) {
	Tcl_Panic("CheckSegments: line tagon information is wrong");
    }
    if (!TkTextTagSetIsEqual(linePtr->tagoffPtr, tagoffPtr)) {
	Tcl_Panic("CheckSegments: line tagoff information is wrong");
    }
    if (numBranches != linePtr->numBranches) {
	Tcl_Panic("CheckSegments: wrong branch count %u (expected is %u)",
		numBranches, linePtr->numBranches);
    }
    if (numLinks != linePtr->numLinks) {
	Tcl_Panic("CheckSegments: wrong link count %u (expected is %u)",
		numLinks, linePtr->numLinks);
    }
    if (startsWithLink && linePtr->logicalLine) {
	Tcl_Panic("CheckSegments: this line cannot be a logical line");
    }
    if (startsWithBranch && !linePtr->logicalLine) {
	Tcl_Panic("CheckSegments: this line must be a logical line");
    }
    if (linePtr->nextPtr) {
	if (endsWithBranch && linePtr->nextPtr->logicalLine) {
	    Tcl_Panic("CheckSegments: next line cannot be a logical line");
	}
	if (linePtr->logicalLine
		&& !linePtr->nextPtr->logicalLine
		&& (numBranches == 0 || endsWithLink)) {
	    Tcl_Panic("CheckSegments: next line must be a logical line");
	}
    }

    TkTextTagSetDecrRefCount(tagonPtr);
    TkTextTagSetDecrRefCount(tagoffPtr);
    return true;
}

/*
 *--------------------------------------------------------------
 *
 * CheckSections --
 *
 *	This function is invoked to perform consistency checks on the
 *	chain of sections.
 *
 * Results:
 *	Returns always true for successful, because in case of an detected
 *	error the panic function will be called.
 *
 * Side effects:
 *	If a consistency problem is found the function panics.
 *
 *--------------------------------------------------------------
 */

static bool
CheckSections(
    const TkTextLine *linePtr)	/* Pointer to line with sections. */
{
    const TkTextSection *sectionPtr = linePtr->segPtr->sectionPtr;
    const TkTextSegment *segPtr;
    unsigned numSegs, length, count;
    int size, lineSize = 0;

    if (!sectionPtr) {
	Tcl_Panic("CheckSections: segment has no section");
    }
    if (linePtr->segPtr->sectionPtr->segPtr != linePtr->segPtr) {
	Tcl_Panic("CheckSections: first segment has wrong section pointer");
    }
    if (linePtr->segPtr->sectionPtr->prevPtr) {
	Tcl_Panic("CheckSections: first section has predecessor");
    }

    for ( ; sectionPtr; sectionPtr = sectionPtr->nextPtr) {
	segPtr = sectionPtr->segPtr;
	if (!sectionPtr->linePtr) {
	    Tcl_Panic("CheckSections: section has no line pointer");
	}
	if (sectionPtr->prevPtr
		? sectionPtr->prevPtr->nextPtr != sectionPtr
		: sectionPtr->prevPtr != NULL) {
	    Tcl_Panic("CheckSections: wrong predecessor in section");
	}
	if (sectionPtr->nextPtr && sectionPtr->nextPtr->prevPtr != sectionPtr) {
	    Tcl_Panic("CheckSegments: wrong successor in segment");
	}
	numSegs = 0;
	size = 0;
	length = 0;
	count = 0;
	for ( ; segPtr && segPtr->sectionPtr == sectionPtr; segPtr = segPtr->nextPtr, numSegs++) {
	    size += segPtr->size;
	    length += 1;
	    if (++count > 4*MAX_TEXT_SEGS) {
	       Tcl_Panic("CheckSections: infinite chain of segments");
	    }
	}
	if (!sectionPtr->nextPtr && segPtr) {
	    Tcl_Panic("CheckSections: missing successor in section");
	}
	if (sectionPtr->nextPtr && sectionPtr->nextPtr->segPtr != segPtr) {
	    Tcl_Panic("CheckSections: wrong predecessor in section");
	}
	if (sectionPtr->length != length) {
	    Tcl_Panic("CheckSections: wrong segment count %d in section (expected is %d)",
		    sectionPtr->length, length);
	}
	if (sectionPtr->size != size) {
	    Tcl_Panic("CheckSections: wrong size %d in section (expected is %d)",
		    sectionPtr->size, size);
	}
	if (sectionPtr->linePtr != linePtr) {
	    Tcl_Panic("CheckSections: section has wrong line pointer");
	}
	if (numSegs < MIN_TEXT_SEGS
		&& sectionPtr->nextPtr
		&& (!sectionPtr->nextPtr
		    || sectionPtr->nextPtr->segPtr->prevPtr->typePtr != &tkTextBranchType
		    || (sectionPtr->prevPtr && sectionPtr->segPtr->typePtr != &tkTextLinkType))
		&& (!sectionPtr->nextPtr
		    || sectionPtr->nextPtr->segPtr->typePtr != &tkTextLinkType
		    || (sectionPtr->prevPtr
			&& sectionPtr->segPtr->prevPtr->typePtr != &tkTextBranchType))) {
	    Tcl_Panic("CheckSections: too few segments in section");
	}
	if (numSegs > MAX_TEXT_SEGS) {
	    Tcl_Panic("CheckSections: too many segments in section");
	}
	lineSize += sectionPtr->size;
    }

    if (linePtr->size != lineSize) {
	Tcl_Panic("CheckSections: wrong size in line");
    }

    return true;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 105
 * End:
 * vi:set ts=8 sw=4:
 */
