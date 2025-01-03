/*
 * Copyright © 2003 Joe English
 *
 * Tk alternate theme, intended to match the MSUE and Gtk's (old) default theme
 */

#include "tkInt.h"
#include "ttkThemeInt.h"

#if defined(_WIN32)
  #define WIN32_XDRAWLINE_HACK 1
#else
  #define WIN32_XDRAWLINE_HACK 0
#endif

#if defined(MAC_OSX_TK)
  #define IGNORES_VISUAL
#endif

#define BORDERWIDTH     2
#define SCROLLBAR_WIDTH 14
#define MIN_THUMB_SIZE  8

/*
 *----------------------------------------------------------------------
 *
 * Helper routines for border drawing:
 *
 * NOTE: MSUE specifies a slightly different arrangement
 * for button borders than for other elements; "shadowColors"
 * is for button borders.
 *
 * Please excuse the gross misspelling "LITE" for "LIGHT",
 * but it makes things line up nicer.
 */

enum BorderColor { FLAT = 1, LITE = 2, DARK = 3, BRDR = 4 };

/* top-left outer, top-left inner, bottom-right inner, bottom-right outer */
static const enum BorderColor shadowColors[6][4] = {
    { FLAT, FLAT, FLAT, FLAT },	/* TK_RELIEF_FLAT   = 0*/
    { DARK, LITE, DARK, LITE },	/* TK_RELIEF_GROOVE = 1*/
    { LITE, FLAT, DARK, BRDR },	/* TK_RELIEF_RAISED = 2*/
    { LITE, DARK, LITE, DARK },	/* TK_RELIEF_RIDGE  = 3*/
    { BRDR, BRDR, BRDR, BRDR },	/* TK_RELIEF_SOLID  = 4*/
    { BRDR, DARK, FLAT, LITE }	/* TK_RELIEF_SUNKEN = 5*/
};

/* top-left, bottom-right */
static const enum BorderColor thinShadowColors[6][4] = {
    { FLAT, FLAT },	/* TK_RELIEF_FLAT   = 0*/
    { DARK, LITE },	/* TK_RELIEF_GROOVE = 1*/
    { LITE, DARK },	/* TK_RELIEF_RAISED = 2*/
    { LITE, DARK },	/* TK_RELIEF_RIDGE  = 3*/
    { BRDR, BRDR },	/* TK_RELIEF_SOLID  = 4*/
    { DARK, LITE }	/* TK_RELIEF_SUNKEN = 5*/
};

static void DrawCorner(
    Tk_Window tkwin,
    Drawable d,
    Tk_3DBorder border,			/* get most GCs from here... */
    GC borderGC,			/* "window border" color GC */
    int x,int y, int width,int height,	/* where to draw */
    int corner,				/* 0 => top left; 1 => bottom right */
    enum BorderColor color)
{
    XPoint points[3];
    GC gc;

    --width; --height;
    points[0].x = x;			points[0].y = y+height;
    points[1].x = x+width*corner;	points[1].y = y+height*corner;
    points[2].x = x+width;		points[2].y = y;

    if (color == BRDR)
	gc = borderGC;
    else
	gc = Tk_3DBorderGC(tkwin, border, (int)color);

    XDrawLines(Tk_Display(tkwin), d, gc, points, 3, CoordModeOrigin);
}

static void DrawBorder(
    Tk_Window tkwin, Drawable d, Tk_3DBorder border, XColor *borderColor,
    Ttk_Box b, int borderWidth, int relief)
{
    GC borderGC = Tk_GCForColor(borderColor, d);

    switch (borderWidth) {
	case 2: /* "thick" border */
	    DrawCorner(tkwin, d, border, borderGC,
		b.x, b.y, b.width, b.height, 0,shadowColors[relief][0]);
	    DrawCorner(tkwin, d, border, borderGC,
		b.x+1, b.y+1, b.width-2, b.height-2, 0,shadowColors[relief][1]);
	    DrawCorner(tkwin, d, border, borderGC,
		b.x+1, b.y+1, b.width-2, b.height-2, 1,shadowColors[relief][2]);
	    DrawCorner(tkwin, d, border, borderGC,
		b.x, b.y, b.width, b.height, 1,shadowColors[relief][3]);
	    break;
	case 1: /* "thin" border */
	    DrawCorner(tkwin, d, border, borderGC,
		b.x, b.y, b.width, b.height, 0, thinShadowColors[relief][0]);
	    DrawCorner(tkwin, d, border, borderGC,
		b.x, b.y, b.width, b.height, 1, thinShadowColors[relief][1]);
	    break;
	case 0:	/* no border -- do nothing */
	    break;
	default: /* Fall back to Motif-style borders: */
	    Tk_Draw3DRectangle(tkwin, d, border,
		b.x, b.y, b.width, b.height, borderWidth, relief);
	    break;
    }
}

/* Alternate shadow colors for entry fields:
 * NOTE: FLAT color is normally white, and the LITE color is a darker shade.
 */
static void DrawFieldBorder(
    Tk_Window tkwin, Drawable d, Tk_3DBorder border, XColor *borderColor,
    Ttk_Box b)
{
    GC borderGC = Tk_GCForColor(borderColor, d);
    DrawCorner(tkwin, d, border, borderGC,
	b.x, b.y, b.width, b.height, 0, DARK);
    DrawCorner(tkwin, d, border, borderGC,
	b.x+1, b.y+1, b.width-2, b.height-2, 0, BRDR);
    DrawCorner(tkwin, d, border, borderGC,
	b.x+1, b.y+1, b.width-2, b.height-2, 1, LITE);
    DrawCorner(tkwin, d, border, borderGC,
	b.x, b.y, b.width, b.height, 1, FLAT);
    return;
}

