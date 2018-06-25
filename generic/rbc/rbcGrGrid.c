/*
 * rbcGrGrid.c --
 *
 *      This module implements grid lines for the rbc graph widget.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

extern Tk_CustomOption rbcDistanceOption;
extern Tk_CustomOption rbcDashesOption;
extern Tk_CustomOption rbcAnyXAxisOption;
extern Tk_CustomOption rbcAnyYAxisOption;

#define DEF_GRID_DASHES             "dot"
#define DEF_GRID_FOREGROUND         "gray64"
#define DEF_GRID_FG_MONO            "black"
#define DEF_GRID_LINE_WIDTH         "0"
#define DEF_GRID_HIDE_BARCHART      "no"
#define DEF_GRID_HIDE_GRAPH         "yes"
#define DEF_GRID_MINOR              "yes"
#define DEF_GRID_MAP_X_GRAPH        "x"
#define DEF_GRID_MAP_X_BARCHART     (char *)NULL
#define DEF_GRID_MAP_Y              "y"
#define DEF_GRID_POSITION           (char *)NULL

static Tk_ConfigSpec configSpecs[] = {
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_GRID_FOREGROUND,
            Tk_Offset(RbcGrid, colorPtr),
        TK_CONFIG_COLOR_ONLY | RBC_ALL_GRAPHS},
    {TK_CONFIG_COLOR, "-color", "color", "color", DEF_GRID_FG_MONO,
        Tk_Offset(RbcGrid, colorPtr), TK_CONFIG_MONO_ONLY | RBC_ALL_GRAPHS},
    {TK_CONFIG_CUSTOM, "-dashes", "dashes", "Dashes", DEF_GRID_DASHES,
            Tk_Offset(RbcGrid, dashes), TK_CONFIG_NULL_OK | RBC_ALL_GRAPHS,
        &rbcDashesOption},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_GRID_HIDE_BARCHART,
        Tk_Offset(RbcGrid, hidden), RBC_BARCHART},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_GRID_HIDE_GRAPH,
        Tk_Offset(RbcGrid, hidden), RBC_GRAPH | RBC_STRIPCHART},
    {TK_CONFIG_CUSTOM, "-linewidth", "lineWidth", "Linewidth",
            DEF_GRID_LINE_WIDTH, Tk_Offset(RbcGrid, lineWidth),
        TK_CONFIG_DONT_SET_DEFAULT | RBC_ALL_GRAPHS, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX", DEF_GRID_MAP_X_GRAPH,
            Tk_Offset(RbcGrid, axes.x), RBC_GRAPH | RBC_STRIPCHART,
        &rbcAnyXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX", DEF_GRID_MAP_X_BARCHART,
        Tk_Offset(RbcGrid, axes.x), RBC_BARCHART, &rbcAnyXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY", DEF_GRID_MAP_Y,
        Tk_Offset(RbcGrid, axes.y), RBC_ALL_GRAPHS, &rbcAnyYAxisOption},
    {TK_CONFIG_BOOLEAN, "-minor", "minor", "Minor", DEF_GRID_MINOR,
            Tk_Offset(RbcGrid, minorGrid),
        TK_CONFIG_DONT_SET_DEFAULT | RBC_ALL_GRAPHS},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

static void     ConfigureGrid(
    RbcGraph * graphPtr,
    RbcGrid * gridPtr);
static int      CgetOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      ConfigureOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      MapOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      UnmapOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      ToggleOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);

/*
 *----------------------------------------------------------------------
 *
 * ConfigureGrid --
 *
 *      Configures attributes of the grid such as line width,
 *      dashes, and position.  The grid are first turned off
 *      before any of the attributes changes.
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      Crosshair GC is allocated.
 *
 *----------------------------------------------------------------------
 */
