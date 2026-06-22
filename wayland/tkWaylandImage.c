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
#include "tkWaylandInt.h"
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#define NANOVG_GLES3
#include "nanovg_gl_utils.h"

#ifdef XDestroyImage
#undef XDestroyImage
#endif


/* Forward declarations for XImage function pointers */
static int		DestroyImage(XImage *imagePtr);
static unsigned long	ImageGetPixel(XImage *image, int x, int y);
static int		PutPixel(XImage *image, int x, int y, unsigned long pixel);

/*
 *----------------------------------------------------------------------
 *
 * DestroyImage --
 *
 *	Releases the memory associated with an XImage structure and its
 *	associated pixel data. Both the structure and the data are freed.
 *
 * Results:
 *	Always returns 0 (success).
 *
 * Side effects:
 *	Deallocates the image structure and data.
 *
 *----------------------------------------------------------------------
 */

static int
DestroyImage(
    XImage *imagePtr)
{
    if (imagePtr) {
        if (imagePtr->data) {
            Tcl_Free(imagePtr->data);
        }
        Tcl_Free((char *)imagePtr);
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XDestroyImage --
 *
 *	Exported wrapper for DestroyImage to maintain Xlib compatibility layer.
 *
 * Results:
 *	Always returns 0 (success).
 *
 * Side effects:
 *	Frees heap memory via DestroyImage.
 *
 *----------------------------------------------------------------------
 */

int
XDestroyImage(
    XImage *image)
{
    return DestroyImage(image);
}

/*
 *----------------------------------------------------------------------
 *
 * ImageGetPixel --
 *
 *	Extracts a single pixel from the XImage buffer. Maps from the internal
 *	32-bit layout into a standard color pixel layout.
 *
 * Results:
 *	Returns the 32-bit pixel value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static unsigned long
ImageGetPixel(
    XImage *image,
    int x, int y)
{
    unsigned long pixel = 0;
    
    if (!image || !image->data || x < 0 || y < 0 || x >= image->width || y >= image->height) {
        return 0;
    }

    unsigned char *srcPtr = (unsigned char *) &(image->data[(y * image->bytes_per_line)
	    + ((x * image->bits_per_pixel) / 8)]);

    switch (image->bits_per_pixel) {
    case 32:
    case 24:
        /* Map standard byte streams - R, G, B order. */
        pixel = ((unsigned long)srcPtr[0] << 16) |  /* R */
                ((unsigned long)srcPtr[1] << 8)  |  /* G */
                (unsigned long)srcPtr[2];           /* B */
        break;
    case 16:
        pixel = ((((unsigned short*)srcPtr)[0] & 0xF800) >> 8) |
                ((((unsigned short*)srcPtr)[0] & 0x07E0) << 5) |
                ((((unsigned short*)srcPtr)[0] & 0x001F) << 19);
        break;
    case 8:
        pixel = srcPtr[0];
        break;
    case 1:
        pixel = ((*srcPtr) & (0x80 >> (x % 8))) ? 1 : 0;
        break;
    }
    return pixel;
}

/*
 *----------------------------------------------------------------------
 *
 * PutPixel --
 *
 *	Writes a single pixel color value directly into the XImage memory buffer.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Modifies the raw data buffer of the target XImage.
 *
 *----------------------------------------------------------------------
 */

