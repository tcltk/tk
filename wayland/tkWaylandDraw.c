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
#include <GLES2/gl2.h>
#include "nanovg.h"
#include <math.h>
#include <X11/Xutil.h>

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
    
    NVGcontext *vg = TkGlfwGetNVGContext();
    if (!vg) return BadDrawable

    
    /* Apply GC settings (line width, color, etc.) */
    TkGlfwApplyGC(vg, gc);
    
    nvgBeginPath(vg);
    nvgMoveTo(vg, points[0].x, points[0].y);
    
    for (i = 1; i < npoints; i++) {
        if (mode == CoordModeOrigin) {
            nvgLineTo(vg, points[i].x, points[i].y);
        } else {  /* CoordModePrevious */
            nvgLineTo(vg, points[i-1].x + points[i].x,
                      points[i-1].y + points[i].y);
        }
    }
    
    nvgStroke(vg);
    

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
    
    NVGcontext *vg = TkGlfwGetNVGContext();
    if (!vg) return BadDrawable;
    
    /* Apply GC settings. */
    TkGlfwApplyGC(vg, gc);
    
    for (i = 0; i < nsegments; i++) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, segments[i].x1, segments[i].y1);
        nvgLineTo(vg, segments[i].x2, segments[i].y2);
        nvgStroke(vg);
    }
    
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
    
    NVGcontext *vg = TkGlfwGetNVGContext();
    if (!vg) return BadDrawable;
    
    /* Apply GC settings (fill color, etc.) */
    TkGlfwApplyGC(vg, gc);
    
    /* Get GC values for fill rule. */
    if (TkWaylandGetGCValues(gc, GCFillRule, &gcValues) == 0) {
        gcValues.fill_rule = WindingRule; /* Default */
    }
    
    nvgBeginPath(vg);
    nvgMoveTo(vg, points[0].x, points[0].y);
    
    for (i = 1; i < npoints; i++) {
        if (mode == CoordModeOrigin) {
            nvgLineTo(vg, points[i].x, points[i].y);
        } else {  /* CoordModePrevious */
            nvgLineTo(vg, points[i-1].x + points[i].x,
                      points[i-1].y + points[i].y);
        }
    }
    
    nvgClosePath(vg);
    
    /* Set winding based on fill rule. */
    if (gcValues.fill_rule == EvenOddRule) {
        nvgPathWinding(vg, NVG_HOLE);
    } else {
        nvgPathWinding(vg, NVG_SOLID);
    }
    
    nvgFill(vg);
    
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
    
    NVGcontext *vg = TkGlfwGetNVGContext();
    if (!vg) return BadDrawable;
    
    /* Apply GC settings. */
    TkGlfwApplyGC(vg, gc);
    
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgStroke(vg);
    
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
    
    NVGcontext *vg = TkGlfwGetNVGContext();
    if (!vg) return BadDrawable;
    
    /* Apply GC settings. */
    TkGlfwApplyGC(vg, gc);
    
    for (i = 0; i < nRects; i++) {
        nvgBeginPath(vg);
        nvgRect(vg, rectArr[i].x, rectArr[i].y,
                rectArr[i].width, rectArr[i].height);
        nvgStroke(vg);
    }
    
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
    
    NVGcontext *vg = TkGlfwGetNVGContext();
    if (!vg) return BadDrawable;
    
    /* Apply GC settings (fill color). */
    TkGlfwApplyGC(vg, gc);
    
    for (i = 0; i < n_rectangles; i++) {
        nvgBeginPath(vg);
        nvgRect(vg, rectangles[i].x, rectangles[i].y,
                rectangles[i].width, rectangles[i].height);
        nvgFill(vg);
    }
    
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
    
    NVGcontext *vg = TkGlfwGetNVGContext();
    if (!vg) return BadDrawable;
    
    /* Apply GC settings. */
    TkGlfwApplyGC(vg, gc);
    
    cx = x + width / 2.0f;
    cy = y + height / 2.0f;
    rx = width / 2.0f;
    ry = height / 2.0f;
    
    startAngle = -radians(angle1 / 64.0);
    endAngle = -radians((angle1 + angle2) / 64.0);
    
    nvgBeginPath(vg);
    
    if (width == height) {
        nvgArc(vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
    } else {
        /* Ellipse: scale transform. */
        nvgSave(vg);
        nvgTranslate(vg, cx, cy);
        nvgScale(vg, 1.0f, ry / rx);
        nvgTranslate(vg, -cx, -cy);
        nvgArc(vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
        nvgRestore(vg);
    }
    
    nvgStroke(vg);
    
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
    
    NVGcontext *vg = TkGlfwGetNVGContext();
    if (!vg) return BadDrawable;
    
    /* Apply GC settings. */
    TkGlfwApplyGC(vg, gc);
    
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
        
        nvgBeginPath(vg);
        
        if (arcArr[i].width == arcArr[i].height) {
            nvgArc(vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
        } else {
            nvgSave(vg);
            nvgTranslate(vg, cx, cy);
            nvgScale(vg, 1.0f, ry / rx);
            nvgTranslate(vg, -cx, -cy);
            nvgArc(vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
            nvgRestore(vg);
        }
        
        nvgStroke(vg);
    }
    
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
    
    NVGcontext *vg = TkGlfwGetNVGContext();
    if (!vg) return BadDrawable;
    
    /* Apply GC settings (fill color) */
    TkGlfwApplyGC(vg, gc);
    
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
    
    nvgBeginPath(vg);
    
    if (gcValues.arc_mode == ArcPieSlice) {
        /* Pie slice: line from center to start, arc, line back to center. */
        nvgMoveTo(vg, cx, cy);
    }
    
    if (width == height) {
        nvgArc(vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
    } else {
        nvgSave(vg);
        nvgTranslate(vg, cx, cy);
        nvgScale(vg, 1.0f, ry / rx);
        nvgTranslate(vg, -cx, -cy);
        nvgArc(vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
        nvgRestore(vg);
    }
    
    if (gcValues.arc_mode == ArcPieSlice) {
        nvgLineTo(vg, cx, cy);
    }
    
    nvgClosePath(vg);
    nvgFill(vg);
    

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
    
    NVGcontext *vg = TkGlfwGetNVGContext();
    if (!vg) return BadDrawable;
    
    /* Apply GC settings (fill color). */
    TkGlfwApplyGC(vg, gc);
    
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
        
        nvgBeginPath(vg);
        
        if (gcValues.arc_mode == ArcPieSlice) {
            nvgMoveTo(vg, cx, cy);
        }
        
        if (arcArr[i].width == arcArr[i].height) {
            nvgArc(vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
        } else {
            nvgSave(vg);
            nvgTranslate(vg, cx, cy);
            nvgScale(vg, 1.0f, ry / rx);
            nvgTranslate(vg, -cx, -cy);
            nvgArc(vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
            nvgRestore(vg);
        }
        
        if (gcValues.arc_mode == ArcPieSlice) {
            nvgLineTo(vg, cx, cy);
        }
        
        nvgClosePath(vg);
        nvgFill(vg);
    }
    
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
 *----------------------------------------------------------------------
 *
 * TkScrollWindow --
 *
 *	Scroll a rectangular area of a window.  Uses glBlitFramebuffer
 *	to copy pixels, with scissor test enabled to clip the copy to
 *	the destination rectangle.  The damage region is updated to
 *	include the area that becomes exposed after the scroll.
 *
 * Results:
 *	Returns 1 (True) on success, 0 (False) on failure.
 *
 * Side effects:
 *	Pixels within the source rectangle are moved to the destination.
 *	The damage region is enlarged to include the uncovered area.
 *
 *----------------------------------------------------------------------
 */

bool
TkScrollWindow(
    TCL_UNUSED(Tk_Window), 	/* Window to scroll. */
    TCL_UNUSED(GC),        	/* GC (not used in this implementation). */
    TCL_UNUSED(int), 		/* Source x. */
    TCL_UNUSED(int),        /* Source y. */
	TCL_UNUSED(int), 		/* Source width. */
	TCL_UNUSED(int),     	/* Source height. */
    TCL_UNUSED(int),		/* Destination x. */
    TCL_UNUSED(int),        /* Destination rect. */    	
    TCL_UNUSED(TkRegion))	/* Region to which exposed area is added. */
{
   
   /* Will implement if needed. */
    return 1;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