static void
ConfigureGrid(
    RbcGraph * graphPtr,
    RbcGrid * gridPtr)
{
    XGCValues       gcValues;
    unsigned long   gcMask;
    GC              newGC;

    gcValues.background = gcValues.foreground = gridPtr->colorPtr->pixel;
    gcValues.line_width = RbcLineWidth(gridPtr->lineWidth);
    gcMask = (GCForeground | GCBackground | GCLineWidth);
    if (RbcLineIsDashed(gridPtr->dashes)) {
        gcValues.line_style = LineOnOffDash;
        gcMask |= GCLineStyle;
    }
    newGC = RbcGetPrivateGC(graphPtr->tkwin, gcMask, &gcValues);
    if (RbcLineIsDashed(gridPtr->dashes)) {
        RbcSetDashes(graphPtr->display, newGC, &(gridPtr->dashes));
    }
    if (gridPtr->gc != NULL) {
        RbcFreePrivateGC(graphPtr->display, gridPtr->gc);
    }
    gridPtr->gc = newGC;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcMapGrid --
 *
 *      Determines the coordinates of the line segments corresponding
 *      to the grid lines for each axis.
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
RbcMapGrid(
    RbcGraph * graphPtr)
{
    RbcGrid        *gridPtr = (RbcGrid *) graphPtr->gridPtr;
    int             nSegments;
    RbcSegment2D   *segments;

    if (gridPtr->x.segments != NULL) {
        ckfree((char *) gridPtr->x.segments);
        gridPtr->x.segments = NULL;
    }
    if (gridPtr->y.segments != NULL) {
        ckfree((char *) gridPtr->y.segments);
        gridPtr->y.segments = NULL;
    }
    gridPtr->x.nSegments = gridPtr->y.nSegments = 0;
    /*
     * Generate line segments to represent the grid.  Line segments
     * are calculated from the major tick intervals of each axis mapped.
     */
    RbcGetAxisSegments(graphPtr, gridPtr->axes.x, &segments, &nSegments);
    if (nSegments > 0) {
        gridPtr->x.nSegments = nSegments;
        gridPtr->x.segments = segments;
    }
    RbcGetAxisSegments(graphPtr, gridPtr->axes.y, &segments, &nSegments);
    if (nSegments > 0) {
        gridPtr->y.nSegments = nSegments;
        gridPtr->y.segments = segments;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcDrawGrid --
 *
 *      Draws the grid lines associated with each axis.
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
RbcDrawGrid(
    RbcGraph * graphPtr,
    Drawable drawable)
{                               /* Pixmap or window to draw into */
    RbcGrid        *gridPtr = (RbcGrid *) graphPtr->gridPtr;

    if (gridPtr->hidden) {
        return;
    }
    if (gridPtr->x.nSegments > 0) {
        RbcDraw2DSegments(graphPtr->display, drawable, gridPtr->gc,
            gridPtr->x.segments, gridPtr->x.nSegments);
    }
    if (gridPtr->y.nSegments > 0) {
        RbcDraw2DSegments(graphPtr->display, drawable, gridPtr->gc,
            gridPtr->y.segments, gridPtr->y.nSegments);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcGridToPostScript --
 *
 *      Prints the grid lines associated with each axis.
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
RbcGridToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken)
{
    RbcGrid        *gridPtr = (RbcGrid *) graphPtr->gridPtr;

    if (gridPtr->hidden) {
        return;
    }
    RbcLineAttributesToPostScript(psToken, gridPtr->colorPtr,
        gridPtr->lineWidth, &(gridPtr->dashes), CapButt, JoinMiter);
    if (gridPtr->x.nSegments > 0) {
        Rbc2DSegmentsToPostScript(psToken, gridPtr->x.segments,
            gridPtr->x.nSegments);
    }
    if (gridPtr->y.nSegments > 0) {
        Rbc2DSegmentsToPostScript(psToken, gridPtr->y.segments,
            gridPtr->y.nSegments);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcDestroyGrid --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Grid GC is released.
 *
 *----------------------------------------------------------------------
 */
void
RbcDestroyGrid(
    RbcGraph * graphPtr)
{
    RbcGrid        *gridPtr = (RbcGrid *) graphPtr->gridPtr;

    Tk_FreeOptions(configSpecs, (char *) gridPtr, graphPtr->display,
        RbcGraphType(graphPtr));
    if (gridPtr->gc != NULL) {
        RbcFreePrivateGC(graphPtr->display, gridPtr->gc);
    }
    if (gridPtr->x.segments != NULL) {
        ckfree((char *) gridPtr->x.segments);
    }
    if (gridPtr->y.segments != NULL) {
        ckfree((char *) gridPtr->y.segments);
    }
    ckfree((char *) gridPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcCreateGrid --
 *
 *      Creates and initializes a new grid structure.
 *
 * Results:
 *      Returns TCL_ERROR if the configuration failed, otherwise TCL_OK.
 *
 * Side Effects:
 *      Memory for grid structure is allocated.
 *
 *----------------------------------------------------------------------
 */
int
RbcCreateGrid(
    RbcGraph * graphPtr)
{
    RbcGrid        *gridPtr;

    gridPtr = RbcCalloc(1, sizeof(RbcGrid));
    assert(gridPtr);
    gridPtr->minorGrid = TRUE;
    graphPtr->gridPtr = gridPtr;

    if (RbcConfigureWidgetComponent(graphPtr->interp, graphPtr->tkwin, "grid",
            "Grid", configSpecs, 0, (const char **) NULL, (char *) gridPtr,
            RbcGraphType(graphPtr)) != TCL_OK) {
        return TCL_ERROR;
    }
    ConfigureGrid(graphPtr, gridPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CgetOp --
 *
 *      Queries configuration attributes of the grid such as line
 *      width, dashes, and position.
 *
 * Results:
 *      A standard Tcl result.
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
    int argc,
    const char **argv)
{
    RbcGrid        *gridPtr = (RbcGrid *) graphPtr->gridPtr;

    return Tk_ConfigureValue(interp, graphPtr->tkwin, configSpecs,
        (char *) gridPtr, argv[3], RbcGraphType(graphPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      Queries or resets configuration attributes of the grid
 *      such as line width, dashes, and position.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Grid attributes are reset.  The graph is redrawn at the
 *      next idle point.
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
    RbcGrid        *gridPtr = (RbcGrid *) graphPtr->gridPtr;
    int             flags;

    flags = RbcGraphType(graphPtr) | TK_CONFIG_ARGV_ONLY;
    if (argc == 3) {
        return Tk_ConfigureInfo(interp, graphPtr->tkwin, configSpecs,
            (char *) gridPtr, (char *) NULL, flags);
    } else if (argc == 4) {
        return Tk_ConfigureInfo(interp, graphPtr->tkwin, configSpecs,
            (char *) gridPtr, argv[3], flags);
    }
    if (Tk_ConfigureWidget(graphPtr->interp, graphPtr->tkwin, configSpecs,
            argc - 3, argv + 3, (char *) gridPtr, flags) != TCL_OK) {
        return TCL_ERROR;
    }
    ConfigureGrid(graphPtr, gridPtr);
    graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
    RbcEventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * MapOp --
 *
 *      Maps the grid.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Grid attributes are reset and the graph is redrawn if necessary.
 *
 *----------------------------------------------------------------------
 */
static int
MapOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcGrid        *gridPtr = (RbcGrid *) graphPtr->gridPtr;

    if (gridPtr->hidden) {
        gridPtr->hidden = FALSE;        /* Changes "-hide" configuration option */
        graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
        RbcEventuallyRedrawGraph(graphPtr);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * MapOp --
 *
 *      Maps or unmaps the grid (off or on).
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Grid attributes are reset and the graph is redrawn if necessary.
 *
 *----------------------------------------------------------------------
 */
static int
UnmapOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcGrid        *gridPtr = (RbcGrid *) graphPtr->gridPtr;

    if (!gridPtr->hidden) {
        gridPtr->hidden = TRUE; /* Changes "-hide" configuration option */
        graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
        RbcEventuallyRedrawGraph(graphPtr);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ToggleOp --
 *
 *      Toggles the state of the grid shown/hidden.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Grid is hidden/displayed. The graph is redrawn at the next
 *      idle time.
 *
 *----------------------------------------------------------------------
 */
static int
ToggleOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcGrid        *gridPtr = (RbcGrid *) graphPtr->gridPtr;

    gridPtr->hidden = (!gridPtr->hidden);
    graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
    RbcEventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

static RbcOpSpec gridOps[] = {
    {"cget", 2, (RbcOp) CgetOp, 4, 4, "option",},
    {"configure", 2, (RbcOp) ConfigureOp, 3, 0, "?options...?",},
    {"off", 2, (RbcOp) UnmapOp, 3, 3, "",},
    {"on", 2, (RbcOp) MapOp, 3, 3, "",},
    {"toggle", 1, (RbcOp) ToggleOp, 3, 3, "",},
};

static int      nGridOps = sizeof(gridOps) / sizeof(RbcOpSpec);

/*
 *----------------------------------------------------------------------
 *
 * RbcGridOp --
 *
 *      User routine to configure grid lines.  Grids are drawn
 *      at major tick intervals across the graph.
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      Grid may be drawn in the plotting area.
 *
 *----------------------------------------------------------------------
 */
int
RbcGridOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcOp           proc;

    proc = RbcGetOp(interp, nGridOps, gridOps, RBC_OP_ARG2, argc, argv, 0);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    return (*proc) (graphPtr, interp, argc, argv);
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
