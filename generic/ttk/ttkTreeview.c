/*
 * Copyright © 2004, Joe English
 *
 * ttk::treeview widget implementation.
 */

#include "tkInt.h"
#include "ttkThemeInt.h"
#include "ttkWidget.h"

#ifdef _WIN32
#include "tkWinInt.h"
#elif defined(MAC_OSX_TK)
#include "tkMacOSXPrivate.h"
#endif

#define DEF_TREE_ROWS		"10"
#define DEF_TITLECOLUMNS	"0"
#define DEF_TITLEITEMS		"0"
#define DEF_STRIPED		"0"
#define DEF_COLWIDTH		"200"
#define DEF_MINWIDTH		"20"

static const Tk_Anchor DEFAULT_IMAGEANCHOR = TK_ANCHOR_W;
static const int DEFAULT_INDENT = 20;
static const int HALO		= 4;	/* heading separator */

#define STATE_CHANGED		(0x100)	/* item state option changed */

/*------------------------------------------------------------------------
 * +++ Tree items.
 *
 * INVARIANTS:
 *	item->children	==> item->children->parent == item
 *	item->lastChild	==> item->lastChild->parent == item
 *	item->next	==> item->next->parent == item->parent
 *	item->next	==> item->next->prev == item
 *	item->prev	==> item->prev->next == item
 */

typedef struct TreeItemRec TreeItem;
struct TreeItemRec {
    Tcl_HashEntry *entryPtr;	/* Back-pointer to hash table entry */
    TreeItem	*parent;	/* Parent item */
    TreeItem	*children;	/* Linked list of child items */
    TreeItem	*lastChild;	/* Last child in linked list of child items */
    TreeItem	*next;		/* Next sibling */
    TreeItem	*prev;		/* Previous sibling */

    /*
     * Options and instance data:
     */
    Ttk_State	state;
    Tcl_Obj	*idObj;
    Tcl_Obj	*textObj;
    Tcl_Obj	*imageObj;
    Tcl_Obj	*valuesObj;
    Tcl_Obj	*openObj;
    Tcl_Obj	*tagsObj;
    Tcl_Obj	*selObj;
    Tcl_Obj	*imageAnchorObj;
    int		hidden;
    int		height;	/* Height is in number of row heights */

    Ttk_TagSet  *cellTagSets;
    Tcl_Size	nTagSets;

    /*
     * Derived resources:
     */
    Ttk_TagSet	tagset;
    Ttk_ImageSpec *imagespec;
    int itemPos;		/* Counting items */
    int visiblePos;		/* Counting visible items */
    int rowPos;			/* Counting rows (visible physical space) */
};

#define ITEM_OPTION_TAGS_CHANGED	0x100
#define ITEM_OPTION_IMAGE_CHANGED	0x200

static const Tk_OptionSpec ItemOptionSpecs[] = {
    {TK_OPTION_STRING, "-text", "text", "Text",
	"", offsetof(TreeItem,textObj), TCL_INDEX_NONE,
	0,0,0 },
    {TK_OPTION_INT, "-height", "height", "Height",
	"1", TCL_INDEX_NONE, offsetof(TreeItem,height),
	0,0,0 },
    {TK_OPTION_BOOLEAN, "-hidden", "hidden", "Hidden",
	"0", TCL_INDEX_NONE, offsetof(TreeItem,hidden),
	0,0,0 },
    {TK_OPTION_STRING, "-id", "id", "ID",
	NULL, offsetof(TreeItem,idObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,READONLY_OPTION },
    {TK_OPTION_STRING, "-image", "image", "Image",
	NULL, offsetof(TreeItem,imageObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,ITEM_OPTION_IMAGE_CHANGED },
    {TK_OPTION_ANCHOR, "-imageanchor", "imageAnchor", "ImageAnchor",
	NULL, offsetof(TreeItem,imageAnchorObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0 },
    {TK_OPTION_STRING, "-values", "values", "Values",
	NULL, offsetof(TreeItem,valuesObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0 },
    {TK_OPTION_BOOLEAN, "-open", "open", "Open",
	"0", offsetof(TreeItem,openObj), TCL_INDEX_NONE,
	0,0,0 },
    {TK_OPTION_STRING, "-tags", "tags", "Tags",
	NULL, offsetof(TreeItem,tagsObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,ITEM_OPTION_TAGS_CHANGED },

    {TK_OPTION_END, 0,0,0, NULL, TCL_INDEX_NONE,TCL_INDEX_NONE, 0,0,0}
};

/* Forward declarations */
static void RemoveTag(TreeItem *, Ttk_Tag);
static void RemoveTagFromCellsAtItem(TreeItem *, Ttk_Tag);

/* + NewItem --
 *	Allocate a new, uninitialized, unlinked item
 */
static TreeItem *NewItem(void) {
    TreeItem *item = (TreeItem *)ckalloc(sizeof(*item));

    item->entryPtr = NULL;
    item->parent = item->children = item->lastChild = NULL;
    item->next = item->prev = NULL;

    item->state = 0ul;
    item->idObj = NULL;
    item->textObj = NULL;
    item->imageObj = NULL;
    item->valuesObj = NULL;
    item->openObj = NULL;
    item->tagsObj = NULL;
    item->selObj = NULL;
    item->imageAnchorObj = NULL;
    item->hidden = 0;
    item->height = 1;
    item->cellTagSets = NULL;
    item->nTagSets = 0;

    item->tagset = NULL;
    item->imagespec = NULL;

    item->itemPos = -1;
    item->visiblePos = -1;
    item->rowPos = -1;
    return item;
}

/* + FreeItem --
 *	Destroy an item
 */
static void FreeItem(TreeItem *item) {
    Tcl_Size i;
    if (item->idObj) { Tcl_DecrRefCount(item->idObj); }
    if (item->textObj) { Tcl_DecrRefCount(item->textObj); }
    if (item->imageObj) { Tcl_DecrRefCount(item->imageObj); }
    if (item->valuesObj) { Tcl_DecrRefCount(item->valuesObj); }
    if (item->openObj) { Tcl_DecrRefCount(item->openObj); }
    if (item->tagsObj) { Tcl_DecrRefCount(item->tagsObj); }
    if (item->selObj) { Tcl_DecrRefCount(item->selObj); }
    if (item->imageAnchorObj) { Tcl_DecrRefCount(item->imageAnchorObj); }

    if (item->tagset)	{ Ttk_FreeTagSet(item->tagset); }
    if (item->imagespec) { TtkFreeImageSpec(item->imagespec); }
    if (item->cellTagSets) {
	for (i = 0; i < item->nTagSets; ++i) {
	    if (item->cellTagSets[i] != NULL) {
		Ttk_FreeTagSet(item->cellTagSets[i]);
	    }
	}
	ckfree(item->cellTagSets);
    }

    ckfree(item);
}

static void FreeItemCB(void *clientData) { FreeItem((TreeItem *)clientData); }

/* + DetachItem --
 *	Unlink an item from the tree.
 */
static void DetachItem(TreeItem *item) {
    if (item->parent && item->parent->children == item) {
	item->parent->children = item->next;
    }
    if (item->prev) {
	item->prev->next = item->next;
    }
    if (item->next) {
	item->next->prev = item->prev;
    }
    if (item->parent && item->parent->lastChild == item) {
	item->parent->lastChild = item->prev;
    }
    item->next = item->prev = item->parent = NULL;
}

/* + InsertItem --
 *	Insert an item into the tree after the specified item prev.
 *
 * Preconditions:
 *	+ item is currently detached
 *	+ prev != NULL ==> prev->parent == parent.
 */
static void InsertItem(TreeItem *parent, TreeItem *prev, TreeItem *item) {
    item->parent = parent;
    item->prev = prev;
    if (prev) {
	item->next = prev->next;
	prev->next = item;
    } else {
	item->next = parent->children;
	parent->children = item;
    }
    if (item->next) {
	item->next->prev = item;
    } else {
	parent->lastChild = item;
    }
}

/* + NextPreorder --
 *	Return the next item in preorder traversal order.
 */
static TreeItem *NextPreorder(TreeItem *item) {
    if (item->children) {
	return item->children;
    }
    while (!item->next) {
	item = item->parent;
	if (!item) {
	    return 0;
	}
    }
    return item->next;
}

/*------------------------------------------------------------------------
 * +++ Display items and tag options.
 */
typedef struct {
    Tcl_Obj *textObj;		/* taken from item / data cell */
    Tcl_Obj *imageObj;		/* taken from item or tag*/
    Tcl_Obj *imageAnchorObj;	/* taken from item or tag */
    Tcl_Obj *anchorObj;		/* from column <<NOTE-ANCHOR>> */
    Tcl_Obj *backgroundObj;	/* remainder from tag */
    Tcl_Obj *stripedBgObj;
    Tcl_Obj *foregroundObj;
    Tcl_Obj *fontObj;
    Tcl_Obj *paddingObj;
} DisplayItem;

static const Tk_OptionSpec DisplayOptionSpecs[] = {
    {TK_OPTION_STRING, "-text", "text", "Text",
	NULL, offsetof(DisplayItem,textObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0 },
    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor",
	"center", offsetof(DisplayItem,anchorObj), TCL_INDEX_NONE,
	0, 0, GEOMETRY_CHANGED},	/* <<NOTE-ANCHOR>> */
    /* From here down are the tags options. The index in TagOptionSpecs
     * below should be kept in synch with this position.
     */
    {TK_OPTION_STRING, "-image", "image", "Image",
	NULL, offsetof(DisplayItem,imageObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0 },
    {TK_OPTION_ANCHOR, "-imageanchor", "imageAnchor", "ImageAnchor",
	NULL, offsetof(DisplayItem,imageAnchorObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0 },
    {TK_OPTION_COLOR, "-background", "windowColor", "WindowColor",
	NULL, offsetof(DisplayItem,backgroundObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0 },
    {TK_OPTION_COLOR, "-stripedbackground", "windowColor", "WindowColor",
	NULL, offsetof(DisplayItem,stripedBgObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0 },
    {TK_OPTION_COLOR, "-foreground", "textColor", "TextColor",
	NULL, offsetof(DisplayItem,foregroundObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0 },
    {TK_OPTION_FONT, "-font", "font", "Font",
	NULL, offsetof(DisplayItem,fontObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED },
    {TK_OPTION_STRING, "-padding", "padding", "Pad",
	NULL, offsetof(DisplayItem,paddingObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED },

    {TK_OPTION_END, 0,0,0, NULL, TCL_INDEX_NONE,TCL_INDEX_NONE, 0,0,0}
};

static const Tk_OptionSpec *TagOptionSpecs = &DisplayOptionSpecs[2];

/*------------------------------------------------------------------------
 * +++ Columns.
 *
 * There are separate option tables associated with the column record:
 * ColumnOptionSpecs is for configuring the column,
 * and HeadingOptionSpecs is for drawing headings.
 */
typedef struct {
    int		width;		/* Column width, in pixels */
    int		minWidth;	/* Minimum column width, in pixels */
    int		stretch;	/* Should column stretch while resizing? */
    int		separator;	/* Should this column have a separator? */
    Tcl_Obj	*idObj;		/* Column identifier, from -columns option */

    Tcl_Obj	*anchorObj;	/* -anchor for cell data <<NOTE-ANCHOR>> */

    /* Column heading data: */
    Tcl_Obj	*headingObj;		/* Heading label */
    Tcl_Obj	*headingImageObj;	/* Heading image */
    Tcl_Obj	*headingAnchorObj;	/* -anchor for heading label */
    Tcl_Obj	*headingCommandObj;	/* Command to execute */
    Tcl_Obj	*headingStateObj;	/* @@@ testing ... */
    Ttk_State	headingState;		/* ... */

    /* Temporary storage for cell data */
    Tcl_Obj	*data;
    int		selected;
    Ttk_TagSet	tagset;
} TreeColumn;

static void InitColumn(TreeColumn *column) {
    column->width = atoi(DEF_COLWIDTH);
    column->minWidth = atoi(DEF_MINWIDTH);
    column->stretch = 1;
    column->separator = 0;
    column->idObj = NULL;
    column->anchorObj = NULL;

    column->headingState = 0;
    column->headingObj = NULL;
    column->headingImageObj = NULL;
    column->headingAnchorObj = NULL;
    column->headingStateObj = NULL;
    column->headingCommandObj = NULL;

    column->data = 0;
    column->tagset = NULL;
}

static void FreeColumn(TreeColumn *column) {
    if (column->idObj) { Tcl_DecrRefCount(column->idObj); }
    if (column->anchorObj) { Tcl_DecrRefCount(column->anchorObj); }

    if (column->headingObj) { Tcl_DecrRefCount(column->headingObj); }
    if (column->headingImageObj) { Tcl_DecrRefCount(column->headingImageObj); }
    if (column->headingAnchorObj) { Tcl_DecrRefCount(column->headingAnchorObj); }
    if (column->headingStateObj) { Tcl_DecrRefCount(column->headingStateObj); }
    if (column->headingCommandObj) { Tcl_DecrRefCount(column->headingCommandObj); }

    /* Don't touch column->data, it's scratch storage */
}

static const Tk_OptionSpec ColumnOptionSpecs[] = {
    {TK_OPTION_INT, "-width", "width", "Width",
	DEF_COLWIDTH, TCL_INDEX_NONE, offsetof(TreeColumn,width),
	0,0,GEOMETRY_CHANGED },
    {TK_OPTION_INT, "-minwidth", "minWidth", "MinWidth",
	DEF_MINWIDTH, TCL_INDEX_NONE, offsetof(TreeColumn,minWidth),
	0,0,0 },
    {TK_OPTION_BOOLEAN, "-separator", "separator", "Separator",
	"0", TCL_INDEX_NONE, offsetof(TreeColumn,separator),
	0,0,0 },
    {TK_OPTION_BOOLEAN, "-stretch", "stretch", "Stretch",
	"1", TCL_INDEX_NONE, offsetof(TreeColumn,stretch),
	0,0,GEOMETRY_CHANGED },
    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor",
	"w", offsetof(TreeColumn,anchorObj), TCL_INDEX_NONE,	/* <<NOTE-ANCHOR>> */
	0,0,0 },
    {TK_OPTION_STRING, "-id", "id", "ID",
	NULL, offsetof(TreeColumn,idObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,READONLY_OPTION },
    {TK_OPTION_END, 0,0,0, NULL, TCL_INDEX_NONE,TCL_INDEX_NONE, 0,0,0}
};

static const Tk_OptionSpec HeadingOptionSpecs[] = {
    {TK_OPTION_STRING, "-text", "text", "Text",
	"", offsetof(TreeColumn,headingObj), TCL_INDEX_NONE,
	0,0,0 },
    {TK_OPTION_STRING, "-image", "image", "Image",
	"", offsetof(TreeColumn,headingImageObj), TCL_INDEX_NONE,
	0,0,0 },
    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor",
	"center", offsetof(TreeColumn,headingAnchorObj), TCL_INDEX_NONE,
	0,0,0 },
    {TK_OPTION_STRING, "-command", "", "",
	"", offsetof(TreeColumn,headingCommandObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0 },
    {TK_OPTION_STRING, "state", "", "",
	"", offsetof(TreeColumn,headingStateObj), TCL_INDEX_NONE,
	0,0,STATE_CHANGED },
    {TK_OPTION_END, 0,0,0, NULL, TCL_INDEX_NONE,TCL_INDEX_NONE, 0,0,0}
};

/*------------------------------------------------------------------------
 * +++ -show option:
 * TODO: Implement SHOW_BRANCHES.
 */

#define SHOW_TREE	(0x1)	/* Show tree column? */
#define SHOW_HEADINGS	(0x2)	/* Show heading row? */

#define DEFAULT_SHOW	"tree headings"

static const char *const showStrings[] = {
    "tree", "headings", NULL
};

static int GetEnumSetFromObj(
    Tcl_Interp *interp,
    Tcl_Obj *objPtr,
    const char *const table[],
    unsigned *resultPtr) {
    unsigned result = 0;
    Tcl_Size i, objc;
    Tcl_Obj **objv;

    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
	return TCL_ERROR;
    }
    for (i = 0; i < objc; ++i) {
	int index;
	if (Tcl_GetIndexFromObjStruct(interp, objv[i], table, sizeof(char *),
		"value", TCL_EXACT, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	result |= (1 << index);
    }

    *resultPtr = result;
    return TCL_OK;
}

/*------------------------------------------------------------------------
 * +++ Treeview widget record.
 *
 * Dependencies:
 *	columns, columnNames: -columns
 *	displayColumns:	-columns, -displaycolumns
 *	headingHeight: [layout]
 *	rowHeight, indent: style
 */
typedef struct {
    /* Resources acquired at initialization-time: */
    Tk_OptionTable itemOptionTable;
    Tk_OptionTable columnOptionTable;
    Tk_OptionTable headingOptionTable;
    Tk_OptionTable displayOptionTable;
    Tk_BindingTable bindingTable;
    Ttk_TagTable tagTable;

    /* Acquired in GetLayout hook: */
    Ttk_Layout itemLayout;
    Ttk_Layout cellLayout;
    Ttk_Layout headingLayout;
    Ttk_Layout rowLayout;
    Ttk_Layout separatorLayout;

    int headingHeight;		/* Space for headings */
    int rowHeight;		/* Height of each item */
    int colSeparatorWidth;	/* Width of column separator, if used (screen units) */
    int indent;			/* Horizontal offset for child items (screen units) */

    /* Tree data: */
    Tcl_HashTable items;	/* Map: item name -> item */
    int serial;			/* Next item # for autogenerated names */
    TreeItem *root;		/* Root item */

    TreeColumn column0;		/* Column options for display column #0 */
    TreeColumn *columns;	/* Array of column options for data columns */

    TreeItem *focus;		/* Current focus item */
    TreeColumn *focusCol;	/* Current focus column */
    TreeItem *selAnchor;	/* Selection anchor item */
    Tcl_Obj *selAnchorColObj;	/* Selection anchor column */

    /* Widget options: */
    Tcl_Obj *columnsObj;	/* List of symbolic column names */
    Tcl_Obj *displayColumnsObj;	/* List of columns to display */

    Tcl_Obj *heightObj;		/* height (rows) */
    Tcl_Obj *paddingObj;	/* internal padding */
    Tcl_Size nTitleColumns;	/* -titlecolumns */
    Tcl_Size nTitleItems;	/* -titleitems */
    int striped;		/* -striped option */

    Tcl_Obj *showObj;		/* -show list */
    Tcl_Obj *selectModeObj;	/* -selectmode option */
    Tcl_Obj *selectTypeObj;	/* -selecttype option */

    Scrollable xscroll;
    ScrollHandle xscrollHandle;
    Scrollable yscroll;
    ScrollHandle yscrollHandle;

    /* Derived resources: */
    Tcl_HashTable columnNames;	/* Map: column name -> column table entry */
    Tcl_Size nColumns;		/* #columns */
    Tcl_Size nDisplayColumns;	/* #display columns */
    TreeColumn **displayColumns; /* List of columns for display (incl tree) */
    int titleWidth;		/* Width of non-scrolled columns */
    int titleRows;		/* Height of non-scrolled items, in rows */
    int totalRows;		/* Height of non-hidden items, in rows */
    int rowPosNeedsUpdate;	/* Internal rowPos data needs update */
    Ttk_Box headingArea;	/* Display area for column headings */
    Ttk_Box treeArea;		/* Display area for tree */
    int slack;			/* Slack space (see Resizing section) */
    unsigned showFlags;		/* bitmask of subparts to display */
} TreePart;

typedef struct {
    WidgetCore core;
    TreePart tree;
} Treeview;

#define USER_MASK		0x0100
#define COLUMNS_CHANGED		(USER_MASK)
#define DCOLUMNS_CHANGED	(USER_MASK<<1)
#define SCROLLCMD_CHANGED	(USER_MASK<<2)
#define SHOW_CHANGED		(USER_MASK<<3)

static const char *const SelectModeStrings[] = {
	"none", "single", "browse", "extended", "multiple", NULL };
static const char *const SelectTypeStrings[] = { "item", "cell", NULL };

static const Tk_OptionSpec TreeviewOptionSpecs[] = {
    {TK_OPTION_STRING, "-columns", "columns", "Columns",
	"", offsetof(Treeview,tree.columnsObj), TCL_INDEX_NONE,
	0, 0, COLUMNS_CHANGED | GEOMETRY_CHANGED /*| READONLY_OPTION*/ },
    {TK_OPTION_STRING, "-displaycolumns","displayColumns","DisplayColumns",
	"#all", offsetof(Treeview,tree.displayColumnsObj), TCL_INDEX_NONE,
	0, 0, DCOLUMNS_CHANGED | GEOMETRY_CHANGED },
    {TK_OPTION_STRING, "-show", "show", "Show",
	DEFAULT_SHOW, offsetof(Treeview,tree.showObj), TCL_INDEX_NONE,
	0, 0, SHOW_CHANGED | GEOMETRY_CHANGED },

    {TK_OPTION_STRING_TABLE, "-selectmode", "selectMode", "SelectMode",
	"extended", offsetof(Treeview,tree.selectModeObj), TCL_INDEX_NONE,
	0, SelectModeStrings, 0 },
    {TK_OPTION_STRING_TABLE, "-selecttype", "selectType", "SelectType",
	"item", offsetof(Treeview,tree.selectTypeObj), TCL_INDEX_NONE,
	0, SelectTypeStrings, 0 },

    {TK_OPTION_PIXELS, "-height", "height", "Height",
	DEF_TREE_ROWS, offsetof(Treeview,tree.heightObj), TCL_INDEX_NONE,
	0, 0, GEOMETRY_CHANGED},
    {TK_OPTION_STRING, "-padding", "padding", "Pad",
	NULL, offsetof(Treeview,tree.paddingObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, GEOMETRY_CHANGED },
    {TK_OPTION_INT, "-titlecolumns", "titlecolumns", "Titlecolumns",
	DEF_TITLECOLUMNS, TCL_INDEX_NONE, offsetof(Treeview,tree.nTitleColumns),
	TK_OPTION_VAR(Tcl_Size), 0, GEOMETRY_CHANGED},
    {TK_OPTION_INT, "-titleitems", "titleitems", "Titleitems",
	DEF_TITLEITEMS, TCL_INDEX_NONE, offsetof(Treeview,tree.nTitleItems),
	TK_OPTION_VAR(Tcl_Size), 0, GEOMETRY_CHANGED},
    {TK_OPTION_BOOLEAN, "-striped", "striped", "Striped",
	DEF_STRIPED, TCL_INDEX_NONE, offsetof(Treeview,tree.striped),
	0, 0, GEOMETRY_CHANGED},

    {TK_OPTION_STRING, "-xscrollcommand", "xScrollCommand", "ScrollCommand",
	NULL, offsetof(Treeview, tree.xscroll.scrollCmdObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, SCROLLCMD_CHANGED},
    {TK_OPTION_STRING, "-yscrollcommand", "yScrollCommand", "ScrollCommand",
	NULL, offsetof(Treeview, tree.yscroll.scrollCmdObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, SCROLLCMD_CHANGED},

    WIDGET_TAKEFOCUS_TRUE,
    WIDGET_INHERIT_OPTIONS(ttkCoreOptionSpecs)
};

/*------------------------------------------------------------------------
 * +++ Utilities.
 */
typedef void (*HashEntryIterator)(void *hashValue);

static void foreachHashEntry(Tcl_HashTable *ht, HashEntryIterator func) {
    Tcl_HashSearch search;
    Tcl_HashEntry *entryPtr = Tcl_FirstHashEntry(ht, &search);
    while (entryPtr != NULL) {
	func(Tcl_GetHashValue(entryPtr));
	entryPtr = Tcl_NextHashEntry(&search);
    }
}

static int CellSelectionClear(Treeview *tv) {
    TreeItem *item;
    int anyChange = 0;
    for (item=tv->tree.root; item; item = NextPreorder(item)) {
	if (item->selObj != NULL) {
	    Tcl_DecrRefCount(item->selObj);
	    item->selObj = NULL;
	    anyChange = 1;
	}
    }
    return anyChange;
}

/* + unshareObj(objPtr) --
 *	Ensure that a Tcl_Obj * has refcount 1 -- either return objPtr
 *	itself,	or a duplicated copy.
 */
static Tcl_Obj *unshareObj(Tcl_Obj *objPtr) {
    if (Tcl_IsShared(objPtr)) {
	Tcl_Obj *newObj = Tcl_DuplicateObj(objPtr);
	Tcl_DecrRefCount(objPtr);
	Tcl_IncrRefCount(newObj);
	return newObj;
    }
    return objPtr;
}

/* DisplayLayout --
 *	Rebind, place, and draw a layout + object combination.
 */
static void DisplayLayout(
    Ttk_Layout layout, void *recordPtr, Ttk_State state, Ttk_Box b, Drawable d) {
    Ttk_RebindSublayout(layout, recordPtr);
    Ttk_PlaceLayout(layout, state, b);
    Ttk_DrawLayout(layout, state, d);
}

/* DisplayLayoutTree --
 *	Like DisplayLayout, but for the tree column.
 */
static void DisplayLayoutTree(
    Tk_Anchor imageAnchor, Tk_Anchor textAnchor,
    Ttk_Layout layout, void *recordPtr, Ttk_State state, Ttk_Box b, Drawable d) {
    Ttk_Element elem;
    Ttk_RebindSublayout(layout, recordPtr);

    elem = Ttk_FindElement(layout, "image");
    if (elem != NULL) {
	Ttk_AnchorElement(elem, imageAnchor);
    }
    elem = Ttk_FindElement(layout, "text");
    if (elem != NULL) {
	Ttk_AnchorElement(elem, textAnchor);
    }
    elem = Ttk_FindElement(layout, "focus");
    if (elem != NULL) {
	Ttk_AnchorElement(elem, textAnchor);
    }

    Ttk_PlaceLayout(layout, state, b);
    Ttk_DrawLayout(layout, state, d);
}

/* + GetColumn --
 *	Look up column by name or number.
 *	Returns: pointer to column table entry, NULL if not found.
 *	Leaves an error message in interp->result on error.
 */
static TreeColumn *GetColumn(
    Tcl_Interp *interp, Treeview *tv, Tcl_Obj *columnIDObj) {
    Tcl_HashEntry *entryPtr;
    Tcl_Size columnIndex;

    /* Check for named column: */
    entryPtr = Tcl_FindHashEntry(&tv->tree.columnNames, Tcl_GetString(columnIDObj));
    if (entryPtr) {
	return (TreeColumn *)Tcl_GetHashValue(entryPtr);
    }

    /* Check for index: */
    if (TkGetIntForIndex(columnIDObj, tv->tree.nColumns - 1, 1, &columnIndex) == TCL_OK) {
	if (columnIndex < 0 || columnIndex >= tv->tree.nColumns) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "Column index \"%s\" out of bounds",
		    Tcl_GetString(columnIDObj)));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "COLBOUND", (char *)NULL);
	    return NULL;
	}

	return tv->tree.columns + columnIndex;
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	"Invalid column index \"%s\"", Tcl_GetString(columnIDObj)));
    Tcl_SetErrorCode(interp, "TTK", "TREE", "COLUMN", (char *)NULL);
    return NULL;
}

/* + FindColumn --
 *	Look up column by name, number, or display index.
 */
static TreeColumn *FindColumn(
    Tcl_Interp *interp, Treeview *tv, Tcl_Obj *columnIDObj) {
    Tcl_WideInt colno;

    if (sscanf(Tcl_GetString(columnIDObj), "#%" TCL_LL_MODIFIER "d", &colno) == 1) {
	/* Display column specification, #n */
	if (colno >= 0 && colno < tv->tree.nDisplayColumns) {
	    return tv->tree.displayColumns[colno];
	} else {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"Column %s out of range", Tcl_GetString(columnIDObj)));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "COLUMN", (char *)NULL);
	    return NULL;
	}
    }

    return GetColumn(interp, tv, columnIDObj);
}

/* + FindItem --
 *	Locates the item with the specified identifier in the tree.
 *	If there is no such item, leaves an error message in interp.
 */
static TreeItem *FindItem(
    Tcl_Interp *interp, Treeview *tv, Tcl_Obj *itemNameObj) {
    const char *itemName = Tcl_GetString(itemNameObj);
    Tcl_HashEntry *entryPtr =  Tcl_FindHashEntry(&tv->tree.items, itemName);

    if (entryPtr) {
	return (TreeItem *)Tcl_GetHashValue(entryPtr);
    } else {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf("Item %s not found", itemName));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "ITEM", (char *)NULL);
	return NULL;
    }
}

enum {index_end, index_first, index_last};
static const char *const indexStrings[] = {"end", "first", "last", NULL};
Tcl_Size TreeviewCountItems(TreeItem *, int, int);

/* + FindIndex --
 *	Returns the index for value where index can be a valid index form.
 *	If index is negative, -1 is returned. For an error, -2 is returned.
 */
static Tcl_Size FindIndex(
    Tcl_Interp *interp, TreeItem *item, Tcl_Obj *indexObj) {
    int fn;
    Tcl_Size index = -1;

    if (Tcl_GetIndexFromObjStruct(NULL, indexObj, indexStrings, sizeof(char *),
	    "index", TCL_EXACT, &fn) == TCL_OK) {
	/* Index enums: first, last, end */
	if (fn == index_first) {
	    index = 0;
	} else if (fn == index_last) {
	    index = TreeviewCountItems(item, 1, 0) - 1;
	} else {
	    index = TreeviewCountItems(item, 1, 0);
	}

    } else if ((Tcl_GetSizeIntFromObj(NULL, indexObj, &index) == TCL_OK ||
	TkGetIntForIndex(indexObj, TreeviewCountItems(item, 1, 0)-1, 1, &index) == TCL_OK)) {
	/* Index number, end-n, m+n, or m-n */
	if (index < 0 || index > LONG_MAX - 4) {
	    index = -1;
	}

    } else {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "bad index \"%s\": must be first, last, end, end-n, m+n, m-n, or an index number >= 0",
	    Tcl_GetString(indexObj)));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "INDEX", (char *)NULL);
	index = -2;
    }
    return index;
}

/* + FindItemByIndex --
 *	Returns the item at index in item where index can be a valid index form.
 *	If index is invalid, result is set to error message and TCL_ERROR is returned.
 */
static int FindItemByIndex(
    Tcl_Interp *interp, TreeItem *item, Tcl_Obj *indexObj, int before, int endIsSize, TreeItem **found) {
    int fn;
    Tcl_Size index = -1;
    *found = NULL;


    if (Tcl_GetIndexFromObjStruct(NULL, indexObj, indexStrings, sizeof(char *),
	    "index", TCL_EXACT, &fn) == TCL_OK) {
	/* Index enums: first, last, end */
	if (fn == index_first) {
	    *found = item->children;
	} else {
	    *found = item->lastChild;
	}
	if (*found && before && fn != index_end) {
	    *found = (*found)->prev;
	}

    } else if (Tcl_GetSizeIntFromObj(NULL, indexObj, &index) == TCL_OK ||
	TkGetIntForIndex(indexObj, TreeviewCountItems(item, 1, 0)-1, endIsSize, &index) == TCL_OK) {
	/* Index number, end-n, m+n, or m-n */

	*found = item->children;
	if (*found) {
	    while ((*found)->next && index > 0) {
		index--;
		*found = (*found)->next;
	    }
	    if (*found && before && index <= 0) {
		*found = (*found)->prev;
	    }
	}

    } else {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "bad index \"%s\": must be first, last, end, end-n, m+n, m-n, or an index number >= 0",
	    Tcl_GetString(indexObj)));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "INDEX", (char *)NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}

/* + GetPrevItem --
 *	Return the previous visible item in widget view
 */
TreeItem *GetPrevItem(TreeItem *root, TreeItem *item, int allow_hidden, int recurse) {
    TreeItem *current = item;

    if (!current || !root) {
	return NULL;
    }

    /* Loop over prev items until we find a visible one */
    while (current != NULL) {
	if (current->prev != NULL) {
	    current = current->prev;
	    while ((recurse && current->lastChild) && (allow_hidden ||
		    (current->state & TTK_STATE_OPEN && !current->hidden))) {
		current = current->lastChild;
	    }
	} else if (current->parent != root && recurse) {
	    current = current->parent;
	} else {
	    /* No more items */
	    return NULL;
	}

	/* Exit loop if found prev visible item */
	if (!current->hidden || allow_hidden) {
	    break;
	}
    }
    return current;
}

/* + GetNextItem --
 *	Return the next visible item in widget view
 */
TreeItem *GetNextItem(TreeItem *root, TreeItem *item, int allow_hidden, int recurse) {
    TreeItem *current = item;

    if (!current || !root) {
	return NULL;
    }

    /* Loop over next items until we find a visible one */
    while (current != NULL) {
	if ((recurse && current->children) && (allow_hidden ||
		(current->state & TTK_STATE_OPEN && !current->hidden))) {
	    current = current->children;
	} else if (current->next != NULL) {
	    current = current->next;
	} else if (current->parent != root && recurse) {
	    while (current->parent != root) {
		current = current->parent;
		if (current->next != NULL) {
		   current = current->next;
		   break;
		} else if (current->parent == root) {
		    /* No more items */
		    return NULL;
		}
	    }
	} else {
	    /* No more items */
	    return NULL;
	}

	/* Exit loop if found next visible item */
	if (!current->hidden || allow_hidden) {
	    break;
	}
    }
    return current;
}

/* + GetItemListFromObj --
 *	Parse a Tcl_Obj * as a list of items.
 *	Returns a NULL-terminated array of items; result must
 *	be ckfree()d. On error, returns NULL and leaves an error
 *	message in interp.
 */
static TreeItem **GetItemListFromObj(
    Tcl_Interp *interp, Treeview *tv, Tcl_Obj *objPtr) {
    TreeItem **items;
    Tcl_Obj **elements;
    Tcl_Size i, nElements;

    if (Tcl_ListObjGetElements(interp,objPtr,&nElements,&elements) != TCL_OK) {
	return NULL;
    }

    items = (TreeItem **)ckalloc((nElements + 1)*sizeof(TreeItem*));
    for (i = 0; i < nElements; ++i) {
	items[i] = FindItem(interp, tv, elements[i]);
	if (!items[i]) {
	    ckfree(items);
	    return NULL;
	}
    }
    items[i] = NULL;
    return items;
}

/* + ItemName --
 *	Returns the item's ID.
 */
static const char *ItemName(Treeview *tv, TreeItem *item) {
    return (const char *)Tcl_GetHashKey(&tv->tree.items, item->entryPtr);
}

/*------------------------------------------------------------------------
 * +++ Column configuration.
 */

/* + TreeviewFreeColumns --
 *	Free column data.
 */
static void TreeviewFreeColumns(Treeview *tv) {
    Tcl_Size i;

    Tcl_DeleteHashTable(&tv->tree.columnNames);
    Tcl_InitHashTable(&tv->tree.columnNames, TCL_STRING_KEYS);

    if (tv->tree.columns) {
	for (i = 0; i < tv->tree.nColumns; ++i)
	    FreeColumn(tv->tree.columns + i);
	ckfree(tv->tree.columns);
	tv->tree.columns = 0;
    }
}

/* + TreeviewInitColumns --
 *	Initialize column data when -columns changes.
 *	Returns: TCL_OK or TCL_ERROR;
 */
static int TreeviewInitColumns(Tcl_Interp *interp, Treeview *tv) {
    Tcl_Obj **columns;
    Tcl_Size i, ncols;

    if (Tcl_ListObjGetElements(
	    interp, tv->tree.columnsObj, &ncols, &columns) != TCL_OK) {
	return TCL_ERROR;
    }

    /* Free old values: */
    TreeviewFreeColumns(tv);

    /* Initialize columns array and columnNames hash table: */
    tv->tree.nColumns = ncols;
    tv->tree.columns = (TreeColumn *)ckalloc(tv->tree.nColumns * sizeof(TreeColumn));

    for (i = 0; i < ncols; ++i) {
	int isNew;
	Tcl_Obj *columnName = Tcl_DuplicateObj(columns[i]);

	Tcl_HashEntry *entryPtr = Tcl_CreateHashEntry(
	    &tv->tree.columnNames, Tcl_GetString(columnName), &isNew);
	Tcl_SetHashValue(entryPtr, tv->tree.columns + i);

	InitColumn(tv->tree.columns + i);
	Tk_InitOptions(
	    interp, tv->tree.columns + i,
	    tv->tree.columnOptionTable, tv->core.tkwin);
	Tk_InitOptions(
	    interp, tv->tree.columns + i,
	    tv->tree.headingOptionTable, tv->core.tkwin);
	Tcl_IncrRefCount(columnName);
	tv->tree.columns[i].idObj = columnName;
    }

    return TCL_OK;
}

/* + TreeviewInitDisplayColumns --
 *	Initializes the 'displayColumns' array.
 *
 *	Note that displayColumns[0] is always the tree column,
 *	even when SHOW_TREE is not set.
 *
 * @@@ TODO: disallow duplicated columns
 */
static int TreeviewInitDisplayColumns(Tcl_Interp *interp, Treeview *tv) {
    Tcl_Obj **dcolumns;
    Tcl_Size index, ndcols;
    TreeColumn **displayColumns = NULL;

    if (Tcl_ListObjGetElements(interp,
	    tv->tree.displayColumnsObj, &ndcols, &dcolumns) != TCL_OK) {
	return TCL_ERROR;
    }

    if (!strcmp(Tcl_GetString(tv->tree.displayColumnsObj), "#all")) {
	ndcols = tv->tree.nColumns;
	displayColumns = (TreeColumn **)ckalloc((ndcols+1) * sizeof(TreeColumn*));
	for (index = 0; index < ndcols; ++index) {
	    displayColumns[index+1] = tv->tree.columns + index;
	}
    } else {
	displayColumns = (TreeColumn **)ckalloc((ndcols+1) * sizeof(TreeColumn*));
	for (index = 0; index < ndcols; ++index) {
	    displayColumns[index+1] = GetColumn(interp, tv, dcolumns[index]);
	    if (!displayColumns[index+1]) {
		ckfree(displayColumns);
		return TCL_ERROR;
	    }
	}
    }
    displayColumns[0] = &tv->tree.column0;

    if (tv->tree.displayColumns) {
	ckfree(tv->tree.displayColumns);
    }
    tv->tree.displayColumns = displayColumns;
    tv->tree.nDisplayColumns = ndcols + 1;

    return TCL_OK;
}

/*------------------------------------------------------------------------
 * +++ Resizing.
 *	slack invariant: TreeWidth(tree) + slack = treeArea.width
 */

#define FirstColumn(tv)  ((tv->tree.showFlags&SHOW_TREE) ? 0 : 1)

/* + TreeWidth --
 *	Compute the requested tree width from the sum of visible column widths.
 */
static int TreeWidth(Treeview *tv) {
    Tcl_Size i = FirstColumn(tv);
    int width = 0;

    tv->tree.titleWidth = 0;
    while (i < tv->tree.nDisplayColumns) {
	if (i == tv->tree.nTitleColumns) {
	    tv->tree.titleWidth = width;
	}
	width += tv->tree.displayColumns[i++]->width;
    }
    if (tv->tree.nTitleColumns >= tv->tree.nDisplayColumns) {
	tv->tree.titleWidth = width;
    }
    return width;
}

/* + RecomputeSlack --
 */
static void RecomputeSlack(Treeview *tv) {
    tv->tree.slack = tv->tree.treeArea.width - TreeWidth(tv);
}

/* + PickupSlack/DepositSlack --
 *	When resizing columns, distribute extra space to 'slack' first,
 *	and only adjust column widths if 'slack' goes to zero.
 *	That is, don't bother changing column widths if the tree
 *	is already scrolled or short.
 */
static int PickupSlack(Treeview *tv, int extra) {
    int newSlack = tv->tree.slack + extra;

    if ((newSlack < 0 && 0 <= tv->tree.slack)
	    || (newSlack > 0 && 0 >= tv->tree.slack)) {
	tv->tree.slack = 0;
	return newSlack;
    } else {
	tv->tree.slack = newSlack;
	return 0;
    }
}

static void DepositSlack(Treeview *tv, int extra) {
    tv->tree.slack += extra;
}

/* + Stretch --
 *	Adjust width of column by N pixels, down to minimum width.
 *	Returns: #pixels actually moved.
 */
static int Stretch(TreeColumn *c, int n) {
    int newWidth = n + c->width;
    if (newWidth < c->minWidth) {
	n = c->minWidth - c->width;
	c->width = c->minWidth;
    } else {
	c->width = newWidth;
    }
    return n;
}

/* + ShoveLeft --
 *	Adjust width of (stretchable) columns to the left by N pixels.
 *	Returns: leftover slack.
 */
static int ShoveLeft(Treeview *tv, Tcl_Size i, int n) {
    Tcl_Size first = FirstColumn(tv);
    while (n != 0 && i >= first) {
	TreeColumn *c = tv->tree.displayColumns[i];
	if (c->stretch) {
	    n -= Stretch(c, n);
	}
	--i;
    }
    return n;
}

/* + ShoveRight --
 *	Adjust width of (stretchable) columns to the right by N pixels.
 *	Returns: leftover slack.
 */
static int ShoveRight(Treeview *tv, Tcl_Size i, int n) {
    while (n != 0 && i < tv->tree.nDisplayColumns) {
	TreeColumn *c = tv->tree.displayColumns[i];
	if (c->stretch) {
	    n -= Stretch(c, n);
	}
	++i;
    }
    return n;
}

/* + DistributeWidth --
 *	Distribute n pixels evenly across all stretchable display columns.
 *	Returns: leftover slack.
 * Notes:
 *	The "((++w % m) < r)" term is there so that the remainder r = n % m
 *	is distributed round-robin.
 */
static int DistributeWidth(Treeview *tv, int n) {
    int w = TreeWidth(tv);
    int m = 0;
    Tcl_Size  i;
    int d, r;

    for (i = FirstColumn(tv); i < tv->tree.nDisplayColumns; ++i) {
	if (tv->tree.displayColumns[i]->stretch) {
	    ++m;
	}
    }
    if (m == 0) {
	return n;
    }

    d = n / m;
    r = n % m;
    if (r < 0) { r += m; --d; }

    for (i = FirstColumn(tv); i < tv->tree.nDisplayColumns; ++i) {
	TreeColumn *c = tv->tree.displayColumns[i];
	if (c->stretch) {
	    n -= Stretch(c, d + ((++w % m) < r));
	}
    }
    return n;
}

/* + ResizeColumns --
 *	Recompute column widths based on available width.
 *	Pick up slack first;
 *	Distribute the remainder evenly across stretchable columns;
 *	If any is still left over due to minwidth constraints, shove left.
 */
static void ResizeColumns(Treeview *tv, int newWidth) {
    int delta = newWidth - (TreeWidth(tv) + tv->tree.slack);
    DepositSlack(tv,
	ShoveLeft(tv, tv->tree.nDisplayColumns - 1,
	    DistributeWidth(tv, PickupSlack(tv, delta))));
}

/* + DragColumn --
 *	Move the separator to the right of specified column,
 *	adjusting other column widths as necessary.
 */
static void DragColumn(Treeview *tv, Tcl_Size i, int delta) {
    TreeColumn *c = tv->tree.displayColumns[i];
    int dl = delta - ShoveLeft(tv, i-1, delta - Stretch(c, delta));
    int dr = ShoveRight(tv, i+1, PickupSlack(tv, -dl));
    DepositSlack(tv, dr);
}

/*------------------------------------------------------------------------
 * +++ Cells.
 */

typedef struct {
    TreeItem *item;
    TreeColumn *column;
    Tcl_Obj *colObj;
} TreeCell;

/* + GetCellFromObj
 *	Get Row and Column from a cell ID.
 */
static int GetCellFromObj(
    Tcl_Interp *interp, Treeview *tv, Tcl_Obj *obj,
    int displayColumnOnly, int *displayColumn,
    TreeCell *cell) {
    Tcl_Size nElements;
    Tcl_Obj **elements;

    if (Tcl_ListObjGetElements(interp, obj, &nElements, &elements) != TCL_OK) {
	return TCL_ERROR;
    }
    if (nElements != 2) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"Cell id must be a list of two elements", -1));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "CELL", (char *)NULL);
	return TCL_ERROR;
    }

    /* Valid item/column in each pair? */
    cell->item = FindItem(interp, tv, elements[0]);
    if (!cell->item) {
	return TCL_ERROR;
    }
    cell->column = FindColumn(interp, tv, elements[1]);
    if (!cell->column) {
	return TCL_ERROR;
    }

    /* colObj is short lived and do not keep a reference counted */
    cell->colObj = elements[1];
    if (displayColumnOnly) {
	Tcl_Size i = FirstColumn(tv);
	while (i < tv->tree.nDisplayColumns) {
	    if (tv->tree.displayColumns[i] == cell->column) {
		break;
	    }
	    ++i;
	}
	if (i == tv->tree.nDisplayColumns) { /* specified column unviewable */
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "Cell id must be in a visible column", -1));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "CELL", (char *)NULL);
	    return TCL_ERROR;
	}
	if (displayColumn != NULL) {
	    *displayColumn = i;
	}
    }
    return TCL_OK;
}

/* + GetCellListFromObj --
 *	Parse a Tcl_Obj * as a list of cells.
 *	Returns an array of cells; result must be ckfree()d.
 *      On error, returns NULL and leaves an error
 *	message in interp.
 */
static TreeCell *GetCellListFromObj(
	Tcl_Interp *interp, Treeview *tv, Tcl_Obj *objPtr, Tcl_Size *nCells) {
    TreeCell *cells;
    TreeCell cell;
    Tcl_Obj **elements;
    Tcl_Obj *oneCell;
    Tcl_Size i, n;

    if (Tcl_ListObjGetElements(interp, objPtr, &n, &elements) != TCL_OK) {
	return NULL;
    }

    /* A two element list might be a single cell */
    if (n == 2) {
	if (GetCellFromObj(interp, tv, objPtr, 0, NULL, &cell)
		== TCL_OK) {
	    n = 1;
	    oneCell = objPtr;
	    elements = &oneCell;
	} else {
	    Tcl_ResetResult(interp);
	}
    }

    cells = (TreeCell *) ckalloc(n * sizeof(TreeCell));
    for (i = 0; i < n; ++i) {
	if (GetCellFromObj(interp, tv, elements[i], 0, NULL, &cells[i]) != TCL_OK) {
	    ckfree(cells);
	    return NULL;
	}
    }

    if (nCells) {
	*nCells = n;
    }
    return cells;
}

/*------------------------------------------------------------------------
 * +++ Event handlers.
 */

static TreeItem *IdentifyItem(Treeview *tv, int y); /*forward*/
static Tcl_Size IdentifyDisplayColumn(Treeview *tv, int x, int *x1); /*forward*/

static const unsigned long TreeviewBindEventMask =
      KeyPressMask|KeyReleaseMask
    | ButtonPressMask|ButtonReleaseMask
    | PointerMotionMask|ButtonMotionMask
    | VirtualEventMask;

static void TreeviewBindEventProc(void *clientData, XEvent *event) {
    Treeview *tv = (Treeview *)clientData;
    TreeItem *item = NULL;
    Ttk_TagSet tagset;
    int unused;
    Tcl_Size colno = TCL_INDEX_NONE;
    TreeColumn *column = NULL;

    /* Figure out where to deliver the event. */
    switch (event->type) {
	case KeyPress:
	case KeyRelease:
	case VirtualEvent:
	    item = tv->tree.focus;
	    break;
	case ButtonPress:
	case ButtonRelease:
	    item = IdentifyItem(tv, event->xbutton.y);
	    colno = IdentifyDisplayColumn(tv, event->xbutton.x, &unused);
	    break;
	case MotionNotify:
	    item = IdentifyItem(tv, event->xmotion.y);
	    colno = IdentifyDisplayColumn(tv, event->xmotion.x, &unused);
	    break;
	default:
	    break;
    }

    if (!item) {
	return;
    }

    /* ASSERT: Ttk_GetTagSetFromObj succeeds.
     * NB: must use a local copy of the tagset,
     * in case a binding script stomps on -tags.
     */
    tagset = Ttk_GetTagSetFromObj(NULL, tv->tree.tagTable, item->tagsObj);

    /* Pick up any cell tags. */
    if (colno >= 0) {
	column = tv->tree.displayColumns[colno];
	if (column == &tv->tree.column0) {
	    colno = 0;
	} else {
	    colno = column - tv->tree.columns + 1;
	}
	if (colno < item->nTagSets) {
	    if (item->cellTagSets[colno] != NULL) {
		Ttk_TagSetAddSet(tagset, item->cellTagSets[colno]);
	    }
	}
    }

    /* Fire binding: */
    Tcl_Preserve(clientData);
    Tk_BindEvent(tv->tree.bindingTable, event, tv->core.tkwin,
	    tagset->nTags, (void **)tagset->tags);
    Tcl_Release(clientData);

    Ttk_FreeTagSet(tagset);
}

/*------------------------------------------------------------------------
 * +++ Initialization and cleanup.
 */
static void TreeviewInitialize(Tcl_Interp *interp, void *recordPtr) {
    Treeview *tv = (Treeview *)recordPtr;
    int unused;

    tv->tree.itemOptionTable =
	Tk_CreateOptionTable(interp, ItemOptionSpecs);
    tv->tree.columnOptionTable =
	Tk_CreateOptionTable(interp, ColumnOptionSpecs);
    tv->tree.headingOptionTable =
	Tk_CreateOptionTable(interp, HeadingOptionSpecs);
    tv->tree.displayOptionTable =
	Tk_CreateOptionTable(interp, DisplayOptionSpecs);

    tv->tree.tagTable = Ttk_CreateTagTable(
	interp, tv->core.tkwin, TagOptionSpecs, sizeof(DisplayItem));
    tv->tree.bindingTable = Tk_CreateBindingTable(interp);
    Tk_CreateEventHandler(tv->core.tkwin,
	TreeviewBindEventMask, TreeviewBindEventProc, tv);

    tv->tree.itemLayout
	= tv->tree.cellLayout
	= tv->tree.headingLayout
	= tv->tree.rowLayout
	= tv->tree.separatorLayout
	= 0;
    tv->tree.headingHeight = tv->tree.rowHeight = 0;
    tv->tree.colSeparatorWidth = 1;
    tv->tree.indent = DEFAULT_INDENT;

    Tcl_InitHashTable(&tv->tree.columnNames, TCL_STRING_KEYS);
    tv->tree.nColumns = tv->tree.nDisplayColumns = 0;
    tv->tree.nTitleColumns = 0;
    tv->tree.nTitleItems = 0;
    tv->tree.titleWidth = 0;
    tv->tree.titleRows = 0;
    tv->tree.totalRows = 0;
    tv->tree.rowPosNeedsUpdate = 1;
    tv->tree.striped = 0;
    tv->tree.columns = NULL;
    tv->tree.displayColumns = NULL;
    tv->tree.showFlags = ~0;

    /* Create column #0 */
    InitColumn(&tv->tree.column0);
    tv->tree.column0.idObj = Tcl_NewStringObj("#0", 2);
    Tcl_IncrRefCount(tv->tree.column0.idObj);
    Tk_InitOptions(
	interp, &tv->tree.column0,
	tv->tree.columnOptionTable, tv->core.tkwin);
    Tk_InitOptions(
	interp, &tv->tree.column0,
	tv->tree.headingOptionTable, tv->core.tkwin);

    Tcl_InitHashTable(&tv->tree.items, TCL_STRING_KEYS);
    tv->tree.serial = 0;

    tv->tree.focus = NULL;
    tv->tree.focusCol = NULL;
    tv->tree.selAnchor = NULL;
    tv->tree.selAnchorColObj = NULL;

    /* Create root item "": */
    tv->tree.root = NewItem();
    Tk_InitOptions(interp, tv->tree.root,
	tv->tree.itemOptionTable, tv->core.tkwin);
    tv->tree.root->tagset = Ttk_GetTagSetFromObj(NULL, tv->tree.tagTable, NULL);
    tv->tree.root->entryPtr = Tcl_CreateHashEntry(&tv->tree.items, "", &unused);
    Tcl_SetHashValue(tv->tree.root->entryPtr, tv->tree.root);

    /* Scroll handles: */
    tv->tree.xscrollHandle = TtkCreateScrollHandle(&tv->core,&tv->tree.xscroll);
    tv->tree.yscrollHandle = TtkCreateScrollHandle(&tv->core,&tv->tree.yscroll);

    /* Size parameters: */
    tv->tree.treeArea = tv->tree.headingArea = Ttk_MakeBox(0,0,0,0);
    tv->tree.slack = 0;
}

static void TreeviewCleanup(void *recordPtr) {
    Treeview *tv = (Treeview *)recordPtr;

    Tk_DeleteEventHandler(tv->core.tkwin,
	    TreeviewBindEventMask,  TreeviewBindEventProc, tv);
    Tk_DeleteBindingTable(tv->tree.bindingTable);
    Ttk_DeleteTagTable(tv->tree.tagTable);

    if (tv->tree.itemLayout) Ttk_FreeLayout(tv->tree.itemLayout);
    if (tv->tree.cellLayout) Ttk_FreeLayout(tv->tree.cellLayout);
    if (tv->tree.headingLayout) Ttk_FreeLayout(tv->tree.headingLayout);
    if (tv->tree.rowLayout) Ttk_FreeLayout(tv->tree.rowLayout);
    if (tv->tree.separatorLayout) Ttk_FreeLayout(tv->tree.separatorLayout);

    FreeColumn(&tv->tree.column0);
    TreeviewFreeColumns(tv);

    if (tv->tree.displayColumns) {
	ckfree(tv->tree.displayColumns);
    }

    if (tv->tree.selAnchorColObj) {
	Tcl_DecrRefCount(tv->tree.selAnchorColObj);
    }

    foreachHashEntry(&tv->tree.items, FreeItemCB);
    Tcl_DeleteHashTable(&tv->tree.items);

    TtkFreeScrollHandle(tv->tree.xscrollHandle);
    TtkFreeScrollHandle(tv->tree.yscrollHandle);
}

/* + TreeviewConfigure --
 *	Configuration widget hook.
 *
 *	BUG: If user sets -columns and -displaycolumns, but -displaycolumns
 *	has an error, the widget is left in an inconsistent state.
 */
static int
TreeviewConfigure(Tcl_Interp *interp, void *recordPtr, int mask) {
    Treeview *tv = (Treeview *)recordPtr;
    unsigned showFlags = tv->tree.showFlags;

    if (mask & COLUMNS_CHANGED) {
	if (TreeviewInitColumns(interp, tv) != TCL_OK) {
	    return TCL_ERROR;
	}
	mask |= DCOLUMNS_CHANGED;
    }
    if (mask & DCOLUMNS_CHANGED) {
	if (TreeviewInitDisplayColumns(interp, tv) != TCL_OK) {
	    return TCL_ERROR;
	}
    }
    if (mask & COLUMNS_CHANGED) {
	CellSelectionClear(tv);
    }
    if (tv->tree.nTitleColumns < 0) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"\"#%" TCL_SIZE_MODIFIER "d\" is out of range",
		tv->tree.nTitleColumns));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "TITLECOLUMNS", (char *)NULL);
	return TCL_ERROR;
    }
    if (tv->tree.nTitleItems < 0) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"\"%" TCL_SIZE_MODIFIER "d\" is out of range",
		tv->tree.nTitleItems));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "TITLEITEMS", (char *)NULL);
	return TCL_ERROR;
    }
    if (mask & SCROLLCMD_CHANGED) {
	TtkScrollbarUpdateRequired(tv->tree.xscrollHandle);
	TtkScrollbarUpdateRequired(tv->tree.yscrollHandle);
    }
    if ((mask & SHOW_CHANGED) && GetEnumSetFromObj(
		interp,tv->tree.showObj,showStrings,&showFlags) != TCL_OK) {
	return TCL_ERROR;
    }

    if (TtkCoreConfigure(interp, recordPtr, mask) != TCL_OK) {
	return TCL_ERROR;
    }

    tv->tree.rowPosNeedsUpdate = 1;
    tv->tree.showFlags = showFlags;

    if (mask & (SHOW_CHANGED | DCOLUMNS_CHANGED)) {
	RecomputeSlack(tv);
    }
    return TCL_OK;
}

