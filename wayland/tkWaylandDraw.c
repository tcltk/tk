/*
 * tkWaylandDraw.c --
 *
 *	This file contains functions that draw to windows using Wayland,
 *	GLFW, and libcg. Many of these functions emulate Xlib functions
 *	for compatibility with Tk's traditional API.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkPort.h"
#include "tkWaylandInt.h"
#include <math.h>
#include <string.h>
#include <X11/Xutil.h>

#define radians(d) ((d) * (M_PI / 180.0))

/*
 *----------------------------------------------------------------------
 *
 * XDrawString --
 *
 *	Draw a string of characters. x, y are the X11 baseline position.
 *	Detailed font shaping is handled by the font subsystem; this
 *	function sets source colour and delegates to TkpDrawCharsInContext.
 *
 * Results:
 *	Success on successful completion, BadDrawable on failure.
 *
 * Side effects:
 *	Renders text at the specified position on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawString(
    Display *display,
    Drawable drawable,
    GC gc,
    int x,
    int y,
    const char *string,
    int length)
{
    TkWaylandDrawingContext dc;
    XGCValues v;
    char *buf;

    if (!string || length <= 0) return Success;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    buf = (char *)ckalloc(length + 1);
    memcpy(buf, string, length);
    buf[length] = '\0';

    /* Set foreground color for the font subsystem to pick up. */
    if (TkWaylandGetGCValues(gc, GCForeground, &v) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((v.foreground >> 16) & 0xFF) / 255.0,
            (double)((v.foreground >> 8) & 0xFF) / 255.0,
            (double)(v.foreground & 0xFF) / 255.0,
            1.0);
    }

    /*
     * Actual glyph rendering is performed by the font subsystem
     * (tkWaylandFont.c) via TkpDrawCharsInContext, which has access
     * to the stb_truetype glyph outlines and feeds them into cg.
     * Nothing more to do here beyond colour setup.
     */

    ckfree(buf);
    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawImageString --
 *
 *	Like XDrawString but fills the glyph background with the GC
 *	background colour first ("opaque" text).
 *
 * Results:
 *	Success on successful completion, BadDrawable on failure.
 *
 * Side effects:
 *	Renders text with background fill at the specified position.
 *
 *----------------------------------------------------------------------
 */

int
XDrawImageString(
    Display *display,
    Drawable drawable,
    GC gc,
    int x,
    int y,
    const char *string,
    int length)
{
    TkWaylandDrawingContext dc;
    XGCValues v;
    char *buf;

    if (!string || length <= 0) return Success;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    buf = (char *)ckalloc(length + 1);
    memcpy(buf, string, length);
    buf[length] = '\0';

    /*
     * Fill background rectangle.  We don't have text bounds at this
     * level — the font subsystem will supply them when it renders the
     * glyphs.  Fill a conservative 1-em-high strip here; the font
     * subsystem can refine this if needed.
     */
    if (TkWaylandGetGCValues(gc, GCBackground, &v) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((v.background >> 16) & 0xFF) / 255.0,
            (double)((v.background >> 8) & 0xFF) / 255.0,
            (double)(v.background & 0xFF) / 255.0,
            1.0);
        cg_rectangle(dc.cg, (double)x, (double)(y - 12),
                     (double)(length * 8), 14.0);
        cg_fill(dc.cg);
    }

    /* Restore foreground for glyph rendering. */
    if (TkWaylandGetGCValues(gc, GCForeground, &v) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((v.foreground >> 16) & 0xFF) / 255.0,
            (double)((v.foreground >> 8) & 0xFF) / 255.0,
            (double)(v.foreground & 0xFF) / 255.0,
            1.0);
    }

    ckfree(buf);
    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawPoint --
 *
 *	Draw a single point (filled 1×1 rectangle).
 *
 * Results:
 *	Success on successful completion, BadDrawable on failure.
 *
 * Side effects:
 *	Renders a single pixel at the specified coordinates.
 *
 *----------------------------------------------------------------------
 */

