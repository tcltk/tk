/*
 * tkIcu.c --
 *
 *	tkIcu.c implements various Tk commands which can find
 *	grapheme cluster and workchar bounderies in Unicode strings.
 *
 * Copyright © 2021 Jan Nijtmans
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */
 
#include "tkInt.h"

/* Wrappers from tkTextGrapheme.c. */

extern int mojibake_grapheme_breaks(const char *buf, size_t byteLen,
	unsigned char *breaks);
extern int mojibake_word_breaks_with_dict(const char *buf, size_t byteLen,
	unsigned char *wBreaks, unsigned char *gBreaks_tmp);


/*
 * Index translation: the public commands take and return Tcl CHAR
 * indices (exactly as the original TkGetIntForIndex-based code did).
 * The mojibake wrappers return Tcl BYTE offsets. These two helpers
 * are the only place that translation happens in this file.
 */

/*
 *----------------------------------------------------------------------
 *
 * ByteOffsetForCharIndex --
 *
 *	Translate a Tcl CHAR index into a Tcl BYTE offset within the
 *	same string.
 *
 * Results:
 *	Returns the byte offset of the character at charIndex in
 *	tclBytes, as computed by Tcl_UtfAtIndex. For charIndex equal
 *	to the string's character length, returns the byte length
 *	(one past the last byte). For charIndex <= 0, returns 0.
 *
 * Side effects:
 *	None. This is a pure translation helper and is one of only
 *	two places in this file where CHAR <-> BYTE translation is
 *	performed.
 *
 *----------------------------------------------------------------------
 */
 
static Tcl_Size
ByteOffsetForCharIndex(
    const char *tclBytes,	/* Tcl string bytes, NUL-terminated. */
    Tcl_Size charIndex)		/* Index in Tcl CHAR coordinates. */
{
    return (Tcl_Size)(Tcl_UtfAtIndex(tclBytes, charIndex) - tclBytes);
}

/*
 *----------------------------------------------------------------------
 *
 * CharIndexForByteOffset --
 *
 *	Translate a Tcl BYTE offset into a Tcl CHAR index within the
 *	same string.
 *
 * Results:
 *	Returns the number of UTF characters in the first byteOffset
 *	bytes of tclBytes, as computed by Tcl_NumUtfChars. For
 *	byteOffset <= 0, returns 0. For byteOffset at or past the
 *	byte length, returns the full character length.
 *
 * Side effects:
 *	None. Pure translation helper; the counterpart to
 *	ByteOffsetForCharIndex and the second of the only two places
 *	in this file where CHAR <-> BYTE translation happens.
 *
 *----------------------------------------------------------------------
 */
 
static Tcl_Size
CharIndexForByteOffset(
    const char *tclBytes,	/* Tcl string bytes, NUL-terminated. */
    Tcl_Size byteOffset)	/* Offset in Tcl BYTE coordinates. */
{
    return Tcl_NumUtfChars(tclBytes, byteOffset);
}


/*
 *----------------------------------------------------------------------
 *
 * TkIcuStartEndOfClusterCmd --
 *
 *	Implementation of the ::tk::startOfCluster and
 *	::tk::endOfCluster commands. Finds the boundary of the
 *	grapheme cluster containing (or adjacent to) a given
 *	character index in a Unicode string.
 *
 *	clientData is 0 for startOfCluster and 1 for endOfCluster.
 *	The command syntax is:
 *
 *	    ::tk::startOfCluster str index ?locale?
 *	    ::tk::endOfCluster   str index ?locale?
 *
 *	The optional locale argument is accepted for symmetry with
 *	the word commands but is currently ignored.
 *
 * Results:
 *	TCL_OK on success. On success the interpreter result is set
 *	to the CHAR index of the requested cluster boundary, or is
 *	left unset when no result exists (see below). TCL_ERROR is
 *	returned, with the interpreter result set to a diagnostic
 *	message, if the argument count is wrong, if the index cannot
 *	be parsed by TkGetIntForIndex, or if grapheme segmentation
 *	fails.
 *
 *	"no result" (TCL_OK with the result left untouched) occurs
 *	for the following endpoint cases, matching the original
 *	TkGetIntForIndex-based semantics:
 *	  - startOfCluster with index < 0 (including -1 and "",
 *	    which TkGetIntForIndex reports as TCL_INDEX_NONE):
 *	    no result.
 *	  - endOfCluster with index < 0: returns the first break, 0.
 *	  - startOfCluster with index > length: returns the last
 *	    break (the character length).
 *	  - endOfCluster with index >= length: no result.
 *
 * Side effects:
 *	Allocates a temporary break array sized to the byte length
 *	of the input string plus one, and frees it before returning.
 *
 *----------------------------------------------------------------------
 */
 
