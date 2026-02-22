/*
 * tkWaylandDraw.c --
 *
 *	This file contains functions that draw to windows using Wayland,
 *	GLFW, and NanoVG. Many of these functions emulate Xlib functions
 *      for compatibility with Tk's traditional API.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkPort.h"
#include "tkGlfwInt.h"
#include <GLES3/gl3.h>
#include "nanovg.h"
#include <math.h>

#define radians(d)	((d) * (M_PI/180.0))

/*
 *----------------------------------------------------------------------
 *
 * XDrawLines --
 *
 *	Draw connected line segments using NanoVG with current GC settings.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws lines on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawLines(
    TCL_UNUSED(Display *),
    Drawable d,
    GC gc,
    XPoint *points,
    int npoints,
    int mode)
{
    TkWaylandDrawingContext dc;
    int i;
    
    if (npoints < 2 || points == NULL) {
        return BadValue;
    }
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
    /* Apply GC settings (line width, color, etc.) */
    TkGlfwApplyGC(dc.vg, gc);
    
    nvgBeginPath(dc.vg);
    nvgMoveTo(dc.vg, points[0].x, points[0].y);
    
    for (i = 1; i < npoints; i++) {
        if (mode == CoordModeOrigin) {
            nvgLineTo(dc.vg, points[i].x, points[i].y);
        } else {  /* CoordModePrevious */
            nvgLineTo(dc.vg, points[i-1].x + points[i].x,
                      points[i-1].y + points[i].y);
        }
    }
    
    nvgStroke(dc.vg);
    
    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawSegments --
 *
 *	Draw multiple unconnected line segments.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws line segments on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawSegments(
    TCL_UNUSED(Display *),
    Drawable d,
    GC gc,
    XSegment *segments,
    int nsegments)
{
    TkWaylandDrawingContext dc;
    int i;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
    /* Apply GC settings. */
    TkGlfwApplyGC(dc.vg, gc);
    
    for (i = 0; i < nsegments; i++) {
        nvgBeginPath(dc.vg);
        nvgMoveTo(dc.vg, segments[i].x1, segments[i].y1);
        nvgLineTo(dc.vg, segments[i].x2, segments[i].y2);
        nvgStroke(dc.vg);
    }
    
    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XFillPolygon --
 *
 *	Fill a polygon using NanoVG with current GC settings.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws a filled polygon on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillPolygon(
    TCL_UNUSED(Display *),
    Drawable d,
    GC gc,
    XPoint *points,
    int npoints,
    TCL_UNUSED(int),  /* shape - ignored */
    int mode)
{
    TkWaylandDrawingContext dc;
    XGCValues gcValues;
    int i;
    
    if (npoints < 3 || points == NULL) {
        return BadValue;
    }
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
    /* Apply GC settings (fill color, etc.) */
    TkGlfwApplyGC(dc.vg, gc);
    
    /* Get GC values for fill rule. */
    if (TkWaylandGetGCValues(gc, GCFillRule, &gcValues) == 0) {
        gcValues.fill_rule = WindingRule; /* Default */
    }
    
    nvgBeginPath(dc.vg);
    nvgMoveTo(dc.vg, points[0].x, points[0].y);
    
    for (i = 1; i < npoints; i++) {
        if (mode == CoordModeOrigin) {
            nvgLineTo(dc.vg, points[i].x, points[i].y);
        } else {  /* CoordModePrevious */
            nvgLineTo(dc.vg, points[i-1].x + points[i].x,
                      points[i-1].y + points[i].y);
        }
    }
    
    nvgClosePath(dc.vg);
    
    /* Set winding based on fill rule. */
    if (gcValues.fill_rule == EvenOddRule) {
        nvgPathWinding(dc.vg, NVG_HOLE);
    } else {
        nvgPathWinding(dc.vg, NVG_SOLID);
    }
    
    nvgFill(dc.vg);
    
    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawRectangle --
 *
 *	Draw a rectangle outline.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws a rectangle outline on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawRectangle(
    TCL_UNUSED(Display *),
    Drawable d,
    GC gc,
    int x, int y,
    unsigned int width,
    unsigned int height)
{
    TkWaylandDrawingContext dc;
    
    if (width == 0 || height == 0) {
        return BadValue;
    }
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
    /* Apply GC settings. */
    TkGlfwApplyGC(dc.vg, gc);
    
    nvgBeginPath(dc.vg);
    nvgRect(dc.vg, x, y, width, height);
    nvgStroke(dc.vg);
    
    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawRectangles --
 *
 *	Draw multiple rectangle outlines.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws rectangle outlines on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawRectangles(
    TCL_UNUSED(Display *),
    Drawable d,
    GC gc,
    XRectangle *rectArr,
    int nRects)
{
    TkWaylandDrawingContext dc;
    int i;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
    /* Apply GC settings. */
    TkGlfwApplyGC(dc.vg, gc);
    
    for (i = 0; i < nRects; i++) {
        nvgBeginPath(dc.vg);
        nvgRect(dc.vg, rectArr[i].x, rectArr[i].y,
                rectArr[i].width, rectArr[i].height);
        nvgStroke(dc.vg);
    }
    
    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XFillRectangles --
 *
 *	Fill multiple rectangles.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws filled rectangles on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillRectangles(
    TCL_UNUSED(Display *),
    Drawable d,
    GC gc,
    XRectangle *rectangles,
    int n_rectangles)
{
    TkWaylandDrawingContext dc;
    int i;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
    /* Apply GC settings (fill color). */
    TkGlfwApplyGC(dc.vg, gc);
    
    for (i = 0; i < n_rectangles; i++) {
        nvgBeginPath(dc.vg);
        nvgRect(dc.vg, rectangles[i].x, rectangles[i].y,
                rectangles[i].width, rectangles[i].height);
        nvgFill(dc.vg);
    }
    
    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XFillRectangle --
 *
 *	Fill a rectangle.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws filled rectangle on the specified drawable.
 *
 *----------------------------------------------------------------------
 */



int
XFillRectangle(
    Display *display,
    Drawable d,
    GC gc,
    int x, int y,
    unsigned int width,
    unsigned int height)
{
    XRectangle rect;
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    return XFillRectangles(display, d, gc, &rect, 1);
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawArc --
 *
 *	Draw an arc outline.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws an arc on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawArc(
    TCL_UNUSED(Display *),
    Drawable d,
    GC gc,
    int x, int y,
    unsigned int width,
    unsigned int height,
    int angle1,
    int angle2)
{
    TkWaylandDrawingContext dc;
    float cx, cy, rx, ry;
    float startAngle, endAngle;
    
    if (width == 0 || height == 0 || angle2 == 0) {
        return BadValue;
    }
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
    /* Apply GC settings. */
    TkGlfwApplyGC(dc.vg, gc);
    
    cx = x + width / 2.0f;
    cy = y + height / 2.0f;
    rx = width / 2.0f;
    ry = height / 2.0f;
    
    startAngle = -radians(angle1 / 64.0);
    endAngle = -radians((angle1 + angle2) / 64.0);
    
    nvgBeginPath(dc.vg);
    
    if (width == height) {
        nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
    } else {
        /* Ellipse: scale transform. */
        nvgSave(dc.vg);
        nvgTranslate(dc.vg, cx, cy);
        nvgScale(dc.vg, 1.0f, ry / rx);
        nvgTranslate(dc.vg, -cx, -cy);
        nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
        nvgRestore(dc.vg);
    }
    
    nvgStroke(dc.vg);
    
    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawArcs --
 *
 *	Draw multiple arcs.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws arcs on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawArcs(
    TCL_UNUSED(Display *),
    Drawable d,
    GC gc,
    XArc *arcArr,
    int nArcs)
{
    TkWaylandDrawingContext dc;
    int i;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
    /* Apply GC settings. */
    TkGlfwApplyGC(dc.vg, gc);
    
    for (i = 0; i < nArcs; i++) {
        float cx, cy, rx, ry;
        float startAngle, endAngle;
        
        if (arcArr[i].width == 0 || arcArr[i].height == 0 ||
            arcArr[i].angle2 == 0) {
            continue;
        }
        
        cx = arcArr[i].x + arcArr[i].width / 2.0f;
        cy = arcArr[i].y + arcArr[i].height / 2.0f;
        rx = arcArr[i].width / 2.0f;
        ry = arcArr[i].height / 2.0f;
        
        startAngle = -radians(arcArr[i].angle1 / 64.0);
        endAngle = -radians((arcArr[i].angle1 + arcArr[i].angle2) / 64.0);
        
        nvgBeginPath(dc.vg);
        
        if (arcArr[i].width == arcArr[i].height) {
            nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
        } else {
            nvgSave(dc.vg);
            nvgTranslate(dc.vg, cx, cy);
            nvgScale(dc.vg, 1.0f, ry / rx);
            nvgTranslate(dc.vg, -cx, -cy);
            nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
            nvgRestore(dc.vg);
        }
        
        nvgStroke(dc.vg);
    }
    
    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XFillArc --
 *
 *	Draw a filled arc.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws a filled arc on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillArc(
    TCL_UNUSED(Display *),
    Drawable d,
    GC gc,
    int x, int y,
    unsigned int width,
    unsigned int height,
    int angle1,
    int angle2)
{
    TkWaylandDrawingContext dc;
    XGCValues gcValues;
    float cx, cy, rx, ry;
    float startAngle, endAngle;
    
    if (width == 0 || height == 0 || angle2 == 0) {
        return BadValue;
    }
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
    /* Apply GC settings (fill color) */
    TkGlfwApplyGC(dc.vg, gc);
    
    /* Get GC values for arc mode. */
    if (TkWaylandGetGCValues(gc, GCArcMode, &gcValues) == 0) {
        gcValues.arc_mode = ArcPieSlice; /* Default */
    }
    
    cx = x + width / 2.0f;
    cy = y + height / 2.0f;
    rx = width / 2.0f;
    ry = height / 2.0f;
    
    startAngle = -radians(angle1 / 64.0);
    endAngle = -radians((angle1 + angle2) / 64.0);
    
    nvgBeginPath(dc.vg);
    
    if (gcValues.arc_mode == ArcPieSlice) {
        /* Pie slice: line from center to start, arc, line back to center. */
        nvgMoveTo(dc.vg, cx, cy);
    }
    
    if (width == height) {
        nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
    } else {
        nvgSave(dc.vg);
        nvgTranslate(dc.vg, cx, cy);
        nvgScale(dc.vg, 1.0f, ry / rx);
        nvgTranslate(dc.vg, -cx, -cy);
        nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
        nvgRestore(dc.vg);
    }
    
    if (gcValues.arc_mode == ArcPieSlice) {
        nvgLineTo(dc.vg, cx, cy);
    }
    
    nvgClosePath(dc.vg);
    nvgFill(dc.vg);
    
    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XFillArcs --
 *
 *	Draw multiple filled arcs.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws a filled arc for each array element on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillArcs(
    TCL_UNUSED(Display *),
    Drawable d,
    GC gc,
    XArc *arcArr,
    int nArcs)
{
    TkWaylandDrawingContext dc;
    XGCValues gcValues;
    int i;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
    /* Apply GC settings (fill color). */
    TkGlfwApplyGC(dc.vg, gc);
    
    /* Get GC values for arc mode. */
    if (TkWaylandGetGCValues(gc, GCArcMode, &gcValues) == 0) {
        gcValues.arc_mode = ArcPieSlice; /* Default */
    }
    
    for (i = 0; i < nArcs; i++) {
        float cx, cy, rx, ry;
        float startAngle, endAngle;
        
        if (arcArr[i].width == 0 || arcArr[i].height == 0 ||
            arcArr[i].angle2 == 0) {
            continue;
        }
        
        cx = arcArr[i].x + arcArr[i].width / 2.0f;
        cy = arcArr[i].y + arcArr[i].height / 2.0f;
        rx = arcArr[i].width / 2.0f;
        ry = arcArr[i].height / 2.0f;
        
        startAngle = -radians(arcArr[i].angle1 / 64.0);
        endAngle = -radians((arcArr[i].angle1 + arcArr[i].angle2) / 64.0);
        
        nvgBeginPath(dc.vg);
        
        if (gcValues.arc_mode == ArcPieSlice) {
            nvgMoveTo(dc.vg, cx, cy);
        }
        
        if (arcArr[i].width == arcArr[i].height) {
            nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
        } else {
            nvgSave(dc.vg);
            nvgTranslate(dc.vg, cx, cy);
            nvgScale(dc.vg, 1.0f, ry / rx);
            nvgTranslate(dc.vg, -cx, -cy);
            nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
            nvgRestore(dc.vg);
        }
        
        if (gcValues.arc_mode == ArcPieSlice) {
            nvgLineTo(dc.vg, cx, cy);
        }
        
        nvgClosePath(dc.vg);
        nvgFill(dc.vg);
    }
    
    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_DrawHighlightBorder --
 *
 *	This procedure draws a rectangular ring around the outside of a widget
 *	to indicate that it has received the input focus.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A rectangle "width" pixels wide is drawn.
 *
 *----------------------------------------------------------------------
 */

void
Tk_DrawHighlightBorder(
    Tk_Window tkwin,
    GC fgGC,
    GC bgGC,
    int highlightWidth,
    Drawable drawable)
{
    if (highlightWidth <= 1) {
        TkDrawInsetFocusHighlight(tkwin, fgGC, 1, drawable, 0);
    } else {
        TkDrawInsetFocusHighlight(tkwin, bgGC, highlightWidth, drawable, 0);
        if (fgGC != bgGC) {
            TkDrawInsetFocusHighlight(tkwin, fgGC, highlightWidth - 1,
                                      drawable, 0);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDrawFrameEx --
 *
 *	This procedure draws the rectangular frame area.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws inside the tkwin area.
 *
 *----------------------------------------------------------------------
 */

void
TkpDrawFrameEx(
    Tk_Window tkwin,
    Drawable drawable,
    Tk_3DBorder border,
    int highlightWidth,
    int borderWidth,
    int relief)
{
    Tk_Fill3DRectangle(tkwin, drawable, border, highlightWidth,
                       highlightWidth, Tk_Width(tkwin) - 2 * highlightWidth,
                       Tk_Height(tkwin) - 2 * highlightWidth,
                       borderWidth, relief);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
