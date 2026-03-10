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

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#define NANOVG_GLES2  

/*
 * Undefine X11 macro that conflicts with our implementation.
 * X11 headers define XDestroyImage as a macro that expands to:
 * (*((image)->f.destroy_image))(image)
 */
#ifdef XDestroyImage
#undef XDestroyImage
#endif

extern TkGlfwContext  glfwContext;

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
 * Static Function Prototypes
 *
 *----------------------------------------------------------------------
 */

static NVGImageData* CreateNVGImageFromDrawableRect(
    Drawable drawable, int x, int y,
    unsigned int width, unsigned int height);
static XImage* TkWaylandCreateXImageWithNVGImage(
    NVGcontext* vg, NVGImageData* nvgImage, Display* display);
static int XCopyArea_PixmapToPixmap(
    TkWaylandPixmapImpl *srcPixmap, TkWaylandPixmapImpl *dstPixmap, GC gc,
    int src_x, int src_y,unsigned width, unsigned height, int dst_x, int dst_y);
static int XCopyArea_PixmapToWindow(
    TkWaylandPixmapImpl *srcPixmap,Drawable dst,
    GC gc, int src_x, int src_y, unsigned width, unsigned height,
    int dst_x, int dst_y);

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
 * CreateNVGImageFromDrawableRect --
 *
 *	Create NanoVG image from a rectangular region of a drawable.
 *
 * Results:
 *	Pointer to NVGImageData or NULL on failure.
 *
 * Side effects:
 *	Allocates memory for image data.
 *	Makes the GLFW window's GL context current.
 *
 *----------------------------------------------------------------------
 */

static NVGImageData*
CreateNVGImageFromDrawableRect(
    Drawable drawable,
    int x, int y,
    unsigned int width,
    unsigned int height)
{
    GLFWwindow *glfwWindow;
    NVGcontext *vg;
    NVGImageData *nvgImg;
    unsigned char *pixels;
    unsigned char *rgba_pixels;
    int imageId;

    /* Get GLFW window from drawable. */
    glfwWindow = TkGlfwGetWindowFromDrawable(drawable);
    if (!glfwWindow) {
        return NULL;
    }

    /* Get NanoVG context. */
    vg = TkGlfwGetNVGContext();
    if (!vg) {
        return NULL;
    }

    /* Make context current for GL operations. */
    glfwMakeContextCurrent(glfwWindow);

    /* Allocate pixel buffers. */
    pixels = (unsigned char*)ckalloc(width * height * 4);
    rgba_pixels = (unsigned char*)ckalloc(width * height * 4);

    if (!pixels || !rgba_pixels) {
        if (pixels) ckfree(pixels);
        if (rgba_pixels) ckfree(rgba_pixels);
        return NULL;
    }

    /* Read pixels from current framebuffer (OpenGL uses RGBA). */
    glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    /* NanoVG expects RGBA as well, but we need to handle potential
     * differences in coordinate system (Y inversion). */

    /* For now, just copy the data - NanoVG uses same orientation as OpenGL. */
    memcpy(rgba_pixels, pixels, width * height * 4);

    /* Create NanoVG image. */
    imageId = nvgCreateImageRGBA(vg, width, height,
                                  0,
                                  rgba_pixels);

    ckfree(pixels);

	nvgImg = ckalloc(sizeof(NVGImageData));
	if (!nvgImg) {
	    ckfree(rgba_pixels);
	    nvgDeleteImage(vg, imageId);
	    return NULL;
	}

	nvgImg->id = imageId;
	nvgImg->width = width;
	nvgImg->height = height;
	nvgImg->flags = 0;
	nvgImg->pixels = rgba_pixels;   /* ⭐ store CPU copy */

	return nvgImg;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCreateXImageWithNVGImage --
 *
 *	Create XImage from NVG image data.
 *
 * Results:
 *	Pointer to XImage or NULL on failure.
 *
 * Side effects:
 *	Allocates memory for XImage structure and data.
 *
 *----------------------------------------------------------------------
 */


XImage* TkWaylandCreateXImageWithNVGImage(
    TCL_UNUSED(NVGcontext *),
    NVGImageData* nvgImage,
    TCL_UNUSED(Display *))
{
    if (!nvgImage || !nvgImage->pixels) {
        return NULL;
    }

    XImage *imagePtr = ckalloc(sizeof(XImage));
    if (!imagePtr) return NULL;

    unsigned char *data = ckalloc(nvgImage->width * nvgImage->height * 4);
    if (!data) {
        ckfree(imagePtr);
        return NULL;
    }

    memcpy(data, nvgImage->pixels,
           nvgImage->width * nvgImage->height * 4);

    memset(imagePtr, 0, sizeof(XImage));
    imagePtr->width = nvgImage->width;
    imagePtr->height = nvgImage->height;
    imagePtr->format = ZPixmap;
    imagePtr->data = (char*)data;
    imagePtr->byte_order = LSBFirst;
    imagePtr->bitmap_unit = 32;
    imagePtr->bitmap_bit_order = LSBFirst;
    imagePtr->bitmap_pad = 32;
    imagePtr->depth = 32;
    imagePtr->bytes_per_line = nvgImage->width * 4;
    imagePtr->bits_per_pixel = 32;
    imagePtr->red_mask   = 0x00FF0000u;
    imagePtr->green_mask = 0x0000FF00u;
    imagePtr->blue_mask  = 0x000000FFu;

    return imagePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetImage --
 *
 *	Retrieve image data from drawable (Xlib compatibility).
 *
 * Results:
 *	Pointer to XImage or NULL on failure.
 *
 * Side effects:
 *	Allocates memory for image data.
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
    NVGImageData *nvgImg;
    XImage *imagePtr;
    NVGcontext *vg;

    (void)plane_mask;  /* Not used in NanoVG backend */
    (void)format;      /* Always use ZPixmap */

    if (!display || !drawable) {
        return NULL;
    }

    LastKnownRequestProcessed(display)++;

    /* Create NVG image from drawable region. */
    nvgImg = CreateNVGImageFromDrawableRect(drawable, x, y, width, height);
    if (!nvgImg) {
        return NULL;
    }

    /* Get NanoVG context. */
    vg = TkGlfwGetNVGContext();
    if (!vg) {
        nvgDeleteImage(vg, nvgImg->id);
        if (nvgImg->pixels) ckfree(nvgImg->pixels);
        ckfree((char*)nvgImg);
        return NULL;
    }

    /* Convert to XImage. */
    imagePtr = TkWaylandCreateXImageWithNVGImage(vg, nvgImg, display);

    /* Clean up NVG image. */
    nvgDeleteImage(vg, nvgImg->id);
    if (nvgImg->pixels) ckfree(nvgImg->pixels);
    ckfree((char*)nvgImg);

    return imagePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyArea --
 *
 *	Copy rectangular area from one drawable to another.
 *
 * Results:
 *	Success or error code.
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
    WindowMapping *srcMapping = NULL;
    WindowMapping *dstMapping = NULL;
    TkWaylandPixmapImpl *srcPixmap = NULL;
    TkWaylandPixmapImpl *dstPixmap = NULL;
    
    (void)display;
    (void)gc;
    
    /* Determine source and destination types */
    if (IsPixmap(src)) {
        srcPixmap = (TkWaylandPixmapImpl *)src;
    } else {
        srcMapping = FindMappingByDrawable(src);
    }
    
    if (IsPixmap(dst)) {
        dstPixmap = (TkWaylandPixmapImpl *)dst;
    } else {
        dstMapping = FindMappingByDrawable(dst);
    }
    
    /*
     * Pixmap → Window
     */
    if (srcPixmap && dstMapping) {
        return XCopyArea_PixmapToWindow(srcPixmap, dst, gc,
                                       src_x, src_y, width, height,
                                       dst_x, dst_y);
    }
    
    /*
     * Window → Window (for scrolling)
     */
    if (srcMapping && dstMapping && srcMapping == dstMapping) {
		return Success;
    }
    
    /*
     * Pixmap → Pixmap
     */
    if (srcPixmap && dstPixmap) {
        return XCopyArea_PixmapToPixmap(srcPixmap, dstPixmap, gc,
                                       src_x, src_y, width, height,
                                       dst_x, dst_y);
    }
    
    return Success;
}


    
/* Helper functions for XCopyArea to improve performance. */

static int
XCopyArea_PixmapToWindow(
    TkWaylandPixmapImpl *srcPixmap,
    Drawable             dst,
    GC                   gc,
    int                  src_x, int src_y,
    unsigned             width, unsigned height,
    int                  dst_x, int dst_y)
{
    TkWaylandDrawingContext dc;
    int nvgImage;
    NVGpaint imgPaint;
    int rc;
    unsigned char *pixels = NULL; 
    
    pixels = (unsigned char *)ckalloc(srcPixmap->width * srcPixmap->height * 4);
    
    
    /* Close pixmap frame if open. */
    if (srcPixmap->frameOpen) {
        nvgRestore(glfwContext.vg);
        nvgEndFrame(glfwContext.vg);
        srcPixmap->frameOpen = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    
    /* Begin drawing to destination window. */
    rc = TkGlfwBeginDraw(dst, gc, &dc);
    if (rc != TCL_OK) {
        return BadDrawable;
    }
    
    /* Wrap pixmap texture as NVG image. */
    nvgImage = nvgCreateImageRGBA(dc.vg, srcPixmap->width, srcPixmap->height,
                                  0, pixels);

    
    if (nvgImage == 0) {
        TkGlfwEndDraw(&dc);
        return BadDrawable;
    }
    
    /* Create image pattern. */
    imgPaint = nvgImagePattern(dc.vg,
                               (float)dst_x - src_x,
                               (float)dst_y - src_y,
                               (float)srcPixmap->width,
                               (float)srcPixmap->height,
                               0.0f,
                               nvgImage,
                               1.0f);
    
    /* Draw. */
    nvgBeginPath(dc.vg);
    nvgRect(dc.vg,
            (float)dst_x,
            (float)dst_y,
            (float)width,
            (float)height);
    nvgFillPaint(dc.vg, imgPaint);
    nvgFill(dc.vg);
    
    /* Cleanup. */
    nvgDeleteImage(dc.vg, nvgImage);
    TkGlfwEndDraw(&dc);
    
    return Success;
}

    
static int
XCopyArea_PixmapToPixmap(
    TkWaylandPixmapImpl *srcPixmap,
    TkWaylandPixmapImpl *dstPixmap,
    GC                   gc,
    int                  src_x, int src_y,
    unsigned             width, unsigned height,
    int                  dst_x, int dst_y)
{
    TkWaylandDrawingContext dc;
    int nvgImage;
    NVGpaint imgPaint;
    
    unsigned char *pixels = NULL; 
    
    pixels = (unsigned char *)ckalloc(srcPixmap->width * srcPixmap->height * 4);
 
    
    /* Close source pixmap frame if open. */
    if (srcPixmap->frameOpen) {
        nvgRestore(glfwContext.vg);
        nvgEndFrame(glfwContext.vg);
        srcPixmap->frameOpen = 0;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    
    /* Set up destination pixmap for drawing. */
    dc.drawable = (Drawable)dstPixmap;
    dc.vg = glfwContext.vg;
    dc.width = dstPixmap->width;
    dc.height = dstPixmap->height;
    dc.offsetX = 0;
    dc.offsetY = 0;
    
    /* Bind destination FBO and open frame if needed. */
    if (!dstPixmap->frameOpen) {
        glBindFramebuffer(GL_FRAMEBUFFER, dstPixmap->fbo);
        glViewport(0, 0, dstPixmap->width, dstPixmap->height);
        
        nvgBeginFrame(glfwContext.vg,
                     (float)dstPixmap->width,
                     (float)dstPixmap->height,
                     1.0f);
        
        nvgSave(glfwContext.vg);
        nvgScale(glfwContext.vg, 1.0f, -1.0f);
        nvgTranslate(glfwContext.vg, 0.0f, -(float)dstPixmap->height);
        nvgTranslate(glfwContext.vg, 0.5f, 0.5f);
        
        dstPixmap->frameOpen = 1;
    }
    
   /* Create NanoVG image (basic function, always works) */
    nvgImage = nvgCreateImageRGBA(dc.vg, srcPixmap->width, srcPixmap->height,
                                  0, pixels);
    
    if (nvgImage == 0) {
        return BadDrawable;
    }
    
    /* Create image pattern. */
    imgPaint = nvgImagePattern(glfwContext.vg,
                               (float)dst_x - src_x,
                               (float)dst_y - src_y,
                               (float)srcPixmap->width,
                               (float)srcPixmap->height,
                               0.0f,
                               nvgImage,
                               1.0f);
    
    /* Draw. */
    nvgBeginPath(glfwContext.vg);
    nvgRect(glfwContext.vg,
            (float)dst_x,
            (float)dst_y,
            (float)width,
            (float)height);
    nvgFillPaint(glfwContext.vg, imgPaint);
    nvgFill(glfwContext.vg);
    
    /* Cleanup. */
    nvgDeleteImage(glfwContext.vg, nvgImage);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    return Success;
}


/*
 *----------------------------------------------------------------------
 *
 * XCopyPlane --
 *
 *	Copy a single bit-plane from src to dst, mapping 1-bits to the
 *	GC foreground color and 0-bits to the GC background color.
 *	Used for bitmap-backed images, stipple patterns, and cursor masks.
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
    TCL_UNUSED(unsigned long)) /* plane */
{
    TkWaylandDrawingContext  dc;
    NVGImageData            *srcImg;
    XGCValues                gcValues;
    unsigned char           *expanded;
    unsigned char           *src_row;
    unsigned char           *dst_pix;
    NVGpaint                 imgPaint;
    NVGcolor                 fg, bg;
    int                      imageId;
    unsigned int             x, y;

    if (!display || !src || !dst) {
        return BadDrawable;
    }

    LastKnownRequestProcessed(display)++;

    /* Read the source bitmap pixels. */
    srcImg = CreateNVGImageFromDrawableRect(src, src_x, src_y, width, height);
    if (!srcImg) {
        return BadDrawable;
    }

    /* Resolve foreground and background colors from the GC. */
    if (TkWaylandGetGCValues(gc, GCForeground | GCBackground, &gcValues) == 0) {
        fg = TkGlfwPixelToNVG(gcValues.foreground);
        bg = TkGlfwPixelToNVG(gcValues.background);
    } else {
        fg = nvgRGBA(0,   0,   0,   255);   /* black */
        bg = nvgRGBA(255, 255, 255, 255);   /* white */
    }

    /*
     * Expand the 1-bit plane into an RGBA image:
     * any non-zero pixel in the source maps to fg; zero maps to bg.
     */
    expanded = (unsigned char *)ckalloc(width * height * 4);
    if (!expanded) {
        if (srcImg->pixels) ckfree(srcImg->pixels);
        ckfree((char *)srcImg);
        return BadAlloc;
    }

    for (y = 0; y < height; y++) {
        src_row = srcImg->pixels + y * width * 4;
        dst_pix = expanded      + y * width * 4;

        for (x = 0; x < width; x++) {
            /*
             * Treat the source as a luminance mask: if any channel
             * of the source pixel is non-zero, map to foreground.
             */
            int lit = src_row[x*4+0] || src_row[x*4+1] ||
                      src_row[x*4+2] || src_row[x*4+3];
            NVGcolor c = lit ? fg : bg;

            dst_pix[x*4+0] = (unsigned char)(c.r * 255);
            dst_pix[x*4+1] = (unsigned char)(c.g * 255);
            dst_pix[x*4+2] = (unsigned char)(c.b * 255);
            dst_pix[x*4+3] = (unsigned char)(c.a * 255);
        }
    }

    /* Free the raw source pixels; we no longer need them. */
    if (srcImg->pixels) ckfree(srcImg->pixels);
    ckfree((char *)srcImg);

    /* Begin drawing on destination. */
    int rc = TkGlfwBeginDraw(dst, gc, &dc);
    if (rc != TCL_OK) {
        ckfree(expanded);
        return BadDrawable;
    }

    if (gc) {
        TkGlfwApplyGC(dc.vg, gc);
    }

    /* Upload expanded RGBA bitmap to NanoVG. */
    imageId = nvgCreateImageRGBA(dc.vg, width, height, 0, expanded);
    ckfree(expanded);

    if (imageId <= 0) {
        TkGlfwEndDraw(&dc);
        return BadAlloc;
    }

    /* Draw the expanded bitmap onto the destination. */
    imgPaint = nvgImagePattern(dc.vg, dest_x, dest_y, width, height,
                               0.0f, imageId, 1.0f);
    nvgBeginPath(dc.vg);
    nvgRect(dc.vg, dest_x, dest_y, width, height);
    nvgFillPaint(dc.vg, imgPaint);
    nvgFill(dc.vg);

    /* Clean up. */
    nvgDeleteImage(dc.vg, imageId);

    TkGlfwEndDraw(&dc);
    return Success;
}


/*
 *----------------------------------------------------------------------
 *
 * XPutImage --
 *
 *	Copy XImage data to drawable.
 *
 * Results:
 *	Success or error code.
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
    TkWaylandDrawingContext dc;
    int imageId;
    NVGpaint imgPaint;
    unsigned char *rgba_data;

    if (!display || !drawable || !image || !image->data) {
        return BadValue;
    }

    /* Validate source coordinates */
    if (src_x < 0 || src_y < 0 ||
        src_x + (int)width > image->width ||
        src_y + (int)height > image->height) {
        return BadValue;
    }

    LastKnownRequestProcessed(display)++;

    /* Begin drawing. */
    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }

    /* Apply GC settings. */
    if (gc) {
        TkGlfwApplyGC(dc.vg, gc);
    }

    /* Convert ARGB to RGBA if needed */
    rgba_data = (unsigned char*)ckalloc(width * height * 4);
    if (!rgba_data) {
        TkGlfwEndDraw(&dc);
        return BadAlloc;
    }

    /* Extract the specified region and convert */
    if (image->bits_per_pixel == 32) {
        int i, j;
        unsigned char *src_ptr, *dst_ptr;

        for (j = 0; j < (int)height; j++) {
            src_ptr = (unsigned char*)image->data +
                      ((src_y + j) * image->bytes_per_line) +
                      (src_x * (image->bits_per_pixel / 8));
            dst_ptr = rgba_data + (j * width * 4);

            /* Convert ARGB to RGBA if needed */
            for (i = 0; i < (int)width; i++) {
                /* Assuming XImage data is ARGB (most common with Tk) */
                unsigned char a = src_ptr[i*4];
                unsigned char r = src_ptr[i*4+1];
                unsigned char g = src_ptr[i*4+2];
                unsigned char b = src_ptr[i*4+3];

                dst_ptr[i*4] = r;
                dst_ptr[i*4+1] = g;
                dst_ptr[i*4+2] = b;
                dst_ptr[i*4+3] = a;
            }
        }
    } else {
        /* For other bit depths, we'd need proper conversion */
        memcpy(rgba_data, (unsigned char*)image->data +
               (src_y * image->bytes_per_line) +
               (src_x * (image->bits_per_pixel / 8)),
               width * height * 4);
    }

    /* Create NanoVG image from the region data. */
    imageId = nvgCreateImageRGBA(dc.vg, width, height,
                                  0, rgba_data);

    ckfree(rgba_data);

    if (imageId <= 0) {
        TkGlfwEndDraw(&dc);
        return BadAlloc;
    }

    /* Create image pattern. */
    imgPaint = nvgImagePattern(dc.vg, dest_x, dest_y,
                                width, height,
                                0.0f, imageId, 1.0f);

    /* Draw the image. */
    nvgBeginPath(dc.vg);
    nvgRect(dc.vg, dest_x, dest_y, width, height);
    nvgFillPaint(dc.vg, imgPaint);
    nvgFill(dc.vg);

    /* Clean up. */
    nvgDeleteImage(dc.vg, imageId);

    TkGlfwEndDraw(&dc);
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
