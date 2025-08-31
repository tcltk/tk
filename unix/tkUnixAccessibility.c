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
    gint x, y, width, height;
    char *path;
    int is_focused;
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

#define ATK_CONTEXT g_main_context_default()

/* Variables for managing ATK objects. */
static AtkObject *tk_root_accessible = NULL;
static GList *toplevel_accessible_objects = NULL; /* This list will hold refs to toplevels. */
static GHashTable *tk_to_atk_map = NULL; /* Maps Tk_Window to AtkObject. */
extern Tcl_HashTable *TkAccessibilityObject; /* Hash table for managing accessibility attributes. */
static GMainContext *acc_context = NULL;

/* GLib-Tcl event loop integration. */
static void Atk_Event_Setup (ClientData clientData, int flags);
static void Atk_Event_Check(ClientData clientData, int flags);
static int Atk_Event_Run(Tcl_Event *event, int flags);

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
static gchar *GetAtkValueForWidget(Tk_Window win);
static void tk_get_current_value(AtkValue *obj, GValue *value);
static void tk_atk_value_interface_init(AtkValueIface *iface);
static AtkStateSet *tk_ref_state_set(AtkObject *obj);
static gboolean tk_action_do_action(AtkAction *action, gint i);
static gint tk_action_get_n_actions(AtkAction *action);
static const gchar *tk_action_get_name(AtkAction *action, gint i);
static void tk_atk_action_interface_init(AtkActionIface *iface);

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
static void TkAtkAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_FocusHandler(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_CreateHandler(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_ConfigureHandler(ClientData clientData, XEvent *eventPtr);

/* Tcl command implementations. */
static int EmitSelectionChanged(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int EmitFocusChanged(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int IsScreenReaderActive(void);
int TkAtkAccessibleObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int TkAtkAccessibility_Init(Tcl_Interp *interp);


/* Define custom ATK object bridged to Tcl/Tk. */
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
 * GLib integration functions. These create a Tcl event source so that 
 * the GLib event loop can be smoothly integrated with Tcl/Tk.
 *
 *----------------------------------------------------------------------
 */

/* Configure event loop. */
    static void Atk_Event_Setup(ClientData clientData, int flags) 
{
    (void)clientData;
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
static void Atk_Event_Check(ClientData clientData, int flags) 
{
    (void)clientData;

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
static int Atk_Event_Run(Tcl_Event *event, int flags) 
{
    (void)event;

    if (!(flags & TCL_WINDOW_EVENTS)) {
        return 0;
    }
    
    while (g_main_context_pending(acc_context)) {
	g_main_context_iteration(acc_context, FALSE);
    }
		
    return 1;
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
    iface->contains = tk_contains;
}

/*
 * Functions to manage child count and individual child widgets.
 */
 
static gint tk_get_n_children(AtkObject *obj)
{
    if (obj == tk_root_accessible) {
        /* Root has only toplevel windows as children. */
        return g_list_length(toplevel_accessible_objects);
    }
    
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    if (!acc || !acc->tkwin) return 0;
    
    int count = 0;
    TkWindow *childPtr;
    /* Traverse Tk's native child list. */
    for (childPtr = ((TkWindow*)acc->tkwin)->childList; 
         childPtr != NULL; 
         childPtr = childPtr->nextPtr) {
        count++;
    }
    return count;
}

static AtkObject *tk_ref_child(AtkObject *obj, gint i)
{
    if (obj == tk_root_accessible) {
        /* Handle root's children (toplevel windows). */
        GList *child = g_list_nth(toplevel_accessible_objects, i);
        if (child) {
            g_object_ref(child->data);
            return ATK_OBJECT(child->data);
        }
        return NULL;
    }

    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    if (!acc || !acc->tkwin) return NULL;
    
    TkWindow *childPtr;
    gint index = 0;
    /* Iterate through Tk's native child list. */
    for (childPtr = ((TkWindow*)acc->tkwin)->childList; 
         childPtr != NULL; 
         childPtr = childPtr->nextPtr, index++) {
        if (index == i) {
            Tk_Window child_tkwin = (Tk_Window)childPtr;
            AtkObject *child_obj = GetAtkObjectForTkWindow(child_tkwin);
            if (!child_obj) {
                /* Create ATK object only when needed. */
                child_obj = TkCreateAccessibleAtkObject(acc->interp, child_tkwin,Tk_PathName(child_tkwin));
                if (child_obj) {
                    /* Establish parent-child relationship. */
                    atk_object_set_parent(child_obj, obj);
                    TkAtkAccessible_RegisterEventHandlers(child_tkwin, (TkAtkAccessible *)child_obj);
                }
            }
            if (child_obj) {
                /* ATK requires caller to free this reference. */
                g_object_ref(child_obj);
                return child_obj;
            }
            break;
        }
    }
    return NULL;
}

/*
 * Functions to map accessible role to ATK.
 */

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
    if (!acc || !acc->tkwin) return ATK_ROLE_UNKNOWN;
   
    return GetAtkRoleForWidget(acc->tkwin);
}

/*
 * Name and description getters
 * for Tk-ATK objects.
 */

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
    if (!acc || !acc->tkwin) return NULL;
   
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
    
    AtkRole role = GetAtkRoleForWidget(win);
    /* If label, return the value instead of the description. */
    if (role == ATK_ROLE_LABEL) {
		return GetAtkValueForWidget(win); 
	}
   
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


/*
 * Functions to map accessible value to ATK using
 * AtkValue interface.
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

 
static void tk_get_current_value(AtkValue *obj, GValue *value)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    if (!acc || !acc->tkwin) {
        return;
    }

    gchar *val = GetAtkValueForWidget(acc->tkwin);

    g_value_init(value, G_TYPE_STRING);
    g_value_set_string(value, val ? val : "");
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
   
    if (!acc || !acc->tkwin) return set;
   
    atk_state_set_add_state(set, ATK_STATE_ENABLED);
    atk_state_set_add_state(set, ATK_STATE_SENSITIVE);
   
    if (Tk_IsMapped(acc->tkwin)) {
        atk_state_set_add_state(set, ATK_STATE_VISIBLE);
        atk_state_set_add_state(set, ATK_STATE_SHOWING);
        atk_state_set_add_state(set, ATK_STATE_FOCUSABLE);
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
}

static void tk_atk_accessible_finalize(GObject *gobject)
{
    TkAtkAccessible *self = (TkAtkAccessible*)gobject;
    if (!self) return;

    if (self->tkwin) {
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
    (void) interp;
	
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
	}
    }
    
    /* Check for existing registration. */
    AtkObject *existing = GetAtkObjectForTkWindow(tkwin);
    if (existing && existing != accessible) {
	g_warning("RegisterToplevelWindow: Toplevel %s already registered with different AtkObject",
		  Tk_PathName(tkwin));
	return;
    }

    /* Hold a reference to the accessible object. */
    g_object_ref(accessible);

    /* Set parent and add to root's children. */
    atk_object_set_parent(accessible, tk_root_accessible);
        
    /* Add to toplevel_accessible_objects. */
    if (!g_list_find(toplevel_accessible_objects, accessible)) {
        toplevel_accessible_objects = g_list_append(toplevel_accessible_objects, accessible);
    }
    atk_object_set_parent(accessible, tk_root_accessible);
    
    /* Notify about new child (index is new position in list). */
    gint index = g_list_index(toplevel_accessible_objects, accessible);
    g_signal_emit_by_name(tk_root_accessible, "children-changed::add", index, accessible);
   

    /* Set name, role, and description. */
    const gchar *name = tk_get_name(accessible);
    if (name) {
	tk_set_name(accessible, name);
    } else {
	tk_set_name(accessible, Tk_PathName(tkwin));
    }
					
    atk_object_set_role(accessible, ATK_ROLE_WINDOW);
    atk_object_set_description(accessible, name ? name : ((TkAtkAccessible *)accessible)->path);


    /* Register event handlers. */
    TkAtkAccessible_RegisterEventHandlers(tkwin, (TkAtkAccessible *)accessible);
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
   
    /* Check if exists */
    AtkObject *existing = GetAtkObjectForTkWindow(tkwin);
    if (existing) return existing;
   
    /* Create simple object */
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
   
    RegisterAtkObjectForTkWindow(tkwin, obj);
   
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
static void TkAtkAccessible_CreateHandler(ClientData clientData, XEvent *eventPtr)
{
    if (eventPtr->type != CreateNotify) return;
    
    Tk_Window parentWin = (Tk_Window)clientData;
    /* Convert X window ID to Tk_Window. */
    Tk_Window newWin = Tk_IdToWindow(eventPtr->xcreatewindow.display, 
				     eventPtr->xcreatewindow.window);
    
    if (!newWin || !parentWin) return;
    
    AtkObject *parentObj = GetAtkObjectForTkWindow(parentWin);
    if (!parentObj) return;
    
    AtkObject *childObj = GetAtkObjectForTkWindow(newWin);
    if (!childObj) {
        /* Create accessibility object for new window. */
        childObj = TkCreateAccessibleAtkObject(
					       ((TkAtkAccessible*)parentObj)->interp,
					       newWin,
					       Tk_PathName(newWin)
					       );
    }
    
    if (childObj) {
        /* Set parent-child relationship. */
        atk_object_set_parent(childObj, parentObj);
        /* Register event handlers for new window. */
        TkAtkAccessible_RegisterEventHandlers(newWin, (TkAtkAccessible*)childObj);
        /* Notify ATK about new child. */
        g_signal_emit_by_name(parentObj, "children-changed::add", 
			      tk_get_n_children(parentObj)-1, childObj);
    }
}

/* Respond to <DestroyNotify> events. */
static void TkAtkAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr)
{
    if (eventPtr->type != DestroyNotify) return;
    
    TkAtkAccessible *acc = (TkAtkAccessible *)clientData;
    if (!acc) return;
    GObject *obj =  (GObject*)acc; 
    tk_atk_accessible_finalize(obj); 
}


/* Respond to <Configure> events. */
static void TkAtkAccessible_ConfigureHandler(ClientData clientData, XEvent *eventPtr)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)clientData;
    if (!acc || !acc->tkwin || !Tk_IsMapped(acc->tkwin)) return;

    if (eventPtr->type == ConfigureNotify) {
		
	AtkObject *obj = ATK_OBJECT(acc);  

	/* Build the bounds rectangle. */
	AtkRectangle rect;
	Tk_GetRootCoords(acc->tkwin, &rect.x, &rect.y);
	rect.width  = Tk_Width(acc->tkwin);
	rect.height = Tk_Height(acc->tkwin);

	/* Direct signal emission. */
	g_signal_emit_by_name(obj, "bounds-changed", &rect, TRUE);
    }
}

/* Respond to <FocusIn/Out> events. */
static void TkAtkAccessible_FocusHandler(ClientData clientData, XEvent *eventPtr)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)clientData;
    if (!acc || !Tk_IsMapped(acc->tkwin)) return;
   
    AtkObject *obj = ATK_OBJECT(acc);
    gboolean focused = (eventPtr->type == FocusIn);
   
    /* Direct signal emission. */
    g_signal_emit_by_name(obj, "focus-event", focused);
    g_signal_emit_by_name(obj, "state-change", "focused", focused);

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

static int EmitSelectionChanged(ClientData clientData, Tcl_Interp *ip, int objc, Tcl_Obj *const objv[])
{
    (void)clientData;

    if (objc < 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "window");
        return TCL_ERROR;
    }

    const char *windowName = Tcl_GetString(objv[1]);
    Tk_Window tkwin = Tk_NameToWindow(ip, windowName, Tk_MainWindow(ip));
    if (!tkwin) return TCL_OK;

    AtkObject *obj = GetAtkObjectForTkWindow(tkwin);
    if (!obj) {
        obj = TkCreateAccessibleAtkObject(ip, tkwin, windowName);
        if (!obj) return TCL_OK;
        TkAtkAccessible_RegisterEventHandlers(tkwin, (TkAtkAccessible *)obj);
    }

    AtkRole role = tk_get_role(obj);

    if (role == ATK_ROLE_TEXT || role == ATK_ROLE_ENTRY) {
        /* Text/entry widget selection changed. */
        g_signal_emit_by_name(obj, "text-selection-changed");

    } else if (role == ATK_ROLE_SCROLL_BAR ||
               role == ATK_ROLE_SLIDER ||
               role == ATK_ROLE_SPIN_BUTTON ||
               role == ATK_ROLE_PROGRESS_BAR) {
        /* Numeric widgets (scale, scrollbar, spinbox, progress). */
        GValue gval = G_VALUE_INIT;
        tk_get_current_value(ATK_VALUE(obj), &gval);
        gdouble new_val = 0.0;

        if (G_VALUE_HOLDS_DOUBLE(&gval)) {
            new_val = g_value_get_double(&gval);
        } else if (G_VALUE_HOLDS_STRING(&gval)) {
            const char *s = g_value_get_string(&gval);
            if (s) new_val = g_ascii_strtod(s, NULL);
        }
        g_value_unset(&gval);

        g_signal_emit_by_name(obj, "value-changed", new_val, 0.0);

    } else if (role == ATK_ROLE_TREE ||
	       role == ATK_ROLE_LIST ||
	       role == ATK_ROLE_TABLE) {
        /* String-valued widgets (listbox, combobox, etc.). */
        GValue gval = G_VALUE_INIT;
        tk_get_current_value(ATK_VALUE(obj), &gval);

        if (G_VALUE_HOLDS_STRING(&gval)) {
            const char *s = g_value_get_string(&gval);
            g_signal_emit_by_name(obj, "value-changed", s, NULL);
        }
        g_value_unset(&gval);
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

static int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) 
{
    (void)clientData;
    (void)objc;
    (void)objv;

    int result = IsScreenReaderActive();

    Tcl_SetObjResult(interp, Tcl_NewIntObj(result));
    return TCL_OK;
}

/* 
 * Helper function to determine if screen reader is running. Separate function 
 * because it can be called internally as well as a Tcl command. 
 */

/* Helper function to check if screen reader is running. */
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

    /* Register for cleanup, mapping and other events. */
    TkAtkAccessible_RegisterEventHandlers(tkwin, accessible);

    /* Handle toplevels separately. */
    if (Tk_IsTopLevel(tkwin)) {
	RegisterToplevelWindow(interp, tkwin, ATK_OBJECT(accessible));
    } else {
	Tk_Window parent_win = Tk_Parent(tkwin);
	if (parent_win) {
	    AtkObject *parent_obj = GetAtkObjectForTkWindow(parent_win);
	    if (!parent_obj) {
		/* Recursively register parent. */
		Tcl_Obj *parent_cmd_objv[2];
		parent_cmd_objv[0] = objv[0];  /* Command name. */
		parent_cmd_objv[1] = Tcl_NewStringObj(Tk_PathName(parent_win), -1);
		if (TkAtkAccessibleObjCmd(clientData, interp, 2, parent_cmd_objv) != TCL_OK) {
		    Tcl_DecrRefCount(parent_cmd_objv[1]);
		    return TCL_ERROR;
		}
		Tcl_DecrRefCount(parent_cmd_objv[1]);
		parent_obj = GetAtkObjectForTkWindow(parent_win);  /* Now it should exist. */
	    }
	    if (parent_obj) {
		atk_object_set_parent(ATK_OBJECT(accessible), parent_obj);
	    }
	}
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

    /* 
     * Establish GLib context for event loop processing. 
     * We are creating a custom GLib event source that the
     * Tcl event loop will respond to. 
     */	
    acc_context = ATK_CONTEXT;
    Tcl_CreateEventSource (Atk_Event_Setup, Atk_Event_Check, 0);

	
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

