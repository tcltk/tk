/*
 * tkMacOSXPath.c --
 *
 *	This file implements path drawing API's using CoreGraphics on Mac OS X.
 *
 * Copyright (c) 2005-2008  Mats Bengtsson
 *
 *
 */

/* This should go into configure.in but don't know how. */
#ifdef USE_PANIC_ON_PHOTO_ALLOC_FAILURE
#undef USE_PANIC_ON_PHOTO_ALLOC_FAILURE
#endif

#include "tkMacOSXInt.h"
#include "tkIntPath.h"

#if !__OBJC__
#error Objective-C compiler required
#endif

#import <Cocoa/Cocoa.h>


#define TINT_INT_CALCULATION

/* Seems to work for both Endians. */
#define BlueFloatFromXColorPtr(xc)   ((float) ((xc)->blue >> 8) / 255.0)
#define GreenFloatFromXColorPtr(xc)  ((float) ((xc)->green >> 8) / 255.0)
#define RedFloatFromXColorPtr(xc)    ((float) ((xc)->red >> 8) / 255.0)

#define Blue255FromXColorPtr(xc)   ((xc)->blue >> 8)
#define Green255FromXColorPtr(xc)  ((xc)->green >> 8)
#define Red255FromXColorPtr(xc)    ((xc)->red >> 8)


#ifndef FloatToFixed
#define FloatToFixed(a) ((Fixed)((float) (a) * fixed1))
#endif

extern int Tk_PathAntiAlias;
extern int Tk_PathSurfaceCopyPremultiplyAlpha;
extern int Tk_PathDepixelize;

const CGFloat kValidDomain[2] = {0, 1};
const CGFloat kValidRange[8] = {0, 1, 0, 1, 0, 1, 0, 1};

/*
 * This is used as a place holder for platform dependent stuff between each call.
 */
typedef struct TkPathContext_ {
    CGContextRef    c;
    int             saveCount;
    CGrafPtr        port;	/* QD graphics port, NULL for bitmaps. */
    char            *data;	/* bitmap data, NULL for windows. */
    int             widthCode;  /* Used to depixelize the strokes:
                                 * 0: not integer width
                                 * 1: odd integer width
                                 * 2: even integer width */

    /* fields from TK TkMacOSXDrawingContext: */
    NSView *view;
    HIShapeRef clipRgn;
    CGRect portBounds;
    int focusLocked;
    int xOff, yOff;
} TkPathContext_;

#define MAX_NL 32
typedef struct PathATSUIRecord {
    ATSUStyle       atsuStyle;
    ATSUTextLayout  atsuLayout;
    UniChar         *buffer;	/* @@@ Not sure this needs to be cached! */
    int             nlc;
    int             nl[MAX_NL + 1];
    ATSUTextMeasurement   dx[MAX_NL];
    ATSUTextMeasurement   dy[MAX_NL];
} PathATSUIRecord;

typedef struct FillInfo {
    double fillOpacity;
    GradientStopArray *stopArrPtr;
} FillInfo;

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXDrawableView --
 *
 *      This function returns the NSView for a given X drawable.
 *
 * Results:
 *      A NSView* or nil.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

NSView*
TkMacOSXDrawableView(
    MacDrawable *macWin)
{
    NSView *result = nil;

    if (!macWin) {
        result = nil;
    } else if (!macWin->toplevel) {
        result = macWin->view;
    } else if (!(macWin->toplevel->flags & TK_EMBEDDED)) {
        result = macWin->toplevel->view;
    } else {
        TkWindow *contWinPtr = TkpGetOtherWindow(macWin->toplevel->winPtr);
        if (contWinPtr) {
            result = TkMacOSXDrawableView(contWinPtr->privatePtr);
        }
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpClipDrawableToRect --
 *
 *      Clip all drawing into the drawable d to the given rectangle.
 *      If width or height are negative, reset to no clipping.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Subsequent drawing into d is offset and clipped as specified.
 *
 *----------------------------------------------------------------------
 */

void
TkpClipDrawableToRect(
    Display *display,
    Drawable d,
    int x, int y,
    int width, int height)
{
    MacDrawable *macDraw = (MacDrawable *) d;
    NSView *view = TkMacOSXDrawableView(macDraw);

    if (macDraw->drawRgn) {
        CFRelease(macDraw->drawRgn);
        macDraw->drawRgn = NULL;
    }
    if (width >= 0 && height >= 0) {
        CGRect drawRect = CGRectMake(x + macDraw->xOff, y + macDraw->yOff,
                width, height);
        HIShapeRef drawRgn = HIShapeCreateWithRect(&drawRect);

        if (macDraw->winPtr && macDraw->flags & TK_CLIP_INVALID) {
            TkMacOSXUpdateClipRgn(macDraw->winPtr);
        }
        if (macDraw->visRgn) {
            macDraw->drawRgn = HIShapeCreateIntersection(macDraw->visRgn,
                    drawRgn);
            CFRelease(drawRgn);
        } else {
            macDraw->drawRgn = drawRgn;
        }
        if (view && view != [NSView focusView] && [view lockFocusIfCanDraw]) {
            drawRect.origin.y = [view bounds].size.height -
                    (drawRect.origin.y + drawRect.size.height);
            NSRectClip(NSRectFromCGRect(drawRect));
            macDraw->flags |= TK_FOCUSED_VIEW;
        }
    } else {
        if (view && (macDraw->flags & TK_FOCUSED_VIEW)) {
            [view unlockFocus];
            macDraw->flags &= ~TK_FOCUSED_VIEW;
        }
    }
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
    MacDrawable *macDraw = (MacDrawable *) drawable;
    HIShapeRef clipRgn = NULL;

    if (macDraw->winPtr && macDraw->flags & TK_CLIP_INVALID) {
	TkMacOSXUpdateClipRgn(macDraw->winPtr);
#ifdef TK_MAC_DEBUG_DRAWING
	TkMacOSXDbgMsg("%s visRgn  ", macDraw->winPtr->pathName);
	TkMacOSXDebugFlashRegion(drawable, macDraw->visRgn);
#endif /* TK_MAC_DEBUG_DRAWING */
    }

    if (macDraw->drawRgn) {
        clipRgn = HIShapeCreateCopy(macDraw->drawRgn);
    } else if (macDraw->visRgn) {
        clipRgn = HIShapeCreateCopy(macDraw->visRgn);
    }

    return clipRgn;
}


/* copied from tk8.5.16/macosx/tkMacOSXSubwindows.c */
NSView*
TkpMacOSXDrawableView(
                     MacDrawable *macWin)
{
    NSView *result = nil;

    if (!macWin) {
	result = nil;
    } else if (!macWin->toplevel) {
	result = macWin->view;
    } else if (!(macWin->toplevel->flags & TK_EMBEDDED)) {
	result = macWin->toplevel->view;
    } else {
	TkWindow *contWinPtr = TkpGetOtherWindow(macWin->toplevel->winPtr);
	if (contWinPtr) {
	    result = TkpMacOSXDrawableView(contWinPtr->privatePtr);
	}
    }
    return result;
}

void
PathSetUpCGContext(
        Drawable d,
        TkPathContext_ *dcPtr)
{
    CGrafPtr port;
    MacDrawable *macDraw = (MacDrawable *) d;
    int dontDraw = 0;

    dcPtr->c = NULL;
    dcPtr->view = NULL;
    dcPtr->clipRgn = NULL;
    dcPtr->focusLocked = 0;

    port = TkMacOSXGetDrawablePort(d);

    dcPtr->clipRgn = TkMacOSXGetClipRgn(d);
    if (!dontDraw) {
        dontDraw = dcPtr->clipRgn ? HIShapeIsEmpty(dcPtr->clipRgn) : 0;
    }
    if (dontDraw) {
        goto end;
    }

    NSView *view = TkpMacOSXDrawableView(macDraw);
    if (view) {
        NSView *fView = [NSView focusView];
        if (view != fView) {
            dcPtr->focusLocked = [view lockFocusIfCanDraw];
            dontDraw = !dcPtr->focusLocked;
        } else {
            dontDraw = ![view canDraw];
        }
        if (dontDraw) {
            goto end;
        }
        [[view window] disableFlushWindow];
        dcPtr->view = view;
        NSGraphicsContext *currentGraphicsContext = [NSGraphicsContext currentContext];
        dcPtr->c = (CGContextRef)[currentGraphicsContext graphicsPort];
        dcPtr->portBounds = NSRectToCGRect([view bounds]);
        if (dcPtr->clipRgn) {
        }
    } else {
        Tcl_Panic("PathSetUpCGContext(): "
                  "no NSView to draw into !");
    }

    /*
     * Core Graphics defines the origin to be the bottom left
     * corner of the CGContext and the positive y-axis points up.
     * Move the origin and flip the y-axis for all subsequent
     * Core Graphics drawing operations.
     */

    CGContextSaveGState(dcPtr->c);
    dcPtr->saveCount = 1;
    CGRect cgbounds = CGContextGetClipBoundingBox(dcPtr->c);
    dcPtr->portBounds = NSRectToCGRect([view bounds]);
    dcPtr->portBounds.origin.x += macDraw->xOff;
    dcPtr->portBounds.origin.y += macDraw->yOff;

    dcPtr->xOff = macDraw->xOff;
    dcPtr->yOff = macDraw->yOff;

end:
    if (dontDraw && dcPtr->clipRgn) {
        CFRelease(dcPtr->clipRgn);
        dcPtr->clipRgn = NULL;
    }
}

void
PathReleaseCGContext(
                     TkPathContext_ *dcPtr)
{
    if (dcPtr->c) {
        CGContextSynchronize(dcPtr->c);
        [[dcPtr->view window] setViewsNeedDisplay:YES];
        [[dcPtr->view window] enableFlushWindow];
        if (dcPtr->focusLocked)
        {
	    [dcPtr->view unlockFocus];
        }
	while (dcPtr->saveCount > 0) {
            CGContextRestoreGState(dcPtr->c);
	    dcPtr->saveCount--;
        }
    }
    if (dcPtr->clipRgn) {
        CFRelease(dcPtr->clipRgn);
        dcPtr->clipRgn = NULL;
    }
}

CGColorSpaceRef GetTheColorSpaceRef(void)
{
    static CGColorSpaceRef deviceRGB = NULL;
    if (deviceRGB == NULL) {
        deviceRGB = CGColorSpaceCreateDeviceRGB();
    }
    return deviceRGB;
}

static LookupTable LineCapStyleLookupTable[] = {
    {CapNotLast, 		kCGLineCapButt},
    {CapButt, 	 		kCGLineCapButt},
    {CapRound, 	 		kCGLineCapRound},
    {CapProjecting, 	kCGLineCapSquare}
};

static LookupTable LineJoinStyleLookupTable[] = {
    {JoinMiter, 	kCGLineJoinMiter},
    {JoinRound,		kCGLineJoinRound},
    {JoinBevel, 	kCGLineJoinBevel}
};

void
PathSetCGContextStyle(CGContextRef c, Tk_PathStyle *style)
{
    Tk_PathDash *dashPtr;
    int fill = 0, stroke = 0;

    /** Drawing attribute functions. **/

    /* Set the line width in the current graphics state to `width'. */
    CGContextSetLineWidth(c, style->strokeWidth);

    /* Set the line cap in the current graphics state to `cap'. */
    CGContextSetLineCap(c,
            TkPathTableLookup(LineCapStyleLookupTable, 4, style->capStyle));

    /* Set the line join in the current graphics state to `join'. */
    CGContextSetLineJoin(c,
            TkPathTableLookup(LineJoinStyleLookupTable, 3, style->joinStyle));

    /* Set the miter limit in the current graphics state to `limit'. */
    CGContextSetMiterLimit(c, style->miterLimit);

    /* Set the line dash patttern in the current graphics state. */
    dashPtr = style->dashPtr;
    if ((dashPtr != NULL) && (dashPtr->number != 0)) {
        CGFloat *dashes = (CGFloat *)ckalloc(dashPtr->number * sizeof(CGFloat));
        int i;

        for (i = 0; i < dashPtr->number; i++)
            dashes[i] = dashPtr->array[i] * style->strokeWidth;
        CGContextSetLineDash(c, 0.0, dashes, dashPtr->number);
        ckfree((char *)dashes);
    }

    /* Set the current fill colorspace in the context `c' to `DeviceRGB' and
     * set the components of the current fill color to `(red, green, blue,
     * alpha)'. */
    if (GetColorFromPathColor(style->fill) != NULL) {
        fill = 1;
        CGContextSetRGBFillColor(c,
                RedFloatFromXColorPtr(style->fill->color),
                GreenFloatFromXColorPtr(style->fill->color),
                BlueFloatFromXColorPtr(style->fill->color),
                style->fillOpacity);
    }

    /* Set the current stroke colorspace in the context `c' to `DeviceRGB' and
    * set the components of the current stroke color to `(red, green, blue,
    * alpha)'. */
    if (style->strokeColor != NULL) {
        stroke = 1;
        CGContextSetRGBStrokeColor(c,
                RedFloatFromXColorPtr(style->strokeColor),
                GreenFloatFromXColorPtr(style->strokeColor),
                BlueFloatFromXColorPtr(style->strokeColor),
                style->strokeOpacity);
    }
    if (stroke && fill) {
        CGContextSetTextDrawingMode(c, kCGTextFillStroke);
    } else if (stroke) {
        CGContextSetTextDrawingMode(c, kCGTextStroke);
    } else if (fill) {
        CGContextSetTextDrawingMode(c, kCGTextFill);
    }
}

/* Various ATSUI support functions. */

static OSStatus
CreateATSUIStyle(const char *fontFamily, float fontSize, Boolean isBold, Boolean isItalic, ATSUStyle *atsuStylePtr)
{
    OSStatus	err = noErr;
    ATSUStyle 	style;
    ATSUFontID	atsuFont;
    Fixed	atsuSize;
    Boolean	isUnderline = false;
    static const ATSUAttributeTag tags[] = { kATSUFontTag,       kATSUSizeTag,  kATSUQDBoldfaceTag,  kATSUQDItalicTag,  kATSUQDUnderlineTag };
    static const ByteCount sizes[] =       { sizeof(ATSUFontID), sizeof(Fixed), sizeof(Boolean),     sizeof(Boolean),   sizeof(Boolean) };
    const ATSUAttributeValuePtr values[] = { &atsuFont,          &atsuSize,     &isBold,             &isItalic,         &isUnderline };

    *atsuStylePtr = NULL;
    style = NULL;
    atsuFont = 0;
    atsuSize = FloatToFixed(fontSize);
    {
        err = ATSUFindFontFromName((Ptr) fontFamily, strlen(fontFamily), kFontFamilyName,
                kFontNoPlatformCode, kFontNoScriptCode, kFontNoLanguageCode, &atsuFont);
    }
    if (err != noErr) {
        return err;
    }
    err = ATSUCreateStyle(&style);
    if (err != noErr) {
        if (style) ATSUDisposeStyle(style);
        return err;
    }
    err = ATSUSetAttributes(style, sizeof(tags)/sizeof(tags[0]),
            tags, sizes, values);
    if (err != noErr) {
        if (style) ATSUDisposeStyle(style);
        return err;
    }
    *atsuStylePtr = style;
    return noErr;
}

static OSStatus
CreateLayoutForString(UniChar *buffer, CFIndex length, ATSUStyle atsuStyle, ATSUTextLayout *layoutPtr)
{
    ATSUTextLayout layout = NULL;
    OSStatus err = noErr;

    *layoutPtr = NULL;
    err = ATSUCreateTextLayoutWithTextPtr(buffer, 0,
            length, length, 1, (unsigned long *) &length, &atsuStyle, &layout);
    if (err == noErr) {
        *layoutPtr = layout;
    }
    ATSUSetTransientFontMatching(layout, true);
    return err;
}

TkPathContext
TkPathInit(Tk_Window tkwin, Drawable d)
{
    TkPathContext_ *context = (TkPathContext_ *) ckalloc(sizeof(TkPathContext_));
    bzero(context, sizeof(TkPathContext_));

    PathSetUpCGContext(d, context);
    context->port = TkMacOSXGetDrawablePort(d);
    context->data = NULL;
    context->widthCode = 0;
    return (TkPathContext) context;
}

TkPathContext
TkPathInitSurface(Display *display, int width, int height)
{
    CGContextRef cgContext;
    TkPathContext_ *context = (TkPathContext_ *) ckalloc((unsigned) (sizeof(TkPathContext_)));
    size_t bytesPerRow;
    char *data;

    /* Move up into own function */
    bzero(context, sizeof(TkPathContext_));
    bytesPerRow = 4*width;
    /* Round up to nearest multiple of 16 */
    bytesPerRow = (bytesPerRow + (16-1)) & ~(16-1);
    data = ckalloc(height*bytesPerRow);

    /* Make it RGBA with 32 bit depth. */
    cgContext = CGBitmapContextCreate(data, width, height, 8, bytesPerRow,
            GetTheColorSpaceRef(), kCGImageAlphaPremultipliedLast);
    if (cgContext == NULL) {
        ckfree((char *) context);
        return (TkPathContext) NULL;
    }
    CGContextClearRect(cgContext, CGRectMake(0, 0, width, height));
    CGContextTranslateCTM(cgContext, 0, height);
    CGContextScaleCTM(cgContext, 1, -1);
    context->c = cgContext;
    context->port = NULL;
    context->data = data;
    context->clipRgn = NULL;
    context->saveCount = 0;
    context->xOff = context->yOff = 0;
    return (TkPathContext) context;
}

void
TkPathPushTMatrix(TkPathContext ctx, TMatrix *mPtr)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGAffineTransform transform;

    if (mPtr == NULL) {
        return;
    }
    /* Return the transform [ a b c d tx ty ]. */
    transform = CGAffineTransformMake(
            (float) mPtr->a, (float) mPtr->b,
            (float) mPtr->c, (float) mPtr->d,
            (float) mPtr->tx, (float) mPtr->ty);
    CGContextConcatCTM(context->c, transform);
}

void
TkPathResetTMatrix(TkPathContext ctx)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    context->widthCode = 0;
    while (context->saveCount > 0) {
	CGContextRestoreGState(context->c);
	context->saveCount--;
    }
    CGContextSaveGState(context->c);
    context->saveCount++;

    if (context->data != NULL) {
	/* surface bitmap */
	return;
    }

    CGAffineTransform t = { .a=1.0, .b=0.0, .c=0.0, .d=-1.0, .tx=0.0,
			    .ty=context->portBounds.size.height};
    CGContextConcatCTM(context->c, t);

    CGRect r;
    HIShapeGetBounds(context->clipRgn, &r);

    HIShapeReplacePathInCGContext(context->clipRgn, context->c);
    CGContextEOClip(context->c);

    CGContextTranslateCTM(context->c, context->xOff, context->yOff);

    CGContextSetShouldAntialias(context->c, Tk_PathAntiAlias);
    CGContextSetInterpolationQuality(context->c, kCGInterpolationHigh);
}