/*
 * ArrowPoints --
 *	Compute points of arrow polygon.
 */
static void ArrowPoints(Ttk_Box b, ArrowDirection direction, XPoint points[4])
{
    int cx, cy, h;

    switch (direction) {
	case ARROW_UP:
	    h = (b.width - 1)/2;
	    cx = b.x + h;
	    cy = b.y;
	    if (b.height <= h) h = b.height - 1;
	    points[0].x = cx;		points[0].y = cy;
	    points[1].x = cx - h;	points[1].y = cy + h;
	    points[2].x = cx + h;	points[2].y = cy + h;
	    break;
	case ARROW_DOWN:
	    h = (b.width - 1)/2;
	    cx = b.x + h;
	    cy = b.y + b.height - 1;
	    if (b.height <= h) h = b.height - 1;
	    points[0].x = cx;		points[0].y = cy;
	    points[1].x = cx - h;	points[1].y = cy - h;
	    points[2].x = cx + h;	points[2].y = cy - h;
	    break;
	case ARROW_LEFT:
	    h = (b.height - 1)/2;
	    cx = b.x;
	    cy = b.y + h;
	    if (b.width <= h) h = b.width - 1;
	    points[0].x = cx;		points[0].y = cy;
	    points[1].x = cx + h;	points[1].y = cy - h;
	    points[2].x = cx + h;	points[2].y = cy + h;
	    break;
	case ARROW_RIGHT:
	    h = (b.height - 1)/2;
	    cx = b.x + b.width - 1;
	    cy = b.y + h;
	    if (b.width <= h) h = b.width - 1;
	    points[0].x = cx;		points[0].y = cy;
	    points[1].x = cx - h;	points[1].y = cy - h;
	    points[2].x = cx - h;	points[2].y = cy + h;
	    break;
    }

    points[3].x = points[0].x;
    points[3].y = points[0].y;
}

/*public*/
void TtkArrowSize(int h, ArrowDirection direction, int *widthPtr, int *heightPtr)
{
    switch (direction) {
	case ARROW_UP:
	case ARROW_DOWN:	*widthPtr = 2*h+1; *heightPtr = h+1; break;
	case ARROW_LEFT:
	case ARROW_RIGHT:	*widthPtr = h+1; *heightPtr = 2*h+1;
    }
}

/*
 * TtkDrawArrow, TtkFillArrow --
 *	Draw an arrow in the indicated direction inside the specified box.
 */
/*public*/
void TtkFillArrow(
    Display *display, Drawable d, GC gc, Ttk_Box b, ArrowDirection direction)
{
    XPoint points[4];
    ArrowPoints(b, direction, points);
    XFillPolygon(display, d, gc, points, 3, Convex, CoordModeOrigin);
    XDrawLines(display, d, gc, points, 4, CoordModeOrigin);

    /* Work around bug [77527326e5] - ttk artifacts on Ubuntu */
    XDrawPoint(display, d, gc, points[2].x, points[2].y);
}

/*public*/
void TtkDrawArrow(
    Display *display, Drawable d, GC gc, Ttk_Box b, ArrowDirection direction)
{
    XPoint points[4];
    ArrowPoints(b, direction, points);
    XDrawLines(display, d, gc, points, 4, CoordModeOrigin);

    /* Work around bug [77527326e5] - ttk artifacts on Ubuntu */
    XDrawPoint(display, d, gc, points[2].x, points[2].y);
}

/*
 *----------------------------------------------------------------------
 * +++ Border element implementation.
 *
 * This border consists of (from outside-in):
 *
 * + a 1-pixel thick default indicator (defaultable widgets only)
 * + 1- or 2- pixel shaded border (controlled by -background and -relief)
 * + 1 pixel padding (???)
 */

typedef struct {
    Tcl_Obj	*borderObj;
    Tcl_Obj	*borderColorObj;	/* Extra border color */
    Tcl_Obj	*borderWidthObj;
    Tcl_Obj	*reliefObj;
    Tcl_Obj	*defaultStateObj;	/* for buttons */
} BorderElement;

static const Ttk_ElementOptionSpec BorderElementOptions[] = {
    { "-background", TK_OPTION_BORDER, offsetof(BorderElement,borderObj),
	DEFAULT_BACKGROUND },
    { "-bordercolor",TK_OPTION_COLOR,
	offsetof(BorderElement,borderColorObj), "black" },
    { "-default", TK_OPTION_ANY, offsetof(BorderElement,defaultStateObj),
	"disabled" },
    { "-borderwidth",TK_OPTION_PIXELS, offsetof(BorderElement,borderWidthObj),
	STRINGIFY(BORDERWIDTH) },
    { "-relief", TK_OPTION_RELIEF, offsetof(BorderElement,reliefObj),
	"flat" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void BorderElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    TCL_UNUSED(int *), /* widthPtr */
    TCL_UNUSED(int *), /* heightPtr */
    Ttk_Padding *paddingPtr)
{
    BorderElement *bd = (BorderElement *)elementRecord;
    int borderWidth = 0;
    Ttk_ButtonDefaultState defaultState = TTK_BUTTON_DEFAULT_DISABLED;

    Tk_GetPixelsFromObj(NULL, tkwin, bd->borderWidthObj, &borderWidth);
    Ttk_GetButtonDefaultStateFromObj(NULL, bd->defaultStateObj, &defaultState);

    if (defaultState != TTK_BUTTON_DEFAULT_DISABLED) {
	++borderWidth;
    }

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
    BorderElement *bd = (BorderElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, bd->borderObj);
    XColor *borderColor = Tk_GetColorFromObj(tkwin, bd->borderColorObj);
    int borderWidth = 2;
    int relief = TK_RELIEF_FLAT;
    Ttk_ButtonDefaultState defaultState = TTK_BUTTON_DEFAULT_DISABLED;

    /*
     * Get option values.
     */
    Tk_GetPixelsFromObj(NULL, tkwin, bd->borderWidthObj, &borderWidth);
    Tk_GetReliefFromObj(NULL, bd->reliefObj, &relief);
    Ttk_GetButtonDefaultStateFromObj(NULL, bd->defaultStateObj, &defaultState);

    if (defaultState == TTK_BUTTON_DEFAULT_ACTIVE) {
	GC gc = Tk_GCForColor(borderColor, d);
	XDrawRectangle(Tk_Display(tkwin), d, gc,
		b.x, b.y, b.width-1, b.height-1);
    }
    if (defaultState != TTK_BUTTON_DEFAULT_DISABLED) {
	/* Space for default ring: */
	b = Ttk_PadBox(b, Ttk_UniformPadding(1));
    }

    DrawBorder(tkwin, d, border, borderColor, b, borderWidth, relief);
}

static const Ttk_ElementSpec BorderElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(BorderElement),
    BorderElementOptions,
    BorderElementSize,
    BorderElementDraw
};

