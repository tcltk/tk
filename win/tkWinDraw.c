/* 
 * tkWinDraw.c --
 *
 *	This file contains the Xlib emulation functions pertaining to
 *	actually drawing objects on a window.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 1994 Software Research Associates, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkWinDraw.c,v 1.9.2.2 2002/04/02 21:17:04 hobbs Exp $
 */

#include "tkWinInt.h"

/*
 * These macros convert between X's bizarre angle units to radians.
 */

#define PI 3.14159265358979
#define XAngleToRadians(a) ((double)(a) / 64 * PI / 180);

/*
 * Translation table between X gc functions and Win32 raster op modes.
 */

int tkpWinRopModes[] = {
    R2_BLACK,			/* GXclear */
    R2_MASKPEN,			/* GXand */
    R2_MASKPENNOT,		/* GXandReverse */
    R2_COPYPEN,			/* GXcopy */
    R2_MASKNOTPEN,		/* GXandInverted */
    R2_NOT,			/* GXnoop */
    R2_XORPEN,			/* GXxor */
    R2_MERGEPEN,		/* GXor */
    R2_NOTMERGEPEN,		/* GXnor */
    R2_NOTXORPEN,		/* GXequiv */
    R2_NOT,			/* GXinvert */
    R2_MERGEPENNOT,		/* GXorReverse */
    R2_NOTCOPYPEN,		/* GXcopyInverted */
    R2_MERGENOTPEN,		/* GXorInverted */
    R2_NOTMASKPEN,		/* GXnand */
    R2_WHITE			/* GXset */
};

/*
 * Translation table between X gc functions and Win32 BitBlt op modes.  Some
 * of the operations defined in X don't have names, so we have to construct
 * new opcodes for those functions.  This is arcane and probably not all that
 * useful, but at least it's accurate.
 */

#define NOTSRCAND	(DWORD)0x00220326 /* dest = (NOT source) AND dest */
#define NOTSRCINVERT	(DWORD)0x00990066 /* dest = (NOT source) XOR dest */
#define SRCORREVERSE	(DWORD)0x00DD0228 /* dest = source OR (NOT dest) */
#define SRCNAND		(DWORD)0x007700E6 /* dest = NOT (source AND dest) */

static int bltModes[] = {
    BLACKNESS,			/* GXclear */
    SRCAND,			/* GXand */
    SRCERASE,			/* GXandReverse */
    SRCCOPY,			/* GXcopy */
    NOTSRCAND,			/* GXandInverted */
    PATCOPY,			/* GXnoop */
    SRCINVERT,			/* GXxor */
    SRCPAINT,			/* GXor */
    NOTSRCERASE,		/* GXnor */
    NOTSRCINVERT,		/* GXequiv */
    DSTINVERT,			/* GXinvert */
    SRCORREVERSE,		/* GXorReverse */
    NOTSRCCOPY,			/* GXcopyInverted */
    MERGEPAINT,			/* GXorInverted */
    SRCNAND,			/* GXnand */
    WHITENESS			/* GXset */
};

/*
 * The following raster op uses the source bitmap as a mask for the
 * pattern.  This is used to draw in a foreground color but leave the
 * background color transparent.
 */

#define MASKPAT		0x00E20746 /* dest = (src & pat) | (!src & dst) */

/*
 * The following two raster ops are used to copy the foreground and background
 * bits of a source pattern as defined by a stipple used as the pattern.
 */

#define COPYFG		0x00CA0749 /* dest = (pat & src) | (!pat & dst) */
#define COPYBG		0x00AC0744 /* dest = (!pat & src) | (pat & dst) */

/*
 * Macros used later in the file.
 */

#define MIN(a,b)	((a>b) ? b : a)
#define MAX(a,b)	((a<b) ? b : a)

/*
 * The followng typedef is used to pass Windows GDI drawing functions.
 */
typedef BOOL (CALLBACK *WinDrawFunc) _ANSI_ARGS_((HDC dc,
			    CONST POINT* points, int npoints));
#define F_POLYGON  0
#define F_POLYLINE 1
#define POLYFUNC(functype,hdc,lp,count) \
  functype==F_POLYGON?CkPolygon(hdc,lp,count):CkPolyline(hdc,lp,count)

typedef struct ThreadSpecificData {
    POINT *winPoints;    /* Array of points that is reused. */
    int nWinPoints;	/* Current size of point array. */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * Forward declarations for procedures defined in this file:
 */

static POINT *		ConvertPoints _ANSI_ARGS_((XPoint *points, int npoints,
			    int mode, RECT *bbox));
static void		DrawOrFillArc _ANSI_ARGS_((Display *display,
			    Drawable d, GC gc, int x, int y,
			    unsigned int width, unsigned int height,
			    int start, int extent, int fill));
static void		RenderObject _ANSI_ARGS_((HDC dc, GC gc,
			    XPoint* points, int npoints, int mode, HPEN pen,
			    int func));
static HPEN		SetUpGraphicsPort _ANSI_ARGS_((GC gc));

static double mycos(double val){
  return cos(val);
}
static double mysin(double val){
  return sin(val);
}

#ifdef USE_CKGRAPH_IMP
  int tkWinHashBrushs=1;
  int tkWinHashPens=1;
#endif

#ifdef USE_CKGRAPH_IMP
HDC TkWinGetNULLDC(void){
  return CkGraph_GetHashedDC();
}
void TkWinReleaseNULLDC(HDC hdc){
  CkGraph_ReleaseHashedDC(hdc);
}
#else  /*USE_CKGRAPH_IMP*/
HDC TkWinGetNULLDC(void){
  return GetDC(NULL);
}
void TkWinReleaseNULLDC(HDC hdc){
  ReleaseDC(NULL,hdc);
}
#endif /*USE_CKGRAPH_IMP*/

HBRUSH TkWinCreateSolidBrush(GC gc,COLORREF color){
#ifdef USE_CKGRAPH_IMP
    if ( tkWinHashBrushs==0 ) 
#endif
      return CkCreateSolidBrush(color);
#ifdef USE_CKGRAPH_IMP
    if(color==gc->foreground){
      return (HBRUSH)((gc->fgBrush==None)?
                gc->fgBrush=(unsigned int)CkCreateSolidBrush(gc->foreground):
                gc->fgBrush);
    } else if(color==gc->background) {
      return (HBRUSH)((gc->bgBrush==None)?
                gc->bgBrush=(unsigned int)CkCreateSolidBrush(gc->background):
                gc->bgBrush);
    } else {
      return CkCreateSolidBrush(color);
    }
#endif
}
BOOL TkWinDeleteBrush(GC gc,HBRUSH hBrush){
#ifdef USE_CKGRAPH_IMP
    if ( tkWinHashBrushs==0 ) 
#endif
      return CkDeleteBrush(hBrush);
#ifdef USE_CKGRAPH_IMP
/*
 * let the brushs allocated until GC is destroyed
 * except this is a temporary allocated brush
 */
    if(hBrush!=(HBRUSH)gc->bgBrush && hBrush!=(HBRUSH)gc->fgBrush){
      return CkDeleteBrush(hBrush);
    }
    return TRUE;
#endif
}
static HPEN TkWinExtCreatePen(GC gc,DWORD style,DWORD width,CONST LOGBRUSH* lb,
                     DWORD count,CONST DWORD* lp){
#ifdef USE_CKGRAPH_IMP
    if ( tkWinHashPens==0 ) 
#endif
      return CkExtCreatePen(style, width, lb, count, lp);
#ifdef USE_CKGRAPH_IMP
    if(gc->fgExtPen==None){
       goto done;
    } else if (style!=gc->extpenstyle){
       CkDeletePen((HPEN)gc->fgExtPen);
       goto done;
    } else {
       return (HPEN)gc->fgExtPen;
    }
done:
    gc->extpenstyle=style;
    return (HPEN) (gc->fgExtPen=(unsigned int)CkExtCreatePen(style, width, lb, count, lp));
#endif
}
static HPEN TkWinCreatePen(GC gc,int style,int width,COLORREF color){
#ifdef USE_CKGRAPH_IMP
    if ( tkWinHashPens==0 ) 
#endif
      return CkCreatePen(style,width,color);
#ifdef USE_CKGRAPH_IMP
    return (HPEN)((gc->fgPen==None)?
                gc->fgPen=(unsigned int)CkCreatePen(style,width,color):
                gc->fgPen);
#endif
}
static BOOL TkWinDeletePen(GC gc,HPEN hPen){
#ifdef USE_CKGRAPH_IMP
    if ( tkWinHashPens==0 ) 
#endif
      return CkDeletePen(hPen);
#ifdef USE_CKGRAPH_IMP
/*
 * let the pens allocated until GC is destroyed
 * except this is a temporary allocated brush
 */
    return TRUE;
#endif
}
/*
 *----------------------------------------------------------------------
 *
 * TkWinGetDrawableDC --
 *
 *	Retrieve the DC from a drawable.
 *
 * Results:
 *	Returns the window DC for windows.  Returns a new memory DC
 *	for pixmaps.
 *
 * Side effects:
 *	Sets up the palette for the device context, and saves the old
 *	device context state in the passed in TkWinDCState structure.
 *
 *----------------------------------------------------------------------
 */

HDC
TkWinGetDrawableDC(display, d, state)
    Display *display;
    Drawable d;
    TkWinDCState* state;
{
    HDC dc;
    TkWinDrawable *twdPtr = (TkWinDrawable *)d;
    Colormap cmap;

    GTRACE(("begin TkWinGetDrawableDC\n");)
    if (twdPtr->type == TWD_WINDOW) {
	TkWindow *winPtr = twdPtr->window.winPtr;
    
 	dc = CkGetDC(twdPtr->window.handle);
	if (winPtr == NULL) {
	    cmap = DefaultColormap(display, DefaultScreen(display));
	} else {
	    cmap = winPtr->atts.colormap;
	}
    } else if (twdPtr->type == TWD_WINDC) {
	dc = twdPtr->winDC.hdc;
	cmap = DefaultColormap(display, DefaultScreen(display));
    } else {
#ifdef USE_CKGRAPH_IMP
	dc = CkGraph_GetHashedDC();
#else
	dc = CkCreateCompatibleDC(NULL);
#endif
#ifdef USE_CKGRAPH_IMP
#ifdef CKGRAPH_DEBUG
        CkGraph_CheckSelectedBitmap(dc, twdPtr->bitmap.handle);
#endif
#endif
	CkSelectBitmap(dc, twdPtr->bitmap.handle);
	cmap = twdPtr->bitmap.colormap;
    }
    state->palette = TkWinSelectPalette(dc, cmap);
    state->bkmode  = GetBkMode(dc);
    GTRACE(("end TkWinGetDrawableDC\n");)
    return dc;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinReleaseDrawableDC --
 *
 *	Frees the resources associated with a drawable's DC.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Restores the old bitmap handle to the memory DC for pixmaps.
 *
 *----------------------------------------------------------------------
 */

void
TkWinReleaseDrawableDC(d, dc, state)
    Drawable d;
    HDC dc;
    TkWinDCState *state;
{
    TkWinDrawable *twdPtr = (TkWinDrawable *)d;
    GTRACE(("begin TkWinReleaseDrawableDC\n");)
    CkSetBkMode(dc, state->bkmode);
#ifndef USE_CKGRAPH_IMP
    CkSelectPalette(dc, state->palette, TRUE);
    CkRealizePalette(dc);
#endif
    if (twdPtr->type == TWD_WINDOW) {
	CkReleaseDC(TkWinGetHWND(d), dc);
    } else if (twdPtr->type == TWD_BITMAP) {
#ifdef USE_CKGRAPH_IMP
        CkGraph_ReleaseHashedDC(dc);
#else
	CkDeleteDC(dc);
#endif
    }
    GTRACE(("end TkWinReleaseDrawableDC\n");)
}

/*
 *----------------------------------------------------------------------
 *
 * ConvertPoints --
 *
 *	Convert an array of X points to an array of Win32 points.
 *
 * Results:
 *	Returns the converted array of POINTs.
 *
 * Side effects:
 *	Allocates a block of memory in thread local storage that 
 *      should not be freed.
 *
 *----------------------------------------------------------------------
 */

static POINT *
ConvertPoints(points, npoints, mode, bbox)
    XPoint *points;
    int npoints;
    int mode;			/* CoordModeOrigin or CoordModePrevious. */
    RECT *bbox;			/* Bounding box of points. */
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *) 
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    int i;

    /*
     * To avoid paying the cost of a malloc on every drawing routine,
     * we reuse the last array if it is large enough.
     */

    if (npoints > tsdPtr->nWinPoints) {
	if (tsdPtr->winPoints != NULL) {
	    ckfree((char *) tsdPtr->winPoints);
	}
	tsdPtr->winPoints = (POINT *) ckalloc(sizeof(POINT) * npoints);
	if (tsdPtr->winPoints == NULL) {
	    tsdPtr->nWinPoints = -1;
	    return NULL;
	}
	tsdPtr->nWinPoints = npoints;
    }

    bbox->left = bbox->right = points[0].x;
    bbox->top = bbox->bottom = points[0].y;
    
    if (mode == CoordModeOrigin) {
	for (i = 0; i < npoints; i++) {
	    tsdPtr->winPoints[i].x = points[i].x;
	    tsdPtr->winPoints[i].y = points[i].y;
	    bbox->left = MIN(bbox->left, tsdPtr->winPoints[i].x);
	    bbox->right = MAX(bbox->right, tsdPtr->winPoints[i].x);
	    bbox->top = MIN(bbox->top, tsdPtr->winPoints[i].y);
	    bbox->bottom = MAX(bbox->bottom, tsdPtr->winPoints[i].y);
	}
    } else {
	tsdPtr->winPoints[0].x = points[0].x;
	tsdPtr->winPoints[0].y = points[0].y;
	for (i = 1; i < npoints; i++) {
	    tsdPtr->winPoints[i].x = tsdPtr->winPoints[i-1].x + points[i].x;
	    tsdPtr->winPoints[i].y = tsdPtr->winPoints[i-1].y + points[i].y;
	    bbox->left = MIN(bbox->left, tsdPtr->winPoints[i].x);
	    bbox->right = MAX(bbox->right, tsdPtr->winPoints[i].x);
	    bbox->top = MIN(bbox->top, tsdPtr->winPoints[i].y);
	    bbox->bottom = MAX(bbox->bottom, tsdPtr->winPoints[i].y);
	}
    }
    return tsdPtr->winPoints;
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyArea --
 *
 *	Copies data from one drawable to another using block transfer
 *	routines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Data is moved from a window or bitmap to a second window or
 *	bitmap.
 *
 *----------------------------------------------------------------------
 */

void
XCopyArea(display, src, dest, gc, src_x, src_y, width, height, dest_x, dest_y)
    Display* display;
    Drawable src;
    Drawable dest;
    GC gc;
    int src_x, src_y;
    unsigned int width, height;
    int dest_x, dest_y;
{
    HDC srcDC, destDC;
    TkWinDCState srcState, destState;
    TkpClipMask *clipPtr = (TkpClipMask*)gc->clip_mask;
    GTRACE(("begin XCopyArea\n");)

    srcDC = TkWinGetDrawableDC(display, src, &srcState);

    if (src != dest) {
	destDC = TkWinGetDrawableDC(display, dest, &destState);
    } else {
	destDC = srcDC;
    }

    if (clipPtr && clipPtr->type == TKP_CLIP_REGION) {
	CkSelectClipRgn(destDC, (HRGN) clipPtr->value.region);
	CkOffsetClipRgn(destDC, gc->clip_x_origin, gc->clip_y_origin);
    }

    CkBitBlt(destDC, dest_x, dest_y, width, height, srcDC, src_x, src_y,
	    bltModes[gc->function]);

    CkSelectClipRgn(destDC, NULL);

    if (src != dest) {
	TkWinReleaseDrawableDC(dest, destDC, &destState);
    }
    TkWinReleaseDrawableDC(src, srcDC, &srcState);
    GTRACE(("end XCopyArea\n");)
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyPlane --
 *
 *	Copies a bitmap from a source drawable to a destination
 *	drawable.  The plane argument specifies which bit plane of
 *	the source contains the bitmap.  Note that this implementation
 *	ignores the gc->function.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the destination drawable.
 *
 *----------------------------------------------------------------------
 */

void
XCopyPlane(display, src, dest, gc, src_x, src_y, width, height, dest_x,
	dest_y, plane)
    Display* display;
    Drawable src;
    Drawable dest;
    GC gc;
    int src_x, src_y;
    unsigned int width, height;
    int dest_x, dest_y;
    unsigned long plane;
{
    HDC srcDC, destDC;
    TkWinDCState srcState, destState;
    HBRUSH bgBrush, fgBrush; 
#ifndef USE_CKGRAPH_IMP
    HBRUSH oldBrush;
#endif
    TkpClipMask *clipPtr = (TkpClipMask*)gc->clip_mask;
    GTRACE(("begin XCopyPlane\n");)

    display->request++;

    if (plane != 1) {
	panic("Unexpected plane specified for XCopyPlane");
    }

    srcDC = TkWinGetDrawableDC(display, src, &srcState);

    if (src != dest) {
	destDC = TkWinGetDrawableDC(display, dest, &destState);
    } else {
	destDC = srcDC;
    }

    if (clipPtr == NULL || clipPtr->type == TKP_CLIP_REGION) {

	/*
	 * Case 1: opaque bitmaps.  Windows handles the conversion
	 * from one bit to multiple bits by setting 0 to the
	 * foreground color, and 1 to the background color (seems
	 * backwards, but there you are).
	 */

	if (clipPtr && clipPtr->type == TKP_CLIP_REGION) {
            //this sometimes fail in BLT-Graph, dunno why
	    CkSelectClipRgn(destDC, (HRGN) clipPtr->value.region);
	    CkOffsetClipRgn(destDC, gc->clip_x_origin, gc->clip_y_origin);
	}

	CkSetBkMode(destDC, OPAQUE);
	CkSetBkColor(destDC, gc->foreground);
	CkSetTextColor(destDC, gc->background);
	CkBitBlt(destDC, dest_x, dest_y, width, height, srcDC, src_x, src_y,
		SRCCOPY);

	CkSelectClipRgn(destDC, NULL);
    } else if (clipPtr->type == TKP_CLIP_PIXMAP) {
	if (clipPtr->value.pixmap == src) {

	    /*
	     * Case 2: transparent bitmaps are handled by setting the
	     * destination to the foreground color whenever the source
	     * pixel is set.
	     */
	    fgBrush = TkWinCreateSolidBrush(gc,gc->foreground);

#ifdef USE_CKGRAPH_IMP
	    //Oops,the Tcl/Tk appeared brown in the bitmap ...
	    //so reset the DC correctly
	    CkGraph_ClearDC(destDC);
	    CkGraph_ClearDC(srcDC);
	    CkSelectBrush(destDC, fgBrush);
#else
	    oldBrush = CkSelectBrush(destDC, fgBrush);
#endif
	    CkBitBlt(destDC, dest_x, dest_y, width, height, srcDC, src_x, src_y,
		    MASKPAT);
#ifndef USE_CKGRAPH_IMP
	    CkSelectBrush(destDC, oldBrush);
#endif
	    TkWinDeleteBrush(gc,fgBrush);
	} else {

	    /*
	     * Case 3: two arbitrary bitmaps.  Copy the source rectangle
	     * into a color pixmap.  Use the result as a brush when
	     * copying the clip mask into the destination.	 
	     */

	    HDC memDC, maskDC;
	    HBITMAP bitmap;
	    TkWinDCState maskState;

	    fgBrush = TkWinCreateSolidBrush(gc,gc->foreground);
	    bgBrush = TkWinCreateSolidBrush(gc,gc->background);
	    maskDC = TkWinGetDrawableDC(display, clipPtr->value.pixmap,
		    &maskState);
	    memDC = CkCreateCompatibleDC(destDC);
	    bitmap = CkCreateBitmap(width, height, 1, 1, NULL);
	    CkSelectBitmap(memDC, bitmap);

	    /*
	     * Set foreground bits.  We create a new bitmap containing
	     * (source AND mask), then use it to set the foreground color
	     * into the destination.
	     */

	    CkBitBlt(memDC, 0, 0, width, height, srcDC, src_x, src_y, SRCCOPY);
	    CkBitBlt(memDC, 0, 0, width, height, maskDC,
		    dest_x - gc->clip_x_origin, dest_y - gc->clip_y_origin,
		    SRCAND);
#ifdef USE_CKGRAPH_IMP
	    CkSelectBrush(destDC, fgBrush);
#else
	    oldBrush = CkSelectBrush(destDC, fgBrush);
#endif
	    CkBitBlt(destDC, dest_x, dest_y, width, height, memDC, 0, 0,
		    MASKPAT);

	    /*
	     * Set background bits.  Same as foreground, except we use
	     * ((NOT source) AND mask) and the background brush.
	     */

	    CkBitBlt(memDC, 0, 0, width, height, srcDC, src_x, src_y,
		    NOTSRCCOPY);
	    CkBitBlt(memDC, 0, 0, width, height, maskDC,
		    dest_x - gc->clip_x_origin, dest_y - gc->clip_y_origin,
		    SRCAND);
	    CkSelectBrush(destDC, bgBrush);
	    CkBitBlt(destDC, dest_x, dest_y, width, height, memDC, 0, 0,
		    MASKPAT);

	    TkWinReleaseDrawableDC(clipPtr->value.pixmap, maskDC, &maskState);
#ifndef USE_CKGRAPH_IMP
	    CkSelectBrush(destDC, oldBrush);
#endif
	    CkDeleteDC(memDC);
	    CkDeleteBitmap(bitmap);
	    TkWinDeleteBrush(gc,fgBrush);
	    TkWinDeleteBrush(gc,bgBrush);
	}
    }
    if (src != dest) {
	TkWinReleaseDrawableDC(dest, destDC, &destState);
    }
    TkWinReleaseDrawableDC(src, srcDC, &srcState);
    GTRACE(("end XCopyPlane\n");)
}

/*
 *----------------------------------------------------------------------
 *
 * TkPutImage --
 *
 *	Copies a subimage from an in-memory image to a rectangle of
 *	of the specified drawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws the image on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
TkPutImage(colors, ncolors, display, d, gc, image, src_x, src_y, dest_x,
	dest_y, width, height)
    unsigned long *colors;		/* Array of pixel values used by this
					 * image.  May be NULL. */
    int ncolors;			/* Number of colors used, or 0. */
    Display* display;
    Drawable d;				/* Destination drawable. */
    GC gc;
    XImage* image;			/* Source image. */
    int src_x, src_y;			/* Offset of subimage. */      
    int dest_x, dest_y;			/* Position of subimage origin in
					 * drawable.  */
    unsigned int width, height;		/* Dimensions of subimage. */
{
    HDC dc, dcMem;
    TkWinDCState state;
    BITMAPINFO *infoPtr;
    HBITMAP bitmap;
    char *data;

    display->request++;
    GTRACE(("begin TkPutImage\n");)

    dc = TkWinGetDrawableDC(display, d, &state);
    CkSetROP2(dc, tkpWinRopModes[gc->function]);
    dcMem = CkCreateCompatibleDC(dc);

    if (image->bits_per_pixel == 1) {
	/*
	 * If the image isn't in the right format, we have to copy
	 * it into a new buffer in MSBFirst and word-aligned format.
	 */

	if ((image->bitmap_bit_order != MSBFirst)
		|| (image->bitmap_pad != sizeof(WORD))) {
	    data = TkAlignImageData(image, sizeof(WORD), MSBFirst);
	    bitmap = CkCreateBitmap(image->width, image->height, 1, 1, data);
	    ckfree(data);
	} else {
	    bitmap = CkCreateBitmap(image->width, image->height, 1, 1,
		    image->data);
	}
	CkSetTextColor(dc, gc->foreground);
	CkSetBkColor(dc, gc->background);
    } else {    
	int i, usePalette;

	/*
	 * Do not use a palette for TrueColor images.
	 */
	
	usePalette = (image->bits_per_pixel < 16);
	
	if (usePalette) {
	    infoPtr = (BITMAPINFO*) ckalloc(sizeof(BITMAPINFOHEADER)
		    + sizeof(RGBQUAD)*ncolors);
	} else {
	    infoPtr = (BITMAPINFO*) ckalloc(sizeof(BITMAPINFOHEADER));
	}
	
	infoPtr->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	infoPtr->bmiHeader.biWidth = image->width;
	infoPtr->bmiHeader.biHeight = -image->height; /* Top-down order */
	infoPtr->bmiHeader.biPlanes = 1;
	infoPtr->bmiHeader.biBitCount = image->bits_per_pixel;
	infoPtr->bmiHeader.biCompression = BI_RGB;
	infoPtr->bmiHeader.biSizeImage = 0;
	infoPtr->bmiHeader.biXPelsPerMeter = 0;
	infoPtr->bmiHeader.biYPelsPerMeter = 0;
	infoPtr->bmiHeader.biClrImportant = 0;

	if (usePalette) {
	    infoPtr->bmiHeader.biClrUsed = ncolors;
	    for (i = 0; i < ncolors; i++) {
		infoPtr->bmiColors[i].rgbBlue = GetBValue(colors[i]);
		infoPtr->bmiColors[i].rgbGreen = GetGValue(colors[i]);
		infoPtr->bmiColors[i].rgbRed = GetRValue(colors[i]);
		infoPtr->bmiColors[i].rgbReserved = 0;
	    }
	} else {
	    infoPtr->bmiHeader.biClrUsed = 0;
	}
	bitmap = CkCreateDIBitmap(dc, &infoPtr->bmiHeader, CBM_INIT,
		image->data, infoPtr, DIB_RGB_COLORS);
	ckfree((char *) infoPtr);
    }
#if 1 /* Tk Win Speedup */

#ifdef USE_CKGRAPH_IMP
    CkSelectBitmap(dcMem, bitmap);
#else
    bitmap = CkSelectBitmap(dcMem, bitmap);
#endif
    CkBitBlt(dc, dest_x, dest_y, width, height, dcMem, src_x, src_y, SRCCOPY);
    CkDeleteDC(dcMem);
#ifdef USE_CKGRAPH_IMP
    CkDeleteBitmap(bitmap);
#else
    CkDeleteBitmap(CkSelectBitmap(dcMem, bitmap));
#endif

#else
    if(!bitmap) {
	panic("Fail to allocate bitmap\n");
	DeleteDC(dcMem);
    	TkWinReleaseDrawableDC(d, dc, &state);
	return;
    }
    bitmap = SelectObject(dcMem, bitmap);
    BitBlt(dc, dest_x, dest_y, width, height, dcMem, src_x, src_y, SRCCOPY);
    DeleteObject(SelectObject(dcMem, bitmap));
    DeleteDC(dcMem);
#endif
    TkWinReleaseDrawableDC(d, dc, &state);
    GTRACE(("end TkPutImage\n");)
}

/*
 *----------------------------------------------------------------------
 *
 * XFillRectangles --
 *
 *	Fill multiple rectangular areas in the given drawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws onto the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XFillRectangles(display, d, gc, rectangles, nrectangles)
    Display* display;
    Drawable d;
    GC gc;
    XRectangle* rectangles;
    int nrectangles;
{
    HDC dc;
    int i;
    RECT rect;
    TkWinDCState state;

    if (d == None) {
	return;
    }
    GTRACE(("begin XFillRectangles(..%d)\n",nrectangles);)
    dc = TkWinGetDrawableDC(display, d, &state);
    CkSetROP2(dc, tkpWinRopModes[gc->function]);

    if ((gc->fill_style == FillStippled
	    || gc->fill_style == FillOpaqueStippled)
	    && gc->stipple != None) {
	TkWinDrawable *twdPtr = (TkWinDrawable *)gc->stipple;
        POINT brushOrg;
#ifndef USE_CKGRAPH_IMP
	HBRUSH oldBrush;
	HBITMAP oldBitmap;
#endif
	HBRUSH stipple;
        HBITMAP bitmap;
	HDC dcMem;
        HBRUSH brush = TkWinCreateSolidBrush(gc,gc->foreground);
	HBRUSH bgBrush = TkWinCreateSolidBrush(gc,gc->background);

	if (twdPtr->type != TWD_BITMAP) {
	    panic("unexpected drawable type in stipple");
	}

	/*
	 * Select stipple pattern into destination dc.
	 */
	stipple = CkCreatePatternBrush(twdPtr->bitmap.handle);
	SetBrushOrgEx(dc, gc->ts_x_origin, gc->ts_y_origin, &brushOrg);
#ifdef USE_CKGRAPH_IMP
	CkGraph_ClearDC(dc);
	CkSelectBrush(dc, stipple);
#else
	oldBrush = CkSelectBrush(dc, stipple);
#endif
	dcMem = CkCreateCompatibleDC(dc);

	/*
	 * For each rectangle, create a drawing surface which is the size of
	 * the rectangle and fill it with the background color.  Then merge the
	 * result with the stipple pattern.
	 */

	for (i = 0; i < nrectangles; i++) {
	    bitmap = CkCreateCompatibleBitmap(dc, rectangles[i].width,
		    rectangles[i].height);
#ifdef USE_CKGRAPH_IMP
	    CkSelectBitmap(dcMem, bitmap);
#else
	    oldBitmap = CkSelectBitmap(dcMem, bitmap);
#endif
	    rect.left = 0;
	    rect.top = 0;
	    rect.right = rectangles[i].width;
	    rect.bottom = rectangles[i].height;
	    CkFillRect(dcMem, &rect, brush);
	    CkBitBlt(dc, rectangles[i].x, rectangles[i].y, rectangles[i].width,
		    rectangles[i].height, dcMem, 0, 0, COPYFG);
	    if (gc->fill_style == FillOpaqueStippled) {
		CkFillRect(dcMem, &rect, bgBrush);
		CkBitBlt(dc, rectangles[i].x, rectangles[i].y,
			rectangles[i].width, rectangles[i].height, dcMem,
			0, 0, COPYBG);
	    }
#ifndef USE_CKGRAPH_IMP
	    CkSelectBitmap(dcMem, oldBitmap);
#endif
	    CkDeleteBitmap(bitmap);
	}
	
	CkDeleteDC(dcMem);
#ifndef USE_CKGRAPH_IMP
	CkSelectBrush(dc, oldBrush);
#endif
	CkDeleteBrush(stipple);
	TkWinDeleteBrush(gc,bgBrush);
        TkWinDeleteBrush(gc,brush);
	SetBrushOrgEx(dc, brushOrg.x, brushOrg.y, NULL);
    } else {
	for (i = 0; i < nrectangles; i++) {
#ifdef FILLRECTGC
	    TkWinFillRectGC(dc, rectangles[i].x, rectangles[i].y,
		    rectangles[i].width, rectangles[i].height, gc->foreground,gc);
#else
	    TkWinFillRect(dc, rectangles[i].x, rectangles[i].y,
		    rectangles[i].width, rectangles[i].height, gc->foreground);
#endif
	}
    }
    TkWinReleaseDrawableDC(d, dc, &state);
    GTRACE(("end XFillRectangles(..%d)\n",nrectangles);)
}