/* + ConfigureItem --
 *	Set item options.
 */
static int ConfigureItem(
    Tcl_Interp *interp, Treeview *tv, TreeItem *item,
    Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_SavedOptions savedOptions;
    int mask;
    Ttk_ImageSpec *newImageSpec = NULL;
    Ttk_TagSet newTagSet = NULL;

    if (Tk_SetOptions(interp, item, tv->tree.itemOptionTable, objc, objv,
	    tv->core.tkwin, &savedOptions, &mask) != TCL_OK) {
	return TCL_ERROR;
    }

    /* Make sure that -values is a valid list: */
    if (item->valuesObj) {
	Tcl_Size unused;
	if (Tcl_ListObjLength(interp, item->valuesObj, &unused) != TCL_OK) {
	    goto error;
	}
    }

    /* Check -height */
    if (item->height < 1) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"Invalid item height %d", item->height));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "HEIGHT", (char *)NULL);
	goto error;
    }

    /* Check -image. */
    if ((mask & ITEM_OPTION_IMAGE_CHANGED) && item->imageObj) {
	newImageSpec = TtkGetImageSpec(interp, tv->core.tkwin, item->imageObj);
	if (!newImageSpec) {
	    goto error;
	}
    }

    /* Check -tags. Side effect: may create new tags. */
    if (mask & ITEM_OPTION_TAGS_CHANGED) {
	newTagSet = Ttk_GetTagSetFromObj(
		interp, tv->tree.tagTable, item->tagsObj);
	if (!newTagSet) {
	    goto error;
	}
    }

    /* Keep TTK_STATE_OPEN flag in sync with item->openObj.
     * We use both a state flag and a Tcl_Obj* resource so elements
     * can access the value in either way.
     */
    if (item->openObj) {
	int isOpen;
	if (Tcl_GetBooleanFromObj(interp, item->openObj, &isOpen) != TCL_OK) {
	    goto error;
	}
	if (isOpen) {
	    item->state |= TTK_STATE_OPEN;
	} else {
	    item->state &= ~TTK_STATE_OPEN;
	}
    }

    /* All OK. */
    Tk_FreeSavedOptions(&savedOptions);
    if (mask & ITEM_OPTION_TAGS_CHANGED) {
	if (item->tagset) { Ttk_FreeTagSet(item->tagset); }
	item->tagset = newTagSet;
    }
    if (mask & ITEM_OPTION_IMAGE_CHANGED) {
	if (item->imagespec) { TtkFreeImageSpec(item->imagespec); }
	item->imagespec = newImageSpec;
    }
    tv->tree.rowPosNeedsUpdate = 1;
    TtkRedisplayWidget(&tv->core);
    return TCL_OK;

error:
    Tk_RestoreSavedOptions(&savedOptions);
    if (newTagSet) { Ttk_FreeTagSet(newTagSet); }
    if (newImageSpec) { TtkFreeImageSpec(newImageSpec); }
    return TCL_ERROR;
}

/* + ConfigureColumn --
 *	Set column options.
 */
static int ConfigureColumn(
    Tcl_Interp *interp, Treeview *tv, TreeColumn *column,
    Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_SavedOptions savedOptions;
    int mask;

    if (Tk_SetOptions(interp, column,
	    tv->tree.columnOptionTable, objc, objv, tv->core.tkwin,
	    &savedOptions,&mask) != TCL_OK) {
	return TCL_ERROR;
    }

    if (mask & READONLY_OPTION) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"Attempt to change read-only option", -1));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "READONLY", (char *)NULL);
	goto error;
    }

    /* Propagate column width changes to overall widget request width,
     * but only if the widget is currently unmapped, in order to prevent
     * geometry jumping during interactive column resize.
     */
    if (mask & GEOMETRY_CHANGED) {
	if (!Tk_IsMapped(tv->core.tkwin)) {
	    TtkResizeWidget(&tv->core);
	} else {
	    RecomputeSlack(tv);
	    ResizeColumns(tv, TreeWidth(tv));
	}
    }
    TtkRedisplayWidget(&tv->core);

    Tk_FreeSavedOptions(&savedOptions);
    return TCL_OK;

error:
    Tk_RestoreSavedOptions(&savedOptions);
    return TCL_ERROR;
}

/* + ConfigureHeading --
 *	Set heading options.
 */
static int ConfigureHeading(
    Tcl_Interp *interp, Treeview *tv, TreeColumn *column,
    Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_SavedOptions savedOptions;
    int mask;

    if (Tk_SetOptions(interp, column,
	    tv->tree.headingOptionTable, objc, objv, tv->core.tkwin,
	    &savedOptions,&mask) != TCL_OK) {
	return TCL_ERROR;
    }

    /* @@@ testing ... */
    if ((mask & STATE_CHANGED) && column->headingStateObj) {
	Ttk_StateSpec stateSpec;
	if (Ttk_GetStateSpecFromObj(
		interp, column->headingStateObj, &stateSpec) != TCL_OK) {
	    goto error;
	}
	column->headingState = Ttk_ModifyState(column->headingState,&stateSpec);
	Tcl_DecrRefCount(column->headingStateObj);
	column->headingStateObj = Ttk_NewStateSpecObj(column->headingState,0);
	Tcl_IncrRefCount(column->headingStateObj);
    }

    TtkRedisplayWidget(&tv->core);
    Tk_FreeSavedOptions(&savedOptions);
    return TCL_OK;

error:
    Tk_RestoreSavedOptions(&savedOptions);
    return TCL_ERROR;
}

/*------------------------------------------------------------------------
 * +++ Geometry routines.
 */

/* + UpdatePositionItem --
 *	Update position data for all visible items.
 */
static void UpdatePositionItem(
    Treeview *tv, TreeItem *item, int hidden,
    int *rowPos, int *itemPos, int *visiblePos) {
    TreeItem *child = item->children;
    item->itemPos = *itemPos;
    *itemPos += 1;

    if (item->hidden) {
	hidden = 1;
    }

    if (hidden) {
	item->rowPos = -1;
	item->visiblePos = -1;
    } else {
	item->rowPos = *rowPos;
	item->visiblePos = *visiblePos;
	if (*visiblePos == tv->tree.nTitleItems) {
	    tv->tree.titleRows = *rowPos;
	}

	*visiblePos += 1;
	*rowPos += item->height;
    }

    if (!(item->state & TTK_STATE_OPEN)) {
	hidden = 1;
    }
    while (child) {
	UpdatePositionItem(tv, child, hidden, rowPos, itemPos, visiblePos);
	child = child->next;
    }
}

/* + UpdatePositionTree --
 *	Update position data for all visible items.
 */
static void UpdatePositionTree(Treeview *tv) {
    /* -1 for the invisible root */
    int rowPos = -1, itemPos = -1, visiblePos = -1;
    tv->tree.titleRows = 0;
    UpdatePositionItem(tv, tv->tree.root, 0, &rowPos, &itemPos, &visiblePos);
    tv->tree.totalRows = rowPos;
    tv->tree.rowPosNeedsUpdate = 0;
}

/* + IdentifyItem --
 *	Locate the item at the specified y position, if any.
 */
static TreeItem *IdentifyItem(Treeview *tv, int y) {
    TreeItem *item;
    int rowHeight = tv->tree.rowHeight;
    int ypos = tv->tree.treeArea.y;
    int nextRow, row;
    if (y < ypos) {
	return NULL;
    }
    if (tv->tree.rowPosNeedsUpdate) {
	UpdatePositionTree(tv);
    }
    row = (y - ypos) / rowHeight;
    if (row >= tv->tree.titleRows) {
	row += tv->tree.yscroll.first;
    }
    for (item = tv->tree.root->children; item; item = NextPreorder(item)) {
	nextRow = item->rowPos + item->height;
	if (item->rowPos <= row && row < nextRow) break;
    }
    return item;
}

/* + IdentifyDisplayColumn --
 *	Returns the display column number at the specified x position,
 *	or -1 if x is outside any columns.
 */
static Tcl_Size IdentifyDisplayColumn(Treeview *tv, int x, int *x1) {
    Tcl_Size colno = FirstColumn(tv);
    int xpos = tv->tree.treeArea.x;
    int scaledHALO = round(HALO * TkScalingLevel(tv->core.tkwin));

    if (tv->tree.nTitleColumns <= colno) {
	xpos -= tv->tree.xscroll.first;
    }

    while (colno < tv->tree.nDisplayColumns) {
	TreeColumn *column = tv->tree.displayColumns[colno];
	int next_xpos = xpos + column->width;
	if (xpos <= x && x <= next_xpos + scaledHALO) {
	    *x1 = next_xpos;
	    return colno;
	}
	++colno;
	xpos = next_xpos;
	if (tv->tree.nTitleColumns == colno) {
	    xpos -= tv->tree.xscroll.first;
	}
    }

    return TCL_INDEX_NONE;
}

/* + ItemDepth -- return the depth of a tree item.
 *	The depth of an item is equal to the number of proper ancestors,
 *	not counting the root node.
 */
static int ItemDepth(TreeItem *item) {
    int depth = 0;
    while (item->parent) {
	++depth;
	item = item->parent;
    }
    return depth-1;
}

/* + DisplayRow --
 *	Returns the position row has on screen, or -1 if off-screen.
 */
static int DisplayRow(int row, Treeview *tv) {
    int visibleRows = tv->tree.treeArea.height / tv->tree.rowHeight
	    - tv->tree.titleRows;
    if (row < tv->tree.titleRows) {
	return row;
    }
    row -= tv->tree.titleRows;
    if (row < tv->tree.yscroll.first
	    || row > tv->tree.yscroll.first + visibleRows) {
	/* not viewable, or off-screen */
	return -1;
    }
    return row - tv->tree.yscroll.first + tv->tree.titleRows;
}

/* Is an item detached? The root is never detached. */
static int IsDetached(Treeview* tv, TreeItem* item) {
    return item->next == NULL && item->prev == NULL &&
	item->parent == NULL && item != tv->tree.root;
}

/* Is an item or one of its ancestors detached? */
static int IsItemOrAncestorDetached(Treeview* tv, TreeItem* item) {
    TreeItem *parent;

    for (parent = item; parent; parent = parent->parent) {
	if (IsDetached(tv, parent)) {
	    return 1;
	}
    }
    return 0;
}

/* + BoundingBox --
 *	Compute the parcel of the specified column of the specified item,
 *	(or the entire item if column is NULL)
 *	Returns: 0 if item or column is not viewable, 1 otherwise.
 */
static int BoundingBox(
    Treeview *tv,		/* treeview widget */
    TreeItem *item,		/* desired item */
    TreeColumn *column,		/* desired column */
    Ttk_Box *bbox_rtn) {	/* bounding box of item */

    int dispRow;
    Ttk_Box bbox = tv->tree.treeArea;

    /* Make sure the scroll information is current before use */
    TtkUpdateScrollInfo(tv->tree.xscrollHandle);
    TtkUpdateScrollInfo(tv->tree.yscrollHandle);

    if (tv->tree.rowPosNeedsUpdate) {
	UpdatePositionTree(tv);
    }
    dispRow = DisplayRow(item->rowPos, tv);
    if (dispRow < 0) {
	/* not viewable, or off-screen */
	return 0;
    }
    if (IsItemOrAncestorDetached(tv, item)) {
	return 0;
    }

    bbox.y += dispRow * tv->tree.rowHeight;
    bbox.height = tv->tree.rowHeight * item->height;

    bbox.x -= tv->tree.xscroll.first;
    bbox.width = TreeWidth(tv);

    if (column) {
	int xpos = 0;
	Tcl_Size i = FirstColumn(tv);
	while (i < tv->tree.nDisplayColumns) {
	    if (tv->tree.displayColumns[i] == column) {
		break;
	    }
	    xpos += tv->tree.displayColumns[i]->width;
	    ++i;
	}
	if (i == tv->tree.nDisplayColumns) { /* specified column unviewable */
	    return 0;
	}
	bbox.x += xpos;
	bbox.width = column->width;

	if (i < tv->tree.nTitleColumns) {
	    /* Unscrollable column, remove scroll shift */
	    bbox.x += tv->tree.xscroll.first;
	}

	/* Account for indentation in tree column: */
	if (column == &tv->tree.column0) {
	    int indent = tv->tree.indent * ItemDepth(item);
	    bbox.x += indent;
	    bbox.width -= indent;
	}
    }
    *bbox_rtn = bbox;
    return 1;
}

/* + IdentifyRegion --
 */

typedef enum {
    REGION_NOTHING = 0,
    REGION_HEADING,
    REGION_SEPARATOR,
    REGION_TREE,
    REGION_CELL
} TreeRegion;

static const char *const regionStrings[] = {
    "nothing", "heading", "separator", "tree", "cell", 0
};

static TreeRegion IdentifyRegion(Treeview *tv, int x, int y) {
    int x1 = 0;
    Tcl_Size colno = IdentifyDisplayColumn(tv, x, &x1);
    int scaledHALO = round(HALO * TkScalingLevel(tv->core.tkwin));

    if (Ttk_BoxContains(tv->tree.headingArea, x, y)) {
	if (colno < 0) {
	    return REGION_NOTHING;
	} else if (-scaledHALO <= x1 - x  && x1 - x <= scaledHALO) {
	    return REGION_SEPARATOR;
	} else {
	    return REGION_HEADING;
	}
    } else if (Ttk_BoxContains(tv->tree.treeArea, x, y)) {
	TreeItem *item = IdentifyItem(tv, y);
	if (item && colno > 0) {
	    return REGION_CELL;
	} else if (item) {
	    return REGION_TREE;
	}
    }
    return REGION_NOTHING;
}

/*------------------------------------------------------------------------
 * +++ Display routines.
 */

/* + GetSublayout --
 *	Utility routine; acquires a sublayout for items, cells, etc.
 */
static Ttk_Layout GetSublayout(
    Tcl_Interp *interp,
    Ttk_Theme themePtr,
    Ttk_Layout parentLayout,
    const char *layoutName,
    Tk_OptionTable optionTable,
    Ttk_Layout *layoutPtr) {
    Ttk_Layout newLayout = Ttk_CreateSublayout(
	    interp, themePtr, parentLayout, layoutName, optionTable);

    if (newLayout) {
	if (*layoutPtr) {
	    Ttk_FreeLayout(*layoutPtr);
	}
	*layoutPtr = newLayout;
    }
    return newLayout;
}

/* + TreeviewGetLayout --
 *	GetLayout() widget hook.
 */
static Ttk_Layout TreeviewGetLayout(
    Tcl_Interp *interp, Ttk_Theme themePtr, void *recordPtr) {
    Treeview *tv = (Treeview *)recordPtr;
    Ttk_Layout treeLayout = TtkWidgetGetLayout(interp, themePtr, recordPtr);
    Tcl_Obj *objPtr;
    int unused, cellHeight;
    DisplayItem displayItem;
    Ttk_Style style;

    if (!(treeLayout
     && GetSublayout(interp, themePtr, treeLayout, ".Item",
	    tv->tree.displayOptionTable, &tv->tree.itemLayout)
     && GetSublayout(interp, themePtr, treeLayout, ".Cell",
	    tv->tree.displayOptionTable, &tv->tree.cellLayout)
     && GetSublayout(interp, themePtr, treeLayout, ".Heading",
	    tv->tree.headingOptionTable, &tv->tree.headingLayout)
     && GetSublayout(interp, themePtr, treeLayout, ".Row",
	    tv->tree.displayOptionTable, &tv->tree.rowLayout)
     && GetSublayout(interp, themePtr, treeLayout, ".Separator",
	    tv->tree.displayOptionTable, &tv->tree.separatorLayout)
    )) {
	return 0;
    }

    /* Compute heading height. */
    Ttk_RebindSublayout(tv->tree.headingLayout, &tv->tree.column0);
    Ttk_LayoutSize(tv->tree.headingLayout, 0, &unused, &tv->tree.headingHeight);

    /* Get row height from style, or compute it to fit Item and Cell.
     * Pick up default font from the Treeview style. */
    style = Ttk_LayoutStyle(treeLayout);
    Ttk_TagSetDefaults(tv->tree.tagTable, style, &displayItem);

    Ttk_RebindSublayout(tv->tree.itemLayout, &displayItem);
    Ttk_LayoutSize(tv->tree.itemLayout, 0, &unused, &tv->tree.rowHeight);

    Ttk_RebindSublayout(tv->tree.cellLayout, &displayItem);
    Ttk_LayoutSize(tv->tree.cellLayout, 0, &unused, &cellHeight);

    if (cellHeight > tv->tree.rowHeight) {
	tv->tree.rowHeight = cellHeight;
    }

    if ((objPtr = Ttk_QueryOption(treeLayout, "-rowheight", 0))) {
	(void)Tk_GetPixelsFromObj(NULL, tv->core.tkwin, objPtr, &tv->tree.rowHeight);
    }
    if (tv->tree.rowHeight < 1) {
	tv->tree.rowHeight = 1;
    }

    if ((objPtr = Ttk_QueryOption(treeLayout, "-columnseparatorwidth", 0))) {
	(void)Tk_GetPixelsFromObj(NULL, tv->core.tkwin, objPtr, &tv->tree.colSeparatorWidth);
    }

    /* Get item indent from style: */
    tv->tree.indent = DEFAULT_INDENT;
    if ((objPtr = Ttk_QueryOption(treeLayout, "-indent", 0))) {
	(void)Tk_GetPixelsFromObj(NULL, tv->core.tkwin, objPtr, &tv->tree.indent);
    }

    return treeLayout;
}

/* + TreeviewDoLayout --
 *	DoLayout() widget hook.  Computes widget layout.
 *
 * Side effects:
 *	Computes headingArea and treeArea.
 *	Computes subtree height.
 *	Invokes scroll callbacks.
 */
static void TreeviewDoLayout(void *clientData) {
    Treeview *tv = (Treeview *)clientData;
    int visibleRows;
    int first, last, total;

    Ttk_PlaceLayout(tv->core.layout,tv->core.state,Ttk_WinBox(tv->core.tkwin));
    tv->tree.treeArea = Ttk_ClientRegion(tv->core.layout, "treearea");

    ResizeColumns(tv, tv->tree.treeArea.width);

    first = tv->tree.xscroll.first;
    last = first + tv->tree.treeArea.width - tv->tree.titleWidth;
    total = TreeWidth(tv) - tv->tree.titleWidth;
    TtkScrolled(tv->tree.xscrollHandle, first, last, total);

    if (tv->tree.showFlags & SHOW_HEADINGS) {
	tv->tree.headingArea = Ttk_PackBox(
	    &tv->tree.treeArea, 1, tv->tree.headingHeight, TTK_SIDE_TOP);
    } else {
	tv->tree.headingArea = Ttk_MakeBox(0,0,0,0);
    }

    visibleRows = tv->tree.treeArea.height / tv->tree.rowHeight;
    tv->tree.root->state |= TTK_STATE_OPEN;
    UpdatePositionTree(tv);
    first = tv->tree.yscroll.first;
    last = tv->tree.yscroll.first + visibleRows - tv->tree.titleRows;
    total = tv->tree.totalRows - tv->tree.titleRows;
    TtkScrolled(tv->tree.yscrollHandle, first, last, total);
}

/* + TreeviewSize --
 *	SizeProc() widget hook.  Size is determined by
 *	-height option and column widths.
 */
static int TreeviewSize(void *clientData, int *widthPtr, int *heightPtr) {
    Treeview *tv = (Treeview *)clientData;
    int nRows, padHeight, padWidth;

    Ttk_LayoutSize(tv->core.layout, tv->core.state, &padWidth, &padHeight);
    Tcl_GetIntFromObj(NULL, tv->tree.heightObj, &nRows);

    *widthPtr = padWidth + TreeWidth(tv);
    *heightPtr = padHeight + tv->tree.rowHeight * nRows;

    if (tv->tree.showFlags & SHOW_HEADINGS) {
	*heightPtr += tv->tree.headingHeight;
    }

    return 1;
}

/* + ItemState --
 *	Returns the state of the specified item, based
 *	on widget state, item state, and other information.
 */
static Ttk_State ItemState(Treeview *tv, TreeItem *item) {
    Ttk_State state = tv->core.state | item->state;
    if (!item->children) {
	state |= TTK_STATE_LEAF;
    }
    if (item != tv->tree.focus || tv->tree.focusCol != NULL) {
	state &= ~TTK_STATE_FOCUS;
    }
    return state;
}

/* + DrawHeadings --
 *	Draw tree headings.
 */
static void DrawHeadings(Treeview *tv, Drawable d) {
    int x0 = tv->tree.headingArea.x - tv->tree.xscroll.first;
    const int y0 = tv->tree.headingArea.y;
    const int h0 = tv->tree.headingArea.height;
    Tcl_Size i = FirstColumn(tv);
    int x = 0;

    if (tv->tree.nTitleColumns > i) {
	x = tv->tree.titleWidth;
	i = tv->tree.nTitleColumns;
    }

    while (i < tv->tree.nDisplayColumns) {
	TreeColumn *column = tv->tree.displayColumns[i];
	Ttk_Box parcel = Ttk_MakeBox(x0+x, y0, column->width, h0);
	if (x0+x+column->width > tv->tree.titleWidth) {
	    DisplayLayout(tv->tree.headingLayout,
		    column, column->headingState, parcel, d);
	}
	x += column->width;
	++i;
    }

    x0 = tv->tree.headingArea.x;
    i = FirstColumn(tv);
    x = 0;
    while ((i < tv->tree.nTitleColumns) && (i < tv->tree.nDisplayColumns)) {
	TreeColumn *column = tv->tree.displayColumns[i];
	Ttk_Box parcel = Ttk_MakeBox(x0+x, y0, column->width, h0);
	DisplayLayout(tv->tree.headingLayout,
	    column, column->headingState, parcel, d);
	x += column->width;
	++i;
    }
}

/* + DrawSeparators --
 *	Draw separators between columns
 */
static void DrawSeparators(Treeview *tv, Drawable d) {
    const int y0 = tv->tree.treeArea.y;
    const int h0 = tv->tree.treeArea.height;
    DisplayItem displayItem;
    Ttk_Style style = Ttk_LayoutStyle(tv->tree.separatorLayout);
    int x = tv->tree.treeArea.x;
    Tcl_Size i;

    Ttk_TagSetDefaults(tv->tree.tagTable, style, &displayItem);

    for (i = FirstColumn(tv); i < tv->tree.nDisplayColumns; ++i) {
	TreeColumn *column = tv->tree.displayColumns[i];
	Ttk_Box parcel;
	int xDraw = x + column->width;
	x += column->width;

	if (!column->separator) continue;

	if (i >= tv->tree.nTitleColumns) {
	    xDraw -= tv->tree.xscroll.first;
	    if (xDraw < tv->tree.titleWidth) continue;
	}

	parcel = Ttk_MakeBox(xDraw - (tv->tree.colSeparatorWidth+1)/2, y0,
		tv->tree.colSeparatorWidth, h0);
	DisplayLayout(tv->tree.separatorLayout, &displayItem, 0, parcel, d);
    }
}

/* + OverrideStriped --
 *	Each level of settings might add stripedbackground, and it should
 *	override background if this is indeed on a striped item.
 *	By copying it between each level, and NULL-ing stripedBgObj,
 *	it can be detected if the next level overrides it.
 */
 static void OverrideStriped(
    Treeview *tv, TreeItem *item, DisplayItem *displayItem) {
    int striped = item->visiblePos % 2 && tv->tree.striped;
    if (striped && displayItem->stripedBgObj) {
	displayItem->backgroundObj = displayItem->stripedBgObj;
	displayItem->stripedBgObj = NULL;
    }
}

/* + PrepareItem --
 *	Fill in a displayItem record.
 */
static void PrepareItem(
    Treeview *tv, TreeItem *item, DisplayItem *displayItem, Ttk_State state) {
    Ttk_Style style = Ttk_LayoutStyle(tv->core.layout);

    Ttk_TagSetDefaults(tv->tree.tagTable, style, displayItem);
    OverrideStriped(tv, item, displayItem);
    Ttk_TagSetValues(tv->tree.tagTable, item->tagset, displayItem);
    OverrideStriped(tv, item, displayItem);
    Ttk_TagSetApplyStyle(tv->tree.tagTable, style, state, displayItem);
}

/* Fill in data from item to temporary storage in columns. */
static void PrepareCells(
   Treeview *tv, TreeItem *item) {
    Tcl_Size i, nValues = 0;
    Tcl_Obj **values = NULL;
    TreeColumn *column;

    if (item->valuesObj) {
	Tcl_ListObjGetElements(NULL, item->valuesObj, &nValues, &values);
    }
    for (i = 0; i < tv->tree.nColumns; ++i) {
	tv->tree.columns[i].data = (i < nValues) ? values[i] : 0;
	tv->tree.columns[i].selected = 0;
	tv->tree.columns[i].tagset = NULL;
    }
    tv->tree.column0.data = NULL;
    tv->tree.column0.selected = 0;
    tv->tree.column0.tagset = NULL;

    if (item->selObj != NULL) {
	Tcl_ListObjGetElements(NULL, item->selObj, &nValues, &values);
	for (i = 0; i < nValues; ++i) {
	    column = FindColumn(NULL, tv, values[i]);
	    /* Just in case. It should not be possible for column to be NULL */
	    if (column != NULL) {
		column->selected = 1;
	    }
	}
    }
    if (item->nTagSets > 0) {
	tv->tree.column0.tagset = item->cellTagSets[0];
    }
    for (i = 1; i < item->nTagSets && i <= tv->tree.nColumns; ++i) {
	tv->tree.columns[i-1].tagset = item->cellTagSets[i];
    }
}

/* + DrawCells --
 *	Draw data cells for specified item.
 */
static void DrawCells(
    Treeview *tv, TreeItem *item,
    DisplayItem *displayItem, DisplayItem *displayItemSel,
    Drawable d, int x, int y, int title) {
    Ttk_Layout layout = tv->tree.cellLayout;
    Ttk_Style style = Ttk_LayoutStyle(tv->core.layout);
    Ttk_State state = ItemState(tv, item);
    short horizPad = round(4 * TkScalingLevel(tv->core.tkwin));
    Ttk_Padding cellPadding = {horizPad, 0, horizPad, 0};
    DisplayItem displayItemLocal;
    DisplayItem displayItemCell, displayItemCellSel;
    int rowHeight = tv->tree.rowHeight * item->height;
    int xPad = 0, defaultPadding = 1;
    Tcl_Size i;

    /* Adjust if the tree column has a separator */
    if (tv->tree.showFlags & SHOW_TREE && tv->tree.column0.separator) {
	xPad = tv->tree.colSeparatorWidth/2;
    }

    /* Make sure that the cells won't overlap the border's bottom edge */
    if (y + rowHeight > tv->tree.treeArea.y + tv->tree.treeArea.height) {
	rowHeight = tv->tree.treeArea.y + tv->tree.treeArea.height - y;
    }

    /* An Item's image should not propagate to a Cell.
       A Cell's image can only be set by cell tags. */
    displayItemCell = *displayItem;
    displayItemCellSel = *displayItemSel;
    displayItemCell.imageObj = NULL;
    displayItemCellSel.imageObj = NULL;
    displayItemCell.imageAnchorObj = NULL;
    displayItemCellSel.imageAnchorObj = NULL;

    /* If explicit padding was asked for, skip default. */
    if (Ttk_QueryStyle(Ttk_LayoutStyle(tv->tree.cellLayout), &displayItemCell,
		    tv->tree.displayOptionTable, "-padding", state) != NULL) {
	defaultPadding = 0;
    }

    for (i = 1; i < tv->tree.nDisplayColumns; ++i) {
	TreeColumn *column = tv->tree.displayColumns[i];
	int parcelX = x + xPad;
	int parcelWidth = column->separator ?
		column->width - tv->tree.colSeparatorWidth : column->width;
	Ttk_Box parcel = Ttk_MakeBox(parcelX, y, parcelWidth, rowHeight);
	DisplayItem *displayItemUsed = &displayItemCell;
	Ttk_State stateCell = state;
	Tk_Anchor textAnchor, imageAnchor;
	xPad = column->separator ? tv->tree.colSeparatorWidth/2 : 0;

	x += column->width;
	if (title  && i >= tv->tree.nTitleColumns) break;
	if (!title && i <  tv->tree.nTitleColumns) continue;
	if (!title && x <  tv->tree.titleWidth) continue;

	if (column->selected) {
	    displayItemUsed = &displayItemCellSel;
	    stateCell |= TTK_STATE_SELECTED;
	}

	if (column->tagset) {
	    displayItemLocal = *displayItemUsed;
	    displayItemUsed = &displayItemLocal;
	    Ttk_TagSetValues(tv->tree.tagTable, column->tagset,
		    displayItemUsed);
	    OverrideStriped(tv, item, displayItemUsed);
	    Ttk_TagSetApplyStyle(tv->tree.tagTable, style, stateCell,
		    displayItemUsed);
	}

	displayItemUsed->textObj = column->data;
	displayItemUsed->anchorObj = column->anchorObj;/* <<NOTE-ANCHOR>> */
	Tk_GetAnchorFromObj(NULL, column->anchorObj, &textAnchor);

	imageAnchor = DEFAULT_IMAGEANCHOR;
	if (displayItemUsed->imageAnchorObj) {
	    Tk_GetAnchorFromObj(NULL, displayItemUsed->imageAnchorObj,
		    &imageAnchor);
	}
	/* displayItem was used to draw the full item backgound.
	   Redraw cell background if needed. */
	if (displayItemUsed != &displayItemCell) {
	    DisplayLayout(tv->tree.rowLayout, displayItemUsed, stateCell,
		    parcel, d);
	}

	if (defaultPadding && displayItemUsed->paddingObj == NULL) {
	    /* If no explicit padding was asked for, add some default. */
	    parcel = Ttk_PadBox(parcel, cellPadding);
	}

	DisplayLayoutTree(imageAnchor, textAnchor,
		layout, displayItemUsed, state, parcel, d);
    }
}