static int
TkIcuStartEndOfClusterCmd(
    void *clientData,		/* 0 = startOfCluster, 1 = endOfCluster. */
    Tcl_Interp *interp,		/* Interpreter for error reporting. */
    Tcl_Size objc,		/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    int isEnd = PTR2INT(clientData);
    Tcl_Size tclLen, index, result;
    const char *tclBytes;
    unsigned char *breaks = NULL;

    if (objc < 3 || objc > 4) {
	Tcl_WrongNumArgs(interp, 1, objv, "str start ?locale?");
	return TCL_ERROR;
    }
    tclBytes = Tcl_GetStringFromObj(objv[1], &tclLen);
    {
	Tcl_Size ulen = Tcl_GetCharLength(objv[1]);
	if (TkGetIntForIndex(objv[2], ulen - 1, 0, &index) != TCL_OK) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "bad index \"%s\": must be integer?[+-]integer?, "
		    "end?[+-]integer?, or \"\"",
		    Tcl_GetString(objv[2])));
	    Tcl_SetErrorCode(interp, "TK", "ICU", "INDEX", (char *)NULL);
	    return TCL_ERROR;
	}
	/*
	 * Do NOT clamp the index here: the endpoints are meaningful,
	 * and clamping destroys them. This restores the original
	 * semantics (which the previous draft broke by clamping
	 * index < 0 to 0 and index > ulen to ulen before the search):
	 *  - index < 0 (from -1, or from "" which TkGetIntForIndex
	 *    reports as TCL_INDEX_NONE): startOfCluster returns NO
	 *    result; endOfCluster returns the first break (0).
	 *  - index > ulen: startOfCluster returns the last break
	 *    (ulen); endOfCluster returns NO result.
	 * "No result" means: return TCL_OK without setting a
	 * result, exactly as the original did when idx stayed
	 * TCL_INDEX_NONE. Keep index in CHAR coordinates until
	 * after the endpoint tests; convert to bytes only for the
	 * break search.
	 */
	if (index > ulen) {
	    index = ulen;
	}
	/* index < 0 is left as-is (TCL_INDEX_NONE). */
    }

    /*
     * The wrapper owns Tcl -> strict conversion and remaps breaks
     * back to Tcl offsets. Pass the raw Tcl bytes; do NOT convert
     * here, and do NOT call any mjb_* function here.
     */
    breaks = Tcl_Alloc((size_t) tclLen + 1);
    if (!mojibake_grapheme_breaks(tclBytes, (size_t) tclLen, breaks)) {
	Tcl_Free((char *) breaks);
	Tcl_SetResult(interp, "mojibake grapheme segmentation failed",
		TCL_STATIC);
	return TCL_ERROR;
    }

    if (!isEnd) {
	if (index < 0) {
	    /* startOfCluster with -1 / "": no result. */
	    Tcl_Free((char *) breaks);
	    return TCL_OK;
	}
	/* Greatest break <= index. breaks[0] is always set. */
	{
	    Tcl_Size byteIndex = ByteOffsetForCharIndex(tclBytes, index);
	    result = 0;
	    for (Tcl_Size i = 0; i <= tclLen; i++) {
		if (breaks[i] && i <= byteIndex) {
		    result = i;
		} else if (i > byteIndex) {
		    break;
		}
	    }
	}
    } else {
	Tcl_Size byteIndex;
	if (index < 0) {
	    /* endOfCluster with -1 / "": following(-1) == 0. */
	    byteIndex = -1;
	} else {
	    byteIndex = ByteOffsetForCharIndex(tclBytes, index);
	}
	/* Smallest break > index; none => no result. In
	 * particular, endOfCluster at or past the end of the
	 * string returns no result: byteIndex == tclLen, and no
	 * break is > tclLen. */
	result = -1;
	for (Tcl_Size i = byteIndex + 1; i <= tclLen; i++) {
	    if (breaks[i]) {
		result = i;
		break;
	    }
	}
	if (result < 0) {
	    Tcl_Free((char *) breaks);
	    return TCL_OK;
	}
    }
    Tcl_Free((char *) breaks);
    Tcl_SetObjResult(interp,
	    Tcl_NewLongObj(CharIndexForByteOffset(tclBytes, result)));
    return TCL_OK;
}