/*----------------------------------------------------------------------
 * +++ Field element:
 *	Used for editable fields.
 */
typedef struct {
    Tcl_Obj	*borderObj;
    Tcl_Obj	*borderColorObj;	/* Extra border color */
    Tcl_Obj	*focusWidthObj;
    Tcl_Obj	*focusColorObj;
} FieldElement;

static const Ttk_ElementOptionSpec FieldElementOptions[] = {
    { "-fieldbackground", TK_OPTION_BORDER, offsetof(FieldElement,borderObj),
	"white" },
    { "-bordercolor",TK_OPTION_COLOR, offsetof(FieldElement,borderColorObj),
	"black" },
    { "-focuswidth", TK_OPTION_PIXELS, offsetof(FieldElement,focusWidthObj),
	"2" },
    { "-focuscolor", TK_OPTION_COLOR, offsetof(FieldElement,focusColorObj),
	"#4a6984" },
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
    void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, Ttk_State state)
{
    FieldElement *field = (FieldElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, field->borderObj);
    XColor *borderColor = Tk_GetColorFromObj(tkwin, field->borderColorObj);
    int focusWidth = 2;

    Tk_GetPixelsFromObj(NULL, tkwin, field->focusWidthObj, &focusWidth);

    if (focusWidth > 0 && (state & TTK_STATE_FOCUS)) {
	Display *disp = Tk_Display(tkwin);
	XColor *focusColor = Tk_GetColorFromObj(tkwin, field->focusColorObj);
	GC focusGC = Tk_GCForColor(focusColor, d);

	if (focusWidth > 1) {
	    int x1 = b.x, x2 = b.x + b.width - 1;
	    int y1 = b.y, y2 = b.y + b.height - 1;
	    int w = WIN32_XDRAWLINE_HACK;

	    /*
	     * Draw the outer rounded rectangle
	     */
	    XDrawLine(disp, d, focusGC, x1+1, y1, x2-1+w, y1);	/* N */
	    XDrawLine(disp, d, focusGC, x1+1, y2, x2-1+w, y2);	/* S */
	    XDrawLine(disp, d, focusGC, x1, y1+1, x1, y2-1+w);	/* W */
	    XDrawLine(disp, d, focusGC, x2, y1+1, x2, y2-1+w);	/* E */

	    /*
	     * Draw the inner rectangle
	     */
	    b.x += 1; b.y += 1; b.width -= 2; b.height -= 2;
	    XDrawRectangle(disp, d, focusGC, b.x, b.y, b.width-1, b.height-1);

	    /*
	     * Fill the inner rectangle
	     */
	    GC bgGC = Tk_3DBorderGC(tkwin, border, TK_3D_FLAT_GC);
	    XFillRectangle(disp, d, bgGC, b.x+1, b.y+1, b.width-2, b.height-2);
	} else {
	    /*
	     * Draw the field element as usual
	     */
	    Tk_Fill3DRectangle(tkwin, d, border, b.x, b.y, b.width, b.height,
		0, TK_RELIEF_SUNKEN);
	    DrawFieldBorder(tkwin, d, border, borderColor, b);

	    /*
	     * Change the color of the border's outermost pixels
	     */
	    XDrawRectangle(disp, d, focusGC, b.x, b.y, b.width-1, b.height-1);
	}
    } else {
	Tk_Fill3DRectangle(tkwin, d, border, b.x, b.y, b.width, b.height,
	    0, TK_RELIEF_SUNKEN);
	DrawFieldBorder(tkwin, d, border, borderColor, b);
    }
}

static const Ttk_ElementSpec FieldElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(FieldElement),
    FieldElementOptions,
    FieldElementSize,
    FieldElementDraw
};

