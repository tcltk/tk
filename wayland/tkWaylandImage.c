/*
 * tkWaylandImage.c --
 *
 *	Image handling for Wayland backend using NanoVG.
 *	Provides conversion between Tk images and NanoVG images,
 *	and implements Xlib-compatible image functions for Wayland.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2017-2021 Marc Culler.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkPort.h"
#include "tkImgPhoto.h"
#include "tkColor.h"
#include "tkGlfwInt.h"
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#define NANOVG_GLES3  
#include "nanovg_gl_utils.h"

/*
 * Undefine X11 macro that conflicts with our implementation.
 * X11 headers define XDestroyImage as a macro that expands to:
 * (*((image)->f.destroy_image))(image)
 */
#ifdef XDestroyImage
#undef XDestroyImage
#endif

/*
 *----------------------------------------------------------------------
 *
 * Type Definitions
 *
 *----------------------------------------------------------------------
 */

/*
 * NanoVG image structure for internal tracking.
 */
typedef struct NVGImageData {
    int id;             /* NanoVG image ID (as returned by nvgCreateImage*) */
    int width;          /* Image width in pixels */
    int height;         /* Image height in pixels */
    int flags;          /* Image flags (repeat, etc.) */
    unsigned char *pixels;   /* CPU copy (RGBA) */
} NVGImageData;

/*
 * Pixel formats for image conversion.
 * Tk uses ARGB32, NanoVG uses RGBA.
 */
typedef struct ARGB32pixel_t {
    unsigned char blue;
    unsigned char green;
    unsigned char red;
    unsigned char alpha;
} ARGB32pixel;

typedef union pixel32_t {
    unsigned int uint;
    ARGB32pixel argb;
} pixel32;


/*
 *----------------------------------------------------------------------
 *
 * _XInitImageFuncPtrs --
 *
 *	Initialize XImage function pointers (Xlib compatibility).
 *
 * Results:
 *	Returns 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
_XInitImageFuncPtrs(
    TCL_UNUSED(XImage *))
{
    return 0;
}
/*
 *----------------------------------------------------------------------
 *
 * TkpPutRGBAImage --
 *
 *	Put RGBA image data to a drawable using NanoVG.
 *
 * Results:
 *	Returns 0 on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Draws image on drawable.
 *
 *----------------------------------------------------------------------
 */

int TkpPutRGBAImage(
    Display* display,
    Drawable drawable,
    GC gc,
    XImage* image,
    int src_x,  // assume this is 0 for now
    int src_y,  // assume this is 0 for now
    int dst_x,
    int dst_y,
    unsigned int width,
    unsigned int height)
{
    (void)display; (void)gc;
    if (TkWaylandDrawableIsPixmap(drawable)) {
	printf("TkpPutRGBAImage does not support drawing to pixmaps yet.");
    }
    if (src_x || src_y) {
	printf("Unexpected source offset\n");
	return 0;
    }
    TkWindow *winPtr = TkWaylandTkWindowFromDrawable(drawable);
    TkWaylandDrawingContext dc;
    NVGcontext *vg = TkGlfwGetNVGContext(drawable);
    int rc = TkGlfwBeginDraw(drawable, gc, &dc);
    if (rc != TCL_OK) {
	printf("TkGlfwBeginDraw failed\n");
	return TCL_ERROR;
    }
    int imageID = nvgCreateImageRGBA(vg, width, height, 0, image->data);
    NVGpaint paint = nvgImagePattern(vg, 0, 0, width, height,
				     0.0f, imageID, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, dst_x, dst_y, width, height);
    nvgFillPaint(vg, paint);
    nvgFill(vg);
    TkGlfwEndDraw(&dc);
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * XGetImage --
 *
 *	Retrieve image data from drawable (Xlib compatibility). Stub function
 *      for compatibility.
 *
 * Results:
 *	Returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XImage*
XGetImage(
    Display *display,
    Drawable drawable,
    int x, int y,
    unsigned int width,
    unsigned int height,
    unsigned long plane_mask,
    int format)
{
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyArea --
 *
 *	Copy rectangular area from one drawable to another. Stub function
 *      for compatibility. 
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Copies pixel data between drawables.
 *
 *----------------------------------------------------------------------
 */

int
XCopyArea(
    Display  *display,
    Drawable  src,
    Drawable  dst,
    GC        gc,
    int       src_x, int src_y,
    unsigned  width, unsigned height,
    int       dst_x, int dst_y)
{
    printf("XCopyArea: src = %lx; dst = %lx\n", src, dst);

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateBitmapFromData --
 *
 *	Constructs a 1-bit deep Pixmap from raw inline byte data. 
 *	Used heavily by Tk's text engine to instantiate stippling structures.
 *
 * Results:
 *	A unique Pixmap identifier.
 *
 * Side effects:
 *	Allocates a backend Pixmap structure and assigns a rolling ID.
 *
 *----------------------------------------------------------------------
 */

Pixmap
XCreateBitmapFromData(
    Display      *display,
    Drawable      d,
    const char   *data,
    unsigned int  width,
    unsigned int  height)
{
    /* We pass 1 for the depth parameter since this is a 1-bit bitmap. */
    Pixmap bitmap = Tk_GetPixmap(display, d, (int)width, (int)height, 1);
     
    return bitmap;
}


/*
 *----------------------------------------------------------------------
 *
 * XCopyPlane --
 *
 *	Copy a single bit-plane from src to dst, mapping 1-bits to the
 *	GC foreground color and 0-bits to the GC background color.
 *      Stub function for compatibility.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XCopyPlane(
    Display      *display,
    Drawable      src,
    Drawable      dst,
    GC            gc,
    int           src_x,
    int           src_y,
    unsigned int  width,
    unsigned int  height,
    int           dest_x,
    int           dest_y,
    TCL_UNUSED(unsigned long)) /* plane */
{
    return Success;
}


/*
 *----------------------------------------------------------------------
 *
 * XPutImage --
 *
 *	Copy XImage data to drawable. Stub image for compatbility. 
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws image on drawable.
 *
 *----------------------------------------------------------------------
 */

int
XPutImage(
    Display *display,
    Drawable drawable,
    GC gc,
    XImage *image,
    int src_x, int src_y,
    int dest_x, int dest_y,
    unsigned int width,
    unsigned int height)
{
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDestroyImage --
 *
 *	Free XImage structure and data.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Frees allocated memory.
 *
 *----------------------------------------------------------------------
 */

int
XDestroyImage(
    XImage *image)
{
    if (image) {
        if (image->data) {
            ckfree(image->data);
        }
        ckfree((char*)image);
    }
    return 0;
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
