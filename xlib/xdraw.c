/*
 * xdraw.c --
 *
 *	This file contains generic procedures related to X drawing primitives.
 *
 * Copyright © 1995 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tk.h"

/*
 *----------------------------------------------------------------------
 *
 * XDrawLine --
 *
 *	Draw a single line between two points in a given drawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws a single line segment.
 *
 *----------------------------------------------------------------------
 */

int
XDrawLine(
    Display *display,
    Drawable d,
    GC gc,
    int x1, int y1,
    int x2, int y2)		/* Coordinates of line segment. */
{
    XPoint points[2];

    points[0].x = (short)x1;
    points[0].y = (short)y1;
    points[1].x = (short)x2;
    points[1].y = (short)y2;
    return XDrawLines(display, d, gc, points, 2, CoordModeOrigin);
}

/*
 *----------------------------------------------------------------------
 *
 * XFillRectangle --
 *
 *	Fills a rectangular area in the given drawable. This procedure is
 *	implemented as a call to XFillRectangles.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Fills the specified rectangle.
 *
 *----------------------------------------------------------------------
 */

int
XFillRectangle(
    Display *display,
    Drawable d,
    GC gc,
    int x,
    int y,
    unsigned int width,
    unsigned int height)
{
    XRectangle rectangle;
    rectangle.x = (short)x;
    rectangle.y = (short)y;
    rectangle.width = (unsigned short)width;
    rectangle.height = (unsigned short)height;
    return XFillRectangles(display, d, gc, &rectangle, 1);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