/*------------------------------------------------------------------------
 * Indicators --
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
     <path d='m0 0v15h1v-14h14v-1z' fill='#888888'/>\n\
     <path d='m1 1v13h1v-12h12v-1z' fill='#414141'/>\n\
     <path d='m14 1v13h-13v1h14v-14z' fill='#d9d9d9'/>\n\
     <path d='m15 0v15h-15v1h16v-16z' fill='#eeeeee'/>\n\
     <rect x='2' y='2' width='12' height='12' fill='#ffffff'/>\n\
    </svg>";

static const char checkbtnOnData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <path d='m0 0v15h1v-14h14v-1z' fill='#888888'/>\n\
     <path d='m1 1v13h1v-12h12v-1z' fill='#414141'/>\n\
     <path d='m14 1v13h-13v1h14v-14z' fill='#d9d9d9'/>\n\
     <path d='m15 0v15h-15v1h16v-16z' fill='#eeeeee'/>\n\
     <rect x='2' y='2' width='12' height='12' fill='#ffffff'/>\n\
     <path d='m4.5 8 3 3 4-6' fill='none' stroke='#000000' stroke-linecap='round' stroke-linejoin='round' stroke-width='2'/>\n\
    </svg>";

static const IndicatorSpec checkbutton_spec = {
    16, 16,
    checkbtnOffData,
    checkbtnOnData
};

static const char radiobtnOffData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <defs>\n\
      <linearGradient id='linearGradientOuter' x1='5' y1='5' x2='11' y2='11' gradientUnits='userSpaceOnUse'>\n\
       <stop stop-color='#888888' offset='0'/>\n\
       <stop stop-color='#eeeeee' offset='1'/>\n\
      </linearGradient>\n\
      <linearGradient id='linearGradientInner' x1='5' y1='5' x2='11' y2='11' gradientUnits='userSpaceOnUse'>\n\
       <stop stop-color='#414141' offset='0'/>\n\
       <stop stop-color='#d9d9d9' offset='1'/>\n\
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
       <stop stop-color='#888888' offset='0'/>\n\
       <stop stop-color='#eeeeee' offset='1'/>\n\
      </linearGradient>\n\
      <linearGradient id='linearGradientInner' x1='5' y1='5' x2='11' y2='11' gradientUnits='userSpaceOnUse'>\n\
       <stop stop-color='#414141' offset='0'/>\n\
       <stop stop-color='#d9d9d9' offset='1'/>\n\
      </linearGradient>\n\
     </defs>\n\
     <circle cx='8' cy='8' r='8' fill='url(#linearGradientOuter)'/>\n\
     <circle cx='8' cy='8' r='7' fill='url(#linearGradientInner)'/>\n\
     <circle cx='8' cy='8' r='6' fill='#ffffff'/>\n\
     <circle cx='8' cy='8' r='3' fill='#000000'/>\n\
    </svg>";

static const IndicatorSpec radiobutton_spec = {
    16, 16,
    radiobtnOffData,
    radiobtnOnData
};

typedef struct {
    Tcl_Obj *backgroundObj;
    Tcl_Obj *foregroundObj;
    Tcl_Obj *colorObj;
    Tcl_Obj *lightColorObj;
    Tcl_Obj *shadeColorObj;
    Tcl_Obj *borderColorObj;
    Tcl_Obj *marginObj;
} IndicatorElement;

static const Ttk_ElementOptionSpec IndicatorElementOptions[] = {
    { "-background", TK_OPTION_COLOR,
	    offsetof(IndicatorElement,backgroundObj), DEFAULT_BACKGROUND },
    { "-foreground", TK_OPTION_COLOR,
	    offsetof(IndicatorElement,foregroundObj), DEFAULT_FOREGROUND },
    { "-indicatorcolor", TK_OPTION_COLOR,
	    offsetof(IndicatorElement,colorObj), "#FFFFFF" },
    { "-lightcolor", TK_OPTION_COLOR,
	    offsetof(IndicatorElement,lightColorObj), "#DDDDDD" },
    { "-shadecolor", TK_OPTION_COLOR,
	    offsetof(IndicatorElement,shadeColorObj), "#888888" },
    { "-bordercolor", TK_OPTION_COLOR,
	    offsetof(IndicatorElement,borderColorObj), "black" },
    { "-indicatormargin", TK_OPTION_STRING,
	    offsetof(IndicatorElement,marginObj), "0 2 4 2" },
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

    char bgColorStr[7], fgColorStr[7], indicatorColorStr[7],
	 shadeColorStr[7], borderColorStr[7];
    unsigned int selected = (state & TTK_STATE_SELECTED);
    Tcl_Interp *interp = Tk_Interp(tkwin);
    char imgName[70];
    Tk_Image img;

    const char *svgDataPtr;
    size_t svgDataLen;
    char *svgDataCopy;
    char *shadeColorPtr, *highlightColorPtr, *borderColorPtr, *bgColorPtr,
	 *indicatorColorPtr, *fgColorPtr;
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
     * Construct the color strings bgColorStr, fgColorStr,
     * indicatorColorStr, shadeColorStr, and borderColorStr
     */
    ColorToStr(Tk_GetColorFromObj(tkwin, indicator->backgroundObj),
	       bgColorStr);
    ColorToStr(Tk_GetColorFromObj(tkwin, indicator->foregroundObj),
	       fgColorStr);
    ColorToStr(Tk_GetColorFromObj(tkwin, indicator->colorObj),
	       indicatorColorStr);
    ColorToStr(Tk_GetColorFromObj(tkwin, indicator->shadeColorObj),
	       shadeColorStr);
    ColorToStr(Tk_GetColorFromObj(tkwin, indicator->borderColorObj),
	       borderColorStr);

    /*
     * Check whether there is an SVG image of this size for the indicator's
     * type (0 = checkbtn, 1 = radiobtn) and these color strings
     */
    snprintf(imgName, sizeof(imgName),
	     "::tk::icons::indicator_alt%d_%d_%s_%s_%s_%s_%s",
	     width, spec->offDataPtr == radiobtnOffData,
	     shadeColorStr, indicatorColorStr, borderColorStr, bgColorStr,
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

	shadeColorPtr =	    strstr(svgDataCopy, "888888");
	highlightColorPtr = strstr(svgDataCopy, "eeeeee");
	borderColorPtr =    strstr(svgDataCopy, "414141");
	bgColorPtr =	    strstr(svgDataCopy, "d9d9d9");
	indicatorColorPtr = strstr(svgDataCopy, "ffffff");
	fgColorPtr =	    strstr(svgDataCopy, "000000");

	assert(shadeColorPtr);
	assert(highlightColorPtr);
	assert(borderColorPtr);
	assert(bgColorPtr);
	assert(indicatorColorPtr);

	memcpy(shadeColorPtr, shadeColorStr, 6);
	memcpy(highlightColorPtr, indicatorColorStr, 6);
	memcpy(borderColorPtr, borderColorStr, 6);
	memcpy(bgColorPtr, bgColorStr, 6);
	memcpy(indicatorColorPtr, indicatorColorStr, 6);
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

/*----------------------------------------------------------------------
 * +++ Arrow element(s).
 *
 *	Draws a solid triangle, inside a box.
 *	clientData is an enum ArrowDirection pointer.
 */

typedef struct {
    Tcl_Obj *sizeObj;
    Tcl_Obj *colorObj;		/* Arrow color */
    Tcl_Obj *borderObj;
    Tcl_Obj *borderColorObj;	/* Extra color for borders */
    Tcl_Obj *reliefObj;
} ArrowElement;

static const Ttk_ElementOptionSpec ArrowElementOptions[] = {
    { "-arrowsize", TK_OPTION_PIXELS,
	offsetof(ArrowElement,sizeObj), STRINGIFY(SCROLLBAR_WIDTH) },
    { "-arrowcolor", TK_OPTION_COLOR,
	offsetof(ArrowElement,colorObj), "black"},
    { "-background", TK_OPTION_BORDER,
	offsetof(ArrowElement,borderObj), DEFAULT_BACKGROUND },
    { "-bordercolor", TK_OPTION_COLOR,
	offsetof(ArrowElement,borderColorObj), "black" },
    { "-relief", TK_OPTION_RELIEF,
	offsetof(ArrowElement,reliefObj), "raised"},
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

/*
 * Note asymmetric padding:
 * top/left padding is 1 less than bottom/right,
 * since in this theme 2-pixel borders are asymmetric.
 */
static const Ttk_Padding ArrowPadding = { 3,3,4,4 };

static void ArrowElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    ArrowElement *arrow = (ArrowElement *)elementRecord;
    ArrowDirection direction = (ArrowDirection)PTR2INT(clientData);
    double scalingLevel = TkScalingLevel(tkwin);
    Ttk_Padding padding;
    int size = SCROLLBAR_WIDTH;

    padding.left = round(ArrowPadding.left * scalingLevel);
    padding.right = padding.left + 1;
    padding.top = round(ArrowPadding.top * scalingLevel);
    padding.bottom = padding.top + 1;

    Tk_GetPixelsFromObj(NULL, tkwin, arrow->sizeObj, &size);
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
    ArrowElement *arrow = (ArrowElement *)elementRecord;
    ArrowDirection direction = (ArrowDirection)PTR2INT(clientData);
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, arrow->borderObj);
    XColor *borderColor = Tk_GetColorFromObj(tkwin, arrow->borderColorObj);
    int borderWidth = 2, relief = TK_RELIEF_RAISED;
    Ttk_Padding padding;
    double scalingLevel = TkScalingLevel(tkwin);
    int cx = 0, cy = 0;
    XColor *arrowColor = Tk_GetColorFromObj(tkwin, arrow->colorObj);
    GC gc = Tk_GCForColor(arrowColor, d);

    Tk_GetReliefFromObj(NULL, arrow->reliefObj, &relief);

    Tk_Fill3DRectangle(tkwin, d, border, b.x, b.y, b.width, b.height,
	    0, TK_RELIEF_FLAT);
    DrawBorder(tkwin, d, border, borderColor, b, borderWidth, relief);

    padding.left = round(ArrowPadding.left * scalingLevel);
    padding.right = padding.left + 1;
    padding.top = round(ArrowPadding.top * scalingLevel);
    padding.bottom = padding.top + 1;

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
    sizeof(ArrowElement),
    ArrowElementOptions,
    ArrowElementSize,
    ArrowElementDraw
};

