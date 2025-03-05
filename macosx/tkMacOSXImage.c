/*
 * tkMacOSXImage.c --
 *
 *	The code in this file provides an interface for XImages, and
 *      implements the nsimage image type.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2017-2021 Marc Culler.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkMacOSXConstants.h"
#include "tkMacOSXImage.h"
#include "tkColor.h"
#include "xbytes.h"

static CGImageRef CreateCGImageFromPixmap(Drawable pixmap);
static CGImageRef CreateCGImageFromDrawableRect( Drawable drawable, int force_1x_scale,
     int x, int y, unsigned int width, unsigned int height, CGFloat *scale);
static inline CGRect ClipCopyRects(CGRect srcBounds, CGRect dstBounds,
     int src_x, int src_y, unsigned int width,  unsigned int height);

/* Pixel formats
 *
 * Tk uses the XImage structure defined in Xlib.h for storing images.  The
 * image data in an XImage is a 32-bit aligned array of bytes.  Interpretation
 * of that data is not specified, but the structure includes parameters which
 * provide interpretation hints so that an application can use a family of
 * different data structures.
 *
 * The possible values for the XImage format field are XYBitmap, XYPixmap and
 * ZPixmap.  The macOS port does not support the XYPixmap format.  This means
 * that bitmap images are stored as a single bit plane (XYBitmap) and that
 * color images are stored as a sequence of pixel values (ZPixmap).
 *
 * For a ZPixmap, the number of bits allocated to each pixel is specified by
 * the bits_per_pixel field of the XImage structure.  The functions in this
 * module which convert between XImage and native CGImage or NSImage structures
 * only support XImages with 32 bits per pixel.  The ImageGetPixel and PutPixel
 * implementations in this file allow 1, 4, 8, 16 or 32 bits per pixel, however.
 *
 * In tkImgPhInstance.c the layout used for pixels is determined by the values
 * of the red_mask, blue_mask and green_mask fields in the XImage structure.
 * The Aqua port always sets red_mask = 0xFF0000, green_mask = 0xFF00, and
 * blue_mask = 0xFF. This means that a 32bpp ZPixmap XImage uses ARGB32 pixels,
 * with small-endian byte order BGRA. The data array for such an XImage can be
 * passed directly to construct a CGBitmapImageRep if one specifies the
 * bitmapInfo as kCGBitmapByteOrder32Big | kCGImageAlphaLast.
 *
 * The structures below describe the bitfields in two common 32 bpp pixel
 * layouts.  Note that bit field layouts are compiler dependent. The layouts
 * shown in the comments are those produced by clang and gcc.  Also note
 * that kCGBitmapByteOrder32Big is consistently set when creating CGImages or
 * CGImageBitmapReps.
 */

/* RGBA32 0xRRGGBBAA (Byte order is RGBA on big-endian systems.)
 * This is used by NSBitmapImageRep when the bitmapFormat property is 0,
 * the default value.
 */

typedef struct RGBA32pixel_t {
    unsigned red: 8;
    unsigned green: 8;
    unsigned blue: 8;
    unsigned alpha: 8;
} RGBA32pixel;

/*
 * ARGB32 0xAARRGGBB (Byte order is ARGB on big-endian systems.)
 * This is used by Aqua Tk for XImages and by NSBitmapImageReps whose
 * bitmapFormat property is NSAlphaFirstBitmapFormat.
 */

typedef struct ARGB32pixel_t {
    unsigned blue: 8;
    unsigned green: 8;
    unsigned red: 8;
    unsigned alpha: 8;
} ARGB32pixel;

typedef union pixel32_t {
    unsigned int uint;
    RGBA32pixel rgba;
    ARGB32pixel argb;
} pixel32;

#pragma mark XImage handling

