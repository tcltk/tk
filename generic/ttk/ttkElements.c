/*
 * Copyright © 2003 Joe English
 *
 * Default implementation for themed elements.
 *
 */

#include "tkInt.h"
#include "ttkThemeInt.h"
#include "ttkWidget.h"

#if defined(_WIN32)
  #define WIN32_XDRAWLINE_HACK 1
#else
  #define WIN32_XDRAWLINE_HACK 0
#endif

#define DEFAULT_BORDERWIDTH "2"
#define DEFAULT_ARROW_SIZE "15"
#define MIN_THUMB_SIZE 10

/*----------------------------------------------------------------------
 * +++ Null element.  Does nothing; used as a stub.
 * Null element methods, option table and element spec are public,
 * and may be used in other engines.
 */

/* public */ const Ttk_ElementOptionSpec TtkNullElementOptions[] = { { NULL, TK_OPTION_BOOLEAN, 0, NULL } };

/* public */ void
TtkNullElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    (void)dummy;
    (void)elementRecord;
    (void)tkwin;
    (void)widthPtr;
    (void)heightPtr;
    (void)paddingPtr;
}

/* public */ void
TtkNullElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    (void)dummy;
    (void)elementRecord;
    (void)tkwin;
    (void)d;
    (void)b;
    (void)state;
}

/* public */ Ttk_ElementSpec ttkNullElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    TtkNullElementSize,
    TtkNullElementDraw
};

/*----------------------------------------------------------------------
 * +++ Background and fill elements.
 *
 * The fill element fills its parcel with the background color.
 * The background element ignores the parcel, and fills the entire window.
 *
 * Ttk_GetLayout() automatically includes a background element.
 */

typedef struct {
    Tcl_Obj	*backgroundObj;
} BackgroundElement;

static const Ttk_ElementOptionSpec BackgroundElementOptions[] = {
    { "-background", TK_OPTION_BORDER,
	    offsetof(BackgroundElement,backgroundObj), DEFAULT_BACKGROUND },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void FillElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    BackgroundElement *bg = (BackgroundElement *)elementRecord;
    Tk_3DBorder backgroundPtr = Tk_Get3DBorderFromObj(tkwin,bg->backgroundObj);
    (void)dummy;
    (void)state;

    XFillRectangle(Tk_Display(tkwin), d,
	Tk_3DBorderGC(tkwin, backgroundPtr, TK_3D_FLAT_GC),
	b.x, b.y, b.width, b.height);
}

static void BackgroundElementDraw(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    (void)b;

    FillElementDraw(
	clientData, elementRecord, tkwin,
	d, Ttk_WinBox(tkwin), state);
}

static const Ttk_ElementSpec FillElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(BackgroundElement),
    BackgroundElementOptions,
    TtkNullElementSize,
    FillElementDraw
};

static const Ttk_ElementSpec BackgroundElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(BackgroundElement),
    BackgroundElementOptions,
    TtkNullElementSize,
    BackgroundElementDraw
};

/*----------------------------------------------------------------------
 * +++ Border element.
 */

typedef struct {
    Tcl_Obj	*borderObj;
    Tcl_Obj	*borderWidthObj;
    Tcl_Obj	*reliefObj;
} BorderElement;

static const Ttk_ElementOptionSpec BorderElementOptions[] = {
    { "-background", TK_OPTION_BORDER,
	offsetof(BorderElement,borderObj), DEFAULT_BACKGROUND },
    { "-borderwidth", TK_OPTION_PIXELS,
	offsetof(BorderElement,borderWidthObj), DEFAULT_BORDERWIDTH },
    { "-relief", TK_OPTION_RELIEF,
	offsetof(BorderElement,reliefObj), "flat" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void BorderElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    BorderElement *bd = (BorderElement *)elementRecord;
    int borderWidth = 0;
    (void)dummy;
    (void)tkwin;
    (void)widthPtr;
    (void)heightPtr;

    Tcl_GetIntFromObj(NULL, bd->borderWidthObj, &borderWidth);
    *paddingPtr = Ttk_UniformPadding((short)borderWidth);
}

static void BorderElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    BorderElement *bd = (BorderElement *)elementRecord;
    Tk_3DBorder border = NULL;
    int borderWidth = 1, relief = TK_RELIEF_FLAT;
    (void)dummy;
    (void)state;

    border = Tk_Get3DBorderFromObj(tkwin, bd->borderObj);
    Tcl_GetIntFromObj(NULL, bd->borderWidthObj, &borderWidth);
    Tk_GetReliefFromObj(NULL, bd->reliefObj, &relief);

    if (border && borderWidth > 0 && relief != TK_RELIEF_FLAT) {
	Tk_Draw3DRectangle(tkwin, d, border,
	    b.x, b.y, b.width, b.height, borderWidth, relief);
    }
}

static const Ttk_ElementSpec BorderElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(BorderElement),
    BorderElementOptions,
    BorderElementSize,
    BorderElementDraw
};

/*----------------------------------------------------------------------
 * +++ Field element.
 * 	Used for editable fields.
 */
typedef struct {
    Tcl_Obj	*borderObj;
    Tcl_Obj	*borderWidthObj;
    Tcl_Obj	*focusWidthObj;
    Tcl_Obj	*focusColorObj;
} FieldElement;

static const Ttk_ElementOptionSpec FieldElementOptions[] = {
    { "-fieldbackground", TK_OPTION_BORDER,
	offsetof(FieldElement,borderObj), "white" },
    { "-borderwidth", TK_OPTION_PIXELS,
	offsetof(FieldElement,borderWidthObj), "2" },
    { "-focuswidth", TK_OPTION_PIXELS,
	offsetof(FieldElement,focusWidthObj), "2" },
    { "-focuscolor", TK_OPTION_COLOR,
	offsetof(FieldElement,focusColorObj), "#4a6984" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};


static void FieldElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    FieldElement *field = (FieldElement *)elementRecord;
    int borderWidth = 2, focusWidth = 2;
    (void)dummy;
    (void)widthPtr;
    (void)heightPtr;

    Tk_GetPixelsFromObj(NULL, tkwin, field->borderWidthObj, &borderWidth);
    Tk_GetPixelsFromObj(NULL, tkwin, field->focusWidthObj, &focusWidth);
    if (focusWidth > 0 && borderWidth < 2) {
	borderWidth += (focusWidth - borderWidth);
    }
    *paddingPtr = Ttk_UniformPadding((short)borderWidth);
}

static void FieldElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    FieldElement *field = (FieldElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, field->borderObj);
    int focusWidth = 2;
    (void)dummy;

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

	    b.x += 1; b.y += 1; b.width -= 2; b.height -= 2;
	}

	/*
	 * If focusWidth > 1 then draw the inner rectangle,
	 * else the only one replacing the (outer) border
	 */
	XDrawRectangle(disp, d, focusGC, b.x, b.y, b.width-1, b.height-1);

	GC bgGC = Tk_3DBorderGC(tkwin, border, TK_3D_FLAT_GC);
	XFillRectangle(disp, d, bgGC, b.x+1, b.y+1, b.width-2, b.height-2);
    } else {
	int borderWidth = 2;
	Tk_GetPixelsFromObj(NULL, tkwin, field->borderWidthObj, &borderWidth);

	Tk_Fill3DRectangle(tkwin, d, border, b.x, b.y, b.width, b.height,
		borderWidth, TK_RELIEF_SUNKEN);
    }
}

static const Ttk_ElementSpec FieldElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(FieldElement),
    FieldElementOptions,
    FieldElementSize,
    FieldElementDraw
};

/*
 *----------------------------------------------------------------------
 * +++ Padding element.
 *
 * This element has no visual representation, only geometry.
 * It adds a (possibly non-uniform) internal border.
 * In addition, if "-shiftrelief" is specified,
 * adds additional pixels to shift child elements "in" or "out"
 * depending on the -relief.
 */

typedef struct {
    Tcl_Obj	*paddingObj;
    Tcl_Obj	*reliefObj;
    Tcl_Obj	*shiftreliefObj;
} PaddingElement;

static const Ttk_ElementOptionSpec PaddingElementOptions[] = {
    { "-padding", TK_OPTION_STRING,
	offsetof(PaddingElement,paddingObj), "0" },
    { "-relief", TK_OPTION_RELIEF,
	offsetof(PaddingElement,reliefObj), "flat" },
    { "-shiftrelief", TK_OPTION_INT,
	offsetof(PaddingElement,shiftreliefObj), "0" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void PaddingElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    PaddingElement *padding = (PaddingElement *)elementRecord;
    int shiftRelief = 0;
    int relief = TK_RELIEF_FLAT;
    Ttk_Padding pad;
    (void)dummy;
    (void)widthPtr;
    (void)heightPtr;

    Tk_GetReliefFromObj(NULL, padding->reliefObj, &relief);
    Tcl_GetIntFromObj(NULL, padding->shiftreliefObj, &shiftRelief);
    Ttk_GetPaddingFromObj(NULL,tkwin,padding->paddingObj,&pad);
    *paddingPtr = Ttk_RelievePadding(pad, relief, shiftRelief);
}

static const Ttk_ElementSpec PaddingElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(PaddingElement),
    PaddingElementOptions,
    PaddingElementSize,
    TtkNullElementDraw
};

/*----------------------------------------------------------------------
 * +++ Focus ring element.
 * 	Draws a dashed focus ring, if the widget has keyboard focus.
 */
typedef struct {
    Tcl_Obj	*focusColorObj;
    Tcl_Obj	*focusThicknessObj;
} FocusElement;

/*
 * DrawFocusRing --
 * 	Draw a dotted rectangle to indicate focus.
 */
static void DrawFocusRing(
    Tk_Window tkwin, Drawable d, Tcl_Obj *colorObj, Ttk_Box b)
{
    XColor *color = Tk_GetColorFromObj(tkwin, colorObj);
    unsigned long mask = 0UL;
    XGCValues gcvalues;
    GC gc;

    gcvalues.foreground = color->pixel;
    gcvalues.line_style = LineOnOffDash;
    gcvalues.line_width = 1;
    gcvalues.dashes = 1;
    gcvalues.dash_offset = 1;
    mask = GCForeground | GCLineStyle | GCDashList | GCDashOffset | GCLineWidth;

    gc = Tk_GetGC(tkwin, mask, &gcvalues);
    XDrawRectangle(Tk_Display(tkwin), d, gc, b.x, b.y, b.width-1, b.height-1);
    Tk_FreeGC(Tk_Display(tkwin), gc);
}

static const Ttk_ElementOptionSpec FocusElementOptions[] = {
    { "-focuscolor",TK_OPTION_COLOR,
	offsetof(FocusElement,focusColorObj), "black" },
    { "-focusthickness",TK_OPTION_PIXELS,
	offsetof(FocusElement,focusThicknessObj), "1" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void FocusElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    FocusElement *focus = (FocusElement *)elementRecord;
    int focusThickness = 0;
    (void)dummy;
    (void)tkwin;
    (void)widthPtr;
    (void)heightPtr;

    Tcl_GetIntFromObj(NULL, focus->focusThicknessObj, &focusThickness);
    *paddingPtr = Ttk_UniformPadding((short)focusThickness);
}

static void FocusElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    FocusElement *focus = (FocusElement *)elementRecord;
    int focusThickness = 0;
    (void)dummy;

    if (state & TTK_STATE_FOCUS) {
	Tcl_GetIntFromObj(NULL,focus->focusThicknessObj,&focusThickness);
	DrawFocusRing(tkwin, d, focus->focusColorObj, b);
    }
}

static const Ttk_ElementSpec FocusElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(FocusElement),
    FocusElementOptions,
    FocusElementSize,
    FocusElementDraw
};

/*----------------------------------------------------------------------
 * +++ Separator element.
 * 	Just draws a horizontal or vertical bar.
 * 	Three elements are defined: horizontal, vertical, and general;
 *	the general separator checks the "-orient" option.
 */

typedef struct {
    Tcl_Obj	*orientObj;
    Tcl_Obj	*borderObj;
} SeparatorElement;

static const Ttk_ElementOptionSpec SeparatorElementOptions[] = {
    { "-orient", TK_OPTION_ANY,
	offsetof(SeparatorElement, orientObj), "horizontal" },
    { "-background", TK_OPTION_BORDER,
	offsetof(SeparatorElement,borderObj), DEFAULT_BACKGROUND },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void SeparatorElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    (void)dummy;
    (void)elementRecord;
    (void)tkwin;
    (void)paddingPtr;

    *widthPtr = *heightPtr = 2;
}

static void HorizontalSeparatorElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    SeparatorElement *separator = (SeparatorElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, separator->borderObj);
    GC lightGC = Tk_3DBorderGC(tkwin, border, TK_3D_LIGHT_GC);
    GC darkGC = Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC);
    (void)dummy;
    (void)state;

    XDrawLine(Tk_Display(tkwin), d, darkGC, b.x, b.y, b.x + b.width, b.y);
    XDrawLine(Tk_Display(tkwin), d, lightGC, b.x, b.y+1, b.x + b.width, b.y+1);
}

static void VerticalSeparatorElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    SeparatorElement *separator = (SeparatorElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, separator->borderObj);
    GC lightGC = Tk_3DBorderGC(tkwin, border, TK_3D_LIGHT_GC);
    GC darkGC = Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC);
    (void)dummy;
    (void)state;

    XDrawLine(Tk_Display(tkwin), d, darkGC, b.x, b.y, b.x, b.y + b.height);
    XDrawLine(Tk_Display(tkwin), d, lightGC, b.x+1, b.y, b.x+1, b.y+b.height);
}

static void GeneralSeparatorElementDraw(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    SeparatorElement *separator = (SeparatorElement *)elementRecord;
    Ttk_Orient orient;

    TtkGetOrientFromObj(NULL, separator->orientObj, &orient);
    switch (orient) {
	case TTK_ORIENT_HORIZONTAL:
	    HorizontalSeparatorElementDraw(
		clientData, elementRecord, tkwin, d, b, state);
	    break;
	case TTK_ORIENT_VERTICAL:
	    VerticalSeparatorElementDraw(
		clientData, elementRecord, tkwin, d, b, state);
	    break;
    }
}

static const Ttk_ElementSpec HorizontalSeparatorElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(SeparatorElement),
    SeparatorElementOptions,
    SeparatorElementSize,
    HorizontalSeparatorElementDraw
};

static const Ttk_ElementSpec VerticalSeparatorElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(SeparatorElement),
    SeparatorElementOptions,
    SeparatorElementSize,
    HorizontalSeparatorElementDraw
};

static const Ttk_ElementSpec SeparatorElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(SeparatorElement),
    SeparatorElementOptions,
    SeparatorElementSize,
    GeneralSeparatorElementDraw
};

/*----------------------------------------------------------------------
 * +++ Sizegrip: lower-right corner grip handle for resizing window.
 */

typedef struct {
    Tcl_Obj	*backgroundObj;
    Tcl_Obj	*gripSizeObj;
} SizegripElement;

static const Ttk_ElementOptionSpec SizegripOptions[] = {
    { "-background", TK_OPTION_BORDER,
	offsetof(SizegripElement,backgroundObj), DEFAULT_BACKGROUND },
    { "-gripsize", TK_OPTION_PIXELS,
	offsetof(SizegripElement,gripSizeObj), "11.25p" },
    {0,TK_OPTION_BOOLEAN,0,0}
};

static void SizegripSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    SizegripElement *grip = (SizegripElement *)elementRecord;
    int gripSize = 0;
    (void)dummy;
    (void)tkwin;
    (void)paddingPtr;

    Tk_GetPixelsFromObj(NULL, tkwin, grip->gripSizeObj, &gripSize);
    *widthPtr = *heightPtr = gripSize;
}

static void SizegripDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, Ttk_State state)
{
    SizegripElement *grip = (SizegripElement *)elementRecord;
    int gripSize = 0;
    int gripCount = 3, gripSpace, gripThickness;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, grip->backgroundObj);
    GC lightGC = Tk_3DBorderGC(tkwin, border, TK_3D_LIGHT_GC);
    GC darkGC = Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC);
    int x1 = b.x + b.width-1, y1 = b.y + b.height-1, x2 = x1, y2 = y1;
    (void)dummy;
    (void)state;

    Tk_GetPixelsFromObj(NULL, tkwin, grip->gripSizeObj, &gripSize);
    gripThickness = gripSize * 3 / (gripCount * 5);
    gripSpace = gripSize / 3 - gripThickness;
    while (gripCount--) {
	x1 -= gripSpace; y2 -= gripSpace;
	for (int i = 1; i < gripThickness; i++) {
	    XDrawLine(Tk_Display(tkwin), d, darkGC,  x1,y1, x2,y2); --x1; --y2;
	}
	XDrawLine(Tk_Display(tkwin), d, lightGC,  x1,y1, x2,y2); --x1; --y2;
    }
}

static const Ttk_ElementSpec SizegripElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(SizegripElement),
    SizegripOptions,
    SizegripSize,
    SizegripDraw
};

/*----------------------------------------------------------------------
 * +++ Indicator element.
 *
 * Draws the on/off indicator for checkbuttons and radiobuttons.
 */

/*
 * Indicator image descriptor:
 */
typedef struct {
    int width;				/* unscaled width */
    int height;				/* unscaled height */
    const char *const offDataPtr;
    const char *const onDataPtr;
    const char *const triDataPtr;
} IndicatorSpec;

static const char checkbtnOffData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <rect x='.5' y='.5' width='15' height='15' rx='1.5' fill='#ffffff' stroke='#888888'/>\n\
    </svg>";

static const char checkbtnOnData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <rect x='0' y='0' width='16' height='16' fill='#4a6984' rx='2'/>\n\
     <path d='m4.5 8 3 3 4-6' fill='none' stroke='#ffffff' stroke-linecap='round' stroke-linejoin='round' stroke-width='2'/>\n\
    </svg>";

static const char checkbtnTriData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <rect x='0' y='0' width='16' height='16' fill='#4a6984' rx='2'/>\n\
     <path d='m4 8h8' fill='none' stroke='#ffffff' stroke-width='2'/>\n\
    </svg>";

static const IndicatorSpec checkbutton_spec = {
    16, 16,
    checkbtnOffData,
    checkbtnOnData,
    checkbtnTriData
};

static const char radiobtnOffData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <circle cx='8' cy='8' r='7.5' fill='#ffffff' stroke='#888888'/>\n\
    </svg>";

static const char radiobtnOnData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <circle cx='8' cy='8' r='8' fill='#4a6984'/>\n\
     <circle cx='8' cy='8' r='3' fill='#ffffff'/>\n\
    </svg>";

static const char radiobtnTriData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <circle cx='8' cy='8' r='8' fill='#4a6984'/>\n\
     <path d='m4 8h8' fill='none' stroke='#ffffff' stroke-width='2'/>\n\
    </svg>";

static const IndicatorSpec radiobutton_spec = {
    16, 16,
    radiobtnOffData,
    radiobtnOnData,
    radiobtnTriData
};

typedef struct {
    Tcl_Obj *backgroundObj;
    Tcl_Obj *foregroundObj;
    Tcl_Obj *borderColorObj;
    Tcl_Obj *marginObj;
} IndicatorElement;

/*
 * Note that the -indicatorbackground and -indicatorforeground options below
 * have the same default value "#ffffff", but the -indicatorforeground option
 * will only be used for the alternate and selected states, in which the
 * -indicatorbackground option will have a different value (e.g., "#4a6984").
 */
static const Ttk_ElementOptionSpec IndicatorElementOptions[] = {
    { "-indicatorbackground", TK_OPTION_COLOR,
	offsetof(IndicatorElement,backgroundObj), "#ffffff" },
    { "-indicatorforeground", TK_OPTION_COLOR,
        offsetof(IndicatorElement,foregroundObj), "#ffffff" },
    { "-bordercolor", TK_OPTION_COLOR,
	offsetof(IndicatorElement,borderColorObj), "#888888" },
    { "-indicatormargin", TK_OPTION_STRING,
	offsetof(IndicatorElement,marginObj), "0 2 4 2" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void IndicatorElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    const IndicatorSpec *spec = (const IndicatorSpec *)clientData;
    IndicatorElement *indicator = (IndicatorElement *)elementRecord;
    Ttk_Padding margins;
    double scalingLevel = TkScalingLevel(tkwin);
    (void)paddingPtr;

    Ttk_GetPaddingFromObj(NULL, tkwin, indicator->marginObj, &margins);
    *widthPtr = spec->width * scalingLevel + Ttk_PaddingWidth(margins);
    *heightPtr = spec->height * scalingLevel + Ttk_PaddingHeight(margins);
}

static void ColorToStr(
    const XColor *colorPtr, char *colorStr)     /* in the format "RRGGBB" */
{
    snprintf(colorStr, 7, "%02x%02x%02x",
             colorPtr->red >> 8, colorPtr->green >> 8, colorPtr->blue >> 8);
}

static void ImageChanged(               /* to be passed to Tk_GetImage() */
    ClientData clientData,
    int x, int y, int width, int height,
    int imageWidth, int imageHeight)
{
    (void)clientData;
    (void)x; (void)y; (void)width; (void)height;
    (void)imageWidth; (void)imageHeight;
}

static void IndicatorElementDraw(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    IndicatorElement *indicator = (IndicatorElement *)elementRecord;
    Ttk_Padding padding;
    const IndicatorSpec *spec = (const IndicatorSpec *)clientData;
    double scalingLevel = TkScalingLevel(tkwin);
    int width = spec->width * scalingLevel;
    int height = spec->height * scalingLevel;

    char bgColorStr[7], fgColorStr[7], borderColorStr[7];
    unsigned int selected = (state & TTK_STATE_SELECTED);
    unsigned int tristate = (state & TTK_STATE_ALTERNATE);
    Tcl_Interp *interp = Tk_Interp(tkwin);
    char imgName[60];
    Tk_Image img;

    const char *svgDataPtr;
    size_t svgDataLen;
    char *svgDataCopy;
    char *bgColorPtr, *fgColorPtr, *borderColorPtr;
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
     * Construct the color strings bgColorStr, fgColorStr, and borderColorStr
     */
    ColorToStr(Tk_GetColorFromObj(tkwin, indicator->backgroundObj),
	       bgColorStr);
    ColorToStr(Tk_GetColorFromObj(tkwin, indicator->foregroundObj),
	       fgColorStr);
    ColorToStr(Tk_GetColorFromObj(tkwin, indicator->borderColorObj),
	       borderColorStr);

    /*
     * Check whether there is an SVG image of this size for the
     * indicator's type (0 = checkbtn, 1 = radiobtn), "state"
     * (0 = off, 1 = on, 2 = tristate), and these color strings
     */
    snprintf(imgName, sizeof(imgName),
	     "::tk::icons::indicator_default%d_%d,%d_%s_%s_%s",
	     width,
	     spec->offDataPtr == radiobtnOffData,
	     tristate ? 2 : (selected ? 1 : 0),
	     bgColorStr,
	     selected || tristate ? fgColorStr : "XXXXXX",
	     selected || tristate ? "XXXXXX" : borderColorStr);
    img = Tk_GetImage(interp, tkwin, imgName, ImageChanged, NULL);
    if (img == NULL) {
	/*
	 * Determine the SVG data to use for the photo image
	 */
	svgDataPtr = (tristate ? spec->triDataPtr :
		      (selected ? spec->onDataPtr : spec->offDataPtr));

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
	if (selected || tristate) {
	    bgColorPtr = strstr((char *)svgDataPtr, "4a6984");
	    fgColorPtr = strstr((char *)svgDataPtr, "ffffff");

	    assert(bgColorPtr);
	    assert(fgColorPtr);

	    memcpy(bgColorPtr, bgColorStr, 6);
	    memcpy(fgColorPtr, fgColorStr, 6);
	} else {
	    bgColorPtr =     strstr((char *)svgDataPtr, "ffffff");
	    borderColorPtr = strstr((char *)svgDataPtr, "888888");

	    assert(bgColorPtr);
	    assert(borderColorPtr);

	    memcpy(bgColorPtr, bgColorStr, 6);
	    memcpy(borderColorPtr, borderColorStr, 6);
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
 * 	Draws a solid triangle inside a box.
 * 	clientData is an enum ArrowDirection pointer.
 */

typedef struct {
    Tcl_Obj *sizeObj;
    Tcl_Obj *colorObj;
    Tcl_Obj *borderObj;
    Tcl_Obj *borderWidthObj;
    Tcl_Obj *reliefObj;
} ArrowElement;

static const Ttk_ElementOptionSpec ArrowElementOptions[] = {
    { "-arrowsize", TK_OPTION_PIXELS,
	offsetof(ArrowElement,sizeObj), "14" },
    { "-arrowcolor", TK_OPTION_COLOR,
	offsetof(ArrowElement,colorObj), "black"},
    { "-background", TK_OPTION_BORDER,
	offsetof(ArrowElement,borderObj), DEFAULT_BACKGROUND },
    { "-borderwidth", TK_OPTION_PIXELS,
	offsetof(ArrowElement,borderWidthObj), "1" },
    { "-relief", TK_OPTION_RELIEF,
	offsetof(ArrowElement,reliefObj), "raised"},
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static const Ttk_Padding ArrowPadding = { 3,3,3,3 };

static void ArrowElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    ArrowElement *arrow = (ArrowElement *)elementRecord;
    ArrowDirection direction = (ArrowDirection)PTR2INT(clientData);
    double scalingLevel = TkScalingLevel(tkwin);
    Ttk_Padding padding;
    int size = 14;
    (void)paddingPtr;

    padding.left = round(ArrowPadding.left * scalingLevel);
    padding.top = round(ArrowPadding.top * scalingLevel);
    padding.right = round(ArrowPadding.right * scalingLevel);
    padding.bottom = round(ArrowPadding.bottom * scalingLevel);

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
    Drawable d, Ttk_Box b, unsigned int state)
{
    ArrowElement *arrow = (ArrowElement *)elementRecord;
    ArrowDirection direction = (ArrowDirection)PTR2INT(clientData);
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, arrow->borderObj);
    int borderWidth = 1, relief = TK_RELIEF_RAISED;
    Ttk_Padding padding;
    double scalingLevel = TkScalingLevel(tkwin);
    int cx = 0, cy = 0;
    XColor *arrowColor = Tk_GetColorFromObj(tkwin, arrow->colorObj);
    GC gc = Tk_GCForColor(arrowColor, d);
    (void)state;

    Tk_GetPixelsFromObj(NULL, tkwin, arrow->borderWidthObj, &borderWidth);
    Tk_GetReliefFromObj(NULL, arrow->reliefObj, &relief);

    Tk_Fill3DRectangle(tkwin, d, border, b.x, b.y, b.width, b.height,
	    borderWidth, relief);

    padding.left = round(ArrowPadding.left * scalingLevel);
    padding.top = round(ArrowPadding.top * scalingLevel);
    padding.right = round(ArrowPadding.right * scalingLevel);
    padding.bottom = round(ArrowPadding.bottom * scalingLevel);

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
 * 	The width and height are different, and the left edge is drawn in the
 *	same color as the right one.
 */

static void BoxArrowElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    ArrowElement *arrow = (ArrowElement *)elementRecord;
    ArrowDirection direction = (ArrowDirection)PTR2INT(clientData);
    double scalingLevel = TkScalingLevel(tkwin);
    Ttk_Padding padding;
    int size = 14;
    (void)paddingPtr;

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
    Drawable d, Ttk_Box b, unsigned int state)
{
    ArrowElement *arrow = (ArrowElement *)elementRecord;
    ArrowDirection direction = (ArrowDirection)PTR2INT(clientData);
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, arrow->borderObj);
    int borderWidth = 1, relief = TK_RELIEF_RAISED;
    Display *disp = Tk_Display(tkwin);
    GC darkGC = Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC);
    int w = WIN32_XDRAWLINE_HACK;
    Ttk_Padding padding;
    double scalingLevel = TkScalingLevel(tkwin);
    int cx = 0, cy = 0;
    XColor *arrowColor = Tk_GetColorFromObj(tkwin, arrow->colorObj);
    GC arrowGC = Tk_GCForColor(arrowColor, d);
    (void)state;

    Tk_Fill3DRectangle(tkwin, d, border, b.x, b.y, b.width, b.height,
	    borderWidth, relief);

    XDrawLine(disp, d, darkGC, b.x, b.y+1, b.x, b.y+b.height-1+w);

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

/*
 *----------------------------------------------------------------------
 * +++ Menubutton indicators.
 *
 * These aren't functional like radio/check indicators,
 * they're just affordability indicators.
 */

#define MENUBUTTON_ARROW_SIZE 5

typedef struct {
    Tcl_Obj *sizeObj;
    Tcl_Obj *colorObj;
    Tcl_Obj *paddingObj;
} MenuIndicatorElement;

static const Ttk_ElementOptionSpec MenuIndicatorElementOptions[] = {
    { "-arrowsize", TK_OPTION_PIXELS,
	offsetof(MenuIndicatorElement,sizeObj), STRINGIFY(MENUBUTTON_ARROW_SIZE)},
    { "-arrowcolor", TK_OPTION_COLOR,
	offsetof(MenuIndicatorElement,colorObj), "black" },
    { "-arrowpadding", TK_OPTION_STRING,
	offsetof(MenuIndicatorElement,paddingObj), "3" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void MenuIndicatorElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    MenuIndicatorElement *indicator = (MenuIndicatorElement *)elementRecord;
    Ttk_Padding margins;
    int size = MENUBUTTON_ARROW_SIZE;
    (void)dummy;
    (void)paddingPtr;

    Tk_GetPixelsFromObj(NULL, tkwin, indicator->sizeObj, &size);
    Ttk_GetPaddingFromObj(NULL, tkwin, indicator->paddingObj, &margins);
    TtkArrowSize(size, ARROW_DOWN, widthPtr, heightPtr);
    *widthPtr += Ttk_PaddingWidth(margins);
    *heightPtr += Ttk_PaddingHeight(margins);
}

static void MenuIndicatorElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    MenuIndicatorElement *indicator = (MenuIndicatorElement *)elementRecord;
    XColor *arrowColor = Tk_GetColorFromObj(tkwin, indicator->colorObj);
    GC gc = Tk_GCForColor(arrowColor, d);
    int size = MENUBUTTON_ARROW_SIZE;
    int width, height;
    (void)dummy;
    (void)state;

    Tk_GetPixelsFromObj(NULL, tkwin, indicator->sizeObj, &size);

    TtkArrowSize(size, ARROW_DOWN, &width, &height);
    b = Ttk_StickBox(b, width, height, 0);
    TtkFillArrow(Tk_Display(tkwin), d, gc, b, ARROW_DOWN);
}

static const Ttk_ElementSpec MenuIndicatorElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(MenuIndicatorElement),
    MenuIndicatorElementOptions,
    MenuIndicatorElementSize,
    MenuIndicatorElementDraw
};

/*
 *----------------------------------------------------------------------
 * +++ Trough element.
 *
 * Used in scrollbars and scales in place of "border".
 *
 * The -groovewidth option can be used to set the size of the short axis
 * for the drawn area. This will not affect the geometry, but can be used
 * to draw a thin centered trough inside the packet alloted. Use -1 or a
 * large number to use the full area (default).
 */

typedef struct {
    Tcl_Obj *borderWidthObj;
    Tcl_Obj *reliefObj;
    Tcl_Obj *colorObj;
    Tcl_Obj *grooveWidthObj;
    Tcl_Obj *orientObj;
} TroughElement;

static const Ttk_ElementOptionSpec TroughElementOptions[] = {
    { "-troughborderwidth", TK_OPTION_PIXELS,
	offsetof(TroughElement,borderWidthObj), "1" },
    { "-troughrelief",TK_OPTION_RELIEF,
	offsetof(TroughElement,reliefObj), "sunken" },
    { "-troughcolor", TK_OPTION_BORDER,
	offsetof(TroughElement,colorObj), DEFAULT_BACKGROUND },
    { "-groovewidth", TK_OPTION_PIXELS,
	offsetof(TroughElement,grooveWidthObj), "-1" },
    { "-orient", TK_OPTION_ANY,
	offsetof(TroughElement, orientObj), "horizontal" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void TroughElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    TroughElement *troughPtr = (TroughElement *)elementRecord;
    int borderWidth = 1, grooveWidth = -1;
    (void)dummy;
    (void)widthPtr;
    (void)heightPtr;

    Tk_GetPixelsFromObj(NULL, tkwin, troughPtr->borderWidthObj, &borderWidth);
    Tk_GetPixelsFromObj(NULL, tkwin, troughPtr->grooveWidthObj, &grooveWidth);

    if (grooveWidth <= 0) {
	*paddingPtr = Ttk_UniformPadding((short)borderWidth);
    }
}

static Ttk_Box troughInnerBox;

static void TroughElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    TroughElement *troughPtr = (TroughElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, troughPtr->colorObj);
    int borderWidth = 1, grooveWidth = -1, relief = TK_RELIEF_SUNKEN;
    Ttk_Orient orient;
    (void)dummy;
    (void)state;

    Tk_GetPixelsFromObj(NULL, tkwin, troughPtr->borderWidthObj, &borderWidth);
    Tk_GetPixelsFromObj(NULL, tkwin, troughPtr->grooveWidthObj, &grooveWidth);
    Tk_GetReliefFromObj(NULL, troughPtr->reliefObj, &relief);
    TtkGetOrientFromObj(NULL, troughPtr->orientObj, &orient);

    if (grooveWidth > 0 && grooveWidth < b.height && grooveWidth < b.width) {
	if (orient == TTK_ORIENT_HORIZONTAL) {
	    b.y += (b.height - grooveWidth) / 2;
	    b.height = grooveWidth;
	} else {
	    b.x += (b.width - grooveWidth) / 2;
	    b.width = grooveWidth;
        }

	troughInnerBox.x = b.x + borderWidth;
	troughInnerBox.y = b.y + borderWidth;
	troughInnerBox.width =  b.width -  2*borderWidth;
	troughInnerBox.height = b.height - 2*borderWidth;
    }

    Tk_Fill3DRectangle(tkwin, d, border, b.x, b.y, b.width, b.height,
	    borderWidth, relief);
}

static const Ttk_ElementSpec TroughElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(TroughElement),
    TroughElementOptions,
    TroughElementSize,
    TroughElementDraw
};

/*
 *----------------------------------------------------------------------
 * +++ Thumb element.
 *
 * Used in scrollbars.
 */

typedef struct {
    Tcl_Obj *orientObj;
    Tcl_Obj *thicknessObj;
    Tcl_Obj *reliefObj;
    Tcl_Obj *borderObj;
    Tcl_Obj *borderWidthObj;
} ThumbElement;

static const Ttk_ElementOptionSpec ThumbElementOptions[] = {
    { "-orient", TK_OPTION_ANY,
	offsetof(ThumbElement, orientObj), "horizontal" },
    { "-width", TK_OPTION_PIXELS,
	offsetof(ThumbElement,thicknessObj), DEFAULT_ARROW_SIZE },
    { "-relief", TK_OPTION_RELIEF,
	offsetof(ThumbElement,reliefObj), "raised" },
    { "-background", TK_OPTION_BORDER,
	offsetof(ThumbElement,borderObj), DEFAULT_BACKGROUND },
    { "-borderwidth", TK_OPTION_PIXELS,
	offsetof(ThumbElement,borderWidthObj), DEFAULT_BORDERWIDTH },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void ThumbElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    ThumbElement *thumb = (ThumbElement *)elementRecord;
    Ttk_Orient orient;
    int thickness;
    (void)dummy;
    (void)paddingPtr;

    Tk_GetPixelsFromObj(NULL, tkwin, thumb->thicknessObj, &thickness);
    TtkGetOrientFromObj(NULL, thumb->orientObj, &orient);

    if (orient == TTK_ORIENT_VERTICAL) {
	*widthPtr = thickness;
	*heightPtr = MIN_THUMB_SIZE;
    } else {
	*widthPtr = MIN_THUMB_SIZE;
	*heightPtr = thickness;
    }
}

static void ThumbElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    ThumbElement *thumb = (ThumbElement *)elementRecord;
    Tk_3DBorder  border = Tk_Get3DBorderFromObj(tkwin, thumb->borderObj);
    int borderWidth = 2, relief = TK_RELIEF_RAISED;
    (void)dummy;
    (void)state;

    Tk_GetPixelsFromObj(NULL, tkwin, thumb->borderWidthObj, &borderWidth);
    Tk_GetReliefFromObj(NULL, thumb->reliefObj, &relief);
    Tk_Fill3DRectangle(tkwin, d, border, b.x, b.y, b.width, b.height,
	    borderWidth, relief);
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
 * This is the moving part of the scale widget.  Drawn as a filled circle.
 */

#define SLIDER_DIM 16

static const char sliderData[] = "\
    <svg width='16' height='16' version='1.1' xmlns='http://www.w3.org/2000/svg'>\n\
     <circle cx='8' cy='8' r='7.5' fill='#ffffff' stroke='#c3c3c3'/>\n\
     <circle cx='8' cy='8' r='4' fill='#4a6984'/>\n\
    </svg>";

typedef struct {
    Tcl_Obj *innerColorObj;
    Tcl_Obj *outerColorObj;
    Tcl_Obj *borderColorObj;
    Tcl_Obj *orientObj;		/* Orientation of overall slider */
} SliderElement;

static const Ttk_ElementOptionSpec SliderElementOptions[] = {
    { "-innercolor", TK_OPTION_COLOR, offsetof(SliderElement,innerColorObj),
	"#4a6984" },
    { "-outercolor", TK_OPTION_COLOR, offsetof(SliderElement,outerColorObj),
	"#ffffff" },
    { "-bordercolor", TK_OPTION_COLOR, offsetof(SliderElement,borderColorObj),
	"#c3c3c3" },
    { "-orient", TK_OPTION_ANY, offsetof(SliderElement,orientObj),
	"horizontal" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void SliderElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    (void)dummy;
    (void)elementRecord;
    (void)paddingPtr;

    double scalingLevel = TkScalingLevel(tkwin);
    *widthPtr = *heightPtr = SLIDER_DIM * scalingLevel;
}

static void SliderElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    (void)dummy;
    (void)state;

    double scalingLevel = TkScalingLevel(tkwin);
    int dim = SLIDER_DIM * scalingLevel;

    SliderElement *slider = (SliderElement *)elementRecord;
    Ttk_Orient orient;
    Display *disp = Tk_Display(tkwin);
    XColor *innerColor = Tk_GetColorFromObj(tkwin, slider->innerColorObj);
    XColor *outerColor = Tk_GetColorFromObj(tkwin, slider->outerColorObj);
    XColor *borderColor = Tk_GetColorFromObj(tkwin, slider->borderColorObj);
    GC gc = Tk_GCForColor(innerColor, d);

    char innerColorStr[7], outerColorStr[7], borderColorStr[7];
    Tcl_Interp *interp = Tk_Interp(tkwin);
    char imgName[50];
    Tk_Image img;

    const char *svgDataPtr = sliderData;
    size_t svgDataLen;
    char *svgDataCopy;
    char *innerColorPtr, *outerColorPtr, *borderColorPtr;
    const char *cmdFmt;
    size_t scriptSize;
    char *script;
    int code;

    /*
     * Sanity check
     */
    if (   b.x < 0
	|| b.y < 0
	|| Tk_Width(tkwin) < b.x + dim
	|| Tk_Height(tkwin) < b.y + dim)
    {
	/* Oops!  Not enough room to display the image.
	 * Don't draw anything.
	 */
	return;
    }

    /*
     * Fill the thin trough area preceding the
     * slider's center with the inner color
     */
    TtkGetOrientFromObj(NULL, slider->orientObj, &orient);
    switch (orient) {
	case TTK_ORIENT_HORIZONTAL:
	    XFillRectangle(disp, d, gc, troughInnerBox.x, troughInnerBox.y,
		    b.x + dim/2 - 1, troughInnerBox.height);
	    break;
	case TTK_ORIENT_VERTICAL:
	    XFillRectangle(disp, d, gc, troughInnerBox.x, troughInnerBox.y,
		    troughInnerBox.width, b.y + dim/2 - 1);
	    break;
    }

    /*
     * Construct the color strings innerColorStr,
     * outerColorStr, and borderColorStr
     */
    ColorToStr(innerColor, innerColorStr);
    ColorToStr(outerColor, outerColorStr);
    ColorToStr(borderColor, borderColorStr);

    /*
     * Check whether there is an SVG image of this size for these color strings
     */
    snprintf(imgName, sizeof(imgName),
	     "::tk::icons::slider_default%d_%s_%s_%s",
	     dim, innerColorStr, outerColorStr, borderColorStr);
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
	innerColorPtr = strstr((char *)svgDataPtr, "4a6984");
	outerColorPtr = strstr((char *)svgDataPtr, "ffffff");
	borderColorPtr = strstr((char *)svgDataPtr, "c3c3c3");
	assert(innerColorPtr);
	assert(outerColorPtr);
	assert(borderColorPtr);
	memcpy(innerColorPtr, innerColorStr, 6);
	memcpy(outerColorPtr, outerColorStr, 6);
	memcpy(borderColorPtr, borderColorStr, 6);

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
    Tk_RedrawImage(img, 0, 0, dim, dim, d, b.x, b.y);
    Tk_FreeImage(img);
}

static const Ttk_ElementSpec SliderElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(SliderElement),
    SliderElementOptions,
    SliderElementSize,
    SliderElementDraw
};

/*------------------------------------------------------------------------
 * +++ Progress bar element:
 *	Draws the moving part of the progress bar.
 *
 *	-thickness specifies the size along the short axis of the bar.
 *	-length specifies the default size along the long axis;
 *	the bar will be this long in indeterminate mode.
 */

#define DEFAULT_PBAR_THICKNESS "15"
#define DEFAULT_PBAR_LENGTH "30"

typedef struct {
    Tcl_Obj *orientObj; 	/* widget orientation */
    Tcl_Obj *thicknessObj;	/* the height/width of the bar */
    Tcl_Obj *lengthObj;		/* default width/height of the bar */
    Tcl_Obj *reliefObj; 	/* border relief for this object */
    Tcl_Obj *borderObj; 	/* background color */
    Tcl_Obj *borderWidthObj; 	/* thickness of the border */
} PbarElement;

static const Ttk_ElementOptionSpec PbarElementOptions[] = {
    { "-orient", TK_OPTION_ANY, offsetof(PbarElement,orientObj),
	"horizontal" },
    { "-thickness", TK_OPTION_PIXELS, offsetof(PbarElement,thicknessObj),
	DEFAULT_PBAR_THICKNESS },
    { "-barsize", TK_OPTION_PIXELS, offsetof(PbarElement,lengthObj),
	DEFAULT_PBAR_LENGTH },
    { "-pbarrelief", TK_OPTION_RELIEF, offsetof(PbarElement,reliefObj),
	"raised" },
    { "-borderwidth", TK_OPTION_PIXELS, offsetof(PbarElement,borderWidthObj),
	DEFAULT_BORDERWIDTH },
    { "-background", TK_OPTION_BORDER, offsetof(PbarElement,borderObj),
	DEFAULT_BACKGROUND },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void PbarElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    PbarElement *pbar = (PbarElement *)elementRecord;
    Ttk_Orient orient;
    int thickness = 15, length = 30, borderWidth = 2;
    (void)dummy;
    (void)paddingPtr;

    TtkGetOrientFromObj(NULL, pbar->orientObj, &orient);
    Tk_GetPixelsFromObj(NULL, tkwin, pbar->thicknessObj, &thickness);
    Tk_GetPixelsFromObj(NULL, tkwin, pbar->lengthObj, &length);
    Tk_GetPixelsFromObj(NULL, tkwin, pbar->borderWidthObj, &borderWidth);

    switch (orient) {
	case TTK_ORIENT_HORIZONTAL:
	    *widthPtr	= length + 2 * borderWidth;
	    *heightPtr	= thickness + 2 * borderWidth;
	    break;
	case TTK_ORIENT_VERTICAL:
	    *widthPtr	= thickness + 2 * borderWidth;
	    *heightPtr	= length + 2 * borderWidth;
	    break;
    }
}

static void PbarElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, Ttk_State state)
{
    PbarElement *pbar = (PbarElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, pbar->borderObj);
    int relief = TK_RELIEF_RAISED, borderWidth = 2;
    (void)dummy;
    (void)state;

    Tk_GetPixelsFromObj(NULL, tkwin, pbar->borderWidthObj, &borderWidth);
    Tk_GetReliefFromObj(NULL, pbar->reliefObj, &relief);

    Tk_Fill3DRectangle(tkwin, d, border,
	b.x, b.y, b.width, b.height,
	borderWidth, relief);
}

static const Ttk_ElementSpec PbarElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(PbarElement),
    PbarElementOptions,
    PbarElementSize,
    PbarElementDraw
};

/*------------------------------------------------------------------------
 * +++ Notebook tabs and client area.
 */

typedef struct {
    Tcl_Obj *borderWidthObj;
    Tcl_Obj *backgroundObj;
    Tcl_Obj *highlightObj;
    Tcl_Obj *highlightColorObj;
} TabElement;

static const Ttk_ElementOptionSpec TabElementOptions[] = {
    { "-borderwidth", TK_OPTION_PIXELS,
	offsetof(TabElement,borderWidthObj), "1" },
    { "-background", TK_OPTION_BORDER,
	offsetof(TabElement,backgroundObj), DEFAULT_BACKGROUND },
    { "-highlight", TK_OPTION_BOOLEAN,
	offsetof(TabElement,highlightObj), "0" },
    { "-highlightcolor", TK_OPTION_COLOR,
	offsetof(TabElement,highlightColorObj), "#4a6984" },
    {0,TK_OPTION_BOOLEAN,0,0}
};

static void TabElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    TabElement *tab = (TabElement *)elementRecord;
    int borderWidth = 1;
    (void)dummy;
    (void)widthPtr;
    (void)heightPtr;

    Tk_GetPixelsFromObj(0, tkwin, tab->borderWidthObj, &borderWidth);
    paddingPtr->top = paddingPtr->left = paddingPtr->right = borderWidth;
    paddingPtr->bottom = 0;
}

static void TabElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    TabElement *tab = (TabElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, tab->backgroundObj);
    int borderWidth = 1, highlight = 0;
    XPoint pts[6];
    int n = 0;
    double scalingLevel = TkScalingLevel(tkwin);
    int cut = round(2 * scalingLevel);
    Display *disp = Tk_Display(tkwin);
    (void)dummy;

    Tcl_GetIntFromObj(NULL, tab->borderWidthObj, &borderWidth);

    if (state & TTK_STATE_SELECTED) {
	/*
	 * Draw slightly outside of the allocated parcel,
	 * to overwrite the client area border.
	 */
	b.height += borderWidth;

	Tcl_GetBooleanFromObj(NULL, tab->highlightObj, &highlight);
    }

    pts[n].x = b.x; 			pts[n].y = b.y + b.height - 1; ++n;
    pts[n].x = b.x;			pts[n].y = b.y + cut; ++n;
    pts[n].x = b.x + cut;  		pts[n].y = b.y; ++n;
    pts[n].x = b.x + b.width-1-cut;	pts[n].y = b.y; ++n;
    pts[n].x = b.x + b.width-1; 	pts[n].y = b.y + cut; ++n;
    pts[n].x = b.x + b.width-1; 	pts[n].y = b.y + b.height; ++n;

    XFillPolygon(disp, d, Tk_3DBorderGC(tkwin, border, TK_3D_FLAT_GC),
	    pts, 6, Convex, CoordModeOrigin);

    pts[5].y -= 1 - WIN32_XDRAWLINE_HACK;

    while (borderWidth--) {
	XDrawLines(disp, d, Tk_3DBorderGC(tkwin, border, TK_3D_LIGHT_GC),
		pts, 4, CoordModeOrigin);
	XDrawLines(disp, d, Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC),
		pts+3, 3, CoordModeOrigin);
	++pts[0].x; ++pts[1].x; ++pts[2].x;             --pts[4].x; --pts[5].x;
	                        ++pts[2].y; ++pts[3].y;
    }

    if (highlight) {
	XColor *hlColor = Tk_GetColorFromObj(tkwin, tab->highlightColorObj);
	XFillRectangle(disp, d, Tk_GCForColor(hlColor, d),
		b.x + cut, b.y, b.width - 2*cut, cut);
    }
}

static const Ttk_ElementSpec TabElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(TabElement),
    TabElementOptions,
    TabElementSize,
    TabElementDraw
};

/*
 * Client area element:
 * Uses same resources as tab element.
 */
typedef TabElement ClientElement;
#define ClientElementOptions TabElementOptions

static void ClientElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    ClientElement *ce = (ClientElement *)elementRecord;
    int borderWidth = 1;
    (void)dummy;
    (void)widthPtr;
    (void)heightPtr;

    Tk_GetPixelsFromObj(0, tkwin, ce->borderWidthObj, &borderWidth);
    *paddingPtr = Ttk_UniformPadding((short)borderWidth);
}

static void ClientElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    ClientElement *ce = (ClientElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, ce->backgroundObj);
    int borderWidth = 1;
    (void)dummy;
    (void)state;

    Tcl_GetIntFromObj(NULL, ce->borderWidthObj, &borderWidth);

    Tk_Fill3DRectangle(tkwin, d, border,
	b.x, b.y, b.width, b.height, borderWidth,TK_RELIEF_RAISED);
}

static const Ttk_ElementSpec ClientElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(ClientElement),
    ClientElementOptions,
    ClientElementSize,
    ClientElementDraw
};

/*----------------------------------------------------------------------
 * TtkElements_Init --
 *	Register default element implementations.
 */

MODULE_SCOPE
void TtkElements_Init(Tcl_Interp *interp);

MODULE_SCOPE
void TtkElements_Init(Tcl_Interp *interp)
{
    Ttk_Theme theme =  Ttk_GetDefaultTheme(interp);

    /*
     * Elements:
     */
    Ttk_RegisterElement(interp, theme, "background",
	    &BackgroundElementSpec,NULL);

    Ttk_RegisterElement(interp, theme, "fill", &FillElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "border", &BorderElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "field", &FieldElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "focus", &FocusElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "padding", &PaddingElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "Checkbutton.indicator",
	    &IndicatorElementSpec, (void *)&checkbutton_spec);
    Ttk_RegisterElement(interp, theme, "Radiobutton.indicator",
	    &IndicatorElementSpec, (void *)&radiobutton_spec);
    Ttk_RegisterElement(interp, theme, "Menubutton.indicator",
	    &MenuIndicatorElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "indicator", &ttkNullElementSpec, NULL);

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

    Ttk_RegisterElement(interp, theme, "trough", &TroughElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "thumb", &ThumbElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "slider", &SliderElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "pbar", &PbarElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "separator",
	    &SeparatorElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "hseparator",
	    &HorizontalSeparatorElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "vseparator",
	    &VerticalSeparatorElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "sizegrip", &SizegripElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "tab", &TabElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "client", &ClientElementSpec, NULL);

    /*
     * Register "default" as a user-loadable theme (for now):
     */
    Tcl_PkgProvideEx(interp, "ttk::theme::default", TTK_VERSION, NULL);
}

/*EOF*/