/*
 * Modified arrow element for comboboxes and spinboxes:
 *	The width and height are different, and the left edge is drawn in the
 *	same color as the inner part of the right one.
 */

static void BoxArrowElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    ArrowElement *arrow = (ArrowElement *)elementRecord;
    ArrowDirection direction = (ArrowDirection)PTR2INT(clientData);
    double scalingLevel = TkScalingLevel(tkwin);
    Ttk_Padding padding;
    int size = 14;

    padding.left = round(ArrowPadding.left * scalingLevel);
    padding.top = round(ArrowPadding.top * scalingLevel);
    padding.right = round(ArrowPadding.right * scalingLevel);
    padding.bottom = round(ArrowPadding.bottom * scalingLevel);

    Tk_GetPixelsFromObj(NULL, tkwin, arrow->sizeObj, &size);
    size -= Ttk_PaddingWidth(padding);
    TtkArrowSize(size/2, direction, widthPtr, heightPtr);
    *widthPtr += Ttk_PaddingWidth(padding);
    *heightPtr += Ttk_PaddingHeight(padding);
}

static void BoxArrowElementDraw(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    ArrowElement *arrow = (ArrowElement *)elementRecord;
    ArrowDirection direction = (ArrowDirection)PTR2INT(clientData);
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, arrow->borderObj);
    XColor *borderColor = Tk_GetColorFromObj(tkwin, arrow->borderColorObj);
    int borderWidth = 2, relief = TK_RELIEF_RAISED;
    Display *disp = Tk_Display(tkwin);
    GC darkGC = Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC);
    int w = WIN32_XDRAWLINE_HACK;
    Ttk_Padding padding;
    double scalingLevel = TkScalingLevel(tkwin);
    int cx = 0, cy = 0;
    XColor *arrowColor = Tk_GetColorFromObj(tkwin, arrow->colorObj);
    GC arrowGC = Tk_GCForColor(arrowColor, d);

    Tk_Fill3DRectangle(tkwin, d, border, b.x, b.y, b.width, b.height,
	    0, TK_RELIEF_FLAT);
    DrawBorder(tkwin, d, border, borderColor, b, borderWidth, relief);

    XDrawLine(disp, d, darkGC, b.x, b.y+1, b.x, b.y+b.height-2+w);

    padding.left = round(ArrowPadding.left * scalingLevel);
    padding.top = round(ArrowPadding.top * scalingLevel);
    padding.right = round(ArrowPadding.right * scalingLevel);
    padding.bottom = round(ArrowPadding.bottom * scalingLevel);

    b = Ttk_PadBox(b, padding);

    TtkArrowSize(b.width/2, direction, &cx, &cy);
    if ((b.height - cy) % 2 == 1) {
	++cy;
    }

    b = Ttk_AnchorBox(b, cx, cy, TK_ANCHOR_CENTER);

    TtkFillArrow(disp, d, arrowGC, b, direction);
}

