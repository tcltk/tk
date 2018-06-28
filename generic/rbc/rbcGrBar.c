/*
 * rbcGrBar.c --
 *
 *      This module implements barchart elements for the rbc graph widget.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

/*
 * -------------------------------------------------------------------
 *
 * FreqKey --
 *
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    double          value;      /* Duplicated abscissa */
    RbcAxis2D       axes;       /* Axis mapping of element */
} FreqKey;

typedef struct {
    char           *name;       /* Pen style identifier.  If NULL pen
                                 * was statically allocated. */
    RbcUid          classUid;   /* Type of pen */
    char           *typeId;     /* String token identifying the type of pen */
    unsigned int    flags;      /* Indicates if the pen element is active or
                                 * normal */
    int             refCount;   /* Reference count for elements using
                                 * this pen. */
    Tcl_HashEntry  *hashPtr;
    Tk_ConfigSpec  *specsPtr;   /* Configuration specifications */

    PenConfigureProc *configProc;
    PenDestroyProc *destroyProc;

    XColor         *fgColor;    /* Foreground color of bar */
    Tk_3DBorder     border;     /* 3D border and background color */
    int             borderWidth;        /* 3D border width of bar */
    int             relief;     /* Relief of the bar */
    Pixmap          stipple;    /* Stipple */
    GC              gc;         /* Graphics context */

    /* Error bar attributes. */
    int             errorBarShow;       /* Describes which error bars to
                                         * display: none, x, y, or * both. */

    int             errorBarLineWidth;  /* Width of the error bar segments. */

    int             errorBarCapWidth;
    XColor         *errorBarColor;      /* Color of the error bar. */

    GC              errorBarGC; /* Error bar graphics context. */

    /* Show value attributes. */
    int             valueShow;  /* Indicates whether to display data value.
                                 * Values are x, y, or none. */

    char           *valueFormat;        /* A printf format string. */
    RbcTextStyle    valueStyle; /* Text attributes (color, font,
                                 * rotation, etc.) of the value. */

} BarPen;

typedef struct {
    RbcElemWeight   weight;     /* Weight range where this pen is valid. */

    BarPen         *penPtr;     /* Pen to draw */

    RbcSegment2D   *xErrorBars; /* Point to start of this pen's X-error bar
                                 * segments in the element's array. */

    RbcSegment2D   *yErrorBars; /* Point to start of this pen's Y-error bar
                                 * segments in the element's array. */
    int             xErrorBarCnt;       /* # of error bars for this pen. */

    int             yErrorBarCnt;       /* # of error bars for this pen. */

    int             errorBarCapWidth;   /* Length of the cap ends on each
                                         * error bar. */

    int             symbolSize; /* Size of the pen's symbol scaled to the
                                 * current graph size. */

    /* Bar chart specific data. */
    XRectangle     *rectangles; /* Indicates starting location in bar
                                 * array for this pen. */
    int             nRects;     /* Number of bar segments for this pen. */

} BarPenStyle;

typedef struct {
    char           *name;       /* Identifier to refer the
                                 * element. Used in the "insert",
                                 * "delete", or "show", commands. */

    RbcUid          classUid;   /* Type of element; either
                                 * rbcBarElementUid, rbcLineElementUid, or
                                 * rbcStripElementUid. */

    RbcGraph       *graphPtr;   /* Graph widget of element */
    unsigned int    flags;      /* Indicates if the entire element is
                                 * active, or if coordinates need to
                                 * be calculated */
    char          **tags;
    int             hidden;     /* If non-zero, don't display the element. */

    Tcl_HashEntry  *hashPtr;
    char           *label;      /* Label displayed in legend */
    int             labelRelief;        /* Relief of label in legend. */

    RbcAxis2D       axes;
    RbcElemVector   x, y, w;    /* Contains array of numeric values */

    RbcElemVector   xError;     /* Relative/symmetric X error values. */
    RbcElemVector   yError;     /* Relative/symmetric Y error values. */
    RbcElemVector   xHigh, xLow;        /* Absolute/asymmetric X-coordinate high/low
                                           error values. */
    RbcElemVector   yHigh, yLow;        /* Absolute/asymmetric Y-coordinate high/low
                                           error values. */

    int            *activeIndices;      /* Array of indices (malloc-ed) that
                                         * indicate the data points have been
                                         * selected as active (drawn with
                                         * "active" colors). */

    int             nActiveIndices;     /* Number of active data points. Special
                                         * case: if nActiveIndices < 0 and the
                                         * active bit is set in "flags", then all
                                         * data points are drawn active. */

    RbcElementProcs *procsPtr;  /* Class information for bar elements */
    Tk_ConfigSpec  *specsPtr;   /* Configuration specifications */

    RbcSegment2D   *xErrorBars; /* Point to start of this pen's X-error bar
                                 * segments in the element's array. */
    RbcSegment2D   *yErrorBars; /* Point to start of this pen's Y-error bar
                                 * segments in the element's array. */
    int             xErrorBarCnt;       /* # of error bars for this pen. */
    int             yErrorBarCnt;       /* # of error bars for this pen. */

    int            *xErrorToData;       /* Maps individual error bar segments back
                                         * to the data point associated with it. */
    int            *yErrorToData;       /* Maps individual error bar segments back
                                         * to the data point associated with it. */

    int             errorBarCapWidth;   /* Length of cap on error bars */

    BarPen         *activePenPtr;       /* Standard Pens */
    BarPen         *normalPenPtr;

    RbcChain       *palette;    /* Chain of pen style information. */

    /* Symbol scaling */
    int             scaleSymbols;       /* If non-zero, the symbols will scale
                                         * in size as the graph is zoomed
                                         * in/out.  */

    double          xRange, yRange;     /* Initial X-axis and Y-axis ranges:
                                         * used to scale the size of element's
                                         * symbol. */
    int             state;
    /*
     * Bar specific attributes
     */
    BarPen          builtinPen;

    int            *rectToData;
    XRectangle     *rectangles; /* Array of rectangles comprising the bar
                                 * segments of the element. */
    int             nRects;     /* # of visible bar segments for element */

    int             padX;       /* Spacing on either side of bar */
    double          barWidth;
    int             nActive;

    XRectangle     *activeRects;
    int            *activeToData;
} Bar;

extern Tk_CustomOption rbcBarPenOption;
extern Tk_CustomOption rbcDataOption;
extern Tk_CustomOption rbcDataPairsOption;
extern Tk_CustomOption rbcDistanceOption;
extern Tk_CustomOption rbcListOption;
extern Tk_CustomOption rbcXAxisOption;
extern Tk_CustomOption rbcYAxisOption;
extern Tk_CustomOption rbcShadowOption;
extern Tk_CustomOption rbcFillOption;
extern Tk_CustomOption rbcColorOption;
extern Tk_CustomOption rbcStateOption;

static Tk_OptionParseProc StringToBarMode;
static Tk_OptionPrintProc BarModeToString;

static Tk_CustomOption stylesOption = {
    RbcStringToStyles, RbcStylesToString, (ClientData) sizeof(BarPenStyle)
};

Tk_CustomOption rbcBarModeOption = {
    StringToBarMode, BarModeToString, (ClientData) 0
};

#define DEF_BAR_ACTIVE_PEN          "activeBar"
#define DEF_BAR_AXIS_X              "x"
#define DEF_BAR_AXIS_Y              "y"
#define DEF_BAR_BACKGROUND          "navyblue"
#define DEF_BAR_BG_MONO             "black"
#define DEF_BAR_BORDERWIDTH         "2"
#define DEF_BAR_DATA                (char *)NULL
#define DEF_BAR_ERRORBAR_COLOR      "defcolor"
#define DEF_BAR_ERRORBAR_LINE_WIDTH "1"
#define DEF_BAR_ERRORBAR_CAP_WIDTH  "1"
#define DEF_BAR_FOREGROUND          "blue"
#define DEF_BAR_FG_MONO             "white"
#define DEF_BAR_HIDE                "no"
#define DEF_BAR_LABEL               (char *)NULL
#define DEF_BAR_LABEL_RELIEF        "flat"
#define DEF_BAR_NORMAL_STIPPLE      ""
#define DEF_BAR_RELIEF              "raised"
#define DEF_BAR_SHOW_ERRORBARS      "both"
#define DEF_BAR_STATE               "normal"
#define DEF_BAR_STYLES              ""
#define DEF_BAR_TAGS                "all"
#define DEF_BAR_WIDTH               "0.0"
#define DEF_BAR_DATA                (char *)NULL

#define DEF_PEN_ACTIVE_BACKGROUND   "red"
#define DEF_PEN_ACTIVE_BG_MONO      "white"
#define DEF_PEN_ACTIVE_FOREGROUND   "pink"
#define DEF_PEN_ACTIVE_FG_MONO      "black"
#define DEF_PEN_BORDERWIDTH         "2"
#define DEF_PEN_NORMAL_BACKGROUND   "navyblue"
#define DEF_PEN_NORMAL_BG_MONO      "black"
#define DEF_PEN_NORMAL_FOREGROUND   "blue"
#define DEF_PEN_NORMAL_FG_MONO      "white"
#define DEF_PEN_RELIEF              "raised"
#define DEF_PEN_STIPPLE             ""
#define DEF_PEN_TYPE                "bar"
#define	DEF_PEN_VALUE_ANCHOR        "s"
#define	DEF_PEN_VALUE_COLOR         "black"
#define	DEF_PEN_VALUE_FONT          RBC_FONT_SMALL
#define	DEF_PEN_VALUE_FORMAT        "%g"
#define	DEF_PEN_VALUE_ROTATE        (char *)NULL
#define	DEF_PEN_VALUE_SHADOW        (char *)NULL
#define DEF_PEN_SHOW_VALUES         "no"

#define FreeElemVector(v) \
    if ((v).clientId != NULL) { \
	RbcFreeVectorId((v).clientId); \
    } else if ((v).valueArr != NULL) { \
	ckfree((char *)(v).valueArr); \
    }

