/*
 * tkoPath.c --
 *
 *    This module implements canvas widgets for the Tk toolkit. A canvas
 *    displays a background and a collection of graphical objects such as
 *    rectangles, lines, and texts.
 *
 * Copyright (c) 1991-1994 The Regents of the University of California.
 * Copyright (c) 1994-1997 Sun Microsystems, Inc.
 * Copyright (c) 1998-1999 by Scriptics Corporation.
 * Copyright (c) 2008 Mats Bengtsson
 * Copyright (c) 2019 Rene Zaumseil
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#ifdef MAC_OSX_TK
#define TK_PATH_NO_DOUBLE_BUFFERING
#endif

#include "tkoPath.h"

int Tk_PathAntiAlias = 1;
int Tk_PathDepixelize = 1;
int Tk_PathSurfaceCopyPremultiplyAlpha = 1;

/*
 * Flag bits for canvases:
 *
 * REDRAW_PENDING -        1 means a DoWhenIdle handler has already been
 *                created to redraw some or all of the canvas.
 * REDRAW_BORDERS -         1 means that the borders need to be redrawn
 *                during the next redisplay operation.
 * REPICK_NEEDED -        1 means PathDisplay should pick a new
 *                current item before redrawing the canvas.
 * GOT_FOCUS -            1 means the focus is currently in this widget,
 *                so should draw the insertion cursor and
 *                traversal highlight.
 * CURSOR_ON -            1 means the insertion cursor is in the "on"
 *                phase of its blink cycle. 0 means either we
 *                don't have the focus or the cursor is in the
 *                "off" phase of its cycle.
 * UPDATE_SCROLLBARS -        1 means the scrollbars should get updated as
 *                part of the next display operation.
 * LEFT_GRABBED_ITEM -        1 means that the mouse left the current item
 *                while a grab was in effect, so we didn't
 *                change path->currentItemPtr.
 * REPICK_IN_PROGRESS -        1 means PickCurrentItem is currently
 *                executing. If it should be called recursively,
 *                it should simply return immediately.
 * BBOX_NOT_EMPTY -        1 means that the bounding box of the area that
 *                should be redrawn is not empty.
 */
#define REDRAW_PENDING        (1 << 1)
#define REDRAW_BORDERS        (1 << 2)
#define REPICK_NEEDED        (1 << 3)
#define GOT_FOCUS        (1 << 4)
#define CURSOR_ON        (1 << 5)
#define UPDATE_SCROLLBARS    (1 << 6)
#define LEFT_GRABBED_ITEM    (1 << 7)
#define REPICK_IN_PROGRESS    (1 << 8)
#define BBOX_NOT_EMPTY        (1 << 9)

/*
 * Flag bits for canvas items (redraw_flags):
 *
 * FORCE_REDRAW -        1 means that the new coordinates of some item
 *                are not yet registered using
 *                Tk_PathCanvasEventuallyRedraw(). It should still
 *                be done by the general canvas code.
 */
#define FORCE_REDRAW        8

/*
 * TagSearch --
 *
 * The structure defined below is used to keep track of a tag search in
 * progress. No field should be accessed by anyone other than TagSearchScan,
 * TagSearchFirst, TagSearchNext, TagSearchScanExpr, TagSearchEvalExpr,
 * TagSearchExprInit, TagSearchExprDestroy, TagSearchDestroy.
 * (
 *   Not quite accurate: the TagSearch structure is also accessed from:
 *    CanvasWidgetCmd, FindItems, RelinkItems
 *   The only instances of the structure are owned by:
 *    CanvasWidgetCmd
 *   CanvasWidgetCmd is the only function that calls:
 *    FindItems, RelinkItems
 *   CanvasWidgetCmd, FindItems, RelinkItems, are the only functions that call
 *    TagSearch*
 * )
 */
typedef struct TagSearch {
    TkPathCanvas *path;        /* Canvas widget being searched. */
    Tk_PathItem *currentPtr;   /* Pointer to last item returned. */
    Tk_PathItem *lastPtr;      /* The item right before the currentPtr is
                                * tracked so if the currentPtr is deleted we
                                * don't have to start from the beginning. */
    int searchOver;            /* Non-zero means NextItem should always
                                * return NULL. */
    int type;                  /* Search type (see #defs below) */
    int id;                    /* Item id for searches by id */
    char *string;              /* Tag expression string */
    int stringIndex;           /* Current position in string scan */
    int stringLength;          /* Length of tag expression string */
    char *rewritebuffer;       /* Tag string (after removing escapes) */
    unsigned int rewritebufferAllocated;
    /* Available space for rewrites. */
    TkPathTagSearchExpr *expr; /* Compiled tag expression. */
} TagSearch;

/*
 * Values for the TagSearch type field.
 */
#define SEARCH_TYPE_EMPTY    0  /* Looking for empty tag */
#define SEARCH_TYPE_ID        1 /* Looking for an item by id */
#define SEARCH_TYPE_ALL        2        /* Looking for all items */
#define SEARCH_TYPE_TAG        3        /* Looking for an item by simple tag */
#define SEARCH_TYPE_EXPR    4   /* Compound search */
#define SEARCH_TYPE_ROOT    5   /* Looking for the root item */

#define PATH_DEF_STATE "normal"

/* These MUST be kept in sync with enums! X.h */

static const char *stateStrings[] = {
    "active", "disabled", "normal", "hidden", NULL
};

static const char *tagStyleStrings[] = {
    "exact", "expr", "glob", NULL
};

/*
 * List of all the item types known at present.
 */
static Tk_PathItemType *typeList = NULL;        /* NULL means initialization hasn't
                                                 * been done yet. */

/*
 * Uids for operands in compiled advanced tag search expressions.
 * Initialization is done by GetStaticUids()
 */
typedef struct {
    Tk_Uid allUid;             /* "all" */
    Tk_Uid currentUid;         /* "current" */
    Tk_Uid rootUid;            /* "root" */
    Tk_Uid andUid;
    Tk_Uid orUid;
    Tk_Uid xorUid;
    Tk_Uid parenUid;
    Tk_Uid negparenUid;
    Tk_Uid endparenUid;
    Tk_Uid tagvalUid;
    Tk_Uid negtagvalUid;
} SearchUids;

static Tcl_ThreadDataKey dataKey;
static SearchUids *GetStaticUids(
    void);

/*
* Macros that significantly simplify all code that finds items.
*/

#define FIRST_CANVAS_ITEM_MATCHING(objPtr,searchPtrPtr,errorExitClause) \
    if ((result = TagSearchScan(path,(objPtr),(searchPtrPtr))) != TCL_OK) { \
    errorExitClause; \
    } \
    itemPtr = TagSearchFirst(*(searchPtrPtr));

#define FOR_EVERY_CANVAS_ITEM_MATCHING(objPtr,searchPtrPtr,errorExitClause) \
    if ((result = TagSearchScan(path,(objPtr),(searchPtrPtr))) != TCL_OK) { \
    errorExitClause; \
    } \
    for (itemPtr = TagSearchFirst(*(searchPtrPtr)); \
        itemPtr != NULL; itemPtr = TagSearchNext(*(searchPtrPtr)))
#if defined(_WIN32) && defined(AGG_CUSTOM_ALLOCATOR)
/* AGG custom allocator functions */
void *(
    *agg_custom_alloc) (
    unsigned int size) = NULL;
void (
    *agg_custom_free) (
    void *ptr) = NULL;
#endif

/*
* Methods
*/
static int PathConstructor(
	ClientData clientData,
	Tcl_Interp * interp,
	Tcl_ObjectContext context,
	int objc,
	Tcl_Obj * const objv[]);
static int PathDestructor(
	ClientData clientData,
	Tcl_Interp * interp,
	Tcl_ObjectContext context,
	int objc,
	Tcl_Obj * const objv[]);
static int PathMethod(
	ClientData clientData,
	Tcl_Interp * interp,
	Tcl_ObjectContext context,
	int objc,
	Tcl_Obj * const objv[]);
static int PathMethod_tko_configure(
	ClientData clientData,
	Tcl_Interp * interp,
	Tcl_ObjectContext context,
	int objc,
	Tcl_Obj * const objv[]);
static int PathMethod_offset(
	ClientData clientData,
	Tcl_Interp * interp,
	Tcl_ObjectContext context,
	int objc,
	Tcl_Obj * const objv[]);
static int PathMethod_state(
	ClientData clientData,
	Tcl_Interp * interp,
	Tcl_ObjectContext context,
	int objc,
	Tcl_Obj * const objv[]);
static int PathMethod_tagstyle(
	ClientData clientData,
	Tcl_Interp * interp,
	Tcl_ObjectContext context,
	int objc,
	Tcl_Obj * const objv[]);

/*
 * Functions
 */
static void CanvasBindProc(
    ClientData clientData,
    XEvent * eventPtr);
static void CanvasBlinkProc(
    ClientData clientData);
static void CanvasDoEvent(
    TkPathCanvas * path,
    XEvent * eventPtr);
static void CanvasEventProc(
    ClientData clientData,
    XEvent * eventPtr);
static int CanvasFetchSelection(
    ClientData clientData,
    int offset,
    char *buffer,
    int maxBytes);
static Tk_PathItem *CanvasFindClosest(
    TkPathCanvas * path,
    double coords[2]);
static void CanvasFocusProc(
    TkPathCanvas * path,
    int gotFocus);
static void CanvasLostSelection(
    ClientData clientData);
static void CanvasSelectTo(
    TkPathCanvas * path,
    Tk_PathItem * itemPtr,
    int index);
static void CanvasSetOrigin(
    TkPathCanvas * path,
    int xOrigin,
    int yOrigin);
static void CanvasUpdateScrollbars(
    TkPathCanvas * path);
static void PathCanvasWorldChanged(
    ClientData instanceData);
static void PathDisplay(
    ClientData clientData);
static void DoItem(
    Tcl_Interp * interp,
    Tk_PathItem * itemPtr,
    Tk_Uid tag);
static void EventuallyRedrawItem(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr);
static void EventuallyRedrawItemAndChildren(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr);
static Tcl_Obj *UnshareObj(
    Tcl_Obj * objPtr);
#ifdef NOT_USED
static Tk_PathItem *ItemIteratorSubNext(
    Tk_PathItem * itemPtr,
    Tk_PathItem * groupPtr);
#endif
static void ItemAddToParent(
    Tk_PathItem * parentPtr,
    Tk_PathItem * itemPtr);
static void ItemDelete(
    TkPathCanvas * path,
    Tk_PathItem * itemPtr);
static int ItemCreate(
    Tcl_Interp * interp,
    TkPathCanvas * path,
    Tk_PathItemType * typePtr,
    int isRoot,
    Tk_PathItem ** itemPtrPtr,
    int objc,
    Tcl_Obj * const objv[]);
static int ItemGetNumTags(
    Tk_PathItem * itemPtr);
static void SetAncestorsDirtyBbox(
    Tk_PathItem * itemPtr);
static void DebugGetItemInfo(
    Tk_PathItem * itemPtr,
    char *s);
static int FindItems(
    Tcl_Interp * interp,
    TkPathCanvas * path,
    int objc,
    Tcl_Obj * const *objv,
    Tcl_Obj * newTagObj,
    int first,
    TagSearch ** searchPtrPtr);
static int FindArea(
    Tcl_Interp * interp,
    TkPathCanvas * path,
    Tcl_Obj * const *objv,
    Tk_Uid uid,
    int enclosed);
static double GridAlign(
    double coord,
    double spacing);
static const char **GetStringsFromObjs(
    int objc,
    Tcl_Obj * const *objv);
static void PickCurrentItem(
    TkPathCanvas * path,
    XEvent * eventPtr);
static Tcl_Obj *ScrollFractions(
    int screen1,
    int screen2,
    int object1,
    int object2);
static int RelinkItems(
    TkPathCanvas * path,
    Tcl_Obj * tag,
    Tk_PathItem * prevPtr,
    TagSearch ** searchPtrPtr);
static void TagSearchExprInit(
    TkPathTagSearchExpr ** exprPtrPtr);
static void TagSearchExprDestroy(
    TkPathTagSearchExpr * expr);
static void TagSearchDestroy(
    TagSearch * searchPtr);
static int TagSearchScan(
    TkPathCanvas * path,
    Tcl_Obj * tag,
    TagSearch ** searchPtrPtr);
static int TagSearchScanExpr(
    Tcl_Interp * interp,
    TagSearch * searchPtr,
    TkPathTagSearchExpr * expr);
static int TagSearchEvalExpr(
    TkPathTagSearchExpr * expr,
    Tk_PathItem * itemPtr);
static Tk_PathItem *TagSearchFirst(
    TagSearch * searchPtr);
static Tk_PathItem *TagSearchNext(
    TagSearch * searchPtr);
static void PathMetaDestroy(
    TkPathCanvas * path);
static void
PathMetaDelete(
    ClientData clientData)
{
    Tcl_EventuallyFree(clientData, (Tcl_FreeProc *) PathMetaDestroy);
}

/*
* pathMeta --
*/
static Tcl_ObjectMetadataType pathMeta = {
    TCL_OO_METADATA_VERSION_CURRENT,
    "PathMeta",
    PathMetaDelete,
    NULL
};

/* 
 * pathOptionDefine --
 *
 * Options and option methods created in class constructor.
 */
static tkoWidgetOptionDefine pathOptionDefine[] = {
    {"-class", "class", "Class", "TkoPath", TKO_OPTION_READONLY, NULL,
	NULL, NULL, TKO_SET_CLASS, NULL, 0},
    {"-background", "background", "Background", DEF_CANVAS_BG_COLOR, 0, NULL,
	NULL, NULL, TKO_SET_3DBORDER, &pathMeta, offsetof(TkPathCanvas, bgBorder)},
    {"-bd", "-borderwidth", NULL, NULL, 0, NULL, NULL, NULL, 0, NULL, 0},
    {"-bg", "-background", NULL, NULL, 0, NULL, NULL, NULL, 0, NULL, 0},
    {"-borderwidth", "borderWidth", "BorderWidth", DEF_CANVAS_BORDER_WIDTH, 0, NULL,
	NULL, NULL, TKO_SET_PIXELNONEGATIV, &pathMeta, offsetof(TkPathCanvas,borderWidth)},
    {"-closeenough", "closeEnough", "CloseEnough", DEF_CANVAS_CLOSE_ENOUGH, 0, NULL,
	NULL, NULL, TKO_SET_DOUBLE, &pathMeta, offsetof(TkPathCanvas, closeEnough)},
    {"-confine", "confine", "Confine", DEF_CANVAS_CONFINE, 0, NULL,
	NULL, NULL, TKO_SET_BOOLEAN, &pathMeta, offsetof(TkPathCanvas, confine)},
    {"-cursor", "cursor", "Cursor", DEF_CANVAS_CURSOR, 0, NULL,
	NULL, NULL, TKO_SET_CURSOR, &pathMeta, offsetof(TkPathCanvas, cursor)},
    {"-height", "height", "Height", DEF_CANVAS_HEIGHT, 0, NULL,
	NULL, NULL, TKO_SET_PIXEL, &pathMeta, offsetof(TkPathCanvas, height)},
    {"-highlightbackground", "highlightBackground", "HighlightBackground", DEF_CANVAS_HIGHLIGHT_BG, 0, NULL,
	NULL, NULL, TKO_SET_XCOLOR, &pathMeta, offsetof(TkPathCanvas, highlightBgColorPtr)},
    {"-highlightcolor", "highlightColor", "HighlightColor", DEF_CANVAS_HIGHLIGHT, 0, NULL,
	NULL, NULL, TKO_SET_XCOLOR, &pathMeta, offsetof(TkPathCanvas, highlightColorPtr)},
    {"-highlightthickness", "highlightThickness", "HighlightThickness", DEF_CANVAS_HIGHLIGHT_WIDTH, 0, NULL,
	NULL, NULL, TKO_SET_PIXELNONEGATIV, &pathMeta, offsetof(TkPathCanvas,highlightWidth)},
    {"-insertbackground", "insertBackground", "Foreground", DEF_CANVAS_INSERT_BG, 0, NULL,
	NULL, NULL, TKO_SET_3DBORDER, &pathMeta, offsetof(TkPathCanvas,textInfo.insertBorder)},
    {"-insertborderwidth", "insertBorderWidth", "BorderWidth", DEF_CANVAS_INSERT_BD_COLOR, 0, NULL,
	NULL, NULL, TKO_SET_PIXELNONEGATIV, &pathMeta, offsetof(TkPathCanvas,textInfo.insertBorderWidth)},
    {"-insertofftime", "insertOffTime", "OffTime", DEF_CANVAS_INSERT_OFF_TIME, 0, NULL,
	NULL, NULL, TKO_SET_INT, &pathMeta, offsetof(TkPathCanvas, insertOffTime)},
    {"-insertontime", "insertOnTime", "OnTime", DEF_CANVAS_INSERT_ON_TIME, 0, NULL,
	NULL, NULL, TKO_SET_INT, &pathMeta, offsetof(TkPathCanvas, insertOnTime)},
    {"-insertwidth", "insertWidth", "InsertWidth", DEF_CANVAS_INSERT_ON_TIME, 0, NULL,
	NULL, NULL, TKO_SET_PIXELNONEGATIV, &pathMeta, offsetof(TkPathCanvas,textInfo.insertWidth)},
    {"-offset", "offset", "Offset", "0,0", 0, NULL,
	NULL, PathMethod_offset, 0, NULL, 0},
    {"-relief", "relief", "Relief", DEF_CANVAS_RELIEF, 0, NULL,
	NULL, NULL, TKO_SET_RELIEF, &pathMeta, offsetof(TkPathCanvas, relief)},
    {"-scrollregion", "scrollRegion", "ScrollRegion", DEF_CANVAS_SCROLL_REGION, 0, NULL,
	NULL, NULL, TKO_SET_SCROLLREGION, &pathMeta, offsetof(TkPathCanvas, scroll)},
    {"-selectbackground", "selectBackground", "Foreground", DEF_CANVAS_SELECT_COLOR, 0, NULL,
	NULL, NULL, TKO_SET_3DBORDER, &pathMeta, offsetof(TkPathCanvas,textInfo.selBorder)},
    {"-selectborderwidth", "selectBorderWidth", "BorderWidth", DEF_CANVAS_SELECT_BD_COLOR,0, NULL,
	NULL, NULL, TKO_SET_PIXELNONEGATIV, &pathMeta, offsetof(TkPathCanvas,textInfo.selBorderWidth)},
    {"-selectforeground", "selectForeground", "Background", DEF_CANVAS_SELECT_FG_COLOR, 0, NULL,
	NULL, NULL, TKO_SET_XCOLOR, &pathMeta, offsetof(TkPathCanvas,textInfo.selFgColorPtr)},
    {"-state", "state", "State", PATH_DEF_STATE, 0, NULL,
	NULL, PathMethod_state, 0, NULL, 0},
    {"-tagstyle", "", "", "expr", 0, NULL,
	NULL, PathMethod_tagstyle, 0, NULL, 0},
    {"-takefocus", "takeFocus", "TakeFocus", DEF_CANVAS_TAKE_FOCUS, 0, NULL,
	NULL, NULL, TKO_SET_STRING, NULL, 0},
    {"-width", "width", "Width", DEF_CANVAS_WIDTH, 0, NULL,
	NULL, NULL, TKO_SET_PIXEL, &pathMeta, offsetof(TkPathCanvas, width)},
    {"-xscrollcommand", "xScrollCommand", "ScrollCommand", DEF_CANVAS_X_SCROLL_CMD, 0, NULL,
	NULL, NULL, TKO_SET_STRINGNULL, &pathMeta, offsetof(TkPathCanvas, xScrollCmd)},
    {"-xscrollincrement", "xScrollIncrement", "ScrollIncrement", DEF_CANVAS_X_SCROLL_INCREMENT, 0, NULL,
	NULL, NULL, TKO_SET_PIXEL, &pathMeta, offsetof(TkPathCanvas, xScrollIncrement)},
    {"-yscrollcommand", "yScrollCommand", "ScrollCommand", DEF_CANVAS_Y_SCROLL_CMD, 0, NULL,
	NULL, NULL, TKO_SET_STRINGNULL, &pathMeta, offsetof(TkPathCanvas, yScrollCmd)},
    {"-yscrollincrement", "yScrollIncrement", "ScrollIncrement", DEF_CANVAS_Y_SCROLL_INCREMENT, 0, NULL,
	NULL, NULL, TKO_SET_PIXEL, &pathMeta, offsetof(TkPathCanvas, yScrollIncrement)},
    {NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, 0, NULL, 0}
};

/*
 * pathMethods --
 *
 * Methods created in class constructor.
 */
static Tcl_MethodType pathMethods[] = {
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, PathConstructor, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, PathDestructor, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "addtag", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "ancestors", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "bbox", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "bind", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "canvasx", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "canvasy", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "children", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "cmove", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "coords", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "create", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "cscale", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "dchars", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "delete", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "depth", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "distance", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "dtag", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "find", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "firstchild", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "focus", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "gettags", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "gradient", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "icursor", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "index", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "insert", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "itemcget", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "itemconfigure", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "itempdf", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "lastchild", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "lower", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "move", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "nextsibling", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "parent", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "prevsibling", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "raise", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "scale", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "scan", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "select", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "style", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "type", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "types", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "xview", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "yview", PathMethod, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "debugtree", PathMethod, NULL, NULL},
    {-1, NULL, NULL, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "_tko_configure", PathMethod_tko_configure,
            NULL, NULL},
    {-1, NULL, NULL, NULL, NULL}
};

/*
 * canvasClass --
 *
 * The structure below defines canvas class behavior by means of functions
 * that can be invoked from generic window code.
 */
static Tk_ClassProcs canvasClass = {
    sizeof(Tk_ClassProcs),      /* size */
    PathCanvasWorldChanged,     /* worldChangedProc */
    NULL,      /* createProc */
    NULL       /* modalProc */
};

/*
 * Tko_PathInit --
 *
 *        Initializer for the tko path widget package.
 *
 * Results:
 *        A standard Tcl result.
 *
 * Side Effects:
 *       Tcl commands created
 */
