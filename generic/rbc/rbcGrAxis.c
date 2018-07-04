/*
 * rbcGrAxis.c --
 *
 *      This module implements coordinate axes for the rbc graph widget.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

#define AXIS_CONFIG_MAJOR (1<<4)        /* User specified major tick intervals. */
#define AXIS_CONFIG_MINOR (1<<5)        /* User specified minor tick intervals. */
#define AXIS_ONSCREEN	  (1<<6)        /* Axis is displayed on the screen via
                                         * the "use" operation */
#define AXIS_DIRTY	  (1<<7)
#define AXIS_ALLOW_NULL   (1<<12)

#define DEF_NUM_TICKS		10      /* Each major tick is 10% */
#define STATIC_TICK_SPACE	10

#define TICK_LABEL_SIZE		200
#define MAXTICKS		10001

/*
 * Round x in terms of units
 */
#define UROUND(x,u)		(Round((x)/(u))*(u))
#define UCEIL(x,u)		(ceil((x)/(u))*(u))
#define UFLOOR(x,u)		(floor((x)/(u))*(u))

#define LENGTH_MAJOR_TICK 	0.030   /* Length of a major tick */
#define LENGTH_MINOR_TICK 	0.015   /* Length of a minor (sub)tick */
#define LENGTH_LABEL_TICK 	0.040   /* Distance from graph to start of the
                                         * label */
#define NUMDIGITS		15      /* Specifies the number of
                                         * digits of accuracy used when
                                         * outputting axis tick labels. */
#define AVG_TICK_NUM_CHARS	16      /* Assumed average tick label size */

#define TICK_RANGE_TIGHT	0
#define TICK_RANGE_LOOSE	1
#define TICK_RANGE_ALWAYS_LOOSE	2

#define AXIS_TITLE_PAD		2       /* Padding for axis title. */
#define AXIS_LINE_PAD		1       /* Padding for axis line. */

#define HORIZMARGIN(m)	(!((m)->site & 0x1))    /* Even sites are horizontal */

typedef enum AxisComponents {
    MAJOR_TICK, MINOR_TICK, TICK_LABEL, AXIS_LINE
} AxisComponent;

/*
 * TickLabel --
 *
 * 	Structure containing the X-Y screen coordinates of the tick
 * 	label (anchored at its center).
 */
typedef struct {
    RbcPoint2D      anchorPos;
    int             width, height;
    char            string[1];
} TickLabel;

typedef struct {
    int             axis;       /* Length of the axis.  */
    int             t1;         /* Length of a major tick (in pixels). */
    int             t2;         /* Length of a minor tick (in pixels). */
    int             label;      /* Distance from axis to tick label.  */
} AxisInfo;

extern Tk_CustomOption rbcDistanceOption;
extern Tk_CustomOption rbcPositiveDistanceOption;
extern Tk_CustomOption rbcShadowOption;
extern Tk_CustomOption rbcListOption;

static Tk_OptionParseProc StringToLimit;
static Tk_OptionPrintProc LimitToString;
static Tk_OptionParseProc StringToTicks;
static Tk_OptionPrintProc TicksToString;
static Tk_OptionParseProc StringToAxis;
static Tk_OptionPrintProc AxisToString;
static Tk_OptionParseProc StringToAnyAxis;
static Tk_OptionParseProc StringToFormat;
static Tk_OptionPrintProc FormatToString;
static Tk_OptionParseProc StringToLoose;
static Tk_OptionPrintProc LooseToString;

static Tk_CustomOption limitOption = {
    StringToLimit, LimitToString, (ClientData) 0
};

static Tk_CustomOption majorTicksOption = {
    StringToTicks, TicksToString, (ClientData) AXIS_CONFIG_MAJOR,
};

static Tk_CustomOption minorTicksOption = {
    StringToTicks, TicksToString, (ClientData) AXIS_CONFIG_MINOR,
};

Tk_CustomOption rbcXAxisOption = {
    StringToAxis, AxisToString, (ClientData) & rbcXAxisUid
};

Tk_CustomOption rbcYAxisOption = {
    StringToAxis, AxisToString, (ClientData) & rbcYAxisUid
};

Tk_CustomOption rbcAnyXAxisOption = {
    StringToAnyAxis, AxisToString, (ClientData) & rbcXAxisUid
};

Tk_CustomOption rbcAnyYAxisOption = {
    StringToAnyAxis, AxisToString, (ClientData) & rbcYAxisUid
};

static Tk_CustomOption formatOption = {
    StringToFormat, FormatToString, (ClientData) 0,
};

static Tk_CustomOption looseOption = {
    StringToLoose, LooseToString, (ClientData) 0,
};

/* Axis flags: */

#define DEF_AXIS_COMMAND            (char *)NULL
#define DEF_AXIS_DESCENDING         "no"
#define DEF_AXIS_FOREGROUND         "black"
#define DEF_AXIS_FG_MONO            "black"
#define DEF_AXIS_HIDE               "no"
#define DEF_AXIS_JUSTIFY            "center"
#define DEF_AXIS_LIMITS_FORMAT      (char *)NULL
#define DEF_AXIS_LINE_WIDTH         "1"
#define DEF_AXIS_LOGSCALE           "no"
#define DEF_AXIS_LOOSE              "no"
#define DEF_AXIS_RANGE              "0.0"
#define DEF_AXIS_ROTATE             "0.0"
#define DEF_AXIS_SCROLL_INCREMENT   "10"
#define DEF_AXIS_SHIFTBY            "0.0"
#define DEF_AXIS_SHOWTICKS          "yes"
#define DEF_AXIS_STEP               "0.0"
#define DEF_AXIS_STEP               "0.0"
#define DEF_AXIS_SUBDIVISIONS       "2"
#define DEF_AXIS_TAGS               "all"
#define DEF_AXIS_TICKS              "0"
#define DEF_AXIS_TICK_FONT          RBC_FONT_SMALL
#define DEF_AXIS_TICK_LENGTH        "8"
#define DEF_AXIS_TITLE_ALTERNATE    "0"
#define DEF_AXIS_TITLE_FG           "black"
#define DEF_AXIS_TITLE_FONT         RBC_FONT
#define DEF_AXIS_X_STEP_BARCHART    "1.0"
#define DEF_AXIS_X_SUBDIVISIONS_BARCHART    "0"
#define DEF_AXIS_BACKGROUND         (char *)NULL
#define DEF_AXIS_BORDERWIDTH        "0"
#define DEF_AXIS_RELIEF             "flat"

static Tk_ConfigSpec configSpecs[] = {
    {TK_CONFIG_DOUBLE, "-autorange", "autoRange", "AutoRange", DEF_AXIS_RANGE,
            Tk_Offset(RbcAxis, windowSize),
        RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BORDER, "-background", "background", "Background",
            DEF_AXIS_BACKGROUND, Tk_Offset(RbcAxis, border),
        RBC_ALL_GRAPHS | TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-bg", "background", (char *) NULL, (char *) NULL, 0,
        0},
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags", DEF_AXIS_TAGS,
            Tk_Offset(RbcAxis, tags), RBC_ALL_GRAPHS | TK_CONFIG_NULL_OK,
        &rbcListOption},
    {TK_CONFIG_SYNONYM, "-bd", "borderWidth", (char *) NULL, (char *) NULL, 0,
        RBC_ALL_GRAPHS},
    {TK_CONFIG_CUSTOM, "-borderwidth", "borderWidth", "BorderWidth",
            DEF_AXIS_BORDERWIDTH, Tk_Offset(RbcAxis, borderWidth),
        RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_AXIS_FOREGROUND,
            Tk_Offset(RbcAxis, tickTextStyle.color),
        TK_CONFIG_COLOR_ONLY | RBC_ALL_GRAPHS},
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_AXIS_FG_MONO,
            Tk_Offset(RbcAxis, tickTextStyle.color),
        TK_CONFIG_MONO_ONLY | RBC_ALL_GRAPHS},
    {TK_CONFIG_STRING, "-command", "command", "Command", DEF_AXIS_COMMAND,
        Tk_Offset(RbcAxis, formatCmd), TK_CONFIG_NULL_OK | RBC_ALL_GRAPHS},
    {TK_CONFIG_BOOLEAN, "-descending", "descending", "Descending",
            DEF_AXIS_DESCENDING, Tk_Offset(RbcAxis, descending),
        RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_AXIS_HIDE,
            Tk_Offset(RbcAxis, hidden),
        RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_JUSTIFY, "-justify", "justify", "Justify", DEF_AXIS_JUSTIFY,
            Tk_Offset(RbcAxis, titleTextStyle.justify),
        RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-labeloffset", "labelOffset", "LabelOffset",
        (char *) NULL, Tk_Offset(RbcAxis, labelOffset), RBC_ALL_GRAPHS},
    {TK_CONFIG_COLOR, "-limitscolor", "limitsColor", "Color",
            DEF_AXIS_FOREGROUND, Tk_Offset(RbcAxis, limitsTextStyle.color),
        TK_CONFIG_COLOR_ONLY | RBC_ALL_GRAPHS},
    {TK_CONFIG_COLOR, "-limitscolor", "limitsColor", "Color", DEF_AXIS_FG_MONO,
            Tk_Offset(RbcAxis, limitsTextStyle.color),
        TK_CONFIG_MONO_ONLY | RBC_ALL_GRAPHS},
    {TK_CONFIG_FONT, "-limitsfont", "limitsFont", "Font", DEF_AXIS_TICK_FONT,
        Tk_Offset(RbcAxis, limitsTextStyle.font), RBC_ALL_GRAPHS},
    {TK_CONFIG_CUSTOM, "-limitsformat", "limitsFormat", "LimitsFormat",
            (char *) NULL, Tk_Offset(RbcAxis, limitsFormats),
        TK_CONFIG_NULL_OK | RBC_ALL_GRAPHS, &formatOption},
    {TK_CONFIG_CUSTOM, "-limitsshadow", "limitsShadow", "Shadow", (char *) NULL,
            Tk_Offset(RbcAxis, limitsTextStyle.shadow),
        TK_CONFIG_COLOR_ONLY | RBC_ALL_GRAPHS, &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-limitsshadow", "limitsShadow", "Shadow", (char *) NULL,
            Tk_Offset(RbcAxis, limitsTextStyle.shadow),
        TK_CONFIG_MONO_ONLY | RBC_ALL_GRAPHS, &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-linewidth", "lineWidth", "LineWidth",
            DEF_AXIS_LINE_WIDTH, Tk_Offset(RbcAxis, lineWidth),
        RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_BOOLEAN, "-logscale", "logScale", "LogScale", DEF_AXIS_LOGSCALE,
            Tk_Offset(RbcAxis, logScale),
        RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-loose", "loose", "Loose", DEF_AXIS_LOOSE, 0,
        RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT, &looseOption},
    {TK_CONFIG_CUSTOM, "-majorticks", "majorTicks", "MajorTicks", (char *) NULL,
            Tk_Offset(RbcAxis, t1Ptr), TK_CONFIG_NULL_OK | RBC_ALL_GRAPHS,
        &majorTicksOption},
    {TK_CONFIG_CUSTOM, "-max", "max", "Max", (char *) NULL, Tk_Offset(RbcAxis,
            reqMax), TK_CONFIG_NULL_OK | RBC_ALL_GRAPHS, &limitOption},
    {TK_CONFIG_CUSTOM, "-min", "min", "Min", (char *) NULL, Tk_Offset(RbcAxis,
            reqMin), TK_CONFIG_NULL_OK | RBC_ALL_GRAPHS, &limitOption},
    {TK_CONFIG_CUSTOM, "-minorticks", "minorTicks", "MinorTicks", (char *) NULL,
            Tk_Offset(RbcAxis, t2Ptr), TK_CONFIG_NULL_OK | RBC_ALL_GRAPHS,
        &minorTicksOption},
    {TK_CONFIG_RELIEF, "-relief", "relief", "Relief", DEF_AXIS_RELIEF,
            Tk_Offset(RbcAxis, relief),
        RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_DOUBLE, "-rotate", "rotate", "Rotate", DEF_AXIS_ROTATE,
            Tk_Offset(RbcAxis, tickTextStyle.theta),
        RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_STRING, "-scrollcommand", "scrollCommand", "ScrollCommand",
            (char *) NULL, Tk_Offset(RbcAxis, scrollCmdPrefix),
        RBC_ALL_GRAPHS | TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-scrollincrement", "scrollIncrement", "ScrollIncrement",
            DEF_AXIS_SCROLL_INCREMENT, Tk_Offset(RbcAxis, scrollUnits),
            RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT,
        &rbcPositiveDistanceOption},
    {TK_CONFIG_CUSTOM, "-scrollmax", "scrollMax", "ScrollMax", (char *) NULL,
            Tk_Offset(RbcAxis, scrollMax), TK_CONFIG_NULL_OK | RBC_ALL_GRAPHS,
        &limitOption},
    {TK_CONFIG_CUSTOM, "-scrollmin", "scrollMin", "ScrollMin", (char *) NULL,
            Tk_Offset(RbcAxis, scrollMin), TK_CONFIG_NULL_OK | RBC_ALL_GRAPHS,
        &limitOption},
    {TK_CONFIG_DOUBLE, "-shiftby", "shiftBy", "ShiftBy", DEF_AXIS_SHIFTBY,
            Tk_Offset(RbcAxis, shiftBy),
        RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-showticks", "showTicks", "ShowTicks",
            DEF_AXIS_SHOWTICKS, Tk_Offset(RbcAxis, showTicks),
        RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_DOUBLE, "-stepsize", "stepSize", "StepSize", DEF_AXIS_STEP,
            Tk_Offset(RbcAxis, reqStep),
        RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_DOUBLE, "-tickdivider", "tickDivider", "TickDivider",
            DEF_AXIS_STEP, Tk_Offset(RbcAxis, tickZoom),
        RBC_ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_INT, "-subdivisions", "subdivisions", "Subdivisions",
            DEF_AXIS_SUBDIVISIONS, Tk_Offset(RbcAxis, reqNumMinorTicks),
        RBC_ALL_GRAPHS},
    {TK_CONFIG_FONT, "-tickfont", "tickFont", "Font", DEF_AXIS_TICK_FONT,
        Tk_Offset(RbcAxis, tickTextStyle.font), RBC_ALL_GRAPHS},
    {TK_CONFIG_PIXELS, "-ticklength", "tickLength", "TickLength",
            DEF_AXIS_TICK_LENGTH, Tk_Offset(RbcAxis, tickLength),
        RBC_ALL_GRAPHS},
    {TK_CONFIG_CUSTOM, "-tickshadow", "tickShadow", "Shadow", (char *) NULL,
            Tk_Offset(RbcAxis, tickTextStyle.shadow),
        TK_CONFIG_COLOR_ONLY | RBC_ALL_GRAPHS, &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-tickshadow", "tickShadow", "Shadow", (char *) NULL,
            Tk_Offset(RbcAxis, tickTextStyle.shadow),
        TK_CONFIG_MONO_ONLY | RBC_ALL_GRAPHS, &rbcShadowOption},
    {TK_CONFIG_STRING, "-title", "title", "Title", (char *) NULL,
            Tk_Offset(RbcAxis, title),
        TK_CONFIG_DONT_SET_DEFAULT | TK_CONFIG_NULL_OK | RBC_ALL_GRAPHS},
    {TK_CONFIG_BOOLEAN, "-titlealternate", "titleAlternate", "TitleAlternate",
            DEF_AXIS_TITLE_ALTERNATE, Tk_Offset(RbcAxis, titleAlternate),
        TK_CONFIG_DONT_SET_DEFAULT | RBC_ALL_GRAPHS},
    {TK_CONFIG_COLOR, "-titlecolor", "titleColor", "Color", DEF_AXIS_FOREGROUND,
            Tk_Offset(RbcAxis, titleTextStyle.color),
        TK_CONFIG_COLOR_ONLY | RBC_ALL_GRAPHS},
    {TK_CONFIG_COLOR, "-titlecolor", "titleColor", "TitleColor",
            DEF_AXIS_FG_MONO, Tk_Offset(RbcAxis, titleTextStyle.color),
        TK_CONFIG_MONO_ONLY | RBC_ALL_GRAPHS},
    {TK_CONFIG_FONT, "-titlefont", "titleFont", "Font", DEF_AXIS_TITLE_FONT,
        Tk_Offset(RbcAxis, titleTextStyle.font), RBC_ALL_GRAPHS},
    {TK_CONFIG_CUSTOM, "-titleshadow", "titleShadow", "Shadow", (char *) NULL,
            Tk_Offset(RbcAxis, titleTextStyle.shadow),
        TK_CONFIG_COLOR_ONLY | RBC_ALL_GRAPHS, &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-titleshadow", "titleShadow", "Shadow", (char *) NULL,
            Tk_Offset(RbcAxis, titleTextStyle.shadow),
        TK_CONFIG_MONO_ONLY | RBC_ALL_GRAPHS, &rbcShadowOption},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

/* Rotation for each axis title */
static double   titleRotate[4] = {
    0.0, 90.0, 0.0, 270.0
};

/* Forward declarations */
static int      Round(
    register double x);
static void     SetAxisRange(
    RbcAxisRange * rangePtr,
    double min,
    double max);
static int      InRange(
    register double x,
    RbcAxisRange * rangePtr);
static int      AxisIsHorizontal(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr);
static void     FreeLabels(
    RbcChain * chainPtr);
static TickLabel *MakeLabel(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double value);
static void     GetDataLimits(
    RbcAxis * axisPtr,
    double min,
    double max);
static void     FixAxisRange(
    RbcAxis * axisPtr);
static double   NiceNum(
    double x,
    int round);
static RbcTicks *GenerateTicks(
    RbcTickSweep * sweepPtr);
static void     LogScaleAxis(
    RbcAxis * axisPtr,
    double min,
    double max);
static void     LinearScaleAxis(
    RbcAxis * axisPtr,
    double min,
    double max);
static void     SweepTicks(
    RbcAxis * axisPtr);
static void     ResetTextStyles(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr);
static void     DestroyAxis(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr);
static void     AxisOffsets(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int margin,
    int axisOffset,
    AxisInfo * infoPtr);
static void     MakeAxisLine(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int line,
    RbcSegment2D * segPtr);
static void     MakeTick(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double value,
    int tick,
    int line,
    RbcSegment2D * segPtr);
static void     MapAxis(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int offset,
    int margin);
static double   AdjustViewport(
    double offset,
    double windowSize);
static int      GetAxisScrollInfo(
    Tcl_Interp * interp,
    int argc,
    const char **argv,
    double *offsetPtr,
    double windowSize,
    double scrollUnits);
static void     DrawAxis(
    RbcGraph * graphPtr,
    Drawable drawable,
    RbcAxis * axisPtr);
static void     AxisToPostScript(
    RbcPsToken * psToken,
    RbcAxis * axisPtr);
static void     MakeGridLine(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double value,
    RbcSegment2D * segPtr);
static void     GetAxisGeometry(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr);
static int      GetMarginGeometry(
    RbcGraph * graphPtr,
    RbcMargin * marginPtr);
static void     ComputeMargins(
    RbcGraph * graphPtr);
static RbcAxis *CreateAxis(
    RbcGraph * graphPtr,
    const char *name,
    int margin);
static int      ConfigureAxis(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr);
static int      NameToAxis(
    RbcGraph * graphPtr,
    const char *name,
    RbcAxis ** axisPtrPtr);
static int      GetAxis(
    RbcGraph * graphPtr,
    const char *name,
    RbcUid classUid,
    RbcAxis ** axisPtrPtr);
static void     FreeAxis(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr);

static int      BindOp(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int argc,
    const char **argv);
static int      CgetOp(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int argc,
    const char *argv[]);
static int      ConfigureOp(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int argc,
    const char *argv[]);
static int      GetOp(
    RbcGraph * graphPtr,
    int argc,
    const char *argv[]);
static int      LimitsOp(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int argc,
    const char **argv);
static int      InvTransformOp(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int argc,
    const char **argv);
static int      TransformOp(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int argc,
    const char **argv);
static int      UseOp(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int argc,
    const char **argv);
static int      CreateVirtualOp(
    RbcGraph * graphPtr,
    int argc,
    const char **argv);
static int      BindVirtualOp(
    RbcGraph * graphPtr,
    int argc,
    const char **argv);
static int      CgetVirtualOp(
    RbcGraph * graphPtr,
    int argc,
    const char **argv);
static int      ConfigureVirtualOp(
    RbcGraph * graphPtr,
    int argc,
    const char **argv);
static int      DeleteVirtualOp(
    RbcGraph * graphPtr,
    int argc,
    const char **argv);
static int      InvTransformVirtualOp(
    RbcGraph * graphPtr,
    int argc,
    const char **argv);
static int      LimitsVirtualOp(
    RbcGraph * graphPtr,
    int argc,
    const char **argv);
static int      NamesVirtualOp(
    RbcGraph * graphPtr,
    int argc,
    const char **argv);
static int      TransformVirtualOp(
    RbcGraph * graphPtr,
    int argc,
    const char **argv);
static int      ViewOp(
    RbcGraph * graphPtr,
    int argc,
    const char **argv);

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
 *----------------------------------------------------------------------
 *
 * SetAxisRange --
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
SetAxisRange(
    RbcAxisRange * rangePtr,
    double min,
    double max)
{
    rangePtr->min = min;
    rangePtr->max = max;
    rangePtr->range = max - min;
    if (FABS(rangePtr->range) < DBL_EPSILON) {
        rangePtr->range = 1.0;
    }
    rangePtr->scale = 1.0 / rangePtr->range;
}

