/*
 * tkWaylandImage.c --
 *
 *	Image handling for the Wayland/GLFW/libcg backend.
 *	Provides conversion between Tk images and libcg surfaces,
 *	and implements Xlib-compatible image functions.
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

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "tkInt.h"
#include "tkPort.h"
#include "tkImgPhoto.h"
#include "tkColor.h"
#include "tkWaylandInt.h"

#ifdef XDestroyImage
#undef XDestroyImage
#endif

extern TkGlfwContext glfwContext;
extern int IsPixmap(Drawable drawable);

/*
 *----------------------------------------------------------------------
 *
 * Pixel layout helpers
 *
 * cg_surface_t stores pixels as premultiplied ARGB in host byte order
 * (0xAARRGGBB on little-endian ARM64), which is BGRA in memory.
 * XImage pixels are 0x00RRGGBB (matching the pixel encoding in
 * TkpGetColor), so BGRX in memory on little-endian.
 *
 *----------------------------------------------------------------------
 */

/* Convert a cg surface row (premul ARGB) to XImage row (0x00RRGGBB). */
static void
CGRowToXImageRow(
    const uint32_t *src,
    uint32_t       *dst,
    int             width)
{
    for (int i = 0; i < width; i++) {
        uint32_t px  = src[i];
        uint8_t  a   = (px >> 24) & 0xFF;
        uint8_t  r   = (px >> 16) & 0xFF;
        uint8_t  g   = (px >>  8) & 0xFF;
        uint8_t  b   =  px        & 0xFF;

        /* Un-premultiply. */
        if (a > 0 && a < 255) {
            r = (uint8_t)((r * 255) / a);
            g = (uint8_t)((g * 255) / a);
            b = (uint8_t)((b * 255) / a);
        }
        dst[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
}

/* Convert an XImage row (0x00RRGGBB) to RGBA bytes for
 * cg_surface_create_for_data. */
static void
XImageRowToRGBA(
    const uint32_t *src,
    uint8_t        *dst,   /* 4 bytes per pixel, RGBA */
    int             width)
{
    for (int i = 0; i < width; i++) {
        uint32_t px = src[i];
        dst[i*4+0] = (px >> 16) & 0xFF;   /* R */
        dst[i*4+1] = (px >>  8) & 0xFF;   /* G */
        dst[i*4+2] =  px        & 0xFF;   /* B */
        dst[i*4+3] = 0xFF;                /* A = opaque */
    }
}


/*
 *----------------------------------------------------------------------
 *
 * _XInitImageFuncPtrs --
 *
 *	Xlib compatibility stub.
 *
 *----------------------------------------------------------------------
 */

int
_XInitImageFuncPtrs(TCL_UNUSED(XImage *))
{
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetImage --
 *
 *	Retrieve image data from a drawable as an XImage.
 *	Reads pixels from the drawable's cg_surface_t.
 *
 * Results:
 *	Pointer to a newly allocated XImage, or NULL on failure.
 *
 * Side effects:
 *	Allocates memory for the XImage and its pixel data.
 *
 *----------------------------------------------------------------------
 */

XImage *
XGetImage(
    Display      *display,
    Drawable      drawable,
    int           x,
    int           y,
    unsigned int  width,
    unsigned int  height,
    unsigned long plane_mask,
    int           format)
{
    struct cg_surface_t  *surface;
    XImage               *img;
    uint32_t             *imgData;
    int                   surfW = 0, surfH = 0;

    (void)plane_mask;
    (void)format;

    if (!display || !drawable) return NULL;
    LastKnownRequestProcessed(display)++;

    surface = ResolveSurface(drawable, &surfW, &surfH);
    if (!surface) return NULL;

    /* Clamp region to surface bounds. */
    if (x < 0) { width  += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + (int)width  > surfW) width  = surfW - x;
    if (y + (int)height > surfH) height = surfH - y;
    if ((int)width <= 0 || (int)height <= 0) return NULL;

    imgData = (uint32_t *)ckalloc(width * height * 4);
    if (!imgData) return NULL;

    /* Copy rows, converting premul-ARGB → 0x00RRGGBB. */
    {
        const uint32_t *pixels = (const uint32_t *)surface->pixels;
        int             stride = surface->stride / 4;   /* pixels per row */
        unsigned int    row;

        for (row = 0; row < height; row++) {
            const uint32_t *src = pixels + (y + (int)row) * stride + x;
            uint32_t       *dst = imgData + row * width;
            CGRowToXImageRow(src, dst, (int)width);
        }
    }

    img = (XImage *)ckalloc(sizeof(XImage));
    if (!img) { ckfree(imgData); return NULL; }
    memset(img, 0, sizeof(XImage));

    img->width            = (int)width;
    img->height           = (int)height;
    img->format           = ZPixmap;
    img->data             = (char *)imgData;
    img->byte_order       = LSBFirst;
    img->bitmap_unit      = 32;
    img->bitmap_bit_order = LSBFirst;
    img->bitmap_pad       = 32;
    img->depth            = 24;
    img->bytes_per_line   = (int)(width * 4);
    img->bits_per_pixel   = 32;
    img->red_mask         = 0x00FF0000u;
    img->green_mask       = 0x0000FF00u;
    img->blue_mask        = 0x000000FFu;

    return img;
}

/*
 *----------------------------------------------------------------------
 *
 * XDestroyImage --
 *
 *	Free an XImage and its pixel data.
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
XDestroyImage(XImage *image)
{
    if (image) {
        if (image->data) ckfree(image->data);
        ckfree((char *)image);
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XPutImage --
 *
 *	Draw XImage pixel data onto a drawable via a temporary cg surface.
 *
 * Results:
 *	Success or error code.
 *
 * Side effects:
 *	Draws into the drawable's cg surface.
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
    TkWaylandDrawingContext dc;
    struct cg_surface_t    *imgSurface;
    struct cg_ctx_t        *imgCtx;
    uint8_t                *rgba;
    int                     row;

    if (!display || !drawable || !image || !image->data) return BadValue;
    if (src_x < 0 || src_y < 0 ||
        src_x + (int)width  > image->width ||
        src_y + (int)height > image->height) return BadValue;

    LastKnownRequestProcessed(display)++;

    /* Build a temporary RGBA buffer from the XImage region. */
    rgba = (uint8_t *)ckalloc(width * height * 4);
    if (!rgba) return BadAlloc;

    for (row = 0; row < (int)height; row++) {
        const uint32_t *src = (const uint32_t *)
            (image->data + (src_y + row) * image->bytes_per_line)
            + src_x;
        XImageRowToRGBA(src, rgba + row * width * 4, (int)width);
    }

    /* Wrap it in a cg surface (no copy — surface borrows the buffer). */
    imgSurface = cg_surface_create_for_data((int)width, (int)height, rgba);
    if (!imgSurface) { ckfree(rgba); return BadAlloc; }

    imgCtx = cg_create(imgSurface);
    if (!imgCtx) {
        cg_surface_destroy(imgSurface);
        ckfree(rgba);
        return BadAlloc;
    }

    /* Draw into destination. */
    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        cg_destroy(imgCtx);
        cg_surface_destroy(imgSurface);
        ckfree(rgba);
        return BadDrawable;
    }

    cg_set_source_surface(dc.cg, imgSurface, (double)dest_x, (double)dest_y);
    cg_rectangle(dc.cg, (double)dest_x, (double)dest_y,
                 (double)width, (double)height);
    cg_fill(dc.cg);

    TkGlfwEndDraw(&dc);

    cg_destroy(imgCtx);
    cg_surface_destroy(imgSurface);
    ckfree(rgba);

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyArea --
 *
 *	Copy a rectangular region between drawables using cg surfaces.
 *
 * Results:
 *	Success or error code.
 *
 * Side effects:
 *	Copies pixel data between cg surfaces.
 *
 *----------------------------------------------------------------------
 */

int
XCopyArea(
    Display  *display,
    Drawable  src,
    Drawable  dst,
    GC        gc,
    int       src_x,  int src_y,
    unsigned  width,  unsigned height,
    int       dst_x,  int dst_y)
{
    TkWaylandDrawingContext  dc;
    struct cg_surface_t     *srcSurface;
    int                      srcW = 0, srcH = 0;

    (void)display;

    if (!src || !dst) return BadDrawable;

    srcSurface = ResolveSurface(src, &srcW, &srcH);
    if (!srcSurface) return BadDrawable;

    /* Clamp source region. */
    if (src_x < 0) { dst_x -= src_x; width  += src_x; src_x = 0; }
    if (src_y < 0) { dst_y -= src_y; height += src_y; src_y = 0; }
    if (src_x + (int)width  > srcW) width  = srcW - src_x;
    if (src_y + (int)height > srcH) height = srcH - src_y;
    if ((int)width <= 0 || (int)height <= 0) return Success;

    /* Begin drawing on destination. */
    if (TkGlfwBeginDraw(dst, gc, &dc) != TCL_OK) return BadDrawable;

    /*
     * Use cg_set_source_surface with an offset so only the requested
     * sub-rectangle is painted at (dst_x, dst_y).
     */
    cg_set_source_surface(dc.cg, srcSurface,
                          (double)(dst_x - src_x),
                          (double)(dst_y - src_y));
    cg_rectangle(dc.cg,
                 (double)dst_x, (double)dst_y,
                 (double)width, (double)height);
    cg_fill(dc.cg);

    TkGlfwEndDraw(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyPlane --
 *
 *	Expand a 1-bit mask from src, mapping set pixels to GC foreground
 *	and clear pixels to GC background, then blit onto dst.
 *
 * Results:
 *	Success or error code.
 *
 * Side effects:
 *	Draws the expanded bitmap onto the destination drawable.
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
    TCL_UNUSED(unsigned long))   /* plane */
{
    TkWaylandDrawingContext  dc;
    struct cg_surface_t     *srcSurface;
    struct cg_surface_t     *expSurface;
    XGCValues                gcv;
    struct cg_color_t        fg, bg;
    uint32_t                *expData;
    uint32_t                 fgPx, bgPx;
    int                      srcW = 0, srcH = 0;
    int                      row, col;

    if (!display || !src || !dst) return BadDrawable;
    LastKnownRequestProcessed(display)++;

    srcSurface = ResolveSurface(src, &srcW, &srcH);
    if (!srcSurface) return BadDrawable;

    /* Clamp. */
    if (src_x < 0) { dest_x -= src_x; width  += src_x; src_x = 0; }
    if (src_y < 0) { dest_y -= src_y; height += src_y; src_y = 0; }
    if (src_x + (int)width  > srcW) width  = srcW - src_x;
    if (src_y + (int)height > srcH) height = srcH - src_y;
    if ((int)width <= 0 || (int)height <= 0) return Success;

    /* Resolve fg/bg colors. */
    if (TkWaylandGetGCValues(gc, GCForeground | GCBackground, &gcv) == 0) {
        fg = TkGlfwPixelToCG(gcv.foreground);
        bg = TkGlfwPixelToCG(gcv.background);
    } else {
        fg.r = 0.0; fg.g = 0.0; fg.b = 0.0; fg.a = 1.0;
        bg.r = 1.0; bg.g = 1.0; bg.b = 1.0; bg.a = 1.0;
    }

    /* Pack as premultiplied ARGB for the cg pixel buffer. */
    fgPx = (0xFFu << 24) |
           ((uint8_t)(fg.r * 255) << 16) |
           ((uint8_t)(fg.g * 255) <<  8) |
            (uint8_t)(fg.b * 255);
    bgPx = (0xFFu << 24) |
           ((uint8_t)(bg.r * 255) << 16) |
           ((uint8_t)(bg.g * 255) <<  8) |
            (uint8_t)(bg.b * 255);

    /* Build expanded ARGB buffer: non-zero source pixel → fg, else → bg. */
    expData = (uint32_t *)ckalloc(width * height * 4);
    if (!expData) return BadAlloc;

    {
        const uint32_t *srcPixels = (const uint32_t *)srcSurface->pixels;
        int             srcStride = srcSurface->stride / 4;

        for (row = 0; row < (int)height; row++) {
            const uint32_t *srcRow = srcPixels
                                   + (src_y + row) * srcStride + src_x;
            uint32_t       *dstRow = expData + row * width;
            for (col = 0; col < (int)width; col++) {
                dstRow[col] = (srcRow[col] & 0x00FFFFFF) ? fgPx : bgPx;
            }
        }
    }

    expSurface = cg_surface_create_for_data((int)width, (int)height, expData);
    if (!expSurface) { ckfree(expData); return BadAlloc; }

    if (TkGlfwBeginDraw(dst, gc, &dc) != TCL_OK) {
        cg_surface_destroy(expSurface);
        ckfree(expData);
        return BadDrawable;
    }

    cg_set_source_surface(dc.cg, expSurface,
                          (double)dest_x, (double)dest_y);
    cg_rectangle(dc.cg, (double)dest_x, (double)dest_y,
                 (double)width, (double)height);
    cg_fill(dc.cg);

    TkGlfwEndDraw(&dc);

    cg_surface_destroy(expSurface);
    ckfree(expData);
    return Success;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
