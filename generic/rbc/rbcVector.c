/*
 * rbcVector.c --
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

#define VECTOR_THREAD_KEY	"Rbc Vector Data"
#define VECTOR_MAGIC		((unsigned int) 0x46170277)
#define MAX_ERR_MSG	1023
#define DEF_ARRAY_SIZE		64

#define TRACE_ALL  (TCL_TRACE_WRITES | TCL_TRACE_READS | TCL_TRACE_UNSETS)

/* These defines allow parsing of different types of indices */

/* Never notify clients of updates to the vector */
#define NOTIFY_NEVER		(1<<3)

/* Notify clients after each update of the vector is made */
#define NOTIFY_ALWAYS		(1<<4)

/* Notify clients at the next idle point that the vector has been updated. */
#define NOTIFY_WHENIDLE		(1<<5)

/* A do-when-idle notification of the vector's clients is pending. */
#define NOTIFY_PENDING		(1<<6)
#define NOTIFY_UPDATED		((int) RBC_VECTOR_NOTIFY_UPDATE)
#define NOTIFY_DESTROYED	((int) RBC_VECTOR_NOTIFY_DESTROY)

#define VECTOR_CHAR(c)	((isalnum(UCHAR(c))) || \
	(c == '_') || (c == ':') || (c == '@') || (c == '.'))

static const char *subCmds[] =
    { "*", "+", "-", "/", "append", "binread", "clear", "delete", "dup",
    "expr", "index", "length", "merge", "normalize", "offset", "populate",
    "random", "range", "search", "seq", "set", "sort", "split", "variable",
    NULL
};

enum cmdIdx {
    multIdx,
    plusIdx,
    minusIdx,
    divisionIdx,
    appendIdx,
    binreadIdx,
    clearIdx,
    deleteIdx,
    dupIdx,
    exprIdx,
    indexIdx,
    lengthIdx,
    mergeIdx,
    normalizeIdx,
    offsetIdx,
    populateIdx,
    randomIdx,
    rangeIdx,
    searchIdx,
    seqIdx,
    setIdx,
    sortIdx,
    splitIdx,
    variableIdx
};

/*
 *	A vector can be shared by several clients.  Each client
 *	allocates this structure that acts as its key for using the
 *	vector.  Clients can also designate a callback routine that is
 *	executed whenever the vector is updated or destroyed.
 *
 */
typedef struct {
    unsigned int    magic;      /* Magic value designating whether this
                                 * really is a vector token or not */
    RbcVectorObject *serverPtr; /* Pointer to the master record of the
                                 * vector.  If NULL, indicates that the
                                 * vector has been destroyed but as of
                                 * yet, this client hasn't recognized
                                 * it. */
    RbcVectorChangedProc *proc; /* Routine to call when the contents
                                 * of the vector change or the vector
                                 * is deleted. */
    ClientData      clientData; /* Data passed whenever the vector
                                 * change procedure is called. */
    RbcChainLink   *linkPtr;    /* Used to quickly remove this entry from
                                 * its server's client chain. */
} VectorClient;

static int      VectorObjCmd(
    ClientData dataPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
static int      VectorInstanceCmd(
    ClientData clientData,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
static int      VectorCreateObjCmd(
    ClientData clientData,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
static int      VectorDestroyObjCmd(
    ClientData clientData,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
static int      VectorExprObjCmd(
    ClientData clientData,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
static int      VectorNamesObjCmd(
    ClientData clientData,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
static void     VectorInterpDeleteProc(
    ClientData clientData,
    Tcl_Interp * interp);
static void     VectorInstDeleteProc(
    ClientData clientData);
static void     VectorNotifyClients(
    ClientData clientData);
static void     VectorFlushCache(
    RbcVectorObject * vPtr);
static const char *VectorVarTrace(
    ClientData clientData,
    Tcl_Interp * interp,
    const char *part1,
    const char *part2,
    int flags);
static const char *BuildQualifiedName(
    Tcl_Interp * interp,
    const char *name,
    Tcl_DString * fullName);
static int      ParseQualifiedName(
    Tcl_Interp * interp,
    const char *qualName,
    Tcl_Namespace ** nsPtrPtr,
    const char **namePtrPtr);
static const char *GetQualifiedName(
    Tcl_Namespace * nsPtr,
    const char *name,
    Tcl_DString * resultPtr);
static RbcVectorObject *GetVectorObject(
    RbcVectorInterpData * dataPtr,
    const char *name,
    int flags);
static RbcVectorObject *FindVectorInNamespace(
    RbcVectorInterpData * dataPtr,
    Tcl_Namespace * nsPtr,
    const char *vecName);
static void     DeleteCommand(
    RbcVectorObject * vPtr);
static void     UnmapVariable(
    RbcVectorObject * vPtr);

double          rbcNaN;

/*
 * -----------------------------------------------------------------------
 *
 * RbcVectorInit --
 *
 *      This procedure is invoked to initialize the "vector" command.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates the new command and adds a new entry into a global Tcl
 *      associative array.
 *
 * ------------------------------------------------------------------------
 */
double
Rbcdrand48(
    )
{
    return ((double) rand() / (double) RAND_MAX);
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcVectorInit --
 *
 *      This procedure is invoked to initialize the "vector" command.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates the new command and adds a new entry into a global Tcl
 *      associative array.
 *
 * ------------------------------------------------------------------------
 */
int
RbcVectorInit(
    Tcl_Interp * interp)
{
    RbcVectorInterpData *dataPtr;       /* Interpreter-specific data. */
#ifdef __BORLANDC__
    union Real {
        struct DoubleWord {
            int             lo, hi;
        } doubleWord;
        double          number;
    } real;
    real.doubleWord.lo = real.doubleWord.hi = 0x7FFFFFFF;
    rbcNaN = real.number;
#endif /* __BORLANDC__ */

#ifdef _MSC_VER
    rbcNaN = sqrt(-1.0);        /* Generate IEEE 754 Quiet Not-A-Number. */
#endif /* _MSC_VER */

#if !defined(__BORLANDC__) && !defined(_MSC_VER)
    rbcNaN = 0.0 / 0.0;         /* Generate IEEE 754 Not-A-Number. */
#endif /* !__BORLANDC__  && !_MSC_VER */

    dataPtr = RbcVectorGetInterpData(interp);
    Tcl_CreateObjCommand(interp, "rbc::vector", VectorObjCmd, dataPtr, NULL);

    return TCL_OK;
}

/*
 * ------------------------------------------------------------------------
 *
 * VectorObjCmd --
 *
 *      This implements the Tcl vector command from the rbc package.
 *      See the user documentation on what is does.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      Do some user action.
 *
 * ------------------------------------------------------------------------
 */
static int
VectorObjCmd(
    ClientData dataPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    int             index;
    const char     *subCmds[] = { "create", "destroy", "expr", "names", NULL };
    enum RecodeIndex {
        createIndex, destroyIndex, exprIndex, namesIndex
    };

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "command ?args?");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], subCmds, "command", 0,
            &index) != TCL_OK) {
        return TCL_ERROR;
    }

    switch (index) {
    case createIndex:{
            return VectorCreateObjCmd((ClientData) dataPtr, interp, objc, objv);
        }
    case destroyIndex:{
            return VectorDestroyObjCmd((ClientData) dataPtr, interp, objc,
                objv);
        }
    case exprIndex:{
            return VectorExprObjCmd((ClientData) dataPtr, interp, objc, objv);
        }
    case namesIndex:{
            return VectorNamesObjCmd((ClientData) dataPtr, interp, objc, objv);
        }
    }

    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * VectorInstanceCmd --
 *
 *      Instance command for the vector. This command
 *      is registered via Tcl_CreatObjCommand for each new vector
 *      and is called when the Tcl vector instance command is called
 *
 * Results:
 *      Returns the result from the operation called on the command or
 *      TCL_ERROR if the operation was unknown or a wrong number of
 *      arguments was specified
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
VectorInstanceCmd(
    ClientData clientData,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    RbcVectorObject *vPtr = clientData;
    int             index;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?args?");
        return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], subCmds, "option", 0,
            &index) != TCL_OK) {
        return TCL_ERROR;
    }

    vPtr->first = 0;
    vPtr->last = vPtr->length - 1;

    switch (index) {
    case multIdx:{
            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "list");
                return TCL_ERROR;
            } else {
                return RbcArithOp(vPtr, interp, objc, objv);
            }
        }
    case plusIdx:{
            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "list");
                return TCL_ERROR;
            } else {
                return RbcArithOp(vPtr, interp, objc, objv);
            }
        }
    case minusIdx:{
            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "list");
                return TCL_ERROR;
            } else {
                return RbcArithOp(vPtr, interp, objc, objv);
            }
        }
    case divisionIdx:{
            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "list");
                return TCL_ERROR;
            } else {
                return RbcArithOp(vPtr, interp, objc, objv);
            }
        }
    case appendIdx:{
            if (objc < 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "item ?item...?");
                return TCL_ERROR;
            } else {
                return RbcAppendOp(vPtr, interp, objc, objv);
            }
        }
    case binreadIdx:{
            if (objc < 3) {
                Tcl_WrongNumArgs(interp, 2, objv,
                    "channel ?numValues? ?flags?");
                return TCL_ERROR;
            } else {
                return RbcBinreadOp(vPtr, interp, objc, objv);
            }
        }
    case clearIdx:{
            if (objc > 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            } else {
                return RbcClearOp(vPtr, interp, objc, objv);
            }
        }
    case deleteIdx:{
            if (objc < 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "index ?index...?");
                return TCL_ERROR;
            } else {
                return RbcDeleteOp(vPtr, interp, objc, objv);
            }
        }
    case dupIdx:{
            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "vecName");
                return TCL_ERROR;
            } else {
                return RbcDupOp(vPtr, interp, objc, objv);
            }
        }
    case exprIdx:{
            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "expression");
                return TCL_ERROR;
            } else {
                return RbcExprOp(vPtr, interp, objc, objv);
            }
        }
    case indexIdx:{
            if ((objc < 3) || (objc > 4)) {
                Tcl_WrongNumArgs(interp, 2, objv, "index ?value?");
                return TCL_ERROR;
            } else {
                return RbcIndexOp(vPtr, interp, objc, objv);
            }
        }
    case lengthIdx:{
            if ((objc < 2) || (objc > 3)) {
                Tcl_WrongNumArgs(interp, 2, objv, "?newSize?");
                return TCL_ERROR;
            } else {
                return RbcLengthOp(vPtr, interp, objc, objv);
            }
        }
    case mergeIdx:{
            if (objc < 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "vecName ?vecName...?");
                return TCL_ERROR;
            } else {
                return RbcMergeOp(vPtr, interp, objc, objv);
            }
        }
    case normalizeIdx:{
            if ((objc < 2) || (objc > 3)) {
                Tcl_WrongNumArgs(interp, 2, objv, "?vecName?");
                return TCL_ERROR;
            } else {
                return RbcNormalizeOp(vPtr, interp, objc, objv);
            }
        }
    case offsetIdx:{
            if ((objc < 2) || (objc > 3)) {
                Tcl_WrongNumArgs(interp, 2, objv, "?offset?");
                return TCL_ERROR;
            } else {
                return RbcOffsetOp(vPtr, interp, objc, objv);
            }
        }
    case randomIdx:{
            if (objc > 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            } else {
                return RbcRandomOp(vPtr, interp, objc, objv);
            }
        }
    case populateIdx:{
            if (objc != 4) {
                Tcl_WrongNumArgs(interp, 2, objv, "vecName density");
                return TCL_ERROR;
            } else {
                return RbcPopulateOp(vPtr, interp, objc, objv);
            }
        }
    case rangeIdx:{
            if (objc != 4) {
                Tcl_WrongNumArgs(interp, 2, objv, "first last");
                return TCL_ERROR;
            } else {
                return RbcRangeOp(vPtr, interp, objc, objv);
            }
        }
    case searchIdx:{
            if ((objc < 3) || (objc > 4)) {
                Tcl_WrongNumArgs(interp, 2, objv, "?-value? value ?value?");
                return TCL_ERROR;
            } else {
                return RbcSearchOp(vPtr, interp, objc, objv);
            }
        }
    case seqIdx:{
            if ((objc < 4) || (objc > 5)) {
                Tcl_WrongNumArgs(interp, 2, objv, "start end ?step?");
                return TCL_ERROR;
            } else {
                return RbcSeqOp(vPtr, interp, objc, objv);
            }
        }
    case setIdx:{
            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "list");
                return TCL_ERROR;
            } else {
                return RbcSetOp(vPtr, interp, objc, objv);
            }
        }
    case sortIdx:{
            if (objc < 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "?-reverse? ?vecName...?");
                return TCL_ERROR;
            } else {
                return RbcSortOp(vPtr, interp, objc, objv);
            }
        }
    case splitIdx:{
            if (objc < 2) {
                Tcl_WrongNumArgs(interp, 2, objv, "?vecName...?");
                return TCL_ERROR;
            } else {
                return RbcSplitOp(vPtr, interp, objc, objv);
            }
        }
    case variableIdx:{
            if ((objc < 2) || (objc > 3)) {
                Tcl_WrongNumArgs(interp, 2, objv, "?varName?");
                return TCL_ERROR;
            } else {
                return RbcVariableOp(vPtr, interp, objc, objv);
            }
        }
    }
    /* we should never reach this point: */
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * VectorCreateObjCmd --
 *
 *      processes the Tcl 'vector create' command, and calls
 *      vectorCreate to actually create the vector
 *
 *        vector create a
 *        vector create b(20)
 *        vector create c(-5:14)
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 *----------------------------------------------------------------------
 */
static int
VectorCreateObjCmd(
    ClientData clientData,      /* Vector interp data */
    Tcl_Interp * interp,        /* Interp to return results to */
    int objc,                   /* argument count */
    Tcl_Obj * const objv[])
{                               /* arguments to the command */
    RbcVectorInterpData *dataPtr = clientData;
    RbcVectorObject *vPtr;
    Tcl_Obj        *resultPtr;  /* for the result of this function */
    char           *leftParen, *rightParen;     /* positions of left and right parens in vector specification */
    int             isNew, size, first, last;
    char           *cmdName, *varName, *switchName;
    int             length;
    int             freeOnUnset, flush;
    char          **nameArr;    /* holds all vector names specified */
    int             count;
    register int    i;

    resultPtr = Tcl_NewStringObj("", -1);

    /*
     * Handle switches to the vector command and collect the vector
     * name arguments into an array.
     */
    varName = NULL;             /* name of Tcl variable to link to the vector */
    cmdName = NULL;             /* name of Tcl command to link to vector */
    freeOnUnset = 0;            /* value of the user level '-watchunset' switch */
    flush = FALSE;
    nameArr = (char **) ckalloc(sizeof(char *) * objc);

    /***    assert(nameArr); */
    count = 0;
    vPtr = NULL;
    for (i = 2; i < objc; i++) {
        /* collect all arguments: */
        if (Tcl_GetStringFromObj(objv[i], NULL)[0] == '-') {
            /* found a switch: */
            switchName = Tcl_GetStringFromObj(objv[i], &length);
            if ((length > 1) && (strncmp(switchName, "-variable", length) == 0)) {
                /* process -variable switch: */
                if ((i + 1) == objc) {
                    Tcl_AppendStringsToObj(resultPtr,
                        "no variable name supplied with \"-variable\" switch",
                        NULL);
                    Tcl_SetObjResult(interp, resultPtr);
                    goto error;
                }
                i++;
                varName = Tcl_GetStringFromObj(objv[i], NULL);
            } else if ((length > 1)
                && (strncmp(switchName, "-command", length) == 0)) {
                /* process -command switch: */
                if ((i + 1) == objc) {
                    Tcl_AppendStringsToObj(resultPtr,
                        "no command name supplied with \"-command\" switch",
                        NULL);
                    Tcl_SetObjResult(interp, resultPtr);
                    goto error;
                }
                i++;
                cmdName = Tcl_GetStringFromObj(objv[i], NULL);
            } else if ((length > 1)
                && (strncmp(switchName, "-watchunset", length) == 0)) {
                /* process -watchunset switch: */
                int             bool;

                if ((i + 1) == objc) {
                    Tcl_AppendStringsToObj(resultPtr,
                        "no value name supplied with \"-watchunset\" switch",
                        NULL);
                    Tcl_SetObjResult(interp, resultPtr);
                    goto error;
                }
                i++;
                if (Tcl_GetBooleanFromObj(interp, objv[i], &bool) != TCL_OK) {
                    goto error;
                }
                freeOnUnset = bool;
            } else if ((length > 1)
                && (strncmp(switchName, "-flush", length) == 0)) {
                /* process -flush switch: */
                int             bool;

                if ((i + 1) == objc) {
                    Tcl_AppendStringsToObj(resultPtr,
                        "no value name supplied with \"-flush\" switch", NULL);
                    Tcl_SetObjResult(interp, resultPtr);
                    goto error;
                }
                i++;
                if (Tcl_GetBooleanFromObj(interp, objv[i], &bool) != TCL_OK) {
                    goto error;
                }
                flush = bool;
            } else {
                Tcl_AppendStringsToObj(resultPtr,
                    "bad vector switch \"",
                    switchName,
                    "\": must be -command, -flush, -variable, or -watchunset",
                    NULL);
                Tcl_SetObjResult(interp, resultPtr);
                goto error;
            }
        } else {
            /* found a vector name: */
            nameArr[count++] = Tcl_GetStringFromObj(objv[i], NULL);
        }
    }
    /* finished parsing arguments -> do some sanity checks: */
    if (count == 0) {
        Tcl_AppendStringsToObj(resultPtr, "no vector names supplied", NULL);
        Tcl_SetObjResult(interp, resultPtr);
        goto error;
    }

    if (count > 1) {
        if ((cmdName != NULL) && (cmdName[0] != '\0')) {
            Tcl_AppendStringsToObj(resultPtr,
                "can't specify more than one vector with \"-command\" switch",
                NULL);
            Tcl_SetObjResult(interp, resultPtr);
            goto error;
        }
        if ((varName != NULL) && (varName[0] != '\0')) {
            Tcl_AppendStringsToObj(resultPtr,
                "can't specify more than one vector with \"-variable\" switch",
                NULL);
            Tcl_SetObjResult(interp, resultPtr);
            goto error;
        }
    }

    /* now process the vector names and check their validity: */
    for (i = 0; i < count; i++) {
        size = first = last = 0;
        leftParen = strchr(nameArr[i], '(');
        rightParen = strchr(nameArr[i], ')');
        if (((leftParen != NULL) && (rightParen == NULL)) || ((leftParen
                    == NULL) && (rightParen != NULL))
            || (leftParen > rightParen)) {
            Tcl_AppendStringsToObj(resultPtr, "bad vector specification \"",
                nameArr[i], "\"", NULL);
            Tcl_SetObjResult(interp, resultPtr);
            goto error;
        }
        if (leftParen != NULL) {
            int             result;
            char           *colon;

            *rightParen = '\0';
            colon = strchr(leftParen + 1, ':');
            if (colon != NULL) {
                /* Specification is in the form vecName(first:last) */
                *colon = '\0';
                result = Tcl_GetInt(interp, leftParen + 1, &first);
                if ((*(colon + 1) != '\0') && (result == TCL_OK)) {
                    result = Tcl_GetInt(interp, colon + 1, &last);
                    if (first > last) {
                        Tcl_AppendStringsToObj(resultPtr,
                            "bad vector range \"", nameArr[i], "\"", NULL);
                        Tcl_SetObjResult(interp, resultPtr);
                        result = TCL_ERROR;
                    }
                    size = (last - first) + 1;
                }
                *colon = ':';
            } else {
                /* Specification is in the form vecName(size) */
                result = Tcl_GetInt(interp, leftParen + 1, &size);
            }
            *rightParen = ')';
            if (result != TCL_OK) {
                goto error;
            }
            if (size < 0) {
                Tcl_AppendStringsToObj(resultPtr, "bad vector size \"",
                    nameArr[i], "\"", NULL);
                Tcl_AppendResult(interp, resultPtr);
                goto error;
            }
        }
        if (leftParen != NULL) {
            *leftParen = '\0';
        }

        /*
         * actually create the vector:
         */
        vPtr = RbcVectorCreate(dataPtr, nameArr[i],
            (cmdName == NULL) ? nameArr[i] : cmdName,
            (varName == NULL) ? nameArr[i] : varName, &isNew);

        if (leftParen != NULL) {
            *leftParen = '(';
        }
        if (vPtr == NULL) {
            goto error;
        }
        vPtr->freeOnUnset = freeOnUnset;
        vPtr->flush = flush;
        vPtr->offset = first;
        if (size > 0) {
            if (RbcVectorChangeLength(vPtr, size) != TCL_OK) {
                goto error;
            }
        }
        if (!isNew) {
            if (vPtr->flush) {
                VectorFlushCache(vPtr);
            }
            RbcVectorUpdateClients(vPtr);
        }
    }
    ckfree((char *) nameArr);
    if (vPtr != NULL) {
        /* Return the name of the last vector created  */
        Tcl_SetObjResult(interp, Tcl_NewStringObj(vPtr->name, -1));
    }
    return TCL_OK;
  error:
    ckfree((char *) nameArr);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * VectorDestroyObjCmd --
 *
 *      processes the Tcl 'vector destroy' command, and calls vectorCreate
 *      to actually create the vector
 *
 *        vector create a
 *        vector create b(20)
 *        vector create c(-5:14)
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 *----------------------------------------------------------------------
 */
static int
VectorDestroyObjCmd(
    ClientData clientData,      /* Interpreter-specific data. */
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    /* Not Implemented Correctly */

    RbcVectorInterpData *dataPtr = clientData;
    RbcVectorObject *vPtr;
    register int    i;
    for (i = 2; i < objc; i++) {
        if (RbcVectorLookupName(dataPtr, Tcl_GetStringFromObj(objv[i], NULL),
                &vPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        RbcVectorFree(vPtr);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * VectorExprObjCmd --
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
static int
VectorExprObjCmd(
    ClientData clientData,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    return RbcExprVector(interp, Tcl_GetString(objv[2]), (RbcVector *) NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * VectorNamesObjCmd --
 *
 *      Reports the names of all the current vectors in the
 *      interpreter.
 *
 * Results:
 *      A standard Tcl result.  interp->result will contain a
 *      list of all the names of the vector instances.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
VectorNamesObjCmd(
    ClientData clientData,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[])
{
    RbcVectorInterpData *dataPtr = clientData;
    Tcl_HashEntry  *hPtr;
    const char     *name;
    Tcl_HashSearch  cursor;
    Tcl_Obj        *resultPtr;

    resultPtr = Tcl_NewListObj(0, NULL);
    for (hPtr = Tcl_FirstHashEntry(&(dataPtr->vectorTable), &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        name = Tcl_GetHashKey(&(dataPtr->vectorTable), hPtr);
        if ((objc == 2) || (Tcl_StringMatch(name, Tcl_GetString(objv[2])))) {
            Tcl_ListObjAppendElement(interp, resultPtr, Tcl_NewStringObj(name,
                    -1));
        }
    }
    Tcl_SetObjResult(interp, resultPtr);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 *RbcVectorGetInterpData --
 *
 *      Gathers the data need for the vector interpreter to function.
 *      It then stores it into the vector interpreter.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates the new command and adds a new entry into a global Tcl
 *      associative array.
 *
 * ------------------------------------------------------------------------
 */

RbcVectorInterpData *
RbcVectorGetInterpData(
    Tcl_Interp * interp)
{                               /* Base interpreter to wrap. */
    RbcVectorInterpData *dataPtr;
    Tcl_InterpDeleteProc *proc;

    dataPtr =
        (RbcVectorInterpData *) Tcl_GetAssocData(interp, VECTOR_THREAD_KEY,
        &proc);
    if (dataPtr == NULL) {
        dataPtr = (RbcVectorInterpData *) ckalloc(sizeof(RbcVectorInterpData));

        /***	assert(dataPtr); */
        dataPtr->interp = interp;
        dataPtr->nextId = 0;
        Tcl_SetAssocData(interp, VECTOR_THREAD_KEY, VectorInterpDeleteProc,
            dataPtr);
        Tcl_InitHashTable(&(dataPtr->vectorTable), TCL_STRING_KEYS);
        Tcl_InitHashTable(&(dataPtr->mathProcTable), TCL_STRING_KEYS);
        Tcl_InitHashTable(&(dataPtr->indexProcTable), TCL_STRING_KEYS);
        RbcVectorInstallMathFunctions(&(dataPtr->mathProcTable));
        RbcVectorInstallSpecialIndices(&(dataPtr->indexProcTable));

/* ???        srand(time((time_t *) NULL)); */
    }
    return dataPtr;
}

/*
 * -----------------------------------------------------------------------
 *
 * VectorInterpDeleteProc --
 *
 *      This is called when the interpreter hosting the "vector"
 *      command is deleted.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Destroys the math and index hash tables.  In addition removes
 *      the hash table managing all vector names.
 *
 * ------------------------------------------------------------------------
 */
static void
VectorInterpDeleteProc(
    ClientData clientData,      /* Interpreter Specific */
    Tcl_Interp * interp)
{
    RbcVectorInterpData *dataPtr = clientData;
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  cursor;
    RbcVectorObject *vPtr;

    for (hPtr = Tcl_FirstHashEntry(&(dataPtr->vectorTable), &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        vPtr = (RbcVectorObject *) Tcl_GetHashValue(hPtr);
        vPtr->hashPtr = NULL;
        RbcVectorFree(vPtr);
    }
    Tcl_DeleteHashTable(&(dataPtr->vectorTable));

    /* If any user-defined math functions were installed, remove them.  */
    Tcl_DeleteHashTable(&(dataPtr->mathProcTable));

    Tcl_DeleteHashTable(&(dataPtr->indexProcTable));
    Tcl_DeleteAssocData(interp, VECTOR_THREAD_KEY);
    ckfree((char *) dataPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVectorNew --
 *
 *      Creates a new vector object and populates with the needed data.
 *
 * Results:
 *      A pointer to the new vector object
 *
 * Side effects:
 *      None
 *
 * ---------------------------------------------------------------------- */
RbcVectorObject *
RbcVectorNew(
    RbcVectorInterpData * dataPtr)
{                               /* Interpreter-specific data. */
    RbcVectorObject *vPtr;

    vPtr = RbcCalloc(1, sizeof(RbcVectorObject));

    /***    assert(vPtr); */
    vPtr->notifyFlags = NOTIFY_WHENIDLE;
    vPtr->freeProc = TCL_STATIC;
    vPtr->dataPtr = dataPtr;
    vPtr->valueArr = NULL;
    vPtr->length = vPtr->size = 0;
    vPtr->interp = dataPtr->interp;
    vPtr->hashPtr = NULL;
    vPtr->chainPtr = RbcChainCreate();
    vPtr->flush = FALSE;
    vPtr->min = vPtr->max = rbcNaN;
    return vPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVectorCreate --
 *
 *      Actually creates a vector structure and the following items:
 *
 *        o Tcl command
 *        o Tcl array variable and establishes traces on the variable
 *        o Adds a  new entry in the vector hash table
 *
 * Results:
 *      A pointer to the new vector structure.  If an error occurred
 *      NULL is returned and an error message is left in
 *      interp->result.
 *
 * Side effects:
 *      A new Tcl command and array variable is added to the
 *      interpreter.
 *
 * ---------------------------------------------------------------------- */
RbcVectorObject *RbcVectorCreate(
    RbcVectorInterpData * dataPtr,      /* Interpreter-specific data (clientData). */
    const char *vecName,        /* Name of the vector */
    const char *cmdName,        /* Name of the Tcl command mapped to the vector; if NULL (actually '\0') then do not create a command */
    const char *varName,        /* Name of the Tcl array mapped to the vector; if NULL (actually '\0') then do not create a variable */
    int *newPtr) {              /* pointer to the vector created */
    Tcl_Obj        *resultPtr = Tcl_NewStringObj("", -1);
    RbcVectorObject *vPtr;
    int             isNew;
    int             isAutoName = 0;     /* is the name autmatically generated? */
    const char     *qualVecName = NULL; /* qualified name of the vector */
    const char     *vecNameTail;        /* the name of the vector without namespace */
    Tcl_Namespace  *nsPtr;      /* namespace of the vector name */
    Tcl_HashEntry  *hPtr;
    Tcl_Interp     *interp = dataPtr->interp;
    Tcl_DString     qualVecNamePtr;

    isNew = 0;
    nsPtr = NULL;
    vPtr = NULL;

    /* process the vector name: */
    vecName = BuildQualifiedName(interp, vecName, &qualVecNamePtr);
    if (ParseQualifiedName(interp, vecName, &nsPtr, &vecNameTail) != TCL_OK) {
        Tcl_AppendStringsToObj(resultPtr, "unknown namespace in \"", vecName,
            "\"", NULL);
        Tcl_SetObjResult(interp, resultPtr);
        return NULL;
    }

    if ((vecNameTail[0] == '#') && (strcmp(vecNameTail, "#auto") == 0)) {
        /* generate a unique automatic name for the vector: */
        char            string[200];

        do {
            sprintf(string, "vector%d", dataPtr->nextId++);
            qualVecName = GetQualifiedName(nsPtr, string, &qualVecNamePtr);
            hPtr = Tcl_FindHashEntry(&(dataPtr->vectorTable), qualVecName);
        } while (hPtr != NULL);
        isAutoName = 1;
    } else {
        /* check correct vector name syntax: */
        register const char *p;

        for (p = vecNameTail; *p != '\0'; p++) {
            if (!VECTOR_CHAR(*p)) {
                Tcl_AppendStringsToObj(resultPtr,
                    "bad vector name \"",
                    vecName,
                    "\": must contain digits, letters, underscore, or period",
                    NULL);
                Tcl_SetObjResult(interp, resultPtr);
                goto error;
            }
        }
        qualVecName = (char *) vecName;
        vPtr =
            RbcVectorParseElement((Tcl_Interp *) NULL, dataPtr, qualVecName,
            (char **) NULL, RBC_NS_SEARCH_CURRENT);
    }

    if (vPtr == NULL) {
        hPtr =
            Tcl_CreateHashEntry(&(dataPtr->vectorTable), qualVecName, &isNew);
        vPtr = RbcVectorNew(dataPtr);
        vPtr->hashPtr = hPtr;

        vPtr->name = Tcl_GetHashKey(&(dataPtr->vectorTable), hPtr);

#ifdef NAMESPACE_DELETE_NOTIFY
        /* Not Implemented Yet */

        /***	RbcCreateNsDeleteNotify(interp, nsPtr, vPtr, VectorInstDeleteProc); */
#endif /* NAMESPACE_DELETE_NOTIFY */

        Tcl_SetHashValue(hPtr, vPtr);
    }

    /* process the command name: */
    if (cmdName != NULL) {
        Tcl_CmdInfo     cmdInfo;

        if (isAutoName) {
            cmdName = qualVecName;
        } else {
            cmdName = BuildQualifiedName(interp, cmdName, &qualVecNamePtr);
        }
        nsPtr = NULL;
        vecNameTail = NULL;
        if (ParseQualifiedName(interp, cmdName, &nsPtr, &vecNameTail) != TCL_OK) {
            Tcl_AppendStringsToObj(resultPtr, "unknown namespace in \"",
                cmdName, "\"", NULL);
            Tcl_SetObjResult(interp, resultPtr);
            return NULL;
        }

        if (Tcl_GetCommandInfo(interp, cmdName, &cmdInfo)) {
            if (vPtr != cmdInfo.objClientData) {
                Tcl_AppendStringsToObj(resultPtr, "command \"", cmdName,
                    "\" already exists", NULL);
                Tcl_SetObjResult(interp, resultPtr);
                goto error;
            }
        }
    }

    if (vPtr->cmdToken != 0) {
        DeleteCommand(vPtr);    /* Command already exists, delete old first */
    }

    if (cmdName != NULL) {
        vPtr->cmdToken =
            Tcl_CreateObjCommand(interp, cmdName, VectorInstanceCmd, vPtr,
            VectorInstDeleteProc);
    }

    /* process array variable: */
    if (varName != NULL) {
        if ((varName[0] == '#') && (strcmp(varName, "#auto") == 0)) {
            varName = vPtr->name;
        } else {
            varName = BuildQualifiedName(interp, varName, &qualVecNamePtr);
        }
        if (RbcVectorMapVariable(interp, vPtr, varName) != TCL_OK) {
            goto error;
        }
    }

    *newPtr = isNew;
    Tcl_DStringFree(&qualVecNamePtr);
    return vPtr;

  error:
    if (vPtr != NULL) {
        RbcVectorFree(vPtr);
    }
    Tcl_DStringFree(&qualVecNamePtr);
    return NULL;
}

/*
 * ----------------------------------------------------------------------
 *
 * VectorInstDeleteProc --
 *
 *     Deletes the command associated with the vector.  This is
 *     called only when the command associated with the vector is
 *     destroyed.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void
VectorInstDeleteProc(
    ClientData clientData)
{                               /* Vector object to delete */
    RbcVectorObject *vPtr = clientData;

    vPtr->cmdToken = 0;
    RbcVectorFree(vPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVectorFree --
 *
 *     Removes the memory and frees resources associated with the
 *     vector.
 *
 *        o Removes the trace and the Tcl array variable and unsets
 *          the variable.
 *        o Notifies clients of the vector that the vector is being
 *          destroyed.
 *        o Removes any clients that are left after notification.
 *        o Frees the memory (if necessary) allocated for the array.
 *        o Removes the entry from the hash table of vectors.
 *        o Frees the memory allocated for the name.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
void
RbcVectorFree(
    RbcVectorObject * vPtr)
{                               /* The vector to free */
    RbcChainLink   *linkPtr;
    VectorClient   *clientPtr;

    if (vPtr->cmdToken != 0) {
        DeleteCommand(vPtr);
    }

    if (vPtr->arrayName != NULL) {
        UnmapVariable(vPtr);
    }
    vPtr->length = 0;

    /* Immediately notify clients that vector is going away */
    if (vPtr->notifyFlags & NOTIFY_PENDING) {
        vPtr->notifyFlags &= ~NOTIFY_PENDING;
        Tcl_CancelIdleCall(VectorNotifyClients, vPtr);
    }
    vPtr->notifyFlags |= NOTIFY_DESTROYED;
    VectorNotifyClients(vPtr);

    for (linkPtr = RbcChainFirstLink(vPtr->chainPtr); linkPtr != NULL;
        linkPtr = RbcChainNextLink(linkPtr)) {
        clientPtr = RbcChainGetValue(linkPtr);
        ckfree((char *) clientPtr);
    }
    RbcChainDestroy(vPtr->chainPtr);
    if ((vPtr->valueArr != NULL) && (vPtr->freeProc != TCL_STATIC)) {
        if (vPtr->freeProc == TCL_DYNAMIC) {
            ckfree((char *) vPtr->valueArr);
        } else {
            (*vPtr->freeProc) ((char *) vPtr->valueArr);
        }
    }
    if (vPtr->hashPtr != NULL) {
        Tcl_DeleteHashEntry(vPtr->hashPtr);
    }
#ifdef NAMESPACE_DELETE_NOTIFY
    if (vPtr->nsPtr != NULL) {
        /* Not Implemented Yet */

        /*** RbcDestroyNsDeleteNotify(vPtr->interp, vPtr->nsPtr, vPtr); */
    }
#endif /* NAMESPACE_DELETE_NOTIFY */
    ckfree((char *) vPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVectorDuplicate --
 *
 *      Duplicates all elements of a vector.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      New vector is created.
 *
 * ----------------------------------------------------------------------
 */
int
RbcVectorDuplicate(
    RbcVectorObject * destPtr,
    RbcVectorObject * srcPtr)
{
    int             nBytes;
    int             length;

    length = srcPtr->last - srcPtr->first + 1;
    if (RbcVectorChangeLength(destPtr, length) != TCL_OK) {
        return TCL_ERROR;
    }
    nBytes = length * sizeof(double);
    memcpy(destPtr->valueArr, srcPtr->valueArr + srcPtr->first, nBytes);
    destPtr->offset = srcPtr->offset;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVectorFlushCache --
 *
 *      Unsets all the elements of the Tcl array variable associated
 *      with the vector, freeing memory associated with the variable.
 *      This includes both the hash table and the hash keys.  The down
 *      side is that this effectively flushes the caching of vector
 *      elements in the array.  This means that the subsequent reads
 *      of the array will require a decimal to string conversion.
 *
 *      This is needed when the vector changes its values, making
 *      the array variable out-of-sync.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      All elements of array variable (except one) are unset, freeing
 *      the memory associated with the variable.
 *
 * ----------------------------------------------------------------------
 */
void
RbcVectorFlushCache(
    RbcVectorObject * vPtr)
{
    Tcl_Interp     *interp = vPtr->interp;

    if (vPtr->arrayName == NULL) {
        return;                 /* Doesn't use the variable API */
    }

    /* Turn off the trace temporarily so that we can unset all the
     * elements in the array.  */

    Tcl_UntraceVar2(interp, vPtr->arrayName, (char *) NULL,
        TRACE_ALL | vPtr->varFlags, (Tcl_VarTraceProc *) VectorVarTrace, vPtr);

    /* Clear all the element entries from the entire array */
    Tcl_UnsetVar2(interp, vPtr->arrayName, (char *) NULL, vPtr->varFlags);

    /* Restore the "end" index by default and the trace on the entire array */
    Tcl_SetVar2(interp, vPtr->arrayName, "end", "", vPtr->varFlags);
    Tcl_TraceVar2(interp, vPtr->arrayName, (char *) NULL,
        TRACE_ALL | vPtr->varFlags, (Tcl_VarTraceProc *) VectorVarTrace, vPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVectorMapVariable --
 *
 *      Sets up traces on a Tcl variable to access the vector.
 *
 *      If another variable is already mapped, it's first untraced and
 *      removed.  Don't do anything else for variables named "" (even
 *      though Tcl allows this pathology). Saves the name of the new
 *      array variable.
 *
 * Results:
 *      A standard Tcl result. If an error occurs setting the variable
 *      TCL_ERROR is returned and an error message is left in the
 *      interpreter.
 *
 * Side effects:
 *      Traces are set for the new variable. The new variable name is
 *	    saved in a malloc'ed string in vPtr->arrayName.  If this
 *	    variable is non-NULL, it indicates that a Tcl variable has
 *      been mapped to this vector.
 *
 * ----------------------------------------------------------------------
 */
int
RbcVectorMapVariable(
    Tcl_Interp * interp,
    RbcVectorObject * vPtr,
    const char *name)
{                               /* name of array variable to map to vector */
    const char     *result;
    Tcl_Namespace  *varNsPtr;

    if (vPtr->arrayName != NULL) {
        UnmapVariable(vPtr);
    }
    if ((name == NULL) || (name[0] == '\0')) {
        /* If the variable name is the empty string, simply return after removing any existing variable. */
        return TCL_OK;
    }

    /*
     * To play it safe, delete the variable first.  This has
     * side-effect of unmapping the variable from any vector that may
     * be currently using it.
     */
    Tcl_UnsetVar2(interp, name, NULL, 0);

    /* Set the index "end" in the array.  This will create the
     * variable immediately so that we can check its namespace
     * context.
     */
    result = Tcl_SetVar2(interp, name, "end", "", TCL_LEAVE_ERR_MSG);

    /* Determine if the variable is global or not.  If there wasn't a
     * namespace qualifier, it still may be global.  We need to look
     * inside the Var structure to see what it's namespace field says.
     * NULL indicates that it's local.
     */
    varNsPtr = Tcl_FindNamespace(interp, name, NULL, 0);
    vPtr->varFlags =
        (varNsPtr != NULL) ? (TCL_NAMESPACE_ONLY | TCL_GLOBAL_ONLY) : 0;

    if (result != NULL) {
        /* Trace the array on reads, writes, and unsets */
        /*printf("trace on %s with variable %s\n",vPtr->name,name); */
        Tcl_TraceVar2(interp, name, NULL, (TRACE_ALL | vPtr->varFlags),
            (Tcl_VarTraceProc *) VectorVarTrace, vPtr);
    }

    vPtr->arrayName = RbcStrdup(name);
    return (result == NULL) ? TCL_ERROR : TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcVectorReset --
 *
 *      Resets the vector data.  This is called by a client to
 *      indicate that the vector data has changed.  The vector does
 *      not need to point to different memory.  Any clients of the
 *      vector will be notified of the change.
 *
 * Results:
 *      A standard Tcl result.  If the new array size is invalid,
 *      TCL_ERROR is returned.  Otherwise TCL_OK is returned and the
 *      new vector data is recorded.
 *
 * Side Effects:
 *      Any client designated callbacks will be posted.  Memory may
 *      be changed for the vector array.
 *
 * -----------------------------------------------------------------------
 */
int
RbcVectorReset(
    RbcVectorObject * vPtr,
    double *valueArr,           /* Array containing the elements of th
                                 * vector. If NULL, indicates to reset the
                                 * vector.*/
    int length,                 /* The number of elements that the vector currently holds. */
    int size,                   /* The maximum number of elements that the  array can hold. */
    Tcl_FreeProc * freeProc)
{                               /* Address of memory deallocation routine
                                 * for the array of values.  Can also be
                                 * TCL_STATIC, TCL_DYNAMIC, or TCL_VOLATILE. */
    if (vPtr->valueArr != valueArr) {   /* New array of values resides
                                         * in different memory than
                                         * the current vector.  */
        if ((valueArr == NULL) || (size == 0)) {
            /* Empty array. Set up default values */
            freeProc = TCL_STATIC;
            valueArr = NULL;
            size = length = 0;
        } else if (freeProc == TCL_VOLATILE) {
            double         *newArr;
            /* Data is volatile. Make a copy of the value array.  */
            newArr = (double *) ckalloc(size * sizeof(double));
            if (newArr == NULL) {
                Tcl_AppendPrintfToObj(Tcl_GetObjResult(vPtr->interp),
                    "can't allocate %d elements for vector \"%s\"",
                    size, vPtr->name);
                return TCL_ERROR;
            }
            memcpy((char *) newArr, (char *) valueArr, sizeof(double) * length);
            valueArr = newArr;
            freeProc = TCL_DYNAMIC;
        }

        if (vPtr->freeProc != TCL_STATIC) {
            /* Old data was dynamically allocated. Free it before
             * attaching new data.  */
            if (vPtr->freeProc == TCL_DYNAMIC) {
                ckfree((char *) vPtr->valueArr);
            } else {
                (*freeProc) ((char *) vPtr->valueArr);
            }
        }
        vPtr->freeProc = freeProc;
        vPtr->valueArr = valueArr;
        vPtr->size = size;
    }

    vPtr->length = length;
    if (vPtr->flush) {
        RbcVectorFlushCache(vPtr);
    }
    RbcVectorUpdateClients(vPtr);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * VectorNotifyClients --
 *
 *      Notifies each client of the vector that the vector has changed
 *      (updated or destroyed) by calling the provided function back.
 *      The function pointer may be NULL, in that case the client is
 *      not notified.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The results depend upon what actions the client callbacks
 *      take.
 *
 * ----------------------------------------------------------------------
 */
static void
VectorNotifyClients(
    ClientData clientData)
{
    RbcVectorObject *vPtr = clientData;
    RbcChainLink   *linkPtr;
    VectorClient   *clientPtr;
    RbcVectorNotify notify;

    notify = (vPtr->notifyFlags & NOTIFY_DESTROYED) ? RBC_VECTOR_NOTIFY_DESTROY
        : RBC_VECTOR_NOTIFY_UPDATE;
    vPtr->notifyFlags &= ~(NOTIFY_UPDATED | NOTIFY_DESTROYED | NOTIFY_PENDING);

    for (linkPtr = RbcChainFirstLink(vPtr->chainPtr); linkPtr != NULL;
        linkPtr = RbcChainNextLink(linkPtr)) {
        clientPtr = RbcChainGetValue(linkPtr);
        if (clientPtr->proc != NULL) {
            (*clientPtr->proc) (vPtr->interp, clientPtr->clientData, notify);
        }
    }
    /*
     * Some clients may not handle the "destroy" callback properly
     * (they should call RbcFreeVectorId to release the client
     * identifier), so mark any remaining clients to indicate that
     * vector's server has gone away.
     */
    if (notify == RBC_VECTOR_NOTIFY_DESTROY) {
        for (linkPtr = RbcChainFirstLink(vPtr->chainPtr); linkPtr != NULL;
            linkPtr = RbcChainNextLink(linkPtr)) {
            clientPtr = RbcChainGetValue(linkPtr);
            clientPtr->serverPtr = NULL;
        }
    }
}

/*
 *--------------------------------------------------------------
 *
 * RbcVectorNotifyPending --
 *
 *      Returns the name of the vector (and array variable).
 *
 * Results:
 *      The name of the array variable is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int
RbcVectorNotifyPending(
    RbcVectorId clientId)
{                               /* Client token identifying the vector */
    VectorClient   *clientPtr = (VectorClient *) clientId;

    if ((clientPtr == NULL) || (clientPtr->magic != VECTOR_MAGIC)
        || (clientPtr->serverPtr == NULL)) {
        return 0;
    }
    return (clientPtr->serverPtr->notifyFlags & NOTIFY_PENDING);
}

/*
 * ----------------------------------------------------------------------
 *
 * VectorFlushCache --
 *
 *      Unsets all the elements of the Tcl array variable associated
 *      with the vector, freeing memory associated with the variable.
 *      This includes both the hash table and the hash keys.  The down
 *      side is that this effectively flushes the caching of vector
 *      elements in the array.  This means that the subsequent reads
 *      of the array will require a decimal to string conversion.
 *
 *      This is needed when the vector changes its values, making
 *      the array variable out-of-sync.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      All elements of array variable (except one) are unset, freeing
 *      the memory associated with the variable.
 *
 * ----------------------------------------------------------------------
 */
static void
VectorFlushCache(
    RbcVectorObject * vPtr)
{                               /* The vector to flush */
    Tcl_Interp     *interp = vPtr->interp;

    if (vPtr->arrayName == NULL) {
        return;                 /* Doesn't use the variable API */
    }

    /* Turn off the trace temporarily so that we can unset all the
     * elements in the array.  */
    /* TODO I added a cast to Tcl_VarTraceProc * which might cause issues. */
    Tcl_UntraceVar2(interp, vPtr->arrayName, (char *) NULL,
        TRACE_ALL | vPtr->varFlags, (Tcl_VarTraceProc *) VectorVarTrace, vPtr);

    /* Clear all the element entries from the entire array */
    Tcl_UnsetVar2(interp, vPtr->arrayName, (char *) NULL, vPtr->varFlags);

    /* Restore the "end" index by default and the trace on the entire array */
    Tcl_SetVar2(interp, vPtr->arrayName, "end", "", vPtr->varFlags);
    /* TODO I added a cast to Tcl_VarTraceProc * which might cause issues. */
    Tcl_TraceVar2(interp, vPtr->arrayName, (char *) NULL,
        TRACE_ALL | vPtr->varFlags, (Tcl_VarTraceProc *) VectorVarTrace, vPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVectorChangeLength --
 *
 *      Resizes the vector to the new size.
 *
 *      The new size of the vector is computed by doubling the
 *      size of the vector until it fits the number of slots needed
 *      (designated by *length*).
 *
 *      If the new size is the same as the old, simply adjust the
 *      length of the vector.  Otherwise we're copying the data from
 *      one memory location to another. The trailing elements of the
 *      vector need to be reset to zero.
 *
 *      If the storage changed memory locations, free up the old
 *      location if it was dynamically allocated.
 *
 * Results:
 *      A standard Tcl result.  If the reallocation is successful,
 *      TCL_OK is returned, otherwise TCL_ERROR.
 *
 * Side effects:
 *      Memory for the array is reallocated.
 *
 * ----------------------------------------------------------------------
 */
int
RbcVectorChangeLength(
    RbcVectorObject * vPtr,     /* The vector to change lengths */
    int length)
{                               /* The new size of the vector */
    int             newSize;    /* Size of array in elements */
    double         *newArr;
    Tcl_FreeProc   *freeProc;

    newArr = NULL;
    newSize = 0;
    freeProc = TCL_STATIC;

    if (length > 0) {
        int             wanted, used;

        wanted = length;
        used = vPtr->length;

        /* Compute the new size by doubling old size until it's big enough */
        newSize = DEF_ARRAY_SIZE;
        if (wanted > DEF_ARRAY_SIZE) {
            while (newSize < wanted) {
                newSize += newSize;
            }
        }
        freeProc = vPtr->freeProc;
        if (newSize == vPtr->size) {
            /* Same size, use current array. */
            newArr = vPtr->valueArr;
        } else {
            /* Dynamically allocate memory for the new array. */
            newArr = (double *) ckalloc(newSize * sizeof(double));
            if (newArr == NULL) {
                Tcl_SetObjResult(vPtr->interp, Tcl_ObjPrintf(
                    "can't allocate %d elements for vector \"%s\"",
                    newSize, vPtr->name));
                return TCL_ERROR;
            }
            if (used > wanted) {
                used = wanted;
            }
            /* Copy any previous data */
            if (used > 0) {
                memcpy(newArr, vPtr->valueArr, used * sizeof(double));
            }
            freeProc = TCL_DYNAMIC;
        }
        /* Clear any new slots that we're now using in the array */
        if (wanted > used) {
            memset(newArr + used, 0, (wanted - used) * sizeof(double));
        }
    }
    if ((newArr != vPtr->valueArr) && (vPtr->valueArr != NULL)) {
        /*
         * We're not using the old storage anymore, so free it if it's
         * not static.  It's static because the user previously reset
         * the vector with a statically allocated array (setting freeProc
         * to TCL_STATIC).
         */
        if (vPtr->freeProc != TCL_STATIC) {
            if (vPtr->freeProc == TCL_DYNAMIC) {
                ckfree((char *) vPtr->valueArr);
            } else {
                (*vPtr->freeProc) ((char *) vPtr->valueArr);
            }
        }
    }
    vPtr->valueArr = newArr;
    vPtr->size = newSize;
    vPtr->length = length;
    vPtr->first = 0;
    vPtr->last = length - 1;
    vPtr->freeProc = freeProc;  /* Set the type of the new storage */
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVectorLookupName --
 *
 *      Searches for the vector associated with the name given.  Allow
 *      for a range specification.
 *
 * Results:
 *      Returns a pointer to the vector if found, otherwise NULL.
 *      If the name is not associated with a vector and the
 *      TCL_LEAVE_ERR_MSG flag is set, and interp->result will contain
 *      an error message.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
int
RbcVectorLookupName(
    RbcVectorInterpData * dataPtr,      /* Interpreter-specific data. */
    const char *vecName,
    RbcVectorObject ** vPtrPtr)
{
    RbcVectorObject *vPtr;
    char           *endPtr;

    vPtr =
        RbcVectorParseElement(dataPtr->interp, dataPtr, vecName, &endPtr,
        RBC_NS_SEARCH_BOTH);
    if (vPtr == NULL) {
        return TCL_ERROR;
    }
    if (*endPtr != '\0') {
        Tcl_AppendResult(dataPtr->interp, "extra characters after vector name",
            (char *) NULL);
        return TCL_ERROR;
    }
    *vPtrPtr = vPtr;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVectorUpdateRange --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
void
RbcVectorUpdateRange(
    RbcVectorObject * vPtr)
{
    double          min, max;
    register int    i;

    min = DBL_MAX, max = -DBL_MAX;
    for (i = 0; i < vPtr->length; i++) {
        if (!TclIsInfinite(vPtr->valueArr[i])) {
            min = max = vPtr->valueArr[i];
            break;
        }
    }
    for ( /* empty */ ; i < vPtr->length; i++) {
        if (!TclIsInfinite(vPtr->valueArr[i])) {
            if (min > vPtr->valueArr[i]) {
                min = vPtr->valueArr[i];
            } else if (max < vPtr->valueArr[i]) {
                max = vPtr->valueArr[i];
            }
        }
    }
    vPtr->min = min;
    vPtr->max = max;
    vPtr->notifyFlags &= ~RBC_UPDATE_RANGE;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVectorGetIndex --
 *
 *      Converts the string representing an index in the vector to
 *      its numeric value.  A valid index may be an numeric string or
 *      the string "end" (indicating the last element in the string).
 *
 * Results:
 *      A standard Tcl result.  If the string is a valid index, TCL_OK
 *      is returned.  Otherwise TCL_ERROR is returned and interp->result
 *      will contain an error message.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
int
RbcVectorGetIndex(
    Tcl_Interp * interp,
    RbcVectorObject * vPtr,
    const char *string,
    int *indexPtr,              /* index to convert */
    int flags,
    RbcVectorIndexProc ** procPtrPtr)
{
    char            c;
    int             value;

    c = string[0];

    /* Treat the index "end" like a numeric index.  */

    if ((c == 'e') && (strcmp(string, "end") == 0)) {
        if (vPtr->length < 1) {
            if (interp != NULL) {
                Tcl_AppendResult(interp, "bad index \"end\": vector is empty",
                    (char *) NULL);
            }
            return TCL_ERROR;
        }
        *indexPtr = vPtr->length - 1;
        return TCL_OK;
    } else if ((c == '+') && (strcmp(string, "++end") == 0)) {
        *indexPtr = vPtr->length;
        return TCL_OK;
    }
    if (procPtrPtr != NULL) {
        Tcl_HashEntry  *hPtr;

        hPtr = Tcl_FindHashEntry(&(vPtr->dataPtr->indexProcTable), string);
        if (hPtr != NULL) {
            *indexPtr = RBC_SPECIAL_INDEX;
            *procPtrPtr = (RbcVectorIndexProc *) Tcl_GetHashValue(hPtr);
            return TCL_OK;
        }
    }
    if (Tcl_GetInt(interp, (char *) string, &value) != TCL_OK) {
        long int        lvalue;
        /*
         * Unlike Tcl_GetInt, Tcl_ExprLong needs a valid interpreter,
         * but the interp passed in may be NULL.  So we have to use
         * vPtr->interp and then reset the result.
         */
        if (Tcl_ExprLong(vPtr->interp, (char *) string, &lvalue) != TCL_OK) {
            Tcl_ResetResult(vPtr->interp);
            if (interp != NULL) {
                Tcl_AppendResult(interp, "bad index \"", string, "\"",
                    (char *) NULL);
            }
            return TCL_ERROR;
        }
        value = lvalue;
    }
    /*
     * Correct the index by the current value of the offset. This makes
     * all the numeric indices non-negative, which is how we distinguish
     * the special non-numeric indices.
     */
    value -= vPtr->offset;

    if ((value < 0) || ((flags & RBC_INDEX_CHECK) && (value >= vPtr->length))) {
        if (interp != NULL) {
            Tcl_AppendResult(interp, "index \"", string, "\" is out of range",
                (char *) NULL);
        }
        return TCL_ERROR;
    }
    *indexPtr = (int) value;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVectorGetIndexRange --
 *
 *      Converts the string representing of an index in the vector to
 *      its numeric value.  A valid index may be an numeric string or
 *      the string "end" (indicating the last element in the string).
 *
 * Results:
 *      A standard Tcl result.  If the string is a valid index, TCL_OK
 *      is returned.  Otherwise TCL_ERROR is returned and interp->result
 *      will contain an error message.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
int
RbcVectorGetIndexRange(
    Tcl_Interp * interp,        /* The interpreter to return results to */
    RbcVectorObject * vPtr,     /* The vector object to get the range from */
    const char *string,         /* The index in the vector to convert */
    int flags,                  /* The flags for special cases */
    RbcVectorIndexProc ** procPtrPtr)
{                               /* The index procedure */
    int             ielem;
    char           *colon;

    colon = NULL;
    if (flags & RBC_INDEX_COLON) {
        colon = strchr(string, ':');
    }
    if (colon != NULL) {
        /* there is a colon in the index specification */
        if (string == colon) {
            vPtr->first = 0;    /* Default to the first index */
        } else {
            int             result;

            *colon = '\0';
            result =
                RbcVectorGetIndex(interp, vPtr, string, &ielem, flags,
                (RbcVectorIndexProc **) NULL);
            *colon = ':';
            if (result != TCL_OK) {
                return TCL_ERROR;
            }
            vPtr->first = ielem;
        }
        if (*(colon + 1) == '\0') {
            /* Default to the last index */
            vPtr->last = (vPtr->length > 0) ? vPtr->length - 1 : 0;
        } else {
            if (RbcVectorGetIndex(interp, vPtr, colon + 1, &ielem, flags,
                    (RbcVectorIndexProc **) NULL) != TCL_OK) {
                return TCL_ERROR;
            }
            vPtr->last = ielem;
        }
        if (vPtr->first > vPtr->last) {
            if (interp != NULL) {
                Tcl_AppendResult(interp, "bad range \"", string,
                    "\" (first > last)", (char *) NULL);
            }
            return TCL_ERROR;
        }
    } else {
        /* there is no colon in the index */
        if (RbcVectorGetIndex(interp, vPtr, string, &ielem, flags,
                procPtrPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        vPtr->last = vPtr->first = ielem;
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVectorParseElement --
 *
 *      TODO: Description
 *
 * Results:
 *      A vector object
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
RbcVectorObject *
RbcVectorParseElement(
    Tcl_Interp * interp,
    RbcVectorInterpData * dataPtr,      /* Interpreter-specific data. */
    const char *start,          /* name of the vector */
    char **endPtr,              /* ? */
    int flags)
{                               /* RBC_NS_SEARCH_CURRENT nd such ... */
    register char  *p;
    char            saved;
    RbcVectorObject *vPtr;

    p = (char *) start;
    /* Find the end of the vector name */
    while (VECTOR_CHAR(*p)) {
        p++;
    }
    saved = *p;
    *p = '\0';

    vPtr = GetVectorObject(dataPtr, start, flags);
    if (vPtr == NULL) {
        if (interp != NULL) {
            Tcl_AppendResult(interp, "can't find vector \"", start, "\"",
                (char *) NULL);
        }
        *p = saved;
        return NULL;
    }
    *p = saved;
    vPtr->first = 0;
    vPtr->last = vPtr->length - 1;
    if (*p == '(') {
        int             count, result;

        start = p + 1;
        p++;

        /* Find the matching right parenthesis */
        count = 1;
        while (*p != '\0') {
            if (*p == ')') {
                count--;
                if (count == 0) {
                    break;
                }
            } else if (*p == '(') {
                count++;
            }
            p++;
        }
        if (count > 0) {
            if (interp != NULL) {
                Tcl_AppendResult(interp, "unbalanced parentheses \"", start,
                    "\"", (char *) NULL);
            }
            return NULL;
        }
        *p = '\0';
        result =
            RbcVectorGetIndexRange(interp, vPtr, start,
            (RBC_INDEX_COLON | RBC_INDEX_CHECK), (RbcVectorIndexProc **) NULL);
        *p = ')';
        if (result != TCL_OK) {
            return NULL;
        }
        p++;
    }
    if (endPtr != NULL) {
        *endPtr = p;
    }
    return vPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVectorUpdateClients --
 *
 *      Notifies each client of the vector that the vector has changed
 *      (updated or destroyed) by calling the provided function back.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The individual client callbacks are eventually invoked.
 *
 * ----------------------------------------------------------------------
 */
void
RbcVectorUpdateClients(
    RbcVectorObject * vPtr)
{                               /* The vector to update clients for */
    vPtr->dirty++;
    vPtr->max = vPtr->min = rbcNaN;
    if (vPtr->notifyFlags & NOTIFY_NEVER) {
        return;
    }
    vPtr->notifyFlags |= NOTIFY_UPDATED;
    if (vPtr->notifyFlags & NOTIFY_ALWAYS) {
        VectorNotifyClients(vPtr);
        return;
    }
    if (!(vPtr->notifyFlags & NOTIFY_PENDING)) {
        vPtr->notifyFlags |= NOTIFY_PENDING;
        Tcl_DoWhenIdle(VectorNotifyClients, vPtr);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * VectorVarTrace --
 *
 *      Procedure invoked when a vector variable is read, written or unset
 *
 * Results:
 *      Returns NULL on success.  Returns an error message on failure.
 *      Only called from a variable trace.
 *
 * Side effects:
 *       may be several, like deleting a vector, etc.
 *
 * ----------------------------------------------------------------------
 */
static const char *
VectorVarTrace(
    ClientData clientData,      /* Vector object. */
    Tcl_Interp * interp,        /* Interpreter of the vector */
    const char *part1,          /* name of array variable accessed */
    const char *part2,          /* name of array element accessed */
    int flags)
{
    RbcVectorIndexProc *indexProc;
    RbcVectorObject *vPtr = clientData;
    int             first, last;
    int             varFlags;

    static char     message[MAX_ERR_MSG + 1];

    if (part2 == NULL) {
        if (flags & TCL_TRACE_UNSETS) {
            /* vector is deleted via an unset on the whole array variable */
            ckfree((char *) vPtr->arrayName);
            vPtr->arrayName = NULL;
            if (vPtr->freeOnUnset) {
                RbcVectorFree(vPtr);
            }
        }
        return NULL;
    }
    if (RbcVectorGetIndexRange(interp, vPtr, part2, RBC_INDEX_ALL_FLAGS,
            &indexProc) != TCL_OK) {
        goto error;
    }
    first = vPtr->first;
    last = vPtr->last;
    varFlags = TCL_LEAVE_ERR_MSG | (TCL_GLOBAL_ONLY & flags);
    if (flags & TCL_TRACE_WRITES) {
        double          value;
        Tcl_Obj        *objPtr;

        if (first == RBC_SPECIAL_INDEX) {
            /* Tried to set "min" or "max" */
            return "read-only index";
        }
        objPtr = Tcl_GetVar2Ex(interp, part1, part2, varFlags);
        if (objPtr == NULL) {
            goto error;
        }
        if (RbcGetDouble(interp, objPtr, &value) != TCL_OK) {
            if ((last == first) && (first >= 0)) {
                /* Single numeric index. Reset the array element to
                 * its old value on errors */
                Tcl_SetVar2Ex(interp, part1, part2, objPtr, varFlags);
            }
            goto error;
        }
        if (first == vPtr->length) {
            if (RbcVectorChangeLength(vPtr, vPtr->length + 1) != TCL_OK) {
                return "error resizing vector";
            }
        }
        /* Set possibly an entire range of values */
        RbcReplicateValue(vPtr, first, last, value);
    } else if (flags & TCL_TRACE_READS) {
        double          value;
        Tcl_Obj        *objPtr;

        if (vPtr->length == 0) {
            if (Tcl_SetVar2(interp, part1, part2, "", varFlags) == NULL) {
                goto error;
            }
            return NULL;
        }
        if (first == vPtr->length) {
            return "write-only index";
        }
        if (first == last) {
            if (first >= 0) {
                value = vPtr->valueArr[first];
            } else {
                vPtr->first = 0, vPtr->last = vPtr->length - 1;
                value = (*indexProc) ((RbcVector *) vPtr);
            }
            objPtr = Tcl_NewDoubleObj(value);
            if (Tcl_SetVar2Ex(interp, part1, part2, objPtr, varFlags) == NULL) {
                Tcl_DecrRefCount(objPtr);
                goto error;
            }
        } else {
            objPtr = RbcGetValues(vPtr, first, last);
            if (Tcl_SetVar2Ex(interp, part1, part2, objPtr, varFlags) == NULL) {
                Tcl_DecrRefCount(objPtr);
                goto error;
            }
        }
    } else if (flags & TCL_TRACE_UNSETS) {
        register int    i, j;

        if ((first == vPtr->length) || (first == RBC_SPECIAL_INDEX)) {
            return "special vector index";
        }
        /*
         * Collapse the vector from the point of the first unset element.
         * Also flush any array variable entries so that the shift is
         * reflected when the array variable is read.
         */
        for (i = first, j = last + 1; j < vPtr->length; i++, j++) {
            vPtr->valueArr[i] = vPtr->valueArr[j];
        }
        vPtr->length -= ((last - first) + 1);
        if (vPtr->flush) {
            VectorFlushCache(vPtr);
        }
    } else {
        return "unknown variable trace flag";
    }
    if (flags & (TCL_TRACE_UNSETS | TCL_TRACE_WRITES)) {
        RbcVectorUpdateClients(vPtr);
    }
    Tcl_ResetResult(interp);
    return NULL;

  error:
    strncpy(message, Tcl_GetStringResult(interp), MAX_ERR_MSG);
    message[MAX_ERR_MSG] = '\0';
    return message;
}

/*
 * ----------------------------------------------------------------------
 *
 * BuildQualifiedName --
 *
 *      Builds a fully qualified name from a given name depending on the current namespace
 *
 *        - lookup current namespace
 *        - if name starts with :: -> do nothing
 *        - if name does not start with :: -> set name relative to current namespace
 *
 *      (used in VectorCreate)
 *
 * Results:
 *      Returns the qualified name
 *
 * Side effects:
 *      fullName is filled with the qualified name
 *
 * ----------------------------------------------------------------------
 */
static const char *
BuildQualifiedName(
    Tcl_Interp * interp,        /* the interpreter in which to lookup the variable or command */
    const char *name,           /* the name of a variable, or command to build the qualified name for */
    Tcl_DString * fullName)
{                               /* string pointer to save the qualified name into
                                 * (free or uninitialized DString) */
    Tcl_Namespace  *nsPtr;

    if (name == NULL) {
        return NULL;
    }

    Tcl_DStringInit(fullName);
    /* FIXME: Doesn't work in Tcl 8.4 */
    nsPtr = Tcl_GetCurrentNamespace(interp);

    if ((name[0] == ':') && (name[1] == ':')) {
        /* we have a fully qualified name already -> just return the given name */
        Tcl_DStringAppend(fullName, name, -1);
        return Tcl_DStringValue(fullName);
    }

    /* build a qualified name */
    Tcl_DStringAppend(fullName, nsPtr->fullName, -1);
    if (Tcl_DStringLength(fullName) > 2) {
        /* namespace is not the root namespace, so we need a separator */
        Tcl_DStringAppend(fullName, "::", -1);
    }
    Tcl_DStringAppend(fullName, name, -1);
    return Tcl_DStringValue(fullName);
}

/*
 * ----------------------------------------------------------------------
 *
 * ParseQualifiedName --
 *
 *      Parses a possibly namespaced (variable) name
 *      and checkes whether the corresponding namespace
 *      exists or not. Splits the name into its components
 *      as the namespace part and the name itself
 *
 *      This function is the counterpart of GetQualifiedName
 *
 * Results:
 *      A standard Tcl result. Returns TCL_ERROR if the namespace does
 *      not exist yet, else returns TCL_OK
 *
 * Side effects:
 *      If TCL_OK is returned, the nsPtr contains the namespace
 *      and namePtr contains the name of the vector in that namespace
 *
 * ----------------------------------------------------------------------
 */
static int
ParseQualifiedName(
    Tcl_Interp * interp,        /* the interpreter, where the name is found in */
    const char *qualName,       /* the qualified name to parse */
    Tcl_Namespace ** nsPtrPtr,  /* pointer to store the namespace part into */
    const char **namePtrPtr)
{                               /* pointer to store the name itself into */
    register char  *p, *colon;
    Tcl_Namespace  *nsPtr;

    colon = NULL;
    p = (char *) (qualName + strlen(qualName));
    while (--p > qualName) {
        if ((*p == ':') && (*(p - 1) == ':')) {
            p++;                /* just after the last "::" */
            colon = p - 2;
            break;
        }
    }
    if (colon == NULL) {
        *nsPtrPtr = NULL;
        *namePtrPtr = (char *) qualName;
        return TCL_OK;
    }
    *colon = '\0';
    if (qualName[0] == '\0') {
        nsPtr = Tcl_GetGlobalNamespace(interp);
    } else {
        nsPtr = Tcl_FindNamespace(interp, (char *) qualName,
            (Tcl_Namespace *) NULL, 0);
    }
    *colon = ':';
    if (nsPtr == NULL) {
        return TCL_ERROR;
    }
    *nsPtrPtr = nsPtr;
    *namePtrPtr = p;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * GetQualifiedName --
 *
 *      Builds a namespaced variable name
 *      from a namespace and a variable name specification
 *
 *      This function is the counterpart of ParseQualifiedName
 *
 * Results:
 *      A namespaced Tcl name
 *
 * Side effects:
 *      fills the supplied DString with the qualified name
 *
 * ----------------------------------------------------------------------
 */
static const char *
GetQualifiedName(
    Tcl_Namespace * nsPtr,
    const char *name,
    Tcl_DString * resultPtr)
{
    Tcl_DStringInit(resultPtr);
    if ((nsPtr->fullName[0] != ':') || (nsPtr->fullName[1] != ':')
        || (nsPtr->fullName[2] != '\0')) {
        Tcl_DStringAppend(resultPtr, nsPtr->fullName, -1);
    }
    Tcl_DStringAppend(resultPtr, "::", -1);
    Tcl_DStringAppend(resultPtr, (char *) name, -1);
    return Tcl_DStringValue(resultPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * GetVectorObject --
 *
 *      Searches for the vector associated with the name given.
 *      Allow for a range specification.
 *
 * Results:
 *      Returns a pointer to the vector if found, otherwise NULL.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static RbcVectorObject *
GetVectorObject(
    RbcVectorInterpData * dataPtr,      /* Interpreter-specific data. */
    const char *name,
    int flags)
{
    const char     *vecName;
    Tcl_Namespace  *nsPtr;
    RbcVectorObject *vPtr;

    nsPtr = NULL;
    vecName = name;
    if (ParseQualifiedName(dataPtr->interp, name, &nsPtr, &vecName) != TCL_OK) {
        return NULL;            /* Can't find namespace. */
    }
    vPtr = NULL;
    if (nsPtr != NULL) {
        vPtr = FindVectorInNamespace(dataPtr, nsPtr, vecName);
    } else {
        if (flags & RBC_NS_SEARCH_CURRENT) {
            nsPtr = Tcl_GetCurrentNamespace(dataPtr->interp);
            vPtr = FindVectorInNamespace(dataPtr, nsPtr, vecName);
        }
        if ((vPtr == NULL) && (flags & RBC_NS_SEARCH_GLOBAL)) {
            nsPtr = Tcl_GetGlobalNamespace(dataPtr->interp);
            vPtr = FindVectorInNamespace(dataPtr, nsPtr, vecName);
        }
    }
    return vPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * FindVectorInNamespace --
 *
 *      Retrieves the vector indicated when it is located in
 *      a certain namespace.
 *
 * Results:
 *      Returns a pointer to the vector if found, otherwise NULL.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static RbcVectorObject *
FindVectorInNamespace(
    RbcVectorInterpData * dataPtr,      /* Interpreter-specific data. */
    Tcl_Namespace * nsPtr,      /* Namespace pointer */
    const char *vecName)
{                               /* Name of the vector to find */
    Tcl_DString     dString;
    const char     *name;
    Tcl_HashEntry  *hPtr;

    name = GetQualifiedName(nsPtr, vecName, &dString);
    hPtr = Tcl_FindHashEntry(&(dataPtr->vectorTable), name);
    Tcl_DStringFree(&dString);
    if (hPtr != NULL) {
        return (RbcVectorObject *) Tcl_GetHashValue(hPtr);
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcGetValues --
 *
 *      Return a list containing the values of the vector
 *
 * Results:
 *      Returns a Tcl_Obj pointer to a list of doubles
 *      representing the values of the vector.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
Tcl_Obj        *
RbcGetValues(
    RbcVectorObject * vPtr,
    int first,
    int last)
{
    register int    i;
    Tcl_Obj        *listObjPtr;

    listObjPtr = Tcl_NewListObj(0, NULL);
    for (i = first; i <= last; i++) {
        Tcl_ListObjAppendElement(vPtr->interp, listObjPtr,
            Tcl_NewDoubleObj(vPtr->valueArr[i]));
    }
    return listObjPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcReplicateValue --
 *
 *      Sets the value into the array from the first to last index.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *       Sets new value for vector from first to last.
 *
 * ----------------------------------------------------------------------
 */
void
RbcReplicateValue(
    RbcVectorObject * vPtr,     /* The vector to replicate values on */
    int first,                  /* The start index to replicate into */
    int last,                   /* The end index to replicate into */
    double value)
{                               /* The value to replicate */
    register int    i;

    for (i = first; i <= last; i++) {
        vPtr->valueArr[i] = value;
    }
    vPtr->notifyFlags |= RBC_UPDATE_RANGE;
}

/*
 * ----------------------------------------------------------------------
 *
 * DeleteCommand --
 *
 *      Deletes the Tcl command associated with the vector, without
 *      triggering a callback to "VectorInstDeleteProc".
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 * ----------------------------------------------------------------------
 */
static void
DeleteCommand(
    RbcVectorObject * vPtr)
{                               /* Vector associated with the Tcl command. */
    Tcl_Interp     *interp = vPtr->interp;
    Tcl_CmdInfo     cmdInfo;
    const char     *cmdName;

    cmdName = Tcl_GetCommandName(interp, vPtr->cmdToken);

    if (Tcl_GetCommandInfo(interp, cmdName, &cmdInfo)) {
        /* Disable the callback before deleting the Tcl command. */
        cmdInfo.deleteProc = NULL;
        Tcl_SetCommandInfo(interp, cmdName, &cmdInfo);
        Tcl_DeleteCommand(interp, cmdName);
    }
    vPtr->cmdToken = 0;
}

/*
 * ----------------------------------------------------------------------
 *
 * UnmapVariable --
 *
 *      Destroys the trace on the current Tcl variable designated
 *      to access the vector.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void
UnmapVariable(
    RbcVectorObject * vPtr)
{                               /* Vector to unmap */
    Tcl_Interp     *interp = vPtr->interp;

    if (vPtr->arrayName == NULL) {
        return;
    }

    /* Unset the entire array */
    Tcl_UntraceVar2(interp, vPtr->arrayName, NULL,
        (TRACE_ALL | vPtr->varFlags), (Tcl_VarTraceProc *) VectorVarTrace,
        vPtr);
    Tcl_UnsetVar2(interp, vPtr->arrayName, (char *) NULL, vPtr->varFlags);

    /* free the space */
    ckfree((char *) vPtr->arrayName);
    vPtr->arrayName = NULL;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcGetDouble --
 *
 *      Returns a double from the Tcl_Obj provided.
 *
 * Results:
 *      Success of failure and the value object.
 *
 * Side effects:
 *       None.
 *
 * ----------------------------------------------------------------------
 */

int
RbcGetDouble(
    Tcl_Interp * interp,        /* Tcl Interp to use for extracting. */
    Tcl_Obj * objPtr,           /* The object holding the double value */
    double *valuePtr)
{                               /* Return value for the double */
    /* First try to extract the value as a double precision number. */
    if (Tcl_GetDoubleFromObj(interp, objPtr, valuePtr) == TCL_OK) {
        return TCL_OK;
    }
    Tcl_ResetResult(interp);

    /* Then try to parse it as an expression. */
    if (Tcl_ExprDouble(interp, Tcl_GetString(objPtr), valuePtr) == TCL_OK) {
        return TCL_OK;
    }
    return TCL_ERROR;
}

/*
 *--------------------------------------------------------------
 *
 * RbcFreeVectorId --
 *
 *      Releases the token for an existing vector.  This
 *      indicates that the client is no longer interested
 *      the vector.  Any previously specified callback
 *      routine will no longer be invoked when (and if) the
 *      vector changes.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Any previously specified callback routine will no
 *      longer be invoked when (and if) the vector changes.
 *
 *--------------------------------------------------------------
 */
void
RbcFreeVectorId(
    RbcVectorId clientId)
{                               /* Client token identifying the vector */
    VectorClient   *clientPtr = (VectorClient *) clientId;

    if (clientPtr->magic != VECTOR_MAGIC) {
        return;                 /* Not a valid token */
    }
    if (clientPtr->serverPtr != NULL) {
        /* Remove the client from the server's list */
        RbcChainDeleteLink(clientPtr->serverPtr->chainPtr, clientPtr->linkPtr);
    }
    ckfree((char *) clientPtr);
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcGetVectorById --
 *
 *      Returns a pointer to the vector associated with the client
 *      token.
 *
 * Results:
 *      A standard Tcl result.  If the client token is not associated
 *      with a vector any longer, TCL_ERROR is returned. Otherwise,
 *      TCL_OK is returned and vecPtrPtr will point to vector.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int
RbcGetVectorById(
    Tcl_Interp * interp,
    RbcVectorId clientId,       /* Client token identifying the vector */
    RbcVector ** vecPtrPtr)
{
    VectorClient   *clientPtr = (VectorClient *) clientId;

    if (clientPtr->magic != VECTOR_MAGIC) {
        Tcl_AppendResult(interp, "bad vector token", (char *) NULL);
        return TCL_ERROR;
    }
    if (clientPtr->serverPtr == NULL) {
        Tcl_AppendResult(interp, "vector no longer exists", (char *) NULL);
        return TCL_ERROR;
    }
    RbcVectorUpdateRange(clientPtr->serverPtr);
    *vecPtrPtr = (RbcVector *) clientPtr->serverPtr;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVectorExists2 --
 *
 *      Returns whether the vector associated with the client token
 *      still exists.
 *
 * Results:
 *      Returns 1 is the vector still exists, 0 otherwise.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
int
RbcVectorExists2(
    Tcl_Interp * interp,
    const char *vecName)
{
    RbcVectorInterpData *dataPtr;       /* Interpreter-specific data. */

    dataPtr = RbcVectorGetInterpData(interp);
    if (GetVectorObject(dataPtr, vecName, RBC_NS_SEARCH_BOTH) != NULL) {
        return TRUE;
    }
    return FALSE;
}

/*
 *--------------------------------------------------------------
 *
 * RbcAllocVectorId --
 *
 *      Creates an identifier token for an existing vector.
 *      The identifier is used by the client routines to get
 *      call backs when (and if) the vector changes.
 *
 * Results:
 *      A standard Tcl result.  If "vecName" is not associated
 *      with a vector, TCL_ERROR is returned and interp->result
 *      is filled with an error message.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
RbcVectorId
RbcAllocVectorId(
    Tcl_Interp * interp,
    const char *name)
{
    RbcVectorInterpData *dataPtr;       /* Interpreter-specific data. */
    RbcVectorObject *vPtr;
    VectorClient   *clientPtr;
    RbcVectorId     clientId;
    int             result;
    char           *nameCopy;

    dataPtr = RbcVectorGetInterpData(interp);
    /*
     * If the vector name was passed via a read-only string (e.g. "x"),
     * the VectorParseName routine will segfault when it tries to write
     * into the string.  Therefore make a writable copy and free it
     * when we're done.
     */
    nameCopy = RbcStrdup(name);
    result = RbcVectorLookupName(dataPtr, nameCopy, &vPtr);
    ckfree((char *) nameCopy);

    if (result != TCL_OK) {
        return (RbcVectorId) 0;
    }
    /* Allocate a new client structure */
    clientPtr = RbcCalloc(1, sizeof(VectorClient));
    assert(clientPtr);
    clientPtr->magic = VECTOR_MAGIC;

    /* Add the new client to the server's list of clients */
    clientPtr->linkPtr = RbcChainAppend(vPtr->chainPtr, clientPtr);
    clientPtr->serverPtr = vPtr;
    clientId = (RbcVectorId) clientPtr;
    return clientId;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcSetVectorChangedProc --
 *
 *      Sets the routine to be called back when the vector is changed
 *      or deleted.  *clientData* will be provided as an argument. If
 *      *proc* is NULL, no callback will be made.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The designated routine will be called when the vector is changed
 *      or deleted.
 *
 * -----------------------------------------------------------------------
 */
void
RbcSetVectorChangedProc(
    RbcVectorId clientId,       /* Client token identifying the vector */
    RbcVectorChangedProc * proc,        /* Address of routine to call when the contents
                                         * of the vector change. If NULL, no routine
                                         * will be called */
    ClientData clientData)
{                               /* One word of information to pass along when
                                 * the above routine is called */
    VectorClient   *clientPtr = (VectorClient *) clientId;

    if (clientPtr->magic != VECTOR_MAGIC) {
        return;                 /* Not a valid token */
    }
    clientPtr->clientData = clientData;
    clientPtr->proc = proc;
}

/*
 *--------------------------------------------------------------
 *
 * RbcNameOfVectorId --
 *
 *      Returns the name of the vector (and array variable).
 *
 * Results:
 *      The name of the array variable is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
char           *
RbcNameOfVectorId(
    RbcVectorId clientId)
{                               /* Client token identifying the vector */
    VectorClient   *clientPtr = (VectorClient *) clientId;

    if ((clientPtr->magic != VECTOR_MAGIC) || (clientPtr->serverPtr == NULL)) {
        return NULL;
    }
    return clientPtr->serverPtr->name;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcGetVector --
 *
 *      Returns a pointer to the vector associated with the given name.
 *
 * Results:
 *      A standard Tcl result.  If there is no vector "name", TCL_ERROR
 *      is returned.  Otherwise TCL_OK is returned and vecPtrPtr will
 *      point to the vector.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int
RbcGetVector(
    Tcl_Interp * interp,
    const char *name,
    RbcVector ** vecPtrPtr)
{
    RbcVectorInterpData *dataPtr;       /* Interpreter-specific data. */
    RbcVectorObject *vPtr;
    char           *nameCopy;
    int             result;

    dataPtr = RbcVectorGetInterpData(interp);
    /*
     * If the vector name was passed via a read-only string (e.g. "x"),
     * the VectorParseName routine will segfault when it tries to write
     * into the string.  Therefore make a writable copy and free it
     * when we're done.
     */
    nameCopy = RbcStrdup(name);
    result = RbcVectorLookupName(dataPtr, nameCopy, &vPtr);
    ckfree((char *) nameCopy);
    if (result != TCL_OK) {
        return TCL_ERROR;
    }
    RbcVectorUpdateRange(vPtr);
    *vecPtrPtr = (RbcVector *) vPtr;
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcCreateVector --
 *
 *      Creates a new vector by the name and size.
 *
 * Results:
 *      A standard Tcl result.  If the new array size is invalid or a
 *      vector already exists by that name, TCL_ERROR is returned.
 *      Otherwise TCL_OK is returned and the new vector is created.
 *
 * Side Effects:
 *      Memory will be allocated for the new vector.  A new Tcl command
 *      and Tcl array variable will be created.
 *
 * -----------------------------------------------------------------------
 */
int
RbcCreateVector2(
    Tcl_Interp * interp,
    const char *vecName,
    const char *cmdName,
    const char *varName,
    int initialSize,
    RbcVector ** vecPtrPtr)
{
    RbcVectorInterpData *dataPtr;       /* Interpreter-specific data. */
    RbcVectorObject *vPtr;
    int             isNew;
    char           *nameCopy;

    if (initialSize < 0) {
        Tcl_AppendPrintfToObj(Tcl_GetObjResult(interp),
                    "bad vector size \"%d\"",initialSize);
        return TCL_ERROR;
    }
    dataPtr = RbcVectorGetInterpData(interp);

    nameCopy = RbcStrdup(vecName);
    vPtr = RbcVectorCreate(dataPtr, nameCopy, cmdName, varName, &isNew);
    ckfree((char *) nameCopy);

    if (vPtr == NULL) {
        return TCL_ERROR;
    }
    if (initialSize > 0) {
        if (RbcVectorChangeLength(vPtr, initialSize) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (vecPtrPtr != NULL) {
        *vecPtrPtr = (RbcVector *) vPtr;
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * RbcCreateVector --
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
RbcCreateVector(
    Tcl_Interp * interp,
    const char *name,
    int size,
    RbcVector ** vecPtrPtr)
{
    return RbcCreateVector2(interp, name, name, name, size, vecPtrPtr);
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcResizeVector --
 *
 *      Changes the size of the vector.  All clients with designated
 *      callback routines will be notified of the size change.
 *
 * Results:
 *      A standard Tcl result.  If no vector exists by that name,
 *      TCL_ERROR is returned.  Otherwise TCL_OK is returned and
 *      vector is resized.
 *
 * Side Effects:
 *      Memory may be reallocated for the new vector size.  All clients
 *      which set call back procedures will be notified.
 *
 * -----------------------------------------------------------------------
 */
int
RbcResizeVector(
    RbcVector * vecPtr,
    int length)
{
    RbcVectorObject *vPtr = (RbcVectorObject *) vecPtr;

    if (RbcVectorChangeLength(vPtr, length) != TCL_OK) {
        Tcl_AppendResult(vPtr->interp, "can't resize vector \"", vPtr->name,
            "\"", (char *) NULL);
        return TCL_ERROR;
    }
    if (vPtr->flush) {
        RbcVectorFlushCache(vPtr);
    }
    RbcVectorUpdateClients(vPtr);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * RbcNameOfVector --
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
RbcNameOfVector(
    RbcVector * vecPtr)
{                               /* Vector to query. */
    RbcVectorObject *vPtr = (RbcVectorObject *) vecPtr;
    return vPtr->name;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcResetVector --
 *
 *      Resets the vector data.  This is called by a client to
 *      indicate that the vector data has changed.  The vector does
 *      not need to point to different memory.  Any clients of the
 *      vector will be notified of the change.
 *
 * Results:
 *      A standard Tcl result.  If the new array size is invalid,
 *      TCL_ERROR is returned.  Otherwise TCL_OK is returned and the
 *      new vector data is recorded.
 *
 * Side Effects:
 *      Any client designated callbacks will be posted.  Memory may
 *      be changed for the vector array.
 *
 * -----------------------------------------------------------------------
 */
int
RbcResetVector(
    RbcVector * vecPtr,
    double *valueArr,           /* Array containing the elements of the
                                 * vector. If NULL, indicates to reset the
                                 * vector.*/
    int length,                 /* The number of elements that the vector
                                 * currently holds. */
    int size,                   /* The maximum number of elements that the
                                 * array can hold. */
    Tcl_FreeProc * freeProc)
{                               /* Address of memory deallocation routine
                                 * for the array of values.  Can also be
                                 * TCL_STATIC, TCL_DYNAMIC, or TCL_VOLATILE. */
    RbcVectorObject *vPtr = (RbcVectorObject *) vecPtr;

    if (size < 0) {
        Tcl_AppendResult(vPtr->interp, "bad array size", (char *) NULL);
        return TCL_ERROR;
    }
    return RbcVectorReset(vPtr, valueArr, length, size, freeProc);
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
