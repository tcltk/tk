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

#include <wayland-client.h>
#include <GLES3/gl3.h>
#include <nanovg.h>
#include <nanovg_gl.h>
#include "tkGlfwInt.h"

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
    WaylandDrawable* drawable, int x, int y, 
    unsigned int width, unsigned int height);
static int ConvertTkImageToNVG(
    Tk_Window tkwin, Tk_Image image, 
    NVGcontext* vg, NVGImageData** nvgImage);
static int ConvertNVGToTkImage(
    NVGcontext* vg, NVGImageData* nvgImage, 
    Tk_PhotoHandle photoHandle);
static NVGImageData* TkWaylandCreateNVGImageWithXImage(
    NVGcontext* vg, XImage *image);
static XImage* TkWaylandCreateXImageWithNVGImage(
    NVGcontext* vg, NVGImageData* nvgImage, Display* display);
static WaylandDrawable* GetWaylandDrawable(Drawable drawable);

/* Helper Functions. */

/*
 *----------------------------------------------------------------------
 *
 * GetWaylandDrawable --
 *
 *	Safely cast Drawable to WaylandDrawable with validation.
 *
 * Results:
 *	Pointer to WaylandDrawable or NULL if invalid.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static WaylandDrawable*
GetWaylandDrawable(
    Drawable drawable)
{
    WaylandDrawable* wlDraw = (WaylandDrawable*)drawable;
    
    /* Basic validation - check if vg context exists. */
    if (!wlDraw || !wlDraw->vg) {
        return NULL;
    }
    
    return wlDraw;
}

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

/* Image Conversion Functions. */

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCreateNVGImageWithXImage --
 *
 *	Create NVGImageData from XImage, converting pixel formats.
 *	Supports 1bpp, 24bpp, and 32bpp formats.
 *
 * Results:
 *	Pointer to NVGImageData, or NULL on failure.
 *	Caller must free with nvgDeleteImage() and Tcl_Free().
 *
 * Side effects:
 *	Allocates memory for temporary RGBA buffer.
 *
 *----------------------------------------------------------------------
 */

static NVGImageData*
TkWaylandCreateNVGImageWithXImage(
    NVGcontext* vg,
    XImage *image)
{
    if (!vg || !image || !image->data) {
        return NULL;
    }
    
    NVGImageData* nvgImg = NULL;
    unsigned char* rgbaData = NULL;
    int len = image->width * image->height * 4;
    int imageId = -1;
    
    /* Allocate RGBA buffer. */
    rgbaData = (unsigned char*)Tcl_Alloc(len);
    if (!rgbaData) {
        return NULL;
    }
    
    if (image->bits_per_pixel == 1) {
        /*
         * Convert 1bpp bitmap to RGBA.
         * Each bit represents a pixel (1 = white, 0 = black).
         */
        const unsigned char* src = (const unsigned char*)image->data + image->xoffset;
        unsigned char* dst = rgbaData;
        
        for (int y = 0; y < image->height; y++) {
            for (int x = 0; x < image->width; x++) {
                int byteIdx = (y * image->bytes_per_line) + (x / 8);
                int bitIdx = 7 - (x % 8);  /* MSB first */
                unsigned char bit = (src[byteIdx] >> bitIdx) & 1;
                unsigned char value = bit ? 255 : 0;
                
                *dst++ = value;  /* R */
                *dst++ = value;  /* G */
                *dst++ = value;  /* B */
                *dst++ = 255;    /* A (opaque) */
            }
        }
    } 
    else if (image->format == ZPixmap && image->bits_per_pixel == 32) {
        /*
         * Convert 32bpp ARGB to RGBA for NanoVG.
         * Handle both big-endian and little-endian byte orders.
         */
        const unsigned char* src = (const unsigned char*)image->data + image->xoffset;
        unsigned char* dst = rgbaData;
		/* Little endian: ARGB stored as [B,G,R,A] */
		for (int i = 0; i < image->width * image->height; i++) {
			dst[0] = src[2];  /* R */
			dst[1] = src[1];  /* G */
			dst[2] = src[0];  /* B */
			dst[3] = src[3];  /* A */
			src += 4;
			dst += 4;
		}
	}
    else if (image->format == ZPixmap && image->bits_per_pixel == 24) {
        /*
         * Convert 24bpp RGB to RGBA.
         * Assume RGB byte order and add full opacity.
         */
        const unsigned char* src = (const unsigned char*)image->data + image->xoffset;
        unsigned char* dst = rgbaData;
        
        for (int i = 0; i < image->width * image->height; i++) {
            dst[0] = src[0];  /* R */
            dst[1] = src[1];  /* G */
            dst[2] = src[2];  /* B */
            dst[3] = 255;     /* A (opaque) */
            src += 3;
            dst += 4;
        }
    }
    else {
        /* Unsupported format */
        Tcl_Free(rgbaData);
        return NULL;
    }
    
    /* Create NanoVG image */
    imageId = nvgCreateImageRGBA(vg, image->width, image->height, 
                                  NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY, 
                                  rgbaData);
    
    Tcl_Free(rgbaData);
    
    if (imageId > 0) {
        nvgImg = (NVGImageData*)Tcl_Alloc(sizeof(NVGImageData));
        if (nvgImg) {
            nvgImg->id = imageId;
            nvgImg->width = image->width;
            nvgImg->height = image->height;
            nvgImg->flags = NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY;
        } else {
            nvgDeleteImage(vg, imageId);
        }
    }
    
    return nvgImg;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCreateXImageWithNVGImage --
 *
 *	Create XImage from NVGImageData by rendering to FBO and reading back.
 *
 * Results:
 *	Pointer to XImage, or NULL on failure.
 *	Caller must free with DestroyImage().
 *
 * Side effects:
 *	Creates temporary OpenGL framebuffer objects.
 *
 *----------------------------------------------------------------------
 */

static XImage*
TkWaylandCreateXImageWithNVGImage(
    NVGcontext* vg,
    NVGImageData* nvgImage,
    Display* display)
{
    if (!vg || !nvgImage || nvgImage->id <= 0) {
        return NULL;
    }
    
    int width, height;
    nvgImageSize(vg, nvgImage->id, &width, &height);
    
    if (width <= 0 || height <= 0) {
        return NULL;
    }
    
    GLuint fbo = 0, texture = 0;
    GLint oldFbo = 0;
    XImage* ximage = NULL;
    unsigned char* pixelData = NULL;
    
    /* Save current FBO. */
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    
    /* Create texture for rendering. */
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, 
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    /* Create FBO. */
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                          GL_TEXTURE_2D, texture, 0);
    
    /* Verify FBO is complete. */
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        goto cleanup;
    }
    
    /* Set viewport and clear. */
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    
    /* Render NVG image to FBO. */
    nvgSave(vg);
    nvgResetTransform(vg);
    
    NVGpaint imgPaint = nvgImagePattern(vg, 0, 0, width, height, 
                                        0, nvgImage->id, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, width, height);
    nvgFillPaint(vg, imgPaint);
    nvgFill(vg);
    
    nvgRestore(vg);
    
    /* Read pixels back. */
    pixelData = (unsigned char*)Tcl_Alloc(width * height * 4);
    if (!pixelData) {
        goto cleanup;
    }
    
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixelData);
    
    /* Create XImage. */
    ximage = XCreateImage(display, NULL, 32, ZPixmap, 0, 
                         (char*)pixelData, width, height, 32, 0);
    
    if (ximage) {
        /* Convert RGBA to ARGB for XImage. */
        unsigned char* data = (unsigned char*)ximage->data;
        for (int i = 0; i < width * height; i++) {
            unsigned char r = data[i*4 + 0];
            unsigned char g = data[i*4 + 1];
            unsigned char b = data[i*4 + 2];
            unsigned char a = data[i*4 + 3];
            
            /* Store as ARGB in little-endian format [B,G,R,A] */
            data[i*4 + 0] = b;
            data[i*4 + 1] = g;
            data[i*4 + 2] = r;
            data[i*4 + 3] = a;
        }
    } else {
        Tcl_Free(pixelData);
    }
    
cleanup:
    /* Restore OpenGL state. */
    glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
    if (texture) glDeleteTextures(1, &texture);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    
    return ximage;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateNVGImageFromDrawableRect --
 *
 *	Capture a rectangular region from a drawable as NVG image.
 *
 * Results:
 *	Pointer to NVGImageData, or NULL on failure.
 *
 * Side effects:
 *	Creates OpenGL framebuffer to capture the region.
 *
 *----------------------------------------------------------------------
 */

static NVGImageData*
CreateNVGImageFromDrawableRect(
    WaylandDrawable* drawable,
    int x, int y,
    unsigned int width,
    unsigned int height)
{
    if (!drawable || !drawable->vg) {
        return NULL;
    }
    
    /* Allocate pixel buffer .*/
    unsigned char* pixels = (unsigned char*)Tcl_Alloc(width * height * 4);
    if (!pixels) {
        return NULL;
    }
    
    /* Read pixels from current framebuffer. */
    glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    
    /* Create NanoVG image. */
    int imageId = nvgCreateImageRGBA(drawable->vg, width, height, 
                                     NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY, 
                                     pixels);
    
    Tcl_Free(pixels);
    
    if (imageId <= 0) {
        return NULL;
    }
    
    NVGImageData* nvgImg = (NVGImageData*)Tcl_Alloc(sizeof(NVGImageData));
    if (nvgImg) {
        nvgImg->id = imageId;
        nvgImg->width = width;
        nvgImg->height = height;
        nvgImg->flags = NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY;
    } else {
        nvgDeleteImage(drawable->vg, imageId);
    }
    
    return nvgImg;
}

/*
 *----------------------------------------------------------------------
 *
 * ConvertTkImageToNVG --
 *
 *	Convert Tk_Image (specifically Tk photo image) to NVG image.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Creates a new NVG image that must be deleted by caller.
 *
 *----------------------------------------------------------------------
 */

static int
ConvertTkImageToNVG(
    Tk_Window tkwin,
    Tk_Image image,
    NVGcontext* vg,
    NVGImageData** nvgImage)
{
    if (!vg || !image || !nvgImage) {
        return TCL_ERROR;
    }
    
    *nvgImage = NULL;
    
    Tcl_Interp *interp = Tk_Interp(tkwin);
    
    /* Get photo image handle. */
	const char *imageName = Tk_NameOfImage((Tk_ImageModel)image);
	Tk_PhotoHandle photoHandle = Tk_FindPhoto(interp, imageName);
	if (!photoHandle) {
	    Tcl_SetResult(interp, "not a photo image", TCL_STATIC); 
	    return TCL_ERROR;
	}
    
    /* Get image dimensions. */
    int width, height;
    Tk_PhotoGetSize(photoHandle, &width, &height);
    
    if (width <= 0 || height <= 0) {
        return TCL_ERROR;
    }
    
    /* Get photo block. */
    Tk_PhotoImageBlock block;
    if (Tk_PhotoGetImage(photoHandle, &block) != TCL_OK) {
        return TCL_ERROR;
    }
    
    /* Convert to RGBA format for NanoVG. */
    unsigned char* rgbaData = (unsigned char*)Tcl_Alloc(width * height * 4);
    if (!rgbaData) {
        return TCL_ERROR;
    }
    
    unsigned char* dst = rgbaData;
    for (int y = 0; y < height; y++) {
        unsigned char* srcRow = block.pixelPtr + y * block.pitch;
        for (int x = 0; x < width; x++) {
            unsigned char* src = srcRow + x * block.pixelSize;
            dst[0] = src[block.offset[0]];  /* R */
            dst[1] = src[block.offset[1]];  /* G */
            dst[2] = src[block.offset[2]];  /* B */
            dst[3] = (block.offset[3] >= 0) ? src[block.offset[3]] : 255;  /* A */
            dst += 4;
        }
    }
    
    /* Create NanoVG image. */
    int imageId = nvgCreateImageRGBA(vg, width, height, 
                                     NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY, 
                                     rgbaData);
    
    Tcl_Free(rgbaData);
    
    if (imageId <= 0) {
        return TCL_ERROR;
    }
    
    *nvgImage = (NVGImageData*)Tcl_Alloc(sizeof(NVGImageData));
    if (!*nvgImage) {
        nvgDeleteImage(vg, imageId);
        return TCL_ERROR;
    }
    
    (*nvgImage)->id = imageId;
    (*nvgImage)->width = width;
    (*nvgImage)->height = height;
    (*nvgImage)->flags = NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY;
    
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ConvertNVGToTkImage --
 *
 *	Convert NVG image to Tk photo image.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Updates the Tk photo image data.
 *
 *----------------------------------------------------------------------
 */

static int
ConvertNVGToTkImage(
    NVGcontext* vg,
    NVGImageData* nvgImage,
    Tk_PhotoHandle photoHandle)
{
    if (!vg || !nvgImage || !photoHandle) {
        return TCL_ERROR;
    }
    
    int width, height;
    nvgImageSize(vg, nvgImage->id, &width, &height);
    
    if (width <= 0 || height <= 0) {
        return TCL_ERROR;
    }
    
    /* Create temporary FBO to read image data. */
    GLuint fbo = 0, texture = 0;
    GLint oldFbo = 0;
    unsigned char* pixelData = NULL;
    int result = TCL_ERROR;
    
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    
    /* Create texture. */
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, 
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    /* Create FBO. */
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                          GL_TEXTURE_2D, texture, 0);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        goto cleanup;
    }
    
    /* Render NVG image. */
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    
    nvgSave(vg);
    nvgResetTransform(vg);
    
    NVGpaint imgPaint = nvgImagePattern(vg, 0, 0, width, height, 
                                        0, nvgImage->id, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, width, height);
    nvgFillPaint(vg, imgPaint);
    nvgFill(vg);
    
    nvgRestore(vg);
    
    /* Read pixels. */
    pixelData = (unsigned char*)Tcl_Alloc(width * height * 4);
    if (!pixelData) {
        goto cleanup;
    }
    
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixelData);
    
    /* Update Tk photo image. */
    Tk_PhotoImageBlock block;
    block.width = width;
    block.height = height;
    block.pixelSize = 4;
    block.pitch = width * 4;
    block.offset[0] = 0;  /* R */
    block.offset[1] = 1;  /* G */
    block.offset[2] = 2;  /* B */
    block.offset[3] = 3;  /* A */
    block.pixelPtr = pixelData;
    
    if (Tk_PhotoPutBlock(NULL, photoHandle, &block, 0, 0, width, height,
                        TK_PHOTO_COMPOSITE_SET) == TCL_OK) {
        result = TCL_OK;
    }
    
cleanup:
    if (pixelData) Tcl_Free(pixelData);
    glBindFramebuffer(GL_FRAMEBUFFER, oldFbo);
    if (texture) glDeleteTextures(1, &texture);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    
    return result;
}

/* Xlib-Compatible Image Functions. */

/*
 *----------------------------------------------------------------------
 *
 * XCreateImage --
 *
 *	Create a new XImage structure.
 *
 * Results:
 *	Pointer to XImage.
 *
 * Side effects:
 *	Allocates memory.
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
    XImage *img = (XImage*)Tcl_Alloc(sizeof(XImage));
    if (!img) {
        return NULL;
    }
    
    img->width = width;
    img->height = height;
    img->xoffset = offset;
    img->format = format;
    img->data = data;
    img->byte_order = LSBFirst;  /* Wayland/OpenGL uses little-endian. */
    img->bitmap_unit = 32;
    img->bitmap_bit_order = LSBFirst;
    img->bitmap_pad = bitmap_pad;
    img->depth = depth;
    
    if (format == ZPixmap) {
        img->bits_per_pixel = (depth == 1) ? 1 : 32;
    } else {
        img->bits_per_pixel = 1;
    }
    
    if (bytes_per_line == 0) {
        img->bytes_per_line = (width * img->bits_per_pixel + 7) / 8;
    } else {
        img->bytes_per_line = bytes_per_line;
    }
    
    img->red_mask = 0x00FF0000;
    img->green_mask = 0x0000FF00;
    img->blue_mask = 0x000000FF;
    
    return img;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetImage --
 *
 *	Copy image data from a drawable into an XImage.
 *
 * Results:
 *	Pointer to XImage, or NULL on failure.
 *
 * Side effects:
 *	Allocates memory.
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
    if (format != ZPixmap) {
        return NULL;
    }
    
    WaylandDrawable* wlDraw = GetWaylandDrawable(drawable);
    if (!wlDraw || width == 0 || height == 0) {
        return NULL;
    }
    
    /* Capture region from drawable. */
    NVGImageData* nvgImg = CreateNVGImageFromDrawableRect(wlDraw, x, y, 
                                                          width, height);
    if (!nvgImg) {
        return NULL;
    }
    
    /* Convert to XImage. */
    XImage* imagePtr = TkWaylandCreateXImageWithNVGImage(wlDraw->vg, 
                                                         nvgImg, display);
    
    /* Clean up */
    nvgDeleteImage(wlDraw->vg, nvgImg->id);
    Tcl_Free(nvgImg);
    
    return imagePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyArea --
 *
 *	Copy image data from one drawable to another using NanoVG.
 *
 * Results:
 *	Success or error code.
 *
 * Side effects:
 *	Modifies destination drawable.
 *
 *----------------------------------------------------------------------
 */

int
XCopyArea(
    Display *display,
    Drawable src,
    Drawable dst,
    TCL_UNUSED(GC), /* gc */
    int src_x,
    int src_y,
    unsigned int width,
    unsigned int height,
    int dst_x,
    int dst_y)
{
    WaylandDrawable* srcDraw = GetWaylandDrawable(src);
    WaylandDrawable* dstDraw = GetWaylandDrawable(dst);
    
    LastKnownRequestProcessed(display)++;
    
    if (!dstDraw || !width || !height) {
        return BadDrawable;
    }
    
    /* Capture source region. */
    NVGImageData* srcImg = CreateNVGImageFromDrawableRect(srcDraw, src_x, src_y, 
                                                          width, height);
    if (!srcImg) {
        return BadDrawable;
    }
    
    NVGcontext* vg = dstDraw->vg;
    
    /* Draw to destination. */
    nvgSave(vg);
    
    /* Draw image. */
    NVGpaint imgPaint = nvgImagePattern(vg, dst_x, dst_y, width, height, 
                                        0, srcImg->id, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, dst_x, dst_y, width, height);
    nvgFillPaint(vg, imgPaint);
    nvgFill(vg);
    
    nvgRestore(vg);
    
    /* Clean up. */
    nvgDeleteImage(vg, srcImg->id);
    Tcl_Free(srcImg);
    
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyPlane --
 *
 *	Copy a bitmap plane from source to destination.
 *	For Wayland, we treat this as XCopyArea.
 *
 * Results:
 *	Success or error code.
 *
 * Side effects:
 *	Modifies destination drawable.
 *
 *----------------------------------------------------------------------
 */

int
XCopyPlane(
    Display *display,
    Drawable src,
    Drawable dst,
    GC gc,
    int src_x,
    int src_y,
    unsigned int width,
    unsigned int height,
    int dest_x,
    int dest_y,
    TCL_UNUSED(unsigned long)) /* plane */
{
    return XCopyArea(display, src, dst, gc, src_x, src_y, 
                     width, height, dest_x, dest_y);
}

/*
 *----------------------------------------------------------------------
 *
 * TkScrollWindow --
 *
 *	Scroll a rectangular region of a window.
 *
 * Results:
 *	Returns 1 if scroll generated damage, 0 otherwise.
 *
 * Side effects:
 *	Scrolls pixels and updates damage region.
 *
 *----------------------------------------------------------------------
 */

int
TkScrollWindow(
    Tk_Window tkwin,
    GC gc,
    int x, int y,
    int width, int height,
    int dx, int dy,
    Region damageRgn)
{
    if (dx == 0 && dy == 0) {
        return 0;
    }
    
    Drawable drawable = Tk_WindowId(tkwin);
    Display* display = Tk_Display(tkwin);
    
    /* Perform the scroll using XCopyArea. */
    XCopyArea(display, drawable, drawable, gc, 
              x, y, width, height, x + dx, y + dy);
    
    /* Calculate exposed regions */
    XRectangle rect;
    
    if (dx > 0) {
        /* Exposed area on left */
        rect.x = x;
        rect.y = y;
        rect.width = dx;
        rect.height = height;
        XUnionRectWithRegion(&rect, damageRgn, damageRgn);
    } else if (dx < 0) {
        /* Exposed area on right */
        rect.x = x + width + dx;
        rect.y = y;
        rect.width = -dx;
        rect.height = height;
        XUnionRectWithRegion(&rect, damageRgn, damageRgn);
    }
    
    if (dy > 0) {
        /* Exposed area on top */
        rect.x = x;
        rect.y = y;
        rect.width = width;
        rect.height = dy;
        XUnionRectWithRegion(&rect, damageRgn, damageRgn);
    } else if (dy < 0) {
        /* Exposed area on bottom */
        rect.x = x;
        rect.y = y + height + dy;
        rect.width = width;
        rect.height = -dy;
        XUnionRectWithRegion(&rect, damageRgn, damageRgn);
    }
    
    return 1;
}

/*  Public API Functions. */

/*
 *----------------------------------------------------------------------
 *
 * TkGetNVGImageFromTkImage --
 *
 *	Convert Tk_Image to NVG image (public API).
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Creates NVG image that must be freed by caller.
 *
 *----------------------------------------------------------------------
 */

int
TkGetNVGImageFromTkImage(
    Tk_Window tkwin,
    Tk_Image image,
    NVGcontext* vg,
    NVGImageData** nvgImage)
{
    return ConvertTkImageToNVG(tkwin, image, vg, nvgImage);
}

/*
 *----------------------------------------------------------------------
 *
 * TkPutTkImageFromNVGImage --
 *
 *	Convert NVG image to Tk photo image (public API).
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Updates Tk photo image data.
 *
 *----------------------------------------------------------------------
 */

int
TkPutTkImageFromNVGImage(
    NVGcontext* vg,
    NVGImageData* nvgImage,
    Tk_PhotoHandle photoHandle)
{
    return ConvertNVGToTkImage(vg, nvgImage, photoHandle);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