static Tk_ConfigSpec barPenConfigSpecs[] = {
    {TK_CONFIG_BORDER, "-background", "background", "Background",
            DEF_PEN_ACTIVE_BACKGROUND, Tk_Offset(BarPen, border),
        TK_CONFIG_NULL_OK | TK_CONFIG_COLOR_ONLY | RBC_ACTIVE_PEN},
    {TK_CONFIG_BORDER, "-background", "background", "Background",
            DEF_PEN_ACTIVE_BACKGROUND, Tk_Offset(BarPen, border),
        TK_CONFIG_NULL_OK | TK_CONFIG_MONO_ONLY | RBC_ACTIVE_PEN},
    {TK_CONFIG_BORDER, "-background", "background", "Background",
            DEF_PEN_NORMAL_BACKGROUND, Tk_Offset(BarPen, border),
        TK_CONFIG_NULL_OK | TK_CONFIG_COLOR_ONLY | RBC_NORMAL_PEN},
    {TK_CONFIG_BORDER, "-background", "background", "Background",
            DEF_PEN_NORMAL_BACKGROUND, Tk_Offset(BarPen, border),
        TK_CONFIG_NULL_OK | TK_CONFIG_MONO_ONLY | RBC_NORMAL_PEN},
    {TK_CONFIG_SYNONYM, "-bd", "borderWidth", (char *) NULL, (char *) NULL, 0,
        RBC_ALL_PENS},
    {TK_CONFIG_SYNONYM, "-bg", "background", (char *) NULL, (char *) NULL, 0,
        RBC_ALL_PENS},
    {TK_CONFIG_CUSTOM, "-borderwidth", "borderWidth", "BorderWidth",
            DEF_PEN_BORDERWIDTH, Tk_Offset(BarPen, borderWidth), RBC_ALL_PENS,
        &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-errorbarcolor", "errorBarColor", "ErrorBarColor",
            DEF_BAR_ERRORBAR_COLOR, Tk_Offset(BarPen, errorBarColor),
        RBC_ALL_PENS, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-errorbarwidth", "errorBarWidth", "ErrorBarWidth",
            DEF_BAR_ERRORBAR_LINE_WIDTH, Tk_Offset(BarPen, errorBarLineWidth),
        RBC_ALL_PENS | TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-errorbarcap", "errorBarCap", "ErrorBarCap",
            DEF_BAR_ERRORBAR_CAP_WIDTH, Tk_Offset(BarPen, errorBarCapWidth),
        RBC_ALL_PENS | TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_SYNONYM, "-fg", "foreground", (char *) NULL, (char *) NULL, 0,
        RBC_ALL_PENS},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground",
            DEF_PEN_ACTIVE_FOREGROUND, Tk_Offset(BarPen, fgColor),
        RBC_ACTIVE_PEN | TK_CONFIG_NULL_OK | TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground",
            DEF_PEN_ACTIVE_FOREGROUND, Tk_Offset(BarPen, fgColor),
        RBC_ACTIVE_PEN | TK_CONFIG_NULL_OK | TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground",
            DEF_PEN_NORMAL_FOREGROUND, Tk_Offset(BarPen, fgColor),
        RBC_NORMAL_PEN | TK_CONFIG_NULL_OK | TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground",
            DEF_PEN_NORMAL_FOREGROUND, Tk_Offset(BarPen, fgColor),
        RBC_NORMAL_PEN | TK_CONFIG_NULL_OK | TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_RELIEF, "-relief", "relief", "Relief", DEF_PEN_RELIEF,
        Tk_Offset(BarPen, relief), RBC_ALL_PENS},
    {TK_CONFIG_CUSTOM, "-showerrorbars", "showErrorBars", "ShowErrorBars",
            DEF_BAR_SHOW_ERRORBARS, Tk_Offset(BarPen, errorBarShow),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcFillOption},
    {TK_CONFIG_CUSTOM, "-showvalues", "showValues", "ShowValues",
            DEF_PEN_SHOW_VALUES, Tk_Offset(BarPen, valueShow),
        RBC_ALL_PENS | TK_CONFIG_DONT_SET_DEFAULT, &rbcFillOption},
    {TK_CONFIG_BITMAP, "-stipple", "stipple", "Stipple", DEF_PEN_STIPPLE,
        Tk_Offset(BarPen, stipple), RBC_ALL_PENS | TK_CONFIG_NULL_OK},
    {TK_CONFIG_STRING, "-type", (char *) NULL, (char *) NULL, DEF_PEN_TYPE,
        Tk_Offset(BarPen, typeId), RBC_ALL_PENS | TK_CONFIG_NULL_OK},
    {TK_CONFIG_ANCHOR, "-valueanchor", "valueAnchor", "ValueAnchor",
            DEF_PEN_VALUE_ANCHOR, Tk_Offset(BarPen, valueStyle.anchor),
        RBC_ALL_PENS},
    {TK_CONFIG_COLOR, "-valuecolor", "valueColor", "ValueColor",
            DEF_PEN_VALUE_COLOR, Tk_Offset(BarPen, valueStyle.color),
        RBC_ALL_PENS},
    {TK_CONFIG_FONT, "-valuefont", "valueFont", "ValueFont", DEF_PEN_VALUE_FONT,
        Tk_Offset(BarPen, valueStyle.font), RBC_ALL_PENS},
    {TK_CONFIG_STRING, "-valueformat", "valueFormat", "ValueFormat",
            DEF_PEN_VALUE_FORMAT, Tk_Offset(BarPen, valueFormat),
        RBC_ALL_PENS | TK_CONFIG_NULL_OK},
    {TK_CONFIG_DOUBLE, "-valuerotate", "valueRotate", "ValueRotate",
            DEF_PEN_VALUE_ROTATE, Tk_Offset(BarPen, valueStyle.theta),
        RBC_ALL_PENS},
    {TK_CONFIG_CUSTOM, "-valueshadow", "valueShadow", "ValueShadow",
            DEF_PEN_VALUE_SHADOW, Tk_Offset(BarPen, valueStyle.shadow),
        RBC_ALL_PENS, &rbcShadowOption},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

static Tk_ConfigSpec barElemConfigSpecs[] = {
    {TK_CONFIG_CUSTOM, "-activepen", "activePen", "ActivePen",
            DEF_BAR_ACTIVE_PEN, Tk_Offset(Bar, activePenPtr), TK_CONFIG_NULL_OK,
        &rbcBarPenOption},
    {TK_CONFIG_BORDER, "-background", "background", "Background",
            DEF_BAR_BACKGROUND, Tk_Offset(Bar, builtinPen.border),
        TK_CONFIG_NULL_OK | TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_BORDER, "-background", "background", "Background",
            DEF_BAR_BACKGROUND, Tk_Offset(Bar, builtinPen.border),
        TK_CONFIG_NULL_OK | TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_DOUBLE, "-barwidth", "barWidth", "BarWidth", DEF_BAR_WIDTH,
        Tk_Offset(Bar, barWidth), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_SYNONYM, "-bd", "borderWidth", (char *) NULL, (char *) NULL, 0,
        0},
    {TK_CONFIG_SYNONYM, "-bg", "background", (char *) NULL, (char *) NULL, 0,
        0},
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags", DEF_BAR_TAGS,
        Tk_Offset(Bar, tags), TK_CONFIG_NULL_OK, &rbcListOption},
    {TK_CONFIG_CUSTOM, "-borderwidth", "borderWidth", "BorderWidth",
            DEF_BAR_BORDERWIDTH, Tk_Offset(Bar, builtinPen.borderWidth), 0,
        &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-errorbarcolor", "errorBarColor", "ErrorBarColor",
            DEF_BAR_ERRORBAR_COLOR, Tk_Offset(Bar, builtinPen.errorBarColor), 0,
        &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-errorbarwidth", "errorBarWidth", "ErrorBarWidth",
            DEF_BAR_ERRORBAR_LINE_WIDTH, Tk_Offset(Bar,
                builtinPen.errorBarLineWidth), TK_CONFIG_DONT_SET_DEFAULT,
        &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-errorbarcap", "errorBarCap", "ErrorBarCap",
            DEF_BAR_ERRORBAR_CAP_WIDTH, Tk_Offset(Bar,
                builtinPen.errorBarCapWidth),
        RBC_ALL_PENS | TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_SYNONYM, "-fg", "foreground", (char *) NULL, (char *) NULL, 0,
        0},
    {TK_CONFIG_CUSTOM, "-data", "data", "Data", (char *) NULL, 0, 0,
        &rbcDataPairsOption},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground",
            DEF_BAR_FOREGROUND, Tk_Offset(Bar, builtinPen.fgColor),
        TK_CONFIG_NULL_OK | TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground",
            DEF_BAR_FOREGROUND, Tk_Offset(Bar, builtinPen.fgColor),
        TK_CONFIG_NULL_OK | TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_STRING, "-label", "label", "Label", DEF_BAR_LABEL, Tk_Offset(Bar,
            label), TK_CONFIG_NULL_OK},
    {TK_CONFIG_RELIEF, "-labelrelief", "labelRelief", "LabelRelief",
            DEF_BAR_LABEL_RELIEF, Tk_Offset(Bar, labelRelief),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_BAR_HIDE, Tk_Offset(Bar,
            hidden), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX", DEF_BAR_AXIS_X, Tk_Offset(Bar,
            axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY", DEF_BAR_AXIS_Y, Tk_Offset(Bar,
            axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_CUSTOM, "-pen", "pen", "Pen", (char *) NULL, Tk_Offset(Bar,
            normalPenPtr), TK_CONFIG_NULL_OK, &rbcBarPenOption},
    {TK_CONFIG_RELIEF, "-relief", "relief", "Relief", DEF_BAR_RELIEF,
        Tk_Offset(Bar, builtinPen.relief), 0},
    {TK_CONFIG_CUSTOM, "-showerrorbars", "showErrorBars", "ShowErrorBars",
            DEF_BAR_SHOW_ERRORBARS, Tk_Offset(Bar, builtinPen.errorBarShow),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcFillOption},
    {TK_CONFIG_CUSTOM, "-showvalues", "showValues", "ShowValues",
            DEF_PEN_SHOW_VALUES, Tk_Offset(Bar, builtinPen.valueShow),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcFillOption},
    {TK_CONFIG_CUSTOM, "-state", "state", "State", DEF_BAR_STATE, Tk_Offset(Bar,
            state), TK_CONFIG_DONT_SET_DEFAULT, &rbcStateOption},
    {TK_CONFIG_BITMAP, "-stipple", "stipple", "Stipple", DEF_BAR_NORMAL_STIPPLE,
        Tk_Offset(Bar, builtinPen.stipple), TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-styles", "styles", "Styles", DEF_BAR_STYLES,
        Tk_Offset(Bar, palette), TK_CONFIG_NULL_OK, &stylesOption},
    {TK_CONFIG_ANCHOR, "-valueanchor", "valueAnchor", "ValueAnchor",
            DEF_PEN_VALUE_ANCHOR, Tk_Offset(Bar, builtinPen.valueStyle.anchor),
        0},
    {TK_CONFIG_COLOR, "-valuecolor", "valueColor", "ValueColor",
            DEF_PEN_VALUE_COLOR, Tk_Offset(Bar, builtinPen.valueStyle.color),
        0},
    {TK_CONFIG_FONT, "-valuefont", "valueFont", "ValueFont", DEF_PEN_VALUE_FONT,
        Tk_Offset(Bar, builtinPen.valueStyle.font), 0},
    {TK_CONFIG_STRING, "-valueformat", "valueFormat", "ValueFormat",
            DEF_PEN_VALUE_FORMAT, Tk_Offset(Bar, builtinPen.valueFormat),
        TK_CONFIG_NULL_OK},
    {TK_CONFIG_DOUBLE, "-valuerotate", "valueRotate", "ValueRotate",
            DEF_PEN_VALUE_ROTATE, Tk_Offset(Bar, builtinPen.valueStyle.theta),
        0},
    {TK_CONFIG_CUSTOM, "-valueshadow", "valueShadow", "ValueShadow",
            DEF_PEN_VALUE_SHADOW, Tk_Offset(Bar, builtinPen.valueStyle.shadow),
        0, &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-weights", "weights", "Weights", (char *) NULL,
        Tk_Offset(Bar, w), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-x", "xdata", "Xdata", DEF_BAR_DATA, Tk_Offset(Bar, x),
        0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-y", "ydata", "Ydata", DEF_BAR_DATA, Tk_Offset(Bar, y),
        0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-xdata", "xdata", "Xdata", DEF_BAR_DATA, Tk_Offset(Bar,
            x), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-ydata", "ydata", "Ydata", DEF_BAR_DATA, Tk_Offset(Bar,
            y), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-xerror", "xError", "XError", DEF_BAR_DATA,
        Tk_Offset(Bar, xError), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-xhigh", "xHigh", "XHigh", DEF_BAR_DATA, Tk_Offset(Bar,
            xHigh), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-xlow", "xLow", "XLow", DEF_BAR_DATA, Tk_Offset(Bar,
            xLow), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-yerror", "yError", "YError", DEF_BAR_DATA,
        Tk_Offset(Bar, yError), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-yhigh", "yHigh", "YHigh", DEF_BAR_DATA, Tk_Offset(Bar,
            yHigh), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-ylow", "yLow", "YLow", DEF_BAR_DATA, Tk_Offset(Bar,
            yLow), 0, &rbcDataOption},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

