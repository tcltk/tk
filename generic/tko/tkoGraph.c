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
 * 5) Surface, contour, and flow graphs
 *
 * 7) Arrows for line markers
 *
 */

#include "tkoGraph.h"

Tk_Uid rbcXAxisUid;
Tk_Uid rbcYAxisUid;
Tk_Uid rbcBarElementUid;
Tk_Uid rbcLineElementUid;
Tk_Uid rbcStripElementUid;
Tk_Uid rbcContourElementUid;
Tk_Uid rbcLineMarkerUid;
Tk_Uid rbcBitmapMarkerUid;
Tk_Uid rbcImageMarkerUid;
Tk_Uid rbcTextMarkerUid;
Tk_Uid rbcPolygonMarkerUid;
Tk_Uid rbcWindowMarkerUid;

extern Tk_CustomOption rbcLinePenOption;
extern Tk_CustomOption rbcBarPenOption;
extern Tk_CustomOption rbcDistanceOption;
extern Tk_CustomOption rbcBarModeOption;
extern Tk_CustomOption rbcPadOption;
extern Tk_CustomOption rbcTileOption;
extern Tk_CustomOption rbcShadowOption;
extern Tk_CustomOption rbcStyleOption;

static Tk_ConfigSpec configSpecs[] = {
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

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

static RbcSwitchParseProc StringToFormat;
static RbcSwitchCustom formatSwitch = {
    StringToFormat, (RbcSwitchFreeProc *) NULL, (ClientData) 0,
};

/*
 * SnapData --
 */
typedef struct SnapData {
    char *name;
    int width, height;
    int format;
} SnapData;

enum SnapFormats { FORMAT_PHOTO, FORMAT_EMF, FORMAT_WMF };

/*
 * snapSwitches --
 */
static RbcSwitchSpec snapSwitches[] = {
    {RBC_SWITCH_INT_POSITIVE, "-width", Tk_Offset(SnapData, width), 0},
    {RBC_SWITCH_INT_POSITIVE, "-height", Tk_Offset(SnapData, height), 0},
    {RBC_SWITCH_CUSTOM, "-format", Tk_Offset(SnapData, format), 0,
        &formatSwitch},
    {RBC_SWITCH_END, NULL, 0, 0}
};

static Tcl_IdleProc DisplayGraph;
static Tk_EventProc GraphEventProc;

static RbcBindPickProc PickEntry;
static RbcTileChangedProc TileChangedProc;
/*
* Methods
*/
static int GraphConstructor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int GraphDestructor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int GraphMethod_tko_configure(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int GraphMethod(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int GraphMethod_style(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int GraphMethod_barmode(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int GraphMethod_barwidth(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int GraphMethod_plotpadx(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int GraphMethod_plotpady(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int GraphMethod_shadow(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int GraphMethod_tile(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);

/*
 * Functions
 */
static void AdjustAxisPointers(
    RbcGraph * graph);
static void DrawMargins(
    RbcGraph * graph,
    Drawable drawable);
static void DrawPlotRegion(
    RbcGraph * graph,
    Drawable drawable);
static void UpdateMarginTraces(
    RbcGraph * graph);
static int XAxisOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int X2AxisOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int YAxisOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int Y2AxisOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int BarOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int LineOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int ElementOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int ExtentsOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int InsideOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int InvtransformOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int TransformOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int SnapOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
#ifdef __WIN32
static int InitMetaFileHeader(
    Tk_Window tkwin,
    int width,
    int height,
    APMHEADER * mfhPtr);
static int CreateAPMetaFile(
    Tcl_Interp * interp,
    HANDLE hMetaFile,
    HDC hDC,
    APMHEADER * mfhPtr,
    char *fileName);
#endif
static void GraphMetaDestroy(
    RbcGraph * graph);
static void
GraphMetaDelete(
    ClientData clientData)
{
    Tcl_EventuallyFree(clientData, (Tcl_FreeProc *) GraphMetaDestroy);
}

/*
 * graphMeta --
 */
static Tcl_ObjectMetadataType graphMeta = {
    TCL_OO_METADATA_VERSION_CURRENT,
    "GraphMeta",
    GraphMetaDelete,
    NULL
};

/*
 * RbcGraphFromObject --
 *
 * Return:
 *  RbcGraph structure from object meta data.
 */
RbcGraph *
RbcGraphFromObject(
    Tcl_Object object)
{
    return (RbcGraph *) Tcl_ObjectGetMetadata(object, &graphMeta);
}

/*
 * graphOptionDefine --
 *
 * Options and option methods created in class constructor.
 */
static tkoWidgetOptionDefine graphOptionDefine[] = {
    {"-class", "class", "Class",
            "TkoGraph", NULL, NULL, TKO_WIDGETOPTIONREADONLY,
        TKO_SET_CLASS, NULL, 0},
    {"-style", "style", "Style",
            "line", NULL, GraphMethod_style, TKO_WIDGETOPTIONREADONLY,
        0, NULL, 0},
    {"-aspect", "aspect", "Aspect",
            DEF_GRAPH_ASPECT_RATIO, NULL, NULL, 0,
        TKO_SET_DOUBLE, &graphMeta, offsetof(RbcGraph, aspect)},
    {"-background", "background", "Background",
            DEF_GRAPH_BACKGROUND, NULL, NULL, 0,
        TKO_SET_3DBORDER, &graphMeta, offsetof(RbcGraph, border)},
    {"-barmode", "barMode", "BarMode",
            DEF_GRAPH_BAR_MODE, NULL, GraphMethod_barmode, 0,
        0, NULL, 0},
    {"-barwidth", "barWidth", "BarWidth",
            DEF_GRAPH_BAR_WIDTH, NULL, GraphMethod_barwidth, 0,
        0, NULL, 0},
    {"-baseline", "baseline", "Baseline",
            DEF_GRAPH_BAR_BASELINE, NULL, NULL, 0,
        TKO_SET_DOUBLE, &graphMeta, offsetof(RbcGraph, baseline)},
    {"-bd", "-borderwidth", NULL, NULL, NULL, NULL, 0,
        0, NULL, 0},
    {"-bg", "-background", NULL, NULL, NULL, NULL, 0,
        0, NULL, 0},
    {"-bm", "-bottommargin", NULL, NULL, NULL, NULL, 0,
        0, NULL, 0},
    {"-borderwidth", "borderWidth", "BorderWidth",
            DEF_GRAPH_BORDERWIDTH, NULL, NULL, 0,
        TKO_SET_PIXELNONEGATIV, &graphMeta, offsetof(RbcGraph, borderWidth)},
    {"-bottommargin", "bottomMargin", "Margin",
            DEF_GRAPH_MARGIN, NULL, NULL, 0,
        TKO_SET_PIXELNONEGATIV, &graphMeta, offsetof(RbcGraph,
                margins[RBC_MARGIN_BOTTOM].reqSize)},
    {"-bottomvariable", "bottomVariable", "BottomVariable",
            DEF_GRAPH_MARGIN_VAR, NULL, NULL, 0,
        TKO_SET_STRINGNULL, &graphMeta, offsetof(RbcGraph,
                margins[RBC_MARGIN_BOTTOM].varName)},
    {"-bufferelements", "bufferElements", "BufferElements",
            DEF_GRAPH_BUFFER_ELEMENTS, NULL, NULL, 0,
        TKO_SET_BOOLEAN, &graphMeta, offsetof(RbcGraph, backingStore)},
    {"-buffergraph", "bufferGraph", "BufferGraph",
            DEF_GRAPH_BUFFER_GRAPH, NULL, NULL, 0,
        TKO_SET_BOOLEAN, &graphMeta, offsetof(RbcGraph, doubleBuffer)},
    {"-cursor", "cursor", "Cursor",
            DEF_GRAPH_CURSOR, NULL, NULL, 0,
        TKO_SET_CURSOR, &graphMeta, offsetof(RbcGraph, cursor)},
    {"-fg", "-foreground", NULL, NULL, NULL, NULL, 0,
        0, NULL, 0},
    {"-font", "font", "Font",
            DEF_GRAPH_FONT, NULL, NULL, 0,
        TKO_SET_FONT, &graphMeta, offsetof(RbcGraph, titleTextStyle.font)},
    {"-foreground", "foreground", "Foreground",
            DEF_GRAPH_TITLE_COLOR, NULL, NULL, 0,
        TKO_SET_XCOLOR, &graphMeta, offsetof(RbcGraph, titleTextStyle.color)},
    {"-halo", "halo", "Halo",
            DEF_GRAPH_HALO, NULL, NULL, 0,
        TKO_SET_PIXELNONEGATIV, &graphMeta, offsetof(RbcGraph, halo)},
    {"-height", "height", "Height",
            DEF_GRAPH_HEIGHT, NULL, NULL, 0,
        TKO_SET_PIXELNONEGATIV, &graphMeta, offsetof(RbcGraph, reqHeight)},
    {"-highlightbackground", "highlightBackground", "HighlightBackground",
            DEF_GRAPH_HIGHLIGHT_BACKGROUND, NULL, NULL, 0,
        TKO_SET_XCOLOR, &graphMeta, offsetof(RbcGraph, highlightBgColor)},
    {"-highlightcolor", "highlightColor", "HighlightColor",
            DEF_GRAPH_HIGHLIGHT_COLOR, 0, NULL, 0,
        TKO_SET_XCOLOR, &graphMeta, offsetof(RbcGraph, highlightColor)},
    {"-highlightthickness", "highlightThickness", "HighlightThickness",
            DEF_GRAPH_HIGHLIGHT_WIDTH, NULL, NULL, 0,
        TKO_SET_PIXEL, &graphMeta, offsetof(RbcGraph, highlightWidth)},
    {"-invertxy", "invertXY", "InvertXY",
            DEF_GRAPH_INVERT_XY, NULL, NULL, 0,
        TKO_SET_BOOLEAN, &graphMeta, offsetof(RbcGraph, inverted)},
    {"-justify", "justify", "Justify",
            DEF_GRAPH_JUSTIFY, NULL, NULL, 0,
        TKO_SET_JUSTIFY, &graphMeta, offsetof(RbcGraph,
                titleTextStyle.justify)},
    {"-leftmargin", "leftMargin", "Margin",
            DEF_GRAPH_MARGIN, NULL, NULL, 0,
        TKO_SET_PIXELNONEGATIV, &graphMeta, offsetof(RbcGraph,
                margins[RBC_MARGIN_LEFT].reqSize)},
    {"-leftvariable", "leftVariable", "LeftVariable",
            DEF_GRAPH_MARGIN_VAR, NULL, NULL, 0,
        TKO_SET_STRINGNULL, &graphMeta, offsetof(RbcGraph,
                margins[RBC_MARGIN_LEFT].varName)},
    {"-lm", "-leftmargin", NULL, NULL, NULL, NULL, 0,
        0, NULL, 0},
    {"-plotbackground", "plotBackground", "Background",
            DEF_GRAPH_PLOT_BG_MONO, NULL, NULL, 0,
        TKO_SET_XCOLOR, &graphMeta, offsetof(RbcGraph, plotBg)},
    {"-plotborderwidth", "plotBorderWidth", "BorderWidth",
            DEF_GRAPH_PLOT_BW_COLOR, NULL, NULL, 0,
        TKO_SET_PIXELNONEGATIV, &graphMeta, offsetof(RbcGraph,
                plotBorderWidth)},
    {"-plotpadx", "plotPadX", "PlotPad",
            DEF_GRAPH_PLOT_PADX, NULL, GraphMethod_plotpadx, 0,
        0, NULL, 0},
    {"-plotpady", "plotPadY", "PlotPad",
            DEF_GRAPH_PLOT_PADY, NULL, GraphMethod_plotpady, 0,
        0, NULL, 0},
    {"-plotrelief", "plotRelief", "Relief",
            DEF_GRAPH_PLOT_RELIEF, NULL, NULL, 0,
        TKO_SET_RELIEF, &graphMeta, offsetof(RbcGraph, plotRelief)},
    {"-relief", "relief", "Relief",
            DEF_GRAPH_RELIEF, NULL, NULL, 0,
        TKO_SET_RELIEF, &graphMeta, offsetof(RbcGraph, relief)},
    {"-rightmargin", "rightMargin", "Margin",
            DEF_GRAPH_MARGIN, NULL, NULL, 0,
        TKO_SET_PIXELNONEGATIV, &graphMeta, offsetof(RbcGraph,
                margins[RBC_MARGIN_RIGHT].reqSize)},
    {"-rightvariable", "rightVariable", "RightVariable",
            DEF_GRAPH_MARGIN_VAR, NULL, NULL, 0,
        TKO_SET_STRINGNULL, &graphMeta, offsetof(RbcGraph,
                margins[RBC_MARGIN_RIGHT].varName)},
    {"-rm", "-rightmargin", NULL, NULL, NULL, NULL, 0,
        0, NULL, 0},
    {"-shadow", "shadow", "Shadow",
            DEF_GRAPH_SHADOW_COLOR, NULL, GraphMethod_shadow, 0,
        0, NULL, 0},
    {"-takefocus", "takeFocus", "TakeFocus",
            DEF_GRAPH_TAKE_FOCUS, NULL, NULL, 0,
        TKO_SET_STRINGNULL, &graphMeta, offsetof(RbcGraph, takeFocus)},
    {"-tile", "tile", "Tile",
            NULL, NULL, GraphMethod_tile, 0,
        0, NULL, 0},
    {"-title", "title", "Title",
            DEF_GRAPH_TITLE, NULL, NULL, 0,
        TKO_SET_STRINGNULL, &graphMeta, offsetof(RbcGraph, title)},
    {"-tm", "-topmargin", NULL, NULL, NULL, NULL, 0,
        0, NULL, 0},
    {"-topmargin", "-topmargin", "Margin",
            DEF_GRAPH_MARGIN, NULL, NULL, 0,
        TKO_SET_PIXELNONEGATIV, &graphMeta, offsetof(RbcGraph,
                margins[RBC_MARGIN_TOP].reqSize)},
    {"-topvariable", "topVariable", "TopVariable",
            DEF_GRAPH_MARGIN_VAR, NULL, NULL, 0,
        TKO_SET_STRINGNULL, &graphMeta, offsetof(RbcGraph,
                margins[RBC_MARGIN_TOP].varName)},
    {"-width", "width", "Width",
            DEF_GRAPH_WIDTH, NULL, NULL, 0,
        TKO_SET_PIXELNONEGATIV, &graphMeta, offsetof(RbcGraph, reqWidth)},
    {NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, NULL, 0}
};

/*
 * graphMethods --
 *
 * Methods created in class constructor.
 */
static Tcl_MethodType graphMethods[] = {
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, GraphConstructor, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, GraphDestructor, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "axis", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "bar", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "crosshairs", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "element", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "extents", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "grid", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "inside", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "invtransform", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "legend", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "line", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "marker", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "pen", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "postscript", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "snap", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "transform", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "x2axis", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "xaxis", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "y2axis", GraphMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "yaxis", GraphMethod, NULL, NULL},
    {-1, NULL, NULL, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "_tko_configure", GraphMethod_tko_configure,
            NULL, NULL},
    {-1, NULL, NULL, NULL, NULL}
};

/*
 * Tko_GraphInit --
 *
 *  Initializer for the graph widget package.
 *
 * Results:
 *  A standard Tcl result.
 *
 * Side Effects:
 *  Tcl commands created
 */
int
Tko_GraphInit(
    Tcl_Interp * interp)
{
Tcl_Class clazz;
Tcl_Object object;
static const char *initScript =
    "::oo::class create ::graph {superclass ::tko::widget; variable tko; {*}$::tko::unknown}";

    rbcBarElementUid = Tk_GetUid("BarElement");
    rbcLineElementUid = Tk_GetUid("LineElement");
    rbcStripElementUid = Tk_GetUid("StripElement");
    rbcContourElementUid = Tk_GetUid("ContourElement");

    rbcLineMarkerUid = Tk_GetUid("LineMarker");
    rbcBitmapMarkerUid = Tk_GetUid("BitmapMarker");
    rbcImageMarkerUid = Tk_GetUid("ImageMarker");
    rbcTextMarkerUid = Tk_GetUid("TextMarker");
    rbcPolygonMarkerUid = Tk_GetUid("PolygonMarker");
    rbcWindowMarkerUid = Tk_GetUid("WindowMarker");

    rbcXAxisUid = Tk_GetUid("X");
    rbcYAxisUid = Tk_GetUid("Y");

    /* Create widget class. */
    if(Tcl_Eval(interp, initScript) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Get class object
     */
    if((object = Tcl_GetObjectFromObj(interp, TkoObj.graph)) == NULL
        || (clazz = Tcl_GetObjectAsClass(object)) == NULL) {
        return TCL_ERROR;
    }
    /*
     * Add methods and options
     */
    if(TkoWidgetClassDefine(interp, clazz, Tcl_GetObjectName(interp, object),
            graphMethods, graphOptionDefine) != TCL_OK) {
        return TCL_ERROR;
    }

    return TCL_OK;
}

/*
 * GraphConstructor --
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
GraphConstructor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Object object;
    RbcGraph *graph;
    int skip;
    Tcl_Obj *myObjv[5];

    /* Get current object. Should not fail? */
    if((object = Tcl_ObjectContextObject(context)) == NULL) {
        return TCL_ERROR;
    }
    skip = Tcl_ObjectContextSkippedArgs(context);
    /* Check calling args */
    if(skip != 3 || objc != 5 || strcmp("create", Tcl_GetString(objv[1])) != 0) {
        Tcl_WrongNumArgs(interp, 1, objv, "pathName ?options?");
        return TCL_ERROR;
    }
    if(objc < 3 || strcmp("create", Tcl_GetString(objv[1])) != 0) {
        Tcl_WrongNumArgs(interp, 1, objv, "pathName ?options?");
        return TCL_ERROR;
    }
    /* Get own options */
    myObjv[3] =
        Tcl_ObjGetVar2(interp, TkoObj.tko_options, TkoObj.graph,
        TCL_GLOBAL_ONLY);
    if(myObjv[3] == NULL) {
        return TCL_ERROR;
    }

    /*
     * Create and initialize the graph data structure.
     */
    graph = RbcCalloc(1, sizeof(RbcGraph));
    assert(graph);
    graph->interp = interp;
    graph->win = NULL;
    graph->classUid = rbcLineElementUid;
    graph->chartStyle = "line";
    graph->object = object;
    graph->display = None;
    graph->flags = (RBC_RESET_WORLD);
    graph->cursor = None;
    graph->inset = 0;
    graph->borderWidth = 0;
    graph->relief = TK_RELIEF_FLAT;
    graph->highlightWidth = 2;
    graph->cursor = None;
    graph->border = NULL;
    graph->highlightBgColor = NULL;
    graph->highlightColor = NULL;
    graph->title = NULL;
    graph->titleX = graph->titleY = 0;
    RbcInitTextStyle(&graph->titleTextStyle);
    graph->takeFocus = NULL;
    graph->reqWidth = graph->reqHeight = 0;
    graph->width = graph->height = 0;
    Tcl_InitHashTable(&graph->penTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graph->axes.table, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graph->axes.tagTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graph->elements.table, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graph->elements.tagTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graph->markers.table, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graph->markers.tagTable, TCL_STRING_KEYS);
    graph->elements.displayList = RbcChainCreate();
    graph->markers.displayList = RbcChainCreate();
    graph->axes.displayList = RbcChainCreate();
    graph->classUid = NULL;
    graph->chartStyle = NULL;
    graph->bindTable = NULL;
    graph->nextMarkerId = 1;
    graph->axisChain[0] = NULL; /* set in RbcDefaultAxes() */
    graph->axisChain[1] = NULL; /* set in RbcDefaultAxes() */
    graph->axisChain[2] = NULL; /* set in RbcDefaultAxes() */
    graph->axisChain[3] = NULL; /* set in RbcDefaultAxes() */
    graph->margins[RBC_MARGIN_BOTTOM].site = RBC_MARGIN_BOTTOM;
    graph->margins[RBC_MARGIN_LEFT].site = RBC_MARGIN_LEFT;
    graph->margins[RBC_MARGIN_TOP].site = RBC_MARGIN_TOP;
    graph->margins[RBC_MARGIN_RIGHT].site = RBC_MARGIN_RIGHT;
    graph->postscript = NULL;
    graph->legend = NULL;
    graph->crosshairs = NULL;
    graph->gridPtr = NULL;
    graph->halo = 0;
    graph->inverted = 0;
    graph->tile = NULL;
    graph->drawGC = NULL;
    graph->fillGC = NULL;
    graph->plotBorderWidth = 0;
    graph->plotRelief = TK_RELIEF_SUNKEN;
    graph->plotBg = NULL;
    graph->plotFillGC = NULL;
    graph->aspect = 0.0;
    graph->left = graph->right = 0;
    graph->top = graph->bottom = 0;
    graph->padX.side1 = graph->padX.side2 = 8;
    graph->vRange = graph->vOffset = 0;
    graph->padY.side1 = graph->padY.side2 = 8;
    graph->hRange = graph->hOffset = 0;
    graph->vScale = graph->hScale = 0.;
    graph->doubleBuffer = TRUE;
    graph->backingStore = TRUE;
    graph->backPixmap = None;
    graph->backWidth = graph->backHeight = 0;
    graph->baseline = 0.;
    graph->barWidth = 0.;
    graph->mode = MODE_INFRONT;
    graph->freqArr = NULL;
/*      Tcl_HashTable   freqTable; */
    graph->nStacks = 0;

    Tcl_ObjectSetMetadata(object, &graphMeta, (ClientData) graph);

    graph->win = TkoWidgetWindow(object);
    if(graph)
        /* call next constructor */
        myObjv[0] = objv[0];
    myObjv[1] = objv[1];
    myObjv[2] = objv[2];
    myObjv[3] = Tcl_DuplicateObj(myObjv[3]);
    Tcl_IncrRefCount(myObjv[3]);
    Tcl_ListObjAppendList(interp, myObjv[3], objv[objc - 2]);
    myObjv[4] = objv[4];
    if(Tcl_ObjectContextInvokeNext(interp, context, objc, myObjv,
            skip) != TCL_OK) {
        Tcl_DecrRefCount(myObjv[3]);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(myObjv[3]);
    graph->win = TkoWidgetWindow(object);
    if(graph->win == NULL || *(graph->win) == NULL) {
        return TCL_ERROR;
    }
    if((graph->display = Tk_Display(*(graph->win))) == None) {
        return TCL_ERROR;
    }

    RbcSetWindowInstanceData(*(graph->win), graph);

    /*
     * Init pens
     */
    if(RbcCreatePen(graph, "activeLine", rbcLineElementUid, 0,
            (const char **)NULL) == NULL) {
        return TCL_ERROR;
    }
    if(RbcCreatePen(graph, "activeBar", rbcBarElementUid, 0,
            (const char **)NULL) == NULL) {
        return TCL_ERROR;
    }
    /*
     * Create axis
     */
    if(RbcDefaultAxes(graph) != TCL_OK) {
        return TCL_ERROR;
    }
    AdjustAxisPointers(graph);

    if(RbcCreatePostScript(graph) != TCL_OK) {
        return TCL_ERROR;
    }
    if(RbcCreateCrosshairs(graph) != TCL_OK) {
        return TCL_ERROR;
    }
    if(RbcCreateLegend(graph) != TCL_OK) {
        return TCL_ERROR;
    }
    if(RbcCreateGrid(graph) != TCL_OK) {
        return TCL_ERROR;
    }
    Tk_CreateEventHandler(*(graph->win),
        ExposureMask | StructureNotifyMask | FocusChangeMask, GraphEventProc,
        graph);

    graph->bindTable =
        RbcCreateBindingTable(interp, *(graph->win), graph, PickEntry);

    /* No need to set return value. It will be ignored by "oo::class create" */
    return TCL_OK;
}

/*
 * GraphDestructor --
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
GraphDestructor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Object object;
    int skip;
    RbcGraph *graph;
    Tk_Window tkWin = NULL;

    /* Get current object. Should not fail? */
    if((object = Tcl_ObjectContextObject(context)) == NULL)
        return TCL_ERROR;
    skip = Tcl_ObjectContextSkippedArgs(context);

    if((graph = (RbcGraph *) Tcl_ObjectGetMetadata(object, &graphMeta)) != NULL) {
        Tcl_Preserve(graph);

        if(graph->win) {
            tkWin = *(graph->win);
            graph->win = NULL;
        }
        if(tkWin) {
            Tk_DeleteEventHandler(tkWin,
                ExposureMask | StructureNotifyMask | FocusChangeMask,
                GraphEventProc, graph);
        }

        Tcl_Release(graph);
        Tcl_ObjectSetMetadata(object, &graphMeta, NULL);
    }
    /* ignore errors */
    Tcl_ObjectContextInvokeNext(interp, context, objc, objv, skip);

    return TCL_OK;
}

/*
* GraphMetaDestroy --
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
*/
static void
GraphMetaDestroy(
    RbcGraph * graph)
{
    if(graph->flags & RBC_REDRAW_PENDING) {
        Tcl_CancelIdleCall(DisplayGraph, graph);
    }
    if(graph->border != NULL) {
        Tk_Free3DBorder(graph->border);
    }
    if(graph->highlightBgColor != NULL) {
        Tk_FreeColor(graph->highlightBgColor);
    }
    if(graph->highlightColor != NULL) {
        Tk_FreeColor(graph->highlightColor);
    }
    if(graph->plotBg != NULL) {
        Tk_FreeColor(graph->plotBg);
    }
    /*
     * Destroy the individual components of the graph: elements, markers,
     * X and Y axes, legend, display lists etc.
     */
    RbcDestroyMarkers(graph);
    RbcDestroyElements(graph);
    RbcDestroyAxes(graph);      /* take care of *axisChain */
    RbcDestroyPens(graph);

    if(graph->legend != NULL) {
        RbcDestroyLegend(graph);
    }
    if(graph->postscript != NULL) {
        RbcDestroyPostScript(graph);
    }
    if(graph->crosshairs != NULL) {
        RbcDestroyCrosshairs(graph);
    }
    if(graph->gridPtr != NULL) {
        RbcDestroyGrid(graph);
    }
    if(graph->bindTable != NULL) {
        RbcDestroyBindingTable(graph->bindTable);
    }

    /* Release allocated X resources and memory. */
    if(graph->display != None) {
        if(graph->cursor != None) {
            Tk_FreeCursor(graph->display, graph->cursor);
        }
        if(graph->drawGC != NULL) {
            Tk_FreeGC(graph->display, graph->drawGC);
        }
        if(graph->fillGC != NULL) {
            Tk_FreeGC(graph->display, graph->fillGC);
        }
        if(graph->plotFillGC != NULL) {
            Tk_FreeGC(graph->display, graph->plotFillGC);
        }
        RbcFreeTextStyle(graph->display, &graph->titleTextStyle);
        if(graph->backPixmap != None) {
            Tk_FreePixmap(graph->display, graph->backPixmap);
        }
    }
    if(graph->freqArr != NULL) {
        ckfree((char *)graph->freqArr);
    }
    if(graph->title != NULL) {
        ckfree(graph->title);
    }
    if(graph->takeFocus != NULL) {
        ckfree(graph->takeFocus);
    }
    if(graph->nStacks > 0) {
        Tcl_DeleteHashTable(&graph->freqTable);
    }
    if(graph->tile != NULL) {
        RbcFreeTile(graph->tile);
    }
    ckfree((char *)graph);
}

/*
* GraphMethod_tko_configure --
*
*      Allocates resources for the graph.
*
* Results:
*      None.
*
* Side effects:
*      Configuration information, such as text string, colors, font,
*      etc. get set for graph;  old resources get freed, if there
*      were any.  The graph is redisplayed.
*/
static int
GraphMethod_tko_configure(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Object object;
    RbcGraph *graph;
    XColor *colorPtr;
    GC  newGC;
    XGCValues gcValues;
    unsigned long gcMask;

    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (graph =
            (RbcGraph *) Tcl_ObjectGetMetadata(object, &graphMeta)) == NULL) {
        return TCL_ERROR;
    }
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    /* Don't allow negative bar widths. Reset to an arbitrary value (0.1) */
    if(graph->barWidth <= 0.0) {
        graph->barWidth = 0.1;
    }
    graph->inset = graph->borderWidth + graph->highlightWidth + 1;
    if((graph->reqHeight != Tk_ReqHeight(*(graph->win)))
        || (graph->reqWidth != Tk_ReqWidth(*(graph->win)))) {
        Tk_GeometryRequest(*(graph->win), graph->reqWidth, graph->reqHeight);
    }
    Tk_SetInternalBorder(*(graph->win), graph->borderWidth);
    colorPtr = Tk_3DBorderColor(graph->border);

    if(graph->title != NULL) {
    int w, h;

        RbcGetTextExtents(&graph->titleTextStyle, graph->title, &w, &h);
        graph->titleTextStyle.height = h + 10;
    } else {
        graph->titleTextStyle.width = graph->titleTextStyle.height = 0;
    }

    /*
     * Create GCs for interior and exterior regions, and a background
     * GC for clearing the margins with XFillRectangle
     */

    /* Margin GC */

    gcValues.foreground = graph->titleTextStyle.color->pixel;
    gcValues.background = colorPtr->pixel;
    gcMask = (GCForeground | GCBackground);
    newGC = Tk_GetGC(*(graph->win), gcMask, &gcValues);
    if(graph->drawGC != NULL) {
        Tk_FreeGC(graph->display, graph->drawGC);
    }
    graph->drawGC = newGC;

    /* Plot fill GC (Background = Foreground) */

    gcValues.foreground = graph->plotBg->pixel;
    newGC = Tk_GetGC(*(graph->win), gcMask, &gcValues);
    if(graph->plotFillGC != NULL) {
        Tk_FreeGC(graph->display, graph->plotFillGC);
    }
    graph->plotFillGC = newGC;

    /* Margin fill GC (Background = Foreground) */

    gcValues.foreground = colorPtr->pixel;
    gcValues.background = graph->titleTextStyle.color->pixel;
    newGC = Tk_GetGC(*(graph->win), gcMask, &gcValues);
    if(graph->fillGC != NULL) {
        Tk_FreeGC(graph->display, graph->fillGC);
    }
    graph->fillGC = newGC;
    if(graph->tile != NULL) {
        RbcSetTileChangedProc(graph->tile, TileChangedProc, graph);
    }

    RbcResetTextStyle(*(graph->win), &graph->titleTextStyle);

    if(RbcConfigModified(configSpecs, "-invertxy", (char *)NULL)) {

        /*
         * If the -inverted option changed, we need to readjust the pointers
         * to the axes and recompute the their scales.
         */

        AdjustAxisPointers(graph);
        graph->flags |= RBC_RESET_AXES;
    }
    if((!graph->backingStore) && (graph->backPixmap != None)) {

        /*
         * Free the pixmap if we're not buffering the display of elements
         * anymore.
         */

        Tk_FreePixmap(graph->display, graph->backPixmap);
        graph->backPixmap = None;
    }
    /*
     * Reconfigure the crosshairs, just in case the background color of
     * the plotarea has been changed.
     */
    RbcConfigureCrosshairs(graph);

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
    if(RbcConfigModified(configSpecs, "-invertxy", "-title", "-font",
            "-*margin", "-*width", "-height", "-barmode", "-*pad*", "-aspect",
            (char *)NULL)) {
        graph->flags |= RBC_RESET_WORLD;
    }
    if(RbcConfigModified(configSpecs, "-plotbackground", (char *)NULL)) {
        graph->flags |= RBC_REDRAW_BACKING_STORE;
    }
    graph->flags |= RBC_REDRAW_WORLD;
    RbcEventuallyRedrawGraph(graph);
    return TCL_OK;
}

/*
 * GraphMethod --
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
GraphMethod(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    int cmdIndex, result;
    RbcOp proc;
    int i;
    const char **myArgv;
    static const char *const graphCmdNames[] = {
        "axis", "bar",
        "crosshairs", "element", "extents", "grid",
        "inside", "invtransform", "legend", "line",
        "marker", "pen", "postscript",
        "snap", "transform",
        "x2axis", "xaxis", "y2axis", "yaxis",
        NULL
    };

    enum graphCmd {
        COMMAND_AXIS, COMMAND_BAR,
        COMMAND_CROSSHAIRS, COMMAND_ELEMENT, COMMAND_EXTENTS, COMMAND_GRID,
        COMMAND_INSIDE, COMMAND_INVTRANSFORM, COMMAND_LEGEND, COMMAND_LINE,
        COMMAND_MARKER, COMMAND_PEN, COMMAND_POSTSCRIPT,
        COMMAND_SNAP, COMMAND_TRANSFORM,
        COMMAND_X2AXIS, COMMAND_XAXIS, COMMAND_Y2AXIS, COMMAND_YAXIS
    };

    RbcGraph *graph =
        (RbcGraph *) Tcl_ObjectGetMetadata(Tcl_ObjectContextObject(context),
        &graphMeta);

    /*
     * Parse the widget command by looking up the second token in the list of
     * valid command names.
     */

    result = Tcl_GetIndexFromObj(interp, objv[1], graphCmdNames, "option", 0,
        &cmdIndex);
    if(result != TCL_OK) {
        return result;
    }

    switch ((enum graphCmd)cmdIndex) {
    case COMMAND_AXIS:{
        proc = (RbcOp) RbcVirtualAxisOp;
        break;
    }
    case COMMAND_BAR:{
        proc = BarOp;
        break;
    }
    case COMMAND_CROSSHAIRS:{
        proc = RbcCrosshairsOp;
        break;
    }
    case COMMAND_ELEMENT:{
        proc = ElementOp;
        break;
    }
    case COMMAND_EXTENTS:{
        if(objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "item");
            return TCL_ERROR;
        }
        proc = ExtentsOp;
        break;
    }
    case COMMAND_GRID:{
        proc = RbcGridOp;
        break;
    }
    case COMMAND_INSIDE:{
        if(objc != 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "winX winY");
            return TCL_ERROR;
        }
        proc = InsideOp;
        break;
    }
    case COMMAND_INVTRANSFORM:{
        if(objc != 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "winX winY");
            return TCL_ERROR;
        }
        proc = InvtransformOp;
        break;
    }
    case COMMAND_LEGEND:{
        proc = RbcLegendOp;
        break;
    }
    case COMMAND_LINE:{
        proc = LineOp;
        break;
    }
    case COMMAND_MARKER:{
        proc = RbcMarkerOp;
        break;
    }
    case COMMAND_PEN:{
        proc = RbcPenOp;
        break;
    }
    case COMMAND_POSTSCRIPT:{
        proc = RbcPostScriptOp;
        break;
    }
    case COMMAND_SNAP:{
        if(objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "?switchse? name");
            return TCL_ERROR;
        }
        proc = SnapOp;
        break;
    }
    case COMMAND_TRANSFORM:{
        if(objc != 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "x y");
            return TCL_ERROR;
        }
        proc = TransformOp;
        break;
    }
    case COMMAND_X2AXIS:{
        proc = X2AxisOp;
        break;
    }
    case COMMAND_XAXIS:{
        proc = XAxisOp;
        break;
    }
    case COMMAND_Y2AXIS:{
        proc = Y2AxisOp;
        break;
    }
    case COMMAND_YAXIS:{
        proc = YAxisOp;
        break;
    }
    default:{
        return TCL_ERROR;
    }
    }
    myArgv = ckalloc(objc * sizeof(char *));
    for(i = 0; i < objc; i++) {
        myArgv[i] = Tcl_GetString(objv[i]);
    }
    Tcl_Preserve(graph);
    result = (*proc) (graph, interp, objc, myArgv);
    Tcl_Release(graph);
    ckfree(myArgv);
    return result;
}

/*
 * GraphMethod_style --
 *
 *  Process -style option.
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
GraphMethod_style(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    const char *chPtr;
    Tcl_Object object;
    RbcGraph *graph;
    Tcl_Obj *value;
    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (graph =
            (RbcGraph *) Tcl_ObjectGetMetadata(object, &graphMeta)) == NULL
        || (value =
            TkoWidgetOptionGet(interp, object, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }

    chPtr = Tcl_GetString(value);
    if(strcmp(chPtr, "line") == 0) {
        graph->classUid = rbcLineElementUid;
        graph->chartStyle = "line";
    } else if(strcmp(chPtr, "bar") == 0) {
        graph->classUid = rbcBarElementUid;
        graph->chartStyle = "bar";
    } else if(strcmp(chPtr, "chart") == 0) {
        graph->classUid = rbcStripElementUid;
        graph->chartStyle = "strip";
    } else {
        Tcl_SetObjResult(interp,
            Tcl_ObjPrintf("wrong -style option, should be line,bar or chart"));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * GraphMethod_barmode --
 *
 *  Process -barmode option.
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
GraphMethod_barmode(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    const char *string;
    int length;
    Tcl_Object object;
    RbcGraph *graph;
    Tcl_Obj *value;
    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (graph =
            (RbcGraph *) Tcl_ObjectGetMetadata(object, &graphMeta)) == NULL
        || (value =
            TkoWidgetOptionGet(interp, object, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }

    string = Tcl_GetStringFromObj(value, &length);
    if((string[0] == 'n') && (strncmp(string, "normal", length) == 0)) {
        graph->mode = MODE_INFRONT;
    } else if((string[0] == 'i') && (strncmp(string, "infront", length) == 0)) {
        graph->mode = MODE_INFRONT;
    } else if((string[0] == 's') && (strncmp(string, "stacked", length) == 0)) {
        graph->mode = MODE_STACKED;
    } else if((string[0] == 'a') && (strncmp(string, "aligned", length) == 0)) {
        graph->mode = MODE_ALIGNED;
    } else if((string[0] == 'o') && (strncmp(string, "overlap", length) == 0)) {
        graph->mode = MODE_OVERLAP;
    } else {
        Tcl_AppendResult(interp, "bad mode argument \"", string,
            "\": should be \"infront\", \"stacked\", \"overlap\", or \"aligned\"",
            (char *)NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * GraphMethod_barwidth --
 *
 *  Process -barwidth option.
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
GraphMethod_barwidth(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    double dblVal;
    Tcl_Obj *array;
    Tcl_Object object;
    RbcGraph *graph;
    Tcl_Obj *value;
    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (graph =
            (RbcGraph *) Tcl_ObjectGetMetadata(object, &graphMeta)) == NULL
        || (value =
            TkoWidgetOptionGet(interp, object, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }

    if(Tcl_GetDoubleFromObj(interp, value, &dblVal) != TCL_OK) {
        return TCL_ERROR;
    }
    if((array = TkoWidgetOptionVar(object)) == NULL) {
        return TCL_ERROR;
    }
    if(dblVal < 0.1) {
        dblVal = 0.1;
    }
    Tcl_ObjSetVar2(interp, array, objv[objc - 1], Tcl_NewDoubleObj(dblVal),
        TCL_GLOBAL_ONLY);
    graph->barWidth = dblVal;
    return TCL_OK;
}

/*
 * GraphMethod_plotpadx --
 *
 *  Process -plotpadx option.
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
GraphMethod_plotpadx(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Obj *array;
    Tcl_Object object;
    RbcGraph *graph;
    Tcl_Obj *value;
    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (graph =
            (RbcGraph *) Tcl_ObjectGetMetadata(object, &graphMeta)) == NULL
        || (value =
            TkoWidgetOptionGet(interp, object, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }

    if((array = TkoWidgetOptionVar(object)) == NULL) {
        return TCL_ERROR;
    }
    if(RbcGraphOptionSetPad(interp, object, value, &graph->padX) != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_ObjSetVar2(interp, array, objv[objc - 1],
        Tcl_ObjPrintf("%d %d", graph->padX.side1, graph->padX.side2),
        TCL_GLOBAL_ONLY);
    return TCL_OK;
}

/*
 * GraphMethod_plotpady --
 *
 *  Process -plotpady option.
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
GraphMethod_plotpady(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Obj *array;
    Tcl_Object object;
    RbcGraph *graph;
    Tcl_Obj *value;
    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (graph =
            (RbcGraph *) Tcl_ObjectGetMetadata(object, &graphMeta)) == NULL
        || (value =
            TkoWidgetOptionGet(interp, object, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }

    if((array = TkoWidgetOptionVar(object)) == NULL) {
        return TCL_ERROR;
    }
    if(RbcGraphOptionSetPad(interp, object, value, &graph->padY) != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_ObjSetVar2(interp, array, objv[objc - 1],
        Tcl_ObjPrintf("%d %d", graph->padY.side1, graph->padY.side2),
        TCL_GLOBAL_ONLY);
    return TCL_OK;
}

/*
 * GraphMethod_shadow --
 *
 * Process -shadow option.
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
GraphMethod_shadow(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Obj *array;
    Tcl_Object object;
    RbcGraph *graph;
    Tcl_Obj *value;
    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (graph =
            (RbcGraph *) Tcl_ObjectGetMetadata(object, &graphMeta)) == NULL
        || (value =
            TkoWidgetOptionGet(interp, object, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }

    if((array = TkoWidgetOptionVar(object)) == NULL) {
        return TCL_ERROR;
    }
    if(RbcGraphOptionSetShadow(interp, object, value,
            &graph->titleTextStyle.shadow) != TCL_OK) {
        return TCL_ERROR;
    }
    if(graph->titleTextStyle.shadow.color != NULL) {
        Tcl_ObjSetVar2(interp, array, objv[objc - 1],
            Tcl_ObjPrintf("%s %d",
                Tk_NameOfColor(graph->titleTextStyle.shadow.color),
                graph->titleTextStyle.shadow.offset), TCL_GLOBAL_ONLY);
    } else {
        Tcl_ObjSetVar2(interp, array, objv[objc - 1], TkoObj.empty,
            TCL_GLOBAL_ONLY);
    }
    return TCL_OK;
}

/*
 * GraphMethod_tile --
 *
 *  Process -tile option.
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
GraphMethod_tile(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Object object;
    RbcGraph *graph;
    Tcl_Obj *value;
    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (graph =
            (RbcGraph *) Tcl_ObjectGetMetadata(object, &graphMeta)) == NULL
        || (value =
            TkoWidgetOptionGet(interp, object, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }

    return (RbcGraphOptionSetTile(interp, object, value, &graph->tile));
}

/*
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
 */
void
RbcEventuallyRedrawGraph(
    RbcGraph * graph)
{              /* Graph widget record */
    if(graph->win == NULL || *(graph->win) == NULL)
        return;
    if(!(graph->flags & RBC_REDRAW_PENDING)) {
        Tcl_DoWhenIdle(DisplayGraph, graph);
        graph->flags |= RBC_REDRAW_PENDING;
    }
}

/*
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
 */
static void
GraphEventProc(
    ClientData clientData,     /* Graph widget record */
    register XEvent * eventPtr)
{              /* Event which triggered call to routine */
    RbcGraph *graph = clientData;
    if(eventPtr->type == DestroyNotify || graph->win == NULL
        || *(graph->win) == NULL)
        return;

    if(eventPtr->type == Expose) {
        if(eventPtr->xexpose.count == 0) {
            graph->flags |= RBC_REDRAW_WORLD;
            RbcEventuallyRedrawGraph(graph);
        }
    } else if((eventPtr->type == FocusIn) || (eventPtr->type == FocusOut)) {
        if(eventPtr->xfocus.detail != NotifyInferior) {
            if(eventPtr->type == FocusIn) {
                graph->flags |= RBC_GRAPH_FOCUS;
            } else {
                graph->flags &= ~RBC_GRAPH_FOCUS;
            }
            graph->flags |= RBC_REDRAW_WORLD;
            RbcEventuallyRedrawGraph(graph);
        }
    } else if(eventPtr->type == ConfigureNotify) {
        graph->flags |= (RBC_MAP_WORLD | RBC_REDRAW_WORLD);
        RbcEventuallyRedrawGraph(graph);
    }
}

/*
 * TileChangedProc --
 *
 *      Rebuilds the designated GC with the new tile pixmap.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 */
static void
TileChangedProc(
    ClientData clientData,
    RbcTile tile)
{              /* Not used. */
RbcGraph *graph = clientData;
    if(graph->win == NULL || *(graph->win) == NULL)
        return;

    graph->flags |= RBC_REDRAW_WORLD;
    RbcEventuallyRedrawGraph(graph);
}

/*
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
 */
static void
AdjustAxisPointers(
    RbcGraph * graph)
{              /* Graph widget record */
    if(graph->inverted) {
        graph->margins[RBC_MARGIN_LEFT].axes = graph->axisChain[0];
        graph->margins[RBC_MARGIN_BOTTOM].axes = graph->axisChain[1];
        graph->margins[RBC_MARGIN_RIGHT].axes = graph->axisChain[2];
        graph->margins[RBC_MARGIN_TOP].axes = graph->axisChain[3];
    } else {
        graph->margins[RBC_MARGIN_LEFT].axes = graph->axisChain[1];
        graph->margins[RBC_MARGIN_BOTTOM].axes = graph->axisChain[0];
        graph->margins[RBC_MARGIN_RIGHT].axes = graph->axisChain[3];
        graph->margins[RBC_MARGIN_TOP].axes = graph->axisChain[2];
    }
}

/*
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
 */
static ClientData
PickEntry(
    ClientData clientData,
    int x,
    int y,
    ClientData * contextPtr)
{              /* Not used. */
    RbcGraph *graph = clientData;
    RbcChainLink *linkPtr;
    RbcElement *elemPtr;
    RbcMarker *markerPtr;
    RbcExtents2D exts;

    if(graph->flags & RBC_MAP_ALL) {
        /* Can't pick anything until the next
         * redraw occurs. */
        return NULL;
    }
    RbcGraphExtents(graph, &exts);

    if((x > exts.right) || (x < exts.left) || (y > exts.bottom)
        || (y < exts.top)) {
        /*
         * Sample coordinate is in one of the graph margins.  Can only
         * pick an axis.
         */
        return RbcNearestAxis(graph, x, y);
    }

    /*
     * From top-to-bottom check:
     *  1. markers drawn on top (-under false).
     *  2. elements using its display list back to front.
     *  3. markers drawn under element (-under true).
     */
    markerPtr = (RbcMarker *) RbcNearestMarker(graph, x, y, FALSE);
    if(markerPtr != NULL) {
        /* Found a marker (-under false). */
        return markerPtr;
    }
    {
    RbcClosestSearch search;

        search.along = RBC_SEARCH_BOTH;
        search.halo = graph->halo + 1;
        search.index = -1;
        search.x = x;
        search.y = y;
        search.dist = (double)(search.halo + 1);
        search.mode = RBC_SEARCH_AUTO;

        for(linkPtr = RbcChainLastLink(graph->elements.displayList);
            linkPtr != NULL; linkPtr = RbcChainPrevLink(linkPtr)) {
            elemPtr = RbcChainGetValue(linkPtr);
            if((elemPtr->flags & RBC_MAP_ITEM)
                || (RbcVectorNotifyPending(elemPtr->x.clientId))
                || (RbcVectorNotifyPending(elemPtr->y.clientId))) {
                continue;
            }
            if((!elemPtr->hidden) && (elemPtr->state == RBC_STATE_NORMAL)) {
                (*elemPtr->procsPtr->closestProc) (graph, elemPtr, &search);
            }
        }
        if(search.dist <= (double)search.halo) {
            /* Found an element within the
             * minimum halo distance. */
            return search.elemPtr;
        }
    }
    markerPtr = (RbcMarker *) RbcNearestMarker(graph, x, y, TRUE);
    if(markerPtr != NULL) {
        /* Found a marker (-under true) */
        return markerPtr;
    }
    /* Nothing found. */
    return NULL;
}

/* Widget sub-commands */

/*
 * XAxisOp --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
static int
XAxisOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    int margin;

    margin = (graph->inverted) ? RBC_MARGIN_LEFT : RBC_MARGIN_BOTTOM;
    return RbcAxisOp(graph, margin, argc, argv);
}

/*
 * X2AxisOp --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
static int
X2AxisOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    int margin;

    margin = (graph->inverted) ? RBC_MARGIN_RIGHT : RBC_MARGIN_TOP;
    return RbcAxisOp(graph, margin, argc, argv);
}

/*
 * YAxisOp --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
static int
YAxisOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    int margin;

    margin = (graph->inverted) ? RBC_MARGIN_BOTTOM : RBC_MARGIN_LEFT;
    return RbcAxisOp(graph, margin, argc, argv);
}

/*
 * Y2AxisOp --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
static int
Y2AxisOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    int margin;

    margin = (graph->inverted) ? RBC_MARGIN_TOP : RBC_MARGIN_RIGHT;
    return RbcAxisOp(graph, margin, argc, argv);
}

/*
 * BarOp --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
static int
BarOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    return RbcElementOp(graph, interp, argc, argv, rbcBarElementUid);
}

/*
 * LineOp --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
static int
LineOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    return RbcElementOp(graph, interp, argc, argv, rbcLineElementUid);
}

/*
 * ElementOp --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
static int
ElementOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    return RbcElementOp(graph, interp, argc, argv, graph->classUid);
}

/*
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
 */
static int
ExtentsOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,                  /* Not used. */
    const char **argv)
{
    char c;
    unsigned int length;
    char string[200];

    c = argv[2][0];
    length = strlen(argv[2]);
    if((c == 'p') && (length > 4)
        && (strncmp("plotheight", argv[2], length) == 0)) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(graph->bottom - graph->top + 1));
    } else if((c == 'p') && (length > 4)
        && (strncmp("plotwidth", argv[2], length) == 0)) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(graph->right - graph->left + 1));
    } else if((c == 'p') && (length > 4)
        && (strncmp("plotarea", argv[2], length) == 0)) {
        sprintf(string, "%d %d %d %d", graph->left, graph->top,
            graph->right - graph->left + 1, graph->bottom - graph->top + 1);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(string, -1));
    } else if((c == 'l') && (length > 2)
        && (strncmp("legend", argv[2], length) == 0)) {
        sprintf(string, "%d %d %d %d", RbcLegendX(graph->legend),
            RbcLegendY(graph->legend), RbcLegendWidth(graph->legend),
            RbcLegendHeight(graph->legend));
        Tcl_SetObjResult(interp, Tcl_NewStringObj(string, -1));
    } else if((c == 'l') && (length > 2)
        && (strncmp("leftmargin", argv[2], length) == 0)) {
        Tcl_SetObjResult(interp,
            Tcl_NewIntObj(graph->margins[RBC_MARGIN_LEFT].width));
    } else if((c == 'r') && (length > 1)
        && (strncmp("rightmargin", argv[2], length) == 0)) {
        Tcl_SetObjResult(interp,
            Tcl_NewIntObj(graph->margins[RBC_MARGIN_RIGHT].width));
    } else if((c == 't') && (length > 1)
        && (strncmp("topmargin", argv[2], length) == 0)) {
        Tcl_SetObjResult(interp,
            Tcl_NewIntObj(graph->margins[RBC_MARGIN_TOP].height));
    } else if((c == 'b') && (length > 1)
        && (strncmp("bottommargin", argv[2], length) == 0)) {
        Tcl_SetObjResult(interp,
            Tcl_NewIntObj(graph->margins[RBC_MARGIN_BOTTOM].height));
    } else {
        Tcl_AppendResult(interp, "bad extent item \"", argv[2],
            "\": should be plotheight, plotwidth, leftmargin, rightmargin, \
topmargin, bottommargin, plotarea, or legend", (char *)NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
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
 */
static int
InsideOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,                  /* Not used. */
    const char **argv)
{
    int x, y;
    RbcExtents2D exts;
    int result;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    if(Tk_GetPixels(interp, *(graph->win), argv[2], &x) != TCL_OK) {
        return TCL_ERROR;
    }
    if(Tk_GetPixels(interp, *(graph->win), argv[3], &y) != TCL_OK) {
        return TCL_ERROR;
    }
    RbcGraphExtents(graph, &exts);
    result = RbcPointInRegion(&exts, x, y);
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(result));
    return TCL_OK;
}

