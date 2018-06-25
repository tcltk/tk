/*
 * rbcUnixImage.c --
 *
 *	This module implements image processing procedures for the rbc
 *	toolkit.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

#define RGBIndex(r,g,b) (((r)<<10) + ((r)<<6) + (r) + ((g) << 5) + (g) + (b))

/* Defined rbcColor.c TODO
extern int redAdjust, greenAdjust, blueAdjust;
extern int redMaskShift, greenMaskShift, blueMaskShift;
*/
int             redAdjust, greenAdjust, blueAdjust;
int             redMaskShift, greenMaskShift, blueMaskShift;

/*
 *----------------------------------------------------------------------
 *
 * ShiftCount --
 *
 *	Returns the position of the least significant (low) bit in
 *	the given mask.
 *
 *	For TrueColor and DirectColor visuals, a pixel value is
 *	formed by OR-ing the red, green, and blue colormap indices
 *	into a single 32-bit word.  The visual's color masks tell
 *	you where in the word the indices are supposed to be.  The
 *	masks contain bits only where the index is found.  By counting
 *	the leading zeros in the mask, we know how many bits to shift
 *	to the individual red, green, and blue values to form a pixel.
 *
 * Results:
 *      The number of the least significant bit.
 *
 *----------------------------------------------------------------------
 */
static int
ShiftCount(
    register unsigned int mask)
{
    register int    count;

    for (count = 0; count < 32; count++) {
        if (mask & 0x01) {
            break;
        }
        mask >>= 1;
    }
    return count;
}

/*
 *----------------------------------------------------------------------
 *
 * CountBits --
 *
 *	Returns the number of bits set in the given mask.
 *
 *	Reference: Graphics Gems Volume 2.
 *
 * Results:
 *      The number of bits to set in the mask.
 *
 *
 *----------------------------------------------------------------------
 */
static int
CountBits(
    register unsigned long mask)
{                               /* 32  1-bit tallies */
    /* 16  2-bit tallies */
    mask = (mask & 0x55555555) + ((mask >> 1) & (0x55555555));
    /* 8  4-bit tallies */
    mask = (mask & 0x33333333) + ((mask >> 2) & (0x33333333));
    /* 4  8-bit tallies */
    mask = (mask & 0x07070707) + ((mask >> 4) & (0x07070707));
    /* 2 16-bit tallies */
    mask = (mask & 0x000F000F) + ((mask >> 8) & (0x000F000F));
    /* 1 32-bit tally */
    mask = (mask & 0x0000001F) + ((mask >> 16) & (0x0000001F));
    return mask;
}

static void
ComputeMasks(
    Visual * visualPtr)
{
    int             count;

    redMaskShift = ShiftCount((unsigned int) visualPtr->red_mask);
    greenMaskShift = ShiftCount((unsigned int) visualPtr->green_mask);
    blueMaskShift = ShiftCount((unsigned int) visualPtr->blue_mask);

    redAdjust = greenAdjust = blueAdjust = 0;
    count = CountBits((unsigned long) visualPtr->red_mask);
    if (count < 8) {
        redAdjust = 8 - count;
    }
    count = CountBits((unsigned long) visualPtr->green_mask);
    if (count < 8) {
        greenAdjust = 8 - count;
    }
    count = CountBits((unsigned long) visualPtr->blue_mask);
    if (count < 8) {
        blueAdjust = 8 - count;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TrueColorPixel --
 *
 *      Computes a pixel index from the 3 component RGB values.
 *
 * Results:
 *      The pixel index is returned.
 *
 *----------------------------------------------------------------------
 */
static unsigned int
TrueColorPixel(
    Visual * visualPtr,
    RbcPix32 * pixelPtr)
{
    unsigned int    red, green, blue;

    /*
     * The number of bits per color may be less than eight. For example,
     * 15/16 bit displays (hi-color) use only 5 bits, 8-bit displays
     * use 2 or 3 bits (don't ask me why you'd have an 8-bit TrueColor
     * display). So shift off the least significant bits.
     */
    red = ((unsigned int) pixelPtr->rgba.red >> redAdjust);
    green = ((unsigned int) pixelPtr->rgba.green >> greenAdjust);
    blue = ((unsigned int) pixelPtr->rgba.blue >> blueAdjust);

    /* Shift each color into the proper location of the pixel index. */
    red = (red << redMaskShift) & visualPtr->red_mask;
    green = (green << greenMaskShift) & visualPtr->green_mask;
    blue = (blue << blueMaskShift) & visualPtr->blue_mask;
    return (red | green | blue);
}

/*
 *----------------------------------------------------------------------
 *
 * DirectColorPixel --
 *
 *      Translates the 3 component RGB values into a pixel index.
 *      This differs from TrueColor only in that it first translates
 *	the RGB values through a color table.
 *
 * Results:
 *      The pixel index is returned.
 *
 *----------------------------------------------------------------------
 */

/*TODO
static unsigned int
DirectColorPixel(
    struct ColorTableStruct *colorTabPtr,
    RbcPix32 *pixelPtr)
{
    unsigned int red, green, blue;

    red = colorTabPtr->red[pixelPtr->Red];
    green = colorTabPtr->green[pixelPtr->Green];
    blue = colorTabPtr->blue[pixelPtr->Blue];
    return (red | green | blue);
}
*/

/*
 *----------------------------------------------------------------------
 *
 * PseudoColorPixel --
 *
 *      Translates the 3 component RGB values into a pixel index.
 *      This differs from TrueColor only in that it first translates
 *	the RGB values through a color table.
 *
 * Results:
 *      The pixel index is returned.
 *
 *----------------------------------------------------------------------
 */
static unsigned int
PseudoColorPixel(
    RbcPix32 * pixelPtr,
    unsigned int *lut)
{
    int             red, green, blue;
    int             pixel;

    red = (pixelPtr->rgba.red >> 3) + 1;
    green = (pixelPtr->rgba.green >> 3) + 1;
    blue = (pixelPtr->rgba.blue >> 3) + 1;
    pixel = RGBIndex(red, green, blue);
    return lut[pixel];
}

static int
XGetImageErrorProc(
    ClientData clientData,
    XErrorEvent * errEventPtr)
{
    int            *errorPtr = clientData;

    *errorPtr = TCL_ERROR;
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcDrawableToColorImage --
 *
 *      Takes a snapshot of an X drawable (pixmap or window) and
 *	converts it to a color image.
 *
 *	The trick here is to efficiently convert the pixel values
 *	(indices into the color table) into RGB color values.  In the
 *	days of 8-bit displays, it was simpler to get RGB values for
 *	all 256 indices into the colormap.  Instead we'll build a
 *	hashtable of unique pixels and from that an array of pixels to
 *	pass to XQueryColors.  For TrueColor visuals, we'll simple
 *	compute the colors from the pixel.
 *
 *	[I don't know how much faster it would be to take advantage
 *	of all the different visual types.  This pretty much depends
 *	on the size of the image and the number of colors it uses.]
 *
 * Results:
 *      Returns a color image of the drawable.  If an error occurred,
 *	NULL is returned.
 *
 *----------------------------------------------------------------------
 */
RbcColorImage  *
RbcDrawableToColorImage(
    Tk_Window tkwin,
    Drawable drawable,
    register int x,             /* Offset of image from the drawable's origin. */
    register int y,             /* Offset of image from the drawable's origin. */
    int width,                  /* Dimension of the image.  Image must
                                 * be completely contained by the
                                 * drawable. */
    int height,                 /* Dimension of the image.  Image must
                                 * be completely contained by the
                                 * drawable. */
    double inputGamma)
{
    XImage         *imagePtr;
    RbcColorImage  *image;
    register RbcPix32 *destPtr;
    unsigned long   pixel;
    int             result = TCL_OK;
    Tk_ErrorHandler errHandler;
    Visual         *visualPtr;
    unsigned char   lut[256];

    errHandler =
        Tk_CreateErrorHandler(Tk_Display(tkwin), BadMatch, X_GetImage, -1,
        XGetImageErrorProc, &result);
    imagePtr =
        XGetImage(Tk_Display(tkwin), drawable, x, y, width, height, AllPlanes,
        ZPixmap);
    Tk_DeleteErrorHandler(errHandler);
    XSync(Tk_Display(tkwin), False);
    if (result != TCL_OK) {
        return NULL;
    }

    {
        register int    i;
        double          value;

        for (i = 0; i < 256; i++) {
            value = pow(i / 255.0, inputGamma) * 255.0 + 0.5;
            lut[i] = CLAMP((unsigned char) value, 0, 255);
        }
    }
    /*
     * First allocate a color image to hold the screen snapshot.
     */
    image = RbcCreateColorImage(width, height);
    visualPtr = Tk_Visual(tkwin);
    if (visualPtr->class == TrueColor) {
        unsigned int    red, green, blue;
        /*
         * Directly compute the RGB color values from the pixel index
         * rather than of going through XQueryColors.
         */
        ComputeMasks(visualPtr);
        destPtr = image->bits;
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                pixel = XGetPixel(imagePtr, x, y);

                red =
                    ((pixel & visualPtr->red_mask) >> redMaskShift) <<
                    redAdjust;
                green =
                    ((pixel & visualPtr->green_mask) >> greenMaskShift) <<
                    greenAdjust;
                blue =
                    ((pixel & visualPtr->blue_mask) >> blueMaskShift) <<
                    blueAdjust;

                /*
                 * The number of bits per color in the pixel may be
                 * less than eight. For example, 15/16 bit displays
                 * (hi-color) use only 5 bits, 8-bit displays use 2 or
                 * 3 bits (don't ask me why you'd have an 8-bit
                 * TrueColor display). So shift back the least
                 * significant bits.
                 */
                destPtr->rgba.red = lut[red];
                destPtr->rgba.green = lut[green];
                destPtr->rgba.blue = lut[blue];
                destPtr->rgba.alpha = (unsigned char) -1;
                destPtr++;
            }
        }
        XDestroyImage(imagePtr);
    } else {
        Tcl_HashEntry  *hPtr;
        Tcl_HashSearch  cursor;
        Tcl_HashTable   pixelTable;
        XColor         *colorPtr, *colorArr;
        RbcPix32       *endPtr;
        int             nPixels;
        int             nColors;
        int             isNew;

        /*
         * Fill the array with each pixel of the image. At the same time, build
         * up a hashtable of the pixels used.
         */
        nPixels = width * height;
        Tcl_InitHashTable(&pixelTable, TCL_ONE_WORD_KEYS);
        destPtr = image->bits;
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                pixel = XGetPixel(imagePtr, x, y);
                hPtr = Tcl_CreateHashEntry(&pixelTable, (char *) pixel, &isNew);
                if (isNew) {
                    Tcl_SetHashValue(hPtr, (char *) pixel);
                }
                destPtr->value = pixel;
                destPtr++;
            }
        }
        XDestroyImage(imagePtr);

        /*
         * Convert the hashtable of pixels into an array of XColors so
         * that we can call XQueryColors with it. XQueryColors will
         * convert the pixels into their RGB values.
         */
        nColors = pixelTable.numEntries;
        colorArr = (XColor *) ckalloc(sizeof(XColor) * nColors);
        assert(colorArr);

        colorPtr = colorArr;
        for (hPtr = Tcl_FirstHashEntry(&pixelTable, &cursor); hPtr != NULL;
            hPtr = Tcl_NextHashEntry(&cursor)) {
            colorPtr->pixel = (unsigned long) Tcl_GetHashValue(hPtr);
            Tcl_SetHashValue(hPtr, (char *) colorPtr);
            colorPtr++;
        }
        XQueryColors(Tk_Display(tkwin), Tk_Colormap(tkwin), colorArr, nColors);

        /*
         * Go again through the array of pixels, replacing each pixel
         * of the image with its RGB value.
         */
        destPtr = image->bits;
        endPtr = destPtr + nPixels;
        for ( /* empty */ ; destPtr < endPtr; destPtr++) {
            hPtr = Tcl_FindHashEntry(&pixelTable, (char *) destPtr->value);
            colorPtr = (XColor *) Tcl_GetHashValue(hPtr);
            destPtr->rgba.red = lut[colorPtr->red >> 8];
            destPtr->rgba.green = lut[colorPtr->green >> 8];
            destPtr->rgba.blue = lut[colorPtr->blue >> 8];
            destPtr->rgba.alpha = (unsigned char) -1;
        }
        ckfree((char *) colorArr);
        Tcl_DeleteHashTable(&pixelTable);
    }
    return image;
}

Pixmap
RbcPhotoImageMask(
    Tk_Window tkwin,
    Tk_PhotoImageBlock src)
{
    Pixmap          bitmap;
    int             arraySize, bytes_per_line;
    int             offset, count;
    int             value, bitMask;
    register int    x, y;
    unsigned char  *bits;
    unsigned char  *srcPtr;
    unsigned char  *destPtr;
    unsigned long   pixel;

    bytes_per_line = (src.width + 7) / 8;
    arraySize = src.height * bytes_per_line;
    bits = (unsigned char *) ckalloc(sizeof(unsigned char) * arraySize);
    assert(bits);
    destPtr = bits;
    offset = count = 0;
    for (y = 0; y < src.height; y++) {
        value = 0, bitMask = 1;
        srcPtr = src.pixelPtr + offset;
        for (x = 0; x < src.width; /*empty */ ) {
            pixel = (srcPtr[src.offset[3]] != 0x00);
            if (pixel) {
                value |= bitMask;
            } else {
                count++;        /* Count the number of transparent pixels. */
            }
            bitMask <<= 1;
            x++;
            if (!(x & 7)) {
                *destPtr++ = (unsigned char) value;
                value = 0, bitMask = 1;
            }
            srcPtr += src.pixelSize;
        }
        if (x & 7) {
            *destPtr++ = (unsigned char) value;
        }
        offset += src.pitch;
    }
    if (count > 0) {
        Tk_MakeWindowExist(tkwin);
        bitmap = XCreateBitmapFromData(Tk_Display(tkwin), Tk_WindowId(tkwin),
            (char *) bits, (unsigned int) src.width, (unsigned int) src.height);
    } else {
        bitmap = None;          /* Image is opaque. */
    }
    ckfree((char *) bits);
    return bitmap;
}

/*
 * -----------------------------------------------------------------
 *
 * RbcRotateBitmap --
 *
 *	Creates a new bitmap containing the rotated image of the given
 *	bitmap.  We also need a special GC of depth 1, so that we do
 *	not need to rotate more than one plane of the bitmap.
 *
 * Results:
 *	Returns a new bitmap containing the rotated image.
 *
 * -----------------------------------------------------------------
 */
Pixmap
RbcRotateBitmap(
    Tk_Window tkwin,
    Pixmap srcBitmap,           /* Source bitmap to be rotated */
    int srcWidth,               /* Width of the source bitmap */
    int srcHeight,              /* Height of the source bitmap */
    double theta,               /* Right angle rotation to perform */
    int *destWidthPtr,
    int *destHeightPtr)
{
    Display        *display;    /* X display */
    Window          root;       /* Root window drawable */
    Pixmap          destBitmap;
    int             destWidth, destHeight;
    XImage         *src, *dest;
    register int    x, y;       /* Destination bitmap coordinates */
    register int    sx, sy;     /* Source bitmap coordinates */
    unsigned long   pixel;
    GC              bitmapGC;
    double          rotWidth, rotHeight;

    display = Tk_Display(tkwin);
    root = RootWindow(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));

    /* Create a bitmap and image big enough to contain the rotated text */
    RbcGetBoundingBox(srcWidth, srcHeight, theta, &rotWidth, &rotHeight,
        (RbcPoint2D *) NULL);
    destWidth = ROUND(rotWidth);
    destHeight = ROUND(rotHeight);
    destBitmap = Tk_GetPixmap(display, root, destWidth, destHeight, 1);
    bitmapGC = RbcGetBitmapGC(tkwin);
    XSetForeground(display, bitmapGC, 0x0);
    XFillRectangle(display, destBitmap, bitmapGC, 0, 0, destWidth, destHeight);

    src = XGetImage(display, srcBitmap, 0, 0, srcWidth, srcHeight, 1, ZPixmap);
    dest = XGetImage(display, destBitmap, 0, 0, destWidth, destHeight, 1,
        ZPixmap);
    theta = FMOD(theta, 360.0);
    if (FMOD(theta, (double) 90.0) == 0.0) {
        int             quadrant;

        /* Handle right-angle rotations specifically */

        quadrant = (int) (theta / 90.0);
        switch (quadrant) {
        case RBC_ROTATE_270:   /* 270 degrees */
            for (y = 0; y < destHeight; y++) {
                sx = y;
                for (x = 0; x < destWidth; x++) {
                    sy = destWidth - x - 1;
                    pixel = XGetPixel(src, sx, sy);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;

        case RBC_ROTATE_180:   /* 180 degrees */
            for (y = 0; y < destHeight; y++) {
                sy = destHeight - y - 1;
                for (x = 0; x < destWidth; x++) {
                    sx = destWidth - x - 1, pixel = XGetPixel(src, sx, sy);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;

        case RBC_ROTATE_90:    /* 90 degrees */
            for (y = 0; y < destHeight; y++) {
                sx = destHeight - y - 1;
                for (x = 0; x < destWidth; x++) {
                    sy = x;
                    pixel = XGetPixel(src, sx, sy);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;

        case RBC_ROTATE_0:     /* 0 degrees */
            for (y = 0; y < destHeight; y++) {
                for (x = 0; x < destWidth; x++) {
                    pixel = XGetPixel(src, x, y);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;

        default:
            /* The calling routine should never let this happen. */
            break;
        }
    } else {
        double          radians, sinTheta, cosTheta;
        double          sox, soy;       /* Offset from the center of
                                         * the source rectangle. */
        double          destCX, destCY; /* Offset to the center of the destination
                                         * rectangle. */
        double          tx, ty; /* Translated coordinates from center */
        double          rx, ry; /* Angle of rotation for x and y coordinates */

        radians = (theta / 180.0) * M_PI;
        sinTheta = sin(radians), cosTheta = cos(radians);

        /*
         * Coordinates of the centers of the source and destination rectangles
         */
        sox = srcWidth * 0.5;
        soy = srcHeight * 0.5;
        destCX = destWidth * 0.5;
        destCY = destHeight * 0.5;

        /* For each pixel of the destination image, transform back to the
         * associated pixel in the source image. */

        for (y = 0; y < destHeight; y++) {
            ty = y - destCY;
            for (x = 0; x < destWidth; x++) {

                /* Translate origin to center of destination image. */
                tx = x - destCX;

                /* Rotate the coordinates about the origin. */
                rx = (tx * cosTheta) - (ty * sinTheta);
                ry = (tx * sinTheta) + (ty * cosTheta);

                /* Translate back to the center of the source image. */
                rx += sox;
                ry += soy;

                sx = ROUND(rx);
                sy = ROUND(ry);

                /*
                 * Verify the coordinates, since the destination image can be
                 * bigger than the source.
                 */

                if ((sx >= srcWidth) || (sx < 0) || (sy >= srcHeight) ||
                    (sy < 0)) {
                    continue;
                }
                pixel = XGetPixel(src, sx, sy);
                if (pixel) {
                    XPutPixel(dest, x, y, pixel);
                }
            }
        }
    }
    /* Write the rotated image into the destination bitmap. */
    XPutImage(display, destBitmap, bitmapGC, dest, 0, 0, 0, 0, destWidth,
        destHeight);

    /* Clean up the temporary resources used. */
    XDestroyImage(src), XDestroyImage(dest);
    *destWidthPtr = destWidth;
    *destHeightPtr = destHeight;
    return destBitmap;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcScaleBitmap --
 *
 *	Creates a new scaled bitmap from another bitmap. The new bitmap
 *	is bounded by a specified region. Only this portion of the bitmap
 *	is scaled from the original bitmap.
 *
 *	By bounding scaling to a region we can generate a new bitmap
 *	which is no bigger than the specified viewport.
 *
 * Results:
 *	The new scaled bitmap is returned.
 *
 * Side Effects:
 *	A new pixmap is allocated. The caller must release this.
 *
 * -----------------------------------------------------------------------
 */
Pixmap
RbcScaleBitmap(
    Tk_Window tkwin,
    Pixmap srcBitmap,
    int srcWidth,
    int srcHeight,
    int destWidth,
    int destHeight)
{
    Display        *display;
    GC              bitmapGC;
    Pixmap          destBitmap;
    Window          root;
    XImage         *src, *dest;
    double          xScale, yScale;
    register int    sx, sy;     /* Source bitmap coordinates */
    register int    x, y;       /* Destination bitmap coordinates */
    unsigned long   pixel;

    /* Create a new bitmap the size of the region and clear it */

    display = Tk_Display(tkwin);

    root = RootWindow(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
    destBitmap = Tk_GetPixmap(display, root, destWidth, destHeight, 1);
    bitmapGC = RbcGetBitmapGC(tkwin);
    XSetForeground(display, bitmapGC, 0x0);
    XFillRectangle(display, destBitmap, bitmapGC, 0, 0, destWidth, destHeight);

    src = XGetImage(display, srcBitmap, 0, 0, srcWidth, srcHeight, 1, ZPixmap);
    dest = XGetImage(display, destBitmap, 0, 0, destWidth, destHeight, 1,
        ZPixmap);

    /*
     * Scale each pixel of destination image from results of source
     * image. Verify the coordinates, since the destination image can
     * be bigger than the source
     */
    xScale = (double) srcWidth / (double) destWidth;
    yScale = (double) srcHeight / (double) destHeight;

    /* Map each pixel in the destination image back to the source. */
    for (y = 0; y < destHeight; y++) {
        sy = (int) (yScale * (double) y);
        for (x = 0; x < destWidth; x++) {
            sx = (int) (xScale * (double) x);
            pixel = XGetPixel(src, sx, sy);
            if (pixel) {
                XPutPixel(dest, x, y, pixel);
            }
        }
    }
    /* Write the scaled image into the destination bitmap */

    XPutImage(display, destBitmap, bitmapGC, dest, 0, 0, 0, 0,
        destWidth, destHeight);
    XDestroyImage(src), XDestroyImage(dest);
    return destBitmap;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcRotateScaleBitmapRegion --
 *
 *	Creates a scaled and rotated bitmap from a given bitmap.  The
 *	caller also provides (offsets and dimensions) the region of
 *	interest in the destination bitmap.  This saves having to
 *	process the entire destination bitmap is only part of it is
 *	showing in the viewport.
 *
 *	This uses a simple rotation/scaling of each pixel in the
 *	destination image.  For each pixel, the corresponding
 *	pixel in the source bitmap is used.  This means that
 *	destination coordinates are first scaled to the size of
 *	the rotated source bitmap.  These coordinates are then
 *	rotated back to their original orientation in the source.
 *
 * Results:
 *	The new rotated and scaled bitmap is returned.
 *
 * Side Effects:
 *	A new pixmap is allocated. The caller must release this.
 *
 * -----------------------------------------------------------------------
 */
Pixmap
RbcScaleRotateBitmapRegion(
    Tk_Window tkwin,
    Pixmap srcBitmap,           /* Source bitmap. */
    unsigned int srcWidth,
    unsigned int srcHeight,     /* Size of source bitmap */
    int regionX,
    int regionY,                /* Offset of region in virtual
                                 * destination bitmap. */
    unsigned int regionWidth,
    unsigned int regionHeight,  /* Desire size of bitmap region. */
    unsigned int destWidth,
    unsigned int destHeight,    /* Virtual size of destination bitmap. */
    double theta)
{                               /* Angle to rotate bitmap.  */
    Display        *display;    /* X display */
    Window          root;       /* Root window drawable */
    Pixmap          destBitmap;
    XImage         *src, *dest;
    register int    x, y;       /* Destination bitmap coordinates */
    register int    sx, sy;     /* Source bitmap coordinates */
    unsigned long   pixel;
    double          xScale, yScale;
    double          rotWidth, rotHeight;
    GC              bitmapGC;

    display = Tk_Display(tkwin);
    root = RootWindow(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));

    /* Create a bitmap and image big enough to contain the rotated text */
    bitmapGC = RbcGetBitmapGC(tkwin);
    destBitmap = Tk_GetPixmap(display, root, regionWidth, regionHeight, 1);
    XSetForeground(display, bitmapGC, 0x0);
    XFillRectangle(display, destBitmap, bitmapGC, 0, 0, regionWidth,
        regionHeight);

    src = XGetImage(display, srcBitmap, 0, 0, srcWidth, srcHeight, 1, ZPixmap);
    dest = XGetImage(display, destBitmap, 0, 0, regionWidth, regionHeight, 1,
        ZPixmap);
    theta = FMOD(theta, 360.0);

    RbcGetBoundingBox(srcWidth, srcHeight, theta, &rotWidth, &rotHeight,
        (RbcPoint2D *) NULL);
    xScale = rotWidth / (double) destWidth;
    yScale = rotHeight / (double) destHeight;

    if (FMOD(theta, (double) 90.0) == 0.0) {
        int             quadrant;

        /* Handle right-angle rotations specifically */

        quadrant = (int) (theta / 90.0);
        switch (quadrant) {
        case RBC_ROTATE_270:   /* 270 degrees */
            for (y = 0; y < regionHeight; y++) {
                sx = (int) (yScale * (double) (y + regionY));
                for (x = 0; x < regionWidth; x++) {
                    sy = (int) (xScale * (double) (destWidth - (x + regionX) -
                            1));
                    pixel = XGetPixel(src, sx, sy);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;

        case RBC_ROTATE_180:   /* 180 degrees */
            for (y = 0; y < regionHeight; y++) {
                sy = (int) (yScale * (double) (destHeight - (y + regionY) - 1));
                for (x = 0; x < regionWidth; x++) {
                    sx = (int) (xScale * (double) (destWidth - (x + regionX) -
                            1));
                    pixel = XGetPixel(src, sx, sy);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;

        case RBC_ROTATE_90:    /* 90 degrees */
            for (y = 0; y < regionHeight; y++) {
                sx = (int) (yScale * (double) (destHeight - (y + regionY) - 1));
                for (x = 0; x < regionWidth; x++) {
                    sy = (int) (xScale * (double) (x + regionX));
                    pixel = XGetPixel(src, sx, sy);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;

        case RBC_ROTATE_0:     /* 0 degrees */
            for (y = 0; y < regionHeight; y++) {
                sy = (int) (yScale * (double) (y + regionY));
                for (x = 0; x < regionWidth; x++) {
                    sx = (int) (xScale * (double) (x + regionX));
                    pixel = XGetPixel(src, sx, sy);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;

        default:
            /* The calling routine should never let this happen. */
            break;
        }
    } else {
        double          radians, sinTheta, cosTheta;
        double          sox, soy;       /* Offset from the center of the
                                         * source rectangle. */
        double          rox, roy;       /* Offset to the center of the
                                         * rotated rectangle. */
        double          tx, ty; /* Translated coordinates from center */
        double          rx, ry; /* Angle of rotation for x and y coordinates */

        radians = (theta / 180.0) * M_PI;
        sinTheta = sin(radians), cosTheta = cos(radians);

        /*
         * Coordinates of the centers of the source and destination rectangles
         */
        sox = srcWidth * 0.5;
        soy = srcHeight * 0.5;
        rox = rotWidth * 0.5;
        roy = rotHeight * 0.5;

        /* For each pixel of the destination image, transform back to the
         * associated pixel in the source image. */

        for (y = 0; y < regionHeight; y++) {
            ty = (yScale * (double) (y + regionY)) - roy;
            for (x = 0; x < regionWidth; x++) {

                /* Translate origin to center of destination image. */
                tx = (xScale * (double) (x + regionX)) - rox;

                /* Rotate the coordinates about the origin. */
                rx = (tx * cosTheta) - (ty * sinTheta);
                ry = (tx * sinTheta) + (ty * cosTheta);

                /* Translate back to the center of the source image. */
                rx += sox;
                ry += soy;

                sx = ROUND(rx);
                sy = ROUND(ry);

                /*
                 * Verify the coordinates, since the destination image can be
                 * bigger than the source.
                 */

                if ((sx >= srcWidth) || (sx < 0) || (sy >= srcHeight) ||
                    (sy < 0)) {
                    continue;
                }
                pixel = XGetPixel(src, sx, sy);
                if (pixel) {
                    XPutPixel(dest, x, y, pixel);
                }
            }
        }
    }
    /* Write the rotated image into the destination bitmap. */
    XPutImage(display, destBitmap, bitmapGC, dest, 0, 0, 0, 0, regionWidth,
        regionHeight);

    /* Clean up the temporary resources used. */
    XDestroyImage(src), XDestroyImage(dest);
    return destBitmap;

}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