/*
 *----------------------------------------------------------------------
 *
 * RenderObject --
 *
 *	This function draws a shape using a list of points, a
 *	stipple pattern, and the specified drawing function type.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
RenderObject(dc, gc, points, npoints, mode, pen, functype)
    HDC dc;
    GC gc;
    XPoint* points;
    int npoints;
    int mode;
    HPEN pen;
    int functype;
{
    RECT rect;
#ifndef USE_CKGRAPH_IMP
    HPEN oldPen;
    HBRUSH oldBrush;
#endif
    POINT *winPoints = ConvertPoints(points, npoints, mode, &rect);
    GTRACE(("begin RenderObject\n");)
    
    if ((gc->fill_style == FillStippled
	    || gc->fill_style == FillOpaqueStippled)
	    && gc->stipple != None) {

	TkWinDrawable *twdPtr = (TkWinDrawable *)gc->stipple;
	HDC dcMem;
	LONG width, height;
	int i;
	POINT brushOrg;
#ifdef USE_CKGRAPH_IMP
	HBRUSH fgBrush;
	HBRUSH bgBrush=(HBRUSH)NULL;
	HBRUSH patBrush;
	HBITMAP bitmap;
	CkGraph_ClearDC(dc);
#else
	HBITMAP oldBitmap;
	HBRUSH oldMemBrush;
#endif
	
	if (twdPtr->type != TWD_BITMAP) {
	    panic("unexpected drawable type in stipple");
	}

	/*
	 * Grow the bounding box enough to account for line width.
	 */

	rect.left -= gc->line_width;
	rect.top -= gc->line_width;
	rect.right += gc->line_width;
	rect.bottom += gc->line_width;

	width = rect.right - rect.left;
	height = rect.bottom - rect.top;

	/*
	 * Select stipple pattern into destination dc.
	 */
	
	SetBrushOrgEx(dc, gc->ts_x_origin, gc->ts_y_origin, &brushOrg);
#ifdef USE_CKGRAPH_IMP
	CkSelectBrush(dc,patBrush=CkCreatePatternBrush(twdPtr->bitmap.handle));
#else
	oldBrush = CkSelectBrush(dc, CkCreatePatternBrush(twdPtr->bitmap.handle));
#endif

	/*
	 * Create temporary drawing surface containing a copy of the
	 * destination equal in size to the bounding box of the object.
	 */
	
	dcMem = CkCreateCompatibleDC(dc);
#ifdef USE_CKGRAPH_IMP
	CkSelectPen(dcMem, pen);
	CkSelectBitmap(dcMem, bitmap=CkCreateCompatibleBitmap(dc, width,
		height));
#else
	oldBitmap = CkSelectBitmap(dcMem, CkCreateCompatibleBitmap(dc, width,
		height));
	oldPen = CkSelectPen(dcMem, pen);
#endif
	CkBitBlt(dcMem, 0, 0, width, height, dc, rect.left, rect.top, SRCCOPY);

	/*
	 * Translate the object for rendering in the temporary drawing
	 * surface. 
	 */

	for (i = 0; i < npoints; i++) {
	    winPoints[i].x -= rect.left;
	    winPoints[i].y -= rect.top;
	}

	/*
	 * Draw the object in the foreground color and copy it to the
	 * destination wherever the pattern is set.
	 */

	SetPolyFillMode(dcMem, (gc->fill_rule == EvenOddRule) ? ALTERNATE
		: WINDING);
#ifdef USE_CKGRAPH_IMP
	CkSelectBrush(dcMem, fgBrush=TkWinCreateSolidBrush(gc,gc->foreground));
#else
	oldMemBrush = CkSelectBrush(dcMem, TkWinCreateSolidBrush(gc,gc->foreground));
#endif
	POLYFUNC(functype, dcMem, winPoints, npoints);
	CkBitBlt(dc, rect.left, rect.top, width, height, dcMem, 0, 0, COPYFG);

	/*
	 * If we are rendering an opaque stipple, then draw the polygon in the
	 * background color and copy it to the destination wherever the pattern
	 * is clear.
	 */
	if (gc->fill_style == FillOpaqueStippled) {
#ifdef USE_CKGRAPH_IMP
	    CkSelectBrush(dcMem,bgBrush=TkWinCreateSolidBrush(gc,gc->background));
#else
	    TkWinDeleteBrush(gc,CkSelectBrush(dcMem,
		    TkWinCreateSolidBrush(gc,gc->background)));
#endif
	    POLYFUNC(functype, dcMem, winPoints, npoints);
	    CkBitBlt(dc, rect.left, rect.top, width, height, dcMem, 0, 0,
		    COPYBG);
	}

#ifdef USE_CKGRAPH_IMP
	CkDeleteDC(dcMem);
	CkDeleteBrush(patBrush);
	TkWinDeleteBrush(gc,fgBrush);
        if(bgBrush!=NULL)
	  TkWinDeleteBrush(gc,bgBrush);
	CkDeleteBitmap(bitmap);
#else
	CkSelectPen(dcMem, oldPen);
	TkWinDeleteBrush(gc,CkSelectBrush(dcMem, oldMemBrush));
	CkDeleteBitmap(CkSelectBitmap(dcMem, oldBitmap));
	CkDeleteDC(dcMem);
#endif
	SetBrushOrgEx(dc, brushOrg.x, brushOrg.y, NULL);
    } else {
#ifdef USE_CKGRAPH_IMP
        HBRUSH hBrush;
	CkSelectPen(dc, pen);
	CkSelectBrush(dc,hBrush=TkWinCreateSolidBrush(gc,gc->foreground));
#else
	oldPen = CkSelectPen(dc, pen);
	oldBrush = CkSelectBrush(dc, TkWinCreateSolidBrush(gc,gc->foreground));
#endif
	CkSetROP2(dc, tkpWinRopModes[gc->function]);

	CkSetPolyFillMode(dc, (gc->fill_rule == EvenOddRule) ? ALTERNATE
		: WINDING);
	POLYFUNC(functype, dc, winPoints, npoints);
#ifdef USE_CKGRAPH_IMP
        TkWinDeleteBrush(gc,hBrush);
#else 
	CkSelectPen(dc, oldPen);
#endif
    }
#ifndef USE_CKGRAPH_IMP
    TkWinDeleteBrush(gc,CkSelectBrush(dc, oldBrush));
#endif
    GTRACE(("end RenderObject\n");)
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawLines --
 *
 *	Draw connected lines.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders a series of connected lines.
 *
 *----------------------------------------------------------------------
 */

void
XDrawLines(display, d, gc, points, npoints, mode)
    Display* display;
    Drawable d;
    GC gc;
    XPoint* points;
    int npoints;
    int mode;
{
    HPEN pen;
    TkWinDCState state;
    HDC dc;
    
    if (d == None) {
	return;
    }
    GTRACE(("begin XDrawLines\n");)

    dc = TkWinGetDrawableDC(display, d, &state);

    pen = SetUpGraphicsPort(gc);
    CkSetBkMode(dc, TRANSPARENT);
    RenderObject(dc, gc, points, npoints, mode, pen, F_POLYLINE);
    TkWinDeletePen(gc,pen);
    
    TkWinReleaseDrawableDC(d, dc, &state);
    GTRACE(("end XDrawLines\n");)
}

/*
 *----------------------------------------------------------------------
 *
 * XFillPolygon --
 *
 *	Draws a filled polygon.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws a filled polygon on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XFillPolygon(display, d, gc, points, npoints, shape, mode)
    Display* display;
    Drawable d;
    GC gc;
    XPoint* points;
    int npoints;
    int shape;
    int mode;
{
    HPEN pen;
    TkWinDCState state;
    HDC dc;

    if (d == None) {
	return;
    }

    GTRACE(("begin XFillPoly\n");)
    dc = TkWinGetDrawableDC(display, d, &state);

    pen = CkGetStockObject(NULL_PEN);
    RenderObject(dc, gc, points, npoints, mode, pen, F_POLYGON);

    TkWinReleaseDrawableDC(d, dc, &state);
    GTRACE(("end XFillPoly\n");)
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawRectangle --
 *
 *	Draws a rectangle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws a rectangle on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XDrawRectangle(display, d, gc, x, y, width, height)
    Display* display;
    Drawable d;
    GC gc;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
{
    HPEN pen;
#ifndef USE_CKGRAPH_IMP
    HPEN oldPen;
    HBRUSH oldBrush;
#endif
    TkWinDCState state;
    HDC dc;

    if (d == None) {
	return;
    }

    dc = TkWinGetDrawableDC(display, d, &state);

    pen = SetUpGraphicsPort(gc);
    CkSetBkMode(dc, TRANSPARENT);
#ifdef USE_CKGRAPH_IMP
    CkSelectPen(dc, pen);
    CkSelectBrush(dc, GetStockObject(NULL_BRUSH));
#else
    oldPen = CkSelectPen(dc, pen);
    oldBrush = CkSelectBrush(dc, GetStockObject(NULL_BRUSH));
#endif
    CkSetROP2(dc, tkpWinRopModes[gc->function]);

    CkRectangle(dc, x, y, x+width+1, y+height+1);
#ifdef USE_CKGRAPH_IMP
    TkWinDeletePen(gc, pen);
#else
    TkWinDeletePen(gc,CkSelectPen(dc, oldPen));
    CkSelectBrush(dc, oldBrush);
#endif
    TkWinReleaseDrawableDC(d, dc, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawArc --
 *
 *	Draw an arc.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws an arc on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XDrawArc(display, d, gc, x, y, width, height, start, extent)
    Display* display;
    Drawable d;
    GC gc;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    int start;
    int extent;
{
    display->request++;
    GTRACE(("begin XDrawArc\n");)

    DrawOrFillArc(display, d, gc, x, y, width, height, start, extent, 0);
    GTRACE(("end XDrawArc\n");)
}

/*
 *----------------------------------------------------------------------
 *
 * XFillArc --
 *
 *	Draw a filled arc.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws a filled arc on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
XFillArc(display, d, gc, x, y, width, height, start, extent)
    Display* display;
    Drawable d;
    GC gc;
    int x;
    int y;
    unsigned int width;
    unsigned int height;
    int start;
    int extent;
{
    display->request++;

    GTRACE(("begin XFillArc\n");)
    DrawOrFillArc(display, d, gc, x, y, width, height, start, extent, 1);
    GTRACE(("end XFillArc\n");)
}

/*
 *----------------------------------------------------------------------
 *
 * DrawOrFillArc --
 *
 *	This procedure handles the rendering of drawn or filled
 *	arcs and chords.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the requested arc.
 *
 *----------------------------------------------------------------------
 */

/*
 * Implements the "pixeling" of small arcs, because GDI-performance
 * for this is awful
 * was made especially for BLT, graph4 demo now runs 4x faster
 *
 *
 /
/* O-outer , I-inner, B-both */
#define _ 0
#define O 1
#define I 2
#define B (O|I)
#define MINIARCS 5
static int arcus0[1]={B};
static int arcus1[4]={B,B,
                      B,B};

static int arcus2[9]={_,O,_,
                      O,I,O,
                      _,O,_};

static int arcus3[16]={_,O,O,_,
                       O,I,I,O,
                       O,I,I,O,
                       _,O,O,_};

static int arcus4[25]={_,O,O,O,_,
                       O,I,I,I,O,
                       O,I,I,I,O,
                       O,I,I,I,O,
                       _,O,O,O,_};
/* ... someone could add some more here if wanted :-) ... */
static int* arcis[MINIARCS]={arcus0,arcus1,arcus2,arcus3,arcus4};

static void DrawMiniArc(HDC dc,int width,int x,int y,
                        int mask,COLORREF inner,COLORREF outer) {
  int *arc;
  int i,j;
  if(width>MINIARCS)
    return;
  arc=arcis[width];
  for(i=0;i<=width;i++){
    for(j=0;j<=width;j++){
      if(mask&(arc[i*(width+1)+j])&O)
        SetPixel(dc,x+i,y+j,outer);
      if(mask&(arc[i*(width+1)+j])&I)
        SetPixel(dc,x+i,y+j,inner);
    }
  }
}
static void
DrawOrFillArc(display, d, gc, x, y, width, height, start, extent, fill)
    Display *display;
    Drawable d;
    GC gc;
    int x, y;			/* left top */
    unsigned int width, height;
    int start;			/* start: three-o'clock (deg*64) */
    int extent;			/* extent: relative (deg*64) */
    int fill;			/* ==0 draw, !=0 fill */
{
    HDC dc;
    HBRUSH brush;
#ifndef USE_CKGRAPH_IMP
    HBRUSH oldBrush;
    HPEN oldPen;
#endif
    HPEN pen;
    TkWinDCState state;
    int full=0;
    int clockwise = (extent < 0); /* non-zero if clockwise */
    int xstart, ystart, xend, yend;
    double radian_start, radian_end, xr, yr;

    if (d == None) {
	return;
    }

    dc = TkWinGetDrawableDC(display, d, &state);

    CkSetROP2(dc, tkpWinRopModes[gc->function]);

    /*
     * Compute the absolute starting and ending angles in normalized radians.
     * Swap the start and end if drawing clockwise.
     */
    if (start == 0 && extent == 64*360 && width==height ) {
      full=1;
      xend= xstart = x + width;
      yend = ystart = y + (int)(((double)height/2.0)+0.5);
      goto sel;
    }
    start = start % (64*360);
    if (start < 0) {
	start += (64*360);
    }
    extent = (start+extent) % (64*360);
    if (extent < 0) {
	extent += (64*360);
    }
    if (clockwise) {
	int tmp = start;
	start = extent;
	extent = tmp;
    }
    radian_start = XAngleToRadians(start);
    radian_end = XAngleToRadians(extent);

    /*
     * Now compute points on the radial lines that define the starting and
     * ending angles.  Be sure to take into account that the y-coordinate
     * system is inverted.
     */

    xr = x + width / 2.0;
    yr = y + height / 2.0;
    xstart = (int)((xr + cos(radian_start)*width/2.0) + 0.5);
    ystart = (int)((yr + sin(-radian_start)*height/2.0) + 0.5);
    xend = (int)((xr + cos(radian_end)*width/2.0) + 0.5);
    yend = (int)((yr + sin(-radian_end)*height/2.0) + 0.5);

    /*
     * Now draw a filled or open figure.  Note that we have to
     * increase the size of the bounding box by one to account for the
     * difference in pixel definitions between X and Windows.
     */
sel:
    if(full && width==height && width<MINIARCS){
        if(!fill)
            DrawMiniArc(dc,width, x, y,O,0,gc->foreground);
        else
            DrawMiniArc(dc,width, x, y,I,gc->foreground,0);
        goto dcfree;

    }
    pen = SetUpGraphicsPort(gc);
#ifdef USE_CKGRAPH_IMP
    CkSelectPen(dc, pen);
#else
    oldPen = CkSelectPen(dc, pen);
#endif
    if (!fill) {
	/*
	 * Note that this call will leave a gap of one pixel at the
	 * end of the arc for thin arcs.  We can't use ArcTo because
	 * it's only supported under Windows NT.
	 */

	CkSetBkMode(dc, TRANSPARENT);
	CkArc(dc, x, y, x+width+1, y+height+1, xstart, ystart, xend, yend);
    } else {
	brush = TkWinCreateSolidBrush(gc,gc->foreground);
#ifdef USE_CKGRAPH_IMP
	CkSelectBrush(dc, brush);
#else
	oldBrush = SelectObject(dc, brush);
#endif
	if (gc->arc_mode == ArcChord) {
	    CkChord(dc, x, y, x+width+1, y+height+1, xstart, ystart, xend, yend);
	} else if ( gc->arc_mode == ArcPieSlice ) {
	    CkPie(dc, x, y, x+width+1, y+height+1, xstart, ystart, xend, yend);
	}
#ifdef USE_CKGRAPH_IMP
	TkWinDeleteBrush(gc, brush);
#else
	TkWinDeleteBrush(gc , CkSelectBrush(dc, oldBrush));
#endif
    }
#ifdef USE_CKGRAPH_IMP
    TkWinDeletePen(gc,pen);
#else
    TkWinDeletePen(gc,CkSelectPen(dc, oldPen));
#endif
dcfree:
    TkWinReleaseDrawableDC(d, dc, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * SetUpGraphicsPort --
 *
 *	Set up the graphics port from the given GC.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current port is adjusted.
 *
 *----------------------------------------------------------------------
 */

static HPEN
SetUpGraphicsPort(gc)
    GC gc;
{
    DWORD style;

    if (gc->line_style == LineOnOffDash) {
	unsigned char *p = (unsigned char *) &(gc->dashes);
				/* pointer to the dash-list */

	/*
	 * Below is a simple translation of serveral dash patterns
	 * to valid windows pen types. Far from complete,
	 * but I don't know how to do it better.
	 * Any ideas: <mailto:j.nijtmans@chello.nl>
	 */

	if (p[1] && p[2]) {
	    if (!p[3] || p[4]) {
		style = PS_DASHDOTDOT;		/*	-..	*/
	    } else {
		style = PS_DASHDOT;		/*	-.	*/
	    }
	} else {
	    if (p[0] > (4 * gc->line_width)) {
		style = PS_DASH;		/*	-	*/
	    } else {
		style = PS_DOT;			/*	.	*/
	    }
	}
    } else {
	style = PS_SOLID;
    }
    if (gc->line_width < 2) {
	return TkWinCreatePen(gc, style, gc->line_width, gc->foreground);
    } else {
	LOGBRUSH lb;

	lb.lbStyle = BS_SOLID;
	lb.lbColor = gc->foreground;
	lb.lbHatch = 0;

	style |= PS_GEOMETRIC;
	switch (gc->cap_style) {
	    case CapNotLast:
	    case CapButt:
		style |= PS_ENDCAP_FLAT; 
		break;
	    case CapRound:
		style |= PS_ENDCAP_ROUND; 
		break;
	    default:
		style |= PS_ENDCAP_SQUARE; 
		break;
	}
	switch (gc->join_style) {
	    case JoinMiter: 
		style |= PS_JOIN_MITER; 
		break;
	    case JoinRound:
		style |= PS_JOIN_ROUND; 
		break;
	    default:
		style |= PS_JOIN_BEVEL; 
		break;
	}
	return TkWinExtCreatePen(gc, style, gc->line_width, &lb, 0, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkScrollWindow --
 *
 *	Scroll a rectangle of the specified window and accumulate
 *	a damage region.
 *
 * Results:
 *	Returns 0 if the scroll genereated no additional damage.
 *	Otherwise, sets the region that needs to be repainted after
 *	scrolling and returns 1.
 *
 * Side effects:
 *	Scrolls the bits in the window.
 *
 *----------------------------------------------------------------------
 */

int
TkScrollWindow(tkwin, gc, x, y, width, height, dx, dy, damageRgn)
    Tk_Window tkwin;		/* The window to be scrolled. */
    GC gc;			/* GC for window to be scrolled. */
    int x, y, width, height;	/* Position rectangle to be scrolled. */
    int dx, dy;			/* Distance rectangle should be moved. */
    TkRegion damageRgn;		/* Region to accumulate damage in. */
{
    HWND hwnd = TkWinGetHWND(Tk_WindowId(tkwin));
    RECT scrollRect;

    scrollRect.left = x;
    scrollRect.top = y;
    scrollRect.right = x + width;
    scrollRect.bottom = y + height;
    return (ScrollWindowEx(hwnd, dx, dy, &scrollRect, NULL, (HRGN) damageRgn,
	    NULL, 0) == NULLREGION) ? 0 : 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinFillRect --
 *
 *	This routine fills a rectangle with the foreground color
 *	from the specified GC ignoring all other GC values.  This
 *	is the fastest way to fill a drawable with a solid color.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the contents of the DC drawing surface.
 *
 *----------------------------------------------------------------------
 */

#ifdef FILLRECTGC
void
TkWinFillRectGC(dc, x, y, width, height, pixel,gc)
    HDC dc;
    int x, y, width, height;
    int pixel;
    GC gc;
{
    HBRUSH hbr;
    RECT rect;
    rect.left = x;
    rect.top = y;
    rect.right = x + width;
    rect.bottom = y + height;
    GTRACE(("begin TkWinFillRectGC\n");)
    FillRect(dc,&rect,hbr=TkWinCreateSolidBrush(gc,pixel));
    TkWinDeleteBrush(gc,hbr);
    GTRACE(("end TkWinFillRectGC\n");)
}
#endif
void
TkWinFillRect(dc, x, y, width, height, pixel)
    HDC dc;
    int x, y, width, height;
    int pixel;
{
    RECT rect;
    rect.left = x;
    rect.top = y;
    rect.right = x + width;
    rect.bottom = y + height;
    GTRACE(("begin TkWinFillRect\n");)
    CkSetBkColor(dc, (COLORREF)pixel);
    CkSetBkMode(dc, OPAQUE);
    CkExtTextOut(dc, 0, 0, ETO_OPAQUE, &rect, NULL, 0, NULL);
    GTRACE(("end TkWinFillRect\n");)
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDrawHighlightBorder --
 *
 *	This procedure draws a rectangular ring around the outside of
 *	a widget to indicate that it has received the input focus.
 *
 *      On Windows, we just draw the simple inset ring.  On other sytems,
 *      e.g. the Mac, the focus ring is a little more complicated, so we
 *      need this abstraction.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A rectangle "width" pixels wide is drawn in "drawable",
 *	corresponding to the outer area of "tkwin".
 *
 *----------------------------------------------------------------------
 */

void 
TkpDrawHighlightBorder(tkwin, fgGC, bgGC, highlightWidth, drawable)
    Tk_Window tkwin;
    GC fgGC;
    GC bgGC;
    int highlightWidth;
    Drawable drawable;
{
    TkDrawInsetFocusHighlight(tkwin, fgGC, highlightWidth, drawable, 0);
}

