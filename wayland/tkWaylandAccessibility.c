/*
 * tkWaylandAccessibility.c --
 *
 * This file implements accessibility/screen-reader support
 * for Tk on Wayland systems using direct at-spi access via sd-bus.
 * It replaces the ATK-based implementation used on X11.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 2006, Marcus von Appen
 * Copyright (c) 2019-2026 Kevin Walzer
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
#include <systemd/sd-bus.h>
#include <wayland-client.h>
#include "tkInt.h"
#include "tkGlfwInt.h"

/*
 *----------------------------------------------------------------------
 *
 * at-spi definitions. 
 *
 *----------------------------------------------------------------------
 */

/* at-spi D-Bus constants. */
#define ATSPI_DBUS_NAME           "org.a11y.Bus"
#define ATSPI_DBUS_PATH           "/org/a11y/bus"
#define ATSPI_REGISTRY_INTERFACE  "org.a11y.atspi.Registry"
#define ATSPI_ACCESSIBLE_INTERFACE "org.a11y.atspi.Accessible"
#define ATSPI_ACTION_INTERFACE    "org.a11y.atspi.Action"
#define ATSPI_COMPONENT_INTERFACE "org.a11y.atspi.Component"
#define ATSPI_VALUE_INTERFACE     "org.a11y.atspi.Value"
#define ATSPI_TEXT_INTERFACE      "org.a11y.atspi.Text"
#define ATSPI_SELECTION_INTERFACE "org.a11y.atspi.Selection"
#define ATSPI_EVENT_INTERFACE     "org.a11y.atspi.Event"
#define ATSPI_CACHE_INTERFACE     "org.a11y.atspi.Cache"

/* at-spi role constants. */
#define ATSPI_ROLE_INVALID           0
#define ATSPI_ROLE_APPLICATION       1
#define ATSPI_ROLE_WINDOW            2
#define ATSPI_ROLE_PUSH_BUTTON       3
#define ATSPI_ROLE_CHECK_BOX         4
#define ATSPI_ROLE_RADIO_BUTTON      5
#define ATSPI_ROLE_ENTRY             6
#define ATSPI_ROLE_LABEL             7
#define ATSPI_ROLE_LIST_BOX          8
#define ATSPI_ROLE_COMBO_BOX         9
#define ATSPI_ROLE_MENU              10
#define ATSPI_ROLE_MENU_BAR          11
#define ATSPI_ROLE_TREE              12
#define ATSPI_ROLE_PAGE_TAB          13
#define ATSPI_ROLE_PROGRESS_BAR      14
#define ATSPI_ROLE_SLIDER            15
#define ATSPI_ROLE_SPIN_BUTTON       16
#define ATSPI_ROLE_TREE_TABLE        17
#define ATSPI_ROLE_TEXT              18
#define ATSPI_ROLE_PANEL             19
#define ATSPI_ROLE_CANVAS            20
#define ATSPI_ROLE_SCROLL_BAR        21
#define ATSPI_ROLE_TOGGLE_BUTTON     22

/* at-spi state constants (bit flags). */
#define ATSPI_STATE_ENABLED          (1ULL << 0)
#define ATSPI_STATE_SENSITIVE        (1ULL << 1)
#define ATSPI_STATE_FOCUSABLE        (1ULL << 2)
#define ATSPI_STATE_FOCUSED          (1ULL << 3)
#define ATSPI_STATE_VISIBLE          (1ULL << 4)
#define ATSPI_STATE_SHOWING          (1ULL << 5)
#define ATSPI_STATE_EDITABLE         (1ULL << 6)
#define ATSPI_STATE_CHECKED          (1ULL << 7)
#define ATSPI_STATE_SELECTABLE       (1ULL << 8)
#define ATSPI_STATE_SELECTED         (1ULL << 9)
#define ATSPI_STATE_ACTIVE           (1ULL << 10)
#define ATSPI_STATE_EXPANDABLE       (1ULL << 11)
#define ATSPI_STATE_EXPANDED         (1ULL << 12)

/* at-spi event types. */
#define ATSPI_EVENT_FOCUS             "focus"
#define ATSPI_EVENT_STATE_CHANGED     "state-changed"
#define ATSPI_EVENT_VALUE_CHANGED     "value-changed"
#define ATSPI_EVENT_TEXT_CHANGED      "text-changed"
#define ATSPI_EVENT_SELECTION_CHANGED "selection-changed"
#define ATSPI_EVENT_WINDOW_ACTIVATE   "window:activate"
#define ATSPI_EVENT_WINDOW_DEACTIVATE "window:deactivate"
#define ATSPI_EVENT_WINDOW_CREATE     "window:create"
#define ATSPI_EVENT_CHILDREN_CHANGED  "children-changed"
#define ATSPI_EVENT_ACTIVE_DESCENDANT "active-descendant-changed"


/*
 *----------------------------------------------------------------------
 *
 * Core structures for Tk accessibility and at-spi data. 
 *
 *----------------------------------------------------------------------
 */

/* Core data type. */
typedef struct TkAccessible TkAccessible;

/* Simple linked list for accessible children. */
typedef struct AccessibleList {
    TkAccessible *acc;
    struct AccessibleList *next;
} AccessibleList;

/* Main accessible object structure. */
struct TkAccessible {
    Tk_Window tkwin;
    Tcl_Interp *interp;
    char *path;
    int role;
    uint64_t states;
    int x, y, width, height;
    int is_focused;
    int ref_count;
    
    /* D-Bus object path for this accessible. */
    char *dbus_path;
    
    /* Parent and children tracking. */
    struct TkAccessible *parent;
    AccessibleList *children;        /* For virtual children only. */
    
    /* Virtual child support. */
    int is_virtual;
    int virtual_index;
    char *virtual_name;
    struct TkAccessible *virtual_parent;
    
    /* D-Bus slot for this object (for cleanup) */
    sd_bus_slot *vtable_slot;
};

/* Global connection state. */
typedef struct {
    sd_bus *bus;
    int is_initialized;
    Tcl_HashTable *tk_to_accessible_map;   /* key = Tk_Window, value = TkAccessible*. */
    AccessibleList *toplevel_accessibles;
    TkAccessible *root_accessible;
    
    /* For Tcl event loop integration. */
    int bus_fd;
    int file_handler;    /* dummy, just to know it's set */
} AtspiConnection;

/*
 *----------------------------------------------------------------------
 *
 * Forward declarations of functions defined in this file.  
 *
 *----------------------------------------------------------------------
 */

/* Core functions. */
static void EnsureAccessibleInHierarchy(Tcl_Interp *interp, Tk_Window tkwin);
static TkAccessible *CreateAccessible(Tcl_Interp *interp, Tk_Window tkwin, const char *path);
static void RegisterAccessible(Tk_Window tkwin, TkAccessible *acc);
static TkAccessible *GetAccessible(Tk_Window tkwin);
static void UnregisterAccessible(Tk_Window tkwin);
static void FreeAccessible(TkAccessible *acc);
static int GetRoleForWidget(Tk_Window tkwin);
static uint64_t ComputeStateForWidget(TkAccessible *acc);
static char *GetNameForWidget(Tk_Window tkwin);
static char *GetDescriptionForWidget(Tk_Window tkwin);
static char *GetValueForWidget(Tk_Window tkwin);
static Tk_Window GetToplevelOfWidget(Tk_Window tkwin);
static void RegisterToplevel(TkAccessible *acc);
static void UnregisterToplevel(TkAccessible *acc);
static void RegisterWidgetRecursive(Tcl_Interp *interp, Tk_Window tkwin);
static void UpdateFocusChain(Tk_Window focused);
static char *Tcl_Strdup(const char *s);

/* D-Bus vtables and method handlers. */
static const sd_bus_vtable accessible_vtable[];
static const sd_bus_vtable component_vtable[];
static const sd_bus_vtable action_vtable[];
static const sd_bus_vtable value_vtable[];
static const sd_bus_vtable text_vtable[];
static const sd_bus_vtable selection_vtable[];

