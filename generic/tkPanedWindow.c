/*
 * tkPanedWindow.c --
 *
 *	This module implements "paned window" widgets that are object based. A
 *	"paned window" is a widget that manages the geometry for some number
 *	of other widgets, placing a movable "sash" between them, which can be
 *	used to alter the relative sizes of adjacent widgets.
 *
 * Copyright © 1997 Sun Microsystems, Inc.
 * Copyright © 2000 Ajuba Solutions.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "default.h"

/*
 * Flag values for "sticky"ness. The 16 combinations subsume the packer's
 * notion of anchor and fill.
 *
 * STICK_NORTH		This window sticks to the top of its cavity.
 * STICK_EAST		This window sticks to the right edge of its cavity.
 * STICK_SOUTH		This window sticks to the bottom of its cavity.
 * STICK_WEST		This window sticks to the left edge of its cavity.
 */

#define STICK_NORTH		1
#define STICK_EAST		2
#define STICK_SOUTH		4
#define STICK_WEST		8

/*
 * The following table defines the legal values for the -orient option.
 */

static const char *const orientStrings[] = {
    "horizontal", "vertical", NULL
};

enum orient { ORIENT_HORIZONTAL, ORIENT_VERTICAL };

/*
 * The following table defines the legal values for the -stretch option.
 */

static const char *const stretchStrings[] = {
    "always", "first", "last", "middle", "never", NULL
};

enum stretch {
    STRETCH_ALWAYS,		/* Always give extra space to this pane. */
    STRETCH_FIRST,		/* Give extra space to pane if it is first. */
    STRETCH_LAST,		/* Give extra space to pane if it is last. */
    STRETCH_MIDDLE,		/* Give extra space to pane only if it is
				 * neither first nor last. */
    STRETCH_NEVER		/* Never give extra space to this pane. */
};

/*
 * Codify the stretchiness rule in one place.
 */

#define IsStretchable(stretch,index,first,last)			\
    (((stretch) == STRETCH_ALWAYS) ||				\
     ((stretch) == STRETCH_FIRST && (index) == (first)) ||	\
     ((stretch) == STRETCH_LAST && (index) == (last)) ||	\
     ((stretch) == STRETCH_MIDDLE && (index) != (first) && (index) != (last)))

typedef struct {
    Tk_OptionTable pwOptions;	/* Token for paned window option table. */
    Tk_OptionTable paneOpts;	/* Token for pane cget option table. */
} OptionTables;

/*
 * One structure of the following type is kept for each window
 * managed by a paned window widget.
 */

typedef struct Pane {
    Tk_Window tkwin;		/* Window being managed. */
    Tcl_Obj *minSizeObj;		/* Minimum size of this pane, on the relevant
				 * axis, in pixels. */
    Tcl_Obj *padXObj;		/* Additional padding requested for pane, in
				 * the x dimension. */
    Tcl_Obj *padYObj;		/* Additional padding requested for pane, in
				 * the y dimension. */
    Tcl_Obj *widthObj, *heightObj;
				/* Tcl_Obj rep's of pane width/height, to
				 * allow for null values. */
    int sticky;			/* Sticky string. */
    int x, y;			/* Coordinates of the widget. */
    int paneWidth, paneHeight;	/* Pane dimensions (may be different from
				 * pane width/height). */
    int sashx, sashy;		/* Coordinates of the sash of the right or
				 * bottom of this pane. */
    int markx, marky;		/* Coordinates of the last mark set for the
				 * sash. */
    int handlex, handley;	/* Coordinates of the sash handle. */
    enum stretch stretch;	/* Controls how pane grows/shrinks */
    int hide;			/* Controls visibility of pane */
    struct PanedWindow *containerPtr;
				/* Paned window managing the window. */
    Tk_Window after;		/* Placeholder for parsing options. */
    Tk_Window before;		/* Placeholder for parsing options. */
    int width;			/* Pane width. Same as widthObj, but updatable */
    int height;			/* Pane height. Same as heightObj, but updatable */
} Pane;

/*
 * A data structure of the following type is kept for each paned window widget
 * managed by this file:
 */

typedef struct PanedWindow {
    Tk_Window tkwin;		/* Window that embodies the paned window. */
    Tk_Window proxywin;		/* Window for the resizing proxy. */
    Display *display;		/* X's token for the window's display. */
    Tcl_Interp *interp;		/* Interpreter associated with widget. */
    Tcl_Command widgetCmd;	/* Token for square's widget command. */
    Tk_OptionTable optionTable;	/* Token representing the configuration
				 * specifications. */
    Tk_OptionTable paneOpts;	/* Token for pane cget table. */
    Tk_3DBorder background;	/* Background color. */
    Tcl_Obj *borderWidthObj;
    int relief;			/* 3D border effect (TK_RELIEF_RAISED, etc) */
    Tcl_Obj *widthObj;		/* Tcl_Obj rep for width. */
    Tcl_Obj *heightObj;		/* Tcl_Obj rep for height. */
    enum orient orient;		/* Orientation of the widget. */
    Tk_Cursor cursor;		/* Current cursor for window, or None. */
    int resizeOpaque;		/* Boolean indicating whether resize should be
				 * opaque or rubberband style. */
    int sashRelief;		/* Relief used to draw sash. */
    Tcl_Obj *sashWidthObj;	/* Tcl_Obj rep for sash width. */
    Tcl_Obj *sashPadObj;	/* Tcl_Obj rep for sash padding. */
    int showHandle;		/* Boolean indicating whether sash handles
				 * should be drawn. */
    Tcl_Obj *handleSizeObj;	/* Size of one side of a sash handle (handles
				 * are square), in pixels. */
    Tcl_Obj *handlePadObj;	/* Distance from border to draw handle. */
    Tk_Cursor sashCursor;	/* Cursor used when mouse is above a sash. */
    GC gc;			/* Graphics context for copying from
				 * off-screen pixmap onto screen. */
    int proxyx, proxyy;		/* Proxy x,y coordinates. */
    Tk_3DBorder proxyBackground;/* Background color used to draw proxy. If NULL, use background. */
    Tcl_Obj *proxyBorderWidthObj; /* Tcl_Obj rep for proxyBorderWidth */
    int proxyRelief;		/* Relief used to draw proxy, if TK_RELIEF_NULL then use relief. */
    Pane **panes;		/* Pointer to array of Panes. */
    int numPanes;		/* Number of panes. */
    int sizeofPanes;		/* Number of elements in the panes array. */
    int flags;			/* Flags for widget; see below. */
} PanedWindow;

/*
 * Flags used for paned windows:
 *
 * REDRAW_PENDING:		Non-zero means a DoWhenIdle handler has been
 *				queued to redraw this window.
 *
 * WIDGET_DELETED:		Non-zero means that the paned window has been,
 *				or is in the process of being, deleted.
 *
 * RESIZE_PENDING:		Non-zero means that the window might need to
 *				change its size (or the size of its panes)
 *				because of a change in the size of one of its
 *				children.
 */

#define REDRAW_PENDING		0x0001
#define WIDGET_DELETED		0x0002
#define REQUESTED_RELAYOUT	0x0004
#define RECOMPUTE_GEOMETRY	0x0008
#define PROXY_REDRAW_PENDING	0x0010
#define RESIZE_PENDING		0x0020

/*
 * Forward declarations for functions defined later in this file:
 */

static void		PanedWindowCmdDeletedProc(void *clientData);
static int		ConfigurePanedWindow(Tcl_Interp *interp,
			    PanedWindow *pwPtr, int objc,
			    Tcl_Obj *const objv[]);
static void		DestroyPanedWindow(PanedWindow *pwPtr);
static void		DisplayPanedWindow(void *clientData);
static void		PanedWindowEventProc(void *clientData,
			    XEvent *eventPtr);
static void		ProxyWindowEventProc(void *clientData,
			    XEvent *eventPtr);
static void		DisplayProxyWindow(void *clientData);
static void		PanedWindowWorldChanged(void *instanceData);
static Tcl_ObjCmdProc2 PanedWindowWidgetObjCmd;
static void		PanedWindowLostPaneProc(void *clientData,
			    Tk_Window tkwin);
static void		PanedWindowReqProc(void *clientData,
			    Tk_Window tkwin);
static void		ArrangePanes(void *clientData);
static void		Unlink(Pane *panePtr);
static Pane *		GetPane(PanedWindow *pwPtr, Tk_Window tkwin);
static void		GetFirstLastVisiblePane(PanedWindow *pwPtr,
			    int *firstPtr, int *lastPtr);
static void		PaneStructureProc(void *clientData,
			    XEvent *eventPtr);
static int		PanedWindowSashCommand(PanedWindow *pwPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj * const objv[]);
static int		PanedWindowProxyCommand(PanedWindow *pwPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj * const objv[]);
static void		ComputeGeometry(PanedWindow *pwPtr);
static int		ConfigurePanes(PanedWindow *pwPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj * const objv[]);
static void		DestroyOptionTables(void *clientData,
			    Tcl_Interp *interp);
static int		SetSticky(void *clientData, Tcl_Interp *interp,
			    Tk_Window tkwin, Tcl_Obj **value, char *recordPtr,
			    Tcl_Size internalOffset, char *oldInternalPtr,
			    int flags);
static Tcl_Obj *	GetSticky(void *clientData, Tk_Window tkwin,
			    char *recordPtr, Tcl_Size internalOffset);
static void		RestoreSticky(void *clientData, Tk_Window tkwin,
			    char *internalPtr, char *oldInternalPtr);
static void		AdjustForSticky(int sticky, int cavityWidth,
			    int cavityHeight, int *xPtr, int *yPtr,
			    int *paneWidthPtr, int *paneHeightPtr);
static void		MoveSash(PanedWindow *pwPtr, int sash, int diff);
static void *	ComputeSlotAddress(void *recordPtr, Tcl_Size offset);
static int		PanedWindowIdentifyCoords(PanedWindow *pwPtr,
			    Tcl_Interp *interp, int x, int y);

/*
 * Sashes are between panes only, so there is one less sash than panes
 */

#define ValidSashIndex(pwPtr, sash) \
	(((sash) >= 0) && ((sash) < ((pwPtr)->numPanes-1)))

static const Tk_GeomMgr panedWindowMgrType = {
    "panedwindow",		/* name */
    PanedWindowReqProc,		/* requestProc */
    PanedWindowLostPaneProc,	/* lostPaneProc */
};

/*
 * Information used for objv parsing.
 */

#define GEOMETRY		0x0001

/*
 * The following structure contains pointers to functions used for processing
 * the custom "-sticky" option for panes.
 */

static const Tk_ObjCustomOption stickyOption = {
    "sticky",			/* name */
    SetSticky,			/* setProc */
    GetSticky,			/* getProc */
    RestoreSticky,		/* restoreProc */
    NULL,			/* freeProc */
    0
};

