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
 * RCS: @(#) $Id: tkMacOSXRegion.c,v 1.2.2.6 2007/06/29 03:22:02 das Exp $
 */

#include "tkMacOSXPrivate.h"


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
    return (TkRegion) NewRgn();
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
    DisposeRgn((RgnHandle) r);
}

/*
 *----------------------------------------------------------------------
 *
 * TkIntersectRegion --
 *
 *	Implements the equivalent of the X window function
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
    SectRgn((RgnHandle) sra, (RgnHandle) srb, (RgnHandle) dr_return);
}

/*
 *----------------------------------------------------------------------
 *
 * TkSubtractRegion --
 *
 *	Implements the equivalent of the X window function
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
    DiffRgn((RgnHandle) sra, (RgnHandle) srb, (RgnHandle) dr_return);
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
    TkMacOSXCheckTmpQdRgnEmpty();
    SetRectRgn(tkMacOSXtmpQdRgn, rectangle->x, rectangle->y,
	    rectangle->x + rectangle->width, rectangle->y + rectangle->height);
    UnionRgn((RgnHandle) src_region, tkMacOSXtmpQdRgn,
	    (RgnHandle) dest_region_return);
    SetEmptyRgn(tkMacOSXtmpQdRgn);
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
    int result;

    TkMacOSXCheckTmpQdRgnEmpty();
    SetRectRgn(tkMacOSXtmpQdRgn, x, y, x + width, y + height);
    SectRgn((RgnHandle) region, tkMacOSXtmpQdRgn, tkMacOSXtmpQdRgn);
    if (EmptyRgn(tkMacOSXtmpQdRgn)) {
	result = RectangleOut;
    } else if (EqualRgn((RgnHandle) region, tkMacOSXtmpQdRgn)) {
	result = RectangleIn;
    } else {
	result = RectanglePart;
    }
    SetEmptyRgn(tkMacOSXtmpQdRgn);
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
    Rect rect;

    GetRegionBounds((RgnHandle) r,&rect);
    rect_return->x = rect.left;
    rect_return->y = rect.top;
    rect_return->width = rect.right-rect.left;
    rect_return->height = rect.bottom-rect.top;
}