void
TkPathSaveState(TkPathContext ctx)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGContextSaveGState(context->c);
    context->saveCount++;
}

void
TkPathRestoreState(TkPathContext ctx)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if (context->saveCount > 0) {
	CGContextRestoreGState(context->c);
	context->saveCount--;
    }
}

void
TkPathBeginPath(TkPathContext ctx, Tk_PathStyle *stylePtr)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    int nint;
    double width;
    CGContextBeginPath(context->c);
    PathSetCGContextStyle(context->c, stylePtr);
    if (stylePtr->strokeColor == NULL) {
        context->widthCode = 0;
    } else {
        width = stylePtr->strokeWidth;
        nint = (int) (width + 0.5);
        context->widthCode = fabs(width - nint) > 0.01 ? 0 : 2 - nint % 2;
    }
}

void
TkPathMoveTo(TkPathContext ctx, double x, double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if (Tk_PathDepixelize) {
        x = PATH_DEPIXELIZE(context->widthCode, x);
        y = PATH_DEPIXELIZE(context->widthCode, y);
    }
    CGContextMoveToPoint(context->c, x, y);
}

void
TkPathLineTo(TkPathContext ctx, double x, double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if (Tk_PathDepixelize) {
        x = PATH_DEPIXELIZE(context->widthCode, x);
        y = PATH_DEPIXELIZE(context->widthCode, y);
    }
    CGContextAddLineToPoint(context->c, x, y);
}

