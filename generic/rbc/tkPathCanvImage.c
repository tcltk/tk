/*
 * tkCanvPimage.c --
 *
 *    This file implements an image canvas item modelled after its
 *    SVG counterpart. See http://www.w3.org/TR/SVG11/.
 *
 * Copyright (c) 2007-2008  Mats Bengtsson
 *
 */

#include "tkPathInt.h"

#define BBOX_OUT 2.0

/*
 * The structure below defines the record for each path item.
 */

typedef struct PimageItem  {
    Tk_PathItemEx headerEx; /* Generic stuff that's the same for all
                 * types.  MUST BE FIRST IN STRUCTURE. */
    Tk_PathCanvas canvas;   /* Canvas containing item. */
    double fillOpacity;
    TkPathMatrix *matrixPtr;        /*  a  b   default (NULL): 1 0
                c  d           0 1
                tx ty            0 0 */
    double coord[2];        /* nw coord. */
    Tcl_Obj *imageObj;        /* Object describing the -image option.
                 * NULL means no image right now. */
    Tk_Image image;        /* Image to display in window, or NULL if
                 * no image at present. */
    Tk_PhotoHandle photo;
    double width;        /* If 0 use natural width or height. */
    double height;
    Tk_Anchor anchor;       /* Where to anchor image relative to (x,y). */
    XColor *tintColor;
    double tintAmount;
    int interpolation;
    TkPathRect *srcRegionPtr;
} PimageItem;

/*
 * Prototypes for procedures defined in this file:
 */

static void    ComputePimageBbox(Tk_PathCanvas canvas, PimageItem *pimagePtr);
static int    ConfigurePimage(Tcl_Interp *interp, Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, int objc,
            Tcl_Obj *const objv[], int flags);
static int    CreatePimage(Tcl_Interp *interp,
            Tk_PathCanvas canvas, struct Tk_PathItem *itemPtr,
            int objc, Tcl_Obj *const objv[]);
static void    DeletePimage(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, Display *display);
static void    DisplayPimage(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, Display *display, Drawable drawable,
            int x, int y, int width, int height);
static void    PimageBbox(Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
            int mask);
static int    PimageCoords(Tcl_Interp *interp,
            Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
            int objc, Tcl_Obj *const objv[]);
static int    PimageToArea(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, double *rectPtr);
static int    PimageToPdf(Tcl_Interp *interp, Tk_PathCanvas canvas,
            Tk_PathItem *item, int objc, Tcl_Obj *const objv[],
            int prepass);
static double    PimageToPoint(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, double *coordPtr);
static void    ScalePimage(Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
            int compensate, double originX, double originY,
            double scaleX, double scaleY);
static void    TranslatePimage(Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
            int compensate, double deltaX, double deltaY);
static void    ImageChangedProc(ClientData clientData,
            int x, int y, int width, int height, int imgWidth,
            int imgHeight);
static void    PimageStyleChangedProc(ClientData clientData, int flags);

enum {
    PIMAGE_OPTION_INDEX_FILLOPACITY =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 1)),
    PIMAGE_OPTION_INDEX_HEIGHT =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 2)),
    PIMAGE_OPTION_INDEX_IMAGE =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 3)),
    PIMAGE_OPTION_INDEX_MATRIX =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 4)),
    PIMAGE_OPTION_INDEX_WIDTH =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 5)),
    PIMAGE_OPTION_INDEX_ANCHOR =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 6)),
    PIMAGE_OPTION_INDEX_TINTCOLOR =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 7)),
    PIMAGE_OPTION_INDEX_TINTAMOUNT =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 8)),
    PIMAGE_OPTION_INDEX_INTERPOLATION =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 9)),
    PIMAGE_OPTION_INDEX_SRCREGION =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 10))
};

static const char *imageInterpolationST[] = {
    "none", "fast", "best", NULL
};

static int    PathRectSetOption(ClientData clientData, Tcl_Interp *interp,
            Tk_Window tkwin, Tcl_Obj **value, char *recordPtr,
            int internalOffset, char *oldInternalPtr, int flags);
static Tcl_Obj *PathRectGetOption(ClientData clientData, Tk_Window tkwin,
            char *recordPtr, int internalOffset);
static void    PathRectRestoreOption(ClientData clientData, Tk_Window tkwin,
            char *internalPtr, char *oldInternalPtr);
static void    PathRectFreeOption(ClientData clientData, Tk_Window tkwin,
            char *internalPtr);

#define TK_PATH_STYLE_CUSTOM_OPTION_PATHRECT    \
    static Tk_ObjCustomOption pathRectCO = {    \
    "pathrect",                \
    PathRectSetOption,            \
    PathRectGetOption,            \
    PathRectRestoreOption,            \
    PathRectFreeOption,            \
    (ClientData) NULL            \
    };

TK_PATH_STYLE_CUSTOM_OPTION_MATRIX
TK_PATH_CUSTOM_OPTION_TAGS
TK_PATH_OPTION_STRING_TABLES_STATE
TK_PATH_STYLE_CUSTOM_OPTION_PATHRECT

#define TK_PATH_OPTION_SPEC_FILLOPACITY                \
    {TK_OPTION_DOUBLE, "-fillopacity", NULL, NULL,        \
    "1.0", -1, Tk_Offset(PimageItem, fillOpacity),        \
    0, 0, PIMAGE_OPTION_INDEX_FILLOPACITY}

#define TK_PATH_OPTION_SPEC_HEIGHT                    \
    {TK_OPTION_DOUBLE, "-height", NULL, NULL,            \
    "0", -1, Tk_Offset(PimageItem, height),            \
    0, 0, PIMAGE_OPTION_INDEX_HEIGHT}

#define TK_PATH_OPTION_SPEC_IMAGE                    \
    {TK_OPTION_STRING, "-image", NULL, NULL,            \
    NULL, Tk_Offset(PimageItem, imageObj), -1,        \
    TK_OPTION_NULL_OK, 0, PIMAGE_OPTION_INDEX_IMAGE}

#define TK_PATH_OPTION_SPEC_MATRIX                    \
    {TK_OPTION_CUSTOM, "-matrix", NULL, NULL,            \
    NULL, -1, Tk_Offset(PimageItem, matrixPtr),        \
    TK_OPTION_NULL_OK, (ClientData) &matrixCO,        \
    PIMAGE_OPTION_INDEX_MATRIX}

