/*
 * tkoPathCanvPoly.c --
 *
 *    This file implements polygon and polyline canvas items modelled after its
 *  SVG counterpart. See http://www.w3.org/TR/SVG11/.
 *
 * Copyright (c) 2007-2008  Mats Bengtsson
 *
 */

#include "tkoPath.h"

/*
 * The structure below defines the record for each path item.
 */

typedef struct PpolyItem {
    Tk_PathItemEx headerEx;    /* Generic stuff that's the same for all
                                * path types.  MUST BE FIRST IN STRUCTURE. */
    char type;                 /* Polyline or polygon. */
    TkPathAtom *atomPtr;
    int maxNumSegments;        /* Max number of straight segments (for subpath)
                                * needed for Area and Point functions. */
    TkPathArrowDescr startarrow;
    TkPathArrowDescr endarrow;
} PpolyItem;

enum {
    kPpolyTypePolyline,
    kPpolyTypePolygon
};

/*
 * Prototypes for procedures defined in this file:
 */

static void ComputePpolyBbox(
    Tk_PathCanvas canvas,
    PpolyItem * ppolyPtr);
static int ConfigurePpoly(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int objc,
    Tcl_Obj * const objv[],
    int flags);
static int CoordsForPolygonline(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    int closed,
    int objc,
    Tcl_Obj * const objv[],
    TkPathAtom ** atomPtrPtr,
    int *lenPtr);
static int CreateAny(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    struct Tk_PathItem *itemPtr,
    int objc,
    Tcl_Obj * const objv[],
    char type);
static int CreatePolyline(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    struct Tk_PathItem *itemPtr,
    int objc,
    Tcl_Obj * const objv[]);
static int CreatePpolygon(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    struct Tk_PathItem *itemPtr,
    int objc,
    Tcl_Obj * const objv[]);
static void DeletePpoly(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    Display * display);
static void DisplayPpoly(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    Display * display,
    Drawable drawable,
    int x,
    int y,
    int width,
    int height);
static void PpolyBbox(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int mask);
static int PpolyCoords(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int objc,
    Tcl_Obj * const objv[]);
static int PpolyToArea(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    double *rectPtr);
static int PpolyToPdf(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int objc,
    Tcl_Obj * const objv[],
    int prepass);
static double PpolyToPoint(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    double *coordPtr);
static void ScalePpoly(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int compensate,
    double originX,
    double originY,
    double scaleX,
    double scaleY);
static void TranslatePpoly(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int compensate,
    double deltaX,
    double deltaY);
static int ConfigureArrows(
    Tk_PathCanvas canvas,
    PpolyItem * ppolyPtr);

TK_PATH_STYLE_CUSTOM_OPTION_RECORDS
    TK_PATH_CUSTOM_OPTION_TAGS
    TK_PATH_OPTION_STRING_TABLES_FILL
    TK_PATH_OPTION_STRING_TABLES_STROKE
    TK_PATH_OPTION_STRING_TABLES_STATE
    static Tk_OptionSpec optionSpecsPolyline[] = {
    TK_PATH_OPTION_SPEC_CORE(Tk_PathItemEx),
    TK_PATH_OPTION_SPEC_PARENT,
    TK_PATH_OPTION_SPEC_STYLE_FILL(Tk_PathItemEx, ""),
    TK_PATH_OPTION_SPEC_STYLE_MATRIX(Tk_PathItemEx),
    TK_PATH_OPTION_SPEC_STYLE_STROKE(Tk_PathItemEx, "black"),
    TK_PATH_OPTION_SPEC_STARTARROW_GRP(PpolyItem),
    TK_PATH_OPTION_SPEC_ENDARROW_GRP(PpolyItem),
    TK_PATH_OPTION_SPEC_END
};

static Tk_OptionSpec optionSpecsPpolygon[] = {
    TK_PATH_OPTION_SPEC_CORE(Tk_PathItemEx),
    TK_PATH_OPTION_SPEC_PARENT,
    TK_PATH_OPTION_SPEC_STYLE_FILL(Tk_PathItemEx, ""),
    TK_PATH_OPTION_SPEC_STYLE_MATRIX(Tk_PathItemEx),
    TK_PATH_OPTION_SPEC_STYLE_STROKE(Tk_PathItemEx, "black"),
    TK_PATH_OPTION_SPEC_END
};