int
Tko_PathInit(
    Tcl_Interp * interp /* Tcl interpreter. */)
{             
    Tcl_Class clazz;
    Tcl_Object object;

    /* Create class like tk command and remove oo functions from widget commands */
static const char *initScript =
    "::oo::class create ::path {superclass ::tko::widget; variable tko; {*}$::tko::unknown}";

#if defined(_WIN32) && defined(AGG_CUSTOM_ALLOCATOR)
    agg_custom_alloc = (void *(*)(unsigned int))Tcl_Alloc;
    agg_custom_free = (void (*)(void *))Tcl_Free;
#endif

    if(TkPathSetup(interp) == TCL_ERROR) {
        return TCL_ERROR;
    }

    /*
     * Create widget class
     */
    if(Tcl_Eval(interp, initScript) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Get class object
     */
    if((object = Tcl_GetObjectFromObj(interp, TkoObj.path)) == NULL
        || (clazz = Tcl_GetObjectAsClass(object)) == NULL) {
        return TCL_ERROR;
    }
    /*
     * Add methods and options
     */
    if(TkoWidgetClassDefine(interp, clazz, Tcl_GetObjectName(interp, object),
            pathMethods, pathOptionDefine) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * Link the variable to control antialiasing.
     */
    if(Tcl_LinkVar(interp, TK_PATHVAR_ANTIALIAS,
            (char *)&Tk_PathAntiAlias, TCL_LINK_BOOLEAN) != TCL_OK) {
        Tcl_ResetResult(interp);
    }

    /*
     * With Tk_PathSurfaceCopyPremultiplyAlpha true we ignore the "premultiply alpha"
     * and use RGB as is. Else we need to divide each RGB with alpha
     * to get "true" values.
     */
    if(Tcl_LinkVar(interp, TK_PATHVAR_PREMULTIPLYALPHA,
            (char *)&Tk_PathSurfaceCopyPremultiplyAlpha,
            TCL_LINK_BOOLEAN) != TCL_OK) {
        Tcl_ResetResult(interp);
    }
    if(Tcl_LinkVar(interp, TK_PATHVAR_DEPIXELIZE,
            (char *)&Tk_PathDepixelize, TCL_LINK_BOOLEAN) != TCL_OK) {
        Tcl_ResetResult(interp);
    }

    /*
     * Create additional Tcl commands
     */
    Tcl_CreateObjCommand(interp, TK_PATHCMD_PIXELALIGN,
        TkPathPixelAlignObjCmd, (ClientData) NULL, (Tcl_CmdDeleteProc *) NULL);

    /*
     * tkpath specific item types. Hopefully ordered by usage.
     */
    typeList = &tkPathTypeGroup;
    tkPathTypeGroup.nextPtr = &tkPathTypePath;
    tkPathTypePath.nextPtr = &tkPathTypeText;
    tkPathTypeText.nextPtr = &tkPathTypeLine;
    tkPathTypeLine.nextPtr = &tkPathTypePolyline;
    tkPathTypePolyline.nextPtr = &tkPathTypePolygon;
    tkPathTypePolygon.nextPtr = &tkPathTypeRect;
    tkPathTypeRect.nextPtr = &tkPathTypeCircle;
    tkPathTypeCircle.nextPtr = &tkPathTypeEllipse;
    tkPathTypeEllipse.nextPtr = &tkPathTypeImage;
    tkPathTypeImage.nextPtr = &tkPathTypeWindow;
    tkPathTypeWindow.nextPtr = NULL;

    /*
     * Make separate gradient objects, similar to SVG.
     */
    TkPathGradientInit(interp);
    TkPathSurfaceInit(interp);

    /*
     * Style object.
     */
    TkPathStyleInit(interp);

    return TCL_OK;
}

/*
 * PathConstructor --
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
PathConstructor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Object object;
    TkPathCanvas *path;
    Tk_PathItem *rootItemPtr;
    Tcl_Obj *rootObj;
    int skip;
    Tcl_Obj *myObjv[5];

    /* Get current object. Should not fail? */
    if((object = Tcl_ObjectContextObject(context)) == NULL) {
        return TCL_ERROR;
    }
    skip = Tcl_ObjectContextSkippedArgs(context);
    /* Check objv[]: 0=class 1="create" 2=.path 3=opts 4=args */
    if(skip != 3 || objc != 5 || strcmp("create", Tcl_GetString(objv[1])) != 0) {
        Tcl_WrongNumArgs(interp, 1, objv, "pathname ?options?");
        return TCL_ERROR;
    }
    /* Get own options */
    myObjv[3] =
        Tcl_ObjGetVar2(interp, TkoObj.tko_options, TkoObj.path,
        TCL_GLOBAL_ONLY);
    if(myObjv[3] == NULL) {
        return TCL_ERROR;
    }

    /*
     * Initialize fields that won't be initialized by ConfigureCanvas, or
     * which ConfigureCanvas expects to have reasonable values (e.g. resource
     * pointers).
     */

    path = (TkPathCanvas *) ckalloc(sizeof(TkPathCanvas));
    path->win = NULL;
    path->display = None;
    path->interp = interp;
    path->rootItemPtr = NULL;   /* root item created below. */
    path->borderWidth = 0;
    path->bgBorder = NULL;
    path->relief = TK_RELIEF_FLAT;
    path->highlightWidth = 0;
    path->highlightBgColorPtr = NULL;
    path->highlightColorPtr = NULL;
    path->inset = 0;
    path->pixmapGC = None;
    path->width = 0;
    path->height = 0;
    path->confine = 0;
    path->textInfo.selBorder = NULL;
    path->textInfo.selBorderWidth = 0;
    path->textInfo.selFgColorPtr = NULL;
    path->textInfo.selItemPtr = NULL;
    path->textInfo.selectFirst = -1;
    path->textInfo.selectLast = -1;
    path->textInfo.anchorItemPtr = NULL;
    path->textInfo.selectAnchor = 0;
    path->textInfo.insertBorder = NULL;
    path->textInfo.insertWidth = 0;
    path->textInfo.insertBorderWidth = 0;
    path->textInfo.focusItemPtr = NULL;
    path->textInfo.gotFocus = 0;
    path->textInfo.cursorOn = 0;
    path->insertOnTime = 0;
    path->insertOffTime = 0;
    path->insertBlinkHandler = (Tcl_TimerToken) NULL;
    path->xOrigin = path->yOrigin = 0;
    path->drawableXOrigin = path->drawableYOrigin = 0;
    path->bindingTable = NULL;
    path->currentItemPtr = NULL;
    path->newCurrentPtr = NULL;
    path->closeEnough = 0.0;
    path->pickEvent.type = LeaveNotify;
    path->pickEvent.xcrossing.x = 0;
    path->pickEvent.xcrossing.y = 0;
    path->state = 0;
    path->xScrollCmd = NULL;
    path->yScrollCmd = NULL;
    path->scroll[0] = 0;
    path->scroll[1] = 0;
    path->scroll[2] = 0;
    path->scroll[3] = 0;
    path->xScrollIncrement = 0;
    path->yScrollIncrement = 0;
    path->scanX = 0;
    path->scanXOrigin = 0;
    path->scanY = 0;
    path->scanYOrigin = 0;
    path->hotPtr = NULL;
    path->hotPrevPtr = NULL;
    path->cursor = None;
    path->pixelsPerMM = 76.;    /*TODO default */
    path->nextId = 1;   /* id = 0 reserved for root item */
    Tcl_InitHashTable(&path->idTable, TCL_ONE_WORD_KEYS);
    Tcl_InitHashTable(&path->styleTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&path->gradientTable, TCL_STRING_KEYS);
    path->styleUid = 0;
    path->gradientUid = 0;
    path->tagStyle = 0;
    /*TODO*/ path->flags = 0;
    path->canvas_state = TK_PATHSTATE_NORMAL;
    path->context = NULL;
    path->tsoffsetPtr = NULL;
    path->bindTagExprs = NULL;

    Tcl_ObjectSetMetadata(object, &pathMeta, (ClientData) path);

    /* call next constructor */
    myObjv[0] = objv[0];
    myObjv[1] = objv[1];
    myObjv[2] = objv[2];
    myObjv[3] = Tcl_DuplicateObj(myObjv[3]);
    Tcl_IncrRefCount(myObjv[3]);
    Tcl_ListObjAppendList(interp, myObjv[3], objv[objc - 2]);
    myObjv[4] = objv[4];
    if(Tcl_ObjectContextInvokeNext(interp, context, objc, myObjv,
            skip) != TCL_OK) {
        Tcl_DecrRefCount(myObjv[3]);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(myObjv[3]);
    path->win = TkoWidgetWindow(object);
    if(path->win == NULL || *(path->win) == NULL) {
        return TCL_ERROR;
    }
    if((path->display = Tk_Display(*(path->win))) == None) {
        return TCL_ERROR;
    }
#ifdef PLATFORM_SDL
    {
    double dW, dH;

        dW = WidthOfScreen(Tk_Screen(*(path->win)));
        dW /= WidthMMOfScreen(Tk_Screen(*(path->win)));
        dH = HeightOfScreen(Tk_Screen(*(path->win)));
        dH /= HeightMMOfScreen(Tk_Screen(*(path->win)));
        path->pixelsPerMM = (dH > dW) ? dH : dW;
    }
#else
    path->pixelsPerMM = WidthOfScreen(Tk_Screen(*(path->win)));
    path->pixelsPerMM /= WidthMMOfScreen(Tk_Screen(*(path->win)));
#endif

    Tk_SetClassProcs(*(path->win), &canvasClass, (ClientData) path);
    Tk_CreateEventHandler(*(path->win),
        ExposureMask | StructureNotifyMask | FocusChangeMask,
        CanvasEventProc, (ClientData) path);
    Tk_CreateEventHandler(*(path->win), KeyPressMask | KeyReleaseMask
        | ButtonPressMask | ButtonReleaseMask | EnterWindowMask
        | LeaveWindowMask | PointerMotionMask | VirtualEventMask,
        CanvasBindProc, (ClientData) path);
    Tk_CreateSelHandler(*(path->win), XA_PRIMARY, XA_STRING,
        CanvasFetchSelection, (ClientData) path, XA_STRING);

    /*
     * Create the root item as a group item.
     * Need to set the tag "root" by hand since its configProc
     * forbids this for the root item.
     */
    ItemCreate(interp, path, &tkPathTypeGroup, 1, &rootItemPtr, 0, NULL);
    rootObj = Tcl_NewStringObj("root", -1);
    Tcl_IncrRefCount(rootObj);
    rootItemPtr->pathTagsPtr = TkPathAllocTagsFromObj(NULL, rootObj);
    Tcl_DecrRefCount(rootObj);
    path->rootItemPtr = rootItemPtr;

    //TODO configure

    /* No need to set return value. It will be ignored by "oo::class create" */
    return TCL_OK;
}

/*
 * PathDestructor --
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
PathDestructor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Object object;
    TkPathCanvas *path;
    int skip;
    Tk_Window tkWin = NULL;
    Tk_PathItem *itemPtr, *prevItemPtr, *lastPtr = NULL;
    TkPathTagSearchExpr *expr, *next;

    /* Get current object. Should not fail? */
    if((object = Tcl_ObjectContextObject(context)) == NULL)
        return TCL_ERROR;
    skip = Tcl_ObjectContextSkippedArgs(context);
    if((path =
            (TkPathCanvas *) Tcl_ObjectGetMetadata(object,
                &pathMeta)) != NULL) {
        Tcl_Preserve(path);
        if(path->win) {
            tkWin = *(path->win);
            path->win = NULL;
        }
        /* Delete event handlers */
        if(tkWin) {
            Tk_DeleteEventHandler(tkWin,
                ExposureMask | StructureNotifyMask | FocusChangeMask,
                CanvasEventProc, (ClientData) path);
            Tk_DeleteEventHandler(tkWin, KeyPressMask | KeyReleaseMask
                | ButtonPressMask | ButtonReleaseMask | EnterWindowMask
                | LeaveWindowMask | PointerMotionMask | VirtualEventMask,
                CanvasBindProc, (ClientData) path);
            Tk_DeleteSelHandler(tkWin, XA_PRIMARY, XA_STRING);
        }
        if(path->insertBlinkHandler != NULL) {
            Tcl_DeleteTimerHandler(path->insertBlinkHandler);
            path->insertBlinkHandler = NULL;
        }
        /* Cancel idle calls */
        Tcl_CancelIdleCall(PathDisplay, (ClientData) path);

        /*
         * Free up all of the items in the canvas.
         * NB: We need to traverse the tree from the last item
         *     until reached the root item.
         */
        for(itemPtr = path->rootItemPtr; itemPtr != NULL;
            itemPtr = TkPathCanvasItemIteratorNext(itemPtr)) {
            lastPtr = itemPtr;
        }
        for(itemPtr = lastPtr; itemPtr != NULL;) {
            prevItemPtr = TkPathCanvasItemIteratorPrev(itemPtr);
            if(path->display != None) {
                (*itemPtr->typePtr->deleteProc) ((Tk_PathCanvas) path, itemPtr,
                    path->display);
            }
            ckfree((char *)itemPtr);
            itemPtr = prevItemPtr;
        }
        path->rootItemPtr = NULL;
        /*
         * Free up all the stuff that requires special handling, then let
         * PathMetaDestroy() handle all the standard option-related stuff.
         */
        if(tkWin) {
            TkPathStylesFree(tkWin, &path->styleTable);
        }
        TkPathCanvasGradientsFree(path);

        expr = path->bindTagExprs;
        while(expr) {
            next = expr->next;
            TagSearchExprDestroy(expr);
            expr = next;
        }
        path->bindTagExprs = NULL;
        if(path->bindingTable != NULL) {
            Tk_DeleteBindingTable(path->bindingTable);
            path->bindingTable = NULL;
        }

        Tcl_Release(path);
        if(tkWin) {
            Tcl_ObjectSetMetadata(object, &pathMeta, NULL);
        }
    }
    /* ignore errors */
    Tcl_ObjectContextInvokeNext(interp, context, objc, objv, skip);

    return TCL_OK;
}

/*
* PathMetaDestroy --
*
*    This function is invoked by Tcl_EventuallyFree or Tcl_Release to clean
*    up the internal structure of a canvas at a safe time (when no-one is
*    using it anymore).
*
* Results:
*    None.
*
* Side effects:
*    Everything associated with the canvas is freed up.
*/
static void
PathMetaDestroy(
    TkPathCanvas * path)
{
    if(path->bgBorder != NULL) {
        Tk_Free3DBorder(path->bgBorder);
    }
    if(path->highlightBgColorPtr != NULL) {
        Tk_FreeColor(path->highlightBgColorPtr);
    }
    if(path->highlightColorPtr != NULL) {
        Tk_FreeColor(path->highlightColorPtr);
    }
    if(path->pixmapGC != None && path->display != None) {
        Tk_FreeGC(path->display, path->pixmapGC);
    }
    if(path->textInfo.insertBorder != NULL) {
        Tk_Free3DBorder(path->textInfo.insertBorder);
    }
    if(path->textInfo.selBorder != NULL) {
        Tk_Free3DBorder(path->textInfo.selBorder);
    }
    if(path->textInfo.selFgColorPtr != NULL) {
        Tk_FreeColor(path->textInfo.selFgColorPtr);
    }
    if(path->display != None) {
        if(path->cursor != None) {
            Tk_FreeCursor(path->display, path->cursor);
        }
    }
    if(path->xScrollCmd != NULL) {
        ckfree(path->xScrollCmd);
    }
    if(path->yScrollCmd != NULL) {
        ckfree(path->yScrollCmd);
    }
    Tcl_DeleteHashTable(&path->idTable);
    Tcl_DeleteHashTable(&path->styleTable);
    Tcl_DeleteHashTable(&path->gradientTable);
    if(path->context != NULL) {
        TkPathFree(path->context);
    }
    if(path->tsoffsetPtr != NULL) {
        ckfree(path->tsoffsetPtr);
    }
    ckfree((char *)path);
}

/*
 * PathMethod_tko_configure --
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
PathMethod_tko_configure(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    XGCValues gcValues;
    GC  newGC;
    int flags;
    Tcl_Object object;
    TkPathCanvas *path;
    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (path =
            (TkPathCanvas *) Tcl_ObjectGetMetadata(object, &pathMeta)) == NULL
        || path->win == NULL || *(path->win) == NULL)
        return TCL_ERROR;

    /*
     * A few options need special processing, such as setting the background
     * from a 3-D border and creating a GC for copying bits to the screen.
     */
    Tk_SetBackgroundFromBorder(*(path->win), path->bgBorder);

    if(path->highlightWidth < 0) {
        path->highlightWidth = 0;
    }
    path->inset = path->borderWidth + path->highlightWidth;

    gcValues.function = GXcopy;
    gcValues.graphics_exposures = False;
    gcValues.foreground = Tk_3DBorderColor(path->bgBorder)->pixel;
    newGC = Tk_GetGC(*(path->win),
        GCFunction | GCGraphicsExposures | GCForeground, &gcValues);
    if(path->pixmapGC != None) {
        Tk_FreeGC(path->display, path->pixmapGC);
    }
    path->pixmapGC = newGC;

    /*
     * Reset the desired dimensions for the window.
     */
    Tk_GeometryRequest(*(path->win), path->width + 2 * path->inset,
        path->height + 2 * path->inset);

    /*
     * Restart the cursor timing sequence in case the on-time or off-time just
     * changed.
     */
    if(path->textInfo.gotFocus) {
        CanvasFocusProc(path, 1);
    }

    /* @@@ TODO: I don't see anywhere this is used. Nothing in man page. */
    if(path->tsoffsetPtr != NULL) {
        flags = path->tsoffsetPtr->flags;
        if(flags & TK_OFFSET_LEFT) {
            path->tsoffsetPtr->xoffset = 0;
        } else if(flags & TK_OFFSET_CENTER) {
            path->tsoffsetPtr->xoffset = path->width / 2;
        } else if(flags & TK_OFFSET_RIGHT) {
            path->tsoffsetPtr->xoffset = path->width;
        }
        if(flags & TK_OFFSET_TOP) {
            path->tsoffsetPtr->yoffset = 0;
        } else if(flags & TK_OFFSET_MIDDLE) {
            path->tsoffsetPtr->yoffset = path->height / 2;
        } else if(flags & TK_OFFSET_BOTTOM) {
            path->tsoffsetPtr->yoffset = path->height;
        }
    }
    /*
     * Reset the canvas's origin (this is a no-op unless confine mode has just
     * been turned on or the scroll region has changed).
     */

    CanvasSetOrigin(path, path->xOrigin, path->yOrigin);
    path->flags |= UPDATE_SCROLLBARS | REDRAW_BORDERS;
    Tk_PathCanvasEventuallyRedraw((Tk_PathCanvas) path,
        path->xOrigin, path->yOrigin,
        path->xOrigin + Tk_Width(*(path->win)),
        path->yOrigin + Tk_Height(*(path->win)));
    return TCL_OK;
}

/*
 * PathMethod_offset --
 *
 *  Process -offset option.
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
PathMethod_offset(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tk_TSOffset *offset;
    Tcl_Object object;
    TkPathCanvas *path;
    Tcl_Obj *value;
    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (path =
            (TkPathCanvas *) Tcl_ObjectGetMetadata(object, &pathMeta)) == NULL
        || (value =
            TkoWidgetOptionGet(interp, object, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }
    if(path->win == NULL || *(path->win) == NULL)
        return TCL_ERROR;
    offset =
        TkPathOffsetNew(interp,
        (ClientData) (TK_OFFSET_RELATIVE | TK_OFFSET_INDEX), *(path->win),
        value);
    if(offset == NULL) {
        return TCL_ERROR;
    }
    if(path->tsoffsetPtr != NULL) {
        ckfree(path->tsoffsetPtr);
    }
    path->tsoffsetPtr = offset;
    return TCL_OK;
}

/*
 * PathMethod_state --
 *
 *  Process -state option.
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
PathMethod_state(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    int state;
    Tcl_Object object;
    TkPathCanvas *path;
    Tcl_Obj *value;
    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (path =
            (TkPathCanvas *) Tcl_ObjectGetMetadata(object, &pathMeta)) == NULL
        || (value =
            TkoWidgetOptionGet(interp, object, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }
    if(Tcl_GetIndexFromObj(interp, value,
            stateStrings, "state", TCL_EXACT, &state) != TCL_OK) {
        return TCL_ERROR;
    }
    path->canvas_state = state;
    return TCL_OK;
}

/*
 * PathMethod_tagstyle --
 *
 *  Process -tagstyle option.
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
PathMethod_tagstyle(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    int tagstyle;
    Tcl_Object object;
    TkPathCanvas *path;
    Tcl_Obj *value;
    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (path =
            (TkPathCanvas *) Tcl_ObjectGetMetadata(object, &pathMeta)) == NULL
        || (value =
            TkoWidgetOptionGet(interp, object, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }
    if(Tcl_GetIndexFromObj(interp, value,
            tagStyleStrings, "tagstyle", TCL_EXACT, &tagstyle) != TCL_OK) {
        return TCL_ERROR;
    }
    path->tagStyle = tagstyle;
    return TCL_OK;
}

/*
 * PathMethod --
 *
 *    This function is invoked to process the Tcl command that corresponds
 *    to a widget managed by this module. See the user documentation for
 *    details on what it does.
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *    See the user documentation.
 */

static int
PathMethod(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    int c, result;
    Tcl_Obj *resultObjPtr;
    Tk_PathItem *itemPtr = NULL;        /* Initialization needed only to prevent
                                         * compiler warning. */
    TagSearch *searchPtr = NULL;        /* Allocated by first TagSearchScan, freed by
                                         * TagSearchDestroy */
    int index;
    static const char *optionStrings[] = {
        "addtag", "ancestors", "bbox", "bind",
        "canvasx", "canvasy", "children",
        "cmove", "coords", "create",
        "cscale", "dchars",
        "delete", "depth", "distance", "dtag",
        "find", "firstchild", "focus", "gettags",
        "gradient", "icursor", "index", "insert",
        "itemcget", "itemconfigure", "itempdf", "lastchild",
        "lower", "move", "nextsibling", "parent",
        "prevsibling", "raise", "scale",
        "scan", "select", "style", "type",
        "types", "xview", "yview",
#if 1
        "debugtree",
#endif
        NULL
    };
    enum options {
        CANV_ADDTAG, CANV_ANCESTORS, CANV_BBOX, CANV_BIND,
        CANV_CANVASX, CANV_CANVASY, CANV_CHILDREN,
        CANV_CMOVE, CANV_COORDS, CANV_CREATE,
        CANV_CSCALE, CANV_DCHARS,
        CANV_DELETE, CANV_DEPTH, CANV_DISTANCE, CANV_DTAG,
        CANV_FIND, CANV_FIRSTCHILD, CANV_FOCUS, CANV_GETTAGS,
        CANV_GRADIENT, CANV_ICURSOR, CANV_INDEX, CANV_INSERT,
        CANV_ITEMCGET, CANV_ITEMCONFIGURE, CANV_ITEMPDF, CANV_LASTCHILD,
        CANV_LOWER, CANV_MOVE, CANV_NEXTSIBLING, CANV_PARENT,
        CANV_PREVSIBLING, CANV_RAISE, CANV_SCALE,
        CANV_SCAN, CANV_SELECT, CANV_STYLE, CANV_TYPE,
        CANV_TYPES, CANV_XVIEW, CANV_YVIEW,
#if 1
        CANV_DEBUGTREE,
#endif
    };
    Tcl_Object object;
    TkPathCanvas *path;

    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (path =
            (TkPathCanvas *) Tcl_ObjectGetMetadata(object, &pathMeta)) == NULL
        || path->win == NULL || *(path->win) == NULL)
        return TCL_ERROR;

    if(objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?arg arg ...?");
        return TCL_ERROR;
    }
    if(Tcl_GetIndexFromObj(interp, objv[1], optionStrings, "option", 0,
            &index) != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_Preserve((ClientData) path);

    result = TCL_OK;
    switch ((enum options)index) {
    case CANV_ADDTAG:{
        if(objc < 4) {
            Tcl_WrongNumArgs(interp, 2, objv,
                "tag searchCommand ?arg arg ...?");
            result = TCL_ERROR;
            goto done;
        }
        result = FindItems(interp, path, objc, objv, objv[2], 3, &searchPtr);
        break;
    }
    case CANV_ANCESTORS:{

        if(objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId");
            result = TCL_ERROR;
            goto done;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done);
        if(itemPtr != NULL) {
    Tcl_Obj *listPtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
    Tcl_Obj *obj;
    Tk_PathItem *walkPtr;

            walkPtr = itemPtr->parentPtr;
            while(walkPtr != NULL) {

                /*
                 * Insert items higher in the tree first.
                 */
                obj = Tcl_NewIntObj(walkPtr->id);
                Tcl_ListObjReplace(NULL, listPtr, 0, 0, 1, &obj);
                walkPtr = walkPtr->parentPtr;
            }
            Tcl_SetObjResult(interp, listPtr);
        } else {
            Tcl_AppendResult(interp, "tag \"", Tcl_GetString(objv[2]),
                "\" doesn't match any items", NULL);
            result = TCL_ERROR;
            goto done;
        }
        break;
    }
    case CANV_BBOX:{
    int i, gotAny;
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0; /* Initializations needed only
                                         * to prevent overcautious
                                         * compiler warnings. */

        if(objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId ?tagOrId ...?");
            result = TCL_ERROR;
            goto done;
        }
        gotAny = 0;
        for(i = 2; i < objc; i++) {
            FOR_EVERY_CANVAS_ITEM_MATCHING(objv[i], &searchPtr, goto done) {

                /*
                 * Groups bbox are only updated lazily, when needed.
                 */
                if(itemPtr->firstChildPtr != NULL) {
                    TkPathCanvasGroupBbox((Tk_PathCanvas) path, itemPtr,
                        &itemPtr->x1, &itemPtr->y1, &itemPtr->x2, &itemPtr->y2);
                }
                if((itemPtr->x1 >= itemPtr->x2)
                    || (itemPtr->y1 >= itemPtr->y2)) {
                    continue;
                }
                if(!gotAny) {
                    x1 = itemPtr->x1;
                    y1 = itemPtr->y1;
                    x2 = itemPtr->x2;
                    y2 = itemPtr->y2;
                    gotAny = 1;
                } else {
                    if(itemPtr->x1 < x1) {
                        x1 = itemPtr->x1;
                    }
                    if(itemPtr->y1 < y1) {
                        y1 = itemPtr->y1;
                    }
                    if(itemPtr->x2 > x2) {
                        x2 = itemPtr->x2;
                    }
                    if(itemPtr->y2 > y2) {
                        y2 = itemPtr->y2;
                    }
                }
            }
        }
        if(gotAny) {
    Tcl_Obj *listObj = Tcl_NewListObj(0, NULL);

            Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(x1));
            Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(y1));
            Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(x2));
            Tcl_ListObjAppendElement(interp, listObj, Tcl_NewIntObj(y2));
            Tcl_SetObjResult(interp, listObj);
        }
        break;
    }
    case CANV_BIND:{
    ClientData object;

        if((objc < 3) || (objc > 5)) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId ?sequence? ?command?");
            result = TCL_ERROR;
            goto done;
        }

        /*
         * Figure out what object to use for the binding (individual item vs.
         * tag).
         */

        object = 0;
        result = TagSearchScan(path, objv[2], &searchPtr);
        if(result != TCL_OK) {
            goto done;
        }
        if(searchPtr->type == SEARCH_TYPE_ID) {
    Tcl_HashEntry *entryPtr;

            entryPtr = Tcl_FindHashEntry(&path->idTable,
                (char *)INT2PTR(searchPtr->id));
            if(entryPtr != NULL) {
                itemPtr = (Tk_PathItem *) Tcl_GetHashValue(entryPtr);
                object = (ClientData) itemPtr;
            }

            if(object == 0) {
                Tcl_AppendResult(interp, "item \"", Tcl_GetString(objv[2]),
                    "\" doesn't exist", NULL);
                result = TCL_ERROR;
                goto done;
            }
        } else {
            object = (ClientData) searchPtr->expr->uid;
        }

        /*
         * Make a binding table if the canvas doesn't already have one.
         */

        if(path->bindingTable == NULL) {
            path->bindingTable = Tk_CreateBindingTable(interp);
        }

        if(objc == 5) {
    int append = 0;
    unsigned long mask;
    char *argv4 = Tcl_GetString(objv[4]);

            if(argv4[0] == 0) {
                result = Tk_DeleteBinding(interp, path->bindingTable,
                    object, Tcl_GetString(objv[3]));
                goto done;
            }
            if(searchPtr->type == SEARCH_TYPE_EXPR) {
                /*
                 * If new tag expression, then insert in linked list.
                 */

    TkPathTagSearchExpr *expr, **lastPtr;

                lastPtr = &(path->bindTagExprs);
                while((expr = *lastPtr) != NULL) {
                    if(expr->uid == searchPtr->expr->uid) {
                        break;
                    }
                    lastPtr = &(expr->next);
                }
                if(!expr) {
                    /*
                     * Transfer ownership of expr to bindTagExprs list.
                     */

                    *lastPtr = searchPtr->expr;
                    searchPtr->expr->next = NULL;

                    /*
                     * Flag in TagSearch that expr has changed ownership so
                     * that TagSearchDestroy doesn't try to free it.
                     */

                    searchPtr->expr = NULL;
                }
            }
            if(argv4[0] == '+') {
                argv4++;
                append = 1;
            }
            mask = Tk_CreateBinding(interp, path->bindingTable,
                object, Tcl_GetString(objv[3]), argv4, append);
            if(mask == 0) {
                result = TCL_ERROR;
                goto done;
            }
            if(mask & (unsigned)~(ButtonMotionMask | Button1MotionMask
                    | Button2MotionMask | Button3MotionMask | Button4MotionMask
                    | Button5MotionMask | ButtonPressMask | ButtonReleaseMask
                    | EnterWindowMask | LeaveWindowMask | KeyPressMask
                    | KeyReleaseMask | PointerMotionMask | VirtualEventMask)) {
                Tk_DeleteBinding(interp, path->bindingTable,
                    object, Tcl_GetString(objv[3]));
                Tcl_ResetResult(interp);
                Tcl_AppendResult(interp, "requested illegal events; ",
                    "only key, button, motion, enter, leave, and virtual ",
                    "events may be used", NULL);
                result = TCL_ERROR;
                goto done;
            }
        } else if(objc == 4) {
    const char *command;

            command = Tk_GetBinding(interp, path->bindingTable,
                object, Tcl_GetString(objv[3]));
            if(command == NULL) {
    const char *string;

                string = Tcl_GetStringResult(interp);

                /*
                 * Ignore missing binding errors. This is a special hack that
                 * relies on the error message returned by FindSequence in
                 * tkBind.c.
                 */

                if(string[0] != '\0') {
                    result = TCL_ERROR;
                    goto done;
                } else {
                    Tcl_ResetResult(interp);
                }
            } else {
                Tcl_SetResult(interp, (char *)command, TCL_STATIC);
            }
        } else {
            Tk_GetAllBindings(interp, path->bindingTable, object);
        }
        break;
    }
    case CANV_CANVASX:{
    int x;
    double grid;
    char buf[TCL_DOUBLE_SPACE];

        if((objc < 3) || (objc > 4)) {
            Tcl_WrongNumArgs(interp, 2, objv, "screenx ?gridspacing?");
            result = TCL_ERROR;
            goto done;
        }
        if(Tk_GetPixelsFromObj(interp, *(path->win), objv[2], &x) != TCL_OK) {
            result = TCL_ERROR;
            goto done;
        }
        if(objc == 4) {
            if(Tk_PathCanvasGetCoordFromObj(interp, (Tk_PathCanvas) path,
                    objv[3], &grid) != TCL_OK) {
                result = TCL_ERROR;
                goto done;
            }
        } else {
            grid = 0.0;
        }
        x += path->xOrigin;
        Tcl_PrintDouble(interp, GridAlign((double)x, grid), buf);
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
        break;
    }
    case CANV_CANVASY:{
    int y;
    double grid;
    char buf[TCL_DOUBLE_SPACE];

        if((objc < 3) || (objc > 4)) {
            Tcl_WrongNumArgs(interp, 2, objv, "screeny ?gridspacing?");
            result = TCL_ERROR;
            goto done;
        }
        if(Tk_GetPixelsFromObj(interp, *(path->win), objv[2], &y) != TCL_OK) {
            result = TCL_ERROR;
            goto done;
        }
        if(objc == 4) {
            if(Tk_PathCanvasGetCoordFromObj(interp, (Tk_PathCanvas) path,
                    objv[3], &grid) != TCL_OK) {
                result = TCL_ERROR;
                goto done;
            }
        } else {
            grid = 0.0;
        }
        y += path->yOrigin;
        Tcl_PrintDouble(interp, GridAlign((double)y, grid), buf);
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
        break;
    }
    case CANV_CHILDREN:{
    Tcl_Obj *listObj;
    Tk_PathItem *childPtr;

        if(objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId");
            result = TCL_ERROR;
            goto done;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done);
        if(itemPtr != NULL) {
            listObj = Tcl_NewListObj(0, NULL);
            childPtr = itemPtr->firstChildPtr;
            while(childPtr != NULL) {
                Tcl_ListObjAppendElement(interp, listObj,
                    Tcl_NewIntObj(childPtr->id));
                childPtr = childPtr->nextPtr;
            }
            Tcl_SetObjResult(interp, listObj);
        } else {
            Tcl_AppendResult(interp, "tag \"", Tcl_GetString(objv[2]),
                "\" doesn't match any items", NULL);
            result = TCL_ERROR;
            goto done;
        }
        break;
    }
    case CANV_COORDS:{
        if(objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId ?x y x y ...?");
            result = TCL_ERROR;
            goto done;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done);
        if(itemPtr != NULL) {
            if(objc != 3) {
                EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
            }
            if(itemPtr->typePtr->coordProc != NULL) {
                result = (*itemPtr->typePtr->coordProc) (interp,
                    (Tk_PathCanvas) path, itemPtr, objc - 3, objv + 3);
            }
            if(objc != 3) {
                EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
            }
        }
        break;
    }
    case CANV_CREATE:{
    Tk_PathItemType *typePtr;
    Tk_PathItemType *matchPtr = NULL;
    Tk_PathItem *itemPtr;
    char *arg;
    int length;

        if(objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "type coords ?arg arg ...?");
            result = TCL_ERROR;
            goto done;
        }
        arg = Tcl_GetStringFromObj(objv[2], &length);
        c = arg[0];
        for(typePtr = typeList; typePtr != NULL; typePtr = typePtr->nextPtr) {
            if((c == typePtr->name[0])
                && (strncmp(arg, typePtr->name, (unsigned)length) == 0)) {
                if(matchPtr != NULL) {
                  badType:
                    Tcl_AppendResult(interp,
                        "unknown or ambiguous item type \"", arg, "\"", NULL);
                    result = TCL_ERROR;
                    goto done;
                }
                matchPtr = typePtr;
            }
        }
        if(matchPtr == NULL) {
            goto badType;
        }
        if((strncmp("group", matchPtr->name, (unsigned)length) != 0) &&
            (objc < 4)) {
            /*
             * Allow more specific error return. Groups have no coords.
             */
            Tcl_WrongNumArgs(interp, 3, objv, "coords ?arg arg ...?");
            result = TCL_ERROR;
            goto done;
        }
        typePtr = matchPtr;

        result =
            ItemCreate(interp, path, typePtr, 0, &itemPtr, objc - 3, objv + 3);
        if(result != TCL_OK) {
            result = TCL_ERROR;
            goto done;
        }
        path->hotPtr = itemPtr;
        path->hotPrevPtr = itemPtr->prevPtr;

        EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
        path->flags |= REPICK_NEEDED;
        Tcl_SetObjResult(interp, Tcl_NewIntObj(itemPtr->id));
        break;
    }
    case CANV_DCHARS:{
    int first, last;
    int x1, x2, y1, y2;

        if((objc != 4) && (objc != 5)) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId first ?last?");
            result = TCL_ERROR;
            goto done;
        }
        FOR_EVERY_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done) {
            if((itemPtr->typePtr->indexProc == NULL)
                || (itemPtr->typePtr->dCharsProc == NULL)) {
                continue;
            }
            result = itemPtr->typePtr->indexProc(interp,
                (Tk_PathCanvas) path, itemPtr, (char *)objv[3], &first);
            if(result != TCL_OK) {
                goto done;
            }
            if(objc == 5) {
                result = itemPtr->typePtr->indexProc(interp,
                    (Tk_PathCanvas) path, itemPtr, (char *)objv[4], &last);
                if(result != TCL_OK) {
                    goto done;
                }
            } else {
                last = first;
            }

            /*
             * Redraw both item's old and new areas: it's possible that a
             * delete could result in a new area larger than the old area.
             * Except if the insertProc sets the TK_ITEM_DONT_REDRAW flag,
             * nothing more needs to be done.
             */

            x1 = itemPtr->x1;
            y1 = itemPtr->y1;
            x2 = itemPtr->x2;
            y2 = itemPtr->y2;
            itemPtr->redraw_flags &= ~TK_ITEM_DONT_REDRAW;
            (*itemPtr->typePtr->dCharsProc) ((Tk_PathCanvas) path,
                itemPtr, first, last);
            if(!(itemPtr->redraw_flags & TK_ITEM_DONT_REDRAW)) {
                Tk_PathCanvasEventuallyRedraw((Tk_PathCanvas) path,
                    x1, y1, x2, y2);
                EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
            }
            itemPtr->redraw_flags &= ~TK_ITEM_DONT_REDRAW;
        }
        break;
    }
    case CANV_DEBUGTREE:{
    Tk_PathItem *walkPtr, *tmpPtr;
    char tmp[256], info[256];
    const char *s;
    int depth;

        if(objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, "");
            result = TCL_ERROR;
            goto done;
        }
        for(walkPtr = path->rootItemPtr; walkPtr != NULL;
            walkPtr = TkPathCanvasItemIteratorNext(walkPtr)) {
            depth = 0;
            tmpPtr = walkPtr;
            while(tmpPtr->parentPtr != NULL) {
                depth++;
                tmpPtr = tmpPtr->parentPtr;
            }
            if(walkPtr->firstChildPtr != NULL) {
                s = "----";
            } else {
                s = "";
            }
            info[0] = '\0';
            DebugGetItemInfo(walkPtr, info);
            sprintf(tmp, "%*d%s\t%s (itemPtr=%p)\n", 4 * depth + 3, walkPtr->id,
                s, info, walkPtr);
            Tcl_WriteChars(Tcl_GetChannel(interp, "stdout", NULL), tmp, -1);
        }
        break;
    }
    case CANV_DELETE:{
    int i;

        /*
         * Since deletinga group item implicitly deletes all its children
         * we may unintentionally try to delete an item more than once.
         * We therefore flatten (parent = root) all items first.
         */
        for(i = 2; i < objc; i++) {
            FOR_EVERY_CANVAS_ITEM_MATCHING(objv[i], &searchPtr, goto done) {

                /*
                 * Silently ignoring the root item.
                 */
                if(itemPtr->id == 0)
                    continue;

                /*
                 * This will also delete all its descendants by
                 * recursive calls.
                 */
                ItemDelete(path, itemPtr);
            }
        }
        break;
    }
    case CANV_DEPTH:{
        if(objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId");
            result = TCL_ERROR;
            goto done;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done);
        if(itemPtr != NULL) {
            Tcl_SetObjResult(interp,
                Tcl_NewIntObj(TkPathCanvasGetDepth(itemPtr)));
        } else {
            Tcl_AppendResult(interp, "tag \"", Tcl_GetString(objv[2]),
                "\" doesn't match any items", NULL);
            result = TCL_ERROR;
            goto done;
        }
        break;
    }
    case CANV_DISTANCE:{
    double point[2], dist;

        if(objc != 5) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId x y");
            result = TCL_ERROR;
            goto done;
        }
        if((Tcl_GetDoubleFromObj(interp, objv[3], &point[0]) != TCL_OK) ||
            (Tcl_GetDoubleFromObj(interp, objv[4], &point[1]) != TCL_OK)) {
            result = TCL_ERROR;
            goto done;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done);
        if(itemPtr != NULL) {
            dist =
                (*itemPtr->typePtr->pointProc) ((Tk_PathCanvas) path, itemPtr,
                point);
            Tcl_SetObjResult(interp, Tcl_NewDoubleObj(dist));
        } else {
            Tcl_AppendResult(interp, "tag \"", Tcl_GetString(objv[2]),
                "\" doesn't match any items", NULL);
            result = TCL_ERROR;
            goto done;
        }
        break;
    }
    case CANV_DTAG:{
    Tk_PathTags *ptagsPtr;
    Tk_Uid tag;
    int i;

        if((objc != 3) && (objc != 4)) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId ?tagToDelete?");
            result = TCL_ERROR;
            goto done;
        }
        if(objc == 4) {
            tag = Tk_GetUid(Tcl_GetString(objv[3]));
        } else {
            tag = Tk_GetUid(Tcl_GetString(objv[2]));
        }
        FOR_EVERY_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done) {
            ptagsPtr = itemPtr->pathTagsPtr;
            if(ptagsPtr != NULL) {
                for(i = ptagsPtr->numTags - 1; i >= 0; i--) {
                    if(ptagsPtr->tagPtr[i] == tag) {
                        ptagsPtr->tagPtr[i] =
                            ptagsPtr->tagPtr[ptagsPtr->numTags - 1];
                        ptagsPtr->numTags--;
                    }
                }
            }
        }
        break;
    }
    case CANV_FIND:{
        if(objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "searchCommand ?arg arg ...?");
            result = TCL_ERROR;
            goto done;
        }
        result = FindItems(interp, path, objc, objv, NULL, 2, &searchPtr);
        break;
    }
    case CANV_FIRSTCHILD:{
    Tk_PathItem *childPtr;

        if(objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId");
            result = TCL_ERROR;
            goto done;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done);
        if(itemPtr != NULL) {
            childPtr = itemPtr->firstChildPtr;
            if(childPtr != NULL) {
                Tcl_SetObjResult(interp, Tcl_NewIntObj(childPtr->id));
            }
        } else {
            Tcl_AppendResult(interp, "tag \"", Tcl_GetString(objv[2]),
                "\" doesn't match any items", NULL);
            result = TCL_ERROR;
            goto done;
        }
        break;
    }
    case CANV_FOCUS:{
        if(objc > 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "?tagOrId?");
            result = TCL_ERROR;
            goto done;
        }
        itemPtr = path->textInfo.focusItemPtr;
        if(objc == 2) {
            if(itemPtr != NULL) {
    char buf[TCL_INTEGER_SPACE];

                sprintf(buf, "%d", itemPtr->id);
                Tcl_SetResult(interp, buf, TCL_VOLATILE);
            }
            goto done;
        }
        if((itemPtr != NULL) && (path->textInfo.gotFocus)) {
            EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
        }
        if(Tcl_GetString(objv[2])[0] == 0) {
            path->textInfo.focusItemPtr = NULL;
            goto done;
        }
        FOR_EVERY_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done) {
            if(itemPtr->typePtr->icursorProc != NULL) {
                break;
            }
        }
        if(itemPtr == NULL) {
            goto done;
        }
        path->textInfo.focusItemPtr = itemPtr;
        if(path->textInfo.gotFocus) {
            EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
        }
        break;
    }
    case CANV_GETTAGS:{
        if(objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId");
            result = TCL_ERROR;
            goto done;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done);
        if(itemPtr != NULL) {
    int i;
    Tk_PathTags *ptagsPtr;

            ptagsPtr = itemPtr->pathTagsPtr;
            if(ptagsPtr != NULL) {
                for(i = 0; i < ptagsPtr->numTags; i++) {
                    Tcl_AppendElement(interp, (char *)ptagsPtr->tagPtr[i]);
                }
            }
        }
        break;
    }
    case CANV_GRADIENT:{
        result = TkPathCanvasGradientObjCmd(interp, path, objc, objv);
        break;
    }
    case CANV_ICURSOR:{
    int index;

        if(objc != 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId index");
            result = TCL_ERROR;
            goto done;
        }
        FOR_EVERY_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done) {
            if((itemPtr->typePtr->indexProc == NULL)
                || (itemPtr->typePtr->icursorProc == NULL)) {
                goto done;
            }
            result = itemPtr->typePtr->indexProc(interp,
                (Tk_PathCanvas) path, itemPtr, (char *)objv[3], &index);
            if(result != TCL_OK) {
                goto done;
            }
            (*itemPtr->typePtr->icursorProc) ((Tk_PathCanvas) path, itemPtr,
                index);
            if((itemPtr == path->textInfo.focusItemPtr)
                && (path->textInfo.cursorOn)) {
                EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
            }
        }
        break;
    }
    case CANV_INDEX:{
    int index;
    char buf[TCL_INTEGER_SPACE];

        if(objc != 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId string");
            result = TCL_ERROR;
            goto done;
        }
        FOR_EVERY_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done) {
            if(itemPtr->typePtr->indexProc != NULL) {
                break;
            }
        }
        if(itemPtr == NULL) {
            Tcl_AppendResult(interp, "can't find an indexable item \"",
                Tcl_GetString(objv[2]), "\"", NULL);
            result = TCL_ERROR;
            goto done;
        }
        result = itemPtr->typePtr->indexProc(interp, (Tk_PathCanvas) path,
            itemPtr, (char *)objv[3], &index);
        if(result != TCL_OK) {
            goto done;
        }
        sprintf(buf, "%d", index);
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
        break;
    }
    case CANV_INSERT:{
    int beforeThis;
    int x1, x2, y1, y2;

        if(objc != 5) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId beforeThis string");
            result = TCL_ERROR;
            goto done;
        }
        FOR_EVERY_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done) {
            if((itemPtr->typePtr->indexProc == NULL)
                || (itemPtr->typePtr->insertProc == NULL)) {
                continue;
            }
            result = itemPtr->typePtr->indexProc(interp,
                (Tk_PathCanvas) path, itemPtr, (char *)objv[3], &beforeThis);
            if(result != TCL_OK) {
                goto done;
            }

            /*
             * Redraw both item's old and new areas: it's possible that an
             * insertion could result in a new area either larger or smaller
             * than the old area. Except if the insertProc sets the
             * TK_ITEM_DONT_REDRAW flag, nothing more needs to be done.
             */

            x1 = itemPtr->x1;
            y1 = itemPtr->y1;
            x2 = itemPtr->x2;
            y2 = itemPtr->y2;
            itemPtr->redraw_flags &= ~TK_ITEM_DONT_REDRAW;
            (*itemPtr->typePtr->insertProc) ((Tk_PathCanvas) path,
                itemPtr, beforeThis, (char *)objv[4]);
            if(!(itemPtr->redraw_flags & TK_ITEM_DONT_REDRAW)) {
                Tk_PathCanvasEventuallyRedraw((Tk_PathCanvas) path,
                    x1, y1, x2, y2);
                EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
            }
            itemPtr->redraw_flags &= ~TK_ITEM_DONT_REDRAW;
        }
        break;
    }
    case CANV_ITEMCGET:{
        if(objc != 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId option");
            result = TCL_ERROR;
            goto done;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done);
        if(itemPtr != NULL) {
            resultObjPtr = Tk_GetOptionValue(path->interp, (char *)itemPtr,
                itemPtr->optionTable, objv[3], *(path->win));
            if(resultObjPtr == NULL) {
                result = TCL_ERROR;
                goto done;
            } else {
                Tcl_SetObjResult(interp, resultObjPtr);
            }
        }
        break;
    }
    case CANV_ITEMCONFIGURE:{
        if(objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId ?option value ...?");
            result = TCL_ERROR;
            goto done;
        }
        FOR_EVERY_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done) {
            if(objc <= 4) {
                resultObjPtr = Tk_GetOptionInfo(path->interp, (char *)itemPtr,
                    itemPtr->optionTable, (objc == 4) ? objv[3] : NULL,
                    *(path->win));
                if(resultObjPtr == NULL) {
                    result = TCL_ERROR;
                    goto done;
                } else {
                    Tcl_SetObjResult(interp, resultObjPtr);
                }
            } else {
                EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
                result = (*itemPtr->typePtr->configProc) (interp,
                    (Tk_PathCanvas) path, itemPtr, objc - 3, objv + 3,
                    TK_CONFIG_ARGV_ONLY);
                EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
                path->flags |= REPICK_NEEDED;
            }
            if((result != TCL_OK) || (objc < 5)) {
                break;
            }
        }
        break;
    }
    case CANV_ITEMPDF:{
        if(objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId ...");
            result = TCL_ERROR;
            goto done;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done);
        if(itemPtr != NULL) {
            if(itemPtr->typePtr->pdfProc != NULL) {
                result = (*itemPtr->typePtr->pdfProc) (interp,
                    (Tk_PathCanvas) path, itemPtr, objc - 3, objv + 3, 0);
            }
        }
        break;
    }
    case CANV_LASTCHILD:{
    Tk_PathItem *childPtr;

        if(objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId");
            result = TCL_ERROR;
            goto done;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done);
        if(itemPtr != NULL) {
            childPtr = itemPtr->lastChildPtr;
            if(childPtr != NULL) {
                Tcl_SetObjResult(interp, Tcl_NewIntObj(childPtr->id));
            }
        } else {
            Tcl_AppendResult(interp, "tag \"", Tcl_GetString(objv[2]),
                "\" doesn't match any items", NULL);
            result = TCL_ERROR;
            goto done;
        }
        break;
    }
    case CANV_LOWER:{
    Tk_PathItem *itemPtr;

        if((objc != 3) && (objc != 4)) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId ?belowThis?");
            result = TCL_ERROR;
            goto done;
        }

        /*
         * First find the item just after which we'll insert the named items.
         */

        if(objc == 3) {
            itemPtr = NULL;
        } else {
            FIRST_CANVAS_ITEM_MATCHING(objv[3], &searchPtr, goto done);
            if(itemPtr == NULL) {
                Tcl_AppendResult(interp, "tag \"", Tcl_GetString(objv[3]),
                    "\" doesn't match any items", NULL);
                result = TCL_ERROR;
                goto done;
            }
            itemPtr = itemPtr->prevPtr;
        }
        result = RelinkItems(path, objv[2], itemPtr, &searchPtr);
        break;
    }
    case CANV_CMOVE:
    case CANV_MOVE:{
    int compensate = ((enum options)index) == CANV_CMOVE;
    double xAmount, yAmount;

        if(objc != 5) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId xAmount yAmount");
            result = TCL_ERROR;
            goto done;
        }
        if((Tk_PathCanvasGetCoordFromObj(interp, (Tk_PathCanvas) path, objv[3],
                    &xAmount) != TCL_OK)
            || (Tk_PathCanvasGetCoordFromObj(interp, (Tk_PathCanvas) path,
                    objv[4], &yAmount) != TCL_OK)) {
            result = TCL_ERROR;
            goto done;
        }

        /* Round the deltas to the nearest integer to avoid round-off errors */
        xAmount = (double)((int)(xAmount + (xAmount > 0 ? 0.5 : -0.5)));
        yAmount = (double)((int)(yAmount + (yAmount > 0 ? 0.5 : -0.5)));

        FOR_EVERY_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done) {
            EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
            (void)(*itemPtr->typePtr->translateProc) ((Tk_PathCanvas) path,
                itemPtr, compensate, xAmount, yAmount);
            EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
            path->flags |= REPICK_NEEDED;
        }
        break;
    }
    case CANV_NEXTSIBLING:{
    Tk_PathItem *nextPtr;

        /* @@@ TODO: add optional argument like TreeCtrl has. */
        if(objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId");
            result = TCL_ERROR;
            goto done;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done);
        if(itemPtr != NULL) {
            nextPtr = itemPtr->nextPtr;
            if(nextPtr != NULL) {
                Tcl_SetObjResult(interp, Tcl_NewIntObj(nextPtr->id));
            }
        } else {
            Tcl_AppendResult(interp, "tag \"", Tcl_GetString(objv[2]),
                "\" doesn't match any items", NULL);
            result = TCL_ERROR;
            goto done;
        }
        break;
    }
    case CANV_PARENT:{
    int id;

        if(objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "id");
            result = TCL_ERROR;
            goto done;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done);
        if(itemPtr != NULL) {
            if(itemPtr->id == 0) {
                id = -1;        /* @@@ TODO: What else to return? */
            } else {
                id = itemPtr->parentPtr->id;
            }
            Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
        } else {
            Tcl_AppendResult(interp, "tag \"", Tcl_GetString(objv[2]),
                "\" doesn't match any items", NULL);
            result = TCL_ERROR;
            goto done;
        }
        break;
    }
    case CANV_PREVSIBLING:{
    Tk_PathItem *prevPtr;

        /* @@@ TODO: add optional argument like TreeCtrl has. */
        if(objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId");
            result = TCL_ERROR;
            goto done;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done);
        if(itemPtr != NULL) {
            prevPtr = itemPtr->prevPtr;
            if(prevPtr != NULL) {
                Tcl_SetObjResult(interp, Tcl_NewIntObj(prevPtr->id));
            }
        } else {
            Tcl_AppendResult(interp, "tag \"", Tcl_GetString(objv[2]),
                "\" doesn't match any items", NULL);
            result = TCL_ERROR;
            goto done;
        }
        break;
    }
    case CANV_RAISE:{
    Tk_PathItem *prevPtr;

        if((objc != 3) && (objc != 4)) {
            Tcl_WrongNumArgs(interp, 2, objv, "tagOrId ?aboveThis?");
            result = TCL_ERROR;
            goto done;
        }

        /*
         * First find the item just after which we'll insert the named items.
         */

        if(objc == 3) {
            prevPtr = path->rootItemPtr->lastChildPtr;
        } else {
            prevPtr = NULL;
            FOR_EVERY_CANVAS_ITEM_MATCHING(objv[3], &searchPtr, goto done) {
                prevPtr = itemPtr;
            }
            if(prevPtr == NULL) {
                Tcl_AppendResult(interp, "tagOrId \"", Tcl_GetString(objv[3]),
                    "\" doesn't match any items", NULL);
                result = TCL_ERROR;
                goto done;
            }
        }
        result = RelinkItems(path, objv[2], prevPtr, &searchPtr);
        break;
    }
    case CANV_CSCALE:
    case CANV_SCALE:{
    int compensate = ((enum options)index) == CANV_CSCALE;
    double xOrigin, yOrigin, xScale, yScale;

        if(objc != 7) {
            Tcl_WrongNumArgs(interp, 2, objv,
                "tagOrId xOrigin yOrigin xScale yScale");
            result = TCL_ERROR;
            goto done;
        }
        if((Tk_PathCanvasGetCoordFromObj(interp, (Tk_PathCanvas) path,
                    objv[3], &xOrigin) != TCL_OK)
            || (Tk_PathCanvasGetCoordFromObj(interp, (Tk_PathCanvas) path,
                    objv[4], &yOrigin) != TCL_OK)
            || (Tcl_GetDoubleFromObj(interp, objv[5], &xScale) != TCL_OK)
            || (Tcl_GetDoubleFromObj(interp, objv[6], &yScale) != TCL_OK)) {
            result = TCL_ERROR;
            goto done;
        }
        if((xScale == 0.0) || (yScale == 0.0)) {
            Tcl_SetResult(interp, (char *)"scale factor cannot be zero",
                TCL_STATIC);
            result = TCL_ERROR;
            goto done;
        }
        FOR_EVERY_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done) {
            EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
            (void)(*itemPtr->typePtr->scaleProc) ((Tk_PathCanvas) path,
                itemPtr, compensate, xOrigin, yOrigin, xScale, yScale);
            EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
            path->flags |= REPICK_NEEDED;
        }
        break;
    }
    case CANV_SCAN:{
    int x, y;
#ifdef ANDROID
    int gain = 2;
#else
    int gain = 10;
#endif
    static const char *optionStrings[] = {
        "mark", "dragto", NULL
    };

        if(objc < 5) {
            Tcl_WrongNumArgs(interp, 2, objv, "mark|dragto x y ?dragGain?");
            result = TCL_ERROR;
        } else if(Tcl_GetIndexFromObj(interp, objv[2], optionStrings,
                "scan option", 0, &index) != TCL_OK) {
            result = TCL_ERROR;
        } else if((objc != 5) && (objc != 5 + index)) {
            Tcl_WrongNumArgs(interp, 3, objv, index ? "x y ?gain?" : "x y");
            result = TCL_ERROR;
        } else if((Tcl_GetIntFromObj(interp, objv[3], &x) != TCL_OK)
            || (Tcl_GetIntFromObj(interp, objv[4], &y) != TCL_OK)) {
            result = TCL_ERROR;
        } else if((objc == 6) &&
            (Tcl_GetIntFromObj(interp, objv[5], &gain) != TCL_OK)) {
            result = TCL_ERROR;
        } else if(!index) {
            path->scanX = x;
            path->scanXOrigin = path->xOrigin;
            path->scanY = y;
            path->scanYOrigin = path->yOrigin;
        } else {
    int newXOrigin, newYOrigin, tmp;

            /*
             * Compute a new view origin for the canvas, amplifying the
             * mouse motion.
             */

            tmp = path->scanXOrigin - gain * (x - path->scanX)
                - path->scroll[0];
            newXOrigin = path->scroll[0] + tmp;
            tmp = path->scanYOrigin - gain * (y - path->scanY)
                - path->scroll[1];
            newYOrigin = path->scroll[1] + tmp;
            CanvasSetOrigin(path, newXOrigin, newYOrigin);
        }
        break;
    }
    case CANV_SELECT:{
    int index, optionindex;
    static const char *optionStrings[] = {
        "adjust", "clear", "from", "item", "to", NULL
    };
    enum options {
        CANV_ADJUST, CANV_CLEAR, CANV_FROM, CANV_ITEM, CANV_TO
    };

        if(objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "option ?tagOrId? ?arg?");
            result = TCL_ERROR;
            goto done;
        }
        if(objc >= 4) {
            FOR_EVERY_CANVAS_ITEM_MATCHING(objv[3], &searchPtr, goto done) {
                if((itemPtr->typePtr->indexProc != NULL)
                    && (itemPtr->typePtr->selectionProc != NULL)) {
                    break;
                }
            }
            if(itemPtr == NULL) {
                Tcl_AppendResult(interp,
                    "can't find an indexable and selectable item \"",
                    Tcl_GetString(objv[3]), "\"", NULL);
                result = TCL_ERROR;
                goto done;
            }
        }
        if(objc == 5) {
            result = itemPtr->typePtr->indexProc(interp,
                (Tk_PathCanvas) path, itemPtr, (char *)objv[4], &index);
            if(result != TCL_OK) {
                goto done;
            }
        }
        if(Tcl_GetIndexFromObj(interp, objv[2], optionStrings,
                "select option", 0, &optionindex) != TCL_OK) {
            result = TCL_ERROR;
            goto done;
        }
        switch ((enum options)optionindex) {
        case CANV_ADJUST:
            if(objc != 5) {
                Tcl_WrongNumArgs(interp, 3, objv, "tagOrId index");
                result = TCL_ERROR;
                goto done;
            }
            if(path->textInfo.selItemPtr == itemPtr) {
                if(index < (path->textInfo.selectFirst
                        + path->textInfo.selectLast) / 2) {
                    path->textInfo.selectAnchor = path->textInfo.selectLast + 1;
                } else {
                    path->textInfo.selectAnchor = path->textInfo.selectFirst;
                }
            }
            CanvasSelectTo(path, itemPtr, index);
            break;
        case CANV_CLEAR:
            if(objc != 3) {
                Tcl_AppendResult(interp, 3, objv, NULL);
                result = TCL_ERROR;
                goto done;
            }
            if(path->textInfo.selItemPtr != NULL) {
                EventuallyRedrawItem((Tk_PathCanvas) path,
                    path->textInfo.selItemPtr);
                path->textInfo.selItemPtr = NULL;
            }
            goto done;
            break;
        case CANV_FROM:
            if(objc != 5) {
                Tcl_WrongNumArgs(interp, 3, objv, "tagOrId index");
                result = TCL_ERROR;
                goto done;
            }
            path->textInfo.anchorItemPtr = itemPtr;
            path->textInfo.selectAnchor = index;
            break;
        case CANV_ITEM:
            if(objc != 3) {
                Tcl_WrongNumArgs(interp, 3, objv, NULL);
                result = TCL_ERROR;
                goto done;
            }
            if(path->textInfo.selItemPtr != NULL) {
                Tcl_SetObjResult(interp,
                    Tcl_NewIntObj(path->textInfo.selItemPtr->id));
            }
            break;
        case CANV_TO:
            if(objc != 5) {
                Tcl_WrongNumArgs(interp, 2, objv, "tagOrId index");
                result = TCL_ERROR;
                goto done;
            }
            CanvasSelectTo(path, itemPtr, index);
            break;
        }
        break;
    }
    case CANV_STYLE:{
        result = TkPathCanvasStyleObjCmd(interp, path, objc, objv);
        break;
    }
    case CANV_TYPE:{
        if(objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tag");
            result = TCL_ERROR;
            goto done;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[2], &searchPtr, goto done);
        if(itemPtr != NULL) {
            Tcl_SetResult(interp, (char *)itemPtr->typePtr->name, TCL_STATIC);
        }
        break;
    }
    case CANV_TYPES:{
    Tk_PathItemType *typePtr;
    Tcl_Obj *listObj;

        if(objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, "");
            result = TCL_ERROR;
            goto done;
        }
        listObj = Tcl_NewListObj(0, NULL);
        for(typePtr = typeList; typePtr != NULL; typePtr = typePtr->nextPtr) {
            Tcl_ListObjAppendElement(interp, listObj,
                Tcl_NewStringObj(typePtr->name, -1));
        }
        Tcl_SetObjResult(interp, listObj);
        break;
    }
    case CANV_XVIEW:{
    int count, type;
    int newX = 0;              /* Initialization needed only to prevent
                                * gcc warnings. */
    double fraction;

        if(objc == 2) {
            Tcl_SetObjResult(interp,
                ScrollFractions(path->xOrigin + path->inset,
                    path->xOrigin + Tk_Width(*(path->win))
                    - path->inset, path->scroll[0], path->scroll[2]));
        } else {
    const char **args = GetStringsFromObjs(objc, objv);
            type = Tk_GetScrollInfo(interp, objc, args, &fraction, &count);
            if(args != NULL) {
                ckfree((char *)args);
            }
            switch (type) {
            case TK_SCROLL_ERROR:
                result = TCL_ERROR;
                goto done;
            case TK_SCROLL_MOVETO:
                newX = path->scroll[0] - path->inset
                    + (int)(fraction * (path->scroll[2]
                        - path->scroll[0]) + 0.5);
                break;
            case TK_SCROLL_PAGES:
                newX = (int)(path->xOrigin + count * .9
                    * (Tk_Width(*(path->win)) - 2 * path->inset));
                break;
            case TK_SCROLL_UNITS:
                if(path->xScrollIncrement > 0) {
                    newX = path->xOrigin + count * path->xScrollIncrement;
                } else {
                    newX = (int)(path->xOrigin + count * .1
                        * (Tk_Width(*(path->win))
                            - 2 * path->inset));
                }
                break;
            }
            CanvasSetOrigin(path, newX, path->yOrigin);
        }
        break;
    }
    case CANV_YVIEW:{
    int count, type;
    int newY = 0;              /* Initialization needed only to prevent
                                * gcc warnings. */
    double fraction;

        if(objc == 2) {
            Tcl_SetObjResult(interp,
                ScrollFractions(path->yOrigin + path->inset,
                    path->yOrigin + Tk_Height(*(path->win))
                    - path->inset, path->scroll[1], path->scroll[3]));
        } else {
    const char **args = GetStringsFromObjs(objc, objv);
            type = Tk_GetScrollInfo(interp, objc, args, &fraction, &count);
            if(args != NULL) {
                ckfree((char *)args);
            }
            switch (type) {
            case TK_SCROLL_ERROR:
                result = TCL_ERROR;
                goto done;
            case TK_SCROLL_MOVETO:
                newY = path->scroll[1] - path->inset
                    + (int)(fraction * (path->scroll[3]
                        - path->scroll[1]) + 0.5);
                break;
            case TK_SCROLL_PAGES:
                newY = (int)(path->yOrigin + count * .9
                    * (Tk_Height(*(path->win))
                        - 2 * path->inset));
                break;
            case TK_SCROLL_UNITS:
                if(path->yScrollIncrement > 0) {
                    newY = path->yOrigin + count * path->yScrollIncrement;
                } else {
                    newY = (int)(path->yOrigin + count * .1
                        * (Tk_Height(*(path->win))
                            - 2 * path->inset));
                }
                break;
            }
            CanvasSetOrigin(path, path->xOrigin, newY);
        }
        break;
    }
    }

  done:
    TagSearchDestroy(searchPtr);
    Tcl_Release((ClientData) path);
    return result;
}

/*
 * PathCanvasWorldChanged --
 *
 *    This function is called when the world has changed in some way and the
 *    widget needs to recompute all its graphics contexts and determine its
 *    new geometry.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Configures all items in the canvas with a empty argc/argv, for the
 *    side effect of causing all the items to recompute their geometry and
 *    to be redisplayed.
 */
static void
PathCanvasWorldChanged(
    ClientData instanceData)
{              /* Information about widget. */
TkPathCanvas *path = (TkPathCanvas *) instanceData;
Tk_PathItem *itemPtr;
int result;
    if(path->win == NULL || *(path->win) == NULL)
        return;

    itemPtr = path->rootItemPtr;
    for(; itemPtr != NULL; itemPtr = TkPathCanvasItemIteratorNext(itemPtr)) {
        result = (*itemPtr->typePtr->configProc) (path->interp,
            (Tk_PathCanvas) path, itemPtr, 0, NULL, TK_CONFIG_ARGV_ONLY);
        if(result != TCL_OK) {
            Tcl_ResetResult(path->interp);
        }
    }
    path->flags |= REPICK_NEEDED;
    Tk_PathCanvasEventuallyRedraw((Tk_PathCanvas) path,
        path->xOrigin, path->yOrigin,
        path->xOrigin + Tk_Width(*(path->win)),
        path->yOrigin + Tk_Height(*(path->win)));
}

/*
 * PathDisplay --
 *
 *    This function redraws the contents of a canvas window. It is invoked
 *    as a do-when-idle handler, so it only runs when there's nothing else
 *    for the application to do.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Information appears on the screen.
 */
static void
PathDisplay(
    ClientData clientData /* Information about widget. */)
{             
    TkPathCanvas *path = (TkPathCanvas *) clientData;
    Tk_PathItem *itemPtr;
    Pixmap pixmap;
    int screenX1, screenX2, screenY1, screenY2, width, height;

    if(path->win == NULL || *(path->win) == NULL)
        return;
    if(!Tk_IsMapped(*(path->win))) {
        goto done;
    }

    /*
     * Choose a new current item if that is needed (this could cause event
     * handlers to be invoked).
     */

    Tcl_Preserve((ClientData) path);
    while(path->flags & REPICK_NEEDED) {
        path->flags &= ~REPICK_NEEDED;
        PickCurrentItem(path, &path->pickEvent);
        if(path->win == NULL || *(path->win) == NULL) {
            Tcl_Release((ClientData) path);
            return;
        }
    }
    Tcl_Release((ClientData) path);

    /*
     * Scan through the item list, registering the bounding box for all items
     * that didn't do that for the final coordinates yet. This can be
     * determined by the FORCE_REDRAW flag.
     */

    for(itemPtr = path->rootItemPtr; itemPtr != NULL;
        itemPtr = TkPathCanvasItemIteratorNext(itemPtr)) {
        if(itemPtr->redraw_flags & FORCE_REDRAW) {
            itemPtr->redraw_flags &= ~FORCE_REDRAW;
            EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
            itemPtr->redraw_flags &= ~FORCE_REDRAW;
        }
    }

    /*
     * Compute the intersection between the area that needs redrawing and the
     * area that's visible on the screen.
     */

    if((path->redrawX1 < path->redrawX2)
        && (path->redrawY1 < path->redrawY2)) {
        screenX1 = path->xOrigin + path->inset;
        screenY1 = path->yOrigin + path->inset;
        screenX2 = path->xOrigin + Tk_Width(*(path->win)) - path->inset;
        screenY2 = path->yOrigin + Tk_Height(*(path->win)) - path->inset;
        if(path->redrawX1 > screenX1) {
            screenX1 = path->redrawX1;
        }
        if(path->redrawY1 > screenY1) {
            screenY1 = path->redrawY1;
        }
        if(path->redrawX2 < screenX2) {
            screenX2 = path->redrawX2;
        }
        if(path->redrawY2 < screenY2) {
            screenY2 = path->redrawY2;
        }
        if((screenX1 >= screenX2) || (screenY1 >= screenY2)) {
            goto borders;
        }

        width = screenX2 - screenX1;
        height = screenY2 - screenY1;

#ifndef TK_PATH_NO_DOUBLE_BUFFERING
        /*
         * Redrawing is done in a temporary pixmap that is allocated here and
         * freed at the end of the function. All drawing is done to the
         * pixmap, and the pixmap is copied to the screen at the end of the
         * function. The temporary pixmap serves two purposes:
         *
         * 1. It provides a smoother visual effect (no clearing and gradual
         *    redraw will be visible to users).
         * 2. It allows us to redraw only the objects that overlap the redraw
         *    area. Otherwise incorrect results could occur from redrawing
         *    things that stick outside of the redraw area (we'd have to
         *    redraw everything in order to make the overlaps look right).
         *
         * Some tricky points about the pixmap:
         *
         * 1. We only allocate a large enough pixmap to hold the area that has
         *    to be redisplayed. This saves time in in the X server for large
         *    objects that cover much more than the area being redisplayed:
         *    only the area of the pixmap will actually have to be redrawn.
         * 2. Some X servers (e.g. the one for DECstations) have troubles with
         *    with characters that overlap an edge of the pixmap (on the DEC
         *    servers, as of 8/18/92, such characters are drawn one pixel too
         *    far to the right). To handle this problem, make the pixmap a bit
         *    larger than is absolutely needed so that for normal-sized fonts
         *    the characters that overlap the edge of the pixmap will be
         *    outside the area we care about.
         */

#ifdef PLATFORM_SDL
        path->drawableXOrigin = screenX1;
        path->drawableYOrigin = screenY1;
        pixmap =
            Tk_GetPixmap(Tk_Display(*(path->win)), Tk_WindowId(*(path->win)),
            width, height, (unsigned)-32);
#else
        path->drawableXOrigin = screenX1 - 30;
        path->drawableYOrigin = screenY1 - 30;
        pixmap =
            Tk_GetPixmap(Tk_Display(*(path->win)), Tk_WindowId(*(path->win)),
            (screenX2 + 30 - path->drawableXOrigin),
            (screenY2 + 30 - path->drawableYOrigin), Tk_Depth(*(path->win)));
#endif
#else
        path->drawableXOrigin = path->xOrigin;
        path->drawableYOrigin = path->yOrigin;
        pixmap = Tk_WindowId(*(path->win));
        TkpClipDrawableToRect(Tk_Display(*(path->win)), pixmap,
            screenX1 - path->xOrigin, screenY1 - path->yOrigin, width, height);
#endif /* TK_PATH_NO_DOUBLE_BUFFERING */

        /*
         * Clear the area to be redrawn.
         */

        XFillRectangle(Tk_Display(*(path->win)), pixmap, path->pixmapGC,
            screenX1 - path->drawableXOrigin,
            screenY1 - path->drawableYOrigin, (unsigned int)width,
            (unsigned int)height);

        /*
         * Scan through the item list, redrawing those items that need it. An
         * item must be redraw if either (a) it intersects the smaller
         * on-screen area or (b) it intersects the full canvas area and its
         * type requests that it be redrawn always (e.g. so subwindows can be
         * unmapped when they move off-screen).
         */

#if defined(_WIN32) && !defined(PLATFORM_SDL)
        path->context = NULL;
#else
        path->context = TkPathInit(*(path->win), pixmap);
#endif
        for(itemPtr = path->rootItemPtr; itemPtr != NULL;
            itemPtr = TkPathCanvasItemIteratorNext(itemPtr)) {
            if((itemPtr->x1 >= screenX2)
                || (itemPtr->y1 >= screenY2)
                || (itemPtr->x2 < screenX1)
                || (itemPtr->y2 < screenY1)) {
                if(!(itemPtr->typePtr->alwaysRedraw & 1)
                    || (itemPtr->x1 >= path->redrawX2)
                    || (itemPtr->y1 >= path->redrawY2)
                    || (itemPtr->x2 < path->redrawX1)
                    || (itemPtr->y2 < path->redrawY1)) {
                    continue;
                }
            }
            if(itemPtr->state == TK_PATHSTATE_HIDDEN ||
                (itemPtr->state == TK_PATHSTATE_NULL &&
                    path->canvas_state == TK_PATHSTATE_HIDDEN)) {
                continue;
            }
#if defined(_WIN32) && !defined(PLATFORM_SDL)
            if(itemPtr->typePtr->isPathType) {
                if(path->context == NULL) {
                    path->context = TkPathInit(*(path->win), pixmap);
                } else {
                    TkPathResetTMatrix(path->context);
                }
            } else if(path->context != NULL) {
                TkPathFree(path->context);
                path->context = NULL;
            }
#else
            if(itemPtr->typePtr->isPathType) {
                TkPathResetTMatrix(path->context);
            }
#endif
            (*itemPtr->typePtr->displayProc) ((Tk_PathCanvas) path, itemPtr,
                path->display, pixmap, screenX1, screenY1, width, height);
        }
        if(path->context != NULL) {
            TkPathFree(path->context);
            path->context = NULL;
        }
#ifndef TK_PATH_NO_DOUBLE_BUFFERING
        /*
         * Copy from the temporary pixmap to the screen, then free up the
         * temporary pixmap.
         */

        XCopyArea(Tk_Display(*(path->win)), pixmap, Tk_WindowId(*(path->win)),
            path->pixmapGC,
            screenX1 - path->drawableXOrigin,
            screenY1 - path->drawableYOrigin,
            (unsigned int)width, (unsigned int)height,
            screenX1 - path->xOrigin, screenY1 - path->yOrigin);
        Tk_FreePixmap(Tk_Display(*(path->win)), pixmap);
#else
        TkpClipDrawableToRect(Tk_Display(*(path->win)), pixmap, 0, 0, -1, -1);
#endif /* TK_PATH_NO_DOUBLE_BUFFERING */
    }

    /*
     * Draw the window borders, if needed.
     */

  borders:
    if(path->flags & REDRAW_BORDERS) {
        path->flags &= ~REDRAW_BORDERS;
        if(path->borderWidth > 0) {
            Tk_Draw3DRectangle(*(path->win), Tk_WindowId(*(path->win)),
                path->bgBorder, path->highlightWidth,
                path->highlightWidth,
                Tk_Width(*(path->win)) - 2 * path->highlightWidth,
                Tk_Height(*(path->win)) - 2 * path->highlightWidth,
                path->borderWidth, path->relief);
        }
        if(path->highlightWidth != 0) {
GC  fgGC, bgGC;

            bgGC = Tk_GCForColor(path->highlightBgColorPtr,
                Tk_WindowId(*(path->win)));
            if(path->textInfo.gotFocus) {
                fgGC = Tk_GCForColor(path->highlightColorPtr,
                    Tk_WindowId(*(path->win)));
                TkpDrawHighlightBorder(*(path->win), fgGC, bgGC,
                    path->highlightWidth, Tk_WindowId(*(path->win)));
            } else {
                TkpDrawHighlightBorder(*(path->win), bgGC, bgGC,
                    path->highlightWidth, Tk_WindowId(*(path->win)));
            }
        }
    }

  done:
    path->flags &= ~(REDRAW_PENDING | BBOX_NOT_EMPTY);
    path->redrawX1 = path->redrawX2 = 0;
    path->redrawY1 = path->redrawY2 = 0;
    if(path->flags & UPDATE_SCROLLBARS) {
        CanvasUpdateScrollbars(path);
    }
}

/*
 * CanvasEventProc --
 *
 *    This function is invoked by the Tk dispatcher for various events on
 *    canvases.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    When the window gets deleted, internal structures get cleaned up.
 *    When it gets exposed, it is redisplayed.
 */
static void
CanvasEventProc(
    ClientData clientData,     /* Information about window. */
    XEvent * eventPtr /* Information about event. */)
{             
    TkPathCanvas *path = (TkPathCanvas *) clientData;
    if(eventPtr->type == DestroyNotify || path->win == NULL
        || *(path->win) == NULL)
        return;

    if(eventPtr->type == Expose) {
int x, y;

        x = eventPtr->xexpose.x + path->xOrigin;
        y = eventPtr->xexpose.y + path->yOrigin;
        Tk_PathCanvasEventuallyRedraw((Tk_PathCanvas) path, x, y,
            x + eventPtr->xexpose.width, y + eventPtr->xexpose.height);
        if((eventPtr->xexpose.x < path->inset)
            || (eventPtr->xexpose.y < path->inset)
            || ((eventPtr->xexpose.x + eventPtr->xexpose.width)
                > (Tk_Width(*(path->win)) - path->inset))
            || ((eventPtr->xexpose.y + eventPtr->xexpose.height)
                > (Tk_Height(*(path->win)) - path->inset))) {
            path->flags |= REDRAW_BORDERS;
        }
    } else if(eventPtr->type == ConfigureNotify) {
        path->flags |= UPDATE_SCROLLBARS;

        /*
         * The call below is needed in order to recenter the canvas if it's
         * confined and its scroll region is smaller than the window.
         */

        CanvasSetOrigin(path, path->xOrigin, path->yOrigin);
        Tk_PathCanvasEventuallyRedraw((Tk_PathCanvas) path, path->xOrigin,
            path->yOrigin,
            path->xOrigin + Tk_Width(*(path->win)),
            path->yOrigin + Tk_Height(*(path->win)));
        path->flags |= REDRAW_BORDERS;
    } else if(eventPtr->type == FocusIn) {
        if(eventPtr->xfocus.detail != NotifyInferior) {
            CanvasFocusProc(path, 1);
        }
    } else if(eventPtr->type == FocusOut) {
        if(eventPtr->xfocus.detail != NotifyInferior) {
            CanvasFocusProc(path, 0);
        }
    } else if(eventPtr->type == UnmapNotify) {
Tk_PathItem *itemPtr;

        /*
         * Special hack: if the canvas is unmapped, then must notify all items
         * with "alwaysRedraw" set, so that they know that they are no longer
         * displayed.
         */

        for(itemPtr = path->rootItemPtr; itemPtr != NULL;
            itemPtr = TkPathCanvasItemIteratorNext(itemPtr)) {
            if(itemPtr->typePtr->alwaysRedraw & 1) {
                (*itemPtr->typePtr->displayProc) ((Tk_PathCanvas) path,
                    itemPtr, path->display, None, 0, 0, 0, 0);
            }
        }
    }
}

/*
 * Tk_PathCanvasEventuallyRedraw --
 *
 *    Arrange for part or all of a canvas widget to redrawn at some
 *    convenient time in the future.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The screen will eventually be refreshed.
 */
void
Tk_PathCanvasEventuallyRedraw(
    Tk_PathCanvas canvas,      /* Information about widget. */
    int x1,
    int y1,                    /* Upper left corner of area to redraw. Pixels
                                * on edge are redrawn. */
    int x2,
    int y2)
{              /* Lower right corner of area to redraw.
                * Pixels on edge are not redrawn. */
    TkPathCanvas *path = (TkPathCanvas *) canvas;
    if(path->win == NULL || *(path->win) == NULL || !Tk_IsMapped(*(path->win)))
        return;

    if((x1 >= x2) || (y1 >= y2) ||
        (x2 < path->xOrigin) || (y2 < path->yOrigin) ||
        (x1 >= path->xOrigin + Tk_Width(*(path->win))) ||
        (y1 >= path->yOrigin + Tk_Height(*(path->win)))) {
        return;
    }
    if(path->flags & BBOX_NOT_EMPTY) {
        if(x1 <= path->redrawX1) {
            path->redrawX1 = x1;
        }
        if(y1 <= path->redrawY1) {
            path->redrawY1 = y1;
        }
        if(x2 >= path->redrawX2) {
            path->redrawX2 = x2;
        }
        if(y2 >= path->redrawY2) {
            path->redrawY2 = y2;
        }
    } else {
        path->redrawX1 = x1;
        path->redrawY1 = y1;
        path->redrawX2 = x2;
        path->redrawY2 = y2;
        path->flags |= BBOX_NOT_EMPTY;
    }
    if(!(path->flags & REDRAW_PENDING)) {
        Tcl_DoWhenIdle(PathDisplay, (ClientData) path);
        path->flags |= REDRAW_PENDING;
    }
}

/*
 * EventuallyRedrawItem --
 *
 *    Arrange for part or all of a canvas widget to redrawn at some
 *    convenient time in the future.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The screen will eventually be refreshed.
 */
static void
EventuallyRedrawItem(
    Tk_PathCanvas canvas,      /* Information about widget. */
    Tk_PathItem * itemPtr)
{              /* Item to be redrawn. */
TkPathCanvas *path = (TkPathCanvas *) canvas;
    if(path->win == NULL || *(path->win) == NULL)
        return;

    if((itemPtr->x1 >= itemPtr->x2) || (itemPtr->y1 >= itemPtr->y2) ||
        (itemPtr->x2 < path->xOrigin) ||
        (itemPtr->y2 < path->yOrigin) ||
        (itemPtr->x1 >= path->xOrigin + Tk_Width(*(path->win))) ||
        (itemPtr->y1 >= path->yOrigin + Tk_Height(*(path->win)))) {
        if(!(itemPtr->typePtr->alwaysRedraw & 1)) {
            return;
        }
    }
    if(!(itemPtr->redraw_flags & FORCE_REDRAW)) {
        if(path->flags & BBOX_NOT_EMPTY) {
            if(itemPtr->x1 <= path->redrawX1) {
                path->redrawX1 = itemPtr->x1;
            }
            if(itemPtr->y1 <= path->redrawY1) {
                path->redrawY1 = itemPtr->y1;
            }
            if(itemPtr->x2 >= path->redrawX2) {
                path->redrawX2 = itemPtr->x2;
            }
            if(itemPtr->y2 >= path->redrawY2) {
                path->redrawY2 = itemPtr->y2;
            }
        } else {
            path->redrawX1 = itemPtr->x1;
            path->redrawY1 = itemPtr->y1;
            path->redrawX2 = itemPtr->x2;
            path->redrawY2 = itemPtr->y2;
            path->flags |= BBOX_NOT_EMPTY;
        }
        itemPtr->redraw_flags |= FORCE_REDRAW;
    }
    SetAncestorsDirtyBbox(itemPtr);
    if(!(path->flags & REDRAW_PENDING)) {
        Tcl_DoWhenIdle(PathDisplay, (ClientData) path);
        path->flags |= REDRAW_PENDING;
    }
}

/*
 * EventuallyRedrawItemAndChildren --
 *
 *    Arrange for part or all of a canvas widget to redrawn at some
 *    convenient time in the future. This version traverses over all
 *    child items, too.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The screen will eventually be refreshed.
 */
static void
EventuallyRedrawItemAndChildren(
    Tk_PathCanvas canvas,      /* Information about widget. */
    Tk_PathItem * itemPtr)
{              /* Item to be redrawn. */
Tk_PathItem *childPtr = itemPtr->firstChildPtr;

    while(childPtr != NULL) {
        EventuallyRedrawItemAndChildren(canvas, childPtr);
        childPtr = childPtr->nextPtr;
    }
    EventuallyRedrawItem(canvas, itemPtr);
}

/*
 * TkPathCanvasSetParent --
 *
 *    Appends an item as the last sibling to a parent item.
 *    May unlink any existing linkage.
 *
 * Results:
 *    Standard tcl result.
 *
 * Side effects:
 *    Links in item in display list.
 */
void
TkPathCanvasSetParent(
    Tk_PathItem * parentPtr,
    Tk_PathItem * itemPtr)
{

    /*
     * Unlink any present parent, then link in again.
     */
    if(itemPtr->parentPtr != NULL) {
        TkPathCanvasItemDetach(itemPtr);
    }
    ItemAddToParent(parentPtr, itemPtr);

    /*
     * We may have configured -parent with a tag but need to return an id.
     */
    itemPtr->parentObj = UnshareObj(itemPtr->parentObj);
    Tcl_SetIntObj(itemPtr->parentObj, parentPtr->id);
}

/*
* TkPathCanvasSetParentToRoot --
*
*
* Results:
*    Standard tcl result.
*
* Side effects:
*/
void
TkPathCanvasSetParentToRoot(
    Tk_PathItem * itemPtr)
{
Tk_PathItemEx *itemExPtr = (Tk_PathItemEx *) itemPtr;
Tk_PathCanvas canvas = itemExPtr->canvas;
TkPathCanvas *path = (TkPathCanvas *) canvas;
    TkPathCanvasSetParent(path->rootItemPtr, itemPtr);
}

/*
 * TkPathCanvasFindGroup --
 *
 *    Searches for the first group item described by the tagOrId parentObj.
 *
 * Results:
 *    Standard tcl result. parentPtrPtr filled in on success.
 *
 * Side effects:
 *    Leaves any error result in interp.
 */
int
TkPathCanvasFindGroup(
    Tcl_Interp * interp,
    Tk_PathCanvas canvas,
    Tcl_Obj * parentObj,
    Tk_PathItem ** parentPtrPtr)
{
TkPathCanvas *path = (TkPathCanvas *) canvas;
Tk_PathItem *parentPtr;
int result = TCL_OK;
TagSearch *searchPtr = NULL;   /* Allocated by first TagSearchScan, freed by
                                * TagSearchDestroy */

    if(parentObj != NULL) {
        if((result = TagSearchScan(path, parentObj, &searchPtr)) != TCL_OK) {
            return TCL_ERROR;
        }
        parentPtr = TagSearchFirst(searchPtr);
        if(parentPtr == NULL) {
            Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
                "tag \"", Tcl_GetString(parentObj),
                "\" doesn't match any items", NULL);
            result = TCL_ERROR;
        } else if(strcmp(parentPtr->typePtr->name, "group") != 0) {
            Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
                "tag \"", Tcl_GetString(parentObj),
                "\" is not a group item", NULL);
            result = TCL_ERROR;
        } else {
            *parentPtrPtr = parentPtr;
        }
        TagSearchDestroy(searchPtr);
    }
    return result;
}

/*
* TkPathCanvasTranslateGroup --
*
*
* Results:
*
* Side effects:
*/
void
TkPathCanvasTranslateGroup(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int compensate,
    double deltaX,
    double deltaY)
{
    TkPathCanvas *path = (TkPathCanvas *) canvas;
    Tk_PathItem *walkPtr;

    /* Round the deltas to the nearest integer to avoid round-off errors */
    deltaX = (double)((int)(deltaX + (deltaX > 0 ? 0.5 : -0.5)));
    deltaY = (double)((int)(deltaY + (deltaY > 0 ? 0.5 : -0.5)));

    /*
     * Invoke all its childs translateProc. Any child groups will call this
     * function recursively.
     */
    for(walkPtr = itemPtr->firstChildPtr; walkPtr != NULL;
        walkPtr = walkPtr->nextPtr) {
        EventuallyRedrawItem(canvas, walkPtr);
        (void)(*walkPtr->typePtr->translateProc) (canvas, walkPtr, compensate,
            deltaX, deltaY);
        EventuallyRedrawItem(canvas, walkPtr);
        path->flags |= REPICK_NEEDED;
    }
}

/*
 * TkPathGroupItemConfigured --
 *
 *    Schedules all children of a group for redisplay in a recursive way.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    A number of items scheduled for redisplay.
 */
void
TkPathGroupItemConfigured(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int mask)
{
    Tk_PathItem *walkPtr;

    for(walkPtr = itemPtr->firstChildPtr; walkPtr != NULL;
        walkPtr = walkPtr->nextPtr) {
        EventuallyRedrawItem(canvas, walkPtr);
        if(walkPtr->typePtr->bboxProc != NULL) {
            (*walkPtr->typePtr->bboxProc) (canvas, walkPtr, mask);
            /*
             * Only if the item responds to the bboxProc we need to redraw it.
             */
            EventuallyRedrawItem(canvas, walkPtr);
        }
        if(walkPtr->typePtr == &tkPathTypeGroup) {
            /*
             * Call ourself recursively for each group.
             * @@@ An alternative would be to have this call in the group's
             *     own bbox proc.
             */
            TkPathGroupItemConfigured(canvas, walkPtr, mask);
        }
    }
}

/*
* TkPathCanvasScaleGroup --
*
*
* Results:
*
* Side effects:
*/
void
TkPathCanvasScaleGroup(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int compensate,
    double originX,
    double originY,
    double scaleX,
    double scaleY)
{
    TkPathCanvas *path = (TkPathCanvas *) canvas;
    Tk_PathItem *walkPtr;

    /*
     * Invoke all its childs scaleProc. Any child groups will call this
     * function recursively.
     */
    for(walkPtr = itemPtr->firstChildPtr; walkPtr != NULL;
        walkPtr = walkPtr->nextPtr) {
        EventuallyRedrawItem(canvas, walkPtr);
        (void)(*walkPtr->typePtr->scaleProc) (canvas, walkPtr,
            compensate, originX, originY, scaleX, scaleY);
        EventuallyRedrawItem(canvas, walkPtr);
        path->flags |= REPICK_NEEDED;
    }
}

/*
 * SetAncestorsDirtyBbox --
 *
 *    Used by items when they need a redisplay for some reason
 *    so that its ancestor groups know that they need to compute
 *    a new bbox when requested.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Groups get their dirty bbox flag set.
 */
static void
SetAncestorsDirtyBbox(
    Tk_PathItem * itemPtr)
{
Tk_PathItem *walkPtr;

    walkPtr = itemPtr->parentPtr;
    while(walkPtr != NULL) {
        TkPathCanvasSetGroupDirtyBbox(walkPtr);
        walkPtr = walkPtr->parentPtr;
    }
}

/*
* TkPathCanvasGroupBbox --
*
*
* Results:
*
* Side effects:
*/
void
TkPathCanvasGroupBbox(
    Tk_PathCanvas canvas,
    Tk_PathItem * itemPtr,
    int *x1Ptr,
    int *y1Ptr,
    int *x2Ptr,
    int *y2Ptr)
{
    int gotAny = 0;
    Tk_PathItem *walkPtr;
    int x1 = -1, y1 = -1, x2 = -1, y2 = -1;

    for(walkPtr = itemPtr->firstChildPtr; walkPtr != NULL;
        walkPtr = walkPtr->nextPtr) {

        /*
         * Make sure sub groups have its bbox updated.
         * We may be called recursively.
         */
        if(walkPtr->firstChildPtr != NULL) {
            TkPathCanvasUpdateGroupBbox(canvas, walkPtr);
        }
        if((walkPtr->x1 >= walkPtr->x2)
            || (walkPtr->y1 >= walkPtr->y2)) {
            continue;
        }
        if(!gotAny) {
            x1 = walkPtr->x1;
            y1 = walkPtr->y1;
            x2 = walkPtr->x2;
            y2 = walkPtr->y2;
            gotAny = 1;
        } else {
            if(walkPtr->x1 < x1) {
                x1 = walkPtr->x1;
            }
            if(walkPtr->y1 < y1) {
                y1 = walkPtr->y1;
            }
            if(walkPtr->x2 > x2) {
                x2 = walkPtr->x2;
            }
            if(walkPtr->y2 > y2) {
                y2 = walkPtr->y2;
            }
        }
    }
    *x1Ptr = x1, *y1Ptr = y1, *x2Ptr = x2, *y2Ptr = y2;
}

/*
 * ItemCreate --
 *
 *    Creates a new item, configures it, and add it to the display tree.
 *    It remains for the calling code to call EventuallyRedrawItem
 *    and a few more canvas admin stuff.
 *    The item's creatProc may leave any error message in interp.
 *
 * Results:
 *    Standard Tcl result and a new item pointer in itemPtrPtr.
 *
 * Side effects:
 *    Item allocated, configured, and linked into display list.
 */
static int
ItemCreate(
    Tcl_Interp * interp,
    TkPathCanvas * path,
    Tk_PathItemType * typePtr,
    int isRoot,
    Tk_PathItem ** itemPtrPtr,
    int objc,
    Tcl_Obj * const objv[])
{
    Tk_PathItem *itemPtr;
    Tcl_HashEntry *entryPtr;
    int isNew = 0;
    int result;

    itemPtr = (Tk_PathItem *) ckalloc((unsigned)typePtr->itemSize);
    if(isRoot) {
        itemPtr->id = 0;
    } else {
        itemPtr->id = path->nextId;
        path->nextId++;
    }
    itemPtr->typePtr = typePtr;
    itemPtr->state = TK_PATHSTATE_NULL;
    itemPtr->redraw_flags = 0;
    itemPtr->optionTable = NULL;
    itemPtr->pathTagsPtr = NULL;
    itemPtr->nextPtr = NULL;
    itemPtr->prevPtr = NULL;
    itemPtr->firstChildPtr = NULL;
    itemPtr->lastChildPtr = NULL;

    /*
     * This is just to be able to detect if createProc processes
     * any -parent option.
     * NB: It is absolutely vital to set parentObj to NULL
     *     else option free bails.
     */
    itemPtr->parentPtr = NULL;
    itemPtr->parentObj = NULL;

    result = (*typePtr->createProc) (interp, (Tk_PathCanvas) path,
        itemPtr, objc, objv);
    if(result != TCL_OK) {
        ckfree((char *)itemPtr);
        return TCL_ERROR;
    }
    entryPtr = Tcl_CreateHashEntry(&path->idTable,
        (char *)INT2PTR(itemPtr->id), &isNew);
    Tcl_SetHashValue(entryPtr, itemPtr);

    /*
     * If item's createProc didn't put it in the display list we do.
     * Typically done only for the tk::canvas items which don't have
     * a -parent option.
     */
    if(!isRoot && (itemPtr->parentPtr == NULL)) {
        ItemAddToParent(path->rootItemPtr, itemPtr);
    }
    itemPtr->redraw_flags |= FORCE_REDRAW;
    *itemPtrPtr = itemPtr;

    return TCL_OK;
}

/*
* UnshareObj --
*
*
* Results:
*
* Side effects:
*/
static Tcl_Obj *
UnshareObj(
    Tcl_Obj * objPtr)
{
    if(Tcl_IsShared(objPtr)) {
Tcl_Obj *newObj = Tcl_DuplicateObj(objPtr);
        Tcl_DecrRefCount(objPtr);
        Tcl_IncrRefCount(newObj);
        return newObj;
    }
    return objPtr;
}

/*
 * TkPathCanvasItemIteratorNext --
 *
 *    Convinience function to obtain the next item in the item tree.
 *
 * Results:
 *    Tk_PathItem pointer.
 *
 * Side effects:
 *    None.
 */
Tk_PathItem *
TkPathCanvasItemIteratorNext(
    Tk_PathItem * itemPtr)
{
    if(itemPtr->firstChildPtr != NULL) {
        return itemPtr->firstChildPtr;
    }
    while(itemPtr->nextPtr == NULL) {
        itemPtr = itemPtr->parentPtr;
        if(itemPtr == NULL) {   /* root item */
            return NULL;
        }
    }
    return itemPtr->nextPtr;
}

/*
* TkPathCanvasItemIteratorPrev --
*
*
* Results:
*
* Side effects:
*/
Tk_PathItem *
TkPathCanvasItemIteratorPrev(
    Tk_PathItem * itemPtr)
{
Tk_PathItem *walkPtr;

    if(itemPtr->parentPtr == NULL) {    /* root item */
        return NULL;
    } else {
        walkPtr = itemPtr->parentPtr;
        if(itemPtr->prevPtr != NULL) {
            walkPtr = itemPtr->prevPtr;
            while(walkPtr != NULL && walkPtr->lastChildPtr != NULL) {
                walkPtr = walkPtr->lastChildPtr;
            }
        }
        return walkPtr;
    }
}

/*
* ItemGetNumTags --
*
*
* Results:
*
* Side effects:
*/
static int
ItemGetNumTags(
    Tk_PathItem * itemPtr)
{
    if(itemPtr->pathTagsPtr != NULL) {
        return itemPtr->pathTagsPtr->numTags;
    } else {
        return 0;
    }
}

/*
 * TkPathCanvasItemDetach --
 *
 *    Splice out (unlink) an item from the display list.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Item will be unlinked from the display list.
 */
void
TkPathCanvasItemDetach(
    Tk_PathItem * itemPtr)
{
Tk_PathItem *parentPtr;

    if(itemPtr->prevPtr != NULL) {
        itemPtr->prevPtr->nextPtr = itemPtr->nextPtr;
    }
    if(itemPtr->nextPtr != NULL) {
        itemPtr->nextPtr->prevPtr = itemPtr->prevPtr;
    }
    parentPtr = itemPtr->parentPtr;
    if((parentPtr != NULL) && (parentPtr->firstChildPtr == itemPtr)) {
        parentPtr->firstChildPtr = itemPtr->nextPtr;
        if(parentPtr->firstChildPtr == NULL) {
            parentPtr->lastChildPtr = NULL;
        }
    }
    if((parentPtr != NULL) && (parentPtr->lastChildPtr == itemPtr)) {
        parentPtr->lastChildPtr = itemPtr->prevPtr;
    }

    /*
     * This signals an orfan item.
     */
    itemPtr->nextPtr = itemPtr->prevPtr = itemPtr->parentPtr = NULL;
}

/*
 * ItemAddToParent --
 *
 *    Appends an item as the last sibling to a parent item.
 *    It doesn't do any unlinking from a previous tree position.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Display list updated.
 */
static void
ItemAddToParent(
    Tk_PathItem * parentPtr,
    Tk_PathItem * itemPtr)
{
    itemPtr->nextPtr = NULL;
    itemPtr->prevPtr = parentPtr->lastChildPtr;
    if(parentPtr->lastChildPtr != NULL) {
        parentPtr->lastChildPtr->nextPtr = itemPtr;
    } else {
        parentPtr->firstChildPtr = itemPtr;
    }
    parentPtr->lastChildPtr = itemPtr;
    itemPtr->parentPtr = parentPtr;
}

/*
 * ItemDelete --
 *
 *    Recursively frees all resources associated with an Item and its
 *    descendants and removes it from display list.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Items are removed from their parent and freed.
 */
static void
ItemDelete(
    TkPathCanvas * path,
    Tk_PathItem * itemPtr)
{
Tcl_HashEntry *entryPtr;

    /*
     * Remove any children by recursively calling us.
     * NB: This is very tricky code! Children updates
     *     the itemPtr->firstChildPtr here via calls
     *     to TkPathCanvasItemDetach.
     */
    while(itemPtr->firstChildPtr != NULL) {
        ItemDelete(path, itemPtr->firstChildPtr);
    }

    EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
    if(path->bindingTable != NULL) {
        Tk_DeleteAllBindings(path->bindingTable, (ClientData) itemPtr);
    }

    /*
     * The item type deleteProc is responsible for calling
     * Tk_FreeConfigOptions which will implicitly also clean up
     * the Tk_PathTags via its custom free proc.
     */
    (*itemPtr->typePtr->deleteProc) ((Tk_PathCanvas) path, itemPtr,
        path->display);

    entryPtr = Tcl_FindHashEntry(&path->idTable, (char *)INT2PTR(itemPtr->id));
    Tcl_DeleteHashEntry(entryPtr);
    TkPathCanvasItemDetach(itemPtr);

    if(itemPtr == path->currentItemPtr) {
        path->currentItemPtr = NULL;
        path->flags |= REPICK_NEEDED;
    }
    if(itemPtr == path->newCurrentPtr) {
        path->newCurrentPtr = NULL;
        path->flags |= REPICK_NEEDED;
    }
    if(itemPtr == path->textInfo.focusItemPtr) {
        path->textInfo.focusItemPtr = NULL;
    }
    if(itemPtr == path->textInfo.selItemPtr) {
        path->textInfo.selItemPtr = NULL;
    }
    if((itemPtr == path->hotPtr)
        || (itemPtr == path->hotPrevPtr)) {
        path->hotPtr = NULL;
    }
    ckfree((char *)itemPtr);
}

/*
* DebugGetItemInfo --
*
*
* Results:
*
* Side effects:
*/
static void
DebugGetItemInfo(
    Tk_PathItem * itemPtr,
    char *s)
{
    Tk_PathItem *p = itemPtr;
    char tmp[256];

    sprintf(tmp, " parentPtr->id=%d\t", (p->parentPtr ? p->parentPtr->id : -1));
    strcat(s, tmp);
    sprintf(tmp, " prevPtr->id=%d\t", (p->prevPtr ? p->prevPtr->id : -1));
    strcat(s, tmp);
    sprintf(tmp, " nextPtr->id=%d\t", (p->nextPtr ? p->nextPtr->id : -1));
    strcat(s, tmp);
    sprintf(tmp, " firstChildPtr->id=%d\t",
        (p->firstChildPtr ? p->firstChildPtr->id : -1));
    strcat(s, tmp);
    sprintf(tmp, " lastChildPtr->id=%d\t",
        (p->lastChildPtr ? p->lastChildPtr->id : -1));
    strcat(s, tmp);
}

/*
 * GetStaticUids --
 *
 *    This function is invoked to return a structure filled with the Uids
 *    used when doing tag searching. If it was never before called in the
 *    current thread, it initializes the structure for that thread (uids are
 *    only ever local to one thread [Bug 1114977]).
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 */
static SearchUids *
GetStaticUids(
    void)
{
    SearchUids *searchUids = (SearchUids *)
        Tcl_GetThreadData(&dataKey, sizeof(SearchUids));

    if(searchUids->allUid == NULL) {
        searchUids->allUid = Tk_GetUid("all");
        searchUids->currentUid = Tk_GetUid("current");
        searchUids->rootUid = Tk_GetUid("root");
        searchUids->andUid = Tk_GetUid("&&");
        searchUids->orUid = Tk_GetUid("||");
        searchUids->xorUid = Tk_GetUid("^");
        searchUids->parenUid = Tk_GetUid("(");
        searchUids->endparenUid = Tk_GetUid(")");
        searchUids->negparenUid = Tk_GetUid("!(");
        searchUids->tagvalUid = Tk_GetUid("!!");
        searchUids->negtagvalUid = Tk_GetUid("!");
    }
    return searchUids;
}

/*
 * TagSearchExprInit --
 *
 *    This function allocates and initializes one TagSearchExpr struct.
 *
 * Results:
 *
 * Side effects:
 */
static void
TagSearchExprInit(
    TkPathTagSearchExpr ** exprPtrPtr)
{
TkPathTagSearchExpr *expr = *exprPtrPtr;

    if(!expr) {
        expr = (TkPathTagSearchExpr *) ckalloc(sizeof(TkPathTagSearchExpr));
        expr->allocated = 0;
        expr->uids = NULL;
        expr->next = NULL;
    }
    expr->uid = NULL;
    expr->index = 0;
    expr->length = 0;
    *exprPtrPtr = expr;
}

/*
 * TagSearchExprDestroy --
 *
 *    This function destroys one TkPathTagSearchExpr structure.
 *
 * Results:
 *
 * Side effects:
 */
static void
TagSearchExprDestroy(
    TkPathTagSearchExpr * expr)
{
    if(expr) {
        if(expr->uids) {
            ckfree((char *)expr->uids);
        }
        ckfree((char *)expr);
    }
}

/*
 * TagSearchScan --
 *
 *    This function is called to initiate an enumeration of all items in a
 *    given canvas that contain a tag that matches the tagOrId expression.
 *
 * Results:
 *    The return value indicates if the tagOrId expression was successfully
 *    scanned (syntax). The information at *searchPtr is initialized such
 *    that a call to TagSearchFirst, followed by successive calls to
 *    TagSearchNext will return items that match tag.
 *
 * Side effects:
 *    SearchPtr is linked into a list of searches in progress on path,
 *    so that elements can safely be deleted while the search is in
 *    progress.
 */
static int
TagSearchScan(
    TkPathCanvas * path,       /* Canvas whose items are to be searched. */
    Tcl_Obj * tagObj,          /* Object giving tag value. */
    TagSearch ** searchPtrPtr)
{              /* Record describing tag search; will be
                * initialized here. */
char *tag = Tcl_GetString(tagObj);
int i;
TagSearch *searchPtr;

    /*
     * Initialize the search.
     */

    if(*searchPtrPtr) {
        searchPtr = *searchPtrPtr;
    } else {
        /*
         * Allocate primary search struct on first call.
         */

        *searchPtrPtr = searchPtr = (TagSearch *) ckalloc(sizeof(TagSearch));
        searchPtr->expr = NULL;

        /*
         * Allocate buffer for rewritten tags (after de-escaping).
         */

        searchPtr->rewritebufferAllocated = 100;
        searchPtr->rewritebuffer =
            (char *)ckalloc(searchPtr->rewritebufferAllocated);
    }
    TagSearchExprInit(&(searchPtr->expr));

    /*
     * How long is the tagOrId?
     */

    searchPtr->stringLength = (int)strlen(tag);

    /*
     * Make sure there is enough buffer to hold rewritten tags.
     */

    if((unsigned int)searchPtr->stringLength >=
        searchPtr->rewritebufferAllocated) {
        searchPtr->rewritebufferAllocated = searchPtr->stringLength + 100;
        searchPtr->rewritebuffer = (char *)
            ckrealloc(searchPtr->rewritebuffer,
            searchPtr->rewritebufferAllocated);
    }

    /*
     * Initialize search.
     */

    searchPtr->path = path;
    searchPtr->searchOver = 0;
    searchPtr->type = SEARCH_TYPE_EMPTY;

    /*
     * Find the first matching item in one of several ways. If the tag is a
     * number then it selects the single item with the matching identifier.
     * In this case see if the item being requested is the hot item, in which
     * case the search can be skipped.
     */

    if(searchPtr->stringLength && isdigit(UCHAR(*tag))) {
char *end;

        searchPtr->id = strtoul(tag, &end, 0);
        if(*end == 0) {
            searchPtr->type = SEARCH_TYPE_ID;
            return TCL_OK;
        }
    }

    /*
     * For all other tags and tag expressions convert to a UID. This UID is
     * kept forever, but this should be thought of as a cache rather than as a
     * memory leak.
     */
    searchPtr->expr->uid = Tk_GetUid(tag);

    /*
     * Short circuit impossible searches for null tags.
     */

    if(searchPtr->stringLength == 0) {
        return TCL_OK;
    }

    /*
     * Pre-scan tag for at least one unquoted "&&" "||" "^" "!"
     *   if not found then use string as simple tag
     */

    for(i = 0; i < searchPtr->stringLength; i++) {
        if(tag[i] == '"') {
            i++;
            for(; i < searchPtr->stringLength; i++) {
                if(tag[i] == '\\') {
                    i++;
                    continue;
                }
                if(tag[i] == '"') {
                    break;
                }
            }
        } else if((tag[i] == '&' && tag[i + 1] == '&')
            || (tag[i] == '|' && tag[i + 1] == '|')
            || (tag[i] == '^')
            || (tag[i] == '!')) {
            searchPtr->type = SEARCH_TYPE_EXPR;
            break;
        }
    }

    searchPtr->string = tag;
    searchPtr->stringIndex = 0;
    if(searchPtr->type == SEARCH_TYPE_EXPR) {
        /*
         * An operator was found in the prescan, so now compile the tag
         * expression into array of Tk_Uid flagging any syntax errors found.
         */

        if(TagSearchScanExpr(path->interp, searchPtr,
                searchPtr->expr) != TCL_OK) {
            /*
             * Syntax error in tag expression. The result message was set by
             * TagSearchScanExpr.
             */

            return TCL_ERROR;
        }
        searchPtr->expr->length = searchPtr->expr->index;
    } else if(searchPtr->expr->uid == GetStaticUids()->allUid) {
        /*
         * All items match.
         */

        searchPtr->type = SEARCH_TYPE_ALL;
    } else if(searchPtr->expr->uid == GetStaticUids()->rootUid) {
        searchPtr->type = SEARCH_TYPE_ROOT;
    } else {
        /*
         * Optimized single-tag search
         */

        searchPtr->type = SEARCH_TYPE_TAG;
    }
    return TCL_OK;
}

/*
 * TagSearchDestroy --
 *
 *    This function destroys any dynamic structures that may have been
 *    allocated by TagSearchScan.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    Deallocates memory.
 */
static void
TagSearchDestroy(
    TagSearch * searchPtr)
{              /* Record describing tag search */
    if(searchPtr) {
        TagSearchExprDestroy(searchPtr->expr);
        ckfree((char *)searchPtr->rewritebuffer);
        ckfree((char *)searchPtr);
    }
}

/*
 * TagSearchScanExpr --
 *
 *    This recursive function is called to scan a tag expression and compile
 *    it into an array of Tk_Uids.
 *
 * Results:
 *    The return value indicates if the tagOrId expression was successfully
 *    scanned (syntax). The information at *searchPtr is initialized such
 *    that a call to TagSearchFirst, followed by successive calls to
 *    TagSearchNext will return items that match tag.
 *
 * Side effects:
 */
static int
TagSearchScanExpr(
    Tcl_Interp * interp,       /* Current interpreter. */
    TagSearch * searchPtr,     /* Search data */
    TkPathTagSearchExpr * expr /* compiled expression result */)
{             
    int looking_for_tag;       /* When true, scanner expects next char(s) to
                                * be a tag, else operand expected */
    int found_tag;             /* One or more tags found */
    int found_endquote;        /* For quoted tag string parsing */
    int negate_result;         /* Pending negation of next tag value */
    char *tag;                 /* Tag from tag expression string */
    char c;
    SearchUids *searchUids;    /* Collection of uids for basic search
                                * expression terms. */

    searchUids = GetStaticUids();
    negate_result = 0;
    found_tag = 0;
    looking_for_tag = 1;
    while(searchPtr->stringIndex < searchPtr->stringLength) {
        c = searchPtr->string[searchPtr->stringIndex++];

        /*
         * Need two slots free at this point, not one. [Bug 2931374]
         */

        if(expr->index >= expr->allocated - 1) {
            expr->allocated += 15;
            if(expr->uids) {
                expr->uids = (Tk_Uid *)
                    ckrealloc((char *)(expr->uids),
                    (expr->allocated) * sizeof(Tk_Uid));
            } else {
                expr->uids = (Tk_Uid *)
                    ckalloc((expr->allocated) * sizeof(Tk_Uid));
            }
        }

        if(looking_for_tag) {
            switch (c) {
            case ' ':  /* ignore unquoted whitespace */
            case '\t':
            case '\n':
            case '\r':
                break;

            case '!':  /* negate next tag or subexpr */
                if(looking_for_tag > 1) {
                    Tcl_AppendResult(interp,
                        "Too many '!' in tag search expression", NULL);
                    return TCL_ERROR;
                }
                looking_for_tag++;
                negate_result = 1;
                break;

            case '(':  /* scan (negated) subexpr recursively */
                if(negate_result) {
                    expr->uids[expr->index++] = searchUids->negparenUid;
                    negate_result = 0;
                } else {
                    expr->uids[expr->index++] = searchUids->parenUid;
                }
                if(TagSearchScanExpr(interp, searchPtr, expr) != TCL_OK) {
                    /*
                     * Result string should be already set by nested call to
                     * tag_expr_scan()
                     */

                    return TCL_ERROR;
                }
                looking_for_tag = 0;
                found_tag = 1;
                break;

            case '"':  /* quoted tag string */
                if(negate_result) {
                    expr->uids[expr->index++] = searchUids->negtagvalUid;
                    negate_result = 0;
                } else {
                    expr->uids[expr->index++] = searchUids->tagvalUid;
                }
                tag = searchPtr->rewritebuffer;
                found_endquote = 0;
                while(searchPtr->stringIndex < searchPtr->stringLength) {
                    c = searchPtr->string[searchPtr->stringIndex++];
                    if(c == '\\') {
                        c = searchPtr->string[searchPtr->stringIndex++];
                    }
                    if(c == '"') {
                        found_endquote = 1;
                        break;
                    }
                    *tag++ = c;
                }
                if(!found_endquote) {
                    Tcl_AppendResult(interp,
                        "Missing endquote in tag search expression", NULL);
                    return TCL_ERROR;
                }
                if(!(tag - searchPtr->rewritebuffer)) {
                    Tcl_AppendResult(interp,
                        "Null quoted tag string in tag search expression",
                        NULL);
                    return TCL_ERROR;
                }
                *tag++ = '\0';
                expr->uids[expr->index++] = Tk_GetUid(searchPtr->rewritebuffer);
                looking_for_tag = 0;
                found_tag = 1;
                break;

            case '&':  /* illegal chars when looking for tag */
            case '|':
            case '^':
            case ')':
                Tcl_AppendResult(interp,
                    "Unexpected operator in tag search expression", NULL);
                return TCL_ERROR;

            default:   /* unquoted tag string */
                if(negate_result) {
                    expr->uids[expr->index++] = searchUids->negtagvalUid;
                    negate_result = 0;
                } else {
                    expr->uids[expr->index++] = searchUids->tagvalUid;
                }
                tag = searchPtr->rewritebuffer;
                *tag++ = c;

                /*
                 * Copy rest of tag, including any embedded whitespace.
                 */

                while(searchPtr->stringIndex < searchPtr->stringLength) {
                    c = searchPtr->string[searchPtr->stringIndex];
                    if(c == '!' || c == '&' || c == '|' || c == '^'
                        || c == '(' || c == ')' || c == '"') {
                        break;
                    }
                    *tag++ = c;
                    searchPtr->stringIndex++;
                }

                /*
                 * Remove trailing whitespace.
                 */

                while(1) {
                    c = *--tag;

                    /*
                     * There must have been one non-whitespace char, so this
                     * will terminate.
                     */

                    if(c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                        break;
                    }
                }
                *++tag = '\0';
                expr->uids[expr->index++] = Tk_GetUid(searchPtr->rewritebuffer);
                looking_for_tag = 0;
                found_tag = 1;
            }

        } else {        /* ! looking_for_tag */
            switch (c) {
            case ' ':  /* ignore whitespace */
            case '\t':
            case '\n':
            case '\r':
                break;

            case '&':  /* AND operator */
                c = searchPtr->string[searchPtr->stringIndex++];
                if(c != '&') {
                    Tcl_AppendResult(interp,
                        "Singleton '&' in tag search expression", NULL);
                    return TCL_ERROR;
                }
                expr->uids[expr->index++] = searchUids->andUid;
                looking_for_tag = 1;
                break;

            case '|':  /* OR operator */
                c = searchPtr->string[searchPtr->stringIndex++];
                if(c != '|') {
                    Tcl_AppendResult(interp,
                        "Singleton '|' in tag search expression", NULL);
                    return TCL_ERROR;
                }
                expr->uids[expr->index++] = searchUids->orUid;
                looking_for_tag = 1;
                break;

            case '^':  /* XOR operator */
                expr->uids[expr->index++] = searchUids->xorUid;
                looking_for_tag = 1;
                break;

            case ')':  /* end subexpression */
                expr->uids[expr->index++] = searchUids->endparenUid;
                goto breakwhile;

            default:   /* syntax error */
                Tcl_AppendResult(interp,
                    "Invalid boolean operator in tag search expression", NULL);
                return TCL_ERROR;
            }
        }
    }

  breakwhile:
    if(found_tag && !looking_for_tag) {
        return TCL_OK;
    }
    Tcl_AppendResult(interp, "Missing tag in tag search expression", NULL);
    return TCL_ERROR;
}

/*
 * TagSearchEvalExpr --
 *
 *    This recursive function is called to eval a tag expression.
 *
 * Results:
 *    The return value indicates if the tagOrId expression successfully
 *    matched the tags of the current item.
 *
 * Side effects:
 */
static int
TagSearchEvalExpr(
    TkPathTagSearchExpr * expr, /* Search expression */
    Tk_PathItem * itemPtr       /* Item being test for match */)
{          
    int looking_for_tag;       /* When true, scanner expects next char(s) to
                                * be a tag, else operand expected. */
    int negate_result;         /* Pending negation of next tag value */
    Tk_Uid uid;
    Tk_Uid *tagPtr;
    Tk_PathTags *ptagsPtr;
    int count;
    int result;                /* Value of expr so far */
    int parendepth;
    SearchUids *searchUids;    /* Collection of uids for basic search
                                * expression terms. */

    searchUids = GetStaticUids();
    result = 0; /* just to keep the compiler quiet */

    negate_result = 0;
    looking_for_tag = 1;
    while(expr->index < expr->length) {
        uid = expr->uids[expr->index++];
        if(looking_for_tag) {
            if(uid == searchUids->tagvalUid) {
/*
 *        assert(expr->index < expr->length);
 */
                uid = expr->uids[expr->index++];
                result = 0;

                /*
                 * set result 1 if tag is found in item's tags
                 */

                ptagsPtr = itemPtr->pathTagsPtr;
                if(ptagsPtr != NULL) {
                    for(tagPtr = ptagsPtr->tagPtr, count = ptagsPtr->numTags;
                        count > 0; tagPtr++, count--) {
                        if(*tagPtr == uid) {
                            result = 1;
                            break;
                        }
                    }
                }
            } else if(uid == searchUids->negtagvalUid) {
                negate_result = !negate_result;
/*
 *        assert(expr->index < expr->length);
 */
                uid = expr->uids[expr->index++];
                result = 0;

                /*
                 * set result 1 if tag is found in item's tags
                 */
                ptagsPtr = itemPtr->pathTagsPtr;
                if(ptagsPtr != NULL) {
                    for(tagPtr = ptagsPtr->tagPtr, count = ptagsPtr->numTags;
                        count > 0; tagPtr++, count--) {
                        if(*tagPtr == uid) {
                            result = 1;
                            break;
                        }
                    }
                }
            } else if(uid == searchUids->parenUid) {
                /*
                 * Evaluate subexpressions with recursion
                 */

                result = TagSearchEvalExpr(expr, itemPtr);

            } else if(uid == searchUids->negparenUid) {
                negate_result = !negate_result;

                /*
                 * Evaluate subexpressions with recursion
                 */

                result = TagSearchEvalExpr(expr, itemPtr);
/*
 *        } else {
 *        assert(0);
 */
            }
            if(negate_result) {
                result = !result;
                negate_result = 0;
            }
            looking_for_tag = 0;
        } else {        /* ! looking_for_tag */
            if(((uid == searchUids->andUid) && (!result)) ||
                ((uid == searchUids->orUid) && result)) {
                /*
                 * Short circuit expression evaluation.
                 *
                 * if result before && is 0, or result before || is 1, then
                 * the expression is decided and no further evaluation is
                 * needed.
                 */

                parendepth = 0;
                while(expr->index < expr->length) {
                    uid = expr->uids[expr->index++];
                    if(uid == searchUids->tagvalUid ||
                        uid == searchUids->negtagvalUid) {
                        expr->index++;
                        continue;
                    }
                    if(uid == searchUids->parenUid ||
                        uid == searchUids->negparenUid) {
                        parendepth++;
                        continue;
                    }
                    if(uid == searchUids->endparenUid) {
                        parendepth--;
                        if(parendepth < 0) {
                            break;
                        }
                    }
                }
                return result;

            } else if(uid == searchUids->xorUid) {
                /*
                 * If the previous result was 1 then negate the next result.
                 */

                negate_result = result;

            } else if(uid == searchUids->endparenUid) {
                return result;
/*
 *        } else {
 *        assert(0);
 */
            }
            looking_for_tag = 1;
        }
    }
/*
 *  assert(! looking_for_tag);
 */
    return result;
}

/*
 * TagSearchFirst --
 *
 *    This function is called to get the first item item that matches a
 *    preestablished search predicate that was set by TagSearchScan.
 *
 * Results:
 *    The return value is a pointer to the first item, or NULL if there is
 *    no such item. The information at *searchPtr is updated such that
 *    successive calls to TagSearchNext will return successive items.
 *
 * Side effects:
 *    SearchPtr is linked into a list of searches in progress on path,
 *    so that elements can safely be deleted while the search is in
 *    progress.
 */
static Tk_PathItem *
TagSearchFirst(
    TagSearch * searchPtr /* Record describing tag search */)
{             
    Tk_PathItem *itemPtr, *lastPtr;
    Tk_Uid uid, *tagPtr;
    Tk_PathTags *ptagsPtr;
    int count;

    /*
     * Short circuit impossible searches for null tags.
     */

    if(searchPtr->stringLength == 0) {
        return NULL;
    }

    /*
     * Find the first matching item in one of several ways. If the tag is a
     * number then it selects the single item with the matching identifier.
     * In this case see if the item being requested is the hot item, in which
     * case the search can be skipped.
     */

    if(searchPtr->type == SEARCH_TYPE_ID) {
Tcl_HashEntry *entryPtr;

        itemPtr = searchPtr->path->hotPtr;
        lastPtr = searchPtr->path->hotPrevPtr;
        if((itemPtr == NULL) || (itemPtr->id != searchPtr->id)
            || (lastPtr == NULL)
            || (TkPathCanvasItemIteratorNext(lastPtr) != itemPtr)) {
            entryPtr =
                Tcl_FindHashEntry(&searchPtr->path->idTable,
                (char *)INT2PTR(searchPtr->id));
            if(entryPtr != NULL) {
                itemPtr = (Tk_PathItem *) Tcl_GetHashValue(entryPtr);
                lastPtr = TkPathCanvasItemIteratorPrev(itemPtr);
            } else {
                lastPtr = itemPtr = NULL;
            }
        }
        searchPtr->lastPtr = lastPtr;
        searchPtr->searchOver = 1;
        searchPtr->path->hotPtr = itemPtr;
        searchPtr->path->hotPrevPtr = lastPtr;
        return itemPtr;
    }

    if(searchPtr->type == SEARCH_TYPE_ALL) {
        /*
         * All items match.
         */

        searchPtr->lastPtr = NULL;
        searchPtr->currentPtr = searchPtr->path->rootItemPtr;
        return searchPtr->path->rootItemPtr;
    }
    if(searchPtr->type == SEARCH_TYPE_ROOT) {
        itemPtr = searchPtr->path->rootItemPtr;
        lastPtr = NULL;
        searchPtr->lastPtr = lastPtr;
        searchPtr->searchOver = 1;
        searchPtr->path->hotPtr = itemPtr;
        searchPtr->path->hotPrevPtr = lastPtr;
        return itemPtr;
    }

    if(searchPtr->type == SEARCH_TYPE_TAG) {
        /*
         * Optimized single-tag search
         */

        uid = searchPtr->expr->uid;
        for(lastPtr = NULL, itemPtr = searchPtr->path->rootItemPtr;
            itemPtr != NULL;
            lastPtr = itemPtr, itemPtr =
            TkPathCanvasItemIteratorNext(itemPtr)) {
            ptagsPtr = itemPtr->pathTagsPtr;
            if(ptagsPtr != NULL) {
                for(tagPtr = ptagsPtr->tagPtr, count = ptagsPtr->numTags;
                    count > 0; tagPtr++, count--) {
                    if(*tagPtr == uid) {
                        searchPtr->lastPtr = lastPtr;
                        searchPtr->currentPtr = itemPtr;
                        return itemPtr;
                    }
                }
            }
        }
    } else {

        /*
         * None of the above. Search for an item matching the tag expression.
         */

        for(lastPtr = NULL, itemPtr = searchPtr->path->rootItemPtr;
            itemPtr != NULL;
            lastPtr = itemPtr, itemPtr =
            TkPathCanvasItemIteratorNext(itemPtr)) {
            searchPtr->expr->index = 0;
            if(TagSearchEvalExpr(searchPtr->expr, itemPtr)) {
                searchPtr->lastPtr = lastPtr;
                searchPtr->currentPtr = itemPtr;
                return itemPtr;
            }
        }
    }
    searchPtr->lastPtr = lastPtr;
    searchPtr->searchOver = 1;
    return NULL;
}

/*
 * TagSearchNext --
 *
 *    This function returns successive items that match a given tag; it
 *    should be called only after TagSearchFirst has been used to begin a
 *    search.
 *
 * Results:
 *    The return value is a pointer to the next item that matches the tag
 *    expr specified to TagSearchScan, or NULL if no such item exists.
 *    *SearchPtr is updated so that the next call to this function will
 *    return the next item.
 *
 * Side effects:
 *    None.
 */
static Tk_PathItem *
TagSearchNext(
    TagSearch * searchPtr  /* Record describing search in progress. */)
{            
Tk_PathItem *itemPtr, *lastPtr;
Tk_PathTags *ptagsPtr;
Tk_Uid uid, *tagPtr;
int count;

    /*
     * Find next item in list (this may not actually be a suitable one to
     * return), and return if there are no items left.
     */

    lastPtr = searchPtr->lastPtr;
    if(lastPtr == NULL) {
        itemPtr = searchPtr->path->rootItemPtr;
    } else {
        itemPtr = TkPathCanvasItemIteratorNext(lastPtr);
    }
    if((itemPtr == NULL) || (searchPtr->searchOver)) {
        searchPtr->searchOver = 1;
        return NULL;
    }
    if(itemPtr != searchPtr->currentPtr) {
        /*
         * The structure of the list has changed. Probably the previously-
         * returned item was removed from the list. In this case, don't
         * advance lastPtr; just return its new successor (i.e. do nothing
         * here).
         */
    } else {
        lastPtr = itemPtr;
        itemPtr = TkPathCanvasItemIteratorNext(lastPtr);
    }

    if(searchPtr->type == SEARCH_TYPE_ALL) {
        /*
         * All items match.
         */

        searchPtr->lastPtr = lastPtr;
        searchPtr->currentPtr = itemPtr;
        return itemPtr;
    }

    if(searchPtr->type == SEARCH_TYPE_TAG) {
        /*
         * Optimized single-tag search
         */

        uid = searchPtr->expr->uid;
        for(; itemPtr != NULL;
            lastPtr = itemPtr, itemPtr =
            TkPathCanvasItemIteratorNext(itemPtr)) {
            ptagsPtr = itemPtr->pathTagsPtr;
            if(ptagsPtr != NULL) {
                for(tagPtr = ptagsPtr->tagPtr, count = ptagsPtr->numTags;
                    count > 0; tagPtr++, count--) {
                    if(*tagPtr == uid) {
                        searchPtr->lastPtr = lastPtr;
                        searchPtr->currentPtr = itemPtr;
                        return itemPtr;
                    }
                }
            }
        }
        searchPtr->lastPtr = lastPtr;
        searchPtr->searchOver = 1;
        return NULL;
    }

    /*
     * Else.... evaluate tag expression
     */

    for(; itemPtr != NULL;
        lastPtr = itemPtr, itemPtr = TkPathCanvasItemIteratorNext(itemPtr)) {
        searchPtr->expr->index = 0;
        if(TagSearchEvalExpr(searchPtr->expr, itemPtr)) {
            searchPtr->lastPtr = lastPtr;
            searchPtr->currentPtr = itemPtr;
            return itemPtr;
        }
    }
    searchPtr->lastPtr = lastPtr;
    searchPtr->searchOver = 1;
    return NULL;
}

/*
 * DoItem --
 *
 *    This is a utility function called by FindItems. It either adds
 *    itemPtr's id to the result forming in interp, or it adds a new tag to
 *    itemPtr, depending on the value of tag.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    If tag is NULL then itemPtr's id is added as a list element to the
 *    interp's result; otherwise tag is added to itemPtr's list of tags.
 */
