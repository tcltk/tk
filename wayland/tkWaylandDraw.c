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
 * Minimal region representation for this Wayland port.
 *
 * `Region` is `struct _XRegion *`, but the public/dev X11 headers
 * included above only ever forward-declare that struct -- the real
 * body is private to libX11 and isn't installed anywhere this file can
 * see it. Since nothing outside this port's own region functions below
 * ever dereferences a Region, it's safe for this file to be the one
 * place that gives struct _XRegion a real body.
 *
 * ttk's only consumer of this (ttkLabel.c's TextDraw, for clipping
 * treeview/label cell text that overflows its cell) does XCreateRegion(),
 * exactly one XUnionRectWithRegion() with a single rect, then reads the
 * result back out via TkUnixSetXftClipRegion()/XClipBox(). A single
 * bounding-box rectangle is therefore all this needs to track -- no
 * general multi-rect region support required.
 */
struct _XRegion {
    XRectangle extents;
    int        valid;   /* 0 = empty region, 1 = extents holds a rect */
};

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

    /* Foreground color is already set by TkWaylandBeginDraw via TkWaylandApplyGC. */
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
 *	background color first ("opaque" text).  Used for selected
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

    /* Fill background with GC background color. */
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
 * XClipBox --
 *
 *	Get bounding box of region.
 *
 * Results:
 *	1, with rect_return set to the region's bounding box (zeroed if
 *	the region is NULL or empty). Real Xlib returns a rect-vs-region
 *	shape flag here (Rectangle/Complex); since this port never tracks
 *	more than a bounding box to begin with, that distinction doesn't
 *	apply -- callers here (TkUnixSetXftClipRegion) only care about the
 *	rect.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XClipBox(
    Region region,
    XRectangle *rect_return)
{
    if (rect_return == NULL) {
        return 0;
    }
    if (region != NULL && region->valid) {
        *rect_return = region->extents;
    } else {
        rect_return->x = rect_return->y = 0;
        rect_return->width = rect_return->height = 0;
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * XDestroyRegion --
 *
 *	Destroy a region allocated by XCreateRegion().
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	Frees region.
 *
 *----------------------------------------------------------------------
 */

int
XDestroyRegion(
    Region region)
{
    if (region != NULL) {
        Tcl_Free((char *)region);
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateRegion --
 *
 *	Create an empty region. See the struct _XRegion comment near the
 *	top of this file for why this port can define the struct body
 *	and allocate a real one here instead of returning NULL.
 *
 * Results:
 *	A newly allocated, empty Region. Caller must XDestroyRegion() it.
 *
 * Side effects:
 *	Allocates memory.
 *
 *----------------------------------------------------------------------
 */

Region
XCreateRegion(
    void)
{
    Region region = (Region)Tcl_Alloc(sizeof(struct _XRegion));

    region->extents.x = region->extents.y = 0;
    region->extents.width = region->extents.height = 0;
    region->valid = 0;
    return region;
}

/*
 *----------------------------------------------------------------------
 *
 * XUnionRectWithRegion --
 *
 *	Union a rectangle with a region, tracking only the resulting
 *	bounding box (see struct _XRegion comment near the top of this
 *	file). Handles srcRegion == dstRegion, which is how ttk's
 *	TextDraw() (ttkLabel.c) calls this -- union a single rect into a
 *	freshly created, still-empty region.
 *
 * Results:
 *	Always returns 1 (Success).
 *
 * Side effects:
 *	Updates dstRegion's extents/valid fields.
 *
 *----------------------------------------------------------------------
 */

int
XUnionRectWithRegion(
    XRectangle *rect,
    Region srcRegion,
    Region dstRegion)
{
    XRectangle r;
    int x0, y0, x1, y1;

    if (rect == NULL || dstRegion == NULL) {
        return 0;
    }
    r = *rect;

    if (srcRegion != NULL && srcRegion != dstRegion && srcRegion->valid) {
        x0 = (r.x < srcRegion->extents.x) ? r.x : srcRegion->extents.x;
        y0 = (r.y < srcRegion->extents.y) ? r.y : srcRegion->extents.y;
        x1 = (r.x + r.width > srcRegion->extents.x + srcRegion->extents.width)
                ? r.x + r.width
                : srcRegion->extents.x + srcRegion->extents.width;
        y1 = (r.y + r.height > srcRegion->extents.y + srcRegion->extents.height)
                ? r.y + r.height
                : srcRegion->extents.y + srcRegion->extents.height;
        r.x = x0; r.y = y0;
        r.width = x1 - x0; r.height = y1 - y0;
    }

    if (!dstRegion->valid) {
        dstRegion->extents = r;
    } else {
        x0 = (r.x < dstRegion->extents.x) ? r.x : dstRegion->extents.x;
        y0 = (r.y < dstRegion->extents.y) ? r.y : dstRegion->extents.y;
        x1 = (r.x + r.width > dstRegion->extents.x + dstRegion->extents.width)
                ? r.x + r.width
                : dstRegion->extents.x + dstRegion->extents.width;
        y1 = (r.y + r.height > dstRegion->extents.y + dstRegion->extents.height)
                ? r.y + r.height
                : dstRegion->extents.y + dstRegion->extents.height;
        dstRegion->extents.x = x0;
        dstRegion->extents.y = y0;
        dstRegion->extents.width = x1 - x0;
        dstRegion->extents.height = y1 - y0;
    }
    dstRegion->valid = 1;
    return 1;
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
 *	Scroll a rectangular area of a window by copying the pixel
 *	content within the drawable's toplevel backing FBO, then
 *	reporting the newly-revealed strip as the damage region so Tk's
 *	generic display code redraws only that area.
 *
 * Results:
 *	Returns 1 (True) if the scroll was performed by copying pixels
 *	(so the caller should only redraw the region added to
 *	damageRgn); returns 0 (False) if the copy could not be done, so
 *	the caller must redraw the entire scrolled rectangle itself.
 *
 * Side effects:
 *	Copies pixel content within the drawable's backing store and
 *	fills in damageRgn with the region that still needs a fresh
 *	redraw.
 *
 *----------------------------------------------------------------------
 */

bool
TkScrollWindow(
    Tk_Window tkwin,
    TCL_UNUSED(GC),
    int       x, int y,
    int       width, int height,
    int       dx, int dy,
    TkRegion  damageRgn)
{
    TkWindow  *childPtr = (TkWindow *)tkwin;
    TkWindow  *winPtr   = childPtr;
    float      offX = 0, offY = 0;
    XRectangle srcRect, dstRect;
    TkRegion   coveredRgn;
    GLFWwindow *glfwWindow;
    NVGLUframebuffer *fb;
    GLuint scratchFbo = 0, scratchTex = 0;
    int fbX0, fbY0, fbX1, fbY1, fbW, fbH, fbDx, fbDy;
    int winFbWidth, winFbHeight;
    int srcGLY0, srcGLY1, dstGLY0, dstGLY1;
    float scale;

    if (width <= 0 || height <= 0 || (dx == 0 && dy == 0)) {
        return 0;
    }

    /* 
     * Same walk TkWaylandBeginDraw does, to find the toplevel and this
     * widget's offset within it. 
     */
    while (!Tk_IsTopLevel(winPtr)) {
        offX += winPtr->changes.x;
        offY += winPtr->changes.y;
        winPtr = winPtr->parentPtr;
    }

    if (!winPtr->privatePtr || !winPtr->privatePtr->fb) {
        return 0;               /* no backing store yet -- let caller redraw */
    }

    glfwWindow = winPtr->privatePtr->glfwWindow;
    fb         = winPtr->privatePtr->fb;

    glfwMakeContextCurrent(glfwWindow);
    glfwGetWindowContentScale(glfwWindow, &scale, NULL);
    glfwGetFramebufferSize(glfwWindow, &winFbWidth, &winFbHeight);

    /* 
     * Convert widget-local logical coords to backing-store pixels,
     * computing each edge independently so rounding can't open a
     * seam between the blit rect and its own width/height. 
     */
    fbX0 = (int)lroundf((offX + x) * scale);
    fbY0 = (int)lroundf((offY + y) * scale);
    fbX1 = (int)lroundf((offX + x + width)  * scale);
    fbY1 = (int)lroundf((offY + y + height) * scale);
    fbW  = fbX1 - fbX0;
    fbH  = fbY1 - fbY0;
    fbDx = (int)lroundf((offX + x + dx) * scale) - fbX0;
    fbDy = (int)lroundf((offY + y + dy) * scale) - fbY0;

    /*
     * Move the pixels within the toplevel's backing FBO. A direct
     * FBO->FBO blit with overlapping src/dst rects is undefined by the
     * GL spec (and for a scroll they normally do overlap), so
     * round-trip through a same-size scratch texture -- same
     * read-FBO/draw-FBO blit renderFBO() already uses, just targeting
     * an offscreen FBO instead of the default framebuffer.
     */
    glGenTextures(1, &scratchTex);
    glBindTexture(GL_TEXTURE_2D, scratchTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fbW, fbH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &scratchFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, scratchFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, scratchTex, 0);

    /*
     * NanoVG's own draw calls flip Tk's top-down (y=0 at top)
     * coordinates into GL's native bottom-up framebuffer space
     * internally, via the projection matrix set up in nvgBeginFrame --
     * see the same flip called out for the clip-rect vertex shader in
     * tkWaylandSubwindows.c's addClipRect(). glBlitFramebuffer is raw
     * GL and gets none of that, so do it by hand for the two touches
     * to the real backing store (the scratch texture has no such
     * convention to preserve, so it's left alone).
     */
    srcGLY0 = winFbHeight - fbY1;
    srcGLY1 = winFbHeight - fbY0;
    dstGLY0 = winFbHeight - (fbY1 + fbDy);
    dstGLY1 = winFbHeight - (fbY0 + fbDy);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scratchFbo);
    glBlitFramebuffer(fbX0, srcGLY0, fbX1, srcGLY1,
                       0, 0, fbW, fbH,
                       GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, scratchFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb->fbo);
    glBlitFramebuffer(0, 0, fbW, fbH,
                       fbX0 + fbDx, dstGLY0, fbX1 + fbDx, dstGLY1,
                       GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &scratchFbo);
    glDeleteTextures(1, &scratchTex);

    /*
     * This changes the backing store outside the normal
     * BeginDraw/EndDraw display-proc path, so nothing will otherwise
     * mark the toplevel dirty -- do that ourselves or the scrolled
     * content won't reach the screen until something else happens to
     * trigger a present.
     */
    {
        glfwTkInfo *infoPtr = glfwGetWindowUserPointer(glfwWindow);
        if (infoPtr) {
            infoPtr->flags |= TKWL_NEEDS_DISPLAY;
        }
    }

    /*
     * Report the newly-revealed strip -- source rect minus what the
     * shifted copy now covers -- in the same widget-local, unscaled
     * coordinates the caller passed in.
     */
    srcRect.x = (short)x;
    srcRect.y = (short)y;
    srcRect.width  = (unsigned short)width;
    srcRect.height = (unsigned short)height;

    dstRect = srcRect;
    dstRect.x = (short)(x + dx);
    dstRect.y = (short)(y + dy);

    coveredRgn = TkCreateRegion();
    TkUnionRectWithRegion(&dstRect, coveredRgn, coveredRgn);
    TkUnionRectWithRegion(&srcRect, damageRgn, damageRgn);
    TkSubtractRegion(damageRgn, coveredRgn, damageRgn);
    TkDestroyRegion(coveredRgn);

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
