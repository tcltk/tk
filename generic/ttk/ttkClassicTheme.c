/*
 * Copyright © 2004, Joe English
 *
 * "classic" theme; implements the classic Motif-like Tk look.
 *
 */

#include "tkInt.h"
#include "ttkTheme.h"

#define DEFAULT_BORDERWIDTH "2"
#define DEFAULT_ARROW_SIZE "15"

/*----------------------------------------------------------------------
 * +++ Highlight element implementation.
 *	Draw a solid highlight border to indicate focus.
 */

typedef struct {
    Tcl_Obj	*highlightColorObj;
    Tcl_Obj	*highlightThicknessObj;
    Tcl_Obj	*defaultStateObj;
} HighlightElement;

static const Ttk_ElementOptionSpec HighlightElementOptions[] = {
    { "-highlightcolor",TK_OPTION_COLOR,
	offsetof(HighlightElement,highlightColorObj), DEFAULT_BACKGROUND },
    { "-highlightthickness",TK_OPTION_PIXELS,
	offsetof(HighlightElement,highlightThicknessObj), "0" },
    { "-default", TK_OPTION_ANY,
	offsetof(HighlightElement,defaultStateObj), "disabled" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void HighlightElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    TCL_UNUSED(int *), /* widthPtr */
    TCL_UNUSED(int *), /* heightPtr */
    Ttk_Padding *paddingPtr)
{
    HighlightElement *hl = (HighlightElement *)elementRecord;
    int highlightThickness = 0;

    Tk_GetPixelsFromObj(NULL, tkwin, hl->highlightThicknessObj, &highlightThickness);
    *paddingPtr = Ttk_UniformPadding((short)highlightThickness);
}

static void HighlightElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    TCL_UNUSED(Ttk_Box),
    TCL_UNUSED(Ttk_State))
{
    HighlightElement *hl = (HighlightElement *)elementRecord;
    int highlightThickness = 0;
    XColor *highlightColor = Tk_GetColorFromObj(tkwin, hl->highlightColorObj);
    Ttk_ButtonDefaultState defaultState = TTK_BUTTON_DEFAULT_DISABLED;

    Tk_GetPixelsFromObj(NULL, tkwin, hl->highlightThicknessObj, &highlightThickness);
    if (highlightColor && highlightThickness > 0) {
	Ttk_GetButtonDefaultStateFromObj(NULL, hl->defaultStateObj,
	    &defaultState);
	GC gc = Tk_GCForColor(highlightColor, d);
	if (defaultState == TTK_BUTTON_DEFAULT_NORMAL) {
	    TkDrawInsetFocusHighlight(tkwin, gc, highlightThickness, d,
		round(5 * TkScalingLevel(tkwin)));
	} else {
	    Tk_DrawFocusHighlight(tkwin, gc, highlightThickness, d);
	}
    }
}

static const Ttk_ElementSpec HighlightElementSpec =
{
    TK_STYLE_VERSION_2,
    sizeof(HighlightElement),
    HighlightElementOptions,
    HighlightElementSize,
    HighlightElementDraw
};

/*------------------------------------------------------------------------
 * +++ Button Border element:
 *
 * The Motif-style button border on X11 consists of (from outside-in):
 *
 * + focus indicator (controlled by -highlightcolor and -highlightthickness),
 * + default ring (if -default active; blank if -default normal)
 * + shaded border (controlled by -background, -borderwidth, and -relief)
 */

typedef struct {
    Tcl_Obj	*borderObj;
    Tcl_Obj	*borderWidthObj;
    Tcl_Obj	*reliefObj;
    Tcl_Obj	*defaultStateObj;
} ButtonBorderElement;

