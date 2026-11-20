/*
 * tkTextGrapheme.c --
 *
 *      Implements line breaks (UAX #14) and word/sentence breaks (UAX #29)
 *      for Unicode extended grapheme clusters utilizing the mojibake Unicode
 *      library (version 0.8.0). Break boundaries come from the
 *      iterator's state.position field, which mojibake sets to the
 *      byte offset of the boundary the last returned break type
 *      refers to; no boundary is reconstructed from the current
 *      codepoint here.  Also, for scripts without spaces (Thai, Lao, Khmer),
 *      implements a forward maximum-matching word segmenter using
 *      ICU-derived dictionaries.
 *
 * Copyright (c) 2026 Kevin Walzer.
 *
 * See the file "license.terms" for information on usage and redistribution.
 */

#include "tkInt.h"
#include "tkText.h"
#include <mojibake.h>
#include <string.h>
#include <stdlib.h>
#include "dictionaries.h"


/*
 * ---------------------------------------------------------------
 * TclHasEncodedNul --
 *
 *   Test whether a Tcl (modified UTF-8) buffer contains an encoded
 *   NUL, the two-byte sequence C0 80. In valid Tcl UTF-8, C0 only
 *   occurs as the first byte of that sequence (U+0000 is the only
 *   codepoint Tcl encodes differently from strict UTF-8).
 *
 * Why this scan exists:
 *   Mojibake accepts only strict UTF-8, and its mjb_*_break_next
 *   iterators have no status return. On inspection of the
 *   mojibake decoder, an unconverted C0 80 is not rejected
 *   visibly: it decodes as two U+FFFD codepoints (class Other,
 *   not Control), so the NUL would be silently misclassified
 *   and its boundaries wrong. The buffer is therefore checked
 *   before mojibake sees it, not after.
 *
 *   The check is a separate, allocation-free O(n) pass so that
 *   the common case stays cheap: buffers with no encoded NUL,
 *   which is nearly all text, take the fast path with no copy
 *   and no offset map. Only buffers that actually contain C0 80
 *   pay for ConvertTclToUtf8(), which allocates and remaps
 *   offsets. Converting unconditionally would pay that cost
 *   on every call.
 *
 * Results:
 *   Returns 1 if the buffer contains C0 80, 0 otherwise.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
 
static int
TclHasEncodedNul(const char *buf,
		 size_t byteLen)
{
    size_t i;

    for (i = 0; i + 1 < byteLen; i++) {
	if ((unsigned char) buf[i] == 0xC0
		&& (unsigned char) buf[i + 1] == 0x80) {
	    return 1;
	}
    }
    return 0;
}

/*
 * ---------------------------------------------------------------
 * ConvertTclToUtf8 --
 *
 *   Convert a Tcl (modified UTF-8) buffer to strict UTF-8 for the
 *   mojibake library, which expects strict UTF-8 and rejects the
 *   overlong C0 80 encoding of U+0000 as malformed. Every C0 80
 *   pair is collapsed to the single byte 00; every other byte is
 *   copied unchanged, since Tcl UTF-8 and strict UTF-8 agree on all
 *   other codepoints.
 *
 *   Also builds an offset map so boundaries computed by mojibake in
 *   strict-UTF-8 coordinates can be translated back to Tcl
 *   coordinates: offsetMap[utf8Offset] is the Tcl byte offset of
 *   the byte at utf8Offset, and offsetMap[utf8Len] == tclLen.
 *   Each collapsed NUL therefore shifts every later Tcl offset by
 *   one relative to its strict-UTF-8 offset.
 *
 * Results:
 *   Returns 1 on success, with *utf8BufPtr set to a malloced
 *   strict-UTF-8 buffer (caller frees it), *utf8LenPtr set to its
 *   length, and *offsetMapPtr set to a malloced offset map of
 *   *utf8LenPtr + 1 entries (caller frees it). Returns 0 on
 *   allocation failure, with all three out-parameters left NULL/0.
 *
 * Side effects:
 *   Allocates memory.
 * ---------------------------------------------------------------
 */

static int
ConvertTclToUtf8(const char *buf,
		 size_t byteLen,
		 char **utf8BufPtr,
		 size_t *utf8LenPtr,
		 size_t **offsetMapPtr)
{
    char *utf8Buf;
    size_t *offsetMap;
    size_t i = 0, j = 0;

    *utf8BufPtr = NULL;
    *utf8LenPtr = 0;
    *offsetMapPtr = NULL;

    /*
     * The converted buffer is at most as long as the input, since
     * conversion only ever collapses two bytes into one. Allocate
     * byteLen + 1 so the offset map can hold the end position, and
     * so the buffer itself is always NUL-terminated for safety even
     * though every consumer here is length-aware.
     */
    utf8Buf = (char *) malloc(byteLen + 1);
    offsetMap = (size_t *) malloc((byteLen + 1) * sizeof(size_t));
    if (utf8Buf == NULL || offsetMap == NULL) {
	if (utf8Buf != NULL) {
	    free(utf8Buf);
	}
	if (offsetMap != NULL) {
	    free(offsetMap);
	}
	return 0;
    }

    while (i < byteLen) {
	offsetMap[j] = i;
	if (i + 1 < byteLen
		&& (unsigned char) buf[i] == 0xC0
		&& (unsigned char) buf[i + 1] == 0x80) {
	    utf8Buf[j] = '\0';
	    i += 2;
	} else {
	    utf8Buf[j] = buf[i];
	    i += 1;
	}
	j++;
    }
    offsetMap[j] = byteLen;
    utf8Buf[j] = '\0';

    *utf8BufPtr = utf8Buf;
    *utf8LenPtr = j;
    *offsetMapPtr = offsetMap;
    return 1;
}

/*
 * ---------------------------------------------------------------
 * TclOffsetToUtf8Offset --
 *
 *   Translate a Tcl byte offset to the corresponding strict-UTF-8
 *   byte offset using the map built by ConvertTclToUtf8. An offset
 *   in the middle of a C0 80 pair (i.e. just past the C0 byte) maps
 *   to just past the converted NUL byte, so callers that pass a
 *   non-boundary offset still land on the correct side of the NUL.
 *
 * Results:
 *   Returns the strict-UTF-8 offset.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

static size_t
TclOffsetToUtf8Offset(const size_t *offsetMap,
		      size_t utf8Len,
		      size_t tclOffset)
{
    size_t j = 0;

    while (j < utf8Len && offsetMap[j] < tclOffset) {
	j++;
    }
    return j;
}

/*
 * ---------------------------------------------------------------
 * Utf8OffsetToTclOffset --
 *
 *   Translate a strict-UTF-8 byte offset back to a Tcl byte offset
 *   using the map built by ConvertTclToUtf8.
 *
 * Results:
 *   Returns the Tcl byte offset.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

static size_t
Utf8OffsetToTclOffset(const size_t *offsetMap,
		      size_t utf8Offset)
{
    return offsetMap[utf8Offset];
}

/*
 * ---------------------------------------------------------------
 * RemapBreaksToTcl --
 *
 *   Translate a breaks array computed by mojibake in strict-UTF-8
 *   coordinates back to Tcl coordinates, in place. The array on
 *   entry has utf8Len + 1 entries; on exit the first tclLen + 1
 *   entries hold the Tcl-coordinate breaks. A break at strict-UTF-8
 *   offset p becomes a break at Tcl offset offsetMap[p].
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Rewrites the breaks array. Frees the temporary copy.
 * ---------------------------------------------------------------
 */

static void
RemapBreaksToTcl(unsigned char *breaks,
		 size_t utf8Len,
		 size_t tclLen,
		 const size_t *offsetMap)
{
    unsigned char *converted;
    size_t i;

    converted = (unsigned char *) malloc(utf8Len + 1);
    if (converted == NULL) {
	/*
	 * Out of memory: leave the array as-is rather than corrupting
	 * it. The offsets will be off by the NUL shift, but the array
	 * still has valid break entries for the converted buffer.
	 */
	return;
    }
    memcpy(converted, breaks, utf8Len + 1);
    memset(breaks, 0, tclLen + 1);
    for (i = 0; i <= utf8Len; i++) {
	if (converted[i]) {
	    breaks[offsetMap[i]] = 1;
	}
    }
    free(converted);
}