static int
PutPixel(
    XImage *image,
    int x, int y,
    unsigned long pixel)
{
    if (!image || !image->data || x < 0 || y < 0 || x >= image->width || y >= image->height) {
        return 0;
    }

    unsigned char *destPtr = (unsigned char *) &(image->data[(y * image->bytes_per_line)
	    + ((x * image->bits_per_pixel) / 8)]);

    switch (image->bits_per_pixel) {
    case 32:
    case 24:
        destPtr[0] = (unsigned char)((pixel >> 16) & 0xFF); /* R */
        destPtr[1] = (unsigned char)((pixel >> 8)  & 0xFF); /* G */
        destPtr[2] = (unsigned char)(pixel & 0xFF);         /* B */
        if (image->bits_per_pixel == 32) {
            destPtr[3] = 0xFF;  /* Opaque if no alpha in pixel value. */
        }
        break;
    case 16:
        (*(unsigned short*)destPtr) = (unsigned short)(
            ((pixel & 0xFF) >> 3) |
            (((pixel >> 8) & 0xFF) << 2) |
            (((pixel >> 16) & 0xFF) << 7));
        break;
    case 8:
        *destPtr = (unsigned char) pixel;
        break;
    case 1: {
        unsigned char mask = (0x80 >> (x % 8));
        if (pixel) {
            *destPtr |= mask;
        } else {
            *destPtr &= ~mask;
        }
        break;
    }
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateImage --
 *
 *	Allocates storage for a new XImage mirroring the Windows API 
 *	implementation context.
 *
 * Results:
 *	Returns a newly allocated XImage.
 *
 * Side effects:
 *	Allocates memory for the XImage structure.
 *
 *----------------------------------------------------------------------
 */

XImage *
XCreateImage(
	     TCL_UNUSED(Display *), /* display */
	     TCL_UNUSED(Visual *), /* visual */
	     unsigned int depth,
	     int format,
	     int offset,
	     char *data,
	     unsigned int width,
	     unsigned int height,
	     int bitmap_pad,
	     int bytes_per_line)
{
    XImage* imagePtr = (XImage*)Tcl_Alloc(sizeof(XImage));

    imagePtr->width = width;
    imagePtr->height = height;
    imagePtr->xoffset = offset;
    imagePtr->format = format;
    imagePtr->data = data;
    imagePtr->byte_order = LSBFirst;
    imagePtr->bitmap_unit = 8;
    imagePtr->bitmap_bit_order = LSBFirst;
    imagePtr->bitmap_pad = bitmap_pad;
    imagePtr->bits_per_pixel = depth;
    imagePtr->depth = depth;

    /* Align bitmap_pad bounds to a 32-bit boundary context. */
    bitmap_pad = (bitmap_pad + 31) / 32 * 32;

    if (bytes_per_line) {
        imagePtr->bytes_per_line = bytes_per_line;
    } else {
        imagePtr->bytes_per_line = (((depth * width) + (bitmap_pad - 1)) >> 3) & ~((bitmap_pad >> 3) - 1);
    }

    imagePtr->red_mask = 0xFF0000;
    imagePtr->green_mask = 0x00FF00;
    imagePtr->blue_mask = 0x0000FF;

    /* Bind internal function interfaces. */
    imagePtr->f.put_pixel = PutPixel;
    imagePtr->f.get_pixel = ImageGetPixel;
    imagePtr->f.destroy_image = DestroyImage;
    imagePtr->f.create_image = NULL;
    imagePtr->f.sub_image = NULL;
    imagePtr->f.add_pixel = NULL;

    return imagePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * _XInitImageFuncPtrs --
 *
 *	Initializes the function pointers inside an XImage structure
 *	so the generic Tk framework knows how to manipulate it.
 *
 * Results:
 *	Returns 0 (standard Xlib convention for successful init).
 *
 * Side effects:
 *	Binds the image function hooks to our custom backend logic.
 *
 *----------------------------------------------------------------------
 */

int
_XInitImageFuncPtrs(
    XImage *image)
{
    if (image == NULL) {
	return -1;
    }

    image->f.destroy_image = DestroyImage;
    image->f.get_pixel     = ImageGetPixel;
    image->f.put_pixel     = PutPixel;
    

    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpPutRGBAImage --
 *
 *	Accepts a raw image container from Tk, extracts the requested 
 *	sub-region, converts pixel formats from Tk's XImage layout to 
 *	native NanoVG RGBA, and draws it using NanoVG.
 *
 * Results:
 *	Returns 0 on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Draws the target image block onto the drawable surface.
 *
 *----------------------------------------------------------------------
 */

int 
TkpPutRGBAImage(
		TCL_UNUSED(Display *), /* display */
		Drawable drawable,
		GC gc,
		XImage* image,
		int src_x,
		int src_y,
		int dst_x,
		int dst_y,
		unsigned int width,
		unsigned int height)
{
    int imageId;
    NVGpaint imgPaint;

    if (!image || !image->data) {
        return 0;
    }

    /* Validate source coordinates against image bounds to prevent buffer overreads. */
    if (src_x < 0 || src_y < 0 ||
        src_x + (int)width > image->width ||
        src_y + (int)height > image->height) {
        return TCL_ERROR;
    }

    /* Secure and bind the target OpenGL / NanoVG drawing surface context. */
    TkWaylandDrawingContext dc;
    if (TkWaylandBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return TCL_ERROR;
    }

    if (gc) {
        TkWaylandApplyGC(dc.vg, gc);
    }

    /* Allocate workspace memory for the extracted sub-region. */
    size_t numPixels = (size_t)width * (size_t)height;
    unsigned char *rgbaData = (unsigned char *)ckalloc(numPixels * 4);
    if (!rgbaData) {
        TkWaylandEndDraw(&dc);
        return TCL_ERROR;
    }

    /* Extract sub-region and map Tk XImage (RGBA) to NanoVG RGBA. */
    if (image->bits_per_pixel == 32) {
        for (unsigned int j = 0; j < height; j++) {
            /* Map source row accounting for vertical offset and explicit line pitch. */
            unsigned char *src_ptr = (unsigned char*)image->data +
                                     ((src_y + j) * image->bytes_per_line) +
                                     (src_x * 4);
            unsigned char *dst_ptr = rgbaData + (j * width * 4);

            for (unsigned int i = 0; i < width; i++) {
                /* Direct copy - no R/B swap (Tk XImage is RGBA). */
                dst_ptr[i * 4 + 0] = src_ptr[i * 4 + 0]; /* R */
                dst_ptr[i * 4 + 1] = src_ptr[i * 4 + 1]; /* G */
                dst_ptr[i * 4 + 2] = src_ptr[i * 4 + 2]; /* B */
                dst_ptr[i * 4 + 3] = src_ptr[i * 4 + 3]; /* A */
            }
        }
    } else {
        /* Fallback linear block memory copy if bit depth is unmanaged. */
        for (unsigned int j = 0; j < height; j++) {
            memcpy(rgbaData + (j * width * 4),
                   (unsigned char*)image->data + 
                   ((src_y + j) * image->bytes_per_line) + 
                   (src_x * (image->bits_per_pixel / 8)),
                   width * 4);
        }
    }

    /* Create the texture atlas inside the active GLES NanoVG context. */
    imageId = nvgCreateImageRGBA(dc.vg, width, height, 0, rgbaData);
    ckfree(rgbaData);

    if (imageId <= 0) {
        TkWaylandEndDraw(&dc);
        return TCL_ERROR;
    }

    /* Construct the texture pattern brush positioned relative to destination offsets. */
    imgPaint = nvgImagePattern(dc.vg, (float)dst_x, (float)dst_y, 
                                (float)width, (float)height, 0.0f, imageId, 1.0f);
    
    /* Draw the texture path onto the active canvas window. */
    nvgBeginPath(dc.vg);
    nvgRect(dc.vg, (float)dst_x, (float)dst_y, (float)width, (float)height);
    nvgFillPaint(dc.vg, imgPaint);
    nvgFill(dc.vg);

    /* Note: Consider keeping nvgDeleteImage if you see VRAM growth. */
    // nvgDeleteImage(dc.vg, imageId);

    /* Finalize context pass, swap buffers, and flush layout changes. */
    TkWaylandEndDraw(&dc);
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetImage --
 *
 *	Copies layout surface pixels back from the GPU to CPU memory storage
 *	via glReadPixels. Emulates standard Xlib fallback behaviors.
 *
 * Results:
 *	Returns a newly allocated XImage container, or NULL on absolute failure.
 *
 * Side effects:
 *	Allocates memory for a new XImage structure and its pixel buffer data.
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
    TCL_UNUSED(unsigned long), /*  plane_mask */
    TCL_UNUSED(int)) /* format */
{
    TkWaylandDrawingContext dc;
    XImage *imagePtr;
    size_t size;

    /* Initialize target image mapping context. */
    imagePtr = XCreateImage(display, NULL, 32, ZPixmap, 0, NULL, width, height, 32, 0);
    if (!imagePtr) {
        return NULL;
    }

    size = imagePtr->bytes_per_line * imagePtr->height;
    imagePtr->data = (char *)Tcl_Alloc(size);
    if (!imagePtr->data) {
        Tcl_Free((char *)imagePtr);
        return NULL;
    }
    memset(imagePtr->data, 0, size);

    /* Bind context to securely read current screen surface framebuffers. */
    if (TkWaylandBeginDraw(drawable, NULL, &dc) == TCL_OK) {
        /*
         * Note: OpenGL coordinates are bottom-left relative.
         * glReadPixels reads native RGBA, but we must store it back mapped
         * safely to our native local layout formats.
         */
        unsigned char *glBuffer = (unsigned char *)ckalloc(width * height * 4);
        if (glBuffer) {
            glReadPixels(x, y, (GLsizei)width, (GLsizei)height, GL_RGBA, GL_UNSIGNED_BYTE, glBuffer);

            for (unsigned int yy = 0; yy < height; yy++) {
                /* Invert y row sequence due to OpenGL's coordinate orientation upside down format. */
                unsigned char *srcRow = glBuffer + ((height - 1 - yy) * width * 4);
                unsigned char *dstRow = (unsigned char *)imagePtr->data + (yy * imagePtr->bytes_per_line);

                for (unsigned int xx = 0; xx < width; xx++) {
                    /* Store as RGBA to match TkpPutRGBAImage. */
                    dstRow[xx * 4 + 0] = srcRow[xx * 4 + 0]; /* R */
                    dstRow[xx * 4 + 1] = srcRow[xx * 4 + 1]; /* G */
                    dstRow[xx * 4 + 2] = srcRow[xx * 4 + 2]; /* B */
                    dstRow[xx * 4 + 3] = srcRow[xx * 4 + 3]; /* A */
                }
            }
            ckfree(glBuffer);
        }
        TkWaylandEndDraw(&dc);
    }

    return imagePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyArea --
 *
 *	Intercepts Tk's internal double‑buffering presentation sentinels
 *	and handles them without introducing raw OpenGL state mutations.
 *	All other calls are no‑ops; actual drawing is performed through
 *	the NanoVG‑based rendering pipeline.
 *
 * Results:
 *	Always returns Success.
 *
 * Side effects:
 *	None (the function is a synchronization pass‑through).
 *
 *----------------------------------------------------------------------
 */

int
XCopyArea(
	  TCL_UNUSED(Display *), /* display */
	  TCL_UNUSED(Drawable), /* src */
	  TCL_UNUSED(Drawable), /* dst */
	  TCL_UNUSED(GC), /* gc */
	  TCL_UNUSED(int), /* src_x */
	  TCL_UNUSED(int), /* src_y */
	  unsigned int width,
	  unsigned int height,
	  TCL_UNUSED(int), /* dest_x */
	  TCL_UNUSED(int)) /* dest_y */
{
    /*
     * Safely intercept and isolate Tk's internal presentation sentinels.
     * Returning Success here maintains stable startup window geometry.
     */
    if ((int)width == -1 && (int)height == -1) {
        return Success;
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateBitmapFromData --
 *
 *	Constructs a 1‑bit deep Pixmap from raw inline bitmap data.
 *	This is a compatibility wrapper that allocates a new pixmap
 *	of the requested size and depth 1.
 *
 * Results:
 *	Returns a new Pixmap handle on success, or None on failure.
 *
 * Side effects:
 *	Allocates a new pixmap resource.
 *
 *----------------------------------------------------------------------
 */

Pixmap
XCreateBitmapFromData(
    Display      *display,
    Drawable      d,
    TCL_UNUSED(const char *), /* data */
    unsigned int  width,
    unsigned int  height)
{
    return Tk_GetPixmap(display, d, (int)width, (int)height, 1);
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyPlane --
 *
 *	Stub implementation for XCopyPlane. Not used in the Wayland
 *	backend; provided solely for Xlib compatibility.
 *
 * Results:
 *	Always returns Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XCopyPlane(
	   TCL_UNUSED(Display *), /* display */
	   TCL_UNUSED(Drawable), /* src */
	   TCL_UNUSED(Drawable),  /* dst */
	   TCL_UNUSED(GC), /* gc */
	   TCL_UNUSED(int), /* src_x */
	   TCL_UNUSED(int), /* src_y */
	   TCL_UNUSED(unsigned int), /* width */
	   TCL_UNUSED(unsigned int), /* height */
	   TCL_UNUSED(int), /* dest_x */
	   TCL_UNUSED(int), /* dest_y */
	   TCL_UNUSED(unsigned long)) /* plane */
{
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XPutImage --
 *
 *	Standard Xlib entry point for image drawing. This function
 *	dispatches directly to TkpPutRGBAImage.
 *
 * Results:
 *	Returns Success on success, or BadAlloc on allocation failure.
 *
 * Side effects:
 *	Draws the image onto the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XPutImage(
    Display      *display,
    Drawable      drawable,
    GC            gc,
    XImage       *image,
    int           src_x,
    int           src_y,
    int           dest_x,
    int           dest_y,
    unsigned int  width,
    unsigned int  height)
{
    int rc = TkpPutRGBAImage(display, drawable, gc, image,
                             src_x, src_y, dest_x, dest_y, width, height);
    return (rc == 0) ? Success : BadAlloc;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
