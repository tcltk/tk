/*
 * tkMacOSXImage.c --
 *
 *	The code in this file provides an interface for XImages,
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright 2001-2009, Apple Inc.
 * Copyright (c) 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright 2017-2018 Marc Culler.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkMacOSXConstants.h"
#include "xbytes.h"

#pragma mark XImage handling

int
_XInitImageFuncPtrs(
    XImage *image)
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

static void ReleaseData(void *info, const void *data, size_t size) {
    ckfree(info);
}

CGImageRef
TkMacOSXCreateCGImageWithXImage(
    XImage *image)
{
    CGImageRef img = NULL;
    size_t bitsPerComponent, bitsPerPixel;
    size_t len = image->bytes_per_line * image->height;
    const CGFloat *decode = NULL;
    CGBitmapInfo bitmapInfo;
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
	if (image->bitmap_bit_order != MSBFirst) {
	    char *srcPtr = image->data + image->xoffset;
	    char *endPtr = srcPtr + len;
	    char *destPtr = (data = ckalloc(len));

	    while (srcPtr < endPtr) {
		*destPtr++ = xBitReverseTable[(unsigned char)(*(srcPtr++))];
	    }
	} else {
	    data = memcpy(ckalloc(len), image->data + image->xoffset, len);
	}
	if (data) {
	    provider = CGDataProviderCreateWithData(data, data, len,
		    releaseData);
	}
	if (provider) {
	    img = CGImageMaskCreate(image->width, image->height,
		    bitsPerComponent, bitsPerPixel, image->bytes_per_line,
		    provider, decode, 0);
	}
    } else if ((image->format == ZPixmap) && (image->bits_per_pixel == 32)) {
	/*
	 * Color image
	 */

	CGColorSpaceRef colorspace = CGColorSpaceCreateDeviceRGB();

	if (image->width == 0 && image->height == 0) {
	    /*
	     * CGCreateImage complains on early macOS releases.
	     */

	    return NULL;
	}
	bitsPerComponent = 8;
	bitsPerPixel = 32;
	bitmapInfo = (image->byte_order == MSBFirst ?
		kCGBitmapByteOrder32Little : kCGBitmapByteOrder32Big);
	bitmapInfo |= kCGImageAlphaLast;
	data = memcpy(ckalloc(len), image->data + image->xoffset, len);
	if (data) {
	    provider = CGDataProviderCreateWithData(data, data, len,
		    releaseData);
	}
	if (provider) {
	    img = CGImageCreate(image->width, image->height, bitsPerComponent,
		    bitsPerPixel, image->bytes_per_line, colorspace, bitmapInfo,
		    provider, decode, 0, kCGRenderingIntentDefault);
	    CFRelease(provider);
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
 * XGetImage --
 *
 *	This function copies data from a pixmap or window into an XImage.  It
 *      is essentially never used. At one time it was called by
 *      pTkImgPhotoDisplay, but that is no longer the case. Currently it is
 *      called two places, one of which is requesting an XY image which we do
 *      not support.  It probably does not work correctly -- see the comments
 *      for TkMacOSXBitmapRepFromDrawableRect.
 *
 * Results:
 *	Returns a newly allocated XImage containing the data from the given
 *	rectangle of the given drawable, or NULL if the XImage could not be
 *	constructed.  NOTE: If we are copying from a window on a Retina
 *	display, the dimensions of the XImage will be 2*width x 2*height.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
struct pixel_fmt {int r; int g; int b; int a;};
static struct pixel_fmt bgra = {2, 1, 0, 3};
static struct pixel_fmt abgr = {3, 2, 1, 0};

XImage *
XGetImage(
    Display *display,
    Drawable drawable,
    int x,
    int y,
    unsigned int width,
    unsigned int height,
    unsigned long plane_mask,
    int format)
{
    NSBitmapImageRep* bitmap_rep = NULL;
    NSUInteger bitmap_fmt = 0;
    XImage* imagePtr = NULL;
    char* bitmap = NULL;
    char R, G, B, A;
    int depth = 32, offset = 0, bitmap_pad = 0;
    unsigned int bytes_per_row, size, row, n, m;
    unsigned int scalefactor=1, scaled_height=height, scaled_width=width;
    NSWindow *win = TkMacOSXDrawableWindow(drawable);
    static enum {unknown, no, yes} has_retina = unknown;

    if (win && has_retina == unknown) {
#ifdef __clang__
	has_retina = [win respondsToSelector:@selector(backingScaleFactor)] ?
		yes : no;
#else
	has_retina = no;
#endif
    }

    if (has_retina == yes) {
	/*
	 * We only allow scale factors 1 or 2, as Apple currently does.
	 */

#ifdef __clang__
	scalefactor = [win backingScaleFactor] == 2.0 ? 2 : 1;
#endif
	scaled_height *= scalefactor;
	scaled_width *= scalefactor;
    }

    if (format == ZPixmap) {
	if (width == 0 || height == 0) {
	    return NULL;
	}

	bitmap_rep = TkMacOSXBitmapRepFromDrawableRect(drawable,
		x, y, width, height);
	if (!bitmap_rep) {
	    TkMacOSXDbgMsg("XGetImage: Failed to construct NSBitmapRep");
	    return NULL;
	}
	bitmap_fmt = [bitmap_rep bitmapFormat];
	size = [bitmap_rep bytesPerPlane];
	bytes_per_row = [bitmap_rep bytesPerRow];
	bitmap = ckalloc(size);
	if (!bitmap
		|| (bitmap_fmt != 0 && bitmap_fmt != 1)
		|| [bitmap_rep samplesPerPixel] != 4
		|| [bitmap_rep isPlanar] != 0
		|| bytes_per_row < 4 * scaled_width
		|| size != bytes_per_row * scaled_height) {
	    TkMacOSXDbgMsg("XGetImage: Unrecognized bitmap format");
	    CFRelease(bitmap_rep);
	    return NULL;
	}
	memcpy(bitmap, (char *)[bitmap_rep bitmapData], size);
	CFRelease(bitmap_rep);

	/*
	 * When Apple extracts a bitmap from an NSView, it may be in either
	 * BGRA or ABGR format.  For an XImage we need RGBA.
	 */

	struct pixel_fmt pixel = bitmap_fmt == 0 ? bgra : abgr;

	for (row = 0, n = 0; row < scaled_height; row++, n += bytes_per_row) {
	    for (m = n; m < n + 4*scaled_width; m += 4) {
		R = *(bitmap + m + pixel.r);
		G = *(bitmap + m + pixel.g);
		B = *(bitmap + m + pixel.b);
		A = *(bitmap + m + pixel.a);

		*(bitmap + m)     = R;
		*(bitmap + m + 1) = G;
		*(bitmap + m + 2) = B;
		*(bitmap + m + 3) = A;
	    }
	}
	imagePtr = XCreateImage(display, NULL, depth, format, offset,
		(char*) bitmap, scaled_width, scaled_height,
		bitmap_pad, bytes_per_row);
	if (scalefactor == 2) {
	    imagePtr->pixelpower = 1;
	}
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
 *	Returns the 32 bit pixel value.
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

    if (image && image->data) {
	unsigned char *srcPtr = ((unsigned char*) image->data)
		+ (y * image->bytes_per_line)
		+ (((image->xoffset + x) * image->bits_per_pixel) / NBBY);

	switch (image->bits_per_pixel) {
	case 32:
	    r = (*((unsigned int*) srcPtr) >> 16) & 0xff;
	    g = (*((unsigned int*) srcPtr) >>  8) & 0xff;
	    b = (*((unsigned int*) srcPtr)      ) & 0xff;
	    /*if (image->byte_order == LSBFirst) {
		r = srcPtr[2]; g = srcPtr[1]; b = srcPtr[0];
	    } else {
		r = srcPtr[1]; g = srcPtr[2]; b = srcPtr[3];
	    }*/
	    break;
	case 16:
	    r = (*((unsigned short*) srcPtr) >> 7) & 0xf8;
	    g = (*((unsigned short*) srcPtr) >> 2) & 0xf8;
	    b = (*((unsigned short*) srcPtr) << 3) & 0xf8;
	    break;
	case 8:
	    r = (*srcPtr << 2) & 0xc0;
	    g = (*srcPtr << 4) & 0xc0;
	    b = (*srcPtr << 6) & 0xc0;
	    r |= r >> 2 | r >> 4 | r >> 6;
	    g |= g >> 2 | g >> 4 | g >> 6;
	    b |= b >> 2 | b >> 4 | b >> 6;
	    break;
	case 4: {
	    unsigned char c = (x % 2) ? *srcPtr : (*srcPtr >> 4);

	    r = (c & 0x04) ? 0xff : 0;
	    g = (c & 0x02) ? 0xff : 0;
	    b = (c & 0x01) ? 0xff : 0;
	    break;
	}
	case 1:
	    r = g = b = ((*srcPtr) & (0x80 >> (x % 8))) ? 0xff : 0;
	    break;
	}
    }
    return (PIXEL_MAGIC << 24) | (r << 16) | (g << 8) | b;
}

/*
 *----------------------------------------------------------------------
 *
 * ImagePutPixel --
 *
 *	Set a single pixel in an image.
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
	    unsigned char r = ((pixel & image->red_mask)   >> 16) & 0xff;
	    unsigned char g = ((pixel & image->green_mask) >>  8) & 0xff;
	    unsigned char b = ((pixel & image->blue_mask)       ) & 0xff;
	    switch (image->bits_per_pixel) {
	    case 16:
		*((unsigned short*) dstPtr) = ((r & 0xf8) << 7) |
			((g & 0xf8) << 2) | ((b & 0xf8) >> 3);
		break;
	    case 8:
		*dstPtr = ((r & 0xc0) >> 2) | ((g & 0xc0) >> 4) |
			((b & 0xc0) >> 6);
		break;
	    case 4: {
		unsigned char c = ((r & 0x80) >> 5) | ((g & 0x80) >> 6) |
			((b & 0x80) >> 7);
		*dstPtr = (x % 2) ? ((*dstPtr & 0xf0) | (c & 0x0f)) :
			((*dstPtr & 0x0f) | ((c << 4) & 0xf0));
		break;
		}
	    case 1:
		*dstPtr = ((r|g|b) & 0x80) ? (*dstPtr | (0x80 >> (x % 8))) :
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
    Visual* visual,
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

    display->request++;
    ximage = ckalloc(sizeof(XImage));

    ximage->height = height;
    ximage->width = width;
    ximage->depth = depth;
    ximage->xoffset = offset;
    ximage->format = format;
    ximage->data = data;
    ximage->obdata = NULL;

    /*
     * The default pixelpower is 0.  This must be explicitly set to 1 in the
     * case of an XImage extracted from a Retina display.
     */

    ximage->pixelpower = 0;

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
	 * Use 16 byte alignment for best Quartz perfomance.
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
 * TkPutImage, XPutImage --
 *
 *	Copies a rectangular subimage of an XImage into a drawable.  Currently
 *      this is only called by TkImgPhotoDisplay, using a Window as the
 *      drawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws the image on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XPutImage(
    Display* display,		/* Display. */
    Drawable drawable,		/* Drawable to place image on. */
    GC gc,			/* GC to use. */
    XImage* image,		/* Image to place. */
    int src_x,			/* Source X & Y. */
    int src_y,
    int dest_x,			/* Destination X & Y. */
    int dest_y,
    unsigned int width,	        /* Same width & height for both */
    unsigned int height)	/* distination and source. */
{
    TkMacOSXDrawingContext dc;
    MacDrawable *macDraw = (MacDrawable *) drawable;

    display->request++;
    if (!TkMacOSXSetupDrawingContext(drawable, gc, 1, &dc)) {
	return BadDrawable;
    }
    if (dc.context) {
	CGRect bounds, srcRect, dstRect;
	CGImageRef img = TkMacOSXCreateCGImageWithXImage(image);

	/*
	 * The CGContext for a pixmap is RGB only, with A = 0.
	 */

	if (!(macDraw->flags & TK_IS_PIXMAP)) {
	    CGContextSetBlendMode(dc.context, kCGBlendModeSourceAtop);
	}
	if (img) {

	    /*
	     * If the XImage has big pixels, the source is rescaled to reflect
	     * the actual pixel dimensions.  This is not currently used, but
	     * could arise if the image were copied from a retina monitor and
	     * redrawn on an ordinary monitor.
	     */

	    int pp = image->pixelpower;

	    bounds = CGRectMake(0, 0, image->width, image->height);
	    srcRect = CGRectMake(src_x<<pp, src_y<<pp, width<<pp, height<<pp);
	    dstRect = CGRectMake(dest_x, dest_y, width, height);
	    TkMacOSXDrawCGImage(drawable, gc, dc.context,
				img, gc->foreground, gc->background,
				bounds, srcRect, dstRect);
	    CFRelease(img);
	} else {
	    TkMacOSXDbgMsg("Invalid source drawable");
	}
    } else {
	TkMacOSXDbgMsg("Invalid destination drawable");
    }
    TkMacOSXRestoreDrawingContext(&dc);
    return Success;
}

int
TkPutImage(
    unsigned long *colors,	/* Array of pixel values used by this image.
				 * May be NULL. */
    int ncolors,		/* Number of colors used, or 0. */
    Display *display,
    Drawable d,			/* Destination drawable. */
    GC gc,
    XImage *image,		/* Source image. */
    int src_x, int src_y,	/* Offset of subimage. */
    int dest_x, int dest_y,	/* Position of subimage origin in drawable. */
    unsigned int width, unsigned int height)
				/* Dimensions of subimage. */
{
    return XPutImage(display, d, gc, image, src_x, src_y, dest_x, dest_y, width, height);
}


/* ---------------------------------------------------------------------------*/

/*
 * Implementation of a system image type to provide access to named NSImages
 * provided by macOS for use in buttons etc.
 */

/*
 * Forward declarations.
 */

typedef struct SystemImageInstance SystemImageInstance;
typedef struct SystemImageMaster SystemImageMaster;

/*
 * The following data structure represents a particular use of a particular
 * system image.
 */

struct SystemImageInstance {
    SystemImageMaster *masterPtr; /* Pointer to the master for the image. */
    NSImage *image;		  /* Pointer to a named NSImage.*/
    SystemImageInstance *nextPtr; /* First in the list of instances associated
				   * with this master. */
};

/*
 * The following data structure represents the master for a system image:
 */

struct SystemImageMaster {
    Tk_ImageMaster tkMaster;	      /* Tk's token for image master. */
    Tcl_Interp *interp;		      /* Interpreter for application. */
    int width, height;		      /* Dimensions of the image. */
    char *imageName ;                 /* Malloc'ed image name. */
    char *systemName;       	      /* Malloc'ed name of the NSimage. */
    int	flags;			      /* Sundry flags, defined below. */
    SystemImageInstance *instancePtr; /* First in the list of instances associated
				       * with this master. */
    NSImage *image;                   /* The underlying NSImage object. */
};

/*
 * Bit definitions for the flags field of a SystemImageMaster.
 * TEMPLATE:			1 means that this is a Template image
 * IMAGE_CHANGED:		1 means that the instances of this image need
 *				to be redisplayed.
 */

#define TEMPLATE		1
#define IMAGE_CHANGED		2

/*
 * The type record for system images:
 */

static int		SystemImageCreate(Tcl_Interp *interp,
			    const char *name, int argc, Tcl_Obj *const objv[],
			    const Tk_ImageType *typePtr, Tk_ImageMaster master,
			    ClientData *clientDataPtr);
static ClientData	SystemImageGet(Tk_Window tkwin, ClientData clientData);
static void		SystemImageDisplay(ClientData clientData,
			    Display *display, Drawable drawable,
			    int imageX, int imageY, int width,
			    int height, int drawableX,
			    int drawableY);
static void		SystemImageFree(ClientData clientData, Display *display);
static void		SystemImageDelete(ClientData clientData);

static Tk_ImageType SystemImageType = {
    "system",			/* name */
    SystemImageCreate,		/* createProc */
    SystemImageGet,		/* getProc */
    SystemImageDisplay,		/* displayProc */
    SystemImageFree,		/* freeProc */
    SystemImageDelete,		/* deleteProc */
    NULL,			/* postscriptPtr */
    NULL,			/* nextPtr */
    NULL
};

/*
 * Information used for parsing configuration specifications:
 */
#define DEF_NAME    ""
#define DEF_HEIGHT  "32"
#define DEF_WIDTH   "32"

static const Tk_OptionSpec systemImageOptions[] = {
    {TK_OPTION_STRING, "-systemname", NULL, NULL, DEF_NAME,
     -1, Tk_Offset(SystemImageMaster, systemName), 0, NULL, 0},
    {TK_OPTION_INT, "-width", NULL, NULL, DEF_WIDTH,
     -1, Tk_Offset(SystemImageMaster, width), 0, NULL, 0},
    {TK_OPTION_INT, "-height", NULL, NULL, DEF_HEIGHT,
     -1, Tk_Offset(SystemImageMaster, height), 0, 0, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, -1, 0, NULL, 0}
};

/*
 *----------------------------------------------------------------------
 *
 * SystemImageConfigureMaster --
 *
 *	This function is called when a system image is created or reconfigured.
 *	It processes configuration options and resets any instances of the
 *	image.
 *
 * Results:
 *	A standard Tcl return value. If TCL_ERROR is returned then an error
 *	message is left in the masterPtr->interp's result.
 *
 * Side effects:
 *	Existing instances of the image will be redisplayed to match the new
 *	configuration options.
 *
 *----------------------------------------------------------------------
 */

static int
SystemImageConfigureMaster(
    Tcl_Interp *interp,		   /* Interpreter to use for reporting errors. */
    SystemImageMaster *masterPtr,  /* Pointer to data structure describing
				    * overall photo image to (re)configure. */
    int objc,			   /* Number of entries in objv. */
    Tcl_Obj *const objv[])	   /* Pairs of configuration options for image. */
{
    Tk_OptionTable optionTable = Tk_CreateOptionTable(interp, systemImageOptions);
    SystemImageInstance *instancePtr;
    
    if (Tk_SetOptions(interp, (char *) masterPtr, optionTable, objc, objv,
		      NULL, NULL, NULL) != TCL_OK){
	goto errorExit;
    }

    if (masterPtr->systemName == NULL || masterPtr->systemName[0] == '0') {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("-systemname is required.", -1));
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SYSTEM",
			 "BAD_VALUE", NULL);
	goto errorExit;
    }
    NSString *name = [[NSString alloc] initWithUTF8String: masterPtr->systemName];
    masterPtr->image = [NSImage imageNamed:(NSImageName)name];
    [name release];
    if (masterPtr->image) {
	NSSize size = NSMakeSize(masterPtr->width, masterPtr->height);
	[masterPtr->image setSize:size];
    } else {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Unknown system image name.\n"
	    "Try omitting ImageName, e.g. use NSCaution for NSImageNameCaution.", -1));
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SYSTEM",
			 "BAD_VALUE", NULL);
	goto errorExit;
    }

    /*
     * Cycle through all of the instances of this image, regenerating the
     * information for each instance. Then force the image to be redisplayed
     * everywhere that it is used.
     */

    for (instancePtr = masterPtr->instancePtr; instancePtr != NULL;
	    instancePtr = instancePtr->nextPtr) {
	// Photo images do this.  What do we need to do here?
    }

    /*
     * Inform the generic image code that the image has (potentially) changed.
     */

    Tk_ImageChanged(masterPtr->tkMaster, 0, 0, masterPtr->width,
	    masterPtr->height, masterPtr->width, masterPtr->height);
    masterPtr->flags &= ~IMAGE_CHANGED;

    return TCL_OK;

  errorExit:
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * SystemImageObjCmd --
 *
 *	This function implements the configure and cget commands for a 
 *	system image.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	The image may be reconfigured.
 *
 *----------------------------------------------------------------------
 */