static const Tk_OptionSpec optionSpecs[] = {
    {TK_OPTION_BORDER, "-background", "background", "Background",
	DEF_PANEDWINDOW_BG_COLOR, TCL_INDEX_NONE, offsetof(PanedWindow, background), 0,
	DEF_PANEDWINDOW_BG_MONO, 0},
    {TK_OPTION_SYNONYM, "-bd", NULL, NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-borderwidth", 0},
    {TK_OPTION_SYNONYM, "-bg", NULL, NULL,
	NULL, 0, TCL_INDEX_NONE, 0, "-background", 0},
    {TK_OPTION_PIXELS, "-borderwidth", "borderWidth", "BorderWidth",
	DEF_PANEDWINDOW_BORDERWIDTH, offsetof(PanedWindow, borderWidthObj), TCL_INDEX_NONE,
	0, 0, GEOMETRY},
    {TK_OPTION_CURSOR, "-cursor", "cursor", "Cursor",
	DEF_PANEDWINDOW_CURSOR, TCL_INDEX_NONE, offsetof(PanedWindow, cursor),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_PIXELS, "-handlepad", "handlePad", "HandlePad",
	DEF_PANEDWINDOW_HANDLEPAD, offsetof(PanedWindow, handlePadObj), TCL_INDEX_NONE,
	0, 0, GEOMETRY},
    {TK_OPTION_PIXELS, "-handlesize", "handleSize", "HandleSize",
	DEF_PANEDWINDOW_HANDLESIZE, offsetof(PanedWindow, handleSizeObj),
	TCL_INDEX_NONE, 0, 0, GEOMETRY},
    {TK_OPTION_PIXELS, "-height", "height", "Height",
	DEF_PANEDWINDOW_HEIGHT, offsetof(PanedWindow, heightObj),
	TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, GEOMETRY},
    {TK_OPTION_BOOLEAN, "-opaqueresize", "opaqueResize", "OpaqueResize",
	DEF_PANEDWINDOW_OPAQUERESIZE, TCL_INDEX_NONE,
	offsetof(PanedWindow, resizeOpaque), 0, 0, 0},
    {TK_OPTION_STRING_TABLE, "-orient", "orient", "Orient",
	DEF_PANEDWINDOW_ORIENT, TCL_INDEX_NONE, offsetof(PanedWindow, orient),
	TK_OPTION_ENUM_VAR, orientStrings, GEOMETRY},
    {TK_OPTION_BORDER, "-proxybackground", "proxyBackground", "ProxyBackground",
	0, TCL_INDEX_NONE, offsetof(PanedWindow, proxyBackground), TK_OPTION_NULL_OK,
	(void *)DEF_PANEDWINDOW_BG_MONO, 0},
    {TK_OPTION_PIXELS, "-proxyborderwidth", "proxyBorderWidth", "ProxyBorderWidth",
	DEF_PANEDWINDOW_PROXYBORDER, offsetof(PanedWindow, proxyBorderWidthObj),
	TCL_INDEX_NONE, 0, 0, GEOMETRY},
    {TK_OPTION_RELIEF, "-proxyrelief", "proxyRelief", "Relief",
	0, TCL_INDEX_NONE, offsetof(PanedWindow, proxyRelief),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_RELIEF, "-relief", "relief", "Relief",
	DEF_PANEDWINDOW_RELIEF, TCL_INDEX_NONE, offsetof(PanedWindow, relief), 0, 0, 0},
    {TK_OPTION_CURSOR, "-sashcursor", "sashCursor", "Cursor",
	DEF_PANEDWINDOW_SASHCURSOR, TCL_INDEX_NONE, offsetof(PanedWindow, sashCursor),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_PIXELS, "-sashpad", "sashPad", "SashPad",
	DEF_PANEDWINDOW_SASHPAD, offsetof(PanedWindow, sashPadObj), TCL_INDEX_NONE,
	0, 0, GEOMETRY},
    {TK_OPTION_RELIEF, "-sashrelief", "sashRelief", "Relief",
	DEF_PANEDWINDOW_SASHRELIEF, TCL_INDEX_NONE, offsetof(PanedWindow, sashRelief),
	0, 0, 0},
    {TK_OPTION_PIXELS, "-sashwidth", "sashWidth", "Width",
	DEF_PANEDWINDOW_SASHWIDTH, offsetof(PanedWindow, sashWidthObj),
	TCL_INDEX_NONE, 0, 0, GEOMETRY},
    {TK_OPTION_BOOLEAN, "-showhandle", "showHandle", "ShowHandle",
	DEF_PANEDWINDOW_SHOWHANDLE, TCL_INDEX_NONE, offsetof(PanedWindow, showHandle),
	0, 0, GEOMETRY},
    {TK_OPTION_PIXELS, "-width", "width", "Width",
	DEF_PANEDWINDOW_WIDTH, offsetof(PanedWindow, widthObj),
	TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, GEOMETRY},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0}
};

static const Tk_OptionSpec paneOptionSpecs[] = {
    {TK_OPTION_WINDOW, "-after", NULL, NULL,
	DEF_PANEDWINDOW_PANE_AFTER, TCL_INDEX_NONE, offsetof(Pane, after),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_WINDOW, "-before", NULL, NULL,
	DEF_PANEDWINDOW_PANE_BEFORE, TCL_INDEX_NONE, offsetof(Pane, before),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_PIXELS, "-height", NULL, NULL,
	DEF_PANEDWINDOW_PANE_HEIGHT, offsetof(Pane, heightObj),
	offsetof(Pane, height), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide",
	DEF_PANEDWINDOW_PANE_HIDE, TCL_INDEX_NONE, offsetof(Pane, hide), 0,0,GEOMETRY},
    {TK_OPTION_PIXELS, "-minsize", NULL, NULL,
	DEF_PANEDWINDOW_PANE_MINSIZE, offsetof(Pane, minSizeObj), TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_PIXELS, "-padx", NULL, NULL,
	DEF_PANEDWINDOW_PANE_PADX, offsetof(Pane, padXObj), TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_PIXELS, "-pady", NULL, NULL,
	DEF_PANEDWINDOW_PANE_PADY, offsetof(Pane, padYObj), TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_CUSTOM, "-sticky", NULL, NULL,
	DEF_PANEDWINDOW_PANE_STICKY, TCL_INDEX_NONE, offsetof(Pane, sticky),
	0, &stickyOption, 0},
    {TK_OPTION_STRING_TABLE, "-stretch", "stretch", "Stretch",
	DEF_PANEDWINDOW_PANE_STRETCH, TCL_INDEX_NONE, offsetof(Pane, stretch),
	TK_OPTION_ENUM_VAR, stretchStrings, 0},
    {TK_OPTION_PIXELS, "-width", NULL, NULL,
	DEF_PANEDWINDOW_PANE_WIDTH, offsetof(Pane, widthObj),
	offsetof(Pane, width), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0}
};

/*
 *--------------------------------------------------------------
 *
 * Tk_PanedWindowObjCmd --
 *
 *	This function is invoked to process the "panedwindow" Tcl command. It
 *	creates a new "panedwindow" widget.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	A new widget is created and configured.
 *
 *--------------------------------------------------------------
 */

int
Tk_PanedWindowObjCmd(
    TCL_UNUSED(void *),	/* NULL. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj * const objv[])	/* Argument objects. */
{
    PanedWindow *pwPtr;
    Tk_Window tkwin, parent;
    OptionTables *pwOpts;
    XSetWindowAttributes atts;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "pathName ?-option value ...?");
	return TCL_ERROR;
    }

    tkwin = Tk_CreateWindowFromPath(interp, Tk_MainWindow(interp),
	    Tcl_GetString(objv[1]), NULL);
    if (tkwin == NULL) {
	return TCL_ERROR;
    }

    pwOpts = (OptionTables *)
	    Tcl_GetAssocData(interp, "PanedWindowOptionTables", NULL);
    if (pwOpts == NULL) {
	/*
	 * The first time this function is invoked, the option tables will be
	 * NULL. We then create the option tables from the templates and store
	 * a pointer to the tables as the command's clientData so we'll have
	 * easy access to it in the future.
	 */

	pwOpts = (OptionTables *)ckalloc(sizeof(OptionTables));

	/*
	 * Set up an exit handler to free the optionTables struct.
	 */

	Tcl_SetAssocData(interp, "PanedWindowOptionTables",
		DestroyOptionTables, pwOpts);

	/*
	 * Create the paned window option tables.
	 */

	pwOpts->pwOptions = Tk_CreateOptionTable(interp, optionSpecs);
	pwOpts->paneOpts = Tk_CreateOptionTable(interp, paneOptionSpecs);
    }

    Tk_SetClass(tkwin, "Panedwindow");

    /*
     * Allocate and initialize the widget record.
     */

    pwPtr = (PanedWindow *)ckalloc(sizeof(PanedWindow));
    memset((void *)pwPtr, 0, (sizeof(PanedWindow)));
    pwPtr->tkwin = tkwin;
    pwPtr->display = Tk_Display(tkwin);
    pwPtr->interp = interp;
    pwPtr->widgetCmd = Tcl_CreateObjCommand2(interp,
	    Tk_PathName(pwPtr->tkwin), PanedWindowWidgetObjCmd, pwPtr,
	    PanedWindowCmdDeletedProc);
    pwPtr->optionTable = pwOpts->pwOptions;
    pwPtr->paneOpts = pwOpts->paneOpts;
    pwPtr->relief = TK_RELIEF_RAISED;
    pwPtr->gc = NULL;
    pwPtr->cursor = NULL;
    pwPtr->sashCursor = NULL;

    /*
     * Keep a hold of the associated tkwin until we destroy the widget,
     * otherwise Tk might free it while we still need it.
     */

    Tcl_Preserve(pwPtr->tkwin);

    if (Tk_InitOptions(interp, pwPtr, pwOpts->pwOptions,
	    tkwin) != TCL_OK) {
	Tk_DestroyWindow(pwPtr->tkwin);
	return TCL_ERROR;
    }

    Tk_CreateEventHandler(pwPtr->tkwin, ExposureMask|StructureNotifyMask,
	    PanedWindowEventProc, pwPtr);

    /*
     * Find the toplevel ancestor of the panedwindow, and make a proxy win as
     * a child of that window; this way the proxy can always float above
     * panes in the panedwindow.
     */

    parent = Tk_Parent(pwPtr->tkwin);
    while (!(Tk_IsTopLevel(parent))) {
	parent = Tk_Parent(parent);
	if (parent == NULL) {
	    parent = pwPtr->tkwin;
	    break;
	}
    }

    pwPtr->proxywin = Tk_CreateAnonymousWindow(interp, parent, NULL);

    /*
     * The proxy window has to be able to share GCs with the main panedwindow
     * despite being children of windows with potentially different
     * characteristics, and it looks better that way too. [Bug 702230] Also
     * set the X window save under attribute to avoid expose events as the
     * proxy sash is dragged across the panes. [Bug 1036963]
     */

    Tk_SetWindowVisual(pwPtr->proxywin,
	    Tk_Visual(tkwin), Tk_Depth(tkwin), Tk_Colormap(tkwin));
    Tk_CreateEventHandler(pwPtr->proxywin, ExposureMask, ProxyWindowEventProc,
	    pwPtr);
    atts.save_under = True;
    Tk_ChangeWindowAttributes(pwPtr->proxywin, CWSaveUnder, &atts);

    if (ConfigurePanedWindow(interp, pwPtr, objc - 2, objv + 2) != TCL_OK) {
	Tk_DestroyWindow(pwPtr->proxywin);
	Tk_DestroyWindow(pwPtr->tkwin);
	return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tk_NewWindowObj(pwPtr->tkwin));
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * PanedWindowWidgetObjCmd --
 *
 *	This function is invoked to process the Tcl command that corresponds
 *	to a widget managed by this module. See the user documentation for
 *	details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

static int
PanedWindowWidgetObjCmd(
    void *clientData,	/* Information about square widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj * const objv[])	/* Argument objects. */
{
    PanedWindow *pwPtr = (PanedWindow *)clientData;
    int result = TCL_OK;
    static const char *const optionStrings[] = {
	"add", "cget", "configure", "forget", "identify", "panecget",
	"paneconfigure", "panes", "proxy", "sash", NULL
    };
    enum options {
	PW_ADD, PW_CGET, PW_CONFIGURE, PW_FORGET, PW_IDENTIFY, PW_PANECGET,
	PW_PANECONFIGURE, PW_PANES, PW_PROXY, PW_SASH
    };
    Tcl_Obj *resultObj;
    int index, count, i, x, y;
    Tk_Window tkwin;
    Pane *panePtr;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], optionStrings, "command",
	    0, &index) != TCL_OK) {
	return TCL_ERROR;
    }

    Tcl_Preserve(pwPtr);

    switch ((enum options) index) {
    case PW_ADD:
	if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "widget ?widget ...?");
	    result = TCL_ERROR;
	    break;
	}
	result = ConfigurePanes(pwPtr, interp, objc, objv);
	break;

    case PW_CGET:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option");
	    result = TCL_ERROR;
	    break;
	}
	resultObj = Tk_GetOptionValue(interp, pwPtr,
		pwPtr->optionTable, objv[2], pwPtr->tkwin);
	if (resultObj == NULL) {
	    result = TCL_ERROR;
	} else {
	    Tcl_SetObjResult(interp, resultObj);
	}
	break;

    case PW_CONFIGURE:
	resultObj = NULL;
	if (objc <= 3) {
	    resultObj = Tk_GetOptionInfo(interp, pwPtr,
		    pwPtr->optionTable,
		    (objc == 3) ? objv[2] : NULL, pwPtr->tkwin);
	    if (resultObj == NULL) {
		result = TCL_ERROR;
	    } else {
		Tcl_SetObjResult(interp, resultObj);
	    }
	} else {
	    result = ConfigurePanedWindow(interp, pwPtr, objc - 2, objv + 2);
	}
	break;

    case PW_FORGET: {

	if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "widget ?widget ...?");
	    result = TCL_ERROR;
	    break;
	}

	/*
	 * Clean up each window named in the arg list.
	 */
	for (count = 0, i = 2; i < objc; i++) {
	    Tk_Window pane = Tk_NameToWindow(interp, Tcl_GetString(objv[i]),
		    pwPtr->tkwin);

	    if (pane == NULL) {
		continue;
	    }
	    panePtr = GetPane(pwPtr, pane);
	    if ((panePtr != NULL) && (panePtr->containerPtr != NULL)) {
		count++;
		Tk_ManageGeometry(pane, NULL, NULL);
		Tk_UnmaintainGeometry(panePtr->tkwin, pwPtr->tkwin);
		Tk_DeleteEventHandler(panePtr->tkwin, StructureNotifyMask,
			PaneStructureProc, panePtr);
		Tk_UnmapWindow(panePtr->tkwin);
		Unlink(panePtr);
	    }
	    if (count != 0) {
		ComputeGeometry(pwPtr);
	    }
	}
	break;
    }

    case PW_IDENTIFY:
	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "x y");
	    result = TCL_ERROR;
	    break;
	}

	if ((Tcl_GetIntFromObj(interp, objv[2], &x) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[3], &y) != TCL_OK)) {
	    result = TCL_ERROR;
	    break;
	}
	result = PanedWindowIdentifyCoords(pwPtr, interp, x, y);
	break;

    case PW_PANECGET:
	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "pane option");
	    result = TCL_ERROR;
	    break;
	}
	tkwin = Tk_NameToWindow(interp, Tcl_GetString(objv[2]), pwPtr->tkwin);
	if (tkwin == NULL) {
	    result = TCL_ERROR;
	    break;
	}
	resultObj = NULL;
	for (i = 0; i < pwPtr->numPanes; i++) {
	    if (pwPtr->panes[i]->tkwin == tkwin) {
		resultObj = Tk_GetOptionValue(interp,
			pwPtr->panes[i], pwPtr->paneOpts,
			objv[3], tkwin);
	    }
	}
	if (resultObj == NULL) {
	    if (i == pwPtr->numPanes) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"not managed by this window", TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "PANEDWINDOW", "UNMANAGED",
			(char *)NULL);
	    }
	    result = TCL_ERROR;
	} else {
	    Tcl_SetObjResult(interp, resultObj);
	}
	break;

    case PW_PANECONFIGURE:
	if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 2, objv,
		    "pane ?-option value ...?");
	    result = TCL_ERROR;
	    break;
	}
	resultObj = NULL;
	if (objc <= 4) {
	    tkwin = Tk_NameToWindow(interp, Tcl_GetString(objv[2]),
		    pwPtr->tkwin);
	    if (tkwin == NULL) {
		/*
		 * Just a plain old bad window; Tk_NameToWindow filled in an
		 * error message for us.
		 */

		result = TCL_ERROR;
		break;
	    }
	    for (i = 0; i < pwPtr->numPanes; i++) {
		if (pwPtr->panes[i]->tkwin == tkwin) {
		    resultObj = Tk_GetOptionInfo(interp,
			    pwPtr->panes[i], pwPtr->paneOpts,
			    (objc == 4) ? objv[3] : NULL,
			    pwPtr->tkwin);
		    if (resultObj == NULL) {
			result = TCL_ERROR;
		    } else {
			Tcl_SetObjResult(interp, resultObj);
		    }
		    break;
		}
	    }
	} else {
	    result = ConfigurePanes(pwPtr, interp, objc, objv);
	}
	break;

    case PW_PANES:
	resultObj = Tcl_NewObj();
	for (i = 0; i < pwPtr->numPanes; i++) {
	    Tcl_ListObjAppendElement(NULL, resultObj,
		    Tk_NewWindowObj(pwPtr->panes[i]->tkwin));
	}
	Tcl_SetObjResult(interp, resultObj);
	break;

    case PW_PROXY:
	result = PanedWindowProxyCommand(pwPtr, interp, objc, objv);
	break;

    case PW_SASH:
	result = PanedWindowSashCommand(pwPtr, interp, objc, objv);
	break;
    }
    Tcl_Release(pwPtr);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigurePanes --
 *
 *	Add or alter the configuration options of a pane in a paned window.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Depends on options; may add a pane to the paned window, may alter the
 *	geometry management options of a pane.
 *
 *----------------------------------------------------------------------
 */

