/*
 * rbcGrPen.c --
 *
 *      This module implements pens for the rbc graph widget.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkoGraph.h"

static Tk_OptionParseProc StringToColor;
static Tk_OptionPrintProc ColorToString;
static Tk_OptionParseProc StringToPen;
static Tk_OptionPrintProc PenToString;
Tk_CustomOption rbcColorOption = {
    StringToColor, ColorToString, (ClientData) 0
};

Tk_CustomOption rbcPenOption = {
    StringToPen, PenToString, (ClientData) 0
};

Tk_CustomOption rbcBarPenOption = {
    StringToPen, PenToString, (ClientData) & rbcBarElementUid
};

Tk_CustomOption rbcLinePenOption = {
    StringToPen, PenToString, (ClientData) & rbcLineElementUid
};

static const char *NameOfColor(
    XColor * colorPtr);
static RbcPen *NameToPen(
    RbcGraph * graph,
    const char *name);
static int CgetOp(
    Tcl_Interp * interp,
    RbcGraph * graph,
    int argc,
    const char *argv[]);
static int ConfigureOp(
    Tcl_Interp * interp,
    RbcGraph * graph,
    int argc,
    const char *argv[]);
static int CreateOp(
    Tcl_Interp * interp,
    RbcGraph * graph,
    int argc,
    const char **argv);
static int DeleteOp(
    Tcl_Interp * interp,
    RbcGraph * graph,
    int argc,
    const char **argv);
static int NamesOp(
    Tcl_Interp * interp,
    RbcGraph * graph,
    int argc,
    const char **argv);
static int TypeOp(
    Tcl_Interp * interp,
    RbcGraph * graph,
    int argc,
    const char **argv);

/*
 *----------------------------------------------------------------------

 * StringToColor --
 *
 *      Convert the string representation of a color into a XColor pointer.
 *
 * Results:
 *      The return value is a standard Tcl result.  The color pointer is
 *      written into the widget record.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
 */
static int
StringToColor(
    ClientData clientData,     /* Not used. */
    Tcl_Interp * interp,       /* Interpreter to send results back to */
    Tk_Window tkwin,           /* Not used. */
    const char *string,        /* String representing color */
    char *widgRec,             /* Widget record */
    int offset)
{              /* Offset of color field in record */
    XColor **colorPtrPtr = (XColor **) (widgRec + offset);
    XColor *colorPtr;
    unsigned int length;
    char c;

    if((string == NULL) || (*string == '\0')) {
        *colorPtrPtr = NULL;
        return TCL_OK;
    }
    c = string[0];
    length = strlen(string);

    if((c == 'd') && (strncmp(string, "defcolor", length) == 0)) {
        colorPtr = RBC_COLOR_DEFAULT;
    } else {
        colorPtr = Tk_GetColor(interp, tkwin, Tk_GetUid(string));
        if(colorPtr == NULL) {
            return TCL_ERROR;
        }
    }
    *colorPtrPtr = colorPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * NameOfColor --
 *
 *      Convert the color option value into a string.
 *
 * Results:
 *      The static string representing the color option is returned.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
 */
static const char *
NameOfColor(
    XColor * colorPtr)
{
    if(colorPtr == NULL) {
        return "";
    } else if(colorPtr == RBC_COLOR_DEFAULT) {
        return "defcolor";
    } else {
        return Tk_NameOfColor(colorPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ColorToString --
 *
 *      Convert the color value into a string.
 *
 * Results:
 *      The string representing the symbol color is returned.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
 */
static const char *
ColorToString(
    ClientData clientData,     /* Not used. */
    Tk_Window tkwin,           /* Not used. */
    char *widgRec,             /* Widget information record */
    int offset,                /* Offset of symbol type in record */
    Tcl_FreeProc ** freeProcPtr)
{              /* Not used. */
    XColor *colorPtr = *(XColor **) (widgRec + offset);

    return NameOfColor(colorPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * StringToPen --
 *
 *      Convert the color value into a string.
 *
 * Results:
 *      The string representing the symbol color is returned.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
 */
static int
StringToPen(
    ClientData clientData,     /* Not used. */
    Tcl_Interp * interp,       /* Interpreter to send results back to */
    Tk_Window tkwin,           /* Not used. */
    const char *string,        /* String representing pen */
    char *widgRec,             /* Widget record */
    int offset)
{              /* Offset of pen field in record */
    Tk_Uid classUid = *(Tk_Uid *) clientData;   /* Element type. */
    RbcPen **penPtrPtr = (RbcPen **) (widgRec + offset);
    RbcPen *penPtr;
    RbcGraph *graph;

    penPtr = NULL;
    graph = RbcGetGraphFromWindowData(tkwin);

    if(classUid == NULL) {
        classUid = graph->classUid;
    }
    if((string != NULL) && (string[0] != '\0')) {
        if(RbcGetPen(graph, string, classUid, &penPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    /* Release the old pen */
    if(*penPtrPtr != NULL) {
        RbcFreePen(graph, *penPtrPtr);
    }
    *penPtrPtr = penPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * PenToString --
 *
 *      Parse the name of the name.
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
 */
static const char *
PenToString(
    ClientData clientData,     /* Not used. */
    Tk_Window tkwin,           /* Not used. */
    char *widgRec,             /* Widget information record */
    int offset,                /* Offset of pen in record */
    Tcl_FreeProc ** freeProcPtr)
{              /* Not used. */
    RbcPen *penPtr = *(RbcPen **) (widgRec + offset);

    return penPtr->name;
}

/*
 *----------------------------------------------------------------------
 *
 * NameToPen --
 *
 *      Find and return the pen style from a given name.
 *
 * Results:
 *      A standard TCL result.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
 */
static RbcPen *
NameToPen(
    RbcGraph * graph,
    const char *name)
{
    Tcl_HashEntry *hPtr;
    RbcPen *penPtr;
    if(graph->win == NULL || *(graph->win) == NULL)
        return NULL;

    hPtr = Tcl_FindHashEntry(&(graph->penTable), name);
    if(hPtr == NULL) {
        goto notFound;
    }
    penPtr = (RbcPen *) Tcl_GetHashValue(hPtr);
    if(penPtr->flags & RBC_PEN_DELETE_PENDING) {
        goto notFound;
    }
    return penPtr;
  notFound:
    Tcl_AppendResult(graph->interp, "can't find pen \"", name,
        "\" in \"", Tk_PathName(*(graph->win)), "\"", (char *)NULL);
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyPen --
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
DestroyPen(
    RbcGraph * graph,
    RbcPen * penPtr)
{
    Tk_FreeOptions(penPtr->configSpecs, (char *)penPtr, graph->display, 0);
    (*penPtr->destroyProc) (graph, penPtr);
    if((penPtr->name != NULL) && (penPtr->name[0] != '\0')) {
        ckfree((char *)penPtr->name);
    }
    if(penPtr->hashPtr != NULL) {
        Tcl_DeleteHashEntry(penPtr->hashPtr);
    }
    ckfree((char *)penPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcFreePen --
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
RbcFreePen(
    RbcGraph * graph,
    RbcPen * penPtr)
{
    penPtr->refCount--;
    if((penPtr->refCount == 0) && (penPtr->flags & RBC_PEN_DELETE_PENDING)) {
        DestroyPen(graph, penPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcCreatePen --
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
RbcPen *
RbcCreatePen(
    RbcGraph * graph,
    const char *penName,
    Tk_Uid classUid,
    int nOpts,
    const char **options)
{

    RbcPen *penPtr;
    Tcl_HashEntry *hPtr;
    unsigned int length, configFlags;
    int isNew;
    register int i;
    if(graph->win == NULL || *(graph->win) == NULL)
        return NULL;

    /*
     * Scan the option list for a "-type" entry.  This will indicate
     * what type of pen we are creating. Otherwise we'll default to the
     * suggested type.  Last -type option wins.
     */
    for(i = 0; i < nOpts; i += 2) {
        length = strlen(options[i]);
        if((length > 2) && (strncmp(options[i], "-type", length) == 0)) {
    const char *arg;

            arg = options[i + 1];
            if(strcmp(arg, "bar") == 0) {
                classUid = rbcBarElementUid;
            } else if(strcmp(arg, "line") != 0) {
                classUid = rbcLineElementUid;
            } else if(strcmp(arg, "strip") != 0) {
                classUid = rbcLineElementUid;
            } else {
                Tcl_AppendResult(graph->interp, "unknown pen type \"",
                    arg, "\" specified", (char *)NULL);
                return NULL;
            }
        }
    }
    if(classUid == rbcStripElementUid) {
        classUid = rbcLineElementUid;
    }
    hPtr = Tcl_CreateHashEntry(&(graph->penTable), penName, &isNew);
    if(!isNew) {
        penPtr = (RbcPen *) Tcl_GetHashValue(hPtr);
        if(!(penPtr->flags & RBC_PEN_DELETE_PENDING)) {
            Tcl_AppendResult(graph->interp, "pen \"", penName,
                "\" already exists in \"", Tk_PathName(*(graph->win)), "\"",
                (char *)NULL);
            return NULL;
        }
        if(penPtr->classUid != classUid) {
            Tcl_AppendResult(graph->interp, "pen \"", penName,
                "\" in-use: can't change pen type from \"", penPtr->classUid,
                "\" to \"", classUid, "\"", (char *)NULL);
            return NULL;
        }
        penPtr->flags &= ~RBC_PEN_DELETE_PENDING;
    } else {
        if(classUid == rbcBarElementUid) {
            penPtr = RbcBarPen(penName);
        } else {
            penPtr = RbcLinePen(penName);
        }
        penPtr->classUid = classUid;
        penPtr->hashPtr = hPtr;
        Tcl_SetHashValue(hPtr, penPtr);
    }

    configFlags = (penPtr->flags & (RBC_ACTIVE_PEN | RBC_NORMAL_PEN));
    if(RbcConfigureWidgetComponent(graph->interp, *(graph->win),
            penPtr->name, "Pen", penPtr->configSpecs, nOpts, options,
            (char *)penPtr, configFlags) != TCL_OK) {
        if(isNew) {
            DestroyPen(graph, penPtr);
        }
        return NULL;
    }
    (*penPtr->configProc) (graph, penPtr);
    return penPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcGetPen --
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
RbcGetPen(
    RbcGraph * graph,
    const char *name,
    Tk_Uid classUid,
    RbcPen ** penPtrPtr)
{
    RbcPen *penPtr;

    penPtr = NameToPen(graph, name);
    if(penPtr == NULL) {
        return TCL_ERROR;
    }
    if(classUid == rbcStripElementUid) {
        classUid = rbcLineElementUid;
    }
    if(penPtr->classUid != classUid) {
        Tcl_AppendResult(graph->interp, "pen \"", name,
            "\" is the wrong type (is \"", penPtr->classUid, "\"",
            ", wanted \"", classUid, "\")", (char *)NULL);
        return TCL_ERROR;
    }
    penPtr->refCount++;
    *penPtrPtr = penPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcDestroyPens --
 *
 *      Release memory and resources allocated for the style.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Everything associated with the pen style is freed up.
 *
 *----------------------------------------------------------------------
 */
void
RbcDestroyPens(
    RbcGraph * graph)
{
Tcl_HashEntry *hPtr;
Tcl_HashSearch cursor;
RbcPen *penPtr;

    for(hPtr = Tcl_FirstHashEntry(&(graph->penTable), &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        penPtr = (RbcPen *) Tcl_GetHashValue(hPtr);
        penPtr->hashPtr = NULL;
        DestroyPen(graph, penPtr);
    }
    Tcl_DeleteHashTable(&(graph->penTable));
}

/*
 * ----------------------------------------------------------------------
 *
 * CgetOp --
 *
 *      Queries axis attributes (font, line width, label, etc).
 *
 * Results:
 *      A standard Tcl result.  If querying configuration values,
 *      interp->result will contain the results.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 * ----------------------------------------------------------------------
 */
static int
CgetOp(
    Tcl_Interp * interp,
    RbcGraph * graph,
    int argc,                  /* Not used. */
    const char *argv[])
{
    RbcPen *penPtr;
    unsigned int configFlags;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    penPtr = NameToPen(graph, argv[3]);
    if(penPtr == NULL) {
        return TCL_ERROR;
    }
    configFlags = (penPtr->flags & (RBC_ACTIVE_PEN | RBC_NORMAL_PEN));
    return Tk_ConfigureValue(interp, *(graph->win), penPtr->configSpecs,
        (char *)penPtr, argv[4], configFlags);
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      Queries or resets pen attributes (font, line width, color, etc).
 *
 * Results:
 *      A standard Tcl result.  If querying configuration values,
 *      interp->result will contain the results.
 *
 * Side Effects:
 *      Pen resources are possibly allocated (GC, font).
 *
 * ----------------------------------------------------------------------
 */
static int
ConfigureOp(
    Tcl_Interp * interp,
    RbcGraph * graph,
    int argc,
    const char *argv[])
{
    int flags;
    RbcPen *penPtr;
    int nNames, nOpts;
    int redraw;
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
        if(NameToPen(graph, argv[i]) == NULL) {
            return TCL_ERROR;
        }
    }
    nNames = i; /* Number of pen names specified */
    nOpts = argc - i;   /* Number of options specified */
    options = argv + i; /* Start of options in argv  */

    redraw = 0;
    for(i = 0; i < nNames; i++) {
        penPtr = NameToPen(graph, argv[i]);
        flags =
            TK_CONFIG_ARGV_ONLY | (penPtr->flags & (RBC_ACTIVE_PEN |
                RBC_NORMAL_PEN));
        if(nOpts == 0) {
            return Tk_ConfigureInfo(interp, *(graph->win),
                penPtr->configSpecs, (char *)penPtr, (char *)NULL, flags);
        } else if(nOpts == 1) {
            return Tk_ConfigureInfo(interp, *(graph->win),
                penPtr->configSpecs, (char *)penPtr, options[0], flags);
        }
        if(Tk_ConfigureWidget(interp, *(graph->win), penPtr->configSpecs,
                nOpts, options, (char *)penPtr, flags) != TCL_OK) {
            break;
        }
        (*penPtr->configProc) (graph, penPtr);
        if(penPtr->refCount > 0) {
            redraw++;
        }
    }
    if(redraw) {
        graph->flags |= RBC_REDRAW_BACKING_STORE | RBC_DRAW_MARGINS;
        RbcEventuallyRedrawGraph(graph);
    }
    if(i < nNames) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateOp --
 *
 *      Adds a new penstyle to the graph.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
 */
static int
CreateOp(
    Tcl_Interp * interp,
    RbcGraph * graph,
    int argc,
    const char **argv)
{
    RbcPen *penPtr;

    penPtr = RbcCreatePen(graph, argv[3], graph->classUid, argc - 4, argv + 4);
    if(penPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(penPtr->name, -1));
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * DeleteOp --
 *
 *      Delete the given pen.
 *
 * Results:
 *      Always returns TCL_OK.  The interp->result field is
 *      a list of the graph axis limits.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *--------------------------------------------------------------
 */
static int
DeleteOp(
    Tcl_Interp * interp,
    RbcGraph * graph,
    int argc,
    const char **argv)
{
    RbcPen *penPtr;
    int i;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    for(i = 3; i < argc; i++) {
        penPtr = NameToPen(graph, argv[i]);
        if(penPtr == NULL) {
            return TCL_ERROR;
        }
        if(penPtr->flags & RBC_PEN_DELETE_PENDING) {
            Tcl_AppendResult(graph->interp, "can't find pen \"", argv[i],
                "\" in \"", Tk_PathName(*(graph->win)), "\"", (char *)NULL);
            return TCL_ERROR;
        }
        penPtr->flags |= RBC_PEN_DELETE_PENDING;
        if(penPtr->refCount == 0) {
            DestroyPen(graph, penPtr);
        }
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * NamesOp --
 *
 *      Return a list of the names of all the axes.
 *
 * Results:
 *      Returns a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 * ----------------------------------------------------------------------
 */
static int
NamesOp(
    Tcl_Interp * interp,
    RbcGraph * graph,
    int argc,
    const char **argv)
{
    Tcl_HashSearch cursor;
    RbcPen *penPtr;
    register int i;
    register Tcl_HashEntry *hPtr;

    for(hPtr = Tcl_FirstHashEntry(&(graph->penTable), &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        penPtr = (RbcPen *) Tcl_GetHashValue(hPtr);
        if(penPtr->flags & RBC_PEN_DELETE_PENDING) {
            continue;
        }
        if(argc == 3) {
            Tcl_AppendElement(interp, penPtr->name);
            continue;
        }
        for(i = 3; i < argc; i++) {
            if(Tcl_StringMatch(penPtr->name, argv[i])) {
                Tcl_AppendElement(interp, penPtr->name);
                break;
            }
        }
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * TypeOp --
 *
 *      Return the type of pen.
 *
 * Results:
 *      Returns a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 * ----------------------------------------------------------------------
 */
static int
TypeOp(
    Tcl_Interp * interp,
    RbcGraph * graph,
    int argc,
    const char **argv)
{
    RbcPen *penPtr;

    penPtr = NameToPen(graph, argv[3]);
    if(penPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(penPtr->classUid, -1));
    return TCL_OK;
}

static RbcOpSpec penOps[] = {
    {"cget", 2, (RbcOp) CgetOp, 5, 5, "penName option",},
    {"configure", 2, (RbcOp) ConfigureOp, 4, 0,
        "penName ?penName?... ?option value?...",},
    {"create", 2, (RbcOp) CreateOp, 4, 0, "penName ?option value?...",},
    {"delete", 2, (RbcOp) DeleteOp, 3, 0, "?penName?...",},
    {"names", 1, (RbcOp) NamesOp, 3, 0, "?pattern?...",},
    {"type", 1, (RbcOp) TypeOp, 4, 4, "penName",},
};

static int nPenOps = sizeof(penOps) / sizeof(RbcOpSpec);

/*
 *----------------------------------------------------------------------
 *
 * RbcPenOp --
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
RbcPenOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcOp proc;

    proc = RbcGetOp(interp, nPenOps, penOps, RBC_OP_ARG2, argc, argv, 0);
    if(proc == NULL) {
        return TCL_ERROR;
    }
    return (*proc) (interp, graph, argc, argv);
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