/*
 * ---------------------------------------------------------------
 * mojibake_grapheme_breaks --
 *
 *   Compute all grapheme cluster break boundaries in a UTF-8 buffer.
 *   Implements Unicode UAX #29 extended grapheme cluster boundaries.
 *
 *   The input buffer is in Tcl (modified UTF-8) encoding, in
 *   which U+0000 is stored as C0 80. Mojibake expects strict
 *   UTF-8 and rejects C0 80 as malformed, so when the buffer
 *   contains an encoded NUL it is converted to strict UTF-8
 *   before calling mojibake, and every boundary returned is
 *   translated back to Tcl byte offsets. Buffers without an
 *   encoded NUL take the fast path with no copy. The conversion
 *   is required, not optional: on inspection of the mojibake
 *   decoder, unconverted C0 80 decodes as two U+FFFDs (Other,
 *   not Control), and the mjb_*_break_next iterators have no status
 *   return with which to report that, so they would silently
 *   misclassify the NUL. After conversion, a 00 byte is a
 *   genuine U+0000 Control and mojibake, given an explicit
 *   byte length, is expected to iterate past it like any other
 *   codepoint; this is the behaviour this file relies on.
 *
 * Results:
 *   Returns 1 on success. The breaks array is filled with 1 at each
 *   grapheme boundary byte offset (including 0 and byteLen).
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_grapheme_breaks(const char *buf,
			 size_t byteLen,
			 unsigned char *breaks)
{
    mjb_grapheme_break_state state;
    mjb_break_type bt;
    size_t tclLen = byteLen;
    char *convertedBuf = NULL;
    size_t *offsetMap = NULL;
    int converted = 0;
    if (buf != NULL && byteLen > 0 && TclHasEncodedNul(buf, byteLen)) {
	if (!ConvertTclToUtf8(buf, byteLen, &convertedBuf, &byteLen, &offsetMap)) {
	    return 0;
	}
	buf = convertedBuf;
	converted = 1;
    }
    if (buf == NULL || breaks == NULL) { if (converted) { free(convertedBuf); free(offsetMap); } return 0; }
    memset(breaks, 0, byteLen + 1);
    if (byteLen == 0) { breaks[0]=1; return 1; }
    breaks[0]=1;
    /*
     * Call mojibake once on the whole buffer. After conversion,
     * embedded U+0000 is represented as byte 00 and is expected
     * to be handled as a normal control codepoint when an explicit
     * length is supplied (as opposed to MJB_NUL_TERMINATED):
     * decoded as U+0000, breaking before/after it per GB4/GB5,
     * with RI / ZWJ / Indic state carried across it. Splitting
     * at NULs and resetting state per segment would discard that
     * cross-boundary context, so do not do that. The only reason
     * the buffer ever contains 00 here is ConvertTclToUtf8()
     * above, which collapsed each Tcl C0 80 pair to one 00;
     * unconverted C0 80 would instead decode as two U+FFFDs.
     * This relies on mojibake's length-delimited behaviour; if
     * that ever changes, this single-call assumption must be
     * re-checked against the mojibake source and tests.
     */
    state.index = 0;
    mjb_text text = mjb_text_utf8(buf, byteLen);
    while ((bt = mjb_grapheme_break_next(&text, &state)) != MJB_BT_NOT_SET) {
        if (bt == MJB_BT_ALLOWED) {
            size_t pos = state.position;
            if (pos <= byteLen) {
                breaks[pos] = 1;
            }
        }
    }
    breaks[byteLen]=1;
    if (converted) {
	RemapBreaksToTcl(breaks, byteLen, tclLen, offsetMap);
	free(convertedBuf);
	free(offsetMap);
    }
    return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_grapheme_next --
 *
 *   Find the next grapheme cluster boundary after a given byte offset.
 *
 *   The input buffer is in Tcl (modified UTF-8) encoding, in
 *   which U+0000 is stored as C0 80. Mojibake expects strict
 *   UTF-8 and rejects C0 80 as malformed, so when the buffer
 *   contains an encoded NUL it is converted to strict UTF-8
 *   before calling mojibake, and every boundary returned is
 *   translated back to Tcl byte offsets. Buffers without an
 *   encoded NUL take the fast path with no copy. The conversion
 *   is required, not optional: on inspection of the mojibake
 *   decoder, unconverted C0 80 decodes as two U+FFFDs (Other,
 *   not Control), and the mjb_*_break_next iterators have no status
 *   return with which to report that, so they would silently
 *   misclassify the NUL. After conversion, a 00 byte is a
 *   genuine U+0000 Control and mojibake, given an explicit
 *   byte length, is expected to iterate past it like any other
 *   codepoint; this is the behaviour this file relies on.
 *
 * Results:
 *   Returns 1 on success. nextOffset is set to the byte offset of
 *   the next grapheme boundary, or byteLen if none exists.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_grapheme_next(const char *buf,
		       size_t byteLen,
		       size_t byteOffset,
		       size_t *nextOffset)
{
    mjb_grapheme_break_state state;
    mjb_break_type bt;
    if (nextOffset==NULL) return 0;
    if (buf==NULL || byteOffset>=byteLen) { *nextOffset=byteLen; return buf!=NULL; }

    char *convertedBuf = NULL;
    size_t *offsetMap = NULL;
    int converted = 0;
    if (buf != NULL && byteLen > 0 && TclHasEncodedNul(buf, byteLen)) {
	if (!ConvertTclToUtf8(buf, byteLen, &convertedBuf, &byteLen, &offsetMap)) {
	    return 0;
	}
	buf = convertedBuf;
	converted = 1;
	byteOffset = TclOffsetToUtf8Offset(offsetMap, byteLen, byteOffset);
    }
    state.index=0;
    mjb_text text = mjb_text_utf8(buf, byteLen);
    while ((bt = mjb_grapheme_break_next(&text, &state)) != MJB_BT_NOT_SET) {
	if (bt==MJB_BT_ALLOWED) {
	    size_t pos=state.position;
	    if (pos>byteOffset) { *nextOffset= converted ? Utf8OffsetToTclOffset(offsetMap, pos) : pos; if (converted) { free(convertedBuf); free(offsetMap); } return 1; }
	}
    }
    *nextOffset= converted ? Utf8OffsetToTclOffset(offsetMap, byteLen) : byteLen; if (converted) { free(convertedBuf); free(offsetMap); } return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_grapheme_prev --
 *
 *   Find the previous grapheme cluster boundary before a given byte offset.
 *
 *   The input buffer is in Tcl (modified UTF-8) encoding, in
 *   which U+0000 is stored as C0 80. Mojibake expects strict
 *   UTF-8 and rejects C0 80 as malformed, so when the buffer
 *   contains an encoded NUL it is converted to strict UTF-8
 *   before calling mojibake, and every boundary returned is
 *   translated back to Tcl byte offsets. Buffers without an
 *   encoded NUL take the fast path with no copy. The conversion
 *   is required, not optional: on inspection of the mojibake
 *   decoder, unconverted C0 80 decodes as two U+FFFDs (Other,
 *   not Control), and the mjb_*_break_next iterators have no status
 *   return with which to report that, so they would silently
 *   misclassify the NUL. After conversion, a 00 byte is a
 *   genuine U+0000 Control and mojibake, given an explicit
 *   byte length, is expected to iterate past it like any other
 *   codepoint; this is the behaviour this file relies on.
 *
 * Results:
 *   Returns 1 on success. prevOffset is set to the byte offset of
 *   the previous grapheme boundary, or 0 if none exists.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_grapheme_prev(const char *buf,
		       size_t byteLen,
		       size_t byteOffset,
		       size_t *prevOffset)
{
    mjb_grapheme_break_state state;
    mjb_break_type bt;
    size_t last=0;
    if (prevOffset==NULL) return 0;
    if (buf==NULL || byteOffset==0) { *prevOffset=0; return buf!=NULL; }

    char *convertedBuf = NULL;
    size_t *offsetMap = NULL;
    int converted = 0;
    if (buf != NULL && byteLen > 0 && TclHasEncodedNul(buf, byteLen)) {
	if (!ConvertTclToUtf8(buf, byteLen, &convertedBuf, &byteLen, &offsetMap)) {
	    return 0;
	}
	buf = convertedBuf;
	converted = 1;
	byteOffset = TclOffsetToUtf8Offset(offsetMap, byteLen, byteOffset);
    }
    state.index=0;
    mjb_text text = mjb_text_utf8(buf, byteLen);
    while ((bt = mjb_grapheme_break_next(&text, &state)) != MJB_BT_NOT_SET) {
	if (bt==MJB_BT_ALLOWED) {
	    size_t pos=state.position;
	    if (pos>=byteOffset) break;
	    last=pos;
	}
    }
    *prevOffset= converted ? Utf8OffsetToTclOffset(offsetMap, last) : last; if (converted) { free(convertedBuf); free(offsetMap); } return 1;
}


/*
 * ---------------------------------------------------------------
 * mojibake_line_breaks --
 *
 *   Compute all line break boundaries in a UTF-8 buffer.
 *   Implements Unicode UAX #14 line breaking rules.
 *
 *   The input buffer is in Tcl (modified UTF-8) encoding, in
 *   which U+0000 is stored as C0 80. Mojibake expects strict
 *   UTF-8 and rejects C0 80 as malformed, so when the buffer
 *   contains an encoded NUL it is converted to strict UTF-8
 *   before calling mojibake, and every boundary returned is
 *   translated back to Tcl byte offsets. Buffers without an
 *   encoded NUL take the fast path with no copy. The conversion
 *   is required, not optional: on inspection of the mojibake
 *   decoder, unconverted C0 80 decodes as two U+FFFDs (Other,
 *   not Control), and the mjb_*_break_next iterators have no status
 *   return with which to report that, so they would silently
 *   misclassify the NUL. After conversion, a 00 byte is a
 *   genuine U+0000 Control and mojibake, given an explicit
 *   byte length, is expected to iterate past it like any other
 *   codepoint; this is the behaviour this file relies on.
 *
 * Results:
 *   Returns 1 on success. The breaks array is filled with 1 at each
 *   line break boundary byte offset (including 0 and byteLen).
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

static int
MojibakeLineBreaksBase(const char *buf,
		     size_t byteLen,
		     unsigned char *breaks)
{
    mjb_line_break_state state;
    mjb_break_type bt;
    size_t tclLen = byteLen;
    char *convertedBuf = NULL;
    size_t *offsetMap = NULL;
    int converted = 0;
    if (buf != NULL && byteLen > 0 && TclHasEncodedNul(buf, byteLen)) {
	if (!ConvertTclToUtf8(buf, byteLen, &convertedBuf, &byteLen, &offsetMap)) {
	    return 0;
	}
	buf = convertedBuf;
	converted = 1;
    }
    if (!buf || !breaks) { if (converted) { free(convertedBuf); free(offsetMap); } return 0; }
    memset(breaks,0,byteLen+1);
    if (byteLen==0) { breaks[0]=1; return 1; }
    breaks[0]=1;
    /*
     * Call mojibake once on the whole buffer. After conversion,
     * embedded U+0000 is represented as byte 00 and is expected
     * to be handled as a normal control codepoint when an explicit
     * length is supplied, with UAX #14 breaking around it.
     * Splitting at NULs and resetting state per segment would
     * discard the lookahead (spaces, quotes) that line breaking
     * needs, so do not do that. See the grapheme bulk function
     * for why C0 80 must be converted to 00 before this call,
     * and for the caveat that this relies on mojibake's
     * length-delimited behaviour.
     */
    state.index=0;
    mjb_text text = mjb_text_utf8(buf, byteLen);
    while ((bt = mjb_line_break_next(&text, &state)) != MJB_BT_NOT_SET) {
        if (bt==MJB_BT_ALLOWED) {
            size_t pos=state.position;
            if (pos<=byteLen) breaks[pos]=1;
        }
    }
    breaks[byteLen]=1;
    if (converted) {
	RemapBreaksToTcl(breaks, byteLen, tclLen, offsetMap);
	free(convertedBuf);
	free(offsetMap);
    }
    return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_line_next --
 *
 *   Find the next line break boundary after a given byte offset.
 *
 *   The input buffer is in Tcl (modified UTF-8) encoding, in
 *   which U+0000 is stored as C0 80. Mojibake expects strict
 *   UTF-8 and rejects C0 80 as malformed, so when the buffer
 *   contains an encoded NUL it is converted to strict UTF-8
 *   before calling mojibake, and every boundary returned is
 *   translated back to Tcl byte offsets. Buffers without an
 *   encoded NUL take the fast path with no copy. The conversion
 *   is required, not optional: on inspection of the mojibake
 *   decoder, unconverted C0 80 decodes as two U+FFFDs (Other,
 *   not Control), and the mjb_*_break_next iterators have no status
 *   return with which to report that, so they would silently
 *   misclassify the NUL. After conversion, a 00 byte is a
 *   genuine U+0000 Control and mojibake, given an explicit
 *   byte length, is expected to iterate past it like any other
 *   codepoint; this is the behaviour this file relies on.
 *
 * Results:
 *   Returns 1 on success. nextOffset is set to the byte offset of
 *   the next line break boundary, or byteLen if none exists.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_line_next(const char *buf,
		   size_t byteLen,
		   size_t byteOffset,
		   size_t *nextOffset)
{
    mjb_line_break_state state;
    mjb_break_type bt;
    if (!nextOffset) return 0;
    if (!buf || byteOffset>=byteLen) { *nextOffset=byteLen; return buf!=NULL; }

    char *convertedBuf = NULL;
    size_t *offsetMap = NULL;
    int converted = 0;
    if (buf != NULL && byteLen > 0 && TclHasEncodedNul(buf, byteLen)) {
	if (!ConvertTclToUtf8(buf, byteLen, &convertedBuf, &byteLen, &offsetMap)) {
	    return 0;
	}
	buf = convertedBuf;
	converted = 1;
	byteOffset = TclOffsetToUtf8Offset(offsetMap, byteLen, byteOffset);
    }
    state.index=0;
    mjb_text text = mjb_text_utf8(buf, byteLen);
    while ((bt = mjb_line_break_next(&text, &state)) != MJB_BT_NOT_SET) {
	if (bt==MJB_BT_ALLOWED) {
	    size_t pos=state.position;
	    if (pos>byteOffset) { *nextOffset= converted ? Utf8OffsetToTclOffset(offsetMap, pos) : pos; if (converted) { free(convertedBuf); free(offsetMap); } return 1; }
	}
    }
    *nextOffset= converted ? Utf8OffsetToTclOffset(offsetMap, byteLen) : byteLen; if (converted) { free(convertedBuf); free(offsetMap); } return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_line_prev --
 *
 *   Find the previous line break boundary before a given byte offset.
 *
 *   The input buffer is in Tcl (modified UTF-8) encoding, in
 *   which U+0000 is stored as C0 80. Mojibake expects strict
 *   UTF-8 and rejects C0 80 as malformed, so when the buffer
 *   contains an encoded NUL it is converted to strict UTF-8
 *   before calling mojibake, and every boundary returned is
 *   translated back to Tcl byte offsets. Buffers without an
 *   encoded NUL take the fast path with no copy. The conversion
 *   is required, not optional: on inspection of the mojibake
 *   decoder, unconverted C0 80 decodes as two U+FFFDs (Other,
 *   not Control), and the mjb_*_break_next iterators have no status
 *   return with which to report that, so they would silently
 *   misclassify the NUL. After conversion, a 00 byte is a
 *   genuine U+0000 Control and mojibake, given an explicit
 *   byte length, is expected to iterate past it like any other
 *   codepoint; this is the behaviour this file relies on.
 *
 * Results:
 *   Returns 1 on success. prevOffset is set to the byte offset of
 *   the previous line break boundary, or 0 if none exists.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_line_prev(const char *buf,
		   size_t byteLen,
		   size_t byteOffset,
		   size_t *prevOffset)
{
    mjb_line_break_state state;
    mjb_break_type bt;
    size_t last=0;
    if (!prevOffset) return 0;
    if (!buf || byteOffset==0) { *prevOffset=0; return buf!=NULL; }

    char *convertedBuf = NULL;
    size_t *offsetMap = NULL;
    int converted = 0;
    if (buf != NULL && byteLen > 0 && TclHasEncodedNul(buf, byteLen)) {
	if (!ConvertTclToUtf8(buf, byteLen, &convertedBuf, &byteLen, &offsetMap)) {
	    return 0;
	}
	buf = convertedBuf;
	converted = 1;
	byteOffset = TclOffsetToUtf8Offset(offsetMap, byteLen, byteOffset);
    }
    state.index=0;
    mjb_text text = mjb_text_utf8(buf, byteLen);
    while ((bt = mjb_line_break_next(&text, &state)) != MJB_BT_NOT_SET) {
	if (bt==MJB_BT_ALLOWED) {
	    size_t pos=state.position;
	    if (pos>=byteOffset) break;
	    last=pos;
	}
    }
    *prevOffset= converted ? Utf8OffsetToTclOffset(offsetMap, last) : last; if (converted) { free(convertedBuf); free(offsetMap); } return 1;
}


/*
 * ---------------------------------------------------------------
 * mojibake_word_breaks --
 *
 *   Compute all word break boundaries in a UTF-8 buffer.
 *   Implements Unicode UAX #29 word breaking rules.
 *
 *   The input buffer is in Tcl (modified UTF-8) encoding, in
 *   which U+0000 is stored as C0 80. Mojibake expects strict
 *   UTF-8 and rejects C0 80 as malformed, so when the buffer
 *   contains an encoded NUL it is converted to strict UTF-8
 *   before calling mojibake, and every boundary returned is
 *   translated back to Tcl byte offsets. Buffers without an
 *   encoded NUL take the fast path with no copy. The conversion
 *   is required, not optional: on inspection of the mojibake
 *   decoder, unconverted C0 80 decodes as two U+FFFDs (Other,
 *   not Control), and the mjb_*_break_next iterators have no status
 *   return with which to report that, so they would silently
 *   misclassify the NUL. After conversion, a 00 byte is a
 *   genuine U+0000 Control and mojibake, given an explicit
 *   byte length, is expected to iterate past it like any other
 *   codepoint; this is the behaviour this file relies on.
 *
 * Results:
 *   Returns 1 on success. The breaks array is filled with 1 at each
 *   word break boundary byte offset (including 0 and byteLen).
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

static int
MojibakeWordBreaksBase(const char *buf,
		     size_t byteLen,
		     unsigned char *breaks)
{
    mjb_word_break_state state;
    mjb_break_type bt;
    size_t tclLen = byteLen;
    char *convertedBuf = NULL;
    size_t *offsetMap = NULL;
    int converted = 0;
    if (buf != NULL && byteLen > 0 && TclHasEncodedNul(buf, byteLen)) {
	if (!ConvertTclToUtf8(buf, byteLen, &convertedBuf, &byteLen, &offsetMap)) {
	    return 0;
	}
	buf = convertedBuf;
	converted = 1;
    }
    if (!buf || !breaks) { if (converted) { free(convertedBuf); free(offsetMap); } return 0; }
    memset(breaks,0,byteLen+1);
    if (byteLen==0) { breaks[0]=1; return 1; }
    breaks[0]=1;
    /*
     * Call mojibake once on the whole buffer. After conversion,
     * embedded U+0000 is represented as byte 00 and is expected
     * to be handled as a normal control codepoint when an explicit
     * length is supplied, with UAX #29 breaking around it.
     * Splitting at NULs and resetting state per segment would
     * discard the cross-boundary context (e.g. ZWJ / Regional
     * Indicator state), so do not do that. See the grapheme
     * bulk function for why C0 80 must be converted to 00
     * before this call, and for the caveat that this relies
     * on mojibake's length-delimited behaviour.
     */
    state.index=0;
    mjb_text text = mjb_text_utf8(buf, byteLen);
    while ((bt = mjb_word_break_next(&text, &state)) != MJB_BT_NOT_SET) {
        if (bt==MJB_BT_ALLOWED) {
            size_t pos=state.position;
            if (pos<=byteLen) breaks[pos]=1;
        }
    }
    breaks[byteLen]=1;
    if (converted) {
	RemapBreaksToTcl(breaks, byteLen, tclLen, offsetMap);
	free(convertedBuf);
	free(offsetMap);
    }
    return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_word_next --
 *
 *   Find the next word break boundary after a given byte offset.
 *
 *   The input buffer is in Tcl (modified UTF-8) encoding, in
 *   which U+0000 is stored as C0 80. Mojibake expects strict
 *   UTF-8 and rejects C0 80 as malformed, so when the buffer
 *   contains an encoded NUL it is converted to strict UTF-8
 *   before calling mojibake, and every boundary returned is
 *   translated back to Tcl byte offsets. Buffers without an
 *   encoded NUL take the fast path with no copy. The conversion
 *   is required, not optional: on inspection of the mojibake
 *   decoder, unconverted C0 80 decodes as two U+FFFDs (Other,
 *   not Control), and the mjb_*_break_next iterators have no status
 *   return with which to report that, so they would silently
 *   misclassify the NUL. After conversion, a 00 byte is a
 *   genuine U+0000 Control and mojibake, given an explicit
 *   byte length, is expected to iterate past it like any other
 *   codepoint; this is the behaviour this file relies on.
 *
 * Results:
 *   Returns 1 on success. nextOffset is set to the byte offset of
 *   the next word break boundary, or byteLen if none exists.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_word_next(const char *buf,
		   size_t byteLen,
		   size_t byteOffset,
		   size_t *nextOffset)
{
    mjb_word_break_state state;
    mjb_break_type bt;
    if (!nextOffset) return 0;
    if (!buf || byteOffset>=byteLen) { *nextOffset=byteLen; return buf!=NULL; }

    char *convertedBuf = NULL;
    size_t *offsetMap = NULL;
    int converted = 0;
    if (buf != NULL && byteLen > 0 && TclHasEncodedNul(buf, byteLen)) {
	if (!ConvertTclToUtf8(buf, byteLen, &convertedBuf, &byteLen, &offsetMap)) {
	    return 0;
	}
	buf = convertedBuf;
	converted = 1;
	byteOffset = TclOffsetToUtf8Offset(offsetMap, byteLen, byteOffset);
    }
    state.index=0;
    mjb_text text = mjb_text_utf8(buf, byteLen);
    while ((bt = mjb_word_break_next(&text, &state)) != MJB_BT_NOT_SET) {
	if (bt==MJB_BT_ALLOWED) {
	    size_t pos=state.position;
	    if (pos>byteOffset) { *nextOffset= converted ? Utf8OffsetToTclOffset(offsetMap, pos) : pos; if (converted) { free(convertedBuf); free(offsetMap); } return 1; }
	}
    }
    *nextOffset= converted ? Utf8OffsetToTclOffset(offsetMap, byteLen) : byteLen; if (converted) { free(convertedBuf); free(offsetMap); } return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_word_prev --
 *
 *   Find the previous word break boundary before a given byte offset.
 *
 *   The input buffer is in Tcl (modified UTF-8) encoding, in
 *   which U+0000 is stored as C0 80. Mojibake expects strict
 *   UTF-8 and rejects C0 80 as malformed, so when the buffer
 *   contains an encoded NUL it is converted to strict UTF-8
 *   before calling mojibake, and every boundary returned is
 *   translated back to Tcl byte offsets. Buffers without an
 *   encoded NUL take the fast path with no copy. The conversion
 *   is required, not optional: on inspection of the mojibake
 *   decoder, unconverted C0 80 decodes as two U+FFFDs (Other,
 *   not Control), and the mjb_*_break_next iterators have no status
 *   return with which to report that, so they would silently
 *   misclassify the NUL. After conversion, a 00 byte is a
 *   genuine U+0000 Control and mojibake, given an explicit
 *   byte length, is expected to iterate past it like any other
 *   codepoint; this is the behaviour this file relies on.
 *
 * Results:
 *   Returns 1 on success. prevOffset is set to the byte offset of
 *   the previous word break boundary, or 0 if none exists.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_word_prev(const char *buf,
		   size_t byteLen,
		   size_t byteOffset,
		   size_t *prevOffset)
{
    mjb_word_break_state state;
    mjb_break_type bt;
    size_t last=0;
    if (!prevOffset) return 0;
    if (!buf || byteOffset==0) { *prevOffset=0; return buf!=NULL; }

    char *convertedBuf = NULL;
    size_t *offsetMap = NULL;
    int converted = 0;
    if (buf != NULL && byteLen > 0 && TclHasEncodedNul(buf, byteLen)) {
	if (!ConvertTclToUtf8(buf, byteLen, &convertedBuf, &byteLen, &offsetMap)) {
	    return 0;
	}
	buf = convertedBuf;
	converted = 1;
	byteOffset = TclOffsetToUtf8Offset(offsetMap, byteLen, byteOffset);
    }
    state.index=0;
    mjb_text text = mjb_text_utf8(buf, byteLen);
    while ((bt = mjb_word_break_next(&text, &state)) != MJB_BT_NOT_SET) {
	if (bt==MJB_BT_ALLOWED) {
	    size_t pos=state.position;
	    if (pos>=byteOffset) break;
	    last=pos;
	}
    }
    *prevOffset= converted ? Utf8OffsetToTclOffset(offsetMap, last) : last; if (converted) { free(convertedBuf); free(offsetMap); } return 1;
}


/*
 * ---------------------------------------------------------------
 * mojibake_sentence_breaks --
 *
 *   Compute all sentence break boundaries in a UTF-8 buffer.
 *   Implements Unicode UAX #29 sentence breaking rules.
 *
 *   The input buffer is in Tcl (modified UTF-8) encoding, in
 *   which U+0000 is stored as C0 80. Mojibake expects strict
 *   UTF-8 and rejects C0 80 as malformed, so when the buffer
 *   contains an encoded NUL it is converted to strict UTF-8
 *   before calling mojibake, and every boundary returned is
 *   translated back to Tcl byte offsets. Buffers without an
 *   encoded NUL take the fast path with no copy. The conversion
 *   is required, not optional: on inspection of the mojibake
 *   decoder, unconverted C0 80 decodes as two U+FFFDs (Other,
 *   not Control), and the mjb_*_break_next iterators have no status
 *   return with which to report that, so they would silently
 *   misclassify the NUL. After conversion, a 00 byte is a
 *   genuine U+0000 Control and mojibake, given an explicit
 *   byte length, is expected to iterate past it like any other
 *   codepoint; this is the behaviour this file relies on.
 *
 * Results:
 *   Returns 1 on success. The breaks array is filled with 1 at each
 *   sentence break boundary byte offset (including 0 and byteLen).
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_sentence_breaks(const char *buf,
			 size_t byteLen,
			 unsigned char *breaks)
{
    mjb_sentence_break_state state;
    mjb_break_type bt;
    size_t tclLen = byteLen;
    char *convertedBuf = NULL;
    size_t *offsetMap = NULL;
    int converted = 0;
    if (buf != NULL && byteLen > 0 && TclHasEncodedNul(buf, byteLen)) {
	if (!ConvertTclToUtf8(buf, byteLen, &convertedBuf, &byteLen, &offsetMap)) {
	    return 0;
	}
	buf = convertedBuf;
	converted = 1;
    }
    if (!buf || !breaks) { if (converted) { free(convertedBuf); free(offsetMap); } return 0; }
    memset(breaks,0,byteLen+1);
    if (byteLen==0) { breaks[0]=1; return 1; }
    breaks[0]=1;
    state.index=0;
    mjb_text text = mjb_text_utf8(buf, byteLen);
    while ((bt = mjb_sentence_break_next(&text, &state)) != MJB_BT_NOT_SET) {
	if (bt==MJB_BT_ALLOWED) {
	    size_t pos=state.position;
	    if (pos<=byteLen) breaks[pos]=1;
	}
    }
    breaks[byteLen]=1;
    if (converted) {
	RemapBreaksToTcl(breaks, byteLen, tclLen, offsetMap);
	free(convertedBuf);
	free(offsetMap);
    }
    return 1;
}

/*
 * ---------------------------------------------------------------
 * ComplexScript --
 *
 *   Enumeration of complex scripts that require dictionary-based
 *   word segmentation (scripts without spaces between words).
 * ---------------------------------------------------------------
 */

typedef enum {
    SCRIPT_OTHER = 0,
    SCRIPT_THAI,
    SCRIPT_LAO,
    SCRIPT_KHMER,
    SCRIPT_MYANMAR
} ComplexScript;

/*
 * ---------------------------------------------------------------
 * ScriptFromCodepoint --
 *
 *   Determine the complex script category of a Unicode codepoint.
 *
 * Results:
 *   Returns SCRIPT_THAI, SCRIPT_LAO, SCRIPT_KHMER, SCRIPT_MYANMAR,
 *   or SCRIPT_OTHER if the codepoint is not in a dictionary-supported
 *   script.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

static ComplexScript
ScriptFromCodepoint(mjb_codepoint cp)
{
    if (cp >= 0x0E00 && cp <= 0x0E7F) return SCRIPT_THAI;
    if (cp >= 0x0E80 && cp <= 0x0EFF) return SCRIPT_LAO;
    if ((cp >= 0x1780 && cp <= 0x17FF) || (cp >= 0x19E0 && cp <= 0x19FF)) return SCRIPT_KHMER;
    if ((cp >= 0x1000 && cp <= 0x109F) || (cp >= 0xAA60 && cp <= 0xAA7F) || (cp >= 0xA9E0 && cp <= 0xA9FF)) return SCRIPT_MYANMAR;
    return SCRIPT_OTHER;
}

/*
 * ---------------------------------------------------------------
 * DictCache --
 *
 *   Cached sorted dictionary structure for fast binary search
 *   lookups during maximum-matching word segmentation.
 * ---------------------------------------------------------------
 */

typedef struct {
    const char* const* orig_words;
    size_t count;
    const char** sorted;	/* malloced array of pointers sorted */
    size_t* lens;		/* byte lengths */
    size_t max_len;		/* max byte length */
    int initialized;
} DictCache;

static DictCache cache_thai = { NULL,0,NULL,NULL,0,0 };
static DictCache cache_lao  = { NULL,0,NULL,NULL,0,0 };
static DictCache cache_khmer= { NULL,0,NULL,NULL,0,0 };
static DictCache cache_myanmar={NULL,0,NULL,NULL,0,0};

/*
 * ---------------------------------------------------------------
 * cmp_strptr --
 *
 *   Comparison function for qsort on string pointers.
 *
 * Results:
 *   Returns strcmp result of the two strings.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

static int cmp_strptr(const void *a, const void *b)
{
    const char * const *sa = (const char* const*)a;
    const char * const *sb = (const char* const*)b;
    return strcmp(*sa,*sb);
}

/*
 * ---------------------------------------------------------------
 * InitDictCache --
 *
 *   Initialize a dictionary cache by sorting the word list and
 *   computing lengths. Does nothing if already initialized.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Allocates memory for sorted pointers and length arrays.
 * ---------------------------------------------------------------
 */

static void
InitDictCache(DictCache *c, const char* const* words, size_t count)
{
    if (c->initialized) return;
    c->orig_words = words;
    c->count = count;
    c->sorted = (const char**)malloc(count * sizeof(char*));
    c->lens = (size_t*)malloc(count * sizeof(size_t));
    if (!c->sorted || !c->lens) return;
    size_t max=0;
    for (size_t i=0;i<count;i++) {
	c->sorted[i]=words[i];
	size_t l = strlen(words[i]);
	c->lens[i]=l; /* Temporary, will recompute after sort. */
	if (l>max) max=l;
    }
    qsort(c->sorted, count, sizeof(char*), cmp_strptr);
    /* Recompute lens after sort and max. */
    max=0;
    for (size_t i=0;i<count;i++) {
	size_t l = strlen(c->sorted[i]);
	c->lens[i]=l;
	if (l>max) max=l;
    }
    c->max_len = max;
    c->initialized=1;
}

/*
 * ---------------------------------------------------------------
 * EnsureDictsInitialized --
 *
 *   Initialize all dictionary caches. Called once at first use.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Initializes the Thai, Lao, Khmer, and Myanmar dictionary caches.
 * ---------------------------------------------------------------
 */

static void
EnsureDictsInitialized(void)
{
    static int once=0;
    if (once) return;
    once=1;
    InitDictCache(&cache_thai, thai_words, thai_word_count);
    InitDictCache(&cache_lao, lao_words, lao_word_count);
    InitDictCache(&cache_khmer, khmer_words, khmer_word_count);
}

/*
 * ---------------------------------------------------------------
 * CacheForScript --
 *
 *   Get the dictionary cache for a given complex script type.
 *
 * Results:
 *   Returns a pointer to the DictCache for the script, or NULL
 *   if the script has no dictionary.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

static DictCache*
CacheForScript(ComplexScript s)
{
    switch(s){
    case SCRIPT_THAI: return &cache_thai;
    case SCRIPT_LAO: return &cache_lao;
    case SCRIPT_KHMER: return &cache_khmer;
    case SCRIPT_MYANMAR: return &cache_myanmar;
    default: return NULL;
    }
}

/*
 * ---------------------------------------------------------------
 * DictContains --
 *
 *   Binary search for an exact match of a query string in a
 *   sorted dictionary.
 *
 * Results:
 *   Returns 1 if the query is found in the dictionary, 0 otherwise.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

static int
DictContains(DictCache *c, const char *query, size_t qlen)
{
    if (!c || !c->initialized || qlen==0 || qlen>c->max_len) return 0;
    size_t lo=0, hi=c->count;
    while (lo < hi) {
	size_t mid = lo + (hi-lo)/2;
	size_t mlen = c->lens[mid];
	size_t cmpLen = mlen < qlen ? mlen : qlen;
	int cmp = strncmp(c->sorted[mid], query, cmpLen);
	if (cmp==0) {
	    if (mlen < qlen) cmp = -1;
	    else if (mlen > qlen) cmp = 1;
	    else return 1;
	}
	if (cmp < 0) lo = mid+1;
	else hi = mid;
    }
    return 0;
}

/*
 * ---------------------------------------------------------------
 * DecodeUtf8At / CannotStartLine --
 *
 *   Decode the codepoint starting at byte offset pos, and test
 *   whether a line may start with it. Repetition signs (Thai
 *   U+0E46, Lao U+0EC6, Khmer U+17D4, Thai U+0E2F) repeat the
 *   preceding word and can never start a line; neither can a
 *   ZWJ or a combining / spacing mark that belongs to the
 *   previous cluster. These are their own grapheme clusters,
 *   so the grapheme-subset filter does NOT clear a break before
 *   them -- this check is what stops WordBreakIndex from
 *   choosing a break there (textGrapheme-5.2, 5.3).
 * ---------------------------------------------------------------
 */

static mjb_codepoint
DecodeUtf8At(const char *buf,
	     size_t byteLen,
	     size_t pos)
{
    unsigned char b;
    if (pos >= byteLen) return 0;
    b = (unsigned char) buf[pos];
    if (b < 0x80) return b;
    if ((b >> 5) == 0x6 && pos + 1 < byteLen) {
	return ((b & 0x1F) << 6) | (buf[pos + 1] & 0x3F);
    }
    if ((b >> 4) == 0xE && pos + 2 < byteLen) {
	return ((b & 0x0F) << 12) | ((buf[pos + 1] & 0x3F) << 6)
	    | (buf[pos + 2] & 0x3F);
    }
    if ((b >> 3) == 0x1E && pos + 3 < byteLen) {
	return ((b & 0x07) << 18) | ((buf[pos + 1] & 0x3F) << 12)
	    | ((buf[pos + 2] & 0x3F) << 6) | (buf[pos + 3] & 0x3F);
    }
    return b;
}

static int
CannotStartLine(mjb_codepoint cp)
{
    if (cp == 0x200D) return 1; /* ZWJ */
    if (cp == 0x0E46) return 1; /* THAI MAIYAMOK (repetition) */
    if (cp == 0x0E2F) return 1; /* THAI PAIYANNOI */
    if (cp == 0x0EC6) return 1; /* LAO KO LA (repetition) */
    if (cp == 0x17D4) return 1; /* KHMER SIGN KHAN (repetition) */
    if (cp == 0x17D5) return 1; /* KHMER SIGN BARI */
    /* Combining / spacing marks of Thai, Lao, Khmer, Myanmar. */
    if (cp >= 0x0E31 && cp <= 0x0E3A) return 1;
    if (cp >= 0x0E47 && cp <= 0x0E4E) return 1;
    if (cp >= 0x0EB1 && cp <= 0x0EB9) return 1;
    if (cp >= 0x0EBB && cp <= 0x0EBC) return 1;
    if (cp >= 0x0EC8 && cp <= 0x0ECD) return 1;
    if (cp >= 0x17C6 && cp <= 0x17D3) return 1;
    if (cp == 0x17DD) return 1;
    if (cp >= 0x102D && cp <= 0x1030) return 1;
    if (cp >= 0x1032 && cp <= 0x1037) return 1;
    if (cp >= 0x1039 && cp <= 0x103A) return 1;
    if (cp >= 0x103D && cp <= 0x103E) return 1;
    return 0;
}

/*
 * ---------------------------------------------------------------
 * ForwardMaxMatchRun --
 *
 *   Forward maximum matching word segmentation inside a single
 *   script run. Finds the longest dictionary word at each position,
 *   falling back to single grapheme clusters for unknown sequences.
 *
 * Results:
 *   Returns the number of words found. Updates wBreaks and optionally
 *   lBreaks with word boundaries.
 *
 * Side effects:
 *   Modifies the break arrays.
 * ---------------------------------------------------------------
 */

static size_t
ForwardMaxMatchRun(const char *buf, size_t runStart, size_t runEnd,
		   const unsigned char *gBreaks,
		   unsigned char *wBreaks,
		   unsigned char *lBreaks,
		   DictCache *dc)
{
    size_t pos = runStart;
    size_t words = 0;

    while (pos < runEnd) {

	/* Find next grapheme boundary after pos. */
	size_t nextG = pos + 1;
	while (nextG < runEnd && !gBreaks[nextG]) nextG++;
	if (nextG > runEnd) nextG = runEnd;

	/* Ensure forward progress. */
	if (nextG == pos) {
	    nextG = pos + 1;
	    while (nextG < runEnd && (buf[nextG] & 0xC0) == 0x80) nextG++;
	}

	size_t bestEnd = 0;

	/* Try longest dictionary match first. */
	size_t maxTry = dc->max_len;
	size_t remaining = runEnd - pos;
	if (maxTry > remaining) maxTry = remaining;

	size_t tryEnd = pos + maxTry;

	/* Snap tryEnd to a grapheme boundary. */
	while (tryEnd > pos && tryEnd < runEnd && !gBreaks[tryEnd]) tryEnd--;

	for (; tryEnd > pos; ) {
	    size_t tryLen = tryEnd - pos;
	    if (tryLen > 0 && DictContains(dc, buf + pos, tryLen)) {
		bestEnd = tryEnd;
		break;
	    }

	    /* Move tryEnd to previous grapheme boundary. */
	    size_t prev = tryEnd - 1;
	    while (prev > pos && !gBreaks[prev]) prev--;
	    if (prev == pos) break;
	    tryEnd = prev;
	}

	if (bestEnd) {
	    /* Dictionary hit → real word boundary. */
	    wBreaks[bestEnd] = 1;
	    if (lBreaks) lBreaks[bestEnd] = 1;
	    pos = bestEnd;
	} else {
	    /*
	     * Fallback: NO word break here. An unknown Thai/Lao/Khmer/
	     * Myanmar run is ONE word for word-selection purposes.
	     * The old code marked a word break at every grapheme
	     * cluster, so double-click on an unknown Thai word
	     * selected a single cluster -- a fragment, not a word
	     * (textGraphemeNul-4.2). The fallback exists only for
	     * LINE breaking, to keep an unknown run from becoming
	     * one unbreakable line: mark a line break at the next
	     * grapheme boundary, and only if the caller wants line
	     * breaks (lBreaks != NULL). Callers that only want word
	     * breaks pass lBreaks == NULL and get no fallback break
	     * at all.
	     */
	    if (lBreaks
		    && !CannotStartLine(DecodeUtf8At(buf, runEnd, nextG))) {
		lBreaks[nextG] = 1;
	    }
	    pos = nextG;
	}

	words++;
    }

    return words;
}

/*
 * ---------------------------------------------------------------
 * mojibake_word_breaks_with_dict --
 *
 *   Compute word breaks with dictionary enhancement for complex
 *   scripts (Thai, Lao, Khmer, Myanmar). Uses forward maximum
 *   matching to insert word boundaries where UAX #29 would not.
 *
 * Results:
 *   Returns 1 on success. wBreaks is filled with break boundaries
 *   including dictionary-derived word boundaries.
 *
 * Side effects:
 *   May allocate temporary grapheme break array if gBreaks_tmp is NULL.
 * ---------------------------------------------------------------
 */

/*
 * WordBreaksWithDictInternal -- the body of
 * mojibake_word_breaks_with_dict, plus an lBreaks out-parameter
 * for the line-breaking caller. When lBreaks is NULL (the word
 * path), ForwardMaxMatchRun's fallback marks nothing; when it is
 * non-NULL (the line path), the fallback marks line breaks.
 * Public callers use mojibake_word_breaks_with_dict (lBreaks
 * NULL); mojibake_line_breaks_with_dict passes its lBreaks so
 * unknown runs still get per-cluster line breaks.
 */
static int
WordBreaksWithDictInternal(const char *buf,
			       size_t byteLen,
			       unsigned char *wBreaks,
			       unsigned char *gBreaks_tmp,
			       unsigned char *lBreaks)
{
    if (!buf || !wBreaks) return 0;
    EnsureDictsInitialized();

    unsigned char *gBreaks = gBreaks_tmp;
    unsigned char *localG = NULL;
    if (!gBreaks) {
	localG = (unsigned char*)malloc(byteLen+1);
	if (!localG) return 0;
	gBreaks = localG;
    }
    mojibake_grapheme_breaks(buf, byteLen, gBreaks);
    MojibakeWordBreaksBase(buf, byteLen, wBreaks);

    /* Now walk the buffer codepoint by codepoint to find runs of complex script. */
    size_t i=0;
    while (i < byteLen) {
	/* Skip if not at grapheme start. */
	if (!gBreaks[i]) { i++; continue; }
	/* decode codepoint at i */
	mjb_codepoint cp=0;
	int clen=0;
	unsigned char b = (unsigned char)buf[i];
	if (b < 0x80) { cp=b; clen=1; }
	else if ((b>>5)==0x6) { if (i+1<byteLen){ cp=((b&0x1F)<<6)|(buf[i+1]&0x3F); clen=2; } else clen=1; }
	else if ((b>>4)==0xE) { if (i+2<byteLen){ cp=((b&0x0F)<<12)|((buf[i+1]&0x3F)<<6)|(buf[i+2]&0x3F); clen=3; } else clen=1; }
	else if ((b>>3)==0x1E) { if (i+3<byteLen){ cp=((b&0x07)<<18)|((buf[i+1]&0x3F)<<12)|((buf[i+2]&0x3F)<<6)|(buf[i+3]&0x3F); clen=4; } else clen=1; }
	else { cp=b; clen=1; }

	ComplexScript sc = ScriptFromCodepoint(cp);
	if (sc==SCRIPT_OTHER) { i+=clen; continue; }

	/*
	 * Start of run: continue while same script and not space
	 * and not word break from base? Actually we merge until script
	 *changes or space/newline.
	 */
	size_t runStart=i;
	size_t runEnd=runStart;
	ComplexScript curSc=sc;
	size_t j=i;
	while (j < byteLen) {
	    if (!gBreaks[j]) { j++; continue; }
	    if (j>=byteLen) break;
	    /* Decode at j. */
	    unsigned char bj = (unsigned char)buf[j];
	    mjb_codepoint cpj=0; int lj=1;
	    if (bj < 0x80) { cpj=bj; lj=1; }
	    else if ((bj>>5)==0x6) { if (j+1<byteLen){ cpj=((bj&0x1F)<<6)|(buf[j+1]&0x3F); lj=2; } }
	    else if ((bj>>4)==0xE) { if (j+2<byteLen){ cpj=((bj&0x0F)<<12)|((buf[j+1]&0x3F)<<6)|(buf[j+2]&0x3F); lj=3; } }
	    else if ((bj>>3)==0x1E) { if (j+3<byteLen){ cpj=((bj&0x07)<<18)|((buf[j+1]&0x3F)<<12)|((buf[j+2]&0x3F)<<6)|(buf[j+3]&0x3F); lj=4; } }
	    else { cpj=bj; lj=1; }

	    ComplexScript scj = ScriptFromCodepoint(cpj);
	    if (scj!=curSc) break;
	    /* Break run on ASCII space or line break already? Keep spaces as separators, so stop before space */
	    if (cpj==0x20 || cpj==0x09 || cpj==0x0A || cpj==0x0D) break;
	    runEnd = j+lj;
	    j += lj;
	}
	if (runEnd>runStart+1) {
	    /*
	     * The baseline mojibake_word_breaks() call above has no
	     * knowledge of Thai/Lao/Khmer/Myanmar word boundaries, so
	     * per UAX #29's documented fallback for scripts without
	     * spaces, it marks a word break after every single grapheme
	     * cluster in this run. Left in place, those per-character
	     * breaks would sit alongside the real boundaries found by
	     * the dictionary matcher below, and every downstream
	     * consumer (line-break promotion, the line-wrap search)
	     * would see "break allowed" almost everywhere -- effectively
	     * degrading word-wrap to character-wrap for this run. Clear
	     * them so only genuine dictionary-derived boundaries (plus
	     * ForwardMaxMatchRun's own fallback breaks for real
	     * dictionary misses) remain.
	     */
	    memset(wBreaks + runStart + 1, 0, runEnd - runStart - 1);
	    DictCache *dc = CacheForScript(curSc);
	    if (dc) {
		ForwardMaxMatchRun(buf, runStart, runEnd, gBreaks, wBreaks, lBreaks, dc);
	    }
	}
	i = runEnd ? runEnd : i+clen;
    }

    /*
     * Grapheme-subset filter: a word break must never split a
     * grapheme cluster. UAX #29 guarantees this (word boundaries
     * fall on grapheme boundaries), so a base word break that
     * lands inside a cluster -- e.g. between "e" and its combining
     * acute, which double-click must select as one cluster
     * (textGraphemeNul-2.2) -- is a segmentation error, not a
     * real break. Dictionary hits are already snapped to
     * grapheme boundaries, so they survive. Breaks at 0 and
     * byteLen are always kept.
     */
    for (i = 1; i < byteLen; i++) {
	if (wBreaks[i] && !gBreaks[i]) {
	    wBreaks[i] = 0;
	}
    }

    /*
     * No-line-start / no-word-start filter: a word break must
     * never sit before a character that may not start a line
     * or a word. The grapheme-subset filter above does NOT
     * catch these: Lao U+0EC6 (KO LA, repetition), Khmer
     * U+17D4 (SIGN KHAN, repetition) and the other marks in
     * CannotStartLine are each their own grapheme cluster, so
     * a break before them survives the grapheme filter. The
     * line-break wrapper filters them again for line breaks,
     * but word wrap also consumes this word-break array, so a
     * break before U+0EC6 / U+17D4 here is what put a wrapped
     * line starting with U+0EC6 at widths 12 and 20. Clearing the 
     * break merges the repetition sign / mark into the preceding word, 
     * which is what it belongs to: it repeats or modifies that word, it
     * does not start a new one. Breaks at 0 and byteLen are
     * always kept. The same filter is applied to lBreaks when
     * the caller supplied it, so the dictionary fallback's
     * per-grapheme line breaks get the same treatment here as
     * in the line-break wrapper.
     */
    for (i = 1; i < byteLen; i++) {
	if (wBreaks[i] && CannotStartLine(DecodeUtf8At(buf, byteLen, i))) {
	    wBreaks[i] = 0;
	}
	if (lBreaks != NULL && lBreaks[i]
		&& CannotStartLine(DecodeUtf8At(buf, byteLen, i))) {
	    lBreaks[i] = 0;
	}
    }

    if (localG) free(localG);
    return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_word_breaks_with_dict --
 *
 *   Public wrapper: WordBreaksWithDictInternal with lBreaks NULL,
 *   i.e. word breaks only, no line-break fallback. See the
 *   internal function for the full contract.
 * ---------------------------------------------------------------
 */

int
mojibake_word_breaks_with_dict(const char *buf,
			       size_t byteLen,
			       unsigned char *wBreaks,
			       unsigned char *gBreaks_tmp)
{
    return WordBreaksWithDictInternal(buf, byteLen, wBreaks,
					      gBreaks_tmp, NULL);
}

/*
 * ---------------------------------------------------------------
 * mojibake_line_breaks_with_dict --
 *
 *   Compute line breaks with dictionary enhancement. UAX #14
 *   line breaks are computed first, then dictionary word boundaries
 *   from complex scripts are promoted to line break opportunities.
 *
 * Results:
 *   Returns 1 on success. lBreaks is filled with break boundaries
 *   including dictionary-derived line break opportunities.
 *
 * Side effects:
 *   May allocate temporary grapheme and word break arrays if
 *   gBreaks_tmp or wBreaks_tmp is NULL.
 * ---------------------------------------------------------------
 */

int
mojibake_line_breaks_with_dict(const char *buf,
			       size_t byteLen,
			       unsigned char *lBreaks,
			       unsigned char *gBreaks_tmp,
			       unsigned char *wBreaks_tmp)
{
    if (!buf || !lBreaks) return 0;
    EnsureDictsInitialized();

    unsigned char *gBreaks = gBreaks_tmp;
    unsigned char *localG = NULL;
    if (!gBreaks) {
	localG = (unsigned char*)malloc(byteLen+1);
	if (!localG) return 0;
	gBreaks = localG;
	mojibake_grapheme_breaks(buf, byteLen, gBreaks);
    }

    unsigned char *wBreaks = wBreaks_tmp;
    unsigned char *localW = NULL;
    if (!wBreaks) {
	localW = (unsigned char*)malloc(byteLen+1);
	if (!localW) { if(localG) free(localG); return 0; }
	wBreaks = localW;
    }

    /* Base line breaks. */
    MojibakeLineBreaksBase(buf, byteLen, lBreaks);
    /* Enhanced word breaks. Pass lBreaks so the dictionary
     * fallback marks line breaks for unknown runs (it must NOT
     * mark word breaks there -- see ForwardMaxMatchRun). */
    WordBreaksWithDictInternal(buf, byteLen, wBreaks, gBreaks, lBreaks);

    /* For complex scripts, every dictionary word boundary is an allowed line break. */
    for (size_t i=0;i<=byteLen;i++) {
	if (wBreaks[i]) {
	    /*
	     * Only promote if inside complex script run - check surrounding cp.
	     *  Simple: if i>0, look at cp before i.
	     */
	    if (i>0 && i<byteLen) {
		/* Find start of cluster ending at i: previous grapheme start. */
		size_t prev = i-1;
		while (prev>0 && !gBreaks[prev]) prev--;
		/* Decode cp at prev. */
		unsigned char b = (unsigned char)buf[prev];
		mjb_codepoint cp=0;
		if (b < 0x80) cp=b;
		else if ((b>>5)==0x6 && prev+1<byteLen) cp=((b&0x1F)<<6)|(buf[prev+1]&0x3F);
		else if ((b>>4)==0xE && prev+2<byteLen) cp=((b&0x0F)<<12)|((buf[prev+1]&0x3F)<<6)|(buf[prev+2]&0x3F);
		else if ((b>>3)==0x1E && prev+3<byteLen) cp=((b&0x07)<<18)|((buf[prev+1]&0x3F)<<12)|((buf[prev+2]&0x3F)<<6)|(buf[prev+3]&0x3F);
		if (ScriptFromCodepoint(cp)!=SCRIPT_OTHER) {
		    lBreaks[i]=1;
		}
	    }
	}
    }

    /*
     * Grapheme-subset filter: a line break must never split a
     * grapheme cluster. The base UAX #14 pass does not know
     * about grapheme clusters: it allows a break before a Lao
     * Lo vowel sign (U+0EC6, class SA, treated as AL by the base
     * rules) and can leave a break before a Khmer combining
     * mark (U+17D4). Those breaks put a vowel sign / mark at
     * the start of a wrapped line -- an orphan line start.
     * UAX #14 LB9 already forbids
     * breaking a combining sequence; clearing every line break
     * that is not also a grapheme break enforces it uniformly.
     * Dictionary boundaries are snapped to grapheme boundaries,
     * so they survive. Breaks at 0 and byteLen are kept.
     */
    for (size_t i = 1; i < byteLen; i++) {
	if (lBreaks[i] && !gBreaks[i]) {
	    lBreaks[i] = 0;
	}
    }

    /*
     * No-line-start filter: a line break is not allowed before a
     * repetition sign, ZWJ, or combining mark, even though it is
     * a grapheme break. This clears both the base UAX #14 breaks
     * and the dictionary-fallback breaks (ForwardMaxMatchRun
     * marks a line break at every grapheme of an unknown run)
     * at those positions, so the next line never starts with
     * e.g. Lao U+0EC6 or Khmer U+17D4.
     * The wrap search then falls through to the next allowed
     * break instead.
     */
    for (size_t i = 1; i < byteLen; i++) {
	if (lBreaks[i] && CannotStartLine(DecodeUtf8At(buf, byteLen, i))) {
	    lBreaks[i] = 0;
	}
    }

    if (localG) free(localG);
    if (localW) free(localW);
    return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_word_breaks --
 *
 *   Plain entry point used by the text widget (double-click word
 *   selection). It MUST NOT be bare mojibake: bare UAX #29 marks
 *   a word break after every cluster in Thai/Lao/Khmer runs
 *   (double-click then selects one cluster, not a word --
 *   and it can return word breaks inside a grapheme cluster. 
 *   Delegates to the _with_dict implementation, which applies
 *   the dictionary segmenter for complex scripts and the
 *   grapheme-subset filter (a word break is kept only if it is
 *   also a grapheme break) for everything else.
 *
 * Results:
 *   Returns 1 on success.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_word_breaks(const char *buf,
		     size_t byteLen,
		     unsigned char *breaks)
{
    return WordBreaksWithDictInternal(buf, byteLen, breaks, NULL, NULL);
}

/*
 * ---------------------------------------------------------------
 * mojibake_line_breaks --
 *
 *   Plain entry point used by the text widget (word wrap). It
 *   MUST NOT be bare mojibake: bare UAX #14 can return line
 *   breaks inside a grapheme cluster (Lao/Khmer vowel signs /
 *   combining marks strand at a line start.
 *   Delegates to the _with_dict implementation, which
 *   applies the dictionary segmenter for complex scripts and
 *   the grapheme-subset filter (a line break is kept only if it
 *   is also a grapheme break) for everything else.
 *
 * Results:
 *   Returns 1 on success.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_line_breaks(const char *buf,
		     size_t byteLen,
		     unsigned char *breaks)
{
    return mojibake_line_breaks_with_dict(buf, byteLen, breaks, NULL, NULL);
}

/*
 * ---------------------------------------------------------------
 * mojibake_cannot_start_line_at --
 *
 *   Test whether the character starting at byte offset pos in
 *   buf may start a line. Used by tkTextDisp.c's WordBreakIndex,
 *   which has a break-after-whitespace path that bypasses the
 *   break arrays: a break after a space is only a valid wrap
 *   point if the next character may start a line. Without this
 *   check a space before Lao U+0EC6 or Khmer U+17D4 is taken
 *   as a wrap point and the next line starts with the
 *   repetition sign.
 *
 * Results:
 *   Returns 1 if the character at pos may not start a line,
 *   0 otherwise (including pos >= byteLen, where there is no
 *   next character to orphan).
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_cannot_start_line_at(const char *buf,
			      size_t byteLen,
			      size_t pos)
{
    if (buf == NULL || pos >= byteLen) {
	return 0;
    }
    return CannotStartLine(DecodeUtf8At(buf, byteLen, pos));
}

/*
 * ---------------------------------------------------------------
 * mojibake_snap_wrap_break --
 *
 *   Snap a proposed hard (character-wrap) break so the next line
 *   never starts inside a grapheme cluster or with a character
 *   that may not start a line. The break arrays already clear
 *   those positions, but the text widget's wrap code has a
 *   second path: when no allowed break fits inside the line
 *   width, it hard-breaks at the width boundary without
 *   consulting the array. Call this from that path: pass the
 *   proposed byte offset and use *adjusted instead. The break
 *   moves forward to the next grapheme boundary whose first
 *   codepoint may start a line, pulling the whole cluster onto
 *   the current line instead of orphaning it. It never moves
 *   backward, so it never re-opens a line that is already laid
 *   out.
 *
 * Results:
 *   Returns 1 on success, with *adjusted set to the snapped
 *   byte offset (byteLen if no such boundary exists).
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_snap_wrap_break(const char *buf,
			 size_t byteLen,
			 size_t proposed,
			 size_t *adjusted)
{
    unsigned char *gBreaks;
    size_t pos;

    if (!adjusted) return 0;
    if (!buf || proposed >= byteLen) {
	*adjusted = byteLen;
	return 1;
    }
    if (proposed == 0) {
	*adjusted = 0;
	return 1;
    }

    gBreaks = (unsigned char *) malloc(byteLen + 1);
    if (gBreaks == NULL) {
	*adjusted = proposed;
	return 0;
    }
    mojibake_grapheme_breaks(buf, byteLen, gBreaks);

    pos = proposed;
    while (pos < byteLen
	   && (!gBreaks[pos]
	       || CannotStartLine(DecodeUtf8At(buf, byteLen, pos)))) {
	pos++;
    }
    *adjusted = pos;
    free(gBreaks);
    return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_grapheme_breaks_wrapper --
 *
 *   Convenience wrapper for mojibake_grapheme_breaks.
 *
 * Results:
 *   Returns 1 on success.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_grapheme_breaks_wrapper(const char *buf,
				 size_t len,
				 unsigned char *br) {
    return mojibake_grapheme_breaks(buf,len,br);
}

/*
 * ---------------------------------------------------------------
 * mojibake_dict_init --
 *
 *   Expose dictionary initialization for Tk initialization.
 *   Ensures all dictionary caches are initialized.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Initializes all dictionary caches.
 * ---------------------------------------------------------------
 */

void
mojibake_dict_init(void) {
    EnsureDictsInitialized();
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
