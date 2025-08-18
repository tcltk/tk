/*
 * tkMacOSXRegion.c --
 *
 *	Implements X window calls for manipulating regions
 *
 * Copyright © 1995-1996 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2006-2009 Daniel A. Steffen <das@users.sourceforge.net>
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
static void ReleaseRegion(TkRegion r);

#ifdef DEBUG
static int totalRegions = 0;
static int totalRegionRetainCount = 0;
#define DebugLog(msg, ...) fprintf(stderr, (msg), ##__VA_ARGS__)
#else
#define DebugLog(msg, ...)
#endif


/*
 *----------------------------------------------------------------------
 *
 * XCreateRegion --
 *
 *	Implements the equivalent of the X window function XCreateRegion. See
 *	Xwindow documentation for more details.
 *
 * Results:
 *	Returns an allocated region handle.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Region
XCreateRegion(void)
{
    Region region = (Region) HIShapeCreateMutable();
    DebugLog("Created region: total regions = %d, total count is %d\n",
	++totalRegions, ++totalRegionRetainCount);
    return region;
}

/*
 *----------------------------------------------------------------------
 *
 * XDestroyRegion --
 *
 *	Implements the equivalent of the X window function XDestroyRegion. See
 *	Xwindow documentation for more details.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is freed.
 *
 *----------------------------------------------------------------------
 */

