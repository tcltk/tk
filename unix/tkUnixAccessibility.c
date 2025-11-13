/*
 * tkUnixAccessibility.c --
 *
 * This file implements accessibility/screen-reader support
 * on Unix-like systems based on the Gnome Accessibility Toolkit,
 * the standard accessibility library for X11 systems.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 2006, Marcus von Appen
 * Copyright (c) 2019-2025 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <tcl.h>
#include <tk.h>
#include "tkInt.h"

#ifdef USE_ATK
#include <atk/atk.h>
#include <atk/atktext.h>
#include <atk/atkvalue.h>
#include <atk-bridge.h>
#include <dbus/dbus.h>
#include <glib.h>



/* Structs for custom ATK objects bound to Tk. */
typedef struct _TkAtkAccessible {
    AtkObject parent;
    Tk_Window tkwin;
    Tcl_Interp *interp;
    gint x, y, width, height;
    char *path;
    int is_focused;
    int virtual_count;
} TkAtkAccessible;

typedef struct _TkAtkAccessibleClass {
    AtkObjectClass parent_class;
} TkAtkAccessibleClass;

/* Structs to map Tk roles into ATK roles. */

typedef struct AtkRoleMap {
    const char *tkrole;
    AtkRole atkrole;
} AtkRoleMap;

struct AtkRoleMap roleMap[] = {
    {"Button", ATK_ROLE_PUSH_BUTTON},
    {"Checkbox", ATK_ROLE_CHECK_BOX},
    {"Menuitem", ATK_ROLE_CHECK_MENU_ITEM},
    {"Combobox", ATK_ROLE_COMBO_BOX},
    {"Entry", ATK_ROLE_ENTRY},
    {"Label", ATK_ROLE_LABEL},
    {"Listbox", ATK_ROLE_LIST_BOX},
    {"Menu", ATK_ROLE_MENU},
    {"Menubar", ATK_ROLE_MENU_BAR},
    {"Tree", ATK_ROLE_TREE},
    {"Notebook", ATK_ROLE_PAGE_TAB},
    {"Progressbar", ATK_ROLE_PROGRESS_BAR},
    {"Radiobutton",ATK_ROLE_RADIO_BUTTON},
    {"Scale", ATK_ROLE_SLIDER},
    {"Spinbox", ATK_ROLE_SPIN_BUTTON},
    {"Table", ATK_ROLE_TREE_TABLE},
    {"Text", ATK_ROLE_TEXT},
    {"Toplevel", ATK_ROLE_WINDOW},
    {"Frame", ATK_ROLE_PANEL},
    {"Canvas", ATK_ROLE_CANVAS},
    {"Scrollbar", ATK_ROLE_SCROLL_BAR},
    {"Menubar", ATK_ROLE_MENU_BAR},
    {"Toggleswitch", ATK_ROLE_TOGGLE_BUTTON},
    {NULL, 0}
};


#define ATK_CONTEXT g_main_context_default()

/* Variables for managing ATK objects. */
static AtkObject *tk_root_accessible = NULL;
static GList *toplevel_accessible_objects = NULL;
static GHashTable *tk_to_atk_map = NULL;
extern Tcl_HashTable *TkAccessibilityObject;
static GMainContext *acc_context = NULL;
static GHashTable *virtual_child_cache = NULL;

/* GLib-Tcl event loop integration. */
static void Atk_Event_Setup (void *clientData, int flags);
static void Atk_Event_Check(void *clientData, int flags);
static int Atk_Event_Run(Tcl_Event *event, int flags);
static void ignore_atk_critical(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data);

/* ATK component interface. */
static void tk_get_extents(AtkComponent *component, gint *x, gint *y, gint *width, gint *height, AtkCoordType coord_type);
static gboolean tk_contains(AtkComponent *component, gint x, gint y, AtkCoordType coord_type);
static void tk_atk_component_interface_init(AtkComponentIface *iface);

/* ATK child, attribute and state management. */
static gint tk_get_n_children(AtkObject *obj);
static AtkObject *tk_ref_child(AtkObject *obj, gint i);
static AtkObject *TkCreateVirtualChild(Tcl_Interp *interp, Tk_Window parent, int index, AtkRole role);
void InvalidateVirtualChildren(Tk_Window parent);
static char *make_virtual_child_key(const char *parent_path, int index);
static void cleanup_virtual_child_cache(void);
static AtkRole GetAtkRoleForWidget(Tk_Window win);
static AtkRole tk_get_role(AtkObject *obj);
static gchar *GetAtkNameForWidget(Tk_Window win);
static const gchar *tk_get_name(AtkObject *obj);
static void tk_set_name(AtkObject *obj, const gchar *name);
static gchar *GetAtkDescriptionForWidget(Tk_Window win);
static const gchar *tk_get_description(AtkObject *obj);
static AtkStateSet *tk_ref_state_set(AtkObject *obj);

/* ATK value interface. */
static gchar *GetAtkValueForWidget(Tk_Window win);
static void tk_get_value_and_text(AtkValue *obj, gdouble *value, gchar **text);
static AtkRange *tk_get_range(AtkValue *obj);
static void tk_get_current_value(AtkValue *obj, GValue *value);
static void tk_get_minimum_value(AtkValue *obj, GValue *value);
static void tk_get_maximum_value(AtkValue *obj, GValue *value);
static void tk_atk_value_interface_init(AtkValueIface *iface);

/* ATK action interface. */
static gboolean tk_action_do_action(AtkAction *action, gint i);
static gint tk_action_get_n_actions(AtkAction *action);
static const gchar *tk_action_get_name(AtkAction *action, gint i);
static void tk_atk_action_interface_init(AtkActionIface *iface);

/* ATK text interface. */
static gchar *tk_text_get_text(AtkText *text, gint start_offset, gint end_offset);
static gint tk_text_get_caret_offset(AtkText *text);
static gchar *tk_text_get_selection(AtkText *text,gint selection_num, gint *start_offset, gint *end_offset);
static inline gchar *tk_acc_value_dup(Tk_Window win);
static gint tk_text_get_character_count(AtkText *text);
static gchar *tk_text_get_text_at_offset(AtkText *text, gint offset, AtkTextBoundary boundary_type, gint *start_offset, gint *end_offset);
static gchar *tk_text_get_text_after_offset(AtkText *text, gint offset, AtkTextBoundary boundary_type, gint *start_offset, gint *end_offset);
static gchar *tk_text_get_text_before_offset(AtkText *text, gint offset, AtkTextBoundary boundary_type, gint *start_offset, gint *end_offset);
static AtkAttributeSet *tk_text_get_run_attributes(AtkText *text, gint offset, gint *start_offset, gint *end_offset);
static AtkAttributeSet *tk_text_get_default_attributes(AtkText *text);
static void tk_text_get_character_extents(AtkText *text, gint offset, gint *x, gint *y, gint *width, gint *height, AtkCoordType coords);
static void tk_text_get_range_extents(AtkText *text,gint start_offset, gint end_offset, AtkCoordType    coords, AtkTextRectangle *rect);
static gint tk_text_get_offset_at_point(AtkText *text, gint x, gint y, AtkCoordType coords);
static gboolean tk_text_set_caret_offset(AtkText *text, gint offset);
static gboolean tk_text_set_selection(AtkText *text, gint selection_num, gint start_offset, gint end_offset);
static gint tk_text_get_n_selections(AtkText *text);
static void tk_atk_text_interface_init(AtkTextIface *iface);

/* ATK selection interface. */
static gboolean tk_selection_add_selection(AtkSelection *selection, gint i);
static gboolean tk_selection_remove_selection(AtkSelection *selection, gint i);
static gboolean tk_selection_clear_selection(AtkSelection *selection);
static gint tk_selection_get_selection_count(AtkSelection *selection);
static gboolean tk_selection_is_child_selected(AtkSelection *selection, gint i);
static AtkObject *tk_selection_ref_selection(AtkSelection *selection, gint i);
static gboolean tk_selection_select_all_selection(AtkSelection *selection);
static void tk_atk_selection_interface_init(AtkSelectionIface *iface);
static void TkAtkNotifySelectionChanged(Tk_Window tkwin);

/* Object lifecycle functions. */
static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass);
static void tk_atk_accessible_init(TkAtkAccessible *accessible);
static void tk_atk_accessible_finalize(GObject *gobject);

/* Registration and mapping functions. */
static void RegisterToplevelWindow(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *accessible);
static void UnregisterToplevelWindow(AtkObject *accessible);
Tk_Window GetToplevelOfWidget(Tk_Window tkwin);
AtkObject *TkCreateAccessibleAtkObject(Tcl_Interp *interp, Tk_Window tkwin, const char *path);
void InitAtkTkMapping(void);
void RegisterAtkObjectForTkWindow(Tk_Window tkwin, AtkObject *atkobj);
AtkObject *GetAtkObjectForTkWindow(Tk_Window tkwin);
void UnregisterAtkObjectForTkWindow(Tk_Window tkwin);
static AtkObject *tk_util_get_root(void);
AtkObject *atk_get_root(void);

/* Event handlers. */
void TkAtkAccessible_RegisterEventHandlers(Tk_Window tkwin, void *tkAccessible);
static void TkAtkAccessible_DestroyHandler(void *clientData, XEvent *eventPtr);
static void TkAtkAccessible_FocusHandler(void *clientData, XEvent *eventPtr);
static void TkAtkAccessible_CreateHandler(void *clientData, XEvent *eventPtr);
static void TkAtkAccessible_ConfigureHandler(void *clientData, XEvent *eventPtr);

/* Tcl command implementations. */
static int EmitSelectionChanged(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int EmitFocusChanged(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int IsScreenReaderRunning(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int IsScreenReaderActive(void);
int TkAtkAccessibleObjCmd(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int TkAtkAccessibility_Init(Tcl_Interp *interp);

/* Signal IDs for custom AT-SPI signals. */
static guint window_create_signal_id;
static guint window_activate_signal_id;
static guint window_deactivate_signal_id;

/* Define custom ATK object bridged to Tcl/Tk. */
#define TK_ATK_TYPE_ACCESSIBLE (tk_atk_accessible_get_type())
#define TK_ATK_IS_ACCESSIBLE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), TK_ATK_TYPE_ACCESSIBLE))
G_DEFINE_TYPE_WITH_CODE(TkAtkAccessible, tk_atk_accessible, ATK_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(ATK_TYPE_COMPONENT, tk_atk_component_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_ACTION, tk_atk_action_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_VALUE, tk_atk_value_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_TEXT, tk_atk_text_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_SELECTION, tk_atk_selection_interface_init)
			)
/*
 *----------------------------------------------------------------------
 *
 * GLib integration functions. These create a Tcl event source so that
 * the GLib event loop can be smoothly integrated with Tcl/Tk.
 *
 *----------------------------------------------------------------------
 */

/* Configure event loop. */
static void Atk_Event_Setup(
    TCL_UNUSED(void *), /* clientData */
    int flags)
{
    static Tcl_Time block_time = {0, 10000};

    if (!(flags & TCL_WINDOW_EVENTS)) {
	return;
    }

    if (g_main_context_pending(acc_context)) {
	block_time.usec = 0;
    }
    Tcl_SetMaxBlockTime(&block_time);
}

/* Check event queue. */
static void Atk_Event_Check(
    TCL_UNUSED(void *), /* clientData */
    int flags)
{
    if (!(flags & TCL_WINDOW_EVENTS)) {
	return;
    }

    if (g_main_context_pending(acc_context)) {
	Tcl_Event *event = (Tcl_Event *)ckalloc(sizeof(Tcl_Event));
	event->proc = Atk_Event_Run;
	Tcl_QueueEvent(event, TCL_QUEUE_TAIL);
    }
}

/* Run the event. */
static int Atk_Event_Run(
    TCL_UNUSED(Tcl_Event *), /* event */
    int flags)
{
    if (!(flags & TCL_WINDOW_EVENTS)) {
	return 0;
    }

    while (g_main_context_pending(acc_context)) {
	g_main_context_iteration(acc_context, FALSE);
    }

    return 1;
}

/* Disable GLib warnings that can pollute the console - dummy function.  */
static void ignore_atk_critical(
    TCL_UNUSED(const gchar *), /* log_domain */
    TCL_UNUSED(GLogLevelFlags), /* log_level */
    TCL_UNUSED(const gchar *), /* message */
    TCL_UNUSED(gpointer)) /* user_data */
{
}

/*
 *----------------------------------------------------------------------
 *
 * ATK interface functions. These do the heavy lifting of mapping Tk to ATK
 * functionality. ATK has a more rigid structure than NSAccessibility and
 * Microsoft Active Accessibility and requires a great deal more specific
 * implementation for accessibility to work properly.
 *
 *----------------------------------------------------------------------
 */

/*
 * ATK component interface. This tracks widget location/geometry.
 */

static void tk_get_extents(AtkComponent *component, gint *x, gint *y, gint *width, gint *height, AtkCoordType coord_type)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)component;

    if (!acc || !acc->tkwin) {
	*x = *y = *width = *height = 0;
	return;
    }

    int wx, wy;
    Tk_GetRootCoords(acc->tkwin, &wx, &wy);
    int w = Tk_Width(acc->tkwin);
    int h = Tk_Height(acc->tkwin);

    if (coord_type == ATK_XY_SCREEN) {
	/* Absolute screen coords. */
	*x = wx;
	*y = wy;
    } else {
	/* Relative to toplevel window. */
	Tk_Window top = GetToplevelOfWidget(acc->tkwin);
	int tx, ty;
	Tk_GetRootCoords(top, &tx, &ty);
	*x = wx - tx;
	*y = wy - ty;
    }

    *width = w;
    *height = h;
}

static gboolean tk_contains(AtkComponent *component, gint x, gint y, AtkCoordType coord_type)
{
    gint comp_x, comp_y, comp_width, comp_height;
    if (!component) return FALSE;
    tk_get_extents(component, &comp_x, &comp_y, &comp_width, &comp_height, coord_type);

    return (x >= comp_x && x < comp_x + comp_width &&
	    y >= comp_y && y < comp_y + comp_height);
}

static void tk_atk_component_interface_init(AtkComponentIface *iface)
{
    iface->get_extents = tk_get_extents;
    iface->contains    = tk_contains;
}

/*
 * Accessible children, attributes and state. Here we create accessible objects
 * from "virtual" children (listbox rows, tree rows/cells, menu items), cache
 * them, and assign them the appropriate ATK role for selection events and other
 * interactions. We also create accessible objects from native Tk widgets (buttons,
 * entries, etc.), map them them to the appropriate role, and track them.
 */

static char *make_virtual_child_key(const char *parent_path, int index)
{
    gchar *key = g_strdup_printf("%s#%d", parent_path ? parent_path : "<null>", index);
    return key;
}

static void cleanup_virtual_child_cache(void)
{
    if (virtual_child_cache) {
	g_hash_table_destroy(virtual_child_cache);
	virtual_child_cache = NULL;
    }
}

static AtkObject *TkCreateVirtualChild(Tcl_Interp *interp, Tk_Window parent, int index, AtkRole role)
{
    if (!interp || !parent) return NULL;

    const char *parent_path = Tk_PathName(parent);
    char cmd[512];
    const char *label = NULL;
    char *label_copy = NULL;

    /*
     * Retrieve label text depending on role. In other areas we
     * pull this data from the core TkAccessibleObject hash table
     * via GetAtkValueForWidget, because selection events update
     * the "value" field in that hash table with the string value
     * from the selection event. However, in virtual widget creation,
     * it makes sense to pull the data via direct inquiry of the
     * virtual widget so there is no conflict. The hash table will
     * automatically be updated by this selection event.
     */
    switch (role) {
    case ATK_ROLE_LIST_ITEM:
	/* Listbox: use get index. */
	snprintf(cmd, sizeof(cmd), "%s get %d", parent_path, index);
	if (Tcl_Eval(interp, cmd) == TCL_OK) {
	    label = Tcl_GetString(Tcl_GetObjResult(interp));
	}
	break;

    case ATK_ROLE_MENU_ITEM:
	/* Menu: use entrycget -label. */
	snprintf(cmd, sizeof(cmd), "%s entrycget %d -label", parent_path, index);
	if (Tcl_Eval(interp, cmd) == TCL_OK) {
	    label = Tcl_GetString(Tcl_GetObjResult(interp));
	}
	break;

    case ATK_ROLE_TREE_ITEM:
	{
	    /* Treeview: map index → item ID → cget -values. */
	    snprintf(cmd, sizeof(cmd), "%s children {}", parent_path);
	    if (Tcl_Eval(interp, cmd) == TCL_OK) {
		Tcl_Obj *list = Tcl_GetObjResult(interp);
		Tcl_Size count;
		Tcl_Obj **elems;
		if (Tcl_ListObjGetElements(interp, list, &count, &elems) == TCL_OK && index < count) {
		    const char *itemid = Tcl_GetString(elems[index]);
		    snprintf(cmd, sizeof(cmd), "%s item [lindex %s 0] -values", parent_path, itemid);
		    if (Tcl_Eval(interp, cmd) == TCL_OK) {
			label = Tcl_GetString(Tcl_GetObjResult(interp));
		    }
		}
	    }
	    break;
	}

    default:
	break;
    }

    if (!label || !*label) {
	/* Fallback to generic name. */
	char buf[64];
	snprintf(buf, sizeof(buf), "Item %d", index);
	label_copy = g_strdup(buf);
    } else {
	label_copy = g_strdup(label);
    }

    /* Create minimal virtual child. */
    TkAtkAccessible *child_acc = g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
    child_acc->interp = interp;
    child_acc->tkwin = NULL; /* Virtual child - real widgets have a Tk_Window associated. */
    child_acc->path = g_strdup_printf("%s#%d", parent_path, index);
    child_acc->virtual_count = index;

    AtkObject *child = ATK_OBJECT(child_acc);

    atk_object_initialize(child, NULL);
    atk_object_set_role(child, role);
    atk_object_set_name(child, label_copy);

    g_free(label_copy);

    /* Store index for selection tracking. */
    g_object_set_data(G_OBJECT(child), "tk-index", GINT_TO_POINTER(index));

    /* Set parent relationship. */
    AtkObject *accParent = GetAtkObjectForTkWindow(parent);
    if (!accParent) {
	Tk_Window toplevel = GetToplevelOfWidget(parent);
	if (toplevel) {
	    accParent = GetAtkObjectForTkWindow(toplevel);
	}
    }
    if (accParent) {
	atk_object_set_parent(child, accParent);
    }


    return child;
}

void InvalidateVirtualChildren(Tk_Window parent)
{
    if (!virtual_child_cache) return;

    const char *parent_path = Tk_PathName(parent);
    GList *to_remove = NULL;

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, virtual_child_cache);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
	const char *cache_key = (const char *)key;
	if (strstr(cache_key, parent_path) == cache_key) {
	    to_remove = g_list_prepend(to_remove, g_strdup(cache_key));
	}
    }

    for (GList *l = to_remove; l != NULL; l = l->next) {
	g_hash_table_remove(virtual_child_cache, l->data);
	g_free(l->data);
    }
    g_list_free(to_remove);
}

static gint tk_get_n_children(AtkObject *obj)
{
    if (obj == tk_root_accessible) {
	return g_list_length(toplevel_accessible_objects);
    }

    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    if (!acc || !acc->tkwin || !acc->interp) return 0;

    int virtual_count = 0;
    int native_count = 0;

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);

    /* Only query for virtual children if widget is mapped and ready. */
    if (Tk_IsMapped(acc->tkwin) &&
	(role == ATK_ROLE_LIST_BOX || role == ATK_ROLE_MENU || role == ATK_ROLE_MENU_BAR ||
	 role == ATK_ROLE_TREE || role == ATK_ROLE_TREE_TABLE)) {

	const char *count_cmd = NULL;

	switch (role) {
	case ATK_ROLE_LIST_BOX:
	    count_cmd = "size";
	    break;
	case ATK_ROLE_MENU:
	case ATK_ROLE_MENU_BAR:
	    count_cmd = "index end";
	    break;
	case ATK_ROLE_TREE:
	case ATK_ROLE_TREE_TABLE:
	    /* Tree: get the top-level children list and count its length. */
	    char tcmd[256];
	    snprintf(tcmd, sizeof(tcmd), "%s children {}", Tk_PathName(acc->tkwin));
	    Tcl_Obj *savedResult = Tcl_GetObjResult(acc->interp);
	    Tcl_IncrRefCount(savedResult);
	    if (Tcl_Eval(acc->interp, tcmd) == TCL_OK) {
		Tcl_Obj *list = Tcl_GetObjResult(acc->interp);
		Tcl_Size count;
		Tcl_Obj **elems;
		if (Tcl_ListObjGetElements(acc->interp, list, &count, &elems) == TCL_OK) {
		    virtual_count = (int)count;
		    if (virtual_count < 0) virtual_count = 0;
		}
	    }
	    Tcl_SetObjResult(acc->interp, savedResult);
	    Tcl_DecrRefCount(savedResult);
	    /* We handled the tree case here — don't use the generic integer-path below. */
	    break;

	default:
	    break;
	}

	if (count_cmd) {
	    char cmd[256];
	    snprintf(cmd, sizeof(cmd), "%s %s", Tk_PathName(acc->tkwin), count_cmd);

	    /* Try to prevent crashes. */
	    Tcl_Obj *savedResult = Tcl_GetObjResult(acc->interp);
	    Tcl_IncrRefCount(savedResult);

	    if (Tcl_Eval(acc->interp, cmd) == TCL_OK) {
		int count;
		if (Tcl_GetIntFromObj(acc->interp, Tcl_GetObjResult(acc->interp), &count) == TCL_OK) {
		    virtual_count = count;
		    /* Handle menu special case. */
		    if ((role == ATK_ROLE_MENU || role == ATK_ROLE_MENU_BAR) && count >= 0) {
			virtual_count = count + 1;
		    }
		    if (virtual_count < 0) virtual_count = 0;
		}
	    }

	    /* Restore interpreter state. */
	    Tcl_SetObjResult(acc->interp, savedResult);
	    Tcl_DecrRefCount(savedResult);
	}
    }

    /* Count native children. */
    for (TkWindow *childPtr = ((TkWindow*)acc->tkwin)->childList;
	 childPtr != NULL; childPtr = childPtr->nextPtr) {
	native_count++;
    }

    return virtual_count + native_count;
}

static AtkObject *tk_ref_child(AtkObject *obj, gint i)
{
    if (obj == tk_root_accessible) {
	if (i < 0 || i >= (gint)g_list_length(toplevel_accessible_objects)) return NULL;
	GList *child = g_list_nth(toplevel_accessible_objects, i);
	if (child) {
	    g_object_ref(child->data);
	    return ATK_OBJECT(child->data);
	}
	return NULL;
    }

    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    if (!acc || !acc->tkwin || !acc->interp || i < 0) return NULL;

    /* Only handle virtual children for mapped widgets to avoid startup issues. */
    if (!Tk_IsMapped(acc->tkwin)) {
	/* During startup, only handle native children. */
	TkWindow *childPtr;
	gint index = 0;
	for (childPtr = ((TkWindow*)acc->tkwin)->childList; childPtr != NULL; childPtr = childPtr->nextPtr, index++) {
	    if (index == i) {
		Tk_Window child_tkwin = (Tk_Window)childPtr;
		AtkObject *child_obj = GetAtkObjectForTkWindow(child_tkwin);
		if (!child_obj) {
		    child_obj = TkCreateAccessibleAtkObject(acc->interp, child_tkwin, Tk_PathName(child_tkwin));
		    if (child_obj) {
			atk_object_set_parent(child_obj, obj);
			/* Don't register event handlers during startup. */
		    }
		}
		if (child_obj) {
		    g_object_ref(child_obj);
		    return child_obj;
		}
		break;
	    }
	}
	return NULL;
    }

    AtkRole parentRole = GetAtkRoleForWidget(acc->tkwin);
    int virtual_count = 0;

    /* Handle virtual children. */
    if ((parentRole == ATK_ROLE_LIST_BOX) || (parentRole == ATK_ROLE_MENU) ||
	(parentRole == ATK_ROLE_MENU_BAR) || (parentRole == ATK_ROLE_TREE) ||
	(parentRole == ATK_ROLE_TREE_TABLE)) {

	/* Get virtual count safely. */
	const char *count_cmd = NULL;
	AtkRole childRole = ATK_ROLE_UNKNOWN;

	switch (parentRole) {
	case ATK_ROLE_LIST_BOX:
	    childRole = ATK_ROLE_LIST_ITEM;
	    count_cmd = "size";
	    break;
	case ATK_ROLE_MENU:
	case ATK_ROLE_MENU_BAR:
	    childRole = ATK_ROLE_MENU_ITEM;
	    count_cmd = "index end";
	    break;
	case ATK_ROLE_TREE:
	case ATK_ROLE_TREE_TABLE:
	    childRole = ATK_ROLE_TREE_ITEM;
	    /* Tree: get the top-level children list and count its length. */
	    char tcmd[256];
	    snprintf(tcmd, sizeof(tcmd), "%s children {}", Tk_PathName(acc->tkwin));
	    Tcl_Obj *savedResult = Tcl_GetObjResult(acc->interp);
	    Tcl_IncrRefCount(savedResult);
	    if (Tcl_Eval(acc->interp, tcmd) == TCL_OK) {
		Tcl_Obj *list = Tcl_GetObjResult(acc->interp);
		Tcl_Size count;
		Tcl_Obj **elems;
		if (Tcl_ListObjGetElements(acc->interp, list, &count, &elems) == TCL_OK) {
		    virtual_count = (int)count;
		    if (virtual_count < 0) virtual_count = 0;
		}
	    }
	    Tcl_SetObjResult(acc->interp, savedResult);
	    Tcl_DecrRefCount(savedResult);
	    break;
	default:
	    break;
	}

	if (count_cmd) {
	    char cmd[256];
	    snprintf(cmd, sizeof(cmd), "%s %s", Tk_PathName(acc->tkwin), count_cmd);

	    Tcl_Obj *savedResult = Tcl_GetObjResult(acc->interp);
	    Tcl_IncrRefCount(savedResult);

	    if (Tcl_Eval(acc->interp, cmd) == TCL_OK) {
		int count;
		if (Tcl_GetIntFromObj(acc->interp, Tcl_GetObjResult(acc->interp), &count) == TCL_OK) {
		    virtual_count = count;
		    if ((parentRole == ATK_ROLE_MENU || parentRole == ATK_ROLE_MENU_BAR) && count >= 0) {
			virtual_count = count + 1;
		    }
		    if (virtual_count < 0) virtual_count = 0;
		}
	    }

	    Tcl_SetObjResult(acc->interp, savedResult);
	    Tcl_DecrRefCount(savedResult);
	}

	/* Check if requested index is in virtual range. */
	if (i < virtual_count && childRole != ATK_ROLE_UNKNOWN) {
	    /* Initialize cache if needed. */
	    if (!virtual_child_cache) {
		virtual_child_cache = g_hash_table_new_full(
							    g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_object_unref);
	    }

	    /* Check cache first. */
	    const char *parent_path = Tk_PathName(acc->tkwin);
	    char *key = make_virtual_child_key(parent_path, i);

	    AtkObject *child = g_hash_table_lookup(virtual_child_cache, key);
	    if (child) {
		g_object_ref(child);
		g_free(key);
		return child;
	    }

	    /* Create new virtual child. */
	    child = TkCreateVirtualChild(acc->interp, acc->tkwin, i, childRole);
	    if (child) {
		/* Ensure parent relationship points to this accessible
		 * object (obj).
		 */
		atk_object_set_parent(child, obj);


		if (acc->tkwin) {  /* Only register handlers for real windows. */
		    TkAtkAccessible_RegisterEventHandlers(acc->tkwin, acc);
		}
		/* Insert into cache: store a referenced child.
		 * Use the allocated key as the hash key.
		 * g_hash_table_insert takes ownership of key and
		 * value pointers.
		 */
		g_hash_table_insert(virtual_child_cache, key, g_object_ref(child)); /* Cache holds one ref. */

		/* Notify AT clients that a child was added at index i. */
		g_signal_emit_by_name(obj, "children-changed::add", i, child);

		/* Return a referenced child to the caller (callers expect a ref). */
		g_object_ref(child);
		return child;
	    }
	    /* If child creation failed, free the key we allocated. */
	    g_free(key);
	}
    }

    /* Handle native children - adjust index for virtual children. */
    gint native_index = i - virtual_count;
    if (native_index >= 0) {
	TkWindow *childPtr;
	gint index = 0;
	for (childPtr = ((TkWindow*)acc->tkwin)->childList; childPtr != NULL; childPtr = childPtr->nextPtr, index++) {
	    if (index == native_index) {
		Tk_Window child_tkwin = (Tk_Window)childPtr;
		AtkObject *child_obj = GetAtkObjectForTkWindow(child_tkwin);
		if (!child_obj) {
		    child_obj = TkCreateAccessibleAtkObject(acc->interp, child_tkwin, Tk_PathName(child_tkwin));
		    if (child_obj) {
			atk_object_set_parent(child_obj, obj);
			TkAtkAccessible_RegisterEventHandlers(child_tkwin, (TkAtkAccessible *)child_obj);
		    }
		}
		if (child_obj) {
		    g_object_ref(child_obj);
		    return child_obj;
		}
		break;
	    }
	}
    }

    return NULL;
}

