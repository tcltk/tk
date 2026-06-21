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

#ifdef XDestroyImage
#undef XDestroyImage
#endif

/*
 *----------------------------------------------------------------------
 *
 * _XInitImageFuncPtrs --
 *
 *	Stub implementation for Xlib image initialization. Required for
 *	compatibility with Tk's X11 emulation layer; does nothing on Wayland.
 *
 * Results:
 *	Always returns 0 (success).
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
 *	Accepts a raw image container from Tk, extracts the requested 
 *	sub-region, converts pixel formats from ARGB to native NanoVG RGBA, 
 *	and draws it safely onto the target drawable surface.
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
    TCL_UNUSED(Display *),
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
    if (!image || !image->data) {
        return 0;
    }

    /* Validate source coordinates against image bounds to prevent buffer overreads */
    if (src_x < 0 || src_y < 0 ||
        src_x + (int)width > image->width ||
        src_y + (int)height > image->height) {
        return TCL_ERROR;
    }

    TkWaylandDrawingContext dc;
    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return TCL_ERROR;
    }

    if (gc) {
        TkGlfwApplyGC(dc.vg, gc);
    }

    /* Allocate workspace memory for the extracted sub-region. */
    size_t numPixels = (size_t)width * (size_t)height;
    unsigned char *rgbaData = (unsigned char *)Tcl_Alloc(numPixels * 4);
    if (!rgbaData) {
        TkGlfwEndDraw(&dc);
        return TCL_ERROR;
    }

    /* Extract specified region and perform explicit ARGB -> RGBA mapping. */
    if (image->bits_per_pixel == 32) {
        for (unsigned int j = 0; j < height; j++) {
            unsigned char *src_ptr = (unsigned char*)image->data +
                                     ((src_y + j) * image->bytes_per_line) +
                                     (src_x * 4);
            unsigned char *dst_ptr = rgbaData + (j * width * 4);

            for (unsigned int i = 0; i < width; i++) {
                /* Extract channels accurately matching standard Tk core structures. */
                unsigned char a = src_ptr[i * 4 + 0];
                unsigned char r = src_ptr[i * 4 + 1];
                unsigned char g = src_ptr[i * 4 + 2];
                unsigned char b = src_ptr[i * 4 + 3];

                /* Map cleanly to NanoVG's hardware native ordering. */
                dst_ptr[i * 4 + 0] = r;
                dst_ptr[i * 4 + 1] = g;
                dst_ptr[i * 4 + 2] = b;
                dst_ptr[i * 4 + 3] = a;
            }
        }
    } else {
        /* Fallback direct copy if data format matches exactly. */
        for (unsigned int j = 0; j < height; j++) {
            memcpy(rgbaData + (j * width * 4),
                   (unsigned char*)image->data + ((src_y + j) * image->bytes_per_line) + (src_x * (image->bits_per_pixel / 8)),
                   width * 4);
        }
    }

    /* Generate a temporary texture inside the NanoVG context. */
    int imageID = nvgCreateImageRGBA(dc.vg, width, height, 0, rgbaData);
    Tcl_Free((char *)rgbaData);

    if (imageID <= 0) {
        TkGlfwEndDraw(&dc);
        return TCL_ERROR;
    }

    /* Construct image pattern brush relative to destination coordinates. */
    NVGpaint paint = nvgImagePattern(dc.vg, (float)dst_x, (float)dst_y, 
                                     (float)width, (float)height, 0.0f, imageID, 1.0f);
    
    /* Paint texture rect to canvas. */
    nvgBeginPath(dc.vg);
    nvgRect(dc.vg, (float)dst_x, (float)dst_y, (float)width, (float)height);
    nvgFillPaint(dc.vg, paint);
    nvgFill(dc.vg);

    /* Safely destroy texture to prevent GPU leaks. */
    nvgDeleteImage(dc.vg, imageID);

    TkGlfwEndDraw(&dc);
    return 0;
}
/*
 *----------------------------------------------------------------------
 *
 * XGetImage --
 *
 *	Stub implementation for XGetImage. This function is not used by
 *	Tk's core image rendering on Wayland; retained only for Xlib
 *	compatibility.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XImage*
XGetImage(
    Display *display, Drawable drawable, int x, int y,
    unsigned int width, unsigned int height,
    unsigned long plane_mask, int format)
{
    return NULL;
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
XCopyArea(Display *display, Drawable src, Drawable dst, GC gc,
          int src_x, int src_y, unsigned int width, unsigned int height,
          int dest_x, int dest_y)
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
    const char   *data,
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
    Display *display, Drawable src, Drawable dst, GC gc,
    int src_x, int src_y, unsigned int width, unsigned int height,
    int dest_x, int dest_y, unsigned long plane)
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
 *----------------------------------------------------------------------
 *
 * XDestroyImage --
 *
 *	Releases the memory associated with an XImage structure and its
 *	associated pixel data. Both the structure and the data are freed.
 *
 * Results:
 *	Always returns 0 (success).
 *
 * Side effects:
 *	Frees heap memory.
 *
 *----------------------------------------------------------------------
 */

int
XDestroyImage(
    XImage *image)
{
    if (image) {
        if (image->data) {
            Tcl_Free(image->data);
        }
        Tcl_Free((char *)image);
    }
    return 0;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
