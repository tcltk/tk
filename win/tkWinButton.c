/*
 * tkWinButton.c --
 *
 *	This file implements the Windows specific portion of the button
 *	widgets.
 *
 * Copyright © 1996-1998 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#define OEMRESOURCE
#include "tkWinInt.h"
#include "tkButton.h"

/*
 * These macros define the base style flags for the different button types.
 */

#define LABEL_STYLE (BS_OWNERDRAW | WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS)
#define PUSH_STYLE  (LABEL_STYLE | BS_PUSHBUTTON)
#define CHECK_STYLE (LABEL_STYLE | BS_CHECKBOX)
#define RADIO_STYLE (LABEL_STYLE | BS_RADIOBUTTON)

/*
 * Declaration of Windows specific button structure.
 */

typedef struct WinButton {
    TkButton info;		/* Generic button info. */
    WNDPROC oldProc;		/* Old window procedure. */
    HWND hwnd;			/* Current window handle. */
    Pixmap pixmap;		/* Bitmap for rendering the button. */
    DWORD style;		/* Window style flags. */
} WinButton;

/*
 * Cached information about the checkbutton and radiobutton indicator boxes
 */

typedef struct {
    BOOLEAN initialized;
    int boxSize;		/* Width & height of the box. */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * Data of the SVG images used for drawing the indicators
 */

static const char checkbtnOffData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <path d='m0 0v15h1v-14h14v-1z' fill='#a0a0a0'/>\n\
     <path d='m1 1v13h1v-12h12v-1z' fill='#696969'/>\n\
     <path d='m14 1v13h-13v1h14v-14z' fill='#e3e3e3'/>\n\
     <path d='m15 0v15h-15v1h16v-16z' fill='#eeeeee'/>\n\
     <rect x='2' y='2' width='12' height='12' fill='#ffffff'/>\n\
    </svg>";

static const char checkbtnOnData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <path d='m0 0v15h1v-14h14v-1z' fill='#a0a0a0'/>\n\
     <path d='m1 1v13h1v-12h12v-1z' fill='#696969'/>\n\
     <path d='m14 1v13h-13v1h14v-14z' fill='#e3e3e3'/>\n\
     <path d='m15 0v15h-15v1h16v-16z' fill='#eeeeee'/>\n\
     <rect x='2' y='2' width='12' height='12' fill='#ffffff'/>\n\
     <path d='m4.5 8 3 3 4-6' fill='none' stroke='#000000' stroke-linecap='round' stroke-linejoin='round' stroke-width='2'/>\n\
    </svg>";

static const char radiobtnOffData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <defs>\n\
      <linearGradient id='linearGradientOuter' x1='5' y1='5' x2='11' y2='11' gradientUnits='userSpaceOnUse'>\n\
       <stop stop-color='#a0a0a0' offset='0'/>\n\
       <stop stop-color='#eeeeee' offset='1'/>\n\
      </linearGradient>\n\
      <linearGradient id='linearGradientInner' x1='5' y1='5' x2='11' y2='11' gradientUnits='userSpaceOnUse'>\n\
       <stop stop-color='#696969' offset='0'/>\n\
       <stop stop-color='#e3e3e3' offset='1'/>\n\
      </linearGradient>\n\
     </defs>\n\
     <circle cx='8' cy='8' r='8' fill='url(#linearGradientOuter)'/>\n\
     <circle cx='8' cy='8' r='7' fill='url(#linearGradientInner)'/>\n\
     <circle cx='8' cy='8' r='6' fill='#ffffff'/>\n\
    </svg>";

static const char radiobtnOnData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <defs>\n\
      <linearGradient id='linearGradientOuter' x1='5' y1='5' x2='11' y2='11' gradientUnits='userSpaceOnUse'>\n\
       <stop stop-color='#a0a0a0' offset='0'/>\n\
       <stop stop-color='#eeeeee' offset='1'/>\n\
      </linearGradient>\n\
      <linearGradient id='linearGradientInner' x1='5' y1='5' x2='11' y2='11' gradientUnits='userSpaceOnUse'>\n\
       <stop stop-color='#696969' offset='0'/>\n\
       <stop stop-color='#e3e3e3' offset='1'/>\n\
      </linearGradient>\n\
     </defs>\n\
     <circle cx='8' cy='8' r='8' fill='url(#linearGradientOuter)'/>\n\
     <circle cx='8' cy='8' r='7' fill='url(#linearGradientInner)'/>\n\
     <circle cx='8' cy='8' r='6' fill='#ffffff'/>\n\
     <circle cx='8' cy='8' r='3' fill='#000000'/>\n\
    </svg>";

/*
 * Declarations for functions defined in this file.
 */

static LRESULT CALLBACK	ButtonProc(HWND hwnd, UINT message,
			    WPARAM wParam, LPARAM lParam);
static Window		CreateProc(Tk_Window tkwin, Window parent,
			    void *instanceData);
static void		InitBoxes(Tk_Window tkwin);
static void		ColorToStr(COLORREF color, char *colorStr);
static void		ImageChanged(void *clientData,
			    int x, int y, int width, int height,
			    int imageWidth, int imageHeight);
static void		TkpDrawIndicator(TkButton *butPtr, Drawable d,
			    Tk_3DBorder border, GC gc, int dim, int x, int y);

/*
 * The class procedure table for the button widgets.
 */

const Tk_ClassProcs tkpButtonProcs = {
    sizeof(Tk_ClassProcs),	/* size */
    TkButtonWorldChanged,	/* worldChangedProc */
    CreateProc,			/* createProc */
    NULL			/* modalProc */
};


/*
 *----------------------------------------------------------------------
 *
 * InitBoxes --
 *
 *	This function computes the size of the checkbutton and radiobutton
 *	indicator boxes, according to the display's scaling percentage.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Populates the thread-private data.
 *
 *----------------------------------------------------------------------
 */

static void
InitBoxes(Tk_Window tkwin)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    double scalingLevel = TkScalingLevel(tkwin);

    tsdPtr->boxSize = (int)(16.0 * scalingLevel);
    tsdPtr->initialized = TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpButtonSetDefaults --
 *
 *	This procedure is invoked before option tables are created for
 *	buttons. It modifies some of the default values to match the current
 *	values defined for this platform.
 *
 * Results:
 *	Some of the default values in *specPtr are modified.
 *
 * Side effects:
 *	Updates some of.
 *
 *----------------------------------------------------------------------
 */

void
TkpButtonSetDefaults(void)
{
    int width = GetSystemMetrics(SM_CXEDGE);
	if (width > 0) {
	    snprintf(tkDefButtonBorderWidth, sizeof(tkDefButtonBorderWidth), "%d", width);
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
 *	Registers an event handler for the widget.
 *
 *----------------------------------------------------------------------
 */

TkButton *
TkpCreateButton(
    TCL_UNUSED(Tk_Window))
{
    WinButton *butPtr;

    butPtr = (WinButton *)ckalloc(sizeof(WinButton));
    butPtr->hwnd = NULL;
    return (TkButton *) butPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateProc --
 *
 *	This function creates a new Button control, subclasses the instance,
 *	and generates a new Window object.
 *
 * Results:
 *	Returns the newly allocated Window object, or None on failure.
 *
 * Side effects:
 *	Causes a new Button control to come into existence.
 *
 *----------------------------------------------------------------------
 */

static Window
CreateProc(
    Tk_Window tkwin,		/* Token for window. */
    Window parentWin,		/* Parent of new window. */
    void *instanceData)		/* Button instance data. */
{
    Window window;
    HWND parent;
    LPCWSTR windowClass;
    WinButton *butPtr = (WinButton *)instanceData;

    parent = Tk_GetHWND(parentWin);
    if (butPtr->info.type == TYPE_LABEL) {
	windowClass = L"STATIC";
	butPtr->style = SS_OWNERDRAW | WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS;
    } else {
	windowClass = L"BUTTON";
	butPtr->style = BS_OWNERDRAW | WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS;
    }
    butPtr->hwnd = CreateWindowW(windowClass, NULL, butPtr->style,
	    Tk_X(tkwin), Tk_Y(tkwin), Tk_Width(tkwin), Tk_Height(tkwin),
	    parent, NULL, Tk_GetHINSTANCE(), NULL);
    SetWindowPos(butPtr->hwnd, HWND_TOP, 0, 0, 0, 0,
		    SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
    butPtr->oldProc = (WNDPROC)SetWindowLongPtrW(butPtr->hwnd, GWLP_WNDPROC,
	    (LONG_PTR) ButtonProc);

    window = Tk_AttachHWND(tkwin, butPtr->hwnd);
    return window;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDestroyButton --
 *
 *	Free data structures associated with the button control.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Restores the default control state.
 *
 *----------------------------------------------------------------------
 */

void
TkpDestroyButton(
    TkButton *butPtr)
{
    WinButton *winButPtr = (WinButton *)butPtr;
    HWND hwnd = winButPtr->hwnd;

    if (hwnd) {
	SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR) winButPtr->oldProc);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ColorToStr --
 *
 *	Writes a given color to a string, in the format "RRGGBB".
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the content of the memory area pointed to by the 2nd argument.
 *
 *----------------------------------------------------------------------
 */

static void
ColorToStr(
    COLORREF color,	/* specifies a color */
    char *colorStr)	/* memory area to which the color is to be
			   output in the format "RRGGBB" */
{
    snprintf(colorStr, 7, "%02x%02x%02x",
	     GetRValue(color), GetGValue(color), GetBValue(color));
}

/*
 *----------------------------------------------------------------------
 *
 * ImageChanged --
 *
 *	Dummy function to be passed to Tk_GetImage().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
ImageChanged(
    void *clientData,
    int x, int y, int width, int height,
    int imageWidth, int imageHeight)
{
    (void)clientData;
    (void)x; (void)y; (void)width; (void)height;
    (void)imageWidth; (void)imageHeight;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDrawIndicator -
 *
 *      Draws the indicator image in the drawable at the (x,y) location.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      An image is drawn in the drawable at the given location.
 *
 *----------------------------------------------------------------------
 */

static void
TkpDrawIndicator(
    TkButton *butPtr,		/* checkbutton or radiobutton */
    Drawable d,			/* what to draw on */
    Tk_3DBorder border,		/* colors of the border */
    GC gc,			/* graphics context */
    int dim,			/* width & height of the indicator */
    int x, int y)		/* where to draw */
{
    Tk_Window tkwin = butPtr->tkwin;
    char topOuterColorStr[7], btmOuterColorStr[7], topInnerColorStr[7],
	 btmInnerColorStr[7], interiorColorStr[7], checkColorStr[7];
    Tcl_Interp *interp = Tk_Interp(tkwin);
    char imgName[80];
    Tk_Image img;
    const char *svgDataPtr;
    size_t svgDataLen;
    char *svgDataCopy;
    char *topOuterColorPtr, *btmOuterColorPtr, *topInnerColorPtr,
	 *btmInnerColorPtr, *interiorColorPtr, *checkColorPtr;
    const char *cmdFmt;
    size_t scriptSize;
    char *script;
    int code;

    /*
     * Construct the color strings topOuterColorStr, btmOuterColorStr,
     * topInnerColorStr, btmInnerColorStr, interiorColorStr, and checkColorStr
     */

    ColorToStr(TkWinGetBorderPixels(tkwin, border, TK_3D_DARK_GC),
	       topOuterColorStr);
    ColorToStr(TkWinGetBorderPixels(tkwin, border, TK_3D_LIGHT_GC),
	       btmOuterColorStr);
    ColorToStr(TkWinGetBorderPixels(tkwin, border, TK_3D_DARK2),
	       topInnerColorStr);
    ColorToStr(TkWinGetBorderPixels(tkwin, border, TK_3D_LIGHT2),
	       btmInnerColorStr);

    if (butPtr->state == STATE_ACTIVE) {
	ColorToStr(TkWinGetBorderPixels(tkwin, butPtr->activeBorder,
		   TK_3D_FLAT_GC), interiorColorStr);
    } else if (butPtr->state == STATE_DISABLED || (butPtr->flags & TRISTATED)) {
	ColorToStr(TkWinGetBorderPixels(tkwin, border, TK_3D_LIGHT2),
		   interiorColorStr);
    } else if (butPtr->selectBorder != NULL) {
	ColorToStr(TkWinGetBorderPixels(tkwin, butPtr->selectBorder,
		   TK_3D_FLAT_GC), interiorColorStr);
    } else {
	ColorToStr(GetSysColor(COLOR_WINDOW), interiorColorStr);
    }

    if (butPtr->state == STATE_DISABLED && butPtr->disabledFg == NULL) {
	ColorToStr(TkWinGetBorderPixels(tkwin, border, TK_3D_DARK_GC),
		   checkColorStr);
    } else {
	ColorToStr(gc->foreground, checkColorStr);
    }

    /*
    * Check whether there is an SVG image of this size for the indicator's
    * type (0 = checkbtn, 1 = radiobtn) and these color strings
    */

    snprintf(imgName, sizeof(imgName),
	     "::tk::icons::indicator%d_%d_%s_%s_%s_%s_%s_%s",
	     dim, butPtr->type == TYPE_RADIO_BUTTON,
	     topOuterColorStr, btmOuterColorStr, topInnerColorStr,
	     btmInnerColorStr, interiorColorStr,
	     (butPtr->flags & (SELECTED|TRISTATED)) ? checkColorStr : "XXXXXX");
    img = Tk_GetImage(interp, tkwin, imgName, ImageChanged, NULL);
    if (img == NULL) {
	/*
	 * Determine the SVG data to use for the photo image
	 */

	if (butPtr->type == TYPE_CHECK_BUTTON) {
	    svgDataPtr = ((butPtr->flags & (SELECTED|TRISTATED)) ?
			  checkbtnOnData : checkbtnOffData);
	} else {
	    svgDataPtr = ((butPtr->flags & (SELECTED|TRISTATED)) ?
			  radiobtnOnData : radiobtnOffData);
	}

	/*
	 * Copy the string pointed to by svgDataPtr to
	 * a newly allocated memory area svgDataCopy
	 */

	svgDataLen = strlen(svgDataPtr);
	svgDataCopy = (char *)attemptckalloc(svgDataLen + 1);
	if (svgDataCopy == NULL) {
	    return;
	}
	memcpy(svgDataCopy, svgDataPtr, svgDataLen);
	svgDataCopy[svgDataLen] = '\0';

	/*
	 * Update the colors within svgDataCopy
	 */

	topOuterColorPtr = strstr(svgDataCopy, "a0a0a0");
	btmOuterColorPtr = strstr(svgDataCopy, "eeeeee");
	topInnerColorPtr = strstr(svgDataCopy, "696969");
	btmInnerColorPtr = strstr(svgDataCopy, "e3e3e3");
	interiorColorPtr = strstr(svgDataCopy, "ffffff");
	checkColorPtr =    strstr(svgDataCopy, "000000");

	assert(topOuterColorPtr);
	assert(btmOuterColorPtr);
	assert(topInnerColorPtr);
	assert(btmInnerColorPtr);
	assert(interiorColorPtr);

	memcpy(topOuterColorPtr, topOuterColorStr, 6);
	memcpy(btmOuterColorPtr, btmOuterColorStr, 6);
	memcpy(topInnerColorPtr, topInnerColorStr, 6);
	memcpy(btmInnerColorPtr, btmInnerColorStr, 6);
	memcpy(interiorColorPtr, interiorColorStr, 6);
	if (checkColorPtr != NULL) {
	    memcpy(checkColorPtr, checkColorStr, 6);
	}

	/*
	 * Create an SVG photo image from svgDataCopy
	 */

	cmdFmt = "image create photo %s -format $::tk::svgFmt -data {%s}";
	scriptSize = strlen(cmdFmt) + strlen(imgName) + svgDataLen;
	script = (char *)attemptckalloc(scriptSize);
	if (script == NULL) {
	    ckfree(svgDataCopy);
	    return;
	}
	snprintf(script, scriptSize, cmdFmt, imgName, svgDataCopy);
	ckfree(svgDataCopy);
	code = Tcl_EvalEx(interp, script, TCL_INDEX_NONE, TCL_EVAL_GLOBAL);
	ckfree(script);
	if (code != TCL_OK) {
	    Tcl_BackgroundException(interp, code);
	    return;
	}
	img = Tk_GetImage(interp, tkwin, imgName, ImageChanged, NULL);
    }

    /*
     * Display the image
     */

    Tk_RedrawImage(img, 0, 0, dim, dim, d, x, y);
    Tk_FreeImage(img);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayButton --
 *
 *	This procedure is invoked to display a button widget. It is normally
 *	invoked as an idle handler.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information appears on the screen. The REDRAW_PENDING flag is cleared.
 *
 *----------------------------------------------------------------------
 */

void
TkpDisplayButton(
    void *clientData)	/* Information about widget. */
{
    TkWinDCState state;
    HDC dc;
    TkButton *butPtr = (TkButton *)clientData;
    GC gc;
    Tk_3DBorder border;
    Pixmap pixmap;
    int x = 0;			/* Initialization only needed to stop compiler
				 * warning. */
    int y, relief;
    Tk_Window tkwin = butPtr->tkwin;
    int width = 0, height = 0, haveImage = 0, haveText = 0, drawRing = 0;
    int defaultWidth;		/* Width of default ring. */
    int offset;			/* 0 means this is a label widget. 1 means it
				 * is a flavor of button, so we offset the
				 * text to make the button appear to move up
				 * and down as the relief changes. */
    int textXOffset = 0, textYOffset = 0;
				/* Text offsets for use with compound buttons
				 * and focus ring. */
    int imageWidth, imageHeight;
    int imageXOffset = 0, imageYOffset = 0;
				/* Image information that will be used to
				 * restrict disabled pixmap as well. */
    int padX, padY, borderWidth, highlightWidth;

    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    butPtr->flags &= ~REDRAW_PENDING;
    if ((butPtr->tkwin == NULL) || !Tk_IsMapped(tkwin)) {
	return;
    }

    Tk_GetPixelsFromObj(NULL, tkwin, butPtr->borderWidthObj, &borderWidth);
    Tk_GetPixelsFromObj(NULL, tkwin, butPtr->highlightWidthObj, &highlightWidth);
    Tk_GetPixelsFromObj(NULL, tkwin, butPtr->padXObj, &padX);
    Tk_GetPixelsFromObj(NULL, tkwin, butPtr->padYObj, &padY);

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
    if ((butPtr->flags & SELECTED) && (butPtr->state != STATE_ACTIVE)
	    && (butPtr->selectBorder != NULL) && !butPtr->indicatorOn) {
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
     * Compute width of default ring and offset for pushed buttons.
     */

    if (butPtr->type == TYPE_LABEL) {
	defaultWidth = highlightWidth;
	offset = 0;
    } else if (butPtr->type == TYPE_BUTTON) {
	defaultWidth = ((butPtr->defaultState == DEFAULT_ACTIVE)
		? highlightWidth : 0);
	offset = 1;
    } else {
	defaultWidth = 0;
	if ((butPtr->type >= TYPE_CHECK_BUTTON) && !butPtr->indicatorOn) {
	    offset = 1;
	} else {
	    offset = 0;
	}
    }

    /*
     * In order to avoid screen flashes, this procedure redraws the button in
     * a pixmap, then copies the pixmap to the screen in a single operation.
     * This means that there's no point in time where the on-sreen image has
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
	int fullWidth = 0, fullHeight = 0;

	switch ((enum compound) butPtr->compound) {
	case COMPOUND_TOP:
	case COMPOUND_BOTTOM:
	    /*
	     * Image is above or below text.
	     */

	    if (butPtr->compound == COMPOUND_TOP) {
		textYOffset = height + padY;
	    } else {
		imageYOffset = butPtr->textHeight + padY;
	    }
	    fullHeight = height + butPtr->textHeight + padY;
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
		textXOffset = width + padX;
	    } else {
		imageXOffset = butPtr->textWidth + padX;
	    }
	    fullWidth = butPtr->textWidth + padX + width;
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
	TkComputeAnchor(butPtr->anchor, tkwin, padX, padY,
		butPtr->indicatorSpace + fullWidth, fullHeight, &x, &y);
	x += butPtr->indicatorSpace;

	if (relief == TK_RELIEF_SUNKEN) {
	    x += offset;
	    y += offset;
	}
	imageXOffset += x;
	imageYOffset += y;
	if (butPtr->image != NULL) {
	    if ((butPtr->selectImage != NULL) && (butPtr->flags & SELECTED)) {
		Tk_RedrawImage(butPtr->selectImage, 0, 0,
			width, height, pixmap, imageXOffset, imageYOffset);
	    } else if ((butPtr->tristateImage != NULL)
		    && (butPtr->flags & TRISTATED)) {
		Tk_RedrawImage(butPtr->tristateImage, 0, 0,
			width, height, pixmap, imageXOffset, imageYOffset);
	    } else {
		Tk_RedrawImage(butPtr->image, 0, 0, width, height, pixmap,
			imageXOffset, imageYOffset);
	    }
	} else {
	    XSetClipOrigin(butPtr->display, gc, imageXOffset, imageYOffset);
	    XCopyPlane(butPtr->display, butPtr->bitmap, pixmap, gc,
		    0, 0, (unsigned int) width, (unsigned int) height,
		    imageXOffset, imageYOffset, 1);
	    XSetClipOrigin(butPtr->display, gc, 0, 0);
	}
	if ((butPtr->state == STATE_DISABLED) &&
		(butPtr->disabledFg != NULL)) {
	    COLORREF oldFgColor = gc->foreground;

	    if (gc->background == GetSysColor(COLOR_BTNFACE)) {
		gc->foreground = GetSysColor(COLOR_3DHILIGHT);
		Tk_DrawTextLayout(butPtr->display, pixmap, gc,
		    butPtr->textLayout, x + textXOffset + 1,
		    y + textYOffset + 1, 0, -1);
		Tk_UnderlineTextLayout(butPtr->display, pixmap, gc,
		    butPtr->textLayout, x + textXOffset + 1,
		    y + textYOffset + 1,
		    butPtr->underline);
		gc->foreground = oldFgColor;
	    }
	}

	Tk_DrawTextLayout(butPtr->display, pixmap, gc,
		butPtr->textLayout, x + textXOffset, y + textYOffset, 0, -1);
	Tk_UnderlineTextLayout(butPtr->display, pixmap, gc,
		butPtr->textLayout, x + textXOffset, y + textYOffset,
		butPtr->underline);
	height = fullHeight;
	drawRing = 1;
    } else {
	if (haveImage) {
	    TkComputeAnchor(butPtr->anchor, tkwin, 0, 0,
		    butPtr->indicatorSpace + width, height, &x, &y);
	    x += butPtr->indicatorSpace;

	    if (relief == TK_RELIEF_SUNKEN) {
		x += offset;
		y += offset;
	    }
	    imageXOffset += x;
	    imageYOffset += y;
	    if (butPtr->image != NULL) {
		if ((butPtr->selectImage != NULL) &&
			(butPtr->flags & SELECTED)) {
		    Tk_RedrawImage(butPtr->selectImage, 0, 0, width, height,
			    pixmap, imageXOffset, imageYOffset);
		} else if ((butPtr->tristateImage != NULL) &&
			(butPtr->flags & TRISTATED)) {
		    Tk_RedrawImage(butPtr->tristateImage, 0, 0, width, height,
			    pixmap, imageXOffset, imageYOffset);
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
	} else {
	    TkComputeAnchor(butPtr->anchor, tkwin, padX, padY,
		    butPtr->indicatorSpace + butPtr->textWidth,
		    butPtr->textHeight,	&x, &y);

	    x += butPtr->indicatorSpace;

	    if (relief == TK_RELIEF_SUNKEN) {
		x += offset;
		y += offset;
	    }
	    if ((butPtr->state == STATE_DISABLED) &&
		    (butPtr->disabledFg != NULL)) {
		COLORREF oldFgColor = gc->foreground;
		if (gc->background == GetSysColor(COLOR_BTNFACE)) {
		    gc->foreground = GetSysColor(COLOR_3DHILIGHT);
		    Tk_DrawTextLayout(butPtr->display, pixmap, gc,
			    butPtr->textLayout, x + 1, y + 1, 0, -1);
		    Tk_UnderlineTextLayout(butPtr->display, pixmap, gc,
			    butPtr->textLayout, x + 1, y + 1, butPtr->underline);
		    gc->foreground = oldFgColor;
		}
	    }
	    Tk_DrawTextLayout(butPtr->display, pixmap, gc, butPtr->textLayout,
		    x, y, 0, -1);
	    Tk_UnderlineTextLayout(butPtr->display, pixmap, gc,
		    butPtr->textLayout, x, y, butPtr->underline);

	    height = butPtr->textHeight;
	    drawRing = 1;
	}
    }

    /*
     * Draw the focus ring. If this is a push button then we need to put it
     * around the inner edge of the border, otherwise we put it around the
     * text. The text offsets are only non-zero when this is a compound
     * button.
     */

    if (drawRing && butPtr->flags & GOT_FOCUS && butPtr->type != TYPE_LABEL) {
	if (butPtr->type == TYPE_BUTTON || !butPtr->indicatorOn) {
	    int dottedWidth = borderWidth + 1 + defaultWidth;
	    TkWinDrawDottedRect(butPtr->display, pixmap, gc->foreground,
		    dottedWidth, dottedWidth,
		    Tk_Width(tkwin) - 2*dottedWidth,
		    Tk_Height(tkwin) - 2*dottedWidth);
	} else {
	    TkWinDrawDottedRect(butPtr->display, pixmap, gc->foreground,
		    x-1 + textXOffset, y-1 + textYOffset,
		    butPtr->textWidth + 2, butPtr->textHeight + 3);
	}
    }

    y += height/2;

    /*
     * Draw the indicator for check buttons and radio buttons. At this point
     * x and y refer to the top-left corner of the text or image or bitmap.
     */

    if ((butPtr->type >= TYPE_CHECK_BUTTON) && butPtr->indicatorOn
	    && tsdPtr->initialized) {
	x -= butPtr->indicatorSpace;
	y -= butPtr->indicatorDiameter / 2;

	TkpDrawIndicator(butPtr, pixmap, border, gc, tsdPtr->boxSize, x, y + 1);
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
     * contents overflow they'll be covered up by the border.
     */

    if (relief != TK_RELIEF_FLAT) {
	Tk_Draw3DRectangle(tkwin, pixmap, border,
		defaultWidth, defaultWidth,
		Tk_Width(tkwin) - 2 * defaultWidth,
		Tk_Height(tkwin) - 2 * defaultWidth,
		borderWidth, relief);
    }
    if (defaultWidth != 0) {
	int highlightColor;

	dc = TkWinGetDrawableDC(butPtr->display, pixmap, &state);
	if (butPtr->type == TYPE_LABEL) {
	    highlightColor = (int) Tk_3DBorderColor(butPtr->highlightBorder)->pixel;
	} else {
	    highlightColor = (int) butPtr->highlightColorPtr->pixel;
	}
	TkWinFillRect(dc, 0, 0, Tk_Width(tkwin), defaultWidth,
		highlightColor);
	TkWinFillRect(dc, 0, 0, defaultWidth, Tk_Height(tkwin),
		highlightColor);
	TkWinFillRect(dc, 0, Tk_Height(tkwin) - defaultWidth,
		Tk_Width(tkwin), defaultWidth,
		highlightColor);
	TkWinFillRect(dc, Tk_Width(tkwin) - defaultWidth, 0,
		defaultWidth, Tk_Height(tkwin),
		highlightColor);
	TkWinReleaseDrawableDC(pixmap, dc, &state);
    }

    if (butPtr->flags & GOT_FOCUS) {
	Tk_SetCaretPos(tkwin, x, y, 0 /* not used */);
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
 *	After changes in a button's text or bitmap, this procedure recomputes
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
    int txtWidth, txtHeight;	/* Width and height of text */
    int imgWidth, imgHeight;	/* Width and height of image */
    int width = 0, height = 0;	/* Width and height of button */
    int haveImage, haveText;
    int avgWidth;
    int minWidth;
    /* Vertical and horizontal dialog units size in pixels. */
    double vDLU, hDLU;
    Tk_FontMetrics fm;
    int borderWidth, highlightWidth, wrapLength;
    int butPtrWidth, butPtrHeight;
    int padX, padY;

    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    Tk_GetPixelsFromObj(NULL, butPtr->tkwin, butPtr->highlightWidthObj, &highlightWidth);
    Tk_GetPixelsFromObj(NULL, butPtr->tkwin, butPtr->borderWidthObj, &borderWidth);
    Tk_GetPixelsFromObj(NULL, butPtr->tkwin, butPtr->wrapLengthObj, &wrapLength);
    Tk_GetPixelsFromObj(NULL, butPtr->tkwin, butPtr->widthObj, &butPtrWidth);
    Tk_GetPixelsFromObj(NULL, butPtr->tkwin, butPtr->heightObj, &butPtrHeight);

    butPtr->inset = highlightWidth + borderWidth;
    butPtr->indicatorSpace = 0;

    if (!tsdPtr->initialized) {
	InitBoxes(butPtr->tkwin);
    }

    /*
     * Figure out image metrics.
     */

    if (butPtr->image != NULL) {
	Tk_SizeOfImage(butPtr->image, &imgWidth, &imgHeight);
	haveImage = 1;
    } else if (butPtr->bitmap != None) {
	Tk_SizeOfBitmap(butPtr->display, butPtr->bitmap,
		&imgWidth, &imgHeight);
	haveImage = 1;
    } else {
	imgWidth = 0;
	imgHeight = 0;
	haveImage = 0;
    }

    /*
     * Figure out font metrics (even if we don't have text because we need
     * DLUs (based on font, not text) for some spacing calculations below).
     */

    Tk_FreeTextLayout(butPtr->textLayout);
    butPtr->textLayout = Tk_ComputeTextLayout(butPtr->tkfont,
	    Tcl_GetString(butPtr->textPtr), TCL_INDEX_NONE, wrapLength,
	    butPtr->justify, 0, &butPtr->textWidth, &butPtr->textHeight);

    txtWidth = butPtr->textWidth;
    txtHeight = butPtr->textHeight;
    haveText = (*(Tcl_GetString(butPtr->textPtr)) != '\0');
    avgWidth = (Tk_TextWidth(butPtr->tkfont,
	    "abcdefghijklmnopqurstuvwzyABCDEFGHIJKLMNOPQURSTUVWZY",
	    52) + 26) / 52;
    Tk_GetFontMetrics(butPtr->tkfont, &fm);

    /*
     * Compute dialog units for layout calculations.
     */

    hDLU = avgWidth / 4.0;
    vDLU = fm.linespace / 8.0;

    /*
     * First, let's try to compute button size "by the book" (See "Microsoft
     * Windows User Experience" (ISBN 0-7356-0566-1), Chapter 14 - Visual
     * Design, Section 4 - Layout (page 448)).
     *
     * Note, that Tk "buttons" are Microsoft "Command buttons", Tk
     * "checkbuttons" are Microsoft "check boxes", Tk "radiobuttons" are
     * Microsoft "option buttons", and Tk "labels" are Microsoft "text
     * labels".
     */

    /*
     * Set width and height by button type; See User Experience table, p449.
     * These are text-based measurements, even if the text is "". If there is
     * an image, height will get set again later.
     */

    switch (butPtr->type) {
    case TYPE_BUTTON:
	/*
	 * First compute the minimum width of the button in characters. MWUE
	 * says that the button should be 50 DLUs. We allow 6 DLUs padding
	 * left and right. (There is no rule but this is consistent with the
	 * fact that button text is 8 DLUs high and buttons are 14 DLUs high.)
	 *
	 * The width is specified in characters. A character is, by
	 * definition, 4 DLUs wide. 11 char * 4 DLU is 44 DLU + 6 DLU padding
	 * = 50 DLU. Therefore, width = -11 -> MWUE compliant buttons.
	 */

	if (butPtrWidth < 0) {
	    minWidth = -(butPtrWidth);	    /* Min width in chars */
	    width = avgWidth * minWidth;	    /* Allow for characters */
	    width += (int)(0.5 + (6 * hDLU));	    /* Add for padding */
	}

	/*
	 * If shrink-wrapping was requested (width = 0) or if the text is
	 * wider than the default button width, adjust the button width up to
	 * suit.
	 */

	if (butPtrWidth == 0
		|| (txtWidth + (int)(0.5 + (6 * hDLU)) > width)) {
	    width = txtWidth + (int)(0.5 + (6 * hDLU));
	}

	/*
	 * The User Experience says 14 DLUs. Since text is, by definition, 8
	 * DLU/line, this allows for multi-line text while working perfectly
	 * for single-line text.
	 */

	height = txtHeight + (int)(0.5 + (6 * vDLU));

	/*
	 * The above includes 6 DLUs of padding which should include defaults
	 * of 1 pixel of highlightwidth, 2 pixels of borderwidth, 1 pixel of
	 * padding and 1 pixel of extra inset on each side. Those will be
	 * added later so reduce width and height now to compensate.
	 */

	width -= 10;
	height -= 10;

	if (!haveImage) {
	    /*
	     * Extra inset for the focus ring.
	     */

	    butPtr->inset += 1;
	}
	break;

    case TYPE_LABEL:
	/*
	 * The User Experience says, "as wide as needed".
	 */

	width = txtWidth;

	/*
	 * The User Experience says, "8 (DLUs) per line of text". Since text
	 * is, by definition, 8 DLU/line, this allows for multi-line text
	 * while working perfectly for single-line text.
	 */

	if (txtHeight) {
	    height = txtHeight;
	} else {
	    /*
	     * If there's no text, we want the height to be one linespace.
	     */
	    height = fm.linespace;
	}
	break;

    case TYPE_RADIO_BUTTON:
    case TYPE_CHECK_BUTTON: {
	/*
	 * See note for TYPE_LABEL.
	 */

	width = txtWidth;

	/*
	 * The User Experience says 10 DLUs. (Is that one DLU above and below
	 * for the focus ring?) See note above about multi-line text and 8
	 * DLU/line.
	 */

	height = txtHeight + (int)(0.5 + (2.0 * vDLU));

	/*
	 * The above includes 2 DLUs of padding which should include defaults
	 * of 1 pixel of highlightwidth, 0 pixels of borderwidth, and 1 pixel
	 * of padding on each side. Those will be added later so reduce height
	 * now to compensate.
	 */

	height -= 4;

	/*
	 * Extra inset for the focus ring.
	 */
	butPtr->inset += 1;
	break;
    }
    }/* switch */

    /*
     * At this point, the width and height are correct for a Tk text button,
     * excluding padding and inset, but we have to allow for compound buttons.
     * The image may be above, below, left, or right of the text.
     */

    /*
     * If the button is compound (i.e., it shows both an image and text), the
     * new geometry is a combination of the image and text geometry. We only
     * honor the compound bit if the button has both text and an image,
     * because otherwise it is not really a compound button.
     */

    Tk_GetPixelsFromObj(NULL, butPtr->tkwin, butPtr->padXObj, &padX);
    Tk_GetPixelsFromObj(NULL, butPtr->tkwin, butPtr->padYObj, &padY);

    if (butPtr->compound != COMPOUND_NONE && haveImage && haveText) {
	switch ((enum compound) butPtr->compound) {
	case COMPOUND_TOP:
	case COMPOUND_BOTTOM:
	    /*
	     * Image is above or below text.
	     */

	    if (imgWidth > width) {
		width = imgWidth;
	    }
	    height += imgHeight + padY;
	    break;

	case COMPOUND_LEFT:
	case COMPOUND_RIGHT:

	    /*
	     * Image is left or right of text.
	     *
	     * Only increase width of button if image doesn't fit in slack
	     * space of default button width
	     */

	    if ((imgWidth + txtWidth + padX) > width) {
		width = imgWidth + txtWidth + padX;
	    }

	    if (imgHeight > height) {
		height = imgHeight;
	    }
	    break;

	case COMPOUND_CENTER:
	    /*
	     * Image and text are superimposed.
	     */

	    if (imgWidth > width) {
		width = imgWidth;
	    }
	    if (imgHeight > height) {
		height = imgHeight;
	    }
	    break;
	case COMPOUND_NONE:
	    break;
	} /* switch */

	/*
	 * Fix up for minimum width.
	 */

	if (butPtrWidth < 0) {
	    /*
	     * minWidth in pixels (because there's an image.
	     */

	    minWidth = -(butPtrWidth);
	    if (width < minWidth) {
		width = minWidth;
	    }
	} else if (butPtrWidth > 0) {
	    width = butPtrWidth;
	}

	if (butPtrHeight > 0) {
	    height = butPtrHeight;
	}

	width += 2 * padX;
	height += 2 * padY;
    } else if (haveImage) {
	if (butPtrWidth > 0) {
	    width = butPtrWidth;
	} else {
	    width = imgWidth;
	}
	if (butPtrHeight > 0) {
	    height = butPtrHeight;
	} else {
	    height = imgHeight;
	}
    } else {
	/*
	 * No image. May or may not be text. May or may not be compound.
	 */

	/*
	 * butPtr->width is in characters. We need to allow for that many
	 * characters on the face, not in the over-all button width
	 */

	if (butPtrWidth > 0) {
	    width = butPtrWidth * avgWidth;
	}

	/*
	 * butPtr->height is in lines of text. We need to allow for that many
	 * lines on the face, not in the over-all button height.
	 */

	if (butPtrHeight > 0) {
	    height = butPtrHeight * fm.linespace;

	    /*
	     * Make the same adjustments as above to get same height for e.g.
	     * a one line text with -height 0 or 1. [Bug #565485]
	     */

	    switch (butPtr->type) {
	    case TYPE_BUTTON: {
		height += (int)(0.5 + (6 * vDLU)) - 10;
		break;
	    }
	    case TYPE_RADIO_BUTTON:
	    case TYPE_CHECK_BUTTON: {
		height += (int)(0.5 + (2.0 * vDLU)) - 4;
		break;
	    }
	    }
	}

	width += 2 * padX;
	height += 2 * padY;
    }

    /*
     * Fix up width and height for indicator sizing and spacing.
     */

    if (butPtr->type == TYPE_RADIO_BUTTON
	    || butPtr->type == TYPE_CHECK_BUTTON) {
	if (butPtr->indicatorOn) {
	    butPtr->indicatorDiameter = tsdPtr->boxSize;

	    /*
	     * Make sure we can see the whole indicator, even if the text or
	     * image is very small.
	     */

	    if (height < butPtr->indicatorDiameter) {
		height = butPtr->indicatorDiameter;
	    }

	    /*
	     * There is no rule for space between the indicator and the text
	     * (the two are atomic on 'Windows) but the User Experience page
	     * 451 says leave 3 hDLUs between "text labels and their
	     * associated controls".
	     */

	    butPtr->indicatorSpace = butPtr->indicatorDiameter +
		    (int)(0.5 + (3.0 * hDLU));
	    width += butPtr->indicatorSpace;
	}
    }

    /*
     * Inset is always added to the size.
     */

    width += 2 * butPtr->inset;
    height += 2 * butPtr->inset;

    Tk_GeometryRequest(butPtr->tkwin, width, height);
    Tk_SetInternalBorder(butPtr->tkwin, butPtr->inset);
}

/*
 *----------------------------------------------------------------------
 *
 * ButtonProc --
 *
 *	This function is called by Windows whenever an event occurs on a
 *	button control created by Tk.
 *
 * Results:
 *	Standard Windows return value.
 *
 * Side effects:
 *	May generate events.
 *
 *----------------------------------------------------------------------
 */

static LRESULT CALLBACK
ButtonProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    LRESULT result;
    WinButton *butPtr;
    Tk_Window tkwin = Tk_HWNDToWindow(hwnd);

    if (tkwin == NULL) {
	Tcl_Panic("ButtonProc called on an invalid HWND");
    }
    butPtr = (WinButton *)((TkWindow*)tkwin)->instanceData;

    switch(message) {
    case WM_ERASEBKGND:
	return 0;

    case BM_GETCHECK:
	if (((butPtr->info.type == TYPE_CHECK_BUTTON)
		|| (butPtr->info.type == TYPE_RADIO_BUTTON))
		&& butPtr->info.indicatorOn) {
	    return (butPtr->info.flags & SELECTED)
		    ? BST_CHECKED : BST_UNCHECKED;
	}
	return 0;

    case BM_GETSTATE: {
	DWORD state = 0;

	if (((butPtr->info.type == TYPE_CHECK_BUTTON)
		|| (butPtr->info.type == TYPE_RADIO_BUTTON))
		&& butPtr->info.indicatorOn) {
	    state = (butPtr->info.flags & SELECTED)
		    ? BST_CHECKED : BST_UNCHECKED;
	}
	if (butPtr->info.flags & GOT_FOCUS) {
	    state |= BST_FOCUS;
	}
	return state;
    }
    case WM_ENABLE:
	break;

    case WM_PAINT: {
	PAINTSTRUCT ps;
	BeginPaint(hwnd, &ps);
	EndPaint(hwnd, &ps);
	TkpDisplayButton(butPtr);

	/*
	 * Special note: must cancel any existing idle handler for
	 * TkpDisplayButton; it's no longer needed, and TkpDisplayButton
	 * cleared the REDRAW_PENDING flag.
	 */

	Tcl_CancelIdleCall(TkpDisplayButton, butPtr);
	return 0;
    }
    case BN_CLICKED: {
	/*
	 * OOPS: chromium fires WM_NULL regularly to ping if plugin is still
	 * alive. When using an external window (i.e. via the tcl plugin), this
	 * causes all buttons to fire once a second, so we need to make sure
	 * that we are not dealing with the chromium life check.
	*/
	if (wParam != 0 || lParam != 0) {
	    int code;
	    Tcl_Interp *interp = butPtr->info.interp;

	    if (butPtr->info.state != STATE_DISABLED) {
		Tcl_Preserve(interp);
		code = TkInvokeButton((TkButton*)butPtr);
		if (code != TCL_OK && code != TCL_CONTINUE
			&& code != TCL_BREAK) {
		    Tcl_AddErrorInfo(interp, "\n    (button invoke)");
		    Tcl_BackgroundException(interp, code);
		}
		Tcl_Release(interp);
	    }
	    Tcl_ServiceAll();
	    return 0;
	}
    }
    /* FALLTHRU */
    default:
	if (TkTranslateWinEvent(hwnd, message, wParam, lParam, &result)) {
	    return result;
	}
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