static AtkRole GetAtkRoleForWidget(Tk_Window win)
{
    if (!win) return ATK_ROLE_UNKNOWN;

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)win);
    if (hPtr) {
	Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
	if (attrs) {
	    Tcl_HashEntry *roleEntry = Tcl_FindHashEntry(attrs, "role");
	    if (roleEntry) {
		const char *result = Tcl_GetString(Tcl_GetHashValue(roleEntry));
		if (result) {
		    for (int i = 0; roleMap[i].tkrole != NULL; i++) {
			if (strcmp(roleMap[i].tkrole, result) == 0) {
			    return roleMap[i].atkrole;
			}
		    }
		}
	    }
	}
    }

    /* Fallback to widget class. */
    const char *widgetClass = Tk_Class(win);
    if (widgetClass) {
	for (int i = 0; roleMap[i].tkrole != NULL; i++) {
	    if (strcasecmp(roleMap[i].tkrole, widgetClass) == 0) {
		return roleMap[i].atkrole;
	    }
	}
    }

    if (Tk_IsTopLevel(win)) {
	return ATK_ROLE_WINDOW;
    }

    return ATK_ROLE_UNKNOWN;
}

static AtkRole tk_get_role(AtkObject *obj)
{
    if (obj == tk_root_accessible) {
	return ATK_ROLE_APPLICATION;
    }

    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    if (!acc) return ATK_ROLE_UNKNOWN;

    if (!acc->tkwin) {
	/* Virtual child: return the role already stored in obj->role. */
	return obj->role;
    }

    return GetAtkRoleForWidget(acc->tkwin);
}

static gchar *GetAtkNameForWidget(Tk_Window win)
{
    if (!win) return NULL;

    AtkRole role = GetAtkRoleForWidget(win);
    /* If label, return the value instead of the name so Orca does not say "label" twice. */
    if (role == ATK_ROLE_LABEL) {
	return GetAtkValueForWidget(win);
    }

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)win);
    if (!hPtr) return NULL;

    Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
    if (!attrs) return NULL;

    Tcl_HashEntry *nameEntry = Tcl_FindHashEntry(attrs, "name");
    if (!nameEntry) return NULL;

    const char *name = Tcl_GetString(Tcl_GetHashValue(nameEntry));
    return name ? g_utf8_make_valid(name, -1) : NULL;
}

static const gchar *tk_get_name(AtkObject *obj)
{
    if (obj == tk_root_accessible) {
	return "Tk Application";
    }

    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    if (!acc) return NULL;

    if (!acc->tkwin) {
	/*
	 * Virtual child: Return the name set in GetAtkValueForWidget.
	 * This is written via selection events at the script levels.
	 * Callbacks into the Tcl interpreter to get the directly selected
	 * index in the Atk selection functions also retrieve this value.
	 * However, those calls are necessary for fine-grained index tracking in
	 * the accessible selection API. Here it's simpler to retrieve
	 * the string from the value field in the hash table.
	 */
	AtkObject *parent_obj = atk_object_get_parent(obj);
	TkAtkAccessible *parent = (TkAtkAccessible*)parent_obj;
	Tk_Window parentwin = parent->tkwin;
	gchar *name = GetAtkValueForWidget(parentwin);
	return name;
    }

    /* Real widget: Existing logic.*/
    return GetAtkNameForWidget(acc->tkwin);
}

static void tk_set_name(AtkObject *obj, const gchar *name)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;

    if (!acc) return;
    atk_object_set_name(obj, name);
}

static gchar *GetAtkDescriptionForWidget(Tk_Window win)
{
    if (!win) return NULL;

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)win);
    if (!hPtr) return NULL;

    Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
    if (!attrs) return NULL;

    Tcl_HashEntry *descriptionEntry = Tcl_FindHashEntry(attrs, "description");
    if (!descriptionEntry) return NULL;

    const char *description = Tcl_GetString(Tcl_GetHashValue(descriptionEntry));
    return description ? g_utf8_make_valid(description, -1) : NULL;
}

static const gchar *tk_get_description(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    return GetAtkDescriptionForWidget(acc->tkwin);
}

static AtkStateSet *tk_ref_state_set(AtkObject *obj)
{
    if (!obj) return atk_state_set_new();

    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    AtkStateSet *set = atk_state_set_new();

    /* Virtual child: tkwin == NULL. */
    if (!acc->tkwin) {
	atk_state_set_add_state(set, ATK_STATE_ENABLED);
	atk_state_set_add_state(set, ATK_STATE_SENSITIVE);
	atk_state_set_add_state(set, ATK_STATE_VISIBLE);
	atk_state_set_add_state(set, ATK_STATE_SHOWING);
	atk_state_set_add_state(set, ATK_STATE_FOCUSABLE);
	/* Check selection and focus states. */
	AtkObject *parent_obj = atk_object_get_parent(obj);
	if (parent_obj && ATK_IS_SELECTION(parent_obj)) {
	    gint index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(obj), "tk-index"));

	    /* Check if selected. */
	    if (tk_selection_is_child_selected(ATK_SELECTION(parent_obj), index)) {
		atk_state_set_add_state(set, ATK_STATE_SELECTED);
	    }

	    /* Check if focused (active item). */
	    if (tk_selection_is_child_selected(ATK_SELECTION(parent_obj), index)) {
		atk_state_set_add_state(set, ATK_STATE_FOCUSED);
	    }
	}
	return set;
    }
    /* Real widget. */
    atk_state_set_add_state(set, ATK_STATE_ENABLED);
    atk_state_set_add_state(set, ATK_STATE_SENSITIVE);

    if (Tk_IsMapped(acc->tkwin)) {
	atk_state_set_add_state(set, ATK_STATE_VISIBLE);
	atk_state_set_add_state(set, ATK_STATE_SHOWING);
	atk_state_set_add_state(set, ATK_STATE_FOCUSABLE);
    }

    if (acc->is_focused) {
	atk_state_set_add_state(set, ATK_STATE_FOCUSED);
    }

    return set;
}

/*
 * ATK value interface.
 */

static gchar *GetAtkValueForWidget(Tk_Window win)
{
    if (!win) return NULL;

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)win);
    if (!hPtr) return NULL;

    Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
    if (!attrs) return NULL;

    Tcl_HashEntry *valueEntry = Tcl_FindHashEntry(attrs, "value");
    if (!valueEntry) return NULL;

    const char *value = Tcl_GetString(Tcl_GetHashValue(valueEntry));
    return value ? g_utf8_make_valid(value, -1) : NULL;
}

/* Modern AtkValue methods (replace deprecated stubs). */
static void tk_get_value_and_text(AtkValue *obj, gdouble *value, gchar **text)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    if (!acc || !acc->tkwin || !acc->interp) {
	if (value) *value = 0.0;
	if (text) *text = g_strdup("0.0");
	return;
    }

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    if (role != ATK_ROLE_SPIN_BUTTON) {
	if (value) *value = 0.0;
	if (text) *text = g_strdup("0");
	return;
    }

    gchar *val = GetAtkValueForWidget(acc->tkwin);
    double cur_val = val ? atof(val) : 0.0;

    if (value) *value = cur_val;
    if (text) *text = g_strdup(val ? val : "0");
}


static AtkRange *tk_get_range(AtkValue *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    if (!acc || !acc->tkwin || !acc->interp) {
	return NULL;
    }

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    double min_val = 0.0, max_val = 0.0;
    char cmd[256];

    if (role == ATK_ROLE_SPIN_BUTTON || role == ATK_ROLE_SLIDER) {
	/* Spinbox/Scale: -from .. -to. */
	snprintf(cmd, sizeof(cmd), "%s cget -from", Tk_PathName(acc->tkwin));
	if (Tcl_Eval(acc->interp, cmd) == TCL_OK) {
	    Tcl_GetDoubleFromObj(acc->interp, Tcl_GetObjResult(acc->interp), &min_val);
	}

	snprintf(cmd, sizeof(cmd), "%s cget -to", Tk_PathName(acc->tkwin));
	if (Tcl_Eval(acc->interp, cmd) == TCL_OK) {
	    Tcl_GetDoubleFromObj(acc->interp, Tcl_GetObjResult(acc->interp), &max_val);
	}
    }
    else if (role == ATK_ROLE_PROGRESS_BAR || role == ATK_ROLE_SCROLL_BAR) {
	/* Progressbar/Scrollbar: 0 .. -maximum (default 100.) */
	min_val = 0.0;
	max_val = 100.0;

	snprintf(cmd, sizeof(cmd), "%s cget -maximum", Tk_PathName(acc->tkwin));
	if (Tcl_Eval(acc->interp, cmd) == TCL_OK) {
	    Tcl_GetDoubleFromObj(acc->interp, Tcl_GetObjResult(acc->interp), &max_val);
	}
    }
    else {
	return NULL; /* Not applicable. */
    }

    return atk_range_new(min_val, max_val, NULL);
}

/* Deprecated methods - updated to call modern ones (for compatibility). */
static void tk_get_current_value(AtkValue *obj, GValue *value)
{
    gdouble val = 0.0;
    gchar *text = NULL;
    tk_get_value_and_text(obj, &val, &text);
    g_value_init(value, G_TYPE_DOUBLE);
    g_value_set_double(value, val);
    g_free(text);
}

static void tk_get_minimum_value(AtkValue *obj, GValue *value)
{
    AtkRange *range = tk_get_range(obj);
    gdouble min_val = range ? atk_range_get_lower_limit(range) : 0.0;
    if (range) g_object_unref(range);
    g_value_init(value, G_TYPE_DOUBLE);
    g_value_set_double(value, min_val);
}

static void tk_get_maximum_value(AtkValue *obj, GValue *value)
{
    AtkRange *range = tk_get_range(obj);
    gdouble max_val = range ? atk_range_get_upper_limit(range) : 0.0;
    if (range) g_object_unref(range);
    g_value_init(value, G_TYPE_DOUBLE);
    g_value_set_double(value, max_val);
}

static void tk_atk_value_interface_init(AtkValueIface *iface)
{
    iface->get_value_and_text = tk_get_value_and_text;
    iface->get_range = tk_get_range;
    iface->get_current_value = tk_get_current_value;  /* Deprecated fallback. */
    iface->get_minimum_value = tk_get_minimum_value;  /* Deprecated fallback. */
    iface->get_maximum_value = tk_get_maximum_value;  /* Deprecated fallback. */
}

