/*
 * tkTextIndex.c --
 *
 *	This module provides functions that manipulate indices for text widgets.
 *
 * Copyright (c) 1992-1994 The Regents of the University of California.
 * Copyright (c) 1994-1997 Sun Microsystems, Inc.
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "default.h"
#include "tkInt.h"
#include "tkText.h"
#include "tkAlloc.h"
#include <stdlib.h>
#include <assert.h>

#ifndef MAX
# define MAX(a,b) (((int) a) < ((int) b) ? b : a)
#endif
#ifndef MIN
# define MIN(a,b) (((int) a) < ((int) b) ? a : b)
#endif

#ifdef NDEBUG
# define DEBUG(expr)
#else
# define DEBUG(expr) expr
#endif

/*
 * Modifiers for index parsing: 'display', 'any' or nothing.
 */

enum { TKINDEX_NONE, TKINDEX_DISPLAY, TKINDEX_CHAR };

/*
 * Forward declarations for functions defined later in this file:
 */

static const char *	ForwBack(TkText *textPtr, const char *string, TkTextIndex *indexPtr);
static const char *	StartEnd(TkText *textPtr, const char *string, TkTextIndex *indexPtr);
static bool		GetIndex(Tcl_Interp *interp, TkSharedText *sharedTextPtr, TkText *textPtr,
			    const char *string, TkTextIndex *indexPtr);
static TkTextSegment *	IndexToSeg(const TkTextIndex *indexPtr, int *offsetPtr);
static int		SegToIndex(const TkTextLine *linePtr, const TkTextSegment *segPtr);

