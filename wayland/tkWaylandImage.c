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
#include <nanovg.h>

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
    Drawable drawable, int x, int y, 
    unsigned int width, unsigned int height);
static int ConvertTkImageToNVG(
    Tk_Window tkwin, Tk_Image image, 
    NVGcontext* vg, NVGImageData** nvgImage);
static int ConvertNVGToTkImage(
    NVGcontext* vg, NVGImageData* nvgImage, 
    Tk_PhotoHandle photoHandle);
static XImage* TkWaylandCreateXImageWithNVGImage(
    NVGcontext* vg, NVGImageData* nvgImage, Display* display);

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
    int imageId;
    
    /* Get GLFW window from drawable */
    glfwWindow = TkGlfwGetWindowFromDrawable(drawable);
    if (!glfwWindow) {
        return NULL;
    }
    
    /* Get NanoVG context */
    vg = TkGlfwGetNVGContext();
    if (!vg) {
        return NULL;
    }
    
    /* Make context current */
    glfwMakeContextCurrent(glfwWindow);
    
    /* Allocate pixel buffer */
    pixels = (unsigned char*)ckalloc(width * height * 4);
    if (!pixels) {
        return NULL;
    }
    
    /* Read pixels from current framebuffer */
    glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    
    /* Create NanoVG image */
    imageId = nvgCreateImageRGBA(vg, width, height, 
                                  NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY, 
                                  pixels);
    
    ckfree(pixels);
    
    if (imageId <= 0) {
        return NULL;
    }
    
    nvgImg = (NVGImageData*)ckalloc(sizeof(NVGImageData));
    if (nvgImg) {
        nvgImg->id = imageId;
        nvgImg->width = width;
        nvgImg->height = height;
        nvgImg->flags = NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY;
    } else {
        nvgDeleteImage(vg, imageId);
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
    Tcl_Interp *interp;
    const char *imageName;
    Tk_PhotoHandle photoHandle;
    Tk_PhotoImageBlock block;
    int width, height;
    unsigned char *rgbaData;
    int imageId;
    int x, y;
    unsigned char *src, *dst;
    
    if (!vg || !image || !nvgImage) {
        return TCL_ERROR;
    }
    
    *nvgImage = NULL;
    
    interp = Tk_Interp(tkwin);
    
    /* Get photo image handle */
    imageName = Tk_NameOfImage((Tk_ImageModel)image);
    photoHandle = Tk_FindPhoto(interp, imageName);
    if (!photoHandle) {
        Tcl_SetResult(interp, "not a photo image", TCL_STATIC); 
        return TCL_ERROR;
    }
    
    /* Get image dimensions */
    Tk_PhotoGetSize(photoHandle, &width, &height);
    
    if (width <= 0 || height <= 0) {
        return TCL_ERROR;
    }
    
    /* Get photo block */
    if (Tk_PhotoGetImage(photoHandle, &block) != TCL_OK) {
        return TCL_ERROR;
    }
    
    /* Convert to RGBA format for NanoVG */
    rgbaData = (unsigned char*)ckalloc(width * height * 4);
    if (!rgbaData) {
        return TCL_ERROR;
    }
    
    /* Copy and convert pixel data */
    for (y = 0; y < height; y++) {
        src = block.pixelPtr + y * block.pitch;
        dst = rgbaData + y * width * 4;
        
        for (x = 0; x < width; x++) {
            dst[0] = src[block.offset[0]];  /* R */
            dst[1] = src[block.offset[1]];  /* G */
            dst[2] = src[block.offset[2]];  /* B */
            dst[3] = (block.offset[3] >= 0) ? src[block.offset[3]] : 255;  /* A */
            
            src += block.pixelSize;
            dst += 4;
        }
    }
    
    /* Create NanoVG image */
    imageId = nvgCreateImageRGBA(vg, width, height, 0, rgbaData);
    
    ckfree(rgbaData);
    
    if (imageId <= 0) {
        return TCL_ERROR;
    }
    
    *nvgImage = (NVGImageData*)ckalloc(sizeof(NVGImageData));
    if (!*nvgImage) {
        nvgDeleteImage(vg, imageId);
        return TCL_ERROR;
    }
    
    (*nvgImage)->id = imageId;
    (*nvgImage)->width = width;
    (*nvgImage)->height = height;
    (*nvgImage)->flags = 0;
    
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
 *	Updates photo image data.
 *
 *----------------------------------------------------------------------
 */

static int
ConvertNVGToTkImage(
    NVGcontext* vg,
    NVGImageData* nvgImage,
    Tk_PhotoHandle photoHandle)
{
    unsigned char *rgbaData;
    Tk_PhotoImageBlock block;
    int x, y;
    unsigned char *src, *dst;
    
    if (!vg || !nvgImage || !photoHandle) {
        return TCL_ERROR;
    }
    
    /* Allocate buffer for image data */
    rgbaData = (unsigned char*)ckalloc(nvgImage->width * nvgImage->height * 4);
    if (!rgbaData) {
        return TCL_ERROR;
    }
    
    /* Read image data from NanoVG (note: not all NanoVG backends support this) */
    /* This is a placeholder - actual implementation depends on NanoVG backend */
    memset(rgbaData, 0, nvgImage->width * nvgImage->height * 4);
    
    /* Set up photo block */
    block.pixelPtr = rgbaData;
    block.width = nvgImage->width;
    block.height = nvgImage->height;
    block.pitch = nvgImage->width * 4;
    block.pixelSize = 4;
    block.offset[0] = 0;  /* R */
    block.offset[1] = 1;  /* G */
    block.offset[2] = 2;  /* B */
    block.offset[3] = 3;  /* A */
    
    /* Put image data into photo */
    if (Tk_PhotoPutBlock(NULL, photoHandle, &block, 0, 0,
                         nvgImage->width, nvgImage->height,
                         TK_PHOTO_COMPOSITE_SET) != TCL_OK) {
        ckfree(rgbaData);
        return TCL_ERROR;
    }
    
    ckfree(rgbaData);
    return TCL_OK;
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

static XImage*
TkWaylandCreateXImageWithNVGImage(
    NVGcontext* vg,
    NVGImageData* nvgImage,
    Display* display)
{
    XImage *imagePtr;
    char *data;
    
    if (!vg || !nvgImage) {
        return NULL;
    }
    
    /* Allocate XImage structure */
    imagePtr = (XImage*)ckalloc(sizeof(XImage));
    if (!imagePtr) {
        return NULL;
    }
    
    /* Allocate image data */
    data = (char*)ckalloc(nvgImage->width * nvgImage->height * 4);
    if (!data) {
        ckfree((char*)imagePtr);
        return NULL;
    }
    
    /* Initialize XImage structure */
    memset(imagePtr, 0, sizeof(XImage));
    imagePtr->width = nvgImage->width;
    imagePtr->height = nvgImage->height;
    imagePtr->xoffset = 0;
    imagePtr->format = ZPixmap;
    imagePtr->data = data;
    imagePtr->byte_order = LSBFirst;
    imagePtr->bitmap_unit = 32;
    imagePtr->bitmap_bit_order = LSBFirst;
    imagePtr->bitmap_pad = 32;
    imagePtr->depth = 24;
    imagePtr->bytes_per_line = nvgImage->width * 4;
    imagePtr->bits_per_pixel = 32;
    imagePtr->red_mask = 0xFF0000;
    imagePtr->green_mask = 0x00FF00;
    imagePtr->blue_mask = 0x0000FF;
    
    /* Read NVG image data (placeholder - actual implementation depends on backend) */
    memset(data, 0, nvgImage->width * nvgImage->height * 4);
    
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
    
    LastKnownRequestProcessed(display)++;
    
    /* Create NVG image from drawable region */
    nvgImg = CreateNVGImageFromDrawableRect(drawable, x, y, width, height);
    if (!nvgImg) {
        return NULL;
    }
    
    /* Get NanoVG context */
    vg = TkGlfwGetNVGContext();
    if (!vg) {
        nvgDeleteImage(vg, nvgImg->id);
        ckfree((char*)nvgImg);
        return NULL;
    }
    
    /* Convert to XImage */
    imagePtr = TkWaylandCreateXImageWithNVGImage(vg, nvgImg, display);
    
    /* Clean up NVG image */
    nvgDeleteImage(vg, nvgImg->id);
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
 *	Success.
 *
 * Side effects:
 *	Copies pixel data between drawables.
 *
 *----------------------------------------------------------------------
 */

int
XCopyArea(
    Display *display,
    Drawable src,
    Drawable dst,
    GC gc,
    int src_x, int src_y,
    unsigned int width,
    unsigned int height,
    int dest_x, int dest_y)
{
    TkWaylandDrawingContext dc;
    NVGImageData *srcImg;
    NVGpaint imgPaint;
    
    LastKnownRequestProcessed(display)++;
    
    /* Create NVG image from source region */
    srcImg = CreateNVGImageFromDrawableRect(src, src_x, src_y, width, height);
    if (!srcImg) {
        return BadDrawable;
    }
    
    /* Begin drawing on destination */
    if (TkGlfwBeginDraw(dst, gc, &dc) != TCL_OK) {
        nvgDeleteImage(dc.vg, srcImg->id);
        ckfree((char*)srcImg);
        return BadDrawable;
    }
    
    /* Create image pattern and draw */
    imgPaint = nvgImagePattern(dc.vg, dest_x, dest_y, width, height, 
                                0.0f, srcImg->id, 1.0f);
    
    nvgBeginPath(dc.vg);
    nvgRect(dc.vg, dest_x, dest_y, width, height);
    nvgFillPaint(dc.vg, imgPaint);
    nvgFill(dc.vg);
    
    /* Clean up */
    nvgDeleteImage(dc.vg, srcImg->id);
    ckfree((char*)srcImg);
    
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
    TkWaylandDrawingContext dc;
    int imageId;
    NVGpaint imgPaint;
    
    if (!image || !image->data) {
        return BadValue;
    }
    
    LastKnownRequestProcessed(display)++;
    
    /* Begin drawing */
    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return BadDrawable;
    }
    
    /* Create NanoVG image from XImage data */
    imageId = nvgCreateImageRGBA(dc.vg, image->width, image->height, 
                                  0, (unsigned char*)image->data);
    
    if (imageId <= 0) {
        TkGlfwEndDraw(&dc);
        return BadAlloc;
    }
    
    /* Create image pattern */
    imgPaint = nvgImagePattern(dc.vg, dest_x - src_x, dest_y - src_y,
                                image->width, image->height, 
                                0.0f, imageId, 1.0f);
    
    /* Draw the image */
    nvgBeginPath(dc.vg);
    nvgRect(dc.vg, dest_x, dest_y, width, height);
    nvgFillPaint(dc.vg, imgPaint);
    nvgFill(dc.vg);
    
    /* Clean up */
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