#define TK_PATH_OPTION_SPEC_WIDTH                    \
    {TK_OPTION_DOUBLE, "-width", NULL, NULL,            \
    "0", -1, Tk_Offset(PimageItem, width),            \
    0, 0, PIMAGE_OPTION_INDEX_WIDTH}

#define TK_PATH_OPTION_SPEC_ANCHOR                    \
    {TK_OPTION_ANCHOR, "-anchor", NULL, NULL,            \
    "nw", -1, Tk_Offset(PimageItem, anchor),        \
    0, 0, 0}

#define TK_PATH_OPTION_SPEC_TINTCOLOR                \
    {TK_OPTION_COLOR, "-tintcolor", NULL, NULL,            \
    NULL, -1, Tk_Offset(PimageItem, tintColor),        \
    TK_OPTION_NULL_OK, 0, PIMAGE_OPTION_INDEX_TINTCOLOR}

#define TK_PATH_OPTION_SPEC_TINTAMOUNT                \
    {TK_OPTION_DOUBLE, "-tintamount", NULL, NULL,        \
    "0.5", -1, Tk_Offset(PimageItem, tintAmount),        \
    0, 0, PIMAGE_OPTION_INDEX_TINTAMOUNT}

#define TK_PATH_OPTION_SPEC_INTERPOLATION                \
    {TK_OPTION_STRING_TABLE, "-interpolation", NULL, NULL,  \
    "fast", -1, Tk_Offset(PimageItem, interpolation),   \
    0, (ClientData) imageInterpolationST, 0}

#define TK_PATH_OPTION_SPEC_SRCREGION                \
    {TK_OPTION_CUSTOM, "-srcregion", NULL, NULL,        \
    NULL, -1, Tk_Offset(PimageItem, srcRegionPtr),        \
    TK_OPTION_NULL_OK, (ClientData) &pathRectCO,        \
    PIMAGE_OPTION_INDEX_SRCREGION}

static Tk_OptionSpec optionSpecs[] = {
    TK_PATH_OPTION_SPEC_CORE(Tk_PathItemEx),
    TK_PATH_OPTION_SPEC_PARENT,
    TK_PATH_OPTION_SPEC_MATRIX,
    TK_PATH_OPTION_SPEC_FILLOPACITY,
    TK_PATH_OPTION_SPEC_HEIGHT,
    TK_PATH_OPTION_SPEC_IMAGE,
    TK_PATH_OPTION_SPEC_WIDTH,
    TK_PATH_OPTION_SPEC_ANCHOR,
    TK_PATH_OPTION_SPEC_TINTCOLOR,
    TK_PATH_OPTION_SPEC_TINTAMOUNT,
    TK_PATH_OPTION_SPEC_INTERPOLATION,
    TK_PATH_OPTION_SPEC_SRCREGION,
    TK_PATH_OPTION_SPEC_END
};

/*
 * The structures below defines the 'image' item type by means
 * of procedures that can be invoked by generic item code.
 */

Tk_PathItemType tkPathTypeImage = {
    "image",                /* name */
    sizeof(PimageItem),            /* itemSize */
    CreatePimage,            /* createProc */
    optionSpecs,            /* optionSpecs */
    ConfigurePimage,            /* configureProc */
    PimageCoords,            /* coordProc */
    DeletePimage,            /* deleteProc */
    DisplayPimage,            /* displayProc */
    0,                    /* flags */
    PimageBbox,                /* bboxProc */
    PimageToPoint,            /* pointProc */
    PimageToArea,            /* areaProc */
    PimageToPdf,            /* pdfProc */
    ScalePimage,            /* scaleProc */
    TranslatePimage,            /* translateProc */
    (Tk_PathItemIndexProc *) NULL,    /* indexProc */
    (Tk_PathItemCursorProc *) NULL,    /* icursorProc */
    (Tk_PathItemSelectionProc *) NULL,    /* selectionProc */
    (Tk_PathItemInsertProc *) NULL,    /* insertProc */
    (Tk_PathItemDCharsProc *) NULL,    /* dTextProc */
    (Tk_PathItemType *) NULL,        /* nextPtr */
    1,                    /* isPathType */
};

