/*
 * tkUnixAccessibility.c --
 *
 *	This file implements accessibility/screen-reader support 
 *      on Unix-like systems based on the Gnome Accessibility Toolkit, 
 *      the standard accessibility library for X11 systems. 
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

#ifdef USE_ATK
#include <atk/atk.h>
#include <atk-bridge.h> 
#include <dbus/dbus.h>
#include <glib.h>

/* Data declarations used in this file. */
typedef struct _TkAtkAccessible {
    AtkObject parent;
    Tk_Window tkwin;
    Tcl_Interp *interp;
    char *path;
} TkAtkAccessible;

typedef struct _TkAtkAccessibleClass {
    AtkObjectClass parent_class;
} TkAtkAccessibleClass;

static AtkObject *tk_root_accessible = NULL;
static GList *global_accessible_objects = NULL;
static GHashTable *tk_to_atk_map = NULL;
static GList *child_widgets = NULL; 

/* Atk/Tk glue functions. */
static void tk_get_extents(AtkComponent *component, gint *x, gint *y, gint *width, gint *height, AtkCoordType coord_type);
static gint tk_get_n_children(AtkObject *obj);
static AtkObject *tk_ref_child(AtkObject *obj, gint i);
static AtkRole GetAtkRoleForWidget(Tk_Window win);
static AtkRole tk_get_role(AtkObject *obj);
static const gchar *tk_get_name(AtkObject *obj);
static const gchar *tk_get_description(AtkObject *obj);
static const gchar *tk_get_description(AtkObject *obj);
static void tk_get_current_value(AtkValue *obj, GValue *value);
static AtkStateSet *tk_ref_state_set(AtkObject *obj);
static gboolean tk_action_do_action(AtkAction *action, gint i);
static gint tk_action_get_n_actions(AtkAction *action);
static const gchar *tk_action_get_name(AtkAction *action, gint i);
static void tk_atk_component_interface_init(AtkComponentIface *iface);
static void tk_atk_action_interface_init(AtkActionIface *iface);
static void tk_atk_value_interface_init(AtkValueIface *iface);
static gboolean tk_contains(AtkComponent *component, gint x, gint y, AtkCoordType coord_type);

/* Lower-level functions providing integration between Atk objects and Tcl/Tk. */
static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass);
static void tk_atk_accessible_init(TkAtkAccessible *accessible);
static void tk_atk_accessible_finalize(GObject *gobject);
AtkObject *TkCreateAccessibleAtkObject(Tcl_Interp *interp, Tk_Window tkwin, const char *path);
static void GtkEventLoop(ClientData clientData); 
void InstallGtkEventLoop(void);
void InitAtkTkMapping(void);
void RegisterAtkObjectForTkWindow(Tk_Window tkwin, AtkObject *atkobj);
AtkObject *GetAtkObjectForTkWindow(Tk_Window tkwin);
void UnregisterAtkObjectForTkWindow(Tk_Window tkwin);
static AtkObject *tk_util_get_root(void);

/* Script-level commands and helper functions. */
static int EmitSelectionChanged(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
void TkAtkAccessible_RegisterForCleanup(Tk_Window tkwin, void *tkAccessible);
static void TkAtkAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr);
int TkAtkAccessibleObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int TkAtkAccessibility_Init(Tcl_Interp *interp);

/*
 * Struct to map Tk roles into Atk roles.
 */

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
    {"Toplevel", ATK_ROLE_WINDOW},  
    {"Frame", ATK_ROLE_PANEL},     
    {NULL, 0}
};

/* Hash table for managing accessibility attributes. */
extern Tcl_HashTable *TkAccessibilityObject;

/* Variable for widget values. */
static GValue *tkvalue = NULL;

#define TK_ATK_TYPE_ACCESSIBLE (tk_atk_accessible_get_type())
G_DEFINE_TYPE_WITH_CODE(TkAtkAccessible, tk_atk_accessible, ATK_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(ATK_TYPE_COMPONENT, tk_atk_component_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_ACTION, tk_atk_action_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_VALUE, tk_atk_value_interface_init))

static AtkObjectClass *parent_class = NULL;

/* 
 * Map Atk component interface to Tk.
 */
static void tk_get_extents(AtkComponent *component, gint *x, gint *y,gint *width, gint *height, AtkCoordType coord_type)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)component;

    if (!acc->tkwin) {
	*x = *y = *width = *height = 0;
	return;
    }

    *x = Tk_X(acc->tkwin);
    *y = Tk_Y(acc->tkwin);
    *width = Tk_Width(acc->tkwin);
    *height = Tk_Height(acc->tkwin);

    /* Handle coordinate type conversion. */
    if (coord_type == ATK_XY_SCREEN) {
	int root_x, root_y;
	Tk_GetRootCoords(acc->tkwin, &root_x, &root_y);
	*x = root_x;
	*y = root_y;
    }
}

static gboolean tk_contains(AtkComponent *component, gint x, gint y, AtkCoordType coord_type)
{
    gint comp_x, comp_y, comp_width, comp_height;
    tk_get_extents(component, &comp_x, &comp_y, &comp_width, &comp_height, coord_type);


    return (x >= comp_x && x < comp_x + comp_width && 
	    y >= comp_y && y < comp_y + comp_height);
}

static void tk_atk_component_interface_init(AtkComponentIface *iface)
{
    iface->get_extents = tk_get_extents;
    iface->contains = tk_contains;
}


/* Limit children of widget. Only the toplevel should return children. */
static gint tk_get_n_children(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;

    /* Only the root window and toplevels should have children. */
    if (!acc->tkwin || obj == tk_root_accessible) {
	return g_list_length(child_widgets);
    }

    /* Check if this is a toplevel window. */
    if (Tk_IsTopLevel(acc->tkwin)) {
	return g_list_length(child_widgets);
    }

    return 0;
}


/* Limit children of widget. Only the toplevel should return children. */
static AtkObject *tk_ref_child(AtkObject *obj, gint i)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;


    if (!acc->tkwin && obj != tk_root_accessible) {
	return NULL;
    }

    if (i >= g_list_length(child_widgets)) {
	return NULL;
    }

    AtkObject *child = g_list_nth_data(child_widgets, i);
    if (child) {
	g_object_ref(child);
    }

    return child;
}

/* 
 * Functions to map accessible role to Atk.
 */

static AtkRole GetAtkRoleForWidget(Tk_Window win)
{
    if (!win) return ATK_ROLE_UNKNOWN;

    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
    AtkRole role = ATK_ROLE_UNKNOWN;

    /* Check if we have accessibility attributes */
    hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (hPtr) {
	AccessibleAttributes = Tcl_GetHashValue(hPtr);
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
    
    /* Special case for toplevel windows */
    if (Tk_IsTopLevel(win)) {
	role = ATK_ROLE_WINDOW;
    }
    return role;
}

static AtkRole tk_get_role(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    Tk_Window win = acc->tkwin;
    return GetAtkRoleForWidget(win);
}


/*
 * Name and description getters
 * for Tk-Atk objects. 
 */
 
static const gchar *tk_get_name(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;


    if (!acc->tkwin) return NULL;

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, acc->tkwin);
    if (!hPtr) return Tk_PathName(acc->tkwin); 

    Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
    Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "name");
    if (!hPtr2) return Tk_PathName(acc->tkwin);

    return Tcl_GetString(Tcl_GetHashValue(hPtr2));
}

static const gchar *tk_get_description(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;

    if (!acc->tkwin) return NULL;

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, acc->tkwin);
    if (!hPtr) return NULL;

    Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
    Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "description");
    if (!hPtr2) return NULL;

    return Tcl_GetString(Tcl_GetHashValue(hPtr2));

}

/* 
 * Functions to map accessible value to Atk using 
 * AtkValue interface. 

 */
static void tk_get_current_value(AtkValue *obj, GValue *value)
{
    AtkObject *atkObj = ATK_OBJECT(obj);
    TkAtkAccessible *acc = (TkAtkAccessible *)atkObj;

    Tk_Window win = acc->tkwin;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;

    hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
        g_value_init(value, G_TYPE_STRING);
        g_value_set_string(value, "");
        return;
    }

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "value");
    if (!hPtr2) {
        g_value_init(value, G_TYPE_STRING);
        g_value_set_string(value, "");
        return;
    }

    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    g_value_init(value, G_TYPE_STRING);
    g_value_set_string(value, result);
    tkvalue = value;
}

static void tk_atk_value_interface_init(AtkValueIface *iface)
{
    iface->get_current_value = tk_get_current_value;
}

/* Function to map accessible state to Atk.*/
static AtkStateSet *tk_ref_state_set(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    AtkStateSet *set = atk_state_set_new();


    if (!acc->tkwin) {
	return set;
    }

    /* Basic states. */
    atk_state_set_add_state(set, ATK_STATE_ENABLED);
    atk_state_set_add_state(set, ATK_STATE_SENSITIVE);
    atk_state_set_add_state(set, ATK_STATE_VISIBLE);

    /* Check if widget is mapped/visible. */
    if (Tk_IsMapped(acc->tkwin)) {
	atk_state_set_add_state(set, ATK_STATE_SHOWING);
    }

    /* Check for focusable widgets. */
    if (Tk_IsMapped(acc->tkwin)) {
	atk_state_set_add_state(set, ATK_STATE_FOCUSABLE);
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
    if (i == 0) {
	Tk_Window win = acc->tkwin;
	/*Retrieve command string from hash table. */
	if (!win) {
	    return false;    
	}
	Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)win);
	if (!hPtr) {
	    return false;
	}
	Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
	if (!AccessibleAttributes) {
	    return false;
	}
	Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "action");
	if (!hPtr2) {
	    return false;
	}
	const char *cmd = Tcl_GetString(Tcl_GetHashValue(hPtr2));
	if (!action) {
	    return false;
	}
	/* Finally, execute command. */
	if ((Tcl_EvalEx(acc->interp, cmd, -1, TCL_EVAL_GLOBAL)) != TCL_OK) {
	    return false;
	}
	return TCL_OK;
    }
    return TCL_OK;
}

static gint tk_action_get_n_actions(AtkAction *action)
{
    (void) action;
    return 1;
}

static const gchar *tk_action_get_name(AtkAction *action, gint i)
{
    (void) action;
    (void) i;
    return "click";
}

static void tk_atk_action_interface_init(AtkActionIface *iface)
{
    iface->do_action = tk_action_do_action;
    iface->get_n_actions = tk_action_get_n_actions;
    iface->get_name = tk_action_get_name;
}

/* 
 * Functions to initialize the Atk class and objects. 
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

    /* Remove from child list. */
    child_widgets = g_list_remove(child_widgets, self);

    g_free(self->path);
    G_OBJECT_CLASS(tk_atk_accessible_parent_class)->finalize(gobject);

}

static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    AtkObjectClass *atk_class = ATK_OBJECT_CLASS(klass);


    gobject_class->finalize = tk_atk_accessible_finalize;

    /* Set up virtual functions. */
    atk_class->get_name = tk_get_name;
    atk_class->get_description = tk_get_description;
    atk_class->get_role = tk_get_role;
    atk_class->ref_state_set = tk_ref_state_set;
    atk_class->get_n_children = tk_get_n_children;
    atk_class->ref_child = tk_ref_child;
}


/* Atk-Tk object creation with proper parent relationship. */
AtkObject *TkCreateAccessibleAtkObject(Tcl_Interp *interp, Tk_Window tkwin, const char *path)
{
    TkAtkAccessible *acc = g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
    acc->interp = interp;
    acc->tkwin = tkwin;
    acc->path = g_strdup(path);


    AtkObject *obj = ATK_OBJECT(acc);

    /* Set initial properties. */
    atk_object_set_role(obj, GetAtkRoleForWidget(tkwin));
    atk_object_set_name(obj, path);

    /* Set up parent-child relationships. */
    if (tkwin) {
	Tk_Window parent = Tk_Parent(tkwin);
	if (parent) {
	    AtkObject *parent_obj = GetAtkObjectForTkWindow(parent);
	    if (parent_obj) {
		atk_object_set_parent(obj, parent_obj);
		g_signal_emit_by_name(parent_obj, "children-changed::add", 0, obj); 
	    }
	} else {
	    /* This is a toplevel, make it a child of root. */
	    atk_object_set_parent(obj, tk_root_accessible);
	    atk_object_set_role(ATK_OBJECT(acc), ATK_ROLE_WINDOW);
	    g_signal_emit_by_name(tk_root_accessible, "children-changed::add", 0, obj);
	}
    }

    /* Add to global list and child widgets. */
    global_accessible_objects = g_list_prepend(global_accessible_objects, acc);
    child_widgets = g_list_prepend(child_widgets, obj);

    return obj;

}

/* Root window setup. */
AtkObject *tk_util_get_root(void)
{
    if (!tk_root_accessible) {
        tk_root_accessible = g_object_new(ATK_TYPE_NO_OP_OBJECT, NULL);
        atk_object_initialize(tk_root_accessible, NULL);
        atk_object_set_name(tk_root_accessible, "Tk Application");
        atk_object_set_role(tk_root_accessible, ATK_ROLE_APPLICATION);
    }
    return tk_root_accessible;
}

AtkObject *atk_get_root(void) {
    return tk_util_get_root();
}


/* 
 * Functions to integrate Tk and Gtk event loops. 
 */

static void GtkEventLoop(void *clientData)
{
    (void) clientData;
 
    /* Non-blocking GLib iteration. */
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }

    /* Schedule again to run in 25 MS. */
    Tcl_CreateTimerHandler(25, GtkEventLoop, NULL);
}

void InstallGtkEventLoop() {
    Tcl_CreateTimerHandler(25, GtkEventLoop, NULL);
}

/* 
 * Functions to map Tk window to its corresponding Atk object. 
 */
 
void InitAtkTkMapping(void)
{
    if (!tk_to_atk_map) {
	tk_to_atk_map = g_hash_table_new_full(g_direct_hash, g_direct_equal,
					      NULL, g_object_unref);
    }
}

void RegisterAtkObjectForTkWindow(Tk_Window tkwin, AtkObject *atkobj)
{
    InitAtkTkMapping();
    g_object_ref(atkobj);
    g_hash_table_insert(tk_to_atk_map, tkwin, atkobj);
}

AtkObject *GetAtkObjectForTkWindow(Tk_Window tkwin)
{
    if (!tk_to_atk_map) return NULL;
    return g_hash_table_lookup(tk_to_atk_map, tkwin);
}

void UnregisterAtkObjectForTkWindow(Tk_Window tkwin)
{
    if (tk_to_atk_map) {
	g_hash_table_remove(tk_to_atk_map, tkwin);
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

static int EmitSelectionChanged(ClientData clientData, Tcl_Interp *ip, int objc,Tcl_Obj *const objv[])
{		    
    (void) clientData;
    
    if (objc < 2) {
	Tcl_WrongNumArgs(ip, 1, objv, "window?");
	return TCL_ERROR;
    }
    Tk_Window path;
	
    path = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
    if (path == NULL) {
	return 0;
    }
    
    AtkObject *acc = GetAtkObjectForTkWindow(path);
    
    if (!acc) {
        Tcl_AppendResult(ip, "No accessible object for window", NULL);
        return TCL_ERROR;
    }

    GValue gval = G_VALUE_INIT;
    tk_get_current_value(ATK_VALUE(acc), &gval);
    g_signal_emit_by_name(G_OBJECT(acc), "value-changed", &tkvalue);
	
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
static int EmitFocusChanged(ClientData clientData, Tcl_Interp *ip, int objc,Tcl_Obj *const objv[])
{
    (void) clientData;

    if (objc < 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "window?");
        return TCL_ERROR;
    }

    Tk_Window path = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
    if (path == NULL) {
        Tcl_AppendResult(ip, "Invalid window path", NULL);
        return TCL_ERROR;
    }

    AtkObject *acc = GetAtkObjectForTkWindow(path);
    if (!acc) {
        Tcl_AppendResult(ip, "No accessible object for window", NULL);
        return TCL_ERROR;
    }

    /* Emit focus-event with TRUE to indicate focus gained */
    g_signal_emit_by_name(G_OBJECT(acc), "focus-event", TRUE);
    g_signal_emit_by_name(G_OBJECT(acc), "state-change", ATK_STATE_FOCUSED, TRUE);

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

int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) 
{
    (void) clientData;
    (void) objc;
    (void) *objv;
    
    DBusError error;
    DBusConnection *connection;
    dbus_bool_t has_owner;
    bool result = true;

    dbus_error_init(&error);

    /* Connect to the session bus. */
    connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (dbus_error_is_set(&error)) {
        dbus_error_free(&error);
        result = false;
    }

    if (!connection) {
        result = false;
    }

    /* Check if org.a11y.Bus is owned (required for AT-SPI to work). */
    has_owner = dbus_bus_name_has_owner(connection, "org.a11y.Bus", &error);
    if (dbus_error_is_set(&error)) {
        dbus_error_free(&error);
        result = false;
    }

    if (!has_owner) {
        result = false;
    }

    /* Now check for a screen reader on the accessibility bus. */
    has_owner = dbus_bus_name_has_owner(connection, "org.a11y.atspi.Registry", &error);
    dbus_connection_unref(connection);

    if (dbus_error_is_set(&error)) {
        dbus_error_free(&error);
        result = false;
    }

    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(result));
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * TkAtkAccessible_RegisterForCleanup --
 *
 * Register event handler for destroying accessibility element.
 *
 * Results:
 *      Event handler is registered.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void TkAtkAccessible_RegisterForCleanup(Tk_Window tkwin, void *tkAccessible) {
    Tk_CreateEventHandler(tkwin, StructureNotifyMask, 
			  TkAtkAccessible_DestroyHandler, tkAccessible);
}

/*
 *----------------------------------------------------------------------
 *
 * TkAtkAccessible_DestroyHandler --
 *
 * Clean up accessibility element structures when window is destroyed.
 *
 * Results:
 *	Accessibility element is deallocated. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void TkAtkAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr) 
{
    if (eventPtr->type == DestroyNotify) {
	TkAtkAccessible *tkAccessible = (TkAtkAccessible *)clientData;
	if (tkAccessible) {
	    UnregisterAtkObjectForTkWindow(tkAccessible->tkwin);	  
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkAtkAccessibleObjCmd --
 *
 *	Main command for adding and managing accessibility objects to Tk
 *      widgets on Linux using the Atk accessibility API.
 *
 * Results:
 *
 *      A standard Tcl result.
 *
 * Side effects:
 *
 *	Tk widgets are now accessible to screen readers.
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
    Tk_Window tkwin = Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));

    if (tkwin == NULL) {
	Tcl_SetResult(interp, "Invalid window name.", TCL_STATIC);
	return TCL_ERROR;
    }

    TkAtkAccessible *accessible = (TkAtkAccessible*) TkCreateAccessibleAtkObject(interp, tkwin, windowName);
    TkAtkAccessible_RegisterForCleanup(tkwin, accessible);
    RegisterAtkObjectForTkWindow(tkwin, (AtkObject*)accessible);
 
    if (Tk_IsTopLevel(tkwin)) {
	atk_object_set_parent(ATK_OBJECT(accessible), tk_root_accessible);
	atk_object_set_role(accessible, ATK_ROLE_WINDOW);
    }
	
    if (accessible == NULL) {		
	Tcl_SetResult(interp, "Failed to create accessible object.", TCL_STATIC);
	return TCL_ERROR;
    }

    return TCL_OK;
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
 *      A standard Tcl result.
 *
 * Side effects:
 *
 *	Accessibility module is now activated.
 *
 *----------------------------------------------------------------------
 */

#ifdef USE_ATK
int TkAtkAccessibility_Init(Tcl_Interp *interp)
{

    /* Force accessibility module. */
    g_setenv("GTK_MODULES", "gail:atk-bridge", FALSE);
   

    /* Confirm root is available. */
    tk_root_accessible = tk_util_get_root();
    atk_object_set_role(tk_root_accessible, ATK_ROLE_APPLICATION);
    atk_object_set_name(tk_root_accessible, "Tk Application");;

    /*  Prime the GLib main loop once to allow bridge setup. */
    while (g_main_context_iteration(NULL, FALSE));
 
    /* Start AT-SPI connection. */
    atk_bridge_adaptor_init(NULL, NULL);

    /* Signal window update. */
    g_signal_emit_by_name(tk_root_accessible, "children-changed", 0, NULL);

    /* Start GLib event loop with Tcl integration. */
    InstallGtkEventLoop(); 

    /* Register Tcl accessibility commands */
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
/* No Atk found. */
int TkAtkAccessibility_Init(Tcl_Interp *interp)
{
    /*Create empty commands if Atk not available. */
    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", NULL, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", NULL, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_focus__change", NULL, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader", NULL, NULL, NULL);
    return TCL_OK;	
}
#endif

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
