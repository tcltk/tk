/*
 * tkCanvPtext.c --
 *
 *    This file implements a text canvas item modelled after its
 *    SVG counterpart. See http://www.w3.org/TR/SVG11/.
 *
 * Copyright (c) 2007-2008  Mats Bengtsson
 *
 */

#include "tkPathInt.h"

/*
 * The structure below defines the record for each path item.
 */

typedef struct PtextItem  {
    Tk_PathItemEx headerEx;    /* Generic stuff that's the same for all
                 * path types.  MUST BE FIRST IN STRUCTURE. */
    Tk_PathTextStyle textStyle;
    int textAnchor;
    int fillOverStroke;        /* boolean parameter */
    double x;
    double y;
    double baseHeightRatio;
    double lineSpacing;
    Tcl_Obj *utf8Obj;        /* The actual text to display; UTF-8 */
    int numChars;        /* Length of text in characters. */
    int numBytes;        /* Length of text in bytes. */
    void *custom;        /* Place holder for platform dependent stuff. */
} PtextItem;

/*
 * Prototypes for procedures defined in this file:
 */

static void    ComputePtextBbox(Tk_PathCanvas canvas, PtextItem *ptextPtr);
static int    ConfigurePtext(Tcl_Interp *interp, Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, int objc,
            Tcl_Obj *const objv[], int flags);
static int    CreatePtext(Tcl_Interp *interp,
            Tk_PathCanvas canvas, struct Tk_PathItem *itemPtr,
            int objc, Tcl_Obj *const objv[]);
static void    DeletePtext(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, Display *display);
static void    DisplayPtext(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, Display *display, Drawable drawable,
            int x, int y, int width, int height);
static void    PtextBbox(Tk_PathCanvas canvas, Tk_PathItem *itemPtr, int mask);
static int    PtextCoords(Tcl_Interp *interp,
            Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
            int objc, Tcl_Obj *const objv[]);
static int    ProcessPtextCoords(Tcl_Interp *interp, Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, int objc, Tcl_Obj *const objv[]);
static int    PtextToArea(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, double *rectPtr);
static int    PtextToPdf(Tcl_Interp *interp,
            Tk_PathCanvas canvas, Tk_PathItem *itemPtr, int objc,
            Tcl_Obj *const objv[], int prepass);
static double    PtextToPoint(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, double *coordPtr);
static void    ScalePtext(Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
            int compensate, double originX, double originY,
            double scaleX, double scaleY);
static void    TranslatePtext(Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
            int compensate, double deltaX, double deltaY);
#if 0
static void    PtextDeleteChars(Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
            int first, int last);
#endif
static int    drawptext(Tcl_Interp *interp, PtextItem *ptextPtr,
            Tcl_Obj *ret, Tcl_Obj *cmdl);
static char *    linebreak(char *str, char **nextp);

enum {
    PRECT_OPTION_INDEX_FONTFAMILY =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 0)),
    PRECT_OPTION_INDEX_FONTSIZE =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 1)),
    PRECT_OPTION_INDEX_TEXT =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 2)),
    PRECT_OPTION_INDEX_TEXTANCHOR =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 3)),
    PRECT_OPTION_INDEX_FONTWEIGHT =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 4)),
    PRECT_OPTION_INDEX_FONTSLANT =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 5)),
    PRECT_OPTION_INDEX_FILLOVERSTROKE =
    (1L << (TK_PATH_STYLE_OPTION_INDEX_END + 6)),
};

TK_PATH_STYLE_CUSTOM_OPTION_RECORDS
TK_PATH_CUSTOM_OPTION_TAGS
TK_PATH_OPTION_STRING_TABLES_FILL
TK_PATH_OPTION_STRING_TABLES_STROKE
TK_PATH_OPTION_STRING_TABLES_STATE

/*
 * Best would be to extract font information from the named font
 * "TkDefaultFont" but the option defaults need static strings.
 * Perhaps using NULL and extracting family and size dynamically?
 */
#if (defined(__WIN32__) || defined(_WIN32) || \
    defined(__CYGWIN__) || defined(__MINGW32__)) && !defined(PLATFORM_SDL)
#   define DEF_PATHCANVTEXT_FONTFAMILY         "Tahoma"
#   define DEF_PATHCANVTEXT_FONTSIZE         "8"
#else
#   if defined(MAC_OSX_TK)
#    define DEF_PATHCANVTEXT_FONTFAMILY     "Lucida Grande"
#    define DEF_PATHCANVTEXT_FONTSIZE     "13"
#   else
#    define DEF_PATHCANVTEXT_FONTFAMILY     "Helvetica"
#    define DEF_PATHCANVTEXT_FONTSIZE     "12"
#   endif
#endif
#define DEF_PATHCANVTEXT_FONTWEIGHT "normal"
#define DEF_PATHCANVTEXT_FONTSLANT  "normal"

/*
 * The enum TK_PATH_TEXTANCHOR_Start... MUST be kept in sync!
 */
static const char *textAnchorST[] = {
    "start", "middle", "end", "n", "w", "s", "e",
    "nw", "ne", "sw", "se", "c", NULL
};

static const char *fontWeightST[] = {
    "normal", "bold", NULL
};

static const char *fontSlantST[] = {
    "normal", "italic", "oblique", NULL
};

#define TK_PATH_OPTION_SPEC_FONTFAMILY                \
    {TK_OPTION_STRING, "-fontfamily", NULL, NULL,        \
        DEF_PATHCANVTEXT_FONTFAMILY, -1,            \
        Tk_Offset(PtextItem, textStyle.fontFamily),        \
        0, 0, PRECT_OPTION_INDEX_FONTFAMILY}

#define TK_PATH_OPTION_SPEC_FONTSIZE                \
    {TK_OPTION_DOUBLE, "-fontsize", NULL, NULL,            \
        DEF_PATHCANVTEXT_FONTSIZE, -1,            \
        Tk_Offset(PtextItem, textStyle.fontSize),        \
        0, 0, PRECT_OPTION_INDEX_FONTSIZE}

#define TK_PATH_OPTION_SPEC_TEXT                    \
    {TK_OPTION_STRING, "-text", NULL, NULL,            \
        NULL, Tk_Offset(PtextItem, utf8Obj), -1,        \
        TK_OPTION_NULL_OK, 0, PRECT_OPTION_INDEX_TEXT}

#define TK_PATH_OPTION_SPEC_TEXTANCHOR                \
    {TK_OPTION_STRING_TABLE, "-textanchor", NULL, NULL,        \
        "start", -1, Tk_Offset(PtextItem, textAnchor),    \
        0, (ClientData) textAnchorST, 0}

#define TK_PATH_OPTION_SPEC_FONTWEIGHT                \
    {TK_OPTION_STRING_TABLE, "-fontweight", NULL, NULL,        \
        DEF_PATHCANVTEXT_FONTWEIGHT, -1,            \
        Tk_Offset(PtextItem, textStyle.fontWeight),        \
        0, (ClientData) fontWeightST, PRECT_OPTION_INDEX_FONTWEIGHT}

#define TK_PATH_OPTION_SPEC_FONTSLANT                \
    {TK_OPTION_STRING_TABLE, "-fontslant", NULL, NULL,        \
        DEF_PATHCANVTEXT_FONTSLANT, -1,            \
        Tk_Offset(PtextItem, textStyle.fontSlant),        \
        0, (ClientData) fontSlantST, PRECT_OPTION_INDEX_FONTSLANT}

#define TK_PATH_OPTION_SPEC_FILLOVERSTROKE                \
    {TK_OPTION_BOOLEAN, "-filloverstroke", NULL, NULL,        \
        0, -1, Tk_Offset(PtextItem, fillOverStroke),    \
        0, 0, PRECT_OPTION_INDEX_FILLOVERSTROKE}

static Tk_OptionSpec optionSpecs[] = {
    TK_PATH_OPTION_SPEC_CORE(Tk_PathItemEx),
    TK_PATH_OPTION_SPEC_PARENT,
    TK_PATH_OPTION_SPEC_STYLE_FILL(Tk_PathItemEx, "black"),
    TK_PATH_OPTION_SPEC_STYLE_MATRIX(Tk_PathItemEx),
    TK_PATH_OPTION_SPEC_STYLE_STROKE(Tk_PathItemEx, ""),
    TK_PATH_OPTION_SPEC_FONTFAMILY,
    TK_PATH_OPTION_SPEC_FONTSIZE,
    TK_PATH_OPTION_SPEC_FONTSLANT,
    TK_PATH_OPTION_SPEC_FONTWEIGHT,
    TK_PATH_OPTION_SPEC_TEXT,
    TK_PATH_OPTION_SPEC_TEXTANCHOR,
    TK_PATH_OPTION_SPEC_FILLOVERSTROKE,
    TK_PATH_OPTION_SPEC_END
};

/*
 * The structures below defines the 'prect' item type by means
 * of procedures that can be invoked by generic item code.
 */

Tk_PathItemType tkPathTypeText = {
    "text",                /* name */
    sizeof(PtextItem),            /* itemSize */
    CreatePtext,            /* createProc */
    optionSpecs,            /* configSpecs */
    ConfigurePtext,            /* configureProc */
    PtextCoords,            /* coordProc */
    DeletePtext,            /* deleteProc */
    DisplayPtext,            /* displayProc */
    0,                    /* flags */
    PtextBbox,                /* bboxProc */
    PtextToPoint,            /* pointProc */
    PtextToArea,            /* areaProc */
    PtextToPdf,                /* pdfProc */
    ScalePtext,                /* scaleProc */
    TranslatePtext,            /* translateProc */
    (Tk_PathItemIndexProc *) NULL,    /* indexProc */
    (Tk_PathItemCursorProc *) NULL,    /* icursorProc */
    (Tk_PathItemSelectionProc *) NULL,    /* selectionProc */
    (Tk_PathItemInsertProc *) NULL,    /* insertProc */
    (Tk_PathItemDCharsProc *) NULL,    /* dTextProc */
    (Tk_PathItemType *) NULL,        /* nextPtr */
    1,                    /* isPathType */
};

static int
CreatePtext(Tcl_Interp *interp, Tk_PathCanvas canvas,
    struct Tk_PathItem *itemPtr,
    int objc, Tcl_Obj *const objv[])
{
    PtextItem *ptextPtr = (PtextItem *) itemPtr;
    Tk_PathItemEx *itemExPtr = &ptextPtr->headerEx;
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
    itemPtr->bbox = TkPathNewEmptyPathRect();
    ptextPtr->utf8Obj = NULL;
    ptextPtr->numChars = 0;
    ptextPtr->numBytes = 0;
    ptextPtr->textAnchor = TK_PATH_TEXTANCHOR_Start;
    ptextPtr->textStyle.fontFamily = NULL;
    ptextPtr->textStyle.fontSize = 0.0;
    ptextPtr->fillOverStroke = 0;
    ptextPtr->custom = NULL;

    optionTable = Tk_CreateOptionTable(interp, optionSpecs);
    itemPtr->optionTable = optionTable;
    if (Tk_InitOptions(interp, (char *) ptextPtr, optionTable,
        Tk_PathCanvasTkwin(canvas)) != TCL_OK) {
        goto error;
    }

    for (i = 1; i < objc; i++) {
        char *arg = Tcl_GetString(objv[i]);
        if ((arg[0] == '-') && (arg[1] >= 'a') && (arg[1] <= 'z')) {
            break;
        }
    }
    if (ProcessPtextCoords(interp, canvas, itemPtr, i, objv) != TCL_OK) {
        goto error;
    }
    if (ConfigurePtext(interp, canvas, itemPtr, objc-i, objv+i, 0) == TCL_OK) {
        return TCL_OK;
    }

error:
    /*
     * NB: We must unlink the item here since the TkPathCanvasItemExConfigure()
     *     link it to the root by default.
     */
    TkPathCanvasItemDetach(itemPtr);
    DeletePtext(canvas, itemPtr, Tk_Display(Tk_PathCanvasTkwin(canvas)));
    return TCL_ERROR;
}

static int
ProcessPtextCoords(Tcl_Interp *interp, Tk_PathCanvas canvas,
    Tk_PathItem *itemPtr,
    int objc, Tcl_Obj *const objv[])
{
    PtextItem *ptextPtr = (PtextItem *) itemPtr;

    if (objc == 0) {
        Tcl_Obj *obj = Tcl_NewObj();
        Tcl_Obj *subobj = Tcl_NewDoubleObj(ptextPtr->x);
        Tcl_ListObjAppendElement(interp, obj, subobj);
        subobj = Tcl_NewDoubleObj(ptextPtr->y);
        Tcl_ListObjAppendElement(interp, obj, subobj);
        Tcl_SetObjResult(interp, obj);
    } else if (objc < 3) {
        if (objc == 1) {
            if (Tcl_ListObjGetElements(interp, objv[0], &objc,
                    (Tcl_Obj ***) &objv) != TCL_OK) {
                return TCL_ERROR;
            } else if (objc != 2) {
                Tcl_SetObjResult(interp,
            Tcl_NewStringObj("wrong # coordinates: expected 0 or 2",
                     -1));
                return TCL_ERROR;
            }
        }
        if ((Tk_PathCanvasGetCoordFromObj(interp, canvas, objv[0],
                      &ptextPtr->x) != TCL_OK) ||
        (Tk_PathCanvasGetCoordFromObj(interp, canvas, objv[1],
                      &ptextPtr->y) != TCL_OK)) {
            return TCL_ERROR;
        }
    } else {
        Tcl_SetObjResult(interp,
        Tcl_NewStringObj("wrong # coordinates: expected 0 or 2", -1));
        return TCL_ERROR;
    }
    return TCL_OK;
}

static int
PtextCoords(Tcl_Interp *interp, Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
    int objc, Tcl_Obj *const objv[])
{
    PtextItem *ptextPtr = (PtextItem *) itemPtr;
    int result;

    result = ProcessPtextCoords(interp, canvas, itemPtr, objc, objv);
    if ((result == TCL_OK) && (objc > 0) && (objc < 3)) {
    ComputePtextBbox(canvas, ptextPtr);
    }
    return result;
}