void
TkPathLinesTo(TkPathContext ctx, double *pts, int n)
{
    /* TkPathContext_ *context = (TkPathContext_ *) ctx; */
    /* Add a set of lines to the context's path. */
    /* CGContextAddLines(context->c, const CGPoint points[], size_t count); */
}

void
TkPathQuadBezier(TkPathContext ctx, double ctrlX, double ctrlY, double x, double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if (Tk_PathDepixelize) {
        x = PATH_DEPIXELIZE(context->widthCode, x);
        y = PATH_DEPIXELIZE(context->widthCode, y);
    }
    CGContextAddQuadCurveToPoint(context->c, ctrlX, ctrlY, x, y);
}

void
TkPathCurveTo(TkPathContext ctx, double ctrlX1, double ctrlY1,
        double ctrlX2, double ctrlY2, double x, double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if (Tk_PathDepixelize) {
        x = PATH_DEPIXELIZE(context->widthCode, x);
        y = PATH_DEPIXELIZE(context->widthCode, y);
    }
    CGContextAddCurveToPoint(context->c, ctrlX1, ctrlY1, ctrlX2, ctrlY2, x, y);
}

void
TkPathArcTo(TkPathContext ctx,
        double rx, double ry,
        double phiDegrees, 	/* The rotation angle in degrees! */
        char largeArcFlag, char sweepFlag, double x, double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    /* @@@ Should we try to use the native arc functions here? */
    if (Tk_PathDepixelize) {
        x = PATH_DEPIXELIZE(context->widthCode, x);
        y = PATH_DEPIXELIZE(context->widthCode, y);
    }
    TkPathArcToUsingBezier(ctx, rx, ry, phiDegrees, largeArcFlag, sweepFlag, x, y);
}

void
TkPathRectangle(TkPathContext ctx, double x, double y, double width, double height)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGRect r;
    if (Tk_PathDepixelize) {
        x = PATH_DEPIXELIZE(context->widthCode, x);
        y = PATH_DEPIXELIZE(context->widthCode, y);
    }
    r = CGRectMake(x, y, width, height);
    CGContextAddRect(context->c, r);
}

void
TkPathOval(TkPathContext ctx, double cx, double cy, double rx, double ry)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;

    CGRect r;
    r = CGRectMake(cx-rx, cy-ry, 2*rx, 2*ry);
    CGContextAddEllipseInRect(context->c, r);
}


CGInterpolationQuality convertInterpolationToCGInterpolation(int interpolation)
{
    switch (interpolation) {
        case kPathImageInterpolationNone:
            return kCGInterpolationNone;
        case kPathImageInterpolationFast:
            return kCGInterpolationLow;
        case kPathImageInterpolationBest:
            return kCGInterpolationHigh;
        default:
            return kCGInterpolationMedium;
    }
}

void
TkPathImage(TkPathContext ctx, Tk_Image image, Tk_PhotoHandle photo,
        double x, double y, double width0, double height0, double fillOpacity,
        XColor *tintColor, double tintAmount, int interpolation, PathRect *srcRegion)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGImageRef cgImage;
    CGDataProviderRef provider;
    CGColorSpaceRef colorspace;
    CGImageAlphaInfo alphaInfo;
    size_t size;
    Tk_PhotoImageBlock block;
    unsigned char *data = NULL;
    unsigned char *ptr = NULL;
    unsigned char *srcPtr, *dstPtr;
    int srcR, srcG, srcB, srcA;     /* The source pixel offsets. */
    int dstR, dstG, dstB, dstA;     /* The destination pixel offsets. */
    int pitch;
    int iwidth, iheight;
    int i, j;

    /* Return value? */
    Tk_PhotoGetImage(photo, &block);
    size = block.pitch * block.height;
    iheight = block.height;
    iwidth = block.width;
    pitch = block.pitch;
    double width = (width0 == 0.0) ? (double)iwidth : width0;
    double height = (height0 == 0.0) ? (double)iheight : height0;


    /*
     * The offset array contains the offsets from the address of a pixel to
     * the addresses of the bytes containing the red, green, blue and alpha
     * (transparency) components.  These are normally 0, 1, 2 and 3.
     * @@@ There are more cases to consider than these!
     */
    srcR = dstR = block.offset[0];
    srcG = dstG = block.offset[1];
    srcB = dstB = block.offset[2];
    srcA = dstA = block.offset[3];

    if (srcA == 3) {
        alphaInfo = kCGImageAlphaLast;
    } else if (srcA == 0) {
        alphaInfo = kCGImageAlphaFirst;
    } else {
        /* @@@ What to do here? */
        return;
    }

    if (block.pixelSize == 4) {
        if ((srcR == dstR) && (srcG == dstG) && (srcB == dstB) && (srcA == dstA) && fillOpacity >= 1.0 && (tintAmount <= 0.0 || tintColor == NULL)) {
            ptr = (unsigned char *) block.pixelPtr;
        } else {
            data = (unsigned char *) ckalloc(pitch*iheight);
            ptr = data;

            if (tintColor && tintAmount > 0.0) {
#ifdef TINT_INT_CALCULATION
                uint32_t tintR, tintG, tintB, uAmount, uRemain, uOpacity;

                if (tintAmount > 1.0)
                    tintAmount = 1.0;
                uAmount = (uint32_t)(tintAmount * 256.0);
                uRemain = 256 - uAmount;
                uOpacity = (uint32_t)(fillOpacity * 256.0);
                tintR = Red255FromXColorPtr(tintColor);
                tintG = Green255FromXColorPtr(tintColor);
                tintB = Blue255FromXColorPtr(tintColor);
                for (i = 0; i < iheight; i++) {
                    srcPtr = block.pixelPtr + i*pitch;
                    dstPtr = ptr + i*pitch;
                    for (j = 0; j < iwidth; j++) {
                        /* extract */
                        uint32_t r = *(srcPtr+srcR);
                        uint32_t g = *(srcPtr+srcG);
                        uint32_t b = *(srcPtr+srcB);
                        uint32_t a = *(srcPtr+srcA);

                        /* transform */
                        uint32_t lumAmount = ((r * 6966 + g * 23436 + b * 2366) * uAmount) >> 23;  /* 0-256 */
                        r = (uRemain * r + lumAmount * tintR);
                        g = (uRemain * g + lumAmount * tintG);
                        b = (uRemain * b + lumAmount * tintB);

                        /* fix range */
                        r = r>0xFFFF ? 0xFFFF : r;
                        g = g>0xFFFF ? 0xFFFF : g;
                        b = b>0xFFFF ? 0xFFFF : b;

                        /* and put back */
                        *(dstPtr+dstR) = r >> 8;
                        *(dstPtr+dstG) = g >> 8;
                        *(dstPtr+dstB) = b >> 8;
                        *(dstPtr+dstA) = (a * uOpacity) >> 8;
                        srcPtr += 4;
                        dstPtr += 4;
                    }
                }
#else
                float tintR, tintG, tintB;

                if (tintAmount > 1.0)
                    tintAmount = 1.0;
                tintR = RedFloatFromXColorPtr(tintColor);
                tintG = GreenFloatFromXColorPtr(tintColor);
                tintB = BlueFloatFromXColorPtr(tintColor);
                for (i = 0; i < iheight; i++) {
                    srcPtr = block.pixelPtr + i*pitch;
                    dstPtr = ptr + i*pitch;
                    for (j = 0; j < iwidth; j++) {
                        /* extract */
                        int r = *(srcPtr+srcR);
                        int g = *(srcPtr+srcG);
                        int b = *(srcPtr+srcB);

                        /* transform */
                        int lum = (int)(0.2126*r + 0.7152*g + 0.0722*b);
                        r = (int)((1.0-tintAmount)*r + tintAmount*lum*tintR);
                        g = (int)((1.0-tintAmount)*g + tintAmount*lum*tintG);
                        b = (int)((1.0-tintAmount)*b + tintAmount*lum*tintB);

                        /* fix range */
                        r = r<0 ? 0 : r>255 ? 255 : r;
                        g = g<0 ? 0 : g>255 ? 255 : g;
                        b = b<0 ? 0 : b>255 ? 255 : b;

                        /* and put back */
                        *(dstPtr+dstR) = r;
                        *(dstPtr+dstG) = g;
                        *(dstPtr+dstB) = b;
                        *(dstPtr+dstA) = *(srcPtr+srcA) * fillOpacity;
                        srcPtr += 4;
                        dstPtr += 4;
                    }
                }
#endif
            } else {
                for (i = 0; i < iheight; i++) {
                    srcPtr = block.pixelPtr + i*pitch;
                    dstPtr = ptr + i*pitch;
                    for (j = 0; j < iwidth; j++) {
                        *(dstPtr+dstR) = *(srcPtr+srcR);
                        *(dstPtr+dstG) = *(srcPtr+srcG);
                        *(dstPtr+dstB) = *(srcPtr+srcB);
                        *(dstPtr+dstA) = *(srcPtr+srcA) * fillOpacity;
                        srcPtr += 4;
                        dstPtr += 4;
                    }
                }
            }
        }
    } else {
        ptr = (unsigned char *) block.pixelPtr;
        return;
    }
    provider = CGDataProviderCreateWithData(NULL, ptr, size, NULL);
    colorspace = CGColorSpaceCreateDeviceRGB();
    cgImage = CGImageCreate(block.width, block.height,
            8, 						/* bitsPerComponent */
            block.pixelSize*8,	 	/* bitsPerPixel */
            block.pitch, 			/* bytesPerRow */
            colorspace,				/* colorspace */
            alphaInfo,				/* alphaInfo */
            provider, NULL,
            interpolation > 0 ? 1 : 0,  /* shouldInterpolate */
            kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorspace);
    if (width == 0.0) {
        width = (double) block.width;
    }
    if (height == 0.0) {
        height = (double) block.height;
    }

    CGContextSaveGState(context->c);
    context->saveCount++;

    if (srcRegion != NULL) {
        width = (width0 == 0.0) ? srcRegion->x2 - srcRegion->x1 : width0;
        height = (height0 == 0.0) ? srcRegion->y2 - srcRegion->y1 : height0;
        double xscale = width / (srcRegion->x2 - srcRegion->x1);
        double yscale = height / (srcRegion->y2 - srcRegion->y1);
        CGContextSetInterpolationQuality(context->c, convertInterpolationToCGInterpolation(interpolation));
        CGContextTranslateCTM(context->c, x, y+height);
        CGContextScaleCTM(context->c, xscale, -yscale);
        CGContextClipToRect(context->c, CGRectMake(0.0, 0.0, width/xscale, height/yscale));
        CGContextDrawTiledImage(context->c,
                CGRectMake(srcRegion->x1, fmod(srcRegion->y2, iheight), iwidth, iheight),
                cgImage);
    } else {
        /* Flip back to an upright coordinate system since CGContextDrawImage expect this. */
        CGContextSetInterpolationQuality(context->c, convertInterpolationToCGInterpolation(interpolation));
        CGContextTranslateCTM(context->c, x, y+height);
        CGContextScaleCTM(context->c, 1, -1);
        CGContextDrawImage(context->c, CGRectMake(0.0, 0.0, width, height), cgImage);
    }
    CGImageRelease(cgImage);
    CGContextRestoreGState(context->c);
    context->saveCount--;
    if (data) {
        ckfree((char *)data);
    }
}

void
TkPathClosePath(TkPathContext ctx)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGContextClosePath(context->c);
}

/*
 * @@@ Problems: don't want Tcl_Interp, finding matching font not while processing options.
 * Separate font style from layout???
 */

Boolean isItalic(enum FontSlant slant)
{
    switch(slant) {
        case PATH_TEXT_SLANT_NORMAL: return false;
        case PATH_TEXT_SLANT_ITALIC: return true;
        case PATH_TEXT_SLANT_OBLIQUE: return true;
        default: return false;
    }
}

Boolean
isBold(enum FontWeight weight)
{
    switch(weight) {
        case PATH_TEXT_WEIGHT_NORMAL: return false;
        case PATH_TEXT_WEIGHT_BOLD: return true;
        default: return false;
    }
}

int
TkPathTextConfig(Tcl_Interp *interp, Tk_PathTextStyle *textStylePtr, char *utf8, void **customPtr)
{
    PathATSUIRecord	*recordPtr;
    ATSUStyle 		atsuStyle = NULL;
    ATSUTextLayout 	atsuLayout = NULL;
    CFStringRef 	cf;
    UniChar 		*buffer;
    CFRange 		range;
    CFIndex 		length;
    OSStatus 		err;
    Tcl_Encoding	enc;
    Tcl_DString		ds;

    if (utf8 == NULL) {
        return TCL_OK;
    }
    TkPathTextFree(textStylePtr, *customPtr);

    enc = Tcl_GetEncoding(NULL, "utf-8");
    Tcl_DStringInit(&ds);
    Tcl_UtfToExternalDString(enc, utf8, -1, &ds);
    Tcl_FreeEncoding(enc);
    cf = CFStringCreateWithCString(NULL, Tcl_DStringValue(&ds), kCFStringEncodingUTF8);
    Tcl_DStringFree(&ds);
    length = CFStringGetLength(cf);
    if (length == 0) {
        return TCL_OK;
    }
    range = CFRangeMake(0, length);
    err = CreateATSUIStyle(textStylePtr->fontFamily, textStylePtr->fontSize, isBold(textStylePtr->fontWeight), isItalic(textStylePtr->fontSlant), &atsuStyle);
    if (err != noErr) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("font style couldn't be created", -1));
        return TCL_ERROR;
    }
    buffer = (UniChar *) ckalloc(length * sizeof(UniChar));
    CFStringGetCharacters(cf, range, buffer);
    err = CreateLayoutForString(buffer, length, atsuStyle, &atsuLayout);
    CFRelease(cf);
    if (err != noErr) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("text layout couldn't be created", -1));
        ckfree((char *)buffer);
        return TCL_ERROR;
    }
    recordPtr = (PathATSUIRecord *) ckalloc(sizeof(PathATSUIRecord));
    recordPtr->atsuStyle = atsuStyle;
    recordPtr->atsuLayout = atsuLayout;
    recordPtr->buffer = buffer;
    int i, j;
    recordPtr->nl[0] = 0;
    for (i = 0, j = 1; i < length; i++) {
        if ((j < MAX_NL) && (buffer[i] == '\n')) {
            recordPtr->nl[j++] = i + 1;
            buffer[i] = 0x2028;
        }
    }
    recordPtr->nl[j] = i + 1;
    recordPtr->nlc = j;
    *customPtr = (PathATSUIRecord *) recordPtr;
    return TCL_OK;
}

static void drawMultilineText(PathATSUIRecord *recordPtr)
{
    int i;

    for (i = 0; i < recordPtr->nlc; i++) {
        ATSUDrawText(recordPtr->atsuLayout, recordPtr->nl[i], recordPtr->nl[i+1] - recordPtr->nl[i] - 1, recordPtr->dx[i], recordPtr->dy[i]);
    }
}

void
TkPathTextDraw(TkPathContext ctx, Tk_PathStyle *style, Tk_PathTextStyle *textStylePtr,
        double x, double y, int fillOverStroke, char *utf8, void *custom)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    PathATSUIRecord *recordPtr = (PathATSUIRecord *) custom;
    ByteCount iSize = sizeof(CGContextRef);
    ATSUAttributeTag iTag = kATSUCGContextTag;
    ATSUAttributeValuePtr iValuePtr = &(context->c);

    ATSUSetLayoutControls(recordPtr->atsuLayout, 1, &iTag, &iSize, &iValuePtr);
    CGContextSaveGState(context->c);
    context->saveCount++;
    CGContextTranslateCTM(context->c, x, y);
    CGContextScaleCTM(context->c, 1, -1);
    if ((style->strokeColor != NULL) && (GetColorFromPathColor(style->fill) != NULL)) {
        CGContextSetTextDrawingMode(context->c, fillOverStroke ? kCGTextStroke : kCGTextFill);
        drawMultilineText(recordPtr);
        CGContextSetTextDrawingMode(context->c, fillOverStroke ? kCGTextFill : kCGTextStroke);
        drawMultilineText(recordPtr);
    } else {
        drawMultilineText(recordPtr);
    }
    CGContextRestoreGState(context->c);
    context->saveCount--;
}

void
TkPathTextFree(Tk_PathTextStyle *textStylePtr, void *custom)
{
    PathATSUIRecord *recordPtr = (PathATSUIRecord *) custom;
    if (recordPtr) {
        if (recordPtr->atsuStyle) {
            ATSUDisposeStyle(recordPtr->atsuStyle);
        }
        if (recordPtr->atsuLayout) {
            ATSUDisposeTextLayout(recordPtr->atsuLayout);
        }
        if (recordPtr->buffer) {
            ckfree((char *) recordPtr->buffer);
        }
    }
}