/*
 * The structures below defines the 'polyline' item type by means
 * of procedures that can be invoked by generic item code.
 */

Tk_PathItemType tkPathTypePolyline = {
    "polyline", /* name */
    sizeof(PpolyItem),  /* itemSize */
    CreatePolyline,     /* createProc */
    optionSpecsPolyline,        /* OptionSpecs */
    ConfigurePpoly,     /* configureProc */
    PpolyCoords,        /* coordProc */
    DeletePpoly,        /* deleteProc */
    DisplayPpoly,       /* displayProc */
    0,         /* flags */
    PpolyBbox, /* bboxProc */
    PpolyToPoint,       /* pointProc */
    PpolyToArea,        /* areaProc */
    PpolyToPdf, /* pdfProc */
    ScalePpoly, /* scaleProc */
    TranslatePpoly,     /* translateProc */
    (Tk_PathItemIndexProc *) NULL,      /* indexProc */
    (Tk_PathItemCursorProc *) NULL,     /* icursorProc */
    (Tk_PathItemSelectionProc *) NULL,  /* selectionProc */
    (Tk_PathItemInsertProc *) NULL,     /* insertProc */
    (Tk_PathItemDCharsProc *) NULL,     /* dTextProc */
    (Tk_PathItemType *) NULL,   /* nextPtr */
    1,         /* isPathType */
};

Tk_PathItemType tkPathTypePolygon = {
    "polygon", /* name */
    sizeof(PpolyItem),  /* itemSize */
    CreatePpolygon,     /* createProc */
    optionSpecsPpolygon,        /* OptionSpecs */
    ConfigurePpoly,     /* configureProc */
    PpolyCoords,        /* coordProc */
    DeletePpoly,        /* deleteProc */
    DisplayPpoly,       /* displayProc */
    0,         /* flags */
    PpolyBbox, /* bboxProc */
    PpolyToPoint,       /* pointProc */
    PpolyToArea,        /* areaProc */
    PpolyToPdf, /* pdfProc */
    ScalePpoly, /* scaleProc */
    TranslatePpoly,     /* translateProc */
    (Tk_PathItemIndexProc *) NULL,      /* indexProc */
    (Tk_PathItemCursorProc *) NULL,     /* icursorProc */
    (Tk_PathItemSelectionProc *) NULL,  /* selectionProc */
    (Tk_PathItemInsertProc *) NULL,     /* insertProc */
    (Tk_PathItemDCharsProc *) NULL,     /* dTextProc */
    (Tk_PathItemType *) NULL,   /* nextPtr */
    1,         /* isPathType */
};

static int
CreatePolyline(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    struct Tk_PathItem *itemPtr,
    int objc,
    Tcl_Obj * const objv[])
{
    return CreateAny(interp, canvas, itemPtr, objc, objv, kPpolyTypePolyline);
}

static int
CreatePpolygon(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    struct Tk_PathItem *itemPtr,
    int objc,
    Tcl_Obj * const objv[])
{
    return CreateAny(interp, canvas, itemPtr, objc, objv, kPpolyTypePolygon);
}

