/*
 * rbcWinDraw.c --
 *
 *      This module contains WIN32 routines not included in the Tcl/Tk
 *      libraries.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"
#include <X11/Xlib.h>

/*
 * Data structure for setting graphics context.
 */
typedef struct {
    int function;		/* logical operation */
    unsigned long plane_mask;	/* plane mask */
    unsigned long foreground;	/* foreground pixel */
    unsigned long background;	/* background pixel */
    int line_width;		/* line width */
    int line_style;		/* LineSolid, LineOnOffDash, LineDoubleDash */
    int cap_style;		/* CapNotLast, CapButt,
				   CapRound, CapProjecting */
    int join_style;		/* JoinMiter, JoinRound, JoinBevel */
    int fill_style;		/* FillSolid, FillTiled,
				   FillStippled, FillOpaeueStippled */
    int fill_rule;		/* EvenOddRule, WindingRule */
    int arc_mode;		/* ArcChord, ArcPieSlice */
    Pixmap tile;		/* tile pixmap for tiling operations */
    Pixmap stipple;		/* stipple 1 plane pixmap for stipping */
    int ts_x_origin;		/* offset for tile or stipple operations */
    int ts_y_origin;
    Font font;			/* default text font for text operations */
    int subwindow_mode;		/* ClipByChildren, IncludeInferiors */
    Bool graphics_exposures;	/* boolean, should exposures be generated */
    int clip_x_origin;		/* origin for clipping */
    int clip_y_origin;
    Pixmap clip_mask;		/* bitmap clipping; other calls for rects */
    int dash_offset;		/* patterned/dashed line information */
    char dashes;		/* If -1, indicates that the extended
				 * information below is available. */
    int nDashValues;
    char dashValues[12];
} XGCValuesEx;

typedef struct {
    HDC dc;
    int count;
    COLORREF color;
    int offset, nBits;
} DashInfo;

static Tcl_Encoding systemEncoding = NULL;

static HFONT CreateRotatedFont(
    unsigned long fontId,
    double theta);
static XGCValuesEx *CreateGC();
static BOOL DrawChars(
    HDC dc,
    int x,
    int y,
    char *string,
    int length);
static int GetDashInfo(
    HDC dc,
    GC gc,
    DashInfo *infoPtr);
static void CALLBACK DrawDot(
    int x,
    int y,
    LPARAM clientData);

/*
 *--------------------------------------------------------------
 *
 * RbcGetPlatformId --
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
int
RbcGetPlatformId(
    )
{
    static int      platformId = 0;

    if (platformId == 0) {
        OSVERSIONINFO   opsysInfo;

        opsysInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
        if (GetVersionEx(&opsysInfo)) {
            platformId = opsysInfo.dwPlatformId;
        }
    }
    return platformId;
}

/*
 *--------------------------------------------------------------
 *
 * RbcLastError --
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
char           *
RbcLastError(
    )
{
    static char     buffer[1024];
    int             length;

    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), /* Default language */
        buffer, 1024, NULL);
    length = strlen(buffer);
    if (buffer[length - 2] == '\r') {
        buffer[length - 2] = '\0';
    }
    return buffer;
}

/*
 *--------------------------------------------------------------
 *
 * RbcGetSystemPalette --
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
HPALETTE
RbcGetSystemPalette()
{
    HDC hDC;
    HPALETTE hPalette;
    DWORD flags;
    hPalette = NULL;
    hDC = GetDC(NULL);		/* Get the desktop device context */
    flags = GetDeviceCaps(hDC, RASTERCAPS);
    if (flags & RC_PALETTE) {
        LOGPALETTE *palettePtr;
        palettePtr = (LOGPALETTE *)
                     GlobalAlloc(GPTR, sizeof(LOGPALETTE) + 256 * sizeof(PALETTEENTRY));
        palettePtr->palVersion = 0x300;
        palettePtr->palNumEntries = 256;
        GetSystemPaletteEntries(hDC, 0, 256, palettePtr->palPalEntry);
        hPalette = CreatePalette(palettePtr);
        GlobalFree(palettePtr);
    }
    ReleaseDC(NULL, hDC);
    return hPalette;
}

/*
 *--------------------------------------------------------------
 *
 * RbcGetBitmapData --
 *
 *      Returns the DIB bits from a bitmap.
 *
 * Results:
 *      Returns a byte array of bitmap data or NULL if an error
 *      occurred.  The parameter pitchPtr returns the number
 *      of bytes per row.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
unsigned char *
RbcGetBitmapData(
    Display *display, /* Display of bitmap */
    Pixmap bitmap, /* Bitmap to query */
    int width, /* Width of bitmap */
    int height, /* Height of bitmap */
    int *pitchPtr) /* (out) Number of bytes per row */
{
    TkWinDCState state;
    HDC dc;
    int result;
    unsigned char *bits;
    unsigned int size;
    HBITMAP hBitmap;
    BITMAPINFOHEADER *bmiPtr;
    HANDLE hMem, hMem2;
    int bytesPerRow, imageSize;
    size = sizeof(BITMAPINFOHEADER) + 2 * sizeof(RGBQUAD);
    hMem = GlobalAlloc(GHND, size);
    bmiPtr = (BITMAPINFOHEADER *)GlobalLock(hMem);
    bmiPtr->biSize = sizeof(BITMAPINFOHEADER);
    bmiPtr->biPlanes = 1;
    bmiPtr->biBitCount = 1;
    bmiPtr->biCompression = BI_RGB;
    bmiPtr->biWidth = width;
    bmiPtr->biHeight = height;
    hBitmap = ((TkWinDrawable *)bitmap)->bitmap.handle;
    dc = TkWinGetDrawableDC(display, bitmap, &state);
    result = GetDIBits(dc, hBitmap, 0, height, (LPVOID)NULL,
                       (BITMAPINFO *)bmiPtr, DIB_RGB_COLORS);
    TkWinReleaseDrawableDC(bitmap, dc, &state);
    if (!result) {
        GlobalUnlock(hMem);
        GlobalFree(hMem);
        return NULL;
    }
    imageSize = bmiPtr->biSizeImage;
    GlobalUnlock(hMem);
    bytesPerRow = ((width + 31) & ~31) / 8;
    if (imageSize == 0) {
        imageSize = bytesPerRow * height;
    }
    hMem2 = GlobalReAlloc(hMem, size + imageSize, 0);
    if (hMem2 == NULL) {
        GlobalFree(hMem);
        return NULL;
    }
    hMem = hMem2;
    bmiPtr = (LPBITMAPINFOHEADER)GlobalLock(hMem);
    dc = TkWinGetDrawableDC(display, bitmap, &state);
    result = GetDIBits(dc, hBitmap, 0, height, (unsigned char *)bmiPtr + size,
                       (BITMAPINFO *)bmiPtr, DIB_RGB_COLORS);
    TkWinReleaseDrawableDC(bitmap, dc, &state);
    bits = NULL;
    if (!result) {
        OutputDebugStringA("GetDIBits failed\n");
    } else {
        bits = (unsigned char *)ckalloc(imageSize);
        if (bits != NULL) {
            memcpy (bits, (unsigned char *)bmiPtr + size, imageSize);
        }
    }
    *pitchPtr = bytesPerRow;
    GlobalUnlock(hMem);
    GlobalFree(hMem);
    return bits;
}

/*
 *--------------------------------------------------------------
 *
 * RbcSetROP2 --
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
void
RbcSetROP2(
    HDC dc,
    int function)
{
static int WinRop2Mode[] = {
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
    if (function < 0 || function > 15) return;
    SetROP2(dc, WinRop2Mode[function]);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcEmulateXCreateGC --
 *
 *      Allocate a new extended GC, and initialize the specified fields.
 *
 * Results:
 *      Returns a newly allocated GC.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
GC
RbcEmulateXCreateGC(
    Display *display,
    Drawable drawable,
    unsigned long mask,
    XGCValues *srcPtr)
{
    XGCValuesEx *destPtr;
    destPtr = CreateGC();
    if (destPtr == NULL) {
        return None;
    }
    if (mask & GCFunction) {
        destPtr->function = srcPtr->function;
    }
    if (mask & GCPlaneMask) {
        destPtr->plane_mask = srcPtr->plane_mask;
    }
    if (mask & GCForeground) {
        destPtr->foreground = srcPtr->foreground;
    }
    if (mask & GCBackground) {
        destPtr->background = srcPtr->background;
    }
    if (mask & GCLineWidth) {
        destPtr->line_width = srcPtr->line_width;
    }
    if (mask & GCLineStyle) {
        destPtr->line_style = srcPtr->line_style;
    }
    if (mask & GCCapStyle) {
        destPtr->cap_style = srcPtr->cap_style;
    }
    if (mask & GCJoinStyle) {
        destPtr->join_style = srcPtr->join_style;
    }
    if (mask & GCFillStyle) {
        destPtr->fill_style = srcPtr->fill_style;
    }
    if (mask & GCFillRule) {
        destPtr->fill_rule = srcPtr->fill_rule;
    }
    if (mask & GCArcMode) {
        destPtr->arc_mode = srcPtr->arc_mode;
    }
    if (mask & GCTile) {
        destPtr->tile = srcPtr->tile;
    }
    if (mask & GCStipple) {
        destPtr->stipple = srcPtr->stipple;
    }
    if (mask & GCTileStipXOrigin) {
        destPtr->ts_x_origin = srcPtr->ts_x_origin;
    }
    if (mask & GCTileStipXOrigin) {
        destPtr->ts_y_origin = srcPtr->ts_y_origin;
    }
    if (mask & GCFont) {
        destPtr->font = srcPtr->font;
    }
    if (mask & GCSubwindowMode) {
        destPtr->subwindow_mode = srcPtr->subwindow_mode;
    }
    if (mask & GCGraphicsExposures) {
        destPtr->graphics_exposures = srcPtr->graphics_exposures;
    }
    if (mask & GCClipXOrigin) {
        destPtr->clip_x_origin = srcPtr->clip_x_origin;
    }
    if (mask & GCClipYOrigin) {
        destPtr->clip_y_origin = srcPtr->clip_y_origin;
    }
    if (mask & GCDashOffset) {
        destPtr->dash_offset = srcPtr->dash_offset;
    }
    if (mask & GCDashList) {
        destPtr->dashes = srcPtr->dashes;
    }
    if (mask & GCClipMask) {
        struct ClipMask {
            int type;		/* TKP_CLIP_PIXMAP or TKP_CLIP_REGION */
            Pixmap pixmap;
        } *clipPtr;
        clipPtr = (struct ClipMask *)ckalloc(sizeof(struct ClipMask));
#define TKP_CLIP_PIXMAP 0
        clipPtr->type = TKP_CLIP_PIXMAP;
        clipPtr->pixmap = srcPtr->clip_mask;
        destPtr->clip_mask = (Pixmap) clipPtr;
    }
    return (GC)destPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcGCToPen --
 *
 *      Set up the graphics port from the given GC.
 *
 *      Geometric and cosmetic pens available under both 95 and NT.
 *      Geometric pens differ from cosmetic pens in that they can
 *        1. Draw in world units (can have thick lines: line width > 1).
 *        2. Under NT, allow arbitrary line style.
 *        3. Can have caps and join (needed for thick lines).
 *        4. Draw very, very slowly.
 *
 *      Cosmetic pens are single line width only.
 *
 *                       95   98   NT
 *        PS_SOLID      c,g  c,g  c,g
 *        PS_DASH       c,g  c,g  c,g
 *        PS_DOT          c    c  c,g
 *        PS_DASHDOT      c    -  c,g
 *        PS_DASHDOTDOT   c    -  c,g
 *        PS_USERSTYLE    -    -  c,g
 *        PS_ALTERNATE    -    -    c
 *
 *      Geometric only for 95/98
 *
 *        PS_ENDCAP_ROUND
 *        PS_ENDCAP_SQUARE
 *        PS_ENDCAP_FLAT
 *        PS_JOIN_BEVEL
 *        PS_JOIN_ROUND
 *        PS_JOIN_MITER
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The current port is adjusted.
 *
 *----------------------------------------------------------------------
 */
HPEN
RbcGCToPen(
    HDC dc,
    GC gc)
{
    DWORD lineAttrs, lineStyle;
    DWORD dashArr[12];
    DWORD *dashPtr;
    int nValues, lineWidth;
    LOGBRUSH lBrush;
    HPEN pen;
    nValues = 0;
    lineWidth = (gc->line_width < 1) ? 1 : gc->line_width;
    if ((gc->line_style == LineOnOffDash) ||
            (gc->line_style == LineDoubleDash)) {
        XGCValuesEx *gcPtr = (XGCValuesEx *)gc;
        if ((int)gc->dashes == -1) {
            register int i;
            nValues = strlen(gcPtr->dashValues);
            for (i = 0; i < nValues; i++) {
                dashArr[i] = (DWORD)gcPtr->dashValues[i];
            }
            if (nValues == 1) {
                dashArr[1] = dashArr[0];
                nValues = 2;
            }
        } else {
            dashArr[1] = dashArr[0] = (DWORD) gc->dashes;
            nValues = 2;
            gc->dashes = -1;
        }
    }
    switch (nValues) {
        case 0:
            lineStyle = PS_SOLID;
            break;
        case 3:
            lineStyle = PS_DASHDOT;
            break;
        case 4:
            lineStyle = PS_DASHDOTDOT;
            break;
        case 2:
        default:
            /* PS_DASH style dash length is too long. */
            lineStyle = PS_DOT;
            break;
    }
    lBrush.lbStyle = BS_SOLID;
    lBrush.lbColor = gc->foreground;
    lBrush.lbHatch = 0;		/* Value is ignored when style is BS_SOLID. */
    lineAttrs = 0;
    switch (gc->cap_style) {
        case CapNotLast:
        case CapButt:
            lineAttrs |= PS_ENDCAP_FLAT;
            break;
        case CapRound:
            lineAttrs |= PS_ENDCAP_ROUND;
            break;
        default:
            lineAttrs |= PS_ENDCAP_SQUARE;
            break;
    }
    switch (gc->join_style) {
        case JoinMiter:
            lineAttrs |= PS_JOIN_MITER;
            break;
        case JoinBevel:
            lineAttrs |= PS_JOIN_BEVEL;
            break;
        case JoinRound:
        default:
            lineAttrs |= PS_JOIN_ROUND;
            break;
    }
    SetBkMode(dc, TRANSPARENT);
    if (RbcGetPlatformId() == VER_PLATFORM_WIN32_NT) {
        /* Windows NT/2000/XP. */
        if (nValues > 0) {
            lineStyle = PS_USERSTYLE;
            dashPtr = dashArr;
        } else {
            dashPtr = NULL;
        }
        if (lineWidth > 1) {
            /* Limit the use of geometric pens to thick lines. */
            pen = ExtCreatePen(PS_GEOMETRIC | lineAttrs | lineStyle, lineWidth,
                               &lBrush, nValues, dashPtr);
        } else {
            /* Cosmetic pens are much faster. */
            pen = ExtCreatePen(PS_COSMETIC | lineAttrs | lineStyle, 1, &lBrush,
                               nValues, dashPtr);
        }
    } else {
        /* Windows 95/98. */
        if ((lineStyle == PS_SOLID) && (lineWidth > 1)) {
            /* Use geometric pens with solid, thick lines only. */
            pen = ExtCreatePen(PS_GEOMETRIC | lineAttrs | lineStyle, lineWidth,
                               &lBrush, 0, NULL);
        } else {
            /* Otherwise sacrifice thick lines for dashes. */
            pen = ExtCreatePen(PS_COSMETIC | lineStyle, 1, &lBrush, 0, NULL);
        }
    }
    assert(pen != NULL);
    return pen;
}

/*
 *--------------------------------------------------------------
 *
 * RbcDrawRotatedText --
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
int
RbcDrawRotatedText(
    Display *display,
    Drawable drawable,
    int x,
    int y,
    double theta,
    RbcTextStyle *tsPtr,
    RbcTextLayout *textPtr)
{
    HFONT hFont, oldFont;
    TkWinDCState state;
    HDC hDC;
    int isActive;
    int bbWidth, bbHeight;
    double rotWidth, rotHeight;
    double sinTheta, cosTheta;
    RbcPoint2D p, q, center;
    register RbcTextFragment *fragPtr, *endPtr;
    static int initialized = 0;
    if (!initialized) {
        if (RbcGetPlatformId() == VER_PLATFORM_WIN32_NT) {
            /*
             * If running NT, then we will be calling some Unicode functions
             * explictly.  So, even if the Tcl system encoding isn't Unicode,
             * make sure we convert to/from the Unicode char set.
             */
            systemEncoding = Tcl_GetEncoding(NULL, "unicode");
        }
        initialized = 1;
    }
    hFont = CreateRotatedFont(tsPtr->gc->font, theta);
    if (hFont == NULL) {
        return FALSE;
    }
    isActive = (tsPtr->state & RBC_STATE_ACTIVE);
    hDC = TkWinGetDrawableDC(display, drawable, &state);
    RbcSetROP2(hDC, tsPtr->gc->function);
    oldFont = SelectFont(hDC, hFont);
    RbcGetBoundingBox(textPtr->width, textPtr->height, theta, &rotWidth,
                       &rotHeight, (RbcPoint2D *)NULL);
    bbWidth = ROUND(rotWidth);
    bbHeight = ROUND(rotHeight);
    RbcTranslateAnchor(x, y, bbWidth, bbHeight, tsPtr->anchor, &x, &y);
    center.x = (double)textPtr->width * -0.5;
    center.y = (double)textPtr->height * -0.5;
    theta = (-theta / 180.0) * M_PI;
    sinTheta = sin(theta), cosTheta = cos(theta);
    endPtr = textPtr->fragArr + textPtr->nFrags;
    for (fragPtr = textPtr->fragArr; fragPtr < endPtr; fragPtr++) {
        p.x = center.x + (double)fragPtr->x;
        p.y = center.y + (double)fragPtr->y;
        q.x = x + (p.x * cosTheta) - (p.y * sinTheta) + (bbWidth * 0.5);
        q.y = y + (p.x * sinTheta) + (p.y * cosTheta) + (bbHeight * 0.5);
        fragPtr->sx = ROUND(q.x);
        fragPtr->sy = ROUND(q.y);
    }
    SetBkMode(hDC, TRANSPARENT);
    SetTextAlign(hDC, TA_LEFT | TA_BASELINE);
    if (tsPtr->state & (RBC_STATE_DISABLED | RBC_STATE_EMPHASIS)) {
        TkBorder *borderPtr = (TkBorder *) tsPtr->border;
        XColor *color1, *color2;
        color1 = borderPtr->lightColorPtr, color2 = borderPtr->darkColorPtr;
        if (tsPtr->state & RBC_STATE_EMPHASIS) {
            XColor *hold;
            hold = color1, color1 = color2, color2 = hold;
        }
        if (color1 != NULL) {
            SetTextColor(hDC, color1->pixel);
            for (fragPtr = textPtr->fragArr; fragPtr < endPtr; fragPtr++) {
                DrawChars(hDC, fragPtr->sx, fragPtr->sy, fragPtr->text,
                          fragPtr->count);
            }
        }
        if (color2 != NULL) {
            SetTextColor(hDC, color2->pixel);
            for (fragPtr = textPtr->fragArr; fragPtr < endPtr; fragPtr++) {
                DrawChars(hDC, fragPtr->sx + 1, fragPtr->sy + 1, fragPtr->text,
                          fragPtr->count);
            }
        }
        goto done;		/* Done */
    }
    SetBkMode(hDC, TRANSPARENT);
    if ((tsPtr->shadow.offset > 0) && (tsPtr->shadow.color != NULL)) {
        SetTextColor(hDC, tsPtr->shadow.color->pixel);
        for (fragPtr = textPtr->fragArr; fragPtr < endPtr; fragPtr++) {
            DrawChars(hDC, fragPtr->sx + tsPtr->shadow.offset,
                      fragPtr->sy + tsPtr->shadow.offset, fragPtr->text,
                      fragPtr->count);
        }
    }
    if (isActive) {
        SetTextColor(hDC, tsPtr->activeColor->pixel);
    } else {
        SetTextColor(hDC, tsPtr->color->pixel);
    }
    for (fragPtr = textPtr->fragArr; fragPtr < endPtr; fragPtr++) {
        DrawChars(hDC, fragPtr->sx, fragPtr->sy, fragPtr->text,
                  fragPtr->count);
    }
    if (isActive) {
        SetTextColor(hDC, tsPtr->color->pixel);
    }
done:
    SelectFont(hDC, oldFont);
    DeleteFont(hFont);
    TkWinReleaseDrawableDC(drawable, hDC, &state);
    return TRUE;
}

/*
 *--------------------------------------------------------------
 *
 * RbcSetDashes --
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
void
RbcSetDashes(
    Display *display,
    GC gc,
    RbcDashes *dashesPtr)
{
    XGCValuesEx *gcPtr = (XGCValuesEx *)gc;
    /* This must be used only with a privately created GC */
    assert((int)gcPtr->dashes == -1);
    gcPtr->nDashValues = strlen(dashesPtr->values);
    gcPtr->dash_offset = dashesPtr->offset;
    strcpy(gcPtr->dashValues, dashesPtr->values);
}

/*
 *--------------------------------------------------------------
 *
 * CreateRotatedFont --
 *
 *      Creates a rotated copy of the given font.  This only works
 *      for TrueType fonts.
 *
 * Results:
 *      Returns the newly create font or NULL if the font could not
 *      be created.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static HFONT
CreateRotatedFont(
    unsigned long fontId, /* Font identifier (actually a Tk_Font) */
    double theta) /* Number of degrees to rotate font */
{
    TkFontAttributes *faPtr;	/* Set of attributes to match. */
    TkFont *fontPtr;
    HFONT hFont;
    LOGFONTW lf;
    fontPtr = (TkFont *) fontId;
    faPtr = &fontPtr->fa;
    ZeroMemory(&lf, sizeof(LOGFONT));
    lf.lfHeight = -faPtr->size;
    if (lf.lfHeight < 0) {
        HDC dc;
        dc = GetDC(NULL);
        lf.lfHeight = -MulDiv(faPtr->size,
                              GetDeviceCaps(dc, LOGPIXELSY), 72);
        ReleaseDC(NULL, dc);
    }
    lf.lfWidth = 0;
    lf.lfEscapement = lf.lfOrientation = ROUND(theta * 10.0);
#define TK_FW_NORMAL	0
    lf.lfWeight = (faPtr->weight == TK_FW_NORMAL) ? FW_NORMAL : FW_BOLD;
    lf.lfItalic = faPtr->slant;
    lf.lfUnderline = faPtr->underline;
    lf.lfStrikeOut = faPtr->overstrike;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_ONLY_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfQuality = DEFAULT_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    hFont = NULL;
    if (faPtr->family == NULL) {
        lf.lfFaceName[0] = '\0';
    } else {
        Tcl_DString dString;
        Tcl_UtfToExternalDString(systemEncoding, faPtr->family, -1, &dString);
        if (RbcGetPlatformId() == VER_PLATFORM_WIN32_NT) {
            Tcl_UniChar *src, *dst;
            /*
             * We can only store up to LF_FACESIZE wide characters
             */
            if (Tcl_DStringLength(&dString) >= (int)(LF_FACESIZE * sizeof(WCHAR))) {
                Tcl_DStringSetLength(&dString, LF_FACESIZE);
            }
            src = (Tcl_UniChar *)Tcl_DStringValue(&dString);
            dst = (Tcl_UniChar *)lf.lfFaceName;
            while (*src != '\0') {
                *dst++ = *src++;
            }
            *dst = '\0';
            hFont = CreateFontIndirectW((LOGFONTW *)&lf);
        } else {
            /*
             * We can only store up to LF_FACESIZE characters
             */
            if (Tcl_DStringLength(&dString) >= LF_FACESIZE) {
                Tcl_DStringSetLength(&dString, LF_FACESIZE);
            }
            strcpy((char *)lf.lfFaceName, Tcl_DStringValue(&dString));
            hFont = CreateFontIndirectA((LOGFONTA *)&lf);
        }
        Tcl_DStringFree(&dString);
    }
    if (hFont == NULL) {
    } else {
        HFONT oldFont;
        TEXTMETRIC tm;
        HDC hRefDC;
        int result;
        /* Check if the rotated font is really a TrueType font. */
        hRefDC = GetDC(NULL);		/* Get the desktop device context */
        oldFont = SelectFont(hRefDC, hFont);
        result = ((GetTextMetrics(hRefDC, &tm)) &&
                  (tm.tmPitchAndFamily & TMPF_TRUETYPE));
        SelectFont(hRefDC, oldFont);
        ReleaseDC(NULL, hRefDC);
        if (!result) {
            DeleteFont(hFont);
            return NULL;
        }
    }
    return hFont;
}

/*
 *--------------------------------------------------------------
 *
 * CreateGC --
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
static XGCValuesEx *
CreateGC()
{
    XGCValuesEx *gcPtr;
    gcPtr = (XGCValuesEx *)ckalloc(sizeof(XGCValuesEx));
    if (gcPtr == NULL) {
        return NULL;
    }
    gcPtr->arc_mode = ArcPieSlice;
    gcPtr->background = 0xffffff;
    gcPtr->cap_style = CapNotLast;
    gcPtr->clip_mask = None;
    gcPtr->clip_x_origin = gcPtr->clip_y_origin = 0;
    gcPtr->dash_offset	= 0;
    gcPtr->fill_rule = WindingRule;
    gcPtr->fill_style = FillSolid;
    gcPtr->font = None;
    gcPtr->foreground = 0;
    gcPtr->function = GXcopy;
    gcPtr->graphics_exposures = True;
    gcPtr->join_style = JoinMiter;
    gcPtr->line_style = LineSolid;
    gcPtr->line_width = 0;
    gcPtr->plane_mask = ~0;
    gcPtr->stipple = None;
    gcPtr->subwindow_mode = ClipByChildren;
    gcPtr->tile = None;
    gcPtr->ts_x_origin = gcPtr->ts_y_origin = 0;
    gcPtr->dashes = -1;    /* Mark that this an extended GC */
    gcPtr->nDashValues	= 0;
    return gcPtr;
}

/*
 *--------------------------------------------------------------
 *
 * DrawChars --
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
static BOOL
DrawChars(
    HDC dc,
    int x,
    int y,
    char *string,
    int length)
{
    BOOL result;
    if (systemEncoding == NULL) {
        result = TextOutA(dc, x, y, string, length);
    } else {
        const unsigned short *wstring;
        Tcl_DString dString;
        Tcl_DStringInit(&dString);
        Tcl_UtfToExternalDString(systemEncoding, string, length, &dString);
        length = Tcl_NumUtfChars(string, -1);
        wstring = (const unsigned short *)Tcl_DStringValue(&dString);
        result = TextOutW(dc, x, y, wstring, length);
        Tcl_DStringFree(&dString);
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcEmulateXDrawSegments --
 *
 *      Draws multiple, unconnected lines. For each segment, draws a
 *      line between (x1, y1) and (x2, y2).  It draws the lines in the
 *      order listed in the array of XSegment structures and does not
 *      perform joining at coincident endpoints.  For any given line,
 *      does not draw a pixel more than once. If lines intersect, the
 *      intersecting pixels are drawn multiple times.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Draws unconnected line segments on the specified drawable.
 *
 *----------------------------------------------------------------------
 */
void
RbcEmulateXDrawSegments(
    Display *display,
    Drawable drawable,
    GC gc,
    XSegment *segArr,
    int nSegments)
{
    HDC dc;
    register XSegment *segPtr, *endPtr;
    TkWinDCState state;

    display->request++;
    if (drawable == None) {
        return;
    }
    dc = TkWinGetDrawableDC(display, drawable, &state);
    RbcSetROP2(dc, gc->function);
    if (gc->line_style != LineSolid) {
        /* Handle dotted lines specially */
        DashInfo info;

        if (!GetDashInfo(dc, gc, &info)) {
            goto solidLine;
        }
        endPtr = segArr + nSegments;
        for (segPtr = segArr; segPtr < endPtr; segPtr++) {
            info.count = 0; /* Reset dash counter after every segment. */
            LineDDA(segPtr->x1, segPtr->y1, segPtr->x2, segPtr->y2, DrawDot,
                    (LPARAM)&info);
        }
    } else {
        HPEN pen, oldPen;

solidLine:
        pen = RbcGCToPen(dc, gc);
        oldPen = SelectPen(dc, pen);
        endPtr = segArr + nSegments;
        for (segPtr = segArr; segPtr < endPtr; segPtr++) {
            MoveToEx(dc, segPtr->x1, segPtr->y1, (LPPOINT)NULL);
            LineTo(dc, segPtr->x2, segPtr->y2);
        }
        DeletePen(SelectPen(dc, oldPen));
    }
    TkWinReleaseDrawableDC(drawable, dc, &state);
}

/*
 *--------------------------------------------------------------
 *
 * GetDashInfo --
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
static int
GetDashInfo(
    HDC dc,
    GC gc,
    DashInfo *infoPtr)
{
    int dashOffset, dashValue;

    dashValue = 0;
    dashOffset = gc->dash_offset;
    if ((int)gc->dashes == -1) {
        XGCValuesEx *gcPtr = (XGCValuesEx *)gc;

        if (gcPtr->nDashValues == 1) {
            dashValue = gcPtr->dashValues[0];
        }
    } else if (gc->dashes > 0) {
        dashValue = (int)gc->dashes;
    }
    if (dashValue == 0) {
        return FALSE;
    }
    infoPtr->dc = dc;
    infoPtr->nBits = dashValue;
    infoPtr->offset = dashOffset;
    infoPtr->count = 0;
    infoPtr->color = gc->foreground;
    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * DrawDot --
 *
 *      Draws a dot.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Renders a dot.
 *
 *----------------------------------------------------------------------
 */
static void CALLBACK DrawDot(
    int x, /* y-coordinates of point */
    int y, /* y-coordinates of point */
    LPARAM clientData)
{				/* Line information */
    DashInfo *infoPtr = (DashInfo *) clientData;
    int count;

    infoPtr->count++;
    count = (infoPtr->count + infoPtr->offset) / infoPtr->nBits;
    if (count & 0x1) {
        SetPixelV(infoPtr->dc, x, y, infoPtr->color);
    }
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