PathRect
TkPathTextMeasureBbox(Display *display, Tk_PathTextStyle *textStylePtr, char *utf8, double *lineSpacing, void *custom)
{
    PathATSUIRecord *recordPtr = (PathATSUIRecord *) custom;
    PathRect r,ri;
    int i;
    ATSTrapezoid b;
    ItemCount numBounds;
    double x = 0.0;
    double y = 0.0;
    double baseX = 0.0;
    double lineSp = 0;

    for (i = 0; i < recordPtr->nlc; i++) {
        b.upperRight.x = b.upperLeft.x = 0;

        ATSUGetGlyphBounds(recordPtr->atsuLayout, 0, 0,
                recordPtr->nl[i], recordPtr->nl[i+1] - recordPtr->nl[i] - 1,
                kATSUseFractionalOrigins, 1, &b, &numBounds);
        ri.x1 = MIN(Fix2X(b.upperLeft.x), Fix2X(b.lowerLeft.x));
        ri.y1 = MIN(Fix2X(b.upperLeft.y), Fix2X(b.upperRight.y));
        ri.x2 = MAX(Fix2X(b.upperRight.x), Fix2X(b.lowerRight.x));
        ri.y2 = MAX(Fix2X(b.lowerLeft.y), Fix2X(b.lowerRight.y));
        if (i == 0) {
            baseX = ri.x1;
            r.x1 = ri.x1;
            r.y1 = ri.y1;
            r.x2 = ri.x2;
            r.y2 = ri.y2;
        } else {
            x = ri.x1 - baseX;
            ri.x1 -= x;
            ri.y1 += y;
            ri.x2 -= x;
            ri.y2 += y;
            if (r.x1 > ri.x1) r.x1 = ri.x1;
            if (r.y1 > ri.y1) r.y1 = ri.y1;
            if (r.x2 < ri.x2) r.x2 = ri.x2;
            if (r.y2 < ri.y2) r.y2 = ri.y2;
        }
        recordPtr->dx[i] = X2Fix(-x);
        recordPtr->dy[i] = X2Fix(-y);
        y = r.y2 - r.y1;
	lineSp += y;
    }

    if (lineSpacing != NULL && i > 0) {
	*lineSpacing = lineSp / i;
    }

    return r;
}

void
TkPathSurfaceErase(TkPathContext ctx, double x, double y, double width, double height)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGContextClearRect(context->c, CGRectMake(x, y, width, height));
}

void
TkPathSurfaceToPhoto(Tcl_Interp *interp, TkPathContext ctx, Tk_PhotoHandle photo)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGContextRef c = context->c;
    Tk_PhotoImageBlock block;
    unsigned char *data;
    unsigned char *pixel;
    int width, height;
    int bytesPerRow;

    width = CGBitmapContextGetWidth(c);
    height = CGBitmapContextGetHeight(c);
    data = CGBitmapContextGetData(c);
    bytesPerRow = CGBitmapContextGetBytesPerRow(c);

    Tk_PhotoGetImage(photo, &block);
    pixel = (unsigned char *) attemptckalloc(height*bytesPerRow);
    if (pixel == NULL) {
        return;
    }
    if (Tk_PathSurfaceCopyPremultiplyAlpha) {
        TkPathCopyBitsPremultipliedAlphaRGBA(data, pixel, width, height, bytesPerRow);
    } else {
        memcpy(pixel, data, height*bytesPerRow);
    }
    block.pixelPtr = pixel;
    block.width = width;
    block.height = height;
    block.pitch = bytesPerRow;
    block.pixelSize = 4;
    block.offset[0] = 0;
    block.offset[1] = 1;
    block.offset[2] = 2;
    block.offset[3] = 3;
    /* Should change this to check for errors... */
    Tk_PhotoPutBlock(interp, photo, &block, 0, 0, width, height, TK_PHOTO_COMPOSITE_OVERLAY);
    ckfree((char *) pixel);
}

void
TkPathClipToPath(TkPathContext ctx, int fillRule)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;

    /* If you need to grow the clipping path after it’s shrunk, you must save the
     * graphics state before you clip, then restore the graphics state to restore the current
     * clipping path. */
    CGContextSaveGState(context->c);
    context->saveCount++;
    if (fillRule == WindingRule) {
        CGContextClip(context->c);
    } else if (fillRule == EvenOddRule) {
        CGContextEOClip(context->c);
    }
}

void
TkPathReleaseClipToPath(TkPathContext ctx)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGContextRestoreGState(context->c);
    context->saveCount--;
}

void
TkPathStroke(TkPathContext ctx, Tk_PathStyle *style)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGContextStrokePath(context->c);
}

void
TkPathFill(TkPathContext ctx, Tk_PathStyle *style)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if (style->fillRule == WindingRule) {
        CGContextFillPath(context->c);
    } else if (style->fillRule == EvenOddRule) {
        CGContextEOFillPath(context->c);
    }
}

void
TkPathFillAndStroke(TkPathContext ctx, Tk_PathStyle *style)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if (style->fillRule == WindingRule) {
        CGContextDrawPath(context->c, kCGPathFillStroke);
    } else if (style->fillRule == EvenOddRule) {
        CGContextDrawPath(context->c, kCGPathEOFillStroke);
    }
}

void
TkPathEndPath(TkPathContext ctx)
{
    /* TkPathContext_ *context = (TkPathContext_ *) ctx; */
    /* Empty ??? */
}

void
TkPathFree(TkPathContext ctx)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    PathReleaseCGContext(context);
    if (context->data) {
        ckfree(context->data);
    }
    ckfree((char *) ctx);
}

int
TkPathDrawingDestroysPath(void)
{
    return 1;
}

int
TkPathPixelAlign(void)
{
    return 0;
}

/* TkPathGetCurrentPosition --
 *
 * 		Returns the current pen position in untransformed coordinates!
 */

int
TkPathGetCurrentPosition(TkPathContext ctx, PathPoint *ptPtr)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGPoint cgpt;

    cgpt = CGContextGetPathCurrentPoint(context->c);
    ptPtr->x = cgpt.x;
    ptPtr->y = cgpt.y;
    return TCL_OK;
}

int
TkPathBoundingBox(TkPathContext ctx, PathRect *rPtr)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGRect cgRect;

    /* This one is not very useful since it includes the control points. */
    cgRect = CGContextGetPathBoundingBox(context->c);
    rPtr->x1 = cgRect.origin.x;
    rPtr->y1 = cgRect.origin.y;
    rPtr->x2 = cgRect.origin.x + cgRect.size.width;
    rPtr->y2 = cgRect.origin.y + cgRect.size.height;
    return TCL_OK;
}

/*
 * Using CGShading for fill gradients.
 */