int
XDestroyRegion(
    Region r)
{
    if (r) {
	DebugLog("Destroyed region: total regions = %d\n", --totalRegions);
	ReleaseRegion(r);
    }
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XIntersectRegion --
 *
 *	Implements the equivalent of the X window function XIntersectRegion.
 *	See Xwindow documentation for more details.
 *
 * Results:
 *	None.
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
    ChkErr(HIShapeIntersect, (HIShapeRef) sra, (HIShapeRef) srb,
	   (HIMutableShapeRef) dr_return);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSubtractRegion --
 *
 *	Implements the equivalent of the X window function XSubtractRegion.
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

int
XSubtractRegion(
    Region sra,
    Region srb,
    Region dr_return)
{
    ChkErr(HIShapeDifference, (HIShapeRef) sra, (HIShapeRef) srb,
	   (HIMutableShapeRef) dr_return);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XUnionRectWithRegion --
 *
 *	Implements the equivalent of the X window function
 *	XUnionRectWithRegion. See Xwindow documentation for more details.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XUnionRectWithRegion(
    XRectangle* rectangle,
    Region src_region,
    Region dest_region_return)
{
    const CGRect r = CGRectMake(rectangle->x, rectangle->y,
	    rectangle->width, rectangle->height);

    if (src_region == dest_region_return) {
	ChkErr(HIShapeUnionWithRect,
		(HIMutableShapeRef) dest_region_return, &r);
    } else {
	HIShapeRef rectRgn = HIShapeCreateWithRect(&r);

	ChkErr(HIShapeUnion, rectRgn, (HIShapeRef) src_region,
		(HIMutableShapeRef) dest_region_return);
	CFRelease(rectRgn);
    }
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXIsEmptyRegion --
 *
 *	Return native region for given tk region.
 *
 * Results:
 *	1 if empty, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TkMacOSXIsEmptyRegion(
    Region r)
{
    return HIShapeIsEmpty((HIMutableShapeRef) r) ? 1 : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XRectInRegion --
 *
 *	Implements the equivalent of the X window function XRectInRegion. See
 *	Xwindow documentation for more details.
 *
 * Results:
 *	Returns RectanglePart or RectangleOut. Note that this is not a complete
 *	implementation since it doesn't test for RectangleIn.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XRectInRegion(
    Region region,
    int x,
    int y,
    unsigned int width,
    unsigned int height)
{
    if (TkMacOSXIsEmptyRegion(region)) {
	return RectangleOut;
    } else {
	const CGRect r = CGRectMake(x, y, width, height);

	return HIShapeIntersectsRect((HIShapeRef) region, &r) ?
		RectanglePart : RectangleOut;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XClipBox --
 *
 *	Implements the equivalent of the X window function XClipBox. See
 *	Xwindow documentation for more details.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XClipBox(
    Region r,
    XRectangle *rect_return)
{
    CGRect rect;

    HIShapeGetBounds((HIShapeRef) r, &rect);
    rect_return->x = rect.origin.x;
    rect_return->y = rect.origin.y;
    rect_return->width = rect.size.width;
    rect_return->height = rect.size.height;
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
    Region region,			/* Region to update. */
    unsigned int x,			/* Where in region to update. */
    unsigned int y,			/* Where in region to update. */
    unsigned int width,			/* Size of rectangle to update. */
    unsigned int height,		/* Size of rectangle to update. */
    unsigned char *dataPtr,		/* Data to read from. */
    unsigned int pixelStride,		/* num bytes from one piece of alpha
					 * data to the next in the line. */
    unsigned int lineStride)		/* num bytes from one line of alpha
					 * data to the next line. */
{
    unsigned char *lineDataPtr;
    unsigned int x1, y1, end;
    XRectangle rect;

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
		rect.x = x + x1;
		rect.y = y + y1;
		rect.width = end - x1;
		rect.height = 1;
		XUnionRectWithRegion(&rect, region, region);
	    }
	}
	dataPtr += lineStride;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ReleaseRegion --
 *
 *	Decreases reference count of region.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May free memory.
 *
 *----------------------------------------------------------------------
 */

static void
ReleaseRegion(
    Region r)
{
    CFRelease(r);
    DebugLog("Released region: total count is %d\n", --totalRegionRetainCount);
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXSetEmptyRegion --
 *
 *	Set region to emtpy.
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
TkMacOSXSetEmptyRegion(
    Region r)
{
    ChkErr(HIShapeSetEmpty, (HIMutableShapeRef) r);
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXGetNativeRegion --
 *
 *	Return native region for given tk region.
 *
 * Results:
 *	Native region, CFRelease when done.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

HIShapeRef
TkMacOSXGetNativeRegion(
    Region r)
{
    return (HIShapeRef) CFRetain(r);
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXSetWithNativeRegion --
 *
 *	Set region to the native region.
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
TkMacOSXSetWithNativeRegion(
    Region r,
    HIShapeRef rgn)
{
    ChkErr(HIShapeSetWithShape, (HIMutableShapeRef) r, rgn);
}

/*
 *----------------------------------------------------------------------
 *
 * XOffsetRegion --
 *
 *	Offsets region by given distances.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XOffsetRegion(
    Region r,
    int dx,
    int dy)
{
    ChkErr(HIShapeOffset, (HIMutableShapeRef) r, dx, dy);
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
    TkRegion dst,
    TkRegion src)
{
    ChkErr(HIShapeSetWithShape, (HIMutableShapeRef)dst, (HIShapeRef)src);
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSHIShapeDifferenceWithRect --
 *
 *	Wrapper functions for missing/buggy HIShape API
 *
 *----------------------------------------------------------------------
 */

OSStatus
TkMacOSHIShapeDifferenceWithRect(
    HIMutableShapeRef inShape,
    const CGRect *inRect)
{
    OSStatus result;
    HIShapeRef rgn = HIShapeCreateWithRect(inRect);

    result = HIShapeDifference(inShape, rgn, inShape);
    CFRelease(rgn);

    return result;
}

static OSStatus
rectCounter(
    TCL_UNUSED(int),
    TCL_UNUSED(HIShapeRef),
    TCL_UNUSED(const CGRect *),
    void *ref)
{
    int *count = (int *)ref;
    (*count)++;
    return noErr;
}

static OSStatus
rectPrinter(
    TCL_UNUSED(int),
    TCL_UNUSED(HIShapeRef),
    const CGRect *rect,
    TCL_UNUSED(void *))
{
    if (rect) {
	fprintf(stderr, "    %s\n", NSStringFromRect(NSRectFromCGRect(*rect)).UTF8String);
    }
    return noErr;
}

int
TkMacOSXCountRectsInRegion(
    HIShapeRef shape)
{
    int rect_count = 0;
    if (!HIShapeIsEmpty(shape)) {
	HIShapeEnumerate(shape,
	    kHIShapeParseFromBottom|kHIShapeParseFromLeft,
	    (HIShapeEnumerateProcPtr) rectCounter, (void *) &rect_count);
    }
    return rect_count;
}

void
TkMacOSXPrintRectsInRegion(
    HIShapeRef shape)
{
    if (!HIShapeIsEmpty(shape)) {
	HIShapeEnumerate( shape,
	    kHIShapeParseFromBottom|kHIShapeParseFromLeft,
	    (HIShapeEnumerateProcPtr) rectPrinter, NULL);
    }
}

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
