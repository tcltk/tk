/*
 * tkUnixButton.c --
 *
 *	This file implements the Unix specific portion of the button widgets.
 *
 * Copyright © 1996-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkButton.h"
#include "tk3d.h"

/*
 * Shared with menu widget.
 */

MODULE_SCOPE void	TkpDrawCheckIndicator(Tk_Window tkwin,
			    Display *display, Drawable d, int x, int y,
			    Tk_3DBorder bgBorder, XColor *indicatorColor,
			    XColor *selectColor, XColor *disColor, int on,
			    int disabled, int mode);

/*
 * Declaration of Unix specific button structure.
 */

typedef struct UnixButton {
    TkButton info;		/* Generic button info. */
} UnixButton;

/*
 * The class function table for the button widgets.
 */

const Tk_ClassProcs tkpButtonProcs = {
    sizeof(Tk_ClassProcs),	/* size */
    TkButtonWorldChanged,	/* worldChangedProc */
    NULL,			/* createProc */
    NULL			/* modalProc */
};

/*
 * Indicator draw modes
 */

#define CHECK_BUTTON 0
#define CHECK_MENU   1
#define RADIO_BUTTON 2
#define RADIO_MENU   3

/*
 * Indicator sizes
 */

#define CHECK_BUTTON_DIM 16
#define CHECK_MENU_DIM    8
#define RADIO_BUTTON_DIM 16
#define RADIO_MENU_DIM    8

/*
 * The SVG images used here are based on some icons provided by the
 * official open source SVG icon library for the Bootstrap project,
 * licensed under the MIT license (https://opensource.org/licenses/MIT).
 *
 * See https://github.com/twbs/icons.
 */

static const char *const checkbtnOffData =
"    <svg id='classiccheckbox' width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n"
"  <defs id='defs325'>\n"
"    <linearGradient id='linearGradientLeft' gradientUnits='userSpaceOnUse'\n"
"		    x1='2' y1='7.5' x2='2' y2='7.5'>\n"
"      <stop style='stop-color:#DARKKK;stop-opacity:1;' offset='1' id='stop1885' />\n"
"      <stop style='stop-color:#000000;stop-opacity:1;' offset='0' id='stop1887' />\n"
"  </linearGradient>\n"
"  <linearGradient id='LinearGradientBottom' gradientUnits='userSpaceOnUse'\n"
"		  x1='7.5' y1='16' x2='7.5' y2='15'>\n"
"    <stop style='stop-color:#LIGHTT;stop-opacity:1;' offset='0' id='stop2672' />\n"
"    <stop style='stop-color:#ffffff;stop-opacity:1;' offset='1' id='stop2674' />\n"
"  </linearGradient>\n"
"  <linearGradient id='linearGradientRight' gradientUnits='userSpaceOnUse'\n"
"		  x1='16' y1='7.5' x2='16' y2='7.5'>\n"
"    <stop style='stop-color:#LIGHTT;stop-opacity:1;' offset='0' id='stop2672' />\n"
"    <stop style='stop-color:#ffffff;stop-opacity:1;' offset='1' id='stop2674' />\n"
"  </linearGradient>\n"
"  <linearGradient id='linearGradientTop' gradientUnits='userSpaceOnUse'\n"
"		  x1='7.5' y1='1.5' x2='7.5' y2='1.5'>\n"
"    <stop style='stop-color:#DARKKK;stop-opacity:1;' offset='1' id='stop1885' />\n"
"    <stop style='stop-color:#000000;stop-opacity:1;' offset='0' id='stop1887' />\n"
"  </linearGradient>\n"
"  </defs>\n"
"  <g id='layer1'>\n"
"    <rect id='rectbackdrop'\n"
"	  style='fill:#INTROR;stroke-width:11;paint-order:stroke markers fill;fill-opacity:1'\n"
"	  width='12' height='12' x='2' y='2' />\n"
"    <path id='recttopside'\n"
"	  style='stroke-width:10.16;paint-order:stroke markers fill;fill-opacity:1;fill:url(#linearGradientTop)'\n"
"	  d='M 0,0 L 13,0 12,1 1,1 0,0' />\n"
"    <path id='rectleftside'\n"
"	  style='stroke-width:10.16;paint-order:stroke markers fill;fill-opacity:1;fill:url(#linearGradientLeft)'\n"
"	  d='M 0,0 L 1,1 1,12 0,13' />\n"
"    <path id='rectrightside'\n"
"	  style='stroke-width:10.16;paint-order:stroke markers fill;fill-opacity:1;fill:url(#linearGradientRight)'\n"
"	  d='M 13,0 L 12,1 12,12 13,13 13,0' />\n"
"    <path id='rectbottomside'\n"
"	  style='stroke-width:10.16;paint-order:stroke markers fill;fill-opacity:1;fill:url(#LinearGradientBottom)'\n"
"	  d='M 0,13 L 1,12 12,12 13,13 0,13' />\n"
"  </g>\n"
"</svg>";

static const char *const checkbtnOnData =
"    <svg id='classiccheckbox' width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n"
"  <defs id='defs325'>\n"
"    <linearGradient id='linearGradientLeft' gradientUnits='userSpaceOnUse'\n"
"		    x1='2' y1='7.5' x2='2' y2='7.5'>\n"
"      <stop style='stop-color:#DARKKK;stop-opacity:1;' offset='1' id='stop1885' />\n"
"      <stop style='stop-color:#000000;stop-opacity:1;' offset='0' id='stop1887' />\n"
"  </linearGradient>\n"
"  <linearGradient id='LinearGradientBottom' gradientUnits='userSpaceOnUse'\n"
"		  x1='7.5' y1='16' x2='7.5' y2='15'>\n"
"    <stop style='stop-color:#LIGHTT;stop-opacity:1;' offset='0' id='stop2672' />\n"
"    <stop style='stop-color:#ffffff;stop-opacity:1;' offset='1' id='stop2674' />\n"
"  </linearGradient>\n"
"  <linearGradient id='linearGradientRight' gradientUnits='userSpaceOnUse'\n"
"		  x1='16' y1='7.5' x2='16' y2='7.5'>\n"
"    <stop style='stop-color:#LIGHTT;stop-opacity:1;' offset='0' id='stop2672' />\n"
"    <stop style='stop-color:#ffffff;stop-opacity:1;' offset='1' id='stop2674' />\n"
"  </linearGradient>\n"
"  <linearGradient id='linearGradientTop' gradientUnits='userSpaceOnUse'\n"
"		  x1='7.5' y1='1.5' x2='7.5' y2='1.5'>\n"
"    <stop style='stop-color:#DARKKK;stop-opacity:1;' offset='1' id='stop1885' />\n"
"    <stop style='stop-color:#000000;stop-opacity:1;' offset='0' id='stop1887' />\n"
"  </linearGradient>\n"
"  </defs>\n"
"  <g id='layer1'>\n"
"    <rect id='rectbackdrop'\n"
"	  style='fill:#INTROR;stroke-width:11;paint-order:stroke markers fill;fill-opacity:1'\n"
"	  width='12' height='12' x='2' y='2' />\n"
"    <path id='recttopside'\n"
"	  style='stroke-width:10.16;paint-order:stroke markers fill;fill-opacity:1;fill:url(#linearGradientTop)'\n"
"	  d='M 0,0 L 13,0 12,1 1,1 0,0' />\n"
"    <path id='rectleftside'\n"
"	  style='stroke-width:10.16;paint-order:stroke markers fill;fill-opacity:1;fill:url(#linearGradientLeft)'\n"
"	  d='M 0,0 L 1,1 1,12 0,13' />\n"
"    <path id='rectrightside'\n"
"	  style='stroke-width:10.16;paint-order:stroke markers fill;fill-opacity:1;fill:url(#linearGradientRight)'\n"
"	  d='M 13,0 L 12,1 12,12 13,13 13,0' />\n"
"    <path id='rectbottomside'\n"
"	  style='stroke-width:10.16;paint-order:stroke markers fill;fill-opacity:1;fill:url(#LinearGradientBottom)'\n"
"	  d='M 0,13 L 1,12 12,12 13,13 0,13' />\n"
"    <path id='indicator'\n"
"	  style='fill:none;stroke:#INDCTR;stroke-width:2px;stroke-linecap:butt;stroke-linejoin:miter;paint-order:stroke markers fill;stroke-opacity:1'\n"
"	  d='M 3,7 L 6,10 10,3' />\n"
"  </g>\n"
"</svg>";

static const char *const radiobtnOffData =
    "<svg width='16px' height='16px' version='1.1' id='radiobutton' xmlns:svg='http://www.w3.org/2000/svg'>\n"
    "  <defs id='defs4785'>\n"
    "    <linearGradient id='gradient1' x1='2' y1='2' x2='11' y2='11' gradientUnits='userSpaceOnUse'>\n"
    "      <stop id='stop0' style='stop-color:#DARKKK;stop-opacity:1;' offset='0' />\n"
    "      <stop id='stop1' style='stop-color:#LIGHTT;stop-opacity:0;' offset='1' />\n"
    "    </linearGradient>\n"
    "  </defs>\n"
    "  <circle id='path4803' cx='8' cy='8' r='8' style='fill:url(#gradient1);fill-opacity:1;fill-rule:evenodd;stroke-width:1;stroke-miterlimit:4;stroke-dasharray:none' />\n"
    "  <circle id='path4803-6' cx='8' cy='8' r='6.5' style='fill:#INTROR;fill-opacity:1;fill-rule:evenodd;stroke-width:1;stroke-miterlimit:4;stroke-dasharray:none'/>\n"
    "</svg>";

static const char *const radiobtnOnData =
    "<svg width='16px' height='16px' version='1.1' id='radiobutton' xmlns:svg='http://www.w3.org/2000/svg'>\n"
    "  <defs id='defs4785'>\n"
    "    <linearGradient id='gradient1' x1='2' y1='2' x2='11' y2='11' gradientUnits='userSpaceOnUse'>\n"
    "      <stop id='stop0' style='stop-color:#DARKKK;stop-opacity:1;' offset='0' />\n"
    "      <stop id='stop1' style='stop-color:#LIGHTT;stop-opacity:0;' offset='1' />\n"
    "    </linearGradient>\n"
    "  </defs>\n"
    "  <circle id='path4803' cx='8' cy='8' r='8' style='fill:url(#gradient1);fill-opacity:1;fill-rule:evenodd;stroke-width:1;stroke-miterlimit:4;stroke-dasharray:none' />\n"
    "  <circle id='path4803-6' cx='8' cy='8' r='7' style='fill:#INTROR;fill-opacity:1;fill-rule:evenodd;stroke-width:1;stroke-miterlimit:4;stroke-dasharray:none'/>\n"
    "  <circle id='path4820' cx='8' cy='8' r='4' style='fill:#INDCTR;fill-opacity:1;fill-rule:evenodd;stroke-width:1;stroke-miterlimit:4;stroke-dasharray:none'/>\n"
    "</svg>";

static const char *const menuOffData =
"    <svg width='8' height='8' version='1.1' viewBox='0 0 8 8' xmlns='http://www.w3.org/2000/svg'></svg>\n";

static const char *const checkmenuOnData =
"    <svg width='8' height='8' version='1.1' viewBox='0 0 8 8' xmlns='http://www.w3.org/2000/svg'>\n"
"    <path id='indicator'\n"
"	  style='fill:none;stroke:#INDCTR;stroke-width:2px;stroke-linecap:butt;stroke-linejoin:miter;paint-order:stroke markers fill;stroke-opacity:1'\n"
"	  d='M 0,3 L 4,7 8,0' />\n"
"    </svg>";

static const char *const radiomenuOnData =
"    <svg width='8' height='8' version='1.1' viewBox='0 0 8 8' xmlns='http://www.w3.org/2000/svg'>\n"
"  <circle id='path4820' cx='4' cy='4' r='3' style='fill:#INDCTR;fill-opacity:1;fill-rule:evenodd;stroke-width:1;stroke-miterlimit:4;stroke-dasharray:none'/>\n"
"    </svg>";


/*
 *----------------------------------------------------------------------
 *
 * TkpDrawCheckIndicator -
 *
 *	Draws the checkbox image in the drawable at the (x,y) location, value,
 *	and state given. This routine is used by the button and menu widgets.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	An image is drawn in the drawable at the location given.
 *
 *----------------------------------------------------------------------
 */

static void
ColorToStr(
    const XColor *colorPtr,	/* specifies a color */
    char *colorStr)		/* memory area to which the color is to be
				   output in the format "RRGGBB" */
{
    char str[13];

    snprintf(str, sizeof(str), "%04x%04x%04x",
	     colorPtr->red, colorPtr->green, colorPtr->blue);
    snprintf(colorStr, 7, "%.2s%.2s%.2s", str, str + 4, str + 8);
}

static void
ImageChanged(			/* to be passed to Tk_GetImage() */
    ClientData clientData,
    int x, int y, int width, int height,
    int imageWidth, int imageHeight)
{
    (void)clientData;
    (void)x; (void)y; (void)width; (void)height;
    (void)imageWidth; (void)imageHeight;
}

