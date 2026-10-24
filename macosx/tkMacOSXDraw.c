/*
 * tkMacOSXDraw.c --
 *
 *	This file contains functions that draw to windows. Many of these
 *	functions emulate Xlib functions.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2006-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2014-2020 Marc Culler.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkMacOSXConstants.h"
#include "tkMacOSXDebug.h"
#include "tkButton.h"

#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101000
#define GET_CGCONTEXT [[NSGraphicsContext currentContext] CGContext]
#else
#define GET_CGCONTEXT [[NSGraphicsContext currentContext] graphicsPort]
#endif

/*
#ifdef TK_MAC_DEBUG
#define TK_MAC_DEBUG_DRAWING
#define TK_MAC_DEBUG_IMAGE_DRAWING
#define TK_MAC_DEBUG_CG
#endif
*/

#define radians(d)	((d) * (M_PI/180.0))

/*
 * Non-antialiased CG drawing looks better and more like X11 drawing when using
 * very fine lines, so decrease all linewidths by the following constant.
 */
#define NON_AA_CG_OFFSET .999

static int cgAntiAliasLimit = 0;
#define notAA(w)	((w) < cgAntiAliasLimit)

static int useThemedToplevel = 0;
static int useThemedFrame = 0;
static unsigned long transparentColor;

/*
 * Prototypes for functions used only in this file.
 */

static void ClipToGC(Drawable d, GC gc, HIShapeRef *clipRgnPtr);
static NSImage *CreateNSImageFromPixmap(Pixmap pixmap, int width, int height);
static HIShapeRef GetClipRgn(Drawable drawable);

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXInitCGDrawing --
 *
 *	Initializes link vars that control CG drawing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkMacOSXInitCGDrawing(
    Tcl_Interp *interp,
    TCL_UNUSED(int),
    int limit)
{
    static bool initialized = false;

    if (!initialized) {
	initialized = true;

	if (Tcl_CreateNamespace(interp, "::tk::mac", NULL, NULL) == NULL) {
	    Tcl_ResetResult(interp);
	}

	if (Tcl_LinkVar(interp, "::tk::mac::CGAntialiasLimit",
		&cgAntiAliasLimit, TCL_LINK_INT) != TCL_OK) {
	    Tcl_ResetResult(interp);
	}
	cgAntiAliasLimit = limit;

	/*
	 * Piggy-back the themed drawing var init here.
	 */

	if (Tcl_LinkVar(interp, "::tk::mac::useThemedToplevel",
		&useThemedToplevel, TCL_LINK_BOOLEAN) != TCL_OK) {
	    Tcl_ResetResult(interp);
	}
	if (Tcl_LinkVar(interp, "::tk::mac::useThemedFrame",
		&useThemedFrame, TCL_LINK_BOOLEAN) != TCL_OK) {
	    Tcl_ResetResult(interp);
	}
	transparentColor = TkMacOSXClearPixel();
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXGetNSImageFromTkImage --
 *
 *	Get autoreleased NSImage for Tk_Image.
 *
 * Results:
 *	NSImage.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

NSImage *
TkMacOSXGetNSImageFromTkImage(
    Display *display,
    Tk_Image image,
    int width,
    int height)
{
    Pixmap pixmap;
    NSImage *nsImage;
    if (width <= 0 || height <= 0) {
	return nsImage = [[NSImage alloc] initWithSize:NSMakeSize(0,0)];
    }
    pixmap = Tk_GetPixmap(display, None, width, height, 0);
    Tk_RedrawImage(image, 0, 0, width, height, pixmap, 0, 0);
    nsImage = CreateNSImageFromPixmap(pixmap, width, height);
    Tk_FreePixmap(display, pixmap);

    return [nsImage autorelease];
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXGetNSImageFromBitmap --
 *
 *	Get autoreleased NSImage for Bitmap.
 *
 * Results:
 *	NSImage.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

NSImage *
TkMacOSXGetNSImageFromBitmap(
    Display *display,
    Pixmap bitmap,
    GC gc,
    int width,
    int height)
{
    Pixmap pixmap = Tk_GetPixmap(display, None, width, height, 0);
    NSImage *nsImage;

    unsigned long origBackground = gc->background;

    gc->background = transparentColor;
    XSetClipOrigin(display, gc, 0, 0);
    XCopyPlane(display, bitmap, pixmap, gc, 0, 0, (unsigned)width, (unsigned)height, 0, 0, 1);
    gc->background = origBackground;
    nsImage = CreateNSImageFromPixmap(pixmap, width, height);
    Tk_FreePixmap(display, pixmap);

    return [nsImage autorelease];
}

/*
 *----------------------------------------------------------------------
 *
 * CreateNSImageFromPixmap --
 *
 *	Create NSImage for Pixmap.
 *
 * Results:
 *	NSImage.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static NSImage *
CreateNSImageFromPixmap(
    Pixmap pixmap,
    int width,
    int height)
{
    CGImageRef cgImage;
    NSImage *nsImage;
    NSBitmapImageRep *bitmapImageRep;
    CGContextRef context = TkMacOSXGetCGContextForDrawable(pixmap);

    if (context) {
	cgImage = CGBitmapContextCreateImage(context);
    } else {
	return NULL;
    }
    nsImage = [[NSImage alloc] initWithSize:NSMakeSize(width, height)];
    bitmapImageRep = [[NSBitmapImageRep alloc] initWithCGImage:cgImage];
    [nsImage addRepresentation:bitmapImageRep];
    [bitmapImageRep release];
    CFRelease(cgImage);

    return nsImage;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MacOSXGetCGContextForDrawable --
 *
 *	Get CGContext for given Drawable, creating one if necessary.
 *
 * Results:
 *	CGContext.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
Tk_MacOSXGetCGContextForDrawable(
    Drawable drawable)
{
    MacDrawable *macDraw = (MacDrawable *)drawable;

    if (macDraw && (macDraw->flags & TK_IS_PIXMAP) && !macDraw->context) {
	const size_t bitsPerComponent = 8;
	CGColorSpaceRef colorspace = NULL;
	CGBitmapInfo bitmapInfo =
#ifdef __LITTLE_ENDIAN__
		kCGBitmapByteOrder32Host;
#else
		kCGBitmapByteOrderDefault;
#endif
	CGRect bounds = CGRectMake(0, 0,
		macDraw->size.width, macDraw->size.height);

	if (macDraw->flags & TK_IS_BW_PIXMAP) {
	    bitmapInfo = (CGBitmapInfo)kCGImageAlphaOnly;
	} else {
	    colorspace = CGColorSpaceCreateDeviceRGB();
	    bitmapInfo |= kCGImageAlphaPremultipliedFirst;
	}
	macDraw->context = CGBitmapContextCreate(NULL, (unsigned)macDraw->size.width,
		(unsigned)macDraw->size.height, bitsPerComponent, 0,
		colorspace, bitmapInfo);
	if (macDraw->context) {
	    CGContextClearRect(macDraw->context, bounds);
	}
	if (colorspace) {
	    CFRelease(colorspace);
	}
    }

    return (macDraw ? macDraw->context : NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXDrawCGImage --
 *
 *	Draw CG image into drawable. The entire image is used, and will
 *	be rescaled if its dimensions do not equal dstBounds.size.
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
TkMacOSXDrawCGImage(
    Drawable d,
    GC gc,
    CGContextRef context,
    CGImageRef image,
    unsigned long imageForeground,
    unsigned long imageBackground,
    CGRect dstBounds)
{
    MacDrawable *macDraw = (MacDrawable *)d;

    if (macDraw && context && image) {
	dstBounds = CGRectOffset(dstBounds, macDraw->xOff, macDraw->yOff);
	if (CGImageIsMask(image)) {
	    if (macDraw->flags & TK_IS_BW_PIXMAP) {

		/*
		 * Set fill color to black; background comes from the context,
		 * or is transparent.
		 */

		if (imageBackground != transparentColor) {
		    CGContextClearRect(context, dstBounds);
		}
		CGContextSetRGBFillColor(context, 0.0, 0.0, 0.0, 1.0);
	    } else {
		if (imageBackground != transparentColor) {
		    TkMacOSXSetColorInContext(gc, imageBackground, context);
		    CGContextFillRect(context, dstBounds);
		}
		TkMacOSXSetColorInContext(gc, imageForeground, context);
	    }
	}

#ifdef TK_MAC_DEBUG_IMAGE_DRAWING
	CGContextSaveGState(context);
	CGContextSetLineWidth(context, 1.0);
	CGContextSetRGBStrokeColor(context, 0, 0, 0, 0.1);
	CGContextSetRGBFillColor(context, 0, 1, 0, 0.1);
	CGContextFillRect(context, dstBounds);
	CGContextStrokeRect(context, dstBounds);

	CGPoint p[4] = {dstBounds.origin,
	    CGPointMake(CGRectGetMaxX(dstBounds), CGRectGetMaxY(dstBounds)),
	    CGPointMake(CGRectGetMinX(dstBounds), CGRectGetMaxY(dstBounds)),
	    CGPointMake(CGRectGetMaxX(dstBounds), CGRectGetMinY(dstBounds))
	};

	CGContextStrokeLineSegments(context, p, 4);
	CGContextRestoreGState(context);
	TkMacOSXDbgMsg("Drawing CGImage at (x=%f, y=%f), (w=%f, h=%f)",
		dstBounds.origin.x, dstBounds.origin.y,
		dstBounds.size.width, dstBounds.size.height);
#else /* TK_MAC_DEBUG_IMAGE_DRAWING */
	CGContextSaveGState(context);
	CGContextTranslateCTM(context, 0, dstBounds.origin.y + CGRectGetMaxY(dstBounds));
	CGContextScaleCTM(context, 1, -1);
	CGContextDrawImage(context, dstBounds, image);
	CGContextRestoreGState(context);
#endif /* TK_MAC_DEBUG_IMAGE_DRAWING */
    } else {
	TkMacOSXDbgMsg("Drawing of empty CGImage requested");
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawLines --
 *
 *	Draw connected lines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders a series of connected lines.
 *
 *----------------------------------------------------------------------
 */

int
XDrawLines(
    Display *display,		/* Display. */
    Drawable d,			/* Draw on this. */
    GC gc,			/* Use this GC. */
    XPoint *points,		/* Array of points. */
    int npoints,		/* Number of points. */
    int mode)			/* Line drawing mode. */
{
    MacDrawable *macWin = (MacDrawable *)d;
    TkMacOSXDrawingContext dc;

    if (npoints < 2 || (mode != CoordModeOrigin && mode != CoordModePrevious)) {
	return BadValue;
    }

    LastKnownRequestProcessed(display)++;
    if (!TkMacOSXSetupDrawingContext(d, gc, &dc)) {
	return BadDrawable;
    }

    if (dc.context) {
	double o = (gc->line_width % 2) ? .5 : 0;
	short x0 = points[0].x, y0 = points[0].y;
	short x1 = (mode == CoordModeOrigin) ? points[1].x : x0 + points[1].x;
	short y1 = (mode == CoordModeOrigin) ? points[1].y : y0 + points[1].y;
	double prevx = macWin->xOff + x0 + o, prevy = macWin->yOff + y0 + o;
	int i;

	CGContextBeginPath(dc.context);
	if (y1 == y0) {					/* horiz. line */
	    if (x0 <= x1) {
		prevx = macWin->xOff + x0;
	    } else {
		prevx = macWin->xOff + x0 + 1;
	    }
	} else if (x1 == x0) {				/* vert. line */
	    if (y0 <= y1) {
		prevy = macWin->yOff + y0;
	    } else {
		prevy = macWin->yOff + y0 + 1;
	    }
	}
	CGContextMoveToPoint(dc.context, prevx, prevy);

	for (i = 1; i < npoints; i++) {
	    double nextx, nexty;

	    x1 = (mode == CoordModeOrigin) ? points[i].x : x0 + points[i].x;
	    y1 = (mode == CoordModeOrigin) ? points[i].y : y0 + points[i].y;
	    nextx = macWin->xOff + x1 + o;
	    nexty = macWin->yOff + y1 + o;

	    if (i == npoints-1) {
		if (y1 == y0) {				/* horiz. line */
		    if (x0 <= x1) {
			nextx = macWin->xOff + x1 + 1;
		    } else {
			nextx = macWin->xOff + x1;
		    }
		} else if (x1 == x0) {			/* vert. line */
		    if (y0 <= y1) {
			nexty = macWin->yOff + y1 + 1;
		    } else {
			nexty = macWin->yOff + y1;
		    }
		}
	    }
	    CGContextAddLineToPoint(dc.context, nextx, nexty);
	    x0 = x1; y0 = y1;
	}

	/*
	 * In the case of closed polylines, the first and last points are the
	 * same. We want miter or bevel join be rendered also at this point,
	 * this needs telling CoreGraphics that the path is closed.
	 */

	if (points[0].x == x1 && points[0].y == y1) {
	    CGContextClosePath(dc.context);
	}
	CGContextStrokePath(dc.context);
    }
    TkMacOSXRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawSegments --
 *
 *	Draw unconnected lines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders a series of unconnected lines.
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
    MacDrawable *macWin = (MacDrawable *)d;
    TkMacOSXDrawingContext dc;

    LastKnownRequestProcessed(display)++;
    if (!TkMacOSXSetupDrawingContext(d, gc, &dc)) {
	return BadDrawable;
    }

    if (dc.context) {
	int i;
	double o = (gc->line_width % 2) ? .5 : 0;

	for (i = 0; i < nsegments; i++) {
	    double prevx = macWin->xOff + segments[i].x1 + o;
	    double prevy = macWin->yOff + segments[i].y1 + o;
	    double nextx = macWin->xOff + segments[i].x2 + o;
	    double nexty = macWin->yOff + segments[i].y2 + o;

	    if (segments[i].y2 == segments[i].y1) {	      /* horiz. line */
		if (segments[i].x1 <= segments[i].x2) {
		    prevx = macWin->xOff + segments[i].x1;
		    nextx = macWin->xOff + segments[i].x2 + 1;
		} else {
		    prevx = macWin->xOff + segments[i].x1 + 1;
		    nextx = macWin->xOff + segments[i].x2;
		}
	    } else if (segments[i].x2 == segments[i].x1) {    /* vert. line */
		if (segments[i].y1 <= segments[i].y2) {
		    prevy = macWin->yOff + segments[i].y1;
		    nexty = macWin->yOff + segments[i].y2 + 1;
		} else {
		    prevy = macWin->yOff + segments[i].y1 + 1;
		    nexty = macWin->yOff + segments[i].y2;
		}
	    }

	    CGContextBeginPath(dc.context);
	    CGContextMoveToPoint(dc.context, prevx, prevy);
	    CGContextAddLineToPoint(dc.context, nextx, nexty);
	    CGContextStrokePath(dc.context);
	}
    }
    TkMacOSXRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XFillPolygon --
 *
 *	Draws a filled polygon.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws a filled polygon on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillPolygon(
    Display *display,		/* Display. */
    Drawable d,			/* Draw on this. */
    GC gc,			/* Use this GC. */
    XPoint *points,		/* Array of points. */
    int npoints,		/* Number of points. */
    TCL_UNUSED(int),		/* Shape to draw. */
    int mode)			/* Drawing mode. */
{
    MacDrawable *macWin = (MacDrawable *)d;
    TkMacOSXDrawingContext dc;

    if (npoints < 2 || (mode != CoordModeOrigin && mode != CoordModePrevious)) {
	return BadValue;
    }

    LastKnownRequestProcessed(display)++;
    if (!TkMacOSXSetupDrawingContext(d, gc, &dc)) {
	return BadDrawable;
    }

    if (dc.context) {
	double o = (gc->line_width % 2) ? .5 : 0;
	short x0 = points[0].x, y0 = points[0].y;
	short x1 = (mode == CoordModeOrigin) ? points[1].x : x0 + points[1].x;
	short y1 = (mode == CoordModeOrigin) ? points[1].y : y0 + points[1].y;
	double prevx = macWin->xOff + x0 + o, prevy = macWin->yOff + y0 + o;
	int i;

	CGContextBeginPath(dc.context);
	if (y1 == y0) {					/* horiz. line */
	    if (x0 <= x1) {
		prevx = macWin->xOff + x0;
	    } else {
		prevx = macWin->xOff + x0 + 1;
	    }
	} else if (x1 == x0) {				/* vert. line */
	    if (y0 <= y1) {
		prevy = macWin->yOff + y0;
	    } else {
		prevy = macWin->yOff + y0 + 1;
	    }
	}
	CGContextMoveToPoint(dc.context, prevx, prevy);

	for (i = 1; i < npoints; i++) {
	    double nextx, nexty;

	    x1 = (mode == CoordModeOrigin) ? points[i].x : x0 + points[i].x;
	    y1 = (mode == CoordModeOrigin) ? points[i].y : y0 + points[i].y;
	    nextx = macWin->xOff + x1 + o;
	    nexty = macWin->yOff + y1 + o;

	    if (i == npoints-1) {
		if (y1 == y0) {				/* horiz. line */
		    if (x0 <= x1) {
			nextx = macWin->xOff + x1 + 1;
		    } else {
			nextx = macWin->xOff + x1;
		    }
		} else if (x1 == x0) {			/* vert. line */
		    if (y0 <= y1) {
			nexty = macWin->yOff + y1 + 1;
		    } else {
			nexty = macWin->yOff + y1;
		    }
		}
	    }
	    CGContextAddLineToPoint(dc.context, nextx, nexty);
	    x0 = x1; y0 = y1;
	}

	(gc->fill_rule == EvenOddRule)
		? CGContextEOFillPath(dc.context)
		: CGContextFillPath(dc.context);
    }
    TkMacOSXRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawRectangle --
 *
 *	Draws a rectangle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws a rectangle on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawRectangle(
    Display *display,		/* Display. */
    Drawable d,			/* Draw on this. */
    GC gc,			/* Use this GC. */
    int x, int y,		/* Upper left corner. */
    unsigned int width,		/* Width & height of rect. */
    unsigned int height)
{
    MacDrawable *macWin = (MacDrawable *)d;
    TkMacOSXDrawingContext dc;
    int lw = gc->line_width;

    if (width == 0 || height == 0) {
	return BadDrawable;
    }

    LastKnownRequestProcessed(display)++;
    if (!TkMacOSXSetupDrawingContext(d, gc, &dc)) {
	return BadDrawable;
    }
    if (dc.context) {
	CGRect rect;
	double o = (lw % 2) ? .5 : 0;

	rect = CGRectMake(
		macWin->xOff + x + o, macWin->yOff + y + o,
		width, height);
	CGContextStrokeRect(dc.context, rect);
    }
    TkMacOSXRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawRectangles --
 *
 *	Draws the outlines of the specified rectangles as if a five-point
 *	PolyLine protocol request were specified for each rectangle:
 *
 *	    [x,y] [x+width,y] [x+width,y+height] [x,y+height] [x,y]
 *
 *	For the specified rectangles, these functions do not draw a pixel more
 *	than once. XDrawRectangles draws the rectangles in the order listed in
 *	the array. If rectangles intersect, the intersecting pixels are drawn
 *	multiple times. Draws a rectangle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws rectangles on the specified drawable.
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
    MacDrawable *macWin = (MacDrawable *)d;
    TkMacOSXDrawingContext dc;
    XRectangle * rectPtr;
    int i, lw = gc->line_width;

    LastKnownRequestProcessed(display)++;
    if (!TkMacOSXSetupDrawingContext(d, gc, &dc)) {
	return BadDrawable;
    }
    if (dc.context) {
	CGRect rect;
	double o = (lw % 2) ? .5 : 0;

	for (i = 0, rectPtr = rectArr; i < nRects; i++, rectPtr++) {
	    if (rectPtr->width == 0 || rectPtr->height == 0) {
		continue;
	    }
	    rect = CGRectMake(
		    macWin->xOff + rectPtr->x + o,
		    macWin->yOff + rectPtr->y + o,
		    rectPtr->width, rectPtr->height);
	    CGContextStrokeRect(dc.context, rect);
	}
    }
    TkMacOSXRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XFillRectangles --
 *
 *	Fill multiple rectangular areas in the given drawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws onto the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillRectangles(
    Display *display,		/* Display. */
    Drawable d,			/* Draw on this. */
    GC gc,			/* Use this GC. */
    XRectangle *rectangles,	/* Rectangle array. */
    int n_rectangles)		/* Number of rectangles. */
{
    MacDrawable *macWin = (MacDrawable *)d;
    TkMacOSXDrawingContext dc;
    XRectangle * rectPtr;
    int i;

    LastKnownRequestProcessed(display)++;
    if (!TkMacOSXSetupDrawingContext(d, gc, &dc)) {
	return BadDrawable;
    }
    if (dc.context) {
	CGRect rect;

	for (i = 0, rectPtr = rectangles; i < n_rectangles; i++, rectPtr++) {
	    if (rectPtr->width == 0 || rectPtr->height == 0) {
		continue;
	    }
	    rect = CGRectMake(
		    macWin->xOff + rectPtr->x,
		    macWin->yOff + rectPtr->y,
		    rectPtr->width, rectPtr->height);
	    CGContextFillRect(dc.context, rect);
	}
    }
    TkMacOSXRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXDrawSolidBorder --
 *
 *	Draws a border rectangle of specified thickness inside the bounding
 *      rectangle of a Tk Window.  The border rectangle can be inset within the
 *      bounding rectangle.  For a highlight border the inset should be 0, but
 *      for a solid border around the actual window the inset should equal the
 *      thickness of the highlight border.  The color of the border rectangle
 *      is the foreground color of the graphics context passed to the function.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws a rectangular border inside the bounding rectangle of a window.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkMacOSXDrawSolidBorder(
    Tk_Window tkwin,
    GC gc,
    int inset,
    int thickness)
{
    Drawable d = Tk_WindowId(tkwin);
    TkMacOSXDrawingContext dc;
    CGRect outerRect, innerRect;

    if (!TkMacOSXSetupDrawingContext(d, gc, &dc)) {
	return;
    }
    if (dc.context) {
	outerRect = CGRectMake(Tk_X(tkwin), Tk_Y(tkwin),
			       Tk_Width(tkwin), Tk_Height(tkwin));
	outerRect = CGRectInset(outerRect, inset, inset);
	innerRect = CGRectInset(outerRect, thickness, thickness);
	CGContextBeginPath(dc.context);
	CGContextAddRect(dc.context, outerRect);
	CGContextAddRect(dc.context, innerRect);
	CGContextEOFillPath(dc.context);
    }
    TkMacOSXRestoreDrawingContext(&dc);
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawArc --
 *
 *	Draw an arc.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws an arc on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawArc(
    Display *display,		/* Display. */
    Drawable d,			/* Draw on this. */
    GC gc,			/* Use this GC. */
    int x, int y,		/* Upper left of bounding rect. */
    unsigned int width,		/* Width & height. */
    unsigned int height,
    int angle1,			/* Staring angle of arc. */
    int angle2)			/* Extent of arc. */
{
    MacDrawable *macWin = (MacDrawable *)d;
    TkMacOSXDrawingContext dc;
    int lw = gc->line_width;

    if (width == 0 || height == 0 || angle2 == 0) {
	return BadDrawable;
    }

    LastKnownRequestProcessed(display)++;
    if (!TkMacOSXSetupDrawingContext(d, gc, &dc)) {
	return BadDrawable;
    }
    if (dc.context) {
	CGRect rect;
	double o = (lw % 2) ? .5 : 0;

	rect = CGRectMake(
		macWin->xOff + x + o,
		macWin->yOff + y + o,
		width, height);
	if (angle1 == 0 && angle2 == 23040) {
	    CGContextStrokeEllipseInRect(dc.context, rect);
	} else {
	    CGMutablePathRef p = CGPathCreateMutable();
	    CGAffineTransform t = CGAffineTransformIdentity;
	    CGPoint c = CGPointMake(CGRectGetMidX(rect), CGRectGetMidY(rect));
	    double w = CGRectGetWidth(rect);

	    if (width != height) {
		t = CGAffineTransformMakeScale(1.0, CGRectGetHeight(rect)/w);
		c = CGPointApplyAffineTransform(c, CGAffineTransformInvert(t));
	    }
	    CGPathAddArc(p, &t, c.x, c.y, w/2, radians(-angle1/64.0),
		    radians(-(angle1 + angle2)/64.0), angle2 > 0);
	    CGContextAddPath(dc.context, p);
	    CGPathRelease(p);
	    CGContextStrokePath(dc.context);
	}
    }
    TkMacOSXRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawArcs --
 *
 *	Draws multiple circular or elliptical arcs. Each arc is specified by a
 *	rectangle and two angles. The center of the circle or ellipse is the
 *	center of the rect- angle, and the major and minor axes are specified
 *	by the width and height.  Positive angles indicate counterclock- wise
 *	motion, and negative angles indicate clockwise motion. If the magnitude
 *	of angle2 is greater than 360 degrees, XDrawArcs truncates it to 360
 *	degrees.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws an arc for each array element on the specified drawable.
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
    MacDrawable *macWin = (MacDrawable *)d;
    TkMacOSXDrawingContext dc;
    XArc *arcPtr;
    int i, lw = gc->line_width;

    LastKnownRequestProcessed(display)++;
    if (!TkMacOSXSetupDrawingContext(d, gc, &dc)) {
	return BadDrawable;
    }
    if (dc.context) {
	CGRect rect;
	double o = (lw % 2) ? .5 : 0;

	for (i=0, arcPtr = arcArr; i < nArcs; i++, arcPtr++) {
	    if (arcPtr->width == 0 || arcPtr->height == 0
		    || arcPtr->angle2 == 0) {
		continue;
	    }
	    rect = CGRectMake(
		    macWin->xOff + arcPtr->x + o,
		    macWin->yOff + arcPtr->y + o,
		    arcPtr->width, arcPtr->height);

	    if (arcPtr->angle1 == 0 && arcPtr->angle2 == 23040) {
		CGContextStrokeEllipseInRect(dc.context, rect);
	    } else {
		CGMutablePathRef p = CGPathCreateMutable();
		CGAffineTransform t = CGAffineTransformIdentity;
		CGPoint c = CGPointMake(CGRectGetMidX(rect),
			CGRectGetMidY(rect));
		double w = CGRectGetWidth(rect);

		if (arcPtr->width != arcPtr->height) {
		    t = CGAffineTransformMakeScale(1, CGRectGetHeight(rect)/w);
		    c = CGPointApplyAffineTransform(c,
			    CGAffineTransformInvert(t));
		}
		CGPathAddArc(p, &t, c.x, c.y, w/2,
			radians(-arcPtr->angle1/64.0),
			radians(-(arcPtr->angle1 + arcPtr->angle2)/64.0),
			arcPtr->angle2 > 0);
		CGContextAddPath(dc.context, p);
		CGPathRelease(p);
		CGContextStrokePath(dc.context);
	    }
	}
    }
    TkMacOSXRestoreDrawingContext(&dc);
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
 *	None.
 *
 * Side effects:
 *	Draws a filled arc on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillArc(
    Display *display,		/* Display. */
    Drawable d,			/* Draw on this. */
    GC gc,			/* Use this GC. */
    int x, int y,		/* Upper left of bounding rect. */
    unsigned int width,		/* Width & height. */
    unsigned int height,
    int angle1,			/* Staring angle of arc. */
    int angle2)			/* Extent of arc. */
{
    MacDrawable *macWin = (MacDrawable *)d;
    TkMacOSXDrawingContext dc;
    int lw = gc->line_width;

    if (width == 0 || height == 0 || angle2 == 0) {
	return BadDrawable;
    }

    LastKnownRequestProcessed(display)++;
    if (!TkMacOSXSetupDrawingContext(d, gc, &dc)) {
	return BadDrawable;
    }
    if (dc.context) {
	CGRect rect;
	double o = (lw % 2) ? .5 : 0, u = 0;

	if (notAA(lw)) {
	    o += NON_AA_CG_OFFSET/2;
	    u += NON_AA_CG_OFFSET;
	}
	rect = CGRectMake(
		macWin->xOff + x + o,
		macWin->yOff + y + o,
		width - u, height - u);

	if (angle1 == 0 && angle2 == 23040) {
	    CGContextFillEllipseInRect(dc.context, rect);
	} else {
	    CGMutablePathRef p = CGPathCreateMutable();
	    CGAffineTransform t = CGAffineTransformIdentity;
	    CGPoint c = CGPointMake(CGRectGetMidX(rect), CGRectGetMidY(rect));
	    double w = CGRectGetWidth(rect);

	    if (width != height) {
		t = CGAffineTransformMakeScale(1, CGRectGetHeight(rect)/w);
		c = CGPointApplyAffineTransform(c, CGAffineTransformInvert(t));
	    }
	    if (gc->arc_mode == ArcPieSlice) {
		CGPathMoveToPoint(p, &t, c.x, c.y);
	    }
	    CGPathAddArc(p, &t, c.x, c.y, w/2, radians(-angle1/64.0),
		    radians(-(angle1 + angle2)/64.0), angle2 > 0);
	    CGPathCloseSubpath(p);
	    CGContextAddPath(dc.context, p);
	    CGPathRelease(p);
	    CGContextFillPath(dc.context);
	}
    }
    TkMacOSXRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XFillArcs --
 *
 *	Draw a filled arc.
 *
 * Results:
 *	None.
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
    MacDrawable *macWin = (MacDrawable *)d;
    TkMacOSXDrawingContext dc;
    XArc * arcPtr;
    int i, lw = gc->line_width;

    LastKnownRequestProcessed(display)++;
    if (!TkMacOSXSetupDrawingContext(d, gc, &dc)) {
	return BadDrawable;
    }
    if (dc.context) {
	CGRect rect;
	double o = (lw % 2) ? .5 : 0, u = 0;

	if (notAA(lw)) {
	    o += NON_AA_CG_OFFSET/2;
	    u += NON_AA_CG_OFFSET;
	}
	for (i = 0, arcPtr = arcArr; i < nArcs; i++, arcPtr++) {
	    if (arcPtr->width == 0 || arcPtr->height == 0
		    || arcPtr->angle2 == 0) {
		continue;
	    }
	    rect = CGRectMake(
		    macWin->xOff + arcPtr->x + o,
		    macWin->yOff + arcPtr->y + o,
		    arcPtr->width - u, arcPtr->height - u);
	    if (arcPtr->angle1 == 0 && arcPtr->angle2 == 23040) {
		CGContextFillEllipseInRect(dc.context, rect);
	    } else {
		CGMutablePathRef p = CGPathCreateMutable();
		CGAffineTransform t = CGAffineTransformIdentity;
		CGPoint c = CGPointMake(CGRectGetMidX(rect),
			CGRectGetMidY(rect));
		double w = CGRectGetWidth(rect);

		if (arcPtr->width != arcPtr->height) {
		    t = CGAffineTransformMakeScale(1, CGRectGetHeight(rect)/w);
		    c = CGPointApplyAffineTransform(c,
			    CGAffineTransformInvert(t));
		}
		if (gc->arc_mode == ArcPieSlice) {
		    CGPathMoveToPoint(p, &t, c.x, c.y);
		}
		CGPathAddArc(p, &t, c.x, c.y, w/2,
			radians(-arcPtr->angle1/64.0),
			radians(-(arcPtr->angle1 + arcPtr->angle2)/64.0),
			arcPtr->angle2 > 0);
		CGPathCloseSubpath(p);
		CGContextAddPath(dc.context, p);
		CGPathRelease(p);
		CGContextFillPath(dc.context);
	    }
	}
    }
    TkMacOSXRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXSetUpGraphicsPort --
 *
 *	Set up the graphics port from the given GC.
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
TkMacOSXSetUpGraphicsPort(
    TCL_UNUSED(GC),			/* GC to apply to current port. */
    TCL_UNUSED(void *))
{
    Tcl_Panic("TkMacOSXSetUpGraphicsPort: Obsolete, no more QD!");
}


/*
 *----------------------------------------------------------------------
 *
 * Stipple support (FillStippled / FillOpaqueStippled) --
 *
 *	An X stipple is a 1-bit-deep Pixmap tiled over the drawable with its
 *	origin at (ts_x_origin, ts_y_origin).  We emulate it with a *colored*
 *	CGPattern whose cell is drawn by StippleDrawCell(): the cell is filled
 *	with the GC background (opaque case only) and then the stipple, turned
 *	into a CGImage mask, is painted in the GC foreground.  Installing the
 *	pattern as both the fill and stroke "color" of the context means that
 *	every fill, stroke and text-fill operation in the context honors the
 *	stipple without any change to the individual X drawing functions.
 *
 *	CoreGraphics owns the lifetime of everything allocated here: the
 *	CGColor created with the pattern is retained by the context's GState,
 *	so StippleReleaseInfo() runs when TkMacOSXRestoreDrawingContext() pops
 *	that GState.
 *
 *----------------------------------------------------------------------
 */

typedef struct {
    CGImageRef mask;		/* Image mask made from the stipple bitmap. */
    CGColorRef fg;		/* Foreground color. */
    CGColorRef bg;		/* Background color; NULL for FillStippled. */
    CGSize size;		/* Size of one stipple tile. */
} StippleInfo;

/*
 *----------------------------------------------------------------------
 *
 * StippleDrawCell --
 *
 *	CGPattern cell-drawing callback.  Paints one tile of the stipple
 *	pattern: first the background color (only when an opaque stipple
 *	is in use), then the stipple mask image in the foreground color.
 *	The mask is drawn with interpolation disabled so that stipple
 *	pixels stay crisp.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws into the supplied CGContext, which is owned by CoreGraphics
 *	and is only valid for the duration of the call.
 *
 *----------------------------------------------------------------------
 */

static void
StippleDrawCell(
    void *clientData,
    CGContextRef context)
{
    StippleInfo *info = (StippleInfo *)clientData;
    CGRect cell = CGRectMake(0, 0, info->size.width, info->size.height);

    CGContextSetInterpolationQuality(context, kCGInterpolationNone);
    if (info->bg) {
	CGContextSetFillColorWithColor(context, info->bg);
	CGContextFillRect(context, cell);
    }
    CGContextSetFillColorWithColor(context, info->fg);
    CGContextDrawImage(context, cell, info->mask);
}

/*
 *----------------------------------------------------------------------
 *
 * StippleReleaseInfo --
 *
 *	CGPattern release callback.  Frees the StippleInfo structure and
 *	releases the CGImage mask and CGColor objects it owns.  This is
 *	called by CoreGraphics when the last reference to the CGPattern
 *	is dropped, typically when the GState that holds the pattern
 *	color is restored by TkMacOSXRestoreDrawingContext().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory; the clientData must not be used afterwards.
 *
 *----------------------------------------------------------------------
 */

static void
StippleReleaseInfo(
    void *clientData)
{
    StippleInfo *info = (StippleInfo *)clientData;

    CGImageRelease(info->mask);
    CGColorRelease(info->fg);
    CGColorRelease(info->bg);
    ckfree(info);
}

/*
 *----------------------------------------------------------------------
 *
 * CreateStippleMask --
 *
 *	Build an 8-bit CGImage mask from a Tk 1-bit (BW) pixmap.  Tk's
 *	BW pixmaps are alpha-only bitmap contexts in which a set bit has
 *	alpha 255.  Image mask samples paint where the sample is 0, so an
 *	inverted decode array is supplied.  The pixel data is copied out
 *	of the bitmap context, so the returned mask does not depend on
 *	the lifetime of the pixmap.
 *
 * Results:
 *	Returns a newly created CGImage mask, or NULL if the pixmap is
 *	not a BW pixmap or has no usable backing store.  The caller owns
 *	a reference to the returned image and must release it.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/*
 * Build an 8-bit image mask from a Tk bitmap.  Tk's BW pixmaps are
 * alpha-only bitmap contexts in which a set bit has alpha 255.  Image mask
 * samples paint where the sample is 0, so use an inverted decode array.  The
 * pixel data is copied, so the mask does not depend on the pixmap's lifetime.
 */

static CGImageRef
CreateStippleMask(
    Pixmap stipple)
{
    MacDrawable *macStipple = (MacDrawable *)stipple;
    CGContextRef bc;
    CGDataProviderRef provider;
    CFDataRef copy;
    CGImageRef mask;
    const void *data;
    size_t w, h, bytesPerRow;
    static const CGFloat decode[2] = {1.0, 0.0};

    if (!macStipple || !(macStipple->flags & TK_IS_PIXMAP)
	    || !(macStipple->flags & TK_IS_BW_PIXMAP)) {
	return NULL;
    }
    bc = TkMacOSXGetCGContextForDrawable(stipple);
    if (!bc) {
	return NULL;
    }
    w = CGBitmapContextGetWidth(bc);
    h = CGBitmapContextGetHeight(bc);
    bytesPerRow = CGBitmapContextGetBytesPerRow(bc);
    data = CGBitmapContextGetData(bc);
    if (!data || !w || !h) {
	return NULL;
    }
    copy = CFDataCreate(NULL, (const UInt8 *)data, (CFIndex)(bytesPerRow * h));
    provider = CGDataProviderCreateWithCFData(copy);
    CFRelease(copy);
    mask = CGImageMaskCreate(w, h, 8, 8, bytesPerRow, provider, decode, false);
    CGDataProviderRelease(provider);
    return mask;
}

/*
 *----------------------------------------------------------------------
 *
 * SetStipplePattern --
 *
 *	Replace the fill and stroke colors of the supplied CGContext
 *	with a colored CGPattern that emulates the X stipple described by
 *	the GC.  Each pattern cell is drawn by StippleDrawCell(), which
 *	fills with the GC background (opaque stipples only) and then
 *	paints the stipple mask in the GC foreground.  Because the
 *	pattern is installed as both the fill and stroke color, every
 *	subsequent fill, stroke and text-fill operation in the context
 *	honors the stipple without changes to the individual X drawing
 *	functions.
 *
 *	The pattern matrix is scaled by the current CTM so that pattern
 *	tiles line up with device pixels on Retina displays, and the
 *	pattern phase is set so that the X tile origin
 *	(ts_x_origin, ts_y_origin) lands on a tile corner.
 *
 *	If the GC has no stipple, if the stipple cannot be converted to a
 *	mask, or if the foreground or background color is itself a
 *	pattern-valued color (which cannot be nested inside a pattern
 *	cell), the solid foreground color already installed by the
 *	caller is left in place.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The context's fill and stroke colors are replaced by a pattern
 *	color.  Memory allocated here is owned by CoreGraphics and is
 *	freed by StippleReleaseInfo() when the enclosing GState is
 *	restored.
 *
 *----------------------------------------------------------------------
 */

/*
 * Replace the fill and stroke colors of the context by the stipple pattern.
 * If anything goes wrong the solid foreground color that the caller already
 * installed is left in place.
 */

static void
SetStipplePattern(
    MacDrawable *macDraw,
    GC gc,
    CGContextRef context,
    BOOL useDarkAppearance)
{
    static const CGPatternCallbacks callbacks = {
	0, StippleDrawCell, StippleReleaseInfo
    };
    CGFloat alpha = 1.0, scale;
    CGAffineTransform ctm;
    CGPoint origin;
    StippleInfo *info;
    CGPatternRef pattern;
    CGColorSpaceRef patternSpace;
    CGColorRef patternColor;
    CGImageRef mask;
    CGColorRef fg = NULL, bg = NULL;

    if (!gc->stipple || !(mask = CreateStippleMask(gc->stipple))) {
	return;
    }
    TkSetMacColor2(gc->foreground, &fg, useDarkAppearance);
    if (gc->fill_style == FillOpaqueStippled) {
	TkSetMacColor2(gc->background, &bg, useDarkAppearance);
    }

    /*
     * Pattern-valued colors (possible with some HITheme brushes) can't be
     * nested inside a pattern cell; fall back to the solid color.
     */

    if (!fg || CGColorGetPattern(fg) || (bg && CGColorGetPattern(bg))) {
	CGImageRelease(mask);
	CGColorRelease(fg);
	CGColorRelease(bg);
	return;
    }

    info = (StippleInfo *)ckalloc(sizeof(StippleInfo));
    info->mask = mask;
    info->fg = fg;
    info->bg = bg;
    info->size = CGSizeMake((CGFloat)CGImageGetWidth(mask),
	    (CGFloat)CGImageGetHeight(mask));

    /*
     * A pattern lives in the context's *default* space, which is not affected
     * by the scale and flip that were concatenated to the CTM.  Use the
     * current CTM to find the number of device pixels per Tk pixel (2 on a
     * Retina display) and scale the pattern matrix by it; otherwise each
     * stipple pixel shrinks to one device pixel and the pattern turns into a
     * fine gray mesh.
     */

    ctm = CGContextGetCTM(context);
    scale = sqrt(ctm.a * ctm.a + ctm.b * ctm.b);
    if (scale <= 0.0) {
	scale = 1.0;
    }

    pattern = CGPatternCreate(info, CGRectMake(0, 0, info->size.width,
	    info->size.height), CGAffineTransformMakeScale(scale, scale),
	    info->size.width,
	    info->size.height, kCGPatternTilingConstantSpacing,
	    true /* colored */, &callbacks);
    if (!pattern) {
	StippleReleaseInfo(info);
	return;
    }
    patternSpace = CGColorSpaceCreatePattern(NULL);
    patternColor = CGColorCreateWithPattern(patternSpace, pattern, &alpha);
    CGContextSetFillColorWithColor(context, patternColor);
    CGContextSetStrokeColorWithColor(context, patternColor);

    /*
     * The X tile origin is the top left corner of a tile in y-down Tk
     * coordinates, i.e. the bottom left corner (y + height) of an upright
     * tile in the flipped user space.  Map it through the CTM to get the
     * position of the tile in default (device) space, which is what the
     * pattern phase is expressed in.
     */

    origin = CGPointApplyAffineTransform(CGPointMake(
	    macDraw->xOff + gc->ts_x_origin,
	    macDraw->yOff + gc->ts_y_origin + info->size.height), ctm);
    CGContextSetPatternPhase(context, CGSizeMake(origin.x, origin.y));

    CGColorRelease(patternColor);
    CGColorSpaceRelease(patternSpace);
    CGPatternRelease(pattern);
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXSetUpDrawingContext --
 *
 *	Set up a drawing context for the given drawable from an X GC.
 *
 * Results:
 *	Boolean indicating whether it is ok to draw; if false, the drawing
 *	context was not setup, so do not attempt to draw and do not call
 *	TkMacOSXRestoreDrawingContext().
 *
 * Side effects:
 *	May modify or create the drawable's graphics context.  May expand the
 *      drawable's dirty rectangle.  When the result is true The dcPtr
 *      parameter is set to reference the new or updated drawing context.
 *
 *----------------------------------------------------------------------
 */

bool
TkMacOSXSetupDrawingContext(
    Drawable d,
    GC gc,
    TkMacOSXDrawingContext *dcPtr)
{
    MacDrawable *macDraw = (MacDrawable *)d;
    bool canDraw = true;
    TKContentView *view = nil;
    TkMacOSXDrawingContext dc = {};
    CGFloat drawingHeight;

#ifdef TK_MAC_DEBUG_CG
    fprintf(stderr, "TkMacOSXSetupDrawingContext: %s\n",
	    macDraw->winPtr ? Tk_PathName(macDraw->winPtr) : "None");
#endif

    /*
     * If the drawable is not a pixmap, get the associated NSView.
     */

    if (!(macDraw->flags & TK_IS_PIXMAP)) {
	view = (TKContentView *)TkMacOSXGetNSViewForDrawable(d);
	if (!view) {
	    Tcl_Panic("TkMacOSXSetupDrawingContext(): "
		    "no NSView to draw into !");
	}
    }

    /*
     * Intersect the drawable's clipping region with the region stored in the
     * X GC.  If the resulting region is empty, don't do any drawing.
     */
//#if 0 // disable clipping (almost works, but windows can open up blank)
    dc.clipRgn = GetClipRgn(d);
    ClipToGC(d, gc, &dc.clipRgn);
    if (dc.clipRgn && HIShapeIsEmpty(dc.clipRgn)) {
	/*
	 * Things are probably not set up for drawing yet.  Request a call to
	 * updateLayer and return failure.
	 */
	canDraw = false;
	goto end;
    }
//#endif //disable clipping
    /*
     * If the drawable already has a CGContext, use it.  Otherwise, we must be
     * drawing to a window and we use the current context of its ContentView.
     */

    dc.context = TkMacOSXGetCGContextForDrawable(d);
    if (!dc.context) {
	dc.view = view;
	dc.context = view.tkLayerBitmapContext;
	if (dc.clipRgn) {
	    CGRect clipBounds;
	    CGAffineTransform t = { .a = 1, .b = 0, .c = 0, .d = -1, .tx = 0,
				    .ty = [view bounds].size.height};
	    HIShapeGetBounds(dc.clipRgn, &clipBounds);
	    clipBounds = CGRectApplyAffineTransform(clipBounds, t);
	}

	/*
	 * Workaround for an Apple bug.
	 *
	 * Without the block below, ttk frames, labelframes and labels do not
	 * get the correct background color on macOS 12.5 after the appearance
	 * changes.  This function is only called when drawing, so we know that
	 * our view is the focus view. Even though the effective appearance of
	 * the view has been changed, the currentAppearance, i.e. the
	 * appearance that will be used for drawing, may not have been changed
	 * to match.
	 *
	 * Prior to macOS 12.0 the currentAppearance property of NSAppearance
	 * was settable.  In macOS 12.0 currentAppearance was deprecated and
	 * replaced by the read-only property currentDrawingAppearance.  The
	 * ttk color issues are fixed by setting the currentAppearance to
	 * the effectiveAppearance of the view.  So we are forced to use this
	 * deprecated function until Apple fixes this.
	 *
	 * It is a mystery why this only affects the ttk widgets.  A possible
	 * clue is that when drawing a ttk widget this function is called with
	 * a NULL gc, whereas the gc is non-null when it is called for drawing
	 * a Tk widget.  This means that the CGContext setup below is not done
	 * for ttk widgets.  Perhaps that setup triggers an update of the
	 * currentAppearance property, but that has not been verified.
	 */

	if (@available(macOS 12.0, *)) {
#if MAC_OS_X_VERSION_MAX_ALLOWED > 120000
	    NSAppearance *current = NSAppearance.currentDrawingAppearance;
	    NSAppearance *effective = view.effectiveAppearance;
	    if( current != effective) {
		// printf("Appearances are out of sync!\n");
		// Deprecations be damned!
		NSAppearance.currentAppearance = effective;
	    }
#endif
	} else {
	    /*
	     *It is not clear if this is a problem before macos 12.0, but
	     * we might as well do the update anyway.
	     */

#if MAC_OS_X_VERSION_MIN_REQUIRED < 120000
/* currentAppearance is not deprecated. */
	    NSAppearance.currentAppearance = view.effectiveAppearance;
#endif
	}
    }

    /*
     * Finish configuring the drawing context.
     */

#ifdef TK_MAC_DEBUG_CG
    fprintf(stderr, "TkMacOSXSetupDrawingContext: pushing GState for %s\n",
	    macDraw->winPtr ? Tk_PathName(macDraw->winPtr) : "None");
#endif

    CGContextSaveGState(dc.context);
    CGContextSetTextDrawingMode(dc.context, kCGTextFill);
    { /* Restricted scope for t needed for C++ */
	drawingHeight = view ? [view bounds].size.height :
	    CGContextGetClipBoundingBox(dc.context).size.height;
	CGAffineTransform t = {
	    .a = 1, .b = 0,
	    .c = 0, .d = -1,
	    .tx = 0,
	    .ty = drawingHeight
	};
	CGContextConcatCTM(dc.context, t);
    }
//#if 0    // disable clipping
    if (dc.clipRgn) {

#ifdef TK_MAC_DEBUG_DRAWING
	CGContextSaveGState(dc.context);
	ChkErr(HIShapeReplacePathInCGContext, dc.clipRgn, dc.context);
	CGContextSetRGBFillColor(dc.context, 1.0, 0.0, 0.0, 0.1);
	CGContextEOFillPath(dc.context);
	CGContextRestoreGState(dc.context);
#endif /* TK_MAC_DEBUG_DRAWING */

	if (!HIShapeIsRectangular(dc.clipRgn)) {

	    /*
	     * We expect the clipping path dc.clipRgn to consist of the
	     * bounding rectangle of the drawable window, together with
	     * disjoint smaller rectangles inside of it which bound its
	     * geometric children.  In that case the even-odd rule will
	     * clip to the region inside the large rectangle and outside
	     * of the smaller rectangles.
	     */

	    ChkErr(HIShapeReplacePathInCGContext, dc.clipRgn, dc.context);

#ifdef TK_MAC_DEBUG_CG
	    fprintf(stderr, "Setting complex clip for %s to:\n",
		    macDraw->winPtr ? Tk_PathName(macDraw->winPtr) : "None");
	    TkMacOSXPrintRectsInRegion(dc.clipRgn);
#endif

	    CGContextEOClip(dc.context);
	} else {
	    CGRect r;
	    HIShapeGetBounds(dc.clipRgn, &r);

#ifdef TK_MAC_DEBUG_CG
	    fprintf(stderr, "Current clip BBox is %s\n",
		    NSStringFromRect(CGContextGetClipBoundingBox(GET_CGCONTEXT)).UTF8String);
	    fprintf(stderr, "Setting clip for %s to rect %s:\n",
		    macDraw->winPtr ? Tk_PathName(macDraw->winPtr) : "None",
		    NSStringFromRect(r).UTF8String);
#endif

	    CGContextClipToRect(dc.context, r);
	}
    }
//#endif //disable clipping

    if (gc) {
	static const CGLineCap cgCap[] = {
	    [CapNotLast] = kCGLineCapButt,
	    [CapButt] = kCGLineCapButt,
	    [CapRound] = kCGLineCapRound,
	    [CapProjecting] = kCGLineCapSquare,
	};
	static const CGLineJoin cgJoin[] = {
	    [JoinMiter] = kCGLineJoinMiter,
	    [JoinRound] = kCGLineJoinRound,
	    [JoinBevel] = kCGLineJoinBevel,
	};
	bool shouldAntialias = !notAA(gc->line_width);
	double w = gc->line_width;

	TkMacOSXSetColorInContext(gc, gc->foreground, dc.context);
	if (view) {
	    CGSize size = NSSizeToCGSize([view bounds].size);
	    CGContextSetPatternPhase(dc.context, size);
	}
	if (gc->fill_style == FillStippled
		|| gc->fill_style == FillOpaqueStippled) {
	    SetStipplePattern(macDraw, gc, dc.context,
		    TkMacOSXInDarkMode((Tk_Window)macDraw->winPtr));
	}
	if (gc->function != GXcopy) {
	    TkMacOSXDbgMsg("Logical functions other than GXcopy are "
			   "not supported for CG drawing!");
	}
	if (!shouldAntialias) {

	    /*
	     * Make non-antialiased CG drawing look more like X11.
	     */

	    w -= (gc->line_width ? NON_AA_CG_OFFSET : 0);
	}
	CGContextSetShouldAntialias(dc.context, shouldAntialias);
	CGContextSetLineWidth(dc.context, w);
	if (gc->line_style != LineSolid) {
	    size_t num = 0;
	    char *p = &gc->dashes;
	    CGFloat dashOffset = gc->dash_offset;
	    dashOffset -= (gc->line_width % 2) ? 0.5 : 0.0;
	    CGFloat lengths[10];

	    while (p[num] != '\0' && num < 10) {
		lengths[num] = p[num];
		num++;
	    }
	    CGContextSetLineDash(dc.context, dashOffset, lengths, num);
	}
	if ((unsigned) gc->cap_style < sizeof(cgCap)/sizeof(CGLineCap)) {
	    CGContextSetLineCap(dc.context, cgCap[(unsigned) gc->cap_style]);
	}
	if ((unsigned)gc->join_style < sizeof(cgJoin)/sizeof(CGLineJoin)) {
	    CGContextSetLineJoin(dc.context, cgJoin[(unsigned) gc->join_style]);
	}
    }

end:

#ifdef TK_MAC_DEBUG_DRAWING
    if (!canDraw && (macDraw->winPtr != NULL)) {
	fprintf(stderr, "Cannot draw in %s - postponing.\n",
		Tk_PathName(macDraw->winPtr));
    }
#endif

    if (!canDraw && dc.clipRgn) {
	CFRelease(dc.clipRgn);
	dc.clipRgn = NULL;
    }
    *dcPtr = dc;
    return canDraw;
}

/*
 * Idle task to schedule a call to updateLayer so the results of drawing
 * operations become visible. The call to nextEventMatchingMask has no effect,
 * but it provides an opportunity for the window manager to call updateLayer.
 */

MODULE_SCOPE void
TkMacOSXUpdateViewIdleTask(void *clientData) {
    NSView *view = (NSView *) clientData;
    [view setNeedsDisplay:YES];
    [NSApp nextEventMatchingMask:NSAnyEventMask
		       untilDate:[NSDate distantPast]
			  inMode:NSDefaultRunLoopMode
			 dequeue:NO];
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXRestoreDrawingContext --
 *
 *	Restore drawing context.
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
TkMacOSXRestoreDrawingContext(
    TkMacOSXDrawingContext *dcPtr)
{
    if (dcPtr->context) {
	CGContextSynchronize(dcPtr->context);
	CGContextRestoreGState(dcPtr->context);

#ifdef TK_MAC_DEBUG_CG
	fprintf(stderr, "TkMacOSXRestoreDrawingContext: popped GState\n");
#endif

    }
    if (dcPtr->clipRgn) {
	CFRelease(dcPtr->clipRgn);
	dcPtr->clipRgn = NULL;
    }

    /*
     * Schedule a call to updateLayer, since we have drawn on the view's
     * backing layer.
     */

    Tcl_CancelIdleCall(TkMacOSXUpdateViewIdleTask, dcPtr->view);
    Tcl_DoWhenIdle(TkMacOSXUpdateViewIdleTask, dcPtr->view);

#ifdef TK_MAC_DEBUG
    bzero(dcPtr, sizeof(TkMacOSXDrawingContext));
#endif

}

/*
 *----------------------------------------------------------------------
 *
 * GetClipRgn --
 *
 *	Get the clipping region needed to restrict drawing to the given
 *	drawable.
 *
 * Results:
 *	Clipping region. If non-NULL, CFRelease it when done.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

HIShapeRef
GetClipRgn(
    Drawable drawable)		/* Drawable. */
{
    MacDrawable *macDraw = (MacDrawable *)drawable;
    HIShapeRef clipRgn = NULL;

    if (macDraw->winPtr && macDraw->flags & TK_CLIP_INVALID) {
	TkMacOSXUpdateClipRgn(macDraw->winPtr);
#ifdef TK_MAC_DEBUG_DRAWING
	TkMacOSXDbgMsg("%s", macDraw->winPtr->pathName);

	NSView *view = TkMacOSXGetNSViewForDrawable(macDraw);
	CGContextRef context = GET_CGCONTEXT;

	CGContextSaveGState(context);
	CGContextConcatCTM(context, CGAffineTransformMake(1.0, 0.0, 0.0,
	      -1.0, 0.0, [view bounds].size.height));
	ChkErr(HIShapeReplacePathInCGContext, macDraw->visRgn, context);
	CGContextSetRGBFillColor(context, 0.0, 1.0, 0.0, 0.1);
	CGContextEOFillPath(context);
	CGContextRestoreGState(context);
#endif /* TK_MAC_DEBUG_DRAWING */
    }

    if (macDraw->drawRgn) {
	// The drawRgn is the visRgn intersected with a rectangle which
	// may be smaller than the widget bounds.
	clipRgn = HIShapeCreateCopy(macDraw->drawRgn);
    } else if (macDraw->visRgn) {
	clipRgn = HIShapeCreateCopy(macDraw->visRgn);
    }
    // A NULL clipRgn does not allow any drawing at all.
    return clipRgn;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_ClipDrawableToRect --
 *
 *	Clip all drawing into the drawable d to the given rectangle. If width
 *	or height are negative, reset to no clipping. This is called by the
 *	Text widget to display each DLine, and by the Canvas widget when it
 *	is updating a sub rectangle in the canvas.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Subsequent drawing into d is offset and clipped as specified.
 *
 *----------------------------------------------------------------------
 */

void
Tk_ClipDrawableToRect(
    TCL_UNUSED(Display *),
    Drawable d,
    int x, int y,
    int width, int height)
{
    MacDrawable *macDraw = (MacDrawable *)d;

    if (macDraw->drawRgn) {
	CFRelease(macDraw->drawRgn);
	macDraw->drawRgn = NULL;
    }
    if (width >= 0 && height >= 0) {
	CGRect clipRect = CGRectMake(x + macDraw->xOff, y + macDraw->yOff,
		width, height);
	HIShapeRef drawRgn = HIShapeCreateWithRect(&clipRect);

	// When drawing a Text widget we can reuse the
	// clipping region for different DLines, so we don't want to
	// update unless necessary.
	if (macDraw->winPtr && (macDraw->flags & TK_CLIP_INVALID)) {
	    TkMacOSXUpdateClipRgn(macDraw->winPtr);
	}
	if (macDraw->visRgn) {
	    macDraw->drawRgn = HIShapeCreateIntersection(macDraw->visRgn,
		    drawRgn);
	    CFRelease(drawRgn);
	} else {
	    macDraw->drawRgn = drawRgn;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ClipToGC --
 *
 *	Helper function to intersect given region with gc clip region.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
ClipToGC(
    Drawable d,
    GC gc,
    HIShapeRef *clipRgnPtr) /* must point to initialized variable */
{
    if (gc && gc->clip_mask &&
	    ((TkpClipMask *)gc->clip_mask)->type == TKP_CLIP_REGION) {
	Region gcClip = ((TkpClipMask *)gc->clip_mask)->value.region;
	int xOffset = ((MacDrawable *)d)->xOff + gc->clip_x_origin;
	int yOffset = ((MacDrawable *)d)->yOff + gc->clip_y_origin;
	HIShapeRef clipRgn = *clipRgnPtr, gcClipRgn;

	XOffsetRegion(gcClip, xOffset, yOffset);
	gcClipRgn = TkMacOSXGetNativeRegion(gcClip);
	if (clipRgn) {
	    *clipRgnPtr = HIShapeCreateIntersection(gcClipRgn, clipRgn);
	    CFRelease(clipRgn);
	} else {
	    *clipRgnPtr = HIShapeCreateCopy(gcClipRgn);
	}
	CFRelease(gcClipRgn);
	XOffsetRegion(gcClip, -xOffset, -yOffset);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXMakeStippleMap --
 *
 *	Given a drawable and a stipple pattern this function draws the pattern
 *	repeatedly over the drawable. The drawable can then be used as a mask
 *	for bit-bliting a stipple pattern over an object.
 *
 * Results:
 *	A BitMap data structure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
TkMacOSXMakeStippleMap(
    TCL_UNUSED(Drawable),	/* Window to apply stipple. */
    TCL_UNUSED(Drawable))	/* The stipple pattern. */
{
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_DrawHighlightBorder --
 *
 *	This procedure draws a rectangular ring around the outside of a widget
 *	to indicate that it has received the input focus.
 *
 *	On the Macintosh, this puts a 1 pixel border in the bgGC color between
 *	the widget and the focus ring, except in the case where highlightWidth
 *	is 0 or 1, in which case the border is left out.
 *
 *	For proper Mac L&F, use highlightWidth of 3.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A rectangle "width" pixels wide is drawn in "drawable", corresponding
 *	to the outer area of "tkwin".
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
 *	This procedure draws the rectangular frame area. If the user has
 *	requested themeing, it draws with the background theme.
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
    if (useThemedToplevel && Tk_IsTopLevel(tkwin)) {
	static Tk_3DBorder themedBorder = NULL;

	if (!themedBorder) {
	    themedBorder = Tk_Get3DBorder(NULL, tkwin,
		    "systemWindowHeaderBackground");
	}
	if (themedBorder) {
	    border = themedBorder;
	}
    }

    Tk_Fill3DRectangle(tkwin, drawable, border, highlightWidth,
	    highlightWidth, Tk_Width(tkwin) - 2 * highlightWidth,
	    Tk_Height(tkwin) - 2 * highlightWidth, borderWidth, relief);
}

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
