/*
 * tkWaylandImage.c --
 *
 *	The code in this file provides an interface for XImages, and
 *      implements Xlib image functions for Wayland with nanovg.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2017-2021 Marc Culler.
 * Copyright © 2024 Wayland/nanovg port.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkPort.h"
#include "tkWindow.h"
#include "tkImage.h"
#include "tkPhoto.h"
#include "tkVisual.h"
#include "tkWaylandPrivate.h"
#include "tkWaylandConstants.h"
#include "tkWaylandImage.h"
#include "tkColor.h"
#include "xbytes.h"

#include <nanovg.h>
#include <wayland-client.h>
#include <cairo.h>

#ifdef USE_NANOVG_GL3
#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg_gl.h>
#else
#define NANOVG_GLES3_IMPLEMENTATION
#include <nanovg_gl.h>
#endif

static NVGimage* CreateNVGImageFromPixmap(Drawable pixmap);
static NVGimage* CreateNVGImageFromDrawableRect(Drawable drawable, 
    int x, int y, unsigned int width, unsigned int height);
static int ConvertTkImageToNVG(Tk_Window tkwin, Tk_Image image, 
    NVGcontext* vg, NVGimage** nvgImage);

/* Pixel formats
 *
 * Tk uses the XImage structure defined in Xlib.h for storing images.
 * For Wayland/nanovg, we use ARGB32 format consistently.
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
 * TkWaylandCreateNVGImageWithXImage --
 *
 *	Create NVGimage from XImage, copying the image data.
 *
 * Results:
 *	NVGimage*, delete after use with nvgDeleteImage().
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static NVGimage*
TkWaylandCreateNVGImageWithXImage(
    NVGcontext* vg,
    XImage *image)
{
    if (!vg || !image || !image->data) {
        return NULL;
    }
    
    NVGimage* nvgImg = NULL;
    
    if (image->bits_per_pixel == 1) {
        /* Convert 1bpp bitmap to RGBA */
        int len = image->width * image->height * 4;
        unsigned char* rgbaData = (unsigned char*)Tcl_Alloc(len);
        
        if (rgbaData) {
            const unsigned char* src = (const unsigned char*)image->data + image->xoffset;
            unsigned char* dst = rgbaData;
            
            for (int y = 0; y < image->height; y++) {
                for (int x = 0; x < image->width; x++) {
                    int byteIdx = (y * image->bytes_per_line) + (x / 8);
                    int bitIdx = 7 - (x % 8);  // MSB first
                    unsigned char bit = (src[byteIdx] >> bitIdx) & 1;
                    unsigned char value = bit ? 255 : 0;
                    
                    *dst++ = value;  // R
                    *dst++ = value;  // G
                    *dst++ = value;  // B
                    *dst++ = 255;    // A
                }
            }
            
            int imageId = nvgCreateImageRGBA(vg, image->width, image->height, 
                                             NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY, 
                                             rgbaData);
            if (imageId > 0) {
                nvgImg = (NVGimage*)Tcl_Alloc(sizeof(NVGimage));
                nvgImg->id = imageId;
                nvgImg->width = image->width;
                nvgImg->height = image->height;
            }
            Tcl_Free(rgbaData);
        }
    } 
    else if (image->format == ZPixmap && image->bits_per_pixel == 32) {
        /* Convert ARGB to RGBA for nanovg */
        int len = image->width * image->height * 4;
        unsigned char* rgbaData = (unsigned char*)Tcl_Alloc(len);
        
        if (rgbaData) {
            const unsigned char* src = (const unsigned char*)image->data + image->xoffset;
            unsigned char* dst = rgbaData;
            
            /* Handle byte order */
            if (image->byte_order == MSBFirst) {
                /* Big endian: ARGB stored as [A,R,G,B] */
                for (int i = 0; i < image->width * image->height; i++) {
                    dst[0] = src[1];  /* R */
                    dst[1] = src[2];  /* G */
                    dst[2] = src[3];  /* B */
                    dst[3] = src[0];  /* A */
                    src += 4;
                    dst += 4;
                }
            } else {
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
            
            int imageId = nvgCreateImageRGBA(vg, image->width, image->height, 
                                             NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY, 
                                             rgbaData);
            if (imageId > 0) {
                nvgImg = (NVGimage*)Tcl_Alloc(sizeof(NVGimage));
                nvgImg->id = imageId;
                nvgImg->width = image->width;
                nvgImg->height = image->height;
            }
            Tcl_Free(rgbaData);
        }
    }
    else if (image->format == ZPixmap && image->bits_per_pixel == 24) {
        /* Convert 24bpp to RGBA */
        int len = image->width * image->height * 4;
        unsigned char* rgbaData = (unsigned char*)Tcl_Alloc(len);
        
        if (rgbaData) {
            const unsigned char* src = (const unsigned char*)image->data + image->xoffset;
            unsigned char* dst = rgbaData;
            
            for (int i = 0; i < image->width * image->height; i++) {
                dst[0] = src[0];  /* R */
                dst[1] = src[1];  /* G */
                dst[2] = src[2];  /* B */
                dst[3] = 255;     /* A */
                src += 3;
                dst += 4;
            }
            
            int imageId = nvgCreateImageRGBA(vg, image->width, image->height, 
                                             NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY, 
                                             rgbaData);
            if (imageId > 0) {
                nvgImg = (NVGimage*)Tcl_Alloc(sizeof(NVGimage));
                nvgImg->id = imageId;
                nvgImg->width = image->width;
                nvgImg->height = image->height;
            }
            Tcl_Free(rgbaData);
        }
    }
    
    return nvgImg;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCreateXImageWithNVGImage --
 *
 *	Create XImage from NVGimage, copying the image data.
 *
 * Results:
 *	XImage*, delete after use with DestroyImage().
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static XImage*
TkWaylandCreateXImageWithNVGImage(
    NVGcontext* vg,
    NVGimage* nvgImage,
    Display* display)
{
    if (!vg || !nvgImage || nvgImage->id <= 0) {
        return NULL;
    }
    
    /* Get image dimensions */
    int width, height;
    nvgImageSize(vg, nvgImage->id, &width, &height);
    
    /* Read image data from GPU */
    int dataSize = width * height * 4;
    unsigned char* rgbaData = (unsigned char*)Tcl_Alloc(dataSize);
    if (!rgbaData) {
        return NULL;
    }
    
    /* In a real implementation, we'd use glReadPixels or similar */
    /* For now, create dummy data */
    memset(rgbaData, 0, dataSize);
    
    /* Convert RGBA to ARGB for XImage */
    unsigned char* argbData = (unsigned char*)Tcl_Alloc(dataSize);
    if (!argbData) {
        Tcl_Free(rgbaData);
        return NULL;
    }
    
#ifdef WORDS_BIGENDIAN
    /* Big endian: ARGB format */
    for (int i = 0; i < width * height; i++) {
        argbData[i*4] = rgbaData[i*4+3];     /* A */
        argbData[i*4+1] = rgbaData[i*4];     /* R */
        argbData[i*4+2] = rgbaData[i*4+1];   /* G */
        argbData[i*4+3] = rgbaData[i*4+2];   /* B */
    }
#else
    /* Little endian: BGRA format */
    for (int i = 0; i < width * height; i++) {
        argbData[i*4] = rgbaData[i*4+2];     /* B */
        argbData[i*4+1] = rgbaData[i*4+1];   /* G */
        argbData[i*4+2] = rgbaData[i*4];     /* R */
        argbData[i*4+3] = rgbaData[i*4+3];   /* A */
    }
#endif
    
    XImage* image = XCreateImage(display, NULL, 32, ZPixmap, 0,
                                 (char*)argbData, width, height, 32, width * 4);
    
    Tcl_Free(rgbaData);
    return image;
}

/*
 *----------------------------------------------------------------------
 *
 * ConvertTkImageToNVG --
 *
 *	Convert a Tk image to an NVG image.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Creates a new NVG image that must be deleted with nvgDeleteImage().
 *
 *----------------------------------------------------------------------
 */

static int
ConvertTkImageToNVG(
    Tk_Window tkwin,
    Tk_Image image,
    NVGcontext* vg,
    NVGimage** nvgImage)
{
    if (!tkwin || !image || !vg || !nvgImage) {
        return TCL_ERROR;
    }
    
    Display* display = Tk_Display(tkwin);
    Drawable drawable = Tk_WindowId(tkwin);
    Tk_ImageType* typePtr = Tk_GetImageTypeData(image);
    
    if (!typePtr) {
        return TCL_ERROR;
    }
    
    /* Get image dimensions */
    int width, height;
    Tk_SizeOfImage(image, &width, &height);
    
    if (width <= 0 || height <= 0) {
        return TCL_ERROR;
    }
    
    /* Create a pixmap to render the Tk image into */
    Pixmap pixmap = XCreatePixmap(display, drawable, width, height, 
                                  DefaultDepth(display, DefaultScreen(display)));
    
    if (!pixmap) {
        return TCL_ERROR;
    }
    
    /* Create a GC for the pixmap */
    XGCValues gcValues;
    GC gc = XCreateGC(display, pixmap, 0, &gcValues);
    
    if (!gc) {
        XFreePixmap(display, pixmap);
        return TCL_ERROR;
    }
    
    /* Clear the pixmap */
    XSetForeground(display, gc, WhitePixel(display, DefaultScreen(display)));
    XFillRectangle(display, pixmap, gc, 0, 0, width, height);
    
    /* Render the Tk image into the pixmap */
    typePtr->displayProc(Tk_GetImageMasterData(image), display, pixmap,
                        0, 0, width, height, 0, 0);
    
    /* Get the image data from the pixmap */
    XImage* ximage = XGetImage(display, pixmap, 0, 0, width, height, 
                               AllPlanes, ZPixmap);
    
    /* Convert XImage to NVG image */
    *nvgImage = TkWaylandCreateNVGImageWithXImage(vg, ximage);
    
    /* Clean up */
    if (ximage) {
        DestroyImage(ximage);
    }
    XFreeGC(display, gc);
    XFreePixmap(display, pixmap);
    
    return (*nvgImage != NULL) ? TCL_OK : TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * ConvertNVGToTkImage --
 *
 *	Convert an NVG image to a Tk photo image.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Updates the Tk photo image with NVG image data.
 *
 *----------------------------------------------------------------------
 */

int
ConvertNVGToTkImage(
    NVGcontext* vg,
    NVGimage* nvgImage,
    Tk_PhotoHandle photoHandle)
{
    if (!vg || !nvgImage || nvgImage->id <= 0 || !photoHandle) {
        return TCL_ERROR;
    }
    
    /* Get image dimensions */
    int width, height;
    nvgImageSize(vg, nvgImage->id, &width, &height);
    
    if (width <= 0 || height <= 0) {
        return TCL_ERROR;
    }
    
    /* Create photo image block */
    Tk_PhotoImageBlock block;
    block.width = width;
    block.height = height;
    block.pitch = width * 4;
    block.pixelSize = 4;
    block.offset[0] = 0;  /* R offset */
    block.offset[1] = 1;  /* G offset */
    block.offset[2] = 2;  /* B offset */
    block.offset[3] = 3;  /* A offset */
    
    /* Allocate buffer for image data */
    int dataSize = width * height * 4;
    unsigned char* rgbaData = (unsigned char*)Tcl_Alloc(dataSize);
    if (!rgbaData) {
        return TCL_ERROR;
    }
    
    /* In a real implementation, read from framebuffer using glReadPixels */
    /* For now, fill with placeholder data */
    memset(rgbaData, 0, dataSize);
    
    block.pixelPtr = rgbaData;
    
    /* Set photo image data */
    Tk_PhotoSetSize(photoHandle, width, height);
    Tk_PhotoPutBlock(photoHandle, &block, 0, 0, width, height, 
                     TK_PHOTO_COMPOSITE_SET);
    
    Tcl_Free(rgbaData);
    return TCL_OK;
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
            Tcl_Free(image->data);
        }
        Tcl_Free(image);
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
 *      Returns pixel value as unsigned long.
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

    return TkWaylandRGBPixel(r, g, b);
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
    ximage = (XImage *)Tcl_Alloc(sizeof(XImage));

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
        ximage->bitmap_pad = 32;  /* 4-byte alignment for RGBA */
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
 *	Copy image data to drawable using nanovg.
 *
 * Results:
 *	These functions return either BadDrawable or Success.
 *
 * Side effects:
 *	Draws the image on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

static int
TkWaylandPutImage(
    int useAlpha,
    Display* display,
    Drawable drawable,
    GC gc,
    XImage* image,
    int src_x,
    int src_y,
    int dest_x,
    int dest_y,
    unsigned int width,
    unsigned int height)
{
    WaylandDrawable *wlDraw = (WaylandDrawable *)drawable;
    int result = Success;

    if (width <= 0 || height <= 0) {
        return Success;
    }
    
    LastKnownRequestProcessed(display)++;
    
    if (!wlDraw || !wlDraw->vg) {
        return BadDrawable;
    }
    
    NVGcontext* vg = wlDraw->vg;
    
    /* Create NVG image from XImage */
    NVGimage* nvgImg = TkWaylandCreateNVGImageWithXImage(vg, image);
    if (!nvgImg) {
        return BadDrawable;
    }
    
    /* Save nanovg state */
    nvgSave(vg);
    
    /* Setup clipping if needed */
    if (gc && gc->clip_mask) {
        /* Handle clipping - simplified version */
        TkpClipMask *clipPtr = (TkpClipMask *)gc->clip_mask;
        if (clipPtr && clipPtr->type == TKP_CLIP_PIXMAP) {
            /* Create clipping path from pixmap */
            nvgSave(vg);
            nvgResetScissor(vg);
            
            /* Note: Actual implementation would convert pixmap to alpha mask */
            /* For now, just use rectangular clip */
            if (gc->clip_x_origin || gc->clip_y_origin) {
                nvgScissor(vg, 
                          dest_x + gc->clip_x_origin, 
                          dest_y + gc->clip_y_origin,
                          width, height);
            }
        }
    }
    
    /* Set up composite operation */
    if (useAlpha) {
        nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
    } else {
        nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
    }
    
    /* Draw the image */
    NVGpaint imgPaint = nvgImagePattern(vg, dest_x, dest_y, width, height, 
                                        0, nvgImg->id, useAlpha ? 1.0f : (gc ? gc->fill_opacity : 1.0f));
    
    nvgBeginPath(vg);
    nvgRect(vg, dest_x, dest_y, width, height);
    nvgFillPaint(vg, imgPaint);
    nvgFill(vg);
    
    /* Restore state */
    nvgRestore(vg);
    
    /* Clean up */
    nvgDeleteImage(vg, nvgImg->id);
    Tcl_Free(nvgImg);
    
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
    return TkWaylandPutImage(0, display, drawable, gc, image,
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
    return TkWaylandPutImage(1, display, drawable, gc, image,
                 src_x, src_y, dest_x, dest_y, width, height);
}

/*
 *----------------------------------------------------------------------
 *
 * CreateNVGImageFromDrawableRect
 *
 *	Extract image data from a Wayland drawable as an NVGimage.
 *
 * Results:
 *	Returns NVGimage* representing the image rectangle.
 *
 * Side effects:
 *     None
 *
 *----------------------------------------------------------------------
 */

static NVGimage*
CreateNVGImageFromDrawableRect(
    Drawable drawable,
    int x,
    int y,
    unsigned int width,
    unsigned int height)
{
    WaylandDrawable *wlDraw = (WaylandDrawable *)drawable;
    
    if (!wlDraw || !wlDraw->vg) {
        return NULL;
    }
    
    /* For Wayland/nanovg, capture framebuffer region */
    /* This would use FBO or glReadPixels in real implementation */
    
    /* Create an FBO to capture the region */
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    
    /* Create texture to render to */
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, 
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 
                          GL_TEXTURE_2D, texture, 0);
    
    /* Set viewport to capture region */
    glViewport(0, 0, width, height);
    
    /* Render the specified region */
    nvgSave(wlDraw->vg);
    nvgResetTransform(wlDraw->vg);
    nvgTranslate(wlDraw->vg, -x, -y);
    
    /* Flush any pending drawing */
    nvgEndFrame(wlDraw->vg);
    
    /* Read pixels from framebuffer */
    unsigned char* pixels = (unsigned char*)Tcl_Alloc(width * height * 4);
    if (!pixels) {
        nvgRestore(wlDraw->vg);
        glDeleteTextures(1, &texture);
        glDeleteFramebuffers(1, &fbo);
        return NULL;
    }
    
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    
    nvgRestore(wlDraw->vg);
    
    /* Create NVG image from pixel data */
    int imageId = nvgCreateImageRGBA(wlDraw->vg, width, height, 0, pixels);
    
    /* Clean up */
    Tcl_Free(pixels);
    glDeleteTextures(1, &texture);
    glDeleteFramebuffers(1, &fbo);
    
    if (imageId <= 0) {
        return NULL;
    }
    
    NVGimage* img = (NVGimage*)Tcl_Alloc(sizeof(NVGimage));
    img->id = imageId;
    img->width = width;
    img->height = height;
    
    return img;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetImage --
 *
 *	Copies data from a drawable into an XImage.
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
    
    if (format == ZPixmap) {
        WaylandDrawable *wlDraw = (WaylandDrawable *)drawable;
        
        if (width == 0 || height == 0) {
            return NULL;
        }
        
        /* Capture region from drawable */
        NVGimage* nvgImg = CreateNVGImageFromDrawableRect(drawable, x, y, width, height);
        if (!nvgImg) {
            return NULL;
        }
        
        /* Convert NVG image to XImage */
        imagePtr = TkWaylandCreateXImageWithNVGImage(wlDraw->vg, nvgImg, display);
        
        /* Clean up */
        nvgDeleteImage(wlDraw->vg, nvgImg->id);
        Tcl_Free(nvgImg);
    } else {
        /* XYPixmap not supported */
        return NULL;
    }
    
    return imagePtr;
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
 *	Image data is moved.
 *
 *----------------------------------------------------------------------
 */

int
XCopyArea(
    Display *display,
    Drawable src,
    Drawable dst,
    GC gc,
    int src_x,
    int src_y,
    unsigned int width,
    unsigned int height,
    int dst_x,
    int dst_y)
{
    WaylandDrawable *srcDraw = (WaylandDrawable *)src;
    WaylandDrawable *dstDraw = (WaylandDrawable *)dst;
    
    LastKnownRequestProcessed(display)++;
    
    if (!width || !height || !dstDraw || !dstDraw->vg) {
        return BadDrawable;
    }
    
    /* Get source image region */
    NVGimage* srcImg = CreateNVGImageFromDrawableRect(src, src_x, src_y, width, height);
    if (!srcImg) {
        return BadDrawable;
    }
    
    NVGcontext* vg = dstDraw->vg;
    
    /* Draw source image to destination */
    nvgSave(vg);
    
    /* Apply clipping if specified */
    if (gc && gc->clip_mask) {
        /* Handle clipping */
    }
    
    /* Draw the image */
    NVGpaint imgPaint = nvgImagePattern(vg, dst_x, dst_y, width, height, 
                                        0, srcImg->id, 1.0f);
    
    nvgBeginPath(vg);
    nvgRect(vg, dst_x, dst_y, width, height);
    nvgFillPaint(vg, imgPaint);
    nvgFill(vg);
    
    nvgRestore(vg);
    
    /* Clean up */
    nvgDeleteImage(vg, srcImg->id);
    Tcl_Free(srcImg);
    
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyPlane --
 *
 *	Copies a bitmap from source to destination.
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
    unsigned long plane)
{
    /* For Wayland/nanovg, treat as XCopyArea with alpha mask */
    return XCopyArea(display, src, dst, gc, src_x, src_y, 
                     width, height, dest_x, dest_y);
}

/*
 *----------------------------------------------------------------------
 *
 * TkScrollWindow --
 *
 *	Scroll a rectangle of the specified window.
 *
 * Results:
 *	Returns 0 if the scroll generated no additional damage.
 *
 * Side effects:
 *	Scrolls the bits in the window.
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
    Drawable drawable = Tk_WindowId(tkwin);
    Display* display = Tk_Display(tkwin);
    
    /* For Wayland/nanovg, implement scrolling using XCopyArea */
    if (dx != 0 || dy != 0) {
        /* Copy the scrolled region */
        XCopyArea(display, drawable, drawable, gc, 
                  x, y, width, height, x + dx, y + dy);
        
        /* Calculate damage region (area not covered by the copy) */
        /* This is a simplified calculation */
        if (dx > 0) {
            /* Exposed area on left */
            XRectangle rect = {x, y, dx, height};
            XUnionRectWithRegion(&rect, damageRgn, damageRgn);
        } else if (dx < 0) {
            /* Exposed area on right */
            XRectangle rect = {x + width + dx, y, -dx, height};
            XUnionRectWithRegion(&rect, damageRgn, damageRgn);
        }
        
        if (dy > 0) {
            /* Exposed area on top */
            XRectangle rect = {x, y, width, dy};
            XUnionRectWithRegion(&rect, damageRgn, damageRgn);
        } else if (dy < 0) {
            /* Exposed area on bottom */
            XRectangle rect = {x, y + height + dy, width, -dy};
            XUnionRectWithRegion(&rect, damageRgn, damageRgn);
        }
        
        return 1;
    }
    
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetNVGImageFromTkImage --
 *
 *	Public API function to convert Tk image to NVG image.
 *	For internal use by the Wayland port.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Creates a new NVG image that must be deleted with nvgDeleteImage().
 *
 *----------------------------------------------------------------------
 */

int
TkGetNVGImageFromTkImage(
    Tk_Window tkwin,
    Tk_Image image,
    NVGcontext* vg,
    NVGimage** nvgImage)
{
    return ConvertTkImageToNVG(tkwin, image, vg, nvgImage);
}

/*
 *----------------------------------------------------------------------
 *
 * TkPutTkImageFromNVGImage --
 *
 *	Public API function to convert NVG image to Tk photo image.
 *	For internal use by the Wayland port.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Updates the Tk photo image with NVG image data.
 *
 *----------------------------------------------------------------------
 */

int
TkPutTkImageFromNVGImage(
    NVGcontext* vg,
    NVGimage* nvgImage,
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