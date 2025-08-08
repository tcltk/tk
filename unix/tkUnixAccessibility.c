/*
 * tkUnixAccessibility.c --
 *
 *
 This file implements accessibility/screen-reader support
 * on Unix-like systems based on the Gnome Accessibility Toolkit,
 * the standard accessibility library for X11 systems.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 2006, Marcus von Appen
 * Copyright (c) 2019-2025 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <tcl.h>
#include <tk.h>
#include "tkInt.h"

#ifdef USE_ATK
#include <atk/atk.h>
#include <atk/atktext.h>
#include <atk-bridge.h>
#include <dbus/dbus.h>
#include <glib.h>

/* Structs for custom Atk objects bound to Tk. */
typedef struct _TkAtkAccessible {
    AtkObject parent;
    Tk_Window tkwin;
    Tcl_Interp *interp;
    char *path;
    gchar *cached_name;
    gchar *cached_description;
    gchar *cached_value;
    AtkRole cached_role;
    gint x, y, width, height;
    gboolean is_mapped;
    gboolean has_focus;
} TkAtkAccessible;

typedef struct _TkAtkAccessibleClass {
    AtkObjectClass parent_class;
} TkAtkAccessibleClass;


/* Structs for passing data to main thread. */
typedef struct {
    GMutex mutex;
    GCond cond;
    gboolean done;
    gpointer result;

    gpointer (*func)(gpointer user_data);
    gpointer user_data;
} TkMainThreadCall;

typedef struct {
    Tk_Window tkwin;
    Tcl_Interp *interp;
    gpointer result;
} MainThreadData;

/* Structs to map Tk roles into Atk roles. */

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
    {"Listbox", ATK_ROLE_LIST},
    {"Menu", ATK_ROLE_MENU},
    {"Tree", ATK_ROLE_TREE},
    {"Notebook", ATK_ROLE_PAGE_TAB},
    {"Progressbar", ATK_ROLE_PROGRESS_BAR},
    {"Radiobutton",ATK_ROLE_RADIO_BUTTON},
    {"Scale", ATK_ROLE_SLIDER},
    {"Spinbox", ATK_ROLE_SPIN_BUTTON},
    {"Table", ATK_ROLE_TABLE},
    {"Text", ATK_ROLE_TEXT},
    {"Toplevel", ATK_ROLE_WINDOW},
    {"Frame", ATK_ROLE_PANEL},
    {NULL, 0}
};

/* Structs for signals data. */
typedef struct {
    AtkObject *obj;
    GValue value;
} ValueChangedData;

typedef struct {
    AtkObject *obj;
    gboolean state;
} FocusEventData;

typedef struct {
    AtkObject *obj;
    gchar *name;
    gboolean state;
} StateChangeData;

typedef struct {
    AtkObject *obj;
    AtkRectangle rect;
} BoundsChangedData;

typedef struct {
    AtkObject *parent;
    gint index;
    AtkObject *child;
} ChildrenChangedRemoveData;

typedef struct {
    gint index;
    AtkObject *child;
} ChildrenChangedAddData;


/* Variables for managing Atk objects. */
static AtkObject *tk_root_accessible = NULL;
static GList *toplevel_accessible_objects = NULL; /* This list will hold refs to toplevels. */
static GHashTable *tk_to_atk_map = NULL; /* Maps Tk_Window to AtkObject. */
extern Tcl_HashTable *TkAccessibilityObject; /* Hash table for managing accessibility attributes. */

/* Thread management functions. */
gpointer RunOnMainThread(gpointer (*func)(gpointer user_data), gpointer user_data);
void RunOnMainThreadAsync(GSourceFunc func, gpointer user_data);
void EmitBoundsChanged(AtkObject *obj, gint x, gint y, gint width, gint height);
static gboolean run_main_thread_callback(gpointer data);

/* Helper functions for main thread operations. */
static gpointer get_atk_role_for_widget_main(gpointer data);
static gpointer get_window_name_main(gpointer data);
static gpointer get_window_geometry_main(gpointer data);
static gpointer is_window_mapped_main(gpointer data);
static gpointer get_focus_window_main(gpointer data);
static gpointer get_window_parent_main(gpointer data);
static gpointer make_window_exist_main(gpointer data);
static gpointer map_window_main(gpointer data);
static gpointer is_toplevel_main(gpointer data);
static gpointer find_hash_entry_main(gpointer data);
static gpointer get_hash_value_main(gpointer data);
static gpointer find_hash_entry_by_key_main(gpointer data);
static gpointer get_hash_string_value_main(gpointer data);
static gpointer name_to_window_main(gpointer data);
static gpointer get_main_window_main(gpointer data);
static gpointer get_window_handle_main(gpointer data);

/* Signal emission helpers. */
static gboolean emit_children_changed_add(gpointer data);
static gboolean emit_children_changed_remove(gpointer data);
static gboolean emit_value_changed(gpointer data);
static gboolean emit_text_selection_changed(gpointer data);
static gboolean emit_focus_event(gpointer data);
static gboolean emit_state_change(gpointer data);
static gboolean emit_bounds_changed(gpointer data);

/* ATK interface implementations. */
static void tk_get_extents(AtkComponent *component, gint *x, gint *y, gint *width, gint *height, AtkCoordType coord_type);
static gboolean tk_contains(AtkComponent *component, gint x, gint y, AtkCoordType coord_type);
static void tk_atk_component_interface_init(AtkComponentIface *iface);
static gint tk_get_n_children(AtkObject *obj);
static AtkObject *tk_ref_child(AtkObject *obj, gint i);
static AtkRole GetAtkRoleForWidget(Tk_Window win);
static AtkRole tk_get_role(AtkObject *obj);
static const gchar *tk_get_name(AtkObject *obj);
static void tk_set_name(AtkObject *obj, const gchar *name);
static const gchar *tk_get_description(AtkObject *obj);
static void tk_get_current_value(AtkValue *obj, GValue *value);
static void tk_atk_value_interface_init(AtkValueIface *iface);
static AtkStateSet *tk_ref_state_set(AtkObject *obj);
static gboolean tk_action_do_action(AtkAction *action, gint i);
static gint tk_action_get_n_actions(AtkAction *action);
static const gchar *tk_action_get_name(AtkAction *action, gint i);
static void tk_atk_action_interface_init(AtkActionIface *iface);
static gchar *sanitize_utf8(const gchar *str);

/* Object lifecycle functions. */
static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass);
static void tk_atk_accessible_init(TkAtkAccessible *accessible);
static void tk_atk_accessible_finalize(GObject *gobject);

/* Registration and mapping functions. */
static void RegisterChildWidgets(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *parent_obj);
static void RegisterToplevelWindow(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *accessible);
AtkObject *TkCreateAccessibleAtkObject(Tcl_Interp *interp, Tk_Window tkwin, const char *path);
void InitAtkTkMapping(void);
void RegisterAtkObjectForTkWindow(Tk_Window tkwin, AtkObject *atkobj);
AtkObject *GetAtkObjectForTkWindow(Tk_Window tkwin);
void UnregisterAtkObjectForTkWindow(Tk_Window tkwin);
static AtkObject *tk_util_get_root(void);
AtkObject *atk_get_root(void);

/* Cache update functions. */
static void UpdateGeometryCache(TkAtkAccessible *acc);
static void UpdateNameCache(TkAtkAccessible *acc);
static void UpdateDescriptionCache(TkAtkAccessible *acc);
static void UpdateValueCache(TkAtkAccessible *acc);
static void UpdateRoleCache(TkAtkAccessible *acc);
static void UpdateStateCache(TkAtkAccessible *acc);

/* Event handlers. */
void TkAtkAccessible_RegisterEventHandlers(Tk_Window tkwin, void *tkAccessible);
static void TkAtkAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_ConfigureHandler(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_MapHandler(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_UnmapHandler(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_FocusHandler(ClientData clientData, XEvent *eventPtr);

/* Tcl command implementations. */
static int EmitSelectionChanged(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int EmitFocusChanged(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int AtkEventLoop(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int TkAtkAccessibleObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int TkAtkAccessibility_Init(Tcl_Interp *interp);


/* Define custom Atk object bridged to Tcl/Tk. */
#define TK_ATK_TYPE_ACCESSIBLE (tk_atk_accessible_get_type())
G_DEFINE_TYPE_WITH_CODE(TkAtkAccessible, tk_atk_accessible, ATK_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(ATK_TYPE_COMPONENT, tk_atk_component_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_ACTION, tk_atk_action_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_VALUE, tk_atk_value_interface_init)
			)

/* Helper function to integrate strings. */			
    static gchar *sanitize_utf8(const gchar *str) {
    if (!str) return NULL;
    return g_utf8_make_valid(str, -1);
}

/*
 *----------------------------------------------------------------------
 *
 * Thread management functions. at-spi works on background/worker threads
 * and calls to Tcl/Tk and Atk must be directed to the main thread.  
 *
 *----------------------------------------------------------------------
 */
			

/* Callback function to main thread. */
static gboolean run_main_thread_callback(gpointer data)
{
    TkMainThreadCall *call = (TkMainThreadCall *)data;

    call->result = call->func(call->user_data);

    g_mutex_lock(&call->mutex);
    call->done = TRUE;
    g_cond_signal(&call->cond);
    g_mutex_unlock(&call->mutex);

    return G_SOURCE_REMOVE;
}


/* Run a function on the main thread and wait for the result. */
gpointer RunOnMainThread(gpointer (*func)(gpointer), gpointer user_data)
{
    if (g_main_context_is_owner(g_main_context_default())) {
	return func(user_data);
    }

    TkMainThreadCall call;
    call.func = func;
    call.user_data = user_data;
    call.result = NULL;
    call.done = FALSE;

    g_mutex_init(&call.mutex);
    g_cond_init(&call.cond);

    g_mutex_lock(&call.mutex);
    g_main_context_invoke(NULL, run_main_thread_callback, &call);
    while (!call.done) {
	g_cond_wait(&call.cond, &call.mutex);
    }
    g_mutex_unlock(&call.mutex);

    g_mutex_clear(&call.mutex);
    g_cond_clear(&call.cond);

    return call.result;
}

/* Run a function on the main thread asynchronously (no wait). */
void RunOnMainThreadAsync(GSourceFunc func, gpointer user_data)
{
    if (g_main_context_is_owner(g_main_context_default())) {
	func(user_data);
    } else {
	g_main_context_invoke(NULL, func, user_data);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Helper functions for main thread operations.
 *
 *----------------------------------------------------------------------
 */

/* Get accessible role. */
static gpointer get_atk_role_for_widget_main(gpointer data) {
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)(uintptr_t)GetAtkRoleForWidget(mt_data->tkwin);
}

/* Get accessible name. */
static gpointer get_window_name_main(gpointer data) {
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)Tk_PathName(mt_data->tkwin);
}

/* Get window geometry. */
static gpointer get_window_geometry_main(gpointer data) {
    MainThreadData *mt_data = (MainThreadData *)data;
    Tk_Window tkwin = mt_data->tkwin;
    gint *geometry = g_new(gint, 4);
    geometry[0] = Tk_X(tkwin);
    geometry[1] = Tk_Y(tkwin);
    geometry[2] = Tk_Width(tkwin);
    geometry[3] = Tk_Height(tkwin);
    return geometry;
}

/* Check if window is mapped. */
static gpointer is_window_mapped_main(gpointer data) {
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)(uintptr_t)Tk_IsMapped(mt_data->tkwin);
}

/* Get focused window. */
static gpointer get_focus_window_main(gpointer data) {
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)TkGetFocusWin((TkWindow *)mt_data->tkwin);
}

/* Get parent winodw. */
static gpointer get_window_parent_main(gpointer data) {
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)Tk_Parent(mt_data->tkwin);
}

/* Force window creation. */
static gpointer make_window_exist_main(gpointer data) {
    MainThreadData *mt_data = (MainThreadData *)data;
    Tk_MakeWindowExist(mt_data->tkwin);
    return NULL;
}

/* Force window mapping. */
static gpointer map_window_main(gpointer data) {
    MainThreadData *mt_data = (MainThreadData *)data;
    Tk_MapWindow(mt_data->tkwin);
    return NULL;
}

/* Check if a window has a window ID.  */
static gpointer has_window_id_main(gpointer data)
{
    MainThreadData *mt_data = (MainThreadData *)data;
    if (!mt_data || !mt_data->tkwin) return NULL;

    Window win = Tk_WindowId(mt_data->tkwin);
    return (win != None) ? (gpointer)1 : NULL;
}

/* Check if a window is toplevel.  */
static gpointer is_toplevel_main(gpointer data)
{
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)(uintptr_t)Tk_IsTopLevel(mt_data->tkwin);
}

/* Get the Tk window.  */
static gpointer get_window_handle_main(gpointer data)
{
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)mt_data->tkwin;
}

/* Find a hash entry.  */
static gpointer find_hash_entry_main(gpointer data)
{
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)Tcl_FindHashEntry(TkAccessibilityObject, (char *)mt_data->tkwin);
}

/* Get a hash value.  */
static gpointer get_hash_value_main(gpointer data)
{
    MainThreadData *mt_data = (MainThreadData *)data;
    Tcl_HashEntry *hPtr = (Tcl_HashEntry *)mt_data->result;
    return hPtr ? (gpointer)Tcl_GetHashValue(hPtr) : NULL;
}

/* Find hash entry by key. */
static gpointer find_hash_entry_by_key_main(gpointer data)
{
    MainThreadData *mt_data = (MainThreadData *)data;
    Tcl_HashTable *table = (Tcl_HashTable *)mt_data->result;
    return (gpointer)Tcl_FindHashEntry(table, "role");
}

/* Obtain the string value for a hash entry. */ 
static gpointer get_hash_string_value_main(gpointer data)
{
    MainThreadData *mt_data = (MainThreadData *)data;
    Tcl_HashEntry *hPtr = (Tcl_HashEntry *)mt_data->result;
    return hPtr ? (gpointer)Tcl_GetString(Tcl_GetHashValue(hPtr)) : NULL;
}

/* Obtain a window name.  */
static gpointer name_to_window_main(gpointer data)
{
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)Tk_NameToWindow(mt_data->interp, 
                                     Tcl_GetString(mt_data->result), 
                                     Tk_MainWindow(mt_data->interp));
}

/* Get the main Tk window.  */
static gpointer get_main_window_main(gpointer data)
{
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)Tk_MainWindow(mt_data->interp);
}

/*
 *----------------------------------------------------------------------
 *
 * Signal emission helpers. These are called to notify Atk when a Tk widget
 * changes state, focus, or one of its attributes like its name or value. 
 *
 *----------------------------------------------------------------------
 */

/* Emit children-changed::add signal. */
static gboolean emit_children_changed_add(gpointer data)
{
    if (data) {
        AtkObject *child = (AtkObject *)data;
        g_signal_emit_by_name(child, "children-changed::add", -1, child);
    }
    return G_SOURCE_REMOVE;
}

/* Emit children-changed::remove signal. */
static gboolean emit_children_changed_remove(gpointer data)
{
    if (data) {
        AtkObject *child = (AtkObject *)data;
        AtkObject *parent = atk_object_get_parent(child);
        if (parent) {
            g_signal_emit_by_name(parent, "children-changed::remove", -1, child);
        }
    }
    return G_SOURCE_REMOVE;
}

/* Emit value-changed signal. */
static gboolean emit_value_changed(gpointer data)
{
    if (data) {
        ValueChangedData *vcd = (ValueChangedData *)data;
        g_signal_emit_by_name(vcd->obj, "value-changed", &vcd->value);
        g_value_unset(&vcd->value);
        g_free(vcd);
    }
    return G_SOURCE_REMOVE;
}


/* Emit text-selection-changed signal. */
static gboolean emit_text_selection_changed(gpointer data)
{
    if (data) {
        g_signal_emit_by_name(data, "text-selection-changed");
    }
    return G_SOURCE_REMOVE;
}

/* Emit focus-event signal. */
static gboolean emit_focus_event(gpointer data)
{
    if (data) {
        FocusEventData *fed = (FocusEventData *)data;
        g_signal_emit_by_name(fed->obj, "focus-event", fed->state);
        g_free(fed);
    }
    return G_SOURCE_REMOVE;
}

/* Emit state-change signal. */
static gboolean emit_state_change(gpointer data)
{
    if (data) {
        StateChangeData *scd = (StateChangeData *)data;
        g_signal_emit_by_name(scd->obj, "state-change", scd->name, scd->state);
        g_free(scd->name);
        g_free(scd);
    }
    return G_SOURCE_REMOVE;
}

/* Emit bounds-changed signal. */
static gboolean emit_bounds_changed(gpointer data)
{
    if (data) {
        BoundsChangedData *bcd = (BoundsChangedData *)data;
        g_signal_emit_by_name(bcd->obj, "bounds-changed", &bcd->rect);
        g_free(bcd);
    }
    return G_SOURCE_REMOVE;
}

/*
 *----------------------------------------------------------------------
 *
 * Function to map Tk data to ATK's API. These functions do the heavy 
 * lifting of implementing the Tk-ATK interface. 
 *
 *----------------------------------------------------------------------
 */


/*
 * Map Atk component interface to Tk.
 */
 
static void tk_get_extents(AtkComponent *component, gint *x, gint *y, gint *width, gint *height, AtkCoordType coord_type)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)component;

    if (!acc) {
	*x = *y = *width = *height = 0;
	return;
    }

    *x = acc->x;
    *y = acc->y;
    *width = acc->width;
    *height = acc->height;

    /*Handle coordinate type conversion.  */
    if (coord_type == ATK_XY_SCREEN) {
	MainThreadData data = {acc->tkwin, acc->interp, NULL};
	gint *root_coords = RunOnMainThread(get_window_geometry_main, &data);
	if (root_coords) {
	    *x = root_coords[0];
	    *y = root_coords[1];
	    g_free(root_coords);
	}
    }
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
    iface->contains = tk_contains;
}

/*
 * Functions to manage child count and individual child widgets.
 */
 
static gint tk_get_n_children(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;

    if (!acc) return 0;

    if (obj == tk_root_accessible) {
	/*The root's children are the toplevel windows.  */
	return g_list_length(toplevel_accessible_objects);
    }

    if (!acc->tkwin) {
	return 0;
    }

    /* Count direct child windows with accessible objects.  */
    int count = 0;
    MainThreadData data = {acc->tkwin, acc->interp, NULL};
    TkWindow *winPtr = (TkWindow *)RunOnMainThread(get_window_handle_main, &data);
    TkWindow *childPtr;
    
    /* Iterate through Tk's internal child list.  */
    for (childPtr = winPtr->childList; childPtr != NULL; childPtr = childPtr->nextPtr) {
	MainThreadData child_data = {(Tk_Window)childPtr, acc->interp, NULL};
	gboolean has_window_id = (gboolean)(uintptr_t)RunOnMainThread(has_window_id_main, &child_data);
	if (has_window_id && GetAtkObjectForTkWindow((Tk_Window)childPtr)) {
	    count++;
	} else {
	    AtkObject *child_obj = TkCreateAccessibleAtkObject(acc->interp, (Tk_Window)childPtr, 
							       (const char *)RunOnMainThread(get_window_name_main, &child_data));
	    RegisterAtkObjectForTkWindow((Tk_Window)childPtr, child_obj);
	    count++;
	}
    }
    return count;
}

static AtkObject *tk_ref_child(AtkObject *obj, gint i)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;

    if (!acc) return NULL;

    if (obj == tk_root_accessible) {
	if (i >= (gint)g_list_length(toplevel_accessible_objects)) {
	    return NULL;
	}
	/*Get accessible object from toplevel list.  */
	AtkObject *child = g_list_nth_data(toplevel_accessible_objects, i);
	if (child) {
	    g_object_ref(child);
	}
	return child;
    }

    if (!acc->tkwin) {
	return NULL;
    }
    
    /*Return i-th direct child with accessible object.  */
    guint index = 0;
    MainThreadData data = {acc->tkwin, acc->interp, NULL};
    TkWindow *winPtr = (TkWindow *)RunOnMainThread(get_window_handle_main, &data);
    TkWindow *childPtr;
    
    for (childPtr = winPtr->childList; childPtr != NULL; childPtr = childPtr->nextPtr) {
	AtkObject *child_obj = GetAtkObjectForTkWindow((Tk_Window)childPtr);
	if (child_obj) {
	    if (i >= 0 && (guint)i == index) {
		g_object_ref(child_obj);
		return child_obj;
	    }
	    index++;
	}
    }
    
    if ((gboolean)(uintptr_t)RunOnMainThread(is_toplevel_main, &data)) {
	toplevel_accessible_objects = g_list_append(toplevel_accessible_objects, obj);
    }
    return NULL;
}

/*
 * Functions to map accessible role to Atk.
 */

static AtkRole GetAtkRoleForWidget(Tk_Window win)
{
    if (!win) return ATK_ROLE_UNKNOWN;

    MainThreadData data = {win, NULL, NULL};
    Tcl_HashEntry *hPtr = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_main, &data);
   
    /* Check if we have accessibility attributes. */ 
    if (hPtr) {
	Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)RunOnMainThread(get_hash_value_main, &data);
	if (AccessibleAttributes) {
	    Tcl_HashEntry *hPtr2 = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_by_key_main, &data);
	    if (hPtr2) {
		char *result = (char *)RunOnMainThread(get_hash_string_value_main, &data);
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

    /* Special case for toplevel windows. */
    if ((gboolean)(uintptr_t)RunOnMainThread(is_toplevel_main, &data)) {
	return ATK_ROLE_WINDOW;
    }
    
    return ATK_ROLE_UNKNOWN;
}

static AtkRole tk_get_role(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    return acc->cached_role;
}

/*
 * Name and description getters
 * for Tk-ATK objects.
 */

static const gchar *tk_get_name(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    return acc->cached_name;
}

static void tk_set_name(AtkObject *obj, const gchar *name)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;

    if (!acc) return; 

    if (obj == tk_root_accessible) {
	/* Free old cached name.  */
	g_free(acc->cached_name);
	acc->cached_name = sanitize_utf8(name); 
    }
    atk_object_set_name(obj, name);
}

static const gchar *tk_get_description(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    return acc->cached_description;
}


/*
 * Functions to map accessible value to ATK using
 * AtkValue interface.
 */
 
static void tk_get_current_value(AtkValue *obj, GValue *value)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)ATK_OBJECT(obj);
    g_value_init(value, G_TYPE_STRING);
    g_value_set_string(value, acc->cached_value);
}

static void tk_atk_value_interface_init(AtkValueIface *iface)
{
    iface->get_current_value = tk_get_current_value;
}

/* Function to map accessible state to ATK. */
static AtkStateSet *tk_ref_state_set(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    AtkStateSet *set = atk_state_set_new();
    if (!acc) return set;

    atk_state_set_add_state(set, ATK_STATE_ENABLED);
    atk_state_set_add_state(set, ATK_STATE_SENSITIVE);
    
    if (acc->cached_role == ATK_ROLE_ENTRY) {
	atk_state_set_add_state(set, ATK_STATE_EDITABLE);
	atk_state_set_add_state(set, ATK_STATE_SINGLE_LINE);
    }
    
    if (acc->is_mapped || acc->width > 0 || acc->height > 0) {
	atk_state_set_add_state(set, ATK_STATE_VISIBLE);
	if (acc->is_mapped) {
	    atk_state_set_add_state(set, ATK_STATE_SHOWING);
	}
	atk_state_set_add_state(set, ATK_STATE_FOCUSABLE);
	if (acc->has_focus) {
	    atk_state_set_add_state(set, ATK_STATE_FOCUSED);
	}
    }
    return set;
}

/*
 * Functions that implement actions (i.e. button press)
 * from Tk to Atk.
 */

static gboolean tk_action_do_action(AtkAction *action, gint i)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)action;

    if (!acc || !acc->tkwin || !acc->interp) {
	return FALSE;
    }

    if (i == 0) {
	/*Retrieve the command string.  */
	MainThreadData data = {acc->tkwin, acc->interp, NULL};
	Tcl_HashEntry *hPtr = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_main, &data);
	if (!hPtr) {
	    return FALSE;
	}
        
	Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)RunOnMainThread(get_hash_value_main, &data);
	if (!AccessibleAttributes) {
	    return FALSE;
	}
        
	Tcl_HashEntry *hPtr2 = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_by_key_main, &data);
	if (!hPtr2) {
	    return FALSE;
	}
        
	const char *cmd = (const char *)RunOnMainThread(get_hash_string_value_main, &data);
	if (!cmd) {
	    return FALSE;
	}

	/*Finally, execute command.  */
	if (Tcl_EvalEx(acc->interp, cmd, -1, TCL_EVAL_GLOBAL) != TCL_OK) {
	    return FALSE;
	}
	return TRUE;
    }
    return FALSE; 
}

static gint tk_action_get_n_actions(AtkAction *action)
{
    (void) action;
    return 1;
}

static const gchar *tk_action_get_name(AtkAction *action, gint i)
{
    (void) action;
    if (i == 0) {
	return "click";
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
 * Functions to initialize and manage the parent Atk class and object instances.
 */

static void tk_atk_accessible_init(TkAtkAccessible *self)
{
    self->tkwin = NULL;
    self->interp = NULL;
    self->path = NULL;
    self->cached_name = NULL;
    self->cached_description = NULL;
    self->cached_value = NULL;
    self->cached_role = ATK_ROLE_UNKNOWN;
    self->x = 0;
    self->y = 0;
    self->width = 0;
    self->height = 0;
    self->is_mapped = FALSE;
    self->has_focus = FALSE;
}

static void tk_atk_accessible_finalize(GObject *gobject)
{
    TkAtkAccessible *self = (TkAtkAccessible*)gobject;

    if (self->tkwin) {
        if (RunOnMainThread(is_toplevel_main, &(MainThreadData){self->tkwin, NULL, NULL})) {
            toplevel_accessible_objects = g_list_remove(toplevel_accessible_objects, self);
            RunOnMainThreadAsync(emit_children_changed_remove, ATK_OBJECT(self));
        }
        UnregisterAtkObjectForTkWindow(self->tkwin);
    }

    g_free(self->path);
    g_free(self->cached_name);
    g_free(self->cached_description);
    g_free(self->cached_value);

    G_OBJECT_CLASS(tk_atk_accessible_parent_class)->finalize(gobject);
}

static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    AtkObjectClass *atk_class = ATK_OBJECT_CLASS(klass);

    gobject_class->finalize = tk_atk_accessible_finalize;

    /*Map ATK class functions to Tk functions.  */
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
    (void) interp;
    if (!accessible || !tkwin) {
	g_warning("RegisterToplevelWindow: Invalid tkwin or accessible");
	return;
    }
    if (!tk_root_accessible) {
	tk_root_accessible = tk_util_get_root();
    }
    AtkObject *existing = GetAtkObjectForTkWindow(tkwin);
    if (existing && existing != accessible) {
	g_warning("RegisterToplevelWindow: Toplevel %s already registered with different AtkObject",
		  (const char *)RunOnMainThread(get_window_name_main, &(MainThreadData){tkwin, NULL, NULL}));
	return;
    }
    atk_object_set_parent(accessible, tk_root_accessible);
    if (!g_list_find(toplevel_accessible_objects, accessible)) {
        toplevel_accessible_objects = g_list_append(toplevel_accessible_objects, accessible);
        RunOnMainThreadAsync(emit_children_changed_add, accessible);
    }
    const gchar *name = tk_get_name(accessible);
    if (name) {
	tk_set_name(accessible, name);
    }
}

/* Register child widgets as accessible objects. */
static void RegisterChildWidgets(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *parent_obj)
{
    if (!tkwin || !parent_obj) {
	g_warning("RegisterChildWidgets: Invalid tkwin or parent_obj");
	return;
    }
    
    MainThreadData data = {tkwin, interp, NULL};
    TkWindow *winPtr = (TkWindow *)RunOnMainThread(get_window_handle_main, &data);
    TkWindow *childPtr;
    int index = 0;
    
    for (childPtr = winPtr->childList; childPtr != NULL; childPtr = childPtr->nextPtr) {
	Tk_Window child = (Tk_Window)childPtr;
	MainThreadData child_data = {child, interp, NULL};
	gboolean is_mapped = (gboolean)(uintptr_t)RunOnMainThread(is_window_mapped_main, &child_data);
	if (!child || !is_mapped) {
	    continue;
	}
        
	AtkObject *child_obj = GetAtkObjectForTkWindow(child);
	if (!child_obj) {
	    child_obj = TkCreateAccessibleAtkObject(interp, child, 
						    (const char *)RunOnMainThread(get_window_name_main, &child_data));
	    if (!child_obj) {
		g_warning("RegisterChildWidgets: Failed to create accessible object for %s",
			  (const char *)RunOnMainThread(get_window_name_main, &child_data));
		continue;
	    }
	    RegisterAtkObjectForTkWindow(child, child_obj);
	    TkAtkAccessible_RegisterEventHandlers(child, (TkAtkAccessible *)child_obj);
            
	    MainThreadData role_data = {child, interp, NULL};
	    AtkRole role = (AtkRole)(uintptr_t)RunOnMainThread(get_atk_role_for_widget_main, &role_data);
	    if (role == ATK_ROLE_UNKNOWN) {
		atk_object_set_role(child_obj, ATK_ROLE_PANEL);
	    }
            
	    AtkStateSet *state_set = atk_state_set_new();
	    if (role == ATK_ROLE_PUSH_BUTTON || role == ATK_ROLE_ENTRY ||
		role == ATK_ROLE_COMBO_BOX || role == ATK_ROLE_CHECK_BOX ||
		role == ATK_ROLE_RADIO_BUTTON || role == ATK_ROLE_SLIDER ||
		role == ATK_ROLE_SPIN_BUTTON) {
		atk_state_set_add_state(state_set, ATK_STATE_FOCUSABLE);
	    }
	    g_object_unref(state_set);
	    atk_object_set_parent(child_obj, parent_obj);
	    RunOnMainThreadAsync(emit_children_changed_add, child_obj);
	    RegisterChildWidgets(interp, child, child_obj);
	}
	index++;
    }
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
	/*Set proper name and role.  */
	atk_object_set_role(tk_root_accessible, ATK_ROLE_APPLICATION);
	tk_set_name(tk_root_accessible, "Tk Application");
    }

    return tk_root_accessible;
}

/* Core function linking Tk objects to the ATK root object and at-spi. */
AtkObject *atk_get_root(void) {
    return tk_util_get_root();
}

/* ATK-Tk object creation with proper parent relationship. */
AtkObject *TkCreateAccessibleAtkObject(Tcl_Interp *interp, Tk_Window tkwin, const char *path)
{
    if (!interp || !tkwin || !path) {
	g_warning("TkCreateAccessibleAtkObject: Invalid parameters");
	return NULL;
    }
    
    TkAtkAccessible *acc = g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
    acc->interp = interp;
    acc->tkwin = tkwin;
    acc->path = sanitize_utf8(path);
    
    MainThreadData data = {tkwin, interp, NULL};
    if ((gboolean)(uintptr_t)RunOnMainThread(is_toplevel_main, &data) && 
	tkwin != (Tk_Window)RunOnMainThread(get_main_window_main, &(MainThreadData){NULL, interp, NULL})) {
	RunOnMainThread(make_window_exist_main, &data);
	RunOnMainThread(map_window_main, &data);
    }
    
    if ((gboolean)(uintptr_t)RunOnMainThread(is_window_mapped_main, &data)) {
	UpdateGeometryCache(acc);
	UpdateStateCache(acc);
    } else {
	acc->x = acc->y = acc->width = acc->height = 0;
	acc->is_mapped = FALSE;
    }
    
    UpdateNameCache(acc);
    UpdateDescriptionCache(acc);
    UpdateValueCache(acc);
    UpdateRoleCache(acc);
    
    AtkObject *obj = ATK_OBJECT(acc);
    AtkRole role = acc->cached_role;
    if (role == ATK_ROLE_UNKNOWN && 
	((gboolean)(uintptr_t)RunOnMainThread(is_toplevel_main, &data) || 
	 tkwin == (Tk_Window)RunOnMainThread(get_main_window_main, &(MainThreadData){NULL, interp, NULL}))) {
	role = ATK_ROLE_WINDOW;
    }
    atk_object_set_role(obj, role);
    if (acc->cached_name) {
	atk_object_set_name(ATK_OBJECT(acc), acc->cached_name);
    }
    
    Tk_Window parent_tkwin = (Tk_Window)RunOnMainThread(get_window_parent_main, &data);
    AtkObject *parent_obj = parent_tkwin ? GetAtkObjectForTkWindow(parent_tkwin) : tk_root_accessible;
    if (parent_obj) {
	atk_object_set_parent(obj, parent_obj);
    }
    
    if ((gboolean)(uintptr_t)RunOnMainThread(is_toplevel_main, &data)) {
	RegisterToplevelWindow(interp, tkwin, obj);
	RegisterChildWidgets(interp, tkwin, obj);
    }

    return obj;
}

/*
 * Functions to map Tk window to its corresponding Atk object.
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
    if (tk_to_atk_map && tkwin) { 
	g_hash_table_remove(tk_to_atk_map, tkwin); /* g_object_unref will be called by hash table. */
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Cache update functions so that Tcl/Tk calls are not made from within Atk 
 * functions controlled by the GLib event loop.
 *
 *----------------------------------------------------------------------
 */
 
static void UpdateGeometryCache(TkAtkAccessible *acc)
{
    if (!acc || !acc->tkwin) {
	g_warning("UpdateGeometryCache: Invalid acc or tkwin");
	acc->x = acc->y = acc->width = acc->height = 0;
	return;
    }
    
    MainThreadData data = {acc->tkwin, acc->interp, NULL};
    gboolean is_mapped = (gboolean)(uintptr_t)RunOnMainThread(is_window_mapped_main, &data);
    if (!is_mapped) {
	return;
    }
    
    gint *geometry = RunOnMainThread(get_window_geometry_main, &data);
    if (geometry) {
	if (geometry[2] <= 0 || geometry[3] <= 0) {
	    g_warning("UpdateGeometryCache: Invalid geometry for %s (width: %d, height: %d)",
		      (const char *)RunOnMainThread(get_window_name_main, &data), 
		      geometry[2], geometry[3]);
	    acc->x = acc->y = acc->width = acc->height = 0;
	} else {
	    acc->x = geometry[0];
	    acc->y = geometry[1];
	    acc->width = geometry[2];
	    acc->height = geometry[3];
	}
	g_free(geometry);
    }
}

static void UpdateNameCache(TkAtkAccessible *acc)
{
    if (!acc || !acc->tkwin || !acc->interp) return;

    g_free(acc->cached_name);
    acc->cached_name = NULL;

    MainThreadData data = {acc->tkwin, acc->interp, NULL};
    Tcl_HashEntry *hPtr = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_main, &data);
    if (hPtr) {
	Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)RunOnMainThread(get_hash_value_main, &data);
	if (AccessibleAttributes) {
	    Tcl_HashEntry *hPtr2 = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_by_key_main, &data);
	    if (hPtr2) {
		const char *result = (const char *)RunOnMainThread(get_hash_string_value_main, &data);
		if (result) {
		    acc->cached_name = sanitize_utf8(result);
		}
	    }
	}
    }

    if (!acc->cached_name) {
	acc->cached_name = sanitize_utf8((const char *)RunOnMainThread(get_window_name_main, &data));
    }
}

static void UpdateDescriptionCache(TkAtkAccessible *acc)
{
    if (!acc || !acc->tkwin) return;

    g_free(acc->cached_description);
    acc->cached_description = NULL;

    MainThreadData data = {acc->tkwin, acc->interp, NULL};
    Tcl_HashEntry *hPtr = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_main, &data);
    if (hPtr) {
	Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)RunOnMainThread(get_hash_value_main, &data);
	if (AccessibleAttributes) {
	    Tcl_HashEntry *hPtr2 = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_by_key_main, &data);
	    if (hPtr2) {
		const char *result = (const char *)RunOnMainThread(get_hash_string_value_main, &data);
		if (result) {
		    acc->cached_description = sanitize_utf8(result);
		}
	    }
	}
    }
}

static void UpdateValueCache(TkAtkAccessible *acc)
{
    if (!acc || !acc->tkwin) return;

    g_free(acc->cached_value);
    acc->cached_value = NULL;

    MainThreadData data = {acc->tkwin, acc->interp, NULL};
    Tcl_HashEntry *hPtr = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_main, &data);
    if (hPtr) {
	Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)RunOnMainThread(get_hash_value_main, &data);
	if (AccessibleAttributes) {
	    Tcl_HashEntry *hPtr2 = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_by_key_main, &data);
	    if (hPtr2) {
		const char *result = (const char *)RunOnMainThread(get_hash_string_value_main, &data);
		if (result) {
		    acc->cached_value = sanitize_utf8(result);
		}
	    }
	}
    }

    if (!acc->cached_value) {
	acc->cached_value = sanitize_utf8("");
    }
}

static void UpdateRoleCache(TkAtkAccessible *acc)
{
    if (!acc || !acc->tkwin) return;
    MainThreadData data = {acc->tkwin, acc->interp, NULL};
    acc->cached_role = (AtkRole)(uintptr_t)RunOnMainThread(get_atk_role_for_widget_main, &data);
}

