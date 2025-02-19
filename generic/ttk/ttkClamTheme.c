/*
 * Copyright © 2004 Joe English
 *
 * "clam" theme; inspired by the XFCE family of Gnome themes.
 */

#include "tkInt.h"
#include "ttkThemeInt.h"

/*
 * Under windows, the Tk-provided XDrawLine and XDrawArc have an
 * off-by-one error in the end point. This is especially apparent with this
 * theme. Defining this macro as true handles this case.
 */
#if defined(_WIN32) && !defined(WIN32_XDRAWLINE_HACK)
  #define WIN32_XDRAWLINE_HACK 1
#else
  #define WIN32_XDRAWLINE_HACK 0
#endif

#define STR(x) StR(x)
#define StR(x) #x

#define SCROLLBAR_THICKNESS 14

#define FRAME_COLOR	"#dcdad5"
#define LIGHT_COLOR	"#ffffff"
#define DARK_COLOR	"#cfcdc8"
#define DARKER_COLOR	"#bab5ab"
#define DARKEST_COLOR	"#9e9a91"

/*------------------------------------------------------------------------
 * +++ Utilities.
 */

static GC Ttk_GCForColor(Tk_Window tkwin, Tcl_Obj* colorObj, Drawable d)
{
    GC gc = Tk_GCForColor(Tk_GetColorFromObj(tkwin, colorObj), d);

#ifdef MAC_OSX_TK
    /*
     * Workaround for Tk bug under Aqua where the default line width is 0.
     */
    Display *display = Tk_Display(tkwin);
    unsigned long mask = 0ul;
    XGCValues gcValues;

    gcValues.line_width = 1;
    mask = GCLineWidth;

    XChangeGC(display, gc, mask, &gcValues);
#endif

    return gc;
}

static void DrawSmoothBorder(
    Tk_Window tkwin, Drawable d, Ttk_Box b,
    Tcl_Obj *outerColorObj, Tcl_Obj *upperColorObj, Tcl_Obj *lowerColorObj)
{
    Display *display = Tk_Display(tkwin);
    int x1 = b.x, x2 = b.x + b.width - 1;
    int y1 = b.y, y2 = b.y + b.height - 1;
    const int w = WIN32_XDRAWLINE_HACK;
    GC gc;

    if (   outerColorObj
	&& (gc=Ttk_GCForColor(tkwin,outerColorObj,d)))
    {
	XDrawLine(display,d,gc, x1+1,y1, x2-1+w,y1);		/* N */
	XDrawLine(display,d,gc, x1+1,y2, x2-1+w,y2);		/* S */
	XDrawLine(display,d,gc, x1,y1+1, x1,y2-1+w);		/* W */
	XDrawLine(display,d,gc, x2,y1+1, x2,y2-1+w);		/* E */
    }

    if (   upperColorObj
	&& (gc=Ttk_GCForColor(tkwin,upperColorObj,d)))
    {
	XDrawLine(display,d,gc, x1+1,y1+1, x2-1+w,y1+1);	/* N */
	XDrawLine(display,d,gc, x1+1,y1+1, x1+1,y2-1);		/* W */
    }

    if (   lowerColorObj
	&& (gc=Ttk_GCForColor(tkwin,lowerColorObj,d)))
    {
	XDrawLine(display,d,gc, x2-1,y2-1, x1+1-w,y2-1);	/* S */
	XDrawLine(display,d,gc, x2-1,y2-1, x2-1,y1+1-w);	/* E */
    }
}

static GC BackgroundGC(Tk_Window tkwin, Tcl_Obj *backgroundObj)
{
    Tk_3DBorder bd = Tk_Get3DBorderFromObj(tkwin, backgroundObj);
    return Tk_3DBorderGC(tkwin, bd, TK_3D_FLAT_GC);
}

/*------------------------------------------------------------------------
 * +++ Border element.
 */

typedef struct {
    Tcl_Obj	*borderColorObj;
    Tcl_Obj	*lightColorObj;
    Tcl_Obj	*darkColorObj;
    Tcl_Obj	*reliefObj;
    Tcl_Obj	*borderWidthObj;	/* See <<NOTE-BORDERWIDTH>> */
} BorderElement;

static const Ttk_ElementOptionSpec BorderElementOptions[] = {
    { "-bordercolor", TK_OPTION_COLOR,
	offsetof(BorderElement,borderColorObj), DARKEST_COLOR },
    { "-lightcolor", TK_OPTION_COLOR,
	offsetof(BorderElement,lightColorObj), LIGHT_COLOR },
    { "-darkcolor", TK_OPTION_COLOR,
	offsetof(BorderElement,darkColorObj), DARK_COLOR },
    { "-relief", TK_OPTION_RELIEF,
	offsetof(BorderElement,reliefObj), "flat" },
    { "-borderwidth", TK_OPTION_PIXELS,
	offsetof(BorderElement,borderWidthObj), "2" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

/*
 * <<NOTE-BORDERWIDTH>>: -borderwidth is only partially supported:
 * in this theme, borders are always exactly 2 pixels thick.
 * With -borderwidth 0, border is not drawn at all;
 * otherwise a 2-pixel border is used.  For -borderwidth > 2,
 * the excess is used as padding.
 */

static void BorderElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    TCL_UNUSED(int *), /* widthPtr */
    TCL_UNUSED(int *), /* heightPtr */
    Ttk_Padding *paddingPtr)
{
    BorderElement *border = (BorderElement*)elementRecord;
    int borderWidth = 2;

    Tk_GetPixelsFromObj(NULL, tkwin, border->borderWidthObj, &borderWidth);
    if (borderWidth == 1) ++borderWidth;
    *paddingPtr = Ttk_UniformPadding((short)borderWidth);
}

static void BorderElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    BorderElement *border = (BorderElement *)elementRecord;
    int relief = TK_RELIEF_FLAT;
    int borderWidth = 2;
    Tcl_Obj *outer = 0, *upper = 0, *lower = 0;

    Tk_GetReliefFromObj(NULL, border->reliefObj, &relief);
    Tk_GetPixelsFromObj(NULL, tkwin, border->borderWidthObj, &borderWidth);

    if (borderWidth == 0) return;

    switch (relief) {
	case TK_RELIEF_GROOVE :
	case TK_RELIEF_RIDGE :
	case TK_RELIEF_RAISED :
	    outer = border->borderColorObj;
	    upper = border->lightColorObj;
	    lower = border->darkColorObj;
	    break;
	case TK_RELIEF_SUNKEN :
	    outer = border->borderColorObj;
	    upper = border->darkColorObj;
	    lower = border->lightColorObj;
	    break;
	case TK_RELIEF_FLAT :
	    outer = upper = lower = 0;
	    break;
	case TK_RELIEF_SOLID :
	    outer = upper = lower = border->borderColorObj;
	    break;
    }

    DrawSmoothBorder(tkwin, d, b, outer, upper, lower);
}

static const Ttk_ElementSpec BorderElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(BorderElement),
    BorderElementOptions,
    BorderElementSize,
    BorderElementDraw
};

/*------------------------------------------------------------------------
 * +++ Field element.
 */

typedef struct {
    Tcl_Obj	*borderColorObj;
    Tcl_Obj	*lightColorObj;
    Tcl_Obj	*backgroundObj;
} FieldElement;

static const Ttk_ElementOptionSpec FieldElementOptions[] = {
    { "-bordercolor", TK_OPTION_COLOR,
	offsetof(FieldElement,borderColorObj), DARKEST_COLOR },
    { "-lightcolor", TK_OPTION_COLOR,
	offsetof(FieldElement,lightColorObj), LIGHT_COLOR },
    { "-fieldbackground", TK_OPTION_BORDER,
	offsetof(FieldElement,backgroundObj), "white" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void FieldElementSize(
    TCL_UNUSED(void *), /* clientData */
    TCL_UNUSED(void *), /* elementRecord */
    TCL_UNUSED(Tk_Window),
    TCL_UNUSED(int *), /* widthPtr */
    TCL_UNUSED(int *), /* heightPtr */
    Ttk_Padding *paddingPtr)
{
    *paddingPtr = Ttk_UniformPadding(2);
}

static void FieldElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    FieldElement *field = (FieldElement *)elementRecord;
    Tk_3DBorder bg = Tk_Get3DBorderFromObj(tkwin, field->backgroundObj);
    Ttk_Box f = Ttk_PadBox(b, Ttk_UniformPadding(2));
    Tcl_Obj *outer = field->borderColorObj,
	    *inner = field->lightColorObj;

    DrawSmoothBorder(tkwin, d, b, outer, inner, inner);
    Tk_Fill3DRectangle(
	tkwin, d, bg, f.x, f.y, f.width, f.height, 0, TK_RELIEF_SUNKEN);
}

static const Ttk_ElementSpec FieldElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(FieldElement),
    FieldElementOptions,
    FieldElementSize,
    FieldElementDraw
};

/*
 * Modified field element for comboboxes:
 *	Right edge is expanded to overlap the dropdown button.
 */
static void ComboboxFieldElementDraw(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, Ttk_State state)
{
    FieldElement *field = (FieldElement *)elementRecord;
    GC gc = Ttk_GCForColor(tkwin,field->borderColorObj,d);

    ++b.width;
    FieldElementDraw(clientData, elementRecord, tkwin, d, b, state);

    XDrawLine(Tk_Display(tkwin), d, gc,
	    b.x + b.width - 1, b.y,
	    b.x + b.width - 1, b.y + b.height - 1 + WIN32_XDRAWLINE_HACK);
}

static const Ttk_ElementSpec ComboboxFieldElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(FieldElement),
    FieldElementOptions,
    FieldElementSize,
    ComboboxFieldElementDraw
};

/*------------------------------------------------------------------------
 * +++ Indicator elements for check and radio buttons.
 */

/*
 * Indicator image descriptor:
 */
typedef struct {
    int width;				/* unscaled width */
    int height;				/* unscaled height */
    const char *const offDataPtr;
    const char *const onDataPtr;
} IndicatorSpec;

static const char checkbtnOffData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <path d='m0 0v16h1v-15h15v-1z' fill='#9e9a91'/>\n\
     <path d='m15 1v14h-14v1h15v-15z' fill='#cfcdc8'/>\n\
     <rect x='1' y='1' width='14' height='14' fill='#ffffff'/>\n\
    </svg>";

static const char checkbtnOnData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <path d='m0 0v16h1v-15h15v-1z' fill='#9e9a91'/>\n\
     <path d='m15 1v14h-14v1h15v-15z' fill='#cfcdc8'/>\n\
     <rect x='1' y='1' width='14' height='14' fill='#ffffff'/>\n\
     <path d='m5 5 6 6m0-6-6 6' fill='none' stroke='#000000' stroke-linecap='round' stroke-width='2'/>\n\
    </svg>";

static const IndicatorSpec checkbutton_spec = {
    16, 16,
    checkbtnOffData,
    checkbtnOnData
};

