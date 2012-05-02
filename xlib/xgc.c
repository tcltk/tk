/* 
 * xgc.c --
 *
 *	This file contains generic routines for manipulating X graphics
 *	contexts. 
 *
 * Copyright (c) 1995-1996 Sun Microsystems, Inc.
 * Copyright (c) 2002-2007 Daniel A. Steffen <das@users.sourceforge.net>
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include <tkInt.h>

#if !defined(MAC_TCL) && !defined(MAC_OSX_TK)
#	include <X11/Xlib.h>
#endif
#ifdef MAC_TCL
#	include <Xlib.h>
#	include <X.h>
#	define Cursor XCursor
#	define Region XRegion
#endif
#ifdef MAC_OSX_TK
#	include <tkMacOSXInt.h>
#	include <X11/Xlib.h>
#	include <X11/X.h>
#	define Cursor XCursor
#	define Region XRegion
#endif


/*
 *----------------------------------------------------------------------
 *
 * AllocClipMask --
 *
 *	Static helper proc to allocate new or clear existing TkpClipMask.
 *
 * Results:
 *	Returns ptr to the new/cleared TkpClipMask.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkpClipMask *AllocClipMask(GC gc) {
    TkpClipMask *clip_mask = (TkpClipMask*) gc->clip_mask;
    
    if (clip_mask == None) {
	clip_mask = (TkpClipMask*) ckalloc(sizeof(TkpClipMask));
	gc->clip_mask = (Pixmap) clip_mask;
#ifdef MAC_OSX_TK
    } else if (clip_mask->type == TKP_CLIP_REGION) {
	TkpReleaseRegion(clip_mask->value.region);
#endif
    }
    return clip_mask;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeClipMask --
 *
 *	Static helper proc to free TkpClipMask.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void FreeClipMask(GC gc) {
    if (gc->clip_mask != None) {
#ifdef MAC_OSX_TK
	if (((TkpClipMask*) gc->clip_mask)->type == TKP_CLIP_REGION) {
	    TkpReleaseRegion(((TkpClipMask*) gc->clip_mask)->value.region);
	}
#endif
	ckfree((char*) gc->clip_mask);
	gc->clip_mask = None;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateGC --
 *
 *	Allocate a new GC, and initialize the specified fields.
 *
 * Results:
 *	Returns a newly allocated GC. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

GC
XCreateGC(display, d, mask, values)
    Display* display;
    Drawable d;
    unsigned long mask;
    XGCValues* values;
{
    GC gp;

/*
 * In order to have room for a dash list, MAX_DASH_LIST_SIZE extra chars are
 * defined, which is invisible from the outside. The list is assumed to end
 * with a 0-char, so this must be set explicitely during initialization.
 */

#define MAX_DASH_LIST_SIZE 10

    gp = (XGCValues *)ckalloc(sizeof(XGCValues) + MAX_DASH_LIST_SIZE);
    if (!gp) {
	return None;
    }

    gp->function = 	(mask & GCFunction) 	?values->function	:GXcopy;
    gp->plane_mask = 	(mask & GCPlaneMask) 	?values->plane_mask 	:~0;
    gp->foreground = 	(mask & GCForeground) 	?values->foreground 	:
	    BlackPixelOfScreen(DefaultScreenOfDisplay(display));
    gp->background = 	(mask & GCBackground) 	?values->background 	:
	    WhitePixelOfScreen(DefaultScreenOfDisplay(display));
    gp->line_width = 	(mask & GCLineWidth)	?values->line_width	:1;	
    gp->line_style = 	(mask & GCLineStyle)	?values->line_style	:LineSolid;
    gp->cap_style =  	(mask & GCCapStyle)	?values->cap_style	:0;
    gp->join_style = 	(mask & GCJoinStyle)	?values->join_style	:0;
    gp->fill_style =  	(mask & GCFillStyle)	?values->fill_style	:FillSolid;
    gp->fill_rule =  	(mask & GCFillRule)	?values->fill_rule	:WindingRule;
    gp->arc_mode = 	(mask & GCArcMode)	?values->arc_mode	:ArcPieSlice;
    gp->tile = 		(mask & GCTile)		?values->tile		:None;
    gp->stipple = 	(mask & GCStipple)	?values->stipple	:None;
    gp->ts_x_origin = 	(mask & GCTileStipXOrigin)	?values->ts_x_origin:0;
    gp->ts_y_origin = 	(mask & GCTileStipYOrigin)	?values->ts_y_origin:0;
    gp->font = 		(mask & GCFont)		?values->font		:None;
    gp->subwindow_mode = (mask & GCSubwindowMode)?values->subwindow_mode:ClipByChildren;
    gp->graphics_exposures = (mask & GCGraphicsExposures)?values->graphics_exposures:True;
    gp->clip_x_origin = (mask & GCClipXOrigin)	?values->clip_x_origin	:0;
    gp->clip_y_origin = (mask & GCClipYOrigin)	?values->clip_y_origin	:0;
    gp->dash_offset = 	(mask & GCDashOffset)	?values->dash_offset	:0;
    gp->dashes = 	(mask & GCDashList)	?values->dashes		:4;
    (&(gp->dashes))[1] = 	0;

    gp->clip_mask = None;
    if (mask & GCClipMask) {
	TkpClipMask *clip_mask = AllocClipMask(gp);
	
	clip_mask->type = TKP_CLIP_PIXMAP;
	clip_mask->value.pixmap = values->clip_mask;
    }

    return gp;
}

/*
 *----------------------------------------------------------------------
 *
 * XChangeGC --
 *
 *	Changes the GC components specified by valuemask for the
 *	specified GC.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the specified GC.
 *
 *----------------------------------------------------------------------
 */

int
XChangeGC(d, gc, mask, values)
    Display * d;
    GC gc;
    unsigned long mask;
    XGCValues *values;
{
    if (mask & GCFunction) { gc->function = values->function; }
    if (mask & GCPlaneMask) { gc->plane_mask = values->plane_mask; }
    if (mask & GCForeground) { gc->foreground = values->foreground; }
    if (mask & GCBackground) { gc->background = values->background; }
    if (mask & GCLineWidth) { gc->line_width = values->line_width; }	
    if (mask & GCLineStyle) { gc->line_style = values->line_style; }
    if (mask & GCCapStyle) { gc->cap_style = values->cap_style; }
    if (mask & GCJoinStyle) { gc->join_style = values->join_style; }
    if (mask & GCFillStyle) { gc->fill_style = values->fill_style; }
    if (mask & GCFillRule) { gc->fill_rule = values->fill_rule; }
    if (mask & GCArcMode) { gc->arc_mode = values->arc_mode; }
    if (mask & GCTile) { gc->tile = values->tile; }
    if (mask & GCStipple) { gc->stipple = values->stipple; }
    if (mask & GCTileStipXOrigin) { gc->ts_x_origin = values->ts_x_origin; }
    if (mask & GCTileStipYOrigin) { gc->ts_y_origin = values->ts_y_origin; }
    if (mask & GCFont) { gc->font = values->font; }
    if (mask & GCSubwindowMode) { gc->subwindow_mode = values->subwindow_mode; }
    if (mask & GCGraphicsExposures) { gc->graphics_exposures = values->graphics_exposures; }
    if (mask & GCClipXOrigin) { gc->clip_x_origin = values->clip_x_origin; }
    if (mask & GCClipYOrigin) { gc->clip_y_origin = values->clip_y_origin; }
    if (mask & GCClipMask) { XSetClipMask(d, gc, values->clip_mask); }
    if (mask & GCDashOffset) { gc->dash_offset = values->dash_offset; }
    if (mask & GCDashList) { gc->dashes = values->dashes; (&(gc->dashes))[1] = 0;}
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XFreeGC --
 *
 *	Deallocates the specified graphics context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int XFreeGC(d, gc)
    Display * d;
    GC gc;
{
    if (gc != None) {
	FreeClipMask(gc);
	ckfree((char *) gc);
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetForeground, etc. --
 *
 *	The following functions are simply accessor functions for
 *	the GC slots.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Each function sets some slot in the GC.
 *
 *----------------------------------------------------------------------
 */

int
XSetForeground(display, gc, foreground)
    Display *display;
    GC gc;
    unsigned long foreground;
{
    gc->foreground = foreground;
    return 0;
}

int
XSetBackground(display, gc, background)
    Display *display;
    GC gc;
    unsigned long background;
{
    gc->background = background;
    return 0;
}

int
XSetDashes(display, gc, dash_offset, dash_list, n)
    Display* display;
    GC gc;
    int dash_offset;
    _Xconst char* dash_list;
    int n;
{
    char *p = &(gc->dashes);

#ifdef TkWinDeleteBrush
    TkWinDeleteBrush(gc->fgBrush);
    TkWinDeletePen(gc->fgPen);
    TkWinDeleteBrush(gc->bgBrush);
    TkWinDeletePen(gc->fgExtPen);
#endif
    gc->dash_offset = dash_offset;
    if (n > MAX_DASH_LIST_SIZE) n = MAX_DASH_LIST_SIZE;
    while (n-- > 0) {
	*p++ = *dash_list++;
    }
    *p = 0;
    return 0;
}

int
XSetFunction(display, gc, function)
    Display *display;
    GC gc;
    int function;
{
    gc->function = function;
    return 0;
}

int
XSetFillRule(display, gc, fill_rule)
    Display *display;
    GC gc;
    int fill_rule;
{
    gc->fill_rule = fill_rule;
    return 0;
}

int
XSetFillStyle(display, gc, fill_style)
    Display *display;
    GC gc;
    int fill_style;
{
    gc->fill_style = fill_style;
    return 0;
}

int
XSetTSOrigin(display, gc, x, y)
    Display *display;
    GC gc;
    int x, y;
{
    gc->ts_x_origin = x;
    gc->ts_y_origin = y;
    return 0;
}

int
XSetFont(display, gc, font)
    Display *display;
    GC gc;
    Font font;
{
    gc->font = font;
    return 0;
}

int
XSetArcMode(display, gc, arc_mode)
    Display *display;
    GC gc;
    int arc_mode;
{
    gc->arc_mode = arc_mode;
    return 0;
}

int
XSetStipple(display, gc, stipple)
    Display *display;
    GC gc;
    Pixmap stipple;
{
    gc->stipple = stipple;
    return 0;
}

int
XSetLineAttributes(display, gc, line_width, line_style, cap_style,
	join_style)
    Display *display;
    GC gc;
    unsigned int line_width;
    int line_style;
    int cap_style;
    int join_style;
{
    gc->line_width = line_width;
    gc->line_style = line_style;
    gc->cap_style = cap_style;
    gc->join_style = join_style;
    return 0;
}

int
XSetClipOrigin(display, gc, clip_x_origin, clip_y_origin)
    Display* display;
    GC gc;
    int clip_x_origin;
    int clip_y_origin;
{
    gc->clip_x_origin = clip_x_origin;
    gc->clip_y_origin = clip_y_origin;
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkSetRegion, XSetClipMask --
 *
 *	Sets the clipping region/pixmap for a GC.
 *
 *	Note that unlike the Xlib equivalent, it is not safe to delete the
 *	region after setting it into the GC (except on Mac OS X). The only
 *	uses of TkSetRegion are currently in DisplayFrame and in
 *	ImgPhotoDisplay, which use the GC immediately.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates or dealloates a TkpClipMask.
 *
 *----------------------------------------------------------------------
 */

void
TkSetRegion(display, gc, r)
    Display* display;
    GC gc;
    TkRegion r;
{
    if (r == None) {
	FreeClipMask(gc);
    } else {
	TkpClipMask *clip_mask = AllocClipMask(gc);

	clip_mask->type = TKP_CLIP_REGION;
	clip_mask->value.region = r;
#ifdef MAC_OSX_TK
	TkpRetainRegion(r);
#endif
    }
}

int
XSetClipMask(display, gc, pixmap)
    Display* display;
    GC gc;
    Pixmap pixmap;
{
    if (pixmap == None) {
	FreeClipMask(gc);
    } else {
	TkpClipMask *clip_mask = AllocClipMask(gc);

	clip_mask->type = TKP_CLIP_PIXMAP;
	clip_mask->value.pixmap = pixmap;
    }
    return 0;
}

/*
 * Some additional dummy functions (hopefully implemented soon).
 */

#if 0
Cursor
XCreateFontCursor(display, shape)
    Display* display;
    unsigned int shape;
{
    return (Cursor) 0;
}

void
XDrawImageString(display, d, gc, x, y, string, length)
    Display* display;
    Drawable d;
    GC gc;
    int x;
    int y;
    _Xconst char* string;
    int length;
{
}
#endif

void
XDrawPoint(display, d, gc, x, y)
    Display* display;
    Drawable d;
    GC gc;
    int x;
    int y;
{
    XDrawLine(display, d, gc, x, y, x, y);
}

void
XDrawPoints(display, d, gc, points, npoints, mode)
    Display* display;
    Drawable d;
    GC gc;
    XPoint* points;
    int npoints;
    int mode;
{
    int i;

    for (i=0; i<npoints; i++) {
	XDrawPoint(display, d, gc, points[i].x, points[i].y);
    }
}

#if !defined(MAC_TCL) && !defined(MAC_OSX_TK)
void
XDrawSegments(display, d, gc, segments, nsegments)
    Display* display;
    Drawable d;
    GC gc;
    XSegment* segments;
    int nsegments;
{
}
#endif

#if 0
char *
XFetchBuffer(display, nbytes_return, buffer)
    Display* display;
    int* nbytes_return;
    int buffer;
{
    return (char *) 0;
}

Status XFetchName(display, w, window_name_return)
    Display* display;
    Window w;
    char** window_name_return;
{
    return (Status) 0;
}

Atom *XListProperties(display, w, num_prop_return)
    Display* display;
    Window w;
    int* num_prop_return;
{
    return (Atom *) 0;
}

void
XMapRaised(display, w)
    Display* display;
    Window w;
{
}

void
XPutImage(display, d, gc, image, src_x, src_y, dest_x, dest_y, width, height)
    Display* display;
    Drawable d;
    GC gc;
    XImage* image;
    int src_x;
    int src_y;
    int dest_x;
    int dest_y;
    unsigned int width;
    unsigned int height;
{
}

void
XQueryTextExtents(display, font_ID, string, nchars, direction_return,
	font_ascent_return, font_descent_return, overall_return)
    Display* display;
    XID font_ID;
    _Xconst char* string;
    int nchars;
    int* direction_return;
    int* font_ascent_return;
    int* font_descent_return;
    XCharStruct* overall_return;
{
}

void
XReparentWindow(display, w, parent, x, y)
    Display* display;
    Window w;
    Window parent;
    int x;
    int y;
{
}

void
XRotateBuffers(display, rotate)
    Display* display;
    int rotate;
{
}

void
XStoreBuffer(display, bytes, nbytes, buffer)
    Display* display;
    _Xconst char* bytes;
    int nbytes;
    int buffer;
{
}

void
XUndefineCursor(display, w)
    Display* display;
    Window w;
{
}
#endif
