/*
 * tkUnixAccessibility.c --
 *
 * This file implements accessibility/screen-reader support
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
#include <time.h>
#include <tcl.h>
#include <tk.h>
#include "tkInt.h"

#ifdef USE_ATK
#include <atk/atk.h>
#include <atk/atktext.h>
#include <atk-bridge.h>
#include <dbus/dbus.h>
#include <glib.h>

/* Structs for custom ATK objects bound to Tk. */
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
    gboolean is_being_destroyed;
    gboolean is_being_created;     /* NEW: Prevent recursion during creation */
    gboolean children_initialized; /* NEW: Track if children are set up */
    GList *children;
    GMutex cleanup_mutex;
} TkAtkAccessible;

typedef struct _TkAtkAccessibleClass {
    AtkObjectClass parent_class;
} TkAtkAccessibleClass;


/* Structs for passing data to main thread. */
typedef struct {
    gpointer (*func)(gpointer);
    gpointer user_data;
    gpointer result;
    gboolean done;
    GMutex mutex;
    GCond cond;
} TkMainThreadCall;

typedef struct {
    Tk_Window tkwin;
    Tcl_Interp *interp;
    gpointer result;
    const char *windowName;
    const char *key;
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
    {"Canvas", ATK_ROLE_CANVAS},
    {"Scrollbar", ATK_ROLE_SCROLL_BAR},
    {"Menubar", ATK_ROLE_MENU_BAR},
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
    AtkObject *parent;
    gint index;
    AtkObject *child;
} ChildrenChangedAddData;


/* Variables for managing Atk objects. */
static AtkObject *tk_root_accessible = NULL;
static GList *toplevel_accessible_objects = NULL; /* This list will hold refs to toplevels. */
static GHashTable *tk_to_atk_map = NULL; /* Maps Tk_Window to AtkObject. */
extern Tcl_HashTable *TkAccessibilityObject; /* Hash table for managing accessibility attributes. */
static GMainContext *glib_context = NULL;
static gboolean in_process_pending = FALSE;
static GHashTable *creation_in_progress = NULL;  /* Track windows being created */


/* Thread management functions. */
gpointer RunOnMainThread(gpointer (*func)(gpointer user_data), gpointer user_data);
void EmitBoundsChanged(AtkObject *obj, gint x, gint y, gint width, gint height);
static gboolean run_main_thread_callback(gpointer data);

/* Helper functions for main thread operations. */
static gpointer get_atk_role_for_widget_main(gpointer data);
static gpointer get_atk_name_for_widget_main(gpointer data);
static gpointer get_atk_description_for_widget_main(gpointer data);
static gpointer get_atk_value_for_widget_main(gpointer data);
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
/* Added: New helper for root/screen coordinates to fix extents */
static gpointer get_root_coords_main(gpointer data);

/* Signal emission helpers. */
static gpointer emit_children_changed_add(gpointer data);
static gpointer emit_children_changed_remove(gpointer data);
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
static gchar *GetAtkNameForWidget(Tk_Window win);
static const gchar *tk_get_name(AtkObject *obj);
static void tk_set_name(AtkObject *obj, const gchar *name);
static gchar *GetAtkDescriptionForWidget(Tk_Window win);
static const gchar *tk_get_description(AtkObject *obj);
static gchar *GetAtkValueforTkWidget(Tk_Window win);
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
static void UnregisterToplevelWindow(AtkObject *accessible);
AtkObject *TkCreateAccessibleAtkObject(Tcl_Interp *interp, Tk_Window tkwin, const char *path);
void InitAtkTkMapping(void);
static void InitRecursionTracking(void);
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
static void UpdateChildrenCache(TkAtkAccessible *acc);
static gboolean DeferredChildrenUpdate(gpointer user_data);

/* Event handlers. */
void TkAtkAccessible_RegisterEventHandlers(Tk_Window tkwin, void *tkAccessible);
static void TkAtkAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_ConfigureHandler(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_MapHandler(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_UnmapHandler(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_FocusHandler(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_CreateHandler(ClientData clientData, XEvent *eventPtr);

/* Tcl command implementations. */
static int EmitSelectionChanged(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int EmitFocusChanged(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int TkAtkAccessibleObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int TkAtkAccessibility_Init(Tcl_Interp *interp);

/* GLib-Tcl event loop integration. */
static void SetupGlibIntegration(void);
static void ProcessPendingEvents(ClientData clientData);

/* Child management functions. */
static void AddChildToParent(TkAtkAccessible *parent, AtkObject *child);
static void RemoveChildFromParent(TkAtkAccessible *parent, AtkObject *child);


/* Define custom Atk object bridged to Tcl/Tk. */
#define TK_ATK_TYPE_ACCESSIBLE (tk_atk_accessible_get_type())
#define TK_ATK_IS_ACCESSIBLE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), TK_ATK_TYPE_ACCESSIBLE))
G_DEFINE_TYPE_WITH_CODE(TkAtkAccessible, tk_atk_accessible, ATK_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(ATK_TYPE_COMPONENT, tk_atk_component_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_ACTION, tk_atk_action_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_VALUE, tk_atk_value_interface_init)
			)
			
/*
 *----------------------------------------------------------------------
 *
 * Thread management functions. at-spi works on background/worker threads
 * and calls to Tcl/Tk and Atk must be directed to the main thread.  
 *
 *----------------------------------------------------------------------
 */
            
/* 
 * Callback function to main thread. This runs on 
 * the main thread (GLib main context). 
 */
 
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

/* Synchronous run-on-main-thread.  Calls function and waits for result. */
gpointer RunOnMainThread(gpointer (*func)(gpointer), gpointer user_data)
{
    if (!glib_context) {
        return func(user_data);
    }

    if (g_main_context_is_owner(glib_context)) {
        return func(user_data);
    }

    TkMainThreadCall call;
    call.func = func;
    call.user_data = user_data;
    call.result = NULL;
    call.done = FALSE;

    g_mutex_init(&call.mutex);
    g_cond_init(&call.cond);

    g_main_context_invoke(glib_context, run_main_thread_callback, &call);

    g_mutex_lock(&call.mutex);
    while (!call.done) {
        g_cond_wait(&call.cond, &call.mutex);
    }
    g_mutex_unlock(&call.mutex);

    g_mutex_clear(&call.mutex);
    g_cond_clear(&call.cond);

    return call.result;
}

/*
 *----------------------------------------------------------------------
 *
 * GLib-Tcl event loop integration
 *
 *----------------------------------------------------------------------
 */

/* 
 * Establish integration between Tcl and GLib 
 * event loops. Call once, from the main thread early 
 * during startup. 
 */
 
static void SetupGlibIntegration(void)
{
    /* Capture the context the main thread actually uses. */
    glib_context = g_main_context_ref_thread_default();
    /* Start a periodic timer that will iterate GLib without blocking. */
    Tcl_CreateTimerHandler(10, ProcessPendingEvents, NULL);
}

/* Process GLib and Tcl events periodically on the Tcl/Tk main thread. */
static void ProcessPendingEvents(ClientData clientData)
{
    (void)clientData;

    /* Prevent reentrancy from nested timers/callbacks. */
    if (in_process_pending) {
        Tcl_CreateTimerHandler(10, ProcessPendingEvents, NULL);
        return;
    }
    in_process_pending = TRUE;

    /* Let Tk service anything queued without blocking. */
    while (Tcl_DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT)) {}

    /* Let GLib service anything queued without blocking. */
    if (glib_context) {
        while (g_main_context_iteration(glib_context, FALSE)) {
        }
    }

    in_process_pending = FALSE;

    /* Re-schedule. */
    Tcl_CreateTimerHandler(10, ProcessPendingEvents, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Child management functions
 *
 *----------------------------------------------------------------------
 */

static void AddChildToParent(TkAtkAccessible *parent, AtkObject *child) 
{
    if (!parent || !child || !G_IS_OBJECT(child)) return;
    
    g_mutex_lock(&parent->cleanup_mutex);
    
    /* Prevent duplicates. */
    if (g_list_find(parent->children, child)) {
        g_mutex_unlock(&parent->cleanup_mutex);
        return;
    }
    
    /* Take explicit reference when adding to parent. */
    g_object_ref(child);
    parent->children = g_list_append(parent->children, child);
    atk_object_set_parent(child, ATK_OBJECT(parent));
    
    g_mutex_unlock(&parent->cleanup_mutex);
}

static void RemoveChildFromParent(TkAtkAccessible *parent, AtkObject *child) 
{
    if (!parent || !child) return;
    
    g_mutex_lock(&parent->cleanup_mutex);
    
    GList *found = g_list_find(parent->children, child);
    if (found) {
        parent->children = g_list_remove(parent->children, child);
        /* Release the reference we took in AddChildToParent. */
        g_object_unref(child);
    }
    
    g_mutex_unlock(&parent->cleanup_mutex);
}

/*
 *----------------------------------------------------------------------
 *
 * Helper functions for main thread operations.
 *
 *----------------------------------------------------------------------
 */
 
/* Helper function to integrate strings. */         
static gchar *sanitize_utf8(const gchar *str) 
{
    if (!str) return NULL;
    return g_utf8_make_valid(str, -1);
}

/* Get accessible value. */
static gpointer get_atk_value_for_widget_main(gpointer data) 
{
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)GetAtkValueForWidget(mt_data->tkwin);
}

/* Get accessible name. */
static gpointer get_atk_name_for_widget_main(gpointer data) 
{
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)(GetAtkNameForWidget(mt_data->tkwin));
}



/* Get accessible role. */
static gpointer get_atk_role_for_widget_main(gpointer data) 
{
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)(uintptr_t)GetAtkRoleForWidget(mt_data->tkwin);
}

/* Get accessible main window name. */
static gpointer get_window_name_main(gpointer data) 
{
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)Tk_PathName(mt_data->tkwin);
}

/* Get window geometry. */
static gpointer get_window_geometry_main(gpointer data) 
{
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
static gpointer is_window_mapped_main(gpointer data) 
{
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)(uintptr_t)Tk_IsMapped(mt_data->tkwin);
}

/* Get focused window. */
static gpointer get_focus_window_main(gpointer data) 
{
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)TkGetFocusWin((TkWindow *)mt_data->tkwin);
}

/* Get parent window. */
static gpointer get_window_parent_main(gpointer data) {
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)Tk_Parent(mt_data->tkwin);
}

/* Force window creation. */
static gpointer make_window_exist_main(gpointer data) 
{
    MainThreadData *mt_data = (MainThreadData *)data;
    Tk_MakeWindowExist(mt_data->tkwin);
    return NULL;
}

/* Force window mapping. */
static gpointer map_window_main(gpointer data) 
{
    MainThreadData *mt_data = (MainThreadData *)data;
    Tk_MapWindow(mt_data->tkwin);
    return NULL;
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
    const char *key = mt_data->key; 
    return (gpointer)Tcl_FindHashEntry(table, key);
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

    if (!mt_data || !mt_data->windowName) {
        return NULL;
    }
    
    return (gpointer)Tk_NameToWindow(mt_data->interp, mt_data->windowName, Tk_MainWindow(mt_data->interp));
}

/* Get the main Tk window.  */
static gpointer get_main_window_main(gpointer data)
{
    MainThreadData *mt_data = (MainThreadData *)data;
    return (gpointer)Tk_MainWindow(mt_data->interp);
}

/* Get root/screen coordinates for correct extents. */
static gpointer get_root_coords_main(gpointer data)
{
    MainThreadData *mt_data = (MainThreadData *)data;
    gint *coords = g_new(gint, 4);
    Tk_GetRootCoords(mt_data->tkwin, &coords[0], &coords[1]);
    coords[2] = Tk_Width(mt_data->tkwin);
    coords[3] = Tk_Height(mt_data->tkwin);
    return coords;
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
static gpointer emit_children_changed_add(gpointer data)
{
    ChildrenChangedAddData *cad = (ChildrenChangedAddData *)data;
    
    if (!cad) {
        g_warning("emit_children_changed_add: NULL data");
        return G_SOURCE_REMOVE;
    }
    
    /* Validate objects before emission. */
    if (cad->parent && G_IS_OBJECT(cad->parent) && 
        cad->child && G_IS_OBJECT(cad->child)) {
        
        /* Check if objects are still valid (not being destroyed). */
        if (TK_ATK_IS_ACCESSIBLE(cad->parent)) {
            TkAtkAccessible *parent_acc = (TkAtkAccessible *)cad->parent;
            if (!parent_acc->is_being_destroyed) {
                g_signal_emit_by_name(cad->parent, "children-changed::add", cad->index, cad->child);
            }
        }
    } else {
        g_warning("emit_children_changed_add: Invalid parent=%p or child=%p object", 
                  cad->parent, cad->child);
    }
    
    /* Safe cleanup .*/
    if (cad->parent && G_IS_OBJECT(cad->parent)) {
        g_object_unref(cad->parent);
    }
    if (cad->child && G_IS_OBJECT(cad->child)) {
        g_object_unref(cad->child);
    }
    g_free(cad);
    
    return G_SOURCE_REMOVE;
}


/* Emit children-changed::remove signal. */
static gpointer emit_children_changed_remove(gpointer data)
{
    ChildrenChangedRemoveData *crd = (ChildrenChangedRemoveData *)data;
    
    if (!crd) {
        g_warning("emit_children_changed_remove: NULL data");
        return G_SOURCE_REMOVE;
    }
    
    /* Validate objects before emission. */
    if (crd->parent && G_IS_OBJECT(crd->parent) && 
        crd->child && G_IS_OBJECT(crd->child)) {
        
        if (TK_ATK_IS_ACCESSIBLE(crd->parent)) {
            TkAtkAccessible *parent_acc = (TkAtkAccessible *)crd->parent;
            if (!parent_acc->is_being_destroyed) {
                g_signal_emit_by_name(crd->parent, "children-changed::remove", crd->index, crd->child);
            }
        }
    } else {
        g_warning("emit_children_changed_remove: Invalid parent=%p or child=%p object", 
                  crd->parent, crd->child);
    }
    
    /* Safe cleanup */
    if (crd->parent && G_IS_OBJECT(crd->parent)) {
        g_object_unref(crd->parent);
    }
    if (crd->child && G_IS_OBJECT(crd->child)) {
        g_object_unref(crd->child);
    }
    g_free(crd);
    
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
 * Map ATK component interface to Tk.
 */
 
static void tk_get_extents(AtkComponent *component, gint *x, gint *y, gint *width, gint *height, AtkCoordType coord_type)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)component;

    if (!acc) {
	*x = *y = *width = *height = 0;
	return;
    }

    MainThreadData data = {acc->tkwin, acc->interp, NULL, NULL, NULL};

    /* Use correct coordinates based on coord_type.
     * ATK_XY_WINDOW: Cached relative to toplevel window (sum of Tk_X/Tk_Y).
     * ATK_XY_SCREEN: Absolute screen via Tk_GetRootCoords (fixes navigation issues).
     */
    if (coord_type == ATK_XY_SCREEN) {
        gint *coords = (gint *)RunOnMainThread(get_root_coords_main, &data);
        if (coords) {
            *x = coords[0];
            *y = coords[1];
            *width = coords[2];
            *height = coords[3];
            g_free(coords);
        } else {
            *x = *y = *width = *height = 0;
        }
    } else {
        /* ATK_XY_WINDOW: Use cached values (relative to toplevel). */
        *x = acc->x;
        *y = acc->y;
        *width = acc->width;
        *height = acc->height;
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
    return g_list_length(acc->children);
}

static AtkObject *tk_ref_child(AtkObject *obj, gint i)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    if (!acc) return NULL;
    
    GList *child = g_list_nth(acc->children, i);
    if (child) {
        g_object_ref(child->data);
        return ATK_OBJECT(child->data);
    }
    return NULL;
}

/*
 * Functions to map accessible role to ATK.
 */

static AtkRole GetAtkRoleForWidget(Tk_Window win)
{
    if (!win) return ATK_ROLE_UNKNOWN;

    MainThreadData data = {win, NULL, NULL, NULL, "role"};
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

    /* Fallback to widget class if no attribute set. */
    const char *widgetClass = Tk_Class(win);
    if (widgetClass) {
        for (int i = 0; roleMap[i].tkrole != NULL; i++) {
            if (strcasecmp(roleMap[i].tkrole, widgetClass) == 0) {  /* Case-insensitive for robustness */
                return roleMap[i].atkrole;
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

static gchar *GetAtkNameForWidget(Tk_Window win)
{
    if (!win) return NULL;
    
    gchar *name = NULL;

    MainThreadData data = {win, NULL, NULL, NULL, "name"};
    Tcl_HashEntry *hPtr = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_main, &data);
    if (hPtr) {
	Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)RunOnMainThread(get_hash_value_main, &data);
	if (AccessibleAttributes) {
	    Tcl_HashEntry *hPtr2 = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_by_key_main, &data);
	    if (hPtr2) {
		const char *result = (const char *)RunOnMainThread(get_hash_string_value_main, &data);
		if (result) {
		    name = sanitize_utf8(result);
		}
	    }
	}
    }
    return name;
}

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

static gchar *GetAtkDescriptionForWidget(Tk_Window win)
{
    if (!win) return;

    gchar *description = NULL;

    MainThreadData data = {win, NULL, NULL, NULL, "description"};
    Tcl_HashEntry *hPtr = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_main, &data);
    if (hPtr) {
	Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)RunOnMainThread(get_hash_value_main, &data);
	if (AccessibleAttributes) {
	    Tcl_HashEntry *hPtr2 = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_by_key_main, &data);
	    if (hPtr2) {
		const char *result = (const char *)RunOnMainThread(get_hash_string_value_main, &data);
		if (result) {
		    description = sanitize_utf8(result);
		}
	    }
	}
    }
    return description;
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