/*
 * ATK action interface.
 */

static gboolean tk_action_do_action(AtkAction *action, gint i)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)action;

    if (!acc || !acc->tkwin || !acc->interp) {
	return FALSE;
    }

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    AtkObject *obj = GetAtkObjectForTkWindow(acc->tkwin);

    if (role == ATK_ROLE_SPIN_BUTTON) {
	if (i < 0 || i > 1) return FALSE;  /* Only support 0 and 1. */

	/* Retrieve current value that was updated at script level. */
	GValue gval = G_VALUE_INIT;
	tk_get_current_value(ATK_VALUE(obj), &gval);

	/* Notify ATK clients. */
	g_signal_emit_by_name(obj, "value-changed");
	g_object_notify(G_OBJECT(obj), "accessible-value");

	g_value_unset(&gval);
	return TRUE;
    }

    /* Handle toggle action for toggle buttons, checkboxes, and radio buttons */
    if ((role == ATK_ROLE_TOGGLE_BUTTON || role == ATK_ROLE_CHECK_BOX ||
	    role == ATK_ROLE_RADIO_BUTTON || role == ATK_ROLE_TOGGLE_BUTTON) && i == 0) {
	/* Toggle the state */
	Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)acc->tkwin);
	if (!hPtr) return FALSE;

	Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
	if (!attrs) return FALSE;

	Tcl_HashEntry *valueEntry = Tcl_FindHashEntry(attrs, "value");
	if (valueEntry) {
	    Tcl_Obj *valObj = Tcl_GetHashValue(valueEntry);
	    const char *currentVal = Tcl_GetString(valObj);

	    /* Toggle between "0" and "1" */
	    const char *newVal = (currentVal && strcmp(currentVal, "1") == 0) ? "0" : "1";

	    Tcl_SetStringObj(valObj, newVal, -1);

	    /* Notify value change */
	    g_signal_emit_by_name(obj, "value-changed");
	    g_object_notify(G_OBJECT(obj), "accessible-value");
	}
    }

    /* Fallback: Generic "click" action for other roles. */
    if (i == 0) {
	/* Retrieve the command string.  */
	Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)acc->tkwin);
	if (!hPtr) return FALSE;

	Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
	if (!attrs) return FALSE;

	Tcl_HashEntry *actionEntry = Tcl_FindHashEntry(attrs, "action");
	if (!actionEntry) return false;

	const char *actionString = Tcl_GetString(Tcl_GetHashValue(actionEntry));
	if (!actionString) return FALSE;

	/* Finally, execute command.  */
	if (Tcl_EvalEx(acc->interp, actionString, -1, TCL_EVAL_GLOBAL) != TCL_OK) {
	    return FALSE;
	}
    }
    return TRUE;
}



static gint tk_action_get_n_actions(AtkAction *action)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)action;
    if (!acc || !acc->tkwin) return 0;

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);

    switch (role) {
    case ATK_ROLE_PUSH_BUTTON:
    case ATK_ROLE_MENU_ITEM:
    case ATK_ROLE_TOGGLE_BUTTON:
	return 1;  /* Click/activate/toggle. */
    case ATK_ROLE_SPIN_BUTTON:
	return 2;  /* Increment + decrement. */
    case ATK_ROLE_CHECK_BOX:
    case ATK_ROLE_RADIO_BUTTON:
	return 1;  /* Toggle/select. */
    case ATK_ROLE_SLIDER:
    case ATK_ROLE_SCROLL_BAR:
    case ATK_ROLE_PROGRESS_BAR:
	return 0;  /* Value-only controls, no actions. */
    default:
	return 0;
    }
}


static const gchar *tk_action_get_name(AtkAction *action, gint i)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)action;
    if (!acc || !acc->tkwin) return NULL;

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);

    switch (role) {
    case ATK_ROLE_PUSH_BUTTON:
    case ATK_ROLE_MENU_ITEM:
	if (i == 0) return "click";
	break;
    case ATK_ROLE_SPIN_BUTTON:
	if (i == 0) return "increment";
	if (i == 1) return "decrement";
	break;
    case ATK_ROLE_CHECK_BOX:
    case ATK_ROLE_RADIO_BUTTON:
    case ATK_ROLE_TOGGLE_BUTTON:
	if (i == 0) return "toggle";
	break;
    default:
	break;
    }
    return NULL;
}

static void tk_atk_action_interface_init(AtkActionIface *iface)
{
    iface->do_action = tk_action_do_action;
    iface->get_n_actions = tk_action_get_n_actions;
    iface->get_name = tk_action_get_name;
}

/*
 * ATK text interface.
 */

static gchar *tk_text_get_text(AtkText *text, gint start_offset, gint end_offset)
{
    if (!TK_ATK_IS_ACCESSIBLE(text)) return NULL;
    TkAtkAccessible *acc = (TkAtkAccessible *) (ATK_OBJECT(text));
    if (!acc || !acc->tkwin || !acc->interp) return NULL;

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    if (role != ATK_ROLE_TEXT && role != ATK_ROLE_ENTRY) return NULL;

    gchar *val = tk_acc_value_dup(acc->tkwin);
    if (!val) return NULL;

    /* Normalize offsets to character indices. */
    const gint total = g_utf8_strlen(val, -1);
    gint start = start_offset < 0 ? 0 : start_offset;
    gint end   = (end_offset < 0 || end_offset > total) ? total : end_offset;
    if (end < start) { /* Avoid ATK assertions downstream. */
	g_free(val);
	return g_strdup("");
    }

    /* Convert char offsets to byte offsets for slicing. */
    const gchar *start_p = g_utf8_offset_to_pointer(val, start);
    const gchar *end_p   = g_utf8_offset_to_pointer(val, end);

    /* Return a newly allocated substring (ATK expects caller-owned memory). */
    gchar *out = g_strndup(start_p, end_p - start_p);
    g_free(val);
    return out;
}

static gint tk_text_get_caret_offset(AtkText *text)
{
    TkAtkAccessible *acc = (TkAtkAccessible *) ATK_OBJECT(text);
    if (!acc || !acc->tkwin || !acc->interp) return 0;

    Tcl_Obj *cmd[3];
    cmd[0] = Tcl_NewStringObj(Tk_PathName(acc->tkwin), -1);
    cmd[1] = Tcl_NewStringObj("index", -1);
    cmd[2] = Tcl_NewStringObj("insert", -1);
    Tcl_IncrRefCount(cmd[0]);
    Tcl_IncrRefCount(cmd[1]);
    Tcl_IncrRefCount(cmd[2]);

    if (Tcl_EvalObjv(acc->interp, 3, cmd, TCL_EVAL_GLOBAL) != TCL_OK) {
	Tcl_DecrRefCount(cmd[0]);
	Tcl_DecrRefCount(cmd[1]);
	Tcl_DecrRefCount(cmd[2]);
	return 0;
    }

    const char *idx = Tcl_GetStringResult(acc->interp);
    Tcl_DecrRefCount(cmd[0]);
    Tcl_DecrRefCount(cmd[1]);
    Tcl_DecrRefCount(cmd[2]);

    /* "line.char" form, convert to offset. */
    int line, col;
    if (sscanf(idx, "%d.%d", &line, &col) == 2) {
	Tcl_Obj *countCmd[5];
	countCmd[0] = Tcl_NewStringObj(Tk_PathName(acc->tkwin), -1);
	countCmd[1] = Tcl_NewStringObj("count", -1);
	countCmd[2] = Tcl_NewStringObj("-chars", -1);
	countCmd[3] = Tcl_NewStringObj("1.0", -1);
	countCmd[4] = Tcl_NewStringObj("insert", -1);
	for (int i=0;i<5;i++) Tcl_IncrRefCount(countCmd[i]);

	int offset = 0;
	if (Tcl_EvalObjv(acc->interp, 5, countCmd, TCL_EVAL_GLOBAL) == TCL_OK) {
	    Tcl_GetIntFromObj(acc->interp, Tcl_GetObjResult(acc->interp), &offset);
	}
	for (int i=0;i<5;i++) Tcl_DecrRefCount(countCmd[i]);

	return offset;
    }

    return 0;
}

static gchar *tk_text_get_selection(
    AtkText *text,
    TCL_UNUSED(gint), /* selection_num */
    gint *start_offset,
    gint *end_offset)
{
    if (!start_offset || !end_offset) return NULL;

    *start_offset = 0;
    *end_offset   = 0;

    if (!TK_ATK_IS_ACCESSIBLE(text)) return NULL;

    TkAtkAccessible *acc = (TkAtkAccessible *)(ATK_OBJECT(text));
    if (!acc || !acc->tkwin || !acc->interp) return NULL;

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    if (role != ATK_ROLE_TEXT && role != ATK_ROLE_ENTRY) return NULL;

    gchar *val = tk_acc_value_dup(acc->tkwin);
    if (!val) return NULL;

    const gint total = g_utf8_strlen(val, -1);
    if (total <= 0) {
	g_free(val);
	return NULL;
    }

    /* Return full selection for now. */
    *start_offset = 0;
    *end_offset   = total;

    return val;  /* Caller will g_free(). */
}

static inline gchar *tk_acc_value_dup(Tk_Window win)
{
    /*
     * Text data is written to the "value" field of the TkAccessibleObject
     * hash table and queried there.
     */

    gchar *v = GetAtkValueForWidget(win);
    return v;
}

static gint tk_text_get_character_count(AtkText *text)
{
    if (!TK_ATK_IS_ACCESSIBLE(text)) return 0;
    TkAtkAccessible *acc = (TkAtkAccessible *)(ATK_OBJECT(text));
    if (!acc || !acc->tkwin || !acc->interp) return 0;

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    if (role != ATK_ROLE_TEXT && role != ATK_ROLE_ENTRY) return 0;

    gchar *val = tk_acc_value_dup(acc->tkwin);
    if (!val) return 0;

    gint count = g_utf8_strlen(val, -1);
    g_free(val);
    return count;
}

static gchar *tk_text_get_text_at_offset(
    AtkText *text,
    TCL_UNUSED(gint), /* offset */
    TCL_UNUSED(AtkTextBoundary), /* boundary_type */
    gint *start_offset,
    gint *end_offset)
{
    if (!TK_ATK_IS_ACCESSIBLE(text)) return NULL;
    TkAtkAccessible *acc = (TkAtkAccessible *)(ATK_OBJECT(text));
    if (!acc || !acc->tkwin || !acc->interp) return NULL;

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    if (role != ATK_ROLE_TEXT && role != ATK_ROLE_ENTRY) return NULL;

    /* For simplicity, return the entire text for any boundary type. */
    gchar *full_text = tk_text_get_text(text, 0, -1);
    if (start_offset) *start_offset = 0;
    if (end_offset) *end_offset = g_utf8_strlen(full_text, -1);
    return full_text;
}

static gchar *tk_text_get_text_after_offset(
    AtkText *text,
    gint offset,
    TCL_UNUSED(AtkTextBoundary), /* boundary_type, */
    gint *start_offset,
    gint *end_offset)
{
    if (!TK_ATK_IS_ACCESSIBLE(text)) return NULL;
    TkAtkAccessible *acc = (TkAtkAccessible *)(ATK_OBJECT(text));
    if (!acc || !acc->tkwin || !acc->interp) return NULL;

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    if (role != ATK_ROLE_TEXT && role != ATK_ROLE_ENTRY) return NULL;

    gchar *full_text = tk_text_get_text(text, 0, -1);
    gint length = g_utf8_strlen(full_text, -1);
    if (start_offset) *start_offset = offset + 1;
    if (end_offset) *end_offset = length;
    g_free(full_text);
    return tk_text_get_text(text, offset + 1, -1);
}

static gchar *tk_text_get_text_before_offset(
    AtkText *text,
    gint offset,
    TCL_UNUSED(AtkTextBoundary), /* boundary_type */
    gint *start_offset,
    gint *end_offset)
{
    if (!TK_ATK_IS_ACCESSIBLE(text)) return NULL;
    TkAtkAccessible *acc = (TkAtkAccessible *)(ATK_OBJECT(text));
    if (!acc || !acc->tkwin || !acc->interp) return NULL;

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    if (role != ATK_ROLE_TEXT && role != ATK_ROLE_ENTRY) return NULL;

    if (start_offset) *start_offset = 0;
    if (end_offset) *end_offset = offset;
    return tk_text_get_text(text, 0, offset);
}

static AtkAttributeSet *tk_text_get_run_attributes(
    AtkText *text,
    TCL_UNUSED(gint), /* offset */
    gint *start_offset,
    gint *end_offset)
{
    if (!TK_ATK_IS_ACCESSIBLE(text)) return NULL;
    TkAtkAccessible *acc = (TkAtkAccessible *)(ATK_OBJECT(text));
    if (!acc || !acc->tkwin || !acc->interp) return NULL;

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    if (role != ATK_ROLE_TEXT && role != ATK_ROLE_ENTRY) return NULL;

    if (start_offset) *start_offset = 0;
    if (end_offset) *end_offset = tk_text_get_character_count(text);
    return NULL; /* No attributes for now. */
}

