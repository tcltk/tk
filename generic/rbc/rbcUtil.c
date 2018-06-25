/*
 * rbcUtil.c --
 *
 *      This module implements utility procedures for the rbc
 *      toolkit.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

/*
#include <ctype.h>
#include <stdarg.h>
*/

static int      BinaryOpSearch(
    RbcOpSpec specArr[],
    int nSpecs,
    const char *string);
static int      LinearOpSearch(
    RbcOpSpec specArr[],
    int nSpecs,
    const char *string);

#if HAVE_UTF

/*
 *----------------------------------------------------------------------
 *
 * RbcDictionaryCompare
 *
 *      This function compares two strings as if they were being used in
 *      an index or card catalog.  The case of alphabetic characters is
 *      ignored, except to break ties.  Thus "B" comes before "b" but
 *      after "a".  Also, integers embedded in the strings compare in
 *      numerical order.  In other words, "x10y" comes after "x9y", not
 *      before it as it would when using strcmp().
 *
 * Results:
 *      A negative result means that the first element comes before the
 *      second, and a positive result means that the second element
 *      should come first.  A result of zero means the two elements
 *      are equal and it doesn't matter which comes first.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
int
RbcDictionaryCompare(
    char *left,
    char *right)
{
    Tcl_UniChar     uniLeft, uniRight, uniLeftLower, uniRightLower;
    int             diff, zeros;
    int             secondaryDiff = 0;

    for (;;) {
        if ((isdigit(UCHAR(*right))) && (isdigit(UCHAR(*left)))) {
            /*
             * There are decimal numbers embedded in the two
             * strings.  Compare them as numbers, rather than
             * strings.  If one number has more leading zeros than
             * the other, the number with more leading zeros sorts
             * later, but only as a secondary choice.
             */

            zeros = 0;
            while ((*right == '0') && (isdigit(UCHAR(right[1])))) {
                right++;
                zeros--;
            }
            while ((*left == '0') && (isdigit(UCHAR(left[1])))) {
                left++;
                zeros++;
            }
            if (secondaryDiff == 0) {
                secondaryDiff = zeros;
            }

            /*
             * The code below compares the numbers in the two
             * strings without ever converting them to integers.  It
             * does this by first comparing the lengths of the
             * numbers and then comparing the digit values.
             */

            diff = 0;
            for (;;) {
                if (diff == 0) {
                    diff = UCHAR(*left) - UCHAR(*right);
                }
                right++;
                left++;

                /* Ignore commas in numbers. */
                if (*left == ',') {
                    left++;
                }
                if (*right == ',') {
                    right++;
                }

                if (!isdigit(UCHAR(*right))) {  /* INTL: digit */
                    if (isdigit(UCHAR(*left))) {        /* INTL: digit */
                        return 1;
                    } else {
                        /*
                         * The two numbers have the same length. See
                         * if their values are different.
                         */

                        if (diff != 0) {
                            return diff;
                        }
                        break;
                    }
                } else if (!isdigit(UCHAR(*left))) {    /* INTL: digit */
                    return -1;
                }
            }
            continue;
        }

        /*
         * Convert character to Unicode for comparison purposes.  If either
         * string is at the terminating null, do a byte-wise comparison and
         * bail out immediately.
         */
        if ((*left != '\0') && (*right != '\0')) {
            left += Tcl_UtfToUniChar(left, &uniLeft);
            right += Tcl_UtfToUniChar(right, &uniRight);
            /*
             * Convert both chars to lower for the comparison, because
             * dictionary sorts are case insensitve.  Convert to lower, not
             * upper, so chars between Z and a will sort before A (where most
             * other interesting punctuations occur)
             */
            uniLeftLower = Tcl_UniCharToLower(uniLeft);
            uniRightLower = Tcl_UniCharToLower(uniRight);
        } else {
            diff = UCHAR(*left) - UCHAR(*right);
            break;
        }

        diff = uniLeftLower - uniRightLower;
        if (diff) {
            return diff;
        } else if (secondaryDiff == 0) {
            if (Tcl_UniCharIsUpper(uniLeft) && Tcl_UniCharIsLower(uniRight)) {
                secondaryDiff = -1;
            } else if (Tcl_UniCharIsUpper(uniRight)
                && Tcl_UniCharIsLower(uniLeft)) {
                secondaryDiff = 1;
            }
        }
    }
    if (diff == 0) {
        diff = secondaryDiff;
    }
    return diff;
}