static void
DoItem(
    Tcl_Interp * interp,       /* Interpreter in which to (possibly) record
                                * item id. */
    Tk_PathItem * itemPtr,     /* Item to (possibly) modify. */
    Tk_Uid tag)
{              /* Tag to add to those already present for
                * item, or NULL. */
Tk_Uid *tagPtr;
Tk_PathTags *ptagsPtr;
int count;

    /*
     * Handle the "add-to-result" case and return, if appropriate.
     */

    if(tag == NULL) {
char msg[TCL_INTEGER_SPACE];

        sprintf(msg, "%d", itemPtr->id);
        Tcl_AppendElement(interp, msg);
        return;
    }

    /*
     * Do not add if already there.
     */

    ptagsPtr = itemPtr->pathTagsPtr;
    if(ptagsPtr != NULL) {
        for(tagPtr = ptagsPtr->tagPtr, count = ptagsPtr->numTags;
            count > 0; tagPtr++, count--) {
            if(tag == *tagPtr) {
                return;
            }
        }
    }

    /*
     * Grow the tag space if there's no more room left in the current block.
     */

    if(itemPtr->pathTagsPtr == NULL) {
        ptagsPtr = TkPathAllocTagsFromObj(NULL, NULL);
        itemPtr->pathTagsPtr = ptagsPtr;
        tagPtr = ptagsPtr->tagPtr;
    } else {
        ptagsPtr = itemPtr->pathTagsPtr;
        if(ptagsPtr->tagSpace == ptagsPtr->numTags) {
Tk_Uid *newTagPtr;

            ptagsPtr->tagSpace += 5;
            newTagPtr = (Tk_Uid *)
                ckalloc((unsigned)(ptagsPtr->tagSpace * sizeof(Tk_Uid)));
            memcpy((void *)newTagPtr, ptagsPtr->tagPtr,
                ptagsPtr->numTags * sizeof(Tk_Uid));
            ckfree((char *)ptagsPtr->tagPtr);
            ptagsPtr->tagPtr = newTagPtr;
        }

        /* NB: This returns the first free tag address. */
        tagPtr = &ptagsPtr->tagPtr[ptagsPtr->numTags];
    }

    /*
     * Add in the new tag.
     */

    *tagPtr = tag;
    ptagsPtr->numTags++;
}

/*
 * FindItems --
 *
 *    This function does all the work of implementing the "find" and
 *    "addtag" options of the canvas widget command, which locate items that
 *    have certain features (location, tags, position in display list, etc.)
 *
 * Results:
 *    A standard Tcl return value. If newTag is NULL, then a list of ids
 *    from all the items that match objc/objv is returned in the interp's
 *    result. If newTag is NULL, then the normal the interp's result is an
 *    empty string. If an error occurs, then the interp's result will hold
 *    an error message.
 *
 * Side effects:
 *    If newTag is non-NULL, then all the items that match the information
 *    in objc/objv have that tag added to their lists of tags.
 */
static int
    FindItems(
    Tcl_Interp * interp,       /* Interpreter for error reporting. */
    TkPathCanvas * path,       /* Canvas whose items are to be searched. */
    int objc,                  /* Number of entries in argv. Must be greater
                                * than zero. */
    Tcl_Obj * const *objv,     /* Arguments that describe what items to
                                * search for (see user doc on "find" and
                                * "addtag" options). */
    Tcl_Obj * newTag,          /* If non-NULL, gives new tag to set on all
                                * found items; if NULL, then ids of found
                                * items are returned in the interp's
                                * result. */
    int first,                 /* For error messages: gives number of
                                * elements of objv which are already
                                * handled. */
    TagSearch ** searchPtrPtr  /* From CanvasWidgetCmd local vars */
    ) {
    Tk_PathItem *itemPtr;
    Tk_Uid uid;
    int index, result;
    static const char *optionStrings[] = {
        "above", "all", "below", "closest",
        "enclosed", "overlapping", "withtag", NULL
    };
    enum options {
        CANV_ABOVE, CANV_ALL, CANV_BELOW, CANV_CLOSEST,
        CANV_ENCLOSED, CANV_OVERLAPPING, CANV_WITHTAG
    };

    if(newTag != NULL) {
        uid = Tk_GetUid(Tcl_GetString(newTag));
    } else {
        uid = NULL;
    }
    if(Tcl_GetIndexFromObj(interp, objv[first], optionStrings,
            "search command", 0, &index) != TCL_OK) {
        return TCL_ERROR;
    }
    switch ((enum options)index) {
    case CANV_ABOVE:{
    Tk_PathItem *lastPtr = NULL;

        if(objc != first + 2) {
            Tcl_WrongNumArgs(interp, first + 1, objv, "tagOrId");
            return TCL_ERROR;
        }
        FOR_EVERY_CANVAS_ITEM_MATCHING(objv[first + 1], searchPtrPtr,
            return TCL_ERROR) {
            lastPtr = itemPtr;
        }

        /* We constrain this to siblings. */
        if((lastPtr != NULL) && (lastPtr->nextPtr != NULL)) {
            DoItem(interp, lastPtr->nextPtr, uid);
        }
        break;
    }
    case CANV_ALL:
        if(objc != first + 1) {
            Tcl_WrongNumArgs(interp, first + 1, objv, NULL);
            return TCL_ERROR;
        }
        for(itemPtr = path->rootItemPtr; itemPtr != NULL;
            itemPtr = TkPathCanvasItemIteratorNext(itemPtr)) {
            DoItem(interp, itemPtr, uid);
        }
        break;

    case CANV_BELOW:
        if(objc != first + 2) {
            Tcl_WrongNumArgs(interp, first + 1, objv, "tagOrId");
            return TCL_ERROR;
        }
        FIRST_CANVAS_ITEM_MATCHING(objv[first + 1], searchPtrPtr,
            return TCL_ERROR);
        if(itemPtr != NULL) {

            /* We constrain this to siblings. */
            if(itemPtr->prevPtr != NULL) {
                DoItem(interp, itemPtr->prevPtr, uid);
            }
        }
        break;
    case CANV_CLOSEST:{
    double closestDist;
    Tk_PathItem *startPtr, *closestPtr;
    double coords[2], halo;
    int x1, y1, x2, y2;

        if((objc < first + 3) || (objc > first + 5)) {
            Tcl_WrongNumArgs(interp, first + 1, objv, "x y ?halo? ?start?");
            return TCL_ERROR;
        }
        if((Tk_PathCanvasGetCoordFromObj(interp, (Tk_PathCanvas) path,
                    objv[first + 1], &coords[0]) != TCL_OK)
            || (Tk_PathCanvasGetCoordFromObj(interp, (Tk_PathCanvas) path,
                    objv[first + 2], &coords[1]) != TCL_OK)) {
            return TCL_ERROR;
        }
        if(objc > first + 3) {
            if(Tk_PathCanvasGetCoordFromObj(interp, (Tk_PathCanvas) path,
                    objv[first + 3], &halo) != TCL_OK) {
                return TCL_ERROR;
            }
            if(halo < 0.0) {
                Tcl_AppendResult(interp, "can't have negative halo value \"",
                    Tcl_GetString(objv[3]), "\"", NULL);
                return TCL_ERROR;
            }
        } else {
            halo = 0.0;
        }

        /*
         * Find the item at which to start the search.
         */

        startPtr = path->rootItemPtr;
        if(objc == first + 5) {
            FIRST_CANVAS_ITEM_MATCHING(objv[first + 4], searchPtrPtr,
                return TCL_ERROR);
            if(itemPtr != NULL) {
                startPtr = itemPtr;
            }
        }

        /*
         * The code below is optimized so that it can eliminate most items
         * without having to call their item-specific functions. This is done
         * by keeping a bounding box (x1, y1, x2, y2) that an item's bbox must
         * overlap if the item is to have any chance of being closer than the
         * closest so far.
         */

        itemPtr = startPtr;
        while(itemPtr && (itemPtr->state == TK_PATHSTATE_HIDDEN ||
                (itemPtr->state == TK_PATHSTATE_NULL &&
                    path->canvas_state == TK_PATHSTATE_HIDDEN))) {
            itemPtr = TkPathCanvasItemIteratorNext(itemPtr);
        }
        if(itemPtr == NULL) {
            return TCL_OK;
        }
        closestDist = (*itemPtr->typePtr->pointProc) ((Tk_PathCanvas) path,
            itemPtr, coords) - halo;
        if(closestDist < 0.0) {
            closestDist = 0.0;
        }
        while(1) {
    double newDist;

            /*
             * Update the bounding box using itemPtr, which is the new closest
             * item.
             */

            x1 = (int)(coords[0] - closestDist - halo - 1);
            y1 = (int)(coords[1] - closestDist - halo - 1);
            x2 = (int)(coords[0] + closestDist + halo + 1);
            y2 = (int)(coords[1] + closestDist + halo + 1);
            closestPtr = itemPtr;

            /*
             * Search for an item that beats the current closest one. Work
             * circularly through the canvas's item list until getting back to
             * the starting item.
             */

            while(1) {
                itemPtr = TkPathCanvasItemIteratorNext(itemPtr);
                if(itemPtr == NULL) {
                    itemPtr = path->rootItemPtr;
                }
                if(itemPtr == startPtr) {
                    DoItem(interp, closestPtr, uid);
                    return TCL_OK;
                }
                if(itemPtr->state == TK_PATHSTATE_HIDDEN ||
                    (itemPtr->state == TK_PATHSTATE_NULL &&
                        path->canvas_state == TK_PATHSTATE_HIDDEN)) {
                    continue;
                }
                if((itemPtr->x1 >= x2) || (itemPtr->x2 <= x1)
                    || (itemPtr->y1 >= y2) || (itemPtr->y2 <= y1)) {
                    continue;
                }
                newDist = (*itemPtr->typePtr->pointProc) ((Tk_PathCanvas) path,
                    itemPtr, coords) - halo;
                if(newDist < 0.0) {
                    newDist = 0.0;
                }
                if(newDist <= closestDist) {
                    closestDist = newDist;
                    break;
                }
            }
        }
        break;
    }
    case CANV_ENCLOSED:
        if(objc != first + 5) {
            Tcl_WrongNumArgs(interp, first + 1, objv, "x1 y1 x2 y2");
            return TCL_ERROR;
        }
        return FindArea(interp, path, objv + first + 1, uid, 1);
    case CANV_OVERLAPPING:
        if(objc != first + 5) {
            Tcl_WrongNumArgs(interp, first + 1, objv, "x1 y1 x2 y2");
            return TCL_ERROR;
        }
        return FindArea(interp, path, objv + first + 1, uid, 0);
    case CANV_WITHTAG:
        if(objc != first + 2) {
            Tcl_WrongNumArgs(interp, first + 1, objv, "tagOrId");
            return TCL_ERROR;
        }
        FOR_EVERY_CANVAS_ITEM_MATCHING(objv[first + 1], searchPtrPtr,
            return TCL_ERROR) {
            DoItem(interp, itemPtr, uid);
        }
    }
    return TCL_OK;
}

/*
 * FindArea --
 *
 *    This function implements area searches for the "find" and "addtag"
 *    options.
 *
 * Results:
 *    A standard Tcl return value. If newTag is NULL, then a list of ids
 *    from all the items overlapping or enclosed by the rectangle given by
 *    objc is returned in the interp's result. If newTag is NULL, then the
 *    normal the interp's result is an empty string. If an error occurs,
 *    then the interp's result will hold an error message.
 *
 * Side effects:
 *    If uid is non-NULL, then all the items overlapping or enclosed by the
 *    area in objv have that tag added to their lists of tags.
 */
static int
    FindArea(
    Tcl_Interp * interp,       /* Interpreter for error reporting and result
                                * storing. */
    TkPathCanvas * path,       /* Canvas whose items are to be searched. */
    Tcl_Obj * const *objv,     /* Array of four arguments that give the
                                * coordinates of the rectangular area to
                                * search. */
    Tk_Uid uid,                /* If non-NULL, gives new tag to set on all
                                * found items; if NULL, then ids of found
                                * items are returned in the interp's
                                * result. */
    int enclosed) {     /* 0 means overlapping or enclosed items are
                         * OK, 1 means only enclosed items are OK. */
    double rect[4], tmp;
    int x1, y1, x2, y2;
    Tk_PathItem *itemPtr;

    if((Tk_PathCanvasGetCoordFromObj(interp, (Tk_PathCanvas) path, objv[0],
                &rect[0]) != TCL_OK)
        || (Tk_PathCanvasGetCoordFromObj(interp, (Tk_PathCanvas) path, objv[1],
                &rect[1]) != TCL_OK)
        || (Tk_PathCanvasGetCoordFromObj(interp, (Tk_PathCanvas) path, objv[2],
                &rect[2]) != TCL_OK)
        || (Tk_PathCanvasGetCoordFromObj(interp, (Tk_PathCanvas) path, objv[3],
                &rect[3]) != TCL_OK)) {
        return TCL_ERROR;
    }
    if(rect[0] > rect[2]) {
        tmp = rect[0];
        rect[0] = rect[2];
        rect[2] = tmp;
    }
    if(rect[1] > rect[3]) {
        tmp = rect[1];
        rect[1] = rect[3];
        rect[3] = tmp;
    }

    /*
     * Use an integer bounding box for a quick test, to avoid calling
     * item-specific code except for items that are close.
     */

    x1 = (int)(rect[0] - 1.0);
    y1 = (int)(rect[1] - 1.0);
    x2 = (int)(rect[2] + 1.0);
    y2 = (int)(rect[3] + 1.0);
    for(itemPtr = path->rootItemPtr; itemPtr != NULL;
        itemPtr = TkPathCanvasItemIteratorNext(itemPtr)) {
        if(itemPtr->state == TK_PATHSTATE_HIDDEN
            || (itemPtr->state == TK_PATHSTATE_NULL
                && path->canvas_state == TK_PATHSTATE_HIDDEN)) {
            continue;
        }
        if((itemPtr->x1 >= x2) || (itemPtr->x2 <= x1)
            || (itemPtr->y1 >= y2) || (itemPtr->y2 <= y1)) {
            continue;
        }
        if((*itemPtr->typePtr->areaProc) ((Tk_PathCanvas) path, itemPtr, rect)
            >= enclosed) {
            DoItem(interp, itemPtr, uid);
        }
    }
    return TCL_OK;
}

/*
 * RelinkItems --
 *
 *    Move one or more items to a different place in the display order for a
 *    canvas.
 *    Only items with same parent as prevPtr will be moved. Items matching
 *    tag but with different parent will be silently ignored.
 *    If we didn't do this we would break the tree hierarchy structure
 *    which would create a mess!
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The items identified by "tag" are moved so that they are all together
 *    in the display list and immediately after prevPtr. The order of the
 *    moved items relative to each other is not changed.
 */
static int
RelinkItems(
    TkPathCanvas * path,       /* Canvas to be modified. */
    Tcl_Obj * tag,             /* Tag identifying items to be moved in the
                                * redisplay list. */
    Tk_PathItem * prevPtr,     /* Reposition the items so that they go just
                                * after this item (NULL means put at
                                * beginning of list). */
    TagSearch ** searchPtrPtr)
{              /* From CanvasWidgetCmd local vars */
Tk_PathItem *itemPtr;
Tk_PathItem *firstMovePtr, *lastMovePtr;
Tk_PathItem *parentPtr, *rootItemPtr;
int result;

    rootItemPtr = path->rootItemPtr;
    if(prevPtr == rootItemPtr) {
        return TCL_OK;
    }

    /*
     * Keep track of parentPtr for the selection of items.
     * prevPtr equal to NULL means use the root item as parent.
     * This keeps compatiblity with old canvas.
     */
    if(prevPtr != NULL) {
        parentPtr = prevPtr->parentPtr;
    } else {
        parentPtr = NULL;       /* later resolved, see below */
    }

    /*
     * Find all of the items to be moved and remove them from the list, making
     * an auxiliary list running from firstMovePtr to lastMovePtr. Record
     * their areas for redisplay.
     */
    firstMovePtr = lastMovePtr = NULL;
    FOR_EVERY_CANVAS_ITEM_MATCHING(tag, searchPtrPtr, return TCL_ERROR) {
        if(itemPtr->parentPtr == NULL) {
            continue;
        }
        if(parentPtr == NULL) {
            /* first matching item determines parent */
            parentPtr = itemPtr->parentPtr;
        } else if(itemPtr->parentPtr != parentPtr) {
            continue;
        }
        if(itemPtr == prevPtr) {
            /*
             * Item after which insertion is to occur is being moved! Switch
             * to insert after its predecessor.
             */

            prevPtr = prevPtr->prevPtr;
        }

        /*
         * Detach (splice out) item to be moved.
         */
        if(itemPtr->parentPtr->firstChildPtr == itemPtr) {
            itemPtr->parentPtr->firstChildPtr = itemPtr->nextPtr;
        }
        if(itemPtr->parentPtr->lastChildPtr == itemPtr) {
            itemPtr->parentPtr->lastChildPtr = itemPtr->prevPtr;
        }
        if(itemPtr->prevPtr != NULL) {
            itemPtr->prevPtr->nextPtr = itemPtr->nextPtr;
        }
        if(itemPtr->nextPtr != NULL) {
            itemPtr->nextPtr->prevPtr = itemPtr->prevPtr;
        }

        /*
         * Place moved item as the last item of the
         * moved linked list.
         */
        if(firstMovePtr == NULL) {
            itemPtr->prevPtr = NULL;
            itemPtr->nextPtr = NULL;
            firstMovePtr = itemPtr;
        } else {
            itemPtr->prevPtr = lastMovePtr;
            lastMovePtr->nextPtr = itemPtr;
        }
        lastMovePtr = itemPtr;
        EventuallyRedrawItemAndChildren((Tk_PathCanvas) path, itemPtr);
        path->flags |= REPICK_NEEDED;
    }

    if(firstMovePtr == NULL) {
        return TCL_OK;
    }

    /*
     * Insert the list of to-be-moved items back into the canvas's at the
     * desired position.
     */
    firstMovePtr->prevPtr = prevPtr;
    if(prevPtr != NULL) {
        if(prevPtr->nextPtr != NULL) {
            prevPtr->nextPtr->prevPtr = lastMovePtr;
        }
        lastMovePtr->nextPtr = prevPtr->nextPtr;
        prevPtr->nextPtr = firstMovePtr;
    } else {
        if(parentPtr->firstChildPtr != NULL) {
            parentPtr->firstChildPtr->prevPtr = lastMovePtr;
        }
        lastMovePtr->nextPtr = parentPtr->firstChildPtr;
        parentPtr->firstChildPtr = firstMovePtr;
    }
    if(parentPtr->lastChildPtr == prevPtr) {
        parentPtr->lastChildPtr = lastMovePtr;
    }

    return TCL_OK;
}

/*
 * CanvasBindProc --
 *
 *    This function is invoked by the Tk dispatcher to handle events
 *    associated with bindings on items.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Depends on the command invoked as part of the binding (if there was
 *    any).
 */
static void
CanvasBindProc(
    ClientData clientData,     /* Pointer to canvas structure. */
    XEvent * eventPtr)
{              /* Pointer to X event that just happened. */
TkPathCanvas *path = (TkPathCanvas *) clientData;
    if(path->win == NULL || *(path->win) == NULL)
        return;

    Tcl_Preserve((ClientData) path);

    /*
     * This code below keeps track of the current modifier state in
     * path>state. This information is used to defer repicks of the
     * current item while buttons are down.
     */

    if((eventPtr->type == ButtonPress) || (eventPtr->type == ButtonRelease)) {
int mask;

        switch (eventPtr->xbutton.button) {
        case Button1:
            mask = Button1Mask;
            break;
        case Button2:
            mask = Button2Mask;
            break;
        case Button3:
            mask = Button3Mask;
            break;
        case Button4:
            mask = Button4Mask;
            break;
        case Button5:
            mask = Button5Mask;
            break;
        default:
            mask = 0;
            break;
        }

        /*
         * For button press events, repick the current item using the button
         * state before the event, then process the event. For button release
         * events, first process the event, then repick the current item using
         * the button state *after* the event (the button has logically gone
         * up before we change the current item).
         */

        if(eventPtr->type == ButtonPress) {
            /*
             * On a button press, first repick the current item using the
             * button state before the event, the process the event.
             */

            path->state = eventPtr->xbutton.state;
            PickCurrentItem(path, eventPtr);
            path->state ^= mask;
            CanvasDoEvent(path, eventPtr);
        } else {
            /*
             * Button release: first process the event, with the button still
             * considered to be down. Then repick the current item under the
             * assumption that the button is no longer down.
             */

            path->state = eventPtr->xbutton.state;
            CanvasDoEvent(path, eventPtr);
            eventPtr->xbutton.state ^= mask;
            path->state = eventPtr->xbutton.state;
            PickCurrentItem(path, eventPtr);
            eventPtr->xbutton.state ^= mask;
        }
        goto done;
    } else if((eventPtr->type == EnterNotify)
        || (eventPtr->type == LeaveNotify)) {
        path->state = eventPtr->xcrossing.state;
        PickCurrentItem(path, eventPtr);
        goto done;
    } else if(eventPtr->type == MotionNotify) {
        path->state = eventPtr->xmotion.state;
        PickCurrentItem(path, eventPtr);
    }
    CanvasDoEvent(path, eventPtr);

  done:
    Tcl_Release((ClientData) path);
}

/*
 * PickCurrentItem --
 *
 *    Find the topmost item in a canvas that contains a given location and
 *    mark the the current item. If the current item has changed, generate a
 *    fake exit event on the old current item, a fake enter event on the new
 *    current item item and force a redraw of the two items. Canvas items
 *    that are hidden or disabled are ignored.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The current item for path may change. If it does, then the
 *    commands associated with item entry and exit could do just about
 *    anything. A binding script could delete the canvas, so callers should
 *    protect themselves with Tcl_Preserve and Tcl_Release.
 */
