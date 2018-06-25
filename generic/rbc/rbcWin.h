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

/*
 * Add definitions missing from windgi.h, windowsx.h, and winspool.h
 */
#endif /* __GNUC__ */

/*
#define XCopyArea		RbcEmulateXCopyArea
#define XCopyPlane		RbcEmulateXCopyPlane
*/
#ifdef XDrawArcs
#undef XDrawArcs
#endif
#define XDrawArcs		RbcEmulateXDrawArcs

/*
#define XDrawLine		RbcEmulateXDrawLine
#define XDrawLines		RbcEmulateXDrawLines
*/
#ifdef XDrawPoints
#undef XDrawPoints
#endif
#define XDrawPoints		RbcEmulateXDrawPoints

/*
#define XDrawRectangle		RbcEmulateXDrawRectangle
*/
#ifdef XDrawRectangles
#undef XDrawRectangles
#endif
#define XDrawRectangles		RbcEmulateXDrawRectangles
#ifdef XDrawSegments
#undef XDrawSegments
#endif
#define XDrawSegments		RbcEmulateXDrawSegments
#ifdef XDrawString
#undef XDrawString
#endif
#define XDrawString		RbcEmulateXDrawString
#ifdef XFillArcs
#undef XFillArcs
#endif
#define XFillArcs		RbcEmulateXFillArcs

/*
#define XFillPolygon		RbcEmulateXFillPolygon
#define XFillRectangle		RbcEmulateXFillRectangle
#define XFillRectangles		RbcEmulateXFillRectangles
#define XFree			RbcEmulateXFree
#define XGetWindowAttributes	RbcEmulateXGetWindowAttributes
*/
#ifdef XLowerWindow
#undef XLowerWindow
#endif
#define XLowerWindow		RbcEmulateXLowerWindow
#ifdef XMaxRequestSize
#undef XMaxRequestSize
#endif
#define XMaxRequestSize		RbcEmulateXMaxRequestSize

/*
#define XRaiseWindow		RbcEmulateXRaiseWindow
*/
#ifdef XReparentWindow
#undef XReparentWindow
#endif
#define XReparentWindow		RbcEmulateXReparentWindow

/*
#define XSetDashes		RbcEmulateXSetDashes
#define XUnmapWindow		RbcEmulateXUnmapWindow
#define XWarpPointer		RbcEmulateXWarpPointer
*/

MODULE_SCOPE GC RbcEmulateXCreateGC(
    Display * display,
    Drawable drawable,
    unsigned long mask,
    XGCValues * valuesPtr);
MODULE_SCOPE void RbcEmulateXCopyArea(
    Display * display,
    Drawable src,
    Drawable dest,
    GC gc,
    int src_x,
    int src_y,
    unsigned int width,
    unsigned int height,
    int dest_x,
    int dest_y);
MODULE_SCOPE void RbcEmulateXCopyPlane(
    Display * display,
    Drawable src,
    Drawable dest,
    GC gc,
    int src_x,
    int src_y,
    unsigned int width,
    unsigned int height,
    int dest_x,
    int dest_y,
    unsigned long plane);
MODULE_SCOPE void RbcEmulateXDrawArcs(
    Display * display,
    Drawable drawable,
    GC gc,
    XArc * arcArr,
    int nArcs);
MODULE_SCOPE void RbcEmulateXDrawLine(
    Display * display,
    Drawable drawable,
    GC gc,
    int x1,
    int y1,
    int x2,
    int y2);
MODULE_SCOPE void RbcEmulateXDrawLines(
    Display * display,
    Drawable drawable,
    GC gc,
    XPoint * pointArr,
    int nPoints,
    int mode);
MODULE_SCOPE void RbcEmulateXDrawPoints(
    Display * display,
    Drawable drawable,
    GC gc,
    XPoint * pointArr,
    int nPoints,
    int mode);
MODULE_SCOPE void RbcEmulateXDrawRectangle(
    Display * display,
    Drawable drawable,
    GC gc,
    int x,
    int y,
    unsigned int width,
    unsigned int height);
MODULE_SCOPE void RbcEmulateXDrawRectangles(
    Display * display,
    Drawable drawable,
    GC gc,
    XRectangle * rectArr,
    int nRects);
MODULE_SCOPE void RbcEmulateXDrawSegments(
    Display * display,
    Drawable drawable,
    GC gc,
    XSegment * segArr,
    int nSegments);
MODULE_SCOPE void RbcEmulateXDrawSegments(
    Display * display,
    Drawable drawable,
    GC gc,
    XSegment * segArr,
    int nSegments);
MODULE_SCOPE void RbcEmulateXDrawString(
    Display * display,
    Drawable drawable,
    GC gc,
    int x,
    int y,
    _Xconst char *string,
    int length);
MODULE_SCOPE void RbcEmulateXFillArcs(
    Display * display,
    Drawable drawable,
    GC gc,
    XArc * arcArr,
    int nArcs);
MODULE_SCOPE void RbcEmulateXFillPolygon(
    Display * display,
    Drawable drawable,
    GC gc,
    XPoint * points,
    int nPoints,
    int shape,
    int mode);
MODULE_SCOPE void RbcEmulateXFillRectangle(
    Display * display,
    Drawable drawable,
    GC gc,
    int x,
    int y,
    unsigned int width,
    unsigned int height);
MODULE_SCOPE void RbcEmulateXFillRectangles(
    Display * display,
    Drawable drawable,
    GC gc,
    XRectangle * rectArr,
    int nRects);
MODULE_SCOPE int RbcEmulateXGetWindowAttributes(
    Display * display,
    Window window,
    XWindowAttributes * attrsPtr);
MODULE_SCOPE void RbcEmulateXMapWindow(
    Display * display,
    Window window);
MODULE_SCOPE void RbcEmulateXReparentWindow(
    Display * display,
    Window window,
    Window parent,
    int x,
    int y);
MODULE_SCOPE void RbcEmulateXSetDashes(
    Display * display,
    GC gc,
    int dashOffset,
    _Xconst char *dashList,
    int n);



MODULE_SCOPE HPEN RbcGCToPen(
    HDC dc,
    GC gc);

/* rbcWinDraw.c */
MODULE_SCOPE HPALETTE RbcGetSystemPalette(
    );
MODULE_SCOPE unsigned char *RbcGetBitmapData(
    Display * display,
    Pixmap bitmap,
    int width,
    int height,
    int *pitchPtr);
MODULE_SCOPE void RbcEmulateXFree(
    void *ptr);
MODULE_SCOPE long RbcEmulateXMaxRequestSize(
    Display * display);
MODULE_SCOPE void RbcEmulateXLowerWindow(
    Display * display,
    Window window);
MODULE_SCOPE void RbcEmulateXRaiseWindow(
    Display * display,
    Window window);
MODULE_SCOPE void RbcEmulateXUnmapWindow(
    Display * display,
    Window window);
MODULE_SCOPE void RbcEmulateXWarpPointer(
    Display * display,
    Window srcWindow,
    Window destWindow,
    int srcX,
    int srcY,
    unsigned int srcWidth,
    unsigned int srcHeight,
    int destX,
    int destY);

/* rbcWinUtil.c */
MODULE_SCOPE int RbcGetPlatformId(
    );
MODULE_SCOPE char *RbcLastError(
    );

#endif /*_RBCWIN*/

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
