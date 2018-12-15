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

static int      BinaryOpSearch(
    RbcOpSpec specArr[],
    int nSpecs,
    const char *string);
static int      LinearOpSearch(
    RbcOpSpec specArr[],
    int nSpecs,
    const char *string);

/*
 *----------------------------------------------------------------------
 *
 * RbcCalloc --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void           *
RbcCalloc(
    unsigned int nElems,
    size_t sizeOfElem)
{
    char           *allocPtr;
    size_t          size;

    size = nElems * sizeOfElem;
    allocPtr = ckalloc(size);
    if (allocPtr != NULL) {
        memset(allocPtr, 0, size);
    }
    return allocPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcStrdup --
 *
 *      Create a copy of the string from heap storage.
 *
 * Results:
 *      Returns a pointer to the need string copy.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
char           *
RbcStrdup(
    const char *string)
{
    size_t          size;
    char           *allocPtr;

    size = strlen(string) + 1;
    allocPtr = ckalloc(size * sizeof(char));
    if (allocPtr != NULL) {
        strcpy(allocPtr, string);
    }
    return allocPtr;
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