void
ComputePtextBbox(Tk_PathCanvas canvas, PtextItem *ptextPtr)
{
    Tk_PathItemEx *itemExPtr = &ptextPtr->headerEx;
    Tk_PathItem *itemPtr = &itemExPtr->header;
    Tk_PathStyle style;
    Tk_PathState state = itemExPtr->header.state;
    Tk_Window tkwin = Tk_PathCanvasTkwin(canvas);
    double width;
    double height;
    double bheight;
    double lineSpacing;
    TkPathRect bbox, r;

    if (state == TK_PATHSTATE_NULL) {
    state = TkPathCanvasState(canvas);
    }
    if ((ptextPtr->utf8Obj == NULL) || (state == TK_PATHSTATE_HIDDEN)) {
        itemExPtr->header.x1 = itemExPtr->header.x2 =
        itemExPtr->header.y1 = itemExPtr->header.y2 = -1;
        return;
    }
    style = TkPathCanvasInheritStyle(itemPtr, TK_PATH_MERGESTYLE_NOTFILL);
    r = TkPathTextMeasureBbox(Tk_Display(tkwin), &ptextPtr->textStyle,
        Tcl_GetString(ptextPtr->utf8Obj), &lineSpacing,
        ptextPtr->custom);
    width = r.x2 - r.x1;
    height = r.y2 - r.y1;
    bheight = -r.y1;

    switch (ptextPtr->textAnchor) {
        case TK_PATH_TEXTANCHOR_Start:
        case TK_PATH_TEXTANCHOR_W:
        case TK_PATH_TEXTANCHOR_NW:
        case TK_PATH_TEXTANCHOR_SW:
            bbox.x1 = ptextPtr->x;
            bbox.x2 = bbox.x1 + width;
            break;
        case TK_PATH_TEXTANCHOR_Middle:
        case TK_PATH_TEXTANCHOR_N:
        case TK_PATH_TEXTANCHOR_S:
        case TK_PATH_TEXTANCHOR_C:
            bbox.x1 = ptextPtr->x - width/2;
            bbox.x2 = ptextPtr->x + width/2;
            break;
        case TK_PATH_TEXTANCHOR_End:
        case TK_PATH_TEXTANCHOR_E:
        case TK_PATH_TEXTANCHOR_NE:
        case TK_PATH_TEXTANCHOR_SE:
            bbox.x1 = ptextPtr->x - width;
            bbox.x2 = ptextPtr->x;
            break;
        default:
            break;
    }

    switch (ptextPtr->textAnchor) {
        case TK_PATH_TEXTANCHOR_Start:
        case TK_PATH_TEXTANCHOR_Middle:
        case TK_PATH_TEXTANCHOR_End:
            bbox.y1 = ptextPtr->y + r.y1;   /* r.y1 is negative! */
            bbox.y2 = ptextPtr->y + r.y2;
            break;
        case TK_PATH_TEXTANCHOR_N:
        case TK_PATH_TEXTANCHOR_NW:
        case TK_PATH_TEXTANCHOR_NE:
            bbox.y1 = ptextPtr->y;
            bbox.y2 = ptextPtr->y + height;
            break;
        case TK_PATH_TEXTANCHOR_W:
        case TK_PATH_TEXTANCHOR_E:
        case TK_PATH_TEXTANCHOR_C:
            bbox.y1 = ptextPtr->y - height/2;
            bbox.y2 = ptextPtr->y + height/2;
            break;
        case TK_PATH_TEXTANCHOR_S:
        case TK_PATH_TEXTANCHOR_SW:
        case TK_PATH_TEXTANCHOR_SE:
            bbox.y1 = ptextPtr->y - height;
            bbox.y2 = ptextPtr->y;
            break;
        default:
            break;
    }

    /* Fudge for antialiasing etc. */
    bbox.x1 -= 1.0;
    bbox.y1 -= 1.0;
    bbox.x2 += 1.0;
    bbox.y2 += 1.0;
    height += 2.0;
    bheight += 1.0;
    if (style.strokeColor) {
        double halfWidth = style.strokeWidth/2;
        bbox.x1 -= halfWidth;
        bbox.y1 -= halfWidth;
        bbox.x2 += halfWidth;
        bbox.x2 += halfWidth;
        height += style.strokeWidth;
        bheight += halfWidth;
    }
    itemPtr->bbox = bbox;
    ptextPtr->baseHeightRatio = bheight / height;
    ptextPtr->lineSpacing = lineSpacing;
    itemPtr->totalBbox = itemPtr->bbox;    /* FIXME */
    TkPathSetGenericPathHeaderBbox(&itemExPtr->header, style.matrixPtr, &bbox);
    TkPathCanvasFreeInheritedStyle(&style);
}

static int
ConfigurePtext(Tcl_Interp *interp, Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
    int objc, Tcl_Obj *const objv[], int flags)
{
    PtextItem *ptextPtr = (PtextItem *) itemPtr;
    Tk_PathItemEx *itemExPtr = &ptextPtr->headerEx;
    Tk_PathStyle *stylePtr = &itemExPtr->style;
    Tk_Window tkwin;
    Tk_SavedOptions savedOptions;
    Tcl_Obj *errorResult = NULL;
    int error, mask;

    tkwin = Tk_PathCanvasTkwin(canvas);
    for (error = 0; error <= 1; error++) {
    if (!error) {
        if (Tk_SetOptions(interp, (char *) ptextPtr, itemPtr->optionTable,
            objc, objv, tkwin, &savedOptions, &mask) != TCL_OK) {
        continue;
        }
    } else {
        errorResult = Tcl_GetObjResult(interp);
        Tcl_IncrRefCount(errorResult);
        Tk_RestoreSavedOptions(&savedOptions);
    }

    /*
     * Since we have -fill default equal to black we need to force
     * setting the fill member of the style.
     */
    if (TkPathCanvasItemExConfigure(interp, canvas, itemExPtr,
                    mask | TK_PATH_STYLE_OPTION_FILL)
        != TCL_OK) {
        continue;
    }
    /* @@@ TkPathTextConfig needs to be reworked! */
    if (ptextPtr->utf8Obj != NULL) {
        void *custom = NULL;

        ptextPtr->textStyle.fontSize = fabs(ptextPtr->textStyle.fontSize);
        if (TkPathTextConfig(interp, &ptextPtr->textStyle,
            Tcl_GetString(ptextPtr->utf8Obj), &custom) != TCL_OK) {
        continue;
        }
        if (ptextPtr->custom != NULL) {
        TkPathTextFree(&ptextPtr->textStyle, ptextPtr->custom);
        }
        ptextPtr->custom = custom;
    }

    /*
     * If we reach this on the first pass we are OK and continue below.
     */
    break;
    }
    if (!error) {
    Tk_FreeSavedOptions(&savedOptions);
    stylePtr->mask |= mask;
    }

    stylePtr->strokeOpacity = MAX(0.0, MIN(1.0, stylePtr->strokeOpacity));
    if (ptextPtr->utf8Obj != NULL) {
        ptextPtr->numBytes = Tcl_GetCharLength(ptextPtr->utf8Obj);
        ptextPtr->numChars = Tcl_NumUtfChars(Tcl_GetString(ptextPtr->utf8Obj),
        ptextPtr->numBytes);
    } else {
        ptextPtr->numBytes = 0;
        ptextPtr->numChars = 0;
    }
    if (error) {
    Tcl_SetObjResult(interp, errorResult);
    Tcl_DecrRefCount(errorResult);
    return TCL_ERROR;
    } else {
    ComputePtextBbox(canvas, ptextPtr);
    return TCL_OK;
    }
}

static void
DeletePtext(Tk_PathCanvas canvas, Tk_PathItem *itemPtr, Display *display)
{
    PtextItem *ptextPtr = (PtextItem *) itemPtr;
    Tk_PathItemEx *itemExPtr = &ptextPtr->headerEx;
    Tk_PathStyle *stylePtr = &itemExPtr->style;

    if (stylePtr->fill != NULL) {
    TkPathFreePathColor(stylePtr->fill);
    }
    if (itemExPtr->styleInst != NULL) {
    TkPathFreeStyle(itemExPtr->styleInst);
    }
    TkPathTextFree(&ptextPtr->textStyle, ptextPtr->custom);
    ptextPtr->custom = NULL;
    Tk_FreeConfigOptions((char *) ptextPtr, itemPtr->optionTable,
        Tk_PathCanvasTkwin(canvas));
}

static void
DisplayPtext(Tk_PathCanvas canvas, Tk_PathItem *itemPtr, Display *display,
    Drawable drawable, int x, int y, int width, int height)
{
    PtextItem *ptextPtr = (PtextItem *) itemPtr;
    Tk_PathItemEx *itemExPtr = &ptextPtr->headerEx;
    Tk_PathStyle style;
    TkPathMatrix m = TkPathGetCanvasTMatrix(canvas);
    TkPathContext ctx;

    if (ptextPtr->utf8Obj == NULL) {
        return;
    }

    /*
     * The defaults for -fill and -stroke differ for the ptext item.
     */
    style = TkPathCanvasInheritStyle(itemPtr, 0);
    if (!(style.mask & TK_PATH_STYLE_OPTION_FILL)) {
    style.fill = itemExPtr->style.fill;
    }
    if (!(style.mask & TK_PATH_STYLE_OPTION_STROKE)) {
    style.strokeColor = itemExPtr->style.strokeColor;
    }

    ctx = ContextOfCanvas(canvas);
    TkPathPushTMatrix(ctx, &m);
    if (style.matrixPtr != NULL) {
        TkPathPushTMatrix(ctx, style.matrixPtr);
    }
    TkPathBeginPath(ctx, &style);
    /* @@@ We need to handle gradients as well here!
     *     Wait to see what the other APIs have to say.
     */
    TkPathTextDraw(ctx, &style, &ptextPtr->textStyle,
           itemPtr->bbox.x1,
           itemPtr->bbox.y1 + ptextPtr->baseHeightRatio *
           (itemPtr->bbox.y2 - itemPtr->bbox.y1),
           ptextPtr->fillOverStroke,
           Tcl_GetString(ptextPtr->utf8Obj), ptextPtr->custom);
    TkPathEndPath(ctx);
    TkPathCanvasFreeInheritedStyle(&style);
}

