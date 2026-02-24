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
#include "tkGlfwInt.h"
#include <GLES2/gl2.h>
#include "nanovg.h"
#include <X11/Xlib.h>

/*
 * Shared with menu widget.
 */
extern void TkpDrawCheckIndicator(Tk_Window tkwin,
    Display *display, Drawable d, int x, int y,
    Tk_3DBorder bgBorder, XColor *indicatorColor,
    XColor *selectColor, XColor *disColor, int on,
    int disabled, int mode);
    
void ImageChanged(			/* to be passed to Tk_GetImage() */
    void *clientData,
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

/*
 * Indicator sizes (base values — later scaled).
 */
#define CHECK_BUTTON_DIM 16
#define CHECK_MENU_DIM    8
#define RADIO_BUTTON_DIM 16
#define RADIO_MENU_DIM    8


/*
 * -------------------------------------------------------------------------
 * ImageChanged --
 *
 *      Callback for Tk_GetImage changes.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 * ------------------------------------------------------------------------ 
 */

void
ImageChanged(TCL_UNUSED(void *),
             TCL_UNUSED(int), /* x */
             TCL_UNUSED(int), /* y */
             TCL_UNUSED(int), /* width */
             TCL_UNUSED(int), /* height */
             TCL_UNUSED(int), /*imageWidth*/
             TCL_UNUSED(int)) /*imageHeight */
{
  
  /* No-op. */
  
}


/* 
 * -------------------------------------------------------------------------
 * TkpCreateButton --
 *
 *      Create Wayland-specific button structure.
 *      For Wayland, we just use the base TkButton structure.
 *
 * Results:
 *      Returns pointer to newly allocated TkButton.
 *
 * Side effects:
 *      Allocates memory.
 * ------------------------------------------------------------------------ 
*/

TkButton *
TkpCreateButton(
	TCL_UNUSED(Tk_Window)) /* window */
{
    return (TkButton*) ckalloc(sizeof(TkButton));
}

/* 
 * -------------------------------------------------------------------------
 * ShiftByOffset --
 *
 *      Apply visual offset for non-strict Motif buttons.
 *
 * Results:
 *      Modifies x and y coordinates.
 *
 * Side effects:
 *      None.
 * ------------------------------------------------------------------------ 
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
 * -------------------------------------------------------------------------
 * DrawButtonBitmap --
 *
 *      Draw a Tk bitmap using NanoVG. Converts the 1‑bit bitmap to an
 *      RGBA image using the GC’s foreground color and draws it.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Draws bitmap at specified location.
 * ------------------------------------------------------------------------ 
*/

static void
DrawButtonBitmap(TkButton *butPtr,
                 TkWaylandDrawingContext *dc,
                 int x,
                 int y,
                 int width,
                 int height)
{
    Pixmap bitmap = butPtr->bitmap; 
    unsigned char *bits = NULL;
    unsigned char *rgba = NULL;
    unsigned int bm_width, bm_height, border_width, depth;
    int x_hot, y_hot;
    XGCValues gcValues;
    XColor *fgColor;
    XColor fgColorValue;
    int imageId;
    int i, j;
    Drawable screen;
    Display *dpy;
    XImage *image = NULL;

    if (!bitmap) {
        /* No bitmap: draw fallback rectangle. */
        nvgBeginPath(dc->vg);
        nvgRect(dc->vg, x, y, width, height);
        nvgFillColor(dc->vg, nvgRGBA(192, 192, 192, 255));
        nvgFill(dc->vg);
        return;
    }

    /* Get bitmap dimensions using XGetGeometry. */
    dpy = Tk_Display(butPtr->tkwin);
    screen = Tk_WindowId(butPtr->tkwin);

    if (!XGetGeometry(dpy, bitmap, &screen, &x_hot, &y_hot,
                      &bm_width, &bm_height, &border_width, &depth)) {
        /* Geometry failed — fallback. */
        goto fallback_rect;
    }

    /* Validate size (early exit if mismatch). */
    if (bm_width != (unsigned int)width || bm_height != (unsigned int)height) {
        goto fallback_rect;
    }

    /* Get foreground color from the current GC. */
    GC currentGC = butPtr->normalTextGC;
    if (butPtr->state == STATE_DISABLED && butPtr->disabledFg) {
        currentGC = butPtr->disabledGC;
    } else if (butPtr->state == STATE_ACTIVE && !Tk_StrictMotif(butPtr->tkwin)) {
        currentGC = butPtr->activeTextGC;
    }

    if (currentGC) {
        XGetGCValues(butPtr->display, currentGC, GCForeground, &gcValues);
        fgColorValue.pixel = gcValues.foreground;
        fgColor = Tk_GetColorByValue(butPtr->tkwin, &fgColorValue);
    } else {
        fgColor = butPtr->normalFg;
    }

    /* 
     * Read bitmap pixels via XGetImage → XGetPixel
     */
    image = XGetImage(dpy, bitmap,
                      0, 0, bm_width, bm_height,
                      1,          /* Only plane 0 for 1-bit bitmap */
                      XYPixmap);  /* Bitmap format */

    if (image == NULL) {
        goto fallback_rect;
    }

    /* Allocate buffer for packed bitmap data (1 bit per pixel, MSB-first). */
    int packedSize = ((int)bm_width * (int)bm_height + 7) / 8;
    bits = (unsigned char *)ckalloc(packedSize);
    if (!bits) {
        goto cleanup;
    }

    /* Pack the bits from XImage. */
    int byte_idx = 0;
    for (unsigned int yp = 0; yp < bm_height; yp++) {
        unsigned char byte = 0;
        int bit_count = 0;
        for (unsigned int xp = 0; xp < bm_width; xp++) {
            unsigned long pixel = XGetPixel(image, (int)xp, (int)yp);
            if (pixel != 0) {
                byte |= (1U << (7 - bit_count));   /* MSB first */
            }
            bit_count++;
            if (bit_count == 8 || xp == bm_width - 1) {
                bits[byte_idx++] = byte;
                byte = 0;
                bit_count = 0;
            }
        }
    }

    /* Allocate RGBA buffer (premultiplied or straight alpha — NanoVG handles both). */
    rgba = (unsigned char *)ckalloc(bm_width * bm_height * 4);
    if (!rgba) {
        goto cleanup;
    }

    /* Convert packed bits → RGBA (0 = transparent black, 1 = fg color opaque). */
    for (j = 0; j < (int)bm_height; j++) {
        for (i = 0; i < (int)bm_width; i++) {
            int byte_index = j * ((bm_width + 7) / 8) + (i / 8);
            int bit_index = 7 - (i % 8);
            int bit = (bits[byte_index] >> bit_index) & 1;

            unsigned char *pixel = &rgba[(j * bm_width + i) * 4];
            if (bit) {
                pixel[0] = fgColor->red   >> 8;
                pixel[1] = fgColor->green >> 8;
                pixel[2] = fgColor->blue  >> 8;
                pixel[3] = 255;
            } else {
                pixel[0] = pixel[1] = pixel[2] = pixel[3] = 0;
            }
        }
    }

    /* Draw via NanoVG using the RGBA image. */
    imageId = nvgCreateImageRGBA(dc->vg, (int)bm_width, (int)bm_height, 0, rgba);
    if (imageId > 0) {
        NVGpaint paint = nvgImagePattern(dc->vg, x, y, bm_width, bm_height, 0, imageId, 1);
        nvgBeginPath(dc->vg);
        nvgRect(dc->vg, x, y, bm_width, bm_height);
        nvgFillPaint(dc->vg, paint);
        nvgFill(dc->vg);
        nvgDeleteImage(dc->vg, imageId);
    }

cleanup:
    if (image != NULL) {
        XDestroyImage(image);
    }
    if (rgba != NULL) {
        ckfree(rgba);
    }
    if (bits != NULL) {
        ckfree(bits);
    }
    return;

fallback_rect:
    nvgBeginPath(dc->vg);
    nvgRect(dc->vg, x, y, width, height);
    nvgFillColor(dc->vg, nvgRGBA(192, 192, 192, 255));
    nvgFill(dc->vg);
    return;
}

/* 
 * -------------------------------------------------------------------------
 * DrawButtonImage --
 *
 *      Helper function to draw an image or bitmap using NanoVG.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Draws image at specified location.
 * ------------------------------------------------------------------------ 
*/

static void
DrawButtonImage(TkButton *butPtr, TkWaylandDrawingContext *dc,
                 int x, int y, int width, int height, int selected)
{
    if (butPtr->image) {
        /* Use Tk_RedrawImage – it eventually calls XPutImage which
         * in our Wayland port uses the NanoVG drawing context. */
        if (selected && butPtr->selectImage) {
            Tk_RedrawImage(butPtr->selectImage, 0, 0, width, height,
                           (Drawable)dc, x, y);
        } else if ((butPtr->flags & TRISTATED) && butPtr->tristateImage) {
            Tk_RedrawImage(butPtr->tristateImage, 0, 0, width, height,
                           (Drawable)dc, x, y);
        } else {
            Tk_RedrawImage(butPtr->image, 0, 0, width, height,
                           (Drawable)dc, x, y);
        }
    } else if (butPtr->bitmap != None) {
        /* Bitmap drawing – convert to RGBA and draw with NanoVG. */
        DrawButtonBitmap(butPtr, dc, x, y, width, height);
    }
}

/* 
 * -------------------------------------------------------------------------
 * DrawButtonText --
 *
 *      Helper function to draw text layout using NanoVG.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Draws text at specified location.
 * ------------------------------------------------------------------------ 
*/

static void
DrawButtonText(TkButton *butPtr, TkWaylandDrawingContext *dc,
                int x, int y)
{
    GC currentGC;
    
    /* Select appropriate GC based on button state. */
    if (butPtr->state == STATE_DISABLED && butPtr->disabledFg) {
        currentGC = butPtr->disabledGC;
    } else if (butPtr->state == STATE_ACTIVE && !Tk_StrictMotif(butPtr->tkwin)) {
        currentGC = butPtr->activeTextGC;
    } else {
        currentGC = butPtr->normalTextGC;
    }

    /* Apply GC settings for text. */
    TkGlfwApplyGC(dc->vg, currentGC);
    
    /* Draw the text layout. */
    Tk_DrawTextLayout(butPtr->display, (Drawable)dc, currentGC,
                      butPtr->textLayout, x, y, 0, -1);
    
    /* Draw underline if needed. */
    Tk_UnderlineTextLayout(butPtr->display, (Drawable)dc, currentGC,
                           butPtr->textLayout, x, y,
                           butPtr->underline);
}

/* 
 * -------------------------------------------------------------------------
 * TkpDisplayButton --
 *
 *      Main drawing routine for Wayland buttons using NanoVG.
 *
 * Results:
 *      Button is drawn to window.
 *
 * Side effects:
 *      Draws to window surface.
 * ------------------------------------------------------------------------ 
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

    butPtr->flags &= ~REDRAW_PENDING;
    if (!tkwin || !Tk_IsMapped(tkwin)) return;

    winWidth = Tk_Width(tkwin);
    winHeight = Tk_Height(tkwin);

    relief = butPtr->relief;
    if (butPtr->type >= TYPE_CHECK_BUTTON && !butPtr->indicatorOn) {
        if (butPtr->flags & SELECTED) {
            relief = TK_RELIEF_SUNKEN;
        } else if (butPtr->overRelief != relief) {
            relief = butPtr->offRelief;
        }
    }

	currentGC = butPtr->activeTextGC;
    /* Begin drawing with NanoVG. */
    if (TkGlfwBeginDraw((Drawable)tkwin, currentGC, &dc) != TCL_OK) {
        return;
    }

    /* Get padding and border values. */
    Tk_GetPixelsFromObj(NULL, tkwin, butPtr->padXObj, &padX);
    Tk_GetPixelsFromObj(NULL, tkwin, butPtr->padYObj, &padY);
    Tk_GetPixelsFromObj(NULL, tkwin, butPtr->borderWidthObj, &bd);
    Tk_GetPixelsFromObj(NULL, tkwin, butPtr->highlightWidthObj, &hl);

    /* Background fill - using 3D border drawing. */
    Tk_Fill3DRectangle(tkwin, (Drawable)&dc, butPtr->normalBorder, 0, 0,
                       winWidth, winHeight, 0, TK_RELIEF_FLAT);

    /* Determine image/bitmap size. */
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
        width = (int)bm_width;
        height = (int)bm_height;
        haveImage = 1;
    }

    haveText = (butPtr->textWidth > 0 && butPtr->textHeight > 0);

    /* Handle compound button (image + text). */
    if (butPtr->compound != COMPOUND_NONE && haveImage && haveText) {
        switch (butPtr->compound) {
        case COMPOUND_TOP:
            textYOffset = height + padY;
            fullHeight = height + butPtr->textHeight + padY;
            fullWidth = (width > butPtr->textWidth) ? width : butPtr->textWidth;
            textXOffset = (fullWidth - butPtr->textWidth) / 2;
            imageXOffset = (fullWidth - width) / 2;
            break;
        case COMPOUND_BOTTOM:
            imageYOffset = butPtr->textHeight + padY;
            fullHeight = height + butPtr->textHeight + padY;
            fullWidth = (width > butPtr->textWidth) ? width : butPtr->textWidth;
            textXOffset = (fullWidth - butPtr->textWidth) / 2;
            imageXOffset = (fullWidth - width) / 2;
            break;
        case COMPOUND_LEFT:
            textXOffset = width + padX;
            fullWidth = butPtr->textWidth + padX + width;
            fullHeight = (height > butPtr->textHeight) ? height : butPtr->textHeight;
            textYOffset = (fullHeight - butPtr->textHeight) / 2;
            imageYOffset = (fullHeight - height) / 2;
            break;
        case COMPOUND_RIGHT:
            imageXOffset = butPtr->textWidth + padX;
            fullWidth = butPtr->textWidth + padX + width;
            fullHeight = (height > butPtr->textHeight) ? height : butPtr->textHeight;
            textYOffset = (fullHeight - butPtr->textHeight) / 2;
            imageYOffset = (fullHeight - height) / 2;
            break;
        case COMPOUND_CENTER:
            fullWidth = (width > butPtr->textWidth) ? width : butPtr->textWidth;
            fullHeight = (height > butPtr->textHeight) ? height : butPtr->textHeight;
            textXOffset = (fullWidth - butPtr->textWidth) / 2;
            imageXOffset = (fullWidth - width) / 2;
            textYOffset = (fullHeight - butPtr->textHeight) / 2;
            imageYOffset = (fullHeight - height) / 2;
            break;
        default:
            break;
        }

        TkComputeAnchor(butPtr->anchor, tkwin, padX, padY,
                        butPtr->indicatorSpace + fullWidth,
                        fullHeight, &x, &y);

        x += butPtr->indicatorSpace;

        ShiftByOffset(butPtr, relief, &x, &y, width, height);

        /* Draw image with offset. */
        DrawButtonImage(butPtr, &dc, x + imageXOffset, y + imageYOffset, 
                        width, height, (butPtr->flags & SELECTED));

        /* Draw text with offset. */
        DrawButtonText(butPtr, &dc, x + textXOffset, y + textYOffset);
    }

    /* Image only. */
    else if (haveImage) {
        TkComputeAnchor(butPtr->anchor, tkwin, 0, 0,
                        butPtr->indicatorSpace + width,
                        height, &x, &y);

        x += butPtr->indicatorSpace;

        ShiftByOffset(butPtr, relief, &x, &y, width, height);

        DrawButtonImage(butPtr, &dc, x, y, width, height, 
                        (butPtr->flags & SELECTED));
    }

    /* Text only. */
    else if (haveText) {
        TkComputeAnchor(butPtr->anchor, tkwin, padX, padY,
                        butPtr->indicatorSpace + butPtr->textWidth,
                        butPtr->textHeight, &x, &y);
        x += butPtr->indicatorSpace;
        ShiftByOffset(butPtr, relief, &x, &y,
                      butPtr->textWidth, butPtr->textHeight);
        DrawButtonText(butPtr, &dc, x, y);
    }

    /* Draw indicator (check/radio button). */
    if ((butPtr->type == TYPE_CHECK_BUTTON ||
         butPtr->type == TYPE_RADIO_BUTTON) &&
        butPtr->indicatorOn &&
        butPtr->indicatorDiameter > 2 * bd) {

        TkBorder *selBd = (TkBorder *)butPtr->selectBorder;
        XColor *selColor = selBd ? selBd->bgColorPtr : NULL;

        int indType = (butPtr->type == TYPE_CHECK_BUTTON)
          ? CHECK_BUTTON : RADIO_BUTTON;

        int ind_x = -butPtr->indicatorSpace / 2;
        int ind_y = winHeight / 2;

        TkpDrawCheckIndicator(tkwin, butPtr->display,
                              (Drawable)&dc,
                              ind_x, ind_y, butPtr->normalBorder,
                              butPtr->normalFg,
                              selColor,
                              butPtr->disabledFg,
                              (butPtr->flags & SELECTED) ? 1 :
                              (butPtr->flags & TRISTATED) ? 2 : 0,
                              butPtr->state == STATE_DISABLED,
                              indType);
    }

    /* Draw border with 3D effects. */
    if (relief != TK_RELIEF_FLAT) {
        int inset = hl;
        if (butPtr->defaultState == DEFAULT_ACTIVE) {
            /* Draw default ring for active default button. */
            Tk_Draw3DRectangle(tkwin, (Drawable)&dc,
                               butPtr->highlightBorder,
                               inset, inset,
                               winWidth - 2*inset,
                               winHeight - 2*inset,
                               2, TK_RELIEF_FLAT);
            inset += 2;
            Tk_Draw3DRectangle(tkwin, (Drawable)&dc,
                               butPtr->highlightBorder,
                               inset, inset,
                               winWidth - 2*inset,
                               winHeight - 2*inset,
                               1, TK_RELIEF_SUNKEN);
            inset += 3;

        } else if (butPtr->defaultState == DEFAULT_NORMAL) {
            /* Draw extra space for normal default button. */
            Tk_Draw3DRectangle(tkwin, (Drawable)&dc,
                               butPtr->highlightBorder,
                               0, 0,
                               winWidth,
                               winHeight,
                               5, TK_RELIEF_FLAT);
            inset += 5;
        }

        /* Draw main button border. */
        Tk_Draw3DRectangle(tkwin, (Drawable)&dc,
                           butPtr->normalBorder,
                           inset, inset,
                           winWidth - 2*inset,
                           winHeight - 2*inset,
                           bd, relief);
    }

    /* Draw focus highlight. */
    if (hl > 0) {
        if (butPtr->defaultState == DEFAULT_NORMAL) {
            TkDrawInsetFocusHighlight(tkwin, butPtr->normalTextGC, hl,
                                      (Drawable)&dc, 5);
        } else {
            Tk_DrawFocusHighlight(tkwin, butPtr->normalTextGC, hl,
                                  (Drawable)&dc);
        }
    }

    /* End drawing session. */
    TkGlfwEndDraw(&dc);
}

/* 
 * -------------------------------------------------------------------------
 * TkpComputeButtonGeometry --
 *
 *      Calculate button geometry requirements.
 *
 * Results:
 *      Sets geometry request for button window.
 *
 * Side effects:
 *      May request window resize.
 * ------------------------------------------------------------------------ 
 */

void
TkpComputeButtonGeometry(
    TkButton *butPtr)	/* Button whose geometry may have changed. */
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

    /*
     * Leave room for the default ring if needed.
     */
    if (butPtr->defaultState != DEFAULT_DISABLED) {
        butPtr->inset += 5;
    }
    butPtr->indicatorSpace = 0;

    width = 0;
    height = 0;
    txtWidth = 0;
    txtHeight = 0;
    avgWidth = 0;

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
        width = (int)bm_width;
        height = (int)bm_height;
        haveImage = 1;
    }

    if (haveImage == 0 || butPtr->compound != COMPOUND_NONE) {
        Tk_FreeTextLayout(butPtr->textLayout);

        butPtr->textLayout = Tk_ComputeTextLayout(butPtr->tkfont,
                Tcl_GetString(butPtr->textPtr), TCL_INDEX_NONE, wrapLength,
                butPtr->justify, 0, &butPtr->textWidth, &butPtr->textHeight);

        txtWidth = butPtr->textWidth;
        txtHeight = butPtr->textHeight;
        avgWidth = Tk_TextWidth(butPtr->tkfont, "0", 1);
        Tk_GetFontMetrics(butPtr->tkfont, &fm);
        haveText = (txtWidth != 0 && txtHeight != 0);
    }

    /*
     * If the button is compound (i.e., it shows both an image and text), the
     * new geometry is a combination of the image and text geometry. We only
     * honor the compound bit if the button has both text and an image,
     * because otherwise it is not really a compound button.
     */

    if (butPtr->compound != COMPOUND_NONE && haveImage && haveText) {
        switch ((enum compound) butPtr->compound) {
        case COMPOUND_TOP:
        case COMPOUND_BOTTOM:
            /*
             * Image is above or below text.
             */
            height += txtHeight + padY;
            width = (width > txtWidth ? width : txtWidth);
            break;
        case COMPOUND_LEFT:
        case COMPOUND_RIGHT:
            /*
             * Image is left or right of text.
             */
            width += txtWidth + padX;
            height = (height > txtHeight ? height : txtHeight);
            break;
        case COMPOUND_CENTER:
            /*
             * Image and text are superimposed.
             */
            width = (width > txtWidth ? width : txtWidth);
            height = (height > txtHeight ? height : txtHeight);
            break;
        case COMPOUND_NONE:
            break;
        }
        if (butPtrWidth > 0) {
            width = butPtrWidth;
        }
        if (butPtrHeight > 0) {
            height = butPtrHeight;
        }

        if ((butPtr->type >= TYPE_CHECK_BUTTON) && butPtr->indicatorOn) {
            butPtr->indicatorSpace = height;
            if (butPtr->type == TYPE_CHECK_BUTTON) {
                butPtr->indicatorDiameter = (65*height)/100;
            } else {
                butPtr->indicatorDiameter = (75*height)/100;
            }
        }

        width += 2 * padX;
        height += 2 * padY;
    } else {
        if (haveImage) {
            if (butPtrWidth > 0) {
                width = butPtrWidth;
            }
            if (butPtrHeight > 0) {
                height = butPtrHeight;
            }

            if ((butPtr->type >= TYPE_CHECK_BUTTON) && butPtr->indicatorOn) {
                butPtr->indicatorSpace = height;
                if (butPtr->type == TYPE_CHECK_BUTTON) {
                    butPtr->indicatorDiameter = (65*height)/100;
                } else {
                    butPtr->indicatorDiameter = (75*height)/100;
                }
            }
        } else {
            width = txtWidth;
            height = txtHeight;

            if (butPtrWidth > 0) {
                width = butPtrWidth * avgWidth;
            }
            if (butPtrHeight > 0) {
                height = butPtrHeight * fm.linespace;
            }
            if ((butPtr->type >= TYPE_CHECK_BUTTON) && butPtr->indicatorOn) {
                butPtr->indicatorDiameter = fm.linespace;
                butPtr->indicatorSpace = butPtr->indicatorDiameter + avgWidth;
            }
        }
    }

    /*
     * When issuing the geometry request, add extra space for the indicator,
     * if any, and for the border and padding, plus two extra pixels so the
     * display can be offset by 1 pixel in either direction for the raised or
     * lowered effect.
     */

    if ((butPtr->image == NULL) && (butPtr->bitmap == None)) {
        width += 2 * padX;
        height += 2 * padY;
    }
    if ((butPtr->type == TYPE_BUTTON) && !Tk_StrictMotif(butPtr->tkwin)) {
        width += 2;
        height += 2;
    }
    Tk_GeometryRequest(butPtr->tkwin, (int) (width + butPtr->indicatorSpace
            + 2 * butPtr->inset), (int) (height + 2 * butPtr->inset));
    Tk_SetInternalBorder(butPtr->tkwin, butPtr->inset);
}

/* 
 * -------------------------------------------------------------------------
 * TkpButtonWorldChanged --
 *
 *      Wayland replacement for TkButtonWorldChanged. Sets up GCs for text
 *      and image drawing without touching any X11 bitmap/stipple machinery.
 *      Uses Wayland/NanoVG drawing primitives.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      GCs reallocated; display list redraw scheduled.
 * ------------------------------------------------------------------------ 
 */

void
TkpButtonWorldChanged(void *instanceData)
{
    XGCValues gcValues;
    GC newGC;
    unsigned long mask;
    TkButton *butPtr = (TkButton *) instanceData;

    /* Normal text GC. */
    gcValues.foreground = butPtr->normalFg->pixel;
    gcValues.background = Tk_3DBorderColor(butPtr->normalBorder)->pixel;
    gcValues.font = Tk_FontId(butPtr->tkfont);
    gcValues.graphics_exposures = False;
    mask = GCForeground | GCBackground | GCFont | GCGraphicsExposures;
    newGC = Tk_GetGC(butPtr->tkwin, mask, &gcValues);
    if (butPtr->normalTextGC != NULL) {
        Tk_FreeGC(butPtr->display, butPtr->normalTextGC);
    }
    butPtr->normalTextGC = newGC;

    /* Active text GC. */
    gcValues.foreground = butPtr->activeFg->pixel;
    gcValues.background = Tk_3DBorderColor(butPtr->activeBorder)->pixel;
    newGC = Tk_GetGC(butPtr->tkwin, mask, &gcValues);
    if (butPtr->activeTextGC != NULL) {
        Tk_FreeGC(butPtr->display, butPtr->activeTextGC);
    }
    butPtr->activeTextGC = newGC;

    /* 
     * Disabled text GC - Wayland implementation
     * 
     * On Wayland, we cannot use X11 stipple bitmaps.
     * The disabled state will be handled by the compositor
     * or through visual effects. We use the disabledFg color
     * if specified, otherwise fall back to normal colors.
     */
    if (butPtr->disabledFg != NULL) {
        gcValues.foreground = butPtr->disabledFg->pixel;
        gcValues.background = Tk_3DBorderColor(butPtr->normalBorder)->pixel;
        newGC = Tk_GetGC(butPtr->tkwin, mask, &gcValues);
    } else {
        /* No disabledFg: use normal colors */
        gcValues.foreground = butPtr->normalFg->pixel;
        gcValues.background = Tk_3DBorderColor(butPtr->normalBorder)->pixel;
        newGC = Tk_GetGC(butPtr->tkwin, mask, &gcValues);
    }
    
    if (butPtr->disabledGC != NULL) {
        Tk_FreeGC(butPtr->display, butPtr->disabledGC);
    }
    butPtr->disabledGC = newGC;

    /* 
     * CRITICAL: Do NOT create gray50 stipple bitmap.
     * Set butPtr->gray to None to indicate no stipple available.
     * The stipple effect for disabled buttons will be handled
     * by the Wayland compositor or through NanoVG effects.
     */
    if (butPtr->gray != None) {
        /* If there was a previous stipple bitmap, free it */
        Tk_FreeBitmap(butPtr->display, butPtr->gray);
        butPtr->gray = None;
    }

    /* Recompute geometry with new settings */
    TkpComputeButtonGeometry(butPtr);

    /* Schedule redraw if needed */
    if ((butPtr->tkwin != NULL) && Tk_IsMapped(butPtr->tkwin)
            && !(butPtr->flags & REDRAW_PENDING)) {
        Tcl_DoWhenIdle(TkpDisplayButton, butPtr);
        butPtr->flags |= REDRAW_PENDING;
    }
}

/*
 * -------------------------------------------------------------------------
 * TkpDrawCheckIndicator --
 *
 *      Draw check/radio button indicator using NanoVG.
 *      This function is shared with menu widget and needs to be
 *      implemented for Wayland.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Draws indicator on the specified drawable.
 * ------------------------------------------------------------------------ 
 */

void
TkpDrawCheckIndicator(
    TCL_UNUSED(Tk_Window), /* tkwin */
    TCL_UNUSED(Display *), /* display */
    Drawable d,
    int x, 
    int y,
    TCL_UNUSED(Tk_3DBorder),  /* bgBorder */
    XColor *indicatorColor,
    XColor *selectColor,
    XColor *disColor,
    int on,
    int disabled,
    int mode)
{
    TkWaylandDrawingContext *dc = (TkWaylandDrawingContext *)d;
    int size = 0;
    int indicatorSize;
    
    /* Get indicator size based on mode. */
    switch (mode) {
        case CHECK_BUTTON:
            indicatorSize = CHECK_BUTTON_DIM;
            break;
        case CHECK_MENU:
            indicatorSize = CHECK_MENU_DIM;
            break;
        case RADIO_BUTTON:
            indicatorSize = RADIO_BUTTON_DIM;
            break;
        case RADIO_MENU:
            indicatorSize = RADIO_MENU_DIM;
            break;
        default:
            indicatorSize = 12;
    }
    
    /* Scale for DPI if needed. */
    size = indicatorSize;
    
    /* Center the indicator. */
    x = x - size/2;
    y = y - size/2;
    
    /* Draw background. */
    nvgBeginPath(dc->vg);
    nvgRect(dc->vg, x, y, size, size);
    
    if (disabled && disColor) {
        nvgFillColor(dc->vg, TkGlfwXColorToNVG(disColor));
    } else {
        nvgFillColor(dc->vg, TkGlfwXColorToNVG(indicatorColor));
    }
    nvgFill(dc->vg);
    
    /* Draw indicator state (check mark, radio dot, or tristate). */
    if (on == 1) {  /* Selected */
        if (mode == CHECK_BUTTON || mode == CHECK_MENU) {
            /* Draw check mark */
            nvgBeginPath(dc->vg);
            nvgMoveTo(dc->vg, x + size/4, y + size/2);
            nvgLineTo(dc->vg, x + size/2, y + 3*size/4);
            nvgLineTo(dc->vg, x + 3*size/4, y + size/4);
            nvgStrokeColor(dc->vg, TkGlfwXColorToNVG(selectColor));
            nvgStrokeWidth(dc->vg, 2.0f);
            nvgStroke(dc->vg);
        } else {  /* Radio button */
            nvgBeginPath(dc->vg);
            nvgCircle(dc->vg, x + size/2, y + size/2, size/4);
            nvgFillColor(dc->vg, TkGlfwXColorToNVG(selectColor));
            nvgFill(dc->vg);
        }
    } else if (on == 2) {  /* Tristate */
        /* Draw horizontal line for tristate. */
        nvgBeginPath(dc->vg);
        nvgMoveTo(dc->vg, x + size/4, y + size/2);
        nvgLineTo(dc->vg, x + 3*size/4, y + size/2);
        nvgStrokeColor(dc->vg, TkGlfwXColorToNVG(selectColor));
        nvgStrokeWidth(dc->vg, 2.0f);
        nvgStroke(dc->vg);
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 */
