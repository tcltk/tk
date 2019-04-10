
/*
 * rbcWinImage.c --
 *
 *      This module implements image processing procedures for the rbc
 *      toolkit.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkoGraph.h"

#define GetBit(x, y) \
   srcBits[(srcBytesPerRow * (srcHeight - y - 1)) + (x>>3)] & (0x80 >> (x&7))
#define SetBit(x, y) \
   destBits[(destBytesPerRow * (destHeight - y - 1)) + (x>>3)] |= (0x80 >>(x&7))

/*
 *----------------------------------------------------------------------
 *
 * RbcDrawableToColorImage --
 *
 *      Takes a snapshot of an X drawable (pixmap or window) and
 *      converts it to a color image.
 *
 * Results:
 *      Returns a color image of the drawable.  If an error occurred,
 *      NULL is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcColorImage *
RbcDrawableToColorImage(
    Tk_Window tkwin,
    Drawable drawable,
    int x,
    int y,
    int width,
    int height,                /* Dimension of the drawable. */
    double inputGamma)
{
    void *data;
    BITMAPINFO info;
    DIBSECTION ds;
    HBITMAP hBitmap, oldBitmap;
    HPALETTE hPalette;
    HDC memDC;
    unsigned char *srcArr;
    register unsigned char *srcPtr;
    HDC hDC;
    TkWinDCState state;
    register RbcPix32 *destPtr;
    RbcColorImage *image;
    unsigned char lut[256];

    hDC = TkWinGetDrawableDC(Tk_Display(tkwin), drawable, &state);

    /* Create the intermediate drawing surface at window resolution. */
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    hBitmap = CreateDIBSection(hDC, &info, DIB_RGB_COLORS, &data, NULL, 0);
    memDC = CreateCompatibleDC(hDC);
    oldBitmap = SelectBitmap(memDC, hBitmap);

    hPalette = RbcGetSystemPalette();
    if(hPalette != NULL) {
        SelectPalette(hDC, hPalette, FALSE);
        RealizePalette(hDC);
        SelectPalette(memDC, hPalette, FALSE);
        RealizePalette(memDC);
    }
    image = NULL;
    /* Copy the window contents to the memory surface. */
    if(!BitBlt(memDC, 0, 0, width, height, hDC, x, y, SRCCOPY)) {
        goto done;
    }
    if(GetObject(hBitmap, sizeof(DIBSECTION), &ds) == 0) {
        goto done;
    }
    srcArr = (unsigned char *)ds.dsBm.bmBits;
    image = RbcCreateColorImage(width, height);
    destPtr = image->bits;

    {
    register int i;
    double value;

        for(i = 0; i < 256; i++) {
            value = pow(i / 255.0, inputGamma) * 255.0 + 0.5;
            lut[i] = CLAMP((unsigned char)value, 0, 255);
        }
    }

    /*
     * Copy the DIB RGB data into the color image. The DIB scanlines
     * are stored bottom-to-top and the order of the RGB color
     * components is BGR. Who says Win32 GDI programming isn't
     * backwards?
     */

    for(y = height - 1; y >= 0; y--) {
        srcPtr = srcArr + (y * ds.dsBm.bmWidthBytes);
        for(x = 0; x < width; x++) {
            destPtr->rgba.blue = lut[*srcPtr++];
            destPtr->rgba.green = lut[*srcPtr++];
            destPtr->rgba.red = lut[*srcPtr++];
            destPtr->rgba.alpha = (unsigned char)-1;
            destPtr++;
            srcPtr++;
        }
    }
  done:
    DeleteBitmap(SelectBitmap(memDC, oldBitmap));
    DeleteDC(memDC);
    TkWinReleaseDrawableDC(drawable, hDC, &state);
    if(hPalette != NULL) {
        DeletePalette(hPalette);
    }
    return image;
}

