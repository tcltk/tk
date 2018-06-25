/*
 * rbcParse.c --
 *
 *      TODO: Description
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

#ifndef TCL_RESULT_SIZE
#define TCL_RESULT_SIZE		200
#endif

#define TCL_BRACKET_TERM	  1

/*
 * A table used to classify input characters to assist in parsing
 * Tcl commands.  The table should be indexed with a signed character
 * using the CHAR_TYPE macro.  The character may have a negative
 * value.  The CHAR_TYPE macro takes a pointer to a signed character
 * and a pointer to the last character in the source string.  If the
 * src pointer is pointing at the terminating null of the string,
 * CHAR_TYPE returns TCL_COMMAND_END.
 */

#define TCL_NORMAL		0x01
#define TCL_SPACE		0x02
#define TCL_COMMAND_END		0x04
#define TCL_QUOTE		0x08
#define TCL_OPEN_BRACKET	0x10
#define TCL_OPEN_BRACE		0x20
#define TCL_CLOSE_BRACE		0x40
#define TCL_BACKSLASH		0x80
#define TCL_DOLLAR		0x00

static unsigned char tclTypeTable[] = {
    /*
     * Negative character values, from -128 to -1:
     */

    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,

    /*
     * Positive character values, from 0-127:
     */

    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_SPACE, TCL_COMMAND_END, TCL_SPACE,
    TCL_SPACE, TCL_SPACE, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_SPACE, TCL_NORMAL, TCL_QUOTE, TCL_NORMAL,
    TCL_DOLLAR, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_COMMAND_END,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_OPEN_BRACKET,
    TCL_BACKSLASH, TCL_COMMAND_END, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_OPEN_BRACE,
    TCL_NORMAL, TCL_CLOSE_BRACE, TCL_NORMAL, TCL_NORMAL,

    /*
     * Large unsigned character values, from 128-255:
     */

    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
    TCL_NORMAL, TCL_NORMAL, TCL_NORMAL, TCL_NORMAL,
};

#define CHAR_TYPE(src,last) \
	(((src)==(last))?TCL_COMMAND_END:(tclTypeTable+128)[(int)*(src)])

/*
 *--------------------------------------------------------------
 *
 * RbcExpandParseValue --
 *
 *      This procedure is commonly used as the value of the
 *      expandProc in a ParseValue.  It uses malloc to allocate
 *      more space for the result of a parse.
 *
 * Results:
 *      The buffer space in *parsePtr is reallocated to something
 *      larger, and if parsePtr->clientData is non-zero the old
 *      buffer is freed.  Information is copied from the old
 *      buffer to the new one.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------
 */
void
RbcExpandParseValue(
    RbcParseValue * parsePtr,   /* Information about buffer that
                                 * must be expanded.  If the clientData
                                 * in the structure is non-zero, it
                                 * means that the current buffer is
                                 * dynamically allocated. */
    int needed)
{                               /* Minimum amount of additional space
                                 * to allocate. */
    int             size;
    char           *buffer;

    /*
     * Either double the size of the buffer or add enough new space
     * to meet the demand, whichever produces a larger new buffer.
     */
    size = (parsePtr->end - parsePtr->buffer) + 1;
    if (size < needed) {
        size += needed;
    } else {
        size += size;
    }
    buffer = (char *) ckalloc((unsigned int) size);

    /*
     * Copy from old buffer to new, free old buffer if needed, and
     * mark new buffer as malloc-ed.
     */
    memcpy((void *) buffer, (void *) parsePtr->buffer,
        (size_t) (parsePtr->next - parsePtr->buffer));
    parsePtr->next = buffer + (parsePtr->next - parsePtr->buffer);
    if (parsePtr->clientData != 0) {
        ckfree((char *) parsePtr->buffer);
    }
    parsePtr->buffer = buffer;
    parsePtr->end = buffer + size - 1;
    parsePtr->clientData = (ClientData) 1;
}

/*
 *--------------------------------------------------------------
 *
 * RbcParseNestedCmd --
 *
 *      This procedure parses a nested Tcl command between
 *      brackets, returning the result of the command.
 *
 * Results:
 *      The return value is a standard Tcl result, which is
 *      TCL_OK unless there was an error while executing the
 *      nested command.  If an error occurs then interp->result
 *      contains a standard error message.  *TermPtr is filled
 *      in with the address of the character just after the
 *      last one processed;  this is usually the character just
 *      after the matching close-bracket, or the null character
 *      at the end of the string if the close-bracket was missing
 *      (a missing close bracket is an error).  The result returned
 *      by the command is stored in standard fashion in *parsePtr,
 *      null-terminated, with parsePtr->next pointing to the null
 *      character.
 *
 * Side effects:
 *      The storage space at *parsePtr may be expanded.
 *--------------------------------------------------------------
 */
