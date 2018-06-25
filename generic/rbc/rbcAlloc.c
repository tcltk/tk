/*
 * rbcAlloc.C --
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

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