static int
ConfigurePanes(
    PanedWindow *pwPtr,		/* Information about paned window. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    int i, firstOptionArg, j, found, doubleBw, index, numNewPanes, haveLoc;
    int insertIndex;
    Tk_Window tkwin = NULL, ancestor, parent;
    Pane *panePtr, **inserts, **newPanes;
    Pane options;
    const char *arg;

    /*
     * Find the non-window name arguments; these are the configure options for
     * the panes. Also validate that the window names given are legitimate
     * (ie, they are real windows, they are not the panedwindow itself, etc.).
     */

    for (i = 2; i < objc; i++) {
	arg = Tcl_GetString(objv[i]);
	if (arg[0] == '-') {
	    break;
	} else {
	    tkwin = Tk_NameToWindow(interp, arg, pwPtr->tkwin);
	    if (tkwin == NULL) {
		/*
		 * Just a plain old bad window; Tk_NameToWindow filled in an
		 * error message for us.
		 */

		return TCL_ERROR;
	    } else if (tkwin == pwPtr->tkwin) {
		/*
		 * A panedwindow cannot manage itself.
		 */

		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"cannot add %s to itself", arg));
		Tcl_SetErrorCode(interp, "TK", "GEOMETRY", "SELF", (char *)NULL);
		return TCL_ERROR;
	    } else if (Tk_IsTopLevel(tkwin)) {
		/*
		 * A panedwindow cannot manage a toplevel.
		 */

		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"cannot add toplevel %s to %s", arg,
			Tk_PathName(pwPtr->tkwin)));
		Tcl_SetErrorCode(interp, "TK", "GEOMETRY", "TOPLEVEL", (char *)NULL);
		return TCL_ERROR;
	    } else {
		/*
		 * Make sure the panedwindow is the parent of the pane,
		 * or a descendant of the pane's parent.
		 */

		parent = Tk_Parent(tkwin);
		for (ancestor = pwPtr->tkwin;;ancestor = Tk_Parent(ancestor)) {
		    if (ancestor == parent) {
			break;
		    }
		    if (Tk_IsTopLevel(ancestor)) {
			Tcl_SetObjResult(interp, Tcl_ObjPrintf(
				"cannot add %s to %s", arg,
				Tk_PathName(pwPtr->tkwin)));
			Tcl_SetErrorCode(interp, "TK", "GEOMETRY",
				"HIERARCHY", (char *)NULL);
			return TCL_ERROR;
		    }
		}
	    }
	}
    }
    firstOptionArg = i;

    /*
     * Pre-parse the configuration options, to get the before/after specifiers
     * into an easy-to-find location (a local variable). Also, check the
     * return from Tk_SetOptions once, here, so we can save a little bit of
     * extra testing in the for loop below.
     */

    memset((void *)&options, 0, sizeof(Pane));
    if (Tk_SetOptions(interp, &options, pwPtr->paneOpts,
	    objc - firstOptionArg, objv + firstOptionArg,
	    pwPtr->tkwin, NULL, NULL) != TCL_OK) {
	return TCL_ERROR;
    }

    /*
     * If either -after or -before was given, find the numerical index that
     * corresponds to the given window. If both -after and -before are given,
     * the option precedence is: -after, then -before.
     */

    index = -1;
    haveLoc = 0;
    if (options.after != NULL) {
	tkwin = options.after;
	haveLoc = 1;
	for (i = 0; i < pwPtr->numPanes; i++) {
	    if (options.after == pwPtr->panes[i]->tkwin) {
		index = i + 1;
		break;
	    }
	}
    } else if (options.before != NULL) {
	tkwin = options.before;
	haveLoc = 1;
	for (i = 0; i < pwPtr->numPanes; i++) {
	    if (options.before == pwPtr->panes[i]->tkwin) {
		index = i;
		break;
	    }
	}
    }

    /*
     * If a window was given for -after/-before, but it's not a window managed
     * by the panedwindow, throw an error
     */

    if (haveLoc && index == -1) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"window \"%s\" is not managed by %s",
		Tk_PathName(tkwin), Tk_PathName(pwPtr->tkwin)));
	Tcl_SetErrorCode(interp, "TK", "PANEDWINDOW", "UNMANAGED", (char *)NULL);
	Tk_FreeConfigOptions(&options, pwPtr->paneOpts,
		pwPtr->tkwin);
	return TCL_ERROR;
    }

    /*
     * Allocate an array to hold, in order, the pointers to the pane
     * structures corresponding to the windows specified. Some of those
     * structures may already have existed, some may be new.
     */

    inserts = (Pane **)ckalloc(sizeof(Pane *) * (firstOptionArg - 2));
    insertIndex = 0;

    /*
     * Populate the inserts array, creating new pane structures as necessary,
     * applying the options to each structure as we go, and, if necessary,
     * marking the spot in the original panes array as empty (for
     * pre-existing pane structures).
     */

    for (i = 0, numNewPanes = 0; i < firstOptionArg - 2; i++) {
	/*
	 * We don't check that tkwin is NULL here, because the pre-pass above
	 * guarantees that the input at this stage is good.
	 */

	tkwin = Tk_NameToWindow(interp, Tcl_GetString(objv[i + 2]),
		pwPtr->tkwin);

	found = 0;
	for (j = 0; j < pwPtr->numPanes; j++) {
	    if (pwPtr->panes[j] != NULL && pwPtr->panes[j]->tkwin == tkwin) {
		int minSize;
		Tk_SetOptions(interp, pwPtr->panes[j],
			pwPtr->paneOpts, objc - firstOptionArg,
			objv + firstOptionArg, pwPtr->tkwin, NULL, NULL);
		Tk_GetPixelsFromObj(NULL, tkwin, pwPtr->panes[j]->minSizeObj, &minSize);
		found = 1;

		/*
		 * If the pane is supposed to move, add it to the inserts
		 * array now; otherwise, leave it where it is.
		 */

		if (index != -1) {
		    inserts[insertIndex++] = pwPtr->panes[j];
		    pwPtr->panes[j] = NULL;
		}
		break;
	    }
	}

	if (found) {
	    continue;
	}

	/*
	 * Make sure this pane wasn't already put into the inserts array,
	 * i.e., when the user specifies the same window multiple times in a
	 * single add command.
	 */
	for (j = 0; j < insertIndex; j++) {
	    if (inserts[j]->tkwin == tkwin) {
		found = 1;
		break;
	    }
	}
	if (found) {
	    continue;
	}

	/*
	 * Create a new pane structure and initialize it. All panes start
	 * out with their "natural" dimensions.
	 */
	int minSize;

	panePtr = (Pane *)ckalloc(sizeof(Pane));
	memset(panePtr, 0, sizeof(Pane));
	Tk_InitOptions(interp, panePtr, pwPtr->paneOpts,
		pwPtr->tkwin);
	Tk_SetOptions(interp, panePtr, pwPtr->paneOpts,
		objc - firstOptionArg, objv + firstOptionArg,
		pwPtr->tkwin, NULL, NULL);
	panePtr->tkwin = tkwin;
	panePtr->containerPtr = pwPtr;
	doubleBw = 2 * Tk_Changes(panePtr->tkwin)->border_width;
	if (panePtr->width > 0) {
	    panePtr->paneWidth = panePtr->width;
	} else {
	    panePtr->paneWidth = Tk_ReqWidth(tkwin) + doubleBw;
	}
	if (panePtr->height > 0) {
	    panePtr->paneHeight = panePtr->height;
	} else {
	    panePtr->paneHeight = Tk_ReqHeight(tkwin) + doubleBw;
	}
	Tk_GetPixelsFromObj(NULL, panePtr->tkwin, panePtr->minSizeObj, &minSize);

	/*
	 * Set up the geometry management callbacks for this pane.
	 */

	Tk_CreateEventHandler(panePtr->tkwin, StructureNotifyMask,
		PaneStructureProc, panePtr);
	Tk_ManageGeometry(panePtr->tkwin, &panedWindowMgrType, panePtr);
	inserts[insertIndex++] = panePtr;
	numNewPanes++;
    }

    /*
     * Allocate the new panes array, then copy the panes into it, in order.
     */

    i = sizeof(Pane *) * (pwPtr->numPanes + numNewPanes);
    newPanes = (Pane **)ckalloc(i);
    memset(newPanes, 0, i);
    if (index == -1) {
	/*
	 * If none of the existing panes have to be moved, just copy the old
	 * and append the new.
	 * Be careful about the case pwPtr->numPanes == 0 since in this case
	 * pwPtr->panes is NULL, and the memcpy would have undefined behavior.
	 */
	if (pwPtr->numPanes) {
	    memcpy(newPanes, pwPtr->panes,
		    sizeof(Pane *) * pwPtr->numPanes);
	}
	memcpy(&newPanes[pwPtr->numPanes], inserts,
		sizeof(Pane *) * numNewPanes);
    } else {
	/*
	 * If some of the existing panes were moved, the old panes array
	 * will be partially populated, with some valid and some invalid
	 * entries. Walk through it, copying valid entries to the new panes
	 * array as we go; when we get to the insert location for the new
	 * panes, copy the inserts array over, then finish off the old panes
	 * array.
	 */

	for (i = 0, j = 0; i < index; i++) {
	    if (pwPtr->panes[i] != NULL) {
		newPanes[j] = pwPtr->panes[i];
		j++;
	    }
	}

	memcpy(&newPanes[j], inserts, sizeof(Pane *)*insertIndex);
	j += firstOptionArg - 2;

	for (i = index; i < pwPtr->numPanes; i++) {
	    if (pwPtr->panes[i] != NULL) {
		newPanes[j] = pwPtr->panes[i];
		j++;
	    }
	}
    }

    /*
     * Make the new panes array the paned window's pane array, and clean up.
     */

    ckfree(pwPtr->panes);
    ckfree(inserts);
    pwPtr->panes = newPanes;

    /*
     * Set the paned window's pane count to the new value.
     */

    pwPtr->numPanes += numNewPanes;

    Tk_FreeConfigOptions(&options, pwPtr->paneOpts, pwPtr->tkwin);

    ComputeGeometry(pwPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * PanedWindowSashCommand --
 *
 *	Implementation of the panedwindow sash subcommand. See the user
 *	documentation for details on what it does.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Depends on the arguments.
 *
 *----------------------------------------------------------------------
 */

static int
PanedWindowSashCommand(
    PanedWindow *pwPtr,		/* Pointer to paned window information. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    static const char *const sashOptionStrings[] = {
	"coord", "dragto", "mark", "place", NULL
    };
    enum sashOptions {
	SASH_COORD, SASH_DRAGTO, SASH_MARK, SASH_PLACE
    };
    int index, sash, x, y, diff;
    Tcl_Obj *coords[2];
    Pane *panePtr;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "option ?arg ...?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[2], sashOptionStrings, "option", 0,
	    &index) != TCL_OK) {
	return TCL_ERROR;
    }

    switch ((enum sashOptions) index) {
    case SASH_COORD:
	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "index");
	    return TCL_ERROR;
	}

	if (Tcl_GetIntFromObj(interp, objv[3], &sash) != TCL_OK) {
	    return TCL_ERROR;
	}

	if (!ValidSashIndex(pwPtr, sash)) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "invalid sash index", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "VALUE", "SASH_INDEX", (char *)NULL);
	    return TCL_ERROR;
	}
	panePtr = pwPtr->panes[sash];

	coords[0] = Tcl_NewWideIntObj(panePtr->sashx);
	coords[1] = Tcl_NewWideIntObj(panePtr->sashy);
	Tcl_SetObjResult(interp, Tcl_NewListObj(2, coords));
	break;

    case SASH_MARK:
	if (objc != 6 && objc != 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "index ?x y?");
	    return TCL_ERROR;
	}

	if (Tcl_GetIntFromObj(interp, objv[3], &sash) != TCL_OK) {
	    return TCL_ERROR;
	}

	if (!ValidSashIndex(pwPtr, sash)) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "invalid sash index", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "VALUE", "SASH_INDEX", (char *)NULL);
	    return TCL_ERROR;
	}

	if (objc == 6) {
	    if (Tcl_GetIntFromObj(interp, objv[4], &x) != TCL_OK) {
		return TCL_ERROR;
	    }

	    if (Tcl_GetIntFromObj(interp, objv[5], &y) != TCL_OK) {
		return TCL_ERROR;
	    }

	    pwPtr->panes[sash]->markx = x;
	    pwPtr->panes[sash]->marky = y;
	} else {
	    coords[0] = Tcl_NewWideIntObj(pwPtr->panes[sash]->markx);
	    coords[1] = Tcl_NewWideIntObj(pwPtr->panes[sash]->marky);
	    Tcl_SetObjResult(interp, Tcl_NewListObj(2, coords));
	}
	break;

    case SASH_DRAGTO:
    case SASH_PLACE:
	if (objc != 6) {
	    Tcl_WrongNumArgs(interp, 3, objv, "index x y");
	    return TCL_ERROR;
	}

	if (Tcl_GetIntFromObj(interp, objv[3], &sash) != TCL_OK) {
	    return TCL_ERROR;
	}

	if (!ValidSashIndex(pwPtr, sash)) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "invalid sash index", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "VALUE", "SASH_INDEX", (char *)NULL);
	    return TCL_ERROR;
	}

	if (Tcl_GetIntFromObj(interp, objv[4], &x) != TCL_OK) {
	    return TCL_ERROR;
	}

	if (Tcl_GetIntFromObj(interp, objv[5], &y) != TCL_OK) {
	    return TCL_ERROR;
	}

	panePtr = pwPtr->panes[sash];
	if (pwPtr->orient == ORIENT_HORIZONTAL) {
	    if (index == SASH_PLACE) {
		diff = x - pwPtr->panes[sash]->sashx;
	    } else {
		diff = x - pwPtr->panes[sash]->markx;
	    }
	} else {
	    if (index == SASH_PLACE) {
		diff = y - pwPtr->panes[sash]->sashy;
	    } else {
		diff = y - pwPtr->panes[sash]->marky;
	    }
	}

	MoveSash(pwPtr, sash, diff);
	ComputeGeometry(pwPtr);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigurePanedWindow --
 *
 *	This function is called to process an objv/objc list in conjunction
 *	with the Tk option database to configure (or reconfigure) a paned
 *	window widget.
 *
 * Results:
 *	The return value is a standard Tcl result. If TCL_ERROR is returned,
 *	then the interp's result contains an error message.
 *
 * Side effects:
 *	Configuration information, such as colors, border width, etc. get set
 *	for pwPtr; old resources get freed, if there were any.
 *
 *----------------------------------------------------------------------
 */

static int
ConfigurePanedWindow(
    Tcl_Interp *interp,		/* Used for error reporting. */
    PanedWindow *pwPtr,		/* Information about widget. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument values. */
{
    Tk_SavedOptions savedOptions;
    int typemask = 0;

    if (Tk_SetOptions(interp, pwPtr, pwPtr->optionTable, objc, objv,
	    pwPtr->tkwin, &savedOptions, &typemask) != TCL_OK) {
	Tk_RestoreSavedOptions(&savedOptions);
	return TCL_ERROR;
    }

    Tk_FreeSavedOptions(&savedOptions);

    PanedWindowWorldChanged(pwPtr);

    /*
     * If an option that affects geometry has changed, make a re-layout
     * request.
     */

    if (typemask & GEOMETRY) {
	ComputeGeometry(pwPtr);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * PanedWindowWorldChanged --
 *
 *	This function is invoked anytime a paned window's world has changed in
 *	some way that causes the widget to have to recompute graphics contexts
 *	and geometry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Paned window will be relayed out and redisplayed.
 *
 *----------------------------------------------------------------------
 */

static void
PanedWindowWorldChanged(
    void *instanceData)	/* Information about the paned window. */
{
    XGCValues gcValues;
    GC newGC;
    PanedWindow *pwPtr = (PanedWindow *)instanceData;
    int borderWidth, width = -1, height = -1;

    /*
     * Allocated a graphics context for drawing the paned window widget
     * elements (background, sashes, etc.) and set the window background.
     */

    gcValues.background = Tk_3DBorderColor(pwPtr->background)->pixel;
    newGC = Tk_GetGC(pwPtr->tkwin, GCBackground, &gcValues);
    if (pwPtr->gc != NULL) {
	Tk_FreeGC(pwPtr->display, pwPtr->gc);
    }
    pwPtr->gc = newGC;
    Tk_SetWindowBackground(pwPtr->tkwin, gcValues.background);

    /*
     * Issue geometry size requests to Tk.
     */

    Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->borderWidthObj, &borderWidth);
    Tk_SetInternalBorder(pwPtr->tkwin, borderWidth);
    if (pwPtr->widthObj) {
	Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->widthObj, &width);
    }
    if (pwPtr->heightObj) {
	Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->heightObj, &height);
    }
    if (width > 0 && height > 0) {
	Tk_GeometryRequest(pwPtr->tkwin, width, height);
    }

    /*
     * Arrange for the window to be redrawn, if neccessary.
     */

    if (Tk_IsMapped(pwPtr->tkwin) && !(pwPtr->flags & REDRAW_PENDING)) {
	Tcl_DoWhenIdle(DisplayPanedWindow, pwPtr);
	pwPtr->flags |= REDRAW_PENDING;
    }
}

/*
 *--------------------------------------------------------------
 *
 * PanedWindowEventProc --
 *
 *	This function is invoked by the Tk dispatcher for various events on
 *	paned windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	When the window gets deleted, internal structures get cleaned up. When
 *	it gets exposed, it is redisplayed.
 *
 *--------------------------------------------------------------
 */

static void
PanedWindowEventProc(
    void *clientData,	/* Information about window. */
    XEvent *eventPtr)		/* Information about event. */
{
    PanedWindow *pwPtr = (PanedWindow *)clientData;
    int i;

    if (eventPtr->type == Expose) {
	if (pwPtr->tkwin != NULL && !(pwPtr->flags & REDRAW_PENDING)) {
	    Tcl_DoWhenIdle(DisplayPanedWindow, pwPtr);
	    pwPtr->flags |= REDRAW_PENDING;
	}
    } else if (eventPtr->type == ConfigureNotify) {
	pwPtr->flags |= REQUESTED_RELAYOUT;
	if (pwPtr->tkwin != NULL && !(pwPtr->flags & REDRAW_PENDING)) {
	    Tcl_DoWhenIdle(DisplayPanedWindow, pwPtr);
	    pwPtr->flags |= REDRAW_PENDING;
	}
    } else if (eventPtr->type == DestroyNotify) {
	DestroyPanedWindow(pwPtr);
    } else if (eventPtr->type == UnmapNotify) {
	for (i = 0; i < pwPtr->numPanes; i++) {
	    if (!pwPtr->panes[i]->hide) {
		Tk_UnmapWindow(pwPtr->panes[i]->tkwin);
	    }
	}
    } else if (eventPtr->type == MapNotify) {
	for (i = 0; i < pwPtr->numPanes; i++) {
	    if (!pwPtr->panes[i]->hide) {
		Tk_MapWindow(pwPtr->panes[i]->tkwin);
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PanedWindowCmdDeletedProc --
 *
 *	This function is invoked when a widget command is deleted. If the
 *	widget isn't already in the process of being destroyed, this command
 *	destroys it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The widget is destroyed.
 *
 *----------------------------------------------------------------------
 */

static void
PanedWindowCmdDeletedProc(
    void *clientData)	/* Pointer to widget record for widget. */
{
    PanedWindow *pwPtr = (PanedWindow *)clientData;

    /*
     * This function could be invoked either because the window was destroyed
     * and the command was then deleted or because the command was deleted,
     * and then this function destroys the widget. The WIDGET_DELETED flag
     * distinguishes these cases.
     */

    if (!(pwPtr->flags & WIDGET_DELETED)) {
	Tk_DestroyWindow(pwPtr->proxywin);
	Tk_DestroyWindow(pwPtr->tkwin);
    }
}

/*
 *--------------------------------------------------------------
 *
 * DisplayPanedWindow --
 *
 *	This function redraws the contents of a paned window widget. It is
 *	invoked as a do-when-idle handler, so it only runs when there's
 *	nothing else for the application to do.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information appears on the screen.
 *
 *--------------------------------------------------------------
 */

static void
DisplayPanedWindow(
    void *clientData)	/* Information about window. */
{
    PanedWindow *pwPtr = (PanedWindow *)clientData;
    Pane *panePtr;
    Pixmap pixmap;
    Tk_Window tkwin = pwPtr->tkwin;
    int i, sashWidth, sashHeight;
    const int horizontal = (pwPtr->orient == ORIENT_HORIZONTAL);
    int first, last;
    int borderWidth;

    pwPtr->flags &= ~REDRAW_PENDING;
    if ((pwPtr->tkwin == NULL) || !Tk_IsMapped(tkwin)) {
	return;
    }

    if (pwPtr->flags & REQUESTED_RELAYOUT) {
	ArrangePanes(clientData);
    }

#ifndef TK_NO_DOUBLE_BUFFERING
    /*
     * Create a pixmap for double-buffering, if necessary.
     */

    pixmap = Tk_GetPixmap(Tk_Display(tkwin), Tk_WindowId(tkwin),
	    Tk_Width(tkwin), Tk_Height(tkwin), Tk_Depth(tkwin));
#else
    pixmap = Tk_WindowId(tkwin);
#endif /* TK_NO_DOUBLE_BUFFERING */

    /*
     * Redraw the widget's background and border.
     */

    Tk_GetPixelsFromObj(NULL, tkwin, pwPtr->borderWidthObj, &borderWidth);
    Tk_Fill3DRectangle(tkwin, pixmap, pwPtr->background, 0, 0,
	    Tk_Width(tkwin), Tk_Height(tkwin), borderWidth, pwPtr->relief);

    /*
     * Set up boilerplate geometry values for sashes (width, height, common
     * coordinates).
     */

    Tk_GetPixelsFromObj(NULL, tkwin, pwPtr->sashWidthObj, &sashWidth);
    if (horizontal) {
	sashHeight = Tk_Height(tkwin) - (2 * Tk_InternalBorderLeft(tkwin));
    } else {
	sashHeight = sashWidth;
	sashWidth = Tk_Width(tkwin) - (2 * Tk_InternalBorderLeft(tkwin));
    }

    /*
     * Draw the sashes.
     */

    GetFirstLastVisiblePane(pwPtr, &first, &last);
    for (i = 0; i < pwPtr->numPanes - 1; i++) {

	panePtr = pwPtr->panes[i];
	if (panePtr->hide || i == last) {
	    continue;
	}
	if (sashWidth > 0 && sashHeight > 0) {
	    Tk_Fill3DRectangle(tkwin, pixmap, pwPtr->background,
		    panePtr->sashx, panePtr->sashy, sashWidth, sashHeight,
		    1, pwPtr->sashRelief);
	}
	if (pwPtr->showHandle) {
	    int handleSize;
	    Tk_GetPixelsFromObj(NULL, tkwin, pwPtr->handleSizeObj, &handleSize);
	    Tk_Fill3DRectangle(tkwin, pixmap, pwPtr->background,
		    panePtr->handlex, panePtr->handley,
		    handleSize, handleSize, 1,
		    TK_RELIEF_RAISED);
	}
    }

#ifndef TK_NO_DOUBLE_BUFFERING
    /*
     * Copy the information from the off-screen pixmap onto the screen, then
     * delete the pixmap.
     */

    XCopyArea(Tk_Display(tkwin), pixmap, Tk_WindowId(tkwin), pwPtr->gc, 0, 0,
	    (unsigned) Tk_Width(tkwin), (unsigned) Tk_Height(tkwin), 0, 0);
    Tk_FreePixmap(Tk_Display(tkwin), pixmap);
#endif /* TK_NO_DOUBLE_BUFFERING */
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyPanedWindow --
 *
 *	This function is invoked by PanedWindowEventProc to free the internal
 *	structure of a paned window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Everything associated with the paned window is freed up.
 *
 *----------------------------------------------------------------------
 */

static void
DestroyPanedWindow(
    PanedWindow *pwPtr)		/* Info about paned window widget. */
{
    int i;

    /*
     * First mark the widget as in the process of being deleted, so that any
     * code that causes calls to other paned window functions will abort.
     */

    pwPtr->flags |= WIDGET_DELETED;

    /*
     * Cancel idle callbacks for redrawing the widget and for rearranging the
     * panes.
     */

    if (pwPtr->flags & REDRAW_PENDING) {
	Tcl_CancelIdleCall(DisplayPanedWindow, pwPtr);
    }
    if (pwPtr->flags & RESIZE_PENDING) {
	Tcl_CancelIdleCall(ArrangePanes, pwPtr);
    }

    /*
     * Clean up the pane list; foreach pane:
     *  o  Cancel the pane's structure notification callback
     *  o  Cancel geometry management for the pane.
     *  o  Free memory for the pane
     */

    for (i = 0; i < pwPtr->numPanes; i++) {
	Tk_DeleteEventHandler(pwPtr->panes[i]->tkwin, StructureNotifyMask,
		PaneStructureProc, pwPtr->panes[i]);
	Tk_ManageGeometry(pwPtr->panes[i]->tkwin, NULL, NULL);
	Tk_FreeConfigOptions(pwPtr->panes[i], pwPtr->paneOpts,
		pwPtr->tkwin);
	ckfree(pwPtr->panes[i]);
	pwPtr->panes[i] = NULL;
    }
    if (pwPtr->panes) {
	ckfree(pwPtr->panes);
    }

    /*
     * Remove the widget command from the interpreter.
     */

    Tcl_DeleteCommandFromToken(pwPtr->interp, pwPtr->widgetCmd);

    /*
     * Let Tk_FreeConfigOptions clean up the rest.
     */

    Tk_FreeConfigOptions(pwPtr, pwPtr->optionTable, pwPtr->tkwin);
    Tcl_Release(pwPtr->tkwin);
    pwPtr->tkwin = NULL;

    Tcl_EventuallyFree(pwPtr, TCL_DYNAMIC);
}

/*
 *--------------------------------------------------------------
 *
 * PanedWindowReqProc --
 *
 *	This function is invoked by Tk_GeometryRequest for windows managed by
 *	a paned window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Arranges for tkwin, and all its managed siblings, to be re-arranged at
 *	the next idle point.
 *
 *--------------------------------------------------------------
 */

static void
PanedWindowReqProc(
    void *clientData,	/* Paned window's information about window
				 * that got new preferred geometry. */
    TCL_UNUSED(Tk_Window))		/* Other Tk-related information about the
				 * window. */
{
    Pane *panePtr = (Pane *)clientData;
    PanedWindow *pwPtr = (PanedWindow *) panePtr->containerPtr;

    if (Tk_IsMapped(pwPtr->tkwin)) {
	if (!(pwPtr->flags & RESIZE_PENDING)) {
	    pwPtr->flags |= RESIZE_PENDING;
	    Tcl_DoWhenIdle(ArrangePanes, pwPtr);
	}
    } else {
	int doubleBw = 2 * Tk_Changes(panePtr->tkwin)->border_width;

	if (panePtr->width <= 0) {
	    panePtr->paneWidth = Tk_ReqWidth(panePtr->tkwin) + doubleBw;
	}
	if (panePtr->height <= 0) {
	    panePtr->paneHeight = Tk_ReqHeight(panePtr->tkwin) + doubleBw;
	}
	ComputeGeometry(pwPtr);
    }
}

/*
 *--------------------------------------------------------------
 *
 * PanedWindowLostPaneProc --
 *
 *	This function is invoked by Tk whenever some other geometry claims
 *	control over a pane that used to be managed by us.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Forgets all information about the pane. Causes geometry to be
 *	recomputed for the panedwindow.
 *
 *--------------------------------------------------------------
 */

static void
PanedWindowLostPaneProc(
    void *clientData,	/* Grid structure for the pane that was
				 * stolen away. */
    TCL_UNUSED(Tk_Window))		/* Tk's handle for the pane. */
{
    Pane *panePtr = (Pane *)clientData;
    PanedWindow *pwPtr = (PanedWindow *) panePtr->containerPtr;

    if (pwPtr->tkwin != Tk_Parent(panePtr->tkwin)) {
	Tk_UnmaintainGeometry(panePtr->tkwin, pwPtr->tkwin);
    }
    Unlink(panePtr);
    Tk_DeleteEventHandler(panePtr->tkwin, StructureNotifyMask,
	    PaneStructureProc, panePtr);
    Tk_UnmapWindow(panePtr->tkwin);
    panePtr->tkwin = NULL;
    ckfree(panePtr);
    ComputeGeometry(pwPtr);
}

/*
 *--------------------------------------------------------------
 *
 * ArrangePanes --
 *
 *	This function is invoked (using the Tcl_DoWhenIdle mechanism) to
 *	re-layout a set of windows managed by a paned window. It is invoked at
 *	idle time so that a series of pane requests can be merged into a
 *	single layout operation.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The panes of containerPtr may get resized or moved.
 *
 *--------------------------------------------------------------
 */

static void
ArrangePanes(
    void *clientData)	/* Structure describing parent whose panes
				 * are to be re-layed out. */
{
    PanedWindow *pwPtr = (PanedWindow *)clientData;
    Pane *panePtr;
    int i, newPaneWidth, newPaneHeight, paneX, paneY;
    int paneWidth, paneHeight, paneSize, paneMinSize;
    int doubleBw;
    int x, y;
    int sashWidth, sashOffset, sashCount, handleOffset;
    int sashReserve, sxReserve, syReserve;
    int internalBW;
    int paneDynSize, paneDynMinSize, pwHeight, pwWidth, pwSize;
    int first, last;
    int stretchReserve, stretchAmount;
    const int horizontal = (pwPtr->orient == ORIENT_HORIZONTAL);
    int handleSize, sashPad, handlePad;

    pwPtr->flags &= ~(REQUESTED_RELAYOUT|RESIZE_PENDING);

    /*
     * If the parent has no panes anymore, then don't do anything at all:
     * just leave the parent's size as-is. Otherwise there is no way to
     * "relinquish" control over the parent so another geometry manager can
     * take over.
     */

    if (pwPtr->numPanes == 0) {
	return;
    }

    Tcl_Preserve(pwPtr);

    /*
     * Find index of first and last visible panes.
     */

    GetFirstLastVisiblePane(pwPtr, &first, &last);

    /*
     * First pass; compute sizes
     */

    paneDynSize = paneDynMinSize = 0;
    internalBW = Tk_InternalBorderLeft(pwPtr->tkwin);
    pwHeight = Tk_Height(pwPtr->tkwin) - (2 * internalBW);
    pwWidth = Tk_Width(pwPtr->tkwin) - (2 * internalBW);
    x = y = internalBW;
    stretchReserve = (horizontal ? pwWidth : pwHeight);

    /*
     * Calculate the sash width, including handle and padding, and the sash
     * and handle offsets.
     */

    Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->sashPadObj, &sashPad);
    sashOffset = handleOffset = sashPad;
    Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->handleSizeObj, &handleSize);
    Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->sashWidthObj, &sashWidth);
    if (pwPtr->showHandle && handleSize > sashWidth) {
	sashOffset = ((handleSize - sashWidth) / 2) + sashPad;
	sashWidth = (2 * sashPad) + handleSize;
    } else {
	handleOffset = ((sashWidth - handleSize) / 2) + sashPad;
	sashWidth = (2 * sashPad) + sashWidth;
    }

    for (i = sashCount = 0; i < pwPtr->numPanes; i++) {
	int padX, padY, minSize;

	panePtr = pwPtr->panes[i];

	if (panePtr->hide) {
	    continue;
	}

	/*
	 * Compute the total size needed by all the panes and the left-over,
	 * or shortage of space available.
	 */

	Tk_GetPixelsFromObj(NULL, panePtr->tkwin, panePtr->padXObj, &padX);
	Tk_GetPixelsFromObj(NULL, panePtr->tkwin, panePtr->padYObj, &padY);
	if (horizontal) {
	    if (panePtr->width > 0) {
		paneSize = panePtr->width;
	    } else {
		paneSize = panePtr->paneWidth;
	    }
	    stretchReserve -= paneSize + (2 * padX);
	} else {
	    if (panePtr->height > 0) {
		paneSize = panePtr->height;
	    } else {
		paneSize = panePtr->paneHeight;
	    }
	    stretchReserve -= paneSize + (2 * padY);
	}
	Tk_GetPixelsFromObj(NULL, panePtr->tkwin, panePtr->minSizeObj, &minSize);
	if (IsStretchable(panePtr->stretch,i,first,last)
		&& Tk_IsMapped(pwPtr->tkwin)) {
	    paneDynSize += paneSize;
	    paneDynMinSize += minSize;
	}
	if (i != last) {
	    stretchReserve -= sashWidth;
	    sashCount++;
	}
    }

    /*
     * Second pass; adjust/arrange panes.
     */

    for (i = 0; i < pwPtr->numPanes; i++) {
	int padX, padY;

	panePtr = pwPtr->panes[i];

	if (panePtr->hide) {
	    Tk_UnmaintainGeometry(panePtr->tkwin, pwPtr->tkwin);
	    Tk_UnmapWindow(panePtr->tkwin);
	    continue;
	}

	/*
	 * Compute the size of this pane. The algorithm (assuming a
	 * horizontal paned window) is:
	 *
	 * 1.  Get "base" dimensions. If a width or height is specified for
	 *     this pane, use those values; else use the ReqWidth/ReqHeight.
	 * 2.  Using base dimensions, pane dimensions, and sticky values,
	 *     determine the x and y, and actual width and height of the
	 *     widget.
	 */

	doubleBw = 2 * Tk_Changes(panePtr->tkwin)->border_width;
	newPaneWidth = (panePtr->width > 0 ? panePtr->width :
		Tk_ReqWidth(panePtr->tkwin) + doubleBw);
	newPaneHeight = (panePtr->height > 0 ? panePtr->height :
		Tk_ReqHeight(panePtr->tkwin) + doubleBw);
	Tk_GetPixelsFromObj(NULL, panePtr->tkwin, panePtr->minSizeObj, &paneMinSize);

	/*
	 * Calculate pane width and height.
	 */

	if (horizontal) {
	    if (panePtr->width > 0) {
		paneSize = panePtr->width;
	    } else {
		paneSize = panePtr->paneWidth;
	    }
	    pwSize = pwWidth;
	} else {
	    if (panePtr->height > 0) {
		paneSize = panePtr->height;
	    } else {
		paneSize = panePtr->paneHeight;
	    }
	    pwSize = pwHeight;
	}
	if (IsStretchable(panePtr->stretch, i, first, last)) {
	    double frac;

	    if (paneDynSize > 0) {
		frac = (double)paneSize / (double)paneDynSize;
	    } else {
		frac = (double)paneSize / (double)pwSize;
	    }

	    paneDynSize -= paneSize;
	    paneDynMinSize -= paneMinSize;
	    stretchAmount = (int) (frac * stretchReserve);
	    if (paneSize + stretchAmount >= paneMinSize) {
		stretchReserve -= stretchAmount;
		paneSize += stretchAmount;
	    } else {
		stretchReserve += paneSize - paneMinSize;
		paneSize = paneMinSize;
	    }
	    if (i == last && stretchReserve > 0) {
		paneSize += stretchReserve;
		stretchReserve = 0;
	    }
	} else if (paneDynSize - paneDynMinSize + stretchReserve < 0) {
	    if (paneSize + paneDynSize - paneDynMinSize + stretchReserve
		    <= paneMinSize) {
		stretchReserve += paneSize - paneMinSize;
		paneSize = paneMinSize;
	    } else {
		paneSize += paneDynSize - paneDynMinSize + stretchReserve;
		stretchReserve = paneDynMinSize - paneDynSize;
	    }
	}
	Tk_GetPixelsFromObj(NULL, panePtr->tkwin, panePtr->padXObj, &padX);
	Tk_GetPixelsFromObj(NULL, panePtr->tkwin, panePtr->padYObj, &padY);
	if (horizontal) {
	    paneWidth = paneSize;
	    paneHeight = pwHeight - (2 * padY);
	} else {
	    paneWidth = pwWidth - (2 * padX);
	    paneHeight = paneSize;
	}

	/*
	 * Adjust for area reserved for sashes.
	 */

	if (sashCount) {
	    sashReserve = sashWidth * sashCount;
	    if (horizontal) {
		sxReserve = sashReserve;
		syReserve = 0;
	    } else {
		sxReserve = 0;
		syReserve = sashReserve;
	    }
	} else {
	    sxReserve = syReserve = 0;
	}

	if (pwWidth - sxReserve < x + paneWidth - internalBW) {
	    paneWidth = pwWidth - sxReserve - x + internalBW;
	}
	if (pwHeight - syReserve < y + paneHeight - internalBW) {
	    paneHeight = pwHeight - syReserve - y + internalBW;
	}

	if (newPaneWidth > paneWidth) {
	    newPaneWidth = paneWidth;
	}
	if (newPaneHeight > paneHeight) {
	    newPaneHeight = paneHeight;
	}

	panePtr->x = x;
	panePtr->y = y;

	/*
	 * Compute the location of the sash at the right or bottom of the
	 * parcel and the location of the next parcel.
	 */

	Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->handlePadObj, &handlePad);
	if (horizontal) {
	    x += paneWidth + (2 * padX);
	    if (x < internalBW) {
		x = internalBW;
	    }
	    panePtr->sashx = x + sashOffset;
	    panePtr->sashy = y;
	    panePtr->handlex = x + handleOffset;
	    panePtr->handley = y + handlePad;
	    x += sashWidth;
	} else {
	    y += paneHeight + (2 * padY);
	    if (y < internalBW) {
		y = internalBW;
	    }
	    panePtr->sashx = x;
	    panePtr->sashy = y + sashOffset;
	    panePtr->handlex = x + handlePad;
	    panePtr->handley = y + handleOffset;
	    y += sashWidth;
	}

	/*
	 * Compute the actual dimensions of the pane in the pane.
	 */

	paneX = panePtr->x;
	paneY = panePtr->y;
	AdjustForSticky(panePtr->sticky, paneWidth, paneHeight,
		&paneX, &paneY, &newPaneWidth, &newPaneHeight);

	paneX += padX;
	paneY += padY;

	/*
	 * Now put the window in the proper spot.
	 */

	if (newPaneWidth <= 0 || newPaneHeight <= 0 ||
		(horizontal ? paneX - internalBW > pwWidth :
		paneY - internalBW > pwHeight)) {
	    Tk_UnmaintainGeometry(panePtr->tkwin, pwPtr->tkwin);
	    Tk_UnmapWindow(panePtr->tkwin);
	} else {
	    Tk_MaintainGeometry(panePtr->tkwin, pwPtr->tkwin,
		    paneX, paneY, newPaneWidth, newPaneHeight);
	}
	sashCount--;
    }
    Tcl_Release(pwPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Unlink --
 *
 *	Remove a pane from a paned window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The paned window will be scheduled for re-arranging and redrawing.
 *
 *----------------------------------------------------------------------
 */

static void
Unlink(
    Pane *panePtr)		/* Window to unlink. */
{
    PanedWindow *containerPtr;
    int i, j;

    containerPtr = panePtr->containerPtr;
    if (containerPtr == NULL) {
	return;
    }

    /*
     * Find the specified pane in the panedwindow's list of panes, then
     * remove it from that list.
     */

    for (i = 0; i < containerPtr->numPanes; i++) {
	if (containerPtr->panes[i] == panePtr) {
	    for (j = i; j < containerPtr->numPanes - 1; j++) {
		containerPtr->panes[j] = containerPtr->panes[j + 1];
	    }
	    break;
	}
    }

    /*
     * Clean out any -after or -before references to this pane
     */

    for (i = 0; i < containerPtr->numPanes; i++) {
	if (containerPtr->panes[i]->before == panePtr->tkwin) {
	    containerPtr->panes[i]->before = NULL;
	}
	if (containerPtr->panes[i]->after == panePtr->tkwin) {
	    containerPtr->panes[i]->after = NULL;
	}
    }

    containerPtr->flags |= REQUESTED_RELAYOUT;
    if (!(containerPtr->flags & REDRAW_PENDING)) {
	containerPtr->flags |= REDRAW_PENDING;
	Tcl_DoWhenIdle(DisplayPanedWindow, containerPtr);
    }

    /*
     * Set the pane's containerPtr to NULL, so that we can tell that the pane
     * is no longer attached to any panedwindow.
     */

    panePtr->containerPtr = NULL;

    containerPtr->numPanes--;
}

/*
 *----------------------------------------------------------------------
 *
 * GetPane --
 *
 *	Given a token to a Tk window, find the pane that corresponds to that
 *	token in a given paned window.
 *
 * Results:
 *	Pointer to the pane structure, or NULL if the window is not managed
 *	by this paned window.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Pane *
GetPane(
    PanedWindow *pwPtr,		/* Pointer to the paned window info. */
    Tk_Window tkwin)		/* Window to search for. */
{
    int i;

    for (i = 0; i < pwPtr->numPanes; i++) {
	if (pwPtr->panes[i]->tkwin == tkwin) {
	    return pwPtr->panes[i];
	}
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * GetFirstLastVisiblePane --
 *
 *	Given panedwindow, find the index of the first and last visible panes
 *	of that paned window.
 *
 * Results:
 *	Index of the first and last visible panes.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
GetFirstLastVisiblePane(
    PanedWindow *pwPtr,		/* Pointer to the paned window info. */
    int *firstPtr,		/* Returned index for first. */
    int *lastPtr)		/* Returned index for last. */
{
    int i;

    for (i = 0, *lastPtr = 0, *firstPtr = -1; i < pwPtr->numPanes; i++) {
	if (pwPtr->panes[i]->hide == 0) {
	    if (*firstPtr < 0) {
		*firstPtr = i;
	    }
	    *lastPtr = i;
	}
    }
}

/*
 *--------------------------------------------------------------
 *
 * PaneStructureProc --
 *
 *	This function is invoked whenever StructureNotify events occur for a
 *	window that's managed by a paned window. This function's only purpose
 *	is to clean up when windows are deleted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The paned window pane structure associated with the window
 *	is freed, and the pane is disassociated from the paned
 *	window which managed it.
 *
 *--------------------------------------------------------------
 */

static void
PaneStructureProc(
    void *clientData,	/* Pointer to record describing window item. */
    XEvent *eventPtr)		/* Describes what just happened. */
{
    Pane *panePtr = (Pane *)clientData;
    PanedWindow *pwPtr = panePtr->containerPtr;

    if (eventPtr->type == DestroyNotify) {
	Unlink(panePtr);
	panePtr->tkwin = NULL;
	ckfree(panePtr);
	ComputeGeometry(pwPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ComputeGeometry --
 *
 *	Compute geometry for the paned window, including coordinates of all
 *	panes and each sash.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Recomputes geometry information for a paned window.
 *
 *----------------------------------------------------------------------
 */

static void
ComputeGeometry(
    PanedWindow *pwPtr)		/* Pointer to the Paned Window structure. */
{
    int i, x, y, doubleBw, internalBw;
    int sashWidth, sashOffset, handleOffset;
    int reqWidth, reqHeight, dim, handleSize;
    Pane *panePtr;
    const int horizontal = (pwPtr->orient == ORIENT_HORIZONTAL);
    int sashPad;
    int width = -1, height = -1;

    pwPtr->flags |= REQUESTED_RELAYOUT;

    x = y = internalBw = Tk_InternalBorderLeft(pwPtr->tkwin);
    reqWidth = reqHeight = 0;

    /*
     * Sashes and handles share space on the display. To simplify processing
     * below, precompute the x and y offsets of the handles and sashes within
     * the space occupied by their combination; later, just add those offsets
     * blindly (avoiding the extra showHandle, etc, checks).
     */

    Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->sashPadObj, &sashPad);
    sashOffset = handleOffset = sashPad;
    Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->handleSizeObj, &handleSize);
    Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->sashWidthObj, &sashWidth);
    if (pwPtr->showHandle && handleSize > sashWidth) {
	sashOffset = ((handleSize - sashWidth) / 2) + sashPad;
	sashWidth = (2 * sashPad) + handleSize;
    } else {
	handleOffset = ((sashWidth - handleSize) / 2) + sashPad;
	sashWidth = (2 * sashPad) + sashWidth;
    }

    for (i = 0; i < pwPtr->numPanes; i++) {
	int padX, padY, minSize, handlePad;
	panePtr = pwPtr->panes[i];

	if (panePtr->hide) {
	    continue;
	}

	/*
	 * First set the coordinates for the top left corner of the pane's
	 * parcel.
	 */

	panePtr->x = x;
	panePtr->y = y;

	/*
	 * Make sure the pane's paned dimension is at least minsize. This
	 * check may be redundant, since the only way to change a pane's size
	 * is by moving a sash, and that code checks the minsize.
	 */

	Tk_GetPixelsFromObj(NULL, panePtr->tkwin, panePtr->minSizeObj, &minSize);
	if (horizontal) {
	    if (panePtr->paneWidth < minSize) {
		panePtr->paneWidth = minSize;
	    }
	} else {
	    if (panePtr->paneHeight < minSize) {
		panePtr->paneHeight = minSize;
	    }
	}

	/*
	 * Compute the location of the sash at the right or bottom of the
	 * parcel.
	 */

	Tk_GetPixelsFromObj(NULL, panePtr->tkwin, panePtr->padXObj, &padX);
	Tk_GetPixelsFromObj(NULL, panePtr->tkwin, panePtr->padYObj, &padY);
	Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->handlePadObj, &handlePad);
	if (horizontal) {
	    x += panePtr->paneWidth + (2 * padX);
	    panePtr->sashx = x + sashOffset;
	    panePtr->sashy = y;
	    panePtr->handlex = x + handleOffset;
	    panePtr->handley = y + handlePad;
	    x += sashWidth;
	} else {
	    y += panePtr->paneHeight + (2 * padY);
	    panePtr->sashx = x;
	    panePtr->sashy = y + sashOffset;
	    panePtr->handlex = x + handlePad;
	    panePtr->handley = y + handleOffset;
	    y += sashWidth;
	}

	/*
	 * Find the maximum height/width of the panes, for computing the
	 * requested height/width of the paned window.
	 */

	if (horizontal) {
	    /*
	     * If the pane has an explicit height set, use that; otherwise,
	     * use the pane's requested height.
	     */

	    if (panePtr->height > 0) {
		dim = panePtr->height;
	    } else {
		doubleBw = 2 * Tk_Changes(panePtr->tkwin)->border_width;
		dim = Tk_ReqHeight(panePtr->tkwin) + doubleBw;
	    }
	    dim += 2 * padY;
	    if (dim > reqHeight) {
		reqHeight = dim;
	    }
	} else {
	    /*
	     * If the pane has an explicit width set use that; otherwise, use
	     * the pane's requested width.
	     */

	    if (panePtr->width > 0) {
		dim = panePtr->width;
	    } else {
		doubleBw = 2 * Tk_Changes(panePtr->tkwin)->border_width;
		dim = Tk_ReqWidth(panePtr->tkwin) + doubleBw;
	    }
	    dim += 2 * padX;
	    if (dim > reqWidth) {
		reqWidth = dim;
	    }
	}
    }

    /*
     * The loop above should have left x (or y) equal to the sum of the widths
     * (or heights) of the widgets, plus the size of one sash and the sash
     * padding for each widget, plus the width of the left (or top) border of
     * the paned window.
     *
     * The requested width (or height) is therefore x (or y) minus the size of
     * one sash and padding, plus the width of the right (or bottom) border of
     * the paned window.
     *
     * The height (or width) is equal to the maximum height (or width) of the
     * panes, plus the width of the border of the top and bottom (or left and
     * right) of the paned window.
     *
     * If the panedwindow has an explicit width/height set use that;
     * otherwise, use the requested width/height.
     */

    if (pwPtr->widthObj) {
	Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->widthObj, &width);
    }
    if (pwPtr->heightObj) {
	Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->heightObj, &height);
    }
    if (horizontal) {
	reqWidth = (width > 0 ?
		width : x - sashWidth + internalBw);
	reqHeight = (height > 0 ?
		height : reqHeight + (2 * internalBw));
    } else {
	reqWidth = (width > 0 ?
		width : reqWidth + (2 * internalBw));
	reqHeight = (height > 0 ?
		height : y - sashWidth + internalBw);
    }
    Tk_GeometryRequest(pwPtr->tkwin, reqWidth, reqHeight);
    if (Tk_IsMapped(pwPtr->tkwin) && !(pwPtr->flags & REDRAW_PENDING)) {
	pwPtr->flags |= REDRAW_PENDING;
	Tcl_DoWhenIdle(DisplayPanedWindow, pwPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyOptionTables --
 *
 *	This function is registered as an exit callback when the paned window
 *	command is first called. It cleans up the OptionTables structure
 *	allocated by that command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory.
 *
 *----------------------------------------------------------------------
 */

static void
DestroyOptionTables(
    void *clientData,	/* Pointer to the OptionTables struct */
    TCL_UNUSED(Tcl_Interp *))		/* Pointer to the calling interp */
{
    ckfree(clientData);
}

/*
 *----------------------------------------------------------------------
 *
 * GetSticky -
 *
 *	Converts an internal boolean combination of "sticky" bits into a Tcl
 *	string obj containing zero or more of n, s, e, or w.
 *
 * Results:
 *	Tcl_Obj containing the string representation of the sticky value.
 *
 * Side effects:
 *	Creates a new Tcl_Obj.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *
GetSticky(
    TCL_UNUSED(void *),
    TCL_UNUSED(Tk_Window),
    char *recordPtr,		/* Pointer to widget record. */
    Tcl_Size internalOffset)		/* Offset within *recordPtr containing the
				 * sticky value. */
{
    int sticky = *(int *)(recordPtr + internalOffset);
    char buffer[5];
    char *p = &buffer[0];

    if (sticky & STICK_NORTH) {
	*p++ = 'n';
    }
    if (sticky & STICK_EAST) {
	*p++ = 'e';
    }
    if (sticky & STICK_SOUTH) {
	*p++ = 's';
    }
    if (sticky & STICK_WEST) {
	*p++ = 'w';
    }
    *p = '\0';

    return Tcl_NewStringObj(buffer, TCL_INDEX_NONE);
}

/*
 *----------------------------------------------------------------------
 *
 * SetSticky --
 *
 *	Converts a Tcl_Obj representing a widgets stickyness into an integer
 *	value.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	May store the integer value into the internal representation pointer.
 *	May change the pointer to the Tcl_Obj to NULL to indicate that the
 *	specified string was empty and that is acceptable.
 *
 *----------------------------------------------------------------------
 */

static int
SetSticky(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,		/* Current interp; may be used for errors. */
    TCL_UNUSED(Tk_Window),	/* Window for which option is being set. */
    Tcl_Obj **value,		/* Pointer to the pointer to the value object.
				 * We use a pointer to the pointer because we
				 * may need to return a value (NULL). */
    char *recordPtr,		/* Pointer to storage for the widget record. */
    Tcl_Size internalOffset,		/* Offset within *recordPtr at which the
				 * internal value is to be stored. */
    char *oldInternalPtr,	/* Pointer to storage for the old value. */
    int flags)			/* Flags for the option, set Tk_SetOptions. */
{
    int sticky = 0;
    char c;
    void *internalPtr;
    const char *string;

    internalPtr = ComputeSlotAddress(recordPtr, internalOffset);

    if (flags & TK_OPTION_NULL_OK && TkObjIsEmpty(*value)) {
	*value = NULL;
    } else {
	/*
	 * Convert the sticky specifier into an integer value.
	 */

	string = Tcl_GetString(*value);

	while ((c = *string++) != '\0') {
	    switch (c) {
	    case 'n': case 'N':
		sticky |= STICK_NORTH;
		break;
	    case 'e': case 'E':
		sticky |= STICK_EAST;
		break;
	    case 's': case 'S':
		sticky |= STICK_SOUTH;
		break;
	    case 'w': case 'W':
		sticky |= STICK_WEST;
		break;
	    case ' ': case ',': case '\t': case '\r': case '\n':
		break;
	    default:
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"bad stickyness value \"%s\": must be a string"
			" containing zero or more of n, e, s, and w",
			Tcl_GetString(*value)));
		Tcl_SetErrorCode(interp, "TK", "VALUE", "STICKY", (char *)NULL);
		return TCL_ERROR;
	    }
	}
    }

    if (internalPtr != NULL) {
	*((int *) oldInternalPtr) = *((int *) internalPtr);
	*((int *) internalPtr) = sticky;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RestoreSticky --
 *
 *	Restore a sticky option value from a saved value.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Restores the old value.
 *
 *----------------------------------------------------------------------
 */

static void
RestoreSticky(
    TCL_UNUSED(void *),
    TCL_UNUSED(Tk_Window),
    char *internalPtr,		/* Pointer to storage for value. */
    char *oldInternalPtr)	/* Pointer to old value. */
{
    *(int *)internalPtr = *(int *)oldInternalPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * AdjustForSticky --
 *
 *	Given the x,y coords of the top-left corner of a pane, the dimensions
 *	of that pane, and the dimensions of a pane, compute the x,y coords
 *	and actual dimensions of the pane based on the pane's sticky value.
 *
 * Results:
 *	No direct return; sets the x, y, paneWidth and paneHeight to correct
 *	values.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
AdjustForSticky(
    int sticky,			/* Sticky value; see top of file for
				 * definition. */
    int cavityWidth,		/* Width of the cavity. */
    int cavityHeight,		/* Height of the cavity. */
    int *xPtr, int *yPtr,	/* Initially, coordinates of the top-left
				 * corner of cavity; also return values for
				 * actual x, y coords of pane. */
    int *paneWidthPtr,		/* Pane width. */
    int *paneHeightPtr)	/* Pane height. */
{
    int diffx = 0;		/* Cavity width - pane width. */
    int diffy = 0;		/* Cavity hight - pane height. */

    if (cavityWidth > *paneWidthPtr) {
	diffx = cavityWidth - *paneWidthPtr;
    }

    if (cavityHeight > *paneHeightPtr) {
	diffy = cavityHeight - *paneHeightPtr;
    }

    if ((sticky & STICK_EAST) && (sticky & STICK_WEST)) {
	*paneWidthPtr += diffx;
    }
    if ((sticky & STICK_NORTH) && (sticky & STICK_SOUTH)) {
	*paneHeightPtr += diffy;
    }
    if (!(sticky & STICK_WEST)) {
	*xPtr += (sticky & STICK_EAST) ? diffx : diffx/2;
    }
    if (!(sticky & STICK_NORTH)) {
	*yPtr += (sticky & STICK_SOUTH) ? diffy : diffy/2;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MoveSash --
 *
 *	Move the sash given by index the amount given.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Recomputes the sizes of the panes in a panedwindow.
 *
 *----------------------------------------------------------------------
 */

static void
MoveSash(
    PanedWindow *pwPtr,
    int sash,
    int diff)
{
    int i;
    int expandPane, reduceFirst, reduceLast, reduceIncr, paneSize, sashOffset;
    Pane *panePtr;
    int stretchReserve = 0;
    int nextSash = sash + 1;
    const int horizontal = (pwPtr->orient == ORIENT_HORIZONTAL);
    int handleSize, sashPad, sashWidth;

    if (diff == 0)
	return;

    /*
     * Update the pane sizes with their real sizes.
     */

    Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->handleSizeObj, &handleSize);
    Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->sashPadObj, &sashPad);
    Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->sashWidthObj, &sashWidth);
    if (pwPtr->showHandle && handleSize > sashWidth) {
	sashOffset = ((handleSize - sashWidth) / 2) + sashPad;
    } else {
	sashOffset = sashPad;
    }
    for (i = 0; i < pwPtr->numPanes; i++) {
	int padX, padY;

	panePtr = pwPtr->panes[i];
	if (panePtr->hide) {
	    continue;
	}
	Tk_GetPixelsFromObj(NULL, panePtr->tkwin, panePtr->padXObj, &padX);
	Tk_GetPixelsFromObj(NULL, panePtr->tkwin, panePtr->padYObj, &padY);
	if (horizontal) {
	    panePtr->paneWidth = panePtr->width = panePtr->sashx
		    - sashOffset - panePtr->x - (2 * padX);
	} else {
	    panePtr->paneHeight = panePtr->height = panePtr->sashy
		    - sashOffset - panePtr->y - (2 * padY);
	}
    }

    /*
     * There must be a next sash since it is only possible to enter this
     * routine when moving an actual sash which implies there exists a visible
     * pane to either side of the sash.
     */

    while (nextSash < pwPtr->numPanes-1 && pwPtr->panes[nextSash]->hide) {
	nextSash++;
    }

    /*
     * Consolidate +/-diff variables to reduce duplicate code.
     */

    if (diff > 0) {
	expandPane = sash;
	reduceFirst = nextSash;
	reduceLast = pwPtr->numPanes;
	reduceIncr = 1;
    } else {
	diff = abs(diff);
	expandPane = nextSash;
	reduceFirst = sash;
	reduceLast = -1;
	reduceIncr = -1;
    }

    /*
     * Calculate how much room we have to stretch in and adjust diff value
     * accordingly.
     */
    for (i = reduceFirst; i != reduceLast; i += reduceIncr) {
	int minSize;

	panePtr = pwPtr->panes[i];
	if (panePtr->hide) {
	    continue;
	}
	Tk_GetPixelsFromObj(NULL, panePtr->tkwin, panePtr->minSizeObj, &minSize);
	if (horizontal) {
	    stretchReserve += panePtr->width - minSize;
	} else {
	    stretchReserve += panePtr->height - minSize;
	}
    }
    if (stretchReserve <= 0) {
	return;
    }
    if (diff > stretchReserve) {
	diff = stretchReserve;
    }

    /*
     * Expand pane by diff amount.
     */

    panePtr = pwPtr->panes[expandPane];
    if (horizontal) {
	panePtr->paneWidth = panePtr->width += diff;
    } else {
	panePtr->paneHeight = panePtr->height += diff;
    }

    /*
     * Reduce panes, respecting minsize, until diff amount has been used.
     */

    for (i = reduceFirst; i != reduceLast; i += reduceIncr) {
	int minSize;

	panePtr = pwPtr->panes[i];
	if (panePtr->hide) {
	    continue;
	}
	if (horizontal) {
	    paneSize = panePtr->width;
	} else {
	    paneSize = panePtr->height;
	}
	Tk_GetPixelsFromObj(NULL, panePtr->tkwin, panePtr->minSizeObj, &minSize);
	if (diff > (paneSize - minSize)) {
	    diff -= paneSize - minSize;
	    paneSize = minSize;
	} else {
	    paneSize -= diff;
	    i = reduceLast - reduceIncr;
	}
	if (horizontal) {
	    panePtr->paneWidth = panePtr->width = paneSize;
	} else {
	    panePtr->paneHeight = panePtr->height = paneSize;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ProxyWindowEventProc --
 *
 *	This function is invoked by the Tk dispatcher for various events on
 *	paned window proxy windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	When the window gets deleted, internal structures get cleaned up. When
 *	it gets exposed, it is redisplayed.
 *
 *--------------------------------------------------------------
 */

static void
ProxyWindowEventProc(
    void *clientData,	/* Information about window. */
    XEvent *eventPtr)		/* Information about event. */
{
    PanedWindow *pwPtr = (PanedWindow *)clientData;

    if (eventPtr->type == Expose) {
	if (pwPtr->proxywin != NULL &&!(pwPtr->flags & PROXY_REDRAW_PENDING)) {
	    Tcl_DoWhenIdle(DisplayProxyWindow, pwPtr);
	    pwPtr->flags |= PROXY_REDRAW_PENDING;
	}
    }
}

/*
 *--------------------------------------------------------------
 *
 * DisplayProxyWindow --
 *
 *	This function redraws a paned window proxy window. It is invoked as a
 *	do-when-idle handler, so it only runs when there's nothing else for
 *	the application to do.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information appears on the screen.
 *
 *--------------------------------------------------------------
 */

static void
DisplayProxyWindow(
    void *clientData)	/* Information about window. */
{
    PanedWindow *pwPtr = (PanedWindow *)clientData;
    Pixmap pixmap;
    Tk_Window tkwin = pwPtr->proxywin;
    int proxyBorderWidth;

    pwPtr->flags &= ~PROXY_REDRAW_PENDING;
    if ((tkwin == NULL) || !Tk_IsMapped(tkwin)) {
	return;
    }

#ifndef TK_NO_DOUBLE_BUFFERING
    /*
     * Create a pixmap for double-buffering, if necessary.
     */

    pixmap = Tk_GetPixmap(Tk_Display(tkwin), Tk_WindowId(tkwin),
	    Tk_Width(tkwin), Tk_Height(tkwin), Tk_Depth(tkwin));
#else
    pixmap = Tk_WindowId(tkwin);
#endif /* TK_NO_DOUBLE_BUFFERING */

    /*
     * Redraw the widget's background and border.
     */

    Tk_GetPixelsFromObj(NULL, tkwin, pwPtr->proxyBorderWidthObj, &proxyBorderWidth);
    Tk_Fill3DRectangle(tkwin, pixmap,
	    pwPtr->proxyBackground ? pwPtr->proxyBackground : pwPtr->background,
	    0, 0, Tk_Width(tkwin), Tk_Height(tkwin), proxyBorderWidth,
	    (pwPtr->proxyRelief != TK_RELIEF_NULL) ? pwPtr->proxyRelief : pwPtr->sashRelief);

#ifndef TK_NO_DOUBLE_BUFFERING
    /*
     * Copy the pixmap to the display.
     */

    XCopyArea(Tk_Display(tkwin), pixmap, Tk_WindowId(tkwin), pwPtr->gc, 0, 0,
	    (unsigned) Tk_Width(tkwin), (unsigned) Tk_Height(tkwin), 0, 0);
    Tk_FreePixmap(Tk_Display(tkwin), pixmap);
#endif /* TK_NO_DOUBLE_BUFFERING */
}

/*
 *----------------------------------------------------------------------
 *
 * PanedWindowProxyCommand --
 *
 *	Handles the panedwindow proxy subcommand. See the user documentation
 *	for details.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	May map or unmap the proxy sash.
 *
 *----------------------------------------------------------------------
 */

static int
PanedWindowProxyCommand(
    PanedWindow *pwPtr,		/* Pointer to paned window information. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    static const char *const optionStrings[] = {
	"coord", "forget", "place", NULL
    };
    enum options {
	PROXY_COORD, PROXY_FORGET, PROXY_PLACE
    };
    int index, x, y, sashWidth, sashHeight;
    int internalBW, pwWidth, pwHeight;
    Tcl_Obj *coords[2];

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "option ?arg ...?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[2], optionStrings, "option", 0,
	    &index) != TCL_OK) {
	return TCL_ERROR;
    }

    switch ((enum options) index) {
    case PROXY_COORD:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, NULL);
	    return TCL_ERROR;
	}

	coords[0] = Tcl_NewWideIntObj(pwPtr->proxyx);
	coords[1] = Tcl_NewWideIntObj(pwPtr->proxyy);
	Tcl_SetObjResult(interp, Tcl_NewListObj(2, coords));
	break;

    case PROXY_FORGET:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, NULL);
	    return TCL_ERROR;
	}
	if (Tk_IsMapped(pwPtr->proxywin)) {
	    Tk_UnmapWindow(pwPtr->proxywin);
	    Tk_UnmaintainGeometry(pwPtr->proxywin, pwPtr->tkwin);
	}
	break;

    case PROXY_PLACE:
	if (objc != 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "x y");
	    return TCL_ERROR;
	}

	if (Tcl_GetIntFromObj(interp, objv[3], &x) != TCL_OK) {
	    return TCL_ERROR;
	}

	if (Tcl_GetIntFromObj(interp, objv[4], &y) != TCL_OK) {
	    return TCL_ERROR;
	}

	internalBW = Tk_InternalBorderLeft(pwPtr->tkwin);
	Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->sashWidthObj, &sashWidth);
	if (pwPtr->orient == ORIENT_HORIZONTAL) {
	    if (x < 0) {
		x = 0;
	    }
	    pwWidth = Tk_Width(pwPtr->tkwin) - (2 * internalBW);
	    if (x > pwWidth) {
		x = pwWidth;
	    }
	    y = Tk_InternalBorderLeft(pwPtr->tkwin);
	    sashHeight = Tk_Height(pwPtr->tkwin) -
		    (2 * Tk_InternalBorderLeft(pwPtr->tkwin));
	} else {
	    if (y < 0) {
		y = 0;
	    }
	    pwHeight = Tk_Height(pwPtr->tkwin) - (2 * internalBW);
	    if (y > pwHeight) {
		y = pwHeight;
	    }
	    x = Tk_InternalBorderLeft(pwPtr->tkwin);
	    sashHeight = sashWidth;
	    sashWidth = Tk_Width(pwPtr->tkwin) -
		    (2 * Tk_InternalBorderLeft(pwPtr->tkwin));
	}

	if (sashWidth < 1) {
	    sashWidth = 1;
	}
	if (sashHeight < 1) {
	    sashHeight = 1;
	}

	/*
	 * Stash the proxy coordinates for future "proxy coord" calls.
	 */

	pwPtr->proxyx = x;
	pwPtr->proxyy = y;

	/*
	 * Make sure the proxy window is higher in the stacking order than the
	 * panes, so that it will be visible when drawn. It would be more
	 * correct to push the proxy window just high enough to appear above
	 * the highest pane, but it's much easier to just force it all the
	 * way to the top of the stacking order.
	 */

	Tk_RestackWindow(pwPtr->proxywin, Above, NULL);

	/*
	 * Let Tk_MaintainGeometry take care of placing the window at the
	 * right coordinates.
	 */

	Tk_MaintainGeometry(pwPtr->proxywin, pwPtr->tkwin, x, y,
		sashWidth, sashHeight);
	break;
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ComputeInternalPointer --
 *
 *	Given a pointer to the start of a record and the offset of a slot
 *	within that record, compute the address of that slot.
 *
 * Results:
 *	If offset is non-negative, returns the computed address; else, returns
 *	NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void *
ComputeSlotAddress(
    void *recordPtr,	/* Pointer to the start of a record. */
    Tcl_Size offset)		/* Offset of a slot within that record; may be TCL_INDEX_NONE. */
{
    if (offset != TCL_INDEX_NONE) {
	return (char *)recordPtr + offset;
    } else {
	return NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PanedWindowIdentifyCoords --
 *
 *	Given a pair of x,y coordinates, identify the panedwindow component at
 *	that point, if any.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Modifies the interpreter's result to contain either an empty list, or
 *	a two element list of the form {sash n} or {handle n} to indicate that
 *	the point lies within the n'th sash or handle.
 *
 *----------------------------------------------------------------------
 */

static int
PanedWindowIdentifyCoords(
    PanedWindow *pwPtr,		/* Information about the widget. */
    Tcl_Interp *interp,		/* Interpreter in which to store result. */
    int x, int y)		/* Coordinates of the point to identify. */
{
    int i, sashHeight, sashWidth, thisx, thisy;
    int found, isHandle, lpad, rpad, tpad, bpad;
    int first, last, handleSize, sashPad;

    Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->handleSizeObj, &handleSize);
    Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->sashPadObj, &sashPad);
    Tk_GetPixelsFromObj(NULL, pwPtr->tkwin, pwPtr->sashWidthObj, &sashWidth);
    if (pwPtr->orient == ORIENT_HORIZONTAL) {
	if (Tk_IsMapped(pwPtr->tkwin)) {
	    sashHeight = Tk_Height(pwPtr->tkwin);
	} else {
	    sashHeight = Tk_ReqHeight(pwPtr->tkwin);
	}
	sashHeight -= 2 * Tk_InternalBorderLeft(pwPtr->tkwin);
	if (pwPtr->showHandle && handleSize > sashWidth) {
	    lpad = (handleSize - sashWidth) / 2;
	    rpad = handleSize - lpad;
	    lpad += sashPad;
	    rpad += sashPad;
	    sashWidth = handleSize;
	} else {
	    lpad = rpad = sashPad;
	}
	tpad = bpad = 0;
    } else {
	if (pwPtr->showHandle && handleSize > sashWidth) {
	    sashHeight = handleSize;
	    tpad = (handleSize - sashWidth) / 2;
	    bpad = handleSize - tpad;
	    tpad += sashPad;
	    bpad += sashPad;
	} else {
	    sashHeight = sashWidth;
	    tpad = bpad = sashPad;
	}
	if (Tk_IsMapped(pwPtr->tkwin)) {
	    sashWidth = Tk_Width(pwPtr->tkwin);
	} else {
	    sashWidth = Tk_ReqWidth(pwPtr->tkwin);
	}
	sashWidth -= 2 * Tk_InternalBorderLeft(pwPtr->tkwin);
	lpad = rpad = 0;
    }

    GetFirstLastVisiblePane(pwPtr, &first, &last);
    isHandle = 0;
    found = -1;
    for (i = 0; i < pwPtr->numPanes - 1; i++) {
	if (pwPtr->panes[i]->hide || i == last) {
	    continue;
	}
	thisx = pwPtr->panes[i]->sashx;
	thisy = pwPtr->panes[i]->sashy;

	if (((thisx - lpad) <= x && x <= (thisx + rpad + sashWidth)) &&
		((thisy - tpad) <= y && y <= (thisy + bpad + sashHeight))) {
	    found = i;

	    /*
	     * Determine if the point is over the handle or the sash.
	     */

	    if (pwPtr->showHandle) {
		thisx = pwPtr->panes[i]->handlex;
		thisy = pwPtr->panes[i]->handley;
		if (pwPtr->orient == ORIENT_HORIZONTAL) {
		    if (thisy <= y && y <= (thisy + handleSize)) {
			isHandle = 1;
		    }
		} else {
		    if (thisx <= x && x <= (thisx + handleSize)) {
			isHandle = 1;
		    }
		}
	    }
	    break;
	}
    }

    /*
     * Set results. Note that the empty string is the default (this function
     * is called inside the implementation of a command).
     */

    if (found != -1) {
	Tcl_Obj *list[2];

	list[0] = Tcl_NewWideIntObj(found);
	list[1] = Tcl_NewStringObj((isHandle ? "handle" : "sash"), TCL_INDEX_NONE);
	Tcl_SetObjResult(interp, Tcl_NewListObj(2, list));
    }
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
