/*
 * Tk theme engine which uses the Windows "Visual Styles" API
 * Adapted from Georgios Petasis' XP theme patch.
 *
 * Copyright © 2003 Georgios Petasis, petasis@iit.demokritos.gr.
 * Copyright © 2003 Joe English
 * Copyright © 2003 Pat Thoyts
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * See also:
 *
 * <URL: http://msdn.microsoft.com/library/en-us/
 *	shellcc/platform/commctls/userex/refentry.asp >
 */

#include "tkWinInt.h"
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>
#include "ttk/ttkThemeInt.h"
#ifdef _MSC_VER
#   pragma comment (lib, "uxtheme.lib")
#endif

/*
 * VistaThemeDeleteProc --
 *
 *      Release any theme allocated resources.
 */

static void
VistaThemeDeleteProc(
    TCL_UNUSED(void *))
{
}

static int
VistaThemeEnabled(
    TCL_UNUSED(Ttk_Theme),
    TCL_UNUSED(void *))
{
    int active = IsThemeActive();
    int themed = IsAppThemed();

    return (active && themed);
}

/*
 * BoxToRect --
 *	Helper routine.  Returns a RECT data structure.
 */
static RECT
BoxToRect(Ttk_Box b)
{
    RECT rc;
    rc.top = b.y;
    rc.left = b.x;
    rc.bottom = b.y + b.height;
    rc.right = b.x + b.width;
    return rc;
}

/*
 * Map Tk state bitmaps to Vista style enumerated values.
 */
static const Ttk_StateTable null_statemap[] = { {0,0,0} };

/*
 * Pushbuttons (Tk: "Button")
 */
static const Ttk_StateTable pushbutton_statemap[] =
{
    { PBS_DISABLED,	TTK_STATE_DISABLED, 0 },
    { PBS_PRESSED,	TTK_STATE_PRESSED, 0 },
    { PBS_HOT,		TTK_STATE_ACTIVE, 0 },
    { PBS_DEFAULTED,	TTK_STATE_ALTERNATE, 0 },
    { PBS_NORMAL,	0, 0 }
};

/*
 * Checkboxes (Tk: "Checkbutton")
 */
static const Ttk_StateTable checkbox_statemap[] =
{
{CBS_MIXEDDISABLED,	TTK_STATE_ALTERNATE|TTK_STATE_DISABLED, 0},
{CBS_MIXEDPRESSED,	TTK_STATE_ALTERNATE|TTK_STATE_PRESSED, 0},
{CBS_MIXEDHOT,	TTK_STATE_ALTERNATE|TTK_STATE_ACTIVE, 0},
{CBS_MIXEDNORMAL,	TTK_STATE_ALTERNATE, 0},
{CBS_CHECKEDDISABLED,	TTK_STATE_SELECTED|TTK_STATE_DISABLED, 0},
{CBS_CHECKEDPRESSED,	TTK_STATE_SELECTED|TTK_STATE_PRESSED, 0},
{CBS_CHECKEDHOT,	TTK_STATE_SELECTED|TTK_STATE_ACTIVE, 0},
{CBS_CHECKEDNORMAL,	TTK_STATE_SELECTED, 0},
{CBS_UNCHECKEDDISABLED,	TTK_STATE_DISABLED, 0},
{CBS_UNCHECKEDPRESSED,	TTK_STATE_PRESSED, 0},
{CBS_UNCHECKEDHOT,	TTK_STATE_ACTIVE, 0},
{CBS_UNCHECKEDNORMAL,	0,0 }
};

/*
 * Radiobuttons:
 */
static const Ttk_StateTable radiobutton_statemap[] =
{
{RBS_UNCHECKEDDISABLED,	TTK_STATE_ALTERNATE|TTK_STATE_DISABLED, 0},
{RBS_UNCHECKEDNORMAL,	TTK_STATE_ALTERNATE, 0},
{RBS_CHECKEDDISABLED,	TTK_STATE_SELECTED|TTK_STATE_DISABLED, 0},
{RBS_CHECKEDPRESSED,	TTK_STATE_SELECTED|TTK_STATE_PRESSED, 0},
{RBS_CHECKEDHOT,	TTK_STATE_SELECTED|TTK_STATE_ACTIVE, 0},
{RBS_CHECKEDNORMAL,	TTK_STATE_SELECTED, 0},
{RBS_UNCHECKEDDISABLED,	TTK_STATE_DISABLED, 0},
{RBS_UNCHECKEDPRESSED,	TTK_STATE_PRESSED, 0},
{RBS_UNCHECKEDHOT,	TTK_STATE_ACTIVE, 0},
{RBS_UNCHECKEDNORMAL,	0,0 }
};

/*
 * Groupboxes (tk: "frame")
 */
static const Ttk_StateTable groupbox_statemap[] =
{
{GBS_DISABLED,	TTK_STATE_DISABLED, 0},
{GBS_NORMAL,	0,0 }
};

/*
 * Edit fields (tk: "entry")
 */
static const Ttk_StateTable edittext_statemap[] =
{
    { ETS_DISABLED,	TTK_STATE_DISABLED, 0 },
    { ETS_READONLY,	TTK_STATE_READONLY, 0 },
    { ETS_FOCUSED,	TTK_STATE_FOCUS, 0 },
    { ETS_HOT,		TTK_STATE_ACTIVE, 0 },
    { ETS_NORMAL,	0, 0 }
/* NOT USED: ETS_ASSIST, ETS_SELECTED */
};

/*
 * Combobox text field statemap:
 * Same as edittext_statemap, but doesn't use ETS_READONLY
 * (fixes: #1032409)
 */
static const Ttk_StateTable combotext_statemap[] =
{
    { ETS_DISABLED,	TTK_STATE_DISABLED, 0 },
    { ETS_FOCUSED,	TTK_STATE_FOCUS, 0 },
    { ETS_HOT,		TTK_STATE_ACTIVE, 0 },
    { ETS_NORMAL,	0, 0 }
};

/*
 * Combobox button: (CBP_DROPDOWNBUTTON)
 */
static const Ttk_StateTable combobox_statemap[] = {
    { CBXS_DISABLED,	TTK_STATE_DISABLED, 0 },
    { CBXS_PRESSED,	TTK_STATE_PRESSED, 0 },
    { CBXS_HOT,	TTK_STATE_ACTIVE, 0 },
    { CBXS_HOT,	TTK_STATE_HOVER, 0 },
    { CBXS_NORMAL,	0, 0 }
};

/*
 * Toolbar buttons (TP_BUTTON):
 */
static const Ttk_StateTable toolbutton_statemap[] =  {
    { TS_DISABLED,	TTK_STATE_DISABLED, 0 },
    { TS_PRESSED,	TTK_STATE_PRESSED, 0 },
    { TS_HOTCHECKED,	TTK_STATE_SELECTED|TTK_STATE_ACTIVE, 0 },
    { TS_CHECKED,	TTK_STATE_SELECTED, 0 },
    { TS_HOT,		TTK_STATE_ACTIVE, 0 },
    { TS_NORMAL,	0,0 }
};

/*
 * Scrollbars (Tk: "Scrollbar.thumb")
 */
static const Ttk_StateTable scrollbar_statemap[] =
{
    { SCRBS_DISABLED,	TTK_STATE_DISABLED, 0 },
    { SCRBS_PRESSED,	TTK_STATE_PRESSED, 0 },
    { SCRBS_HOT,	TTK_STATE_ACTIVE, 0 },
    { SCRBS_NORMAL,	0, 0 }
};

static const Ttk_StateTable uparrow_statemap[] =
{
    { ABS_UPDISABLED,	TTK_STATE_DISABLED, 0 },
    { ABS_UPPRESSED,	TTK_STATE_PRESSED, 0 },
    { ABS_UPHOT,	TTK_STATE_ACTIVE, 0 },
    { ABS_UPNORMAL,	0, 0 }
};

static const Ttk_StateTable downarrow_statemap[] =
{
    { ABS_DOWNDISABLED,	TTK_STATE_DISABLED, 0 },
    { ABS_DOWNPRESSED,	TTK_STATE_PRESSED, 0 },
    { ABS_DOWNHOT,	TTK_STATE_ACTIVE, 0 },
    { ABS_DOWNNORMAL,	0, 0 }
};

static const Ttk_StateTable leftarrow_statemap[] =
{
    { ABS_LEFTDISABLED,	TTK_STATE_DISABLED, 0 },
    { ABS_LEFTPRESSED,	TTK_STATE_PRESSED, 0 },
    { ABS_LEFTHOT,	TTK_STATE_ACTIVE, 0 },
    { ABS_LEFTNORMAL,	0, 0 }
};

static const Ttk_StateTable rightarrow_statemap[] =
{
    { ABS_RIGHTDISABLED,TTK_STATE_DISABLED, 0 },
    { ABS_RIGHTPRESSED, TTK_STATE_PRESSED, 0 },
    { ABS_RIGHTHOT,	TTK_STATE_ACTIVE, 0 },
    { ABS_RIGHTNORMAL,	0, 0 }
};

static const Ttk_StateTable spinbutton_statemap[] =
{
    { DNS_DISABLED,	TTK_STATE_DISABLED, 0 },
    { DNS_PRESSED,	TTK_STATE_PRESSED,  0 },
    { DNS_HOT,		TTK_STATE_ACTIVE,   0 },
    { DNS_NORMAL,	0,		    0 },
};

/*
 * Trackbar thumb: (Tk: "scale slider")
 */
static const Ttk_StateTable scale_statemap[] =
{
    { TUS_DISABLED,	TTK_STATE_DISABLED, 0 },
    { TUS_PRESSED,	TTK_STATE_PRESSED, 0 },
    { TUS_FOCUSED,	TTK_STATE_FOCUS, 0 },
    { TUS_HOT,		TTK_STATE_ACTIVE, 0 },
    { TUS_NORMAL,	0, 0 }
};

static const Ttk_StateTable tabitem_statemap[] =
{
    { TIS_DISABLED,     TTK_STATE_DISABLED, 0 },
    { TIS_SELECTED,     TTK_STATE_SELECTED, 0 },
    { TIS_HOT,          TTK_STATE_ACTIVE,   0 },
    { TIS_FOCUSED,      TTK_STATE_FOCUS,    0 },
    { TIS_NORMAL,       0,                  0 },
};


/*
 *----------------------------------------------------------------------
 * +++ Element data:
 *
 * The following structure is passed as the 'clientData' pointer
 * to most elements in this theme.  It contains data relevant
 * to a single Vista Theme "part".
 *
 * <<NOTE-GetThemeMargins>>:
 *	In theory, we should be call GetThemeMargins(...TMT_CONTENTRECT...)
 *	to calculate the internal padding.  In practice, this routine
 *	only seems to work properly for BP_PUSHBUTTON.  So we hardcode
 *	the required padding at element registration time instead.
 *
 *	The PAD_MARGINS flag bit determines whether the padding
 *	should be added on the inside (0) or outside (1) of the element.
 *
 * <<NOTE-GetThemePartSize>>:
 *	This gives bogus metrics for some parts (in particular,
 *	BP_PUSHBUTTONS).  Set the IGNORE_THEMESIZE flag to skip this call.
 */

typedef struct	/* Vista element specifications */
{
    const char	*elementName;	/* Tk theme engine element name */
    const Ttk_ElementSpec *elementSpec;
				/* Element spec (usually GenericElementSpec) */
    LPCWSTR	className;	/* Windows window class name */
    int	partId;		/* BP_PUSHBUTTON, BP_CHECKBUTTON, etc. */
    const Ttk_StateTable *statemap;	/* Map Tk states to Vista states */
    Ttk_Padding	padding;	/* See NOTE-GetThemeMargins */
    unsigned	flags;
#   define	IGNORE_THEMESIZE 0x80000000U /* See NOTE-GetThemePartSize */
#   define	PAD_MARGINS	 0x40000000U /* See NOTE-GetThemeMargins */
#   define	HEAP_ELEMENT	 0x20000000U /* ElementInfo is on heap */
#   define	HALF_HEIGHT	 0x10000000U /* Used by GenericSizedElements */
#   define	HALF_WIDTH	 0x08000000U /* Used by GenericSizedElements */
} ElementInfo;

typedef struct
{
    /*
     * Static data, initialized when element is registered:
     */
    const ElementInfo	*info;
    HWND parentHwnd;

    /*
     * Dynamic data, allocated by InitElementData:
     */
    HTHEME	hTheme;
    HDC		hDC;
    HWND	hwnd;

    /* For TkWinDrawableReleaseDC: */
    Drawable	drawable;
    TkWinDCState dcState;
} ElementData;

static ElementData *
NewElementData(HWND hwnd, const ElementInfo *info)
{
    ElementData *elementData = (ElementData *)Tcl_Alloc(sizeof(ElementData));

    elementData->parentHwnd = hwnd;
    elementData->info = info;
    elementData->hTheme = elementData->hDC = 0;

    return elementData;
}

/*
 * Destroy elements. If the element was created by the element factory
 * then the info member is dynamically allocated. Otherwise it was
 * static data from the C object and only the ElementData needs freeing.
 */
static void DestroyElementData(void *clientData)
{
    ElementData *elementData = (ElementData *)clientData;
    if (elementData->info->flags & HEAP_ELEMENT) {
	Tcl_Free((void *)elementData->info->statemap);
	Tcl_Free((void *)elementData->info->className);
	Tcl_Free((void *)elementData->info->elementName);
	Tcl_Free((void *)elementData->info);
    }
    Tcl_Free(clientData);
}

/*
 * InitElementData --
 *	Looks up theme handle.  If Drawable argument is non-NULL,
 *	also initializes DC.
 *
 * Returns:
 *	1 on success, 0 on error.
 *	Caller must later call FreeElementData() so this element
 *	can be reused.
 */

static int
InitElementData(ElementData *elementData, Tk_Window tkwin, Drawable d)
{
    Window win = Tk_WindowId(tkwin);

    if (win) {
	elementData->hwnd = Tk_GetHWND(win);
    } else  {
	elementData->hwnd = elementData->parentHwnd;
    }

    elementData->hTheme = OpenThemeData(
	    elementData->hwnd, elementData->info->className);

    if (!elementData->hTheme) {
	return 0;
    }

    elementData->drawable = d;
    if (d != 0) {
	elementData->hDC = TkWinGetDrawableDC(Tk_Display(tkwin), d,
		&elementData->dcState);
    }

    return 1;
}

static void
FreeElementData(ElementData *elementData)
{
    CloseThemeData(elementData->hTheme);
    if (elementData->drawable != 0) {
	TkWinReleaseDrawableDC(
	    elementData->drawable, elementData->hDC, &elementData->dcState);
    }
}

/*----------------------------------------------------------------------
 * +++ Generic element implementation.
 *
 * Used for elements which are handled entirely by the Vista Theme API,
 * such as radiobutton and checkbutton indicators, scrollbar arrows, etc.
 */

static void GenericElementSize(
    void *clientData,
    TCL_UNUSED(void *), /* elementRecord */
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    Ttk_Padding *paddingPtr)
{
    ElementData *elementData = (ElementData *)clientData;
    HRESULT result;
    SIZE size;

    if (!InitElementData(elementData, tkwin, 0)) {
	return;
    }

    if (!(elementData->info->flags & IGNORE_THEMESIZE)) {
	result = GetThemePartSize(
	    elementData->hTheme,
	    NULL,
	    elementData->info->partId,
	    Ttk_StateTableLookup(elementData->info->statemap, 0),
	    NULL /*RECT *prc*/,
	    TS_TRUE,
	    &size);

	if (SUCCEEDED(result)) {
	    *widthPtr = size.cx;
	    *heightPtr = size.cy;
	}
    }

    /* See NOTE-GetThemeMargins
     */
    *paddingPtr = elementData->info->padding;
    if (elementData->info->flags & PAD_MARGINS) {
	*widthPtr += Ttk_PaddingWidth(elementData->info->padding);
	*heightPtr += Ttk_PaddingHeight(elementData->info->padding);
    }
}

static void GenericElementDraw(
    void *clientData,
    TCL_UNUSED(void *), /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    ElementData *elementData = (ElementData *)clientData;
    RECT rc;

    if (!InitElementData(elementData, tkwin, d)) {
	return;
    }

    if (elementData->info->flags & PAD_MARGINS) {
	b = Ttk_PadBox(b, elementData->info->padding);
    }
    rc = BoxToRect(b);

    DrawThemeBackground(
	elementData->hTheme,
	elementData->hDC,
	elementData->info->partId,
	Ttk_StateTableLookup(elementData->info->statemap, state),
	&rc,
	NULL/*pContentRect*/);

    FreeElementData(elementData);
}

static const Ttk_ElementSpec GenericElementSpec =
{
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    GenericElementSize,
    GenericElementDraw
};

/*----------------------------------------------------------------------
 * +++ Sized element implementation.
 *
 * Used for elements which are handled entirely by the Vista Theme API,
 * but that require a fixed size adjustment.
 * Note that GetThemeSysSize calls through to GetSystemMetrics
 */

static void
GenericSizedElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    ElementData *elementData = (ElementData *)clientData;

    if (!InitElementData(elementData, tkwin, 0)) {
	return;
    }

    GenericElementSize(clientData, elementRecord, tkwin,
	widthPtr, heightPtr, paddingPtr);

    *widthPtr = GetThemeSysSize(NULL,
	(elementData->info->flags >> 8) & 0xff);
    *heightPtr = GetThemeSysSize(NULL,
	elementData->info->flags & 0xff);
    if (elementData->info->flags & HALF_HEIGHT) {
	*heightPtr /= 2;
    }
    if (elementData->info->flags & HALF_WIDTH) {
	*widthPtr /= 2;
    }
}

static const Ttk_ElementSpec GenericSizedElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    GenericSizedElementSize,
    GenericElementDraw
};

/*----------------------------------------------------------------------
 * +++ Spinbox arrow element.
 *     These are half-height scrollbar buttons.
 */

static void
SpinboxArrowElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    ElementData *elementData = (ElementData *)clientData;

    if (!InitElementData(elementData, tkwin, 0)) {
	return;
    }

    GenericSizedElementSize(clientData, elementRecord, tkwin,
	widthPtr, heightPtr, paddingPtr);

    /* force the arrow button height to half size */
    *heightPtr /= 2;
}

static const Ttk_ElementSpec SpinboxArrowElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    SpinboxArrowElementSize,
    GenericElementDraw
};

/*----------------------------------------------------------------------
 * +++ Scrollbar thumb element.
 *     Same as a GenericElement, but don't draw in the disabled state.
 */

static void ThumbElementDraw(
    void *clientData,
    TCL_UNUSED(void *), /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    ElementData *elementData = (ElementData *)clientData;
    int stateId = Ttk_StateTableLookup(elementData->info->statemap, state);
    RECT rc = BoxToRect(b);

    /*
     * Don't draw the thumb if we are disabled.
     */
    if (state & TTK_STATE_DISABLED) {
	return;
    }

    if (!InitElementData(elementData, tkwin, d)) {
	return;
    }

    DrawThemeBackground(elementData->hTheme,
	elementData->hDC, elementData->info->partId, stateId,
	&rc, NULL);

    FreeElementData(elementData);
}

static const Ttk_ElementSpec ThumbElementSpec =
{
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    GenericElementSize,
    ThumbElementDraw
};

/*----------------------------------------------------------------------
 * +++ Progress bar element.
 *	Increases the requested length of PP_CHUNK and PP_CHUNKVERT parts
 *	so that indeterminate progress bars show 3 bars instead of 1.
 */

static void PbarElementSize(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    ElementData *elementData = (ElementData *)clientData;
    int nBars = 3;

    GenericElementSize(clientData, elementRecord, tkwin,
	widthPtr, heightPtr, paddingPtr);

    if (elementData->info->partId == PP_CHUNK) {
	*widthPtr *= nBars;
    } else if (elementData->info->partId == PP_CHUNKVERT) {
	*heightPtr *= nBars;
    }
}

static const Ttk_ElementSpec PbarElementSpec =
{
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    PbarElementSize,
    GenericElementDraw
};

/*----------------------------------------------------------------------
 * +++  Notebook tab element.
 *	Same as generic element, with additional logic to select
 *	proper iPartID for the leftmost tab.
 *
 *	Notes: TABP_TABITEMRIGHTEDGE (or TABP_TOPTABITEMRIGHTEDGE,
 *	which appears to be identical) should be used if the
 *	tab is exactly at the right edge of the notebook, but
 *	not if it's simply the rightmost tab.  This information
 *	is not available.
 *
 *	The TIS_* and TILES_* definitions are identical, so
 *	we can use the same statemap no matter what the partId.
 */

static void TabElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    Ttk_Padding *paddingPtr)
{
    Ttk_PositionSpec nbTabsStickBit = TTK_STICK_S;
    TkMainInfo *mainInfoPtr = ((TkWindow *) tkwin)->mainPtr;

    if (mainInfoPtr != NULL) {
	nbTabsStickBit = (Ttk_PositionSpec) mainInfoPtr->ttkNbTabsStickBit;
    }

    GenericElementSize(clientData, elementRecord, tkwin,
	    widthPtr, heightPtr, paddingPtr);

    *paddingPtr = Ttk_UniformPadding(3);
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
    void *clientData,
    TCL_UNUSED(void *), /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    Ttk_PositionSpec nbTabsStickBit = TTK_STICK_S;
    TkMainInfo *mainInfoPtr = ((TkWindow *) tkwin)->mainPtr;
    ElementData *elementData = (ElementData *)clientData;
    int partId = elementData->info->partId;
    int isSelected = (state & TTK_STATE_SELECTED);
    int stateId = Ttk_StateTableLookup(elementData->info->statemap, state);

    if (mainInfoPtr != NULL) {
	nbTabsStickBit = (Ttk_PositionSpec) mainInfoPtr->ttkNbTabsStickBit;
    }

    /*
     * Correct the members of b if needed
     */
    switch (nbTabsStickBit) {
	default:
	case TTK_STICK_S:
	    break;
	case TTK_STICK_N:
	    b.y -= isSelected ? 0 : 1; b.height -= isSelected ? 1 : 0;
	    break;
	case TTK_STICK_E:
	    b.width -= isSelected ? 1 : 0;
	    break;
	case TTK_STICK_W:
	    b.x -= isSelected ? 1 : 2; b.width -= isSelected ? 1 : 0;
	    break;
    }

    RECT rc = BoxToRect(b);

    if (!InitElementData(elementData, tkwin, d)) {
	return;
    }

    if (nbTabsStickBit == TTK_STICK_S) {
	if (state & TTK_STATE_FIRST) {
	    partId = TABP_TABITEMLEFTEDGE;
	}

	/*
	 * Draw the border and fill into rc
	 */
	DrawThemeBackground(
	    elementData->hTheme, elementData->hDC, partId, stateId, &rc, NULL);
    } else {
	/*
	 * Draw the fill but no border into rc
	 */
	RECT rc2 = rc;
	--rc2.top; --rc2.left; ++rc2.bottom; ++rc2.right;
	DrawThemeBackground(
	    elementData->hTheme, elementData->hDC, partId, stateId, &rc2, &rc);
    }

    /*
     * Draw a flat border at 3 edges
     */
    switch (nbTabsStickBit) {
	default:
	case TTK_STICK_S:
	    break;
	case TTK_STICK_N:
	    DrawThemeEdge(
		elementData->hTheme, elementData->hDC, partId, stateId, &rc,
		BDR_RAISEDINNER, BF_FLAT|BF_LEFT|BF_RIGHT|BF_BOTTOM, NULL);
	    break;
	case TTK_STICK_E:
	    DrawThemeEdge(
		elementData->hTheme, elementData->hDC, partId, stateId, &rc,
		BDR_RAISEDINNER, BF_FLAT|BF_LEFT|BF_TOP|BF_BOTTOM, NULL);
	    break;
	case TTK_STICK_W:
	    DrawThemeEdge(
		elementData->hTheme, elementData->hDC, partId, stateId, &rc,
		BDR_RAISEDINNER, BF_FLAT|BF_TOP|BF_RIGHT|BF_BOTTOM, NULL);
	    break;
    }

    FreeElementData(elementData);
}

static const Ttk_ElementSpec TabElementSpec =
{
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    TabElementSize,
    TabElementDraw
};

/*----------------------------------------------------------------------
 * +++  Tree indicator element.
 *
 *	Generic element, but don't display at all if TTK_STATE_LEAF (=USER2) set
 */

static const Ttk_StateTable header_statemap[] =
{
    { HIS_PRESSED,	TTK_STATE_PRESSED, 0 },
    { HIS_HOT,	TTK_STATE_ACTIVE, 0 },
    { HIS_NORMAL,	0,0 },
};

static const Ttk_StateTable treeview_statemap[] =
{
    { TREIS_DISABLED,	TTK_STATE_DISABLED, 0 },
    { TREIS_SELECTED,	TTK_STATE_SELECTED, 0},
    { TREIS_HOT,	TTK_STATE_ACTIVE, 0 },
    { TREIS_NORMAL,	0,0 },
};

static const Ttk_StateTable tvpglyph_statemap[] =
{
    { GLPS_OPENED,	TTK_STATE_OPEN, 0 },
    { GLPS_CLOSED,	0,0 },
};

static void TreeIndicatorElementDraw(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, Ttk_State state)
{
    if (!(state & TTK_STATE_LEAF)) {
	GenericElementDraw(clientData,elementRecord,tkwin,d,b,state);
    }
}

static const Ttk_ElementSpec TreeIndicatorElementSpec =
{
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    GenericElementSize,
    TreeIndicatorElementDraw
};

/*----------------------------------------------------------------------
 * +++ Widget layouts:
 */

TTK_BEGIN_LAYOUT_TABLE(LayoutTable)

TTK_LAYOUT("TButton",
    TTK_GROUP("Button.button", TTK_FILL_BOTH,
	TTK_GROUP("Button.focus", TTK_FILL_BOTH,
	    TTK_GROUP("Button.padding", TTK_FILL_BOTH,
		TTK_NODE("Button.label", TTK_FILL_BOTH)))))

TTK_LAYOUT("TMenubutton",
    TTK_NODE("Menubutton.dropdown", TTK_PACK_RIGHT|TTK_FILL_Y)
    TTK_GROUP("Menubutton.button", TTK_FILL_BOTH,
	    TTK_GROUP("Menubutton.padding", TTK_FILL_X,
		TTK_NODE("Menubutton.label", 0))))

TTK_LAYOUT("Horizontal.TScrollbar",
    TTK_GROUP("Horizontal.Scrollbar.trough", TTK_FILL_X,
	TTK_NODE("Horizontal.Scrollbar.leftarrow", TTK_PACK_LEFT)
	TTK_NODE("Horizontal.Scrollbar.rightarrow", TTK_PACK_RIGHT)
	TTK_GROUP("Horizontal.Scrollbar.thumb", TTK_FILL_BOTH|TTK_UNIT,
	    TTK_NODE("Horizontal.Scrollbar.grip", 0))))

TTK_LAYOUT("Vertical.TScrollbar",
    TTK_GROUP("Vertical.Scrollbar.trough", TTK_FILL_Y,
	TTK_NODE("Vertical.Scrollbar.uparrow", TTK_PACK_TOP)
	TTK_NODE("Vertical.Scrollbar.downarrow", TTK_PACK_BOTTOM)
	TTK_GROUP("Vertical.Scrollbar.thumb", TTK_FILL_BOTH|TTK_UNIT,
	    TTK_NODE("Vertical.Scrollbar.grip", 0))))

TTK_LAYOUT("Horizontal.TScale",
    TTK_GROUP("Scale.focus", TTK_FILL_BOTH,
	TTK_GROUP("Horizontal.Scale.trough", TTK_FILL_BOTH,
	    TTK_NODE("Horizontal.Scale.track", TTK_FILL_X)
	    TTK_NODE("Horizontal.Scale.slider", TTK_PACK_LEFT) )))

TTK_LAYOUT("Vertical.TScale",
    TTK_GROUP("Scale.focus", TTK_FILL_BOTH,
	TTK_GROUP("Vertical.Scale.trough", TTK_FILL_BOTH,
	    TTK_NODE("Vertical.Scale.track", TTK_FILL_Y)
	    TTK_NODE("Vertical.Scale.slider", TTK_PACK_TOP) )))

TTK_END_LAYOUT_TABLE

/*----------------------------------------------------------------------
 * +++ Vista element info table:
 */

#define PAD(l,t,r,b) {l,t,r,b}
#define NOPAD {0,0,0,0}

/* name spec className partId statemap padding flags */

static const ElementInfo ElementInfoTable[] = {
    { "Checkbutton.indicator", &GenericElementSpec, L"BUTTON",
	BP_CHECKBOX, checkbox_statemap, PAD(0, 0, 4, 0), PAD_MARGINS },
    { "Radiobutton.indicator", &GenericElementSpec, L"BUTTON",
	BP_RADIOBUTTON, radiobutton_statemap, PAD(0, 0, 4, 0), PAD_MARGINS },
    { "Button.button", &GenericElementSpec, L"BUTTON",
	BP_PUSHBUTTON, pushbutton_statemap, PAD(3, 3, 3, 3), IGNORE_THEMESIZE },
    { "Labelframe.border", &GenericElementSpec, L"BUTTON",
	BP_GROUPBOX, groupbox_statemap, PAD(2, 2, 2, 2), 0 },
    { "Entry.field", &GenericElementSpec, L"EDIT", EP_EDITTEXT,
	edittext_statemap, PAD(1, 1, 1, 1), 0 },
    { "Combobox.field", &GenericElementSpec, L"EDIT",
	EP_EDITTEXT, combotext_statemap, PAD(1, 1, 1, 1), 0 },
    { "Combobox.downarrow", &GenericSizedElementSpec, L"COMBOBOX",
	CP_DROPDOWNBUTTON, combobox_statemap, NOPAD,
	(SM_CXVSCROLL << 8) | SM_CYVSCROLL },
    { "Vertical.Scrollbar.trough", &GenericElementSpec, L"SCROLLBAR",
	SBP_UPPERTRACKVERT, scrollbar_statemap, NOPAD, 0 },
    { "Vertical.Scrollbar.thumb", &ThumbElementSpec, L"SCROLLBAR",
	SBP_THUMBBTNVERT, scrollbar_statemap, NOPAD, 0 },
    { "Vertical.Scrollbar.grip", &GenericElementSpec, L"SCROLLBAR",
	SBP_GRIPPERVERT, scrollbar_statemap, NOPAD, 0 },
    { "Horizontal.Scrollbar.trough", &GenericElementSpec, L"SCROLLBAR",
	SBP_UPPERTRACKHORZ, scrollbar_statemap, NOPAD, 0 },
    { "Horizontal.Scrollbar.thumb", &ThumbElementSpec, L"SCROLLBAR",
	SBP_THUMBBTNHORZ, scrollbar_statemap, NOPAD, 0 },
    { "Horizontal.Scrollbar.grip", &GenericElementSpec, L"SCROLLBAR",
	SBP_GRIPPERHORZ, scrollbar_statemap, NOPAD, 0 },
    { "Scrollbar.uparrow", &GenericSizedElementSpec, L"SCROLLBAR",
	SBP_ARROWBTN, uparrow_statemap, NOPAD,
	(SM_CXVSCROLL << 8) | SM_CYVSCROLL },
    { "Scrollbar.downarrow", &GenericSizedElementSpec, L"SCROLLBAR",
	SBP_ARROWBTN, downarrow_statemap, NOPAD,
	(SM_CXVSCROLL << 8) | SM_CYVSCROLL },
    { "Scrollbar.leftarrow", &GenericSizedElementSpec, L"SCROLLBAR",
	SBP_ARROWBTN, leftarrow_statemap, NOPAD,
	(SM_CXHSCROLL << 8) | SM_CYHSCROLL },
    { "Scrollbar.rightarrow", &GenericSizedElementSpec, L"SCROLLBAR",
	SBP_ARROWBTN, rightarrow_statemap, NOPAD,
	(SM_CXHSCROLL << 8) | SM_CYHSCROLL },
    { "Horizontal.Scale.slider", &GenericElementSpec, L"TRACKBAR",
	TKP_THUMB, scale_statemap, NOPAD, 0 },
    { "Vertical.Scale.slider", &GenericElementSpec, L"TRACKBAR",
	TKP_THUMBVERT, scale_statemap, NOPAD, 0 },
    { "Horizontal.Scale.track", &GenericElementSpec, L"TRACKBAR",
	TKP_TRACK, scale_statemap, NOPAD, 0 },
    { "Vertical.Scale.track", &GenericElementSpec, L"TRACKBAR",
	TKP_TRACKVERT, scale_statemap, NOPAD, 0 },
    /* ttk::progressbar elements */
    { "Horizontal.Progressbar.pbar", &PbarElementSpec, L"PROGRESS",
	PP_CHUNK, null_statemap, NOPAD, 0 },
    { "Vertical.Progressbar.pbar", &PbarElementSpec, L"PROGRESS",
	PP_CHUNKVERT, null_statemap, NOPAD, 0 },
    { "Horizontal.Progressbar.trough", &GenericElementSpec, L"PROGRESS",
	PP_BAR, null_statemap, PAD(3,3,3,3), IGNORE_THEMESIZE },
    { "Vertical.Progressbar.trough", &GenericElementSpec, L"PROGRESS",
	PP_BARVERT, null_statemap, PAD(3,3,3,3), IGNORE_THEMESIZE },
    /* ttk::notebook */
    { "tab", &TabElementSpec, L"TAB",
	TABP_TABITEM, tabitem_statemap, PAD(3,3,3,0), 0 },
    { "client", &GenericElementSpec, L"TAB",
	TABP_PANE, null_statemap, PAD(1,1,3,3), 0 },
    { "NotebookPane.background", &GenericElementSpec, L"TAB",
	TABP_BODY, null_statemap, NOPAD, 0 },
    { "Toolbutton.border", &GenericElementSpec, L"TOOLBAR",
	TP_BUTTON, toolbutton_statemap, NOPAD, 0 },
    { "Menubutton.button", &GenericElementSpec, L"TOOLBAR",
	TP_SPLITBUTTON, toolbutton_statemap, NOPAD, 0 },
    { "Menubutton.dropdown", &GenericSizedElementSpec, L"TOOLBAR",
	TP_SPLITBUTTONDROPDOWN, toolbutton_statemap, NOPAD,
	(SM_CXVSCROLL << 8) | SM_CYVSCROLL },
    { "Treeview.field", &GenericElementSpec, L"TREEVIEW",
	TVP_TREEITEM, treeview_statemap, PAD(1, 1, 1, 1), IGNORE_THEMESIZE },
    { "Treeitem.indicator", &TreeIndicatorElementSpec, L"TREEVIEW",
	TVP_GLYPH, tvpglyph_statemap, PAD(1,1,6,0), PAD_MARGINS },
    { "Treeheading.border", &GenericElementSpec, L"HEADER",
	HP_HEADERITEM, header_statemap, PAD(4,0,4,0), 0 },
    { "sizegrip", &GenericElementSpec, L"STATUS",
	SP_GRIPPER, null_statemap, NOPAD, 0 },
    { "Spinbox.field", &GenericElementSpec, L"EDIT",
	EP_EDITTEXT, edittext_statemap, PAD(1, 1, 1, 1), 0 },
    { "Spinbox.uparrow", &SpinboxArrowElementSpec, L"SPIN",
	SPNP_UP, spinbutton_statemap, NOPAD,
	PAD_MARGINS | ((SM_CXVSCROLL << 8) | SM_CYVSCROLL) },
    { "Spinbox.downarrow", &SpinboxArrowElementSpec, L"SPIN",
	SPNP_DOWN, spinbutton_statemap, NOPAD,
	PAD_MARGINS | ((SM_CXVSCROLL << 8) | SM_CYVSCROLL) },
    { 0, 0, 0, 0, 0, NOPAD, 0 }
};
#undef PAD


static int
GetSysFlagFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *resultPtr)
{
    static const char *const names[] = {
	"SM_CXBORDER", "SM_CYBORDER", "SM_CXVSCROLL", "SM_CYVSCROLL",
	"SM_CXHSCROLL", "SM_CYHSCROLL", "SM_CXMENUCHECK", "SM_CYMENUCHECK",
	"SM_CXMENUSIZE", "SM_CYMENUSIZE", "SM_CXSIZE", "SM_CYSIZE", "SM_CXSMSIZE",
	"SM_CYSMSIZE", NULL
    };
    int flags[] = {
	SM_CXBORDER, SM_CYBORDER, SM_CXVSCROLL, SM_CYVSCROLL,
	SM_CXHSCROLL, SM_CYHSCROLL, SM_CXMENUCHECK, SM_CYMENUCHECK,
	SM_CXMENUSIZE, SM_CYMENUSIZE, SM_CXSIZE, SM_CYSIZE, SM_CXSMSIZE,
	SM_CYSMSIZE
    };

    Tcl_Obj **objv;
    Tcl_Size i, objc;

    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
	return TCL_ERROR;
    }
    if (objc != 2) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("wrong # args", TCL_INDEX_NONE));
	Tcl_SetErrorCode(interp, "TCL", "WRONGARGS", (char *)NULL);
	return TCL_ERROR;
    }
    for (i = 0; i < objc; ++i) {
	int option;
	if (Tcl_GetIndexFromObjStruct(interp, objv[i], names,
		sizeof(char *), "system constant", 0, &option) != TCL_OK)
	    return TCL_ERROR;
	*resultPtr |= (flags[option] << (8 * (1 - i)));
    }
    return TCL_OK;
}

/*----------------------------------------------------------------------
 * Windows Visual Styles API Element Factory
 *
 * The Vista release has shown that the Windows Visual Styles can be
 * extended with additional elements. This element factory can permit
 * the programmer to create elements for use with script-defined layouts
 *
 * eg: to create the small close button:
 * style element create smallclose vsapi \
 *    WINDOW 19 {disabled 4 pressed 3 active 2 {} 1}
 */

static int
Ttk_CreateVsapiElement(
    Tcl_Interp *interp,
    void *clientData,
    Ttk_Theme theme,
    const char *elementName,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    HWND hwnd = (HWND)clientData;
    ElementInfo *elementPtr = NULL;
    void *elementData;
    LPCWSTR className;
    int partId = 0;
    Ttk_StateTable *stateTable;
    Ttk_Padding pad = {0, 0, 0, 0};
    unsigned flags = 0;
    Tcl_Size length = 0;
    char *name;
    LPWSTR wname;
    const Ttk_ElementSpec *elementSpec = &GenericElementSpec;
    Tcl_DString classBuf;

    static const char *const optionStrings[] =
	{ "-halfheight", "-halfwidth", "-height", "-margins", "-padding",
	  "-syssize", "-width", NULL };
    enum { O_HALFHEIGHT, O_HALFWIDTH, O_HEIGHT, O_MARGINS, O_PADDING,
	   O_SYSSIZE, O_WIDTH };

    if (objc < 2) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
	    "missing required arguments 'class' and/or 'partId'", TCL_INDEX_NONE));
	Tcl_SetErrorCode(interp, "TTK", "VSAPI", "REQUIRED", (char *)NULL);
	return TCL_ERROR;
    }

    if (Tcl_GetIntFromObj(interp, objv[1], &partId) != TCL_OK) {
	return TCL_ERROR;
    }
    name = Tcl_GetStringFromObj(objv[0], &length);
    Tcl_DStringInit(&classBuf);
    className = Tcl_UtfToWCharDString(name, length, &classBuf);

    /* flags or padding */
    if (objc > 3) {
	Tcl_Size i = 3;
	int option = 0;
	for (i = 3; i < objc; i += 2) {
	    int tmp = 0;
	    if (i == objc -1) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"Missing value for \"%s\".",
			Tcl_GetString(objv[i])));
		Tcl_SetErrorCode(interp, "TTK", "VSAPI", "MISSING", (char *)NULL);
		goto retErr;
	    }
	    if (Tcl_GetIndexFromObj(interp, objv[i], optionStrings,
		    "option", 0, &option) != TCL_OK)
		goto retErr;
	    switch (option) {
	    case O_PADDING:
		if (Ttk_GetBorderFromObj(interp, objv[i+1], &pad) != TCL_OK) {
		    goto retErr;
		}
		break;
	    case O_MARGINS:
		if (Ttk_GetBorderFromObj(interp, objv[i+1], &pad) != TCL_OK) {
		    goto retErr;
		}
		flags |= PAD_MARGINS;
		break;
	    case O_WIDTH:
		if (Tcl_GetIntFromObj(interp, objv[i+1], &tmp) != TCL_OK) {
		    goto retErr;
		}
		pad.left = pad.right = (short)tmp;
		flags |= IGNORE_THEMESIZE;
		break;
	    case O_HEIGHT:
		if (Tcl_GetIntFromObj(interp, objv[i+1], &tmp) != TCL_OK) {
		    goto retErr;
		}
		pad.top = pad.bottom = (short)tmp;
		flags |= IGNORE_THEMESIZE;
		break;
	    case O_SYSSIZE:
		if (GetSysFlagFromObj(interp, objv[i+1], &tmp) != TCL_OK) {
		    goto retErr;
		}
		elementSpec = &GenericSizedElementSpec;
		flags |= (tmp & 0xFFFF);
		break;
	    case O_HALFHEIGHT:
		if (Tcl_GetBooleanFromObj(interp, objv[i+1], &tmp) != TCL_OK) {
		    goto retErr;
		}
		if (tmp) {
		    flags |= HALF_HEIGHT;
		}
		break;
	    case O_HALFWIDTH:
		if (Tcl_GetBooleanFromObj(interp, objv[i+1], &tmp) != TCL_OK) {
		    goto retErr;
		}
		if (tmp) {
		    flags |= HALF_WIDTH;
		}
		break;
	    }
	}
    }

    /* convert a statemap into a state table */
    if (objc > 2) {
	Tcl_Obj **specs;
	Tcl_Size n, j, count;
	int status = TCL_OK;
	if (Tcl_ListObjGetElements(interp, objv[2], &count, &specs) != TCL_OK) {
	    goto retErr;
	}
	/* we over-allocate to ensure there is a terminating entry */
	stateTable = (Ttk_StateTable *)Tcl_Alloc(sizeof(Ttk_StateTable) * ((size_t)count + 1));
	memset(stateTable, 0, sizeof(Ttk_StateTable) * ((size_t)count + 1));
	for (n = 0, j = 0; status == TCL_OK && n < count; n += 2, ++j) {
	    Ttk_StateSpec spec = {0,0};
	    status = Ttk_GetStateSpecFromObj(interp, specs[n], &spec);
	    if (status == TCL_OK) {
		stateTable[j].onBits = spec.onbits;
		stateTable[j].offBits = spec.offbits;
		status = Tcl_GetIntFromObj(interp, specs[n+1],
			&stateTable[j].index);
	    }
	}
	if (status != TCL_OK) {
	    Tcl_Free(stateTable);
	    Tcl_DStringFree(&classBuf);
	    return status;
	}
    } else {
	stateTable = (Ttk_StateTable *)Tcl_Alloc(sizeof(Ttk_StateTable));
	memset(stateTable, 0, sizeof(Ttk_StateTable));
    }

    elementPtr = (ElementInfo *)Tcl_Alloc(sizeof(ElementInfo));
    elementPtr->elementSpec = elementSpec;
    elementPtr->partId = partId;
    elementPtr->statemap = stateTable;
    elementPtr->padding = pad;
    elementPtr->flags = HEAP_ELEMENT | (unsigned)flags;

    /* set the element name to an allocated copy */
    name = (char *)Tcl_Alloc(strlen(elementName) + 1);
    strcpy(name, elementName);
    elementPtr->elementName = name;

    /* set the class name to an allocated copy */
    wname = (LPWSTR)Tcl_Alloc((size_t)Tcl_DStringLength(&classBuf) + sizeof(WCHAR));
    wcscpy(wname, className);
    elementPtr->className = wname;

    elementData = NewElementData(hwnd, elementPtr);
    Ttk_RegisterElement(NULL,
	theme, elementName, elementPtr->elementSpec, elementData);

    Ttk_RegisterCleanup(interp, elementData, DestroyElementData);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(elementName, TCL_INDEX_NONE));
    Tcl_DStringFree(&classBuf);
    return TCL_OK;

retErr:
    Tcl_DStringFree(&classBuf);
    return TCL_ERROR;
}

/*----------------------------------------------------------------------
 * +++ Initialization routine:
 */

MODULE_SCOPE int
TtkWinVistaTheme_Init(Tcl_Interp *interp, HWND hwnd)
{
    Ttk_Theme themePtr, parentPtr;
    const ElementInfo *infoPtr;

    /*
     * Create the new style engine.
     */
    parentPtr = Ttk_GetTheme(interp, "winnative");
    themePtr = Ttk_CreateTheme(interp, "vista", parentPtr);

    if (!themePtr) {
	return TCL_ERROR;
    }

    /*
     * Set theme data and cleanup proc
     */

    Ttk_SetThemeEnabledProc(themePtr, VistaThemeEnabled, hwnd);
    Ttk_RegisterCleanup(interp, hwnd, VistaThemeDeleteProc);
    Ttk_RegisterElementFactory(interp, "vsapi", Ttk_CreateVsapiElement, hwnd);

    /*
     * New elements:
     */
    for (infoPtr = ElementInfoTable; infoPtr->elementName != 0; ++infoPtr) {
	void *clientData = NewElementData(hwnd, infoPtr);
	Ttk_RegisterElement(NULL,
	    themePtr, infoPtr->elementName, infoPtr->elementSpec, clientData);
	Ttk_RegisterCleanup(interp, clientData, DestroyElementData);
    }

    Ttk_RegisterElement(NULL, themePtr, "Scale.trough", &ttkNullElementSpec, 0);

    /*
     * Layouts:
     */
    Ttk_RegisterLayouts(themePtr, LayoutTable);

    Tcl_PkgProvide(interp, "ttk::theme::vista", TTK_VERSION);

    return TCL_OK;
}