/* + DrawItem --
 *	Draw an item (row background, tree label, and cells).
 */
static void DrawItem(
    Treeview *tv, TreeItem *item, Drawable d, int depth) {
    Ttk_Style style = Ttk_LayoutStyle(tv->core.layout);
    Ttk_State state = ItemState(tv, item);
    DisplayItem displayItem, displayItemSel, displayItemLocal;
    int x, y, h, xTitle, dispRow, rowHeight;

    dispRow = DisplayRow(item->rowPos, tv);
    h = tv->tree.rowHeight * dispRow;
    if (h >= tv->tree.treeArea.height) {
	/* The item is outside the visible area */
	return;
    }

    rowHeight = tv->tree.rowHeight * item->height;
    x = tv->tree.treeArea.x - tv->tree.xscroll.first;
    xTitle = tv->tree.treeArea.x;
    y = tv->tree.treeArea.y + h;

    /* Make sure that the item won't overlap the border's bottom edge: */
    if (y + rowHeight > tv->tree.treeArea.y + tv->tree.treeArea.height) {
	rowHeight = tv->tree.treeArea.y + tv->tree.treeArea.height - y;
    }

    PrepareItem(tv, item, &displayItem, state);
    PrepareItem(tv, item, &displayItemSel, state | TTK_STATE_SELECTED);

    /* Draw row background: */
    {
	Ttk_Box rowBox = Ttk_MakeBox(tv->tree.treeArea.x, y,
				     TreeWidth(tv), rowHeight);
	DisplayLayout(tv->tree.rowLayout, &displayItem, state, rowBox, d);
    }

    /* Make room for tree label: */
    if (tv->tree.showFlags & SHOW_TREE) {
	x += tv->tree.column0.width;
    }

    /* Draw data cells: */
    PrepareCells(tv, item);
    DrawCells(tv, item, &displayItem, &displayItemSel, d, x, y, 0);

    /* Draw row background for non-scrolled area: */
    if (tv->tree.nTitleColumns >= 1) {
	Ttk_Box rowBox = Ttk_MakeBox(tv->tree.treeArea.x, y,
		tv->tree.titleWidth, rowHeight);
	DisplayLayout(tv->tree.rowLayout, &displayItem, state, rowBox, d);
    }

    /* Draw tree label: */
    x = tv->tree.treeArea.x - tv->tree.xscroll.first;
    if (tv->tree.showFlags & SHOW_TREE) {
	TreeColumn *column = &tv->tree.column0;
	int indent = depth * tv->tree.indent;
	int colwidth = tv->tree.column0.width -
		(tv->tree.column0.separator ? tv->tree.colSeparatorWidth/2 : 0);
	int xTree = tv->tree.nTitleColumns >= 1 ? xTitle : x;
	Ttk_Box parcel = Ttk_MakeBox(xTree, y, colwidth, rowHeight);
	DisplayItem *displayItemUsed = &displayItem;
	Ttk_State stateCell = state;
	Tk_Anchor textAnchor, imageAnchor = DEFAULT_IMAGEANCHOR;
	Ttk_Padding cellPadding = {(short)indent, 0, 0, 0};

	if (column->selected) {
	    displayItemUsed = &displayItemSel;
	    stateCell |= TTK_STATE_SELECTED;
	}

	if (column->tagset) {
	    displayItemLocal = *displayItemUsed;
	    displayItemUsed = &displayItemLocal;
	    Ttk_TagSetValues(tv->tree.tagTable, column->tagset,
		    displayItemUsed);
	    OverrideStriped(tv, item, displayItemUsed);
	    Ttk_TagSetApplyStyle(tv->tree.tagTable, style, stateCell,
		    displayItemUsed);
	}

	displayItem.anchorObj = tv->tree.column0.anchorObj;
	Tk_GetAnchorFromObj(NULL, column->anchorObj, &textAnchor);
	displayItemUsed->textObj = item->textObj;
	/* Item's image can be null, and may come from the tag */
	if (item->imageObj) {
	    displayItemUsed->imageObj = item->imageObj;
	}
	if (item->imageAnchorObj) {
	    displayItemUsed->imageAnchorObj = item->imageAnchorObj;
	}
	if (displayItemUsed->imageAnchorObj) {
	    Tk_GetAnchorFromObj(NULL, displayItemUsed->imageAnchorObj,
		    &imageAnchor);
	}

	if (displayItemUsed != &displayItem) {
	    DisplayLayout(tv->tree.rowLayout, displayItemUsed, stateCell,
		    parcel, d);
	}

	parcel = Ttk_PadBox(parcel, cellPadding);
	DisplayLayoutTree(imageAnchor, textAnchor,
		tv->tree.itemLayout, displayItemUsed, state, parcel, d);
	xTitle += colwidth;
    }

    /* Draw non-scrolled data cells: */
    if (tv->tree.nTitleColumns > 1) {
	DrawCells(tv, item, &displayItem, &displayItemSel, d, xTitle, y, 1);
    }
}

/* + DrawSubtree --
 *	Draw an item and all of its (viewable) descendants.
 */

static void DrawForest(	/* forward */
    Treeview *tv, TreeItem *item, Drawable d, int depth);

static void DrawSubtree(
    Treeview *tv, TreeItem *item, Drawable d, int depth) {
    int dispRow = DisplayRow(item->rowPos, tv);
    if (dispRow >= 0) {
	DrawItem(tv, item, d, depth);
    }

    if (item->state & TTK_STATE_OPEN) {
	DrawForest(tv, item->children, d, depth + 1);
    }
}

/* + DrawForest --
 *	Draw a sequence of items and their visible descendants.
 */
static void DrawForest(
    Treeview *tv, TreeItem *item, Drawable d, int depth) {
    while (item) {
	DrawSubtree(tv, item, d, depth);
	item = item->next;
    }
}

/* + DrawTreeArea --
 *     Draw the tree area including the headings, if any
 */
static void DrawTreeArea(Treeview *tv, Drawable d) {
    if (tv->tree.showFlags & SHOW_HEADINGS) {
	DrawHeadings(tv, d);
    }
    DrawForest(tv, tv->tree.root->children, d, 0);
    DrawSeparators(tv, d);
}

/* + TreeviewDisplay --
 *	Display() widget hook.  Draw the widget contents.
 */
static void TreeviewDisplay(void *clientData, Drawable d) {
    Treeview *tv = (Treeview *)clientData;
    Tk_Window tkwin = tv->core.tkwin;
    int width, height, winWidth, winHeight;

    /* Draw the general layout of the treeview widget */
    Ttk_DrawLayout(tv->core.layout, tv->core.state, d);

    /* When the tree area does not fit in the available space, there is a
     * risk that it will be drawn over other areas of the layout. */
    winWidth = Tk_Width(tkwin);
    winHeight = Tk_Height(tkwin);
    width = tv->tree.treeArea.width;
    height = tv->tree.headingArea.height + tv->tree.treeArea.height;

    if ((width == winWidth && height == winHeight)
      || (tv->tree.treeArea.height % tv->tree.rowHeight == 0
	&& TreeWidth(tv) <= width)) {
	/* No protection is needed; either the tree area fills the entire
	 * widget, or everything fits within the available area. */
	DrawTreeArea(tv, d);
    } else {
	/* The tree area needs to be clipped */

	int x, y;

	x = tv->tree.treeArea.x;
	if (tv->tree.showFlags & SHOW_HEADINGS) {
	    y = tv->tree.headingArea.y;
	} else {
	    y = tv->tree.treeArea.y;
	}

#ifndef TK_NO_DOUBLE_BUFFERING
	Drawable p;
	XGCValues gcValues;
	GC gc;

	/* Create a temporary helper drawable */
	p = Tk_GetPixmap(Tk_Display(tkwin), Tk_WindowId(tkwin),
	  winWidth, winHeight, Tk_Depth(tkwin));

	/* Get a graphics context for copying the drawable content */
	gcValues.function = GXcopy;
	gcValues.graphics_exposures = False;
	gc = Tk_GetGC(tkwin, GCFunction|GCGraphicsExposures, &gcValues);

	/* Copy the widget background into the helper */
	XCopyArea(Tk_Display(tkwin), d, p, gc, 0, 0,
	  (unsigned) winWidth, (unsigned) winHeight, 0, 0);

	/* Draw the tree onto the helper without regard for borders */
	DrawTreeArea(tv, p);

	/* Copy only the tree area inside the borders back */
	XCopyArea(Tk_Display(tkwin), p, d, gc, x, y,
	  (unsigned) width, (unsigned) height, x, y);

	/* Clean up the temporary resources */
	Tk_FreePixmap(Tk_Display(tkwin), p);
	Tk_FreeGC(Tk_Display(tkwin), gc);
#else
	Ttk_Theme currentTheme = Ttk_GetCurrentTheme(tv->core.interp);
	Ttk_Theme aquaTheme = Ttk_GetTheme(tv->core.interp, "aqua");
	if (currentTheme == aquaTheme && [NSApp macOSVersion] > 100800) {
	    y -= 4;
	    height += 4;
	}

	Tk_ClipDrawableToRect(Tk_Display(tkwin), d, x, y, width, height);
	DrawTreeArea(tv, d);
	Tk_ClipDrawableToRect(Tk_Display(tkwin), d, 0, 0, -1, -1);
#endif
    }
}

/*------------------------------------------------------------------------
 * +++ Utilities for widget commands
 */

/* + NotAncestryCheck --
 *	Verify that specified item is not an ancestor of the specified parent;
 *	returns 1 if OK, 0 and leaves an error message in interp otherwise.
 */
static int NotAncestryCheck(
    Tcl_Interp *interp, Treeview *tv, TreeItem *item, TreeItem *parent) {
    TreeItem *p = parent;
    while (p) {
	if (p == item) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "Cannot insert %s as descendant of %s",
		    ItemName(tv, item), ItemName(tv, parent)));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "ANCESTRY", (char *)NULL);
	    return 0;
	}
	p = p->parent;
    }
    return 1;
}

/* + AncestryCheck --
 *	Verify that specified item is an ancestor of the specified parent;
 *	returns 1 if OK, 0 and leaves an error message in interp otherwise.
 */
static int AncestryCheck(
    Tcl_Interp *interp, Treeview *tv, TreeItem *item, TreeItem *parent) {
    TreeItem *p = item;
    while (p) {
	if (p == parent) {
	    return 1;
	}
	p = p->parent;
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("Item %s is not a descendant of %s",
	    ItemName(tv, item), ItemName(tv, parent)));
    Tcl_SetErrorCode(interp, "TTK", "TREE", "ANCESTRY", (char *)NULL);
    return 0;
}

/* + DeleteItems --
 *	Remove an item and all of its descendants from the hash table
 *	and detach them from the tree; returns a linked list (chained
 *	along the ->next pointer) of deleted items.
 */
static TreeItem *DeleteItems(TreeItem *item, TreeItem *delq) {
    if (item->entryPtr) {
	DetachItem(item);
	while (item->children) {
	    delq = DeleteItems(item->children, delq);
	}
	Tcl_DeleteHashEntry(item->entryPtr);
	item->entryPtr = NULL;
	item->next = delq;
	delq = item;
    } /* else -- item has already been unlinked */
    return delq;
}

/*------------------------------------------------------------------------
 * +++ Widget commands -- item inquiry.
 */

/* + $tv children $item ?newchildren? --
 *	Return the list of children associated with $item
 */
static int TreeviewChildrenCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;
    Tcl_Obj *resultObj;

    if (objc < 3 || objc > 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "item ?newchildren?");
	return TCL_ERROR;
    }
    item = FindItem(interp, tv, objv[2]);
    if (!item) {
	return TCL_ERROR;
    }

    if (objc == 3) {
	resultObj = Tcl_NewListObj(0,0);
	for (item = item->children; item; item = item->next) {
	    Tcl_ListObjAppendElement(interp, resultObj, item->idObj);
	}
	Tcl_SetObjResult(interp, resultObj);
    } else {
	TreeItem **newChildren = GetItemListFromObj(interp, tv, objv[3]);
	TreeItem *child;
	int i;

	if (!newChildren) {
	    return TCL_ERROR;
	}

	/* Sanity-check: */
	for (i = 0; newChildren[i]; ++i) {
	    if (!NotAncestryCheck(interp, tv, newChildren[i], item)) {
		ckfree(newChildren);
		return TCL_ERROR;
	    }
	}

	/* Detach old children: */
	child = item->children;
	while (child) {
	    TreeItem *next = child->next;
	    DetachItem(child);
	    child = next;
	}

	/* Detach new children from their current locations: */
	for (i = 0; newChildren[i]; ++i) {
	    DetachItem(newChildren[i]);
	}

	/* Reinsert new children:
	 * Note: it is not an error for an item to be listed more than once,
	 * though it probably should be...
	 */
	child = 0;
	for (i = 0; newChildren[i]; ++i) {
	    if (newChildren[i]->parent) {
		/* This is a duplicate element which has already been
		 * inserted.  Ignore it. */
		continue;
	    }
	    InsertItem(item, child, newChildren[i]);
	    child = newChildren[i];
	}

	ckfree(newChildren);
	tv->tree.rowPosNeedsUpdate = 1;
	TtkRedisplayWidget(&tv->core);
    }

    return TCL_OK;
}

/* + $tv haschildren $item --
 *	Return boolean for whether item has children or not
 */
static int TreeviewHasChildrenCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "item");
	return TCL_ERROR;
    }
    item = FindItem(interp, tv, objv[2]);
    if (!item) {
	return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(item->children));
    return TCL_OK;
}

/*
 * Recursively count elements
 */
Tcl_Size TreeviewCountItems(TreeItem *parent, int hidden, int recurse) {
    Tcl_Size count = 0;
    TreeItem *item;

    for (item = parent->children; item; item = item->next) {
	if (!item->hidden || hidden) {
	    count++;
	    if (recurse && item->children) {
		count += TreeviewCountItems(item, hidden, recurse);
	    }
	}
    }
    return count;
}

enum { OPT_HIDDEN, OPT_RECURSE, OPT_RECURSIVE, OPT_NORECURSE };
static const char *const optStrings[] = {
	"-hidden", "-recurse", "-recursive", "-norecurse", NULL };

/* + $tv size ?-opt ...? $item --
 *	Return count of immediate children associated with $item or with
 *	-recurse, all sub children. With -hidden, include hidden items.
 */
static int TreeviewSizeCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;
    int option = -1, hidden = 0, recurse = 0;
    Tcl_Size i, count;

    if (objc < 3 || objc > 5) {
	Tcl_WrongNumArgs(interp, 2, objv, "?-hidden? ?-recurse? item");
	return TCL_ERROR;
    }

    if (objc > 3) {
	for (i = 2; i < objc-1; ++i) {
	    if (Tcl_GetIndexFromObjStruct(interp, objv[i], optStrings,
		    sizeof(char *), "option", 0, &option) == TCL_OK) {
		if (option == OPT_HIDDEN) {
		    hidden = 1;
		} else if (option != OPT_NORECURSE) {
		    recurse = 1;
		} else {
		    recurse = 0;
		}
	    } else {
		return TCL_ERROR;
	    }
	}
    }

    item = FindItem(interp, tv, objv[objc-1]);
    if (!item) {
	return TCL_ERROR;
    }

    count = TreeviewCountItems(item, hidden, recurse);
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(count));
    return TCL_OK;
}

/* + $tv parent $item --
 *	Return the item ID of $item's parent.
 */
static int TreeviewParentCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "item");
	return TCL_ERROR;
    }
    item = FindItem(interp, tv, objv[2]);
    if (!item) {
	return TCL_ERROR;
    }

    if (item->parent && (item->parent)->idObj) {
	Tcl_SetObjResult(interp, (item->parent)->idObj);
    } /* This is the root item.  Leave interp-result empty */

    return TCL_OK;
}

/* + $tv depth $item --
 *	Return the number of levels between item and root where root would be 0.
 */
static int TreeviewDepthCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "item");
	return TCL_ERROR;
    }
    item = FindItem(interp, tv, objv[2]);
    if (!item) {
	return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tcl_NewIntObj(ItemDepth(item)+1));
    return TCL_OK;
}

/* + $tv next $item
 *	Return the ID of $item's next sibling.
 */
static int TreeviewNextCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "item");
	return TCL_ERROR;
    }
    item = FindItem(interp, tv, objv[2]);
    if (!item) {
	return TCL_ERROR;
    }

    if (item->next) {
	Tcl_SetObjResult(interp, (item->next)->idObj);
    } /* else -- leave interp-result empty */

    return TCL_OK;
}

/* + $tv prev $item
 *	Return the ID of $item's previous sibling.
 */
static int TreeviewPrevCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "item");
	return TCL_ERROR;
    }
    item = FindItem(interp, tv, objv[2]);
    if (!item) {
	return TCL_ERROR;
    }

    if (item->prev) {
	Tcl_SetObjResult(interp, (item->prev)->idObj);
    } /* else -- leave interp-result empty */

    return TCL_OK;
}

/* + $tv before ?-opt ...? $item --
 *	Get item before $item in view, which may be a sibling or parent
 */
static int TreeviewBeforeCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item, *before;
    int option = -1, hidden = 0, recurse = 1;
    Tcl_Size i;

    if (objc < 3 || objc > 5) {
	Tcl_WrongNumArgs(interp, 2, objv, "?-hidden? ?-norecurse? item");
	return TCL_ERROR;
    }

    if (objc > 3) {
	for (i = 2; i < objc-1; ++i) {
	    if (Tcl_GetIndexFromObjStruct(interp, objv[i], optStrings,
		    sizeof(char *), "option", 0, &option) == TCL_OK) {
		if (option == OPT_HIDDEN) {
		    hidden = 1;
		} else if (option != OPT_NORECURSE) {
		    recurse = 1;
		} else {
		    recurse = 0;
		}
	    } else {
		return TCL_ERROR;
	    }
	}
    }

    item = FindItem(interp, tv, objv[objc-1]);
    /* Abort if invalid, root, or detached item */
    if (!item) {
	return TCL_ERROR;
    }
    if (item == tv->tree.root || item->parent == NULL) {
	return TCL_OK;
    }

    before = GetPrevItem(tv->tree.root, item, hidden, recurse);
    if (before && before->idObj) {
	Tcl_SetObjResult(interp, before->idObj);
    }
    return TCL_OK;
}

/* + $tv after ?-opt ...? $item --
 *	Get item after $item in view, which may be a child, sibling, or sibling of ancestor
 */
static int TreeviewAfterCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item, *after;
    int option = -1, hidden = 0, recurse = 1;
    Tcl_Size i;

    if (objc < 3 || objc > 5) {
	Tcl_WrongNumArgs(interp, 2, objv, "?-hidden? ?-norecurse? item");
	return TCL_ERROR;
    }

    if (objc > 3) {
	for (i = 2; i < objc-1; ++i) {
	    if (Tcl_GetIndexFromObjStruct(interp, objv[i], optStrings,
		    sizeof(char *), "option", 0, &option) == TCL_OK) {
		if (option == OPT_HIDDEN) {
		    hidden = 1;
		} else if (option != OPT_NORECURSE) {
		    recurse = 1;
		} else {
		    recurse = 0;
		}
	    } else {
		return TCL_ERROR;
	    }
	}
    }

    item = FindItem(interp, tv, objv[objc-1]);
    /* Abort if invalid, root, or detached item */
    if (!item) {
	return TCL_ERROR;
    }
    if (item == tv->tree.root || item->parent == NULL) {
	return TCL_OK;
    }

    after = GetNextItem(tv->tree.root, item, hidden, recurse);
    if (after && after->idObj) {
	Tcl_SetObjResult(interp, after->idObj);
    }
    return TCL_OK;
}

/* + GetBetweenList --
 *	Get a list of items between from and to in widget
 */
Tcl_Obj *GetBetweenList(
    Tcl_Interp *interp, Treeview *tv, TreeItem *from, TreeItem *to, int hidden, int recurse) {
    TreeItem *item = from;
    int forwards;
    Tcl_Obj *resultObj = Tcl_NewListObj(0,0);
    if (!resultObj) {
	return NULL;
    }

    if (tv->tree.rowPosNeedsUpdate) {
	UpdatePositionTree(tv);
    }
    forwards = (from->itemPos < to->itemPos) ? 1 : 0;

    while (item && item != to) {
	if (Tcl_ListObjAppendElement(interp, resultObj, item->idObj) != TCL_OK) {
	    Tcl_BounceRefCount(resultObj);
	    return NULL;
	}
	if (forwards) {
	    item = GetNextItem(tv->tree.root, item, hidden, recurse);
	} else {
	    item = GetPrevItem(tv->tree.root, item, hidden, recurse);
	}
    }
    if (item == to) {
	Tcl_ListObjAppendElement(interp, resultObj, item->idObj);
    }
    return resultObj;
}

/* + $tv between ?-opt ...? $from $to --
 *	Get list of items between from and to, inclusive
 */
static int TreeviewBetweenCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *from, *to;
    Tcl_Obj *resultObj;
    int option = -1, hidden = 0, recurse = 1;
    Tcl_Size i;

    if (objc < 3 || objc > 6) {
	Tcl_WrongNumArgs(interp, 2, objv, "?-hidden? ?-norecurse? from to");
	return TCL_ERROR;
    }

    if (objc > 3) {
	for (i = 2; i < objc-2; ++i) {
	    if (Tcl_GetIndexFromObjStruct(interp, objv[i], optStrings,
		    sizeof(char *), "option", 0, &option) == TCL_OK) {
		if (option == OPT_HIDDEN) {
		    hidden = 1;
		} else if (option != OPT_NORECURSE) {
		    recurse = 1;
		} else {
		    recurse = 0;
		}
	    } else {
		return TCL_ERROR;
	    }
	}
    }

    /* Abort if invalid, root, or detached item */
    if (!(from = FindItem(interp, tv, objv[objc-2])) || !(to = FindItem(interp, tv, objv[objc-1]))) {
	return TCL_ERROR;
    }
    if (from == tv->tree.root || to == tv->tree.root || from->parent == NULL || to->parent == NULL) {
	return TCL_OK;
    }

    resultObj = GetBetweenList(interp, tv, from, to, hidden, recurse);
    if (resultObj) {
	Tcl_SetObjResult(interp, resultObj);
    } else {
	return TCL_ERROR;
    }
    return TCL_OK;
}

/* + $tv identifier $item index --
 *	Return the id of the item at index in parent $item.
 */
static int TreeviewIdentifierCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item, *found;

    if (objc != 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "item index");
	return TCL_ERROR;
    }
    item = FindItem(interp, tv, objv[2]);
    if (!item) {
	return TCL_ERROR;
    }

    if (FindItemByIndex(interp, item, objv[3], 0, 0, &found) != TCL_OK) {
	return TCL_ERROR;
    } else if (found && found->idObj != NULL) {
	Tcl_SetObjResult(interp, found->idObj);
    }
    return TCL_OK;
}

/* + $tv index $item ?index? --
 *	Return the index of $item within its parent or
 *	the index of the item at position index.
 */
static int TreeviewIndexCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;
    Tcl_Size index = 0;

    if (objc < 3 || objc > 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "item ?index?");
	return TCL_ERROR;
    }
    item = FindItem(interp, tv, objv[2]);
    if (!item) {
	return TCL_ERROR;
    }

    if (objc == 4) {
	index = FindIndex(interp, item, objv[3]);
	if (index == -1) {
	    return TCL_OK;
	} else if (index == -2) {
	    return TCL_ERROR;
	}
    } else {
	while (item->prev) {
	    ++index;
	    item = item->prev;
	}
    }

    Tcl_SetObjResult(interp, TkNewIndexObj(index));
    return TCL_OK;
}

/* + $tv exists $item --
 *	Test if the specified item id is present in the tree.
 */
static int TreeviewExistsCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    Tcl_HashEntry *entryPtr;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "item");
	return TCL_ERROR;
    }

    entryPtr = Tcl_FindHashEntry(&tv->tree.items, Tcl_GetString(objv[2]));
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(entryPtr != 0));
    return TCL_OK;
}

/* + $tv bbox $item ?$column? --
 *	Return bounding box [x y width height] of specified item.
 */
static int TreeviewBBoxCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item = NULL;
    TreeColumn *column = NULL;
    Ttk_Box bbox;

    if (objc < 3 || objc > 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "item ?column");
	return TCL_ERROR;
    }

    item = FindItem(interp, tv, objv[2]);
    if (!item) {
	return TCL_ERROR;
    }
    if (objc >= 4 && (column = FindColumn(interp,tv,objv[3])) == NULL) {
	return TCL_ERROR;
    }

    if (BoundingBox(tv, item, column, &bbox)) {
	Tcl_SetObjResult(interp, Ttk_NewBoxObj(bbox));
    }

    return TCL_OK;
}

/* + $tv identify $x $y -- (obsolescent)
 *	Implements the old, horrible, 2-argument form of [$tv identify].
 *
 * Returns: one of
 *	heading #n
 *	cell item #n
 *	item item element
 *	row item
 */
static int TreeviewHorribleIdentify(
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size), /* objc */
    Tcl_Obj *const objv[],
    Treeview *tv) {
    const char *what = "nothing", *detail = NULL;
    TreeItem *item = NULL;
    Tcl_Obj *resultObj;
    Tcl_Size dColumnNumber;
    char dcolbuf[32];
    int x, y, x1;
    int scaledHALO = round(HALO * TkScalingLevel(tv->core.tkwin));

    /* ASSERT: objc == 4 */

    if (Tk_GetPixelsFromObj(interp, tv->core.tkwin, objv[2], &x) != TCL_OK
	    || Tk_GetPixelsFromObj(interp, tv->core.tkwin, objv[3], &y) != TCL_OK) {
	return TCL_ERROR;
    }

    dColumnNumber = IdentifyDisplayColumn(tv, x, &x1);
    if (dColumnNumber < 0) {
	goto done;
    }
    snprintf(dcolbuf, sizeof(dcolbuf), "#%" TCL_SIZE_MODIFIER "d", dColumnNumber);

    if (Ttk_BoxContains(tv->tree.headingArea,x,y)) {
	if (-scaledHALO <= x1 - x  && x1 - x <= scaledHALO) {
	    what = "separator";
	} else {
	    what = "heading";
	}
	detail = dcolbuf;
    } else if (Ttk_BoxContains(tv->tree.treeArea,x,y)) {
	item = IdentifyItem(tv, y);
	if (item && dColumnNumber > 0) {
	    what = "cell";
	    detail = dcolbuf;
	} else if (item) {
	    Ttk_Layout layout = tv->tree.itemLayout;
	    Ttk_Box itemBox;
	    DisplayItem displayItem;
	    Ttk_Element element;
	    Ttk_State state = ItemState(tv, item);

	    BoundingBox(tv, item, NULL, &itemBox);
	    PrepareItem(tv, item, &displayItem, state);
	    if (item->textObj) { displayItem.textObj = item->textObj; }
	    if (item->imageObj) { displayItem.imageObj = item->imageObj; }
	    Ttk_RebindSublayout(layout, &displayItem);
	    Ttk_PlaceLayout(layout, state, itemBox);
	    element = Ttk_IdentifyElement(layout, x, y);

	    if (element) {
		what = "item";
		detail = Ttk_ElementName(element);
	    } else {
		what = "row";
	    }
	}
    }

done:
    resultObj = Tcl_NewListObj(0,0);
    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(what, -1));
    if (item) {
	Tcl_ListObjAppendElement(NULL, resultObj, item->idObj);
    }
    if (detail) {
	Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(detail, -1));
    }

    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

/* + $tv identify $component $x $y --
 *	Identify the component at position x,y.
 */

static int TreeviewIdentifyCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    static const char *const submethodStrings[] =
	 { "region", "item", "column", "row", "element", "cell", NULL };
    enum { I_REGION, I_ITEM, I_COLUMN, I_ROW, I_ELEMENT, I_CELL };

    Treeview *tv = (Treeview *)recordPtr;
    int submethod;
    int x, y;

    TreeRegion region;
    Ttk_Box bbox;
    TreeItem *item;
    TreeColumn *column;
    Tcl_Size colno;
    int x1;

    if (objc == 4) {	/* Old form */
	return TreeviewHorribleIdentify(interp, objc, objv, tv);
    } else if (objc != 5) {
	Tcl_WrongNumArgs(interp, 2, objv, "command x y");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[2], submethodStrings,
		sizeof(char *), "command", TCL_EXACT, &submethod) != TCL_OK
	|| Tk_GetPixelsFromObj(interp, tv->core.tkwin, objv[3], &x) != TCL_OK
	|| Tk_GetPixelsFromObj(interp, tv->core.tkwin, objv[4], &y) != TCL_OK
    ) {
	return TCL_ERROR;
    }

    /* Make sure the scroll information is current before use */
    TtkUpdateScrollInfo(tv->tree.xscrollHandle);
    TtkUpdateScrollInfo(tv->tree.yscrollHandle);

    region = IdentifyRegion(tv, x, y);
    item = IdentifyItem(tv, y);
    colno = IdentifyDisplayColumn(tv, x, &x1);
    column = (colno >= 0) ?  tv->tree.displayColumns[colno] : NULL;

    switch (submethod) {
	case I_REGION :
	    Tcl_SetObjResult(interp,Tcl_NewStringObj(regionStrings[region],-1));
	    break;

	case I_ITEM :
	case I_ROW :
	    if (item) {
		Tcl_SetObjResult(interp, item->idObj);
	    }
	    break;

	case I_COLUMN :
	    if (colno >= 0) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("#%" TCL_SIZE_MODIFIER "d", colno));
	    }
	    break;

	case I_CELL :
	    if (item && colno >= 0) {
		Tcl_Obj *elem[2];
		elem[0] = item->idObj;
		elem[1] = Tcl_ObjPrintf("#%" TCL_SIZE_MODIFIER "d", colno);
		Tcl_SetObjResult(interp, Tcl_NewListObj(2, elem));
	    }
	    break;

	case I_ELEMENT :
	{
	    Ttk_Layout layout = 0;
	    DisplayItem displayItem;
	    Ttk_Element element;
	    Ttk_State state;

	    switch (region) {
		case REGION_NOTHING:
		    layout = tv->core.layout;
		    return TCL_OK; /* @@@ NYI */
		case REGION_HEADING:
		case REGION_SEPARATOR:
		    layout = tv->tree.headingLayout;
		    return TCL_OK; /* @@@ NYI */
		case REGION_TREE:
		    layout = tv->tree.itemLayout;
		    break;
		case REGION_CELL:
		    layout = tv->tree.cellLayout;
		    break;
	    }

	    if (item == NULL) {
		return TCL_OK;
	    }
	    if (!BoundingBox(tv, item, column, &bbox)) {
		return TCL_OK;
	    }
	    state = ItemState(tv, item);
	    PrepareItem(tv, item, &displayItem, state);
	    if (item->textObj) { displayItem.textObj = item->textObj; }
	    if (item->imageObj) { displayItem.imageObj = item->imageObj; }
	    Ttk_RebindSublayout(layout, &displayItem);
	    Ttk_PlaceLayout(layout, state, bbox);
	    element = Ttk_IdentifyElement(layout, x, y);

	    if (element) {
		const char *elementName = Ttk_ElementName(element);
		Tcl_SetObjResult(interp, Tcl_NewStringObj(elementName, -1));
	    }
	    break;
	}
    }
    return TCL_OK;
}

/*------------------------------------------------------------------------
 * +++ Widget commands -- item and column configuration.
 */

/* + $tv item $item ?options ....?
 *	Query or configure item options.
 */
static int TreeviewItemCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "item ?-option ?value??...");
	return TCL_ERROR;
    }
    if (!(item = FindItem(interp, tv, objv[2]))) {
	return TCL_ERROR;
    }

    /* Get all options and values, get value for opt, or set opt to value. */
    if (objc == 3) {
	return TtkEnumerateOptions(interp, item, ItemOptionSpecs,
	    tv->tree.itemOptionTable,  tv->core.tkwin);
    } else if (objc == 4) {
	return TtkGetOptionValue(interp, item, objv[3],
	    tv->tree.itemOptionTable, tv->core.tkwin);
    } else {
	return ConfigureItem(interp, tv, item, objc-3, objv+3);
    }
}

/* + $tv column column ?options ....?
 *	Column data accessor
 */
static int TreeviewColumnCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeColumn *column;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "column -option value...");
	return TCL_ERROR;
    }
    if (!(column = FindColumn(interp, tv, objv[2]))) {
	return TCL_ERROR;
    }

    if (objc == 3) {
	return TtkEnumerateOptions(interp, column, ColumnOptionSpecs,
	    tv->tree.columnOptionTable, tv->core.tkwin);
    } else if (objc == 4) {
	return TtkGetOptionValue(interp, column, objv[3],
	    tv->tree.columnOptionTable, tv->core.tkwin);
    } else {
	return ConfigureColumn(interp, tv, column, objc-3, objv+3);
    }
}

/* + $tv heading column ?options ....?
 *	Heading data accessor
 */
static int TreeviewHeadingCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    Tk_OptionTable optionTable = tv->tree.headingOptionTable;
    Tk_Window tkwin = tv->core.tkwin;
    TreeColumn *column;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "column -option value...");
	return TCL_ERROR;
    }
    if (!(column = FindColumn(interp, tv, objv[2]))) {
	return TCL_ERROR;
    }

    if (objc == 3) {
	return TtkEnumerateOptions(
	    interp, column, HeadingOptionSpecs, optionTable, tkwin);
    } else if (objc == 4) {
	return TtkGetOptionValue(
	    interp, column, objv[3], optionTable, tkwin);
    } else {
	return ConfigureHeading(interp, tv, column, objc-3,objv+3);
    }
}

/* + $tv set $item ?$column ?value??
 *	Query or configure cell values
 */
static int TreeviewSetCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;
    TreeColumn *column = NULL;
    Tcl_Size columnNumber;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "item ?column? ?value? ?column value ...?");
	return TCL_ERROR;
    }

    /* Get item */
    if (!(item = FindItem(interp, tv, objv[2]))) {
	return TCL_ERROR;
    }

    if (objc == 3) {
	/* Return dictionary */
	Tcl_Obj *valObj, *resultObj = Tcl_NewListObj(0,0);

	/* Column #0 */
	if (item->textObj != NULL) {
	    Tcl_ListObjAppendElement(NULL, resultObj, tv->tree.column0.idObj);
	    Tcl_ListObjAppendElement(NULL, resultObj, item->textObj);
	}

	/* Other columns */
	for (columnNumber = 0; columnNumber < tv->tree.nColumns; ++columnNumber) {
	    valObj = NULL;

	    Tcl_ListObjAppendElement(NULL, resultObj,
		tv->tree.columns[columnNumber].idObj);
	    if (item->valuesObj != NULL) {
		Tcl_ListObjIndex(interp,item->valuesObj,columnNumber,&valObj);
	    }
	    if (valObj == NULL) {
		valObj = Tcl_NewStringObj("", 0);
	    }
	    Tcl_ListObjAppendElement(NULL, resultObj, valObj);
	}
	Tcl_SetObjResult(interp, resultObj);
	return TCL_OK;

    } else if (objc == 4) {
	/* Get value */
	Tcl_Obj *resultObj = NULL;

	if (!(column = FindColumn(interp, tv, objv[3]))) {
	    return TCL_ERROR;
	}

	/* Get value for column #0 or other column */
	if (column == &tv->tree.column0) {
	    if (item->textObj != NULL) {
		resultObj = item->textObj;
	    }
	} else {
	    if (item->valuesObj != NULL) {
		columnNumber = column - tv->tree.columns;
		Tcl_ListObjIndex(interp,item->valuesObj,columnNumber,&resultObj);
	    }
	}

	/* Use empty string for no value */
	if (!resultObj) {
	    resultObj = Tcl_NewStringObj("", 0);
	}
	Tcl_SetObjResult(interp, resultObj);

    } else {
	/* Set 1 or more values */
	if (tv->tree.nColumns > 0) {
	    Tcl_Size length;

	    /* Make sure -values exists ... */
	    if (!item->valuesObj) {
		item->valuesObj = Tcl_NewListObj(0,0);
		Tcl_IncrRefCount(item->valuesObj);
	    }

	    /* .. and isn't shared */
	    item->valuesObj = unshareObj(item->valuesObj);

	    /* .. and is the right size. */
	    Tcl_ListObjLength(interp, item->valuesObj, &length);
	    while (length < tv->tree.nColumns) {
		Tcl_Obj *empty = Tcl_NewStringObj("",0);
		Tcl_ListObjAppendElement(interp, item->valuesObj, empty);
		++length;
	    }
	}

	/* Set column to value */
	for (int i = 3; i < objc-1; i+=2) {
	    if (!(column = FindColumn(interp, tv, objv[i]))) {
		return TCL_ERROR;
	    }

	    /* Set column #0 or other column to value */
	    if (column == &tv->tree.column0) {
		if (item->textObj != NULL) {
		    Tcl_DecrRefCount(item->textObj);
		}
		item->textObj = objv[i+1];
		Tcl_IncrRefCount(item->textObj);
	    } else if (tv->tree.nColumns > 0 && item->valuesObj) {
		columnNumber = column - tv->tree.columns;
		Tcl_ListObjReplace(interp, item->valuesObj, columnNumber, 1, 1, &objv[i+1]);
	    }
	}
	TtkRedisplayWidget(&tv->core);
    }
    return TCL_OK;
}

/*
 * Recursively set items open state
 */
int TreeviewOpenRecursive(TreeItem *item, int open, Tcl_Obj *openObj, int recurse) {
    int changed = 0;

    if (open && !(item->state & TTK_STATE_OPEN) && item->children) {
	/* open */
	if (item->openObj) {
	    Tcl_DecrRefCount(item->openObj);
	}
	item->openObj = openObj;
	Tcl_IncrRefCount(item->openObj);
	item->state |= TTK_STATE_OPEN;
	changed = 1;

    } else if (!open && (item->state & TTK_STATE_OPEN)) {
	/* close */
	if (item->openObj) {
	    Tcl_DecrRefCount(item->openObj);
	}
	item->openObj = openObj;
	Tcl_IncrRefCount(item->openObj);
	item->state &= ~TTK_STATE_OPEN;
	changed = 1;
    }

    if (recurse && item->children) {
	for (item = item->children; item; item = item->next) {
	    changed |= TreeviewOpenRecursive(item, open, openObj, recurse);
	}
    }
    return changed;
}

/* + $tv collapse|expand ?-recurse? $items --
 *	Collapse/expand items and with -recurse all child items
 */
static int TreeviewCollapseExpand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], int open) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem **items;
    int option = -1, changed = 0;
    Tcl_Obj *openObj;
    Tcl_Size i = 0;

    static const char *const optionStrings[] = { "-recurse", "-recursive", NULL };

    if (objc < 3 || objc > 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "?-recurse? items");
	return TCL_ERROR;
    }

    if (objc == 4 && Tcl_GetIndexFromObjStruct(interp, objv[2], optionStrings,
	    sizeof(char *), "option", 0, &option) != TCL_OK) {
	return TCL_ERROR;
    }

    /* Get items with special work-around for only root item {} */
    if (Tcl_ListObjLength(interp, objv[objc-1], &i) == TCL_OK && i == 0) {
	Tcl_Obj *listObj = Tcl_NewListObj(0, 0);
	Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj("",-1));
	Tcl_IncrRefCount(listObj);
	items = GetItemListFromObj(interp, tv, listObj);
	Tcl_DecrRefCount(listObj);
    } else {
	items = GetItemListFromObj(interp, tv, objv[objc-1]);
    }

    if (!items) {
	return TCL_ERROR;
    }

    /* Cache boolean object */
    openObj = Tcl_NewBooleanObj(open);
    Tcl_IncrRefCount(openObj);

    /* Do expand/collapse for each item */
    for (i = 0; items[i]; ++i) {
	changed |= TreeviewOpenRecursive(items[i], open, openObj, option > -1);
    }

    /* Update widget if any changes were made */
    if (changed) {
	tv->tree.rowPosNeedsUpdate = 1;
	TtkRedisplayWidget(&tv->core);
    }
    Tcl_DecrRefCount(openObj);
    ckfree(items);
    return TCL_OK;
}

/* + $tv collapse ?-recurse? $items --
 *	Collapse items and with -recurse all child items
 */
static int TreeviewCollapseCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return TreeviewCollapseExpand(recordPtr, interp, objc, objv, 0);
}

/* + $tv expand ?-recurse? $items --
 *	Expand items and with -recurse all child items
 */
static int TreeviewExpandCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return TreeviewCollapseExpand(recordPtr, interp, objc, objv, 1);
}

/*
 * Recursively set items hidden state
 */
int TreeviewHideRecursive(TreeItem *item, int hide, int recurse) {
    int changed = 0;

    if ((item->hidden && !hide) || (!item->hidden && hide)) {
	item->hidden = hide;
	changed = 1;
    }

    if (recurse && item->children) {
	for (item = item->children; item; item = item->next) {
	    changed |= TreeviewHideRecursive(item, hide, recurse);
	}
    }
    return changed;
}

/* + $tv hide|unhide ?-recurse? $items --
 *	Hide/unhide items and with -recurse all child items
 */
static int TreeviewHideUnhide(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], int hide) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem **items;
    int option = -1, changed = 0;
    Tcl_Size i = 0;

    static const char *const optionStrings[] = { "-recurse", "-recursive", NULL };

    if (objc < 3 || objc > 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "?-recurse? items");
	return TCL_ERROR;
    }

    if (objc == 4 && Tcl_GetIndexFromObjStruct(interp, objv[2], optionStrings,
	    sizeof(char *), "option", 0, &option) != TCL_OK) {
	return TCL_ERROR;
    }

    /* Get items with special work-around for only root item {} */
    if (Tcl_ListObjLength(interp, objv[objc-1], &i) == TCL_OK && i == 0) {
	Tcl_Obj *listObj = Tcl_NewListObj(0, 0);
	Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj("",-1));
	Tcl_IncrRefCount(listObj);
	items = GetItemListFromObj(interp, tv, listObj);
	Tcl_DecrRefCount(listObj);
    } else {
	items = GetItemListFromObj(interp, tv, objv[objc-1]);
    }

    if (!items) {
	return TCL_ERROR;
    }

    /* Do hide/unhide for each item */
    for (i = 0; items[i]; ++i) {
	changed |= TreeviewHideRecursive(items[i], hide, option > -1);
    }

    /* Update widget if any changes were made */
    if (changed) {
	tv->tree.rowPosNeedsUpdate = 1;
	TtkRedisplayWidget(&tv->core);
    }
    ckfree(items);
    return TCL_OK;
}

/* + $tv hide ?-recurse? $items --
 *	Hide items and with -recurse all child items
 */
static int TreeviewHideCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return TreeviewHideUnhide(recordPtr, interp, objc, objv, 1);
}

/* + $tv unhide ?-recurse? $items --
 *	Unhide items and with -recurse all child items
 */
static int TreeviewUnhideCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return TreeviewHideUnhide(recordPtr, interp, objc, objv, 0);
}

/*------------------------------------------------------------------------
 * +++ Widget commands -- tree modification.
 */

enum { INSERT_AFTER, INSERT_BEFORE };
static const char *const insertStrings[] = {
	"after", "before", NULL };

/* + $tv insert $parent $index ?-id id? ?-option value ...?
 *	Insert a new item at $index in $parent's children.
 * + $tv insert after|before $item ?-id id? ?-option value ...?
 *	Insert new item before or after $item.
 */
static int TreeviewInsertCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *parent, *sibling, *newItem;
    Tcl_HashEntry *entryPtr;
    int isNew, option;
    Tcl_Obj *idObj;

    if (objc < 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "?parent index?|?before|after item? ?-id id? -options...");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(NULL, objv[2], insertStrings, sizeof(char *),
	    "option", TCL_EXACT, &option) == TCL_OK) {
	if ((sibling = FindItem(interp, tv, objv[3])) == NULL) {
	    return TCL_ERROR;
	}

	/* Check if detached */
	parent = sibling->parent;
	if (!parent) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot insert %s detached item",
		insertStrings[option]));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "DETACHED", "ITEM", (char *)NULL);
	    return TCL_ERROR;
	}
	if (option == INSERT_BEFORE) {
	    sibling = sibling->prev;
	}

    } else {
	/* Get parent item: */
	if ((parent = FindItem(interp, tv, objv[2])) == NULL) {
	    return TCL_ERROR;
	}

	/* Locate previous sibling based on $index: */
	if (FindItemByIndex(interp, parent, objv[3], 1, 1, &sibling) != TCL_OK) {
	    return TCL_ERROR;
	}
    }

    /* Get item name:
     *     If -id supplied and does not already exist, use that;
     *     Otherwise autogenerate new one.
     */
    objc -= 4; objv += 4;
    if (objc >= 2 && !strcmp("-id", Tcl_GetString(objv[0]))) {
	const char *itemName = Tcl_GetString(objv[1]);

	entryPtr = Tcl_CreateHashEntry(&tv->tree.items, itemName, &isNew);
	if (!isNew) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"Item %s already exists", itemName));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "ITEM_EXISTS", (char *)NULL);
	    return TCL_ERROR;
	}
	idObj = objv[1];
	objc -= 2; objv += 2;
    } else {
	char idbuf[16];
	do {
	    ++tv->tree.serial;
	    snprintf(idbuf, sizeof(idbuf), "I%03X", tv->tree.serial);
	    entryPtr = Tcl_CreateHashEntry(&tv->tree.items, idbuf, &isNew);
	} while (!isNew);
	idObj = Tcl_NewStringObj(idbuf,-1);
    }

    /* Create and configure new item: */
    newItem = NewItem();
    Tk_InitOptions(interp, newItem, tv->tree.itemOptionTable, tv->core.tkwin);
    newItem->idObj = idObj;
    Tcl_IncrRefCount(newItem->idObj);
    newItem->tagset = Ttk_GetTagSetFromObj(NULL, tv->tree.tagTable, NULL);
    if (ConfigureItem(interp, tv, newItem, objc, objv) != TCL_OK) {
	Tcl_DeleteHashEntry(entryPtr);
	FreeItem(newItem);
	return TCL_ERROR;
    }

    /* Store in hash table, link into tree: */
    Tcl_SetHashValue(entryPtr, newItem);
    newItem->entryPtr = entryPtr;
    InsertItem(parent, sibling, newItem);
    tv->tree.rowPosNeedsUpdate = 1;
    TtkRedisplayWidget(&tv->core);

    Tcl_SetObjResult(interp, newItem->idObj);
    return TCL_OK;
}

/* + $tv detach $items --
 *	Unlink each item in $items from the tree.
 */
static int TreeviewDetachCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem **items;
    Tcl_Size i;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "items");
	return TCL_ERROR;
    }
    if (!(items = GetItemListFromObj(interp, tv, objv[2]))) {
	return TCL_ERROR;
    }

    /* Sanity-check */
    for (i = 0; items[i]; ++i) {
	if (items[i] == tv->tree.root) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"Cannot detach root item", -1));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "ROOT", (char *)NULL);
	    ckfree(items);
	    return TCL_ERROR;
	}
    }

    /* Do detach */
    for (i = 0; items[i]; ++i) {
	DetachItem(items[i]);
    }

    tv->tree.rowPosNeedsUpdate = 1;
    TtkRedisplayWidget(&tv->core);
    ckfree(items);
    return TCL_OK;
}

/* + $tv detached ?-all|$item? --
 *	List detached items (in arbitrary order) or query the detached state of
 *	$item. With -all, will return detached items and all of their descendants.
 */
static int TreeviewDetachedCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;
    int (*fnPtr)(Treeview*, TreeItem*) = IsDetached;

    if (objc < 2 || objc > 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "?-all|item?");
	return TCL_ERROR;
    }

    if (objc == 3) {
	if (!strcmp(Tcl_GetString(objv[2]), "-all")) {
	    fnPtr = IsItemOrAncestorDetached;
	}
    }

    if (objc == 2 || fnPtr == IsItemOrAncestorDetached) {
	/* List detached items */
	Tcl_HashSearch search;
	Tcl_HashEntry *entryPtr = Tcl_FirstHashEntry(&tv->tree.items, &search);
	Tcl_Obj *objPtr = Tcl_NewObj();

	while (entryPtr != NULL) {
	    item = (TreeItem *)Tcl_GetHashValue(entryPtr);
	    entryPtr = Tcl_NextHashEntry(&search);
	    if (fnPtr(tv, item)) {
		Tcl_ListObjAppendElement(NULL, objPtr, item->idObj);
	    }
	}
	Tcl_SetObjResult(interp, objPtr);

    } else if (objc == 3) {
	/* Query; the root is never reported as detached */
	if (!(item = FindItem(interp, tv, objv[2]))) {
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(IsItemOrAncestorDetached(tv, item)));
    }
    return TCL_OK;
}

/* + $tv delete $items --
 *	Delete each item in $items.
 *
 *	Do this in two passes:
 *	First detach the item and all its descendants and remove them
 *	from the hash table.  Free the items themselves in a second pass.
 *
 *	It's done this way because an item may appear more than once
 *	in the list of items to delete (either directly or as a descendant
 *	of a previously deleted item.)
 */

static int TreeviewDeleteCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem **items, *delq;
    Tcl_Size i;
    int selChange = 0;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "items");
	return TCL_ERROR;
    }

    if (!(items = GetItemListFromObj(interp, tv, objv[2]))) {
	return TCL_ERROR;
    }

    /* Sanity-check: */
    for (i = 0; items[i]; ++i) {
	if (items[i] == tv->tree.root) {
	    ckfree(items);
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"Cannot delete root item", -1));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "ROOT", (char *)NULL);
	    return TCL_ERROR;
	}
    }

    /* Remove items from hash table. */
    delq = 0;
    for (i = 0; items[i]; ++i) {
	if (items[i]->state & TTK_STATE_SELECTED) {
	    selChange = 1;
	} else if (items[i]->selObj != NULL) {
	    Tcl_Size length;
	    Tcl_ListObjLength(interp, items[i]->selObj, &length);
	    if (length > 0) {
		selChange = 1;
	    }
	}
	delq = DeleteItems(items[i], delq);
    }

    /* Free items: */
    while (delq) {
	TreeItem *next = delq->next;
	if (tv->tree.focus == delq) {
	    tv->tree.focus = NULL;
	    tv->tree.focusCol = NULL;
	}
	FreeItem(delq);
	delq = next;
    }

    ckfree(items);
    if (selChange) {
	Tk_SendVirtualEvent(tv->core.tkwin, "TreeviewSelect", NULL);
    }
    tv->tree.rowPosNeedsUpdate = 1;
    TtkRedisplayWidget(&tv->core);
    return TCL_OK;
}

/* + $tv move $item $parent $index
 *	Move $item to the specified $index in $parent's child list.
 * + $tv move $item before|after $otherItem
 *	Move $item to before or after $otherItem.
 */
static int TreeviewMoveCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item, *parent;
    TreeItem *sibling;
    int option;

    if (objc != 5) {
	Tcl_WrongNumArgs(interp, 2, objv, "item ?parent index?|?before|after otherItem?");
	return TCL_ERROR;
    }

    /* Get item to move */
    if ((item = FindItem(interp, tv, objv[2])) == NULL) {
	return TCL_ERROR;
    }

    /* Check if root item, if so abort */
    if (item == tv->tree.root) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot move root item"));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "ROOT", "ITEM", (char *)NULL);
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(NULL, objv[3], insertStrings, sizeof(char *),
	    "option", TCL_EXACT, &option) == TCL_OK) {
	/* Get before or after item */
	if ((sibling = FindItem(interp, tv, objv[4])) == NULL) {
	    return TCL_ERROR;
	}

	/* Check if detached */
	if (IsItemOrAncestorDetached(tv, sibling)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot move %s detached item",
		insertStrings[option]));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "DETACHED", "ITEM", (char *)NULL);
	    return TCL_ERROR;
	}
	parent = sibling->parent;
	if (option == INSERT_BEFORE) {
	    sibling = sibling->prev;
	}

    } else {
	/* Get new parent */
	if ((parent = FindItem(interp, tv, objv[3])) == NULL) {
	    return TCL_ERROR;
	}

	/* Locate previous sibling based on $index: */
	if (FindItemByIndex(interp, parent, objv[4], 1, 1, &sibling) != TCL_OK) {
	    return TCL_ERROR;
	}
    }

    /* Check ancestry: */
    if (!NotAncestryCheck(interp, tv, item, parent)) {
	return TCL_ERROR;
    }

    /* Moving an item after itself is a no-op: */
    if (item == sibling) {
	return TCL_OK;
    }

    /* Move item: */
    DetachItem(item);
    InsertItem(parent, sibling, item);

    tv->tree.rowPosNeedsUpdate = 1;
    TtkRedisplayWidget(&tv->core);
    return TCL_OK;
}

/*------------------------------------------------------------------------
 * +++ Widget commands -- scrolling
 */
static int TreeviewXViewCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    return TtkScrollviewCommand(interp, objc, objv, tv->tree.xscrollHandle);
}

static int TreeviewYViewCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    return TtkScrollviewCommand(interp, objc, objv, tv->tree.yscrollHandle);
}

/* $tree see $item ?index? --
 *	Ensure that $item is visible.
 */
static int TreeviewSeeCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item, *parent;
    int scrollRow1, scrollRow2, visibleRows;

    if (objc < 3 || objc > 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "item ?index?");
	return TCL_ERROR;
    }
    if (!(item = FindItem(interp, tv, objv[2]))) {
	return TCL_ERROR;
    }

    if (objc == 4) {
	if (FindItemByIndex(interp, item, objv[3], 0, 0, &item) != TCL_OK) {
	    return TCL_ERROR;
	}
    }

    /* The item cannot be moved into view if any ancestor (or itself) is detached. */
    if (IsItemOrAncestorDetached(tv, item)) {
	return TCL_OK;
    }

    /* Make sure all ancestors are open: */
    for (parent = item->parent; parent; parent = parent->parent) {
	if (!(parent->state & TTK_STATE_OPEN)) {
	    parent->openObj = unshareObj(parent->openObj);
	    Tcl_SetBooleanObj(parent->openObj, 1);
	    parent->state |= TTK_STATE_OPEN;
	    tv->tree.rowPosNeedsUpdate = 1;
	    TtkRedisplayWidget(&tv->core);
	}
    }
    if (tv->tree.rowPosNeedsUpdate) {
	UpdatePositionTree(tv);
    }

    /* Update the scroll information, if necessary */
    TtkUpdateScrollInfo(tv->tree.yscrollHandle);

    /* Make sure item is visible: */
    if (item->rowPos < tv->tree.titleRows) {
	return TCL_OK;
    }
    visibleRows = tv->tree.treeArea.height / tv->tree.rowHeight
	    - tv->tree.titleRows;
    scrollRow1 = item->rowPos - tv->tree.titleRows;
    scrollRow2 = scrollRow1 + item->height - 1;

    if (scrollRow2 >= tv->tree.yscroll.first + visibleRows) {
	scrollRow2 = 1 + scrollRow2 - visibleRows;
	TtkScrollTo(tv->tree.yscrollHandle, scrollRow2, 1);
    }

    /* On small widgets (shorter than one row high, which is also the case
     * before the widget is initially mapped) the above command will have
     * scrolled down too far. This is why both conditions must be checked.
     */
    if (scrollRow1 < tv->tree.yscroll.first || item->height > visibleRows) {
	TtkScrollTo(tv->tree.yscrollHandle, scrollRow1, 1);
    }

    return TCL_OK;
}

/*------------------------------------------------------------------------
 * +++ Widget commands -- interactive column resize
 */

/* + $tree drag $column $newX --
 *	Set right edge of display column $column to x position $X
 */
static int TreeviewDragCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    int left = tv->tree.treeArea.x - tv->tree.xscroll.first;
    Tcl_Size i = FirstColumn(tv);
    TreeColumn *column;
    int newx;

    if (objc != 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "column xposition");
	return TCL_ERROR;
    }

    if (!(column = FindColumn(interp, tv, objv[2])) ||
	    Tcl_GetIntFromObj(interp, objv[3], &newx) != TCL_OK) {
	return TCL_ERROR;
    }

    for (;i < tv->tree.nDisplayColumns; ++i) {
	TreeColumn *c = tv->tree.displayColumns[i];
	int right = left + c->width;
	if (c == column) {
	    if (i < tv->tree.nTitleColumns) {
		/* Unscrollable column, remove scroll shift */
		right += tv->tree.xscroll.first;
	    }
	    DragColumn(tv, i, newx - right);
	    TtkRedisplayWidget(&tv->core);
	    return TCL_OK;
	}
	left = right;
    }

    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	"column %s is not displayed", Tcl_GetString(objv[2])));
    Tcl_SetErrorCode(interp, "TTK", "TREE", "COLUMN_INVISIBLE", (char *)NULL);
    return TCL_ERROR;
}

static int TreeviewDropCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "drop");
	return TCL_ERROR;
    }
    ResizeColumns(tv, TreeWidth(tv));
    TtkRedisplayWidget(&tv->core);
    return TCL_OK;
}

/*------------------------------------------------------------------------
 * +++ Widget commands -- focus and selection
 */

/* + $tree focus ?item?
 */
static int TreeviewFocusCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;

    if (objc < 2 || objc > 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "?item?");
	return TCL_ERROR;
    }

    if (objc == 2) {
	if (tv->tree.focus && tv->tree.focusCol == NULL) {
	    Tcl_SetObjResult(interp, (tv->tree.focus)->idObj);
	}

    } else {
	TreeItem *newFocus;

	/* Get item */
	if (!(newFocus = FindItem(interp, tv, objv[2]))) {
	    return TCL_ERROR;
	}

	/* Clear focus */
	if (newFocus == tv->tree.root) {
	    newFocus = NULL;
	}

	/* Abort if item or ancestor is detached */
	if (newFocus && IsItemOrAncestorDetached(tv, newFocus)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf("detached item"));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "ITEM", "DETACHED", (char *)NULL);
	    return TCL_ERROR;
	}

	tv->tree.focus = newFocus;
	tv->tree.focusCol = NULL;
	TtkRedisplayWidget(&tv->core);
    }
    return TCL_OK;
}

/* + $tree cellfocus ?cell?
 */
static int TreeviewCellFocusCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;

    if (objc < 2 || objc > 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "?cell?");
	return TCL_ERROR;
    }

    if (objc == 2) {
	if (tv->tree.focus && tv->tree.focusCol) {
	    Tcl_Obj *listPtr;

	    if (!(listPtr = Tcl_NewListObj(0, NULL)) ||
		Tcl_ListObjAppendElement(interp, listPtr, (tv->tree.focus)->idObj) != TCL_OK ||
		Tcl_ListObjAppendElement(interp, listPtr, (tv->tree.focusCol)->idObj) != TCL_OK) {
		return TCL_ERROR;
	    }
	    Tcl_SetObjResult(interp, listPtr);
	}

    } else {
	TreeCell cell;
	Tcl_Size len;

	/* Clear focus or get cell */
	Tcl_ListObjLength(interp, objv[2], &len);
	if (len == 0) {
	    cell.item = NULL;
	    cell.column = NULL;
	} else if (GetCellFromObj(interp, tv, objv[2], 0, NULL, &cell) != TCL_OK) {
	    return TCL_ERROR;
	}

	/* Clear focus */
	if (cell.item == tv->tree.root) {
	    cell.item = NULL;
	    cell.column = NULL;
	}

	/* Abort if item or ancestor is detached */
	if (cell.item && IsItemOrAncestorDetached(tv, cell.item)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf("detached item"));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "ITEM", "DETACHED", (char *)NULL);
	    return TCL_ERROR;
	}

	tv->tree.focus = cell.item;
	tv->tree.focusCol = cell.column;
	TtkRedisplayWidget(&tv->core);
    }
    return TCL_OK;
}

/* + $tree selection ?add|remove|set|toggle $items|$from $to?
 */
static int TreeviewSelectionCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    enum {
	SELECTION_ADD, SELECTION_ANCHOR, SELECTION_HAS, SELECTION_INCLUDES,
	SELECTION_PRESENT, SELECTION_REMOVE, SELECTION_SET, SELECTION_SIZE,
	SELECTION_TOGGLE
    };
    static const char *const selopStrings[] = {
	"add", "anchor", "has", "includes", "present", "remove", "set", "size",
	"toggle", NULL
    };

    Treeview *tv = (Treeview *)recordPtr;
    int selop = 0, i, selChange = 0;
    TreeItem *item, **items = NULL, *from, *to;
    Tcl_Obj *listObj = NULL;

    /* Get selection */
    if (objc == 2) {
	Tcl_Obj *resultObj = Tcl_NewListObj(0,0);
	for (item = tv->tree.root->children; item; item = NextPreorder(item)) {
	    if (item->state & TTK_STATE_SELECTED) {
		Tcl_ListObjAppendElement(NULL, resultObj, item->idObj);
	    }
	}
	Tcl_SetObjResult(interp, resultObj);
	return TCL_OK;
    }

    if (objc < 3 || objc > 5) {
	Tcl_WrongNumArgs(interp, 2, objv, "?add|anchor|has|includes|present|remove|set|size|toggle? ?items|from? ?to?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[2], selopStrings,
	    sizeof(char *), "selection operation", 0, &selop) != TCL_OK) {
	return TCL_ERROR;
    }

    if ((objc == 3 && selop != SELECTION_ANCHOR && selop != SELECTION_PRESENT &&
	    selop != SELECTION_SIZE) ||
	    (objc > 3 && (selop == SELECTION_SIZE || selop == SELECTION_PRESENT)) ||
	    (objc > 4 && selop == SELECTION_ANCHOR)) {
	Tcl_WrongNumArgs(interp, 2, objv, "?add|anchor|has|includes|present|remove|set|size|toggle? ?items|from? ?to?");
	return TCL_ERROR;

    } else if (objc == 4) {
	items = GetItemListFromObj(interp, tv, objv[3]);
	if (!items) {
	    return TCL_ERROR;
	}
    } else if (objc == 5) {
	int hidden = 0;
	int recurse = 1;
	if (!(from = FindItem(interp, tv, objv[3])) || !(to = FindItem(interp, tv, objv[4]))) {
	    return TCL_ERROR;
	}
	if (from == tv->tree.root || to == tv->tree.root) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot select root item"));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "ROOT", "ITEM", (char *)NULL);
	    return TCL_ERROR;
	}

	listObj = GetBetweenList(interp, tv, from, to, hidden, recurse);
	if (listObj) {
	    items = GetItemListFromObj(interp, tv, listObj);
	} else {
	    return TCL_ERROR;
	}
	if (!items) {
	    return TCL_ERROR;
	}
    }

    switch (selop) {
	case SELECTION_ADD:
	    /* Add to selection */
	    for (i=0; items[i]; ++i) {
		if (!(items[i]->state & TTK_STATE_SELECTED)) {
		    items[i]->state |= TTK_STATE_SELECTED;
		    selChange = 1;
		}
	    }
	    break;
	case SELECTION_ANCHOR:
	    /* Set or get selection anchor */
	    if (objc == 3) {
		if (tv->tree.selAnchor) {
		    Tcl_SetObjResult(interp, (tv->tree.selAnchor)->idObj);
		}
	    } else {
		if (items[0] != tv->tree.root) {
		    tv->tree.selAnchor = items[0];
		} else {
		    tv->tree.selAnchor = NULL;
		}
		if (tv->tree.selAnchorColObj != NULL) {
		    Tcl_DecrRefCount(tv->tree.selAnchorColObj);
		    tv->tree.selAnchorColObj = NULL;
		}
	    }
	    break;
	case SELECTION_HAS:
	case SELECTION_INCLUDES:
	    /* Check if in selection */
	    int result = 1;
	    for (i=0; items[i]; ++i) {
		if (!(items[i]->state & TTK_STATE_SELECTED)) {
		    result = 0;
		    break;
		}
	    }
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(i > 0 ? result : 0));
	    break;
	case SELECTION_PRESENT:
	    /* Get whether there area selected items or not */
	    int present = 0;
	    for (item = tv->tree.root->children; item; item = NextPreorder(item)) {
		if (item->state & TTK_STATE_SELECTED) {
		    present = 1;
		    break;
		}
	    }
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(present));
	    break;
	case SELECTION_REMOVE:
	    /* Remove from selection */
	    for (i=0; items[i]; ++i) {
		if (items[i]->state & TTK_STATE_SELECTED) {
		    items[i]->state &= ~TTK_STATE_SELECTED;
		    selChange = 1;
		}
	    }
	    break;
	case SELECTION_SET:
	    /* Set selection */
	    for (item=tv->tree.root; item; item = NextPreorder(item)) {
		if (item->state & TTK_STATE_SELECTED) {
		    item->state &= ~TTK_STATE_SELECTED;
		    selChange = 1;
		}
	    }
	    /* Select */
	    for (i=0; items[i]; ++i) {
		items[i]->state |= TTK_STATE_SELECTED;
		selChange = 1;
	    }
	    break;
	case SELECTION_SIZE:
	    /* Number of selected items */
	    Tcl_WideInt count = 0;
	    for (item = tv->tree.root->children; item; item = NextPreorder(item)) {
		if (item->state & TTK_STATE_SELECTED) {
		    count++;
		}
	    }
	    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(count));
	    break;
	case SELECTION_TOGGLE:
	    /* Toggle selection state */
	    for (i=0; items[i]; ++i) {
		items[i]->state ^= TTK_STATE_SELECTED;
		selChange = 1;
	    }
	    break;
    }

    if (objc == 4) {
	ckfree(items);
    }
    if (selChange) {
	Tk_SendVirtualEvent(tv->core.tkwin, "TreeviewSelect", NULL);
	TtkRedisplayWidget(&tv->core);
    }
    return TCL_OK;
}

/* + SelObjChangeElement --
 *	Change an element in a cell selection list.
 */
static int SelObjChangeElement(
    Treeview *tv, Tcl_Obj *listPtr, Tcl_Obj *elemPtr,
    int add,
    TCL_UNUSED(int) /*remove*/,
    int toggle) {
    Tcl_Size i, nElements;
    int anyChange = 0;
    TreeColumn *column, *elemColumn;
    Tcl_Obj **elements;

    elemColumn = FindColumn(NULL, tv, elemPtr);
    Tcl_ListObjGetElements(NULL, listPtr, &nElements, &elements);
    for (i = 0; i < nElements; i++) {
	column = FindColumn(NULL, tv, elements[i]);
	if (column == elemColumn) {
	    if (add) {
		return anyChange;
	    }
	    Tcl_ListObjReplace(NULL, listPtr, i, 1, 0, NULL);
	    anyChange = 1;
	    return anyChange;
	}
    }
    if (add || toggle) {
	Tcl_ListObjAppendElement(NULL, listPtr, elemColumn->idObj);
	anyChange = 1;
    }
    return anyChange;
}

/* + $tree cellselection ?add|remove|set|toggle $items?
 */
static int CellSelectionRange(
    Tcl_Interp *interp, Treeview *tv, Tcl_Obj *fromCell, Tcl_Obj *toCell,
    int add, int remove, int toggle) {
    TreeCell cellFrom, cellTo;
    TreeItem *item;
    Tcl_Obj *columns, **elements;
    int colno, fromNo, toNo, anyChange = 0;
    Tcl_Size i, nElements;
    int set = !(add || remove || toggle);

    if (GetCellFromObj(interp, tv, fromCell, 1, &fromNo, &cellFrom) != TCL_OK) {
	return TCL_ERROR;
    }
    if (GetCellFromObj(interp, tv, toCell, 1, &toNo, &cellTo) != TCL_OK) {
	return TCL_ERROR;
    }

    /* Correct order. */
    if (fromNo > toNo) {
	colno = fromNo;
	fromNo = toNo;
	toNo = colno;
    }

    /* Make a list of columns in this rectangle. */
    columns = Tcl_NewListObj(0, 0);
    Tcl_IncrRefCount(columns);
    for (colno = fromNo; colno <= toNo; colno++) {
	Tcl_ListObjAppendElement(NULL, columns,
		tv->tree.displayColumns[colno]->idObj);
    }

    /* Set is the only operation that affects items outside its rectangle.
     * Start with clearing out. */
    if (set) {
	anyChange = CellSelectionClear(tv);
    }

    /* Correct order. */
    if (tv->tree.rowPosNeedsUpdate) {
	UpdatePositionTree(tv);
    }
    if (cellFrom.item->itemPos > cellTo.item->itemPos) {
	item = cellFrom.item;
	cellFrom.item = cellTo.item;
	cellTo.item = item;
    }

    /* Go through all items in this rectangle. */
    for (item = cellFrom.item; item; item = NextPreorder(item)) {
	if (item->selObj != NULL) {
	    item->selObj = unshareObj(item->selObj);

	    Tcl_ListObjGetElements(NULL, columns, &nElements, &elements);
	    for (i = 0; i < nElements; ++i) {
		anyChange |= SelObjChangeElement(tv, item->selObj, elements[i],
			add, remove, toggle);
	    }
	} else {
	    /* Set, add and toggle do the same thing when empty before. */
	    if (!remove) {
		item->selObj = columns;
		Tcl_IncrRefCount(item->selObj);
		anyChange = 1;
	    }
	}
	if (item == cellTo.item) {
	    break;
	}
    }

    Tcl_DecrRefCount(columns);

    if (anyChange) {
	Tk_SendVirtualEvent(tv->core.tkwin, "TreeviewSelect", NULL);
    }
    TtkRedisplayWidget(&tv->core);
    return TCL_OK;
}

/* + $tree cellselection ?add|remove|set|toggle $items?
 */
static int TreeviewCellSelectionCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    enum {
	SELECTION_SET, SELECTION_ADD, SELECTION_ANCHOR, SELECTION_PRESENT,
	SELECTION_REMOVE, SELECTION_SIZE, SELECTION_TOGGLE
    };
    static const char *const selopStrings[] = {
	"set", "add", "anchor", "present", "remove", "size", "toggle", NULL
    };

    Treeview *tv = (Treeview *)recordPtr;
    int selop, anyChange = 0, nosel = 0;
    Tcl_Size i, nCells = 0;
    TreeCell *cells = NULL;
    TreeItem *item;

    /* Get selected cells */
    if (objc == 2) {
	Tcl_Obj *resultObj = Tcl_NewListObj(0,0);
	for (item = tv->tree.root->children; item; item = NextPreorder(item)) {
	    if (item->selObj != NULL) {
		Tcl_Size n, elemc;
		Tcl_Obj **elemv;

		Tcl_ListObjGetElements(interp, item->selObj, &n, &elemv);
		elemc = n;
		for (i = 0; i < elemc; ++i) {
		    Tcl_Obj *elem[2];
		    elem[0] = item->idObj;
		    elem[1] = elemv[i];
		    Tcl_ListObjAppendElement(NULL, resultObj,
			    Tcl_NewListObj(2, elem));
		}
	    }
	}
	Tcl_SetObjResult(interp, resultObj);
	return TCL_OK;
    }

    if (objc < 3 || objc > 5) {
	Tcl_WrongNumArgs(interp, 2, objv, "?add|anchor|present|remove|set|size|toggle? ?cells|from? ?to?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[2], selopStrings,
	    sizeof(char *), "cellselection operation", 0, &selop) != TCL_OK) {
	return TCL_ERROR;
    }

    nosel = (selop == SELECTION_ANCHOR || selop == SELECTION_PRESENT || selop == SELECTION_SIZE);
    if ((objc == 3 && !nosel) || (objc == 4 && (selop == SELECTION_PRESENT ||
	    selop == SELECTION_SIZE)) || (objc == 5 && nosel)) {
	Tcl_WrongNumArgs(interp, 2, objv, "?add|anchor|present|remove|set|size|toggle? ?cells|from? ?to?");
	return TCL_ERROR;
    } else if (objc == 4 && selop != SELECTION_ANCHOR) {
	cells = GetCellListFromObj(interp, tv, objv[3], &nCells);
	if (cells == NULL) {
	    return TCL_ERROR;
	}
    } else if (objc == 5) {
	switch (selop) {
	    case SELECTION_SET:
		return CellSelectionRange(interp, tv, objv[3], objv[4], 0, 0, 0);
	    case SELECTION_ADD:
		return CellSelectionRange(interp, tv, objv[3], objv[4], 1, 0, 0);
	    case SELECTION_REMOVE:
		return CellSelectionRange(interp, tv, objv[3], objv[4], 0, 1, 0);
	    case SELECTION_TOGGLE:
		return CellSelectionRange(interp, tv, objv[3], objv[4], 0, 0, 1);
	}
    }

    switch (selop) {
	case SELECTION_SET:
	    /* Set selection */
	    anyChange = CellSelectionClear(tv);
	    /*FALLTHRU*/
	case SELECTION_ADD:
	    /* Add cells to selection */
	    for (i = 0; i < nCells; i++) {
		item = cells[i].item;
		if (item->selObj == NULL) {
		    item->selObj = Tcl_NewListObj(0, 0);
		    Tcl_IncrRefCount(item->selObj);
		}
		item->selObj = unshareObj(item->selObj);
		anyChange |= SelObjChangeElement(tv, item->selObj,
			cells[i].colObj, 1, 0, 0);
	    }
	    break;
	case SELECTION_ANCHOR:
	    /* Set or get cell selection anchor */
	    if (objc == 3) {
		if (tv->tree.selAnchor != NULL && tv->tree.selAnchorColObj != NULL) {
		    Tcl_Obj *elem[2];
		    elem[0] = (tv->tree.selAnchor)->idObj;
		    elem[1] = tv->tree.selAnchorColObj;
		    Tcl_SetObjResult(interp, Tcl_NewListObj(2, elem));
		}
		return TCL_OK;
	    } else {
		TreeCell cell;

		if (GetCellFromObj(interp, tv, objv[3], 0, NULL, &cell) != TCL_OK) {
		    Tcl_Size len;

		    Tcl_GetStringFromObj(objv[3], &len);
		    if (len == 0) {
			cell.item = tv->tree.root;
		    } else {
			return TCL_ERROR;
		    }
		}
		if (tv->tree.selAnchorColObj != NULL) {
		    Tcl_DecrRefCount(tv->tree.selAnchorColObj);
		}
		if (cell.item == tv->tree.root) {
		    tv->tree.selAnchor = NULL;
		    tv->tree.selAnchorColObj = NULL;
		} else {
		    tv->tree.selAnchor = cell.item;
		    tv->tree.selAnchorColObj = cell.colObj;
		    Tcl_IncrRefCount(tv->tree.selAnchorColObj);
		}
		return TCL_OK;
	    }
	    break;
	case SELECTION_PRESENT:
	    /* Get whether there are selected cells or not */
	    int present = 0;
	    for (item = tv->tree.root->children; item; item = NextPreorder(item)) {
		if (item->selObj != NULL) {
		    Tcl_Size n;

		    Tcl_ListObjLength(interp, item->selObj, &n);
		    if (n > 0) {
			present = 1;
			break;
		    }
		}
	    }
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(present));
	    break;
	case SELECTION_REMOVE:
	    /* Remove cells from selection */
	    for (i = 0; i < nCells; i++) {
		item = cells[i].item;
		if (item->selObj == NULL) {
		    continue;
		}
		item->selObj = unshareObj(item->selObj);
		anyChange |= SelObjChangeElement(tv, item->selObj,
			cells[i].colObj, 0, 1, 0);
	    }
	    break;
	case SELECTION_SIZE:
	    /* Get number of selected cells */
	    Tcl_Size count = 0;
	    for (item = tv->tree.root->children; item; item = NextPreorder(item)) {
		if (item->selObj != NULL) {
		    Tcl_Size n;

		    Tcl_ListObjLength(interp, item->selObj, &n);
		    count += n;
		}
	    }
	    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(count));
	    break;
	case SELECTION_TOGGLE:
	    /* Toggle selection state for cells */
	    for (i = 0; i < nCells; i++) {
		item = cells[i].item;
		if (item->selObj == NULL) {
		    item->selObj = Tcl_NewListObj(0, 0);
		    Tcl_IncrRefCount(item->selObj);
		}
		item->selObj = unshareObj(item->selObj);
		anyChange = SelObjChangeElement(tv, item->selObj,
			cells[i].colObj, 0, 0, 1);
	    }
	    break;
    }

    if (objc >= 4 && selop != SELECTION_ANCHOR) {
	ckfree(cells);
    }
    if (anyChange) {
	Tk_SendVirtualEvent(tv->core.tkwin, "TreeviewSelect", NULL);
	TtkRedisplayWidget(&tv->core);
    }
    return TCL_OK;
}

/*------------------------------------------------------------------------
 * +++ Widget commands -- search and sort
 */

typedef enum {
    TYPE_ASCII, TYPE_ASCII_NC, TYPE_DICTIONARY,
    TYPE_INTEGER, TYPE_REAL, TYPE_COMMAND
} sortModes_t;

/* + $tv search item ?-option value...? pattern
 */