/*
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
 */
static int
InvtransformOp(
    RbcGraph * graph,          /* Graph widget record */
    Tcl_Interp * interp,
    int argc,                  /* Not used. */
    const char **argv)
{
    double x, y;
    RbcPoint2D point;
    RbcAxis2D axes;
    char stringDouble[TCL_DOUBLE_SPACE];

    if(Tcl_ExprDouble(interp, argv[2], &x) != TCL_OK ||
        Tcl_ExprDouble(interp, argv[3], &y) != TCL_OK) {
        return TCL_ERROR;
    }
    if(graph->flags & RBC_RESET_AXES) {
        RbcResetAxes(graph);
    }
    /* Perform the reverse transformation, converting from window
     * coordinates to graph data coordinates.  Note that the point is
     * always mapped to the bottom and left axes (which may not be
     * what the user wants).  */

    /*  Pick the first pair of axes */
    axes.x = RbcGetFirstAxis(graph->axisChain[0]);
    axes.y = RbcGetFirstAxis(graph->axisChain[1]);
    point = RbcInvMap2D(graph, x, y, &axes);

    Tcl_PrintDouble(NULL, point.x, stringDouble);
    Tcl_AppendElement(interp, stringDouble);
    Tcl_PrintDouble(NULL, point.y, stringDouble);
    Tcl_AppendElement(interp, stringDouble);
    return TCL_OK;
}

