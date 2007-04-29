/*
 * tkMacOSXRegion.c --
 *
 *	Implements X window calls for manipulating regions
 *
 * Copyright (c) 1995-1996 Sun Microsystems, Inc.
 * Copyright 2001, Apple Computer, Inc.
 * Copyright (c) 2006-2007 Daniel A. Steffen <das@users.sourceforge.net>
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkMacOSXRegion.c,v 1.2.2.3 2007/04/29 02:26:50 das Exp $
 */

#include "tkMacOSXInt.h"


/*
 *----------------------------------------------------------------------
 *
 * TkCreateRegion --
 *
 *	Implements the equivelent of the X window function
 *	XCreateRegion. See X window documentation for more details.
 *
 * Results:
 *	Returns an allocated region handle.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkRegion
TkCreateRegion(void)
{
    RgnHandle rgn;
    rgn = NewRgn();
    return (TkRegion) rgn;
}

/*
 *----------------------------------------------------------------------
 *
 * TkDestroyRegion --
 *
 *	Implements the equivelent of the X window function
 *	XDestroyRegion. See X window documentation for more details.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is freed.
 *
 *----------------------------------------------------------------------
 */

void
TkDestroyRegion(
    TkRegion r)
{
    RgnHandle rgn = (RgnHandle) r;
    DisposeRgn(rgn);
}

/*
 *----------------------------------------------------------------------
 *
 * TkIntersectRegion --
 *
 *	Implements the equivilent of the X window function
 *	XIntersectRegion. See X window documentation for more details.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkIntersectRegion(
    TkRegion sra,
    TkRegion srb,
    TkRegion dr_return)
{
    RgnHandle srcRgnA = (RgnHandle) sra;
    RgnHandle srcRgnB = (RgnHandle) srb;
    RgnHandle destRgn = (RgnHandle) dr_return;
    SectRgn(srcRgnA, srcRgnB, destRgn);
}

/*
 *----------------------------------------------------------------------
 *
 * TkUnionRectWithRegion --
 *
 *	Implements the equivelent of the X window function
 *	XUnionRectWithRegion. See X window documentation for more
 *	details.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkUnionRectWithRegion(
    XRectangle* rectangle,
    TkRegion src_region,
    TkRegion dest_region_return)
{
    RgnHandle srcRgn = (RgnHandle) src_region;
    RgnHandle destRgn = (RgnHandle) dest_region_return;

    TkMacOSXCheckTmpRgnEmpty(1);
    SetRectRgn(tkMacOSXtmpRgn1, rectangle->x, rectangle->y,
	    rectangle->x + rectangle->width, rectangle->y + rectangle->height);
    UnionRgn(srcRgn, tkMacOSXtmpRgn1, destRgn);
    SetEmptyRgn(tkMacOSXtmpRgn1);
}

/*
 *----------------------------------------------------------------------
 *
 * TkRectInRegion --
 *
 *	Implements the equivelent of the X window function
 *	XRectInRegion. See X window documentation for more details.
 *
 * Results:
 *	Returns one of: RectangleOut, RectangleIn, RectanglePart.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkRectInRegion(
    TkRegion region,
    int x,
    int y,
    unsigned int width,
    unsigned int height)
{
    RgnHandle rgn = (RgnHandle) region;
    int result;

    TkMacOSXCheckTmpRgnEmpty(1);
    SetRectRgn(tkMacOSXtmpRgn1, x, y, x + width, y + height);
    SectRgn(rgn, tkMacOSXtmpRgn1, tkMacOSXtmpRgn1);
    if (EmptyRgn(tkMacOSXtmpRgn1)) {
	result = RectangleOut;
    } else if (EqualRgn(rgn, tkMacOSXtmpRgn1)) {
	result = RectangleIn;
    } else {
	result = RectanglePart;
    }
    SetEmptyRgn(tkMacOSXtmpRgn1);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TkClipBox --
 *
 *	Implements the equivelent of the X window function XClipBox.
 *	See X window documentation for more details.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkClipBox(
    TkRegion r,
    XRectangle* rect_return)
{
    RgnHandle rgn = (RgnHandle) r;
    Rect      rect;

    GetRegionBounds(rgn,&rect);

    rect_return->x = rect.left;
    rect_return->y = rect.top;
    rect_return->width = rect.right-rect.left;
    rect_return->height = rect.bottom-rect.top;
}

/*
 *----------------------------------------------------------------------
 *
 * TkSubtractRegion --
 *
 *	Implements the equivilent of the X window function
 *	XSubtractRegion. See X window documentation for more details.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkSubtractRegion(
    TkRegion sra,
    TkRegion srb,
    TkRegion dr_return)
{
    RgnHandle srcRgnA = (RgnHandle) sra;
    RgnHandle srcRgnB = (RgnHandle) srb;
    RgnHandle destRgn = (RgnHandle) dr_return;

    DiffRgn(srcRgnA, srcRgnB, destRgn);
}

#if 0
int
XSetClipRectangles(Display *d, GC gc, int clip_x_origin, int clip_y_origin,
	XRectangle* rectangles, int n, int ordering)
{
    RgnHandle clipRgn;

    if (gc->clip_mask && ((TkpClipMask*)gc->clip_mask)->type
	    == TKP_CLIP_REGION) {
	clipRgn = (RgnHandle) ((TkpClipMask*)gc->clip_mask)->value.region;
	SetEmptyRgn(clipRgn);
    } else {
	clipRgn = NewRgn(); /* LEAK! */
    }

    TkMacOSXCheckTmpRgnEmpty(1);
    while (n--) {
	int x = clip_x_origin + rectangles->x;
	int y = clip_y_origin + rectangles->y;

	SetRectRgn(tkMacOSXtmpRgn1, x, y, x + rectangles->width,
		y + rectangles->height);
	UnionRgn(tkMacOSXtmpRgn1, clipRgn, clipRgn);
	rectangles++;
    }
    SetEmptyRgn(tkMacOSXtmpRgn1);
    TkSetRegion(d, gc, (TkRegion) clipRgn);
    return 1;
}
#endif