static int
CreateAny(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    struct Tk_PathItem *itemPtr,
    int objc,
    Tcl_Obj * const objv[],
    char type)
{
    PpolyItem *ppolyPtr = (PpolyItem *) itemPtr;
    Tk_PathItemEx *itemExPtr = &ppolyPtr->headerEx;
    Tk_OptionTable optionTable;
    int i, len;

    if(objc == 0) {
        Tcl_Panic("canvas did not pass any coords\n");
    }

    /*
     * Carry out initialization that is needed to set defaults and to
     * allow proper cleanup after errors during the the remainder of
     * this procedure.
     */
    TkPathInitStyle(&itemExPtr->style);
    itemExPtr->canvas = canvas;
    itemExPtr->styleObj = NULL;
    itemExPtr->styleInst = NULL;
    ppolyPtr->atomPtr = NULL;
    ppolyPtr->type = type;
    itemPtr->bbox = TkPathNewEmptyPathRect();
    itemPtr->totalBbox = TkPathNewEmptyPathRect();
    ppolyPtr->maxNumSegments = 0;
    TkPathArrowDescrInit(&ppolyPtr->startarrow);
    TkPathArrowDescrInit(&ppolyPtr->endarrow);

    if(ppolyPtr->type == kPpolyTypePolyline) {
        optionTable = Tk_CreateOptionTable(interp, optionSpecsPolyline);
    } else {
        optionTable = Tk_CreateOptionTable(interp, optionSpecsPpolygon);
    }
    itemPtr->optionTable = optionTable;
    if(Tk_InitOptions(interp, (char *)ppolyPtr, optionTable,
            Tk_PathCanvasTkwin(canvas)) != TCL_OK) {
        goto error;
    }

    for(i = 1; i < objc; i++) {
    char *arg = Tcl_GetString(objv[i]);
        if((arg[0] == '-') && (arg[1] >= 'a') && (arg[1] <= 'z')) {
            break;
        }
    }
    if(CoordsForPolygonline(interp, canvas,
            (ppolyPtr->type == kPpolyTypePolyline) ? 0 : 1,
            i, objv, &(ppolyPtr->atomPtr), &len) != TCL_OK) {
        goto error;
    }
    ppolyPtr->maxNumSegments = len;

    if(ConfigurePpoly(interp, canvas, itemPtr, objc - i, objv + i, 0) == TCL_OK) {
        return TCL_OK;
    }

  error:
    /*
     * NB: We must unlink the item here since the TkPathCanvasItemExConfigure()
     *     link it to the root by default.
     */
    TkPathCanvasItemDetach(itemPtr);
    DeletePpoly(canvas, itemPtr, Tk_Display(Tk_PathCanvasTkwin(canvas)));
    return TCL_ERROR;
}

static int
PpolyCoords(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int objc,
    Tcl_Obj * const objv[])
{
    PpolyItem *ppolyPtr = (PpolyItem *) itemPtr;
    int len, closed;

    closed = (ppolyPtr->type == kPpolyTypePolyline) ? 0 : 1;
    if(CoordsForPolygonline(interp, canvas, closed, objc, objv,
            &(ppolyPtr->atomPtr), &len) != TCL_OK) {
        return TCL_ERROR;
    }
    if(objc > 0) {
        ppolyPtr->maxNumSegments = len;
        ConfigureArrows(canvas, ppolyPtr);
        ComputePpolyBbox(canvas, ppolyPtr);
    }
    return TCL_OK;
}

void
ComputePpolyBbox(
    Tk_PathCanvas canvas,
    PpolyItem * ppolyPtr)
{
Tk_PathItemEx *itemExPtr = &ppolyPtr->headerEx;
Tk_PathItem *itemPtr = &itemExPtr->header;
Tk_PathStyle style;
Tk_PathState state = itemExPtr->header.state;

    if(state == TK_PATHSTATE_NULL) {
        state = TkPathCanvasState(canvas);
    }
    if((ppolyPtr->atomPtr == NULL) || (state == TK_PATHSTATE_HIDDEN)) {
        itemExPtr->header.x1 = itemExPtr->header.x2 =
            itemExPtr->header.y1 = itemExPtr->header.y2 = -1;
        return;
    }
    style = TkPathCanvasInheritStyle(itemPtr, TK_PATH_MERGESTYLE_NOTFILL);
    itemPtr->bbox = TkPathGetGenericBarePathBbox(ppolyPtr->atomPtr);
    TkPathIncludeArrowPointsInRect(&itemPtr->bbox, &ppolyPtr->startarrow);
    TkPathIncludeArrowPointsInRect(&itemPtr->bbox, &ppolyPtr->endarrow);
    itemPtr->totalBbox =
        TkPathGetGenericPathTotalBboxFromBare(ppolyPtr->atomPtr, &style,
        &itemPtr->bbox);
    TkPathSetGenericPathHeaderBbox(&itemExPtr->header, style.matrixPtr,
        &itemPtr->totalBbox);
    TkPathCanvasFreeInheritedStyle(&style);
}

/*--------------------------------------------------------------
 *
 * ConfigureArrows --
 *
 *  If arrowheads have been requested for a line, this function makes
 *  arrangements for the arrowheads.
 *
 * Results:
 *  Always returns TCL_OK.
 *
 * Side effects:
 *  Information in ppolyPtr is set up for one or two arrowheads. The
 *  startarrowPtr and endarrowPtr polygons are allocated and initialized,
 *  if need be, and the end points of the line are adjusted so that a
 *  thick line doesn't stick out past the arrowheads.
 *
 *--------------------------------------------------------------
 */

