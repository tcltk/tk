/*
 * tkWayland3D.c --
 *
 *	This file contains the platform specific routines for drawing 3d
 *	borders in the Motif style for Wayland/GLFW/NanoVG.
 *
 * Copyright © 1996 Sun Microsystems Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tk3d.h"
#include "tkGlfwInt.h"
#include <GLES2/gl2.h>
#include "nanovg.h"

/*
 * This structure is used to keep track of the extra colors used by Wayland 3D
 * borders. Now includes NanoVG context and color storage.
 */

typedef struct {
    TkBorder info;
    NVGcolor solidColor;	/* Used to draw solid relief. */
    NVGcolor lightColor;        /* Cached light shadow color */
    NVGcolor darkColor;         /* Cached dark shadow color */
    NVGcolor bgColor;           /* Cached background color */
} WaylandBorder;

/*
 *----------------------------------------------------------------------
 *
 * TkpGetBorder --
 *
 *	This function allocates a new TkBorder structure.
 *
 * Results:
 *	Returns a newly allocated TkBorder.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkBorder *
TkpGetBorder(void)
{
    WaylandBorder *borderPtr = (WaylandBorder *)Tcl_Alloc(sizeof(WaylandBorder));

    /* Initialize colors to black/white defaults. */
    borderPtr->solidColor = nvgRGB(0, 0, 0);
    borderPtr->lightColor = nvgRGB(255, 255, 255);
    borderPtr->darkColor  = nvgRGB(128, 128, 128);
    borderPtr->bgColor    = nvgRGB(192, 192, 192);

    return (TkBorder *) borderPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpFreeBorder --
 *
 *	This function frees any colors allocated by the platform specific part
 *	of this module.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May deallocate some colors.
 *
 *----------------------------------------------------------------------
 */

void
TkpFreeBorder(
    TCL_UNUSED(TkBorder *))
{
    /*
     * NanoVG colors are just structs, no explicit freeing needed.
     * This function kept for compatibility.
     */
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_3DVerticalBevel --
 *
 *	This procedure draws a vertical bevel along one side of an object.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Graphics are drawn in drawable.
 *
 *----------------------------------------------------------------------
 */

void
Tk_3DVerticalBevel(
    Tk_Window      tkwin,
    Drawable       drawable,
    Tk_3DBorder    border,
    int x, int y, int width, int height,
    int            leftBevel,
    int            relief)
{
    TkBorder               *borderPtr      = (TkBorder *) border;
    WaylandBorder          *waylandBorderPtr = (WaylandBorder *) borderPtr;
    TkWaylandDrawingContext dc;
    NVGcolor                leftColor, rightColor;

    if ((borderPtr->lightGC == NULL) && (relief != TK_RELIEF_FLAT)) {
        TkpGetShadows(borderPtr, tkwin);
    }

    /* Sync cached NVG colors from XColor pointers. */
    if (borderPtr->bgColorPtr) {
        waylandBorderPtr->bgColor = TkGlfwXColorToNVG(borderPtr->bgColorPtr);
    }
    if (borderPtr->lightColorPtr) {
        waylandBorderPtr->lightColor = TkGlfwXColorToNVG(borderPtr->lightColorPtr);
    }
    if (borderPtr->darkColorPtr) {
        waylandBorderPtr->darkColor = TkGlfwXColorToNVG(borderPtr->darkColorPtr);
    }

    int rc = TkGlfwBeginDraw(drawable, borderPtr->bgGC, &dc);
    if (rc != TCL_OK)
        return;

    switch (relief) {
    case TK_RELIEF_RAISED:
        nvgBeginPath(dc.vg);
        nvgRect(dc.vg, x, y, width, height);
        nvgFillColor(dc.vg, leftBevel ? waylandBorderPtr->lightColor :
                     waylandBorderPtr->darkColor);
        nvgFill(dc.vg);
        break;

    case TK_RELIEF_SUNKEN:
        nvgBeginPath(dc.vg);
        nvgRect(dc.vg, x, y, width, height);
        nvgFillColor(dc.vg, leftBevel ? waylandBorderPtr->darkColor :
                     waylandBorderPtr->lightColor);
        nvgFill(dc.vg);
        break;

    case TK_RELIEF_RIDGE:
        leftColor  = waylandBorderPtr->lightColor;
        rightColor = waylandBorderPtr->darkColor;
        goto ridgeGroove;

    case TK_RELIEF_GROOVE:
        leftColor  = waylandBorderPtr->darkColor;
        rightColor = waylandBorderPtr->lightColor;
    ridgeGroove: {
            int half = width / 2;
            if (!leftBevel && (width & 1)) {
                half++;
            }
            nvgBeginPath(dc.vg);
            nvgRect(dc.vg, x, y, half, height);
            nvgFillColor(dc.vg, leftColor);
            nvgFill(dc.vg);

            nvgBeginPath(dc.vg);
            nvgRect(dc.vg, x + half, y, width - half, height);
            nvgFillColor(dc.vg, rightColor);
            nvgFill(dc.vg);
            break;
        }

    case TK_RELIEF_FLAT:
        nvgBeginPath(dc.vg);
        nvgRect(dc.vg, x, y, width, height);
        nvgFillColor(dc.vg, waylandBorderPtr->bgColor);
        nvgFill(dc.vg);
        break;

    case TK_RELIEF_SOLID:
        nvgBeginPath(dc.vg);
        nvgRect(dc.vg, x, y, width, height);
        nvgFillColor(dc.vg, waylandBorderPtr->solidColor);
        nvgFill(dc.vg);
        break;
    }

    TkGlfwEndDraw(&dc);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_3DHorizontalBevel --
 *
 *	This procedure draws a horizontal bevel along one side of an object.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Graphics are drawn in drawable.
 *
 *----------------------------------------------------------------------
 */

void
Tk_3DHorizontalBevel(
    Tk_Window      tkwin,
    Drawable       drawable,
    Tk_3DBorder    border,
    int x, int y, int width, int height,
    int            leftIn,
    int            rightIn,
    int            topBevel,
    int            relief)
{
    TkBorder               *borderPtr       = (TkBorder *) border;
    WaylandBorder          *waylandBorderPtr = (WaylandBorder *) borderPtr;
    TkWaylandDrawingContext dc;
    NVGcolor                topColor, bottomColor;
    int                     bottom, halfway, x1, x2, x1Delta, x2Delta;

    if ((borderPtr->lightGC == NULL) && (relief != TK_RELIEF_FLAT) &&
        (relief != TK_RELIEF_SOLID)) {
        TkpGetShadows(borderPtr, tkwin);
    }

    /* Sync cached NVG colors from XColor pointers. */
    if (borderPtr->bgColorPtr) {
        waylandBorderPtr->bgColor = TkGlfwXColorToNVG(borderPtr->bgColorPtr);
    }
    if (borderPtr->lightColorPtr) {
        waylandBorderPtr->lightColor = TkGlfwXColorToNVG(borderPtr->lightColorPtr);
    }
    if (borderPtr->darkColorPtr) {
        waylandBorderPtr->darkColor = TkGlfwXColorToNVG(borderPtr->darkColorPtr);
    }

    int rc = TkGlfwBeginDraw(drawable, borderPtr->bgGC, &dc);
    if (rc != TCL_OK)
        return;

    /* Compute top/bottom half colors based on relief. */
    switch (relief) {
    case TK_RELIEF_FLAT:
        topColor = bottomColor = waylandBorderPtr->bgColor;
        break;
    case TK_RELIEF_GROOVE:
        topColor    = waylandBorderPtr->darkColor;
        bottomColor = waylandBorderPtr->lightColor;
        break;
    case TK_RELIEF_RAISED:
        topColor = bottomColor = topBevel ? waylandBorderPtr->lightColor :
            waylandBorderPtr->darkColor;
        break;
    case TK_RELIEF_RIDGE:
        topColor    = waylandBorderPtr->lightColor;
        bottomColor = waylandBorderPtr->darkColor;
        break;
    case TK_RELIEF_SOLID:
        nvgBeginPath(dc.vg);
        nvgRect(dc.vg, x, y, width, height);
        nvgFillColor(dc.vg, waylandBorderPtr->solidColor);
        nvgFill(dc.vg);
        TkGlfwEndDraw(&dc);
        return;
    case TK_RELIEF_SUNKEN:
        topColor = bottomColor = topBevel ? waylandBorderPtr->darkColor :
            waylandBorderPtr->lightColor;
        break;
    default:
        topColor = bottomColor = waylandBorderPtr->bgColor;
        break;
    }

    /* Compute geometry. */
    x1 = x;
    if (!leftIn) {
        x1 += height;
    }
    x2 = x + width;
    if (!rightIn) {
        x2 -= height;
    }
    x1Delta = leftIn  ?  1 : -1;
    x2Delta = rightIn ? -1 :  1;
    halfway = y + height / 2;
    if (!topBevel && (height & 1)) {
        halfway++;
    }
    bottom = y + height;

    /* Draw one scanline per y coordinate. */
    for ( ; y < bottom; y++) {
        NVGcolor currentColor = (y < halfway) ? topColor : bottomColor;

        nvgBeginPath(dc.vg);
        nvgRect(dc.vg, x1, y, x2 - x1, 1);
        nvgFillColor(dc.vg, currentColor);
        nvgFill(dc.vg);

        x1 += x1Delta;
        x2 += x2Delta;
    }

    TkGlfwEndDraw(&dc);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetShadows --
 *
 *	This procedure computes the shadow colors for a 3-D border.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The lightColor and darkColor fields in WaylandBorder get filled in.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetShadows(
    TkBorder  *borderPtr,
    Tk_Window  tkwin)
{
    WaylandBorder *waylandBorderPtr = (WaylandBorder *) borderPtr;
    XColor  lightColor, darkColor;
    int     stressed;
    int     r, g, b;
    float   darkFactor, lightFactor;

    if (borderPtr->lightGC != NULL) {
        return;
    }

    stressed = TkpCmapStressed(tkwin, borderPtr->colormap);

    r = (int) borderPtr->bgColorPtr->red;
    g = (int) borderPtr->bgColorPtr->green;
    b = (int) borderPtr->bgColorPtr->blue;

    if (!stressed && (Tk_Depth(tkwin) >= 6)) {
        darkFactor  = 0.6f;
        lightFactor = 1.4f;
    } else {
        darkFactor  = 0.7f;
        lightFactor = 1.3f;
    }

    /* Dark color. */
    darkColor.red   = (unsigned short)(r * darkFactor);
    darkColor.green = (unsigned short)(g * darkFactor);
    darkColor.blue  = (unsigned short)(b * darkFactor);

    /* Light color. */
    if (!stressed && (Tk_Depth(tkwin) >= 6)) {
        int tmp1, tmp2, result;

        tmp1 = (int)(r * lightFactor);
        if (tmp1 > MAX_INTENSITY) tmp1 = MAX_INTENSITY;
        tmp2 = (MAX_INTENSITY + r) / 2;
        lightColor.red = (unsigned short)((tmp1 > tmp2) ? tmp1 : tmp2);

        tmp1 = (int)(g * lightFactor);
        if (tmp1 > MAX_INTENSITY) tmp1 = MAX_INTENSITY;
        tmp2 = (MAX_INTENSITY + g) / 2;
        lightColor.green = (unsigned short)((tmp1 > tmp2) ? tmp1 : tmp2);

        tmp1 = (int)(b * lightFactor);
        if (tmp1 > MAX_INTENSITY) tmp1 = MAX_INTENSITY;
        tmp2 = (MAX_INTENSITY + b) / 2;
        lightColor.blue = (unsigned short)((tmp1 > tmp2) ? tmp1 : tmp2);
    } else {
        int tmp;

        tmp = (int)(r * lightFactor);
        lightColor.red   = (unsigned short)(tmp > MAX_INTENSITY ? MAX_INTENSITY : tmp);

        tmp = (int)(g * lightFactor);
        lightColor.green = (unsigned short)(tmp > MAX_INTENSITY ? MAX_INTENSITY : tmp);

        tmp = (int)(b * lightFactor);
        lightColor.blue  = (unsigned short)(tmp > MAX_INTENSITY ? MAX_INTENSITY : tmp);
    }

    borderPtr->darkColorPtr  = Tk_GetColorByValue(tkwin, &darkColor);
    borderPtr->lightColorPtr = Tk_GetColorByValue(tkwin, &lightColor);

    /* Cache NVG colors. */
    if (borderPtr->bgColorPtr) {
        waylandBorderPtr->bgColor = TkGlfwXColorToNVG(borderPtr->bgColorPtr);
    }
    if (borderPtr->lightColorPtr) {
        waylandBorderPtr->lightColor = TkGlfwXColorToNVG(borderPtr->lightColorPtr);
    }
    if (borderPtr->darkColorPtr) {
        waylandBorderPtr->darkColor = TkGlfwXColorToNVG(borderPtr->darkColorPtr);
    }

    /* Dummy non-NULL values — NanoVG doesn't use GCs but Tk checks these. */
    borderPtr->darkGC  = (GC)1;
    borderPtr->lightGC = (GC)1;

    if (stressed && borderPtr->shadow == None) {
        borderPtr->shadow = Tk_GetBitmap(NULL, tkwin, "gray50");
        if (borderPtr->shadow == None) {
            Tcl_Panic("TkpGetShadows couldn't allocate bitmap for border");
        }
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */