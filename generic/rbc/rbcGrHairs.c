/*
 * rbcGrHairs.c --
 *
 *      This module implements crosshairs for the rbc graph widget.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
*/

#include "rbcInt.h"

extern Tk_CustomOption rbcPointOption;
extern Tk_CustomOption rbcDistanceOption;
extern Tk_CustomOption rbcDashesOption;

/*
 * -------------------------------------------------------------------
 *
 * RbcCrosshairs --
 *
 *      Contains the line segments positions and graphics context used
 *      to simulate crosshairs (by XORing) on the graph.
 *
 * -------------------------------------------------------------------
 */

typedef struct RbcCrosshairs {

    XPoint          hotSpot;    /* Hot spot for crosshairs */
    int             visible;    /* Internal state of crosshairs. If non-zero,
                                 * crosshairs are displayed. */
    int             hidden;     /* If non-zero, crosshairs are not displayed.
                                 * This is not necessarily consistent with the
                                 * internal state variable.  This is true when
                                 * the hot spot is off the graph.  */
    RbcDashes       dashes;     /* Dashstyle of the crosshairs. This represents
                                 * an array of alternatingly drawn pixel
                                 * values. If NULL, the hairs are drawn as a
                                 * solid line */
    int             lineWidth;  /* Width of the simulated crosshair lines */
    XSegment        segArr[2];  /* Positions of line segments representing the
                                 * simulated crosshairs. */
    XColor         *colorPtr;   /* Foreground color of crosshairs */
    GC              gc;         /* Graphics context for crosshairs. Set to
                                 * GXxor to not require redraws of graph */
} RbcCrosshairs;

#define DEF_HAIRS_DASHES	(char *)NULL
#define DEF_HAIRS_FOREGROUND	"#000000"
#define DEF_HAIRS_FG_MONO	"#000000"
#define DEF_HAIRS_LINE_WIDTH	"0"
#define DEF_HAIRS_HIDE		"yes"
#define DEF_HAIRS_POSITION	(char *)NULL

static const Tk_ConfigSpec configSpecs[] = {
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_HAIRS_FOREGROUND,
        Tk_Offset(RbcCrosshairs, colorPtr), TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_HAIRS_FG_MONO,
        Tk_Offset(RbcCrosshairs, colorPtr), TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_CUSTOM, "-dashes", "dashes", "Dashes", DEF_HAIRS_DASHES,
            Tk_Offset(RbcCrosshairs, dashes), TK_CONFIG_NULL_OK,
        &rbcDashesOption},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_HAIRS_HIDE,
        Tk_Offset(RbcCrosshairs, hidden), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-linewidth", "lineWidth", "Linewidth",
            DEF_HAIRS_LINE_WIDTH, Tk_Offset(RbcCrosshairs, lineWidth),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-position", "position", "Position", DEF_HAIRS_POSITION,
        Tk_Offset(RbcCrosshairs, hotSpot), 0, &rbcPointOption},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

/* TODO new object code
static const Tk_OptionSpec optionSpecs[] = {
    {TK_OPTION_COLOR, "-color", "color", "Color", DEF_HAIRS_FOREGROUND, -1, Tk_Offset(Crosshairs, colorPtr), TK_CONFIG_COLOR_ONLY, NULL, 0},
    {TK_OPTION_COLOR, "-color", "color", "Color", DEF_HAIRS_FG_MONO, -1, Tk_Offset(Crosshairs, colorPtr), TK_CONFIG_MONO_ONLY, NULL, 0},
    {TK_OPTION_CUSTOM, "-dashes", "dashes", "Dashes", DEF_HAIRS_DASHES, -1, Tk_Offset(Crosshairs, dashes), TK_CONFIG_NULL_OK, &rbcDashesOption, 0},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_HAIRS_HIDE, -1, Tk_Offset(Crosshairs, hidden), TK_CONFIG_DONT_SET_DEFAULT, NULL, 0},
    {TK_OPTION_CUSTOM, "-linewidth", "lineWidth", "Linewidth", DEF_HAIRS_LINE_WIDTH, -1, Tk_Offset(Crosshairs, lineWidth), TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption, 0},
    {TK_OPTION_CUSTOM, "-position", "position", "Position", DEF_HAIRS_POSITION, -1, Tk_Offset(Crosshairs, hotSpot), 0, &rbcPointOption, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, -1, 0, NULL, 0}
};
*/

static void     TurnOffHairs(
    Tk_Window tkwin,
    RbcCrosshairs * chPtr);
static void     TurnOnHairs(
    RbcGraph * graphPtr,
    RbcCrosshairs * chPtr);
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
static int      OnOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      OffOp(
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
 * TurnOffHairs --
 *
 *      XOR's the existing line segments (representing the crosshairs),
 *      thereby erasing them.  The internal state of the crosshairs is
 *      tracked.
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      Crosshairs are erased.
 *
 *----------------------------------------------------------------------
 */
static void
TurnOffHairs(
    Tk_Window tkwin,
    RbcCrosshairs * chPtr)
{
    if (Tk_IsMapped(tkwin) && (chPtr->visible)) {
        XDrawSegments(Tk_Display(tkwin), Tk_WindowId(tkwin), chPtr->gc,
            chPtr->segArr, 2);
        chPtr->visible = FALSE;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TurnOnHairs --
 *
 *      Draws (by XORing) new line segments, creating the effect of
 *      crosshairs. The internal state of the crosshairs is tracked.
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      Crosshairs are displayed.
 *
 *----------------------------------------------------------------------
 */
static void
TurnOnHairs(
    RbcGraph * graphPtr,
    RbcCrosshairs * chPtr)
{
    if (Tk_IsMapped(graphPtr->tkwin) && (!chPtr->visible)) {
        /* Coordinates are off the graph */
        if (chPtr->hotSpot.x > graphPtr->right
            || chPtr->hotSpot.x < graphPtr->left
            || chPtr->hotSpot.y > graphPtr->bottom
            || chPtr->hotSpot.y < graphPtr->top)
            return;
        XDrawSegments(graphPtr->display, Tk_WindowId(graphPtr->tkwin),
            chPtr->gc, chPtr->segArr, 2);
        chPtr->visible = TRUE;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcConfigureCrosshairs --
 *
 *      Configures attributes of the crosshairs such as line width,
 *      dashes, and position.  The crosshairs are first turned off
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
void
RbcConfigureCrosshairs(
    RbcGraph * graphPtr)
{
    XGCValues       gcValues;
    unsigned long   gcMask;
    GC              newGC;
    long            colorValue;
    RbcCrosshairs  *chPtr = graphPtr->crosshairs;

    /*
     * Turn off the crosshairs temporarily. This is in case the new
     * configuration changes the size, style, or position of the lines.
     */
    TurnOffHairs(graphPtr->tkwin, chPtr);

    gcValues.function = GXxor;

    if (graphPtr->plotBg == NULL) {
        /* The graph's color option may not have been set yet */
        colorValue = WhitePixelOfScreen(Tk_Screen(graphPtr->tkwin));
    } else {
        colorValue = graphPtr->plotBg->pixel;
    }
    gcValues.background = colorValue;
    gcValues.foreground = (colorValue ^ chPtr->colorPtr->pixel);

    gcValues.line_width = RbcLineWidth(chPtr->lineWidth);
    gcMask = (GCForeground | GCBackground | GCFunction | GCLineWidth);
    if (RbcLineIsDashed(chPtr->dashes)) {
        gcValues.line_style = LineOnOffDash;
        gcMask |= GCLineStyle;
    }
    newGC = RbcGetPrivateGC(graphPtr->tkwin, gcMask, &gcValues);
    if (RbcLineIsDashed(chPtr->dashes)) {
        RbcSetDashes(graphPtr->display, newGC, &(chPtr->dashes));
    }
    if (chPtr->gc != NULL) {
        RbcFreePrivateGC(graphPtr->display, chPtr->gc);
    }
    chPtr->gc = newGC;

    /*
     * Are the new coordinates on the graph?
     */
    chPtr->segArr[0].x2 = chPtr->segArr[0].x1 = chPtr->hotSpot.x;
    chPtr->segArr[0].y1 = graphPtr->bottom;
    chPtr->segArr[0].y2 = graphPtr->top;
    chPtr->segArr[1].y2 = chPtr->segArr[1].y1 = chPtr->hotSpot.y;
    chPtr->segArr[1].x1 = graphPtr->left;
    chPtr->segArr[1].x2 = graphPtr->right;

    if (!chPtr->hidden) {
        TurnOnHairs(graphPtr, chPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcEnableCrosshairs --
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
RbcEnableCrosshairs(
    RbcGraph * graphPtr)
{
    if (!graphPtr->crosshairs->hidden) {
        TurnOnHairs(graphPtr, graphPtr->crosshairs);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcDisableCrosshairs --
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
RbcDisableCrosshairs(
    RbcGraph * graphPtr)
{
    if (!graphPtr->crosshairs->hidden) {
        TurnOffHairs(graphPtr->tkwin, graphPtr->crosshairs);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcUpdateCrosshairs --
 *
 *      Update the length of the hairs (not the hot spot).
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcUpdateCrosshairs(
    RbcGraph * graphPtr)
{
    RbcCrosshairs  *chPtr = graphPtr->crosshairs;

    chPtr->segArr[0].y1 = graphPtr->bottom;
    chPtr->segArr[0].y2 = graphPtr->top;
    chPtr->segArr[1].x1 = graphPtr->left;
    chPtr->segArr[1].x2 = graphPtr->right;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcDestroyCrosshairs --
 *
 *      TODO: Description
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      Crosshair GC is allocated.
 *
 *----------------------------------------------------------------------
 */
void
RbcDestroyCrosshairs(
    RbcGraph * graphPtr)
{
    RbcCrosshairs  *chPtr = graphPtr->crosshairs;

    Tk_FreeOptions(configSpecs, (char *) chPtr, graphPtr->display, 0);
    if (chPtr->gc != NULL) {
        RbcFreePrivateGC(graphPtr->display, chPtr->gc);
    }
    ckfree((char *) chPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcCreateCrosshairs --
 *
 *      Creates and initializes a new crosshair structure.
 *
 * Results:
 *      Returns TCL_ERROR if the crosshair structure can't be created,
 *      otherwise TCL_OK.
 *
 * Side Effects:
 *      Crosshair GC is allocated.
 *
 *----------------------------------------------------------------------
 */
int
RbcCreateCrosshairs(
    RbcGraph * graphPtr)
{
    RbcCrosshairs  *chPtr;

    chPtr = RbcCalloc(1, sizeof(RbcCrosshairs));
    assert(chPtr);
    chPtr->hidden = TRUE;
    chPtr->hotSpot.x = chPtr->hotSpot.y = -1;
    graphPtr->crosshairs = chPtr;

    if (RbcConfigureWidgetComponent(graphPtr->interp, graphPtr->tkwin,
            "crosshairs", "Crosshairs", configSpecs, 0, (const char **) NULL,
            (char *) chPtr, 0) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CgetOp --
 *
 *      Queries configuration attributes of the crosshairs such as
 *      line width, dashes, and position.
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
    int argc,                   /* Not used. */
    const char **argv)
{
    RbcCrosshairs  *chPtr = graphPtr->crosshairs;

    return Tk_ConfigureValue(interp, graphPtr->tkwin, configSpecs,
        (char *) chPtr, argv[3], 0);
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      Queries or resets configuration attributes of the crosshairs
 *      such as line width, dashes, and position.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Crosshairs are reset.
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
    RbcCrosshairs  *chPtr = graphPtr->crosshairs;

    if (argc == 3) {
        return Tk_ConfigureInfo(interp, graphPtr->tkwin, configSpecs,
            (char *) chPtr, (char *) NULL, 0);
    } else if (argc == 4) {
        return Tk_ConfigureInfo(interp, graphPtr->tkwin, configSpecs,
            (char *) chPtr, argv[3], 0);
    }
    if (Tk_ConfigureWidget(interp, graphPtr->tkwin, configSpecs, argc - 3,
            argv + 3, (char *) chPtr, TK_CONFIG_ARGV_ONLY) != TCL_OK) {
        return TCL_ERROR;
    }
    RbcConfigureCrosshairs(graphPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * OnOp --
 *
 *      Maps the crosshairs.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Crosshairs are reset if necessary.
 *
 *----------------------------------------------------------------------
 */
static int
OnOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcCrosshairs  *chPtr = graphPtr->crosshairs;

    if (chPtr->hidden) {
        TurnOnHairs(graphPtr, chPtr);
        chPtr->hidden = FALSE;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * OffOp --
 *
 *      Unmaps the crosshairs.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Crosshairs are reset if necessary.
 *
 *----------------------------------------------------------------------
 */
static int
OffOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcCrosshairs  *chPtr = graphPtr->crosshairs;

    if (!chPtr->hidden) {
        TurnOffHairs(graphPtr->tkwin, chPtr);
        chPtr->hidden = TRUE;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ToggleOp --
 *
 *      Toggles the state of the crosshairs.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Crosshairs are reset.
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
    RbcCrosshairs  *chPtr = graphPtr->crosshairs;

    chPtr->hidden = (chPtr->hidden == 0);
    if (chPtr->hidden) {
        TurnOffHairs(graphPtr->tkwin, chPtr);
    } else {
        TurnOnHairs(graphPtr, chPtr);
    }
    return TCL_OK;
}

static RbcOpSpec xhairOps[] = {
    {"cget", 2, (RbcOp) CgetOp, 4, 4, "option",},
    {"configure", 2, (RbcOp) ConfigureOp, 3, 0, "?options...?",},
    {"off", 2, (RbcOp) OffOp, 3, 3, "",},
    {"on", 2, (RbcOp) OnOp, 3, 3, "",},
    {"toggle", 1, (RbcOp) ToggleOp, 3, 3, "",},
};

static int      nXhairOps = sizeof(xhairOps) / sizeof(RbcOpSpec);

/*
 *----------------------------------------------------------------------
 *
 * RbcCrosshairsOp --
 *
 *      User routine to configure crosshair simulation.  Crosshairs
 *      are simulated by drawing line segments parallel to both axes
 *      using the XOR drawing function. The allows the lines to be
 *      erased (by drawing them again) without redrawing the entire
 *      graph.  Care must be taken to erase crosshairs before redrawing
 *      the graph and redraw them after the graph is redraw.
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      Crosshairs may be drawn in the plotting area.
 *
 *----------------------------------------------------------------------
 */
int
RbcCrosshairsOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcOp           proc;

    proc = RbcGetOp(interp, nXhairOps, xhairOps, RBC_OP_ARG2, argc, argv, 0);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    return (*proc) (graphPtr, interp, argc, argv);
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