int
RbcParseNestedCmd(
    Tcl_Interp * interp,        /* Interpreter to use for nested command
                                 * evaluations and error messages. */
    char *string,               /* Character just after opening bracket. */
    int flags,                  /* Flags to pass to nested Tcl_Eval. */
    char **termPtr,             /* Store address of terminating character
                                 * here. */
    RbcParseValue * parsePtr)
{                               /* Information about where to place
                                 * result of command. */
    int             level;
    register char  *src, *dest, *end;
    register char   c;
    char           *lastChar = string + strlen(string);
    int             length, shortfall;

    src = string;
    dest = parsePtr->next;
    end = parsePtr->end;
    level = 1;

    /*
     * Copy the characters one at a time to the result area, stopping
     * when the matching close-bracket is found.
     */

    for (;;) {
        c = *src;
        src++;

        if (dest == end) {
            parsePtr->next = dest;
            (*parsePtr->expandProc) (parsePtr, 20);
            dest = parsePtr->next;
            end = parsePtr->end;
        }
        *dest = c;
        dest++;

        if (CHAR_TYPE(src - 1, lastChar) == TCL_NORMAL) {
            continue;
        } else if (c == '[') {
            level++;
        } else if (c == ']') {
            level--;
            if (level == 0) {
                dest--;         /* Don't copy the last close bracket. */
                break;
            }
        } else if (c == '\\') {
            int             count;

            /*
             * Must always squish out backslash-newlines, even when in
             * braces.  This is needed so that this sequence can appear
             * anywhere in a command, such as the middle of an expression.
             */

            if (*src == '\n') {
                dest[-1] = Tcl_Backslash(src - 1, &count);
                src += count - 1;
            } else {
                Tcl_Backslash(src - 1, &count);
                while (count > 1) {
                    if (dest == end) {
                        parsePtr->next = dest;
                        (*parsePtr->expandProc) (parsePtr, 20);
                        dest = parsePtr->next;
                        end = parsePtr->end;
                    }
                    *dest = *src;
                    dest++;
                    src++;
                    count--;
                }
            }
        } else if (c == '\0') {
            Tcl_AppendResult(interp, "missing close-bracket", (char *) NULL);
            *termPtr = string - 1;
            return TCL_ERROR;
        }
    }

    *dest = '\0';
    /*
     * *dest contain the command. Now we call the command and get the result.
     */
    if (Tcl_Eval(interp, dest) != TCL_OK) {
        Tcl_AppendResult(interp, "command error: ", Tcl_GetStringResult(interp),
            (char *) NULL);
        return TCL_ERROR;
    }
    dest = Tcl_GetStringFromObj(Tcl_GetObjResult(interp), &length);
    shortfall = length + 1 - (parsePtr->end - parsePtr->next);
    if (shortfall > 0) {
        (*parsePtr->expandProc) (parsePtr, shortfall);
    }
    strcpy(parsePtr->next, dest);
    parsePtr->next += length;
    *termPtr = src;
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * RbcParseBraces --
 *
 *      This procedure scans the information between matching
 *      curly braces.
 *
 * Results:
 *      The return value is a standard Tcl result, which is
 *      TCL_OK unless there was an error while parsing string.
 *      If an error occurs then interp->result contains a
 *      standard error message.  *TermPtr is filled
 *      in with the address of the character just after the
 *      last one successfully processed;  this is usually the
 *      character just after the matching close-brace.  The
 *      information between curly braces is stored in standard
 *      fashion in *parsePtr, null-terminated with parsePtr->next
 *      pointing to the terminating null character.
 *
 * Side effects:
 *      The storage space at *parsePtr may be expanded.
 *
 *--------------------------------------------------------------
 */
int
RbcParseBraces(
    Tcl_Interp * interp,        /* Interpreter to use for nested command
                                 * evaluations and error messages. */
    char *string,               /* Character just after opening bracket. */
    char **termPtr,             /* Store address of terminating character here. */
    RbcParseValue * parsePtr)
{                               /* Information about where to place
                                 * result of command. */
    int             level;
    register char  *src, *dest, *end;
    register char   c;
    char           *lastChar = string + strlen(string);

    src = string;
    dest = parsePtr->next;
    end = parsePtr->end;
    level = 1;

    /*
     * Copy the characters one at a time to the result area, stopping
     * when the matching close-brace is found.
     */

    for (;;) {
        c = *src;
        src++;

        if (dest == end) {
            parsePtr->next = dest;
            (*parsePtr->expandProc) (parsePtr, 20);
            dest = parsePtr->next;
            end = parsePtr->end;
        }
        *dest = c;
        dest++;

        if (CHAR_TYPE(src - 1, lastChar) == TCL_NORMAL) {
            continue;
        } else if (c == '{') {
            level++;
        } else if (c == '}') {
            level--;
            if (level == 0) {
                dest--;         /* Don't copy the last close brace. */
                break;
            }
        } else if (c == '\\') {
            int             count;

            /*
             * Must always squish out backslash-newlines, even when in
             * braces.  This is needed so that this sequence can appear
             * anywhere in a command, such as the middle of an expression.
             */

            if (*src == '\n') {
                dest[-1] = Tcl_Backslash(src - 1, &count);
                src += count - 1;
            } else {
                Tcl_Backslash(src - 1, &count);
                while (count > 1) {
                    if (dest == end) {
                        parsePtr->next = dest;
                        (*parsePtr->expandProc) (parsePtr, 20);
                        dest = parsePtr->next;
                        end = parsePtr->end;
                    }
                    *dest = *src;
                    dest++;
                    src++;
                    count--;
                }
            }
        } else if (c == '\0') {
            Tcl_AppendResult(interp, "missing close-brace", (char *) NULL);
            *termPtr = string - 1;
            return TCL_ERROR;
        }
    }

    *dest = '\0';
    parsePtr->next = dest;
    *termPtr = src;
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * RbcParseQuotes --
 *
 *      This procedure parses a double-quoted string such as a
 *      quoted Tcl command argument or a quoted value in a Tcl
 *      expression.  This procedure is also used to parse array
 *      element names within parentheses, or anything else that
 *      needs all the substitutions that happen in quotes.
 *
 * Results:
 *      The return value is a standard Tcl result, which is
 *      TCL_OK unless there was an error while parsing the
 *      quoted string.  If an error occurs then interp->result
 *      contains a standard error message.  *TermPtr is filled
 *      in with the address of the character just after the
 *      last one successfully processed;  this is usually the
 *      character just after the matching close-quote.  The
 *      fully-substituted contents of the quotes are stored in
 *      standard fashion in *parsePtr, null-terminated with
 *      parsePtr->next pointing to the terminating null character.
 *
 * Side effects:
 *      The buffer space in parsePtr may be enlarged by calling its
 *      expandProc.
 *
 *--------------------------------------------------------------
 */
int
RbcParseQuotes(
    Tcl_Interp * interp,        /* Interpreter to use for nested command
                                 * evaluations and error messages. */
    char *string,               /* Character just after opening double-
                                 * quote. */
    int termChar,               /* Character that terminates "quoted" string
                                 * (usually double-quote, but sometimes
                                 * right-paren or something else). */
    int flags,                  /* Flags to pass to nested Tcl_Eval calls. */
    char **termPtr,             /* Store address of terminating character
                                 * here. */
    RbcParseValue * parsePtr)
{                               /* Information about where to place
                                 * fully-substituted result of parse. */
    register char  *src, *dest, c;
    char           *lastChar = string + strlen(string);

    src = string;
    dest = parsePtr->next;

    for (;;) {
        if (dest == parsePtr->end) {
            /*
             * Target buffer space is about to run out.  Make more space.
             */
            parsePtr->next = dest;
            (*parsePtr->expandProc) (parsePtr, 1);
            dest = parsePtr->next;
        }
        c = *src;
        src++;
        if (c == termChar) {
            *dest = '\0';
            parsePtr->next = dest;
            *termPtr = src;
            return TCL_OK;
        } else if (CHAR_TYPE(src - 1, lastChar) == TCL_NORMAL) {
          copy:
            *dest = c;
            dest++;
            continue;
        } else if (c == '$') {
            int             length;
            const char     *value;
            value = Tcl_ParseVar(interp, src - 1, (const char **) termPtr);
            if (value == NULL) {
                return TCL_ERROR;
            }
            src = *termPtr;
            length = strlen(value);
            if ((parsePtr->end - dest) <= length) {
                parsePtr->next = dest;
                (*parsePtr->expandProc) (parsePtr, length);
                dest = parsePtr->next;
            }
            strcpy(dest, value);
            dest += length;
            continue;
        } else if (c == '[') {
            int             result;

            parsePtr->next = dest;
            result = RbcParseNestedCmd(interp, src, flags, termPtr, parsePtr);
            if (result != TCL_OK) {
                return result;
            }
            src = *termPtr;
            dest = parsePtr->next;
            continue;
        } else if (c == '\\') {
            int             nRead;

            src--;
            *dest = Tcl_Backslash(src, &nRead);
            dest++;
            src += nRead;
            continue;
        } else if (c == '\0') {
            char            buf[30];

            Tcl_ResetResult(interp);
            sprintf(buf, "missing %c", termChar);
            Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, -1));
            *termPtr = string - 1;
            return TCL_ERROR;
        } else {
            goto copy;
        }
    }
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