/* D-Bus methods for getting at-spi child, attribute and state management. */
static int dbus_method_get_children(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_child_at_index(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_attributes(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_states(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_role(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_name(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_description(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_parent(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_grab_focus(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_index_in_parent(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_interfaces(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);

/* at-spi component interface. */
static int dbus_method_component_get_extents(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_component_get_position(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_component_get_size(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_component_contains(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_component_grab_focus(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);

/* at-spi action interface. */
static int dbus_method_action_get_n_actions(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_action_do_action(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_action_get_name(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_action_get_description(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_action_get_key_binding(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);

/* at-spi value interface. */
static int dbus_method_value_get_current(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_value_get_minimum(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_value_get_maximum(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_value_set_current(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);

/* at-spi text interface. */
static int dbus_method_text_get_text(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_text_get_caret_offset(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_text_get_character_count(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);

/* at-spi selection interface. */
static int dbus_method_selection_get_n_selections(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_selection_get_selection(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_selection_is_selected(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_selection_select_all(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_selection_clear_selection(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_selection_add_selection(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_selection_remove_selection(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);

/* Event emission. */
static void SendAtspiEvent(TkAccessible *acc, const char *event_type, const char *detail);
static void SendChildrenChanged(TkAccessible *parent, int index, TkAccessible *child, int added);
static void SendStateChanged(TkAccessible *acc, uint64_t state, int value);
static void SendActiveDescendantChanged(TkAccessible *container, TkAccessible *descendant);

/* X Event handlers. */
static void TkAccessible_DestroyHandler(void *clientData, XEvent *eventPtr);
static void TkAccessible_FocusHandler(void *clientData, XEvent *eventPtr);
static void TkAccessible_CreateHandler(void *clientData, XEvent *eventPtr);
static void TkAccessible_ConfigureHandler(void *clientData, XEvent *eventPtr);
static void TkAccessible_RegisterEventHandlers(Tk_Window tkwin, TkAccessible *acc);

/* Tcl event loop integration. */
static void BusFileHandlerProc(ClientData clientData, int mask);
static void TclEventSetupProc(ClientData clientData, int flags);
static void TclEventCheckProc(ClientData clientData, int flags);

/* Tcl command implementations. */
static int AddAccessibleCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int EmitSelectionChangedCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int EmitFocusChangedCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int IsScreenReaderRunningCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

/* Screen reader detection. */
static int IsScreenReaderActive(void);

/* Role mapping table. */
typedef struct {
    const char *tkrole;
    int atspi_role;
} AtspiRoleMap;

static const AtspiRoleMap roleMap[] = {
    {"Button",        ATSPI_ROLE_PUSH_BUTTON},
    {"Checkbox",      ATSPI_ROLE_CHECK_BOX},
    {"Combobox",      ATSPI_ROLE_COMBO_BOX},
    {"Entry",         ATSPI_ROLE_ENTRY},
    {"Label",         ATSPI_ROLE_LABEL},
    {"Listbox",       ATSPI_ROLE_LIST_BOX},
    {"Menu",          ATSPI_ROLE_MENU},
    {"Menubar",       ATSPI_ROLE_MENU_BAR},
    {"Tree",          ATSPI_ROLE_TREE},
    {"Notebook",      ATSPI_ROLE_PAGE_TAB},
    {"Progressbar",   ATSPI_ROLE_PROGRESS_BAR},
    {"Radiobutton",   ATSPI_ROLE_RADIO_BUTTON},
    {"Scale",         ATSPI_ROLE_SLIDER},
    {"Spinbox",       ATSPI_ROLE_SPIN_BUTTON},
    {"Table",         ATSPI_ROLE_TREE_TABLE},
    {"Text",          ATSPI_ROLE_TEXT},
    {"Toplevel",      ATSPI_ROLE_WINDOW},
    {"Frame",         ATSPI_ROLE_PANEL},
    {"Canvas",        ATSPI_ROLE_CANVAS},
    {"Scrollbar",     ATSPI_ROLE_SCROLL_BAR},
    {"Toggleswitch",  ATSPI_ROLE_TOGGLE_BUTTON},
    {NULL,            ATSPI_ROLE_INVALID}
};

static AtspiConnection *atspi_conn = NULL;
extern Tcl_HashTable *TkAccessibilityObject;  /* from tkAccessibility.c */

/*
 * D-Bus vtables - these map functions to the ati-spi API. 
 */

/* org.a11y.atspi.Accessible interface. */
static const sd_bus_vtable accessible_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetChildren", "", "a(so)", dbus_method_get_children, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetChildAtIndex", "i", "(so)", dbus_method_get_child_at_index, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetAttributes", "", "a{ss}", dbus_method_get_attributes, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetStates", "", "t", dbus_method_get_states, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetRole", "", "i", dbus_method_get_role, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetName", "", "s", dbus_method_get_name, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetDescription", "", "s", dbus_method_get_description, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetParent", "", "(so)", dbus_method_get_parent, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GrabFocus", "", "b", dbus_method_grab_focus, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetIndexInParent", "", "i", dbus_method_get_index_in_parent, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetInterfaces", "", "as", dbus_method_get_interfaces, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

/* org.a11y.atspi.Component interface. */
static const sd_bus_vtable component_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetExtents", "i", "(iiii)", dbus_method_component_get_extents, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetPosition", "i", "(ii)", dbus_method_component_get_position, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetSize", "", "(ii)", dbus_method_component_get_size, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Contains", "iii", "b", dbus_method_component_contains, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GrabFocus", "", "b", dbus_method_component_grab_focus, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

/* org.a11y.atspi.Action interface. */
static const sd_bus_vtable action_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetNActions", "", "i", dbus_method_action_get_n_actions, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("DoAction", "i", "b", dbus_method_action_do_action, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetName", "i", "s", dbus_method_action_get_name, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetDescription", "i", "s", dbus_method_action_get_description, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetKeyBinding", "i", "s", dbus_method_action_get_key_binding, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

/* org.a11y.atspi.Value interface. */
static const sd_bus_vtable value_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetCurrentValue", "", "d", dbus_method_value_get_current, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetMinimumValue", "", "d", dbus_method_value_get_minimum, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetMaximumValue", "", "d", dbus_method_value_get_maximum, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetCurrentValue", "d", "b", dbus_method_value_set_current, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

/* org.a11y.atspi.Text interface (minimal). */
static const sd_bus_vtable text_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetText", "ii", "s", dbus_method_text_get_text, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetCaretOffset", "", "i", dbus_method_text_get_caret_offset, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetCharacterCount", "", "i", dbus_method_text_get_character_count, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

/* org.a11y.atspi.Selection interface. */
static const sd_bus_vtable selection_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetNSelections", "", "i", dbus_method_selection_get_n_selections, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetSelection", "i", "(so)", dbus_method_selection_get_selection, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("IsChildSelected", "i", "b", dbus_method_selection_is_selected, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SelectAll", "", "b", dbus_method_selection_select_all, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ClearSelection", "", "b", dbus_method_selection_clear_selection, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AddSelection", "i", "b", dbus_method_selection_add_selection, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("RemoveSelection", "i", "b", dbus_method_selection_remove_selection, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};


/* Convenience function. */
static char *
Tcl_Strdup(const char *s)
{
    if (s == NULL) return NULL;
    return strcpy((char *) Tcl_Alloc(strlen(s) + 1), s);
}

/*
 *----------------------------------------------------------------------
 *
 * D-Bus interface functions. These do the heavy lifting of mapping Tk to 
 * at-spi functionality. 
 *
 *----------------------------------------------------------------------
 */

/*
 * Accessible children, attributes and state. Here we create create accessible
 * objects from native Tk widgets (buttons, entries, etc.), map them them to
 * the appropriate role, and track them.
 */
 
static int dbus_method_get_children(
	sd_bus_message *m, 
	void *userdata,
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    sd_bus_message_open_container(reply, 'a', "(so)");

    if (acc == atspi_conn->root_accessible) {
        /* Root returns toplevel windows. */
        AccessibleList *l;
        for (l = atspi_conn->toplevel_accessibles; l != NULL; l = l->next) {
            TkAccessible *top = l->acc;
            if (top && top->dbus_path) {
                sd_bus_message_append(reply, "(so)", top->dbus_path, ATSPI_ACCESSIBLE_INTERFACE);
            }
        }
    } else if (acc->tkwin && !acc->is_virtual) {
        /* Real Tk widget children. */
        TkWindow *childPtr;
        for (childPtr = ((TkWindow*)acc->tkwin)->childList;
             childPtr != NULL;
             childPtr = childPtr->nextPtr) {
            TkAccessible *child_acc = GetAccessible((Tk_Window)childPtr);
            if (child_acc && child_acc->dbus_path) {
                sd_bus_message_append(reply, "(so)", child_acc->dbus_path, ATSPI_ACCESSIBLE_INTERFACE);
            }
        }
    } else {
        /* Virtual children. */
        AccessibleList *l;
        for (l = acc->children; l != NULL; l = l->next) {
            TkAccessible *child = l->acc;
            if (child && child->dbus_path) {
                sd_bus_message_append(reply, "(so)", child->dbus_path, ATSPI_ACCESSIBLE_INTERFACE);
            }
        }
    }

    sd_bus_message_close_container(reply);
    return sd_bus_send(NULL, reply, NULL);
}

static int dbus_method_get_child_at_index(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t index;
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_read(m, "i", &index);
    if (r < 0) return r;

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    if (acc == atspi_conn->root_accessible) {
        AccessibleList *l = atspi_conn->toplevel_accessibles;
        int i = 0;
        while (l && i < index) {
            l = l->next;
            i++;
        }
        if (l) {
            TkAccessible *top = l->acc;
            sd_bus_message_append(reply, "(so)", top->dbus_path, ATSPI_ACCESSIBLE_INTERFACE);
        } else {
            sd_bus_message_append(reply, "(so)", "", "");
        }
    } else if (acc->tkwin && !acc->is_virtual) {
        TkWindow *childPtr;
        int i = 0;
        for (childPtr = ((TkWindow*)acc->tkwin)->childList;
             childPtr != NULL;
             childPtr = childPtr->nextPtr, i++) {
            if (i == index) {
                TkAccessible *child_acc = GetAccessible((Tk_Window)childPtr);
                if (child_acc && child_acc->dbus_path) {
                    sd_bus_message_append(reply, "(so)", child_acc->dbus_path, ATSPI_ACCESSIBLE_INTERFACE);
                } else {
                    sd_bus_message_append(reply, "(so)", "", "");
                }
                break;
            }
        }
        if (!childPtr) {
            sd_bus_message_append(reply, "(so)", "", "");
        }
    } else {
        AccessibleList *l = acc->children;
        int i = 0;
        while (l && i < index) {
            l = l->next;
            i++;
        }
        if (l) {
            TkAccessible *child = l->acc;
            sd_bus_message_append(reply, "(so)", child->dbus_path, ATSPI_ACCESSIBLE_INTERFACE);
        } else {
            sd_bus_message_append(reply, "(so)", "", "");
        }
    }

    return sd_bus_send(NULL, reply, NULL);
}

static int dbus_method_get_attributes(
sd_bus_message *m, 
	TCL_UNUSED(void *), /* userdata */
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    sd_bus_message_open_container(reply, 'a', "{ss}");
    /* No custom attributes for now. */
    sd_bus_message_close_container(reply);
    return sd_bus_send(NULL, reply, NULL);
}

static int dbus_method_get_states(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    uint64_t states = ComputeStateForWidget(acc);
    sd_bus_message_append(reply, "t", states);
    return sd_bus_send(NULL, reply, NULL);
}

static int dbus_method_get_role(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    sd_bus_message_append(reply, "i", acc->role);
    return sd_bus_send(NULL, reply, NULL);
}

static int dbus_method_get_name(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    sd_bus_message *reply = NULL;
    int r;
    char *name = NULL;

    if (acc->is_virtual && acc->virtual_name) {
        name = Tcl_Strdup(acc->virtual_name);
    } else if (acc->tkwin) {
        name = GetNameForWidget(acc->tkwin);
    }

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) {
        if (name) Tcl_Free(name);
        return r;
    }

    sd_bus_message_append(reply, "s", name ? name : "");
    if (name) Tcl_Free(name);
    return sd_bus_send(NULL, reply, NULL);
}

static int dbus_method_get_description(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    sd_bus_message *reply = NULL;
    int r;
    char *desc = NULL;

    if (acc->tkwin) {
        desc = GetDescriptionForWidget(acc->tkwin);
    }

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) {
        if (desc) Tcl_Free(desc);
        return r;
    }

    sd_bus_message_append(reply, "s", desc ? desc : "");
    if (desc) Tcl_Free(desc);
    return sd_bus_send(NULL, reply, NULL);
}

static int dbus_method_get_parent(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    if (acc->parent && acc->parent->dbus_path) {
        sd_bus_message_append(reply, "(so)", acc->parent->dbus_path, ATSPI_ACCESSIBLE_INTERFACE);
    } else {
        sd_bus_message_append(reply, "(so)", "", "");
    }
    return sd_bus_send(NULL, reply, NULL);
}

static int dbus_method_grab_focus(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    sd_bus_message *reply = NULL;
    int r;

    if (!acc || !acc->tkwin || !acc->interp) {
        return sd_bus_reply_method_return(m, "b", 0);
    }

    /* Actually give Tk focus to the widget. */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "focus -force %s", Tk_PathName(acc->tkwin));
    Tcl_Eval(acc->interp, cmd);

    /* Update internal state */
    acc->is_focused = 1;
    acc->states |= ATSPI_STATE_FOCUSED;

    /* Send focus event */
    SendAtspiEvent(acc, ATSPI_EVENT_FOCUS, NULL);

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    sd_bus_message_append(reply, "b", 1);
    return sd_bus_send(NULL, reply, NULL);
}

static int dbus_method_get_index_in_parent(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int index = -1;

    if (acc->parent && acc->parent->children) {
        AccessibleList *l = acc->parent->children;
        int i = 0;
        while (l) {
            if (l->acc == acc) {
                index = i;
                break;
            }
            l = l->next;
            i++;
        }
    } else if (acc->tkwin && acc->parent && acc->parent->tkwin) {
        /* Real Tk child: compute index from parent's child list. */
        TkWindow *childPtr;
        int i = 0;
        for (childPtr = ((TkWindow*)acc->parent->tkwin)->childList;
             childPtr != NULL;
             childPtr = childPtr->nextPtr, i++) {
            if ((Tk_Window)childPtr == acc->tkwin) {
                index = i;
                break;
            }
        }
    }

    return sd_bus_reply_method_return(m, "i", index);
}

static int dbus_method_get_interfaces(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    sd_bus_message_open_container(reply, 'a', "s");
    sd_bus_message_append(reply, "s", ATSPI_ACCESSIBLE_INTERFACE);
    sd_bus_message_append(reply, "s", ATSPI_COMPONENT_INTERFACE);

    int role = acc->role;
    if (role == ATSPI_ROLE_PUSH_BUTTON || role == ATSPI_ROLE_CHECK_BOX ||
        role == ATSPI_ROLE_RADIO_BUTTON || role == ATSPI_ROLE_TOGGLE_BUTTON) {
        sd_bus_message_append(reply, "s", ATSPI_ACTION_INTERFACE);
    }
    if (role == ATSPI_ROLE_SPIN_BUTTON || role == ATSPI_ROLE_SLIDER ||
        role == ATSPI_ROLE_PROGRESS_BAR || role == ATSPI_ROLE_SCROLL_BAR) {
        sd_bus_message_append(reply, "s", ATSPI_VALUE_INTERFACE);
    }
    if (role == ATSPI_ROLE_ENTRY || role == ATSPI_ROLE_TEXT) {
        sd_bus_message_append(reply, "s", ATSPI_TEXT_INTERFACE);
    }
    if (role == ATSPI_ROLE_LIST_BOX || role == ATSPI_ROLE_TREE ||
        role == ATSPI_ROLE_TREE_TABLE) {
        sd_bus_message_append(reply, "s", ATSPI_SELECTION_INTERFACE);
    }
    sd_bus_message_close_container(reply);
    return sd_bus_send(NULL, reply, NULL);
}

/*
 * at-spi component interface. This tracks widget location/geometry.
 */

static int dbus_method_component_get_extents(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t coord_type;
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_read(m, "i", &coord_type);
    if (r < 0) return r;

    if (!acc->tkwin) {
        return sd_bus_reply_method_return(m, "(iiii)", 0, 0, 0, 0);
    }

    int x, y, w, h;
    Tk_GetRootCoords(acc->tkwin, &x, &y);
    w = Tk_Width(acc->tkwin);
    h = Tk_Height(acc->tkwin);

    if (coord_type == 1) { /* ATSPI_XY_WINDOW: relative to parent */
        Tk_Window top = GetToplevelOfWidget(acc->tkwin);
        int tx, ty;
        Tk_GetRootCoords(top, &tx, &ty);
        x -= tx;
        y -= ty;
    }

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    sd_bus_message_append(reply, "(iiii)", x, y, w, h);
    return sd_bus_send(NULL, reply, NULL);
}

static int dbus_method_component_get_position(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t coord_type;
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_read(m, "i", &coord_type);
    if (r < 0) return r;

    if (!acc->tkwin) {
        return sd_bus_reply_method_return(m, "(ii)", 0, 0);
    }

    int x, y;
    Tk_GetRootCoords(acc->tkwin, &x, &y);

    if (coord_type == 1) {
        Tk_Window top = GetToplevelOfWidget(acc->tkwin);
        int tx, ty;
        Tk_GetRootCoords(top, &tx, &ty);
        x -= tx;
        y -= ty;
    }

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    sd_bus_message_append(reply, "(ii)", x, y);
    return sd_bus_send(NULL, reply, NULL);
}

static int dbus_method_component_get_size(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;

    if (!acc->tkwin) {
        return sd_bus_reply_method_return(m, "(ii)", 0, 0);
    }

    int w = Tk_Width(acc->tkwin);
    int h = Tk_Height(acc->tkwin);
    return sd_bus_reply_method_return(m, "(ii)", w, h);
}

static int dbus_method_component_contains(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t x, y, coord_type;
    int r;

    r = sd_bus_message_read(m, "iii", &x, &y, &coord_type);
    if (r < 0) return r;

    if (!acc->tkwin) {
        return sd_bus_reply_method_return(m, "b", 0);
    }

    int comp_x, comp_y, comp_w, comp_h;
    Tk_GetRootCoords(acc->tkwin, &comp_x, &comp_y);
    comp_w = Tk_Width(acc->tkwin);
    comp_h = Tk_Height(acc->tkwin);

    if (coord_type == 1) {
        Tk_Window top = GetToplevelOfWidget(acc->tkwin);
        int tx, ty;
        Tk_GetRootCoords(top, &tx, &ty);
        comp_x -= tx;
        comp_y -= ty;
    }

    int contains = (x >= comp_x && x < comp_x + comp_w &&
                    y >= comp_y && y < comp_y + comp_h);
    return sd_bus_reply_method_return(m, "b", contains);
}

static int dbus_method_component_grab_focus(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    return dbus_method_grab_focus(m, userdata, ret_error); /* same as Accessible.GrabFocus */
}

/*
 * ati-spi / D-Bus method implementations: Action interface for button presses, menu invocation, etc.
*/

static int dbus_method_action_get_n_actions(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int n_actions = 0;

    if (acc->tkwin) {
        int role = GetRoleForWidget(acc->tkwin);
        switch (role) {
            case ATSPI_ROLE_PUSH_BUTTON:
            case ATSPI_ROLE_CHECK_BOX:
            case ATSPI_ROLE_RADIO_BUTTON:
            case ATSPI_ROLE_TOGGLE_BUTTON:
                n_actions = 1;
                break;
        }
    }

    return sd_bus_reply_method_return(m, "i", n_actions);
}

static int dbus_method_action_do_action(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t index;
    int r;

    r = sd_bus_message_read(m, "i", &index);
    if (r < 0) return r;

    if (!acc->tkwin || !acc->interp || index != 0) {
        return sd_bus_reply_method_return(m, "b", 0);
    }

    /* Call the widget's invoke method */
    Tcl_Obj *cmd[2];
    cmd[0] = Tcl_NewStringObj(Tk_PathName(acc->tkwin), -1);
    cmd[1] = Tcl_NewStringObj("invoke", -1);

    Tcl_IncrRefCount(cmd[0]);
    Tcl_IncrRefCount(cmd[1]);

    int result = Tcl_EvalObjv(acc->interp, 2, cmd, TCL_EVAL_GLOBAL);

    Tcl_DecrRefCount(cmd[0]);
    Tcl_DecrRefCount(cmd[1]);

    if (result != TCL_OK) {
        Tcl_ResetResult(acc->interp);
        return sd_bus_reply_method_return(m, "b", 0);
    }

    /* Send state change for toggleable widgets. */
    int role = GetRoleForWidget(acc->tkwin);
    if (role == ATSPI_ROLE_CHECK_BOX || role == ATSPI_ROLE_RADIO_BUTTON) {
        SendStateChanged(acc, ATSPI_STATE_CHECKED, 1); /* value recomputed inside */
    }

    return sd_bus_reply_method_return(m, "b", 1);
}

static int dbus_method_action_get_name(
	sd_bus_message *m, 
	void *userdata, 
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t index;
    int r;
    const char *action_name = NULL;

    r = sd_bus_message_read(m, "i", &index);
    if (r < 0) return r;

    if (index == 0 && acc->tkwin) {
        int role = GetRoleForWidget(acc->tkwin);
        switch (role) {
            case ATSPI_ROLE_PUSH_BUTTON:
                action_name = "press";
                break;
            case ATSPI_ROLE_CHECK_BOX:
            case ATSPI_ROLE_RADIO_BUTTON:
            case ATSPI_ROLE_TOGGLE_BUTTON:
                action_name = "toggle";
                break;
        }
    }

    return sd_bus_reply_method_return(m, "s", action_name ? action_name : "");
}

static int dbus_method_action_get_description(
	sd_bus_message *m, 
	TCL_UNUSED(void *), /* userdata */
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    return sd_bus_reply_method_return(m, "s", "");
}

static int dbus_method_action_get_key_binding(
	sd_bus_message *m, 
	TCL_UNUSED(void *), /* userdata */
	TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "s", "");
}

/*
 * at-spi/D-Bus method implementations: Value interface. Returns float. 
*/

static int dbus_method_value_get_current(
    sd_bus_message *m, 
    void *userdata, 
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;

    if (!acc->tkwin) {
        return sd_bus_reply_method_return(m, "d", 0.0);
    }

    char *val_str = GetValueForWidget(acc->tkwin);
    double value = val_str ? atof(val_str) : 0.0;
    if (val_str) Tcl_Free(val_str);
    return sd_bus_reply_method_return(m, "d", value);
}

static int dbus_method_value_get_minimum(
    sd_bus_message *m, 
    void *userdata, 
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    double min_val = 0.0;
    char cmd[256];

    if (acc->tkwin && acc->interp) {
        snprintf(cmd, sizeof(cmd), "%s cget -from", Tk_PathName(acc->tkwin));
        if (Tcl_Eval(acc->interp, cmd) == TCL_OK) {
            Tcl_GetDoubleFromObj(acc->interp, Tcl_GetObjResult(acc->interp), &min_val);
        }
    }

    return sd_bus_reply_method_return(m, "d", min_val);
}

static int dbus_method_value_get_maximum(
    sd_bus_message *m, 
    void *userdata, 
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    double max_val = 100.0;
    char cmd[256];

    if (acc->tkwin && acc->interp) {
        snprintf(cmd, sizeof(cmd), "%s cget -to", Tk_PathName(acc->tkwin));
        if (Tcl_Eval(acc->interp, cmd) == TCL_OK) {
            Tcl_GetDoubleFromObj(acc->interp, Tcl_GetObjResult(acc->interp), &max_val);
        }
    }

    return sd_bus_reply_method_return(m, "d", max_val);
}

static int dbus_method_value_set_current(
    sd_bus_message *m, 
    void *userdata, 
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    double value;
    int r;

    r = sd_bus_message_read(m, "d", &value);
    if (r < 0) return r;

    if (!acc->tkwin || !acc->interp) {
        return sd_bus_reply_method_return(m, "b", 0);
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s set %g", Tk_PathName(acc->tkwin), value);
    Tcl_Eval(acc->interp, cmd);

    SendAtspiEvent(acc, ATSPI_EVENT_VALUE_CHANGED, NULL);
    return sd_bus_reply_method_return(m, "b", 1);
}

/*
 * at-spi/D-Bus method implementations: Text interface (stubs). Text processing is handled at the 
 * Tcl script level. 
 */

static int dbus_method_text_get_text(
    sd_bus_message *m, 
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "s", "");
}

static int dbus_method_text_get_caret_offset(
    sd_bus_message *m, 
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "i", -1);
}

static int dbus_method_text_get_character_count(
    sd_bus_message *m, 
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "i", 0);
}

/*
 * at-spi D-Bus method implementations: Selection interface.
 */

static int dbus_method_selection_get_n_selections(
    sd_bus_message *m, 
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "i", 0);
}

static int dbus_method_selection_get_selection(
    sd_bus_message *m, 
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "(so)", "", "");
}

static int dbus_method_selection_is_selected(
    sd_bus_message *m, 
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "b", 0);
}

static int dbus_method_selection_select_all(
    sd_bus_message *m, 
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "b", 0);
}

static int dbus_method_selection_clear_selection(
    sd_bus_message *m, 
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "b", 0);
}

static int dbus_method_selection_add_selection(
    sd_bus_message *m, 
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "b", 0);
}

static int dbus_method_selection_remove_selection(
    sd_bus_message *m, 
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "b", 0);
}

/*
 * D-Bus object registration. This makes Tk's accessibility visible to the system. 
 */

static int RegisterDbusObject(TkAccessible *acc)
{
    if (!atspi_conn || !atspi_conn->bus || !acc) {
        return 0;
    }

    /* Generate a unique object path if not already set */
    if (!acc->dbus_path) {
        static int counter = 0;
        char path[256];
        if (acc->tkwin && Tk_IsTopLevel(acc->tkwin)) {
            snprintf(path, sizeof(path), "/org/a11y/atspi/accessible/%s", Tk_PathName(acc->tkwin));
        } else {
            snprintf(path, sizeof(path), "/org/a11y/atspi/accessible/obj%d", counter++);
        }
        /* Replace dots with underscores for D-Bus path */
        char *p;
        for (p = path; *p; p++) {
            if (*p == '.') *p = '_';
        }
        acc->dbus_path = Tcl_Strdup(path);
    }

    /* Register main Accessible interface. */
    sd_bus_slot *slot = NULL;
    int r = sd_bus_add_object_vtable(atspi_conn->bus,
                                      &slot,
                                      acc->dbus_path,
                                      ATSPI_ACCESSIBLE_INTERFACE,
                                      accessible_vtable,
                                      acc);
    if (r < 0) {
        /* g_warning not available; we ignore for now. */
        return 0;
    }
    acc->vtable_slot = slot;

    /* Register Component interface (all objects support it). */
    sd_bus_add_object_vtable(atspi_conn->bus,
                              NULL,
                              acc->dbus_path,
                              ATSPI_COMPONENT_INTERFACE,
                              component_vtable,
                              acc);

    /* Conditionally register other interfaces based on role. */
    int role = acc->role;
    if (role == ATSPI_ROLE_PUSH_BUTTON || role == ATSPI_ROLE_CHECK_BOX ||
        role == ATSPI_ROLE_RADIO_BUTTON || role == ATSPI_ROLE_TOGGLE_BUTTON) {
        sd_bus_add_object_vtable(atspi_conn->bus, NULL, acc->dbus_path,
                                  ATSPI_ACTION_INTERFACE, action_vtable, acc);
    }
    if (role == ATSPI_ROLE_SPIN_BUTTON || role == ATSPI_ROLE_SLIDER ||
        role == ATSPI_ROLE_PROGRESS_BAR || role == ATSPI_ROLE_SCROLL_BAR) {
        sd_bus_add_object_vtable(atspi_conn->bus, NULL, acc->dbus_path,
                                  ATSPI_VALUE_INTERFACE, value_vtable, acc);
    }
    if (role == ATSPI_ROLE_ENTRY || role == ATSPI_ROLE_TEXT) {
        sd_bus_add_object_vtable(atspi_conn->bus, NULL, acc->dbus_path,
                                  ATSPI_TEXT_INTERFACE, text_vtable, acc);
    }
    if (role == ATSPI_ROLE_LIST_BOX || role == ATSPI_ROLE_TREE ||
        role == ATSPI_ROLE_TREE_TABLE) {
        sd_bus_add_object_vtable(atspi_conn->bus, NULL, acc->dbus_path,
                                  ATSPI_SELECTION_INTERFACE, selection_vtable, acc);
    }

    return 1;
}

/*
 * Event emission. Send notifications of state changes, actions, and more. 
 */

static void SendAtspiEvent(TkAccessible *acc, const char *event_type, const char *detail)
{
    if (!atspi_conn || !atspi_conn->bus || !acc || !acc->dbus_path) {
        return;
    }

    char event_name[256];
    if (detail) {
        snprintf(event_name, sizeof(event_name), "%s:%s", event_type, detail);
    } else {
        snprintf(event_name, sizeof(event_name), "%s", event_type);
    }

    sd_bus_emit_signal(atspi_conn->bus,
                       acc->dbus_path,
                       ATSPI_EVENT_INTERFACE,
                       event_name,
                       "s",
                       acc->dbus_path);
}

static void SendChildrenChanged(TkAccessible *parent, int index, TkAccessible *child, int added)
{
    if (!parent || !child) return;

    const char *detail = added ? "add" : "remove";
    SendAtspiEvent(parent, ATSPI_EVENT_CHILDREN_CHANGED, detail);
    /* Also emit a more specific signal with index and child */
    if (atspi_conn && atspi_conn->bus) {
        sd_bus_emit_signal(atspi_conn->bus,
                           parent->dbus_path,
                           ATSPI_EVENT_INTERFACE,
                           "children-changed",
                           "(i(so))",
                           index,
                           child->dbus_path,
                           ATSPI_ACCESSIBLE_INTERFACE);
    }
}

static void SendStateChanged(TkAccessible *acc, uint64_t state, int value)
{
    if (!acc) return;
    char state_name[64];
    snprintf(state_name, sizeof(state_name), "state-changed:%lu", state);
    SendAtspiEvent(acc, ATSPI_EVENT_STATE_CHANGED, state_name);
    if (atspi_conn && atspi_conn->bus) {
        sd_bus_emit_signal(atspi_conn->bus,
                           acc->dbus_path,
                           ATSPI_EVENT_INTERFACE,
                           "StateChanged",
                           "(sb)",
                           state,
                           value);
    }
}

static void SendActiveDescendantChanged(TkAccessible *container, TkAccessible *descendant)
{
    if (!container || !descendant) return;
    if (atspi_conn && atspi_conn->bus) {
        sd_bus_emit_signal(atspi_conn->bus,
                           container->dbus_path,
                           ATSPI_EVENT_INTERFACE,
                           "ActiveDescendantChanged",
                           "(so)",
                           descendant->dbus_path,
                           ATSPI_ACCESSIBLE_INTERFACE);
    }
}

/*
 * Tcl event loop integration. D-Bus will be come a custom event source for Tcl. 
 */

static void BusFileHandlerProc(ClientData clientData, int mask)
{
    AtspiConnection *conn = (AtspiConnection *)clientData;
    if (mask & TCL_READABLE) {
        sd_bus_process(conn->bus, NULL);
    }
}

static void TclEventSetupProc(ClientData clientData, int flags)
{
    AtspiConnection *conn = (AtspiConnection *)clientData;
    if (!(flags & TCL_WINDOW_EVENTS)) {
        return;
    }

    /* Ensure the bus file descriptor is watched */
    if (!conn->file_handler) {
        int fd = sd_bus_get_fd(conn->bus);
        if (fd >= 0) {
            conn->bus_fd = fd;
            Tcl_CreateFileHandler(fd, TCL_READABLE, BusFileHandlerProc, conn);
            conn->file_handler = 1; /* dummy */
        }
    }
}

static void TclEventCheckProc(ClientData clientData, int flags)
{
    AtspiConnection *conn = (AtspiConnection *)clientData;
    if (!(flags & TCL_WINDOW_EVENTS)) {
        return;
    }

    /* Check if any D-Bus messages are pending */
    if (sd_bus_get_events(conn->bus) > 0) {
        Tcl_ServiceEvent(TCL_WINDOW_EVENTS);
    }
}

/*
 * Tk Accessible object lifecycle: create, register, track, and free accessible objects
 * in toplevel windows and child widgets. 
 */

static TkAccessible *CreateAccessible(Tcl_Interp *interp, Tk_Window tkwin, const char *path)
{
    if (!interp || !tkwin) return NULL;

    TkAccessible *acc = (TkAccessible *)Tcl_Alloc(sizeof(TkAccessible));
    if (!acc) return NULL;
    memset(acc, 0, sizeof(TkAccessible));

    acc->interp = interp;
    acc->tkwin = tkwin;
    acc->path = Tcl_Strdup(path ? path : Tk_PathName(tkwin));
    acc->role = GetRoleForWidget(tkwin);
    acc->ref_count = 1;
    acc->states = ComputeStateForWidget(acc);

    /* Register D-Bus object */
    if (!RegisterDbusObject(acc)) {
        Tcl_Free(acc->path);
        Tcl_Free(acc);
        return NULL;
    }

    return acc;
}

static void FreeAccessible(TkAccessible *acc)
{
    if (!acc) return;

    if (acc->ref_count > 1) {
        acc->ref_count--;
        return;
    }

    /* Unregister D-Bus object (slot cleanup) */
    if (acc->vtable_slot) {
        sd_bus_slot_unref(acc->vtable_slot);
    }

    if (acc->path) Tcl_Free(acc->path);
    if (acc->dbus_path) Tcl_Free(acc->dbus_path);
    if (acc->virtual_name) Tcl_Free(acc->virtual_name);
    /* Free children list */
    AccessibleList *l = acc->children;
    while (l) {
        AccessibleList *next = l->next;
        FreeAccessible(l->acc);
        Tcl_Free(l);
        l = next;
    }
    Tcl_Free(acc);
}

static void RegisterAccessible(Tk_Window tkwin, TkAccessible *acc)
{
    if (!tkwin || !acc || !atspi_conn) return;

    int isNew;
    Tcl_HashEntry *entry = Tcl_CreateHashEntry(atspi_conn->tk_to_accessible_map, (char *)tkwin, &isNew);
    Tcl_SetHashValue(entry, acc);
    if (Tk_IsTopLevel(tkwin)) {
        RegisterToplevel(acc);
    }
}

static TkAccessible *
GetAccessible(Tk_Window tkwin)
{
    if (!atspi_conn) {
            return NULL;  /* still failed */
    }

    if (!atspi_conn->tk_to_accessible_map || !tkwin) {
        return NULL;
    }

    Tcl_HashEntry *entry = Tcl_FindHashEntry(atspi_conn->tk_to_accessible_map, (char *)tkwin);
    if (!entry) return NULL;
    return (TkAccessible *)Tcl_GetHashValue(entry);
}


static void UnregisterAccessible(Tk_Window tkwin)
{
    if (!atspi_conn || !tkwin) return;

    Tcl_HashEntry *entry = Tcl_FindHashEntry(atspi_conn->tk_to_accessible_map, (char *)tkwin);
    if (!entry) return;
    TkAccessible *acc = (TkAccessible *)Tcl_GetHashValue(entry);
    if (acc) {
        if (Tk_IsTopLevel(tkwin)) {
            UnregisterToplevel(acc);
        }
        Tcl_DeleteHashEntry(entry);
        FreeAccessible(acc);
    }
}

static void RegisterToplevel(TkAccessible *acc)
{
    if (!acc) return;

    AccessibleList *l = atspi_conn->toplevel_accessibles;
    while (l) {
        if (l->acc == acc) return;
        l = l->next;
    }
    AccessibleList *node = (AccessibleList *)Tcl_Alloc(sizeof(AccessibleList));
    node->acc = acc;
    node->next = atspi_conn->toplevel_accessibles;
    atspi_conn->toplevel_accessibles = node;
    SendAtspiEvent(acc, ATSPI_EVENT_WINDOW_CREATE, NULL);
}

static void UnregisterToplevel(TkAccessible *acc)
{
    if (!acc || !atspi_conn) return;
    AccessibleList *prev = NULL;
    AccessibleList *l = atspi_conn->toplevel_accessibles;
    while (l) {
        if (l->acc == acc) {
            if (prev) prev->next = l->next;
            else atspi_conn->toplevel_accessibles = l->next;
            Tcl_Free(l);
            return;
        }
        prev = l;
        l = l->next;
    }
}

static void RegisterWidgetRecursive(Tcl_Interp *interp, Tk_Window tkwin)
{
    if (!tkwin) return;

    TkAccessible *acc = GetAccessible(tkwin);
    if (!acc) {
        acc = CreateAccessible(interp, tkwin, Tk_PathName(tkwin));
        if (!acc) return;

        /* Set parent relationship */
        if (!Tk_IsTopLevel(tkwin)) {
            Tk_Window parent = Tk_Parent(tkwin);
            if (parent) {
                TkAccessible *parent_acc = GetAccessible(parent);
                if (!parent_acc) {
                    RegisterWidgetRecursive(interp, parent);
                    parent_acc = GetAccessible(parent);
                }
                acc->parent = parent_acc;
            }
        }

        RegisterAccessible(tkwin, acc);
        TkAccessible_RegisterEventHandlers(tkwin, acc);
    }

    /* Recursively register children. */
    TkWindow *child;
    for (child = ((TkWindow*)tkwin)->childList;
         child != NULL;
         child = child->nextPtr) {
        RegisterWidgetRecursive(interp, (Tk_Window)child);
    }
}

static void EnsureAccessibleInHierarchy(Tcl_Interp *interp, Tk_Window tkwin)
{
    if (!tkwin) return;

    /* Ensure all ancestors exist. */
    Tk_Window current = tkwin;
    /* We'll use a simple array of windows; max depth is limited. */
    Tk_Window ancestors[256];
    int depth = 0;
    while (current) {
        ancestors[depth++] = current;
        if (Tk_IsTopLevel(current)) break;
        current = Tk_Parent(current);
        if (depth >= 256) break;
    }

    /* Process from root to leaf */
    for (int i = depth - 1; i >= 0; i--) {
        Tk_Window win = ancestors[i];
        if (!GetAccessible(win)) {
            TkAccessible *acc = CreateAccessible(interp, win, Tk_PathName(win));
            if (acc) {
                if (!Tk_IsTopLevel(win)) {
                    Tk_Window parent = Tk_Parent(win);
                    TkAccessible *parent_acc = GetAccessible(parent);
                    if (parent_acc) {
                        acc->parent = parent_acc;
                    }
                }
                RegisterAccessible(win, acc);
                TkAccessible_RegisterEventHandlers(win, acc);
            }
        }
    }
}

static void UpdateFocusChain(Tk_Window focused)
{
    if (!focused) return;

    Tcl_Interp *interp = Tk_Interp(focused);
    if (!interp) return;

    EnsureAccessibleInHierarchy(interp, focused);

    TkAccessible *focused_acc = GetAccessible(focused);
    if (!focused_acc) return;

    /* Update focus state. */
    focused_acc->is_focused = 1;
    focused_acc->states |= ATSPI_STATE_FOCUSED;
    SendStateChanged(focused_acc, ATSPI_STATE_FOCUSED, 1);
    SendAtspiEvent(focused_acc, ATSPI_EVENT_FOCUS, NULL);

    /* Notify parent about active descendant. */
    Tk_Window current = focused;
    while (current && !Tk_IsTopLevel(current)) {
        Tk_Window parent = Tk_Parent(current);
        if (parent) {
            TkAccessible *parent_acc = GetAccessible(parent);
            if (parent_acc) {
                SendActiveDescendantChanged(parent_acc, focused_acc);
            }
        }
        current = parent;
    }

    /* If this is a toplevel, emit window activation. */
    if (Tk_IsTopLevel(focused)) {
        SendAtspiEvent(focused_acc, ATSPI_EVENT_WINDOW_ACTIVATE, NULL);
    }
}

static Tk_Window GetToplevelOfWidget(Tk_Window tkwin)
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
 * Track accessibility roles and determine/update state. 
*/

static int GetRoleForWidget(Tk_Window tkwin)
{
    if (!tkwin) return ATSPI_ROLE_INVALID;

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)tkwin);
    if (hPtr) {
        Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
        if (attrs) {
            Tcl_HashEntry *roleEntry = Tcl_FindHashEntry(attrs, "role");
            if (roleEntry) {
                const char *result = Tcl_GetString((Tcl_Obj *)Tcl_GetHashValue(roleEntry));
                if (result) {
                    for (int i = 0; roleMap[i].tkrole != NULL; i++) {
                        if (strcmp(roleMap[i].tkrole, result) == 0) {
                            return roleMap[i].atspi_role;
                        }
                    }
                }
            }
        }
    }

    /* Fallback to widget class. */
    const char *widgetClass = Tk_Class(tkwin);
    if (widgetClass) {
        for (int i = 0; roleMap[i].tkrole != NULL; i++) {
            if (strcasecmp(roleMap[i].tkrole, widgetClass) == 0) {
                return roleMap[i].atspi_role;
            }
        }
    }

    if (Tk_IsTopLevel(tkwin)) {
        return ATSPI_ROLE_WINDOW;
    }

    return ATSPI_ROLE_INVALID;
}

static uint64_t ComputeStateForWidget(TkAccessible *acc)
{
    uint64_t states = 0;
    if (!acc || !acc->tkwin) return states;

    /* Basic states */
    states |= ATSPI_STATE_ENABLED;
    states |= ATSPI_STATE_SENSITIVE;

    /* Focusable based on role */
    int role = acc->role;
    if (role == ATSPI_ROLE_PUSH_BUTTON ||
        role == ATSPI_ROLE_CHECK_BOX ||
        role == ATSPI_ROLE_RADIO_BUTTON ||
        role == ATSPI_ROLE_ENTRY ||
        role == ATSPI_ROLE_TEXT ||
        role == ATSPI_ROLE_COMBO_BOX ||
        role == ATSPI_ROLE_SPIN_BUTTON ||
        role == ATSPI_ROLE_SLIDER ||
        role == ATSPI_ROLE_TOGGLE_BUTTON ||
        role == ATSPI_ROLE_LIST_BOX ||
        role == ATSPI_ROLE_TREE) {
        states |= ATSPI_STATE_FOCUSABLE;
    }

    /* Focused.*/
    if (acc->is_focused) {
        states |= ATSPI_STATE_FOCUSED;
    }

    /* Visible/Showing. */
    if (Tk_IsMapped(acc->tkwin)) {
        states |= ATSPI_STATE_VISIBLE;
        states |= ATSPI_STATE_SHOWING;
    }

    /* Editable for entries. */
    if (role == ATSPI_ROLE_ENTRY || role == ATSPI_ROLE_TEXT) {
        Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)acc->tkwin);
        int is_editable = 1;
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
        if (is_editable) {
            states |= ATSPI_STATE_EDITABLE;
        }
    }

    /* Checked state for toggleable widgets. */
    if (role == ATSPI_ROLE_CHECK_BOX ||
        role == ATSPI_ROLE_RADIO_BUTTON ||
        role == ATSPI_ROLE_TOGGLE_BUTTON) {
        char *value = GetValueForWidget(acc->tkwin);
        if (value) {
            if (strcmp(value, "selected") == 0 ||
                strcmp(value, "1") == 0 ||
                (value[0] != '0' && value[0] != '\0')) {
                states |= ATSPI_STATE_CHECKED;
            }
            Tcl_Free(value);
        }
    }

    return states;
}

/*
 * Attribute getters from Tk - pull data from the central AccessibleObject hash table. 
 */

static char *GetNameForWidget(Tk_Window tkwin)
{
    if (!tkwin) return NULL;

    int role = GetRoleForWidget(tkwin);
    if (role == ATSPI_ROLE_LABEL) {
        return GetValueForWidget(tkwin);  /* Label uses value as name */
    }

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)tkwin);
    if (!hPtr) return NULL;

    Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
    if (!attrs) return NULL;

    Tcl_HashEntry *nameEntry = Tcl_FindHashEntry(attrs, "name");
    if (!nameEntry) return NULL;

    const char *name = Tcl_GetString((Tcl_Obj *)Tcl_GetHashValue(nameEntry));
    return name ? Tcl_Strdup(name) : NULL;
}

static char *GetDescriptionForWidget(Tk_Window tkwin)
{
    if (!tkwin) return NULL;

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)tkwin);
    if (!hPtr) return NULL;

    Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
    if (!attrs) return NULL;

    Tcl_HashEntry *descEntry = Tcl_FindHashEntry(attrs, "description");
    if (!descEntry) return NULL;

    const char *desc = Tcl_GetString((Tcl_Obj *)Tcl_GetHashValue(descEntry));
    return desc ? Tcl_Strdup(desc) : NULL;
}

static char *GetValueForWidget(Tk_Window tkwin)
{
    if (!tkwin) return NULL;

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)tkwin);
    if (!hPtr) return NULL;

    Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
    if (!attrs) return NULL;

    Tcl_HashEntry *valueEntry = Tcl_FindHashEntry(attrs, "value");
    if (!valueEntry) return NULL;

    const char *value = Tcl_GetString((Tcl_Obj *)Tcl_GetHashValue(valueEntry));
    return value ? Tcl_Strdup(value) : NULL;
}

/*
 * X Event Handlers.
 */

static void TkAccessible_RegisterEventHandlers(Tk_Window tkwin, TkAccessible *acc)
{
    if (!tkwin || !acc) return;

    Tk_CreateEventHandler(tkwin, StructureNotifyMask,
                          TkAccessible_DestroyHandler, acc);
    Tk_CreateEventHandler(tkwin, FocusChangeMask,
                          TkAccessible_FocusHandler, acc);
    Tk_CreateEventHandler(tkwin, SubstructureNotifyMask,
                          TkAccessible_CreateHandler, acc);
    Tk_CreateEventHandler(tkwin, ConfigureNotify,
                          TkAccessible_ConfigureHandler, acc);
}

static void TkAccessible_DestroyHandler(void *clientData, XEvent *eventPtr)
{
    if (eventPtr->type != DestroyNotify) return;

    TkAccessible *acc = (TkAccessible *)clientData;
    if (!acc || !acc->tkwin) return;

    /* Notify that this object is going away */
    if (acc->parent) {
        int idx = -1;
        if (acc->parent->children) {
            AccessibleList *l = acc->parent->children;
            int i = 0;
            while (l) {
                if (l->acc == acc) {
                    idx = i;
                    break;
                }
                l = l->next;
                i++;
            }
        } else if (acc->parent->tkwin) {
            /* Compute index from parent's child list. */
            TkWindow *childPtr;
            int i = 0;
            for (childPtr = ((TkWindow*)acc->parent->tkwin)->childList;
                 childPtr != NULL;
                 childPtr = childPtr->nextPtr, i++) {
                if ((Tk_Window)childPtr == acc->tkwin) {
                    idx = i;
                    break;
                }
            }
        }
        SendChildrenChanged(acc->parent, idx, acc, 0);
    }

    UnregisterAccessible(acc->tkwin);
}

static void TkAccessible_FocusHandler(void *clientData, XEvent *eventPtr)
{
    TkAccessible *acc = (TkAccessible *)clientData;
    if (!acc || !acc->tkwin) return;

    int focused = (eventPtr->type == FocusIn);
    acc->is_focused = focused;
    uint64_t old_states = acc->states;
    acc->states = ComputeStateForWidget(acc);

    if ((old_states & ATSPI_STATE_FOCUSED) != (acc->states & ATSPI_STATE_FOCUSED)) {
        SendStateChanged(acc, ATSPI_STATE_FOCUSED, focused);
        SendAtspiEvent(acc, ATSPI_EVENT_FOCUS, NULL);
    }

    /* Handle window activation */
    if (acc->role == ATSPI_ROLE_WINDOW) {
        if (focused) {
            SendAtspiEvent(acc, ATSPI_EVENT_WINDOW_ACTIVATE, NULL);
        } else {
            SendAtspiEvent(acc, ATSPI_EVENT_WINDOW_DEACTIVATE, NULL);
        }
    }

    /* Notify parent about active descendant */
    if (focused && acc->parent) {
        SendActiveDescendantChanged(acc->parent, acc);
    }
}

static void TkAccessible_CreateHandler(void *clientData, XEvent *eventPtr)
{
    if (!eventPtr || eventPtr->type != CreateNotify) return;

    Tk_Window parentWin = (Tk_Window)clientData;
    if (!parentWin) return;

    Tcl_Interp *interp = Tk_Interp(parentWin);
    if (!interp) return;

    Window childWindow = eventPtr->xcreatewindow.window;
    Tk_Window childWin = Tk_IdToWindow(Tk_Display(parentWin), childWindow);
    if (!childWin || GetAccessible(childWin)) return;

    TkAccessible *child_acc = CreateAccessible(interp, childWin, Tk_PathName(childWin));
    if (!child_acc) return;

    TkAccessible *parent_acc = GetAccessible(parentWin);
    if (!parent_acc) {
        parent_acc = CreateAccessible(interp, parentWin, Tk_PathName(parentWin));
        if (parent_acc) {
            RegisterAccessible(parentWin, parent_acc);
            if (Tk_IsTopLevel(parentWin)) {
                RegisterToplevel(parent_acc);
            }
        }
    }

    if (parent_acc) {
        child_acc->parent = parent_acc;
    }

    RegisterAccessible(childWin, child_acc);
    TkAccessible_RegisterEventHandlers(childWin, child_acc);

    /* Notify parent about new child. */
    int idx = -1;
    if (parent_acc) {
        if (parent_acc->children) {
            idx = 0; /* just count. */
            AccessibleList *l = parent_acc->children;
            while (l) { idx++; l = l->next; }
        } else if (parent_acc->tkwin) {
            /* Compute index */
            TkWindow *ptr;
            int i = 0;
            for (ptr = ((TkWindow*)parent_acc->tkwin)->childList; ptr; ptr = ptr->nextPtr, i++) {
                if ((Tk_Window)ptr == childWin) {
                    idx = i;
                    break;
                }
            }
        }
    }
    SendChildrenChanged(parent_acc, idx, child_acc, 1);
}

static void TkAccessible_ConfigureHandler(void *clientData, XEvent *eventPtr)
{
    if (!eventPtr || eventPtr->type != ConfigureNotify) return;

    Tk_Window tkwin = (Tk_Window)clientData;
    if (!tkwin) return;

    TkAccessible *acc = GetAccessible(tkwin);
    if (!acc) return;

    /* Update geometry (optional, could emit signal) */
    acc->width = Tk_Width(tkwin);
    acc->height = Tk_Height(tkwin);
    Tk_GetRootCoords(tkwin, &acc->x, &acc->y);

    /* Update visibility states */
    uint64_t old_states = acc->states;
    acc->states = ComputeStateForWidget(acc);

    if ((old_states & ATSPI_STATE_VISIBLE) != (acc->states & ATSPI_STATE_VISIBLE)) {
        SendStateChanged(acc, ATSPI_STATE_VISIBLE, (acc->states & ATSPI_STATE_VISIBLE) != 0);
    }
    if ((old_states & ATSPI_STATE_SHOWING) != (acc->states & ATSPI_STATE_SHOWING)) {
        SendStateChanged(acc, ATSPI_STATE_SHOWING, (acc->states & ATSPI_STATE_SHOWING) != 0);
    }
}

/*
 * Screen reader detection.
 */

static int IsScreenReaderActive(void)
{
    FILE *fp = popen("pgrep -x orca", "r");
    if (!fp) return 0;
    char buffer[16];
    int running = (fgets(buffer, sizeof(buffer), fp) != NULL);
    pclose(fp);
    return running;
}

/*
 * Tcl command implementations.
*/

static int AddAccessibleCmd(
	TCL_UNUSED(ClientData),
	Tcl_Interp *interp,
	int objc, 
	Tcl_Obj 
	*const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "window");
        return TCL_ERROR;
    }

    const char *windowName = Tcl_GetString(objv[1]);
    Tk_Window tkwin = Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));
    if (!tkwin) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Invalid window name.", -1));
        return TCL_ERROR;
    }

    RegisterWidgetRecursive(interp, tkwin);
    EnsureAccessibleInHierarchy(interp, tkwin);

    TkWindow *focusPtr = TkGetFocusWin((TkWindow*)tkwin);
    if (focusPtr == (TkWindow*)tkwin) {
        UpdateFocusChain(tkwin);
    }

    return TCL_OK;
}

static int EmitSelectionChangedCmd(
	TCL_UNUSED(ClientData),  
	Tcl_Interp *interp,
	int objc, Tcl_Obj 
	*const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "window");
        return TCL_ERROR;
    }

    const char *windowName = Tcl_GetString(objv[1]);
    Tk_Window tkwin = Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));
    if (!tkwin) return TCL_OK;

    TkAccessible *acc = GetAccessible(tkwin);
    if (!acc) {
        acc = CreateAccessible(interp, tkwin, windowName);
        if (!acc) return TCL_OK;
        RegisterAccessible(tkwin, acc);
        TkAccessible_RegisterEventHandlers(tkwin, acc);
    }

    int role = GetRoleForWidget(tkwin);
    if (role == ATSPI_ROLE_CHECK_BOX || role == ATSPI_ROLE_RADIO_BUTTON) {
        SendStateChanged(acc, ATSPI_STATE_CHECKED, 1); /* will recompute */
    } else if (role == ATSPI_ROLE_ENTRY || role == ATSPI_ROLE_TEXT) {
        SendAtspiEvent(acc, ATSPI_EVENT_TEXT_CHANGED, "insert");
    } else if (role == ATSPI_ROLE_SPIN_BUTTON || role == ATSPI_ROLE_SLIDER) {
        SendAtspiEvent(acc, ATSPI_EVENT_VALUE_CHANGED, NULL);
    } else {
        SendAtspiEvent(acc, ATSPI_EVENT_SELECTION_CHANGED, NULL);
    }

    return TCL_OK;
}