void
TkpDrawCheckIndicator(
    Tk_Window tkwin,		/* handle for resource alloc */
    Display *display,
    Drawable d,			/* what to draw on */
    int x, int y,		/* where to draw */
    Tk_3DBorder bgBorder,	/* colors of the border */
    XColor *indicatorColor,	/* color of the indicator */
    XColor *selectColor,	/* color when selected */
    XColor *disableColor,	/* color when disabled */
    int on,			/* are we on? */
    int disabled,		/* are we disabled? */
    int mode)			/* kind of indicator to draw */
{
    const char *svgDataPtr;
    int hasBorder, hasInterior, dim;
    TkBorder *bg_brdr = (TkBorder*)bgBorder;
    char borderColorStr[7], lightColorStr[7], interiorColorStr[7], indicatorColorStr[7];
    char imgName[50];
    Tk_Image img;
    size_t svgDataLen;
    char *svgDataCopy;
    char *borderColorPtr, *lightColorPtr, *interiorColorPtr, *indicatorColorPtr;
    Tcl_Interp *interp = Tk_Interp(tkwin);
    char *script;
    int code;
    const char *scalingPctPtr;
    double scalingFactor;

    /*
     * Sanity check
     */

    if (tkwin == NULL || display == NULL || d == None || bgBorder == NULL
	    || indicatorColor == NULL) {
	return;
    }

    if (disableColor == NULL) {
	disableColor = bg_brdr->bgColorPtr;
    }

    if (selectColor == NULL) {
	selectColor = bg_brdr->bgColorPtr;
    }

    /*
     * Determine the SVG data to use for the
     * photo image and the latter's dimensions
     */

    switch (mode) {
    default:
    case CHECK_BUTTON:
	svgDataPtr = (on == 0 ? checkbtnOffData : checkbtnOnData);
	hasBorder = 1; hasInterior = 1;
	dim = CHECK_BUTTON_DIM;
	break;

    case CHECK_MENU:
	svgDataPtr = (on == 0 ? menuOffData : checkmenuOnData);
	hasBorder = 0; hasInterior = 0;
	dim = CHECK_MENU_DIM;
	break;

    case RADIO_BUTTON:
	svgDataPtr = (on == 0 ? radiobtnOffData : radiobtnOnData);
	hasBorder = 1; hasInterior = 1;
	dim = RADIO_BUTTON_DIM;
	break;

    case RADIO_MENU:
	svgDataPtr = (on == 0 ? menuOffData : radiomenuOnData);
	hasBorder = 0; hasInterior = 0;
	dim = RADIO_MENU_DIM;
	break;
    }

    /*
     * Construct the color strings borderColorStr,
     * interiorColorStr, and indicatorColorStr
     */

    TkpGetShadows(bg_brdr, tkwin);

    if (bg_brdr->darkColorPtr == NULL) {
	strcpy(borderColorStr, "000000");
    } else {
	ColorToStr(Tk_GetColorByValue(tkwin, bg_brdr->darkColorPtr),
		   borderColorStr);
    }
    if (bg_brdr->lightColorPtr == NULL) {
	strcpy(lightColorStr, "ffffff");
    } else {
	ColorToStr(Tk_GetColorByValue(tkwin, bg_brdr->lightColorPtr),
		   lightColorStr);
    }
    if (on == 2 || disabled) {			/* tri-state or disabled */
	ColorToStr(Tk_GetColorByValue(tkwin, bg_brdr->bgColorPtr),
		   interiorColorStr);
	ColorToStr(Tk_GetColorByValue(tkwin, disableColor),
		   indicatorColorStr);
    } else {
	ColorToStr(Tk_GetColorByValue(tkwin, selectColor),
		   interiorColorStr);
	ColorToStr(Tk_GetColorByValue(tkwin, indicatorColor),
		   indicatorColorStr);
    }

    /*
     * Check whether there is an SVG image for
     * the value of mode and these color strings
     */

    snprintf(imgName, sizeof(imgName), "::tk::icons::indicator%d_%s_%s_%s",
	     mode,
	     hasBorder ? borderColorStr : "XXXXXX",
	     hasInterior ? interiorColorStr : "XXXXXX",
	     on ? indicatorColorStr : "XXXXXX");
    img = Tk_GetImage(interp, tkwin, imgName, ImageChanged, NULL);
    if (img == NULL) {
	/*
	 * Copy the string pointed to by svgDataPtr to a newly allocated memory
	 * area svgDataCopy and assign the latter's address to svgDataPtr
	 */

	svgDataLen = strlen(svgDataPtr);
	svgDataCopy = (char *)attemptckalloc(svgDataLen + 1);
	if (svgDataCopy == NULL) {
	    return;
	}
	memcpy(svgDataCopy, svgDataPtr, svgDataLen);
	svgDataCopy[svgDataLen] = '\0';
	svgDataPtr = svgDataCopy;

	/*
	 * Update the colors within svgDataCopy
	 */

	borderColorPtr =    strstr(svgDataPtr, "DARKKK"); //888888
	lightColorPtr =     strstr(svgDataPtr, "LIGHTT"); //
	interiorColorPtr =  strstr(svgDataPtr, "INTROR"); //ffffff
	indicatorColorPtr = strstr(svgDataPtr, "INDCTR"); //000000

	while (borderColorPtr != NULL) {
	    memcpy(borderColorPtr, borderColorStr, 6);
	    borderColorPtr = strstr(borderColorPtr, "DARKKK"); //888888
	}
	while (lightColorPtr != NULL) {
	    memcpy(lightColorPtr, lightColorStr, 6);
	    lightColorPtr = strstr(lightColorPtr, "LIGHTT"); //888888
	}
	while (interiorColorPtr != NULL) {
	    memcpy(interiorColorPtr, interiorColorStr, 6);
	    interiorColorPtr =  strstr(interiorColorPtr, "INTROR"); //ffffff
	}
	while (indicatorColorPtr != NULL) {
	    memcpy(indicatorColorPtr, indicatorColorStr, 6);
	    indicatorColorPtr = strstr(indicatorColorPtr, "INDCTR"); //000000
	}

	/*
	 * Create an SVG photo image from svgDataCopy
	 */
	const char *cmdfmt = "image create photo %s -format $::tk::svgFmt -data {%s}";
	size_t scriptsz = strlen(cmdfmt) + strlen(imgName) + svgDataLen;
	script = (char *)attemptckalloc(scriptsz);
	if (script == NULL) {
	    ckfree(svgDataCopy);
	    return;
	}
	snprintf(script, scriptsz, cmdfmt, imgName, svgDataCopy);
	ckfree(svgDataCopy);
	code = Tcl_EvalEx(interp, script, -1, TCL_EVAL_GLOBAL);
	ckfree(script);
	if (code != TCL_OK) {
	    Tcl_BackgroundException(interp, code);
	    return;
	}
	img = Tk_GetImage(interp, tkwin, imgName, ImageChanged, NULL);
    }

    /*
     * Retrieve the scaling factor and multiply dim by it
     */

    scalingPctPtr = Tcl_GetVar(interp, "::tk::scalingPct", TCL_GLOBAL_ONLY);
    if (scalingPctPtr == NULL) {
	scalingPctPtr = "100";
    }
    scalingFactor = atof(scalingPctPtr) / 100;	/* 1.0, 1.25, 1.5, 1.75, ... */
    dim *= scalingFactor;

    /*
     * Adjust the image's coordinates in the drawable and display the image
     */

    x -= dim/2;
    y -= dim/2;
    Tk_RedrawImage(img, 0, 0, dim, dim, d, x, y);
    Tk_FreeImage(img);

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
 *	Registers an event handler for the widget.
 *
 *----------------------------------------------------------------------
 */

TkButton *
TkpCreateButton(
    TCL_UNUSED(Tk_Window))
{
    return (TkButton *)ckalloc(sizeof(UnixButton));
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayButton --
 *
 *	This function is invoked to display a button widget. It is normally
 *	invoked as an idle handler.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Commands are output to X to display the button in its current mode.
 *	The REDRAW_PENDING flag is cleared.
 *
 *----------------------------------------------------------------------
 */

static void
ShiftByOffset(
    TkButton *butPtr,
    int relief,
    int *x,		/* shift this x coordinate */
    int *y,		/* shift this y coordinate */
    int width,		/* width of image/text */
    int height)		/* height of image/text */
{
    if (relief != TK_RELIEF_RAISED
	    && butPtr->type == TYPE_BUTTON
	    && !Tk_StrictMotif(butPtr->tkwin)) {
	int shiftX;
	int shiftY;

	/*
	 * This is an (unraised) button widget, so we offset the text to make
	 * the button appear to move up and down as the relief changes.
	 */

	shiftX = shiftY = (relief == TK_RELIEF_SUNKEN) ? 2 : 1;

	if (relief != TK_RELIEF_RIDGE) {
	    /*
	     * Take back one pixel if the padding is even, otherwise the
	     * content will be displayed too far right/down.
	     */

	    if ((Tk_Width(butPtr->tkwin) - width) % 2 == 0) {
		shiftX -= 1;
	    }
	    if ((Tk_Height(butPtr->tkwin) - height) % 2 == 0) {
		shiftY -= 1;
	    }
	}

	*x += shiftX;
	*y += shiftY;
    }
}

void
TkpDisplayButton(
    ClientData clientData)	/* Information about widget. */
{
    TkButton *butPtr = (TkButton *)clientData;
    GC gc;
    Tk_3DBorder border;
    Pixmap pixmap;
    int x = 0;			/* Initialization only needed to stop compiler
				 * warning. */
    int y, relief;
    Tk_Window tkwin = butPtr->tkwin;
    int width = 0, height = 0, fullWidth, fullHeight;
    int textXOffset, textYOffset;
    int haveImage = 0, haveText = 0;
    int imageWidth, imageHeight;
    int imageXOffset = 0, imageYOffset = 0;
				/* image information that will be used to
				 * restrict disabled pixmap as well */

    butPtr->flags &= ~REDRAW_PENDING;
    if ((butPtr->tkwin == NULL) || !Tk_IsMapped(tkwin)) {
	return;
    }

    border = butPtr->normalBorder;
    if ((butPtr->state == STATE_DISABLED) && (butPtr->disabledFg != NULL)) {
	gc = butPtr->disabledGC;
    } else if ((butPtr->state == STATE_ACTIVE)
	    && !Tk_StrictMotif(butPtr->tkwin)) {
	gc = butPtr->activeTextGC;
	border = butPtr->activeBorder;
    } else {
	gc = butPtr->normalTextGC;
    }
    if ((butPtr->flags & SELECTED) && (butPtr->selectBorder != NULL)
	    && !butPtr->indicatorOn) {
	border = butPtr->selectBorder;
    }

    /*
     * Override the relief specified for the button if this is a checkbutton
     * or radiobutton and there's no indicator. The new relief is as follows:
     *      If the button is select  --> "sunken"
     *      If relief==overrelief    --> relief
     *      Otherwise                --> overrelief
     *
     * The effect we are trying to achieve is as follows:
     *
     *      value    mouse-over?   -->   relief
     *     -------  ------------        --------
     *       off        no               flat
     *       off        yes              raised
     *       on         no               sunken
     *       on         yes              sunken
     *
     * This is accomplished by configuring the checkbutton or radiobutton like
     * this:
     *
     *     -indicatoron 0 -overrelief raised -offrelief flat
     *
     * Bindings (see library/button.tcl) will copy the -overrelief into
     * -relief on mouseover. Hence, we can tell if we are in mouse-over by
     * comparing relief against overRelief. This is an aweful kludge, but it
     * gives use the desired behavior while keeping the code backwards
     * compatible.
     */

    relief = butPtr->relief;
    if ((butPtr->type >= TYPE_CHECK_BUTTON) && !butPtr->indicatorOn) {
	if (butPtr->flags & SELECTED) {
	    relief = TK_RELIEF_SUNKEN;
	} else if (butPtr->overRelief != relief) {
	    relief = butPtr->offRelief;
	}
    }

    /*
     * In order to avoid screen flashes, this function redraws the button in a
     * pixmap, then copies the pixmap to the screen in a single operation.
     * This means that there's no point in time where the on-screen image has
     * been cleared.
     */

    pixmap = Tk_GetPixmap(butPtr->display, Tk_WindowId(tkwin),
	    Tk_Width(tkwin), Tk_Height(tkwin), Tk_Depth(tkwin));
    Tk_Fill3DRectangle(tkwin, pixmap, border, 0, 0, Tk_Width(tkwin),
	    Tk_Height(tkwin), 0, TK_RELIEF_FLAT);

    /*
     * Display image or bitmap or text for button.
     */

    if (butPtr->image != NULL) {
	Tk_SizeOfImage(butPtr->image, &width, &height);
	haveImage = 1;
    } else if (butPtr->bitmap != None) {
	Tk_SizeOfBitmap(butPtr->display, butPtr->bitmap, &width, &height);
	haveImage = 1;
    }
    imageWidth = width;
    imageHeight = height;

    haveText = (butPtr->textWidth != 0 && butPtr->textHeight != 0);

    if (butPtr->compound != COMPOUND_NONE && haveImage && haveText) {
	textXOffset = 0;
	textYOffset = 0;
	fullWidth = 0;
	fullHeight = 0;

	switch ((enum compound) butPtr->compound) {
	case COMPOUND_TOP:
	case COMPOUND_BOTTOM:
	    /*
	     * Image is above or below text.
	     */

	    if (butPtr->compound == COMPOUND_TOP) {
		textYOffset = height + butPtr->padY;
	    } else {
		imageYOffset = butPtr->textHeight + butPtr->padY;
	    }
	    fullHeight = height + butPtr->textHeight + butPtr->padY;
	    fullWidth = (width > butPtr->textWidth ? width :
		    butPtr->textWidth);
	    textXOffset = (fullWidth - butPtr->textWidth)/2;
	    imageXOffset = (fullWidth - width)/2;
	    break;
	case COMPOUND_LEFT:
	case COMPOUND_RIGHT:
	    /*
	     * Image is left or right of text.
	     */

	    if (butPtr->compound == COMPOUND_LEFT) {
		textXOffset = width + butPtr->padX;
	    } else {
		imageXOffset = butPtr->textWidth + butPtr->padX;
	    }
	    fullWidth = butPtr->textWidth + butPtr->padX + width;
	    fullHeight = (height > butPtr->textHeight ? height :
		    butPtr->textHeight);
	    textYOffset = (fullHeight - butPtr->textHeight)/2;
	    imageYOffset = (fullHeight - height)/2;
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

	TkComputeAnchor(butPtr->anchor, tkwin, butPtr->padX, butPtr->padY,
		butPtr->indicatorSpace + fullWidth, fullHeight, &x, &y);

	x += butPtr->indicatorSpace;
	ShiftByOffset(butPtr, relief, &x, &y, width, height);
	imageXOffset += x;
	imageYOffset += y;

	if (butPtr->image != NULL) {
	    /*
	     * Do boundary clipping, so that Tk_RedrawImage is passed valid
	     * coordinates. [Bug 979239]
	     */

	    if (imageXOffset < 0) {
		imageXOffset = 0;
	    }
	    if (imageYOffset < 0) {
		imageYOffset = 0;
	    }
	    if (width > Tk_Width(tkwin)) {
		width = Tk_Width(tkwin);
	    }
	    if (height > Tk_Height(tkwin)) {
		height = Tk_Height(tkwin);
	    }
	    if ((width + imageXOffset) > Tk_Width(tkwin)) {
		imageXOffset = Tk_Width(tkwin) - width;
	    }
	    if ((height + imageYOffset) > Tk_Height(tkwin)) {
		imageYOffset = Tk_Height(tkwin) - height;
	    }

	    if ((butPtr->selectImage != NULL) && (butPtr->flags & SELECTED)) {
		Tk_RedrawImage(butPtr->selectImage, 0, 0,
			width, height, pixmap, imageXOffset, imageYOffset);
	    } else if ((butPtr->tristateImage != NULL) && (butPtr->flags & TRISTATED)) {
		Tk_RedrawImage(butPtr->tristateImage, 0, 0,
			width, height, pixmap, imageXOffset, imageYOffset);
	    } else {
		Tk_RedrawImage(butPtr->image, 0, 0, width,
			height, pixmap, imageXOffset, imageYOffset);
	    }
	} else {
	    XSetClipOrigin(butPtr->display, gc, imageXOffset, imageYOffset);
	    XCopyPlane(butPtr->display, butPtr->bitmap, pixmap, gc,
		    0, 0, (unsigned int) width, (unsigned int) height,
		    imageXOffset, imageYOffset, 1);
	    XSetClipOrigin(butPtr->display, gc, 0, 0);
	}

	Tk_DrawTextLayout(butPtr->display, pixmap, gc,
		butPtr->textLayout, x + textXOffset, y + textYOffset, 0, -1);
	Tk_UnderlineTextLayout(butPtr->display, pixmap, gc,
		butPtr->textLayout, x + textXOffset, y + textYOffset,
		butPtr->underline);
	y += fullHeight/2;
    } else {
	if (haveImage) {
	    TkComputeAnchor(butPtr->anchor, tkwin, 0, 0,
		    butPtr->indicatorSpace + width, height, &x, &y);
	    x += butPtr->indicatorSpace;
	    ShiftByOffset(butPtr, relief, &x, &y, width, height);
	    imageXOffset += x;
	    imageYOffset += y;
	    if (butPtr->image != NULL) {
		/*
		 * Do boundary clipping, so that Tk_RedrawImage is passed
		 * valid coordinates. [Bug 979239]
		 */

		if (imageXOffset < 0) {
		    imageXOffset = 0;
		}
		if (imageYOffset < 0) {
		    imageYOffset = 0;
		}
		if (width > Tk_Width(tkwin)) {
		    width = Tk_Width(tkwin);
		}
		if (height > Tk_Height(tkwin)) {
		    height = Tk_Height(tkwin);
		}
		if ((width + imageXOffset) > Tk_Width(tkwin)) {
		    imageXOffset = Tk_Width(tkwin) - width;
		}
		if ((height + imageYOffset) > Tk_Height(tkwin)) {
		    imageYOffset = Tk_Height(tkwin) - height;
		}

		if ((butPtr->selectImage != NULL) &&
			(butPtr->flags & SELECTED)) {
		    Tk_RedrawImage(butPtr->selectImage, 0, 0, width,
			    height, pixmap, imageXOffset, imageYOffset);
		} else if ((butPtr->tristateImage != NULL) &&
			(butPtr->flags & TRISTATED)) {
		    Tk_RedrawImage(butPtr->tristateImage, 0, 0, width,
			    height, pixmap, imageXOffset, imageYOffset);
		} else {
		    Tk_RedrawImage(butPtr->image, 0, 0, width, height, pixmap,
			    imageXOffset, imageYOffset);
		}
	    } else {
		XSetClipOrigin(butPtr->display, gc, x, y);
		XCopyPlane(butPtr->display, butPtr->bitmap, pixmap, gc, 0, 0,
			(unsigned int) width, (unsigned int) height, x, y, 1);
		XSetClipOrigin(butPtr->display, gc, 0, 0);
	    }
	    y += height/2;
	} else {
 	    TkComputeAnchor(butPtr->anchor, tkwin, butPtr->padX, butPtr->padY,
		    butPtr->indicatorSpace + butPtr->textWidth,
		    butPtr->textHeight, &x, &y);

	    x += butPtr->indicatorSpace;
	    ShiftByOffset(butPtr, relief, &x, &y, width, height);
	    Tk_DrawTextLayout(butPtr->display, pixmap, gc, butPtr->textLayout,
		    x, y, 0, -1);
	    Tk_UnderlineTextLayout(butPtr->display, pixmap, gc,
		    butPtr->textLayout, x, y, butPtr->underline);
	    y += butPtr->textHeight/2;
	}
    }

    /*
     * Draw the indicator for check buttons and radio buttons. At this point,
     * x and y refer to the top-left corner of the text or image or bitmap.
     */

    if (((butPtr->type == TYPE_CHECK_BUTTON) || (butPtr->type == TYPE_RADIO_BUTTON))
	&& butPtr->indicatorOn) {
	if (butPtr->indicatorDiameter > 2*butPtr->borderWidth) {
	    TkBorder *selBorder = (TkBorder *) butPtr->selectBorder;
	    XColor *selColor = NULL;

	    if (selBorder != NULL) {
		selColor = selBorder->bgColorPtr;
	    }
	    x -= butPtr->indicatorSpace/2;
	    y = Tk_Height(tkwin)/2 + butPtr->borderWidth*2;
	    int btype = (butPtr->type == TYPE_CHECK_BUTTON ?
			 CHECK_BUTTON : RADIO_BUTTON);
	    TkpDrawCheckIndicator(tkwin, butPtr->display, pixmap, x, y,
		    border, butPtr->normalFg, selColor, butPtr->disabledFg,
		    ((butPtr->flags & SELECTED) ? 1 :
		     (butPtr->flags & TRISTATED) ? 2 : 0),
		     (butPtr->state == STATE_DISABLED), btype);
	}
    }

    /*
     * If the button is disabled with a stipple rather than a special
     * foreground color, generate the stippled effect. If the widget is
     * selected and we use a different background color when selected, must
     * temporarily modify the GC so the stippling is the right color.
     */

    if ((butPtr->state == STATE_DISABLED)
	    && ((butPtr->disabledFg == NULL) || (butPtr->image != NULL))) {
	if ((butPtr->flags & SELECTED) && !butPtr->indicatorOn
		&& (butPtr->selectBorder != NULL)) {
	    XSetForeground(butPtr->display, butPtr->stippleGC,
		    Tk_3DBorderColor(butPtr->selectBorder)->pixel);
	}

	/*
	 * Stipple the whole button if no disabledFg was specified, otherwise
	 * restrict stippling only to displayed image
	 */

	if (butPtr->disabledFg == NULL) {
	    XFillRectangle(butPtr->display, pixmap, butPtr->stippleGC, 0, 0,
		    (unsigned) Tk_Width(tkwin), (unsigned) Tk_Height(tkwin));
	} else {
	    XFillRectangle(butPtr->display, pixmap, butPtr->stippleGC,
		    imageXOffset, imageYOffset,
		    (unsigned) imageWidth, (unsigned) imageHeight);
	}
	if ((butPtr->flags & SELECTED) && !butPtr->indicatorOn
		&& (butPtr->selectBorder != NULL)) {
	    XSetForeground(butPtr->display, butPtr->stippleGC,
		    Tk_3DBorderColor(butPtr->normalBorder)->pixel);
	}
    }

    /*
     * Draw the border and traversal highlight last. This way, if the button's
     * contents overflow they'll be covered up by the border. This code is
     * complicated by the possible combinations of focus highlight and default
     * rings. We draw the focus and highlight rings using the highlight border
     * and highlight foreground color.
     */

    if (relief != TK_RELIEF_FLAT) {
	int inset = butPtr->highlightWidth;

	if (butPtr->defaultState == DEFAULT_ACTIVE) {
	    /*
	     * Draw the default ring with 2 pixels of space between the
	     * default ring and the button and the default ring and the focus
	     * ring. Note that we need to explicitly draw the space in the
	     * highlightBorder color to ensure that we overwrite any overflow
	     * text and/or a different button background color.
	     */

	    Tk_Draw3DRectangle(tkwin, pixmap, butPtr->highlightBorder, inset,
		    inset, Tk_Width(tkwin) - 2*inset,
		    Tk_Height(tkwin) - 2*inset, 2, TK_RELIEF_FLAT);
	    inset += 2;
	    Tk_Draw3DRectangle(tkwin, pixmap, butPtr->highlightBorder, inset,
		    inset, Tk_Width(tkwin) - 2*inset,
		    Tk_Height(tkwin) - 2*inset, 1, TK_RELIEF_SUNKEN);
	    inset++;
	    Tk_Draw3DRectangle(tkwin, pixmap, butPtr->highlightBorder, inset,
		    inset, Tk_Width(tkwin) - 2*inset,
		    Tk_Height(tkwin) - 2*inset, 2, TK_RELIEF_FLAT);

	    inset += 2;
	} else if (butPtr->defaultState == DEFAULT_NORMAL) {
	    /*
	     * Leave room for the default ring and write over any text or
	     * background color.
	     */

	    Tk_Draw3DRectangle(tkwin, pixmap, butPtr->highlightBorder, 0,
		    0, Tk_Width(tkwin), Tk_Height(tkwin), 5, TK_RELIEF_FLAT);
	    inset += 5;
	}

	/*
	 * Draw the button border.
	 */

	Tk_Draw3DRectangle(tkwin, pixmap, border, inset, inset,
		Tk_Width(tkwin) - 2*inset, Tk_Height(tkwin) - 2*inset,
		butPtr->borderWidth, relief);
    }
    if (butPtr->highlightWidth > 0) {
	if (butPtr->flags & GOT_FOCUS) {
	    gc = Tk_GCForColor(butPtr->highlightColorPtr, pixmap);
	} else {
	    gc = Tk_GCForColor(Tk_3DBorderColor(butPtr->highlightBorder),
		    pixmap);
	}

	/*
	 * Make sure the focus ring shrink-wraps the actual button, not the
	 * padding space left for a default ring.
	 */

	if (butPtr->defaultState == DEFAULT_NORMAL) {
	    TkDrawInsetFocusHighlight(tkwin, gc, butPtr->highlightWidth,
		    pixmap, 5);
	} else {
	    Tk_DrawFocusHighlight(tkwin, gc, butPtr->highlightWidth, pixmap);
	}
    }

    /*
     * Copy the information from the off-screen pixmap onto the screen, then
     * delete the pixmap.
     */

    XCopyArea(butPtr->display, pixmap, Tk_WindowId(tkwin),
	    butPtr->copyGC, 0, 0, (unsigned) Tk_Width(tkwin),
	    (unsigned) Tk_Height(tkwin), 0, 0);
    Tk_FreePixmap(butPtr->display, pixmap);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpComputeButtonGeometry --
 *
 *	After changes in a button's text or bitmap, this function recomputes
 *	the button's geometry and passes this information along to the
 *	geometry manager for the window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The button's window may change size.
 *
 *----------------------------------------------------------------------
 */

void
TkpComputeButtonGeometry(
    TkButton *butPtr)	/* Button whose geometry may have changed. */
{
    int width, height, avgWidth, txtWidth, txtHeight;
    int haveImage = 0, haveText = 0;
    Tk_FontMetrics fm;

    butPtr->inset = butPtr->highlightWidth + butPtr->borderWidth;

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
		Tcl_GetString(butPtr->textPtr), -1, butPtr->wrapLength,
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

	    height += txtHeight + butPtr->padY;
	    width = (width > txtWidth ? width : txtWidth);
	    break;
	case COMPOUND_LEFT:
	case COMPOUND_RIGHT:
	    /*
	     * Image is left or right of text.
	     */

	    width += txtWidth + butPtr->padX;
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
	if (butPtr->width > 0) {
	    width = butPtr->width;
	}
	if (butPtr->height > 0) {
	    height = butPtr->height;
	}

	if ((butPtr->type >= TYPE_CHECK_BUTTON) && butPtr->indicatorOn) {
	    butPtr->indicatorSpace = height;
	    if (butPtr->type == TYPE_CHECK_BUTTON) {
		butPtr->indicatorDiameter = (65*height)/100;
	    } else {
		butPtr->indicatorDiameter = (75*height)/100;
	    }
	}

	width += 2*butPtr->padX;
	height += 2*butPtr->padY;
    } else {
	if (haveImage) {
	    if (butPtr->width > 0) {
		width = butPtr->width;
	    }
	    if (butPtr->height > 0) {
		height = butPtr->height;
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

	    if (butPtr->width > 0) {
		width = butPtr->width * avgWidth;
	    }
	    if (butPtr->height > 0) {
		height = butPtr->height * fm.linespace;
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
	width += 2*butPtr->padX;
	height += 2*butPtr->padY;
    }
    if ((butPtr->type == TYPE_BUTTON) && !Tk_StrictMotif(butPtr->tkwin)) {
	width += 2;
	height += 2;
    }
    Tk_GeometryRequest(butPtr->tkwin, (int) (width + butPtr->indicatorSpace
	    + 2*butPtr->inset), (int) (height + 2*butPtr->inset));
    Tk_SetInternalBorder(butPtr->tkwin, butPtr->inset);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