static AtkAttributeSet *tk_text_get_default_attributes(
    TCL_UNUSED(AtkText *)) /* text */
{
    return NULL; /* No default attributes. */
}

static void tk_text_get_character_extents(
    AtkText *text,
    gint offset,
    gint *x,
    gint *y,
    gint *width,
    gint *height,
    TCL_UNUSED(AtkCoordType)) /* coords */
{
    TkAtkAccessible *acc = (TkAtkAccessible *)(ATK_OBJECT(text));
    if (!acc || !acc->tkwin || !acc->interp) return;

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    if (role != ATK_ROLE_TEXT && role != ATK_ROLE_ENTRY) return;

    /* Approximate character extents based on widget size and text length. */
    gint char_count = tk_text_get_character_count(text);
    if (char_count == 0) return;

    gint widget_width = Tk_Width(acc->tkwin);
    gint char_width = widget_width / char_count;

    if (x) *x = offset * char_width;
    if (y) *y = 0;
    if (width) *width = char_width;
    if (height) *height = Tk_Height(acc->tkwin);
}

static void tk_text_get_range_extents(AtkText *text, gint start_offset, gint end_offset, AtkCoordType coords, AtkTextRectangle *rect)
{
    if (!rect) return;

    TkAtkAccessible *acc = (TkAtkAccessible *)(ATK_OBJECT(text));
    if (!acc || !acc->tkwin || !acc->interp) {
	rect->x = rect->y = rect->width = rect->height = 0;
	return;
    }

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    if (role != ATK_ROLE_TEXT && role != ATK_ROLE_ENTRY) {
	rect->x = rect->y = rect->width = rect->height = 0;
	return;
    }

    gint char_count = tk_text_get_character_count(text);
    if (char_count <= 0) {
	rect->x = rect->y = rect->width = rect->height = 0;
	return;
    }

    /* Clamp offsets. */
    if (start_offset < 0) start_offset = 0;
    if (end_offset > char_count) end_offset = char_count;

    /* Ensure ATK requirement: start < end. */
    if (start_offset >= end_offset) {
	if (start_offset > 0) {
	    start_offset--;
	    end_offset = start_offset + 1;
	} else if (char_count > 0) {
	    start_offset = 0;
	    end_offset = 1;
	} else {
	    rect->x = rect->y = rect->width = rect->height = 0;
	    return;
	}
    }

    gint widget_width  = Tk_Width(acc->tkwin);
    gint widget_height = Tk_Height(acc->tkwin);
    gint char_width    = (widget_width / char_count);
    if (char_width <= 0) char_width = 1;

    gint range_width = (end_offset - start_offset) * char_width;
    if (range_width <= 0) range_width = 1;

    rect->x      = start_offset * char_width;
    rect->y      = 0;
    rect->width  = range_width;
    rect->height = widget_height;

    if (coords == ATK_XY_SCREEN) {
	int root_x, root_y;
	Tk_GetRootCoords(acc->tkwin, &root_x, &root_y);
	rect->x += root_x;
	rect->y += root_y;
    }
}

static gint tk_text_get_offset_at_point(
    AtkText *text,
    gint x,
    TCL_UNUSED(gint), /* y */
    TCL_UNUSED(AtkCoordType)) /* coords*/
{
    TkAtkAccessible *acc = (TkAtkAccessible *)ATK_OBJECT(text);
    if (!acc || !acc->tkwin) return 0;


    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    if (role != ATK_ROLE_TEXT && role != ATK_ROLE_ENTRY) return 0;

    gint char_count = tk_text_get_character_count(text);
    if (char_count == 0) return 0;

    gint widget_width = Tk_Width(acc->tkwin);
    gint char_width = widget_width / char_count;

    return x / char_width;
}

static gboolean tk_text_set_caret_offset(AtkText *text, gint offset)
{
    TkAtkAccessible *acc = (TkAtkAccessible *) ATK_OBJECT(text);
    if (!acc || !acc->tkwin || !acc->interp) return FALSE;

    Tcl_Obj *cmd[3];
    cmd[0] = Tcl_NewStringObj(Tk_PathName(acc->tkwin), -1);
    cmd[1] = Tcl_NewStringObj("mark", -1);
    char buf[64];
    snprintf(buf, sizeof(buf), "1.0 + %d chars", offset);
    cmd[2] = Tcl_NewStringObj(buf, -1);

    Tcl_IncrRefCount(cmd[0]);
    Tcl_IncrRefCount(cmd[1]);
    Tcl_IncrRefCount(cmd[2]);

    Tcl_Obj *args[4] = { cmd[0], cmd[1], Tcl_NewStringObj("set", -1), Tcl_NewStringObj("insert", -1) };
    Tcl_IncrRefCount(args[2]);
    Tcl_IncrRefCount(args[3]);

    int ok = (Tcl_EvalObjv(acc->interp, 4, args, TCL_EVAL_GLOBAL) == TCL_OK);

    for (int i=0;i<4;i++) Tcl_DecrRefCount(args[i]);

    g_signal_emit_by_name(ATK_OBJECT(acc), "text-caret-moved", offset);
    return ok ? TRUE : FALSE;

}

static gboolean tk_text_set_selection(AtkText *text, gint selection_num, gint start_offset, gint end_offset)
{
    if (!TK_ATK_IS_ACCESSIBLE(text) || selection_num != 0)
	return FALSE;

    TkAtkAccessible *acc = (TkAtkAccessible *)ATK_OBJECT(text);
    if (!acc || !acc->tkwin)
	return FALSE;

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    if (role != ATK_ROLE_TEXT && role != ATK_ROLE_ENTRY)
	return FALSE;

    gchar *full_text = tk_acc_value_dup(acc->tkwin);
    if (!full_text)
	return FALSE;

    gint char_count = g_utf8_strlen(full_text, -1);
    if (start_offset < 0) start_offset = 0;
    if (end_offset > char_count) end_offset = char_count;
    if (start_offset > end_offset) {
	gint tmp = start_offset;
	start_offset = end_offset;
	end_offset = tmp;
    }

    const gchar *start_p = g_utf8_offset_to_pointer(full_text, start_offset);
    const gchar *end_p   = g_utf8_offset_to_pointer(full_text, end_offset);
    gchar *selected_text = g_strndup(start_p, end_p - start_p);
    g_free(full_text);

    /* Actual updating of text selection in the hash table is handled
     * at the script level, so just free the text and fire signal.
     */
    g_free(selected_text);
    g_signal_emit_by_name(ATK_OBJECT(acc), "text-selection-changed");
    return TRUE;
}

static gint tk_text_get_n_selections(
    TCL_UNUSED(AtkText *)) /* text */
{
    return 1; /* One selection supported. */
}

static void tk_atk_text_interface_init(AtkTextIface *iface)
{
    iface->get_text = tk_text_get_text;
    iface->get_caret_offset = tk_text_get_caret_offset;
    iface -> get_selection = tk_text_get_selection;
    iface->get_character_count = tk_text_get_character_count;
    iface->get_text_at_offset = tk_text_get_text_at_offset;
    iface->get_text_after_offset = tk_text_get_text_after_offset;
    iface->get_text_before_offset = tk_text_get_text_before_offset;
    iface->get_run_attributes = tk_text_get_run_attributes;
    iface->get_default_attributes = tk_text_get_default_attributes;
    iface->get_character_extents = tk_text_get_character_extents;
    iface->get_offset_at_point = tk_text_get_offset_at_point;
    iface->set_caret_offset = tk_text_set_caret_offset;
    iface->set_selection = tk_text_set_selection;
    iface->get_n_selections = tk_text_get_n_selections;
    iface->get_range_extents = tk_text_get_range_extents;
    iface->get_bounded_ranges = NULL;
}

/*
 * ATK select interface.
 */

static gboolean tk_selection_add_selection(AtkSelection *selection, gint i)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)ATK_OBJECT(selection);
    if (!acc || !acc->tkwin) return FALSE;

    AtkRole parentRole = GetAtkRoleForWidget(acc->tkwin);
    const char *cmd_name = NULL;

    switch (parentRole) {
    case ATK_ROLE_LIST:
    case ATK_ROLE_LIST_BOX:
    case ATK_ROLE_TABLE:
    case ATK_ROLE_TREE:
    case ATK_ROLE_TREE_TABLE:
	cmd_name = "selection set";
	break;
    case ATK_ROLE_MENU:
    case ATK_ROLE_MENU_BAR:
	cmd_name = "activate";
	break;
    default:
	return FALSE;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s %s %d", Tk_PathName(acc->tkwin), cmd_name, i);

    /* Fire selection event. */
    if (Tcl_Eval(acc->interp, cmd) == TCL_OK) {
	TkAtkNotifySelectionChanged(acc->tkwin);
	return TRUE;
    }

    return FALSE;
}

static gboolean tk_selection_remove_selection(AtkSelection *selection, gint i)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)ATK_OBJECT(selection);
    if (!acc || !acc->tkwin) return FALSE;

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s selection clear %d", Tk_PathName(acc->tkwin), i);

    /* Fire selection event. */
    if (Tcl_Eval(acc->interp, cmd) == TCL_OK) {
	TkAtkNotifySelectionChanged(acc->tkwin);
	return TRUE;
    }

    return FALSE;
}

static gboolean tk_selection_clear_selection(AtkSelection *selection)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)ATK_OBJECT(selection);
    if (!acc || !acc->tkwin) return FALSE;

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s selection clear all", Tk_PathName(acc->tkwin));

    /* Fire selection event. */
    if (Tcl_Eval(acc->interp, cmd) == TCL_OK) {
	TkAtkNotifySelectionChanged(acc->tkwin);
	return TRUE;
    }

    return FALSE;
}

static gboolean tk_selection_select_all_selection(AtkSelection *selection)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)ATK_OBJECT(selection);
    if (!acc || !acc->tkwin) return FALSE;

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s selection set all", Tk_PathName(acc->tkwin));

    /* Fire selection event. */
    if (Tcl_Eval(acc->interp, cmd) == TCL_OK) {
	TkAtkNotifySelectionChanged(acc->tkwin);
	return TRUE;
    }

    return FALSE;
}

static gint tk_selection_get_selection_count(AtkSelection *selection)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)ATK_OBJECT(selection);
    if (!acc || !acc->tkwin) return 0;

    AtkRole parentRole = GetAtkRoleForWidget(acc->tkwin);
    const char *selection_cmd = NULL;

    switch (parentRole) {
    case ATK_ROLE_LIST:
    case ATK_ROLE_LIST_BOX:
	selection_cmd = "curselection";
	break;
    case ATK_ROLE_MENU:
    case ATK_ROLE_MENU_BAR:
	selection_cmd = "index active";
	break;
    case ATK_ROLE_TREE:
    case ATK_ROLE_TREE_TABLE:
	selection_cmd = "selection";
	break;
    case ATK_ROLE_TABLE:
	selection_cmd = "curselection";
	break;
    default:
	return 0;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s %s", Tk_PathName(acc->tkwin), selection_cmd);
    if (Tcl_Eval(acc->interp, cmd) != TCL_OK) return 0;

    Tcl_Obj *result = Tcl_GetObjResult(acc->interp);
    Tcl_Size list_size = 0;
    Tcl_Obj **objv = NULL;

    if (Tcl_ListObjGetElements(acc->interp, result, &list_size, &objv) == TCL_OK) {
	return list_size;
    } else {
	/* Single selection case. */
	int index;
	if (Tcl_GetIntFromObj(acc->interp, result, &index) == TCL_OK && index >= 0) {
	    return 1;
	}
    }

    return 0;
}

static gboolean tk_selection_is_child_selected(AtkSelection *selection, gint i)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)ATK_OBJECT(selection);
    if (!acc) return FALSE;

    /* Virtual child. */
    if (!acc->tkwin) {
	AtkObject *parent = atk_object_get_parent(ATK_OBJECT(selection));
	if (parent && ATK_IS_SELECTION(parent)) {
	    gpointer index_ptr = g_object_get_data(G_OBJECT(selection), "tk-index");
	    gint my_index = GPOINTER_TO_INT(index_ptr);
	    return tk_selection_is_child_selected(ATK_SELECTION(parent), my_index);
	}
	return FALSE;
    }

    Tcl_Interp *interp = acc->interp;
    if (!interp) return FALSE;

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    char cmd[256];

    if (role == ATK_ROLE_LIST || role == ATK_ROLE_LIST_BOX) {
	snprintf(cmd, sizeof(cmd), "%s selection includes %d", Tk_PathName(acc->tkwin), i);
	if (Tcl_Eval(interp, cmd) == TCL_OK) {
	    int result;
	    if (Tcl_GetIntFromObj(interp, Tcl_GetObjResult(interp), &result) == TCL_OK) {
		return result ? TRUE : FALSE;
	    }
	}
    } else if (role == ATK_ROLE_TREE || role == ATK_ROLE_TREE_TABLE || role == ATK_ROLE_TABLE) {
	/* Treeview: need to compare against $tree selection. */
	snprintf(cmd, sizeof(cmd), "%s selection", Tk_PathName(acc->tkwin));
	if (Tcl_Eval(interp, cmd) == TCL_OK) {
	    Tcl_Obj *list = Tcl_GetObjResult(interp);
	    Tcl_Size count;
	    Tcl_Obj **elems;
	    if (Tcl_ListObjGetElements(interp, list, &count, &elems) == TCL_OK) {
		for (Tcl_Size j = 0; j < count; j++) {
		    const char *itemid = Tcl_GetString(elems[j]);
		    int sel_idx = -1;
		    snprintf(cmd, sizeof(cmd), "%s index %s", Tk_PathName(acc->tkwin), itemid);
		    if (Tcl_Eval(interp, cmd) == TCL_OK &&
			Tcl_GetIntFromObj(interp, Tcl_GetObjResult(interp), &sel_idx) == TCL_OK) {
			if (sel_idx == i) return TRUE;
		    }
		}
	    }
	}
    } else if (role == ATK_ROLE_MENU || role == ATK_ROLE_MENU_BAR) {
	/* Only active index counts. */
	int active = -1;
	snprintf(cmd, sizeof(cmd), "%s index active", Tk_PathName(acc->tkwin));
	if (Tcl_Eval(interp, cmd) == TCL_OK &&
	    Tcl_GetIntFromObj(interp, Tcl_GetObjResult(interp), &active) == TCL_OK) {
	    return active == i;
	}
    }

    return FALSE;
}

static AtkObject *tk_selection_ref_selection(AtkSelection *selection, gint i)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)ATK_OBJECT(selection);
    if (!acc || !acc->tkwin) return NULL;

    int sel_index = -1;
    Tcl_Interp *interp = acc->interp;
    AtkObject *obj = ATK_OBJECT(acc);

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s curselection", Tk_PathName(acc->tkwin));

    if (Tcl_Eval(interp, cmd) != TCL_OK) return NULL;

    Tcl_Obj *result = Tcl_GetObjResult(interp);
    Tcl_Size list_len = 0;
    Tcl_Obj **elems = NULL;

    AtkRole role = tk_get_role(obj);

    /* Listbox or table: numeric indices. */
    if (Tcl_ListObjGetElements(interp, result, &list_len, &elems) == TCL_OK) {
	if (i < 0 || i >= (gint)list_len) return NULL;

	if (role == ATK_ROLE_LIST || role == ATK_ROLE_LIST_BOX || role == ATK_ROLE_TABLE) {
	    if (Tcl_GetIntFromObj(interp, elems[i], &sel_index) != TCL_OK) return NULL;
	} else if (role == ATK_ROLE_TREE || role == ATK_ROLE_TREE_TABLE) {
	    /* Treeview: get index from item ID. */
	    const char *itemid = Tcl_GetString(elems[i]);
	    char idxcmd[512];
	    snprintf(idxcmd, sizeof(idxcmd), "%s index %s", Tk_PathName(acc->tkwin), itemid);
	    if (Tcl_Eval(interp, idxcmd) != TCL_OK) return NULL;
	    if (Tcl_GetIntFromObj(interp, Tcl_GetObjResult(interp), &sel_index) != TCL_OK) return NULL;
	} else {
	    /* Menu: numeric index. */
	    if (Tcl_GetIntFromObj(interp, elems[i], &sel_index) != TCL_OK) return NULL;
	}
    } else {
	/* Single selection. */
	if (Tcl_GetIntFromObj(interp, result, &sel_index) != TCL_OK) return NULL;
    }

    if (sel_index < 0) return NULL;

    /* Set textual value as child name for ATK. */
    AtkObject *child = tk_ref_child(obj, sel_index);
    if (child) {
	gchar *text = GetAtkValueForWidget(acc->tkwin);
	if (text) {
	    atk_object_set_name(child, text);
	    g_free(text);
	}
    }

    return child;
}


static void tk_atk_selection_interface_init(AtkSelectionIface *iface)
{
    iface->add_selection = tk_selection_add_selection;
    iface->clear_selection = tk_selection_clear_selection;
    iface->get_selection_count = tk_selection_get_selection_count;
    iface->is_child_selected = tk_selection_is_child_selected;
    iface->ref_selection = tk_selection_ref_selection;
    iface->remove_selection = tk_selection_remove_selection;
    iface->select_all_selection = tk_selection_select_all_selection;
}

/*
 * Notify ATK clients (Orca) about a selection change.
 * Updates child names to their textual values and emits selection signals.
 */
void TkAtkNotifySelectionChanged(Tk_Window tkwin)
{
    if (!tkwin) return;

    AtkObject *obj = GetAtkObjectForTkWindow(tkwin);
    if (!obj) return;

    AtkRole role = tk_get_role(obj);
    if (role != ATK_ROLE_LIST && role != ATK_ROLE_LIST_BOX &&
	role != ATK_ROLE_TABLE && role != ATK_ROLE_TREE &&
	role != ATK_ROLE_TREE_TABLE && role != ATK_ROLE_MENU &&
	role != ATK_ROLE_MENU_BAR) {
	return;
    }

    Tcl_Interp *interp = Tk_Interp(tkwin);
    if (!interp) return;

    int n_children = tk_get_n_children(obj);
    if (n_children <= 0) return;

    Tcl_Obj *selection_list = NULL;
    Tcl_Obj **elems = NULL;
    Tcl_Size selection_count = 0;
    char cmd[512] = {0};

    /* Get selection list. */
    switch (role) {
    case ATK_ROLE_LIST:
    case ATK_ROLE_LIST_BOX:
    case ATK_ROLE_TABLE:
	snprintf(cmd, sizeof(cmd) - 1, "%s curselection", Tk_PathName(tkwin));
	break;
    case ATK_ROLE_MENU:
    case ATK_ROLE_MENU_BAR:
    case ATK_ROLE_TREE:
    case ATK_ROLE_TREE_TABLE:
	snprintf(cmd, sizeof(cmd) - 1, "%s selection", Tk_PathName(tkwin));
	break;
    default:
	return;
    }

    if (Tcl_Eval(interp, cmd) == TCL_OK) {
	selection_list = Tcl_GetObjResult(interp);
	if (selection_list &&
	    Tcl_ListObjGetElements(interp, selection_list,
				   &selection_count, &elems) != TCL_OK) {
	    selection_count = 0;
	    elems = NULL;
	}
    }

    /* Find active item. */
    int active_index = -1;
    const char *active_id = NULL;

    if (role == ATK_ROLE_LIST || role == ATK_ROLE_LIST_BOX ||
	role == ATK_ROLE_TABLE || role == ATK_ROLE_MENU ||
	role == ATK_ROLE_MENU_BAR) {

	snprintf(cmd, sizeof(cmd) - 1, "%s index active", Tk_PathName(tkwin));
	if (Tcl_Eval(interp, cmd) == TCL_OK) {
	    Tcl_Obj *result = Tcl_GetObjResult(interp);
	    if (!result || Tcl_GetIntFromObj(interp, result, &active_index) != TCL_OK) {
		active_index = -1;
	    }
	}

    } else if (role == ATK_ROLE_TREE || role == ATK_ROLE_TREE_TABLE) {
	snprintf(cmd, sizeof(cmd) - 1, "%s focus", Tk_PathName(tkwin));
	if (Tcl_Eval(interp, cmd) == TCL_OK) {
	    Tcl_Obj *result = Tcl_GetObjResult(interp);
	    if (result) {
		active_id = Tcl_GetString(result);
		if (active_id && *active_id) {
		    snprintf(cmd, sizeof(cmd) - 1,
			     "%s index {%s}", Tk_PathName(tkwin), active_id);
		    if (Tcl_Eval(interp, cmd) == TCL_OK) {
			Tcl_Obj *idxObj = Tcl_GetObjResult(interp);
			if (idxObj &&
			    Tcl_GetIntFromObj(interp, idxObj, &active_index) != TCL_OK) {
			    active_index = -1;
			}
		    }
		}
	    }
	}
    }

    /* Iterate over children. */
    for (int i = 0; i < n_children; i++) {
	AtkObject *child = tk_ref_child(obj, i);
	if (!child) continue;

	gboolean is_selected = FALSE;

	if (elems && selection_count > 0) {
	    if (role == ATK_ROLE_LIST || role == ATK_ROLE_LIST_BOX ||
		role == ATK_ROLE_TABLE || role == ATK_ROLE_MENU ||
		role == ATK_ROLE_MENU_BAR) {

		for (Tcl_Size j = 0; j < selection_count; j++) {

		      if (elems[j] == NULL) {
			  continue; /* Skip NULL elements. */
	    }
		    int sel_idx = -1;
		    if (Tcl_GetIntFromObj(interp, elems[j], &sel_idx) == TCL_OK &&
			sel_idx == i) {
			is_selected = TRUE;
			break;
		    }
		}

	    } else if (role == ATK_ROLE_TREE || role == ATK_ROLE_TREE_TABLE) {

		for (Tcl_Size j = 0; j < selection_count; j++) {
		    if (elems[j] == NULL) {
			continue; /* Skip NULL elements. */
		    }
		    const char *itemid = Tcl_GetString(elems[j]);
		    if (itemid && *itemid) {
			snprintf(cmd, sizeof(cmd) - 1,
				 "%s index {%s}", Tk_PathName(tkwin), itemid);
			if (Tcl_Eval(interp, cmd) == TCL_OK) {
			    Tcl_Obj *result = Tcl_GetObjResult(interp);
			    int sel_idx = -1;
			    if (result &&
				Tcl_GetIntFromObj(interp, result, &sel_idx) == TCL_OK &&
				sel_idx == i) {
				is_selected = TRUE;
				break;
			    }
			}
		    }
		}
	    }
	}

	atk_object_notify_state_change(child, ATK_STATE_SELECTED, is_selected);
	if (is_selected) {
	    g_signal_emit_by_name(child, "selection-changed");
	}

	/* Focus/active state. */
	if (i == active_index) {
	    atk_object_notify_state_change(child, ATK_STATE_FOCUSED, TRUE);
	    g_signal_emit_by_name(child, "focus-event", TRUE);
	    g_signal_emit_by_name(obj, "active-descendant-changed", child);
	} else {
	    atk_object_notify_state_change(child, ATK_STATE_FOCUSED, FALSE);
	}

	gchar *text = GetAtkValueForWidget(tkwin);
	if (text) {
	    atk_object_set_name(child, text);
	    g_free(text);
	}

	g_object_unref(child);
    }

    g_signal_emit_by_name(obj, "selection-changed");
}


/*
 * Functions to initialize and manage the parent ATK class and object instances.
 */

static void tk_atk_accessible_init(TkAtkAccessible *self)
{
    self->tkwin = NULL;
    self->interp = NULL;
    self->path = NULL;
}

static void tk_atk_accessible_finalize(GObject *gobject)
{
    TkAtkAccessible *self = (TkAtkAccessible*)gobject;
    if (!self) return;

    if (self->tkwin) {
	/* Clean up any virtual children in cache for this window. */
	if (virtual_child_cache) {
	    const char *parent_path = Tk_PathName(self->tkwin);
	    GHashTableIter iter;
	    gpointer key, value;
	    GList *keys_to_remove = NULL;

	    g_hash_table_iter_init(&iter, virtual_child_cache);
	    while (g_hash_table_iter_next(&iter, &key, &value)) {
		const char *cache_key = (const char *)key;
		if (strstr(cache_key, parent_path) == cache_key) {
		    keys_to_remove = g_list_prepend(keys_to_remove, g_strdup(cache_key));
		}
	    }

	    for (GList *l = keys_to_remove; l != NULL; l = l->next) {
		g_hash_table_remove(virtual_child_cache, l->data);
		g_free(l->data);
	    }
	    g_list_free(keys_to_remove);
	}

	InvalidateVirtualChildren(self->tkwin);
	cleanup_virtual_child_cache();

	/* Unregister from tracking structures. */
	UnregisterAtkObjectForTkWindow(self->tkwin);
	if (Tk_IsTopLevel(self->tkwin)) {
	    UnregisterToplevelWindow(ATK_OBJECT(self));
	}
	self->tkwin = NULL;
    }

    g_free(self->path);
    /* Chain up to parent finalizer. */
    G_OBJECT_CLASS(tk_atk_accessible_parent_class)->finalize(gobject);
}

static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    AtkObjectClass *atk_class = ATK_OBJECT_CLASS(klass);

    /* Register custom AT-SPI signals. */
    window_create_signal_id = g_signal_new("window-create",
					   TK_ATK_TYPE_ACCESSIBLE,
					   G_SIGNAL_RUN_LAST,
					   0,
					   NULL, NULL,
					   g_cclosure_marshal_VOID__VOID,
					   G_TYPE_NONE, 0);

    window_activate_signal_id = g_signal_new("window-activate",
					     TK_ATK_TYPE_ACCESSIBLE,
					     G_SIGNAL_RUN_LAST,
					     0,
					     NULL, NULL,
					     g_cclosure_marshal_VOID__VOID,
					     G_TYPE_NONE, 0);

    window_deactivate_signal_id = g_signal_new("window-deactivate",
					       TK_ATK_TYPE_ACCESSIBLE,
					       G_SIGNAL_RUN_LAST,
					       0,
					       NULL, NULL,
					       g_cclosure_marshal_VOID__VOID,
					       G_TYPE_NONE, 0);

    gobject_class->finalize = tk_atk_accessible_finalize;

    /* Map ATK class functions to Tk functions.  */
    atk_class->get_name = tk_get_name;
    atk_class->get_description = tk_get_description;
    atk_class->get_role = tk_get_role;
    atk_class->ref_state_set = tk_ref_state_set;
    atk_class->get_n_children = tk_get_n_children;
    atk_class->ref_child = tk_ref_child;
}

/*
 *----------------------------------------------------------------------
 *
 * Registration and mapping functions. These functions set, track and update
 * the association between Tk windows and ATK objects.
 *
 *----------------------------------------------------------------------
 */

/* Function to complete toplevel registration with proper hierarchy. */
static void RegisterToplevelWindow(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *accessible)
{
    if (!accessible || !tkwin || !G_IS_OBJECT(accessible)) {
	g_warning("RegisterToplevelWindow: Invalid tkwin or accessible");
	return;
    }

    if (!tk_root_accessible) {
	tk_root_accessible = tk_util_get_root();
	if (tk_root_accessible) {
	    tk_set_name(tk_root_accessible, "Tk Application");
	}
    }

    AtkObject *existing = GetAtkObjectForTkWindow(tkwin);
    if (existing && existing != accessible) {
	g_warning("RegisterToplevelWindow: Toplevel %s already registered with different AtkObject",
		  Tk_PathName(tkwin));
	return;
    }

    g_object_ref(accessible);

    /* Ensure proper parent hierarchy for all windows. */
    AtkObject *parentAcc = NULL;
    if (Tk_IsTopLevel(tkwin)) {
	/* True toplevels get root as parent. */
	parentAcc = tk_root_accessible;

	/* Add to toplevel list only for real toplevels. */
	if (!g_list_find(toplevel_accessible_objects, accessible)) {
	    toplevel_accessible_objects = g_list_append(toplevel_accessible_objects, accessible);
	    gint index = g_list_index(toplevel_accessible_objects, accessible);
	    if (index >= 0 && tk_root_accessible) {
		g_signal_emit_by_name(tk_root_accessible, "children-changed::add", index, accessible);
	    }
	}
    } else {
	/* Non-toplevels: find and ensure parent is registered first. */
	Tk_Window parentWin = Tk_Parent(tkwin);
	if (parentWin) {
	    /* Ensure parent is registered before setting hierarchy. */
	    parentAcc = GetAtkObjectForTkWindow(parentWin);
	    if (!parentAcc) {
		/* Recursively create parent accessibility object. */
		parentAcc = TkCreateAccessibleAtkObject(interp, parentWin, Tk_PathName(parentWin));
		if (parentAcc && Tk_IsTopLevel(parentWin)) {
		    /* If parent is toplevel, register it properly. */
		    RegisterToplevelWindow(interp, parentWin, parentAcc);
		}
	    }
	}

	if (!parentAcc) {
	    /* Last resort fallback. */
	    parentAcc = tk_root_accessible;
	}

	/* Emit child-added signal on the actual parent. */
	if (parentAcc && parentAcc != tk_root_accessible) {
	    /* Get current child count before adding. */
	    gint child_count = atk_object_get_n_accessible_children(parentAcc);
	    g_signal_emit_by_name(parentAcc, "children-changed::add", child_count, accessible);
	}
    }

    /* Set parent relationship in ATK. */
    if (parentAcc) {
	atk_object_set_parent(accessible, parentAcc);
    }

    /* Set accessible properties. */
    const gchar *name = tk_get_name(accessible);
    if (name) {
	tk_set_name(accessible, name);
    } else {
	tk_set_name(accessible, Tk_PathName(tkwin));
    }

    AtkRole role = GetAtkRoleForWidget(tkwin);
    atk_object_set_role(accessible, role);
    atk_object_set_description(accessible, name ? name : ((TkAtkAccessible *)accessible)->path);

    TkAtkAccessible_RegisterEventHandlers(tkwin, (TkAtkAccessible *)accessible);
    atk_object_notify_state_change(ATK_OBJECT(accessible), ATK_STATE_SHOWING, TRUE);
}


/* Remove toplevel window from ATK object list. */
static void UnregisterToplevelWindow(AtkObject *accessible)
{
    if (!accessible) return;

    if (g_list_find(toplevel_accessible_objects, accessible)) {
	/* Find position before removal. */
	gint index = g_list_index(toplevel_accessible_objects, accessible);

	/* Remove from toplevel list. */
	toplevel_accessible_objects = g_list_remove(toplevel_accessible_objects, accessible);

	/* Notify about removed child. */
	g_signal_emit_by_name(tk_root_accessible, "children-changed::remove", index, accessible);
    }
}

/* Function to return the toplevel window that contains a given Tk widget. */
Tk_Window GetToplevelOfWidget(Tk_Window tkwin)
{
    if (!tkwin) return NULL;
    Tk_Window current = tkwin;
    if (Tk_IsTopLevel(current)) return current;
    while (current != NULL && Tk_WindowId(current) != None) {
	Tk_Window parent = Tk_Parent(current);
	if (parent == NULL || Tk_IsTopLevel(current)) break;
	current = parent;
    }
    return Tk_IsTopLevel(current) ? current : NULL;
}

/*
 * Root window setup. These are the foundation of the
 * accessibility object system in ATK. atk_get_root() is the
 * critical link to at-spi - it is called by the ATK system
 * and at-spi bridge initialization will silently fail if this
 * function is not implemented. This API is confusing because
 * atk_get_root cannot be called directly in our functions, but
 * it still must be implemented if we are using a custom setup,
 * as we are here.
 */

static AtkObject *tk_util_get_root(void)
{
    if (!tk_root_accessible) {
	TkAtkAccessible *acc = g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
	tk_root_accessible = ATK_OBJECT(acc);
	atk_object_initialize(tk_root_accessible, NULL);
	/* Set proper name and role.  */
	atk_object_set_role(tk_root_accessible, ATK_ROLE_APPLICATION);
	tk_set_name(tk_root_accessible, "Tk Application");
    }

    return tk_root_accessible;
}

/* Core function linking Tk objects to the ATK root object and at-spi. */
AtkObject *atk_get_root(void) {
    return tk_util_get_root();
}

/* ATK-Tk object creation with proper parent/child relationship. */
AtkObject *TkCreateAccessibleAtkObject(Tcl_Interp *interp, Tk_Window tkwin, const char *path)
{
    if (!interp || !tkwin) return NULL;

    AtkObject *existing = GetAtkObjectForTkWindow(tkwin);
    if (existing) return existing;

    TkAtkAccessible *acc = g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
    acc->interp = interp;
    acc->tkwin = tkwin;
    acc->path = g_utf8_make_valid(path, -1);

    AtkObject *obj = ATK_OBJECT(acc);
    AtkRole role = GetAtkRoleForWidget(tkwin);
    atk_object_set_role(obj, role);

    gchar *name = GetAtkNameForWidget(tkwin);
    if (name) {
	atk_object_set_name(obj, name);
	g_free(name);
    }

    /* Check if widget has focus using TkGetFocusWin. */
    if (role == ATK_ROLE_PUSH_BUTTON || role == ATK_ROLE_CHECK_BOX ||
	role == ATK_ROLE_RADIO_BUTTON || role == ATK_ROLE_TOGGLE_BUTTON ||
	role == ATK_ROLE_ENTRY || role == ATK_ROLE_TEXT ||
	role == ATK_ROLE_LIST_ITEM || role == ATK_ROLE_MENU_ITEM ||
	role == ATK_ROLE_TREE_ITEM || role == ATK_ROLE_COMBO_BOX ||
	role == ATK_ROLE_SPIN_BUTTON || role == ATK_ROLE_TOGGLE_BUTTON) {
	TkWindow *focusPtr = TkGetFocusWin((TkWindow*)tkwin);
	acc->is_focused = (focusPtr == (TkWindow*)tkwin) ? 1 : 0;
    }

    RegisterAtkObjectForTkWindow(tkwin, obj);
    TkAtkAccessible_RegisterEventHandlers(tkwin, acc);

    return obj;
}

/*
 * Functions to map Tk window to its corresponding ATK object.
 */

void InitAtkTkMapping(void)
{
    if (!tk_to_atk_map) {
	tk_to_atk_map = g_hash_table_new_full(g_direct_hash, g_direct_equal,
					      NULL, (GDestroyNotify)g_object_unref);
    }
}

void RegisterAtkObjectForTkWindow(Tk_Window tkwin, AtkObject *atkobj)
{
    if (!tkwin || !atkobj) return;
    InitAtkTkMapping();
    g_object_ref(atkobj); /*Increment ref count because hash table takes ownership. */
    g_hash_table_insert(tk_to_atk_map, tkwin, atkobj);
}

AtkObject *GetAtkObjectForTkWindow(Tk_Window tkwin)
{
    if (!tk_to_atk_map || !tkwin) return NULL;
    return (AtkObject *)g_hash_table_lookup(tk_to_atk_map, tkwin);
}

void UnregisterAtkObjectForTkWindow(Tk_Window tkwin)
{
    if (!tk_to_atk_map || !tkwin) return;

    AtkObject *atkobj = (AtkObject *)g_hash_table_lookup(tk_to_atk_map, tkwin);
    if (atkobj) {
	/* If toplevel, unregister from toplevel list. */
	if (g_list_find(toplevel_accessible_objects, atkobj)) {
	    UnregisterToplevelWindow(atkobj);
	}

	g_hash_table_remove(tk_to_atk_map, tkwin);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Event handlers - update Tk and ATK in response to various X events.
 *
 *----------------------------------------------------------------------
 */

/* Configure event handlers. */
void TkAtkAccessible_RegisterEventHandlers(Tk_Window tkwin, void *tkAccessible)
{
    if (!tkwin || !tkAccessible) return;

    Tk_CreateEventHandler(tkwin, StructureNotifyMask,
			  TkAtkAccessible_DestroyHandler, tkAccessible);
    Tk_CreateEventHandler(tkwin, FocusChangeMask,
			  TkAtkAccessible_FocusHandler, tkAccessible);
    Tk_CreateEventHandler(tkwin, SubstructureNotifyMask,
			  TkAtkAccessible_CreateHandler, tkAccessible);
    Tk_CreateEventHandler(tkwin, ConfigureNotify,
			  TkAtkAccessible_ConfigureHandler, tkAccessible);

}

/* Respond to <CreateNotify> events. */
static void TkAtkAccessible_CreateHandler(void *clientData, XEvent *eventPtr)
{
    if (!eventPtr || eventPtr->type != CreateNotify) {
	return;
    }

    Tk_Window tkwin = (Tk_Window)clientData;
    if (!tkwin) {
	return;
    }

    Tcl_Interp *interp = Tk_Interp(tkwin);
    if (!interp) {
	return;
    }

    /* Don’t recreate if we already have an accessible for this window. */
    if (GetAtkObjectForTkWindow(tkwin)) {
	return;
    }

    /* Create a new accessible object for this widget. */
    AtkObject *accObj = TkCreateAccessibleAtkObject(interp, tkwin, Tk_PathName(tkwin));
    if (!accObj) {
	return;
    }

    if (Tk_IsTopLevel(tkwin)) {
	/* Root or non-root toplevel window */
	RegisterToplevelWindow(interp, tkwin, accObj);
    } else {
	/* Child widget inside some toplevel. */
	Tk_Window parentWin = Tk_Parent(tkwin);
	AtkObject *parentAcc = GetAtkObjectForTkWindow(parentWin);
	if (!parentAcc && parentWin) {
	    /* Parent not yet registered: create it now */
	    parentAcc = TkCreateAccessibleAtkObject(interp, parentWin, Tk_PathName(parentWin));
	    if (parentAcc) {
		atk_object_set_parent(parentAcc, GetAtkObjectForTkWindow(GetToplevelOfWidget(parentWin)));
		RegisterAtkObjectForTkWindow(parentWin, parentAcc);
	    }
	}

	if (parentAcc) {
	    atk_object_set_parent(accObj, parentAcc);

	    /* Insert into our Tk→Atk mapping. */
	    RegisterAtkObjectForTkWindow(tkwin, accObj);

	    /* Notify AT clients that a child was added. */
	    gint idx = atk_object_get_n_accessible_children(parentAcc);
	    g_signal_emit_by_name(parentAcc, "children-changed::add", idx, accObj);
	}
    }
}



/* Respond to <DestroyNotify> events. */
static void TkAtkAccessible_DestroyHandler(void *clientData, XEvent *eventPtr)
{
    if (eventPtr->type != DestroyNotify) return;

    TkAtkAccessible *acc = (TkAtkAccessible *)clientData;
    if (!acc) return;

    GObject *obj =  (GObject*)acc;

    tk_atk_accessible_finalize(obj);
}


/* Respond to <Configure> events. */
static void TkAtkAccessible_ConfigureHandler(void *clientData, XEvent *eventPtr)
{
    if (!eventPtr || eventPtr->type != ConfigureNotify) {
	return;
    }

    Tk_Window tkwin = (Tk_Window)clientData;
    if (!tkwin) {
	return;
    }

    AtkObject *accObj = GetAtkObjectForTkWindow(tkwin);
    if (!accObj) {
	return;
    }

    /* Update geometry on configure. */
    gint x, y, w, h;
    tk_get_extents(ATK_COMPONENT(accObj), &x, &y, &w, &h, ATK_XY_SCREEN);

    /* If the widget just became mapped/visible, fire state-change signals. */
    if (Tk_IsMapped(tkwin)) {
	atk_object_notify_state_change(accObj, ATK_STATE_VISIBLE, TRUE);
	atk_object_notify_state_change(accObj, ATK_STATE_SHOWING, TRUE);

	/* For child widgets of a non-root toplevel, also nudge with children-changed. */
	if (!Tk_IsTopLevel(tkwin)) {
	    Tk_Window parentWin = Tk_Parent(tkwin);
	    AtkObject *parentAcc = GetAtkObjectForTkWindow(parentWin);
	    if (parentAcc) {
		gint idx = atk_object_get_n_accessible_children(parentAcc) - 1;
		if (idx < 0) idx = 0;
		g_signal_emit_by_name(parentAcc, "children-changed::add", idx, accObj);
	    }
	}
    } else {
	atk_object_notify_state_change(accObj, ATK_STATE_SHOWING, FALSE);
	atk_object_notify_state_change(accObj, ATK_STATE_VISIBLE, FALSE);
    }
}



/* Respond to <FocusIn/Out> events. */
static void TkAtkAccessible_FocusHandler(void *clientData, XEvent *eventPtr)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)clientData;
    if (!acc || !acc->tkwin || !Tk_IsMapped(acc->tkwin)) return;

    AtkObject *obj = ATK_OBJECT(acc);
    gboolean focused = (eventPtr->type == FocusIn);
    AtkRole role = GetAtkRoleForWidget(acc->tkwin);

    InvalidateVirtualChildren(acc->tkwin);

    /* Emit window-level signals for toplevels. */
    if (role == ATK_ROLE_WINDOW) {
	if (focused) {
	    g_signal_emit_by_name(obj, "window-activate");
	} else {
	    g_signal_emit_by_name(obj, "window-deactivate");
	}
    }

    /* Update focus state. */
    acc->is_focused = focused ? 1 : 0;
    atk_object_notify_state_change(obj, ATK_STATE_FOCUSED, focused);
    g_signal_emit_by_name(obj, "focus-event", focused);

    /* Handle child widget focus */
    if (focused && role != ATK_ROLE_WINDOW) {
	/* Verify this widget is the focused one using TkGetFocusWin. */
	TkWindow *focusPtr = TkGetFocusWin((TkWindow*)acc->tkwin);
	if (focusPtr == (TkWindow*)acc->tkwin) {
	    /* Ensure the child widget's accessible object is created. */
	    AtkObject *child_obj = GetAtkObjectForTkWindow(acc->tkwin);
	    if (!child_obj) {
		child_obj = TkCreateAccessibleAtkObject(acc->interp, acc->tkwin, Tk_PathName(acc->tkwin));
		if (child_obj) {
		    AtkObject *parent_obj = GetAtkObjectForTkWindow(Tk_Parent(acc->tkwin));
		    if (parent_obj) {
			atk_object_set_parent(child_obj, parent_obj);
			TkAtkAccessible_RegisterEventHandlers(acc->tkwin, (TkAtkAccessible *)child_obj);
		    }
		}
	    }
	    if (child_obj) {
		/* Emit focus signals for the child. */
		atk_object_notify_state_change(child_obj, ATK_STATE_FOCUSED, TRUE);
		g_signal_emit_by_name(child_obj, "focus-event", TRUE);

		/* Notify parent of active descendant change for containers. */
		AtkObject *parent_obj = atk_object_get_parent(child_obj);
		if (parent_obj && (role == ATK_ROLE_LIST_BOX || role == ATK_ROLE_MENU ||
				   role == ATK_ROLE_MENU_BAR || role == ATK_ROLE_TREE ||
				   role == ATK_ROLE_TREE_TABLE)) {
		    g_signal_emit_by_name(parent_obj, "active-descendant-changed", child_obj);
		}
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl command implementations - expose these API's to Tcl scripts and
 * initialize ATK integration in Tcl/Tk.
 *
 *----------------------------------------------------------------------
 */


/*
 *----------------------------------------------------------------------
 *
 * EmitSelectionChanged --
 *
 *  Accessibility system notification when selection changed.
 *
 * Results:
 *
 * Accessibility system is made aware when selection or value data is changed.
 *
 * Side effects:
 *
 *  None.
 *
 *----------------------------------------------------------------------
 */

static int EmitSelectionChanged(
    TCL_UNUSED(void *), /* clientData */
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    /* Validate arguments. */
    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "window");
	return TCL_ERROR;
    }

    const char *windowName = Tcl_GetString(objv[1]);
    Tk_Window tkwin = Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));
    if (!tkwin) return TCL_OK;  /* Window not found, nothing to do. */
    InvalidateVirtualChildren(tkwin);

    /* Ensure AtkObject exists for this window. */
    AtkObject *obj = GetAtkObjectForTkWindow(tkwin);
    if (!obj) {
	obj = TkCreateAccessibleAtkObject(interp, tkwin, windowName);
	if (!obj) return TCL_OK;
	TkAtkAccessible_RegisterEventHandlers(tkwin, (TkAtkAccessible *)obj);
    }

    AtkRole role = tk_get_role(obj);

    /* Handle text/entry widgets separately. */
    if (role == ATK_ROLE_TEXT || role == ATK_ROLE_ENTRY) {
	/* Emit a proper "insert" text change and caret-move for typing. */
	g_signal_emit_by_name(obj, "text-changed::insert", 0, 0, NULL);

	/* Compute or estimate caret position */
	int caret_offset = 0;
	if (ATK_IS_TEXT(obj)) {
	    caret_offset = atk_text_get_caret_offset(ATK_TEXT(obj));
	}

	g_signal_emit_by_name(obj, "text-caret-moved", caret_offset);
	return TCL_OK;
    }


    /* Call the robust selection-change notifier. */
    TkAtkNotifySelectionChanged(tkwin);

    /* Handle value-changed for sliders, spin buttons, scrollbars, and progress bars. */
    if (role == ATK_ROLE_SCROLL_BAR || role == ATK_ROLE_SLIDER ||
	role == ATK_ROLE_SPIN_BUTTON || role == ATK_ROLE_PROGRESS_BAR)
	{
	    GValue gval = G_VALUE_INIT;
	    tk_get_current_value(ATK_VALUE(obj), &gval);

	    /* Notify ATK clients. */
	    g_signal_emit_by_name(obj, "value-changed");
	    g_object_notify(G_OBJECT(obj), "accessible-value");

	    g_value_unset(&gval);
	}


    /* Invalidate virtual children if needed (e.g., for spinbox-linked lists) */
    InvalidateVirtualChildren(tkwin);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * EmitFocusChanged --
 *
 * Accessibility system notification when focus changed.
 *
 * Results:
 *
 * Accessibility system is made aware when focus is changed.
 *
 * Side effects:
 *
 * None.
 *
 *----------------------------------------------------------------------
 */

static int EmitFocusChanged(
    TCL_UNUSED(void *), /* clientData */
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "window");
	return TCL_ERROR;
    }

    /* No-op on X11. All work is done in FocusHandler. */

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * IsScreenReaderRunning --
 *
 * Runtime check to see if screen reader is running.
 *
 * Results:
 *
 * Returns if screen reader is active or not.
 *
 * Side effects:
 *
 * None.
 *
 *----------------------------------------------------------------------
 */

static int IsScreenReaderRunning(
    TCL_UNUSED(void *), /* clientData */
    Tcl_Interp *interp,
    TCL_UNUSED(int), /* objc */
    TCL_UNUSED(Tcl_Obj *const *)) /* objv */
{
    int result = IsScreenReaderActive();

    Tcl_SetObjResult(interp, Tcl_NewIntObj(result));
    return TCL_OK;
}

/*
 * Helper function to determine if screen reader is running. Separate function
 * because it can be called internally as well as a Tcl command.
 */
static int IsScreenReaderActive(void)
{
    FILE *fp = popen("pgrep -x orca", "r");
    if (!fp) return 0;

    char buffer[16];
    int running = (fgets(buffer, sizeof(buffer), fp) != NULL);
    pclose(fp);

    return running ? 1 : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkAtkAccessibleObjCmd --
 *
 *   Main command for adding and managing accessibility objects to Tk
 *   widgets on Linux using the ATK accessibility API.
 *
 * Results:
 *
 *   A standard Tcl result.
 *
 * Side effects:
 *
 *   Tk widgets are now accessible to screen readers.
 *
 *----------------------------------------------------------------------
 */

int TkAtkAccessibleObjCmd(
    TCL_UNUSED(void *), /* clientData */
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "window");
	return TCL_ERROR;
    }

    char *windowName = Tcl_GetString(objv[1]);
    if (!windowName) {
	Tcl_SetResult(interp, "Window name cannot be null.", TCL_STATIC);
	return TCL_ERROR;
    }

    Tk_Window tkwin = Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));
    if (tkwin == NULL) {
	Tcl_SetResult(interp, "Invalid window name.", TCL_STATIC);
	return TCL_ERROR;
    }

    /* Check if already registered. */
    if (GetAtkObjectForTkWindow(tkwin)) {
	return TCL_OK;
    }

    /* Create accessible object. */
    TkAtkAccessible *accessible = (TkAtkAccessible*) TkCreateAccessibleAtkObject(interp, tkwin, windowName);
    if (accessible == NULL) {
	Tcl_SetResult(interp, "Failed to create accessible object.", TCL_STATIC);
	return TCL_ERROR;
    }

    /* Always use RegisterToplevelWindow for proper hierarchy. */
    RegisterToplevelWindow(interp, tkwin, ATK_OBJECT(accessible));

    return TCL_OK;
}

#endif

/*
 *----------------------------------------------------------------------
 *
 * TkAtkAccessibility_Init --
 *
 *  Initializes the accessibility module.
 *
 * Results:
 *
 *   A standard Tcl result.
 *
 * Side effects:
 *
 *  Accessibility module is now activated.
 *
 *----------------------------------------------------------------------
 */

#ifdef USE_ATK
int TkAtkAccessibility_Init(Tcl_Interp *interp)
{

    /* Initialize AT-SPI bridge. */
    if (atk_bridge_adaptor_init(NULL, NULL) != 0) {
	Tcl_SetResult(interp, "Failed to initialize AT-SPI bridge", TCL_STATIC);
	return TCL_ERROR;
    }

    /* Get and initialize root accessible. */
    tk_root_accessible = tk_util_get_root();
    if (!tk_root_accessible) {
	Tcl_SetResult(interp, "Failed to create root accessible object", TCL_STATIC);
	return TCL_ERROR;
    }

    /* Activate widget-object hash table mapping. */
    InitAtkTkMapping();

    /*
     * Establish GLib context for event loop processing.
     * We are creating a custom GLib event source that the
     * Tcl event loop will respond to.
     */
    acc_context = ATK_CONTEXT;
    Tcl_CreateEventSource (Atk_Event_Setup, Atk_Event_Check, 0);

    /*
     * GLib is VERY noisy. Shut off warnings to prevent logs/console
     * from being polluted.
     */
    g_log_set_handler("Atk", G_LOG_LEVEL_CRITICAL, ignore_atk_critical, NULL);

    /* Initialize main window and create accessible object. */
    Tk_Window mainWin = Tk_MainWindow(interp);
    if (!mainWin) {
	Tcl_SetResult(interp, "Failed to get main window", TCL_STATIC);
	return TCL_ERROR;
    }

    Tk_MakeWindowExist(mainWin);
    Tk_MapWindow(mainWin);

    AtkObject *main_acc = TkCreateAccessibleAtkObject(interp, mainWin, Tk_PathName(mainWin));
    if (!main_acc) {
	Tcl_SetResult(interp, "Failed to create AtkObject for root window", TCL_STATIC);
	return TCL_ERROR;
    }

    atk_object_set_role(main_acc, ATK_ROLE_WINDOW);
    tk_set_name(main_acc, "Tk Application");
    RegisterAtkObjectForTkWindow(mainWin, main_acc);
    RegisterToplevelWindow(interp, mainWin, main_acc);

    /* Register X event handlers. */
    TkAtkAccessible_RegisterEventHandlers(mainWin, (TkAtkAccessible *)main_acc);

    /* Register Tcl commands. */
    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object",
	    TkAtkAccessibleObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change",
	    EmitSelectionChanged, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_focus_change",
	    EmitFocusChanged, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader",
	    IsScreenReaderRunning, NULL, NULL);

    return TCL_OK;
}
#else

/* Stub command to run if Tk is compiled without accessibility support. */

static int
TkAccessibleStubObjCmd(
    TCL_UNUSED(void *), /* clientData */
    Tcl_Interp *interp,
    TCL_UNUSED(int), /* objc */
    TCL_UNUSED(Tcl_Obj *const *)) /* objv */
{
    static int warned = 0;

    if (!warned) {
	Tcl_SetObjResult(interp,
	Tcl_NewStringObj("Warning: Tk accessibility support not available in this build.", -1));
	warned = 1;
    } else {
	Tcl_SetObjResult(interp, Tcl_NewObj()); /* Empty string after first warning. */
    }

    return TCL_OK;
}


int TkAtkAccessibility_Init(Tcl_Interp *interp)
{
    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", TkAccessibleStubObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", TkAccessibleStubObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_focus_change", TkAccessibleStubObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader", TkAccessibleStubObjCmd, NULL, NULL);
    return TCL_OK;
}
#endif

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */

