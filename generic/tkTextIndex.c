/* 
 * tkTextIndex.c --
 *
 *	This module provides procedures that manipulate indices for
 *	text widgets.
 *
 * Copyright (c) 1992-1994 The Regents of the University of California.
 * Copyright (c) 1994-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkTextIndex.c,v 1.7 2003/05/19 13:04:23 vincentdarley Exp $
 */

#include "default.h"
#include "tkPort.h"
#include "tkInt.h"
#include "tkText.h"

/*
 * Index to use to select last character in line (very large integer):
 */

#define LAST_CHAR 1000000

/*
 * Forward declarations for procedures defined later in this file:
 */

static CONST char *	ForwBack _ANSI_ARGS_((CONST char *string,
			    TkTextIndex *indexPtr));
static CONST char *	StartEnd _ANSI_ARGS_((CONST char *string,
			    TkTextIndex *indexPtr));
static int		GetIndex _ANSI_ARGS_((Tcl_Interp *interp,
			    TkText *textPtr, CONST char *string,
			    TkTextIndex *indexPtr, int *canCachePtr));


static void		DupTextIndexInternalRep _ANSI_ARGS_((Tcl_Obj *srcPtr,
			    Tcl_Obj *copyPtr));
static void		FreeTextIndexInternalRep _ANSI_ARGS_((Tcl_Obj *listPtr));
static int		SetTextIndexFromAny _ANSI_ARGS_((Tcl_Interp *interp,
			    Tcl_Obj *objPtr));
static void		UpdateStringOfTextIndex _ANSI_ARGS_((Tcl_Obj *objPtr));

#define GET_TEXTINDEX(objPtr) \
		((TkTextIndex *) (objPtr)->internalRep.twoPtrValue.ptr1)
#define GET_INDEXEPOCH(objPtr) \
		((int) (objPtr)->internalRep.twoPtrValue.ptr2)
#define SET_TEXTINDEX(objPtr, indexPtr) \
		(objPtr)->internalRep.twoPtrValue.ptr1 = (VOID*) (indexPtr)
#define SET_INDEXEPOCH(objPtr, epoch) \
		(objPtr)->internalRep.twoPtrValue.ptr2 = (VOID*) (epoch)
/*
 * Define the 'textindex' object type, which Tk uses to represent
 * indices in text widgets internally.
 */
Tcl_ObjType tclTextIndexType = {
    "textindex",			/* name */
    FreeTextIndexInternalRep,		/* freeIntRepProc */
    DupTextIndexInternalRep,	        /* dupIntRepProc */
    NULL,                               /* updateStringProc */
    SetTextIndexFromAny		        /* setFromAnyProc */
};

static void
FreeTextIndexInternalRep(indexObjPtr)
    Tcl_Obj *indexObjPtr;   /* TextIndex object with internal rep to free. */
{
    TkTextIndex *indexPtr = GET_TEXTINDEX(indexObjPtr);
    if (indexPtr->textPtr != NULL) {
        if (--indexPtr->textPtr->refCount == 0) {
	    /* The text widget has been deleted and we need to free it now */
	    ckfree((char *) (indexPtr->textPtr));
	}
    }
    ckfree((char*)indexPtr);
}

static void
DupTextIndexInternalRep(srcPtr, copyPtr)
    Tcl_Obj *srcPtr;		/* TextIndex obj with internal rep to copy. */
    Tcl_Obj *copyPtr;		/* TextIndex obj with internal rep to set. */
{
    int epoch;
    TkTextIndex *dupIndexPtr, *indexPtr;
    dupIndexPtr = (TkTextIndex*) ckalloc(sizeof(TkTextIndex));
    indexPtr = GET_TEXTINDEX(srcPtr);
    epoch = GET_INDEXEPOCH(srcPtr);
    
    dupIndexPtr->tree = indexPtr->tree;
    dupIndexPtr->linePtr = indexPtr->linePtr;
    dupIndexPtr->byteIndex = indexPtr->byteIndex;
    
    SET_TEXTINDEX(copyPtr, dupIndexPtr);
    SET_INDEXEPOCH(copyPtr, epoch);
}

/* 
 * This will not be called except by TkTextNewIndexObj below.
 * This is because if a TkTextIndex is no longer valid, it is
 * not possible to regenerate the string representation.
 */
static void
UpdateStringOfTextIndex(objPtr)
    Tcl_Obj *objPtr;
{
    char buffer[TK_POS_CHARS];
    register int len;

    CONST TkTextIndex *indexPtr = GET_TEXTINDEX(objPtr);

    len = TkTextPrintIndex(indexPtr, buffer);

    objPtr->bytes = ckalloc((unsigned) len + 1);
    strcpy(objPtr->bytes, buffer);
    objPtr->length = len;
}
    
static int
SetTextIndexFromAny(interp, objPtr)
    Tcl_Interp *interp;		/* Used for error reporting if not NULL. */
    Tcl_Obj *objPtr;		/* The object to convert. */
{
    Tcl_AppendToObj(Tcl_GetObjResult(interp),
	"can't convert value to textindex except via TkTextGetIndexFromObj API",
	-1);
    return TCL_ERROR;
}

/*
 *---------------------------------------------------------------------------
 *
 * MakeObjIndex --
 *	
 *	This procedure generates a Tcl_Obj description of an index,
 *	suitable for reading in again later.  If the 'textPtr' is NULL
 *	then we still generate an index object, but it's internal
 *	description is deemed non-cacheable, and therefore effectively
 *	useless (apart from as a temporary memory storage).  This is used
 *	for indices whose meaning is very temporary (like @0,0 or the
 *	name of a mark or tag).  The mapping from such strings/objects to
 *	actual TkTextIndex pointers is not stable to minor text widget
 *	changes which we do not track (we track insertions/deletions).
 *
 * Results:
 *	A pointer to an allocated TkTextIndex which will be freed 
 *	automatically when the Tcl_Obj is used for other purposes.
 *
 * Side effects:
 *	A small amount of memory is allocated.
 *
 *---------------------------------------------------------------------------
 */
static TkTextIndex*
MakeObjIndex(textPtr, objPtr, origPtr) 
    TkText *textPtr;		/* Information about text widget. */
    Tcl_Obj *objPtr;		/* Object containing description of position. */
    CONST TkTextIndex *origPtr; /* Pointer to index. */
{
    TkTextIndex *indexPtr = (TkTextIndex*) ckalloc(sizeof(TkTextIndex));

    indexPtr->tree = origPtr->tree;
    indexPtr->linePtr = origPtr->linePtr;
    indexPtr->byteIndex = origPtr->byteIndex;
    SET_TEXTINDEX(objPtr, indexPtr);
    objPtr->typePtr = &tclTextIndexType;
    indexPtr->textPtr = textPtr;

    if (textPtr != NULL) {
	textPtr->refCount++;
	SET_INDEXEPOCH(objPtr, textPtr->stateEpoch);
    } else {
	SET_INDEXEPOCH(objPtr, 0);
    }
    return indexPtr;
}

CONST TkTextIndex*
TkTextGetIndexFromObj(interp, textPtr, objPtr)
    Tcl_Interp *interp;		/* Use this for error reporting. */
    TkText *textPtr;		/* Information about text widget. */
    Tcl_Obj *objPtr;		/* Object containing description of position. */
{
    TkTextIndex index;
    TkTextIndex *indexPtr = NULL;
    int cache;
    
    if (objPtr->typePtr == &tclTextIndexType) {
	int epoch;
	
	indexPtr = GET_TEXTINDEX(objPtr);
	epoch = GET_INDEXEPOCH(objPtr);
	
	if (epoch == textPtr->stateEpoch) {
	    if (indexPtr->textPtr == textPtr) {
		return indexPtr;
	    }
	}
    }

    /* 
     * The object is either not an index type or referred to a different
     * text widget, or referred to the correct widget, but it is out of
     * date (text has been added/deleted since).
     */
    
    if (GetIndex(interp, textPtr, Tcl_GetString(objPtr), 
		 &index, &cache) != TCL_OK) {
	return NULL;
    }
    
    if (objPtr->typePtr != NULL) {
	if (objPtr->bytes == NULL) {
	    objPtr->typePtr->updateStringProc(objPtr);
	}
	if ((objPtr->typePtr->freeIntRepProc) != NULL) {
	    (*objPtr->typePtr->freeIntRepProc)(objPtr);
	}
    }
    
    if (cache) {
	return MakeObjIndex(textPtr, objPtr, &index);
    } else {
	return MakeObjIndex(NULL, objPtr, &index);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextNewIndexObj --
 *	
 *	This procedure generates a Tcl_Obj description of an index,
 *	suitable for reading in again later.  The index generated is
 *	effectively stable to all except insertion/deletion operations on
 *	the widget.
 *
 * Results:
 *	A new Tcl_Obj with refCount zero.
 *
 * Side effects:
 *	A small amount of memory is allocated.
 *
 *---------------------------------------------------------------------------
 */

Tcl_Obj*
TkTextNewIndexObj(textPtr, indexPtr)
    TkText *textPtr;             /* text widget for this index */
    CONST TkTextIndex *indexPtr; /* Pointer to index. */
{
    Tcl_Obj *retVal;

    retVal = Tcl_NewObj();
    retVal->bytes = NULL;

    /* 
     * Assumption that the above call returns an object with
     * retVal->typePtr == NULL
     */
    MakeObjIndex(textPtr, retVal, indexPtr);
    
    /* 
     * Unfortunately, it isn't possible for us to regenerate the
     * string representation so we have to create it here, while we
     * can be sure the contents of the index are still valid.
     */
    UpdateStringOfTextIndex(retVal);
    return retVal;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextMakeByteIndex --
 *
 *	Given a line index and a byte index, look things up in the B-tree
 *	and fill in a TkTextIndex structure.
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
TkTextMakeByteIndex(tree, lineIndex, byteIndex, indexPtr)
    TkTextBTree tree;		/* Tree that lineIndex and charIndex refer
				 * to. */
    int lineIndex;		/* Index of desired line (0 means first
				 * line of text). */
    int byteIndex;		/* Byte index of desired character. */
    TkTextIndex *indexPtr;	/* Structure to fill in. */
{
    TkTextSegment *segPtr;
    int index;
    CONST char *p, *start;
    Tcl_UniChar ch;

    indexPtr->tree = tree;
    if (lineIndex < 0) {
	lineIndex = 0;
	byteIndex = 0;
    }
    if (byteIndex < 0) {
	byteIndex = 0;
    }
    indexPtr->linePtr = TkBTreeFindLine(tree, lineIndex);
    if (indexPtr->linePtr == NULL) {
	indexPtr->linePtr = TkBTreeFindLine(tree, TkBTreeNumLines(tree));
	byteIndex = 0;
    }
    if (byteIndex == 0) {
	indexPtr->byteIndex = byteIndex;
	return indexPtr;
    }

    /*
     * Verify that the index is within the range of the line and points
     * to a valid character boundary.  
     */

    index = 0;
    for (segPtr = indexPtr->linePtr->segPtr; ; segPtr = segPtr->nextPtr) {
	if (segPtr == NULL) {
	    /*
	     * Use the index of the last character in the line.  Since
	     * the last character on the line is guaranteed to be a '\n',
	     * we can back up a constant sizeof(char) bytes.
	     */
	     
	    indexPtr->byteIndex = index - sizeof(char);
	    break;
	}
	if (index + segPtr->size > byteIndex) {
	    indexPtr->byteIndex = byteIndex;
	    if ((byteIndex > index) && (segPtr->typePtr == &tkTextCharType)) {
		/*
		 * Prevent UTF-8 character from being split up by ensuring
		 * that byteIndex falls on a character boundary.  If index
		 * falls in the middle of a UTF-8 character, it will be
		 * adjusted to the end of that UTF-8 character.
		 */

		start = segPtr->body.chars + (byteIndex - index);
		p = Tcl_UtfPrev(start, segPtr->body.chars);
		p += Tcl_UtfToUniChar(p, &ch);
		indexPtr->byteIndex += p - start;
	    }
	    break;
	}
	index += segPtr->size;
    }
    return indexPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextMakeCharIndex --
 *
 *	Given a line index and a character index, look things up in the
 *	B-tree and fill in a TkTextIndex structure.
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

TkTextIndex *
TkTextMakeCharIndex(tree, lineIndex, charIndex, indexPtr)
    TkTextBTree tree;		/* Tree that lineIndex and charIndex refer
				 * to. */
    int lineIndex;		/* Index of desired line (0 means first
				 * line of text). */
    int charIndex;		/* Index of desired character. */
    TkTextIndex *indexPtr;	/* Structure to fill in. */
{
    register TkTextSegment *segPtr;
    char *p, *start, *end;
    int index, offset;
    Tcl_UniChar ch;

    indexPtr->tree = tree;
    if (lineIndex < 0) {
	lineIndex = 0;
	charIndex = 0;
    }
    if (charIndex < 0) {
	charIndex = 0;
    }
    indexPtr->linePtr = TkBTreeFindLine(tree, lineIndex);
    if (indexPtr->linePtr == NULL) {
	indexPtr->linePtr = TkBTreeFindLine(tree, TkBTreeNumLines(tree));
	charIndex = 0;
    }

    /*
     * Verify that the index is within the range of the line.
     * If not, just use the index of the last character in the line.
     */

    index = 0;
    for (segPtr = indexPtr->linePtr->segPtr; ; segPtr = segPtr->nextPtr) {
	if (segPtr == NULL) {
	    /*
	     * Use the index of the last character in the line.  Since
	     * the last character on the line is guaranteed to be a '\n',
	     * we can back up a constant sizeof(char) bytes.
	     */
	     
	    indexPtr->byteIndex = index - sizeof(char);
	    break;
	}
	if (segPtr->typePtr == &tkTextCharType) {
	    /*
	     * Turn character offset into a byte offset.
	     */

	    start = segPtr->body.chars;
	    end = start + segPtr->size;
	    for (p = start; p < end; p += offset) {
		if (charIndex == 0) {
		    indexPtr->byteIndex = index;
		    return indexPtr;
		}
		charIndex--;
		offset = Tcl_UtfToUniChar(p, &ch);
		index += offset;
	    }
	} else {
	    if (charIndex < segPtr->size) {
		indexPtr->byteIndex = index;
		break;
	    }
	    charIndex -= segPtr->size;
	    index += segPtr->size;
	}
    }
    return indexPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextIndexToSeg --
 *
 *	Given an index, this procedure returns the segment and offset
 *	within segment for the index.
 *
 * Results:
 *	The return value is a pointer to the segment referred to by
 *	indexPtr; this will always be a segment with non-zero size.  The
 *	variable at *offsetPtr is set to hold the integer offset within
 *	the segment of the character given by indexPtr.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

TkTextSegment *
TkTextIndexToSeg(indexPtr, offsetPtr)
    CONST TkTextIndex *indexPtr;/* Text index. */
    int *offsetPtr;		/* Where to store offset within segment, or
				 * NULL if offset isn't wanted. */
{
    TkTextSegment *segPtr;
    int offset;

    for (offset = indexPtr->byteIndex, segPtr = indexPtr->linePtr->segPtr;
	    offset >= segPtr->size;
	    offset -= segPtr->size, segPtr = segPtr->nextPtr) {
	/* Empty loop body. */
    }
    if (offsetPtr != NULL) {
	*offsetPtr = offset;
    }
    return segPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextSegToOffset --
 *
 *	Given a segment pointer and the line containing it, this procedure
 *	returns the offset of the segment within its line.
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
TkTextSegToOffset(segPtr, linePtr)
    CONST TkTextSegment *segPtr;/* Segment whose offset is desired. */
    CONST TkTextLine *linePtr;	/* Line containing segPtr. */
{
    CONST TkTextSegment *segPtr2;
    int offset;

    offset = 0;
    for (segPtr2 = linePtr->segPtr; segPtr2 != segPtr;
	    segPtr2 = segPtr2->nextPtr) {
	offset += segPtr2->size;
    }
    return offset;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextGetObjIndex --
 * 
 *    Simpler wrapper around the string based function, but could be
 *    enhanced with a new object type in the future.
 *
 * Results:
 *    see TkTextGetIndex
 *
 * Side effects:
 *    None.
 *    
 *---------------------------------------------------------------------------
 */

int
TkTextGetObjIndex(interp, textPtr, idxObj, indexPtr)
    Tcl_Interp *interp;		/* Use this for error reporting. */
    TkText *textPtr;		/* Information about text widget. */
    Tcl_Obj *idxObj;	        /* Object containing textual description 
				 * of position. */
    TkTextIndex *indexPtr;	/* Index structure to fill in. */
{
    return GetIndex(interp, textPtr, Tcl_GetString(idxObj), indexPtr, NULL);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextGetIndex --
 *
 *	Given a string, return the index that is described.
 *
 * Results:
 *	The return value is a standard Tcl return result.  If TCL_OK is
 *	returned, then everything went well and the index at *indexPtr is
 *	filled in; otherwise TCL_ERROR is returned and an error message
 *	is left in the interp's result.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

int
TkTextGetIndex(interp, textPtr, string, indexPtr)
    Tcl_Interp *interp;		/* Use this for error reporting. */
    TkText *textPtr;		/* Information about text widget. */
    CONST char *string;		/* Textual description of position. */
    TkTextIndex *indexPtr;	/* Index structure to fill in. */
{
    return GetIndex(interp, textPtr, string, indexPtr, NULL);
}

/*
 *---------------------------------------------------------------------------
 *
 * GetIndex --
 *
 *	Given a string, return the index that is described.
 *
 * Results:
 *	The return value is a standard Tcl return result.  If TCL_OK is
 *	returned, then everything went well and the index at *indexPtr is
 *	filled in; otherwise TCL_ERROR is returned and an error message
 *	is left in the interp's result.
 *	
 *	If *canCachePtr is non-NULL, and everything went well, the
 *	integer it points to is set to 1 if the indexPtr is something
 *	which can be cached, and zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static int
GetIndex(interp, textPtr, string, indexPtr, canCachePtr)
    Tcl_Interp *interp;		/* Use this for error reporting. */
    TkText *textPtr;		/* Information about text widget. */
    CONST char *string;		/* Textual description of position. */
    TkTextIndex *indexPtr;	/* Index structure to fill in. */
    int *canCachePtr;           /* Pointer to integer to store whether
                                 * we can cache the index (or NULL) */
{
    char *p, *end, *endOfBase;
    Tcl_HashEntry *hPtr;
    TkTextTag *tagPtr;
    TkTextSearch search;
    TkTextIndex first, last;
    int wantLast, result;
    char c;
    CONST char *cp;
    Tcl_DString copy;
    int canCache = 0;
    
    /*
     *---------------------------------------------------------------------
     * Stage 1: check to see if the index consists of nothing but a mark
     * name.  We do this check now even though it's also done later, in
     * order to allow mark names that include funny characters such as
     * spaces or "+1c".
     *---------------------------------------------------------------------
     */

    if (TkTextMarkNameToIndex(textPtr, string, indexPtr) == TCL_OK) {
	goto done;
    }

    /*
     *------------------------------------------------
     * Stage 2: start again by parsing the base index.
     *------------------------------------------------
     */

    indexPtr->tree = textPtr->tree;

    /*
     * First look for the form "tag.first" or "tag.last" where "tag"
     * is the name of a valid tag.  Try to use up as much as possible
     * of the string in this check (strrchr instead of strchr below).
     * Doing the check now, and in this way, allows tag names to include
     * funny characters like "@" or "+1c".
     */

    Tcl_DStringInit(&copy);
    p = strrchr(Tcl_DStringAppend(&copy, string, -1), '.');
    if (p != NULL) {
	if ((p[1] == 'f') && (strncmp(p+1, "first", 5) == 0)) {
	    wantLast = 0;
	    endOfBase = p+6;
	} else if ((p[1] == 'l') && (strncmp(p+1, "last", 4) == 0)) {
	    wantLast = 1;
	    endOfBase = p+5;
	} else {
	    goto tryxy;
	}
	*p = 0;
	hPtr = Tcl_FindHashEntry(&textPtr->tagTable, Tcl_DStringValue(&copy));
	*p = '.';
	if (hPtr == NULL) {
	    goto tryxy;
	}
	tagPtr = (TkTextTag *) Tcl_GetHashValue(hPtr);
	TkTextMakeByteIndex(textPtr->tree, 0, 0, &first);
	TkTextMakeByteIndex(textPtr->tree, TkBTreeNumLines(textPtr->tree), 0,
		&last);
	TkBTreeStartSearch(&first, &last, tagPtr, &search);
	if (!TkBTreeCharTagged(&first, tagPtr) && !TkBTreeNextTag(&search)) {
	    Tcl_ResetResult(interp);
	    Tcl_AppendResult(interp,
		    "text doesn't contain any characters tagged with \"",
		    Tcl_GetHashKey(&textPtr->tagTable, hPtr), "\"",
			    (char *) NULL);
	    Tcl_DStringFree(&copy);
	    return TCL_ERROR;
	}
	*indexPtr = search.curIndex;
	if (wantLast) {
	    while (TkBTreeNextTag(&search)) {
		*indexPtr = search.curIndex;
	    }
	}
	goto gotBase;
    }

    tryxy:
    if (string[0] == '@') {
	/*
	 * Find character at a given x,y location in the window.
	 */

	int x, y;

	cp = string+1;
	x = strtol(cp, &end, 0);
	if ((end == cp) || (*end != ',')) {
	    goto error;
	}
	cp = end+1;
	y = strtol(cp, &end, 0);
	if (end == cp) {
	    goto error;
	}
	TkTextPixelIndex(textPtr, x, y, indexPtr);
	endOfBase = end;
	goto gotBase; 
    }

    if (isdigit(UCHAR(string[0])) || (string[0] == '-')) {
	int lineIndex, charIndex;

	/*
	 * Base is identified with line and character indices.
	 */

	lineIndex = strtol(string, &end, 0) - 1;
	if ((end == string) || (*end != '.')) {
	    goto error;
	}
	p = end+1;
	if ((*p == 'e') && (strncmp(p, "end", 3) == 0)) {
	    charIndex = LAST_CHAR;
	    endOfBase = p+3;
	} else {
	    charIndex = strtol(p, &end, 0);
	    if (end == p) {
		goto error;
	    }
	    endOfBase = end;
	}
	TkTextMakeCharIndex(textPtr->tree, lineIndex, charIndex, indexPtr);
	canCache = 1;
	goto gotBase;
    }

    for (p = Tcl_DStringValue(&copy); *p != 0; p++) {
	if (isspace(UCHAR(*p)) || (*p == '+') || (*p == '-')) {
	    break;
	}
    }
    endOfBase = p;
    if (string[0] == '.') {
	/*
	 * See if the base position is the name of an embedded window.
	 */

	c = *endOfBase;
	*endOfBase = 0;
	result = TkTextWindowIndex(textPtr, Tcl_DStringValue(&copy), indexPtr);
	*endOfBase = c;
	if (result != 0) {
	    goto gotBase;
	}
    }
    if ((string[0] == 'e')
	    && (strncmp(string, "end",
	    (size_t) (endOfBase-Tcl_DStringValue(&copy))) == 0)) {
	/*
	 * Base position is end of text.
	 */

	TkTextMakeByteIndex(textPtr->tree, TkBTreeNumLines(textPtr->tree),
		0, indexPtr);
	canCache = 1;
	goto gotBase;
    } else {
	/*
	 * See if the base position is the name of a mark.
	 */

	c = *endOfBase;
	*endOfBase = 0;
	result = TkTextMarkNameToIndex(textPtr, Tcl_DStringValue(&copy),
		indexPtr);
	*endOfBase = c;
	if (result == TCL_OK) {
	    goto gotBase;
	}

	/*
	 * See if the base position is the name of an embedded image
	 */

	c = *endOfBase;
	*endOfBase = 0;
	result = TkTextImageIndex(textPtr, Tcl_DStringValue(&copy), indexPtr);
	*endOfBase = c;
	if (result != 0) {
	    goto gotBase;
	}
    }
    goto error;

    /*
     *-------------------------------------------------------------------
     * Stage 3: process zero or more modifiers.  Each modifier is either
     * a keyword like "wordend" or "linestart", or it has the form
     * "op count units" where op is + or -, count is a number, and units
     * is "chars" or "lines".
     *-------------------------------------------------------------------
     */

    gotBase:
    cp = endOfBase;
    while (1) {
	while (isspace(UCHAR(*cp))) {
	    cp++;
	}
	if (*cp == 0) {
	    break;
	}
    
	if ((*cp == '+') || (*cp == '-')) {
	    cp = ForwBack(cp, indexPtr);
	} else {
	    cp = StartEnd(cp, indexPtr);
	}
	if (cp == NULL) {
	    goto error;
	}
    }
    Tcl_DStringFree(&copy);
    done:
    if (canCachePtr != NULL) {
	*canCachePtr = canCache;
    }
    return TCL_OK;

    error:
    Tcl_DStringFree(&copy);
    Tcl_ResetResult(interp);
    Tcl_AppendResult(interp, "bad text index \"", string, "\"",
	    (char *) NULL);
    return TCL_ERROR;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextPrintIndex --
 *	
 *	This procedure generates a string description of an index, suitable
 *	for reading in again later.
 *
 * Results:
 *	The characters pointed to by string are modified.  Returns the
 *	number of characters in the string.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

int
TkTextPrintIndex(indexPtr, string)
    CONST TkTextIndex *indexPtr;/* Pointer to index. */
    char *string;		/* Place to store the position.  Must have
				 * at least TK_POS_CHARS characters. */
{
    TkTextSegment *segPtr;
    int numBytes, charIndex;

    numBytes = indexPtr->byteIndex;
    charIndex = 0;
    for (segPtr = indexPtr->linePtr->segPtr; ; segPtr = segPtr->nextPtr) {
	if (numBytes <= segPtr->size) {
	    break;
	}
	if (segPtr->typePtr == &tkTextCharType) {
	    charIndex += Tcl_NumUtfChars(segPtr->body.chars, segPtr->size);
	} else {
	    charIndex += segPtr->size;
	}
	numBytes -= segPtr->size;
    }
    if (segPtr->typePtr == &tkTextCharType) {
	charIndex += Tcl_NumUtfChars(segPtr->body.chars, numBytes);
    } else {
	charIndex += numBytes;
    }
    return sprintf(string, "%d.%d", TkBTreeLineIndex(indexPtr->linePtr) + 1,
	    charIndex);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextIndexCmp --
 *
 *	Compare two indices to see which one is earlier in the text.
 *
 * Results:
 *	The return value is 0 if index1Ptr and index2Ptr refer to the same
 *	position in the file, -1 if index1Ptr refers to an earlier position
 *	than index2Ptr, and 1 otherwise.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

int
TkTextIndexCmp(index1Ptr, index2Ptr)
    CONST TkTextIndex *index1Ptr;		/* First index. */
    CONST TkTextIndex *index2Ptr;		/* Second index. */
{
    int line1, line2;

    if (index1Ptr->linePtr == index2Ptr->linePtr) {
	if (index1Ptr->byteIndex < index2Ptr->byteIndex) {
	    return -1;
	} else if (index1Ptr->byteIndex > index2Ptr->byteIndex) {
	    return 1;
	} else {
	    return 0;
	}
    }
    line1 = TkBTreeLineIndex(index1Ptr->linePtr);
    line2 = TkBTreeLineIndex(index2Ptr->linePtr);
    if (line1 < line2) {
	return -1;
    }
    if (line1 > line2) {
	return 1;
    }
    return 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * ForwBack --
 *
 *	This procedure handles +/- modifiers for indices to adjust the
 *	index forwards or backwards.
 *
 * Results:
 *	If the modifier in string is successfully parsed then the return
 *	value is the address of the first character after the modifier,
 *	and *indexPtr is updated to reflect the modifier.  If there is a
 *	syntax error in the modifier then NULL is returned.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static CONST char *
ForwBack(string, indexPtr)
    CONST char *string;		/* String to parse for additional info
				 * about modifier (count and units). 
				 * Points to "+" or "-" that starts
				 * modifier. */
    TkTextIndex *indexPtr;	/* Index to update as specified in string. */
{
    register CONST char *p, *units;
    char *end;
    int count, lineIndex;
    size_t length;

    /*
     * Get the count (how many units forward or backward).
     */

    p = string+1;
    while (isspace(UCHAR(*p))) {
	p++;
    }
    count = strtol(p, &end, 0);
    if (end == p) {
	return NULL;
    }
    p = end;
    while (isspace(UCHAR(*p))) {
	p++;
    }

    /*
     * Find the end of this modifier (next space or + or - character),
     * then parse the unit specifier and update the position
     * accordingly.
     */

    units = p; 
    while ((*p != '\0') && !isspace(UCHAR(*p)) && (*p != '+') && (*p != '-')) {
	p++;
    }
    length = p - units;
    if ((*units == 'c') && (strncmp(units, "chars", length) == 0)) {
	if (*string == '+') {
	    TkTextIndexForwChars(indexPtr, count, indexPtr);
	} else {
	    TkTextIndexBackChars(indexPtr, count, indexPtr);
	}
    } else if ((*units == 'l') && (strncmp(units, "lines", length) == 0)) {
	lineIndex = TkBTreeLineIndex(indexPtr->linePtr);
	if (*string == '+') {
	    lineIndex += count;
	} else {
	    lineIndex -= count;

	    /*
	     * The check below retains the character position, even
	     * if the line runs off the start of the file.  Without
	     * it, the character position will get reset to 0 by
	     * TkTextMakeIndex.
	     */

	    if (lineIndex < 0) {
		lineIndex = 0;
	    }
	}
	/*
	 * This doesn't work quite right if using a proportional font or
	 * UTF-8 characters with varying numbers of bytes.  The cursor will
	 * bop around, keeping a constant number of bytes (not characters)
	 * from the left edge (but making sure not to split any UTF-8
	 * characters), regardless of the x-position the index corresponds
	 * to.  The proper way to do this is to get the x-position of the
	 * index and then pick the character at the same x-position in the
	 * new line.
	 */

	TkTextMakeByteIndex(indexPtr->tree, lineIndex, indexPtr->byteIndex,
		indexPtr);
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
 *	Given an index for a text widget, this procedure creates a new
 *	index that points "count" bytes ahead of the source index.
 *
 * Results:
 *	*dstPtr is modified to refer to the character "count" bytes after
 *	srcPtr, or to the last character in the TkText if there aren't
 *	"count" bytes left.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

void
TkTextIndexForwBytes(srcPtr, byteCount, dstPtr)
    CONST TkTextIndex *srcPtr;	/* Source index. */
    int byteCount;		/* How many bytes forward to move.  May be
				 * negative. */
    TkTextIndex *dstPtr;	/* Destination index: gets modified. */
{
    TkTextLine *linePtr;
    TkTextSegment *segPtr;
    int lineLength;

    if (byteCount < 0) {
	TkTextIndexBackBytes(srcPtr, -byteCount, dstPtr);
	return;
    }

    *dstPtr = *srcPtr;
    dstPtr->byteIndex += byteCount;
    while (1) {
	/*
	 * Compute the length of the current line.
	 */

	lineLength = 0;
	for (segPtr = dstPtr->linePtr->segPtr; segPtr != NULL;
		segPtr = segPtr->nextPtr) {
	    lineLength += segPtr->size;
	}

	/*
	 * If the new index is in the same line then we're done.
	 * Otherwise go on to the next line.
	 */

	if (dstPtr->byteIndex < lineLength) {
	    return;
	}
	dstPtr->byteIndex -= lineLength;
	linePtr = TkBTreeNextLine(dstPtr->linePtr);
	if (linePtr == NULL) {
	    dstPtr->byteIndex = lineLength - 1;
	    return;
	}
	dstPtr->linePtr = linePtr;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextIndexForwChars --
 *
 *	Given an index for a text widget, this procedure creates a new
 *	index that points "count" characters ahead of the source index.
 *
 * Results:
 *	*dstPtr is modified to refer to the character "count" characters
 *	after srcPtr, or to the last character in the TkText if there
 *	aren't "count" characters left in the file.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

void
TkTextIndexForwChars(srcPtr, charCount, dstPtr)
    CONST TkTextIndex *srcPtr;	/* Source index. */
    int charCount;		/* How many characters forward to move.
				 * May be negative. */
    TkTextIndex *dstPtr;	/* Destination index: gets modified. */
{
    TkTextLine *linePtr;
    TkTextSegment *segPtr;
    int byteOffset;
    char *start, *end, *p;
    Tcl_UniChar ch;

    if (charCount < 0) {
	TkTextIndexBackChars(srcPtr, -charCount, dstPtr);
	return;
    }

    *dstPtr = *srcPtr;

    /*
     * Find seg that contains src byteIndex.
     * Move forward specified number of chars.
     */

    segPtr = TkTextIndexToSeg(dstPtr, &byteOffset);
    while (1) {
	/*
	 * Go through each segment in line looking for specified character
	 * index.
	 */

	for ( ; segPtr != NULL; segPtr = segPtr->nextPtr) {
	    if (segPtr->typePtr == &tkTextCharType) {
		start = segPtr->body.chars + byteOffset;
		end = segPtr->body.chars + segPtr->size;
		for (p = start; p < end; p += Tcl_UtfToUniChar(p, &ch)) {
		    if (charCount == 0) {
			dstPtr->byteIndex += (p - start);
			return;
		    }
		    charCount--;
		}
	    } else {
		if (charCount < segPtr->size - byteOffset) {
		    dstPtr->byteIndex += charCount;
		    return;
		}
		charCount -= segPtr->size - byteOffset;
	    }
	    dstPtr->byteIndex += segPtr->size - byteOffset;
	    byteOffset = 0;
	}

	/*
	 * Go to the next line.  If we are at the end of the text item,
	 * back up one byte (for the terminal '\n' character) and return
	 * that index.
	 */
	 
	linePtr = TkBTreeNextLine(dstPtr->linePtr);
	if (linePtr == NULL) {
	    dstPtr->byteIndex -= sizeof(char);
	    return;
	}
	dstPtr->linePtr = linePtr;
	dstPtr->byteIndex = 0;
	segPtr = dstPtr->linePtr->segPtr;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextIndexBackBytes --
 *
 *	Given an index for a text widget, this procedure creates a new
 *	index that points "count" bytes earlier than the source index.
 *
 * Results:
 *	*dstPtr is modified to refer to the character "count" bytes before
 *	srcPtr, or to the first character in the TkText if there aren't
 *	"count" bytes earlier than srcPtr.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

void
TkTextIndexBackBytes(srcPtr, byteCount, dstPtr)
    CONST TkTextIndex *srcPtr;	/* Source index. */
    int byteCount;		/* How many bytes backward to move.  May be
				 * negative. */
    TkTextIndex *dstPtr;	/* Destination index: gets modified. */
{
    TkTextSegment *segPtr;
    int lineIndex;

    if (byteCount < 0) {
	TkTextIndexForwBytes(srcPtr, -byteCount, dstPtr);
	return;
    }

    *dstPtr = *srcPtr;
    dstPtr->byteIndex -= byteCount;
    lineIndex = -1;
    while (dstPtr->byteIndex < 0) {
	/*
	 * Move back one line in the text.  If we run off the beginning
	 * of the file then just return the first character in the text.
	 */

	if (lineIndex < 0) {
	    lineIndex = TkBTreeLineIndex(dstPtr->linePtr);
	}
	if (lineIndex == 0) {
	    dstPtr->byteIndex = 0;
	    return;
	}
	lineIndex--;
	dstPtr->linePtr = TkBTreeFindLine(dstPtr->tree, lineIndex);

	/*
	 * Compute the length of the line and add that to dstPtr->charIndex.
	 */

	for (segPtr = dstPtr->linePtr->segPtr; segPtr != NULL;
		segPtr = segPtr->nextPtr) {
	    dstPtr->byteIndex += segPtr->size;
	}
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkTextIndexBackChars --
 *
 *	Given an index for a text widget, this procedure creates a new
 *	index that points "count" characters earlier than the source index.
 *
 * Results:
 *	*dstPtr is modified to refer to the character "count" characters
 *	before srcPtr, or to the first character in the file if there
 *	aren't "count" characters earlier than srcPtr.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

void
TkTextIndexBackChars(srcPtr, charCount, dstPtr)
    CONST TkTextIndex *srcPtr;	/* Source index. */
    int charCount;		/* How many characters backward to move.
				 * May be negative. */
    TkTextIndex *dstPtr;	/* Destination index: gets modified. */
{
    TkTextSegment *segPtr, *oldPtr;
    int lineIndex, segSize;
    CONST char *p, *start, *end;

    if (charCount <= 0) {
	TkTextIndexForwChars(srcPtr, -charCount, dstPtr);
	return;
    }

    *dstPtr = *srcPtr;

    /*
     * Find offset within seg that contains byteIndex.
     * Move backward specified number of chars.
     */

    lineIndex = -1;
    
    segSize = dstPtr->byteIndex;
    for (segPtr = dstPtr->linePtr->segPtr; ; segPtr = segPtr->nextPtr) {
	if (segSize <= segPtr->size) {
	    break;
	}
	segSize -= segPtr->size;
    }
    while (1) {
	if (segPtr->typePtr == &tkTextCharType) {
	    start = segPtr->body.chars;
	    end = segPtr->body.chars + segSize;
	    for (p = end; ; p = Tcl_UtfPrev(p, start)) {
		if (charCount == 0) {
		    dstPtr->byteIndex -= (end - p);
		    return;
		}
		if (p == start) {
		    break;
		}
		charCount--;
	    }
	} else {
	    if (charCount <= segSize) {
		dstPtr->byteIndex -= charCount;
		return;
	    }
	    charCount -= segSize;
	}
	dstPtr->byteIndex -= segSize;

	/*
	 * Move back into previous segment.
	 */

	oldPtr = segPtr;
	segPtr = dstPtr->linePtr->segPtr;
	if (segPtr != oldPtr) {
	    for ( ; segPtr->nextPtr != oldPtr; segPtr = segPtr->nextPtr) {
		/* Empty body. */
	    }
	    segSize = segPtr->size;
	    continue;
	}

	/*
	 * Move back to previous line.
	 */

	if (lineIndex < 0) {
	    lineIndex = TkBTreeLineIndex(dstPtr->linePtr);
	}
	if (lineIndex == 0) {
	    dstPtr->byteIndex = 0;
	    return;
	}
	lineIndex--;
	dstPtr->linePtr = TkBTreeFindLine(dstPtr->tree, lineIndex);

	/*
	 * Compute the length of the line and add that to dstPtr->byteIndex.
	 */

	oldPtr = dstPtr->linePtr->segPtr;
	for (segPtr = oldPtr; segPtr != NULL; segPtr = segPtr->nextPtr) {
	    dstPtr->byteIndex += segPtr->size;
	    oldPtr = segPtr;
	}
	segPtr = oldPtr;
	segSize = segPtr->size;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * StartEnd --
 *
 *	This procedure handles modifiers like "wordstart" and "lineend"
 *	to adjust indices forwards or backwards.
 *
 * Results:
 *	If the modifier is successfully parsed then the return value
 *	is the address of the first character after the modifier, and
 *	*indexPtr is updated to reflect the modifier. If there is a
 *	syntax error in the modifier then NULL is returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static CONST char *
StartEnd(string, indexPtr)
    CONST char *string;		/* String to parse for additional info
				 * about modifier (count and units). 
				 * Points to first character of modifer
				 * word. */
    TkTextIndex *indexPtr;	/* Index to mdoify based on string. */
{
    CONST char *p;
    int c, offset;
    size_t length;
    register TkTextSegment *segPtr;

    /*
     * Find the end of the modifier word.
     */

    for (p = string; isalnum(UCHAR(*p)); p++) {
	/* Empty loop body. */
    }
    length = p-string;
    if ((*string == 'l') && (strncmp(string, "lineend", length) == 0)
	    && (length >= 5)) {
	indexPtr->byteIndex = 0;
	for (segPtr = indexPtr->linePtr->segPtr; segPtr != NULL;
		segPtr = segPtr->nextPtr) {
	    indexPtr->byteIndex += segPtr->size;
	}
	indexPtr->byteIndex -= sizeof(char);
    } else if ((*string == 'l') && (strncmp(string, "linestart", length) == 0)
	    && (length >= 5)) {
	indexPtr->byteIndex = 0;
    } else if ((*string == 'w') && (strncmp(string, "wordend", length) == 0)
	    && (length >= 5)) {
	int firstChar = 1;

	/*
	 * If the current character isn't part of a word then just move
	 * forward one character.  Otherwise move forward until finding
	 * a character that isn't part of a word and stop there.
	 */

	segPtr = TkTextIndexToSeg(indexPtr, &offset);
	while (1) {
	    if (segPtr->typePtr == &tkTextCharType) {
		c = segPtr->body.chars[offset];
		if (!isalnum(UCHAR(c)) && (c != '_')) {
		    break;
		}
		firstChar = 0;
	    }
	    offset += 1;
	    indexPtr->byteIndex += sizeof(char);
	    if (offset >= segPtr->size) {
		segPtr = TkTextIndexToSeg(indexPtr, &offset);
	    }
	}
	if (firstChar) {
	    TkTextIndexForwChars(indexPtr, 1, indexPtr);
	}
    } else if ((*string == 'w') && (strncmp(string, "wordstart", length) == 0)
	    && (length >= 5)) {
	int firstChar = 1;

	/*
	 * Starting with the current character, look for one that's not
	 * part of a word and keep moving backward until you find one.
	 * Then if the character found wasn't the first one, move forward
	 * again one position.
	 */

	segPtr = TkTextIndexToSeg(indexPtr, &offset);
	while (1) {
	    if (segPtr->typePtr == &tkTextCharType) {
		c = segPtr->body.chars[offset];
		if (!isalnum(UCHAR(c)) && (c != '_')) {
		    break;
		}
		firstChar = 0;
	    }
	    offset -= 1;
	    indexPtr->byteIndex -= sizeof(char);
	    if (offset < 0) {
		if (indexPtr->byteIndex < 0) {
		    indexPtr->byteIndex = 0;
		    goto done;
		}
		segPtr = TkTextIndexToSeg(indexPtr, &offset);
	    }
	}
	if (!firstChar) {
	    TkTextIndexForwChars(indexPtr, 1, indexPtr);
	}
    } else {
	return NULL;
    }
    done:
    return p;
}
