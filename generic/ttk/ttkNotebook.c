/*
 * Copyright © 2004 Joe English
 */

#include "tkInt.h"
#include "ttkThemeInt.h"
#include "ttkWidget.h"
#include "ttkManager.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

/*------------------------------------------------------------------------
 * +++ Tab resources.
 */

#define DEFAULT_MIN_TAB_WIDTH 24

static const char *const TabStateStrings[] = { "disabled", "hidden", "normal", 0 };
typedef enum {
    TAB_STATE_DISABLED, TAB_STATE_HIDDEN, TAB_STATE_NORMAL
} TAB_STATE;

typedef struct
{
    /* Internal data:
     */
    int	width, height;		/* Requested size of tab */
    Ttk_Box	parcel;			/* Tab position */

    /* Tab options:
     */
    TAB_STATE	state;

    /* Child window options:
     */
    Tcl_Obj	*paddingObj;		/* Padding inside pane */
    Ttk_Padding	padding;
    Tcl_Obj	*stickyObj;
    Ttk_Sticky	sticky;

    /* Label options:
     */
    Tcl_Obj *textObj;
    Tcl_Obj *imageObj;
    Tcl_Obj *compoundObj;
    Tcl_Obj *underlineObj;

} Tab;

/* Two different option tables are used for tabs:
 * TabOptionSpecs is used to draw the tab, and only includes resources
 * relevant to the tab.
 *
 * PaneOptionSpecs includes additional options for child window placement
 * and is used to configure the pane.
 */
static const Tk_OptionSpec TabOptionSpecs[] =
{
    {TK_OPTION_STRING_TABLE, "-state", "", "",
	"normal", TCL_INDEX_NONE, offsetof(Tab,state),
	TK_OPTION_ENUM_VAR, TabStateStrings, 0 },
    {TK_OPTION_STRING, "-text", "text", "Text", "",
	offsetof(Tab,textObj), TCL_INDEX_NONE, 0, 0, GEOMETRY_CHANGED },
    {TK_OPTION_STRING, "-image", "image", "Image", NULL/*default*/,
	offsetof(Tab,imageObj), TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, GEOMETRY_CHANGED },
    {TK_OPTION_STRING_TABLE, "-compound", "compound", "Compound",
	NULL, offsetof(Tab,compoundObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, ttkCompoundStrings, GEOMETRY_CHANGED },
    {TK_OPTION_INDEX, "-underline", "underline", "Underline",
	TTK_OPTION_UNDERLINE_DEF(Tab, underlineObj), GEOMETRY_CHANGED},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0 }
};

static const Tk_OptionSpec PaneOptionSpecs[] =
{
    {TK_OPTION_STRING, "-padding", "padding", "Padding", "0",
	offsetof(Tab,paddingObj), TCL_INDEX_NONE, 0, 0, GEOMETRY_CHANGED },
    {TK_OPTION_STRING, "-sticky", "sticky", "Sticky", "nsew",
	offsetof(Tab,stickyObj), TCL_INDEX_NONE, 0, 0, GEOMETRY_CHANGED },

    WIDGET_INHERIT_OPTIONS(TabOptionSpecs)
};

/*------------------------------------------------------------------------
 * +++ Notebook resources.
 */
typedef struct
{
    Tcl_Obj *widthObj;		/* Default width */
    Tcl_Obj *heightObj;		/* Default height */
    Tcl_Obj *paddingObj;	/* Padding around notebook */

    Ttk_Manager *mgr;		/* Geometry manager */
    Tk_OptionTable tabOptionTable;	/* Tab options */
    Tk_OptionTable paneOptionTable;	/* Tab+pane options */
    Tcl_Size currentIndex;		/* index of currently selected tab */
    Tcl_Size activeIndex;		/* index of currently active tab */
    Ttk_Layout tabLayout;	/* Sublayout for tabs */

    Ttk_Box clientArea;		/* Where to pack content windows */
} NotebookPart;

typedef struct
{
    WidgetCore core;
    NotebookPart notebook;
} Notebook;

static const Tk_OptionSpec NotebookOptionSpecs[] =
{
    {TK_OPTION_PIXELS, "-width", "width", "Width", "0",
	offsetof(Notebook,notebook.widthObj),TCL_INDEX_NONE,
	0,0,GEOMETRY_CHANGED },
    {TK_OPTION_PIXELS, "-height", "height", "Height", "0",
	offsetof(Notebook,notebook.heightObj),TCL_INDEX_NONE,
	0,0,GEOMETRY_CHANGED },
    {TK_OPTION_STRING, "-padding", "padding", "Padding", NULL,
	offsetof(Notebook,notebook.paddingObj),TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED },

    WIDGET_TAKEFOCUS_TRUE,
    WIDGET_INHERIT_OPTIONS(ttkCoreOptionSpecs)
};

/* Notebook style options:
 */
typedef struct
{
    Ttk_PositionSpec	tabPosition;	/* Where to place tabs */
    Ttk_Padding	tabMargins;	/* Margins around tab row */
    Ttk_PositionSpec	tabPlacement;	/* How to pack tabs within tab row */
    Ttk_Orient		tabOrient;	/* ... */
    int		minTabWidth;	/* Minimum tab width */
    Ttk_Padding	padding;	/* External padding */
} NotebookStyle;

