/*
 * tkWayland3d.c --
 *
 *	This file contains the platform specific routines for drawing 3d
 *	borders in the Motif style.
 *
 * Copyright © 1996 Sun Microsystems Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tk3d.h"
#include "nanovg.h"
#include "tkGlfwInt.h"

/*
 * This structure is used to keep track of the extra colors used by Unix 3D
 * borders. Now also includes NanoVG context.
 */

typedef struct {
    TkBorder info;
    NVGcolor solidColor;	/* Used to draw solid relief. */
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
    
    /* Initialize solidColor to black */
    borderPtr->solidColor = nvgRGB(0, 0, 0);
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
 *	This procedure draws a vertical bevel along one side of an object. The
 *	bevel is always rectangular in shape:
 *			|||
 *			|||
 *			|||
 *			|||
 *			|||
 *			|||
 *	An appropriate shadow color is chosen for the bevel based on the
 *	leftBevel and relief arguments. Normally this procedure is called
 *	first, then Tk_3DHorizontalBevel is called next to draw neat corners.
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
     Drawable drawable,        /* X window or pixmap in which to draw. */
     Tk_3DBorder border,        /* Token for border to draw. */
     int x, int y, int width, int height,
                 /* Area of vertical bevel. */
     int leftBevel,        /* Non-zero means this bevel forms the left
                  * side of the object; 0 means it forms the
                  * right side. */
     int relief)            /* Kind of bevel to draw. For example,
                  * TK_RELIEF_RAISED means interior of object
                  * should appear higher than exterior. */
{
     TkBorder *borderPtr = (TkBorder *) border;
     NVGcolor leftColor, rightColor;
     WaylandBorder *waylandBorderPtr = (WaylandBorder *) borderPtr;
     TkWaylandDrawingContext dc;
     Display *display = Tk_Display(tkwin);
     GC gc;

     if ((borderPtr->lightGC == NULL) && (relief != TK_RELIEF_FLAT)) {
     TkpGetShadows(borderPtr, tkwin);
     }

     /* Use the background GC for drawing */
     gc = borderPtr->bgGC;

     LastKnownRequestProcessed(display)++;

     if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
         return;  /* void function - just return */
     }

     /* Convert X colors to NVG colors. */
     NVGcolor bgColor = TkGlfwXColorToNVG(borderPtr->bgColorPtr);
     NVGcolor lightColor = (borderPtr->lightColorPtr) ?
     TkGlfwXColorToNVG(borderPtr->lightColorPtr) : bgColor;
     NVGcolor darkColor = (borderPtr->darkColorPtr) ?
     TkGlfwXColorToNVG(borderPtr->darkColorPtr) : bgColor;

     switch (relief) {
     case TK_RELIEF_RAISED:
         nvgBeginPath(dc.vg);
         nvgRect(dc.vg, x, y, width, height);
         nvgFillColor(dc.vg, leftBevel ? lightColor : darkColor);
         nvgFill(dc.vg);
         break;

     case TK_RELIEF_SUNKEN:
         nvgBeginPath(dc.vg);
         nvgRect(dc.vg, x, y, width, height);
         nvgFillColor(dc.vg, leftBevel ? darkColor : lightColor);
         nvgFill(dc.vg);
         break;

     case TK_RELIEF_RIDGE:
         leftColor = lightColor;
         rightColor = darkColor;
         goto ridgeGroove;

     case TK_RELIEF_GROOVE:
         leftColor = darkColor;
         rightColor = lightColor;
     ridgeGroove: {
         int half = width/2;
         if (!leftBevel && (width & 1)) {
         half++;
         }
         nvgBeginPath(dc.vg);  /* Fixed: was 'vg' */
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
         nvgFillColor(dc.vg, bgColor);
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
 *	The bevel has mitered corners (depending on leftIn and rightIn
 *	arguments).
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
     Drawable drawable,        /* X window or pixmap in which to draw. */
     Tk_3DBorder border,        /* Token for border to draw. */
     int x, int y, int width, int height,
                 /* Bounding box of area of bevel. Height gives
                  * width of border. */
     int leftIn, int rightIn,    /* Describes whether the left and right
edges
                  * of the bevel angle in or out as they go
                  * down. For example, if "leftIn" is true, the
                  * left side of the bevel looks like this:
                  *    ___________
                  *     __________
                  *      _________
                  *       ________
                  */
     int topBevel,        /* Non-zero means this bevel forms the top
                  * side of the object; 0 means it forms the
                  * bottom side. */
     int relief)            /* Kind of bevel to draw. For example,
                  * TK_RELIEF_RAISED means interior of object
                  * should appear higher than exterior. */
{
     TkBorder *borderPtr = (TkBorder *) border;
     WaylandBorder *waylandBorderPtr = (WaylandBorder *) borderPtr;
     NVGcolor topColor, bottomColor;
     int bottom, halfway, x1, x2, x1Delta, x2Delta;
     TkWaylandDrawingContext dc;
     Display *display = Tk_Display(tkwin);
     GC gc;

     if ((borderPtr->lightGC == NULL) && (relief != TK_RELIEF_FLAT) &&
         (relief != TK_RELIEF_SOLID)) {
     TkpGetShadows(borderPtr, tkwin);
     }

     /* Use the background GC for drawing */
     gc = borderPtr->bgGC;

     LastKnownRequestProcessed(display)++;

     if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
         return;  /* void function - just return */
     }

     /* Convert X colors to NVG colors. */
     NVGcolor bgColor = TkGlfwXColorToNVG(borderPtr->bgColorPtr);
     NVGcolor lightColor = (borderPtr->lightColorPtr) ?
TkGlfwXColorToNVG(borderPtr->lightColorPtr) : bgColor;
     NVGcolor darkColor = (borderPtr->darkColorPtr) ?
  TkGlfwXColorToNVG(borderPtr->darkColorPtr) : bgColor;

     /*
      * Compute a color for the top half of the bevel and a color for
the bottom half
      * (they're the same in many cases).
      */
     switch (relief) {
     case TK_RELIEF_FLAT:
         topColor = bottomColor = bgColor;
         break;
     case TK_RELIEF_GROOVE:
         topColor = darkColor;
         bottomColor = lightColor;
         break;
     case TK_RELIEF_RAISED:
         topColor = bottomColor = topBevel ? lightColor : darkColor;
         break;
     case TK_RELIEF_RIDGE:
         topColor = lightColor;
         bottomColor = darkColor;
         break;
     case TK_RELIEF_SOLID:
         nvgBeginPath(dc.vg);  /* Fixed: was 'vg' */
         nvgRect(dc.vg, x, y, width, height);
         nvgFillColor(dc.vg, waylandBorderPtr->solidColor);
         nvgFill(dc.vg);
         TkGlfwEndDraw(&dc);
         return;
     case TK_RELIEF_SUNKEN:
         topColor = bottomColor = topBevel ? darkColor : lightColor;
         break;
     default:
         topColor = bottomColor = bgColor;
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
         nvgRect(dc.vg, x1, y, x2 - x1, 1);  /* Fixed: was 'vg' */
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
 *	This procedure computes the shadow colors for a 3-D border and fills
 *	in the corresponding fields of the Border structure. It's called
 *	lazily, so that the colors aren't allocated until something is
 *	actually drawn with them. That way, if a border is only used for flat
 *	backgrounds the shadow colors will never be allocated.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The lightGC and darkGC fields in borderPtr get filled in, if they
 *	weren't already.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetShadows(
    TkBorder *borderPtr,	/* Information about border. */
    Tk_Window tkwin)		/* Window where border will be used for
				 * drawing. */
{
    /* For NanoVG, we need to create colors rather than GCs. */
    XColor lightColor, darkColor;
    int stressed, tmp1, tmp2;
    int r, g, b;

    if (borderPtr->lightGC != NULL) {
        return;
    }
    
    stressed = TkpCmapStressed(tkwin, borderPtr->colormap);

    /*
     * First, handle the case of a color display with lots of colors. The
     * shadow colors get computed using whichever formula results in the
     * greatest change in color:
     * 1. Lighter shadow is half-way to white, darker shadow is half way to
     *    dark.
     * 2. Lighter shadow is 40% brighter than background, darker shadow is 40%
     *    darker than background.
     */

    if (!stressed && (Tk_Depth(tkwin) >= 6)) {
        /*
         * Compute the dark shadow color.
         */

        r = (int) borderPtr->bgColorPtr->red;
        g = (int) borderPtr->bgColorPtr->green;
        b = (int) borderPtr->bgColorPtr->blue;

        /* For NanoVG, we'll use a simplified approach. */
        float darkFactor = 0.6f;  /* 40% darker */
        float lightFactor = 1.4f; /* 40% brighter */

        /* Create dark color. */
        darkColor.red = (unsigned short)(r * darkFactor);
        darkColor.green = (unsigned short)(g * darkFactor);
        darkColor.blue = (unsigned short)(b * darkFactor);

        /* Create light color. */
        int result;

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

        /*
         * Allocate the shadow colors - for NanoVG we store them directly.
         */
        borderPtr->darkColorPtr = Tk_GetColorByValue(tkwin, &darkColor);
        borderPtr->lightColorPtr = Tk_GetColorByValue(tkwin, &lightColor);
        
        /* For NanoVG, we don't need GCs, but we keep the fields for compatibility. */
        borderPtr->darkGC = (GC)1;  /* Dummy non-NULL value */
        borderPtr->lightGC = (GC)1; /* Dummy non-NULL value */
        
        return;
    }

    /* For simpler displays, use stippled patterns. */
    if (borderPtr->shadow == None) {
        borderPtr->shadow = Tk_GetBitmap(NULL, tkwin, "gray50");
        if (borderPtr->shadow == None) {
            Tcl_Panic("TkpGetShadows couldn't allocate bitmap for border");
        }
    }
    
    /* For NanoVG/Wayland, we'll use simple color variations even for monochrome. */
    r = (int) borderPtr->bgColorPtr->red;
    g = (int) borderPtr->bgColorPtr->green;
    b = (int) borderPtr->bgColorPtr->blue;
    
    /* Dark shadow - 30% darker. */
    darkColor.red = (unsigned short)(r * 0.7f);
    darkColor.green = (unsigned short)(g * 0.7f);
    darkColor.blue = (unsigned short)(b * 0.7f);
    
	/* Light shadow - 30% lighter. */
	int tmp;

	/* Light shadow - 30% lighter. */
	tmp = (int)(r * 1.3f);
	if (tmp > MAX_INTENSITY) tmp = MAX_INTENSITY;
	lightColor.red = (unsigned short)tmp;
	
	tmp = (int)(g * 1.3f);
	if (tmp > MAX_INTENSITY) tmp = MAX_INTENSITY;
	lightColor.green = (unsigned short)tmp;
	
	tmp = (int)(b * 1.3f);
	if (tmp > MAX_INTENSITY) tmp = MAX_INTENSITY;
	lightColor.blue = (unsigned short)tmp;

    borderPtr->darkColorPtr = Tk_GetColorByValue(tkwin, &darkColor);
    borderPtr->lightColorPtr = Tk_GetColorByValue(tkwin, &lightColor);
    
    /* Dummy GC values for compatibility. */
    borderPtr->darkGC = (GC)1;
    borderPtr->lightGC = (GC)1;
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
