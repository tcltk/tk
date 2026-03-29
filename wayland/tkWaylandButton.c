/*
 * tkWaylandButton.c --
 *
 *      This file implements the Wayland specific portion of the button widgets.
 *
 *  Copyright © 1996-1997 Sun Microsystems, Inc.
 *  Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkButton.h"
#include "tk3d.h"
#include "tkWaylandInt.h"
#include <X11/Xlib.h>

/*
 * Shared with menu widget.
 */
extern void TkpDrawCheckIndicator(Tk_Window tkwin,
    Display *display, Drawable d, int x, int y,
    Tk_3DBorder bgBorder, XColor *indicatorColor,
    XColor *selectColor, XColor *disColor, int on,
    int disabled, int mode);

void ImageChanged(void *clientData,
    int x, int y, int width, int height,
    int imageWidth, int imageHeight);

void TkpButtonWorldChanged(void *instanceData);

/*
 * Class function table.
 */
const Tk_ClassProcs tkpButtonProcs = {
    sizeof(Tk_ClassProcs),
    TkpButtonWorldChanged,
    NULL,
    NULL
};

/*
 * Indicator draw modes.
 */
#define CHECK_BUTTON  0
#define CHECK_MENU    1
#define RADIO_BUTTON  2
#define RADIO_MENU    3

#define CHECK_BUTTON_DIM 16
#define CHECK_MENU_DIM    8
#define RADIO_BUTTON_DIM 16
#define RADIO_MENU_DIM    8
/*
 *----------------------------------------------------------------------
 *
 * ImageChanged --
 *
 *	This function is called when an image used by a button changes.
 *	It schedules a redisplay of the button to reflect the updated image.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If the button is currently mapped, a redisplay is scheduled.
 *
 *----------------------------------------------------------------------
 */

void
ImageChanged(
    void *clientData,           /* The button widget record. */
    TCL_UNUSED(int),            /* x-coordinate (unused) */
    TCL_UNUSED(int),            /* y-coordinate (unused) */
    TCL_UNUSED(int),            /* width (unused) */
    TCL_UNUSED(int),            /* height (unused) */
    TCL_UNUSED(int),            /* imageWidth (unused) */
    TCL_UNUSED(int))            /* imageHeight (unused) */
{
    TkButton *butPtr = (TkButton *)clientData;
    
    /* Only schedule a redraw if the button still exists and is mapped. */
    if (butPtr->tkwin != NULL && Tk_IsMapped(butPtr->tkwin)) {
        /* Mark that a redraw is pending. */
        if (!(butPtr->flags & REDRAW_PENDING)) {
            Tcl_DoWhenIdle(TkpDisplayButton, butPtr);
            butPtr->flags |= REDRAW_PENDING;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpCreateButton --
 *
 *	Allocate a new TkButton structure.
 *
 * Results:
 *	Returns a newly allocated TkButton structure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkButton *
TkpCreateButton(TCL_UNUSED(Tk_Window))
{
    return (TkButton *)ckalloc(sizeof(TkButton));
}

/*
 *----------------------------------------------------------------------
 *
 * ShiftByOffset --
 *
 *	Shift the drawing position based on relief and button type.
 *	This provides the "motif" look for buttons.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies x and y coordinates.
 *
 *----------------------------------------------------------------------
 */

static void
ShiftByOffset(TkButton *butPtr, int relief, int *x, int *y,
              int width, int height)
{
    if (relief != TK_RELIEF_RAISED && butPtr->type == TYPE_BUTTON &&
        !Tk_StrictMotif(butPtr->tkwin)) {
        int shiftX = (relief == TK_RELIEF_SUNKEN) ? 2 : 1;
        int shiftY = shiftX;
        if (relief != TK_RELIEF_RIDGE) {
            if ((Tk_Width(butPtr->tkwin) - width) % 2 == 0)  shiftX--;
            if ((Tk_Height(butPtr->tkwin) - height) % 2 == 0) shiftY--;
        }
        *x += shiftX;
        *y += shiftY;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DrawButtonBitmap --
 *
 *	Draw a Tk bitmap using libcg. Converts the 1-bit bitmap to an
 *	RGBA surface using the GC's foreground color and blits it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the bitmap on the drawing context.
 *
 *----------------------------------------------------------------------
 */

static void
DrawButtonBitmap(TkButton *butPtr,
                 TkWaylandDrawingContext *dc,
                 int x, int y,
                 int width, int height)
{
    Pixmap bitmap = butPtr->bitmap;
    XImage *image = NULL;
    unsigned char *rgba = NULL;
    struct cg_surface_t *imgSurface = NULL;
    unsigned int bm_width, bm_height, border_width, depth;
    int x_hot, y_hot;
    XGCValues gcValues;
    struct cg_color_t fgColor;
    XColor *fgXColor;
    XColor fgColorValue;
    GC currentGC;
    Display *dpy;
    Drawable screen;
    int i, j;

    if (!bitmap) {
        /* Fallback: grey rectangle. */
        cg_set_source_rgba(dc->cg, 0.753, 0.753, 0.753, 1.0);
        cg_rectangle(dc->cg, x, y, width, height);
        cg_fill(dc->cg);
        return;
    }

    dpy    = Tk_Display(butPtr->tkwin);
    screen = Tk_WindowId(butPtr->tkwin);

    if (!XGetGeometry(dpy, bitmap, &screen, &x_hot, &y_hot,
                      &bm_width, &bm_height, &border_width, &depth)) {
        goto fallback_rect;
    }
    if (bm_width != (unsigned int)width || bm_height != (unsigned int)height) {
        goto fallback_rect;
    }

    currentGC = butPtr->normalTextGC;
    if (butPtr->state == STATE_DISABLED && butPtr->disabledFg)
        currentGC = butPtr->disabledGC;
    else if (butPtr->state == STATE_ACTIVE && !Tk_StrictMotif(butPtr->tkwin))
        currentGC = butPtr->activeTextGC;

    if (currentGC) {
        XGetGCValues(butPtr->display, currentGC, GCForeground, &gcValues);
        fgColorValue.pixel = gcValues.foreground;
        fgXColor = Tk_GetColorByValue(butPtr->tkwin, &fgColorValue);
        fgColor  = TkGlfwXColorToCG(fgXColor);
    } else {
        fgColor = TkGlfwXColorToCG(butPtr->normalFg);
    }

    image = XGetImage(dpy, bitmap, 0, 0, bm_width, bm_height, 1, XYPixmap);
    if (!image) goto fallback_rect;

    rgba = (unsigned char *)ckalloc(bm_width * bm_height * 4);
    if (!rgba) goto cleanup;

    for (j = 0; j < (int)bm_height; j++) {
        for (i = 0; i < (int)bm_width; i++) {
            unsigned long pixel = XGetPixel(image, i, j);
            unsigned char *p = &rgba[(j * bm_width + i) * 4];
            if (pixel) {
                p[0] = (unsigned char)(fgColor.r * 255);
                p[1] = (unsigned char)(fgColor.g * 255);
                p[2] = (unsigned char)(fgColor.b * 255);
                p[3] = 255;
            } else {
                p[0] = p[1] = p[2] = p[3] = 0;
            }
        }
    }

    imgSurface = cg_surface_create_for_data((int)bm_width, (int)bm_height, rgba);
    if (imgSurface) {
        cg_set_source_surface(dc->cg, imgSurface, (double)x, (double)y);
        cg_rectangle(dc->cg, (double)x, (double)y,
                     (double)bm_width, (double)bm_height);
        cg_fill(dc->cg);
        cg_surface_destroy(imgSurface);
    }

cleanup:
    if (image) XDestroyImage(image);
    if (rgba)  ckfree(rgba);
    return;

fallback_rect:
    if (image) XDestroyImage(image);
    cg_set_source_rgba(dc->cg, 0.753, 0.753, 0.753, 1.0);
    cg_rectangle(dc->cg, x, y, width, height);
    cg_fill(dc->cg);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawButtonImage --
 *
 *	Draw the button's image (either from a Tk image or a bitmap).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the image on the drawing context.
 *
 *----------------------------------------------------------------------
 */

static void
DrawButtonImage(TkButton *butPtr, TkWaylandDrawingContext *dc,
                int x, int y, int width, int height, int selected)
{
    if (butPtr->image) {
        if (selected && butPtr->selectImage)
            Tk_RedrawImage(butPtr->selectImage, 0, 0, width, height,
                           (Drawable)dc, x, y);
        else if ((butPtr->flags & TRISTATED) && butPtr->tristateImage)
            Tk_RedrawImage(butPtr->tristateImage, 0, 0, width, height,
                           (Drawable)dc, x, y);
        else
            Tk_RedrawImage(butPtr->image, 0, 0, width, height,
                           (Drawable)dc, x, y);
    } else if (butPtr->bitmap != None) {
        DrawButtonBitmap(butPtr, dc, x, y, width, height);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DrawButtonText --
 *
 *	Draw the button's text label.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders text on the drawing context.
 *
 *----------------------------------------------------------------------
 */

static void
DrawButtonText(TkButton *butPtr, TkWaylandDrawingContext *dc,
               int x, int y)
{
    GC currentGC;

    if (butPtr->state == STATE_DISABLED && butPtr->disabledFg)
        currentGC = butPtr->disabledGC;
    else if (butPtr->state == STATE_ACTIVE && !Tk_StrictMotif(butPtr->tkwin))
        currentGC = butPtr->activeTextGC;
    else
        currentGC = butPtr->normalTextGC;

    TkGlfwApplyGC(dc->cg, currentGC);
    
    Drawable drawable = dc->drawable;

	Tk_DrawTextLayout(butPtr->display, drawable, currentGC,
	                  butPtr->textLayout, x, y, 0, -1);
	
	Tk_UnderlineTextLayout(butPtr->display, drawable, currentGC,
	                       butPtr->textLayout, x, y,
	                       butPtr->underline);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayButton --
 *
 *	This procedure is invoked as an idle handler to redisplay the
 *	contents of a button widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The button gets redisplayed.
 *
 *----------------------------------------------------------------------
 */

void
TkpDisplayButton(void *clientData)
{
    TkButton *butPtr = clientData;
    TkWaylandDrawingContext dc;
    GC currentGC;
    int x = 0, y = 0, relief;
    Tk_Window tkwin = butPtr->tkwin;
    int width = 0, height = 0;
    int fullWidth = 0, fullHeight = 0;
    int textXOffset = 0, textYOffset = 0;
    int haveImage = 0, haveText = 0;
    int imageXOffset = 0, imageYOffset = 0;
    int padX, padY, bd, hl;
    int winWidth, winHeight;
    Drawable drawable;

    butPtr->flags &= ~REDRAW_PENDING;
    if (!tkwin || !Tk_IsMapped(tkwin)) return;

    winWidth  = Tk_Width(tkwin);
    winHeight = Tk_Height(tkwin);

    relief = butPtr->relief;
    if (butPtr->type >= TYPE_CHECK_BUTTON && !butPtr->indicatorOn) {
        if (butPtr->flags & SELECTED)
            relief = TK_RELIEF_SUNKEN;
        else if (butPtr->overRelief != relief)
            relief = butPtr->offRelief;
    }

    currentGC = butPtr->activeTextGC;

#ifdef TK_NO_DOUBLE_BUFFERING
    /* Draw directly to the window. */
    drawable = Tk_WindowId(tkwin);
#else
    /* Use off-screen pixmap for double-buffering. */
    drawable = Tk_GetPixmap(butPtr->display, Tk_WindowId(tkwin),
                            winWidth, winHeight, Tk_Depth(tkwin));
#endif

    if (TkGlfwBeginDraw(drawable, currentGC, &dc) != TCL_OK) {
#ifndef TK_NO_DOUBLE_BUFFERING
        Tk_FreePixmap(butPtr->display, drawable);
#endif
        return;
    }

    Tk_GetPixelsFromObj(NULL, tkwin, butPtr->padXObj, &padX);
    Tk_GetPixelsFromObj(NULL, tkwin, butPtr->padYObj, &padY);
    Tk_GetPixelsFromObj(NULL, tkwin, butPtr->borderWidthObj, &bd);
    Tk_GetPixelsFromObj(NULL, tkwin, butPtr->highlightWidthObj, &hl);

    Tk_Fill3DRectangle(tkwin, (Drawable)&dc, butPtr->normalBorder, 0, 0,
                       winWidth, winHeight, 0, TK_RELIEF_FLAT);

    if (butPtr->image) {
        Tk_SizeOfImage(butPtr->image, &width, &height);
        haveImage = 1;
    } else if (butPtr->bitmap != None) {
        unsigned int bm_width, bm_height, border_width, depth;
        int x_hot, y_hot;
        Drawable screen;
        Display *dpy = Tk_Display(butPtr->tkwin);
        screen = Tk_WindowId(butPtr->tkwin);
        XGetGeometry(dpy, butPtr->bitmap, &screen, &x_hot, &y_hot,
                     &bm_width, &bm_height, &border_width, &depth);
        width  = (int)bm_width;
        height = (int)bm_height;
        haveImage = 1;
    }

    haveText = (butPtr->textWidth > 0 && butPtr->textHeight > 0);

    if (butPtr->compound != COMPOUND_NONE && haveImage && haveText) {
        switch (butPtr->compound) {
        case COMPOUND_TOP:
            textYOffset  = height + padY;
            fullHeight   = height + butPtr->textHeight + padY;
            fullWidth    = (width > butPtr->textWidth) ? width : butPtr->textWidth;
            textXOffset  = (fullWidth - butPtr->textWidth) / 2;
            imageXOffset = (fullWidth - width) / 2;
            break;
        case COMPOUND_BOTTOM:
            imageYOffset = butPtr->textHeight + padY;
            fullHeight   = height + butPtr->textHeight + padY;
            fullWidth    = (width > butPtr->textWidth) ? width : butPtr->textWidth;
            textXOffset  = (fullWidth - butPtr->textWidth) / 2;
            imageXOffset = (fullWidth - width) / 2;
            break;
        case COMPOUND_LEFT:
            textXOffset  = width + padX;
            fullWidth    = butPtr->textWidth + padX + width;
            fullHeight   = (height > butPtr->textHeight) ? height : butPtr->textHeight;
            textYOffset  = (fullHeight - butPtr->textHeight) / 2;
            imageYOffset = (fullHeight - height) / 2;
            break;
        case COMPOUND_RIGHT:
            imageXOffset = butPtr->textWidth + padX;
            fullWidth    = butPtr->textWidth + padX + width;
            fullHeight   = (height > butPtr->textHeight) ? height : butPtr->textHeight;
            textYOffset  = (fullHeight - butPtr->textHeight) / 2;
            imageYOffset = (fullHeight - height) / 2;
            break;
        case COMPOUND_CENTER:
            fullWidth    = (width > butPtr->textWidth) ? width : butPtr->textWidth;
            fullHeight   = (height > butPtr->textHeight) ? height : butPtr->textHeight;
            textXOffset  = (fullWidth - butPtr->textWidth) / 2;
            imageXOffset = (fullWidth - width) / 2;
            textYOffset  = (fullHeight - butPtr->textHeight) / 2;
            imageYOffset = (fullHeight - height) / 2;
            break;
        default: break;
        }
        TkComputeAnchor(butPtr->anchor, tkwin, padX, padY,
                        butPtr->indicatorSpace + fullWidth, fullHeight, &x, &y);
        x += butPtr->indicatorSpace;
        ShiftByOffset(butPtr, relief, &x, &y, width, height);
        DrawButtonImage(butPtr, &dc, x + imageXOffset, y + imageYOffset,
                        width, height, (butPtr->flags & SELECTED));
        DrawButtonText(butPtr, &dc, x + textXOffset, y + textYOffset);
    } else if (haveImage) {
        TkComputeAnchor(butPtr->anchor, tkwin, 0, 0,
                        butPtr->indicatorSpace + width, height, &x, &y);
        x += butPtr->indicatorSpace;
        ShiftByOffset(butPtr, relief, &x, &y, width, height);
        DrawButtonImage(butPtr, &dc, x, y, width, height,
                        (butPtr->flags & SELECTED));
    } else if (haveText) {
        TkComputeAnchor(butPtr->anchor, tkwin, padX, padY,
                        butPtr->indicatorSpace + butPtr->textWidth,
                        butPtr->textHeight, &x, &y);
        x += butPtr->indicatorSpace;
        ShiftByOffset(butPtr, relief, &x, &y,
                      butPtr->textWidth, butPtr->textHeight);
        DrawButtonText(butPtr, &dc, x, y);
    }

    if ((butPtr->type == TYPE_CHECK_BUTTON ||
         butPtr->type == TYPE_RADIO_BUTTON) &&
        butPtr->indicatorOn &&
        butPtr->indicatorDiameter > 2 * bd) {
        TkBorder *selBd  = (TkBorder *)butPtr->selectBorder;
        XColor   *selColor = selBd ? selBd->bgColorPtr : NULL;
        int indType = (butPtr->type == TYPE_CHECK_BUTTON)
                      ? CHECK_BUTTON : RADIO_BUTTON;
        TkpDrawCheckIndicator(tkwin, butPtr->display, (Drawable)&dc,
                              -butPtr->indicatorSpace / 2, winHeight / 2,
                              butPtr->normalBorder, butPtr->normalFg,
                              selColor, butPtr->disabledFg,
                              (butPtr->flags & SELECTED) ? 1 :
                              (butPtr->flags & TRISTATED) ? 2 : 0,
                              butPtr->state == STATE_DISABLED, indType);
    }

    if (relief != TK_RELIEF_FLAT) {
        int inset = hl;
        if (butPtr->defaultState == DEFAULT_ACTIVE) {
            Tk_Draw3DRectangle(tkwin, (Drawable)&dc, butPtr->highlightBorder,
                               inset, inset, winWidth-2*inset, winHeight-2*inset,
                               2, TK_RELIEF_FLAT);
            inset += 2;
            Tk_Draw3DRectangle(tkwin, (Drawable)&dc, butPtr->highlightBorder,
                               inset, inset, winWidth-2*inset, winHeight-2*inset,
                               1, TK_RELIEF_SUNKEN);
            inset += 3;
        } else if (butPtr->defaultState == DEFAULT_NORMAL) {
            Tk_Draw3DRectangle(tkwin, (Drawable)&dc, butPtr->highlightBorder,
                               0, 0, winWidth, winHeight, 5, TK_RELIEF_FLAT);
            inset += 5;
        }
        Tk_Draw3DRectangle(tkwin, (Drawable)&dc, butPtr->normalBorder,
                           inset, inset, winWidth-2*inset, winHeight-2*inset,
                           bd, relief);
    }

    if (hl > 0) {
        if (butPtr->defaultState == DEFAULT_NORMAL)
            TkDrawInsetFocusHighlight(tkwin, butPtr->normalTextGC, hl,
                                      (Drawable)&dc, 5);
        else
            Tk_DrawFocusHighlight(tkwin, butPtr->normalTextGC, hl,
                                  (Drawable)&dc);
    }

    TkGlfwEndDraw(&dc);

#ifndef TK_NO_DOUBLE_BUFFERING
    /* Copy off-screen pixmap to screen and free it. */
    XCopyArea(butPtr->display, drawable, Tk_WindowId(tkwin),
              butPtr->normalTextGC, 0, 0,
              (unsigned)winWidth, (unsigned)winHeight, 0, 0);
    Tk_FreePixmap(butPtr->display, drawable);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * TkpComputeButtonGeometry --
 *
 *	After changes in a button's size or configuration, this procedure
 *	recomputes various geometry information used in displaying the
 *	button.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The button will be displayed differently.
 *
 *----------------------------------------------------------------------
 */

void
TkpComputeButtonGeometry(TkButton *butPtr)
{
    int width, height, avgWidth, txtWidth, txtHeight;
    int haveImage = 0, haveText = 0;
    Tk_FontMetrics fm;
    int padX, padY, borderWidth, highlightWidth, wrapLength;
    int butPtrWidth, butPtrHeight;

    Tk_GetPixelsFromObj(NULL, butPtr->tkwin, butPtr->highlightWidthObj, &highlightWidth);
    Tk_GetPixelsFromObj(NULL, butPtr->tkwin, butPtr->borderWidthObj, &borderWidth);
    Tk_GetPixelsFromObj(NULL, butPtr->tkwin, butPtr->padXObj, &padX);
    Tk_GetPixelsFromObj(NULL, butPtr->tkwin, butPtr->padYObj, &padY);
    Tk_GetPixelsFromObj(NULL, butPtr->tkwin, butPtr->wrapLengthObj, &wrapLength);
    Tk_GetPixelsFromObj(NULL, butPtr->tkwin, butPtr->widthObj, &butPtrWidth);
    Tk_GetPixelsFromObj(NULL, butPtr->tkwin, butPtr->heightObj, &butPtrHeight);

    butPtr->inset = highlightWidth + borderWidth;
    if (butPtr->defaultState != DEFAULT_DISABLED) butPtr->inset += 5;
    butPtr->indicatorSpace = 0;

    width = height = txtWidth = txtHeight = avgWidth = 0;

    if (butPtr->image != NULL) {
        Tk_SizeOfImage(butPtr->image, &width, &height);
        haveImage = 1;
    } else if (butPtr->bitmap != None) {
        unsigned int bm_width, bm_height, border_width, depth;
        int x_hot, y_hot;
        Drawable screen;
        Display *dpy = Tk_Display(butPtr->tkwin);
        screen = Tk_WindowId(butPtr->tkwin);
        XGetGeometry(dpy, butPtr->bitmap, &screen, &x_hot, &y_hot,
                     &bm_width, &bm_height, &border_width, &depth);
        width  = (int)bm_width;
        height = (int)bm_height;
        haveImage = 1;
    }

    if (haveImage == 0 || butPtr->compound != COMPOUND_NONE) {
        Tk_FreeTextLayout(butPtr->textLayout);
        butPtr->textLayout = Tk_ComputeTextLayout(butPtr->tkfont,
                Tcl_GetString(butPtr->textPtr), TCL_INDEX_NONE, wrapLength,
                butPtr->justify, 0, &butPtr->textWidth, &butPtr->textHeight);
        txtWidth  = butPtr->textWidth;
        txtHeight = butPtr->textHeight;
        avgWidth  = Tk_TextWidth(butPtr->tkfont, "0", 1);
        Tk_GetFontMetrics(butPtr->tkfont, &fm);
        haveText = (txtWidth != 0 && txtHeight != 0);
    }

    if (butPtr->compound != COMPOUND_NONE && haveImage && haveText) {
        switch ((enum compound)butPtr->compound) {
        case COMPOUND_TOP: case COMPOUND_BOTTOM:
            height += txtHeight + padY;
            width   = (width > txtWidth) ? width : txtWidth;
            break;
        case COMPOUND_LEFT: case COMPOUND_RIGHT:
            width  += txtWidth + padX;
            height  = (height > txtHeight) ? height : txtHeight;
            break;
        case COMPOUND_CENTER:
            width  = (width > txtWidth)  ? width  : txtWidth;
            height = (height > txtHeight) ? height : txtHeight;
            break;
        case COMPOUND_NONE: break;
        }
        if (butPtrWidth  > 0) width  = butPtrWidth;
        if (butPtrHeight > 0) height = butPtrHeight;
        if ((butPtr->type >= TYPE_CHECK_BUTTON) && butPtr->indicatorOn) {
            butPtr->indicatorSpace    = height;
            butPtr->indicatorDiameter = (butPtr->type == TYPE_CHECK_BUTTON)
                                        ? (65*height)/100 : (75*height)/100;
        }
        width  += 2 * padX;
        height += 2 * padY;
    } else {
        if (haveImage) {
            if (butPtrWidth  > 0) width  = butPtrWidth;
            if (butPtrHeight > 0) height = butPtrHeight;
            if ((butPtr->type >= TYPE_CHECK_BUTTON) && butPtr->indicatorOn) {
                butPtr->indicatorSpace    = height;
                butPtr->indicatorDiameter = (butPtr->type == TYPE_CHECK_BUTTON)
                                            ? (65*height)/100 : (75*height)/100;
            }
        } else {
            width  = txtWidth;
            height = txtHeight;
            if (butPtrWidth  > 0) width  = butPtrWidth  * avgWidth;
            if (butPtrHeight > 0) height = butPtrHeight * fm.linespace;
            if ((butPtr->type >= TYPE_CHECK_BUTTON) && butPtr->indicatorOn) {
                butPtr->indicatorDiameter = fm.linespace;
                butPtr->indicatorSpace    = butPtr->indicatorDiameter + avgWidth;
            }
        }
    }

    if ((butPtr->image == NULL) && (butPtr->bitmap == None)) {
        width  += 2 * padX;
        height += 2 * padY;
    }
    if ((butPtr->type == TYPE_BUTTON) && !Tk_StrictMotif(butPtr->tkwin)) {
        width  += 2;
        height += 2;
    }
    Tk_GeometryRequest(butPtr->tkwin,
        (int)(width  + butPtr->indicatorSpace + 2 * butPtr->inset),
        (int)(height + 2 * butPtr->inset));
    Tk_SetInternalBorder(butPtr->tkwin, butPtr->inset);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpButtonWorldChanged --
 *
 *	This procedure is invoked when the screen has changed (e.g., DPI
 *	scaling) to update GCs and geometry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates GCs, frees cached bitmaps, and schedules a redraw.
 *
 *----------------------------------------------------------------------
 */

void
TkpButtonWorldChanged(void *instanceData)
{
    XGCValues gcValues;
    GC newGC;
    unsigned long mask;
    TkButton *butPtr = (TkButton *)instanceData;

    gcValues.foreground        = butPtr->normalFg->pixel;
    gcValues.background        = Tk_3DBorderColor(butPtr->normalBorder)->pixel;
    gcValues.font              = Tk_FontId(butPtr->tkfont);
    gcValues.graphics_exposures = False;
    mask = GCForeground | GCBackground | GCFont | GCGraphicsExposures;
    newGC = Tk_GetGC(butPtr->tkwin, mask, &gcValues);
    if (butPtr->normalTextGC != NULL) Tk_FreeGC(butPtr->display, butPtr->normalTextGC);
    butPtr->normalTextGC = newGC;

    gcValues.foreground = butPtr->activeFg->pixel;
    gcValues.background = Tk_3DBorderColor(butPtr->activeBorder)->pixel;
    newGC = Tk_GetGC(butPtr->tkwin, mask, &gcValues);
    if (butPtr->activeTextGC != NULL) Tk_FreeGC(butPtr->display, butPtr->activeTextGC);
    butPtr->activeTextGC = newGC;

    if (butPtr->disabledFg != NULL) {
        gcValues.foreground = butPtr->disabledFg->pixel;
        gcValues.background = Tk_3DBorderColor(butPtr->normalBorder)->pixel;
    } else {
        gcValues.foreground = butPtr->normalFg->pixel;
        gcValues.background = Tk_3DBorderColor(butPtr->normalBorder)->pixel;
    }
    newGC = Tk_GetGC(butPtr->tkwin, mask, &gcValues);
    if (butPtr->disabledGC != NULL) Tk_FreeGC(butPtr->display, butPtr->disabledGC);
    butPtr->disabledGC = newGC;

    if (butPtr->gray != None) {
        Tk_FreeBitmap(butPtr->display, butPtr->gray);
        butPtr->gray = None;
    }

    TkpComputeButtonGeometry(butPtr);

    if ((butPtr->tkwin != NULL) && Tk_IsMapped(butPtr->tkwin)
            && !(butPtr->flags & REDRAW_PENDING)) {
        Tcl_DoWhenIdle(TkpDisplayButton, butPtr);
        butPtr->flags |= REDRAW_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDrawCheckIndicator --
 *
 *	Draw check/radio button indicator using libcg.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the indicator on the drawing context.
 *
 *----------------------------------------------------------------------
 */

void
TkpDrawCheckIndicator(
    TCL_UNUSED(Tk_Window),
    TCL_UNUSED(Display *),
    Drawable d,
    int x, int y,
    TCL_UNUSED(Tk_3DBorder),
    XColor *indicatorColor,
    XColor *selectColor,
    XColor *disColor,
    int on,
    int disabled,
    int mode)
{
    TkWaylandDrawingContext *dc = (TkWaylandDrawingContext *)d;
    struct cg_color_t fillColor, markColor;
    int size;

    switch (mode) {
    case CHECK_BUTTON:  size = CHECK_BUTTON_DIM; break;
    case CHECK_MENU:    size = CHECK_MENU_DIM;   break;
    case RADIO_BUTTON:  size = RADIO_BUTTON_DIM; break;
    case RADIO_MENU:    size = RADIO_MENU_DIM;   break;
    default:            size = 12;               break;
    }

    x -= size / 2;
    y -= size / 2;

    /* Choose fill color. */
    if (disabled && disColor)
        fillColor = TkGlfwXColorToCG(disColor);
    else if (indicatorColor)
        fillColor = TkGlfwXColorToCG(indicatorColor);
    else {
        fillColor.r = 0.75; fillColor.g = 0.75;
        fillColor.b = 0.75; fillColor.a = 1.0;
    }

    if (selectColor)
        markColor = TkGlfwXColorToCG(selectColor);
    else {
        markColor.r = 0.0; markColor.g = 0.0;
        markColor.b = 0.0; markColor.a = 1.0;
    }

    /* Draw indicator background box/circle. */
    cg_set_source_rgba(dc->cg, fillColor.r, fillColor.g, fillColor.b, fillColor.a);
    if (mode == RADIO_BUTTON || mode == RADIO_MENU) {
        cg_arc(dc->cg, x + size/2.0, y + size/2.0, size/2.0, 0, 2*M_PI);
    } else {
        cg_rectangle(dc->cg, (double)x, (double)y, (double)size, (double)size);
    }
    cg_fill(dc->cg);

    if (on == 1) {
        cg_set_source_rgba(dc->cg, markColor.r, markColor.g, markColor.b, markColor.a);
        if (mode == CHECK_BUTTON || mode == CHECK_MENU) {
            /* Check mark. */
            cg_set_line_width(dc->cg, 2.0);
            cg_move_to(dc->cg, x + size/4.0,   y + size/2.0);
            cg_line_to(dc->cg, x + size/2.0,   y + 3*size/4.0);
            cg_line_to(dc->cg, x + 3*size/4.0, y + size/4.0);
            cg_stroke(dc->cg);
        } else {
            /* Radio dot. */
            cg_arc(dc->cg, x + size/2.0, y + size/2.0, size/4.0, 0, 2*M_PI);
            cg_fill(dc->cg);
        }
    } else if (on == 2) {
        /* Tristate: horizontal dash. */
        cg_set_source_rgba(dc->cg, markColor.r, markColor.g, markColor.b, markColor.a);
        cg_set_line_width(dc->cg, 2.0);
        cg_move_to(dc->cg, x + size/4.0,   y + size/2.0);
        cg_line_to(dc->cg, x + 3*size/4.0, y + size/2.0);
        cg_stroke(dc->cg);
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 */
