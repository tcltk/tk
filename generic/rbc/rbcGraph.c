/*
 * rbcGraph.c --
 *
 *      This module implements a graph widget for the rbc toolkit.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

/*
 * To do:
 *
 * 2) Update manual pages.
 *
 * 3) Update comments.
 *
 * 5) Surface, contour, and flow graphs
 *
 * 7) Arrows for line markers
 *
 */

#include "rbcInt.h"

RbcUid          rbcXAxisUid;
RbcUid          rbcYAxisUid;
RbcUid          rbcBarElementUid;
RbcUid          rbcLineElementUid;
RbcUid          rbcStripElementUid;
RbcUid          rbcContourElementUid;
RbcUid          rbcLineMarkerUid;
RbcUid          rbcBitmapMarkerUid;
RbcUid          rbcImageMarkerUid;
RbcUid          rbcTextMarkerUid;
RbcUid          rbcPolygonMarkerUid;
RbcUid          rbcWindowMarkerUid;

extern Tk_CustomOption rbcLinePenOption;
extern Tk_CustomOption rbcBarPenOption;
extern Tk_CustomOption rbcDistanceOption;
extern Tk_CustomOption rbcBarModeOption;
extern Tk_CustomOption rbcPadOption;
extern Tk_CustomOption rbcTileOption;
extern Tk_CustomOption rbcShadowOption;

#define DEF_GRAPH_ASPECT_RATIO      "0.0"
#define DEF_GRAPH_BAR_BASELINE      "0.0"
#define DEF_GRAPH_BAR_MODE          "normal"
#define DEF_GRAPH_BAR_WIDTH         "0.8"
#define DEF_GRAPH_BACKGROUND        RBC_NORMAL_BACKGROUND
#define DEF_GRAPH_BG_MONO           RBC_NORMAL_BG_MONO
#define DEF_GRAPH_BORDERWIDTH       RBC_BORDERWIDTH
#define DEF_GRAPH_BUFFER_ELEMENTS   "1"
#define DEF_GRAPH_BUFFER_GRAPH	    "1"
#define DEF_GRAPH_CURSOR            "crosshair"
#define DEF_GRAPH_FONT              RBC_FONT_LARGE
#define DEF_GRAPH_HALO              "2m"
#define DEF_GRAPH_HALO_BAR          "0.1i"
#define DEF_GRAPH_HEIGHT            "4i"
#define DEF_GRAPH_HIGHLIGHT_BACKGROUND  RBC_NORMAL_BACKGROUND
#define DEF_GRAPH_HIGHLIGHT_BG_MONO RBC_NORMAL_BG_MONO
#define DEF_GRAPH_HIGHLIGHT_COLOR   "black"
#define DEF_GRAPH_HIGHLIGHT_WIDTH   "2"
#define DEF_GRAPH_INVERT_XY         "0"
#define DEF_GRAPH_JUSTIFY           "center"
#define DEF_GRAPH_MARGIN            "0"
#define DEF_GRAPH_MARGIN_VAR        (char *)NULL
#define DEF_GRAPH_PLOT_BACKGROUND   "white"
#define DEF_GRAPH_PLOT_BG_MONO      "white"
#define DEF_GRAPH_PLOT_BW_COLOR     RBC_BORDERWIDTH
#define DEF_GRAPH_PLOT_BW_MONO      "0"
#define DEF_GRAPH_PLOT_PADX         "8"
#define DEF_GRAPH_PLOT_PADY         "8"
#define DEF_GRAPH_PLOT_RELIEF       "sunken"
#define DEF_GRAPH_RELIEF            "flat"
#define DEF_GRAPH_SHADOW_COLOR      (char *)NULL
#define DEF_GRAPH_SHADOW_MONO       (char *)NULL
#define DEF_GRAPH_SHOW_VALUES       "no"
#define DEF_GRAPH_TAKE_FOCUS        ""
#define DEF_GRAPH_TITLE             (char *)NULL
#define DEF_GRAPH_TITLE_COLOR       RBC_NORMAL_FOREGROUND
#define DEF_GRAPH_TITLE_MONO        RBC_NORMAL_FG_MONO
#define DEF_GRAPH_WIDTH             "5i"
#define DEF_GRAPH_DATA              (char *)NULL
#define DEF_GRAPH_DATA_COMMAND      (char *)NULL