static int TreeviewSearchCommand(
    void *recordPtr,		/* Treeview data */
    Tcl_Interp *interp,		/* Current interpreter */
    Tcl_Size objc,		/* Number of arguments */
    Tcl_Obj *const objv[]) {	/* Argument values */

    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *parent, *item = NULL, *stop = NULL, *initial;
    const char *pattern = NULL;
    Tcl_Size i, plen, start, end;
    Tcl_Obj *patObj, *resultObj = NULL, *columnsObj = NULL, *valObj, *emptyObj = NULL;
    Tcl_WideInt intVal;
    double doubleVal;
    int index, all = 0, forwards = 1, hidden = 0, nocase = 0, not = 0, recurse = 0;
    int *intArray = NULL, matches = 0, type = 1, wrap = 0;
    Tcl_RegExp regexp = NULL;

    enum {
	SEARCH_ALL, SEARCH_ASCII, SEARCH_BACKWARDS, SEARCH_CELL, SEARCH_COLUMNS,
	SEARCH_DICTIONARY, SEARCH_EXACT, SEARCH_FORWARDS, SEARCH_GLOB,
	SEARCH_HIDDEN, SEARCH_INTEGER, SEARCH_NOCASE, SEARCH_NOT, SEARCH_REAL,
	SEARCH_RECURSE, SEARCH_RECURSIVE, SEARCH_REGEXP, SEARCH_START,
	SEARCH_STOP, SEARCH_UNICODE, SEARCH_WRAP
    };
    static const char *const searchStrings[] = {
	"-all", "-ascii", "-backwards", "-cell", "-columns", "-dictionary",
	"-exact", "-forwards", "-glob", "-hidden", "-integer", "-nocase",
	"-not", "-real", "-recurse", "-recursive", "-regexp", "-start",
	"-stop", "-unicode", "-wraparound", NULL
    };
    int matchType = SEARCH_EXACT;
    sortModes_t dataType = TYPE_ASCII;

    /* Use this to have type default to -selecttype value. */
    /* type = strcmp(Tcl_GetString(tv->tree.selectTypeObj), "cell");*/

    if (objc < 4 || objc > 25) {
	Tcl_WrongNumArgs(interp, 2, objv, "parent ?-option value ...? pattern");
	return TCL_ERROR;
    }

    if (!(parent = FindItem(interp, tv, objv[2]))) {
	return TCL_ERROR;
    }

    /* Parse options */
    for (i = 3; i < objc - 1; ++i) {
	if (Tcl_GetIndexFromObjStruct(interp, objv[i], searchStrings, sizeof(char *),
		"option", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}

	switch (index) {
	    case SEARCH_ALL:
		all = 1;
		break;
	    case SEARCH_ASCII:
	    case SEARCH_UNICODE:
		dataType = TYPE_ASCII;
		break;
	    case SEARCH_BACKWARDS:
		forwards = 0;
		break;
	    case SEARCH_CELL:
		type = 0;
		break;
	    case SEARCH_COLUMNS:
		if (i == objc - 2) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf("no column specified"));
		    Tcl_SetErrorCode(interp, "TTK", "TREE", "COLUMN", (char *)NULL);
		    return TCL_ERROR;
		}
		columnsObj = objv[++i];
		break;
	    case SEARCH_DICTIONARY:
		dataType = TYPE_DICTIONARY;
		nocase = 1;
		break;
	    case SEARCH_EXACT:
		matchType = SEARCH_EXACT;
		break;
	    case SEARCH_FORWARDS:
		forwards = 1;
		break;
	    case SEARCH_GLOB:
		matchType = SEARCH_GLOB;
		break;
	    case SEARCH_HIDDEN:
		hidden = 1;
		break;
	    case SEARCH_INTEGER:
		dataType = TYPE_INTEGER;
		break;
	    case SEARCH_NOCASE:
		nocase = 1;
		break;
	    case SEARCH_NOT:
		not = 1;
		break;
	    case SEARCH_REAL:
		dataType = TYPE_REAL;
		break;
	    case SEARCH_RECURSE:
	    case SEARCH_RECURSIVE:
		recurse = 1;
		break;
	    case SEARCH_REGEXP:
		matchType = SEARCH_REGEXP;
		break;
	    case SEARCH_START:
		if (i == objc - 2) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf("no start item specified"));
		    Tcl_SetErrorCode(interp, "TTK", "TREE", "ITEM", (char *)NULL);
		    return TCL_ERROR;
		}
		if (!(item = FindItem(interp, tv, objv[++i]))) {
		    return TCL_ERROR;
		}
		if (item == tv->tree.root) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot start with root item"));
		    Tcl_SetErrorCode(interp, "TTK", "TREE", "ROOT", "ITEM", (char *)NULL);
		    return TCL_ERROR;
		}
		if (!AncestryCheck(interp, tv, item, parent)) {
		    return TCL_ERROR;
		}
		break;
	    case SEARCH_STOP:
		if (i == objc - 2) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf("no stop item specified"));
		    Tcl_SetErrorCode(interp, "TTK", "TREE", "ITEM", (char *)NULL);
		    return TCL_ERROR;
		}
		if (!(stop = FindItem(interp, tv, objv[++i]))) {
		    return TCL_ERROR;
		}
		if (stop == tv->tree.root) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot stop with root item"));
		    Tcl_SetErrorCode(interp, "TTK", "TREE", "ROOT", "ITEM", (char *)NULL);
		    return TCL_ERROR;
		}
		if (!AncestryCheck(interp, tv, stop, parent)) {
		    return TCL_ERROR;
		}
		break;
	    case SEARCH_WRAP:
		wrap = 1;
		break;
	}
    }

    /* Abort if no items to search */
    if (parent->children == NULL) {
	return TCL_OK;
    }

    /* If no start item, use first child for forward search and last for backwards */
    if (!item) {
	if (forwards) {
	    item = parent->children;
	} else {
	    /* Need to find last child in descendants */
	    if (recurse) {
		item = parent;
		while (item->lastChild) {
		    item = item->lastChild;
		}
	    } else {
		item = parent->lastChild;
	    }
	}
	wrap = 0; /* No wrap-around if starting at first or last */
    }
    initial = item;

    /* Get native form of pattern */
    patObj = objv[objc-1];
    if (dataType <= TYPE_DICTIONARY) {
	if (!(pattern = Tcl_GetStringFromObj(patObj, &plen))) {
	    return TCL_ERROR;
	}
    } else if (dataType == TYPE_INTEGER) {
	if (Tcl_GetWideIntFromObj(interp, patObj, &intVal) != TCL_OK) {
	    return TCL_ERROR;
	}
    } else if (dataType == TYPE_REAL) {
	if (Tcl_GetDoubleFromObj(interp, patObj, &doubleVal) != TCL_OK) {
	    return TCL_ERROR;
	}
    }

    /* Compile Regexp */
    if (matchType == SEARCH_REGEXP) {
	regexp = Tcl_GetRegExpFromObj(interp, patObj, TCL_REG_ADVANCED |
		TCL_REG_NOSUB | (nocase ? TCL_REG_NOCASE : 0));
	if (!regexp) {
	    regexp = Tcl_GetRegExpFromObj(interp, patObj, TCL_REG_ADVANCED |
		(nocase ? TCL_REG_NOCASE : 0));
	    if (!regexp) {
		return TCL_ERROR;
	    }
	}
    }

    /* Map display columns or user requested column ids to data columns */
    if (!columnsObj) {
	TreeColumn *column;
	start = FirstColumn(tv);
	end = tv->tree.nDisplayColumns;

	if (!(intArray = (int *)ckalloc(sizeof(Tcl_Size)*end))) {
	    return TCL_ERROR;
	}

	for (i = 0; i < end; i++) {
	    column = tv->tree.displayColumns[i];
	    if (column == &tv->tree.column0) {
		intArray[i] = 0;
	    } else {
		intArray[i] = column - tv->tree.columns + 1;
	    }
	}

    } else {
	TreeColumn *column;
	start = 0;

	if (Tcl_ListObjLength(interp, columnsObj, &end) != TCL_OK ||
		!(intArray = (int *)ckalloc(sizeof(Tcl_Size)*end))) {
	    return TCL_ERROR;
	}

	for (i = start; i < end; i++) {
	    if (Tcl_ListObjIndex(interp, columnsObj, i, &valObj) != TCL_OK ||
		!(column = FindColumn(interp, tv, valObj))) {
		Tcl_Free(intArray);
		return TCL_ERROR;
	    }

	    if (column == &tv->tree.column0) {
		intArray[i] = 0;
	    } else {
		intArray[i] = column - tv->tree.columns + 1;
	    }
	}
    }

    /* Create list of matching ids */
    if (!(resultObj = Tcl_NewListObj(0,0)) ||
	!(emptyObj = Tcl_NewStringObj("",0))) {
	goto abort;
    }

    /* Loop over items, compare values to pattern, and add matches to result */
    while (item) {
	int match = 0;

	/* Skip hidden items unless allowed */
	if (!(item->hidden) || (item->hidden && hidden)) {
	    Tcl_Size len;

	    /* Loop over text & cell values and compare to pattern */
	    for (i = start; i < end; ++i) {
		if (intArray[i] == 0) {
		    valObj = item->textObj;
		} else if (item->valuesObj == NULL) {
		    valObj = emptyObj;
		} else if (Tcl_ListObjIndex(interp, item->valuesObj, intArray[i]-1,
			&valObj) != TCL_OK) {
		    goto abort;
		}
		if (!valObj) {
		    valObj = emptyObj;
		}

		/* Do ASCII/Unicode compare */
		if (dataType <= TYPE_DICTIONARY) {
		    if (matchType == SEARCH_EXACT) {
			const char *string = Tcl_GetStringFromObj(valObj, &len);
			Tcl_Size numChars = (len <= plen ? len : plen);

			if (len == 0 && plen > 0) {
			    match = 0; /* Empty cell should not match non empty pattern */
			} else if (!nocase) {
			    match = !Tcl_UtfNcmp(string, pattern, numChars);
			} else {
			    match = !Tcl_UtfNcasecmp(string, pattern, numChars);
			}

		    } else if (matchType == SEARCH_GLOB) {
			match = Tcl_StringCaseMatch(Tcl_GetString(valObj),
				pattern, nocase ? TCL_MATCH_NOCASE : 0);

		    } else if (matchType == SEARCH_REGEXP) {
			match =  Tcl_RegExpExecObj(interp, regexp, valObj, 0, 0, 0);
			if (match < 0) {
			    goto abort;
			}
		    }

		/* Do wide integer compare */
		} else if (dataType == TYPE_INTEGER) {
		    Tcl_WideInt val;

		    if (Tcl_GetWideIntFromObj(interp, valObj, &val) == TCL_OK) {
			match = (intVal == val);
		    } else if (Tcl_GetStringFromObj(valObj, &len) && len > 0) {
			goto abort;
		    } /* Ignore empty values */

		/* Do double value compare */
		} else if (dataType == TYPE_REAL) {
		    double val;

		    if (Tcl_GetDoubleFromObj(interp, valObj, &val) == TCL_OK) {
			match = (doubleVal == val);
		    } else if (Tcl_GetStringFromObj(valObj, &len) && len > 0) {
			goto abort;
		    } /* Ignore empty values */
		}

		/* If match, add item or cell id to result list */
		if (match == !not) {
		    match = 1;
		    if (type) {
			if (Tcl_ListObjAppendElement(interp, resultObj,
				item->idObj) != TCL_OK) {
			    goto abort;
			}
		    } else {
			Tcl_Obj *elem[2];
			elem[0] = item->idObj;
			if (intArray[i] == 0) {
			    elem[1] = tv->tree.column0.idObj;
			} else {
			    elem[1] = tv->tree.columns[intArray[i]-1].idObj;
			}
			if (Tcl_ListObjAppendElement(interp, resultObj,
				Tcl_NewListObj(2, elem)) != TCL_OK) {
			    goto abort;
			}
		    }
		    matches++;
		    if (type || !all) {
			break;
		    }
		} else {
		    match = 0;
		}
	    }
	}

	/* Exit loop if match found and not all or at stop index (inclusive) */
	/* Remove "|| (item == stop)" for exclusive stop */
	if ((match && !all) || (item == stop)) {
	   break;
	}

	/* Move to next/previous item */
	if (forwards) {
	    item = GetNextItem(parent, item, hidden, recurse);
	} else {
	    item = GetPrevItem(parent, item, hidden, recurse);
	}

	/* If at end and wrap-around is enabled */
	if (!item && wrap) {
	    if (forwards) {
		item = parent->children;
		if (!stop) {
		    /*stop = initial;	Use this for exclusive stop */
		    stop = GetPrevItem(parent, initial, hidden, recurse);
		}

	    } else {
		/* Need to find last child in descendants */
		if (recurse) {
		    item = parent;
		    while (item->lastChild) {
			item = item->lastChild;
		    }
		} else {
		    item = parent->lastChild;
		}
		if (!stop) {
		    /*stop = initial;	Use this for exclusive stop */
		    stop = GetNextItem(parent, initial, hidden, recurse);
		}
	    }
	    wrap = 0;
	}

	/* Exit loop if at stop index (exclusive stop) */
	/*if (item == stop) {
	   break;
	}*/
    }

    if (intArray) {
	ckfree(intArray);
    }
    if (emptyObj) {
	Tcl_BounceRefCount(emptyObj);
    }

    /* Return list for all values or if not all, only the id object. */
    if (all) {
	Tcl_SetObjResult(interp, resultObj);
    } else if (matches == 1) {
	if (Tcl_ListObjIndex(interp, resultObj, 0, &valObj) == TCL_OK && valObj) {
	    Tcl_SetObjResult(interp, valObj);
	    Tcl_BounceRefCount(resultObj);
	}
    }
    return TCL_OK;

abort:
    if (intArray) {
	ckfree(intArray);
    }
    if (emptyObj) {
	Tcl_BounceRefCount(emptyObj);
    }
    if (resultObj) {
	Tcl_BounceRefCount(resultObj);
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * DictionaryCompare
 *
 *	This function compares two strings as if they were being used in an
 *	index or card catalog. The case of alphabetic characters is ignored,
 *	except to break ties. Thus "B" comes before "b" but after "a". Also,
 *	integers embedded in the strings compare in numerical order. In other
 *	words, "x10y" comes after "x9y", not * before it as it would when
 *	using strcmp().
 *
 * Results:
 *	A negative result means that the first element comes before the
 *	second, and a positive result means that the second element should
 *	come first. A result of zero means the two elements are equal and it
 *	doesn't matter which comes first.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
DictionaryCompare(
    const char *left, const char *right) {	/* The strings to compare. */
    int uniLeft = 0, uniRight = 0, uniLeftLower, uniRightLower;
    int diff, zeros;
    int secondaryDiff = 0;

    while (1) {
	if (isdigit(UCHAR(*right)) && isdigit(UCHAR(*left))) {	/* INTL: digit */
	    /*
	     * There are decimal numbers embedded in the two strings. Compare
	     * them as numbers, rather than strings. If one number has more
	     * leading zeros than the other, the number with more leading
	     * zeros sorts later, but only as a secondary choice.
	     */

	    zeros = 0;
	    while ((*right == '0') && isdigit(UCHAR(right[1]))) {
		right++;
		zeros--;
	    }
	    while ((*left == '0') && isdigit(UCHAR(left[1]))) {
		left++;
		zeros++;
	    }
	    if (secondaryDiff == 0) {
		secondaryDiff = zeros;
	    }

	    /*
	     * The code below compares the numbers in the two strings without
	     * ever converting them to integers. It does this by first
	     * comparing the lengths of the numbers and then comparing the
	     * digit values.
	     */

	    diff = 0;
	    while (1) {
		if (diff == 0) {
		    diff = UCHAR(*left) - UCHAR(*right);
		}
		right++;
		left++;
		if (!isdigit(UCHAR(*right))) {		/* INTL: digit */
		    if (isdigit(UCHAR(*left))) {	/* INTL: digit */
			return 1;
		    } else {
			/*
			 * The two numbers have the same length. See if their
			 * values are different.
			 */

			if (diff != 0) {
			    return diff;
			}
			break;
		    }
		} else if (!isdigit(UCHAR(*left))) {	/* INTL: digit */
		    return -1;
		}
	    }
	    continue;
	}

	/*
	 * Convert character to Unicode for comparison purposes. If either
	 * string is at the terminating null, do a byte-wise comparison and
	 * bail out immediately.
	 */

	if ((*left != '\0') && (*right != '\0')) {
	    left += Tcl_UtfToUniChar(left, &uniLeft);
	    right += Tcl_UtfToUniChar(right, &uniRight);

	    /*
	     * Convert both chars to lower for the comparison, because
	     * dictionary sorts are case-insensitive. Covert to lower, not
	     * upper, so chars between Z and a will sort before A (where most
	     * other interesting punctuations occur).
	     */

	    uniLeftLower = Tcl_UniCharToLower(uniLeft);
	    uniRightLower = Tcl_UniCharToLower(uniRight);
	} else {
	    diff = UCHAR(*left) - UCHAR(*right);
	    break;
	}

	diff = uniLeftLower - uniRightLower;
	if (diff) {
	    return diff;
	}
	if (secondaryDiff == 0) {
	    if (Tcl_UniCharIsUpper(uniLeft) && Tcl_UniCharIsLower(uniRight)) {
		secondaryDiff = -1;
	    } else if (Tcl_UniCharIsUpper(uniRight)
		    && Tcl_UniCharIsLower(uniLeft)) {
		secondaryDiff = 1;
	    }
	}
    }
    if (diff == 0) {
	diff = secondaryDiff;
    }
    return diff;
}

/*
 * This structure stores the data value used by the sort function to
 * arrange the items being sorted into a collection of linked lists.
 */

typedef struct SortElement {
    union {			/* The value that we sorting by. */
	const char *strValuePtr;
	Tcl_WideInt wideValue;
	double doubleValue;
	Tcl_Obj *objValuePtr;
    } collationKey;
    TreeItem *item;		/* Tree item */
    Tcl_Size len;		/* Length of string value or 0 for no value. */
    struct SortElement *nextPtr;/* Next element in the list, or NULL for end. */
} SortElement;

/*
 * This structure defines the sort config info needed by the compare functions
 * and the sort success or failure status.
 */

typedef struct {
    Tcl_Interp *interp;		/* Current interpreter. */
    int isIncreasing;		/* Order: 0=decreasing, 1=increasing */
    sortModes_t sortMode;	/* The sort mode. See sortMode enums. */
    Tcl_Size columnNumber;	/* Widget data column number */
    Tcl_Obj *compareCmdPtr;	/* TCL compare command for TYPE_COMMAND.
				 * Preinitialized to hold base command. */
    int recurse;		/* Sort all descendants flag. */
    int ignoreEmpty;		/* Ignore empty values for integer/real sorts. */
    int resultCode;		/* Completion code for the sort operation. If
				 * an error occurs during the sort this is
				 * changed from TCL_OK to TCL_ERROR. */
} SortInfo;

/*
 *----------------------------------------------------------------------
 *
 * SortCompare --
 *
 *	This procedure is invoked by MergeLists to determine the proper
 *	ordering between two elements.
 *
 * Results:
 *	-1 means the first element comes before the second, 0 means the
 *	two elements are equal, and +1 means that the second element should
 *	come first. Empty values are sorted before non-empty values.
 *
 * Side effects:
 *	None, unless a user-defined comparison command does something weird.
 *
 *----------------------------------------------------------------------
 */

static int
SortCompare(
    SortElement *elemPtr1, SortElement *elemPtr2, /* Values to be compared. */
    SortInfo *infoPtr) {	/* Sort operation configure info. */
    int order = 0, len;

    if (elemPtr1->len > 0 && elemPtr2->len > 0) {
	if (infoPtr->sortMode <= TYPE_DICTIONARY) {
	    /* String compares */
	    if (infoPtr->sortMode == TYPE_ASCII) {
		/* String compare using Unicode order */
		len = elemPtr1->len < elemPtr2->len ? elemPtr1->len : elemPtr2->len;
		order = Tcl_UtfNcmp(elemPtr1->collationKey.strValuePtr,
		    elemPtr2->collationKey.strValuePtr, len);

	    } else if (infoPtr->sortMode == TYPE_ASCII_NC) {
		/* String compare using Unicode no-case order */
		len = elemPtr1->len < elemPtr2->len ? elemPtr1->len : elemPtr2->len;
		order = Tcl_UtfNcasecmp(elemPtr1->collationKey.strValuePtr,
		    elemPtr2->collationKey.strValuePtr, len);

	    } else if (infoPtr->sortMode == TYPE_DICTIONARY) {
		/* String compare using dictionary order */
		order = DictionaryCompare(elemPtr1->collationKey.strValuePtr,
		    elemPtr2->collationKey.strValuePtr);
	    }

	} else if (infoPtr->sortMode == TYPE_INTEGER) {
	    /* Integer compare */
	    Tcl_WideInt a, b;

	    a = elemPtr1->collationKey.wideValue;
	    b = elemPtr2->collationKey.wideValue;
	    order = ((a >= b) - (a <= b));

	} else if (infoPtr->sortMode == TYPE_REAL) {
	    /* Double compare */
	    double a, b;

	    a = elemPtr1->collationKey.doubleValue;
	    b = elemPtr2->collationKey.doubleValue;
	    order = ((a >= b) - (a <= b));

	} else {
	    /* Command compare */
	    Tcl_Obj **objv, *paramObjv[2];
	    Tcl_Obj *objPtr1, *objPtr2;
	    Tcl_Size objc;

	    /* Abort if an error has previously occurred. */
	    if (infoPtr->resultCode != TCL_OK) {
		return 0;
	    }

	    objPtr1 = elemPtr1->collationKey.objValuePtr;
	    objPtr2 = elemPtr2->collationKey.objValuePtr;

	    paramObjv[0] = objPtr1;
	    paramObjv[1] = objPtr2;

	    /* We made space in the command list for the two things to compare.
	    * Replace them and evaluate the result. */
	    Tcl_ListObjLength(infoPtr->interp, infoPtr->compareCmdPtr, &objc);
	    Tcl_ListObjReplace(infoPtr->interp, infoPtr->compareCmdPtr, objc - 2, 2, 2,
		paramObjv);
	    Tcl_ListObjGetElements(infoPtr->interp, infoPtr->compareCmdPtr, &objc, &objv);

	    /* Call command to do compare */
	    infoPtr->resultCode = Tcl_EvalObjv(infoPtr->interp, objc, objv, 0);
	    if (infoPtr->resultCode != TCL_OK) {
		Tcl_AddErrorInfo(infoPtr->interp, "\n    (-compare command)");
		return 0;
	    }

	    /* Parse the result of the command. */
	    if (Tcl_GetIntFromObj(infoPtr->interp, Tcl_GetObjResult(infoPtr->interp), &order) != TCL_OK) {
		Tcl_SetObjResult(infoPtr->interp, Tcl_NewStringObj(
		    "-compare command returned non-integer result", -1));
		Tcl_SetErrorCode(infoPtr->interp, "TCL", "OPERATION", "LSORT",
		    "COMPARISONFAILED", (char *)NULL);
		infoPtr->resultCode = TCL_ERROR;
		return 0;
	    }
	}
    } else {
	/* Empty value compare */
	if (elemPtr1->len == 0 && elemPtr2->len > 0) {
	    order = -1;
	} else if (elemPtr1->len > 0 && elemPtr2->len == 0) {
	    order = 1;
	} else {
	    order = 0;
	}
    }

    if (!infoPtr->isIncreasing) {
	order = -order;
    }
    return order;
}

/*
 *----------------------------------------------------------------------
 *
 * MergeLists -
 *
 *	This procedure combines two sorted lists of SortElement structures
 *	into a single sorted list.
 *
 * Results:
 *	The unified list of SortElement structures.
 *
 *----------------------------------------------------------------------
 */

static SortElement *
MergeLists(
    SortElement *leftPtr,	/* First list to be merged; may be NULL. */
    SortElement *rightPtr,	/* Second list to be merged; may be NULL. */
    SortInfo *infoPtr) {	/* Sort operation config info. */

    SortElement *headPtr, *tailPtr;
    int cmp;

    if (leftPtr == NULL) {
	return rightPtr;
    }
    if (rightPtr == NULL) {
	return leftPtr;
    }
    cmp = SortCompare(leftPtr, rightPtr, infoPtr);
    if (cmp > 0) {
	tailPtr = rightPtr;
	rightPtr = rightPtr->nextPtr;
    } else {
	tailPtr = leftPtr;
	leftPtr = leftPtr->nextPtr;
    }
    headPtr = tailPtr;
    while ((leftPtr != NULL) && (rightPtr != NULL)) {
	cmp = SortCompare(leftPtr, rightPtr, infoPtr);
	if (cmp > 0) {
	    tailPtr->nextPtr = rightPtr;
	    tailPtr = rightPtr;
	    rightPtr = rightPtr->nextPtr;
	} else {
	    tailPtr->nextPtr = leftPtr;
	    tailPtr = leftPtr;
	    leftPtr = leftPtr->nextPtr;
	}
    }
    if (leftPtr != NULL) {
	tailPtr->nextPtr = leftPtr;
    } else {
	tailPtr->nextPtr = rightPtr;
    }
    return headPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * SortItems --
 *
 *	This procedure is invoked to sort all child items of parent.
 *
 * Results:
 *	Tcl result code.
 *
 * Side effects:
 *	Sorts child items.
 *
 *----------------------------------------------------------------------
 */

static int SortItems(
    TreeItem *parent,		/* Parent of child items to sort */
    SortInfo *infoPtr) {	/* Sort operation config info. */

    TreeItem *item;
    Tcl_Size i, j, length, elmArrSize;
    SortElement *elementArray, *elementPtr;
#   define MAXCALLOC 1024000
#   define NUM_LISTS 30
    /* This array holds pointers to temporary lists built during the merge
     * sort. Element i of the array holds a list of length 2**i. */
    SortElement *subList[NUM_LISTS+1];

    /* Initialize the sublists. After the following loop, subList[i] will
     * contain a sorted sublist of length 2**i. Use one extra subList at the
     * end, always at NULL, to indicate the end of the lists. */
    for (j=0; j<=NUM_LISTS; j++) {
	subList[j] = NULL;
    }

    item = parent->children;
    length = TreeviewCountItems(parent, 1, 0);

    /* Allocate storage for sort elements. */
    elmArrSize = length * sizeof(SortElement);
    if (elmArrSize <= MAXCALLOC) {
	elementArray = (SortElement *)Tcl_Alloc(elmArrSize);
    } else {
	elementArray = (SortElement *)malloc(elmArrSize);
    }
    if (!elementArray) {
	Tcl_SetObjResult(infoPtr->interp, Tcl_ObjPrintf("no enough memory to sort %"
		TCL_Z_MODIFIER "u items", length));
	Tcl_SetErrorCode(infoPtr->interp, "TCL", "MEMORY", (char *)NULL);
	return TCL_ERROR;
    }

    /* The following loop creates a SortElement for each item and
     * begins to sort elements into the sublists as they appear. */
    for (i = 0; i < length && item; i++) {
	Tcl_Obj *valPtr;
	Tcl_Size len;

	/* Get cell value to sort by from item. */
	if (infoPtr->columnNumber == -1) {
	    valPtr = item->textObj;
	} else if (item->valuesObj != NULL) {
	    Tcl_ListObjIndex(infoPtr->interp, item->valuesObj, infoPtr->columnNumber, &valPtr);
	} else {
	    valPtr = NULL;
	}
	elementArray[i].len = (valPtr == NULL ? 0 : 1);
	elementArray[i].item = item;
	elementArray[i].nextPtr = NULL;

	/* Get value from valPtr and put into sortable format. */
	if (valPtr) {
	    if (infoPtr->sortMode <= TYPE_DICTIONARY) {
		elementArray[i].collationKey.strValuePtr = Tcl_GetStringFromObj(valPtr, &len);
		elementArray[i].len = len;

	    } else if (infoPtr->sortMode == TYPE_INTEGER) {
		Tcl_WideInt a;

		if (Tcl_GetWideIntFromObj(infoPtr->interp, valPtr, &a) == TCL_OK) {
		    elementArray[i].collationKey.wideValue = a;
		} else {
		    Tcl_GetStringFromObj(valPtr, &len);
		    if (len == 0 && infoPtr->ignoreEmpty) {
			elementArray[i].len = len;
		    } else {
			infoPtr->resultCode = TCL_ERROR;
			goto done;
		    }
		}

	    } else if (infoPtr->sortMode == TYPE_REAL) {
		double a;

		if (Tcl_GetDoubleFromObj(infoPtr->interp, valPtr, &a) == TCL_OK) {
		    elementArray[i].collationKey.doubleValue = a;
		} else {
		    Tcl_GetStringFromObj(valPtr, &len);
		    if (len == 0 && infoPtr->ignoreEmpty) {
			elementArray[i].len = len;
		    } else {
			infoPtr->resultCode = TCL_ERROR;
			goto done;
		    }
		}

	    } else {
		elementArray[i].collationKey.objValuePtr = valPtr;
	    }
	}

	/* Merge this element into the preexisting sublists (and merge together
	 * sublists when we have two of the same size). */
	elementPtr = &elementArray[i];
	for (j=0; subList[j]; j++) {
	    elementPtr = MergeLists(subList[j], elementPtr, infoPtr);
	    subList[j] = NULL;
	}

	if (j >= NUM_LISTS) {
	    j = NUM_LISTS-1;
	}
	subList[j] = elementPtr;
	item = item->next;
    }

    /* Merge all sublists */
    elementPtr = subList[0];
    for (j=1; j<NUM_LISTS; j++) {
	elementPtr = MergeLists(subList[j], elementPtr, infoPtr);
    }

    /* Update widget */
    if (infoPtr->resultCode == TCL_OK) {
	TreeItem *prev = NULL;
	parent->children = NULL;
	parent->lastChild = NULL;

	for (i=0; elementPtr != NULL; elementPtr = elementPtr->nextPtr) {
	    item = elementPtr->item;
	    item->next = NULL;
	    InsertItem(parent, prev, item);
	    prev = item;
	}
    }

    /* Clean-up */
done:
    if (elementArray) {
	if (elmArrSize <= MAXCALLOC) {
	    Tcl_Free(elementArray);
	} else {
	    free((char *)elementArray);
	}
    }

    /* Sort children */
    if (infoPtr->resultCode == TCL_OK && infoPtr->recurse) {
	item = parent->children;
	while (item != NULL && infoPtr->resultCode == TCL_OK) {
	    if (item->children) {
		infoPtr->resultCode = SortItems(item, infoPtr);
	    }
	    item = item->next;
	}
    }
    return infoPtr->resultCode;
}

/* + $tv sort parent ?-option value...?
 */
static int TreeviewSortCommand(
    void *recordPtr,		/* Treeview data */
    Tcl_Interp *interp,		/* Current interpreter */
    Tcl_Size objc,		/* Number of arguments */
    Tcl_Obj *const objv[]) {	/* Argument values */

    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *parent;
    int index, nocase = 0, result = TCL_OK;
    Tcl_Obj *cmdPtr = NULL;
    Tcl_Size i;
    SortInfo sortInfo;

    enum {
	SORT_ASCII, SORT_COLUMN, SORT_COMMAND, SORT_DECREASING,
	SORT_DICTIONARY, SORT_IGNORE_EMPTY, SORT_INCREASING, SORT_INTEGER,
	SORT_NOCASE, SORT_REAL, SORT_RECURSE, SORT_RECURSIVE,
	SORT_UNICODE
    };
    static const char *const sortStrings[] = {
	"-ascii", "-column", "-command", "-decreasing", "-dictionary",
	"-ignoreempty", "-increasing", "-integer", "-nocase", "-real",
	"-recurse", "-recursive", "-unicode", NULL
    };

    if (objc < 3 || objc > 22) {
	Tcl_WrongNumArgs(interp, 2, objv, "parent ?-option value ...?");
	return TCL_ERROR;
    }

    /* Get items to sort */
    if (!(parent = FindItem(interp, tv, objv[2]))) {
	return TCL_ERROR;
    }

    sortInfo.interp = interp;
    sortInfo.isIncreasing = 1;
    sortInfo.sortMode = TYPE_ASCII;
    sortInfo.compareCmdPtr = NULL;
    sortInfo.columnNumber = FirstColumn(tv)-1;
    sortInfo.recurse = 0;
    sortInfo.ignoreEmpty = 0;
    sortInfo.resultCode = TCL_OK;

    /* Pasre options */
    for (i = 3; i < objc; ++i) {
	if (Tcl_GetIndexFromObjStruct(interp, objv[i], sortStrings, sizeof(char *),
		"option", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}

	switch (index) {
	    case SORT_ASCII:
	    case SORT_UNICODE:
		sortInfo.sortMode = TYPE_ASCII;
		break;
	    case SORT_COLUMN: {
		TreeColumn *column = NULL;

		if (i == objc - 1) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf("no column specified"));
		    Tcl_SetErrorCode(interp, "TTK", "TREE", "COLUMN", (char *)NULL);
		    return TCL_ERROR;
		}
		if (!(column = FindColumn(interp, tv, objv[++i]))) {
		    return TCL_ERROR;
		}
		if (column == &tv->tree.column0) {
		    sortInfo.columnNumber = -1;
		} else if (tv->tree.columns) {
		    sortInfo.columnNumber = (column - tv->tree.columns);
		}
		break;
	    }
	    case SORT_COMMAND:
		if (i < objc - 1) {
		    sortInfo.sortMode = TYPE_COMMAND;
		    cmdPtr = objv[++i];
		} else {
		    Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"\"-command\" option must be followed by comparison command", -1));
		    Tcl_SetErrorCode(interp, "TCL", "ARGUMENT", "MISSING", (char *)NULL);
		    return TCL_ERROR;
		}
		break;
	    case SORT_DECREASING:
		sortInfo.isIncreasing = 0;
		break;
	    case SORT_DICTIONARY:
		sortInfo.sortMode = TYPE_DICTIONARY;
		break;
	    case SORT_IGNORE_EMPTY:
		sortInfo.ignoreEmpty = 1;
		break;
	    case SORT_INCREASING:
		sortInfo.isIncreasing = 1;
		break;
	    case SORT_INTEGER:
		sortInfo.sortMode = TYPE_INTEGER;
		break;
	    case SORT_NOCASE:
		nocase = 1;
		break;
	    case SORT_REAL:
		sortInfo.sortMode = TYPE_REAL;
		break;
	    case SORT_RECURSE:
	    case SORT_RECURSIVE:
		sortInfo.recurse = 1;
		break;
	}
    }

    /* Use ASCII case insensitive matching */
    if (nocase && (sortInfo.sortMode == TYPE_ASCII)) {
	sortInfo.sortMode = TYPE_ASCII_NC;
    }

    /* Abort if no items to sort */
    if (parent->children == NULL) {
	return TCL_OK;
    }

    /* For command sorts, duplicate command in case it's deleted while sort is
     * in progress. Also flattens it and adds placeholders to end. */
    if (sortInfo.sortMode == TYPE_COMMAND && cmdPtr) {
	Tcl_Obj *newCommandPtr, *newObjPtr;

	newCommandPtr = Tcl_DuplicateObj(cmdPtr);
	newObjPtr = Tcl_NewObj();
	Tcl_IncrRefCount(newCommandPtr);
	if (Tcl_ListObjAppendElement(interp, newCommandPtr, newObjPtr) != TCL_OK) {
	    Tcl_DecrRefCount(newCommandPtr);
	    Tcl_DecrRefCount(newObjPtr);
	    return TCL_ERROR;
	}
	Tcl_ListObjAppendElement(interp, newCommandPtr, Tcl_NewObj());
	sortInfo.compareCmdPtr = newCommandPtr;
    }

    /* Do sort */
    result = SortItems(parent, &sortInfo);

    /* Clean-up */
    if (sortInfo.sortMode == TYPE_COMMAND) {
	Tcl_DecrRefCount(sortInfo.compareCmdPtr);
	sortInfo.compareCmdPtr = NULL;
    }

    /* Update widget */
    if (result == TCL_OK) {
	tv->tree.rowPosNeedsUpdate = 1;
	TtkRedisplayWidget(&tv->core);
    }
    return result;
}