static void NotebookStyleOptions(
    Notebook *nb, NotebookStyle *nbstyle, Tk_Window tkwin)
{
    Tcl_Obj *objPtr;
    TkMainInfo *mainInfoPtr = ((TkWindow *) tkwin)->mainPtr;

    nbstyle->tabPosition = TTK_PACK_TOP | TTK_STICK_W;
    if ((objPtr = Ttk_QueryOption(nb->core.layout, "-tabposition", 0)) != 0) {
	TtkGetLabelAnchorFromObj(NULL, objPtr, &nbstyle->tabPosition);
    }

    /* Guess default tabPlacement as function of tabPosition:
     */
    if (nbstyle->tabPosition & TTK_PACK_LEFT) {
	nbstyle->tabPlacement = TTK_PACK_TOP | TTK_STICK_E;
    } else if (nbstyle->tabPosition & TTK_PACK_RIGHT) {
	nbstyle->tabPlacement = TTK_PACK_TOP | TTK_STICK_W;
    } else if (nbstyle->tabPosition & TTK_PACK_BOTTOM) {
	nbstyle->tabPlacement = TTK_PACK_LEFT | TTK_STICK_N;
    } else { /* Assume TTK_PACK_TOP */
	nbstyle->tabPlacement = TTK_PACK_LEFT | TTK_STICK_S;
    }
    if ((objPtr = Ttk_QueryOption(nb->core.layout, "-tabplacement", 0)) != 0) {
	TtkGetLabelAnchorFromObj(NULL, objPtr, &nbstyle->tabPlacement);
    }

    /* Save the stick bit for later.  One of the values
     * TTK_STICK_S, TTK_STICK_N, TTK_STICK_E, or TTK_STICK_W:
     */
    if (mainInfoPtr != NULL) {
	mainInfoPtr->ttkNbTabsStickBit = (nbstyle->tabPlacement & 0x0f);
    }

    /* Compute tabOrient as function of tabPlacement:
     */
    if (nbstyle->tabPlacement & (TTK_PACK_LEFT|TTK_PACK_RIGHT)) {
	nbstyle->tabOrient = TTK_ORIENT_HORIZONTAL;
    } else {
	nbstyle->tabOrient = TTK_ORIENT_VERTICAL;
    }

    nbstyle->tabMargins = Ttk_UniformPadding(0);
    if ((objPtr = Ttk_QueryOption(nb->core.layout, "-tabmargins", 0)) != 0) {
	Ttk_GetPaddingFromObj(NULL, tkwin, objPtr, &nbstyle->tabMargins);
    }

    nbstyle->padding = Ttk_UniformPadding(0);
    if ((objPtr = Ttk_QueryOption(nb->core.layout, "-padding", 0)) != 0) {
	Ttk_GetPaddingFromObj(NULL, tkwin, objPtr, &nbstyle->padding);
    }

    nbstyle->minTabWidth = DEFAULT_MIN_TAB_WIDTH;
    if ((objPtr = Ttk_QueryOption(nb->core.layout, "-mintabwidth", 0)) != 0) {
	Tk_GetPixelsFromObj(NULL, tkwin, objPtr, &nbstyle->minTabWidth);
    }
}

/*------------------------------------------------------------------------
 * +++ Tab management.
 */

static Tab *CreateTab(Tcl_Interp *interp, Notebook *nb, Tk_Window window)
{
    Tk_OptionTable optionTable = nb->notebook.paneOptionTable;
    Tab *record = (Tab *)ckalloc(sizeof(Tab));
    memset(record, 0, sizeof(Tab));

    if (Tk_InitOptions(interp, record, optionTable, window) != TCL_OK) {
	ckfree(record);
	return NULL;
    }

    return record;
}

static void DestroyTab(Notebook *nb, Tab *tab)
{
    void *record = tab;
    Tk_FreeConfigOptions(record, nb->notebook.paneOptionTable, nb->core.tkwin);
    ckfree(record);
}

static int ConfigureTab(
    Tcl_Interp *interp, Notebook *nb, Tab *tab, Tk_Window window,
    Tcl_Size objc, Tcl_Obj *const objv[])
{
    Ttk_Sticky sticky = tab->sticky;
    Ttk_Padding padding = tab->padding;
    Tk_SavedOptions savedOptions;
    int mask = 0;

    if (Tk_SetOptions(interp, tab, nb->notebook.paneOptionTable,
	    objc, objv, window, &savedOptions, &mask) != TCL_OK)
    {
	return TCL_ERROR;
    }

    /* Check options:
     * @@@ TODO: validate -image option.
     */
    if (Ttk_GetStickyFromObj(interp, tab->stickyObj, &sticky) != TCL_OK)
    {
	goto error;
    }
    if (Ttk_GetPaddingFromObj(interp, window, tab->paddingObj, &padding)
	    != TCL_OK)
    {
	goto error;
    }

    tab->sticky = sticky;
    tab->padding = padding;

    Tk_FreeSavedOptions(&savedOptions);
    Ttk_ManagerSizeChanged(nb->notebook.mgr);
    TtkRedisplayWidget(&nb->core);

    return TCL_OK;
error:
    Tk_RestoreSavedOptions(&savedOptions);
    return TCL_ERROR;
}

/*
 * IdentifyTab --
 *	Return the index of the tab at point x,y,
 *	or -1 if no tab at that point.
 */
static Tcl_Size IdentifyTab(Notebook *nb, int x, int y)
{
    Tcl_Size index;
    for (index = 0; index < Ttk_NumberContent(nb->notebook.mgr); ++index) {
	Tab *tab = (Tab *)Ttk_ContentData(nb->notebook.mgr,index);
	if (	tab->state != TAB_STATE_HIDDEN
	     && Ttk_BoxContains(tab->parcel, x,y))
	{
	    return index;
	}
    }
    return TCL_INDEX_NONE;
}

/*
 * ActivateTab --
 *	Set the active tab index, redisplay if necessary.
 */
static void ActivateTab(Notebook *nb, Tcl_Size index)
{
    if (index != nb->notebook.activeIndex) {
	nb->notebook.activeIndex = index;
	TtkRedisplayWidget(&nb->core);
    }
}

/*
 * TabState --
 *	Return the state of the specified tab, based on
 *	notebook state, currentIndex, activeIndex, and user-specified tab state.
 *	The USER1 bit is set for the leftmost visible tab, and USER2
 *	is set for the rightmost visible tab.
 */
static Ttk_State TabState(Notebook *nb, Tcl_Size index)
{
    Ttk_State state = nb->core.state;
    Tab *itab = (Tab *)Ttk_ContentData(nb->notebook.mgr, index);
    Tcl_Size i = 0;

    if (index == nb->notebook.currentIndex) {
	state |= TTK_STATE_SELECTED;
    } else {
	state &= ~TTK_STATE_FOCUS;
    }

    if (index == nb->notebook.activeIndex) {
	state |= TTK_STATE_ACTIVE;
    }
    for (i = 0; i < Ttk_NumberContent(nb->notebook.mgr); ++i) {
	Tab *tab = (Tab *)Ttk_ContentData(nb->notebook.mgr, i);
	if (tab->state == TAB_STATE_HIDDEN) {
	    continue;
	}
	if (index == i) {
	    state |= TTK_STATE_FIRST;
	}
	break;
    }
    for (i = Ttk_NumberContent(nb->notebook.mgr) - 1; i >= 0; --i) {
	Tab *tab = (Tab *)Ttk_ContentData(nb->notebook.mgr, i);
	if (tab->state == TAB_STATE_HIDDEN) {
	    continue;
	}
	if (index == i) {
	    state |= TTK_STATE_LAST;
	}
	break;
    }
    if (itab->state == TAB_STATE_DISABLED) {
	state |= TTK_STATE_DISABLED;
    }

    return state;
}