static void
PtextBbox(Tk_PathCanvas canvas, Tk_PathItem *itemPtr, int mask)
{
    PtextItem *ptextPtr = (PtextItem *) itemPtr;
    ComputePtextBbox(canvas, ptextPtr);
}

static double
PtextToPoint(Tk_PathCanvas canvas, Tk_PathItem *itemPtr, double *pointPtr)
{
    Tk_PathStyle style;
    double dist;

    style = TkPathCanvasInheritStyle(itemPtr,
        TK_PATH_MERGESTYLE_NOTFILL | TK_PATH_MERGESTYLE_NOTSTROKE);
    dist = TkPathRectToPointWithMatrix(itemPtr->bbox, style.matrixPtr, pointPtr);
    TkPathCanvasFreeInheritedStyle(&style);
    return dist;
}

static int
PtextToArea(Tk_PathCanvas canvas, Tk_PathItem *itemPtr, double *areaPtr)
{
    Tk_PathStyle style;
    int area;

    style = TkPathCanvasInheritStyle(itemPtr,
        TK_PATH_MERGESTYLE_NOTFILL | TK_PATH_MERGESTYLE_NOTSTROKE);
    area = TkPathRectToAreaWithMatrix(itemPtr->bbox, style.matrixPtr, areaPtr);
    TkPathCanvasFreeInheritedStyle(&style);
    return area;
}

/* From tkUnixCairoPath.c */
static char *
linebreak(char *str, char **nextp)
{
    char *ret;

    if (str == NULL) {
        str = *nextp;
    }
    str += strspn(str, "\r\n");
    if (*str == '\0') {
        return NULL;
    }
    ret = str;
    str += strcspn(str, "\r\n");
    if (*str) {
        int ch = *str;

        *str++ = '\0';
        if ((ch == '\r') && (*str == '\n')) {
            str++;
    }
    }
    *nextp = str;
    return ret;
}

static int
drawptext(Tcl_Interp *interp, PtextItem *ptextPtr, Tcl_Obj *ret, Tcl_Obj *cmdl)
{
    char *utf8, *token, *savep;
    Tcl_DString ds;
    int i, result = TCL_OK;

    savep = Tcl_GetStringFromObj(ptextPtr->utf8Obj, &i);
    utf8 = (char *) ckalloc((unsigned) i + 1);
    strcpy(utf8, savep);
    Tcl_DStringInit(&ds);
    for (token = linebreak(utf8, &savep); token != NULL;
     token = linebreak(NULL, &savep)) {
    if (cmdl != NULL) {
        Tcl_Obj *cmd;

        /*
         * Use provided callback for formatting/encoding text.
         */
        cmd = Tcl_DuplicateObj(cmdl);
        Tcl_IncrRefCount(cmd);
        if (Tcl_ListObjAppendElement(interp, cmd,
            Tcl_NewStringObj(token, strlen(token))) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        result = TCL_ERROR;
        break;
        }
        if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_DIRECT) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        result = TCL_ERROR;
        break;
        }
        Tcl_DecrRefCount(cmd);
        Tcl_AppendToObj(ret, "(", 1);
        Tcl_AppendObjToObj(ret, Tcl_GetObjResult(interp));
        Tcl_AppendToObj(ret, ") Tj\nT*\n", 8);
    } else {
        Tcl_DStringSetLength(&ds, 0);
        Tcl_DStringAppend(&ds, "(", 1);
        i = 0;
        while (token[i] != '\0') {
        switch (token[i]) {
        case '(':
        case ')':
        case '\\':
            Tcl_DStringAppend(&ds, "\\", 1);
        default:
            if ((token[i] < 0) || (token[i] >= ' ')) {
            Tcl_DStringAppend(&ds, token + i, 1);
            } else {
            char obuf[8];

            sprintf(obuf, "\\%03o", token[i]);
            Tcl_DStringAppend(&ds, obuf, -1);
            }
            break;
        case '\n':
            Tcl_DStringAppend(&ds, "\\n", 2);
            break;
        case '\r':
            Tcl_DStringAppend(&ds, "\\r", 2);
            break;
        case '\t':
            Tcl_DStringAppend(&ds, "\\t", 2);
            break;
        case '\b':
            Tcl_DStringAppend(&ds, "\\b", 2);
            break;
        case '\f':
            Tcl_DStringAppend(&ds, "\\f", 2);
            break;
        }
        i++;
        }
        Tcl_DStringAppend(&ds, ") Tj\nT*\n", -1);
        Tcl_AppendToObj(ret, Tcl_DStringValue(&ds), Tcl_DStringLength(&ds));
    }
    }
    Tcl_DStringFree(&ds);
    ckfree(utf8);
    return result;
}