static Tk_ConfigSpec configSpecs[] = {
    {TK_CONFIG_DOUBLE, "-aspect", "aspect", "Aspect", DEF_GRAPH_ASPECT_RATIO,
        Tk_Offset(RbcGraph, aspect), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BORDER, "-background", "background", "Background",
            DEF_GRAPH_BACKGROUND, Tk_Offset(RbcGraph, border),
        TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_BORDER, "-background", "background", "Background",
            DEF_GRAPH_BG_MONO, Tk_Offset(RbcGraph, border),
        TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_CUSTOM, "-barmode", "barMode", "BarMode", DEF_GRAPH_BAR_MODE,
            Tk_Offset(RbcGraph, mode), TK_CONFIG_DONT_SET_DEFAULT,
        &rbcBarModeOption},
    {TK_CONFIG_DOUBLE, "-barwidth", "barWidth", "BarWidth", DEF_GRAPH_BAR_WIDTH,
        Tk_Offset(RbcGraph, barWidth), 0},
    {TK_CONFIG_DOUBLE, "-baseline", "baseline", "Baseline",
        DEF_GRAPH_BAR_BASELINE, Tk_Offset(RbcGraph, baseline), 0},
    {TK_CONFIG_SYNONYM, "-bd", "borderWidth", (char *) NULL, (char *) NULL, 0,
        0},
    {TK_CONFIG_SYNONYM, "-bg", "background", (char *) NULL, (char *) NULL, 0,
        0},
    {TK_CONFIG_SYNONYM, "-bm", "bottomMargin", (char *) NULL, (char *) NULL, 0,
        0},
    {TK_CONFIG_CUSTOM, "-borderwidth", "borderWidth", "BorderWidth",
            DEF_GRAPH_BORDERWIDTH, Tk_Offset(RbcGraph, borderWidth),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-bottommargin", "bottomMargin", "Margin",
            DEF_GRAPH_MARGIN, Tk_Offset(RbcGraph, margins[RBC_MARGIN_BOTTOM].reqSize), 0,
        &rbcDistanceOption},
    {TK_CONFIG_STRING, "-bottomvariable", "bottomVariable", "BottomVariable",
            DEF_GRAPH_MARGIN_VAR, Tk_Offset(RbcGraph, margins[RBC_MARGIN_BOTTOM].varName),
        TK_CONFIG_NULL_OK},
    {TK_CONFIG_BOOLEAN, "-bufferelements", "bufferElements", "BufferElements",
            DEF_GRAPH_BUFFER_ELEMENTS, Tk_Offset(RbcGraph, backingStore),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-buffergraph", "bufferGraph", "BufferGraph",
            DEF_GRAPH_BUFFER_GRAPH, Tk_Offset(RbcGraph, doubleBuffer),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_ACTIVE_CURSOR, "-cursor", "cursor", "Cursor", DEF_GRAPH_CURSOR,
        Tk_Offset(RbcGraph, cursor), TK_CONFIG_NULL_OK},
    {TK_CONFIG_STRING, "-data", "data", "Data", (char *) NULL,
        Tk_Offset(RbcGraph, data), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_STRING, "-datacommand", "dataCommand", "DataCommand",
            (char *) NULL, Tk_Offset(RbcGraph, dataCmd),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_SYNONYM, "-fg", "foreground", (char *) NULL, (char *) NULL, 0,
        0},
    {TK_CONFIG_FONT, "-font", "font", "Font", DEF_GRAPH_FONT,
        Tk_Offset(RbcGraph, titleTextStyle.font), 0},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground",
            DEF_GRAPH_TITLE_COLOR, Tk_Offset(RbcGraph, titleTextStyle.color),
        TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground",
            DEF_GRAPH_TITLE_MONO, Tk_Offset(RbcGraph, titleTextStyle.color),
        TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_CUSTOM, "-halo", "halo", "Halo", DEF_GRAPH_HALO,
        Tk_Offset(RbcGraph, halo), 0, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-height", "height", "Height", DEF_GRAPH_HEIGHT,
        Tk_Offset(RbcGraph, reqHeight), 0, &rbcDistanceOption},
    {TK_CONFIG_COLOR, "-highlightbackground", "highlightBackground",
            "HighlightBackground", DEF_GRAPH_HIGHLIGHT_BACKGROUND,
        Tk_Offset(RbcGraph, highlightBgColor), TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_COLOR, "-highlightbackground", "highlightBackground",
            "HighlightBackground", DEF_GRAPH_HIGHLIGHT_BG_MONO,
        Tk_Offset(RbcGraph, highlightBgColor), TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_COLOR, "-highlightcolor", "highlightColor", "HighlightColor",
        DEF_GRAPH_HIGHLIGHT_COLOR, Tk_Offset(RbcGraph, highlightColor), 0},
    {TK_CONFIG_PIXELS, "-highlightthickness", "highlightThickness",
            "HighlightThickness", DEF_GRAPH_HIGHLIGHT_WIDTH, Tk_Offset(RbcGraph,
            highlightWidth), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-invertxy", "invertXY", "InvertXY",
            DEF_GRAPH_INVERT_XY, Tk_Offset(RbcGraph, inverted),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_JUSTIFY, "-justify", "justify", "Justify", DEF_GRAPH_JUSTIFY,
            Tk_Offset(RbcGraph, titleTextStyle.justify),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-leftmargin", "leftMargin", "Margin", DEF_GRAPH_MARGIN,
            Tk_Offset(RbcGraph, margins[RBC_MARGIN_LEFT].reqSize), TK_CONFIG_DONT_SET_DEFAULT,
        &rbcDistanceOption},
    {TK_CONFIG_STRING, "-leftvariable", "leftVariable", "LeftVariable",
            DEF_GRAPH_MARGIN_VAR, Tk_Offset(RbcGraph, margins[RBC_MARGIN_LEFT].varName),
        TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-lm", "leftMargin", (char *) NULL, (char *) NULL, 0,
        0},
    {TK_CONFIG_COLOR, "-plotbackground", "plotBackground", "Background",
            DEF_GRAPH_PLOT_BG_MONO, Tk_Offset(RbcGraph, plotBg),
        TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_COLOR, "-plotbackground", "plotBackground", "Background",
            DEF_GRAPH_PLOT_BACKGROUND, Tk_Offset(RbcGraph, plotBg),
        TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_CUSTOM, "-plotborderwidth", "plotBorderWidth", "BorderWidth",
            DEF_GRAPH_PLOT_BW_COLOR, Tk_Offset(RbcGraph, plotBorderWidth),
        TK_CONFIG_COLOR_ONLY, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-plotborderwidth", "plotBorderWidth", "BorderWidth",
            DEF_GRAPH_PLOT_BW_MONO, Tk_Offset(RbcGraph, plotBorderWidth),
        TK_CONFIG_MONO_ONLY, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-plotpadx", "plotPadX", "PlotPad", DEF_GRAPH_PLOT_PADX,
            Tk_Offset(RbcGraph, padX), TK_CONFIG_DONT_SET_DEFAULT,
        &rbcPadOption},
    {TK_CONFIG_CUSTOM, "-plotpady", "plotPadY", "PlotPad", DEF_GRAPH_PLOT_PADY,
            Tk_Offset(RbcGraph, padY), TK_CONFIG_DONT_SET_DEFAULT,
        &rbcPadOption},
    {TK_CONFIG_RELIEF, "-plotrelief", "plotRelief", "Relief",
            DEF_GRAPH_PLOT_RELIEF, Tk_Offset(RbcGraph, plotRelief),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_RELIEF, "-relief", "relief", "Relief", DEF_GRAPH_RELIEF,
        Tk_Offset(RbcGraph, relief), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-rightmargin", "rightMargin", "Margin",
            DEF_GRAPH_MARGIN, Tk_Offset(RbcGraph, margins[RBC_MARGIN_RIGHT].reqSize),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_STRING, "-rightvariable", "rightVariable", "RightVariable",
            DEF_GRAPH_MARGIN_VAR, Tk_Offset(RbcGraph, margins[RBC_MARGIN_RIGHT].varName),
        TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-rm", "rightMargin", (char *) NULL, (char *) NULL, 0,
        0},
    {TK_CONFIG_CUSTOM, "-shadow", "shadow", "Shadow", DEF_GRAPH_SHADOW_COLOR,
            Tk_Offset(RbcGraph, titleTextStyle.shadow), TK_CONFIG_COLOR_ONLY,
        &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-shadow", "shadow", "Shadow", DEF_GRAPH_SHADOW_MONO,
            Tk_Offset(RbcGraph, titleTextStyle.shadow), TK_CONFIG_MONO_ONLY,
        &rbcShadowOption},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground",
            DEF_GRAPH_TITLE_MONO, Tk_Offset(RbcGraph, titleTextStyle.color),
        TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_STRING, "-takefocus", "takeFocus", "TakeFocus",
            DEF_GRAPH_TAKE_FOCUS, Tk_Offset(RbcGraph, takeFocus),
        TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-tile", "tile", "Tile", (char *) NULL,
        Tk_Offset(RbcGraph, tile), TK_CONFIG_NULL_OK, &rbcTileOption},
    {TK_CONFIG_STRING, "-title", "title", "Title", DEF_GRAPH_TITLE,
        Tk_Offset(RbcGraph, title), TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-tm", "topMargin", (char *) NULL, (char *) NULL, 0, 0},
    {TK_CONFIG_CUSTOM, "-topmargin", "topMargin", "Margin", DEF_GRAPH_MARGIN,
            Tk_Offset(RbcGraph, margins[RBC_MARGIN_TOP].reqSize), TK_CONFIG_DONT_SET_DEFAULT,
        &rbcDistanceOption},
    {TK_CONFIG_STRING, "-topvariable", "topVariable", "TopVariable",
            DEF_GRAPH_MARGIN_VAR, Tk_Offset(RbcGraph, margins[RBC_MARGIN_TOP].varName),
        TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-width", "width", "Width", DEF_GRAPH_WIDTH,
        Tk_Offset(RbcGraph, reqWidth), 0, &rbcDistanceOption},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

static RbcSwitchParseProc StringToFormat;
static RbcSwitchCustom formatSwitch = {
    StringToFormat, (RbcSwitchFreeProc *) NULL, (ClientData) 0,
};

typedef struct {
    char           *name;
    int             width, height;
    int             format;
} SnapData;

enum SnapFormats { FORMAT_PHOTO, FORMAT_EMF, FORMAT_WMF };

static RbcSwitchSpec snapSwitches[] = {
    {RBC_SWITCH_INT_POSITIVE, "-width", Tk_Offset(SnapData, width), 0},
    {RBC_SWITCH_INT_POSITIVE, "-height", Tk_Offset(SnapData, height), 0},
    {RBC_SWITCH_CUSTOM, "-format", Tk_Offset(SnapData, format), 0,
        &formatSwitch},
    {RBC_SWITCH_END, NULL, 0, 0}
};

static Tcl_IdleProc DisplayGraph;
static Tcl_FreeProc DestroyGraph;
static Tk_EventProc GraphEventProc;

static RbcBindPickProc PickEntry;
static Tcl_CmdProc StripchartCmd;
static Tcl_CmdProc BarchartCmd;
static Tcl_CmdProc GraphCmd;
static Tcl_CmdDeleteProc GraphInstCmdDeleteProc;
static RbcTileChangedProc TileChangedProc;

static void     AdjustAxisPointers(
    RbcGraph * graphPtr);
static int      InitPens(
    RbcGraph * graphPtr);
static RbcGraph *CreateGraph(
    Tcl_Interp * interp,
    int argc,
    const const char **argv,
    RbcUid classUid);
static void     ConfigureGraph(
    RbcGraph * graphPtr);
static int      NewGraph(
    Tcl_Interp * interp,
    int argc,
    const char **argv,
    RbcUid classUid);
static void     DrawMargins(
    RbcGraph * graphPtr,
    Drawable drawable);
static void     DrawPlotRegion(
    RbcGraph * graphPtr,
    Drawable drawable);
static void     UpdateMarginTraces(
    RbcGraph * graphPtr);

static int      XAxisOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      X2AxisOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      YAxisOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      Y2AxisOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      BarOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      LineOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      ElementOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      ConfigureOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const const char **argv);
static int      CgetOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      ExtentsOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      InsideOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      InvtransformOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      TransformOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      SnapOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);

#ifdef __WIN32
static int      InitMetaFileHeader(
    Tk_Window tkwin,
    int width,
    int height,
    APMHEADER * mfhPtr);
static int      CreateAPMetaFile(
    Tcl_Interp * interp,
    HANDLE hMetaFile,
    HDC hDC,
    APMHEADER * mfhPtr,
    char *fileName);
#endif

/*
 *--------------------------------------------------------------
 *
 * RbcEventuallyRedrawGraph --
 *
 *      Tells the Tk dispatcher to call the graph display routine at
 *      the next idle point.  This request is made only if the window
 *      is displayed and no other redraw request is pending.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The window is eventually redisplayed.
 *
 *--------------------------------------------------------------
 */
void
RbcEventuallyRedrawGraph(
    RbcGraph * graphPtr)
{                               /* Graph widget record */
    if ((graphPtr->tkwin != NULL) && !(graphPtr->flags & RBC_REDRAW_PENDING)) {
        Tcl_DoWhenIdle(DisplayGraph, graphPtr);
        graphPtr->flags |= RBC_REDRAW_PENDING;
    }
}

/*
 *--------------------------------------------------------------
 *
 * GraphEventProc --
 *
 *      This procedure is invoked by the Tk dispatcher for various
 *      events on graphs.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      When the window gets deleted, internal structures get
 *      cleaned up.  When it gets exposed, the graph is eventually
 *      redisplayed.
 *
 *--------------------------------------------------------------
 */
static void
GraphEventProc(
    ClientData clientData,      /* Graph widget record */
    register XEvent * eventPtr)
{                               /* Event which triggered call to routine */
    RbcGraph       *graphPtr = clientData;

    if (eventPtr->type == Expose) {
        if (eventPtr->xexpose.count == 0) {
            graphPtr->flags |= RBC_REDRAW_WORLD;
            RbcEventuallyRedrawGraph(graphPtr);
        }
    } else if ((eventPtr->type == FocusIn) || (eventPtr->type == FocusOut)) {
        if (eventPtr->xfocus.detail != NotifyInferior) {
            if (eventPtr->type == FocusIn) {
                graphPtr->flags |= RBC_GRAPH_FOCUS;
            } else {
                graphPtr->flags &= ~RBC_GRAPH_FOCUS;
            }
            graphPtr->flags |= RBC_REDRAW_WORLD;
            RbcEventuallyRedrawGraph(graphPtr);
        }
    } else if (eventPtr->type == DestroyNotify) {
        if (graphPtr->tkwin != NULL) {
            RbcDeleteWindowInstanceData(graphPtr->tkwin);
            graphPtr->tkwin = NULL;
            Tcl_DeleteCommandFromToken(graphPtr->interp, graphPtr->cmdToken);
        }
        if (graphPtr->flags & RBC_REDRAW_PENDING) {
            Tcl_CancelIdleCall(DisplayGraph, graphPtr);
        }
        Tcl_EventuallyFree(graphPtr, DestroyGraph);
    } else if (eventPtr->type == ConfigureNotify) {
        graphPtr->flags |= (RBC_MAP_WORLD | RBC_REDRAW_WORLD);
        RbcEventuallyRedrawGraph(graphPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GraphInstCmdDeleteProc --
 *
 *      This procedure is invoked when a widget command is deleted.  If
 *      the widget isn't already in the process of being destroyed,
 *      this command destroys it.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The widget is destroyed.
 *
 *---------------------------------------------------------------------- */
static void
GraphInstCmdDeleteProc(
    ClientData clientData)
{                               /* Pointer to widget record. */
    RbcGraph       *graphPtr = clientData;

    if (graphPtr->tkwin != NULL) {      /* NULL indicates window has
                                         * already been destroyed. */
        Tk_Window       tkwin;

        tkwin = graphPtr->tkwin;
        graphPtr->tkwin = NULL;
        RbcDeleteWindowInstanceData(tkwin);
        Tk_DestroyWindow(tkwin);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TileChangedProc --
 *
 *      Rebuilds the designated GC with the new tile pixmap.
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
TileChangedProc(
    ClientData clientData,
    RbcTile tile)
{                               /* Not used. */
    RbcGraph       *graphPtr = clientData;

    if (graphPtr->tkwin != NULL) {
        graphPtr->flags |= RBC_REDRAW_WORLD;
        RbcEventuallyRedrawGraph(graphPtr);
    }
}

/*
 *--------------------------------------------------------------
 *
 * AdjustAxisPointers --
 *
 *      Sets the axis pointers according to whether the axis is
 *      inverted on not.  The axis sites are also reset.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void
AdjustAxisPointers(
    RbcGraph * graphPtr)
{                               /* Graph widget record */
    if (graphPtr->inverted) {
        graphPtr->margins[RBC_MARGIN_LEFT].axes = graphPtr->axisChain[0];
        graphPtr->margins[RBC_MARGIN_BOTTOM].axes = graphPtr->axisChain[1];
        graphPtr->margins[RBC_MARGIN_RIGHT].axes = graphPtr->axisChain[2];
        graphPtr->margins[RBC_MARGIN_TOP].axes = graphPtr->axisChain[3];
    } else {
        graphPtr->margins[RBC_MARGIN_LEFT].axes = graphPtr->axisChain[1];
        graphPtr->margins[RBC_MARGIN_BOTTOM].axes = graphPtr->axisChain[0];
        graphPtr->margins[RBC_MARGIN_RIGHT].axes = graphPtr->axisChain[3];
        graphPtr->margins[RBC_MARGIN_TOP].axes = graphPtr->axisChain[2];
    }
}

/*
 *----------------------------------------------------------------------
 *
 * InitPens --
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
InitPens(
    RbcGraph * graphPtr)
{
    Tcl_InitHashTable(&graphPtr->penTable, TCL_STRING_KEYS);
    if (RbcCreatePen(graphPtr, "activeLine", rbcLineElementUid, 0,
            (const char **) NULL) == NULL) {
        return TCL_ERROR;
    }
    if (RbcCreatePen(graphPtr, "activeBar", rbcBarElementUid, 0,
            (const char **) NULL) == NULL) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * PickEntry --
 *
 *      Find the closest point from the set of displayed elements,
 *      searching the display list from back to front.  That way, if
 *      the points from two different elements overlay each other exactly,
 *      the one that's on top (visible) is picked.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static          ClientData
PickEntry(
    ClientData clientData,
    int x,
    int y,
    ClientData * contextPtr)
{                               /* Not used. */
    RbcGraph       *graphPtr = clientData;
    RbcChainLink   *linkPtr;
    RbcElement     *elemPtr;
    RbcMarker      *markerPtr;
    RbcExtents2D    exts;

    if (graphPtr->flags & RBC_MAP_ALL) {
        /* Can't pick anything until the next
         * redraw occurs. */
        return NULL;
    }
    RbcGraphExtents(graphPtr, &exts);

    if ((x > exts.right) || (x < exts.left) || (y > exts.bottom)
        || (y < exts.top)) {
        /*
         * Sample coordinate is in one of the graph margins.  Can only
         * pick an axis.
         */
        return RbcNearestAxis(graphPtr, x, y);
    }

    /*
     * From top-to-bottom check:
     *  1. markers drawn on top (-under false).
     *  2. elements using its display list back to front.
     *  3. markers drawn under element (-under true).
     */
    markerPtr = (RbcMarker *) RbcNearestMarker(graphPtr, x, y, FALSE);
    if (markerPtr != NULL) {
        /* Found a marker (-under false). */
        return markerPtr;
    }
    {
        RbcClosestSearch search;

        search.along = RBC_SEARCH_BOTH;
        search.halo = graphPtr->halo + 1;
        search.index = -1;
        search.x = x;
        search.y = y;
        search.dist = (double) (search.halo + 1);
        search.mode = RBC_SEARCH_AUTO;

        for (linkPtr = RbcChainLastLink(graphPtr->elements.displayList);
            linkPtr != NULL; linkPtr = RbcChainPrevLink(linkPtr)) {
            elemPtr = RbcChainGetValue(linkPtr);
            if ((elemPtr->flags & RBC_MAP_ITEM)
                || (RbcVectorNotifyPending(elemPtr->x.clientId))
                || (RbcVectorNotifyPending(elemPtr->y.clientId))) {
                continue;
            }
            if ((!elemPtr->hidden) && (elemPtr->state == RBC_STATE_NORMAL)) {
                (*elemPtr->procsPtr->closestProc) (graphPtr, elemPtr, &search);
            }
        }
        if (search.dist <= (double) search.halo) {
            /* Found an element within the
             * minimum halo distance. */
            return search.elemPtr;
        }
    }
    markerPtr = (RbcMarker *) RbcNearestMarker(graphPtr, x, y, TRUE);
    if (markerPtr != NULL) {
        /* Found a marker (-under true) */
        return markerPtr;
    }
    /* Nothing found. */
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureGraph --
 *
 *      Allocates resources for the graph.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Configuration information, such as text string, colors, font,
 *      etc. get set for graphPtr;  old resources get freed, if there
 *      were any.  The graph is redisplayed.
 *
 *----------------------------------------------------------------------
 */
static void
ConfigureGraph(
    RbcGraph * graphPtr)
{                               /* Graph widget record */
    XColor         *colorPtr;
    GC              newGC;
    XGCValues       gcValues;
    unsigned long   gcMask;

    /* Don't allow negative bar widths. Reset to an arbitrary value (0.1) */
    if (graphPtr->barWidth <= 0.0) {
        graphPtr->barWidth = 0.1;
    }
    graphPtr->inset = graphPtr->borderWidth + graphPtr->highlightWidth + 1;
    if ((graphPtr->reqHeight != Tk_ReqHeight(graphPtr->tkwin))
        || (graphPtr->reqWidth != Tk_ReqWidth(graphPtr->tkwin))) {
        Tk_GeometryRequest(graphPtr->tkwin, graphPtr->reqWidth,
            graphPtr->reqHeight);
    }
    Tk_SetInternalBorder(graphPtr->tkwin, graphPtr->borderWidth);
    colorPtr = Tk_3DBorderColor(graphPtr->border);

    if (graphPtr->title != NULL) {
        int             w, h;

        RbcGetTextExtents(&graphPtr->titleTextStyle, graphPtr->title, &w, &h);
        graphPtr->titleTextStyle.height = h + 10;
    } else {
        graphPtr->titleTextStyle.width = graphPtr->titleTextStyle.height = 0;
    }

    /*
     * Create GCs for interior and exterior regions, and a background
     * GC for clearing the margins with XFillRectangle
     */

    /* Margin GC */

    gcValues.foreground = graphPtr->titleTextStyle.color->pixel;
    gcValues.background = colorPtr->pixel;
    gcMask = (GCForeground | GCBackground);
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (graphPtr->drawGC != NULL) {
        Tk_FreeGC(graphPtr->display, graphPtr->drawGC);
    }
    graphPtr->drawGC = newGC;

    /* Plot fill GC (Background = Foreground) */

    gcValues.foreground = graphPtr->plotBg->pixel;
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (graphPtr->plotFillGC != NULL) {
        Tk_FreeGC(graphPtr->display, graphPtr->plotFillGC);
    }
    graphPtr->plotFillGC = newGC;

    /* Margin fill GC (Background = Foreground) */

    gcValues.foreground = colorPtr->pixel;
    gcValues.background = graphPtr->titleTextStyle.color->pixel;
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (graphPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, graphPtr->fillGC);
    }
    graphPtr->fillGC = newGC;
    if (graphPtr->tile != NULL) {
        RbcSetTileChangedProc(graphPtr->tile, TileChangedProc, graphPtr);
    }

    RbcResetTextStyle(graphPtr->tkwin, &graphPtr->titleTextStyle);

    if (RbcConfigModified(configSpecs, "-invertxy", (char *) NULL)) {

        /*
         * If the -inverted option changed, we need to readjust the pointers
         * to the axes and recompute the their scales.
         */

        AdjustAxisPointers(graphPtr);
        graphPtr->flags |= RBC_RESET_AXES;
    }
    if ((!graphPtr->backingStore) && (graphPtr->backPixmap != None)) {

        /*
         * Free the pixmap if we're not buffering the display of elements
         * anymore.
         */

        Tk_FreePixmap(graphPtr->display, graphPtr->backPixmap);
        graphPtr->backPixmap = None;
    }
    /*
     * Reconfigure the crosshairs, just in case the background color of
     * the plotarea has been changed.
     */
    RbcConfigureCrosshairs(graphPtr);

    /*
     *  Update the layout of the graph (and redraw the elements) if
     *  any of the following graph options which affect the size of
     *  the plotting area has changed.
     *
     *      -aspect
     *      -borderwidth, -plotborderwidth
     *      -font, -title
     *      -width, -height
     *      -invertxy
     *      -bottommargin, -leftmargin, -rightmargin, -topmargin,
     *      -barmode, -barwidth
     */
    if (RbcConfigModified(configSpecs, "-invertxy", "-title", "-font",
            "-*margin", "-*width", "-height", "-barmode", "-*pad*", "-aspect",
            (char *) NULL)) {
        graphPtr->flags |= RBC_RESET_WORLD;
    }
    if (RbcConfigModified(configSpecs, "-plotbackground", (char *) NULL)) {
        graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
    }
    graphPtr->flags |= RBC_REDRAW_WORLD;
    RbcEventuallyRedrawGraph(graphPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyGraph --
 *
 *      This procedure is invoked by Tcl_EventuallyFree or Tcl_Release
 *      to clean up the internal structure of a graph at a safe time
 *      (when no-one is using it anymore).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Everything associated with the widget is freed up.
 *
 *----------------------------------------------------------------------
 */
static void
DestroyGraph(
    char *dataPtr)
{
    RbcGraph       *graphPtr = (RbcGraph *) dataPtr;

    Tk_FreeOptions(configSpecs, (char *) graphPtr, graphPtr->display, 0);
    /*
     * Destroy the individual components of the graph: elements, markers,
     * X and Y axes, legend, display lists etc.
     */
    RbcDestroyMarkers(graphPtr);
    RbcDestroyElements(graphPtr);

    RbcDestroyAxes(graphPtr);
    RbcDestroyPens(graphPtr);

    if (graphPtr->legend != NULL) {
        RbcDestroyLegend(graphPtr);
    }
    if (graphPtr->postscript != NULL) {
        RbcDestroyPostScript(graphPtr);
    }
    if (graphPtr->crosshairs != NULL) {
        RbcDestroyCrosshairs(graphPtr);
    }
    if (graphPtr->gridPtr != NULL) {
        RbcDestroyGrid(graphPtr);
    }
    if (graphPtr->bindTable != NULL) {
        RbcDestroyBindingTable(graphPtr->bindTable);
    }

    /* Release allocated X resources and memory. */
    if (graphPtr->drawGC != NULL) {
        Tk_FreeGC(graphPtr->display, graphPtr->drawGC);
    }
    if (graphPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, graphPtr->fillGC);
    }
    if (graphPtr->plotFillGC != NULL) {
        Tk_FreeGC(graphPtr->display, graphPtr->plotFillGC);
    }
    RbcFreeTextStyle(graphPtr->display, &graphPtr->titleTextStyle);
    if (graphPtr->backPixmap != None) {
        Tk_FreePixmap(graphPtr->display, graphPtr->backPixmap);
    }
    if (graphPtr->freqArr != NULL) {
        ckfree((char *) graphPtr->freqArr);
    }
    if (graphPtr->nStacks > 0) {
        Tcl_DeleteHashTable(&graphPtr->freqTable);
    }
    if (graphPtr->tile != NULL) {
        RbcFreeTile(graphPtr->tile);
    }
    ckfree((char *) graphPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * CreateGraph --
 *
 *      This procedure creates and initializes a new widget.
 *
 * Results:
 *      The return value is a pointer to a structure describing
 *      the new widget.  If an error occurred, then the return
 *      value is NULL and an error message is left in interp->result.
 *
 * Side effects:
 *      Memory is allocated, a Tk_Window is created, etc.
 *
 *----------------------------------------------------------------------
 */
static RbcGraph *
CreateGraph(
    Tcl_Interp * interp,
    int argc,
    const char **argv,
    RbcUid classUid)
{
    RbcGraph       *graphPtr;
    Tk_Window       tkwin;

    tkwin = Tk_CreateWindowFromPath(interp, Tk_MainWindow(interp), argv[1],
        (char *) NULL);
    if (tkwin == NULL) {
        return NULL;
    }
    graphPtr = RbcCalloc(1, sizeof(RbcGraph));
    assert(graphPtr);
    /* Initialize the graph data structure. */

    graphPtr->tkwin = tkwin;
    graphPtr->display = Tk_Display(tkwin);
    graphPtr->interp = interp;
    graphPtr->classUid = classUid;
    graphPtr->backingStore = TRUE;
    graphPtr->doubleBuffer = TRUE;
    graphPtr->highlightWidth = 2;
    graphPtr->plotRelief = TK_RELIEF_SUNKEN;
    graphPtr->relief = TK_RELIEF_FLAT;
    graphPtr->flags = (RBC_RESET_WORLD);
    graphPtr->nextMarkerId = 1;
    graphPtr->padX.side1 = graphPtr->padX.side2 = 8;
    graphPtr->padY.side1 = graphPtr->padY.side2 = 8;
    graphPtr->margins[RBC_MARGIN_BOTTOM].site = RBC_MARGIN_BOTTOM;
    graphPtr->margins[RBC_MARGIN_LEFT].site = RBC_MARGIN_LEFT;
    graphPtr->margins[RBC_MARGIN_TOP].site = RBC_MARGIN_TOP;
    graphPtr->margins[RBC_MARGIN_RIGHT].site = RBC_MARGIN_RIGHT;
    RbcInitTextStyle(&graphPtr->titleTextStyle);

    Tcl_InitHashTable(&graphPtr->axes.table, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graphPtr->axes.tagTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graphPtr->elements.table, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graphPtr->elements.tagTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graphPtr->markers.table, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graphPtr->markers.tagTable, TCL_STRING_KEYS);
    graphPtr->elements.displayList = RbcChainCreate();
    graphPtr->markers.displayList = RbcChainCreate();
    graphPtr->axes.displayList = RbcChainCreate();

    if (classUid == rbcLineElementUid) {
        Tk_SetClass(tkwin, "Graph");
    } else if (classUid == rbcBarElementUid) {
        Tk_SetClass(tkwin, "Barchart");
    } else if (classUid == rbcStripElementUid) {
        Tk_SetClass(tkwin, "Stripchart");
    }
    RbcSetWindowInstanceData(tkwin, graphPtr);

    if (InitPens(graphPtr) != TCL_OK) {
        goto error;
    }
    if (Tk_ConfigureWidget(interp, tkwin, configSpecs, argc - 2, argv + 2,
            (char *) graphPtr, 0) != TCL_OK) {
        goto error;
    }
    if (RbcDefaultAxes(graphPtr) != TCL_OK) {
        goto error;
    }
    AdjustAxisPointers(graphPtr);

    if (RbcCreatePostScript(graphPtr) != TCL_OK) {
        goto error;
    }
    if (RbcCreateCrosshairs(graphPtr) != TCL_OK) {
        goto error;
    }
    if (RbcCreateLegend(graphPtr) != TCL_OK) {
        goto error;
    }
    if (RbcCreateGrid(graphPtr) != TCL_OK) {
        goto error;
    }
    Tk_CreateEventHandler(graphPtr->tkwin,
        ExposureMask | StructureNotifyMask | FocusChangeMask, GraphEventProc,
        graphPtr);

    graphPtr->cmdToken =
        Tcl_CreateCommand(interp, argv[1], RbcGraphInstCmdProc, graphPtr,
        GraphInstCmdDeleteProc);
    ConfigureGraph(graphPtr);
    graphPtr->bindTable =
        RbcCreateBindingTable(interp, tkwin, graphPtr, PickEntry);
    return graphPtr;

  error:
    DestroyGraph((char *) graphPtr);
    return NULL;
}

/* Widget sub-commands */

/*
 *----------------------------------------------------------------------
 *
 * XAxisOp --
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
XAxisOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    int             margin;

    margin = (graphPtr->inverted) ? RBC_MARGIN_LEFT : RBC_MARGIN_BOTTOM;
    return RbcAxisOp(graphPtr, margin, argc, argv);
}

/*
 *----------------------------------------------------------------------
 *
 * X2AxisOp --
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
X2AxisOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    int             margin;

    margin = (graphPtr->inverted) ? RBC_MARGIN_RIGHT : RBC_MARGIN_TOP;
    return RbcAxisOp(graphPtr, margin, argc, argv);
}

/*
 *----------------------------------------------------------------------
 *
 * YAxisOp --
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
YAxisOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    int             margin;

    margin = (graphPtr->inverted) ? RBC_MARGIN_BOTTOM : RBC_MARGIN_LEFT;
    return RbcAxisOp(graphPtr, margin, argc, argv);
}

/*
 *----------------------------------------------------------------------
 *
 * Y2AxisOp --
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
Y2AxisOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    int             margin;

    margin = (graphPtr->inverted) ? RBC_MARGIN_TOP : RBC_MARGIN_RIGHT;
    return RbcAxisOp(graphPtr, margin, argc, argv);
}

/*
 *----------------------------------------------------------------------
 *
 * BarOp --
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
BarOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    return RbcElementOp(graphPtr, interp, argc, argv, rbcBarElementUid);
}

/*
 *----------------------------------------------------------------------
 *
 * LineOp --
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
LineOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    return RbcElementOp(graphPtr, interp, argc, argv, rbcLineElementUid);
}

/*
 *----------------------------------------------------------------------
 *
 * ElementOp --
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
ElementOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    return RbcElementOp(graphPtr, interp, argc, argv, graphPtr->classUid);
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureOp --
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
ConfigureOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    int             flags;

    flags = TK_CONFIG_ARGV_ONLY;
    if (argc == 2) {
        return Tk_ConfigureInfo(interp, graphPtr->tkwin, configSpecs,
            (char *) graphPtr, (char *) NULL, flags);
    } else if (argc == 3) {
        return Tk_ConfigureInfo(interp, graphPtr->tkwin, configSpecs,
            (char *) graphPtr, argv[2], flags);
    } else {
        if (Tk_ConfigureWidget(interp, graphPtr->tkwin, configSpecs, argc - 2,
                argv + 2, (char *) graphPtr, flags) != TCL_OK) {
            return TCL_ERROR;
        }
        ConfigureGraph(graphPtr);
        return TCL_OK;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CgetOp --
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
CgetOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,                   /* Not used. */
    const char **argv)
{
    return Tk_ConfigureValue(interp, graphPtr->tkwin, configSpecs,
        (char *) graphPtr, argv[2], 0);
}

/*
 *--------------------------------------------------------------
 *
 * ExtentsOp --
 *
 *      Reports the size of one of several items within the graph.
 *      The following are valid items:
 *
 *        "bottommargin"    Height of the bottom margin
 *        "leftmargin"      Width of the left margin
 *        "legend"          x y w h of the legend
 *        "plotarea"        x y w h of the plotarea
 *        "plotheight"      Height of the plot area
 *        "rightmargin"     Width of the right margin
 *        "topmargin"       Height of the top margin
 *        "plotwidth"       Width of the plot area
 *
 * Results:
 *      Always returns TCL_OK.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int
ExtentsOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,                   /* Not used. */
    const char **argv)
{
    char            c;
    unsigned int    length;
    char            string[200];

    c = argv[2][0];
    length = strlen(argv[2]);
    if ((c == 'p') && (length > 4)
        && (strncmp("plotheight", argv[2], length) == 0)) {
        Tcl_SetObjResult(interp,
            Tcl_NewIntObj(graphPtr->bottom - graphPtr->top + 1));
    } else if ((c == 'p') && (length > 4)
        && (strncmp("plotwidth", argv[2], length) == 0)) {
        Tcl_SetObjResult(interp,
            Tcl_NewIntObj(graphPtr->right - graphPtr->left + 1));
    } else if ((c == 'p') && (length > 4)
        && (strncmp("plotarea", argv[2], length) == 0)) {
        sprintf(string, "%d %d %d %d", graphPtr->left, graphPtr->top,
            graphPtr->right - graphPtr->left + 1,
            graphPtr->bottom - graphPtr->top + 1);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(string, -1));
    } else if ((c == 'l') && (length > 2)
        && (strncmp("legend", argv[2], length) == 0)) {
        sprintf(string, "%d %d %d %d", RbcLegendX(graphPtr->legend),
            RbcLegendY(graphPtr->legend), RbcLegendWidth(graphPtr->legend),
            RbcLegendHeight(graphPtr->legend));
        Tcl_SetObjResult(interp, Tcl_NewStringObj(string, -1));
    } else if ((c == 'l') && (length > 2)
        && (strncmp("leftmargin", argv[2], length) == 0)) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(graphPtr->margins[RBC_MARGIN_LEFT].width));
    } else if ((c == 'r') && (length > 1)
        && (strncmp("rightmargin", argv[2], length) == 0)) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(graphPtr->margins[RBC_MARGIN_RIGHT].width));
    } else if ((c == 't') && (length > 1)
        && (strncmp("topmargin", argv[2], length) == 0)) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(graphPtr->margins[RBC_MARGIN_TOP].height));
    } else if ((c == 'b') && (length > 1)
        && (strncmp("bottommargin", argv[2], length) == 0)) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(graphPtr->margins[RBC_MARGIN_BOTTOM].height));
    } else {
        Tcl_AppendResult(interp, "bad extent item \"", argv[2],
            "\": should be plotheight, plotwidth, leftmargin, rightmargin, \
topmargin, bottommargin, plotarea, or legend", (char *) NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * InsideOp --
 *
 *      Returns true of false whether the given point is inside
 *      the plotting area (defined by left,bottom right, top).
 *
 * Results:
 *      Always returns TCL_OK.  interp->result will contain
 *      the boolean string representation.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int
InsideOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,                   /* Not used. */
    const char **argv)
{
    int             x, y;
    RbcExtents2D    exts;
    int             result;

    if (Tk_GetPixels(interp, graphPtr->tkwin, argv[2], &x) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tk_GetPixels(interp, graphPtr->tkwin, argv[3], &y) != TCL_OK) {
        return TCL_ERROR;
    }
    RbcGraphExtents(graphPtr, &exts);
    result = RbcPointInRegion(&exts, x, y);
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(result));
    return TCL_OK;
}

/*
 * -------------------------------------------------------------------------
 *
 * InvtransformOp --
 *
 *      This procedure returns a list of the graph coordinate
 *      values corresponding with the given window X and Y
 *      coordinate positions.
 *
 * Results:
 *      Returns a standard Tcl result.  If an error occurred while
 *      parsing the window positions, TCL_ERROR is returned, and
 *      interp->result will contain the error message.  Otherwise
 *      interp->result will contain a Tcl list of the x and y
 *      coordinates.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ------------------------------------------------------------------------
 */
static int
InvtransformOp(
    RbcGraph * graphPtr,        /* Graph widget record */
    Tcl_Interp * interp,
    int argc,                   /* Not used. */
    const char **argv)
{
    double          x, y;
    RbcPoint2D      point;
    RbcAxis2D       axes;

    if (Tcl_ExprDouble(interp, argv[2], &x) != TCL_OK ||
        Tcl_ExprDouble(interp, argv[3], &y) != TCL_OK) {
        return TCL_ERROR;
    }
    if (graphPtr->flags & RBC_RESET_AXES) {
        RbcResetAxes(graphPtr);
    }
    /* Perform the reverse transformation, converting from window
     * coordinates to graph data coordinates.  Note that the point is
     * always mapped to the bottom and left axes (which may not be
     * what the user wants).  */

    /*  Pick the first pair of axes */
    axes.x = RbcGetFirstAxis(graphPtr->axisChain[0]);
    axes.y = RbcGetFirstAxis(graphPtr->axisChain[1]);
    point = RbcInvMap2D(graphPtr, x, y, &axes);

    Tcl_AppendElement(interp, RbcDtoa(interp, point.x));
    Tcl_AppendElement(interp, RbcDtoa(interp, point.y));
    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * TransformOp --
 *
 *      This procedure returns a list of the window coordinates
 *      corresponding with the given graph x and y coordinates.
 *
 * Results:
 *      Returns a standard Tcl result.  interp->result contains
 *      the list of the graph coordinates. If an error occurred
 *      while parsing the window positions, TCL_ERROR is returned,
 *      then interp->result will contain an error message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -------------------------------------------------------------------------
 */
static int
TransformOp(
    RbcGraph * graphPtr,        /* Graph widget record */
    Tcl_Interp * interp,
    int argc,                   /* Not used. */
    const char **argv)
{
    double          x, y;
    RbcPoint2D      point;
    RbcAxis2D       axes;

    if ((Tcl_ExprDouble(interp, argv[2], &x) != TCL_OK) ||
        (Tcl_ExprDouble(interp, argv[3], &y) != TCL_OK)) {
        return TCL_ERROR;
    }
    if (graphPtr->flags & RBC_RESET_AXES) {
        RbcResetAxes(graphPtr);
    }
    /*
     * Perform the transformation from window to graph coordinates.
     * Note that the points are always mapped onto the bottom and left
     * axes (which may not be the what the user wants).
     */
    axes.x = RbcGetFirstAxis(graphPtr->axisChain[0]);
    axes.y = RbcGetFirstAxis(graphPtr->axisChain[1]);

    point = RbcMap2D(graphPtr, x, y, &axes);
    Tcl_AppendElement(interp, RbcItoa(ROUND(point.x)));
    Tcl_AppendElement(interp, RbcItoa(ROUND(point.y)));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToFormat --
 *
 *      Convert a string represent a node number into its integer
 *      value.
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToFormat(
    ClientData clientData,      /* Contains a pointer to the tabset containing
                                 * this image. */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    char *switchName,           /* Not used. */
    char *string,               /* String representation */
    char *record,               /* Structure record */
    int offset)
{                               /* Offset to field in structure */
    int            *formatPtr = (int *) (record + offset);
    char            c;

    c = string[0];
    if ((c == 'p') && (strcmp(string, "photo") == 0)) {
        *formatPtr = FORMAT_PHOTO;
#ifdef _WIN32
    } else if ((c == 'e') && (strcmp(string, "emf") == 0)) {
        *formatPtr = FORMAT_EMF;
    } else if ((c == 'w') && (strcmp(string, "wmf") == 0)) {
        *formatPtr = FORMAT_WMF;
#endif /* _WIN32 */
    } else {
#ifdef _WIN32
        Tcl_AppendResult(interp, "bad format \"", string,
            "\": should be photo, emf, or wmf.", (char *) NULL);
#else
        Tcl_AppendResult(interp, "bad format \"", string,
            "\": should be photo.", (char *) NULL);
#endif /* _WIN32 */
        return TCL_ERROR;
    }
    return TCL_OK;
}

#ifdef _WIN32

/*
 *----------------------------------------------------------------------
 *
 * InitMetaFileHeader --
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
InitMetaFileHeader(
    Tk_Window tkwin,
    int width,
    int height,
    APMHEADER * mfhPtr)
{
    unsigned int   *p;
    unsigned int    sum;
    Screen         *screen;
#define MM_INCH		25.4
    double          dpiX, dpiY;

    mfhPtr->key = 0x9ac6cdd7L;
    mfhPtr->hmf = 0;
    mfhPtr->inch = 1440;

    screen = Tk_Screen(tkwin);
    dpiX = (WidthOfScreen(screen) * MM_INCH) / WidthMMOfScreen(screen);
    dpiY = (HeightOfScreen(screen) * MM_INCH) / HeightMMOfScreen(screen);

    mfhPtr->bbox.Left = mfhPtr->bbox.Top = 0;
    mfhPtr->bbox.Bottom = (SHORT) ((width * 1440) / dpiX);
    mfhPtr->bbox.Right = (SHORT) ((height * 1440) / dpiY);
    mfhPtr->reserved = 0;
    sum = 0;
    for (p = (unsigned int *) mfhPtr;
        p < (unsigned int *) &(mfhPtr->checksum); p++) {
        sum ^= *p;
    }
    mfhPtr->checksum = sum;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateAPMetaFile --
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
CreateAPMetaFile(
    Tcl_Interp * interp,
    HANDLE hMetaFile,
    HDC hDC,
    APMHEADER * mfhPtr,
    char *fileName)
{
    HANDLE          hFile;
    HANDLE          hMem;
    LPVOID          buffer;
    int             result;
    DWORD           count, nBytes;

    result = TCL_ERROR;
    hMem = NULL;
    hFile = CreateFile(fileName,        /* File path */
        GENERIC_WRITE,          /* Access mode */
        0,                      /* No sharing. */
        NULL,                   /* Security attributes */
        CREATE_ALWAYS,          /* Overwrite any existing file */
        FILE_ATTRIBUTE_NORMAL, NULL);   /* No template file */
    if (hFile == INVALID_HANDLE_VALUE) {
        Tcl_AppendResult(interp, "can't create metafile \"", fileName,
            "\":", RbcLastError(), (char *) NULL);
        return TCL_ERROR;
    }
    if ((!WriteFile(hFile, (LPVOID) mfhPtr, sizeof(APMHEADER), &count,
                NULL)) || (count != sizeof(APMHEADER))) {
        Tcl_AppendResult(interp, "can't create metafile header to \"",
            fileName, "\":", RbcLastError(), (char *) NULL);
        goto error;
    }
    nBytes = GetWinMetaFileBits(hMetaFile, 0, NULL, MM_ANISOTROPIC, hDC);
    hMem = GlobalAlloc(GHND, nBytes);
    if (hMem == NULL) {
        Tcl_AppendResult(interp, "can't create allocate global memory:",
            RbcLastError(), (char *) NULL);
        goto error;
    }
    buffer = (LPVOID) GlobalLock(hMem);
    if (!GetWinMetaFileBits(hMetaFile, nBytes, buffer, MM_ANISOTROPIC, hDC)) {
        Tcl_AppendResult(interp, "can't get metafile bits:",
            RbcLastError(), (char *) NULL);
        goto error;
    }
    if ((!WriteFile(hFile, buffer, nBytes, &count, NULL)) || (count != nBytes)) {
        Tcl_AppendResult(interp, "can't write metafile bits:",
            RbcLastError(), (char *) NULL);
        goto error;
    }
    result = TCL_OK;
  error:
    CloseHandle(hFile);
    if (hMem != NULL) {
        GlobalUnlock(hMem);
        GlobalFree(hMem);
    }
    return result;
}

#endif /*_WIN32*/

/*
 * --------------------------------------------------------------------------
 *
 * SnapOp --
 *
 *      Snaps a picture of the graph and stores it in the specified image
 *
 * Results:
 *      Returns a standard Tcl result.  interp->result contains
 *      the list of the graph coordinates. If an error occurred
 *      while parsing the window positions, TCL_ERROR is returned,
 *      then interp->result will contain an error message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -------------------------------------------------------------------------
 */
static int
SnapOp(
    RbcGraph * graphPtr,        /* Graph widget record */
    Tcl_Interp * interp,
    int argc,                   /* Not used. */
    const char **argv)
{
    int             result;
    Pixmap          drawable;
    int             noBackingStore = 0;
    register int    i;
    SnapData        data;

    /* .g snap ?switches? name */
    data.height = Tk_Height(graphPtr->tkwin);
    data.width = Tk_Width(graphPtr->tkwin);
    data.format = FORMAT_PHOTO;
    /* Process switches  */
    i = RbcProcessSwitches(interp, snapSwitches, argc - 2, argv + 2,
        (char *) &data, RBC_SWITCH_OBJV_PARTIAL);
    if (i < 0) {
        return TCL_ERROR;
    }
    i += 2;
    if (i >= argc) {
        Tcl_AppendResult(interp, "missing name argument: should be \"",
            argv[0], "snap ?switches? name\"", (char *) NULL);
        return TCL_ERROR;
    }
    data.name = argv[i];
    if (data.width < 2) {
        data.width = 400;
    }
    if (data.height < 2) {
        data.height = 400;
    }
    /* Always re-compute the layout of the graph before snapping the photo. */
    graphPtr->width = data.width;
    graphPtr->height = data.height;
    RbcLayoutGraph(graphPtr);

    drawable = Tk_WindowId(graphPtr->tkwin);
    if (data.format == FORMAT_PHOTO) {
        drawable = Tk_GetPixmap(graphPtr->display, drawable, graphPtr->width,
            graphPtr->height, Tk_Depth(graphPtr->tkwin));
#ifdef _WIN32
        assert(drawable != None);
#endif
        graphPtr->flags |= RBC_RESET_WORLD;
        RbcDrawGraph(graphPtr, drawable, noBackingStore);
        result = RbcSnapPhoto(interp, graphPtr->tkwin, drawable, 0, 0,
            data.width, data.height, data.width, data.height, data.name, 1.0);
        Tk_FreePixmap(graphPtr->display, drawable);
#ifdef _WIN32
    } else if ((data.format == FORMAT_WMF) || (data.format == FORMAT_EMF)) {
        TkWinDC         drawableDC;
        TkWinDCState    state;
        HDC             hRefDC, hDC;
        HENHMETAFILE    hMetaFile;
        Tcl_DString     dString;
        char           *title;

        hRefDC = TkWinGetDrawableDC(graphPtr->display, drawable, &state);

        Tcl_DStringInit(&dString);
        Tcl_DStringAppend(&dString, "RBC Graph ", -1);
        Tcl_DStringAppend(&dString, RBC_VERSION, -1);
        Tcl_DStringAppend(&dString, "\0", -1);
        Tcl_DStringAppend(&dString, Tk_PathName(graphPtr->tkwin), -1);
        Tcl_DStringAppend(&dString, "\0", -1);
        title = Tcl_DStringValue(&dString);
        hDC = CreateEnhMetaFile(hRefDC, NULL, NULL, title);
        Tcl_DStringFree(&dString);

        if (hDC == NULL) {
            Tcl_AppendResult(interp, "can't create metafile: ",
                RbcLastError(), (char *) NULL);
            return TCL_ERROR;
        }

        drawableDC.hdc = hDC;
        drawableDC.type = TWD_WINDC;

        RbcLayoutGraph(graphPtr);
        graphPtr->flags |= RBC_RESET_WORLD;
        RbcDrawGraph(graphPtr, (Drawable) & drawableDC, FALSE);

        hMetaFile = CloseEnhMetaFile(hDC);
        if (strcmp(data.name, "CLIPBOARD") == 0) {
            HWND            hWnd;

            hWnd = Tk_GetHWND(drawable);
            OpenClipboard(hWnd);
            EmptyClipboard();
            SetClipboardData(CF_ENHMETAFILE, hMetaFile);
            CloseClipboard();
            result = TCL_OK;
        } else {
            result = TCL_ERROR;
            if (data.format == FORMAT_WMF) {
                APMHEADER       mfh;

                assert(sizeof(mfh) == 22);
                InitMetaFileHeader(graphPtr->tkwin, data.width, data.height,
                    &mfh);
                result = CreateAPMetaFile(interp, hMetaFile, hRefDC, &mfh,
                    data.name);
            } else {
                HENHMETAFILE    hMetaFile2;

                hMetaFile2 = CopyEnhMetaFile(hMetaFile, data.name);
                if (hMetaFile2 != NULL) {
                    result = TCL_OK;
                    DeleteEnhMetaFile(hMetaFile2);
                }
            }
            DeleteEnhMetaFile(hMetaFile);
        }
        TkWinReleaseDrawableDC(drawable, hRefDC, &state);

#endif /*_WIN32*/
    } else {
        Tcl_AppendResult(interp, "bad snapshot format", (char *) NULL);
        return TCL_ERROR;
    }
    graphPtr->flags = RBC_MAP_WORLD;
    RbcEventuallyRedrawGraph(graphPtr);
    return result;
}