static int
ConfigureArrows(
    Tk_PathCanvas canvas,
    PpolyItem * ppolyPtr)
{
TkPathPoint *pfirstp;
TkPathPoint psecond;
TkPathPoint ppenult;
TkPathPoint *plastp;

int error = TkPathGetSegmentsFromPathAtomList(ppolyPtr->atomPtr,
        &pfirstp, &psecond, &ppenult, &plastp);

    if(error == TCL_OK) {
TkPathPoint pfirst = *pfirstp;
TkPathPoint plast = *plastp;
Tk_PathStyle *lineStyle = &ppolyPtr->headerEx.style;
int isOpen = lineStyle->fill == NULL &&
    ((pfirst.x != plast.x) || (pfirst.y != plast.y));

        TkPathPreconfigureArrow(&pfirst, &ppolyPtr->startarrow);
        TkPathPreconfigureArrow(&plast, &ppolyPtr->endarrow);

        *pfirstp = TkPathConfigureArrow(pfirst, psecond,
            &ppolyPtr->startarrow, lineStyle, isOpen);
        *plastp = TkPathConfigureArrow(plast, ppenult, &ppolyPtr->endarrow,
            lineStyle, isOpen);
    } else {
        TkPathFreeArrow(&ppolyPtr->startarrow);
        TkPathFreeArrow(&ppolyPtr->endarrow);
    }

    return TCL_OK;
}

static int
ConfigurePpoly(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int objc,
    Tcl_Obj * const objv[],
    int flags)
{
    PpolyItem *ppolyPtr = (PpolyItem *) itemPtr;
    Tk_PathItemEx *itemExPtr = &ppolyPtr->headerEx;
    Tk_PathStyle *stylePtr = &itemExPtr->style;
    Tk_Window tkwin;
    Tk_SavedOptions savedOptions;
    Tcl_Obj *errorResult = NULL;
    int mask, error;

    tkwin = Tk_PathCanvasTkwin(canvas);
    for(error = 0; error <= 1; error++) {
        if(!error) {
            if(Tk_SetOptions(interp, (char *)ppolyPtr, itemPtr->optionTable,
                    objc, objv, tkwin, &savedOptions, &mask) != TCL_OK) {
                continue;
            }
        } else {
            errorResult = Tcl_GetObjResult(interp);
            Tcl_IncrRefCount(errorResult);
            Tk_RestoreSavedOptions(&savedOptions);
        }
        if(TkPathCanvasItemExConfigure(interp, canvas, itemExPtr,
                mask) != TCL_OK) {
            continue;
        }

        /*
         * If we reach this on the first pass we are OK and continue below.
         */
        break;
    }
    if(!error) {
        Tk_FreeSavedOptions(&savedOptions);
        stylePtr->mask |= mask;
    }
    stylePtr->strokeOpacity = MAX(0.0, MIN(1.0, stylePtr->strokeOpacity));

    ConfigureArrows(canvas, ppolyPtr);

    if(error) {
        Tcl_SetObjResult(interp, errorResult);
        Tcl_DecrRefCount(errorResult);
        return TCL_ERROR;
    } else {
        ComputePpolyBbox(canvas, ppolyPtr);
        return TCL_OK;
    }
}

static void
DeletePpoly(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    Display * display)
{
PpolyItem *ppolyPtr = (PpolyItem *) itemPtr;
Tk_PathItemEx *itemExPtr = &ppolyPtr->headerEx;
Tk_PathStyle *stylePtr = &itemExPtr->style;

    if(stylePtr->fill != NULL) {
        TkPathFreePathColor(stylePtr->fill);
    }
    if(itemExPtr->styleInst != NULL) {
        TkPathFreeStyle(itemExPtr->styleInst);
    }
    if(ppolyPtr->atomPtr != NULL) {
        TkPathFreeAtoms(ppolyPtr->atomPtr);
        ppolyPtr->atomPtr = NULL;
    }
    TkPathFreeArrow(&ppolyPtr->startarrow);
    TkPathFreeArrow(&ppolyPtr->endarrow);
    Tk_FreeConfigOptions((char *)itemPtr, itemPtr->optionTable,
        Tk_PathCanvasTkwin(canvas));
}