static void
ShadeEvaluate(void *info, const CGFloat *in, CGFloat *out)
{
    FillInfo            *fillInfo = (FillInfo *)info;
    GradientStopArray 	*stopArrPtr = fillInfo->stopArrPtr;
    double              fillOpacity = fillInfo->fillOpacity;
    GradientStop        **stopPtrPtr = stopArrPtr->stops;
    GradientStop		*stop1 = NULL, *stop2 = NULL;
    int					nstops = stopArrPtr->nstops;
    int					i = 0;
    float 				par = *in;
    float				f1, f2;

    /* Find the two stops for this point. Tricky! */
    while ((i < nstops) && ((*stopPtrPtr)->offset < par)) {
        stopPtrPtr++, i++;
    }
    if (i == 0) {
        /* First stop > 0. */
        stop1 = *stopPtrPtr;
        stop2 = stop1;
    } else if (i == nstops) {
        /* We have stepped beyond the last stop; step back! */
        stop1 = *(stopPtrPtr - 1);
        stop2 = stop1;
    } else {
        stop1 = *(stopPtrPtr - 1);
        stop2 = *stopPtrPtr;
    }
    /* Interpolate between the two stops.
     * "If two gradient stops have the same offset value,
     * then the latter gradient stop controls the color value at the
     * overlap point."
     */
    if (fabs(stop2->offset - stop1->offset) < 1e-6) {
        *out++ = RedFloatFromXColorPtr(stop2->color);
        *out++ = GreenFloatFromXColorPtr(stop2->color);
        *out++ = BlueFloatFromXColorPtr(stop2->color);
        *out++ = stop2->opacity * fillOpacity;
    } else {
        f1 = (stop2->offset - par)/(stop2->offset - stop1->offset);
        f2 = (par - stop1->offset)/(stop2->offset - stop1->offset);
        *out++ = f1 * RedFloatFromXColorPtr(stop1->color) +
                f2 * RedFloatFromXColorPtr(stop2->color);
        *out++ = f1 * GreenFloatFromXColorPtr(stop1->color) +
                f2 * GreenFloatFromXColorPtr(stop2->color);
        *out++ = f1 * BlueFloatFromXColorPtr(stop1->color) +
                f2 * BlueFloatFromXColorPtr(stop2->color);
        *out++ = (f1 * stop1->opacity + f2 * stop2->opacity) * fillOpacity;
    }
}

static void
ShadeRelease(void *info)
{
    /* Not sure if anything to do here. */
}

void
TkPathPaintLinearGradient(TkPathContext ctx, PathRect *bbox, LinearGradientFill *fillPtr, int fillRule, double fillOpacity, TMatrix *mPtr)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGShadingRef 		shading;
    CGPoint 			start, end;
    CGColorSpaceRef 	colorSpaceRef;
    CGFunctionRef 		function;
    CGFunctionCallbacks callbacks;
    PathRect 			*trans = fillPtr->transitionPtr;		/* The transition line. */
    FillInfo            fillInfo;

    fillInfo.fillOpacity = fillOpacity;
    fillInfo.stopArrPtr = fillPtr->stopArrPtr;

    callbacks.version = 0;
    callbacks.evaluate = ShadeEvaluate;
    callbacks.releaseInfo = ShadeRelease;
    colorSpaceRef = CGColorSpaceCreateDeviceRGB();

    /*
     * We need to do like this since this is how SVG defines gradient drawing
     * in case the transition vector is in relative coordinates.
     */
    CGContextSaveGState(context->c);
    context->saveCount++;
    if (fillPtr->units == kPathGradientUnitsBoundingBox) {
        CGContextTranslateCTM(context->c, bbox->x1, bbox->y1);
        CGContextScaleCTM(context->c, bbox->x2 - bbox->x1, bbox->y2 - bbox->y1);
    }
    function = CGFunctionCreate((void *) &fillInfo, 1, kValidDomain, 4, kValidRange, &callbacks);
    start = CGPointMake(trans->x1, trans->y1);
    end   = CGPointMake(trans->x2, trans->y2);
    shading = CGShadingCreateAxial(colorSpaceRef, start, end, function, 1, 1);
    if (mPtr) {
        /* @@@ I'm not completely sure of the order of transforms here! */
        TkPathPushTMatrix(ctx, mPtr);
    }
    CGContextDrawShading(context->c, shading);
    CGContextRestoreGState(context->c);
    context->saveCount--;
    CGShadingRelease(shading);
    CGFunctionRelease(function);
    CGColorSpaceRelease(colorSpaceRef);
}

void
TkPathPaintRadialGradient(TkPathContext ctx, PathRect *bbox, RadialGradientFill *fillPtr, int fillRule, double fillOpacity, TMatrix *mPtr)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    CGShadingRef 		shading;
    CGPoint 			start, end;
    CGColorSpaceRef 	colorSpaceRef;
    CGFunctionRef 		function;
    CGFunctionCallbacks callbacks;
    RadialTransition    *tPtr = fillPtr->radialPtr;
    FillInfo            fillInfo;

    fillInfo.fillOpacity = fillOpacity;
    fillInfo.stopArrPtr = fillPtr->stopArrPtr;

    callbacks.version = 0;
    callbacks.evaluate = ShadeEvaluate;
    callbacks.releaseInfo = ShadeRelease;
    colorSpaceRef = CGColorSpaceCreateDeviceRGB();

    /*
     * We need to do like this since this is how SVG defines gradient drawing
     * in case the transition vector is in relative coordinates.
     */
    if (fillPtr->units == kPathGradientUnitsBoundingBox) {
        CGContextSaveGState(context->c);
	context->saveCount++;
        CGContextTranslateCTM(context->c, bbox->x1, bbox->y1);
        CGContextScaleCTM(context->c, bbox->x2 - bbox->x1, bbox->y2 - bbox->y1);
    }
    function = CGFunctionCreate((void *) &fillInfo, 1, kValidDomain, 4, kValidRange, &callbacks);
    start = CGPointMake(tPtr->focalX, tPtr->focalY);
    end   = CGPointMake(tPtr->centerX, tPtr->centerY);
    shading = CGShadingCreateRadial(colorSpaceRef, start, 0.0, end, tPtr->radius, function, 1, 1);
    if (mPtr) {
        /* @@@ I'm not completely sure of the order of transforms here! */
        TkPathPushTMatrix(ctx, mPtr);
    }
    CGContextDrawShading(context->c, shading);
    CGShadingRelease(shading);
    CGFunctionRelease(function);
    CGColorSpaceRelease(colorSpaceRef);
    if (fillPtr->units == kPathGradientUnitsBoundingBox) {
        CGContextRestoreGState(context->c);
	context->saveCount--;
    }
}

int
TkPathSetup(Tcl_Interp *interp)
{
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */

