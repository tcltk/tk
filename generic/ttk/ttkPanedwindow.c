/*
 * Copyright © 2005 Joe English.  Freely redistributable.
 *
 * ttk::panedwindow widget implementation.
 *
 * TODO: track active/pressed sash.
 */

#include "tkInt.h"
#include "ttkManager.h"
#include "ttkTheme.h"
#include "ttkWidget.h"

/*------------------------------------------------------------------------
 * +++ Layout algorithm.
 *
 * (pos=x/y, size=width/height, depending on -orient=horizontal/vertical)
 *
 * Each pane carries two pieces of state: the request size and the
 * position of the following sash.  (The final pane has no sash,
 * its sash position is used as a sentinel value).
 *
 * Pane geometry is determined by the sash positions.
 * When resizing, sash positions are computed from the request sizes,
 * the available space, and pane weights (see PlaceSashes()).
 * This ensures continuous resize behavior (that is: changing
 * the size by X pixels then changing the size by Y pixels
 * gives the same result as changing the size by X+Y pixels
 * in one step).
 *
 * The request size is initially set to the content window's requested size.
 * When the user drags a sash, each pane's request size is set to its
 * actual size.  This ensures that panes "stay put" on the next resize.
 *
 * If reqSize == 0, use 0 for the weight as well.  This ensures that
 * "collapsed" panes stay collapsed during a resize, regardless of
 * their nominal -weight.
 *
 * +++ Invariants.
 *
 * #sash		=  #pane - 1
 * pos(pane[0])	=  0
 * pos(sash[i])	=  pos(pane[i]) + size(pane[i]), 0 <= i <= #sash
 * pos(pane[i+1])	=  pos(sash[i]) + size(sash[i]), 0 <= i <  #sash
 * pos(sash[#sash])	=  size(pw)   // sentinel value, constraint
 *
 * size(pw)		=  sum(size(pane(0..#pane))) + sum(size(sash(0..#sash)))
 * size(pane[i])	>= 0,  for 0 <= i < #pane
 * size(sash[i])	>= 0,  for 0 <= i < #sash
 * ==> pos(pane[i]) <= pos(sash[i]) <= pos(pane[i+1]), for 0 <= i < #sash
 *
 * Assumption: all sashes are the same size.
 */

/*------------------------------------------------------------------------
 * +++ Widget record.
 */

typedef struct {
    Tcl_Obj	*orientObj;
    int	orient;
    int	width;
    int	height;
    Ttk_Manager	*mgr;
    Tk_OptionTable paneOptionTable;
    Ttk_Layout	sashLayout;
    int	sashThickness;
} PanedPart;

typedef struct {
    WidgetCore	core;
    PanedPart	paned;
} Paned;

/* @@@ NOTE: -orient is readonly 'cause dynamic oriention changes NYI
 */
static const Tk_OptionSpec PanedOptionSpecs[] = {
    {TK_OPTION_STRING_TABLE, "-orient", "orient", "Orient", "vertical",
	offsetof(Paned,paned.orientObj), offsetof(Paned,paned.orient),
	0, ttkOrientStrings, READONLY_OPTION|STYLE_CHANGED },
    {TK_OPTION_INT, "-width", "width", "Width", "0",
	TCL_INDEX_NONE, offsetof(Paned, paned.width),
	0, 0, GEOMETRY_CHANGED },
    {TK_OPTION_INT, "-height", "height", "Height", "0",
	TCL_INDEX_NONE, offsetof(Paned, paned.height),
	0, 0, GEOMETRY_CHANGED },

    WIDGET_TAKEFOCUS_FALSE,
    WIDGET_INHERIT_OPTIONS(ttkCoreOptionSpecs)
};

/*------------------------------------------------------------------------
 * +++ Pane record.
 */
typedef struct {
    int	reqSize;		/* Pane request size */
    int	sashPos;		/* Folowing sash position */
    int	weight;		/* Pane -weight, for resizing */
} Pane;

static const Tk_OptionSpec PaneOptionSpecs[] = {
    {TK_OPTION_INT, "-weight", "weight", "Weight", "0",
	TCL_INDEX_NONE, offsetof(Pane,weight), 0,0,GEOMETRY_CHANGED },
    {TK_OPTION_END, 0,0,0, NULL, TCL_INDEX_NONE,TCL_INDEX_NONE, 0,0,0}
};

/* CreatePane --
 *	Create a new pane record.
 */
static Pane *CreatePane(Tcl_Interp *interp, Paned *pw, Tk_Window window)
{
    Tk_OptionTable optionTable = pw->paned.paneOptionTable;
    void *record = ckalloc(sizeof(Pane));
    Pane *pane = (Pane *)record;

    memset(record, 0, sizeof(Pane));
    if (Tk_InitOptions(interp, record, optionTable, window) != TCL_OK) {
	ckfree(record);
	return NULL;
    }

    pane->reqSize
	= pw->paned.orient == TTK_ORIENT_HORIZONTAL
	? Tk_ReqWidth(window) : Tk_ReqHeight(window);

    return pane;
}

/* DestroyPane --
 *	Free pane record.
 */
static void DestroyPane(Paned *pw, Pane *pane)
{
    void *record = pane;
    Tk_FreeConfigOptions(record, pw->paned.paneOptionTable, pw->core.tkwin);
    ckfree(record);
}

/* ConfigurePane --
 *	Set pane options.
 */
static int ConfigurePane(
    Tcl_Interp *interp, Paned *pw, Pane *pane, Tk_Window window,
    Tcl_Size objc, Tcl_Obj *const objv[])
{
    Ttk_Manager *mgr = pw->paned.mgr;
    Tk_SavedOptions savedOptions;
    int mask = 0;

    if (Tk_SetOptions(interp, pane, pw->paned.paneOptionTable,
	    objc, objv, window, &savedOptions, &mask) != TCL_OK)
    {
	return TCL_ERROR;
    }

    /* Sanity-check:
     */
    if (pane->weight < 0) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"-weight must be non-negative", -1));
	Tcl_SetErrorCode(interp, "TTK", "PANE", "WEIGHT", (char *)NULL);
	goto error;
    }

    /* Done.
     */
    Tk_FreeSavedOptions(&savedOptions);
    Ttk_ManagerSizeChanged(mgr);
    return TCL_OK;

error:
    Tk_RestoreSavedOptions(&savedOptions);
    return TCL_ERROR;
}


/*------------------------------------------------------------------------
 * +++ Sash adjustment.
 */

/* ShoveUp --
 *	Place sash i at specified position, recursively shoving
 *	previous sashes upwards as needed, until hitting the top
 *	of the window.  If that happens, shove back down.
 *
 *	Returns: final position of sash i.
 */

static int ShoveUp(Paned *pw, int i, int pos)
{
    Pane *pane = (Pane *)Ttk_ContentData(pw->paned.mgr, i);
    int sashThickness = pw->paned.sashThickness;

    if (i == 0) {
	if (pos < 0)
	    pos = 0;
    } else {
	Pane *prevPane = (Pane *)Ttk_ContentData(pw->paned.mgr, i-1);
	if (pos < prevPane->sashPos + sashThickness)
	    pos = ShoveUp(pw, i-1, pos - sashThickness) + sashThickness;
    }
    return pane->sashPos = pos;
}

/* ShoveDown --
 *	Same as ShoveUp, but going in the opposite direction
 *	and stopping at the sentinel sash.
 */
static int ShoveDown(Paned *pw, Tcl_Size i, int pos)
{
    Pane *pane = (Pane *)Ttk_ContentData(pw->paned.mgr,i);
    int sashThickness = pw->paned.sashThickness;

    if (i == Ttk_NumberContent(pw->paned.mgr) - 1) {
	pos = pane->sashPos; /* Sentinel value == container window size */
    } else {
	Pane *nextPane = (Pane *)Ttk_ContentData(pw->paned.mgr,i+1);
	if (pos + sashThickness > nextPane->sashPos)
	    pos = ShoveDown(pw, i+1, pos + sashThickness) - sashThickness;
    }
    return pane->sashPos = pos;
}

/* PanedSize --
 *	Compute the requested size of the paned widget
 *	from the individual pane request sizes.
 *
 *	Used as the WidgetSpec sizeProc and the ManagerSpec sizeProc.
 */
static int PanedSize(void *recordPtr, int *widthPtr, int *heightPtr)
{
    Paned *pw = (Paned *)recordPtr;
    int nPanes = Ttk_NumberContent(pw->paned.mgr);
    int nSashes = nPanes - 1;
    int sashThickness = pw->paned.sashThickness;
    int width = 0, height = 0;
    int index;

    if (pw->paned.orient == TTK_ORIENT_HORIZONTAL) {
	for (index = 0; index < nPanes; ++index) {
	    Pane *pane = (Pane *)Ttk_ContentData(pw->paned.mgr, index);
	    Tk_Window window = Ttk_ContentWindow(pw->paned.mgr, index);

	    if (height < Tk_ReqHeight(window))
		height = Tk_ReqHeight(window);
	    width += pane->reqSize;
	}
	width += nSashes * sashThickness;
    } else {
	for (index = 0; index < nPanes; ++index) {
	    Pane *pane = (Pane *)Ttk_ContentData(pw->paned.mgr, index);
	    Tk_Window window = Ttk_ContentWindow(pw->paned.mgr, index);

	    if (width < Tk_ReqWidth(window))
		width = Tk_ReqWidth(window);
	    height += pane->reqSize;
	}
	height += nSashes * sashThickness;
    }

    *widthPtr = pw->paned.width > 0 ? pw->paned.width : width;
    *heightPtr = pw->paned.height > 0 ? pw->paned.height : height;
    return 1;
}

/* AdjustPanes --
 *	Set pane request sizes from sash positions.
 *
 * NOTE:
 *	AdjustPanes followed by PlaceSashes (called during relayout)
 *	will leave the sashes in the same place, as long as available size
 *	remains contant.
 */
static void AdjustPanes(Paned *pw)
{
    int sashThickness = pw->paned.sashThickness;
    int pos = 0;
    Tcl_Size index;

    for (index = 0; index < Ttk_NumberContent(pw->paned.mgr); ++index) {
	Pane *pane = (Pane *)Ttk_ContentData(pw->paned.mgr, index);
	int size = pane->sashPos - pos;
	pane->reqSize = size >= 0 ? size : 0;
	pos = pane->sashPos + sashThickness;
    }
}

/* PlaceSashes --
 *	Set sash positions from pane request sizes and available space.
 *	The sentinel sash position is set to the available space.
 *
 *	Allocate pane->reqSize pixels to each pane, and distribute
 *	the difference = available size - requested size according
 *	to pane->weight.
 *
 *	If there's still some left over, squeeze panes from the bottom up
 *	(This can happen if all weights are zero, or if one or more panes
 *	are too small to absorb the required shrinkage).
 *
 * Notes:
 *	This doesn't distribute the remainder pixels as evenly as it could
 *	when more than one pane has weight > 1.
 */
static void PlaceSashes(Paned *pw, int width, int height)
{
    Ttk_Manager *mgr = pw->paned.mgr;
    int nPanes = Ttk_NumberContent(mgr);
    int sashThickness = pw->paned.sashThickness;
    int available = pw->paned.orient == TTK_ORIENT_HORIZONTAL ? width : height;
    int reqSize = 0, totalWeight = 0;
    int difference, delta, remainder, pos, i;

    if (nPanes == 0)
	return;

    /* Compute total required size and total available weight:
     */
    for (i = 0; i < nPanes; ++i) {
	Pane *pane = (Pane *)Ttk_ContentData(mgr, i);
	reqSize += pane->reqSize;
	totalWeight += pane->weight * (pane->reqSize != 0);
    }

    /* Compute difference to be redistributed:
     */
    difference = available - reqSize - sashThickness*(nPanes-1);
    if (totalWeight != 0) {
	delta = difference / totalWeight;
	remainder = difference % totalWeight;
	if (remainder < 0) {
	    --delta;
	    remainder += totalWeight;
	}
    } else {
	delta = remainder = 0;
    }
    /* ASSERT: 0 <= remainder < totalWeight */

    /* Place sashes:
     */
    pos = 0;
    for (i = 0; i < nPanes; ++i) {
	Pane *pane = (Pane *)Ttk_ContentData(mgr, i);
	int weight = pane->weight * (pane->reqSize != 0);
	int size = pane->reqSize + delta * weight;

	if (weight > remainder)
	    weight = remainder;
	remainder -= weight;
	size += weight;

	if (size < 0)
	    size = 0;

	pane->sashPos = (pos += size);
	pos += sashThickness;
    }

    /* Handle emergency shrink/emergency stretch:
     * Set sentinel sash position to end of widget,
     * shove preceding sashes up.
     */
    ShoveUp(pw, nPanes - 1, available);
}

/* PlacePanes --
 *	Places panes based on sash positions.
 */
static void PlacePanes(Paned *pw)
{
    int horizontal = pw->paned.orient == TTK_ORIENT_HORIZONTAL;
    int width = Tk_Width(pw->core.tkwin), height = Tk_Height(pw->core.tkwin);
    int sashThickness = pw->paned.sashThickness;
    int pos = 0;
    Tcl_Size index;

    for (index = 0; index < Ttk_NumberContent(pw->paned.mgr); ++index) {
	Pane *pane = (Pane *)Ttk_ContentData(pw->paned.mgr, index);
	int size = pane->sashPos - pos;

	if (size > 0) {
	    if (horizontal) {
		Ttk_PlaceContent(pw->paned.mgr, index, pos, 0, size, height);
	    } else {
		Ttk_PlaceContent(pw->paned.mgr, index, 0, pos, width, size);
	    }
	} else {
	    Ttk_UnmapContent(pw->paned.mgr, index);
	}

	pos = pane->sashPos + sashThickness;
    }
}

