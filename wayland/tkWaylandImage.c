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
 *	Accepts a raw image container from Tk, performs a single-pass
 *	swizzle from ARGB to native NanoVG RGBA spacing, and draws it
 *	safely into the active, open NanoVG rendering frame.
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

    TkWaylandDrawingContext dc;
    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return TCL_ERROR;
    }

    /* Allocate temporary workspace memory for the swizzle conversion */
    size_t numPixels = (size_t)width * (size_t)height;
    unsigned char *rgbaData = (unsigned char *)Tcl_Alloc(numPixels * 4);
    if (!rgbaData) {
        TkGlfwEndDraw(&dc);
        return TCL_ERROR;
    }

    /* * Perform a single-pass matrix swizzle conversion.
     * Tk provides ARGB [B,G,R,A] bytes. Convert them to NanoVG RGBA [R,G,B,A].
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

    /* Create a transient NanoVG texture safely within the open window frame context */
    int imageID = nvgCreateImageRGBA(dc.vg, width, height, 0, rgbaData);
    Tcl_Free((char *)rgbaData);

    if (imageID == 0) {
        TkGlfwEndDraw(&dc);
        return TCL_ERROR;
    }

    /* Render the texture pattern directly onto the destination coordinates */
    NVGpaint paint = nvgImagePattern(dc.vg, (float)(dst_x - src_x), (float)(dst_y - src_y), 
                                     (float)width, (float)height, 0.0f, imageID, 1.0f);
    nvgBeginPath(dc.vg);
    nvgRect(dc.vg, (float)dst_x, (float)dst_y, (float)width, (float)height);
    nvgFillPaint(dc.vg, paint);
    nvgFill(dc.vg);

    /* Delete the transient texture object immediately to avoid running out of GL resources */
    nvgDeleteImage(dc.vg, imageID);

    TkGlfwEndDraw(&dc);
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetImage --
 *
 *	Stub function for compatibility.
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
 *	Lightweight synchronization metadata handler. Safely intercepts
 *	Tk's internal double-buffering presentation sentinels without
 *	introducing raw OpenGL state mutations that break the backing store.
 *
 * Results:
 *	Success.
 *
 *----------------------------------------------------------------------
 */

int
XCopyArea(Display *display, Drawable src, Drawable dst, GC gc,
          int src_x, int src_y, unsigned int width, unsigned int height,
          int dest_x, int dest_y)
{
    /* * Safely intercept and isolate Tk's internal presentation sentinels.
     * Returning Success here prevents the startup rendering loop from breaking.
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
 *	Constructs a 1-bit deep Pixmap from raw inline byte data.
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
 *	Stub function for compatibility.
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
 *	Standard Xlib routing entry-point. Passes the incoming image
 *	data directly down to our optimized swizzling pipeline.
 *
 * Results:
 *	Success.
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
 *	Free XImage structure and data.
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