/*------------------------------------------------------------------------
 * +++ Widget commands -- tags and bindings.
 */

/* + $tv tag bind $tag ?$sequence ?$script??
 */
static int TreeviewTagBindCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    Ttk_TagTable tagTable = tv->tree.tagTable;
    Tk_BindingTable bindingTable = tv->tree.bindingTable;
    Ttk_Tag tag;

    if (objc < 4 || objc > 6) {
	Tcl_WrongNumArgs(interp, 3, objv, "tagName ?sequence? ?script?");
	return TCL_ERROR;
    }

    tag = Ttk_GetTagFromObj(tagTable, objv[3]);
    if (!tag) { return TCL_ERROR; }

    if (objc == 4) {		/* $tv tag bind $tag */
	Tk_GetAllBindings(interp, bindingTable, tag);
    } else if (objc == 5) {	/* $tv tag bind $tag $sequence */
	/* TODO: distinguish "no such binding" (OK) from "bad pattern" (ERROR) */
	const char *script = Tk_GetBinding(interp, bindingTable, tag,
		Tcl_GetString(objv[4]));
	if (script != NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(script,-1));
	}
    } else if (objc == 6) {	/* $tv tag bind $tag $sequence $script */
	const char *sequence = Tcl_GetString(objv[4]);
	const char *script = Tcl_GetString(objv[5]);

	if (!*script) { /* Delete existing binding */
	    Tk_DeleteBinding(interp, bindingTable, tag, sequence);
	} else {
	    unsigned long mask = Tk_CreateBinding(interp,
		    bindingTable, tag, sequence, script, 0);

	    /* Test mask to make sure event is supported: */
	    if (mask & (~TreeviewBindEventMask)) {
		Tk_DeleteBinding(interp, bindingTable, tag, sequence);
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "unsupported event %s\nonly key, button, motion, and"
		    " virtual events supported", sequence));
		Tcl_SetErrorCode(interp, "TTK", "TREE", "BIND_EVENTS", (char *)NULL);
		return TCL_ERROR;
	    }
	}
    }
    return TCL_OK;
}

/* + $tv tag configure $tag ?-option ?value -option value...??
 */
static int TreeviewTagConfigureCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    Ttk_TagTable tagTable = tv->tree.tagTable;
    Ttk_Tag tag;

    if (objc < 4) {
	Tcl_WrongNumArgs(interp, 3, objv, "tagName ?-option ?value ...??");
	return TCL_ERROR;
    }

    tag = Ttk_GetTagFromObj(tagTable, objv[3]);

    if (objc == 4) {
	return Ttk_EnumerateTagOptions(interp, tagTable, tag);
    } else if (objc == 5) {
	Tcl_Obj *resultObj = Ttk_TagOptionValue(interp, tagTable, tag, objv[4]);
	if (resultObj) {
	    Tcl_SetObjResult(interp, resultObj);
	    return TCL_OK;
	} else {
	    return TCL_ERROR;
	}
    } else {
	TtkRedisplayWidget(&tv->core);
	return Ttk_ConfigureTag(interp, tagTable, tag, objc - 4, objv + 4);
    }
}

/* + $tv tag delete $tag
 */
static int TreeviewTagDeleteCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    Ttk_TagTable tagTable = tv->tree.tagTable;
    TreeItem *item = tv->tree.root;
    Ttk_Tag tag;

    if (objc != 4) {
	Tcl_WrongNumArgs(interp, 3, objv, "tagName");
	return TCL_ERROR;
    }

    tag = Ttk_GetTagFromObj(tagTable, objv[3]);
    /* remove the tag from all cells and items */
    while (item) {
	RemoveTagFromCellsAtItem(item, tag);
	RemoveTag(item, tag);
	item = NextPreorder(item);
    }
    /* then remove the tag from the tag table */
    Tk_DeleteAllBindings(tv->tree.bindingTable, tag);
    Ttk_DeleteTagFromTable(tagTable, tag);
    TtkRedisplayWidget(&tv->core);

    return TCL_OK;
}

/* + $tv tag has $tag ?$item?
 */
static int TreeviewTagHasCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;

    if (objc == 4) {	/* Return list of all items with tag */
	Ttk_Tag tag = Ttk_GetTagFromObj(tv->tree.tagTable, objv[3]);
	TreeItem *item = tv->tree.root;
	Tcl_Obj *resultObj = Tcl_NewListObj(0,0);

	while (item) {
	    if (Ttk_TagSetContains(item->tagset, tag)) {
		Tcl_ListObjAppendElement(NULL, resultObj, item->idObj);
	    }
	    item = NextPreorder(item);
	}

	Tcl_SetObjResult(interp, resultObj);
	return TCL_OK;

    } else if (objc == 5) {	/* Test if item has specified tag */
	Ttk_Tag tag = Ttk_GetTagFromObj(tv->tree.tagTable, objv[3]);
	TreeItem *item = FindItem(interp, tv, objv[4]);
	if (!item) {
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp,
	    Tcl_NewBooleanObj(Ttk_TagSetContains(item->tagset, tag)));
	return TCL_OK;

    } else {
	Tcl_WrongNumArgs(interp, 3, objv, "tagName ?item?");
	return TCL_ERROR;
    }
}

/* + $tv tag cell has $tag ?$cell?
 */
static int TreeviewCtagHasCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    TreeCell cell;
    Tcl_Size i, columnNumber;

    if (objc == 5) {	/* Return list of all cells with tag */
	Ttk_Tag tag = Ttk_GetTagFromObj(tv->tree.tagTable, objv[4]);
	TreeItem *item = tv->tree.root;
	Tcl_Obj *resultObj = Tcl_NewListObj(0,0);

	while (item) {
	    for (i = 0; i < item->nTagSets && i <= tv->tree.nColumns; ++i) {
		if (item->cellTagSets[i] != NULL) {
		    if (Ttk_TagSetContains(item->cellTagSets[i], tag)) {
			Tcl_Obj *elem[2];
			elem[0] = item->idObj;
			if (i == 0) {
			    elem[1] = tv->tree.column0.idObj;
			} else {
			    elem[1] = tv->tree.columns[i-1].idObj;
			}
			Tcl_ListObjAppendElement(NULL, resultObj,
				Tcl_NewListObj(2, elem));
		    }
		}
	    }
	    item = NextPreorder(item);
	}

	Tcl_SetObjResult(interp, resultObj);
	return TCL_OK;

    } else if (objc == 6) {	/* Test if cell has specified tag */
	Ttk_Tag tag = Ttk_GetTagFromObj(tv->tree.tagTable, objv[4]);
	int result = 0;
	if (GetCellFromObj(interp, tv, objv[5], 0, NULL, &cell) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (cell.item == tv->tree.root) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot tag root item"));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "ROOT", "ITEM", (char *)NULL);
	    return TCL_ERROR;
	}
	if (cell.column == &tv->tree.column0) {
	    columnNumber = 0;
	} else {
	    columnNumber = (cell.column - tv->tree.columns) + 1;
	}
	if (columnNumber < cell.item->nTagSets) {
	    if (cell.item->cellTagSets[columnNumber] != NULL) {
		result = Ttk_TagSetContains(
			cell.item->cellTagSets[columnNumber], tag);
	    }
	}

	Tcl_SetObjResult(interp, Tcl_NewWideIntObj(result));
	return TCL_OK;

    } else {
	Tcl_WrongNumArgs(interp, 4, objv, "tagName ?cell?");
	return TCL_ERROR;
    }
}

/* + $tv tag names
 */
static int TreeviewTagNamesCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 3, objv, "");
	return TCL_ERROR;
    }

    return Ttk_EnumerateTags(interp, tv->tree.tagTable);
}

/* + $tv tag add $tag $items
 */
static void AddTag(TreeItem *item, Ttk_Tag tag) {
    if (Ttk_TagSetAdd(item->tagset, tag)) {
	if (item->tagsObj) {
	    Tcl_DecrRefCount(item->tagsObj);
	}
	item->tagsObj = Ttk_NewTagSetObj(item->tagset);
	Tcl_IncrRefCount(item->tagsObj);
    }
}

static int TreeviewTagAddCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    Ttk_Tag tag;
    TreeItem **items;
    Tcl_Size i;

    if (objc != 5) {
	Tcl_WrongNumArgs(interp, 3, objv, "tagName items");
	return TCL_ERROR;
    }

    tag = Ttk_GetTagFromObj(tv->tree.tagTable, objv[3]);
    items = GetItemListFromObj(interp, tv, objv[4]);

    if (!items) {
	return TCL_ERROR;
    }

    for (i = 0; items[i]; ++i) {
	if (items[i] == tv->tree.root) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot tag root item"));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "ROOT", "ITEM", (char *)NULL);
	    return TCL_ERROR;
	}
	AddTag(items[i], tag);
    }
    ckfree(items);

    TtkRedisplayWidget(&tv->core);

    return TCL_OK;
}

/* Make sure tagset at column is allocated and initialised */
static void AllocCellTagSets(Treeview *tv, TreeItem *item, Tcl_Size columnNumber) {
    Tcl_Size i, newSize = columnNumber + 1;
    if (newSize < tv->tree.nColumns + 1) {
	newSize = tv->tree.nColumns + 1;
    }
    if (item->nTagSets < newSize) {
	if (item->cellTagSets == NULL) {
	    item->cellTagSets = (Ttk_TagSet *)
		    ckalloc(sizeof(Ttk_TagSet)*newSize);
	} else {
	    item->cellTagSets = (Ttk_TagSet *)
		    ckrealloc(item->cellTagSets, sizeof(Ttk_TagSet) * newSize);
	}
	for (i = item->nTagSets; i < newSize; i++) {
	    item->cellTagSets[i] = NULL;
	}
	item->nTagSets = newSize;
    }

    if (item->cellTagSets[columnNumber] == NULL) {
	item->cellTagSets[columnNumber] =
		Ttk_GetTagSetFromObj(NULL, tv->tree.tagTable, NULL);
    }
}

/* + $tv tag cell add $tag $cells
 */
static int TreeviewCtagAddCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    Ttk_Tag tag;
    TreeCell *cells;
    TreeItem *item;
    Tcl_Size i, nCells, columnNumber;

    if (objc != 6) {
	Tcl_WrongNumArgs(interp, 4, objv, "tagName cells");
	return TCL_ERROR;
    }

    cells = GetCellListFromObj(interp, tv, objv[5], &nCells);
    if (cells == NULL) {
	return TCL_ERROR;
    }

    tag = Ttk_GetTagFromObj(tv->tree.tagTable, objv[4]);

    for (i = 0; i < nCells; i++) {
	if (cells[i].item == tv->tree.root) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot tag root item"));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "ROOT", "ITEM", (char *)NULL);
	    return TCL_ERROR;
	}

	if (cells[i].column == &tv->tree.column0) {
	    columnNumber = 0;
	} else {
	    columnNumber = (cells[i].column - tv->tree.columns)  + 1;
	}
	item = cells[i].item;
	AllocCellTagSets(tv, item, columnNumber);
	Ttk_TagSetAdd(item->cellTagSets[columnNumber], tag);
    }

    ckfree(cells);
    TtkRedisplayWidget(&tv->core);

    return TCL_OK;
}

/* + $tv tag remove $tag ?$items?
 */
static void RemoveTag(TreeItem *item, Ttk_Tag tag) {
    if (Ttk_TagSetRemove(item->tagset, tag)) {
	if (item->tagsObj) {
	    Tcl_DecrRefCount(item->tagsObj);
	}
	item->tagsObj = Ttk_NewTagSetObj(item->tagset);
	Tcl_IncrRefCount(item->tagsObj);
    }
}

/* Remove tag from all cells at row 'item'
 */
static void RemoveTagFromCellsAtItem(TreeItem *item, Ttk_Tag tag) {
    Tcl_Size i;

    for (i = 0; i < item->nTagSets; i++) {
	if (item->cellTagSets[i] != NULL) {
	    Ttk_TagSetRemove(item->cellTagSets[i], tag);
	}
    }
}

static int TreeviewTagRemoveCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    Ttk_Tag tag;

    if (objc < 4 || objc > 5) {
	Tcl_WrongNumArgs(interp, 3, objv, "tagName ?items?");
	return TCL_ERROR;
    }

    tag = Ttk_GetTagFromObj(tv->tree.tagTable, objv[3]);

    if (objc == 5) {
	TreeItem **items = GetItemListFromObj(interp, tv, objv[4]);
	int i;

	if (!items) {
	    return TCL_ERROR;
	}
	for (i = 0; items[i]; ++i) {
	    if (items[i] == tv->tree.root) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot tag root item"));
		Tcl_SetErrorCode(interp, "TTK", "TREE", "ROOT", "ITEM", (char *)NULL);
		return TCL_ERROR;
	    }
	    RemoveTag(items[i], tag);
	}
	ckfree(items);
    } else if (objc == 4) {
	TreeItem *item = tv->tree.root;
	while (item) {
	    RemoveTag(item, tag);
	    item = NextPreorder(item);
	}
    }

    TtkRedisplayWidget(&tv->core);

    return TCL_OK;
}

/* + $tv tag cell remove $tag ?$cells?
 */
static int TreeviewCtagRemoveCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Treeview *tv = (Treeview *)recordPtr;
    Ttk_Tag tag;
    TreeCell *cells;
    TreeItem *item;
    Tcl_Size i, nCells, columnNumber;

    if (objc < 5 || objc > 6) {
	Tcl_WrongNumArgs(interp, 4, objv, "tagName ?cells?");
	return TCL_ERROR;
    }

    tag = Ttk_GetTagFromObj(tv->tree.tagTable, objv[4]);

    if (objc == 6) {
	cells = GetCellListFromObj(interp, tv, objv[5], &nCells);
	if (cells == NULL) {
	    return TCL_ERROR;
	}

	for (i = 0; i < nCells; i++) {
	    if (cells[i].item == tv->tree.root) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot tag root item"));
		Tcl_SetErrorCode(interp, "TTK", "TREE", "ROOT", "ITEM", (char *)NULL);
		return TCL_ERROR;
	    }
	    if (cells[i].column == &tv->tree.column0) {
		columnNumber = 0;
	    } else {
		columnNumber = (cells[i].column - tv->tree.columns) + 1;
	    }
	    item = cells[i].item;
	    AllocCellTagSets(tv, item, columnNumber);
	    Ttk_TagSetRemove(item->cellTagSets[columnNumber], tag);
	}
	ckfree(cells);
    } else {
	item = tv->tree.root;
	while (item) {
	    RemoveTagFromCellsAtItem(item, tag);
	    item = NextPreorder(item);
	}
    }

    TtkRedisplayWidget(&tv->core);

    return TCL_OK;
}

static const Ttk_Ensemble TreeviewCtagCommands[] = {
    { "add",		TreeviewCtagAddCommand,0 },
    { "has",		TreeviewCtagHasCommand,0 },
    { "remove",		TreeviewCtagRemoveCommand,0 },
    { 0,0,0 }
};

static const Ttk_Ensemble TreeviewTagCommands[] = {
    { "add",		TreeviewTagAddCommand,0 },
    { "bind",		TreeviewTagBindCommand,0 },
    { "cell",		0,TreeviewCtagCommands },
    { "configure",	TreeviewTagConfigureCommand,0 },
    { "delete",		TreeviewTagDeleteCommand,0 },
    { "has",		TreeviewTagHasCommand,0 },
    { "names",		TreeviewTagNamesCommand,0 },
    { "remove",		TreeviewTagRemoveCommand,0 },
    { 0,0,0 }
};

/*------------------------------------------------------------------------
 * +++ Widget commands record.
 */
static const Ttk_Ensemble TreeviewCommands[] = {
    { "after",		TreeviewAfterCommand,0 },
    { "bbox",		TreeviewBBoxCommand,0 },
    { "before",		TreeviewBeforeCommand,0 },
    { "between",	TreeviewBetweenCommand,0 },
    { "cellfocus",	TreeviewCellFocusCommand,0 },
    { "cellselection",	TreeviewCellSelectionCommand,0 },
    { "children",	TreeviewChildrenCommand,0 },
    { "cget",		TtkWidgetCgetCommand,0 },
    { "collapse",	TreeviewCollapseCommand,0 },
    { "column",		TreeviewColumnCommand,0 },
    { "configure",	TtkWidgetConfigureCommand,0 },
    { "delete",		TreeviewDeleteCommand,0 },
    { "depth",		TreeviewDepthCommand,0 },
    { "detach",		TreeviewDetachCommand,0 },
    { "detached",	TreeviewDetachedCommand,0 },
    { "drag",		TreeviewDragCommand,0 },
    { "drop",		TreeviewDropCommand,0 },
    { "expand",		TreeviewExpandCommand,0 },
    { "exists",		TreeviewExistsCommand,0 },
    { "focus",		TreeviewFocusCommand,0 },
    { "haschildren",	TreeviewHasChildrenCommand,0 },
    { "heading",	TreeviewHeadingCommand,0 },
    { "hide",		TreeviewHideCommand,0 },
    { "id",		TreeviewIdentifierCommand,0 },
    { "identify",	TreeviewIdentifyCommand,0 },
    { "identifier",	TreeviewIdentifierCommand,0 },
    { "index",		TreeviewIndexCommand,0 },
    { "insert",		TreeviewInsertCommand,0 },
    { "instate",	TtkWidgetInstateCommand,0 },
    { "item",		TreeviewItemCommand,0 },
    { "move",		TreeviewMoveCommand,0 },
    { "next",		TreeviewNextCommand,0 },
    { "parent",		TreeviewParentCommand,0 },
    { "prev",		TreeviewPrevCommand,0 },
    { "search",		TreeviewSearchCommand,0 },
    { "see",		TreeviewSeeCommand,0 },
    { "selection",	TreeviewSelectionCommand,0 },
    { "set",		TreeviewSetCommand,0 },
    { "size",		TreeviewSizeCommand,0 },
    { "sort",		TreeviewSortCommand,0 },
    { "state",		TtkWidgetStateCommand,0 },
    { "style",		TtkWidgetStyleCommand,0 },
    { "tag",		0,TreeviewTagCommands },
    { "unhide",		TreeviewUnhideCommand,0 },
    { "xview",		TreeviewXViewCommand,0 },
    { "yview",		TreeviewYViewCommand,0 },
    { 0,0,0 }
};

/*------------------------------------------------------------------------
 * +++ Widget definition.
 */

static const WidgetSpec TreeviewWidgetSpec = {
    "Treeview",			/* className */
    sizeof(Treeview),		/* recordSize */
    TreeviewOptionSpecs,	/* optionSpecs */
    TreeviewCommands,		/* subcommands */
    TreeviewInitialize,		/* initializeProc */
    TreeviewCleanup,		/* cleanupProc */
    TreeviewConfigure,		/* configureProc */
    TtkNullPostConfigure,	/* postConfigureProc */
    TreeviewGetLayout,		/* getLayoutProc */
    TreeviewSize,		/* sizeProc */
    TreeviewDoLayout,		/* layoutProc */
    TreeviewDisplay		/* displayProc */
};

/*------------------------------------------------------------------------
 * +++ Layout specifications.
 */

TTK_BEGIN_LAYOUT_TABLE(LayoutTable)

TTK_LAYOUT("Treeview",
    TTK_GROUP("Treeview.field", TTK_FILL_BOTH|TTK_BORDER,
	TTK_GROUP("Treeview.padding", TTK_FILL_BOTH,
	    TTK_NODE("Treeview.treearea", TTK_FILL_BOTH))))

TTK_LAYOUT("Item",
    TTK_GROUP("Treeitem.padding", TTK_FILL_BOTH,
	TTK_NODE("Treeitem.indicator", TTK_PACK_LEFT)
	TTK_NODE("Treeitem.image", TTK_PACK_LEFT)
	TTK_NODE("Treeitem.text", TTK_FILL_BOTH)))

TTK_LAYOUT("Cell",
    TTK_GROUP("Treedata.padding", TTK_FILL_BOTH,
	TTK_NODE("Treeitem.image", TTK_PACK_LEFT)
	TTK_NODE("Treeitem.text", TTK_FILL_BOTH)))

TTK_LAYOUT("Heading",
    TTK_NODE("Treeheading.cell", TTK_FILL_BOTH)
    TTK_GROUP("Treeheading.border", TTK_FILL_BOTH,
	TTK_GROUP("Treeheading.padding", TTK_FILL_BOTH,
	    TTK_NODE("Treeheading.image", TTK_PACK_RIGHT)
	    TTK_NODE("Treeheading.text", TTK_FILL_X))))

TTK_LAYOUT("Row",
    TTK_NODE("Treeitem.row", TTK_FILL_BOTH))

TTK_LAYOUT("Separator",
    TTK_NODE("Treeitem.separator", TTK_FILL_BOTH))

TTK_END_LAYOUT_TABLE

/*------------------------------------------------------------------------
 * +++ Tree indicator element.
 */

typedef struct {
    Tcl_Obj *colorObj;
    Tcl_Obj *sizeObj;
    Tcl_Obj *marginsObj;
} TreeitemIndicator;

static const Ttk_ElementOptionSpec TreeitemIndicatorOptions[] = {
    { "-foreground", TK_OPTION_COLOR,
	offsetof(TreeitemIndicator,colorObj), DEFAULT_FOREGROUND },
    { "-indicatorsize", TK_OPTION_PIXELS,
	offsetof(TreeitemIndicator,sizeObj), "12" },
    { "-indicatormargins", TK_OPTION_STRING,
	offsetof(TreeitemIndicator,marginsObj), "2 2 4 2" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void TreeitemIndicatorSize(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    int *widthPtr,
    int *heightPtr,
    TCL_UNUSED(Ttk_Padding *)) {

    TreeitemIndicator *indicator = (TreeitemIndicator *)elementRecord;
    int size = 0;
    Ttk_Padding margins;

    Tk_GetPixelsFromObj(NULL, tkwin, indicator->sizeObj, &size);
    if (size % 2 == 0) --size;	/* An odd size is better for the indicator. */
    Ttk_GetPaddingFromObj(NULL, tkwin, indicator->marginsObj, &margins);

    *widthPtr = size + Ttk_PaddingWidth(margins);
    *heightPtr = size + Ttk_PaddingHeight(margins);
}

static void TreeitemIndicatorDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state) {

    TreeitemIndicator *indicator = (TreeitemIndicator *)elementRecord;
    ArrowDirection direction =
	(state & TTK_STATE_OPEN) ? ARROW_DOWN : ARROW_RIGHT;
    Ttk_Padding margins;
    int cx, cy;
    XColor *borderColor = Tk_GetColorFromObj(tkwin, indicator->colorObj);
    XGCValues gcvalues; GC gc; unsigned mask;

    if (state & TTK_STATE_LEAF) /* don't draw anything */
	return;

    Ttk_GetPaddingFromObj(NULL,tkwin,indicator->marginsObj,&margins);
    b = Ttk_PadBox(b, margins);

    switch (direction) {
	case ARROW_DOWN:
	    TtkArrowSize(b.width/2, direction, &cx, &cy);
	    if ((b.height - cy) % 2 == 1) {
		++cy;
	    }
	    break;
	case ARROW_RIGHT:
	default:
	    TtkArrowSize(b.height/2, direction, &cx, &cy);
	    if ((b.width - cx) % 2 == 1) {
		++cx;
	    }
	    break;
    }

    b = Ttk_AnchorBox(b, cx, cy, TK_ANCHOR_CENTER);

    gcvalues.foreground = borderColor->pixel;
    gcvalues.line_width = 1;
    mask = GCForeground | GCLineWidth;
    gc = Tk_GetGC(tkwin, mask, &gcvalues);

    TtkDrawArrow(Tk_Display(tkwin), d, gc, b, direction);

    Tk_FreeGC(Tk_Display(tkwin), gc);
}

static const Ttk_ElementSpec TreeitemIndicatorElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(TreeitemIndicator),
    TreeitemIndicatorOptions,
    TreeitemIndicatorSize,
    TreeitemIndicatorDraw
};

/*------------------------------------------------------------------------
 * +++ Row element.
 */

typedef struct {
    Tcl_Obj *backgroundObj;
    Tcl_Obj *rowNumberObj;
} RowElement;

static const Ttk_ElementOptionSpec RowElementOptions[] = {
    { "-background", TK_OPTION_COLOR,
	offsetof(RowElement,backgroundObj), DEFAULT_BACKGROUND },
    { "-rownumber", TK_OPTION_INT,
	offsetof(RowElement,rowNumberObj), "0" },
    { NULL, TK_OPTION_BOOLEAN, 0, NULL }
};

static void RowElementDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State)) {

    RowElement *row = (RowElement *)elementRecord;
    XColor *color = Tk_GetColorFromObj(tkwin, row->backgroundObj);
    GC gc = Tk_GCForColor(color, d);

    XFillRectangle(Tk_Display(tkwin), d, gc,
	    b.x, b.y, b.width, b.height);
}

static const Ttk_ElementSpec RowElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(RowElement),
    RowElementOptions,
    TtkNullElementSize,
    RowElementDraw
};

/*------------------------------------------------------------------------
 * +++ Initialization.
 */

MODULE_SCOPE void
TtkTreeview_Init(Tcl_Interp *interp) {
    Ttk_Theme theme = Ttk_GetDefaultTheme(interp);

    RegisterWidget(interp, "ttk::treeview", &TreeviewWidgetSpec);

    Ttk_RegisterElement(interp, theme, "Treeitem.indicator",
	    &TreeitemIndicatorElementSpec, 0);
    Ttk_RegisterElement(interp, theme, "Treeitem.row", &RowElementSpec, 0);
    Ttk_RegisterElement(interp, theme, "Treeitem.separator", &RowElementSpec, 0);
    Ttk_RegisterElement(interp, theme, "Treeheading.cell", &RowElementSpec, 0);
    Ttk_RegisterElement(interp, theme, "treearea", &ttkNullElementSpec, 0);

    Ttk_RegisterLayouts(theme, LayoutTable);
}

/*EOF*/