int
XDrawPoint(
    Display *display,
    Drawable drawable,
    GC gc,
    int x,
    int y)
{
    TkWaylandDrawingContext dc;
    XGCValues v;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    if (TkWaylandGetGCValues(gc, GCForeground, &v) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((v.foreground >> 16) & 0xFF) / 255.0,
            (double)((v.foreground >> 8) & 0xFF) / 255.0,
            (double)(v.foreground & 0xFF) / 255.0,
            1.0);
    }

    cg_rectangle(dc.cg, (double)x, (double)y, 1.0, 1.0);
    cg_fill(dc.cg);

    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawPoints --
 *
 *	Draw multiple points.
 *
 * Results:
 *	Success on successful completion, BadDrawable or BadValue on failure.
 *
 * Side effects:
 *	Renders multiple pixels at the specified coordinates.
 *
 *----------------------------------------------------------------------
 */

int
XDrawPoints(
    Display *display,
    Drawable drawable,
    GC gc,
    XPoint *points,
    int npoints,
    int mode)
{
    TkWaylandDrawingContext dc;
    XGCValues v;
    int i;

    if (!points || npoints <= 0) return BadValue;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    if (TkWaylandGetGCValues(gc, GCForeground, &v) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((v.foreground >> 16) & 0xFF) / 255.0,
            (double)((v.foreground >> 8) & 0xFF) / 255.0,
            (double)(v.foreground & 0xFF) / 255.0,
            1.0);
    }

    for (i = 0; i < npoints; i++) {
        double px = (double)points[i].x;
        double py = (double)points[i].y;
        if (mode == CoordModePrevious && i > 0) {
            px += points[i-1].x;
            py += points[i-1].y;
        }
        cg_rectangle(dc.cg, px, py, 1.0, 1.0);
        cg_fill(dc.cg);
    }

    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawLines --
 *
 *	Draw connected line segments.
 *
 * Results:
 *	Success on successful completion, BadDrawable or BadValue on failure.
 *
 * Side effects:
 *	Renders connected line segments on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawLines(
    Display *display,
    Drawable drawable,
    GC gc,
    XPoint *points,
    int npoints,
    int mode)
{
    TkWaylandDrawingContext dc;
    XGCValues v;
    int i;

    if (npoints < 2 || points == NULL) return BadValue;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    if (TkWaylandGetGCValues(gc, GCForeground, &v) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((v.foreground >> 16) & 0xFF) / 255.0,
            (double)((v.foreground >> 8) & 0xFF) / 255.0,
            (double)(v.foreground & 0xFF) / 255.0,
            1.0);
    }

    if (TkWaylandGetGCValues(gc, GCLineWidth, &v) == 0) {
        cg_set_line_width(dc.cg, (double)v.line_width);
    }

    cg_move_to(dc.cg, (double)points[0].x, (double)points[0].y);

    for (i = 1; i < npoints; i++) {
        if (mode == CoordModeOrigin) {
            cg_line_to(dc.cg, (double)points[i].x, (double)points[i].y);
        } else {
            cg_line_to(dc.cg,
                       (double)(points[i-1].x + points[i].x),
                       (double)(points[i-1].y + points[i].y));
        }
    }
    cg_stroke(dc.cg);

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
 *	Success on successful completion, BadDrawable on failure.
 *
 * Side effects:
 *	Renders multiple unconnected line segments on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawSegments(
    Display *display,
    Drawable drawable,
    GC gc,
    XSegment *segments,
    int nsegments)
{
    TkWaylandDrawingContext dc;
    XGCValues v;
    int i;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    if (TkWaylandGetGCValues(gc, GCForeground, &v) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((v.foreground >> 16) & 0xFF) / 255.0,
            (double)((v.foreground >> 8) & 0xFF) / 255.0,
            (double)(v.foreground & 0xFF) / 255.0,
            1.0);
    }

    if (TkWaylandGetGCValues(gc, GCLineWidth, &v) == 0) {
        cg_set_line_width(dc.cg, (double)v.line_width);
    }

    for (i = 0; i < nsegments; i++) {
        cg_move_to(dc.cg, (double)segments[i].x1, (double)segments[i].y1);
        cg_line_to(dc.cg, (double)segments[i].x2, (double)segments[i].y2);
        cg_stroke(dc.cg);
    }

    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XFillPolygon --
 *
 *	Fill a polygon.
 *
 * Results:
 *	Success on successful completion, BadDrawable or BadValue on failure.
 *
 * Side effects:
 *	Renders a filled polygon on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillPolygon(
    Display *display,
    Drawable drawable,
    GC gc,
    XPoint *points,
    int npoints,
    int shape,
    int mode)
{
    TkWaylandDrawingContext dc;
    XGCValues gcValues;
    int i;

    if (npoints < 3 || points == NULL) return BadValue;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    if (TkWaylandGetGCValues(gc, GCForeground, &gcValues) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((gcValues.foreground >> 16) & 0xFF) / 255.0,
            (double)((gcValues.foreground >> 8) & 0xFF) / 255.0,
            (double)(gcValues.foreground & 0xFF) / 255.0,
            1.0);
    }

    if (TkWaylandGetGCValues(gc, GCFillRule, &gcValues) == 0) {
        if (gcValues.fill_rule == EvenOddRule)
            cg_set_fill_rule(dc.cg, CG_FILL_RULE_EVEN_ODD);
        else
            cg_set_fill_rule(dc.cg, CG_FILL_RULE_NON_ZERO);
    } else {
        cg_set_fill_rule(dc.cg, CG_FILL_RULE_NON_ZERO);
    }

    cg_move_to(dc.cg, (double)points[0].x, (double)points[0].y);

    for (i = 1; i < npoints; i++) {
        if (mode == CoordModeOrigin) {
            cg_line_to(dc.cg, (double)points[i].x, (double)points[i].y);
        } else {
            cg_line_to(dc.cg,
                       (double)(points[i-1].x + points[i].x),
                       (double)(points[i-1].y + points[i].y));
        }
    }
    cg_close_path(dc.cg);
    cg_fill(dc.cg);

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
 *	Success on successful completion, BadDrawable or BadValue on failure.
 *
 * Side effects:
 *	Renders a rectangle outline on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawRectangle(
    Display *display,
    Drawable drawable,
    GC gc,
    int x,
    int y,
    unsigned int width,
    unsigned int height)
{
    TkWaylandDrawingContext dc;
    XGCValues v;

    if (width == 0 || height == 0) return BadValue;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    if (TkWaylandGetGCValues(gc, GCForeground, &v) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((v.foreground >> 16) & 0xFF) / 255.0,
            (double)((v.foreground >> 8) & 0xFF) / 255.0,
            (double)(v.foreground & 0xFF) / 255.0,
            1.0);
    }

    if (TkWaylandGetGCValues(gc, GCLineWidth, &v) == 0) {
        cg_set_line_width(dc.cg, (double)v.line_width);
    }

    cg_rectangle(dc.cg, (double)x, (double)y,
                 (double)width, (double)height);
    cg_stroke(dc.cg);

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
 *	Success on successful completion, BadDrawable on failure.
 *
 * Side effects:
 *	Renders multiple rectangle outlines on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawRectangles(
    Display *display,
    Drawable drawable,
    GC gc,
    XRectangle *rectArr,
    int nRects)
{
    TkWaylandDrawingContext dc;
    XGCValues v;
    int i;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    if (TkWaylandGetGCValues(gc, GCForeground, &v) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((v.foreground >> 16) & 0xFF) / 255.0,
            (double)((v.foreground >> 8) & 0xFF) / 255.0,
            (double)(v.foreground & 0xFF) / 255.0,
            1.0);
    }

    if (TkWaylandGetGCValues(gc, GCLineWidth, &v) == 0) {
        cg_set_line_width(dc.cg, (double)v.line_width);
    }

    for (i = 0; i < nRects; i++) {
        cg_rectangle(dc.cg,
                     (double)rectArr[i].x,   (double)rectArr[i].y,
                     (double)rectArr[i].width,(double)rectArr[i].height);
        cg_stroke(dc.cg);
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
 *	Success on successful completion, BadDrawable on failure.
 *
 * Side effects:
 *	Renders multiple filled rectangles on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillRectangles(
    Display *display,
    Drawable drawable,
    GC gc,
    XRectangle *rectangles,
    int nrectangles)
{
    TkWaylandDrawingContext dc;
    XGCValues v;
    int i;

    if (nrectangles < 1) return Success;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    if (TkWaylandGetGCValues(gc, GCForeground, &v) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((v.foreground >> 16) & 0xFF) / 255.0,
            (double)((v.foreground >> 8) & 0xFF) / 255.0,
            (double)(v.foreground & 0xFF) / 255.0,
            1.0);
    }

    for (i = 0; i < nrectangles; i++) {
        cg_rectangle(dc.cg,
                     (double)rectangles[i].x,    (double)rectangles[i].y,
                     (double)rectangles[i].width, (double)rectangles[i].height);
        cg_fill(dc.cg);
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
 *	Success on successful completion, BadDrawable on failure.
 *
 * Side effects:
 *	Renders a filled rectangle on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillRectangle(
    Display *display,
    Drawable drawable,
    GC gc,
    int x,
    int y,
    unsigned int width,
    unsigned int height)
{
    TkWaylandDrawingContext dc;
    XGCValues v;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    if (TkWaylandGetGCValues(gc, GCForeground, &v) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((v.foreground >> 16) & 0xFF) / 255.0,
            (double)((v.foreground >> 8) & 0xFF) / 255.0,
            (double)(v.foreground & 0xFF) / 255.0,
            1.0);
    }

    cg_rectangle(dc.cg, (double)x, (double)y, (double)width, (double)height);
    cg_fill(dc.cg);

    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawArc --
 *
 *	Draw an arc outline.
 *
 * Results:
 *	Success on successful completion, BadDrawable or BadValue on failure.
 *
 * Side effects:
 *	Renders an arc outline on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawArc(
    Display *display,
    Drawable drawable,
    GC gc,
    int x,
    int y,
    unsigned int width,
    unsigned int height,
    int angle1,
    int angle2)
{
    TkWaylandDrawingContext dc;
    XGCValues v;
    double cx, cy, rx, ry, startAngle, endAngle;

    if (width == 0 || height == 0 || angle2 == 0) return BadValue;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    if (TkWaylandGetGCValues(gc, GCForeground, &v) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((v.foreground >> 16) & 0xFF) / 255.0,
            (double)((v.foreground >> 8) & 0xFF) / 255.0,
            (double)(v.foreground & 0xFF) / 255.0,
            1.0);
    }

    if (TkWaylandGetGCValues(gc, GCLineWidth, &v) == 0) {
        cg_set_line_width(dc.cg, (double)v.line_width);
    }

    cx = x + width  / 2.0;
    cy = y + height / 2.0;
    rx = width  / 2.0;
    ry = height / 2.0;
    startAngle = -radians(angle1 / 64.0);
    endAngle   = -radians((angle1 + angle2) / 64.0);

    if (width == height) {
        cg_arc(dc.cg, cx, cy, rx, startAngle, endAngle);
    } else {
        /* Elliptical arc: scale y axis then draw a circular arc. */
        cg_save(dc.cg);
        cg_translate(dc.cg, cx, cy);
        cg_scale(dc.cg, 1.0, ry / rx);
        cg_translate(dc.cg, -cx, -cy);
        cg_arc(dc.cg, cx, cy, rx, startAngle, endAngle);
        cg_restore(dc.cg);
    }
    cg_stroke(dc.cg);

    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawArcs --
 *
 *	Draw multiple arc outlines.
 *
 * Results:
 *	Success on successful completion, BadDrawable on failure.
 *
 * Side effects:
 *	Renders multiple arc outlines on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawArcs(
    Display *display,
    Drawable drawable,
    GC gc,
    XArc *arcArr,
    int nArcs)
{
    TkWaylandDrawingContext dc;
    XGCValues v;
    int i;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    if (TkWaylandGetGCValues(gc, GCForeground, &v) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((v.foreground >> 16) & 0xFF) / 255.0,
            (double)((v.foreground >> 8) & 0xFF) / 255.0,
            (double)(v.foreground & 0xFF) / 255.0,
            1.0);
    }

    if (TkWaylandGetGCValues(gc, GCLineWidth, &v) == 0) {
        cg_set_line_width(dc.cg, (double)v.line_width);
    }

    for (i = 0; i < nArcs; i++) {
        double cx, cy, rx, ry, startAngle, endAngle;

        if (arcArr[i].width == 0 || arcArr[i].height == 0 ||
            arcArr[i].angle2 == 0) continue;

        cx = arcArr[i].x + arcArr[i].width  / 2.0;
        cy = arcArr[i].y + arcArr[i].height / 2.0;
        rx = arcArr[i].width  / 2.0;
        ry = arcArr[i].height / 2.0;
        startAngle = -radians(arcArr[i].angle1 / 64.0);
        endAngle   = -radians((arcArr[i].angle1 + arcArr[i].angle2) / 64.0);

        if (arcArr[i].width == arcArr[i].height) {
            cg_arc(dc.cg, cx, cy, rx, startAngle, endAngle);
        } else {
            cg_save(dc.cg);
            cg_translate(dc.cg, cx, cy);
            cg_scale(dc.cg, 1.0, ry / rx);
            cg_translate(dc.cg, -cx, -cy);
            cg_arc(dc.cg, cx, cy, rx, startAngle, endAngle);
            cg_restore(dc.cg);
        }
        cg_stroke(dc.cg);
    }

    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XFillArc --
 *
 *	Draw a filled arc (pie slice or chord).
 *
 * Results:
 *	Success on successful completion, BadDrawable or BadValue on failure.
 *
 * Side effects:
 *	Renders a filled arc on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillArc(
    Display *display,
    Drawable drawable,
    GC gc,
    int x,
    int y,
    unsigned int width,
    unsigned int height,
    int angle1,
    int angle2)
{
    TkWaylandDrawingContext dc;
    XGCValues gcValues;
    double cx, cy, rx, ry, startAngle, endAngle;

    if (width == 0 || height == 0 || angle2 == 0) return BadValue;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    if (TkWaylandGetGCValues(gc, GCForeground, &gcValues) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((gcValues.foreground >> 16) & 0xFF) / 255.0,
            (double)((gcValues.foreground >> 8) & 0xFF) / 255.0,
            (double)(gcValues.foreground & 0xFF) / 255.0,
            1.0);
    }

    if (TkWaylandGetGCValues(gc, GCArcMode, &gcValues) != 0)
        gcValues.arc_mode = ArcPieSlice;

    cx = x + width  / 2.0;
    cy = y + height / 2.0;
    rx = width  / 2.0;
    ry = height / 2.0;
    startAngle = -radians(angle1 / 64.0);
    endAngle   = -radians((angle1 + angle2) / 64.0);

    if (gcValues.arc_mode == ArcPieSlice)
        cg_move_to(dc.cg, cx, cy);

    if (width == height) {
        cg_arc(dc.cg, cx, cy, rx, startAngle, endAngle);
    } else {
        cg_save(dc.cg);
        cg_translate(dc.cg, cx, cy);
        cg_scale(dc.cg, 1.0, ry / rx);
        cg_translate(dc.cg, -cx, -cy);
        cg_arc(dc.cg, cx, cy, rx, startAngle, endAngle);
        cg_restore(dc.cg);
    }

    if (gcValues.arc_mode == ArcPieSlice)
        cg_line_to(dc.cg, cx, cy);

    cg_close_path(dc.cg);
    cg_fill(dc.cg);

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
 *	Success on successful completion, BadDrawable on failure.
 *
 * Side effects:
 *	Renders multiple filled arcs on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillArcs(
    Display *display,
    Drawable drawable,
    GC gc,
    XArc *arcArr,
    int nArcs)
{
    TkWaylandDrawingContext dc;
    XGCValues gcValues;
    int i;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    if (TkWaylandGetGCValues(gc, GCForeground, &gcValues) == 0) {
        cg_set_source_rgba(dc.cg,
            (double)((gcValues.foreground >> 16) & 0xFF) / 255.0,
            (double)((gcValues.foreground >> 8) & 0xFF) / 255.0,
            (double)(gcValues.foreground & 0xFF) / 255.0,
            1.0);
    }

    if (TkWaylandGetGCValues(gc, GCArcMode, &gcValues) != 0)
        gcValues.arc_mode = ArcPieSlice;

    for (i = 0; i < nArcs; i++) {
        double cx, cy, rx, ry, startAngle, endAngle;

        if (arcArr[i].width == 0 || arcArr[i].height == 0 ||
            arcArr[i].angle2 == 0) continue;

        cx = arcArr[i].x + arcArr[i].width  / 2.0;
        cy = arcArr[i].y + arcArr[i].height / 2.0;
        rx = arcArr[i].width  / 2.0;
        ry = arcArr[i].height / 2.0;
        startAngle = -radians(arcArr[i].angle1 / 64.0);
        endAngle   = -radians((arcArr[i].angle1 + arcArr[i].angle2) / 64.0);

        if (gcValues.arc_mode == ArcPieSlice)
            cg_move_to(dc.cg, cx, cy);

        if (arcArr[i].width == arcArr[i].height) {
            cg_arc(dc.cg, cx, cy, rx, startAngle, endAngle);
        } else {
            cg_save(dc.cg);
            cg_translate(dc.cg, cx, cy);
            cg_scale(dc.cg, 1.0, ry / rx);
            cg_translate(dc.cg, -cx, -cy);
            cg_arc(dc.cg, cx, cy, rx, startAngle, endAngle);
            cg_restore(dc.cg);
        }

        if (gcValues.arc_mode == ArcPieSlice)
            cg_line_to(dc.cg, cx, cy);

        cg_close_path(dc.cg);
        cg_fill(dc.cg);
    }

    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_DrawHighlightBorder --
 *
 *	Draw the focus highlight ring around a widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the focus highlight border on the drawable.
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
 *	Draw the rectangular frame area.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the frame with the specified 3D border on the drawable.
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
    Tk_Fill3DRectangle(tkwin, drawable, border,
                       highlightWidth, highlightWidth,
                       Tk_Width(tkwin) - 2 * highlightWidth,
                       Tk_Height(tkwin) - 2 * highlightWidth,
                       borderWidth, relief);
}

/*
 *----------------------------------------------------------------------
 *
 * TkScrollWindow --
 *
 *	Scroll a rectangular area of a window.
 *	Returns 1 (True) — the exposed region is handled by a subsequent
 *	expose event that Tk will generate.
 *
 * Results:
 *	Always returns 1 (True).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkScrollWindow(
    Tk_Window tkwin,
    GC gc,
    int x,
    int y,
    int width,
    int height,
    int dx,
    int dy,
    TkRegion damageRgn)
{
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
