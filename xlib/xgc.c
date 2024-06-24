/*
 * xgc.c --
 *
 *	This file contains generic routines for manipulating X graphics
 *	contexts.
 *
 * Copyright © 1995-1996 Sun Microsystems, Inc.
 * Copyright © 2002-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2008-2009 Apple Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include <X11/Xlib.h>
#if defined(MAC_OSX_TK)
#endif


#define MAX_DASH_LIST_SIZE 10
typedef struct {
    XGCValues gc;
    char dash[MAX_DASH_LIST_SIZE];
} XGCValuesWithDash;

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

    if (clip_mask == NULL) {
	clip_mask = (TkpClipMask *)ckalloc(sizeof(TkpClipMask));
	gc->clip_mask = (Pixmap) clip_mask;
    } else if (clip_mask->type == TKP_CLIP_REGION) {
	TkDestroyRegion(clip_mask->value.region);
    }
    clip_mask->type = TKP_CLIP_PIXMAP;
    clip_mask->value.pixmap = None;
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
    TkpClipMask * clip_mask = (TkpClipMask*)gc->clip_mask;
    if (clip_mask == NULL) {
	return;
    }
    if (clip_mask->type == TKP_CLIP_REGION) {
	TkDestroyRegion(clip_mask->value.region);
    }
    ckfree(clip_mask);
    gc->clip_mask = None;
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
XCreateGC(
    Display *display,
    TCL_UNUSED(Drawable),
    unsigned long mask,
    XGCValues *values)
{
    GC gp;

    /*
     * In order to have room for a dash list, MAX_DASH_LIST_SIZE extra chars
     * are defined, which is invisible from the outside. The list is assumed
     * to end with a 0-char, so this must be set explicitly during
     * initialization.
     */

    gp = (GC)ckalloc(sizeof(XGCValuesWithDash));