static const Ttk_ElementOptionSpec ButtonBorderElementOptions[] =
{
    { "-background", TK_OPTION_BORDER,
	offsetof(ButtonBorderElement,borderObj), DEFAULT_BACKGROUND },
    { "-borderwidth", TK_OPTION_PIXELS,
	offsetof(ButtonBorderElement,borderWidthObj), DEFAULT_BORDERWIDTH },
    { "-relief", TK_OPTION_RELIEF,
	offsetof(ButtonBorderElement,reliefObj), "flat" },
    { "-default", TK_OPTION_ANY,
	offsetof(ButtonBorderElement,defaultStateObj), "disabled" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void ButtonBorderElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    TCL_UNUSED(int *), /* widthPtr */
    TCL_UNUSED(int *), /* heightPtr */
    Ttk_Padding *paddingPtr)
{
    ButtonBorderElement *bd = (ButtonBorderElement *)elementRecord;
    Ttk_ButtonDefaultState defaultState = TTK_BUTTON_DEFAULT_DISABLED;
    int borderWidth = 0;

    Tk_GetPixelsFromObj(NULL, tkwin, bd->borderWidthObj, &borderWidth);
    Ttk_GetButtonDefaultStateFromObj(NULL, bd->defaultStateObj, &defaultState);

    if (defaultState != TTK_BUTTON_DEFAULT_DISABLED) {
	borderWidth += round(5 * TkScalingLevel(tkwin));
    }
    *paddingPtr = Ttk_UniformPadding((short)borderWidth);
}

/*
 * (@@@ Note: ButtonBorderElement still still still buggy:
 * padding for default ring is drawn in the wrong color
 * when the button is active.)
 */
static void ButtonBorderElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    ButtonBorderElement *bd = (ButtonBorderElement *)elementRecord;
    Tk_3DBorder border = NULL;
    int borderWidth = 1, relief = TK_RELIEF_FLAT;
    Ttk_ButtonDefaultState defaultState = TTK_BUTTON_DEFAULT_DISABLED;
    int inset = 0;

    /*
     * Get option values.
     */
    border = Tk_Get3DBorderFromObj(tkwin, bd->borderObj);
    Tk_GetPixelsFromObj(NULL, tkwin, bd->borderWidthObj, &borderWidth);
    Tk_GetReliefFromObj(NULL, bd->reliefObj, &relief);
    Ttk_GetButtonDefaultStateFromObj(NULL, bd->defaultStateObj, &defaultState);

    /*
     * Default ring:
     */
    switch (defaultState)
    {
	case TTK_BUTTON_DEFAULT_DISABLED :
	    break;
	case TTK_BUTTON_DEFAULT_NORMAL :
	    inset += round(5 * TkScalingLevel(tkwin));
	    break;
	case TTK_BUTTON_DEFAULT_ACTIVE :
	    Tk_Draw3DRectangle(tkwin, d, border,
		b.x+inset, b.y+inset, b.width - 2*inset, b.height - 2*inset,
		2, TK_RELIEF_FLAT);
	    inset += 2;
	    Tk_Draw3DRectangle(tkwin, d, border,
		b.x+inset, b.y+inset, b.width - 2*inset, b.height - 2*inset,
		1, TK_RELIEF_SUNKEN);
	    ++inset;
	    Tk_Draw3DRectangle(tkwin, d, border,
		b.x+inset, b.y+inset, b.width - 2*inset, b.height - 2*inset,
		2, TK_RELIEF_FLAT);
	    inset += 2;
	    break;
    }

    /*
     * 3-D border:
     */
    if (border && borderWidth > 0) {
	Tk_Draw3DRectangle(tkwin, d, border,
	    b.x+inset, b.y+inset, b.width - 2*inset, b.height - 2*inset,
	    borderWidth,relief);
    }
}

static const Ttk_ElementSpec ButtonBorderElementSpec =
{
    TK_STYLE_VERSION_2,
    sizeof(ButtonBorderElement),
    ButtonBorderElementOptions,
    ButtonBorderElementSize,
    ButtonBorderElementDraw
};

/*----------------------------------------------------------------------
 * +++ Indicator element.
 *
 * Draws the on/off indicator for checkbuttons and radiobuttons.
 *
 * Draws a 3-D square (or diamond), raised if off, sunken if on.
 *
 * This is actually a regression from Tk 8.5 back to the ugly old Motif
 * style; use the "alt", "clam", or "default" theme" for newer, nicer
 * versions.
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
 * Checkbutton indicators: 3-D square.
 */
static void SquareIndicatorElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    IndicatorElement *indicator = (IndicatorElement *)elementRecord;
    Ttk_Padding margins;
    int diameter = 0;

    Ttk_GetPaddingFromObj(NULL, tkwin, indicator->marginObj, &margins);
    Tk_GetPixelsFromObj(NULL, tkwin, indicator->sizeObj, &diameter);
    *widthPtr = diameter + Ttk_PaddingWidth(margins);
    *heightPtr = diameter + Ttk_PaddingHeight(margins);
}

static void SquareIndicatorElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    IndicatorElement *indicator = (IndicatorElement *)elementRecord;
    Tk_3DBorder border = 0, interior = 0;
    int relief = TK_RELIEF_RAISED;
    Ttk_Padding padding;
    int borderWidth = 2;
    int diameter;

    interior = Tk_Get3DBorderFromObj(tkwin, indicator->colorObj);
    border = Tk_Get3DBorderFromObj(tkwin, indicator->backgroundObj);
    Tk_GetPixelsFromObj(NULL, tkwin, indicator->borderWidthObj,&borderWidth);
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
 * Radiobutton indicators: 3-D diamond.
 */
static void DiamondIndicatorElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    IndicatorElement *indicator = (IndicatorElement *)elementRecord;
    Ttk_Padding margins;
    int diameter = 0;

    Ttk_GetPaddingFromObj(NULL, tkwin, indicator->marginObj, &margins);
    Tk_GetPixelsFromObj(NULL, tkwin, indicator->sizeObj, &diameter);
    *widthPtr = diameter + 3 + Ttk_PaddingWidth(margins);
    *heightPtr = diameter + 3 + Ttk_PaddingHeight(margins);
}

static void DiamondIndicatorElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    IndicatorElement *indicator = (IndicatorElement *)elementRecord;
    Tk_3DBorder border = 0, interior = 0;
    int borderWidth = 2;
    int relief = TK_RELIEF_RAISED;
    int diameter, radius;
    XPoint points[4];
    Ttk_Padding padding;

    interior = Tk_Get3DBorderFromObj(tkwin, indicator->colorObj);
    border = Tk_Get3DBorderFromObj(tkwin, indicator->backgroundObj);
    Tk_GetPixelsFromObj(NULL, tkwin, indicator->borderWidthObj, &borderWidth);
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

/*----------------------------------------------------------------------
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
    { "-indicatorborderwidth", TK_OPTION_PIXELS,
	offsetof(MenuIndicatorElement,borderWidthObj), DEFAULT_BORDERWIDTH },
    { "-indicatorrelief", TK_OPTION_RELIEF,
	offsetof(MenuIndicatorElement,reliefObj), "raised" },
    { "-indicatormargin", TK_OPTION_STRING,
	offsetof(MenuIndicatorElement,marginObj), "5 0" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void MenuIndicatorElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    MenuIndicatorElement *mi = (MenuIndicatorElement *)elementRecord;
    Ttk_Padding margins;

    Tk_GetPixelsFromObj(NULL, tkwin, mi->widthObj, widthPtr);
    Tk_GetPixelsFromObj(NULL, tkwin, mi->heightObj, heightPtr);
    Ttk_GetPaddingFromObj(NULL, tkwin, mi->marginObj, &margins);
    *widthPtr += Ttk_PaddingWidth(margins);
    *heightPtr += Ttk_PaddingHeight(margins);
}

static void MenuIndicatorElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    MenuIndicatorElement *mi = (MenuIndicatorElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, mi->backgroundObj);
    Ttk_Padding margins;
    int borderWidth = 2;

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
 * +++ Arrow element(s).
 *
 * Draws a 3-D shaded triangle.
 * clientData is an enum ArrowDirection pointer.
 */

typedef struct
{
    Tcl_Obj *sizeObj;
    Tcl_Obj *borderObj;
    Tcl_Obj *borderWidthObj;
    Tcl_Obj *reliefObj;
} ArrowElement;