/*------------------------------------------------------------------------
 * +++ Geometry management - size computation.
 */

/* TabrowSize --
 *	Compute max height and total width of all tabs (horizontal layouts)
 *	or total height and max width (vertical layouts).
 *	The -mintabwidth style option is taken into account (for the width
 *	only).
 *
 * Side effects:
 *	Sets width and height fields for all tabs.
 *
 * Notes:
 *	Hidden tabs are included in the perpendicular computation
 *	(max height/width) but not parallel (total width/height).
 */
static void TabrowSize(
    Notebook *nb, Ttk_Orient orient, int minTabWidth, int *widthPtr, int *heightPtr)
{
    Ttk_Layout tabLayout = nb->notebook.tabLayout;
    int tabrowWidth = 0, tabrowHeight = 0;
    Tcl_Size i;

    for (i = 0; i < Ttk_NumberContent(nb->notebook.mgr); ++i) {
	Tab *tab = (Tab *)Ttk_ContentData(nb->notebook.mgr, i);
	Ttk_State tabState = TabState(nb,i);

	Ttk_RebindSublayout(tabLayout, tab);
	Ttk_LayoutSize(tabLayout,tabState,&tab->width,&tab->height);
	tab->width = MAX(tab->width, minTabWidth);

	if (orient == TTK_ORIENT_HORIZONTAL) {
	    tabrowHeight = MAX(tabrowHeight, tab->height);
	    if (tab->state != TAB_STATE_HIDDEN) { tabrowWidth += tab->width; }
	} else {
	    tabrowWidth = MAX(tabrowWidth, tab->width);
	    if (tab->state != TAB_STATE_HIDDEN) { tabrowHeight += tab->height; }
	}
    }

    *widthPtr = tabrowWidth;
    *heightPtr = tabrowHeight;
}

/* NotebookSize -- GM and widget size hook.
 *
 * Total height is tab height + client area height + pane internal padding
 * Total width is max(client width, tab width) + pane internal padding
 * Client area size determined by max size of content windows,
 * overridden by -width and/or -height if nonzero.
 */

static int NotebookSize(void *clientData, int *widthPtr, int *heightPtr)
{
    Notebook *nb = (Notebook *)clientData;
    Tk_Window nbwin = nb->core.tkwin;
    NotebookStyle nbstyle;
    Ttk_Padding padding;
    Ttk_Element clientNode = Ttk_FindElement(nb->core.layout, "client");
    int clientWidth = 0, clientHeight = 0,
	reqWidth = 0, reqHeight = 0,
	tabrowWidth = 0, tabrowHeight = 0;
    Tcl_Size i;

    NotebookStyleOptions(nb, &nbstyle, nbwin);

    /* Compute max requested size of all content windows:
     */
    for (i = 0; i < Ttk_NumberContent(nb->notebook.mgr); ++i) {
	Tk_Window window = Ttk_ContentWindow(nb->notebook.mgr, i);
	Tab *tab = (Tab *)Ttk_ContentData(nb->notebook.mgr, i);
	int width
	    = Tk_ReqWidth(window) + Ttk_PaddingWidth(tab->padding);
	int height
	    = Tk_ReqHeight(window) + Ttk_PaddingHeight(tab->padding);

	clientWidth = MAX(clientWidth, width);
	clientHeight = MAX(clientHeight, height);
    }

    /* Client width/height overridable by widget options:
     */
    Tk_GetPixelsFromObj(NULL, nbwin, nb->notebook.widthObj, &reqWidth);
    Tk_GetPixelsFromObj(NULL, nbwin, nb->notebook.heightObj, &reqHeight);
    if (reqWidth > 0)
	clientWidth = reqWidth;
    if (reqHeight > 0)
	clientHeight = reqHeight;

    /* Tab row:
     */
    TabrowSize(nb, nbstyle.tabOrient, nbstyle.minTabWidth, &tabrowWidth, &tabrowHeight);
    tabrowHeight += Ttk_PaddingHeight(nbstyle.tabMargins);
    tabrowWidth += Ttk_PaddingWidth(nbstyle.tabMargins);

    /* Account for exterior and interior padding:
     */
    padding = nbstyle.padding;
    if (clientNode) {
	Ttk_Padding ipad =
	    Ttk_LayoutNodeInternalPadding(nb->core.layout, clientNode);
	padding = Ttk_AddPadding(padding, ipad);
    }

    if (nbstyle.tabPosition & (TTK_PACK_TOP|TTK_PACK_BOTTOM)) {
	*widthPtr = MAX(tabrowWidth, clientWidth) + Ttk_PaddingWidth(padding);
	*heightPtr = tabrowHeight + clientHeight + Ttk_PaddingHeight(padding);
    } else {
	*widthPtr = tabrowWidth + clientWidth + Ttk_PaddingWidth(padding);
	*heightPtr = MAX(tabrowHeight,clientHeight) + Ttk_PaddingHeight(padding);
    }

    return 1;
}

/*------------------------------------------------------------------------
 * +++ Geometry management - layout.
 */

/* SqueezeTabs --
 *	Squeeze or stretch tabs to fit within the tab area parcel.
 *	This happens independently of the -mintabwidth style option.
 *
 *	All tabs are adjusted by an equal amount.
 *
 * @@@ <<NOTE-TABPOSITION>> bug: only works for horizontal orientations
 * @@@ <<NOTE-SQUEEZE-HIDDEN>> does not account for hidden tabs.
 */

static void SqueezeTabs(
    Notebook *nb, int needed, int available)
{
    int nTabs = Ttk_NumberContent(nb->notebook.mgr);

    if (nTabs > 0) {
	int difference = available - needed;
	double delta = (double)difference / needed;
	double slack = 0;
	int i;

	for (i = 0; i < nTabs; ++i) {
	    Tab *tab = (Tab *)Ttk_ContentData(nb->notebook.mgr,i);
	    double ad = slack + tab->width * delta;
	    tab->width += (int)ad;
	    slack = ad - (int)ad;
	}
    }
}

/* PlaceTabs --
 *	Compute all tab parcels.
 */
static void PlaceTabs(
    Notebook *nb, Ttk_Box tabrowBox, Ttk_PositionSpec tabPlacement)
{
    Ttk_Layout tabLayout = nb->notebook.tabLayout;
    int nTabs = Ttk_NumberContent(nb->notebook.mgr);
    int i;

    for (i = 0; i < nTabs; ++i) {
	Tab *tab = (Tab *)Ttk_ContentData(nb->notebook.mgr, i);
	Ttk_State tabState = TabState(nb, i);

	if (tab->state != TAB_STATE_HIDDEN) {
	    Ttk_Padding expand = Ttk_UniformPadding(0);
	    Tcl_Obj *expandObj = Ttk_QueryOption(tabLayout,"-expand",tabState);

	    if (expandObj) {
		Ttk_GetPaddingFromObj(NULL, nb->core.tkwin, expandObj, &expand);
	    }

	    tab->parcel =
		Ttk_ExpandBox(
		    Ttk_PositionBox(&tabrowBox,
			tab->width, tab->height, tabPlacement),
		    expand);
	}
    }
}

/*
 * NotebookPlaceContent --
 *	Set the position and size of a child widget
 *	based on the current client area and content window options:
 */
static void NotebookPlaceContent(Notebook* nb, Tcl_Size index)
{
    Tab* tab = (Tab*)Ttk_ContentData(nb->notebook.mgr, index);
    Tk_Window window = Ttk_ContentWindow(nb->notebook.mgr, index);
    Ttk_Box box =
	Ttk_StickBox(Ttk_PadBox(nb->notebook.clientArea, tab->padding),
	    Tk_ReqWidth(window), Tk_ReqHeight(window), tab->sticky);

    Ttk_PlaceContent(nb->notebook.mgr, index,
	box.x, box.y, box.width, box.height);
}

/* NotebookDoLayout --
 *	Computes notebook layout and places tabs.
 *
 * Side effects:
 *	Sets clientArea, used to place panes.
 */
static void NotebookDoLayout(void *recordPtr)
{
    Notebook *nb = (Notebook *)recordPtr;
    Tk_Window nbwin = nb->core.tkwin;
    Ttk_Box cavity = Ttk_WinBox(nbwin);
    int tabrowWidth = 0, tabrowHeight = 0;
    Ttk_Element clientNode = Ttk_FindElement(nb->core.layout, "client");
    Ttk_Box tabrowBox;
    NotebookStyle nbstyle;
    Tcl_Size currentIndex = nb->notebook.currentIndex;

    NotebookStyleOptions(nb, &nbstyle, nbwin);

    /* Notebook internal padding:
     */
    cavity = Ttk_PadBox(cavity, nbstyle.padding);

    /* Layout for notebook background (base layout):
     */
    Ttk_PlaceLayout(nb->core.layout, nb->core.state, Ttk_WinBox(nbwin));

    /* Place tabs:
     * Note: TabrowSize() takes into account -mintabwidth, but the tabs will
     * actually have this minimum size when displayed only if there is enough
     * space to draw the tabs with this width. Otherwise some of the tabs can
     * be squeezed to a size smaller than -mintabwidth because we prefer
     * displaying all tabs than than honoring -mintabwidth for all of them.
     */
    TabrowSize(nb, nbstyle.tabOrient, nbstyle.minTabWidth, &tabrowWidth, &tabrowHeight);
    tabrowBox = Ttk_PadBox(
		    Ttk_PositionBox(&cavity,
			tabrowWidth + Ttk_PaddingWidth(nbstyle.tabMargins),
			tabrowHeight + Ttk_PaddingHeight(nbstyle.tabMargins),
			nbstyle.tabPosition),
		    nbstyle.tabMargins);

    SqueezeTabs(nb, tabrowWidth, tabrowBox.width);
    PlaceTabs(nb, tabrowBox, nbstyle.tabPlacement);

    /* Layout for client area frame:
     */
    if (clientNode) {
	Ttk_PlaceElement(nb->core.layout, clientNode, cavity);
	cavity = Ttk_LayoutNodeInternalParcel(nb->core.layout, clientNode);
    }

    if (cavity.height <= 0) cavity.height = 1;
    if (cavity.width <= 0) cavity.width = 1;

    if (!TtkBoxEqual(nb->notebook.clientArea, cavity)) {
	nb->notebook.clientArea = cavity;
	if (currentIndex >= 0) {
	    NotebookPlaceContent(nb, currentIndex);
	}
    }
}

/* NotebookPlaceContents --
 *	Geometry manager hook.
 */
static void NotebookPlaceContents(void *recordPtr)
{
    Notebook *nb = (Notebook *)recordPtr;
    Tcl_Size currentIndex = nb->notebook.currentIndex;
    if (currentIndex >= 0) {
	NotebookDoLayout(nb);
	NotebookPlaceContent(nb, currentIndex);
    }
}

/*
 * SelectTab(nb, index) --
 *	Change the currently-selected tab.
 */
static void SelectTab(Notebook *nb, Tcl_Size index)
{
    Tab *tab = (Tab *)Ttk_ContentData(nb->notebook.mgr, index);
    Tcl_Size currentIndex = nb->notebook.currentIndex;

    if (index == currentIndex) {
	return;
    }

    if (TabState(nb, index) & TTK_STATE_DISABLED) {
	return;
    }

    /* Unhide the tab if it is currently hidden and being selected.
     */
    if (tab->state == TAB_STATE_HIDDEN) {
	tab->state = TAB_STATE_NORMAL;
    }

    if (currentIndex >= 0) {
	Ttk_UnmapContent(nb->notebook.mgr, currentIndex);
    }

    /* Must be set before calling NotebookPlaceContent(), otherwise it may
     * happen that NotebookPlaceContents(), triggered by an interveaning
     * geometry request, will swap to old index. */
    nb->notebook.currentIndex = index;

    NotebookPlaceContent(nb, index);
    TtkRedisplayWidget(&nb->core);

    Tk_SendVirtualEvent(nb->core.tkwin, "NotebookTabChanged", NULL);
}

/* NextTab --
 *	Returns the index of the next tab after the specified tab
 *	in the normal state (e.g., not hidden or disabled),
 *	or -1 if all tabs are disabled or hidden.
 */