/* Forward declarations */
static PenConfigureProc ConfigurePen;
static PenDestroyProc DestroyPen;
static RbcElementClosestProc ClosestBar;
static RbcElementConfigProc ConfigureBar;
static RbcElementDestroyProc DestroyBar;
static RbcElementDrawProc DrawActiveBar;
static RbcElementDrawProc DrawNormalBar;
static RbcElementDrawSymbolProc DrawSymbol;
static RbcElementExtentsProc GetBarExtents;
static RbcElementToPostScriptProc ActiveBarToPostScript;
static RbcElementToPostScriptProc NormalBarToPostScript;
static RbcElementSymbolToPostScriptProc SymbolToPostScript;
static RbcElementMapProc MapBar;

static int      Round(
    register double x);
static const char *NameOfBarMode(
    RbcBarMode mode);
static void     ClearPalette(
    RbcChain * palette);
static void     InitPen(
    BarPen * penPtr);
static void     CheckStacks(
    RbcGraph * graphPtr,
    RbcAxis2D * pairPtr,
    double *minPtr,
    double *maxPtr);
static void     MergePens(
    Bar * barPtr,
    RbcPenStyle ** dataToStyle);
static void     MapActiveBars(
    Bar * barPtr);
static void     ResetBar(
    Bar * barPtr);
static void     DrawBarSegments(
    RbcGraph * graphPtr,
    Drawable drawable,
    BarPen * penPtr,
    XRectangle * rectangles,
    int nRects);
static void     DrawBarValues(
    RbcGraph * graphPtr,
    Drawable drawable,
    Bar * barPtr,
    BarPen * penPtr,
    XRectangle * rectangles,
    int nRects,
    int *rectToData);
static void     SegmentsToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken,
    BarPen * penPtr,
    register XRectangle * rectPtr,
    int nRects);
static void     BarValuesToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken,
    Bar * barPtr,
    BarPen * penPtr,
    XRectangle * rectangles,
    int nRects,
    int *rectToData);

/*
 *----------------------------------------------------------------------
 *
 * Round --
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
Round(
    register double x)
{
    return (int) (x + ((x < 0.0) ? -0.5 : 0.5));
}

/*
 * ----------------------------------------------------------------------
 * Custom option parse and print procedures
 * ----------------------------------------------------------------------
 */