/*
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
 */
static int
TransformOp(
    RbcGraph * graph,          /* Graph widget record */
    Tcl_Interp * interp,
    int argc,                  /* Not used. */
    const char **argv)
{
    double x, y;
    RbcPoint2D point;
    RbcAxis2D axes;

    if((Tcl_ExprDouble(interp, argv[2], &x) != TCL_OK) ||
        (Tcl_ExprDouble(interp, argv[3], &y) != TCL_OK)) {
        return TCL_ERROR;
    }
    if(graph->flags & RBC_RESET_AXES) {
        RbcResetAxes(graph);
    }
    /*
     * Perform the transformation from window to graph coordinates.
     * Note that the points are always mapped onto the bottom and left
     * axes (which may not be the what the user wants).
     */
    axes.x = RbcGetFirstAxis(graph->axisChain[0]);
    axes.y = RbcGetFirstAxis(graph->axisChain[1]);

    point = RbcMap2D(graph, x, y, &axes);
    Tcl_AppendPrintfToObj(Tcl_GetObjResult(interp),
        "%d %d", ROUND(point.x), ROUND(point.y));
    return TCL_OK;
}

/*
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
 */
static int
StringToFormat(
    ClientData clientData,     /* Contains a pointer to the tabset containing
                                * this image. */
    Tcl_Interp * interp,       /* Interpreter to send results back to */
    char *switchName,          /* Not used. */
    char *string,              /* String representation */
    char *record,              /* Structure record */
    int offset)
{              /* Offset to field in structure */
    int *formatPtr = (int *)(record + offset);
    char c;

    c = string[0];
    if((c == 'p') && (strcmp(string, "photo") == 0)) {
        *formatPtr = FORMAT_PHOTO;
#ifdef _WIN32
    } else if((c == 'e') && (strcmp(string, "emf") == 0)) {
        *formatPtr = FORMAT_EMF;
    } else if((c == 'w') && (strcmp(string, "wmf") == 0)) {
        *formatPtr = FORMAT_WMF;
#endif /* _WIN32 */
    } else {
#ifdef _WIN32
        Tcl_AppendResult(interp, "bad format \"", string,
            "\": should be photo, emf, or wmf.", (char *)NULL);
#else
        Tcl_AppendResult(interp, "bad format \"", string,
            "\": should be photo.", (char *)NULL);
#endif /* _WIN32 */
        return TCL_ERROR;
    }
    return TCL_OK;
}

#ifdef _WIN32

/*
 * InitMetaFileHeader --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
static int
InitMetaFileHeader(
    Tk_Window tkwin,
    int width,
    int height,
    APMHEADER * mfhPtr)
{
    unsigned int *p;
    unsigned int sum;
    Screen *screen;
#define MM_INCH		25.4
    double dpiX, dpiY;

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
    for(p = (unsigned int *)mfhPtr;
        p < (unsigned int *)&(mfhPtr->checksum); p++) {
        sum ^= *p;
    }
    mfhPtr->checksum = sum;
    return TCL_OK;
}

/*
 * CreateAPMetaFile --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
static int
CreateAPMetaFile(
    Tcl_Interp * interp,
    HANDLE hMetaFile,
    HDC hDC,
    APMHEADER * mfhPtr,
    char *fileName)
{
    HANDLE hFile;
    HANDLE hMem;
    LPVOID buffer;
    int result;
    DWORD count, nBytes;

    result = TCL_ERROR;
    hMem = NULL;
    hFile = CreateFile((LPCWSTR) fileName,      /* File path */
        GENERIC_WRITE,  /* Access mode */
        0,     /* No sharing. */
        NULL,  /* Security attributes */
        CREATE_ALWAYS,  /* Overwrite any existing file */
        FILE_ATTRIBUTE_NORMAL, NULL);   /* No template file */
    if(hFile == INVALID_HANDLE_VALUE) {
        Tcl_AppendResult(interp, "can't create metafile \"", fileName,
            "\":", RbcLastError(), (char *)NULL);
        return TCL_ERROR;
    }
    if((!WriteFile(hFile, (LPVOID) mfhPtr, sizeof(APMHEADER), &count,
                NULL)) || (count != sizeof(APMHEADER))) {
        Tcl_AppendResult(interp, "can't create metafile header to \"",
            fileName, "\":", RbcLastError(), (char *)NULL);
        goto error;
    }
    nBytes = GetWinMetaFileBits(hMetaFile, 0, NULL, MM_ANISOTROPIC, hDC);
    hMem = GlobalAlloc(GHND, nBytes);
    if(hMem == NULL) {
        Tcl_AppendResult(interp, "can't create allocate global memory:",
            RbcLastError(), (char *)NULL);
        goto error;
    }
    buffer = (LPVOID) GlobalLock(hMem);
    if(!GetWinMetaFileBits(hMetaFile, nBytes, buffer, MM_ANISOTROPIC, hDC)) {
        Tcl_AppendResult(interp, "can't get metafile bits:",
            RbcLastError(), (char *)NULL);
        goto error;
    }
    if((!WriteFile(hFile, buffer, nBytes, &count, NULL)) || (count != nBytes)) {
        Tcl_AppendResult(interp, "can't write metafile bits:",
            RbcLastError(), (char *)NULL);
        goto error;
    }
    result = TCL_OK;
  error:
    CloseHandle(hFile);
    if(hMem != NULL) {
        GlobalUnlock(hMem);
        GlobalFree(hMem);
    }
    return result;
}

#endif /*_WIN32*/

/*
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
 */
static int
SnapOp(
    RbcGraph * graph,          /* Graph widget record */
    Tcl_Interp * interp,
    int argc,                  /* Not used. */
    const char **argv)
{
    int result;
    Pixmap drawable;
    int noBackingStore = 0;
    register int i;
    SnapData data;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    /* .g snap ?switches? name */
    data.height = Tk_Height(*(graph->win));
    data.width = Tk_Width(*(graph->win));
    data.format = FORMAT_PHOTO;
    /* Process switches  */
    i = RbcProcessSwitches(interp, snapSwitches, argc - 2, argv + 2,
        (char *)&data, RBC_SWITCH_OBJV_PARTIAL);
    if(i < 0) {
        return TCL_ERROR;
    }
    i += 2;
    if(i >= argc) {
        Tcl_AppendResult(interp, "missing name argument: should be \"",
            argv[0], "snap ?switches? name\"", (char *)NULL);
        return TCL_ERROR;
    }
    data.name = (char *)argv[i];
    if(data.width < 2) {
        data.width = 400;
    }
    if(data.height < 2) {
        data.height = 400;
    }
    /* Always re-compute the layout of the graph before snapping the photo. */
    graph->width = data.width;
    graph->height = data.height;
    RbcLayoutGraph(graph);

    drawable = Tk_WindowId(*(graph->win));
    if(data.format == FORMAT_PHOTO) {
        drawable = Tk_GetPixmap(graph->display, drawable, graph->width,
            graph->height, Tk_Depth(*(graph->win)));
#ifdef _WIN32
        assert(drawable != None);
#endif
        graph->flags |= RBC_RESET_WORLD;
        RbcDrawGraph(graph, drawable, noBackingStore);
        result = RbcSnapPhoto(interp, *(graph->win), drawable, 0, 0,
            data.width, data.height, data.width, data.height, data.name, 1.0);
        Tk_FreePixmap(graph->display, drawable);
#ifdef _WIN32
    } else if((data.format == FORMAT_WMF) || (data.format == FORMAT_EMF)) {
    TkWinDC drawableDC;
    TkWinDCState state;
    HDC hRefDC, hDC;
    HENHMETAFILE hMetaFile;
    Tcl_DString dString;
    char *title;

        hRefDC = TkWinGetDrawableDC(graph->display, drawable, &state);

        Tcl_DStringInit(&dString);
        Tcl_DStringAppend(&dString, "::graph ", -1);
        Tcl_DStringAppend(&dString, "\0", -1);
        Tcl_DStringAppend(&dString, Tk_PathName(*(graph->win)), -1);
        Tcl_DStringAppend(&dString, "\0", -1);
        title = Tcl_DStringValue(&dString);
        hDC = CreateEnhMetaFile(hRefDC, NULL, NULL, (LPCWSTR) title);
        Tcl_DStringFree(&dString);

        if(hDC == NULL) {
            Tcl_AppendResult(interp, "can't create metafile: ",
                RbcLastError(), (char *)NULL);
            return TCL_ERROR;
        }

        drawableDC.hdc = hDC;
        drawableDC.type = TWD_WINDC;

        RbcLayoutGraph(graph);
        graph->flags |= RBC_RESET_WORLD;
        RbcDrawGraph(graph, (Drawable) & drawableDC, FALSE);

        hMetaFile = CloseEnhMetaFile(hDC);
        if(strcmp(data.name, "CLIPBOARD") == 0) {
    HWND hWnd;

            hWnd = Tk_GetHWND(drawable);
            OpenClipboard(hWnd);
            EmptyClipboard();
            SetClipboardData(CF_ENHMETAFILE, hMetaFile);
            CloseClipboard();
            result = TCL_OK;
        } else {
            result = TCL_ERROR;
            if(data.format == FORMAT_WMF) {
    APMHEADER mfh;

                assert(sizeof(mfh) == 22);
                InitMetaFileHeader(*(graph->win), data.width, data.height,
                    &mfh);
                result = CreateAPMetaFile(interp, hMetaFile, hRefDC, &mfh,
                    data.name);
            } else {
    HENHMETAFILE hMetaFile2;

                hMetaFile2 = CopyEnhMetaFile(hMetaFile, (LPCWSTR) data.name);
                if(hMetaFile2 != NULL) {
                    result = TCL_OK;
                    DeleteEnhMetaFile(hMetaFile2);
                }
            }
            DeleteEnhMetaFile(hMetaFile);
        }
        TkWinReleaseDrawableDC(drawable, hRefDC, &state);

#endif /*_WIN32*/
    } else {
        Tcl_AppendResult(interp, "bad snapshot format", (char *)NULL);
        return TCL_ERROR;
    }
    graph->flags = RBC_MAP_WORLD;
    RbcEventuallyRedrawGraph(graph);
    return result;
}

