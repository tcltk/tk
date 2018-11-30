/*
 * tkPathSurface.c --
 *
 *    This file implements style objects used when drawing paths.
 *      See http://www.w3.org/TR/SVG11/.
 *
 * Copyright (c) 2007-2008  Mats Bengtsson
 *
 */

#include "tkPathInt.h"

typedef struct {
    Tcl_HashTable    surfaceHash;
    Tk_OptionTable     optionTableCircle;
    Tk_OptionTable     optionTableEllipse;
    Tk_OptionTable     optionTablePath;
    Tk_OptionTable     optionTablePimage;
    Tk_OptionTable     optionTablePline;
    Tk_OptionTable     optionTablePolyline;
    Tk_OptionTable     optionTablePpolygon;
    Tk_OptionTable     optionTablePrect;
    Tk_OptionTable     optionTablePtext;
    int            uid;
    Tk_Window        tkwin;
} InterpData;

typedef struct PathSurface {
    TkPathContext ctx;
    char *token;
    ClientData clientData;
    int width;
    int height;
    Tcl_HashTable *surfaceHash;
} PathSurface;

static void    StaticSurfaceEventProc(ClientData clientData, XEvent *eventPtr);
static void    StaticSurfaceObjCmdDeleted(ClientData clientData);
static int     StaticSurfaceObjCmd(ClientData clientData,
                    Tcl_Interp *interp,
                    int objc, Tcl_Obj* const objv[]);
static int     NamesSurfaceObjCmd(ClientData clientData,
                   Tcl_Interp *interp,
                   int objc, Tcl_Obj* const objv[]);
static int     NewSurfaceObjCmd(ClientData clientData,
                 Tcl_Interp *interp,
                 int objc, Tcl_Obj* const objv[]);
static int     SurfaceObjCmd(ClientData clientData,
                  Tcl_Interp *interp,
                  int objc, Tcl_Obj* const objv[]);
static int     SurfaceCopyObjCmd(Tcl_Interp *interp,
                  PathSurface *surfacePtr,
                  int objc, Tcl_Obj* const objv[]);
static int     SurfaceDestroyObjCmd(Tcl_Interp *interp,
                     PathSurface *surfacePtr);
static void    SurfaceDeletedProc(ClientData clientData);
static int     SurfaceCreateObjCmd(ClientData clientData, Tcl_Interp *interp,
                    PathSurface *surfacePtr,
                    int objc, Tcl_Obj* const objv[]);
static int     SurfaceEraseObjCmd(Tcl_Interp *interp,
                   PathSurface *surfacePtr,
                   int objc, Tcl_Obj* const objv[]);
static int    SurfaceCreateEllipse(Tcl_Interp *interp, InterpData *dataPtr,
                     PathSurface *surfacePtr,
                     int type, int objc, Tcl_Obj* const objv[]);
static int    SurfaceCreatePath(Tcl_Interp *interp, InterpData *dataPtr,
                  PathSurface *surfacePtr,
                  int objc, Tcl_Obj* const objv[]);
static int    SurfaceCreatePimage(Tcl_Interp *interp,
                    InterpData *dataPtr,
                    PathSurface *surfacePtr,
                    int objc, Tcl_Obj* const objv[]);
static int    SurfaceCreatePline(Tcl_Interp *interp, InterpData *dataPtr,
                   PathSurface *surfacePtr,
                   int objc, Tcl_Obj* const objv[]);
static int    SurfaceCreatePpoly(Tcl_Interp *interp, InterpData *dataPtr,
                   PathSurface *surfacePtr,
                   int type, int objc, Tcl_Obj* const objv[]);
static int    SurfaceCreatePrect(Tcl_Interp *interp, InterpData *dataPtr,
                   PathSurface *surfacePtr,
                   int objc, Tcl_Obj* const objv[]);
static int    SurfaceCreatePtext(Tcl_Interp *interp, InterpData *dataPtr,
                   PathSurface *surfacePtr,
                   int objc, Tcl_Obj* const objv[]);

static const char *staticSurfaceCmds[] = {
    "names", "new", (char *) NULL
};

enum {
    kPathStaticSurfaceCmdNames    = 0L,
    kPathStaticSurfaceCmdNew
};

static int
StaticSurfaceObjCmd(ClientData clientData, Tcl_Interp *interp,
            int objc, Tcl_Obj* const objv[])
{
    int index;
    int result = TCL_OK;

    if (objc < 2) {
    Tcl_WrongNumArgs(interp, 1, objv, "command ?arg arg...?");
    return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], staticSurfaceCmds, "command", 0,
        &index) != TCL_OK) {
    return TCL_ERROR;
    }
    switch (index) {
    case kPathStaticSurfaceCmdNames: {
        result = NamesSurfaceObjCmd(clientData, interp, objc, objv);
        break;
    }
    case kPathStaticSurfaceCmdNew: {
        result = NewSurfaceObjCmd(clientData, interp, objc, objv);
        break;
    }
    }
    return result;
}

static void
StaticSurfaceEventProc(ClientData clientData, XEvent *eventPtr)
{
    InterpData *dataPtr = (InterpData *) clientData;

    if (eventPtr->type == DestroyNotify) {
    dataPtr->tkwin = NULL;
    }
}

static void
StaticSurfaceObjCmdDeleted(ClientData clientData)
{
    InterpData *dataPtr = (InterpData *) clientData;

    if (dataPtr->tkwin != NULL) {
    Tk_DeleteEventHandler(dataPtr->tkwin, StructureNotifyMask,
                  StaticSurfaceEventProc, dataPtr);
    }
    Tcl_DeleteHashTable(&dataPtr->surfaceHash);
    Tk_DeleteOptionTable(dataPtr->optionTableCircle);
    Tk_DeleteOptionTable(dataPtr->optionTableEllipse);
    Tk_DeleteOptionTable(dataPtr->optionTablePath);
    Tk_DeleteOptionTable(dataPtr->optionTablePimage);
    Tk_DeleteOptionTable(dataPtr->optionTablePline);
    Tk_DeleteOptionTable(dataPtr->optionTablePolyline);
    Tk_DeleteOptionTable(dataPtr->optionTablePpolygon);
    Tk_DeleteOptionTable(dataPtr->optionTablePrect);
    Tk_DeleteOptionTable(dataPtr->optionTablePtext);
    ckfree((char *) dataPtr);
}

static int
NamesSurfaceObjCmd(ClientData clientData, Tcl_Interp *interp,
           int objc, Tcl_Obj* const objv[])
{
    InterpData        *dataPtr = (InterpData *) clientData;
    char        *name;
    Tcl_HashEntry   *hPtr;
    Tcl_Obj        *listObj;
    Tcl_HashSearch  search;

    if (objc != 2) {
    Tcl_WrongNumArgs(interp, 2, objv, NULL);
    return TCL_ERROR;
    }
    listObj = Tcl_NewListObj(0, NULL);
    hPtr = Tcl_FirstHashEntry(&dataPtr->surfaceHash, &search);
    while (hPtr != NULL) {
    name = (char *) Tcl_GetHashKey(&dataPtr->surfaceHash, hPtr);
    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj(name, -1));
    hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_SetObjResult(interp, listObj);
    return TCL_OK;
}

static int
NewSurfaceObjCmd(ClientData clientData, Tcl_Interp *interp,
         int objc, Tcl_Obj* const objv[])
{
    InterpData        *dataPtr = (InterpData *) clientData;
    TkPathContext   ctx;
    PathSurface        *surfacePtr;
    Tcl_HashEntry   *hPtr;
    char        str[255];
    int            width, height;
    int            isNew;
    int            result = TCL_OK;
    Display        *display = NULL;

    if (objc != 4) {
    Tcl_WrongNumArgs(interp, 2, objv, "width height");
    return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[2], &width) != TCL_OK) {
    return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[3], &height) != TCL_OK) {
    return TCL_ERROR;
    }

    if (dataPtr->tkwin == NULL) {
    dataPtr->tkwin = Tk_MainWindow(interp);
    if (dataPtr->tkwin != NULL) {
        Tk_CreateEventHandler(dataPtr->tkwin, StructureNotifyMask,
                  StaticSurfaceEventProc, dataPtr);
    }
    }
    if (dataPtr->tkwin != NULL) {
    display = Tk_Display(dataPtr->tkwin);
    }
    ctx = TkPathInitSurface(display, width, height);
    if (ctx == 0) {
    Tcl_SetObjResult(interp,
        Tcl_NewStringObj("Failed in TkPathInitSurface", -1));
    return TCL_ERROR;
    }

    sprintf(str, "%s%d", TK_PATHCMD_PATHSURFACE, dataPtr->uid++);
    surfacePtr = (PathSurface *) ckalloc( sizeof(PathSurface) );
    surfacePtr->token = (char *) ckalloc( (unsigned int)strlen(str) + 1 );
    strcpy(surfacePtr->token, str);
    surfacePtr->ctx = ctx;
    surfacePtr->clientData = clientData;
    surfacePtr->width = width;
    surfacePtr->height = height;
    surfacePtr->surfaceHash = &dataPtr->surfaceHash;
    Tcl_CreateObjCommand(interp, str, SurfaceObjCmd,
             (ClientData) surfacePtr, SurfaceDeletedProc);

    hPtr = Tcl_CreateHashEntry(&dataPtr->surfaceHash, str, &isNew);
    Tcl_SetHashValue(hPtr, surfacePtr);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(str, -1));
    return result;
}

static const char *surfaceCmds[] = {
    "copy",     "create",     "destroy",
    "erase",     "height",     "width",
    (char *) NULL
};

enum {
    kPathSurfaceCmdCopy        = 0L,
    kPathSurfaceCmdCreate,
    kPathSurfaceCmdDestroy,
    kPathSurfaceCmdErase,
    kPathSurfaceCmdHeight,
    kPathSurfaceCmdWidth
};

static int
SurfaceObjCmd(ClientData clientData, Tcl_Interp *interp,
          int objc, Tcl_Obj* const objv[])
{
    PathSurface *surfacePtr = (PathSurface *) clientData;
    int     index;
    int     result = TCL_OK;

    if (objc < 2) {
    Tcl_WrongNumArgs(interp, 1, objv, "command ?arg arg...?");
    return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], surfaceCmds, "command", 0,
        &index) != TCL_OK) {
    return TCL_ERROR;
    }
    switch (index) {
    case kPathSurfaceCmdCopy: {
        result = SurfaceCopyObjCmd(interp, surfacePtr, objc, objv);
        break;
    }
    case kPathSurfaceCmdCreate: {
        result = SurfaceCreateObjCmd(surfacePtr->clientData,
                     interp, surfacePtr, objc, objv);
        break;
    }
    case kPathSurfaceCmdDestroy: {
        result = SurfaceDestroyObjCmd(interp, surfacePtr);
        break;
    }
    case kPathSurfaceCmdErase: {
        result = SurfaceEraseObjCmd(interp, surfacePtr, objc, objv);
        break;
    }
    case kPathSurfaceCmdHeight:
    case kPathSurfaceCmdWidth: {
        if (objc != 2) {
        Tcl_WrongNumArgs(interp, 2, objv, NULL);
        return TCL_ERROR;
        }
        Tcl_SetObjResult(interp,
                 Tcl_NewIntObj((index == kPathSurfaceCmdHeight) ?
                       surfacePtr->height :
                       surfacePtr->width));
        break;
    }
    }
    return result;
}

static int
SurfaceCopyObjCmd(Tcl_Interp *interp, PathSurface *surfacePtr,
          int objc, Tcl_Obj* const objv[])
{
    Tk_PhotoHandle photo;

    if (objc != 3) {
    Tcl_WrongNumArgs(interp, 2, objv, "image");
    return TCL_ERROR;
    }
    photo = Tk_FindPhoto( interp, Tcl_GetString(objv[2]) );
    if (photo == NULL) {
    Tcl_SetObjResult(interp,
        Tcl_NewStringObj("didn't find that image", -1));
    return TCL_ERROR;
    }
    TkPathSurfaceToPhoto(interp, surfacePtr->ctx, photo);
    Tcl_SetObjResult(interp, objv[2]);
    return TCL_OK;
}

static int
SurfaceDestroyObjCmd(Tcl_Interp *interp, PathSurface *surfacePtr)
{
    Tcl_DeleteCommand(interp, surfacePtr->token);
    return TCL_OK;
}

static void
SurfaceDeletedProc(ClientData clientData)
{
    PathSurface *surfacePtr = (PathSurface *) clientData;
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(surfacePtr->surfaceHash, surfacePtr->token);
    if (hPtr != NULL) {
    Tcl_DeleteHashEntry(hPtr);
    }
    TkPathFree(surfacePtr->ctx);
    ckfree(surfacePtr->token);
    ckfree((char *)surfacePtr);
}

/* @@@ TODO: should we have a group item? */

static const char *surfaceItemCmds[] = {
    "circle",    "ellipse",  "path",
    "image",    "line",    "polyline",
    "polygon",  "rect",    "text",
    (char *) NULL
};

enum {
    kPathSurfaceItemCircle    = 0L,
    kPathSurfaceItemEllipse,
    kPathSurfaceItemPath,
    kPathSurfaceItemPimage,
    kPathSurfaceItemPline,
    kPathSurfaceItemPolyline,
    kPathSurfaceItemPpolygon,
    kPathSurfaceItemPrect,
    kPathSurfaceItemPtext
};

static int
SurfaceCreateObjCmd(ClientData clientData, Tcl_Interp* interp,
            PathSurface *surfacePtr, int objc, Tcl_Obj* const objv[])
{
    InterpData *dataPtr = (InterpData *) clientData;
    int index;
    int result = TCL_OK;

    if (objc < 3) {
    Tcl_WrongNumArgs(interp, 2, objv, "type ?arg arg...?");
    return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[2], surfaceItemCmds, "type", 0,
        &index) != TCL_OK) {
    return TCL_ERROR;
    }

    switch (index) {
    case kPathSurfaceItemCircle:
    case kPathSurfaceItemEllipse: {
        result = SurfaceCreateEllipse(interp, dataPtr, surfacePtr,
                      index, objc, objv);
        break;
    }
    case kPathSurfaceItemPath: {
        result = SurfaceCreatePath(interp, dataPtr, surfacePtr,
                       objc, objv);
        break;
    }
    case kPathSurfaceItemPimage: {
        result = SurfaceCreatePimage(interp, dataPtr, surfacePtr,
                     objc, objv);
        break;
    }
    case kPathSurfaceItemPline: {
        result = SurfaceCreatePline(interp, dataPtr, surfacePtr,
                    objc, objv);
        break;
    }
    case kPathSurfaceItemPolyline:
    case kPathSurfaceItemPpolygon: {
        result = SurfaceCreatePpoly(interp, dataPtr, surfacePtr,
                    index, objc, objv);
        break;
    }
    case kPathSurfaceItemPrect: {
        result = SurfaceCreatePrect(interp, dataPtr, surfacePtr,
                    objc, objv);
        break;
    }
    case kPathSurfaceItemPtext: {
        result = SurfaceCreatePtext(interp, dataPtr, surfacePtr,
                    objc, objv);
        break;
    }
    }
    return result;
}

TK_PATH_STYLE_CUSTOM_OPTION_RECORDS

#define TK_PATH_OPTION_SPEC_R(typeName)                \
    {TK_OPTION_DOUBLE, "-r", (char *) NULL, (char *) NULL,  \
    "0.0", -1, Tk_Offset(typeName, rx), 0, 0, 0}

#define TK_PATH_OPTION_SPEC_RX(typeName)                \
    {TK_OPTION_DOUBLE, "-rx", (char *) NULL, (char *) NULL, \
    "0.0", -1, Tk_Offset(typeName, rx), 0, 0, 0}

#define TK_PATH_OPTION_SPEC_RY(typeName)                \
    {TK_OPTION_DOUBLE, "-ry", (char *) NULL, (char *) NULL, \
    "0.0", -1, Tk_Offset(typeName, ry), 0, 0, 0}

typedef struct SurfGenericItem {
    Tcl_Obj *styleObj;
    Tk_PathStyle style;
    TkPathArrowDescr startarrow;
    TkPathArrowDescr endarrow;
} SurfGenericItem;

static int
GetPointCoords(Tcl_Interp *interp, double *pointPtr, int objc,
           Tcl_Obj *const objv[])
{
    if ((objc == 1) || (objc == 2)) {
    double x, y;

    if (objc==1) {
        if (Tcl_ListObjGetElements(interp, objv[0], &objc,
            (Tcl_Obj ***) &objv) != TCL_OK) {
        return TCL_ERROR;
        } else if (objc != 2) {
        Tcl_SetObjResult(interp,
            Tcl_NewStringObj("wrong # coords: expected 2", -1));
        return TCL_ERROR;
        }
    }
    if ((Tcl_GetDoubleFromObj(interp, objv[0], &x) != TCL_OK)
        || (Tcl_GetDoubleFromObj(interp, objv[1], &y) != TCL_OK)) {
        return TCL_ERROR;
    }
    pointPtr[0] = x;
    pointPtr[1] = y;
    } else {
    Tcl_SetObjResult(interp,
        Tcl_NewStringObj("wrong # coords: expected 2", -1));
    return TCL_ERROR;
    }
    return TCL_OK;
}

static int
GetTwoPointsCoords(Tcl_Interp *interp, double *pointsPtr, int objc,
           Tcl_Obj *const objv[])
{
    if ((objc == 1) || (objc == 4)) {
    double x1, y1, x2, y2;

    if (objc==1) {
        if (Tcl_ListObjGetElements(interp, objv[0], &objc,
            (Tcl_Obj ***) &objv) != TCL_OK) {
        return TCL_ERROR;
        } else if (objc != 4) {
        Tcl_SetObjResult(interp,
            Tcl_NewStringObj("wrong # coords: expected 4", -1));
        return TCL_ERROR;
        }
    }
    if ((Tcl_GetDoubleFromObj(interp, objv[0], &x1) != TCL_OK)
        || (Tcl_GetDoubleFromObj(interp, objv[1], &y1) != TCL_OK)
        || (Tcl_GetDoubleFromObj(interp, objv[2], &x2) != TCL_OK)
        || (Tcl_GetDoubleFromObj(interp, objv[3], &y2) != TCL_OK)) {
        return TCL_ERROR;
    }
    pointsPtr[0] = x1;
    pointsPtr[1] = y1;
    pointsPtr[2] = x2;
    pointsPtr[3] = y2;
    } else {
    Tcl_SetObjResult(interp,
        Tcl_NewStringObj("wrong # coords: expected 4", -1));
    return TCL_ERROR;
    }
    return TCL_OK;
}

static int
MakePolyAtoms(Tcl_Interp *interp, int closed, int objc, Tcl_Obj *const objv[],
          TkPathAtom **atomPtrPtr)
{
    TkPathAtom *atomPtr = NULL;

    if (objc == 1) {
    if (Tcl_ListObjGetElements(interp, objv[0], &objc,
        (Tcl_Obj ***) &objv) != TCL_OK) {
        return TCL_ERROR;
    }
    }
    if (objc & 1) {
    Tcl_SetObjResult(interp,
        Tcl_NewStringObj("wrong # coords: expected an even number", -1));
    return TCL_ERROR;
    } else if (objc < 4) {
    Tcl_SetObjResult(interp,
        Tcl_NewStringObj("wrong # coords: expected at least 4", -1));
    return TCL_ERROR;
    } else {
    int     i;
    double    x, y;
    double    firstX = 0.0, firstY = 0.0;
    TkPathAtom *firstAtomPtr = NULL;

    for (i = 0; i < objc; i += 2) {
        if ((Tcl_GetDoubleFromObj(interp, objv[i], &x) != TCL_OK)
        || (Tcl_GetDoubleFromObj(interp, objv[i+1], &y) != TCL_OK)) {
        TkPathFreeAtoms(atomPtr);
        return TCL_ERROR;
        }
        if (i == 0) {
        firstX = x;
        firstY = y;
        atomPtr = TkPathNewMoveToAtom(x, y);
        firstAtomPtr = atomPtr;
        } else {
        atomPtr->nextPtr = TkPathNewLineToAtom(x, y);
        atomPtr = atomPtr->nextPtr;
        }
    }
    if (closed) {
        atomPtr->nextPtr = TkPathNewCloseAtom(firstX, firstY);
    }
    *atomPtrPtr = firstAtomPtr;
    }
    return TCL_OK;
}

