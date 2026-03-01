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
 *
 */

#include "tkInt.h"
#include "tkPort.h"
#include "tkGlfwInt.h"
#include <GLES2/gl2.h>
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
 *	Falls back to the "sans" font registered during TkGlfwInitialize.
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
        if (TkWaylandGetGCValues(gc, GCFont, &v) == 0 && v.font != None) {
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

    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

    buf = (char *)ckalloc(length + 1);
    memcpy(buf, string, length);
    buf[length] = '\0';

    GetNVGFont(dc.vg, gc, &fontId, &fontSize);
    nvgFontFaceId(dc.vg, fontId);
    nvgFontSize(dc.vg, fontSize);
    nvgTextAlign(dc.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

    /* Foreground color is already set by TkGlfwBeginDraw via TkGlfwApplyGC */
    nvgText(dc.vg, (float)x, (float)y, buf, NULL);

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

    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
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
        if (TkWaylandGetGCValues(gc, GCBackground, &v) == 0) {
            NVGcolor bg = TkGlfwPixelToNVG(v.background);
            nvgBeginPath(dc.vg);
            nvgRect(dc.vg, bounds[0], bounds[1],
                    bounds[2] - bounds[0], bounds[3] - bounds[1]);
            nvgFillColor(dc.vg, bg);
            nvgFill(dc.vg);
        }
    }

    /* Draw text in foreground colour (restored by ApplyGC). */
    TkGlfwApplyGC(dc.vg, gc);
    nvgFontFaceId(dc.vg, fontId);
    nvgFontSize(dc.vg, fontSize);
    nvgTextAlign(dc.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgText(dc.vg, (float)x, (float)y, buf, NULL);

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
    TCL_UNUSED(Display *),
    Drawable drawable,
    GC       gc,
    int      x,
    int      y)
{
    TkWaylandDrawingContext dc;
    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;
    nvgBeginPath(dc.vg);
    nvgRect(dc.vg, (float)x, (float)y, 1.0f, 1.0f);
    nvgFill(dc.vg);
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
    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
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
    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
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
    TCL_UNUSED(Display *),
    Drawable   drawable,
    GC         gc,
    XSegment  *segments,
    int        nsegments)
{
    TkWaylandDrawingContext dc;
    int i;

    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

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
    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

    if (TkWaylandGetGCValues(gc, GCFillRule, &gcValues) == 0)
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
    TCL_UNUSED(Display *),
    Drawable     drawable,
    GC           gc,
    int          x, int y,
    unsigned int width,
    unsigned int height)
{
    TkWaylandDrawingContext dc;

    if (width == 0 || height == 0) return BadValue;
    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

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

    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

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
 *	Success on successful completion, BadDrawable on failure.
 *
 * Side effects:
 *	Renders multiple filled rectangles on the drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillRectangles(
    TCL_UNUSED(Display *),
    Drawable    drawable,
    GC          gc,
    XRectangle *rectangles,
    int         n_rectangles)
{
    TkWaylandDrawingContext dc;
    int i;

    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

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
    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
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

    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
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
    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

    if (TkWaylandGetGCValues(gc, GCArcMode, &gcValues) == 0)
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
    TCL_UNUSED(Display *),
    Drawable drawable,
    GC       gc,
    XArc    *arcArr,
    int      nArcs)
{
    TkWaylandDrawingContext dc;
    XGCValues gcValues;
    int i;

    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK)
        return BadDrawable;

    if (TkWaylandGetGCValues(gc, GCArcMode, &gcValues) == 0)
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
    TCL_UNUSED(Tk_Window),
    TCL_UNUSED(GC),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(TkRegion))
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