#else

/*
 *--------------------------------------------------------------
 *
 * RbcDictionaryCompare --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int
RbcDictionaryCompare(
    char *left,                 /* Left string of the comparison */
    char *right)
{                               /* Right string of the comparison */
    int             diff, zeros;
    int             secondaryDiff = 0;

    while (1) {
        if (isdigit(UCHAR(*right)) && isdigit(UCHAR(*left))) {
            /*
             * There are decimal numbers embedded in the two
             * strings.  Compare them as numbers, rather than
             * strings.  If one number has more leading zeros than
             * the other, the number with more leading zeros sorts
             * later, but only as a secondary choice.
             */

            zeros = 0;
            while ((*right == '0') && (isdigit(UCHAR(right[1])))) {
                right++;
                zeros--;
            }
            while ((*left == '0') && (isdigit(UCHAR(left[1])))) {
                left++;
                zeros++;
            }
            if (secondaryDiff == 0) {
                secondaryDiff = zeros;
            }

            /*
             * The code below compares the numbers in the two
             * strings without ever converting them to integers.  It
             * does this by first comparing the lengths of the
             * numbers and then comparing the digit values.
             */

            diff = 0;
            while (1) {
                if (diff == 0) {
                    diff = UCHAR(*left) - UCHAR(*right);
                }
                right++;
                left++;
                /* Ignore commas in numbers. */
                if (*left == ',') {
                    left++;
                }
                if (*right == ',') {
                    right++;
                }
                if (!isdigit(UCHAR(*right))) {
                    if (isdigit(UCHAR(*left))) {
                        return 1;
                    } else {
                        /*
                         * The two numbers have the same length. See
                         * if their values are different.
                         */

                        if (diff != 0) {
                            return diff;
                        }
                        break;
                    }
                } else if (!isdigit(UCHAR(*left))) {
                    return -1;
                }
            }
            continue;
        }
        diff = UCHAR(*left) - UCHAR(*right);
        if (diff) {
            if (isupper(UCHAR(*left)) && islower(UCHAR(*right))) {
                diff = UCHAR(tolower(*left)) - UCHAR(*right);
                if (diff) {
                    return diff;
                } else if (secondaryDiff == 0) {
                    secondaryDiff = -1;
                }
            } else if (isupper(UCHAR(*right)) && islower(UCHAR(*left))) {
                diff = UCHAR(*left) - UCHAR(tolower(UCHAR(*right)));
                if (diff) {
                    return diff;
                } else if (secondaryDiff == 0) {
                    secondaryDiff = 1;
                }
            } else {
                return diff;
            }
        }
        if (*left == 0) {
            break;
        }
        left++;
        right++;
    }
    if (diff == 0) {
        diff = secondaryDiff;
    }
    return diff;
}
#endif

#ifndef NDEBUG

/*
 *--------------------------------------------------------------
 *
 * RbcAssert --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcAssert(
    char *testExpr,
    char *fileName,
    int lineNumber)
{
#ifdef WINDEBUG
    PurifyPrintf("line %d of %s: Assert \"%s\" failed\n", lineNumber,
        fileName, testExpr);
#endif
    fprintf(stderr, "line %d of %s: Assert \"%s\" failed\n",
        lineNumber, fileName, testExpr);
    fflush(stderr);
    abort();
}
#endif

/*
 *--------------------------------------------------------------
 *
 * RbcDStringAppendElements --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcDStringAppendElements(
    Tcl_DString * dsPtr,
    ...)
{
    va_list         argList;
    register char  *elem;

    va_start(argList, dsPtr);
    while ((elem = va_arg(argList, char *)) != NULL) {
        Tcl_DStringAppendElement(dsPtr, elem);
    }
    va_end(argList);
}

static char     stringRep[200];

/*
 *--------------------------------------------------------------
 *
 * RbcItoa --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
char           *
RbcItoa(
    int value)
{
    sprintf(stringRep, "%d", value);
    return stringRep;
}

/*
 *--------------------------------------------------------------
 *
 * RbcUtoa --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
char           *
RbcUtoa(
    unsigned int value)
{
    sprintf(stringRep, "%u", value);
    return stringRep;
}

/*
 *--------------------------------------------------------------
 *
 * RbcDtoa --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
char           *
RbcDtoa(
    Tcl_Interp * interp,
    double value)
{
    Tcl_PrintDouble(interp, value, stringRep);
    return stringRep;
}

#if HAVE_UTF

#undef fopen

/*
 *--------------------------------------------------------------
 *
 * RbcOpenUtfFile --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
FILE           *
RbcOpenUtfFile(
    char *fileName,
    char *mode)
{
    Tcl_DString     dString;
    FILE           *f;

    fileName = Tcl_UtfToExternalDString(NULL, fileName, -1, &dString);
    f = fopen(fileName, mode);
    Tcl_DStringFree(&dString);
    return f;
}

#endif /* HAVE_UTF */

