/*
 * tkWinIco.h --
 *
 *	This file contains declarations for icon-manipulation routines
 *      in Windows.
 *
 * Copyright © 1995-1996 Microsoft Corp.
 * Copyright © 1998 Brueckner & Jarosch Ing.GmbH, Erfurt, Germany
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkWin.h"
#include <windows.h>
#include <shellapi.h>

/*
 * These structures represent the contents of a icon, in terms of its image
 * or resource.
 */

typedef struct {
    UINT Width, Height, Colors;	/* Width, Height and bpp */
    LPBYTE lpBits;		/* Ptr to DIB bits */
    DWORD dwNumBytes;		/* How many bytes? */
    LPBITMAPINFO lpbi;		/* Ptr to header */
    LPBYTE lpXOR;		/* Ptr to XOR image bits */
    LPBYTE lpAND;		/* Ptr to AND image bits */
    HICON hIcon;		/* DAS ICON */
} ICONIMAGE, *LPICONIMAGE;

typedef struct {
    BOOL         bHasChanged;                     // Has image changed?
    TCHAR        szOriginalICOFileName[MAX_PATH]; // Original name
    TCHAR        szOriginalDLLFileName[MAX_PATH]; // Original name
    int          nNumImages;                      // How many images?
    ICONIMAGE    IconImages[1];                   // Image entries
} ICONRESOURCE, *LPICONRESOURCE;

/*
 * This structure is how we represent a block of the above items. We will
 * reallocate these structures according to how many images they need to
 * contain.
 */

typedef struct {
    int nNumImages;		/* How many images? */
    ICONIMAGE IconImages[1];	/* Image entries */
} BlockOfIconImages, *BlockOfIconImagesPtr;

/*
 * These two structures are used to read in icons from an 'icon directory'
 * (i.e. the contents of a .icr file, say). We only use these structures
 * temporarily, since we copy the information we want into a
 * BlockOfIconImages.
 */

typedef struct {
    BYTE bWidth;		/* Width of the image */
    BYTE bHeight;		/* Height of the image (times 2) */
    BYTE bColorCount;		/* Number of colors in image (0 if >=8bpp) */
    BYTE bReserved;		/* Reserved */
    WORD wPlanes;		/* Color Planes */
    WORD wBitCount;		/* Bits per pixel */
    DWORD dwBytesInRes;		/* How many bytes in this resource? */
    DWORD dwImageOffset;	/* Where in the file is this image */
} ICONDIRENTRY, *LPICONDIRENTRY;

typedef struct {
    WORD idReserved;		/* Reserved */
    WORD idType;		/* Resource type (1 for icons) */
    WORD idCount;		/* How many images? */
    ICONDIRENTRY idEntries[1];	/* The entries for each image */
} ICONDIR, *LPICONDIR;

/*
 * Used in BytesPerLine
 */

#define WIDTHBYTES(bits)	((((bits) + 31)>>5)<<2)

/*
 * The following are implemented in tkWinIco.c and also used in tkWinWm.c and tkWinSysTray.c.
 */

DWORD BytesPerLine(LPBITMAPINFOHEADER lpBMIH);
LPSTR FindDIBBits(LPSTR lpbi);
HICON CreateIcoFromPhoto(int width, int height, Tk_PhotoImageBlock block);


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */



