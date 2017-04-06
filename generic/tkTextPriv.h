/*
 * tkTextPriv.h --
 *
 *	Private implementation for text widget.
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKTEXT
# error "do not include this private header file"
#endif


#ifndef _TKTEXTPRIV
#define _TKTEXTPRIV

/*
 * The following struct is private for TkTextBTree.c, but we want fast access to
 * the internal content.
 *
 * The data structure below defines an entire B-tree. Since text widgets are
 * the only current B-tree clients, 'clients' and 'numPixelReferences' are
 * identical.
 */

struct TkBTreeNodePixelInfo;

struct TkTextMyBTree {
    struct Node *rootPtr;
				/* Pointer to root of B-tree. */
    unsigned clients;		/* Number of clients of this B-tree. */
    unsigned numPixelReferences;
				/* Number of clients of this B-tree which care about pixel heights. */
    struct TkBTreeNodePixelInfo *pixelInfoBuffer;
    				/* Buffer of size numPixelReferences used for recomputation of pixel
    				 * information. */
    unsigned stateEpoch;	/* Updated each time any aspect of the B-tree changes. */
    TkSharedText *sharedTextPtr;/* Used to find tagTable in consistency checking code, and to access
    				 * list of all B-tree clients. */
};


MODULE_SCOPE bool TkpTextGetIndex(Tcl_Interp *interp, TkSharedText *sharedTextPtr, TkText *textPtr,
			    const char *string, unsigned lenOfString, TkTextIndex *indexPtr);

#endif /* _TKTEXTPRIV */

#ifdef _TK_NEED_IMPLEMENTATION

#include <assert.h>

