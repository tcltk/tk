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

/* X11 region headers for BoxPtr and Region types. */
#include <X11/Xutil.h>

#define radians(d)	((d) * (M_PI/180.0))

extern void InitializeXKBKeymap(TkDisplay *dispPtr);
extern void CleanupXKBKeymap(TkDisplay *dispPtr);

/*
 *----------------------------------------------------------------------
 *
 * XDrawLines --
 *
 *	Draw connected line segments.
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
    Display *display,
    Drawable d,
    GC gc,
    XPoint *points,
    int npoints,
    int mode)
{
    TkWaylandDrawingContext dc;
    int i;
    
    if (npoints < 2) {
        return BadValue;
    }
    
    LastKnownRequestProcessed(display)++;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
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
    Display *display,
    Drawable d,
    GC gc,
    XSegment *segments,
    int nsegments)
{
    TkWaylandDrawingContext dc;
    int i;
    
    LastKnownRequestProcessed(display)++;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
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
 *	Success.
 *
 * Side effects:
 *	Draws a filled polygon on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillPolygon(
    Display *display,
    Drawable d,
    GC gc,
    XPoint *points,
    int npoints,
    int shape,
    int mode)
{
    TkWaylandDrawingContext dc;
    XGCValues gcValues;
    int i;
    
    if (npoints < 3) {
        return BadValue;
    }
    
    LastKnownRequestProcessed(display)++;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
    /* Get GC values for fill rule. */
    XGetGCValues(NULL, gc, GCFillRule, &gcValues);
    
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
    
    (void)shape;  /* Suppress unused warning. */
    
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
    Display *display,
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
    
    LastKnownRequestProcessed(display)++;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
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
    Display *display,
    Drawable d,
    GC gc,
    XRectangle *rectArr,
    int nRects)
{
    TkWaylandDrawingContext dc;
    int i;
    
    LastKnownRequestProcessed(display)++;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
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
    Display *display,
    Drawable d,
    GC gc,
    XRectangle *rectangles,
    int n_rectangles)
{
    TkWaylandDrawingContext dc;
    int i;
    
    LastKnownRequestProcessed(display)++;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
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
 * XDrawArc --
 *
 *	Draw an arc.
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
    Display *display,
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
    
    LastKnownRequestProcessed(display)++;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
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
    Display *display,
    Drawable d,
    GC gc,
    XArc *arcArr,
    int nArcs)
{
    TkWaylandDrawingContext dc;
    int i;
    
    LastKnownRequestProcessed(display)++;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
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
    Display *display,
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
        return BadDrawable;
    }
    
    LastKnownRequestProcessed(display)++;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
    /* Get GC values for arc mode. */
    XGetGCValues(NULL, gc, GCArcMode, &gcValues);
    
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
    Display *display,
    Drawable d,
    GC gc,
    XArc *arcArr,
    int nArcs)
{
    TkWaylandDrawingContext dc;
    XGCValues gcValues;
    int i;
    
    LastKnownRequestProcessed(display)++;
    
    if (TkGlfwBeginDraw(d, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
    /* Get GC values for arc mode */
    XGetGCValues(NULL, gc, GCArcMode, &gcValues);
    
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
 * --------------------------------------------------------------------------------
 *
 * TkpOpenDisplay -
 * 
 *     Allocates a new TkDisplay, opens the display, and returns
 *     a pointer to a display.
 * 
 * Results:
 *     A pointer to a TkDisplay structure, or NULL if the display
 *     could not be opened.
 * 
 * Side effects:
 *     Allocates memory for the TkDisplay structure and initializes
 *     GLFW and Wayland subsystems.
 *
 * --------------------------------------------------------------------------------

 */

TkDisplay *
TkpOpenDisplay(
	       TCL_UNUSED(const char *))	/* Display name (ignored on Wayland). */
{
    TkDisplay *dispPtr;
    Display *display;


    /*
     * Under GLFW/Wayland, we don't use traditional X11 display names.
     * GLFW handles display connection internally. We just need to
     * initialize GLFW if not already done.
     */

    if (!glfwInit()) {
	return NULL;
    }

    /*
     * GLFW must be told to use Wayland. This is platform-specific
     * initialization that should happen before any window creation.
     */
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    /*
     * Allocate the TkDisplay structure. This structure bridges Tk's
     * X11-style display management with our GLFW/Wayland implementation.
     */
    dispPtr = (TkDisplay *)ckalloc(sizeof(TkDisplay));
    memset(dispPtr, 0, sizeof(TkDisplay));

    /*
     * Create a minimal X11-compatible Display structure. While we're
     * on Wayland, Tk's core still expects certain X11-style structures.
     */
    display = (Display *)ckalloc(sizeof(TkDisplay));
    memset(display, 0, sizeof(TkDisplay));
    dispPtr->display = NULL; /* Do not assign a "dummy" diplay. */

    /*
     * Set up basic display properties.
     */
    dispPtr->name = (char *)ckalloc(strlen("wayland-0") + 1);
    strcpy(dispPtr->name, "wayland-0");

    /*
     * Initialize keyboard mapping using XKB (X Keyboard Extension).
     * XKB is also used on Wayland for keyboard handling.
     */
    InitializeXKBKeymap(dispPtr);

    return dispPtr;
}

/*
 * --------------------------------------------------------------------------------

 *
 * TkpCloseDisplay -
 * 
 *     Deallocates a TkDisplay structure and closes the display.
 * 
 * Results:
 *     None.
 * 
 * Side effects:
 *     Frees memory and performs cleanup of GLFW/Wayland resources.
 *
 **********************************************************************
 */

void
TkpCloseDisplay(
		TkDisplay *dispPtr)
{
    if (dispPtr == NULL) {
	return;
    }


    /*
     * Clean up keyboard mapping resources.
     */
    CleanupXKBKeymap(dispPtr);

    /*
     * Free the display name string.
     */
    if (dispPtr->name) {
	ckfree(dispPtr->name);
	dispPtr->name = NULL;
    }

    /*
     * Free the X11-compatible Display structure.
     */
    if (dispPtr->display) {
	ckfree((char *)dispPtr->display);
	dispPtr->display = NULL;
    }

    /*
     * Note: We don't call glfwTerminate() here because other Tk
     * displays might still be active. GLFW cleanup happens when
     * the application exits.
     */

    /*
     * Free the TkDisplay structure itself.
     */
    ckfree((char *)dispPtr);


}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