static void
DisplayPpoly(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    Display * display,
    Drawable drawable,
    int x,
    int y,
    int width,
    int height)
{
    PpolyItem *ppolyPtr = (PpolyItem *) itemPtr;
    TkPathMatrix m = TkPathGetCanvasTMatrix(canvas);
    Tk_PathStyle style;

    style = TkPathCanvasInheritStyle(itemPtr, 0);
    TkPathDrawPath(ContextOfCanvas(canvas), ppolyPtr->atomPtr, &style,
        &m, &itemPtr->bbox);
    /*
     * Display arrowheads, if they are wanted.
     */
    TkPathDisplayArrow(canvas, &ppolyPtr->startarrow, &style, &m,
        &itemPtr->bbox);
    TkPathDisplayArrow(canvas, &ppolyPtr->endarrow, &style, &m, &itemPtr->bbox);
    TkPathCanvasFreeInheritedStyle(&style);
}

static void
PpolyBbox(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int mask)
{
    PpolyItem *ppolyPtr = (PpolyItem *) itemPtr;
    ComputePpolyBbox(canvas, ppolyPtr);
}

static double
PpolyToPoint(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    double *pointPtr)
{
    PpolyItem *ppolyPtr = (PpolyItem *) itemPtr;
    Tk_PathStyle style;
    double dist;
    long flags;

    flags = (ppolyPtr->type == kPpolyTypePolyline) ?
        TK_PATH_MERGESTYLE_NOTFILL : 0;
    style = TkPathCanvasInheritStyle(itemPtr, flags);
    dist = TkPathGenericPathToPoint(canvas, itemPtr, &style, ppolyPtr->atomPtr,
        ppolyPtr->maxNumSegments, pointPtr);
    TkPathCanvasFreeInheritedStyle(&style);
    return dist;
}

static int
PpolyToArea(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    double *areaPtr)
{
    PpolyItem *ppolyPtr = (PpolyItem *) itemPtr;
    Tk_PathStyle style;
    int area;
    long flags;

    flags = (ppolyPtr->type == kPpolyTypePolyline) ?
        TK_PATH_MERGESTYLE_NOTFILL : 0;
    style = TkPathCanvasInheritStyle(itemPtr, flags);
    area = TkPathGenericPathToArea(canvas, itemPtr, &style,
        ppolyPtr->atomPtr, ppolyPtr->maxNumSegments, areaPtr);
    TkPathCanvasFreeInheritedStyle(&style);
    return area;
}

static int
PpolyToPdf(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int objc,
    Tcl_Obj * const objv[],
    int prepass)
{
    Tk_PathStyle style;
    PpolyItem *ppolyPtr = (PpolyItem *) itemPtr;
    Tk_PathState state = itemPtr->state;
    int result;

    if(state == TK_PATHSTATE_NULL) {
        state = TkPathCanvasState(canvas);
    }
    if((ppolyPtr->atomPtr == NULL) || (state == TK_PATHSTATE_HIDDEN)) {
        return TCL_OK;
    }
    style = TkPathCanvasInheritStyle(itemPtr, 0);
    result = TkPathPdf(interp, ppolyPtr->atomPtr, &style, &itemPtr->bbox,
        objc, objv);
    if(result == TCL_OK) {
        result = TkPathPdfArrow(interp, &ppolyPtr->startarrow, &style);
        if(result == TCL_OK) {
            result = TkPathPdfArrow(interp, &ppolyPtr->endarrow, &style);
        }
    }
    TkPathCanvasFreeInheritedStyle(&style);
    return result;
}

static void
ScalePpoly(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int compensate,
    double originX,
    double originY,
    double scaleX,
    double scaleY)
{
    PpolyItem *ppolyPtr = (PpolyItem *) itemPtr;

    TkPathCompensateScale(itemPtr, compensate, &originX, &originY, &scaleX,
        &scaleY);

    TkPathScalePathAtoms(ppolyPtr->atomPtr, originX, originY, scaleX, scaleY);
    TkPathScalePathRect(&itemPtr->bbox, originX, originY, scaleX, scaleY);
    TkPathScaleArrow(&ppolyPtr->startarrow, originX, originY, scaleX, scaleY);
    TkPathScaleArrow(&ppolyPtr->endarrow, originX, originY, scaleX, scaleY);
    ConfigureArrows(canvas, ppolyPtr);
    TkPathScaleItemHeader(itemPtr, originX, originY, scaleX, scaleY);
}

static void
TranslatePpoly(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int compensate,
    double deltaX,
    double deltaY)
{
    PpolyItem *ppolyPtr = (PpolyItem *) itemPtr;

    TkPathCompensateTranslate(itemPtr, compensate, &deltaX, &deltaY);

    TkPathTranslatePathAtoms(ppolyPtr->atomPtr, deltaX, deltaY);
    TkPathTranslatePathRect(&itemPtr->bbox, deltaX, deltaY);
    TkPathTranslateArrow(&ppolyPtr->startarrow, deltaX, deltaY);
    TkPathTranslateArrow(&ppolyPtr->endarrow, deltaX, deltaY);
    TkPathTranslateItemHeader(itemPtr, deltaX, deltaY);
}

/*
 *--------------------------------------------------------------
 *
 * CoordsForPolygonline --
 *
 *        Used as coordProc for polyline and polygon items.
 *
 * Results:
 *        Standard tcl result.
 *
 * Side effects:
 *        May store new atoms in atomPtrPtr and max number of points
 *        in lenPtr.
 *
 *--------------------------------------------------------------
 */

int
CoordsForPolygonline(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    int closed,                /* Polyline (0) or polygon (1) */
    int objc,
    Tcl_Obj * const objv[],
    TkPathAtom ** atomPtrPtr,
    int *lenPtr)
{
    TkPathAtom *atomPtr = *atomPtrPtr;

    if(objc == 0) {
    Tcl_Obj *obj = Tcl_NewListObj(0, (Tcl_Obj **) NULL);

        while(atomPtr != NULL) {
            switch (atomPtr->type) {
            case TK_PATH_ATOM_M:{
    TkMoveToAtom *move = (TkMoveToAtom *) atomPtr;
                Tcl_ListObjAppendElement(interp, obj,
                    Tcl_NewDoubleObj(move->x));
                Tcl_ListObjAppendElement(interp, obj,
                    Tcl_NewDoubleObj(move->y));
                break;
            }
            case TK_PATH_ATOM_L:{
    TkLineToAtom *line = (TkLineToAtom *) atomPtr;
                Tcl_ListObjAppendElement(interp, obj,
                    Tcl_NewDoubleObj(line->x));
                Tcl_ListObjAppendElement(interp, obj,
                    Tcl_NewDoubleObj(line->y));
                break;
            }
            case TK_PATH_ATOM_Z:{

                break;
            }
            default:{
                /* empty */
            }
            }
            atomPtr = atomPtr->nextPtr;
        }
        Tcl_SetObjResult(interp, obj);
        *lenPtr = 0;
        return TCL_OK;
    }
    if(objc == 1) {
        if(Tcl_ListObjGetElements(interp, objv[0], &objc,
                (Tcl_Obj ***) & objv) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if(objc & 1) {
    char buf[64 + TCL_INTEGER_SPACE];
        sprintf(buf, "wrong # coordinates: expected an even number, got %d",
            objc);
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
        return TCL_ERROR;
    } else if(objc < 4) {
    char buf[64 + TCL_INTEGER_SPACE];
        sprintf(buf, "wrong # coordinates: expected at least 4, got %d", objc);
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
        return TCL_ERROR;
    } else {
    int i;
    double x, y;
    double firstX = 0.0, firstY = 0.0;
    TkPathAtom *firstAtomPtr = NULL;

        /*
         * Free any old stuff.
         */
        if(atomPtr != NULL) {
            TkPathFreeAtoms(atomPtr);
            atomPtr = NULL;
        }
        for(i = 0; i < objc; i += 2) {
            if(Tk_PathCanvasGetCoordFromObj(interp, canvas, objv[i],
                    &x) != TCL_OK) {
                /* @@@ error recovery? */
                return TCL_ERROR;
            }
            if(Tk_PathCanvasGetCoordFromObj(interp, canvas, objv[i + 1],
                    &y) != TCL_OK) {
                return TCL_ERROR;
            }
            if(i == 0) {
                firstX = x;
                firstY = y;
                atomPtr = TkPathNewMoveToAtom(x, y);
                firstAtomPtr = atomPtr;
            } else {
                atomPtr->nextPtr = TkPathNewLineToAtom(x, y);
                atomPtr = atomPtr->nextPtr;
            }
        }
        if(closed) {
            atomPtr->nextPtr = TkPathNewCloseAtom(firstX, firstY);
        }
        *atomPtrPtr = firstAtomPtr;
        *lenPtr = i / 2 + 2;
    }
    return TCL_OK;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
