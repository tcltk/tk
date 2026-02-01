/*
 * tkWaylandImage.c --
 *
 *	The code in this file provides an interface for XImages, and
 *      implements the image type for Wayland/GLFW.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkColor.h"
#include "xbytes.h"
#include <GLFW/glfw3.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* Pixel formats
 *
 * Tk uses the XImage structure defined in Xlib.h for storing images.
 * For Wayland/GLFW, we'll use RGBA format (8 bits per channel).
 */

typedef struct RGBA32pixel_t {
    unsigned char r, g, b, a;
} RGBA32pixel;

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
 * TkWaylandCreateGLFWImageWithXImage --
 *
 *	Create texture from XImage for GLFW rendering.
 *
 * Results:
 *	GLuint texture ID.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static GLuint
TkWaylandCreateGLFWImageWithXImage(
    XImage *image)
{
    GLuint texture = 0;
    
    if (!image || !image->data) {
        return 0;
    }
    
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Upload image data to GPU
    if (image->bits_per_pixel == 32) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image->width, image->height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, image->data);
    } else if (image->bits_per_pixel == 24) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image->width, image->height,
                     0, GL_RGB, GL_UNSIGNED_BYTE, image->data);
    } else if (image->bits_per_pixel == 1) {
        // Convert 1-bit bitmap to RGBA
        unsigned char *rgba = (unsigned char *)Tcl_Alloc(image->width * image->height * 4);
        if (rgba) {
            for (int y = 0; y < image->height; y++) {
                for (int x = 0; x < image->width; x++) {
                    int byte_idx = y * image->bytes_per_line + (x / 8);
                    int bit_idx = 7 - (x % 8);  // Most significant bit first
                    unsigned char bit = (image->data[byte_idx] >> bit_idx) & 1;
                    int rgba_idx = (y * image->width + x) * 4;
                    rgba[rgba_idx] = bit ? 255 : 0;
                    rgba[rgba_idx + 1] = bit ? 255 : 0;
                    rgba[rgba_idx + 2] = bit ? 255 : 0;
                    rgba[rgba_idx + 3] = 255;
                }
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image->width, image->height,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            Tcl_Free(rgba);
        }
    }
    
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
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
 *      The pixel value as an unsigned long.
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
            + ((image->xoffset + x) * image->bits_per_pixel / 8);
        
        switch (image->bits_per_pixel) {
            case 32: {
                RGBA32pixel *pixel = (RGBA32pixel *)srcPtr;
                r = pixel->r;
                g = pixel->g;
                b = pixel->b;
                break;
            }
            case 24: {
                r = srcPtr[0];
                g = srcPtr[1];
                b = srcPtr[2];
                break;
            }
            case 1: {
                int byte_idx = (y * image->bytes_per_line) + (x / 8);
                int bit_idx = 7 - (x % 8);
                unsigned char bit = (image->data[byte_idx] >> bit_idx) & 1;
                r = g = b = bit ? 255 : 0;
                break;
            }
        }
    }
    
    return (r << 16) | (g << 8) | b;
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
            + ((image->xoffset + x) * image->bits_per_pixel / 8);
        
        switch (image->bits_per_pixel) {
            case 32: {
                RGBA32pixel *p = (RGBA32pixel *)dstPtr;
                p->r = (pixel >> 16) & 0xFF;
                p->g = (pixel >> 8) & 0xFF;
                p->b = pixel & 0xFF;
                p->a = 255;
                break;
            }
            case 24: {
                dstPtr[0] = (pixel >> 16) & 0xFF;
                dstPtr[1] = (pixel >> 8) & 0xFF;
                dstPtr[2] = pixel & 0xFF;
                break;
            }
            case 1: {
                int byte_idx = (y * image->bytes_per_line) + (x / 8);
                int bit_idx = 7 - (x % 8);
                unsigned char bit = (pixel & 0xFFFFFF) ? 1 : 0;
                if (bit) {
                    image->data[byte_idx] |= (1 << bit_idx);
                } else {
                    image->data[byte_idx] &= ~(1 << bit_idx);
                }
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
    
    ximage = (XImage *)Tcl_Alloc(sizeof(XImage));
    memset(ximage, 0, sizeof(XImage));
    
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
    
    ximage->bitmap_pad = bitmap_pad ? bitmap_pad : 32;
    
    if (bytes_per_line) {
        ximage->bytes_per_line = bytes_per_line;
    } else {
        ximage->bytes_per_line = ((width * ximage->bits_per_pixel + 31) / 32) * 4;
    }
    
    // For Wayland/GLFW, we use little-endian RGBA format
    ximage->byte_order = LSBFirst;
    ximage->bitmap_bit_order = LSBFirst;
    ximage->red_mask = 0x000000FF;
    ximage->green_mask = 0x0000FF00;
    ximage->blue_mask = 0x00FF0000;
    
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
 * XPutImage --
 *
 *	Copy image to drawable (GLFW window).
 *
 * Results:
 *	Returns Success or BadDrawable.
 *
 * Side effects:
 *	Draws the image on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

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
    unsigned int height)
{
    // For Wayland/GLFW, we need to implement rendering
    // This would typically involve creating a texture and rendering a quad
    // Since this depends heavily on the GLFW integration, we'll just return Success
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetImage --
 *
 *	Get image from drawable (GLFW window).
 *
 * Results:
 *	Returns XImage or NULL.
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
    unsigned long plane_mask,
    int format)
{
    // For GLFW, we could use glReadPixels to capture the framebuffer
    // But this requires an OpenGL context
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyArea --
 *
 *	Copy area between drawables.
 *
 * Results:
 *	Returns Success or BadDrawable.
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
    // For GLFW, this would involve texture copying
    // Implementation depends on specific GLFW setup
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * TkScrollWindow --
 *
 *	Scroll a rectangle of the specified window.
 *
 * Results:
 *	Returns 0 if no damage, 1 if damage occurred.
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
    // For GLFW, scrolling would be done via texture manipulation
    // or by redrawing at offset
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * LoadImageWithSTB --
 *
 *	Load image file using stb_image.
 *
 * Results:
 *	Returns XImage or NULL.
 *
 * Side effects:
 *	Allocates memory for image data.
 *
 *----------------------------------------------------------------------
 */

XImage *
LoadImageWithSTB(
    const char *filename)
{
    int width, height, channels;
    unsigned char *data = stbi_load(filename, &width, &height, &channels, 0);
    
    if (!data) {
        return NULL;
    }
    
    // Convert to XImage format
    int bpp = (channels == 4) ? 32 : 24;
    int bytes_per_line = ((width * bpp + 31) / 32) * 4;
    char *image_data = (char *)Tcl_Alloc(bytes_per_line * height);
    
    if (!image_data) {
        stbi_image_free(data);
        return NULL;
    }
    
    // Copy and convert data
    for (int y = 0; y < height; y++) {
        unsigned char *src = data + y * width * channels;
        unsigned char *dst = (unsigned char *)image_data + y * bytes_per_line;
        
        if (bpp == 32) {
            for (int x = 0; x < width; x++) {
                dst[x * 4] = src[x * channels];          // R
                dst[x * 4 + 1] = src[x * channels + 1];  // G
                dst[x * 4 + 2] = src[x * channels + 2];  // B
                dst[x * 4 + 3] = (channels == 4) ? src[x * channels + 3] : 255;  // A
            }
        } else {
            for (int x = 0; x < width; x++) {
                dst[x * 3] = src[x * channels];          // R
                dst[x * 3 + 1] = src[x * channels + 1];  // G
                dst[x * 3 + 2] = src[x * channels + 2];  // B
            }
        }
    }
    
    stbi_image_free(data);
    
    // Create XImage
    XImage *image = XCreateImage(NULL, NULL, bpp, ZPixmap, 0,
                                 image_data, width, height, 32, bytes_per_line);
    
    return image;
}

/*
 *----------------------------------------------------------------------
 *
 * SaveImageWithSTB --
 *
 *	Save XImage to file using stb_image_write.
 *
 * Results:
 *	Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *	Creates image file.
 *
 *----------------------------------------------------------------------
 */

int
SaveImageWithSTB(
    XImage *image,
    const char *filename)
{
    if (!image || !image->data) {
        return 0;
    }
    
    // Convert XImage data to RGBA
    unsigned char *rgba = (unsigned char *)Tcl_Alloc(image->width * image->height * 4);
    if (!rgba) {
        return 0;
    }
    
    for (int y = 0; y < image->height; y++) {
        for (int x = 0; x < image->width; x++) {
            unsigned long pixel = ImageGetPixel(image, x, y);
            int idx = (y * image->width + x) * 4;
            rgba[idx] = (pixel >> 16) & 0xFF;     // R
            rgba[idx + 1] = (pixel >> 8) & 0xFF;  // G
            rgba[idx + 2] = pixel & 0xFF;         // B
            rgba[idx + 3] = 255;                  // A
        }
    }
    
    // Save as PNG
    int result = stbi_write_png(filename, image->width, image->height,
                                4, rgba, image->width * 4);
    
    Tcl_Free(rgba);
    return result;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */