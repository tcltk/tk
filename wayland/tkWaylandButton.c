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

/*
 * Wayland specific button structure.
 */
typedef struct WaylandButton {
    TkButton info;              /* Generic button info. */
} WaylandButton;

/*
 * Class function table.
 */
const Tk_ClassProcs tkpButtonProcs = {
    sizeof(Tk_ClassProcs),
    TkButtonWorldChanged,
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
 *
 * Results:
 *      Returns pointer to newly allocated WaylandButton.
 *
 * Side effects:
 *      Allocates memory.
 * ------------------------------------------------------------------------ 
*/

TkButton *
TkpCreateButton(
	TCL_UNUSED(Tk_Window)) /* window */
{
    return (TkButton *) ckalloc(sizeof(WaylandButton));
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
 * TkpDisplayButton --
 *
 *      Main drawing routine for Wayland buttons.
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
  GC gc;
  Tk_3DBorder border;
  int x = 0, y = 0, relief;
  Tk_Window tkwin = butPtr->tkwin;
  int width = 0, height = 0;
  int fullWidth = 0, fullHeight = 0;
  int textXOffset = 0, textYOffset = 0;
  int haveImage = 0, haveText = 0;
  int imageXOffset = 0, imageYOffset = 0;
  int padX, padY, bd, hl;

  butPtr->flags &= ~REDRAW_PENDING;
  if (!tkwin || !Tk_IsMapped(tkwin)) return;
  border = butPtr->normalBorder;
  if (butPtr->state == STATE_DISABLED && butPtr->disabledFg) {
    gc = butPtr->disabledGC;
  } else if (butPtr->state == STATE_ACTIVE && !Tk_StrictMotif(tkwin)) {
    gc = butPtr->activeTextGC;
    border = butPtr->activeBorder;
  } else {
    gc = butPtr->normalTextGC;
  }
  if ((butPtr->flags & SELECTED) && butPtr->selectBorder && !butPtr->indicatorOn) {
    border = butPtr->selectBorder;
  }
  relief = butPtr->relief;
  if (butPtr->type >= TYPE_CHECK_BUTTON && !butPtr->indicatorOn) {
    if (butPtr->flags & SELECTED) {
      relief = TK_RELIEF_SUNKEN;
    } else if (butPtr->overRelief != relief) {
      relief = butPtr->offRelief;
    }
  }
  /* Background fill. */
  Tk_Fill3DRectangle(tkwin, Tk_WindowId(tkwin), border, 0, 0,
                     Tk_Width(tkwin), Tk_Height(tkwin), 0, TK_RELIEF_FLAT);
  /* Image / bitmap size. */
  if (butPtr->image) {
    Tk_SizeOfImage(butPtr->image, &width, &height);
    haveImage = 1;
  } else if (butPtr->bitmap != None) {
    Tk_SizeOfBitmap(butPtr->display, butPtr->bitmap, &width, &height);
    haveImage = 1;
  }
  Tk_GetPixelsFromObj(NULL, tkwin, butPtr->padXObj, &padX);
  Tk_GetPixelsFromObj(NULL, tkwin, butPtr->padYObj, &padY);
  Tk_GetPixelsFromObj(NULL, tkwin, butPtr->borderWidthObj, &bd);
  Tk_GetPixelsFromObj(NULL, tkwin, butPtr->highlightWidthObj, &hl);
  haveText = (butPtr->textWidth > 0 && butPtr->textHeight > 0);
  if (butPtr->compound != COMPOUND_NONE && haveImage && haveText) {
    switch (butPtr->compound) {
    case COMPOUND_TOP:
      textYOffset = height + padY;
      fullHeight = height + butPtr->textHeight + padY;
      fullWidth = (width > butPtr->textWidth) ? width : butPtr->textWidth;
      textXOffset = (fullWidth - butPtr->textWidth) / 2;
      imageXOffset = (fullWidth - width) / 2;
      break;
    case COMPOUND_LEFT:
    case COMPOUND_RIGHT:
      if (butPtr->compound == COMPOUND_LEFT) {
        textXOffset = width + padX;
      } else {
        imageXOffset = butPtr->textWidth + padX;
      }
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
    imageXOffset += x;
    imageYOffset += y;
    textXOffset += x;
    textYOffset += y;
    if (butPtr->image) {

      if ((butPtr->flags & SELECTED) && butPtr->selectImage) {
        Tk_RedrawImage(butPtr->selectImage, 0, 0, width, height,
                       Tk_WindowId(tkwin), imageXOffset, imageYOffset);

      } else if ((butPtr->flags & TRISTATED) && butPtr->tristateImage) {
        Tk_RedrawImage(butPtr->tristateImage, 0, 0, width, height,
                       Tk_WindowId(tkwin), imageXOffset, imageYOffset);

      } else {
        Tk_RedrawImage(butPtr->image, 0, 0, width, height,
                       Tk_WindowId(tkwin), imageXOffset, imageYOffset);
      }

    } else if (butPtr->bitmap != None) {

      XSetClipOrigin(butPtr->display, gc, imageXOffset, imageYOffset);

      XCopyPlane(butPtr->display, butPtr->bitmap,
                 Tk_WindowId(tkwin), gc,
                 0, 0, (unsigned)width, (unsigned)height,
                 imageXOffset, imageYOffset, 1);

      XSetClipOrigin(butPtr->display, gc, 0, 0);
    }

    Tk_DrawTextLayout(butPtr->display, Tk_WindowId(tkwin), gc,
                      butPtr->textLayout,
                      textXOffset, textYOffset,
                      0, -1);

    Tk_UnderlineTextLayout(butPtr->display, Tk_WindowId(tkwin), gc,
                           butPtr->textLayout,
                           textXOffset, textYOffset,
                           butPtr->underline);
  }

  else if (haveImage) {
    TkComputeAnchor(butPtr->anchor, tkwin, 0, 0,
                    butPtr->indicatorSpace + width,
                    height, &x, &y);

    x += butPtr->indicatorSpace;

    ShiftByOffset(butPtr, relief, &x, &y, width, height);

    if (butPtr->image) {

      Tk_RedrawImage(butPtr->image, 0, 0, width, height,
                     Tk_WindowId(tkwin), x, y);
    } else {

      XSetClipOrigin(butPtr->display, gc, x, y);

      XCopyPlane(butPtr->display, butPtr->bitmap,
                 Tk_WindowId(tkwin), gc,
                 0, 0, (unsigned)width, (unsigned)height,
                 x, y, 1);
      XSetClipOrigin(butPtr->display, gc, 0, 0);
    }
  }

  else if (haveText) {
    TkComputeAnchor(butPtr->anchor, tkwin, padX, padY,
                    butPtr->indicatorSpace + butPtr->textWidth,
                    butPtr->textHeight, &x, &y);
    x += butPtr->indicatorSpace;
    ShiftByOffset(butPtr, relief, &x, &y,
                  butPtr->textWidth, butPtr->textHeight);
    Tk_DrawTextLayout(butPtr->display, Tk_WindowId(tkwin), gc,
                      butPtr->textLayout, x, y, 0, -1);
    Tk_UnderlineTextLayout(butPtr->display, Tk_WindowId(tkwin), gc,
                           butPtr->textLayout, x, y,
                           butPtr->underline);
  }

  /* Indicator. */
  if ((butPtr->type == TYPE_CHECK_BUTTON ||
       butPtr->type == TYPE_RADIO_BUTTON) &&
      butPtr->indicatorOn &&
      butPtr->indicatorDiameter > 2 * bd) {

    TkBorder *selBd = (TkBorder *)butPtr->selectBorder;
    XColor *selColor = selBd ? selBd->bgColorPtr : NULL;

    int indType = (butPtr->type == TYPE_CHECK_BUTTON)
      ? CHECK_BUTTON : RADIO_BUTTON;

    int ind_x = -butPtr->indicatorSpace / 2;
    int ind_y = Tk_Height(tkwin) / 2;

    TkpDrawCheckIndicator(tkwin, butPtr->display,
                          Tk_WindowId(tkwin),
                          ind_x, ind_y, border,
                          butPtr->normalFg,
                          selColor,
                          butPtr->disabledFg,
                          (butPtr->flags & SELECTED) ? 1 :
                          (butPtr->flags & TRISTATED) ? 2 : 0,
                          butPtr->state == STATE_DISABLED,
                          indType);
  }
  /* Border. */
  if (relief != TK_RELIEF_FLAT) {
    int inset = hl;
    if (butPtr->defaultState == DEFAULT_ACTIVE) {

      Tk_Draw3DRectangle(tkwin, Tk_WindowId(tkwin),
                         butPtr->highlightBorder,
                         inset, inset,
                         Tk_Width(tkwin) - 2*inset,
                         Tk_Height(tkwin) - 2*inset,
                         2, TK_RELIEF_FLAT);
      inset += 2;
      Tk_Draw3DRectangle(tkwin, Tk_WindowId(tkwin),
                         butPtr->highlightBorder,
                         inset, inset,
                         Tk_Width(tkwin) - 2*inset,
                         Tk_Height(tkwin) - 2*inset,
                         1, TK_RELIEF_SUNKEN);
      inset += 3;

    } else if (butPtr->defaultState == DEFAULT_NORMAL) {

      Tk_Draw3DRectangle(tkwin, Tk_WindowId(tkwin),
                         butPtr->highlightBorder,
                         0, 0,
                         Tk_Width(tkwin),
                         Tk_Height(tkwin),
                         5, TK_RELIEF_FLAT);

      inset += 5;
    }

    Tk_Draw3DRectangle(tkwin, Tk_WindowId(tkwin),
                       border,
                       inset, inset,
                       Tk_Width(tkwin) - 2*inset,
                       Tk_Height(tkwin) - 2*inset,
                       bd, relief);
  }
  /* Focus highlight. */
  if (hl > 0) {
    GC hgc = (butPtr->flags & GOT_FOCUS)
      ? Tk_GCForColor(butPtr->highlightColorPtr,
                      Tk_WindowId(tkwin))
      : Tk_GCForColor(Tk_3DBorderColor(
				       butPtr->highlightBorder),
                      Tk_WindowId(tkwin));
    if (butPtr->defaultState == DEFAULT_NORMAL) {
      TkDrawInsetFocusHighlight(tkwin, hgc,
                                hl,
                                Tk_WindowId(tkwin),
                                5);
    } else {
      Tk_DrawFocusHighlight(tkwin, hgc,
                            hl,
                            Tk_WindowId(tkwin));
    }
  }
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
	Tk_SizeOfBitmap(butPtr->display, butPtr->bitmap, &width, &height);
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
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 */
