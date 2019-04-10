/*
 * tkoPathCanvEllipse.c --
 *
 *    This file implements the circle and ellipse canvas items modelled after its
 *    SVG counterpart. See http://www.w3.org/TR/SVG11/.
 *
 * Copyright (c) 2007-2008  Mats Bengtsson
 *
 */

#include "tkoPath.h"

/*
 * The structure below defines the record for each circle and ellipse item.
 */

typedef struct EllipseItem {
    Tk_PathItemEx headerEx;    /* Generic stuff that's the same for all
                                * path types.  MUST BE FIRST IN STRUCTURE. */
    char type;                 /* Circle or ellipse. */
    double center[2];          /* Center coord. */
    double rx;                 /* Radius. Circle uses rx for overall radius. */
    double ry;
} EllipseItem;

enum {
    kOvalTypeCircle,
    kOvalTypeEllipse
};

/*
 * Prototypes for procedures defined in this file:
 */

static void ComputeEllipseBbox(
    Tk_PathCanvas canvas,
    EllipseItem * ellPtr);
static int ConfigureEllipse(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int objc,
    Tcl_Obj * const objv[],
    int flags);
static int CreateAny(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    struct Tk_PathItem *itemPtr,
    int objc,
    Tcl_Obj * const objv[],
    char type);
static int CreateCircle(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    struct Tk_PathItem *itemPtr,
    int objc,
    Tcl_Obj * const objv[]);
static int CreateEllipse(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    struct Tk_PathItem *itemPtr,
    int objc,
    Tcl_Obj * const objv[]);
static void DeleteEllipse(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    Display * display);
static void DisplayEllipse(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    Display * display,
    Drawable drawable,
    int x,
    int y,
    int width,
    int height);
static void EllipseBbox(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int mask);
static int EllipseCoords(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int objc,
    Tcl_Obj * const objv[]);
static int EllipseToArea(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    double *rectPtr);
static int EllipseToPdf(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int objc,
    Tcl_Obj * const objv[],
    int prepass);
static double EllipseToPoint(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    double *coordPtr);
static void ScaleEllipse(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int compensate,
    double originX,
    double originY,
    double scaleX,
    double scaleY);
static void TranslateEllipse(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int compensate,
    double deltaX,
    double deltaY);

enum {
    ELLIPSE_OPTION_INDEX_RX = (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 0)),
    ELLIPSE_OPTION_INDEX_RY = (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 1)),
    ELLIPSE_OPTION_INDEX_R = (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 2)),
};

TK_PATH_STYLE_CUSTOM_OPTION_RECORDS
    TK_PATH_CUSTOM_OPTION_TAGS
    TK_PATH_OPTION_STRING_TABLES_FILL
    TK_PATH_OPTION_STRING_TABLES_STROKE TK_PATH_OPTION_STRING_TABLES_STATE
#define TK_PATH_OPTION_SPEC_R(typeName)            \
    {TK_OPTION_DOUBLE, "-rx", NULL, NULL,        \
        "0.0", -1, Tk_Offset(typeName, rx),        \
    0, 0, ELLIPSE_OPTION_INDEX_R}
#define TK_PATH_OPTION_SPEC_RX(typeName)            \
    {TK_OPTION_DOUBLE, "-rx", NULL, NULL,        \
        "0.0", -1, Tk_Offset(typeName, rx),        \
    0, 0, ELLIPSE_OPTION_INDEX_RX}
#define TK_PATH_OPTION_SPEC_RY(typeName)            \
    {TK_OPTION_DOUBLE, "-ry", NULL, NULL,        \
        "0.0", -1, Tk_Offset(typeName, ry),        \
    0, 0, ELLIPSE_OPTION_INDEX_RY}
static Tk_OptionSpec optionSpecsCircle[] = {
    TK_PATH_OPTION_SPEC_CORE(Tk_PathItemEx),
    TK_PATH_OPTION_SPEC_PARENT,
    TK_PATH_OPTION_SPEC_STYLE_FILL(Tk_PathItemEx, ""),
    TK_PATH_OPTION_SPEC_STYLE_MATRIX(Tk_PathItemEx),
    TK_PATH_OPTION_SPEC_STYLE_STROKE(Tk_PathItemEx, "black"),
    TK_PATH_OPTION_SPEC_R(EllipseItem),
    TK_PATH_OPTION_SPEC_END
};

static Tk_OptionSpec optionSpecsEllipse[] = {
    TK_PATH_OPTION_SPEC_CORE(Tk_PathItemEx),
    TK_PATH_OPTION_SPEC_PARENT,
    TK_PATH_OPTION_SPEC_STYLE_FILL(Tk_PathItemEx, ""),
    TK_PATH_OPTION_SPEC_STYLE_MATRIX(Tk_PathItemEx),
    TK_PATH_OPTION_SPEC_STYLE_STROKE(Tk_PathItemEx, "black"),
    TK_PATH_OPTION_SPEC_RX(EllipseItem),
    TK_PATH_OPTION_SPEC_RY(EllipseItem),
    TK_PATH_OPTION_SPEC_END
};

/*
 * The structures below define the 'circle' and 'ellipse' item types by means
 * of procedures that can be invoked by generic item code.
 */

Tk_PathItemType tkPathTypeCircle = {
    "circle",  /* name */
    sizeof(EllipseItem),        /* itemSize */
    CreateCircle,       /* createProc */
    optionSpecsCircle,  /* optionSpecs */
    ConfigureEllipse,   /* configureProc */
    EllipseCoords,      /* coordProc */
    DeleteEllipse,      /* deleteProc */
    DisplayEllipse,     /* displayProc */
    0,         /* flags */
    EllipseBbox,        /* bboxProc */
    EllipseToPoint,     /* pointProc */
    EllipseToArea,      /* areaProc */
    EllipseToPdf,       /* pdfProc */
    ScaleEllipse,       /* scaleProc */
    TranslateEllipse,   /* translateProc */
    (Tk_PathItemIndexProc *) NULL,      /* indexProc */
    (Tk_PathItemCursorProc *) NULL,     /* icursorProc */
    (Tk_PathItemSelectionProc *) NULL,  /* selectionProc */
    (Tk_PathItemInsertProc *) NULL,     /* insertProc */
    (Tk_PathItemDCharsProc *) NULL,     /* dTextProc */
    (Tk_PathItemType *) NULL,   /* nextPtr */
    1,         /* isPathType */
};

Tk_PathItemType tkPathTypeEllipse = {
    "ellipse", /* name */
    sizeof(EllipseItem),        /* itemSize */
    CreateEllipse,      /* createProc */
    optionSpecsEllipse, /* optionSpecs */
    ConfigureEllipse,   /* configureProc */
    EllipseCoords,      /* coordProc */
    DeleteEllipse,      /* deleteProc */
    DisplayEllipse,     /* displayProc */
    0,         /* flags */
    EllipseBbox,        /* bboxProc */
    EllipseToPoint,     /* pointProc */
    EllipseToArea,      /* areaProc */
    EllipseToPdf,       /* pdfProc */
    ScaleEllipse,       /* scaleProc */
    TranslateEllipse,   /* translateProc */
    (Tk_PathItemIndexProc *) NULL,      /* indexProc */
    (Tk_PathItemCursorProc *) NULL,     /* icursorProc */
    (Tk_PathItemSelectionProc *) NULL,  /* selectionProc */
    (Tk_PathItemInsertProc *) NULL,     /* insertProc */
    (Tk_PathItemDCharsProc *) NULL,     /* dTextProc */
    (Tk_PathItemType *) NULL,   /* nextPtr */
    1,         /* isPathType */
};

static int
CreateCircle(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    struct Tk_PathItem *itemPtr,
    int objc,
    Tcl_Obj * const objv[])
{
    return CreateAny(interp, canvas, itemPtr, objc, objv, kOvalTypeCircle);
}

static int
CreateEllipse(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    struct Tk_PathItem *itemPtr,
    int objc,
    Tcl_Obj * const objv[])
{
    return CreateAny(interp, canvas, itemPtr, objc, objv, kOvalTypeEllipse);
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
    EllipseItem *ellPtr = (EllipseItem *) itemPtr;
    Tk_PathItemEx *itemExPtr = &ellPtr->headerEx;
    Tk_OptionTable optionTable;
    int i;

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
    itemPtr->bbox = TkPathNewEmptyPathRect();
    itemPtr->totalBbox = TkPathNewEmptyPathRect();
    ellPtr->type = type;

    if(ellPtr->type == kOvalTypeCircle) {
        optionTable = Tk_CreateOptionTable(interp, optionSpecsCircle);
    } else {
        optionTable = Tk_CreateOptionTable(interp, optionSpecsEllipse);
    }
    itemPtr->optionTable = optionTable;
    if(Tk_InitOptions(interp, (char *)ellPtr, optionTable,
            Tk_PathCanvasTkwin(canvas)) != TCL_OK) {
        goto error;
    }

    for(i = 1; i < objc; i++) {
    char *arg = Tcl_GetString(objv[i]);
        if((arg[0] == '-') && (arg[1] >= 'a') && (arg[1] <= 'z')) {
            break;
        }
    }
    if(TkPathCoordsForPointItems(interp, canvas, ellPtr->center, i,
            objv) != TCL_OK) {
        goto error;
    }
    if(ConfigureEllipse(interp, canvas, itemPtr, objc - i, objv + i,
            0) == TCL_OK) {
        return TCL_OK;
    }

  error:
    /*
     * NB: We must unlink the item here since the TkPathCanvasItemExConfigure()
     *     link it to the root by default.
     */
    TkPathCanvasItemDetach(itemPtr);
    DeleteEllipse(canvas, itemPtr, Tk_Display(Tk_PathCanvasTkwin(canvas)));
    return TCL_ERROR;
}

static int
EllipseCoords(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int objc,
    Tcl_Obj * const objv[])
{
    EllipseItem *ellPtr = (EllipseItem *) itemPtr;
    int result;

    result =
        TkPathCoordsForPointItems(interp, canvas, ellPtr->center, objc, objv);
    if((result == TCL_OK) && ((objc == 1) || (objc == 2))) {
        ComputeEllipseBbox(canvas, ellPtr);
    }
    return result;
}

static TkPathRect
GetBareBbox(
    EllipseItem * ellPtr)
{
TkPathRect bbox;

    bbox.x1 = ellPtr->center[0] - ellPtr->rx;
    bbox.y1 = ellPtr->center[1] - ellPtr->ry;
    bbox.x2 = ellPtr->center[0] + ellPtr->rx;
    bbox.y2 = ellPtr->center[1] + ellPtr->ry;
    return bbox;
}

static void
ComputeEllipseBbox(
    Tk_PathCanvas canvas,
    EllipseItem * ellPtr)
{
Tk_PathItemEx *itemExPtr = &ellPtr->headerEx;
Tk_PathItem *itemPtr = &itemExPtr->header;
Tk_PathStyle style;
Tk_PathState state = itemExPtr->header.state;

    if(state == TK_PATHSTATE_NULL) {
        state = TkPathCanvasState(canvas);
    }
    if(state == TK_PATHSTATE_HIDDEN) {
        itemExPtr->header.x1 = itemExPtr->header.x2 =
            itemExPtr->header.y1 = itemExPtr->header.y2 = -1;
        return;
    }
    style = TkPathCanvasInheritStyle(itemPtr, TK_PATH_MERGESTYLE_NOTFILL);
    itemPtr->bbox = GetBareBbox(ellPtr);
    itemPtr->totalBbox =
        TkPathGetGenericPathTotalBboxFromBare(NULL, &style, &itemPtr->bbox);
    TkPathSetGenericPathHeaderBbox(&itemExPtr->header, style.matrixPtr,
        &itemPtr->totalBbox);
    TkPathCanvasFreeInheritedStyle(&style);
}

static int
ConfigureEllipse(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int objc,
    Tcl_Obj * const objv[],
    int flags)
{
    EllipseItem *ellPtr = (EllipseItem *) itemPtr;
    Tk_PathItemEx *itemExPtr = &ellPtr->headerEx;
    Tk_PathStyle *stylePtr = &itemExPtr->style;
    Tk_Window tkwin;
    Tk_SavedOptions savedOptions;
    Tcl_Obj *errorResult = NULL;
    int mask, error;

    tkwin = Tk_PathCanvasTkwin(canvas);
    for(error = 0; error <= 1; error++) {
        if(!error) {
            if(Tk_SetOptions(interp, (char *)ellPtr, itemPtr->optionTable,
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
    stylePtr->fillOpacity = MAX(0.0, MIN(1.0, stylePtr->fillOpacity));
    ellPtr->rx = MAX(0.0, ellPtr->rx);
    ellPtr->ry = MAX(0.0, ellPtr->ry);
    if(ellPtr->type == kOvalTypeCircle) {
        /* Practical. */
        ellPtr->ry = ellPtr->rx;
    }
    if(error) {
        Tcl_SetObjResult(interp, errorResult);
        Tcl_DecrRefCount(errorResult);
        return TCL_ERROR;
    } else {
        ComputeEllipseBbox(canvas, ellPtr);
        return TCL_OK;
    }
}

static void
DeleteEllipse(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    Display * display)
{
EllipseItem *ellPtr = (EllipseItem *) itemPtr;
Tk_PathItemEx *itemExPtr = &ellPtr->headerEx;
Tk_PathStyle *stylePtr = &itemExPtr->style;

    if(stylePtr->fill != NULL) {
        TkPathFreePathColor(stylePtr->fill);
    }
    if(itemExPtr->styleInst != NULL) {
        TkPathFreeStyle(itemExPtr->styleInst);
    }
    Tk_FreeConfigOptions((char *)itemPtr, itemPtr->optionTable,
        Tk_PathCanvasTkwin(canvas));
}

static void
DisplayEllipse(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    Display * display,
    Drawable drawable,
    int x,
    int y,
    int width,
    int height)
{
    EllipseItem *ellPtr = (EllipseItem *) itemPtr;
    TkPathMatrix m = TkPathGetCanvasTMatrix(canvas);
#if 0
    TkPathRect bbox;
#endif
    TkPathAtom *atomPtr;
    TkEllipseAtom ellAtom;
    Tk_PathStyle style;

    /*
     * We create the atom on the fly to save some memory.
     */
    atomPtr = (TkPathAtom *) & ellAtom;
    atomPtr->nextPtr = NULL;
    atomPtr->type = TK_PATH_ATOM_ELLIPSE;
    ellAtom.cx = ellPtr->center[0];
    ellAtom.cy = ellPtr->center[1];
    ellAtom.rx = ellPtr->rx;
    ellAtom.ry = ellPtr->ry;

    itemPtr->bbox = GetBareBbox(ellPtr);
    style = TkPathCanvasInheritStyle(itemPtr, 0);
    TkPathDrawPath(ContextOfCanvas(canvas), atomPtr, &style,
        &m, &itemPtr->bbox);
    TkPathCanvasFreeInheritedStyle(&style);
}

static void
EllipseBbox(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int mask)
{
    EllipseItem *ellPtr = (EllipseItem *) itemPtr;
    ComputeEllipseBbox(canvas, ellPtr);
}

static double
EllipseToPoint(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    double *pointPtr)
{
    EllipseItem *ellPtr = (EllipseItem *) itemPtr;
    Tk_PathStyle style;
    TkPathMatrix *mPtr;
    double bareOval[4];
    double width, dist;
    int rectiLinear = 0;
    int haveDist = 0;
    int filled;

    style = TkPathCanvasInheritStyle(itemPtr, 0);
    filled = HaveAnyFillFromPathColor(style.fill);
    width = 0.0;
    if(style.strokeColor != NULL) {
        width = style.strokeWidth;
    }
    mPtr = style.matrixPtr;
    if(mPtr == NULL) {
        rectiLinear = 1;
        bareOval[0] = ellPtr->center[0] - ellPtr->rx;
        bareOval[1] = ellPtr->center[1] - ellPtr->ry;
        bareOval[2] = ellPtr->center[0] + ellPtr->rx;
        bareOval[3] = ellPtr->center[1] + ellPtr->ry;

        /* For tiny points make it simple. */
        if((ellPtr->rx <= 2.0) && (ellPtr->ry <= 2.0)) {
            dist =
                hypot(ellPtr->center[0] - pointPtr[0],
                ellPtr->center[1] - pointPtr[1]);
            dist = MAX(0.0, dist - (ellPtr->rx + ellPtr->ry) / 2.0);
            haveDist = 1;
        }
    } else if((fabs(mPtr->b) == 0.0) && (fabs(mPtr->c) == 0.0)) {
    double rx, ry;

        /* This is a situation we can treat in a simplified way. Apply the transform here. */
        rectiLinear = 1;
        bareOval[0] = mPtr->a * (ellPtr->center[0] - ellPtr->rx) + mPtr->tx;
        bareOval[1] = mPtr->d * (ellPtr->center[1] - ellPtr->ry) + mPtr->ty;
        bareOval[2] = mPtr->a * (ellPtr->center[0] + ellPtr->rx) + mPtr->tx;
        bareOval[3] = mPtr->d * (ellPtr->center[1] + ellPtr->ry) + mPtr->ty;

        /* For tiny points make it simple. */
        rx = fabs(bareOval[0] - bareOval[2]) / 2.0;
        ry = fabs(bareOval[1] - bareOval[3]) / 2.0;
        if((rx <= 2.0) && (ry <= 2.0)) {
            dist = hypot((bareOval[0] + bareOval[2] / 2.0) - pointPtr[0],
                (bareOval[1] + bareOval[3] / 2.0) - pointPtr[1]);
            dist = MAX(0.0, dist - (rx + ry) / 2.0);
            haveDist = 1;
        }
    }
    if(!haveDist) {
        if(rectiLinear) {
            dist = TkOvalToPoint(bareOval, width, filled, pointPtr);
        } else {
    TkPathAtom *atomPtr;
    TkEllipseAtom ellAtom;

            /*
             * We create the atom on the fly to save some memory.
             */
            atomPtr = (TkPathAtom *) & ellAtom;
            atomPtr->nextPtr = NULL;
            atomPtr->type = TK_PATH_ATOM_ELLIPSE;
            ellAtom.cx = ellPtr->center[0];
            ellAtom.cy = ellPtr->center[1];
            ellAtom.rx = ellPtr->rx;
            ellAtom.ry = ellPtr->ry;
            dist = TkPathGenericPathToPoint(canvas, itemPtr, &style, atomPtr,
                TK_PATH_NUMSEGEMENTS_Ellipse + 1, pointPtr);
        }
    }
    TkPathCanvasFreeInheritedStyle(&style);
    return dist;
}

static int
EllipseToArea(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    double *areaPtr)
{
    EllipseItem *ellPtr = (EllipseItem *) itemPtr;
    Tk_PathStyle style;
    TkPathMatrix *mPtr;
    double center[2], bareOval[4], halfWidth;
    int rectiLinear = 0;
    int result;

    style = TkPathCanvasInheritStyle(itemPtr, 0);
    halfWidth = 0.0;
    center[0] = ellPtr->center[0];
    center[1] = ellPtr->center[1];
    if(style.strokeColor != NULL) {
        halfWidth = style.strokeWidth / 2.0;
    }
    mPtr = style.matrixPtr;
    if(mPtr == NULL) {
        rectiLinear = 1;
        bareOval[0] = center[0] - ellPtr->rx;
        bareOval[1] = center[1] - ellPtr->ry;
        bareOval[2] = center[0] + ellPtr->rx;
        bareOval[3] = center[1] + ellPtr->ry;
    } else if((fabs(mPtr->b) == 0.0) && (fabs(mPtr->c) == 0.0)) {
        /*
         * This is a situation we can treat in a simplified way.
         * Apply the transform here.
         */
        rectiLinear = 1;
        bareOval[0] = mPtr->a * (center[0] - ellPtr->rx) + mPtr->tx;
        bareOval[1] = mPtr->d * (center[1] - ellPtr->ry) + mPtr->ty;
        bareOval[2] = mPtr->a * (center[0] + ellPtr->rx) + mPtr->tx;
        bareOval[3] = mPtr->d * (center[1] + ellPtr->ry) + mPtr->ty;
        center[0] = mPtr->a * center[0] + mPtr->tx;
        center[1] = mPtr->d * center[1] + mPtr->ty;
    }

    if(rectiLinear) {
    double oval[4];

        /* @@@ Assuming untransformed strokes */
        oval[0] = bareOval[0] - halfWidth;
        oval[1] = bareOval[1] - halfWidth;
        oval[2] = bareOval[2] + halfWidth;
        oval[3] = bareOval[3] + halfWidth;

        result = TkOvalToArea(oval, areaPtr);

        /*
         * If the rectangle appears to overlap the oval and the oval
         * isn't filled, do one more check to see if perhaps all four
         * of the rectangle's corners are totally inside the oval's
         * unfilled center, in which case we should return "outside".
         */
        if((result == 0) && (style.strokeColor != NULL)
            && !HaveAnyFillFromPathColor(style.fill)) {
    double width, height;
    double xDelta1, yDelta1, xDelta2, yDelta2;

            width = (bareOval[2] - bareOval[0]) / 2.0 - halfWidth;
            height = (bareOval[3] - bareOval[1]) / 2.0 - halfWidth;
            if((width <= 0.0) || (height <= 0.0)) {
                return 0;
            }
            xDelta1 = (areaPtr[0] - center[0]) / width;
            xDelta1 *= xDelta1;
            yDelta1 = (areaPtr[1] - center[1]) / height;
            yDelta1 *= yDelta1;
            xDelta2 = (areaPtr[2] - center[0]) / width;
            xDelta2 *= xDelta2;
            yDelta2 = (areaPtr[3] - center[1]) / height;
            yDelta2 *= yDelta2;
            if(((xDelta1 + yDelta1) < 1.0)
                && ((xDelta1 + yDelta2) < 1.0)
                && ((xDelta2 + yDelta1) < 1.0)
                && ((xDelta2 + yDelta2) < 1.0)) {
                result = -1;
            }
        }
    } else {
    TkPathAtom *atomPtr;
    TkEllipseAtom ellAtom;

        /*
         * We create the atom on the fly to save some memory.
         */
        atomPtr = (TkPathAtom *) & ellAtom;
        atomPtr->nextPtr = NULL;
        atomPtr->type = TK_PATH_ATOM_ELLIPSE;
        ellAtom.cx = ellPtr->center[0];
        ellAtom.cy = ellPtr->center[1];
        ellAtom.rx = ellPtr->rx;
        ellAtom.ry = ellPtr->ry;
        result = TkPathGenericPathToArea(canvas, itemPtr, &style, atomPtr,
            TK_PATH_NUMSEGEMENTS_Ellipse + 1, areaPtr);
    }
    TkPathCanvasFreeInheritedStyle(&style);
    return result;
}

static int
EllipseToPdf(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int objc,
    Tcl_Obj * const objv[],
    int prepass)
{
    Tk_PathStyle style;
    TkPathAtom *atomPtr;
    EllipseItem *ellPtr = (EllipseItem *) itemPtr;
    TkEllipseAtom atom;
    Tk_PathState state = itemPtr->state;
    int result;

    if(state == TK_PATHSTATE_NULL) {
        state = TkPathCanvasState(canvas);
    }
    if(state == TK_PATHSTATE_HIDDEN) {
        return TCL_OK;
    }
    /* We create the atom on the fly to save some memory.  */
    atomPtr = (TkPathAtom *) & atom;
    atomPtr->nextPtr = NULL;
    atomPtr->type = TK_PATH_ATOM_ELLIPSE;
    atom.cx = ellPtr->center[0];
    atom.cy = ellPtr->center[1];
    atom.rx = ellPtr->rx;
    atom.ry = ellPtr->ry;
    style = TkPathCanvasInheritStyle(itemPtr, 0);
    result = TkPathPdf(interp, atomPtr, &style, &itemPtr->bbox, objc, objv);
    TkPathCanvasFreeInheritedStyle(&style);
    return result;
}

static void
ScaleEllipse(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int compensate,
    double originX,
    double originY,
    double scaleX,
    double scaleY)
{
    EllipseItem *ellPtr = (EllipseItem *) itemPtr;

    TkPathCompensateScale(itemPtr, compensate, &originX, &originY, &scaleX,
        &scaleY);

    ellPtr->center[0] = originX + scaleX * (ellPtr->center[0] - originX);
    ellPtr->center[1] = originY + scaleY * (ellPtr->center[1] - originY);
    ellPtr->rx *= scaleX;
    ellPtr->ry *= scaleY;
    TkPathScalePathRect(&itemPtr->bbox, originX, originY, scaleX, scaleY);
    TkPathScaleItemHeader(itemPtr, originX, originY, scaleX, scaleY);
}

static void
TranslateEllipse(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int compensate,
    double deltaX,
    double deltaY)
{
    EllipseItem *ellPtr = (EllipseItem *) itemPtr;

    TkPathCompensateTranslate(itemPtr, compensate, &deltaX, &deltaY);

    ellPtr->center[0] += deltaX;
    ellPtr->center[1] += deltaY;
#if 0
    TkPathTranslatePathAtoms(ellPtr->atomPtr, deltaX, deltaY);
#endif
    TkPathTranslatePathRect(&itemPtr->bbox, deltaX, deltaY);
    TkPathTranslateItemHeader(itemPtr, deltaX, deltaY);
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
