/*
 * This file contains macro based templates for functions that use mojibake to
 * manage grapheme cluster, word, line and sentence breaks.  To use the file
 * define the following macros and then include the file.  The macros should be
 * undef'ed after the include statement.
 *
 *  BREAK_TYPE
 *  BREAK_FUNCTION
 *  BREAK_NEXT_FUNCTION
 *  BREAK_PREV_FUNCTION
 *
 * The purpose of the templates is to improve readability and maintainability
 * of these utility functions which are almost identical, differing only in the
 * break type.
 */

#ifndef MJB_PREPEND_
#define MJB_PREPEND_(a, b) a##b // needed to allow expansion of BREAK_TYPE
#define MJB_PREPEND(a, b)  MJB_PREPEND_(a, b)
#define MJB(suffix) MJB_PREPEND(BREAK_TYPE, suffix)
#endif

/*
 * ---------------------------------------------------------------
 * BREAK_FUNCTION
 *
 *   Computes all BREAK_TYPE break boundaries in a TUTF-8 buffer.  Implements
 *   Unicode UAX #29 extended boundaries.
 *
 *   Uses mojibake to iterate through the buffer, recording each break by
 *   setting a byte of the breaks buffer to 1 unless the break corresponds to
 *   the \x80 byte of an encoded null byte, in which case it is ignored.
 *
 * Results:
 *   Returns 1 on success. The breaks array is filled with 1 at each break
 *   boundary byte offset (including 0 and byteLen).
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
BREAK_FUNCTION(
    const char *buf,
    size_t byteLen,
    unsigned char *breaks)
{
    const unsigned char *u = (const unsigned char*) buf;
    MJB(_state) state = {0};
    mjb_break_type bt;
    if (buf == NULL || breaks == NULL) {
	 return 0;
    }
    memset(breaks, 0, byteLen + 1);
    if (byteLen == 0) {
	breaks[0]=1;
	return 1;
    }
    breaks[0]=1;
    mjb_text text = mjb_text_utf8(buf, byteLen);
    while ((bt = MJB(_next)(&text, &state)) != MJB_BT_NOT_SET) {
	if (bt == MJB_BT_NO_BREAK) {
	    continue;
	}
	size_t pos = state.position;
	if (pos > 0 && pos < byteLen && u[pos - 1] == 0xC0 && u[pos] == 0x80) {
	    continue;
	}
	if (bt == MJB_BT_ALLOWED) {
	    if (pos <= byteLen) {
		breaks[pos] = 1;
	    }
	}
    }
    breaks[byteLen]=1;
    return 1;
}

/*
 * ---------------------------------------------------------------
 * BREAK_NEXT_FUNCTION --
 *
 *   Find the offset of the first BREAK_TYPE boundary in a TUTF-8 buffer which
 *   is larger than a given byte offset.
 *
 *   Uses mojibake to iterate through the buffer, starting at a preceding
 *   newline, and ignoring any break at the \x80 byte of an encoded nul
 *   codepoint.  Writes the offset of the first allowed break after the
 *   provided offset at the nextOffset address.
 *
 * Results:
 *   Returns 1 on success. nextOffset is set to the byte offset of
 *   the next BREAK_TYPE boundary, or byteLen if none exists.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
BREAK_NEXT_FUNCTION(
    const char *buf,
    size_t byteLen,
    size_t byteOffset,
    size_t *nextOffset)
{
    const unsigned char *u = (const unsigned char*) buf;
    MJB(_state) state = {0};
    mjb_break_type bt;
    if (nextOffset==NULL){
	return 0;
    }
    if (buf == NULL || byteOffset >= byteLen) {
	*nextOffset = byteLen;
	return (buf != NULL);
    }
    /* Rather than search from the beginning of the buffer, start
     * at the byte following the last newline before the offset,
     * if there is one.
     */
    size_t start;
    if (byteOffset > 0) {
	start = byteOffset - 1;
	while (start > 0 && buf[start] != '\n') {
	    start--;
	}
	if (buf[start] == '\n') {
	    start++;
	}
	byteLen -= start;
	byteOffset -= start;
	buf += start;
	u += start;
    } else {
	start = 0;
    }
    mjb_text text = mjb_text_utf8(buf, byteLen);
    while ((bt = MJB(_next)(&text, &state)) != MJB_BT_NOT_SET) {
	if (bt == MJB_BT_NO_BREAK) {
	    continue;
	}
	size_t pos = state.position;
	if (pos <= byteOffset) {
	    continue;
	}
	if (pos > 0 && pos < byteLen && u[pos - 1] == 0xC0 && u[pos] == 0x80) {
	    continue;
	}
	*nextOffset = (bt == MJB_BT_ALLOWED || bt == MJB_BT_MANDATORY) ?
	     pos : byteLen;
	*nextOffset += start;
	break;
    }
    return 1;
}

/*
 * ---------------------------------------------------------------
 *  BREAK_PREV_FUNCTION --
 *
 *   Find the offset of the last BREAK_TYPE boundary before a given byte offset
 *   in a TUTF-8 buffer.
 *
 *   Uses mojibake to iterate through the buffer starting at a preceding
 *   newline, while remembering each break as it is searching for the next
 *   one.  As soon as the position passes the byteOffset return the last saved
 *   value.  However, we must completely ignore any break at the \x80 byte of
 *   an encoded nul codepoint.
 *
 * Results:
 *   Returns 1 on success. prevOffset is set to the byte offset of the previous
 *   grapheme boundary, or 0 if none exists.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
BREAK_PREV_FUNCTION(
    const char *buf,
    size_t byteLen,
    size_t byteOffset,
    size_t *prevOffset)
{
    const unsigned char *u = (unsigned char*) buf;
    MJB(_state) state = {0};
    mjb_break_type bt;
    size_t last = 0;
    if (prevOffset==NULL) {
	return 0;
    }
    if (buf==NULL || byteOffset==0) {
	*prevOffset=0;
	return buf!=NULL;
    }
    /* Rather than search from the beginning of the buffer, start
     * at the byte following the last newline before the offset,
     * if there is one.
     */
    size_t start = 0;
    if (byteOffset > 0) {
	/*
	 * Handle the case where the offset is preceded by \n or \r\n.
	 */
        if (buf[byteOffset - 1] == '\n') {
            if (byteOffset >= 2 && buf[byteOffset - 2] == '\r') {
                *prevOffset = byteOffset - 2;
            } else {
                *prevOffset = byteOffset - 1;
            }
            return 1;
        }
        start = byteOffset - 1;
        while (start > 0 && buf[start] != '\n') {
            start--;
        }
        if (buf[start] == '\n') {
            start++;
        }
        byteLen -= start;
        byteOffset -= start;
        buf += start;
        u += start;
    }
    mjb_text text = mjb_text_utf8(buf, byteLen);
    while ((bt = MJB(_next)(&text, &state)) != MJB_BT_NOT_SET) {
	if (bt == MJB_BT_NO_BREAK) {
	    continue;
	}
	size_t pos = state.position;
	if (pos > 0 && pos < byteLen && u[pos - 1] == 0xC0 && u[pos] == 0x80) {
	    continue;
	}
	if (bt == MJB_BT_ALLOWED || bt == MJB_BT_MANDATORY) {
	    if (pos < byteOffset) {
		last = pos;
	    } else {
		break;
	    }
	}
    }
    *prevOffset = last + start;
    return 1;
}

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * eval: (c-set-offset 'arglist-intro '+)
 * eval: (c-set-offset 'arglist-cont-nonempty '+)
 * fill-column: 79
 * coding: utf-8
 * End:
 */
