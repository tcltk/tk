/*
 * rbcGrElem.c --
 *
 *      This module implements generic elements for the rbc graph widget.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkoGraph.h"

static Tk_OptionParseProc StringToData;
static Tk_OptionPrintProc DataToString;
static Tk_OptionParseProc StringToDataPairs;
static Tk_OptionPrintProc DataPairsToString;
static Tk_OptionParseProc StringToAlong;
static Tk_OptionPrintProc AlongToString;
static Tk_CustomOption alongOption = {
    StringToAlong, AlongToString, (ClientData) 0
};

Tk_CustomOption rbcDataOption = {
    StringToData, DataToString, (ClientData) 0
};

Tk_CustomOption rbcDataPairsOption = {
    StringToDataPairs, DataPairsToString, (ClientData) 0
};

extern Tk_CustomOption rbcDistanceOption;

static int counter;

static RbcVectorChangedProc VectorChangedProc;

static int GetPenStyle(
    RbcGraph * graph,
    const char *string,
    Tk_Uid type,
    RbcPenStyle * stylePtr);
static void SyncElemVector(
    RbcElemVector * vPtr);
static void FindRange(
    RbcElemVector * vPtr);
static void FreeDataVector(
    RbcElemVector * vPtr);
static int EvalExprList(
    Tcl_Interp * interp,
    const char *list,
    int *nElemPtr,
    double **arrayPtr);
static int GetIndex(
    Tcl_Interp * interp,
    RbcElement * elemPtr,
    const char *string,
    int *indexPtr);
static int NameToElement(
    RbcGraph * graph,
    const char *name,
    RbcElement ** elemPtrPtr);
static int CreateElement(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv,
    Tk_Uid classUid);
static void DestroyElement(
    RbcGraph * graph,
    RbcElement * elemPtr);
static int RebuildDisplayList(
    RbcGraph * graph,
    const char *newList);

static int ActivateOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int BindOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int CreateOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv,
    Tk_Uid type);
static int ConfigureOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char *argv[]);
static int DeactivateOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int DeleteOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int ExistsOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int GetOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char *argv[]);
static int NamesOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int ShowOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int TypeOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);

/*
 * ----------------------------------------------------------------------
 * Custom option parse and print procedures
 * ----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * GetPenStyle --
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
static int
GetPenStyle(
    RbcGraph * graph,
    const char *string,
    Tk_Uid type,
    RbcPenStyle * stylePtr)
{
    RbcPen *penPtr;
    Tcl_Interp *interp = graph->interp;
    const char **elemArr;
    int nElem;

    elemArr = NULL;
    if(Tcl_SplitList(interp, string, &nElem, &elemArr) != TCL_OK) {
        return TCL_ERROR;
    }
    if((nElem != 1) && (nElem != 3)) {
        Tcl_AppendResult(interp, "bad style \"", string, "\": should be ",
            "\"penName\" or \"penName min max\"", (char *)NULL);
        if(elemArr != NULL) {
            ckfree((char *)elemArr);
        }
        return TCL_ERROR;
    }
    if(RbcGetPen(graph, elemArr[0], type, &penPtr) != TCL_OK) {
        ckfree((char *)elemArr);
        return TCL_ERROR;
    }
    if(nElem == 3) {
    double min, max;

        if((Tcl_GetDouble(interp, elemArr[1], &min) != TCL_OK) ||
            (Tcl_GetDouble(interp, elemArr[2], &max) != TCL_OK)) {
            ckfree((char *)elemArr);
            return TCL_ERROR;
        }
        stylePtr->weight.min = min;
        stylePtr->weight.max = max;
        stylePtr->weight.range = (max > min) ? (max - min) : DBL_EPSILON;
    }
    stylePtr->penPtr = penPtr;
    ckfree((char *)elemArr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * SyncElemVector --
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
static void
SyncElemVector(
    RbcElemVector * vPtr)
{
    vPtr->nValues = vPtr->vecPtr->numValues;
    vPtr->valueArr = vPtr->vecPtr->valueArr;
    vPtr->min = RbcVecMin(vPtr->vecPtr);
    vPtr->max = RbcVecMax(vPtr->vecPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * FindRange --
 *
 *      Find the minimum, positive minimum, and maximum values in a
 *      given vector and store the results in the vector structure.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Minimum, positive minimum, and maximum values are stored in
 *      the vector.
 *
 *----------------------------------------------------------------------
 */