#define ICU_WORD_FOLLOWING	0x1
#define ICU_WORD_SPACE		0x2

/*
 * Break-array iterator helpers: emulate ubrk_following/preceding/
 * next/previous over a sorted Tcl_Size break-offset array.
 * *pos tracks the iterator position, exactly as the ICU iterator's
 * position did. Breaks are Tcl BYTE offsets; offsets[0] == 0,
 * offsets[numBreaks-1] == byte length.
 */

/*
 *----------------------------------------------------------------------
 *
 * BreakFollowing --
 *
 *	Emulate ubrk_following over a sorted break-offset array:
 *	return the first break strictly greater than idx, and update
 *	the iterator position *pos to that break's index.
 *
 * Results:
 *	The offset of the first break > idx, or -1 if there is no
 *	such break. *pos is set to the corresponding array index, or
 *	to numBreaks when no break qualifies.
 *
 * Side effects:
 *	Updates *pos. No allocation; caller owns the arrays.
 *
 *----------------------------------------------------------------------
 */
 
static Tcl_Size
BreakFollowing(
    const Tcl_Size *offsets,	/* Sorted break offsets (BYTE). */
    Tcl_Size numBreaks,		/* Number of entries in offsets. */
    Tcl_Size *pos,		/* In/out iterator position. */
    Tcl_Size idx)		/* Current BYTE position. */
{
    Tcl_Size i;
    for (i = 0; i < numBreaks; i++) {
	if (offsets[i] > idx) {
	    *pos = i;
	    return offsets[i];
	}
    }
    *pos = numBreaks;
    return -1;
}

/*
 *----------------------------------------------------------------------
 *
 * BreakPreceding --
 *
 *	Emulate ubrk_preceding over a sorted break-offset array:
 *	return the last break strictly less than idx, and update the
 *	iterator position *pos to that break's index.
 *
 * Results:
 *	The offset of the last break < idx, or -1 if there is no
 *	such break. *pos is set to that break's array index, or to 0
 *	when no break qualifies.
 *
 * Side effects:
 *	Updates *pos. No allocation; caller owns the arrays.
 *
 *----------------------------------------------------------------------
 */
 
static Tcl_Size
BreakPreceding(
    const Tcl_Size *offsets,	/* Sorted break offsets (BYTE). */
    Tcl_Size numBreaks,		/* Number of entries in offsets. */
    Tcl_Size *pos,		/* In/out iterator position. */
    Tcl_Size idx)		/* Current BYTE position. */
{
    Tcl_Size i, found = -1;
    for (i = 0; i < numBreaks; i++) {
	if (offsets[i] < idx) {
	    found = i;
	} else {
	    break;
	}
    }
    *pos = (found >= 0) ? found : 0;
    return (found >= 0) ? offsets[found] : -1;
}

/*
 *----------------------------------------------------------------------
 *
 * BreakNext --
 *
 *	Emulate ubrk_next over a sorted break-offset array: advance
 *	the iterator position by one and return the break there.
 *
 * Results:
 *	The offset at the new position, or -1 if the iterator has
 *	run past the last break.
 *
 * Side effects:
 *	Updates *pos (increments it).
 *
 *----------------------------------------------------------------------
 */
 
static Tcl_Size
BreakNext(
    const Tcl_Size *offsets,	/* Sorted break offsets (BYTE). */
    Tcl_Size numBreaks,		/* Number of entries in offsets. */
    Tcl_Size *pos)		/* In/out iterator position. */
{
    (*pos)++;
    return (*pos < numBreaks) ? offsets[*pos] : -1;
}