static const Ttk_ElementOptionSpec ArrowElementOptions[] =
{
    { "-arrowsize", TK_OPTION_PIXELS, offsetof(ArrowElement,sizeObj),
	DEFAULT_ARROW_SIZE },
    { "-background", TK_OPTION_BORDER, offsetof(ArrowElement,borderObj),
	DEFAULT_BACKGROUND },
    { "-borderwidth", TK_OPTION_PIXELS, offsetof(ArrowElement,borderWidthObj),
	DEFAULT_BORDERWIDTH },
    { "-relief", TK_OPTION_RELIEF, offsetof(ArrowElement,reliefObj),"raised" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void ArrowElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    ArrowElement *arrow = (ArrowElement *)elementRecord;
    int size = 12;

    Tk_GetPixelsFromObj(NULL, tkwin, arrow->sizeObj, &size);
    *widthPtr = *heightPtr = size;
}

static void ArrowElementDraw(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    ArrowDirection direction = (ArrowDirection)PTR2INT(clientData);
    ArrowElement *arrow = (ArrowElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, arrow->borderObj);
    int borderWidth = 2;
    int relief = TK_RELIEF_RAISED;
    int size = b.width < b.height ? b.width : b.height;
    XPoint points[3];

    Tk_GetPixelsFromObj(NULL, tkwin, arrow->borderWidthObj, &borderWidth);
    Tk_GetReliefFromObj(NULL, arrow->reliefObj, &relief);

    /*
     * @@@ There are off-by-one pixel errors in the way these are drawn;
     * @@@ need to take a look at Tk_Fill3DPolygon and X11 to find the
     * @@@ exact rules.
     */
    switch (direction)
    {
	case ARROW_UP:
	    points[2].x = b.x;		points[2].y = b.y + size;
	    points[1].x = b.x + size/2;	points[1].y = b.y;
	    points[0].x = b.x + size;	points[0].y = b.y + size;
	    break;
	case ARROW_DOWN:
	    points[0].x = b.x;		points[0].y = b.y;
	    points[1].x = b.x + size/2;	points[1].y = b.y + size;
	    points[2].x = b.x + size;	points[2].y = b.y;
	    break;
	case ARROW_LEFT:
	    points[0].x = b.x;		points[0].y = b.y + size / 2;
	    points[1].x = b.x + size;	points[1].y = b.y + size;
	    points[2].x = b.x + size;	points[2].y = b.y;
	    break;
	case ARROW_RIGHT:
	    points[0].x = b.x + size;	points[0].y = b.y + size / 2;
	    points[1].x = b.x;		points[1].y = b.y;
	    points[2].x = b.x;		points[2].y = b.y + size;
	    break;
    }

    Tk_Fill3DPolygon(tkwin, d, border, points, 3, borderWidth, relief);
}

static const Ttk_ElementSpec ArrowElementSpec =
{
    TK_STYLE_VERSION_2,
    sizeof(ArrowElement),
    ArrowElementOptions,
    ArrowElementSize,
    ArrowElementDraw
};

/*------------------------------------------------------------------------
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
    { "-sliderlength", TK_OPTION_PIXELS,
	offsetof(SliderElement,lengthObj), "30" },
    { "-sliderthickness",TK_OPTION_PIXELS,
	offsetof(SliderElement,thicknessObj), "15" },
    { "-sliderrelief", TK_OPTION_RELIEF,
	offsetof(SliderElement,reliefObj), "raised" },
    { "-sliderborderwidth", TK_OPTION_PIXELS,
	offsetof(SliderElement,borderWidthObj), DEFAULT_BORDERWIDTH },
    { "-background", TK_OPTION_BORDER,
	offsetof(SliderElement,borderObj), DEFAULT_BACKGROUND },
    { "-orient", TK_OPTION_ANY,
	offsetof(SliderElement,orientObj), "horizontal" },
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
    int length, thickness;

    Ttk_GetOrientFromObj(NULL, slider->orientObj, &orient);
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
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    SliderElement *slider = (SliderElement *)elementRecord;
    Tk_3DBorder border = NULL;
    int relief = TK_RELIEF_RAISED, borderWidth = 2;
    Ttk_Orient orient;

    border = Tk_Get3DBorderFromObj(tkwin, slider->borderObj);
    Tk_GetReliefFromObj(NULL, slider->reliefObj, &relief);
    Tk_GetPixelsFromObj(NULL, tkwin, slider->borderWidthObj, &borderWidth);
    Ttk_GetOrientFromObj(NULL, slider->orientObj, &orient);

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
 * +++ Sash element (for ttk::panedwindow)
 *
 * NOTES:
 *
 * panedwindows with -orient horizontal use vertical sashes, and vice versa.
 *
 * Interpretation of -sashrelief 'groove' and 'ridge' are
 * swapped wrt. the core panedwindow, which (I think) has them backwards.
 *
 * Default -sashrelief is sunken; the core panedwindow has default
 * -sashrelief raised, but that looks wrong to me.
 */

typedef struct {
    Tcl_Obj *borderObj;	/* background color */
    Tcl_Obj *sashReliefObj;	/* sash relief */
    Tcl_Obj *sashThicknessObj;	/* overall thickness of sash */
    Tcl_Obj *sashPadObj;	/* padding on either side of handle */
    Tcl_Obj *handleSizeObj;	/* handle width and height */
    Tcl_Obj *handlePadObj;	/* handle's distance from edge */
} SashElement;

static const Ttk_ElementOptionSpec SashOptions[] = {
    { "-background", TK_OPTION_BORDER,
	offsetof(SashElement,borderObj), DEFAULT_BACKGROUND },
    { "-sashrelief", TK_OPTION_RELIEF,
	offsetof(SashElement,sashReliefObj), "sunken" },
    { "-sashthickness", TK_OPTION_PIXELS,
	offsetof(SashElement,sashThicknessObj), "6" },
    { "-sashpad", TK_OPTION_PIXELS,
	offsetof(SashElement,sashPadObj), "2" },
    { "-handlesize", TK_OPTION_PIXELS,
	offsetof(SashElement,handleSizeObj), "8" },
    { "-handlepad", TK_OPTION_PIXELS,
	offsetof(SashElement,handlePadObj), "8" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void SashElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    SashElement *sash = (SashElement *)elementRecord;
    int sashPad = 2, sashThickness = 6, handleSize = 8;
    Ttk_Orient orient = (Ttk_Orient)PTR2INT(clientData);

    Tk_GetPixelsFromObj(NULL, tkwin, sash->sashThicknessObj, &sashThickness);
    Tk_GetPixelsFromObj(NULL, tkwin, sash->handleSizeObj, &handleSize);
    Tk_GetPixelsFromObj(NULL, tkwin, sash->sashPadObj, &sashPad);

    if (sashThickness < handleSize + 2*sashPad)
	sashThickness = handleSize + 2*sashPad;

    if (orient == TTK_ORIENT_HORIZONTAL)
	*heightPtr = sashThickness;
    else
	*widthPtr = sashThickness;
}

static void SashElementDraw(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    SashElement *sash = (SashElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, sash->borderObj);
    GC gc1,gc2;
    int relief = TK_RELIEF_RAISED;
    int handleSize = 8, handlePad = 8;
    Ttk_Orient orient = (Ttk_Orient)PTR2INT(clientData);
    Ttk_Box hb;

    Tk_GetPixelsFromObj(NULL, tkwin, sash->handleSizeObj, &handleSize);
    Tk_GetPixelsFromObj(NULL, tkwin, sash->handlePadObj, &handlePad);
    Tk_GetReliefFromObj(NULL, sash->sashReliefObj, &relief);

    switch (relief) {
	case TK_RELIEF_RAISED: case TK_RELIEF_RIDGE:
	    gc1 = Tk_3DBorderGC(tkwin, border, TK_3D_LIGHT_GC);
	    gc2 = Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC);
	    break;
	case TK_RELIEF_SUNKEN: case TK_RELIEF_GROOVE:
	    gc1 = Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC);
	    gc2 = Tk_3DBorderGC(tkwin, border, TK_3D_LIGHT_GC);
	    break;
	case TK_RELIEF_SOLID:
	    gc1 = gc2 = Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC);
	    break;
	case TK_RELIEF_FLAT:
	default:
	    gc1 = gc2 = Tk_3DBorderGC(tkwin, border, TK_3D_FLAT_GC);
	    break;
    }

    /* Draw sash line:
     */
    if (orient == TTK_ORIENT_HORIZONTAL) {
	int y = b.y + b.height/2 - 1;
	XDrawLine(Tk_Display(tkwin), d, gc1, b.x, y, b.x+b.width, y); ++y;
	XDrawLine(Tk_Display(tkwin), d, gc2, b.x, y, b.x+b.width, y);
    } else {
	int x = b.x + b.width/2 - 1;
	XDrawLine(Tk_Display(tkwin), d, gc1, x, b.y, x, b.y+b.height); ++x;
	XDrawLine(Tk_Display(tkwin), d, gc2, x, b.y, x, b.y+b.height);
    }

    /* Draw handle:
     */
    if (handleSize >= 0) {
	if (orient == TTK_ORIENT_HORIZONTAL) {
	    hb = Ttk_StickBox(b, handleSize, handleSize, TTK_STICK_W);
	    hb.x += handlePad;
	} else {
	    hb = Ttk_StickBox(b, handleSize, handleSize, TTK_STICK_N);
	    hb.y += handlePad;
	}
	Tk_Fill3DRectangle(tkwin, d, border,
	    hb.x, hb.y, hb.width, hb.height, 1, TK_RELIEF_RAISED);
    }
}