#define InitField(name,maskbit,default) \
	(gp->name = (mask & (maskbit)) ? values->name : (default))

    InitField(function,		  GCFunction,		GXcopy);
    InitField(plane_mask,	  GCPlaneMask,		(unsigned long)(~0));
    InitField(foreground,	  GCForeground,
	    BlackPixelOfScreen(DefaultScreenOfDisplay(display)));
    InitField(background,	  GCBackground,
	    WhitePixelOfScreen(DefaultScreenOfDisplay(display)));
    InitField(line_width,	  GCLineWidth,		1);
    InitField(line_style,	  GCLineStyle,		LineSolid);
    InitField(cap_style,	  GCCapStyle,		0);
    InitField(join_style,	  GCJoinStyle,		0);
    InitField(fill_style,	  GCFillStyle,		FillSolid);
    InitField(fill_rule,	  GCFillRule,		EvenOddRule);
    InitField(arc_mode,		  GCArcMode,		ArcPieSlice);
    InitField(tile,		  GCTile,		0);
    InitField(stipple,		  GCStipple,		0);
    InitField(ts_x_origin,	  GCTileStipXOrigin,	0);
    InitField(ts_y_origin,	  GCTileStipYOrigin,	0);
    InitField(font,		  GCFont,		0);
    InitField(subwindow_mode,	  GCSubwindowMode,	ClipByChildren);
    InitField(graphics_exposures, GCGraphicsExposures,	True);
    InitField(clip_x_origin,	  GCClipXOrigin,	0);
    InitField(clip_y_origin,	  GCClipYOrigin,	0);
    InitField(dash_offset,	  GCDashOffset,		0);
    InitField(dashes,		  GCDashList,		4);
    (&(gp->dashes))[1] = 0;

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
 *	Changes the GC components specified by valuemask for the specified GC.
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
XChangeGC(
    Display *d,
    GC gc,
    unsigned long mask,
    XGCValues *values)
{
#define ModifyField(name,maskbit) \
	if (mask & (maskbit)) { gc->name = values->name; }

    ModifyField(function, GCFunction);
    ModifyField(plane_mask, GCPlaneMask);
    ModifyField(foreground, GCForeground);
    ModifyField(background, GCBackground);
    ModifyField(line_width, GCLineWidth);
    ModifyField(line_style, GCLineStyle);
    ModifyField(cap_style, GCCapStyle);
    ModifyField(join_style, GCJoinStyle);
    ModifyField(fill_style, GCFillStyle);
    ModifyField(fill_rule, GCFillRule);
    ModifyField(arc_mode, GCArcMode);
    ModifyField(tile, GCTile);
    ModifyField(stipple, GCStipple);
    ModifyField(ts_x_origin, GCTileStipXOrigin);
    ModifyField(ts_y_origin, GCTileStipYOrigin);
    ModifyField(font, GCFont);
    ModifyField(subwindow_mode, GCSubwindowMode);
    ModifyField(graphics_exposures, GCGraphicsExposures);
    ModifyField(clip_x_origin, GCClipXOrigin);
    ModifyField(clip_y_origin, GCClipYOrigin);
    ModifyField(dash_offset, GCDashOffset);
    if (mask & GCClipMask) {
	XSetClipMask(d, gc, values->clip_mask);
    }
    if (mask & GCDashList) {
	gc->dashes = values->dashes;
	(&(gc->dashes))[1] = 0;
    }
    return Success;
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

int XFreeGC(
    TCL_UNUSED(Display *),
    GC gc)
{
    if (gc != NULL) {
	FreeClipMask(gc);
	ckfree(gc);
    }
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetForeground, etc. --
 *
 *	The following functions are simply accessor functions for the GC
 *	slots.
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
XSetForeground(
    TCL_UNUSED(Display *),
    GC gc,
    unsigned long foreground)
{
    gc->foreground = foreground;
    return Success;
}

int
XSetBackground(
    TCL_UNUSED(Display *),
    GC gc,
    unsigned long background)
{
    gc->background = background;
    return Success;
}

int
XSetDashes(
    TCL_UNUSED(Display *),
    GC gc,
    int dash_offset,
    _Xconst char *dash_list,
    int n)
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
    return Success;
}

int
XSetFunction(
    TCL_UNUSED(Display *),
    GC gc,
    int function)
{
    gc->function = function;
    return Success;
}

int
XSetFillRule(
    TCL_UNUSED(Display *),
    GC gc,
    int fill_rule)
{
    gc->fill_rule = fill_rule;
    return Success;
}

int
XSetFillStyle(
    TCL_UNUSED(Display *),
    GC gc,
    int fill_style)
{
    gc->fill_style = fill_style;
    return Success;
}

int
XSetTSOrigin(
    TCL_UNUSED(Display *),
    GC gc,
    int x, int y)
{
    gc->ts_x_origin = x;
    gc->ts_y_origin = y;
    return Success;
}

int
XSetFont(
    TCL_UNUSED(Display *),
    GC gc,
    Font font)
{
    gc->font = font;
    return Success;
}

int
XSetArcMode(
    TCL_UNUSED(Display *),
    GC gc,
    int arc_mode)
{
    gc->arc_mode = arc_mode;
    return Success;
}

int
XSetStipple(
    TCL_UNUSED(Display *),
    GC gc,
    Pixmap stipple)
{
    gc->stipple = stipple;
    return Success;
}

int
XSetLineAttributes(
    TCL_UNUSED(Display *),
    GC gc,
    unsigned int line_width,
    int line_style,
    int cap_style,
    int join_style)
{
    gc->line_width = line_width;
    gc->line_style = line_style;
    gc->cap_style = cap_style;
    gc->join_style = join_style;
    return Success;
}

int
XSetClipOrigin(
    TCL_UNUSED(Display *),
    GC gc,
    int clip_x_origin,
    int clip_y_origin)
{
    gc->clip_x_origin = clip_x_origin;
    gc->clip_y_origin = clip_y_origin;
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * TkSetRegion, XSetClipMask, XSetClipRectangles --
 *
 *	Sets the clipping region/pixmap for a GC.
 *
 *	Like the Xlib equivalent, it is safe to delete the
 *	region after setting it into the GC.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates or deallocates a TkpClipMask.
 *
 *----------------------------------------------------------------------
 */

int
TkSetRegion(
    TCL_UNUSED(Display *),
    GC gc,
    TkRegion r)
{
    if (r == NULL) {
	Tcl_Panic("must not pass NULL to TkSetRegion for compatibility with X11; use XSetClipMask instead");
    } else {
	TkpClipMask *clip_mask = AllocClipMask(gc);

	clip_mask->type = TKP_CLIP_REGION;
	clip_mask->value.region = r;
	clip_mask->value.region = TkCreateRegion();
	TkpCopyRegion(clip_mask->value.region, r);
    }
    return Success;
}

int
XSetClipMask(
    TCL_UNUSED(Display *),
    GC gc,
    Pixmap pixmap)
{
    if (pixmap == None) {
	FreeClipMask(gc);
    } else {
	TkpClipMask *clip_mask = AllocClipMask(gc);

	clip_mask->type = TKP_CLIP_PIXMAP;
	clip_mask->value.pixmap = pixmap;
    }
    return Success;
}

int
XSetClipRectangles(
    TCL_UNUSED(Display*),
    GC gc,
    int clip_x_origin,
    int clip_y_origin,
    XRectangle* rectangles,
    int n,
    TCL_UNUSED(int))
{
    TkRegion clipRgn = TkCreateRegion();
    TkpClipMask * clip_mask = AllocClipMask(gc);
    clip_mask->type = TKP_CLIP_REGION;
    clip_mask->value.region = clipRgn;

    while (n--) {
	XRectangle rect = *rectangles;

	rect.x += clip_x_origin;
	rect.y += clip_y_origin;
	TkUnionRectWithRegion(&rect, clipRgn, clipRgn);
	rectangles++;
    }
    return 1;
}

/*
 * Some additional dummy functions (hopefully implemented soon).
 */

#if 0
Cursor
XCreateFontCursor(
    Display *display,
    unsigned int shape)
{
    return (Cursor) 0;
}

void
XDrawImageString(
    Display *display,
    Drawable d,
    GC gc,
    int x,
    int y,
    _Xconst char *string,
    int length)
{
}
#endif

int
XDrawPoint(
    Display *display,
    Drawable d,
    GC gc,
    int x,
    int y)
{
    return XDrawLine(display, d, gc, x, y, x, y);
}

int
XDrawPoints(
    Display *display,
    Drawable d,
    GC gc,
    XPoint *points,
    int npoints,
    TCL_UNUSED(int))
{
    int res = Success;

    while (npoints-- > 0) {
	res = XDrawLine(display, d, gc,
		points[0].x, points[0].y, points[0].x, points[0].y);
	if (res != Success) break;
	++points;
    }
    return res;
}

#if !defined(MAC_OSX_TK)
int
XDrawSegments(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Drawable),
    TCL_UNUSED(GC),
    TCL_UNUSED(XSegment *),
    TCL_UNUSED(int))
{
    return BadDrawable;
}
#endif

char *
XFetchBuffer(
    TCL_UNUSED(Display *),
    TCL_UNUSED(int *),
    TCL_UNUSED(int))
{
    return (char *) 0;
}

Status
XFetchName(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(char **))
{
    return Success;
}

Atom *
XListProperties(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(int *))
{
    return (Atom *) 0;
}

int
XMapRaised(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window))
{
    return Success;
}

int
XQueryTextExtents(
    TCL_UNUSED(Display *),
    TCL_UNUSED(XID),
    TCL_UNUSED(_Xconst char *),
    TCL_UNUSED(int),
    TCL_UNUSED(int *),
    TCL_UNUSED(int *),
    TCL_UNUSED(int *),
    TCL_UNUSED(XCharStruct *))
{
    return Success;
}

int
XReparentWindow(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Window),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    return BadWindow;
}

int
XUndefineCursor(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window))
{
    return Success;
}

XVaNestedList
XVaCreateNestedList(
    TCL_UNUSED(int), ...)
{
    return NULL;
}

char *
XSetICValues(
    TCL_UNUSED(XIC), ...)
{
    return NULL;
}

char *
XGetICValues(
    TCL_UNUSED(XIC), ...)
{
    return NULL;
}

void
XSetICFocus(
    TCL_UNUSED(XIC))
{
}

Window
XCreateWindow(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(int),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(Visual *),
    TCL_UNUSED(unsigned long),
    TCL_UNUSED(XSetWindowAttributes *))
{
	return 0;
}

int
XPointInRegion(
    TCL_UNUSED(Region),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
	return 0;
}

int
XUnionRegion(
    TCL_UNUSED(Region),
    TCL_UNUSED(Region),
    TCL_UNUSED(Region))
{
	return 0;
}