/*
 *----------------------------------------------------------------------
 *
 * BreakPrev --
 *
 *	Emulate ubrk_previous over a sorted break-offset array:
 *	decrement the iterator position by one and return the break
 *	there.
 *
 * Results:
 *	The offset at the new position, or -1 if the iterator has
 *	run before the first break.
 *
 * Side effects:
 *	Updates *pos (decrements it).
 *
 *----------------------------------------------------------------------
 */
 
static Tcl_Size
BreakPrev(
    const Tcl_Size *offsets,	/* Sorted break offsets (BYTE). */
    Tcl_Size numBreaks,		/* Number of entries in offsets (unused). */
    Tcl_Size *pos)		/* In/out iterator position. */
{
    /* numBreaks unused: previous() only walks toward 0. */
    (void) numBreaks;
    (*pos)--;
    return (*pos >= 0) ? offsets[*pos] : -1;
}

/*
 *----------------------------------------------------------------------
 *
 * IsSpaceAtByte --
 *
 *	Test whether the Unicode character starting at a given byte
 *	offset is a space character, mirroring the original
 *	"(idx >= len) || Tcl_UniCharIsSpace(ustr[idx])" test used
 *	by the previous implementation.
 *
 * Results:
 *	1 if byteOffset is at or past byteLen, or if the character
 *	starting there satisfies Tcl_UniCharIsSpace; 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
 
static int
IsSpaceAtByte(
    const char *tclBytes,	/* Tcl string bytes, NUL-terminated. */
    Tcl_Size byteOffset,	/* BYTE offset of the character to test. */
    Tcl_Size byteLen)		/* BYTE length of the string. */
{
    Tcl_UniChar ch;
    if (byteOffset >= byteLen) {
	return 1;
    }
    Tcl_UtfToUniChar(tclBytes + byteOffset, &ch);
    return Tcl_UniCharIsSpace(ch);
}

/*
 *----------------------------------------------------------------------
 *
 * TkIcuWordCmd --
 *
 *	Implementation of the ::tk::startOfNextWord,
 *	::tk::startOfPreviousWord, and ::tk::endOfWord commands.
 *	These find word boundaries in a Unicode string using the
 *	mojibake word-break segmenter, replaying the semantics of
 *	the original ICU ubrk_* iterator walk.
 *
 *	clientData is a bitwise OR of ICU_WORD_FOLLOWING and
 *	ICU_WORD_SPACE selecting which of the three commands this
 *	invocation implements:
 *
 *	  ICU_WORD_FOLLOWING                 -> startOfNextWord
 *	  ICU_WORD_FOLLOWING | ICU_WORD_SPACE -> endOfWord
 *	  0                                  -> startOfPreviousWord
 *
 *	The command syntax is:
 *
 *	    ::tk::startOfNextWord     str index ?locale?
 *	    ::tk::startOfPreviousWord str index ?locale?
 *	    ::tk::endOfWord           str index ?locale?
 *
 *	If locale is the literal string "TCL8", the command is
 *	delegated to the corresponding Tcl 8 core command
 *	(tcl_startOfNextWord, tcl_startOfPreviousWord, or
 *	tcl_endOfWord) to preserve legacy behavior.
 *
 * Results:
 *	TCL_OK on success. On success the interpreter result is set
 *	to the CHAR index of the found word boundary, or is left
 *	unset if no boundary exists (TCL_INDEX_NONE). TCL_ERROR is
 *	returned, with the interpreter result set to a diagnostic
 *	message, if the argument count is wrong, if the index cannot
 *	be parsed by TkGetIntForIndex, or if word segmentation fails.
 *
 * Side effects:
 *	Allocates a temporary break array and a break-offset list,
 *	both freed before returning. May evaluate a Tcl command via
 *	Tcl_EvalObjv when locale is "TCL8".
 *
 *----------------------------------------------------------------------
 */
 
static int
TkIcuWordCmd(
    void *clientData,		/* ICU_WORD_* flags selecting the command. */
    Tcl_Interp *interp,		/* Interpreter for error reporting. */
    Tcl_Size objc,		/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    int flags = PTR2INT(clientData);
    int following = (flags & ICU_WORD_FOLLOWING) != 0;
    int wantSpace = (flags & ICU_WORD_SPACE) != 0;
    int wordFlags = 1; /* All three commands here are word commands. */
    Tcl_Size tclLen, index, result;
    const char *tclBytes;
    const char *locale = NULL;
    unsigned char *breakArr = NULL;
    Tcl_Size *offsets = NULL;
    Tcl_Size numBreaks = 0, pos = 0, idx;

    if (objc < 3 || objc > 4) {
	Tcl_WrongNumArgs(interp, 1, objv, "str start ?locale?");
	return TCL_ERROR;
    }
    tclBytes = Tcl_GetStringFromObj(objv[1], &tclLen);
    if (objc == 4) {
	locale = Tcl_GetString(objv[3]);
    }
    if (locale != NULL && strcmp(locale, "TCL8") == 0) {
	Tcl_Obj *cmdObjv[3];
	const char *cmdName;
	if (wantSpace) {
	    cmdName = "tcl_endOfWord";
	} else if (following) {
	    cmdName = "tcl_startOfNextWord";
	} else {
	    cmdName = "tcl_startOfPreviousWord";
	}
	cmdObjv[0] = Tcl_NewStringObj(cmdName, -1);
	cmdObjv[1] = objv[1];
	cmdObjv[2] = objv[2];
	return Tcl_EvalObjv(interp, 3, cmdObjv, 0);
    }
    {
	Tcl_Size ulen = Tcl_GetCharLength(objv[1]);
	if (TkGetIntForIndex(objv[2], ulen - 1, 0, &index) != TCL_OK) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "bad index \"%s\": must be integer?[+-]integer?, "
		    "end?[+-]integer?, or \"\"",
		    Tcl_GetString(objv[2])));
	    Tcl_SetErrorCode(interp, "TK", "ICU", "INDEX", (char *)NULL);
	    return TCL_ERROR;
	}
	if (index < 0) {
	    index = 0;
	} else if (index > ulen) {
	    index = ulen;
	}
	/* Work in Tcl BYTE offsets internally; convert back on return. */
	index = ByteOffsetForCharIndex(tclBytes, index);
    }

    breakArr = Tcl_Alloc((size_t) tclLen + 1);
    /* Wrapper owns Tcl -> strict conversion; see header comment. */
    if (!mojibake_word_breaks_with_dict(tclBytes, (size_t) tclLen,
	    breakArr, NULL)) {
	Tcl_Free((char *) breakArr);
	Tcl_SetResult(interp, "mojibake word segmentation failed",
		TCL_STATIC);
	return TCL_ERROR;
    }
    /* Convert the 0/1 break array into a sorted offset list. */
    {
	Tcl_Size count = 0, k, w = 0;
	for (k = 0; k <= tclLen; k++) {
	    if (breakArr[k]) {
		count++;
	    }
	}
	offsets = (Tcl_Size *) Tcl_Alloc(
		(size_t) (count + 1) * sizeof(Tcl_Size));
	for (k = 0; k <= tclLen; k++) {
	    if (breakArr[k]) {
		offsets[w++] = k;
	    }
	}
	numBreaks = w;
    }
    Tcl_Free((char *) breakArr);
    breakArr = NULL;

    /*
     * Replay the ORIGINAL ubrk_* walk over the break list, step for
     * step. This is a transliteration of the original startEndOfCmd
     * FLAG_WORD logic -- preceding / following, then skipping space
     * segments with next / previous -- not a re-derivation. That is
     * what makes startOfNextWord("hello world", 0) return 6 (the
     * next word) rather than 5 (the space boundary): the following()
     * lands on the space boundary, the WORD test sees a space
     * segment, and next() advances past it.
     */
    idx = index;
    if (following) {
	if (idx < 0) {
	    idx = 0;
	}
	idx = BreakFollowing(offsets, numBreaks, &pos, idx);
	if (idx >= tclLen) {
	    idx = TCL_INDEX_NONE;
	}
    } else if (idx > 0) {
	idx = BreakPreceding(offsets, numBreaks, &pos, idx);
	if (idx == 0) {
	    wordFlags = 0; /* 0 reached: don't do a further search */
	    if (IsSpaceAtByte(tclBytes, 0, tclLen)) {
		idx = TCL_INDEX_NONE; /* Only spaces before start: no word */
	    }
	}
    }
    if (wordFlags && (idx != TCL_INDEX_NONE)) {
	if ((!wantSpace) == IsSpaceAtByte(tclBytes, idx, tclLen)) {
	    if (following) {
		idx = BreakNext(offsets, numBreaks, &pos);
		if (idx >= tclLen) {
		    idx = TCL_INDEX_NONE;
		}
	    } else {
		/* Skip all space segments, e.g. " " followed by "\n" */
		do {
		    idx = BreakPrev(offsets, numBreaks, &pos);
		} while (idx > 0 && IsSpaceAtByte(tclBytes, idx, tclLen));
		if (idx == 0 && IsSpaceAtByte(tclBytes, 0, tclLen)) {
		    idx = TCL_INDEX_NONE;
		}
	    }
	} else if (idx == 0 && !following) {
	    idx = TCL_INDEX_NONE;
	}
    }
    result = idx;
    Tcl_Free((char *) offsets);
    if (result != TCL_INDEX_NONE) {
	Tcl_SetObjResult(interp, Tcl_NewLongObj(
		CharIndexForByteOffset(tclBytes, result)));
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * TkIcuWordBreakCmd --
 *
 *	Implementation of the ::tk::wordBreakBefore and
 *	::tk::wordBreakAfter commands, which tk::TextSelectTo (double-click
 *	word selection) reaches through tk::TextPrevPos and
 *	tk::TextNextPos. Without these commands, tk.tcl falls back to
 *	tcl_wordBreakBefore/After, which only classify characters as word
 *	or non-word and so split grapheme clusters and Thai words.
 *
 *	clientData is 0 for wordBreakBefore and 1 for wordBreakAfter. The
 *	command syntax is:
 *
 *	    ::tk::wordBreakBefore str index ?locale?
 *	    ::tk::wordBreakAfter  str index ?locale?
 *
 *	If locale is the literal string "TCL8", the command is delegated
 *	to tcl_wordBreakBefore or tcl_wordBreakAfter.
 *
 * Results:
 *	TCL_OK on success. wordBreakAfter sets the result to the CHAR
 *	index of the first word break strictly after index; wordBreakBefore
 *	sets it to the CHAR index of the last word break at or before
 *	index, ignoring a break at the end of the string. The result is left unset when there is no such break (for
 *	wordBreakAfter, a break at the end of the string does not count; for
 *	wordBreakBefore, a break at 0 does not count), so the caller goes on
 *	to the adjacent line. TCL_ERROR if the arguments are bad or word
 *	segmentation fails.
 *
 * Side effects:
 *	Allocates temporary break arrays and frees them before returning.
 *
 *----------------------------------------------------------------------
 */

static int
TkIcuWordBreakCmd(
    void *clientData,		/* 0 = wordBreakBefore, 1 = wordBreakAfter. */
    Tcl_Interp *interp,		/* Interpreter for error reporting. */
    Tcl_Size objc,		/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    int after = PTR2INT(clientData);
    Tcl_Size tclLen, ulen, index, idx, pos = 0, numBreaks = 0, k;
    const char *tclBytes;
    unsigned char *breakArr;
    Tcl_Size *offsets;

    if (objc < 3 || objc > 4) {
	Tcl_WrongNumArgs(interp, 1, objv, "str start ?locale?");
	return TCL_ERROR;
    }
    if (objc == 4 && strcmp(Tcl_GetString(objv[3]), "TCL8") == 0) {
	Tcl_Obj *cmdObjv[3];
	int code;

	cmdObjv[0] = Tcl_NewStringObj(
		after ? "tcl_wordBreakAfter" : "tcl_wordBreakBefore", -1);
	cmdObjv[1] = objv[1];
	cmdObjv[2] = objv[2];
	Tcl_IncrRefCount(cmdObjv[0]);
	code = Tcl_EvalObjv(interp, 3, cmdObjv, 0);
	Tcl_DecrRefCount(cmdObjv[0]);
	return code;
    }
    tclBytes = Tcl_GetStringFromObj(objv[1], &tclLen);
    ulen = Tcl_GetCharLength(objv[1]);
    if (ulen == 0) {
	return TCL_OK;
    }
    if (TkGetIntForIndex(objv[2], ulen - 1, 0, &index) != TCL_OK) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"bad index \"%s\": must be integer?[+-]integer?, "
		"end?[+-]integer?, or \"\"", Tcl_GetString(objv[2])));
	Tcl_SetErrorCode(interp, "TK", "ICU", "INDEX", (char *)NULL);
	return TCL_ERROR;
    }
    if (index < 0) {
	index = 0;
    } else if (index > ulen) {
	index = ulen;
    }
    idx = ByteOffsetForCharIndex(tclBytes, index);

    breakArr = Tcl_Alloc((size_t) tclLen + 1);
    if (!mojibake_word_breaks_with_dict(tclBytes, (size_t) tclLen,
	    breakArr, NULL)) {
	Tcl_Free((char *) breakArr);
	Tcl_SetResult(interp, "mojibake word segmentation failed",
		TCL_STATIC);
	return TCL_ERROR;
    }
    offsets = (Tcl_Size *) Tcl_Alloc(
	    (size_t) (tclLen + 1) * sizeof(Tcl_Size));
    for (k = 0; k <= tclLen; k++) {
	if (breakArr[k]) {
	    offsets[numBreaks++] = k;
	}
    }
    Tcl_Free((char *) breakArr);

    if (after) {
	idx = BreakFollowing(offsets, numBreaks, &pos, idx);
	if (idx >= tclLen) {
	    idx = TCL_INDEX_NONE;
	}
    } else {
	/* A break at the very end of the string does not count. */
	if (idx > tclLen - 1) {
	    idx = tclLen - 1;
	}
	idx = BreakPreceding(offsets, numBreaks, &pos, idx + 1);
	if (idx <= 0) {
	    idx = TCL_INDEX_NONE;
	}
    }
    Tcl_Free((char *) offsets);
    if (idx != TCL_INDEX_NONE) {
	Tcl_SetObjResult(interp, Tcl_NewWideIntObj(
		CharIndexForByteOffset(tclBytes, idx)));
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Icu_Init --
 *
 *	Register the ICU-backed Tk commands in the given interpreter.
 *	Called once per interpreter during Tk initialization.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates seven interpreter commands: ::tk::startOfCluster,
 *	::tk::endOfCluster, ::tk::startOfNextWord,
 *	::tk::startOfPreviousWord, ::tk::endOfWord, ::tk::wordBreakBefore,
 *	and ::tk::wordBreakAfter. Each is
 *	associated with the appropriate TkIcu*Cmd implementation and
 *	void * flags.
 *
 *----------------------------------------------------------------------
 */
 
void
Icu_Init(
    Tcl_Interp *interp)		/* Interpreter in which to create commands. */
{
    Tcl_CreateObjCommand2(interp, "::tk::startOfCluster",
	    TkIcuStartEndOfClusterCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::endOfCluster",
	    TkIcuStartEndOfClusterCmd, INT2PTR(1), NULL);
    Tcl_CreateObjCommand2(interp, "::tk::startOfNextWord",
	    TkIcuWordCmd, INT2PTR(ICU_WORD_FOLLOWING), NULL);
    Tcl_CreateObjCommand2(interp, "::tk::startOfPreviousWord",
	    TkIcuWordCmd, INT2PTR(0), NULL);
    Tcl_CreateObjCommand2(interp, "::tk::endOfWord",
	    TkIcuWordCmd,
	    INT2PTR(ICU_WORD_FOLLOWING | ICU_WORD_SPACE),
	    NULL);
    Tcl_CreateObjCommand2(interp, "::tk::wordBreakBefore",
	    TkIcuWordBreakCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::wordBreakAfter",
	    TkIcuWordBreakCmd, INT2PTR(1), NULL);
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
