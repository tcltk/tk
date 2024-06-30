/*
 * tkWinIco.c --
 *
 *	This file contains functions for icon-manipulation routines
 *      in Windows.
 *
 * Copyright © 1995-1996 Microsoft Corp.
 * Copyright © 1998 Brueckner & Jarosch Ing.GmbH, Erfurt, Germany
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkWinIco.h"

/*
 *----------------------------------------------------------------------
 *
 * DIBNumColors --
 *
 *	Calculates the number of entries in the color table, given by LPSTR
 *	lpbi - pointer to the CF_DIB memory block. Used by titlebar icon code.
 *
 * Results:
 *	WORD - Number of entries in the color table.
 *
 *----------------------------------------------------------------------
 */

static WORD
DIBNumColors(
    LPSTR lpbi)
{
    WORD wBitCount;
    DWORD dwClrUsed;

    dwClrUsed = ((LPBITMAPINFOHEADER) lpbi)->biClrUsed;

    if (dwClrUsed) {
	return (WORD) dwClrUsed;
    }

    wBitCount = ((LPBITMAPINFOHEADER) lpbi)->biBitCount;

    switch (wBitCount) {
    case 1:
	return 2;
    case 4:
	return 16;
    case 8:
	return 256;
    default:
	return 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PaletteSize --
 *
 *	Calculates the number of bytes in the color table, as given by LPSTR
 *	lpbi - pointer to the CF_DIB memory block. Used by titlebar icon code.
 *
 * Results:
 *	Number of bytes in the color table
 *
 *----------------------------------------------------------------------
 */
static WORD
PaletteSize(
    LPSTR lpbi)
{
    return (WORD) (DIBNumColors(lpbi) * sizeof(RGBQUAD));
}

/*
 *----------------------------------------------------------------------
 *
 * FindDIBits --
 *
 *	Locate the image bits in a CF_DIB format DIB, as given by LPSTR lpbi -
 *	pointer to the CF_DIB memory block. Used by titlebar icon code.
 *
 * Results:
 *	pointer to the image bits
 *
 * Side effects: None
 *
 *
 *----------------------------------------------------------------------
 */

LPSTR
FindDIBBits(
    LPSTR lpbi)
{
    return lpbi + *((LPDWORD) lpbi) + PaletteSize(lpbi);
}

/*
 *----------------------------------------------------------------------
 *
 * BytesPerLine --
 *
 *	Calculates the number of bytes in one scan line, as given by
 *	LPBITMAPINFOHEADER lpBMIH - pointer to the BITMAPINFOHEADER that
 *	begins the CF_DIB block. Used by titlebar icon code.
 *
 * Results:
 *	number of bytes in one scan line (DWORD aligned)
 *
 *----------------------------------------------------------------------
 */

DWORD
BytesPerLine(
    LPBITMAPINFOHEADER lpBMIH)
{
    return WIDTHBYTES(lpBMIH->biWidth * lpBMIH->biPlanes * lpBMIH->biBitCount);
}
/*
 *----------------------------------------------------------------------
 *
 * CreateIcoFromPhoto --
 *
 *	Create ico pointer from Tk photo block.
 *
 * Results:
 *	Icon image is created from a valid Tk photo image.
 *
 * Side effects:
 *	Icon is created.
 *
 *----------------------------------------------------------------------
 */

HICON
CreateIcoFromPhoto(
    int width,                  /* Width of image. */
    int height,                 /* Height of image. */
    Tk_PhotoImageBlock block)   /* Image block to convert. */
{
    int idx, bufferSize;
    union {unsigned char *ptr; void *voidPtr;} bgraPixel;
    union {unsigned char *ptr; void *voidPtr;} bgraMask;
    HICON hIcon;
    BITMAPINFO bmInfo;
    ICONINFO iconInfo;

    /*
     * Don't use CreateIcon to create the icon, as it requires color
     * bitmap data in device-dependent format. Instead we use
     * CreateIconIndirect which takes device-independent bitmaps and
     * converts them as required. Initialise icon info structure.
     */

    memset(&iconInfo, 0, sizeof(iconInfo));
    iconInfo.fIcon = TRUE;

    /*
     * Create device-independent color bitmap.
     */

    memset(&bmInfo, 0, sizeof bmInfo);
    bmInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmInfo.bmiHeader.biWidth = width;
    bmInfo.bmiHeader.biHeight = -height;
    bmInfo.bmiHeader.biPlanes = 1;
    bmInfo.bmiHeader.biBitCount = 32;
    bmInfo.bmiHeader.biCompression = BI_RGB;

    iconInfo.hbmColor = CreateDIBSection(NULL, &bmInfo, DIB_RGB_COLORS,
	    &bgraPixel.voidPtr, NULL, 0);
    if (!iconInfo.hbmColor) {
	return NULL;
    }

    /*
     * Convert the photo image data into BGRA format (RGBQUAD).
     */

    bufferSize = height * width * 4;
    for (idx = 0 ; idx < bufferSize ; idx += 4) {
	bgraPixel.ptr[idx] = block.pixelPtr[idx+2];
	bgraPixel.ptr[idx+1] = block.pixelPtr[idx+1];
	bgraPixel.ptr[idx+2] = block.pixelPtr[idx+0];
	bgraPixel.ptr[idx+3] = block.pixelPtr[idx+3];
    }

    /*
     * Create a dummy mask bitmap. The contents of this don't appear to
     * matter, as CreateIconIndirect will setup the icon mask based on the
     * alpha channel in our color bitmap.
     */

    bmInfo.bmiHeader.biBitCount = 1;

    iconInfo.hbmMask = CreateDIBSection(NULL, &bmInfo, DIB_RGB_COLORS,
	    &bgraMask.voidPtr, NULL, 0);
    if (!iconInfo.hbmMask) {
	DeleteObject(iconInfo.hbmColor);
	return NULL;
    }

    memset(bgraMask.ptr, 0, width*height/8);

    /*
     * Create an icon from the bitmaps.
     */

    hIcon = CreateIconIndirect(&iconInfo);
    DeleteObject(iconInfo.hbmColor);
    DeleteObject(iconInfo.hbmMask);
    if (hIcon == NULL) {
	return NULL;
    }

    return hIcon;
}
/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