static void
PickCurrentItem(
    TkPathCanvas * path,       /* Canvas widget in which to select current
                                * item. */
    XEvent * eventPtr)
{              /* Event describing location of mouse cursor.
                * Must be EnterWindow, LeaveWindow,
                * ButtonRelease, or MotionNotify. */
double coords[2];
int buttonDown;
Tk_PathItem *prevItemPtr;
SearchUids *searchUids = GetStaticUids();
    if(path->win == NULL || *(path->win) == NULL)
        return;
    /*
     * Check whether or not a button is down. If so, we'll log entry and exit
     * into and out of the current item, but not entry into any other item.
     * This implements a form of grabbing equivalent to what the X server does
     * for windows.
     */

    buttonDown = path->state
        & (Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask);

    /*
     * Save information about this event in the canvas. The event in the
     * canvas is used for two purposes:
     *
     * 1. Event bindings: if the current item changes, fake events are
     *    generated to allow item-enter and item-leave bindings to trigger.
     * 2. Reselection: if the current item gets deleted, can use the saved
     *    event to find a new current item.
     *
     * Translate MotionNotify events into EnterNotify events, since that's
     * what gets reported to item handlers.
     */

    if(eventPtr != &path->pickEvent) {
        if((eventPtr->type == MotionNotify)
            || (eventPtr->type == ButtonRelease)) {
            path->pickEvent.xcrossing.type = EnterNotify;
            path->pickEvent.xcrossing.serial = eventPtr->xmotion.serial;
            path->pickEvent.xcrossing.send_event = eventPtr->xmotion.send_event;
            path->pickEvent.xcrossing.display = eventPtr->xmotion.display;
            path->pickEvent.xcrossing.window = eventPtr->xmotion.window;
            path->pickEvent.xcrossing.root = eventPtr->xmotion.root;
            path->pickEvent.xcrossing.subwindow = None;
            path->pickEvent.xcrossing.time = eventPtr->xmotion.time;
            path->pickEvent.xcrossing.x = eventPtr->xmotion.x;
            path->pickEvent.xcrossing.y = eventPtr->xmotion.y;
            path->pickEvent.xcrossing.x_root = eventPtr->xmotion.x_root;
            path->pickEvent.xcrossing.y_root = eventPtr->xmotion.y_root;
            path->pickEvent.xcrossing.mode = NotifyNormal;
            path->pickEvent.xcrossing.detail = NotifyNonlinear;
            path->pickEvent.xcrossing.same_screen =
                eventPtr->xmotion.same_screen;
            path->pickEvent.xcrossing.focus = False;
            path->pickEvent.xcrossing.state = eventPtr->xmotion.state;
        } else {
            path->pickEvent = *eventPtr;
        }
    }

    /*
     * If this is a recursive call (there's already a partially completed call
     * pending on the stack; it's in the middle of processing a Leave event
     * handler for the old current item) then just return; the pending call
     * will do everything that's needed.
     */

    if(path->flags & REPICK_IN_PROGRESS) {
        return;
    }

    /*
     * A LeaveNotify event automatically means that there's no current object,
     * so the check for closest item can be skipped.
     */

    coords[0] = path->pickEvent.xcrossing.x + path->xOrigin;
    coords[1] = path->pickEvent.xcrossing.y + path->yOrigin;
    if(path->pickEvent.type != LeaveNotify) {
        path->newCurrentPtr = CanvasFindClosest(path, coords);
    } else {
        path->newCurrentPtr = NULL;
    }

    if((path->newCurrentPtr == path->currentItemPtr)
        && !(path->flags & LEFT_GRABBED_ITEM)) {
        /*
         * Nothing to do:  the current item hasn't changed.
         */

        return;
    }

    if(!buttonDown) {
        path->flags &= ~LEFT_GRABBED_ITEM;
    }

    /*
     * Simulate a LeaveNotify event on the previous current item and an
     * EnterNotify event on the new current item. Remove the "current" tag
     * from the previous current item and place it on the new current item.
     */

    if((path->newCurrentPtr != path->currentItemPtr)
        && (path->currentItemPtr != NULL)
        && !(path->flags & LEFT_GRABBED_ITEM)) {
XEvent event;
Tk_PathItem *itemPtr = path->currentItemPtr;
Tk_PathTags *ptagsPtr;
int i;

        event = path->pickEvent;
        event.type = LeaveNotify;

        /*
         * If the event's detail happens to be NotifyInferior the binding
         * mechanism will discard the event. To be consistent, always use
         * NotifyAncestor.
         */

        event.xcrossing.detail = NotifyAncestor;
        path->flags |= REPICK_IN_PROGRESS;
        CanvasDoEvent(path, &event);
        path->flags &= ~REPICK_IN_PROGRESS;

        /*
         * The check below is needed because there could be an event handler
         * for <LeaveNotify> that deletes the current item.
         */

        if((itemPtr == path->currentItemPtr) && !buttonDown &&
            (itemPtr->pathTagsPtr != NULL)) {
            ptagsPtr = itemPtr->pathTagsPtr;
            for(i = ptagsPtr->numTags - 1; i >= 0; i--) {
                if(ptagsPtr->tagPtr[i] == searchUids->currentUid)
                    /* then */  {
                    ptagsPtr->tagPtr[i] =
                        ptagsPtr->tagPtr[ptagsPtr->numTags - 1];
                    ptagsPtr->numTags--;
                    break;
                    }
            }
        }

        /*
         * Note: during CanvasDoEvent above, it's possible that
         * path->newCurrentPtr got reset to NULL because the item was
         * deleted.
         */
    }
    if((path->newCurrentPtr != path->currentItemPtr) && buttonDown) {
        path->flags |= LEFT_GRABBED_ITEM;
        return;
    }

    /*
     * Special note: it's possible that path->newCurrentPtr ==
     * path->currentItemPtr here. This can happen, for example, if
     * LEFT_GRABBED_ITEM was set.
     */

    prevItemPtr = path->currentItemPtr;
    path->flags &= ~LEFT_GRABBED_ITEM;
    path->currentItemPtr = path->newCurrentPtr;
    if(prevItemPtr != NULL && prevItemPtr != path->currentItemPtr &&
        (prevItemPtr->redraw_flags & TK_ITEM_STATE_DEPENDANT)) {
        EventuallyRedrawItem((Tk_PathCanvas) path, prevItemPtr);
        (*prevItemPtr->typePtr->configProc) (path->interp,
            (Tk_PathCanvas) path, prevItemPtr, 0, NULL, TK_CONFIG_ARGV_ONLY);
    }
    if(path->currentItemPtr != NULL) {
XEvent event;

        DoItem(NULL, path->currentItemPtr, searchUids->currentUid);
        if((path->currentItemPtr->redraw_flags & TK_ITEM_STATE_DEPENDANT &&
                prevItemPtr != path->currentItemPtr)) {
            (*path->currentItemPtr->typePtr->configProc) (path->interp,
                (Tk_PathCanvas) path, path->currentItemPtr, 0, NULL,
                TK_CONFIG_ARGV_ONLY);
            EventuallyRedrawItem((Tk_PathCanvas) path, path->currentItemPtr);
        }
        event = path->pickEvent;
        event.type = EnterNotify;
        event.xcrossing.detail = NotifyAncestor;
        CanvasDoEvent(path, &event);
    }
}

/*
 * CanvasFindClosest --
 *
 *    Given x and y coordinates, find the topmost canvas item that is
 *    "close" to the coordinates. Canvas items that are hidden or disabled
 *    are ignored.
 *
 * Results:
 *    The return value is a pointer to the topmost item that is close to
 *    (x,y), or NULL if no item is close.
 *
 * Side effects:
 *    None.
 */
static Tk_PathItem *
CanvasFindClosest(
    TkPathCanvas * path,       /* Canvas widget to search. */
    double coords[2])
{              /* Desired x,y position in canvas, not screen,
                * coordinates.) */
    Tk_PathItem *itemPtr;
    Tk_PathItem *bestPtr;
    int x1, y1, x2, y2;

    x1 = (int)(coords[0] - path->closeEnough);
    y1 = (int)(coords[1] - path->closeEnough);
    x2 = (int)(coords[0] + path->closeEnough);
    y2 = (int)(coords[1] + path->closeEnough);

    bestPtr = NULL;
    for(itemPtr = path->rootItemPtr; itemPtr != NULL;
        itemPtr = TkPathCanvasItemIteratorNext(itemPtr)) {
        if((itemPtr->state == TK_PATHSTATE_HIDDEN) ||
            (itemPtr->state == TK_PATHSTATE_DISABLED) ||
            ((itemPtr->state == TK_PATHSTATE_NULL) &&
                ((path->canvas_state == TK_PATHSTATE_HIDDEN) ||
                    (path->canvas_state == TK_PATHSTATE_DISABLED)))) {
            continue;
        }
        if((itemPtr->x1 > x2) || (itemPtr->x2 < x1)
            || (itemPtr->y1 > y2) || (itemPtr->y2 < y1)) {
            continue;
        }
        if((*itemPtr->typePtr->pointProc) ((Tk_PathCanvas) path,
                itemPtr, coords) <= path->closeEnough) {
            bestPtr = itemPtr;
        }
    }
    return bestPtr;
}

/*
 * CanvasDoEvent --
 *
 *    This function is called to invoke binding processing for a new event
 *    that is associated with the current item for a canvas.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Depends on the bindings for the canvas. A binding script could delete
 *    the canvas, so callers should protect themselves with Tcl_Preserve and
 *    Tcl_Release.
 */
static void
CanvasDoEvent(
    TkPathCanvas * path,       /* Canvas widget in which event occurred. */
    XEvent * eventPtr)
{              /* Real or simulated X event that is to be
                * processed. */
#define NUM_STATIC 3
ClientData staticObjects[NUM_STATIC];
ClientData *objectPtr;
int numObjects, i;
int numTags;
Tk_PathItem *itemPtr;
Tk_PathTags *ptagsPtr;
TkPathTagSearchExpr *expr;
int numExprs;
SearchUids *searchUids = GetStaticUids();
    if(path->win == NULL || *(path->win) == NULL)
        return;

    itemPtr = path->currentItemPtr;
    if((eventPtr->type == KeyPress) || (eventPtr->type == KeyRelease)) {
        itemPtr = path->textInfo.focusItemPtr;
    }
    if(itemPtr == NULL) {
        return;
    }
    ptagsPtr = itemPtr->pathTagsPtr;
    numTags = ItemGetNumTags(itemPtr);

    /*
     * Set up an array with all the relevant objects for processing this
     * event. The relevant objects are:
     * (a) the event's item,
     * (b) the tags associated with the event's item,
     * (c) the expressions that are true for the event's item's tags, and
     * (d) the tag "all".
     *
     * If there are a lot of tags then malloc an array to hold all of the
     * objects.
     */

    /*
     * Flag and count all expressions that match item's tags.
     */

    numExprs = 0;
    expr = path->bindTagExprs;
    while(expr) {
        expr->index = 0;
        expr->match = TagSearchEvalExpr(expr, itemPtr);
        if(expr->match) {
            numExprs++;
        }
        expr = expr->next;
    }
    numObjects = numTags + numExprs + 2;

    if(numObjects <= NUM_STATIC) {
        objectPtr = staticObjects;
    } else {
        objectPtr = (ClientData *) ckalloc((unsigned)
            (numObjects * sizeof(ClientData)));
    }
    objectPtr[0] = (ClientData) searchUids->allUid;

    if(ptagsPtr != NULL) {
        for(i = ptagsPtr->numTags - 1; i >= 0; i--) {
            objectPtr[i + 1] = (ClientData) ptagsPtr->tagPtr[i];
        }
    }
    objectPtr[numTags + 1] = (ClientData) itemPtr;

    /*
     * Copy uids of matching expressions into object array
     */

    i = numTags + 2;
    expr = path->bindTagExprs;
    while(expr) {
        if(expr->match) {
            objectPtr[i++] = (int *)expr->uid;
        }
        expr = expr->next;
    }

    /*
     * Invoke the binding system, then free up the object array if it was
     * malloc-ed.
     */

    Tk_BindEvent(path->bindingTable, eventPtr, *(path->win),
        numObjects, objectPtr);
    if(objectPtr != staticObjects) {
        ckfree((char *)objectPtr);
    }
}

/*
 * CanvasBlinkProc --
 *
 *    This function is called as a timer handler to blink the insertion
 *    cursor off and on.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The cursor gets turned on or off, redisplay gets invoked, and this
 *    function reschedules itself.
 */

static void
CanvasBlinkProc(
    ClientData clientData)
{              /* Pointer to record describing entry. */
TkPathCanvas *path = (TkPathCanvas *) clientData;

    if(!path->textInfo.gotFocus || (path->insertOffTime == 0)) {
        return;
    }
    if(path->textInfo.cursorOn) {
        path->textInfo.cursorOn = 0;
        path->insertBlinkHandler =
            Tcl_CreateTimerHandler(path->insertOffTime, CanvasBlinkProc,
            (ClientData) path);
    } else {
        path->textInfo.cursorOn = 1;
        path->insertBlinkHandler =
            Tcl_CreateTimerHandler(path->insertOnTime, CanvasBlinkProc,
            (ClientData) path);
    }
    if(path->textInfo.focusItemPtr != NULL) {
        EventuallyRedrawItem((Tk_PathCanvas) path, path->textInfo.focusItemPtr);
    }
}

/*
 * CanvasFocusProc --
 *
 *    This function is called whenever a canvas gets or loses the input
 *    focus. It's also called whenever the window is reconfigured while it
 *    has the focus.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The cursor gets turned on or off.
 */
static void
CanvasFocusProc(
    TkPathCanvas * path,       /* Canvas that just got or lost focus. */
    int gotFocus)
{              /* 1 means window is getting focus, 0 means
                * it's losing it. */
    Tcl_DeleteTimerHandler(path->insertBlinkHandler);
    if(gotFocus) {
        path->textInfo.gotFocus = 1;
        path->textInfo.cursorOn = 1;
        if(path->insertOffTime != 0) {
            path->insertBlinkHandler =
                Tcl_CreateTimerHandler(path->insertOffTime, CanvasBlinkProc,
                (ClientData) path);
        }
    } else {
        path->textInfo.gotFocus = 0;
        path->textInfo.cursorOn = 0;
        path->insertBlinkHandler = (Tcl_TimerToken) NULL;
    }
    if(path->textInfo.focusItemPtr != NULL) {
        EventuallyRedrawItem((Tk_PathCanvas) path, path->textInfo.focusItemPtr);
    }
    if(path->highlightWidth > 0) {
        path->flags |= REDRAW_BORDERS;
        if(!(path->flags & REDRAW_PENDING)) {
            Tcl_DoWhenIdle(PathDisplay, (ClientData) path);
            path->flags |= REDRAW_PENDING;
        }
    }
}

/*
 * CanvasSelectTo --
 *
 *    Modify the selection by moving its un-anchored end. This could make
 *    the selection either larger or smaller.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The selection changes.
 */
static void
CanvasSelectTo(
    TkPathCanvas * path,       /* Information about widget. */
    Tk_PathItem * itemPtr,     /* Item that is to hold selection. */
    int index)
{              /* Index of element that is to become the
                * "other" end of the selection. */
    int oldFirst, oldLast;
    Tk_PathItem *oldSelPtr;
    if(path->win == NULL || *(path->win) == NULL)
        return;

    oldFirst = path->textInfo.selectFirst;
    oldLast = path->textInfo.selectLast;
    oldSelPtr = path->textInfo.selItemPtr;

    /*
     * Grab the selection if we don't own it already.
     */

    if(path->textInfo.selItemPtr == NULL) {
        Tk_OwnSelection(*(path->win), XA_PRIMARY, CanvasLostSelection,
            (ClientData) path);
    } else if(path->textInfo.selItemPtr != itemPtr) {
        EventuallyRedrawItem((Tk_PathCanvas) path, path->textInfo.selItemPtr);
    }
    path->textInfo.selItemPtr = itemPtr;

    if(path->textInfo.anchorItemPtr != itemPtr) {
        path->textInfo.anchorItemPtr = itemPtr;
        path->textInfo.selectAnchor = index;
    }
    if(path->textInfo.selectAnchor <= index) {
        path->textInfo.selectFirst = path->textInfo.selectAnchor;
        path->textInfo.selectLast = index;
    } else {
        path->textInfo.selectFirst = index;
        path->textInfo.selectLast = path->textInfo.selectAnchor - 1;
    }
    if((path->textInfo.selectFirst != oldFirst)
        || (path->textInfo.selectLast != oldLast)
        || (itemPtr != oldSelPtr)) {
        EventuallyRedrawItem((Tk_PathCanvas) path, itemPtr);
    }
}

/*
 * CanvasFetchSelection --
 *
 *    This function is invoked by Tk to return part or all of the selection,
 *    when the selection is in a canvas widget. This function always returns
 *    the selection as a STRING.
 *
 * Results:
 *    The return value is the number of non-NULL bytes stored at buffer.
 *    Buffer is filled (or partially filled) with a NULL-terminated string
 *    containing part or all of the selection, as given by offset and
 *    maxBytes.
 *
 * Side effects:
 *    None.
 */
static int
CanvasFetchSelection(
    ClientData clientData,     /* Information about canvas widget. */
    int offset,                /* Offset within selection of first character
                                * to be returned. */
    char *buffer,              /* Location in which to place selection. */
    int maxBytes)
{              /* Maximum number of bytes to place at buffer,
                * not including terminating NULL
                * character. */
    TkPathCanvas *path = (TkPathCanvas *) clientData;
    if(path->win == NULL || *(path->win) == NULL)
        return -1;

    if(path->textInfo.selItemPtr == NULL) {
        return -1;
    }
    if(path->textInfo.selItemPtr->typePtr->selectionProc == NULL) {
        return -1;
    }
    return (*path->textInfo.selItemPtr->typePtr->selectionProc) (
        (Tk_PathCanvas) path, path->textInfo.selItemPtr, offset,
        buffer, maxBytes);
}

/*
 * CanvasLostSelection --
 *
 *    This function is called back by Tk when the selection is grabbed away
 *    from a canvas widget.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The existing selection is unhighlighted, and the window is marked as
 *    not containing a selection.
 */
static void
CanvasLostSelection(
    ClientData clientData)
{              /* Information about entry widget. */
TkPathCanvas *path = (TkPathCanvas *) clientData;

    if(path->textInfo.selItemPtr != NULL) {
        EventuallyRedrawItem((Tk_PathCanvas) path, path->textInfo.selItemPtr);
    }
    path->textInfo.selItemPtr = NULL;
}

/*
 * GridAlign --
 *
 *    Given a coordinate and a grid spacing, this function computes the
 *    location of the nearest grid line to the coordinate.
 *
 * Results:
 *    The return value is the location of the grid line nearest to coord.
 *
 * Side effects:
 *    None.
 */
static double
GridAlign(
    double coord,              /* Coordinate to grid-align. */
    double spacing)
{              /* Spacing between grid lines. If <= 0 then no
                * alignment is done. */
    if(spacing <= 0.0) {
        return coord;
    }
    if(coord < 0) {
        return -((int)((-coord) / spacing + 0.5)) * spacing;
    }
    return ((int)(coord / spacing + 0.5)) * spacing;
}

/*
 * ScrollFractions --
 *
 *    Given the range that's visible in the window and the "100% range" for
 *    what's in the canvas, return a list of two doubles representing the
 *    scroll fractions. This function is used for both x and y scrolling.
 *
 * Results:
 *    A List Tcl_Obj with two real numbers (Double Tcl_Objs) containing the
 *    scroll fractions (between 0 and 1) corresponding to the other
 *    arguments.
 *
 * Side effects:
 *    None.
 */
static Tcl_Obj *
ScrollFractions(
    int screen1,               /* Lowest coordinate visible in the window. */
    int screen2,               /* Highest coordinate visible in the window. */
    int object1,               /* Lowest coordinate in the object. */
    int object2)
{              /* Highest coordinate in the object. */
    Tcl_Obj *buffer[2];
    double range, f1, f2;

    range = object2 - object1;
    if(range <= 0) {
        f1 = 0;
        f2 = 1.0;
    } else {
        f1 = (screen1 - object1) / range;
        if(f1 < 0) {
            f1 = 0.0;
        }
        f2 = (screen2 - object1) / range;
        if(f2 > 1.0) {
            f2 = 1.0;
        }
        if(f2 < f1) {
            f2 = f1;
        }
    }
    buffer[0] = Tcl_NewDoubleObj(f1);
    buffer[1] = Tcl_NewDoubleObj(f2);
    return Tcl_NewListObj(2, buffer);
}

/*
 * CanvasUpdateScrollbars --
 *
 *    This function is invoked whenever a canvas has changed in a way that
 *    requires scrollbars to be redisplayed (e.g. the view in the canvas has
 *    changed).
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    If there are scrollbars associated with the canvas, then their
 *    scrolling commands are invoked to cause them to redisplay. If errors
 *    occur, additional Tcl commands may be invoked to process the errors.
 */
static void
CanvasUpdateScrollbars(
    TkPathCanvas * path)
{              /* Information about canvas. */
int result;
Tcl_Interp *interp;
int xOrigin, yOrigin, inset, width, height;
int scrollX1, scrollX2, scrollY1, scrollY2;
char *xScrollCmd, *yScrollCmd;
    if(path->win == NULL || *(path->win) == NULL)
        return;

    /*
     * Save all the relevant values from the path, because it might be
     * deleted as part of either of the two calls to Tcl_VarEval below.
     */

    interp = path->interp;
    Tcl_Preserve((ClientData) interp);
    xScrollCmd = path->xScrollCmd;
    if(xScrollCmd != NULL) {
        Tcl_Preserve((ClientData) xScrollCmd);
    }
    yScrollCmd = path->yScrollCmd;
    if(yScrollCmd != NULL) {
        Tcl_Preserve((ClientData) yScrollCmd);
    }
    xOrigin = path->xOrigin;
    yOrigin = path->yOrigin;
    inset = path->inset;
    width = Tk_Width(*(path->win));
    height = Tk_Height(*(path->win));
    scrollX1 = path->scroll[0];
    scrollX2 = path->scroll[2];
    scrollY1 = path->scroll[1];
    scrollY2 = path->scroll[3];
    path->flags &= ~UPDATE_SCROLLBARS;
    if(path->xScrollCmd != NULL) {
Tcl_Obj *fractions = ScrollFractions(xOrigin + inset,
            xOrigin + width - inset, scrollX1, scrollX2);
        result = Tcl_VarEval(interp, xScrollCmd, " ", Tcl_GetString(fractions),
            NULL);
        Tcl_DecrRefCount(fractions);
        if(result != TCL_OK) {
            Tcl_BackgroundError(interp);
        }
        Tcl_ResetResult(interp);
        Tcl_Release((ClientData) xScrollCmd);
    }

    if(yScrollCmd != NULL) {
Tcl_Obj *fractions = ScrollFractions(yOrigin + inset,
            yOrigin + height - inset, scrollY1, scrollY2);
        result = Tcl_VarEval(interp, yScrollCmd, " ", Tcl_GetString(fractions),
            NULL);
        Tcl_DecrRefCount(fractions);
        if(result != TCL_OK) {
            Tcl_BackgroundError(interp);
        }
        Tcl_ResetResult(interp);
        Tcl_Release((ClientData) yScrollCmd);
    }
    Tcl_Release((ClientData) interp);
}

/*
 * CanvasSetOrigin --
 *
 *    This function is invoked to change the mapping between canvas
 *    coordinates and screen coordinates in the canvas window.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The canvas will be redisplayed to reflect the change in view. In
 *    addition, scrollbars will be updated if there are any.
 */
static void
CanvasSetOrigin(
    TkPathCanvas * path,       /* Information about canvas. */
    int xOrigin,               /* New X origin for canvas (canvas x-coord
                                * corresponding to left edge of canvas
                                * window). */
    int yOrigin)
{              /* New Y origin for canvas (canvas y-coord
                * corresponding to top edge of canvas
                * window). */
    int left, right, top, bottom, delta;
    if(path->win == NULL || *(path->win) == NULL)
        return;

    /*
     * If scroll increments have been set, round the window origin to the
     * nearest multiple of the increments. Remember, the origin is the place
     * just inside the borders, not the upper left corner.
     */

    if(path->xScrollIncrement > 0) {
        if(xOrigin >= 0) {
            xOrigin += path->xScrollIncrement / 2;
            xOrigin -= (xOrigin + path->inset)
                % path->xScrollIncrement;
        } else {
            xOrigin = (-xOrigin) + path->xScrollIncrement / 2;
            xOrigin = -(xOrigin - (xOrigin - path->inset)
                % path->xScrollIncrement);
        }
    }
    if(path->yScrollIncrement > 0) {
        if(yOrigin >= 0) {
            yOrigin += path->yScrollIncrement / 2;
            yOrigin -= (yOrigin + path->inset)
                % path->yScrollIncrement;
        } else {
            yOrigin = (-yOrigin) + path->yScrollIncrement / 2;
            yOrigin = -(yOrigin - (yOrigin - path->inset)
                % path->yScrollIncrement);
        }
    }

    /*
     * Adjust the origin if necessary to keep as much as possible of the
     * canvas in the view. The variables left, right, etc. keep track of how
     * much extra space there is on each side of the view before it will stick
     * out past the scroll region.  If one side sticks out past the edge of
     * the scroll region, adjust the view to bring that side back to the edge
     * of the scrollregion (but don't move it so much that the other side
     * sticks out now). If scroll increments are in effect, be sure to adjust
     * only by full increments.
     */

    if((path->confine) && (path->scroll[0] != 0 || path->scroll[1] != 0 ||
            path->scroll[2] != 0 || path->scroll[3] != 0)) {
        left = xOrigin + path->inset - path->scroll[0];
        right = path->scroll[2]
            - (xOrigin + Tk_Width(*(path->win)) - path->inset);
        top = yOrigin + path->inset - path->scroll[1];
        bottom = path->scroll[3]
            - (yOrigin + Tk_Height(*(path->win)) - path->inset);
        if((left < 0) && (right > 0)) {
            delta = (right > -left) ? -left : right;
            if(path->xScrollIncrement > 0) {
                delta -= delta % path->xScrollIncrement;
            }
            xOrigin += delta;
        } else if((right < 0) && (left > 0)) {
            delta = (left > -right) ? -right : left;
            if(path->xScrollIncrement > 0) {
                delta -= delta % path->xScrollIncrement;
            }
            xOrigin -= delta;
        }
        if((top < 0) && (bottom > 0)) {
            delta = (bottom > -top) ? -top : bottom;
            if(path->yScrollIncrement > 0) {
                delta -= delta % path->yScrollIncrement;
            }
            yOrigin += delta;
        } else if((bottom < 0) && (top > 0)) {
            delta = (top > -bottom) ? -bottom : top;
            if(path->yScrollIncrement > 0) {
                delta -= delta % path->yScrollIncrement;
            }
            yOrigin -= delta;
        }
    }

    if((xOrigin == path->xOrigin) && (yOrigin == path->yOrigin)) {
        return;
    }

    /*
     * Tricky point: must redisplay not only everything that's visible in the
     * window's final configuration, but also everything that was visible in
     * the initial configuration. This is needed because some item types, like
     * windows, need to know when they move off-screen so they can explicitly
     * undisplay themselves.
     */

    Tk_PathCanvasEventuallyRedraw((Tk_PathCanvas) path,
        path->xOrigin, path->yOrigin,
        path->xOrigin + Tk_Width(*(path->win)),
        path->yOrigin + Tk_Height(*(path->win)));
    path->xOrigin = xOrigin;
    path->yOrigin = yOrigin;
    path->flags |= UPDATE_SCROLLBARS;
    Tk_PathCanvasEventuallyRedraw((Tk_PathCanvas) path,
        path->xOrigin, path->yOrigin,
        path->xOrigin + Tk_Width(*(path->win)),
        path->yOrigin + Tk_Height(*(path->win)));
}

/*
 * GetStringsFromObjs --
 *
 * Results:
 *    Converts object list into string list.
 *
 * Side effects:
 *    Memory is allocated for the objv array, which must be freed using
 *    ckfree() when no longer needed.
	* @@@ TODO: this shouldn't be needed when fully objectified!
 */
static const char **
GetStringsFromObjs(
    int objc,
    Tcl_Obj * const objv[])
{
    int i;
    const char **argv;
    if(objc <= 0) {
        return NULL;
    }
    argv = (const char **)ckalloc((objc + 1) * sizeof(char *));
    for(i = 0; i < objc; i++) {
        argv[i] = Tcl_GetString(objv[i]);
    }
    argv[objc] = 0;
    return argv;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
