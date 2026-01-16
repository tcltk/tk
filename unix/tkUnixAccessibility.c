/*
 * tkUnixAccessibility.c --
 *
 * This file implements accessibility/screen-reader support
 * on Unix-like systems based on the Gnome Accessibility Toolkit.
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
    {"Toggleswitch", ATK_ROLE_TOGGLE_BUTTON},
    {NULL, ATK_ROLE_INVALID }
};


#define ATK_CONTEXT g_main_context_default()

/* Variables for managing ATK objects. */
static AtkObject *tk_root_accessible = NULL;
static GList *toplevel_accessible_objects = NULL;
static GHashTable *tk_to_atk_map = NULL;
extern Tcl_HashTable *TkAccessibilityObject;
static GMainContext *acc_context = NULL;

/* GLib-Tcl event loop integration. */
static void Atk_Event_Setup (void *clientData, int flags);
static void Atk_Event_Check(void *clientData, int flags);
static int Atk_Event_Run(Tcl_Event *event, int flags);
static void ignore_atk_critical(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data);

/* ATK component interface. */
static void tk_get_extents(AtkComponent *component, gint *x, gint *y, gint *width, gint *height, AtkCoordType coord_type);
static gboolean tk_contains(AtkComponent *component, gint x, gint y, AtkCoordType coord_type);
static gboolean tk_grab_focus(AtkComponent *component);
static void tk_atk_component_interface_init(AtkComponentIface *iface);

/* ATK child, attribute and state management. */
static gint tk_get_n_children(AtkObject *obj);
static AtkObject *tk_ref_child(AtkObject *obj, gint i);
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
static gint tk_text_get_character_count(AtkText *text);
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

/* Object lifecycle functions. */
static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass);
static void tk_atk_accessible_init(TkAtkAccessible *accessible);
static void tk_atk_accessible_finalize(GObject *gobject);

/* Registration and mapping functions. */
static void RegisterToplevelWindow(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *accessible);
static void UnregisterToplevelWindow(AtkObject *accessible);
static void RegisterWidgetRecursive(Tcl_Interp *interp, Tk_Window tkwin);
static void EnsureWidgetInAtkHierarchy(Tcl_Interp *interp, Tk_Window tkwin);
static void UpdateAtkFocusChain(Tk_Window focused);
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
static int EmitSelectionChanged(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int EmitFocusChanged(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int IsScreenReaderRunning(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int IsScreenReaderActive(void);
int TkAtkAccessibleObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
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
	Tcl_Event *event = (Tcl_Event *)Tcl_Alloc(sizeof(Tcl_Event));
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

/* Force accessible focus on a Tk widget. */
static gboolean tk_grab_focus(AtkComponent *component)
{
   TkAtkAccessible *acc = (TkAtkAccessible *)component;
   if (!acc || !acc->tkwin || !acc->interp) return FALSE;

   /* Actually give Tk focus to the widget. */
   char cmd[256];
   snprintf(cmd, sizeof(cmd), "focus -force %s", Tk_PathName(acc->tkwin));
   Tcl_Eval(acc->interp, cmd);

   /* Update internal state. */
   acc->is_focused = 1;
   AtkObject *obj = ATK_OBJECT(acc);

   /* Force ATK notifications for focus change. */
   atk_object_notify_state_change(obj, ATK_STATE_FOCUSED, TRUE);
   g_signal_emit_by_name(obj, "focus-event", TRUE);

   /* Help Orca with container navigation. */
   AtkObject *parent = atk_object_get_parent(obj);
   if (parent) {
       /* Notify parent about active descendant */
       g_signal_emit_by_name(parent, "active-descendant-changed", obj);

       /* Also emit children-changed to ensure ATK hierarchy is refreshed. */
       g_signal_emit_by_name(parent, "children-changed::add",
			     atk_object_get_n_accessible_children(parent) - 1,
			     obj);
   }

   return TRUE;
}

static void tk_atk_component_interface_init(AtkComponentIface *iface)
{
    iface->get_extents = tk_get_extents;
    iface->contains    = tk_contains;
    iface->grab_focus  = tk_grab_focus;
}

/*
 * Accessible children, attributes and state. Here we create create accessible
 * objects from native Tk widgets (buttons, entries, etc.), map them them to
 * the appropriate role, and track them.
 */

static gint tk_get_n_children(AtkObject *obj)
{
    if (obj == tk_root_accessible) {
	return g_list_length(toplevel_accessible_objects);
    }

    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    if (!acc || !acc->tkwin || !acc->interp) return 0;

    /* Count only real/native children. */
    int native_count = 0;
    for (TkWindow *childPtr = ((TkWindow*)acc->tkwin)->childList;
	    childPtr != NULL; childPtr = childPtr->nextPtr) {
	native_count++;
    }

    return native_count;
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

    /* Only handle real/native children. */
    TkWindow *childPtr;
    gint index = 0;
    for (childPtr = ((TkWindow*)acc->tkwin)->childList;
	 childPtr != NULL;
	 childPtr = childPtr->nextPtr, index++) {

	if (index == i) {
	    Tk_Window child_tkwin = (Tk_Window)childPtr;
	    AtkObject *child_obj = GetAtkObjectForTkWindow(child_tkwin);

	    /* Always create accessible object if it doesn't exist. */
	    if (!child_obj) {
		child_obj = TkCreateAccessibleAtkObject(acc->interp, child_tkwin,
						       Tk_PathName(child_tkwin));
		if (child_obj) {
		    atk_object_set_parent(child_obj, obj);
		    RegisterAtkObjectForTkWindow(child_tkwin, child_obj);
		    TkAtkAccessible_RegisterEventHandlers(child_tkwin,
							  (TkAtkAccessible *)child_obj);

		    /* Notify ATK about the new child. */
		    gint childCount = atk_object_get_n_accessible_children(obj);
		    g_signal_emit_by_name(obj, "children-changed::add", childCount - 1, child_obj);
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

static AtkRole GetAtkRoleForWidget(Tk_Window win)
{
    if (!win) return ATK_ROLE_UNKNOWN;

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)win);
    if (hPtr) {
	Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
	if (attrs) {
	    Tcl_HashEntry *roleEntry = Tcl_FindHashEntry(attrs, "role");
	    if (roleEntry) {
		const char *result = Tcl_GetString((Tcl_Obj *)Tcl_GetHashValue(roleEntry));
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

    const char *name = Tcl_GetString((Tcl_Obj *)Tcl_GetHashValue(nameEntry));
    return name ? g_utf8_make_valid(name, -1) : NULL;
}

static const gchar *tk_get_name(AtkObject *obj)
{
    if (obj == tk_root_accessible) {
	return "Tk Application";
    }

    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    if (!acc) return NULL;

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

    const char *description = Tcl_GetString((Tcl_Obj *)Tcl_GetHashValue(descriptionEntry));
    return description ? g_utf8_make_valid(description, -1) : NULL;
}

static const gchar *tk_get_description(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    return GetAtkDescriptionForWidget(acc->tkwin);
}

static AtkStateSet *tk_ref_state_set(AtkObject *obj)
{
    AtkStateSet *state_set = atk_state_set_new();
    TkAtkAccessible *acc = (TkAtkAccessible *) obj;

    if (!acc) {
	return state_set;
    }

    /* Always add these basic states. */
    atk_state_set_add_state(state_set, ATK_STATE_ENABLED);
    atk_state_set_add_state(state_set, ATK_STATE_SENSITIVE);

    /* Only add FOCUSABLE if widget can receive focus. */
    if (acc->tkwin) {
	AtkRole role = GetAtkRoleForWidget(acc->tkwin);

	/* Use if-else to avoid switch warning and handle all roles. */
	if (role == ATK_ROLE_PUSH_BUTTON ||
	    role == ATK_ROLE_CHECK_BOX ||
	    role == ATK_ROLE_RADIO_BUTTON ||
	    role == ATK_ROLE_ENTRY ||
	    role == ATK_ROLE_TEXT ||
	    role == ATK_ROLE_COMBO_BOX ||
	    role == ATK_ROLE_SPIN_BUTTON ||
	    role == ATK_ROLE_SLIDER ||
	    role == ATK_ROLE_TOGGLE_BUTTON ||
	    role == ATK_ROLE_LIST_BOX ||
	    role == ATK_ROLE_TREE ||
	    role == ATK_ROLE_LIST_ITEM ||
	    role == ATK_ROLE_TREE_ITEM) {
	    atk_state_set_add_state(state_set, ATK_STATE_FOCUSABLE);
	}

	/* Entry widgets should be EDITABLE, not read-only. */
	if (role == ATK_ROLE_ENTRY || role == ATK_ROLE_TEXT) {
	    /* Check if widget has -state normal or is not disabled */
	    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)acc->tkwin);
	    int is_editable = 1; /* Default to editable. */

	    if (hPtr) {
		Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
		if (attrs) {
		    Tcl_HashEntry *stateEntry = Tcl_FindHashEntry(attrs, "state");
		    if (stateEntry) {
			const char *state = Tcl_GetString((Tcl_Obj *)Tcl_GetHashValue(stateEntry));
			if (state && (strcmp(state, "disabled") == 0 || strcmp(state, "readonly") == 0)) {
			    is_editable = 0;
			}
		    }
		}
	    }

	    /* Add EDITABLE state for normal entry widgets. */
	    if (is_editable) {
		atk_state_set_add_state(state_set, ATK_STATE_EDITABLE);
	    }

	    /* Add SINGLE_LINE for entry widgets (not multiline text). */
	    if (role == ATK_ROLE_ENTRY) {
		atk_state_set_add_state(state_set, ATK_STATE_SINGLE_LINE);
	    } else if (role == ATK_ROLE_TEXT) {
		atk_state_set_add_state(state_set, ATK_STATE_MULTI_LINE);
	    }
	}
    }

    /* Add FOCUSED if widget has focus. */
    if (acc->is_focused) {
	atk_state_set_add_state(state_set, ATK_STATE_FOCUSED);
    }

    /* Always add VISIBLE/SHOWING if widget is mapped. */
    if (acc->tkwin && Tk_IsMapped(acc->tkwin)) {
	atk_state_set_add_state(state_set, ATK_STATE_VISIBLE);
	atk_state_set_add_state(state_set, ATK_STATE_SHOWING);
    }

    /* Toggle state for checkboxes/radiobuttons. */
    if (acc->tkwin) {
	AtkRole role = GetAtkRoleForWidget(acc->tkwin);
	if (role == ATK_ROLE_CHECK_BOX ||
	    role == ATK_ROLE_RADIO_BUTTON ||
	    role == ATK_ROLE_TOGGLE_BUTTON) {

	    const char *value = GetAtkValueForWidget(acc->tkwin);
	    /* Check for proper state values. */
	    if (value) {
		/* For checkboxes/radiobuttons, check if value equals "selected" or "1" or onvalue. */
		if (strcmp(value, "selected") == 0 ||
		    strcmp(value, "1") == 0 ||
		    (value[0] != '0' && value[0] != '\0')) {
		    atk_state_set_add_state(state_set, ATK_STATE_CHECKED);
		}
	    }
	}
    }
    return state_set;
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

    const char *value = Tcl_GetString((Tcl_Obj *)Tcl_GetHashValue(valueEntry));
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
    TkAtkAccessible *acc = (TkAtkAccessible *) action;
    Tcl_Interp *interp;
    Tcl_Obj *cmd[2];
    int result;

    if (!acc || !acc->tkwin || i != 0) {
	return FALSE;
    }

    interp = acc->interp;
    if (!interp) {
	return FALSE;
    }

    /*
     * Call: <widgetPath> invoke
     * This is the ONLY supported way to activate a Tk button from C.
     */
    cmd[0] = Tcl_NewStringObj(Tk_PathName(acc->tkwin), -1);
    cmd[1] = Tcl_NewStringObj("invoke", -1);

    Tcl_IncrRefCount(cmd[0]);
    Tcl_IncrRefCount(cmd[1]);

    result = Tcl_EvalObjv(interp, 2, cmd, TCL_EVAL_GLOBAL);

    Tcl_DecrRefCount(cmd[0]);
    Tcl_DecrRefCount(cmd[1]);

    if (result != TCL_OK) {
	Tcl_ResetResult(interp);
	return FALSE;
    }

    /*
     * Toggle state notification.
     */
    AtkRole role = GetAtkRoleForWidget(acc->tkwin);
    if (role == ATK_ROLE_CHECK_BOX ||
	role == ATK_ROLE_RADIO_BUTTON) {

	const char *value = GetAtkValueForWidget(acc->tkwin);
	gboolean checked = (value && value[0] != '0');

	atk_object_notify_state_change(
	    ATK_OBJECT(acc),
	    ATK_STATE_CHECKED,
	    checked
	);
    }

    return TRUE;
}


static gint tk_action_get_n_actions(AtkAction *action)
{
    TkAtkAccessible *acc = (TkAtkAccessible *) action;
    if (!acc || !acc->tkwin) {
	return 0;
    }

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);

    switch (role) {
    case ATK_ROLE_PUSH_BUTTON:
    case ATK_ROLE_CHECK_BOX:
    case ATK_ROLE_RADIO_BUTTON:
    case ATK_ROLE_TOGGLE_BUTTON:
	return 1;
    default:
	return 0;
    }
}

static const gchar *tk_action_get_name(AtkAction *action, gint i)
{
    if (i != 0) {
	return NULL;
    }

    TkAtkAccessible *acc = (TkAtkAccessible *) action;
    if (!acc || !acc->tkwin) {
	return NULL;
    }

    AtkRole role = GetAtkRoleForWidget(acc->tkwin);

    switch (role) {
    case ATK_ROLE_PUSH_BUTTON:
	return "press";
    case ATK_ROLE_CHECK_BOX:
    case ATK_ROLE_RADIO_BUTTON:
    case ATK_ROLE_TOGGLE_BUTTON:
	return "toggle";
    default:
	return NULL;
    }
}

static void tk_atk_action_interface_init(AtkActionIface *iface)
{
    iface->do_action = tk_action_do_action;
    iface->get_n_actions = tk_action_get_n_actions;
    iface->get_name = tk_action_get_name;
}

/*
 * ATK text interface. These are stub functions to ensure that Orca recognizes
 * text widgets. All accessibility in text data is managed at the script level.
 */

static gchar *tk_text_get_text(
    TCL_UNUSED(AtkText *),
    TCL_UNUSED(gint),
    TCL_UNUSED(gint))
{
    return NULL;
}

static gint tk_text_get_caret_offset(
    TCL_UNUSED(AtkText *))
{
    return -1;
}

static gint tk_text_get_character_count(
    TCL_UNUSED(AtkText *))
{
    return 0;
}


static void tk_atk_text_interface_init(AtkTextIface *iface)
{
    iface->get_text = tk_text_get_text;
    iface->get_caret_offset = tk_text_get_caret_offset;
    iface->get_character_count = tk_text_get_character_count;
    iface->get_selection = NULL;
    iface->get_text_at_offset = NULL;
    iface->get_text_after_offset = NULL;
    iface->get_text_before_offset = NULL;
    iface->get_run_attributes = NULL;
    iface->get_default_attributes = NULL;
    iface->get_character_extents = NULL;
    iface->get_offset_at_point = NULL;
    iface->set_caret_offset = NULL;
    iface->set_selection = NULL;
    iface->get_n_selections = NULL;
    iface->get_range_extents = NULL;
    iface->get_bounded_ranges = NULL;
}

/*
 * ATK select interface. Stubs only since we handle selection at
 * script level for virtual widgets.
 */

static gboolean tk_selection_add_selection(
    TCL_UNUSED(AtkSelection *),
    TCL_UNUSED(gint))
{
    return FALSE;
}

static gboolean tk_selection_remove_selection(
    TCL_UNUSED(AtkSelection *),
    TCL_UNUSED(gint))
{
    return FALSE;
}

static gboolean tk_selection_clear_selection(
    TCL_UNUSED(AtkSelection *))
{
    return FALSE;
}

static gint tk_selection_get_selection_count(
    TCL_UNUSED(AtkSelection *))
{
    return 0;
}

static gboolean tk_selection_is_child_selected(
    TCL_UNUSED(AtkSelection *),
    TCL_UNUSED(gint))
{
    return FALSE;
}

static AtkObject *tk_selection_ref_selection(
    TCL_UNUSED(AtkSelection *),
    TCL_UNUSED(gint))
{
    return NULL;
}

static gboolean tk_selection_select_all_selection(
    TCL_UNUSED(AtkSelection *))
{
    return FALSE;
}

static void tk_atk_selection_interface_init(AtkSelectionIface *iface)
{
    /* Keep minimal interface for compatibility. */
    iface->add_selection = tk_selection_add_selection;
    iface->clear_selection = tk_selection_clear_selection;
    iface->get_selection_count = tk_selection_get_selection_count;
    iface->is_child_selected = tk_selection_is_child_selected;
    iface->ref_selection = tk_selection_ref_selection;
    iface->remove_selection = tk_selection_remove_selection;
    iface->select_all_selection = tk_selection_select_all_selection;
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
	/* Clean up from tracking structures. */
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

    AtkObject *parentAcc = NULL;

    if (Tk_IsTopLevel(tkwin)) {
	parentAcc = tk_root_accessible;

	if (!g_list_find(toplevel_accessible_objects, accessible)) {
	    toplevel_accessible_objects = g_list_append(toplevel_accessible_objects, accessible);
	    gint index = g_list_index(toplevel_accessible_objects, accessible);
	    if (index >= 0 && tk_root_accessible) {
		g_signal_emit_by_name(tk_root_accessible, "children-changed::add", index, accessible);
	    }
	}
    } else {
	Tk_Window parentWin = Tk_Parent(tkwin);
	if (parentWin) {
	    AtkObject *parentObj = GetAtkObjectForTkWindow(parentWin);
	    if (!parentObj) {
		parentObj = TkCreateAccessibleAtkObject(interp, parentWin, Tk_PathName(parentWin));
		if (parentObj) {
		    RegisterAtkObjectForTkWindow(parentWin, parentObj);
		    if (Tk_IsTopLevel(parentWin)) {
			RegisterToplevelWindow(interp, parentWin, parentObj);
		    }
		}
	    }
	    parentAcc = parentObj;
	}

	if (!parentAcc) {
	    parentAcc = tk_root_accessible;
	}

	atk_object_set_parent(accessible, parentAcc);
	RegisterAtkObjectForTkWindow(tkwin, accessible);

	/* Always emit children-changed::add for non-toplevel children. */
	gint child_count = atk_object_get_n_accessible_children(parentAcc);
	g_signal_emit_by_name(parentAcc, "children-changed::add", child_count, accessible);
    }

    const gchar *name = tk_get_name(accessible);
    if (!name || !*name) {
	tk_set_name(accessible, Tk_PathName(tkwin));
    }

    AtkRole role = GetAtkRoleForWidget(tkwin);
    atk_object_set_role(accessible, role);

    TkAtkAccessible_RegisterEventHandlers(tkwin, (TkAtkAccessible *)accessible);

    if (Tk_IsMapped(tkwin)) {
	atk_object_notify_state_change(ATK_OBJECT(accessible), ATK_STATE_VISIBLE, TRUE);
	atk_object_notify_state_change(ATK_OBJECT(accessible), ATK_STATE_SHOWING, TRUE);
    }
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

/* Recursively register widget and all its children with proper events. */
static void RegisterWidgetRecursive(Tcl_Interp *interp, Tk_Window tkwin)
{
    if (!tkwin) return;

    AtkObject *acc = GetAtkObjectForTkWindow(tkwin);

    /* Create accessible object if it doesn't exist. */
    if (!acc) {
	acc = TkCreateAccessibleAtkObject(interp, tkwin, Tk_PathName(tkwin));
	if (!acc) return;

	AtkObject *parentAcc = NULL;

	if (Tk_IsTopLevel(tkwin)) {
	    /* Toplevel window - register with root. */
	    RegisterToplevelWindow(interp, tkwin, acc);
	} else {
	    /* Non-toplevel widget - ensure parent is registered first. */
	    Tk_Window parent = Tk_Parent(tkwin);
	    if (parent) {
		AtkObject *parentObj = GetAtkObjectForTkWindow(parent);
		if (!parentObj) {
		    /* Recursively register parent first. */
		    RegisterWidgetRecursive(interp, parent);
		    parentObj = GetAtkObjectForTkWindow(parent);
		}
		parentAcc = parentObj;
	    }

	    /* Fallback to root accessible if no parent found. */
	    if (!parentAcc) {
		parentAcc = tk_root_accessible;
	    }

	    /* Set parent-child relationship. */
	    atk_object_set_parent(acc, parentAcc);
	    RegisterAtkObjectForTkWindow(tkwin, acc);

	    /* Force children-changed signal to refresh ATK hierarchy. */
	    if (parentAcc) {
		gint child_index = atk_object_get_n_accessible_children(parentAcc);
		g_signal_emit_by_name(parentAcc, "children-changed::add", child_index, acc);
	    }

	    /* Register event handlers for this widget. */
	    TkAtkAccessible_RegisterEventHandlers(tkwin, (TkAtkAccessible *)acc);
	}

	/* Notify visibility if already mapped. */
	if (Tk_IsMapped(tkwin)) {
	    atk_object_notify_state_change(acc, ATK_STATE_VISIBLE, TRUE);
	    atk_object_notify_state_change(acc, ATK_STATE_SHOWING, TRUE);

	    /* Also ensure SHOWING state is set for parents. */
	    Tk_Window current = tkwin;
	    while (current && !Tk_IsTopLevel(current)) {
		Tk_Window parent = Tk_Parent(current);
		if (parent) {
		    AtkObject *pAcc = GetAtkObjectForTkWindow(parent);
		    if (pAcc) {
			atk_object_notify_state_change(pAcc, ATK_STATE_SHOWING, TRUE);
		    }
		}
		current = parent;
	    }
	}

	/* If this widget currently has focus, update focus state. */
	TkWindow *focusPtr = TkGetFocusWin((TkWindow*)tkwin);
	if (focusPtr == (TkWindow*)tkwin) {
	    TkAtkAccessible *tkAcc = (TkAtkAccessible *)acc;
	    tkAcc->is_focused = 1;
	    atk_object_notify_state_change(acc, ATK_STATE_FOCUSED, TRUE);
	    g_signal_emit_by_name(acc, "focus-event", TRUE);

	    /* Notify parent about active descendant. */
	    if (!Tk_IsTopLevel(tkwin)) {
		Tk_Window parent = Tk_Parent(tkwin);
		AtkObject *parentAc = GetAtkObjectForTkWindow(parent);
		if (parentAc) {
		    g_signal_emit_by_name(parentAc, "active-descendant-changed", acc);
		}
	    }
	}
    }

    /* Recursively register all children. */
    TkWindow *child;
    for (child = ((TkWindow*)tkwin)->childList;
	 child != NULL;
	 child = child->nextPtr) {
	RegisterWidgetRecursive(interp, (Tk_Window)child);
    }

    /* After registering all children, emit one more children-changed
     * for this container to ensure ATK sees all children. */
    if (acc) {
	g_signal_emit_by_name(acc, "children-changed::add",
			      atk_object_get_n_accessible_children(acc),
			      NULL);
    }
}

 /*  Function to ensure a widget and all its ancestors are in the ATK hierarchy. */
static void EnsureWidgetInAtkHierarchy(Tcl_Interp *interp, Tk_Window tkwin)
{
    if (!tkwin) return;

    /* First ensure all ancestors exist. */
    Tk_Window current = tkwin;
    GList *widgets_to_process = NULL;

    /* Collect all widgets from leaf to root. */
    while (current) {
	widgets_to_process = g_list_prepend(widgets_to_process, current);
	if (Tk_IsTopLevel(current)) break;
	current = Tk_Parent(current);
    }

    /* Process from root to leaf to ensure proper parent-child relationships. */
    GList *iter;
    for (iter = widgets_to_process; iter != NULL; iter = iter->next) {
	Tk_Window win = (Tk_Window)iter->data;
	AtkObject *acc = GetAtkObjectForTkWindow(win);

	if (!acc) {
	    acc = TkCreateAccessibleAtkObject(interp, win, Tk_PathName(win));
	    if (acc) {
		/* Set up parent relationship. */
		if (!Tk_IsTopLevel(win)) {
		    Tk_Window parent = Tk_Parent(win);
		    AtkObject *parentAcc = GetAtkObjectForTkWindow(parent);
		    if (parentAcc) {
			atk_object_set_parent(acc, parentAcc);

			/* Notify ATK about the new child. */
			gint childCount = atk_object_get_n_accessible_children(parentAcc);
			g_signal_emit_by_name(parentAcc, "children-changed::add", childCount - 1, acc);
		    }
		}

		RegisterAtkObjectForTkWindow(win, acc);
		TkAtkAccessible_RegisterEventHandlers(win, (TkAtkAccessible *)acc);

		/* Update state if widget is mapped. */
		if (Tk_IsMapped(win)) {
		    atk_object_notify_state_change(acc, ATK_STATE_VISIBLE, TRUE);
		    atk_object_notify_state_change(acc, ATK_STATE_SHOWING, TRUE);
		}
	    }
	}
    }

    g_list_free(widgets_to_process);
}


 /*  Function to update the ATK focus chain when a widget receives focus. */
 static void UpdateAtkFocusChain(Tk_Window focused)
{
    if (!focused) return;

    Tcl_Interp *interp = Tk_Interp(focused);
    if (!interp) return;

    /* Ensure widget is in ATK hierarchy. */
    EnsureWidgetInAtkHierarchy(interp, focused);

    AtkObject *focusedAcc = GetAtkObjectForTkWindow(focused);
    if (!focusedAcc) return;

    TkAtkAccessible *focusedTkAcc = (TkAtkAccessible *)focusedAcc;

    /* Update focus state. */
    focusedTkAcc->is_focused = 1;
    atk_object_notify_state_change(focusedAcc, ATK_STATE_FOCUSED, TRUE);
    g_signal_emit_by_name(focusedAcc, "focus-event", TRUE);

    /* Walk up the hierarchy and update parent focus states. */
    Tk_Window current = focused;
    while (current && !Tk_IsTopLevel(current)) {
	Tk_Window parent = Tk_Parent(current);
	if (parent) {
	    AtkObject *parentAcc = GetAtkObjectForTkWindow(parent);
	    if (parentAcc) {
		/* Notify parent about active descendant. */
		AtkObject *childAcc = GetAtkObjectForTkWindow(current);
		if (childAcc) {
		    g_signal_emit_by_name(parentAcc, "active-descendant-changed", childAcc);
		}

		/* Also emit children-changed to refresh ATK's view. */
		g_signal_emit_by_name(parentAcc, "children-changed::add",
				      atk_object_get_n_accessible_children(parentAcc) - 1,
				      childAcc);
	    }
	}
	current = parent;
    }

    /* If this is a toplevel, emit window activation. */
    if (Tk_IsTopLevel(focused)) {
	g_signal_emit_by_name(focusedAcc, "window-activate");
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
	TkAtkAccessible *acc = (TkAtkAccessible *)g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
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

    TkAtkAccessible *acc = (TkAtkAccessible *)g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
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
	role == ATK_ROLE_LIST_ITEM ||
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

    Tk_Window parentWin = (Tk_Window)clientData;
    if (!parentWin) return;

    Tcl_Interp *interp = Tk_Interp(parentWin);
    if (!interp) return;

    Window childWindow = eventPtr->xcreatewindow.window;
    Tk_Window childWin = Tk_IdToWindow(Tk_Display(parentWin), childWindow);
    if (!childWin) return;

    if (GetAtkObjectForTkWindow(childWin)) {
	return; /* Already registered. */
    }

    AtkObject *childAcc = TkCreateAccessibleAtkObject(interp, childWin, Tk_PathName(childWin));
    if (!childAcc) return;

    AtkObject *parentAcc = GetAtkObjectForTkWindow(parentWin);
    if (!parentAcc) {
	parentAcc = TkCreateAccessibleAtkObject(interp, parentWin, Tk_PathName(parentWin));
	if (parentAcc) {
	    RegisterAtkObjectForTkWindow(parentWin, parentAcc);
	    if (Tk_IsTopLevel(parentWin)) {
		RegisterToplevelWindow(interp, parentWin, parentAcc);
	    }
	}
    }

    if (!parentAcc) {
	parentAcc = tk_root_accessible;
    }

    atk_object_set_parent(childAcc, parentAcc);
    RegisterAtkObjectForTkWindow(childWin, childAcc);
    TkAtkAccessible_RegisterEventHandlers(childWin, (TkAtkAccessible *)childAcc);

    /* Emit children-changed::add.*/
    gint idx = atk_object_get_n_accessible_children(parentAcc);
    g_signal_emit_by_name(parentAcc, "children-changed::add", idx, childAcc);

    /* Notify visibility if mapped. */
    if (Tk_IsMapped(childWin)) {
	atk_object_notify_state_change(childAcc, ATK_STATE_VISIBLE, TRUE);
	atk_object_notify_state_change(childAcc, ATK_STATE_SHOWING, TRUE);
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
    if (!acc || !acc->tkwin) return;

    gboolean focused = (eventPtr->type == FocusIn);
    AtkObject *obj = ATK_OBJECT(acc);
    AtkRole role = GetAtkRoleForWidget(acc->tkwin);

    /* Update this widget's focus state. */
    acc->is_focused = focused ? 1 : 0;
    atk_object_notify_state_change(obj, ATK_STATE_FOCUSED, focused);
    g_signal_emit_by_name(obj, "focus-event", focused);

    /* Track the last focused widget globally. */
    static Tk_Window last_focused_win = NULL;

    if (focused) {
	/* Widget gained focus - update the global tracker. */
	last_focused_win = acc->tkwin;

	/* Notify parent container about active descendant. */
	if (role != ATK_ROLE_WINDOW) {
	    AtkObject *parent = atk_object_get_parent(obj);
	    if (parent) {
		g_signal_emit_by_name(parent, "active-descendant-changed", obj);

		/* Also emit children-changed to refresh ATK's view. */
		g_signal_emit_by_name(parent, "children-changed::add",
				      atk_object_get_n_accessible_children(parent) - 1,
				      obj);
	    }
	}
    } else {
	/* Widget lost focus - clear from global tracker if it was this widget. */
	if (last_focused_win == acc->tkwin) {
	    last_focused_win = NULL;
	}
    }

    /* Handle window activation/deactivation for toplevels. */
    if (role == ATK_ROLE_WINDOW) {
	if (focused) {
	    g_signal_emit_by_name(obj, "window-activate");
	} else {
	    g_signal_emit_by_name(obj, "window-deactivate");
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
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "window");
	return TCL_ERROR;
    }

    const char *windowName = Tcl_GetString(objv[1]);
    Tk_Window tkwin = Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));
    if (!tkwin) return TCL_OK;

    /* Ensure AtkObject exists. */
    AtkObject *obj = GetAtkObjectForTkWindow(tkwin);
    if (!obj) {
	obj = TkCreateAccessibleAtkObject(interp, tkwin, windowName);
	if (!obj) return TCL_OK;
	TkAtkAccessible_RegisterEventHandlers(tkwin, (TkAtkAccessible *)obj);
    }

    AtkRole role = GetAtkRoleForWidget(tkwin);

    /* For checkboxes and radiobuttons, emit state-changed signal. */
    if (role == ATK_ROLE_CHECK_BOX || role == ATK_ROLE_RADIO_BUTTON || role == ATK_ROLE_TOGGLE_BUTTON) {
	const char *value = GetAtkValueForWidget(tkwin);
	gboolean checked = FALSE;

	if (value) {
	    if (strcmp(value, "selected") == 0 || strcmp(value, "1") == 0) {
		checked = TRUE;
	    }
	}

	/* Emit the state change notification */
	atk_object_notify_state_change(obj, ATK_STATE_CHECKED, checked);
    }

    /* For value-supporting widgets, emit text-changed or value-changed */
    if (role == ATK_ROLE_ENTRY || role == ATK_ROLE_TEXT || role == ATK_ROLE_COMBO_BOX) {
	/* Use text-changed for text-based widgets. */
	g_signal_emit_by_name(obj, "text-changed::insert", 0, 0);
    } else if (role == ATK_ROLE_SPIN_BUTTON || role == ATK_ROLE_SLIDER ||
	       role == ATK_ROLE_PROGRESS_BAR || role == ATK_ROLE_SCROLL_BAR) {
	/* For numeric widgets, emit value-changed on the AtkValue interface. */
	if (ATK_IS_VALUE(obj)) {
	    g_object_notify(G_OBJECT(obj), "accessible-value");
	}
    }

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
    Tcl_Size objc,
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
    TCL_UNUSED(Tcl_Size), /* objc */
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
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "window");
	return TCL_ERROR;
    }

    char *windowName = Tcl_GetString(objv[1]);
    if (!windowName) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Window name cannot be null.", -1));
	return TCL_ERROR;
    }

    Tk_Window tkwin = Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));
    if (tkwin == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Invalid window name.", -1));
	return TCL_ERROR;
    }

    /* Use the recursive registration. */
    RegisterWidgetRecursive(interp, tkwin);

    /* Also ensure the entire hierarchy is in ATK. */
    EnsureWidgetInAtkHierarchy(interp, tkwin);

    /* If widget has focus, update ATK focus chain. */
    TkWindow *focusPtr = TkGetFocusWin((TkWindow*)tkwin);
    if (focusPtr == (TkWindow*)tkwin) {
	UpdateAtkFocusChain(tkwin);
    }

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

    /* Establish GLib context for event loop processing. */
    acc_context = ATK_CONTEXT;
    Tcl_CreateEventSource(Atk_Event_Setup, Atk_Event_Check, 0);

    /* Shut off GLib warnings. */
    g_log_set_handler("Atk", G_LOG_LEVEL_CRITICAL, ignore_atk_critical, NULL);
    g_log_set_handler("GLib-GObject", G_LOG_LEVEL_CRITICAL, ignore_atk_critical, NULL);

    /* Initialize main window */
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

    /* Recursively register ALL existing widgets. */
    RegisterWidgetRecursive(interp, mainWin);

	/* Force initial children-changed signals for all toplevels (helps Orca at startup).  */
    GList *l;
    for (l = toplevel_accessible_objects; l != NULL; l = l->next) {
	AtkObject *top = ATK_OBJECT(l->data);
	gint idx = g_list_index(toplevel_accessible_objects, top);
	if (idx >= 0 && tk_root_accessible) {
	    g_signal_emit_by_name(tk_root_accessible, "children-changed::add", idx, top);
	}

	/* Also notify showing if mapped */
	if (Tk_IsMapped(((TkAtkAccessible*)top)->tkwin)) {
	    atk_object_notify_state_change(top, ATK_STATE_SHOWING, TRUE);
	    atk_object_notify_state_change(top, ATK_STATE_VISIBLE, TRUE);
	}
    }

    /* Register X event handlers for main window. */
    TkAtkAccessible_RegisterEventHandlers(mainWin, (TkAtkAccessible *)main_acc);

    /* Register Tcl commands. */
    Tcl_CreateObjCommand2(interp, "::tk::accessible::add_acc_object",
	    TkAtkAccessibleObjCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::accessible::emit_selection_change",
	    EmitSelectionChanged, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::accessible::emit_focus_change",
	    EmitFocusChanged, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::accessible::check_screenreader",
	    IsScreenReaderRunning, NULL, NULL);

    return TCL_OK;
}
#else
/* Stub command to run if Tk is compiled without accessibility support. */

static int
TkAccessibleStubObjCmd(
    TCL_UNUSED(void *), /* clientData */
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size), /* objc */
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
    Tcl_CreateObjCommand2(interp, "::tk::accessible::add_acc_object", TkAccessibleStubObjCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::accessible::emit_selection_change", TkAccessibleStubObjCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::accessible::emit_focus_change", TkAccessibleStubObjCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::accessible::check_screenreader", TkAccessibleStubObjCmd, NULL, NULL);
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
