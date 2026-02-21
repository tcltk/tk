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
#include <GLES3/gl3.h>
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
    borderPtr->darkColor = nvgRGB(128, 128, 128);
    borderPtr->bgColor = nvgRGB(192, 192, 192);
    
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
    TCL_UNUSED(TkBorder *))  /* borderPtr */
{
    /* 
     * NanoVG colors are just structs, no explicit freeing needed.
     * This function kept for compatibility.
     */
}

/*
 *--------------------------------------------------------------
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
 *--------------------------------------------------------------
 */

void
Tk_3DVerticalBevel(
    Tk_Window tkwin,        /* Window for which border was allocated. */
    Drawable drawable,      /* X window or pixmap in which to draw. */
    Tk_3DBorder border,     /* Token for border to draw. */
    int x, int y, int width, int height,
    int leftBevel,          /* Non-zero means this bevel forms the left side */
    int relief)             /* Kind of bevel to draw */
{
    TkBorder *borderPtr = (TkBorder *) border;
    WaylandBorder *waylandBorderPtr = (WaylandBorder *) borderPtr;
    NVGcolor leftColor, rightColor;
    TkWaylandDrawingContext dc;
    GC gc;

    if ((borderPtr->lightGC == NULL) && (relief != TK_RELIEF_FLAT)) {
        TkpGetShadows(borderPtr, tkwin);
    }

    /* Use the background GC for drawing. */
    gc = borderPtr->bgGC;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return;
    }

    /* Convert X colors to NVG colors if needed, or use cached values. */
    if (borderPtr->bgColorPtr) {
        waylandBorderPtr->bgColor = TkGlfwXColorToNVG(borderPtr->bgColorPtr);
    }
    if (borderPtr->lightColorPtr) {
        waylandBorderPtr->lightColor = TkGlfwXColorToNVG(borderPtr->lightColorPtr);
    }
    if (borderPtr->darkColorPtr) {
        waylandBorderPtr->darkColor = TkGlfwXColorToNVG(borderPtr->darkColorPtr);
    }

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
        leftColor = waylandBorderPtr->lightColor;
        rightColor = waylandBorderPtr->darkColor;
        goto ridgeGroove;

    case TK_RELIEF_GROOVE:
        leftColor = waylandBorderPtr->darkColor;
        rightColor = waylandBorderPtr->lightColor;
    ridgeGroove: {
        int half = width/2;
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
 *--------------------------------------------------------------
 *
 * Tk_3DHorizontalBevel --
 *
 *	This procedure draws a horizontal bevel along one side of an object.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

void
Tk_3DHorizontalBevel(
    Tk_Window tkwin,        /* Window for which border was allocated. */
    Drawable drawable,      /* X window or pixmap in which to draw. */
    Tk_3DBorder border,     /* Token for border to draw. */
    int x, int y, int width, int height,
    int leftIn, int rightIn,/* Whether left/right edges angle in/out */
    int topBevel,           /* Non-zero means this bevel forms the top side */
    int relief)             /* Kind of bevel to draw */
{
    TkBorder *borderPtr = (TkBorder *) border;
    WaylandBorder *waylandBorderPtr = (WaylandBorder *) borderPtr;
    NVGcolor topColor, bottomColor;
    int bottom, halfway, x1, x2, x1Delta, x2Delta;
    TkWaylandDrawingContext dc;
    GC gc;

    if ((borderPtr->lightGC == NULL) && (relief != TK_RELIEF_FLAT) &&
        (relief != TK_RELIEF_SOLID)) {
        TkpGetShadows(borderPtr, tkwin);
    }

    /* Use the background GC for drawing. */
    gc = borderPtr->bgGC;

    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        return;
    }

    /* Convert X colors to NVG colors if needed, or use cached values. */
    if (borderPtr->bgColorPtr) {
        waylandBorderPtr->bgColor = TkGlfwXColorToNVG(borderPtr->bgColorPtr);
    }
    if (borderPtr->lightColorPtr) {
        waylandBorderPtr->lightColor = TkGlfwXColorToNVG(borderPtr->lightColorPtr);
    }
    if (borderPtr->darkColorPtr) {
        waylandBorderPtr->darkColor = TkGlfwXColorToNVG(borderPtr->darkColorPtr);
    }

    /*
     * Compute a color for the top half of the bevel and a color 
     * for the bottom half (they're the same in many cases).
     */
    switch (relief) {
    case TK_RELIEF_FLAT:
        topColor = bottomColor = waylandBorderPtr->bgColor;
        break;
    case TK_RELIEF_GROOVE:
        topColor = waylandBorderPtr->darkColor;
        bottomColor = waylandBorderPtr->lightColor;
        break;
    case TK_RELIEF_RAISED:
        topColor = bottomColor = topBevel ? waylandBorderPtr->lightColor : 
                                            waylandBorderPtr->darkColor;
        break;
    case TK_RELIEF_RIDGE:
        topColor = waylandBorderPtr->lightColor;
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

    /*
     * Compute various other geometry-related details.
     */
    x1 = x;
    if (!leftIn) {
        x1 += height;
    }
    x2 = x + width;
    if (!rightIn) {
        x2 -= height;
    }
    x1Delta = leftIn ? 1 : -1;
    x2Delta = rightIn ? -1 : 1;
    halfway = y + height / 2;
    if (!topBevel && (height & 1)) {
        halfway++;
    }
    bottom = y + height;

    /*
     * Draw one line for each y-coordinate covered by the bevel.
     */
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
    TkBorder *borderPtr,	/* Information about border. */
    Tk_Window tkwin)		/* Window where border will be used for
				 * drawing. */
{
    WaylandBorder *waylandBorderPtr = (WaylandBorder *) borderPtr;
    XColor lightColor, darkColor;
    int stressed;
    int r, g, b;
    float darkFactor, lightFactor;

    if (borderPtr->lightGC != NULL) {
        return;
    }
    
    stressed = TkpCmapStressed(tkwin, borderPtr->colormap);

    /*
     * For NanoVG/Wayland, we use simple color variations.
     */
    r = (int) borderPtr->bgColorPtr->red;
    g = (int) borderPtr->bgColorPtr->green;
    b = (int) borderPtr->bgColorPtr->blue;

    if (!stressed && (Tk_Depth(tkwin) >= 6)) {
        /* Rich colors: 40% darker and 40% brighter with half-to-white */
        darkFactor = 0.6f;   /* 40% darker */
        lightFactor = 1.4f;  /* 40% brighter */
    } else {
        /* Limited palette: 30% darker/lighter */
        darkFactor = 0.7f;
        lightFactor = 1.3f;
    }

    /* Create dark color. */
    darkColor.red = (unsigned short)(r * darkFactor);
    darkColor.green = (unsigned short)(g * darkFactor);
    darkColor.blue = (unsigned short)(b * darkFactor);

    /* Create light color with half-to-white blending for rich case. */
    if (!stressed && (Tk_Depth(tkwin) >= 6)) {
        int tmp1, tmp2, result;
        
        /* Red */
        tmp1 = (int)(r * lightFactor);
        if (tmp1 > MAX_INTENSITY) tmp1 = MAX_INTENSITY;
        tmp2 = (MAX_INTENSITY + r) / 2;
        result = (tmp1 > tmp2) ? tmp1 : tmp2;
        lightColor.red = (unsigned short)result;
        
        /* Green */
        tmp1 = (int)(g * lightFactor);
        if (tmp1 > MAX_INTENSITY) tmp1 = MAX_INTENSITY;
        tmp2 = (MAX_INTENSITY + g) / 2;
        result = (tmp1 > tmp2) ? tmp1 : tmp2;
        lightColor.green = (unsigned short)result;
        
        /* Blue */
        tmp1 = (int)(b * lightFactor);
        if (tmp1 > MAX_INTENSITY) tmp1 = MAX_INTENSITY;
        tmp2 = (MAX_INTENSITY + b) / 2;
        result = (tmp1 > tmp2) ? tmp1 : tmp2;
        lightColor.blue = (unsigned short)result;
    } else {
        /* Simple lighter */
        int tmp;
        
        tmp = (int)(r * lightFactor);
        if (tmp > MAX_INTENSITY) tmp = MAX_INTENSITY;
        lightColor.red = (unsigned short)tmp;
        
        tmp = (int)(g * lightFactor);
        if (tmp > MAX_INTENSITY) tmp = MAX_INTENSITY;
        lightColor.green = (unsigned short)tmp;
        
        tmp = (int)(b * lightFactor);
        if (tmp > MAX_INTENSITY) tmp = MAX_INTENSITY;
        lightColor.blue = (unsigned short)tmp;
    }

    /*
     * Allocate the shadow colors using Tk's color allocator.
     */
    borderPtr->darkColorPtr = Tk_GetColorByValue(tkwin, &darkColor);
    borderPtr->lightColorPtr = Tk_GetColorByValue(tkwin, &lightColor);
    
    /* Cache the NVG colors directly in the WaylandBorder struct. */
    if (borderPtr->bgColorPtr) {
        waylandBorderPtr->bgColor = TkGlfwXColorToNVG(borderPtr->bgColorPtr);
    }
    if (borderPtr->lightColorPtr) {
        waylandBorderPtr->lightColor = TkGlfwXColorToNVG(borderPtr->lightColorPtr);
    }
    if (borderPtr->darkColorPtr) {
        waylandBorderPtr->darkColor = TkGlfwXColorToNVG(borderPtr->darkColorPtr);
    }
    
    /* For NanoVG, we don't need GCs, but we keep the fields for compatibility. */
    borderPtr->darkGC = (GC)1;  /* Dummy non-NULL value */
    borderPtr->lightGC = (GC)1; /* Dummy non-NULL value */
    
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