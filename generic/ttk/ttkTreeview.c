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
#endif

#define DEF_TREE_ROWS		"10"
#define DEF_TITLECOLUMNS	"0"
#define DEF_TITLEITEMS		"0"
#define DEF_STRIPED		"0"
#define DEF_COLWIDTH		"200"
#define DEF_MINWIDTH		"20"

static const Tk_Anchor DEFAULT_IMAGEANCHOR = TK_ANCHOR_W;
static const int DEFAULT_INDENT 	= 20;
static const int HALO   		= 4;	/* heading separator */

#define TTK_STATE_OPEN TTK_STATE_USER1
#define TTK_STATE_LEAF TTK_STATE_USER2

#define STATE_CHANGED	 	(0x100)	/* item state option changed */

#define MAX(a,b) (((a) > (b)) ? (a) : (b))

/*------------------------------------------------------------------------
 * +++ Tree items.
 *
 * INVARIANTS:
 * 	item->children	==> item->children->parent == item
 *	item->next	==> item->next->parent == item->parent
 * 	item->next 	==> item->next->prev == item
 * 	item->prev 	==> item->prev->next == item
 */

typedef struct TreeItemRec TreeItem;
struct TreeItemRec {
    Tcl_HashEntry *entryPtr;	/* Back-pointer to hash table entry */
    TreeItem	*parent;	/* Parent item */
    TreeItem	*children;	/* Linked list of child items */
    TreeItem	*next;		/* Next sibling */
    TreeItem	*prev;		/* Previous sibling */

    /*
     * Options and instance data:
     */
    Ttk_State 	state;
    Tcl_Obj	*textObj;
    Tcl_Obj	*imageObj;
    Tcl_Obj	*valuesObj;
    Tcl_Obj	*openObj;
    Tcl_Obj	*tagsObj;
    Tcl_Obj     *selObj;
    Tcl_Obj     *imageAnchorObj;
    int 	hidden;
    int		height; 	/* Height is in number of row heights */

    Ttk_TagSet  *cellTagSets;
    Tcl_Size	nTagSets;

    /*
     * Derived resources:
     */
    Ttk_TagSet	tagset;
    Ttk_ImageSpec *imagespec;
    int itemPos; 		/* Counting items */
    int visiblePos; 		/* Counting visible items */
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
 * 	Allocate a new, uninitialized, unlinked item
 */
static TreeItem *NewItem(void)
{
    TreeItem *item = (TreeItem *)ckalloc(sizeof(*item));

    item->entryPtr = 0;
    item->parent = item->children = item->next = item->prev = NULL;

    item->state = 0ul;
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

    return item;
}

/* + FreeItem --
 * 	Destroy an item
 */
static void FreeItem(TreeItem *item)
{
    Tcl_Size i;
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
 * 	Unlink an item from the tree.
 */
static void DetachItem(TreeItem *item)
{
    if (item->parent && item->parent->children == item)
	item->parent->children = item->next;
    if (item->prev)
	item->prev->next = item->next;
    if (item->next)
	item->next->prev = item->prev;
    item->next = item->prev = item->parent = NULL;
}

/* + InsertItem --
 * 	Insert an item into the tree after the specified item.
 *
 * Preconditions:
 * 	+ item is currently detached
 * 	+ prev != NULL ==> prev->parent == parent.
 */
static void InsertItem(TreeItem *parent, TreeItem *prev, TreeItem *item)
{
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
    }
}

/* + NextPreorder --
 * 	Return the next item in preorder traversal order.
 */

static TreeItem *NextPreorder(TreeItem *item)
{
    if (item->children)
	return item->children;
    while (!item->next) {
	item = item->parent;
	if (!item)
	    return 0;
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
    int 	width;		/* Column width, in pixels */
    int 	minWidth;	/* Minimum column width, in pixels */
    int 	stretch;	/* Should column stretch while resizing? */
    int         separator;      /* Should this column have a separator? */
    Tcl_Obj	*idObj;		/* Column identifier, from -columns option */

    Tcl_Obj	*anchorObj;	/* -anchor for cell data <<NOTE-ANCHOR>> */

    /* Column heading data:
     */
    Tcl_Obj 	*headingObj;		/* Heading label */
    Tcl_Obj	*headingImageObj;	/* Heading image */
    Tcl_Obj 	*headingAnchorObj;	/* -anchor for heading label */
    Tcl_Obj	*headingCommandObj;	/* Command to execute */
    Tcl_Obj 	*headingStateObj;	/* @@@ testing ... */
    Ttk_State	headingState;		/* ... */

    /* Temporary storage for cell data
     */
    Tcl_Obj 	*data;
    int         selected;
    Ttk_TagSet	tagset;
} TreeColumn;

static void InitColumn(TreeColumn *column)
{
    column->width = atoi(DEF_COLWIDTH);
    column->minWidth = atoi(DEF_MINWIDTH);
    column->stretch = 1;
    column->separator = 0;
    column->idObj = 0;
    column->anchorObj = 0;

    column->headingState = 0;
    column->headingObj = 0;
    column->headingImageObj = 0;
    column->headingAnchorObj = 0;
    column->headingStateObj = 0;
    column->headingCommandObj = 0;

    column->data = 0;
    column->tagset = NULL;
}

static void FreeColumn(TreeColumn *column)
{
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

#define SHOW_TREE 	(0x1) 	/* Show tree column? */
#define SHOW_HEADINGS	(0x2)	/* Show heading row? */

#define DEFAULT_SHOW	"tree headings"

static const char *const showStrings[] = {
    "tree", "headings", NULL
};

static int GetEnumSetFromObj(
    Tcl_Interp *interp,
    Tcl_Obj *objPtr,
    const char *const table[],
    unsigned *resultPtr)
{
    unsigned result = 0;
    Tcl_Size i, objc;
    Tcl_Obj **objv;

    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK)
	return TCL_ERROR;

    for (i = 0; i < objc; ++i) {
	int index;
	if (TCL_OK != Tcl_GetIndexFromObjStruct(interp, objv[i], table,
		sizeof(char *), "value", TCL_EXACT, &index))
	{
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
 * 	columns, columnNames: -columns
 * 	displayColumns:	-columns, -displaycolumns
 * 	headingHeight: [layout]
 * 	rowHeight, indent: style
 */
typedef struct {
    /* Resources acquired at initialization-time:
     */
    Tk_OptionTable itemOptionTable;
    Tk_OptionTable columnOptionTable;
    Tk_OptionTable headingOptionTable;
    Tk_OptionTable displayOptionTable;
    Tk_BindingTable bindingTable;
    Ttk_TagTable tagTable;

    /* Acquired in GetLayout hook:
     */
    Ttk_Layout itemLayout;
    Ttk_Layout cellLayout;
    Ttk_Layout headingLayout;
    Ttk_Layout rowLayout;
    Ttk_Layout separatorLayout;

    int headingHeight;		/* Space for headings */
    int rowHeight;		/* Height of each item */
    int colSeparatorWidth;	/* Width of column separator, if used (screen units) */
    int indent;			/* Horizontal offset for child items (screen units) */

    /* Tree data:
     */
    Tcl_HashTable items;	/* Map: item name -> item */
    int serial;			/* Next item # for autogenerated names */
    TreeItem *root;		/* Root item */

    TreeColumn column0;		/* Column options for display column #0 */
    TreeColumn *columns;	/* Array of column options for data columns */

    TreeItem *focus;		/* Current focus item */
    TreeItem *endPtr;		/* See EndPosition() */

    /* Widget options:
     */
    Tcl_Obj *columnsObj;	/* List of symbolic column names */
    Tcl_Obj *displayColumnsObj;	/* List of columns to display */

    Tcl_Obj *heightObj;		/* height (rows) */
    Tcl_Obj *paddingObj;	/* internal padding */
    Tcl_Size nTitleColumns;	/* -titlecolumns */
    Tcl_Size nTitleItems;		/* -titleitems */
    int striped;		/* -striped option */

    Tcl_Obj *showObj;		/* -show list */
    Tcl_Obj *selectModeObj;	/* -selectmode option */
    Tcl_Obj *selectTypeObj;	/* -selecttype option */

    Scrollable xscroll;
    ScrollHandle xscrollHandle;
    Scrollable yscroll;
    ScrollHandle yscrollHandle;

    /* Derived resources:
     */
    Tcl_HashTable columnNames;	/* Map: column name -> column table entry */
    Tcl_Size nColumns; 		/* #columns */
    Tcl_Size nDisplayColumns;	/* #display columns */
    TreeColumn **displayColumns; /* List of columns for display (incl tree) */
    int titleWidth;		/* Width of non-scrolled columns */
    int titleRows;		/* Height of non-scrolled items, in rows */
    int totalRows;		/* Height of non-hidden items, in rows */
    int rowPosNeedsUpdate;	/* Internal rowPos data needs update */
    Ttk_Box headingArea;	/* Display area for column headings */
    Ttk_Box treeArea;   	/* Display area for tree */
    int slack;			/* Slack space (see Resizing section) */
    unsigned showFlags;		/* bitmask of subparts to display */
} TreePart;

typedef struct {
    WidgetCore core;
    TreePart tree;
} Treeview;

#define USER_MASK 		0x0100
#define COLUMNS_CHANGED 	(USER_MASK)
#define DCOLUMNS_CHANGED	(USER_MASK<<1)
#define SCROLLCMD_CHANGED	(USER_MASK<<2)
#define SHOW_CHANGED 		(USER_MASK<<3)

static const char *const SelectModeStrings[] = { "none", "browse", "extended", NULL };
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
	NULL, TCL_INDEX_NONE, offsetof(Treeview, tree.xscroll.scrollCmd),
	TK_OPTION_NULL_OK, 0, SCROLLCMD_CHANGED},
    {TK_OPTION_STRING, "-yscrollcommand", "yScrollCommand", "ScrollCommand",
	NULL, TCL_INDEX_NONE, offsetof(Treeview, tree.yscroll.scrollCmd),
	TK_OPTION_NULL_OK, 0, SCROLLCMD_CHANGED},

    WIDGET_TAKEFOCUS_TRUE,
    WIDGET_INHERIT_OPTIONS(ttkCoreOptionSpecs)
};

/*------------------------------------------------------------------------
 * +++ Utilities.
 */
typedef void (*HashEntryIterator)(void *hashValue);

static void foreachHashEntry(Tcl_HashTable *ht, HashEntryIterator func)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *entryPtr = Tcl_FirstHashEntry(ht, &search);
    while (entryPtr != NULL) {
	func(Tcl_GetHashValue(entryPtr));
	entryPtr = Tcl_NextHashEntry(&search);
    }
}

static int CellSelectionClear(Treeview *tv)
{
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
 * 	Ensure that a Tcl_Obj * has refcount 1 -- either return objPtr
 * 	itself,	or a duplicated copy.
 */
static Tcl_Obj *unshareObj(Tcl_Obj *objPtr)
{
    if (Tcl_IsShared(objPtr)) {
	Tcl_Obj *newObj = Tcl_DuplicateObj(objPtr);
	Tcl_DecrRefCount(objPtr);
	Tcl_IncrRefCount(newObj);
	return newObj;
    }
    return objPtr;
}

/* DisplayLayout --
 * 	Rebind, place, and draw a layout + object combination.
 */
static void DisplayLayout(
    Ttk_Layout layout, void *recordPtr, Ttk_State state, Ttk_Box b, Drawable d)
{
    Ttk_RebindSublayout(layout, recordPtr);
    Ttk_PlaceLayout(layout, state, b);
    Ttk_DrawLayout(layout, state, d);
}

/* DisplayLayoutTree --
 *	Like DisplayLayout, but for the tree column.
 */
static void DisplayLayoutTree(
    Tk_Anchor imageAnchor, Tk_Anchor textAnchor,
    Ttk_Layout layout, void *recordPtr, Ttk_State state, Ttk_Box b, Drawable d)
{
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
 * 	Look up column by name or number.
 * 	Returns: pointer to column table entry, NULL if not found.
 * 	Leaves an error message in interp->result on error.
 */
static TreeColumn *GetColumn(
    Tcl_Interp *interp, Treeview *tv, Tcl_Obj *columnIDObj)
{
    Tcl_HashEntry *entryPtr;
    Tcl_Size columnIndex;

    /* Check for named column:
     */
    entryPtr = Tcl_FindHashEntry(
	    &tv->tree.columnNames, Tcl_GetString(columnIDObj));
    if (entryPtr) {
	return (TreeColumn *)Tcl_GetHashValue(entryPtr);
    }

    /* Check for index:
     */
    if (TkGetIntForIndex(columnIDObj, tv->tree.nColumns - 1, 1, &columnIndex) == TCL_OK) {
	if (columnIndex < 0 || columnIndex >= tv->tree.nColumns) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "Column index \"%s\" out of bounds",
		    Tcl_GetString(columnIDObj)));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "COLBOUND", NULL);
	    return NULL;
	}

	return tv->tree.columns + columnIndex;
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	"Invalid column index %s", Tcl_GetString(columnIDObj)));
    Tcl_SetErrorCode(interp, "TTK", "TREE", "COLUMN", NULL);
    return NULL;
}

/* + FindColumn --
 * 	Look up column by name, number, or display index.
 */
static TreeColumn *FindColumn(
    Tcl_Interp *interp, Treeview *tv, Tcl_Obj *columnIDObj)
{
    Tcl_WideInt colno;

    if (sscanf(Tcl_GetString(columnIDObj), "#%" TCL_LL_MODIFIER "d", &colno) == 1)
    {	/* Display column specification, #n */
	if (colno >= 0 && colno < tv->tree.nDisplayColumns) {
	    return tv->tree.displayColumns[colno];
	}
	/* else */
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "Column %s out of range", Tcl_GetString(columnIDObj)));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "COLUMN", NULL);
	return NULL;
    }

    return GetColumn(interp, tv, columnIDObj);
}

/* + FindItem --
 * 	Locates the item with the specified identifier in the tree.
 * 	If there is no such item, leaves an error message in interp.
 */
static TreeItem *FindItem(
    Tcl_Interp *interp, Treeview *tv, Tcl_Obj *itemNameObj)
{
    const char *itemName = Tcl_GetString(itemNameObj);
    Tcl_HashEntry *entryPtr =  Tcl_FindHashEntry(&tv->tree.items, itemName);

    if (!entryPtr) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"Item %s not found", itemName));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "ITEM", NULL);
	return 0;
    }
    return (TreeItem *)Tcl_GetHashValue(entryPtr);
}

/* + GetItemListFromObj --
 * 	Parse a Tcl_Obj * as a list of items.
 * 	Returns a NULL-terminated array of items; result must
 * 	be ckfree()d. On error, returns NULL and leaves an error
 * 	message in interp.
 */

static TreeItem **GetItemListFromObj(
    Tcl_Interp *interp, Treeview *tv, Tcl_Obj *objPtr)
{
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
 * 	Returns the item's ID.
 */
static const char *ItemName(Treeview *tv, TreeItem *item)
{
    return (const char *)Tcl_GetHashKey(&tv->tree.items, item->entryPtr);
}

/* + ItemID --
 * 	Returns a fresh Tcl_Obj * (refcount 0) holding the
 * 	item identifier of the specified item.
 */
static Tcl_Obj *ItemID(Treeview *tv, TreeItem *item)
{
    return Tcl_NewStringObj(ItemName(tv, item), -1);
}

/*------------------------------------------------------------------------
 * +++ Column configuration.
 */

/* + TreeviewFreeColumns --
 * 	Free column data.
 */
static void TreeviewFreeColumns(Treeview *tv)
{
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
static int TreeviewInitColumns(Tcl_Interp *interp, Treeview *tv)
{
    Tcl_Obj **columns;
    Tcl_Size i, ncols;

    if (Tcl_ListObjGetElements(
	    interp, tv->tree.columnsObj, &ncols, &columns) != TCL_OK)
    {
	return TCL_ERROR;
    }

    /*
     * Free old values:
     */
    TreeviewFreeColumns(tv);

    /*
     * Initialize columns array and columnNames hash table:
     */
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
 * 	Initializes the 'displayColumns' array.
 *
 * 	Note that displayColumns[0] is always the tree column,
 * 	even when SHOW_TREE is not set.
 *
 * @@@ TODO: disallow duplicated columns
 */
static int TreeviewInitDisplayColumns(Tcl_Interp *interp, Treeview *tv)
{
    Tcl_Obj **dcolumns;
    Tcl_Size index, ndcols;
    TreeColumn **displayColumns = 0;

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

    if (tv->tree.displayColumns)
	ckfree(tv->tree.displayColumns);
    tv->tree.displayColumns = displayColumns;
    tv->tree.nDisplayColumns = ndcols + 1;

    return TCL_OK;
}

/*------------------------------------------------------------------------
 * +++ Resizing.
 * 	slack invariant: TreeWidth(tree) + slack = treeArea.width
 */

#define FirstColumn(tv)  ((tv->tree.showFlags&SHOW_TREE) ? 0 : 1)

/* + TreeWidth --
 * 	Compute the requested tree width from the sum of visible column widths.
 */
static int TreeWidth(Treeview *tv)
{
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
static void RecomputeSlack(Treeview *tv)
{
    tv->tree.slack = tv->tree.treeArea.width - TreeWidth(tv);
}

/* + PickupSlack/DepositSlack --
 * 	When resizing columns, distribute extra space to 'slack' first,
 * 	and only adjust column widths if 'slack' goes to zero.
 * 	That is, don't bother changing column widths if the tree
 * 	is already scrolled or short.
 */
static int PickupSlack(Treeview *tv, int extra)
{
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

static void DepositSlack(Treeview *tv, int extra)
{
    tv->tree.slack += extra;
}

/* + Stretch --
 * 	Adjust width of column by N pixels, down to minimum width.
 * 	Returns: #pixels actually moved.
 */
static int Stretch(TreeColumn *c, int n)
{
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
 * 	Adjust width of (stretchable) columns to the left by N pixels.
 * 	Returns: leftover slack.
 */
static int ShoveLeft(Treeview *tv, Tcl_Size i, int n)
{
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
 * 	Adjust width of (stretchable) columns to the right by N pixels.
 * 	Returns: leftover slack.
 */
static int ShoveRight(Treeview *tv, Tcl_Size i, int n)
{
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
 * 	Distribute n pixels evenly across all stretchable display columns.
 * 	Returns: leftover slack.
 * Notes:
 * 	The "((++w % m) < r)" term is there so that the remainder r = n % m
 * 	is distributed round-robin.
 */
static int DistributeWidth(Treeview *tv, int n)
{
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
 * 	Recompute column widths based on available width.
 * 	Pick up slack first;
 * 	Distribute the remainder evenly across stretchable columns;
 * 	If any is still left over due to minwidth constraints, shove left.
 */
static void ResizeColumns(Treeview *tv, int newWidth)
{
    int delta = newWidth - (TreeWidth(tv) + tv->tree.slack);
    DepositSlack(tv,
	ShoveLeft(tv, tv->tree.nDisplayColumns - 1,
	    DistributeWidth(tv, PickupSlack(tv, delta))));
}

/* + DragColumn --
 * 	Move the separator to the right of specified column,
 * 	adjusting other column widths as necessary.
 */
static void DragColumn(Treeview *tv, Tcl_Size i, int delta)
{
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
 * 	Get Row and Column from a cell ID.
 */
static int GetCellFromObj(
    Tcl_Interp *interp, Treeview *tv, Tcl_Obj *obj,
    int displayColumnOnly, int *displayColumn,
    TreeCell *cell)
{
    Tcl_Size nElements;
    Tcl_Obj **elements;

    if (Tcl_ListObjGetElements(interp, obj, &nElements, &elements) != TCL_OK) {
	return TCL_ERROR;
    }
    if (nElements != 2) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"Cell id must be a list of two elements", -1));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "CELL", NULL);
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
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "CELL", NULL);
	    return TCL_ERROR;
	}
	if (displayColumn != NULL) {
	    *displayColumn = i;
	}
    }
    return TCL_OK;
}

/* + GetCellListFromObj --
 * 	Parse a Tcl_Obj * as a list of cells.
 * 	Returns an array of cells; result must be ckfree()d.
 *      On error, returns NULL and leaves an error
 * 	message in interp.
 */

static TreeCell *GetCellListFromObj(
	Tcl_Interp *interp, Treeview *tv, Tcl_Obj *objPtr, Tcl_Size *nCells)
{
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
    | VirtualEventMask
    ;

static void TreeviewBindEventProc(void *clientData, XEvent *event)
{
    Treeview *tv = (Treeview *)clientData;
    TreeItem *item = NULL;
    Ttk_TagSet tagset;
    int unused;
    Tcl_Size colno = TCL_INDEX_NONE;
    TreeColumn *column = NULL;

    /*
     * Figure out where to deliver the event.
     */
    switch (event->type)
    {
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

    /*
     * Pick up any cell tags.
     */
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

    /*
     * Fire binding:
     */
    Tcl_Preserve(clientData);
    Tk_BindEvent(tv->tree.bindingTable, event, tv->core.tkwin,
	    tagset->nTags, (void **)tagset->tags);
    Tcl_Release(clientData);

    Ttk_FreeTagSet(tagset);
}

/*------------------------------------------------------------------------
 * +++ Initialization and cleanup.
 */

static void TreeviewInitialize(Tcl_Interp *interp, void *recordPtr)
{
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

    tv->tree.focus = tv->tree.endPtr = 0;

    /* Create root item "":
     */
    tv->tree.root = NewItem();
    Tk_InitOptions(interp, tv->tree.root,
	tv->tree.itemOptionTable, tv->core.tkwin);
    tv->tree.root->tagset = Ttk_GetTagSetFromObj(NULL, tv->tree.tagTable, NULL);
    tv->tree.root->entryPtr = Tcl_CreateHashEntry(&tv->tree.items, "", &unused);
    Tcl_SetHashValue(tv->tree.root->entryPtr, tv->tree.root);

    /* Scroll handles:
     */
    tv->tree.xscrollHandle = TtkCreateScrollHandle(&tv->core,&tv->tree.xscroll);
    tv->tree.yscrollHandle = TtkCreateScrollHandle(&tv->core,&tv->tree.yscroll);

    /* Size parameters:
     */
    tv->tree.treeArea = tv->tree.headingArea = Ttk_MakeBox(0,0,0,0);
    tv->tree.slack = 0;
}

static void TreeviewCleanup(void *recordPtr)
{
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

    if (tv->tree.displayColumns)
	ckfree(tv->tree.displayColumns);

    foreachHashEntry(&tv->tree.items, FreeItemCB);
    Tcl_DeleteHashTable(&tv->tree.items);

    TtkFreeScrollHandle(tv->tree.xscrollHandle);
    TtkFreeScrollHandle(tv->tree.yscrollHandle);
}

/* + TreeviewConfigure --
 * 	Configuration widget hook.
 *
 * 	BUG: If user sets -columns and -displaycolumns, but -displaycolumns
 * 	has an error, the widget is left in an inconsistent state.
 */
static int
TreeviewConfigure(Tcl_Interp *interp, void *recordPtr, int mask)
{
    Treeview *tv = (Treeview *)recordPtr;
    unsigned showFlags = tv->tree.showFlags;

    if (mask & COLUMNS_CHANGED) {
	if (TreeviewInitColumns(interp, tv) != TCL_OK)
	    return TCL_ERROR;
	mask |= DCOLUMNS_CHANGED;
    }
    if (mask & DCOLUMNS_CHANGED) {
	if (TreeviewInitDisplayColumns(interp, tv) != TCL_OK)
	    return TCL_ERROR;
    }
    if (mask & COLUMNS_CHANGED) {
	CellSelectionClear(tv);
    }
    if (tv->tree.nTitleColumns < 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                "\"#%" TCL_SIZE_MODIFIER "d\" is out of range",
                tv->tree.nTitleColumns));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "TITLECOLUMNS", NULL);
	return TCL_ERROR;
    }
    if (tv->tree.nTitleItems < 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                "\"%" TCL_SIZE_MODIFIER "d\" is out of range",
                tv->tree.nTitleItems));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "TITLEITEMS", NULL);
	return TCL_ERROR;
    }
    if (mask & SCROLLCMD_CHANGED) {
	TtkScrollbarUpdateRequired(tv->tree.xscrollHandle);
	TtkScrollbarUpdateRequired(tv->tree.yscrollHandle);
    }
    if ((mask & SHOW_CHANGED)
	    && GetEnumSetFromObj(
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
 * 	Set item options.
 */
static int ConfigureItem(
    Tcl_Interp *interp, Treeview *tv, TreeItem *item,
    Tcl_Size objc, Tcl_Obj *const objv[])
{
    Tk_SavedOptions savedOptions;
    int mask;
    Ttk_ImageSpec *newImageSpec = NULL;
    Ttk_TagSet newTagSet = NULL;

    if (Tk_SetOptions(interp, item, tv->tree.itemOptionTable,
		objc, objv, tv->core.tkwin, &savedOptions, &mask)
		!= TCL_OK)
    {
	return TCL_ERROR;
    }

    /* Make sure that -values is a valid list:
     */
    if (item->valuesObj) {
	Tcl_Size unused;
	if (Tcl_ListObjLength(interp, item->valuesObj, &unused) != TCL_OK)
	    goto error;
    }

    /* Check -height
     */
    if (item->height < 1) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"Invalid item height %d", item->height));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "HEIGHT", NULL);
	goto error;
    }

    /* Check -image.
     */
    if ((mask & ITEM_OPTION_IMAGE_CHANGED) && item->imageObj) {
	newImageSpec = TtkGetImageSpec(interp, tv->core.tkwin, item->imageObj);
	if (!newImageSpec) {
	    goto error;
	}
    }

    /* Check -tags.
     * Side effect: may create new tags.
     */
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
	if (Tcl_GetBooleanFromObj(interp, item->openObj, &isOpen) != TCL_OK)
	    goto error;
	if (isOpen)
	    item->state |= TTK_STATE_OPEN;
	else
	    item->state &= ~TTK_STATE_OPEN;
    }

    /* All OK.
     */
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
 * 	Set column options.
 */
static int ConfigureColumn(
    Tcl_Interp *interp, Treeview *tv, TreeColumn *column,
    Tcl_Size objc, Tcl_Obj *const objv[])
{
    Tk_SavedOptions savedOptions;
    int mask;

    if (Tk_SetOptions(interp, column,
	    tv->tree.columnOptionTable, objc, objv, tv->core.tkwin,
	    &savedOptions,&mask) != TCL_OK)
    {
	return TCL_ERROR;
    }

    if (mask & READONLY_OPTION) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"Attempt to change read-only option", -1));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "READONLY", NULL);
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
 * 	Set heading options.
 */
static int ConfigureHeading(
    Tcl_Interp *interp, Treeview *tv, TreeColumn *column,
    Tcl_Size objc, Tcl_Obj *const objv[])
{
    Tk_SavedOptions savedOptions;
    int mask;

    if (Tk_SetOptions(interp, column,
	    tv->tree.headingOptionTable, objc, objv, tv->core.tkwin,
	    &savedOptions,&mask) != TCL_OK)
    {
	return TCL_ERROR;
    }

    /* @@@ testing ... */
    if ((mask & STATE_CHANGED) && column->headingStateObj) {
	Ttk_StateSpec stateSpec;
	if (Ttk_GetStateSpecFromObj(
		interp, column->headingStateObj, &stateSpec) != TCL_OK)
	{
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
 * 	Update position data for all visible items.
 */
static void UpdatePositionItem(
    Treeview *tv, TreeItem *item, int hidden,
    int *rowPos, int *itemPos, int *visiblePos)
{
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
 * 	Update position data for all visible items.
 */
static void UpdatePositionTree(Treeview *tv)
{
    /* -1 for the invisible root */
    int rowPos = -1, itemPos = -1, visiblePos = -1;
    tv->tree.titleRows = 0;
    UpdatePositionItem(tv, tv->tree.root, 0, &rowPos, &itemPos, &visiblePos);
    tv->tree.totalRows = rowPos;
    tv->tree.rowPosNeedsUpdate = 0;
}

/* + IdentifyItem --
 * 	Locate the item at the specified y position, if any.
 */
static TreeItem *IdentifyItem(Treeview *tv, int y)
{
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
 * 	Returns the display column number at the specified x position,
 * 	or -1 if x is outside any columns.
 */
static Tcl_Size IdentifyDisplayColumn(Treeview *tv, int x, int *x1)
{
    Tcl_Size colno = FirstColumn(tv);
    int xpos = tv->tree.treeArea.x;

    if (tv->tree.nTitleColumns <= colno) {
	xpos -= tv->tree.xscroll.first;
    }

    while (colno < tv->tree.nDisplayColumns) {
	TreeColumn *column = tv->tree.displayColumns[colno];
	int next_xpos = xpos + column->width;
	if (xpos <= x && x <= next_xpos + HALO) {
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
 * 	The depth of an item is equal to the number of proper ancestors,
 * 	not counting the root node.
 */
static int ItemDepth(TreeItem *item)
{
    int depth = 0;
    while (item->parent) {
	++depth;
	item = item->parent;
    }
    return depth-1;
}

/* + DisplayRow --
 * 	Returns the position row has on screen, or -1 if off-screen.
 */
static int DisplayRow(int row, Treeview *tv)
{
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

/* + BoundingBox --
 * 	Compute the parcel of the specified column of the specified item,
 *	(or the entire item if column is NULL)
 *	Returns: 0 if item or column is not viewable, 1 otherwise.
 */
static int BoundingBox(
    Treeview *tv,		/* treeview widget */
    TreeItem *item,		/* desired item */
    TreeColumn *column,		/* desired column */
    Ttk_Box *bbox_rtn)		/* bounding box of item */
{
    int dispRow;
    Ttk_Box bbox = tv->tree.treeArea;

    if (tv->tree.rowPosNeedsUpdate) {
	UpdatePositionTree(tv);
    }
    dispRow = DisplayRow(item->rowPos, tv);
    if (dispRow < 0) {
	/* not viewable, or off-screen */
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

	/* Account for indentation in tree column:
	 */
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

static TreeRegion IdentifyRegion(Treeview *tv, int x, int y)
{
    int x1 = 0;
    Tcl_Size colno = IdentifyDisplayColumn(tv, x, &x1);

    if (Ttk_BoxContains(tv->tree.headingArea, x, y)) {
	if (colno < 0) {
	    return REGION_NOTHING;
	} else if (-HALO <= x1 - x  && x1 - x <= HALO) {
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
 * 	Utility routine; acquires a sublayout for items, cells, etc.
 */
static Ttk_Layout GetSublayout(
    Tcl_Interp *interp,
    Ttk_Theme themePtr,
    Ttk_Layout parentLayout,
    const char *layoutName,
    Tk_OptionTable optionTable,
    Ttk_Layout *layoutPtr)
{
    Ttk_Layout newLayout = Ttk_CreateSublayout(
	    interp, themePtr, parentLayout, layoutName, optionTable);

    if (newLayout) {
	if (*layoutPtr)
	    Ttk_FreeLayout(*layoutPtr);
	*layoutPtr = newLayout;
    }
    return newLayout;
}

/* + TreeviewGetLayout --
 * 	GetLayout() widget hook.
 */
static Ttk_Layout TreeviewGetLayout(
    Tcl_Interp *interp, Ttk_Theme themePtr, void *recordPtr)
{
    Treeview *tv = (Treeview *)recordPtr;
    Ttk_Layout treeLayout = TtkWidgetGetLayout(interp, themePtr, recordPtr);
    Tcl_Obj *objPtr;
    int unused, cellHeight;
    DisplayItem displayItem;
    Ttk_Style style;

    if (!(
	treeLayout
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

    /* Compute heading height.
     */
    Ttk_RebindSublayout(tv->tree.headingLayout, &tv->tree.column0);
    Ttk_LayoutSize(tv->tree.headingLayout, 0, &unused, &tv->tree.headingHeight);

    /* Get row height from style, or compute it to fit Item and Cell.
     * Pick up default font from the Treeview style.
     */
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
    tv->tree.rowHeight = MAX(tv->tree.rowHeight, 1);

    if ((objPtr = Ttk_QueryOption(treeLayout, "-columnseparatorwidth", 0))) {
	(void)Tk_GetPixelsFromObj(NULL, tv->core.tkwin, objPtr, &tv->tree.colSeparatorWidth);
    }

    /* Get item indent from style:
     */
    tv->tree.indent = DEFAULT_INDENT;
    if ((objPtr = Ttk_QueryOption(treeLayout, "-indent", 0))) {
	(void)Tk_GetPixelsFromObj(NULL, tv->core.tkwin, objPtr, &tv->tree.indent);
    }

    return treeLayout;
}

/* + TreeviewDoLayout --
 * 	DoLayout() widget hook.  Computes widget layout.
 *
 * Side effects:
 * 	Computes headingArea and treeArea.
 * 	Computes subtree height.
 * 	Invokes scroll callbacks.
 */
static void TreeviewDoLayout(void *clientData)
{
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
    if (tv->tree.treeArea.height % tv->tree.rowHeight) {
        /* When the treeview height doesn't correspond to an exact number
         * of rows, the last row count must be incremented to draw a
         * partial row at the bottom. The total row count must also be
         * incremented to be able to scroll all the way to the bottom.
         */
        last++;
        total++;
    }
    TtkScrolled(tv->tree.yscrollHandle, first, last, total);
}

/* + TreeviewSize --
 * 	SizeProc() widget hook.  Size is determined by
 * 	-height option and column widths.
 */
static int TreeviewSize(void *clientData, int *widthPtr, int *heightPtr)
{
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
 * 	Returns the state of the specified item, based
 * 	on widget state, item state, and other information.
 */
static Ttk_State ItemState(Treeview *tv, TreeItem *item)
{
    Ttk_State state = tv->core.state | item->state;
    if (!item->children)
	state |= TTK_STATE_LEAF;
    if (item != tv->tree.focus)
	state &= ~TTK_STATE_FOCUS;
    return state;
}

/* + DrawHeadings --
 *	Draw tree headings.
 */
static void DrawHeadings(Treeview *tv, Drawable d)
{
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
static void DrawSeparators(Treeview *tv, Drawable d)
{
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
 * 	Each level of settings might add stripedbackground, and it should
 * 	override background if this is indeed on a striped item.
 * 	By copying it between each level, and NULL-ing stripedBgObj,
 * 	it can be detected if the next level overrides it.
 */
 static void OverrideStriped(
    Treeview *tv, TreeItem *item, DisplayItem *displayItem)
{
    int striped = item->visiblePos % 2 && tv->tree.striped;
    if (striped && displayItem->stripedBgObj) {
	displayItem->backgroundObj = displayItem->stripedBgObj;
	displayItem->stripedBgObj = NULL;
    }
}

/* + PrepareItem --
 * 	Fill in a displayItem record.
 */
static void PrepareItem(
    Treeview *tv, TreeItem *item, DisplayItem *displayItem, Ttk_State state)
{
    Ttk_Style style = Ttk_LayoutStyle(tv->core.layout);

    Ttk_TagSetDefaults(tv->tree.tagTable, style, displayItem);
    OverrideStriped(tv, item, displayItem);
    Ttk_TagSetValues(tv->tree.tagTable, item->tagset, displayItem);
    OverrideStriped(tv, item, displayItem);
    Ttk_TagSetApplyStyle(tv->tree.tagTable, style, state, displayItem);
}

/* Fill in data from item to temporary storage in columns. */
static void PrepareCells(
   Treeview *tv, TreeItem *item)
{
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
    Drawable d, int x, int y, int title)
{
    Ttk_Layout layout = tv->tree.cellLayout;
    Ttk_Style style = Ttk_LayoutStyle(tv->core.layout);
    Ttk_State state = ItemState(tv, item);
    Ttk_Padding cellPadding = {4, 0, 4, 0};
    DisplayItem displayItemLocal;
    DisplayItem displayItemCell, displayItemCellSel;
    int rowHeight = tv->tree.rowHeight * item->height;
    int xPad = 0, defaultPadding = 1;
    Tcl_Size i;

    /* Adjust if the tree column has a separator */
    if (tv->tree.showFlags & SHOW_TREE && tv->tree.column0.separator) {
	xPad = tv->tree.colSeparatorWidth/2;
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
 * 	Draw an item (row background, tree label, and cells).
 */
static void DrawItem(
    Treeview *tv, TreeItem *item, Drawable d, int depth)
{
    Ttk_Style style = Ttk_LayoutStyle(tv->core.layout);
    Ttk_State state = ItemState(tv, item);
    DisplayItem displayItem, displayItemSel, displayItemLocal;
    int rowHeight = tv->tree.rowHeight * item->height;
    int x = tv->tree.treeArea.x - tv->tree.xscroll.first;
    int xTitle = tv->tree.treeArea.x;
    int dispRow = DisplayRow(item->rowPos, tv);
    int y = tv->tree.treeArea.y + tv->tree.rowHeight * dispRow;

    PrepareItem(tv, item, &displayItem, state);
    PrepareItem(tv, item, &displayItemSel, state | TTK_STATE_SELECTED);

    /* Draw row background:
     */
    {
	Ttk_Box rowBox = Ttk_MakeBox(x, y, TreeWidth(tv), rowHeight);
	DisplayLayout(tv->tree.rowLayout, &displayItem, state, rowBox, d);
    }

    /* Make room for tree label:
     */
    if (tv->tree.showFlags & SHOW_TREE) {
	x += tv->tree.column0.width;
    }

    /* Draw data cells:
     */
    PrepareCells(tv, item);
    DrawCells(tv, item, &displayItem, &displayItemSel, d, x, y, 0);

    /* Draw row background for non-scrolled area:
     */
    if (tv->tree.nTitleColumns >= 1) {
	Ttk_Box rowBox = Ttk_MakeBox(tv->tree.treeArea.x, y,
		tv->tree.titleWidth, rowHeight);
	DisplayLayout(tv->tree.rowLayout, &displayItem, state, rowBox, d);
    }

    /* Draw tree label:
     */
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

    /* Draw non-scrolled data cells:
     */
    if (tv->tree.nTitleColumns > 1) {
	DrawCells(tv, item, &displayItem, &displayItemSel, d, xTitle, y, 1);
    }
}

/* + DrawSubtree --
 * 	Draw an item and all of its (viewable) descendants.
 */

static void DrawForest(	/* forward */
    Treeview *tv, TreeItem *item, Drawable d, int depth);

static void DrawSubtree(
    Treeview *tv, TreeItem *item, Drawable d, int depth)
{
    int dispRow = DisplayRow(item->rowPos, tv);
    if (dispRow >= 0) {
	DrawItem(tv, item, d, depth);
    }

    if (item->state & TTK_STATE_OPEN) {
	DrawForest(tv, item->children, d, depth + 1);
    }
}

/* + DrawForest --
 * 	Draw a sequence of items and their visible descendants.
 */
static void DrawForest(
    Treeview *tv, TreeItem *item, Drawable d, int depth)
{
    while (item) {
        DrawSubtree(tv, item, d, depth);
	item = item->next;
    }
}

/* + TreeviewDisplay --
 * 	Display() widget hook.  Draw the widget contents.
 */
static void TreeviewDisplay(void *clientData, Drawable d)
{
    Treeview *tv = (Treeview *)clientData;

    Ttk_DrawLayout(tv->core.layout, tv->core.state, d);
    if (tv->tree.showFlags & SHOW_HEADINGS) {
	DrawHeadings(tv, d);
    }
    DrawForest(tv, tv->tree.root->children, d, 0);
    DrawSeparators(tv, d);
}

/*------------------------------------------------------------------------
 * +++ Utilities for widget commands
 */

/* + InsertPosition --
 * 	Locate the previous sibling for [$tree insert].
 *
 * 	Returns a pointer to the item just before the specified index,
 * 	or 0 if the item is to be inserted at the beginning.
 */
static TreeItem *InsertPosition(TreeItem *parent, int index)
{
    TreeItem *prev = 0, *next = parent->children;

    while (next != 0 && index > 0) {
	--index;
	prev = next;
	next = prev->next;
    }

    return prev;
}

/* + EndPosition --
 * 	Locate the last child of the specified node.
 *
 * 	To avoid quadratic-time behavior in the common cases
 * 	where the treeview is populated in breadth-first or
 * 	depth-first order using [$tv insert $parent end ...],
 * 	we cache the result from the last call to EndPosition()
 * 	and start the search from there on a cache hit.
 *
 */
static TreeItem *EndPosition(Treeview *tv, TreeItem *parent)
{
    TreeItem *endPtr = tv->tree.endPtr;

    while (endPtr && endPtr->parent != parent) {
	endPtr = endPtr->parent;
    }
    if (!endPtr) {
	endPtr = parent->children;
    }

    if (endPtr) {
	while (endPtr->next) {
	    endPtr = endPtr->next;
	}
	tv->tree.endPtr = endPtr;
    }

    return endPtr;
}

/* + AncestryCheck --
 * 	Verify that specified item is not an ancestor of the specified parent;
 * 	returns 1 if OK, 0 and leaves an error message in interp otherwise.
 */
static int AncestryCheck(
    Tcl_Interp *interp, Treeview *tv, TreeItem *item, TreeItem *parent)
{
    TreeItem *p = parent;
    while (p) {
	if (p == item) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "Cannot insert %s as descendant of %s",
		    ItemName(tv, item), ItemName(tv, parent)));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "ANCESTRY", NULL);
	    return 0;
	}
	p = p->parent;
    }
    return 1;
}

/* + DeleteItems --
 * 	Remove an item and all of its descendants from the hash table
 * 	and detach them from the tree; returns a linked list (chained
 * 	along the ->next pointer) of deleted items.
 */
static TreeItem *DeleteItems(TreeItem *item, TreeItem *delq)
{
    if (item->entryPtr) {
	DetachItem(item);
	while (item->children) {
	    delq = DeleteItems(item->children, delq);
	}
	Tcl_DeleteHashEntry(item->entryPtr);
	item->entryPtr = 0;
	item->next = delq;
	delq = item;
    } /* else -- item has already been unlinked */
    return delq;
}

/*------------------------------------------------------------------------
 * +++ Widget commands -- item inquiry.
 */

/* + $tv children $item ?newchildren? --
 * 	Return the list of children associated with $item
 */
static int TreeviewChildrenCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;
    Tcl_Obj *result;

    if (objc < 3 || objc > 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "item ?newchildren?");
	return TCL_ERROR;
    }
    item = FindItem(interp, tv, objv[2]);
    if (!item) {
	return TCL_ERROR;
    }

    if (objc == 3) {
	result = Tcl_NewListObj(0,0);
	for (item = item->children; item; item = item->next) {
	    Tcl_ListObjAppendElement(interp, result, ItemID(tv, item));
	}
	Tcl_SetObjResult(interp, result);
    } else {
	TreeItem **newChildren = GetItemListFromObj(interp, tv, objv[3]);
	TreeItem *child;
	int i;

	if (!newChildren)
	    return TCL_ERROR;

	/* Sanity-check:
	 */
	for (i = 0; newChildren[i]; ++i) {
	    if (!AncestryCheck(interp, tv, newChildren[i], item)) {
		ckfree(newChildren);
		return TCL_ERROR;
	    }
	}

	/* Detach old children:
	 */
	child = item->children;
	while (child) {
	    TreeItem *next = child->next;
	    DetachItem(child);
	    child = next;
	}

	/* Detach new children from their current locations:
	 */
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
		 * inserted.  Ignore it.
		 */
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

/* + $tv parent $item --
 * 	Return the item ID of $item's parent.
 */
static int TreeviewParentCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
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

    if (item->parent) {
	Tcl_SetObjResult(interp, ItemID(tv, item->parent));
    } else {
	/* This is the root item.  @@@ Return an error? */
	Tcl_ResetResult(interp);
    }

    return TCL_OK;
}

/* + $tv next $item
 * 	Return the ID of $item's next sibling.
 */
static int TreeviewNextCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
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
	Tcl_SetObjResult(interp, ItemID(tv, item->next));
    } /* else -- leave interp-result empty */

    return TCL_OK;
}

/* + $tv prev $item
 * 	Return the ID of $item's previous sibling.
 */
static int TreeviewPrevCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
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
	Tcl_SetObjResult(interp, ItemID(tv, item->prev));
    } /* else -- leave interp-result empty */

    return TCL_OK;
}

/* + $tv index $item --
 * 	Return the index of $item within its parent.
 */
static int TreeviewIndexCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;
    Tcl_Size index = 0;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "item");
	return TCL_ERROR;
    }
    item = FindItem(interp, tv, objv[2]);
    if (!item) {
	return TCL_ERROR;
    }

    while (item->prev) {
	++index;
	item = item->prev;
    }

    Tcl_SetObjResult(interp, TkNewIndexObj(index));
    return TCL_OK;
}

/* + $tv exists $itemid --
 * 	Test if the specified item id is present in the tree.
 */
static int TreeviewExistsCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    Tcl_HashEntry *entryPtr;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "itemid");
	return TCL_ERROR;
    }

    entryPtr = Tcl_FindHashEntry(&tv->tree.items, Tcl_GetString(objv[2]));
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(entryPtr != 0));
    return TCL_OK;
}

/* + $tv bbox $itemid ?$column? --
 * 	Return bounding box [x y width height] of specified item.
 */
static int TreeviewBBoxCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item = 0;
    TreeColumn *column = 0;
    Ttk_Box bbox;

    if (objc < 3 || objc > 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "itemid ?column");
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
 * 	Implements the old, horrible, 2-argument form of [$tv identify].
 *
 * Returns: one of
 * 	heading #n
 * 	cell itemid #n
 * 	item itemid element
 * 	row itemid
 */
static int TreeviewHorribleIdentify(
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size), /* objc */
    Tcl_Obj *const objv[],
    Treeview *tv)
{
    const char *what = "nothing", *detail = NULL;
    TreeItem *item = 0;
    Tcl_Obj *result;
    Tcl_Size dColumnNumber;
    char dcolbuf[32];
    int x, y, x1;

    /* ASSERT: objc == 4 */

    if (Tcl_GetIntFromObj(interp, objv[2], &x) != TCL_OK
	    || Tcl_GetIntFromObj(interp, objv[3], &y) != TCL_OK) {
	return TCL_ERROR;
    }

    dColumnNumber = IdentifyDisplayColumn(tv, x, &x1);
    if (dColumnNumber < 0) {
	goto done;
    }
    snprintf(dcolbuf, sizeof(dcolbuf), "#%" TCL_SIZE_MODIFIER "d", dColumnNumber);

    if (Ttk_BoxContains(tv->tree.headingArea,x,y)) {
	if (-HALO <= x1 - x  && x1 - x <= HALO) {
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
    result = Tcl_NewListObj(0,0);
    Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj(what, -1));
    if (item)
	Tcl_ListObjAppendElement(NULL, result, ItemID(tv, item));
    if (detail)
	Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj(detail, -1));

    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/* + $tv identify $component $x $y --
 * 	Identify the component at position x,y.
 */

static int TreeviewIdentifyCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    static const char *const submethodStrings[] =
	 { "region", "item", "column", "row", "element", "cell", NULL };
    enum { I_REGION, I_ITEM, I_COLUMN, I_ROW, I_ELEMENT, I_CELL };

    Treeview *tv = (Treeview *)recordPtr;
    int submethod;
    int x, y;

    TreeRegion region;
    Ttk_Box bbox;
    TreeItem *item;
    TreeColumn *column = 0;
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
        || Tcl_GetIntFromObj(interp, objv[3], &x) != TCL_OK
	|| Tcl_GetIntFromObj(interp, objv[4], &y) != TCL_OK
    ) {
	return TCL_ERROR;
    }

    region = IdentifyRegion(tv, x, y);
    item = IdentifyItem(tv, y);
    colno = IdentifyDisplayColumn(tv, x, &x1);
    column = (colno >= 0) ?  tv->tree.displayColumns[colno] : NULL;

    switch (submethod)
    {
	case I_REGION :
	    Tcl_SetObjResult(interp,Tcl_NewStringObj(regionStrings[region],-1));
	    break;

	case I_ITEM :
	case I_ROW :
	    if (item) {
		Tcl_SetObjResult(interp, ItemID(tv, item));
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
		elem[0] = ItemID(tv, item);
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
 * 	Query or configure item options.
 */
static int TreeviewItemCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "item ?-option ?value??...");
	return TCL_ERROR;
    }
    if (!(item = FindItem(interp, tv, objv[2]))) {
	return TCL_ERROR;
    }

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
 * 	Column data accessor
 */
static int TreeviewColumnCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
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
 * 	Heading data accessor
 */
static int TreeviewHeadingCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
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
 * 	Query or configure cell values
 */
static int TreeviewSetCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;
    TreeColumn *column;
    Tcl_Size columnNumber;

    if (objc < 3 || objc > 5) {
	Tcl_WrongNumArgs(interp, 2, objv, "item ?column ?value??");
	return TCL_ERROR;
    }
    if (!(item = FindItem(interp, tv, objv[2])))
	return TCL_ERROR;

    /* Make sure -values exists:
     */
    if (!item->valuesObj) {
	item->valuesObj = Tcl_NewListObj(0,0);
	Tcl_IncrRefCount(item->valuesObj);
    }

    if (objc == 3) {
	/* Return dictionary:
	 */
	Tcl_Obj *result = Tcl_NewListObj(0,0);
	Tcl_Obj *value;
	for (columnNumber = 0; columnNumber < tv->tree.nColumns; ++columnNumber) {
	    Tcl_ListObjIndex(interp, item->valuesObj, columnNumber, &value);
	    if (value) {
		Tcl_ListObjAppendElement(NULL, result,
			tv->tree.columns[columnNumber].idObj);
		Tcl_ListObjAppendElement(NULL, result, value);
	    }
	}
	Tcl_SetObjResult(interp, result);
	return TCL_OK;
    }

    /* else -- get or set column
     */
    if (!(column = FindColumn(interp, tv, objv[3])))
	return TCL_ERROR;

    if (column == &tv->tree.column0) {
	/* @@@ Maybe set -text here instead? */
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"Display column #0 cannot be set", -1));
	Tcl_SetErrorCode(interp, "TTK", "TREE", "COLUMN_0", NULL);
	return TCL_ERROR;
    }

    /* Note: we don't do any error checking in the list operations,
     * since item->valuesObj is guaranteed to be a list.
     */
    columnNumber = column - tv->tree.columns;

    if (objc == 4) {	/* get column */
	Tcl_Obj *result = 0;
	Tcl_ListObjIndex(interp, item->valuesObj, columnNumber, &result);
	if (!result) {
	    result = Tcl_NewStringObj("",0);
	}
	Tcl_SetObjResult(interp, result);
	return TCL_OK;
    } else {		/* set column */
	Tcl_Size length;

	item->valuesObj = unshareObj(item->valuesObj);

	/* Make sure -values is fully populated:
	 */
	Tcl_ListObjLength(interp, item->valuesObj, &length);
	while (length < tv->tree.nColumns) {
	    Tcl_Obj *empty = Tcl_NewStringObj("",0);
	    Tcl_ListObjAppendElement(interp, item->valuesObj, empty);
	    ++length;
	}

	/* Set value:
	 */
	Tcl_ListObjReplace(interp,item->valuesObj,columnNumber,1,1,objv+4);
	TtkRedisplayWidget(&tv->core);
	return TCL_OK;
    }
}

/*------------------------------------------------------------------------
 * +++ Widget commands -- tree modification.
 */

/* + $tv insert $parent $index ?-id id? ?-option value ...?
 * 	Insert a new item.
 */
static int TreeviewInsertCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *parent, *sibling, *newItem;
    Tcl_HashEntry *entryPtr;
    int isNew;

    if (objc < 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "parent index ?-id id? -options...");
	return TCL_ERROR;
    }

    /* Get parent node:
     */
    if ((parent = FindItem(interp, tv, objv[2])) == NULL) {
	return TCL_ERROR;
    }

    /* Locate previous sibling based on $index:
     */
    if (!strcmp(Tcl_GetString(objv[3]), "end")) {
	sibling = EndPosition(tv, parent);
    } else {
	int index;
	if (Tcl_GetIntFromObj(interp, objv[3], &index) != TCL_OK)
	    return TCL_ERROR;
	sibling = InsertPosition(parent, index);
    }

    /* Get node name:
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
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "ITEM_EXISTS", NULL);
	    return TCL_ERROR;
	}
	objc -= 2; objv += 2;
    } else {
	char idbuf[16];
	do {
	    ++tv->tree.serial;
	    snprintf(idbuf, sizeof(idbuf), "I%03X", tv->tree.serial);
	    entryPtr = Tcl_CreateHashEntry(&tv->tree.items, idbuf, &isNew);
	} while (!isNew);
    }

    /* Create and configure new item:
     */
    newItem = NewItem();
    Tk_InitOptions(
	interp, newItem, tv->tree.itemOptionTable, tv->core.tkwin);
    newItem->tagset = Ttk_GetTagSetFromObj(NULL, tv->tree.tagTable, NULL);
    if (ConfigureItem(interp, tv, newItem, objc, objv) != TCL_OK) {
    	Tcl_DeleteHashEntry(entryPtr);
	FreeItem(newItem);
	return TCL_ERROR;
    }

    /* Store in hash table, link into tree:
     */
    Tcl_SetHashValue(entryPtr, newItem);
    newItem->entryPtr = entryPtr;
    InsertItem(parent, sibling, newItem);
    tv->tree.rowPosNeedsUpdate = 1;
    TtkRedisplayWidget(&tv->core);

    Tcl_SetObjResult(interp, ItemID(tv, newItem));
    return TCL_OK;
}

/* + $tv detach $items --
 * 	Unlink each item in $items from the tree.
 */
static int TreeviewDetachCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem **items;
    Tcl_Size i;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "item");
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
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "ROOT", NULL);
	    ckfree(items);
	    return TCL_ERROR;
	}
    }

    for (i = 0; items[i]; ++i) {
	DetachItem(items[i]);
    }

    tv->tree.rowPosNeedsUpdate = 1;
    TtkRedisplayWidget(&tv->core);
    ckfree(items);
    return TCL_OK;
}

/* Is an item detached? The root is never detached. */
static int IsDetached(Treeview *tv, TreeItem *item)
{
	return item->next == NULL && item->prev == NULL &&
			item->parent == NULL && item != tv->tree.root;
}

/* + $tv detached ?$item? --
 * 	List detached items (in arbitrary order) or query the detached state of
 * 	$item.
 */
static int TreeviewDetachedCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item;

    if (objc == 2) {
	/* List detached items */
	Tcl_HashSearch search;
	Tcl_HashEntry *entryPtr = Tcl_FirstHashEntry(&tv->tree.items, &search);
	Tcl_Obj *objPtr = Tcl_NewObj();

	while (entryPtr != NULL) {
	    item = (TreeItem *)Tcl_GetHashValue(entryPtr);
	    entryPtr = Tcl_NextHashEntry(&search);
	    if (IsDetached(tv, item)) {
		Tcl_ListObjAppendElement(NULL, objPtr, ItemID(tv, item));
	    }
	}
	Tcl_SetObjResult(interp, objPtr);
	return TCL_OK;
    } else if (objc == 3) {
	/* Query; the root is never reported as detached */
	if (!(item = FindItem(interp, tv, objv[2]))) {
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(IsDetached(tv, item)));
	return TCL_OK;
    } else {
	Tcl_WrongNumArgs(interp, 2, objv, "?item?");
	return TCL_ERROR;
    }
}
/* + $tv delete $items --
 * 	Delete each item in $items.
 *
 * 	Do this in two passes:
 * 	First detach the item and all its descendants and remove them
 * 	from the hash table.  Free the items themselves in a second pass.
 *
 * 	It's done this way because an item may appear more than once
 *	in the list of items to delete (either directly or as a descendant
 *	of a previously deleted item.)
 */

static int TreeviewDeleteCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
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

    /* Sanity-check:
     */
    for (i = 0; items[i]; ++i) {
	if (items[i] == tv->tree.root) {
	    ckfree(items);
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"Cannot delete root item", -1));
	    Tcl_SetErrorCode(interp, "TTK", "TREE", "ROOT", NULL);
	    return TCL_ERROR;
	}
    }

    /* Remove items from hash table.
     */
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

    /* Free items:
     */
    while (delq) {
	TreeItem *next = delq->next;
	if (tv->tree.focus == delq)
	    tv->tree.focus = 0;
	if (tv->tree.endPtr == delq)
	    tv->tree.endPtr = 0;
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
 * 	Move $item to the specified $index in $parent's child list.
 */
static int TreeviewMoveCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item, *parent;
    TreeItem *sibling;

    if (objc != 5) {
	Tcl_WrongNumArgs(interp, 2, objv, "item parent index");
	return TCL_ERROR;
    }
    if ((item = FindItem(interp, tv, objv[2])) == 0
	    || (parent = FindItem(interp, tv, objv[3])) == 0) {
	return TCL_ERROR;
    }

    /* Locate previous sibling based on $index:
     */
    if (!strcmp(Tcl_GetString(objv[4]), "end")) {
	sibling = EndPosition(tv, parent);
    } else {
	TreeItem *p;
	int index;

	if (Tcl_GetIntFromObj(interp, objv[4], &index) != TCL_OK) {
	    return TCL_ERROR;
	}

	sibling = 0;
	for (p = parent->children; p != NULL && index > 0; p = p->next) {
	    if (p != item) {
		--index;
	    } /* else -- moving node forward, count index+1 nodes  */
	    sibling = p;
	}
    }

    /* Check ancestry:
     */
    if (!AncestryCheck(interp, tv, item, parent)) {
	return TCL_ERROR;
    }

    /* Moving an item after itself is a no-op:
     */
    if (item == sibling) {
	return TCL_OK;
    }

    /* Move item:
     */
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
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    return TtkScrollviewCommand(interp, objc, objv, tv->tree.xscrollHandle);
}

static int TreeviewYViewCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    return TtkScrollviewCommand(interp, objc, objv, tv->tree.yscrollHandle);
}

/* $tree see $item --
 * 	Ensure that $item is visible.
 */
static int TreeviewSeeCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    TreeItem *item, *parent;
    int scrollRow1, scrollRow2, visibleRows;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "item");
	return TCL_ERROR;
    }
    if (!(item = FindItem(interp, tv, objv[2]))) {
	return TCL_ERROR;
    }

    /* Make sure all ancestors are open:
     */
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

    /* Make sure item is visible:
     */
    if (item->rowPos < tv->tree.titleRows) {
	return TCL_OK;
    }
    visibleRows = tv->tree.treeArea.height / tv->tree.rowHeight
	    - tv->tree.titleRows;
    scrollRow1 = item->rowPos - tv->tree.titleRows;
    scrollRow2 = scrollRow1 + item->height - 1;
    if (scrollRow1 < tv->tree.yscroll.first || item->height > visibleRows) {
	TtkScrollTo(tv->tree.yscrollHandle, scrollRow1, 1);
    } else if (scrollRow2 >= tv->tree.yscroll.first + visibleRows) {
	scrollRow1 = 1 + scrollRow2 - visibleRows;
	TtkScrollTo(tv->tree.yscrollHandle, scrollRow1, 1);
    }

    return TCL_OK;
}

/*------------------------------------------------------------------------
 * +++ Widget commands -- interactive column resize
 */

/* + $tree drag $column $newX --
 * 	Set right edge of display column $column to x position $X
 */
static int TreeviewDragCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    int left = tv->tree.treeArea.x - tv->tree.xscroll.first;
    Tcl_Size i = FirstColumn(tv);
    TreeColumn *column;
    int newx;

    if (objc != 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "column xposition");
	return TCL_ERROR;
    }

    if ((column = FindColumn(interp, tv, objv[2])) == 0
	    || Tcl_GetIntFromObj(interp, objv[3], &newx) != TCL_OK) {
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
    Tcl_SetErrorCode(interp, "TTK", "TREE", "COLUMN_INVISIBLE", NULL);
    return TCL_ERROR;
}

static int TreeviewDropCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
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
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;

    if (objc == 2) {
	if (tv->tree.focus) {
	    Tcl_SetObjResult(interp, ItemID(tv, tv->tree.focus));
	}
	return TCL_OK;
    } else if (objc == 3) {
	TreeItem *newFocus = FindItem(interp, tv, objv[2]);
	if (!newFocus)
	    return TCL_ERROR;
	tv->tree.focus = newFocus;
	TtkRedisplayWidget(&tv->core);
	return TCL_OK;
    } else {
	Tcl_WrongNumArgs(interp, 2, objv, "?newFocus?");
	return TCL_ERROR;
    }
}

/* + $tree selection ?add|remove|set|toggle $items?
 */
static int TreeviewSelectionCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    enum {
	SELECTION_SET, SELECTION_ADD, SELECTION_REMOVE, SELECTION_TOGGLE
    };
    static const char *const selopStrings[] = {
	"set", "add", "remove", "toggle", NULL
    };

    Treeview *tv = (Treeview *)recordPtr;
    int selop, i, selChange = 0;
    TreeItem *item, **items;

    if (objc == 2) {
	Tcl_Obj *result = Tcl_NewListObj(0,0);
	for (item = tv->tree.root->children; item; item = NextPreorder(item)) {
	    if (item->state & TTK_STATE_SELECTED)
		Tcl_ListObjAppendElement(NULL, result, ItemID(tv, item));
	}
	Tcl_SetObjResult(interp, result);
	return TCL_OK;
    }

    if (objc != 4) {
    	Tcl_WrongNumArgs(interp, 2, objv, "?add|remove|set|toggle items?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[2], selopStrings,
	    sizeof(char *), "selection operation", 0, &selop) != TCL_OK) {
	return TCL_ERROR;
    }

    items = GetItemListFromObj(interp, tv, objv[3]);
    if (!items) {
	return TCL_ERROR;
    }

    switch (selop)
    {
	case SELECTION_SET:
	    /* Clear */
	    for (item=tv->tree.root; item; item = NextPreorder(item)) {
		if (item->state & TTK_STATE_SELECTED) {
		    item->state &= ~TTK_STATE_SELECTED;
		    selChange = 1;
		}
	    }
	    for (i=0; items[i]; ++i) {
		items[i]->state |= TTK_STATE_SELECTED;
		selChange = 1;
	    }
	    break;
	case SELECTION_ADD:
	    for (i=0; items[i]; ++i) {
		if (!(items[i]->state & TTK_STATE_SELECTED)) {
		    items[i]->state |= TTK_STATE_SELECTED;
		    selChange = 1;
		}
	    }
	    break;
	case SELECTION_REMOVE:
	    for (i=0; items[i]; ++i) {
		if (items[i]->state & TTK_STATE_SELECTED) {
		    items[i]->state &= ~TTK_STATE_SELECTED;
		    selChange = 1;
		}
	    }
	    break;
	case SELECTION_TOGGLE:
	    for (i=0; items[i]; ++i) {
		items[i]->state ^= TTK_STATE_SELECTED;
		selChange = 1;
	    }
	    break;
    }

    ckfree(items);
    if (selChange) {
	Tk_SendVirtualEvent(tv->core.tkwin, "TreeviewSelect", NULL);
    }
    TtkRedisplayWidget(&tv->core);

    return TCL_OK;
}

/* + SelObjChangeElement --
 * 	Change an element in a cell selection list.
 */
static int SelObjChangeElement(
    Treeview *tv, Tcl_Obj *listPtr, Tcl_Obj *elemPtr,
    int add,
    TCL_UNUSED(int) /*remove*/,
    int toggle)
{
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
    int add, int remove, int toggle)
{
    TreeCell cellFrom, cellTo;
    TreeItem *item;
    Tcl_Obj *columns, **elements;
    int colno, fromNo, toNo, anyChange = 0;
    Tcl_Size i, nElements;
    int set = !(add || remove || toggle);

    if (GetCellFromObj(interp, tv, fromCell, 1, &fromNo, &cellFrom)
	    != TCL_OK) {
	return TCL_ERROR;
    }
    if (GetCellFromObj(interp, tv, toCell, 1, &toNo, &cellTo)
	    != TCL_OK) {
	return TCL_ERROR;
    }

    /* Correct order.
     */
    if (fromNo > toNo) {
	colno = fromNo;
	fromNo = toNo;
	toNo = colno;
    }

    /* Make a list of columns in this rectangle.
     */
    columns = Tcl_NewListObj(0, 0);
    Tcl_IncrRefCount(columns);
    for (colno = fromNo; colno <= toNo; colno++) {
	Tcl_ListObjAppendElement(NULL, columns,
		tv->tree.displayColumns[colno]->idObj);
    }

    /* Set is the only operation that affects items outside its rectangle.
     * Start with clearing out.
     */
    if (set) {
	anyChange = CellSelectionClear(tv);
    }

    /* Correct order.
     */
    if (tv->tree.rowPosNeedsUpdate) {
	UpdatePositionTree(tv);
    }
    if (cellFrom.item->itemPos > cellTo.item->itemPos) {
	item = cellFrom.item;
	cellFrom.item = cellTo.item;
	cellTo.item = item;
    }

    /* Go through all items in this rectangle.
     */
    for (item = cellFrom.item; item; item = NextPreorder(item)) {
	if (item->selObj != NULL) {
	    item->selObj = unshareObj(item->selObj);

	    Tcl_ListObjGetElements(NULL, columns, &nElements, &elements);
	    for (i = 0; i < nElements; ++i) {
		anyChange |= SelObjChangeElement(tv, item->selObj, elements[i],
			add, remove, toggle);
	    }
	} else {
	    /* Set, add and toggle do the same thing when empty before.
	     */
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
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    enum {
	SELECTION_SET, SELECTION_ADD, SELECTION_REMOVE, SELECTION_TOGGLE
    };
    static const char *const selopStrings[] = {
	"set", "add", "remove", "toggle", NULL
    };

    Treeview *tv = (Treeview *)recordPtr;
    int selop, anyChange = 0;
    Tcl_Size i, nCells;
    TreeCell *cells;
    TreeItem *item;

    if (objc == 2) {
	Tcl_Obj *result = Tcl_NewListObj(0,0);
	for (item = tv->tree.root->children; item; item = NextPreorder(item)) {
	    if (item->selObj != NULL) {
		Tcl_Size n, elemc;
		Tcl_Obj **elemv;

		Tcl_ListObjGetElements(interp, item->selObj, &n, &elemv);
		elemc = n;
		for (i = 0; i < elemc; ++i) {
		    Tcl_Obj *elem[2];
		    elem[0] = ItemID(tv, item);
		    elem[1] = elemv[i];
		    Tcl_ListObjAppendElement(NULL, result,
			    Tcl_NewListObj(2, elem));
		}
	    }
	}
	Tcl_SetObjResult(interp, result);
	return TCL_OK;
    }

    if (objc < 4 || objc > 5) {
    	Tcl_WrongNumArgs(interp, 2, objv, "?add|remove|set|toggle arg...?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[2], selopStrings,
	    sizeof(char *), "cellselection operation", 0, &selop) != TCL_OK) {
	return TCL_ERROR;
    }

    if (objc == 5) {
	switch (selop)
	{
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

    cells = GetCellListFromObj(interp, tv, objv[3], &nCells);
    if (cells == NULL) {
	return TCL_ERROR;
    }

    switch (selop)
    {
	case SELECTION_SET:
	    anyChange = CellSelectionClear(tv);
	    /*FALLTHRU*/
	case SELECTION_ADD:
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
	case SELECTION_REMOVE:
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
	case SELECTION_TOGGLE:
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

    ckfree(cells);
    if (anyChange) {
	Tk_SendVirtualEvent(tv->core.tkwin, "TreeviewSelect", NULL);
    }
    TtkRedisplayWidget(&tv->core);

    return TCL_OK;
}

/*------------------------------------------------------------------------
 * +++ Widget commands -- tags and bindings.
 */

/* + $tv tag bind $tag ?$sequence ?$script??
 */
static int TreeviewTagBindCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
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
    } else if (objc == 5) { 	/* $tv tag bind $tag $sequence */
	/* TODO: distinguish "no such binding" (OK) from "bad pattern" (ERROR)
	 */
	const char *script = Tk_GetBinding(interp,
		bindingTable, tag, Tcl_GetString(objv[4]));
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

	    /* Test mask to make sure event is supported:
	     */
	    if (mask & (~TreeviewBindEventMask)) {
		Tk_DeleteBinding(interp, bindingTable, tag, sequence);
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "unsupported event %s\nonly key, button, motion, and"
		    " virtual events supported", sequence));
		Tcl_SetErrorCode(interp, "TTK", "TREE", "BIND_EVENTS", NULL);
		return TCL_ERROR;
	    }
	}
    }
    return TCL_OK;
}

/* + $tv tag configure $tag ?-option ?value -option value...??
 */
static int TreeviewTagConfigureCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
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
	Tcl_Obj *result = Ttk_TagOptionValue(interp, tagTable, tag, objv[4]);
	if (result) {
	    Tcl_SetObjResult(interp, result);
	    return TCL_OK;
	} /* else */
	return TCL_ERROR;
    }
    /* else */
    TtkRedisplayWidget(&tv->core);
    return Ttk_ConfigureTag(interp, tagTable, tag, objc - 4, objv + 4);
}

/* + $tv tag delete $tag
 */
static int TreeviewTagDeleteCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
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
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;

    if (objc == 4) {	/* Return list of all items with tag */
	Ttk_Tag tag = Ttk_GetTagFromObj(tv->tree.tagTable, objv[3]);
	TreeItem *item = tv->tree.root;
	Tcl_Obj *result = Tcl_NewListObj(0,0);

	while (item) {
	    if (Ttk_TagSetContains(item->tagset, tag)) {
		Tcl_ListObjAppendElement(NULL, result, ItemID(tv, item));
	    }
	    item = NextPreorder(item);
	}

	Tcl_SetObjResult(interp, result);
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
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;
    TreeCell cell;
    Tcl_Size i, columnNumber;

    if (objc == 5) {	/* Return list of all cells with tag */
	Ttk_Tag tag = Ttk_GetTagFromObj(tv->tree.tagTable, objv[4]);
	TreeItem *item = tv->tree.root;
	Tcl_Obj *result = Tcl_NewListObj(0,0);

	while (item) {
	    for (i = 0; i < item->nTagSets && i <= tv->tree.nColumns; ++i) {
		if (item->cellTagSets[i] != NULL) {
		    if (Ttk_TagSetContains(item->cellTagSets[i], tag)) {
			Tcl_Obj *elem[2];
			elem[0] = ItemID(tv, item);
			if (i == 0) {
			    elem[1] = tv->tree.column0.idObj;
			} else {
			    elem[1] = tv->tree.columns[i-1].idObj;
			}
			Tcl_ListObjAppendElement(NULL, result,
				Tcl_NewListObj(2, elem));
		    }
		}
	    }
	    item = NextPreorder(item);
	}

	Tcl_SetObjResult(interp, result);
	return TCL_OK;
    } else if (objc == 6) {	/* Test if cell has specified tag */
	Ttk_Tag tag = Ttk_GetTagFromObj(tv->tree.tagTable, objv[4]);
	int result = 0;
	if (GetCellFromObj(interp, tv, objv[5], 0, NULL, &cell) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (cell.column == &tv->tree.column0) {
	    columnNumber = 0;
	} else {
	    columnNumber = cell.column - tv->tree.columns + 1;
	}
	if (columnNumber < cell.item->nTagSets) {
	    if (cell.item->cellTagSets[columnNumber] != NULL) {
		result = Ttk_TagSetContains(
			cell.item->cellTagSets[columnNumber],
			tag);
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
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Treeview *tv = (Treeview *)recordPtr;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 3, objv, "");
	return TCL_ERROR;
    }

    return Ttk_EnumerateTags(interp, tv->tree.tagTable);
}

/* + $tv tag add $tag $items
 */
static void AddTag(TreeItem *item, Ttk_Tag tag)
{
    if (Ttk_TagSetAdd(item->tagset, tag)) {
	if (item->tagsObj) Tcl_DecrRefCount(item->tagsObj);
	item->tagsObj = Ttk_NewTagSetObj(item->tagset);
	Tcl_IncrRefCount(item->tagsObj);
    }
}

static int TreeviewTagAddCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
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
	AddTag(items[i], tag);
    }
    ckfree(items);

    TtkRedisplayWidget(&tv->core);

    return TCL_OK;
}

/* Make sure tagset at column is allocated and initialised */
static void AllocCellTagSets(Treeview *tv, TreeItem *item, Tcl_Size columnNumber)
{
    Tcl_Size i, newSize = MAX(columnNumber + 1, tv->tree.nColumns + 1);
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
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
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
	if (cells[i].column == &tv->tree.column0) {
	    columnNumber = 0;
	} else {
	    columnNumber = cells[i].column - tv->tree.columns  + 1;
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
static void RemoveTag(TreeItem *item, Ttk_Tag tag)
{
    if (Ttk_TagSetRemove(item->tagset, tag)) {
	if (item->tagsObj) Tcl_DecrRefCount(item->tagsObj);
	item->tagsObj = Ttk_NewTagSetObj(item->tagset);
	Tcl_IncrRefCount(item->tagsObj);
    }
}

/* Remove tag from all cells at row 'item'
 */
static void RemoveTagFromCellsAtItem(TreeItem *item, Ttk_Tag tag)
{
    Tcl_Size i;

    for (i = 0; i < item->nTagSets; i++) {
        if (item->cellTagSets[i] != NULL) {
            Ttk_TagSetRemove(item->cellTagSets[i], tag);
        }
    }
}

static int TreeviewTagRemoveCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
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
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
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
	    if (cells[i].column == &tv->tree.column0) {
		columnNumber = 0;
	    } else {
		columnNumber = cells[i].column - tv->tree.columns  + 1;
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
    { "cell",    	0,TreeviewCtagCommands },
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
    { "bbox",  		TreeviewBBoxCommand,0 },
    { "cellselection" ,	TreeviewCellSelectionCommand,0 },
    { "children",	TreeviewChildrenCommand,0 },
    { "cget",		TtkWidgetCgetCommand,0 },
    { "column", 	TreeviewColumnCommand,0 },
    { "configure",	TtkWidgetConfigureCommand,0 },
    { "delete", 	TreeviewDeleteCommand,0 },
    { "detach", 	TreeviewDetachCommand,0 },
    { "detached", 	TreeviewDetachedCommand,0 },
    { "drag",   	TreeviewDragCommand,0 },
    { "drop",   	TreeviewDropCommand,0 },
    { "exists", 	TreeviewExistsCommand,0 },
    { "focus", 		TreeviewFocusCommand,0 },
    { "heading", 	TreeviewHeadingCommand,0 },
    { "identify",  	TreeviewIdentifyCommand,0 },
    { "index",  	TreeviewIndexCommand,0 },
    { "insert", 	TreeviewInsertCommand,0 },
    { "instate",	TtkWidgetInstateCommand,0 },
    { "item", 		TreeviewItemCommand,0 },
    { "move", 		TreeviewMoveCommand,0 },
    { "next", 		TreeviewNextCommand,0 },
    { "parent", 	TreeviewParentCommand,0 },
    { "prev", 		TreeviewPrevCommand,0 },
    { "see", 		TreeviewSeeCommand,0 },
    { "selection" ,	TreeviewSelectionCommand,0 },
    { "set",  		TreeviewSetCommand,0 },
    { "state",  	TtkWidgetStateCommand,0 },
    { "style",		TtkWidgetStyleCommand,0 },
    { "tag",    	0,TreeviewTagCommands },
    { "xview",  	TreeviewXViewCommand,0 },
    { "yview",  	TreeviewYViewCommand,0 },
    { 0,0,0 }
};

/*------------------------------------------------------------------------
 * +++ Widget definition.
 */

static const WidgetSpec TreeviewWidgetSpec = {
    "Treeview",			/* className */
    sizeof(Treeview),   	/* recordSize */
    TreeviewOptionSpecs,	/* optionSpecs */
    TreeviewCommands,   	/* subcommands */
    TreeviewInitialize,   	/* initializeProc */
    TreeviewCleanup,		/* cleanupProc */
    TreeviewConfigure,    	/* configureProc */
    TtkNullPostConfigure,  	/* postConfigureProc */
    TreeviewGetLayout, 		/* getLayoutProc */
    TreeviewSize, 		/* sizeProc */
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
    TCL_UNUSED(Ttk_Padding *))
{
    TreeitemIndicator *indicator = (TreeitemIndicator *)elementRecord;
    Ttk_Padding margins;
    int size = 0;

    Ttk_GetPaddingFromObj(NULL, tkwin, indicator->marginsObj, &margins);
    Tk_GetPixelsFromObj(NULL, tkwin, indicator->sizeObj, &size);

    *widthPtr = size + Ttk_PaddingWidth(margins);
    *heightPtr = size + Ttk_PaddingHeight(margins);
}

static void TreeitemIndicatorDraw(
    TCL_UNUSED(void *), /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    TreeitemIndicator *indicator = (TreeitemIndicator *)elementRecord;
    ArrowDirection direction =
	(state & TTK_STATE_OPEN) ? ARROW_DOWN : ARROW_RIGHT;
    Ttk_Padding margins;
    XColor *borderColor = Tk_GetColorFromObj(tkwin, indicator->colorObj);
    XGCValues gcvalues; GC gc; unsigned mask;

    if (state & TTK_STATE_LEAF) /* don't draw anything */
	return;

    Ttk_GetPaddingFromObj(NULL,tkwin,indicator->marginsObj,&margins);
    b = Ttk_PadBox(b, margins);

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
    TCL_UNUSED(Ttk_State))
{
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
 * +++ Initialisation.
 */

MODULE_SCOPE
void TtkTreeview_Init(Tcl_Interp *interp);

MODULE_SCOPE
void TtkTreeview_Init(Tcl_Interp *interp)
{
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
