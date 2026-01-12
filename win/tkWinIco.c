/*
 * tkWinIco.c --
 *
 *	This file contains functions for icon-manipulation routines
 *      in Windows.
 *
 * Copyright Â© 1995-1996 Microsoft Corp.
 * Copyright Â© 1998 Brueckner & Jarosch Ing.GmbH, Erfurt, Germany
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkWinIco.h"

#ifndef SHIL_JUMBO
#   define SHIL_JUMBO 0x4
#endif

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
 *----------------------------------------------------------------------
 *
 * GetFileIcon --
 *
 * Given a file path, retrieves the system-defined icon for that file.
 * Source: Mark Janssen, https://wiki.tcl-lang.org/page/Retrieve+file+icon+using+the+Win32+API
 * Updated for Tk 9 by Paul Obermeier
 *
 * Results:
 *	Icon image is created from a file path.
 *
 * Side effects:
 *	Icon is created.
 *
 *----------------------------------------------------------------------
 */


int
GetFileIcon(
    TCL_UNUSED(void *),         /* Not used. */
    Tcl_Interp *interp,         /* Current interpreter */
    int objc,                   /* Number of arguments */
    Tcl_Obj *const objv[]       /* Argument strings */
)
{
    SHFILEINFOW shfi;
    ICONINFO iconInfo;
    BITMAP bmp;
    long imageSize;
    char *bitBuffer = NULL;
    unsigned char *byteBuffer = NULL;
    int i, hasAlpha;
    const wchar_t *iconPath;
    Tk_PhotoHandle photo;
    Tk_PhotoImageBlock block;
    int bitSize;
    int pixelSize;
    int shil;
    DWORD attrs = 0;
    DWORD flags = SHGFI_SYSICONINDEX;

    IImageList *iml = NULL;
    HICON hIcon = NULL;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "fileName pixelSize");
	return TCL_ERROR;
    }

    if (Tcl_GetIntFromObj(interp, objv[2], &pixelSize) != TCL_OK) {
		Tcl_NewStringObj("Unable to parse icon size", -1);
	return TCL_ERROR;
    }

    /* Size → SHIL mapping. */
    if (pixelSize <= 16) shil = SHIL_SMALL;
    else if (pixelSize <= 32) shil = SHIL_LARGE;
    else if (pixelSize <= 48) shil = SHIL_EXTRALARGE;
    else shil = SHIL_JUMBO;

    ZeroMemory(&shfi, sizeof(shfi));

    /* Try native filesystem path. */
    iconPath = (const wchar_t *)Tcl_FSGetNativePath(objv[1]);

    if (iconPath == NULL) {
	/* Virtual filesystem fallback. */
	flags |= SHGFI_USEFILEATTRIBUTES;
	attrs = FILE_ATTRIBUTE_DIRECTORY;
	iconPath = L"dummy";
    }

    if (!SHGetFileInfoW(
	    iconPath,
	    attrs,
	    &shfi,
	    sizeof(shfi),
	    flags))
    {
	Tcl_SetObjResult(interp,
	    Tcl_NewStringObj("Unable to retrieve system icon index", -1));
	return TCL_ERROR;
    }

    if (FAILED(SHGetImageList(shil, &IID_IImageList, (void **)&iml))) {
	Tcl_SetObjResult(interp,
	    Tcl_NewStringObj("Unable to retrieve system image list", -1));
	return TCL_ERROR;
    }

    if (FAILED(iml->lpVtbl->GetIcon(
	    iml, shfi.iIcon, ILD_TRANSPARENT, &hIcon)))
    {
	iml->lpVtbl->Release(iml);
	Tcl_SetObjResult(interp,
	    Tcl_NewStringObj("Unable to extract icon", -1));
	return TCL_ERROR;
    }

    iml->lpVtbl->Release(iml);

    /* Bitmap extraction. */

    GetIconInfo(hIcon, &iconInfo);

    GetObject(iconInfo.hbmMask, sizeof(BITMAP), &bmp);
    bitSize = bmp.bmWidth * bmp.bmHeight * bmp.bmBitsPixel / 8;
    bitBuffer = ckalloc(bitSize);
    GetBitmapBits(iconInfo.hbmMask, bitSize, bitBuffer);

    GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bmp);
    imageSize = bmp.bmWidth * bmp.bmHeight * bmp.bmBitsPixel / 8;
    byteBuffer = ckalloc(imageSize);
    GetBitmapBits(iconInfo.hbmColor, imageSize, byteBuffer);

    hasAlpha = 0;
    for (i = 0; i < imageSize; i += 4) {
	if (byteBuffer[i + 3] != 0) {
	    hasAlpha = 1;
	    break;
	}
    }

#define BIT_SET(x,y) (((x) >> (7-(y))) & 1)

    if (!hasAlpha) {
	for (i = 0; i < bitSize; i++) {
	    int bit;
	    for (bit = 0; bit < 8; bit++) {
		byteBuffer[(i*8 + bit)*4 + 3] =
		    BIT_SET(bitBuffer[i], bit) ? 0 : 255;
	    }
	}
    }

    block.pixelPtr  = byteBuffer;
    block.width     = bmp.bmWidth;
    block.height    = bmp.bmHeight;
    block.pitch     = bmp.bmWidthBytes;
    block.pixelSize = bmp.bmBitsPixel / 8;
    block.offset[0] = 2;
    block.offset[1] = 1;
    block.offset[2] = 0;
    block.offset[3] = 3;

    if (Tcl_Eval(interp, "image create photo") != TCL_OK) {
	goto cleanup;
    }

    photo = Tk_FindPhoto(interp, Tcl_GetStringResult(interp));

    Tk_PhotoPutBlock(
	interp, photo, &block,
	0, 0, block.width, block.height,
	TK_PHOTO_COMPOSITE_SET);

cleanup:
    if (bitBuffer) ckfree(bitBuffer);
    if (byteBuffer) ckfree(byteBuffer);

    DeleteObject(iconInfo.hbmMask);
    DeleteObject(iconInfo.hbmColor);
    DestroyIcon(hIcon);

    return TCL_OK;
}



/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