static RbcOpSpec graphOps[] = {
    {"axis", 1, (RbcOp) RbcVirtualAxisOp, 2, 0, "oper ?args?",},
    {"bar", 2, (RbcOp) BarOp, 2, 0, "oper ?args?",},
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

static int nGraphOps = sizeof(graphOps) / sizeof(RbcOpSpec);

/*
 * RbcGraphInstCmdProc --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
int
RbcGraphInstCmdProc(
    ClientData clientData,
    Tcl_Interp * interp,
    int argc,
    const char *argv[])
{
    RbcOp proc;
    int result;
    RbcGraph *graph = clientData;

    proc = RbcGetOp(interp, nGraphOps, graphOps, RBC_OP_ARG1, argc, argv, 0);
    if(proc == NULL) {
        return TCL_ERROR;
    }
    Tcl_Preserve(graph);
    result = (*proc) (graph, interp, argc, argv);
    Tcl_Release(graph);
    return result;
}

/*
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
 */
static void
DrawMargins(
    RbcGraph * graph,
    Drawable drawable /* Pixmap or window to draw into */)
{             
    XRectangle rects[4];

    if(graph->win == NULL || *(graph->win) == NULL)
        return;
    /*
     * Draw the four outer rectangles which encompass the plotting
     * surface. This clears the surrounding area and clips the plot.
     */
    rects[0].x = rects[0].y = rects[3].x = rects[1].x = 0;
    rects[0].width = rects[3].width = (short int)graph->width;
    rects[0].height = (short int)graph->top;
    rects[3].y = graph->bottom;
    rects[3].height = graph->height - graph->bottom;
    rects[2].y = rects[1].y = graph->top;
    rects[1].width = graph->left;
    rects[2].height = rects[1].height = graph->bottom - graph->top;
    rects[2].x = graph->right;
    rects[2].width = graph->width - graph->right;

    if(graph->tile != NULL) {
        RbcSetTileOrigin(*(graph->win), graph->tile, 0, 0);
        RbcTileRectangles(*(graph->win), drawable, graph->tile, rects, 4);
    } else {
        XFillRectangles(graph->display, drawable, graph->fillGC, rects, 4);
    }

    /* Draw 3D border around the plotting area */

    if(graph->plotBorderWidth > 0) {
        int x, y, width, height;

        x = graph->left - graph->plotBorderWidth;
        y = graph->top - graph->plotBorderWidth;
        width = (graph->right - graph->left) + (2 * graph->plotBorderWidth);
        height = (graph->bottom - graph->top) + (2 * graph->plotBorderWidth);
        Tk_Draw3DRectangle(*(graph->win), drawable, graph->border, x, y,
            width, height, graph->plotBorderWidth, graph->plotRelief);
    }
    if(RbcLegendSite(graph->legend) & RBC_LEGEND_IN_MARGIN) {
        /* Legend is drawn on one of the graph margins */
        RbcDrawLegend(graph->legend, drawable);
    }
    if(graph->title != NULL) {
        RbcDrawText(*(graph->win), drawable, graph->title,
            &graph->titleTextStyle, graph->titleX, graph->titleY);
    }
    RbcDrawAxes(graph, drawable);

}

/*
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
 */
static void
DrawPlotRegion(
    RbcGraph * graph,
    Drawable drawable)
{              /* Pixmap or window to draw into */
    /* Clear the background of the plotting area. */
    XFillRectangle(graph->display, drawable, graph->plotFillGC,
        graph->left, graph->top, graph->right - graph->left + 1,
        graph->bottom - graph->top + 1);

    /* Draw the elements, markers, legend, and axis limits. */

    if(!graph->gridPtr->hidden) {
        RbcDrawGrid(graph, drawable);
    }
    RbcDrawMarkers(graph, drawable, RBC_MARKER_UNDER);
    if((RbcLegendSite(graph->legend) & RBC_LEGEND_IN_PLOT) &&
        (!RbcLegendIsRaised(graph->legend))) {
        RbcDrawLegend(graph->legend, drawable);
    }
    RbcDrawAxisLimits(graph, drawable);
    RbcDrawElements(graph, drawable);
}

/*
 * RbcLayoutGraph --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
void
RbcLayoutGraph(
    RbcGraph * graph)
{
    if(graph->flags & RBC_RESET_AXES) {
        RbcResetAxes(graph);
    }
    if(graph->flags & RBC_LAYOUT_NEEDED) {
        RbcLayoutMargins(graph);
        graph->flags &= ~RBC_LAYOUT_NEEDED;
    }
    /* Compute coordinate transformations for graph components */
    if((graph->vRange > 1) && (graph->hRange > 1)) {
        if(graph->flags & RBC_MAP_WORLD) {
            RbcMapAxes(graph);
        }
        RbcMapElements(graph);
        RbcMapMarkers(graph);
        RbcMapGrid(graph);
        graph->flags &= ~(RBC_MAP_ALL);
    }
}