static int
SystemImageObjCmd(
    ClientData clientData,	/* Information about the image master. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    SystemImageMaster *masterPtr = clientData;
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
    Tcl_Preserve(masterPtr);
    switch (index) {
    case CGET:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option");
	    return TCL_ERROR;
	}
	objPtr = Tk_GetOptionValue(interp, (char *)masterPtr, optionTable,
		objv[2], NULL);
	if (objPtr == NULL) {
            goto error;
        }
        Tcl_SetObjResult(interp, objPtr);
	break;
    case CONFIGURE:
	if (objc == 2) {
	    objPtr = Tk_GetOptionInfo(interp, (char *)masterPtr, optionTable,
				     NULL, NULL);
	    if (objPtr == NULL) {
		goto error;
	    }
	    Tcl_SetObjResult(interp, objPtr);
	    break;
	} else if (objc == 3) {
	    objPtr = Tk_GetOptionInfo(interp, (char *)masterPtr, optionTable,
				     objv[2], NULL);
	    if (objPtr == NULL) {
		goto error;
	    }
	    Tcl_SetObjResult(interp, objPtr);
	    break;
	} else {
	    SystemImageConfigureMaster(interp, masterPtr, objc - 2, objv + 2);
	    break;
	}
    default:
	break;
    }

    Tcl_Release(masterPtr);
    return TCL_OK;

 error:
    Tcl_Release(masterPtr);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * SystemImageCreate --
 *
 *	This function is called by the Tk image code to create system images.
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
SystemImageCreate(
    Tcl_Interp *interp,		 /* Interpreter for application using image. */
    const char *name,		 /* Name to use for image. */
    int objc,			 /* Number of arguments. */
    Tcl_Obj *const objv[],	 /* Argument strings for options (doesn't
				  * include image name or type). */
    const Tk_ImageType *typePtr, /* Pointer to our type record (not used). */
    Tk_ImageMaster master,	 /* Token for image, to be used in callbacks. */
    ClientData *clientDataPtr)	 /* Store manager's token for image here; it
				  * will be returned in later callbacks. */
{
    SystemImageMaster *masterPtr;

    masterPtr = ckalloc(sizeof(SystemImageMaster));
    masterPtr->tkMaster = master;
    masterPtr->interp = interp;
    masterPtr->width = 32;
    masterPtr->height = 32;
    masterPtr->imageName = ckalloc(strlen(name) + 1);
    strcpy(masterPtr->imageName, name);
    masterPtr->systemName = NULL;
    masterPtr->flags = 0;
    masterPtr->instancePtr = NULL;
    masterPtr->image = NULL;
    Tcl_CreateObjCommand(interp, name, SystemImageObjCmd, masterPtr, NULL);
    *clientDataPtr = masterPtr;

    /*
     * Process configuration options given in the image create command.
     */

    if (SystemImageConfigureMaster(interp, masterPtr, objc, objv) != TCL_OK) {
	SystemImageDelete(masterPtr);
	return TCL_ERROR;
    }

    
    *clientDataPtr = masterPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * SystemImageGet --
 *
 *	This function is called by Tk when it is preparing to use a system
 *	image in a particular widget.
 *
 * Results:
 *	The return value is a token for the image instance, which is used in
 *	future callbacks to ImageDisplay and ImageFree.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static ClientData
SystemImageGet(
    Tk_Window tkwin,		/* Token for window in which image will be
				 * used. */
    ClientData clientData)	/* Pointer to SystemImageMaster for image. */
{
    SystemImageMaster *masterPtr = (SystemImageMaster *) clientData;
    SystemImageInstance *instPtr;

    instPtr = ckalloc(sizeof(SystemImageInstance));
    instPtr->masterPtr = masterPtr;
    return instPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * SystemImageDisplay --
 *
 *	This function is invoked to redisplay part or all of an image in a
 *	given drawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The image gets partially redrawn, as an "X" that shows the exact
 *	redraw area.
 *
 *----------------------------------------------------------------------
 */

static void
SystemImageDisplay(
    ClientData clientData,	/* Pointer to SystemImageInstance for image. */
    Display *display,		/* Display to use for drawing. */
    Drawable drawable,		/* Where to draw or redraw image. */
    int imageX, int imageY,	/* Origin of area to redraw, relative to
				 * origin of image. */
    int width, int height,	/* Dimensions of area to redraw. */
    int drawableX, int drawableY)
				/* Coordinates in drawable corresponding to
				 * imageX and imageY. */
{
    MacDrawable *macWin = (MacDrawable *) drawable;
    SystemImageInstance *instPtr = (SystemImageInstance *) clientData;
    SystemImageMaster *masterPtr = instPtr->masterPtr; 
    TkMacOSXDrawingContext dc;
    NSImage *image = instPtr->masterPtr->image;
    NSRect dstRect = NSMakeRect(macWin->xOff + drawableX,
				 macWin->yOff + drawableY, width, height);
    NSRect srcRect = NSMakeRect(imageX, imageY, width, height);

    if (TkMacOSXSetupDrawingContext(drawable, NULL, 1, &dc)) {
	if (dc.context) {

	    /*
	     * There is only one global instance of each named NSImage, which
	     * may be shared by many SystemImageMasters.  So we need to set the
	     * size of the image to the size of the master before drawing.
	     * Changing the size of an image invalidates all of the cached
	     * representations.  It may be slightly more efficient to not
	     * change the size if the current size is correct.
	     */

	    NSSize savedSize = [masterPtr->image size];
	    NSSize size = NSMakeSize(masterPtr->width, masterPtr->height);
	    int sizeChanged = (savedSize.width != size.width ||
	    		       savedSize.height != size.height);
	    NSGraphicsContext *savedContext = NSGraphicsContext.currentContext;
	    NSGraphicsContext.currentContext = [NSGraphicsContext
		graphicsContextWithCGContext:dc.context flipped: YES];
	    if (sizeChanged) {
		[masterPtr->image setSize:size];
	    }
	    [image drawInRect:dstRect
		     fromRect:srcRect
		    operation:NSCompositeSourceOver
		     fraction:1.0
	       respectFlipped:YES
			hints:nil];
	    NSGraphicsContext.currentContext = savedContext;
	}
	TkMacOSXRestoreDrawingContext(&dc);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SystemImageFree --
 *
 *	This function is called when an instance of an image is no longer
 *	used.
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
SystemImageFree(
    ClientData clientData,	/* Pointer to SystemImageInstance for instance. */
    Display *display)		/* Display where image was to be drawn. */
{
    SystemImageInstance *instPtr = (SystemImageInstance *) clientData;
    ckfree(instPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * SystemImageDelete --
 *
 *	This function is called to clean up a test image when an application
 *	goes away.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information about the image is deleted.
 *
 *----------------------------------------------------------------------
 */

static void
SystemImageDelete(
    ClientData clientData)	/* Pointer to SystemImageMaster for image. When
				 * this function is called, no more instances
				 * exist. */
{
    SystemImageMaster *masterPtr = (SystemImageMaster *) clientData;

    Tcl_DeleteCommand(masterPtr->interp, masterPtr->imageName);
    ckfree(masterPtr->imageName);
    ckfree(masterPtr->systemName);
    ckfree(masterPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXSystemImage_Init --
 *
 *	Adds the SystemImage type to Tk.
 *
 * Results:
 *	Returns a standard Tcl completion code, and leaves an error message in
 *	the interp's result if an error occurs.
 *
 * Side effects:
 *	Creates the command: image create system -systemname -width -height
 *
 *----------------------------------------------------------------------
 */

int
TkMacOSXSystemImage_Init(
    Tcl_Interp *interp)		/* Interpreter for application. */
{
    Tk_CreateImageType(&SystemImageType);
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
