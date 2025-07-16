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
#include "tkInt.h" 

#ifdef USE_ATK
#include <atk/atk.h>
#include <atk/atktext.h>
#include <atk-bridge.h> 
#include <dbus/dbus.h>
#include <glib.h>

/* Data declarations used in this file. */

typedef struct TkWmInfo {
    char *title;
} TkWmInfo;

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
static GList *toplevel_accessible_objects = NULL;
static GList *child_widgets = NULL; 
static GHashTable *tk_to_atk_map = NULL;

/* Atk/Tk glue functions. */
static void tk_get_extents(AtkComponent *component, gint *x, gint *y, gint *width, gint *height, AtkCoordType coord_type);
static gint tk_get_n_children(AtkObject *obj);
static AtkObject *tk_ref_child(AtkObject *obj, guint i);
static AtkRole GetAtkRoleForWidget(Tk_Window win);
static AtkRole tk_get_role(AtkObject *obj);
static const gchar *tk_get_name(AtkObject *obj);
int *tk_set_name(AtkObject *obj);
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
static gchar *tk_get_text(AtkText *text);
static void tk_atk_text_interface_init(AtkTextIface *iface);

/* Lower-level functions providing integration between Atk objects and Tcl/Tk. */
static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass);
static void tk_atk_accessible_init(TkAtkAccessible *accessible);
static void tk_atk_accessible_finalize(GObject *gobject);
static void RegisterChildWidgets(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *parent_obj);
static void RegisterToplevelWindow(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *accessible);
AtkObject *TkCreateAccessibleAtkObject(Tcl_Interp *interp, Tk_Window tkwin, const char *path);
static void GtkEventLoop(ClientData clientData); 
void InstallGtkEventLoop(void);
void InitAtkTkMapping(void);
void RegisterAtkObjectForTkWindow(Tk_Window tkwin, AtkObject *atkobj);
AtkObject *GetAtkObjectForTkWindow(Tk_Window tkwin);
void UnregisterAtkObjectForTkWindow(Tk_Window tkwin);
static AtkObject *tk_util_get_root(void);
static gboolean delayed_init();

/* Script-level commands and helper functions. */
static int EmitSelectionChanged(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
void TkAtkAccessible_RegisterEventHandlers(Tk_Window tkwin, void *tkAccessible);
static void TkAtkAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr);
static void TkAtkAccessible_NameHandler(ClientData clientData, XEvent *eventPtr);
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
    {"Text", ATK_ROLE_TEXT},
    {"Toplevel", ATK_ROLE_WINDOW},  
    {"Frame", ATK_ROLE_PANEL},     
    {NULL, 0}
};

/* Hash table for managing accessibility attributes. */
extern Tcl_HashTable *TkAccessibilityObject;

/* Variable for widget values. */
static GValue *tkvalue = NULL;


/* Define custom Atk object bridged to Tcl/Tk. */
#define TK_ATK_TYPE_ACCESSIBLE (tk_atk_accessible_get_type())
G_DEFINE_TYPE_WITH_CODE(TkAtkAccessible, tk_atk_accessible, ATK_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(ATK_TYPE_COMPONENT, tk_atk_component_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_ACTION, tk_atk_action_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_VALUE, tk_atk_value_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_TEXT, tk_atk_text_interface_init))

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

/* 
 * Functions to manage child count and individual child widgets. 
 */
static gint tk_get_n_children(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;

    if (obj == tk_root_accessible) {
        return g_list_length(toplevel_accessible_objects);
    }

    if (!acc->tkwin) {
        return 0;
    }

    /* Count direct child windows with accessible objects. */
    int count = 0;
    TkWindow *winPtr = (TkWindow *)acc->tkwin;
    TkWindow *childPtr;
    for (childPtr = winPtr->childList; childPtr != NULL; childPtr = childPtr->nextPtr) {
        if (GetAtkObjectForTkWindow((Tk_Window)childPtr)) {
            count++;
        }
    }
    return count;
}


