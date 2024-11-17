/*
 * tkMacOSXDraw.c --
 *
 *	This file contains functions that draw to windows. Many of thees
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
    static Boolean initialized = FALSE;

    if (!initialized) {
	initialized = TRUE;

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
    int i, lw = gc->line_width;

    if (npoints < 2) {
	return BadValue;
    }

    LastKnownRequestProcessed(display)++;
    if (!TkMacOSXSetupDrawingContext(d, gc, &dc)) {
	return BadDrawable;
    }
    if (dc.context) {
	double prevx, prevy;
	double o = (lw % 2) ? .5 : 0;

	CGContextBeginPath(dc.context);
	prevx = macWin->xOff + points[0].x + o;
	prevy = macWin->yOff + points[0].y + o;
	CGContextMoveToPoint(dc.context, prevx, prevy);
	for (i = 1; i < npoints; i++) {
	    if (mode == CoordModeOrigin) {
		CGContextAddLineToPoint(dc.context,
			macWin->xOff + points[i].x + o,
			macWin->yOff + points[i].y + o);
	    } else {
		prevx += points[i].x;
		prevy += points[i].y;
		CGContextAddLineToPoint(dc.context, prevx, prevy);
	    }
	}

	/*
	 * In the case of closed polylines, the first and last points are the
	 * same. We want miter or bevel join be rendered also at this point,
	 * this needs telling CoreGraphics that the path is closed.
	 */

	if ((points[0].x == points[npoints-1].x) &&
		(points[0].y == points[npoints-1].y)) {
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
    int i, lw = gc->line_width;

    LastKnownRequestProcessed(display)++;
    if (!TkMacOSXSetupDrawingContext(d, gc, &dc)) {
	return BadDrawable;
    }
    if (dc.context) {
	double o = (lw % 2) ? .5 : 0;

	for (i = 0; i < nsegments; i++) {
	    CGContextBeginPath(dc.context);
	    CGContextMoveToPoint(dc.context,
		    macWin->xOff + segments[i].x1 + o,
		    macWin->yOff + segments[i].y1 + o);
	    CGContextAddLineToPoint(dc.context,
		    macWin->xOff + segments[i].x2 + o,
		    macWin->yOff + segments[i].y2 + o);
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
    int i;

    LastKnownRequestProcessed(display)++;
    if (!TkMacOSXSetupDrawingContext(d, gc, &dc)) {
	return BadDrawable;
    }
    if (dc.context) {
	double prevx, prevy;
	double o = (gc->line_width % 2) ? .5 : 0;

	CGContextBeginPath(dc.context);
	prevx = macWin->xOff + points[0].x + o;
	prevy = macWin->yOff + points[0].y + o;
	CGContextMoveToPoint(dc.context, prevx, prevy);
	for (i = 1; i < npoints; i++) {
	    if (mode == CoordModeOrigin) {
		CGContextAddLineToPoint(dc.context,
			macWin->xOff + points[i].x + o,
			macWin->yOff + points[i].y + o);
	    } else {
		prevx += points[i].x;
		prevy += points[i].y;
		CGContextAddLineToPoint(dc.context, prevx, prevy);
	    }
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
 * TkScrollWindow --
 *
 *	Scroll a rectangle of the specified window and accumulate a damage
 *	region.
 *
 * Results:
 *	Returns 0 if the scroll generated no additional damage. Otherwise, sets
 *	the region that needs to be repainted after scrolling and returns 1.
 *      When drawRect was in use, this function used the now deprecated
 *      scrollRect method of NSView.  With the current updateLayer
 *      implementation, using a CGImage as the view's backing layer, we are
 *      able to use XCopyArea.  But both implementations are incomplete.
 *      They return a damage area which is just the source rectangle minus
 *      destination rectangle.  Other platforms, e.g. Windows, where
 *      this function is essentially provided by the windowing system,
 *      are able to add to the damage region the bounding rectangles of
 *      all subwindows which meet the source rectangle, even if they are
 *      contained in the destination rectangle.  The information needed
 *      to do that is not available in this module, as far as I know.
 *
 *      In fact, the Text widget is the only one which calls this
 *      function, and  textDisp.c compensates for this defect by using
 *      macOS-specific code.  This is possible because access to the
 *      list of all embedded windows in a Text widget is available in
 *      that module.
 *
 * Side effects:
 *	Scrolls the bits in the window.
 *
 *----------------------------------------------------------------------
 */

int
TkScrollWindow(
    Tk_Window tkwin,		/* The window to be scrolled. */
    GC gc,			/* GC for window to be scrolled. */
    int x, int y,		/* Position rectangle to be scrolled. */
    int width, int height,
    int dx, int dy,		/* Distance rectangle should be moved. */
    Region damageRgn)		/* Region to accumulate damage in. */
{
    Drawable drawable = Tk_WindowId(tkwin);
    HIShapeRef srcRgn, dstRgn;
    HIMutableShapeRef dmgRgn = HIShapeCreateMutable();
    NSRect srcRect, dstRect;
    int result = 0;

    // Should behave more like TkScrollWindow on other platforms
    if (XCopyArea(Tk_Display(tkwin), drawable, drawable, gc, x, y,
	    (unsigned)width, (unsigned)height, x+dx, y+dy) == Success) {

	/*
	 * Compute the damage region, using Tk coordinates (origin at top left).
	 */

	srcRect = CGRectMake(x, y, width, height);
	dstRect = CGRectOffset(srcRect, dx, dy);
	srcRgn = HIShapeCreateWithRect(&srcRect);
	dstRgn = HIShapeCreateWithRect(&dstRect);
	ChkErr(HIShapeDifference, srcRgn, dstRgn, dmgRgn);
	CFRelease(dstRgn);
	CFRelease(srcRgn);
	result = HIShapeIsEmpty(dmgRgn) ? 0 : 1;

    }

    /*
     * Convert the HIShape dmgRgn into a TkRegion and store it.
     */

    TkMacOSXSetWithNativeRegion(damageRgn, dmgRgn);

    CFRelease(dmgRgn);
    return result;
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

Bool
TkMacOSXSetupDrawingContext(
    Drawable d,
    GC gc,
    TkMacOSXDrawingContext *dcPtr)
{
    MacDrawable *macDraw = (MacDrawable *)d;
    Bool canDraw = true;
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
    dc.clipRgn = TkMacOSXGetClipRgn(d);
    ClipToGC(d, gc, &dc.clipRgn);
    if (dc.clipRgn && HIShapeIsEmpty(dc.clipRgn)) {
	/*
	 * Things are probably not set up for drawing yet.  Request a call to
	 * updateLayer and return failure.
	 */
	canDraw = false;
	[view setNeedsDisplay:YES];
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
    if (!canDraw && macDraw->winPtr != NULL) {
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
     * Mark the view as needing to be redisplayed, since we have drawn on its
     * backing layer.
     */
    
    [dcPtr->view setNeedsDisplay:YES];

#ifdef TK_MAC_DEBUG
    bzero(dcPtr, sizeof(TkMacOSXDrawingContext));
#endif

}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXGetClipRgn --
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
TkMacOSXGetClipRgn(
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
