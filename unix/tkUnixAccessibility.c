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
} TkAtkAccessible;

typedef struct _TkAtkAccessibleClass {
    AtkObjectClass parent_class;
} TkAtkAccessibleClass;

static AtkObject *tk_root_accessible = NULL;
static GList *toplevel_accessible_objects = NULL; /* This list will hold refs to toplevels. */
static GHashTable *tk_to_atk_map = NULL; /* Maps Tk_Window to AtkObject. */

/* Atk/Tk glue functions. */
static void tk_get_extents(AtkComponent *component, gint *x, gint *y, gint *width, gint *height, AtkCoordType coord_type);
static gint tk_get_n_children(AtkObject *obj);
static AtkObject *tk_ref_child(AtkObject *obj, guint i);
static AtkRole GetAtkRoleForWidget(Tk_Window win);
static AtkRole tk_get_role(AtkObject *obj);
static const gchar *tk_get_name(AtkObject *obj);
static void tk_set_name(AtkObject *obj, const gchar *name);
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

/* Script-level commands and helper functions. */
static int EmitSelectionChanged(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int EmitFocusChanged(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
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


/* Define custom Atk object bridged to Tcl/Tk. */
#define TK_ATK_TYPE_ACCESSIBLE (tk_atk_accessible_get_type())
G_DEFINE_TYPE_WITH_CODE(TkAtkAccessible, tk_atk_accessible, ATK_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(ATK_TYPE_COMPONENT, tk_atk_component_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_ACTION, tk_atk_action_interface_init)
			G_IMPLEMENT_INTERFACE(ATK_TYPE_VALUE, tk_atk_value_interface_init))

/*
 * Map Atk component interface to Tk.
 */
 
static void tk_get_extents(AtkComponent *component, gint *x, gint *y,gint *width, gint *height, AtkCoordType coord_type)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)component;

    if (!acc || !acc->tkwin) {
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
        /* The root's children are the toplevel windows. */
        return g_list_length(toplevel_accessible_objects);
    }

    if (!acc->tkwin) {
        return 0;
    }

    /* Count direct child windows with accessible objects. */
    int count = 0;
    TkWindow *winPtr = (TkWindow *)acc->tkwin;
    TkWindow *childPtr;
    /* Iterate through Tk's internal child list. */
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

    if (!acc) return NULL;

    if (obj == tk_root_accessible) {
        if (i >= g_list_length(toplevel_accessible_objects)) {
            return NULL;
        }
	/* Get accessible object from toplevel list. */
        AtkObject *child = g_list_nth_data(toplevel_accessible_objects, i);
        if (child) {
            g_object_ref(child); /* Increment ref count as per ATK interface contract. */
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
                g_object_ref(child_obj); /* Increment ref count as per ATK interface contract. */
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

    AtkObject *obj = GetAtkObjectForTkWindow(win);

    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
    AtkRole role = ATK_ROLE_UNKNOWN;

    /* Check if we have accessibility attributes. */
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

    /* Special case for toplevel windows. */
    if (Tk_IsTopLevel(win)) {
	role = ATK_ROLE_WINDOW;
    }
    
    /* Notify system of role attribute. */
    g_object_notify(G_OBJECT(obj), "accessible-role");
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
    gchar *ret = NULL;

    if (!acc) return NULL;

    if (obj == tk_root_accessible) {
        /* For the root, use the cached name or fallback. */
        if (acc->cached_name) {
            return g_strdup(acc->cached_name);
        }
        /* Fallback if cached name not set (should be set during init). */
        return g_strdup("Tk Application");
    }

    if (!acc->tkwin || !acc->interp) {
        return NULL;
    }

    Tcl_DString cmd;
    Tcl_DStringInit(&cmd);

    /* For menus, use entry label as the accessible name. */
    if (GetAtkRoleForWidget(acc->tkwin) == ATK_ROLE_MENU) {
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

    /* For labels, use text content as the accessible name. */
    if (GetAtkRoleForWidget(acc->tkwin) == ATK_ROLE_LABEL) {
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
            /* Fallback to pathname if wm title fails. */
            ret = g_strdup(Tk_PathName(acc->tkwin));
        } else {
            const char *result = Tcl_GetStringResult(acc->interp);
            ret = g_strdup(result && *result ? result : Tk_PathName(acc->tkwin));
        }
        Tcl_DStringFree(&cmd);
        if (ret) return ret;
    }

    /* For other widgets: use accessible name if set. */
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)acc->tkwin);
    if (hPtr) {
	Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
        if (AccessibleAttributes) {
            Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "name");
            if (hPtr2) {
                const char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
                if (result) {
                    return g_strdup(result); /* ATK expects a newly allocated string. */
                }
            }
        }
    }

    /* Default: use window path. */
    if (Tk_PathName(acc->tkwin)) {
        return g_strdup(Tk_PathName(acc->tkwin));
    }
    return NULL;
}

