/*
 * tkWinIco.h --
 *
 *	This file contains declarations for icon-manipulation routines
 *      in Windows.
 *
 * Copyright (c) 1995-1996 Microsoft Corp.
 * Copyright (c) 1998 Brueckner & Jarosch Ing.GmbH, Erfurt, Germany
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

WORD
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
 WORD
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
 * AdjustIconImagePointers --
 *
 *	Adjusts internal pointers in icon resource struct, as given by
 *	LPICONIMAGE lpImage - the resource to handle. Used by titlebar icon
 *	code.
 *
 * Results:
 *	BOOL - TRUE for success, FALSE for failure
 *
 *----------------------------------------------------------------------
 */

static BOOL
AdjustIconImagePointers(
    LPICONIMAGE lpImage)
{
    /*
     * Sanity check.
     */

    if (lpImage == NULL) {
	return FALSE;
    }

    /*
     * BITMAPINFO is at beginning of bits.
     */

    lpImage->lpbi = (LPBITMAPINFO) lpImage->lpBits;

    /*
     * Width - simple enough.
     */

    lpImage->Width = lpImage->lpbi->bmiHeader.biWidth;

    /*
     * Icons are stored in funky format where height is doubled so account for
     * that.
     */

    lpImage->Height = (lpImage->lpbi->bmiHeader.biHeight)/2;

    /*
     * How many colors?
     */

    lpImage->Colors = lpImage->lpbi->bmiHeader.biPlanes
	    * lpImage->lpbi->bmiHeader.biBitCount;

    /*
     * XOR bits follow the header and color table.
     */

    lpImage->lpXOR = (LPBYTE) FindDIBBits((LPSTR) lpImage->lpbi);

    /*
     * AND bits follow the XOR bits.
     */

    lpImage->lpAND = lpImage->lpXOR +
	    lpImage->Height*BytesPerLine((LPBITMAPINFOHEADER) lpImage->lpbi);
    return TRUE;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