static const Ttk_ElementSpec BoxArrowElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(ArrowElement),
    ArrowElementOptions,
    BoxArrowElementSize,
    BoxArrowElementDraw
};

/*----------------------------------------------------------------------
 * +++ Menubutton indicator:
 *	Draw an arrow in the direction where the menu will be posted.
 */

#define MENUBUTTON_ARROW_SIZE 5

typedef struct {
    Tcl_Obj *directionObj;
    Tcl_Obj *sizeObj;
    Tcl_Obj *colorObj;
} MenubuttonArrowElement;

static const char *const directionStrings[] = {	/* See also: button.c */
    "above", "below", "flush", "left", "right", NULL
};
enum { POST_ABOVE, POST_BELOW, POST_FLUSH, POST_LEFT, POST_RIGHT };

static const Ttk_ElementOptionSpec MenubuttonArrowElementOptions[] = {
    { "-direction", TK_OPTION_STRING,
	offsetof(MenubuttonArrowElement,directionObj), "below" },
    { "-arrowsize", TK_OPTION_PIXELS,
	offsetof(MenubuttonArrowElement,sizeObj), STRINGIFY(MENUBUTTON_ARROW_SIZE)},
    { "-arrowcolor", TK_OPTION_COLOR,
	offsetof(MenubuttonArrowElement,colorObj), "black"},
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static const Ttk_Padding MenubuttonArrowPadding = { 3, 0, 3, 0 };

static void MenubuttonArrowElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    MenubuttonArrowElement *arrow = (MenubuttonArrowElement *)elementRecord;
    int size = MENUBUTTON_ARROW_SIZE;
    Ttk_Padding padding;
    double scalingLevel = TkScalingLevel(tkwin);

    Tk_GetPixelsFromObj(NULL, tkwin, arrow->sizeObj, &size);

    padding.left = round(MenubuttonArrowPadding.left * scalingLevel);
    padding.top = round(MenubuttonArrowPadding.top * scalingLevel);
    padding.right = round(MenubuttonArrowPadding.right * scalingLevel);
    padding.bottom = round(MenubuttonArrowPadding.bottom * scalingLevel);

    *widthPtr = *heightPtr = 2 * size + 1;
    *widthPtr += Ttk_PaddingWidth(padding);
    *heightPtr += Ttk_PaddingHeight(padding);
}

static void MenubuttonArrowElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    MenubuttonArrowElement *arrow = (MenubuttonArrowElement *)elementRecord;
    XColor *arrowColor = Tk_GetColorFromObj(tkwin, arrow->colorObj);
    GC gc = Tk_GCForColor(arrowColor, d);
    int size = MENUBUTTON_ARROW_SIZE;
    int postDirection = POST_BELOW;
    ArrowDirection arrowDirection = ARROW_DOWN;
    int width = 0, height = 0;
    Ttk_Padding padding;
    double scalingLevel = TkScalingLevel(tkwin);

    Tk_GetPixelsFromObj(NULL, tkwin, arrow->sizeObj, &size);
    Tcl_GetIndexFromObjStruct(NULL, arrow->directionObj, directionStrings,
	   sizeof(char *), ""/*message*/, 0/*flags*/, &postDirection);

    /* ... this might not be such a great idea ... */
    switch (postDirection) {
	case POST_ABOVE:	arrowDirection = ARROW_UP; break;
	case POST_BELOW:	arrowDirection = ARROW_DOWN; break;
	case POST_LEFT:		arrowDirection = ARROW_LEFT; break;
	case POST_RIGHT:	arrowDirection = ARROW_RIGHT; break;
	case POST_FLUSH:	arrowDirection = ARROW_DOWN; break;
    }

    TtkArrowSize(size, arrowDirection, &width, &height);

    padding.left = round(MenubuttonArrowPadding.left * scalingLevel);
    padding.top = round(MenubuttonArrowPadding.top * scalingLevel);
    padding.right = round(MenubuttonArrowPadding.right * scalingLevel);
    padding.bottom = round(MenubuttonArrowPadding.bottom * scalingLevel);

    b = Ttk_PadBox(b, padding);
    b = Ttk_AnchorBox(b, width, height, TK_ANCHOR_CENTER);
    TtkFillArrow(Tk_Display(tkwin), d, gc, b, arrowDirection);
}

static const Ttk_ElementSpec MenubuttonArrowElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(MenubuttonArrowElement),
    MenubuttonArrowElementOptions,
    MenubuttonArrowElementSize,
    MenubuttonArrowElementDraw
};

/*
 *----------------------------------------------------------------------
 * +++ Thumb element.
 */

typedef struct {
    Tcl_Obj *sizeObj;
    Tcl_Obj *firstObj;
    Tcl_Obj *lastObj;
    Tcl_Obj *borderObj;
    Tcl_Obj *borderColorObj;
    Tcl_Obj *reliefObj;
    Tcl_Obj *orientObj;
} ThumbElement;

static const Ttk_ElementOptionSpec ThumbElementOptions[] = {
    { "-width", TK_OPTION_PIXELS, offsetof(ThumbElement,sizeObj),
	STRINGIFY(SCROLLBAR_WIDTH) },
    { "-background", TK_OPTION_BORDER, offsetof(ThumbElement,borderObj),
	DEFAULT_BACKGROUND },
    { "-bordercolor", TK_OPTION_COLOR, offsetof(ThumbElement,borderColorObj),
	"black" },
    { "-relief", TK_OPTION_RELIEF, offsetof(ThumbElement,reliefObj),"raised" },
    { "-orient", TK_OPTION_ANY, offsetof(ThumbElement,orientObj),"horizontal"},
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void ThumbElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    ThumbElement *thumb = (ThumbElement *)elementRecord;
    Ttk_Orient orient;
    int size;

    Tk_GetPixelsFromObj(NULL, tkwin, thumb->sizeObj, &size);
    Ttk_GetOrientFromObj(NULL, thumb->orientObj, &orient);

    if (orient == TTK_ORIENT_VERTICAL) {
	*widthPtr = size;
	*heightPtr = MIN_THUMB_SIZE;
    } else {
	*widthPtr = MIN_THUMB_SIZE;
	*heightPtr = size;
    }
}

static void ThumbElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    ThumbElement *thumb = (ThumbElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, thumb->borderObj);
    XColor *borderColor = Tk_GetColorFromObj(tkwin, thumb->borderColorObj);
    int relief = TK_RELIEF_RAISED;
    int borderWidth = 2;

    /*
     * Don't draw the thumb if we are disabled.
     * This makes it behave like Windows ... if that's what we want.
    if (state & TTK_STATE_DISABLED)
	return;
     */

    Tk_GetReliefFromObj(NULL, thumb->reliefObj, &relief);

    Tk_Fill3DRectangle(
	tkwin, d, border, b.x,b.y,b.width,b.height, 0, TK_RELIEF_FLAT);
    DrawBorder(tkwin, d, border, borderColor, b, borderWidth, relief);
}

static const Ttk_ElementSpec ThumbElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(ThumbElement),
    ThumbElementOptions,
    ThumbElementSize,
    ThumbElementDraw
};

/*
 *----------------------------------------------------------------------
 * +++ Slider element.
 *
 * This is the moving part of the scale widget.
 *
 * The slider element is the thumb in the scale widget. This is drawn
 * as an arrow-type element that can point up, down, left or right.
 *
 */

typedef struct {
    Tcl_Obj *thicknessObj;	/* Short axis dimension */
    Tcl_Obj *reliefObj;		/* Relief for this object */
    Tcl_Obj *borderObj;		/* Border / background color */
    Tcl_Obj *borderColorObj;	/* Additional border color */
    Tcl_Obj *borderWidthObj;
    Tcl_Obj *orientObj;		/* Orientation of overall slider */
} SliderElement;

static const Ttk_ElementOptionSpec SliderElementOptions[] = {
    { "-sliderthickness",TK_OPTION_PIXELS, offsetof(SliderElement,thicknessObj),
	"15" },
    { "-sliderrelief", TK_OPTION_RELIEF, offsetof(SliderElement,reliefObj),
	"raised" },
    { "-background", TK_OPTION_BORDER, offsetof(SliderElement,borderObj),
	DEFAULT_BACKGROUND },
    { "-bordercolor", TK_OPTION_COLOR, offsetof(SliderElement,borderColorObj),
	"black" },
    { "-borderwidth", TK_OPTION_PIXELS, offsetof(SliderElement,borderWidthObj),
	STRINGIFY(BORDERWIDTH) },
    { "-orient", TK_OPTION_ANY, offsetof(SliderElement,orientObj),
	"horizontal" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void SliderElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    SliderElement *slider = (SliderElement *)elementRecord;
    Ttk_Orient orient;
    int thickness, borderWidth;

    Ttk_GetOrientFromObj(NULL, slider->orientObj, &orient);
    Tk_GetPixelsFromObj(NULL, tkwin, slider->thicknessObj, &thickness);
    Tk_GetPixelsFromObj(NULL, tkwin, slider->borderWidthObj, &borderWidth);

    switch (orient) {
	case TTK_ORIENT_VERTICAL:
	    *widthPtr = thickness + (borderWidth *2);
	    *heightPtr = *widthPtr/2;
	    break;
	case TTK_ORIENT_HORIZONTAL:
	    *heightPtr = thickness + (borderWidth *2);
	    *widthPtr = *heightPtr/2;
	    break;
    }
}

static void SliderElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    SliderElement *slider = (SliderElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, slider->borderObj);
    XColor *borderColor = Tk_GetColorFromObj(tkwin, slider->borderColorObj);
    int relief = TK_RELIEF_RAISED, borderWidth = 2;

    Tk_GetPixelsFromObj(NULL, tkwin, slider->borderWidthObj, &borderWidth);
    Tk_GetReliefFromObj(NULL, slider->reliefObj, &relief);

    Tk_Fill3DRectangle(tkwin, d, border,
	b.x, b.y, b.width, b.height,
	borderWidth, TK_RELIEF_FLAT);
    DrawBorder(tkwin, d, border, borderColor, b, borderWidth, relief);
}

static const Ttk_ElementSpec SliderElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(SliderElement),
    SliderElementOptions,
    SliderElementSize,
    SliderElementDraw
};

/*------------------------------------------------------------------------
 * +++ Tree indicator element.
 */

typedef struct {
    Tcl_Obj *colorObj;
    Tcl_Obj *marginObj;
    Tcl_Obj *sizeObj;
} TreeitemIndicator;

static const Ttk_ElementOptionSpec TreeitemIndicatorOptions[] = {
    { "-foreground", TK_OPTION_COLOR,
	offsetof(TreeitemIndicator,colorObj), DEFAULT_FOREGROUND },
    { "-size", TK_OPTION_PIXELS,
	offsetof(TreeitemIndicator,sizeObj), "6.75p" },
    { "-indicatormargins", TK_OPTION_STRING,
	offsetof(TreeitemIndicator,marginObj), "2 2 4 2" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void TreeitemIndicatorSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    TreeitemIndicator *indicator = (TreeitemIndicator *)elementRecord;
    int size = 0;
    Ttk_Padding margins;

    Tk_GetPixelsFromObj(NULL, tkwin, indicator->sizeObj, &size);
    if (size % 2 == 0) --size;  /* An odd size is better for the indicator. */
    Ttk_GetPaddingFromObj(NULL, tkwin, indicator->marginObj, &margins);
    *widthPtr = size + Ttk_PaddingWidth(margins);
    *heightPtr = size + Ttk_PaddingHeight(margins);
}

static void TreeitemIndicatorDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, Ttk_State state)
{
    TreeitemIndicator *indicator = (TreeitemIndicator *)elementRecord;
    XColor *color = Tk_GetColorFromObj(tkwin, indicator->colorObj);
    GC gc = Tk_GCForColor(color, d);
    Ttk_Padding padding = Ttk_UniformPadding(0);
    int w = WIN32_XDRAWLINE_HACK;
    int cx, cy;

    if (state & TTK_STATE_LEAF) {
	/* don't draw anything ... */
	return;
    }

    Ttk_GetPaddingFromObj(NULL, tkwin, indicator->marginObj, &padding);
    b = Ttk_PadBox(b, padding);

    XDrawRectangle(Tk_Display(tkwin), d, gc,
	    b.x, b.y, b.width - 1, b.height - 1);

    cx = b.x + (b.width - 1) / 2;
    cy = b.y + (b.height - 1) / 2;
    XDrawLine(Tk_Display(tkwin), d, gc, b.x+2, cy, b.x+b.width-3+w, cy);

    if (!(state & TTK_STATE_OPEN)) {
	/* turn '-' into a '+' */
	XDrawLine(Tk_Display(tkwin), d, gc, cx, b.y+2, cx, b.y+b.height-3+w);
    }
}

static const Ttk_ElementSpec TreeitemIndicatorElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(TreeitemIndicator),
    TreeitemIndicatorOptions,
    TreeitemIndicatorSize,
    TreeitemIndicatorDraw
};

/*------------------------------------------------------------------------
 * TtkAltTheme_Init --
 *	Install alternate theme.
 */

MODULE_SCOPE int
TtkAltTheme_Init(Tcl_Interp *interp)
{
    Ttk_Theme theme =  Ttk_CreateTheme(interp, "alt", NULL);

    if (!theme) {
	return TCL_ERROR;
    }

    Ttk_RegisterElement(interp, theme, "border", &BorderElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "Checkbutton.indicator",
	    &IndicatorElementSpec, (void *)&checkbutton_spec);
    Ttk_RegisterElement(interp, theme, "Radiobutton.indicator",
	    &IndicatorElementSpec, (void *)&radiobutton_spec);
    Ttk_RegisterElement(interp, theme, "Menubutton.indicator",
	    &MenubuttonArrowElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "field", &FieldElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "thumb", &ThumbElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "slider", &SliderElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "uparrow",
	    &ArrowElementSpec, INT2PTR(ARROW_UP));
    Ttk_RegisterElement(interp, theme, "Spinbox.uparrow",
	    &BoxArrowElementSpec, INT2PTR(ARROW_UP));
    Ttk_RegisterElement(interp, theme, "downarrow",
	    &ArrowElementSpec, INT2PTR(ARROW_DOWN));
    Ttk_RegisterElement(interp, theme, "Spinbox.downarrow",
	    &BoxArrowElementSpec, INT2PTR(ARROW_DOWN));
    Ttk_RegisterElement(interp, theme, "Combobox.downarrow",
	    &BoxArrowElementSpec, INT2PTR(ARROW_DOWN));
    Ttk_RegisterElement(interp, theme, "leftarrow",
	    &ArrowElementSpec, INT2PTR(ARROW_LEFT));
    Ttk_RegisterElement(interp, theme, "rightarrow",
	    &ArrowElementSpec, INT2PTR(ARROW_RIGHT));
    Ttk_RegisterElement(interp, theme, "arrow",
	    &ArrowElementSpec, INT2PTR(ARROW_UP));

    Ttk_RegisterElement(interp, theme, "Treeitem.indicator",
	    &TreeitemIndicatorElementSpec, NULL);

    Tcl_PkgProvide(interp, "ttk::theme::alt", TTK_VERSION);

    return TCL_OK;
}

/*EOF*/