static void UpdateStateCache(TkAtkAccessible *acc) {
    if (!acc || !acc->tkwin) return;
    
    MainThreadData data = {acc->tkwin, acc->interp, NULL};
    acc->is_mapped = (gboolean)(uintptr_t)RunOnMainThread(is_window_mapped_main, &data);
    
    TkWindow *focuswin = (TkWindow *)RunOnMainThread(get_focus_window_main, &data);
    Tk_Window focus_win = (Tk_Window)focuswin;
    acc->has_focus = (focus_win == acc->tkwin);
    
    if (!acc->has_focus && focus_win) {
	Tk_Window parent = (Tk_Window)RunOnMainThread(get_window_parent_main, &(MainThreadData){focus_win, NULL, NULL});
	while (parent) {
	    if (parent == acc->tkwin) {
		acc->has_focus = TRUE;
		break;
	    }
	    parent = (Tk_Window)RunOnMainThread(get_window_parent_main, &(MainThreadData){parent, NULL, NULL});
	}
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
void TkAtkAccessible_RegisterEventHandlers(Tk_Window tkwin, void *tkAccessible) {
    if (!tkwin || !tkAccessible) return;
    Tk_CreateEventHandler(tkwin, StructureNotifyMask,
			  TkAtkAccessible_DestroyHandler, tkAccessible);
    Tk_CreateEventHandler(tkwin, StructureNotifyMask,
			  TkAtkAccessible_ConfigureHandler, tkAccessible);
    Tk_CreateEventHandler(tkwin, MapNotify,
			  TkAtkAccessible_MapHandler, tkAccessible);
    Tk_CreateEventHandler(tkwin, UnmapNotify,
			  TkAtkAccessible_UnmapHandler, tkAccessible);
    Tk_CreateEventHandler(tkwin, FocusChangeMask,
			  TkAtkAccessible_FocusHandler, tkAccessible);
}

/* Respond to <Destroy> event. */
static void TkAtkAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr)
{
    if (eventPtr->type == DestroyNotify) {
        TkAtkAccessible *tkAccessible = (TkAtkAccessible *)clientData;
        if (tkAccessible && tkAccessible->tkwin) {
            /* Notify parent about removal. */
            RunOnMainThreadAsync(emit_children_changed_remove, ATK_OBJECT(tkAccessible));
            
            /* Unregister and cleanup. */
            g_object_unref(tkAccessible);
        }
    }
}

/* Respond to <Confgure> event. */
static void TkAtkAccessible_ConfigureHandler(ClientData clientData, XEvent *eventPtr)
{
    if (eventPtr->type != ConfigureNotify) {
	return;
    }
    
    TkAtkAccessible *acc = (TkAtkAccessible *)clientData;
    if (!acc || !acc->tkwin) {
	g_warning("TkAtkAccessible_ConfigureHandler: Invalid or null acc/tkwin");
	return;
    }
    
    MainThreadData data = {acc->tkwin, acc->interp, NULL};
    gboolean is_mapped = (gboolean)(uintptr_t)RunOnMainThread(is_window_mapped_main, &data);
    if (!is_mapped) {
	g_warning("TkAtkAccessible_ConfigureHandler: Widget %s is not mapped", 
		  (const char *)RunOnMainThread(get_window_name_main, &data));
	return;
    }
    
    /* Update all caches with additional validation. */
    UpdateGeometryCache(acc);
    UpdateNameCache(acc);
    UpdateDescriptionCache(acc);
    UpdateValueCache(acc);
    UpdateRoleCache(acc);
    UpdateStateCache(acc);
    
    /*  Notify ATK of changes only if geometry is valid and non-zero. */
    if (acc->width > 0 && acc->height > 0 && is_mapped) {
        BoundsChangedData *bcd = g_new0(BoundsChangedData, 1);
        bcd->obj = ATK_OBJECT(acc);
        bcd->rect.x = acc->x;
        bcd->rect.y = acc->y;
        bcd->rect.width = acc->width;
        bcd->rect.height = acc->height;
        RunOnMainThreadAsync(emit_bounds_changed, bcd);
    }
    
    if (acc->cached_name) {
	tk_set_name(ATK_OBJECT(acc), acc->cached_name);
    }
    if (acc->cached_description) {
	atk_object_set_description(ATK_OBJECT(acc), acc->cached_description);
    }
    
    StateChangeData *visible_scd = g_new0(StateChangeData, 1);
    visible_scd->obj = ATK_OBJECT(acc);
    visible_scd->name = g_strdup("visible");
    visible_scd->state = acc->is_mapped;
    RunOnMainThreadAsync(emit_state_change, visible_scd);
    
    StateChangeData *showing_scd = g_new0(StateChangeData, 1);
    showing_scd->obj = ATK_OBJECT(acc);
    showing_scd->name = g_strdup("showing");
    showing_scd->state = acc->is_mapped;
    RunOnMainThreadAsync(emit_state_change, showing_scd);
     
    /* Update GLib event loop. */
    int glib_iterations = 0;
    while (g_main_context_iteration(g_main_context_default(), FALSE) && glib_iterations < 20) {
	glib_iterations++;
    }
}

/* Respond to <Map> event. */
static void TkAtkAccessible_MapHandler(ClientData clientData, XEvent *eventPtr) 
{
    if (eventPtr->type != MapNotify) return;
    TkAtkAccessible *acc = (TkAtkAccessible *)clientData;
    
    MainThreadData data = {acc->tkwin, acc->interp, NULL};
    gboolean is_mapped = (gboolean)(uintptr_t)RunOnMainThread(is_window_mapped_main, &data);
    if (!acc || !acc->tkwin || !is_mapped) {
	g_warning("TkAtkAccessible_MapHandler: Invalid or unmapped tkwin");
	return;
    }
    
    UpdateStateCache(acc);
    AtkObject *atk_obj = (AtkObject*)acc;
    
    /* Recursively register child widgets. */
    MainThreadData win_data = {acc->tkwin, acc->interp, NULL};
    TkWindow *winPtr = (TkWindow *)RunOnMainThread(get_window_handle_main, &win_data);
    for (TkWindow *childPtr = winPtr->childList; childPtr; childPtr = childPtr->nextPtr) {
	Tk_Window child = (Tk_Window)childPtr;
	MainThreadData child_data = {child, acc->interp, NULL};
	gboolean child_mapped = (gboolean)(uintptr_t)RunOnMainThread(is_window_mapped_main, &child_data);
	if (!child || !child_mapped) {
	    continue;
	}
	if (!GetAtkObjectForTkWindow(child)) {
	    AtkObject *child_acc = TkCreateAccessibleAtkObject(acc->interp, child, 
							       (const char *)RunOnMainThread(get_window_name_main, &child_data));
	    if (child_acc) {
		RegisterAtkObjectForTkWindow(child, child_acc);
		atk_object_set_parent(child_acc, atk_obj);
		TkAtkAccessible_RegisterEventHandlers(child, (TkAtkAccessible *)child_acc);
	    } else {
		g_warning("TkAtkAccessible_MapHandler: Failed to create accessible object for %s",
			  (const char *)RunOnMainThread(get_window_name_main, &child_data));
	    }
	}
    }
    
    StateChangeData *visible_scd = g_new0(StateChangeData, 1);
    visible_scd->obj = atk_obj;
    visible_scd->name = g_strdup("visible");
    visible_scd->state = TRUE;
    RunOnMainThreadAsync(emit_state_change, visible_scd);
    
    StateChangeData *showing_scd = g_new0(StateChangeData, 1);
    showing_scd->obj = atk_obj;
    showing_scd->name = g_strdup("showing");
    showing_scd->state = TRUE;
    RunOnMainThreadAsync(emit_state_change, showing_scd);
}

/* Respond to <Unmap> event. */
static void TkAtkAccessible_UnmapHandler(ClientData clientData, XEvent *eventPtr)
{
    if (eventPtr->type != UnmapNotify) return;
    TkAtkAccessible *acc = (TkAtkAccessible *)clientData;
    if (!acc || !acc->tkwin) return;
    UpdateStateCache(acc);
    AtkObject *atk_obj = (AtkObject*)acc;
    AtkStateSet *state_set = atk_state_set_new();
    atk_state_set_remove_state(state_set, ATK_STATE_SHOWING);
    StateChangeData *scd = g_new0(StateChangeData, 1);
    scd->obj = atk_obj;
    scd->name = g_strdup("showing");
    scd->state = FALSE;
    RunOnMainThreadAsync(emit_state_change, scd);
}

/* Respond to <FocusIn/Out> events. */
static void TkAtkAccessible_FocusHandler(ClientData clientData, XEvent *eventPtr)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)clientData;
    if (!acc || !acc->tkwin || !(gboolean)(uintptr_t)RunOnMainThread(is_window_mapped_main, &(MainThreadData){acc->tkwin, acc->interp, NULL})) {
	g_warning("No accessible object!\n");
	return;
    }
    UpdateStateCache(acc);
    AtkObject *atk_obj = (AtkObject*)acc;
    AtkStateSet *state_set = atk_state_set_new();
    AtkRole role = acc->cached_role;
    if (role == ATK_ROLE_PUSH_BUTTON || role == ATK_ROLE_ENTRY ||
	role == ATK_ROLE_COMBO_BOX || role == ATK_ROLE_CHECK_BOX ||
	role == ATK_ROLE_RADIO_BUTTON || role == ATK_ROLE_SLIDER ||
	role == ATK_ROLE_SPIN_BUTTON || role == ATK_ROLE_WINDOW) {
	atk_state_set_add_state(state_set, ATK_STATE_FOCUSABLE);
    }
    if (eventPtr->type == FocusIn) {
        FocusEventData *fed = g_new0(FocusEventData, 1);
        fed->obj = atk_obj;
        fed->state = TRUE;
        RunOnMainThreadAsync(emit_focus_event, fed);
        
        StateChangeData *scd = g_new0(StateChangeData, 1);
        scd->obj = atk_obj;
        scd->name = g_strdup("focused");
        scd->state = TRUE;
        RunOnMainThreadAsync(emit_state_change, scd);
    } else if (eventPtr->type == FocusOut) {
        FocusEventData *fed = g_new0(FocusEventData, 1);
        fed->obj = atk_obj;
        fed->state = FALSE;
        RunOnMainThreadAsync(emit_focus_event, fed);
        
        StateChangeData *scd = g_new0(StateChangeData, 1);
        scd->obj = atk_obj;
        scd->name = g_strdup("focused");
        scd->state = FALSE;
        RunOnMainThreadAsync(emit_state_change, scd);
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
 * Accessibility system is made aware when a selection is changed.
 *
 * Side effects:
 *
 *  None.
 *
 *----------------------------------------------------------------------
 */

static int EmitSelectionChanged(ClientData clientData, Tcl_Interp *ip, int objc,Tcl_Obj *const objv[])
{
    (void) clientData;

    if (objc < 2) {
	Tcl_WrongNumArgs(ip, 1, objv, "window?");
	return TCL_ERROR;
    }
    
    MainThreadData data = {NULL, ip, NULL};
    Tk_Window path_tkwin = (Tk_Window)RunOnMainThread(name_to_window_main, &data);
    if (path_tkwin == NULL) {
	Tcl_SetResult(ip, "Invalid window path", TCL_STATIC);
	return TCL_ERROR; 
    }

    AtkObject *acc = GetAtkObjectForTkWindow(path_tkwin);

    if (!acc) {
	Tcl_SetResult(ip, "No accessible object for window", TCL_STATIC);
	return TCL_ERROR;
    }

    TkAtkAccessible *tk_acc = (TkAtkAccessible *)acc;
    UpdateValueCache(tk_acc);

    AtkRole role = tk_acc->cached_role;

    GValue gval = G_VALUE_INIT;
    tk_get_current_value(ATK_VALUE(acc), &gval);
    
    ValueChangedData *vcd = g_new0(ValueChangedData, 1);
    vcd->obj = acc;
    g_value_init(&vcd->value, G_TYPE_STRING);
    g_value_set_string(&vcd->value, g_value_get_string(&gval));
    g_value_unset(&gval);
    
    RunOnMainThreadAsync(emit_value_changed, vcd);

    if (role == ATK_ROLE_TEXT || role == ATK_ROLE_ENTRY) {
        RunOnMainThreadAsync(emit_text_selection_changed, acc);
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
 
static int EmitFocusChanged(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    (void) clientData;
    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "window");
	return TCL_ERROR;
    }
    
    MainThreadData data = {NULL, interp, NULL};
    Tk_Window path_tkwin = (Tk_Window)RunOnMainThread(name_to_window_main, &data);
    if (path_tkwin == NULL) {
	Tcl_SetResult(interp, "Invalid window path", TCL_STATIC);
	return TCL_ERROR;
    }
    
    AtkObject *acc = GetAtkObjectForTkWindow(path_tkwin);
    if (!acc) {
	acc = TkCreateAccessibleAtkObject(interp, path_tkwin, Tcl_GetString(objv[1]));
	if (!acc) {
	    Tcl_SetResult(interp, "Failed to create accessible object", TCL_STATIC);
	    return TCL_ERROR;
	}
	RegisterAtkObjectForTkWindow(path_tkwin, acc);
        
	MainThreadData win_data = {path_tkwin, interp, NULL};
	if ((gboolean)(uintptr_t)RunOnMainThread(is_toplevel_main, &win_data)) {
	    RegisterToplevelWindow(interp, path_tkwin, acc);
	} else {
	    Tk_Window parent_tkwin = (Tk_Window)RunOnMainThread(get_window_parent_main, &win_data);
	    AtkObject *parent_obj = parent_tkwin ? GetAtkObjectForTkWindow(parent_tkwin) : tk_root_accessible;
	    if (parent_obj) {
		atk_object_set_parent(acc, parent_obj);
		RunOnMainThreadAsync(emit_children_changed_add, acc);
	    }
	}
    }
    
    TkAtkAccessible *tk_acc = (TkAtkAccessible *)acc;
    UpdateStateCache(tk_acc);
    AtkStateSet *state_set = atk_state_set_new();
    atk_state_set_add_state(state_set, ATK_STATE_FOCUSABLE);
    atk_state_set_add_state(state_set, ATK_STATE_FOCUSED);
    
    FocusEventData *fed = g_new0(FocusEventData, 1);
    fed->obj = acc;
    fed->state = TRUE;
    RunOnMainThreadAsync(emit_focus_event, fed);
    
    StateChangeData *scd = g_new0(StateChangeData, 1);
    scd->obj = acc;
    scd->name = g_strdup("focused");
    scd->state = TRUE;
    RunOnMainThreadAsync(emit_state_change, scd);
    g_object_unref(state_set);
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

static int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) 
{
    (void)clientData;
    (void)objc;
    (void)objv;

    int result = 0;
    FILE *fp = popen("pgrep -x orca", "r");
    if (fp == NULL) {
	result = 0;
    }

    char buffer[16];
    int running = (fgets(buffer, sizeof(buffer), fp) != NULL); 
    pclose(fp);
    if (running) {
	result = 1;
    }

    Tcl_SetObjResult(interp, Tcl_NewIntObj(result));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * AtkEventLoop --
 *
 *   Spins GLib event loop.
 *
 * Results:
 *
 *   ATK/GLib events are processed.
 *
 * Side effects:
 *
 *    None.
 *
 *----------------------------------------------------------------------
 */
 
int AtkEventLoop(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) 
{
    (void)clientData;
    (void)objc;
    (void)objv;
    
    /* Process all pending Tk events first (keyboard priority).  */
    int tk_processed = 0;
    const int max_tk_events = 20;
    while (tk_processed < max_tk_events && 
	   Tcl_DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT)) {
	tk_processed++;
    }
    
    /* Process GLib events only if no Tk events were pending. */
    int glib_processed = 0;
    if (tk_processed == 0) {
	while (g_main_context_iteration(g_main_context_default(), FALSE)) {
	    glib_processed++;
	}
    }
    
    Tcl_SetObjResult(interp, Tcl_NewIntObj(glib_processed));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkAtkAccessibleObjCmd --
 *
 *   Main command for adding and managing accessibility objects to Tk
 *   widgets on Linux using the Atk accessibility API.
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
 
int TkAtkAccessibleObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    (void) clientData;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "window");
	return TCL_ERROR;
    }

    char *windowName = Tcl_GetString(objv[1]);
    if (!windowName) {
	Tcl_SetResult(interp, "Window name cannot be null.", TCL_STATIC);
	return TCL_ERROR;
    }

    MainThreadData data = {NULL, interp, NULL};
    Tk_Window tkwin = (Tk_Window)RunOnMainThread(name_to_window_main, &data);

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

    /* Register for cleanup, mapping and other events. */
    TkAtkAccessible_RegisterEventHandlers(tkwin, accessible);

    /* Handle toplevels. */
    if ((gboolean)(uintptr_t)RunOnMainThread(is_toplevel_main, &(MainThreadData){tkwin, interp, NULL})) {
	RegisterToplevelWindow(interp, tkwin, (AtkObject*)accessible);
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
    /* Initialize the at-spi bridge.  */
    if (atk_bridge_adaptor_init(NULL, NULL) != 0) {
	Tcl_SetResult(interp, "Failed to initialize AT-SPI bridge", TCL_STATIC);
	return TCL_ERROR;
    }
    
    /* Create and name root accessible.  */
    tk_root_accessible = atk_get_root();
    if (tk_root_accessible) {
	tk_set_name(tk_root_accessible, "Tk Application");
    }

    /* Initialize mapping table.  */
    InitAtkTkMapping();

    /* Register root window as an accessible object - NO THREADING */
    Tk_Window mainWin = Tk_MainWindow(interp);
    if (!mainWin) {
	Tcl_SetResult(interp, "Failed to get main window", TCL_STATIC);
	return TCL_ERROR;
    }

    Tk_MakeWindowExist(mainWin);
    Tk_MapWindow(mainWin);

    /* Process events to ensure window is fully displayed.  */
    while (Tcl_DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT)) {} 

    AtkObject *main_acc = TkCreateAccessibleAtkObject(interp, mainWin, Tk_PathName(mainWin));
    if (!main_acc) {
	Tcl_SetResult(interp, "Failed to create AtkObject for root window", TCL_STATIC);
	return TCL_ERROR;
    }
    
    atk_object_set_role(main_acc, ATK_ROLE_WINDOW);
    RegisterAtkObjectForTkWindow(mainWin, main_acc);
    TkAtkAccessible_RegisterEventHandlers(mainWin, (TkAtkAccessible *)main_acc);
    atk_object_set_parent(main_acc, tk_root_accessible);
    if (!g_list_find(toplevel_accessible_objects, main_acc)) {
	toplevel_accessible_objects = g_list_append(toplevel_accessible_objects, main_acc);
    }
    RegisterChildWidgets(interp, mainWin, main_acc);

    /* Finally, register Tcl commands.  */
    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object",
			 TkAtkAccessibleObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change",
			 EmitSelectionChanged, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_focus_change",
			 EmitFocusChanged, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader",
			 IsScreenReaderRunning, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::_run_atk_eventloop",
			 AtkEventLoop, NULL, NULL);

    return TCL_OK;
}
#else
int TkAtkAccessibility_Init(Tcl_Interp *interp)
{
    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", NULL, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", NULL, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_focus_change", NULL, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader", NULL, NULL, NULL);
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