/*------------------------------------------------------------------------
 * +++ Manager specification.
 */

static void PanedPlaceContent(void *managerData)
{
    Paned *pw = (Paned *)managerData;
    PlaceSashes(pw, Tk_Width(pw->core.tkwin), Tk_Height(pw->core.tkwin));
    PlacePanes(pw);
}

static void PaneRemoved(void *managerData, Tcl_Size index)
{
    Paned *pw = (Paned *)managerData;
    Pane *pane = (Pane *)Ttk_ContentData(pw->paned.mgr, index);
    DestroyPane(pw, pane);
}

static int AddPane(
    Tcl_Interp *interp, Paned *pw,
    int destIndex, Tk_Window window,
    Tcl_Size objc, Tcl_Obj *const objv[])
{
    Pane *pane;
    if (!Ttk_Maintainable(interp, window, pw->core.tkwin)) {
	return TCL_ERROR;
    }
    if (Ttk_ContentIndex(pw->paned.mgr, window) >= 0) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"%s already added", Tk_PathName(window)));
	Tcl_SetErrorCode(interp, "TTK", "PANE", "PRESENT", (char *)NULL);
	return TCL_ERROR;
    }

    pane = CreatePane(interp, pw, window);
    if (!pane) {
	return TCL_ERROR;
    }
    if (ConfigurePane(interp, pw, pane, window, objc, objv) != TCL_OK) {
	DestroyPane(pw, pane);
	return TCL_ERROR;
    }

    Ttk_InsertContent(pw->paned.mgr, destIndex, window, pane);
    return TCL_OK;
}

/* PaneRequest --
 *	Only update pane request size if pane is currently unmapped.
 *	Geometry requests from mapped panes are not directly honored
 *	in order to avoid unexpected pane resizes (esp. while the
 *	user is dragging a sash [#1325286]).
 */
static int PaneRequest(void *managerData, Tcl_Size index, int width, int height)
{
    Paned *pw = (Paned *)managerData;
    Pane *pane = (Pane *)Ttk_ContentData(pw->paned.mgr, index);
    Tk_Window window = Ttk_ContentWindow(pw->paned.mgr, index);
    int horizontal = pw->paned.orient == TTK_ORIENT_HORIZONTAL;

    if (!Tk_IsMapped(window)) {
	pane->reqSize = horizontal ? width : height;
    }
    return 1;
}

static const Ttk_ManagerSpec PanedManagerSpec = {
    { "panedwindow", Ttk_GeometryRequestProc, Ttk_LostContentProc },
    PanedSize,
    PanedPlaceContent,
    PaneRequest,
    PaneRemoved
};

/*------------------------------------------------------------------------
 * +++ Event handler.
 *
 * This event handler generates an <<EnteredChild>> virtual event
 * on LeaveNotify/NotifyInferior.
 * This was originally introduced because Tk used to discard events with
 * detail field NotifyInferior. The <<EnteredChild>> event was then used
 * to reset the cursor when the pointer crosses from a parent to a child.
 * Since ticket #47d4f29159, LeaveNotify/NotifyInferior are no longer
 * discarded: the <Leave> event will trigger even with NotifyInferior
 * detail field. The generated <<EnteredChild>> is nevertheless kept for
 * backwards compatibility purpose since it is publicly documented,
 * meaning that someone could bind to it.
 */

static const unsigned PanedEventMask = LeaveWindowMask;
static void PanedEventProc(void *clientData, XEvent *eventPtr)
{
    WidgetCore *corePtr = (WidgetCore *)clientData;
    if (   eventPtr->type == LeaveNotify
	&& eventPtr->xcrossing.detail == NotifyInferior)
    {
	Tk_SendVirtualEvent(corePtr->tkwin, "EnteredChild", NULL);
    }
}

/*------------------------------------------------------------------------
 * +++ Initialization and cleanup hooks.
 */

static void PanedInitialize(Tcl_Interp *interp, void *recordPtr)
{
    Paned *pw = (Paned *)recordPtr;

    Tk_CreateEventHandler(pw->core.tkwin,
	PanedEventMask, PanedEventProc, recordPtr);
    pw->paned.mgr = Ttk_CreateManager(&PanedManagerSpec, pw, pw->core.tkwin);
    pw->paned.paneOptionTable = Tk_CreateOptionTable(interp,PaneOptionSpecs);
    pw->paned.sashLayout = 0;
    pw->paned.sashThickness = 1;
}

static void PanedCleanup(void *recordPtr)
{
    Paned *pw = (Paned *)recordPtr;

    if (pw->paned.sashLayout)
	Ttk_FreeLayout(pw->paned.sashLayout);
    Tk_DeleteEventHandler(pw->core.tkwin,
	PanedEventMask, PanedEventProc, recordPtr);
    Ttk_DeleteManager(pw->paned.mgr);
}

/* Post-configuration hook.
 */
static int PanedPostConfigure(
    TCL_UNUSED(Tcl_Interp *),
    void *clientData,
    int mask)
{
    Paned *pw = (Paned *)clientData;

    if (mask & GEOMETRY_CHANGED) {
	/* User has changed -width or -height.
	 * Recalculate sash positions based on requested size.
	 */
	Tk_Window tkwin = pw->core.tkwin;
	PlaceSashes(pw,
	    pw->paned.width > 0 ? pw->paned.width : Tk_Width(tkwin),
	    pw->paned.height > 0 ? pw->paned.height : Tk_Height(tkwin));
    }

    return TCL_OK;
}

/*------------------------------------------------------------------------
 * +++ Layout management hooks.
 */
static Ttk_Layout PanedGetLayout(
    Tcl_Interp *interp, Ttk_Theme themePtr, void *recordPtr)
{
    Paned *pw = (Paned *)recordPtr;
    Ttk_Layout panedLayout = TtkWidgetGetLayout(interp, themePtr, recordPtr);

    if (panedLayout) {
	int horizontal = pw->paned.orient == TTK_ORIENT_HORIZONTAL;
	const char *layoutName =
	    horizontal ? ".Vertical.Sash" : ".Horizontal.Sash";
	Ttk_Layout sashLayout = Ttk_CreateSublayout(
	    interp, themePtr, panedLayout, layoutName, pw->core.optionTable);

	if (sashLayout) {
	    int sashWidth, sashHeight;

	    Ttk_LayoutSize(sashLayout, 0, &sashWidth, &sashHeight);
	    pw->paned.sashThickness = horizontal ? sashWidth : sashHeight;

	    if (pw->paned.sashLayout)
		Ttk_FreeLayout(pw->paned.sashLayout);
	    pw->paned.sashLayout = sashLayout;
	} else {
	    Ttk_FreeLayout(panedLayout);
	    return 0;
	}
    }

    return panedLayout;
}

/*------------------------------------------------------------------------
 * +++ Drawing routines.
 */

/* SashLayout --
 *	Place the sash sublayout after the specified pane,
 *	in preparation for drawing.
 */
static Ttk_Layout SashLayout(Paned *pw, int index)
{
    Pane *pane = (Pane *)Ttk_ContentData(pw->paned.mgr, index);
    int thickness = pw->paned.sashThickness,
	height = Tk_Height(pw->core.tkwin),
	width = Tk_Width(pw->core.tkwin),
	sashPos = pane->sashPos;

    Ttk_PlaceLayout(
	pw->paned.sashLayout, pw->core.state,
	pw->paned.orient == TTK_ORIENT_HORIZONTAL
	    ? Ttk_MakeBox(sashPos, 0, thickness, height)
	    : Ttk_MakeBox(0, sashPos, width, thickness));

    return pw->paned.sashLayout;
}

static void DrawSash(Paned *pw, int index, Drawable d)
{
    Ttk_DrawLayout(SashLayout(pw, index), pw->core.state, d);
}

static void PanedDisplay(void *recordPtr, Drawable d)
{
    Paned *pw = (Paned *)recordPtr;
    Tcl_Size i, nContent = Ttk_NumberContent(pw->paned.mgr);

    TtkWidgetDisplay(recordPtr, d);
    for (i = 1; i < nContent; ++i) {
	DrawSash(pw, i - 1, d);
    }
}

/*------------------------------------------------------------------------
 * +++ Widget commands.
 */

/* $pw add window [ options ... ]
 */
static int PanedAddCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Paned *pw = (Paned *)recordPtr;
    Tk_Window window;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "window");
	return TCL_ERROR;
    }

    window = Tk_NameToWindow(
	interp, Tcl_GetString(objv[2]), pw->core.tkwin);

    if (!window) {
	return TCL_ERROR;
    }

    return AddPane(interp, pw, Ttk_NumberContent(pw->paned.mgr), window,
	    objc - 3, objv + 3);
}

/* $pw insert $index $window ?-option value ...?
 *	Insert new content window, or move existing one.
 */
static int PanedInsertCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Paned *pw = (Paned *)recordPtr;
    Tcl_Size nContent = Ttk_NumberContent(pw->paned.mgr);
    Tcl_Size srcIndex, destIndex;
    Tk_Window window;

    if (objc < 4) {
	Tcl_WrongNumArgs(interp, 2,objv, "index window ?-option value ...?");
	return TCL_ERROR;
    }

    window = Tk_NameToWindow(
	interp, Tcl_GetString(objv[3]), pw->core.tkwin);
    if (!window) {
	return TCL_ERROR;
    }

    if (TCL_OK != Ttk_GetContentIndexFromObj(
		interp,pw->paned.mgr, objv[2], 1, &destIndex))
    {
	return TCL_ERROR;
    }

    srcIndex = Ttk_ContentIndex(pw->paned.mgr, window);
    if (srcIndex < 0) { /* New content: */
	return AddPane(interp, pw, destIndex, window, objc-4, objv+4);
    } /* else -- move existing content: */

    if (destIndex >= nContent)
	destIndex  = nContent - 1;
    Ttk_ReorderContent(pw->paned.mgr, srcIndex, destIndex);

    return objc == 4 ? TCL_OK :
	ConfigurePane(interp, pw,
		(Pane *)Ttk_ContentData(pw->paned.mgr, destIndex),
		Ttk_ContentWindow(pw->paned.mgr, destIndex),
		objc-4, objv+4);
}

/* $pw forget $pane
 */
static int PanedForgetCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Paned *pw = (Paned *)recordPtr;
    Tcl_Size paneIndex;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2,objv, "pane");
	return TCL_ERROR;
    }

    if (TCL_OK != Ttk_GetContentIndexFromObj(
		    interp, pw->paned.mgr, objv[2], 0, &paneIndex))
    {
	return TCL_ERROR;
    } else if (paneIndex >= Ttk_NumberContent(pw->paned.mgr)) {
	paneIndex = Ttk_NumberContent(pw->paned.mgr) - 1;
    }
    Ttk_ForgetContent(pw->paned.mgr, paneIndex);

    return TCL_OK;
}

/* $pw identify ?what? $x $y --
 *	Return index of sash at $x,$y
 */
static int PanedIdentifyCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    static const char *const whatTable[] = { "element", "sash", NULL };
    enum { IDENTIFY_ELEMENT, IDENTIFY_SASH };
    int what = IDENTIFY_SASH;
    Paned *pw = (Paned *)recordPtr;
    int sashThickness = pw->paned.sashThickness;
    int nSashes = Ttk_NumberContent(pw->paned.mgr) - 1;
    int x, y, pos;
    int index;

    if (objc < 4 || objc > 5) {
	Tcl_WrongNumArgs(interp, 2,objv, "?what? x y");
	return TCL_ERROR;
    }

    if (Tcl_GetIntFromObj(interp, objv[objc-2], &x) != TCL_OK
	    || Tcl_GetIntFromObj(interp, objv[objc-1], &y) != TCL_OK
	    || (objc == 5 && Tcl_GetIndexFromObjStruct(interp, objv[2], whatTable,
	    sizeof(char *), "option", 0, &what) != TCL_OK)) {
	return TCL_ERROR;
    }

    pos = pw->paned.orient == TTK_ORIENT_HORIZONTAL ? x : y;
    for (index = 0; index < nSashes; ++index) {
	Pane *pane = (Pane *)Ttk_ContentData(pw->paned.mgr, index);
	if (pane->sashPos <= pos && pos <= pane->sashPos + sashThickness) {
	    /* Found it. */
	    switch (what) {
		case IDENTIFY_SASH:
		    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(index));
		    return TCL_OK;
		case IDENTIFY_ELEMENT:
		{
		    Ttk_Element element =
			Ttk_IdentifyElement(SashLayout(pw, index), x, y);
		    if (element) {
			Tcl_SetObjResult(interp,
			    Tcl_NewStringObj(Ttk_ElementName(element), -1));
		    }
		    return TCL_OK;
		}
	    }
	}
    }

    return TCL_OK; /* nothing found - return empty string */
}

/* $pw pane $pane ?-option ?value -option value ...??
 *	Query/modify pane options.
 */
static int PanedPaneCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Paned *pw = (Paned *)recordPtr;
    Tcl_Size paneIndex;
    Tk_Window window;
    Pane *pane;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2,objv, "pane ?-option value ...?");
	return TCL_ERROR;
    }

    if (TCL_OK != Ttk_GetContentIndexFromObj(
		    interp,pw->paned.mgr, objv[2], 0, &paneIndex))
    {
	return TCL_ERROR;
    } else if (paneIndex >= Ttk_NumberContent(pw->paned.mgr)) {
	paneIndex = Ttk_NumberContent(pw->paned.mgr) - 1;
    }

    pane = (Pane *)Ttk_ContentData(pw->paned.mgr, paneIndex);
    window = Ttk_ContentWindow(pw->paned.mgr, paneIndex);

    switch (objc) {
	case 3:
	    return TtkEnumerateOptions(interp, pane, PaneOptionSpecs,
			pw->paned.paneOptionTable, window);
	case 4:
	    return TtkGetOptionValue(interp, pane, objv[3],
			pw->paned.paneOptionTable, window);
	default:
	    return ConfigurePane(interp, pw, pane, window, objc-3,objv+3);
    }
}