static const char radiobtnOffData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <defs>\n\
      <linearGradient id='linearGradient' x1='5' y1='5' x2='11' y2='11' gradientUnits='userSpaceOnUse'>\n\
       <stop stop-color='#9e9a91' offset='0'/>\n\
       <stop stop-color='#cfcdc8' offset='1'/>\n\
      </linearGradient>\n\
     </defs>\n\
     <circle cx='8' cy='8' r='8' fill='url(#linearGradient)'/>\n\
     <circle cx='8' cy='8' r='7' fill='#ffffff'/>\n\
    </svg>";

static const char radiobtnOnData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <defs>\n\
      <linearGradient id='linearGradient' x1='5' y1='5' x2='11' y2='11' gradientUnits='userSpaceOnUse'>\n\
       <stop stop-color='#9e9a91' offset='0'/>\n\
       <stop stop-color='#cfcdc8' offset='1'/>\n\
      </linearGradient>\n\
     </defs>\n\
     <circle cx='8' cy='8' r='8' fill='url(#linearGradient)'/>\n\
     <circle cx='8' cy='8' r='7' fill='#ffffff'/>\n\
     <circle cx='8' cy='8' r='4' fill='#000000'/>\n\
    </svg>";

static const IndicatorSpec radiobutton_spec = {
    16, 16,
    radiobtnOffData,
    radiobtnOnData
};

typedef struct {
    Tcl_Obj *marginObj;
    Tcl_Obj *backgroundObj;
    Tcl_Obj *foregroundObj;
    Tcl_Obj *upperColorObj;
    Tcl_Obj *lowerColorObj;
} IndicatorElement;

static const Ttk_ElementOptionSpec IndicatorElementOptions[] = {
    { "-indicatormargin", TK_OPTION_STRING,
	offsetof(IndicatorElement,marginObj), "1" },
    { "-indicatorbackground", TK_OPTION_COLOR,
	offsetof(IndicatorElement,backgroundObj), "white" },
    { "-indicatorforeground", TK_OPTION_COLOR,
	offsetof(IndicatorElement,foregroundObj), "black" },
    { "-upperbordercolor", TK_OPTION_COLOR,
	offsetof(IndicatorElement,upperColorObj), DARKEST_COLOR },
    { "-lowerbordercolor", TK_OPTION_COLOR,
	offsetof(IndicatorElement,lowerColorObj), DARK_COLOR },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void IndicatorElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    const IndicatorSpec *spec = (const IndicatorSpec *)clientData;
    IndicatorElement *indicator = (IndicatorElement *)elementRecord;
    Ttk_Padding margins;
    double scalingLevel = TkScalingLevel(tkwin);

    Ttk_GetPaddingFromObj(NULL, tkwin, indicator->marginObj, &margins);
    *widthPtr = spec->width * scalingLevel + Ttk_PaddingWidth(margins);
    *heightPtr = spec->height * scalingLevel + Ttk_PaddingHeight(margins);
}

static void ColorToStr(
    const XColor *colorPtr, char *colorStr)	/* in the format "RRGGBB" */
{
    snprintf(colorStr, 7, "%02x%02x%02x",
	     colorPtr->red >> 8, colorPtr->green >> 8, colorPtr->blue >> 8);
}

static void ImageChanged(		/* to be passed to Tk_GetImage() */
    TCL_UNUSED(void *),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
}

static void IndicatorElementDraw(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, Ttk_State state)
{
    IndicatorElement *indicator = (IndicatorElement *)elementRecord;
    Ttk_Padding padding;
    const IndicatorSpec *spec = (const IndicatorSpec *)clientData;
    double scalingLevel = TkScalingLevel(tkwin);
    int width = spec->width * scalingLevel;
    int height = spec->height * scalingLevel;

    char upperBdColorStr[7], lowerBdColorStr[7], bgColorStr[7], fgColorStr[7];
    unsigned int selected = (state & TTK_STATE_SELECTED);
    Tcl_Interp *interp = Tk_Interp(tkwin);
    char imgName[60];
    Tk_Image img;

    const char *svgDataPtr;
    size_t svgDataLen;
    char *svgDataCopy;
    char *upperBdColorPtr, *lowerBdColorPtr, *bgColorPtr, *fgColorPtr;
    const char *cmdFmt;
    size_t scriptSize;
    char *script;
    int code;

    Ttk_GetPaddingFromObj(NULL, tkwin, indicator->marginObj, &padding);
    b = Ttk_PadBox(b, padding);

    /*
     * Sanity check
     */
    if (   b.x < 0
	|| b.y < 0
	|| Tk_Width(tkwin) < b.x + width
	|| Tk_Height(tkwin) < b.y + height)
    {
	/* Oops!  Not enough room to display the image.
	 * Don't draw anything.
	 */
	return;
    }

    /*
     * Construct the color strings upperBdColorStr, lowerBdColorStr,
     * bgColorStr, and fgColorStr
     */
    ColorToStr(Tk_GetColorFromObj(tkwin, indicator->upperColorObj),
	       upperBdColorStr);
    ColorToStr(Tk_GetColorFromObj(tkwin, indicator->lowerColorObj),
	       lowerBdColorStr);
    ColorToStr(Tk_GetColorFromObj(tkwin, indicator->backgroundObj),
	       bgColorStr);
    ColorToStr(Tk_GetColorFromObj(tkwin, indicator->foregroundObj),
	       fgColorStr);

    /*
     * Check whether there is an SVG image of this size for the indicator's
     * type (0 = checkbtn, 1 = radiobtn) and these color strings
     */
    snprintf(imgName, sizeof(imgName),
	     "::tk::icons::indicator_clam%d_%d_%s_%s_%s_%s",
	     width, spec->offDataPtr == radiobtnOffData,
	     upperBdColorStr, lowerBdColorStr, bgColorStr,
	     selected ? fgColorStr : "XXXXXX");
    img = Tk_GetImage(interp, tkwin, imgName, ImageChanged, NULL);
    if (img == NULL) {
	/*
	 * Determine the SVG data to use for the photo image
	 */
	svgDataPtr = (selected ? spec->onDataPtr : spec->offDataPtr);

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

	upperBdColorPtr = strstr(svgDataCopy, "9e9a91");
	lowerBdColorPtr = strstr(svgDataCopy, "cfcdc8");
	bgColorPtr =	  strstr(svgDataCopy, "ffffff");
	fgColorPtr =	  strstr(svgDataCopy, "000000");

	assert(upperBdColorPtr);
	assert(lowerBdColorPtr);
	assert(bgColorPtr);

	memcpy(upperBdColorPtr, upperBdColorStr, 6);
	memcpy(lowerBdColorPtr, lowerBdColorStr, 6);
	memcpy(bgColorPtr, bgColorStr, 6);
	if (fgColorPtr != NULL) {
	    memcpy(fgColorPtr, fgColorStr, 6);
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
	code = Tcl_EvalEx(interp, script, -1, TCL_EVAL_GLOBAL);
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
    Tk_RedrawImage(img, 0, 0, width, height, d, b.x, b.y);
    Tk_FreeImage(img);
}

static const Ttk_ElementSpec IndicatorElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(IndicatorElement),
    IndicatorElementOptions,
    IndicatorElementSize,
    IndicatorElementDraw
};

/*------------------------------------------------------------------------
 * +++ Grips.
 *
 * TODO: factor this with ThumbElementDraw
 */

typedef struct {
    Tcl_Obj	*lightColorObj;
    Tcl_Obj	*borderColorObj;
    Tcl_Obj	*gripSizeObj;
} GripElement;

static const Ttk_ElementOptionSpec GripElementOptions[] = {
    { "-lightcolor", TK_OPTION_COLOR,
	offsetof(GripElement,lightColorObj), LIGHT_COLOR },
    { "-bordercolor", TK_OPTION_COLOR,
	offsetof(GripElement,borderColorObj), DARKEST_COLOR },
    { "-gripsize", TK_OPTION_PIXELS,
	offsetof(GripElement,gripSizeObj), "7.5p" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void GripElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    Ttk_Orient orient = (Ttk_Orient)PTR2INT(clientData);
    GripElement *grip = (GripElement *)elementRecord;
    int gripSize = 0;

    Tk_GetPixelsFromObj(NULL, tkwin, grip->gripSizeObj, &gripSize);
    if (orient == TTK_ORIENT_HORIZONTAL) {
	*widthPtr = gripSize;
    } else {
	*heightPtr = gripSize;
    }
}

static void GripElementDraw(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    const int w = WIN32_XDRAWLINE_HACK;
    Ttk_Orient orient = (Ttk_Orient)PTR2INT(clientData);
    GripElement *grip = (GripElement *)elementRecord;
    GC lightGC = Ttk_GCForColor(tkwin,grip->lightColorObj,d);
    GC darkGC = Ttk_GCForColor(tkwin,grip->borderColorObj,d);
    int gripPad = 1, gripSize = 0;
    int i;

    Tk_GetPixelsFromObj(NULL, tkwin, grip->gripSizeObj, &gripSize);

    if (orient == TTK_ORIENT_HORIZONTAL) {
	int x = b.x + (b.width - gripSize) / 2;
	int y1 = b.y + gripPad, y2 = b.y + b.height - gripPad - 1 + w;
	for (i=0; i<gripSize; ++i) {
	    XDrawLine(Tk_Display(tkwin), d, (i&1)?lightGC:darkGC, x,y1, x,y2);
	    ++x;
	}
    } else {
	int y = b.y + (b.height - gripSize) / 2;
	int x1 = b.x + gripPad, x2 = b.x + b.width - gripPad - 1 + w;
	for (i=0; i<gripSize; ++i) {
	    XDrawLine(Tk_Display(tkwin), d, (i&1)?lightGC:darkGC, x1,y, x2,y);
	    ++y;
	}
    }
}

static const Ttk_ElementSpec GripElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(GripElement),
    GripElementOptions,
    GripElementSize,
    GripElementDraw
};

/*------------------------------------------------------------------------
 * +++ Scrollbar elements: trough, arrows, thumb.
 *
 * Notice that the trough element has 0 internal padding;
 * that way the thumb and arrow borders overlap the trough.
 */

typedef struct { /* Common element record for scrollbar elements */
    Tcl_Obj	*orientObj;
    Tcl_Obj	*backgroundObj;
    Tcl_Obj	*borderColorObj;
    Tcl_Obj	*troughColorObj;
    Tcl_Obj	*lightColorObj;
    Tcl_Obj	*darkColorObj;
    Tcl_Obj	*arrowColorObj;
    Tcl_Obj	*arrowSizeObj;
    Tcl_Obj	*gripSizeObj;
    Tcl_Obj	*sliderlengthObj;
} ScrollbarElement;

static const Ttk_ElementOptionSpec ScrollbarElementOptions[] = {
    { "-orient", TK_OPTION_ANY,
	offsetof(ScrollbarElement, orientObj), "horizontal" },
    { "-background", TK_OPTION_BORDER,
	offsetof(ScrollbarElement,backgroundObj), FRAME_COLOR },
    { "-bordercolor", TK_OPTION_COLOR,
	offsetof(ScrollbarElement,borderColorObj), DARKEST_COLOR },
    { "-troughcolor", TK_OPTION_COLOR,
	offsetof(ScrollbarElement,troughColorObj), DARKER_COLOR },
    { "-lightcolor", TK_OPTION_COLOR,
	offsetof(ScrollbarElement,lightColorObj), LIGHT_COLOR },
    { "-darkcolor", TK_OPTION_COLOR,
	offsetof(ScrollbarElement,darkColorObj), DARK_COLOR },
    { "-arrowcolor", TK_OPTION_COLOR,
	offsetof(ScrollbarElement,arrowColorObj), "#000000" },
    { "-arrowsize", TK_OPTION_PIXELS,
	offsetof(ScrollbarElement,arrowSizeObj), STR(SCROLLBAR_THICKNESS) },
    { "-gripsize", TK_OPTION_PIXELS,
	offsetof(ScrollbarElement,gripSizeObj), "7.5p" },
    { "-sliderlength", TK_OPTION_PIXELS,
	offsetof(ScrollbarElement,sliderlengthObj), "30" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void TroughElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    ScrollbarElement *sb = (ScrollbarElement *)elementRecord;
    GC gcb = Ttk_GCForColor(tkwin,sb->borderColorObj,d);
    GC gct = Ttk_GCForColor(tkwin,sb->troughColorObj,d);

    XFillRectangle(Tk_Display(tkwin), d, gct, b.x, b.y, b.width-1, b.height-1);
    XDrawRectangle(Tk_Display(tkwin), d, gcb, b.x, b.y, b.width-1, b.height-1);
}

static const Ttk_ElementSpec TroughElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(ScrollbarElement),
    ScrollbarElementOptions,
    TtkNullElementSize,
    TroughElementDraw
};

static void ThumbElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    ScrollbarElement *sb = (ScrollbarElement *)elementRecord;
    int size = SCROLLBAR_THICKNESS;

    Tk_GetPixelsFromObj(NULL, tkwin, sb->arrowSizeObj, &size);
    *widthPtr = *heightPtr = size;
}

static void ThumbElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    ScrollbarElement *sb = (ScrollbarElement *)elementRecord;
    int gripSize = 0;
    Ttk_Orient orient = TTK_ORIENT_HORIZONTAL;
    GC lightGC, darkGC;
    int x1, y1, x2, y2, dx, dy, i;
    const int w = WIN32_XDRAWLINE_HACK;

    DrawSmoothBorder(tkwin, d, b,
	sb->borderColorObj, sb->lightColorObj, sb->darkColorObj);
    XFillRectangle(
	Tk_Display(tkwin), d, BackgroundGC(tkwin, sb->backgroundObj),
	b.x+2, b.y+2, b.width-4, b.height-4);

    /*
     * Draw grip:
     */
    Ttk_GetOrientFromObj(NULL, sb->orientObj, &orient);
    Tk_GetPixelsFromObj(NULL, tkwin, sb->gripSizeObj, &gripSize);
    lightGC = Ttk_GCForColor(tkwin,sb->lightColorObj,d);
    darkGC = Ttk_GCForColor(tkwin,sb->borderColorObj,d);

    if (orient == TTK_ORIENT_HORIZONTAL) {
	dx = 1; dy = 0;
	x1 = x2 = b.x + (b.width - gripSize) / 2;
	y1 = b.y + 2;
	y2 = b.y + b.height - 3 + w;
    } else {
	dx = 0; dy = 1;
	y1 = y2 = b.y + (b.height - gripSize) / 2;
	x1 = b.x + 2;
	x2 = b.x + b.width - 3 + w;
    }

    for (i=0; i<gripSize; ++i) {
	XDrawLine(Tk_Display(tkwin), d, (i&1)?lightGC:darkGC, x1,y1, x2,y2);
	x1 += dx; x2 += dx; y1 += dy; y2 += dy;

    }
}

static const Ttk_ElementSpec ThumbElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(ScrollbarElement),
    ScrollbarElementOptions,
    ThumbElementSize,
    ThumbElementDraw
};

/*------------------------------------------------------------------------
 * +++ Slider element.
 */
static void SliderElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    ScrollbarElement *sb = (ScrollbarElement *)elementRecord;
    int length, thickness;
    Ttk_Orient orient;

    length = thickness = SCROLLBAR_THICKNESS;
    Ttk_GetOrientFromObj(NULL, sb->orientObj, &orient);
    Tk_GetPixelsFromObj(NULL, tkwin, sb->arrowSizeObj, &thickness);
    Tk_GetPixelsFromObj(NULL, tkwin, sb->sliderlengthObj, &length);
    if (orient == TTK_ORIENT_VERTICAL) {
	*heightPtr = length;
	*widthPtr = thickness;
    } else {
	*heightPtr = thickness;
	*widthPtr = length;
    }
}

static const Ttk_ElementSpec SliderElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(ScrollbarElement),
    ScrollbarElementOptions,
    SliderElementSize,
    ThumbElementDraw
};

/*------------------------------------------------------------------------
 * +++ Progress bar element
 */