int
_XInitImageFuncPtrs(
    TCL_UNUSED(XImage *)) /* image */
{
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXCreateCGImageWithXImage --
 *
 *	Create CGImage from XImage, copying the image data.  Called
 *      in Tk_PutImage and (currently) nowhere else.
 *
 * Results:
 *	CGImage, release after use.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void ReleaseData(
    void *info,
    TCL_UNUSED(const void *), /* data */
    TCL_UNUSED(size_t))        /* size */
{
    ckfree(info);
}

static CGImageRef
TkMacOSXCreateCGImageWithXImage(
    XImage *image,
    uint32_t bitmapInfo)
{
    CGImageRef img = NULL;
    size_t bitsPerComponent, bitsPerPixel;
    size_t len = image->bytes_per_line * image->height;
    const CGFloat *decode = NULL;
    CGDataProviderRef provider = NULL;
    char *data = NULL;
    CGDataProviderReleaseDataCallback releaseData = ReleaseData;

    if (image->bits_per_pixel == 1) {
	/*
	 * BW image
	 */

	/* Reverses the sense of the bits */
	static const CGFloat decodeWB[2] = {1, 0};
	decode = decodeWB;

	bitsPerComponent = 1;
	bitsPerPixel = 1;
	data = (char *)ckalloc(len);
	if (data) {
	    if (image->bitmap_bit_order != MSBFirst) {
		char *srcPtr = image->data + image->xoffset;
		char *endPtr = srcPtr + len;
		char *destPtr = data;

		while (srcPtr < endPtr) {
		    *destPtr++ = xBitReverseTable[(unsigned char)(*(srcPtr++))];
		}
	    } else {
		memcpy(data, image->data + image->xoffset, len);
	    }
	    provider = CGDataProviderCreateWithData(data, data, len,
		    releaseData);
	    if (!provider) {
		ckfree(data);
	    }
	    img = CGImageMaskCreate(image->width, image->height,
		    bitsPerComponent, bitsPerPixel, image->bytes_per_line,
		    provider, decode, 0);
	    CGDataProviderRelease(provider);
	}
    } else if ((image->format == ZPixmap) && (image->bits_per_pixel == 32)) {

	/*
	 * Color image
	 */

	if (image->width == 0 && image->height == 0) {

	    /*
	     * CGCreateImage complains on early macOS releases.
	     */

	    return NULL;
	}
	CGColorSpaceRef colorspace = CGColorSpaceCreateDeviceRGB();
	bitsPerComponent = 8;
	bitsPerPixel = 32;
	data = (char *)ckalloc(len);
	if (data) {
	    memcpy(data, image->data + image->xoffset, len);
	    provider = CGDataProviderCreateWithData(data, data, len,
		    releaseData);
	    if (!provider) {
		ckfree(data);
	    }
	    img = CGImageCreate(image->width, image->height, bitsPerComponent,
		    bitsPerPixel, image->bytes_per_line, colorspace, bitmapInfo,
		    provider, decode, 0, kCGRenderingIntentDefault);
	    CGDataProviderRelease(provider);
	}
	if (colorspace) {
	    CFRelease(colorspace);
	}
    } else {
	TkMacOSXDbgMsg("Unsupported image type");
    }
    return img;
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyImage --
 *
 *	Destroys storage associated with an image.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deallocates the image.
 *
 *----------------------------------------------------------------------
 */

static int
DestroyImage(
    XImage *image)
{
    if (image) {
	if (image->data) {
	    ckfree(image->data);
	}
	ckfree(image);
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * ImageGetPixel --
 *
 *	Get a single pixel from an image.
 *
 * Results:
 *      The XColor structure contains an unsigned long field named pixel which
 *      identifies the color.  This function returns the unsigned long that
 *      would be used as the pixel value of an XColor that has the same red
 *      green and blue components as the XImage pixel at the specified
 *      location.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static unsigned long
ImageGetPixel(
    XImage *image,
    int x,
    int y)
{
    unsigned char r = 0, g = 0, b = 0;

    /*
     * Compute 8 bit red green and blue values, which are passed as inputs to
     * TkMacOSXRGBPixel to produce the pixel value.
     */

    if (image && image->data) {
	unsigned char *srcPtr = ((unsigned char*) image->data)
		+ (y * image->bytes_per_line)
		+ (((image->xoffset + x) * image->bits_per_pixel) / NBBY);

	switch (image->bits_per_pixel) {
	case 32: /* 8 bits per channel */
	    {
		ARGB32pixel *pixel = (ARGB32pixel *)srcPtr;
		r = pixel->red;
		g = pixel->green;
		b = pixel->blue;
	    }
	    break;
	case 16: /* 5 bits per channel */
	    r = (*((unsigned short*) srcPtr) >> 7) & 0xf8;
	    g = (*((unsigned short*) srcPtr) >> 2) & 0xf8;
	    b = (*((unsigned short*) srcPtr) << 3) & 0xf8;
	    break;
	case 8: /* 2 bits per channel */
	    r = (*srcPtr << 2) & 0xc0;
	    g = (*srcPtr << 4) & 0xc0;
	    b = (*srcPtr << 6) & 0xc0;
	    r |= r >> 2 | r >> 4 | r >> 6;
	    g |= g >> 2 | g >> 4 | g >> 6;
	    b |= b >> 2 | b >> 4 | b >> 6;
	    break;
	case 4: { /* 1 bit per channel */
	    unsigned char c = (x % 2) ? *srcPtr : (*srcPtr >> 4);

	    r = (c & 0x04) ? 0xff : 0;
	    g = (c & 0x02) ? 0xff : 0;
	    b = (c & 0x01) ? 0xff : 0;
	    break;
	}
	case 1: /* Black-white bitmap. */
	    r = g = b = ((*srcPtr) & (0x80 >> (x % 8))) ? 0xff : 0;
	    break;
	}
    }

    return TkMacOSXRGBPixel(r, g, b);
}

/*
 *----------------------------------------------------------------------
 *
 * ImagePutPixel --
 *
 *	Set a single pixel in an image.  The pixel is provided as an unsigned
 *      32-bit integer.  The value of that integer is interpreted by assuming
 *      that its low-order N bits have the format specified by the XImage,
 *      where N is equal to the bits_per_pixel field of the XImage.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ImagePutPixel(
    XImage *image,
    int x,
    int y,
    unsigned long pixel)
{
    if (image && image->data) {
	unsigned char *dstPtr = ((unsigned char*) image->data)
		+ (y * image->bytes_per_line)
		+ (((image->xoffset + x) * image->bits_per_pixel) / NBBY);

	if (image->bits_per_pixel == 32) {
	    *((unsigned int*) dstPtr) = pixel;
	} else {
	    switch (image->bits_per_pixel) {
	    case 16:
		*((unsigned short*) dstPtr) = pixel & 0xffff;
		break;
	    case 8:
		*dstPtr = pixel & 0xff;
		break;
	    case 4: {
		*dstPtr = (x % 2) ? ((*dstPtr & 0xf0) | (pixel & 0x0f)) :
			((*dstPtr & 0x0f) | ((pixel << 4) & 0xf0));
		break;
		}
	    case 1:
		*dstPtr = pixel ? (*dstPtr | (0x80 >> (x % 8))) :
			(*dstPtr & ~(0x80 >> (x % 8)));
		break;
	    }
	}
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateImage --
 *
 *	Allocates storage for a new XImage.
 *
 * Results:
 *	Returns a newly allocated XImage.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XImage *
XCreateImage(
    Display* display,
    TCL_UNUSED(Visual*), /* visual */
    unsigned int depth,
    int format,
    int offset,
    char* data,
    unsigned int width,
    unsigned int height,
    int bitmap_pad,
    int bytes_per_line)
{
    XImage *ximage;

    LastKnownRequestProcessed(display)++;
    ximage = (XImage *)ckalloc(sizeof(XImage));

    ximage->height = height;
    ximage->width = width;
    ximage->depth = depth;
    ximage->xoffset = offset;
    ximage->format = format;
    ximage->data = data;
    ximage->obdata = NULL;

    if (format == ZPixmap) {
	ximage->bits_per_pixel = 32;
	ximage->bitmap_unit = 32;
    } else {
	ximage->bits_per_pixel = 1;
	ximage->bitmap_unit = 8;
    }
    if (bitmap_pad) {
	ximage->bitmap_pad = bitmap_pad;
    } else {
	/*
	 * Use 16 byte alignment for best Quartz performance.
	 */

	ximage->bitmap_pad = 128;
    }
    if (bytes_per_line) {
	ximage->bytes_per_line = bytes_per_line;
    } else {
	ximage->bytes_per_line = ((width * ximage->bits_per_pixel +
		(ximage->bitmap_pad - 1)) >> 3) &
		~((ximage->bitmap_pad >> 3) - 1);
    }
#ifdef WORDS_BIGENDIAN
    ximage->byte_order = MSBFirst;
    ximage->bitmap_bit_order = MSBFirst;
#else
    ximage->byte_order = LSBFirst;
    ximage->bitmap_bit_order = LSBFirst;
#endif
    ximage->red_mask = 0x00FF0000;
    ximage->green_mask = 0x0000FF00;
    ximage->blue_mask = 0x000000FF;
    ximage->f.create_image = NULL;
    ximage->f.destroy_image = DestroyImage;
    ximage->f.get_pixel = ImageGetPixel;
    ximage->f.put_pixel = ImagePutPixel;
    ximage->f.sub_image = NULL;
    ximage->f.add_pixel = NULL;

    return ximage;
}

/*
 *----------------------------------------------------------------------
 *
 * TkPutImage, XPutImage, TkpPutRGBAImage --
 *
 *	These functions, which all have the same signature, copy a rectangular
 *      subimage of an XImage into a drawable.  TkPutImage is an alias for
 *      XPutImage, which assumes that the XImage data has the structure of a
 *      32bpp ZPixmap in which the image data is an array of 32bit integers
 *      packed with 8 bit values for the Red Green and Blue channels.  The
 *      fourth byte is ignored.  The function TkpPutRGBAImage assumes that the
 *      XImage data has been extended by using the fourth byte to store an
 *      8-bit Alpha value.  (The Alpha data is assumed not to pre-multiplied).
 *      The image is then drawn into the drawable using standard Porter-Duff
 *      Source Atop Composition (kCGBlendModeSourceAtop in Apple's Core
 *      Graphics).
 *
 *      The TkpPutRGBAImage function is used by TkImgPhotoDisplay to render photo
 *      images if the compile-time variable TK_CAN_RENDER_RGBA is defined in
 *      a platform's tkXXXXPort.h header, as is the case for the macOS Aqua port.
 *
 * Results:
 *	These functions return either BadDrawable or Success.
 *
 * Side effects:
 *	Draws the image on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

#define USE_ALPHA (kCGImageAlphaLast | kCGBitmapByteOrder32Big)
#define IGNORE_ALPHA (kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little)

static int
TkMacOSXPutImage(
    uint32_t pixelFormat,
    Display* display,		/* Display. */
    Drawable drawable,		/* Drawable to place image on. */
    GC gc,			/* GC to use. */
    XImage* image,		/* Image to place. */
    int src_x,			/* Source X & Y. */
    int src_y,
    int dest_x,			/* Destination X & Y. */
    int dest_y,
    unsigned int width,	        /* Same width & height for both */
    unsigned int height)	/* destination and source. */
{
    TkMacOSXDrawingContext dc;
    MacDrawable *macDraw = (MacDrawable *)drawable;
    int result = Success;

    if (width <= 0 || height <= 0) {
	return Success; /* Is OK. Nothing to see here, literally. */
    }
    LastKnownRequestProcessed(display)++;
    if (!TkMacOSXSetupDrawingContext(drawable, gc, &dc)) {
	return BadDrawable;
    }
    if (dc.context) {
	CGRect dstRect, srcRect = CGRectMake(src_x, src_y, width, height);
	/*
	 * Whole image is copied before cropping. For performance,
	 * consider revising TkMacOSXCreateCGImageWithXImage() to accept
	 * source x/y/w/h and copy only the needed portion instead.
	 */
	CGImageRef img = TkMacOSXCreateCGImageWithXImage(image, pixelFormat);
	CGImageRef cropped = CGImageCreateWithImageInRect(img, srcRect);
	CGImageRelease(img);
	img = cropped;

	/*
	 * The CGContext for a pixmap is RGB only, with A = 0.
	 */

	if (!(macDraw->flags & TK_IS_PIXMAP)) {
	    CGContextSetBlendMode(dc.context, kCGBlendModeSourceAtop);
	}
	if (img) {
	    dstRect = CGRectMake(dest_x, dest_y, width, height);
	    TkMacOSXDrawCGImage(drawable, gc, dc.context, img,
				gc->foreground, gc->background, dstRect);
	    CFRelease(img);
	} else {
	    TkMacOSXDbgMsg("Invalid source drawable");
	    result = BadDrawable;
	}
    } else {
	TkMacOSXDbgMsg("Invalid destination drawable");
	result = BadDrawable;
    }
    TkMacOSXRestoreDrawingContext(&dc);
    return result;
}

int XPutImage(
    Display* display,
    Drawable drawable,
    GC gc,
    XImage* image,
    int src_x,
    int src_y,
    int dest_x,
    int dest_y,
    unsigned int width,
    unsigned int height) {
    return TkMacOSXPutImage(IGNORE_ALPHA, display, drawable, gc, image,
			    src_x, src_y, dest_x, dest_y, width, height);
}

int TkpPutRGBAImage(
    Display* display,
    Drawable drawable,
    GC gc,
    XImage* image,
    int src_x,
    int src_y,
    int dest_x,
    int dest_y,
    unsigned int width,
    unsigned int height) {
    return TkMacOSXPutImage(USE_ALPHA, display, drawable, gc, image,
		     src_x, src_y, dest_x, dest_y, width, height);
}


/*
 *----------------------------------------------------------------------
 *
 * CreateCGImageFromDrawableRect
 *
 *	Extract image data from a MacOSX drawable as a CGImage.  The drawable
 *      may be either a pixmap or a window, but there issues in the case of
 *      a window.
 *
 *      CreateCGImageFromDrawableRect is called by XGetImage and XCopyArea.
 *      The Tk core uses these two functions on some platforms in order to
 *      implement explicit double-buffered drawing -- a pixmap is copied from a
 *      window, modified using CPU-based graphics composition, and then copied
 *      back to the window.  Platforms, such as macOS, on which the system
 *      provides double-buffered drawing and GPU-based composition operations
 *      can avoid calls to XGetImage and XCopyArea from the core by defining
 *      the compile-time variable TK_NO_DOUBLE_BUFFERING.  Nonetheless, these
 *      two functions are in the stubs table and therefore could be used by
 *      extensions.
 *
 *      The implementation here does not always work correctly when the source
 *      is a window.  The original version of this function relied on
 *      [NSBitmapImageRep initWithFocusedViewRect:view_rect] which was
 *      deprecated by Apple in OSX 10.14 and also required the use of other
 *      deprecated functions such as [NSView lockFocus]. Apple's suggested
 *      replacement is [NSView cacheDisplayInRect: toBitmapImageRep:] and that
 *      is being used here.  However, cacheDisplayInRect works by calling
 *      [NSView drawRect] after setting the current graphics context to be one
 *      which draws to a bitmap.  There are situations in which this can be
 *      used, e.g. when taking a screenshot of a window.  But it cannot be used
 *      as part of a normal display procedure, using the copy-modify-paste
 *      paradigm that is the basis of the explicit double-buffering.  Since the
 *      copy operation will call the same display procedure that is calling
 *      this function via XGetImage or XCopyArea, this would create an infinite
 *      recursion.
 *
 *      An alternative to the copy-modify-paste paradigm is to use GPU-based
 *      graphics composition, clipping to the specified rectangle.  That is
 *      the approach that must be followed by display procedures on macOS.
 *
 * Results:
 *	Returns an NSBitmapRep representing the image of the given rectangle of
 *      the given drawable. This object is retained. The caller is responsible
 *      for releasing it.
 *
 *      NOTE: The x,y coordinates should be relative to a coordinate system
 *      with origin at the top left, as used by XImage and CGImage, not bottom
 *      left as used by NSView.
 *
 *      If force_1x_scale is true, then the returned CGImage will be downscaled
 *      if necessary to have the requested width and height. Othewise, for
 *      windows on Retina displays, the width and height of the returned CGImage
 *      will be twice the requested width and height.
 *
 * Side effects:
 *     None
 *
 *----------------------------------------------------------------------
 */

static CGImageRef
CreateCGImageFromDrawableRect(
    Drawable drawable,
    int force_1x_scale,
    int x,
    int y,
    unsigned int width,
    unsigned int height,
    CGFloat *scalePtr)
{
    MacDrawable *mac_drawable = (MacDrawable *)drawable;
    CGContextRef cg_context = NULL;
    CGImageRef cg_image = NULL, result = NULL;
    CGFloat scaleFactor = 1.0;
    if (mac_drawable->flags & TK_IS_PIXMAP) {
	cg_context = TkMacOSXGetCGContextForDrawable(drawable);
	CGContextRetain(cg_context);
    } else {
	NSView *view = TkMacOSXGetNSViewForDrawable(mac_drawable);
	if (view == nil) {
	    TkMacOSXDbgMsg("Invalid source drawable");
	    return NULL;
	}
	scaleFactor = view.layer.contentsScale;
	cg_context = ((TKContentView *)view).tkLayerBitmapContext;
	CGContextRetain(cg_context);
    }
    if (scalePtr != nil) {
	*scalePtr = scaleFactor;
    }
    if (cg_context) {
	cg_image = CGBitmapContextCreateImage(cg_context);
	CGContextRelease(cg_context);
    }
    if (cg_image) {
	CGRect rect = CGRectMake(x + mac_drawable->xOff, y + mac_drawable->yOff,
				 width, height);
	rect = CGRectApplyAffineTransform(rect, CGAffineTransformMakeScale(scaleFactor, scaleFactor));
	if (force_1x_scale && (scaleFactor != 1.0)) {
	    // See https://web.archive.org/web/20200219030756/http://blog.foundry376.com/2008/07/scaling-a-cgimage/#comment-200
	    // create context, keeping original image properties
	    CGColorSpaceRef colorspace = CGImageGetColorSpace(cg_image);
	    cg_context = CGBitmapContextCreate(NULL, width, height,
		    CGImageGetBitsPerComponent(cg_image),
		    //CGImageGetBytesPerRow(cg_image), // wastes space?
		    CGImageGetBitsPerPixel(cg_image) * width / 8,
		    colorspace,
		    CGImageGetAlphaInfo(cg_image));
	    CGColorSpaceRelease(colorspace);
	    if (cg_context) {
		// Extract the subimage in the specified rectangle.
		CGImageRef subimage = CGImageCreateWithImageInRect(cg_image, rect);
		// Draw the subimage in our context (resizing it to fit).
		CGContextDrawImage(cg_context, CGRectMake(0, 0, width, height),
			subimage);
		// We will return the image we just drew.
		result = CGBitmapContextCreateImage(cg_context);
		CGContextRelease(cg_context);
		CGImageRelease(subimage);
	    }
	} else {
	    // No resizing is needed.  Just return the subimage
	    result = CGImageCreateWithImageInRect(cg_image, rect);
	}
	CGImageRelease(cg_image);
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * CreatePDFFromDrawableRect
 *
 *	Extract PDF data from a MacOSX drawable.
 *
 * Results:
 *	Returns a CFDataRef that can be written to a file.
 *
 *      NOTE: The x,y coordinates should be relative to a coordinate system
 *      with origin at the bottom left as used by NSView,  not top left
 *      as used by XImage and CGImage.
 *
 * Side effects:
 *     None
 *
 *----------------------------------------------------------------------
 */

CFDataRef
CreatePDFFromDrawableRect(
			  Drawable drawable,
			  int x,
			  int y,
			  unsigned int width,
			  unsigned int height)
{
    MacDrawable *mac_drawable = (MacDrawable *)drawable;
    NSView *view = TkMacOSXGetNSViewForDrawable(mac_drawable);
    if (view == nil) {
	TkMacOSXDbgMsg("Invalid source drawable");
	return NULL;
    }
    NSRect bounds, viewSrcRect;

    /*
     * Get the child window area in NSView coordinates
     * (origin at bottom left).
     */

    bounds = [view bounds];
    viewSrcRect = NSMakeRect(mac_drawable->xOff + x,
			     bounds.size.height - height - (mac_drawable->yOff + y),
			     width, height);
    NSData *viewData = [view dataWithPDFInsideRect:viewSrcRect];
    CFDataRef result = (CFDataRef)viewData;
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * CreateCGImageFromPixmap --
 *
 *	Create a CGImage from an X Pixmap.
 *
 * Results:
 *	CGImage, release after use.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static CGImageRef
CreateCGImageFromPixmap(
    Drawable pixmap)
{
    CGImageRef img = NULL;
    CGContextRef context = TkMacOSXGetCGContextForDrawable(pixmap);

    if (context) {
	img = CGBitmapContextCreateImage(context);
    }
    return img;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetImage --
 *
 *	This function copies data from a pixmap or window into an XImage.  It
 *      is essentially never used. At one time it was called by
 *      pTkImgPhotoDisplay, but that is no longer the case. Currently it is
 *      called two places, one of which is requesting an XY image which we do
 *      not support.  It probably does not work correctly -- see the comments
 *      for CGImageFromDrawableRect.
 *
 * Results:
 *	Returns a newly allocated XImage containing the data from the given
 *	rectangle of the given drawable, or NULL if the XImage could not be
 *	constructed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XImage *
XGetImage(
    Display *display,
    Drawable drawable,
    int x,
    int y,
    unsigned int width,
    unsigned int height,
    TCL_UNUSED(unsigned long),  /* plane_mask */
    int format)
{
    XImage* imagePtr = NULL;
    NSBitmapImageRep* bitmapRep = nil;
    NSBitmapFormat bitmap_fmt = 0;
    char *bitmap = NULL;
    int depth = 32, offset = 0, bitmap_pad = 0;
    NSInteger bytes_per_row, samples_per_pixel, size;
    unsigned int row, n, m;

    if (format == ZPixmap) {
	CGImageRef cgImage;
	if (width == 0 || height == 0) {
	    return NULL;
	}

	// Request 1x-scale image for compatibility
	cgImage = CreateCGImageFromDrawableRect(drawable, 1, x, y, width, height, nil);
	if (cgImage) {
	    bitmapRep = [NSBitmapImageRep alloc];
	    [bitmapRep initWithCGImage:cgImage];
	    CFRelease(cgImage);
	} else {
	    TkMacOSXDbgMsg("XGetImage: Failed to construct CGImage");
	    return NULL;
	}
	bitmap_fmt = [bitmapRep bitmapFormat];
	size = [bitmapRep bytesPerPlane];
	bytes_per_row = [bitmapRep bytesPerRow];
	samples_per_pixel = [bitmapRep samplesPerPixel];
#if 0
	fprintf(stderr, "XGetImage:\n"
		"  bitmsp_fmt = %ld\n"
		"  samples_per_pixel = %ld\n"
		"  width = %u\n"
		"  height = %u\n"
		"  bytes_per_row = %ld\n"
		"  size = %ld\n",
		bitmap_fmt, samples_per_pixel, width, height, bytes_per_row, size);
#endif
	/*
	 * Image data with all pixels having alpha value 255 may be reported
	 * as 3 samples per pixel, even though each row has 4*width pixels and
	 * the pixels are stored in the default ARGB32 format.
	 */

	if ((bitmap_fmt != 0 && bitmap_fmt != NSAlphaFirstBitmapFormat)
	    || samples_per_pixel < 3
	    || samples_per_pixel > 4
	    || [bitmapRep isPlanar] != 0
	    || size != bytes_per_row * height) {
	    TkMacOSXDbgMsg("XGetImage: Unrecognized bitmap format");
	    [bitmapRep release];
	    return NULL;
	}
	bitmap = (char *)ckalloc(size);
	memcpy(bitmap, (char *)[bitmapRep bitmapData], size);
	[bitmapRep release];

	for (row = 0, n = 0; row < height; row++, n += bytes_per_row) {
	    for (m = n; m < n + 4*width; m += 4) {
		pixel32 pixel = *((pixel32 *)(bitmap + m));
		if (bitmap_fmt == 0) { // default format

		    /*
		     * This pixel is in ARGB32 format.  We need RGBA32.
		     */

		    pixel32 flipped;
		    flipped.rgba.red = pixel.argb.red;
		    flipped.rgba.green = pixel.argb.green;
		    flipped.rgba.blue = pixel.argb.blue;
		    flipped.rgba.alpha = pixel.argb.alpha;
		    *((pixel32 *)(bitmap + m)) = flipped;
		} else { // bitmap_fmt = NSAlphaFirstBitmapFormat
		    *((pixel32 *)(bitmap + m)) = pixel;
		}
	    }
	}

	imagePtr = XCreateImage(display, NULL, depth, format, offset,
		(char*) bitmap, width, height,
		bitmap_pad, bytes_per_row);
    } else {

	/*
	 * There are some calls to XGetImage in the generic Tk code which pass
	 * an XYPixmap rather than a ZPixmap.  XYPixmaps should be handled
	 * here.
	 */

	TkMacOSXDbgMsg("XGetImage does not handle XYPixmaps at the moment.");
    }
    return imagePtr;
}

static inline CGRect
ClipCopyRects(
    CGRect srcBounds,
    CGRect dstBounds,
    int src_x,
    int src_y,
    unsigned int width,
    unsigned int height)
{
    CGRect srcRect = CGRectMake(src_x, src_y, width, height);
    CGRect bounds1 = CGRectIntersection(srcRect, srcBounds);
    return CGRectIntersection(bounds1, dstBounds);
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
    NSView *view = TkMacOSXGetNSViewForDrawable(drawable);
    CGRect viewBounds = [view bounds];    
    
    /*
     * To compute the damage region correctly we need to clip the source and
     * destination rectangles to the NSView bounds in the same way that
     * XCopyArea does.
     */

    CGRect bounds = ClipCopyRects(viewBounds, viewBounds, x, y, width, height);
    unsigned int w = bounds.size.width;
    unsigned int h = bounds.size.height;
    
    if (XCopyArea(Tk_Display(tkwin), drawable, drawable, gc, x, y,
	     w, h, x + dx, y + dy) == Success) {

	/*
	 * Compute the damage region, using Tk coordinates (origin at top left).
	 */

	srcRect = CGRectMake(x, y, width, height);
	dstRect = CGRectOffset(bounds, dx, dy);
	//dstRect = CGRectOffset(srcRect, dx, dy);
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
 * XCopyArea --
 *
 *	Copies image data from one drawable to another.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Image data is moved from a window or bitmap to a second window or bitmap.
 *
 *----------------------------------------------------------------------
 */

int
XCopyArea(
    Display *display,		/* Display. */
    Drawable src,		/* Source drawable. */
    Drawable dst,		/* Destination drawable. */
    GC gc,			/* GC to use. */
    int src_x,			/* X & Y, width & height */
    int src_y,			/* define the source rectangle */
    unsigned int width,		/* that will be copied. */
    unsigned int height,
    int dst_x,			/* Dest X & Y on dest rect. */
    int dst_y)
{
    TkMacOSXDrawingContext dc;
    CGImageRef img = NULL;
    CGRect dstRect;

    // XXXX Need to deal with pixmaps!

    NSView *srcView = TkMacOSXGetNSViewForDrawable(src);
    NSView *dstView = TkMacOSXGetNSViewForDrawable(dst);
    CGRect srcBounds = [srcView bounds];    
    CGRect dstBounds = [dstView bounds];

    // To avoid distorting the image when it is drawn we must ensure that
    // the source and destination rectangles have the same size.  This is
    // tricky because each of those rectangles will be clipped to the
    // bounds of its containing NSView.  If the source gets clipped and
    // the destination does not, for example, then the shapes will differ.
    // We deal with this by reducing their common size  enough so that both
    // rectangles are  contained in their respective views.

    CGRect bounds = ClipCopyRects(srcBounds, dstBounds, src_x, src_y, width, height);
    width = (int) bounds.size.width;
    height = (int) bounds.size.height;
    CGFloat scaleFactor;

    LastKnownRequestProcessed(display)++;
    if (!width || !height) {
	return BadDrawable;
    }

    if (!TkMacOSXSetupDrawingContext(dst, gc, &dc)) {
	TkMacOSXDbgMsg("Failed to setup drawing context.");
	return BadDrawable;
    }

    if (!dc.context) {
	TkMacOSXDbgMsg("Invalid destination drawable - no context.");
	return BadDrawable;
    }

    img = CreateCGImageFromDrawableRect(src, 0, src_x, src_y, width, height, &scaleFactor);

    if (img) {
	unsigned int w = (unsigned int) (CGImageGetWidth(img) / scaleFactor);
	unsigned int h = (unsigned int) (CGImageGetHeight(img) / scaleFactor);
	dstRect = CGRectMake(dst_x, dst_y, w, h);
	TkMacOSXDrawCGImage(dst, gc, dc.context, img,
		gc->foreground, gc->background, dstRect);
	CFRelease(img);
    } else {
	TkMacOSXDbgMsg("Failed to construct CGImage.");
    }

    TkMacOSXRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyPlane --
 *
 *	Copies a bitmap from a source drawable to a destination drawable. The
 *	plane argument specifies which bit plane of the source contains the
 *	bitmap. Note that this implementation ignores the gc->function.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the destination drawable.
 *
 *----------------------------------------------------------------------
 */

int
XCopyPlane(
    Display *display,		/* Display. */
    Drawable src,		/* Source drawable. */
    Drawable dst,		/* Destination drawable. */
    GC gc,				/* GC to use. */
    int src_x,			/* X & Y, width & height */
    int src_y,			/* define the source rectangle */
    unsigned int width,	/* that will be copied. */
    unsigned int height,
    int dest_x,			/* Dest X & Y on dest rect. */
    int dest_y,
    unsigned long plane)	/* Which plane to copy. */
{
    TkMacOSXDrawingContext dc;
    MacDrawable *srcDraw = (MacDrawable *)src;
    MacDrawable *dstDraw = (MacDrawable *)dst;
    CGRect srcRect, dstRect;
    LastKnownRequestProcessed(display)++;
    if (!width || !height) {
	/* TkMacOSXDbgMsg("Drawing of empty area requested"); */
	return BadDrawable;
    }
    if (plane != 1) {
	Tcl_Panic("Unexpected plane specified for XCopyPlane");
    }
    if (srcDraw->flags & TK_IS_PIXMAP) {
	if (!TkMacOSXSetupDrawingContext(dst, gc, &dc)) {
	    return BadDrawable;
	}

	CGContextRef context = dc.context;

	if (context) {
	    CGImageRef img = CreateCGImageFromPixmap(src);

	    if (img) {
		TkpClipMask *clipPtr = (TkpClipMask *) gc->clip_mask;
		unsigned long imageBackground  = gc->background;

		if (clipPtr && clipPtr->type == TKP_CLIP_PIXMAP) {
		    srcRect = CGRectMake(src_x, src_y, width, height);
		    CGImageRef mask = CreateCGImageFromPixmap(
			    clipPtr->value.pixmap);
		    CGImageRef submask = CGImageCreateWithImageInRect(
			    img, srcRect);
		    CGRect rect = CGRectMake(dest_x, dest_y, width, height);

		    rect = CGRectOffset(rect, dstDraw->xOff, dstDraw->yOff);
		    CGContextSaveGState(context);

		    /*
		     * Move the origin of the destination to top left.
		     */

		    CGContextTranslateCTM(context,
			    0, rect.origin.y + CGRectGetMaxY(rect));
		    CGContextScaleCTM(context, 1, -1);

		    /*
		     * Fill with the background color, clipping to the mask.
		     */

		    CGContextClipToMask(context, rect, submask);
		    TkMacOSXSetColorInContext(gc, gc->background, dc.context);
		    CGContextFillRect(context, rect);

		    /*
		     * Fill with the foreground color, clipping to the
		     * intersection of img and mask.
		     */

		    CGImageRef subimage = CGImageCreateWithImageInRect(
			    img, srcRect);
		    CGContextClipToMask(context, rect, subimage);
		    TkMacOSXSetColorInContext(gc, gc->foreground, context);
		    CGContextFillRect(context, rect);
		    CGContextRestoreGState(context);
		    CGImageRelease(img);
		    CGImageRelease(mask);
		    CGImageRelease(submask);
		    CGImageRelease(subimage);
		} else {
		    dstRect = CGRectMake(dest_x, dest_y, width, height);
		    TkMacOSXDrawCGImage(dst, gc, dc.context, img,
			    gc->foreground, imageBackground, dstRect);
		    CGImageRelease(img);
		}
	    } else {
		/* no image */
		TkMacOSXDbgMsg("Invalid source drawable");
	    }
	} else {
	    TkMacOSXDbgMsg("Invalid destination drawable - "
		    "could not get a bitmap context.");
	}
	TkMacOSXRestoreDrawingContext(&dc);
	return Success;
    } else {
	/*
	 * Source drawable is a Window, not a Pixmap.
	 */

	return XCopyArea(display, src, dst, gc, src_x, src_y, width, height,
		dest_x, dest_y);
    }
}

/* ---------------------------------------------------------------------------*/

/*
 * Implementation of a Tk image type which provide access to NSImages
 * for use in buttons etc.
 */

/*
 * Forward declarations.
 */

typedef struct TkMacOSXNSImageInstance TkMacOSXNSImageInstance;
typedef struct TkMacOSXNSImageModel TkMacOSXNSImageModel;

/*
 * The following data structure represents a particular use of an nsimage
 * in a widget.
 */

struct TkMacOSXNSImageInstance {
    TkMacOSXNSImageModel *modelPtr;   /* Pointer to the model for the image. */
    NSImage *image;		  /* Pointer to an NSImage.*/
    TkMacOSXNSImageInstance *nextPtr;   /* First in the list of instances associated
				   * with this model. */
};

/*
 * The following data structure represents the model for an nsimage:
 */

struct TkMacOSXNSImageModel {
    Tk_ImageModel tkModel;	      /* Tk's token for image model. */
    Tcl_Interp *interp;		      /* Interpreter for application. */
    int width, height;		      /* Dimensions of the image. */
    int radius;                       /* Radius for rounded corners. */
    int ring;                         /* Thickness of the focus ring. */
    double alpha;                     /* Transparency, between 0.0 and 1.0*/
    char *imageName;                  /* Malloc'ed image name. */
    Tcl_Obj *sourceObj;               /* Describing the image. */
    Tcl_Obj *asObj;                   /* Interpretation of source */
    int	flags;	                      /* Sundry flags, defined below. */
    bool pressed;                     /* Image is for use in a pressed button.*/
    bool templ;                       /* Image is for use as a template.*/
    TkMacOSXNSImageInstance *instancePtr;   /* Start of list of instances associated
				       * with this model. */
    NSImage *image;                   /* The underlying NSImage object. */
    NSImage *darkModeImage;           /* A modified image to use in Dark Mode. */
};

/*
 * Bit definitions for the flags field of a TkMacOSXNSImageModel.
 * IMAGE_CHANGED:		1 means that the instances of this image need
 *				to be redisplayed.
 */

#define IMAGE_CHANGED		1

/*
 * The type record for nsimage images:
 */

static int		TkMacOSXNSImageCreate(Tcl_Interp *interp,
			    const char *name, Tcl_Size objc, Tcl_Obj *const objv[],
			    const Tk_ImageType *typePtr, Tk_ImageModel model,
			    void **clientDataPtr);
static void *TkMacOSXNSImageGet(Tk_Window tkwin, void *clientData);
static void		TkMacOSXNSImageDisplay(void *clientData,
			    Display *display, Drawable drawable,
			    int imageX, int imageY, int width,
			    int height, int drawableX,
			    int drawableY);
static void		TkMacOSXNSImageFree(void *clientData, Display *display);
static void		TkMacOSXNSImageDelete(void *clientData);

static Tk_ImageType TkMacOSXNSImageType = {
    "nsimage",			/* name of image type */
    TkMacOSXNSImageCreate,		/* createProc */
    TkMacOSXNSImageGet,		/* getProc */
    TkMacOSXNSImageDisplay,		/* displayProc */
    TkMacOSXNSImageFree,		/* freeProc */
    TkMacOSXNSImageDelete,		/* deleteProc */
    NULL,			/* postscriptPtr */
    NULL,			/* nextPtr */
    NULL
};

/*
 * Default values used for parsing configuration specifications:
 */
#define DEF_SOURCE   ""
#define DEF_AS       "name"
#define DEF_HEIGHT   "0"
#define DEF_WIDTH    "0"
#define DEF_RADIUS   "0"
#define DEF_RING     "0"
#define DEF_ALPHA    "1.0"
#define DEF_PRESSED  "0"
#define DEF_TEMPLATE "0"

static const Tk_OptionSpec systemImageOptions[] = {
    {TK_OPTION_STRING, "-source", NULL, NULL, DEF_SOURCE,
     offsetof(TkMacOSXNSImageModel, sourceObj), TCL_INDEX_NONE, 0, NULL, 0},
    {TK_OPTION_STRING, "-as", NULL, NULL, DEF_AS,
     offsetof(TkMacOSXNSImageModel, asObj), TCL_INDEX_NONE, 0, NULL, 0},
    {TK_OPTION_INT, "-width", NULL, NULL, DEF_WIDTH,
     TCL_INDEX_NONE, offsetof(TkMacOSXNSImageModel, width), 0, NULL, 0},
    {TK_OPTION_INT, "-height", NULL, NULL, DEF_HEIGHT,
     TCL_INDEX_NONE, offsetof(TkMacOSXNSImageModel, height), 0, NULL, 0},
    {TK_OPTION_INT, "-radius", NULL, NULL, DEF_RADIUS,
     TCL_INDEX_NONE, offsetof(TkMacOSXNSImageModel, radius), 0, NULL, 0},
    {TK_OPTION_INT, "-ring", NULL, NULL, DEF_RING,
     TCL_INDEX_NONE, offsetof(TkMacOSXNSImageModel, ring), 0, NULL, 0},
    {TK_OPTION_DOUBLE, "-alpha", NULL, NULL, DEF_ALPHA,
     TCL_INDEX_NONE, offsetof(TkMacOSXNSImageModel, alpha), 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-pressed", NULL, NULL, DEF_PRESSED,
     TCL_INDEX_NONE, offsetof(TkMacOSXNSImageModel, pressed), TK_OPTION_VAR(bool), NULL, 0},
    {TK_OPTION_BOOLEAN, "-template", NULL, NULL, DEF_TEMPLATE,
     TCL_INDEX_NONE, offsetof(TkMacOSXNSImageModel, templ), TK_OPTION_VAR(bool), NULL, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, TCL_INDEX_NONE, TCL_INDEX_NONE, 0, NULL, 0}
};

/*
 * The -as option specifies how the string provided in the -source
 * option should be interpreted as a description of an NSImage.
 * Below are the possible values and their meanings.  (The last two
 * provide the macOS icon for a particular file type.)
 */

static const char *sourceInterpretations[] = {
    "name",       /* A name for a named NSImage. */
    "file",       /* A path to an image file. */
    "path",       /* A path to a file whose type should be examined. */
    "filetype",   /* A file extension or 4-byte OSCode. */
};

enum {NAME_SOURCE, FILE_SOURCE, PATH_SOURCE, FILETYPE_SOURCE};


/*
 *----------------------------------------------------------------------
 *
 * TintImage --
 *
 *      Modify an NSImage by blending it with a color.  The transparent part of
 *      the image remains transparent.  The opaque part of the image is painted
 *      with the color, using the specified alpha value for the transparency of
 *      the color.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The appearance of the NSImage changes.
 *
 *----------------------------------------------------------------------
 */

static void TintImage(
    NSImage *image,
    NSColor *color,
    double alpha)
{
    NSSize size = [image size];
    NSRect rect = {NSZeroPoint, size};
    NSImage *mask = [[[NSImage alloc] initWithSize:size] retain];
    [mask lockFocus];
    [color set];
    NSRectFillUsingOperation(rect, NSCompositeCopy);
    [image drawInRect:rect
	     fromRect:rect
	    operation:NSCompositeDestinationIn
	     fraction:1.0];
    [mask unlockFocus];
    [image lockFocus];
    [mask drawInRect:rect
	    fromRect:rect
	   operation:NSCompositeSourceOver
	    fraction:alpha];
    [image unlockFocus];
    [mask release];
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXNSImageConfigureModel --
 *
 *	This function is called when an nsimage image is created or
 *	reconfigured.  It processes configuration options and resets any
 *	instances of the image.
 *
 * Results:
 *	A standard Tcl return value. If TCL_ERROR is returned then an error
 *	message is left in the modelPtr->interp's result.
 *
 * Side effects:
 *	Existing instances of the image will be redisplayed to match the new
 *	configuration options.
 *
 *----------------------------------------------------------------------
 */

static int
TkMacOSXNSImageConfigureModel(
    Tcl_Interp *interp,		   /* Interpreter to use for reporting errors. */
    TkMacOSXNSImageModel *modelPtr,    /* Pointer to data structure describing
				    * overall photo image to (re)configure. */
    Tcl_Size objc,			   /* Number of entries in objv. */
    Tcl_Obj *const objv[])	   /* Pairs of configuration options for image. */
{
    Tk_OptionTable optionTable = Tk_CreateOptionTable(interp, systemImageOptions);
    NSImage *newImage;
    Tcl_Obj *objPtr;
    static Tcl_Obj *asOption = NULL;
    int sourceInterpretation;
    NSString *source;
    int oldWidth = modelPtr->width, oldHeight = modelPtr->height;

    if (asOption == NULL) {
	asOption = Tcl_NewStringObj("-as", TCL_INDEX_NONE);
	Tcl_IncrRefCount(asOption);
    }

    modelPtr->width = 0;
    modelPtr->height = 0;
    if (Tk_SetOptions(interp, modelPtr, optionTable, objc, objv,
		      NULL, NULL, NULL) != TCL_OK){
	goto errorExit;
    }
    if (modelPtr->width == 0 && modelPtr->height == 0) {
	modelPtr->width = oldWidth;
	modelPtr->height = oldHeight;
    }

    if (modelPtr->sourceObj == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("-source is required.", TCL_INDEX_NONE));
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SYSTEM", "BAD_VALUE", NULL);
	goto errorExit;
    }

    objPtr = Tk_GetOptionValue(interp, (char *) modelPtr, optionTable,
				asOption, NULL);
    if (Tcl_GetIndexFromObj(interp, objPtr, sourceInterpretations, "option",
			    0, &sourceInterpretation) != TCL_OK) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
	    "Unknown interpretation for source in -as option.  "
	    "Should be name, file, path, or filetype.", TCL_INDEX_NONE));
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SYSTEM", "BAD_VALUE", NULL);
	goto errorExit;
    }

    source = [[NSString alloc] initWithUTF8String: Tcl_GetString(modelPtr->sourceObj)];
    switch (sourceInterpretation) {
    case NAME_SOURCE:
	newImage = [[NSImage imageNamed:source] copy];
	break;
    case FILE_SOURCE:
	newImage = [[NSImage alloc] initWithContentsOfFile:source];
	break;
    case PATH_SOURCE:
	newImage = [[NSWorkspace sharedWorkspace] iconForFile:source];
	break;
    case FILETYPE_SOURCE:
	newImage = TkMacOSXIconForFileType(source);
	break;
    default:
	newImage = NULL;
	break;
    }
    [source release];
    if (newImage) {
	NSSize size = NSMakeSize(modelPtr->width - 2*modelPtr->ring,
				 modelPtr->height - 2*modelPtr->ring);
	[modelPtr->image release];
	[modelPtr->darkModeImage release];
	newImage.size = size;
	modelPtr->image = [newImage retain];
	if (modelPtr->templ) {
	    newImage.template = YES;
	}
	modelPtr->darkModeImage = [[newImage copy] retain];
	if ([modelPtr->darkModeImage isTemplate]) {

	    /*
	     * For a template image the Dark Mode version should be white.
	     */

	    NSRect rect = {NSZeroPoint, size};
	    [modelPtr->darkModeImage lockFocus];
	    [[NSColor whiteColor] set];
	    NSRectFillUsingOperation(rect, NSCompositeSourceAtop);
	    [modelPtr->darkModeImage unlockFocus];
	} else if (modelPtr->pressed) {

	    /*
	     * Non-template pressed images are darker in Light Mode and lighter
	     * in Dark Mode.
	     */

	    TintImage(modelPtr->image, [NSColor blackColor], 0.2);
	    TintImage(modelPtr->darkModeImage, [NSColor whiteColor], 0.5);
	}
    } else {
	switch(sourceInterpretation) {
	case NAME_SOURCE:
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("Unknown named NSImage.\n"
		"Try omitting ImageName, "
		"e.g. use NSCaution for NSImageNameCaution.", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "IMAGE", "SYSTEM", "BAD_VALUE", NULL);
	    goto errorExit;
	case FILE_SOURCE:
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"Failed to load image file.\n", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "IMAGE", "SYSTEM", "BAD_VALUE", NULL);
	    goto errorExit;
	default:
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"Unrecognized file type.\n"
		"If using a filename extension, do not include the dot.\n", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "IMAGE", "SYSTEM", "BAD_VALUE", NULL);
	    goto errorExit;
	}
    }

    /*
     * Set the width and height.  If only one is specified, set the other one
     * so as to preserve the aspect ratio.  If neither is specified, match the
     * size of the image.
     */

    if (modelPtr->width == 0 && modelPtr->height == 0) {
	CGSize size = [modelPtr->image size];
	modelPtr->width = (int) size.width;
	modelPtr->height = (int) size.height;
    } else {
	CGSize size = [modelPtr->image size], newsize;
	CGFloat aspect = size.width && size.height ?
	     size.height / size.width : 1;
	if (modelPtr->width == 0) {
	    modelPtr->width = (int) ((CGFloat)(modelPtr->height) / aspect);
	} else if (modelPtr->height == 0) {
	    modelPtr->height = (int) ((CGFloat)(modelPtr->width) * aspect);
	}
	newsize = NSMakeSize(modelPtr->width, modelPtr->height);
	modelPtr->image.size = newsize;
	modelPtr->darkModeImage.size = newsize;
    }

    /*
     * Inform the generic image code that the image has (potentially) changed.
     */

    Tk_ImageChanged(modelPtr->tkModel, 0, 0, modelPtr->width,
	    modelPtr->height, modelPtr->width, modelPtr->height);
    modelPtr->flags &= ~IMAGE_CHANGED;

    return TCL_OK;

  errorExit:
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXNSImageObjCmd --
 *
 *	This function implements the configure and cget commands for an
 *	nsimage instance.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	The image may be reconfigured.
 *
 *----------------------------------------------------------------------
 */

int
TkMacOSXNSImageObjCmd(
    void *clientData,	/* Information about the image model. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    TkMacOSXNSImageModel *modelPtr = (TkMacOSXNSImageModel *)clientData;
    Tk_OptionTable optionTable = Tk_CreateOptionTable(interp, systemImageOptions);
    static const char *const options[] = {"cget", "configure", NULL};
    enum {CGET, CONFIGURE};
    Tcl_Obj *objPtr;
    int index;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObjStruct(interp, objv[1], options,
	    sizeof(char *), "option", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_Preserve(modelPtr);
    switch (index) {
    case CGET:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option");
	    return TCL_ERROR;
	}
	objPtr = Tk_GetOptionValue(interp, (char *)modelPtr, optionTable,
		objv[2], NULL);
	if (objPtr == NULL) {
	    goto error;
	}
	Tcl_SetObjResult(interp, objPtr);
	break;
    case CONFIGURE:
	if (objc == 2) {
	    objPtr = Tk_GetOptionInfo(interp, (char *)modelPtr, optionTable,
				     NULL, NULL);
	    if (objPtr == NULL) {
		goto error;
	    }
	    Tcl_SetObjResult(interp, objPtr);
	    break;
	} else if (objc == 3) {
	    objPtr = Tk_GetOptionInfo(interp, (char *)modelPtr, optionTable,
				     objv[2], NULL);
	    if (objPtr == NULL) {
		goto error;
	    }
	    Tcl_SetObjResult(interp, objPtr);
	    break;
	} else {
	    TkMacOSXNSImageConfigureModel(interp, modelPtr, objc - 2, objv + 2);
	    break;
	}
    default:
	break;
    }

    Tcl_Release(modelPtr);
    return TCL_OK;

 error:
    Tcl_Release(modelPtr);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXNSImageCreate --
 *
 *	Allocate and initialize an nsimage model.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	The data structure for a new image is allocated.
 *
 *----------------------------------------------------------------------
 */

static int
TkMacOSXNSImageCreate(
    Tcl_Interp *interp,		 /* Interpreter for application using image. */
    const char *name,		 /* Name to use for image. */
    Tcl_Size objc,			 /* Number of arguments. */
    Tcl_Obj *const objv[],	 /* Argument strings for options (not
				  * including image name or type). */
    TCL_UNUSED(const Tk_ImageType *), /* typePtr */
    Tk_ImageModel model,	 /* Token for image, to be used in callbacks. */
    void **clientDataPtr)	 /* Store manager's token for image here; it
				  * will be returned in later callbacks. */
{
    TkMacOSXNSImageModel *modelPtr;
    Tk_OptionTable optionTable = Tk_CreateOptionTable(interp, systemImageOptions);

    modelPtr = (TkMacOSXNSImageModel *)ckalloc(sizeof(TkMacOSXNSImageModel));
    modelPtr->tkModel = model;
    modelPtr->interp = interp;
    modelPtr->imageName = (char *)ckalloc(strlen(name) + 1);
    strcpy(modelPtr->imageName, name);
    modelPtr->flags = 0;
    modelPtr->instancePtr = NULL;
    modelPtr->image = NULL;
    modelPtr->darkModeImage = NULL;
    modelPtr->sourceObj = NULL;
    modelPtr->asObj = NULL;

    /*
     * Process configuration options given in the image create command.
     */

    if (Tk_InitOptions(interp, (char *) modelPtr, optionTable, NULL) != TCL_OK
	|| TkMacOSXNSImageConfigureModel(interp, modelPtr, objc, objv) != TCL_OK) {
	TkMacOSXNSImageDelete(modelPtr);
	return TCL_ERROR;
    }
    Tcl_CreateObjCommand(interp, name, TkMacOSXNSImageObjCmd, modelPtr, NULL);
    *clientDataPtr = modelPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXNSImageGet --
 *
 *	Allocate and initialize an nsimage instance.
 *
 * Results:
 *	The return value is a token for the image instance, which is used in
 *	future callbacks to ImageDisplay and ImageFree.
 *
 * Side effects:
 *	A new new nsimage instance is created.
 *
 *----------------------------------------------------------------------
 */

static void *
TkMacOSXNSImageGet(
    TCL_UNUSED(Tk_Window),      /* tkwin */
    void *clientData)	/* Pointer to TkMacOSXNSImageModel for image. */
{
    TkMacOSXNSImageModel *modelPtr = (TkMacOSXNSImageModel *) clientData;
    TkMacOSXNSImageInstance *instPtr;

    instPtr = (TkMacOSXNSImageInstance *)ckalloc(sizeof(TkMacOSXNSImageInstance));
    instPtr->modelPtr = modelPtr;
    return instPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXNSImageDisplay --
 *
 *	Display or redisplay an nsimage in the given drawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The image gets drawn.
 *
 *----------------------------------------------------------------------
 */

static void
TkMacOSXNSImageDisplay(
    void *clientData,	/* Pointer to TkMacOSXNSImageInstance for image. */
    TCL_UNUSED(Display *),      /* display */
    Drawable drawable,		/* Where to draw or redraw image. */
    int imageX, int imageY,	/* Origin of area to redraw, relative to
				 * origin of image. */
    int width, int height,	/* Dimensions of area to redraw. */
    int drawableX, int drawableY)
				/* Coordinates in drawable corresponding to
				 * imageX and imageY. */
{
    MacDrawable *macWin = (MacDrawable *) drawable;
    Tk_Window tkwin = (Tk_Window) macWin->winPtr;
    TkMacOSXNSImageInstance *instPtr = (TkMacOSXNSImageInstance *) clientData;
    TkMacOSXNSImageModel *modelPtr = instPtr->modelPtr;
    TkMacOSXDrawingContext dc;
    NSRect dstRect = NSMakeRect(macWin->xOff + drawableX,
				 macWin->yOff + drawableY, width, height);
    NSRect srcRect = NSMakeRect(imageX, imageY, width, height);
    NSImage *image = TkMacOSXInDarkMode(tkwin) ? modelPtr->darkModeImage :
	modelPtr->image;
    int ring = modelPtr->ring;
    int radius = modelPtr->radius;

    if (TkMacOSXSetupDrawingContext(drawable, NULL, &dc)) {
	if (dc.context) {
	    CGRect clipRect = CGRectMake(
		dstRect.origin.x - srcRect.origin.x + ring,
		dstRect.origin.y - srcRect.origin.y + ring,
		modelPtr->width - 2*ring,
		modelPtr->height - 2*ring);
	    CGPathRef path = CGPathCreateWithRoundedRect(clipRect, radius, radius, NULL);
	    CGContextSaveGState(dc.context);
	    CGContextBeginPath(dc.context);
	    CGContextAddPath(dc.context, path);
	    CGContextClip(dc.context);
	    NSGraphicsContext *savedContext = NSGraphicsContext.currentContext;
	    NSGraphicsContext.currentContext = [NSGraphicsContext
		graphicsContextWithCGContext:dc.context flipped:YES];
	    [image drawInRect:clipRect
		     fromRect:srcRect
		    operation:NSCompositeSourceOver
		     fraction:modelPtr->alpha
	       respectFlipped:YES
			hints:nil];
	    CGContextRestoreGState(dc.context);

	    /*
	     * Draw the focus ring.
	     */

	    if (ring) {
		CGRect ringRect = CGRectInset(clipRect, -ring, -ring);
		CGPathRef ringPath = CGPathCreateWithRoundedRect(ringRect,
		    radius + ring, radius + ring, NULL);
		CGContextSaveGState(dc.context);
		CGContextAddPath(dc.context, path);
		CGContextAddPath(dc.context, ringPath);
		CGContextSetFillColorWithColor(dc.context,
					       controlAccentColor().CGColor);
		CGContextEOFillPath(dc.context);
		CGContextRestoreGState(dc.context);
		CFRelease(ringPath);
	    }
	    CFRelease(path);
	    NSGraphicsContext.currentContext = savedContext;
	}
	TkMacOSXRestoreDrawingContext(&dc);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXNSImageFree --
 *
 *	Deallocate an instance of an nsimage.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information related to the instance is freed.
 *
 *----------------------------------------------------------------------
 */

static void
TkMacOSXNSImageFree(
    void *clientData,	/* Pointer to TkMacOSXNSImageInstance for instance. */
    TCL_UNUSED(Display *))	/* display */
{
    TkMacOSXNSImageInstance *instPtr = (TkMacOSXNSImageInstance *) clientData;
    ckfree(instPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXNSImageDelete --
 *
 *	Deallocate an nsimage model.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	 NSImages are released and memory is freed.
 *
 *----------------------------------------------------------------------
 */

static void
TkMacOSXNSImageDelete(
    void *clientData)	/* Pointer to TkMacOSXNSImageModel for image. When
				 * this function is called, no more instances
				 * exist. */
{
    TkMacOSXNSImageModel *modelPtr = (TkMacOSXNSImageModel *) clientData;

    Tcl_DeleteCommand(modelPtr->interp, modelPtr->imageName);
    ckfree(modelPtr->imageName);
    Tcl_DecrRefCount(modelPtr->sourceObj);
    Tcl_DecrRefCount(modelPtr->asObj);
    [modelPtr->image release];
    [modelPtr->darkModeImage release];
    ckfree(modelPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXNSImage_Init --
 *
 *	Adds the TkMacOSXNSImage type to Tk.
 *
 * Results:
 *	Returns a standard Tcl completion code, and leaves an error message in
 *	the interp's result if an error occurs.
 *
 * Side effects:
 *	Creates the image create nsrect ...  command.
 *
 *----------------------------------------------------------------------
 */

int
TkMacOSXNSImage_Init(
    TCL_UNUSED(Tcl_Interp *))	 /* interp */
{
    Tk_CreateImageType(&TkMacOSXNSImageType);
    return 1;
}

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