/*
 * RbcDrawGraph --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
void
RbcDrawGraph(
    RbcGraph * graph,
    Drawable drawable,         /* Pixmap or window to draw into */
    int backingStore)
{              /* If non-zero, use backing store for
                * plotting area. */
    if(graph->win == NULL || *(graph->win) == NULL)
        return;
    if(backingStore) {
        /*
         * Create another pixmap to save elements if one doesn't
         * already exist or the size of the window has changed.
         */
        if((graph->backPixmap == None) ||
            (graph->backWidth != graph->width) ||
            (graph->backHeight != graph->height)) {

            if(graph->backPixmap != None) {
                Tk_FreePixmap(graph->display, graph->backPixmap);
            }
            graph->backPixmap = Tk_GetPixmap(graph->display,
                Tk_WindowId(*(graph->win)), graph->width,
                graph->height, Tk_Depth(*(graph->win)));
            graph->backWidth = graph->width;
            graph->backHeight = graph->height;
            graph->flags |= RBC_REDRAW_BACKING_STORE;
        }
        if(graph->flags & RBC_REDRAW_BACKING_STORE) {
            /* The backing store is new or out-of-date. */
            DrawPlotRegion(graph, graph->backPixmap);
            graph->flags &= ~RBC_REDRAW_BACKING_STORE;
        }

        /* Copy the pixmap to the one used for drawing the entire graph. */

        XCopyArea(graph->display, graph->backPixmap, drawable,
            graph->drawGC, graph->left, graph->top,
            (graph->right - graph->left + 1),
            (graph->bottom - graph->top + 1), graph->left, graph->top);
    } else {
        DrawPlotRegion(graph, drawable);
    }

    /* Draw markers above elements */
    RbcDrawMarkers(graph, drawable, RBC_MARKER_ABOVE);
    RbcDrawActiveElements(graph, drawable);

    if(graph->flags & RBC_DRAW_MARGINS) {
        DrawMargins(graph, drawable);
    }
    if((RbcLegendSite(graph->legend) & RBC_LEGEND_IN_PLOT) &&
        (RbcLegendIsRaised(graph->legend))) {
        RbcDrawLegend(graph->legend, drawable);
    }
    /* Draw 3D border just inside of the focus highlight ring. */
    if((graph->borderWidth > 0) && (graph->relief != TK_RELIEF_FLAT)) {
        Tk_Draw3DRectangle(*(graph->win), drawable, graph->border,
            graph->highlightWidth, graph->highlightWidth,
            graph->width - 2 * graph->highlightWidth,
            graph->height - 2 * graph->highlightWidth,
            graph->borderWidth, graph->relief);
    }
    /* Draw focus highlight ring. */
    if((graph->highlightWidth > 0) && (graph->flags & RBC_GRAPH_FOCUS)) {
    GC  gc;

        gc = Tk_GCForColor(graph->highlightColor, drawable);
        Tk_DrawFocusHighlight(*(graph->win), gc, graph->highlightWidth,
            drawable);
    }
}

/*
 * UpdateMarginTraces --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
static void
UpdateMarginTraces(
    RbcGraph * graph)
{
    RbcMargin *marginPtr;
    int size;
    register int i;

    for(i = 0; i < 4; i++) {
        marginPtr = graph->margins + i;
        if(marginPtr->varName != NULL) {        /* Trigger variable traces */
            if((marginPtr->site == RBC_MARGIN_LEFT) ||
                (marginPtr->site == RBC_MARGIN_RIGHT)) {
                size = marginPtr->width;
            } else {
                size = marginPtr->height;
            }
            Tcl_SetVar2Ex(graph->interp, marginPtr->varName, NULL,
                Tcl_NewIntObj(size), TCL_GLOBAL_ONLY);
        }
    }
}

/*
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
 */
static void
DisplayGraph(
    ClientData clientData)
{
    RbcGraph *graph = clientData;
    Pixmap drawable;
    graph->flags &= ~RBC_REDRAW_PENDING;
    if(graph->win == NULL || *(graph->win) == NULL)
        return;

    if(RbcGraphUpdateNeeded(graph)) {
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
    graph->width = Tk_Width(*(graph->win));
    graph->height = Tk_Height(*(graph->win));
    RbcLayoutGraph(graph);
    RbcUpdateCrosshairs(graph);
    if(!Tk_IsMapped(*(graph->win))) {
        /* The graph's window isn't displayed, so don't bother
         * drawing anything.  By getting this far, we've at least
         * computed the coordinates of the graph's new layout.  */
        return;
    }

    /* Disable crosshairs before redisplaying to the screen */
    RbcDisableCrosshairs(graph);
    /*
     * Create a pixmap the size of the window for double buffering.
     */
    if(graph->doubleBuffer) {
        drawable = Tk_GetPixmap(graph->display, Tk_WindowId(*(graph->win)),
            graph->width, graph->height, Tk_Depth(*(graph->win)));
    } else {
        drawable = Tk_WindowId(*(graph->win));
    }
#ifdef _WIN32
    assert(drawable != None);
#endif
    RbcDrawGraph(graph, drawable, graph->backingStore && graph->doubleBuffer);
    if(graph->flags & RBC_DRAW_MARGINS) {
        XCopyArea(graph->display, drawable, Tk_WindowId(*(graph->win)),
            graph->drawGC, 0, 0, graph->width, graph->height, 0, 0);
    } else {
        XCopyArea(graph->display, drawable, Tk_WindowId(*(graph->win)),
            graph->drawGC, graph->left, graph->top,
            (graph->right - graph->left + 1),
            (graph->bottom - graph->top + 1), graph->left, graph->top);
    }
    if(graph->doubleBuffer) {
        Tk_FreePixmap(graph->display, drawable);
    }
    RbcEnableCrosshairs(graph);
    graph->flags &= ~RBC_RESET_WORLD;
    UpdateMarginTraces(graph);
}

/*
 * RbcGetGraphFromWindowData --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
RbcGraph *
RbcGetGraphFromWindowData(
    Tk_Window tkwin)
{
RbcGraph *graph;

    while(tkwin != NULL) {
        graph = (RbcGraph *) RbcGetWindowInstanceData(tkwin);
        if(graph != NULL) {
            return graph;
        }
        tkwin = Tk_Parent(tkwin);
    }
    return NULL;
}

/*
 * RbcGraphType --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 */
int
RbcGraphType(
    RbcGraph * graph)
{
    if(graph->classUid == rbcLineElementUid) {
        return RBC_GRAPH;
    } else if(graph->classUid == rbcBarElementUid) {
        return RBC_BARCHART;
    } else if(graph->classUid == rbcStripElementUid) {
        return RBC_STRIPCHART;
    }
    return 0;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