/* Function to set new name if change made. */
static void tk_set_name(AtkObject *obj, const gchar *name)
{
    TkAtkAccessible *acc = (TkAtkAccessible *)obj;

    if (!acc) return; 

    if (obj == tk_root_accessible) {
	/* Free old cached name, store new one. */
        g_free(acc->cached_name);
        acc->cached_name = g_strdup(name); 
    }
    atk_object_set_name(acc, name);
    g_object_notify(G_OBJECT(acc), "accessible-name");
    g_signal_emit_by_name(, "name-changed");
}


static const gchar *tk_get_description(AtkObject *obj)
{
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
        g_object_notify(G_OBJECT(obj), "accessible-description");
        g_signal_emit_by_name(obj, "description-changed");
        return g_strdup(result); /* ATK expects a newly allocated string. */
    }
    return NULL;
}

/*
 * Functions to map accessible value to Atk using
 * AtkValue interface.
 */

static void tk_get_current_value(AtkValue *obj, GValue *value)
{
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
    g_object_notify(G_OBJECT(atkObj), "accessible-value");
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
    if (!acc || !acc->tkwin) return set; 

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
	Tk_Window win = acc->tkwin;
	if (!win) {
	    return FALSE;
	}

	/* Retrieve the command string. */
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

	/* Finally, execute command. */
	if ((Tcl_EvalEx(acc->interp, cmd, -1, TCL_EVAL_GLOBAL)) != TCL_OK) {
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
}

static void tk_atk_accessible_finalize(GObject *gobject)
{
    TkAtkAccessible *self = (TkAtkAccessible*)gobject;

    if (self->tkwin) { 
	/*
	 * Only attempt removal if tkwin is still valid.
	 * Remove from toplevel list if it was a toplevel.
	 */
        if (Tk_IsTopLevel(self->tkwin)) {
            toplevel_accessible_objects = g_list_remove(toplevel_accessible_objects, self);
	    /* 
	     * No need to unref here, as the object is being finalized,
	     * and the list only held a pointer, not an owning ref.
	     * If the list *did* own a ref, it would be unreffed when removed from list.
	     */
        }
        /* Unregister from the Tk_Window to AtkObject map. */
        UnregisterAtkObjectForTkWindow(self->tkwin);
    }

    /* Free path and cached name. */
    g_free(self->path);
    g_free(self->cached_name);

    /* Call parent finalize last. */
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

/* Function to complete toplevel registration with proper hierarchy. */
static void RegisterToplevelWindow(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *accessible)
{
    if (!accessible) return;

    /* Ensure root exists. */
    if (!tk_root_accessible) {
        tk_root_accessible = tk_util_get_root();
    }

    /* Set proper parent-child relationship. */
    atk_object_set_parent(accessible, tk_root_accessible);
    g_object_notify(G_OBJECT(accessible), "accessible-parent");

    /* 
     * Add to toplevel list if not already present.
     * The list now holds a non-owning reference. 
     * Ownership is with tk_to_atk_map. 
     */
    if (!g_list_find(toplevel_accessible_objects, accessible)) {
        toplevel_accessible_objects = g_list_append(toplevel_accessible_objects, accessible);

	/* 
	 * Critical: Emit children-changed signal for AT-SPI update.
	 * The index should be the position where it was added.
	 */
        int index = g_list_length(toplevel_accessible_objects) - 1;
        g_signal_emit_by_name(tk_root_accessible, "children-changed::add", index, accessible);
    }

    /* Explicitly set and notify accessible name */
    const gchar *name = tk_get_name(accessible); 
    if (name) {
        tk_set_name(accessible, name);
        g_object_notify(G_OBJECT(accessible), "accessible-name");
        g_signal_emit_by_name(, "name-changed");
        g_free((gpointer)name); /* Free the string returned by tk_get_name. */
    }

    /* Register child widgets recursively. */
    RegisterChildWidgets(interp, tkwin, accessible);
}

/*
 * Function to recursively register child widgets using childList.
 */
static void RegisterChildWidgets(Tcl_Interp *interp, Tk_Window tkwin, AtkObject *parent_obj)
{
    if (!tkwin || !parent_obj) return;

    TkWindow *winPtr = (TkWindow *)tkwin;
    TkWindow *childPtr;
    int index = 0; /* Track child index for children-changed::add signal. */

    for (childPtr = winPtr->childList; childPtr != NULL; childPtr = childPtr->nextPtr) {
        Tk_Window child = (Tk_Window)childPtr;
        if (!child || !Tk_WindowId(child)) continue; 

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
	    /* 
             * If the child was previously parented to something else or unparented,
             * emit a remove signal from the old parent first if known.
	     * For simplicity, we'll just set the parent and emit 'add' for the new parent.
	     */
            atk_object_set_parent(child_obj, parent_obj);
	    g_object_notify(G_OBJECT(child_obj), "accessible-parent");
            g_signal_emit_by_name(parent_obj, "children-changed::add", index, child_obj);
        }

        /* Set the name for the child object. */
        const gchar *child_name = tk_get_name(child_obj);
        if (child_name) {
            tk_set_name(child_obj, child_name);
            g_object_notify(G_OBJECT(child_obj), "accessible-name");
            g_signal_emit_by_name(child_obj, "name-changed");
            g_free((gpointer)child_name);
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
        TkAtkAccessible *acc = g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
        tk_root_accessible = ATK_OBJECT(acc);
        atk_object_initialize(tk_root_accessible, NULL);

        /* Set proper application name. */
	atk_object_set_role(tk_root_accessible, ATK_ROLE_APPLICATION);
        g_object_notify(G_OBJECT(tk_root_accessible), "accessible-role");

        /* Set an initial name for the root, can be updated later. */
        tk_set_name(tk_root_accessible, "Tk Application");
        g_object_notify(G_OBJECT(tk_root_accessible), "accessible-name");
        g_signal_emit_by_name(, "name-changed");
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
    if (!interp || !tkwin || !path) {
        g_warning("TkCreateAccessibleAtkObject: Invalid interp, tkwin, or path.");
        return NULL;
    }

    /* Create a new TkAtkAccessible object. */
    TkAtkAccessible *acc = g_object_new(TK_ATK_TYPE_ACCESSIBLE, NULL);
    acc->interp = interp;
    acc->tkwin = tkwin;
    acc->path = g_strdup(path); 

    /* Set initial accessibility properties (role and name). */
    AtkObject *obj = ATK_OBJECT(acc);
    atk_object_set_role(obj, GetAtkRoleForWidget(tkwin));
    g_object_notify(G_OBJECT(obj), "accessible-role");

    /* Initial name setting for the object. */
    const gchar *name = tk_get_name(obj);
    if (name) {
        tk_set_name(obj, name);
        g_object_notify(G_OBJECT(obj), "accessible-name");
        g_signal_emit_by_name(obj, "name-changed");
        g_free((gpointer)name); /* Free the string returned by tk_get_name. */
    }

    /* Set up parent-child relationships for the widget. */
    if (tkwin) {
        Tk_Window parent_tkwin = Tk_Parent(tkwin);
        AtkObject *parent_obj = NULL;

        if (parent_tkwin) {
            parent_obj = GetAtkObjectForTkWindow(parent_tkwin);
        } else {
            /* If no Tk parent, it's a toplevel, parent it to the root accessible. */
            parent_obj = tk_root_accessible;
        }

        if (parent_obj) {
            atk_object_set_parent(obj, parent_obj);
            g_object_notify(G_OBJECT(obj), "accessible-parent");
	    /* 
	     * Emit children-changed signal for the parent to update AT-SPI.
	     * The index here is an approximation; a more precise index would require
	     * knowing the exact position in the parent's child list.
	     */
            g_signal_emit_by_name(parent_obj, "children-changed::add", -1, obj);
        }
    }

    /* No longer adding to global_accessible_objects, tk_to_atk_map handles ownership. */
    return obj;
}


/*
 * Functions to integrate Tk and Gtk event loops.
 */

void InstallGtkEventLoop(void) 
{
    GMainContext *context = g_main_context_default();
    if (!context) {
        g_warning("InstallGtkEventLoop: Failed to get default GLib main context");
        return;
    }

    if (!g_main_context_acquire(context)) {
        g_warning("InstallGtkEventLoop: Failed to acquire GLib main context");
        return;
    }

    Tcl_CreateTimerHandler(10, GtkEventLoop, context);
    g_debug("InstallGtkEventLoop: Installed GLib event loop");
}

static void GtkEventLoop(ClientData clientData) 
{
    GMainContext *context = (GMainContext *)clientData;
    if (!context) {
        g_warning("GtkEventLoop: Context is NULL");
        return;
    }

    /* Process all pending events.*/
    int iterations = 0;
    while (g_main_context_pending(context) && iterations < 100) {
        if (!g_main_context_iteration(context, FALSE)) {
            break;
        }
        iterations++;
    }
    if (iterations >= 100) {
        g_warning("GtkEventLoop: Excessive iterations (%d), possible event loop issue", iterations);
    }

    /* Reschedule the event loop. */
    Tcl_CreateTimerHandler(10, GtkEventLoop, clientData);
    g_debug("GtkEventLoop: Processed %d iterations", iterations);
}

/*
 * Functions to map Tk window to its corresponding Atk object.
 */

void InitAtkTkMapping(void)
{
    if (!tk_to_atk_map) {
	/*
	 * Use g_object_unref as the value_destroy_func so objects are unreffed when removed
	 * or when the hash table is destroyed.
	 */
	tk_to_atk_map = g_hash_table_new_full(g_direct_hash, g_direct_equal,
					      NULL, (GDestroyNotify)g_object_unref);
    }
}

void RegisterAtkObjectForTkWindow(Tk_Window tkwin, AtkObject *atkobj)
{
    if (!tkwin || !atkobj) return; 
    InitAtkTkMapping();
    g_object_ref(atkobj); /* Increment ref count because hash table takes ownership. */
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
	
    Tk_Window path_tkwin; 
    path_tkwin = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
    if (path_tkwin == NULL) {
        Tcl_SetResult(ip, "Invalid window path", TCL_STATIC);
	return TCL_ERROR; 
    }

    AtkObject *acc = GetAtkObjectForTkWindow(path_tkwin);

    if (!acc) {
        Tcl_SetResult(ip, "No accessible object for window", TCL_STATIC);
        return TCL_ERROR;
    }

    AtkRole role = atk_object_get_role(acc);

    GValue gval = G_VALUE_INIT;
    tk_get_current_value(ATK_VALUE(acc), &gval);
    g_object_notify(G_OBJECT(acc), "accessible-value");
    g_signal_emit_by_name(G_OBJECT(acc), "value-changed", &gval);
    g_value_unset(&gval); /* Unset the GValue to free any allocated memory. */

    if (role == ATK_ROLE_TEXT || role == ATK_ROLE_ENTRY) {
        g_signal_emit_by_name(acc, "text-selection-changed");
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

    Tk_Window path_tkwin = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
    if (path_tkwin == NULL) {
	Tcl_SetResult(ip, "Invalid window path", TCL_STATIC);
	return TCL_ERROR;
    }

    AtkObject *acc = GetAtkObjectForTkWindow(path_tkwin);
    if (!acc) {
	Tcl_SetResult(ip, "No accessible object for window", TCL_STATIC);
	return TCL_ERROR;
    }

    /* Emit focus-event with TRUE to indicate focus gained. */
    g_signal_emit_by_name(G_OBJECT(acc), "focus-event", TRUE);
    g_signal_emit_by_name(G_OBJECT(acc), "state-change", "focused", TRUE);

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
    /* If output exists, Orca is running. */
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
 * TkAtkAccessible_RegisterEventHandlers --
 *
 * Register event handler for destroying accessibility element.
 *
 * Results:
 * Event handler is registered.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void TkAtkAccessible_RegisterEventHandlers(Tk_Window tkwin, void *tkAccessible) {
    if (!tkwin || !tkAccessible) return; 
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
		/* The index parameter for children-changed::remove is often ignored or -1.*/
		g_signal_emit_by_name(parent, "children-changed::remove", -1, ATK_OBJECT(tkAccessible));
	    }

	    /* 
	     * Unregister and cleanup. g_object_unref will trigger finalize, 
	     * which handles unregistering from map. 
	     */
	    g_object_unref(tkAccessible); /* This will decrement ref count and call finalize when it reaches 0. */
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
    if (!tkAccessible || !tkAccessible->tkwin) return;

    AtkObject *atk_obj = (AtkObject*) tkAccessible;
    if (atk_obj) {
	/* Get the current name and then set it to trigger notification. */
	const gchar *name = tk_get_name(atk_obj);
	if (name) {
	    tk_set_name(atk_obj, name);
	    g_object_notify(G_OBJECT(atk_obj), "accessible-name");
	    g_signal_emit_by_name(_obj, "name-changed");
	    g_free((gpointer)name); /* Free the string returned by tk_get_name. */
	}
    }
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

    /* Register for cleanup and mapping. */
    TkAtkAccessible_RegisterEventHandlers(tkwin, accessible);

    /* Handle toplevels specially. */
    if (Tk_IsTopLevel(tkwin)) {
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
int TkAtkAccessibility_Init(Tcl_Interp *interp)
{
    /* Set environment variables for proper AT-SPI operation. */
    g_setenv("GTK_MODULES", "gail:atk-bridge", FALSE);
    g_setenv("NO_AT_BRIDGE", "0", FALSE);

    /* Initialize Glib type system first. */
    g_type_init();

    /* Create and configure root object. */
    tk_root_accessible = tk_util_get_root();
    if (tk_root_accessible) {
	const gchar *name = tk_get_name(tk_root_accessible);
	if (name) {
	    tk_set_name(tk_root_accessible, name); /* Set the name and notify. */
	    g_object_notify(G_OBJECT(tk_root_accessible), "accessible-name");
	    g_signal_emit_by_name(tk_root_accessible, "name-changed");
	    g_free((gpointer)name); /* Free the string returned by tk_get_name. */
	}
    }

    /* Initialize AT-SPI bridge. */
    if (atk_bridge_adaptor_init(NULL, NULL) != 0) {
	g_warning("Failed to initialize AT-SPI bridge\n");
	return TCL_ERROR;
    }

    /* Initialize mapping table. */
    InitAtkTkMapping();

    /* Register main window with root. */
    Tk_Window mainWin = Tk_MainWindow(interp);
    if (mainWin) {
	/* 
	 * Create accessible object for main window, which will also register it
	 * and set its parent to the root.
	 */
	TkAtkAccessible *main_acc = (TkAtkAccessible*) TkCreateAccessibleAtkObject(interp, mainWin, Tk_PathName(mainWin));
	if (main_acc) {
	    /* Ensure the main window is treated as a toplevel for ATK hierarchy. */
	    RegisterToplevelWindow(interp, mainWin, (AtkObject*)main_acc);
	}
    }

    /* 
     * Process pending GLib events to establish AT-SPI connection. 
     * Limiting iterations to prevent blocking.
     */
    int iterations = 0;
    while (g_main_context_pending(NULL) && iterations < 100) {
	iterations++;
    }

    /* Install event loop integration. */
    InstallGtkEventLoop();

    /* Register Tcl commands. */
    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object",
			 TkAtkAccessibleObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change",
			 EmitSelectionChanged, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_focus_change",
			 EmitFocusChanged, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader",
			 IsScreenReaderRunning, NULL, NULL);

    /* 
     * Force initial hierarchy update. 
     */
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