/*
 * ----------------------------------------------------------------------
 *
 * InRange --
 *
 *      Determines if a value lies within a given range.
 *
 *      The value is normalized and compared against the interval
 *      [0..1], where 0.0 is the minimum and 1.0 is the maximum.
 *      DBL_EPSILON is the smallest number that can be represented
 *      on the host machine, such that (1.0 + epsilon) != 1.0.
 *
 *      Please note, *max* can't equal *min*.
 *
 * Results:
 *      If the value is within the interval [min..max], 1 is
 *      returned; 0 otherwise.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
InRange(
    register double x,
    RbcAxisRange * rangePtr)
{
    if (rangePtr->range < DBL_EPSILON) {
        return (FABS(rangePtr->max - x) >= DBL_EPSILON);
    } else {
        double          norm;

        norm = (x - rangePtr->min) * rangePtr->scale;
        return ((norm >= -DBL_EPSILON) && ((norm - 1.0) < DBL_EPSILON));
    }
}

static int
AxisIsHorizontal(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr)
{
    return ((axisPtr->classUid == rbcYAxisUid) == graphPtr->inverted);
}

/* ----------------------------------------------------------------------
 * Custom option parse and print procedures
 * ----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * StringToAnyAxis --
 *
 *      Converts the name of an axis to a pointer to its axis structure.
 *
 * Results:
 *      The return value is a standard Tcl result.  The axis flags are
 *      written into the widget record.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToAnyAxis(
    ClientData clientData,      /* Class identifier of the type of
                                 * axis we are looking for. */
    Tcl_Interp * interp,        /* Interpreter to send results back to. */
    Tk_Window tkwin,            /* Used to look up pointer to graph. */
    const char *string,         /* String representing new value. */
    char *widgRec,              /* Pointer to structure record. */
    int offset)
{                               /* Offset of field in structure. */
    RbcAxis       **axisPtrPtr = (RbcAxis **) (widgRec + offset);
    RbcUid          classUid = *(RbcUid *) clientData;
    RbcGraph       *graphPtr;
    RbcAxis        *axisPtr;

    graphPtr = RbcGetGraphFromWindowData(tkwin);
    if (*axisPtrPtr != NULL) {
        FreeAxis(graphPtr, *axisPtrPtr);
    }
    if (string[0] == '\0') {
        axisPtr = NULL;
    } else if (GetAxis(graphPtr, string, classUid, &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    *axisPtrPtr = axisPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToAxis --
 *
 *      Converts the name of an axis to a pointer to its axis structure.
 *
 * Results:
 *      The return value is a standard Tcl result.  The axis flags are
 *      written into the widget record.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToAxis(
    ClientData clientData,      /* Class identifier of the type of
                                 * axis we are looking for. */
    Tcl_Interp * interp,        /* Interpreter to send results back to. */
    Tk_Window tkwin,            /* Used to look up pointer to graph. */
    const char *string,         /* String representing new value. */
    char *widgRec,              /* Pointer to structure record. */
    int offset)
{                               /* Offset of field in structure. */
    RbcAxis       **axisPtrPtr = (RbcAxis **) (widgRec + offset);
    RbcUid          classUid = *(RbcUid *) clientData;
    RbcGraph       *graphPtr;

    graphPtr = RbcGetGraphFromWindowData(tkwin);
    if (*axisPtrPtr != NULL) {
        FreeAxis(graphPtr, *axisPtrPtr);
    }
    if (GetAxis(graphPtr, string, classUid, axisPtrPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * AxisToString --
 *
 *      Convert the window coordinates into a string.
 *
 * Results:
 *      The string representing the coordinate position is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *----------------------------------------------------------------------
 */
static const char *
AxisToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Pointer to structure record . */
    int offset,                 /* Offset of field in structure. */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Not used. */
    RbcAxis        *axisPtr = *(RbcAxis **) (widgRec + offset);

    if (axisPtr == NULL) {
        return "";
    }
    return axisPtr->name;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToFormat --
 *
 *      Convert the name of virtual axis to an pointer.
 *
 * Results:
 *      The return value is a standard Tcl result.  The axis flags are
 *      written into the widget record.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToFormat(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,        /* Interpreter to send results back to. */
    Tk_Window tkwin,            /* Used to look up pointer to graph */
    const char *string,         /* String representing new value. */
    char *widgRec,              /* Pointer to structure record. */
    int offset)
{                               /* Offset of field in structure. */
    RbcAxis        *axisPtr = (RbcAxis *) (widgRec);
    const char    **argv;
    int             argc;

    if (axisPtr->limitsFormats != NULL) {
        ckfree((char *) axisPtr->limitsFormats);
    }
    axisPtr->limitsFormats = NULL;
    axisPtr->nFormats = 0;

    if ((string == NULL) || (*string == '\0')) {
        return TCL_OK;
    }
    if (Tcl_SplitList(interp, string, &argc, &argv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (argc > 2) {
        Tcl_AppendResult(interp, "too many elements in limits format list \"",
            string, "\"", (char *) NULL);
        ckfree((char *) argv);  /*TODO check really? */
        return TCL_ERROR;
    }
    axisPtr->limitsFormats = argv;
    axisPtr->nFormats = argc;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * FormatToString --
 *
 *      Convert the window coordinates into a string.
 *
 * Results:
 *      The string representing the coordinate position is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
FormatToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Widget record */
    int offset,                 /* offset of limits field */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Not used. */
    RbcAxis        *axisPtr = (RbcAxis *) (widgRec);

    if (axisPtr->nFormats == 0) {
        return "";
    }
    *freeProcPtr = (Tcl_FreeProc *) Tcl_Free;
    return Tcl_Merge(axisPtr->nFormats, axisPtr->limitsFormats);
}

/*
 * ----------------------------------------------------------------------
 *
 * StringToLimit --
 *
 *      Convert the string representation of an axis limit into its numeric
 *      form.
 *
 * Results:
 *      The return value is a standard Tcl result.  The symbol type is
 *      written into the widget record.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
StringToLimit(
    ClientData clientData,      /* Either AXIS_CONFIG_MIN or AXIS_CONFIG_MAX.
                                 * Indicates which axis limit to set. */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Not used. */
    const char *string,         /* String representing new value. */
    char *widgRec,              /* Pointer to structure record. */
    int offset)
{                               /* Offset of field in structure. */
    double         *limitPtr = (double *) (widgRec + offset);

    if ((string == NULL) || (*string == '\0')) {
        *limitPtr = rbcNaN;
    } else if (Tcl_ExprDouble(interp, string, limitPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * LimitToString --
 *
 *      Convert the floating point axis limits into a string.
 *
 * Results:
 *      The string representation of the limits is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static const char *
LimitToString(
    ClientData clientData,      /* Either LMIN or LMAX */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,
    int offset,
    Tcl_FreeProc ** freeProcPtr)
{
    double          limit = *(double *) (widgRec + offset);
    const char     *result;

    result = "";
    if (!TclIsNaN(limit)) {
        char            string[TCL_DOUBLE_SPACE + 1];
        RbcGraph       *graphPtr;

        graphPtr = RbcGetGraphFromWindowData(tkwin);
        Tcl_PrintDouble(graphPtr->interp, limit, string);
        result = RbcStrdup(string);
        if (result == NULL) {
            return "";
        }
        *freeProcPtr = (Tcl_FreeProc *) Tcl_Free;
    }
    return result;
}

/*
 * ----------------------------------------------------------------------
 *
 * StringToTicks --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
StringToTicks(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Not used. */
    const char *string,         /* String representing new value. */
    char *widgRec,              /* Pointer to structure record. */
    int offset)
{                               /* Offset of field in structure. */
    unsigned int    mask = (unsigned int) clientData;
    RbcAxis        *axisPtr = (RbcAxis *) widgRec;
    RbcTicks      **ticksPtrPtr = (RbcTicks **) (widgRec + offset);
    int             nTicks;
    RbcTicks       *ticksPtr;

    nTicks = 0;
    ticksPtr = NULL;
    if ((string != NULL) && (*string != '\0')) {
        int             nExprs;
        const char    **exprArr;

        if (Tcl_SplitList(interp, string, &nExprs, &exprArr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (nExprs > 0) {
            register int    i;
            int             result = TCL_ERROR;
            double          value;

            ticksPtr =
                (RbcTicks *) ckalloc(sizeof(RbcTicks) +
                (nExprs * sizeof(double)));
            assert(ticksPtr);
            for (i = 0; i < nExprs; i++) {
                result = Tcl_ExprDouble(interp, exprArr[i], &value);
                if (result != TCL_OK) {
                    break;
                }
                ticksPtr->values[i] = value;
            }
            ckfree((char *) exprArr);
            if (result != TCL_OK) {
                ckfree((char *) ticksPtr);
                return TCL_ERROR;
            }
            nTicks = nExprs;
        }
    }
    axisPtr->flags &= ~mask;
    if (ticksPtr != NULL) {
        axisPtr->flags |= mask;
        ticksPtr->nTicks = nTicks;
    }
    if (*ticksPtrPtr != NULL) {
        ckfree((char *) *ticksPtrPtr);
    }
    *ticksPtrPtr = ticksPtr;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * TicksToString --
 *
 *      Convert array of tick coordinates to a list.
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
TicksToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,
    int offset,
    Tcl_FreeProc ** freeProcPtr)
{
    RbcTicks       *ticksPtr = *(RbcTicks **) (widgRec + offset);
    char            string[TCL_DOUBLE_SPACE + 1];
    register int    i;
    char           *result;
    Tcl_DString     dString;
    RbcGraph       *graphPtr;

    if (ticksPtr == NULL) {
        return "";
    }
    Tcl_DStringInit(&dString);
    graphPtr = RbcGetGraphFromWindowData(tkwin);
    for (i = 0; i < ticksPtr->nTicks; i++) {
        Tcl_PrintDouble(graphPtr->interp, ticksPtr->values[i], string);
        Tcl_DStringAppendElement(&dString, string);
    }
    *freeProcPtr = (Tcl_FreeProc *) Tcl_Free;
    result = RbcStrdup(Tcl_DStringValue(&dString));
    Tcl_DStringFree(&dString);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToLoose --
 *
 *      Convert a string to one of three values.
 *      	0 - false, no, off
 *      	1 - true, yes, on
 *      	2 - always
 *
 * Results:
 *      If the string is successfully converted, TCL_OK is returned.
 *      Otherwise, TCL_ERROR is returned and an error message is left in
 *      interpreter's result field.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToLoose(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Not used. */
    const char *string,         /* String representing new value. */
    char *widgRec,              /* Pointer to structure record. */
    int offset)
{                               /* Offset of field in structure. */
    RbcAxis        *axisPtr = (RbcAxis *) (widgRec);
    register int    i;
    int             argc;
    const char    **argv;
    int             values[2];

    if (Tcl_SplitList(interp, string, &argc, &argv) != TCL_OK) {
        return TCL_ERROR;
    }
    if ((argc < 1) || (argc > 2)) {
        Tcl_AppendResult(interp, "wrong # elements in loose value \"",
            string, "\"", (char *) NULL);
        return TCL_ERROR;
    }
    for (i = 0; i < argc; i++) {
        if ((argv[i][0] == 'a') && (strcmp(argv[i], "always") == 0)) {
            values[i] = TICK_RANGE_ALWAYS_LOOSE;
        } else {
            int             bool;

            if (Tcl_GetBoolean(interp, argv[i], &bool) != TCL_OK) {
                ckfree((char *) argv);
                return TCL_ERROR;
            }
            values[i] = bool;
        }
    }
    axisPtr->looseMin = axisPtr->looseMax = values[0];
    if (argc > 1) {
        axisPtr->looseMax = values[1];
    }
    ckfree((char *) argv);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * LooseToString --
 *
 *      TODO: Description
 *
 * Results:
 *      The string representation of the auto boolean is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
LooseToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Widget record */
    int offset,                 /* offset of flags field in record */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Memory deallocation scheme to use */
    RbcAxis        *axisPtr = (RbcAxis *) widgRec;
    Tcl_DString     dString;
    char           *result;

    Tcl_DStringInit(&dString);
    if (axisPtr->looseMin == TICK_RANGE_TIGHT) {
        Tcl_DStringAppendElement(&dString, "0");
    } else if (axisPtr->looseMin == TICK_RANGE_LOOSE) {
        Tcl_DStringAppendElement(&dString, "1");
    } else if (axisPtr->looseMin == TICK_RANGE_ALWAYS_LOOSE) {
        Tcl_DStringAppendElement(&dString, "always");
    }
    if (axisPtr->looseMin != axisPtr->looseMax) {
        if (axisPtr->looseMax == TICK_RANGE_TIGHT) {
            Tcl_DStringAppendElement(&dString, "0");
        } else if (axisPtr->looseMax == TICK_RANGE_LOOSE) {
            Tcl_DStringAppendElement(&dString, "1");
        } else if (axisPtr->looseMax == TICK_RANGE_ALWAYS_LOOSE) {
            Tcl_DStringAppendElement(&dString, "always");
        }
    }
    result = RbcStrdup(Tcl_DStringValue(&dString));
    Tcl_DStringFree(&dString);
    *freeProcPtr = (Tcl_FreeProc *) Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeLabels --
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
FreeLabels(
    RbcChain * chainPtr)
{
    RbcChainLink   *linkPtr;
    TickLabel      *labelPtr;

    for (linkPtr = RbcChainFirstLink(chainPtr); linkPtr != NULL;
        linkPtr = RbcChainNextLink(linkPtr)) {
        labelPtr = RbcChainGetValue(linkPtr);
        ckfree((char *) labelPtr);
    }
    RbcChainReset(chainPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * MakeLabel --
 *
 *      Converts a floating point tick value to a string to be used as its
 *      label.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Returns a new label in the string character buffer.  The formatted
 *      tick label will be displayed on the graph.
 *
 * ----------------------------------------------------------------------
 */
static TickLabel *
MakeLabel(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,          /* Axis structure */
    double value)
{                               /* Value to be convert to a decimal string */
    char            string[TICK_LABEL_SIZE + 1];
    TickLabel      *labelPtr;

    /* Generate a default tick label based upon the tick value.  */
    if (axisPtr->logScale) {
        sprintf(string, "1E%d", ROUND(value));
    } else {
        sprintf(string, "%.*g", NUMDIGITS, value);
    }

    if (axisPtr->formatCmd != NULL) {
        Tcl_Interp     *interp = graphPtr->interp;
        Tk_Window       tkwin = graphPtr->tkwin;

        /*
         * A Tcl proc was designated to format tick labels. Append the path
         * name of the widget and the default tick label as arguments when
         * invoking it. Copy and save the new label from interp->result.
         */
        Tcl_ResetResult(interp);
        if (Tcl_VarEval(interp, axisPtr->formatCmd, " ", Tk_PathName(tkwin),
                " ", string, (char *) NULL) != TCL_OK) {
            Tcl_BackgroundError(interp);
        } else {
            /*
             * The proc could return a string of any length, so arbitrarily
             * limit it to what will fit in the return string.
             */
            strncpy(string, Tcl_GetStringResult(interp), TICK_LABEL_SIZE);
            string[TICK_LABEL_SIZE] = '\0';

            Tcl_ResetResult(interp);    /* Clear the interpreter's result. */
        }
    }
    labelPtr = (TickLabel *) ckalloc(sizeof(TickLabel) + strlen(string));
    assert(labelPtr);
    strcpy(labelPtr->string, string);
    labelPtr->anchorPos.x = labelPtr->anchorPos.y = DBL_MAX;
    return labelPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcInvHMap --
 *
 *      Maps the given screen coordinate back to a graph coordinate.
 *      Called by the graph locater routine.
 *
 * Results:
 *      Returns the graph coordinate value at the given window
 *      y-coordinate.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
double
RbcInvHMap(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double x)
{
    double          value;

    x = (double) (x - graphPtr->hOffset) * graphPtr->hScale;
    if (axisPtr->descending) {
        x = 1.0 - x;
    }
    value = (x * axisPtr->axisRange.range) + axisPtr->axisRange.min;
    if (axisPtr->logScale) {
        value = EXP10(value);
    }
    return value;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcInvVMap --
 *
 *      Maps the given window y-coordinate back to a graph coordinate
 *      value. Called by the graph locater routine.
 *
 * Results:
 *      Returns the graph coordinate value at the given window
 *      y-coordinate.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
double
RbcInvVMap(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double y)
{
    double          value;

    y = (double) (y - graphPtr->vOffset) * graphPtr->vScale;
    if (axisPtr->descending) {
        y = 1.0 - y;
    }
    value = ((1.0 - y) * axisPtr->axisRange.range) + axisPtr->axisRange.min;
    if (axisPtr->logScale) {
        value = EXP10(value);
    }
    return value;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcHMap --
 *
 *      Map the given graph coordinate value to its axis, returning a window
 *      position.
 *
 * Results:
 *      Returns a double precision number representing the window coordinate
 *      position on the given axis.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
double
RbcHMap(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double x)
{
    if ((axisPtr->logScale) && (x != 0.0)) {
        x = log10(FABS(x));
    }
    /* Map graph coordinate to normalized coordinates [0..1] */
    x = (x - axisPtr->axisRange.min) * axisPtr->axisRange.scale;
    if (axisPtr->descending) {
        x = 1.0 - x;
    }
    return (x * graphPtr->hRange + graphPtr->hOffset);
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcVMap --
 *
 *      Map the given graph coordinate value to its axis, returning a window
 *      position.
 *
 * Results:
 *      Returns a double precision number representing the window coordinate
 *      position on the given axis.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
double
RbcVMap(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double y)
{
    if ((axisPtr->logScale) && (y != 0.0)) {
        y = log10(FABS(y));
    }
    /* Map graph coordinate to normalized coordinates [0..1] */
    y = (y - axisPtr->axisRange.min) * axisPtr->axisRange.scale;
    if (axisPtr->descending) {
        y = 1.0 - y;
    }
    return (((1.0 - y) * graphPtr->vRange) + graphPtr->vOffset);
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcMap2D --
 *
 *      Maps the given graph x,y coordinate values to a window position.
 *
 * Results:
 *      Returns a XPoint structure containing the window coordinates of
 *      the given graph x,y coordinate.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
RbcPoint2D
RbcMap2D(
    RbcGraph * graphPtr,
    double x,                   /* Graph x coordinate */
    double y,                   /* Graph y coordinate */
    RbcAxis2D * axesPtr)
{                               /* Specifies which axes to use */
    RbcPoint2D      point;

    if (graphPtr->inverted) {
        point.x = RbcHMap(graphPtr, axesPtr->y, y);
        point.y = RbcVMap(graphPtr, axesPtr->x, x);
    } else {
        point.x = RbcHMap(graphPtr, axesPtr->x, x);
        point.y = RbcVMap(graphPtr, axesPtr->y, y);
    }
    return point;
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcInvMap2D --
 *
 *      Maps the given window x,y coordinates to graph values.
 *
 * Results:
 *      Returns a structure containing the graph coordinates of
 *      the given window x,y coordinate.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
RbcPoint2D
RbcInvMap2D(
    RbcGraph * graphPtr,
    double x,                   /* Window x coordinate */
    double y,                   /* Window y coordinate */
    RbcAxis2D * axesPtr)
{                               /* Specifies which axes to use */
    RbcPoint2D      point;

    if (graphPtr->inverted) {
        point.x = RbcInvVMap(graphPtr, axesPtr->x, y);
        point.y = RbcInvHMap(graphPtr, axesPtr->y, x);
    } else {
        point.x = RbcInvHMap(graphPtr, axesPtr->x, x);
        point.y = RbcInvVMap(graphPtr, axesPtr->y, y);
    }
    return point;
}

/*
 *----------------------------------------------------------------------
 *
 * GetDataLimits --
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
GetDataLimits(
    RbcAxis * axisPtr,
    double min,
    double max)
{
    if (axisPtr->valueRange.min > min) {
        axisPtr->valueRange.min = min;
    }
    if (axisPtr->valueRange.max < max) {
        axisPtr->valueRange.max = max;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FixAxisRange --
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
FixAxisRange(
    RbcAxis * axisPtr)
{
    double          min, max;
    /*
     * When auto-scaling, the axis limits are the bounds of the element
     * data.  If no data exists, set arbitrary limits (wrt to log/linear
     * scale).
     */
    min = axisPtr->valueRange.min;
    max = axisPtr->valueRange.max;

    if (min == DBL_MAX) {
        if (!TclIsNaN(axisPtr->reqMin)) {
            min = axisPtr->reqMin;
        } else {
            min = (axisPtr->logScale) ? 0.001 : 0.0;
        }
    }
    if (max == -DBL_MAX) {
        if (!TclIsNaN(axisPtr->reqMax)) {
            max = axisPtr->reqMax;
        } else {
            max = 1.0;
        }
    }
    if (min >= max) {
        double          value;

        /*
         * There is no range of data (i.e. min is not less than max),
         * so manufacture one.
         */
        value = min;
        if (value == 0.0) {
            min = -0.1, max = 0.1;
        } else {
            double          x;

            x = FABS(value) * 0.1;
            min = value - x, max = value + x;
        }
    }
    SetAxisRange(&axisPtr->valueRange, min, max);

    /*
     * The axis limits are either the current data range or overridden
     * by the values selected by the user with the -min or -max
     * options.
     */
    axisPtr->min = min;
    axisPtr->max = max;
    if (!TclIsNaN(axisPtr->reqMin)) {
        axisPtr->min = axisPtr->reqMin;
    }
    if (!TclIsNaN(axisPtr->reqMax)) {
        axisPtr->max = axisPtr->reqMax;
    }

    if (axisPtr->max < axisPtr->min) {

        /*
         * If the limits still don't make sense, it's because one
         * limit configuration option (-min or -max) was set and the
         * other default (based upon the data) is too small or large.
         * Remedy this by making up a new min or max from the
         * user-defined limit.
         */

        if (TclIsNaN(axisPtr->reqMin)) {
            axisPtr->min = axisPtr->max - (FABS(axisPtr->max) * 0.1);
        }
        if (TclIsNaN(axisPtr->reqMax)) {
            axisPtr->max = axisPtr->min + (FABS(axisPtr->max) * 0.1);
        }
    }
    /*
     * If a window size is defined, handle auto ranging by shifting
     * the axis limits.
     */
    if ((axisPtr->windowSize > 0.0) &&
        (TclIsNaN(axisPtr->reqMin)) && (TclIsNaN(axisPtr->reqMax))) {
        if (axisPtr->shiftBy < 0.0) {
            axisPtr->shiftBy = 0.0;
        }
        max = axisPtr->min + axisPtr->windowSize;
        if (axisPtr->max >= max) {
            if (axisPtr->shiftBy > 0.0) {
                max = UCEIL(axisPtr->max, axisPtr->shiftBy);
            }
            axisPtr->min = max - axisPtr->windowSize;
        }
        axisPtr->max = max;
    }
    if ((axisPtr->max != axisPtr->prevMax) ||
        (axisPtr->min != axisPtr->prevMin)) {
        /* Indicate if the axis limits have changed */
        axisPtr->flags |= AXIS_DIRTY;
        /* and save the previous minimum and maximum values */
        axisPtr->prevMin = axisPtr->min;
        axisPtr->prevMax = axisPtr->max;
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * NiceNum --
 *
 *      Reference: Paul Heckbert, "Nice Numbers for Graph Labels",
 *             Graphics Gems, pp 61-63.
 *
 *      Finds a "nice" number approximately equal to x.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static double
NiceNum(
    double x,
    int round)
{                               /* If non-zero, round. Otherwise take ceiling
                                 * of value. */
    double          expt;       /* Exponent of x */
    double          frac;       /* Fractional part of x */
    double          nice;       /* Nice, rounded fraction */

    expt = floor(log10(x));
    frac = x / EXP10(expt);     /* between 1 and 10 */
    if (round) {
        if (frac < 1.5) {
            nice = 1.0;
        } else if (frac < 3.0) {
            nice = 2.0;
        } else if (frac < 7.0) {
            nice = 5.0;
        } else {
            nice = 10.0;
        }
    } else {
        if (frac <= 1.0) {
            nice = 1.0;
        } else if (frac <= 2.0) {
            nice = 2.0;
        } else if (frac <= 5.0) {
            nice = 5.0;
        } else {
            nice = 10.0;
        }
    }
    return nice * EXP10(expt);
}

/*
 *----------------------------------------------------------------------
 *
 * GenerateTicks --
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
static RbcTicks *
GenerateTicks(
    RbcTickSweep * sweepPtr)
{
    RbcTicks       *ticksPtr;
    register int    i;

    ticksPtr =
        (RbcTicks *) ckalloc(sizeof(RbcTicks) +
        (sweepPtr->nSteps * sizeof(double)));
    assert(ticksPtr);

    if (sweepPtr->step == 0.0) {
        static double   logTable[] = {  /* Precomputed log10 values [1..10] */
            0.0,
            0.301029995663981, 0.477121254719662,
            0.602059991327962, 0.698970004336019,
            0.778151250383644, 0.845098040014257,
            0.903089986991944, 0.954242509439325,
            1.0
        };
        /* Hack: A zero step indicates to use log values. */
        for (i = 0; i < sweepPtr->nSteps; i++) {
            ticksPtr->values[i] = logTable[i];
        }
    } else {
        double          value;

        value = sweepPtr->initial;      /* Start from smallest axis tick */
        for (i = 0; i < sweepPtr->nSteps; i++) {
            value = UROUND(value, sweepPtr->step);
            ticksPtr->values[i] = value;
            value += sweepPtr->step;
        }
    }
    ticksPtr->nTicks = sweepPtr->nSteps;
    return ticksPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * LogScaleAxis --
 *
 *      Determine the range and units of a log scaled axis.
 *
 *      Unless the axis limits are specified, the axis is scaled
 *      automatically, where the smallest and largest major ticks encompass
 *      the range of actual data values.  When an axis limit is specified,
 *      that value represents the smallest(min)/largest(max) value in the
 *      displayed range of values.
 *
 *      Both manual and automatic scaling are affected by the step used.  By
 *      default, the step is the largest power of ten to divide the range in
 *      more than one piece.
 *
 *      Automatic scaling:
 *      Find the smallest number of units which contain the range of values.
 *      The minimum and maximum major tick values will be represent the
 *      range of values for the axis. This greatest number of major ticks
 *      possible is 10.
 *
 *      Manual scaling:
 *          Make the minimum and maximum data values the represent the range of
 *          the values for the axis.  The minimum and maximum major ticks will be
 *          inclusive of this range.  This provides the largest area for plotting
 *          and the expected results when the axis min and max values have be set
 *          by the user (.e.g zooming).  The maximum number of major ticks is 20.
 *
 *          For log scale, there's the possibility that the minimum and
 *          maximum data values are the same magnitude.  To represent the
 *          points properly, at least one full decade should be shown.
 *          However, if you zoom a log scale plot, the results should be
 *          predictable. Therefore, in that case, show only minor ticks.
 *          Lastly, there should be an appropriate way to handle numbers
 *          <=0.
 *
 *              maxY
 *                |    units = magnitude (of least significant digit)
 *                |    high  = largest unit tick < max axis value
 *          high _|    low   = smallest unit tick > min axis value
 *                |
 *                |    range = high - low
 *                |    # ticks = greatest factor of range/units
 *               _|
 *            U   |
 *            n   |
 *            i   |
 *            t  _|
 *                |
 *                |
 *                |
 *           low _|
 *                |
 *                |_minX________________maxX__
 *                |   |       |      |       |
 *         minY  low                        high
 *               minY
 *
 *
 *      numTicks = Number of ticks
 *      min = Minimum value of axis
 *      max = Maximum value of axis
 *      range    = Range of values (max - min)
 *
 *      If the number of decades is greater than ten, it is assumed
 *      that the full set of log-style ticks can't be drawn properly.
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ---------------------------------------------------------------------- */
static void
LogScaleAxis(
    RbcAxis * axisPtr,
    double min,
    double max)
{
    double          range;
    double          tickMin, tickMax;
    double          majorStep, minorStep;
    int             nMajor, nMinor;

    min = (min != 0.0) ? log10(FABS(min)) : 0.0;
    max = (max != 0.0) ? log10(FABS(max)) : 1.0;

    tickMin = floor(min);
    tickMax = ceil(max);
    range = tickMax - tickMin;

    if (range > 10) {
        /* There are too many decades to display a major tick at every
         * decade.  Instead, treat the axis as a linear scale.  */
        range = NiceNum(range, 0);
        majorStep = NiceNum(range / DEF_NUM_TICKS, 1);
        tickMin = UFLOOR(tickMin, majorStep);
        tickMax = UCEIL(tickMax, majorStep);
        nMajor = (int) ((tickMax - tickMin) / majorStep) + 1;
        minorStep = EXP10(floor(log10(majorStep)));
        if (minorStep == majorStep) {
            nMinor = 4, minorStep = 0.2;
        } else {
            nMinor = Round(majorStep / minorStep) - 1;
        }
    } else {
        if (tickMin == tickMax) {
            tickMax++;
        }
        majorStep = 1.0;
        nMajor = (int) (tickMax - tickMin + 1); /* FIXME: Check this. */

        minorStep = 0.0;        /* This is a special hack to pass
                                 * information to the GenerateTicks
                                 * routine. An interval of 0.0 tells
                                 *      1) this is a minor sweep and
                                 *      2) the axis is log scale.
                                 */
        nMinor = 10;
    }
    if ((axisPtr->looseMin == TICK_RANGE_TIGHT) ||
        ((axisPtr->looseMin == TICK_RANGE_LOOSE) &&
            (!TclIsNaN(axisPtr->reqMin)))) {
        tickMin = min;
        nMajor++;
    }
    if ((axisPtr->looseMax == TICK_RANGE_TIGHT) ||
        ((axisPtr->looseMax == TICK_RANGE_LOOSE) &&
            (!TclIsNaN(axisPtr->reqMax)))) {
        tickMax = max;
    }
    axisPtr->majorSweep.step = majorStep;
    axisPtr->majorSweep.initial = floor(tickMin);
    axisPtr->majorSweep.nSteps = nMajor;
    axisPtr->minorSweep.initial = axisPtr->minorSweep.step = minorStep;
    axisPtr->minorSweep.nSteps = nMinor;

    SetAxisRange(&axisPtr->axisRange, tickMin, tickMax);
}

/*
 * ----------------------------------------------------------------------
 *
 * LinearScaleAxis --
 *
 *      Determine the units of a linear scaled axis.
 *
 *      The axis limits are either the range of the data values mapped
 *      to the axis (autoscaled), or the values specified by the -min
 *      and -max options (manual).
 *
 *      If autoscaled, the smallest and largest major ticks will
 *      encompass the range of data values.  If the -loose option is
 *      selected, the next outer ticks are choosen.  If tight, the
 *      ticks are at or inside of the data limits are used.
 *
 *      If manually set, the ticks are at or inside the data limits
 *      are used.  This makes sense for zooming.  You want the
 *      selected range to represent the next limit, not something a
 *      bit bigger.
 *
 *      Note: I added an "always" value to the -loose option to force
 *            the manually selected axes to be loose. It's probably
 *            not a good idea.
 *
 *              maxY
 *                |    units = magnitude (of least significant digit)
 *                |    high  = largest unit tick < max axis value
 *          high _|    low   = smallest unit tick > min axis value
 *                |
 *                |    range = high - low
 *                |    # ticks = greatest factor of range/units
 *               _|
 *            U   |
 *            n   |
 *            i   |
 *            t  _|
 *                |
 *                |
 *                |
 *           low _|
 *                |
 *                |_minX________________maxX__
 *                |   |       |      |       |
 *         minY  low                        high
 *               minY
 *
 *      numTicks = Number of ticks
 *      min = Minimum value of axis
 *      max = Maximum value of axis
 *      range    = Range of values (max - min)
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The axis tick information is set.  The actual tick values will
 *      be generated later.
 *
 * ----------------------------------------------------------------------
 */
static void
LinearScaleAxis(
    RbcAxis * axisPtr,
    double min,
    double max)
{
    double          range, step;
    double          tickMin, tickMax;
    double          axisMin, axisMax;
    int             nTicks;

    range = max - min;

    /* Calculate the major tick stepping. */
    if (axisPtr->reqStep > 0.0) {
        /* An interval was designated by the user.  Keep scaling it
         * until it fits comfortably within the current range of the
         * axis.  */
        step = axisPtr->reqStep;
        while ((2 * step) >= range) {
            step *= 0.5;
        }
    } else {
        range = NiceNum(range, 0);
        step = NiceNum(range / DEF_NUM_TICKS, 1);
    }

    /* Find the outer tick values. Add 0.0 to prevent getting -0.0. */
    axisMin = tickMin = floor(min / step) * step + 0.0;
    axisMax = tickMax = ceil(max / step) * step + 0.0;

    nTicks = Round((tickMax - tickMin) / step) + 1;
    axisPtr->majorSweep.step = step;
    axisPtr->majorSweep.initial = tickMin;
    axisPtr->majorSweep.nSteps = nTicks;

    /*
     * The limits of the axis are either the range of the data
     * ("tight") or at the next outer tick interval ("loose").  The
     * looseness or tightness has to do with how the axis fits the
     * range of data values.  This option is overridden when
     * the user sets an axis limit (by either -min or -max option).
     * The axis limit is always at the selected limit (otherwise we
     * assume that user would have picked a different number).
     */
    if ((axisPtr->looseMin == TICK_RANGE_TIGHT) ||
        ((axisPtr->looseMin == TICK_RANGE_LOOSE) &&
            (!TclIsNaN(axisPtr->reqMin)))) {
        axisMin = min;
    }
    if ((axisPtr->looseMax == TICK_RANGE_TIGHT) ||
        ((axisPtr->looseMax == TICK_RANGE_LOOSE) &&
            (!TclIsNaN(axisPtr->reqMax)))) {
        axisMax = max;
    }
    SetAxisRange(&axisPtr->axisRange, axisMin, axisMax);

    /* Now calculate the minor tick step and number. */

    if ((axisPtr->reqNumMinorTicks > 0) &&
        ((axisPtr->flags & AXIS_CONFIG_MAJOR) == 0)) {
        nTicks = axisPtr->reqNumMinorTicks - 1;
        step = 1.0 / (nTicks + 1);
    } else {
        nTicks = 0;             /* No minor ticks. */
        step = 0.5;             /* Don't set the minor tick interval
                                 * to 0.0. It makes the GenerateTicks
                                 * routine create minor log-scale tick
                                 * marks.  */
    }
    axisPtr->minorSweep.initial = axisPtr->minorSweep.step = step;
    axisPtr->minorSweep.nSteps = nTicks;
}

/*
 *----------------------------------------------------------------------
 *
 * SweepTicks --
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
SweepTicks(
    RbcAxis * axisPtr)
{
    if ((axisPtr->flags & AXIS_CONFIG_MAJOR) == 0) {
        if (axisPtr->t1Ptr != NULL) {
            ckfree((char *) axisPtr->t1Ptr);
        }
        axisPtr->t1Ptr = GenerateTicks(&axisPtr->majorSweep);
    }
    if ((axisPtr->flags & AXIS_CONFIG_MINOR) == 0) {
        if (axisPtr->t2Ptr != NULL) {
            ckfree((char *) axisPtr->t2Ptr);
        }
        axisPtr->t2Ptr = GenerateTicks(&axisPtr->minorSweep);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcResetAxes --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
void
RbcResetAxes(
    RbcGraph * graphPtr)
{
    RbcChainLink   *linkPtr;
    RbcElement     *elemPtr;
    RbcAxis        *axisPtr;
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  cursor;
    RbcExtents2D    exts;
    double          min, max;

    /* FIXME: This should be called whenever the display list of
     *        elements change. Maybe yet another flag INIT_STACKS to
     *        indicate that the element display list has changed.
     *        Needs to be done before the axis limits are set.
     */
    RbcInitFreqTable(graphPtr);
    if ((graphPtr->mode == MODE_STACKED) && (graphPtr->nStacks > 0)) {
        RbcComputeStacks(graphPtr);
    }
    /*
     * Step 1:  Reset all axes. Initialize the data limits of the axis to
     *          impossible values.
     */
    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (RbcAxis *) Tcl_GetHashValue(hPtr);
        axisPtr->min = axisPtr->valueRange.min = DBL_MAX;
        axisPtr->max = axisPtr->valueRange.max = -DBL_MAX;
    }

    /*
     * Step 2:  For each element that's to be displayed, get the smallest
     *          and largest data values mapped to each X and Y-axis.  This
     *          will be the axis limits if the user doesn't override them
     *          with -min and -max options.
     */
    for (linkPtr = RbcChainFirstLink(graphPtr->elements.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        elemPtr = RbcChainGetValue(linkPtr);
        (*elemPtr->procsPtr->extentsProc) (elemPtr, &exts);
        GetDataLimits(elemPtr->axes.x, exts.left, exts.right);
        GetDataLimits(elemPtr->axes.y, exts.top, exts.bottom);
    }
    /*
     * Step 3:  Now that we know the range of data values for each axis,
     *          set axis limits and compute a sweep to generate tick values.
     */
    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (RbcAxis *) Tcl_GetHashValue(hPtr);
        FixAxisRange(axisPtr);

        /* Calculate min/max tick (major/minor) layouts */
        min = axisPtr->min;
        max = axisPtr->max;
        if ((!TclIsNaN(axisPtr->scrollMin)) && (min < axisPtr->scrollMin)) {
            min = axisPtr->scrollMin;
        }
        if ((!TclIsNaN(axisPtr->scrollMax)) && (max > axisPtr->scrollMax)) {
            max = axisPtr->scrollMax;
        }
        if (axisPtr->logScale) {
            LogScaleAxis(axisPtr, min, max);
        } else {
            LinearScaleAxis(axisPtr, min, max);
        }

        if ((axisPtr->flags & (AXIS_DIRTY | AXIS_ONSCREEN)) ==
            (AXIS_DIRTY | AXIS_ONSCREEN)) {
            graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
        }
    }

    graphPtr->flags &= ~RBC_RESET_AXES;

    /*
     * When any axis changes, we need to layout the entire graph.
     */
    graphPtr->flags |= (RBC_GET_AXIS_GEOMETRY | RBC_LAYOUT_NEEDED |
        RBC_MAP_ALL | RBC_REDRAW_WORLD);
}

/*
 * ----------------------------------------------------------------------
 *
 * ResetTextStyles --
 *
 *      Configures axis attributes (font, line width, label, etc) and
 *      allocates a new (possibly shared) graphics context.  Line cap
 *      style is projecting.  This is for the problem of when a tick
 *      sits directly at the end point of the axis.
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      Axis resources are allocated (GC, font). Axis layout is
 *      deferred until the height and width of the window are known.
 *
 * ----------------------------------------------------------------------
 */
static void
ResetTextStyles(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr)
{
    GC              newGC;
    XGCValues       gcValues;
    unsigned long   gcMask;

    RbcResetTextStyle(graphPtr->tkwin, &axisPtr->titleTextStyle);
    RbcResetTextStyle(graphPtr->tkwin, &axisPtr->tickTextStyle);
    RbcResetTextStyle(graphPtr->tkwin, &axisPtr->limitsTextStyle);

    gcMask = (GCForeground | GCLineWidth | GCCapStyle);
    gcValues.foreground = axisPtr->tickTextStyle.color->pixel;
    gcValues.line_width = RbcLineWidth(axisPtr->lineWidth);
    gcValues.cap_style = CapProjecting;

    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (axisPtr->tickGC != NULL) {
        Tk_FreeGC(graphPtr->display, axisPtr->tickGC);
    }
    axisPtr->tickGC = newGC;
}

/*
 * ----------------------------------------------------------------------
 *
 * DestroyAxis --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Resources (font, color, gc, labels, etc.) associated with the
 *      axis are deallocated.
 *
 * ----------------------------------------------------------------------
 */
static void
DestroyAxis(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr)
{
    int             flags;

    flags = RbcGraphType(graphPtr);
    Tk_FreeOptions(configSpecs, (char *) axisPtr, graphPtr->display, flags);
    if (graphPtr->bindTable != NULL) {
        RbcDeleteBindings(graphPtr->bindTable, axisPtr);
    }
    if (axisPtr->linkPtr != NULL) {
        RbcChainDeleteLink(axisPtr->chainPtr, axisPtr->linkPtr);
    }
    if (axisPtr->name != NULL) {
        ckfree((char *) axisPtr->name);
    }
    if (axisPtr->hashPtr != NULL) {
        Tcl_DeleteHashEntry(axisPtr->hashPtr);
    }
    RbcFreeTextStyle(graphPtr->display, &axisPtr->titleTextStyle);
    RbcFreeTextStyle(graphPtr->display, &axisPtr->limitsTextStyle);
    RbcFreeTextStyle(graphPtr->display, &axisPtr->tickTextStyle);

    if (axisPtr->tickGC != NULL) {
        Tk_FreeGC(graphPtr->display, axisPtr->tickGC);
    }
    if (axisPtr->t1Ptr != NULL) {
        ckfree((char *) axisPtr->t1Ptr);
    }
    if (axisPtr->t2Ptr != NULL) {
        ckfree((char *) axisPtr->t2Ptr);
    }
    if (axisPtr->limitsFormats != NULL) {
        ckfree((char *) axisPtr->limitsFormats);
    }
    FreeLabels(axisPtr->tickLabels);
    RbcChainDestroy(axisPtr->tickLabels);
    if (axisPtr->segments != NULL) {
        ckfree((char *) axisPtr->segments);
    }
    if (axisPtr->tags != NULL) {
        ckfree((char *) axisPtr->tags);
    }
    ckfree((char *) axisPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * AxisOffsets --
 *
 *      Determines the sites of the axis, major and minor ticks,
 *      and title of the axis.
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
AxisOffsets(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int margin,
    int axisOffset,
    AxisInfo * infoPtr)
{
    int             pad;        /* Offset of axis from interior region. This
                                 * includes a possible border and the axis
                                 * line width. */
    int             p;
    int             majorOffset, minorOffset, labelOffset;
    int             offset;
    int             x, y;

    axisPtr->titleTextStyle.theta = titleRotate[margin];

    majorOffset = minorOffset = 0;
    labelOffset = AXIS_TITLE_PAD;
    if (axisPtr->lineWidth > 0) {
        majorOffset = ABS(axisPtr->tickLength);
        minorOffset = 10 * majorOffset / 15;
        labelOffset = majorOffset + AXIS_TITLE_PAD + axisPtr->lineWidth / 2;
    }
    /* Adjust offset for the interior border width and the line width */
    pad = axisPtr->lineWidth + 1;
    if (graphPtr->plotBorderWidth > 0) {
        pad += graphPtr->plotBorderWidth + 1;
    }
    offset = axisOffset + 1 + pad;
    if ((margin == RBC_MARGIN_LEFT) || (margin == RBC_MARGIN_TOP)) {
        majorOffset = -majorOffset;
        minorOffset = -minorOffset;
        labelOffset = -labelOffset;
    }
    /*
     * Pre-calculate the x-coordinate positions of the axis, tick labels, and
     * the individual major and minor ticks.
     */
    p = 0;                      /* Suppress compiler warning */

    switch (margin) {
    case RBC_MARGIN_TOP:
        p = graphPtr->top - axisOffset - pad;
        if (axisPtr->titleAlternate) {
            x = graphPtr->right + AXIS_TITLE_PAD;
            y = graphPtr->top - axisOffset - (axisPtr->height / 2);
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_W;
        } else {
            x = (graphPtr->right + graphPtr->left) / 2;
            y = graphPtr->top - axisOffset - axisPtr->height - AXIS_TITLE_PAD;
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_N;
        }
        axisPtr->tickTextStyle.anchor = TK_ANCHOR_S;
        offset = axisPtr->borderWidth + axisPtr->lineWidth / 2;
        axisPtr->region.left = graphPtr->hOffset - offset - 2;
        axisPtr->region.right = graphPtr->hOffset + graphPtr->hRange +
            offset - 1;
        axisPtr->region.top = p + labelOffset - 1;
        axisPtr->region.bottom = p;
        axisPtr->titlePos.x = x;
        axisPtr->titlePos.y = y;
        break;

    case RBC_MARGIN_BOTTOM:
        p = graphPtr->bottom + axisOffset + pad;
        if (axisPtr->titleAlternate) {
            x = graphPtr->right + AXIS_TITLE_PAD;
            y = graphPtr->bottom + axisOffset + (axisPtr->height / 2);
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_W;
        } else {
            x = (graphPtr->right + graphPtr->left) / 2;
            y = graphPtr->bottom + axisOffset + axisPtr->height +
                AXIS_TITLE_PAD;
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_S;
        }
        axisPtr->tickTextStyle.anchor = TK_ANCHOR_N;
        offset = axisPtr->borderWidth + axisPtr->lineWidth / 2;
        axisPtr->region.left = graphPtr->hOffset - offset - 2;
        axisPtr->region.right = graphPtr->hOffset + graphPtr->hRange +
            offset - 1;

        axisPtr->region.top = graphPtr->bottom + axisOffset +
            axisPtr->lineWidth - axisPtr->lineWidth / 2;
        axisPtr->region.bottom = graphPtr->bottom + axisOffset +
            axisPtr->lineWidth + labelOffset + 1;
        axisPtr->titlePos.x = x;
        axisPtr->titlePos.y = y;
        break;

    case RBC_MARGIN_LEFT:
        p = graphPtr->left - axisOffset - pad;
        if (axisPtr->titleAlternate) {
            x = graphPtr->left - axisOffset - (axisPtr->width / 2);
            y = graphPtr->top - AXIS_TITLE_PAD;
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_SW;
        } else {
            x = graphPtr->left - axisOffset - axisPtr->width -
                graphPtr->plotBorderWidth;
            y = (graphPtr->bottom + graphPtr->top) / 2;
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_W;
        }
        axisPtr->tickTextStyle.anchor = TK_ANCHOR_E;
        axisPtr->region.left = graphPtr->left - offset + labelOffset - 1;
        axisPtr->region.right = graphPtr->left - offset + 2;

        offset = axisPtr->borderWidth + axisPtr->lineWidth / 2;
        axisPtr->region.top = graphPtr->vOffset - offset - 2;
        axisPtr->region.bottom = graphPtr->vOffset + graphPtr->vRange +
            offset - 1;
        axisPtr->titlePos.x = x;
        axisPtr->titlePos.y = y;
        break;

    case RBC_MARGIN_RIGHT:
        p = graphPtr->right + axisOffset + pad;
        if (axisPtr->titleAlternate) {
            x = graphPtr->right + axisOffset + (axisPtr->width / 2);
            y = graphPtr->top - AXIS_TITLE_PAD;
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_SE;
        } else {
            x = graphPtr->right + axisOffset + axisPtr->width + AXIS_TITLE_PAD;
            y = (graphPtr->bottom + graphPtr->top) / 2;
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_E;
        }
        axisPtr->tickTextStyle.anchor = TK_ANCHOR_W;

        axisPtr->region.left = graphPtr->right + axisOffset +
            axisPtr->lineWidth - axisPtr->lineWidth / 2;
        axisPtr->region.right = graphPtr->right + axisOffset +
            labelOffset + axisPtr->lineWidth + 1;

        offset = axisPtr->borderWidth + axisPtr->lineWidth / 2;
        axisPtr->region.top = graphPtr->vOffset - offset - 2;
        axisPtr->region.bottom = graphPtr->vOffset + graphPtr->vRange +
            offset - 1;
        axisPtr->titlePos.x = x;
        axisPtr->titlePos.y = y;
        break;

    case RBC_MARGIN_NONE:
        break;
    }
    infoPtr->axis = p - (axisPtr->lineWidth / 2);
    infoPtr->t1 = p + majorOffset;
    infoPtr->t2 = p + minorOffset;
    infoPtr->label = p + labelOffset;

    if (axisPtr->tickLength < 0) {
        int             hold;

        hold = infoPtr->t1;
        infoPtr->t1 = infoPtr->axis;
        infoPtr->axis = hold;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MakeAxisLine --
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
MakeAxisLine(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,          /* Axis information */
    int line,
    RbcSegment2D * segPtr)
{
    double          min, max;

    min = axisPtr->axisRange.min;
    max = axisPtr->axisRange.max;
    if (axisPtr->logScale) {
        min = EXP10(min);
        max = EXP10(max);
    }
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        segPtr->p.x = RbcHMap(graphPtr, axisPtr, min);
        segPtr->q.x = RbcHMap(graphPtr, axisPtr, max);
        segPtr->p.y = segPtr->q.y = line;
    } else {
        segPtr->q.x = segPtr->p.x = line;
        segPtr->p.y = RbcVMap(graphPtr, axisPtr, min);
        segPtr->q.y = RbcVMap(graphPtr, axisPtr, max);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MakeTick --
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
MakeTick(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double value,
    int tick,                   /* Lengths of tick. */
    int line,                   /* Lengths of axis line. */
    RbcSegment2D * segPtr)
{
    if (axisPtr->logScale) {
        value = EXP10(value);
    }
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        segPtr->p.x = segPtr->q.x = RbcHMap(graphPtr, axisPtr, value);
        segPtr->p.y = line;
        segPtr->q.y = tick;
    } else {
        segPtr->p.x = line;
        segPtr->p.y = segPtr->q.y = RbcVMap(graphPtr, axisPtr, value);
        segPtr->q.x = tick;
    }
}

/*
 * -----------------------------------------------------------------
 *
 * MapAxis --
 *
 *      Pre-calculates positions of the axis, ticks, and labels (to be
 *      used later when displaying the axis).  Calculates the values
 *      for each major and minor tick and checks to see if they are in
 *      range (the outer ticks may be outside of the range of plotted
 *      values).
 *
 *      Line segments for the minor and major ticks are saved into one
 *      XSegment array so that they can be drawn by a single
 *      XDrawSegments call. The positions of the tick labels are also
 *      computed and saved.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Line segments and tick labels are saved and used later to draw
 *      the axis.
 *
 * -----------------------------------------------------------------
 */
static void
MapAxis(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int offset,
    int margin)
{
    int             arraySize;
    int             nMajorTicks, nMinorTicks;
    AxisInfo        info;
    RbcSegment2D   *segments;
    RbcSegment2D   *segPtr;

    AxisOffsets(graphPtr, axisPtr, margin, offset, &info);

    /* Save all line coordinates in an array of line segments. */

    if (axisPtr->segments != NULL) {
        ckfree((char *) axisPtr->segments);
    }
    nMajorTicks = nMinorTicks = 0;
    if (axisPtr->t1Ptr != NULL) {
        nMajorTicks = axisPtr->t1Ptr->nTicks;
    }
    if (axisPtr->t2Ptr != NULL) {
        nMinorTicks = axisPtr->t2Ptr->nTicks;
    }
    arraySize = 1 + (nMajorTicks * (nMinorTicks + 1));
    segments = (RbcSegment2D *) ckalloc(arraySize * sizeof(RbcSegment2D));
    assert(segments);

    segPtr = segments;
    if (axisPtr->lineWidth > 0) {
        /* Axis baseline */
        MakeAxisLine(graphPtr, axisPtr, info.axis, segPtr);
        segPtr++;
    }
    if (axisPtr->showTicks) {
        double          t1, t2;
        double          labelPos;
        register int    i, j;
        int             isHoriz;
        TickLabel      *labelPtr;
        RbcChainLink   *linkPtr;
        RbcSegment2D    seg;

        isHoriz = AxisIsHorizontal(graphPtr, axisPtr);
        for (i = 0; i < axisPtr->t1Ptr->nTicks; i++) {
            t1 = axisPtr->t1Ptr->values[i];
            /* Minor ticks */
            for (j = 0; j < axisPtr->t2Ptr->nTicks; j++) {
                t2 = t1 +
                    (axisPtr->majorSweep.step * axisPtr->t2Ptr->values[j]);
                if (InRange(t2, &axisPtr->axisRange)) {
                    MakeTick(graphPtr, axisPtr, t2, info.t2, info.axis, segPtr);
                    segPtr++;
                }
            }
            if (!InRange(t1, &axisPtr->axisRange)) {
                continue;
            }
            /* Major tick */
            MakeTick(graphPtr, axisPtr, t1, info.t1, info.axis, segPtr);
            segPtr++;
        }

        linkPtr = RbcChainFirstLink(axisPtr->tickLabels);
        labelPos = (double) info.label;

        for (i = 0; i < axisPtr->t1Ptr->nTicks; i++) {
            t1 = axisPtr->t1Ptr->values[i];
            if (axisPtr->labelOffset) {
                t1 += axisPtr->majorSweep.step * 0.5;
            }
            if (!InRange(t1, &axisPtr->axisRange)) {
                continue;
            }
            labelPtr = RbcChainGetValue(linkPtr);
            linkPtr = RbcChainNextLink(linkPtr);
            MakeTick(graphPtr, axisPtr, t1, info.t1, info.axis, &seg);
            /* Save tick label X-Y position. */
            if (isHoriz) {
                labelPtr->anchorPos.x = seg.p.x;
                labelPtr->anchorPos.y = labelPos;
            } else {
                labelPtr->anchorPos.x = labelPos;
                labelPtr->anchorPos.y = seg.p.y;
            }
        }
    }
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        axisPtr->width = graphPtr->right - graphPtr->left;
    } else {
        axisPtr->height = graphPtr->bottom - graphPtr->top;
    }
    axisPtr->segments = segments;
    axisPtr->nSegments = segPtr - segments;
    assert(axisPtr->nSegments <= arraySize);
}

/*
 *----------------------------------------------------------------------
 *
 * AdjustViewport --
 *
 *      Adjusts the offsets of the viewport according to the scroll mode.
 *      This is to accommodate both "listbox" and "canvas" style scrolling.
 *
 *      "canvas"	The viewport scrolls within the range of world
 *              coordinates.  This way the viewport always displays
 *              a full page of the world.  If the world is smaller
 *              than the viewport, then (bizarrely) the world and
 *              viewport are inverted so that the world moves up
 *              and down within the viewport.
 *
 *      "listbox"	The viewport can scroll beyond the range of world
 *              coordinates.  Every entry can be displayed at the
 *              top of the viewport.  This also means that the
 *              scrollbar thumb weirdly shrinks as the last entry
 *              is scrolled upward.
 *
 * Results:
 *      The corrected offset is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static double
AdjustViewport(
    double offset,
    double windowSize)
{
    /*
     * Canvas-style scrolling allows the world to be scrolled
     * within the window.
     */
    if (windowSize > 1.0) {
        if (windowSize < (1.0 - offset)) {
            offset = 1.0 - windowSize;
        }
        if (offset > 0.0) {
            offset = 0.0;
        }
    } else {
        if ((offset + windowSize) > 1.0) {
            offset = 1.0 - windowSize;
        }
        if (offset < 0.0) {
            offset = 0.0;
        }
    }
    return offset;
}

/*
 *----------------------------------------------------------------------
 *
 * GetAxisScrollInfo --
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
GetAxisScrollInfo(
    Tcl_Interp * interp,
    int argc,
    const char **argv,
    double *offsetPtr,
    double windowSize,
    double scrollUnits)
{
    char            c;
    unsigned int    length;
    double          offset;
    int             count;
    double          fract;

    offset = *offsetPtr;
    c = argv[0][0];
    length = strlen(argv[0]);
    if ((c == 's') && (strncmp(argv[0], "scroll", length) == 0)) {
        assert(argc == 3);
        /* scroll number unit/page */
        if (Tcl_GetInt(interp, argv[1], &count) != TCL_OK) {
            return TCL_ERROR;
        }
        c = argv[2][0];
        length = strlen(argv[2]);
        if ((c == 'u') && (strncmp(argv[2], "units", length) == 0)) {
            fract = (double) count *scrollUnits;
        } else if ((c == 'p') && (strncmp(argv[2], "pages", length) == 0)) {
            /* A page is 90% of the view-able window. */
            fract = (double) count *windowSize * 0.9;
        } else {
            Tcl_AppendResult(interp, "unknown \"scroll\" units \"", argv[2],
                "\"", (char *) NULL);
            return TCL_ERROR;
        }
        offset += fract;
    } else if ((c == 'm') && (strncmp(argv[0], "moveto", length) == 0)) {
        assert(argc == 2);
        /* moveto fraction */
        if (Tcl_GetDouble(interp, argv[1], &fract) != TCL_OK) {
            return TCL_ERROR;
        }
        offset = fract;
    } else {
        /* Treat like "scroll units" */
        if (Tcl_GetInt(interp, argv[0], &count) != TCL_OK) {
            return TCL_ERROR;
        }
        fract = (double) count *scrollUnits;
        offset += fract;
        /* CHECK THIS: return TCL_OK; */
    }
    *offsetPtr = AdjustViewport(offset, windowSize);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------
 *
 * DrawAxis --
 *
 *      Draws the axis, ticks, and labels onto the canvas.
 *
 *      Initializes and passes text attribute information through
 *      TextStyle structure.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Axis gets drawn on window.
 *
 * -----------------------------------------------------------------
 */
static void
DrawAxis(
    RbcGraph * graphPtr,
    Drawable drawable,
    RbcAxis * axisPtr)
{
    if (axisPtr->border != NULL) {
        Tk_Fill3DRectangle(graphPtr->tkwin, drawable, axisPtr->border,
            axisPtr->region.left + graphPtr->plotBorderWidth,
            axisPtr->region.top + graphPtr->plotBorderWidth,
            axisPtr->region.right - axisPtr->region.left,
            axisPtr->region.bottom - axisPtr->region.top,
            axisPtr->borderWidth, axisPtr->relief);
    }
    if (axisPtr->title != NULL) {
        RbcDrawText(graphPtr->tkwin, drawable, axisPtr->title,
            &axisPtr->titleTextStyle, (int) axisPtr->titlePos.x,
            (int) axisPtr->titlePos.y);
    }
    if (axisPtr->scrollCmdPrefix != NULL) {
        double          viewWidth, viewMin, viewMax;
        double          worldWidth, worldMin, worldMax;
        double          fract;
        int             isHoriz;

        worldMin = axisPtr->valueRange.min;
        worldMax = axisPtr->valueRange.max;
        if (!TclIsNaN(axisPtr->scrollMin)) {
            worldMin = axisPtr->scrollMin;
        }
        if (!TclIsNaN(axisPtr->scrollMax)) {
            worldMax = axisPtr->scrollMax;
        }
        viewMin = axisPtr->min;
        viewMax = axisPtr->max;
        if (viewMin < worldMin) {
            viewMin = worldMin;
        }
        if (viewMax > worldMax) {
            viewMax = worldMax;
        }
        if (axisPtr->logScale) {
            worldMin = log10(worldMin);
            worldMax = log10(worldMax);
            viewMin = log10(viewMin);
            viewMax = log10(viewMax);
        }
        worldWidth = worldMax - worldMin;
        viewWidth = viewMax - viewMin;
        isHoriz = AxisIsHorizontal(graphPtr, axisPtr);

        if (isHoriz != axisPtr->descending) {
            fract = (viewMin - worldMin) / worldWidth;
        } else {
            fract = (worldMax - viewMax) / worldWidth;
        }
        fract = AdjustViewport(fract, viewWidth / worldWidth);

        if (isHoriz != axisPtr->descending) {
            viewMin = (fract * worldWidth);
            axisPtr->min = viewMin + worldMin;
            axisPtr->max = axisPtr->min + viewWidth;
            viewMax = viewMin + viewWidth;
            if (axisPtr->logScale) {
                axisPtr->min = EXP10(axisPtr->min);
                axisPtr->max = EXP10(axisPtr->max);
            }
            RbcUpdateScrollbar(graphPtr->interp, axisPtr->scrollCmdPrefix,
                (viewMin / worldWidth), (viewMax / worldWidth));
        } else {
            viewMax = (fract * worldWidth);
            axisPtr->max = worldMax - viewMax;
            axisPtr->min = axisPtr->max - viewWidth;
            viewMin = viewMax + viewWidth;
            if (axisPtr->logScale) {
                axisPtr->min = EXP10(axisPtr->min);
                axisPtr->max = EXP10(axisPtr->max);
            }
            RbcUpdateScrollbar(graphPtr->interp, axisPtr->scrollCmdPrefix,
                (viewMax / worldWidth), (viewMin / worldWidth));
        }
    }
    if (axisPtr->showTicks) {
        register RbcChainLink *linkPtr;
        TickLabel      *labelPtr;

        for (linkPtr = RbcChainFirstLink(axisPtr->tickLabels); linkPtr != NULL;
            linkPtr = RbcChainNextLink(linkPtr)) {
            /* Draw major tick labels */
            labelPtr = RbcChainGetValue(linkPtr);
            RbcDrawText(graphPtr->tkwin, drawable, labelPtr->string,
                &axisPtr->tickTextStyle, (int) labelPtr->anchorPos.x,
                (int) labelPtr->anchorPos.y);
        }
    }
    {
    FILE *f=fopen("rz","a");
    fprintf(f,"ticks=%d %d %d\n",axisPtr->showTicks,axisPtr->nSegments,axisPtr->lineWidth);
    fclose(f);
    }
    if ((axisPtr->nSegments > 0) && (axisPtr->lineWidth > 0)) {
        /* Draw the tick marks and axis line. */
        RbcDraw2DSegments(graphPtr->display, drawable, axisPtr->tickGC,
            axisPtr->segments, axisPtr->nSegments);
    }
}

/*
 * -----------------------------------------------------------------
 *
 * AxisToPostScript --
 *
 *      Generates PostScript output to draw the axis, ticks, and
 *      labels.
 *
 *      Initializes and passes text attribute information through
 *      TextStyle structure.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      PostScript output is left in graphPtr->interp->result;
 *
 * -----------------------------------------------------------------
 */
static void
AxisToPostScript(
    RbcPsToken * psToken,
    RbcAxis * axisPtr)
{
    if (axisPtr->title != NULL) {
        RbcTextToPostScript(psToken, axisPtr->title, &axisPtr->titleTextStyle,
            axisPtr->titlePos.x, axisPtr->titlePos.y);
    }
    if (axisPtr->showTicks) {
        register RbcChainLink *linkPtr;
        TickLabel      *labelPtr;

        for (linkPtr = RbcChainFirstLink(axisPtr->tickLabels);
            linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
            labelPtr = RbcChainGetValue(linkPtr);
            RbcTextToPostScript(psToken, labelPtr->string,
                &axisPtr->tickTextStyle, labelPtr->anchorPos.x,
                labelPtr->anchorPos.y);
        }
    }
    if ((axisPtr->nSegments > 0) && (axisPtr->lineWidth > 0)) {
        RbcLineAttributesToPostScript(psToken, axisPtr->tickTextStyle.color,
            axisPtr->lineWidth, (RbcDashes *) NULL, CapButt, JoinMiter);
        Rbc2DSegmentsToPostScript(psToken, axisPtr->segments,
            axisPtr->nSegments);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MakeGridLine --
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
MakeGridLine(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double value,
    RbcSegment2D * segPtr)
{
    if (axisPtr->logScale) {
        value = EXP10(value);
    }
    /* Grid lines run orthogonally to the axis */
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        segPtr->p.y = graphPtr->top;
        segPtr->q.y = graphPtr->bottom;
        segPtr->p.x = segPtr->q.x = RbcHMap(graphPtr, axisPtr, value);
    } else {
        segPtr->p.x = graphPtr->left;
        segPtr->q.x = graphPtr->right;
        segPtr->p.y = segPtr->q.y = RbcVMap(graphPtr, axisPtr, value);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcGetAxisSegments --
 *
 *      Assembles the grid lines associated with an axis. Generates
 *      tick positions if necessary (this happens when the axis is
 *      not a logical axis too).
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcGetAxisSegments(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    RbcSegment2D ** segPtrPtr,
    int *nSegmentsPtr)
{
    int             needed;
    RbcTicks       *t1Ptr, *t2Ptr;
    register int    i;
    double          value;
    RbcSegment2D   *segments, *segPtr;

    *nSegmentsPtr = 0;
    *segPtrPtr = NULL;
    if (axisPtr == NULL) {
        return;
    }
    t1Ptr = axisPtr->t1Ptr;
    if (t1Ptr == NULL) {
        t1Ptr = GenerateTicks(&axisPtr->majorSweep);
    }
    t2Ptr = axisPtr->t2Ptr;
    if (t2Ptr == NULL) {
        t2Ptr = GenerateTicks(&axisPtr->minorSweep);
    }

    needed = t1Ptr->nTicks;
    if (graphPtr->gridPtr->minorGrid) {
        needed += (t1Ptr->nTicks * t2Ptr->nTicks);
    }
    if (needed == 0) {
        return;
    }
    segments = (RbcSegment2D *) ckalloc(sizeof(RbcSegment2D) * needed);
    if (segments == NULL) {
        return;                 /* Can't allocate memory for grid. */
    }

    segPtr = segments;
    for (i = 0; i < t1Ptr->nTicks; i++) {
        value = t1Ptr->values[i];
        if (graphPtr->gridPtr->minorGrid) {
            register int    j;
            double          subValue;

            for (j = 0; j < t2Ptr->nTicks; j++) {
                subValue = value +
                    (axisPtr->majorSweep.step * t2Ptr->values[j]);
                if (InRange(subValue, &axisPtr->axisRange)) {
                    MakeGridLine(graphPtr, axisPtr, subValue, segPtr);
                    segPtr++;
                }
            }
        }
        if (InRange(value, &axisPtr->axisRange)) {
            MakeGridLine(graphPtr, axisPtr, value, segPtr);
            segPtr++;
        }
    }

    if (t1Ptr != axisPtr->t1Ptr) {
        ckfree((char *) t1Ptr); /* Free generated ticks. */
    }
    if (t2Ptr != axisPtr->t2Ptr) {
        ckfree((char *) t2Ptr); /* Free generated ticks. */
    }
    *nSegmentsPtr = segPtr - segments;
    assert(*nSegmentsPtr <= needed);
    *segPtrPtr = segments;
}

/*
 *----------------------------------------------------------------------
 *
 * GetAxisGeometry --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void
GetAxisGeometry(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr)
{
    int             height;

    FreeLabels(axisPtr->tickLabels);
    height = 0;
    if (axisPtr->lineWidth > 0) {
        /* Leave room for axis baseline (and pad) */
        height += axisPtr->lineWidth + 2;
    }
    if (axisPtr->showTicks) {
        int             pad;
        register int    i, nLabels;
        int             lw, lh;
        double          x, x2;
        int             maxWidth, maxHeight;
        TickLabel      *labelPtr;

        SweepTicks(axisPtr);

        if (axisPtr->t1Ptr->nTicks < 0) {
            fprintf(stderr, "%s major ticks can't be %d\n",
                axisPtr->name, axisPtr->t1Ptr->nTicks);
            abort();
        }
        if (axisPtr->t1Ptr->nTicks > MAXTICKS) {
            fprintf(stderr, "too big, %s major ticks can't be %d\n",
                axisPtr->name, axisPtr->t1Ptr->nTicks);
            abort();
        }

        maxHeight = maxWidth = 0;
        nLabels = 0;
        for (i = 0; i < axisPtr->t1Ptr->nTicks; i++) {
            x2 = x = axisPtr->t1Ptr->values[i];
            if (axisPtr->labelOffset) {
                x2 += axisPtr->majorSweep.step * 0.5;
            }
            if (!InRange(x2, &axisPtr->axisRange)) {
                continue;
            }
            labelPtr = MakeLabel(graphPtr, axisPtr, x);
            RbcChainAppend(axisPtr->tickLabels, labelPtr);
            nLabels++;
            /*
             * Get the dimensions of each tick label.
             * Remember tick labels can be multi-lined and/or rotated.
             */
            RbcGetTextExtents(&axisPtr->tickTextStyle, labelPtr->string,
                &lw, &lh);
            labelPtr->width = lw;
            labelPtr->height = lh;

            if (axisPtr->tickTextStyle.theta > 0.0) {
                double          rotWidth, rotHeight;

                RbcGetBoundingBox(lw, lh, axisPtr->tickTextStyle.theta,
                    &rotWidth, &rotHeight, (RbcPoint2D *) NULL);
                lw = ROUND(rotWidth);
                lh = ROUND(rotHeight);
            }
            if (maxWidth < lw) {
                maxWidth = lw;
            }
            if (maxHeight < lh) {
                maxHeight = lh;
            }
        }
        assert(nLabels <= axisPtr->t1Ptr->nTicks);

        /* Because the axis cap style is "CapProjecting", we need to
         * account for an extra 1.5 linewidth at the end of each
         * line.  */

        pad = ((axisPtr->lineWidth * 15) / 10);

        if (AxisIsHorizontal(graphPtr, axisPtr)) {
            height += maxHeight + pad;
        } else {
            height += maxWidth + pad;
        }
        if (axisPtr->lineWidth > 0) {
            /* Distance from axis line to tick label. */
            height += AXIS_TITLE_PAD;
            height += ABS(axisPtr->tickLength);
        }
    }

    if (axisPtr->title != NULL) {
        if (axisPtr->titleAlternate) {
            if (height < axisPtr->titleHeight) {
                height = axisPtr->titleHeight;
            }
        } else {
            height += axisPtr->titleHeight + AXIS_TITLE_PAD;
        }
    }

    /* Correct for orientation of the axis. */
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        axisPtr->height = height;
    } else {
        axisPtr->width = height;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetMarginGeometry --
 *
 *      Examines all the axes in the given margin and determines the
 *      area required to display them.
 *
 *      Note: For multiple axes, the titles are displayed in another
 *            margin. So we must keep track of the widest title.
 *
 * Results:
 *      Returns the width or height of the margin, depending if it
 *      runs horizontally along the graph or vertically.
 *
 * Side Effects:
 *      The area width and height set in the margin.  Note again that
 *      this may be corrected later (mulitple axes) to adjust for
 *      the longest title in another margin.
 *
 *----------------------------------------------------------------------
 */
static int
GetMarginGeometry(
    RbcGraph * graphPtr,
    RbcMargin * marginPtr)
{
    RbcChainLink   *linkPtr;
    RbcAxis        *axisPtr;
    int             width, height;
    int             isHoriz;
    int             length, count;

    isHoriz = HORIZMARGIN(marginPtr);
    /* Count the number of visible axes. */
    count = 0;
    length = width = height = 0;
    for (linkPtr = RbcChainFirstLink(marginPtr->axes); linkPtr != NULL;
        linkPtr = RbcChainNextLink(linkPtr)) {
        axisPtr = RbcChainGetValue(linkPtr);
        if ((!axisPtr->hidden) && (axisPtr->flags & AXIS_ONSCREEN)) {
            count++;
            if (graphPtr->flags & RBC_GET_AXIS_GEOMETRY) {
                GetAxisGeometry(graphPtr, axisPtr);
            }
            if ((axisPtr->titleAlternate) && (length < axisPtr->titleWidth)) {
                length = axisPtr->titleWidth;
            }
            if (isHoriz) {
                height += axisPtr->height;
            } else {
                width += axisPtr->width;
            }
        }
    }
    /* Enforce a minimum size for margins. */
    if (width < 3) {
        width = 3;
    }
    if (height < 3) {
        height = 3;
    }
    marginPtr->nAxes = count;
    marginPtr->axesTitleLength = length;
    marginPtr->width = width;
    marginPtr->height = height;
    marginPtr->axesOffset = (HORIZMARGIN(marginPtr)) ? height : width;
    return marginPtr->axesOffset;
}

/*
 *----------------------------------------------------------------------
 *
 * ComputeMargins --
 *
 *      Computes the size of the margins and the plotting area.  We
 *      first compute the space needed for the axes in each margin.
 *      Then how much space the legend will occupy.  Finally, if the
 *      user has requested a margin size, we override the computed
 *      value.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *---------------------------------------------------------------------- */
static void
ComputeMargins(
    RbcGraph * graphPtr)
{
    int             left, right, top, bottom;
    int             width, height;
    int             insets;

    /*
     * Step 1:  Compute the amount of space needed to display the
     *          axes (there many be 0 or more) associated with the
     *          margin.
     */
    top = GetMarginGeometry(graphPtr, &graphPtr->margins[RBC_MARGIN_TOP]);
    bottom = GetMarginGeometry(graphPtr, &graphPtr->margins[RBC_MARGIN_BOTTOM]);
    left = GetMarginGeometry(graphPtr, &graphPtr->margins[RBC_MARGIN_LEFT]);
    right = GetMarginGeometry(graphPtr, &graphPtr->margins[RBC_MARGIN_RIGHT]);

    /*
     * Step 2:  Add the graph title height to the top margin.
     */
    if (graphPtr->title != NULL) {
        top += graphPtr->titleTextStyle.height;
    }
    insets = 2 * (graphPtr->inset + graphPtr->plotBorderWidth);

    /*
     * Step 3:  Use the current estimate of the plot area to compute
     *          the legend size.  Add it to the proper margin.
     */
    width = graphPtr->width - (insets + left + right);
    height = graphPtr->height - (insets + top + bottom);
    RbcMapLegend(graphPtr->legend, width, height);
    if (!RbcLegendIsHidden(graphPtr->legend)) {
        switch (RbcLegendSite(graphPtr->legend)) {
        case RBC_LEGEND_RIGHT:
            right += RbcLegendWidth(graphPtr->legend) + 2;
            break;
        case RBC_LEGEND_LEFT:
            left += RbcLegendWidth(graphPtr->legend) + 2;
            break;
        case RBC_LEGEND_TOP:
            top += RbcLegendHeight(graphPtr->legend) + 2;
            break;
        case RBC_LEGEND_BOTTOM:
            bottom += RbcLegendHeight(graphPtr->legend) + 2;
            break;
        case RBC_LEGEND_XY:
        case RBC_LEGEND_PLOT:
        case RBC_LEGEND_WINDOW:
            /* Do nothing. */
            break;
        }
    }

    /*
     * Recompute the plotarea, now accounting for the legend.
     */
    width = graphPtr->width - (insets + left + right);
    height = graphPtr->height - (insets + top + bottom);

    /*
     * Step 5:  If necessary, correct for the requested plot area
     *          aspect ratio.
     */
    if (graphPtr->aspect > 0.0) {
        double          ratio;

        /*
         * Shrink one dimension of the plotarea to fit the requested
         * width/height aspect ratio.
         */
        ratio = (double) width / (double) height;
        if (ratio > graphPtr->aspect) {
            int             scaledWidth;

            /* Shrink the width. */
            scaledWidth = (int) (height * graphPtr->aspect);
            if (scaledWidth < 1) {
                scaledWidth = 1;
            }
            right += (width - scaledWidth);     /* Add the difference to
                                                 * the right margin. */
            /* CHECK THIS: width = scaledWidth; */
        } else {
            int             scaledHeight;

            /* Shrink the height. */
            scaledHeight = (int) (width / graphPtr->aspect);
            if (scaledHeight < 1) {
                scaledHeight = 1;
            }
            top += (height - scaledHeight);     /* Add the difference to
                                                 * the top margin. */
            /* CHECK THIS: height = scaledHeight; */
        }
    }

    /*
     * Step 6:  If there's multiple axes in a margin, the axis
     *          titles will be displayed in the adjoining marging.
     *          Make sure there's room for the longest axis titles.
     */

    if (top < graphPtr->margins[RBC_MARGIN_LEFT].axesTitleLength) {
        top = graphPtr->margins[RBC_MARGIN_LEFT].axesTitleLength;
    }
    if (right < graphPtr->margins[RBC_MARGIN_BOTTOM].axesTitleLength) {
        right = graphPtr->margins[RBC_MARGIN_BOTTOM].axesTitleLength;
    }
    if (top < graphPtr->margins[RBC_MARGIN_RIGHT].axesTitleLength) {
        top = graphPtr->margins[RBC_MARGIN_RIGHT].axesTitleLength;
    }
    if (right < graphPtr->margins[RBC_MARGIN_TOP].axesTitleLength) {
        right = graphPtr->margins[RBC_MARGIN_TOP].axesTitleLength;
    }

    /*
     * Step 7:  Override calculated values with requested margin
     *          sizes.
     */

    graphPtr->margins[RBC_MARGIN_LEFT].width = left;
    graphPtr->margins[RBC_MARGIN_RIGHT].width = right;
    graphPtr->margins[RBC_MARGIN_TOP].height = top;
    graphPtr->margins[RBC_MARGIN_BOTTOM].height = bottom;

    if (graphPtr->margins[RBC_MARGIN_LEFT].reqSize > 0) {
        graphPtr->margins[RBC_MARGIN_LEFT].width = graphPtr->margins[RBC_MARGIN_LEFT].reqSize;
    }
    if (graphPtr->margins[RBC_MARGIN_RIGHT].reqSize > 0) {
        graphPtr->margins[RBC_MARGIN_RIGHT].width = graphPtr->margins[RBC_MARGIN_RIGHT].reqSize;
    }
    if (graphPtr->margins[RBC_MARGIN_TOP].reqSize > 0) {
        graphPtr->margins[RBC_MARGIN_TOP].height = graphPtr->margins[RBC_MARGIN_TOP].reqSize;
    }
    if (graphPtr->margins[RBC_MARGIN_BOTTOM].reqSize > 0) {
        graphPtr->margins[RBC_MARGIN_BOTTOM].height = graphPtr->margins[RBC_MARGIN_BOTTOM].reqSize;
    }
}

/*
 * -----------------------------------------------------------------
 *
 * RbcLayoutMargins --
 *
 *      Calculate the layout of the graph.  Based upon the data,
 *      axis limits, X and Y titles, and title height, determine
 *      the cavity left which is the plotting surface.  The first
 *      step get the data and axis limits for calculating the space
 *      needed for the top, bottom, left, and right margins.
 *
 *      1) The LEFT margin is the area from the left border to the
 *         Y axis (not including ticks). It composes the border
 *         width, the width an optional Y axis label and its padding,
 *         and the tick numeric labels. The Y axis label is rotated
 *         90 degrees so that the width is the font height.
 *
 *      2) The RIGHT margin is the area from the end of the graph
 *         to the right window border. It composes the border width,
 *         some padding, the font height (this may be dubious. It
 *         appears to provide a more even border), the max of the
 *         legend width and 1/2 max X tick number. This last part is
 *         so that the last tick label is not clipped.
 *
 *               Window Width
 *          ___________________________________________________________
 *          |          |                               |               |
 *          |          |   TOP  height of title        |               |
 *          |          |                               |               |
 *          |          |           x2 title            |               |
 *          |          |                               |               |
 *          |          |        height of x2-axis      |               |
 *          |__________|_______________________________|_______________|  W
 *          |          | -plotpady                     |               |  i
 *          |__________|_______________________________|_______________|  n
 *          |          | top                   right   |               |  d
 *          |          |                               |               |  o
 *          |   LEFT   |                               |     RIGHT     |  w
 *          |          |                               |               |
 *          | y        |     Free area = 104%          |      y2       |  H
 *          |          |     Plotting surface = 100%   |               |  e
 *          | t        |     Tick length = 2 + 2%      |      t        |  i
 *          | i        |                               |      i        |  g
 *          | t        |                               |      t  legend|  h
 *          | l        |                               |      l   width|  t
 *          | e        |                               |      e        |
 *          |    height|                               |height         |
 *          |       of |                               | of            |
 *          |    y-axis|                               |y2-axis        |
 *          |          |                               |               |
 *          |          |origin 0,0                     |               |
 *          |__________|_left___________________bottom___|_______________|
 *          |          |-plotpady                      |               |
 *          |__________|_______________________________|_______________|
 *          |          | (xoffset, yoffset)            |               |
 *          |          |                               |               |
 *          |          |       height of x-axis        |               |
 *          |          |                               |               |
 *          |          |   BOTTOM   x title            |               |
 *          |__________|_______________________________|_______________|
 *
 *      3) The TOP margin is the area from the top window border to the top
 *         of the graph. It composes the border width, twice the height of
 *         the title font (if one is given) and some padding between the
 *         title.
 *
 *      4) The BOTTOM margin is area from the bottom window border to the
 *         X axis (not including ticks). It composes the border width, the height
 *         an optional X axis label and its padding, the height of the font
 *         of the tick labels.
 *
 *      The plotting area is between the margins which includes the X and Y axes
 *      including the ticks but not the tick numeric labels. The length of
 *      the ticks and its padding is 5% of the entire plotting area.  Hence the
 *      entire plotting area is scaled as 105% of the width and height of the
 *      area.
 *
 *      The axis labels, ticks labels, title, and legend may or may not be
 *      displayed which must be taken into account.
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
RbcLayoutMargins(
    RbcGraph * graphPtr)
{
    int             width, height;
    int             titleY;
    int             left, right, top, bottom;

    ComputeMargins(graphPtr);
    left = graphPtr->margins[RBC_MARGIN_LEFT].width + graphPtr->inset +
        graphPtr->plotBorderWidth;
    right = graphPtr->margins[RBC_MARGIN_RIGHT].width + graphPtr->inset +
        graphPtr->plotBorderWidth;
    top = graphPtr->margins[RBC_MARGIN_TOP].height + graphPtr->inset +
        graphPtr->plotBorderWidth;
    bottom = graphPtr->margins[RBC_MARGIN_BOTTOM].height + graphPtr->inset +
        graphPtr->plotBorderWidth;

    /* Based upon the margins, calculate the space left for the graph. */
    width = graphPtr->width - (left + right);
    height = graphPtr->height - (top + bottom);
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }
    graphPtr->left = left;
    graphPtr->right = left + width;
    graphPtr->bottom = top + height;
    graphPtr->top = top;

    graphPtr->vOffset = top + graphPtr->padY.side1;     /*top */
    graphPtr->vRange = height - RbcPadding(graphPtr->padY);
    graphPtr->hOffset = left + graphPtr->padX.side1 /*left */ ;
    graphPtr->hRange = width - RbcPadding(graphPtr->padX);

    if (graphPtr->vRange < 1) {
        graphPtr->vRange = 1;
    }
    if (graphPtr->hRange < 1) {
        graphPtr->hRange = 1;
    }
    graphPtr->hScale = 1.0 / (double) graphPtr->hRange;
    graphPtr->vScale = 1.0 / (double) graphPtr->vRange;

    /*
     * Calculate the placement of the graph title so it is centered within the
     * space provided for it in the top margin
     */
    titleY = graphPtr->titleTextStyle.height;
    graphPtr->titleY = (titleY / 2) + graphPtr->inset;
    graphPtr->titleX = (graphPtr->right + graphPtr->left) / 2;

}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureAxis --
 *
 *      Configures axis attributes (font, line width, label, etc).
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      Axis layout is deferred until the height and width of the
 *      window are known.
 *
 * ----------------------------------------------------------------------
 */
static int
ConfigureAxis(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr)
{
    char            errMsg[200];

    /* Check the requested axis limits. Can't allow -min to be greater
     * than -max, or have undefined log scale limits.  */
    if (((!TclIsNaN(axisPtr->reqMin)) && (!TclIsNaN(axisPtr->reqMax))) &&
        (axisPtr->reqMin >= axisPtr->reqMax)) {
        sprintf(errMsg, "impossible limits (min %g >= max %g) for axis \"%s\"",
            axisPtr->reqMin, axisPtr->reqMax, axisPtr->name);
        Tcl_AppendResult(graphPtr->interp, errMsg, (char *) NULL);
        /* Bad values, turn on axis auto-scaling */
        axisPtr->reqMin = axisPtr->reqMax = rbcNaN;
        return TCL_ERROR;
    }
    if ((axisPtr->logScale) && (!TclIsNaN(axisPtr->reqMin)) &&
        (axisPtr->reqMin <= 0.0)) {
        sprintf(errMsg, "bad logscale limits (min=%g,max=%g) for axis \"%s\"",
            axisPtr->reqMin, axisPtr->reqMax, axisPtr->name);
        Tcl_AppendResult(graphPtr->interp, errMsg, (char *) NULL);
        /* Bad minimum value, turn on auto-scaling */
        axisPtr->reqMin = rbcNaN;
        return TCL_ERROR;
    }
    axisPtr->tickTextStyle.theta = FMOD(axisPtr->tickTextStyle.theta, 360.0);
    if (axisPtr->tickTextStyle.theta < 0.0) {
        axisPtr->tickTextStyle.theta += 360.0;
    }
    ResetTextStyles(graphPtr, axisPtr);

    axisPtr->titleWidth = axisPtr->titleHeight = 0;
    if (axisPtr->title != NULL) {
        int             w, h;

        RbcGetTextExtents(&axisPtr->titleTextStyle, axisPtr->title, &w, &h);
        axisPtr->titleWidth = (short int) w;
        axisPtr->titleHeight = (short int) h;
    }

    /*
     * Don't bother to check what configuration options have changed.
     * Almost every option changes the size of the plotting area
     * (except for -color and -titlecolor), requiring the graph and
     * its contents to be completely redrawn.
     *
     * Recompute the scale and offset of the axis in case -min, -max
     * options have changed.
     */
    graphPtr->flags |= RBC_REDRAW_WORLD;
    if (!RbcConfigModified(configSpecs, "-*color", "-background", "-bg",
            (char *) NULL)) {
        graphPtr->flags |= (RBC_MAP_WORLD | RBC_RESET_AXES);
        axisPtr->flags |= AXIS_DIRTY;
    }
    RbcEventuallyRedrawGraph(graphPtr);

    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateAxis --
 *
 *      Create and initialize a structure containing information to
 *      display a graph axis.
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static RbcAxis *
CreateAxis(
    RbcGraph * graphPtr,
    const char *name,           /* Identifier for axis. */
    int margin)
{
    RbcAxis        *axisPtr;
    Tcl_HashEntry  *hPtr;
    int             isNew;

    if (name[0] == '-') {
        Tcl_AppendResult(graphPtr->interp, "name of axis \"", name,
            "\" can't start with a '-'", (char *) NULL);
        return NULL;
    }
    hPtr = Tcl_CreateHashEntry(&graphPtr->axes.table, name, &isNew);
    if (!isNew) {
        axisPtr = (RbcAxis *) Tcl_GetHashValue(hPtr);
        if (!axisPtr->deletePending) {
            Tcl_AppendResult(graphPtr->interp, "axis \"", name,
                "\" already exists in \"", Tk_PathName(graphPtr->tkwin), "\"",
                (char *) NULL);
            return NULL;
        }
        axisPtr->deletePending = FALSE;
    } else {
        axisPtr = RbcCalloc(1, sizeof(RbcAxis));
        assert(axisPtr);

        axisPtr->name = RbcStrdup(name);
        axisPtr->hashPtr = hPtr;
        axisPtr->classUid = NULL;
        axisPtr->looseMin = axisPtr->looseMax = TICK_RANGE_TIGHT;
        axisPtr->reqNumMinorTicks = 2;
        axisPtr->scrollUnits = 10;
        axisPtr->showTicks = TRUE;
        axisPtr->reqMin = axisPtr->reqMax = rbcNaN;
        axisPtr->scrollMin = axisPtr->scrollMax = rbcNaN;

        if ((graphPtr->classUid == rbcBarElementUid) &&
            ((margin == RBC_MARGIN_TOP) || (margin == RBC_MARGIN_BOTTOM))) {
            axisPtr->reqStep = 1.0;
            axisPtr->reqNumMinorTicks = 0;
        }
        if ((margin == RBC_MARGIN_RIGHT) || (margin == RBC_MARGIN_TOP)) {
            axisPtr->hidden = TRUE;
        }
        RbcInitTextStyle(&axisPtr->titleTextStyle);
        RbcInitTextStyle(&axisPtr->limitsTextStyle);
        RbcInitTextStyle(&axisPtr->tickTextStyle);
        axisPtr->tickLabels = RbcChainCreate();
        axisPtr->lineWidth = 1;
        axisPtr->tickTextStyle.padX.side1 = 2;
        axisPtr->tickTextStyle.padX.side2 = 2;
        Tcl_SetHashValue(hPtr, axisPtr);
    }
    return axisPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * NameToAxis --
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
NameToAxis(
    RbcGraph * graphPtr,        /* Graph widget record. */
    const char *name,           /* Name of the axis to be searched for. */
    RbcAxis ** axisPtrPtr)
{                               /* (out) Pointer to found axis structure. */
    Tcl_HashEntry  *hPtr;

    hPtr = Tcl_FindHashEntry(&graphPtr->axes.table, name);
    if (hPtr != NULL) {
        RbcAxis        *axisPtr;

        axisPtr = (RbcAxis *) Tcl_GetHashValue(hPtr);
        if (!axisPtr->deletePending) {
            *axisPtrPtr = axisPtr;
            return TCL_OK;
        }
    }
    Tcl_AppendResult(graphPtr->interp, "can't find axis \"", name,
        "\" in \"", Tk_PathName(graphPtr->tkwin), "\"", (char *) NULL);
    *axisPtrPtr = NULL;
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * GetAxis --
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
GetAxis(
    RbcGraph * graphPtr,
    const char *axisName,
    RbcUid classUid,
    RbcAxis ** axisPtrPtr)
{
    RbcAxis        *axisPtr;

    if (NameToAxis(graphPtr, axisName, &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (classUid != NULL) {
        if ((axisPtr->refCount == 0) || (axisPtr->classUid == NULL)) {
            /* Set the axis type on the first use of it. */
            axisPtr->classUid = classUid;
        } else if (axisPtr->classUid != classUid) {
            Tcl_AppendResult(graphPtr->interp, "axis \"", axisName,
                "\" is already in use on an opposite ", axisPtr->classUid,
                "-axis", (char *) NULL);
            return TCL_ERROR;
        }
        axisPtr->refCount++;
    }
    *axisPtrPtr = axisPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeAxis --
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
FreeAxis(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr)
{
    axisPtr->refCount--;
    if ((axisPtr->deletePending) && (axisPtr->refCount == 0)) {
        DestroyAxis(graphPtr, axisPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcDestroyAxes --
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
RbcDestroyAxes(
    RbcGraph * graphPtr)
{
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  cursor;
    RbcAxis        *axisPtr;
    int             i;

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (RbcAxis *) Tcl_GetHashValue(hPtr);
        axisPtr->hashPtr = NULL;
        DestroyAxis(graphPtr, axisPtr);
    }
    Tcl_DeleteHashTable(&graphPtr->axes.table);
    for (i = 0; i < 4; i++) {
        RbcChainDestroy(graphPtr->axisChain[i]);
    }
    Tcl_DeleteHashTable(&graphPtr->axes.tagTable);
    RbcChainDestroy(graphPtr->axes.displayList);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcDefaultAxes --
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
RbcDefaultAxes(
    RbcGraph * graphPtr)
{
    register int    i;
    RbcAxis        *axisPtr;
    RbcChain       *chainPtr;
    static const char *axisNames[4] = { "x", "y", "x2", "y2" };
    int             flags;

    flags = RbcGraphType(graphPtr);
    for (i = 0; i < 4; i++) {
        chainPtr = RbcChainCreate();
        graphPtr->axisChain[i] = chainPtr;

        /* Create a default axis for each chain. */
        axisPtr = CreateAxis(graphPtr, axisNames[i], i);
        if (axisPtr == NULL) {
            return TCL_ERROR;
        }
        axisPtr->refCount = 1;  /* Default axes are assumed in use. */
        axisPtr->classUid = (i & 1) ? rbcYAxisUid : rbcXAxisUid;
        axisPtr->flags |= AXIS_ONSCREEN;

        /*
         * RbcConfigureWidgetComponent creates a temporary child window
         * by the name of the axis.  It's used so that the Tk routines
         * that access the X resource database can describe a single
         * component and not the entire graph.
         */
        if (RbcConfigureWidgetComponent(graphPtr->interp, graphPtr->tkwin,
                axisPtr->name, "Axis", configSpecs, 0, (const char **) NULL,
                (char *) axisPtr, flags) != TCL_OK) {
            return TCL_ERROR;
        }
        if (ConfigureAxis(graphPtr, axisPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        axisPtr->linkPtr = RbcChainAppend(chainPtr, axisPtr);
        axisPtr->chainPtr = chainPtr;
    }
    return TCL_OK;
}

/*----------------------------------------------------------------------
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
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int argc,
    const char **argv)
{
    Tcl_Interp     *interp = graphPtr->interp;

    return RbcConfigureBindings(interp, graphPtr->bindTable,
        RbcMakeAxisTag(graphPtr, axisPtr->name), argc, argv);
}

/*
 * ----------------------------------------------------------------------
 *
 * CgetOp --
 *
 *      Queries axis attributes (font, line width, label, etc).
 *
 * Results:
 *      Return value is a standard Tcl result.  If querying configuration
 *      values, interp->result will contain the results.
 *
 * Side Effects:
 *      TODO: SIde Effects
 *
 * ----------------------------------------------------------------------
 */
static int
CgetOp(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int argc,                   /* Not used. */
    const char *argv[])
{
    return Tk_ConfigureValue(graphPtr->interp, graphPtr->tkwin, configSpecs,
        (char *) axisPtr, argv[0], RbcGraphType(graphPtr));
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      Queries or resets axis attributes (font, line width, label, etc).
 *
 * Results:
 *      Return value is a standard Tcl result.  If querying configuration
 *      values, interp->result will contain the results.
 *
 * Side Effects:
 *      Axis resources are possibly allocated (GC, font). Axis layout is
 *      deferred until the height and width of the window are known.
 *
 * ----------------------------------------------------------------------
 */
static int
ConfigureOp(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int argc,
    const char *argv[])
{
    int             flags;

    flags = TK_CONFIG_ARGV_ONLY | RbcGraphType(graphPtr);
    if (argc == 0) {
        return Tk_ConfigureInfo(graphPtr->interp, graphPtr->tkwin, configSpecs,
            (char *) axisPtr, (char *) NULL, flags);
    } else if (argc == 1) {
        return Tk_ConfigureInfo(graphPtr->interp, graphPtr->tkwin, configSpecs,
            (char *) axisPtr, argv[0], flags);
    }
    if (Tk_ConfigureWidget(graphPtr->interp, graphPtr->tkwin, configSpecs,
            argc, argv, (char *) axisPtr, flags) != TCL_OK) {
        return TCL_ERROR;
    }
    if (ConfigureAxis(graphPtr, axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (axisPtr->flags & AXIS_ONSCREEN) {
        if (!RbcConfigModified(configSpecs, "-*color", "-background", "-bg",
                (char *) NULL)) {
            graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
        }
        graphPtr->flags |= RBC_DRAW_MARGINS;
        RbcEventuallyRedrawGraph(graphPtr);
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * GetOp --
 *
 *      Returns the name of the picked axis (using the axis
 *      bind operation).  Right now, the only name accepted is
 *      "current".
 *
 * Results:
 *      A standard Tcl result.  The interpreter result will contain
 *      the name of the axis.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
GetOp(
    RbcGraph * graphPtr,
    int argc,                   /* Not used. */
    const char *argv[])
{
    Tcl_Interp     *interp = graphPtr->interp;
    register RbcAxis *axisPtr;

    axisPtr = (RbcAxis *) RbcGetCurrentItem(graphPtr->bindTable);
    /* Report only on axes. */
    if ((axisPtr != NULL) &&
        ((axisPtr->classUid == rbcXAxisUid) ||
            (axisPtr->classUid == rbcYAxisUid) ||
            (axisPtr->classUid == NULL))) {
        char            c;

        c = argv[3][0];
        if ((c == 'c') && (strcmp(argv[3], "current") == 0)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(axisPtr->name, -1));
        } else if ((c == 'd') && (strcmp(argv[3], "detail") == 0)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(axisPtr->detail, -1));
        }
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * LimitsOp --
 *
 *      This procedure returns a string representing the axis limits
 *      of the graph.  The format of the string is { left top right bottom}.
 *
 * Results:
 *      Always returns TCL_OK.  The interp->result field is
 *      a list of the graph axis limits.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int
LimitsOp(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int argc,                   /* Not used. */
    const char **argv)
{                               /* Not used. */
    Tcl_Interp     *interp = graphPtr->interp;
    double          min, max;

    if (graphPtr->flags & RBC_RESET_AXES) {
        RbcResetAxes(graphPtr);
    }
    if (axisPtr->logScale) {
        min = EXP10(axisPtr->axisRange.min);
        max = EXP10(axisPtr->axisRange.max);
    } else {
        min = axisPtr->axisRange.min;
        max = axisPtr->axisRange.max;
    }
    Tcl_AppendElement(interp, RbcDtoa(interp, min));
    Tcl_AppendElement(interp, RbcDtoa(interp, max));
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * InvTransformOp --
 *
 *      Maps the given window coordinate into an axis-value.
 *
 * Results:
 *      Returns a standard Tcl result.  interp->result contains
 *      the axis value. If an error occurred, TCL_ERROR is returned
 *      and interp->result will contain an error message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
InvTransformOp(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int argc,                   /* Not used. */
    const char **argv)
{
    int             x;          /* Integer window coordinate */
    double          y;          /* Real graph coordinate */

    if (graphPtr->flags & RBC_RESET_AXES) {
        RbcResetAxes(graphPtr);
    }
    if (Tcl_GetInt(graphPtr->interp, argv[0], &x) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Is the axis vertical or horizontal?
     *
     * Check the site where the axis was positioned.  If the axis is
     * virtual, all we have to go on is how it was mapped to an
     * element (using either -mapx or -mapy options).
     */
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        y = RbcInvHMap(graphPtr, axisPtr, (double) x);
    } else {
        y = RbcInvVMap(graphPtr, axisPtr, (double) x);
    }
    Tcl_AppendElement(graphPtr->interp, RbcDtoa(graphPtr->interp, y));
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * TransformOp --
 *
 *      Maps the given axis-value to a window coordinate.
 *
 * Results:
 *      Returns a standard Tcl result.  interp->result contains
 *      the window coordinate. If an error occurred, TCL_ERROR
 *      is returned and interp->result will contain an error
 *      message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
TransformOp(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    int argc,                   /* Not used. */
    const char **argv)
{
    double          x;

    if (graphPtr->flags & RBC_RESET_AXES) {
        RbcResetAxes(graphPtr);
    }
    if (Tcl_ExprDouble(graphPtr->interp, argv[0], &x) != TCL_OK) {
        return TCL_ERROR;
    }
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        x = RbcHMap(graphPtr, axisPtr, x);
    } else {
        x = RbcVMap(graphPtr, axisPtr, x);
    }
    Tcl_SetObjResult(graphPtr->interp, Tcl_NewIntObj((int) x));
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * UseOp --
 *
 *      Changes the virtual axis used by the logical axis.
 *
 * Results:
 *      A standard Tcl result.  If the named axis doesn't exist
 *      an error message is put in interp->result.
 *
 *      .g xaxis use "abc def gah"
 *      .g xaxis use [lappend abc [.g axis use]]
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int
UseOp(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,          /* Not used. */
    int argc,
    const char **argv)
{
    RbcChain       *chainPtr;
    int             nNames;
    const char    **names;
    RbcChainLink   *linkPtr;
    int             i;
    RbcUid          classUid;
    int             margin;

    margin = (int) argv[-1];
    chainPtr = graphPtr->margins[margin].axes;
    if (argc == 0) {
        for (linkPtr = RbcChainFirstLink(chainPtr); linkPtr != NULL;
            linkPtr = RbcChainNextLink(linkPtr)) {
            axisPtr = RbcChainGetValue(linkPtr);
            Tcl_AppendElement(graphPtr->interp, axisPtr->name);
        }
        return TCL_OK;
    }
    if ((margin == RBC_MARGIN_BOTTOM) || (margin == RBC_MARGIN_TOP)) {
        classUid = (graphPtr->inverted) ? rbcYAxisUid : rbcXAxisUid;
    } else {
        classUid = (graphPtr->inverted) ? rbcXAxisUid : rbcYAxisUid;
    }
    if (Tcl_SplitList(graphPtr->interp, argv[0], &nNames, &names) != TCL_OK) {
        return TCL_ERROR;
    }
    for (linkPtr = RbcChainFirstLink(chainPtr); linkPtr != NULL;
        linkPtr = RbcChainNextLink(linkPtr)) {
        axisPtr = RbcChainGetValue(linkPtr);
        axisPtr->linkPtr = NULL;
        axisPtr->flags &= ~AXIS_ONSCREEN;
        /* Clear the axis type if it's not currently used. */
        if (axisPtr->refCount == 0) {
            axisPtr->classUid = NULL;
        }
    }
    RbcChainReset(chainPtr);
    for (i = 0; i < nNames; i++) {
        if (NameToAxis(graphPtr, names[i], &axisPtr) != TCL_OK) {
            ckfree((char *) names);
            return TCL_ERROR;
        }
        if (axisPtr->classUid == NULL) {
            axisPtr->classUid = classUid;
        } else if (axisPtr->classUid != classUid) {
            Tcl_AppendResult(graphPtr->interp, "wrong type axis \"",
                axisPtr->name, "\": can't use ", classUid, " type axis.",
                (char *) NULL);
            ckfree((char *) names);
            return TCL_ERROR;
        }
        if (axisPtr->linkPtr != NULL) {
            /* Move the axis from the old margin's "use" list to the new. */
            RbcChainUnlinkLink(axisPtr->chainPtr, axisPtr->linkPtr);
            RbcChainLinkBefore(chainPtr, axisPtr->linkPtr, (RbcChainLink *)NULL);/* append on end */
        } else {
            axisPtr->linkPtr = RbcChainAppend(chainPtr, axisPtr);
        }
        axisPtr->chainPtr = chainPtr;
        axisPtr->flags |= AXIS_ONSCREEN;
    }
    graphPtr->flags |=
        (RBC_GET_AXIS_GEOMETRY | RBC_LAYOUT_NEEDED | RBC_RESET_AXES);
    /* When any axis changes, we need to layout the entire graph.  */
    graphPtr->flags |= (RBC_MAP_WORLD | RBC_REDRAW_WORLD);
    RbcEventuallyRedrawGraph(graphPtr);

    ckfree((char *) names);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateVirtualOp --
 *
 *      Creates a new axis.
 *
 * Results:
 *      Returns a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
CreateVirtualOp(
    RbcGraph * graphPtr,
    int argc,
    const char **argv)
{
    RbcAxis        *axisPtr;
    int             flags;

    axisPtr = CreateAxis(graphPtr, argv[3], RBC_MARGIN_NONE);
    if (axisPtr == NULL) {
        return TCL_ERROR;
    }
    flags = RbcGraphType(graphPtr);
    if (RbcConfigureWidgetComponent(graphPtr->interp, graphPtr->tkwin,
            axisPtr->name, "Axis", configSpecs, argc - 4, argv + 4,
            (char *) axisPtr, flags) != TCL_OK) {
        goto error;
    }
    if (ConfigureAxis(graphPtr, axisPtr) != TCL_OK) {
        goto error;
    }
    Tcl_SetResult(graphPtr->interp, axisPtr->name, TCL_VOLATILE);
    return TCL_OK;
  error:
    DestroyAxis(graphPtr, axisPtr);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * BindVirtualOp --
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
BindVirtualOp(
    RbcGraph * graphPtr,
    int argc,
    const char **argv)
{
    Tcl_Interp     *interp = graphPtr->interp;

    if (argc == 3) {
        Tcl_HashEntry  *hPtr;
        Tcl_HashSearch  cursor;
        char           *tagName;

        for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.tagTable, &cursor);
            hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
            tagName = Tcl_GetHashKey(&graphPtr->axes.tagTable, hPtr);
            Tcl_AppendElement(interp, tagName);
        }
        return TCL_OK;
    }
    return RbcConfigureBindings(interp, graphPtr->bindTable,
        RbcMakeAxisTag(graphPtr, argv[3]), argc - 4, argv + 4);
}

/*
 * ----------------------------------------------------------------------
 *
 * CgetVirtualOp --
 *
 *      Queries axis attributes (font, line width, label, etc).
 *
 * Results:
 *      Return value is a standard Tcl result.  If querying configuration
 *      values, interp->result will contain the results.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
CgetVirtualOp(
    RbcGraph * graphPtr,
    int argc,
    const char *argv[])
{
    RbcAxis        *axisPtr;

    if (NameToAxis(graphPtr, argv[3], &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return CgetOp(graphPtr, axisPtr, argc - 4, argv + 4);
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureVirtualOp --
 *
 *      Queries or resets axis attributes (font, line width, label, etc).
 *
 * Results:
 *      Return value is a standard Tcl result.  If querying configuration
 *      values, interp->result will contain the results.
 *
 * Side Effects:
 *      Axis resources are possibly allocated (GC, font). Axis layout is
 *      deferred until the height and width of the window are known.
 *
 * ----------------------------------------------------------------------
 */
static int
ConfigureVirtualOp(
    RbcGraph * graphPtr,
    int argc,
    const char *argv[])
{
    RbcAxis        *axisPtr;
    int             nNames, nOpts;
    const char    **options;
    register int    i;

    /* Figure out where the option value pairs begin */
    argc -= 3;
    argv += 3;
    for (i = 0; i < argc; i++) {
        if (argv[i][0] == '-') {
            break;
        }
        if (NameToAxis(graphPtr, argv[i], &axisPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    nNames = i;                 /* Number of pen names specified */
    nOpts = argc - i;           /* Number of options specified */
    options = argv + i;         /* Start of options in argv  */

    for (i = 0; i < nNames; i++) {
        if (NameToAxis(graphPtr, argv[i], &axisPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (ConfigureOp(graphPtr, axisPtr, nOpts, options) != TCL_OK) {
            break;
        }
    }
    if (i < nNames) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * DeleteVirtualOp --
 *
 *      Deletes one or more axes.  The actual removal may be deferred
 *      until the axis is no longer used by any element. The axis
 *      can't be referenced by its name any longer and it may be
 *      recreated.
 *
 * Results:
 *      Returns a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
DeleteVirtualOp(
    RbcGraph * graphPtr,
    int argc,
    const char **argv)
{
    register int    i;
    RbcAxis        *axisPtr;

    for (i = 3; i < argc; i++) {
        if (NameToAxis(graphPtr, argv[i], &axisPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        axisPtr->deletePending = TRUE;
        if (axisPtr->refCount == 0) {
            DestroyAxis(graphPtr, axisPtr);
        }
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * InvTransformVirtualOp --
 *
 *      Maps the given window coordinate into an axis-value.
 *
 * Results:
 *      Returns a standard Tcl result.  interp->result contains
 *      the axis value. If an error occurred, TCL_ERROR is returned
 *      and interp->result will contain an error message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
InvTransformVirtualOp(
    RbcGraph * graphPtr,
    int argc,                   /* Not used. */
    const char **argv)
{
    RbcAxis        *axisPtr;

    if (NameToAxis(graphPtr, argv[3], &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return InvTransformOp(graphPtr, axisPtr, argc - 4, argv + 4);
}

/*
 *--------------------------------------------------------------
 *
 * LimitsVirtualOp --
 *
 *      This procedure returns a string representing the axis limits
 *      of the graph.  The format of the string is { left top right bottom}.
 *
 * Results:
 *      Always returns TCL_OK.  The interp->result field is
 *      a list of the graph axis limits.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int
LimitsVirtualOp(
    RbcGraph * graphPtr,
    int argc,                   /* Not used. */
    const char **argv)
{                               /* Not used. */
    RbcAxis        *axisPtr;

    if (NameToAxis(graphPtr, argv[3], &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return LimitsOp(graphPtr, axisPtr, argc - 4, argv + 4);
}

/*
 * ----------------------------------------------------------------------
 *
 * NamesVirtualOp --
 *
 *      Return a list of the names of all the axes.
 *
 * Results:
 *      Returns a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
NamesVirtualOp(
    RbcGraph * graphPtr,
    int argc,                   /* Not used. */
    const char **argv)
{                               /* Not used. */
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  cursor;
    RbcAxis        *axisPtr;
    register int    i;

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (RbcAxis *) Tcl_GetHashValue(hPtr);
        if (axisPtr->deletePending) {
            continue;
        }
        if (argc == 3) {
            Tcl_AppendElement(graphPtr->interp, axisPtr->name);
            continue;
        }
        for (i = 3; i < argc; i++) {
            if (Tcl_StringMatch(axisPtr->name, argv[i])) {
                Tcl_AppendElement(graphPtr->interp, axisPtr->name);
                break;
            }
        }
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * TransformVirtualOp --
 *
 *	Maps the given axis-value to a window coordinate.
 *
 * Results:
 *      Returns a standard Tcl result.  interp->result contains
 *      the window coordinate. If an error occurred, TCL_ERROR
 *      is returned and interp->result will contain an error
 *      message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
TransformVirtualOp(
    RbcGraph * graphPtr,
    int argc,                   /* Not used. */
    const char **argv)
{
    RbcAxis        *axisPtr;

    if (NameToAxis(graphPtr, argv[3], &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return TransformOp(graphPtr, axisPtr, argc - 4, argv + 4);
}

/*
 *----------------------------------------------------------------------
 *
 * ViewOp --
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
ViewOp(
    RbcGraph * graphPtr,
    int argc,
    const char **argv)
{
    RbcAxis        *axisPtr;
    Tcl_Interp     *interp = graphPtr->interp;
    double          axisOffset, scrollUnits;
    double          fract;
    double          viewMin, viewMax, worldMin, worldMax;
    double          viewWidth, worldWidth;

    if (NameToAxis(graphPtr, argv[3], &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    worldMin = axisPtr->valueRange.min;
    worldMax = axisPtr->valueRange.max;
    /* Override data dimensions with user-selected limits. */
    if (!TclIsNaN(axisPtr->scrollMin)) {
        worldMin = axisPtr->scrollMin;
    }
    if (!TclIsNaN(axisPtr->scrollMax)) {
        worldMax = axisPtr->scrollMax;
    }
    viewMin = axisPtr->min;
    viewMax = axisPtr->max;
    /* Bound the view within scroll region. */
    if (viewMin < worldMin) {
        viewMin = worldMin;
    }
    if (viewMax > worldMax) {
        viewMax = worldMax;
    }
    if (axisPtr->logScale) {
        worldMin = log10(worldMin);
        worldMax = log10(worldMax);
        viewMin = log10(viewMin);
        viewMax = log10(viewMax);
    }
    worldWidth = worldMax - worldMin;
    viewWidth = viewMax - viewMin;

    /* Unlike horizontal axes, vertical axis values run opposite of
     * the scrollbar first/last values.  So instead of pushing the
     * axis minimum around, we move the maximum instead. */

    if (AxisIsHorizontal(graphPtr, axisPtr) != axisPtr->descending) {
        axisOffset = viewMin - worldMin;
        scrollUnits = (double) axisPtr->scrollUnits * graphPtr->hScale;
    } else {
        axisOffset = worldMax - viewMax;
        scrollUnits = (double) axisPtr->scrollUnits * graphPtr->vScale;
    }
    if (argc == 4) {
        /* Note: Bound the fractions between 0.0 and 1.0 to support
         * "canvas"-style scrolling. */
        fract = axisOffset / worldWidth;
        Tcl_AppendElement(interp, RbcDtoa(interp, CLAMP(fract, 0.0, 1.0)));
        fract = (axisOffset + viewWidth) / worldWidth;
        Tcl_AppendElement(interp, RbcDtoa(interp, CLAMP(fract, 0.0, 1.0)));
        return TCL_OK;
    }
    fract = axisOffset / worldWidth;
    if (GetAxisScrollInfo(interp, argc - 4, argv + 4, &fract,
            viewWidth / worldWidth, scrollUnits) != TCL_OK) {
        return TCL_ERROR;
    }
    if (AxisIsHorizontal(graphPtr, axisPtr) != axisPtr->descending) {
        axisPtr->reqMin = (fract * worldWidth) + worldMin;
        axisPtr->reqMax = axisPtr->reqMin + viewWidth;
    } else {
        axisPtr->reqMax = worldMax - (fract * worldWidth);
        axisPtr->reqMin = axisPtr->reqMax - viewWidth;
    }
    if (axisPtr->logScale) {
        axisPtr->reqMin = EXP10(axisPtr->reqMin);
        axisPtr->reqMax = EXP10(axisPtr->reqMax);
    }
    graphPtr->flags |=
        (RBC_GET_AXIS_GEOMETRY | RBC_LAYOUT_NEEDED | RBC_RESET_AXES);
    RbcEventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcVirtualAxisOp --
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
RbcVirtualAxisOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcOp           proc;
    int             result;
    static RbcOpSpec axisOps[] = {
        {"bind", 1, (RbcOp) BindVirtualOp, 3, 6, "axisName sequence command",},
        {"cget", 2, (RbcOp) CgetVirtualOp, 5, 5, "axisName option",},
        {"configure", 2, (RbcOp) ConfigureVirtualOp, 4, 0,
            "axisName ?axisName?... ?option value?...",},
        {"create", 2, (RbcOp) CreateVirtualOp, 4, 0,
            "axisName ?option value?...",},
        {"delete", 1, (RbcOp) DeleteVirtualOp, 3, 0, "?axisName?...",},
        {"get", 1, (RbcOp) GetOp, 4, 4, "name",},
        {"invtransform", 1, (RbcOp) InvTransformVirtualOp, 5, 5,
            "axisName value",},
        {"limits", 1, (RbcOp) LimitsVirtualOp, 4, 4, "axisName",},
        {"names", 1, (RbcOp) NamesVirtualOp, 3, 0, "?pattern?...",},
        {"transform", 1, (RbcOp) TransformVirtualOp, 5, 5, "axisName value",},
        {"view", 1, (RbcOp) ViewOp, 4, 7,
            "axisName ?moveto fract? ?scroll number what?",},
    };
    static int      nAxisOps = sizeof(axisOps) / sizeof(RbcOpSpec);

    proc = RbcGetOp(interp, nAxisOps, axisOps, RBC_OP_ARG2, argc, argv, 0);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    result = (*proc) (graphPtr, argc, argv);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcAxisOp --
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
RbcAxisOp(
    RbcGraph * graphPtr,
    int margin,
    int argc,
    const char **argv)
{
    int             result;
    RbcOp           proc;
    RbcAxis        *axisPtr;
    static RbcOpSpec axisOps[] = {
        {"bind", 1, (RbcOp) BindOp, 2, 5, "sequence command",},
        {"cget", 2, (RbcOp) CgetOp, 4, 4, "option",},
        {"configure", 2, (RbcOp) ConfigureOp, 3, 0, "?option value?...",},
        {"invtransform", 1, (RbcOp) InvTransformOp, 4, 4, "value",},
        {"limits", 1, (RbcOp) LimitsOp, 3, 3, "",},
        {"transform", 1, (RbcOp) TransformOp, 4, 4, "value",},
        {"use", 1, (RbcOp) UseOp, 3, 4, "?axisName?",},
    };
    static int      nAxisOps = sizeof(axisOps) / sizeof(RbcOpSpec);

    proc = RbcGetOp(graphPtr->interp, nAxisOps, axisOps, RBC_OP_ARG2,
        argc, argv, 0);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    argv[2] = (char *) margin;  /* Hack. Slide a reference to the margin in
                                 * the argument list. Needed only for UseOp.
                                 */
    axisPtr = RbcGetFirstAxis(graphPtr->margins[margin].axes);
    result = (*proc) (graphPtr, axisPtr, argc - 3, argv + 3);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcMapAxes --
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
RbcMapAxes(
    RbcGraph * graphPtr)
{
    RbcAxis        *axisPtr;
    RbcChain       *chainPtr;
    RbcChainLink   *linkPtr;
    register int    margin;
    int             offset;

    for (margin = 0; margin < 4; margin++) {
        chainPtr = graphPtr->margins[margin].axes;
        offset = 0;
        for (linkPtr = RbcChainFirstLink(chainPtr); linkPtr != NULL;
            linkPtr = RbcChainNextLink(linkPtr)) {
            axisPtr = RbcChainGetValue(linkPtr);
            if ((!axisPtr->hidden) && (axisPtr->flags & AXIS_ONSCREEN)) {
                MapAxis(graphPtr, axisPtr, offset, margin);
                if (AxisIsHorizontal(graphPtr, axisPtr)) {
                    offset += axisPtr->height;
                } else {
                    offset += axisPtr->width;
                }
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcDrawAxes --
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
RbcDrawAxes(
    RbcGraph * graphPtr,
    Drawable drawable)
{
    RbcAxis        *axisPtr;
    RbcChainLink   *linkPtr;
    register int    i;

    for (i = 0; i < 4; i++) {
        for (linkPtr = RbcChainFirstLink(graphPtr->margins[i].axes);
            linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
            axisPtr = RbcChainGetValue(linkPtr);
            if ((!axisPtr->hidden) && (axisPtr->flags & AXIS_ONSCREEN)) {
                DrawAxis(graphPtr, drawable, axisPtr);
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcAxesToPostScript --
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
RbcAxesToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken)
{
    RbcAxis        *axisPtr;
    RbcChainLink   *linkPtr;
    register int    i;

    for (i = 0; i < 4; i++) {
        for (linkPtr = RbcChainFirstLink(graphPtr->margins[i].axes);
            linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
            axisPtr = RbcChainGetValue(linkPtr);
            if ((!axisPtr->hidden) && (axisPtr->flags & AXIS_ONSCREEN)) {
                AxisToPostScript(psToken, axisPtr);
            }
        }
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * RbcDrawAxisLimits --
 *
 *      Draws the min/max values of the axis in the plotting area.
 *      The text strings are formatted according to the "sprintf"
 *      format descriptors in the limitsFormats array.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Draws the numeric values of the axis limits into the outer
 *      regions of the plotting area.
 *
 * ----------------------------------------------------------------------
 */
void
RbcDrawAxisLimits(
    RbcGraph * graphPtr,
    Drawable drawable)
{
    RbcAxis        *axisPtr;
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  cursor;
    RbcDim2D        textDim;
    int             isHoriz;
    char           *minPtr, *maxPtr;
    const char     *minFormat, *maxFormat;
    char            minString[200], maxString[200];
    int             vMin, hMin, vMax, hMax;

#define SPACING 8
    vMin = vMax = graphPtr->left + graphPtr->padX.side1 + 2;
    hMin = hMax = graphPtr->bottom - graphPtr->padY.side2 - 2;  /* Offsets */

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (RbcAxis *) Tcl_GetHashValue(hPtr);

        if (axisPtr->nFormats == 0) {
            continue;
        }
        isHoriz = AxisIsHorizontal(graphPtr, axisPtr);
        minPtr = maxPtr = NULL;
        minFormat = maxFormat = axisPtr->limitsFormats[0];
        if (axisPtr->nFormats > 1) {
            maxFormat = axisPtr->limitsFormats[1];
        }
        if (minFormat[0] != '\0') {
            minPtr = minString;
            sprintf(minString, minFormat, axisPtr->axisRange.min);
        }
        if (maxFormat[0] != '\0') {
            maxPtr = maxString;
            sprintf(maxString, maxFormat, axisPtr->axisRange.max);
        }
        if (axisPtr->descending) {
            char           *tmp;

            tmp = minPtr, minPtr = maxPtr, maxPtr = tmp;
        }
        if (maxPtr != NULL) {
            if (isHoriz) {
                axisPtr->limitsTextStyle.theta = 90.0;
                axisPtr->limitsTextStyle.anchor = TK_ANCHOR_SE;
                RbcDrawText2(graphPtr->tkwin, drawable, maxPtr,
                    &axisPtr->limitsTextStyle, graphPtr->right, hMax, &textDim);
                hMax -= (textDim.height + SPACING);
            } else {
                axisPtr->limitsTextStyle.theta = 0.0;
                axisPtr->limitsTextStyle.anchor = TK_ANCHOR_NW;
                RbcDrawText2(graphPtr->tkwin, drawable, maxPtr,
                    &axisPtr->limitsTextStyle, vMax, graphPtr->top, &textDim);
                vMax += (textDim.width + SPACING);
            }
        }
        if (minPtr != NULL) {
            axisPtr->limitsTextStyle.anchor = TK_ANCHOR_SW;
            if (isHoriz) {
                axisPtr->limitsTextStyle.theta = 90.0;
                RbcDrawText2(graphPtr->tkwin, drawable, minPtr,
                    &axisPtr->limitsTextStyle, graphPtr->left, hMin, &textDim);
                hMin -= (textDim.height + SPACING);
            } else {
                axisPtr->limitsTextStyle.theta = 0.0;
                RbcDrawText2(graphPtr->tkwin, drawable, minPtr,
                    &axisPtr->limitsTextStyle, vMin, graphPtr->bottom,
                    &textDim);
                vMin += (textDim.width + SPACING);
            }
        }
    }                           /* Loop on axes */
}

/*
 *----------------------------------------------------------------------
 *
 * RbcAxisLimitsToPostScript --
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
RbcAxisLimitsToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken)
{
    RbcAxis        *axisPtr;
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  cursor;
    double          vMin, hMin, vMax, hMax;
    char            string[200];
    int             textWidth, textHeight;
    const char     *minFmt, *maxFmt;

#define SPACING 8
    vMin = vMax = graphPtr->left + graphPtr->padX.side1 + 2;
    hMin = hMax = graphPtr->bottom - graphPtr->padY.side2 - 2;  /* Offsets */
    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (RbcAxis *) Tcl_GetHashValue(hPtr);

        if (axisPtr->nFormats == 0) {
            continue;
        }
        minFmt = maxFmt = axisPtr->limitsFormats[0];
        if (axisPtr->nFormats > 1) {
            maxFmt = axisPtr->limitsFormats[1];
        }
        if (*maxFmt != '\0') {
            sprintf(string, maxFmt, axisPtr->axisRange.max);
            RbcGetTextExtents(&axisPtr->tickTextStyle, string, &textWidth,
                &textHeight);
            if ((textWidth > 0) && (textHeight > 0)) {
                if (axisPtr->classUid == rbcXAxisUid) {
                    axisPtr->limitsTextStyle.theta = 90.0;
                    axisPtr->limitsTextStyle.anchor = TK_ANCHOR_SE;
                    RbcTextToPostScript(psToken, string,
                        &axisPtr->limitsTextStyle,
                        (double) graphPtr->right, hMax);
                    hMax -= (textWidth + SPACING);
                } else {
                    axisPtr->limitsTextStyle.theta = 0.0;
                    axisPtr->limitsTextStyle.anchor = TK_ANCHOR_NW;
                    RbcTextToPostScript(psToken, string,
                        &axisPtr->limitsTextStyle, vMax,
                        (double) graphPtr->top);
                    vMax += (textWidth + SPACING);
                }
            }
        }
        if (*minFmt != '\0') {
            sprintf(string, minFmt, axisPtr->axisRange.min);
            RbcGetTextExtents(&axisPtr->tickTextStyle, string, &textWidth,
                &textHeight);
            if ((textWidth > 0) && (textHeight > 0)) {
                axisPtr->limitsTextStyle.anchor = TK_ANCHOR_SW;
                if (axisPtr->classUid == rbcXAxisUid) {
                    axisPtr->limitsTextStyle.theta = 90.0;
                    RbcTextToPostScript(psToken, string,
                        &axisPtr->limitsTextStyle,
                        (double) graphPtr->left, hMin);
                    hMin -= (textWidth + SPACING);
                } else {
                    axisPtr->limitsTextStyle.theta = 0.0;
                    RbcTextToPostScript(psToken, string,
                        &axisPtr->limitsTextStyle,
                        vMin, (double) graphPtr->bottom);
                    vMin += (textWidth + SPACING);
                }
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcGetFirstAxis --
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
RbcAxis        *
RbcGetFirstAxis(
    RbcChain * chainPtr)
{
    RbcChainLink   *linkPtr;

    linkPtr = RbcChainFirstLink(chainPtr);
    if (linkPtr == NULL) {
        return NULL;
    }
    return RbcChainGetValue(linkPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcNearestAxis --
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
RbcAxis        *
RbcNearestAxis(
    RbcGraph * graphPtr,
    int x,                      /* Point to be tested */
    int y)
{                               /* Point to be tested */
    register Tcl_HashEntry *hPtr;
    Tcl_HashSearch  cursor;
    RbcAxis        *axisPtr;
    int             width, height;
    double          rotWidth, rotHeight;
    RbcPoint2D      bbox[5];

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (RbcAxis *) Tcl_GetHashValue(hPtr);
        if ((axisPtr->hidden) || (!(axisPtr->flags & AXIS_ONSCREEN))) {
            continue;           /* Don't check hidden axes or axes
                                 * that are virtual. */
        }
        if (axisPtr->showTicks) {
            register RbcChainLink *linkPtr;
            TickLabel      *labelPtr;
            RbcPoint2D      t;

            for (linkPtr = RbcChainFirstLink(axisPtr->tickLabels);
                linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
                labelPtr = RbcChainGetValue(linkPtr);
                RbcGetBoundingBox(labelPtr->width, labelPtr->height,
                    axisPtr->tickTextStyle.theta, &rotWidth, &rotHeight, bbox);
                width = ROUND(rotWidth);
                height = ROUND(rotHeight);
                t = RbcTranslatePoint(&labelPtr->anchorPos, width, height,
                    axisPtr->tickTextStyle.anchor);
                t.x = x - t.x - (width * 0.5);
                t.y = y - t.y - (height * 0.5);

                bbox[4] = bbox[0];
                if (RbcPointInPolygon(&t, bbox, 5)) {
                    axisPtr->detail = "label";
                    return axisPtr;
                }
            }
        }
        if (axisPtr->title != NULL) {   /* and then the title string. */
            RbcPoint2D      t;

            RbcGetTextExtents(&axisPtr->titleTextStyle, axisPtr->title, &width,
                &height);
            RbcGetBoundingBox(width, height, axisPtr->titleTextStyle.theta,
                &rotWidth, &rotHeight, bbox);
            width = ROUND(rotWidth);
            height = ROUND(rotHeight);
            t = RbcTranslatePoint(&axisPtr->titlePos, width, height,
                axisPtr->titleTextStyle.anchor);
            /* Translate the point so that the 0,0 is the upper left
             * corner of the bounding box.  */
            t.x = x - t.x - (width / 2);
            t.y = y - t.y - (height / 2);

            bbox[4] = bbox[0];
            if (RbcPointInPolygon(&t, bbox, 5)) {
                axisPtr->detail = "title";
                return axisPtr;
            }
        }
        if (axisPtr->lineWidth > 0) {   /* Check for the axis region */
            if (RbcPointInRegion(&axisPtr->region, x, y)) {
                axisPtr->detail = "line";
                return axisPtr;
            }
        }
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcMakeAxisTag --
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
RbcMakeAxisTag(
    RbcGraph * graphPtr,
    const char *tagName)
{
    Tcl_HashEntry  *hPtr;
    int             isNew;

    hPtr = Tcl_CreateHashEntry(&graphPtr->axes.tagTable, tagName, &isNew);
    assert(hPtr);
    return Tcl_GetHashKey(&graphPtr->axes.tagTable, hPtr);
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