static void
FindRange(
    RbcElemVector * vPtr)
{
register int i;
register double *x;
register double min, max;

    if((vPtr->nValues < 1) || (vPtr->valueArr == NULL)) {
        return; /* This shouldn't ever happen. */
    }
    x = vPtr->valueArr;

    min = DBL_MAX, max = -DBL_MAX;
    for(i = 0; i < vPtr->nValues; i++) {
        if(!TclIsInfinite(x[i])) {
            min = max = x[i];
            break;
        }
    }
    /*  Initialize values to track the vector range */
    for( /* empty */ ; i < vPtr->nValues; i++) {
        if(!TclIsInfinite(x[i])) {
            if(x[i] < min) {
                min = x[i];
            } else if(x[i] > max) {
                max = x[i];
            }
        }
    }
    vPtr->min = min, vPtr->max = max;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcFindElemVectorMinimum --
 *
 *      Find the minimum, positive minimum, and maximum values in a
 *      given vector and store the results in the vector structure.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Minimum, positive minimum, and maximum values are stored in
 *      the vector.
 *
 *----------------------------------------------------------------------
 */
double
RbcFindElemVectorMinimum(
    RbcElemVector * vPtr,
    double minLimit)
{
    register int i;
    register double *arr;
    register double min, x;

    min = DBL_MAX;
    arr = vPtr->valueArr;
    for(i = 0; i < vPtr->nValues; i++) {
        x = arr[i];
        if(x < 0.0) {
            /* What do you do about negative values when using log
             * scale values seems like a grey area.  Mirror. */
            x = -x;
        }
        if((x > minLimit) && (min > x)) {
            min = x;
        }
    }
    if(min == DBL_MAX) {
        min = minLimit;
    }
    return min;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeDataVector --
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
static void
FreeDataVector(
    RbcElemVector * vPtr)
{
    if(vPtr->clientId != NULL) {
        Rbc_FreeVectorId(vPtr->clientId);       /* Free the old vector */
        vPtr->clientId = NULL;
    } else if(vPtr->valueArr != NULL) {
        ckfree((char *)vPtr->valueArr);
    }
    vPtr->valueArr = NULL;
    vPtr->nValues = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * VectorChangedProc --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Graph is redrawn.
 *
 *----------------------------------------------------------------------
 */
static void
VectorChangedProc(
    Tcl_Interp * interp,
    ClientData clientData,
    RbcVectorNotify notify)
{
RbcElemVector *vPtr = clientData;
RbcElement *elemPtr = vPtr->elemPtr;
RbcGraph *graph = elemPtr->graphPtr;

    switch (notify) {
    case RBC_VECTOR_NOTIFY_DESTROY:
        vPtr->clientId = NULL;
        vPtr->valueArr = NULL;
        vPtr->nValues = 0;
        break;

    case RBC_VECTOR_NOTIFY_UPDATE:
    default:
        Rbc_GetVectorById(interp, vPtr->clientId, &vPtr->vecPtr);
        SyncElemVector(vPtr);
        break;
    }
    graph->flags |= RBC_RESET_AXES;
    elemPtr->flags |= RBC_MAP_ITEM;
    if(!elemPtr->hidden) {
        graph->flags |= RBC_REDRAW_BACKING_STORE;
        RbcEventuallyRedrawGraph(graph);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * EvalExprList --
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
static int
EvalExprList(
    Tcl_Interp * interp,
    const char *list,
    int *nElemPtr,
    double **arrayPtr)
{
    int nElem;
    const char **elemArr;
    double *array;
    int result;

    result = TCL_ERROR;
    elemArr = NULL;
    if(Tcl_SplitList(interp, list, &nElem, &elemArr) != TCL_OK) {
        return TCL_ERROR;
    }
    array = NULL;
    if(nElem > 0) {
    register double *valuePtr;
    register int i;

        counter++;
        array = (double *)ckalloc(sizeof(double) * nElem);
        if(array == NULL) {
            Tcl_AppendResult(interp, "can't allocate new vector", (char *)NULL);
            goto badList;
        }
        valuePtr = array;
        for(i = 0; i < nElem; i++) {
            if(Tcl_ExprDouble(interp, elemArr[i], valuePtr) != TCL_OK) {
                goto badList;
            }
            valuePtr++;
        }
    }
    result = TCL_OK;

  badList:
    ckfree((char *)elemArr);
    *arrayPtr = array;
    *nElemPtr = nElem;
    if(result != TCL_OK) {
        ckfree((char *)array);
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToData --
 *
 *      Given a Tcl list of numeric expression representing the element
 *      values, convert into an array of double precision values. In
 *      addition, the minimum and maximum values are saved.  Since
 *      elastic values are allow (values which translate to the
 *      min/max of the graph), we must try to get the non-elastic
 *      minimum and maximum.
 *
 * Results:
 *      The return value is a standard Tcl result.  The vector is passed
 *      back via the vPtr.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToData(
    ClientData clientData,     /* Type of axis vector to fill */
    Tcl_Interp * interp,       /* Interpreter to send results back to */
    Tk_Window tkwin,           /* Not used. */
    const char *string,        /* Tcl list of expressions */
    char *widgRec,             /* Element record */
    int offset)
{              /* Offset of vector in Element record */
    RbcElement *elemPtr = (RbcElement *) (widgRec);
    RbcElemVector *vPtr = (RbcElemVector *) (widgRec + offset);

    FreeDataVector(vPtr);
    if(Rbc_VectorExists(interp, string)) {
    RbcVectorId clientId;

        clientId = RbcAllocVectorId(interp, string);
        if(Rbc_GetVectorById(interp, clientId, &vPtr->vecPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        Rbc_SetVectorChangedProc(clientId, VectorChangedProc, vPtr);
        vPtr->elemPtr = elemPtr;
        vPtr->clientId = clientId;
        SyncElemVector(vPtr);
        elemPtr->flags |= RBC_MAP_ITEM;
    } else {
    double *newArr;
    int nValues;

        if(EvalExprList(interp, string, &nValues, &newArr) != TCL_OK) {
            return TCL_ERROR;
        }
        if(nValues > 0) {
            vPtr->valueArr = newArr;
        }
        vPtr->nValues = nValues;
        FindRange(vPtr);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DataToString --
 *
 *      Convert the vector of floating point values into a Tcl list.
 *
 * Results:
 *      The string representation of the vector is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
DataToString(
    ClientData clientData,     /* Type of axis vector to print */
    Tk_Window tkwin,           /* Not used. */
    char *widgRec,             /* Element record */
    int offset,                /* Offset of vector in Element record */
    Tcl_FreeProc ** freeProcPtr)
{              /* Memory deallocation scheme to use */
    RbcElemVector *vPtr = (RbcElemVector *) (widgRec + offset);
    RbcElement *elemPtr = (RbcElement *) (widgRec);
    Tcl_DString dString;
    char *result;
    char string[TCL_DOUBLE_SPACE + 1];
    double *p, *endPtr;

    if(vPtr->clientId != NULL) {
        return Rbc_NameOfVectorId(vPtr->clientId);
    }
    if(vPtr->nValues == 0) {
        return "";
    }
    Tcl_DStringInit(&dString);
    endPtr = vPtr->valueArr + vPtr->nValues;
    for(p = vPtr->valueArr; p < endPtr; p++) {
        Tcl_PrintDouble(elemPtr->graphPtr->interp, *p, string);
        Tcl_DStringAppendElement(&dString, string);
    }
    result = Tcl_DStringValue(&dString);

    /*
     * If memory wasn't allocated for the dynamic string, do it here (it's
     * currently on the stack), so that Tcl can free it normally.
     */
    if(result == dString.staticSpace) {
        result = RbcStrdup(result);
    }
    *freeProcPtr = (Tcl_FreeProc *) Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToDataPairs --
 *
 *      This procedure is like StringToData except that it interprets
 *      the list of numeric expressions as X Y coordinate pairs.  The
 *      minimum and maximum for both the X and Y vectors are
 *      determined.
 *
 * Results:
 *      The return value is a standard Tcl result.  The vectors are
 *      passed back via the widget record (elemPtr).
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToDataPairs(
    ClientData clientData,     /* Not used. */
    Tcl_Interp * interp,       /* Interpreter to send results back to */
    Tk_Window tkwin,           /* Not used. */
    const char *string,        /* Tcl list of numeric expressions */
    char *widgRec,             /* Element record */
    int offset)
{              /* Not used. */
    RbcElement *elemPtr = (RbcElement *) widgRec;
    int nElem;
    unsigned int newSize;
    double *newArr;

    if(EvalExprList(interp, string, &nElem, &newArr) != TCL_OK) {
        return TCL_ERROR;
    }
    if(nElem & 1) {
        Tcl_AppendResult(interp, "odd number of data points", (char *)NULL);
        ckfree((char *)newArr);
        return TCL_ERROR;
    }
    nElem /= 2;
    newSize = nElem * sizeof(double);

    FreeDataVector(&elemPtr->x);
    FreeDataVector(&elemPtr->y);

    elemPtr->x.valueArr = (double *)ckalloc(newSize);
    elemPtr->y.valueArr = (double *)ckalloc(newSize);
    assert(elemPtr->x.valueArr && elemPtr->y.valueArr);
    elemPtr->x.nValues = elemPtr->y.nValues = nElem;

    if(newSize > 0) {
    register double *dataPtr;
    register int i;

        for(dataPtr = newArr, i = 0; i < nElem; i++) {
            elemPtr->x.valueArr[i] = *dataPtr++;
            elemPtr->y.valueArr[i] = *dataPtr++;
        }
        ckfree((char *)newArr);
        FindRange(&elemPtr->x);
        FindRange(&elemPtr->y);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DataPairsToString --
 *
 *      Convert pairs of floating point values in the X and Y arrays
 *      into a Tcl list.
 *
 * Results:
 *      The return value is a string (Tcl list).
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
DataPairsToString(
    ClientData clientData,     /* Not used. */
    Tk_Window tkwin,           /* Not used. */
    char *widgRec,             /* Element information record */
    int offset,                /* Not used. */
    Tcl_FreeProc ** freeProcPtr)
{              /* Memory deallocation scheme to use */
    RbcElement *elemPtr = (RbcElement *) widgRec;
    Tcl_Interp *interp = elemPtr->graphPtr->interp;
    int i;
    int length;
    char *result;
    char string[TCL_DOUBLE_SPACE + 1];
    Tcl_DString dString;

    length = RbcNumberOfPoints(elemPtr);
    if(length < 1) {
        return "";
    }
    Tcl_DStringInit(&dString);
    for(i = 0; i < length; i++) {
        Tcl_PrintDouble(interp, elemPtr->x.valueArr[i], string);
        Tcl_DStringAppendElement(&dString, string);
        Tcl_PrintDouble(interp, elemPtr->y.valueArr[i], string);
        Tcl_DStringAppendElement(&dString, string);
    }
    result = Tcl_DStringValue(&dString);

    /*
     * If memory wasn't allocated for the dynamic string, do it here
     * (it's currently on the stack), so that Tcl can free it
     * normally.
     */
    if(result == dString.staticSpace) {
        result = RbcStrdup(result);
    }
    *freeProcPtr = (Tcl_FreeProc *) Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToAlong --
 *
 *      Given a Tcl list of numeric expression representing the element
 *      values, convert into an array of double precision values. In
 *      addition, the minimum and maximum values are saved.  Since
 *      elastic values are allow (values which translate to the
 *      min/max of the graph), we must try to get the non-elastic
 *      minimum and maximum.
 *
 * Results:
 *      The return value is a standard Tcl result.  The vector is passed
 *      back via the vPtr.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToAlong(
    ClientData clientData,     /* Not used. */
    Tcl_Interp * interp,       /* Interpreter to send results back to */
    Tk_Window tkwin,           /* Not used. */
    const char *string,        /* String representation of value. */
    char *widgRec,             /* Widget record. */
    int offset)
{              /* Offset of field in widget record. */
    int *intPtr = (int *)(widgRec + offset);

    if((string[0] == 'x') && (string[1] == '\0')) {
        *intPtr = RBC_SEARCH_X;
    } else if((string[0] == 'y') && (string[1] == '\0')) {
        *intPtr = RBC_SEARCH_Y;
    } else if((string[0] == 'b') && (strcmp(string, "both") == 0)) {
        *intPtr = RBC_SEARCH_BOTH;
    } else {
        Tcl_AppendResult(interp, "bad along value \"", string, "\"",
            (char *)NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * AlongToString --
 *
 *      Convert the vector of floating point values into a Tcl list.
 *
 * Results:
 *      The string representation of the vector is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
AlongToString(
    ClientData clientData,     /* Not used. */
    Tk_Window tkwin,           /* Not used. */
    char *widgRec,             /* Widget record */
    int offset,                /* Offset of field in widget record */
    Tcl_FreeProc ** freeProcPtr)
{              /* Memory deallocation scheme to use */
    int along = *(int *)(widgRec + offset);

    switch (along) {
    case RBC_SEARCH_X:
        return "x";
    case RBC_SEARCH_Y:
        return "y";
    case RBC_SEARCH_BOTH:
        return "both";
    default:
        return "unknown along value";
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcFreePalette --
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
void
RbcFreePalette(
    RbcGraph * graph,
    RbcChain * palette)
{
RbcChainLink *linkPtr;

    /* Skip the first slot. It contains the built-in "normal" pen of
     * the element.  */
    linkPtr = RbcChainFirstLink(palette);
    if(linkPtr != NULL) {
register RbcPenStyle *stylePtr;
RbcChainLink *nextPtr;

        for(linkPtr = RbcChainNextLink(linkPtr); linkPtr != NULL;
            linkPtr = nextPtr) {
            nextPtr = RbcChainNextLink(linkPtr);
            stylePtr = RbcChainGetValue(linkPtr);
            RbcFreePen(graph, stylePtr->penPtr);
            RbcChainDeleteLink(palette, linkPtr);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcStringToStyles --
 *
 *      Parse the list of style names.
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int
RbcStringToStyles(
    ClientData clientData,     /* Not used. */
    Tcl_Interp * interp,       /* Interpreter to send results back to */
    Tk_Window tkwin,           /* Not used. */
    const char *string,        /* String representing style list */
    char *widgRec,             /* Element information record */
    int offset)
{              /* Offset of symbol type field in record */
    RbcChain *palette = *(RbcChain **) (widgRec + offset);
    RbcChainLink *linkPtr;
    RbcElement *elemPtr = (RbcElement *) (widgRec);
    RbcPenStyle *stylePtr;
    const char **elemArr;
    int nStyles;
    register int i;
    size_t size = (size_t) clientData;

    elemArr = NULL;
    RbcFreePalette(elemPtr->graphPtr, palette);
    if((string == NULL) || (*string == '\0')) {
        nStyles = 0;
    } else if(Tcl_SplitList(interp, string, &nStyles, &elemArr) != TCL_OK) {
        return TCL_ERROR;
    }
    /* Reserve the first entry for the "normal" pen. We'll set the
     * style later */
    linkPtr = RbcChainFirstLink(palette);
    if(linkPtr == NULL) {
        linkPtr = RbcChainAllocLink(size);
        RbcChainLinkBefore(palette, linkPtr, NULL);
    }
    stylePtr = RbcChainGetValue(linkPtr);
    stylePtr->penPtr = elemPtr->normalPenPtr;

    for(i = 0; i < nStyles; i++) {
        linkPtr = RbcChainAllocLink(size);
        stylePtr = RbcChainGetValue(linkPtr);
        stylePtr->weight.min = (double)i;
        stylePtr->weight.max = (double)i + 1.0;
        stylePtr->weight.range = 1.0;
        if(GetPenStyle(elemPtr->graphPtr, elemArr[i], elemPtr->classUid,
                (RbcPenStyle *) stylePtr) != TCL_OK) {
            ckfree((char *)elemArr);
            RbcFreePalette(elemPtr->graphPtr, palette);
            return TCL_ERROR;
        }
        RbcChainLinkBefore(palette, linkPtr, NULL);
    }
    if(elemArr != NULL) {
        ckfree((char *)elemArr);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcStylesToString --
 *
 *      Convert the style information into a string.
 *
 * Results:
 *      The string representing the style information is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
const char *
RbcStylesToString(
    ClientData clientData,     /* Not used. */
    Tk_Window tkwin,           /* Not used. */
    char *widgRec,             /* Element information record */
    int offset,                /* Not used. */
    Tcl_FreeProc ** freeProcPtr)
{              /* Not used. */
    RbcChain *palette = *(RbcChain **) (widgRec + offset);
    Tcl_DString dString;
    char *result;
    RbcChainLink *linkPtr;

    Tcl_DStringInit(&dString);
    linkPtr = RbcChainFirstLink(palette);
    if(linkPtr != NULL) {
    RbcElement *elemPtr = (RbcElement *) (widgRec);
    char string[TCL_DOUBLE_SPACE];
    Tcl_Interp *interp;
    RbcPenStyle *stylePtr;

        interp = elemPtr->graphPtr->interp;
        for(linkPtr = RbcChainNextLink(linkPtr); linkPtr != NULL;
            linkPtr = RbcChainNextLink(linkPtr)) {
            stylePtr = RbcChainGetValue(linkPtr);
            Tcl_DStringStartSublist(&dString);
            Tcl_DStringAppendElement(&dString, stylePtr->penPtr->name);
            Tcl_PrintDouble(interp, stylePtr->weight.min, string);
            Tcl_DStringAppendElement(&dString, string);
            Tcl_PrintDouble(interp, stylePtr->weight.max, string);
            Tcl_DStringAppendElement(&dString, string);
            Tcl_DStringEndSublist(&dString);
        }
    }
    result = RbcStrdup(Tcl_DStringValue(&dString));
    *freeProcPtr = (Tcl_FreeProc *) Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcStyleMap --
 *
 *      Creates an array of style indices and fills it based on the weight
 *      of each data point.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is freed and allocated for the index array.
 *
 *----------------------------------------------------------------------
 */
RbcPenStyle **
RbcStyleMap(
    RbcElement * elemPtr)
{
register int i;
int nWeights;                  /* Number of weights to be examined.
                                * If there are more data points than
                                * weights, they will default to the
                                * normal pen. */

RbcPenStyle **dataToStyle;     /* Directory of styles.  Each array
                                * element represents the style for
                                * the data point at that index */
RbcChainLink *linkPtr;
RbcPenStyle *stylePtr;
double *w;                     /* Weight vector */
int nPoints;

    nPoints = RbcNumberOfPoints(elemPtr);
    nWeights = MIN(elemPtr->w.nValues, nPoints);
    w = elemPtr->w.valueArr;
    linkPtr = RbcChainFirstLink(elemPtr->palette);
    stylePtr = RbcChainGetValue(linkPtr);

    /*
     * Create a style mapping array (data point index to style),
     * initialized to the default style.
     */
    dataToStyle = (RbcPenStyle **) ckalloc(nPoints * sizeof(RbcPenStyle *));
    assert(dataToStyle);
    for(i = 0; i < nPoints; i++) {
        dataToStyle[i] = stylePtr;
    }

    for(i = 0; i < nWeights; i++) {
        for(linkPtr = RbcChainLastLink(elemPtr->palette); linkPtr != NULL;
            linkPtr = RbcChainPrevLink(linkPtr)) {
            stylePtr = RbcChainGetValue(linkPtr);

            if(stylePtr->weight.range > 0.0) {
double norm;

                norm = (w[i] - stylePtr->weight.min) / stylePtr->weight.range;
                if(((norm - 1.0) <= DBL_EPSILON) &&
                    (((1.0 - norm) - 1.0) <= DBL_EPSILON)) {
                    dataToStyle[i] = stylePtr;
                    break;      /* Done: found range that matches. */
                }
            }
        }
    }
    return dataToStyle;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcMapErrorBars --
 *
 *      Creates two arrays of points and pen indices, filled with
 *      the screen coordinates of the visible
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is freed and allocated for the index array.
 *
 *----------------------------------------------------------------------
 */
void
RbcMapErrorBars(
    RbcGraph * graph,
    RbcElement * elemPtr,
    RbcPenStyle ** dataToStyle)
{
int n, nPoints;
RbcExtents2D exts;
RbcPenStyle *stylePtr;

    RbcGraphExtents(graph, &exts);
    nPoints = RbcNumberOfPoints(elemPtr);
    if(elemPtr->xError.nValues > 0) {
        n = MIN(elemPtr->xError.nValues, nPoints);
    } else {
        n = MIN3(elemPtr->xHigh.nValues, elemPtr->xLow.nValues, nPoints);
    }
    if(n > 0) {
RbcSegment2D *errorBars;
RbcSegment2D *segPtr;
double high, low;
double x, y;
int *errorToData;
int *indexPtr;
register int i;

        segPtr = errorBars =
            (RbcSegment2D *) ckalloc(n * 3 * sizeof(RbcSegment2D));
        indexPtr = errorToData = (int *)ckalloc(n * 3 * sizeof(int));
        for(i = 0; i < n; i++) {
            x = elemPtr->x.valueArr[i];
            y = elemPtr->y.valueArr[i];
            stylePtr = dataToStyle[i];
            if((!TclIsInfinite(x)) && (!TclIsInfinite(y))) {
                if(elemPtr->xError.nValues > 0) {
                    high = x + elemPtr->xError.valueArr[i];
                    low = x - elemPtr->xError.valueArr[i];
                } else {
                    high = elemPtr->xHigh.valueArr[i];
                    low = elemPtr->xLow.valueArr[i];
                }
                if((!TclIsInfinite(high)) && (!TclIsInfinite(low))) {
RbcPoint2D p, q;

                    p = RbcMap2D(graph, high, y, &elemPtr->axes);
                    q = RbcMap2D(graph, low, y, &elemPtr->axes);
                    segPtr->p = p;
                    segPtr->q = q;
                    if(RbcLineRectClip(&exts, &segPtr->p, &segPtr->q)) {
                        segPtr++;
                        *indexPtr++ = i;
                    }
                    /* Left cap */
                    segPtr->p.x = segPtr->q.x = p.x;
                    segPtr->p.y = p.y - stylePtr->errorBarCapWidth;
                    segPtr->q.y = p.y + stylePtr->errorBarCapWidth;
                    if(RbcLineRectClip(&exts, &segPtr->p, &segPtr->q)) {
                        segPtr++;
                        *indexPtr++ = i;
                    }
                    /* Right cap */
                    segPtr->p.x = segPtr->q.x = q.x;
                    segPtr->p.y = q.y - stylePtr->errorBarCapWidth;
                    segPtr->q.y = q.y + stylePtr->errorBarCapWidth;
                    if(RbcLineRectClip(&exts, &segPtr->p, &segPtr->q)) {
                        segPtr++;
                        *indexPtr++ = i;
                    }
                }
            }
        }
        elemPtr->xErrorBars = errorBars;
        elemPtr->xErrorBarCnt = segPtr - errorBars;
        elemPtr->xErrorToData = errorToData;
    }
    if(elemPtr->yError.nValues > 0) {
        n = MIN(elemPtr->yError.nValues, nPoints);
    } else {
        n = MIN3(elemPtr->yHigh.nValues, elemPtr->yLow.nValues, nPoints);
    }
    if(n > 0) {
RbcSegment2D *errorBars;
RbcSegment2D *segPtr;
double high, low;
double x, y;
int *errorToData;
int *indexPtr;
register int i;

        segPtr = errorBars =
            (RbcSegment2D *) ckalloc(n * 3 * sizeof(RbcSegment2D));
        indexPtr = errorToData = (int *)ckalloc(n * 3 * sizeof(int));
        for(i = 0; i < n; i++) {
            x = elemPtr->x.valueArr[i];
            y = elemPtr->y.valueArr[i];
            stylePtr = dataToStyle[i];
            if((!TclIsInfinite(x)) && (!TclIsInfinite(y))) {
                if(elemPtr->yError.nValues > 0) {
                    high = y + elemPtr->yError.valueArr[i];
                    low = y - elemPtr->yError.valueArr[i];
                } else {
                    high = elemPtr->yHigh.valueArr[i];
                    low = elemPtr->yLow.valueArr[i];
                }
                if((!TclIsInfinite(high)) && (!TclIsInfinite(low))) {
RbcPoint2D p, q;

                    p = RbcMap2D(graph, x, high, &elemPtr->axes);
                    q = RbcMap2D(graph, x, low, &elemPtr->axes);
                    segPtr->p = p;
                    segPtr->q = q;
                    if(RbcLineRectClip(&exts, &segPtr->p, &segPtr->q)) {
                        segPtr++;
                        *indexPtr++ = i;
                    }
                    /* Top cap. */
                    segPtr->p.y = segPtr->q.y = p.y;
                    segPtr->p.x = p.x - stylePtr->errorBarCapWidth;
                    segPtr->q.x = p.x + stylePtr->errorBarCapWidth;
                    if(RbcLineRectClip(&exts, &segPtr->p, &segPtr->q)) {
                        segPtr++;
                        *indexPtr++ = i;
                    }
                    /* Bottom cap. */
                    segPtr->p.y = segPtr->q.y = q.y;
                    segPtr->p.x = q.x - stylePtr->errorBarCapWidth;
                    segPtr->q.x = q.x + stylePtr->errorBarCapWidth;
                    if(RbcLineRectClip(&exts, &segPtr->p, &segPtr->q)) {
                        segPtr++;
                        *indexPtr++ = i;
                    }
                }
            }
        }
        elemPtr->yErrorBars = errorBars;
        elemPtr->yErrorBarCnt = segPtr - errorBars;
        elemPtr->yErrorToData = errorToData;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetIndex --
 *
 *      Given a string representing the index of a pair of x,y
 *      coordinates, return the numeric index.
 *
 * Results:
 *      A standard TCL result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
GetIndex(
    Tcl_Interp * interp,
    RbcElement * elemPtr,
    const char *string,
    int *indexPtr)
{
    long ielem;
    int last;

    last = RbcNumberOfPoints(elemPtr) - 1;
    if((*string == 'e') && (strcmp("end", string) == 0)) {
        ielem = last;
    } else if(Tcl_ExprLong(interp, string, &ielem) != TCL_OK) {
        return TCL_ERROR;
    }
    *indexPtr = (int)ielem;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * NameToElement --
 *
 *      Find the element represented the given name,  returning
 *      a pointer to its data structure via elemPtrPtr.
 *
 * Results:
 *      A standard TCL result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
NameToElement(
    RbcGraph * graph,
    const char *name,
    RbcElement ** elemPtrPtr)
{
    Tcl_HashEntry *hPtr;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    if(name == NULL) {
        return TCL_ERROR;
    }
    hPtr = Tcl_FindHashEntry(&graph->elements.table, name);
    if(hPtr == NULL) {
        Tcl_AppendResult(graph->interp, "can't find element \"", name,
            "\" in \"", Tk_PathName(*(graph->win)), "\"", (char *)NULL);
        return TCL_ERROR;
    }
    *elemPtrPtr = (RbcElement *) Tcl_GetHashValue(hPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyElement --
 *
 *      Add a new element to the graph.
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void
DestroyElement(
    RbcGraph * graph,
    RbcElement * elemPtr)
{
RbcChainLink *linkPtr;

    RbcDeleteBindings(graph->bindTable, elemPtr);
    RbcLegendRemoveElement(graph->legend, elemPtr);

    Tk_FreeOptions(elemPtr->specsPtr, (char *)elemPtr, graph->display, 0);
    /*
     * Call the element's own destructor to release the memory and
     * resources allocated for it.
     */
    (*elemPtr->procsPtr->destroyProc) (graph, elemPtr);

    /* Remove it also from the element display list */
    for(linkPtr = RbcChainFirstLink(graph->elements.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        if(elemPtr == RbcChainGetValue(linkPtr)) {
            RbcChainDeleteLink(graph->elements.displayList, linkPtr);
            if(!elemPtr->hidden) {
                graph->flags |= RBC_RESET_WORLD;
                RbcEventuallyRedrawGraph(graph);
            }
            break;
        }
    }
    /* Remove the element for the graph's hash table of elements */
    if(elemPtr->hashPtr != NULL) {
        Tcl_DeleteHashEntry(elemPtr->hashPtr);
    }
    if(elemPtr->name != NULL) {
        ckfree((char *)elemPtr->name);
    }
    ckfree((char *)elemPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * CreateElement --
 *
 *      Add a new element to the graph.
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
CreateElement(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv,
    Tk_Uid classUid)
{
    RbcElement *elemPtr;
    Tcl_HashEntry *hPtr;
    int isNew;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    if(argv[3][0] == '-') {
        Tcl_AppendResult(graph->interp, "name of element \"", argv[3],
            "\" can't start with a '-'", (char *)NULL);
        return TCL_ERROR;
    }
    hPtr = Tcl_CreateHashEntry(&graph->elements.table, argv[3], &isNew);
    if(!isNew) {
        Tcl_AppendResult(interp, "element \"", argv[3],
            "\" already exists in \"", argv[0], "\"", (char *)NULL);
        return TCL_ERROR;
    }
    if(classUid == rbcBarElementUid) {
        elemPtr = RbcBarElement(graph, argv[3], classUid);
    } else {
        /* Stripcharts are line graphs with some options enabled. */
        elemPtr = RbcLineElement(graph, argv[3], classUid);
    }
    elemPtr->hashPtr = hPtr;
    Tcl_SetHashValue(hPtr, elemPtr);

    if(RbcConfigureWidgetComponent(interp, *(graph->win), elemPtr->name,
            "Element", elemPtr->specsPtr, argc - 4, argv + 4,
            (char *)elemPtr, 0) != TCL_OK) {
        DestroyElement(graph, elemPtr);
        return TCL_ERROR;
    }
    (*elemPtr->procsPtr->configProc) (graph, elemPtr);
    RbcChainPrepend(graph->elements.displayList, elemPtr);

    if(!elemPtr->hidden) {
        /* If the new element isn't hidden then redraw the graph.  */
        graph->flags |= RBC_REDRAW_BACKING_STORE;
        RbcEventuallyRedrawGraph(graph);
    }
    elemPtr->flags |= RBC_MAP_ITEM;
    graph->flags |= RBC_RESET_AXES;
    Tcl_SetObjResult(interp, Tcl_NewStringObj(elemPtr->name, -1));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RebuildDisplayList --
 *
 *      Given a Tcl list of element names, this procedure rebuilds the
 *      display list, ignoring invalid element names. This list describes
 *      not only only which elements to draw, but in what order.  This is
 *      only important for bar and pie charts.
 *
 * Results:
 *      The return value is a standard Tcl result.  Only if the Tcl list
 *      can not be split, a TCL_ERROR is returned and interp->result contains
 *      an error message.
 *
 * Side effects:
 *      The graph is eventually redrawn using the new display list.
 *
 *----------------------------------------------------------------------
 */
static int
RebuildDisplayList(
    RbcGraph * graph,          /* Graph widget record */
    const char *newList)
{              /* Tcl list of element names */
    int nNames;                /* Number of names found in Tcl name list */
    const char **nameArr;      /* Broken out array of element names */
    Tcl_HashSearch cursor;
    register int i;
    register Tcl_HashEntry *hPtr;
    RbcElement *elemPtr;       /* Element information record */

    if(Tcl_SplitList(graph->interp, newList, &nNames, &nameArr) != TCL_OK) {
        Tcl_AppendResult(graph->interp, "can't split name list \"", newList,
            "\"", (char *)NULL);
        return TCL_ERROR;
    }
    /* Clear the display list and mark all elements as hidden.  */
    RbcChainReset(graph->elements.displayList);
    for(hPtr = Tcl_FirstHashEntry(&graph->elements.table, &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        elemPtr = (RbcElement *) Tcl_GetHashValue(hPtr);
        elemPtr->hidden = TRUE;
    }

    /* Rebuild the display list, checking that each name it exists
     * (currently ignoring invalid element names).  */
    for(i = 0; i < nNames; i++) {
        if(NameToElement(graph, nameArr[i], &elemPtr) == TCL_OK) {
            elemPtr->hidden = FALSE;
            RbcChainAppend(graph->elements.displayList, elemPtr);
        }
    }
    ckfree((char *)nameArr);
    graph->flags |= RBC_RESET_WORLD;
    RbcEventuallyRedrawGraph(graph);
    Tcl_ResetResult(graph->interp);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcDestroyElements --
 *
 *      Removes all the graph's elements. This routine is called when
 *      the graph is destroyed.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory allocated for the graph's elements is freed.
 *
 *----------------------------------------------------------------------
 */
void
RbcDestroyElements(
    RbcGraph * graph)
{
Tcl_HashEntry *hPtr;
Tcl_HashSearch cursor;
RbcElement *elemPtr;

    for(hPtr = Tcl_FirstHashEntry(&graph->elements.table, &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        elemPtr = (RbcElement *) Tcl_GetHashValue(hPtr);
        elemPtr->hashPtr = NULL;
        DestroyElement(graph, elemPtr);
    }
    Tcl_DeleteHashTable(&graph->elements.table);
    Tcl_DeleteHashTable(&graph->elements.tagTable);
    RbcChainDestroy(graph->elements.displayList);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcMapElements --
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
void
RbcMapElements(
    RbcGraph * graph)
{
RbcElement *elemPtr;
RbcChainLink *linkPtr;

    if(graph->mode != MODE_INFRONT) {
        RbcResetStacks(graph);
    }
    for(linkPtr = RbcChainFirstLink(graph->elements.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        elemPtr = RbcChainGetValue(linkPtr);
        if(elemPtr->hidden) {
            continue;
        }
        if((graph->flags & RBC_MAP_ALL) || (elemPtr->flags & RBC_MAP_ITEM)) {
            (*elemPtr->procsPtr->mapProc) (graph, elemPtr);
            elemPtr->flags &= ~RBC_MAP_ITEM;
        }
    }
}

/*
 * -----------------------------------------------------------------
 *
 * RbcDrawElements --
 *
 *      Calls the individual element drawing routines for each
 *      element.
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      Elements are drawn into the drawable (pixmap) which will
 *      eventually be displayed in the graph window.
 *
 * -----------------------------------------------------------------
 */
void
RbcDrawElements(
    RbcGraph * graph,
    Drawable drawable)
{              /* Pixmap or window to draw into */
RbcChainLink *linkPtr;
RbcElement *elemPtr;

    for(linkPtr = RbcChainFirstLink(graph->elements.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        elemPtr = RbcChainGetValue(linkPtr);
        if(!elemPtr->hidden) {
            (*elemPtr->procsPtr->drawNormalProc) (graph, drawable, elemPtr);
        }
    }
}

/*
 * -----------------------------------------------------------------
 *
 * RbcDrawActiveElements --
 *
 *      Calls the individual element drawing routines to display
 *      the active colors for each element.
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      Elements are drawn into the drawable (pixmap) which will
 *      eventually be displayed in the graph window.
 *
 * -----------------------------------------------------------------
 */
void
RbcDrawActiveElements(
    RbcGraph * graph,
    Drawable drawable)
{              /* Pixmap or window to draw into */
RbcChainLink *linkPtr;
RbcElement *elemPtr;

    for(linkPtr = RbcChainFirstLink(graph->elements.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        elemPtr = RbcChainGetValue(linkPtr);
        if((!elemPtr->hidden) && (elemPtr->flags & RBC_ELEM_ACTIVE)) {
            (*elemPtr->procsPtr->drawActiveProc) (graph, drawable, elemPtr);
        }
    }
}

/*
 * -----------------------------------------------------------------
 *
 * RbcElementsToPostScript --
 *
 *      Generates PostScript output for each graph element in the
 *      element display list.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
void
RbcElementsToPostScript(
    RbcGraph * graph,
    RbcPsToken * psToken)
{
RbcChainLink *linkPtr;
RbcElement *elemPtr;

    for(linkPtr = RbcChainFirstLink(graph->elements.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        elemPtr = RbcChainGetValue(linkPtr);
        if(!elemPtr->hidden) {
            /* Comment the PostScript to indicate the start of the element */
            RbcFormatToPostScript(psToken, "\n%% Element \"%s\"\n\n",
                elemPtr->name);
            (*elemPtr->procsPtr->printNormalProc) (graph, psToken, elemPtr);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcActiveElementsToPostScript --
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
void
RbcActiveElementsToPostScript(
    RbcGraph * graph,
    RbcPsToken * psToken)
{
RbcChainLink *linkPtr;
RbcElement *elemPtr;

    for(linkPtr = RbcChainFirstLink(graph->elements.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        elemPtr = RbcChainGetValue(linkPtr);
        if((!elemPtr->hidden) && (elemPtr->flags & RBC_ELEM_ACTIVE)) {
            RbcFormatToPostScript(psToken, "\n%% Active Element \"%s\"\n\n",
                elemPtr->name);
            (*elemPtr->procsPtr->printActiveProc) (graph, psToken, elemPtr);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcGraphUpdateNeeded --
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
int
RbcGraphUpdateNeeded(
    RbcGraph * graph)
{
RbcChainLink *linkPtr;
RbcElement *elemPtr;

    for(linkPtr = RbcChainFirstLink(graph->elements.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        elemPtr = RbcChainGetValue(linkPtr);
        if(elemPtr->hidden) {
            continue;
        }
        /* Check if the x or y vectors have notifications pending */
        if((RbcVectorNotifyPending(elemPtr->x.clientId)) ||
            (RbcVectorNotifyPending(elemPtr->y.clientId))) {
            return 1;
        }
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * ActivateOp --
 *
 *      Marks data points of elements (given by their index) as active.
 *
 * Results:
 *      Returns TCL_OK if no errors occurred.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
ActivateOp(
    RbcGraph * graph,          /* Graph widget */
    Tcl_Interp * interp,       /* Interpreter to report errors to */
    int argc,                  /* Number of element names */
    const char **argv)
{              /* List of element names */
    RbcElement *elemPtr;
    register int i;
    int *activeArr;
    int nActiveIndices;

    if(argc == 3) {
    register Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;

        /* List all the currently active elements */
        for(hPtr = Tcl_FirstHashEntry(&graph->elements.table, &cursor);
            hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
            elemPtr = (RbcElement *) Tcl_GetHashValue(hPtr);
            if(elemPtr->flags & RBC_ELEM_ACTIVE) {
                Tcl_AppendElement(graph->interp, elemPtr->name);
            }
        }
        return TCL_OK;
    }
    if(NameToElement(graph, argv[3], &elemPtr) != TCL_OK) {
        return TCL_ERROR;       /* Can't find named element */
    }
    elemPtr->flags |= RBC_ELEM_ACTIVE | RBC_ACTIVE_PENDING;

    activeArr = NULL;
    nActiveIndices = -1;
    if(argc > 4) {
    register int *activePtr;

        nActiveIndices = argc - 4;
        activePtr = activeArr = (int *)ckalloc(sizeof(int) * nActiveIndices);
        assert(activeArr);
        for(i = 4; i < argc; i++) {
            if(GetIndex(interp, elemPtr, argv[i], activePtr) != TCL_OK) {
                return TCL_ERROR;
            }
            activePtr++;
        }
    }
    if(elemPtr->activeIndices != NULL) {
        ckfree((char *)elemPtr->activeIndices);
    }
    elemPtr->nActiveIndices = nActiveIndices;
    elemPtr->activeIndices = activeArr;
    RbcEventuallyRedrawGraph(graph);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcMakeElementTag --
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
ClientData
RbcMakeElementTag(
    RbcGraph * graph,
    const char *tagName)
{
Tcl_HashEntry *hPtr;
int isNew;

    hPtr = Tcl_CreateHashEntry(&graph->elements.tagTable, tagName, &isNew);
    assert(hPtr);
    return Tcl_GetHashKey(&graph->elements.tagTable, hPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * BindOp --
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
static int
BindOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    if(argc == 3) {
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    char *tagName;

        for(hPtr = Tcl_FirstHashEntry(&graph->elements.tagTable, &cursor);
            hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
            tagName = Tcl_GetHashKey(&graph->elements.tagTable, hPtr);
            Tcl_AppendElement(interp, tagName);
        }
        return TCL_OK;
    }
    return RbcConfigureBindings(interp, graph->bindTable,
        RbcMakeElementTag(graph, argv[3]), argc - 4, argv + 4);
}

/*
 *----------------------------------------------------------------------
 *
 * CreateOp --
 *
 *      Add a new element to the graph (using the default type of the
 *      graph).
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
CreateOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv,
    Tk_Uid type)
{
    return CreateElement(graph, interp, argc, argv, type);
}

/*
 *----------------------------------------------------------------------
 *
 * CgetOp --
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
static int
CgetOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    char *argv[])
{
    RbcElement *elemPtr;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    if(NameToElement(graph, argv[3], &elemPtr) != TCL_OK) {
        return TCL_ERROR;       /* Can't find named element */
    }
    if(Tk_ConfigureValue(interp, *(graph->win), elemPtr->specsPtr,
            (char *)elemPtr, argv[4], 0) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

static Tk_ConfigSpec closestSpecs[] = {
    {TK_CONFIG_CUSTOM, "-halo", (char *)NULL, (char *)NULL,
            (char *)NULL, Tk_Offset(RbcClosestSearch, halo), 0,
        &rbcDistanceOption},
    {TK_CONFIG_BOOLEAN, "-interpolate", (char *)NULL, (char *)NULL,
        (char *)NULL, Tk_Offset(RbcClosestSearch, mode), 0},
    {TK_CONFIG_CUSTOM, "-along", (char *)NULL, (char *)NULL,
        (char *)NULL, Tk_Offset(RbcClosestSearch, along), 0, &alongOption},
    {TK_CONFIG_END, (char *)NULL, (char *)NULL, (char *)NULL,
        (char *)NULL, 0, 0}
};

/*
 *----------------------------------------------------------------------
 *
 * ClosestOp --
 *
 *      Find the element closest to the specified screen coordinates.
 *      Options:
 *      -halo		Consider points only with this maximum distance
 *      		from the picked coordinate.
 *      -interpolate	Find closest point along element traces, not just
 *      		data points.
 *      -along
 *
 * Results:
 *      A standard Tcl result. If an element could be found within
 *      the halo distance, the interpreter result is "1", otherwise
 *      "0".  If a closest element exists, the designated Tcl array
 *      variable will be set with the following information:
 *
 *      1) the element name,
 *      2) the index of the closest point,
 *      3) the distance (in screen coordinates) from the picked X-Y
 *         coordinate and the closest point,
 *      4) the X coordinate (graph coordinate) of the closest point,
 *      5) and the Y-coordinate.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
ClosestOp(
    RbcGraph * graph,          /* Graph widget */
    Tcl_Interp * interp,       /* Interpreter to report results to */
    int argc,                  /* Number of element names */
    const char **argv)
{              /* List of element names */
    RbcElement *elemPtr;
    RbcClosestSearch search;
    int i, x, y;
    int flags = TCL_LEAVE_ERR_MSG;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    if(graph->flags & RBC_RESET_AXES) {
        RbcResetAxes(graph);
    }
    if(Tk_GetPixels(interp, *(graph->win), argv[3], &x) != TCL_OK) {
        Tcl_AppendResult(interp, ": bad window x-coordinate", (char *)NULL);
        return TCL_ERROR;
    }
    if(Tk_GetPixels(interp, *(graph->win), argv[4], &y) != TCL_OK) {
        Tcl_AppendResult(interp, ": bad window y-coordinate", (char *)NULL);
        return TCL_ERROR;
    }
    if(graph->inverted) {
    int temp;

        temp = x, x = y, y = temp;
    }
    for(i = 6; i < argc; i += 2) {      /* Count switches-value pairs */
        if((argv[i][0] != '-') || ((argv[i][1] == '-') && (argv[i][2] == '\0'))) {
            break;
        }
    }
    if(i > argc) {
        i = argc;
    }

    search.mode = RBC_SEARCH_POINTS;
    search.halo = graph->halo;
    search.index = -1;
    search.along = RBC_SEARCH_BOTH;
    search.x = x;
    search.y = y;

    if(Tk_ConfigureWidget(interp, *(graph->win), closestSpecs, i - 6,
            argv + 6, (char *)&search, TK_CONFIG_ARGV_ONLY) != TCL_OK) {
        return TCL_ERROR;       /* Error occurred processing an option. */
    }
    if((i < argc) && (argv[i][0] == '-')) {
        i++;   /* Skip "--" */
    }
    search.dist = (double)(search.halo + 1);

    if(i < argc) {
        for( /* empty */ ; i < argc; i++) {
            if(NameToElement(graph, argv[i], &elemPtr) != TCL_OK) {
                return TCL_ERROR;       /* Can't find named element */
            }
            if(elemPtr->hidden) {
                Tcl_AppendResult(interp, "element \"", argv[i], "\" is hidden",
                    (char *)NULL);
                return TCL_ERROR;       /* Element isn't visible */
            }
            /* Check if the X or Y vectors have notifications pending */
            if((elemPtr->flags & RBC_MAP_ITEM) ||
                (RbcVectorNotifyPending(elemPtr->x.clientId)) ||
                (RbcVectorNotifyPending(elemPtr->y.clientId))) {
                continue;
            }
            (*elemPtr->procsPtr->closestProc) (graph, elemPtr, &search);
        }
    } else {
    RbcChainLink *linkPtr;

        /*
         * Find the closest point from the set of displayed elements,
         * searching the display list from back to front.  That way if
         * the points from two different elements overlay each other
         * exactly, the last one picked will be the topmost.
         */
        for(linkPtr = RbcChainLastLink(graph->elements.displayList);
            linkPtr != NULL; linkPtr = RbcChainPrevLink(linkPtr)) {
            elemPtr = RbcChainGetValue(linkPtr);

            /* Check if the X or Y vectors have notifications pending */
            if((elemPtr->flags & RBC_MAP_ITEM) ||
                (RbcVectorNotifyPending(elemPtr->x.clientId)) ||
                (RbcVectorNotifyPending(elemPtr->y.clientId))) {
                continue;
            }
            if(!elemPtr->hidden) {
                (*elemPtr->procsPtr->closestProc) (graph, elemPtr, &search);
            }
        }

    }
    if(search.dist < (double)search.halo) {
    char string[200];
        /*
         *  Return an array of 5 elements
         */
        if(Tcl_SetVar2(interp, argv[5], "name",
                search.elemPtr->name, flags) == NULL) {
            return TCL_ERROR;
        }
        sprintf(string, "%d", search.index);
        if(Tcl_SetVar2(interp, argv[5], "index", string, flags) == NULL) {
            return TCL_ERROR;
        }
        Tcl_PrintDouble(interp, search.point.x, string);
        if(Tcl_SetVar2(interp, argv[5], "x", string, flags) == NULL) {
            return TCL_ERROR;
        }
        Tcl_PrintDouble(interp, search.point.y, string);
        if(Tcl_SetVar2(interp, argv[5], "y", string, flags) == NULL) {
            return TCL_ERROR;
        }
        Tcl_PrintDouble(interp, search.dist, string);
        if(Tcl_SetVar2(interp, argv[5], "dist", string, flags) == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, Tcl_NewIntObj(1));
    } else {
        if(Tcl_SetVar2(interp, argv[5], "name", "", flags) == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      Sets the element specifications by the given the command line
 *      arguments and calls the element specification configuration
 *      routine. If zero or one command line options are given, only
 *      information about the option(s) is returned in interp->result.
 *      If the element configuration has changed and the element is
 *      currently displayed, the axis limits are updated and
 *      recomputed.
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      Graph will be redrawn to reflect the new display list.
 *
 *----------------------------------------------------------------------
 */
static int
ConfigureOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char *argv[])
{
    RbcElement *elemPtr;
    int flags;
    int numNames, numOpts;
    const char **options;
    register int i;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    /* Figure out where the option value pairs begin */
    argc -= 3;
    argv += 3;
    for(i = 0; i < argc; i++) {
        if(argv[i][0] == '-') {
            break;
        }
        if(NameToElement(graph, argv[i], &elemPtr) != TCL_OK) {
            return TCL_ERROR;   /* Can't find named element */
        }
    }
    numNames = i;       /* Number of element names specified */
    numOpts = argc - i; /* Number of options specified */
    options = argv + numNames;  /* Start of options in argv  */

    for(i = 0; i < numNames; i++) {
        NameToElement(graph, argv[i], &elemPtr);
        flags = TK_CONFIG_ARGV_ONLY;
        if(numOpts == 0) {
            return Tk_ConfigureInfo(interp, *(graph->win),
                elemPtr->specsPtr, (char *)elemPtr, (char *)NULL, flags);
        } else if(numOpts == 1) {
            return Tk_ConfigureInfo(interp, *(graph->win),
                elemPtr->specsPtr, (char *)elemPtr, options[0], flags);
        }
        if(Tk_ConfigureWidget(interp, *(graph->win), elemPtr->specsPtr,
                numOpts, options, (char *)elemPtr, flags) != TCL_OK) {
            return TCL_ERROR;
        }
        if((*elemPtr->procsPtr->configProc) (graph, elemPtr) != TCL_OK) {
            return TCL_ERROR;   /* Failed to configure element */
        }
        if(RbcConfigModified(elemPtr->specsPtr, "-hide", (char *)NULL)) {
    RbcChainLink *linkPtr;

            for(linkPtr = RbcChainFirstLink(graph->elements.displayList);
                linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
                if(elemPtr == RbcChainGetValue(linkPtr)) {
                    break;
                }
            }
            if((elemPtr->hidden) != (linkPtr == NULL)) {

                /* The element's "hidden" variable is out of sync with
                 * the display list. [That's what you get for having
                 * two ways to do the same thing.]  This affects what
                 * elements are considered for axis ranges and
                 * displayed in the legend. Update the display list by
                 * either by adding or removing the element.  */

                if(linkPtr == NULL) {
                    RbcChainPrepend(graph->elements.displayList, elemPtr);
                } else {
                    RbcChainDeleteLink(graph->elements.displayList, linkPtr);
                }
            }
            graph->flags |= RBC_RESET_AXES;
            elemPtr->flags |= RBC_MAP_ITEM;
        }
        /* If data points or axes have changed, reset the axes (may
         * affect autoscaling) and recalculate the screen points of
         * the element. */

        if(RbcConfigModified(elemPtr->specsPtr, "-*data", "-map*", "-x",
                "-y", (char *)NULL)) {
            graph->flags |= RBC_RESET_WORLD;
            elemPtr->flags |= RBC_MAP_ITEM;
        }
        /* The new label may change the size of the legend */
        if(RbcConfigModified(elemPtr->specsPtr, "-label", (char *)NULL)) {
            graph->flags |= (RBC_MAP_WORLD | RBC_REDRAW_WORLD);
        }
    }
    /* Update the pixmap if any configuration option changed */
    graph->flags |= (RBC_REDRAW_BACKING_STORE | RBC_DRAW_MARGINS);
    RbcEventuallyRedrawGraph(graph);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DeactivateOp --
 *
 *      Clears the active bit for the named elements.
 *
 * Results:
 *      Returns TCL_OK if no errors occurred.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
DeactivateOp(
    RbcGraph * graph,          /* Graph widget */
    Tcl_Interp * interp,       /* Not used. */
    int argc,                  /* Number of element names */
    const char **argv)
{              /* List of element names */
    RbcElement *elemPtr;
    register int i;

    for(i = 3; i < argc; i++) {
        if(NameToElement(graph, argv[i], &elemPtr) != TCL_OK) {
            return TCL_ERROR;   /* Can't find named element */
        }
        elemPtr->flags &= ~RBC_ELEM_ACTIVE;
        if(elemPtr->activeIndices != NULL) {
            ckfree((char *)elemPtr->activeIndices);
            elemPtr->activeIndices = NULL;
        }
        elemPtr->nActiveIndices = 0;
    }
    RbcEventuallyRedrawGraph(graph);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DeleteOp --
 *
 *      Delete the named elements from the graph.
 *
 * Results:
 *      TCL_ERROR is returned if any of the named elements can not be
 *      found.  Otherwise TCL_OK is returned;
 *
 * Side Effects:
 *      If the element is currently displayed, the plotting area of
 *      the graph is redrawn. Memory and resources allocated by the
 *      elements are released.
 *
 *----------------------------------------------------------------------
 */
static int
DeleteOp(
    RbcGraph * graph,          /* Graph widget */
    Tcl_Interp * interp,       /* Not used. */
    int argc,                  /* Number of element names */
    const char **argv)
{              /* List of element names */
    RbcElement *elemPtr;
    register int i;

    for(i = 3; i < argc; i++) {
        if(NameToElement(graph, argv[i], &elemPtr) != TCL_OK) {
            return TCL_ERROR;   /* Can't find named element */
        }
        DestroyElement(graph, elemPtr);
    }
    RbcEventuallyRedrawGraph(graph);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ExistsOp --
 *
 *      Indicates if the named element exists in the graph.
 *
 * Results:
 *      The return value is a standard Tcl result.  The interpreter
 *      result will contain "1" or "0".
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
ExistsOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,                  /* Not used. */
    const char **argv)
{
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&graph->elements.table, argv[3]);
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj((hPtr != NULL)));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GetOp --
 *
 *      Returns the name of the picked element (using the element
 *      bind operation).  Right now, the only name accepted is
 *      "current".
 *
 * Results:
 *      A standard Tcl result.  The interpreter result will contain
 *      the name of the element.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
GetOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,                  /* Not used. */
    const char *argv[])
{
    register RbcElement *elemPtr;

    if((argv[3][0] == 'c') && (strcmp(argv[3], "current") == 0)) {
        elemPtr = (RbcElement *) RbcGetCurrentItem(graph->bindTable);
        /* Report only on elements. */
        if((elemPtr != NULL) &&
            ((elemPtr->classUid == rbcBarElementUid) ||
                (elemPtr->classUid == rbcLineElementUid) ||
                (elemPtr->classUid == rbcStripElementUid))) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(elemPtr->name, -1));
        }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * NamesOp --
 *
 *      Returns the names of the elements is the graph matching
 *      one of more patterns provided.  If no pattern arguments
 *      are given, then all element names will be returned.
 *
 * Results:
 *      The return value is a standard Tcl result. The interpreter
 *      result will contain a Tcl list of the element names.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
NamesOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcElement *elemPtr;
    Tcl_HashSearch cursor;
    register Tcl_HashEntry *hPtr;
    register int i;

    for(hPtr = Tcl_FirstHashEntry(&graph->elements.table, &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        elemPtr = (RbcElement *) Tcl_GetHashValue(hPtr);
        if(argc == 3) {
            Tcl_AppendElement(graph->interp, elemPtr->name);
            continue;
        }
        for(i = 3; i < argc; i++) {
            if(Tcl_StringMatch(elemPtr->name, argv[i])) {
                Tcl_AppendElement(interp, elemPtr->name);
                break;
            }
        }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ShowOp --
 *
 *      Queries or resets the element display list.
 *
 * Results:
 *      The return value is a standard Tcl result. The interpreter
 *      result will contain the new display list of element names.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
ShowOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcElement *elemPtr;
    RbcChainLink *linkPtr;

    if(argc == 4) {
        if(RebuildDisplayList(graph, argv[3]) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    for(linkPtr = RbcChainFirstLink(graph->elements.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        elemPtr = RbcChainGetValue(linkPtr);
        Tcl_AppendElement(interp, elemPtr->name);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TypeOp --
 *
 *      Returns the name of the type of the element given by some
 *      element name.
 *
 * Results:
 *      A standard Tcl result. Returns the type of the element in
 *      interp->result. If the identifier given doesn't represent an
 *      element, then an error message is left in interp->result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
TypeOp(
    RbcGraph * graph,          /* Graph widget */
    Tcl_Interp * interp,
    int argc,                  /* Not used. */
    const char **argv)
{              /* Element name */
    RbcElement *elemPtr;

    if(NameToElement(graph, argv[3], &elemPtr) != TCL_OK) {
        return TCL_ERROR;       /* Can't find named element */
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(elemPtr->classUid, -1));
    return TCL_OK;
}

/*
 * Global routines:
 */
static RbcOpSpec elemOps[] = {
    {"activate", 1, (RbcOp) ActivateOp, 3, 0, "?elemName? ?index...?",},
    {"bind", 1, (RbcOp) BindOp, 3, 6, "elemName sequence command",},
    {"cget", 2, (RbcOp) CgetOp, 5, 5, "elemName option",},
    {"closest", 2, (RbcOp) ClosestOp, 6, 0,
        "x y varName ?option value?... ?elemName?...",},
    {"configure", 2, (RbcOp) ConfigureOp, 4, 0,
        "elemName ?elemName?... ?option value?...",},
    {"create", 2, (RbcOp) CreateOp, 4, 0, "elemName ?option value?...",},
    {"deactivate", 3, (RbcOp) DeactivateOp, 3, 0, "?elemName?...",},
    {"delete", 3, (RbcOp) DeleteOp, 3, 0, "?elemName?...",},
    {"exists", 1, (RbcOp) ExistsOp, 4, 4, "elemName",},
    {"get", 1, (RbcOp) GetOp, 4, 4, "name",},
    {"names", 1, (RbcOp) NamesOp, 3, 0, "?pattern?...",},
    {"show", 1, (RbcOp) ShowOp, 3, 4, "?elemList?",},
    {"type", 1, (RbcOp) TypeOp, 4, 4, "elemName",},
};

static int numElemOps = sizeof(elemOps) / sizeof(RbcOpSpec);

/*
 * ----------------------------------------------------------------
 *
 * RbcElementOp --
 *
 *      This procedure is invoked to process the Tcl command that
 *      corresponds to a widget managed by this module.  See the user
 *      documentation for details on what it does.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 * ----------------------------------------------------------------
 */
int
RbcElementOp(
    RbcGraph * graph,          /* Graph widget record */
    Tcl_Interp * interp,
    int argc,                  /* # arguments */
    const char **argv,         /* Argument list */
    Tk_Uid type)
{
    RbcOp proc;
    int result;

    proc = RbcGetOp(interp, numElemOps, elemOps, RBC_OP_ARG2, argc, argv, 0);
    if(proc == NULL) {
        return TCL_ERROR;
    }
    if(proc == CreateOp) {
        result = CreateOp(graph, interp, argc, argv, type);
    } else {
        result = (*proc) (graph, interp, argc, argv);
    }
    return result;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