static const Ttk_ElementSpec SashElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(SashElement),
    SashOptions,
    SashElementSize,
    SashElementDraw
};

/*------------------------------------------------------------------------
 * +++ Widget layouts.
 */

TTK_BEGIN_LAYOUT_TABLE(LayoutTable)

TTK_LAYOUT("TButton",
    TTK_GROUP("Button.highlight", TTK_FILL_BOTH,
	TTK_GROUP("Button.border", TTK_FILL_BOTH|TTK_BORDER,
	    TTK_GROUP("Button.padding", TTK_FILL_BOTH,
		TTK_NODE("Button.label", TTK_FILL_BOTH)))))

TTK_LAYOUT("TCheckbutton",
    TTK_GROUP("Checkbutton.highlight", TTK_FILL_BOTH,
	TTK_GROUP("Checkbutton.border", TTK_FILL_BOTH,
	    TTK_GROUP("Checkbutton.padding", TTK_FILL_BOTH,
		TTK_NODE("Checkbutton.indicator", TTK_PACK_LEFT)
		TTK_NODE("Checkbutton.label", TTK_PACK_LEFT|TTK_FILL_BOTH)))))

TTK_LAYOUT("TRadiobutton",
    TTK_GROUP("Radiobutton.highlight", TTK_FILL_BOTH,
	TTK_GROUP("Radiobutton.border", TTK_FILL_BOTH,
	    TTK_GROUP("Radiobutton.padding", TTK_FILL_BOTH,
		TTK_NODE("Radiobutton.indicator", TTK_PACK_LEFT)
		TTK_NODE("Radiobutton.label", TTK_PACK_LEFT|TTK_FILL_BOTH)))))

TTK_LAYOUT("TMenubutton",
    TTK_GROUP("Menubutton.highlight", TTK_FILL_BOTH,
	TTK_GROUP("Menubutton.border", TTK_FILL_BOTH,
	    TTK_NODE("Menubutton.indicator", TTK_PACK_RIGHT)
	    TTK_GROUP("Menubutton.padding", TTK_FILL_X,
		TTK_NODE("Menubutton.label", 0)))))

/* "classic" entry, includes highlight border */
TTK_LAYOUT("TEntry",
    TTK_GROUP("Entry.highlight", TTK_FILL_BOTH,
	TTK_GROUP("Entry.field", TTK_FILL_BOTH|TTK_BORDER,
	    TTK_GROUP("Entry.padding", TTK_FILL_BOTH,
		TTK_NODE("Entry.textarea", TTK_FILL_BOTH)))))

/* "classic" combobox, includes highlight border */
TTK_LAYOUT("TCombobox",
    TTK_GROUP("Combobox.highlight", TTK_FILL_BOTH,
	TTK_GROUP("Combobox.field", TTK_FILL_BOTH,
	    TTK_NODE("Combobox.downarrow", TTK_PACK_RIGHT|TTK_FILL_Y)
	    TTK_GROUP("Combobox.padding", TTK_FILL_BOTH,
		TTK_NODE("Combobox.textarea", TTK_FILL_BOTH)))))