static gchar *GetAtkValueforTkWidget(Tk_Window win)
{
    if (!win) return;
    
    gchar *value = NULL;

    MainThreadData data = {win, NULL, NULL, NULL, "value"};
    Tcl_HashEntry *hPtr = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_main, &data);
    if (hPtr) {
	Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)RunOnMainThread(get_hash_value_main, &data);
	if (AccessibleAttributes) {
	    Tcl_HashEntry *hPtr2 = (Tcl_HashEntry *)RunOnMainThread(find_hash_entry_by_key_main, &data);
	    if (hPtr2) {
		const char *result = (const char *)RunOnMainThread(get_hash_string_value_main, &data);
		if (result) {
		    value = sanitize_utf8(result);
		}
	    }
	}
    }
    return value;
}

 
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
 * from Tk to ATK.
 */

static gboolean tk_action_do_action(AtkAction *action, gint i)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)action;

    if (!acc || !acc->tkwin || !acc->interp) {
	return FALSE;
    }

    if (i == 0) {
	/* Retrieve the command string.  */
	MainThreadData data = {acc->tkwin, acc->interp, NULL, NULL, "action"};
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

	/* Finally, execute command.  */
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
 * Functions to initialize and manage the parent ATK class and object instances.
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
    self->is_being_destroyed = FALSE;
    self->is_being_created = FALSE;      /* NEW */
    self->children_initialized = FALSE;  /* NEW */
    self->children = NULL;
    g_mutex_init(&self->cleanup_mutex);
}
static void tk_atk_accessible_finalize(GObject *gobject)
{
    TkAtkAccessible *self = (TkAtkAccessible*)gobject;
    
    if (!self) return;

    /* Mark as being destroyed. */
    g_mutex_lock(&self->cleanup_mutex);
    self->is_being_destroyed = TRUE;
    g_mutex_unlock(&self->cleanup_mutex);

    /* Clean up Tk window association. */
    if (self->tkwin) {
        /* Check if this is a toplevel. */
        MainThreadData data = {self->tkwin, self->interp, NULL, NULL, NULL};
        if ((gboolean)(uintptr_t)RunOnMainThread(is_toplevel_main, &data)) {
            UnregisterToplevelWindow(ATK_OBJECT(self));
        }
        UnregisterAtkObjectForTkWindow(self->tkwin);
        self->tkwin = NULL;
    }

    /* Clean up children safely. */
    g_mutex_lock(&self->cleanup_mutex);
    GList *children_copy = g_list_copy(self->children);
    /* Clear the list but don't free yet. */
    g_list_free(self->children);
    self->children = NULL;
    g_mutex_unlock(&self->cleanup_mutex);

    /* Release children references outside mutex.*/
    GList *iter = children_copy;
    while (iter) {
        AtkObject *child = (AtkObject *)iter->data;
        if (child && G_IS_OBJECT(child)) {
            /* Only unref if we actually own a reference. */
            g_object_unref(child);
        }
        iter = g_list_next(iter);
    }
    g_list_free(children_copy);

    /* Clean up cached strings. */
    g_free(self->path);
    g_free(self->cached_name);
    g_free(self->cached_description);
    g_free(self->cached_value);
    
    /* Clear mutex last. */
    g_mutex_clear(&self->cleanup_mutex);

    /* Call parent finalize. */
    G_OBJECT_CLASS(tk_atk_accessible_parent_class)->finalize(gobject);
}

static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    AtkObjectClass *atk_class = ATK_OBJECT_CLASS(klass);

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
    /* Validate inputs. */
    if (!accessible || !tkwin || !G_IS_OBJECT(accessible)) {
        g_warning("RegisterToplevelWindow: Invalid tkwin or accessible");
        return;
    }
    
    /* Initialize root accessible if not set. */
    if (!tk_root_accessible) {
        tk_root_accessible = tk_util_get_root();
        if (tk_root_accessible) {
            tk_set_name(tk_root_accessible, "Tk Application");
            AtkStateSet *state_set = atk_object_ref_state_set(tk_root_accessible);
            if (state_set != NULL) {
		atk_state_set_add_state(state_set, ATK_STATE_VISIBLE);
		atk_state_set_add_state(state_set, ATK_STATE_SHOWING);
		atk_state_set_add_state(state_set, ATK_STATE_ENABLED);
		
		atk_object_notify_state_change(tk_root_accessible, ATK_STATE_VISIBLE, TRUE);
		atk_object_notify_state_change(tk_root_accessible, ATK_STATE_SHOWING, TRUE);
		atk_object_notify_state_change(tk_root_accessible, ATK_STATE_ENABLED, TRUE);
		g_object_unref(state_set);
	    }
        }
    }
    
    /* Check for existing registration. */
    AtkObject *existing = GetAtkObjectForTkWindow(tkwin);
    if (existing && existing != accessible) {
        g_warning("RegisterToplevelWindow: Toplevel %s already registered with different AtkObject",
                  (const char *)RunOnMainThread(get_window_name_main, &(MainThreadData){tkwin, NULL, NULL, NULL, NULL}));
        return;
    }

    /* Hold a reference to the accessible object. */
    g_object_ref(accessible);

    /* Set parent and add to root's children. */
    atk_object_set_parent(accessible, tk_root_accessible);
    AddChildToParent((TkAtkAccessible *)tk_root_accessible, accessible);
    
    /* Emit children-changed::add signal synchronously. */
    ChildrenChangedAddData *cad = g_new0(ChildrenChangedAddData, 1);
    cad->parent = tk_root_accessible;
    cad->index = g_list_length(((TkAtkAccessible *)tk_root_accessible)->children) - 1;
    cad->child = accessible;
    if (cad->parent) g_object_ref(cad->parent);
    if (cad->child) g_object_ref(cad->child);
    g_debug("RegisterToplevelWindow: Emitting children-changed::add for %s, parent=%p, index=%d",
            ((TkAtkAccessible *)accessible)->path, tk_root_accessible, cad->index);
    RunOnMainThread(emit_children_changed_add, cad);
    
    /* Add to toplevel_accessible_objects. */
    if (!g_list_find(toplevel_accessible_objects, accessible)) {
        toplevel_accessible_objects = g_list_append(toplevel_accessible_objects, accessible);
    }

    /* Set name, role, and description. */
    const gchar *name = tk_get_name(accessible);
    if (name) {
        tk_set_name(accessible, name);
    } else {
        tk_set_name(accessible, (const char *)RunOnMainThread(get_window_name_main, &(MainThreadData){tkwin, NULL, NULL, NULL, NULL}));
    }
    atk_object_set_role(accessible, ATK_ROLE_WINDOW);
    atk_object_set_description(accessible, name ? name : ((TkAtkAccessible *)accessible)->path);

    /* Set states and notify. */
    AtkStateSet *state_set = atk_object_ref_state_set(accessible);
    if (state_set != NULL) {
	atk_state_set_add_state(state_set, ATK_STATE_VISIBLE);
	atk_state_set_add_state(state_set, ATK_STATE_SHOWING);
	atk_state_set_add_state(state_set, ATK_STATE_ENABLED);
    
	atk_object_notify_state_change(accessible, ATK_STATE_VISIBLE, TRUE);
	atk_object_notify_state_change(accessible, ATK_STATE_SHOWING, TRUE);
	atk_object_notify_state_change(accessible, ATK_STATE_ENABLED, TRUE);
	g_object_unref(state_set);
    }


    /* Register child widgets. */
    RegisterChildWidgets(interp, tkwin, accessible);

    /* Register event handlers. */
    TkAtkAccessible_RegisterEventHandlers(tkwin, (TkAtkAccessible *)accessible);

    /* Force event loop processing to ensure signals reach Orca. */
    if (glib_context) {
        while (g_main_context_iteration(glib_context, FALSE)) {}
    }
}

/* Remove toplevel window from ATK object list. */
static void UnregisterToplevelWindow(AtkObject *accessible)
{
    if (!accessible) return;

    if (g_list_find(toplevel_accessible_objects, accessible)) {
        /* Compute index before removal. */
        gint index = g_list_index(((TkAtkAccessible *)tk_root_accessible)->children, accessible);
        
        /* Remove from root's children. */
        RemoveChildFromParent((TkAtkAccessible *)tk_root_accessible, accessible);
        
        /* Emit signal. */
        ChildrenChangedRemoveData *crd = g_new0(ChildrenChangedRemoveData, 1);
        crd->parent = tk_root_accessible;
        crd->index = index;
        crd->child = accessible;
        RunOnMainThread(emit_children_changed_remove, crd);
        
        toplevel_accessible_objects = g_list_remove(toplevel_accessible_objects, accessible);
    }
}


/* 
 * Register child widgets of a given window 
 * as accessible objects. 
 */

static void RegisterChildWidgets(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *parent_obj) 
{
    (void) interp;
    
    if (!tkwin || !parent_obj || !G_IS_OBJECT(parent_obj)) {
	g_warning("RegisterChildWidgets: Invalid tkwin or parent_obj");
	return;
    }

    TkAtkAccessible *acc = (TkAtkAccessible *)parent_obj;
    /* Update children cache asynchronously. */
    UpdateChildrenCache(acc);
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

/* ATK-Tk object creation with proper parent/child relationship. */
AtkObject *TkCreateAccessibleAtkObject(Tcl_Interp *interp, Tk_Window tkwin, const char *path)
{
    if (!interp || !tkwin || !path) {
        g_warning("TkCreateAccessibleAtkObject: Invalid parameters");
        return NULL;
    }

    /* Check if this window is already being created (recursion detection). */
    if (g_hash_table_contains(creation_in_progress, tkwin)) {
        g_debug("TkCreateAccessibleAtkObject: Recursion detected for %s, skipping", path);
        return NULL;  /* Return NULL to break recursion */
    }

    /* Check if already exists. */
    AtkObject *existing = GetAtkObjectForTkWindow(tkwin);
    if (existing) {
        g_debug("TkCreateAccessibleAtkObject: Object already exists for %s", path);
        return existing;
    }

    /* Mark this window as being created. */
    g_hash_table_add(creation_in_progress, tkwin);

    /* Create the accessible object. */
    TkAtkAccessible *acc = g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
    acc->interp = interp;
    acc->tkwin = tkwin;
    acc->path = sanitize_utf8(path);
    acc->is_being_created = TRUE;      /* Mark as being created */
    acc->children_initialized = FALSE; /* Children not set up yet */

    /* Handle toplevel special case. */
    MainThreadData data = {tkwin, interp, NULL, NULL, NULL};
    if ((gboolean)(uintptr_t)RunOnMainThread(is_toplevel_main, &data) &&
        tkwin != (Tk_Window)RunOnMainThread(get_main_window_main, &(MainThreadData){NULL, interp, NULL, NULL, NULL})) {
        RunOnMainThread(make_window_exist_main, &data);
        RunOnMainThread(map_window_main, &data);
    }

    /* Update basic cached properties (but NOT children yet). */
    UpdateGeometryCache(acc);
    UpdateNameCache(acc);
    UpdateDescriptionCache(acc);
    UpdateValueCache(acc);
    UpdateRoleCache(acc);
    UpdateStateCache(acc);
    /* DON'T call UpdateChildrenCache here - this causes recursion! */

    AtkObject *obj = ATK_OBJECT(acc);
    AtkRole role = acc->cached_role;
    if (role == ATK_ROLE_UNKNOWN &&
        ((gboolean)(uintptr_t)RunOnMainThread(is_toplevel_main, &data) ||
         tkwin == (Tk_Window)RunOnMainThread(get_main_window_main, &(MainThreadData){NULL, interp, NULL, NULL, NULL}))) {
        role = ATK_ROLE_WINDOW;
    }
    atk_object_set_role(obj, role);

    /* Set ATK name and description. */
    if (acc->cached_description && *acc->cached_description) {
        atk_object_set_name(obj, acc->cached_description);
    } else if (acc->cached_name && *acc->cached_name) {
        atk_object_set_name(obj, acc->cached_name);
    } else {
        atk_object_set_name(obj, path);
    }
    if (acc->cached_description && *acc->cached_description) {
        atk_object_set_description(obj, acc->cached_description);
    } else {
        atk_object_set_description(obj, path);
    }

    /* Set initial states. */
    AtkStateSet *state_set = atk_object_ref_state_set(obj);
    if (state_set != NULL) {
        atk_state_set_add_state(state_set, ATK_STATE_VISIBLE);
        atk_state_set_add_state(state_set, ATK_STATE_SHOWING);
        atk_state_set_add_state(state_set, ATK_STATE_ENABLED);
        if (role == ATK_ROLE_PUSH_BUTTON || role == ATK_ROLE_ENTRY ||
            role == ATK_ROLE_COMBO_BOX || role == ATK_ROLE_CHECK_BOX ||
            role == ATK_ROLE_RADIO_BUTTON || role == ATK_ROLE_SLIDER ||
            role == ATK_ROLE_SPIN_BUTTON) {
            atk_state_set_add_state(state_set, ATK_STATE_FOCUSABLE);
        }
        atk_object_notify_state_change(obj, ATK_STATE_VISIBLE, TRUE);
        atk_object_notify_state_change(obj, ATK_STATE_SHOWING, TRUE);
        atk_object_notify_state_change(obj, ATK_STATE_ENABLED, TRUE);
        if (atk_state_set_contains_state(state_set, ATK_STATE_FOCUSABLE)) {
            atk_object_notify_state_change(obj, ATK_STATE_FOCUSABLE, TRUE);
        }
        g_object_unref(state_set);
    }

    /* Register the object BEFORE setting up parent/child relationships. */
    RegisterAtkObjectForTkWindow(tkwin, obj);
    
    /* Set up parent relationship. */
    Tk_Window parent_tkwin = (Tk_Window)RunOnMainThread(get_window_parent_main, &data);
    AtkObject *parent_obj = NULL;

    if (parent_tkwin) {
        parent_obj = GetAtkObjectForTkWindow(parent_tkwin);
        if (!parent_obj && !g_hash_table_contains(creation_in_progress, parent_tkwin)) {
            /* Create parent only if not in recursion. */
            TkAtkAccessible *parent_acc = g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
            parent_acc->interp = interp;
            parent_acc->tkwin = parent_tkwin;
            parent_acc->path = sanitize_utf8((const char *)RunOnMainThread(get_window_name_main, &(MainThreadData){parent_tkwin, interp, NULL, NULL, NULL}));
            parent_acc->is_being_created = TRUE;
            parent_acc->children_initialized = FALSE;
            
            /* Update parent basic properties (no children). */
            UpdateGeometryCache(parent_acc);
            UpdateNameCache(parent_acc);
            UpdateDescriptionCache(parent_acc);
            UpdateValueCache(parent_acc);
            UpdateRoleCache(parent_acc);
            UpdateStateCache(parent_acc);
            
            parent_obj = ATK_OBJECT(parent_acc);
            atk_object_set_role(parent_obj, ATK_ROLE_WINDOW);
            if (parent_acc->cached_name) {
                atk_object_set_name(parent_obj, parent_acc->cached_name);
            } else {
                atk_object_set_name(parent_obj, parent_acc->path);
            }
            if (parent_acc->cached_description) {
                atk_object_set_description(parent_obj, parent_acc->cached_description);
            } else {
                atk_object_set_description(parent_obj, parent_acc->path);
            }
            
            RegisterAtkObjectForTkWindow(parent_tkwin, parent_obj);
            TkAtkAccessible_RegisterEventHandlers(parent_tkwin, parent_acc);
            
            /* Handle parent's parent. */
            Tk_Window grandparent_tkwin = (Tk_Window)RunOnMainThread(get_window_parent_main, &(MainThreadData){parent_tkwin, interp, NULL, NULL, NULL});
            AtkObject *grandparent_obj = grandparent_tkwin ? GetAtkObjectForTkWindow(grandparent_tkwin) : tk_root_accessible;
            if (grandparent_obj && G_IS_OBJECT(grandparent_obj)) {
                atk_object_set_parent(parent_obj, grandparent_obj);
                AddChildToParent((TkAtkAccessible *)grandparent_obj, parent_obj);
            }

            if ((gboolean)(uintptr_t)RunOnMainThread(is_toplevel_main, &(MainThreadData){parent_tkwin, interp, NULL, NULL, NULL})) {
                if (!g_list_find(toplevel_accessible_objects, parent_obj)) {
                    toplevel_accessible_objects = g_list_append(toplevel_accessible_objects, parent_obj);
                }
            }
            
            parent_acc->is_being_created = FALSE;
        }
    } else {
        parent_obj = tk_root_accessible;
    }

    if (parent_obj && G_IS_OBJECT(parent_obj)) {
        atk_object_set_parent(obj, parent_obj);
        AddChildToParent((TkAtkAccessible *)parent_obj, obj);
        
        /* Emit child addition signal */
        ChildrenChangedAddData *cad = g_new0(ChildrenChangedAddData, 1);
        cad->parent = g_object_ref(parent_obj);
        cad->index = g_list_length(((TkAtkAccessible *)parent_obj)->children) - 1;
        cad->child = g_object_ref(obj);
        RunOnMainThread(emit_children_changed_add, cad);
    } else {
        g_warning("TkCreateAccessibleAtkObject: Invalid parent for %s", path);
        g_hash_table_remove(creation_in_progress, tkwin);  /* Clean up tracking */
        g_object_unref(obj);
        return NULL;
    }

    /* Handle toplevel registration. */
    if ((gboolean)(uintptr_t)RunOnMainThread(is_toplevel_main, &data)) {
        RegisterToplevelWindow(interp, tkwin, obj);
    }

    /* Mark creation as complete. */
    acc->is_being_created = FALSE;
    
    /* Remove from creation tracking. */
    g_hash_table_remove(creation_in_progress, tkwin);

    /* Shedule children update for LATER, not now. This breaks the recursion cycle. */
    g_idle_add_full(G_PRIORITY_LOW, (GSourceFunc)DeferredChildrenUpdate, acc, NULL);

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

static void InitRecursionTracking(void)
{
    if (!creation_in_progress) {
        creation_in_progress = g_hash_table_new(g_direct_hash, g_direct_equal);
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
 * Cache update functions so that Tcl/Tk calls are not made from within Atk 
 * functions controlled by the GLib event loop.
 *
 *----------------------------------------------------------------------
 */
 
static void UpdateGeometryCache(TkAtkAccessible *acc) 
{
    if (!acc || !acc->tkwin) return;
    
    MainThreadData data = {acc->tkwin, acc->interp, NULL, NULL, NULL};
    gint *geometry = RunOnMainThread(get_window_geometry_main, &data);
    if (geometry) {
        acc->x = geometry[0];
        acc->y = geometry[1];
        acc->width = geometry[2];
        acc->height = geometry[3];
        
        /* Convert to root coordinates. */
        Tk_Window parent = Tk_Parent(acc->tkwin);
        while (parent) {
            acc->x += Tk_X(parent);
            acc->y += Tk_Y(parent);
            parent = Tk_Parent(parent);
        }
        g_free(geometry);
    }
}

static void UpdateNameCache(TkAtkAccessible *acc)
{
    if (!acc || !acc->tkwin) return;
    MainThreadData data = {acc->tkwin, acc->interp, NULL, NULL, "name"};
    acc->cached_name = (gchar*)RunOnMainThread(get_atk_name_for_widget_main, &data);
}

static void UpdateDescriptionCache(TkAtkAccessible *acc)
{
    if (!acc || !acc->tkwin) return;
    MainThreadData data = {acc->tkwin, acc->interp, NULL, NULL, "description"};
    acc->cached_value = (gchar*)RunOnMainThread(get_atk_description_for_widget_main, &data);
}

static void UpdateValueCache(TkAtkAccessible *acc)
{
    if (!acc || !acc->tkwin) return;
    MainThreadData data = {acc->tkwin, acc->interp, NULL, NULL, "value"};
    acc->cached_value = (gchar*)RunOnMainThread(get_atk_value_for_widget_main, &data);
}

static void UpdateRoleCache(TkAtkAccessible *acc)
{
    if (!acc || !acc->tkwin) return;
    MainThreadData data = {acc->tkwin, acc->interp, NULL, NULL, "role"};
    acc->cached_role = (AtkRole)(uintptr_t)RunOnMainThread(get_atk_role_for_widget_main, &data);
}

static void UpdateStateCache(TkAtkAccessible *acc) 
{
    if (!acc || !acc->tkwin) return;
    
    MainThreadData data = {acc->tkwin, acc->interp, NULL, NULL, "state"};
    acc->is_mapped = (gboolean)(uintptr_t)RunOnMainThread(is_window_mapped_main, &data);
    
    TkWindow *focuswin = (TkWindow *)RunOnMainThread(get_focus_window_main, &data);
    Tk_Window focus_win = (Tk_Window)focuswin;
    acc->has_focus = (focus_win == acc->tkwin);
    
    if (!acc->has_focus && focus_win) {
	Tk_Window parent = (Tk_Window)RunOnMainThread(get_window_parent_main, &(MainThreadData){focus_win, NULL, NULL, NULL, NULL});
	while (parent) {
	    if (parent == acc->tkwin) {
		acc->has_focus = TRUE;
		break;
	    }
	    parent = (Tk_Window)RunOnMainThread(get_window_parent_main, &(MainThreadData){parent, NULL, NULL, NULL, NULL});
	}
    }
}

static void UpdateChildrenCache(TkAtkAccessible *acc)
{
    if (!acc || !acc->tkwin || !acc->interp) return;
    if (acc->is_being_destroyed || acc->is_being_created) return;

    /* Prevent recursive calls. */
    static GHashTable *updating_objects = NULL;
    if (!updating_objects) {
        updating_objects = g_hash_table_new(g_direct_hash, g_direct_equal);
    }
    
    if (g_hash_table_contains(updating_objects, acc)) {
        g_debug("UpdateChildrenCache: Recursive call detected for %s", acc->path);
        return;
    }
    
    g_hash_table_add(updating_objects, acc);

    g_mutex_lock(&acc->cleanup_mutex);

    MainThreadData data = {acc->tkwin, acc->interp, NULL, NULL, NULL};
    TkWindow *winPtr = (TkWindow *)RunOnMainThread(get_window_handle_main, &data);
    if (!winPtr) {
        g_mutex_unlock(&acc->cleanup_mutex);
        g_hash_table_remove(updating_objects, acc);
        return;
    }

    /* Store old children for comparison. */
    GList *old_children = g_list_copy(acc->children);
    
    /* Build new children list - but DON'T create objects if they don't exist.*/
    GList *new_children = NULL;
    for (TkWindow *childPtr = winPtr->childList; childPtr != NULL; childPtr = childPtr->nextPtr) {
        Tk_Window child = (Tk_Window)childPtr;
        AtkObject *child_obj = GetAtkObjectForTkWindow(child);
        
        /* Only add existing objects - don't create new ones here. */
        if (child_obj && G_IS_OBJECT(child_obj)) {
            g_object_ref(child_obj);
            new_children = g_list_append(new_children, child_obj);
            atk_object_set_parent(child_obj, ATK_OBJECT(acc));
        }
    }

    /* Replace children list. */
    acc->children = new_children;
    acc->children_initialized = TRUE;
    
    g_mutex_unlock(&acc->cleanup_mutex);

    /* Clean up old children. */
    GList *iter = old_children;
    while (iter) {
        AtkObject *old_child = (AtkObject *)iter->data;
        if (old_child && G_IS_OBJECT(old_child)) {
            if (!g_list_find(new_children, old_child)) {
                gint old_index = g_list_index(old_children, old_child);
                ChildrenChangedRemoveData *crd = g_new0(ChildrenChangedRemoveData, 1);
                crd->parent = g_object_ref(ATK_OBJECT(acc));
                crd->index = old_index;
                crd->child = g_object_ref(old_child);
                RunOnMainThread(emit_children_changed_remove, crd);
            }
            g_object_unref(old_child);
        }
        iter = g_list_next(iter);
    }
    g_list_free(old_children);

    /* Emit addition signals for genuinely new children. */
    iter = new_children;
    gint index = 0;
    while (iter) {
        AtkObject *new_child = (AtkObject *)iter->data;
        if (new_child && !g_list_find(old_children, new_child)) {
            ChildrenChangedAddData *cad = g_new0(ChildrenChangedAddData, 1);
            cad->parent = g_object_ref(ATK_OBJECT(acc));
            cad->index = index;
            cad->child = g_object_ref(new_child);
            RunOnMainThread(emit_children_changed_add, cad);
        }
        iter = g_list_next(iter);
        index++;
    }

    g_hash_table_remove(updating_objects, acc);
}


static gboolean DeferredChildrenUpdate(gpointer user_data)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)user_data;
    
    if (!acc || acc->is_being_destroyed || acc->children_initialized) {
        return G_SOURCE_REMOVE;
    }

    /* Now it's safe to create missing child objects. */
    if (acc->tkwin && acc->interp) {
        MainThreadData data = {acc->tkwin, acc->interp, NULL, NULL, NULL};
        TkWindow *winPtr = (TkWindow *)RunOnMainThread(get_window_handle_main, &data);
        
        if (winPtr) {
            for (TkWindow *childPtr = winPtr->childList; childPtr != NULL; childPtr = childPtr->nextPtr) {
                Tk_Window child = (Tk_Window)childPtr;
                AtkObject *child_obj = GetAtkObjectForTkWindow(child);
                
                if (!child_obj) {
                    /* Now it's safe to create the child. */
                    MainThreadData child_data = {child, acc->interp, NULL, NULL, NULL};
                    const char *child_path = (const char *)RunOnMainThread(get_window_name_main, &child_data);
                    child_obj = TkCreateAccessibleAtkObject(acc->interp, child, child_path);
                    if (child_obj) {
                        TkAtkAccessible_RegisterEventHandlers(child, (TkAtkAccessible *)child_obj);
                    }
                }
            }
            
            /* Update the children cache now that all objects exist. */
            UpdateChildrenCache(acc);
        }
    }

    return G_SOURCE_REMOVE;
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
    Tk_CreateEventHandler(tkwin, CreateNotify,
			  TkAtkAccessible_CreateHandler, tkAccessible);
}

/* Respond to <Destroy> events. */
static void TkAtkAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr) 
{
    if (eventPtr->type != DestroyNotify) return;
    
    TkAtkAccessible *tkAccessible = (TkAtkAccessible *)clientData;
    if (!tkAccessible || !G_IS_OBJECT(tkAccessible)) {
        g_warning("TkAtkAccessible_DestroyHandler: Invalid tkAccessible");
        return;
    }

    /* Thread-safe cleanup check. */
    g_mutex_lock(&tkAccessible->cleanup_mutex);
    if (tkAccessible->is_being_destroyed) {
        g_mutex_unlock(&tkAccessible->cleanup_mutex);
        return; /* Already being cleaned up. */
    }
    tkAccessible->is_being_destroyed = TRUE;
    g_mutex_unlock(&tkAccessible->cleanup_mutex);

    /* Remove from parent's children list safely. */
    AtkObject *parent = atk_object_get_parent(ATK_OBJECT(tkAccessible));
    if (parent && G_IS_OBJECT(parent) && TK_ATK_IS_ACCESSIBLE(parent)) {
        TkAtkAccessible *parent_acc = (TkAtkAccessible *)parent;
        g_mutex_lock(&parent_acc->cleanup_mutex);
        
        gint index = g_list_index(parent_acc->children, tkAccessible);
        if (index >= 0) {
            RemoveChildFromParent(parent_acc, ATK_OBJECT(tkAccessible));
            
            /* Emit signal safely. */
            ChildrenChangedRemoveData *crd = g_new0(ChildrenChangedRemoveData, 1);
            crd->parent = g_object_ref(parent);  /* Take explicit ref */
            crd->index = index;
            crd->child = g_object_ref(ATK_OBJECT(tkAccessible));  /* Take explicit ref. */
            RunOnMainThread(emit_children_changed_remove, crd);
        }
        g_mutex_unlock(&parent_acc->cleanup_mutex);
    }

    /* Unregister from mapping - this is safe.*/
    if (tkAccessible->tkwin) {
        UnregisterAtkObjectForTkWindow(tkAccessible->tkwin);
        tkAccessible->tkwin = NULL; /* Prevent reuse */
    }
}


/* Respond to <Confgure> event. */
static void TkAtkAccessible_ConfigureHandler(ClientData clientData, XEvent *eventPtr) 
{
    if (eventPtr->type != ConfigureNotify) return;

    TkAtkAccessible *acc = (TkAtkAccessible *)clientData;
    if (!acc || !acc->tkwin || acc->is_being_destroyed) {
        return;
    }

    /* Prevent recursive updates */
    static gboolean updating = FALSE;
    if (updating) return;
    updating = TRUE;

    /* Update caches safely */
    if (!acc->is_being_destroyed) {
        UpdateGeometryCache(acc);
        UpdateNameCache(acc);
        UpdateDescriptionCache(acc);
        UpdateValueCache(acc);
        UpdateRoleCache(acc);
        UpdateStateCache(acc);
        
        /* Update children cache asynchronously to avoid recursion */
        g_idle_add((GSourceFunc)UpdateChildrenCache, acc);
    }

    updating = FALSE;

    /* Emit signals for valid objects only */
    if (!acc->is_being_destroyed && acc->width > 0 && acc->height > 0 && acc->is_mapped) {
        BoundsChangedData *bcd = g_new0(BoundsChangedData, 1);
        bcd->obj = ATK_OBJECT(acc);
        bcd->rect.x = acc->x;
        bcd->rect.y = acc->y;
        bcd->rect.width = acc->width;
        bcd->rect.height = acc->height;
        RunOnMainThread(emit_bounds_changed, bcd);
    }
}

/* Respond to <Map> event. */
static void TkAtkAccessible_MapHandler(ClientData clientData, XEvent *eventPtr)
{
    if (eventPtr->type != MapNotify) return;
    TkAtkAccessible *acc = (TkAtkAccessible *)clientData;
    if (!acc || !acc->tkwin) {
        g_warning("TkAtkAccessible_MapHandler: Invalid or null acc/tkwin");
        return;
    }

    acc->is_mapped = TRUE;
    UpdateStateCache(acc);
    UpdateGeometryCache(acc);

    AtkObject *atk_obj = ATK_OBJECT(acc);
    AtkStateSet *state_set = atk_object_ref_state_set(atk_obj);
    if (state_set != NULL) {
	atk_state_set_add_state(state_set, ATK_STATE_VISIBLE);
	atk_state_set_add_state(state_set, ATK_STATE_SHOWING);
	atk_state_set_add_state(state_set, ATK_STATE_ENABLED);
	if (atk_object_get_role(atk_obj) == ATK_ROLE_PUSH_BUTTON ||
	    atk_object_get_role(atk_obj) == ATK_ROLE_ENTRY ||
	    atk_object_get_role(atk_obj) == ATK_ROLE_COMBO_BOX ||
	    atk_object_get_role(atk_obj) == ATK_ROLE_CHECK_BOX ||
	    atk_object_get_role(atk_obj) == ATK_ROLE_RADIO_BUTTON ||
	    atk_object_get_role(atk_obj) == ATK_ROLE_SLIDER ||
	    atk_object_get_role(atk_obj) == ATK_ROLE_SPIN_BUTTON) {
	    atk_state_set_add_state(state_set, ATK_STATE_FOCUSABLE);
	}
	
	atk_object_notify_state_change(atk_obj, ATK_STATE_VISIBLE, TRUE);
	atk_object_notify_state_change(atk_obj, ATK_STATE_SHOWING, TRUE);
	atk_object_notify_state_change(atk_obj, ATK_STATE_ENABLED, TRUE);
	if (atk_state_set_contains_state(state_set, ATK_STATE_FOCUSABLE)) {
	    atk_object_notify_state_change(atk_obj, ATK_STATE_FOCUSABLE, TRUE);
	}
	g_object_unref(state_set);
    }

    /* Re-register children to ensure hierarchy is updated. */
    RegisterChildWidgets(acc->interp, acc->tkwin, atk_obj);
}

/* Respond to <Unmap> event. */
static void TkAtkAccessible_UnmapHandler(ClientData clientData, XEvent *eventPtr)
{
    if (eventPtr->type != UnmapNotify) return;
    TkAtkAccessible *acc = (TkAtkAccessible *)clientData;
    if (!acc || !acc->tkwin) return;

    acc->is_mapped = FALSE;
    UpdateStateCache(acc);

    AtkObject *atk_obj = ATK_OBJECT(acc);
    atk_object_notify_state_change(atk_obj, ATK_STATE_SHOWING, FALSE);
    atk_object_notify_state_change(atk_obj, ATK_STATE_VISIBLE, FALSE);
}

/* Respond to <FocusIn/Out> events. */
static void TkAtkAccessible_FocusHandler(ClientData clientData, XEvent *eventPtr)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)clientData;
    if (!acc || !acc->tkwin || !G_IS_OBJECT(acc)) {
        g_warning("TkAtkAccessible_FocusHandler: Invalid or null acc/tkwin");
        return;
    }

    UpdateStateCache(acc);
    if (!acc->is_mapped) {
        g_debug("TkAtkAccessible_FocusHandler: Window %s is not mapped, skipping focus handling",
                Tk_PathName(acc->tkwin));
        return;
    }

    AtkObject *atk_obj = ATK_OBJECT(acc);
    AtkStateSet *state_set = atk_object_ref_state_set(atk_obj);
    if (!state_set) {
        g_warning("TkAtkAccessible_FocusHandler: Failed to get state set for %s",
                  Tk_PathName(acc->tkwin));
        return;
    }

    AtkRole role = acc->cached_role;
    if (role == ATK_ROLE_PUSH_BUTTON || role == ATK_ROLE_ENTRY ||
        role == ATK_ROLE_COMBO_BOX || role == ATK_ROLE_CHECK_BOX ||
        role == ATK_ROLE_RADIO_BUTTON || role == ATK_ROLE_SLIDER ||
        role == ATK_ROLE_SPIN_BUTTON || role == ATK_ROLE_WINDOW) {
        atk_state_set_add_state(state_set, ATK_STATE_FOCUSABLE);
    }

    g_debug("TkAtkAccessible_FocusHandler: Object %s focus state: %s",
            Tk_PathName(acc->tkwin), eventPtr->type == FocusIn ? "FocusIn" : "FocusOut");

    if (eventPtr->type == FocusIn) {
        FocusEventData *fed = g_new0(FocusEventData, 1);
        fed->obj = atk_obj;
        fed->state = TRUE;
        RunOnMainThread(emit_focus_event, fed);

        StateChangeData *scd = g_new0(StateChangeData, 1);
        scd->obj = atk_obj;
        scd->name = g_strdup("focused");
        scd->state = TRUE;
        RunOnMainThread(emit_state_change, scd);
    } else if (eventPtr->type == FocusOut) {
        FocusEventData *fed = g_new0(FocusEventData, 1);
        fed->obj = atk_obj;
        fed->state = FALSE;
        RunOnMainThread(emit_focus_event, fed);

        StateChangeData *scd = g_new0(StateChangeData, 1);
        scd->obj = atk_obj;
        scd->name = g_strdup("focused");
        scd->state = FALSE;
        RunOnMainThread(emit_state_change, scd);
    }

    g_object_unref(state_set);
}

/* Respond to <CreateNotify> events. */
static void TkAtkAccessible_CreateHandler(ClientData clientData, XEvent *eventPtr) 
{
	
    if (eventPtr->type != CreateNotify) return;

    TkAtkAccessible *acc = (TkAtkAccessible *)clientData;
    if (!acc || !acc->tkwin || !acc->interp) {
	g_warning("TkAtkAccessible_CreateHandler: Invalid or null acc/tkwin/interp");
	return;
    }

    /* Update children cache asynchronously. */
    RunOnMainThread((GSourceFunc)UpdateChildrenCache, acc);
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
    
    MainThreadData data = {NULL, ip, NULL, NULL, NULL};
    Tk_Window path_tkwin = (Tk_Window)RunOnMainThread(name_to_window_main, &data);
    if (path_tkwin == NULL) {
	return TCL_OK;
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
    
    RunOnMainThread(emit_value_changed, vcd);

    if (role == ATK_ROLE_TEXT || role == ATK_ROLE_ENTRY) {
        RunOnMainThread(emit_text_selection_changed, acc);
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
    (void)clientData;
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "window");
        return TCL_ERROR;
    }

    MainThreadData data = {NULL, interp, NULL, NULL, NULL};
    Tk_Window path_tkwin = (Tk_Window)RunOnMainThread(name_to_window_main, &data);
    if (path_tkwin == NULL) {
	return TCL_OK;
    }

    AtkObject *acc = GetAtkObjectForTkWindow(path_tkwin);
    if (!acc) {
        acc = TkCreateAccessibleAtkObject(interp, path_tkwin, Tcl_GetString(objv[1]));
        if (!acc) {
	    g_warning("Failed to create accessible object\n");
            return TCL_OK;
        }
        
        RegisterAtkObjectForTkWindow(path_tkwin, acc);

        if ((gboolean)(uintptr_t)RunOnMainThread(is_toplevel_main, &(MainThreadData){path_tkwin, interp, NULL, NULL, NULL})) {
            RegisterToplevelWindow(interp, path_tkwin, acc);
        } else {
            Tk_Window parent_tkwin = (Tk_Window)RunOnMainThread(get_window_parent_main, &(MainThreadData){path_tkwin, interp, NULL, NULL, NULL});
            AtkObject *parent_obj = parent_tkwin ? GetAtkObjectForTkWindow(parent_tkwin) : tk_root_accessible;
            if (parent_obj) {
                atk_object_set_parent(acc, parent_obj);
                RunOnMainThread(emit_children_changed_add, acc);
            }
        }
    }

    TkAtkAccessible *tk_acc = (TkAtkAccessible *)acc;
    UpdateStateCache(tk_acc);

    FocusEventData *fed = g_new0(FocusEventData, 1);
    fed->obj = acc;
    fed->state = TRUE;
    RunOnMainThread(emit_focus_event, fed);

    StateChangeData *scd = g_new0(StateChangeData, 1);
    scd->obj = acc;
    scd->name = g_strdup("focused");
    scd->state = TRUE;
    RunOnMainThread(emit_state_change, scd);

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

    MainThreadData data = {NULL, interp, NULL, windowName, NULL};
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
    if ((gboolean)(uintptr_t)RunOnMainThread(is_toplevel_main, &(MainThreadData){tkwin, interp, NULL, NULL, NULL})) {
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
    /* Initialize AT-SPI bridge. */
    if (atk_bridge_adaptor_init(NULL, NULL) != 0) {
        Tcl_SetResult(interp, "Failed to initialize AT-SPI bridge", TCL_STATIC);
        return TCL_ERROR;
    }
    
    /* Get and initialize root accessible. */
    tk_root_accessible = atk_get_root();
    if (!tk_root_accessible || !G_IS_OBJECT(tk_root_accessible)) {
        Tcl_SetResult(interp, "Failed to get root accessible", TCL_STATIC);
        return TCL_ERROR;
    }
    tk_set_name(tk_root_accessible, "Tk Application");
    atk_object_set_role(tk_root_accessible, ATK_ROLE_APPLICATION);
    atk_object_set_description(tk_root_accessible, "Tk Application");
    AtkStateSet *state_set = atk_object_ref_state_set(tk_root_accessible);
    if (state_set != NULL) {
	atk_state_set_add_state(state_set, ATK_STATE_VISIBLE);
	atk_state_set_add_state(state_set, ATK_STATE_SHOWING);
	atk_state_set_add_state(state_set, ATK_STATE_ENABLED);
	atk_object_notify_state_change(tk_root_accessible, ATK_STATE_VISIBLE, TRUE);
	atk_object_notify_state_change(tk_root_accessible, ATK_STATE_SHOWING, TRUE);
	atk_object_notify_state_change(tk_root_accessible, ATK_STATE_ENABLED, TRUE);
	g_object_unref(state_set);
    }

    InitAtkTkMapping();
    InitRecursionTracking();

    /* Initialize main window. */
    Tk_Window mainWin = Tk_MainWindow(interp);
    if (!mainWin) {
        Tcl_SetResult(interp, "Failed to get main window", TCL_STATIC);
        return TCL_ERROR;
    }

    Tk_MakeWindowExist(mainWin);
    Tk_MapWindow(mainWin);

    /* Process Tcl events. */
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
    AddChildToParent((TkAtkAccessible *)tk_root_accessible, main_acc);
    ChildrenChangedAddData *cad = g_new0(ChildrenChangedAddData, 1);
    cad->parent = tk_root_accessible;
    cad->index = g_list_length(((TkAtkAccessible *)tk_root_accessible)->children) - 1;
    cad->child = main_acc;
    if (cad->parent) g_object_ref(cad->parent);
    if (cad->child) g_object_ref(cad->child);
    g_debug("TkAtkAccessibility_Init: Emitting children-changed::add for main window, parent=%p, index=%d",
            tk_root_accessible, cad->index);
    RunOnMainThread(emit_children_changed_add, cad);
    
    if (!g_list_find(toplevel_accessible_objects, main_acc)) {
        toplevel_accessible_objects = g_list_append(toplevel_accessible_objects, main_acc);
    }
    
    RegisterChildWidgets(interp, mainWin, main_acc);

    SetupGlibIntegration();
    Tcl_DoWhenIdle(ProcessPendingEvents, NULL);

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

