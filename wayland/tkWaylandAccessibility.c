/*
 * tkWaylandAccessibility.c --
 *
 * This file implements accessibility/screen-reader support
 * for Tk on Wayland systems using direct at-spi access via sd-bus.
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
#include "tkWaylandInt.h"

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

    /* D-Bus slot for this object (for cleanup). */
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
    int file_handler;    /* Dummy, just to know it's set. */

    /*
     * Desktop reference returned by Socket.Embed - root_accessible's
     * effective parent once we've been embedded in the registry's tree.
     */
    char *desktop_bus_name;
    char *desktop_path;
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
static const sd_bus_vtable application_vtable[];

/*
 * Accessible-reference ((so) = bus-name, object-path) helpers. Centralized
 * so every method reply / signal uses the correct field order and content.
 */
static const char *SelfBusName(void);
static int AppendAccessibleRef(sd_bus_message *reply, const char *path);
static bool EmbedWithRegistry(void);

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
static void BusFileHandlerProc(void *clientData, int mask);
static void TclEventSetupProc(void *clientData, int flags);
static void TclEventCheckProc(void *clientData, int flags);

/* Tcl command implementations. */
static int AddAccessibleCmd(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int EmitSelectionChangedCmd(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int EmitFocusChangedCmd(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int IsScreenReaderRunningCmd(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

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


/*
 * org.a11y.atspi.Application interface - queried by the registry/Orca once
 * an application has been embedded via Socket.Embed.
 */

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_toolkit_name --
 *
 *   D-Bus property getter for the ToolkitName property of the Application
 *   interface. Returns "Tk" as the toolkit name.
 *
 * Results:
 *   Returns 0 on success, negative error code on failure.
 *
 * Side effects:
 *   Appends the toolkit name string to the D-Bus reply message.
 *----------------------------------------------------------------------
 */

static int dbus_prop_get_toolkit_name(
    TCL_UNUSED(sd_bus *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    sd_bus_message *reply,
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    return sd_bus_message_append(reply, "s", "Tk");
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_version --
 *
 *   D-Bus property getter for the Version property of the Application
 *   interface. Returns the Tk version string.
 *
 * Results:
 *   Returns 0 on success, negative error code on failure.
 *
 * Side effects:
 *   Appends the Tk version string to the D-Bus reply message.
 *----------------------------------------------------------------------
 */

static int dbus_prop_get_version(
    TCL_UNUSED(sd_bus *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    sd_bus_message *reply,
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    return sd_bus_message_append(reply, "s", TK_VERSION);
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_atspi_version --
 *
 *   D-Bus property getter for the AtspiVersion property of the Application
 *   interface. Returns the AT-SPI version string.
 *
 * Results:
 *   Returns 0 on success, negative error code on failure.
 *
 * Side effects:
 *   Appends the AT-SPI version string to the D-Bus reply message.
 *----------------------------------------------------------------------
 */

static int dbus_prop_get_atspi_version(
    TCL_UNUSED(sd_bus *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    sd_bus_message *reply,
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    return sd_bus_message_append(reply, "s", "2.1");
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_id --
 *
 *   D-Bus property getter for the Id property of the Application
 *   interface. Returns the virtual index of the accessible object.
 *
 * Results:
 *   Returns 0 on success, negative error code on failure.
 *
 * Side effects:
 *   Appends the virtual index integer to the D-Bus reply message.
 *----------------------------------------------------------------------
 */

static int dbus_prop_get_id(
    TCL_UNUSED(sd_bus *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    sd_bus_message *reply,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    TkAccessible *acc = (TkAccessible *)userdata;
    return sd_bus_message_append(reply, "i", acc ? acc->virtual_index : 0);
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_set_id --
 *
 *   D-Bus property setter for the Id property of the Application
 *   interface. Sets the virtual index of the accessible object.
 *
 * Results:
 *   Returns 0 on success, negative error code on failure.
 *
 * Side effects:
 *   Updates the virtual_index field of the accessible object.
 *----------------------------------------------------------------------
 */

static int dbus_prop_set_id(
    TCL_UNUSED(sd_bus *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    sd_bus_message *value,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t id = 0;
    int r = sd_bus_message_read(value, "i", &id);
    if (r >= 0 && acc) acc->virtual_index = id;
    return r;
}

static const sd_bus_vtable application_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("ToolkitName", "s", dbus_prop_get_toolkit_name, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Version", "s", dbus_prop_get_version, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("AtspiVersion", "s", dbus_prop_get_atspi_version, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_WRITABLE_PROPERTY("Id", "i", dbus_prop_get_id, dbus_prop_set_id, 0, 0),
    SD_BUS_VTABLE_END
};

/*
 *----------------------------------------------------------------------
 * Tcl_Strdup --
 *
 *   Duplicate a string using Tcl's memory allocator.
 *
 * Results:
 *   Returns a pointer to the newly allocated copy of the string, or NULL
 *   if the input string is NULL.
 *
 * Side effects:
 *   Memory is allocated via Tcl_Alloc.
 *----------------------------------------------------------------------
 */

static char *
Tcl_Strdup(const char *s)
{
    if (s == NULL) return NULL;
    return strcpy((char *) Tcl_Alloc(strlen(s) + 1), s);
}

/*
 *----------------------------------------------------------------------
 * SelfBusName --
 *
 *   Get the unique D-Bus name of our own connection. Used as the bus-name
 *   half of accessible references ((so) tuples) handed out to AT-SPI.
 *
 * Results:
 *   Returns a pointer to the static bus name string, or an empty string
 *   if not connected.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

static const char *SelfBusName(void)
{
    static const char *name;
    if (!atspi_conn || !atspi_conn->bus) return "";
    if (sd_bus_get_unique_name(atspi_conn->bus, &name) < 0 || !name) {
        return "";
    }
    return name;
}

/*
 *----------------------------------------------------------------------
 * AppendAccessibleRef --
 *
 *   Append an AT-SPI accessible reference ((so) tuple) to a D-Bus message.
 *   The reference consists of the bus name and the object path of the
 *   accessible. If the path is NULL or empty, the canonical null reference
 *   is appended instead.
 *
 * Results:
 *   Returns 0 on success, or a negative error code from sd_bus_message_append.
 *
 * Side effects:
 *   Appends data to the D-Bus message.
 *----------------------------------------------------------------------
 */

static int AppendAccessibleRef(sd_bus_message *reply, const char *path)
{
    if (path && *path) {
        return sd_bus_message_append(reply, "(so)", SelfBusName(), path);
    }
    return sd_bus_message_append(reply, "(so)", "", "/org/a11y/atspi/null");
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
 *----------------------------------------------------------------------
 * dbus_method_get_children --
 *
 *   D-Bus method handler for GetChildren on the Accessible interface.
 *   Returns the list of child accessibles for the given object.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an array of (so) references.
 *----------------------------------------------------------------------
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
                AppendAccessibleRef(reply, top->dbus_path);
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
                AppendAccessibleRef(reply, child_acc->dbus_path);
            }
        }
    } else {
        /* Virtual children. */
        AccessibleList *l;
        for (l = acc->children; l != NULL; l = l->next) {
            TkAccessible *child = l->acc;
            if (child && child->dbus_path) {
                AppendAccessibleRef(reply, child->dbus_path);
            }
        }
    }

    sd_bus_message_close_container(reply);
    return sd_bus_send(NULL, reply, NULL);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_child_at_index --
 *
 *   D-Bus method handler for GetChildAtIndex on the Accessible interface.
 *   Returns the child accessible at the specified index.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an (so) reference.
 *----------------------------------------------------------------------
 */

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
            AppendAccessibleRef(reply, top->dbus_path);
        } else {
            AppendAccessibleRef(reply, NULL);
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
                    AppendAccessibleRef(reply, child_acc->dbus_path);
                } else {
                    AppendAccessibleRef(reply, NULL);
                }
                break;
            }
        }
        if (!childPtr) {
            AppendAccessibleRef(reply, NULL);
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
            AppendAccessibleRef(reply, child->dbus_path);
        } else {
            AppendAccessibleRef(reply, NULL);
        }
    }

    return sd_bus_send(NULL, reply, NULL);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_attributes --
 *
 *   D-Bus method handler for GetAttributes on the Accessible interface.
 *   Returns the attributes of the accessible object as a dictionary.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an array of key-value pairs.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * dbus_method_get_states --
 *
 *   D-Bus method handler for GetStates on the Accessible interface.
 *   Returns the bitmask of states for the accessible object.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a 64-bit unsigned integer.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * dbus_method_get_role --
 *
 *   D-Bus method handler for GetRole on the Accessible interface.
 *   Returns the AT-SPI role code for the accessible object.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an integer role code.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * dbus_method_get_name --
 *
 *   D-Bus method handler for GetName on the Accessible interface.
 *   Returns the name of the accessible object.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a string.
 *----------------------------------------------------------------------
 */
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

/*
 *----------------------------------------------------------------------
 * dbus_method_get_description --
 *
 *   D-Bus method handler for GetDescription on the Accessible interface.
 *   Returns the description of the accessible object.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a string.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * dbus_method_get_parent --
 *
 *   D-Bus method handler for GetParent on the Accessible interface.
 *   Returns the parent accessible reference.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an (so) reference.
 *----------------------------------------------------------------------
 */

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

    if (acc == atspi_conn->root_accessible &&
        atspi_conn->desktop_bus_name && atspi_conn->desktop_path) {
        /*
	 * Root's parent is the desktop, as returned by Socket.Embed - not
         * one of our own objects, so build the tuple directly rather than
         * via AppendAccessibleRef (which always uses our own bus name).
	 */
        sd_bus_message_append(reply, "(so)",
                               atspi_conn->desktop_bus_name,
                               atspi_conn->desktop_path);
    } else if (acc->parent && acc->parent->dbus_path) {
        AppendAccessibleRef(reply, acc->parent->dbus_path);
    } else {
        AppendAccessibleRef(reply, NULL);
    }
    return sd_bus_send(NULL, reply, NULL);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_grab_focus --
 *
 *   D-Bus method handler for GrabFocus on the Accessible interface.
 *   Attempts to set focus to the accessible object.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Changes focus to the specified widget and sends a focus event.
 *----------------------------------------------------------------------
 */

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

    /* Update internal state. */
    acc->is_focused = 1;
    acc->states |= ATSPI_STATE_FOCUSED;

    /* Send focus event. */
    SendAtspiEvent(acc, ATSPI_EVENT_FOCUS, NULL);

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    sd_bus_message_append(reply, "b", 1);
    return sd_bus_send(NULL, reply, NULL);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_index_in_parent --
 *
 *   D-Bus method handler for GetIndexInParent on the Accessible interface.
 *   Returns the index of this accessible in its parent's child list.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an integer index.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * dbus_method_get_interfaces --
 *
 *   D-Bus method handler for GetInterfaces on the Accessible interface.
 *   Returns the list of D-Bus interfaces supported by this object.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an array of interface names.
 *----------------------------------------------------------------------
 */
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
 *----------------------------------------------------------------------
 * dbus_method_component_get_extents --
 *
 *   D-Bus method handler for GetExtents on the Component interface.
 *   Returns the x, y, width, and height of the component.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a tuple of four integers.
 *----------------------------------------------------------------------
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

    if (coord_type == 1) { /* ATSPI_XY_WINDOW: relative to parent. */
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

/*
 *----------------------------------------------------------------------
 * dbus_method_component_get_position --
 *
 *   D-Bus method handler for GetPosition on the Component interface.
 *   Returns the x and y position of the component.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a tuple of two integers.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * dbus_method_component_get_size --
 *
 *   D-Bus method handler for GetSize on the Component interface.
 *   Returns the width and height of the component.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a tuple of two integers.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * dbus_method_component_contains --
 *
 *   D-Bus method handler for Contains on the Component interface.
 *   Determines whether the given point is within the component.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a boolean value.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * dbus_method_component_grab_focus --
 *
 *   D-Bus method handler for GrabFocus on the Component interface.
 *   Delegates to the Accessible.GrabFocus method.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Same as dbus_method_grab_focus.
 *----------------------------------------------------------------------
 */

static int dbus_method_component_grab_focus(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
    return dbus_method_grab_focus(m, userdata, ret_error); /* Same as Accessible.GrabFocus. */
}

/*
 *----------------------------------------------------------------------
 * dbus_method_action_get_n_actions --
 *
 *   D-Bus method handler for GetNActions on the Action interface.
 *   Returns the number of actions supported by this object.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an integer.
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 * dbus_method_action_do_action --
 *
 *   D-Bus method handler for DoAction on the Action interface.
 *   Performs the specified action on the object.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Invokes the widget's action and sends state change events if
 *   applicable.
 *----------------------------------------------------------------------
 */

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

    /* Call the widget's invoke method. */
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
        SendStateChanged(acc, ATSPI_STATE_CHECKED, 1); /* Value recomputed inside. */
    }

    return sd_bus_reply_method_return(m, "b", 1);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_action_get_name --
 *
 *   D-Bus method handler for GetName on the Action interface.
 *   Returns the name of the specified action.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a string.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * dbus_method_action_get_description --
 *
 *   D-Bus method handler for GetDescription on the Action interface.
 *   Returns the description of the specified action.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an empty string.
 *----------------------------------------------------------------------
 */

static int dbus_method_action_get_description(
	sd_bus_message *m,
	TCL_UNUSED(void *), /* userdata */
	TCL_UNUSED(sd_bus_error *))/* ret_error */
{
    return sd_bus_reply_method_return(m, "s", "");
}

/*
 *----------------------------------------------------------------------
 * dbus_method_action_get_key_binding --
 *
 *   D-Bus method handler for GetKeyBinding on the Action interface.
 *   Returns the key binding for the specified action.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an empty string.
 *----------------------------------------------------------------------
 */

static int dbus_method_action_get_key_binding(
	sd_bus_message *m,
	TCL_UNUSED(void *), /* userdata */
	TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "s", "");
}

/*
 *----------------------------------------------------------------------
 * dbus_method_value_get_current --
 *
 *   D-Bus method handler for GetCurrentValue on the Value interface.
 *   Returns the current value of the component.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a double.
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 * dbus_method_value_get_minimum --
 *
 *   D-Bus method handler for GetMinimumValue on the Value interface.
 *   Returns the minimum value of the component.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a double.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * dbus_method_value_get_maximum --
 *
 *   D-Bus method handler for GetMaximumValue on the Value interface.
 *   Returns the maximum value of the component.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a double.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * dbus_method_value_set_current --
 *
 *   D-Bus method handler for SetCurrentValue on the Value interface.
 *   Sets the current value of the component.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sets the widget's value and sends a value-changed event.
 *----------------------------------------------------------------------
 */

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
 *----------------------------------------------------------------------
 * dbus_method_text_get_text --
 *
 *   D-Bus method handler for GetText on the Text interface.
 *   Returns the text content of the component.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an empty string.
 *----------------------------------------------------------------------
 */
static int dbus_method_text_get_text(
    sd_bus_message *m,
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "s", "");
}

/*
 *----------------------------------------------------------------------
 * dbus_method_text_get_caret_offset --
 *
 *   D-Bus method handler for GetCaretOffset on the Text interface.
 *   Returns the current caret offset in the text.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with -1 (not implemented).
 *----------------------------------------------------------------------
 */

static int dbus_method_text_get_caret_offset(
    sd_bus_message *m,
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "i", -1);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_text_get_character_count --
 *
 *   D-Bus method handler for GetCharacterCount on the Text interface.
 *   Returns the number of characters in the text.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with 0 (not implemented).
 *----------------------------------------------------------------------
 */

static int dbus_method_text_get_character_count(
    sd_bus_message *m,
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "i", 0);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_selection_get_n_selections --
 *
 *   D-Bus method handler for GetNSelections on the Selection interface.
 *   Returns the number of selected items.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with 0 (not implemented).
 *----------------------------------------------------------------------
 */
static int dbus_method_selection_get_n_selections(
    sd_bus_message *m,
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "i", 0);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_selection_get_selection --
 *
 *   D-Bus method handler for GetSelection on the Selection interface.
 *   Returns the selected item at the given index.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a null reference (not implemented).
 *----------------------------------------------------------------------
 */

static int dbus_method_selection_get_selection(
    sd_bus_message *m,
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "(so)", "", "/org/a11y/atspi/null");
}

/*
 *----------------------------------------------------------------------
 * dbus_method_selection_is_selected --
 *
 *   D-Bus method handler for IsChildSelected on the Selection interface.
 *   Determines whether the item at the given index is selected.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with false (not implemented).
 *----------------------------------------------------------------------
 */

static int dbus_method_selection_is_selected(
    sd_bus_message *m,
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "b", 0);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_selection_select_all --
 *
 *   D-Bus method handler for SelectAll on the Selection interface.
 *   Selects all items.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with false (not implemented).
 *----------------------------------------------------------------------
 */

static int dbus_method_selection_select_all(
    sd_bus_message *m,
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "b", 0);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_selection_clear_selection --
 *
 *   D-Bus method handler for ClearSelection on the Selection interface.
 *   Clears all selections.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with false (not implemented).
 *----------------------------------------------------------------------
 */

static int dbus_method_selection_clear_selection(
    sd_bus_message *m,
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "b", 0);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_selection_add_selection --
 *
 *   D-Bus method handler for AddSelection on the Selection interface.
 *   Adds the item at the given index to the selection.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with false (not implemented).
 *----------------------------------------------------------------------
 */

static int dbus_method_selection_add_selection(
    sd_bus_message *m,
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "b", 0);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_selection_remove_selection --
 *
 *   D-Bus method handler for RemoveSelection on the Selection interface.
 *   Removes the item at the given index from the selection.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with false (not implemented).
 *----------------------------------------------------------------------
 */

static int dbus_method_selection_remove_selection(
    sd_bus_message *m,
    TCL_UNUSED(void *), /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    return sd_bus_reply_method_return(m, "b", 0);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_cache_get_items --
 *
 *   D-Bus method handler for GetItems on the Cache interface.
 *   Returns cached accessible items for the application.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an array of accessible references.
 *----------------------------------------------------------------------
 */

static int dbus_method_cache_get_items(
    sd_bus_message *m,
    void *userdata,
    sd_bus_error *ret_error)
{
    sd_bus_message *reply = NULL;
    int r;
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;
    /* Return empty cache - Orca will query children individually. */
    sd_bus_message_open_container(reply, 'a', "((so)a{sv})");
    /* Add root and toplevels. */
    if (atspi_conn && atspi_conn->root_accessible && atspi_conn->root_accessible->dbus_path) {
        sd_bus_message_open_container(reply, 'r', "(so)a{sv}");
        AppendAccessibleRef(reply, atspi_conn->root_accessible->dbus_path);
        sd_bus_message_open_container(reply, 'a', "{sv}");
        sd_bus_message_close_container(reply);
        sd_bus_message_close_container(reply);
        AccessibleList *l;
        for (l = atspi_conn->toplevel_accessibles; l; l = l->next) {
            TkAccessible *top = l->acc;
            if (!top || !top->dbus_path) continue;
            sd_bus_message_open_container(reply, 'r', "(so)a{sv}");
            AppendAccessibleRef(reply, top->dbus_path);
            sd_bus_message_open_container(reply, 'a', "{sv}");
            sd_bus_message_close_container(reply);
            sd_bus_message_close_container(reply);
        }
    }
    sd_bus_message_close_container(reply);
    return sd_bus_send(NULL, reply, NULL);
}

static const sd_bus_vtable cache_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetItems", "", "a((so)a{sv})", dbus_method_cache_get_items, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

/*
 *----------------------------------------------------------------------
 * RegisterDbusObject --
 *
 *   Register a TkAccessible object on the D-Bus with all appropriate
 *   interfaces based on its role.
 *
 * Results:
 *   Returns true on success, false on failure.
 *
 * Side effects:
 *   Creates D-Bus object paths and vtables for the accessible object.
 *----------------------------------------------------------------------
 */

static bool RegisterDbusObject(TkAccessible *acc)
{
    if (!atspi_conn || !atspi_conn->bus || !acc) {
        return false;
    }

    /* Generate a unique object path if not already set. */
    if (!acc->dbus_path) {
        static int counter = 0;
        char path[256];
        if (acc->tkwin && Tk_IsTopLevel(acc->tkwin)) {
            snprintf(path, sizeof(path), "/org/a11y/atspi/accessible/%s", Tk_PathName(acc->tkwin));
        } else {
            snprintf(path, sizeof(path), "/org/a11y/atspi/accessible/obj%d", counter++);
        }
        /* Replace dots with underscores for D-Bus path. */
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
        return false;
    }
    acc->vtable_slot = slot;

    /* Register Component interface (all objects support it). */
    sd_bus_add_object_vtable(atspi_conn->bus,
                              NULL,
                              acc->dbus_path,
                              ATSPI_COMPONENT_INTERFACE,
                              component_vtable,
                              acc);

    /*
     * Register Cache and Application interfaces on root only - both are
     * required for the registry/Orca to catalog us as an application.
     */
    if (acc == atspi_conn->root_accessible) {
        sd_bus_add_object_vtable(atspi_conn->bus,
                                  NULL,
                                  "/org/a11y/atspi/cache",
                                  "org.a11y.atspi.Cache",
                                  cache_vtable,
                                  acc);
        sd_bus_add_object_vtable(atspi_conn->bus,
                                  NULL,
                                  acc->dbus_path,
                                  "org.a11y.atspi.Application",
                                  application_vtable,
                                  acc);
    }

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

    return true;
}

/*
 *----------------------------------------------------------------------
 * EmitObjectEventFull --
 *
 *   Emit a full AT-SPI object event signal on the D-Bus.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Sends a D-Bus signal with the event details.
 *----------------------------------------------------------------------
 */

static void
EmitObjectEventFull(TkAccessible *acc,
		    const char *member,
		    const char *type, int32_t detail1,
		    int32_t detail2, TkAccessible *related)
{
    if (!atspi_conn || !atspi_conn->bus) return;
    if (!acc || !acc->dbus_path) return;
    if (!member || !type) return;

    /*
     * (so) = (bus-name, object-path). The related object, if any, is one
     * of ours, so its bus-name half is always our own unique name; the
     * canonical "no related object" reference uses an empty name and the
     * well-known null path (a bare "" is not a legal object path).
     */
    const char *rel_name = "";
    const char *rel_path = "/org/a11y/atspi/null";
    if (related && related->dbus_path) {
        rel_name = SelfBusName();
        rel_path = related->dbus_path;
    }

    /*
     * AT-SPI Event.Object signature is siiv where v = (so) . This matches
     * current at-spi2-core/xml/Event.xml and is what Orca expects.
     */
    int r = sd_bus_emit_signal(atspi_conn->bus,
                               acc->dbus_path,
                               "org.a11y.atspi.Event.Object",
                               member,
                               "siiv",
                               type,
                               detail1,
                               detail2,
                               "(so)", rel_name, rel_path);
    if (r < 0) {
        /* Don't crash, just debug. */
        fprintf(stderr, "EmitObjectEvent %s/%s failed: %d\n", member, type, r);
    }
}

/*
 *----------------------------------------------------------------------
 * EmitWindowEvent --
 *
 *   Emit an AT-SPI window event signal on the D-Bus.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Sends a D-Bus signal with the window event details.
 *----------------------------------------------------------------------
 */
static void
EmitWindowEvent(TkAccessible *acc, const char *member, const char *type)
{
    if (!atspi_conn || !atspi_conn->bus) return;
    if (!acc || !acc->dbus_path) return;
    if (!member || !type) return;

    int r = sd_bus_emit_signal(atspi_conn->bus,
                               acc->dbus_path,
                               "org.a11y.atspi.Event.Window",
                               member,
                               "siiv",
                               type,
                               0, 0,
                               "(so)", "", "/org/a11y/atspi/null");
    (void)r;
}

/*
 *----------------------------------------------------------------------
 * SendAtspiEvent --
 *
 *   Send an AT-SPI event for an accessible object.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Emits the appropriate D-Bus signal for the event type.
 *----------------------------------------------------------------------
 */

static void SendAtspiEvent(TkAccessible *acc,
			   const char *event_type,
			   const char *detail)
{
    if (!atspi_conn || !atspi_conn->bus || !acc || !acc->dbus_path) {
        return;
    }

    /*
     * event_type comes from ATSPI_EVENT_* constants like "focus",
     * "value-changed", "window:activate", etc.
     */
    if (strcmp(event_type, ATSPI_EVENT_FOCUS) == 0) {
        EmitObjectEventFull(acc, "Focus", "object:focus", 0, 0, NULL);
    } else if (strcmp(event_type, ATSPI_EVENT_VALUE_CHANGED) == 0) {
        EmitObjectEventFull(acc, "PropertyChange", "object:property-change:accessible-value", 0, 0, NULL);
    } else if (strcmp(event_type, ATSPI_EVENT_TEXT_CHANGED) == 0) {
        const char *t = "object:text-changed";
        if (detail) {
            char buf[128];
            snprintf(buf, sizeof(buf), "object:text-changed:%s", detail);
            EmitObjectEventFull(acc, "TextChanged", buf, 0, 0, NULL);
        } else {
            EmitObjectEventFull(acc, "TextChanged", t, 0, 0, NULL);
        }
    } else if (strcmp(event_type, ATSPI_EVENT_SELECTION_CHANGED) == 0) {
        EmitObjectEventFull(acc, "SelectionChanged", "object:selection-changed", 0, 0, NULL);
    } else if (strcmp(event_type, ATSPI_EVENT_WINDOW_ACTIVATE) == 0) {
        EmitWindowEvent(acc, "Activate", "window:activate");
    } else if (strcmp(event_type, ATSPI_EVENT_WINDOW_DEACTIVATE) == 0) {
        EmitWindowEvent(acc, "Deactivate", "window:deactivate");
    } else if (strcmp(event_type, ATSPI_EVENT_WINDOW_CREATE) == 0) {
        EmitWindowEvent(acc, "Create", "window:create");
    } else if (strncmp(event_type, "object:", 7) == 0) {
        /* Generic object event already fully qualified. */
        EmitObjectEventFull(acc, detail ? detail : "StateChanged", event_type, 0, 0, NULL);
    } else {
        /* Fallback: treat event_type as member, detail as type suffix. */
        char typebuf[256];
        if (detail) {
            snprintf(typebuf, sizeof(typebuf), "object:%s:%s", event_type, detail);
        } else {
            snprintf(typebuf, sizeof(typebuf), "object:%s", event_type);
        }
        /* Capitalize first letter for member? keep as is but ensure valid */
        EmitObjectEventFull(acc, "StateChanged", typebuf, 0, 0, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 * SendChildrenChanged --
 *
 *   Send a children-changed event for an accessible object.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Emits a D-Bus signal indicating that a child was added or removed.
 *----------------------------------------------------------------------
 */

static void SendChildrenChanged(TkAccessible *parent,
				int index,
				TkAccessible *child,
				int added)
{
    if (!parent || !child) return;
    if (!parent->dbus_path || !child->dbus_path) return;
    if (!atspi_conn || !atspi_conn->bus) return;

    const char *type = added ? "object:children-changed:add" : "object:children-changed:remove";
    /* detail1 = index, detail2 = 0, child in variant */
    EmitObjectEventFull(parent, "ChildrenChanged", type, (int32_t)index, 0, child);
}

/*
 *----------------------------------------------------------------------
 * StateBitToName --
 *
 *   Convert a state bit flag to its corresponding AT-SPI state name.
 *
 * Results:
 *   Returns a pointer to the state name string, or NULL if the bit
 *   is not recognized.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

static const char *StateBitToName(uint64_t bit)
{
    /*
     * Map single-bit state to at-spi name. bit is like ATSPI_STATE_FOCUSED etc.
     * The file defines bits as 1ULL<<n . We map by bit position.
     */
    if (bit == ATSPI_STATE_ENABLED) return "enabled";
    if (bit == ATSPI_STATE_SENSITIVE) return "sensitive";
    if (bit == ATSPI_STATE_FOCUSABLE) return "focusable";
    if (bit == ATSPI_STATE_FOCUSED) return "focused";
    if (bit == ATSPI_STATE_VISIBLE) return "visible";
    if (bit == ATSPI_STATE_SHOWING) return "showing";
    if (bit == ATSPI_STATE_EDITABLE) return "editable";
    if (bit == ATSPI_STATE_CHECKED) return "checked";
    if (bit == ATSPI_STATE_SELECTABLE) return "selectable";
    if (bit == ATSPI_STATE_SELECTED) return "selected";
    if (bit == ATSPI_STATE_ACTIVE) return "active";
    if (bit == ATSPI_STATE_EXPANDABLE) return "expandable";
    if (bit == ATSPI_STATE_EXPANDED) return "expanded";
    return NULL;
}

/*
 *----------------------------------------------------------------------
 * SendStateChanged --
 *
 *   Send a state-changed event for an accessible object.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Emits a D-Bus signal indicating that a state has changed.
 *----------------------------------------------------------------------
 */

static void SendStateChanged(TkAccessible *acc,
			     uint64_t state,
			     int value)
{
    if (!acc) return;
    if (!acc->dbus_path) return;
    if (!atspi_conn || !atspi_conn->bus) return;

    const char *name = StateBitToName(state);
    if (!name) {
        /* If state is already a bitmask with multiple bits, pick first set. */
        for (int i=0;i<64;i++) {
            uint64_t b = 1ULL<<i;
            if (state & b) {
                name = StateBitToName(b);
                if (name) break;
            }
        }
    }
    if (!name) name = "enabled";

    char type[128];
    snprintf(type, sizeof(type), "object:state-changed:%s", name);

    EmitObjectEventFull(acc, "StateChanged", type, (int32_t)(value ? 1 : 0), 0, NULL);
}

/*
 *----------------------------------------------------------------------
 * SendActiveDescendantChanged --
 *
 *   Send an active-descendant-changed event for an accessible object.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Emits a D-Bus signal indicating that the active descendant has changed.
 *----------------------------------------------------------------------
 */

static void SendActiveDescendantChanged(TkAccessible *container,
					TkAccessible *descendant)
{
    if (!container || !descendant) return;
    if (!container->dbus_path || !descendant->dbus_path) return;
    if (!atspi_conn || !atspi_conn->bus) return;

    EmitObjectEventFull(container, "ActiveDescendantChanged",
                        "object:active-descendant-changed",
                        0, 0, descendant);
}

/*
 *----------------------------------------------------------------------
 * BusFileHandlerProc --
 *
 *   Tcl file handler callback for the D-Bus socket. Processes pending
 *   D-Bus messages when the file descriptor is readable.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Processes D-Bus messages via sd_bus_process.
 *----------------------------------------------------------------------
 */
static void BusFileHandlerProc(void *clientData, int mask)
{
    AtspiConnection *conn = (AtspiConnection *)clientData;
    if (mask & TCL_READABLE) {
        sd_bus_process(conn->bus, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 * TclEventSetupProc --
 *
 *   Tcl event source setup callback. Creates a file handler for the
 *   D-Bus socket when window events are requested.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Creates a Tcl file handler for the D-Bus connection.
 *----------------------------------------------------------------------
 */

static void TclEventSetupProc(void *clientData,
			      int flags)
{
    AtspiConnection *conn = (AtspiConnection *)clientData;
    if (!(flags & TCL_WINDOW_EVENTS)) {
        return;
    }

    /* Ensure the bus file descriptor is watched. */
    if (!conn->file_handler) {
        int fd = sd_bus_get_fd(conn->bus);
        if (fd >= 0) {
            conn->bus_fd = fd;
            Tcl_CreateFileHandler(fd, TCL_READABLE, BusFileHandlerProc, conn);
            conn->file_handler = 1; /* dummy */
        }
    }
}

/*
 *----------------------------------------------------------------------
 * TclEventCheckProc --
 *
 *   Tcl event source check callback. Services pending D-Bus events when
 *   window events are requested.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   May trigger Tcl event processing if D-Bus events are pending.
 *----------------------------------------------------------------------
 */

static void TclEventCheckProc(void *clientData,
			      int flags)
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
 *----------------------------------------------------------------------
 * CreateAccessible --
 *
 *   Create a new TkAccessible object for a Tk window.
 *
 * Results:
 *   Returns a pointer to the newly created TkAccessible, or NULL on
 *   failure.
 *
 * Side effects:
 *   Allocates memory and registers the object on D-Bus.
 *----------------------------------------------------------------------
 */

static TkAccessible *CreateAccessible(Tcl_Interp *interp,
				      Tk_Window tkwin,
				      const char *path)
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

    /* Register D-Bus object. */
    if (!RegisterDbusObject(acc)) {
        Tcl_Free(acc->path);
        Tcl_Free(acc);
        return NULL;
    }

    return acc;
}

/*
 *----------------------------------------------------------------------
 * FreeAccessible --
 *
 *   Free a TkAccessible object and release its resources.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Deallocates memory and unregisters D-Bus objects.
 *----------------------------------------------------------------------
 */

static void FreeAccessible(TkAccessible *acc)
{
    if (!acc) return;

    if (acc->ref_count > 1) {
        acc->ref_count--;
        return;
    }

    /* Unregister D-Bus object (slot cleanup). */
    if (acc->vtable_slot) {
        sd_bus_slot_unref(acc->vtable_slot);
    }

    if (acc->path) Tcl_Free(acc->path);
    if (acc->dbus_path) Tcl_Free(acc->dbus_path);
    if (acc->virtual_name) Tcl_Free(acc->virtual_name);
    /* Free children list. */
    AccessibleList *l = acc->children;
    while (l) {
        AccessibleList *next = l->next;
        FreeAccessible(l->acc);
        Tcl_Free(l);
        l = next;
    }
    Tcl_Free(acc);
}

/*
 *----------------------------------------------------------------------
 * RegisterAccessible --
 *
 *   Register a TkAccessible in the global accessible map.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Adds the accessible to the hash table and toplevel list if applicable.
 *----------------------------------------------------------------------
 */

static void RegisterAccessible(Tk_Window tkwin,
			       TkAccessible *acc)
{
    if (!tkwin || !acc || !atspi_conn) return;

    int isNew;
    Tcl_HashEntry *entry = Tcl_CreateHashEntry(atspi_conn->tk_to_accessible_map, (char *)tkwin, &isNew);
    Tcl_SetHashValue(entry, acc);
    if (Tk_IsTopLevel(tkwin)) {
        RegisterToplevel(acc);
    }
}

/*
 *----------------------------------------------------------------------
 * GetAccessible --
 *
 *   Retrieve the TkAccessible for a Tk window from the global map.
 *
 * Results:
 *   Returns a pointer to the TkAccessible, or NULL if not found.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

static TkAccessible *
GetAccessible(Tk_Window tkwin)
{
    if (!atspi_conn) {
            return NULL;  /* Still failed. */
    }

    if (!atspi_conn->tk_to_accessible_map || !tkwin) {
        return NULL;
    }

    Tcl_HashEntry *entry = Tcl_FindHashEntry(atspi_conn->tk_to_accessible_map, (char *)tkwin);
    if (!entry) return NULL;
    return (TkAccessible *)Tcl_GetHashValue(entry);
}

/*
 *----------------------------------------------------------------------
 * UnregisterAccessible --
 *
 *   Remove a TkAccessible from the global accessible map.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Removes the accessible from the hash table and frees it.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * RegisterToplevel --
 *
 *   Register a toplevel accessible in the toplevel list.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Adds the toplevel to the list and sends a window-create event.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * UnregisterToplevel --
 *
 *   Remove a toplevel accessible from the toplevel list.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Removes the toplevel from the list.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * RegisterWidgetRecursive --
 *
 *   Recursively register all accessible children of a window.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Creates TkAccessible objects for all widgets in the hierarchy.
 *----------------------------------------------------------------------
 */

static void RegisterWidgetRecursive(Tcl_Interp *interp, Tk_Window tkwin)
{
    if (!tkwin) return;

    TkAccessible *acc = GetAccessible(tkwin);
    if (!acc) {
        acc = CreateAccessible(interp, tkwin, Tk_PathName(tkwin));
        if (!acc) return;

        /* Set parent relationship. */
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

/*
 *----------------------------------------------------------------------
 * EnsureAccessibleInHierarchy --
 *
 *   Ensure that a window and all its ancestors have accessible objects.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Creates TkAccessible objects for missing windows in the hierarchy.
 *----------------------------------------------------------------------
 */

static void EnsureAccessibleInHierarchy(Tcl_Interp *interp,
					Tk_Window tkwin)
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

    /* Process from root to leaf. */
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

/*
 *----------------------------------------------------------------------
 * UpdateFocusChain --
 *
 *   Update the focus state for a window and its ancestors.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Sends focus and activation events.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * GetToplevelOfWidget --
 *
 *   Get the toplevel window containing a widget.
 *
 * Results:
 *   Returns a pointer to the toplevel Tk_Window, or NULL if not found.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

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
 *----------------------------------------------------------------------
 * GetRoleForWidget --
 *
 *   Determine the AT-SPI role for a Tk widget.
 *
 * Results:
 *   Returns the AT-SPI role code for the widget.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 * ComputeStateForWidget --
 *
 *   Compute the AT-SPI state bitmask for a widget.
 *
 * Results:
 *   Returns a 64-bit unsigned integer with the state bits set.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

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
 *----------------------------------------------------------------------
 * GetNameForWidget --
 *
 *   Get the accessible name for a widget.
 *
 * Results:
 *   Returns a newly allocated string with the widget's name, or NULL
 *   if no name is set.
 *
 * Side effects:
 *   Memory is allocated via Tcl_Strdup.
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 * GetDescriptionForWidget --
 *
 *   Get the accessible description for a widget.
 *
 * Results:
 *   Returns a newly allocated string with the widget's description, or
 *   NULL if no description is set.
 *
 * Side effects:
 *   Memory is allocated via Tcl_Strdup.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * GetValueForWidget --
 *
 *   Get the accessible value for a widget.
 *
 * Results:
 *   Returns a newly allocated string with the widget's value, or NULL
 *   if no value is set.
 *
 * Side effects:
 *   Memory is allocated via Tcl_Strdup.
 *----------------------------------------------------------------------
 */

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
 *----------------------------------------------------------------------
 * TkAccessible_RegisterEventHandlers --
 *
 *   Register X event handlers for a Tk window.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Creates X event handlers for the window.
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 * TkAccessible_DestroyHandler --
 *
 *   X event handler for window destruction.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Unregisters the accessible object and sends removal events.
 *----------------------------------------------------------------------
 */

static void TkAccessible_DestroyHandler(void *clientData,
					XEvent *eventPtr)
{
    if (eventPtr->type != DestroyNotify) return;

    TkAccessible *acc = (TkAccessible *)clientData;
    if (!acc || !acc->tkwin) return;

    /* Notify that this object is going away. */
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

    /*
     * Remove all event handlers registered on this window *before* the
     * accessible is unregistered/freed below. Without this, a FocusOut
     * (or ConfigureNotify) generated for the same window during teardown
     * can still be dispatched to TkAccessible_FocusHandler/
     * TkAccessible_ConfigureHandler with a stale `acc` pointer that
     * UnregisterAccessible() -> FreeAccessible() has already released,
     * causing a use-after-free/segfault. This was previously worked
     * around by disabling the body of TkAccessible_FocusHandler; that
     * workaround is no longer needed now that the dangling handlers are
     * torn down here.
     */
    Tk_DeleteEventHandler(acc->tkwin, StructureNotifyMask,
                          TkAccessible_DestroyHandler, acc);
    Tk_DeleteEventHandler(acc->tkwin, FocusChangeMask,
                          TkAccessible_FocusHandler, acc);
    Tk_DeleteEventHandler(acc->tkwin, SubstructureNotifyMask,
                          TkAccessible_CreateHandler, acc);
    Tk_DeleteEventHandler(acc->tkwin, ConfigureNotify,
                          TkAccessible_ConfigureHandler, acc);

    UnregisterAccessible(acc->tkwin);
}

/*
 *----------------------------------------------------------------------
 * TkAccessible_FocusHandler --
 *
 *   X event handler for focus changes.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Updates focus state and sends focus events.
 *----------------------------------------------------------------------
 */

static void TkAccessible_FocusHandler(void *clientData,
				      XEvent *eventPtr)
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

    /* Notify parent about active descendant. */
    if (focused && acc->parent) {
        SendActiveDescendantChanged(acc->parent, acc);
    }
}

/*
 *----------------------------------------------------------------------
 * TkAccessible_CreateHandler --
 *
 *   X event handler for window creation.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Registers accessible objects for newly created windows.
 *----------------------------------------------------------------------
 */

static void TkAccessible_CreateHandler(void *clientData,
				       XEvent *eventPtr)
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
            /* Compute index. */
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

/*
 *----------------------------------------------------------------------
 * TkAccessible_ConfigureHandler --
 *
 *   X event handler for window configuration changes.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Updates geometry and visibility states.
 *----------------------------------------------------------------------
 */

static void TkAccessible_ConfigureHandler(void *clientData,
					  XEvent *eventPtr)
{
    if (!eventPtr || eventPtr->type != ConfigureNotify) return;

    Tk_Window tkwin = (Tk_Window)clientData;
    if (!tkwin) return;

    TkAccessible *acc = GetAccessible(tkwin);
    if (!acc) return;

    /* Update geometry. */
    acc->width = Tk_Width(tkwin);
    acc->height = Tk_Height(tkwin);
    Tk_GetRootCoords(tkwin, &acc->x, &acc->y);

    /* Update visibility states. */
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
 *----------------------------------------------------------------------
 * IsScreenReaderActive --
 *
 *   Check if a screen reader (Orca) is currently running.
 *
 * Results:
 *   Returns 1 if a screen reader is running, 0 otherwise.
 *
 * Side effects:
 *   Executes pgrep to check for Orca processes.
 *----------------------------------------------------------------------
 */

static int IsScreenReaderActive(void)
{

    FILE *fp = popen("pgrep -x orca 2>/dev/null; pgrep -f /orca 2>/dev/null | head -1", "r");
    if (!fp) return 0;
    char buffer[64];
    int running = (fgets(buffer, sizeof(buffer), fp) != NULL);
    pclose(fp);
    return running;
}

/*
 *----------------------------------------------------------------------
 * ConnectToAtspiBus --
 *
 *   Connect to the AT-SPI D-Bus.
 *
 * Results:
 *   Returns a pointer to the D-Bus connection, or NULL on failure.
 *
 * Side effects:
 *   Establishes a D-Bus connection.
 *----------------------------------------------------------------------
 */

static sd_bus *ConnectToAtspiBus(void)
{
    sd_bus *a11y_bus = NULL;
    sd_bus *session = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *addr = NULL;
    int r;

    /* Try env var first - set by at-spi-bus-launcher. */
    const char *env = getenv("AT_SPI_BUS");
    if (!env) env = getenv("AT_SPI_BUS_ADDRESS");
    if (env) {
        r = sd_bus_new(&a11y_bus);
        if (r >= 0) {
            r = sd_bus_set_address(a11y_bus, env);
            if (r >= 0) {
                r = sd_bus_start(a11y_bus);
                if (r >= 0) return a11y_bus;
            }
            sd_bus_unref(a11y_bus);
        }
    }

    /* Ask org.a11y.Bus for its address. */
    r = sd_bus_default_user(&session);
    if (r >= 0) {
        r = sd_bus_call_method(session,
            "org.a11y.Bus",
            "/org/a11y/bus",
            "org.a11y.Bus",
            "GetAddress",
            &error,
            &reply,
            "");
        if (r >= 0) {
            r = sd_bus_message_read(reply, "s", &addr);
            if (r >= 0 && addr) {
                r = sd_bus_new(&a11y_bus);
                if (r >= 0) {
                    r = sd_bus_set_address(a11y_bus, addr);
                    if (r >= 0) {
                        r = sd_bus_start(a11y_bus);
                        if (r >= 0) {
                            sd_bus_message_unref(reply);
                            sd_bus_error_free(&error);
                            sd_bus_unref(session);
                            return a11y_bus;
                        }
                    }
                    sd_bus_unref(a11y_bus);
                }
            }
        }
        sd_bus_error_free(&error);
        if (reply) sd_bus_message_unref(reply);

        /*
	 * If we can't get a11y bus, fall back to session bus.
         * Orca will still see us via registry on session bus in some setups.
         */
        sd_bus_ref(session);
        return session;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 * InitializeAtspiConnection --
 *
 *   Initialize the global AT-SPI connection.
 *
 * Results:
 *   Returns true on success, false on failure.
 *
 * Side effects:
 *   Allocates global structures and connects to D-Bus.
 *----------------------------------------------------------------------
 */

static bool
InitializeAtspiConnection(void)
{
    sd_bus *bus = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *msg = NULL;
    int r;

    if (atspi_conn && atspi_conn->is_initialized) {
        return true;
    }

    atspi_conn = (AtspiConnection *)Tcl_Alloc(sizeof(AtspiConnection));
    if (!atspi_conn) {
        return false;
    }
    memset(atspi_conn, 0, sizeof(AtspiConnection));

    /* Connect to AT-SPI bus, not just session bus. */
    bus = ConnectToAtspiBus();
    if (!bus) {
        Tcl_Free(atspi_conn);
        atspi_conn = NULL;
        return false;
    }
    atspi_conn->bus = bus;

    /* Initialize the hash table early. */
    atspi_conn->tk_to_accessible_map = (Tcl_HashTable *)Tcl_Alloc(sizeof(Tcl_HashTable));
    if (!atspi_conn->tk_to_accessible_map) {
        sd_bus_unref(bus);
        Tcl_Free(atspi_conn);
        atspi_conn = NULL;
        return false;
    }
    Tcl_InitHashTable(atspi_conn->tk_to_accessible_map, TCL_ONE_WORD_KEYS);

    /*
     * Check if AT-SPI registry is actually running.
     * On the a11y bus, registry is at org.a11y.atspi.Registry, but we query via session bus
     * org.freedesktop.DBus ListNames and also try GetNameOwner on a11y bus.
     */
    r = sd_bus_call_method(bus,
                           "org.freedesktop.DBus",
                           "/org/freedesktop/DBus",
                           "org.freedesktop.DBus",
                           "GetNameOwner",
                           &error,
                           &msg,
                           "s", "org.a11y.atspi.Registry");

    if (r < 0) {
        /* Try alternative: ListNames on session bus. */
        sd_bus *session = NULL;
        if (sd_bus_default_user(&session) >= 0) {
            sd_bus_message *list_msg = NULL;
            r = sd_bus_call_method(session,
                "org.freedesktop.DBus",
                "/org/freedesktop/DBus",
                "org.freedesktop.DBus",
                "ListNames",
                &error,
                &list_msg,
                "");
            if (r >= 0) {
                /* If call succeeded, assume registry might still appear later - don't fail. */
                sd_bus_message_unref(list_msg);
                r = 0; /* Force success. */
            }
            sd_bus_unref(session);
        }
        if (r < 0) {
            sd_bus_error_free(&error);
            if (msg) sd_bus_message_unref(msg);
            /*
	     * Don't hard-fail here - allow initialization even if
	     * registry not yet up. Orca will discover us when it appears.
             */
            sd_bus_error_free(&error);
        }
    }
    if (msg) sd_bus_message_unref(msg);
    sd_bus_error_free(&error);

    /* Create root accessible object (application). */
    atspi_conn->root_accessible = (TkAccessible *)Tcl_Alloc(sizeof(TkAccessible));
    if (!atspi_conn->root_accessible) {
        Tcl_DeleteHashTable(atspi_conn->tk_to_accessible_map);
        Tcl_Free(atspi_conn->tk_to_accessible_map);
        sd_bus_unref(bus);
        Tcl_Free(atspi_conn);
        atspi_conn = NULL;
        return false;
    }
    memset(atspi_conn->root_accessible, 0, sizeof(TkAccessible));

    atspi_conn->root_accessible->role       = ATSPI_ROLE_APPLICATION;
    atspi_conn->root_accessible->path       = Tcl_Strdup("application");
    atspi_conn->root_accessible->dbus_path  = Tcl_Strdup("/org/a11y/atspi/accessible/root");
    atspi_conn->root_accessible->ref_count  = 1;

    RegisterDbusObject(atspi_conn->root_accessible);

    /*
     * Register as an AT-SPI application via the real Socket.Embed
     * handshake. Not fatal if the registry isn't up yet - it can still
     * discover us later.
     */
    atspi_conn->is_initialized = 1;
    EmbedWithRegistry();

    /* Integrate with Tcl event loop for DBus handling. */
    Tcl_CreateEventSource(TclEventSetupProc, TclEventCheckProc, atspi_conn);

    return true;
}

/*
 *----------------------------------------------------------------------
 * EmbedWithRegistry --
 *
 *   Embed our application into the registry's accessible tree using
 *   the Socket.Embed method.
 *
 * Results:
 *   Returns true on success, false on failure.
 *
 * Side effects:
 *   Stores the desktop reference in the global connection state.
 *----------------------------------------------------------------------
 */
static bool EmbedWithRegistry(void)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *desktop_name = NULL;
    const char *desktop_path = NULL;
    int r;

    if (!atspi_conn || !atspi_conn->bus || !atspi_conn->root_accessible) {
        return false;
    }

    r = sd_bus_call_method(atspi_conn->bus,
        "org.a11y.atspi.Registry",
        "/org/a11y/atspi/accessible/root",
        "org.a11y.atspi.Socket",
        "Embed",
        &error,
        &reply,
        "(so)", SelfBusName(), atspi_conn->root_accessible->dbus_path);

    if (r < 0) {
        /*
	 * Registry may not be up yet; not fatal - Orca can still discover
         * us later via broadcast events once it does start.
	 */
        sd_bus_error_free(&error);
        if (reply) sd_bus_message_unref(reply);
        return false;
    }

    r = sd_bus_message_read(reply, "(so)", &desktop_name, &desktop_path);
    if (r >= 0 && desktop_name && desktop_path) {
        if (atspi_conn->desktop_bus_name) Tcl_Free(atspi_conn->desktop_bus_name);
        if (atspi_conn->desktop_path) Tcl_Free(atspi_conn->desktop_path);
        atspi_conn->desktop_bus_name = Tcl_Strdup(desktop_name);
        atspi_conn->desktop_path = Tcl_Strdup(desktop_path);
    }

    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return true;
}

/*
 *----------------------------------------------------------------------
 * AddAccessibleCmd --
 *
 *   Tcl command implementation for ::tk::accessible::add_acc_object.
 *   Registers a window and its children as accessible objects.
 *
 * Results:
 *   Returns TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *   Creates accessible objects for the window hierarchy.
 *----------------------------------------------------------------------
 */

static int AddAccessibleCmd(
	TCL_UNUSED(void *),
	Tcl_Interp *interp,
	int objc,
	Tcl_Obj*const objv[])
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

/*
 *----------------------------------------------------------------------
 * EmitSelectionChangedCmd --
 *
 *   Tcl command implementation for ::tk::accessible::emit_selection_change.
 *   Emits a selection changed event for a widget.
 *
 * Results:
 *   Returns TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *   Sends a D-Bus event signal.
 *----------------------------------------------------------------------
 */

static int EmitSelectionChangedCmd(
	TCL_UNUSED(void *),
	Tcl_Interp *interp,
	int objc,
	Tcl_Obj*const objv[])
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

/*
 *----------------------------------------------------------------------
 * EmitFocusChangedCmd --
 *
 *   Tcl command implementation for ::tk::accessible::emit_focus_change.
 *   Emits a focus changed event for a widget.
 *
 * Results:
 *   Returns TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *   Sends a D-Bus event signal.
 *----------------------------------------------------------------------
 */

static int EmitFocusChangedCmd(
    TCL_UNUSED(void *), /* void **/
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
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

    /*
     * Real widget focus (e.g. Entry, Button) is already reported by
     * TkAccessible_FocusHandler off real X FocusIn/FocusOut events. This
     * command exists for the cases the Tcl layer drives "focus" itself
     * without a corresponding X event - most notably active menu entries,
     * which change via <<MenuSelect>>/arrow-key navigation rather than
     * real window focus. Report the focused state and, for a container
     * like a menu, notify the parent of the new active descendant.
     */
    acc->is_focused = 1;
    acc->states |= ATSPI_STATE_FOCUSED;
    SendStateChanged(acc, ATSPI_STATE_FOCUSED, 1);
    SendAtspiEvent(acc, ATSPI_EVENT_FOCUS, NULL);

    if (acc->parent) {
        SendActiveDescendantChanged(acc->parent, acc);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 * IsScreenReaderRunningCmd --
 *
 *   Tcl command implementation for ::tk::accessible::check_screenreader.
 *   Checks if a screen reader is currently running.
 *
 * Results:
 *   Returns TCL_OK with a boolean result.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

static int IsScreenReaderRunningCmd(
    TCL_UNUSED(void *), /* void **/
    Tcl_Interp *interp,
    TCL_UNUSED(int), /* objc */
    TCL_UNUSED(Tcl_Obj *const *)) /* objv */
{
    bool result = IsScreenReaderActive();

    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(result));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 * TkWaylandAccessibility_Init --
 *
 *   Initialize the Wayland accessibility module.
 *
 * Results:
 *   Returns TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *   Initializes D-Bus connection, registers accessible objects, and
 *   creates Tcl commands.
 *----------------------------------------------------------------------
 */
int TkWaylandAccessibility_Init(Tcl_Interp *interp)
{
    /* Initialize D-Bus connection to at-spi. */
    if (!InitializeAtspiConnection()) {
	Tcl_AppendResult(interp, "Warning: Could not connect to AT-SPI - accessibility disabled for now", (char *)NULL);
	/* Proceed anyway – don't block Tk init. */
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
