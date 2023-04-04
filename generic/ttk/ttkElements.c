/*
 * Copyright © 2003 Joe English
 *
 * Default implementation for themed elements.
 *
 */

#include "tkInt.h"
#include "ttkTheme.h"
#include "ttkWidget.h"

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
	    b.x, b.y, b.width, b.height, borderWidth,relief);
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
} FieldElement;

static const Ttk_ElementOptionSpec FieldElementOptions[] = {
    { "-fieldbackground", TK_OPTION_BORDER,
	offsetof(FieldElement,borderObj), "white" },
    { "-borderwidth", TK_OPTION_PIXELS,
	offsetof(FieldElement,borderWidthObj), "2" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void FieldElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    FieldElement *field = (FieldElement *)elementRecord;
    int borderWidth = 2;
    (void)dummy;
    (void)widthPtr;
    (void)heightPtr;

    Tk_GetPixelsFromObj(NULL, tkwin, field->borderWidthObj, &borderWidth);
    *paddingPtr = Ttk_UniformPadding((short)borderWidth);
}

static void FieldElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    FieldElement *field = (FieldElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, field->borderObj);
    int borderWidth = 2;
    (void)dummy;
    (void)state;

    Tk_GetPixelsFromObj(NULL, tkwin, field->borderWidthObj, &borderWidth);
    Tk_Fill3DRectangle(tkwin, d, border,
	    b.x, b.y, b.width, b.height, borderWidth, TK_RELIEF_SUNKEN);
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
 *
 * Draws a 3-D square (or diamond), raised if off, sunken if on.
 *
 * This is actually a regression from Tk 8.5 back to the ugly old Motif
 * style; use "altTheme" for the newer, nicer version.
 */

typedef struct {
    Tcl_Obj *backgroundObj;
    Tcl_Obj *reliefObj;
    Tcl_Obj *colorObj;
    Tcl_Obj *sizeObj;
    Tcl_Obj *marginObj;
    Tcl_Obj *borderWidthObj;
} IndicatorElement;

static const Ttk_ElementOptionSpec IndicatorElementOptions[] = {
    { "-background", TK_OPTION_BORDER,
	offsetof(IndicatorElement,backgroundObj), DEFAULT_BACKGROUND },
    { "-indicatorcolor", TK_OPTION_BORDER,
	offsetof(IndicatorElement,colorObj), DEFAULT_BACKGROUND },
    { "-indicatorrelief", TK_OPTION_RELIEF,
	offsetof(IndicatorElement,reliefObj), "raised" },
    { "-indicatorsize", TK_OPTION_PIXELS,
	offsetof(IndicatorElement,sizeObj), "9p" },
    { "-indicatormargin", TK_OPTION_STRING,
	offsetof(IndicatorElement,marginObj), "0 2 4 2" },
    { "-borderwidth", TK_OPTION_PIXELS,
	offsetof(IndicatorElement,borderWidthObj), DEFAULT_BORDERWIDTH },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

/*
 * Checkbutton indicators (default): 3-D square.
 */
static void SquareIndicatorElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    IndicatorElement *indicator = (IndicatorElement *)elementRecord;
    Ttk_Padding margins;
    int diameter = 0;
    (void)dummy;
    (void)paddingPtr;

    Ttk_GetPaddingFromObj(NULL, tkwin, indicator->marginObj, &margins);
    Tk_GetPixelsFromObj(NULL, tkwin, indicator->sizeObj, &diameter);
    *widthPtr = diameter + Ttk_PaddingWidth(margins);
    *heightPtr = diameter + Ttk_PaddingHeight(margins);
}

static void SquareIndicatorElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    IndicatorElement *indicator = (IndicatorElement *)elementRecord;
    Tk_3DBorder border = 0, interior = 0;
    int relief = TK_RELIEF_RAISED;
    Ttk_Padding padding;
    int borderWidth = 2;
    int diameter;
    (void)dummy;
    (void)state;

    interior = Tk_Get3DBorderFromObj(tkwin, indicator->colorObj);
    border = Tk_Get3DBorderFromObj(tkwin, indicator->backgroundObj);
    Tcl_GetIntFromObj(NULL,indicator->borderWidthObj,&borderWidth);
    Tk_GetReliefFromObj(NULL,indicator->reliefObj,&relief);
    Ttk_GetPaddingFromObj(NULL,tkwin,indicator->marginObj,&padding);

    b = Ttk_PadBox(b, padding);

    diameter = b.width < b.height ? b.width : b.height;
    Tk_Fill3DRectangle(tkwin, d, interior, b.x, b.y,
	    diameter, diameter,borderWidth, TK_RELIEF_FLAT);
    Tk_Draw3DRectangle(tkwin, d, border, b.x, b.y,
	    diameter, diameter, borderWidth, relief);
}

/*
 * Radiobutton indicators:  3-D diamond.
 */
static void DiamondIndicatorElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    IndicatorElement *indicator = (IndicatorElement *)elementRecord;
    Ttk_Padding margins;
    int diameter = 0;
    (void)dummy;
    (void)paddingPtr;

    Ttk_GetPaddingFromObj(NULL, tkwin, indicator->marginObj, &margins);
    Tk_GetPixelsFromObj(NULL, tkwin, indicator->sizeObj, &diameter);
    *widthPtr = diameter + 3 + Ttk_PaddingWidth(margins);
    *heightPtr = diameter + 3 + Ttk_PaddingHeight(margins);
}

static void DiamondIndicatorElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    IndicatorElement *indicator = (IndicatorElement *)elementRecord;
    Tk_3DBorder border = 0, interior = 0;
    int borderWidth = 2;
    int relief = TK_RELIEF_RAISED;
    int diameter, radius;
    XPoint points[4];
    Ttk_Padding padding;
    (void)dummy;
    (void)state;

    interior = Tk_Get3DBorderFromObj(tkwin, indicator->colorObj);
    border = Tk_Get3DBorderFromObj(tkwin, indicator->backgroundObj);
    Tcl_GetIntFromObj(NULL,indicator->borderWidthObj,&borderWidth);
    Tk_GetReliefFromObj(NULL,indicator->reliefObj,&relief);
    Ttk_GetPaddingFromObj(NULL,tkwin,indicator->marginObj,&padding);

    b = Ttk_PadBox(b, padding);

    diameter = b.width < b.height ? b.width : b.height;
    radius = diameter / 2;

    points[0].x = b.x;
    points[0].y = b.y + radius;
    points[1].x = b.x + radius;
    points[1].y = b.y + 2*radius;
    points[2].x = b.x + 2*radius;
    points[2].y = b.y + radius;
    points[3].x = b.x + radius;
    points[3].y = b.y;

    Tk_Fill3DPolygon(tkwin,d,interior,points,4,borderWidth,TK_RELIEF_FLAT);
    Tk_Draw3DPolygon(tkwin,d,border,points,4,borderWidth,relief);
}

static const Ttk_ElementSpec CheckbuttonIndicatorElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(IndicatorElement),
    IndicatorElementOptions,
    SquareIndicatorElementSize,
    SquareIndicatorElementDraw
};

static const Ttk_ElementSpec RadiobuttonIndicatorElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(IndicatorElement),
    IndicatorElementOptions,
    DiamondIndicatorElementSize,
    DiamondIndicatorElementDraw
};

/*
 *----------------------------------------------------------------------
 * +++ Menubutton indicators.
 *
 * These aren't functional like radio/check indicators,
 * they're just affordability indicators.
 *
 * Standard Tk sets the indicator size to 4.0 mm by 1.7 mm.
 * I have no idea where these numbers came from.
 */

typedef struct {
    Tcl_Obj *backgroundObj;
    Tcl_Obj *widthObj;
    Tcl_Obj *heightObj;
    Tcl_Obj *borderWidthObj;
    Tcl_Obj *reliefObj;
    Tcl_Obj *marginObj;
} MenuIndicatorElement;

