/*
 * tkWinRegion.c --
 *
 *	Tk Region emulation code.
 *
 * Copyright © 1995 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkWinInt.h"


/*
 *----------------------------------------------------------------------
 *
 * XCreateRegion --
 *
 *	Construct an empty region.
 *
 * Results:
 *	Returns a new region handle.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Region
XCreateRegion(void)
{
    RECT rect;
    memset(&rect, 0, sizeof(RECT));
    return (Region)CreateRectRgnIndirect(&rect);
}

/*
 *----------------------------------------------------------------------
 *
 * XDestroyRegion --
 *
 *	Destroy the specified region.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees the storage associated with the specified region.
 *
 *----------------------------------------------------------------------
 */

int
XDestroyRegion(
    Region r)
{
    DeleteObject((HRGN) r);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XClipBox --
 *
 *	Computes the bounding box of a region.
 *
 * Results:
 *	Sets rect_return to the bounding box of the region.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XClipBox(
    Region r,
    XRectangle* rect_return)
{
    RECT rect;

    GetRgnBox((HRGN)r, &rect);
    rect_return->x = (short) rect.left;
    rect_return->y = (short) rect.top;
    rect_return->width = (short) (rect.right - rect.left);
    rect_return->height = (short) (rect.bottom - rect.top);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XIntersectRegion --
 *
 *	Compute the intersection of two regions.
 *
 * Results:
 *	Returns the result in the dr_return region.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XIntersectRegion(
    Region sra,
    Region srb,
    Region dr_return)
{
    CombineRgn((HRGN) dr_return, (HRGN) sra, (HRGN) srb, RGN_AND);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XUnionRectWithRegion --
 *
 *	Create the union of a source region and a rectangle.
 *
 * Results:
 *	Returns the result in the dr_return region.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XUnionRectWithRegion(
    XRectangle *rectangle,
    Region src_region,
    Region dest_region_return)
{
    HRGN rectRgn = CreateRectRgn(rectangle->x, rectangle->y,
	    rectangle->x + rectangle->width, rectangle->y + rectangle->height);

    CombineRgn((HRGN) dest_region_return, (HRGN) src_region,
	    (HRGN) rectRgn, RGN_OR);
    DeleteObject(rectRgn);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpBuildRegionFromAlphaData --
 *
 *	Set up a rectangle of the given region based on the supplied alpha
 *	data.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	The region is updated, with extra pixels added to it.
 *
 *----------------------------------------------------------------------
 */

void
TkpBuildRegionFromAlphaData(
    Region region,
    unsigned int x, unsigned int y,
				/* Where in region to update. */
    unsigned int width, unsigned int height,
				/* Size of rectangle to update. */
    unsigned char *dataPtr,	/* Data to read from. */
    unsigned int pixelStride,	/* Num bytes from one piece of alpha data to
				 * the next in the line. */
    unsigned int lineStride)	/* Num bytes from one line of alpha data to
				 * the next line. */
{
    unsigned char *lineDataPtr;
    unsigned int x1, y1, end;
    HRGN rectRgn = CreateRectRgn(0,0,1,1); /* Workspace region. */

    for (y1 = 0; y1 < height; y1++) {
	lineDataPtr = dataPtr;
	for (x1 = 0; x1 < width; x1 = end) {
	    /*
	     * Search for first non-transparent pixel.
	     */

	    while ((x1 < width) && !*lineDataPtr) {
		x1++;
		lineDataPtr += pixelStride;
	    }
	    end = x1;

	    /*
	     * Search for first transparent pixel.
	     */

	    while ((end < width) && *lineDataPtr) {
		end++;
		lineDataPtr += pixelStride;
	    }
	    if (end > x1) {
		/*
		 * Manipulate Win32 regions directly; it's more efficient.
		 */

		SetRectRgn(rectRgn, (int) (x+x1), (int) (y+y1),
			(int) (x+end), (int) (y+y1+1));
		CombineRgn((HRGN) region, (HRGN) region, rectRgn, RGN_OR);
	    }
	}
	dataPtr += lineStride;
    }

    DeleteObject(rectRgn);
}

/*
 *----------------------------------------------------------------------
 *
 * XRectInRegion --
 *
 *	Test whether a given rectangle overlaps with a region.
 *
 * Results:
 *	Returns RectanglePart or RectangleOut. Note that this is not a
 *	complete implementation since it doesn't test for RectangleIn.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XRectInRegion(
    Region r,			/* Region to inspect */
    int x, int y,		/* Top-left of rectangle */
    unsigned int width,		/* Width of rectangle */
    unsigned int height)	/* Height of rectangle */
{
    RECT rect;
    rect.top = y;
    rect.left = x;
    rect.bottom = y+height;
    rect.right = x+width;
    return RectInRegion((HRGN)r, &rect) ? RectanglePart : RectangleOut;
}

/*
 *----------------------------------------------------------------------
 *
 * XSubtractRegion --
 *
 *	Compute the set-difference of two regions.
 *
 * Results:
 *	Returns the result in the dr_return region.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSubtractRegion(
    Region sra,
    Region srb,
    Region dr_return)
{
    CombineRgn((HRGN) dr_return, (HRGN) sra, (HRGN) srb, RGN_DIFF);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpCopyRegion --
 *
 *  Makes the destination region a copy of the source region.
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
TkpCopyRegion(
    Region dst,
    Region src)
{
    CombineRgn((HRGN)dst, (HRGN)src, NULL, RGN_COPY);
}

int
XUnionRegion(
    Region srca,
    Region srcb,
    Region dr_return)
{
    CombineRgn((HRGN)dr_return, (HRGN)srca, (HRGN)srcb, RGN_OR);
    return 1;
}

int
XOffsetRegion(
    Region r,
    int dx,
    int dy)
{
    OffsetRgn((HRGN)r, dx, dy);
    return 1;
}

Bool
XPointInRegion(
    Region r,
    int x,
    int y)
{
    return PtInRegion((HRGN)r, x, y);
}

Bool
XEqualRegion(
    Region r1,
    Region r2)
{
    return EqualRgn((HRGN)r1, (HRGN)r2);
}

int
XXorRegion(
    Region sra,
    Region srb,
    Region dr_return)
{
    CombineRgn((HRGN)dr_return, (HRGN)sra, (HRGN)srb, RGN_XOR);
    return 0;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