/*
 *--------------------------------------------------------------
 *
 * RbcPhotoImageMask --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Pixmap
RbcPhotoImageMask(
    Tk_Window tkwin,
    Tk_PhotoImageBlock src)
{
TkWinBitmap *twdPtr;
int offset, count;
register int x, y;
unsigned char *srcPtr;
int destBytesPerRow;
int destHeight;
unsigned char *destBits;

    destBytesPerRow = ((src.width + 31) & ~31) / 8;
    destBits = RbcCalloc(src.height, destBytesPerRow);
    destHeight = src.height;

    offset = count = 0;
    /* FIXME: figure out why this is so! */
    for(y = src.height - 1; y >= 0; y--) {
        srcPtr = src.pixelPtr + offset;
        for(x = 0; x < src.width; x++) {
            if(srcPtr[src.offset[3]] == 0x00) {
                SetBit(x, y);
                count++;
            }
            srcPtr += src.pixelSize;
        }
        offset += src.pitch;
    }
    if(count > 0) {
HBITMAP hBitmap;
BITMAP bm;

        bm.bmType = 0;
        bm.bmWidth = src.width;
        bm.bmHeight = src.height;
        bm.bmWidthBytes = destBytesPerRow;
        bm.bmPlanes = 1;
        bm.bmBitsPixel = 1;
        bm.bmBits = destBits;
        hBitmap = CreateBitmapIndirect(&bm);

        twdPtr = (TkWinBitmap *) ckalloc(sizeof(TkWinBitmap));
        assert(twdPtr);
        twdPtr->type = TWD_BITMAP;
        twdPtr->handle = hBitmap;
        twdPtr->depth = 1;
        if(Tk_WindowId(tkwin) == None) {
            twdPtr->colormap = DefaultColormap(Tk_Display(tkwin),
                DefaultScreen(Tk_Display(tkwin)));
        } else {
            twdPtr->colormap = Tk_Colormap(tkwin);
        }
    } else {
        twdPtr = NULL;
    }
    if(destBits != NULL) {
        ckfree((char *)destBits);
    }
    return (Pixmap) twdPtr;
}

/*
 *--------------------------------------------------------------
 *
 * RbcRotateBitmap --
 *
 *      Creates a new bitmap containing the rotated image
 *      of the given bitmap.  We also need a special GC of
 *      depth 1, so that we do not need to rotate more than
 *      one plane of the bitmap.
 *
 *      Note that under Windows, monochrome bitmaps are
 *      stored bottom-to-top.  This is why the right angle
 *      rotations 0/180 and 90/270 look reversed.
 *
 * Results:
 *      Returns a new bitmap containing the rotated image.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Pixmap
RbcRotateBitmap(
    Tk_Window tkwin,
    Pixmap srcBitmap,          /* Source bitmap to be rotated */
    int srcWidth,
    int srcHeight,             /* Width and height of the source bitmap */
    double theta,              /* Right angle rotation to perform */
    int *destWidthPtr,
    int *destHeightPtr)
{
Display *display;              /* X display */
Window root;                   /* Root window drawable */
Pixmap destBitmap;
double rotWidth, rotHeight;
HDC hDC;
TkWinDCState state;
register int x, y;             /* Destination bitmap coordinates */
register int sx, sy;           /* Source bitmap coordinates */
unsigned long pixel;
HBITMAP hBitmap;
int result;
struct MonoBitmap {
    BITMAPINFOHEADER bi;
    RGBQUAD colors[2];
} mb;
int srcBytesPerRow, destBytesPerRow;
int destWidth, destHeight;
unsigned char *srcBits, *destBits;

    display = Tk_Display(tkwin);
    root = RootWindow(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
    RbcGetBoundingBox(srcWidth, srcHeight, theta, &rotWidth, &rotHeight,
        (RbcPoint2D *) NULL);

    destWidth = (int)ceil(rotWidth);
    destHeight = (int)ceil(rotHeight);
    destBitmap = Tk_GetPixmap(display, root, destWidth, destHeight, 1);
    if(destBitmap == None) {
        return None;    /* Can't allocate pixmap. */
    }
    srcBits = RbcGetBitmapData(display, srcBitmap, srcWidth, srcHeight,
        &srcBytesPerRow);
    if(srcBits == NULL) {
        OutputDebugStringA("RbcGetBitmapData failed");
        return None;
    }
    destBytesPerRow = ((destWidth + 31) & ~31) / 8;
    destBits = RbcCalloc(destHeight, destBytesPerRow);

    theta = FMOD(theta, 360.0);
    if(FMOD(theta, (double)90.0) == 0.0) {
int quadrant;

        /* Handle right-angle rotations specially. */

        quadrant = (int)(theta / 90.0);
        switch (quadrant) {
        case RBC_ROTATE_270:   /* 270 degrees */
            for(y = 0; y < destHeight; y++) {
                sx = y;
                for(x = 0; x < destWidth; x++) {
                    sy = destWidth - x - 1;
                    pixel = GetBit(sx, sy);
                    if(pixel) {
                        SetBit(x, y);
                    }
                }
            }
            break;

        case RBC_ROTATE_180:   /* 180 degrees */
            for(y = 0; y < destHeight; y++) {
                sy = destHeight - y - 1;
                for(x = 0; x < destWidth; x++) {
                    sx = destWidth - x - 1;
                    pixel = GetBit(sx, sy);
                    if(pixel) {
                        SetBit(x, y);
                    }
                }
            }
            break;

        case RBC_ROTATE_90:    /* 90 degrees */
            for(y = 0; y < destHeight; y++) {
                sx = destHeight - y - 1;
                for(x = 0; x < destWidth; x++) {
                    sy = x;
                    pixel = GetBit(sx, sy);
                    if(pixel) {
                        SetBit(x, y);
                    }
                }
            }
            break;

        case RBC_ROTATE_0:     /* 0 degrees */
            for(y = 0; y < destHeight; y++) {
                for(x = 0; x < destWidth; x++) {
                    pixel = GetBit(x, y);
                    if(pixel) {
                        SetBit(x, y);
                    }
                }
            }
            break;

        default:
            /* The calling routine should never let this happen. */
            break;
        }
    } else {
double radians, sinTheta, cosTheta;
double srcCX, srcCY;           /* Center of source rectangle */
double destCX, destCY;         /* Center of destination rectangle */
double tx, ty;
double rx, ry;                 /* Angle of rotation for x and y coordinates */

        radians = (theta / 180.0) * M_PI;
        sinTheta = sin(radians), cosTheta = cos(radians);

        /*
         * Coordinates of the centers of the source and destination rectangles
         */
        srcCX = srcWidth * 0.5;
        srcCY = srcHeight * 0.5;
        destCX = destWidth * 0.5;
        destCY = destHeight * 0.5;

        /* Rotate each pixel of dest image, placing results in source image */

        for(y = 0; y < destHeight; y++) {
            ty = y - destCY;
            for(x = 0; x < destWidth; x++) {

                /* Translate origin to center of destination image */
                tx = x - destCX;

                /* Rotate the coordinates about the origin */
                rx = (tx * cosTheta) - (ty * sinTheta);
                ry = (tx * sinTheta) + (ty * cosTheta);

                /* Translate back to the center of the source image */
                rx += srcCX;
                ry += srcCY;

                sx = ROUND(rx);
                sy = ROUND(ry);

                /*
                 * Verify the coordinates, since the destination image can be
                 * bigger than the source
                 */

                if((sx >= srcWidth) || (sx < 0) || (sy >= srcHeight) ||
                    (sy < 0)) {
                    continue;
                }
                pixel = GetBit(sx, sy);
                if(pixel) {
                    SetBit(x, y);
                }
            }
        }
    }
    hBitmap = ((TkWinDrawable *) destBitmap)->bitmap.handle;
    ZeroMemory(&mb, sizeof(mb));
    mb.bi.biSize = sizeof(BITMAPINFOHEADER);
    mb.bi.biPlanes = 1;
    mb.bi.biBitCount = 1;
    mb.bi.biCompression = BI_RGB;
    mb.bi.biWidth = destWidth;
    mb.bi.biHeight = destHeight;
    mb.bi.biSizeImage = destBytesPerRow * destHeight;
    mb.colors[0].rgbBlue = mb.colors[0].rgbRed = mb.colors[0].rgbGreen = 0x0;
    mb.colors[1].rgbBlue = mb.colors[1].rgbRed = mb.colors[1].rgbGreen = 0xFF;
    hDC = TkWinGetDrawableDC(display, destBitmap, &state);
    result = SetDIBits(hDC, hBitmap, 0, destHeight, (LPVOID) destBits,
        (BITMAPINFO *) & mb, DIB_RGB_COLORS);
    TkWinReleaseDrawableDC(destBitmap, hDC, &state);
    if(!result) {
#if WINDEBUG
        PurifyPrintf("can't setDIBits: %s\n", RbcLastError());
#endif
        destBitmap = None;
    }
    if(destBits != NULL) {
        ckfree((char *)destBits);
    }
    if(srcBits != NULL) {
        ckfree((char *)srcBits);
    }

    *destWidthPtr = destWidth;
    *destHeightPtr = destHeight;
    return destBitmap;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcScaleBitmap --
 *
 *      Creates a new scaled bitmap from another bitmap.
 *
 * Results:
 *      The new scaled bitmap is returned.
 *
 * Side Effects:
 *      A new pixmap is allocated. The caller must release this.
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
TkWinDCState srcState, destState;
HDC src, dest;
Pixmap destBitmap;
Window root;
Display *display;

    /* Create a new bitmap the size of the region and clear it */

    display = Tk_Display(tkwin);
    root = RootWindow(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
    destBitmap = Tk_GetPixmap(display, root, destWidth, destHeight, 1);
    if(destBitmap == None) {
        return None;
    }
    src = TkWinGetDrawableDC(display, srcBitmap, &srcState);
    dest = TkWinGetDrawableDC(display, destBitmap, &destState);

    StretchBlt(dest, 0, 0, destWidth, destHeight, src, 0, 0,
        srcWidth, srcHeight, SRCCOPY);

    TkWinReleaseDrawableDC(srcBitmap, src, &srcState);
    TkWinReleaseDrawableDC(destBitmap, dest, &destState);
    return destBitmap;
}

/*
 * -----------------------------------------------------------------------
 *
 * RbcScaleRotateBitmapRegion --
 *
 *      Creates a scaled and rotated bitmap from a given bitmap.  The
 *      caller also provides (offsets and dimensions) the region of
 *      interest in the destination bitmap.  This saves having to
 *      process the entire destination bitmap is only part of it is
 *      showing in the viewport.
 *
 *      This uses a simple rotation/scaling of each pixel in the
 *      destination image.  For each pixel, the corresponding
 *      pixel in the source bitmap is used.  This means that
 *      destination coordinates are first scaled to the size of
 *      the rotated source bitmap.  These coordinates are then
 *      rotated back to their original orientation in the source.
 *
 * Results:
 *      The new rotated and scaled bitmap is returned.
 *
 * Side Effects:
 *      A new pixmap is allocated. The caller must release this.
 *
 * -----------------------------------------------------------------------
 */
Pixmap
RbcScaleRotateBitmapRegion(
    Tk_Window tkwin,
    Pixmap srcBitmap,          /* Source bitmap. */
    unsigned int srcWidth,
    unsigned int srcHeight,    /* Size of source bitmap */
    int regionX,
    int regionY,               /* Offset of region in virtual
                                * destination bitmap. */
    unsigned int regionWidth,
    unsigned int regionHeight, /* Desire size of bitmap region. */
    unsigned int virtWidth,
    unsigned int virtHeight,   /* Virtual size of destination bitmap. */
    double theta)
{              /* Angle to rotate bitmap. */
Display *display;              /* X display */
HBITMAP hBitmap;
HDC hDC;
Pixmap destBitmap;
TkWinDCState state;
Window root;                   /* Root window drawable */
double rotWidth, rotHeight;
double xScale, yScale;
int srcBytesPerRow, destBytesPerRow;
int destHeight;
int result;
register int sx, sy;           /* Source bitmap coordinates */
register int x, y;             /* Destination bitmap coordinates */
unsigned char *srcBits, *destBits;
unsigned long pixel;
struct MonoBitmap {
    BITMAPINFOHEADER bi;
    RGBQUAD colors[2];
} mb;

    display = Tk_Display(tkwin);
    root = RootWindow(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));

    /* Create a bitmap and image big enough to contain the rotated text */
    destBitmap = Tk_GetPixmap(display, root, regionWidth, regionHeight, 1);
    if(destBitmap == None) {
        return None;    /* Can't allocate pixmap. */
    }
    srcBits = RbcGetBitmapData(display, srcBitmap, srcWidth, srcHeight,
        &srcBytesPerRow);
    if(srcBits == NULL) {
        OutputDebugStringA("RbcGetBitmapData failed");
        return None;
    }
    destBytesPerRow = ((regionWidth + 31) & ~31) / 8;
    destBits = RbcCalloc(regionHeight, destBytesPerRow);
    destHeight = regionHeight;

    theta = FMOD(theta, 360.0);
    RbcGetBoundingBox(srcWidth, srcHeight, theta, &rotWidth, &rotHeight,
        (RbcPoint2D *) NULL);
    xScale = rotWidth / (double)virtWidth;
    yScale = rotHeight / (double)virtHeight;

    if(FMOD(theta, (double)90.0) == 0.0) {
int quadrant;

        /* Handle right-angle rotations specifically */

        quadrant = (int)(theta / 90.0);
        switch (quadrant) {
        case RBC_ROTATE_270:   /* 270 degrees */
            for(y = 0; y < (int)regionHeight; y++) {
                sx = (int)(yScale * (double)(y + regionY));
                for(x = 0; x < (int)regionWidth; x++) {
                    sy = (int)(xScale * (double)(virtWidth - (x + regionX) -
                            1));
                    pixel = GetBit(sx, sy);
                    if(pixel) {
                        SetBit(x, y);
                    }
                }
            }
            break;

        case RBC_ROTATE_180:   /* 180 degrees */
            for(y = 0; y < (int)regionHeight; y++) {
                sy = (int)(yScale * (double)(virtHeight - (y + regionY) - 1));
                for(x = 0; x < (int)regionWidth; x++) {
                    sx = (int)(xScale * (double)(virtWidth - (x + regionX) -
                            1));
                    pixel = GetBit(sx, sy);
                    if(pixel) {
                        SetBit(x, y);
                    }
                }
            }
            break;

        case RBC_ROTATE_90:    /* 90 degrees */
            for(y = 0; y < (int)regionHeight; y++) {
                sx = (int)(yScale * (double)(virtHeight - (y + regionY) - 1));
                for(x = 0; x < (int)regionWidth; x++) {
                    sy = (int)(xScale * (double)(x + regionX));
                    pixel = GetBit(sx, sy);
                    if(pixel) {
                        SetBit(x, y);
                    }
                }
            }
            break;

        case RBC_ROTATE_0:     /* 0 degrees */
            for(y = 0; y < (int)regionHeight; y++) {
                sy = (int)(yScale * (double)(y + regionY));
                for(x = 0; x < (int)regionWidth; x++) {
                    sx = (int)(xScale * (double)(x + regionX));
                    pixel = GetBit(sx, sy);
                    if(pixel) {
                        SetBit(x, y);
                    }
                }
            }
            break;

        default:
            /* The calling routine should never let this happen. */
            break;
        }
    } else {
double radians, sinTheta, cosTheta;
double scx, scy;               /* Offset from the center of the
                                * source rectangle. */
double rcx, rcy;               /* Offset to the center of the
                                * rotated rectangle. */
double tx, ty;                 /* Translated coordinates from center */
double rx, ry;                 /* Angle of rotation for x and y coordinates */

        radians = (theta / 180.0) * M_PI;
        sinTheta = sin(radians), cosTheta = cos(radians);

        /*
         * Coordinates of the centers of the source and destination rectangles
         */
        scx = srcWidth * 0.5;
        scy = srcHeight * 0.5;
        rcx = rotWidth * 0.5;
        rcy = rotHeight * 0.5;

        /* For each pixel of the destination image, transform back to the
         * associated pixel in the source image. */

        for(y = 0; y < (int)regionHeight; y++) {
            ty = (yScale * (double)(y + regionY)) - rcy;
            for(x = 0; x < (int)regionWidth; x++) {

                /* Translate origin to center of destination image. */
                tx = (xScale * (double)(x + regionX)) - rcx;

                /* Rotate the coordinates about the origin. */
                rx = (tx * cosTheta) - (ty * sinTheta);
                ry = (tx * sinTheta) + (ty * cosTheta);

                /* Translate back to the center of the source image. */
                rx += scx;
                ry += scy;

                sx = ROUND(rx);
                sy = ROUND(ry);

                /*
                 * Verify the coordinates, since the destination image can be
                 * bigger than the source.
                 */

                if((sx >= (int)srcWidth) || (sx < 0) ||
                    (sy >= (int)srcHeight) || (sy < 0)) {
                    continue;
                }
                pixel = GetBit(sx, sy);
                if(pixel) {
                    SetBit(x, y);
                }
            }
        }
    }
    /* Write the rotated image into the destination bitmap. */
    hBitmap = ((TkWinDrawable *) destBitmap)->bitmap.handle;
    ZeroMemory(&mb, sizeof(mb));
    mb.bi.biSize = sizeof(BITMAPINFOHEADER);
    mb.bi.biPlanes = 1;
    mb.bi.biBitCount = 1;
    mb.bi.biCompression = BI_RGB;
    mb.bi.biWidth = regionWidth;
    mb.bi.biHeight = regionHeight;
    mb.bi.biSizeImage = destBytesPerRow * regionHeight;
    mb.colors[0].rgbBlue = mb.colors[0].rgbRed = mb.colors[0].rgbGreen = 0x0;
    mb.colors[1].rgbBlue = mb.colors[1].rgbRed = mb.colors[1].rgbGreen = 0xFF;
    hDC = TkWinGetDrawableDC(display, destBitmap, &state);
    result = SetDIBits(hDC, hBitmap, 0, regionHeight, (LPVOID) destBits,
        (BITMAPINFO *) & mb, DIB_RGB_COLORS);
    TkWinReleaseDrawableDC(destBitmap, hDC, &state);
    if(!result) {
#if WINDEBUG
        PurifyPrintf("can't setDIBits: %s\n", RbcLastError());
#endif
        destBitmap = None;
    }
    if(destBits != NULL) {
        ckfree((char *)destBits);
    }
    if(srcBits != NULL) {
        ckfree((char *)srcBits);
    }
    return destBitmap;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