Region
XPolygonRegion(
    TCL_UNUSED(XPoint *),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    return 0;
}

void
XDestroyIC(
    TCL_UNUSED(XIC))
{
}

Cursor
XCreatePixmapCursor(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Pixmap),
    TCL_UNUSED(Pixmap),
    TCL_UNUSED(XColor *),
    TCL_UNUSED(XColor *),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(unsigned int))
{
    return (Cursor) NULL;
}

Cursor
XCreateGlyphCursor(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Font),
    TCL_UNUSED(Font),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(XColor _Xconst *),
    TCL_UNUSED(XColor _Xconst *))
{
    return (Cursor) NULL;
}

XFontSet
XCreateFontSet(
    TCL_UNUSED(Display *)		/* display */,
    TCL_UNUSED(_Xconst char *)	/* base_font_name_list */,
    TCL_UNUSED(char ***)		/* missing_charset_list */,
    TCL_UNUSED(int *)		/* missing_charset_count */,
    TCL_UNUSED(char **)		/* def_string */
) {
    return (XFontSet)0;
}

void
XFreeFontSet(
    TCL_UNUSED(Display *),		/* display */
    TCL_UNUSED(XFontSet)		/* font_set */
) {
}

void
XFreeStringList(
    TCL_UNUSED(char **)		/* list */
) {
}

Status
XCloseIM(
    TCL_UNUSED(XIM) /* im */
) {
    return Success;
}

Bool
XRegisterIMInstantiateCallback(
    TCL_UNUSED(Display *)			/* dpy */,
    TCL_UNUSED(struct _XrmHashBucketRec *)	/* rdb */,
    TCL_UNUSED(char *)			/* res_name */,
    TCL_UNUSED(char *)			/* res_class */,
    TCL_UNUSED(XIDProc)			/* callback */,
    TCL_UNUSED(XPointer)			/* client_data */
) {
    return False;
}

Bool
XUnregisterIMInstantiateCallback(
    TCL_UNUSED(Display *) 		/* dpy */,
    TCL_UNUSED(struct _XrmHashBucketRec *)	/* rdb */,
    TCL_UNUSED(char *)			/* res_name */,
    TCL_UNUSED(char *)			/* res_class */,
    TCL_UNUSED(XIDProc)			/* callback */,
    TCL_UNUSED(XPointer)			/* client_data */
) {
    return False;
}

char *
XSetLocaleModifiers(
    TCL_UNUSED(const char *)		/* modifier_list */
) {
    return NULL;
}

XIM XOpenIM(
    TCL_UNUSED(Display *)			/* dpy */,
    TCL_UNUSED(struct _XrmHashBucketRec *)	/* rdb */,
    TCL_UNUSED(char *)			/* res_name */,
    TCL_UNUSED(char *)			/* res_class */
) {
    return NULL;
}

char *
XGetIMValues(
    TCL_UNUSED(XIM) /* im */, ...
) {
    return NULL;
}

char *
XSetIMValues(
    TCL_UNUSED(XIM) /* im */, ...
) {
    return NULL;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
