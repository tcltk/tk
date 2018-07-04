/*
 * rbcWin.h --
 *
 *      TODO: Description
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _RBCWIN
#define _RBCWIN

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#include <windowsx.h>

/* DOS Encapsulated PostScript File Header */
#pragma pack(2)
typedef struct {
    BYTE            magic[4];   /* Magic number for a DOS EPS file
                                 * C5,D0,D3,C6 */
    DWORD           psStart;    /* Offset of PostScript section. */
    DWORD           psLength;   /* Length of the PostScript section. */
    DWORD           wmfStart;   /* Offset of Windows Meta File section. */
    DWORD           wmfLength;  /* Length of Meta file section. */
    DWORD           tiffStart;  /* Offset of TIFF section. */
    DWORD           tiffLength; /* Length of TIFF section. */
    WORD            checksum;   /* Checksum of header. If FFFF, ignore. */
} DOSEPSHEADER;
#pragma pack()

/* Aldus Portable Metafile Header */
#pragma pack(2)
typedef struct {
    DWORD           key;        /* Type of metafile */
    WORD            hmf;        /* Unused. Must be NULL. */
    SMALL_RECT      bbox;       /* Bounding rectangle */
    WORD            inch;       /* Units per inch. */
    DWORD           reserved;   /* Unused. */
    WORD            checksum;   /* XOR of previous fields (10 32-bit words). */
} APMHEADER;
#pragma pack()

MODULE_SCOPE void RbcSetROP2(
    HDC dc,
    int function);

#ifdef __GNUC__
#include <wingdi.h>
#include <windowsx.h>
#undef Status
#include <winspool.h>
#define Status int

#endif /* __GNUC__ */
/*
 * Add definitions missing from windgi.h, windowsx.h, and winspool.h
 */
#define XDrawSegments		RbcEmulateXDrawSegments

/* rbcWinDraw.c */
MODULE_SCOPE int RbcGetPlatformId(
    );
MODULE_SCOPE char *RbcLastError(
    );
MODULE_SCOPE HPALETTE RbcGetSystemPalette(
    );
MODULE_SCOPE unsigned char *RbcGetBitmapData(
    Display * display,
    Pixmap bitmap,
    int width,
    int height,
    int *pitchPtr);
MODULE_SCOPE void RbcSetROP2(
    HDC dc,
    int function);
MODULE_SCOPE GC RbcEmulateXCreateGC(
    Display *display,
    Drawable drawable,
    unsigned long mask,
    XGCValues *srcPtr);
MODULE_SCOPE HPEN RbcGCToPen(
    HDC dc,
    GC gc);
MODULE_SCOPE int RbcDrawRotatedText(
    Display * display,
    Drawable drawable,
    int x,
    int y,
    double theta,
    RbcTextStyle * stylePtr,
    RbcTextLayout * textPtr);
MODULE_SCOPE void RbcEmulateXDrawSegments(
    Display *display,
    Drawable drawable,
    GC gc,
    XSegment *segArr,
    int nSegments);

/* Already defined in rbcInt.h:
void RbcSetDashes(Display *display,
    GC gc,
    RbcDashes *dashesPtr);

*/

#endif /*_RBCWIN*/

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
