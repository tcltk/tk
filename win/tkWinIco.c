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
 *----------------------------------------------------------------------
 *
 * GetFileIcon --
 *
 * Given a file path, retrieves the system-defined icon for that file.
 * Source: Mark Janssen, https://wiki.tcl-lang.org/page/Retrieve+file+icon+using+the+Win32+API 
 *
 * Results:
 *	Icon image is created from a file path. 
 *
 * Side effects:
 *	Icon is created.
 *
 *----------------------------------------------------------------------
 */

static int GetFileIcon(ClientData cdata, Tcl_Interp *interp, int objc,  Tcl_Obj * const objv[]) {
    SHFILEINFOW shfi;
    ICONINFO iconInfo ;
    BITMAP bmp;
    long imageSize ;
    char * bitBuffer , * byteBuffer ;
    int i, index;
    int result, hasAlpha;
    const char * image_name;
    Tk_PhotoHandle photo;
    Tk_PhotoImageBlock block;
    const char * file_name;
    int bitSize;
    unsigned int uFlags;

    static const char *options[] = {
	"-large", NULL};
    enum IOption {
	ILARGE
    };

    if (objc < 2) {
	Tcl_WrongNumArgs(interp,1,objv,"?options? fileName");
	return TCL_ERROR;
    }

    /* SHGFI_ICON == SHGFI_LARGEICON so large is the default, select small instead
     * then remove the flag if -large is specified.
     */

    uFlags = SHGFI_ICON | SHGFI_SMALLICON;

    for (i=1 ; i < objc-1 ; i++) {
	result = Tcl_GetIndexFromObj(interp, objv[i], options, "option", 0,
				     (int *) &index);
	if (result != TCL_OK) {
	    return result;
	}
	switch (index) {
	case ILARGE:
	    /* Setting LARGE is equivalent to unsetting SMALL. */
	    uFlags ^= SHGFI_SMALLICON;
	    break;
	default:
	    Tcl_Panic("option lookup failed");
	}
    }

    /* Normalize the filename. */
    file_name = Tcl_FSGetNativePath(objv[objc-1]);
    if (file_name == NULL) {
	return TCL_ERROR;
    }

    result = SHGetFileInfoW(
			    (LPCWSTR) file_name,
			    0,
			    &shfi,
			    sizeof(SHFILEINFO),
			    uFlags
			    );

    if (result == 0) {
	WCHAR msg[255];
	int l;
	Tcl_SetResult(interp, "failed to load icon: ",NULL);
	FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,0,GetLastError(),0,msg,255,0);

	/* Lose the newline. */
	l = 0;
	while (msg[l]!='\r' && msg[l]!='\n' && msg[l]!='\0') {
	    l++;
	}
	msg[l]='\0';
	Tcl_AppendResult(interp, msg,NULL);
	return TCL_ERROR;
    }

    GetIconInfo(shfi.hIcon, &iconInfo);

    result = GetObject(
		       iconInfo.hbmMask,
		       sizeof(BITMAP),
		       (void *)&bmp
		       );

    bitSize = bmp.bmWidth * bmp.bmHeight * bmp.bmBitsPixel / 8;

    bitBuffer = ckalloc(bitSize);
    GetBitmapBits(iconInfo.hbmMask,bitSize,bitBuffer);

    result = GetObject(
		       iconInfo.hbmColor,
		       sizeof(BITMAP),
		       (void *)&bmp
		       );

    imageSize = bmp.bmWidth * bmp.bmHeight * bmp.bmBitsPixel / 8;
    byteBuffer = ckalloc(imageSize);
    GetBitmapBits(iconInfo.hbmColor,imageSize,byteBuffer);

    /* 
     * Do some mask and Alpha channel voodoo, because not all Icons define an alpha channel
     * and MS has decided to make completely transparent the default in that case.
     */

    hasAlpha = 0;
    for (i = 0 ; i < imageSize ; i+=4) {
	if (byteBuffer[i+offsetof(RGBQUAD,rgbReserved)]!=0) {
	    hasAlpha = 1;
	    break;
	}
    }

#define BIT_SET(x,y) (((x) >> (8-(y)) ) & 1 )

    for (i=0;i<bitSize;i++) {
	if (hasAlpha) break;
	int bit = 0;
	for (bit=0; bit < 8 ; bit++) {
	    if (BIT_SET(bitBuffer[i],bit)) {
		byteBuffer[(i*8+bit)*4+3] = 0;
	    } else {
		byteBuffer[(i*8+bit)*4+3] = 255;
	    }
	}
    }

    /* Setup the Tk block structure. */
    block.pixelPtr = byteBuffer;
    block.width = bmp.bmWidth;
    block.height = bmp.bmHeight;
    block.pitch = bmp.bmWidthBytes;
    block.pixelSize = bmp.bmBitsPixel/8;
    block.offset[0]  = offsetof(RGBQUAD,rgbRed);
    block.offset[1]  = offsetof(RGBQUAD,rgbGreen);
    block.offset[2]  = offsetof(RGBQUAD,rgbBlue);
    block.offset[3] = offsetof(RGBQUAD,rgbReserved);

    /* Create the image. */
    result = Tcl_Eval(interp,"image create photo");
    if (result != TCL_OK) {
	return TCL_ERROR;
    }
    image_name = Tcl_GetStringResult(interp);
    photo = Tk_FindPhoto(interp, image_name);

    result = Tk_PhotoPutBlock( interp,photo, &block ,0,0,block.width, block.height,TK_PHOTO_COMPOSITE_SET);

    if (result != TCL_OK) {
	return TCL_ERROR;
    }

    /* Cleanup. */
    ckfree(bitBuffer);
    ckfree(byteBuffer);

    DeleteObject(iconInfo.hbmMask);
    DeleteObject(iconInfo.hbmColor);
    DestroyIcon(shfi.hIcon);

    Tcl_SetResult(interp, image_name, NULL);
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