/*
 *--------------------------------------------------------------
 *
 * RbcInitHexTable --
 *
 *      Table index for the hex values. Initialized once, first time.
 *      Used for translation value or delimiter significance lookup.
 *
 *      We build the table at run time for several reasons:
 *
 *        1.  portable to non-ASCII machines.
 *        2.  still reentrant since we set the init flag after setting
 *            table.
 *        3.  easier to extend.
 *        4.  less prone to bugs.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcInitHexTable(
    char hexTable[])
{
    hexTable['0'] = 0;
    hexTable['1'] = 1;
    hexTable['2'] = 2;
    hexTable['3'] = 3;
    hexTable['4'] = 4;
    hexTable['5'] = 5;
    hexTable['6'] = 6;
    hexTable['7'] = 7;
    hexTable['8'] = 8;
    hexTable['9'] = 9;
    hexTable['a'] = hexTable['A'] = 10;
    hexTable['b'] = hexTable['B'] = 11;
    hexTable['c'] = hexTable['C'] = 12;
    hexTable['d'] = hexTable['D'] = 13;
    hexTable['e'] = hexTable['E'] = 14;
    hexTable['f'] = hexTable['F'] = 15;
}

/*
 *--------------------------------------------------------------
 *
 * RbcGetPosition --
 *
 *      Convert a string representing a numeric position.
 *      A position can be in one of the following forms.
 *
 *        number - number of the item in the hierarchy, indexed
 *                 from zero.
 *        "end"  - last position in the hierarchy.
 *
 * Results:
 *      A standard Tcl result.  If "string" is a valid index, then
 *      *indexPtr is filled with the corresponding numeric index.
 *      If "end" was selected then *indexPtr is set to -1.
 *      Otherwise an error message is left in interp->result.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------
 */
int
RbcGetPosition(
    Tcl_Interp * interp,        /* Interpreter to report results back
                                 * to. */
    char *string,               /* String representation of the index.
                                 * Can be an integer or "end" to refer
                                 * to the last index. */
    int *indexPtr)
{                               /* Holds the converted index. */
    if ((string[0] == 'e') && (strcmp(string, "end") == 0)) {
        *indexPtr = -1;         /* Indicates last position in hierarchy. */
    } else {
        int             position;

        if (Tcl_GetInt(interp, string, &position) != TCL_OK) {
            return TCL_ERROR;
        }
        if (position < 0) {
            Tcl_AppendResult(interp, "bad position \"", string, "\"",
                (char *) NULL);
            return TCL_ERROR;
        }
        *indexPtr = position;
    }
    return TCL_OK;
}

/*
 * The hash table below is used to keep track of all the RbcUids created
 * so far.
 */
static Tcl_HashTable uidTable;
static int      uidInitialized = 0;

/*
 *----------------------------------------------------------------------
 *
 * RbcGetUid --
 *
 *      Given a string, returns a unique identifier for the string.
 *      A reference count is maintained, so that the identifier
 *      can be freed when it is not needed any more. This can be used
 *      in many places to replace Tcl_GetUid.
 *
 * Results:
 *      This procedure returns a RbcUid corresponding to the "string"
 *      argument.  The RbcUid has a string value identical to string
 *      (strcmp will return 0), but it's guaranteed that any other
 *      calls to this procedure with a string equal to "string" will
 *      return exactly the same result (i.e. can compare RbcUid
 *      *values* directly, without having to call strcmp on what they
 *      point to).
 *
 * Side effects:
 *      New information may be entered into the identifier table.
 *
 *----------------------------------------------------------------------
 */
