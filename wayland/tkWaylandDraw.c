/*
 * tkWaylandDraw.c --
 *
 *	This file contains functions that draw to windows using Wayland,
 *	GLFW, and NanoVG. Many of these functions emulate Xlib functions
 *  for compatibility with Tk's traditional API.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "tkInt.h"
#include "tkPort.h"
#include "tkWaylandInt.h"
#include <GLES3/gl3.h>
#include "nanovg.h"
#include <math.h>
#include <string.h>
#include <X11/Xutil.h>

#define radians(d) ((d) * (M_PI / 180.0))

/*
 *----------------------------------------------------------------------
 *
 * Internal helpers
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * GetNVGFont --
 *
 *	Resolve a GC's font to an NVGcontext font id and pixel size.
 *	Falls back to the "sans" font registered during TkWaylandInitialize.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the fontIdOut and fontSizeOut parameters with the resolved
 *	font identifier and size.
 *
 *----------------------------------------------------------------------
 */

static void
GetNVGFont(NVGcontext *vg, GC gc, int *fontIdOut, float *fontSizeOut)
{
    int   fid   = nvgFindFont(vg, "sans");
    float fsize = 12.0f;

    if (gc) {
        XGCValues v;
        if (TkWaylandGetGCValues(gc, GCFont, &v) && v.font != None) {
            Tk_Font     tkfont = (Tk_Font)(intptr_t)v.font;
            Tk_FontMetrics fm;
            if (tkfont) {
                Tk_GetFontMetrics(tkfont, &fm);
                if (fm.linespace > 0 && fm.linespace < 256)
                    fsize = (float)fm.linespace;

                const char *fname = Tk_NameOfFont(tkfont);
                if (fname) {
                    if (strstr(fname, "bold") || strstr(fname, "Bold")) {
                        int bid = nvgFindFont(vg, "sans-bold");
                        if (bid >= 0) fid = bid;
                    } else if (strstr(fname, "mono")   ||
                               strstr(fname, "Courier") ||
                               strstr(fname, "Fixed")) {
                        int mid = nvgFindFont(vg, "mono");
                        if (mid >= 0) fid = mid;
                    }
                }
            }
        }
    }

    if (fid < 0) fid = 0;
    *fontIdOut  = fid;
    *fontSizeOut = fsize;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawString --
 *
 *	Draw a string of characters using NanoVG text rendering.
 *	x, y are the X11 baseline position.
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
    TCL_UNUSED(Display *),
    Drawable    drawable,
    GC          gc,
    int         x,
    int         y,
    const char *string,
    int         length)
{
    TkWaylandDrawingContext dc;
    int   fontId;
    float fontSize;
    char *buf;

    if (!string || length <= 0) return Success;

    int rc = TkWaylandBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;            

    buf = (char *)ckalloc(length + 1);
    memcpy(buf, string, length);
    buf[length] = '\0';

    GetNVGFont(dc.vg, gc, &fontId, &fontSize);
    nvgFontFaceId(dc.vg, fontId);
    nvgFontSize(dc.vg, fontSize);
    nvgTextAlign(dc.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

    /* Foreground color is already set by TkWaylandBeginDraw via TkWaylandApplyGC */
    nvgText(dc.vg, (float)x, (float)y, buf, NULL);

    ckfree(buf);
    TkWaylandEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawImageString --
 *
 *	Like XDrawString but fills the glyph background with the GC
 *	background colour first ("opaque" text).  Used for selected
 *	listbox rows and other highlighted text.
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
    TCL_UNUSED(Display *),
    Drawable    drawable,
    GC          gc,
    int         x,
    int         y,
    const char *string,
    int         length)
{
    TkWaylandDrawingContext dc;
    int   fontId;
    float fontSize;
    float bounds[4];
    char *buf;

    if (!string || length <= 0) return Success; 

    int rc = TkWaylandBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

    buf = (char *)ckalloc(length + 1);
    memcpy(buf, string, length);
    buf[length] = '\0';

    GetNVGFont(dc.vg, gc, &fontId, &fontSize);
    nvgFontFaceId(dc.vg, fontId);
    nvgFontSize(dc.vg, fontSize);
    nvgTextAlign(dc.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

    /* Measure text extent so we can fill the background. */
    nvgTextBounds(dc.vg, (float)x, (float)y, buf, NULL, bounds);

    /* Fill background with GC background colour. */
    {
        XGCValues v;
        if (TkWaylandGetGCValues(gc, GCBackground, &v)) {
            NVGcolor bg = TkWaylandPixelToNVG(v.background);
            nvgBeginPath(dc.vg);
            nvgRect(dc.vg, bounds[0], bounds[1],
                    bounds[2] - bounds[0], bounds[3] - bounds[1]);
            nvgFillColor(dc.vg, bg);
            nvgFill(dc.vg);
        }
    }

    /* Draw text in foreground color (restored by ApplyGC). */
    TkWaylandApplyGC(dc.vg, gc);
    nvgFontFaceId(dc.vg, fontId);
    nvgFontSize(dc.vg, fontSize);
    nvgTextAlign(dc.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgText(dc.vg, (float)x, (float)y, buf, NULL);

    ckfree(buf);
    TkWaylandEndDraw(&dc);
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
    TCL_UNUSED(Display *),
    Drawable drawable,
    GC       gc,
    int      x,
    int      y)
{
    TkWaylandDrawingContext dc;
    int rc = TkWaylandBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;
    nvgBeginPath(dc.vg);
    nvgRect(dc.vg, (float)x, (float)y, 1.0f, 1.0f);
    nvgFill(dc.vg);
    TkWaylandEndDraw(&dc);
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
    TCL_UNUSED(Display *),
    Drawable  drawable,
    GC        gc,
    XPoint   *points,
    int       npoints,
    int       mode)
{
    TkWaylandDrawingContext dc;
    int i;

    if (!points || npoints <= 0) return BadValue;
    int rc = TkWaylandBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

    for (i = 0; i < npoints; i++) {
        float px = (float)points[i].x;
        float py = (float)points[i].y;
        if (mode == CoordModePrevious && i > 0) {
            px += points[i-1].x;
            py += points[i-1].y;
        }
        nvgBeginPath(dc.vg);
        nvgRect(dc.vg, px, py, 1.0f, 1.0f);
        nvgFill(dc.vg);
    }

    TkWaylandEndDraw(&dc);
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
    TCL_UNUSED(Display *),
    Drawable drawable,
    GC       gc,
    XPoint  *points,
    int      npoints,
    int      mode)
{
    TkWaylandDrawingContext dc;
    int i;

    if (npoints < 2 || points == NULL) return BadValue;
    int rc = TkWaylandBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

    nvgBeginPath(dc.vg);
    nvgMoveTo(dc.vg, points[0].x, points[0].y);

    for (i = 1; i < npoints; i++) {
        if (mode == CoordModeOrigin) {
            nvgLineTo(dc.vg, points[i].x, points[i].y);
        } else {
            nvgLineTo(dc.vg,
                      points[i-1].x + points[i].x,
                      points[i-1].y + points[i].y);
        }
    }
    nvgStroke(dc.vg);

    TkWaylandEndDraw(&dc);
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
    TCL_UNUSED(Display *),
    Drawable   drawable,
    GC         gc,
    XSegment  *segments,
    int        nsegments)
{
    TkWaylandDrawingContext dc;
    int i;

    int rc = TkWaylandBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

    for (i = 0; i < nsegments; i++) {
        nvgBeginPath(dc.vg);
        nvgMoveTo(dc.vg, segments[i].x1, segments[i].y1);
        nvgLineTo(dc.vg, segments[i].x2, segments[i].y2);
        nvgStroke(dc.vg);
    }

    TkWaylandEndDraw(&dc);
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
    TCL_UNUSED(Display *),
    Drawable drawable,
    GC       gc,
    XPoint  *points,
    int      npoints,
    TCL_UNUSED(int),   /* shape */
    int      mode)
{
    TkWaylandDrawingContext dc;
    XGCValues gcValues;
    int i;

    if (npoints < 3 || points == NULL) return BadValue;
    int rc = TkWaylandBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

    if (TkWaylandGetGCValues(gc, GCFillRule, &gcValues))
        gcValues.fill_rule = WindingRule;

    nvgBeginPath(dc.vg);
    nvgMoveTo(dc.vg, points[0].x, points[0].y);

    for (i = 1; i < npoints; i++) {
        if (mode == CoordModeOrigin) {
            nvgLineTo(dc.vg, points[i].x, points[i].y);
        } else {
            nvgLineTo(dc.vg,
                      points[i-1].x + points[i].x,
                      points[i-1].y + points[i].y);
        }
    }
    nvgClosePath(dc.vg);

    if (gcValues.fill_rule == EvenOddRule)
        nvgPathWinding(dc.vg, NVG_HOLE);
    else
        nvgPathWinding(dc.vg, NVG_SOLID);

    nvgFill(dc.vg);

    TkWaylandEndDraw(&dc);
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
    TCL_UNUSED(Display *),
    Drawable     drawable,
    GC           gc,
    int          x, int y,
    unsigned int width,
    unsigned int height)
{
    TkWaylandDrawingContext dc;

    if (width == 0 || height == 0) return BadValue;
    int rc = TkWaylandBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK) {
        return BadDrawable;
    }
    nvgBeginPath(dc.vg);
    nvgRect(dc.vg, x, y, width, height);
    nvgStroke(dc.vg);

    TkWaylandEndDraw(&dc);
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
    TCL_UNUSED(Display *),
    Drawable     drawable,
    GC           gc,
    XRectangle  *rectArr,
    int          nRects)
{
    TkWaylandDrawingContext dc;
    int i;

    int rc = TkWaylandBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

    for (i = 0; i < nRects; i++) {
        nvgBeginPath(dc.vg);
        nvgRect(dc.vg, rectArr[i].x, rectArr[i].y,
                rectArr[i].width, rectArr[i].height);
        nvgStroke(dc.vg);
    }

    TkWaylandEndDraw(&dc);
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
    NVGcolor color;
    int i;

    if (nrectangles < 1) return Success;
    
    /* Get color. */
    if (TkWaylandGetGCValues(gc, GCForeground, &v)) {
        color = TkWaylandPixelToNVG(v.foreground);
    } else {
	printf("Failed to get color from gc - using white.\n");
        color = nvgRGB(255, 255, 255);
    }
    if (TkWaylandBeginDraw(drawable, gc, &dc) != TCL_OK) {
	// X11 would return 0 and generate a BadDrawable error.
        return BadDrawable;
    }
    for (i = 0; i < nrectangles; i++) {
        nvgBeginPath(dc.vg);
        nvgRect(dc.vg, 
                (float)rectangles[i].x, 
                (float)rectangles[i].y,
                (float)rectangles[i].width, 
                (float)rectangles[i].height);
	nvgFillColor(dc.vg, color);
	nvgFill(dc.vg);
    }
    TkWaylandEndDraw(&dc);
    return Success;
}
                
/*
 *----------------------------------------------------------------------
 *
 * XFillRectangle --
 *
 *	Fill a rectangle.  Delegates to XFillRectangles.
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
    Display     *display,
    Drawable     d,
    GC           gc,
    int          x, int y,
    unsigned int width,
    unsigned int height)
{
    XRectangle rect;
    rect.x      = x;
    rect.y      = y;
    rect.width  = width;
    rect.height = height;
    
    /* Ensure width and height are at least 1. */
    if (width == 0) rect.width = 1;
    if (height == 0) rect.height = 1;
    
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
 *	Success on successful completion, BadDrawable or BadValue on failure.
 *
 * Side effects:
 *	Renders an arc outline on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawArc(
    TCL_UNUSED(Display *),
    Drawable     drawable,
    GC           gc,
    int          x, int y,
    unsigned int width,
    unsigned int height,
    int          angle1,
    int          angle2)
{
    TkWaylandDrawingContext dc;
    float cx, cy, rx, ry, startAngle, endAngle;

    if (width == 0 || height == 0 || angle2 == 0) return BadValue;
    int rc = TkWaylandBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

    cx = x + width  / 2.0f;
    cy = y + height / 2.0f;
    rx = width  / 2.0f;
    ry = height / 2.0f;
    startAngle = -radians(angle1 / 64.0);
    endAngle   = -radians((angle1 + angle2) / 64.0);

    nvgBeginPath(dc.vg);
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
    nvgStroke(dc.vg);

    TkWaylandEndDraw(&dc);
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
 *	Success on successful completion, BadDrawable on failure.
 *
 * Side effects:
 *	Renders multiple arc outlines on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawArcs(
    TCL_UNUSED(Display *),
    Drawable drawable,
    GC       gc,
    XArc    *arcArr,
    int      nArcs)
{
    TkWaylandDrawingContext dc;
    int i;

    int rc = TkWaylandBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

    for (i = 0; i < nArcs; i++) {
        float cx, cy, rx, ry, startAngle, endAngle;

        if (arcArr[i].width == 0 || arcArr[i].height == 0 ||
            arcArr[i].angle2 == 0) continue;

        cx = arcArr[i].x + arcArr[i].width  / 2.0f;
        cy = arcArr[i].y + arcArr[i].height / 2.0f;
        rx = arcArr[i].width  / 2.0f;
        ry = arcArr[i].height / 2.0f;
        startAngle = -radians(arcArr[i].angle1 / 64.0);
        endAngle   = -radians((arcArr[i].angle1 + arcArr[i].angle2) / 64.0);

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

    TkWaylandEndDraw(&dc);
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
    TCL_UNUSED(Display *),
    Drawable     drawable,
    GC           gc,
    int          x, int y,
    unsigned int width,
    unsigned int height,
    int          angle1,
    int          angle2)
{
    TkWaylandDrawingContext dc;
    XGCValues gcValues;
    float cx, cy, rx, ry, startAngle, endAngle;

    if (width == 0 || height == 0 || angle2 == 0) return BadValue;
    int rc = TkWaylandBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

    if (TkWaylandGetGCValues(gc, GCArcMode, &gcValues))
        gcValues.arc_mode = ArcPieSlice;

    cx = x + width  / 2.0f;
    cy = y + height / 2.0f;
    rx = width  / 2.0f;
    ry = height / 2.0f;
    startAngle = -radians(angle1 / 64.0);
    endAngle   = -radians((angle1 + angle2) / 64.0);

    nvgBeginPath(dc.vg);
    if (gcValues.arc_mode == ArcPieSlice)
        nvgMoveTo(dc.vg, cx, cy);

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

    if (gcValues.arc_mode == ArcPieSlice)
        nvgLineTo(dc.vg, cx, cy);

    nvgClosePath(dc.vg);
    nvgFill(dc.vg);

    TkWaylandEndDraw(&dc);
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
    TCL_UNUSED(Display *),
    Drawable drawable,
    GC       gc,
    XArc    *arcArr,
    int      nArcs)
{
    TkWaylandDrawingContext dc;
    XGCValues gcValues;
    int i;

    int rc = TkWaylandBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

    if (TkWaylandGetGCValues(gc, GCArcMode, &gcValues))
        gcValues.arc_mode = ArcPieSlice;

    for (i = 0; i < nArcs; i++) {
        float cx, cy, rx, ry, startAngle, endAngle;

        if (arcArr[i].width == 0 || arcArr[i].height == 0 ||
            arcArr[i].angle2 == 0) continue;

        cx = arcArr[i].x + arcArr[i].width  / 2.0f;
        cy = arcArr[i].y + arcArr[i].height / 2.0f;
        rx = arcArr[i].width  / 2.0f;
        ry = arcArr[i].height / 2.0f;
        startAngle = -radians(arcArr[i].angle1 / 64.0);
        endAngle   = -radians((arcArr[i].angle1 + arcArr[i].angle2) / 64.0);

        nvgBeginPath(dc.vg);
        if (gcValues.arc_mode == ArcPieSlice)
            nvgMoveTo(dc.vg, cx, cy);

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

        if (gcValues.arc_mode == ArcPieSlice)
            nvgLineTo(dc.vg, cx, cy);

        nvgClosePath(dc.vg);
        nvgFill(dc.vg);
    }

    TkWaylandEndDraw(&dc);
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
    GC        fgGC,
    GC        bgGC,
    int       highlightWidth,
    Drawable  drawable)
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
    Tk_Window  tkwin,
    Drawable   drawable,
    Tk_3DBorder border,
    int        highlightWidth,
    int        borderWidth,
    int        relief)
{
    Tk_Fill3DRectangle(tkwin, drawable, border,
                       highlightWidth, highlightWidth,
                       Tk_Width(tkwin)  - 2 * highlightWidth,
                       Tk_Height(tkwin) - 2 * highlightWidth,
                       borderWidth, relief);
}

/*
 *----------------------------------------------------------------------
 *
 * TkScrollWindow --
 *
 *	Scrolls a rectangle of a window's content by (dx, dy) so already-
 *	rendered pixels can be reused instead of redrawn.  This is what
 *	makes scrolling in the text and entry widgets smooth instead of
 *	requiring a full repaint on every line of scroll.
 *
 *	This port keeps each toplevel's content in a single NanoVG-managed
 *	framebuffer (see tkWaylandSubwindows.c/tkWaylandInit.c), and its
 *	Region machinery is entirely unimplemented (XCreateRegion always
 *	returns NULL; XUnionRectWithRegion is a no-op) -- there is no X
 *	server generating real Expose events for us either.  The previous
 *	version of this function relied on that ("the exposed region is
 *	handled by a subsequent expose event that Tk will generate"), but
 *	nothing here ever generates one, and no pixels were ever actually
 *	moved: the result is stale content sitting in the scrolled-away
 *	position until an unrelated full redraw happens to paint over it,
 *	which is exactly the artifact this work is meant to fix.
 *
 *	Instead, this function performs the pixel copy itself and directly
 *	queues expose events (TkWaylandQueueExposeEvent) for the strip(s)
 *	vacated by the move, which is how this backend already tells the
 *	generic display code "this needs to be redrawn" elsewhere (see
 *	Tk_ClipDrawableToRect).  damageRgn is still updated defensively so
 *	that a future real Region implementation continues to work without
 *	changes here.
 *
 * Results:
 *	Returns true if any pixels were left needing a redraw (dx or dy
 *	nonzero and the window has a backing framebuffer), false otherwise.
 *
 * Side effects:
 *	Copies pixels within the window's backing-store framebuffer and
 *	queues expose events for the newly uncovered rectangle(s).
 *
 *----------------------------------------------------------------------
 */

bool
TkScrollWindow(
    Tk_Window tkwin,
    TCL_UNUSED(GC),          /* Not needed -- this is a raw pixel copy, not
                              * a GC-styled drawing operation. */
    int x, int y, int width, int height,
    int dx, int dy,
    TkRegion damageRgn)
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    Drawable drawable = TkWaylandDrawableForTkWindow(winPtr);
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindowFromDrawable(drawable);

    if (glfwWindow == NULL || (dx == 0 && dy == 0)) {
        return false;
    }

    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(glfwWindow);
    if (infoPtr == NULL || infoPtr->winPtr == NULL) {
        return false;
    }
    NVGLUframebuffer *fb = infoPtr->winPtr->privatePtr->fb;
    if (fb == NULL) {
        return false;
    }

    float scale;
    glfwGetWindowContentScale(glfwWindow, &scale, NULL);
    NVGcontext *vg = infoPtr->vg;

    int srcX = (int) (x * scale);
    int srcY = (int) (y * scale);
    int w    = (int) (width * scale);
    int h    = (int) (height * scale);
    int dstX = srcX + (int) (dx * scale);
    int dstY = srcY + (int) (dy * scale);

    int fbWidth, fbHeight;
    glfwGetFramebufferSize(glfwWindow, &fbWidth, &fbHeight);
    /* Framebuffer y is bottom-left-origin; Tk's x/y are top-left-origin. */
    int glSrcY = fbHeight - srcY - h;
    int glDstY = fbHeight - dstY - h;

    /*
     * glBlitFramebuffer's behavior is undefined when the source and
     * destination are the same framebuffer with overlapping rectangles
     * -- which scrolling by less than the viewport size guarantees here
     * -- so stage the copy through a temporary FBO rather than blitting
     * fb->fbo onto itself directly.
     */
    glfwMakeContextCurrent(glfwWindow);
    NVGLUframebuffer *staging = nvgluCreateFramebuffer(vg, w, h, 0);
    if (staging == NULL) {
        return false;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, staging->fbo);
    glBlitFramebuffer(srcX, glSrcY, srcX + w, glSrcY + h,
                       0, 0, w, h,
                       GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, staging->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb->fbo);
    glBlitFramebuffer(0, 0, w, h,
                       dstX, glDstY, dstX + w, glDstY + h,
                       GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    nvgluDeleteFramebuffer(staging);

    /*
     * Queue expose events for the strip(s) uncovered by the move.
     *
     * The blit above works in scaled framebuffer pixels while these
     * rectangles are in logical (unscaled) coordinates; at a fractional
     * content scale (125%, 150%, ...) independently rounding each edge
     * of the blit's source/dest rectangles can leave the seam between
     * "already correctly blitted" and "freshly exposed" off by a
     * fractional logical pixel. Pad each exposed strip by 1 logical
     * pixel back into the blitted region so that seam is always
     * repainted with the new content rather than left as a stale line.
     */
    if (dx > 0) {
        TkWaylandQueueExposeEvent(winPtr, x, y, dx + 1, height);
    } else if (dx < 0) {
        int ex = x + width + dx - 1;
        TkWaylandQueueExposeEvent(winPtr, ex, y, -dx + 1, height);
    }
    if (dy > 0) {
        TkWaylandQueueExposeEvent(winPtr, x, y, width, dy + 1);
    } else if (dy < 0) {
        int ey = y + height + dy - 1;
        TkWaylandQueueExposeEvent(winPtr, x, ey, width, -dy + 1);
    }

    /* Best-effort region bookkeeping for forward compatibility. */
    if (damageRgn != NULL) {
        XRectangle rect;
        if (dx != 0) {
            rect.x = (dx > 0) ? x : x + width + dx;
            rect.y = y;
            rect.width = (dx > 0) ? dx : -dx;
            rect.height = height;
            XUnionRectWithRegion(&rect, (Region) damageRgn, (Region) damageRgn);
        }
        if (dy != 0) {
            rect.x = x;
            rect.y = (dy > 0) ? y : y + height + dy;
            rect.width = width;
            rect.height = (dy > 0) ? dy : -dy;
            XUnionRectWithRegion(&rect, (Region) damageRgn, (Region) damageRgn);
        }
    }

    infoPtr->flags |= TKWL_NEEDS_DISPLAY;
    return true;
}



/*
 *----------------------------------------------------------------------
 *
 * XSetClipMask --
 *
 *	Set clip mask in GC.  If pixmap is None, any existing clip is
 *	cleared.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	Updates the GC's clip state; if a valid pixmap is provided, sets
 *	a rectangular clip region matching the pixmap's extent, offset by
 *	the current clip origin. If pixmap is None, clears any existing clip.
 *
 *----------------------------------------------------------------------
 */

int
XSetClipMask(
    TCL_UNUSED(Display *),
    GC gc,
    Pixmap pixmap)
{
    if (gc == NULL) {
	return 0;
    }
    TkWaylandGC *waylandGC = (TkWaylandGC *) gc;
    if (pixmap == None) {
	/* None clears any clip previously set on this GC. */
	waylandGC->hasClip = 0;
	waylandGC->numClipRects = 0;
	return 0;
    }
    /*
     * A real 1-bit bitmap clip mask can't be represented by NanoVG's
     * rectangular scissor clip.  Rather than silently drawing unclipped
     * (which is what happened before and is a common source of the
     * canvas artifacts this work is meant to fix), fall back to clipping
     * to the mask pixmap's bounding box: still rectangular, but at least
     * bounds the damage to the mask's extent instead of ignoring it.
     */
    TkWaylandPixmap *maskPtr = TkWaylandPixmapFromPixmap(pixmap);
    waylandGC->numClipRects = 0;
    if (maskPtr != NULL) {
	waylandGC->clipRects[0].x = (short) waylandGC->clipXOrigin;
	waylandGC->clipRects[0].y = (short) waylandGC->clipYOrigin;
	waylandGC->clipRects[0].width = (unsigned short) maskPtr->width;
	waylandGC->clipRects[0].height = (unsigned short) maskPtr->height;
	waylandGC->numClipRects = 1;
	waylandGC->hasClip = 1;
    } else {
	waylandGC->hasClip = 0;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetClipOrigin --
 *
 *	Set the clip origin in a GC. The origin offsets whatever rectangles
 *	were (or will be) installed by XSetClipRectangles, matching X11
 *	semantics where the rectangles are specified relative to the clip
 *	origin rather than in absolute drawable coordinates.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	Updates clip_x_origin/clip_y_origin in the GC and shifts any
 *	previously-set clip rectangles to match the new origin.
 *
 *----------------------------------------------------------------------
 */

int
XSetClipOrigin(
    TCL_UNUSED(Display *),
    GC gc,
    int clip_x_origin,
    int clip_y_origin)
{
    if (gc == NULL) {
	return 0;
    }
    TkWaylandGC *waylandGC = (TkWaylandGC *) gc;
    int dx = clip_x_origin - waylandGC->clipXOrigin;
    int dy = clip_y_origin - waylandGC->clipYOrigin;
    if (dx != 0 || dy != 0) {
	for (int i = 0; i < waylandGC->numClipRects; i++) {
	    waylandGC->clipRects[i].x += dx;
	    waylandGC->clipRects[i].y += dy;
	}
    }
    waylandGC->clipXOrigin = clip_x_origin;
    waylandGC->clipYOrigin = clip_y_origin;
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetClipRectangles --
 *
 *	Set clip rectangles in GC. Stores the rectangles in the GC's
 *	internal clipRects array, offset by the specified clip origin.
 *	These rectangles are later used during drawing operations to
 *	restrict rendering to their union.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	Updates the GC's clip state with the provided rectangles, offset
 *	by the clip origin. If n is 0 or rectangles is NULL, clears any
 *	existing clip. Limited to TKWL_MAX_CLIP_RECTS rectangles.
 *
 *----------------------------------------------------------------------
 */
 

int
XSetClipRectangles(
    TCL_UNUSED(Display *),
    GC gc,
    int clip_x_origin,
    int clip_y_origin,
    XRectangle *rectangles,
    int n,
    TCL_UNUSED(int)) /* ordering: irrelevant, we don't depend on order. */
{
    if (gc == NULL) {
	return 0;
    }
    TkWaylandGC *waylandGC = (TkWaylandGC *) gc;
    waylandGC->clipXOrigin = clip_x_origin;
    waylandGC->clipYOrigin = clip_y_origin;
    waylandGC->numClipRects = 0;
    if (n <= 0 || rectangles == NULL) {
	waylandGC->hasClip = 0;
	return 0;
    }
    if (n > TKWL_MAX_CLIP_RECTS) {
	n = TKWL_MAX_CLIP_RECTS;
    }
    for (int i = 0; i < n; i++) {
	waylandGC->clipRects[i].x = rectangles[i].x + (short) clip_x_origin;
	waylandGC->clipRects[i].y = rectangles[i].y + (short) clip_y_origin;
	waylandGC->clipRects[i].width = rectangles[i].width;
	waylandGC->clipRects[i].height = rectangles[i].height;
    }
    waylandGC->numClipRects = n;
    waylandGC->hasClip = 1;
    return 0;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