static int
PtextToPdf(Tcl_Interp *interp, Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
    int objc, Tcl_Obj *const objv[], int prepass)
{
    Tk_PathStyle style;
    PtextItem *ptextPtr = (PtextItem *) itemPtr;
    Tcl_Obj *ret, *cmdl;
    TkPathMatrix matrix = { 1., 0., 0., 1., 0., 0. };
    TkPathMatrix tmp = {1., 0., 0., -1., 0., 0. };
    Tk_PathState state = itemPtr->state;
    TkPathRect bbox = itemPtr->bbox;
    int hasStroke;
    char *font;
    int result = TCL_OK;

    if (state == TK_PATHSTATE_NULL) {
    state = TkPathCanvasState(canvas);
    }
    if ((ptextPtr->utf8Obj == NULL) || (state == TK_PATHSTATE_HIDDEN)) {
    return result;
    }
    ret = Tcl_NewObj();
    style = TkPathCanvasInheritStyle(itemPtr, TK_PATH_MERGESTYLE_NOTFILL);
    if (style.matrixPtr != NULL) {
    matrix = *(style.matrixPtr);
    }
    tmp.tx = bbox.x1; /* value with anchoring applied */
    tmp.ty = ptextPtr->y; /* TODO */
    tmp.ty = bbox.y1 + ptextPtr->baseHeightRatio * (bbox.y2 - bbox.y1);
    TkPathMMulTMatrix(&tmp, &matrix);
    /*
     * The defaults for -fill and -stroke differ for the ptext item.
     */
    if (!(style.mask & TK_PATH_STYLE_OPTION_FILL)) {
    style.fill = ptextPtr->headerEx.style.fill;
    } else if (GetGradientMasterFromPathColor(style.fill) != NULL) {
    style.fill = NULL;
    }
    if (!(style.mask & TK_PATH_STYLE_OPTION_STROKE)) {
    style.strokeColor = ptextPtr->headerEx.style.strokeColor;
    }
    if (objc > 0) {
    int retc;
    Tcl_Obj *gs, *cmd, **retv;

    gs = TkPathExtGS(&style, NULL);
    if (gs != NULL) {
        cmd = Tcl_DuplicateObj(objv[0]);
        Tcl_IncrRefCount(cmd);
        if (Tcl_ListObjAppendElement(interp, cmd, gs) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        Tcl_DecrRefCount(gs);
        result = TCL_ERROR;
        goto done;
        }
        if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_DIRECT) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        result = TCL_ERROR;
        goto done;
        }
        Tcl_DecrRefCount(cmd);
        /*
         * Get name of extended graphics state.
         */
        if (Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp),
                       &retc, &retv) != TCL_OK) {
        result = TCL_ERROR;
        goto done;
        }
        if (retc < 2) {
        Tcl_SetResult(interp, (char *) "missing PDF id/name",
                  TCL_STATIC);
        result = TCL_ERROR;
        goto done;
        }
        Tcl_AppendPrintfToObj(ret, "/%s gs\n", Tcl_GetString(retv[1]));
    }
    }
    hasStroke = (style.strokeColor != NULL);
    cmdl = (objc > 1) ? objv[1] : NULL;
    font = (objc > 2) ? Tcl_GetString(objv[2]) : ptextPtr->textStyle.fontFamily;
    /*
     * TODO: ptextPtr->textStyle.fontSlant ptextPtr->textStyle.fontWeight
     */
    Tcl_AppendPrintfToObj(ret, "q\nBT\n/%s ", font);
    TkPathPdfNumber(ret, 3, ptextPtr->textStyle.fontSize, " Tf\n");
    TkPathPdfNumber(ret, 3, ptextPtr->lineSpacing, " TL\n");
    TkPathPdfNumber(ret, 6, matrix.a, " ");
    TkPathPdfNumber(ret, 6, matrix.b, " ");
    TkPathPdfNumber(ret, 6, matrix.c, " ");
    TkPathPdfNumber(ret, 6, matrix.d, " ");
    TkPathPdfNumber(ret, 3, matrix.tx, " ");
    TkPathPdfNumber(ret, 3, matrix.ty, " Tm\n");
    if (ptextPtr->fillOverStroke && hasStroke &&
    (style.fill != NULL) && (style.fill->color != NULL)) {
    /* first pass w/o fill */
    TkPathPdfColor(ret, style.strokeColor, "RG");
    Tcl_AppendToObj(ret, "1 Tr\n", 5);
    TkPathPdfNumber(ret, 3, style.strokeWidth, " w\n");
    result = drawptext(interp, ptextPtr, ret, cmdl);
    if (result == TCL_OK) {
        /* second pass w/o stroke */
        Tcl_AppendPrintfToObj(ret, "ET\nQ\nq\nBT\n/%s ", font);
        TkPathPdfNumber(ret, 3, ptextPtr->textStyle.fontSize, " Tf\n");
        TkPathPdfNumber(ret, 3, ptextPtr->textStyle.fontSize, " TL\n");
        TkPathPdfNumber(ret, 6, matrix.a, " ");
        TkPathPdfNumber(ret, 6, matrix.b, " ");
        TkPathPdfNumber(ret, 6, matrix.c, " ");
        TkPathPdfNumber(ret, 6, matrix.d, " ");
        TkPathPdfNumber(ret, 3, matrix.tx, " ");
        TkPathPdfNumber(ret, 3, matrix.ty, " Tm\n");
        TkPathPdfColor(ret, style.fill->color, "rg");
        Tcl_AppendToObj(ret, "0 Tr\n", 5);
        result = drawptext(interp, ptextPtr, ret, cmdl);
    }
    } else if ((style.fill != NULL) && (style.fill->color != NULL)) {
    TkPathPdfColor(ret, style.fill->color, "rg");
    if (hasStroke) {
        TkPathPdfColor(ret, style.strokeColor, "RG");
        Tcl_AppendToObj(ret, "2 Tr\n", 5);
        TkPathPdfNumber(ret, 3, style.strokeWidth, " w\n");
    } else {
        Tcl_AppendToObj(ret, "0 Tr\n", 5);
    }
    result = drawptext(interp, ptextPtr, ret, cmdl);
    } else if (hasStroke) {
    TkPathPdfColor(ret, style.strokeColor, "RG");
    Tcl_AppendToObj(ret, "1 Tr\n", 5);
    TkPathPdfNumber(ret, 3, style.strokeWidth, " w\n");
    result = drawptext(interp, ptextPtr, ret, cmdl);
    }