static AtkObject *tk_ref_child(AtkObject *obj, guint i)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;

    if (obj == tk_root_accessible) {
        if (i >= g_list_length(toplevel_accessible_objects)) {
            return NULL;
        }
	/* Get accessible object from Tk child window. */
        AtkObject *child = g_list_nth_data(toplevel_accessible_objects, i);
        if (child) {
            g_object_ref(child);
        }
        return child;
    }

    if (!acc->tkwin) {
        return NULL;
    }

    /* Return i-th direct child with accessible object. */
    int index = 0;
    TkWindow *winPtr = (TkWindow *)acc->tkwin;
    TkWindow *childPtr;
    for (childPtr = winPtr->childList; childPtr != NULL; childPtr = childPtr->nextPtr) {
        AtkObject *child_obj = GetAtkObjectForTkWindow((Tk_Window)childPtr);
        if (child_obj) {
            if (index == i) {
                g_object_ref(child_obj);
                return child_obj;
            }
            index++;
        }
    }
    return NULL;
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
    
    /* Special case for toplevel windows. */
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
    if (!acc->tkwin) {
        if (obj == tk_root_accessible) {
            Tcl_Interp *interp = Tcl_CreateInterp(); // Use a temporary interpreter
            Tk_Window mainWin = Tk_MainWindow(interp);
            if (mainWin) {
                Tcl_DString cmd;
                Tcl_DStringInit(&cmd);
                Tcl_DStringAppend(&cmd, "wm title .", -1); // Get title of main toplevel
                if (Tcl_Eval(interp, Tcl_DStringValue(&cmd)) == TCL_OK) {
                    const char *result = Tcl_GetStringResult(interp);
                    if (result && *result) {
                        gchar *ret = g_strdup(result);
                        Tcl_DStringFree(&cmd);
                        Tcl_DeleteInterp(interp);
                        return ret;
                    }
                }
                Tcl_DStringFree(&cmd);
            }
            Tcl_DeleteInterp(interp);
            return g_strdup("Tk Application"); // Fallback if no title or main window
        }
        return NULL;
    }
	
    /* For menus, use entry label as the accessible name. */
    if (GetAtkRoleForWidget(acc->tkwin) == ATK_ROLE_MENU) {
        Tcl_DString cmd;
        Tcl_DStringInit(&cmd);
	Tcl_DStringAppend(&cmd, Tk_PathName(acc->tkwin), -1);
        Tcl_DStringAppend(&cmd, "entrycget active -label ", -1);
        if (Tcl_Eval(acc->interp, Tcl_DStringValue(&cmd)) == TCL_OK) {
            const char *result = Tcl_GetStringResult(acc->interp);
            if (result && *result) return g_strdup(result);
        }
        Tcl_DStringFree(&cmd);
    }
	
    /* For labels, use text content as the accessible name. */
    if (GetAtkRoleForWidget(acc->tkwin) == ATK_ROLE_LABEL) {
        Tcl_DString cmd;
        Tcl_DStringInit(&cmd);
        Tcl_DStringAppend(&cmd, Tk_PathName(acc->tkwin), -1);
	Tcl_DStringAppend(&cmd, "cget -text ", -1);

        if (Tcl_Eval(acc->interp, Tcl_DStringValue(&cmd)) == TCL_OK) {
            const char *result = Tcl_GetStringResult(acc->interp);
            if (result && *result) {
                gchar *ret = g_strdup(result);
                Tcl_DStringFree(&cmd);
                return ret;
            }
        }
        Tcl_DStringFree(&cmd);
    }

    if (Tk_IsTopLevel(acc->tkwin) && Tk_PathName(acc->tkwin)) {
        Tcl_DString cmd;
        Tcl_DStringInit(&cmd);
        Tcl_DStringAppend(&cmd, "wm title ", -1);
        Tcl_DStringAppend(&cmd, Tk_PathName(acc->tkwin), -1);

        if (Tcl_Eval(acc->interp, Tcl_DStringValue(&cmd)) != TCL_OK) {
            Tcl_DStringFree(&cmd);
            return g_strdup(Tk_PathName(acc->tkwin)); /* Fallback to pathname. */
        }

        const char *result = Tcl_GetStringResult(acc->interp);
        gchar *ret = g_strdup(result && *result ? result : Tk_PathName(acc->tkwin));
        Tcl_DStringFree(&cmd);
        return ret;
    }

    /* For other widgets: use accessible name if set. */
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, acc->tkwin);
    if (hPtr) {
	Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
	Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "name");
	if (hPtr2) {
	    return Tcl_GetString(Tcl_GetHashValue(hPtr2));
	}
    }

    /* Default: use window path. */
    return Tk_PathName(acc->tkwin);
}