static RbcOpSpec graphOps[] = {
    {"axis", 1, (RbcOp) RbcVirtualAxisOp, 2, 0, "oper ?args?",},
    {"bar", 2, (RbcOp) BarOp, 2, 0, "oper ?args?",},
    {"cget", 2, (RbcOp) CgetOp, 3, 3, "option",},
    {"configure", 2, (RbcOp) ConfigureOp, 2, 0, "?option value?...",},
    {"crosshairs", 2, (RbcOp) RbcCrosshairsOp, 2, 0, "oper ?args?",},
    {"element", 2, (RbcOp) ElementOp, 2, 0, "oper ?args?",},
    {"extents", 2, (RbcOp) ExtentsOp, 3, 3, "item",},
    {"grid", 1, (RbcOp) RbcGridOp, 2, 0, "oper ?args?",},
    {"inside", 3, (RbcOp) InsideOp, 4, 4, "winX winY",},
    {"invtransform", 3, (RbcOp) InvtransformOp, 4, 4, "winX winY",},
    {"legend", 2, (RbcOp) RbcLegendOp, 2, 0, "oper ?args?",},
    {"line", 2, (RbcOp) LineOp, 2, 0, "oper ?args?",},
    {"marker", 2, (RbcOp) RbcMarkerOp, 2, 0, "oper ?args?",},
    {"pen", 2, (RbcOp) RbcPenOp, 2, 0, "oper ?args?",},
    {"postscript", 2, (RbcOp) RbcPostScriptOp, 2, 0, "oper ?args?",},
    {"snap", 1, (RbcOp) SnapOp, 3, 0, "?switches? name",},
    {"transform", 1, (RbcOp) TransformOp, 4, 4, "x y",},
    {"x2axis", 2, (RbcOp) X2AxisOp, 2, 0, "oper ?args?",},
    {"xaxis", 2, (RbcOp) XAxisOp, 2, 0, "oper ?args?",},
    {"y2axis", 2, (RbcOp) Y2AxisOp, 2, 0, "oper ?args?",},
    {"yaxis", 2, (RbcOp) YAxisOp, 2, 0, "oper ?args?",},
};

static int      nGraphOps = sizeof(graphOps) / sizeof(RbcOpSpec);

/*
 *----------------------------------------------------------------------
 *
 * RbcGraphInstCmdProc --
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
RbcGraphInstCmdProc(
    ClientData clientData,
    Tcl_Interp * interp,
    int argc,
    const char *argv[])
{
    RbcOp           proc;
    int             result;
    RbcGraph       *graphPtr = clientData;

    proc = RbcGetOp(interp, nGraphOps, graphOps, RBC_OP_ARG1, argc, argv, 0);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    Tcl_Preserve(graphPtr);
    result = (*proc) (graphPtr, interp, argc, argv);
    Tcl_Release(graphPtr);
    return result;
}

/*
 * --------------------------------------------------------------------------
 *
 * NewGraph --
 *
 *      Creates a new window and Tcl command representing an
 *      instance of a graph widget.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 * --------------------------------------------------------------------------
 */
static int
NewGraph(
    Tcl_Interp * interp,
    int argc,
    const char **argv,
    RbcUid classUid)
{
    RbcGraph       *graphPtr;
    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
            " pathName ?option value?...\"", (char *) NULL);
        return TCL_ERROR;
    }
    graphPtr = CreateGraph(interp, argc, argv, classUid);
    if (graphPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(Tk_PathName(graphPtr->tkwin),
            -1));
    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * GraphCmd --
 *
 *      Creates a new window and Tcl command representing an
 *      instance of a graph widget.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 * --------------------------------------------------------------------------
 */
static int
GraphCmd(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,
    int argc,
    const char *argv[])
{
    return NewGraph(interp, argc, argv, rbcLineElementUid);
}

/*
 *--------------------------------------------------------------
 *
 * BarchartCmd --
 *
 *      Creates a new window and Tcl command representing an
 *      instance of a barchart widget.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 *--------------------------------------------------------------
 */
static int
BarchartCmd(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,
    int argc,
    const char *argv[])
{
    return NewGraph(interp, argc, argv, rbcBarElementUid);
}

/*
 *--------------------------------------------------------------
 *
 * StripchartCmd --
 *
 *      Creates a new window and Tcl command representing an
 *      instance of a barchart widget.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 *--------------------------------------------------------------
 */
static int
StripchartCmd(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,
    int argc,
    const char *argv[])
{
    return NewGraph(interp, argc, argv, rbcStripElementUid);
}

/*
 * -----------------------------------------------------------------------
 *
 * DrawMargins --
 *
 *      Draws the exterior region of the graph (axes, ticks, titles, etc)
 *      onto a pixmap. The interior region is defined by the given
 *      rectangle structure.
 *
 *      ---------------------------------
 *          |                               |
 *          |           rectArr[0]          |
 *          |                               |
 *      ---------------------------------
 *          |     |top           right|     |
 *          |     |                   |     |
 *          |     |                   |     |
 *          | [1] |                   | [2] |
 *          |     |                   |     |
 *          |     |                   |     |
 *          |     |                   |     |
 *          |     |                   |     |
 *          |     |                   |     |
 *          |     |left         bottom|     |
 *      ---------------------------------
 *          |                               |
 *          |          rectArr[3]           |
 *          |                               |
 *      ---------------------------------
 *
 *          X coordinate axis
 *          Y coordinate axis
 *          legend
 *          interior border
 *          exterior border
 *          titles (X and Y axis, graph)
 *
 * Returns:
 *      None.
 *
 * Side Effects:
 *      Exterior of graph is displayed in its window.
 *
 * -----------------------------------------------------------------------
 */
static void
DrawMargins(
    RbcGraph * graphPtr,
    Drawable drawable)
{                               /* Pixmap or window to draw into */
    XRectangle      rects[4];
    /*
     * Draw the four outer rectangles which encompass the plotting
     * surface. This clears the surrounding area and clips the plot.
     */
    rects[0].x = rects[0].y = rects[3].x = rects[1].x = 0;
    rects[0].width = rects[3].width = (short int) graphPtr->width;
    rects[0].height = (short int) graphPtr->top;
    rects[3].y = graphPtr->bottom;
    rects[3].height = graphPtr->height - graphPtr->bottom;
    rects[2].y = rects[1].y = graphPtr->top;
    rects[1].width = graphPtr->left;
    rects[2].height = rects[1].height = graphPtr->bottom - graphPtr->top;
    rects[2].x = graphPtr->right;
    rects[2].width = graphPtr->width - graphPtr->right;

    if (graphPtr->tile != NULL) {
        RbcSetTileOrigin(graphPtr->tkwin, graphPtr->tile, 0, 0);
        RbcTileRectangles(graphPtr->tkwin, drawable, graphPtr->tile, rects, 4);
    } else {
        XFillRectangles(graphPtr->display, drawable, graphPtr->fillGC, rects,
            4);
    }

    /* Draw 3D border around the plotting area */

    if (graphPtr->plotBorderWidth > 0) {
        int             x, y, width, height;

        x = graphPtr->left - graphPtr->plotBorderWidth;
        y = graphPtr->top - graphPtr->plotBorderWidth;
        width = (graphPtr->right - graphPtr->left) +
            (2 * graphPtr->plotBorderWidth);
        height = (graphPtr->bottom - graphPtr->top) +
            (2 * graphPtr->plotBorderWidth);
        Tk_Draw3DRectangle(graphPtr->tkwin, drawable, graphPtr->border, x, y,
            width, height, graphPtr->plotBorderWidth, graphPtr->plotRelief);
    }
    if (RbcLegendSite(graphPtr->legend) & RBC_LEGEND_IN_MARGIN) {
        /* Legend is drawn on one of the graph margins */
        RbcDrawLegend(graphPtr->legend, drawable);
    }
    if (graphPtr->title != NULL) {
        RbcDrawText(graphPtr->tkwin, drawable, graphPtr->title,
            &graphPtr->titleTextStyle, graphPtr->titleX, graphPtr->titleY);
    }
    RbcDrawAxes(graphPtr, drawable);

}

/*
 *----------------------------------------------------------------------
 *
 * DrawPlotRegion --
 *
 *      Draws the contents of the plotting area.  This consists of
 *      the elements, markers (draw under elements), axis limits,
 *      grid lines, and possibly the legend.  Typically, the output
 *      will be cached into a backing store pixmap, so that redraws
 *      can occur quickly.
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
DrawPlotRegion(
    RbcGraph * graphPtr,
    Drawable drawable)
{                               /* Pixmap or window to draw into */
    /* Clear the background of the plotting area. */
    XFillRectangle(graphPtr->display, drawable, graphPtr->plotFillGC,
        graphPtr->left, graphPtr->top, graphPtr->right - graphPtr->left + 1,
        graphPtr->bottom - graphPtr->top + 1);

    /* Draw the elements, markers, legend, and axis limits. */

    if (!graphPtr->gridPtr->hidden) {
        RbcDrawGrid(graphPtr, drawable);
    }
    RbcDrawMarkers(graphPtr, drawable, RBC_MARKER_UNDER);
    if ((RbcLegendSite(graphPtr->legend) & RBC_LEGEND_IN_PLOT) &&
        (!RbcLegendIsRaised(graphPtr->legend))) {
        RbcDrawLegend(graphPtr->legend, drawable);
    }
    RbcDrawAxisLimits(graphPtr, drawable);
    RbcDrawElements(graphPtr, drawable);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcLayoutGraph --
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
RbcLayoutGraph(
    RbcGraph * graphPtr)
{
    if (graphPtr->flags & RBC_RESET_AXES) {
        RbcResetAxes(graphPtr);
    }
    if (graphPtr->flags & RBC_LAYOUT_NEEDED) {
        RbcLayoutMargins(graphPtr);
        graphPtr->flags &= ~RBC_LAYOUT_NEEDED;
    }
    /* Compute coordinate transformations for graph components */
    if ((graphPtr->vRange > 1) && (graphPtr->hRange > 1)) {
        if (graphPtr->flags & RBC_MAP_WORLD) {
            RbcMapAxes(graphPtr);
        }
        RbcMapElements(graphPtr);
        RbcMapMarkers(graphPtr);
        RbcMapGrid(graphPtr);
        graphPtr->flags &= ~(RBC_MAP_ALL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcDrawGraph --
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
RbcDrawGraph(
    RbcGraph * graphPtr,
    Drawable drawable,          /* Pixmap or window to draw into */
    int backingStore)
{                               /* If non-zero, use backing store for
                                 * plotting area. */
    if (backingStore) {
        /*
         * Create another pixmap to save elements if one doesn't
         * already exist or the size of the window has changed.
         */
        if ((graphPtr->backPixmap == None) ||
            (graphPtr->backWidth != graphPtr->width) ||
            (graphPtr->backHeight != graphPtr->height)) {

            if (graphPtr->backPixmap != None) {
                Tk_FreePixmap(graphPtr->display, graphPtr->backPixmap);
            }
            graphPtr->backPixmap = Tk_GetPixmap(graphPtr->display,
                Tk_WindowId(graphPtr->tkwin), graphPtr->width,
                graphPtr->height, Tk_Depth(graphPtr->tkwin));
            graphPtr->backWidth = graphPtr->width;
            graphPtr->backHeight = graphPtr->height;
            graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
        }
        if (graphPtr->flags & RBC_REDRAW_BACKING_STORE) {
            /* The backing store is new or out-of-date. */
            DrawPlotRegion(graphPtr, graphPtr->backPixmap);
            graphPtr->flags &= ~RBC_REDRAW_BACKING_STORE;
        }

        /* Copy the pixmap to the one used for drawing the entire graph. */

        XCopyArea(graphPtr->display, graphPtr->backPixmap, drawable,
            graphPtr->drawGC, graphPtr->left, graphPtr->top,
            (graphPtr->right - graphPtr->left + 1),
            (graphPtr->bottom - graphPtr->top + 1),
            graphPtr->left, graphPtr->top);
    } else {
        DrawPlotRegion(graphPtr, drawable);
    }

    /* Draw markers above elements */
    RbcDrawMarkers(graphPtr, drawable, RBC_MARKER_ABOVE);
    RbcDrawActiveElements(graphPtr, drawable);

    if (graphPtr->flags & RBC_DRAW_MARGINS) {
        DrawMargins(graphPtr, drawable);
    }
    if ((RbcLegendSite(graphPtr->legend) & RBC_LEGEND_IN_PLOT) &&
        (RbcLegendIsRaised(graphPtr->legend))) {
        RbcDrawLegend(graphPtr->legend, drawable);
    }
    /* Draw 3D border just inside of the focus highlight ring. */
    if ((graphPtr->borderWidth > 0) && (graphPtr->relief != TK_RELIEF_FLAT)) {
        Tk_Draw3DRectangle(graphPtr->tkwin, drawable, graphPtr->border,
            graphPtr->highlightWidth, graphPtr->highlightWidth,
            graphPtr->width - 2 * graphPtr->highlightWidth,
            graphPtr->height - 2 * graphPtr->highlightWidth,
            graphPtr->borderWidth, graphPtr->relief);
    }
    /* Draw focus highlight ring. */
    if ((graphPtr->highlightWidth > 0) && (graphPtr->flags & RBC_GRAPH_FOCUS)) {
        GC              gc;

        gc = Tk_GCForColor(graphPtr->highlightColor, drawable);
        Tk_DrawFocusHighlight(graphPtr->tkwin, gc, graphPtr->highlightWidth,
            drawable);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateMarginTraces --
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
UpdateMarginTraces(
    RbcGraph * graphPtr)
{
    RbcMargin         *marginPtr;
    int             size;
    register int    i;

    for (i = 0; i < 4; i++) {
        marginPtr = graphPtr->margins + i;
        if (marginPtr->varName != NULL) {       /* Trigger variable traces */
            if ((marginPtr->site == RBC_MARGIN_LEFT) ||
                (marginPtr->site == RBC_MARGIN_RIGHT)) {
                size = marginPtr->width;
            } else {
                size = marginPtr->height;
            }
            Tcl_SetVar(graphPtr->interp, marginPtr->varName, RbcItoa(size),
                TCL_GLOBAL_ONLY);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DisplayGraph --
 *
 *      This procedure is invoked to display a graph widget.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Commands are output to X to display the graph in its
 *      current mode.
 *
 *----------------------------------------------------------------------
 */
static void
DisplayGraph(
    ClientData clientData)
{
    RbcGraph       *graphPtr = clientData;
    Pixmap          drawable;

    graphPtr->flags &= ~RBC_REDRAW_PENDING;
    if (graphPtr->tkwin == NULL) {
        return;                 /* Window destroyed (should not get here) */
    }
    if (RbcGraphUpdateNeeded(graphPtr)) {
        /*
         * One of the elements of the graph has a vector notification
         * pending.  This means that the vector will eventually notify
         * the graph that its data has changed.  Since the graph uses
         * the actual vector (not a copy) we need to keep in-sync.
         * Therefore don't draw right now but wait until we've been
         * notified before redrawing.
         */
        return;
    }
    graphPtr->width = Tk_Width(graphPtr->tkwin);
    graphPtr->height = Tk_Height(graphPtr->tkwin);
    RbcLayoutGraph(graphPtr);
    RbcUpdateCrosshairs(graphPtr);
    if (!Tk_IsMapped(graphPtr->tkwin)) {
        /* The graph's window isn't displayed, so don't bother
         * drawing anything.  By getting this far, we've at least
         * computed the coordinates of the graph's new layout.  */
        return;
    }

    /* Disable crosshairs before redisplaying to the screen */
    RbcDisableCrosshairs(graphPtr);
    /*
     * Create a pixmap the size of the window for double buffering.
     */
    if (graphPtr->doubleBuffer) {
        drawable = Tk_GetPixmap(graphPtr->display, Tk_WindowId(graphPtr->tkwin),
            graphPtr->width, graphPtr->height, Tk_Depth(graphPtr->tkwin));
    } else {
        drawable = Tk_WindowId(graphPtr->tkwin);
    }
#ifdef _WIN32
    assert(drawable != None);
#endif
    RbcDrawGraph(graphPtr, drawable, graphPtr->backingStore
        && graphPtr->doubleBuffer);
    if (graphPtr->flags & RBC_DRAW_MARGINS) {
        XCopyArea(graphPtr->display, drawable, Tk_WindowId(graphPtr->tkwin),
            graphPtr->drawGC, 0, 0, graphPtr->width, graphPtr->height, 0, 0);
    } else {
        XCopyArea(graphPtr->display, drawable, Tk_WindowId(graphPtr->tkwin),
            graphPtr->drawGC, graphPtr->left, graphPtr->top,
            (graphPtr->right - graphPtr->left + 1),
            (graphPtr->bottom - graphPtr->top + 1),
            graphPtr->left, graphPtr->top);
    }
    if (graphPtr->doubleBuffer) {
        Tk_FreePixmap(graphPtr->display, drawable);
    }
    RbcEnableCrosshairs(graphPtr);
    graphPtr->flags &= ~RBC_RESET_WORLD;
    UpdateMarginTraces(graphPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcGraphInit --
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
RbcGraphInit(
    Tcl_Interp * interp)
{
    rbcBarElementUid = (RbcUid) Tk_GetUid("BarElement");
    rbcLineElementUid = (RbcUid) Tk_GetUid("LineElement");
    rbcStripElementUid = (RbcUid) Tk_GetUid("StripElement");
    rbcContourElementUid = (RbcUid) Tk_GetUid("ContourElement");

    rbcLineMarkerUid = (RbcUid) Tk_GetUid("LineMarker");
    rbcBitmapMarkerUid = (RbcUid) Tk_GetUid("BitmapMarker");
    rbcImageMarkerUid = (RbcUid) Tk_GetUid("ImageMarker");
    rbcTextMarkerUid = (RbcUid) Tk_GetUid("TextMarker");
    rbcPolygonMarkerUid = (RbcUid) Tk_GetUid("PolygonMarker");
    rbcWindowMarkerUid = (RbcUid) Tk_GetUid("WindowMarker");

    rbcXAxisUid = (RbcUid) Tk_GetUid("X");
    rbcYAxisUid = (RbcUid) Tk_GetUid("Y");

    Tcl_CreateCommand(interp, "rbc::graph", GraphCmd, (ClientData) NULL,
        (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "rbc::barchart", BarchartCmd, (ClientData) NULL,
        (Tcl_CmdDeleteProc *) NULL);
    Tcl_CreateCommand(interp, "rbc::stripchart", StripchartCmd,
        (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcGetGraphFromWindowData --
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
RbcGraph       *
RbcGetGraphFromWindowData(
    Tk_Window tkwin)
{
    RbcGraph       *graphPtr;

    while (tkwin != NULL) {
        graphPtr = (RbcGraph *) RbcGetWindowInstanceData(tkwin);
        if (graphPtr != NULL) {
            return graphPtr;
        }
        tkwin = Tk_Parent(tkwin);
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcGraphType --
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
RbcGraphType(
    RbcGraph * graphPtr)
{
    if (graphPtr->classUid == rbcLineElementUid) {
        return RBC_GRAPH;
    } else if (graphPtr->classUid == rbcBarElementUid) {
        return RBC_BARCHART;
    } else if (graphPtr->classUid == rbcStripElementUid) {
        return RBC_STRIPCHART;
    }
    return 0;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