static int
GetFirstOptionIndex(int objc, Tcl_Obj* const objv[])
{
    int i;

    for (i = 1; i < objc; i++) {
    char *arg = Tcl_GetString(objv[i]);
    if ((arg[0] == '-') && (arg[1] >= 'a') && (arg[1] <= 'z')) {
        break;
    }
    }
    return i;
}

static int
SurfaceParseOptions(Tcl_Interp *interp, char *recordPtr,
    Tk_OptionTable table, int objc, Tcl_Obj* const objv[])
{
    Tk_Window tkwin = Tk_MainWindow(interp);

    if (Tk_InitOptions(interp, recordPtr, table, tkwin) != TCL_OK) {
    return TCL_ERROR;
    }
    if (Tk_SetOptions(interp, recordPtr, table,
        objc, objv, tkwin, NULL, NULL) != TCL_OK) {
    Tk_FreeConfigOptions(recordPtr, table, tkwin);
    return TCL_ERROR;
    }
    return TCL_OK;
}

typedef struct SurfEllipseItem {
    Tcl_Obj *styleObj;
    Tk_PathStyle style;
    double rx, ry;
} SurfEllipseItem;

TK_PATH_OPTION_STRING_TABLES_FILL
TK_PATH_OPTION_STRING_TABLES_STROKE

static Tk_OptionSpec circleOptionSpecs[] = {
    TK_PATH_OPTION_SPEC_STYLENAME(SurfEllipseItem),
    TK_PATH_OPTION_SPEC_STYLE_FILL(SurfEllipseItem, ""),
    TK_PATH_OPTION_SPEC_STYLE_MATRIX(SurfEllipseItem),
    TK_PATH_OPTION_SPEC_STYLE_STROKE(SurfEllipseItem, "black"),
    TK_PATH_OPTION_SPEC_R(SurfEllipseItem),
    TK_PATH_OPTION_SPEC_END
};

static Tk_OptionSpec ellipseOptionSpecs[] = {
    TK_PATH_OPTION_SPEC_STYLENAME(SurfEllipseItem),
    TK_PATH_OPTION_SPEC_STYLE_FILL(SurfEllipseItem, ""),
    TK_PATH_OPTION_SPEC_STYLE_MATRIX(SurfEllipseItem),
    TK_PATH_OPTION_SPEC_STYLE_STROKE(SurfEllipseItem, "black"),
    TK_PATH_OPTION_SPEC_RX(SurfEllipseItem),
    TK_PATH_OPTION_SPEC_RY(SurfEllipseItem),
    TK_PATH_OPTION_SPEC_END
};

static int
SurfaceCreateEllipse(Tcl_Interp *interp, InterpData *dataPtr,
             PathSurface *surfacePtr,
             int type, int objc, Tcl_Obj* const objv[])
{
    TkPathContext     context = surfacePtr->ctx;
    int            i;
    double        center[2];
    TkPathAtom         *atomPtr;
    TkEllipseAtom     ellAtom;
    TkPathRect        bbox;
    SurfEllipseItem    ellipse;
    Tk_PathStyle    *style = &ellipse.style;
    Tk_PathStyle    mergedStyle;
    int            result = TCL_OK;

    memset(&ellipse, 0, sizeof(ellipse));
    ellipse.styleObj = NULL;
    i = GetFirstOptionIndex(objc, objv);
    TkPathInitStyle(style);
    if (GetPointCoords(interp, center, i-3, objv+3) != TCL_OK) {
    goto bail;
    }
    if (SurfaceParseOptions(interp, (char *)&ellipse,
                (type == kPathSurfaceItemCircle) ?
                dataPtr->optionTableCircle :
                dataPtr->optionTableEllipse,
                objc-i, objv+i) != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    if (style->fillObj != NULL) {
    style->fill = TkPathGetPathColorStatic(interp,
                           Tk_MainWindow(interp),
                           style->fillObj);
    if (style->fill == NULL) {
        result = TCL_ERROR;
        goto bail;
    }
    }

    /*
     * NB: We *copy* the style for temp usage.
     *     Only values and pointers are copied so we shall not free this style.
     */
    mergedStyle = ellipse.style;
    if (TkPathStyleMergeStyleStatic(interp, ellipse.styleObj,
                    &mergedStyle, 0) != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    ellipse.rx = MAX(0.0, ellipse.rx);
    ellipse.ry = MAX(0.0, ellipse.ry);
    atomPtr = (TkPathAtom *)&ellAtom;
    atomPtr->nextPtr = NULL;
    atomPtr->type = TK_PATH_ATOM_ELLIPSE;
    ellAtom.cx = center[0];
    ellAtom.cy = center[1];
    ellAtom.rx = ellipse.rx;
    ellAtom.ry = (type == kPathSurfaceItemCircle) ? ellipse.rx : ellipse.ry;
    TkPathSaveState(context);
    TkPathPushTMatrix(context, mergedStyle.matrixPtr);
    if (TkPathMakePath(context, atomPtr, &mergedStyle) != TCL_OK) {
    TkPathRestoreState(context);
    result = TCL_ERROR;
    goto bail;
    }
    bbox = TkPathGetTotalBbox(atomPtr, &mergedStyle);
    TkPathPaintPath(context, atomPtr, &mergedStyle, &bbox);
    TkPathRestoreState(context);

bail:
    TkPathDeleteStyle(&ellipse.style);
    Tk_FreeConfigOptions((char *)&ellipse,
             (type == kPathSurfaceItemCircle) ?
             dataPtr->optionTableCircle :
             dataPtr->optionTableEllipse,
             Tk_MainWindow(interp));
    return result;
}

static Tk_OptionSpec pathOptionSpecs[] = {
    TK_PATH_OPTION_SPEC_STYLENAME(SurfGenericItem),
    TK_PATH_OPTION_SPEC_STYLE_FILL(SurfGenericItem, ""),
    TK_PATH_OPTION_SPEC_STYLE_MATRIX(SurfGenericItem),
    TK_PATH_OPTION_SPEC_STYLE_STROKE(SurfGenericItem, "black"),
    TK_PATH_OPTION_SPEC_STARTARROW_GRP(SurfGenericItem),
    TK_PATH_OPTION_SPEC_ENDARROW_GRP(SurfGenericItem),
    TK_PATH_OPTION_SPEC_END
};

static int
SurfaceCreatePath(Tcl_Interp *interp, InterpData *dataPtr,
          PathSurface *surfacePtr, int objc, Tcl_Obj* const objv[])
{
    TkPathContext     context = surfacePtr->ctx;
    TkPathAtom         *atomPtr = NULL;
    TkPathRect        bbox;
    SurfGenericItem    item;
    Tk_PathStyle    *style = &item.style;
    Tk_PathStyle    mergedStyle;
    int            len;
    int            result = TCL_OK;

    memset(&item, 0, sizeof(item));
    item.styleObj = NULL;
    TkPathInitStyle(&item.style);
    TkPathArrowDescrInit(&item.startarrow);
    TkPathArrowDescrInit(&item.endarrow);
    if (TkPathParseToAtoms(interp, objv[3], &atomPtr, &len) != TCL_OK) {
    return TCL_ERROR;
    }
    if (SurfaceParseOptions(interp, (char *)&item, dataPtr->optionTablePath,
                objc-4, objv+4) != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    if (style->fillObj != NULL) {
    style->fill = TkPathGetPathColorStatic(interp,
                           Tk_MainWindow(interp),
                           style->fillObj);
    if (style->fill == NULL) {
        result = TCL_ERROR;
        goto bail;
    }
    }
    mergedStyle = item.style;
    if (TkPathStyleMergeStyleStatic(interp, item.styleObj,
                    &mergedStyle, 0) != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    TkPathSaveState(context);
    TkPathPushTMatrix(context, mergedStyle.matrixPtr);
    if (TkPathMakePath(context, atomPtr, &mergedStyle) != TCL_OK) {
    TkPathRestoreState(context);
    result = TCL_ERROR;
    goto bail;
    }
    bbox = TkPathGetTotalBbox(atomPtr, &mergedStyle);
    TkPathPaintPath(context, atomPtr, &mergedStyle, &bbox);
    TkPathRestoreState(context);

bail:
    TkPathDeleteStyle(style);
    TkPathFreeAtoms(atomPtr);
    TkPathFreeArrow(&item.startarrow);
    TkPathFreeArrow(&item.endarrow);
    Tk_FreeConfigOptions((char *)&item, dataPtr->optionTablePath,
             Tk_MainWindow(interp));
    return result;
}

typedef struct SurfPimageItem {
    char *imageName;
    double height;
    double width;
    TkPathMatrix *matrixPtr;
    Tcl_Obj *styleObj;        /* We only use matrixPtr from style. */
} SurfPimageItem;

static Tk_OptionSpec pimageOptionSpecs[] = {
    {TK_OPTION_DOUBLE, "-height", (char *) NULL, (char *) NULL,
    "0", -1, Tk_Offset(SurfPimageItem, height), 0, 0, 0},
    {TK_OPTION_CUSTOM, "-matrix", (char *) NULL, (char *) NULL,
    (char *) NULL, -1, Tk_Offset(SurfPimageItem, matrixPtr),
    TK_OPTION_NULL_OK, (ClientData) &matrixCO, 0},
    {TK_OPTION_STRING, "-image", (char *) NULL, (char *) NULL,
    "", -1, Tk_Offset(SurfPimageItem, imageName), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-style", (char *) NULL, (char *) NULL,
    "", Tk_Offset(SurfPimageItem, styleObj), -1, TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_DOUBLE, "-width", (char *) NULL, (char *) NULL,
    "0", -1, Tk_Offset(SurfPimageItem, width), 0, 0, 0},
    TK_PATH_OPTION_SPEC_END
};

static int
SurfaceCreatePimage(Tcl_Interp *interp, InterpData *dataPtr,
            PathSurface *surfacePtr, int objc, Tcl_Obj* const objv[])
{
    TkPathContext     context = surfacePtr->ctx;
    SurfPimageItem    item;
    Tk_Image        image;
    Tk_PhotoHandle    photo;
    Tk_PathStyle    style;
    double        point[2];
    int            i;
    int            result = TCL_OK;

    memset(&item, 0, sizeof(item));
    item.imageName = NULL;
    item.matrixPtr = NULL;
    item.styleObj = NULL;
    TkPathInitStyle(&style);
    i = GetFirstOptionIndex(objc, objv);
    if (GetPointCoords(interp, point, i-3, objv+3) != TCL_OK) {
    return TCL_ERROR;
    }
    if (SurfaceParseOptions(interp, (char *)&item, dataPtr->optionTablePimage,
                objc-i, objv+i) != TCL_OK) {
    return TCL_ERROR;
    }
    style.matrixPtr = item.matrixPtr;
    if (TkPathStyleMergeStyleStatic(interp, item.styleObj, &style, 0)
    != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    if (item.imageName != NULL) {
    photo = Tk_FindPhoto(interp, item.imageName);
    if (photo == NULL) {
        Tcl_SetObjResult(interp,
        Tcl_NewStringObj("no photo with the given name", -1));
        result = TCL_ERROR;
        goto bail;
     }
    image = Tk_GetImage(interp, Tk_MainWindow(interp),
                item.imageName, NULL, (ClientData) NULL);
    TkPathSaveState(context);
    TkPathPushTMatrix(context, style.matrixPtr);
    TkPathImage(context, image, photo, point[0], point[1],
            item.width, item.height, style.fillOpacity,
            NULL, 0.0, 99, NULL);
    Tk_FreeImage(image);
    TkPathRestoreState(context);
    }

bail:
    Tk_FreeConfigOptions((char *)&item, dataPtr->optionTablePimage,
             Tk_MainWindow(interp));
    return result;
}

static Tk_OptionSpec plineOptionSpecs[] = {
    TK_PATH_OPTION_SPEC_STYLENAME(SurfGenericItem),
    TK_PATH_OPTION_SPEC_STYLE_MATRIX(SurfGenericItem),
    TK_PATH_OPTION_SPEC_STYLE_STROKE(SurfGenericItem, "black"),
    TK_PATH_OPTION_SPEC_STARTARROW_GRP(SurfGenericItem),
    TK_PATH_OPTION_SPEC_ENDARROW_GRP(SurfGenericItem),
    TK_PATH_OPTION_SPEC_END
};

static int
SurfaceCreatePline(Tcl_Interp *interp, InterpData *dataPtr,
           PathSurface *surfacePtr, int objc, Tcl_Obj* const objv[])
{
    TkPathContext     context = surfacePtr->ctx;
    int            i;
    TkPathRect        bbox;
    SurfGenericItem    item;
    TkPathAtom         *atomPtr = NULL;
    Tk_PathStyle    mergedStyle;
    double        points[4];
    TkPathPoint        pf, pl, newp;
    int            result = TCL_OK;

    memset(&item, 0, sizeof(item));
    item.styleObj = NULL;
    i = GetFirstOptionIndex(objc, objv);
    TkPathInitStyle(&item.style);
    TkPathArrowDescrInit(&item.startarrow);
    TkPathArrowDescrInit(&item.endarrow);
    if (GetTwoPointsCoords(interp, points, i-3, objv+3) != TCL_OK) {
    return TCL_ERROR;
    }
    if (SurfaceParseOptions(interp, (char *)&item, dataPtr->optionTablePline,
                objc-i, objv+i) != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    mergedStyle = item.style;
    if (TkPathStyleMergeStyleStatic(interp, item.styleObj,
                    &mergedStyle, 0) != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    pf.x = points[0];
    pf.y = points[1];
    pl.x = points[2];
    pl.y = points[3];
    TkPathPreconfigureArrow(&pf, &item.startarrow);
    TkPathPreconfigureArrow(&pl, &item.endarrow);
    newp = TkPathConfigureArrow(pf, pl, &item.startarrow, &mergedStyle,
                mergedStyle.fill == NULL);
    points[0] = newp.x;
    points[1] = newp.y;
    newp = TkPathConfigureArrow(pl, pf, &item.endarrow, &mergedStyle,
                mergedStyle.fill == NULL);
    points[2] = newp.x;
    points[3] = newp.y;
    atomPtr = TkPathNewMoveToAtom(points[0], points[1]);
    atomPtr->nextPtr = TkPathNewLineToAtom(points[2], points[3]);
    TkPathSaveState(context);
    TkPathPushTMatrix(context, mergedStyle.matrixPtr);
    if (TkPathMakePath(context, atomPtr, &mergedStyle) != TCL_OK) {
    TkPathRestoreState(context);
    result = TCL_ERROR;
    goto bail;
    }
    bbox = TkPathGetTotalBbox(atomPtr, &mergedStyle);
    TkPathPaintPath(context, atomPtr, &mergedStyle, &bbox);
    TkPathPaintArrow(context, &item.startarrow, &mergedStyle, &bbox);
    TkPathPaintArrow(context, &item.endarrow, &mergedStyle, &bbox);
    TkPathRestoreState(context);

bail:
    TkPathDeleteStyle(&item.style);
    TkPathFreeAtoms(atomPtr);
    TkPathFreeArrow(&item.startarrow);
    TkPathFreeArrow(&item.endarrow);
    Tk_FreeConfigOptions((char *)&item, dataPtr->optionTablePline,
             Tk_MainWindow(interp));
    return result;
}

static Tk_OptionSpec polylineOptionSpecs[] = {
    TK_PATH_OPTION_SPEC_STYLENAME(SurfGenericItem),
    TK_PATH_OPTION_SPEC_STYLE_MATRIX(SurfGenericItem),
    TK_PATH_OPTION_SPEC_STYLE_STROKE(SurfGenericItem, "black"),
    TK_PATH_OPTION_SPEC_STARTARROW_GRP(SurfGenericItem),
    TK_PATH_OPTION_SPEC_ENDARROW_GRP(SurfGenericItem),
    TK_PATH_OPTION_SPEC_END
};

static Tk_OptionSpec ppolygonOptionSpecs[] = {
    TK_PATH_OPTION_SPEC_STYLENAME(SurfGenericItem),
    TK_PATH_OPTION_SPEC_STYLE_FILL(SurfGenericItem, ""),
    TK_PATH_OPTION_SPEC_STYLE_MATRIX(SurfGenericItem),
    TK_PATH_OPTION_SPEC_STYLE_STROKE(SurfGenericItem, "black"),
    TK_PATH_OPTION_SPEC_STARTARROW_GRP(SurfGenericItem),
    TK_PATH_OPTION_SPEC_ENDARROW_GRP(SurfGenericItem),
    TK_PATH_OPTION_SPEC_END
};

static int
SurfaceCreatePpoly(Tcl_Interp* interp, InterpData *dataPtr,
           PathSurface *surfacePtr,
           int type, int objc, Tcl_Obj* const objv[])
{
    TkPathContext     context = surfacePtr->ctx;
    int            i;
    TkPathRect        bbox;
    SurfGenericItem    item;
    Tk_PathStyle    *style = &item.style;
    Tk_PathStyle    mergedStyle;
    TkPathAtom         *atomPtr = NULL;
    int            result = TCL_OK;

    memset(&item, 0, sizeof(item));
    item.styleObj = NULL;
    i = GetFirstOptionIndex(objc, objv);
    TkPathInitStyle(style);
    TkPathArrowDescrInit(&item.startarrow);
    TkPathArrowDescrInit(&item.endarrow);
    if (MakePolyAtoms(interp, (type == kPathSurfaceItemPolyline) ? 0 : 1,
        i-3, objv+3, &atomPtr) != TCL_OK) {
    return TCL_ERROR;
    }
    if (SurfaceParseOptions(interp, (char *)&item,
                (type == kPathSurfaceItemPolyline) ?
                dataPtr->optionTablePolyline :
                dataPtr->optionTablePpolygon,
                objc-i, objv+i) != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    if (style->fillObj != NULL) {
    style->fill = TkPathGetPathColorStatic(interp,
                           Tk_MainWindow(interp),
                           style->fillObj);
    if (style->fill == NULL) {
        result = TCL_ERROR;
        goto bail;
    }
    }
    mergedStyle = item.style;
    if (TkPathStyleMergeStyleStatic(interp, item.styleObj,
                    &mergedStyle, 0) != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    TkPathSaveState(context);
    TkPathPushTMatrix(context, mergedStyle.matrixPtr);
    if (TkPathMakePath(context, atomPtr, &mergedStyle) != TCL_OK) {
    TkPathRestoreState(context);
    result = TCL_ERROR;
    goto bail;
    }
    bbox = TkPathGetTotalBbox(atomPtr, &mergedStyle);
    TkPathPaintPath(context, atomPtr, &mergedStyle, &bbox);
    TkPathRestoreState(context);

bail:
    TkPathDeleteStyle(style);
    TkPathFreeAtoms(atomPtr);
    TkPathFreeArrow(&item.startarrow);
    TkPathFreeArrow(&item.endarrow);
    Tk_FreeConfigOptions((char *)&item,
             (type == kPathSurfaceItemPolyline) ?
             dataPtr->optionTablePolyline :
             dataPtr->optionTablePpolygon,
             Tk_MainWindow(interp));
    return result;
}

typedef struct SurfPrectItem {
    Tcl_Obj *styleObj;
    Tk_PathStyle style;
    double rx, ry;
} SurfPrectItem;

static Tk_OptionSpec prectOptionSpecs[] = {
    TK_PATH_OPTION_SPEC_STYLENAME(SurfPrectItem),
    TK_PATH_OPTION_SPEC_STYLE_FILL(SurfPrectItem, ""),
    TK_PATH_OPTION_SPEC_STYLE_MATRIX(SurfPrectItem),
    TK_PATH_OPTION_SPEC_STYLE_STROKE(SurfPrectItem, "black"),
    TK_PATH_OPTION_SPEC_RX(SurfPrectItem),
    TK_PATH_OPTION_SPEC_RY(SurfPrectItem),
    TK_PATH_OPTION_SPEC_END
};

static int
SurfaceCreatePrect(Tcl_Interp *interp, InterpData *dataPtr,
           PathSurface *surfacePtr, int objc, Tcl_Obj* const objv[])
{
    TkPathContext     context = surfacePtr->ctx;
    int            i;
    SurfPrectItem    prect;
    Tk_PathStyle    *style = &prect.style;
    Tk_PathStyle    mergedStyle;
    TkPathRect        bbox;
    TkPathAtom         *atomPtr = NULL;
    double        points[4];
    int            result = TCL_OK;

    memset(&prect, 0, sizeof(prect));
    prect.styleObj = NULL;
    i = GetFirstOptionIndex(objc, objv);
    TkPathInitStyle(style);
    if (GetTwoPointsCoords(interp, points, i-3, objv+3) != TCL_OK) {
    return TCL_ERROR;
    }
    if (SurfaceParseOptions(interp, (char *)&prect, dataPtr->optionTablePrect,
                objc-i, objv+i) != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    if (style->fillObj != NULL) {
    style->fill = TkPathGetPathColorStatic(interp,
                           Tk_MainWindow(interp),
                           style->fillObj);
    if (style->fill == NULL) {
        result = TCL_ERROR;
        goto bail;
    }
    }
    mergedStyle = prect.style;
    if (TkPathStyleMergeStyleStatic(interp, prect.styleObj,
                    &mergedStyle, 0) != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    prect.rx = MAX(0.0, prect.rx);
    prect.ry = MAX(0.0, prect.ry);
    TkPathSaveState(context);
    TkPathPushTMatrix(context, mergedStyle.matrixPtr);
    TkPathMakePrectAtoms(points, prect.rx, prect.ry, &atomPtr);
    if (TkPathMakePath(context, atomPtr, &mergedStyle) != TCL_OK) {
    TkPathRestoreState(context);
    result = TCL_ERROR;
    goto bail;
    }
    bbox = TkPathGetTotalBbox(atomPtr, &mergedStyle);
    TkPathPaintPath(context, atomPtr, &mergedStyle, &bbox);
    TkPathRestoreState(context);

bail:
    TkPathDeleteStyle(&prect.style);
    TkPathFreeAtoms(atomPtr);
    Tk_FreeConfigOptions((char *)&prect, dataPtr->optionTablePrect,
             Tk_MainWindow(interp));
    return result;
}

typedef struct SurfPtextItem {
    Tcl_Obj *styleObj;
    Tk_PathStyle style;
    Tk_PathTextStyle textStyle;
    int textAnchor;
    int fillOverStroke;
    double x;
    double y;
    char *utf8;                /* The actual text to display; UTF-8 */
} SurfPtextItem;

static const char *textAnchorST[] = {
    "start", "middle", "end", "n", "w", "s", "e",
    "nw", "ne", "sw", "se", "c", (char *) NULL
};

static const char *fontWeightST[] = {
    "normal", "bold", NULL
};

static const char *fontSlantST[] = {
    "normal", "italic", "oblique", NULL
};

static Tk_OptionSpec ptextOptionSpecs[] = {
    {TK_OPTION_STRING, "-fontfamily", (char *) NULL, (char *) NULL,
    "Helvetica", -1, Tk_Offset(SurfPtextItem, textStyle.fontFamily),
    TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_DOUBLE, "-fontsize", (char *) NULL, (char *) NULL,
    "12.0", -1, Tk_Offset(SurfPtextItem,  textStyle.fontSize), 0, 0, 0},
    {TK_OPTION_STRING, "-text", (char *) NULL, (char *) NULL,
    "", -1, Tk_Offset(SurfPtextItem, utf8),
    TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING_TABLE, "-textanchor", (char *) NULL, (char *) NULL,
    "start", -1, Tk_Offset(SurfPtextItem, textAnchor),
    0, (ClientData) textAnchorST, 0},
    {TK_OPTION_STRING_TABLE, "-fontweight", (char *) NULL, (char *) NULL,
    "normal", -1, Tk_Offset(SurfPtextItem, textStyle.fontWeight),
    0, (ClientData) fontWeightST, 0},
    {TK_OPTION_STRING_TABLE, "-fontslant", (char *) NULL, (char *) NULL,
    "normal", -1, Tk_Offset(SurfPtextItem, textStyle.fontSlant),
    0, (ClientData) fontSlantST, 0},
    {TK_OPTION_BOOLEAN, "-filloverstroke", (char *) NULL, (char *) NULL,
    0, -1, Tk_Offset(SurfPtextItem, fillOverStroke),
    0, 0, 0},
    TK_PATH_OPTION_SPEC_STYLENAME(SurfPtextItem),
    TK_PATH_OPTION_SPEC_STYLE_FILL(SurfPtextItem, "black"),
    TK_PATH_OPTION_SPEC_STYLE_MATRIX(SurfPtextItem),
    TK_PATH_OPTION_SPEC_STYLE_STROKE(SurfPtextItem, ""),
    TK_PATH_OPTION_SPEC_END
};

static int
SurfaceCreatePtext(Tcl_Interp *interp, InterpData *dataPtr,
           PathSurface *surfacePtr, int objc, Tcl_Obj* const objv[])
{
    TkPathContext   context = surfacePtr->ctx;
    int            i;
    double        point[2];
    SurfPtextItem   item;
    Tk_PathStyle    *style = &item.style;
    Tk_PathStyle    mergedStyle;
    TkPathRect        r;
    void        *custom = NULL;
    int            result = TCL_OK;
    double        width, height, bheight;

    if (dataPtr->tkwin == NULL) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj("no main window", -1));
    return TCL_ERROR;
    }
    memset(&item, 0, sizeof(item));
    item.styleObj = NULL;
    item.textAnchor = TK_PATH_TEXTANCHOR_Start;
    item.utf8 = NULL;
    item.textStyle.fontFamily = NULL;
    i = GetFirstOptionIndex(objc, objv);
    TkPathInitStyle(&item.style);
    if (GetPointCoords(interp, point, i-3, objv+3) != TCL_OK) {
    return TCL_ERROR;
    }
    if (SurfaceParseOptions(interp, (char *)&item, dataPtr->optionTablePtext,
                objc-i, objv+i) != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    if (style->fillObj != NULL) {
    style->fill = TkPathGetPathColorStatic(interp, dataPtr->tkwin,
                           style->fillObj);
    if (style->fill == NULL) {
        result = TCL_ERROR;
        goto bail;
    }
    }
    if (TkPathTextConfig(interp, &item.textStyle, item.utf8,
             &custom) != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    mergedStyle = item.style;
    if (TkPathStyleMergeStyleStatic(interp, item.styleObj,
                    &mergedStyle, 0) != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    r = TkPathTextMeasureBbox(Tk_Display(dataPtr->tkwin), &item.textStyle,
                  item.utf8, NULL, custom);
    width = r.x2 - r.x1;
    height = r.y2 - r.y1;
    bheight = r.y2 + mergedStyle.strokeWidth;

    switch (item.textAnchor) {
        case TK_PATH_TEXTANCHOR_Start:
        case TK_PATH_TEXTANCHOR_W:
        case TK_PATH_TEXTANCHOR_NW:
        case TK_PATH_TEXTANCHOR_SW:
            break;
        case TK_PATH_TEXTANCHOR_Middle:
        case TK_PATH_TEXTANCHOR_N:
        case TK_PATH_TEXTANCHOR_S:
        case TK_PATH_TEXTANCHOR_C:
        point[0] -= width/2;
            break;
        case TK_PATH_TEXTANCHOR_End:
        case TK_PATH_TEXTANCHOR_E:
        case TK_PATH_TEXTANCHOR_NE:
        case TK_PATH_TEXTANCHOR_SE:
        point[0] -= width;
            break;
        default:
            break;
    }

    switch (item.textAnchor) {
        case TK_PATH_TEXTANCHOR_Start:
        case TK_PATH_TEXTANCHOR_Middle:
        case TK_PATH_TEXTANCHOR_End:
            point[1] += r.y1;
            break;
        case TK_PATH_TEXTANCHOR_N:
        case TK_PATH_TEXTANCHOR_NW:
        case TK_PATH_TEXTANCHOR_NE:
            break;
        case TK_PATH_TEXTANCHOR_W:
        case TK_PATH_TEXTANCHOR_E:
        case TK_PATH_TEXTANCHOR_C:
            point[1] += height/2;
            break;
        case TK_PATH_TEXTANCHOR_S:
        case TK_PATH_TEXTANCHOR_SW:
        case TK_PATH_TEXTANCHOR_SE:
            point[1] += height;
            break;
        default:
            break;
    }

    TkPathSaveState(context);
    TkPathPushTMatrix(context, mergedStyle.matrixPtr);
    TkPathBeginPath(context, &mergedStyle);
    TkPathTextDraw(context, &mergedStyle, &item.textStyle,
           point[0], point[1] - bheight, item.fillOverStroke,
           item.utf8, custom);
    TkPathEndPath(context);
    TkPathTextFree(&item.textStyle, custom);
    TkPathRestoreState(context);

bail:
    TkPathDeleteStyle(style);
    Tk_FreeConfigOptions((char *)&item, dataPtr->optionTablePtext,
             dataPtr->tkwin);
    return result;
}

static int
SurfaceEraseObjCmd(Tcl_Interp *interp, PathSurface *surfacePtr,
           int objc, Tcl_Obj* const objv[])
{
    double x, y, width, height;

    if (objc != 6) {
    Tcl_WrongNumArgs(interp, 2, objv, "x y width height");
    return TCL_ERROR;
    }
    if ((Tcl_GetDoubleFromObj(interp, objv[2], &x) != TCL_OK) ||
        (Tcl_GetDoubleFromObj(interp, objv[3], &y) != TCL_OK) ||
        (Tcl_GetDoubleFromObj(interp, objv[4], &width) != TCL_OK) ||
        (Tcl_GetDoubleFromObj(interp, objv[5], &height) != TCL_OK)) {
    return TCL_ERROR;
    }
    TkPathSurfaceErase(surfacePtr->ctx, x, y, width, height);
    return TCL_OK;
}

int
TkPathSurfaceInit(Tcl_Interp *interp)
{
    InterpData *dataPtr = (InterpData *) ckalloc(sizeof(InterpData));

    Tcl_InitHashTable(&dataPtr->surfaceHash, TCL_STRING_KEYS);
    dataPtr->optionTableCircle =
    Tk_CreateOptionTable(interp, circleOptionSpecs);
    dataPtr->optionTableEllipse =
    Tk_CreateOptionTable(interp, ellipseOptionSpecs);
    dataPtr->optionTablePath =
    Tk_CreateOptionTable(interp, pathOptionSpecs);
    dataPtr->optionTablePimage =
    Tk_CreateOptionTable(interp, pimageOptionSpecs);
    dataPtr->optionTablePline =
    Tk_CreateOptionTable(interp, plineOptionSpecs);
    dataPtr->optionTablePolyline =
    Tk_CreateOptionTable(interp, polylineOptionSpecs);
    dataPtr->optionTablePpolygon =
    Tk_CreateOptionTable(interp, ppolygonOptionSpecs);
    dataPtr->optionTablePrect =
    Tk_CreateOptionTable(interp, prectOptionSpecs);
    dataPtr->optionTablePtext =
    Tk_CreateOptionTable(interp, ptextOptionSpecs);
    dataPtr->uid = 0;
    dataPtr->tkwin = Tk_MainWindow(interp);
    if (dataPtr->tkwin != NULL) {
    Tk_CreateEventHandler(dataPtr->tkwin, StructureNotifyMask,
                  StaticSurfaceEventProc, dataPtr);
    }
    Tcl_CreateObjCommand(interp, "::path::surface",
             StaticSurfaceObjCmd, (ClientData) dataPtr,
             (Tcl_CmdDeleteProc *) StaticSurfaceObjCmdDeleted);
    return TCL_OK;
}
/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