/* "classic" spinbox, includes highlight border */
TTK_LAYOUT("TSpinbox",
    TTK_GROUP("Spinbox.highlight", TTK_FILL_BOTH,
	TTK_GROUP("Spinbox.field", TTK_FILL_BOTH|TTK_FILL_X,
	    TTK_GROUP("null", TTK_PACK_RIGHT,
		TTK_NODE("Spinbox.uparrow", TTK_PACK_TOP|TTK_STICK_E)
		TTK_NODE("Spinbox.downarrow", TTK_PACK_BOTTOM|TTK_STICK_E))
	    TTK_GROUP("Spinbox.padding", TTK_FILL_BOTH,
		TTK_NODE("Spinbox.textarea", TTK_FILL_BOTH)))))

/* "classic" scale, includes highlight border */
TTK_LAYOUT("Horizontal.TScale",
    TTK_GROUP("Horizontal.Scale.highlight", TTK_FILL_BOTH,
	TTK_GROUP("Horizontal.Scale.trough", TTK_FILL_BOTH,
	    TTK_NODE("Horizontal.Scale.slider", TTK_PACK_LEFT))))

TTK_LAYOUT("Vertical.TScale",
    TTK_GROUP("Vertical.Scale.highlight", TTK_FILL_BOTH,
	TTK_GROUP("Vertical.Scale.trough", TTK_FILL_BOTH,
	    TTK_NODE("Vertical.Scale.slider", TTK_PACK_TOP))))

TTK_LAYOUT("Horizontal.Sash",
    TTK_NODE("Sash.hsash", TTK_FILL_X))

TTK_LAYOUT("Vertical.Sash",
    TTK_NODE("Sash.vsash", TTK_FILL_Y))

/* put highlight border around treeview */
TTK_LAYOUT("Treeview",
    TTK_GROUP("Treeview.highlight", TTK_FILL_BOTH,
	TTK_GROUP("Treeview.field", TTK_FILL_BOTH|TTK_BORDER,
	    TTK_GROUP("Treeview.padding", TTK_FILL_BOTH,
		TTK_NODE("Treeview.treearea", TTK_FILL_BOTH)))))

TTK_END_LAYOUT_TABLE

/*------------------------------------------------------------------------
 * TtkClassicTheme_Init --
 *	Install classic theme.
 */

MODULE_SCOPE int
TtkClassicTheme_Init(Tcl_Interp *interp)
{
    Ttk_Theme theme =  Ttk_CreateTheme(interp, "classic", NULL);

    if (!theme) {
	return TCL_ERROR;
    }

    /*
     * Register elements:
     */
    Ttk_RegisterElement(interp, theme, "highlight",
	    &HighlightElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "Button.border",
	    &ButtonBorderElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "Checkbutton.indicator",
	    &CheckbuttonIndicatorElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "Radiobutton.indicator",
	    &RadiobuttonIndicatorElementSpec, NULL);
    Ttk_RegisterElement(interp, theme, "Menubutton.indicator",
	    &MenuIndicatorElementSpec, NULL);

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

    Ttk_RegisterElement(interp, theme, "slider",
	    &SliderElementSpec, NULL);

    Ttk_RegisterElement(interp, theme, "hsash",
	    &SashElementSpec, INT2PTR(TTK_ORIENT_HORIZONTAL));
    Ttk_RegisterElement(interp, theme, "vsash",
	    &SashElementSpec, INT2PTR(TTK_ORIENT_VERTICAL));

    /*
     * Register layouts:
     */
    Ttk_RegisterLayouts(theme, LayoutTable);

    Tcl_PkgProvide(interp, "ttk::theme::classic", TTK_VERSION);

    return TCL_OK;
}

/*EOF*/