static int EmitFocusChangedCmd(
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

static int IsScreenReaderRunningCmd(
    TCL_UNUSED(void *), /* clientData */
    Tcl_Interp *interp,
    TCL_UNUSED(int), /* objc */
    TCL_UNUSED(Tcl_Obj *const *)) /* objv */
{
    bool result = IsScreenReaderActive();

    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(result));
    return TCL_OK;
}


/*
 * Module initialization.
*/

static int
InitializeAtspiConnection(void)
{
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *msg = NULL;
    int r;

    if (atspi_conn && atspi_conn->is_initialized) {
        return 1;
    }

    atspi_conn = (AtspiConnection *)Tcl_Alloc(sizeof(AtspiConnection));
    if (!atspi_conn) {
        return 0;
    }
    memset(atspi_conn, 0, sizeof(AtspiConnection));

    /* Connect to session bus (preferred), fallback to system. */
    r = sd_bus_default_user(&bus);
    if (r < 0) {
        r = sd_bus_default_system(&bus);
        if (r < 0) {
            Tcl_Free(atspi_conn);
            atspi_conn = NULL;
            return 0;
        }
    }
    atspi_conn->bus = bus;

    /* Initialize the hash table early. */
    atspi_conn->tk_to_accessible_map = (Tcl_HashTable *)Tcl_Alloc(sizeof(Tcl_HashTable));
    if (!atspi_conn->tk_to_accessible_map) {
        sd_bus_unref(bus);
        Tcl_Free(atspi_conn);
        atspi_conn = NULL;
        return 0;
    }
    Tcl_InitHashTable(atspi_conn->tk_to_accessible_map, TCL_ONE_WORD_KEYS);

    /* Check if AT-SPI registry is actually running/registered on the bus.
     * Use GetNameOwner — lightweight and reliable way to see if org.a11y.atspi.Registry exists.
     */
    r = sd_bus_call_method(bus,
                           "org.freedesktop.DBus",
                           "/org/freedesktop/DBus",
                           "org.freedesktop.DBus",
                           "GetNameOwner",
                           &error,
                           &msg,
                           "s", ATSPI_DBUS_NAME);  /* "org.a11y.atspi.Registry" */

    if (r < 0) {
        /* Registry not present (NameHasNoOwner, timeout, etc.) → disable a11y gracefully */
        /* Optional debug: fprintf(stderr, "AT-SPI registry not available: %s\n", error.message); */
        sd_bus_error_free(&error);
        if (msg) sd_bus_message_unref(msg);

        Tcl_DeleteHashTable(atspi_conn->tk_to_accessible_map);
        Tcl_Free(atspi_conn->tk_to_accessible_map);
        sd_bus_unref(bus);
        Tcl_Free(atspi_conn);
        atspi_conn = NULL;
        return 0;
    }

    /* Registry exists → clean up temp message. */
    sd_bus_message_unref(msg);
    sd_bus_error_free(&error);

    /* Create root accessible object (application). */
    atspi_conn->root_accessible = (TkAccessible *)Tcl_Alloc(sizeof(TkAccessible));
    if (!atspi_conn->root_accessible) {
        Tcl_DeleteHashTable(atspi_conn->tk_to_accessible_map);
        Tcl_Free(atspi_conn->tk_to_accessible_map);
        sd_bus_unref(bus);
        Tcl_Free(atspi_conn);
        atspi_conn = NULL;
        return 0;
    }
    memset(atspi_conn->root_accessible, 0, sizeof(TkAccessible));

    atspi_conn->root_accessible->role       = ATSPI_ROLE_APPLICATION;
    atspi_conn->root_accessible->path       = Tcl_Strdup("application");  /* or your Tcl_Strdup impl */
    atspi_conn->root_accessible->dbus_path  = Tcl_Strdup("/org/a11y/atspi/accessible/root");
    atspi_conn->root_accessible->ref_count  = 1;

    RegisterDbusObject(atspi_conn->root_accessible);

    atspi_conn->is_initialized = 1;

    /* Integrate with Tcl event loop for DBus handling. */
    Tcl_CreateEventSource(TclEventSetupProc, TclEventCheckProc, atspi_conn);

    return 1;
}

int TkWaylandAccessibility_Init(Tcl_Interp *interp)
{
    /* Initialize D-Bus connection to at-spi. */
	if (!InitializeAtspiConnection()) {
		Tcl_AppendResult(interp, "Warning: Could not connect to AT-SPI - accessibility disabled for now", (char *)NULL);
		/* Proceed anyway – don't block Tk init */
	}

    /* Initialize main window. */
    Tk_Window mainWin = Tk_MainWindow(interp);
    if (mainWin) {
        Tk_MakeWindowExist(mainWin);
        Tk_MapWindow(mainWin);

        TkAccessible *main_acc = CreateAccessible(interp, mainWin, Tk_PathName(mainWin));
        if (main_acc) {
            main_acc->role = ATSPI_ROLE_WINDOW;
            RegisterAccessible(mainWin, main_acc);
            RegisterToplevel(main_acc);
            TkAccessible_RegisterEventHandlers(mainWin, main_acc);
        }

        /* Register all existing widgets. */
        RegisterWidgetRecursive(interp, mainWin);
    }

    /* Register Tcl commands. */
    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object",
                          AddAccessibleCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change",
                          EmitSelectionChangedCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_focus_change",
                          EmitFocusChangedCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader",
                          IsScreenReaderRunningCmd, NULL, NULL);

    return TCL_OK;
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