/* Called if name changes. */
int *tk_set_name(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;

    gchar *name = NULL;

    if (!acc->tkwin) {
        /* Handle root case.*/
        if (obj == tk_root_accessible) {
            name = "Tk Application";
        }
    }

    /* For toplevel windows: use WM title */
    if (Tk_IsTopLevel(acc->tkwin)) {
	Tcl_DString cmd;
	Tcl_DStringInit(&cmd);

	Tcl_DStringAppend(&cmd, "wm title ", -1);
	Tcl_DStringAppend(&cmd, Tk_PathName(acc->tkwin), -1);

	const char *result = Tcl_GetStringResult(acc->interp);
	gchar *ret = g_strdup(result);

	Tcl_DStringFree(&cmd);
	name = ret; 
    }

    /* For other widgets: use accessible name if set. */
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, acc->tkwin);
    if (hPtr) {
	Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
	Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "name");
	if (hPtr2) {
	    name = Tcl_GetString(Tcl_GetHashValue(hPtr2));
	}
    }

    /* Default: use window path. */
    name = Tk_PathName(acc->tkwin);

    atk_object_set_name(acc, name);
    g_object_notify(G_OBJECT(acc), "accessible-name");
    return TCL_OK;
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

/* Function to map accessible state to Atk. */
static AtkStateSet *tk_ref_state_set(AtkObject *obj)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;
    AtkStateSet *set = atk_state_set_new();
    if (!acc->tkwin) return set;

    atk_state_set_add_state(set, ATK_STATE_ENABLED);
    atk_state_set_add_state(set, ATK_STATE_SENSITIVE);
    if (GetAtkRoleForWidget(acc->tkwin) == ATK_ROLE_ENTRY) {
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

/* Function to retrieve text from Tk widget. */
static gchar* tk_get_text(AtkText *text)
{
    TkAtkAccessible *acc = (TkAtkAccessible*)text;
    Tcl_Interp *interp = acc->interp;
    Tk_Window tkwin = acc->tkwin;
    const char *path = acc->path;

    if (!interp || !tkwin || !path) {
        g_warning("Invalid interp, tkwin, or path in tk_get_text");
        return g_strdup("");
    }

    Tcl_DString cmd;
    Tcl_DStringInit(&cmd);
    if (GetAtkRoleForWidget(tkwin) == ATK_ROLE_ENTRY) {
        Tcl_DStringAppend(&cmd, "$w get", -1);
    } else {
        Tcl_DStringAppend(&cmd, "::tk::accessible::_gettext ", -1);
    }
    Tcl_DStringAppend(&cmd, " ", -1);
    Tcl_DStringAppend(&cmd, path, -1);

    if (Tcl_Eval(interp, Tcl_DStringValue(&cmd)) != TCL_OK) {
        g_warning("Failed to execute text retrieval for %s: %s", path, Tcl_GetStringResult(interp));
        Tcl_DStringFree(&cmd);
        return g_strdup("");
    }

    const char *result = Tcl_GetStringResult(interp);
    gchar *ret = g_strdup(result && *result ? result : "");
    Tcl_DStringFree(&cmd);
    return ret;
}

static void tk_atk_text_interface_init(AtkTextIface *iface)
{
    iface->get_text = tk_get_text;
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
	if (!win) {
	    return false;    
	}
	
	/* Retrieve the command string. */
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
 * Functions to initialize and manage the parent Atk class and object instances. 
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

    /* Remove from toplevel and child lists as needed. */
    if (Tk_IsTopLevel(self->tkwin)) {
        toplevel_accessible_objects = g_list_remove(toplevel_accessible_objects, self);
    }
	
    child_widgets = g_list_remove(child_widgets, self);

    /* Free path and call parent finalize. */
    g_free(self->path);

    G_OBJECT_CLASS(tk_atk_accessible_parent_class)->finalize(gobject);
}

static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    AtkObjectClass *atk_class = ATK_OBJECT_CLASS(klass);


    gobject_class->finalize = tk_atk_accessible_finalize;

    /* Map Atk class functions Tk functions. */
    atk_class->get_name = tk_get_name;
    atk_class->get_description = tk_get_description;
    atk_class->get_role = tk_get_role;
    atk_class->ref_state_set = tk_ref_state_set;
    atk_class->get_n_children = tk_get_n_children;
    atk_class->ref_child = tk_ref_child;
}

/* Function to copmlete toplevel registration with proper hierarchy. */
static void RegisterToplevelWindow(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *accessible)
{
    /* Ensure root exists. */
    if (!tk_root_accessible) {
        tk_root_accessible = tk_util_get_root();
    }
    
    /* Set proper parent-child relationship. */
    atk_object_set_parent(accessible, tk_root_accessible);
    
    /* Add to toplevel list. */
    if (!g_list_find(toplevel_accessible_objects, accessible)) {
        toplevel_accessible_objects = g_list_append(toplevel_accessible_objects, accessible);
        
        /* Critical: Emit children-changed signal for AT-SPI update. */
        int index = g_list_length(toplevel_accessible_objects) - 1;
        g_signal_emit_by_name(tk_root_accessible, "children-changed::add", index, accessible);
    }
	
    /* Explicitly set and notify accessible name */
    tk_set_name(accessible);
    g_signal_emit_by_name(accessible, "property-change::accessible-name", NULL);
    
    /* Register child widgets recursively. */
    RegisterChildWidgets(interp, tkwin, accessible);
}

/* 
 * Function to recursively register child widgets using childList.
 */
static void RegisterChildWidgets(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *parent_obj)
{
    TkWindow *winPtr = (TkWindow *)tkwin;
    TkWindow *childPtr;
    int index = 0;

    for (childPtr = winPtr->childList; childPtr != NULL; childPtr = childPtr->nextPtr) {
        Tk_Window child = (Tk_Window)childPtr;
        if (!Tk_WindowId(child)) continue;

        AtkObject *child_obj = GetAtkObjectForTkWindow(child);
        if (!child_obj) {
            child_obj = TkCreateAccessibleAtkObject(interp, child, Tk_PathName(child));
            if (!child_obj) continue;
            
            RegisterAtkObjectForTkWindow(child, child_obj);
            TkAtkAccessible_RegisterEventHandlers(child, (TkAtkAccessible *)child_obj);
        }

        /* Ensure proper parent relationship. */
        AtkObject *current_parent = atk_object_get_parent(child_obj);
        if (current_parent != parent_obj) {
            atk_object_set_parent(child_obj, parent_obj);
            g_signal_emit_by_name(parent_obj, "children-changed::add", index, child_obj);
        }

        /* Recursively register children. */
        RegisterChildWidgets(interp, child, child_obj);
        index++;
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

static AtkObject *tk_util_get_root(void)
{
    if (!tk_root_accessible) {
        tk_root_accessible = g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
        atk_object_initialize(tk_root_accessible, NULL);
        
        /* Set proper application name. */
	atk_object_set_name(tk_root_accessible, "TK Application");
	atk_object_set_role(tk_root_accessible, ATK_ROLE_APPLICATION);
    }
    
    return tk_root_accessible;
}

/* Core function linking Tk objects to the Atk root object and at-spi. */
AtkObject *atk_get_root(void) {
    return tk_util_get_root();
}

/* Atk-Tk object creation with proper parent relationship. */
AtkObject *TkCreateAccessibleAtkObject(Tcl_Interp *interp, Tk_Window tkwin, const char *path)
{
    /* Create a new TkAtkAccessible object. */
    TkAtkAccessible *acc = g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
    acc->interp = interp;
    acc->tkwin = tkwin;
    acc->path = g_strdup(path);


    /* Set initial accessibility properties (role and name). */
    AtkObject *obj = ATK_OBJECT(acc);
    atk_object_set_role(obj, GetAtkRoleForWidget(tkwin));

    /* Set up parent-child relationships for the widget. */
    if (tkwin) {
        Tk_Window parent = Tk_Parent(tkwin);
        AtkObject *parent_obj = parent ? GetAtkObjectForTkWindow(parent) : tk_root_accessible;
        if (parent_obj) {
            atk_object_set_parent(obj, parent_obj);
			
            /* Emit children-changed signal for the parent to update AT-SPI. */
            g_signal_emit_by_name(parent_obj, "children-changed::add", g_list_length(child_widgets), obj);
	}
    }

    /* Add the accessible object to the global list. */
    global_accessible_objects = g_list_prepend(global_accessible_objects, acc);

    return obj;
}


/* 
 * Functions to integrate Tk and Gtk event loops. 
 */

static void GtkEventLoop(void *clientData)
{
    (void) clientData;

    /* One safe, non-blocking iteration. */
    g_main_context_iteration(NULL, FALSE);

    /* Schedule again - run every 10 milliseconds. */
    Tcl_CreateTimerHandler(10, GtkEventLoop, NULL);
}


void InstallGtkEventLoop() {
    Tcl_CreateTimerHandler(10, GtkEventLoop, NULL);
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

/*Helper function for screen reader initialization. */
static gboolean delayed_init(void)
{
	
    /* Re-emit all structure signals for Orca. */
    g_signal_emit_by_name(tk_root_accessible, "children-changed", 0, NULL);
    
    /* Notify that the application is now fully accessible. */
    g_signal_emit_by_name(tk_root_accessible, "state-change", "enabled", TRUE);
    g_signal_emit_by_name(tk_root_accessible, "state-change", "sensitive", TRUE);
    
    return FALSE; 
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

    AtkRole role = atk_object_get_role(acc);
    
    if (!acc) {
        Tcl_AppendResult(ip, "No accessible object for window", NULL);
        return TCL_ERROR;
    }

    GValue gval = G_VALUE_INIT;
    tk_get_current_value(ATK_VALUE(acc), &gval);
    g_signal_emit_by_name(G_OBJECT(acc), "value-changed", &tkvalue);

    if (role == ATK_ROLE_TEXT || role == ATK_ROLE_ENTRY)) {
    g_signal_emit_by_name(acc, "text-selection-changed");
    /* Spin GLib event loop to force processing of notification. */
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
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
  
    /* Emit focus-event with TRUE to indicate focus gained. */
    g_signal_emit_by_name(G_OBJECT(acc), "focus-event", TRUE);
    g_signal_emit_by_name(G_OBJECT(acc), "state-change", "focused", TRUE);
    g_signal_emit_by_name(G_OBJECT(acc), "state-change", ATK_STATE_FOCUSED, TRUE);

    /* Force immediate processing of GLib events. */
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
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

    has_owner = dbus_bus_name_has_owner(connection, "org.gnome.Orca", &error);
    if (!dbus_error_is_set(&error) && has_owner) {
        result = true;
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
 * TkAtkAccessible_RegisterEventHandlers --
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

void TkAtkAccessible_RegisterEventHandlers(Tk_Window tkwin, void *tkAccessible) {
    Tk_CreateEventHandler(tkwin, StructureNotifyMask, 
			  TkAtkAccessible_DestroyHandler, tkAccessible);
    Tk_CreateEventHandler(tkwin, StructureNotifyMask, 
			  TkAtkAccessible_NameHandler, tkAccessible);
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
        if (tkAccessible && tkAccessible->tkwin) {
            /* Notify parent about removal. */
            AtkObject *parent = atk_object_get_parent(ATK_OBJECT(tkAccessible));
            if (parent) {
                g_signal_emit_by_name(parent, "children-changed::remove", -1, ATK_OBJECT(tkAccessible));
            }
            
            /* Unregister and cleanup. */
            UnregisterAtkObjectForTkWindow(tkAccessible->tkwin);
            g_object_unref(tkAccessible);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkAtkAccessible_NameHandler --
 *
 * Update accessible names of Tk widgets.
 *
 * Results:
 *	Accessibility name is updated. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void TkAtkAccessible_NameHandler(ClientData clientData, XEvent *eventPtr) 
{
  
    if (eventPtr->type != ConfigureNotify) {
        return;
    }

    TkAtkAccessible *tkAccessible = (TkAtkAccessible *)clientData;

    AtkObject *atk_obj = (AtkObject*) tkAccessible;
    if (atk_obj) {
        tk_set_name(atk_obj);
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

    /* Register for cleanup and mapping. */
    TkAtkAccessible_RegisterEventHandlers(tkwin, accessible);
    RegisterAtkObjectForTkWindow(tkwin, (AtkObject*)accessible);
    
    /* Handle toplevels specially. */
    if (Tk_IsTopLevel(tkwin)) {
        /* Set window role and proper name. */
        atk_object_set_role(ATK_OBJECT(accessible), ATK_ROLE_WINDOW);
        
        /* Register as toplevel and notify system of creation. */
        RegisterToplevelWindow(interp, tkwin, (AtkObject*)accessible);
        
    } else {
        /* Handle regular widgets */
        if (!g_list_find(child_widgets, (AtkObject*)accessible)) {
            child_widgets = g_list_prepend(child_widgets, (AtkObject*)accessible);
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
    /* Set environment variables for proper AT-SPI operation */
    g_setenv("GTK_MODULES", "gail:atk-bridge", FALSE);
    g_setenv("NO_AT_BRIDGE", "0", FALSE);
    
    /* Initialize Glib type system first. */
    g_type_init();
    
    /* Initialize AT-SPI bridge. */
    if (atk_bridge_adaptor_init(NULL, NULL) != 0) {
	g_warning("Failed to initialize AT-SPI bridge\n");
	return TCL_ERROR;
    }
    
    /* Create and configure root object. */
    tk_root_accessible = tk_util_get_root();
    tk_root_accessible = tk_util_get_root();
    if (tk_root_accessible) {
        const gchar *name = tk_get_name(tk_root_accessible);
        atk_object_set_name(tk_root_accessible, name);
        g_signal_emit_by_name(tk_root_accessible, "property-change::accessible-name", NULL);
    }

    /* Initialize mapping table. */
    InitAtkTkMapping();
    
    /* Register main window with root. */
    Tk_Window mainWin = Tk_MainWindow(interp);
    if (mainWin) {
	RegisterAtkObjectForTkWindow(mainWin, tk_root_accessible);
    }
    
    /* Process pending GLib events to establish AT-SPI connection. */
    int iterations = 0;
    while (g_main_context_iteration(NULL, FALSE) && iterations < 100) {
	iterations++;
    }
    
    /* Install event loop integration. */
    InstallGtkEventLoop();

    /* Add delay for screen reader connection */
    if (IsScreenReaderRunning(NULL, interp, 0, NULL)) {
        g_timeout_add(500, (GSourceFunc)delayed_init, interp);
    } else {
        delayed_init();
    }
    
    /* Register Tcl commands. */
    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", 
			 TkAtkAccessibleObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", 
			 EmitSelectionChanged, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_focus_change", 
			 EmitFocusChanged, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader", 
			 IsScreenReaderRunning, NULL, NULL);

    /* Force initial hierarchy update */
    g_signal_emit_by_name(tk_root_accessible, "children-changed", 0, NULL);
    
    return TCL_OK;
}
#else
/* No Atk found. */
int TkAtkAccessibility_Init(Tcl_Interp *interp)
{
    /* Create empty commands if Atk not available. */
    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", NULL, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", NULL, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_focus__change", NULL, NULL, NULL);
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