done:
    TkPathCanvasFreeInheritedStyle(&style);
    if (result == TCL_OK) {
    Tcl_AppendToObj(ret, "ET\nQ\n", 5);
    Tcl_SetObjResult(interp, ret);
    } else {
    Tcl_DecrRefCount(ret);
    }
    return result;
}

static void
ScalePtext(Tk_PathCanvas canvas, Tk_PathItem *itemPtr, int compensate,
    double originX, double originY, double scaleX, double scaleY)
{
    PtextItem *ptextPtr = (PtextItem *) itemPtr;

    TkPathCompensateScale(itemPtr, compensate, &originX, &originY, &scaleX, &scaleY);

    TkPathScalePathRect(&itemPtr->bbox, originX, originY, scaleX, scaleY);
    ptextPtr->x = originX + scaleX*(ptextPtr->x - originX);
    ptextPtr->y = originY + scaleY*(ptextPtr->y - originY);
    TkPathScalePathRect(&itemPtr->bbox, originX, originY, scaleX, scaleY);
    TkPathScaleItemHeader(itemPtr, originX, originY, scaleX, scaleY);
}

static void
TranslatePtext(Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
    int compensate, double deltaX, double deltaY)
{
    PtextItem *ptextPtr = (PtextItem *) itemPtr;

    TkPathCompensateTranslate(itemPtr, compensate, &deltaX, &deltaY);

    ptextPtr->x += deltaX;
    ptextPtr->y += deltaY;
    TkPathTranslatePathRect(&itemPtr->bbox, deltaX, deltaY);
    TkPathTranslateItemHeader(itemPtr, deltaX, deltaY);
}

#if 0    /* TODO */
static void
PtextDeleteChars(Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
    int first, int last)
{
    PtextItem *ptextPtr = (PtextItem *) itemPtr;
    int byteIndex, byteCount, charsRemoved;
    char *new, *text;

    text = ptextPtr->utf8;
    if (first < 0) {
        first = 0;
    }
    if (last >= ptextPtr->numChars) {
        last = ptextPtr->numChars - 1;
    }
    if (first > last) {
        return;
    }
    charsRemoved = last + 1 - first;

    byteIndex = Tcl_UtfAtIndex(text, first) - text;
    byteCount = Tcl_UtfAtIndex(text + byteIndex, charsRemoved)
    - (text + byteIndex);

    new = (char *) ckalloc((unsigned) (ptextPtr->numBytes + 1 - byteCount));
    memcpy(new, text, (size_t) byteIndex);
    strcpy(new + byteIndex, text + byteIndex + byteCount);

    ckfree(text);
    ptextPtr->utf8 = new;
    ptextPtr->numChars -= charsRemoved;
    ptextPtr->numBytes -= byteCount;

    /*
     * TkPathTextConfig(interp, &ptextPtr->textStyle,
     *                  ptextPtr->utf8, &ptextPtr->custom);
     */
    ComputePtextBbox(canvas, ptextPtr);
    return;
}
#endif
/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
