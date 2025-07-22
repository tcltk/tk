/*
 * tkUnixAccessibility.c --
 *
 *	This file implements accessibility/screen-reader support
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

/* Data declarations used in this file. */

typedef struct _TkAtkAccessible {
    AtkObject parent;
    Tk_Window tkwin;
    Tcl_Interp *interp;
    char *path;
    gchar *cached_name;
    GPtrArray *cached_children;
    gboolean cache_dirty;
} TkAtkAccessible;

typedef struct _TkAtkAccessibleClass {
    AtkObjectClass parent_class;
} TkAtkAccessibleClass;

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
    {"Radiobutton", ATK_ROLE_RADIO_BUTTON},
    {"Scale", ATK_ROLE_SLIDER},
    {"Spinbox", ATK_ROLE_SPIN_BUTTON},
    {"Table", ATK_ROLE_TABLE},
    {"Text", ATK_ROLE_TEXT},
    {"Toplevel", ATK_ROLE_WINDOW},
    {"Frame", ATK_ROLE_PANEL},
    {NULL, 0}
};

typedef struct {
    void (*func)(void *);
    void *data;
} DispatcherJob;

/* Hash table for managing accessibility attributes. */
extern Tcl_HashTable *TkAccessibilityObject;

/* Define custom ATK object bridged to Tcl/Tk. */
#define TK_ATK_TYPE_ACCESSIBLE (tk_atk_accessible_get_type())
G_DEFINE_TYPE_WITH_CODE(TkAtkAccessible, tk_atk_accessible, ATK_TYPE_OBJECT,
            G_IMPLEMENT_INTERFACE(ATK_TYPE_COMPONENT, tk_atk_component_interface_init)
            G_IMPLEMENT_INTERFACE(ATK_TYPE_ACTION, tk_atk_action_interface_init)
            G_IMPLEMENT_INTERFACE(ATK_TYPE_VALUE, tk_atk_value_interface_init))

/* Structs for thread-safe operation data. */
typedef struct {
    GList **list;
    gpointer data;
} GListRemoveData;

typedef struct {
    AtkComponent *component;
    gint *x;
    gint *y;
    gint *width;
    gint *height;
    AtkCoordType coord_type;
} ExtentsData;

typedef struct {
    AtkObject *obj;
    gint result;
} NChildrenData;

typedef struct {
    AtkObject *obj;
    guint i;
    AtkObject *result;
} RefChildData;

typedef struct {
    Tk_Window win;
    AtkRole result;
} RoleData;

typedef struct {
    AtkObject *obj;
    gchar *result;
} NameData;

typedef struct {
    AtkObject *obj;
    const gchar *name;
} SetNameData;

typedef struct {
    AtkValue *obj;
    GValue *value;
} ValueData;

typedef struct {
    AtkObject *obj;
    AtkStateSet *result;
} StateSetData;

typedef struct {
    AtkAction *action;
    gint i;
    gboolean result;
} ActionData;

typedef struct {
    AtkAction *action;
    gint i;
    const gchar *result;
} ActionNameData;

typedef struct {
    Tcl_Interp *interp;
    Tk_Window tkwin;
    AtkObject *accessible;
} RegisterToplevelData;

typedef struct {
    Tcl_Interp *interp;
    Tk_Window tkwin;
    AtkObject *parent_obj;
} RegisterChildData;

typedef struct {
    TkAtkAccessible *acc;
} UpdateCacheData;

typedef struct {
    Tcl_Interp *interp;
    Tk_Window tkwin;
    const char *path;
    AtkObject *result;
} CreateObjectData;

typedef struct {
    Tk_Window tkwin;
    AtkObject *atkobj;
} RegisterObjectData;

typedef struct {
    Tk_Window tkwin;
} UnregisterObjectData;

typedef struct {
    AtkObject *obj;
    const gchar *signal_name;
    gpointer data;
} SignalData;

typedef struct {
    Tk_Window tkwin;
    Tk_Window targetChild;
    int result;
} ChildIndexData;

typedef struct {
    Tk_Window tkwin;
    void *tkAccessible;
} RegisterHandlersData;

typedef struct {
    ClientData clientData;
    XEvent *eventPtr;
} EventHandlerData;

typedef struct {
    Tcl_Interp *interp;
    int objc;
    Tcl_Obj *const *objv;
    int result;
} CommandData;

static AtkObject *tk_root_accessible = NULL;
static GList *toplevel_accessible_objects = NULL;
static GHashTable *tk_to_atk_map = NULL;

/* Forward declarations of thread-safe functions. */
static void ThreadSafe_GetExtents(gpointer data);
static void ThreadSafe_GetNChildren(gpointer data);
static void ThreadSafe_RefChild(gpointer data);
static void ThreadSafe_GetRole(gpointer data);
static void ThreadSafe_GetName(gpointer data);
static void ThreadSafe_SetName(gpointer data);
static void ThreadSafe_GetDescription(gpointer data);
static void ThreadSafe_GetCurrentValue(gpointer data);
static void ThreadSafe_RefStateSet(gpointer data);
static void ThreadSafe_DoAction(gpointer data);
static void ThreadSafe_GetNActions(gpointer data);
static void ThreadSafe_GetActionName(gpointer data);
static void ThreadSafe_RegisterToplevelWindow(gpointer data);
static void ThreadSafe_RegisterChildWidgets(gpointer data);
static void ThreadSafe_UpdateAtkChildrenCache(gpointer data);
static void ThreadSafe_CreateAccessibleAtkObject(gpointer data);
static void ThreadSafe_RegisterAtkObject(gpointer data);
static void ThreadSafe_UnregisterAtkObject(gpointer data);
static void ThreadSafe_EmitSignal(gpointer data);
static void ThreadSafe_GetChildIndex(gpointer data);
static void ThreadSafe_RegisterEventHandlers(gpointer data);
static void ThreadSafe_DestroyHandler(gpointer data);
static void ThreadSafe_NameHandler(gpointer data);
static void ThreadSafe_ConfigureHandler(gpointer data);
static void ThreadSafe_EmitSelectionChanged(gpointer data);
static void ThreadSafe_EmitFocusChanged(gpointer data);
static void ThreadSafe_CheckScreenReader(gpointer data);
static void ThreadSafe_AccessibleObjCmd(gpointer data);
static void ThreadSafe_GListRemove(gpointer user_data);

/* Implementation functions of actual Tk-Atk integration. */
static void tk_atk_component_interface_init(AtkComponentIface *iface);
static void tk_atk_action_interface_init(AtkActionIface *iface);
static void tk_atk_value_interface_init(AtkValueIface *iface);
static void tk_atk_accessible_init(TkAtkAccessible *self);
static void tk_atk_accessible_finalize(GObject *gobject);
static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass);
static gboolean tk_contains(AtkComponent *component, gint x, gint y, AtkCoordType coord_type);
static void tk_get_extents(AtkComponent *component, gint *x, gint *y, gint *width, gint *height, AtkCoordType coord_type);
static AtkRole GetAtkRoleForWidget_core(Tk_Window win);
static const gchar *tk_get_name_core(AtkObject *obj);
static void tk_set_name_core(AtkObject *obj, const gchar *name);
static const gchar *tk_get_description_core(AtkObject *obj);
static void tk_get_current_value_core(AtkValue *obj, GValue *value);
static AtkStateSet *tk_ref_state_set_core(AtkObject *obj);
static gboolean tk_action_do_action_core(AtkAction *action, gint i);
static gint tk_action_get_n_actions_core(AtkAction *action);
static const gchar *tk_action_get_name_core(AtkAction *action, gint i);
static void RegisterChildWidgets_core(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *parent_obj);
static void RegisterToplevelWindow_core(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *accessible);
static int GetAccessibleChildIndexFromTkList_core(Tk_Window parent, Tk_Window targetChild);
static void UpdateAtkChildrenCache_core(TkAtkAccessible *acc);
static AtkObject *TkCreateAccessibleAtkObject_core(Tcl_Interp *interp, Tk_Window tkwin, const char *path);
static void RegisterAtkObjectForTkWindow_core(Tk_Window tkwin, AtkObject *atkobj);
static AtkObject *GetAtkObjectForTkWindow_core(Tk_Window tkwin);
static void UnregisterAtkObjectForTkWindow_core(Tk_Window tkwin);
static void TkAtkAccessible_RegisterEventHandlers_core(Tk_Window tkwin, void *tkAccessible);
static void TkAtkAccessible_DestroyHandler_core(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_NameHandler_core(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_ConfigureHandler_core(ClientData clientData, XEvent *eventPtr);
static int EmitSelectionChanged_core(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int EmitFocusChanged_core(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int IsScreenReaderRunning_core(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int TkAtkAccessibleObjCmd_core(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

/*
 *----------------------------------------------------------------------
 *
 * Core ATK interface implementations. These route calls to the primary 
 * Tk implementation functions through thread-safe wrappers.
 *
 *----------------------------------------------------------------------
 */

/*
 * Functions to initialize and manage the parent Atk class and object instances.
 */

static void tk_atk_accessible_init(TkAtkAccessible *self)
{
    self->tkwin = NULL;
    self->interp = NULL;
    self->path = NULL;
    self->cached_name = NULL;
    self->cached_children = NULL;
    self->cache_dirty = FALSE;
}

static void tk_atk_accessible_finalize(GObject *gobject)
{
    TkAtkAccessible *self = (TkAtkAccessible*)gobject;

    if (self->tkwin) { 
        if (Tk_IsTopLevel(self->tkwin)) {
            /* Remove from toplevel list - must be done on main thread */
            GListRemoveData *remove_data = g_new(GListRemoveData, 1);
            remove_data->list = &toplevel_accessible_objects;
            remove_data->data = self;
            RunOnMainThread(ThreadSafe_GListRemove, remove_data);
        }
        
        /* Unregister from the Tk_Window to AtkObject map - must be done on main thread. */
        UnregisterObjectData *unreg_data = g_new(UnregisterObjectData, 1);
        unreg_data->tkwin = self->tkwin;
        RunOnMainThread(ThreadSafe_UnregisterAtkObject, unreg_data);
    }

    /* Free local resources. */
    g_free(self->path);
    g_free(self->cached_name);
    
    if (self->cached_children) {
        g_ptr_array_free(self->cached_children, TRUE);
    }

    /* Call parent finalize last. */
    G_OBJECT_CLASS(tk_atk_accessible_parent_class)->finalize(gobject);
}

/* Initialize ATK object class. */
static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    AtkObjectClass *atk_class = ATK_OBJECT_CLASS(klass);

    gobject_class->finalize = tk_atk_accessible_finalize;

    /* Map Atk class functions to thread-safe Tk functions */
    atk_class->get_name = tk_get_name;
    atk_class->get_description = tk_get_description;
    atk_class->get_role = tk_get_role;
    atk_class->ref_state_set = tk_ref_state_set;
    atk_class->get_n_children = tk_get_n_children;
    atk_class->ref_child = tk_ref_child;
}

/*
 * Functions to map ATK component interface to Tk.
 */
static void tk_get_extents(AtkComponent *component, gint *x, gint *y, gint *width, gint *height, AtkCoordType coord_type) {
    ExtentsData *data = g_new(ExtentsData, 1);
    data->component = component;
    data->x = x;
    data->y = y;
    data->width = width;
    data->height = height;
    data->coord_type = coord_type;
    RunOnMainThread(ThreadSafe_GetExtents, data);
}

static gboolean tk_contains(AtkComponent *component, gint x, gint y, AtkCoordType coord_type) {
    gint comp_x, comp_y, comp_width, comp_height;
    if (!component) return FALSE;
    
    ExtentsData extents_data = {component, &comp_x, &comp_y, &comp_width, &comp_height, coord_type};
    ThreadSafe_GetExtents(&extents_data);
    
    return (x >= comp_x && x < comp_x + comp_width &&
            y >= comp_y && y < comp_y + comp_height);
}

static void tk_atk_component_interface_init(AtkComponentIface *iface) {
    iface->get_extents = tk_get_extents;
    iface->contains = tk_contains;
}

/*
 * Functions to manage child count and individual child widgets.
 */
static gint tk_get_n_children(AtkObject *obj) {
    NChildrenData *data = g_new(NChildrenData, 1);
    data->obj = obj;
    RunOnMainThread(ThreadSafe_GetNChildren, data);
    return data->result;
}

static AtkObject *tk_ref_child(AtkObject *obj, guint i) {
    RefChildData *data = g_new(RefChildData, 1);
    data->obj = obj;
    data->i = i;
    RunOnMainThread(ThreadSafe_RefChild, data);
    return data->result;
}

/*
 * Functions to map accessible role to ATK.
 */
static AtkRole tk_get_role(AtkObject *obj) {
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    RoleData *data = g_new(RoleData, 1);
    data->win = acc->tkwin;
    RunOnMainThread(ThreadSafe_GetRole, data);
    return data->result;
}

/*
 * Name and description getters
 * for Tk-ATK objects.
 */
static const gchar *tk_get_name(AtkObject *obj) {
    NameData *data = g_new(NameData, 1);
    data->obj = obj;
    RunOnMainThread(ThreadSafe_GetName, data);
    return data->result;
}

static void tk_set_name(AtkObject *obj, const gchar *name) {
    SetNameData *data = g_new(SetNameData, 1);
    data->obj = obj;
    data->name = name;
    RunOnMainThread(ThreadSafe_SetName, data);
}

static const gchar *tk_get_description(AtkObject *obj) {
    NameData *data = g_new(NameData, 1);
    data->obj = obj;
    RunOnMainThread(ThreadSafe_GetDescription, data);
    return data->result;
}

/*
 * Functions to map accessible value to ATK using
 * AtkValue interface.
 */
static void tk_get_current_value(AtkValue *obj, GValue *value) {
    ValueData *data = g_new(ValueData, 1);
    data->obj = obj;
    data->value = value;
    RunOnMainThread(ThreadSafe_GetCurrentValue, data);
}

static void tk_atk_value_interface_init(AtkValueIface *iface) {
    iface->get_current_value = tk_get_current_value;
}

/* Function to map accessible state to ATK. */
static AtkStateSet *tk_ref_state_set(AtkObject *obj) {
    StateSetData *data = g_new(StateSetData, 1);
    data->obj = obj;
    RunOnMainThread(ThreadSafe_RefStateSet, data);
    return data->result;
}

/*
 * Functions that implement actions (i.e. button press)
 * from Tk to ATK.
 */
static gboolean tk_action_do_action(AtkAction *action, gint i) {
    ActionData *data = g_new(ActionData, 1);
    data->action = action;
    data->i = i;
    RunOnMainThread(ThreadSafe_DoAction, data);
    return data->result;
}

static gint tk_action_get_n_actions(AtkAction *action) {
    NChildrenData *data = g_new(NChildrenData, 1);
    data->obj = ATK_OBJECT(action);
    RunOnMainThread(ThreadSafe_GetNActions, data);
    return data->result;
}

static const gchar *tk_action_get_name(AtkAction *action, gint i) {
    ActionNameData *data = g_new(ActionNameData, 1);
    data->action = action;
    data->i = i;
    RunOnMainThread(ThreadSafe_GetActionName, data);
    return data->result;
}

static void tk_atk_action_interface_init(AtkActionIface *iface) {
    iface->do_action = tk_action_do_action;
    iface->get_n_actions = tk_action_get_n_actions;
    iface->get_name = tk_action_get_name;
}

/*
 *----------------------------------------------------------------------
 *
 * Thread-safe wrappers that bridge the ATK-Tk interface. 
 *
 *----------------------------------------------------------------------
 */


/* Get accessible frame. */
static void ThreadSafe_GetExtents(gpointer user_data) {
    ExtentsData *data = (ExtentsData *)user_data;
    TkAtkAccessible *acc = (TkAtkAccessible *)data->component;

    if (!acc || !acc->tkwin) {
        *(data->x) = *(data->y) = *(data->width) = *(data->height) = 0;
        g_free(data);
        return;
    }

    *(data->x) = Tk_X(acc->tkwin);
    *(data->y) = Tk_Y(acc->tkwin);
    *(data->width) = Tk_Width(acc->tkwin);
    *(data->height) = Tk_Height(acc->tkwin);

    if (data->coord_type == ATK_XY_SCREEN) {
        int root_x, root_y;
        Tk_GetRootCoords(acc->tkwin, &root_x, &root_y);
        *(data->x) = root_x;
        *(data->y) = root_y;
    }
    g_free(data);
}

/* Get number of accessible children. */
static void ThreadSafe_GetNChildren(gpointer user_data) {
    NChildrenData *data = (NChildrenData *)user_data;
    TkAtkAccessible *acc = (TkAtkAccessible *)data->obj;
    if (!acc) {
        data->result = 0;
        g_free(data);
        return;
    }

    if (data->obj == tk_root_accessible) {
        data->result = g_list_length(toplevel_accessible_objects);
        g_free(data);
        return;
    }

    if (acc->cache_dirty) {
        UpdateCacheData *cache_data = g_new(UpdateCacheData, 1);
        cache_data->acc = acc;
        RunOnMainThread(ThreadSafe_UpdateAtkChildrenCache, cache_data);
        acc->cache_dirty = FALSE;
    }

    data->result = acc->cached_children ? acc->cached_children->len : 0;
    g_free(data);
}

/* Get accessible child widget. */
static void ThreadSafe_RefChild(gpointer user_data) {
    RefChildData *data = (RefChildData *)user_data;
    TkAtkAccessible *acc = (TkAtkAccessible *)data->obj;
    if (!acc) {
        data->result = NULL;
        g_free(data);
        return;
    }

    if (data->obj == tk_root_accessible) {
        if (data->i >= g_list_length(toplevel_accessible_objects)) {
            data->result = NULL;
            g_free(data);
            return;
        }
        AtkObject *child = g_list_nth_data(toplevel_accessible_objects, data->i);
        if (child) g_object_ref(child);
        data->result = child;
        g_free(data);
        return;
    }

    if (acc->cache_dirty) {
        UpdateCacheData *cache_data = g_new(UpdateCacheData, 1);
        cache_data->acc = acc;
        RunOnMainThread(ThreadSafe_UpdateAtkChildrenCache, cache_data);
        acc->cache_dirty = FALSE;
    }

    if (acc->cached_children && data->i < acc->cached_children->len) {
        AtkObject *child = g_ptr_array_index(acc->cached_children, data->i);
        g_object_ref(child);
        data->result = child;
    } else {
        data->result = NULL;
    }
    g_free(data);
}

/* Get accessible role. */
static void ThreadSafe_GetRole(gpointer user_data) {
    RoleData *data = (RoleData *)user_data;
    data->result = GetAtkRoleForWidget_core(data->win);
    g_free(data);
}

/* Get accessible name. */
static void ThreadSafe_GetName(gpointer user_data) {
    NameData *data = (NameData *)user_data;
    data->result = tk_get_name_core(data->obj);
    g_free(data);
}

/* Set accessible name. */
static void ThreadSafe_SetName(gpointer user_data) {
    SetNameData *data = (SetNameData *)user_data;
    tk_set_name_core(data->obj, data->name);
    g_free(data);
}

/* Get accessible escription. */
static void ThreadSafe_GetDescription(gpointer user_data) {
    NameData *data = (NameData *)user_data;
    data->result = tk_get_description_core(data->obj);
    g_free(data);
}

/* Get accessible value. */
static void ThreadSafe_GetCurrentValue(gpointer user_data) {
    ValueData *data = (ValueData *)user_data;
    tk_get_current_value_core(data->obj, data->value);
    g_free(data);
}

/* Get accessible state. */
static void ThreadSafe_RefStateSet(gpointer user_data) {
    StateSetData *data = (StateSetData *)user_data;
    data->result = tk_ref_state_set_core(data->obj);
    g_free(data);
}

/* Process widget action. */
static void ThreadSafe_DoAction(gpointer user_data) {
    ActionData *data = (ActionData *)user_data;
    data->result = tk_action_do_action_core(data->action, data->i);
    g_free(data);
}

/* Get number of available actions. */
static void ThreadSafe_GetNActions(gpointer user_data) {
    NChildrenData *data = (NChildrenData *)user_data;
    data->result = tk_action_get_n_actions_core(ATK_ACTION(data->obj));
    g_free(data);
}

/* Get action names. */
static void ThreadSafe_GetActionName(gpointer user_data) {
    ActionNameData *data = (ActionNameData *)user_data;
    data->result = tk_action_get_name_core(data->action, data->i);
    g_free(data);
}

/* Register toplevel window with ATK object. */
static void ThreadSafe_RegisterToplevelWindow(gpointer user_data) {
    RegisterToplevelData *data = (RegisterToplevelData *)user_data;
    RegisterToplevelWindow_core(data->interp, data->tkwin, data->accessible);
    g_free(data);
}

/* Register child widgets. */
static void ThreadSafe_RegisterChildWidgets(gpointer user_data) {
    RegisterChildData *data = (RegisterChildData *)user_data;
    RegisterChildWidgets_core(data->interp, data->tkwin, data->parent_obj);
    g_free(data);
}

/* Rebuild tree of child widgets. */
static void ThreadSafe_UpdateAtkChildrenCache(gpointer user_data) {
    UpdateCacheData *data = (UpdateCacheData *)user_data;
    UpdateAtkChildrenCache_core(data->acc);
    g_free(data);
}

/* Create accessible ATK object. */
static void ThreadSafe_CreateAccessibleAtkObject(gpointer user_data) {
    CreateObjectData *data = (CreateObjectData *)user_data;
    data->result = TkCreateAccessibleAtkObject_core(data->interp, data->tkwin, data->path);
    g_free(data);
}

/* Register ATK object for window. */
static void ThreadSafe_RegisterAtkObject(gpointer user_data) {
    RegisterObjectData *data = (RegisterObjectData *)user_data;
    RegisterAtkObjectForTkWindow_core(data->tkwin, data->atkobj);
    g_free(data);
}
/* Unregister Atk object.*/
static void ThreadSafe_UnregisterAtkObject(gpointer user_data) {
    UnregisterObjectData *data = (UnregisterObjectData *)user_data;
    UnregisterAtkObjectForTkWindow_core(data->tkwin);
    g_free(data);
}

/* Emit signal / ATK notification. */
static void ThreadSafe_EmitSignal(gpointer user_data) {
    SignalData *data = (SignalData *)user_data;
    g_signal_emit_by_name(data->obj, data->signal_name, data->data);
    g_free(data);
}

/* Get index of child widget in accessibility list. */
static void ThreadSafe_GetChildIndex(gpointer user_data) {
    ChildIndexData *data = (ChildIndexData *)user_data;
    data->result = GetAccessibleChildIndexFromTkList_core(data->tkwin, data->targetChild);
    g_free(data);
}

/* Register Tk event handlers. */
static void ThreadSafe_RegisterEventHandlers(gpointer user_data) {
    RegisterHandlersData *data = (RegisterHandlersData *)user_data;
    TkAtkAccessible_RegisterEventHandlers_core(data->tkwin, data->tkAccessible);
    g_free(data);
}

/* Tk event handler for widget destruction. */
static void ThreadSafe_DestroyHandler(gpointer user_data) {
    EventHandlerData *data = (EventHandlerData *)user_data;
    TkAtkAccessible_DestroyHandler_core(data->clientData, data->eventPtr);
    g_free(data);
}

/* Tk event handler for updating names. */
static void ThreadSafe_NameHandler(gpointer user_data) {
    EventHandlerData *data = (EventHandlerData *)user_data;
    TkAtkAccessible_NameHandler_core(data->clientData, data->eventPtr);
    g_free(data);
}

/* Tk event handler for widget update processes. */
static void ThreadSafe_ConfigureHandler(gpointer user_data) {
    EventHandlerData *data = (EventHandlerData *)user_data;
    TkAtkAccessible_ConfigureHandler_core(data->clientData, data->eventPtr);
    g_free(data);
}

/* Notify system that selection has changed. */
static void ThreadSafe_EmitSelectionChanged(gpointer user_data) {
    CommandData *data = (CommandData *)user_data;
    data->result = EmitSelectionChanged_core(data->clientData, data->interp, data->objc, data->objv);
    g_free(data);
}

/* Notify system that focus has changed. */
static void ThreadSafe_EmitFocusChanged(gpointer user_data) {
    CommandData *data = (CommandData *)user_data;
    data->result = EmitFocusChanged_core(data->clientData, data->interp, data->objc, data->objv);
    g_free(data);
}

/* Check if screen reader running. */
static void ThreadSafe_CheckScreenReader(gpointer user_data) {
    CommandData *data = (CommandData *)user_data;
    data->result = IsScreenReaderRunning_core(data->clientData, data->interp, data->objc, data->objv);
    g_free(data);
}

/* Core Tk command for accessible objects. */
static void ThreadSafe_AccessibleObjCmd(gpointer user_data) {
    CommandData *data = (CommandData *)user_data;
    data->result = TkAtkAccessibleObjCmd_core(data->clientData, data->interp, data->objc, data->objv);
    g_free(data);
}

/* Thread-safe GList removal. */
static void ThreadSafe_GListRemove(gpointer user_data)
{
    GListRemoveData *data = (GListRemoveData *)user_data;
    *(data->list) = g_list_remove(*(data->list), data->data);
    g_free(data);
}

/*
 *----------------------------------------------------------------------
 *
 * Core functions implementing the ATK-Tk interface. 
 *
 *----------------------------------------------------------------------
 */


/* Get widget role. */
static AtkRole GetAtkRoleForWidget_core(Tk_Window win) {
    if (!win) return ATK_ROLE_UNKNOWN;

    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
    AtkRole role = ATK_ROLE_UNKNOWN;

    hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)win);
    if (hPtr) {
        AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
        if (AccessibleAttributes) {
            hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "role");
            if (hPtr2) {
                char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
                if (result) {
                    for (int i = 0; roleMap[i].tkrole != NULL; i++) {
                        if (strcmp(roleMap[i].tkrole, result) == 0) {
                            role = roleMap[i].atkrole;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (Tk_IsTopLevel(win)) {
        role = ATK_ROLE_WINDOW;
    }
    return role;
}

/* Get widget name. */
static const gchar *tk_get_name_core(AtkObject *obj) {
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    gchar *ret = NULL;

    if (!acc) return NULL;

    if (obj == tk_root_accessible) {
        if (acc->cached_name) {
            return g_strdup(acc->cached_name);
        }
        return g_strdup("Tk Application");
    }

    if (!acc->tkwin || !acc->interp) {
        return NULL;
    }

    Tcl_DString cmd;
    Tcl_DStringInit(&cmd);

    if (GetAtkRoleForWidget_core(acc->tkwin) == ATK_ROLE_MENU) {
        Tcl_DStringAppend(&cmd, Tk_PathName(acc->tkwin), -1);
        Tcl_DStringAppend(&cmd, " entrycget active -label", -1);
        if (Tcl_Eval(acc->interp, Tcl_DStringValue(&cmd)) == TCL_OK) {
            const char *result = Tcl_GetStringResult(acc->interp);
            if (result && *result) {
                ret = g_strdup(result);
            }
        }
        Tcl_DStringFree(&cmd);
        if (ret) return ret;
    }

    if (GetAtkRoleForWidget_core(acc->tkwin) == ATK_ROLE_LABEL) {
        Tcl_DStringAppend(&cmd, Tk_PathName(acc->tkwin), -1);
        Tcl_DStringAppend(&cmd, " cget -text", -1);

        if (Tcl_Eval(acc->interp, Tcl_DStringValue(&cmd)) == TCL_OK) {
            const char *result = Tcl_GetStringResult(acc->interp);
            if (result && *result) {
                ret = g_strdup(result);
            }
        }
        Tcl_DStringFree(&cmd);
        if (ret) return ret;
    }

    if (Tk_IsTopLevel(acc->tkwin) && Tk_PathName(acc->tkwin)) {
        Tcl_DStringAppend(&cmd, "wm title ", -1);
        Tcl_DStringAppend(&cmd, Tk_PathName(acc->tkwin), -1);

        if (Tcl_Eval(acc->interp, Tcl_DStringValue(&cmd)) != TCL_OK) {
            ret = g_strdup(Tk_PathName(acc->tkwin));
        } else {
            const char *result = Tcl_GetStringResult(acc->interp);
            ret = g_strdup(result && *result ? result : Tk_PathName(acc->tkwin));
        }
        Tcl_DStringFree(&cmd);
        if (ret) return ret;
    }

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)acc->tkwin);
    if (hPtr) {
        Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
        if (AccessibleAttributes) {
            Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "name");
            if (hPtr2) {
                const char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
                if (result) {
                    return g_strdup(result);
                }
            }
        }
    }

    if (Tk_PathName(acc->tkwin)) {
        return g_strdup(Tk_PathName(acc->tkwin));
    }
    return NULL;
}

/* Set widget name. */
static void tk_set_name_core(AtkObject *obj, const gchar *name) {
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;

    if (!acc) return; 

    if (obj == tk_root_accessible) {
        g_free(acc->cached_name);
        acc->cached_name = g_strdup(name); 
    }
    atk_object_set_name(acc, name);
    g_object_notify(G_OBJECT(acc), "accessible-name");
}

/* Get widget description. */
static const gchar *tk_get_description_core(AtkObject *obj) {
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;

    if (!acc || !acc->tkwin) return NULL;

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)acc->tkwin);
    if (!hPtr) return NULL;

    Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
    if (!AccessibleAttributes) return NULL;

    Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "description");
    if (!hPtr2) return NULL;

    const char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    if (result) {
        return g_strdup(result);
    }
    return NULL;
}

/* Get current widget value. */
static void tk_get_current_value_core(AtkValue *obj, GValue *value) {
    AtkObject *atkObj = ATK_OBJECT(obj);
    TkAtkAccessible *acc = (TkAtkAccessible *)atkObj;

    if (!acc || !acc->tkwin) {
        g_value_init(value, G_TYPE_STRING);
        g_value_set_string(value, "");
        return;
    }

    Tk_Window win = acc->tkwin;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;

    hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)win);
    if (!hPtr) {
        g_value_init(value, G_TYPE_STRING);
        g_value_set_string(value, "");
        return;
    }

    AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
    if (!AccessibleAttributes) {
        g_value_init(value, G_TYPE_STRING);
        g_value_set_string(value, "");
        return;
    }

    hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "value");
    if (!hPtr2) {
        g_value_init(value, G_TYPE_STRING);
        g_value_set_string(value, "");
        return;
    }

    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    g_value_init(value, G_TYPE_STRING);
    g_value_set_string(value, result ? result : "");
}

/* Get widget state. */
static AtkStateSet *tk_ref_state_set_core(AtkObject *obj) {
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    AtkStateSet *set = atk_state_set_new();
    if (!acc || !acc->tkwin) return set; 

    atk_state_set_add_state(set, ATK_STATE_ENABLED);
    atk_state_set_add_state(set, ATK_STATE_SENSITIVE);
    if (GetAtkRoleForWidget_core(acc->tkwin) == ATK_ROLE_ENTRY) {
        atk_state_set_add_state(set, ATK_STATE_EDITABLE);
        atk_state_set_add_state(set, ATK_STATE_SINGLE_LINE);
    }
    if (Tk_IsMapped(acc->tkwin) || Tk_Width(acc->tkwin) > 0 || Tk_Height(acc->tkwin) > 0) {
        atk_state_set_add_state(set, ATK_STATE_VISIBLE);
        if (Tk_IsMapped(acc->tkwin)) {
            atk_state_set_add_state(set, ATK_STATE_SHOWING);
        }
        atk_state_set_add_state(set, ATK_STATE_FOCUSABLE);
    }
    return set;
}

/* Execute widget action . */
static gboolean tk_action_do_action_core(AtkAction *action, gint i) {
    TkAtkAccessible *acc = (TkAtkAccessible *)action;

    if (!acc || !acc->tkwin || !acc->interp) {
        return FALSE;
    }

    if (i == 0) {
        Tk_Window win = acc->tkwin;
        if (!win) {
            return FALSE;
        }

        Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)win);
        if (!hPtr) {
            return FALSE;
        }
        Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
        if (!AccessibleAttributes) {
            return FALSE;
        }
        Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "action");
        if (!hPtr2) {
            return FALSE;
        }
        const char *cmd = Tcl_GetString(Tcl_GetHashValue(hPtr2));
        if (!cmd) {
            return FALSE;
        }

        if ((Tcl_EvalEx(acc->interp, cmd, -1, TCL_EVAL_GLOBAL)) != TCL_OK) {
            return FALSE;
        }
        return TRUE;
    }
    return FALSE; 
}

/* Get number of available actions. */
static gint tk_action_get_n_actions_core(AtkAction *action) {
    (void) action;
    return 1;
}

/* Get action names. */
static const gchar *tk_action_get_name_core(AtkAction *action, gint i) {
    (void) action;
    if (i == 0) {
        return "click";
    }
    return NULL;
}

/* Function to recursively register child widgets using childList. */
static void RegisterChildWidgets_core(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *parent_obj) {
    if (!tkwin || !parent_obj) return;

    TkWindow *winPtr = (TkWindow *)tkwin;
    TkWindow *childPtr;

    for (childPtr = winPtr->childList; childPtr != NULL; childPtr = childPtr->nextPtr) {
        Tk_Window child = (Tk_Window)childPtr;
        if (!child || !Tk_WindowId(child)) continue;

        if (!Tk_IsMapped(child)) continue;

        AtkObject *child_obj = GetAtkObjectForTkWindow_core(child);
        if (!child_obj) {
            child_obj = TkCreateAccessibleAtkObject_core(interp, child, Tk_PathName(child));
            if (!child_obj) continue;

            RegisterAtkObjectForTkWindow_core(child, child_obj);
            RegisterHandlersData *handler_data = g_new(RegisterHandlersData, 1);
            handler_data->tkwin = child;
            handler_data->tkAccessible = (TkAtkAccessible *)child_obj;
            RunOnMainThread(ThreadSafe_RegisterEventHandlers, handler_data);
        }

        int index = GetAccessibleChildIndexFromTkList_core(tkwin, child);

        AtkObject *current_parent = atk_object_get_parent(child_obj);
        if (current_parent != parent_obj) {
            atk_object_set_parent(child_obj, parent_obj);
            SignalData *signal_data = g_new(SignalData, 1);
            signal_data->obj = parent_obj;
            signal_data->signal_name = "children-changed::add";
            signal_data->data = GINT_TO_POINTER(index);
            RunOnMainThread(ThreadSafe_EmitSignal, signal_data);
            
            SignalData *state_data1 = g_new(SignalData, 1);
            state_data1->obj = child_obj;
            state_data1->signal_name = "state-change";
            state_data1->data = (gpointer)"showing";
            RunOnMainThread(ThreadSafe_EmitSignal, state_data1);
            
            SignalData *state_data2 = g_new(SignalData, 1);
            state_data2->obj = child_obj;
            state_data2->signal_name = "state-change";
            state_data2->data = (gpointer)"visible";
            RunOnMainThread(ThreadSafe_EmitSignal, state_data2);
        }

        const gchar *child_name = tk_get_name_core(child_obj);
        if (child_name) {
            SetNameData *name_data = g_new(SetNameData, 1);
            name_data->obj = child_obj;
            name_data->name = child_name;
            RunOnMainThread(ThreadSafe_SetName, name_data);
            g_free((gpointer)child_name);
        }

        RegisterChildData *child_data = g_new(RegisterChildData, 1);
        child_data->interp = interp;
        child_data->tkwin = child;
        child_data->parent_obj = child_obj;
        RunOnMainThread(ThreadSafe_RegisterChildWidgets, child_data);
    }
}

/* Function to complete toplevel registration with proper hierarchy. */
static void RegisterToplevelWindow_core(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *accessible) {
    if (!accessible) return;

    if (!tk_root_accessible) {
        tk_root_accessible = tk_util_get_root();
    }

    atk_object_set_parent(accessible, tk_root_accessible);

    if (!g_list_find(toplevel_accessible_objects, accessible)) {
        toplevel_accessible_objects = g_list_append(toplevel_accessible_objects, accessible);

        int index = g_list_length(toplevel_accessible_objects) - 1;
        SignalData *signal_data = g_new(SignalData, 1);
        signal_data->obj = tk_root_accessible;
        signal_data->signal_name = "children-changed::add";
        signal_data->data = GINT_TO_POINTER(index);
        RunOnMainThread(ThreadSafe_EmitSignal, signal_data);
        
        SignalData *state_data1 = g_new(SignalData, 1);
        state_data1->obj = accessible;
        state_data1->signal_name = "state-change";
        state_data1->data = (gpointer)"showing";
        RunOnMainThread(ThreadSafe_EmitSignal, state_data1);
        
        SignalData *state_data2 = g_new(SignalData, 1);
        state_data2->obj = accessible;
        state_data2->signal_name = "state-change";
        state_data2->data = (gpointer)"visible";
        RunOnMainThread(ThreadSafe_EmitSignal, state_data2);
    }

    const gchar *name = tk_get_name_core(accessible);
    if (name) {
        SetNameData *name_data = g_new(SetNameData, 1);
        name_data->obj = accessible;
        name_data->name = name;
        RunOnMainThread(ThreadSafe_SetName, name_data);
        g_free((gpointer)name);
    }

    RegisterChildData *child_data = g_new(RegisterChildData, 1);
    child_data->interp = interp;
    child_data->tkwin = tkwin;
    child_data->parent_obj = accessible;
    RunOnMainThread(ThreadSafe_RegisterChildWidgets, child_data);
}

/* Helper function to calculate index from Tk window list. */
static int GetAccessibleChildIndexFromTkList_core(Tk_Window parent, Tk_Window targetChild) {
    if (!parent || !targetChild) return -1;

    TkWindow *winPtr = (TkWindow *)parent;
    TkWindow *childPtr;
    int index = 0;

    for (childPtr = winPtr->childList; childPtr != NULL; childPtr = childPtr->nextPtr) {
        if (!Tk_WindowId((Tk_Window)childPtr)) continue;

        AtkObject *acc = GetAtkObjectForTkWindow_core((Tk_Window)childPtr);
        if (acc) {
            if ((Tk_Window)childPtr == targetChild) {
                return index;
            }
            index++;
        }
    }

    return -1;
}

/* Cache list of accessible children and only update with Configure/Map events. */
static void UpdateAtkChildrenCache_core(TkAtkAccessible *acc) {
    if (!acc || !acc->tkwin) return;

    if (acc->cached_children) {
        g_ptr_array_free(acc->cached_children, TRUE);
    }
    acc->cached_children = g_ptr_array_new_with_free_func(g_object_unref);

    TkWindow *winPtr = (TkWindow *)acc->tkwin;
    for (TkWindow *child = winPtr->childList; child != NULL; child = child->nextPtr) {
        if (Tk_WindowId((Tk_Window)child)) {
            AtkObject *child_obj = GetAtkObjectForTkWindow_core((Tk_Window)child);
            if (child_obj) {
                g_object_ref(child_obj);
                g_ptr_array_add(acc->cached_children, child_obj);
            }
        }
    }
}

/*
 * Root window setup. These are the foundation of the
 * accessibility object system in Atk. atk_get_root() is the
 * critical link to at-spi - it is called by the Atk system
 * and at-spi bridge initialization will silently fail if this
 * function is not implemented. This API is confusing because
 * atk_get_root cannot be called directly in our functions, but
 * it still must be implemented if we are using a custom setup,
 * as we are here.
 */

static AtkObject *tk_util_get_root(void) {
    if (!tk_root_accessible) {
        TkAtkAccessible *acc = g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
        tk_root_accessible = ATK_OBJECT(acc);
        atk_object_initialize(tk_root_accessible, NULL);

        atk_object_set_role(tk_root_accessible, ATK_ROLE_APPLICATION);
        SetNameData *name_data = g_new(SetNameData, 1);
        name_data->obj = tk_root_accessible;
        name_data->name = "Tk Application";
        RunOnMainThread(ThreadSafe_SetName, name_data);
    }

    return tk_root_accessible;
}

AtkObject *atk_get_root(void) {
    return tk_util_get_root();
}

/* Atk-Tk object creation with proper parent relationship. */
static AtkObject *TkCreateAccessibleAtkObject_core(Tcl_Interp *interp, Tk_Window tkwin, const char *path) {
    if (!interp || !tkwin || !path) {
        g_warning("TkCreateAccessibleAtkObject: Invalid interp, tkwin, or path.");
        return NULL;
    }

    TkAtkAccessible *acc = g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
    acc->interp = interp;
    acc->tkwin = tkwin;
    acc->path = g_strdup(path); 

    AtkObject *obj = ATK_OBJECT(acc);
    atk_object_initialize(obj, NULL);
    atk_object_set_role(obj, GetAtkRoleForWidget_core(tkwin));

    const gchar *name = tk_get_name_core(obj);
    if (name) {
        SetNameData *name_data = g_new(SetNameData, 1);
        name_data->obj = obj;
        name_data->name = name;
        RunOnMainThread(ThreadSafe_SetName, name_data);
        g_free((gpointer)name);
    }

    if (tkwin) {
        Tk_Window parent_tkwin = Tk_Parent(tkwin);
        AtkObject *parent_obj = NULL;

        if (parent_tkwin) {
            parent_obj = GetAtkObjectForTkWindow_core(parent_tkwin);
        } else {
            parent_obj = tk_root_accessible;
        }

        if (parent_obj) {
            atk_object_set_parent(obj, parent_obj);

            int index = GetAccessibleChildIndexFromTkList_core(parent_tkwin ? parent_tkwin : NULL, tkwin);
            if (index < 0) {
                index = -1;
            }

            SignalData *signal_data = g_new(SignalData, 1);
            signal_data->obj = parent_obj;
            signal_data->signal_name = "children-changed::add";
            signal_data->data = GINT_TO_POINTER(index);
            RunOnMainThread(ThreadSafe_EmitSignal, signal_data);
        }
    }

    return obj;
}

*
* Functions to map Tk window to its corresponding Atk object.
*/

static void InitAtkTkMapping(void) {
    if (!tk_to_atk_map) {
        tk_to_atk_map = g_hash_table_new_full(g_direct_hash, g_direct_equal,
					      NULL, (GDestroyNotify)g_object_unref);
    }
}

static void RegisterAtkObjectForTkWindow_core(Tk_Window tkwin, AtkObject *atkobj) {
    if (!tkwin || !atkobj) return; 
    InitAtkTkMapping();
    g_object_ref(atkobj);
    g_hash_table_insert(tk_to_atk_map, tkwin, atkobj);
}

static AtkObject *GetAtkObjectForTkWindow_core(Tk_Window tkwin) {
    if (!tk_to_atk_map || !tkwin) return NULL; 
    return (AtkObject *)g_hash_table_lookup(tk_to_atk_map, tkwin);
}

static void UnregisterAtkObjectForTkWindow_core(Tk_Window tkwin) {
    if (tk_to_atk_map && tkwin) { 
        g_hash_table_remove(tk_to_atk_map, tkwin);
    }
}
/* Tk event handlers to signal updates to ATK. */
static void TkAtkAccessible_RegisterEventHandlers_core(Tk_Window tkwin, void *tkAccessible) {
    if (!tkwin || !tkAccessible) return; 
    Tk_CreateEventHandler(tkwin, StructureNotifyMask, TkAtkAccessible_DestroyHandler_core, tkAccessible);
    Tk_CreateEventHandler(tkwin, StructureNotifyMask, TkAtkAccessible_NameHandler_core, tkAccessible);
    Tk_CreateEventHandler(tkwin, StructureNotifyMask, TkAtkAccessible_ConfigureHandler_core, tkAccessible);
}

static void TkAtkAccessible_DestroyHandler_core(ClientData clientData, XEvent *eventPtr) {
    if (eventPtr->type == DestroyNotify) {
        TkAtkAccessible *tkAccessible = (TkAtkAccessible *)clientData;
        if (tkAccessible && tkAccessible->tkwin) {
            AtkObject *parent = atk_object_get_parent(ATK_OBJECT(tkAccessible));
            if (parent) {
                SignalData *signal_data = g_new(SignalData, 1);
                signal_data->obj = parent;
                signal_data->signal_name = "children-changed::remove";
                signal_data->data = GINT_TO_POINTER(-1);
                RunOnMainThread(ThreadSafe_EmitSignal, signal_data);
            }

            g_object_unref(tkAccessible);
        }
    }
}

static void TkAtkAccessible_NameHandler_core(ClientData clientData, XEvent *eventPtr) {
    if (eventPtr->type != ConfigureNotify) {
        return;
    }

    TkAtkAccessible *tkAccessible = (TkAtkAccessible *)clientData;
    if (!tkAccessible || !tkAccessible->tkwin) return;

    AtkObject *atk_obj = (AtkObject*) tkAccessible;
    if (atk_obj) {
        const gchar *name = tk_get_name_core(atk_obj);
        if (name) {
            SetNameData *name_data = g_new(SetNameData, 1);
            name_data->obj = atk_obj;
            name_data->name = name;
            RunOnMainThread(ThreadSafe_SetName, name_data);
            g_free((gpointer)name);
        }
    }
}

static void TkAtkAccessible_ConfigureHandler_core(ClientData clientData, XEvent *eventPtr) {
    if (!eventPtr) return;

    Tk_Window tkwin = (Tk_Window)clientData;
    if (!Tk_IsMapped(tkwin)) {
        return;
    }

    AtkObject *atkObj = GetAtkObjectForTkWindow_core(tkwin);
    if (!atkObj) {
        return;
    }

    TkAtkAccessible *acc = (TkAtkAccessible *)atkObj;

    switch (eventPtr->type) {
    case ConfigureNotify:
    case MapNotify:
    case VisibilityNotify:
        acc->cache_dirty = TRUE;
        break;
    default:
        break;
    }
}
/*
 *----------------------------------------------------------------------
 *
 * EmitSelectionChanged --
 *
 * Accessibility system notification when selection changed.
 *
 * Results:
 *	Accessibility system is made aware when a selection is changed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int EmitSelectionChanged_core(ClientData clientData, Tcl_Interp *ip, int objc, Tcl_Obj *const objv[]) {
    (void) clientData;

    if (objc < 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "window?");
        return TCL_ERROR;
    }
    
    Tk_Window path_tkwin; 
    path_tkwin = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
    if (path_tkwin == NULL) {
        Tcl_SetResult(ip, "Invalid window path", TCL_STATIC);
        return TCL_ERROR; 
    }

    AtkObject *acc = GetAtkObjectForTkWindow_core(path_tkwin);
    AtkRole role = GetAtkRoleForWidget_core(path_tkwin);

    if (!acc) {
        Tcl_SetResult(ip, "No accessible object for window", TCL_STATIC);
        return TCL_ERROR;
    }

    GValue gval = G_VALUE_INIT;
    tk_get_current_value_core(ATK_VALUE(acc), &gval);
    SignalData *signal_data = g_new(SignalData, 1);
    signal_data->obj = G_OBJECT(acc);
    signal_data->signal_name = "value-changed";
    signal_data->data = &gval;
    RunOnMainThread(ThreadSafe_EmitSignal, signal_data);
    g_value_unset(&gval);

    if (role == ATK_ROLE_TEXT || role == ATK_ROLE_ENTRY) {
        SignalData *text_signal = g_new(SignalData, 1);
        text_signal->obj = acc;
        text_signal->signal_name = "text-selection-changed";
        RunOnMainThread(ThreadSafe_EmitSignal, text_signal);
    }
   
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * EmitFocusChanged --
 *
 * Accessibility system notification when selection changed.
 *
 * Results:
 *	Accessibility system is made aware when a selection is changed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int EmitFocusChanged_core(ClientData clientData, Tcl_Interp *ip, int objc, Tcl_Obj *const objv[]) {
    (void)clientData;

    if (objc < 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "window?");
        return TCL_ERROR;
    }

    Tk_Window path_tkwin = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
    if (path_tkwin == NULL) {
        Tcl_SetResult(ip, "Invalid window path", TCL_STATIC);
        return TCL_ERROR;
    }

    AtkObject *acc = GetAtkObjectForTkWindow_core(path_tkwin);
    if (!acc) {
        Tcl_SetResult(ip, "No accessible object for window", TCL_STATIC);
        return TCL_ERROR;
    }

    SignalData *focus_signal = g_new(SignalData, 1);
    focus_signal->obj = G_OBJECT(acc);
    focus_signal->signal_name = "focus-event";
    focus_signal->data = GINT_TO_POINTER(TRUE);
    RunOnMainThread(ThreadSafe_EmitSignal, focus_signal);
    
    SignalData *state_signal = g_new(SignalData, 1);
    state_signal->obj = G_OBJECT(acc);
    state_signal->signal_name = "state-change";
    state_signal->data = (gpointer)"focused";
    RunOnMainThread(ThreadSafe_EmitSignal, state_signal);

    TkWindow *winPtr = (TkWindow *)path_tkwin;
    if (winPtr->parentPtr) {
        AtkObject *parent_acc = GetAtkObjectForTkWindow_core((Tk_Window)winPtr->parentPtr);
        if (parent_acc) {
            SignalData *child_signal = g_new(SignalData, 1);
            child_signal->obj = parent_acc;
            child_signal->signal_name = "children-changed";
            RunOnMainThread(ThreadSafe_EmitSignal, child_signal);
        }
    }

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
 *	Returns if screen reader is active or not.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int IsScreenReaderRunning_core(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    (void) clientData;
    (void) objc;
    (void) *objv;

    DBusError error;
    DBusConnection *connection;
    dbus_bool_t has_owner = FALSE; 
    bool result = false; 

    dbus_error_init(&error);

    connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (dbus_error_is_set(&error)) {
        g_warning("DBus connection error: %s", error.message);
        dbus_error_free(&error);
        goto cleanup;
    }

    if (!connection) {
        g_warning("Failed to get DBus connection.");
        goto cleanup;
    }

    has_owner = dbus_bus_name_has_owner(connection, "org.a11y.Bus", &error);
    if (dbus_error_is_set(&error)) {
        g_warning("DBus error checking org.a11y.Bus owner: %s", error.message);
        dbus_error_free(&error);
        goto cleanup;
    }
    if (!has_owner) {
        g_warning("org.a11y.Bus not owned.");
        goto cleanup;
    }

    has_owner = dbus_bus_name_has_owner(connection, "org.gnome.Orca", &error);
    if (dbus_error_is_set(&error)) {
        g_warning("DBus error checking org.gnome.Orca owner: %s", error.message);
        dbus_error_free(&error);
        goto cleanup;
    }
    if (has_owner) {
        result = true;
        goto cleanup;
    }

    has_owner = dbus_bus_name_has_owner(connection, "org.a11y.atspi.Registry", &error);
    if (dbus_error_is_set(&error)) {
        g_warning("DBus error checking org.a11y.atspi.Registry owner: %s", error.message);
        dbus_error_free(&error);
        goto cleanup;
    }
    if (has_owner) {
        result = true;
    }

 cleanup:
    if (connection) {
        dbus_connection_unref(connection);
    }

    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(result));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkAtkAccessibleObjCmd --
 *
 *	Main command for adding and managing accessibility objects to Tk
 * widgets on Linux using the Atk accessibility API.
 *
 * Results:
 *
 * A standard Tcl result.
 *
 * Side effects:
 *
 *	Tk widgets are now accessible to screen readers.
 *
 *----------------------------------------------------------------------
 */

static int TkAtkAccessibleObjCmd_core(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    (void)clientData;

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

    if (GetAtkObjectForTkWindow_core(tkwin)) {
        return TCL_OK;
    }

    CreateObjectData *create_data = g_new(CreateObjectData, 1);
    create_data->interp = interp;
    create_data->tkwin = tkwin;
    create_data->path = windowName;
    RunOnMainThread(ThreadSafe_CreateAccessibleAtkObject, create_data);
    AtkObject *accessible = create_data->result;

    if (accessible == NULL) {
        Tcl_SetResult(interp, "Failed to create accessible object.", TCL_STATIC);
        return TCL_ERROR;
    }

    RegisterHandlersData *handler_data = g_new(RegisterHandlersData, 1);
    handler_data->tkwin = tkwin;
    handler_data->tkAccessible = accessible;
    RunOnMainThread(ThreadSafe_RegisterEventHandlers, handler_data);

    if (Tk_IsTopLevel(tkwin)) {
        RegisterToplevelData *toplevel_data = g_new(RegisterToplevelData, 1);
        toplevel_data->interp = interp;
        toplevel_data->tkwin = tkwin;
        toplevel_data->accessible = accessible;
        RunOnMainThread(ThreadSafe_RegisterToplevelWindow, toplevel_data);
    }

    TkWindow *winPtr = (TkWindow *)tkwin;
    if (winPtr->parentPtr) {
        AtkObject *parent_acc = GetAtkObjectForTkWindow_core((Tk_Window)winPtr->parentPtr);
        if (parent_acc) {
            int index = 0;
            for (TkWindow *childPtr = winPtr->parentPtr->childList; childPtr != NULL; childPtr = childPtr->nextPtr) {
                if (childPtr == winPtr) {
                    break;
                }
                index++;
            }
            SignalData *signal_data = g_new(SignalData, 1);
            signal_data->obj = parent_acc;
            signal_data->signal_name = "children-changed";
            signal_data->data = GINT_TO_POINTER(0); /* CHILD_ADDED */
            RunOnMainThread(ThreadSafe_EmitSignal, signal_data);
        }
    }

    return TCL_OK;
}

static gboolean delayed_init(gpointer user_data) {
    Tcl_Interp *interp = (Tcl_Interp *)user_data; 

    if (!tk_root_accessible) return FALSE;

    SignalData *signal_data = g_new(SignalData, 1);
    signal_data->obj = tk_root_accessible;
    signal_data->signal_name = "children-changed";
    RunOnMainThread(ThreadSafe_EmitSignal, signal_data);

    SignalData *state_data1 = g_new(SignalData, 1);
    state_data1->obj = tk_root_accessible;
    state_data1->signal_name = "state-change";
    state_data1->data = (gpointer)"enabled";
    RunOnMainThread(ThreadSafe_EmitSignal, state_data1);

    SignalData *state_data2 = g_new(SignalData, 1);
    state_data2->obj = tk_root_accessible;
    state_data2->signal_name = "state-change";
    state_data2->data = (gpointer)"sensitive";
    RunOnMainThread(ThreadSafe_EmitSignal, state_data2);

    return FALSE;
}

/* 
 * Functions to support GLib / Tk event loop and threading 
 *  integration. 
 */
static void GtkEventLoop(ClientData clientData) {
    GMainContext *context = g_main_context_default();
    if (!context) return;

    /* Process pending events without blocking. */
    while (g_main_context_pending(context)) {
        g_main_context_iteration(context, FALSE);
    }

    /* Reschedule ourselves. */
    Tcl_CreateTimerHandler(10, GtkEventLoop, clientData);
}

void InstallGtkEventLoop(void) {
    GMainContext *context = g_main_context_default();
    if (!context) {
        g_warning("InstallGtkEventLoop: Failed to get default GLib main context");
        return;
    }

    Tcl_CreateTimerHandler(10, GtkEventLoop, context);
}

static gboolean RunOnMainThreadCallback(gpointer user_data) {
    DispatcherJob *job = (DispatcherJob *)user_data;
    if (job && job->func) {
        job->func(job->data);
    }
    g_free(job);
    return G_SOURCE_REMOVE;
}

void RunOnMainThread(void (*func)(void *), void *data) {
    DispatcherJob *job = g_new(DispatcherJob, 1);
    job->func = func;
    job->data = data;
    g_idle_add(RunOnMainThreadCallback, job);
}

#endif
/*
 *----------------------------------------------------------------------
 *
 * TkAtkAccessibility_Init --
 *
 *	Initializes the accessibility module.
 *
 * Results:
 *
 * A standard Tcl result.
 *
 * Side effects:
 *
 *	Accessibility module is now activated.
 *
 *----------------------------------------------------------------------
 */

#ifdef USE_ATK
int TkAtkAccessibility_Init(Tcl_Interp *interp) {
    g_setenv("GTK_MODULES", "gail:atk-bridge", FALSE);
    g_setenv("NO_AT_BRIDGE", "0", FALSE);

    g_type_init();

    if (atk_bridge_adaptor_init(NULL, NULL) != 0) {
        g_warning("Failed to initialize AT-SPI bridge\n");
        return TCL_ERROR;
    }

    tk_root_accessible = tk_util_get_root();
    if (tk_root_accessible) {
        const gchar *name = tk_get_name_core(tk_root_accessible);
        if (name) {
            SetNameData *name_data = g_new(SetNameData, 1);
            name_data->obj = tk_root_accessible;
            name_data->name = name;
            RunOnMainThread(ThreadSafe_SetName, name_data);
            g_free((gpointer)name);
        }
    }

    InitAtkTkMapping();

    Tk_Window mainWin = Tk_MainWindow(interp);
    if (mainWin) {
        CreateObjectData *create_data = g_new(CreateObjectData, 1);
        create_data->interp = interp;
        create_data->tkwin = mainWin;
        create_data->path = Tk_PathName(mainWin);
        RunOnMainThread(ThreadSafe_CreateAccessibleAtkObject, create_data);
        AtkObject *main_acc = create_data->result;
        
        if (main_acc) {
            RegisterToplevelData *toplevel_data = g_new(RegisterToplevelData, 1);
            toplevel_data->interp = interp;
            toplevel_data->tkwin = mainWin;
            toplevel_data->accessible = main_acc;
            RunOnMainThread(ThreadSafe_RegisterToplevelWindow, toplevel_data);
        }
    }

    int iterations = 0;
    while (g_main_context_pending(NULL) && iterations < 100) {
        iterations++;
    }

    InstallGtkEventLoop();

    CommandData *check_data = g_new(CommandData, 1);
    check_data->interp = interp;
    check_data->objc = 0;
    check_data->objv = NULL;
    RunOnMainThread(ThreadSafe_CheckScreenReader, check_data);
    int has_screen_reader = check_data->result;

    if (has_screen_reader) {
        g_timeout_add(500, (GSourceFunc)delayed_init, interp);
    } else {
        delayed_init(interp);
    }

    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object",
			 TkAtkAccessibleObjCmd_core, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change",
			 EmitSelectionChanged_core, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_focus_change",
			 EmitFocusChanged_core, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader",
			 IsScreenReaderRunning_core, NULL, NULL);

    SignalData *signal_data = g_new(SignalData, 1);
    signal_data->obj = tk_root_accessible;
    signal_data->signal_name = "children-changed";
    RunOnMainThread(ThreadSafe_EmitSignal, signal_data);

    return TCL_OK;
}

#else
/* No ATK found. */
int TkAtkAccessibility_Init(Tcl_Interp *interp) {
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
 * fill-column: 79
 * coding: utf-8
 * End:
 */