/*
 *----------------------------------------------------------------------
 *
 * TkTextIsSpecialMark --
 *
 *	Test whether this is a special mark: "insert", or "current".
 *
 * Results:
 *	Whether this is a special mark.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
bool
TkTextIsSpecialMark(
    const TkTextSegment *segPtr)
{
    assert(segPtr);
    return !!(segPtr->insertMarkFlag | segPtr->currentMarkFlag);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIsPrivateMark --
 *
 *	Test whether this is a private mark, not visible with "inspect"
 *	or "dump". These kind of marks will be used in library/text.tcl.
 *	Furthemore in practice it is guaranteed that this mark has a
 *	unique name.
 *
 * Results:
 *	Whether this is a private mark.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
bool
TkTextIsPrivateMark(
    const TkTextSegment *segPtr)
{
    assert(segPtr);
    return segPtr->privateMarkFlag;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIsNormalMark --
 *
 *	Test whether this is a mark, and it is neither special, nor
 *	private, nor a start/end marker.
 *
 * Results:
 *	Whether this is a normal mark.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
bool
TkTextIsNormalMark(
    const TkTextSegment *segPtr)
{
    assert(segPtr);
    return segPtr->normalMarkFlag;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIsStartEndMarker --
 *
 *	Test whether this is a start/end marker. This must not be a mark,
 *	it can also be a break segment.
 *
 * Results:
 *	Whether this is a start/end marker.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
bool
TkTextIsStartEndMarker(
    const TkTextSegment *segPtr)
{
    assert(segPtr);
    return segPtr->startEndMarkFlag;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIsStableMark --
 *
 *	Test whether this is a mark, and it is neither special, nor
 *	private. Note that also a break segment is interpreted as
 *	a stable mark.
 *
 * Results:
 *	Whether this is a stable mark.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
bool
TkTextIsStableMark(
    const TkTextSegment *segPtr)
{
    return TkTextIsStartEndMarker(segPtr) || TkTextIsNormalMark(segPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIsSpecialOrPrivateMark --
 *
 *	Test whether this is a special mark, or a private mark.
 *
 * Results:
 *	Whether this is a special or private mark.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
bool
TkTextIsSpecialOrPrivateMark(
    const TkTextSegment *segPtr)
{
    assert(segPtr);
    return !!(segPtr->privateMarkFlag | segPtr->insertMarkFlag | segPtr->currentMarkFlag);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIsNormalOrSpecialMark --
 *
 *	Test whether this is a normal mark, or a special mark.
 *
 * Results:
 *	Whether this is a normal or special mark.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
bool
TkTextIsNormalOrSpecialMark(
    const TkTextSegment *segPtr)
{
    return TkTextIsNormalMark(segPtr) || TkTextIsSpecialMark(segPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIsDeadPeer --
 *
 *	Test whether given widget is dead, this means that the start
 *	index is on last line. If it is dead, then this peer will not
 *	have an insert mark.
 *
 * Results:
 *	Returns whether given peer is dead.
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

inline
bool
TkTextIsDeadPeer(
    const TkText *textPtr)
{
    return !textPtr->startMarker->sectionPtr->linePtr->nextPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeLinePixelInfo --
 *
 *	Return widget pixel information for specified line.
 *
 * Results:
 *	The pixel information of this line for specified widget.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
TkTextPixelInfo *
TkBTreeLinePixelInfo(
    const TkText *textPtr,
    TkTextLine *linePtr)
{
    assert(textPtr);
    assert(textPtr->pixelReference >= 0);
    assert(linePtr);

    return linePtr->pixelInfo + textPtr->pixelReference;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeGetStartLine --
 *
 *	This function returns the first line for this text widget.
 *
 * Results:
 *	The first line in this widget.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
TkTextLine *
TkBTreeGetStartLine(
    const TkText *textPtr)
{
    assert(textPtr);
    return textPtr->startMarker->sectionPtr->linePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeGetLastLine --
 *
 *	This function returns the last line for this text widget.
 *
 * Results:
 *	The last line in this widget.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
TkTextLine *
TkBTreeGetLastLine(
    const TkText *textPtr)
{
    TkTextLine *endLine;

    assert(textPtr);
    endLine = textPtr->endMarker->sectionPtr->linePtr;
    return endLine->nextPtr ? endLine->nextPtr : endLine;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeGetShared --
 *
 *	Get the shared resource for given tree.
 *
 * Results:
 *	The shared resource.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
TkSharedText *
TkBTreeGetShared(
    TkTextBTree tree)		/* Return shared resource of this tree. */
{
    return ((struct TkTextMyBTree *) tree)->sharedTextPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeIncrEpoch --
 *
 *	Increment the epoch of the tree.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Increment the epoch number.
 *
 *----------------------------------------------------------------------
 */

inline
unsigned
TkBTreeIncrEpoch(
    TkTextBTree tree)		/* Tree to increment epoch. */
{
    return ((struct TkTextMyBTree *) tree)->stateEpoch += 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeEpoch --
 *
 *	Return the epoch for the B-tree. This number is incremented any time
 *	anything changes in the tree.
 *
 * Results:
 *	The epoch number.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
unsigned
TkBTreeEpoch(
    TkTextBTree tree)		/* Tree to get epoch for. */
{
    return ((struct TkTextMyBTree *) tree)->stateEpoch;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeGetRoot --
 *
 *	Return the root node of the B-Tree.
 *
 * Results:
 *	The root node of the B-Tree.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
struct Node *
TkBTreeGetRoot(
    TkTextBTree tree)		/* Tree to get root node for. */
{
    return ((struct TkTextMyBTree *) tree)->rootPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeNextLine --
 *
 *	Given an existing line in a B-tree, this function locates the next
 *	line in the B-tree, regarding the end line of this widget.
 *	B-tree.
 *
 * Results:
 *	The return value is a pointer to the line that immediately follows
 *	linePtr, or NULL if there is no such line.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
TkTextLine *
TkBTreeNextLine(
    const TkText *textPtr,	/* Next line in the context of this client, can be NULL. */
    TkTextLine *linePtr)	/* Pointer to existing line in B-tree. */
{
    return textPtr && linePtr == TkBTreeGetLastLine(textPtr) ? NULL : linePtr->nextPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreePrevLine --
 *
 *	Given an existing line in a B-tree, this function locates the previous
 *	line in the B-tree, regarding the start line of this widget.
 *
 * Results:
 *	The return value is a pointer to the line that immediately preceeds
 *	linePtr, or NULL if there is no such line.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
TkTextLine *
TkBTreePrevLine(
    const TkText *textPtr,	/* Relative to this client of the B-tree, can be NULL. */
    TkTextLine *linePtr)	/* Pointer to existing line in B-tree. */
{
    return textPtr && linePtr == TkBTreeGetStartLine(textPtr) ? NULL : linePtr->prevPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreePrevLogicalLine --
 *
 *	Given a line, this function is searching for the previous logical line,
 *	which don't has a predecessing line with elided newline. If the search
 *	reaches the start of the text, then the first line will be returned,
 *	even if it's not a logical line (the latter can only happen in peers
 *	with restricted ranges).
 *
 * Results:
 *	The return value is the previous logical line, in most cases this
 *	will be simply the previous line.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
TkTextLine *
TkBTreePrevLogicalLine(
    const TkSharedText* sharedTextPtr,
    const TkText *textPtr,	/* can be NULL */
    TkTextLine *linePtr)
{
    assert(linePtr);
    assert(linePtr != (textPtr ?
	    TkBTreeGetStartLine(textPtr) : sharedTextPtr->startMarker->sectionPtr->linePtr));

    return TkBTreeGetLogicalLine(sharedTextPtr, textPtr, linePtr->prevPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeCountLines --
 *
 *	This function counts the number of lines inside a given range.
 *
 * Results:
 *	The return value is the number of lines inside a given range.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
unsigned
TkBTreeCountLines(
    const TkTextBTree tree,
    const TkTextLine *linePtr1,	/* Start counting at this line. */
    const TkTextLine *linePtr2)	/* Stop counting at this line (don't count this line). */
{
    assert(TkBTreeLinesTo(tree, NULL, linePtr1, NULL) <= TkBTreeLinesTo(tree, NULL, linePtr2, NULL));

    if (linePtr1 == linePtr2) {
	return 0; /* this is catching a frequent case */
    }
    if (linePtr1->nextPtr == linePtr2) {
	return 1; /* this is catching a frequent case */
    }

    return TkBTreeLinesTo(tree, NULL, linePtr2, NULL) - TkBTreeLinesTo(tree, NULL, linePtr1, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetPeer --
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

inline
void
TkTextIndexSetPeer(
    TkTextIndex *indexPtr,
    TkText *textPtr)
{
    assert(indexPtr->tree);

    indexPtr->textPtr = textPtr;
    indexPtr->priv.lineNoRel = -1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexGetShared --
 *
 *	Get the shared resource of this index.
 *
 * Results:
 *	The shared resource.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
TkSharedText *
TkTextIndexGetShared(
    const TkTextIndex *indexPtr)
{
    assert(indexPtr);
    assert(indexPtr->tree);
    return TkBTreeGetShared(indexPtr->tree);
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeGetTags --
 *
 *	Return information about all of the tags that are associated with a
 *	particular character in a B-tree of text.
 *
 * Results:
 *      The return value is the root of the tag chain, containing all tags
 *	associated with the character at the position given by linePtr and ch.
 *	If there are no tags at the given character then a NULL pointer is
 *	returned.
 *
 * Side effects:
 *	The attribute nextPtr of TkTextTag will be modified for any tag.
 *
 *----------------------------------------------------------------------
 */

inline
TkTextTag *
TkBTreeGetTags(
    const TkTextIndex *indexPtr)/* Indicates a particular position in the B-tree. */
{
    const TkTextSegment *segPtr = TkTextIndexGetContentSegment(indexPtr, NULL);
    return TkBTreeGetSegmentTags(TkTextIndexGetShared(indexPtr), segPtr, indexPtr->textPtr, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexGetLine --
 *
 *	Get the line pointer of this index.
 *
 * Results:
 *	The line pointer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
TkTextLine *
TkTextIndexGetLine(
    const TkTextIndex *indexPtr)/* Indicates a particular position in the B-tree. */
{
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */

    return indexPtr->priv.linePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetToLastChar2 --
 *
 *	Set the new line pointer, and set this index to one before the
 *	end of the line.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
void
TkTextIndexSetToLastChar2(
    TkTextIndex *indexPtr,	/* Pointer to index. */
    TkTextLine *linePtr)	/* Pointer to line. */
{
    assert(indexPtr->tree);
    assert(linePtr);
    assert(linePtr->parentPtr); /* expired? */

    indexPtr->priv.linePtr = linePtr;
    indexPtr->priv.lineNo = -1;
    indexPtr->priv.lineNoRel = -1;
    TkTextIndexSetToLastChar(indexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexGetSegment --
 *
 *	Get the pointer to stored segment.
 *
 * Results:
 *	Pointer to the stored segment, this can be NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
TkTextSegment *
TkTextIndexGetSegment(
    const TkTextIndex *indexPtr)/* Pointer to index. */
{
    TkTextSegment *segPtr;

    assert(indexPtr->tree);
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */

    segPtr = indexPtr->priv.segPtr;

    if (!segPtr
	    || (indexPtr->priv.isCharSegment
		&& TkBTreeEpoch(indexPtr->tree) != indexPtr->stateEpoch)) {
	return NULL;
    }

    assert(!segPtr || segPtr->typePtr);    /* expired? */
    assert(!segPtr || segPtr->sectionPtr); /* linked? */
    assert(!segPtr || segPtr->sectionPtr->linePtr == indexPtr->priv.linePtr);

    return segPtr;
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

inline
bool
TkTextGetIndexFromObj(
    Tcl_Interp *interp,		/* Use this for error reporting. */
    TkText *textPtr,		/* Information about text widget, can be NULL. */
    Tcl_Obj *objPtr,		/* Object containing description of position. */
    TkTextIndex *indexPtr)	/* Store the result here. */
{
    int length;

    assert(textPtr);
    assert(objPtr);

    Tcl_GetStringFromObj(objPtr, &length);
    return TkpTextGetIndex(interp, textPtr->sharedTextPtr, textPtr,
	    Tcl_GetStringFromObj(objPtr, &length), length, indexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSave --
 *
 *	Makes the index robust, so that it can be rebuild after modifications.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
void
TkTextIndexSave(
    TkTextIndex *indexPtr)
{
    TkTextIndexGetLineNumber(indexPtr, indexPtr->textPtr);
    TkTextIndexGetByteIndex(indexPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSameLines --
 *
 *	Test whether both given indicies are referring the same line.
 *
 * Results:
 *	Return true if both indices are referring the same line, otherwise
 *	false will be returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
bool
TkTextIndexSameLines(
    const TkTextIndex *indexPtr1,	/* Pointer to index. */
    const TkTextIndex *indexPtr2)	/* Pointer to index. */
{
    assert(indexPtr1->priv.linePtr);
    assert(indexPtr2->priv.linePtr);
    assert(indexPtr1->priv.linePtr->parentPtr); /* expired? */
    assert(indexPtr2->priv.linePtr->parentPtr); /* expired? */

    return indexPtr1->priv.linePtr == indexPtr2->priv.linePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetEpoch --
 *
 *	Update epoch of given index, don't clear the segment pointer.
 *	Use this function with care, it must be ensured that the
 *	segment pointer is still valid.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
void
TkTextIndexUpdateEpoch(
    TkTextIndex *indexPtr,
    unsigned epoch)
{
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */

    indexPtr->stateEpoch = epoch;
    indexPtr->priv.lineNo = -1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexInvalidate --
 *
 *	Clear position attributes: segPtr, and byteIndex.
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

inline
void
TkTextIndexInvalidate(
    TkTextIndex *indexPtr)	/* Pointer to index. */
{
    indexPtr->priv.segPtr = NULL;
    indexPtr->priv.byteIndex = -1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextIndexSetEpoch --
 *
 *	Set epoch of given index, and clear the segment pointer if
 *	the new epoch is different from last epoch.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
void
TkTextIndexSetEpoch(
    TkTextIndex *indexPtr,
    unsigned epoch)
{
    assert(indexPtr->priv.linePtr);
    assert(indexPtr->priv.linePtr->parentPtr); /* expired? */

    if (indexPtr->stateEpoch != epoch) {
	indexPtr->stateEpoch = epoch;
	indexPtr->priv.segPtr = NULL;
	indexPtr->priv.lineNo = -1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkBTreeGetNumberOfDisplayLines --
 *
 *	Return the current number of display lines. This is the number
 *	of lines known by the B-Tree (not the number of lines known
 *	by the display stuff).
 *
 *	We are including the implementation into this private header file,
 *	because it uses some facts only known by the display stuff.
 *
 * Results:
 *	Returns the current number of display lines (known by B-Tree).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

inline
int
TkBTreeGetNumberOfDisplayLines(
    const TkTextPixelInfo *pixelInfo)
{
    const TkTextDispLineInfo *dispLineInfo;

    if (pixelInfo->height == 0) {
	return 0;
    }
    if (!(dispLineInfo = pixelInfo->dispLineInfo)) {
	return 1;
    }
    if (pixelInfo->epoch & 0x80000000) {
	/*
	 * This will return the old number of display lines, because the
	 * computation of the corresponding logical line is currently in
	 * progress, and unfinished.
	 */
	return dispLineInfo->entry[dispLineInfo->numDispLines].pixels;
    }
    return dispLineInfo->numDispLines;
}

#if TCL_UTF_MAX <= 4 && TK_MAJOR_VERSION == 8 && TK_MINOR_VERSION < 7
/*
 *----------------------------------------------------------------------
 *
 * TkUtfToUniChar --
 *
 *	Only needed for backporting, see source of version 8.7 about
 *	this function.
 *
 *	IMO this function is only a bad hack, Tcl should provide the
 *	appropriate functionality.
 *
 *----------------------------------------------------------------------
 */

inline
int
TkUtfToUniChar(const char *src, int *chPtr)
{
    Tcl_UniChar ch;
    int result = Tcl_UtfToUniChar(src, &ch);
    *chPtr = ch;
    return result;
}

#endif /* end of backport for 8.6/8.5 */

#undef _TK_NEED_IMPLEMENTATION
#endif /* _TK_NEED_IMPLEMENTATION */
/* vi:set ts=8 sw=4: */