RbcUid
RbcGetUid(
    const char *string)
{                               /* String to convert. */
    int             isNew;
    Tcl_HashEntry  *hPtr;
    int             refCount;

    if (!uidInitialized) {
        Tcl_InitHashTable(&uidTable, TCL_STRING_KEYS);
        uidInitialized = 1;
    }
    hPtr = Tcl_CreateHashEntry(&uidTable, string, &isNew);
    if (isNew) {
        refCount = 0;
    } else {
        refCount = (int) Tcl_GetHashValue(hPtr);
    }
    refCount++;
    Tcl_SetHashValue(hPtr, (ClientData) refCount);
    return (RbcUid) Tcl_GetHashKey(&uidTable, hPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcFreeUid --
 *
 *      Frees the RbcUid if there are no more clients using this
 *      identifier.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The identifier may be deleted from the identifier table.
 *
 *----------------------------------------------------------------------
 */
void
RbcFreeUid(
    RbcUid uid)
{                               /* Identifier to release. */
    Tcl_HashEntry  *hPtr;

    if (!uidInitialized) {
        Tcl_InitHashTable(&uidTable, TCL_STRING_KEYS);
        uidInitialized = 1;
    }
    hPtr = Tcl_FindHashEntry(&uidTable, uid);
    if (hPtr) {
        int             refCount;

        refCount = (int) Tcl_GetHashValue(hPtr);
        refCount--;
        if (refCount == 0) {
            Tcl_DeleteHashEntry(hPtr);
        } else {
            Tcl_SetHashValue(hPtr, (ClientData) refCount);
        }
    } else {
        fprintf(stderr, "tried to release unknown identifier \"%s\"\n", uid);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcFindUid --
 *
 *      Returns a RbcUid associated with a given string, if one
 *      exists.
 *
 * Results:
 *      A RbcUid for the string if one exists. Otherwise NULL.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcUid
RbcFindUid(
    char *string)
{                               /* String to find. */
    Tcl_HashEntry  *hPtr;

    if (!uidInitialized) {
        Tcl_InitHashTable(&uidTable, TCL_STRING_KEYS);
        uidInitialized = 1;
    }
    hPtr = Tcl_FindHashEntry(&uidTable, string);
    if (hPtr == NULL) {
        return NULL;
    }
    return (RbcUid) Tcl_GetHashKey(&uidTable, hPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * BinaryOpSearch --
 *
 *      Performs a binary search on the array of command operation
 *      specifications to find a partial, anchored match for the
 *      given operation string.
 *
 * Results:
 *      If the string matches unambiguously the index of the
 *      specification in the array is returned.  If the string does
 *      not match, even as an abbreviation, any operation, -1 is
 *      returned.  If the string matches, but ambiguously -2 is
 *      returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
BinaryOpSearch(
    RbcOpSpec specArr[],
    int nSpecs,
    const char *string)
{                               /* Name of minor operation to search for */
    RbcOpSpec      *specPtr;
    char            c;
    register int    high, low, median;
    register int    compare, length;

    low = 0;
    high = nSpecs - 1;
    c = string[0];
    length = strlen(string);
    while (low <= high) {
        median = (low + high) >> 1;
        specPtr = specArr + median;

        /* Test the first character */
        compare = c - specPtr->name[0];
        if (compare == 0) {
            /* Now test the entire string */
            compare = strncmp(string, specPtr->name, length);
            if (compare == 0) {
                if (length < specPtr->minChars) {
                    return -2;  /* Ambiguous operation name */
                }
            }
        }
        if (compare < 0) {
            high = median - 1;
        } else if (compare > 0) {
            low = median + 1;
        } else {
            return median;      /* Op found. */
        }
    }
    return -1;                  /* Can't find operation */
}

/*
 *----------------------------------------------------------------------
 *
 * LinearOpSearch --
 *
 *      Performs a binary search on the array of command operation
 *      specifications to find a partial, anchored match for the
 *      given operation string.
 *
 * Results:
 *      If the string matches unambiguously the index of the
 *      specification in the array is returned.  If the string does
 *      not match, even as an abbreviation, any operation, -1 is
 *      returned.  If the string matches, but ambiguously -2 is
 *      returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
LinearOpSearch(
    RbcOpSpec specArr[],
    int nSpecs,
    const char *string)
{                               /* Name of minor operation to search for */
    RbcOpSpec      *specPtr;
    char            c;
    int             length, nMatches, last;
    register int    i;

    c = string[0];
    length = strlen(string);
    nMatches = 0;
    last = -1;
    for (specPtr = specArr, i = 0; i < nSpecs; i++, specPtr++) {
        if ((c == specPtr->name[0]) &&
            (strncmp(string, specPtr->name, length) == 0)) {
            last = i;
            nMatches++;
            if (length == specPtr->minChars) {
                break;
            }
        }
    }
    if (nMatches > 1) {
        return -2;              /* Ambiguous operation name */
    }
    if (nMatches == 0) {
        return -1;              /* Can't find operation */
    }
    return last;                /* Op found. */
}

/*
 *----------------------------------------------------------------------
 *
 * RbcGetOp --
 *
 *      Find the command operation given a string name.  This is
 *      useful where a group of command operations have the same
 *      argument signature.
 *
 * Results:
 *      If found, a pointer to the procedure (function pointer) is
 *      returned.  Otherwise NULL is returned and an error message
 *      containing a list of the possible commands is returned in
 *      interp->result.
 *
 *
 * Side effects:
 *      TODO: Side Effects
 *----------------------------------------------------------------------
 */
RbcOp
RbcGetOp(
    Tcl_Interp * interp,        /* Interpreter to report errors to */
    int nSpecs,                 /* Number of specifications in array */
    RbcOpSpec specArr[],        /* Op specification array */
    int operPos,                /* Index of the operation name argument */
    int argc,                   /* Number of arguments in the argument vector.
                                 * This includes any prefixed arguments */
    const char **argv,          /* Argument vector */
    int flags)
{                               /*  */
    RbcOpSpec      *specPtr;
    const char     *string;
    register int    i;
    register int    n;

    if (argc <= operPos) {      /* No operation argument */
        Tcl_AppendResult(interp, "wrong # args: ", (char *) NULL);
      usage:
        Tcl_AppendResult(interp, "should be one of...", (char *) NULL);
        for (n = 0; n < nSpecs; n++) {
            Tcl_AppendResult(interp, "\n  ", (char *) NULL);
            for (i = 0; i < operPos; i++) {
                Tcl_AppendResult(interp, argv[i], " ", (char *) NULL);
            }
            specPtr = specArr + n;
            Tcl_AppendResult(interp, specPtr->name, " ", specPtr->usage,
                (char *) NULL);
        }
        return NULL;
    }
    string = argv[operPos];
    if (flags & RBC_OP_LINEAR_SEARCH) {
        n = LinearOpSearch(specArr, nSpecs, string);
    } else {
        n = BinaryOpSearch(specArr, nSpecs, string);
    }
    if (n == -2) {
        char            c;
        int             length;

        Tcl_AppendResult(interp, "ambiguous", (char *) NULL);
        if (operPos > 2) {
            Tcl_AppendResult(interp, " ", argv[operPos - 1], (char *) NULL);
        }
        Tcl_AppendResult(interp, " operation \"", string, "\" matches:",
            (char *) NULL);

        c = string[0];
        length = strlen(string);
        for (n = 0; n < nSpecs; n++) {
            specPtr = specArr + n;
            if ((c == specPtr->name[0]) &&
                (strncmp(string, specPtr->name, length) == 0)) {
                Tcl_AppendResult(interp, " ", specPtr->name, (char *) NULL);
            }
        }
        return NULL;

    } else if (n == -1) {       /* Can't find operation, display help */
        Tcl_AppendResult(interp, "bad", (char *) NULL);
        if (operPos > 2) {
            Tcl_AppendResult(interp, " ", argv[operPos - 1], (char *) NULL);
        }
        Tcl_AppendResult(interp, " operation \"", string, "\": ",
            (char *) NULL);
        goto usage;
    }
    specPtr = specArr + n;
    if ((argc < specPtr->minArgs) || ((specPtr->maxArgs > 0) &&
            (argc > specPtr->maxArgs))) {
        Tcl_AppendResult(interp, "wrong # args: should be \"", (char *) NULL);
        for (i = 0; i < operPos; i++) {
            Tcl_AppendResult(interp, argv[i], " ", (char *) NULL);
        }
        Tcl_AppendResult(interp, specPtr->name, " ", specPtr->usage, "\"",
            (char *) NULL);
        return NULL;
    }
    return specPtr->proc;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcGetOpFromObj --
 *
 *      Find the command operation given a string name.  This is
 *      useful where a group of command operations have the same
 *      argument signature.
 *
 * Results:
 *      If found, a pointer to the procedure (function pointer) is
 *      returned.  Otherwise NULL is returned and an error message
 *      containing a list of the possible commands is returned in
 *      interp->result.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcOp
RbcGetOpFromObj(
    Tcl_Interp * interp,        /* Interpreter to report errors to */
    int nSpecs,                 /* Number of specifications in array */
    RbcOpSpec specArr[],        /* Op specification array */
    int operPos,                /* Position of operation in argument list. */
    int objc,                   /* Number of arguments in the argument vector.
                                 * This includes any prefixed arguments */
    Tcl_Obj * const objv[],     /* Argument vector */
    int flags)
{
    RbcOpSpec      *specPtr;
    char           *string;
    register int    i;
    register int    n;

    if (objc <= operPos) {      /* No operation argument */
        Tcl_AppendResult(interp, "wrong # args: ", (char *) NULL);
      usage:
        Tcl_AppendResult(interp, "should be one of...", (char *) NULL);
        for (n = 0; n < nSpecs; n++) {
            Tcl_AppendResult(interp, "\n  ", (char *) NULL);
            for (i = 0; i < operPos; i++) {
                Tcl_AppendResult(interp, Tcl_GetString(objv[i]), " ",
                    (char *) NULL);
            }
            specPtr = specArr + n;
            Tcl_AppendResult(interp, specPtr->name, " ", specPtr->usage,
                (char *) NULL);
        }
        return NULL;
    }
    string = Tcl_GetString(objv[operPos]);
    if (flags & RBC_OP_LINEAR_SEARCH) {
        n = LinearOpSearch(specArr, nSpecs, string);
    } else {
        n = BinaryOpSearch(specArr, nSpecs, string);
    }
    if (n == -2) {
        char            c;
        int             length;

        Tcl_AppendResult(interp, "ambiguous", (char *) NULL);
        if (operPos > 2) {
            Tcl_AppendResult(interp, " ", Tcl_GetString(objv[operPos - 1]),
                (char *) NULL);
        }
        Tcl_AppendResult(interp, " operation \"", string, "\" matches:",
            (char *) NULL);

        c = string[0];
        length = strlen(string);
        for (n = 0; n < nSpecs; n++) {
            specPtr = specArr + n;
            if ((c == specPtr->name[0]) &&
                (strncmp(string, specPtr->name, length) == 0)) {
                Tcl_AppendResult(interp, " ", specPtr->name, (char *) NULL);
            }
        }
        return NULL;

    } else if (n == -1) {       /* Can't find operation, display help */
        Tcl_AppendResult(interp, "bad", (char *) NULL);
        if (operPos > 2) {
            Tcl_AppendResult(interp, " ", Tcl_GetString(objv[operPos - 1]),
                (char *) NULL);
        }
        Tcl_AppendResult(interp, " operation \"", string, "\": ",
            (char *) NULL);
        goto usage;
    }
    specPtr = specArr + n;
    if ((objc < specPtr->minArgs) ||
        ((specPtr->maxArgs > 0) && (objc > specPtr->maxArgs))) {
        Tcl_AppendResult(interp, "wrong # args: should be \"", (char *) NULL);
        for (i = 0; i < operPos; i++) {
            Tcl_AppendResult(interp, Tcl_GetString(objv[i]), " ",
                (char *) NULL);
        }
        Tcl_AppendResult(interp, specPtr->name, " ", specPtr->usage, "\"",
            (char *) NULL);
        return NULL;
    }
    return specPtr->proc;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