static int NextTab(Notebook *nb, int index)
{
    Tcl_Size nTabs = Ttk_NumberContent(nb->notebook.mgr);
    Tcl_Size nextIndex;

    /* Scan forward for following usable tab:
     */
    for (nextIndex = index + 1; nextIndex < nTabs; ++nextIndex) {
	Tab *tab = (Tab *)Ttk_ContentData(nb->notebook.mgr, nextIndex);
	if (tab->state == TAB_STATE_NORMAL) {
	    return nextIndex;
	}
    }

    /* Not found -- scan backwards.
     */
    for (nextIndex = index - 1; nextIndex >= 0; --nextIndex) {
	Tab *tab = (Tab *)Ttk_ContentData(nb->notebook.mgr, nextIndex);
	if (tab->state == TAB_STATE_NORMAL) {
	    return nextIndex;
	}
    }

    /* Still nothing.  Give up.
     */
    return -1;
}

/* SelectNearestTab --
 *	Handles the case where the current tab is forgotten, hidden,
 *	or destroyed.
 *
 *	Unmap the current tab and schedule the next available one
 *	to be mapped at the next GM update.
 */
static void SelectNearestTab(Notebook *nb)
{
    Tcl_Size currentIndex = nb->notebook.currentIndex;
    Tcl_Size nextIndex = NextTab(nb, currentIndex);

    if (currentIndex >= 0) {
	Ttk_UnmapContent(nb->notebook.mgr, currentIndex);
    }
    if (currentIndex != nextIndex) {
	Tk_SendVirtualEvent(nb->core.tkwin, "NotebookTabChanged", NULL);
    }

    nb->notebook.currentIndex = nextIndex;
    Ttk_ManagerLayoutChanged(nb->notebook.mgr);
    TtkRedisplayWidget(&nb->core);
}

/* TabRemoved -- GM TabRemoved hook.
 *	Select the next tab if the current one is being removed.
 *	Adjust currentIndex to account for removed content window.
 */
static void TabRemoved(void *managerData, Tcl_Size index)
{
    Notebook *nb = (Notebook *)managerData;
    Tab *tab = (Tab *)Ttk_ContentData(nb->notebook.mgr, index);

    if (index == nb->notebook.currentIndex) {
	SelectNearestTab(nb);
    }

    if (index < nb->notebook.currentIndex) {
	--nb->notebook.currentIndex;
    }

    DestroyTab(nb, tab);

    TtkRedisplayWidget(&nb->core);
}

static int TabRequest(
    TCL_UNUSED(void *), /* managerData */
    TCL_UNUSED(Tcl_Size), /* index */
    TCL_UNUSED(int), /* width */
    TCL_UNUSED(int)) /* height */
{
    return 1;
}

/* AddTab --
 *	Add new tab at specified index.
 */
static int AddTab(
    Tcl_Interp *interp, Notebook *nb,
    Tcl_Size destIndex, Tk_Window window,
    Tcl_Size objc, Tcl_Obj *const objv[])
{
    Tab *tab;
    if (!Ttk_Maintainable(interp, window, nb->core.tkwin)) {
	return TCL_ERROR;
    }
#if 0 /* can't happen */
    if (Ttk_ContentIndex(nb->notebook.mgr, window) >= 0) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s already added",
	    Tk_PathName(window)));
	Tcl_SetErrorCode(interp, "TTK", "NOTEBOOK", "PRESENT", (char *)NULL);
	return TCL_ERROR;
    }
#endif

    /* Create and insert tab.
     */
    tab = CreateTab(interp, nb, window);
    if (!tab) {
	return TCL_ERROR;
    }
    if (ConfigureTab(interp, nb, tab, window, objc, objv) != TCL_OK) {
	DestroyTab(nb, tab);
	return TCL_ERROR;
    }

    Ttk_InsertContent(nb->notebook.mgr, destIndex, window, tab);

    /* Adjust indices and/or autoselect first tab:
     */
    if (nb->notebook.currentIndex < 0) {
	SelectTab(nb, destIndex);
    } else if (nb->notebook.currentIndex  >= destIndex) {
	++nb->notebook.currentIndex;
    }

    return TCL_OK;
}

static const Ttk_ManagerSpec NotebookManagerSpec = {
    { "notebook", Ttk_GeometryRequestProc, Ttk_LostContentProc },
    NotebookSize,
    NotebookPlaceContents,
    TabRequest,
    TabRemoved
};

/*------------------------------------------------------------------------
 * +++ Event handlers.
 */

/* NotebookEventHandler --
 *	Tracks the active tab.
 */
static const int NotebookEventMask
    = StructureNotifyMask
    | PointerMotionMask
    | LeaveWindowMask
    ;
static void NotebookEventHandler(void *clientData, XEvent *eventPtr)
{
    Notebook *nb = (Notebook *)clientData;

    if (eventPtr->type == DestroyNotify) { /* Remove self */
	Tk_DeleteEventHandler(nb->core.tkwin,
	    NotebookEventMask, NotebookEventHandler, clientData);
    } else if (eventPtr->type == MotionNotify) {
	Tcl_Size index = IdentifyTab(nb, eventPtr->xmotion.x, eventPtr->xmotion.y);
	ActivateTab(nb, index);
    } else if (eventPtr->type == LeaveNotify) {
	ActivateTab(nb, -1);
    }
}

/*------------------------------------------------------------------------
 * +++ Utilities.
 */

/* FindTabIndex --
 *	Find the index of the specified tab.
 *	Tab identifiers are one of:
 *
 *	+ positional specifications @x,y,
 *	+ "current",
 *	+ numeric indices [0..nTabs],
 *	+ content window names
 *
 *	Stores index of specified tab in *index_rtn, -1 if not found.
 *
 *	Returns TCL_ERROR and leaves an error message in interp->result
 *	if the tab identifier was incorrect.
 *
 *	See also: GetTabIndex.
 */
static int FindTabIndex(
    Tcl_Interp *interp, Notebook *nb, Tcl_Obj *objPtr, Tcl_Size *index_rtn)
{
    const char *string = Tcl_GetString(objPtr);
    int x, y;

    *index_rtn = TCL_INDEX_NONE;

    /* Check for @x,y ...
     */
    if (string[0] == '@' && sscanf(string, "@%d,%d",&x,&y) == 2) {
	*index_rtn = IdentifyTab(nb, x, y);
	return TCL_OK;
    }

    /* ... or "current" ...
     */
    if (!strcmp(string, "current")) {
	*index_rtn = nb->notebook.currentIndex;
	return TCL_OK;
    }

    /* ... or integer index or content window name:
     */
    if (Ttk_GetContentIndexFromObj(
	    interp, nb->notebook.mgr, objPtr, 1, index_rtn) == TCL_OK)
    {
	return TCL_OK;
    }
    if (*index_rtn == Ttk_NumberContent(nb->notebook.mgr)) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"Invalid tab specification %s", string));
	Tcl_SetErrorCode(interp, "TTK", "NOTEBOOK", "SPEC", (char *)NULL);
	return TCL_ERROR;
    }

    /* Nothing matched; Ttk_GetContentIndexFromObj will have left error message.
     */
    return TCL_ERROR;
}

/* GetTabIndex --
 *	Get the index of an existing tab.
 *	Tab identifiers are as per FindTabIndex.
 *	Returns TCL_ERROR if the tab does not exist.
 */
static int GetTabIndex(
    Tcl_Interp *interp, Notebook *nb, Tcl_Obj *objPtr, Tcl_Size *index_rtn)
{
    int status = FindTabIndex(interp, nb, objPtr, index_rtn);
	if (status == TCL_OK && *index_rtn  >= Ttk_NumberContent(nb->notebook.mgr)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"Tab index \"%s\" out of bounds", Tcl_GetString(objPtr)));
	    Tcl_SetErrorCode(interp, "TTK", "NOTEBOOK", "INDEX", (char *)NULL);
	    return TCL_ERROR;
	}

    if (status == TCL_OK && *index_rtn < 0) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "Tab '%s' not found", Tcl_GetString(objPtr)));
	Tcl_SetErrorCode(interp, "TTK", "NOTEBOOK", "TAB", (char *)NULL);
	status = TCL_ERROR;
    }
    return status;
}

/*------------------------------------------------------------------------
 * +++ Widget command routines.
 */

/* $nb add window ?options ... ?
 */
static int NotebookAddCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Notebook *nb = (Notebook *)recordPtr;
    Tk_Window window;
    Tcl_Size index;
    Tab *tab;

    if (objc <= 2 || objc % 2 != 1) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?-option value ...?");
	return TCL_ERROR;
    }

    window = Tk_NameToWindow(interp,Tcl_GetString(objv[2]),nb->core.tkwin);
    if (!window) {
	return TCL_ERROR;
    }
    index = Ttk_ContentIndex(nb->notebook.mgr, window);

    if (index < 0) { /* New tab */
	return AddTab(interp, nb, Ttk_NumberContent(nb->notebook.mgr), window, objc-3,objv+3);
    }

    tab = (Tab *)Ttk_ContentData(nb->notebook.mgr, index);
    if (tab->state == TAB_STATE_HIDDEN) {
	tab->state = TAB_STATE_NORMAL;
    }
    if (ConfigureTab(interp, nb, tab, window, objc-3,objv+3) != TCL_OK) {
	return TCL_ERROR;
    }

    TtkRedisplayWidget(&nb->core);

    return TCL_OK;
}

/* $nb insert $index $tab ?-option value ...?
 *	Insert new tab, or move existing one.
 */
static int NotebookInsertCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Notebook *nb = (Notebook *)recordPtr;
    Tcl_Size current = nb->notebook.currentIndex;
    Tcl_Size nContent = Ttk_NumberContent(nb->notebook.mgr);
    Tcl_Size srcIndex, destIndex;

    if (objc < 4) {
	Tcl_WrongNumArgs(interp, 2,objv, "index window ?-option value ...?");
	return TCL_ERROR;
    }

    if (Tcl_GetString(objv[3])[0] == '.') {
	/* Window name -- could be new or existing content window.
	 */
	Tk_Window window =
	    Tk_NameToWindow(interp,Tcl_GetString(objv[3]),nb->core.tkwin);

	if (!window) {
	    return TCL_ERROR;
	}

	srcIndex = Ttk_ContentIndex(nb->notebook.mgr, window);
	if (srcIndex < 0) {	/* New content window */
	    if (TCL_OK != Ttk_GetContentIndexFromObj(
		interp, nb->notebook.mgr, objv[2], 1, &destIndex)) {
		return TCL_ERROR;
	    }
	    return AddTab(interp, nb, destIndex, window, objc-4,objv+4);
	}
    } else if (Ttk_GetContentIndexFromObj(
		interp, nb->notebook.mgr, objv[3], 0, &srcIndex) != TCL_OK)
    {
	return TCL_ERROR;
    } else if (srcIndex  >= Ttk_NumberContent(nb->notebook.mgr)) {
	srcIndex = Ttk_NumberContent(nb->notebook.mgr) - 1;
    }

    if (TCL_OK != Ttk_GetContentIndexFromObj(
	interp, nb->notebook.mgr, objv[2], 0, &destIndex)) {
	return TCL_ERROR;
    }

    /* Move existing content window:
     */
    if (ConfigureTab(interp, nb,
	     (Tab *)Ttk_ContentData(nb->notebook.mgr, srcIndex),
		 Ttk_ContentWindow(nb->notebook.mgr, srcIndex),
	     objc-4,objv+4) != TCL_OK)
    {
	return TCL_ERROR;
    }

    if (destIndex  >= nContent) {
	destIndex  = nContent - 1;
    }
    Ttk_ReorderContent(nb->notebook.mgr, srcIndex, destIndex);

    /* Adjust internal indexes:
     */
    nb->notebook.activeIndex = TCL_INDEX_NONE;
    if (current == srcIndex) {
	nb->notebook.currentIndex = destIndex;
    } else if (destIndex <= current && current < srcIndex) {
	++nb->notebook.currentIndex;
    } else if (srcIndex < current && current <= destIndex) {
	--nb->notebook.currentIndex;
    }

    TtkRedisplayWidget(&nb->core);

    return TCL_OK;
}

/* $nb forget $tab --
 *	Removes the specified tab.
 */
static int NotebookForgetCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Notebook *nb = (Notebook *)recordPtr;
    Tcl_Size index;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "tab");
	return TCL_ERROR;
    }

    if (GetTabIndex(interp, nb, objv[2], &index) != TCL_OK) {
	return TCL_ERROR;
    }

    Ttk_ForgetContent(nb->notebook.mgr, index);
    TtkRedisplayWidget(&nb->core);

    return TCL_OK;
}

/* $nb hide $tab --
 *	Hides the specified tab.
 */
static int NotebookHideCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Notebook *nb = (Notebook *)recordPtr;
    Tcl_Size index;
    Tab *tab;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "tab");
	return TCL_ERROR;
    }

    if (GetTabIndex(interp, nb, objv[2], &index) != TCL_OK) {
	return TCL_ERROR;
    }

    tab = (Tab *)Ttk_ContentData(nb->notebook.mgr, index);
    tab->state = TAB_STATE_HIDDEN;
    if (index == nb->notebook.currentIndex) {
	SelectNearestTab(nb);
    } else {
	TtkRedisplayWidget(&nb->core);
    }

    return TCL_OK;
}

/* $nb identify $x $y --
 *	Returns name of tab element at $x,$y; empty string if none.
 */
static int NotebookIdentifyCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    static const char *const whatTable[] = { "element", "tab", NULL };
    enum { IDENTIFY_ELEMENT, IDENTIFY_TAB };
    int what = IDENTIFY_ELEMENT;
    Notebook *nb = (Notebook *)recordPtr;
    Ttk_Element element = NULL;
    int x, y;
    Tcl_Size tabIndex;

    if (objc < 4 || objc > 5) {
	Tcl_WrongNumArgs(interp, 2,objv, "?what? x y");
	return TCL_ERROR;
    }

    if (Tcl_GetIntFromObj(interp, objv[objc-2], &x) != TCL_OK
	|| Tcl_GetIntFromObj(interp, objv[objc-1], &y) != TCL_OK
	|| (objc == 5 && Tcl_GetIndexFromObjStruct(interp, objv[2], whatTable,
		sizeof(char *), "option", 0, &what) != TCL_OK)
    ) {
	return TCL_ERROR;
    }

    tabIndex = IdentifyTab(nb, x, y);
    if (tabIndex >= 0) {
	Tab *tab = (Tab *)Ttk_ContentData(nb->notebook.mgr, tabIndex);
	Ttk_State state = TabState(nb, tabIndex);
	Ttk_Layout tabLayout = nb->notebook.tabLayout;

	Ttk_RebindSublayout(tabLayout, tab);
	Ttk_PlaceLayout(tabLayout, state, tab->parcel);

	element = Ttk_IdentifyElement(tabLayout, x, y);
    }

    switch (what) {
	case IDENTIFY_ELEMENT:
	    if (element) {
		const char *elementName = Ttk_ElementName(element);

		Tcl_SetObjResult(interp, Tcl_NewStringObj(elementName, -1));
	    }
	    break;
	case IDENTIFY_TAB:
	    if (tabIndex >= 0) {
		Tcl_SetObjResult(interp, TkNewIndexObj(tabIndex));
	    }
	    break;
    }
    return TCL_OK;
}

/* $nb index $item --
 *	Returns the integer index of the tab specified by $item,
 *	the empty string if $item does not identify a tab.
 *	See above for valid item formats.
 */
static int NotebookIndexCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Notebook *nb = (Notebook *)recordPtr;
    Tcl_Size index;
    int status;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "tab");
	return TCL_ERROR;
    }

    status = FindTabIndex(interp, nb, objv[2], &index);
	if (status == TCL_OK) {
	    if (index >= 0) {
		Tcl_SetObjResult(interp, TkNewIndexObj(index));
	    }
    }

    return status;
}

/* $nb select ?$item? --
 *	Select the specified tab, or return the widget path of
 *	the currently-selected pane.
 */
static int NotebookSelectCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Notebook *nb = (Notebook *)recordPtr;

    if (objc == 2) {
	if (nb->notebook.currentIndex >= 0) {
	    Tk_Window pane = Ttk_ContentWindow(
		nb->notebook.mgr, nb->notebook.currentIndex);
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(Tk_PathName(pane), -1));
	}
	return TCL_OK;
    } else if (objc == 3) {
	Tcl_Size index;
	int status = GetTabIndex(interp, nb, objv[2], &index);
	if (status == TCL_OK) {
	    SelectTab(nb, index);
	}
	return status;
    } /*else*/
    Tcl_WrongNumArgs(interp, 2, objv, "?tab?");
    return TCL_ERROR;
}

/* $nb tabs --
 *	Return list of tabs.
 */
static int NotebookTabsCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Notebook *nb = (Notebook *)recordPtr;
    Ttk_Manager *mgr = nb->notebook.mgr;
    Tcl_Obj *result;
    Tcl_Size i;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 2, objv, "");
	return TCL_ERROR;
    }

    result = Tcl_NewListObj(0, NULL);
    for (i = 0; i < Ttk_NumberContent(mgr); ++i) {
	const char *pathName = Tk_PathName(Ttk_ContentWindow(mgr,i));

	Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj(pathName,-1));
    }
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/* $nb tab $tab ?-option ?value -option value...??
 */
static int NotebookTabCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Notebook *nb = (Notebook *)recordPtr;
    Ttk_Manager *mgr = nb->notebook.mgr;
    Tcl_Size index;
    Tk_Window window;
    Tab *tab;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "tab ?-option ?value??...");
	return TCL_ERROR;
    }

    if (GetTabIndex(interp, nb, objv[2], &index) != TCL_OK) {
	return TCL_ERROR;
    }

    tab = (Tab *)Ttk_ContentData(mgr, index);
    window = Ttk_ContentWindow(mgr, index);

    if (objc == 3) {
	return TtkEnumerateOptions(interp, tab,
	    PaneOptionSpecs, nb->notebook.paneOptionTable, window);
    } else if (objc == 4) {
	return TtkGetOptionValue(interp, tab, objv[3],
	    nb->notebook.paneOptionTable, window);
    } /* else */

    if (ConfigureTab(interp, nb, tab, window, objc-3,objv+3) != TCL_OK) {
	return TCL_ERROR;
    }

    /* If the current tab has become disabled or hidden,
     * select the next nondisabled, unhidden one:
     */
    if (index == nb->notebook.currentIndex && tab->state != TAB_STATE_NORMAL) {
	SelectNearestTab(nb);
    }

    return TCL_OK;
}

/* Subcommand table:
 */
static const Ttk_Ensemble NotebookCommands[] = {
    { "add",	NotebookAddCommand,0 },
    { "cget",		TtkWidgetCgetCommand,0 },
    { "configure",	TtkWidgetConfigureCommand,0 },
    { "forget",		NotebookForgetCommand,0 },
    { "hide",		NotebookHideCommand,0 },
    { "identify",	NotebookIdentifyCommand,0 },
    { "index",		NotebookIndexCommand,0 },
    { "insert",	NotebookInsertCommand,0 },
    { "instate",	TtkWidgetInstateCommand,0 },
    { "select",		NotebookSelectCommand,0 },
    { "state",	TtkWidgetStateCommand,0 },
    { "style",		TtkWidgetStyleCommand,0 },
    { "tab",		NotebookTabCommand,0 },
    { "tabs",	NotebookTabsCommand,0 },
    { 0,0,0 }
};

