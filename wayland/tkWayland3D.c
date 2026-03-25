/*
 * tkWayland3D.c --
 *
 *	This file contains the platform specific routines for drawing 3d
 *	borders in the Motif style for Wayland/GLFW/libcg.
 *
 * Copyright © 1996 Sun Microsystems Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tk3d.h"
#include "tkWaylandInt.h"

/*
 * This structure keeps track of the extra colors used by Wayland 3D borders.
 */

typedef struct {
    TkBorder          info;
    struct cg_color_t solidColor;   /* Used to draw solid relief. */
    struct cg_color_t lightColor;   /* Cached light shadow color. */
    struct cg_color_t darkColor;    /* Cached dark shadow color. */
    struct cg_color_t bgColor;      /* Cached background color. */
} WaylandBorder;

/* Convenience: initialise a cg_color_t from 0-255 RGB components. */
static inline struct cg_color_t
cgRGB(int r, int g, int b)
{
    struct cg_color_t c;
    c.r = r / 255.0;
    c.g = g / 255.0;
    c.b = b / 255.0;
    c.a = 1.0;
    return c;
}

extern bool TkpCmapStressed(Tk_Window tkwin, Colormap  colormap);

/*
 *----------------------------------------------------------------------
 *
 * TkpGetBorder --
 *
 *	Allocate a new TkBorder structure.
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
    WaylandBorder *borderPtr =
        (WaylandBorder *)Tcl_Alloc(sizeof(WaylandBorder));

    borderPtr->solidColor = cgRGB(0,   0,   0);
    borderPtr->lightColor = cgRGB(255, 255, 255);
    borderPtr->darkColor  = cgRGB(128, 128, 128);
    borderPtr->bgColor    = cgRGB(192, 192, 192);

    return (TkBorder *)borderPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpFreeBorder --
 *
 *	Free any colors allocated by the platform-specific part of this
 *	module.  cg_color_t values are plain structs; nothing to free.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkpFreeBorder(
    TCL_UNUSED(TkBorder *))
{
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_3DVerticalBevel --
 *
 *	Draw a vertical bevel along one side of an object.
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
    Tk_Window   tkwin,
    Drawable    drawable,
    Tk_3DBorder border,
    int x, int y, int width, int height,
    int         leftBevel,
    int         relief)
{
    TkBorder               *borderPtr       = (TkBorder *)border;
    WaylandBorder          *waylandBorderPtr = (WaylandBorder *)borderPtr;
    TkWaylandDrawingContext dc;
    struct cg_color_t       leftColor, rightColor;

    if ((borderPtr->lightGC == NULL) && (relief != TK_RELIEF_FLAT)) {
        TkpGetShadows(borderPtr, tkwin);
    }

    /* Sync cached colors from XColor pointers. */
    if (borderPtr->bgColorPtr)
        waylandBorderPtr->bgColor    = TkGlfwXColorToCG(borderPtr->bgColorPtr);
    if (borderPtr->lightColorPtr)
        waylandBorderPtr->lightColor = TkGlfwXColorToCG(borderPtr->lightColorPtr);
    if (borderPtr->darkColorPtr)
        waylandBorderPtr->darkColor  = TkGlfwXColorToCG(borderPtr->darkColorPtr);

    if (TkGlfwBeginDraw(drawable, borderPtr->bgGC, &dc) != TCL_OK)
        return;

    switch (relief) {
    case TK_RELIEF_RAISED:
        cg_set_source_rgba(dc.cg,
            leftBevel ? waylandBorderPtr->lightColor.r
                      : waylandBorderPtr->darkColor.r,
            leftBevel ? waylandBorderPtr->lightColor.g
                      : waylandBorderPtr->darkColor.g,
            leftBevel ? waylandBorderPtr->lightColor.b
                      : waylandBorderPtr->darkColor.b,
            1.0);
        cg_rectangle(dc.cg, x, y, width, height);
        cg_fill(dc.cg);
        break;

    case TK_RELIEF_SUNKEN:
        cg_set_source_rgba(dc.cg,
            leftBevel ? waylandBorderPtr->darkColor.r
                      : waylandBorderPtr->lightColor.r,
            leftBevel ? waylandBorderPtr->darkColor.g
                      : waylandBorderPtr->lightColor.g,
            leftBevel ? waylandBorderPtr->darkColor.b
                      : waylandBorderPtr->lightColor.b,
            1.0);
        cg_rectangle(dc.cg, x, y, width, height);
        cg_fill(dc.cg);
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
            if (!leftBevel && (width & 1))
                half++;

            cg_set_source_rgba(dc.cg,
                leftColor.r, leftColor.g, leftColor.b, 1.0);
            cg_rectangle(dc.cg, x, y, half, height);
            cg_fill(dc.cg);

            cg_set_source_rgba(dc.cg,
                rightColor.r, rightColor.g, rightColor.b, 1.0);
            cg_rectangle(dc.cg, x + half, y, width - half, height);
            cg_fill(dc.cg);
            break;
        }

    case TK_RELIEF_FLAT:
        cg_set_source_rgba(dc.cg,
            waylandBorderPtr->bgColor.r,
            waylandBorderPtr->bgColor.g,
            waylandBorderPtr->bgColor.b, 1.0);
        cg_rectangle(dc.cg, x, y, width, height);
        cg_fill(dc.cg);
        break;

    case TK_RELIEF_SOLID:
        cg_set_source_rgba(dc.cg,
            waylandBorderPtr->solidColor.r,
            waylandBorderPtr->solidColor.g,
            waylandBorderPtr->solidColor.b, 1.0);
        cg_rectangle(dc.cg, x, y, width, height);
        cg_fill(dc.cg);
        break;
    }

    TkGlfwEndDraw(&dc);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_3DHorizontalBevel --
 *
 *	Draw a horizontal bevel along one side of an object.
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
    Tk_Window   tkwin,
    Drawable    drawable,
    Tk_3DBorder border,
    int x, int y, int width, int height,
    int         leftIn,
    int         rightIn,
    int         topBevel,
    int         relief)
{
    TkBorder               *borderPtr       = (TkBorder *)border;
    WaylandBorder          *waylandBorderPtr = (WaylandBorder *)borderPtr;
    TkWaylandDrawingContext dc;
    struct cg_color_t       topColor, bottomColor;
    int                     bottom, halfway, x1, x2, x1Delta, x2Delta;

    if ((borderPtr->lightGC == NULL) && (relief != TK_RELIEF_FLAT) &&
        (relief != TK_RELIEF_SOLID)) {
        TkpGetShadows(borderPtr, tkwin);
    }

    /* Sync cached colors from XColor pointers. */
    if (borderPtr->bgColorPtr)
        waylandBorderPtr->bgColor    = TkGlfwXColorToCG(borderPtr->bgColorPtr);
    if (borderPtr->lightColorPtr)
        waylandBorderPtr->lightColor = TkGlfwXColorToCG(borderPtr->lightColorPtr);
    if (borderPtr->darkColorPtr)
        waylandBorderPtr->darkColor  = TkGlfwXColorToCG(borderPtr->darkColorPtr);

    if (TkGlfwBeginDraw(drawable, borderPtr->bgGC, &dc) != TCL_OK)
        return;

    /* Determine top/bottom half colors based on relief. */
    switch (relief) {
    case TK_RELIEF_FLAT:
        topColor = bottomColor = waylandBorderPtr->bgColor;
        break;
    case TK_RELIEF_GROOVE:
        topColor    = waylandBorderPtr->darkColor;
        bottomColor = waylandBorderPtr->lightColor;
        break;
    case TK_RELIEF_RAISED:
        topColor = bottomColor = topBevel
            ? waylandBorderPtr->lightColor
            : waylandBorderPtr->darkColor;
        break;
    case TK_RELIEF_RIDGE:
        topColor    = waylandBorderPtr->lightColor;
        bottomColor = waylandBorderPtr->darkColor;
        break;
    case TK_RELIEF_SOLID:
        cg_set_source_rgba(dc.cg,
            waylandBorderPtr->solidColor.r,
            waylandBorderPtr->solidColor.g,
            waylandBorderPtr->solidColor.b, 1.0);
        cg_rectangle(dc.cg, x, y, width, height);
        cg_fill(dc.cg);
        TkGlfwEndDraw(&dc);
        return;
    case TK_RELIEF_SUNKEN:
        topColor = bottomColor = topBevel
            ? waylandBorderPtr->darkColor
            : waylandBorderPtr->lightColor;
        break;
    default:
        topColor = bottomColor = waylandBorderPtr->bgColor;
        break;
    }

    /* Compute geometry. */
    x1      = x + (leftIn  ? 0 : height);
    x2      = x + width - (rightIn ? 0 : height);
    x1Delta = leftIn  ?  1 : -1;
    x2Delta = rightIn ? -1 :  1;
    halfway = y + height / 2;
    if (!topBevel && (height & 1))
        halfway++;
    bottom  = y + height;

    /* Draw one scanline per y coordinate. */
    for ( ; y < bottom; y++) {
        struct cg_color_t *cur = (y < halfway) ? &topColor : &bottomColor;
        cg_set_source_rgba(dc.cg, cur->r, cur->g, cur->b, 1.0);
        cg_rectangle(dc.cg, (double)x1, (double)y,
                     (double)(x2 - x1), 1.0);
        cg_fill(dc.cg);
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
 *	Compute the shadow colors for a 3-D border.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	lightColor, darkColor and bgColor in WaylandBorder are filled in.
 *	borderPtr->lightColorPtr and borderPtr->darkColorPtr are set.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetShadows(
    TkBorder  *borderPtr,
    Tk_Window  tkwin)
{
    WaylandBorder *waylandBorderPtr = (WaylandBorder *)borderPtr;
    XColor  lightColor, darkColor;
    int     stressed;
    int     r, g, b;
    double  darkFactor, lightFactor;

    if (borderPtr->lightGC != NULL)
        return;

    stressed = TkpCmapStressed(tkwin, borderPtr->colormap);

    r = (int)borderPtr->bgColorPtr->red;
    g = (int)borderPtr->bgColorPtr->green;
    b = (int)borderPtr->bgColorPtr->blue;

    if (!stressed && (Tk_Depth(tkwin) >= 6)) {
        darkFactor  = 0.6;
        lightFactor = 1.4;
    } else {
        darkFactor  = 0.7;
        lightFactor = 1.3;
    }

    /* Dark color. */
    darkColor.red   = (unsigned short)(r * darkFactor);
    darkColor.green = (unsigned short)(g * darkFactor);
    darkColor.blue  = (unsigned short)(b * darkFactor);

    /* Light color. */
    if (!stressed && (Tk_Depth(tkwin) >= 6)) {
        int tmp1, tmp2;

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
        lightColor.red   = (unsigned short)(tmp > MAX_INTENSITY
                                             ? MAX_INTENSITY : tmp);
        tmp = (int)(g * lightFactor);
        lightColor.green = (unsigned short)(tmp > MAX_INTENSITY
                                             ? MAX_INTENSITY : tmp);
        tmp = (int)(b * lightFactor);
        lightColor.blue  = (unsigned short)(tmp > MAX_INTENSITY
                                             ? MAX_INTENSITY : tmp);
    }

    borderPtr->darkColorPtr  = Tk_GetColorByValue(tkwin, &darkColor);
    borderPtr->lightColorPtr = Tk_GetColorByValue(tkwin, &lightColor);

    /* Cache cg colors. */
    if (borderPtr->bgColorPtr)
        waylandBorderPtr->bgColor    = TkGlfwXColorToCG(borderPtr->bgColorPtr);
    if (borderPtr->lightColorPtr)
        waylandBorderPtr->lightColor = TkGlfwXColorToCG(borderPtr->lightColorPtr);
    if (borderPtr->darkColorPtr)
        waylandBorderPtr->darkColor  = TkGlfwXColorToCG(borderPtr->darkColorPtr);

    /* Dummy non-NULL values — cg doesn't use GCs but Tk checks these. */
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