static const Ttk_ElementOptionSpec MenuIndicatorElementOptions[] = {
    { "-background", TK_OPTION_BORDER,
	offsetof(MenuIndicatorElement,backgroundObj), DEFAULT_BACKGROUND },
    { "-indicatorwidth", TK_OPTION_PIXELS,
	offsetof(MenuIndicatorElement,widthObj), "4.0m" },
    { "-indicatorheight", TK_OPTION_PIXELS,
	offsetof(MenuIndicatorElement,heightObj), "1.7m" },
    { "-borderwidth", TK_OPTION_PIXELS,
	offsetof(MenuIndicatorElement,borderWidthObj), DEFAULT_BORDERWIDTH },
    { "-indicatorrelief", TK_OPTION_RELIEF,
	offsetof(MenuIndicatorElement,reliefObj),"raised" },
    { "-indicatormargin", TK_OPTION_STRING,
	    offsetof(MenuIndicatorElement,marginObj), "5 0" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void MenuIndicatorElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    MenuIndicatorElement *mi = (MenuIndicatorElement *)elementRecord;
    Ttk_Padding margins;
    (void)dummy;
    (void)paddingPtr;

    Tk_GetPixelsFromObj(NULL, tkwin, mi->widthObj, widthPtr);
    Tk_GetPixelsFromObj(NULL, tkwin, mi->heightObj, heightPtr);
    Ttk_GetPaddingFromObj(NULL,tkwin,mi->marginObj, &margins);
    *widthPtr += Ttk_PaddingWidth(margins);
    *heightPtr += Ttk_PaddingHeight(margins);
}

static void MenuIndicatorElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    MenuIndicatorElement *mi = (MenuIndicatorElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, mi->backgroundObj);
    Ttk_Padding margins;
    int borderWidth = 2;
    (void)dummy;
    (void)state;

    Ttk_GetPaddingFromObj(NULL,tkwin,mi->marginObj,&margins);
    b = Ttk_PadBox(b, margins);
    Tk_GetPixelsFromObj(NULL, tkwin, mi->borderWidthObj, &borderWidth);
    Tk_Fill3DRectangle(tkwin, d, border, b.x, b.y, b.width, b.height,
	    borderWidth, TK_RELIEF_RAISED);
}

static const Ttk_ElementSpec MenuIndicatorElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(MenuIndicatorElement),
    MenuIndicatorElementOptions,
    MenuIndicatorElementSize,
    MenuIndicatorElementDraw
};

/*----------------------------------------------------------------------
 * +++ Arrow elements.
 *
 * 	Draws a solid triangle inside a box.
 * 	clientData is an enum ArrowDirection pointer.
 */

typedef struct {
    Tcl_Obj *borderObj;
    Tcl_Obj *borderWidthObj;
    Tcl_Obj *reliefObj;
    Tcl_Obj *sizeObj;
    Tcl_Obj *colorObj;
} ArrowElement;

static const Ttk_ElementOptionSpec ArrowElementOptions[] = {
    { "-background", TK_OPTION_BORDER,
	offsetof(ArrowElement,borderObj), DEFAULT_BACKGROUND },
    { "-relief",TK_OPTION_RELIEF,
	offsetof(ArrowElement,reliefObj),"raised"},
    { "-borderwidth", TK_OPTION_PIXELS,
	offsetof(ArrowElement,borderWidthObj), "1" },
    { "-arrowcolor",TK_OPTION_COLOR,
	offsetof(ArrowElement,colorObj),"black"},
    { "-arrowsize", TK_OPTION_PIXELS,
	offsetof(ArrowElement,sizeObj), "14" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static const Ttk_Padding ArrowMargins = { 3,3,3,3 };

static void ArrowElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    ArrowElement *arrow = (ArrowElement *)elementRecord;
    ArrowDirection direction = (ArrowDirection)PTR2INT(clientData);
    int width = 14;
    (void)paddingPtr;

    Tk_GetPixelsFromObj(NULL, tkwin, arrow->sizeObj, &width);
    width -= Ttk_PaddingWidth(ArrowMargins);
    TtkArrowSize(width/2, direction, widthPtr, heightPtr);
    *widthPtr += Ttk_PaddingWidth(ArrowMargins);
    *heightPtr += Ttk_PaddingWidth(ArrowMargins);
}

static void ArrowElementDraw(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    ArrowDirection direction = (ArrowDirection)PTR2INT(clientData);
    ArrowElement *arrow = (ArrowElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, arrow->borderObj);
    XColor *arrowColor = Tk_GetColorFromObj(tkwin, arrow->colorObj);
    int relief = TK_RELIEF_RAISED;
    int borderWidth = 1;
    (void)state;

    Tk_GetReliefFromObj(NULL, arrow->reliefObj, &relief);

    Tk_Fill3DRectangle( tkwin, d, border, b.x, b.y, b.width, b.height,
	    borderWidth, relief);

    TtkFillArrow(Tk_Display(tkwin), d, Tk_GCForColor(arrowColor, d),
	    Ttk_PadBox(b, ArrowMargins), direction);
}

static const Ttk_ElementSpec ArrowElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(ArrowElement),
    ArrowElementOptions,
    ArrowElementSize,
    ArrowElementDraw
};

/*----------------------------------------------------------------------
 * +++ Trough element.
 *
 * Used in scrollbars and scales in place of "border".
 */

typedef struct {
    Tcl_Obj *colorObj;
    Tcl_Obj *borderWidthObj;
    Tcl_Obj *reliefObj;
} TroughElement;

static const Ttk_ElementOptionSpec TroughElementOptions[] = {
    { "-borderwidth", TK_OPTION_PIXELS,
	offsetof(TroughElement,borderWidthObj), DEFAULT_BORDERWIDTH },
    { "-troughcolor", TK_OPTION_BORDER,
	offsetof(TroughElement,colorObj), DEFAULT_BACKGROUND },
    { "-troughrelief",TK_OPTION_RELIEF,
	offsetof(TroughElement,reliefObj), "sunken" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void TroughElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    TroughElement *troughPtr = (TroughElement *)elementRecord;
    int borderWidth = 2;
    (void)dummy;
    (void)widthPtr;
    (void)heightPtr;

    Tk_GetPixelsFromObj(NULL, tkwin, troughPtr->borderWidthObj, &borderWidth);
    *paddingPtr = Ttk_UniformPadding((short)borderWidth);
}

static void TroughElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    TroughElement *troughPtr = (TroughElement *)elementRecord;
    Tk_3DBorder border = NULL;
    int borderWidth = 2, relief = TK_RELIEF_SUNKEN;
    (void)dummy;
    (void)state;

    border = Tk_Get3DBorderFromObj(tkwin, troughPtr->colorObj);
    Tk_GetReliefFromObj(NULL, troughPtr->reliefObj, &relief);
    Tk_GetPixelsFromObj(NULL, tkwin, troughPtr->borderWidthObj, &borderWidth);

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
 * This is the moving part of the scale widget.  Drawn as a raised box.
 */

typedef struct {
    Tcl_Obj *orientObj;	     /* orientation of overall slider */
    Tcl_Obj *lengthObj;      /* slider length */
    Tcl_Obj *thicknessObj;   /* slider thickness */
    Tcl_Obj *reliefObj;      /* the relief for this object */
    Tcl_Obj *borderObj;      /* the background color */
    Tcl_Obj *borderWidthObj; /* the size of the border */
} SliderElement;

static const Ttk_ElementOptionSpec SliderElementOptions[] = {
    { "-sliderlength", TK_OPTION_PIXELS, offsetof(SliderElement,lengthObj),
	"30" },
    { "-sliderthickness",TK_OPTION_PIXELS, offsetof(SliderElement,thicknessObj),
	"15" },
    { "-sliderrelief", TK_OPTION_RELIEF, offsetof(SliderElement,reliefObj),
	"raised" },
    { "-borderwidth", TK_OPTION_PIXELS, offsetof(SliderElement,borderWidthObj),
	DEFAULT_BORDERWIDTH },
    { "-background", TK_OPTION_BORDER, offsetof(SliderElement,borderObj),
	DEFAULT_BACKGROUND },
    { "-orient", TK_OPTION_ANY, offsetof(SliderElement,orientObj),
	"horizontal" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void SliderElementSize(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    SliderElement *slider = (SliderElement *)elementRecord;
    Ttk_Orient orient;
    int length, thickness;
    (void)dummy;
    (void)paddingPtr;

    TtkGetOrientFromObj(NULL, slider->orientObj, &orient);
    Tk_GetPixelsFromObj(NULL, tkwin, slider->lengthObj, &length);
    Tk_GetPixelsFromObj(NULL, tkwin, slider->thicknessObj, &thickness);

    switch (orient) {
	case TTK_ORIENT_VERTICAL:
	    *widthPtr = thickness;
	    *heightPtr = length;
	    break;

	case TTK_ORIENT_HORIZONTAL:
	    *widthPtr = length;
	    *heightPtr = thickness;
	    break;
    }
}

static void SliderElementDraw(
    void *dummy, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    SliderElement *slider = (SliderElement *)elementRecord;
    Tk_3DBorder border = NULL;
    int relief = TK_RELIEF_RAISED, borderWidth = 2;
    Ttk_Orient orient;
    (void)dummy;
    (void)state;

    border = Tk_Get3DBorderFromObj(tkwin, slider->borderObj);
    TtkGetOrientFromObj(NULL, slider->orientObj, &orient);
    Tk_GetPixelsFromObj(NULL, tkwin, slider->borderWidthObj, &borderWidth);
    Tk_GetReliefFromObj(NULL, slider->reliefObj, &relief);

    Tk_Fill3DRectangle(tkwin, d, border,
	b.x, b.y, b.width, b.height,
	borderWidth, relief);

    if (relief != TK_RELIEF_FLAT) {
	if (orient == TTK_ORIENT_HORIZONTAL) {
	    if (b.width > 4) {
		b.x += b.width/2;
		XDrawLine(Tk_Display(tkwin), d,
		    Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC),
		    b.x-1, b.y+borderWidth, b.x-1, b.y+b.height-borderWidth);
		XDrawLine(Tk_Display(tkwin), d,
		    Tk_3DBorderGC(tkwin, border, TK_3D_LIGHT_GC),
		    b.x, b.y+borderWidth, b.x, b.y+b.height-borderWidth);
	    }
	} else {
	    if (b.height > 4) {
		b.y += b.height/2;
		XDrawLine(Tk_Display(tkwin), d,
		    Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC),
		    b.x+borderWidth, b.y-1, b.x+b.width-borderWidth, b.y-1);
		XDrawLine(Tk_Display(tkwin), d,
		    Tk_3DBorderGC(tkwin, border, TK_3D_LIGHT_GC),
		    b.x+borderWidth, b.y, b.x+b.width-borderWidth, b.y);
	    }
	}
    }
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
} TabElement;

static const Ttk_ElementOptionSpec TabElementOptions[] = {
    { "-borderwidth", TK_OPTION_PIXELS,
	offsetof(TabElement,borderWidthObj),"1" },
    { "-background", TK_OPTION_BORDER,
	offsetof(TabElement,backgroundObj), DEFAULT_BACKGROUND },
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
    int borderWidth = 1;
    int cut = 2;
    XPoint pts[6];
    int n = 0;
    (void)dummy;

    Tcl_GetIntFromObj(NULL, tab->borderWidthObj, &borderWidth);

    if (state & TTK_STATE_SELECTED) {
	/*
	 * Draw slightly outside of the allocated parcel,
	 * to overwrite the client area border.
	 */
	b.height += borderWidth;
    }

    pts[n].x = b.x; 			pts[n].y = b.y + b.height - 1; ++n;
    pts[n].x = b.x;			pts[n].y = b.y + cut; ++n;
    pts[n].x = b.x + cut;  		pts[n].y = b.y; ++n;
    pts[n].x = b.x + b.width-1-cut;	pts[n].y = b.y; ++n;
    pts[n].x = b.x + b.width-1; 	pts[n].y = b.y + cut; ++n;
    pts[n].x = b.x + b.width-1; 	pts[n].y = b.y + b.height; ++n;

    XFillPolygon(Tk_Display(tkwin), d,
	Tk_3DBorderGC(tkwin, border, TK_3D_FLAT_GC),
	pts, 6, Convex, CoordModeOrigin);

#ifndef _WIN32
    /*
     * Account for whether XDrawLines draws endpoints by platform
     */
    --pts[5].y;
#endif

    while (borderWidth--) {
	XDrawLines(Tk_Display(tkwin), d,
		Tk_3DBorderGC(tkwin, border, TK_3D_LIGHT_GC),
		pts, 4, CoordModeOrigin);
	XDrawLines(Tk_Display(tkwin), d,
		Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC),
		pts+3, 3, CoordModeOrigin);
	++pts[0].x; ++pts[1].x; ++pts[2].x;             --pts[4].x; --pts[5].x;
	                        ++pts[2].y; ++pts[3].y;
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
	    &CheckbuttonIndicatorElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "Radiobutton.indicator",
	    &RadiobuttonIndicatorElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "Menubutton.indicator",
	    &MenuIndicatorElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "indicator", &ttkNullElementSpec,NULL);

    Ttk_RegisterElement(interp, theme, "uparrow",
	    &ArrowElementSpec, INT2PTR(ARROW_UP));
    Ttk_RegisterElement(interp, theme, "downarrow",
	    &ArrowElementSpec, INT2PTR(ARROW_DOWN));
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