/*------------------------------------------------------------------------
 * +++ Widget class hooks.
 */

static void NotebookInitialize(Tcl_Interp *interp, void *recordPtr)
{
    Notebook *nb = (Notebook *)recordPtr;

    nb->notebook.mgr = Ttk_CreateManager(
	    &NotebookManagerSpec, recordPtr, nb->core.tkwin);

    nb->notebook.tabOptionTable = Tk_CreateOptionTable(interp,TabOptionSpecs);
    nb->notebook.paneOptionTable = Tk_CreateOptionTable(interp,PaneOptionSpecs);

    nb->notebook.currentIndex = TCL_INDEX_NONE;
    nb->notebook.activeIndex = TCL_INDEX_NONE;
    nb->notebook.tabLayout = 0;

    nb->notebook.clientArea = Ttk_MakeBox(0,0,1,1);

    Tk_CreateEventHandler(
	nb->core.tkwin, NotebookEventMask, NotebookEventHandler, recordPtr);
}

static void NotebookCleanup(void *recordPtr)
{
    Notebook *nb = (Notebook *)recordPtr;

    Ttk_DeleteManager(nb->notebook.mgr);
    if (nb->notebook.tabLayout)
	Ttk_FreeLayout(nb->notebook.tabLayout);
}

static int NotebookConfigure(Tcl_Interp *interp, void *clientData, int mask)
{
    Notebook *nb = (Notebook *)clientData;

    /*
     * Error-checks:
     */
    if (nb->notebook.paddingObj) {
	/* Check for valid -padding: */
	Ttk_Padding unused;
	if (Ttk_GetPaddingFromObj(
		    interp, nb->core.tkwin, nb->notebook.paddingObj, &unused)
		!= TCL_OK) {
	    return TCL_ERROR;
	}
    }

    return TtkCoreConfigure(interp, clientData, mask);
}

/* NotebookGetLayout  --
 *	GetLayout widget hook.
 */
static Ttk_Layout NotebookGetLayout(
    Tcl_Interp *interp, Ttk_Theme theme, void *recordPtr)
{
    Notebook *nb = (Notebook *)recordPtr;
    Ttk_Layout notebookLayout = TtkWidgetGetLayout(interp, theme, recordPtr);
    Ttk_Layout tabLayout;

    if (!notebookLayout) {
	return NULL;
    }

    tabLayout = Ttk_CreateSublayout(
	interp, theme, notebookLayout, ".Tab",	nb->notebook.tabOptionTable);

    if (tabLayout) {
	if (nb->notebook.tabLayout) {
	    Ttk_FreeLayout(nb->notebook.tabLayout);
	}
	nb->notebook.tabLayout = tabLayout;
    }

    return notebookLayout;
}

/*------------------------------------------------------------------------
 * +++ Display routines.
 */

static void DisplayTab(Notebook *nb, int index, Drawable d)
{
    Ttk_Layout tabLayout = nb->notebook.tabLayout;
    Tab *tab = (Tab *)Ttk_ContentData(nb->notebook.mgr, index);
    Ttk_State state = TabState(nb, index);

    if (tab->state != TAB_STATE_HIDDEN) {
	Ttk_RebindSublayout(tabLayout, tab);
	Ttk_PlaceLayout(tabLayout, state, tab->parcel);
	Ttk_DrawLayout(tabLayout, state, d);
    }
}

static void NotebookDisplay(void *clientData, Drawable d)
{
    Notebook *nb = (Notebook *)clientData;
    Tcl_Size nContent = Ttk_NumberContent(nb->notebook.mgr);
    Tcl_Size index;

    /* Draw notebook background (base layout):
     */
    Ttk_DrawLayout(nb->core.layout, nb->core.state, d);

    /* Draw tabs from left to right, but draw the current tab last
     * so it will overwrite its neighbors.
     */
    for (index = 0; index < nContent; ++index) {
	if (index != nb->notebook.currentIndex) {
	    DisplayTab(nb, index, d);
	}
    }
    if (nb->notebook.currentIndex >= 0) {
	DisplayTab(nb, nb->notebook.currentIndex, d);
    }
}

/*------------------------------------------------------------------------
 * +++ Widget specification and layout definitions.
 */

static const WidgetSpec NotebookWidgetSpec =
{
    "TNotebook",		/* className */
    sizeof(Notebook),		/* recordSize */
    NotebookOptionSpecs,	/* optionSpecs */
    NotebookCommands,		/* subcommands */
    NotebookInitialize,		/* initializeProc */
    NotebookCleanup,		/* cleanupProc */
    NotebookConfigure,		/* configureProc */
    TtkNullPostConfigure,	/* postConfigureProc */
    NotebookGetLayout,		/* getLayoutProc */
    NotebookSize,		/* geometryProc */
    NotebookDoLayout,		/* layoutProc */
    NotebookDisplay		/* displayProc */
};

TTK_BEGIN_LAYOUT(NotebookLayout)
    TTK_NODE("Notebook.client", TTK_FILL_BOTH)
TTK_END_LAYOUT

TTK_BEGIN_LAYOUT(TabLayout)
    TTK_GROUP("Notebook.tab", TTK_FILL_BOTH,
	TTK_GROUP("Notebook.padding", TTK_PACK_TOP|TTK_FILL_BOTH,
	    TTK_GROUP("Notebook.focus", TTK_PACK_TOP|TTK_FILL_BOTH,
		TTK_NODE("Notebook.label", TTK_PACK_TOP))))
TTK_END_LAYOUT

/*------------------------------------------------------------------------
 * +++ Initialization.
 */

MODULE_SCOPE void
TtkNotebook_Init(Tcl_Interp *interp)
{
    Ttk_Theme themePtr = Ttk_GetDefaultTheme(interp);

    Ttk_RegisterLayout(themePtr, "Tab", TabLayout);
    Ttk_RegisterLayout(themePtr, "TNotebook", NotebookLayout);

    RegisterWidget(interp, "ttk::notebook", &NotebookWidgetSpec);
}

/*EOF*/
