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

static const char *const checkbtnOffData = "\
    <svg width='16' height='16' version='1.1' viewBox='0 0 16 16' xmlns='http://www.w3.org/2000/svg'>\n\
     <path d='M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1h12zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2H2z' fill='#888888'/>\n\
     <path d='m1.8751 14.98c-0.39054-0.057705-0.70232-0.3244-0.82173-0.70291l-0.042521-0.13478v-12.284l0.042521-0.13478c0.10182-0.32272 0.34737-0.56852 0.66907-0.66974l0.13558-0.042656h12.284l0.13558 0.042656c0.3217 0.10122 0.56726 0.34702 0.66907 0.66974l0.04252 0.13478v12.284l-0.04266 0.13558c-0.09944 0.31605-0.34012 0.55996-0.66011 0.66896l-0.12515 0.04263-6.0939 0.0025c-3.3516 0.0014-6.1382-4e-3 -6.1923-0.01202z' fill='#ffffff' stroke-width='.019254'/>\n\
    </svg>";

static const char *const checkbtnOnData = "\
    <svg width='16' height='16' version='1.1' viewBox='0 0 16 16' xmlns='http://www.w3.org/2000/svg'>\n\
     <path d='M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1h12zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2H2z' fill='#888888'/>\n\
     <path d='m1.8667 14.978c-0.3928-0.058267-0.69878-0.32745-0.82046-0.72179-0.03404-0.11032-0.035392-0.34935-0.035392-6.2567s0.00135-6.1464 0.035392-6.2567c0.057631-0.18677 0.12601-0.30099 0.26105-0.43603s0.24925-0.20342 0.43603-0.26105c0.11032-0.034041 0.34963-0.035392 6.2663-0.035392 5.9801 0 6.1546 0.00101 6.2575 0.036357 0.18858 0.064745 0.2957 0.13021 0.42558 0.26009 0.13504 0.13504 0.20342 0.24925 0.26105 0.43603 0.03404 0.11032 0.03539 0.34935 0.03539 6.2567s-0.0014 6.1464-0.03539 6.2567c-0.05763 0.18677-0.12601 0.30099-0.26105 0.43603-0.13084 0.13084-0.2404 0.19792-0.42558 0.2606-0.10232 0.03463-0.31149 0.03593-6.1998 0.03843-3.3516 0.0014-6.1419-0.0045-6.2007-0.01327zm5.9504-3.7936c0.068833-0.03212 0.15018-0.08171 0.18077-0.11019 0.10388-0.096743 4.1359-5.1488 4.1807-5.2383 0.14238-0.28456 0.081119-0.64471-0.14876-0.87459-0.29134-0.29134-0.79006-0.2863-1.0668 0.010798-0.051764 0.055567-0.85657 1.0733-1.7885 2.2617l-1.6943 2.1606-1.0975-1.0943c-1.2758-1.272-1.1905-1.2054-1.5406-1.2054-0.2003-5.78e-5 -0.21915 0.00342-0.33897 0.062351-0.15277 0.07515-0.28793 0.21278-0.36003 0.36661-0.04572 0.097547-0.051602 0.13318-0.051602 0.31267 0 0.35005-0.099878 0.2293 1.5539 1.8787 0.98662 0.98399 1.4543 1.4376 1.5075 1.462 0.22198 0.10206 0.45583 0.10466 0.66426 0.0074z' fill='#ffffff' stroke-width='.019254'/>\n\
     <path d='M10.97 4.97a.75.75 0 0 1 1.071 1.05l-3.992 4.99a.75.75 0 0 1-1.08.02L4.324 8.384a.75.75 0 1 1 1.06-1.06l2.094 2.093 3.473-4.425a.235.235 0 0 1 .02-.022z' fill='#000000'/>\n\
    </svg>";

static const char *const radiobtnOffData = "\
    <svg width='16' height='16' version='1.1' viewBox='0 0 16 16' xmlns='http://www.w3.org/2000/svg'>\n\
     <path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14zm0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16z' fill='#888888'/>\n\
     <path d='m7.6149 14.979c-0.95143-0.052435-1.8251-0.28345-2.6955-0.71278-1.2049-0.59425-2.1287-1.4307-2.865-2.5939-0.17549-0.27726-0.48912-0.91628-0.60706-1.2369-0.78448-2.1325-0.50124-4.4748 0.76831-6.3538 1.0215-1.5118 2.623-2.5782 4.4077-2.9349 1.8284-0.36541 3.7204 0.011412 5.2659 1.0488 1.5781 1.0592 2.6631 2.7202 2.993 4.5816 0.13747 0.77566 0.13747 1.6696 0 2.4452-0.44509 2.5113-2.2593 4.6096-4.6777 5.4101-0.82562 0.2733-1.7275 0.39395-2.5897 0.34644z' fill='#ffffff' stroke-width='.019254'/>\n\
    </svg>";

static const char *const radiobtnOnData = "\
    <svg width='16' height='16' version='1.1' viewBox='0 0 16 16' xmlns='http://www.w3.org/2000/svg'>\n\
     <path d='M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14zm0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16z' fill='#888888'/>\n\
     <path d='m7.6149 14.979c-0.95143-0.052435-1.8251-0.28345-2.6955-0.71278-1.2049-0.59425-2.1287-1.4307-2.865-2.5939-0.17549-0.27726-0.48912-0.91628-0.60706-1.2369-0.78448-2.1325-0.50124-4.4748 0.76831-6.3538 1.0215-1.5118 2.623-2.5782 4.4077-2.9349 1.8284-0.36541 3.7204 0.011412 5.2659 1.0488 1.5781 1.0592 2.6631 2.7202 2.993 4.5816 0.13747 0.77566 0.13747 1.6696 0 2.4452-0.44509 2.5113-2.2593 4.6096-4.6777 5.4101-0.82562 0.2733-1.7275 0.39395-2.5897 0.34644zm0.6788-3.9854c0.90879-0.095742 1.7099-0.57365 2.2051-1.3155 0.52137-0.78096 0.64871-1.7461 0.34934-2.6476-0.29006-0.87344-1.007-1.5895-1.8827-1.8802-0.90194-0.29948-1.8501-0.17323-2.6406 0.35159-0.43748 0.29046-0.77946 0.67865-1.0192 1.157-0.41915 0.83615-0.416 1.8703 0.0082129 2.6961 0.052793 0.10278 0.13621 0.24712 0.18537 0.32077 0.61057 0.91468 1.7081 1.4323 2.7945 1.3178z' fill='#ffffff' stroke-width='.019254'/>\n\
     <path d='m11 8a3 3 0 1 1-6 0 3 3 0 0 1 6 0z' fill='#000000'/>\n\
    </svg>";

static const char *const menuOffData = "\
    <svg width='8' height='8' version='1.1' viewBox='0 0 8 8' xmlns='http://www.w3.org/2000/svg'></svg>";

static const char *const checkmenuOnData = "\
    <svg width='8' height='8' version='1.1' viewBox='0 0 8 8' xmlns='http://www.w3.org/2000/svg'>\n\
     <path d='m6.7565 1.0201a0.73603 0.73603 0 0 1 1.0501 1.0304l-3.9157 4.897a0.73603 0.73603 0 0 1-1.0599 0.019627l-2.5967-2.5967a0.73603 0.73603 0 1 1 1.0403-1.0403l2.055 2.054 3.4083-4.3426a0.26203 0.26203 0 0 1 0.019627-0.02159z' stroke-width='.98137' fill='#000000'/>\n\
    </svg>";

static const char *const radiomenuOnData = "\
    <svg width='8' height='8' version='1.1' viewBox='0 0 8 8' xmlns='http://www.w3.org/2000/svg'>\n\
     <path d='m4 7a3 3 0 1 0 0-6 3 3 0 0 0 0 6z' fill='#000000' fill-rule='evenodd' stroke-width='.6'/>\n\
    </svg>";

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
    char borderColorStr[7], interiorColorStr[7], indicatorColorStr[7];
    char imgName[50];
    Tk_Image img;
    size_t svgDataLen;
    char *svgDataCopy;
    char *borderColorPtr, *interiorColorPtr, *indicatorColorPtr;
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

	borderColorPtr =    strstr(svgDataPtr, "888888");
	interiorColorPtr =  strstr(svgDataPtr, "ffffff");
	indicatorColorPtr = strstr(svgDataPtr, "000000");

	if (borderColorPtr != NULL) {
	    memcpy(borderColorPtr, borderColorStr, 6);
	}
	if (interiorColorPtr != NULL) {
	    memcpy(interiorColorPtr, interiorColorStr, 6);
	}
	if (indicatorColorPtr != NULL) {
	    memcpy(indicatorColorPtr, indicatorColorStr, 6);
	}

	/*
	 * Create an SVG photo image from svgDataCopy
	 */

	script = (char *)attemptckalloc(svgDataLen + 101);
	if (script == NULL) {
	    ckfree(svgDataCopy);
	    return;
	}
	snprintf(script, svgDataLen + 101,
		 "image create photo %s -format $::tk::svgFmt -data {%s}",
		 imgName, svgDataCopy);
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
     * Retrieve the scaling factor (1.0, 1.25, 1.5, ...) and multiply dim by it
     */

    scalingPctPtr = Tcl_GetVar(interp, "::tk::scalingPct", TCL_GLOBAL_ONLY);
    scalingFactor = atof(scalingPctPtr) / 100;
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

    if ((butPtr->type == TYPE_CHECK_BUTTON || butPtr->type == TYPE_RADIO_BUTTON)
	    && butPtr->indicatorOn
	    && butPtr->indicatorDiameter > 2*butPtr->borderWidth) {
	TkBorder *selBorder = (TkBorder *) butPtr->selectBorder;
	XColor *selColor = NULL;
	int btype = (butPtr->type == TYPE_CHECK_BUTTON ?
		     CHECK_BUTTON : RADIO_BUTTON);

	if (selBorder != NULL) {
	    selColor = selBorder->bgColorPtr;
	}
	x -= butPtr->indicatorSpace/2;
	y = Tk_Height(tkwin)/2;
	TkpDrawCheckIndicator(tkwin, butPtr->display, pixmap, x, y,
		border, butPtr->normalFg, selColor, butPtr->disabledFg,
		((butPtr->flags & SELECTED) ? 1 :
		 (butPtr->flags & TRISTATED) ? 2 : 0),
		 (butPtr->state == STATE_DISABLED), btype);
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
