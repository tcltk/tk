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
MODULE_SCOPE void TkpDrawCheckIndicator(Tk_Window tkwin,
    Display *display, Drawable d, int x, int y,
    Tk_3DBorder bgBorder, XColor *indicatorColor,
    XColor *selectColor, XColor *disColor, int on,
    int disabled, int mode);

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
 * SVG data for indicators.
 */
static const char checkbtnOffData[] =
    "<svg id='checkbutton' width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n"
    " <path id='borderdark' d='m0 0v16l1-1v-14h14l1-1h-16z' fill='#DARKKK'/>\n"
    " <path id='borderlight' d='m16 0-1 1v14h-14l-1 1h16v-16z' fill='#LIGHTT'/>\n"
    " <rect id='rectbackdrop' x='2' y='2' width='12' height='12' fill='#INTROR'/>\n"
    "</svg>";

static const char checkbtnOnData[] =
    "<svg id='checkbutton' width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n"
    " <path id='borderdark' d='m0 0v16l1-1v-14h14l1-1h-16z' fill='#DARKKK'/>\n"
    " <path id='borderlight' d='m16 0-1 1v14h-14l-1 1h16v-16z' fill='#LIGHTT'/>\n"
    " <rect id='rectbackdrop' x='2' y='2' width='12' height='12' fill='#INTROR'/>\n"
    " <path id='indicator' d='m4.5 8 3 3 4-6' fill='none' stroke='#INDCTR' stroke-linecap='round' stroke-linejoin='round' stroke-width='2'/>\n"
    "</svg>";

static const char radiobtnOffData[] =
    "<svg id='radiobutton' width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n"
    " <defs>\n"
    "  <linearGradient id='gradient' x1='5' y1='5' x2='11' y2='11' gradientUnits='userSpaceOnUse'>\n"
    "   <stop stop-color='#DARKKK' offset='0'/>\n"
    "   <stop stop-color='#LIGHTT' offset='1' stop-opacity='0'/>\n"
    "  </linearGradient>\n"
    " </defs>\n"
    " <circle cx='8' cy='8' r='8' fill='url(#gradient)'/>\n"
    " <circle cx='8' cy='8' r='6.5' fill='#INTROR'/>\n"
    "</svg>";

static const char radiobtnOnData[] =
    "<svg id='radiobutton' width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n"
    " <defs>\n"
    "  <linearGradient id='gradient' x1='5' y1='5' x2='11' y2='11' gradientUnits='userSpaceOnUse'>\n"
    "   <stop stop-color='#DARKKK' offset='0'/>\n"
    "   <stop stop-color='#LIGHTT' offset='1' stop-opacity='0'/>\n"
    "  </linearGradient>\n"
    " </defs>\n"
    " <circle cx='8' cy='8' r='8' fill='url(#gradient)'/>\n"
    " <circle cx='8' cy='8' r='7' fill='#INTROR'/>\n"
    " <circle cx='8' cy='8' r='4' fill='#INDCTR'/>\n"
    "</svg>";

static const char menuOffData[] =
    "<svg width='8' height='8' version='1.1' xmlns='http://www.w3.org/2000/svg'></svg>";

static const char checkmenuOnData[] =
    "<svg width='8' height='8' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n"
    " <path id='indicator' d='m1 3.5 2.5 3 3.5-5' fill='none' stroke='#INDCTR' stroke-linecap='round' stroke-linejoin='round' stroke-width='1.975'/>\n"
    "</svg>";

static const char radiomenuOnData[] =
    "<svg width='8' height='8' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n"
    " <circle cx='4' cy='4' r='3' fill='#INDCTR'/>\n"
    "</svg>";


/* 
 * -------------------------------------------------------------------------
 * ColorToStr --
 *
 *      Convert XColor to hex string.
 *
 * Results:
 *      Stores hex color in colorStr.
 *
 * Side effects:
 *      None.
 * ------------------------------------------------------------------------ 
 */

static void
ColorToStr(const XColor *colorPtr, char *colorStr)
{
    snprintf(colorStr, 7, "%02x%02x%02x",
             colorPtr->red >> 8, colorPtr->green >> 8, colorPtr->blue >> 8);
}


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

static void
ImageChanged(void *clientData,
             TCL_UNUSED(int x),
             TCL_UNUSED(int y),
             TCL_UNUSED(int width),
             TCL_UNUSED(int height),
             TCL_UNUSED(int imageWidth),
             TCL_UNUSED(int imageHeight))
{
  
  /* No-op. */
  
}

/* 
 *-------------------------------------------------------------------------
 * TkpDrawCheckIndicator --
 *
 *      Draw check/radio indicator using SVG images.
 *
 * Results:
 *      Draws indicator on screen.
 *
 * Side effects:
 *      Creates temporary image in Tcl interpreter.
 * ------------------------------------------------------------------------ 
*/

void
TkpDrawCheckIndicator(Tk_Window tkwin, Display *display, Drawable d,
                      int x, int y, Tk_3DBorder bgBorder,
                      XColor *indicatorColor, XColor *selectColor,
                      XColor *disableColor, int on, int disabled, int mode)
{
    const char *svgDataPtr;
    int hasBorder = 0, hasInterior = 0, dim;
    double scalingLevel = TkScalingLevel(tkwin);
    TkBorder *bg_brdr = (TkBorder *) bgBorder;
    char darkStr[7], lightStr[7], interiorStr[7], indStr[7];
    Tcl_Interp *interp = Tk_Interp(tkwin);
    char imgName[80];
    Tk_Image img = NULL;
    char *svgCopy = NULL;
    size_t len;
    char *pDark, *pLight, *pInt, *pInd;
    const char *fmt = "image create photo %s -format $::tk::svgFmt -data {%s}";
    char *script;
    int code;

    if (!tkwin || !bgBorder || !indicatorColor) return;

    if (!disableColor) disableColor = bg_brdr->bgColorPtr;
    if (!selectColor)  selectColor  = bg_brdr->bgColorPtr;

    switch (mode) {
        case CHECK_BUTTON:
            svgDataPtr = on ? checkbtnOnData : checkbtnOffData;
            hasBorder = hasInterior = 1;
            dim = CHECK_BUTTON_DIM;
            break;
        case CHECK_MENU:
            svgDataPtr = on ? checkmenuOnData : menuOffData;
            dim = CHECK_MENU_DIM;
            break;
        case RADIO_BUTTON:
            svgDataPtr = on ? radiobtnOnData : radiobtnOffData;
            hasBorder = hasInterior = 1;
            dim = RADIO_BUTTON_DIM;
            break;
        case RADIO_MENU:
            svgDataPtr = on ? radiomenuOnData : menuOffData;
            dim = RADIO_MENU_DIM;
            break;
        default:
            return;
    }
    dim = (int)(dim * scalingLevel);

    TkpGetShadows(bg_brdr, tkwin);

    strcpy(darkStr,     bg_brdr->darkColorPtr   ? "" : "000000");
    strcpy(lightStr,    bg_brdr->lightColorPtr  ? "" : "ffffff");
    if (bg_brdr->darkColorPtr)  ColorToStr(Tk_GetColorByValue(tkwin, bg_brdr->darkColorPtr),  darkStr);
    if (bg_brdr->lightColorPtr) ColorToStr(Tk_GetColorByValue(tkwin, bg_brdr->lightColorPtr), lightStr);

    if (on == 2 || disabled) {
        ColorToStr(Tk_GetColorByValue(tkwin, bg_brdr->bgColorPtr), interiorStr);
        ColorToStr(Tk_GetColorByValue(tkwin, disableColor),        indStr);
    } else {
        ColorToStr(Tk_GetColorByValue(tkwin, selectColor),         interiorStr);
        ColorToStr(Tk_GetColorByValue(tkwin, indicatorColor),      indStr);
    }

    snprintf(imgName, sizeof(imgName),
             "::tk::icons::indicator%d_%d_%s_%s_%s_%s",
             dim, mode,
             hasBorder ? darkStr  : "XXXXXX",
             hasBorder ? lightStr : "XXXXXX",
             hasInterior ? interiorStr : "XXXXXX",
             on ? indStr : "XXXXXX");

    img = Tk_GetImage(interp, tkwin, imgName, ImageChanged, NULL);
    if (!img) {
        len = strlen(svgDataPtr);
        svgCopy = Tcl_AttemptAlloc(len + 1);
        if (!svgCopy) return;
        memcpy(svgCopy, svgDataPtr, len + 1);

        pDark = strstr(svgCopy, "DARKKK");  if (pDark) memcpy(pDark, darkStr,  6);
        pLight= strstr(svgCopy, "LIGHTT");  if (pLight) memcpy(pLight,lightStr,6);
        pInt  = strstr(svgCopy, "INTROR");  if (pInt)   memcpy(pInt, interiorStr,6);
        pInd  = strstr(svgCopy, "INDCTR");  if (pInd)   memcpy(pInd, indStr,     6);

        size_t scriptLen = strlen(fmt) + strlen(imgName) + len + 32;
        script = Tcl_AttemptAlloc(scriptLen);
        if (!script) { Tcl_Free(svgCopy); return; }
        snprintf(script, scriptLen, fmt, imgName, svgCopy);
        Tcl_Free(svgCopy);

        code = Tcl_EvalEx(interp, script, -1, TCL_EVAL_GLOBAL);
        Tcl_Free(script);
        if (code != TCL_OK) {
            Tcl_BackgroundException(interp, code);
            return;
        }

        img = Tk_GetImage(interp, tkwin, imgName, ImageChanged, NULL);
    }

    if (img) {
        x -= dim / 2;
        y -= dim / 2;
        Tk_RedrawImage(img, 0, 0, dim, dim, d, x, y);
        Tk_FreeImage(img);
    }
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
TkpCreateButton(TCL_UNUSED(Tk_Window)) /* window */
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
  int fullWidth, fullHeight;
  int textXOffset = 0, textYOffset = 0;
  int haveImage = 0, haveText = 0;
  int imageWidth = 0, imageHeight = 0;
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

  /* Background fill — direct to window. */
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
  imageWidth = width;
  imageHeight = height;

  Tk_GetPixelsFromObj(NULL, tkwin, butPtr->padXObj, &padX);
  Tk_GetPixelsFromObj(NULL, tkwin, butPtr->padYObj, &padY);
  Tk_GetPixelsFromObj(NULL, tkwin, butPtr->borderWidthObj, &bd);
  Tk_GetPixelsFromObj(NULL, tkwin, butPtr->highlightWidthObj, &hl);

  haveText = (butPtr->textWidth > 0 && butPtr->textHeight > 0);

  if (butPtr->compound != COMPOUND_NONE && haveImage && haveText) {
    /* Compound layout. */
    switch (butPtr->compound) {
    case COMPOUND_TOP:
      textYOffset = height + padY;
      fullHeight = height + butPtr->textHeight + padY;
      fullWidth  = MAX(width, butPtr->textWidth);
      textXOffset = (fullWidth - butPtr->textWidth) / 2;
      imageXOffset = (fullWidth - width) / 2;
      break;
    case COMPOUND_LEFT:
    case COMPOUND_RIGHT:
      textXOffset = width + padX;
      fullWidth = width + butPtr->textWidth + padX;
      fullHeight = MAX(height, butPtr->textHeight);
      textYOffset = (fullHeight - butPtr->textHeight) / 2;
      imageYOffset = (fullHeight - height) / 2;
      break;
    case COMPOUND_CENTER:
      /*
       * Image and text are superimposed.
       */

      fullWidth = (width > butPtr->textWidth ? width :
		   butPtr->textWidth);
      fullHeight = (height > butPtr->textHeight ? height :
		    butPtr->textHeight);
      textXOffset = (fullWidth - butPtr->textWidth)/2;
      imageXOffset = (fullWidth - width)/2;
      textYOffset = (fullHeight - butPtr->textHeight)/2;
      imageYOffset = (fullHeight - height)/2;
      break;
    case COMPOUND_NONE:
      break;
    }

    TkComputeAnchor(butPtr->anchor, tkwin, padX, padY,
		    butPtr->indicatorSpace + fullWidth, fullHeight, &x, &y);
    x += butPtr->indicatorSpace;
    ShiftByOffset(butPtr, relief, &x, &y, width, height);
    imageXOffset += x;
    imageYOffset += y;
    textXOffset   += x;
    textYOffset   += y;

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
      XCopyPlane(butPtr->display, butPtr->bitmap, Tk_WindowId(tkwin), gc,
		 0, 0, (unsigned)width, (unsigned)height,
		 imageXOffset, imageYOffset, 1);
      XSetClipOrigin(butPtr->display, gc, 0, 0);
    }

    Tk_DrawTextLayout(butPtr->display, Tk_WindowId(tkwin), gc,
		      butPtr->textLayout, x + textXOffset, y + textYOffset, 0, -1);
    Tk_UnderlineTextLayout(butPtr->display, Tk_WindowId(tkwin), gc,
			   butPtr->textLayout, x + textXOffset, y + textYOffset,
			   butPtr->underline);
  }
  else if (haveImage) {
    /* image-only or bitmap-only case — similar logic. */
    TkComputeAnchor(butPtr->anchor, tkwin, 0, 0,
		    butPtr->indicatorSpace + width, height, &x, &y);
    x += butPtr->indicatorSpace;
    ShiftByOffset(butPtr, relief, &x, &y, width, height);
    imageXOffset = x;
    imageYOffset = y;

    if (butPtr->image) {
      Tk_RedrawImage(butPtr->image, 0, 0, width, height,
		     Tk_WindowId(tkwin), imageXOffset, imageYOffset);
    } else {
      XSetClipOrigin(butPtr->display, gc, x, y);
      XCopyPlane(butPtr->display, butPtr->bitmap, Tk_WindowId(tkwin), gc,
		 0, 0, (unsigned)width, (unsigned)height, x, y, 1);
      XSetClipOrigin(butPtr->display, gc, 0, 0);
    }
  }
  else if (haveText) {
    TkComputeAnchor(butPtr->anchor, tkwin, padX, padY,
		    butPtr->indicatorSpace + butPtr->textWidth,
		    butPtr->textHeight, &x, &y);
    x += butPtr->indicatorSpace;
    ShiftByOffset(butPtr, relief, &x, &y, butPtr->textWidth, butPtr->textHeight);

    Tk_DrawTextLayout(butPtr->display, Tk_WindowId(tkwin), gc,
		      butPtr->textLayout, x, y, 0, -1);
    Tk_UnderlineTextLayout(butPtr->display, Tk_WindowId(tkwin), gc,
			   butPtr->textLayout, x, y, butPtr->underline);
  }

  /* Indicator (check / radio). */
  if ((butPtr->type == TYPE_CHECK_BUTTON || butPtr->type == TYPE_RADIO_BUTTON) &&
      butPtr->indicatorOn && butPtr->indicatorDiameter > 2 * bd) {
    TkBorder *selBd = (TkBorder *) butPtr->selectBorder;
    XColor *selColor = selBd ? selBd->bgColorPtr : NULL;
    int indType = (butPtr->type == TYPE_CHECK_BUTTON) ? CHECK_BUTTON : RADIO_BUTTON;

    int ind_x = -butPtr->indicatorSpace / 2;
    int ind_y = Tk_Height(tkwin) / 2;

    TkpDrawCheckIndicator(tkwin, butPtr->display, Tk_WindowId(tkwin),
			  ind_x, ind_y, border,
			  butPtr->normalFg, selColor, butPtr->disabledFg,
			  (butPtr->flags & SELECTED) ? 1 :
			  (butPtr->flags & TRISTATED) ? 2 : 0,
			  butPtr->state == STATE_DISABLED, indType);
  }

  /* Disabled stipple emulation (needs port-level support). */
  if (butPtr->state == STATE_DISABLED &&
      (!butPtr->disabledFg || butPtr->image)) {
	/* TODO: Render disabled state. */
  }

  /* Border & default ring & focus highlight — drawn last. */
  if (relief != TK_RELIEF_FLAT) {
    int inset = hl;

    if (butPtr->defaultState == DEFAULT_ACTIVE) {
      Tk_Draw3DRectangle(tkwin, Tk_WindowId(tkwin), butPtr->highlightBorder,
			 inset, inset, Tk_Width(tkwin)-2*inset, Tk_Height(tkwin)-2*inset,
			 2, TK_RELIEF_FLAT);
      inset += 2;
      Tk_Draw3DRectangle(tkwin, Tk_WindowId(tkwin), butPtr->highlightBorder,
			 inset, inset, Tk_Width(tkwin)-2*inset, Tk_Height(tkwin)-2*inset,
			 1, TK_RELIEF_SUNKEN);
      inset += 3;
    } else if (butPtr->defaultState == DEFAULT_NORMAL) {
      Tk_Draw3DRectangle(tkwin, Tk_WindowId(tkwin), butPtr->highlightBorder,
			 0, 0, Tk_Width(tkwin), Tk_Height(tkwin), 5, TK_RELIEF_FLAT);
      inset += 5;
    }

    Tk_Draw3DRectangle(tkwin, Tk_WindowId(tkwin), border,
		       inset, inset,
		       Tk_Width(tkwin) - 2*inset, Tk_Height(tkwin) - 2*inset,
		       bd, relief);
  }

  if (hl > 0) {
    GC hgc = (butPtr->flags & GOT_FOCUS)
      ? Tk_GCForColor(butPtr->highlightColorPtr, Tk_WindowId(tkwin))
      : Tk_GCForColor(Tk_3DBorderColor(butPtr->highlightBorder),
		      Tk_WindowId(tkwin));

    if (butPtr->defaultState == DEFAULT_NORMAL) {
      TkDrawInsetFocusHighlight(tkwin, hgc, hl, Tk_WindowId(tkwin), 5);
    } else {
      Tk_DrawFocusHighlight(tkwin, hgc, hl, Tk_WindowId(tkwin));
    }
  }

  /* No final copy — drawing already occurred directly. */
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
 *
/