static int
CreatePimage(Tcl_Interp *interp, Tk_PathCanvas canvas,
    Tk_PathItem *itemPtr, int objc, Tcl_Obj *const objv[])
{
    PimageItem *pimagePtr = (PimageItem *) itemPtr;
    Tk_PathItemEx *itemExPtr = &pimagePtr->headerEx;
    int    i;
    Tk_OptionTable optionTable;

    if (objc == 0) {
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
    pimagePtr->fillOpacity = 1.0;
    pimagePtr->matrixPtr = NULL;
    pimagePtr->imageObj = NULL;
    pimagePtr->image = NULL;
    pimagePtr->photo = NULL;
    pimagePtr->height = 0;
    pimagePtr->width = 0;
    pimagePtr->anchor = TK_ANCHOR_NW;
    pimagePtr->tintColor = NULL;
    pimagePtr->tintAmount = 0.0;
    pimagePtr->interpolation = TK_PATH_IMAGEINTERPOLATION_Fast;
    pimagePtr->srcRegionPtr = NULL;
    itemPtr->bbox = TkPathNewEmptyPathRect();

    optionTable = Tk_CreateOptionTable(interp, optionSpecs);
    itemPtr->optionTable = optionTable;
    if (Tk_InitOptions(interp, (char *) pimagePtr, optionTable,
        Tk_PathCanvasTkwin(canvas)) != TCL_OK) {
    goto error;
    }

    for (i = 1; i < objc; i++) {
    char *arg = Tcl_GetString(objv[i]);
    if ((arg[0] == '-') && (arg[1] >= 'a') && (arg[1] <= 'z')) {
        break;
    }
    }
    if (TkPathCoordsForPointItems(interp, canvas, pimagePtr->coord, i, objv)
    != TCL_OK) {
    goto error;
    }
    if (ConfigurePimage(interp, canvas, itemPtr, objc-i, objv+i, 0)
    == TCL_OK) {
    return TCL_OK;
    }

    error:
    /*
     * NB: We must unlink the item here since the ConfigurePimage()
     *     link it to the root by default.
     */
    TkPathCanvasItemDetach(itemPtr);
    DeletePimage(canvas, itemPtr, Tk_Display(Tk_PathCanvasTkwin(canvas)));
    return TCL_ERROR;
}

static int
PimageCoords(Tcl_Interp *interp, Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
    int objc, Tcl_Obj *const objv[])
{
    PimageItem *pimagePtr = (PimageItem *) itemPtr;
    int result;

    result = TkPathCoordsForPointItems(interp, canvas, pimagePtr->coord, objc, objv);
    if ((result == TCL_OK) && ((objc == 1) || (objc == 2))) {
    ComputePimageBbox(canvas, pimagePtr);
    }
    return result;
}

/*
 * This is just a convenience function to obtain any style matrix.
 */

static TkPathMatrix
GetTMatrix(PimageItem *pimagePtr)
{
    TkPathMatrix *matrixPtr;
    Tk_PathStyle *stylePtr;
    TkPathMatrix matrix = TkPathCanvasInheritTMatrix((Tk_PathItem *) pimagePtr);

    matrixPtr = pimagePtr->matrixPtr;
    if (pimagePtr->headerEx.styleInst != NULL) {
    stylePtr = pimagePtr->headerEx.styleInst->masterPtr;
    if (stylePtr->mask & TK_PATH_STYLE_OPTION_MATRIX) {
        matrixPtr = stylePtr->matrixPtr;
    }
    }
    if (matrixPtr != NULL) {
    TkPathMMulTMatrix(matrixPtr, &matrix);
    }
    return matrix;
}

void
ComputePimageBbox(Tk_PathCanvas canvas, PimageItem *pimagePtr)
{
    Tk_PathItem *itemPtr = (Tk_PathItem *)pimagePtr;
    Tk_PathState state = pimagePtr->headerEx.header.state;
    TkPathMatrix matrix;
    double width = 0.0, height = 0.0;
    TkPathRect bbox;

    if (state == TK_PATHSTATE_NULL) {
    state = TkPathCanvasState(canvas);
    }
    if (pimagePtr->image == NULL) {
    pimagePtr->headerEx.header.x1 = pimagePtr->headerEx.header.x2 =
    pimagePtr->headerEx.header.y1 = pimagePtr->headerEx.header.y2 = -1;
    return;
    }
    if (pimagePtr->srcRegionPtr) {
    width  = pimagePtr->srcRegionPtr->x2 - pimagePtr->srcRegionPtr->x1;
    height = pimagePtr->srcRegionPtr->y2 - pimagePtr->srcRegionPtr->y1;
    } else {
    int iwidth = 0, iheight = 0;
    Tk_SizeOfImage(pimagePtr->image, &iwidth, &iheight);
    width = iwidth;
    height = iheight;
    }
    if (pimagePtr->width > 0.0) {
    width = pimagePtr->width + 1.0;
    }
    if (pimagePtr->height > 0.0) {
    height = pimagePtr->height + 1.0;
    }

    switch (pimagePtr->anchor) {
    case TK_ANCHOR_NW:
    case TK_ANCHOR_W:
    case TK_ANCHOR_SW:
        bbox.x1 = pimagePtr->coord[0];
        break;

    case TK_ANCHOR_N:
    case TK_ANCHOR_CENTER:
    case TK_ANCHOR_S:
        bbox.x1 = pimagePtr->coord[0] - width / 2.0;
        break;

    case TK_ANCHOR_NE:
    case TK_ANCHOR_E:
    case TK_ANCHOR_SE:
        bbox.x1 = pimagePtr->coord[0] - width;
        break;
    }
    bbox.x2 = bbox.x1 + width;

    switch (pimagePtr->anchor) {
    case TK_ANCHOR_NW:
    case TK_ANCHOR_N:
    case TK_ANCHOR_NE:
        bbox.y1 = pimagePtr->coord[1];
        break;

    case TK_ANCHOR_W:
    case TK_ANCHOR_CENTER:
    case TK_ANCHOR_E:
        bbox.y1 = pimagePtr->coord[1] - height / 2.0;
        break;

    case TK_ANCHOR_SW:
    case TK_ANCHOR_S:
    case TK_ANCHOR_SE:
        bbox.y1 = pimagePtr->coord[1] - height;
        break;
    }
    bbox.y2 = bbox.y1 + height;

    bbox.x1 -= BBOX_OUT;
    bbox.x2 += BBOX_OUT;
    bbox.y1 -= BBOX_OUT;
    bbox.y2 += BBOX_OUT;

    itemPtr->bbox = bbox;
    itemPtr->totalBbox = itemPtr->bbox;    /* FIXME */
    matrix = GetTMatrix(pimagePtr);
    TkPathSetGenericPathHeaderBbox(&pimagePtr->headerEx.header, &matrix, &bbox);
}

static int
ConfigurePimage(Tcl_Interp *interp, Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
    int objc, Tcl_Obj *const objv[], int flags)
{
    PimageItem *pimagePtr = (PimageItem *) itemPtr;
    Tk_Window tkwin;
    Tk_Image image;
    Tk_PhotoHandle photo;
    Tk_SavedOptions savedOptions;
    Tk_PathItem *parentPtr;
    Tcl_Obj *errorResult = NULL;
    int error, mask;

    tkwin = Tk_PathCanvasTkwin(canvas);
    for (error = 0; error <= 1; error++) {
    if (!error) {
        if (Tk_SetOptions(interp, (char *) pimagePtr, itemPtr->optionTable,
            objc, objv, tkwin, &savedOptions, &mask) != TCL_OK) {
        continue;
        }
    } else {
	    if (errorResult != NULL) {
            Tcl_DecrRefCount(errorResult);
	    }
        errorResult = Tcl_GetObjResult(interp);
        Tcl_IncrRefCount(errorResult);
        Tk_RestoreSavedOptions(&savedOptions);
    }

    /*
     * Take each custom option, not handled in Tk_SetOptions, in turn.
     */
    if (mask & TK_PATH_CORE_OPTION_PARENT) {
        if (TkPathCanvasFindGroup(interp, canvas, itemPtr->parentObj,
                      &parentPtr) != TCL_OK) {
        continue;
        }
        TkPathCanvasSetParent(parentPtr, itemPtr);
    } else if ((itemPtr->id != 0) && (itemPtr->parentPtr == NULL)) {
        /*
         * If item not root and parent not set we must set it
         * to root by default.
         */
        TkPathCanvasSetParentToRoot(itemPtr);
    }

    /*
     * If we have got a style name it's options take precedence
     * over the actual path configuration options. This is how SVG does it.
     * Good or bad?
     */
    if (mask & TK_PATH_CORE_OPTION_STYLENAME) {
        TkPathStyleInst *styleInst = NULL;

        if (pimagePtr->headerEx.styleObj != NULL) {
        styleInst = TkPathGetStyle(interp,
            Tcl_GetString(pimagePtr->headerEx.styleObj),
            TkPathCanvasStyleTable(canvas), PimageStyleChangedProc,
            (ClientData) itemPtr);
        if (styleInst == NULL) {
            continue;
        }
        } else {
        styleInst = NULL;
        }
        if (pimagePtr->headerEx.styleInst != NULL) {
        TkPathFreeStyle(pimagePtr->headerEx.styleInst);
        }
        pimagePtr->headerEx.styleInst = styleInst;
    }

    /*
     * Create the image.  Save the old image around and don't free it
     * until after the new one is allocated.  This keeps the reference
     * count from going to zero so the image doesn't have to be recreated
     * if it hasn't changed.
     */
    if (!error && (mask & PIMAGE_OPTION_INDEX_IMAGE)) {
        if (pimagePtr->imageObj != NULL) {
        const char *name = Tcl_GetString(pimagePtr->imageObj);

        image = Tk_GetImage(interp, tkwin, name,
            ImageChangedProc, (ClientData) pimagePtr);
        if (image == NULL) {
            continue;
        }
        photo = Tk_FindPhoto(interp, name);
        if (photo == NULL) {
		    Tk_FreeImage(image);
		    Tcl_SetObjResult(interp,
			Tcl_NewStringObj("no photo with the given name", -1));
            continue;
        }
        } else {
        image = NULL;
        photo = NULL;
        }
        if (pimagePtr->image != NULL) {
        Tk_FreeImage(pimagePtr->image);
        }
        pimagePtr->image = image;
        pimagePtr->photo = photo;
    }

    /*
     * If we reach this on the first pass we are OK and continue below.
     */
    break;
    }
    if (!error) {
    Tk_FreeSavedOptions(&savedOptions);
    }
    pimagePtr->fillOpacity = MAX(0.0, MIN(1.0, pimagePtr->fillOpacity));

    if (errorResult != NULL) {
        Tcl_SetObjResult(interp, errorResult);
        Tcl_DecrRefCount(errorResult);
        return TCL_ERROR;
    }
    /*
     * Recompute bounding box for path.
     */
    ComputePimageBbox(canvas, pimagePtr);
    return TCL_OK;
}

static void
DeletePimage(Tk_PathCanvas canvas, Tk_PathItem *itemPtr, Display *display)
{
    PimageItem *pimagePtr = (PimageItem *) itemPtr;

    if (pimagePtr->headerEx.styleInst != NULL) {
    TkPathFreeStyle(pimagePtr->headerEx.styleInst);
    }
    if (pimagePtr->image != NULL) {
    Tk_FreeImage(pimagePtr->image);
    }
    Tk_FreeConfigOptions((char *) pimagePtr, itemPtr->optionTable,
             Tk_PathCanvasTkwin(canvas));
}

static void
DisplayPimage(Tk_PathCanvas canvas, Tk_PathItem *itemPtr, Display *display,
    Drawable drawable,
    int x, int y, int width, int height)
{
    PimageItem *pimagePtr = (PimageItem *) itemPtr;
    TkPathMatrix m = TkPathGetCanvasTMatrix(canvas);
    TkPathContext ctx;

    ctx = ContextOfCanvas(canvas);
    TkPathPushTMatrix(ctx, &m);
    m = GetTMatrix(pimagePtr);
    TkPathPushTMatrix(ctx, &m);
    /* @@@ Maybe we should taking care of x, y etc.? */
    TkPathImage(ctx, pimagePtr->image, pimagePtr->photo,
        itemPtr->bbox.x1+BBOX_OUT, itemPtr->bbox.y1+BBOX_OUT,
        pimagePtr->width, pimagePtr->height, pimagePtr->fillOpacity,
        pimagePtr->tintColor, pimagePtr->tintAmount,
        pimagePtr->interpolation,
        pimagePtr->srcRegionPtr);
}

static void
PimageBbox(Tk_PathCanvas canvas, Tk_PathItem *itemPtr, int mask)
{
    PimageItem *pimagePtr = (PimageItem *) itemPtr;
    ComputePimageBbox(canvas, pimagePtr);
}

static double
PimageToPoint(Tk_PathCanvas canvas, Tk_PathItem *itemPtr, double *pointPtr)
{
    PimageItem *pimagePtr = (PimageItem *) itemPtr;
    TkPathMatrix m = GetTMatrix(pimagePtr);
    return TkPathRectToPointWithMatrix(itemPtr->bbox, &m, pointPtr);
}

static int
PimageToArea(Tk_PathCanvas canvas, Tk_PathItem *itemPtr, double *areaPtr)
{
    PimageItem *pimagePtr = (PimageItem *) itemPtr;
    TkPathMatrix m = GetTMatrix(pimagePtr);
    return TkPathRectToAreaWithMatrix(itemPtr->bbox, &m, areaPtr);
}

static int
PimageToPdf(Tcl_Interp *interp, Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
    int objc, Tcl_Obj *const objv[], int prepass)
{
    Tk_PathStyle style;
    PimageItem *pimagePtr = (PimageItem *) itemPtr;
    Tcl_Obj *ret, *obj;
    TkPathMatrix matrix = { 1., 0., 0., 1., 0., 0.};
    Tk_PathState state = itemPtr->state;
    Tk_PhotoImageBlock block;
    int x, y;
    unsigned char *p;
    double tintR, tintG, tintB, tintAmount;
    TkPathRect srcRegion, bbox;

    if (state == TK_PATHSTATE_NULL) {
    state = TkPathCanvasState(canvas);
    }
    if ((pimagePtr->photo == NULL) || (state == TK_PATHSTATE_HIDDEN)) {
    return TCL_OK;    /* nothing to display */
    }
    Tk_PhotoGetImage(pimagePtr->photo, &block);
    if ((block.width <= 0) || (block.height <= 0)) {
    return TCL_OK;    /* nothing to display */
    }
    if (pimagePtr->srcRegionPtr == NULL) {
    srcRegion.x1 = 0;
    srcRegion.y1 = 0;
    srcRegion.x2 = block.width;
    srcRegion.y2 = block.height;
    } else {
    srcRegion = *pimagePtr->srcRegionPtr;
    }
    bbox = itemPtr->bbox;
    /* undo effect of BBOX_OUT */
    bbox.x1 += BBOX_OUT - 1;
    bbox.x2 -= BBOX_OUT + 1;
    bbox.y1 += BBOX_OUT - 1;
    bbox.y2 -= BBOX_OUT + 1;

    if (objc > 0) {
    long id;
    int retc, opacity, tx, ty;
    int zLen;
    double scaleX, scaleY, dx, dy;
    Tcl_Obj *pixObj, *nameObj, **retv;

    /*
     * Callback provided, first make alpha mask.
     */
    opacity = (int)(pimagePtr->fillOpacity * 256);
    if (opacity > 256) {
        opacity = 256;
    } else if (opacity < 0) {
        opacity = 0;
    }
    pixObj = Tcl_NewObj();
    p = Tcl_SetByteArrayLength(pixObj, block.width * block.height);
    for (y = 0; y < block.height; y++) {
        unsigned char *q = block.pixelPtr + y * block.pitch;

        for (x = 0; x < block.width; x++) {
        *p++ = ((int) q[block.offset[3]] * opacity) >> 8;
        q += block.pixelSize;
        }
    }
    zLen = 0;
    if (Tcl_ZlibDeflate(interp, TCL_ZLIB_FORMAT_ZLIB,
            pixObj, 9, NULL) == TCL_OK) {
    p = Tcl_GetByteArrayFromObj(Tcl_GetObjResult(interp), &zLen);
    if (zLen > 0) {
        Tcl_DecrRefCount(pixObj);
        pixObj = Tcl_GetObjResult(interp);
    }
    Tcl_IncrRefCount(pixObj);
    }
    Tcl_ResetResult(interp);
    obj = Tcl_NewObj();
    Tcl_AppendPrintfToObj(obj, "<<\n/Type /XObject\n/Subtype /Image\n"
                  "/ColorSpace /DeviceGray\n/BitsPerComponent 8\n"
                  "/Width %d\n/Height %d\n",
                  block.width, block.height);
    if (zLen > 0) {
        Tcl_AppendPrintfToObj(obj, "/Filter /FlateDecode\n"
                  "/Length %d\n>>\nstream\n", zLen);
    } else
    {
        Tcl_AppendPrintfToObj(obj, "/Length %d\n>>\nstream\n",
                  block.width * block.height);
    }
    Tcl_AppendObjToObj(obj, pixObj);
    Tcl_DecrRefCount(pixObj);
    Tcl_AppendToObj(obj, "\nendstream\n", 11);
    ret = Tcl_DuplicateObj(objv[0]);
    Tcl_IncrRefCount(ret);
    if ((Tcl_ListObjAppendElement(interp, ret,
                      Tcl_NewIntObj(block.width)) != TCL_OK) ||
        (Tcl_ListObjAppendElement(interp, ret,
                      Tcl_NewIntObj(block.height)) != TCL_OK) ||
        (Tcl_ListObjAppendElement(interp, ret, obj) != TCL_OK)) {
        Tcl_DecrRefCount(ret);
        Tcl_DecrRefCount(obj);
        return TCL_ERROR;
    }
    if (Tcl_EvalObjEx(interp, ret, TCL_EVAL_DIRECT) != TCL_OK) {
        Tcl_DecrRefCount(ret);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(ret);
    /*
     * Remember result information (object id of alpha channel).
     */
    if (Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp),
                   &retc, &retv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (retc < 1) {
        Tcl_SetResult(interp, (char *) "missing PDF id", TCL_STATIC);
        return TCL_ERROR;
    }
    Tcl_GetLongFromObj(NULL, retv[0], &id);
    /*
     * Make RGB image.
     */
    tintAmount = pimagePtr->tintAmount;
    if ((pimagePtr->tintColor != NULL) && (tintAmount > 0.0)) {
        if (tintAmount > 1.0) {
        tintAmount = 1.0;
        }
        tintR = (double) (pimagePtr->tintColor->red >> 8) / 0xff;
        tintG = (double) (pimagePtr->tintColor->green >> 8) / 0xff;
        tintB = (double) (pimagePtr->tintColor->blue >> 8) / 0xff;
    } else {
        tintAmount = 0.0;
        tintR = 0.0;
        tintG = 0.0;
        tintB = 0.0;
    }
    pixObj = Tcl_NewObj();
    p = Tcl_SetByteArrayLength(pixObj, block.width * block.height * 3);
    for (y = 0; y < block.height; y++) {
        unsigned char R, G, B;
        unsigned char *q = block.pixelPtr + y * block.pitch;

        for (x = 0; x < block.width; x++) {
        R = q[block.offset[0]];
        G = q[block.offset[1]];
        B = q[block.offset[2]];
        if (tintAmount > 0.0) {
            int RR, GG, BB;

            RR = (int)( (1.0 - tintAmount) * R +
            (tintAmount * tintR * 0.2126 * R +
             tintAmount * tintR * 0.7152 * G +
             tintAmount * tintR * 0.0722 * B));
            GG = (int)((1.0 - tintAmount) * G +
            (tintAmount * tintG * 0.2126 * R +
             tintAmount * tintG * 0.7152 * G +
             tintAmount * tintG * 0.0722 * B));
            BB = (int)((1.0 - tintAmount) * B +
            (tintAmount * tintB * 0.2126 * R +
             tintAmount * tintB * 0.7152 * G +
             tintAmount * tintB * 0.0722 * B));
            R = (RR > 0xff) ? 0xff : RR;
            G = (GG > 0xff) ? 0xff : GG;
            B = (BB > 0xff) ? 0xff : BB;
        }
        *p++ = R;
        *p++ = G;
        *p++ = B;
        q += block.pixelSize;
        }
    }
    zLen = 0;
    if (Tcl_ZlibDeflate(interp, TCL_ZLIB_FORMAT_ZLIB,
            pixObj, 9, NULL) == TCL_OK) {
    p = Tcl_GetByteArrayFromObj(Tcl_GetObjResult(interp), &zLen);
    if (zLen > 0) {
        Tcl_DecrRefCount(pixObj);
        pixObj = Tcl_GetObjResult(interp);
    }
    Tcl_IncrRefCount(pixObj);
    }
    Tcl_ResetResult(interp);
    obj = Tcl_NewObj();
    Tcl_AppendPrintfToObj(obj, "<<\n/Type /XObject\n/Subtype /Image\n"
                  "/ColorSpace /DeviceRGB\n/BitsPerComponent 8\n"
                  "/Width %d\n/Height %d\n/SMask %ld 0 R\n",
                  block.width, block.height, id);
    if ((pimagePtr->interpolation == TK_PATH_IMAGEINTERPOLATION_Fast) ||
        (pimagePtr->interpolation == TK_PATH_IMAGEINTERPOLATION_Best)) {
        Tcl_AppendToObj(obj, "/Interpolate true\n", 18);
    }
    if (zLen > 0) {
        Tcl_AppendPrintfToObj(obj, "/Filter /FlateDecode\n"
                  "/Length %d\n>>\nstream\n", zLen);
    } else
    {
        Tcl_AppendPrintfToObj(obj, "/Length %d\n>>\nstream\n",
                  block.width * block.height * 3);
    }
    Tcl_AppendObjToObj(obj, pixObj);
    Tcl_DecrRefCount(pixObj);
    Tcl_AppendToObj(obj, "\nendstream\n", 11);
    ret = Tcl_DuplicateObj(objv[0]);
    Tcl_IncrRefCount(ret);
    if ((Tcl_ListObjAppendElement(interp, ret,
                      Tcl_NewIntObj(block.width)) != TCL_OK) ||
        (Tcl_ListObjAppendElement(interp, ret,
                      Tcl_NewIntObj(block.height)) != TCL_OK) ||
        (Tcl_ListObjAppendElement(interp, ret, obj) != TCL_OK)) {
        Tcl_DecrRefCount(ret);
        Tcl_DecrRefCount(obj);
        return TCL_ERROR;
    }
    if (Tcl_EvalObjEx(interp, ret, TCL_EVAL_DIRECT) != TCL_OK) {
        Tcl_DecrRefCount(ret);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(ret);
    /*
     * Remember result information (name of RGB image).
     */
    if (Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp),
                   &retc, &retv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (retc < 2) {
        Tcl_SetResult(interp, (char *) "missing PDF id/name", TCL_STATIC);
        return TCL_ERROR;
    }
    nameObj = retv[1];
    Tcl_IncrRefCount(nameObj);
    /*
     * Print image object(s).
     */
    ret = Tcl_NewObj();
    style = TkPathCanvasInheritStyle(itemPtr, 0);
    matrix = GetTMatrix(pimagePtr);
    /* TkPathMatrix */
    TkPathPdfNumber(ret, 6, matrix.a, " ");
    TkPathPdfNumber(ret, 6, matrix.b, " ");
    TkPathPdfNumber(ret, 6, matrix.c, " ");
    TkPathPdfNumber(ret, 6, matrix.d, " ");
    TkPathPdfNumber(ret, 3, matrix.tx, " ");
    TkPathPdfNumber(ret, 3, matrix.ty, " cm\n");
    /* translate */
    Tcl_AppendToObj(ret, "1 0 0 1 ", 8);
    TkPathPdfNumber(ret, 3, bbox.x1, " ");
    TkPathPdfNumber(ret, 3, bbox.y2, " cm\n");
    /* clipping */
    Tcl_AppendToObj(ret, "0 0 m ", 6);
    TkPathPdfNumber(ret, 3, bbox.x2 - bbox.x1, " 0 l ");
    TkPathPdfNumber(ret, 3, bbox.x2 - bbox.x1, " ");
    TkPathPdfNumber(ret, 3, bbox.y1 - bbox.y2, " l 0 ");
    TkPathPdfNumber(ret, 3, bbox.y1 - bbox.y2, " l W n\n");
    tx = ty = 1;
    dx = srcRegion.x2 - srcRegion.x1;
    if (dx > block.width) {
        tx += (int)((srcRegion.x2 - srcRegion.x1) / block.width);
    } else {
        srcRegion.x1 = fmod(srcRegion.x1, (double) block.width);
        srcRegion.x2 = srcRegion.x1 + dx;
        if (srcRegion.x1 != 0) {
        tx++;
        }
    }
    dy = srcRegion.y2 - srcRegion.y1;
    if (dy > block.height) {
        ty += (int)((srcRegion.y2 - srcRegion.y1) / block.height);
    } else {
        srcRegion.y1 = fmod(srcRegion.y1, (double) block.height);
        srcRegion.y2 = srcRegion.y1 + dy;
        if (srcRegion.y1 != 0) {
        ty++;
        }
    }
    if (pimagePtr->width > 0) {
        scaleX = pimagePtr->width / (double) block.width;
        scaleX /= (srcRegion.x2 - srcRegion.x1) / block.width;
    } else {
        scaleX = 1.0;
    }
    if (pimagePtr->height > 0) {
        scaleY = pimagePtr->height / (double) block.height;
        scaleY /= (srcRegion.y2 - srcRegion.y1) / block.height;
    } else {
        scaleY = 1.0;
    }
    for (x = 0; x < tx; x++) {
        for (y = 0; y < ty; y++) {
        /* translate */
        Tcl_AppendToObj(ret, "q\n1 0 0 1 ", 10);
        TkPathPdfNumber(ret, 6,    (x * block.width - srcRegion.x1) *
                scaleX, " ");
        TkPathPdfNumber(ret, 6,    -(srcRegion.y2 - (ty - y) *
                      block.height) * scaleY, " cm\n");
        /* scale to requested image size */
        TkPathPdfNumber(ret, 6,    scaleX * block.width, " 0 0 ");
        TkPathPdfNumber(ret, 6,    -scaleY * block.height, " 0 0 cm\n");
        /* print it */
        Tcl_AppendPrintfToObj(ret, "/%s Do\nQ\n",
            Tcl_GetString(nameObj));
        }
    }
    Tcl_DecrRefCount(nameObj);
    Tcl_SetObjResult(interp, ret);
    TkPathCanvasFreeInheritedStyle(&style);
    } else {
    /*
     * No callback, make direct RGB image without alpha mask.
     */
    ret = Tcl_NewObj();
    style = TkPathCanvasInheritStyle(itemPtr, 0);
    matrix = GetTMatrix(pimagePtr);
    /* TkPathMatrix */
    TkPathPdfNumber(ret, 6, matrix.a, " ");
    TkPathPdfNumber(ret, 6, matrix.b, " ");
    TkPathPdfNumber(ret, 6, matrix.c, " ");
    TkPathPdfNumber(ret, 6, matrix.d, " ");
    TkPathPdfNumber(ret, 3, matrix.tx, " ");
    TkPathPdfNumber(ret, 3, matrix.ty, " cm\n");
    /* translate */
    Tcl_AppendToObj(ret, "1 0 0 1 ", 8);
    TkPathPdfNumber(ret, 3, bbox.x1, " ");
    TkPathPdfNumber(ret, 3, bbox.y2, " cm\n");
    /* clipping */
    Tcl_AppendToObj(ret, "0 0 m ", 6);
    TkPathPdfNumber(ret, 3, bbox.x2 - bbox.x1, " 0 l ");
    TkPathPdfNumber(ret, 3, bbox.x2 - bbox.x1, " ");
    TkPathPdfNumber(ret, 3, bbox.y1 - bbox.y2, " l 0 ");
    TkPathPdfNumber(ret, 3, bbox.y1 - bbox.y2, " l W n\n");
    /* scale to requested image size, no cropping/tiling */
    TkPathPdfNumber(ret, 6,    (pimagePtr->width > 0) ? pimagePtr->width :
            (double) block.width, " 0 0 ");
    TkPathPdfNumber(ret, 6,    (pimagePtr->height > 0) ? pimagePtr->height :
            (double) -block.height, " 0 0 cm\n");
    Tcl_AppendToObj(ret, "BI\n", 3);
    Tcl_AppendPrintfToObj(ret, "/W %d\n/H %d\n", block.width, block.height);
    Tcl_AppendToObj(ret, "/CS /RGB\n/BPC 8\nID\n", 19);
    tintAmount = pimagePtr->tintAmount;
    if ((pimagePtr->tintColor != NULL) && (tintAmount > 0.0)) {
        if (tintAmount > 1.0) {
        tintAmount = 1.0;
        }
        tintR = (double) (pimagePtr->tintColor->red >> 8) / 0xff;
        tintG = (double) (pimagePtr->tintColor->green >> 8) / 0xff;
        tintB = (double) (pimagePtr->tintColor->blue >> 8) / 0xff;
    } else {
        tintAmount = 0.0;
        tintR = 0.0;
        tintG = 0.0;
        tintB = 0.0;
    }
    obj = Tcl_NewObj();
    p = Tcl_SetByteArrayLength(obj, block.width * block.height * 3);
    for (y = 0; y < block.height; y++) {
        unsigned char R, G, B;
        unsigned char *q = block.pixelPtr + y * block.pitch;

        for (x = 0; x < block.width; x++) {
        R = q[block.offset[0]];
        G = q[block.offset[1]];
        B = q[block.offset[2]];
        if (tintAmount > 0.0) {
            int RR, GG, BB;

            RR = (int)((1.0 - tintAmount) * R +
            (tintAmount * tintR * 0.2126 * R +
             tintAmount * tintR * 0.7152 * G +
             tintAmount * tintR * 0.0722 * B));
            GG = (int)((1.0 - tintAmount) * G +
            (tintAmount * tintG * 0.2126 * R +
             tintAmount * tintG * 0.7152 * G +
             tintAmount * tintG * 0.0722 * B));
            BB = (int)((1.0 - tintAmount) * B +
            (tintAmount * tintB * 0.2126 * R +
             tintAmount * tintB * 0.7152 * G +
             tintAmount * tintB * 0.0722 * B));
            R = (RR > 0xff) ? 0xff : RR;
            G = (GG > 0xff) ? 0xff : GG;
            B = (BB > 0xff) ? 0xff : BB;
        }
        *p++ = R;
        *p++ = G;
        *p++ = B;
        q += block.pixelSize;
        }
    }
    Tcl_AppendObjToObj(ret, obj);
    Tcl_DecrRefCount(obj);
    Tcl_AppendToObj(ret, "\nEI\n", 4);
    Tcl_SetObjResult(interp, ret);
    TkPathCanvasFreeInheritedStyle(&style);
    }
    return TCL_OK;
}

static void
ScalePimage(Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
    int compensate, double originX, double originY,
    double scaleX, double scaleY)
{
    /* Skip? */
}

static void
TranslatePimage(Tk_PathCanvas canvas, Tk_PathItem *itemPtr, int compensate,
    double deltaX, double deltaY)
{
    PimageItem *pimagePtr = (PimageItem *) itemPtr;

    TkPathCompensateTranslate(itemPtr, compensate, &deltaX, &deltaY);

    /* Translate coordinates. */
    pimagePtr->coord[0] += deltaX;
    pimagePtr->coord[1] += deltaY;
    /* Recompute bounding box. */
    ComputePimageBbox(canvas, pimagePtr);
}

static void
ImageChangedProc(
    ClientData clientData,    /* Pointer to canvas item for image. */
    int x, int y,        /* Upper left pixel (within image)
                 * that must be redisplayed. */
    int width, int height,    /* Dimensions of area to redisplay
                 * (may be <= 0). */
    int imgWidth, int imgHeight)/* New dimensions of image. */
{
    PimageItem *pimagePtr = (PimageItem *) clientData;

    /*
     * If the image's size changed and it's not anchored at its
     * northwest corner then just redisplay the entire area of the
     * image.  This is a bit over-conservative, but we need to do
     * something because a size change also means a position change.
     */

    /* @@@ MUST consider our own width and height settings as well and TMatrix. */

    if (((pimagePtr->headerEx.header.x2 - pimagePtr->headerEx.header.x1) !=
     imgWidth)
    || ((pimagePtr->headerEx.header.y2 - pimagePtr->headerEx.header.y1) !=
        imgHeight)) {
    x = y = 0;
    width = imgWidth;
    height = imgHeight;
    Tk_PathCanvasEventuallyRedraw(pimagePtr->headerEx.canvas,
        pimagePtr->headerEx.header.x1, pimagePtr->headerEx.header.y1,
        pimagePtr->headerEx.header.x2, pimagePtr->headerEx.header.y2);
    }
    ComputePimageBbox(pimagePtr->headerEx.canvas, pimagePtr);
    Tk_PathCanvasEventuallyRedraw(pimagePtr->headerEx.canvas,
        pimagePtr->headerEx.header.x1 + x,
        pimagePtr->headerEx.header.y1 + y,
        (int) (pimagePtr->headerEx.header.x1 + x + width),
        (int) (pimagePtr->headerEx.header.y1 + y + height));
}

static void
PimageStyleChangedProc(ClientData clientData, int flags)
{
    Tk_PathItem *itemPtr = (Tk_PathItem *) clientData;
    PimageItem *pimagePtr = (PimageItem *) itemPtr;

    if (flags) {
    if (flags & TK_PATH_STYLE_FLAG_DELETE) {
        TkPathFreeStyle(pimagePtr->headerEx.styleInst);
        pimagePtr->headerEx.styleInst = NULL;
        Tcl_DecrRefCount(pimagePtr->headerEx.styleObj);
        pimagePtr->headerEx.styleObj = NULL;
    }
    Tk_PathCanvasEventuallyRedraw(pimagePtr->headerEx.canvas,
        itemPtr->x1, itemPtr->y1,
        itemPtr->x2, itemPtr->y2);
    }
}

static int
PathGetPathRect(
    Tcl_Interp* interp,
    const char *list,   /* Object containg the lists for the matrix. */
    TkPathRect *rectPtr) /* Where to store TkPathMatrix corresponding
                 * to list. Must be allocated! */
{
    const char **argv = NULL;
    int i, argc;
    int result = TCL_OK;
    double tmp[4];

    /* Check matrix consistency. */
    if (Tcl_SplitList(interp, list, &argc, &argv) != TCL_OK) {
    result = TCL_ERROR;
    goto bail;
    }
    if (argc != 4) {
    Tcl_AppendResult(interp, "rect \"", list, "\" is inconsistent",
        (char *) NULL);
    result = TCL_ERROR;
    goto bail;
    }

    /* Take each row in turn. */
    for (i = 0; i < 4; i++) {
    if (Tcl_GetDouble(interp, argv[i], &(tmp[i])) != TCL_OK) {
        Tcl_AppendResult(interp, "rect \"", list, "\" is inconsistent", (char *) NULL);
        result = TCL_ERROR;
        goto bail;
    }
    }

    /* TkPathRect. */
    rectPtr->x1  = tmp[0];
    rectPtr->y1  = tmp[1];
    rectPtr->x2  = tmp[2];
    rectPtr->y2  = tmp[3];

bail:
    if (argv != NULL) {
    Tcl_Free((char *) argv);
    }
    return result;
}

static int
PathGetTclObjFromPathRect(
    Tcl_Interp* interp,
    TkPathRect *rectPtr,
    Tcl_Obj **listObjPtrPtr)
{
    Tcl_Obj     *listObj;

    /* @@@ Error handling remains. */

    listObj = Tcl_NewListObj( 0, (Tcl_Obj **) NULL );
    if (rectPtr != NULL) {
    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(rectPtr->x1));
    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(rectPtr->y1));
    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(rectPtr->x2));
    Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(rectPtr->y2));
    }
    *listObjPtrPtr = listObj;
    return TCL_OK;
}

/*
 * The -srcregion custom option.
 */

static int
PathRectSetOption(
    ClientData clientData,
    Tcl_Interp *interp,     /* Current interp; may be used for errors. */
    Tk_Window tkwin,        /* Window for which option is being set. */
    Tcl_Obj **value,        /* Pointer to the pointer to the value object.
                 * We use a pointer to the pointer because
                 * we may need to return a value (NULL). */
    char *recordPtr,        /* Pointer to storage for the widget record. */
    int internalOffset,     /* Offset within *recordPtr at which the
                   internal value is to be stored. */
    char *oldInternalPtr,   /* Pointer to storage for the old value. */
    int flags)          /* Flags for the option, set Tk_SetOptions. */
{
    char *internalPtr;      /* Points to location in record where
                 * internal representation of value should
                 * be stored, or NULL. */
    char *list;
    int length;
    Tcl_Obj *valuePtr;
    TkPathRect *newPtr;

    valuePtr = *value;
    if (internalOffset >= 0) {
    internalPtr = recordPtr + internalOffset;
    } else {
    internalPtr = NULL;
    }
    if ((flags & TK_OPTION_NULL_OK) && TkPathObjectIsEmpty(valuePtr)) {
    valuePtr = NULL;
    }
    if (internalPtr != NULL) {
    if (valuePtr != NULL) {
        list = Tcl_GetStringFromObj(valuePtr, &length);
        newPtr = (TkPathRect *) ckalloc(sizeof(TkPathRect));
        if (PathGetPathRect(interp, list, newPtr) != TCL_OK) {
        ckfree((char *) newPtr);
        return TCL_ERROR;
        }
    } else {
        newPtr = NULL;
    }
    *((TkPathRect **) oldInternalPtr) = *((TkPathRect **) internalPtr);
    *((TkPathRect **) internalPtr) = newPtr;
    }
    return TCL_OK;
}

static Tcl_Obj *
PathRectGetOption(
    ClientData clientData,
    Tk_Window tkwin,
    char *recordPtr,        /* Pointer to widget record. */
    int internalOffset)     /* Offset within *recordPtr containing the
                 * value. */
{
    char    *internalPtr;
    TkPathRect     *pathRectPtr;
    Tcl_Obj     *listObj;

    /* @@@ An alternative to this could be to have an objOffset in option table. */
    internalPtr = recordPtr + internalOffset;
    pathRectPtr = *((TkPathRect **) internalPtr);
    PathGetTclObjFromPathRect(NULL, pathRectPtr, &listObj);
    return listObj;
}

static void
PathRectRestoreOption(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr,      /* Pointer to storage for value. */
    char *oldInternalPtr)   /* Pointer to old value. */
{
    *(TkPathRect **)internalPtr = *(TkPathRect **)oldInternalPtr;
}

static void
PathRectFreeOption(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr)      /* Pointer to storage for value. */
{
    if (*((char **) internalPtr) != NULL) {
    ckfree(*((char **) internalPtr));
    *((char **) internalPtr) = NULL;
    }
}
/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