/*
 * A note about sizeof(char). Due to the specification of sizeof in C99,
 * sizeof(char) is always 1, see section 6.5.3.4:
 *
 *	When applied to an operand that has type char, unsigned char, or
 *	signed char, (or a qualified version thereof) the result is 1.
 *
 * This means that the expression "== sizeof(char)" is not required, the
 * expression "== 1" is good as well.
 */

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexIsEmpty --
 *
 *	Return whether the given index is empty (still unset, or invalid).
 *
 * Results:
 *	True if empty, false otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextIndexIsEmpty(
    const TkTextIndex *indexPtr)
{
    assert(indexPtr);
    return indexPtr->priv.byteIndex == -1 && !indexPtr->priv.segPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextGetIndexFromObj --
 *
 *	Create new text index from given position.
 *
 * Results:
 *	Returns true if and only if the index could be created.
 *
 * Side effects:
 *	Store the new text index in 'indexPtr'.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextGetIndexFromObj(
    Tcl_Interp *interp,		/* Use this for error reporting. */
    TkText *textPtr,		/* Information about text widget, can be NULL. */
    Tcl_Obj *objPtr,		/* Object containing description of position. */
    TkTextIndex *indexPtr)	/* Store the result here. */
{
    assert(textPtr);
    return GetIndex(interp, textPtr->sharedTextPtr, textPtr, Tcl_GetString(objPtr), indexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetLine --
 *
 *	Set the line pointer of this index.
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
static bool
CheckLine(
    const TkTextIndex *indexPtr,
    const TkTextLine *linePtr)
{
    assert(linePtr);

    if (indexPtr->stateEpoch == TkBTreeEpoch(indexPtr->tree)) {
	if (indexPtr->priv.segPtr
		&& indexPtr->priv.segPtr->sectionPtr->linePtr != indexPtr->priv.linePtr) {
	    return false;
	}
	if (indexPtr->priv.lineNo != -1
		&& indexPtr->priv.lineNo !=
		(int) TkBTreeLinesTo(indexPtr->tree, NULL, indexPtr->priv.linePtr, NULL)) {
	    return false;
	}
	if (indexPtr->priv.lineNoRel != -1
		&& indexPtr->priv.lineNoRel !=
		(int) TkBTreeLinesTo(indexPtr->tree, indexPtr->textPtr, indexPtr->priv.linePtr, NULL)) {
	    return false;
	}
    }

    if (!indexPtr->discardConsistencyCheck && indexPtr->textPtr) {
	const TkTextLine *startLine = TkBTreeGetStartLine(indexPtr->textPtr);
	const TkTextLine *endLine = TkBTreeGetLastLine(indexPtr->textPtr);
	int lineNo = TkBTreeLinesTo(indexPtr->tree, NULL, linePtr, NULL);

	if (lineNo < (int) TkBTreeLinesTo(indexPtr->tree, NULL, startLine, NULL)) {
	    return false;
	}
	if (lineNo > (int) TkBTreeLinesTo(indexPtr->tree, NULL, endLine, NULL)) {
	    return false;
	}
    }

    return true;
}
#endif /* NDEBUG */

static int
FindStartByteIndex(
    const TkTextIndex *indexPtr)
{
    const TkText *textPtr = indexPtr->textPtr;
    const TkTextSegment *segPtr;
    const TkTextSection *sectionPtr;
    int byteIndex;

    if (!textPtr) {
	return 0;
    }

    if (textPtr->startMarker == TkBTreeGetShared(indexPtr->tree)->startMarker) {
	return 0;
    }

    segPtr = textPtr->startMarker;
    sectionPtr = segPtr->sectionPtr;
    byteIndex = 0;

    if (sectionPtr->linePtr == indexPtr->priv.linePtr) {
	while (segPtr && sectionPtr == segPtr->sectionPtr) {
	    byteIndex += segPtr->size;
	    segPtr = segPtr->prevPtr;
	}
	while (sectionPtr->prevPtr) {
	    sectionPtr = sectionPtr->prevPtr;
	    byteIndex += sectionPtr->size;
	}
    }

    return byteIndex;
}

static bool
DontNeedSpecialStartLineTreatment(
    const TkTextIndex *indexPtr)
{
    const TkText *textPtr = indexPtr->textPtr;

    return !textPtr
	    || textPtr->startMarker == TkBTreeGetShared(indexPtr->tree)->startMarker
	    || indexPtr->priv.linePtr != textPtr->startMarker->sectionPtr->linePtr;
}

void
TkTextIndexSetLine(
    TkTextIndex *indexPtr,
    TkTextLine *linePtr)
{
    assert(linePtr);
    assert(indexPtr->tree);
    assert(CheckLine(indexPtr, linePtr));

    indexPtr->stateEpoch = TkBTreeEpoch(indexPtr->tree);
    indexPtr->priv.lineNo = -1;
    indexPtr->priv.lineNoRel = -1;
    indexPtr->priv.segPtr = NULL;
    indexPtr->priv.byteIndex = -1;

    if ((indexPtr->priv.linePtr = linePtr)) {
	assert(linePtr->parentPtr); /* expired? */

	if (DontNeedSpecialStartLineTreatment(indexPtr)) {
	    indexPtr->priv.byteIndex = 0;
	} else {
	    indexPtr->priv.segPtr = indexPtr->textPtr->startMarker;
	    indexPtr->priv.isCharSegment = false;
	    indexPtr->priv.byteIndex = FindStartByteIndex(indexPtr);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetPosition --
 *
 *	Set the byte index and the segment, the user is responsible
 *	for proper values.
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
static bool
CheckByteIndex(
    const TkTextIndex *indexPtr,
    const TkTextLine *linePtr,
    int byteIndex)
{
    const TkText *textPtr = indexPtr->textPtr;

    if (byteIndex == -1 && (byteIndex = indexPtr->priv.byteIndex) == -1) {
	assert(indexPtr->priv.segPtr);
	assert(!indexPtr->priv.isCharSegment || TkBTreeEpoch(indexPtr->tree) == indexPtr->stateEpoch);
	byteIndex = SegToIndex(indexPtr->priv.linePtr, indexPtr->priv.segPtr);
    }

    if (!indexPtr->discardConsistencyCheck && textPtr) {
	if (linePtr == textPtr->startMarker->sectionPtr->linePtr) {
	    if (byteIndex < FindStartByteIndex(indexPtr)) {
		return false;
	    }
	}
	if (linePtr == textPtr->endMarker->sectionPtr->linePtr) {
	    return byteIndex <= SegToIndex(linePtr, textPtr->endMarker);
	}
	if (linePtr == textPtr->endMarker->sectionPtr->linePtr->nextPtr) {
	    return byteIndex == 0;
	}
    }

    return byteIndex < linePtr->size;
}
#endif /* NDEBUG */

void
TkTextIndexSetPosition(
    TkTextIndex *indexPtr,	/* Pointer to index. */
    int byteIndex,		/* New byte index. */
    TkTextSegment *segPtr)	/* The segment which belongs to the byte index. */
{
    assert(indexPtr->tree);
    assert(byteIndex >= 0);
    assert(segPtr);
    assert(segPtr->typePtr);    /* expired? */
    assert(segPtr->sectionPtr); /* linked? */
    assert(CheckLine(indexPtr, segPtr->sectionPtr->linePtr));
    assert(CheckByteIndex(indexPtr, segPtr->sectionPtr->linePtr, byteIndex));

    indexPtr->stateEpoch = TkBTreeEpoch(indexPtr->tree);
    indexPtr->priv.linePtr = segPtr->sectionPtr->linePtr;
    indexPtr->priv.byteIndex = byteIndex;
    indexPtr->priv.lineNo = -1;
    indexPtr->priv.lineNoRel = -1;
    indexPtr->priv.segPtr = segPtr;
    indexPtr->priv.isCharSegment = segPtr->typePtr == &tkTextCharType;

#ifndef NDEBUG
    {
	int pos = SegToIndex(indexPtr->priv.linePtr, segPtr);

	if (segPtr->typePtr == &tkTextCharType) {
	    assert(byteIndex - pos < segPtr->size);
	} else {
	    assert(pos == byteIndex);
	}
    }
#endif /* NDEBUG */
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetByteIndex --
 *
 *	Set the byte index. We allow to set to the start of the next
 *	line (this means that argument byteIndex is equal to line size),
 *	required that the next line exists.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static bool
DontNeedSpecialEndLineTreatment(
    const TkTextIndex *indexPtr)
{
    const TkText *textPtr = indexPtr->textPtr;

    return !textPtr
	    || textPtr->endMarker == TkBTreeGetShared(indexPtr->tree)->endMarker
	    || indexPtr->priv.linePtr != textPtr->endMarker->sectionPtr->linePtr;
}

static int
FindEndByteIndex(
    const TkTextIndex *indexPtr)
{
    /*
     * We do not handle the special case with last line, because CheckLine is testing this.
     */

    if (indexPtr->textPtr && indexPtr->priv.linePtr == TkBTreeGetLastLine(indexPtr->textPtr)) {
	return 0;
    }
    if (DontNeedSpecialEndLineTreatment(indexPtr)) {
	return indexPtr->priv.linePtr->size - 1;
    }
    return SegToIndex(indexPtr->priv.linePtr, indexPtr->textPtr->endMarker);
}

void
TkTextIndexSetByteIndex(
    TkTextIndex *indexPtr,	/* Pointer to index. */
    int byteIndex)		/* New byte index. */
{
    assert(indexPtr->tree);
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */
    assert(byteIndex >= 0);

    if (byteIndex == FindEndByteIndex(indexPtr) + 1) {
	assert(indexPtr->priv.linePtr->nextPtr);
	indexPtr->priv.linePtr = indexPtr->priv.linePtr->nextPtr;
	indexPtr->priv.byteIndex = 0;
	indexPtr->priv.segPtr = NULL;
	if (indexPtr->priv.lineNo >= 0) {
	    indexPtr->priv.lineNo += 1;
	}
	if (indexPtr->priv.lineNoRel >= 0) {
	    indexPtr->priv.lineNoRel += 1;
	}
    } else {
	indexPtr->priv.byteIndex = byteIndex;
	indexPtr->priv.segPtr = NULL;
    }

    assert(CheckLine(indexPtr, indexPtr->priv.linePtr));
    assert(CheckByteIndex(indexPtr, indexPtr->priv.linePtr, byteIndex));
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetByteIndex2 --
 *
 *	Set the new line pointer and the byte index. We allow to set to
 *	the start of the next line (this means that argument byteIndex
 *	is equal to line size), required that the next line exists.
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
TkTextIndexSetByteIndex2(
    TkTextIndex *indexPtr,	/* Pointer to index. */
    TkTextLine *linePtr,	/* Pointer to line. */
    int byteIndex)		/* New byte index. */
{
    assert(indexPtr->tree);
    assert(linePtr);
    assert(linePtr->parentPtr); /* expired? */
    assert(byteIndex >= 0);

    if (indexPtr->priv.linePtr != linePtr) {
	indexPtr->priv.linePtr = linePtr;
	indexPtr->priv.lineNo = -1;
	indexPtr->priv.lineNoRel = -1;
    }
    TkTextIndexSetByteIndex(indexPtr, byteIndex);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetSegment --
 *
 *	Set the segment pointer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The byte index will be cleared.
 *
 *----------------------------------------------------------------------
 */

void
TkTextIndexSetSegment(
    TkTextIndex *indexPtr,	/* Pointer to index. */
    TkTextSegment *segPtr)	/* Pointer to segment. */
{
    assert(indexPtr->tree);
    assert(segPtr);
    assert(segPtr->typePtr);    /* expired? */
    assert(segPtr->sectionPtr); /* linked? */
    assert(CheckLine(indexPtr, segPtr->sectionPtr->linePtr));

    indexPtr->stateEpoch = TkBTreeEpoch(indexPtr->tree);
    indexPtr->priv.linePtr = segPtr->sectionPtr->linePtr;
    indexPtr->priv.lineNo = -1;
    indexPtr->priv.lineNoRel = -1;
    indexPtr->priv.segPtr = segPtr;

    if (segPtr->typePtr == &tkTextCharType) {
	indexPtr->priv.byteIndex = SegToIndex(indexPtr->priv.linePtr, segPtr);
	indexPtr->priv.isCharSegment = true;
    } else {
	indexPtr->priv.byteIndex = -1;
	indexPtr->priv.isCharSegment = false;
    }

    assert(CheckByteIndex(indexPtr, segPtr->sectionPtr->linePtr, -1));
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetToStartOfLine --
 *
 *	Set this index to the start of the line.
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
TkTextIndexSetToStartOfLine(
    TkTextIndex *indexPtr)	/* Pointer to index. */
{
    assert(indexPtr->tree);
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */
    assert(CheckLine(indexPtr, indexPtr->priv.linePtr));

    indexPtr->stateEpoch = TkBTreeEpoch(indexPtr->tree);
    indexPtr->priv.segPtr = NULL;
    indexPtr->priv.byteIndex = FindStartByteIndex(indexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetToStartOfLine2 --
 *
 *	Set the new line pointer, and set this index to the start of the line.
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
TkTextIndexSetToStartOfLine2(
    TkTextIndex *indexPtr,	/* Pointer to index. */
    TkTextLine *linePtr)	/* Pointer to line. */
{
    assert(indexPtr->tree);
    assert(linePtr);
    assert(linePtr->parentPtr); /* expired? */
    assert(CheckLine(indexPtr, linePtr));

    indexPtr->stateEpoch = TkBTreeEpoch(indexPtr->tree);
    indexPtr->priv.linePtr = linePtr;
    indexPtr->priv.segPtr = NULL;
    indexPtr->priv.lineNo = -1;
    indexPtr->priv.lineNoRel = -1;
    indexPtr->priv.byteIndex = FindStartByteIndex(indexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetToEndOfLine2 --
 *
 *	Set the new line pointer, and set this index to the end of the line.
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
TkTextIndexSetToEndOfLine2(
    TkTextIndex *indexPtr,	/* Pointer to index. */
    TkTextLine *linePtr)	/* Pointer to line. */
{
    assert(indexPtr->tree);
    assert(linePtr);
    assert(linePtr->parentPtr); /* expired? */
    assert(linePtr->nextPtr);
    assert(CheckLine(indexPtr, linePtr->nextPtr));

    indexPtr->stateEpoch = TkBTreeEpoch(indexPtr->tree);
    indexPtr->priv.segPtr = NULL;
    indexPtr->priv.lineNo = -1;
    indexPtr->priv.lineNoRel = -1;
    indexPtr->priv.linePtr = linePtr->nextPtr;
    indexPtr->priv.byteIndex = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetToLastChar --
 *
 *	Set this index to one byte before the end of the line.
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
TkTextIndexSetToLastChar(
    TkTextIndex *indexPtr)	/* Pointer to index. */
{
    assert(indexPtr->tree);
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */

    indexPtr->stateEpoch = TkBTreeEpoch(indexPtr->tree);
    indexPtr->priv.byteIndex = FindEndByteIndex(indexPtr);
    indexPtr->priv.segPtr = NULL;

    assert(CheckLine(indexPtr, indexPtr->priv.linePtr));
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetupToStartOfText --
 *
 *	Setup this index to the start of the text.
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
TkTextIndexSetupToStartOfText(
    TkTextIndex *indexPtr,	/* Pointer to index. */
    TkText *textPtr,		/* Text widget for this index, can be NULL. */
    TkTextBTree tree)		/* B-tree for this index. */
{
    assert(indexPtr);
    assert(tree);

    indexPtr->textPtr = textPtr;
    indexPtr->tree = tree;
    indexPtr->stateEpoch = TkBTreeEpoch(tree);
    indexPtr->priv.lineNo = textPtr ? -1 : 0;
    indexPtr->priv.lineNoRel = 0;
    indexPtr->priv.isCharSegment = false;
    DEBUG(indexPtr->discardConsistencyCheck = false);

    if (textPtr) {
	indexPtr->priv.segPtr = textPtr->startMarker;
	indexPtr->priv.linePtr = indexPtr->priv.segPtr->sectionPtr->linePtr;
	indexPtr->priv.byteIndex = FindStartByteIndex(indexPtr);
    } else {
	indexPtr->priv.segPtr = TkBTreeGetShared(tree)->startMarker;
	indexPtr->priv.linePtr = indexPtr->priv.segPtr->sectionPtr->linePtr;
	indexPtr->priv.byteIndex = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetupToEndOfText --
 *
 *	Setup this index to the end of the text. If a peer is given,
 *	then this is the start of last line in this peer, otherwise
 *	it's the start of the very last line.
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
TkTextIndexSetupToEndOfText(
    TkTextIndex *indexPtr,	/* Pointer to index. */
    TkText *textPtr,		/* Text widget for this index, can be NULL. */
    TkTextBTree tree)		/* B-tree for this index. */
{
    assert(indexPtr);
    assert(tree);

    indexPtr->textPtr = textPtr;
    indexPtr->tree = tree;
    indexPtr->stateEpoch = TkBTreeEpoch(tree);
    indexPtr->priv.lineNo = -1;
    indexPtr->priv.lineNoRel = -1;
    DEBUG(indexPtr->discardConsistencyCheck = false);

    if (!textPtr) {
	indexPtr->priv.segPtr = TkBTreeGetShared(tree)->endMarker;
	indexPtr->priv.isCharSegment = false;
	indexPtr->priv.linePtr = indexPtr->priv.segPtr->sectionPtr->linePtr;
	indexPtr->priv.byteIndex = 0;
    } else {
	indexPtr->priv.linePtr = TkBTreeGetLastLine(textPtr);
	indexPtr->priv.segPtr = indexPtr->priv.linePtr->segPtr;
	indexPtr->priv.isCharSegment = indexPtr->priv.segPtr->typePtr == &tkTextCharType;
	indexPtr->priv.byteIndex = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexGetByteIndex --
 *
 *	Get the byte index.
 *
 * Results:
 *	The byte index.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkTextIndexGetByteIndex(
    const TkTextIndex *indexPtr)	/* Pointer to index. */
{
    assert(indexPtr->tree);
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */

    if (indexPtr->priv.byteIndex == -1) {
	assert(indexPtr->priv.segPtr);
	assert(!indexPtr->priv.isCharSegment || TkBTreeEpoch(indexPtr->tree) == indexPtr->stateEpoch);
	assert(indexPtr->priv.segPtr->typePtr);    /* expired? */
	assert(indexPtr->priv.segPtr->sectionPtr); /* linked? */
	assert(indexPtr->priv.segPtr->sectionPtr->linePtr == indexPtr->priv.linePtr);
	/* is mutable due to concept */
	((TkTextIndex *)indexPtr)->priv.byteIndex =
		SegToIndex(indexPtr->priv.linePtr, indexPtr->priv.segPtr);
    }
    return indexPtr->priv.byteIndex;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexToByteIndex --
 *
 *	Force the conversion from segment pointer to byte index. This
 *	will unset the segment pointer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The segment pointer will be unset.
 *
 *----------------------------------------------------------------------
 */

void
TkTextIndexToByteIndex(
    TkTextIndex *indexPtr)	/* Pointer to index. */
{
    assert(indexPtr->tree);
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */
    assert(CheckLine(indexPtr, indexPtr->priv.linePtr));

    if (indexPtr->priv.byteIndex == -1) {
	(void) TkTextIndexGetByteIndex(indexPtr);
    }
    indexPtr->priv.segPtr = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexClear --
 *
 *	Clear all attributes, and set up the corresponding text pointer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The given index will be in an invalid state, the TkIndexGet*
 *	functions cannot be used.
 *
 *----------------------------------------------------------------------
 */

void
TkTextIndexClear(
    TkTextIndex *indexPtr,	/* Pointer to index. */
    TkText *textPtr)		/* Overall information for text widget. */
{
    assert(textPtr);

    indexPtr->textPtr = textPtr;
    indexPtr->tree = textPtr->sharedTextPtr->tree;
    indexPtr->stateEpoch = 0;
    indexPtr->priv.linePtr = NULL;
    indexPtr->priv.segPtr = NULL;
    indexPtr->priv.byteIndex = -1;
    indexPtr->priv.lineNo = -1;
    indexPtr->priv.lineNoRel = -1;
    indexPtr->priv.isCharSegment = false;
    DEBUG(indexPtr->discardConsistencyCheck = false);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexClear2 --
 *
 *	Clear all attributes, and set up the corresponding tree.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The given index will be in an invalid state, the TkIndexGet*
 *	functions cannot be used.
 *
 *----------------------------------------------------------------------
 */

void
TkTextIndexClear2(
    TkTextIndex *indexPtr,	/* Pointer to index. */
    TkText *textPtr,		/* Overall information for text widget, can be NULL */
    TkTextBTree tree)		/* B-tree for this index. */
{
    assert(textPtr || tree);
    assert(!textPtr || !tree || textPtr->sharedTextPtr->tree == tree);

    indexPtr->textPtr = textPtr;
    indexPtr->tree = tree ? tree : textPtr->sharedTextPtr->tree;
    indexPtr->stateEpoch = 0;
    indexPtr->priv.linePtr = NULL;
    indexPtr->priv.segPtr = NULL;
    indexPtr->priv.byteIndex = -1;
    indexPtr->priv.lineNo = -1;
    indexPtr->priv.lineNoRel = -1;
    indexPtr->priv.isCharSegment = false;
    DEBUG(indexPtr->discardConsistencyCheck = false);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexGetLineNumber --
 *
 *	Get the line number.
 *
 * Results:
 *	The line number.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned
TkTextIndexGetLineNumber(
    const TkTextIndex *indexPtr,
    const TkText *textPtr)	/* we want the line number belonging to this peer, can be NULL */
{
    unsigned epoch;
    int32_t *lineNo;

    assert(indexPtr->tree);
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */
    assert(!textPtr || indexPtr->textPtr == textPtr);

    lineNo = (int32_t *) (textPtr ? &indexPtr->priv.lineNoRel : &indexPtr->priv.lineNo);
    epoch = TkBTreeEpoch(indexPtr->tree);

    if (*lineNo == -1 || indexPtr->stateEpoch != epoch) {
	TkTextIndex *iPtr = (TkTextIndex *) indexPtr;

	if (iPtr->priv.byteIndex == -1) {
	    assert(iPtr->priv.segPtr);
	    assert(!iPtr->priv.isCharSegment || indexPtr->stateEpoch == epoch);
	    iPtr->priv.byteIndex = SegToIndex(iPtr->priv.linePtr, iPtr->priv.segPtr);
	    assert(CheckByteIndex(iPtr, iPtr->priv.linePtr, iPtr->priv.byteIndex));
	}
	TkTextIndexSetEpoch(iPtr, epoch);
	*lineNo = TkBTreeLinesTo(iPtr->tree, textPtr, iPtr->priv.linePtr, NULL);
    } else {
	assert(*lineNo == (int) TkBTreeLinesTo(indexPtr->tree, textPtr, indexPtr->priv.linePtr, NULL));
    }

    return *lineNo;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexRebuild --
 *
 *	Rebuild the index after possible modifications, it is required
 *	that TkTextIndexSave has been called before.
 *
 * Results:
 *	Returns whether the original line/byte position could be restored.
 *	This does not meean that we have the same content at this position,
 *	this only means that the we have the same position as before.
 *
 * Side effects:
 *	Adjust the line and the byte offset, if required.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextIndexRebuild(
    TkTextIndex *indexPtr)
{
    TkTextLine *linePtr;
    int byteIndex;
    int lineNo;
    bool rc;

    assert(indexPtr->tree);
    assert(indexPtr->priv.lineNo >= 0 || indexPtr->priv.lineNoRel >= 0);
    assert(indexPtr->priv.byteIndex >= 0);

    if (indexPtr->stateEpoch == TkBTreeEpoch(indexPtr->tree)) {
	return true; /* still up-to-date */
    }

    if (indexPtr->priv.lineNo >= 0) {
	lineNo = MIN(TkBTreeNumLines(indexPtr->tree, NULL), indexPtr->priv.lineNo);
	linePtr = TkBTreeFindLine(indexPtr->tree, NULL, lineNo);
	indexPtr->priv.lineNo = lineNo;
    } else {
	lineNo = MIN(TkBTreeNumLines(indexPtr->tree, indexPtr->textPtr), indexPtr->priv.lineNoRel);
	linePtr = TkBTreeFindLine(indexPtr->tree, indexPtr->textPtr, lineNo);
	indexPtr->priv.lineNoRel = lineNo;
    }

    if (!(rc = (linePtr == indexPtr->priv.linePtr))) {
	indexPtr->priv.linePtr = linePtr;
    }
    byteIndex = MIN(indexPtr->priv.byteIndex, FindEndByteIndex(indexPtr));
    if (byteIndex != indexPtr->priv.byteIndex) {
	rc = false;
    }
    indexPtr->priv.byteIndex = byteIndex;
    indexPtr->priv.segPtr = NULL;

    return rc;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexRestrictToStartRange --
 *
 *	If given index is beyond the range of the widget (at left side),
 *	then this index will be set to start range.
 *
 * Results:
 *	Returns -1 if the index is earlier than start of range, 0 if index
 *	is at start of range, and +1 if index is after start of range.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkTextIndexRestrictToStartRange(
    TkTextIndex *indexPtr)
{
    TkText *textPtr = indexPtr->textPtr;
    TkTextIndex start;
    int cmp;

    assert(indexPtr->tree);

    if (!textPtr || textPtr->startMarker == textPtr->sharedTextPtr->startMarker) {
	return TkTextIndexIsStartOfText(indexPtr) ? 0 : 1;
    }

    start = *indexPtr;
    TkTextIndexSetSegment(&start, textPtr->startMarker);

    if ((cmp = TkTextIndexCompare(indexPtr, &start)) < 0) {
	*indexPtr = start;
	cmp = -1;
    }

    return cmp;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexRestrictToEndRange --
 *
 *	If given index is beyond the range of the widget (at right side),
 *	then this index will be set to end range.
 *
 * Results:
 *	Returns +1 if the index has exceeded the range, 0 if index was at
 *	end of range, and -1 if index is earlier than end of range.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkTextIndexRestrictToEndRange(
    TkTextIndex *indexPtr)
{
    TkText *textPtr = indexPtr->textPtr;
    TkTextIndex last;
    int cmp;

    assert(indexPtr->tree);

    if (!textPtr || textPtr->endMarker == textPtr->sharedTextPtr->endMarker) {
	return TkTextIndexIsEndOfText(indexPtr) ? 0 : -1;
    }

    last = *indexPtr;
    TkTextIndexSetByteIndex2(&last, TkBTreeGetLastLine(textPtr), 0);

    if ((cmp = TkTextIndexCompare(indexPtr, &last)) > 0) {
	*indexPtr = last;
	cmp = 1;
    } else if (cmp < 0) {
	TkTextIndex end = *indexPtr;
	TkTextIndexSetSegment(&end, textPtr->endMarker);
	if (TkTextIndexCompare(indexPtr, &end) > 0) {
	    *indexPtr = last;
	    cmp = 0;
	} else {
	    cmp = -1;
	}
    }

    return cmp;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexEnsureBeforeLastChar --
 *
 *	If given index is on last line, then this index will be set to
 *	the position of the last character in second last line.
 *
 * Results:
 *	Returns 'true' if the index is now before last character position.
 *	This is not possible if the peer is empty, and in this case this
 *	function returns 'false'.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextIndexEnsureBeforeLastChar(
    TkTextIndex *indexPtr)
{
    TkText *textPtr = indexPtr->textPtr;
    const TkTextLine *lastLinePtr;

    assert(indexPtr->tree);
    assert(indexPtr->textPtr);

    if (TkTextIsDeadPeer(indexPtr->textPtr)) {
        return false;
    }

    lastLinePtr = TkBTreeGetLastLine(textPtr);

    if (lastLinePtr == indexPtr->priv.linePtr
	    && (!textPtr || lastLinePtr != textPtr->startMarker->sectionPtr->linePtr)) {
	TkTextIndexSetToLastChar2(indexPtr, lastLinePtr->prevPtr);
    }

    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexGetContentSegment --
 *
 *	Get the pointer to the segment at this byte position which
 *	contains any content (chars, image, or window).
 *
 *	This is the equivalent to the older (and eliminated) function
 *	TkTextIndexToSeg.
 *
 * Results:
 *	Pointer to a segment with size > 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkTextSegment *
TkTextIndexGetContentSegment(
    const TkTextIndex *indexPtr,/* Pointer to index. */
    int *offset)		/* Get offset in segment, can be NULL. */
{
    TkTextSegment *segPtr;

    assert(indexPtr->tree);
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */

    if ((segPtr = indexPtr->priv.segPtr)
	    && (!indexPtr->priv.isCharSegment || TkBTreeEpoch(indexPtr->tree) == indexPtr->stateEpoch)) {
	while (segPtr->size == 0) {
	    segPtr = segPtr->nextPtr;
	}

	if (offset) {
	    if (indexPtr->priv.byteIndex == -1) {
		*offset = 0;
	    } else {
		int byteIndex = SegToIndex(indexPtr->priv.linePtr, segPtr);
		assert(byteIndex <= indexPtr->priv.byteIndex);
		assert(indexPtr->priv.byteIndex < byteIndex + segPtr->size);
		*offset = indexPtr->priv.byteIndex - byteIndex;
	    }
	    assert(*offset >= 0);
	    assert(*offset < segPtr->size);
	}
    } else {
	int myOffset;

	assert(indexPtr->priv.byteIndex >= 0);
	segPtr = IndexToSeg(indexPtr, &myOffset);
	if (myOffset == 0) {
	    TkTextIndex *iPtr = (TkTextIndex *) indexPtr; /* mutable due to concept */
	    iPtr->priv.segPtr = segPtr;
	    iPtr->priv.isCharSegment = segPtr->typePtr == &tkTextCharType;
	}
	if (offset) {
	    *offset = myOffset;
	}
    }

    return segPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexGetFirstSegment --
 *
 *	Get the pointer to first segment at this byte position.
 *
 * Results:
 *	Pointer to a segment.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkTextSegment *
TkTextIndexGetFirstSegment(
    const TkTextIndex *indexPtr,/* Pointer to index. */
    int *offset)		/* Get offset in segment, can be NULL. */
{
    TkTextSegment *segPtr;
    TkTextSegment *prevPtr;
    int myOffset;

    assert(indexPtr->tree);
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */

    if ((segPtr = indexPtr->priv.segPtr)
	    && (!indexPtr->priv.isCharSegment || TkBTreeEpoch(indexPtr->tree) == indexPtr->stateEpoch)) {
	if (indexPtr->priv.byteIndex >= 0) {
	    myOffset = indexPtr->priv.byteIndex - SegToIndex(indexPtr->priv.linePtr, segPtr);
	    assert(myOffset >= 0);
	    assert(segPtr->size == 0 || myOffset < segPtr->size);
	} else {
	    myOffset = 0;
	}
    } else {
	assert(indexPtr->priv.byteIndex >= 0);
	segPtr = IndexToSeg(indexPtr, &myOffset);
    }

    assert(segPtr->typePtr);    /* expired? */
    assert(segPtr->sectionPtr); /* linked? */
    assert(segPtr->sectionPtr->linePtr == indexPtr->priv.linePtr);

    if (myOffset == 0) {
	TkTextIndex *iPtr = (TkTextIndex *) indexPtr; /* mutable due to concept */

	while ((prevPtr = segPtr->prevPtr) && prevPtr->size == 0) {
	    segPtr = prevPtr;
	}

	iPtr->priv.segPtr = segPtr;
	iPtr->priv.isCharSegment = segPtr->typePtr == &tkTextCharType;
    }
    if (offset) {
	*offset = myOffset;
    }
 
    return segPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexIsStartOfLine --
 *
 *	Test whether this index refers to the start of a line.
 *
 * Results:
 *	Returns true if the start of a line is referred, zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextIndexIsStartOfLine(
    const TkTextIndex *indexPtr)
{
    const TkTextSegment *segPtr;
    const TkTextSegment *startPtr;

    assert(indexPtr->tree);
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */
    assert(CheckLine(indexPtr, indexPtr->priv.linePtr));

    if (indexPtr->priv.byteIndex >= 0) {
	return FindStartByteIndex(indexPtr) == indexPtr->priv.byteIndex;
    }

    assert(indexPtr->priv.segPtr);
    assert(!indexPtr->priv.isCharSegment || TkBTreeEpoch(indexPtr->tree) == indexPtr->stateEpoch);
    assert(CheckByteIndex(indexPtr, indexPtr->priv.linePtr, -1));

    startPtr = indexPtr->textPtr ? indexPtr->textPtr->startMarker : NULL;
    segPtr = indexPtr->priv.segPtr;
    if (segPtr->size > 0) {
	segPtr = segPtr->prevPtr;
    }
    while (segPtr && segPtr->size == 0) {
	if (segPtr == startPtr) {
	    return true;
	}
	segPtr = segPtr->prevPtr;
    }

    return !segPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexIsEndOfLine --
 *
 *	Test whether this index refers to the end of the line.
 *
 * Results:
 *	Returns true if the end of the line is referred, false otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextIndexIsEndOfLine(
    const TkTextIndex *indexPtr)
{
    const TkTextSegment *segPtr;
    const TkTextSegment *endPtr;

    assert(indexPtr->tree);
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */
    assert(CheckLine(indexPtr, indexPtr->priv.linePtr));
    assert(CheckByteIndex(indexPtr, indexPtr->priv.linePtr, -1));

    if (indexPtr->priv.byteIndex >= 0) {
	return indexPtr->priv.byteIndex == FindEndByteIndex(indexPtr);
    }

    assert(indexPtr->priv.segPtr);
    assert(!indexPtr->priv.isCharSegment || TkBTreeEpoch(indexPtr->tree) == indexPtr->stateEpoch);

    if (indexPtr->priv.linePtr == TkBTreeGetLastLine(indexPtr->textPtr)) {
	return true;
    }

    segPtr = indexPtr->priv.segPtr;

    if (DontNeedSpecialEndLineTreatment(indexPtr)) {
	while (segPtr->size == 0) {
	    segPtr = segPtr->nextPtr;
	}
	return segPtr->size == 1 && segPtr == indexPtr->priv.linePtr->lastPtr;
    }

    assert(indexPtr->textPtr);
    assert(indexPtr->textPtr->endMarker != indexPtr->textPtr->sharedTextPtr->endMarker);

    endPtr = indexPtr->textPtr->endMarker;
    while (segPtr->size == 0) {
	if (segPtr == endPtr) {
	    return true;
	}
	segPtr = segPtr->nextPtr;
    }

    return false;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexIsStartOfText --
 *
 *	Test whether this index refers to the start of the text.
 *
 * Results:
 *	Returns true if the start of the text is referred, false otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextIndexIsStartOfText(
    const TkTextIndex *indexPtr)
{
    const TkText *textPtr = indexPtr->textPtr;
    const TkTextSegment *segPtr;

    assert(indexPtr->tree);
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */
    assert(CheckLine(indexPtr, indexPtr->priv.linePtr));
    assert(CheckByteIndex(indexPtr, indexPtr->priv.linePtr, -1));

    segPtr = textPtr ? textPtr->startMarker : TkBTreeGetShared(indexPtr->tree)->startMarker;
    return indexPtr->priv.linePtr == segPtr->sectionPtr->linePtr && TkTextIndexIsStartOfLine(indexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexIsEndOfText --
 *
 *	Test whether this index refers to the end of the text.
 *
 * Results:
 *	Returns true if the end of the text is referred, false otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextIndexIsEndOfText(
    const TkTextIndex *indexPtr)
{
    assert(indexPtr->tree);
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */
    assert(CheckLine(indexPtr, indexPtr->priv.linePtr));
    assert(CheckByteIndex(indexPtr, indexPtr->priv.linePtr, -1));

    if (indexPtr->textPtr) {
	return indexPtr->priv.linePtr == TkBTreeGetLastLine(indexPtr->textPtr);
    }
    return !indexPtr->priv.linePtr->nextPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexIsEqual --
 *
 *	Test whether both given indicies are referring the same byte
 *	index. Such a test makes sense only if both indices are
 *	belonging to the same line.
 *
 * Results:
 *	Return true if both indices are equal, otherwise false will be returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextIndexIsEqual(
    const TkTextIndex *indexPtr1,	/* Pointer to index. */
    const TkTextIndex *indexPtr2)	/* Pointer to index. */
{
    const TkTextSegment *segPtr1;
    const TkTextSegment *segPtr2;

    assert(indexPtr1->priv.linePtr);
    assert(indexPtr2->priv.linePtr);
    assert(indexPtr1->priv.linePtr->parentPtr); /* expired? */
    assert(indexPtr2->priv.linePtr->parentPtr); /* expired? */

    if (indexPtr1->priv.linePtr != indexPtr2->priv.linePtr) {
	return false;
    }

    if ((segPtr1 = TkTextIndexGetSegment(indexPtr1))) {
	if ((segPtr2 = TkTextIndexGetSegment(indexPtr2))) {
	    while (segPtr1->prevPtr && segPtr1->prevPtr->size == 0) {
		segPtr1 = segPtr1->prevPtr;
	    }
	    while (segPtr2->prevPtr && segPtr2->prevPtr->size == 0) {
		segPtr2 = segPtr2->prevPtr;
	    }
	    return segPtr1 == segPtr2;
	}
    }

    return TkTextIndexGetByteIndex(indexPtr1) == TkTextIndexGetByteIndex(indexPtr2);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexCompare --
 *
 *	Compare two indicies.
 *
 * Results:
 *	It returns an integer less than, equal to, or greater than zero if
 *	indexPtr1 is found, respectively, to be less than, to match, or be
 *	greater than indexPtr2.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkTextIndexCompare(
    const TkTextIndex *indexPtr1,	/* Pointer to index. */
    const TkTextIndex *indexPtr2)	/* Pointer to index. */
{
    const TkTextSection *sectionPtr1;
    const TkTextSection *sectionPtr2;
    const TkTextSegment *segPtr1;
    const TkTextSegment *segPtr2;

    assert(indexPtr1->priv.linePtr);
    assert(indexPtr2->priv.linePtr);
    assert(indexPtr1->priv.linePtr->parentPtr); /* expired? */
    assert(indexPtr2->priv.linePtr->parentPtr); /* expired? */

    if (indexPtr1->priv.linePtr != indexPtr2->priv.linePtr) {
	int lineNo1 = TkTextIndexGetLineNumber(indexPtr1, NULL);
	int lineNo2 = TkTextIndexGetLineNumber(indexPtr2, NULL);

	return lineNo1 - lineNo2;
    }
    if (indexPtr1->priv.byteIndex >= 0 && indexPtr2->priv.byteIndex >= 0) {
	return indexPtr1->priv.byteIndex - indexPtr2->priv.byteIndex;
    }

    if (!(segPtr1 = TkTextIndexGetSegment(indexPtr1)) || !(segPtr2 = TkTextIndexGetSegment(indexPtr2))) {
	return TkTextIndexGetByteIndex(indexPtr1) - TkTextIndexGetByteIndex(indexPtr2);
    }

    assert(!indexPtr1->priv.isCharSegment || TkBTreeEpoch(indexPtr1->tree) == indexPtr1->stateEpoch);
    assert(!indexPtr2->priv.isCharSegment || TkBTreeEpoch(indexPtr2->tree) == indexPtr2->stateEpoch);

    segPtr1 = indexPtr1->priv.segPtr;
    segPtr2 = indexPtr2->priv.segPtr;
    while (segPtr1->size == 0) {
	segPtr1 = segPtr1->nextPtr;
    }
    while (segPtr2->size == 0) {
	segPtr2 = segPtr2->nextPtr;
    }
    if (segPtr1 == segPtr2) {
	return 0;
    }
    sectionPtr1 = indexPtr1->priv.segPtr->sectionPtr;
    sectionPtr2 = indexPtr2->priv.segPtr->sectionPtr;
    if (sectionPtr1 != sectionPtr2) {
	while (sectionPtr1 && sectionPtr1 != sectionPtr2) {
	    sectionPtr1 = sectionPtr1->nextPtr;
	}
	return sectionPtr1 ? -1 : +1;
    }
    segPtr1 = indexPtr1->priv.segPtr;
    segPtr2 = indexPtr2->priv.segPtr;
    while (segPtr1 != segPtr2) {
	if (!(segPtr1 = segPtr1->nextPtr) || segPtr1->sectionPtr != sectionPtr1) {
	    return +1;
	}
    }
    return -1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexAddToByteIndex --
 *
 *	Add given byte offset to byte index.
 *
 *	Note that this function allows that the byte index will reach the
 *	size of the line, in this case the line will be advanced, and the
 *	byte index will be set to zero.
 *
 * Results:
 *	Returns whether we're on same line.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextIndexAddToByteIndex(
    TkTextIndex *indexPtr,	/* Pointer to index. */
    int byteOffset)		/* Add this offset. */
{
    bool rc = true;

    assert(indexPtr->tree);
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */
    assert(CheckLine(indexPtr, indexPtr->priv.linePtr));
    assert(CheckByteIndex(indexPtr, indexPtr->priv.linePtr, -1));

    if (byteOffset == 0) {
	return true;
    }

    if (indexPtr->priv.byteIndex == -1) {
	(void) TkTextIndexGetByteIndex(indexPtr);
    }

    if (byteOffset > 0) {
	if ((indexPtr->priv.byteIndex += byteOffset) > FindEndByteIndex(indexPtr)) {
	    assert(indexPtr->priv.linePtr->nextPtr);
	    assert(indexPtr->priv.byteIndex <= indexPtr->priv.linePtr->size);
	    indexPtr->priv.linePtr = indexPtr->priv.linePtr->nextPtr;
	    if (indexPtr->priv.lineNo >= 0) {
		indexPtr->priv.lineNo += 1;
	    }
	    if (indexPtr->priv.lineNoRel >= 0) {
		indexPtr->priv.lineNoRel += 1;
	    }
	    indexPtr->priv.byteIndex = 0;
	    rc = false;
	}
    } else {
	assert(-byteOffset <= indexPtr->priv.byteIndex);
	indexPtr->priv.byteIndex += byteOffset;
    }

    indexPtr->priv.segPtr = NULL;

    assert(CheckLine(indexPtr, indexPtr->priv.linePtr));
    assert(CheckByteIndex(indexPtr, indexPtr->priv.linePtr, -1));

    return rc;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpTextIndexDump --
 *
 *	This function is for debugging only, printing the given index
 *	on stdout.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */
#ifndef NDEBUG

void
TkpTextIndexDump(
    TkText *textPtr,		/* May be NULL. */
    const TkTextIndex *indexPtr)/* Pointer to index. */
{
    char buf[TK_POS_CHARS];
    TkTextIndexPrint(TkTextIndexGetShared(indexPtr), textPtr, indexPtr, buf);
    printf("%s\n", buf);
}

#endif /* NDEBUG */

/*
 *---------------------------------------------------------------------------
 *
 * TkTextNewIndexObj --
 *
 *	This function generates a Tcl_Obj description of an index, suitable
 *	for reading in again later. The index generated is effectively stable
 *	to all except insertion/deletion operations on the widget.
 *
 * Results:
 *	A new Tcl_String with refCount zero.
 *
 * Side effects:
 *	A small amount of memory is allocated.
 *
 *---------------------------------------------------------------------------
 */

Tcl_Obj *
TkTextNewIndexObj(
    const TkTextIndex *indexPtr)/* Pointer to index. */
{
    char buffer[TK_POS_CHARS];
    int len;

    assert(indexPtr->textPtr);

    len = TkTextPrintIndex(indexPtr->textPtr, indexPtr, buffer);
    return Tcl_NewStringObj(buffer, len);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextMakeByteIndex --
 *
 *	Given a line index and a byte index, look things up in the B-tree and
 *	fill in a TkTextIndex structure.
 *
 * Results:
 *	The structure at *indexPtr is filled in with information about the
 *	character at lineIndex and byteIndex (or the closest existing
 *	character, if the specified one doesn't exist), and indexPtr is
 *	returned as result.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

TkTextIndex *
TkTextMakeByteIndex(
    TkTextBTree tree,		/* Tree that lineIndex and byteIndex refer TkTextBTree tree, to. */
    const TkText *textPtr,	/* Client that lineIndex and byteIndex refer to, can be NULL. */
    int lineIndex,		/* Index of desired line (0 means first line of text). */
    int byteIndex,		/* Byte index of desired character. */
    TkTextIndex *indexPtr)	/* Structure to fill in. */
{
    TkTextSegment *segPtr;
    TkTextSection *sectionPtr;
    TkTextLine *linePtr;
    int index, nextIndex;

    TkTextIndexClear2(indexPtr, (TkText *) textPtr, tree);

    if (lineIndex < 0) {
	TkTextIndexSetupToStartOfText(indexPtr, (TkText *) textPtr, tree);
	return indexPtr;
    }

    if (!(linePtr = TkBTreeFindLine(tree, textPtr, lineIndex))) {
	TkTextIndexSetupToEndOfText(indexPtr, (TkText *) textPtr, tree);
	return indexPtr;
    }

    if (byteIndex < 0) {
	byteIndex = 0;
    }

    if (textPtr) {
	if (textPtr->startMarker != textPtr->sharedTextPtr->startMarker
		&& textPtr->startMarker->sectionPtr->linePtr == linePtr) {
	    int startByteIndex;

	    TkTextIndexSetSegment(indexPtr, textPtr->startMarker);
	    startByteIndex = FindStartByteIndex(indexPtr);
	    if (byteIndex <= startByteIndex) {
		return indexPtr;
	    }
	}
	if (textPtr->endMarker != textPtr->sharedTextPtr->endMarker
		&& textPtr->endMarker->sectionPtr->linePtr == linePtr) {
	    int endByteIndex;

	    TkTextIndexSetSegment(indexPtr, textPtr->endMarker);
	    endByteIndex = FindEndByteIndex(indexPtr);
	    if (endByteIndex <= byteIndex) {
		return indexPtr;
	    }
	}
    }

    indexPtr->priv.linePtr = linePtr;

    if (byteIndex == 0) {
	/* this is catching a frequent case */
	TkTextIndexSetByteIndex(indexPtr, 0);
	return indexPtr;
    }

    if (byteIndex >= linePtr->size) {
	/*
	 * Use the index of the last character in the line. Since the last
	 * character on the line is guaranteed to be a '\n', we can back
	 * up one byte.
	 *
	 * Note that it is already guaranteed that we do not exceed the position
	 * of the end marker.
	 */
	TkTextIndexSetByteIndex(indexPtr, linePtr->size - 1);
	return indexPtr;
    }

    indexPtr->priv.byteIndex = byteIndex;
    index = 0;

    sectionPtr = linePtr->segPtr->sectionPtr;
    while ((nextIndex = index + sectionPtr->size) <= byteIndex) {
	index = nextIndex;
	sectionPtr = sectionPtr->nextPtr;
	assert(sectionPtr);
    }

    segPtr = sectionPtr->segPtr;
    while ((nextIndex = index + segPtr->size) < byteIndex) {
	index = nextIndex;
	segPtr = segPtr->nextPtr;
	assert(segPtr);
    }

    /*
     * Verify that the index points to a valid character boundary.
     */

    if (segPtr->typePtr == &tkTextCharType && byteIndex > index && index + segPtr->size > byteIndex) {
	const char *p = segPtr->body.chars + (byteIndex - index);

	/*
	 * Prevent UTF-8 character from being split up by ensuring that byteIndex
	 * falls on a character boundary. If index falls in the middle of a UTF-8
	 * character, it will be adjusted to the end of that UTF-8 character.
	 */

	while ((*p & 0xc0) == 0x80) {
	    ++p;
	    indexPtr->priv.byteIndex += 1;
	}
    }

    return indexPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextMakeCharIndex --
 *
 *	Given a line index and a character index, look things up in the B-tree
 *	and fill in a TkTextIndex structure.
 *
 * Results:
 *	The structure at *indexPtr is filled in with information about the
 *	character at lineIndex and charIndex (or the closest existing
 *	character, if the specified one doesn't exist), and indexPtr is
 *	returned as result.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static unsigned
CountCharsInSeg(
    const TkTextSegment *segPtr)
{
    assert(segPtr->typePtr == &tkTextCharType);
    return Tcl_NumUtfChars(segPtr->body.chars, segPtr->size);
}

TkTextIndex *
TkTextMakeCharIndex(
    TkTextBTree tree,		/* Tree that lineIndex and charIndex refer to. */
    TkText *textPtr,		/* Client that lineIndex and charIndex refer to, can be NULL. */
    int lineIndex,		/* Index of desired line (0 means first line of text). */
    int charIndex,		/* Index of desired character. */
    TkTextIndex *indexPtr)	/* Structure to fill in. */
{
    TkTextSegment *segPtr, *lastPtr;
    TkTextLine *linePtr;
    char *p, *start, *end;
    int index, offset;

    TkTextIndexClear2(indexPtr, textPtr, tree);

    if (lineIndex < 0) {
	TkTextIndexSetupToStartOfText(indexPtr, textPtr, tree);
	return indexPtr;
    }

    if (!(linePtr = TkBTreeFindLine(tree, textPtr, lineIndex))) {
	TkTextIndexSetupToEndOfText(indexPtr, textPtr, tree);
	return indexPtr;
    }

    indexPtr->priv.linePtr = linePtr;

    if (charIndex >= linePtr->size - 1) {
	/* this is catching a frequent case */
	TkTextIndexSetToLastChar(indexPtr);
	return indexPtr;
    }

    if (charIndex <= 0) {
	/* this is catching a frequent case */
	TkTextIndexSetToStartOfLine(indexPtr);
	return indexPtr;
    }

    if (textPtr && textPtr->endMarker->sectionPtr->linePtr == linePtr) {
	lastPtr = textPtr->endMarker;
    } else {
	lastPtr = NULL;
    }

    if (!textPtr
	    || textPtr->startMarker == TkBTreeGetShared(indexPtr->tree)->startMarker
	    || linePtr != textPtr->startMarker->sectionPtr->linePtr) {
	segPtr = linePtr->segPtr;
	index = 0;
    } else {
	TkTextSegment *startPtr;

	/*
	 * We have to skip some segments not belonging to this peer.
	 */

	TkTextIndexSetSegment(indexPtr, textPtr->startMarker);
	startPtr = TkTextIndexGetFirstSegment(indexPtr, NULL);

	for (segPtr = linePtr->segPtr; segPtr != startPtr; segPtr = segPtr->nextPtr) {
	    if (segPtr->typePtr == &tkTextCharType) {
		charIndex -= CountCharsInSeg(segPtr);
	    } else {
		assert(segPtr->size <= 1);
		charIndex -= segPtr->size;
	    }
	    if (charIndex <= 0) {
		return indexPtr;
	    }
	}

	index = TkTextIndexGetByteIndex(indexPtr);
	indexPtr->priv.segPtr = NULL;
    }

    /*
     * Verify that the index is within the range of the line. If not, just use
     * the index of the last character in the line.
     */

    while (segPtr != lastPtr) {
	if (segPtr->tagInfoPtr) {
	    if (segPtr->typePtr == &tkTextCharType) {
		int ch;

		/*
		 * Turn character offset into a byte offset.
		 */

		start = segPtr->body.chars;
		end = start + segPtr->size;

		for (p = start; p < end; p += offset) {
		    if (charIndex == 0) {
			indexPtr->priv.byteIndex = index;
			return indexPtr;
		    }
		    charIndex -= 1;
		    offset = TkUtfToUniChar(p, &ch);
		    index += offset;
		}
	    } else if (charIndex < segPtr->size) {
		indexPtr->priv.byteIndex = index;
		return indexPtr;
	    } else {
		assert(segPtr->size == 1);
		charIndex -= 1;
		index += 1;
	    }
	}
	if (!(segPtr = segPtr->nextPtr)) {
	    /*
	     * Use the index of the last character in the line.
	     */
	    TkTextIndexSetToLastChar(indexPtr);
	    return indexPtr;
	}
    }

    indexPtr->priv.byteIndex = index;
    return indexPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * IndexToSeg --
 *
 *	Given an index, this function returns the segment and offset within
 *	segment for the index.
 *
 * Results:
 *	The return value is a pointer to the segment referred to by indexPtr;
 *	this will always be a segment with non-zero size. The variable at
 *	*offsetPtr is set to hold the integer offset within the segment of the
 *	character given by indexPtr.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static TkTextSegment *
IndexToSeg(
    const TkTextIndex *indexPtr,/* Text index. */
    int *offsetPtr)		/* Where to store offset within segment, or NULL if offset isn't
    				 * wanted. */
{
    TkTextSection *sectionPtr;
    TkTextSegment *segPtr;
    TkTextLine *linePtr;
    int index;

    assert(indexPtr->priv.byteIndex >= 0);
    assert(indexPtr->priv.byteIndex < indexPtr->priv.linePtr->size);

    index = indexPtr->priv.byteIndex;
    linePtr = indexPtr->priv.linePtr;

    /*
     * Speed up a frequent use case.
     */

    if (index == 0) {
	segPtr = linePtr->segPtr;
	while (segPtr->size == 0) {
	    segPtr = segPtr->nextPtr;
	}
	if (offsetPtr) {
	    *offsetPtr = 0;
	}
	return segPtr;
    }

    /*
     * Speed up a frequent use case.
     */

    if (index == linePtr->size - 1) {
	assert(linePtr->lastPtr->typePtr == &tkTextCharType);
	if (offsetPtr) {
	    *offsetPtr = linePtr->lastPtr->size - 1;
	}
	return linePtr->lastPtr;
    }

    /*
     * Now we iterate through the section an segment structure until we reach the
     * wanted byte index.
     */

    sectionPtr = linePtr->segPtr->sectionPtr;
    for ( ; index >= sectionPtr->size; sectionPtr = sectionPtr->nextPtr) {
	index -= sectionPtr->size;
	assert(sectionPtr->nextPtr);
    }
    for (segPtr = sectionPtr->segPtr; index >= segPtr->size; segPtr = segPtr->nextPtr) {
	index -= segPtr->size;
	assert(segPtr->nextPtr);
    }
    assert(segPtr->size > 0);

    if (offsetPtr) {
	*offsetPtr = index;
    }
    return segPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * SegToIndex --
 *
 *	Given a segment pointer, this function returns the offset of the
 *	segment within its line.
 *
 *	This function assumes that we want the index to the current line,
 *	and not the index from the start of the logical line.
 *
 * Results:
 *	The return value is the offset (within its line) of the first
 *	character in segPtr.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static int
SegToIndex(
    const TkTextLine *linePtr,
    const TkTextSegment *segPtr)/* Segment whose offset is desired. */
{
    const TkTextSection *sectionPtr;
    const TkTextSegment *segPtr2;
    int offset;

    assert(segPtr->sectionPtr); /* otherwise not linked */
    assert(segPtr->sectionPtr->linePtr == linePtr);

    sectionPtr = linePtr->segPtr->sectionPtr; /* first segment in line */

    /*
     * Speed up frequent use cases.
     */

    if (segPtr == sectionPtr->segPtr) {
	return 0;
    }

    if (segPtr == linePtr->lastPtr) {
	return linePtr->size - segPtr->size;
    }

    /*
     * Now we iterate through the section an segment structure until we reach the
     * given segment.
     */

    offset = 0;

    for ( ; sectionPtr != segPtr->sectionPtr; sectionPtr = sectionPtr->nextPtr) {
	offset += sectionPtr->size;
	assert(sectionPtr->nextPtr);
    }
    for (segPtr2 = segPtr->sectionPtr->segPtr; segPtr2 != segPtr; segPtr2 = segPtr2->nextPtr) {
	offset += segPtr2->size;
	assert(segPtr2->nextPtr);
    }

    return offset;
}
/*
 *---------------------------------------------------------------------------
 *
 * TkTextSegToIndex --
 *
 *	Given a segment pointer, this function returns the offset of the
 *	segment within its line.
 *
 *	This function assumes that we want the index to the current line,
 *	and not the index from the start of the logical line.
 *
 * Results:
 *	The return value is the offset (within its line) of the first
 *	character in segPtr.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

int
TkTextSegToIndex(
    const TkTextSegment *segPtr)/* Segment whose offset is desired. */
{
    return SegToIndex(segPtr->sectionPtr->linePtr, segPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextGetIndex --
 *
 *	Given a string, return the index that is described.
 *
 * Results:
 *	The return value is a standard Tcl return result. If TCL_OK is
 *	returned, then everything went well and the index at *indexPtr is
 *	filled in; otherwise TCL_ERROR is returned and an error message is
 *	left in the interp's result.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

int
TkTextGetIndex(
    Tcl_Interp *interp,		/* Use this for error reporting. */
    TkText *textPtr,		/* Information about text widget. */
    const char *string,		/* Textual description of position. */
    TkTextIndex *indexPtr)	/* Index structure to fill in. */
{
    assert(textPtr);
    return GetIndex(interp, textPtr->sharedTextPtr, textPtr, string, indexPtr) ? TCL_OK : TCL_ERROR;
}

/*
 *---------------------------------------------------------------------------
 *
 * GetIndex --
 *
 *	Given a string, return the index that is described.
 *
 * Results:
 *	If 'true' is returned, then everything went well and the index at
 *	*indexPtr is filled in; otherwise 'false' is returned and an error
 *	message is left in the interp's result.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static unsigned
SkipSegments(
    TkTextSegment *startMarkPtr)
{
    TkTextSegment *segPtr = startMarkPtr->sectionPtr->linePtr->segPtr;
    unsigned charIndex = 0;

    /* Skip chars not belonging to this text widget. */

    for ( ; segPtr != startMarkPtr; segPtr = segPtr->nextPtr) {
	if (segPtr->tagInfoPtr) {
	    charIndex += (segPtr->typePtr == &tkTextCharType) ? CountCharsInSeg(segPtr) : 1;
	}
    }

    return charIndex;
}

static bool
GetIndex(
    Tcl_Interp *interp,		/* Use this for error reporting. */
    TkSharedText *sharedTextPtr,/* Pointer to shared resource. */
    TkText *textPtr,		/* Information about text widget. */
    const char *string,		/* Textual description of position. */
    TkTextIndex *indexPtr)	/* Index structure to fill in. */
{
    char *p, *end, *endOfBase;
    TkTextIndex first, last;
    char c;
    const char *cp;
    char *myString;
    Tcl_DString copy;
    bool tagFound;
    bool skipMark;
    bool result;

    assert(textPtr);
    assert(sharedTextPtr);

    /*
     * The documentation about indices says:
     *
     *	The base for an index must have one of the following forms:
     *
     *		<line.<char>
     *		@<x>,<y>
     *		begin
     *		end
     *		<mark>
     *		<tag>.first
     *		<tag>.last
     *		<pathName>
     *		<imageName>
     *
     * Furthermore the documentation says:
     *
     *	If the base could match more than one of the above forms, such as a mark and imageName
     *	both having the same value, then the form earlier in the above list takes precedence. 
     */

#if BEGIN_DOES_NOT_BELONG_TO_BASE
    /*
     *------------------------------------------------
     * Stage 0: for portability reasons keyword "begin" has the lowest
     * precedence (but this should be corrected in a future version).
     *------------------------------------------------
     */

    if (string[0] == 'b' && strncmp(string, "begin", 5)) {
	if (TkTextMarkNameToIndex(textPtr, string, indexPtr)
		|| TkTextWindowIndex(textPtr, string, indexPtr)
		|| TkTextImageIndex(textPtr, string, indexPtr)) {
	    return true;
	}
    }
#endif /* BEGIN_DOES_NOT_BELONG_TO_BASE */

    /*
     *------------------------------------------------
     * Stage 1: start by parsing the base index.
     *------------------------------------------------
     */

    TkTextIndexClear(indexPtr, textPtr);

    /*
     * First look for the form "tag.first" or "tag.last" where "tag" is the
     * name of a valid tag. Try to use up as much as possible of the string in
     * this check (strrchr instead of strchr below). Doing the check now, and
     * in this way, allows tag names to include funny characters like "@" or
     * "+1c".
     */

    Tcl_DStringInit(&copy);
    myString = Tcl_DStringAppend(&copy, string, -1);
    p = strrchr(myString, '.');
    skipMark = false;

    if (p) {
	TkTextSearch search;
	TkTextTag *tagPtr;
	Tcl_HashEntry *hPtr = NULL;
	const char *tagName;
	bool wantLast;

	if (p[1] == 'f' && strncmp(p + 1, "first", 5) == 0) {
	    wantLast = false;
	    endOfBase = p + 6;
	} else if (p[1] == 'l' && strncmp(p + 1, "last", 4) == 0) {
	    wantLast = true;
	    endOfBase = p + 5;
	} else {
	    goto tryxy;
	}

	/*
	 * Marks have a higher precedence than tag.first or tag.last, so we will
	 * search for marks before proceeding with tags.
	 */

	if (TkTextMarkNameToIndex(textPtr, string, indexPtr)) {
	    Tcl_DStringFree(&copy);
	    return true;
	}

	skipMark = true;
	tagPtr = NULL;
	tagName = myString;
	if (p - tagName == 3 && strncmp(tagName, "sel", 3) == 0) {
	    /*
	     * Special case for sel tag which is not stored in the hash table.
	     */
	    tagPtr = textPtr->selTagPtr;
	} else {
	    *p = '\0';
	    hPtr = Tcl_FindHashEntry(&sharedTextPtr->tagTable, tagName);
	    *p = '.';
	    if (hPtr) {
		tagPtr = Tcl_GetHashValue(hPtr);
	    }
	}

	if (!tagPtr) {
	    goto tryxy;
	}

	TkTextIndexSetupToStartOfText(&first, textPtr, sharedTextPtr->tree);
	TkTextIndexSetupToEndOfText(&last, textPtr, sharedTextPtr->tree);
	if (wantLast) {
	    TkBTreeStartSearchBack(&last, &first, tagPtr, &search, SEARCH_EITHER_TAGON_TAGOFF);
	    tagFound = TkBTreePrevTag(&search);
	} else {
	    TkBTreeStartSearch(&first, &last, tagPtr, &search, SEARCH_NEXT_TAGON);
	    tagFound = TkBTreeNextTag(&search);
	}
	if (!tagFound) {
	    if (tagPtr == textPtr->selTagPtr) {
		tagName = "sel";
	    } else if (hPtr) {
		tagName = Tcl_GetHashKey(&sharedTextPtr->tagTable, hPtr);
	    }
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "text doesn't contain any characters tagged with \"%s\"", tagName));
	    Tcl_SetErrorCode(interp, "TK", "LOOKUP", "TEXT_INDEX", tagName, NULL);
	    Tcl_DStringFree(&copy);
	    return false;
	}
	*indexPtr = search.curIndex;
	goto gotBase;
    }

  tryxy:
    if (string[0] == '@') {
	/*
	 * Find character at a given x,y location in the window.
	 */

	int x, y;

	cp = string + 1;
	if (*cp == 'f' && strncmp(cp, "first,", 6) == 0) {
	    x = TkTextGetFirstXPixel(textPtr);
	    end = (char *) cp + 5;
	} else if (*cp == 'l' && strncmp(cp, "last,", 5) == 0) {
	    x = TkTextGetLastXPixel(textPtr);
	    end = (char *) cp + 4;
	} else {
	    x = strtol(cp, &end, 0);
	    if (end == cp || *end != ',') {
		goto noBaseFound;
	    }
	}
	cp = end + 1;
	if (*cp == 'f' && strcmp(cp, "first") == 0) {
	    y = TkTextGetFirstYPixel(textPtr);
	    end += 6;
	} else if (*cp == 'l' && strcmp(cp, "last") == 0) {
	    y = TkTextGetLastYPixel(textPtr);
	    end += 5;
	} else {
	    y = strtol(cp, &end, 0);
	    if (end == cp) {
		goto noBaseFound;
	    }
	}
	TkTextPixelIndex(textPtr, x, y, indexPtr, NULL);
	endOfBase = end;
	goto gotBase;
    }

    if (isdigit(string[0]) || string[0] == '-') {
	int lineIndex, charIndex;

	/*
	 * Base is identified with line and character indices.
	 */

	lineIndex = strtol(string, &end, 0) - 1;
	if (end == string || *end != '.') {
	    goto noBaseFound;
	}
	p = end + 1;
	if (*p == 'e' && strncmp(p, "end", 3) == 0) {
	    charIndex = INT_MAX;
	    endOfBase = p + 3;
	} else if (*p == 'b' && strncmp(p, "begin", 5) == 0) {
	    charIndex = 0;
	    endOfBase = p + 5;
	} else {
	    charIndex = strtol(p, &end, 0);
	    if (end == p) {
		goto noBaseFound;
	    }
	    endOfBase = end;
	}
	if (lineIndex == 0 && textPtr->startMarker != sharedTextPtr->startMarker) {
	    charIndex += SkipSegments(textPtr->startMarker);
	}
	TkTextMakeCharIndex(sharedTextPtr->tree, textPtr, lineIndex, charIndex, indexPtr);
	goto gotBase;
    }

    for (p = myString; *p != 0; ++p) {
	if (isspace(*p) || *p == '+' || *p == '-') {
	    break;
	}
    }
    endOfBase = p;
    if (string[0] == '.') {
	/*
	 * See if the base position is the name of an embedded window.
	 */

	c = *endOfBase;
	*endOfBase = '\0';
	result = TkTextWindowIndex(textPtr, myString, indexPtr);
	*endOfBase = c;
	if (result) {
	    goto gotBase;
	}
    }
    if (string[0] == 'b' && endOfBase - myString == 5 && strncmp(string, "begin", 5) == 0) {
	/*
	 * Base position is start of text.
	 */

	TkTextIndexSetupToStartOfText(indexPtr, textPtr, sharedTextPtr->tree);
	goto gotBase;
    }
    if (string[0] == 'e' && endOfBase - myString == 3 && strncmp(string, "end", 3) == 0) {
	/*
	 * Base position is end of text.
	 */

	TkTextIndexSetupToEndOfText(indexPtr, textPtr, sharedTextPtr->tree);
	goto gotBase;
    }
    
    /*
     * See if the base position is the name of a mark.
     */

    c = *endOfBase;
    *endOfBase = '\0';
    result = TkTextMarkNameToIndex(textPtr, myString, indexPtr);
    if (result) {
	*endOfBase = c;
	goto gotBase;
    }

    /*
     * See if the base position is the name of an embedded image.
     */

    result = TkTextImageIndex(textPtr, myString, indexPtr);
    *endOfBase = c;
    if (result) {
	goto gotBase;
    }

  noBaseFound:
    if ((!skipMark && TkTextMarkNameToIndex(textPtr, string, indexPtr))
	    || TkTextWindowIndex(textPtr, string, indexPtr)
	    || TkTextImageIndex(textPtr, string, indexPtr)) {
	Tcl_DStringFree(&copy);
	return true;
    }

    Tcl_DStringFree(&copy);
    Tcl_ResetResult(interp);
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad text index \"%s\"", string));
    Tcl_SetErrorCode(interp, "TK", "TEXT", "BAD_INDEX", NULL);
    return false;

    /*
     *-------------------------------------------------------------------
     * Stage 2: process zero or more modifiers. Each modifier is either a
     * keyword like "wordend" or "linestart", or it has the form "op count
     * units" where op is + or -, count is a number, and units is "chars" or
     * "lines".
     *-------------------------------------------------------------------
     */

  gotBase:
    cp = endOfBase;

    while (true) {
	while (isspace(*cp)) {
	    cp++;
	}
	if (*cp == '\0') {
	    break;
	}
	if (*cp == '+' || *cp == '-') {
	    cp = ForwBack(textPtr, cp, indexPtr);
	} else {
	    cp = StartEnd(textPtr, cp, indexPtr);
	}
	if (!cp) {
	    goto noBaseFound;
	}
    }

    Tcl_DStringFree(&copy);
    return true;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextIndexPrint --
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

int
TkTextIndexPrint(
    const TkSharedText *sharedTextPtr,
    				/* Pointer to shared resource. */
    const TkText *textPtr,	/* Information about text widget, can be NULL. */
    const TkTextIndex *indexPtr,/* Pointer to index. */
    char *string)		/* Place to store the position. Must have at least TK_POS_CHARS
    				 * characters. */
{
    const TkTextSegment *segPtr;
    const TkTextLine *linePtr;
    const TkTextSegment *startMarker;
    int charIndex;

    assert(sharedTextPtr);
    assert(indexPtr);
    assert(string);
    assert(CheckLine(indexPtr, indexPtr->priv.linePtr));
    assert(CheckByteIndex(indexPtr, indexPtr->priv.linePtr, -1));

    charIndex = 0;
    linePtr = indexPtr->priv.linePtr;
    startMarker = textPtr ? textPtr->startMarker : sharedTextPtr->startMarker;
    segPtr = (linePtr == startMarker->sectionPtr->linePtr) ? startMarker : linePtr->segPtr;

    /*
     * Too bad that we cannot use the section structure here.
     *
     * The user of the Tk text widget is encouraged to work with marks,
     * in this way the expensive mappings between char indices and byte
     * indices can be avoided in many cases.
     */

    if (indexPtr->priv.segPtr && !indexPtr->priv.isCharSegment) {
	TkTextSegment *lastPtr = indexPtr->priv.segPtr;

	assert(indexPtr->priv.segPtr->typePtr);

	while (lastPtr->size == 0) {
	    lastPtr = lastPtr->nextPtr;
	}

	for ( ; segPtr != lastPtr; segPtr = segPtr->nextPtr) {
	    if (segPtr->typePtr == &tkTextCharType) {
		charIndex += CountCharsInSeg(segPtr);
	    } else {
		assert(segPtr->size <= 1);
		charIndex += segPtr->size;
	    }
	    assert(segPtr->nextPtr);
	}
    } else {
	int numBytes = TkTextIndexGetByteIndex(indexPtr);

	if (segPtr == startMarker && startMarker != sharedTextPtr->startMarker) {
	    numBytes -= TkTextSegToIndex(startMarker);
	}

	assert(numBytes >= 0);
	assert(numBytes < linePtr->size);

	for ( ; numBytes > segPtr->size; segPtr = segPtr->nextPtr) {
	    if (segPtr->typePtr == &tkTextCharType) {
		charIndex += CountCharsInSeg(segPtr);
	    } else {
		assert(segPtr->size <= 1);
		charIndex += segPtr->size;
	    }
	    numBytes -= segPtr->size;
	    assert(segPtr->nextPtr);
	}

	if (numBytes) {
	    if (segPtr->typePtr == &tkTextCharType) {
		charIndex += Tcl_NumUtfChars(segPtr->body.chars, numBytes);
	    } else {
		assert(segPtr->size <= 1);
		charIndex += numBytes;
	    }
	}
    }

    return snprintf(string, TK_POS_CHARS, "%d.%d",
	    TkBTreeLinesTo(indexPtr->tree, textPtr, linePtr, NULL) + 1, charIndex);
}

/*
 *---------------------------------------------------------------------------
 *
 * ForwBack --
 *
 *	This function handles +/- modifiers for indices to adjust the index
 *	forwards or backwards.
 *
 * Results:
 *	If the modifier in string is successfully parsed then the return value
 *	is the address of the first character after the modifier, and
 *	*indexPtr is updated to reflect the modifier. If there is a syntax
 *	error in the modifier then NULL is returned.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static const char *
ForwBack(
    TkText *textPtr,		/* Information about text widget. */
    const char *string,		/* String to parse for additional info about
				 * modifier (count and units). Points to "+"
				 * or "-" that starts modifier. */
    TkTextIndex *indexPtr)	/* Index to update as specified in string. */
{
    const char *p, *units;
    char *end;
    int count, lineIndex, modifier;
    size_t length;

    assert(textPtr);

    /*
     * Get the count (how many units forward or backward).
     */

    p = string + 1;
    while (isspace(*p)) {
	p++;
    }
    count = strtol(p, &end, 0);
    if (end == p) {
	return NULL;
    }
    p = end;
    while (isspace(*p)) {
	p++;
    }

    /*
     * Find the end of this modifier (next space or + or - character), then
     * check if there is a textual 'display' or 'any' modifier. These
     * modifiers can be their own word (in which case they can be abbreviated)
     * or they can follow on to the actual unit in a single word (in which
     * case no abbreviation is allowed). So, 'display lines', 'd lines',
     * 'displaylin' are all ok, but 'dline' is not.
     */

    units = p;
    while (*p != '\0' && !isspace(*p) && *p != '+' && *p != '-') {
	p++;
    }
    length = p - units;
    if (*units == 'd' && strncmp(units, "display", MIN(length, 7)) == 0) {
	modifier = TKINDEX_DISPLAY;
	if (length > 7) {
	    p -= (length - 7);
	}
    } else if (*units == 'a' && strncmp(units, "any", MIN(length, 3)) == 0) {
	modifier = TKINDEX_CHAR;
	if (length > 3) {
	    p -= (length - 3);
	}
    } else {
	modifier = TKINDEX_NONE;
    }

    /*
     * If we had a modifier, which we interpreted ok, so now forward to the
     * actual units.
     */

    if (modifier != TKINDEX_NONE) {
	while (isspace(*p)) {
	    p++;
	}
	units = p;
	while (*p != '\0' && !isspace(*p) && *p != '+' && *p != '-') {
	    p++;
	}
	length = p - units;
    }

    /*
     * Finally parse the units.
     */

    if (*units == 'c' && strncmp(units, "chars", length) == 0) {
	TkTextCountType type;

	if (modifier == TKINDEX_DISPLAY) {
	    type = COUNT_DISPLAY_CHARS;
	} else { /* if (modifier == TKINDEX_NONE) */
	    assert(modifier == TKINDEX_NONE || modifier == TKINDEX_CHAR);

	    /*
	     * The following is incompatible to 8.4 (and prior versions), but I think that
	     * now it's the time to eliminate this known issue:
	     *
	     *    Before Tk 8.5, the widget used the string “chars” to refer to index positions
	     *    (which included characters, embedded windows and embedded images). As of Tk 8.5
	     *    the text widget deals separately and correctly with “chars” and “indices”. For
	     *    backwards compatibility, however, the index modifiers “+N chars” and “-N chars”
	     *    continue to refer to indices. One must use any of the full forms “+N any chars”
	     *    or “-N any chars” etc. to refer to actual character indices. This confusion may
	     *    be fixed in a future release by making the widget correctly interpret “+N chars”
	     *    as a synonym for “+N any chars”.
	     *
	     * This confusion is fixed now, we will interpret "+N chars" as a synonym for
	     * “+N any chars”.
	     */

	    type = COUNT_CHARS;
	}

	if (*string == '+') {
	    TkTextIndexForwChars(textPtr, indexPtr, count, indexPtr, type);
	} else {
	    TkTextIndexBackChars(textPtr, indexPtr, count, indexPtr, type);
	}
    } else if (*units == 'i' && strncmp(units, "indices", length) == 0) {
	TkTextCountType type;

	if (modifier == TKINDEX_DISPLAY) {
	    type = COUNT_DISPLAY_INDICES;
	} else {
	    type = COUNT_INDICES;
	}

	if (*string == '+') {
	    TkTextIndexForwChars(textPtr, indexPtr, count, indexPtr, type);
	} else {
	    TkTextIndexBackChars(textPtr, indexPtr, count, indexPtr, type);
	}
    } else if (*units == 'l' && strncmp(units, "lines", length) == 0) {
	if (modifier == TKINDEX_DISPLAY) {
	    /*
	     * Find the appropriate pixel offset of the current position
	     * within its display line. This also has the side-effect of
	     * moving indexPtr, but that doesn't matter since we will do it
	     * again below.
	     *
	     * Then find the right display line, and finally calculated the
	     * index we want in that display line, based on the original pixel
	     * offset.
	     */

	    int xOffset;
	    bool forward;

	    /*
	     * Go forward to the first non-elided index.
	     */

	    if (TkTextIsElided(indexPtr)) {
		TkTextSkipElidedRegion(indexPtr);
	    }

	    if (count == 0) {
		return p;
	    }

	    /*
	     * Unlike the Forw/BackChars code, the display line code is
	     * sensitive to whether we are genuinely going forwards or
	     * backwards. So, we need to determine that. This is important in
	     * the case where we have "+ -3 displaylines", for example.
	     */

	    forward = (count < 0) == (*string == '-');
	    count = abs(count);
	    if (!forward) {
		count = -count;
	    }

	    TkTextFindDisplayIndex(textPtr, indexPtr, count, &xOffset);

	    /*
	     * This call assumes indexPtr is the beginning of a display line
	     * and moves it to the 'xOffset' position of that line, which is
	     * just what we want.
	     */

	    TkTextIndexOfX(textPtr, xOffset, indexPtr);

	    /*
	     * We must skip any elided range.
	     */

	    if (TkTextIsElided(indexPtr)) {
		TkTextSkipElidedRegion(indexPtr);
	    }
	} else {
	    lineIndex = TkBTreeLinesTo(indexPtr->tree, textPtr, indexPtr->priv.linePtr, NULL);
	    if (*string == '+') {
		lineIndex += count;
	    } else {
		lineIndex -= count;

		/*
		 * The check below retains the character position, even if the
		 * line runs off the start of the file. Without it, the
		 * character position will get reset to 0 by TkTextMakeIndex.
		 */

		if (lineIndex < 0) {
		    lineIndex = 0;
		}
	    }

	    /*
	     * This doesn't work quite right if using a proportional font or
	     * UTF-8 characters with varying numbers of bytes, or if there are
	     * embedded windows, images, etc. The cursor will bop around,
	     * keeping a constant number of bytes (not characters) from the
	     * left edge (but making sure not to split any UTF-8 characters),
	     * regardless of the x-position the index corresponds to. The
	     * proper way to do this is to get the x-position of the index and
	     * then pick the character at the same x-position in the new line.
	     */

	    if (textPtr->startMarker != textPtr->sharedTextPtr->startMarker) {
		indexPtr->priv.byteIndex += TkTextSegToIndex(textPtr->startMarker);
	    }
	    TkTextMakeByteIndex(indexPtr->tree, textPtr, lineIndex, indexPtr->priv.byteIndex, indexPtr);
	}
    } else {
	return NULL;
    }
    return p;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextIndexForwBytes --
 *
 *	Given an index for a text widget, this function creates a new index
 *	that points "byteCount" bytes ahead of the source index.
 *
 * Results:
 *	*dstPtr is modified to refer to the character "count" bytes after
 *	srcPtr, or to the last character in the TkText if there aren't "byteCount"
 *	bytes left.
 *
 *	In this latter case, the function returns '1' to indicate that not all
 *	of 'byteCount' could be used.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

int
TkTextIndexForwBytes(
    const TkText *textPtr,	/* Overall information about text widget, can be NULL. */
    const TkTextIndex *srcPtr,	/* Source index. */
    int byteCount,		/* How many bytes forward to move. May be negative. */
    TkTextIndex *dstPtr)	/* Destination index: gets modified. */
{
    TkTextLine *linePtr;
    int byteIndex;

    if (byteCount == 0) {
	if (dstPtr != srcPtr) {
	    *dstPtr = *srcPtr;
	}
	return 0;
    }

    if (byteCount < 0) {
	TkTextIndexBackBytes(textPtr, srcPtr, -byteCount, dstPtr);
	return 0;
    }

    if (dstPtr != srcPtr) {
	*dstPtr = *srcPtr;
    }

    TkTextIndexToByteIndex(dstPtr);
    linePtr = TkTextIndexGetLine(dstPtr);

    if (textPtr) {
	if (linePtr == TkBTreeGetLastLine(textPtr)) {
	    return 1;
	}
	if (textPtr->endMarker->sectionPtr->linePtr == linePtr) {
	    /*
	     * Special case: line contains end marker.
	     */

	    int lineLength = SegToIndex(linePtr, textPtr->endMarker);

	    if ((byteIndex = (dstPtr->priv.byteIndex += byteCount)) > lineLength) {
		assert(linePtr->nextPtr);
		TkTextIndexSetByteIndex2(dstPtr, linePtr->nextPtr, 0);
	    }
	    return byteIndex <= lineLength ? 0 : 1;
	}
    } else if (!linePtr->nextPtr) {
	return 1;
    }

    if ((byteIndex = dstPtr->priv.byteIndex + byteCount) > linePtr->size) {
	bool rc;
	DEBUG(TkTextIndex index = *srcPtr);
	rc = TkBTreeMoveForward(dstPtr, byteCount);
	assert(!rc || (int) TkTextIndexCountBytes(&index, dstPtr) == byteCount);
	return rc ? 0 : 1;
    }

    if (byteIndex == linePtr->size) {
	assert(linePtr->nextPtr);
	TkTextIndexSetByteIndex2(dstPtr, linePtr->nextPtr, 0);
    } else {
	TkTextIndexSetByteIndex(dstPtr, byteIndex);
    }

    return 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextIndexForwChars --
 *
 *	Given an index for a text widget, this function creates a new index
 *	that points "count" items of type given by "type" ahead of the source
 *	index. "count" can be zero, which is useful in the case where one
 *	wishes to move forward by display (non-elided) chars or indices or one
 *	wishes to move forward by chars, skipping any intervening indices. In
 *	this case dstPtr will point to the first acceptable index which is
 *	encountered.
 *
 * Results:
 *	*dstPtr is modified to refer to the character "count" items after
 *	srcPtr, or to the last character in the TkText if there aren't
 *	sufficient items left in the widget.
 *
 *	This function returns whether the resulting index is different from
 *	source index.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

bool
TkTextIndexForwChars(
    const TkText *textPtr,	/* Overall information about text widget, can be NULL. */
    const TkTextIndex *srcPtr,	/* Source index. */
    int charCount,		/* How many characters forward to move. May be negative. */
    TkTextIndex *dstPtr,	/* Destination index: gets modified. */
    TkTextCountType type)	/* The type of item to count */
{
    TkTextLine *linePtr;
    TkTextSegment *segPtr;
    TkTextSegment *endPtr;
    TkSharedText *sharedTextPtr;
    int byteOffset;
    bool checkElided;
    bool trimmed;
    bool skipSpaces;

    if (charCount < 0) {
	return TkTextIndexBackChars(textPtr, srcPtr, -charCount, dstPtr, type);
    }

    if (dstPtr != srcPtr) {
	*dstPtr = *srcPtr;
    }

    if (TkTextIndexIsEndOfText(dstPtr)) {
	return false;
    }

    sharedTextPtr = TkTextIndexGetShared(srcPtr);
    checkElided = !!(type & COUNT_DISPLAY) && TkBTreeHaveElidedSegments(sharedTextPtr);

    if (checkElided && TkTextIsElided(dstPtr) && !TkTextSkipElidedRegion(dstPtr)) {
	return false;
    }

    if (charCount == 0) {
	return false;
    }

    assert(dstPtr->priv.byteIndex <= FindEndByteIndex(dstPtr));

    /*
     * Find seg that contains src byteIndex. Move forward specified number of chars.
     */

    segPtr = TkTextIndexGetFirstSegment(dstPtr, &byteOffset);
    endPtr = textPtr ? textPtr->endMarker : sharedTextPtr->endMarker;
    TkTextIndexToByteIndex(dstPtr);
    trimmed = textPtr && textPtr->spaceMode == TEXT_SPACEMODE_TRIM && !!(type & COUNT_DISPLAY);
    skipSpaces = false;

    while (true) {
	/*
	 * Go through each segment in line looking for specified character index.
	 */

	for ( ; segPtr; segPtr = segPtr->nextPtr) {
	    if (segPtr->tagInfoPtr) {
		if (segPtr->typePtr == &tkTextCharType) {
		    const char *start = segPtr->body.chars + byteOffset;
		    const char *end = segPtr->body.chars + segPtr->size;
		    const char *p = start;
		    int ch, n;

		    for (p = start; p < end; p += n) {
			if (charCount <= 0) {
			    if (skipSpaces) {
				while (*p == ' ') {
				    ++p;
				}
				if (p == end) {
				    break;
				}
			    }
			    dstPtr->priv.byteIndex += (p - start);
			    goto forwardCharDone;
			}
			n = TkUtfToUniChar(p, &ch);
			if (ch == ' ') {
			    if (!skipSpaces) {
				skipSpaces = trimmed;
				charCount -= 1;
			    }
			} else {
			    skipSpaces = false;
			    charCount -= (type & COUNT_INDICES) ? n : 1;
			}
		    }
		} else if (type & COUNT_INDICES) {
		    assert(byteOffset == 0);
		    assert(segPtr->size <= 1);
		    if (charCount < segPtr->size) {
			dstPtr->priv.byteIndex += charCount;
			dstPtr->priv.segPtr = segPtr;
			dstPtr->priv.isCharSegment = false;
			goto forwardCharDone;
		    }
		    charCount -= segPtr->size;
		}
		dstPtr->priv.byteIndex += segPtr->size - byteOffset;
		byteOffset = 0;
	    } else if (checkElided && segPtr->typePtr == &tkTextBranchType) {
		TkTextIndexSetSegment(dstPtr, segPtr = segPtr->body.branch.nextPtr);
		if (TkTextIndexRestrictToEndRange(dstPtr) >= 0) {
		    goto forwardCharDone;
		}
		TkTextIndexToByteIndex(dstPtr);
	    } else if (segPtr == endPtr) {
		if (charCount > 0) {
		    TkTextIndexSetupToEndOfText(dstPtr, (TkText *) textPtr, srcPtr->tree);
		}
		goto forwardCharDone;
	    }
	}

	/*
	 * Go to the next line. If we are at the end of the text item, back up
	 * one byte (for the terminal '\n' character) and return that index.
	 */

	if (!(linePtr = TkBTreeNextLine(textPtr, dstPtr->priv.linePtr))) {
	    TkTextIndexSetToLastChar(dstPtr);
	    goto forwardCharDone;
	}
	dstPtr->priv.linePtr = linePtr;
	dstPtr->priv.byteIndex = 0;
	dstPtr->priv.lineNo = -1;
	dstPtr->priv.lineNoRel = -1;
	segPtr = linePtr->segPtr;
    }

  forwardCharDone:
    dstPtr->stateEpoch = TkBTreeEpoch(dstPtr->tree);
    return true;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextSkipElidedRegion --
 *
 *	Given an index for a text widget, this function returns an index with
 *	the position of first un-elided character, or end of text, if the first
 *	un-elided character is beyond of this text widget. This functions assumes
 *	that the text position specified with incoming index is elided.
 *
 * Results:
 *	*indexPtr is modified to refer to the first un-elided character. This
 *	functions returns 'false' iff we reach the end of the text (belonging to
 *	this widget).
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

bool
TkTextSkipElidedRegion(
    TkTextIndex *indexPtr)
{
    TkTextSegment *segPtr;

    assert(indexPtr->textPtr);
    assert(TkTextIsElided(indexPtr));

    segPtr = TkBTreeFindEndOfElidedRange(indexPtr->textPtr->sharedTextPtr,
	    indexPtr->textPtr, TkTextIndexGetContentSegment(indexPtr, NULL));
    TkTextIndexSetSegment(indexPtr, segPtr);
    return !TkTextIndexIsEndOfText(indexPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextIndexCountBytes --
 *
 *	Given a pair of indices in a text widget, this function counts how
 *	many bytes are between the two indices. The two indices must be
 *	ordered.
 *
 * Results:
 *	The number of bytes in the given range.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

unsigned
TkTextIndexCountBytes(
    const TkTextIndex *indexPtr1,	/* Index describing location of character from which to count. */
    const TkTextIndex *indexPtr2)	/* Index describing location of last character at which to
    					 * stop the count. */
{
    int byteCount;
    TkTextLine *linePtr;

    assert(TkTextIndexCompare(indexPtr1, indexPtr2) <= 0);

    if (indexPtr1->priv.linePtr == indexPtr2->priv.linePtr) {
        return TkTextIndexGetByteIndex(indexPtr2) - TkTextIndexGetByteIndex(indexPtr1);
    }

    /*
     * indexPtr2 is on a line strictly after the line containing indexPtr1.
     * Add up:
     *   bytes between indexPtr1 and end of its line
     *   bytes in lines strictly between indexPtr1 and indexPtr2
     *   bytes between start of the indexPtr2 line and indexPtr2
     */

    linePtr = indexPtr1->priv.linePtr;
    byteCount = linePtr->size - TkTextIndexGetByteIndex(indexPtr1);
    byteCount += TkTextIndexGetByteIndex(indexPtr2);
    byteCount += TkBTreeCountSize(indexPtr1->tree, NULL, linePtr->nextPtr, indexPtr2->priv.linePtr);
    return byteCount;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextIndexGetChar --
 *
 *	Return the character at given index.
 *
 * Results:
 *	The character at given index.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

Tcl_UniChar
TkTextIndexGetChar(
    const TkTextIndex *indexPtr)/* Index describing location of character. */
{
    TkTextSegment *segPtr;
    int byteOffset;
    int ch;

    segPtr = TkTextIndexGetContentSegment(indexPtr, &byteOffset);
    TkUtfToUniChar(segPtr->body.chars + byteOffset, &ch);
    return ch;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextIndexCount --
 *
 *	Given an ordered pair of indices in a text widget, this function
 *	counts how many characters (not bytes) are between the two indices.
 *
 *	It is illegal to call this function with unordered indices.
 *
 *	Note that 'textPtr' is only used if we need to check for elided
 *	attributes, i.e. if type is COUNT_DISPLAY_INDICES or
 *	COUNT_DISPLAY_CHARS.
 *
 *	NOTE: here COUNT_INDICES is also counting chars in text segments.
 *
 * Results:
 *	The number of characters in the given range, which meet the
 *	appropriate 'type' attributes.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

unsigned
TkTextIndexCount(
    const TkText *textPtr,	/* Overall information about text widget. */
    const TkTextIndex *indexPtr1,
				/* Index describing location of character from which to count. */
    const TkTextIndex *indexPtr2,
				/* Index describing location of last character at which to stop
				 * the count. */
    TkTextCountType type)	/* The kind of indices to count. */
{
    TkTextLine *linePtr;
    TkTextIndex index;
    TkTextSegment *segPtr, *lastPtr;
    int byteOffset, maxBytes;
    unsigned count;
    bool checkElided;

    assert(textPtr);
    assert(TkTextIndexCompare(indexPtr1, indexPtr2) <= 0);

    checkElided = !!(type & COUNT_DISPLAY) && TkBTreeHaveElidedSegments(textPtr->sharedTextPtr);
    index = *indexPtr1;

    if (checkElided
	    && TkTextIsElided(&index)
	    && (!TkTextSkipElidedRegion(&index) || TkTextIndexCompare(&index, indexPtr2) >= 0)) {
	return 0;
    }

    /*
     * Find seg that contains src index, and remember how many bytes not to count in the given segment.
     */

    segPtr = TkTextIndexGetContentSegment(&index, &byteOffset);
    lastPtr = TkTextIndexGetContentSegment(indexPtr2, &maxBytes);
    linePtr = index.priv.linePtr;
    count = 0;

    if (byteOffset > 0) {
	if (segPtr->tagInfoPtr) {
	    if (segPtr->typePtr == &tkTextCharType) {
		if (type & (COUNT_INDICES|COUNT_TEXT)) {
		    count += Tcl_NumUtfChars(segPtr->body.chars + byteOffset,
			    (segPtr == lastPtr ? maxBytes : segPtr->size) - byteOffset);
		}
	    } else if (segPtr->typePtr == &tkTextHyphenType) {
		if (type & (COUNT_HYPHENS|COUNT_INDICES)) {
		    count += 1;
		}
	    } else if (type & COUNT_INDICES) {
		assert(segPtr->size == 1);
		count += 1;
	    }
	}
	if (segPtr == lastPtr) {
	    return count;
	}
	segPtr = segPtr->nextPtr;
    }

    if (maxBytes > 0 && (!checkElided || !TkTextSegmentIsElided(textPtr, lastPtr))) {
	if (lastPtr->typePtr == &tkTextCharType) {
	    if (type & (COUNT_TEXT|COUNT_INDICES)) {
		count += Tcl_NumUtfChars(lastPtr->body.chars, maxBytes);
	    }
	} else if (lastPtr->typePtr == &tkTextHyphenType) {
	    if (type & (COUNT_HYPHENS|COUNT_INDICES)) {
		count += 1;
	    }
	} else if (type & COUNT_INDICES) {
	    assert(segPtr->size <= 1);
	    count += 1;
	}
    }

    while (true) {
	/*
	 * Go through each segment in line adding up the number of characters.
	 */

	for ( ; segPtr; segPtr = segPtr->nextPtr) {
	    if (segPtr == lastPtr) {
		return count;
	    }
	    if (segPtr->tagInfoPtr) {
		if (segPtr->typePtr == &tkTextCharType) {
		    if (type & (COUNT_TEXT|COUNT_INDICES)) {
			count += CountCharsInSeg(segPtr);
		    }
		} else if (segPtr->typePtr == &tkTextHyphenType) {
		    if (type & (COUNT_HYPHENS|COUNT_INDICES)) {
			count += CountCharsInSeg(segPtr);
		    }
		} else if (type & COUNT_INDICES) {
		    assert(segPtr->size == 1);
		    count += 1;
		}
	    } else if (checkElided && segPtr->typePtr == &tkTextBranchType) {
		TkTextIndexSetSegment(&index, segPtr = segPtr->body.branch.nextPtr);
		if (TkTextIndexCompare(&index, indexPtr2) >= 0) {
		    return count;
		}
		linePtr = TkTextIndexGetLine(&index);
	    }
	}

	/*
	 * Go to the next line.
	 */

	linePtr = TkBTreeNextLine(textPtr, linePtr);
	assert(linePtr);
	segPtr = linePtr->segPtr;
    }

    return 0; /* never reached */
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextIndexBackBytes --
 *
 *	Given an index for a text widget, this function creates a new index
 *	that points "count" bytes earlier than the source index.
 *
 * Results:
 *	*dstPtr is modified to refer to the character "count" bytes before
 *	srcPtr, or to the first character in the TkText if there aren't
 *	"count" bytes earlier than srcPtr.
 *
 *	Returns 1 if we couldn't use all of 'byteCount' because we have run
 *	into the beginning or end of the text, and zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

/*
 * NOTE: This function has external linkage, so we cannot change the return
 * type to 'bool'.
 */

int
TkTextIndexBackBytes(
    const TkText *textPtr,	/* Overall information about text widget, can be NULL. */
    const TkTextIndex *srcPtr,	/* Source index. */
    int byteCount,		/* How many bytes backward to move. May be negative. */
    TkTextIndex *dstPtr)	/* Destination index: gets modified. */
{
    TkTextLine *linePtr;
    int byteIndex;

    if (byteCount == 0) {
	if (dstPtr != srcPtr) {
	    *dstPtr = *srcPtr;
	}
	return 0;
    }

    if (byteCount < 0) {
	return TkTextIndexForwBytes(textPtr, srcPtr, -byteCount, dstPtr);
    }

    if (dstPtr != srcPtr) {
	*dstPtr = *srcPtr;
    }
    byteIndex = TkTextIndexGetByteIndex(dstPtr);
    linePtr = TkTextIndexGetLine(dstPtr);

    if (textPtr
	    && linePtr == textPtr->startMarker->sectionPtr->linePtr
	    && textPtr->startMarker != textPtr->sharedTextPtr->startMarker) {
	/*
	 * Special case: this is the first line, and we have to consider the start marker.
	 */
	
	if ((byteIndex -= byteCount) < SegToIndex(linePtr, textPtr->startMarker)) {
	    TkTextIndexSetupToStartOfText(dstPtr, (TkText *) textPtr, textPtr->sharedTextPtr->tree);
	    return 1;
	}

	TkTextIndexSetByteIndex(dstPtr, byteIndex);
	return 0;
    }

    if (byteCount > byteIndex + 1) {
	bool rc;
	DEBUG(TkTextIndex index = *srcPtr);
	rc = TkBTreeMoveBackward(dstPtr, byteCount);
	assert(!rc || (int) TkTextIndexCountBytes(dstPtr, &index) == byteCount);
	return rc ? 0 : 1;
    }

    if ((byteIndex -= byteCount) >= 0) {
	TkTextIndexSetByteIndex(dstPtr, byteIndex);
	return 0;
    }

    /*
     * Move back one line in the text. If we run off the beginning
     * then just return the first character in the text widget.
     */

    if (!(linePtr = TkBTreePrevLine(textPtr, linePtr))) {
	TkTextIndexSetToStartOfLine(dstPtr);
	return 1;
    }

    TkTextIndexSetToLastChar2(dstPtr, linePtr);
    return 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextIndexBackChars --
 *
 *	Given an index for a text widget, this function creates a new index
 *	that points "count" items of type given by "type" earlier than the
 *	source index. "count" can be zero, which is useful in the case where
 *	one wishes to move backward by display (non-elided) chars or indices
 *	or one wishes to move backward by chars, skipping any intervening
 *	indices. In this case the returned index *dstPtr will point just
 *	_after_ the first acceptable index which is encountered.
 *
 * Results:
 *	*dstPtr is modified to refer to the character "count" items before
 *	srcPtr, or to the first index in the window if there aren't sufficient
 *	items earlier than srcPtr.
 *
 *	This function returns whether the resulting index is different from
 *	source index.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

bool
TkTextIndexBackChars(
    const TkText *textPtr,	/* Overall information about text widget, can be NULL. */
    const TkTextIndex *srcPtr,	/* Source index. */
    int charCount,		/* How many characters backward to move. May be negative. */
    TkTextIndex *dstPtr,	/* Destination index: gets modified. */
    TkTextCountType type)	/* The type of item to count */
{
    TkSharedText *sharedTextPtr;
    TkTextSegment *segPtr;
    TkTextSegment *startPtr;
    TkTextLine *linePtr;
    int segSize;
    bool checkElided;
    bool trimmed;
    bool skipSpaces;
    int byteIndex;

    assert(textPtr || !(type & COUNT_DISPLAY));

    if (charCount < 0) {
	return TkTextIndexForwChars(textPtr, srcPtr, -charCount, dstPtr, type);
    }

    if (dstPtr != srcPtr) {
	*dstPtr = *srcPtr;
    }

    if (charCount == 0) {
	return false;
    }

    sharedTextPtr = TkTextIndexGetShared(srcPtr);
    checkElided = !!(type & COUNT_DISPLAY) && TkBTreeHaveElidedSegments(textPtr->sharedTextPtr);

    if (checkElided && TkTextIsElided(dstPtr) && !TkTextSkipElidedRegion(dstPtr)) {
	return false;
    }

    if (TkTextIndexIsStartOfLine(dstPtr) && !TkBTreePrevLine(textPtr, dstPtr->priv.linePtr)) {
	return false;
    }

    if (textPtr && textPtr->endMarker != sharedTextPtr->endMarker) {
	linePtr = TkBTreeGetLastLine(textPtr);

	if (dstPtr->priv.linePtr == linePtr && linePtr != textPtr->endMarker->sectionPtr->linePtr) {
	    /*
	     * Special case: we are at start of last line, but this is not the end line.
	     */

	    if (--charCount == 0) {
		byteIndex = TkTextSegToIndex(textPtr->endMarker);
		dstPtr->priv.linePtr = textPtr->endMarker->sectionPtr->linePtr;
		dstPtr->priv.segPtr = NULL;
		dstPtr->priv.lineNo = dstPtr->priv.lineNoRel = -1;
		goto backwardCharDone;
	    }
	    TkTextIndexSetSegment(dstPtr, textPtr->endMarker);
	}
    }

    /*
     * Move backward specified number of chars.
     */

    segPtr = TkTextIndexGetFirstSegment(dstPtr, &segSize);
    startPtr = textPtr ? textPtr->startMarker : sharedTextPtr->startMarker;
    byteIndex = TkTextIndexGetByteIndex(dstPtr);
    dstPtr->priv.segPtr = NULL;

    /*
     * Now segPtr points to the segment containing the starting index.
     */

    trimmed = textPtr && textPtr->spaceMode == TEXT_SPACEMODE_TRIM && !!(type & COUNT_DISPLAY);
    skipSpaces = false;

    while (true) {
	/*
	 * If we do need to pay attention to the visibility of
	 * characters/indices, check that first. If the current segment isn't
	 * visible, then we simply continue the loop.
	 */

	if (segPtr->tagInfoPtr) {
	    if (segPtr->typePtr == &tkTextCharType) {
		const char *start = segPtr->body.chars;
		const char *end = segPtr->body.chars + segSize;
		const char *p = end;

		while (true) {
		    const char *q;

		    if (charCount <= 0) {
			if (skipSpaces) {
			    while (p > start && p[-1] == ' ') {
				--p;
			    }
			    if (p == start) {
				break;
			    }
			}
			byteIndex -= (end - p);
			goto backwardCharDone;
		    }
		    if (p == start) {
			break;
		    }
		    p = Tcl_UtfPrev(q = p, start);
		    if (*p == ' ') {
			if (!skipSpaces) {
			    skipSpaces = trimmed;
			    charCount -= 1;
			}
		    } else {
			skipSpaces = false;
			charCount -= (type & COUNT_INDICES) ? q - p : 1;
		    }
		}
	    } else if (type & COUNT_INDICES) {
		assert(segPtr->size <= 1);
		if (charCount <= segSize) {
		    byteIndex -= charCount;
		    dstPtr->priv.segPtr = segPtr;
		    dstPtr->priv.isCharSegment = false;
		    goto backwardCharDone;
		}
		charCount -= segSize;
	    }
	} else if (checkElided && segPtr->typePtr == &tkTextLinkType) {
	    TkTextIndexSetSegment(dstPtr, segPtr = segPtr->body.link.prevPtr);
	    dstPtr->priv.segPtr = segPtr;
	    dstPtr->priv.isCharSegment = false;
	    if (TkTextIndexRestrictToStartRange(dstPtr) <= 0) {
		dstPtr->stateEpoch = TkBTreeEpoch(dstPtr->tree);
		return true;
	    }
	    TkTextIndexToByteIndex(dstPtr);
	    byteIndex = TkTextIndexGetByteIndex(dstPtr);
	} else if (segPtr == startPtr) {
	    TkTextIndexSetSegment(dstPtr, segPtr = startPtr);
	    byteIndex = TkTextIndexGetByteIndex(dstPtr);
	    goto backwardCharDone;
	}

	/*
	 * Move back into previous segment.
	 */

	if (!(segPtr = segPtr->prevPtr)) {
	    /*
	     * Move back to previous line
	     */

	    linePtr = TkBTreePrevLine(textPtr, dstPtr->priv.linePtr);
	    assert(linePtr);

	    dstPtr->priv.linePtr = linePtr;
	    dstPtr->priv.lineNo = -1;
	    dstPtr->priv.lineNoRel = -1;
	    byteIndex = linePtr->size;
	    segPtr = linePtr->lastPtr;
	} else {
	    byteIndex -= segSize;
	}

	segSize = segPtr->size;
    }

  backwardCharDone:
    dstPtr->stateEpoch = TkBTreeEpoch(dstPtr->tree);
    dstPtr->priv.byteIndex = byteIndex;
    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * StartEnd --
 *
 *	This function handles modifiers like "wordstart" and "lineend" to
 *	adjust indices forwards or backwards.
 *
 * Results:
 *	If the modifier is successfully parsed then the return value is the
 *	address of the first character after the modifier, and *indexPtr is
 *	updated to reflect the modifier. If there is a syntax error in the
 *	modifier then NULL is returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static const char *
StartEnd(
    TkText *textPtr,		/* Information about text widget. */
    const char *string,		/* String to parse for additional info about modifier (count and units).
    				 * Points to first character of modifier word. */
    TkTextIndex *indexPtr)	/* Index to modify based on string. */
{
    const char *p;
    size_t length;
    TkTextSegment *segPtr;
    int modifier;
    int mode;

    assert(textPtr);

    /*
     * Find the end of the modifier word.
     */

    for (p = string; isalnum(*p); ++p) {
	/* Empty loop body. */
    }
    length = p - string;

    if (*string == 'd' && strncmp(string, "display", MIN(length, 7)) == 0) {
	modifier = TKINDEX_DISPLAY;
	mode = COUNT_DISPLAY_INDICES;
	if (length > 7) {
	    p -= (length - 7);
	}
    } else if (*string == 'a' && strncmp(string, "any", MIN(length, 3)) == 0) {
	modifier = TKINDEX_CHAR;
	mode = COUNT_CHARS;
	if (length > 3) {
	    p -= (length - 3);
	}
    } else {
	modifier = TKINDEX_NONE;
	mode = COUNT_INDICES;
    }

    /*
     * If we had a modifier, which we interpreted ok, so now forward to the actual units.
     */

    if (modifier != TKINDEX_NONE) {
	while (isspace(*p)) {
	    ++p;
	}
	string = p;
	while (*p && !isspace(*p) && *p != '+' && *p != '-') {
	    ++p;
	}
	length = p - string;
    }

    if (*string == 'l' && strncmp(string, "lineend", length) == 0 && length >= 5) {
	if (modifier == TKINDEX_DISPLAY) {
	    TkTextFindDisplayLineStartEnd(textPtr, indexPtr, DISP_LINE_END);
	} else {
	    TkTextIndexSetToLastChar(indexPtr);
	}
    } else if (*string == 'l' && strncmp(string, "linestart", length) == 0 && length >= 5) {
	if (modifier == TKINDEX_DISPLAY) {
	    TkTextFindDisplayLineStartEnd(textPtr, indexPtr, DISP_LINE_START);
	} else {
	    TkTextIndexSetToStartOfLine(indexPtr);
	}
    } else if (*string == 'w' && strncmp(string, "wordend", length) == 0 && length >= 5) {
	int firstChar = 1;
	int offset;

	/*
	 * If the current character isn't part of a word then just move
	 * forward one character. Otherwise move forward until finding a
	 * character that isn't part of a word and stop there.
	 */

	if (modifier == TKINDEX_DISPLAY) {
	    TkTextIndexForwChars(textPtr, indexPtr, 0, indexPtr, COUNT_DISPLAY_INDICES);
	}
	segPtr = TkTextIndexGetContentSegment(indexPtr, &offset);
	while (true) {
	    int chSize = 1;

	    if (segPtr->typePtr == &tkTextCharType) {
		int ch;

		chSize = TkUtfToUniChar(segPtr->body.chars + offset, &ch);
		if (!Tcl_UniCharIsWordChar(ch)) {
		    break;
		}
		firstChar = 0;
	    }
	    offset += chSize;
	    indexPtr->priv.byteIndex += chSize;
	    if (offset >= segPtr->size) {
		do {
		    segPtr = segPtr->nextPtr;
		} while (segPtr->size == 0);
		offset = 0;
	    }
	}
	if (firstChar) {
	    TkTextIndexForwChars(textPtr, indexPtr, 1, indexPtr, mode);
	}
    } else if (*string == 'w' && strncmp(string, "wordstart", length) == 0 && length >= 5) {
	int firstChar = 1;
	int offset;

	if (modifier == TKINDEX_DISPLAY) {
	    /*
	     * Skip elided region.
	     */
	    TkTextIndexForwChars(textPtr, indexPtr, 0, indexPtr, COUNT_DISPLAY_INDICES);
	}

	/*
	 * Starting with the current character, look for one that's not part
	 * of a word and keep moving backward until you find one. Then if the
	 * character found wasn't the first one, move forward again one
	 * position.
	 */

	segPtr = TkTextIndexGetContentSegment(indexPtr, &offset);
	while (true) {
	    int chSize = 1;

	    if (segPtr->typePtr == &tkTextCharType) {
		int ch;

		TkUtfToUniChar(segPtr->body.chars + offset, &ch);
		if (!Tcl_UniCharIsWordChar(ch)) {
		    break;
		}
		if (offset > 0) {
		    const char *prevPtr = Tcl_UtfPrev(segPtr->body.chars + offset, segPtr->body.chars);
		    chSize = segPtr->body.chars + offset - prevPtr;
		}
		firstChar = 0;
	    }
            if (offset == 0) {
		TkTextIndexBackChars(textPtr, indexPtr, 1, indexPtr, mode);
		segPtr = TkTextIndexGetContentSegment(indexPtr, &offset);

		if (offset < chSize && indexPtr->priv.byteIndex == 0) {
		    return p;
		}
            } else if ((indexPtr->priv.byteIndex -= chSize) == 0) {
		return p;
	    } else if ((offset -= chSize) < 0) {
		assert(indexPtr->priv.byteIndex > 0);
		do {
		    segPtr = segPtr->prevPtr;
		    assert(segPtr);
		} while (segPtr->size == 0);
		offset = 0;
	    }
	}

	if (!firstChar) {
	    TkTextIndexForwChars(textPtr, indexPtr, 1, indexPtr, mode);
	}
    } else {
	p = NULL;
    }

    return p;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 * vi:set ts=8 sw=4:
 */
