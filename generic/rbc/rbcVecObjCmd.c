
/*
 * rbcVecObjCmd.c --
 *
 *      This file contains all commands to process the operations
 *      on instances of a vector.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

#define BUFFER_SIZE 1024

enum NativeFormats {
    FMT_UNKNOWN = -1,
    FMT_UCHAR,
    FMT_CHAR,
    FMT_USHORT,
    FMT_SHORT,
    FMT_UINT,
    FMT_INT,
    FMT_ULONG,
    FMT_LONG,
    FMT_FLOAT,
    FMT_DOUBLE
};

/* Pointer to the array of values currently being sorted. */
static RbcVectorObject **sortVectorArr;
static int      nSortVectors;
static int      reverse;

static int      AppendVector(
    RbcVectorObject * destPtr,
    RbcVectorObject * srcPtr);
static int      AppendList(
    RbcVectorObject * vPtr,
    int objc,
    Tcl_Obj * const objv[]);
static enum NativeFormats GetBinaryFormat(
    Tcl_Interp * interp,
    char *string,
    int *sizePtr);
static int      CopyValues(
    RbcVectorObject * vPtr,
    char *byteArr,
    enum NativeFormats fmt,
    int size,
    int length,
    int swap,
    int *indexPtr);
static int      InRange(
    double value,
    double min,
    double max);
static int      CopyList(
    RbcVectorObject * vPtr,
    int objc,
    Tcl_Obj * const objv[]);
static int     *SortVectors(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const *objv);
static int      CompareVectors(
    void *a,
    void *b);

/*
 * -----------------------------------------------------------------------
 *
 * RbcAppendOp --
 *
 *      Appends one of more Tcl lists of values, or vector objects
 *      onto the end of the current vector object.
 *
 * Results:
 *      A standard Tcl result.  If a current vector can't be created,
 *      resized, any of the named vectors can't be found, or one of
 *      lists of values is invalid, TCL_ERROR is returned.
 *
 * Side Effects:
 *      Clients of current vector will be notified of the change.
 *
 * -----------------------------------------------------------------------
 */
int
RbcAppendOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    register int    i;
    int             result;
    RbcVectorObject *v2Ptr;

    for (i = 2; i < objc; i++) {
        v2Ptr = RbcVectorParseElement((Tcl_Interp *) NULL, vPtr->dataPtr,
            Tcl_GetStringFromObj(objv[i], NULL), (char **) NULL,
            RBC_NS_SEARCH_BOTH);
        if (v2Ptr != NULL) {
            result = AppendVector(vPtr, v2Ptr);
        } else {
            int             nElem;
            Tcl_Obj       **elemObjArr;

            if (Tcl_ListObjGetElements(interp, objv[i], &nElem,
                    &elemObjArr) != TCL_OK) {
                return TCL_ERROR;
            }
            result = AppendList(vPtr, nElem, elemObjArr);
        }
        if (result != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (objc > 2) {
        if (vPtr->flush) {
            RbcVectorFlushCache(vPtr);
        }
        RbcVectorUpdateClients(vPtr);
    }
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcArithOp --
 *
 *     TODO: Description
 *
 * Results:
 *      A standard Tcl result.  If the source vector doesn't exist
 *      or the source list is not a valid list of numbers, TCL_ERROR
 *      returned.  Otherwise TCL_OK is returned.
 *
 * Side Effects:
 *      The vector data is reset.  Clients of the vector are notified.
 *      Any cached array indices are flushed.
 *
 * -----------------------------------------------------------------------
 */
int
RbcArithOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const *objv)
{
    register double value;
    register int    i;
    RbcVectorObject *v2Ptr;
    double          scalar;
    Tcl_Obj        *listObjPtr;
    char           *string;

    v2Ptr =
        RbcVectorParseElement((Tcl_Interp *) NULL, vPtr->dataPtr,
        Tcl_GetString(objv[2]), (char **) NULL, RBC_NS_SEARCH_BOTH);
    if (v2Ptr != NULL) {
        register int    j;
        int             length;

        length = v2Ptr->last - v2Ptr->first + 1;
        if (length != vPtr->length) {
            Tcl_AppendResult(interp, "vectors \"", Tcl_GetString(objv[0]),
                "\" and \"", Tcl_GetString(objv[2]),
                "\" are not the same length", (char *) NULL);
            return TCL_ERROR;
        }
        string = Tcl_GetString(objv[1]);
        listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
        switch (string[0]) {
        case '*':
            for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                value = vPtr->valueArr[i] * v2Ptr->valueArr[j];
                Tcl_ListObjAppendElement(interp, listObjPtr,
                    Tcl_NewDoubleObj(value));
            }
            break;
        case '/':
            for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                value = vPtr->valueArr[i] / v2Ptr->valueArr[j];
                Tcl_ListObjAppendElement(interp, listObjPtr,
                    Tcl_NewDoubleObj(value));
            }
            break;
        case '-':
            for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                value = vPtr->valueArr[i] - v2Ptr->valueArr[j];
                Tcl_ListObjAppendElement(interp, listObjPtr,
                    Tcl_NewDoubleObj(value));
            }
            break;
        case '+':
            for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                value = vPtr->valueArr[i] + v2Ptr->valueArr[j];
                Tcl_ListObjAppendElement(interp, listObjPtr,
                    Tcl_NewDoubleObj(value));
            }
            break;
        }
        Tcl_SetObjResult(interp, listObjPtr);
    } else if (RbcGetDouble(interp, objv[2], &scalar) == TCL_OK) {
        listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
        string = Tcl_GetString(objv[1]);
        switch (string[0]) {
        case '*':
            for (i = 0; i < vPtr->length; i++) {
                value = vPtr->valueArr[i] * scalar;
                Tcl_ListObjAppendElement(interp, listObjPtr,
                    Tcl_NewDoubleObj(value));
            }
            break;
        case '/':
            for (i = 0; i < vPtr->length; i++) {
                value = vPtr->valueArr[i] / scalar;
                Tcl_ListObjAppendElement(interp, listObjPtr,
                    Tcl_NewDoubleObj(value));
            }
            break;
        case '-':
            for (i = 0; i < vPtr->length; i++) {
                value = vPtr->valueArr[i] - scalar;
                Tcl_ListObjAppendElement(interp, listObjPtr,
                    Tcl_NewDoubleObj(value));
            }
            break;
        case '+':
            for (i = 0; i < vPtr->length; i++) {
                value = vPtr->valueArr[i] + scalar;
                Tcl_ListObjAppendElement(interp, listObjPtr,
                    Tcl_NewDoubleObj(value));
            }
            break;
        }
        Tcl_SetObjResult(interp, listObjPtr);
    } else {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcBinreadOp --
 *
 *      Reads binary values from a Tcl channel. Values are either appended
 *      to the end of the vector or placed at a given index (using the
 *      "-at" option), overwriting existing values.  Data is read until EOF
 *      is found on the channel or a specified number of values are read.
 *      (note that this is not necessarily the same as the number of bytes).
 *
 *      The following flags are supported:
 *        -swap          Swap bytes
 *        -at index      Start writing data at the index.
 *        -format fmt    Specifies the format of the data.
 *
 *      This binary reader was created by Harald Kirsch (kir@iitb.fhg.de).
 *
 * Results:
 *      Returns a standard Tcl result. The interpreter result will contain
 *      the number of values (not the number of bytes) read.
 *
 * Caveats:
 *      Channel reads must end on an element boundary.
 *
 * -----------------------------------------------------------------------
 */
int
RbcBinreadOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Channel     channel;
    char           *byteArr;
    char           *string;
    enum NativeFormats fmt;
    int             arraySize, bytesRead;
    int             count, total;
    int             first;
    int             size, length, mode;
    int             swap;
    register int    i;

    string = Tcl_GetStringFromObj(objv[2], NULL);
    channel = Tcl_GetChannel(interp, string, &mode);
    if (channel == NULL) {
        return TCL_ERROR;
    }
    if ((mode & TCL_READABLE) == 0) {
        Tcl_AppendResult(interp, "channel \"", string,
            "\" wasn't opened for reading", (char *) NULL);
        return TCL_ERROR;
    }
    first = vPtr->length;
    fmt = FMT_DOUBLE;
    size = sizeof(double);
    swap = FALSE;
    count = 0;

    if (objc > 3) {
        string = Tcl_GetStringFromObj(objv[3], NULL);
        if (string[0] != '-') {
            long int        value;
            /* Get the number of values to read.  */
            if (Tcl_GetLongFromObj(interp, objv[3], &value) != TCL_OK) {
                return TCL_ERROR;
            }
            if (value < 0) {
                Tcl_AppendResult(interp, "count can't be negative",
                    (char *) NULL);
                return TCL_ERROR;
            }
            count = (int) value;
            objc--;
            objv++;
        }
    }

    /* Process any option-value pairs that remain.  */
    for (i = 3; i < objc; i++) {
        string = Tcl_GetStringFromObj(objv[i], NULL);
        if (strcmp(string, "-swap") == 0) {
            swap = TRUE;
        } else if (strcmp(string, "-format") == 0) {
            i++;
            if (i >= objc) {
                Tcl_AppendResult(interp, "missing arg after \"", string, "\"",
                    (char *) NULL);
                return TCL_ERROR;
            }
            string = Tcl_GetStringFromObj(objv[i], NULL);
            fmt = GetBinaryFormat(interp, string, &size);
            if (fmt == FMT_UNKNOWN) {
                return TCL_ERROR;
            }
        } else if (strcmp(string, "-at") == 0) {
            i++;
            if (i >= objc) {
                Tcl_AppendResult(interp, "missing arg after \"", string, "\"",
                    (char *) NULL);
                return TCL_ERROR;
            }
            string = Tcl_GetString(objv[i]);
            if (RbcVectorGetIndex(interp, vPtr, string, &first, 0,
                    (RbcVectorIndexProc **) NULL) != TCL_OK) {
                return TCL_ERROR;
            }
            if (first > vPtr->length) {
                Tcl_AppendResult(interp, "index \"", string,
                    "\" is out of range", (char *) NULL);
                return TCL_ERROR;
            }
        }
    }

    if (count == 0) {
        arraySize = BUFFER_SIZE * size;
    } else {
        arraySize = count * size;
    }

    byteArr = (char *) ckalloc(arraySize);

    /* FIXME: restore old channel translation later? */
    if (Tcl_SetChannelOption(interp, channel, "-translation",
            "binary") != TCL_OK) {
        return TCL_ERROR;
    }
    total = 0;
    while (!Tcl_Eof(channel)) {
        bytesRead = Tcl_Read(channel, byteArr, arraySize);
        if (bytesRead < 0) {
            Tcl_AppendResult(interp, "error reading channel: ",
                Tcl_PosixError(interp), (char *) NULL);
            return TCL_ERROR;
        }
        if ((bytesRead % size) != 0) {
            Tcl_AppendResult(interp, "error reading channel: short read",
                (char *) NULL);
            return TCL_ERROR;
        }
        length = bytesRead / size;
        if (CopyValues(vPtr, byteArr, fmt, size, length, swap,
                &first) != TCL_OK) {
            return TCL_ERROR;
        }
        total += length;
        if (count > 0) {
            break;
        }
    }
    ckfree((char *) byteArr);

    if (vPtr->flush) {
        RbcVectorFlushCache(vPtr);
    }
    RbcVectorUpdateClients(vPtr);

    /* Set the result as the number of values read.  */
    Tcl_SetObjResult(interp, Tcl_NewIntObj(total));
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcClearOp --
 *
 *      Deletes all the accumulated array indices for the Tcl array
 *      associated will the vector.  This routine can be used to
 *      free excess memory from a large vector.
 *
 * Results:
 *      Always returns TCL_OK.
 *
 * Side Effects:
 *      Memory used for the entries of the Tcl array variable is freed.
 *
 * -----------------------------------------------------------------------
 */
int
RbcClearOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    RbcVectorFlushCache(vPtr);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcDeleteOp --
 *
 *      Deletes the given indices from the vector.  If no indices are
 *      provided the entire vector is deleted.
 *
 * Results:
 *      A standard Tcl result.  If any of the given indices is invalid,
 *      interp->result will an error message and TCL_ERROR is returned.
 *
 * Side Effects:
 *      The clients of the vector will be notified of the vector
 *      deletions.
 *
 * -----------------------------------------------------------------------
 */
int
RbcDeleteOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    unsigned char  *unsetArr;
    register int    i, j;
    register int    count;
    char           *string;

    /* FIXME: Don't delete vector with no indices.  */
    if (objc == 2) {
        RbcVectorFree(vPtr);
        return TCL_OK;
    }
    /*
     * Allocate an "unset" bitmap the size of the vector.
     */
    unsetArr = (unsigned char *) RbcCalloc(sizeof(unsigned char),
        (vPtr->length + 7) / 8);

    /***    assert(unsetArr); */

#define SetBit(i) \
    unsetArr[(i) >> 3] |= (1 << ((i) & 0x07))
#define GetBit(i) \
    (unsetArr[(i) >> 3] & (1 << ((i) & 0x07)))

    for (i = 2; i < objc; i++) {
        string = Tcl_GetStringFromObj(objv[i], NULL);
        if (RbcVectorGetIndexRange(interp, vPtr, string,
                (RBC_INDEX_COLON | RBC_INDEX_CHECK),
                (RbcVectorIndexProc **) NULL) != TCL_OK) {
            ckfree((char *) unsetArr);
            return TCL_ERROR;
        }
        for (j = vPtr->first; j <= vPtr->last; j++) {
            SetBit(j);          /* Mark the range of elements for deletion. */
        }
    }
    count = 0;
    for (i = 0; i < vPtr->length; i++) {
        if (GetBit(i)) {
            continue;           /* Skip elements marked for deletion. */
        }
        if (count < i) {
            vPtr->valueArr[count] = vPtr->valueArr[i];
        }
        count++;
    }
    ckfree((char *) unsetArr);
    vPtr->length = count;
    if (vPtr->flush) {
        RbcVectorFlushCache(vPtr);
    }
    RbcVectorUpdateClients(vPtr);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcDupOp --
 *
 *      Creates one or more duplicates of the vector object.
 *
 * Results:
 *      A standard Tcl result.  If a new vector can't be created,
 *      or and existing vector resized, TCL_ERROR is returned.
 *
 * Side Effects:
 *      Clients of existing vectors will be notified of the change.
 *
 * -----------------------------------------------------------------------
 */
int
RbcDupOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    RbcVectorObject *v2Ptr;
    int             isNew;
    register int    i;
    char           *string;

    for (i = 2; i < objc; i++) {
        string = Tcl_GetStringFromObj(objv[i], NULL);
        v2Ptr = RbcVectorCreate(vPtr->dataPtr, string, string, string, &isNew);
        if (v2Ptr == NULL) {
            return TCL_ERROR;
        }
        if (v2Ptr == vPtr) {
            continue;
        }
        if (RbcVectorDuplicate(v2Ptr, vPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (!isNew) {
            if (v2Ptr->flush) {
                RbcVectorFlushCache(v2Ptr);
            }
            RbcVectorUpdateClients(v2Ptr);
        }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcExprOp --
 *
 *      Computes the result of the expression which may be
 *      either a scalar (single value) or vector (list of values).
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int
RbcExprOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    if (RbcExprVector(interp, Tcl_GetStringFromObj(objv[2], NULL),
            (RbcVector *) vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (vPtr->flush) {
        RbcVectorFlushCache(vPtr);
    }
    RbcVectorUpdateClients(vPtr);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcIndexOp --
 *
 *      Sets or reads the value of the index.  This simulates what the
 *      vector's variable does.
 *
 * Results:
 *      A standard Tcl result.  If the index is invalid,
 *      interp->result will an error message and TCL_ERROR is returned.
 *      Otherwise interp->result will contain the values.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int
RbcIndexOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    int             first, last;
    char           *string;

    string = Tcl_GetStringFromObj(objv[2], NULL);
    if (RbcVectorGetIndexRange(interp, vPtr, string, RBC_INDEX_ALL_FLAGS,
            (RbcVectorIndexProc **) NULL) != TCL_OK) {
        return TCL_ERROR;
    }
    first = vPtr->first, last = vPtr->last;
    if (objc == 3) {
        Tcl_Obj        *listObjPtr;

        if (first == vPtr->length) {
            Tcl_AppendResult(interp, "can't get index \"", string, "\"",
                (char *) NULL);
            return TCL_ERROR;   /* Can't read from index "++end" */
        }
        listObjPtr = RbcGetValues(vPtr, first, last);
        Tcl_SetObjResult(interp, listObjPtr);
    } else {
        double          value;

        /* FIXME: huh? Why set values here?.  */
        if (first == RBC_SPECIAL_INDEX) {
            Tcl_AppendResult(interp, "can't set index \"", string, "\"",
                (char *) NULL);
            return TCL_ERROR;   /* Tried to set "min" or "max" */
        }
        if (RbcGetDouble(vPtr->interp, objv[3], &value) != TCL_OK) {
            return TCL_ERROR;
        }
        if (first == vPtr->length) {
            if (RbcVectorChangeLength(vPtr, vPtr->length + 1) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        RbcReplicateValue(vPtr, first, last, value);
        Tcl_SetObjResult(interp, objv[3]);
        if (vPtr->flush) {
            RbcVectorFlushCache(vPtr);
        }
        RbcVectorUpdateClients(vPtr);
    }
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcLengthOp --
 *
 *      Returns the length of the vector.  If a new size is given, the
 *      vector is resized to the new vector.
 *
 * Results:
 *      A standard Tcl result.  If the new length is invalid,
 *      interp->result will an error message and TCL_ERROR is returned.
 *      Otherwise interp->result will contain the length of the vector.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int
RbcLengthOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    if (objc == 3) {
        int             size;

        if (Tcl_GetIntFromObj(interp, objv[2], &size) != TCL_OK) {
            return TCL_ERROR;
        }
        if (size < 0) {
            Tcl_AppendResult(interp, "bad vector size \"",
                Tcl_GetString(objv[2]), "\"", (char *) NULL);
            return TCL_ERROR;
        }
        if (RbcVectorChangeLength(vPtr, size) != TCL_OK) {
            return TCL_ERROR;
        }
        if (vPtr->flush) {
            RbcVectorFlushCache(vPtr);
        }
        RbcVectorUpdateClients(vPtr);
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(vPtr->length));
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcMergeOp --
 *
 *      Merges the values from the given vectors to the current vector.
 *
 * Results:
 *      A standard Tcl result.  If any of the given vectors differ in size,
 *      TCL_ERROR is returned.  Otherwise TCL_OK is returned and the
 *      vector data will contain merged values of the given vectors.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int
RbcMergeOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    RbcVectorObject *v2Ptr;
    RbcVectorObject **vecArr;
    register RbcVectorObject **vPtrPtr;
    int             refSize, length, nElem;
    register int    i;
    double         *valuePtr, *valueArr;

    /* Allocate an array of vector pointers of each vector to be
     * merged in the current vector.  */
    vecArr = (RbcVectorObject **) ckalloc(sizeof(RbcVectorObject *) * objc);

    /***    assert(vecArr); */
    vPtrPtr = vecArr;

    refSize = -1;
    nElem = 0;
    for (i = 2; i < objc; i++) {
        if (RbcVectorLookupName(vPtr->dataPtr, Tcl_GetStringFromObj(objv[i],
                    NULL), &v2Ptr) != TCL_OK) {
            ckfree((char *) vecArr);
            return TCL_ERROR;
        }
        /* Check that all the vectors are the same length */
        length = v2Ptr->last - v2Ptr->first + 1;
        if (refSize < 0) {
            refSize = length;
        } else if (length != refSize) {
            Tcl_AppendResult(vPtr->interp, "vectors \"", vPtr->name,
                "\" and \"", v2Ptr->name, "\" differ in length", (char *) NULL);
            ckfree((char *) vecArr);
            return TCL_ERROR;
        }
        *vPtrPtr++ = v2Ptr;
        nElem += refSize;
    }
    *vPtrPtr = NULL;

    valueArr = (double *) ckalloc(sizeof(double) * nElem);
    if (valueArr == NULL) {
        Tcl_AppendPrintfToObj(Tcl_GetObjResult(vPtr->interp),
            "not enough memory to allocate %d vector elements", nElem);
        return TCL_ERROR;
    }
    /* Merge the values from each of the vectors into the current vector */
    valuePtr = valueArr;
    for (i = 0; i < refSize; i++) {
        for (vPtrPtr = vecArr; *vPtrPtr != NULL; vPtrPtr++) {
            *valuePtr++ = (*vPtrPtr)->valueArr[i + (*vPtrPtr)->first];
        }
    }
    ckfree((char *) vecArr);
    RbcVectorReset(vPtr, valueArr, nElem, nElem, TCL_DYNAMIC);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcNormalizeOp --
 *
 *      Normalizes the vector.
 *
 * Results:
 *      A standard Tcl result.  If the density is invalid, TCL_ERROR
 *      is returned.  Otherwise TCL_OK is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int
RbcNormalizeOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    register int    i;
    double          range;

    RbcVectorUpdateRange(vPtr);
    range = vPtr->max - vPtr->min;
    if (objc > 2) {
        RbcVectorObject *v2Ptr;
        int             isNew;
        char           *string;

        string = Tcl_GetStringFromObj(objv[2], NULL);
        v2Ptr = RbcVectorCreate(vPtr->dataPtr, string, string, string, &isNew);
        if (v2Ptr == NULL) {
            return TCL_ERROR;
        }
        if (RbcVectorChangeLength(v2Ptr, vPtr->length) != TCL_OK) {
            return TCL_ERROR;
        }
        for (i = 0; i < vPtr->length; i++) {
            v2Ptr->valueArr[i] = (vPtr->valueArr[i] - vPtr->min) / range;
        }
        RbcVectorUpdateRange(v2Ptr);
        if (!isNew) {
            if (v2Ptr->flush) {
                RbcVectorFlushCache(v2Ptr);
            }
            RbcVectorUpdateClients(v2Ptr);
        }
    } else {
        double          norm;
        Tcl_Obj        *listObjPtr;

        listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
        for (i = 0; i < vPtr->length; i++) {
            norm = (vPtr->valueArr[i] - vPtr->min) / range;
            Tcl_ListObjAppendElement(interp, listObjPtr,
                Tcl_NewDoubleObj(norm));
        }
        Tcl_SetObjResult(interp, listObjPtr);
    }
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcOffsetOp --
 *
 *      Queries or sets the offset of the array index from the base
 *      address of the data array of values.
 *
 * Results:
 *      A standard Tcl result.  If the source vector doesn't exist
 *      or the source list is not a valid list of numbers, TCL_ERROR
 *      returned.  Otherwise TCL_OK is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int
RbcOffsetOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    if (objc == 3) {
        int             newOffset;

        if (Tcl_GetIntFromObj(interp, objv[2], &newOffset) != TCL_OK) {
            return TCL_ERROR;
        }
        vPtr->offset = newOffset;
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(vPtr->offset));
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcPopulateOp --
 *
 *      Creates or resizes a new vector based upon the density specified.
 *
 * Results:
 *      A standard Tcl result.  If the density is invalid, TCL_ERROR
 *      is returned.  Otherwise TCL_OK is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int
RbcPopulateOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    RbcVectorObject *v2Ptr;
    int             size, density;
    int             isNew;
    register int    i, j;
    double          slice, range;
    register double *valuePtr;
    int             count;
    char           *string;

    string = Tcl_GetStringFromObj(objv[2], NULL);
    v2Ptr = RbcVectorCreate(vPtr->dataPtr, string, string, string, &isNew);
    if (v2Ptr == NULL) {
        return TCL_ERROR;
    }
    if (vPtr->length == 0) {
        return TCL_OK;          /* Source vector is empty. */
    }
    if (Tcl_GetIntFromObj(interp, objv[3], &density) != TCL_OK) {
        return TCL_ERROR;
    }
    if (density < 1) {
        Tcl_AppendResult(interp, "bad density \"", Tcl_GetStringFromObj(objv[3],
                NULL), "\"", (char *) NULL);
        return TCL_ERROR;
    }
    size = (vPtr->length - 1) * (density + 1) + 1;
    if (RbcVectorChangeLength(v2Ptr, size) != TCL_OK) {
        return TCL_ERROR;
    }
    count = 0;
    valuePtr = v2Ptr->valueArr;
    for (i = 0; i < (vPtr->length - 1); i++) {
        range = vPtr->valueArr[i + 1] - vPtr->valueArr[i];
        slice = range / (double) (density + 1);
        for (j = 0; j <= density; j++) {
            *valuePtr = vPtr->valueArr[i] + (slice * (double) j);
            valuePtr++;
            count++;
        }
    }
    count++;
    *valuePtr = vPtr->valueArr[i];

    /*** assert(count == v2Ptr->length); */
    if (!isNew) {
        if (v2Ptr->flush) {
            RbcVectorFlushCache(v2Ptr);
        }
        RbcVectorUpdateClients(v2Ptr);
    }
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcRandomOp --
 *
 *      Generates random values for the length of the vector.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int
RbcRandomOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    register int    i;

    for (i = 0; i < vPtr->length; i++) {
        vPtr->valueArr[i] = Rbcdrand48();
    }
    if (vPtr->flush) {
        RbcVectorFlushCache(vPtr);
    }
    RbcVectorUpdateClients(vPtr);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcRangeOp --
 *
 *      Returns a Tcl list of the range of vector values specified.
 *
 * Results:
 *      A standard Tcl result.  If the given range is invalid, TCL_ERROR
 *      is returned.  Otherwise TCL_OK is returned and interp->result
 *      will contain the list of values.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int
RbcRangeOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Obj        *listObjPtr;
    int             first, last;
    register int    i;

    if ((RbcVectorGetIndex(interp, vPtr, Tcl_GetStringFromObj(objv[2], NULL),
                &first, RBC_INDEX_CHECK,
                (RbcVectorIndexProc **) NULL) != TCL_OK)
        || (RbcVectorGetIndex(interp, vPtr, Tcl_GetStringFromObj(objv[3], NULL),
                &last, RBC_INDEX_CHECK,
                (RbcVectorIndexProc **) NULL) != TCL_OK)) {
        return TCL_ERROR;
    }
    listObjPtr = Tcl_NewListObj(0, NULL);
    if (first > last) {
        /* Return the list reversed */
        for (i = last; i <= first; i++) {
            Tcl_ListObjAppendElement(interp, listObjPtr,
                Tcl_NewDoubleObj(vPtr->valueArr[i]));
        }
    } else {
        for (i = first; i <= last; i++) {
            Tcl_ListObjAppendElement(interp, listObjPtr,
                Tcl_NewDoubleObj(vPtr->valueArr[i]));
        }
    }
    Tcl_SetObjResult(interp, listObjPtr);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcSearchOp --
 *
 *      Searchs for a value in the vector. Returns the indices of all
 *      vector elements matching a particular value.
 *
 * Results:
 *      Always returns TCL_OK.  interp->result will contain a list of
 *      the indices of array elements matching value. If no elements
 *      match, interp->result will contain the empty string.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int
RbcSearchOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    double          min, max;
    register int    i;
    int             wantValue;
    char           *string;
    Tcl_Obj        *listObjPtr;

    wantValue = FALSE;
    string = Tcl_GetStringFromObj(objv[2], NULL);
    if ((string[0] == '-') && (strcmp(string, "-value") == 0)) {
        wantValue = TRUE;
        objv++, objc--;
    }
    if (RbcGetDouble(interp, objv[2], &min) != TCL_OK) {
        return TCL_ERROR;
    }
    max = min;
    if ((objc > 3) && (RbcGetDouble(interp, objv[3], &max) != TCL_OK)) {
        return TCL_ERROR;
    }
    if ((min - max) >= DBL_EPSILON) {
        return TCL_OK;          /* Bogus range. Don't bother looking. */
    }
    listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
    if (wantValue) {
        for (i = 0; i < vPtr->length; i++) {
            if (InRange(vPtr->valueArr[i], min, max)) {
                Tcl_ListObjAppendElement(interp, listObjPtr,
                    Tcl_NewDoubleObj(vPtr->valueArr[i]));
            }
        }
    } else {
        for (i = 0; i < vPtr->length; i++) {
            if (InRange(vPtr->valueArr[i], min, max)) {
                Tcl_ListObjAppendElement(interp, listObjPtr,
                    Tcl_NewIntObj(i + vPtr->offset));
            }
        }
    }
    Tcl_SetObjResult(interp, listObjPtr);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcSeqOp --
 *
 *      Generates a sequence of values in the vector.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int
RbcSeqOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    register int    i;
    double          start, finish, step;
    int             fillVector;
    int             nSteps;
    char           *string;

    if (RbcGetDouble(interp, objv[2], &start) != TCL_OK) {
        return TCL_ERROR;
    }
    fillVector = FALSE;
    string = Tcl_GetStringFromObj(objv[3], NULL);
    if ((string[0] == 'e') && (strcmp(string, "end") == 0)) {
        fillVector = TRUE;
    } else if (RbcGetDouble(interp, objv[3], &finish) != TCL_OK) {
        return TCL_ERROR;
    }
    step = 1.0;
    if ((objc > 4) && (RbcGetDouble(interp, objv[4], &step) != TCL_OK)) {
        return TCL_ERROR;
    }
    if (fillVector) {
        nSteps = vPtr->length;
    } else {
        nSteps = (int) ((finish - start) / step) + 1;
    }
    if (nSteps > 0) {
        if (RbcVectorChangeLength(vPtr, nSteps) != TCL_OK) {
            return TCL_ERROR;
        }
        for (i = 0; i < nSteps; i++) {
            vPtr->valueArr[i] = start + (step * (double) i);
        }
        if (vPtr->flush) {
            RbcVectorFlushCache(vPtr);
        }
        RbcVectorUpdateClients(vPtr);
    }
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcSetOp --
 *
 *      Sets the data of the vector object from a list of values.
 *
 * Results:
 *      A standard Tcl result.  If the source vector doesn't exist
 *      or the source list is not a valid list of numbers, TCL_ERROR
 *      returned.  Otherwise TCL_OK is returned.
 *
 * Side Effects:
 *      The vector data is reset.  Clients of the vector are notified.
 *      Any cached array indices are flushed.
 *
 * -----------------------------------------------------------------------
 */
int
RbcSetOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    int             result;
    RbcVectorObject *v2Ptr;
    int             nElem;
    Tcl_Obj       **elemObjArr;

    /* The source can be either a list of numbers or another vector.  */

    v2Ptr =
        RbcVectorParseElement((Tcl_Interp *) NULL, vPtr->dataPtr,
        Tcl_GetStringFromObj(objv[2], NULL), NULL, RBC_NS_SEARCH_BOTH);
    if (v2Ptr != NULL) {
        if (vPtr == v2Ptr) {
            RbcVectorObject *tmpPtr;
            /*
             * Source and destination vectors are the same.  Copy the
             * source first into a temporary vector to avoid memory
             * overlaps.
             */
            tmpPtr = RbcVectorNew(vPtr->dataPtr);
            result = RbcVectorDuplicate(tmpPtr, v2Ptr);
            if (result == TCL_OK) {
                result = RbcVectorDuplicate(vPtr, tmpPtr);
            }
            RbcVectorFree(tmpPtr);
        } else {
            result = RbcVectorDuplicate(vPtr, v2Ptr);
        }
    } else if (Tcl_ListObjGetElements(interp, objv[2], &nElem,
            &elemObjArr) == TCL_OK) {
        result = CopyList(vPtr, nElem, elemObjArr);
    } else {
        return TCL_ERROR;
    }

    if (result == TCL_OK) {
        /*
         * The vector has changed; so flush the array indices (they're
         * wrong now), find the new range of the data, and notify
         * the vector's clients that it's been modified.
         */
        if (vPtr->flush) {
            RbcVectorFlushCache(vPtr);
        }
        RbcVectorUpdateClients(vPtr);
    }
    return result;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcSortOp --
 *
 *      Sorts the vector object and any other vectors according to
 *      sorting order of the vector object.
 *
 * Results:
 *      A standard Tcl result.  If any of the auxiliary vectors are
 *      a different size than the sorted vector object, TCL_ERROR is
 *      returned.  Otherwise TCL_OK is returned.
 *
 * Side Effects:
 *      The vectors are sorted.
 *
 * -----------------------------------------------------------------------
 */
int
RbcSortOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    RbcVectorObject *v2Ptr;
    char           *string;
    double         *mergeArr;
    int            *iArr;
    int             refSize, nBytes;
    int             result;
    register int    i, n;

    reverse = FALSE;
    if (objc > 2) {
        int             length;
        string = Tcl_GetStringFromObj(objv[2], &length);
        if (string[0] == '-') {
            if ((length > 1) && (strncmp(string, "-reverse", length) == 0)) {
                reverse = TRUE;
            } else {
                Tcl_AppendResult(interp, "unknown flag \"", string,
                    "\": should be \"-reverse\"", (char *) NULL);
                return TCL_ERROR;
            }
            objc--;
            objv++;
        }
    }
    if (objc > 2) {
        iArr = SortVectors(vPtr, interp, objc - 2, objv + 2);
    } else {
        iArr = RbcVectorSortIndex(&vPtr, 1);
    }
    if (iArr == NULL) {
        return TCL_ERROR;
    }
    refSize = vPtr->length;

    /*
     * Create an array to store a copy of the current values of the
     * vector. We'll merge the values back into the vector based upon
     * the indices found in the index array.
     */
    nBytes = sizeof(double) * refSize;
    mergeArr = (double *) ckalloc(nBytes);
    memcpy((char *) mergeArr, (char *) vPtr->valueArr, nBytes);
    for (n = 0; n < refSize; n++) {
        vPtr->valueArr[n] = mergeArr[iArr[n]];
    }
    if (vPtr->flush) {
        RbcVectorFlushCache(vPtr);
    }
    RbcVectorUpdateClients(vPtr);

    /* Now sort any other vectors in the same fashion.  The vectors
     * must be the same size as the iArr though.  */
    result = TCL_ERROR;
    for (i = 2; i < objc; i++) {
        if (RbcVectorLookupName(vPtr->dataPtr, Tcl_GetString(objv[i]),
                &v2Ptr) != TCL_OK) {
            goto error;
        }
        if (v2Ptr->length != refSize) {
            Tcl_AppendResult(interp, "vector \"", v2Ptr->name,
                "\" is not the same size as \"", vPtr->name, "\"",
                (char *) NULL);
            goto error;
        }
        memcpy((char *) mergeArr, (char *) v2Ptr->valueArr, nBytes);
        for (n = 0; n < refSize; n++) {
            v2Ptr->valueArr[n] = mergeArr[iArr[n]];
        }
        RbcVectorUpdateClients(v2Ptr);
        if (v2Ptr->flush) {
            RbcVectorFlushCache(v2Ptr);
        }
    }
    result = TCL_OK;
  error:
    ckfree((char *) mergeArr);
    ckfree((char *) iArr);
    return result;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcSplitOp --
 *
 *      Copies the values from the vector evens into one of more
 *      vectors.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int
RbcSplitOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    int             nVectors;

    nVectors = objc - 2;
    if ((vPtr->length % nVectors) != 0) {
        Tcl_AppendPrintfToObj(Tcl_GetObjResult(interp),
            "can't split vector \"%s\" into %d even parts.",
            vPtr->name, nVectors);
        return TCL_ERROR;
    }
    if (nVectors > 0) {
        RbcVectorObject *v2Ptr;
        char           *string; /* Name of vector. */
        int             i, j, k;
        int             oldSize, newSize, extra, isNew;

        extra = vPtr->length / nVectors;
        for (i = 0; i < nVectors; i++) {
            string = Tcl_GetStringFromObj(objv[i + 2], NULL);
            v2Ptr =
                RbcVectorCreate(vPtr->dataPtr, string, string, string, &isNew);
            oldSize = v2Ptr->length;
            newSize = oldSize + extra;
            if (RbcVectorChangeLength(v2Ptr, newSize) != TCL_OK) {
                return TCL_ERROR;
            }
            for (j = i, k = oldSize; j < vPtr->length; j += nVectors, k++) {
                v2Ptr->valueArr[k] = vPtr->valueArr[j];
            }
            RbcVectorUpdateClients(v2Ptr);
            if (v2Ptr->flush) {
                RbcVectorFlushCache(v2Ptr);
            }
        }
    }
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcVariableOp --
 *
 *      Renames the variable associated with the vector
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int
RbcVariableOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    if (objc > 2) {
        if (RbcVectorMapVariable(interp, vPtr,
                Tcl_GetString(objv[2])) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (vPtr->arrayName != NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(vPtr->arrayName, -1));
    }
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * AppendVector --
 *
 *      Appends a vector to the end of another vector
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static int
AppendVector(
    RbcVectorObject * destPtr,
    RbcVectorObject * srcPtr)
{
    int             nBytes;
    int             oldSize, newSize;

    oldSize = destPtr->length;
    newSize = oldSize + srcPtr->last - srcPtr->first + 1;
    if (RbcVectorChangeLength(destPtr, newSize) != TCL_OK) {
        return TCL_ERROR;
    }
    nBytes = (newSize - oldSize) * sizeof(double);
    memcpy((char *) (destPtr->valueArr + oldSize), (srcPtr->valueArr
            + srcPtr->first), nBytes);
    destPtr->notifyFlags |= RBC_UPDATE_RANGE;
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * AppendList --
 *
 *      Appends a list to the end of another list
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static int
AppendList(
    RbcVectorObject * vPtr,
    int objc,
    Tcl_Obj * const objv[])
{
    int             count;
    register int    i;
    double          value;
    int             oldSize;

    oldSize = vPtr->length;
    if (RbcVectorChangeLength(vPtr, vPtr->length + objc) != TCL_OK) {
        return TCL_ERROR;
    }
    count = oldSize;
    for (i = 0; i < objc; i++) {
        if (RbcGetDouble(vPtr->interp, objv[i], &value) != TCL_OK) {
            RbcVectorChangeLength(vPtr, count);
            return TCL_ERROR;
        }
        vPtr->valueArr[count++] = value;
    }
    vPtr->notifyFlags |= RBC_UPDATE_RANGE;
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * GetBinaryFormat
 *
 *      Translates a format string into a native type.  Formats may be
 *      as follows.
 *
 *            signed     i1, i2, i4, i8
 *            unsigned   u1, u2, u4, u8
 *            real       r4, r8, r16
 *
 *      But there must be a corresponding native type.  For example,
 *      this for reading 2-byte binary integers from an instrument and
 *      converting them to unsigned shorts or ints.
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static enum NativeFormats
GetBinaryFormat(
    Tcl_Interp * interp,
    char *string,
    int *sizePtr)
{
    char            c;

    c = tolower(string[0]);
    if (Tcl_GetInt(interp, string + 1, sizePtr) != TCL_OK) {
        Tcl_AppendResult(interp, "unknown binary format \"", string,
            "\": incorrect byte size", (char *) NULL);
        return FMT_UNKNOWN;
    }
    switch (c) {
    case 'r':
        if (*sizePtr == sizeof(double)) {
            return FMT_DOUBLE;
        } else if (*sizePtr == sizeof(float)) {
            return FMT_FLOAT;
        }
        break;

    case 'i':
        if (*sizePtr == sizeof(char)) {
            return FMT_CHAR;
        } else if (*sizePtr == sizeof(int)) {
            return FMT_INT;
        } else if (*sizePtr == sizeof(long)) {
            return FMT_LONG;
        } else if (*sizePtr == sizeof(short)) {
            return FMT_SHORT;
        }
        break;

    case 'u':
        if (*sizePtr == sizeof(unsigned char)) {
            return FMT_UCHAR;
        } else if (*sizePtr == sizeof(unsigned int)) {
            return FMT_UINT;
        } else if (*sizePtr == sizeof(unsigned long)) {
            return FMT_ULONG;
        } else if (*sizePtr == sizeof(unsigned short)) {
            return FMT_USHORT;
        }
        break;

    default:
        Tcl_AppendResult(interp, "unknown binary format \"", string,
            "\": should be either i#, r#, u# (where # is size in bytes)",
            (char *) NULL);
        return FMT_UNKNOWN;
    }
    Tcl_AppendResult(interp, "can't handle format \"", string, "\"",
        (char *) NULL);
    return FMT_UNKNOWN;
}

/*
 *--------------------------------------------------------------
 *
 * CopyValues --
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
static int
CopyValues(
    RbcVectorObject * vPtr,
    char *byteArr,
    enum NativeFormats fmt,
    int size,
    int length,
    int swap,
    int *indexPtr)
{
    register int    i, n;
    int             newSize;

    if ((swap) && (size > 1)) {
        int             nBytes = size * length;
        register unsigned char *p;
        register int    left, right;

        for (i = 0; i < nBytes; i += size) {
            p = (unsigned char *) (byteArr + i);
            for (left = 0, right = size - 1; left < right; left++, right--) {
                p[left] ^= p[right];
                p[right] ^= p[left];
                p[left] ^= p[right];
            }
        }
    }
    newSize = *indexPtr + length;
    if (newSize > vPtr->length) {
        if (RbcVectorChangeLength(vPtr, newSize) != TCL_OK) {
            return TCL_ERROR;
        }
    }
#define CopyArrayToVector(vPtr, arr) \
	    for (i = 0, n = *indexPtr; i < length; i++, n++) { \
		(vPtr)->valueArr[n] = (double)(arr)[i]; \
	    }

    switch (fmt) {
    case FMT_CHAR:
        CopyArrayToVector(vPtr, (char *) byteArr);
        break;

    case FMT_UCHAR:
        CopyArrayToVector(vPtr, (unsigned char *) byteArr);
        break;

    case FMT_INT:
        CopyArrayToVector(vPtr, (int *) byteArr);
        break;

    case FMT_UINT:
        CopyArrayToVector(vPtr, (unsigned int *) byteArr);
        break;

    case FMT_LONG:
        CopyArrayToVector(vPtr, (long *) byteArr);
        break;

    case FMT_ULONG:
        CopyArrayToVector(vPtr, (unsigned long *) byteArr);
        break;

    case FMT_SHORT:
        CopyArrayToVector(vPtr, (short int *) byteArr);
        break;

    case FMT_USHORT:
        CopyArrayToVector(vPtr, (unsigned short int *) byteArr);
        break;

    case FMT_FLOAT:
        CopyArrayToVector(vPtr, (float *) byteArr);
        break;

    case FMT_DOUBLE:
        CopyArrayToVector(vPtr, (double *) byteArr);
        break;

    case FMT_UNKNOWN:
        break;
    }
    *indexPtr += length;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * InRange --
 *
 *      Determines if a value lies within a given range.
 *
 *      The value is normalized and compared against the interval
 *      [0..1], where 0.0 is the minimum and 1.0 is the maximum.
 *      DBL_EPSILON is the smallest number that can be represented
 *      on the host machine, such that (1.0 + epsilon) != 1.0.
 *
 *      Please note, min cannot be greater than max.
 *
 * Results:
 *      If the value is within of the interval [min..max], 1 is
 *      returned; 0 otherwise.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
InRange(
    double value,
    double min,
    double max)
{
    double          range;

    range = max - min;
    if (range < DBL_EPSILON) {
        return (FABS(max - value) < DBL_EPSILON);
    } else {
        double          norm;

        norm = (value - min) / range;
        return ((norm >= -DBL_EPSILON) && ((norm - 1.0) < DBL_EPSILON));
    }
}

/*
 *--------------------------------------------------------------
 *
 * CopyList --
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
static int
CopyList(
    RbcVectorObject * vPtr,
    int objc,
    Tcl_Obj * const objv[])
{
    register int    i;
    double          value;

    if (RbcVectorChangeLength(vPtr, objc) != TCL_OK) {
        return TCL_ERROR;
    }
    for (i = 0; i < objc; i++) {
        if (RbcGetDouble(vPtr->interp, objv[i], &value) != TCL_OK) {
            RbcVectorChangeLength(vPtr, i);
            return TCL_ERROR;
        }
        vPtr->valueArr[i] = value;
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * RbcVectorSortIndex --
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
int            *
RbcVectorSortIndex(
    RbcVectorObject ** vPtrPtr,
    int nVectors)
{
    int            *indexArr;
    register int    i;
    RbcVectorObject *vPtr = *vPtrPtr;
    int             length;

    length = vPtr->last - vPtr->first + 1;
    indexArr = (int *) ckalloc(sizeof(int) * length);
    for (i = vPtr->first; i <= vPtr->last; i++) {
        indexArr[i] = i;
    }
    sortVectorArr = vPtrPtr;
    nSortVectors = nVectors;
    qsort((char *) indexArr, length, sizeof(int),
        (QSortCompareProc *) CompareVectors);
    return indexArr;
}

/*
 *--------------------------------------------------------------
 *
 * SortVectors --
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
static int     *
SortVectors(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const *objv)
{
    RbcVectorObject **vPtrArray, *v2Ptr;
    int            *iArr;
    register int    i;

    vPtrArray =
        (RbcVectorObject **) ckalloc(sizeof(RbcVectorObject *) * (objc + 1));
    vPtrArray[0] = vPtr;
    iArr = NULL;
    for (i = 0; i < objc; i++) {
        if (RbcVectorLookupName(vPtr->dataPtr, Tcl_GetStringFromObj(objv[i],
                    NULL), &v2Ptr) != TCL_OK) {
            goto error;
        }
        if (v2Ptr->length != vPtr->length) {
            Tcl_AppendResult(interp, "vector \"", v2Ptr->name,
                "\" is not the same size as \"", vPtr->name, "\"",
                (char *) NULL);
            goto error;
        }
        vPtrArray[i + 1] = v2Ptr;
    }
    iArr = RbcVectorSortIndex(vPtrArray, objc + 1);
  error:
    ckfree((char *) vPtrArray);
    return iArr;
}

/*
 *--------------------------------------------------------------
 *
 * CompareVectors --
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
static int
CompareVectors(
    void *a,
    void *b)
{
    double          delta;
    int             i;
    int             sign;
    register RbcVectorObject *vPtr;

    sign = (reverse) ? -1 : 1;
    for (i = 0; i < nSortVectors; i++) {
        vPtr = sortVectorArr[i];
        delta = vPtr->valueArr[*(int *) a] - vPtr->valueArr[*(int *) b];
        if (delta < 0.0) {
            return (-1 * sign);
        } else if (delta > 0.0) {
            return (1 * sign);
        }
    }
    return 0;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