/* $pw panes --
 *	Return list of managed panes.
 */
static int PanedPanesCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Paned *pw = (Paned *)recordPtr;
    Ttk_Manager *mgr = pw->paned.mgr;
    Tcl_Obj *panes;
    Tcl_Size i;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 2, objv, "");
	return TCL_ERROR;
    }

    panes = Tcl_NewListObj(0, NULL);
    for (i = 0; i < Ttk_NumberContent(mgr); ++i) {
	const char *pathName = Tk_PathName(Ttk_ContentWindow(mgr,i));
	Tcl_ListObjAppendElement(interp, panes, Tcl_NewStringObj(pathName,-1));
    }
    Tcl_SetObjResult(interp, panes);

    return TCL_OK;
}


/* $pw sashpos $index ?$newpos?
 *	Query or modify sash position.
 */
static int PanedSashposCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Paned *pw = (Paned *)recordPtr;
    Tcl_WideInt sashIndex, position = -1;
    Pane *pane;

    if (objc < 3 || objc > 4) {
	Tcl_WrongNumArgs(interp, 2,objv, "index ?newpos?");
	return TCL_ERROR;
    }
    if (Tcl_GetWideIntFromObj(interp, objv[2], &sashIndex) != TCL_OK) {
	return TCL_ERROR;
    }
    if (sashIndex < 0 || sashIndex >= Ttk_NumberContent(pw->paned.mgr) - 1) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "sash index %" TCL_LL_MODIFIER "d out of range", sashIndex));
	Tcl_SetErrorCode(interp, "TTK", "PANE", "SASH_INDEX", (char *)NULL);
	return TCL_ERROR;
    }

    pane = (Pane *)Ttk_ContentData(pw->paned.mgr, sashIndex);

    if (objc == 3) {
	Tcl_SetObjResult(interp, Tcl_NewWideIntObj(pane->sashPos));
	return TCL_OK;
    }
    /* else -- set new sash position */

    if (Tcl_GetWideIntFromObj(interp, objv[3], &position) != TCL_OK) {
	return TCL_ERROR;
    }

    if (position < pane->sashPos) {
	ShoveUp(pw, sashIndex, position);
    } else {
	ShoveDown(pw, sashIndex, position);
    }

    AdjustPanes(pw);
    Ttk_ManagerLayoutChanged(pw->paned.mgr);

    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(pane->sashPos));
    return TCL_OK;
}

static const Ttk_Ensemble PanedCommands[] = {
    { "add",		PanedAddCommand,0 },
    { "cget",		TtkWidgetCgetCommand,0 },
    { "configure",	TtkWidgetConfigureCommand,0 },
    { "forget",	PanedForgetCommand,0 },
    { "identify",	PanedIdentifyCommand,0 },
    { "insert",	PanedInsertCommand,0 },
    { "instate",	TtkWidgetInstateCommand,0 },
    { "pane",	PanedPaneCommand,0 },
    { "panes",	PanedPanesCommand,0 },
    { "sashpos",	PanedSashposCommand,0 },
    { "state",	TtkWidgetStateCommand,0 },
    { "style",		TtkWidgetStyleCommand,0 },
    { 0,0,0 }
};

/*------------------------------------------------------------------------
 * +++ Widget specification.
 */

static const WidgetSpec PanedWidgetSpec =
{
    "TPanedwindow",		/* className */
    sizeof(Paned),		/* recordSize */
    PanedOptionSpecs,		/* optionSpecs */
    PanedCommands,		/* subcommands */
    PanedInitialize,		/* initializeProc */
    PanedCleanup,		/* cleanupProc */
    TtkCoreConfigure,		/* configureProc */
    PanedPostConfigure,	/* postConfigureProc */
    PanedGetLayout,		/* getLayoutProc */
    PanedSize,			/* sizeProc */
    TtkWidgetDoLayout,		/* layoutProc */
    PanedDisplay		/* displayProc */
};

/*------------------------------------------------------------------------
 * +++ Elements and layouts.
 */

static const int DEFAULT_SASH_THICKNESS = 5;

typedef struct {
    Tcl_Obj *thicknessObj;
} SashElement;

static const Ttk_ElementOptionSpec SashElementOptions[] = {
    { "-sashthickness", TK_OPTION_PIXELS,
	offsetof(SashElement,thicknessObj), "3.75p" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void SashElementSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    SashElement *sash = (SashElement *)elementRecord;
    int thickness = DEFAULT_SASH_THICKNESS;

    Tk_GetPixelsFromObj(NULL, tkwin, sash->thicknessObj, &thickness);
    *widthPtr = *heightPtr = thickness;
}

static const Ttk_ElementSpec SashElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(SashElement),
    SashElementOptions,
    SashElementSize,
    TtkNullElementDraw
};

static const int DEFAULT_GRIP_SIZE = 20;

typedef struct {
    Tcl_Obj	*borderObj;
    Tcl_Obj	*gripSizeObj;
} GripElement;

static const Ttk_ElementOptionSpec GripElementOptions[] = {
    { "-background", TK_OPTION_BORDER,
	offsetof(GripElement,borderObj), DEFAULT_BACKGROUND },
    { "-gripsize", TK_OPTION_PIXELS,
	offsetof(GripElement,gripSizeObj), "15p" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void GripElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    TCL_UNUSED(Ttk_Padding *))
{
    Ttk_Orient orient = (Ttk_Orient)PTR2INT(clientData);
    GripElement *grip = (GripElement *)elementRecord;
    int gripSize = DEFAULT_GRIP_SIZE;

    Tk_GetPixelsFromObj(NULL, tkwin, grip->gripSizeObj, &gripSize);

    if (orient == TTK_ORIENT_HORIZONTAL) {
	*widthPtr = gripSize;
    } else {
	*heightPtr = gripSize;
    }
}

static void GripElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    Ttk_Orient orient = (Ttk_Orient)PTR2INT(clientData);
    GripElement *grip = (GripElement *)elementRecord;
    Tk_3DBorder border = Tk_Get3DBorderFromObj(tkwin, grip->borderObj);
    GC darkGC = Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC);
    int gripSize = DEFAULT_GRIP_SIZE, gripPad = 1;

    Tk_GetPixelsFromObj(NULL, tkwin, grip->gripSizeObj, &gripSize);

    if (orient == TTK_ORIENT_HORIZONTAL) {
	XFillRectangle(Tk_Display(tkwin), d, darkGC,
		b.x + (b.width - gripSize) / 2, b.y + gripPad,
		gripSize, b.height - 2 * gripPad);
    } else {
	XFillRectangle(Tk_Display(tkwin), d, darkGC,
		b.x + gripPad, b.y + (b.height - gripSize) / 2,
		b.width - 2 * gripPad, gripSize);
    }
}

static const Ttk_ElementSpec GripElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(GripElement),
    GripElementOptions,
    GripElementSize,
    GripElementDraw
};

TTK_BEGIN_LAYOUT(PanedLayout)
    TTK_NODE("Panedwindow.background", 0)/* @@@ BUG: empty layouts don't work */
TTK_END_LAYOUT

TTK_BEGIN_LAYOUT(HorizontalSashLayout)
    TTK_GROUP("Sash.hsash", TTK_FILL_BOTH,
	TTK_NODE("Sash.hgrip", TTK_FILL_BOTH))
TTK_END_LAYOUT

TTK_BEGIN_LAYOUT(VerticalSashLayout)
    TTK_GROUP("Sash.vsash", TTK_FILL_BOTH,
	TTK_NODE("Sash.vgrip", TTK_FILL_BOTH))
TTK_END_LAYOUT

/*------------------------------------------------------------------------
 * +++ Registration routine.
 */

MODULE_SCOPE void
TtkPanedwindow_Init(Tcl_Interp *interp)
{
    Ttk_Theme themePtr = Ttk_GetDefaultTheme(interp);
    RegisterWidget(interp, "ttk::panedwindow", &PanedWidgetSpec);

    Ttk_RegisterElement(interp, themePtr, "hsash", &SashElementSpec, 0);
    Ttk_RegisterElement(interp, themePtr, "vsash", &SashElementSpec, 0);
    Ttk_RegisterElement(interp, themePtr, "hgrip",
	    &GripElementSpec,  INT2PTR(TTK_ORIENT_HORIZONTAL));
    Ttk_RegisterElement(interp, themePtr, "vgrip",
	    &GripElementSpec,  INT2PTR(TTK_ORIENT_VERTICAL));

    Ttk_RegisterLayout(themePtr, "TPanedwindow", PanedLayout);
    Ttk_RegisterLayout(themePtr, "Horizontal.Sash", HorizontalSashLayout);
    Ttk_RegisterLayout(themePtr, "Vertical.Sash", VerticalSashLayout);
}