/*
 * ----------------------------------------------------------------------
 *
 * NameOfBarMode --
 *
 *      Converts the integer representing the mode style into a string.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static const char *
NameOfBarMode(
    RbcBarMode mode)
{
    switch (mode) {
    case MODE_INFRONT:
        return "infront";
    case MODE_OVERLAP:
        return "overlap";
    case MODE_STACKED:
        return "stacked";
    case MODE_ALIGNED:
        return "aligned";
    default:
        return "unknown mode value";
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * StringToMode --
 *
 *      Converts the mode string into its numeric representation.
 *
 *      Valid mode strings are:
 *
 *          "infront"   Draw a full bar at each point in the element.
 *
 *      "stacked"   Stack bar segments vertically. Each stack is defined
 *              by each ordinate at a particular abscissa. The height
 *              of each segment is represented by the sum the previous
 *              ordinates.
 *
 *      "aligned"   Align bar segments as smaller slices one next to
 *              the other.  Like "stacks", aligned segments are
 *              defined by each ordinate at a particular abscissa.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
StringToBarMode(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Not used. */
    const char *string,         /* Mode style string */
    char *widgRec,              /* Cubicle structure record */
    int offset)
{                               /* Offset of style in record */
    RbcBarMode     *modePtr = (RbcBarMode *) (widgRec + offset);
    unsigned int    length;
    char            c;

    c = string[0];
    length = strlen(string);
    if ((c == 'n') && (strncmp(string, "normal", length) == 0)) {
        *modePtr = MODE_INFRONT;
    } else if ((c == 'i') && (strncmp(string, "infront", length) == 0)) {
        *modePtr = MODE_INFRONT;
    } else if ((c == 's') && (strncmp(string, "stacked", length) == 0)) {
        *modePtr = MODE_STACKED;
    } else if ((c == 'a') && (strncmp(string, "aligned", length) == 0)) {
        *modePtr = MODE_ALIGNED;
    } else if ((c == 'o') && (strncmp(string, "overlap", length) == 0)) {
        *modePtr = MODE_OVERLAP;
    } else {
        Tcl_AppendResult(interp, "bad mode argument \"", string,
            "\": should be \"infront\", \"stacked\", \"overlap\", or \"aligned\"",
            (char *) NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * BarModeToString --
 *
 *      Returns the mode style string based upon the mode flags.
 *
 * Results:
 *      The mode style string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static const char *
BarModeToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Row/column structure record */
    int offset,                 /* Offset of mode in Partition record */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Not used. */
    RbcBarMode      mode = *(RbcBarMode *) (widgRec + offset);

    return NameOfBarMode(mode);
}

/*
 *----------------------------------------------------------------------
 *
 * ClearPalette --
 *
 *      Zero out the style's number of rectangles and errorbars.
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
ClearPalette(
    RbcChain * palette)
{
    register BarPenStyle *stylePtr;
    RbcChainLink   *linkPtr;

    for (linkPtr = RbcChainFirstLink(palette); linkPtr != NULL;
        linkPtr = RbcChainNextLink(linkPtr)) {
        stylePtr = RbcChainGetValue(linkPtr);
        stylePtr->xErrorBarCnt = stylePtr->yErrorBarCnt = stylePtr->nRects = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigurePen --
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
ConfigurePen(
    RbcGraph * graphPtr,
    RbcPen * penPtr)
{
    BarPen         *bpPtr = (BarPen *) penPtr;
    XGCValues       gcValues;
    unsigned long   gcMask;
    int             fillStyle;
    GC              newGC;
    long            defColor;

    RbcResetTextStyle(graphPtr->tkwin, &(bpPtr->valueStyle));
    gcMask = GCForeground;
    if (bpPtr->fgColor != NULL) {
        defColor = bpPtr->fgColor->pixel;
        gcValues.foreground = bpPtr->fgColor->pixel;
    } else if (bpPtr->border != NULL) {
        defColor = Tk_3DBorderColor(bpPtr->border)->pixel;
        gcValues.foreground = Tk_3DBorderColor(bpPtr->border)->pixel;
    } else {
        defColor = BlackPixel(graphPtr->display,
            Tk_ScreenNumber(graphPtr->tkwin));
    }
    if ((bpPtr->fgColor != NULL) && (bpPtr->border != NULL)) {
        gcMask |= GCBackground;
        gcValues.background = Tk_3DBorderColor(bpPtr->border)->pixel;
        fillStyle = FillOpaqueStippled;
    } else {
        fillStyle = FillStippled;
    }
    if (bpPtr->stipple != None) {
        gcValues.stipple = bpPtr->stipple;
        gcValues.fill_style = fillStyle;
        gcMask |= (GCStipple | GCFillStyle);
    }
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (bpPtr->gc != NULL) {
        Tk_FreeGC(graphPtr->display, bpPtr->gc);
    }
    bpPtr->gc = newGC;

    gcMask = GCForeground | GCLineWidth;
    if (bpPtr->errorBarColor == RBC_COLOR_DEFAULT) {
        gcValues.foreground = defColor;
    } else {
        gcValues.foreground = bpPtr->errorBarColor->pixel;
    }
    gcValues.line_width = RbcLineWidth(bpPtr->errorBarLineWidth);
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (bpPtr->errorBarGC != NULL) {
        Tk_FreeGC(graphPtr->display, bpPtr->errorBarGC);
    }
    bpPtr->errorBarGC = newGC;

    return TCL_OK;
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
    RbcGraph * graphPtr,
    RbcPen * penPtr)
{
    BarPen         *bpPtr = (BarPen *) penPtr;

    RbcFreeTextStyle(graphPtr->display, &(bpPtr->valueStyle));
    if (bpPtr->gc != NULL) {
        Tk_FreeGC(graphPtr->display, bpPtr->gc);
    }
    if (bpPtr->errorBarGC != NULL) {
        Tk_FreeGC(graphPtr->display, bpPtr->errorBarGC);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * InitPen --
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
InitPen(
    BarPen * penPtr)
{
    RbcInitTextStyle(&(penPtr->valueStyle));
    penPtr->specsPtr = barPenConfigSpecs;
    penPtr->configProc = ConfigurePen;
    penPtr->destroyProc = DestroyPen;
    penPtr->relief = TK_RELIEF_RAISED;
    penPtr->flags = RBC_NORMAL_PEN;
    penPtr->errorBarShow = RBC_SHOW_BOTH;
    penPtr->valueShow = RBC_SHOW_NONE;
    penPtr->borderWidth = 2;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcBarPen --
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
RbcPen         *
RbcBarPen(
    const char *penName)
{
    BarPen         *penPtr;

    penPtr = RbcCalloc(1, sizeof(BarPen));
    assert(penPtr);
    InitPen(penPtr);
    penPtr->name = RbcStrdup(penName);
    if (strcmp(penName, "activeBar") == 0) {
        penPtr->flags = RBC_ACTIVE_PEN;
    }
    return (RbcPen *) penPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * CheckStacks --
 *
 *      Check that the data limits are not superseded by the heights
 *      of stacked bar segments.  The heights are calculated by
 *      RbcComputeStacks.
 *
 * Results:
 *      If the y-axis limits need to be adjusted for stacked segments,
 *      *minPtr* or *maxPtr* are updated.
 *
 * Side effects:
 *      Autoscaling of the y-axis is affected.
 *
 * ----------------------------------------------------------------------
 */
static void
CheckStacks(
    RbcGraph * graphPtr,
    RbcAxis2D * pairPtr,
    double *minPtr,             /* Current minimum for y-axis */
    double *maxPtr)
{                               /* Current maximum for y-axis */
    RbcFreqInfo    *infoPtr;
    register int    i;

    if ((graphPtr->mode != MODE_STACKED) || (graphPtr->nStacks == 0)) {
        return;
    }
    infoPtr = graphPtr->freqArr;
    for (i = 0; i < graphPtr->nStacks; i++) {
        if ((infoPtr->axes.x == pairPtr->x) && (infoPtr->axes.y == pairPtr->y)) {
            /*

             * Check if any of the y-values (because of stacking) are
             * greater than the current limits of the graph.
             */
            if (infoPtr->sum < 0.0) {
                if (*minPtr > infoPtr->sum) {
                    *minPtr = infoPtr->sum;
                }
            } else {
                if (*maxPtr < infoPtr->sum) {
                    *maxPtr = infoPtr->sum;
                }
            }
        }
        infoPtr++;
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureBar --
 *
 *      Sets up the appropriate configuration parameters in the GC.
 *      It is assumed the parameters have been previously set by
 *      a call to Tk_ConfigureWidget.
 *
 * Results:
 *      The return value is a standard Tcl result.  If TCL_ERROR is
 *      returned, then interp->result contains an error message.
 *
 * Side effects:
 *      Configuration information such as bar foreground/background
 *      color and stipple etc. get set in a new GC.
 *
 * ----------------------------------------------------------------------
 */
static int
ConfigureBar(
    RbcGraph * graphPtr,
    register RbcElement * elemPtr)
{
    Bar            *barPtr = (Bar *) elemPtr;
    RbcChainLink   *linkPtr;

    if (ConfigurePen(graphPtr, (RbcPen *) & (barPtr->builtinPen)) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Point to the static normal pen if no external pens have
     * been selected.
     */
    if (barPtr->normalPenPtr == NULL) {
        barPtr->normalPenPtr = &(barPtr->builtinPen);
    }
    linkPtr = RbcChainFirstLink(barPtr->palette);
    if (linkPtr != NULL) {
        BarPenStyle    *stylePtr;

        stylePtr = RbcChainGetValue(linkPtr);
        stylePtr->penPtr = barPtr->normalPenPtr;
    }
    if (RbcConfigModified(barPtr->specsPtr, "-barwidth", "-*data",
            "-map*", "-label", "-hide", "-x", "-y", (char *) NULL)) {
        barPtr->flags |= RBC_MAP_ITEM;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GetBarExtents --
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
GetBarExtents(
    RbcElement * elemPtr,
    RbcExtents2D * extsPtr)
{
    RbcGraph       *graphPtr = elemPtr->graphPtr;
    Bar            *barPtr = (Bar *) elemPtr;
    double          middle, barWidth;
    int             nPoints;

    extsPtr->top = extsPtr->left = DBL_MAX;
    extsPtr->bottom = extsPtr->right = -DBL_MAX;

    nPoints = RbcNumberOfPoints(barPtr);
    if (nPoints < 1) {
        return;                 /* No data points */
    }
    barWidth = graphPtr->barWidth;
    if (barPtr->barWidth > 0.0) {
        barWidth = barPtr->barWidth;
    }
    middle = barWidth * 0.5;
    extsPtr->left = barPtr->x.min - middle;
    extsPtr->right = barPtr->x.max + middle;

    extsPtr->top = barPtr->y.min;
    extsPtr->bottom = barPtr->y.max;
    if (extsPtr->bottom < graphPtr->baseline) {
        extsPtr->bottom = graphPtr->baseline;
    }
    /*
     * Handle "stacked" bar elements specially.
     *
     * If element is stacked, the sum of its ordinates may be outside
     * the minimum/maximum limits of the element's data points.
     */
    if ((graphPtr->mode == MODE_STACKED) && (graphPtr->nStacks > 0)) {
        CheckStacks(graphPtr, &(elemPtr->axes), &(extsPtr->top),
            &(extsPtr->bottom));
    }
    /* Warning: You get what you deserve if the x-axis is logScale */
    if (elemPtr->axes.x->logScale) {
        extsPtr->left = RbcFindElemVectorMinimum(&(barPtr->x), DBL_MIN) +
            middle;
    }
    /* Fix y-min limits for barchart */
    if (elemPtr->axes.y->logScale) {
        if ((extsPtr->top <= 0.0) || (extsPtr->top > 1.0)) {
            extsPtr->top = 1.0;
        }
    } else {
        if (extsPtr->top > 0.0) {
            extsPtr->top = 0.0;
        }
    }
    /* Correct the extents for error bars if they exist. */
    if (elemPtr->xError.nValues > 0) {
        register int    i;
        double          x;

        /* Correct the data limits for error bars */
        nPoints = MIN(elemPtr->xError.nValues, nPoints);
        for (i = 0; i < nPoints; i++) {
            x = elemPtr->x.valueArr[i] + elemPtr->xError.valueArr[i];
            if (x > extsPtr->right) {
                extsPtr->right = x;
            }
            x = elemPtr->x.valueArr[i] - elemPtr->xError.valueArr[i];
            if (elemPtr->axes.x->logScale) {
                if (x < 0.0) {
                    x = -x;     /* Mirror negative values, instead
                                 * of ignoring them. */
                }
                if ((x > DBL_MIN) && (x < extsPtr->left)) {
                    extsPtr->left = x;
                }
            } else if (x < extsPtr->left) {
                extsPtr->left = x;
            }
        }
    } else {
        if ((elemPtr->xHigh.nValues > 0) &&
            (elemPtr->xHigh.max > extsPtr->right)) {
            extsPtr->right = elemPtr->xHigh.max;
        }
        if (elemPtr->xLow.nValues > 0) {
            double          left;

            if ((elemPtr->xLow.min <= 0.0) && (elemPtr->axes.x->logScale)) {
                left = RbcFindElemVectorMinimum(&elemPtr->xLow, DBL_MIN);
            } else {
                left = elemPtr->xLow.min;
            }
            if (left < extsPtr->left) {
                extsPtr->left = left;
            }
        }
    }
    if (elemPtr->yError.nValues > 0) {
        register int    i;
        double          y;

        nPoints = MIN(elemPtr->yError.nValues, nPoints);
        for (i = 0; i < nPoints; i++) {
            y = elemPtr->y.valueArr[i] + elemPtr->yError.valueArr[i];
            if (y > extsPtr->bottom) {
                extsPtr->bottom = y;
            }
            y = elemPtr->y.valueArr[i] - elemPtr->yError.valueArr[i];
            if (elemPtr->axes.y->logScale) {
                if (y < 0.0) {
                    y = -y;     /* Mirror negative values, instead
                                 * of ignoring them. */
                }
                if ((y > DBL_MIN) && (y < extsPtr->left)) {
                    extsPtr->top = y;
                }
            } else if (y < extsPtr->top) {
                extsPtr->top = y;
            }
        }
    } else {
        if ((elemPtr->yHigh.nValues > 0) &&
            (elemPtr->yHigh.max > extsPtr->bottom)) {
            extsPtr->bottom = elemPtr->yHigh.max;
        }
        if (elemPtr->yLow.nValues > 0) {
            double          top;

            if ((elemPtr->yLow.min <= 0.0) && (elemPtr->axes.y->logScale)) {
                top = RbcFindElemVectorMinimum(&elemPtr->yLow, DBL_MIN);
            } else {
                top = elemPtr->yLow.min;
            }
            if (top < extsPtr->top) {
                extsPtr->top = top;
            }
        }
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ClosestBar --
 *
 *      Find the bar segment closest to the window coordinates	point
 *      specified.
 *
 *      Note:  This does not return the height of the stacked segment
 *             (in graph coordinates) properly.
 *
 * Results:
 *      Returns 1 if the point is width any bar segment, otherwise 0.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void
ClosestBar(
    RbcGraph * graphPtr,        /* Graph widget record */
    RbcElement * elemPtr,       /* Bar element */
    RbcClosestSearch * searchPtr)
{                               /* Info of closest point in element */
    Bar            *barPtr = (Bar *) elemPtr;
    RbcPoint2D     *pointPtr, *endPtr;
    RbcPoint2D      t, outline[5];
    XRectangle     *rectPtr;
    double          left, right, top, bottom;
    double          minDist, dist;
    int             imin;
    register int    i;

    minDist = searchPtr->dist;
    imin = 0;

    rectPtr = barPtr->rectangles;
    for (i = 0; i < barPtr->nRects; i++) {
        /* Point in rectangle */
        if (    searchPtr->x <= (int)(rectPtr->x + rectPtr->width -1)
            &&  searchPtr->x >= (int)(rectPtr->x)
            &&  searchPtr->y <= (int)(rectPtr->y + rectPtr->height -1)
            &&  searchPtr->y >= (int)(rectPtr->y)) {
            imin = barPtr->rectToData[i];
            minDist = 0.0;
            break;
        }
        left = rectPtr->x, top = rectPtr->y;
        right = (double) (rectPtr->x + rectPtr->width);
        bottom = (double) (rectPtr->y + rectPtr->height);
        outline[4].x = outline[3].x = outline[0].x = left;
        outline[4].y = outline[1].y = outline[0].y = top;
        outline[2].x = outline[1].x = right;
        outline[3].y = outline[2].y = bottom;

        for (pointPtr = outline, endPtr = outline + 4; pointPtr < endPtr;
            pointPtr++) {
            t = RbcGetProjection(searchPtr->x, searchPtr->y,
                pointPtr, pointPtr + 1);
            if (t.x > right) {
                t.x = right;
            } else if (t.x < left) {
                t.x = left;
            }
            if (t.y > bottom) {
                t.y = bottom;
            } else if (t.y < top) {
                t.y = top;
            }
            dist = hypot((t.x - searchPtr->x), (t.y - searchPtr->y));
            if (dist < minDist) {
                minDist = dist;
                imin = barPtr->rectToData[i];
            }
        }
        rectPtr++;
    }
    if (minDist < searchPtr->dist) {
        searchPtr->elemPtr = (RbcElement *) elemPtr;
        searchPtr->dist = minDist;
        searchPtr->index = imin;
        searchPtr->point.x = (double) barPtr->x.valueArr[imin];
        searchPtr->point.y = (double) barPtr->y.valueArr[imin];
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MergePens --
 *
 *      Reorders the both arrays of points and errorbars to merge pens.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The old arrays are freed and new ones allocated containing
 *      the reordered points and errorbars.
 *
 *----------------------------------------------------------------------
 */
static void
MergePens(
    Bar * barPtr,
    RbcPenStyle ** dataToStyle)
{
    BarPenStyle    *stylePtr;
    RbcChainLink   *linkPtr;

    if (RbcChainGetLength(barPtr->palette) < 2) {
        linkPtr = RbcChainFirstLink(barPtr->palette);
        stylePtr = RbcChainGetValue(linkPtr);
        stylePtr->nRects = barPtr->nRects;
        stylePtr->rectangles = barPtr->rectangles;
        stylePtr->symbolSize = barPtr->rectangles->width / 2;
        stylePtr->xErrorBarCnt = barPtr->xErrorBarCnt;
        stylePtr->xErrorBars = barPtr->xErrorBars;
        stylePtr->yErrorBarCnt = barPtr->yErrorBarCnt;
        stylePtr->yErrorBars = barPtr->yErrorBars;
        return;
    }
    /* We have more than one style. Group bar segments of like pen
     * styles together.  */

    if (barPtr->nRects > 0) {
        XRectangle     *rectangles;
        int            *rectToData;
        int             dataIndex;
        register XRectangle *rectPtr;
        register int   *indexPtr;
        register int    i;

        rectangles =
            (XRectangle *) ckalloc(barPtr->nRects * sizeof(XRectangle));
        rectToData = (int *) ckalloc(barPtr->nRects * sizeof(int));
        assert(rectangles && rectToData);

        rectPtr = rectangles, indexPtr = rectToData;
        for (linkPtr = RbcChainFirstLink(barPtr->palette); linkPtr != NULL;
            linkPtr = RbcChainNextLink(linkPtr)) {
            stylePtr = RbcChainGetValue(linkPtr);
            stylePtr->symbolSize = rectPtr->width / 2;
            stylePtr->rectangles = rectPtr;
            for (i = 0; i < barPtr->nRects; i++) {
                dataIndex = barPtr->rectToData[i];
                if (dataToStyle[dataIndex] == (RbcPenStyle *) stylePtr) {
                    *rectPtr++ = barPtr->rectangles[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->nRects = rectPtr - stylePtr->rectangles;
        }
        ckfree((char *) barPtr->rectangles);
        barPtr->rectangles = rectangles;
        ckfree((char *) barPtr->rectToData);
        barPtr->rectToData = rectToData;
    }
    if (barPtr->xErrorBarCnt > 0) {
        RbcSegment2D   *errorBars, *segPtr;
        int            *errorToData, *indexPtr;
        int             dataIndex;
        register int    i;

        errorBars =
            (RbcSegment2D *) ckalloc(barPtr->xErrorBarCnt *
            sizeof(RbcSegment2D));
        errorToData = (int *) ckalloc(barPtr->xErrorBarCnt * sizeof(int));
        assert(errorBars);
        segPtr = errorBars, indexPtr = errorToData;
        for (linkPtr = RbcChainFirstLink(barPtr->palette);
            linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
            stylePtr = RbcChainGetValue(linkPtr);
            stylePtr->xErrorBars = segPtr;
            for (i = 0; i < barPtr->xErrorBarCnt; i++) {
                dataIndex = barPtr->xErrorToData[i];
                if (dataToStyle[dataIndex] == (RbcPenStyle *) stylePtr) {
                    *segPtr++ = barPtr->xErrorBars[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->xErrorBarCnt = segPtr - stylePtr->xErrorBars;
        }
        ckfree((char *) barPtr->xErrorBars);
        barPtr->xErrorBars = errorBars;
        ckfree((char *) barPtr->xErrorToData);
        barPtr->xErrorToData = errorToData;
    }
    if (barPtr->yErrorBarCnt > 0) {
        RbcSegment2D   *errorBars, *segPtr;
        int            *errorToData, *indexPtr;
        int             dataIndex;
        register int    i;

        errorBars =
            (RbcSegment2D *) ckalloc(barPtr->yErrorBarCnt *
            sizeof(RbcSegment2D));
        errorToData = (int *) ckalloc(barPtr->yErrorBarCnt * sizeof(int));
        assert(errorBars);
        segPtr = errorBars, indexPtr = errorToData;
        for (linkPtr = RbcChainFirstLink(barPtr->palette);
            linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
            stylePtr = RbcChainGetValue(linkPtr);
            stylePtr->yErrorBars = segPtr;
            for (i = 0; i < barPtr->yErrorBarCnt; i++) {
                dataIndex = barPtr->yErrorToData[i];
                if (dataToStyle[dataIndex] == (RbcPenStyle *) stylePtr) {
                    *segPtr++ = barPtr->yErrorBars[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->yErrorBarCnt = segPtr - stylePtr->yErrorBars;
        }
        ckfree((char *) barPtr->yErrorBars);
        barPtr->yErrorBars = errorBars;
        ckfree((char *) barPtr->yErrorToData);
        barPtr->yErrorToData = errorToData;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MapActiveBars --
 *
 *      Creates an array of points of the active graph coordinates.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is freed and allocated for the active point array.
 *
 *----------------------------------------------------------------------
 */
static void
MapActiveBars(
    Bar * barPtr)
{
    if (barPtr->activeRects != NULL) {
        ckfree((char *) barPtr->activeRects);
        barPtr->activeRects = NULL;
    }
    if (barPtr->activeToData != NULL) {
        ckfree((char *) barPtr->activeToData);
        barPtr->activeToData = NULL;
    }
    barPtr->nActive = 0;

    if (barPtr->nActiveIndices > 0) {
        XRectangle     *activeRects;
        int            *activeToData;
        register int    i, n;
        register int    count;

        activeRects =
            (XRectangle *) ckalloc(sizeof(XRectangle) * barPtr->nActiveIndices);
        assert(activeRects);
        activeToData = (int *) ckalloc(sizeof(int) * barPtr->nActiveIndices);
        assert(activeToData);
        count = 0;
        for (i = 0; i < barPtr->nRects; i++) {
            for (n = 0; n < barPtr->nActiveIndices; n++) {
                if (barPtr->rectToData[i] == barPtr->activeIndices[n]) {
                    activeRects[count] = barPtr->rectangles[i];
                    activeToData[count] = i;
                    count++;
                }
            }
        }
        barPtr->nActive = count;
        barPtr->activeRects = activeRects;
        barPtr->activeToData = activeToData;
    }
    barPtr->flags &= ~RBC_ACTIVE_PENDING;
}

/*
 *----------------------------------------------------------------------
 *
 * ResetBar --
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
ResetBar(
    Bar * barPtr)
{
    /* Release any storage associated with the display of the bar */
    ClearPalette(barPtr->palette);
    if (barPtr->activeRects != NULL) {
        ckfree((char *) barPtr->activeRects);
    }
    if (barPtr->activeToData != NULL) {
        ckfree((char *) barPtr->activeToData);
    }
    if (barPtr->xErrorBars != NULL) {
        ckfree((char *) barPtr->xErrorBars);
    }
    if (barPtr->xErrorToData != NULL) {
        ckfree((char *) barPtr->xErrorToData);
    }
    if (barPtr->yErrorBars != NULL) {
        ckfree((char *) barPtr->yErrorBars);
    }
    if (barPtr->yErrorToData != NULL) {
        ckfree((char *) barPtr->yErrorToData);
    }
    if (barPtr->rectangles != NULL) {
        ckfree((char *) barPtr->rectangles);
    }
    if (barPtr->rectToData != NULL) {
        ckfree((char *) barPtr->rectToData);
    }
    barPtr->activeToData = barPtr->xErrorToData = barPtr->yErrorToData =
        barPtr->rectToData = NULL;
    barPtr->activeRects = barPtr->rectangles = NULL;
    barPtr->xErrorBars = barPtr->yErrorBars = NULL;
    barPtr->nActive = barPtr->xErrorBarCnt = barPtr->yErrorBarCnt =
        barPtr->nRects = 0;
}

/*
 * ----------------------------------------------------------------------
 *
 * MapBar --
 *
 *      Calculates the actual window coordinates of the bar element.
 *      The window coordinates are saved in the bar element structure.
 *
 *      A bar can have multiple segments (more than one x,y pairs).
 *      In this case, the bar can be represented as either a set of
 *      non-contiguous bars or a single multi-segmented (stacked) bar.
 *
 *      The x-axis layout for a barchart may be presented in one of
 *      two ways.  If abscissas are used, the bars are placed at those
 *      coordinates.  Otherwise, the range will represent the number
 *      of values.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void
MapBar(
    RbcGraph * graphPtr,
    RbcElement * elemPtr)
{
    Bar            *barPtr = (Bar *) elemPtr;
    FreqKey         key;
    RbcPenStyle   **dataToStyle;
    RbcPoint2D      c1, c2;     /* Two opposite corners of the rectangle
                                 * in graph coordinates. */
    double         *x, *y;
    double          barWidth, barOffset;
    double          baseline;
    double          dx, dy;
    int            *rectToData; /* Maps rectangles to data point indices */
    int             height;
    int             invertBar;
    int             nPoints, count;
    register XRectangle *rectPtr, *rectangles;
    register int    i;
    int             size;
    RbcChainLink   *linkPtr;
    BarPenStyle    *stylePtr;

    ResetBar(barPtr);
    nPoints = RbcNumberOfPoints(barPtr);
    if (nPoints < 1) {
        return;                 /* No data points */
    }
    barWidth = graphPtr->barWidth;
    if (barPtr->barWidth > 0.0) {
        barWidth = barPtr->barWidth;
    }
    baseline = (barPtr->axes.y->logScale) ? 1.0 : graphPtr->baseline;
    barOffset = barWidth * 0.5;

    /*
     * Create an array of rectangles representing the screen coordinates
     * of all the segments in the bar.
     */
    rectPtr = rectangles = (XRectangle *) ckalloc(nPoints * sizeof(XRectangle));
    assert(rectangles);
    rectToData = RbcCalloc(nPoints, sizeof(int));
    assert(rectToData);

    x = barPtr->x.valueArr, y = barPtr->y.valueArr;
    count = 0;
    for (i = 0; i < nPoints; i++) {
        if (((x[i] - barWidth) > barPtr->axes.x->axisRange.max) ||
            ((x[i] + barWidth) < barPtr->axes.x->axisRange.min)) {
            continue;           /* Abscissa is out of range of the x-axis */
        }
        c1.x = x[i] - barOffset;
        c1.y = y[i];
        c2.x = c1.x + barWidth;
        c2.y = baseline;

        /*
         * If the mode is "aligned" or "stacked" we need to adjust the
         * x or y coordinates of the two corners.
         */

        if ((graphPtr->nStacks > 0) && (graphPtr->mode != MODE_INFRONT)) {
            Tcl_HashEntry  *hPtr;

            key.value = x[i];
            key.axes = barPtr->axes;
            hPtr = Tcl_FindHashEntry(&(graphPtr->freqTable), (char *) &key);
            if (hPtr != NULL) {
                RbcFreqInfo    *infoPtr;
                double          slice, width;

                infoPtr = (RbcFreqInfo *) Tcl_GetHashValue(hPtr);
                switch (graphPtr->mode) {
                case MODE_STACKED:
                    c2.y = infoPtr->lastY;
                    c1.y += c2.y;
                    infoPtr->lastY = c1.y;
                    break;

                case MODE_ALIGNED:
                    infoPtr->count++;
                    slice = barWidth / (double) infoPtr->freq;
                    c1.x += slice * (infoPtr->freq - infoPtr->count);
                    c2.x = c1.x + slice;
                    break;

                case MODE_OVERLAP:
                    infoPtr->count++;
                    slice = barWidth / (double) (infoPtr->freq * 2);
                    width = slice * (infoPtr->freq + 1);
                    c1.x += slice * (infoPtr->freq - infoPtr->count);
                    c2.x = c1.x + width;
                    break;
                case MODE_INFRONT:
                    break;
                }
            }
        }
        invertBar = FALSE;
        if (c1.y < c2.y) {
            double          temp;

            /* Handle negative bar values by swapping ordinates */
            temp = c1.y, c1.y = c2.y, c2.y = temp;
            invertBar = TRUE;
        }
        /*
         * Get the two corners of the bar segment and compute the rectangle
         */
        c1 = RbcMap2D(graphPtr, c1.x, c1.y, &barPtr->axes);
        c2 = RbcMap2D(graphPtr, c2.x, c2.y, &barPtr->axes);

        /* Bound the bars vertically by the size of the graph window */
        if (c1.y < 0.0) {
            c1.y = 0.0;
        } else if (c1.y > (double) graphPtr->height) {
            c1.y = (double) graphPtr->height;
        }
        if (c2.y < 0.0) {
            c2.y = 0.0;
        } else if (c2.y > (double) graphPtr->height) {
            c2.y = (double) graphPtr->height;
        }
        dx = c1.x - c2.x;
        dy = c1.y - c2.y;
        height = (int) Round(FABS(dy));
        if (invertBar) {
            rectPtr->y = (short int) MIN(c1.y, c2.y);
        } else {
            rectPtr->y = (short int) (MAX(c1.y, c2.y)) - height;
        }
        rectPtr->x = (short int) MIN(c1.x, c2.x);
        rectPtr->width = (short int) Round(FABS(dx)) + 1;
        if (rectPtr->width < 1) {
            rectPtr->width = 1;
        }
        rectPtr->height = height + 1;
        if (rectPtr->height < 1) {
            rectPtr->height = 1;
        }
        rectToData[count] = i;  /* Save the data index corresponding to the
                                 * rectangle */
        rectPtr++;
        count++;
    }
    barPtr->nRects = count;
    barPtr->rectangles = rectangles;
    barPtr->rectToData = rectToData;
    if (barPtr->nActiveIndices > 0) {
        MapActiveBars(barPtr);
    }

    size = 20;
    if (count > 0) {
        size = rectangles->width;
    }
    /* Set the symbol size of all the pen styles. */
    for (linkPtr = RbcChainFirstLink(barPtr->palette); linkPtr != NULL;
        linkPtr = RbcChainNextLink(linkPtr)) {
        stylePtr = RbcChainGetValue(linkPtr);
        stylePtr->symbolSize = size;
        stylePtr->errorBarCapWidth = (stylePtr->penPtr->errorBarCapWidth > 0)
            ? stylePtr->penPtr->errorBarCapWidth : (int) (size * 0.6666666);
        stylePtr->errorBarCapWidth /= 2;
    }
    dataToStyle = RbcStyleMap((RbcElement *) barPtr);
    if (((barPtr->yHigh.nValues > 0) && (barPtr->yLow.nValues > 0)) ||
        ((barPtr->xHigh.nValues > 0) && (barPtr->xLow.nValues > 0)) ||
        (barPtr->xError.nValues > 0) || (barPtr->yError.nValues > 0)) {
        RbcMapErrorBars(graphPtr, (RbcElement *) barPtr, dataToStyle);
    }
    MergePens(barPtr, dataToStyle);
    ckfree((char *) dataToStyle);
}

/*
 * -----------------------------------------------------------------
 *
 * DrawSymbol --
 *
 *      Draw a symbol centered at the given x,y window coordinate
 *      based upon the element symbol type and size.
 *
 *      Most notable problem is the round-off errors generated when
 *      calculating the centered position of the symbol.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
static void
DrawSymbol(
    RbcGraph * graphPtr,
    Drawable drawable,          /* Pixmap or window to draw into */
    RbcElement * elemPtr,
    int x,
    int y,
    int size)
{
    BarPen         *penPtr = ((Bar *) elemPtr)->normalPenPtr;
    int             radius;

    if ((penPtr->border == NULL) && (penPtr->fgColor == NULL)) {
        return;
    }
    radius = (size / 2);
    size--;

    x -= radius;
    y -= radius;
    XSetTSOrigin(graphPtr->display, penPtr->gc, x, y);
    XFillRectangle(graphPtr->display, drawable, penPtr->gc, x, y, size, size);
    XSetTSOrigin(graphPtr->display, penPtr->gc, 0, 0);
}

/*
 * -----------------------------------------------------------------
 *
 * DrawBarSegments --
 *
 *      Draws each of the rectangular segments for the element.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
static void
DrawBarSegments(
    RbcGraph * graphPtr,
    Drawable drawable,          /* Pixmap or window to draw into */
    BarPen * penPtr,
    XRectangle * rectangles,
    int nRects)
{
    register XRectangle *rectPtr;

    if ((penPtr->border == NULL) && (penPtr->fgColor == NULL)) {
        return;
    }
    XFillRectangles(graphPtr->display, drawable, penPtr->gc, rectangles,
        nRects);
    if ((penPtr->border != NULL) && (penPtr->borderWidth > 0) &&
        (penPtr->relief != TK_RELIEF_FLAT)) {
        XRectangle     *endPtr;

        for (rectPtr = rectangles, endPtr = rectangles + nRects;
            rectPtr < endPtr; rectPtr++) {
            Tk_Draw3DRectangle(graphPtr->tkwin, drawable, penPtr->border,
                rectPtr->x, rectPtr->y, rectPtr->width, rectPtr->height,
                penPtr->borderWidth, penPtr->relief);
        }
    }
}

/*
 * -----------------------------------------------------------------
 *
 * DrawBarValues --
 *
 *      Draws the numeric value of the bar.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
static void
DrawBarValues(
    RbcGraph * graphPtr,
    Drawable drawable,
    Bar * barPtr,
    BarPen * penPtr,
    XRectangle * rectangles,
    int nRects,
    int *rectToData)
{
    XRectangle     *rectPtr, *endPtr;
    int             count;
    const char     *fmt;
    char            string[TCL_DOUBLE_SPACE * 2 + 2];
    double          x, y;
    RbcPoint2D      anchorPos;

    count = 0;
    fmt = penPtr->valueFormat;
    if (fmt == NULL) {
        fmt = "%g";
    }
    for (rectPtr = rectangles, endPtr = rectangles + nRects; rectPtr < endPtr;
        rectPtr++) {
        x = barPtr->x.valueArr[rectToData[count]];
        y = barPtr->y.valueArr[rectToData[count]];
        count++;
        if (penPtr->valueShow == RBC_SHOW_X) {
            sprintf(string, fmt, x);
        } else if (penPtr->valueShow == RBC_SHOW_Y) {
            sprintf(string, fmt, y);
        } else if (penPtr->valueShow == RBC_SHOW_BOTH) {
            sprintf(string, fmt, x);
            strcat(string, ",");
            sprintf(string + strlen(string), fmt, y);
        }
        if (graphPtr->inverted) {
            anchorPos.y = rectPtr->y + rectPtr->height * 0.5;
            anchorPos.x = rectPtr->x + rectPtr->width;
            if (y < graphPtr->baseline) {
                anchorPos.x -= rectPtr->width;
            }
        } else {
            anchorPos.x = rectPtr->x + rectPtr->width * 0.5;
            anchorPos.y = rectPtr->y;
            if (y < graphPtr->baseline) {
                anchorPos.y += rectPtr->height;
            }
        }
        RbcDrawText(graphPtr->tkwin, drawable, string, &(penPtr->valueStyle),
            (int) anchorPos.x, (int) anchorPos.y);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * DrawNormalBar --
 *
 *      Draws the rectangle representing the bar element.  If the
 *      relief option is set to "raised" or "sunken" and the bar
 *      borderwidth is set (borderwidth > 0), a 3D border is drawn
 *      around the bar.
 *
 *      Don't draw bars that aren't visible (i.e. within the limits
 *      of the axis).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      X drawing commands are output.
 *
 * ----------------------------------------------------------------------
 */
static void
DrawNormalBar(
    RbcGraph * graphPtr,
    Drawable drawable,
    RbcElement * elemPtr)
{
    Bar            *barPtr = (Bar *) elemPtr;
    int             count;
    RbcChainLink   *linkPtr;
    register BarPenStyle *stylePtr;
    BarPen         *penPtr;

    count = 0;
    for (linkPtr = RbcChainFirstLink(barPtr->palette); linkPtr != NULL;
        linkPtr = RbcChainNextLink(linkPtr)) {
        stylePtr = RbcChainGetValue(linkPtr);
        penPtr = stylePtr->penPtr;
        if (stylePtr->nRects > 0) {
            DrawBarSegments(graphPtr, drawable, penPtr, stylePtr->rectangles,
                stylePtr->nRects);
        }
        if ((stylePtr->xErrorBarCnt > 0) && (penPtr->errorBarShow & RBC_SHOW_X)) {
            RbcDraw2DSegments(graphPtr->display, drawable, penPtr->errorBarGC,
                stylePtr->xErrorBars, stylePtr->xErrorBarCnt);
        }
        if ((stylePtr->yErrorBarCnt > 0) && (penPtr->errorBarShow & RBC_SHOW_Y)) {
            RbcDraw2DSegments(graphPtr->display, drawable, penPtr->errorBarGC,
                stylePtr->yErrorBars, stylePtr->yErrorBarCnt);
        }
        if (penPtr->valueShow != RBC_SHOW_NONE) {
            DrawBarValues(graphPtr, drawable, barPtr, penPtr,
                stylePtr->rectangles, stylePtr->nRects,
                barPtr->rectToData + count);
        }
        count += stylePtr->nRects;
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * DrawActiveBar --
 *
 *      Draws rectangles representing the active segments of the
 *      bar element.  If the -relief option is set (other than "flat")
 *      and the borderwidth is greater than 0, a 3D border is drawn
 *      around the each bar segment.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      X drawing commands are output.
 *
 * ----------------------------------------------------------------------
 */
static void
DrawActiveBar(
    RbcGraph * graphPtr,
    Drawable drawable,
    RbcElement * elemPtr)
{
    Bar            *barPtr = (Bar *) elemPtr;

    if (barPtr->activePenPtr != NULL) {
        BarPen         *penPtr = barPtr->activePenPtr;

        if (barPtr->nActiveIndices > 0) {
            if (barPtr->flags & RBC_ACTIVE_PENDING) {
                MapActiveBars(barPtr);
            }
            DrawBarSegments(graphPtr, drawable, penPtr, barPtr->activeRects,
                barPtr->nActive);
            if (penPtr->valueShow != RBC_SHOW_NONE) {
                DrawBarValues(graphPtr, drawable, barPtr, penPtr,
                    barPtr->activeRects, barPtr->nActive, barPtr->activeToData);
            }
        } else if (barPtr->nActiveIndices < 0) {
            DrawBarSegments(graphPtr, drawable, penPtr, barPtr->rectangles,
                barPtr->nRects);
            if (penPtr->valueShow != RBC_SHOW_NONE) {
                DrawBarValues(graphPtr, drawable, barPtr, penPtr,
                    barPtr->rectangles, barPtr->nRects, barPtr->rectToData);
            }
        }
    }
}

/*
 * -----------------------------------------------------------------
 *
 * SymbolToPostScript --
 *
 *      Draw a symbol centered at the given x,y window coordinate
 *      based upon the element symbol type and size.
 *
 *      Most notable problem is the round-off errors generated when
 *      calculating the centered position of the symbol.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
static void
SymbolToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken,
    RbcElement * elemPtr,
    double x,
    double y,
    int size)
{
    Bar            *barPtr = (Bar *) elemPtr;
    BarPen         *bpPtr = barPtr->normalPenPtr;

    if ((bpPtr->border == NULL) && (bpPtr->fgColor == NULL)) {
        return;
    }
    /*
     * Build a PostScript procedure to draw the fill and outline of
     * the symbol after the path of the symbol shape has been formed
     */
    RbcAppendToPostScript(psToken, "\n",
        "/DrawSymbolProc {\n", "  gsave\n    ", (char *) NULL);
    if (bpPtr->stipple != None) {
        if (bpPtr->border != NULL) {
            RbcBackgroundToPostScript(psToken, Tk_3DBorderColor(bpPtr->border));
            RbcAppendToPostScript(psToken, "    Fill\n    ", (char *) NULL);
        }
        if (bpPtr->fgColor != NULL) {
            RbcForegroundToPostScript(psToken, bpPtr->fgColor);
        } else {
            RbcForegroundToPostScript(psToken, Tk_3DBorderColor(bpPtr->border));
        }
        RbcStippleToPostScript(psToken, graphPtr->display, bpPtr->stipple);
    } else if (bpPtr->fgColor != NULL) {
        RbcForegroundToPostScript(psToken, bpPtr->fgColor);
        RbcAppendToPostScript(psToken, "    fill\n", (char *) NULL);
    }
    RbcAppendToPostScript(psToken, "  grestore\n", (char *) NULL);
    RbcAppendToPostScript(psToken, "} def\n\n", (char *) NULL);
    RbcFormatToPostScript(psToken, "%g %g %d Sq\n", x, y, size);
}

/*
 *----------------------------------------------------------------------
 *
 * SegmentsToPostScript --
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
SegmentsToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken,
    BarPen * penPtr,
    register XRectangle * rectPtr,
    int nRects)
{
    XRectangle     *endPtr;

    if ((penPtr->border == NULL) && (penPtr->fgColor == NULL)) {
        return;
    }
    for (endPtr = rectPtr + nRects; rectPtr < endPtr; rectPtr++) {
        if ((rectPtr->width < 1) || (rectPtr->height < 1)) {
            continue;
        }
        if (penPtr->stipple != None) {
            RbcRegionToPostScript(psToken,
                (double) rectPtr->x, (double) rectPtr->y,
                (int) rectPtr->width - 1, (int) rectPtr->height - 1);
            if (penPtr->border != NULL) {
                RbcBackgroundToPostScript(psToken,
                    Tk_3DBorderColor(penPtr->border));
                RbcAppendToPostScript(psToken, "Fill\n", (char *) NULL);
            }
            if (penPtr->fgColor != NULL) {
                RbcForegroundToPostScript(psToken, penPtr->fgColor);
            } else {
                RbcForegroundToPostScript(psToken,
                    Tk_3DBorderColor(penPtr->border));
            }
            RbcStippleToPostScript(psToken, graphPtr->display, penPtr->stipple);
        } else if (penPtr->fgColor != NULL) {
            RbcForegroundToPostScript(psToken, penPtr->fgColor);
            RbcRectangleToPostScript(psToken,
                (double) rectPtr->x, (double) rectPtr->y,
                (int) rectPtr->width - 1, (int) rectPtr->height - 1);
        }
        if ((penPtr->border != NULL) && (penPtr->borderWidth > 0) &&
            (penPtr->relief != TK_RELIEF_FLAT)) {
            RbcDraw3DRectangleToPostScript(psToken, penPtr->border,
                (double) rectPtr->x, (double) rectPtr->y,
                (int) rectPtr->width, (int) rectPtr->height,
                penPtr->borderWidth, penPtr->relief);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * BarValuesToPostScript --
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
BarValuesToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken,
    Bar * barPtr,
    BarPen * penPtr,
    XRectangle * rectangles,
    int nRects,
    int *rectToData)
{
    XRectangle     *rectPtr, *endPtr;
    int             count;
    const char     *fmt;
    char            string[TCL_DOUBLE_SPACE * 2 + 2];
    double          x, y;
    RbcPoint2D      anchorPos;

    count = 0;
    fmt = penPtr->valueFormat;
    if (fmt == NULL) {
        fmt = "%g";
    }
    for (rectPtr = rectangles, endPtr = rectangles + nRects; rectPtr < endPtr;
        rectPtr++) {
        x = barPtr->x.valueArr[rectToData[count]];
        y = barPtr->y.valueArr[rectToData[count]];
        count++;
        if (penPtr->valueShow == RBC_SHOW_X) {
            sprintf(string, fmt, x);
        } else if (penPtr->valueShow == RBC_SHOW_Y) {
            sprintf(string, fmt, y);
        } else if (penPtr->valueShow == RBC_SHOW_BOTH) {
            sprintf(string, fmt, x);
            strcat(string, ",");
            sprintf(string + strlen(string), fmt, y);
        }
        if (graphPtr->inverted) {
            anchorPos.y = rectPtr->y + rectPtr->height * 0.5;
            anchorPos.x = rectPtr->x + rectPtr->width;
            if (y < graphPtr->baseline) {
                anchorPos.x -= rectPtr->width;
            }
        } else {
            anchorPos.x = rectPtr->x + rectPtr->width * 0.5;
            anchorPos.y = rectPtr->y;
            if (y < graphPtr->baseline) {
                anchorPos.y += rectPtr->height;
            }
        }
        RbcTextToPostScript(psToken, string, &(penPtr->valueStyle),
            anchorPos.x, anchorPos.y);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ActiveBarToPostScript --
 *
 *      Similar to the NormalBarToPostScript procedure, generates
 *      PostScript commands to display the rectangles representing the
 *      active bar segments of the element.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      PostScript pen width, dashes, and color settings are changed.
 *
 * ----------------------------------------------------------------------
 */
static void
ActiveBarToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken,
    RbcElement * elemPtr)
{
    Bar            *barPtr = (Bar *) elemPtr;

    if (barPtr->activePenPtr != NULL) {
        BarPen         *penPtr = barPtr->activePenPtr;

        if (barPtr->nActiveIndices > 0) {
            if (barPtr->flags & RBC_ACTIVE_PENDING) {
                MapActiveBars(barPtr);
            }
            SegmentsToPostScript(graphPtr, psToken, penPtr,
                barPtr->activeRects, barPtr->nActive);
            if (penPtr->valueShow != RBC_SHOW_NONE) {
                BarValuesToPostScript(graphPtr, psToken, barPtr, penPtr,
                    barPtr->activeRects, barPtr->nActive, barPtr->activeToData);
            }
        } else if (barPtr->nActiveIndices < 0) {
            SegmentsToPostScript(graphPtr, psToken, penPtr,
                barPtr->rectangles, barPtr->nRects);
            if (penPtr->valueShow != RBC_SHOW_NONE) {
                BarValuesToPostScript(graphPtr, psToken, barPtr, penPtr,
                    barPtr->rectangles, barPtr->nRects, barPtr->rectToData);
            }
        }
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * NormalBarToPostScript --
 *
 *      Generates PostScript commands to form the rectangles
 *      representing the segments of the bar element.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      PostScript pen width, dashes, and color settings are changed.
 *
 * ----------------------------------------------------------------------
 */
static void
NormalBarToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken,
    RbcElement * elemPtr)
{
    Bar            *barPtr = (Bar *) elemPtr;
    RbcChainLink   *linkPtr;
    register BarPenStyle *stylePtr;
    int             count;
    BarPen         *penPtr;
    XColor         *colorPtr;

    count = 0;
    for (linkPtr = RbcChainFirstLink(barPtr->palette); linkPtr != NULL;
        linkPtr = RbcChainNextLink(linkPtr)) {
        stylePtr = RbcChainGetValue(linkPtr);
        penPtr = stylePtr->penPtr;
        if (stylePtr->nRects > 0) {
            SegmentsToPostScript(graphPtr, psToken, penPtr,
                stylePtr->rectangles, stylePtr->nRects);
        }
        colorPtr = penPtr->errorBarColor;
        if (colorPtr == RBC_COLOR_DEFAULT) {
            colorPtr = penPtr->fgColor;
        }
        if ((stylePtr->xErrorBarCnt > 0) && (penPtr->errorBarShow & RBC_SHOW_X)) {
            RbcLineAttributesToPostScript(psToken, colorPtr,
                penPtr->errorBarLineWidth, NULL, CapButt, JoinMiter);
            Rbc2DSegmentsToPostScript(psToken, stylePtr->xErrorBars,
                stylePtr->xErrorBarCnt);
        }
        if ((stylePtr->yErrorBarCnt > 0) && (penPtr->errorBarShow & RBC_SHOW_Y)) {
            RbcLineAttributesToPostScript(psToken, colorPtr,
                penPtr->errorBarLineWidth, NULL, CapButt, JoinMiter);
            Rbc2DSegmentsToPostScript(psToken, stylePtr->yErrorBars,
                stylePtr->yErrorBarCnt);
        }
        if (penPtr->valueShow != RBC_SHOW_NONE) {
            BarValuesToPostScript(graphPtr, psToken, barPtr, penPtr,
                stylePtr->rectangles, stylePtr->nRects,
                barPtr->rectToData + count);
        }
        count += stylePtr->nRects;
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * DestroyBar --
 *
 *      Release memory and resources allocated for the bar element.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Everything associated with the bar element is freed up.
 *
 * ----------------------------------------------------------------------
 */
static void
DestroyBar(
    RbcGraph * graphPtr,
    RbcElement * elemPtr)
{
    Bar            *barPtr = (Bar *) elemPtr;

    if (barPtr->normalPenPtr != &(barPtr->builtinPen)) {
        RbcFreePen(graphPtr, (RbcPen *) barPtr->normalPenPtr);
    }
    DestroyPen(graphPtr, (RbcPen *) & (barPtr->builtinPen));
    if (barPtr->activePenPtr != NULL) {
        RbcFreePen(graphPtr, (RbcPen *) barPtr->activePenPtr);
    }
    FreeElemVector(barPtr->x);
    FreeElemVector(barPtr->y);
    FreeElemVector(barPtr->w);
    FreeElemVector(barPtr->xHigh);
    FreeElemVector(barPtr->xLow);
    FreeElemVector(barPtr->xError);
    FreeElemVector(barPtr->yHigh);
    FreeElemVector(barPtr->yLow);
    FreeElemVector(barPtr->yError);

    ResetBar(barPtr);
    if (barPtr->activeIndices != NULL) {
        ckfree((char *) barPtr->activeIndices);
    }
    if (barPtr->palette != NULL) {
        RbcFreePalette(graphPtr, barPtr->palette);
        RbcChainDestroy(barPtr->palette);
    }
    if (barPtr->tags != NULL) {
        ckfree((char *) barPtr->tags);
    }
}

static RbcElementProcs barProcs = {
    ClosestBar,
    ConfigureBar,
    DestroyBar,
    DrawActiveBar,
    DrawNormalBar,
    DrawSymbol,
    GetBarExtents,
    ActiveBarToPostScript,
    NormalBarToPostScript,
    SymbolToPostScript,
    MapBar,
};

/*
 * ----------------------------------------------------------------------
 *
 * RbcBarElement --
 *
 *      Allocate memory and initialize methods for the new bar element.
 *
 * Results:
 *      The pointer to the newly allocated element structure is returned.
 *
 * Side effects:
 *      Memory is allocated for the bar element structure.
 *
 * ----------------------------------------------------------------------
 */
RbcElement     *
RbcBarElement(
    RbcGraph * graphPtr,
    const char *name,
    RbcUid type)
{
    register Bar   *barPtr;

    barPtr = RbcCalloc(1, sizeof(Bar));
    assert(barPtr);
    barPtr->normalPenPtr = &(barPtr->builtinPen);
    barPtr->procsPtr = &barProcs;
    barPtr->specsPtr = barElemConfigSpecs;
    barPtr->labelRelief = TK_RELIEF_FLAT;
    barPtr->classUid = type;
    /* By default, an element's name and label are the same. */
    barPtr->label = RbcStrdup(name);
    barPtr->name = RbcStrdup(name);

    barPtr->graphPtr = graphPtr;
    barPtr->hidden = FALSE;

    InitPen(barPtr->normalPenPtr);
    barPtr->palette = RbcChainCreate();
    return (RbcElement *) barPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcInitFreqTable --
 *
 *      Generate a table of abscissa frequencies.  Duplicate
 *      x-coordinates (depending upon the bar drawing mode) indicate
 *      that something special should be done with each bar segment
 *      mapped to the same abscissa (i.e. it should be stacked,
 *      aligned, or overlay-ed with other segments)
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is allocated for the bar element structure.
 *
 * ----------------------------------------------------------------------
 */
void
RbcInitFreqTable(
    RbcGraph * graphPtr)
{
    register RbcElement *elemPtr;
    RbcChainLink   *linkPtr;
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  cursor;
    Bar            *barPtr;
    int             isNew, count;
    int             nStacks, nSegs;
    int             nPoints;
    FreqKey         key;
    Tcl_HashTable   freqTable;
    register int    i;
    double         *xArr;
    /*
     * Free resources associated with a previous frequency table. This
     * includes the array of frequency information and the table itself
     */
    if (graphPtr->freqArr != NULL) {
        ckfree((char *) graphPtr->freqArr);
        graphPtr->freqArr = NULL;
    }
    if (graphPtr->nStacks > 0) {
        Tcl_DeleteHashTable(&(graphPtr->freqTable));
        graphPtr->nStacks = 0;
    }
    if (graphPtr->mode == MODE_INFRONT) {
        return;                 /* No frequency table is needed for
                                 * "infront" mode */
    }
    Tcl_InitHashTable(&(graphPtr->freqTable), sizeof(FreqKey) / sizeof(int));

    /*
     * Initialize a hash table and fill it with unique abscissas.
     * Keep track of the frequency of each x-coordinate and how many
     * abscissas have duplicate mappings.
     */
    Tcl_InitHashTable(&freqTable, sizeof(FreqKey) / sizeof(int));
    nSegs = nStacks = 0;
    for (linkPtr = RbcChainFirstLink(graphPtr->elements.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        elemPtr = RbcChainGetValue(linkPtr);
        if ((elemPtr->hidden) || (elemPtr->classUid != rbcBarElementUid)) {
            continue;
        }
        nSegs++;
        barPtr = (Bar *) elemPtr;
        xArr = barPtr->x.valueArr;
        nPoints = RbcNumberOfPoints(barPtr);
        for (i = 0; i < nPoints; i++) {
            key.value = xArr[i];
            key.axes = barPtr->axes;
            hPtr = Tcl_CreateHashEntry(&freqTable, (char *) &key, &isNew);
            assert(hPtr != NULL);
            if (isNew) {
                count = 1;
            } else {
                count = (int) Tcl_GetHashValue(hPtr);
                if (count == 1) {
                    nStacks++;
                }
                count++;
            }
            Tcl_SetHashValue(hPtr, (ClientData) count);
        }
    }
    if (nSegs == 0) {
        return;                 /* No bar elements to be displayed */
    }
    if (nStacks > 0) {
        RbcFreqInfo    *infoPtr;
        FreqKey        *keyPtr;
        Tcl_HashEntry  *h2Ptr;

        graphPtr->freqArr = RbcCalloc(nStacks, sizeof(RbcFreqInfo));
        assert(graphPtr->freqArr);
        infoPtr = graphPtr->freqArr;
        for (hPtr = Tcl_FirstHashEntry(&freqTable, &cursor); hPtr != NULL;
            hPtr = Tcl_NextHashEntry(&cursor)) {
            count = (int) Tcl_GetHashValue(hPtr);
            keyPtr = (FreqKey *) Tcl_GetHashKey(&freqTable, hPtr);
            if (count > 1) {
                h2Ptr = Tcl_CreateHashEntry(&(graphPtr->freqTable),
                    (char *) keyPtr, &isNew);
                count = (int) Tcl_GetHashValue(hPtr);
                infoPtr->freq = count;
                infoPtr->axes = keyPtr->axes;
                Tcl_SetHashValue(h2Ptr, infoPtr);
                infoPtr++;
            }
        }
    }
    Tcl_DeleteHashTable(&freqTable);
    graphPtr->nStacks = nStacks;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcComputeStacks --
 *
 *      Determine the height of each stack of bar segments.  A stack
 *      is created by designating two or more points with the same
 *      abscissa.  Each ordinate defines the height of a segment in
 *      the stack.  This procedure simply looks at all the data points
 *      summing the heights of each stacked segment. The sum is saved
 *      in the frequency information table.  This value will be used
 *      to calculate the y-axis limits (data limits aren't sufficient).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The heights of each stack is computed. CheckStacks will
 *      use this information to adjust the y-axis limits if necessary.
 *
 * ----------------------------------------------------------------------
 */
void
RbcComputeStacks(
    RbcGraph * graphPtr)
{
    RbcElement     *elemPtr;
    Bar            *barPtr;
    FreqKey         key;
    RbcChainLink   *linkPtr;
    Tcl_HashEntry  *hPtr;
    int             nPoints;
    register int    i;
    register RbcFreqInfo *infoPtr;
    double         *xArr, *yArr;

    if ((graphPtr->mode != MODE_STACKED) || (graphPtr->nStacks == 0)) {
        return;
    }
    /* Reset the sums for all duplicate values to zero. */

    infoPtr = graphPtr->freqArr;
    for (i = 0; i < graphPtr->nStacks; i++) {
        infoPtr->sum = 0.0;
        infoPtr++;
    }

    /* Look at each bar point, adding the ordinates of duplicate abscissas */

    for (linkPtr = RbcChainFirstLink(graphPtr->elements.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        elemPtr = RbcChainGetValue(linkPtr);
        if ((elemPtr->hidden) || (elemPtr->classUid != rbcBarElementUid)) {
            continue;
        }
        barPtr = (Bar *) elemPtr;
        xArr = barPtr->x.valueArr;
        yArr = barPtr->y.valueArr;
        nPoints = RbcNumberOfPoints(barPtr);
        for (i = 0; i < nPoints; i++) {
            key.value = xArr[i];
            key.axes = barPtr->axes;
            hPtr = Tcl_FindHashEntry(&(graphPtr->freqTable), (char *) &key);
            if (hPtr == NULL) {
                continue;
            }
            infoPtr = (RbcFreqInfo *) Tcl_GetHashValue(hPtr);
            infoPtr->sum += yArr[i];
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcResetStacks --
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
RbcResetStacks(
    RbcGraph * graphPtr)
{
    register RbcFreqInfo *infoPtr, *endPtr;

    for (infoPtr = graphPtr->freqArr,
        endPtr = graphPtr->freqArr + graphPtr->nStacks;
        infoPtr < endPtr; infoPtr++) {
        infoPtr->lastY = 0.0;
        infoPtr->count = 0;
    }
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
