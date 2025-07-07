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
#include <gtk/gtk.h>
#include <atk/atk.h>
#include <atk-bridge.h> 
#include <dbus/dbus.h>

/* Data declarations and protoypes of functions used in this file. */
typedef struct _TkAtkAccessible {
    AtkObject parent;
    Tk_Window tkwin;
    Tcl_Interp *interp;
    char *path;
} TkAtkAccessible;

typedef struct _TkAtkAccessibleClass {
    AtkObjectClass parent_class;
} TkAtkAccessibleClass;

#define TK_ATK_TYPE_ACCESSIBLE (tk_atk_accessible_get_type())
G_DEFINE_TYPE(TkAtkAccessible, tk_atk_accessible, ATK_TYPE_OBJECT)

static GList *global_accessible_objects = NULL;
static GHashTable *tk_to_atk_map = NULL;

/* Atk/Tk glue functions. */
static void GetWidgetExtents(Tk_Window tkwin, int *x, int *y, int *w, int *h);
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

/* Lower-level functions providing integration between Atk objects and Tcl/Tk. */
static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass);
static void tk_atk_accessible_init(TkAtkAccessible *accessible);
static void tk_atk_accessible_finalize(GObject *gobject);
AtkObject *TkCreateAccessibleAtkObject(Tcl_Interp *interp, Tk_Window tkwin, const char *path);
static void  GtkEventLoop(); 
void InstallGtkEventLoop();
void InitAtkTkMapping(void);
void RegisterAtkObjectForTkWindow(Tk_Window tkwin, AtkObject *atkobj);
AtkObject *GetAtkObjectForTkWindow(Tk_Window tkwin);
void UnregisterAtkObjectForTkWindow(Tk_Window tkwin);

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
    {NULL, 0}
};

/* Hash table for managing accessibility attributes. */
extern Tcl_HashTable *TkAccessibilityObject;

/* Variable for widget values. */
static GValue *tkvalue = NULL;

/* 
 * Function to get accessible frame to Atk. 
 */
static void GetWidgetExtents(Tk_Window tkwin, int *x,int *y, int *w,int *h)
{
    if (tkwin) {
	*x = Tk_X(tkwin);
	*y = Tk_Y(tkwin);
	*w = Tk_Width(tkwin);
	*h = Tk_Height(tkwin);
    } else {
	*x = *y = *w = *h = 0;
    }
}

static void tk_get_extents(AtkComponent *component, gint *x, gint *y, gint *width, gint *height, AtkCoordType coord_type)
{
    (void) coord_type;
    TkAtkAccessible *acc = (TkAtkAccessible *)component;
    GetWidgetExtents(acc->tkwin, x, y, width, height);
}


/* Limit children of widget. Only the toplevel should return children. */
static gint tk_get_n_children(AtkObject *obj)
{
    (void) *obj;
    return 0;
}

/* Limit children of widget. Only the toplevel should return children. */
static AtkObject *tk_ref_child(AtkObject *obj, gint i)
{
    (void) obj;
    (void) i;
    return NULL;
}

/* 
 * Functions to map accessible role to Atk.
 */

static AtkRole GetAtkRoleForWidget(Tk_Window win)
{
    AtkRole role;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
		
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
		role = ATK_ROLE_UNKNOWN;
		return role;
    }
		
    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "role");
    if (!hPtr2) {
	role = ATK_ROLE_UNKNOWN;
	return role;
    }
    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    for (long unsigned int i = 0; i < sizeof(roleMap); i++) {
	if (strcmp(roleMap[i].tkrole, result) != 0) {
	continue;
	}
	role = roleMap[i].atkrole;
    }
    return role;
}

static AtkRole tk_get_role(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    Tk_Window win = acc->tkwin;
    return GetAtkRoleForWidget(win);
}


/* Function to map accessible name to Atk.*/
static const gchar *tk_get_name(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    Tk_Window win = acc->tkwin;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
	
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	return NULL;
    }
	
    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "name");
    if (!hPtr2) {
	return NULL;
    }
	
    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    return result;
}

/* Function to map accessible description to Atk. */
static const gchar *tk_get_description(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;

    Tk_Window win = acc->tkwin;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;

  
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	return NULL;
    }

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "description");
    if (!hPtr2) {
	return NULL;
    }
	
    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    return result;
}


/* Function to map accessible value to Atk using AtkValue interface. */
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

/* Function to map accessible state to Atk.*/
static AtkStateSet *tk_ref_state_set(AtkObject *obj)
{
    (void) *obj;
    AtkStateSet *set = atk_state_set_new();
    atk_state_set_add_state(set, ATK_STATE_ENABLED);
    atk_state_set_add_state(set, ATK_STATE_SENSITIVE);
    return set;
}

/* 
 * Functions to get button press action to Atk. 
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

/* Initialze Tk-Atk object. */
static void tk_atk_accessible_init(TkAtkAccessible *self) {
    self->tkwin = NULL;
    self->interp = NULL;
    self->path = NULL;
}

/* Initialize Tk-Atk class. */
static void tk_atk_accessible_finalize(GObject *gobject) {
    TkAtkAccessible *self = (TkAtkAccessible*)gobject;
    g_free(self->path);
    G_OBJECT_CLASS(tk_atk_accessible_parent_class)->finalize(gobject);
}

/* Manage Tk-Atk resources. */
static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->finalize = tk_atk_accessible_finalize;
}

/* Function to map Tk window to Atk attributes. */
AtkObject *TkCreateAccessibleAtkObject(Tcl_Interp *interp, Tk_Window tkwin,const char *path)
{
    TkAtkAccessible *acc = g_object_new(tk_atk_accessible_get_type(), NULL);
    acc->interp = interp;
    acc->tkwin = tkwin;
    acc->path = g_strdup(path);

    atk_object_set_role(ATK_OBJECT(acc), GetAtkRoleForWidget(acc->tkwin));
    atk_object_set_name(ATK_OBJECT(acc), path);

    global_accessible_objects = g_list_prepend(global_accessible_objects, acc);
    return ATK_OBJECT(acc);
}

/* 
 * Functions to integrate Tk and Gtk event loops. 
 */

static void GtkEventLoop(void *clientData)
{
    (void) clientData;
    
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
}

void InstallGtkEventLoop() {
    Tcl_DoWhenIdle(GtkEventLoop, NULL);
}

/* 
 * Functions to map Tk window to its corresponding Atk object. 
 */
 
void InitAtkTkMapping(void)
{
    if (!tk_to_atk_map) {
	tk_to_atk_map = g_hash_table_new(g_direct_hash, g_direct_equal);
    }
}

void RegisterAtkObjectForTkWindow(Tk_Window tkwin, AtkObject *atkobj)
{
    /* Make sure the hash table exists. */
    InitAtkTkMapping();  
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
int TkAtkAccessibility_Init(Tcl_Interp *interp) {
	
    /* Handle Atk initialization. */
    
    if (atk_get_major_version() < 2) {
	Tcl_SetResult(interp, "ATK version 2.0 or higher is required.", TCL_STATIC);
	return TCL_ERROR;
    }

    atk_bridge_adaptor_init(NULL, NULL);
    g_type_ensure(TK_ATK_TYPE_ACCESSIBLE);
	
    InstallGtkEventLoop();

    /* Install Tcl commands. */
    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", TkAtkAccessibleObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", EmitSelectionChanged, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_focus__change", EmitFocusChanged, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader", IsScreenReaderRunning, NULL, NULL);
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
