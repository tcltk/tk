/*
 * tkPathGradient.c --
 *
 *    This file implements gradient objects used when drawing paths.
 *      See http://www.w3.org/TR/SVG11/.
 *
 * Copyright (c) 2005-2008  Mats Bengtsson
 *
 * TODO: o Add tkwin option here and there so we can free stop colors!
 *
 */

#include "tkPathInt.h"

/*
 * Per Tcl_Interp data.
 */

typedef struct {
    Tcl_HashTable gradientHash;
    Tk_OptionTable linearOptionTable;
    Tk_OptionTable radialOptionTable;
    int gradientNameUid;
} InterpData;

static const char *gradientCmds[] = {
    "cget", "configure", "create", "delete", "inuse", "names", "type",
    (char *) NULL
};

enum {
    kPathGradientCmdCget    = 0L,
    kPathGradientCmdConfigure,
    kPathGradientCmdCreate,
    kPathGradientCmdDelete,
    kPathGradientCmdInUse,
    kPathGradientCmdNames,
    kPathGradientCmdType
};

static int     GradientObjCmd(ClientData clientData, Tcl_Interp* interp,
            int objc, Tcl_Obj* const objv[]);
static void     GradientInterpDeleted(ClientData clientData);
static int    PathGradientCget(Tcl_Interp *interp, Tk_Window tkwin,
            int objc, Tcl_Obj * const objv[],
            Tcl_HashTable *hashTablePtr);
static int    PathGradientConfigure(Tcl_Interp *interp, Tk_Window tkwin,
            int objc, Tcl_Obj * const objv[],
            Tcl_HashTable *hashTablePtr);
static int    PathGradientCreate(Tcl_Interp *interp, Tk_Window tkwin,
            int objc, Tcl_Obj * const objv[],
            Tcl_HashTable *hashTablePtr, char *tokenName);
static int    PathGradientDelete(Tcl_Interp *interp, Tcl_Obj *obj,
            Tcl_HashTable *hashTablePtr);
static int    PathGradientInUse(Tcl_Interp *interp, Tcl_Obj *obj,
            Tcl_HashTable *tablePtr);
static void   PathGradientNames(Tcl_Interp *interp,
            Tcl_HashTable *hashTablePtr);
static int    PathGradientType(Tcl_Interp *interp, Tcl_Obj *obj,
            Tcl_HashTable *hashTablePtr);
static void   PathGradientMasterFree(TkPathGradientMaster *gradientPtr);

/*
 *----------------------------------------------------------------------
 *
 * TkPathCanvasGradientObjCmd --
 *
 *    Implements the 'pathName gradient' command using the canvas local state.
 *
 * Results:
 *    Standard Tcl result
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */

int
TkPathCanvasGradientObjCmd(Tcl_Interp* interp, TkPathCanvas *canvasPtr,
    int objc, Tcl_Obj* const objv[])
{
    int index;
    int result = TCL_OK;

    /*
     * objv[2] is the subcommand: cget | configure | create | delete | names | type
     */
    if (objc < 3) {
    Tcl_WrongNumArgs(interp, 2, objv, "command ?arg arg...?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[2], gradientCmds, "command", 0,
        &index) != TCL_OK) {
        return TCL_ERROR;
    }
    switch (index) {

        case kPathGradientCmdCget: {
        if (objc != 5) {
        Tcl_WrongNumArgs(interp, 3, objv, "name option");
        return TCL_ERROR;
        }
        result = PathGradientCget(interp, canvasPtr->tkwin, objc-3, objv+3,
            &canvasPtr->gradientTable);
            break;
        }

        case kPathGradientCmdConfigure: {
        if (objc < 4) {
        Tcl_WrongNumArgs(interp, 3, objv, "name ?option? ?value option value...?");
        return TCL_ERROR;
        }
        result = PathGradientConfigure(interp, canvasPtr->tkwin, objc-3, objv+3,
            &canvasPtr->gradientTable);
            break;
        }

        case kPathGradientCmdCreate: {
        char str[255];

        if (objc < 4) {
        Tcl_WrongNumArgs(interp, 3, objv, "type ?option value...?");
        return TCL_ERROR;
        }
            sprintf(str, "%s%d", TK_PATHCMD_GRADIENT, canvasPtr->gradientUid++);
        result = PathGradientCreate(interp, canvasPtr->tkwin, objc-3, objv+3,
            &canvasPtr->gradientTable, str);
            break;
        }

        case kPathGradientCmdDelete: {
        if (objc != 4) {
        Tcl_WrongNumArgs(interp, 3, objv, "name");
        return TCL_ERROR;
        }
        result = PathGradientDelete(interp, objv[3], &canvasPtr->gradientTable);
        break;
        }

    case kPathGradientCmdInUse: {
        if (objc != 4) {
        Tcl_WrongNumArgs(interp, 3, objv, "name");
        return TCL_ERROR;
        }
        result = PathGradientInUse(interp, objv[3], &canvasPtr->gradientTable);
        break;
    }

        case kPathGradientCmdNames: {
        if (objc != 3) {
        Tcl_WrongNumArgs(interp, 3, objv, NULL);
        return TCL_ERROR;
        }
        PathGradientNames(interp, &canvasPtr->gradientTable);
            break;
        }

        case kPathGradientCmdType: {
        if (objc != 4) {
        Tcl_WrongNumArgs(interp, 3, objv, "name");
        return TCL_ERROR;
        }
        result = PathGradientType(interp, objv[3], &canvasPtr->gradientTable);
            break;
        }
    }
    return result;
}

/*
 * TkPathCanvasGradientsFree --
 *
 *    Used by canvas Destroy handler to clean up all gradients.
 *    Note that items clean up all their gradient instances themeselves.
 */
void
TkPathCanvasGradientsFree(TkPathCanvas *canvasPtr)
{
    Tcl_HashEntry   *hPtr;
    Tcl_HashSearch  search;
    TkPathGradientMaster *gradientPtr = NULL;

    hPtr = Tcl_FirstHashEntry(&canvasPtr->gradientTable, &search);
    while (hPtr != NULL) {
    gradientPtr = (TkPathGradientMaster*) Tcl_GetHashValue(hPtr);
    Tcl_DeleteHashEntry(hPtr);
    PathGradientMasterFree(gradientPtr);
    hPtr = Tcl_NextHashEntry(&search);
    }
}

/*
 * Custom option processing code.
 */

static char *
ComputeSlotAddress(
    char *recordPtr,    /* Pointer to the start of a record. */
    int offset)        /* Offset of a slot within that record; may be < 0. */
{
    if (offset >= 0) {
        return recordPtr + offset;
    } else {
        return NULL;
    }
}

/*
 * Procedures for processing the transition option of the linear gradient fill.
 */

static int LinTransitionSet(
    ClientData clientData,
    Tcl_Interp *interp,        /* Current interp; may be used for errors. */
    Tk_Window tkwin,        /* Window for which option is being set. */
    Tcl_Obj **value,        /* Pointer to the pointer to the value object.
                 * We use a pointer to the pointer because
                 * we may need to return a value (NULL). */
    char *recordPtr,        /* Pointer to storage for the widget record. */
    int internalOffset,        /* Offset within *recordPtr at which the
                 * internal value is to be stored. */
    char *oldInternalPtr,    /* Pointer to storage for the old value. */
    int flags)            /* Flags for the option, set Tk_SetOptions. */
{
    char *internalPtr;
    int objEmpty = 0;
    Tcl_Obj *valuePtr;
    double z[4] = {0.0, 0.0, 1.0, 0.0};        /* Defaults according to SVG. */
    TkPathRect *newrc = NULL;

    valuePtr = *value;
    internalPtr = ComputeSlotAddress(recordPtr, internalOffset);
    objEmpty = TkPathObjectIsEmpty(valuePtr);

    /*
     * Important: the new value for the transition is not yet
     * stored into the style! transObj may be NULL!
     * The new value is stored in style *after* we return TCL_OK.
     */
    if ((flags & TK_OPTION_NULL_OK) && objEmpty) {
        valuePtr = NULL;
    } else {
        int i, len;
        Tcl_Obj **objv;

        if (Tcl_ListObjGetElements(interp, valuePtr, &len, &objv) != TCL_OK) {
            return TCL_ERROR;
        }
        if (len != 4) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(
                    "-lineartransition must have four elements", -1));
            return TCL_ERROR;
        }
        for (i = 0; i < 4; i++) {
            if (Tcl_GetDoubleFromObj(interp, objv[i], z+i) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        newrc = (TkPathRect *) ckalloc(sizeof(TkPathRect));
        newrc->x1 = z[0];
        newrc->y1 = z[1];
        newrc->x2 = z[2];
        newrc->y2 = z[3];
    }
    if (internalPtr != NULL) {
        *((TkPathRect **) oldInternalPtr) = *((TkPathRect **) internalPtr);
        *((TkPathRect **) internalPtr) = newrc;
    }
    return TCL_OK;
}

static void
LinTransitionRestore(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr,        /* Pointer to storage for value. */
    char *oldInternalPtr)    /* Pointer to old value. */
{
    *(TkPathRect **)internalPtr = *(TkPathRect **)oldInternalPtr;
}

static void
LinTransitionFree(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr)        /* Pointer to storage for value. */
{
    if (*((char **) internalPtr) != NULL) {
        ckfree(*((char **) internalPtr));
        *((char **) internalPtr) = NULL;
    }
}

static Tk_ObjCustomOption linTransitionCO =
{
    "lineartransition",
    LinTransitionSet,
    NULL,
    LinTransitionRestore,
    LinTransitionFree,
    (ClientData) NULL
};

static int RadTransitionSet(
    ClientData clientData,
    Tcl_Interp *interp,        /* Current interp; may be used for errors. */
    Tk_Window tkwin,        /* Window for which option is being set. */
    Tcl_Obj **value,        /* Pointer to the pointer to the value object.
                             * We use a pointer to the pointer because
                             * we may need to return a value (NULL). */
    char *recordPtr,        /* Pointer to storage for the widget record. */
    int internalOffset,        /* Offset within *recordPtr at which the
                               internal value is to be stored. */
    char *oldInternalPtr,   /* Pointer to storage for the old value. */
    int flags)            /* Flags for the option, set Tk_SetOptions. */
{
    char *internalPtr;
    int objEmpty = 0;
    Tcl_Obj *valuePtr;
    double z[5] = {0.5, 0.5, 0.5, 0.5, 0.5};
    TkRadialTransition *newrc = NULL;

    valuePtr = *value;
    internalPtr = ComputeSlotAddress(recordPtr, internalOffset);
    objEmpty = TkPathObjectIsEmpty(valuePtr);

    if ((flags & TK_OPTION_NULL_OK) && objEmpty) {
        valuePtr = NULL;
    } else {
        int i, len;
        Tcl_Obj **objv;

        if (Tcl_ListObjGetElements(interp, valuePtr, &len, &objv) != TCL_OK) {
            return TCL_ERROR;
        }
        if ((len == 1) || (len == 4) || (len > 5)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(
                    "-radialtransition must be a list {cx cy ?r? ?fx fy?}", -1));
            return TCL_ERROR;
        }
        for (i = 0; i < len; i++) {
            if (Tcl_GetDoubleFromObj(interp, objv[i], z+i) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        newrc = (TkRadialTransition *) ckalloc(sizeof(TkRadialTransition));
        newrc->centerX = z[0];
        newrc->centerY = z[1];
        newrc->radius = z[2];
        newrc->focalX = z[3];
        newrc->focalY = z[4];
    }
    if (internalPtr != NULL) {
        *((TkRadialTransition **) oldInternalPtr) = *((TkRadialTransition **) internalPtr);
        *((TkRadialTransition **) internalPtr) = newrc;
    }
    return TCL_OK;
}

static void
RadTransitionRestore(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr,        /* Pointer to storage for value. */
    char *oldInternalPtr)    /* Pointer to old value. */
{
    *(TkRadialTransition **)internalPtr = *(TkRadialTransition **)oldInternalPtr;
}

static void
RadTransitionFree(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr)        /* Pointer to storage for value. */
{
    if (*((char **) internalPtr) != NULL) {
        ckfree(*((char **) internalPtr));
        *((char **) internalPtr) = NULL;
    }
}

static Tk_ObjCustomOption radTransitionCO =
{
    "radialtransition",
    RadTransitionSet,
    NULL,
    RadTransitionRestore,
    RadTransitionFree,
    (ClientData) NULL
};

static TkGradientStop *
NewGradientStop(double offset, XColor *color, double opacity)
{
    TkGradientStop *stopPtr;

    stopPtr = (TkGradientStop *) ckalloc(sizeof(TkGradientStop));
    memset(stopPtr, '\0', sizeof(TkGradientStop));
    stopPtr->offset = offset;
    stopPtr->color = color;
    stopPtr->opacity = opacity;
    return stopPtr;
}

static TkGradientStopArray *
NewGradientStopArray(int nstops)
{
    TkGradientStopArray *stopArrPtr;
    TkGradientStop **stops;

    stopArrPtr = (TkGradientStopArray *) ckalloc(sizeof(TkGradientStopArray));
    memset(stopArrPtr, '\0', sizeof(TkGradientStopArray));

    /* Array of *pointers* to TkGradientStop. */
    stops = (TkGradientStop **) ckalloc(nstops*sizeof(TkGradientStop *));
    memset(stops, '\0', nstops*sizeof(TkGradientStop *));
    stopArrPtr->nstops = nstops;
    stopArrPtr->stops = stops;
    return stopArrPtr;
}

static void
FreeAllStops(TkGradientStop **stops, int nstops)
{
    int i;
    for (i = 0; i < nstops; i++) {
        if (stops[i] != NULL) {
            /* @@@ Free color? */
            ckfree((char *) (stops[i]));
        }
    }
    ckfree((char *) stops);
}

static void
FreeStopArray(TkGradientStopArray *stopArrPtr)
{
    if (stopArrPtr != NULL) {
        FreeAllStops(stopArrPtr->stops, stopArrPtr->nstops);
        ckfree((char *) stopArrPtr);
    }
}

/*
 * The stops are a list of stop lists where each stop list is:
 *        {offset color ?opacity?}
 */
static int
StopsSet(
    ClientData clientData,
    Tcl_Interp *interp,        /* Current interp; may be used for errors. */
    Tk_Window tkwin,        /* Window for which option is being set. */
    Tcl_Obj **value,        /* Pointer to the pointer to the value object.
                             * We use a pointer to the pointer because
                             * we may need to return a value (NULL). */
    char *recordPtr,        /* Pointer to storage for the widget record. */
    int internalOffset,        /* Offset within *recordPtr at which the
                               internal value is to be stored. */
    char *oldInternalPtr,    /* Pointer to storage for the old value. */
    int flags)                /* Flags for the option, set Tk_SetOptions. */
{
    char *internalPtr;
    int i, nstops, stopLen;
    int objEmpty = 0;
    Tcl_Obj *valuePtr;
    double offset, lastOffset, opacity;
    Tcl_Obj **objv;
    Tcl_Obj *stopObj;
    Tcl_Obj *obj;
    XColor *color;
    TkGradientStopArray *newrc = NULL;

    valuePtr = *value;
    internalPtr = ComputeSlotAddress(recordPtr, internalOffset);
    objEmpty = TkPathObjectIsEmpty(valuePtr);

    if ((flags & TK_OPTION_NULL_OK) && objEmpty) {
        valuePtr = NULL;
    } else {

        /* Deal with each stop list in turn. */
        if (Tcl_ListObjGetElements(interp, valuePtr, &nstops, &objv) != TCL_OK) {
            return TCL_ERROR;
        }
        newrc = NewGradientStopArray(nstops);
        lastOffset = 0.0;

        for (i = 0; i < nstops; i++) {
            stopObj = objv[i];
            if (Tcl_ListObjLength(interp, stopObj, &stopLen) != TCL_OK) {
                goto error;
            }
            if ((stopLen == 2) || (stopLen == 3)) {
                Tcl_ListObjIndex(interp, stopObj, 0, &obj);
                if (Tcl_GetDoubleFromObj(interp, obj, &offset) != TCL_OK) {
                    goto error;
                }
                if ((offset < 0.0) || (offset > 1.0)) {
                    Tcl_SetObjResult(interp, Tcl_NewStringObj(
                            "stop offsets must be in the range 0.0 to 1.0", -1));
                    goto error;
                }
                if (offset < lastOffset) {
                    Tcl_SetObjResult(interp, Tcl_NewStringObj(
                            "stop offsets must be ordered", -1));
                    goto error;
                }
                Tcl_ListObjIndex(interp, stopObj, 1, &obj);
                color = Tk_AllocColorFromObj(interp, Tk_MainWindow(interp), obj);
                if (color == NULL) {
                    Tcl_AppendResult(interp, "color \"",
                            Tcl_GetStringFromObj(obj, NULL),
                            "\" doesn't exist", NULL);
                    goto error;
                }
                if (stopLen == 3) {
                    Tcl_ListObjIndex(interp, stopObj, 2, &obj);
                    if (Tcl_GetDoubleFromObj(interp, obj, &opacity) != TCL_OK) {
                        goto error;
                    }
                } else {
                    opacity = 1.0;
                }

                /* Make new stop. */
                newrc->stops[i] = NewGradientStop(offset, color, opacity);
                lastOffset = offset;
            } else {
                Tcl_SetObjResult(interp, Tcl_NewStringObj(
                        "stop list not {offset color ?opacity?}", -1));
                goto error;
            }
        }
    }
    if (internalPtr != NULL) {
        *((TkGradientStopArray **) oldInternalPtr) = *((TkGradientStopArray **) internalPtr);
        *((TkGradientStopArray **) internalPtr) = newrc;
    }
    return TCL_OK;

error:
    if (newrc != NULL) {
        FreeStopArray(newrc);
    }
    return TCL_ERROR;
}

static void
StopsRestore(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr,        /* Pointer to storage for value. */
    char *oldInternalPtr)    /* Pointer to old value. */
{
    *(TkGradientStopArray **)internalPtr = *(TkGradientStopArray **)oldInternalPtr;
}

static void
StopsFree(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr)
{
    if (*((char **) internalPtr) != NULL) {
        FreeStopArray(*(TkGradientStopArray **)internalPtr);
    }
}

static Tk_ObjCustomOption stopsCO =
{
    "stops",
    StopsSet,
    NULL,
    StopsRestore,
    StopsFree,
    (ClientData) NULL
};

/*
 * The following table defines the legal values for the -method option.
 * The enum TK_PATH_GRADIENTMETHOD_Pad... MUST be kept in sync!
 */

static const char *methodST[] = {
    "pad", "repeat", "reflect", NULL
};
static const char *unitsST[] = {
    "bbox", "userspace", NULL
};

TK_PATH_STYLE_CUSTOM_OPTION_MATRIX

static Tk_OptionSpec linGradientStyleOptionSpecs[] = {
    {TK_OPTION_STRING_TABLE, "-method", NULL, NULL,
        "pad", -1, Tk_Offset(TkPathGradientMaster, linearFill.method),
        0, (ClientData) methodST, 0},
    {TK_OPTION_STRING_TABLE, "-units", NULL, NULL,
        "bbox", -1, Tk_Offset(TkPathGradientMaster, linearFill.units),
        0, (ClientData) unitsST, 0},
    {TK_OPTION_CUSTOM, "-stops", NULL, NULL,
    NULL, Tk_Offset(TkPathGradientMaster, stopsObj),
        Tk_Offset(TkPathGradientMaster, linearFill.stopArrPtr),
    TK_OPTION_NULL_OK, (ClientData) &stopsCO, 0},
    {TK_OPTION_CUSTOM, "-lineartransition", NULL, NULL,
    NULL, Tk_Offset(TkPathGradientMaster, transObj),
    Tk_Offset(TkPathGradientMaster, linearFill.transitionPtr),
    TK_OPTION_NULL_OK, (ClientData) &linTransitionCO, 0},
    {TK_OPTION_CUSTOM, "-matrix", NULL, NULL,
    NULL, -1, Tk_Offset(TkPathGradientMaster, matrixPtr),
    TK_OPTION_NULL_OK, (ClientData) &matrixCO, 0},
    {TK_OPTION_END, NULL, NULL, NULL,
    NULL, 0, -1, 0, (ClientData) NULL, 0}
};

static Tk_OptionSpec radGradientStyleOptionSpecs[] = {
    {TK_OPTION_STRING_TABLE, "-method", NULL, NULL,
        "pad", -1, Tk_Offset(TkPathGradientMaster, radialFill.method),
        0, (ClientData) methodST, 0},
    {TK_OPTION_STRING_TABLE, "-units", NULL, NULL,
        "bbox", -1, Tk_Offset(TkPathGradientMaster, radialFill.units),
        0, (ClientData) unitsST, 0},
    {TK_OPTION_CUSTOM, "-stops", NULL, NULL,
    NULL, Tk_Offset(TkPathGradientMaster, stopsObj),
    Tk_Offset(TkPathGradientMaster, radialFill.stopArrPtr),
    TK_OPTION_NULL_OK, (ClientData) &stopsCO, 0},
    {TK_OPTION_CUSTOM, "-radialtransition", NULL, NULL,
    NULL, Tk_Offset(TkPathGradientMaster, transObj),
        Tk_Offset(TkPathGradientMaster, radialFill.radialPtr),
    TK_OPTION_NULL_OK, (ClientData) &radTransitionCO, 0},
    {TK_OPTION_CUSTOM, "-matrix", NULL, NULL,
    NULL, -1, Tk_Offset(TkPathGradientMaster, matrixPtr),
    TK_OPTION_NULL_OK, (ClientData) &matrixCO, 0},
    {TK_OPTION_END, NULL, NULL, NULL,
    NULL, 0, -1, 0, (ClientData) NULL, 0}
};

void
TkPathGradientPaint(TkPathContext ctx, TkPathRect *bbox,
    TkPathGradientMaster *gradientPtr, int fillRule, double fillOpacity)
{
    if (!TkPathObjectIsEmpty(gradientPtr->stopsObj)) {
    if (gradientPtr->type == TK_PATH_GRADIENTTYPE_LINEAR) {
        TkPathPaintLinearGradient(ctx, bbox, &gradientPtr->linearFill,
                fillRule, fillOpacity, gradientPtr->matrixPtr);
    } else {
        TkPathPaintRadialGradient(ctx, bbox, &gradientPtr->radialFill,
                   fillRule, fillOpacity, gradientPtr->matrixPtr);
    }
    }
}

void
TkPathGradientInit(Tcl_Interp* interp)
{
    InterpData             *dataPtr =
    (InterpData *) Tcl_GetAssocData(interp, TK_PATHCMD_PATHGRADIENT, NULL);

    if (dataPtr == NULL) {
    dataPtr = (InterpData *) ckalloc(sizeof (InterpData));
    Tcl_InitHashTable(&dataPtr->gradientHash, TCL_STRING_KEYS);
    dataPtr->linearOptionTable =
        Tk_CreateOptionTable(interp, linGradientStyleOptionSpecs);
    dataPtr->radialOptionTable =
        Tk_CreateOptionTable(interp, radGradientStyleOptionSpecs);
    dataPtr->gradientNameUid = 0;
    Tcl_SetAssocData(interp, TK_PATHCMD_PATHGRADIENT,
             (Tcl_InterpDeleteProc *) GradientInterpDeleted,
             (ClientData) dataPtr);
    }
    Tcl_CreateObjCommand(interp, TK_PATHCMD_PATHGRADIENT,
             GradientObjCmd, (ClientData) dataPtr,
             (Tcl_CmdDeleteProc *) NULL);
}

static int
FindGradientMaster(Tcl_Interp *interp, Tcl_Obj *nameObj, Tcl_HashTable *tablePtr,
    TkPathGradientMaster **g)
{
    Tcl_HashEntry *hPtr;
    char *name = Tcl_GetString(nameObj);
    *g = NULL;
    hPtr = Tcl_FindHashEntry(tablePtr, name);
    if (hPtr == NULL) {
    Tcl_Obj *resultObj;
    resultObj = Tcl_NewStringObj("gradient \"", -1);
    Tcl_AppendStringsToObj(resultObj, name, "\" doesn't exist", (char *) NULL);
    Tcl_SetObjResult(interp, resultObj);
    return TCL_ERROR;
    }
    *g = (TkPathGradientMaster *) Tcl_GetHashValue(hPtr);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * PathGradientCget, Configure, Create, Delete, InUse, Names, Type --
 *
 *    These functions implement gradient object commands in a generic way.
 *    The Tcl_HashTable defines the gradient namespace.
 *
 * Results:
 *    Varies: typically a standard tcl result or void.
 *
 * Side effects:
 *    Varies.
 *
 *--------------------------------------------------------------
 */

static int
PathGradientCget(Tcl_Interp *interp, Tk_Window tkwin, int objc, Tcl_Obj * const objv[],
    Tcl_HashTable *tablePtr)
{
    TkPathGradientMaster    *gradientPtr = NULL;
    Tcl_Obj        *resultObj = NULL;

    if (FindGradientMaster(interp, objv[0], tablePtr, &gradientPtr) != TCL_OK) {
    return TCL_ERROR;
    }
    resultObj = Tk_GetOptionValue(interp, (char *)gradientPtr,
        gradientPtr->optionTable, objv[1], tkwin);
    if (resultObj == NULL) {
    return TCL_ERROR;
    } else {
    Tcl_SetObjResult(interp, resultObj);
    }
    return TCL_OK;
}

static int
PathGradientConfigure(Tcl_Interp *interp, Tk_Window tkwin, int objc, Tcl_Obj * const objv[],
    Tcl_HashTable *tablePtr)
{
    TkPathGradientMaster   *gradientPtr = NULL;
    int            mask;
    Tcl_Obj        *resultObj = NULL;

    if (FindGradientMaster(interp, objv[0], tablePtr, &gradientPtr) != TCL_OK) {
    return TCL_ERROR;
    }
    if (objc <= 2) {
    resultObj = Tk_GetOptionInfo(interp, (char *)gradientPtr,
        gradientPtr->optionTable,
        (objc == 1) ? (Tcl_Obj *) NULL : objv[1], tkwin);
    if (resultObj == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, resultObj);
    } else {
    if (Tk_SetOptions(interp, (char *)gradientPtr, gradientPtr->optionTable,
        objc - 1, objv + 1, tkwin, NULL, &mask) != TCL_OK) {
        return TCL_ERROR;
    }
    }
    TkPathGradientChanged(gradientPtr, TK_PATH_GRADIENT_FLAG_CONFIGURE);
    return TCL_OK;
}

/* GradientCreate: objv starts with 'type' */

static int
PathGradientCreate(Tcl_Interp *interp, Tk_Window tkwin, int objc,
           Tcl_Obj * const objv[], Tcl_HashTable *hashTablePtr,
           char *tokenName)
{
    char        *typeStr;
    int            isNew;
    int            type;
    int            mask;
    Tcl_HashEntry   *hPtr;
    TkPathGradientMaster   *gradientPtr = NULL;
    InterpData             *dataPtr =
    (InterpData *) Tcl_GetAssocData(interp, TK_PATHCMD_PATHGRADIENT, NULL);

    if (dataPtr == NULL) {
    Tcl_SetObjResult(interp,
        Tcl_NewStringObj("gradients not registered in interpreter", -1));
    return TCL_ERROR;
    }
    typeStr = Tcl_GetString(objv[0]);
    if (strcmp(typeStr, "linear") == 0) {
    type = TK_PATH_GRADIENTTYPE_LINEAR;
    } else if (strcmp(typeStr, "radial") == 0) {
    type = TK_PATH_GRADIENTTYPE_RADIAL;
    } else {
    Tcl_Obj *resultObj;
    resultObj = Tcl_NewStringObj("unrecognized type \"", -1);
    Tcl_AppendStringsToObj(resultObj, typeStr, "\", must be \"linear\" or \"radial\"",
        (char *) NULL);
    Tcl_SetObjResult(interp, resultObj);
    return TCL_ERROR;
    }
    gradientPtr = (TkPathGradientMaster *) ckalloc(sizeof(TkPathGradientMaster));
    memset(gradientPtr, '\0', sizeof(TkPathGradientMaster));

    /*
     * Create the option table for this class.  If it has already
     * been created, the cached pointer will be returned.
     */
    if (type == TK_PATH_GRADIENTTYPE_LINEAR) {
    gradientPtr->optionTable = dataPtr->linearOptionTable;
    } else {
    gradientPtr->optionTable = dataPtr->radialOptionTable;
    }
    gradientPtr->type = type;
    gradientPtr->name = Tk_GetUid(tokenName);
    gradientPtr->matrixPtr = NULL;
    gradientPtr->instancePtr = NULL;

    /*
     * Set default transition vector in case not set.
     */
    if (type == TK_PATH_GRADIENTTYPE_LINEAR) {
    TkPathRect *transitionPtr;

    transitionPtr = (TkPathRect *) ckalloc(sizeof(TkPathRect));
    gradientPtr->linearFill.transitionPtr = transitionPtr;
    transitionPtr->x1 = 0.0;
    transitionPtr->y1 = 0.0;
    transitionPtr->x2 = 1.0;
    transitionPtr->y2 = 0.0;
    } else {
    TkRadialTransition *tPtr;

    tPtr = (TkRadialTransition *) ckalloc(sizeof(TkRadialTransition));
    gradientPtr->radialFill.radialPtr = tPtr;
    tPtr->centerX = 0.5;
    tPtr->centerY = 0.5;
    tPtr->radius = 0.5;
    tPtr->focalX = 0.5;
    tPtr->focalY = 0.5;
    }
    if (Tk_InitOptions(interp, (char *)gradientPtr,
        gradientPtr->optionTable, tkwin) != TCL_OK) {
    ckfree((char *)gradientPtr);
    return TCL_ERROR;
    }
    if (Tk_SetOptions(interp, (char *)gradientPtr, gradientPtr->optionTable,
        objc - 1, objv + 1, tkwin, NULL, &mask) != TCL_OK) {
    Tk_FreeConfigOptions((char *)gradientPtr, gradientPtr->optionTable, NULL);
    ckfree((char *)gradientPtr);
    return TCL_ERROR;
    }
    hPtr = Tcl_CreateHashEntry(hashTablePtr, tokenName, &isNew);
    Tcl_SetHashValue(hPtr, gradientPtr);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(tokenName, -1));
    return TCL_OK;
}

static int
PathGradientDelete(Tcl_Interp *interp, Tcl_Obj *obj, Tcl_HashTable *tablePtr)
{
    TkPathGradientMaster *gradientPtr = NULL;

    if (FindGradientMaster(interp, obj, tablePtr, &gradientPtr) != TCL_OK) {
    return TCL_ERROR;
    }
    TkPathGradientChanged(gradientPtr, TK_PATH_GRADIENT_FLAG_DELETE);
    Tcl_DeleteHashEntry(Tcl_FindHashEntry(tablePtr, Tcl_GetString(obj)));
    PathGradientMasterFree(gradientPtr);
    return TCL_OK;
}

static int
PathGradientInUse(Tcl_Interp *interp, Tcl_Obj *obj, Tcl_HashTable *tablePtr)
{
    TkPathGradientMaster   *gradientPtr = NULL;

    if (FindGradientMaster(interp, obj, tablePtr, &gradientPtr) != TCL_OK) {
    return TCL_ERROR;
    }
    Tcl_SetBooleanObj(Tcl_GetObjResult(interp), gradientPtr->instancePtr != NULL);
    return TCL_OK;
}

static void
PathGradientNames(Tcl_Interp *interp, Tcl_HashTable *tablePtr)
{
    char        *name;
    Tcl_HashEntry   *hPtr;
    Tcl_Obj        *listObj;
    Tcl_HashSearch  search;

    listObj = Tcl_NewListObj(0, NULL);
    hPtr = Tcl_FirstHashEntry(tablePtr, &search);
    while (hPtr != NULL) {
    name = (char *) Tcl_GetHashKey(tablePtr, hPtr);
    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj(name, -1));
    hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_SetObjResult(interp, listObj);
}

static int
PathGradientType(Tcl_Interp *interp, Tcl_Obj *obj, Tcl_HashTable *tablePtr)
{
    TkPathGradientMaster *gradientPtr = NULL;

    if (FindGradientMaster(interp, obj, tablePtr, &gradientPtr) != TCL_OK) {
    return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(
        (gradientPtr->type == TK_PATH_GRADIENTTYPE_LINEAR) ? "linear" : "radial", -1));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GradientObjCmd --
 *
 *    Implements the path::gradient command using gGradientHashPtr.
 *
 * Results:
 *    Standard Tcl result
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */

int
GradientObjCmd(ClientData clientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[])
{
    InterpData *dataPtr = (InterpData *) clientData;
    int     index;
    Tk_Window     tkwin = Tk_MainWindow(interp); /* Should have been the canvas. */
    int     result = TCL_OK;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "command ?arg arg...?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], gradientCmds, "command", 0,
        &index) != TCL_OK) {
        return TCL_ERROR;
    }
    switch (index) {

        case kPathGradientCmdCget: {
        if (objc != 4) {
        Tcl_WrongNumArgs(interp, 3, objv, "option");
        return TCL_ERROR;
        }
        result = PathGradientCget(interp, tkwin, objc-2, objv+2,
                      &dataPtr->gradientHash);
            break;
        }

        case kPathGradientCmdConfigure: {
        if (objc < 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "name ?option? ?value option value...?");
        return TCL_ERROR;
        }
        result = PathGradientConfigure(interp, tkwin, objc-2, objv+2,
                       &dataPtr->gradientHash);
            break;
        }

        case kPathGradientCmdCreate: {
        char str[255];

        if (objc < 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "type ?option value...?");
        return TCL_ERROR;
        }
            sprintf(str, "%s%d", TK_PATHCMD_PATHGRADIENT,
            dataPtr->gradientNameUid++);
        result = PathGradientCreate(interp,    tkwin, objc-2, objv+2,
                    &dataPtr->gradientHash,    str);
            break;
        }

        case kPathGradientCmdDelete: {
        if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "name");
        return TCL_ERROR;
        }
        result = PathGradientDelete(interp, objv[2],
                    &dataPtr->gradientHash);
        break;
        }

    case kPathGradientCmdInUse: {
        if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "name");
        return TCL_ERROR;
        }
        result = PathGradientInUse(interp, objv[2],
                       &dataPtr->gradientHash);
        break;
    }

        case kPathGradientCmdNames: {
        if (objc != 2) {
        Tcl_WrongNumArgs(interp, 2, objv, NULL);
        return TCL_ERROR;
        }
        PathGradientNames(interp, &dataPtr->gradientHash);
            break;
        }

        case kPathGradientCmdType: {
        if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "name");
        return TCL_ERROR;
        }
        result = PathGradientType(interp, objv[2],
                      &dataPtr->gradientHash);
            break;
        }
    }
    return result;
}

static void
GradientInterpDeleted(ClientData clientData)
{
    InterpData *dataPtr = (InterpData *) clientData;

    Tcl_DeleteHashTable(&dataPtr->gradientHash);
    ckfree((char *) dataPtr);
}

static void
PathGradientMasterFree(TkPathGradientMaster *gradientPtr)
{
    Tk_FreeConfigOptions((char *) gradientPtr, gradientPtr->optionTable, NULL);
    ckfree((char *) gradientPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkPathGetPathColorStatic --
 *
 *    Looks up named color or gradient in the global (static) gradient
 *    hash table. Used by the surface command to parse its -fill option.
 *    Else see TkPathGetPathColor.
 *
 * Results:
 *    Pointer to a TkPathColor struct or returns NULL on error
 *      and leaves an error message.
 *
 * Side effects:
 *    TkPathColor malloced if OK.
 *
 *----------------------------------------------------------------------
 */

TkPathColor *
TkPathGetPathColorStatic(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *nameObj)
{
    InterpData *dataPtr =
    (InterpData *) Tcl_GetAssocData(interp, TK_PATHCMD_PATHGRADIENT, NULL);
    Tcl_HashTable *hashTablePtr = NULL;

    if (dataPtr != NULL) {
    hashTablePtr = &dataPtr->gradientHash;
    }
    return TkPathGetPathColor(interp, tkwin, nameObj, hashTablePtr,
                  NULL, NULL);
}

/*
 * These functions are called by users of gradients, typically items,
 * that make instances of gradients from a gradient object (master).
 */

/*
 *----------------------------------------------------------------------
 *
 * TkPathGetGradient --
 *
 *    This function is invoked by an item when it wants to use a particular
 *    gradient for a particular hash table. Compare Tk_GetImage.
 *
 * Results:
 *    The return value is a token for the gradient. If there is no gradient by the
 *    given name, then NULL is returned and an error message is left in the
 *    interp's result.
 *
 * Side effects:
 *    Tk records the fact that the item is using the gradient, and it will
 *    invoke changeProc later if the item needs redisplay. The caller must
 *    eventually invoke TkPathFreeGradient when it no longer needs the gradient.
 *
 *----------------------------------------------------------------------
 */

TkPathGradientInst *
TkPathGetGradient(
    Tcl_Interp *interp,
    const char *name,
    Tcl_HashTable *tablePtr,
    TkPathGradientChangedProc *changeProc,
    ClientData clientData)
{
    Tcl_HashEntry *hPtr;
    TkPathGradientInst *gradientPtr;
    TkPathGradientMaster *masterPtr;

    hPtr = Tcl_FindHashEntry(tablePtr, name);
    if (hPtr == NULL) {
    if (interp != NULL) {
            Tcl_Obj *resultObj;
            resultObj = Tcl_NewStringObj("gradient \"", -1);
            Tcl_AppendStringsToObj(resultObj, name, "\" doesn't exist", (char *) NULL);
            Tcl_SetObjResult(interp, resultObj);
    }
    return NULL;
    }
    masterPtr = (TkPathGradientMaster *) Tcl_GetHashValue(hPtr);
    gradientPtr = (TkPathGradientInst *) ckalloc(sizeof(TkPathGradientInst));
    gradientPtr->masterPtr = masterPtr;
    gradientPtr->changeProc = changeProc;
    gradientPtr->clientData = clientData;
    gradientPtr->nextPtr = masterPtr->instancePtr;
    masterPtr->instancePtr = gradientPtr;
    return gradientPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkPathFreeGradient --
 *
 *    This function is invoked by an item when it no longer needs a gradient
 *    acquired by a previous call to TkPathGetGradient. For each call to
 *    TkPathGetGradient there must be exactly one call to TkPathFreeGradient.
 *    Compare Tk_FreeImage.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The association between the gradient and the item is removed.
 *
 *----------------------------------------------------------------------
 */

void
TkPathFreeGradient(
    TkPathGradientInst *gradientPtr)
{
    TkPathGradientMaster *masterPtr = gradientPtr->masterPtr;
    TkPathGradientInst *walkPtr;

    walkPtr = masterPtr->instancePtr;
    if (walkPtr == gradientPtr) {
    masterPtr->instancePtr = gradientPtr->nextPtr;
    } else {
    while (walkPtr->nextPtr != gradientPtr) {
        walkPtr = walkPtr->nextPtr;
    }
    walkPtr->nextPtr = gradientPtr->nextPtr;
    }
    ckfree((char *)gradientPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkPathGradientChanged --
 *
 *    This function is called by a gradient manager whenever something has
 *    happened that requires the gradient to be redrawn or it has been deleted.
 *    Compare Tk_ImageChanged,
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Any items that display the gradient are notified so that they can
 *    redisplay themselves as appropriate.
 *
 *----------------------------------------------------------------------
 */

void
TkPathGradientChanged(TkPathGradientMaster *masterPtr, int flags)
{
    TkPathGradientInst *walkPtr, *nextPtr;

    if (flags) {
        /*
         * NB: We may implicitly call TkPathFreeGradient if being deleted!
         *     Therefore cache the nextPtr before invoking changeProc.
         */
        for (walkPtr = masterPtr->instancePtr; walkPtr != NULL; ) {
            nextPtr = walkPtr->nextPtr;
            if (walkPtr->changeProc != NULL) {
            (*walkPtr->changeProc)(walkPtr->clientData, flags);
            }
            walkPtr = nextPtr;
        }
    }
}
/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
