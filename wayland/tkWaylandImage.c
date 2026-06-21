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
 *	Accepts a raw image container from Tk, performs a single-pass
 *	swizzle from ARGB to native NanoVG RGBA spacing, and draws it
 *	safely using the current Wayland window drawing frame context and FBO.
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

    /* Resolve drawing context. This automatically tracks the underlying 
     * Tk Wayland window FBO backing store and uses the open NanoVG frame.
     */
    TkWaylandDrawingContext dc;
    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return TCL_ERROR;
    }

    /* Allocate workspace memory for pixel matrix transformation */
    size_t numPixels = (size_t)width * (size_t)height;
    unsigned char *rgbaData = (unsigned char *)Tcl_Alloc(numPixels * 4);
    if (!rgbaData) {
        TkGlfwEndDraw(&dc);
        return TCL_ERROR;
    }

    /*
     * Single-pass swizzle matrix pass. 
     * Tk standard photo allocations store bits as ARGB [B,G,R,A] byte streams. 
     * Swizzle them to match NanoVG's hardware-native RGBA [R,G,B,A] byte format.
     */
    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            unsigned char *src = (unsigned char *)image->data
                               + (src_y + y) * image->bytes_per_line
                               + (src_x + x) * 4;
            unsigned char *dst = rgbaData + (y * width + x) * 4;

            dst[0] = src[2]; /* Red */
            dst[1] = src[1]; /* Green */
            dst[2] = src[0]; /* Blue */
            dst[3] = src[3]; /* Alpha */
        }
    }

    /* Generate a temporary texture inside the bound FBO NanoVG context layer */
    int imageID = nvgCreateImageRGBA(dc.vg, width, height, 0, rgbaData);
    Tcl_Free((char *)rgbaData);

    if (imageID == 0) {
        TkGlfwEndDraw(&dc);
        return TCL_ERROR;
    }

    /* Construct the image pattern brush to draw content from source space */
    NVGpaint paint = nvgImagePattern(dc.vg, (float)(dst_x - src_x), (float)(dst_y - src_y), 
                                     (float)width, (float)height, 0.0f, imageID, 1.0f);
    
    /* Paint the texture rect directly to the current FBO target canvas coordinates */
    nvgBeginPath(dc.vg);
    nvgRect(dc.vg, (float)dst_x, (float)dst_y, (float)width, (float)height);
    nvgFillPaint(dc.vg, paint);
    nvgFill(dc.vg);

    /* Delete the temporary texture object instantly to prevent GPU memory resource leaks */
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
 *	dispatches directly to TkpPutRGBAImage, which performs the
 *	actual swizzling and NanoVG rendering onto the Wayland surface.
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