static void PbarElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    SliderElementSize(clientData, elementRecord, tkwin,
	    widthPtr, heightPtr, paddingPtr);
    *paddingPtr = Ttk_UniformPadding(2);
    *widthPtr += 4;
    *heightPtr += 4;
}

static void PbarElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    ScrollbarElement *sb = (ScrollbarElement *)elementRecord;

    b = Ttk_PadBox(b, Ttk_UniformPadding(2));
    if (b.width > 4 && b.height > 4) {
	DrawSmoothBorder(tkwin, d, b,
	    sb->borderColorObj, sb->lightColorObj, sb->darkColorObj);
	XFillRectangle(Tk_Display(tkwin), d,
	    BackgroundGC(tkwin, sb->backgroundObj),
	    b.x+2, b.y+2, b.width-4, b.height-4);
    }
}

static const Ttk_ElementSpec PbarElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(ScrollbarElement),
    ScrollbarElementOptions,
    PbarElementSize,
    PbarElementDraw
};

/*------------------------------------------------------------------------
 * +++ Scrollbar arrows.
 */
static void ArrowElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    ScrollbarElement *sb = (ScrollbarElement *)elementRecord;
    ArrowDirection direction = (ArrowDirection)PTR2INT(clientData);
    double scalingLevel = TkScalingLevel(tkwin);
    Ttk_Padding padding = Ttk_UniformPadding(round(3 * scalingLevel));
    int size = SCROLLBAR_THICKNESS;

    Tk_GetPixelsFromObj(NULL, tkwin, sb->arrowSizeObj, &size);
    size -= Ttk_PaddingWidth(padding);
    TtkArrowSize(size/2, direction, widthPtr, heightPtr);
    *widthPtr += Ttk_PaddingWidth(padding);
    *heightPtr += Ttk_PaddingHeight(padding);
    if (*widthPtr < *heightPtr) {
	*widthPtr = *heightPtr;
    } else {
	*heightPtr = *widthPtr;
    }
}

static void ArrowElementDraw(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    ScrollbarElement *sb = (ScrollbarElement *)elementRecord;
    ArrowDirection direction = (ArrowDirection)PTR2INT(clientData);
    double scalingLevel = TkScalingLevel(tkwin);
    Ttk_Padding padding = Ttk_UniformPadding(round(3 * scalingLevel));
    int cx, cy;
    GC gc = Ttk_GCForColor(tkwin, sb->arrowColorObj, d);

    DrawSmoothBorder(tkwin, d, b,
	sb->borderColorObj, sb->lightColorObj, sb->darkColorObj);

    XFillRectangle(
	Tk_Display(tkwin), d, BackgroundGC(tkwin, sb->backgroundObj),
	b.x+2, b.y+2, b.width-4, b.height-4);

    b = Ttk_PadBox(b, padding);

    switch (direction) {
	case ARROW_UP:
	case ARROW_DOWN:
	    TtkArrowSize(b.width/2, direction, &cx, &cy);
	    if ((b.height - cy) % 2 == 1) {
		++cy;
	    }
	    break;
	case ARROW_LEFT:
	case ARROW_RIGHT:
	    TtkArrowSize(b.height/2, direction, &cx, &cy);
	    if ((b.width - cx) % 2 == 1) {
		++cx;
	    }
	    break;
    }

    b = Ttk_AnchorBox(b, cx, cy, TK_ANCHOR_CENTER);

    TtkFillArrow(Tk_Display(tkwin), d, gc, b, direction);
}

static const Ttk_ElementSpec ArrowElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(ScrollbarElement),
    ScrollbarElementOptions,
    ArrowElementSize,
    ArrowElementDraw
};

/*
 * Modified arrow element for spinboxes:
 *	The width and height are different.
 */
static void SpinboxArrowElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    ScrollbarElement *sb = (ScrollbarElement *)elementRecord;
    ArrowDirection direction = (ArrowDirection)PTR2INT(clientData);
    double scalingLevel = TkScalingLevel(tkwin);
    Ttk_Padding padding = Ttk_UniformPadding(round(3 * scalingLevel));
    int size = 10;

    Tk_GetPixelsFromObj(NULL, tkwin, sb->arrowSizeObj, &size);
    size -= Ttk_PaddingWidth(padding);
    TtkArrowSize(size/2, direction, widthPtr, heightPtr);
    *widthPtr += Ttk_PaddingWidth(padding);
    *heightPtr += Ttk_PaddingHeight(padding);
}

static const Ttk_ElementSpec SpinboxArrowElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(ScrollbarElement),
    ScrollbarElementOptions,
    SpinboxArrowElementSize,
    ArrowElementDraw
};

/*------------------------------------------------------------------------
 * +++ Notebook elements.
 *
 * Note: Tabs, except for the rightmost, overlap the neighbor to
 * their right by one pixel.
 */

typedef struct {
    Tcl_Obj *backgroundObj;
    Tcl_Obj *borderColorObj;
    Tcl_Obj *lightColorObj;
    Tcl_Obj *darkColorObj;
} NotebookElement;

static const Ttk_ElementOptionSpec NotebookElementOptions[] = {
    { "-background", TK_OPTION_BORDER,
	offsetof(NotebookElement,backgroundObj), FRAME_COLOR },
    { "-bordercolor", TK_OPTION_COLOR,
	offsetof(NotebookElement,borderColorObj), DARKEST_COLOR },
    { "-lightcolor", TK_OPTION_COLOR,
	offsetof(NotebookElement,lightColorObj), LIGHT_COLOR },
    { "-darkcolor", TK_OPTION_COLOR,
	offsetof(NotebookElement,darkColorObj), DARK_COLOR },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void TabElementSize(
    TCL_UNUSED(void *), /* clientData */
    TCL_UNUSED(void *), /* elementRecord */
    Tk_Window tkwin,
    TCL_UNUSED(int *), /* widthPtr */
    TCL_UNUSED(int *), /* heightPtr */
    Ttk_Padding *paddingPtr)
{
    Ttk_PositionSpec nbTabsStickBit = TTK_STICK_S;
    TkMainInfo *mainInfoPtr = ((TkWindow *) tkwin)->mainPtr;
    int borderWidth = 2;

    if (mainInfoPtr != NULL) {
	nbTabsStickBit = (Ttk_PositionSpec) mainInfoPtr->ttkNbTabsStickBit;
    }

    *paddingPtr = Ttk_UniformPadding((short)borderWidth);
    switch (nbTabsStickBit) {
	default:
	case TTK_STICK_S:
	    paddingPtr->bottom = 0;
	    break;
	case TTK_STICK_N:
	    paddingPtr->top = 0;
	    break;
	case TTK_STICK_E:
	    paddingPtr->right = 0;
	    break;
	case TTK_STICK_W:
	    paddingPtr->left = 0;
	    break;
    }
}

static void TabElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    Ttk_PositionSpec nbTabsStickBit = TTK_STICK_S;
    TkMainInfo *mainInfoPtr = ((TkWindow *) tkwin)->mainPtr;
    int borderWidth = 2, delta = 0;
    NotebookElement *tab = (NotebookElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, tab->backgroundObj);
    Display *display = Tk_Display(tkwin);
    int x1, y1, x2, y2;
    GC gc;
    const int w = WIN32_XDRAWLINE_HACK;

    if (mainInfoPtr != NULL) {
	nbTabsStickBit = (Ttk_PositionSpec) mainInfoPtr->ttkNbTabsStickBit;
    }

    if (state & TTK_STATE_SELECTED) {
	delta = borderWidth;
    }

    switch (nbTabsStickBit) {
	default:
	case TTK_STICK_S:
	    if (state & TTK_STATE_LAST) {		/* rightmost tab */
		--b.width;
	    }

	    Tk_Fill3DRectangle(tkwin, d, border,
		b.x+2, b.y+2, b.width-1, b.height-2+delta,
		borderWidth, TK_RELIEF_FLAT);

	    x1 = b.x;		y1 = b.y;		/* top left */
	    x2 = b.x + b.width; y2 = b.y + b.height-1;	/* bottom right */

	    gc = Ttk_GCForColor(tkwin, tab->borderColorObj, d);
	    XDrawLine(display, d, gc, x1, y1+1, x1, y2+1+w);
	    XDrawLine(display, d, gc, x2, y1+1, x2, y2+1+w);
	    XDrawLine(display, d, gc, x1+1, y1, x2-1+w, y1);

	    gc = Ttk_GCForColor(tkwin, tab->lightColorObj, d);
	    XDrawLine(display, d, gc, x1+1, y1+1, x1+1, y2+delta+w);
	    XDrawLine(display, d, gc, x1+1, y1+1, x2-1+w, y1+1);
	    break;

	case TTK_STICK_N:
	    if (state & TTK_STATE_LAST) {		/* rightmost tab */
		--b.width;
	    }

	    Tk_Fill3DRectangle(tkwin, d, border,
		b.x+2, b.y-delta, b.width-1, b.height-2+delta,
		borderWidth, TK_RELIEF_FLAT);

	    x1 = b.x;		y1 = b.y + b.height-1;	/* bottom left */
	    x2 = b.x + b.width; y2 = b.y;		/* top right */

	    gc = Ttk_GCForColor(tkwin, tab->borderColorObj, d);
	    XDrawLine(display, d, gc, x1, y1-1, x1, y2-1-w);
	    XDrawLine(display, d, gc, x2, y1-1, x2, y2-1-w);
	    XDrawLine(display, d, gc, x1+1, y1, x2-1+w, y1);

	    gc = Ttk_GCForColor(tkwin, tab->lightColorObj, d);
	    XDrawLine(display, d, gc, x1+1, y1-1, x1+1, y2-delta-w);
	    XDrawLine(display, d, gc, x1+1, y1-1, x2-1+w, y1-1);
	    break;

	case TTK_STICK_E:
	    if (state & TTK_STATE_LAST) {		/* bottommost tab */
		--b.height;
	    }

	    Tk_Fill3DRectangle(tkwin, d, border,
		b.x+2, b.y+2, b.width-2+delta, b.height-1,
		borderWidth, TK_RELIEF_FLAT);

	    x1 = b.x;		  y1 = b.y;		/* top left */
	    x2 = b.x + b.width-1; y2 = b.y + b.height;	/* bottom right */

	    gc = Ttk_GCForColor(tkwin, tab->borderColorObj, d);
	    XDrawLine(display, d, gc, x1, y1+1, x1, y2-1+w);
	    XDrawLine(display, d, gc, x1+1, y1, x2+1+w, y1);
	    XDrawLine(display, d, gc, x1+1, y2, x2+1+w, y2);

	    gc = Ttk_GCForColor(tkwin, tab->lightColorObj, d);
	    XDrawLine(display, d, gc, x1+1, y1+1, x1+1, y2-1+w);
	    XDrawLine(display, d, gc, x1+1, y1+1, x2+delta+w, y1+1);
	    break;

	case TTK_STICK_W:
	    if (state & TTK_STATE_LAST) {		/* bottommost tab */
		--b.height;
	    }

	    Tk_Fill3DRectangle(tkwin, d, border,
		b.x-delta, b.y+2, b.width-2+delta, b.height-1,
		borderWidth, TK_RELIEF_FLAT);

	    x1 = b.x + b.width-1; y1 = b.y;		/* top right */
	    x2 = b.x;		  y2 = b.y + b.height;	/* bottom left */

	    gc = Ttk_GCForColor(tkwin, tab->borderColorObj, d);
	    XDrawLine(display, d, gc, x1, y1+1, x1, y2-1+w);
	    XDrawLine(display, d, gc, x1-1, y1, x2-1-w, y1);
	    XDrawLine(display, d, gc, x1-1, y2, x2-1-w, y2);

	    gc = Ttk_GCForColor(tkwin, tab->lightColorObj, d);
	    XDrawLine(display, d, gc, x1-1, y1+1, x1-1, y2-1+w);
	    XDrawLine(display, d, gc, x1-1, y1+1, x2-delta-w, y1+1);
	    break;
    }
}

static const Ttk_ElementSpec TabElementSpec =
{
    TK_STYLE_VERSION_2,
    sizeof(NotebookElement),
    NotebookElementOptions,
    TabElementSize,
    TabElementDraw
};

static void ClientElementSize(
    TCL_UNUSED(void *), /* clientData */
    TCL_UNUSED(void *), /* elementRecord */
    TCL_UNUSED(Tk_Window),
    TCL_UNUSED(int *), /* widthPtr */
    TCL_UNUSED(int *), /* heightPtr */
    Ttk_Padding *paddingPtr)
{
    int borderWidth = 2;

    *paddingPtr = Ttk_UniformPadding((short)borderWidth);
}

static void ClientElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    NotebookElement *ce = (NotebookElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, ce->backgroundObj);
    int borderWidth = 2;

    Tk_Fill3DRectangle(tkwin, d, border,
	b.x, b.y, b.width, b.height, borderWidth,TK_RELIEF_FLAT);
    DrawSmoothBorder(tkwin, d, b,
	ce->borderColorObj, ce->lightColorObj, ce->darkColorObj);
}

static const Ttk_ElementSpec ClientElementSpec =
{
    TK_STYLE_VERSION_2,
    sizeof(NotebookElement),
    NotebookElementOptions,
    ClientElementSize,
    ClientElementDraw
};

/*------------------------------------------------------------------------
 * +++ Modified widget layouts.
 */

TTK_BEGIN_LAYOUT_TABLE(LayoutTable)

TTK_LAYOUT("TCombobox",
    TTK_NODE("Combobox.downarrow", TTK_PACK_RIGHT|TTK_FILL_Y)
    TTK_GROUP("Combobox.field", TTK_FILL_BOTH,
	TTK_GROUP("Combobox.padding", TTK_FILL_BOTH,
	    TTK_NODE("Combobox.textarea", TTK_FILL_BOTH))))

TTK_END_LAYOUT_TABLE

/*------------------------------------------------------------------------
 * +++ Initialization.
 */

MODULE_SCOPE int
TtkClamTheme_Init(Tcl_Interp *interp)
{
    Ttk_Theme theme = Ttk_CreateTheme(interp, "clam", 0);

    if (!theme) {
	return TCL_ERROR;
    }

    Ttk_RegisterElement(interp, theme, "border",
	    &BorderElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "field",
	    &FieldElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "Combobox.field",
	    &ComboboxFieldElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "trough",
	    &TroughElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "thumb",
	    &ThumbElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "uparrow",
	    &ArrowElementSpec, INT2PTR(ARROW_UP));
    Ttk_RegisterElement(interp, theme, "Spinbox.uparrow",
	    &SpinboxArrowElementSpec, INT2PTR(ARROW_UP));
    Ttk_RegisterElement(interp, theme, "downarrow",
	    &ArrowElementSpec, INT2PTR(ARROW_DOWN));
    Ttk_RegisterElement(interp, theme, "Spinbox.downarrow",
	    &SpinboxArrowElementSpec, INT2PTR(ARROW_DOWN));
    Ttk_RegisterElement(interp, theme, "leftarrow",
	    &ArrowElementSpec, INT2PTR(ARROW_LEFT));
    Ttk_RegisterElement(interp, theme, "rightarrow",
	    &ArrowElementSpec, INT2PTR(ARROW_RIGHT));
    Ttk_RegisterElement(interp, theme, "arrow",
	    &ArrowElementSpec, INT2PTR(ARROW_UP));

    Ttk_RegisterElement(interp, theme, "Checkbutton.indicator",
	    &IndicatorElementSpec, (void *)&checkbutton_spec);
    Ttk_RegisterElement(interp, theme, "Radiobutton.indicator",
	    &IndicatorElementSpec, (void *)&radiobutton_spec);

    Ttk_RegisterElement(interp, theme, "tab", &TabElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "client", &ClientElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "slider", &SliderElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "bar", &PbarElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "pbar", &PbarElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "hgrip",
	    &GripElementSpec,  INT2PTR(TTK_ORIENT_HORIZONTAL));
    Ttk_RegisterElement(interp, theme, "vgrip",
	    &GripElementSpec,  INT2PTR(TTK_ORIENT_VERTICAL));

    Ttk_RegisterLayouts(theme, LayoutTable);

    Tcl_PkgProvide(interp, "ttk::theme::clam", TTK_VERSION);

    return TCL_OK;
}
