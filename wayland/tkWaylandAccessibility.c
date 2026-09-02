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

/* Debugging
#define DEBUG_CHANNEL stderr
#define DEBUG_LABEL "accessibility"
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

/* at-spi D-Bus constants. */
#define ATSPI_DBUS_NAME           "org.a11y.Bus"
#define ATSPI_DBUS_PATH           "/org/a11y/bus"
#define ATSPI_REGISTRY_INTERFACE  "org.a11y.atspi.Registry"
#define ATSPI_ACCESSIBLE_INTERFACE "org.a11y.atspi.Accessible"
#define ATSPI_ACTION_INTERFACE    "org.a11y.atspi.Action"
#define ATSPI_COMPONENT_INTERFACE "org.a11y.atspi.Component"
#define ATSPI_VALUE_INTERFACE     "org.a11y.atspi.Value"
#define ATSPI_EVENT_INTERFACE     "org.a11y.atspi.Event"
#define ATSPI_CACHE_INTERFACE     "org.a11y.atspi.Cache"

/* at-spi D-Bus paths. */
#define ATSPI_DBUS_PATH_REGISTRY  "/org/a11y/atspi/registry"
#define ATSPI_DBUS_PATH_ROOT      "/org/a11y/atspi/accessible/root"

/*
 * at-spi role constants.
 */
#define ATSPI_ROLE_INVALID           0
#define ATSPI_ROLE_FRAME             23
#define ATSPI_ROLE_APPLICATION       75
#define ATSPI_ROLE_DIALOG            16
#define ATSPI_ROLE_MENU_ITEM         35
#define ATSPI_ROLE_TREE_ITEM         91
#define ATSPI_ROLE_PAGE_TAB_LIST     38
#define ATSPI_ROLE_TABLE             55
#define ATSPI_ROLE_TABLE_CELL        56
#define ATSPI_ROLE_SCROLL_PANE       49
#define ATSPI_ROLE_SEPARATOR         50
#define ATSPI_ROLE_PUSH_BUTTON       43
#define ATSPI_ROLE_CHECK_BOX         7
#define ATSPI_ROLE_COMBO_BOX         11
#define ATSPI_ROLE_ENTRY             79
#define ATSPI_ROLE_LABEL             29
#define ATSPI_ROLE_LIST_BOX          98
#define ATSPI_ROLE_MENU              33
#define ATSPI_ROLE_MENU_BAR          34
#define ATSPI_ROLE_TREE              65
#define ATSPI_ROLE_PAGE_TAB          37
#define ATSPI_ROLE_PROGRESS_BAR      42
#define ATSPI_ROLE_RADIO_BUTTON      44
#define ATSPI_ROLE_SLIDER            51
#define ATSPI_ROLE_SPIN_BUTTON       52
#define ATSPI_ROLE_TREE_TABLE        66
#define ATSPI_ROLE_TEXT              61
#define ATSPI_ROLE_WINDOW            69
#define ATSPI_ROLE_PANEL             39
#define ATSPI_ROLE_CANVAS            6
#define ATSPI_ROLE_SCROLL_BAR        48
#define ATSPI_ROLE_TOGGLE_BUTTON     62

/* 
 * at-spi state constants (bit flags).
 */
#define ATSPI_STATE_ACTIVE           (1ULL << 1)
#define ATSPI_STATE_CHECKED          (1ULL << 4)
#define ATSPI_STATE_EDITABLE         (1ULL << 7)
#define ATSPI_STATE_ENABLED          (1ULL << 8)
#define ATSPI_STATE_EXPANDABLE       (1ULL << 9)
#define ATSPI_STATE_EXPANDED         (1ULL << 10)
#define ATSPI_STATE_FOCUSABLE        (1ULL << 11)
#define ATSPI_STATE_FOCUSED          (1ULL << 12)
#define ATSPI_STATE_SELECTABLE       (1ULL << 22)
#define ATSPI_STATE_SELECTED         (1ULL << 23)
#define ATSPI_STATE_SENSITIVE        (1ULL << 24)
#define ATSPI_STATE_SHOWING          (1ULL << 25)
#define ATSPI_STATE_VISIBLE          (1ULL << 30)

/* at-spi event types. */
#define ATSPI_EVENT_FOCUS             "focus"
#define ATSPI_EVENT_STATE_CHANGED     "state-changed"
#define ATSPI_EVENT_VALUE_CHANGED     "value-changed"
#define ATSPI_EVENT_WINDOW_ACTIVATE   "window:activate"
#define ATSPI_EVENT_WINDOW_DEACTIVATE "window:deactivate"
#define ATSPI_EVENT_WINDOW_CREATE     "window:create"
#define ATSPI_EVENT_CHILDREN_CHANGED  "children-changed"

/*
 * Core structures for Tk accessibility and at-spi data.
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
    int role;                /* Only authoritative for accessibles with no
                              * tkwin (e.g. the root "application" object).
                              * For real widgets, always call GetLiveRole()
                              * instead of reading this directly -- it is
                              * not kept in sync with the widget. */
    uint64_t states;
    int x, y, width, height;
    int is_focused;
    int ref_count;
    char *cached_name;       /* Last name seen by Reconcile, so we only
                              * emit accessible-name PropertyChange when
                              * it actually changes (e.g. a label's
                              * -text is updated after creation). */

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

    /*
     * D-Bus slots for this object (for cleanup). An accessible can have
     * several interfaces registered on the same dbus_path (Accessible,
     * Component, and one of Action/Value, plus Application for the root).
     * Every sd_bus_add_object_vtable() call must have its slot captured
     * here so FreeAccessible() can unref all of them.
     */
#define TK_ACCESSIBLE_MAX_SLOTS 8
    sd_bus_slot *vtable_slots[TK_ACCESSIBLE_MAX_SLOTS];
    int n_vtable_slots;
    int action_vtable_added;   /* 1 once the Action interface has been
                                * added on the bus for this object -- role
                                * can become known after creation, so this
                                * guards against adding it twice. */
    int value_vtable_added;   /* Same, for the Value interface. */
};

/* Global connection state. */
typedef struct {
    sd_bus *bus;
    int is_initialized;
    Tcl_HashTable *tk_to_accessible_map;   /* key = Tk_Window, value = TkAccessible*. */
    AccessibleList *toplevel_accessibles;
    TkAccessible *root_accessible;

    /*
     * Desktop reference returned by Socket.Embed - root_accessible's
     * effective parent once we've been embedded in the registry's tree.
     */
    char *desktop_bus_name;
    char *desktop_path;
    int is_embedded;                     /* true if Embed succeeded */
} AtspiConnection;

/*
 * Forward declarations of functions defined in this file.
 */

/* Core functions. */
static void EnsureAccessibleInHierarchy(Tcl_Interp *interp, Tk_Window tkwin);
static TkAccessible *CreateAccessible(Tcl_Interp *interp, Tk_Window tkwin, const char *path);
static void RegisterAccessible(Tk_Window tkwin, TkAccessible *acc);
static TkAccessible *GetAccessible(Tk_Window tkwin);
static void UnregisterAccessible(Tk_Window tkwin);
static void FreeAccessible(TkAccessible *acc);
static int GetRoleForWidget(Tk_Window tkwin);
static int GetLiveRole(TkAccessible *acc);
static void EnsureRoleVtables(TkAccessible *acc, int notifyIfChanged);
static uint64_t ComputeStateForWidget(TkAccessible *acc);
static char *GetNameForWidget(Tk_Window tkwin);
static char *GetDescriptionForWidget(Tk_Window tkwin);
static char *GetValueForWidget(Tk_Window tkwin);
static void RegisterToplevel(TkAccessible *acc);
static void UnregisterToplevel(TkAccessible *acc);
static void RegisterWidgetRecursive(Tcl_Interp *interp, Tk_Window tkwin);
static void EnsureChildrenRegistered(Tk_Window tkwin,int emitEvents);
static void EnsureChildrenRegisteredRecursive(Tk_Window tkwin, TkAccessible *parent_acc, int emitEvents);
static void UpdateFocusChain(Tk_Window focused);
static void SetAccessibleFocus(TkAccessible *acc, int focused);
static void TkAccessible_Reconcile(TkAccessible *acc);

/* D-Bus vtables and method handlers. */
static const sd_bus_vtable accessible_vtable[];
static const sd_bus_vtable component_vtable[];
static const sd_bus_vtable action_vtable[];
static const sd_bus_vtable value_vtable[];
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
static int dbus_method_get_state(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_role(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_prop_get_name(sd_bus *bus, const char *path, const char *interface,
                               const char *property, sd_bus_message *reply,
                               void *userdata, sd_bus_error *ret_error);
static int dbus_prop_get_description(sd_bus *bus, const char *path, const char *interface,
                                      const char *property, sd_bus_message *reply,
                                      void *userdata, sd_bus_error *ret_error);
static int dbus_prop_get_parent(sd_bus *bus, const char *path, const char *interface,
                                 const char *property, sd_bus_message *reply,
                                 void *userdata, sd_bus_error *ret_error);
static int dbus_prop_get_child_count(sd_bus *bus, const char *path, const char *interface,
                                     const char *property, sd_bus_message *reply,
                                     void *userdata, sd_bus_error *ret_error);
static int dbus_method_grab_focus(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_index_in_parent(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_interfaces(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_application(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);

/* at-spi component interface. */
static int dbus_method_component_get_extents(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_component_get_position(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_component_get_size(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_component_contains(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_component_get_accessible_at_point(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
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

/* Event emission. */
static void SendAtspiEvent(TkAccessible *acc, const char *event_type, const char *detail);
static void SendChildrenChanged(TkAccessible *parent, int index, TkAccessible *child, int added);
static void SendStateChanged(TkAccessible *acc, uint64_t state, int value);
static const sd_bus_vtable cache_vtable[];
static int dbus_method_cache_get_items(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);

static void EmitObjectEventFull(TkAccessible *acc, const char *member, const char *type,
                                int32_t detail1, int32_t detail2, TkAccessible *related);
static void EmitFocusEvent(TkAccessible *acc);
static void EmitWindowEvent(TkAccessible *acc, const char *member, const char *type);
static void PostAccessibilityAnnouncement(TkAccessible *acc, const char *message, int priority);

/* X Event handlers. */
static void TkAccessible_DestroyHandler(void *clientData, XEvent *eventPtr);
static void TkAccessible_FocusHandler(void *clientData, XEvent *eventPtr);
static void TkAccessible_CreateHandler(void *clientData, XEvent *eventPtr);
static void TkAccessible_ConfigureHandler(void *clientData, XEvent *eventPtr);
static void TkAccessible_RegisterEventHandlers(Tk_Window tkwin, TkAccessible *acc);

/* Tcl event loop integration. */
void TkWaylandAtspiProcessEvents(void);

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
    {"Toplevel",      ATSPI_ROLE_FRAME},
    {"Frame",         ATSPI_ROLE_PANEL},
    {"Canvas",        ATSPI_ROLE_CANVAS},
    {"Scrollbar",     ATSPI_ROLE_SCROLL_BAR},
    {"Toggleswitch",  ATSPI_ROLE_TOGGLE_BUTTON},
    {NULL,            ATSPI_ROLE_INVALID}
};

/* Helper getter for role string. */
static const char *
RoleToString(int role)
{
    switch (role) {
        case ATSPI_ROLE_INVALID: return "invalid";
        case ATSPI_ROLE_APPLICATION: return "application";
        case ATSPI_ROLE_FRAME: return "frame";
        case ATSPI_ROLE_WINDOW: return "window";
        case ATSPI_ROLE_DIALOG: return "dialog";
        case ATSPI_ROLE_PUSH_BUTTON: return "push_button";
        case ATSPI_ROLE_CHECK_BOX: return "check_box";
        case ATSPI_ROLE_RADIO_BUTTON: return "radio_button";
        case ATSPI_ROLE_ENTRY: return "entry";
        case ATSPI_ROLE_LABEL: return "label";
        case ATSPI_ROLE_LIST_BOX: return "list_box";
        case ATSPI_ROLE_COMBO_BOX: return "combo_box";
        case ATSPI_ROLE_MENU: return "menu";
        case ATSPI_ROLE_MENU_BAR: return "menu_bar";
        case ATSPI_ROLE_MENU_ITEM: return "menu_item";
        case ATSPI_ROLE_TREE: return "tree";
        case ATSPI_ROLE_TREE_ITEM: return "tree_item";
        case ATSPI_ROLE_PAGE_TAB: return "page_tab";
        case ATSPI_ROLE_PAGE_TAB_LIST: return "page_tab_list";
        case ATSPI_ROLE_PROGRESS_BAR: return "progress_bar";
        case ATSPI_ROLE_SLIDER: return "slider";
        case ATSPI_ROLE_SPIN_BUTTON: return "spin_button";
        case ATSPI_ROLE_TREE_TABLE: return "tree_table";
        case ATSPI_ROLE_TABLE: return "table";
        case ATSPI_ROLE_TABLE_CELL: return "table_cell";
        case ATSPI_ROLE_TEXT: return "text";
        case ATSPI_ROLE_PANEL: return "panel";
        case ATSPI_ROLE_CANVAS: return "canvas";
        case ATSPI_ROLE_SCROLL_BAR: return "scroll_bar";
        case ATSPI_ROLE_SCROLL_PANE: return "scroll_pane";
        case ATSPI_ROLE_TOGGLE_BUTTON: return "toggle_button";
        case ATSPI_ROLE_SEPARATOR: return "separator";
        default: return "unknown";
    }
}

static AtspiConnection *atspi_conn = NULL;
extern Tcl_HashTable *TkAccessibilityObject;  /* from tkAccessibility.c */

/*
 * Non-static handle to the AT-SPI bus, mirroring ibus_bus in tkWaylandKey.c.
 * The Wayland notifier (tkWaylandNotify.c) drains this via
 * TkWaylandAtspiProcessEvents() on its own check-proc cadence.
 */
sd_bus *atspi_bus = NULL;

/*
 * Re-entrancy guard for sd_bus_process on atspi_bus, mirroring
 * ibus_draining in tkWaylandKey.c.  A dispatched AT-SPI method call (e.g.
 * Orca invoking GrabFocus, which runs from inside sd_bus_process and can
 * itself call back into Tk via Tcl_Eval) must not trigger a nested
 * sd_bus_process on the same bus.
 */
int atspi_draining = 0;

/*
 * D-Bus vtables - these map functions to the ati-spi API.
 */

/*
 * org.a11y.atspi.Accessible interface.
 */
static const sd_bus_vtable accessible_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Name", "s", dbus_prop_get_name, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Description", "s", dbus_prop_get_description, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Parent", "(so)", dbus_prop_get_parent, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ChildCount", "i", dbus_prop_get_child_count, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_METHOD("GetChildren", "", "a(so)", dbus_method_get_children, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetChildAtIndex", "i", "(so)", dbus_method_get_child_at_index, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetAttributes", "", "a{ss}", dbus_method_get_attributes, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetState", "", "au", dbus_method_get_state, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetRole", "", "u", dbus_method_get_role, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GrabFocus", "", "b", dbus_method_grab_focus, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetIndexInParent", "", "i", dbus_method_get_index_in_parent, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetInterfaces", "", "as", dbus_method_get_interfaces, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetApplication", "", "(so)", dbus_method_get_application, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

/* org.a11y.atspi.Component interface. */
static const sd_bus_vtable component_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetExtents", "i", "(iiii)", dbus_method_component_get_extents, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetPosition", "i", "(ii)", dbus_method_component_get_position, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetSize", "", "(ii)", dbus_method_component_get_size, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Contains", "iii", "b", dbus_method_component_contains, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetAccessibleAtPoint", "iii", "(so)", dbus_method_component_get_accessible_at_point, SD_BUS_VTABLE_UNPRIVILEGED),
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

static int
dbus_prop_get_toolkit_name(
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

static int
dbus_prop_get_version(
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

static int
dbus_prop_get_atspi_version(
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

static int
dbus_prop_get_id(
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

static int
dbus_prop_set_id(
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

static const char *
SelfBusName(void)
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

static int
AppendAccessibleRef(
    sd_bus_message *reply,  /* D-Bus message to append to. */
    const char *path)       /* Object path to reference, or NULL for null ref. */
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

static int
dbus_method_get_children(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    sd_bus_message *reply = NULL;
    int r;

    if (!acc || !atspi_conn) {
        return sd_bus_reply_method_return(m, "a(so)", 0);
    }
    if (acc->tkwin) {
        EnsureChildrenRegistered(acc->tkwin, 0);
    }
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
    } else if (acc->children) {
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

static int
dbus_method_get_child_at_index(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{

    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t index;
    sd_bus_message *reply = NULL;
    int r;
    
    if (acc && acc->tkwin) EnsureChildrenRegistered(acc->tkwin, 0);

    if (!acc || !atspi_conn) {
        return sd_bus_reply_method_return(m, "(so)", "", "/org/a11y/atspi/null");
    }

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
	/*
         * Count only accessible children, skipping non-Tk windows like
         * listbox rows.
         */
        TkWindow *childPtr;
        int acc_idx = 0;
        for (childPtr = ((TkWindow*)acc->tkwin)->childList;
             childPtr != NULL;
             childPtr = childPtr->nextPtr) {
            TkAccessible *child_acc = GetAccessible((Tk_Window)childPtr);
            if (!child_acc || !child_acc->dbus_path) continue;
            if (acc_idx == index) {
                AppendAccessibleRef(reply, child_acc->dbus_path);
                break;
            }
            acc_idx++;
        }
        if (!childPtr) {
            AppendAccessibleRef(reply, NULL);
        }
    } else if (acc->children) {
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
    } else {
        AppendAccessibleRef(reply, NULL);
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

static int
dbus_method_get_attributes(
    sd_bus_message *m,      /* D-Bus method call message. */
    TCL_UNUSED(void *),     /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    sd_bus_message_open_container(reply, 'a', "{ss}");
    sd_bus_message_close_container(reply);
    return sd_bus_send(NULL, reply, NULL);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_state --
 *
 *   D-Bus method handler for GetState on the Accessible interface.
 *   Returns the bitmask of states for the accessible object.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an array of two uint32s.
 *----------------------------------------------------------------------
 */

static int
dbus_method_get_state(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    if (acc) TkAccessible_Reconcile(acc);
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    uint64_t states = acc ? acc->states : 0;
    uint32_t lo = (uint32_t)(states & 0xffffffffu);
    uint32_t hi = (uint32_t)((states >> 32) & 0xffffffffu);

    sd_bus_message_open_container(reply, 'a', "u");
    sd_bus_message_append(reply, "u", lo);
    sd_bus_message_append(reply, "u", hi);
    sd_bus_message_close_container(reply);

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
 *   Sends a D-Bus reply message with a uint32 role code.
 *----------------------------------------------------------------------
 */

static int
dbus_method_get_role(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    if (acc) TkAccessible_Reconcile(acc);
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;
    sd_bus_message_append(reply, "u", (uint32_t)GetLiveRole(acc));
    return sd_bus_send(NULL, reply, NULL);
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_name --
 *
 *   D-Bus property getter for Name on the Accessible interface. Name is
 *   a property per the AT-SPI2 spec, not a method -- Orca reads it via
 *   Properties.Get/GetAll, not by calling "GetName".
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Appends the widget's accessible name to the D-Bus reply message.
 *----------------------------------------------------------------------
 */

static int
dbus_prop_get_name(
    TCL_UNUSED(sd_bus *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    sd_bus_message *reply,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    TkAccessible *acc = (TkAccessible *)userdata;
    if (acc) TkAccessible_Reconcile(acc);
    const char *name = "";
    char *live_name = NULL;
    if (acc) {
        if (acc->is_virtual && acc->virtual_name) {
            name = acc->virtual_name;
        } else if (acc->tkwin) {
            live_name = GetNameForWidget(acc->tkwin);
            if (live_name && live_name[0]) {
                name = live_name;
            } else if (acc->path) {
                if (acc->path[0]=='.' && acc->path[1]=='\0') {
                    name = "Tk Application";
                } else {
                    name = acc->path;
                }
            }
        } else {
            /* No tkwin (e.g. root application object) - use path or fallback. */
            if (acc->path && acc->path[0]=='.' && acc->path[1]=='\0') {
                name = "Tk Application";
            } else if (acc->path && strcmp(acc->path, "application")==0) {
                name = "Tk";
            } else if (acc->path) {
                name = acc->path;
            }
        }
    }
    int ret = sd_bus_message_append(reply, "s", name);
    if (live_name) free(live_name);
    return ret;
}


/*
 *----------------------------------------------------------------------
 * dbus_prop_get_description --
 *
 *   D-Bus property getter for Description on the Accessible interface.
 *   Description is a property per the AT-SPI2 spec, not a method.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Appends the widget's accessible description to the D-Bus reply
 *   message.
 *----------------------------------------------------------------------
 */


static int
dbus_prop_get_description(
    TCL_UNUSED(sd_bus *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    sd_bus_message *reply,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    TkAccessible *acc = (TkAccessible *)userdata;
    if (acc) TkAccessible_Reconcile(acc);
    const char *desc = "";
    char *live_desc = NULL;
    if (acc && acc->tkwin) {
        live_desc = GetDescriptionForWidget(acc->tkwin);
        if (live_desc) desc = live_desc;
    }
    int ret = sd_bus_message_append(reply, "s", desc);
    if (live_desc) free(live_desc);
    return ret;
}


/*
 *----------------------------------------------------------------------
 * dbus_prop_get_parent --
 *
 *   D-Bus property getter for Parent on the Accessible interface.
 *   Parent is a property per the AT-SPI2 spec, not a method.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Appends an (so) accessible reference to the D-Bus reply message.
 *----------------------------------------------------------------------
 */


static int
dbus_prop_get_parent(
    TCL_UNUSED(sd_bus *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    sd_bus_message *reply,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    TkAccessible *acc = (TkAccessible *)userdata;

    if (!acc || !atspi_conn) {
        return AppendAccessibleRef(reply, NULL);
    }

    if (acc == atspi_conn->root_accessible &&
        atspi_conn->desktop_bus_name && atspi_conn->desktop_path) {
        return sd_bus_message_append(reply, "(so)",
                                      atspi_conn->desktop_bus_name,
                                      atspi_conn->desktop_path);
    } else if (acc->parent && acc->parent->dbus_path) {
        if (acc->parent == acc || (acc->parent->dbus_path && acc->dbus_path && strcmp(acc->parent->dbus_path, acc->dbus_path)==0)) {
            DEBUG_LOG("dbus_prop_get_parent: SELF-PARENT for %s, returning NULL", acc->dbus_path);
            return AppendAccessibleRef(reply, NULL);
        }
        return AppendAccessibleRef(reply, acc->parent->dbus_path);
    }
    /* For toplevel windows, parent is root application if not set. */
    if (acc->tkwin && Tk_IsTopLevel(acc->tkwin) &&
        atspi_conn && atspi_conn->root_accessible &&
        atspi_conn->root_accessible->dbus_path) {
        if (acc != atspi_conn->root_accessible) {
            return AppendAccessibleRef(reply, atspi_conn->root_accessible->dbus_path);
        }
    }
    return AppendAccessibleRef(reply, NULL);
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_child_count --
 *
 *   D-Bus property getter for ChildCount on the Accessible interface.
 *   ChildCount is a property per the AT-SPI2 spec, not a method.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Appends an integer count of child accessible objects to the D-Bus
 *   reply message.
 *----------------------------------------------------------------------
 */

static int
dbus_prop_get_child_count(
    TCL_UNUSED(sd_bus *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    sd_bus_message *reply,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int cnt = 0;
    if (!acc || !atspi_conn) {
        return sd_bus_message_append(reply, "i", 0);
    }
    if (acc == atspi_conn->root_accessible) {
        for (AccessibleList *l=atspi_conn->toplevel_accessibles; l; l=l->next) cnt++;
    } else if (acc->tkwin && !acc->is_virtual) {
        EnsureChildrenRegistered(acc->tkwin, 0);

        /* Orca queries ChildCount before GetChildren - ensure children are registered first. */
        EnsureChildrenRegistered(acc->tkwin, 0);
        for (TkWindow *c = ((TkWindow*)acc->tkwin)->childList; c; c = c->nextPtr) {
            if (GetAccessible((Tk_Window)c)) cnt++;
        }
    } else if (acc->children) {
        for (AccessibleList *l = acc->children; l; l=l->next) if (l->acc) cnt++;
    }
    return sd_bus_message_append(reply, "i", cnt);
}

/*
 *----------------------------------------------------------------------
 *
 * dbus_method_grab_focus --
 *
 *     D-Bus method handler for GrabFocus on the Accessible interface.
 *
 *     Requests Tk focus and immediately synchronizes the accessibility
 *     focus chain.  The latter is necessary on the Wayland port because
 *     a logical Tk focus change is not guaranteed to generate an X
 *     FocusIn event.
 *
 * Results:
 *
 *     Returns TRUE on success, FALSE on failure.
 *
 * Side effects:
 *
 *     Gives Tk focus to the widget and updates AT-SPI accessibility
 *     focus.
 *
 *----------------------------------------------------------------------
 */

static int
dbus_method_grab_focus(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;

    if (!acc || !acc->tkwin || !acc->interp) {
        return sd_bus_reply_method_return(m, "b", 0);
    }

    DEBUG_LOG("dbus_method_grab_focus: path=%s",
              acc->path ? acc->path : "?");

    /*
     * First give Tk logical focus to the widget.
     */
    char cmd[256];

    snprintf(cmd, sizeof(cmd),
             "focus -force %s",
             Tk_PathName(acc->tkwin));

    int rc = Tcl_Eval(acc->interp, cmd);

    if (rc != TCL_OK) {
        Tcl_ResetResult(acc->interp);
        return sd_bus_reply_method_return(m, "b", 0);
    }

    /*
     * Do not depend on FocusIn being generated by the Wayland/X
     * compatibility layer.  Explicitly synchronize the accessibility
     * focus state.
     */
    UpdateFocusChain(acc->tkwin);

    return sd_bus_reply_method_return(m, "b", 1);
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

static int
dbus_method_get_index_in_parent(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int index = -1;

    if (!acc || !atspi_conn) {
        return sd_bus_reply_method_return(m, "i", -1);
    }

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
        /* Real Tk child: compute accessible-filtered index from parent's child list.
         * Must match ChildCount/GetChildren which count only GetAccessible() children,
         * otherwise Atspi clients (Orca/Accerciser) see index out of range and hang.
         */
        TkWindow *childPtr;
        int acc_idx = 0;
        for (childPtr = ((TkWindow*)acc->parent->tkwin)->childList;
             childPtr != NULL;
             childPtr = childPtr->nextPtr) {
            TkAccessible *sib = GetAccessible((Tk_Window)childPtr);
            if (!sib) continue;
            if ((Tk_Window)childPtr == acc->tkwin) {
                index = acc_idx;
                break;
            }
            acc_idx++;
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

static int
dbus_method_get_interfaces(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    sd_bus_message *reply = NULL;
    int r;

    if (!acc || !atspi_conn) {
        return sd_bus_reply_method_return(m, "as", 0);
    }

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    sd_bus_message_open_container(reply, 'a', "s");
    sd_bus_message_append(reply, "s", ATSPI_ACCESSIBLE_INTERFACE);
    sd_bus_message_append(reply, "s", ATSPI_COMPONENT_INTERFACE);
    if (acc == atspi_conn->root_accessible) {
        sd_bus_message_append(reply, "s", "org.a11y.atspi.Application");
    }

    int role = GetLiveRole(acc);
    if (role == ATSPI_ROLE_PUSH_BUTTON || role == ATSPI_ROLE_CHECK_BOX ||
        role == ATSPI_ROLE_RADIO_BUTTON || role == ATSPI_ROLE_TOGGLE_BUTTON) {
        sd_bus_message_append(reply, "s", ATSPI_ACTION_INTERFACE);
    }
    if (role == ATSPI_ROLE_SPIN_BUTTON || role == ATSPI_ROLE_SLIDER ||
        role == ATSPI_ROLE_PROGRESS_BAR || role == ATSPI_ROLE_SCROLL_BAR) {
        sd_bus_message_append(reply, "s", ATSPI_VALUE_INTERFACE);
    }
    /*
     * Text and Selection interfaces are intentionally omitted
     * as they are addressed at the script level.
     */
    sd_bus_message_close_container(reply);
    return sd_bus_send(NULL, reply, NULL);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_application--
 *
 *   D-Bus method handler for an application reference.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an application reference.
 *----------------------------------------------------------------------
 */

static int
dbus_method_get_application(
    sd_bus_message *m,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    int r;
    sd_bus_message *reply = NULL;
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;
    if (atspi_conn && atspi_conn->root_accessible && atspi_conn->root_accessible->dbus_path) {
        AppendAccessibleRef(reply, atspi_conn->root_accessible->dbus_path);
    } else {
        AppendAccessibleRef(reply, NULL);
    }
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

static int
dbus_method_component_get_extents(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t coord_type;
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_read(m, "i", &coord_type);
    if (r < 0) return r;

    if (!acc || !acc->tkwin) {
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

static int
dbus_method_component_get_position(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t coord_type;
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_read(m, "i", &coord_type);
    if (r < 0) return r;

    if (!acc || !acc->tkwin) {
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

static int
dbus_method_component_get_size(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;

    if (!acc || !acc->tkwin) {
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

static int
dbus_method_component_contains(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t x, y, coord_type;
    int r;

    r = sd_bus_message_read(m, "iii", &x, &y, &coord_type);
    if (r < 0) return r;

    if (!acc || !acc->tkwin) {
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
 * dbus_method_component_get_accessible_at_point --
 *
 *   D-Bus method handler for GetAccessibleAtPoint on the Component interface.
 *   Finds which UI component (widget) is located at a specific screen position.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a boolean value.
 *----------------------------------------------------------------------
 */

static int
dbus_method_component_get_accessible_at_point(
    sd_bus_message *m,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t x, y, coord_type;
    sd_bus_message *reply = NULL;
    int r;
    r = sd_bus_message_read(m, "iii", &x, &y, &coord_type);
    if (r < 0) return r;
    TkAccessible *hit = NULL;
    if (atspi_conn && acc == atspi_conn->root_accessible) {
        for (AccessibleList *l = atspi_conn->toplevel_accessibles; l; l = l->next) {
            TkAccessible *top = l->acc;
            if (!top || !top->tkwin) continue;
            int cx, cy;
            Tk_GetRootCoords(top->tkwin, &cx, &cy);
            int cw = Tk_Width(top->tkwin);
            int ch = Tk_Height(top->tkwin);
            if (x >= cx && x < cx + cw && y >= cy && y < cy + ch) { hit = top; break; }
        }
    } else if (acc && acc->tkwin && !acc->is_virtual) {
        TkWindow *childPtr;
        for (childPtr = ((TkWindow*)acc->tkwin)->childList; childPtr; childPtr = childPtr->nextPtr) {
            TkAccessible *child_acc = GetAccessible((Tk_Window)childPtr);
            if (!child_acc || !child_acc->tkwin) continue;
            int cx, cy;
            Tk_GetRootCoords((Tk_Window)childPtr, &cx, &cy);
            int cw = Tk_Width((Tk_Window)childPtr);
            int ch = Tk_Height((Tk_Window)childPtr);
            if (x >= cx && x < cx + cw && y >= cy && y < cy + ch) { hit = child_acc; break; }
        }
    }
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;
    if (hit && hit->dbus_path) AppendAccessibleRef(reply, hit->dbus_path);
    else if (acc && acc->dbus_path) AppendAccessibleRef(reply, acc->dbus_path);
    else AppendAccessibleRef(reply, NULL);
    return sd_bus_send(NULL, reply, NULL);
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

static int
dbus_method_component_grab_focus(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    sd_bus_error *ret_error)/* Error object. */
{
    return dbus_method_grab_focus(m, userdata, ret_error);
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

static int
dbus_method_action_get_n_actions(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int n_actions = 0;

    if (acc && acc->tkwin) {
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

static int
dbus_method_action_do_action(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t index;
    int r;

    r = sd_bus_message_read(m, "i", &index);
    if (r < 0) return r;

    if (!acc || !acc->tkwin || !acc->interp || index != 0) {
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
        SendStateChanged(acc, ATSPI_STATE_CHECKED, 1);
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

static int
dbus_method_action_get_name(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t index;
    int r;
    const char *action_name = NULL;

    r = sd_bus_message_read(m, "i", &index);
    if (r < 0) return r;

    if (acc && index == 0 && acc->tkwin) {
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

static int
dbus_method_action_get_description(
    sd_bus_message *m,      /* D-Bus method call message. */
    TCL_UNUSED(void *),     /* userdata */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
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

static int
dbus_method_action_get_key_binding(
    sd_bus_message *m,      /* D-Bus method call message. */
    TCL_UNUSED(void *),     /* userdata */
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

static int
dbus_method_value_get_current(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;

    if (!acc || !acc->tkwin) {
        return sd_bus_reply_method_return(m, "d", 0.0);
    }

    char *val_str = GetValueForWidget(acc->tkwin);
    double value = val_str ? atof(val_str) : 0.0;
    if (val_str) free(val_str); val_str=NULL;
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

static int
dbus_method_value_get_minimum(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    double min_val = 0.0;
    char cmd[256];

    if (acc && acc->tkwin && acc->interp) {
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

static int
dbus_method_value_get_maximum(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    double max_val = 100.0;
    char cmd[256];

    if (acc && acc->tkwin && acc->interp) {
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

static int
dbus_method_value_set_current(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    double value;
    int r;

    r = sd_bus_message_read(m, "d", &value);
    if (r < 0) return r;

    if (!acc || !acc->tkwin || !acc->interp) {
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
 * AppendCacheItemLive --
 *
 *   Recursively appends cache item data for an accessible object and its
 *   children to a D-Bus reply message. This function retrieves live
 *   information about the accessible object including its name, description,
 *   role, states, and child count. It handles special cases for the root
 *   accessible, virtual objects, and Tk windows.
 *
 * Results:
 *   None. Modifies the reply message in place.
 *
 * Side effects:
 *   Allocates and frees memory for live name and description strings.
 *   Recursively calls itself to process child accessible objects.
 *----------------------------------------------------------------------
 */

static void
AppendCacheItemLive(
    sd_bus_message *reply,
    TkAccessible *acc,
    TkAccessible *parent_acc,
    const char *app_path,
    int index_in_parent)
{
    if (!acc || !acc->dbus_path) return;
    if (parent_acc && parent_acc==acc) parent_acc=NULL;
    if (parent_acc && parent_acc->dbus_path && acc->dbus_path && strcmp(parent_acc->dbus_path, acc->dbus_path)==0) parent_acc=NULL;
    
    int childcnt=0;
    if (atspi_conn && acc == atspi_conn->root_accessible) {
        /* Root's children are the registered toplevels, not acc->children. */
        for (AccessibleList *l=atspi_conn->toplevel_accessibles; l; l=l->next) {
            if (l->acc) childcnt++;
        }
    } else if (acc->tkwin && !acc->is_virtual) {
        EnsureChildrenRegistered(acc->tkwin, 0);
        for (TkWindow *c=((TkWindow*)acc->tkwin)->childList; c; c=c->nextPtr) {
            if (GetAccessible((Tk_Window)c)) childcnt++;
        }
    } else if (acc->children){
        for(AccessibleList *l=acc->children;l;l=l->next) if(l->acc) childcnt++;
    }

    sd_bus_message_open_container(reply, 'r', "(so)(so)(so)iiassusau");
    AppendAccessibleRef(reply, acc->dbus_path);
    AppendAccessibleRef(reply, app_path);
    /* Accerciser warns if accessible has itself as parent.
     * Root's parent must be null, not itself (app_path == root path).
     * For any other object, if parent_acc is NULL or self, fall back to app_path (root). */
    int is_root = (atspi_conn && acc == atspi_conn->root_accessible) ||
                  (acc->dbus_path && app_path && strcmp(acc->dbus_path, app_path)==0);
    if (is_root) {
        AppendAccessibleRef(reply, "/org/a11y/atspi/null");
    } else if (parent_acc && parent_acc->dbus_path && parent_acc != acc &&
               !(parent_acc->dbus_path && acc->dbus_path && strcmp(parent_acc->dbus_path, acc->dbus_path)==0)) {
        AppendAccessibleRef(reply, parent_acc->dbus_path);
    } else {
        /* Non-root with no valid parent -> parent is the application root. */
        AppendAccessibleRef(reply, app_path);
    }
    sd_bus_message_append(reply, "i", index_in_parent);
    sd_bus_message_append(reply, "i", childcnt);
    sd_bus_message_open_container(reply, 'a', "s");
    sd_bus_message_append(reply, "s", ATSPI_ACCESSIBLE_INTERFACE);
    sd_bus_message_append(reply, "s", ATSPI_COMPONENT_INTERFACE);
    int live_role = GetLiveRole(acc);
    if (live_role==ATSPI_ROLE_PUSH_BUTTON||live_role==ATSPI_ROLE_CHECK_BOX||live_role==ATSPI_ROLE_RADIO_BUTTON||live_role==ATSPI_ROLE_TOGGLE_BUTTON)
        sd_bus_message_append(reply, "s", ATSPI_ACTION_INTERFACE);
    else if (live_role==ATSPI_ROLE_SPIN_BUTTON||live_role==ATSPI_ROLE_SLIDER||live_role==ATSPI_ROLE_PROGRESS_BAR||live_role==ATSPI_ROLE_SCROLL_BAR)
        sd_bus_message_append(reply, "s", ATSPI_VALUE_INTERFACE);
    sd_bus_message_close_container(reply);

    /* LIVE reads for name/description - no cached._* */
    char *live_name = NULL;
    char *live_desc = NULL;
    if (acc->is_virtual && acc->virtual_name) {
        live_name = NULL; /* Use virtual_name directly. */
    } else if (acc->tkwin) {
        live_name = GetNameForWidget(acc->tkwin);
        live_desc = GetDescriptionForWidget(acc->tkwin);
    }
    
    const char *nm_raw;
    if (acc->is_virtual && acc->virtual_name) nm_raw = acc->virtual_name;
    else if (live_name && live_name[0]) nm_raw = live_name;
    else if (acc->path) {
        if (acc->path[0]=='.' && acc->path[1]=='\0') nm_raw = "Tk Application";
        else if (strcmp(acc->path, "application")==0) nm_raw = "Tk";
        else nm_raw = acc->path;
    } else nm_raw = "";
    
    const char *ds = live_desc ? live_desc : "";

    sd_bus_message_append(reply, "s", nm_raw);
    sd_bus_message_append(reply, "u", (uint32_t)live_role);
    sd_bus_message_append(reply, "s", ds);
    sd_bus_message_open_container(reply, 'a', "u");
    uint64_t st=acc->states;
    sd_bus_message_append(reply, "u", (uint32_t)(st&0xffffffffu));
    sd_bus_message_append(reply, "u", (uint32_t)((st>>32)&0xffffffffu));
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);

    if (live_name) free(live_name);
    if (live_desc) free(live_desc);

    if (atspi_conn && acc == atspi_conn->root_accessible) {
        /* Recurse into the registered toplevels, mirroring GetChildren. */
        int emit_idx=0;
        for (AccessibleList *l=atspi_conn->toplevel_accessibles; l; l=l->next, emit_idx++) {
            if (l->acc) AppendCacheItemLive(reply, l->acc, acc, app_path, emit_idx);
        }
    } else if (acc->tkwin && !acc->is_virtual) {
        int emit_idx=0;
        for (TkWindow *c=((TkWindow*)acc->tkwin)->childList; c; c=c->nextPtr) {
            TkAccessible *ch=GetAccessible((Tk_Window)c);
            if (ch){AppendCacheItemLive(reply,ch,acc,app_path,emit_idx);emit_idx++;}
        }
    } else if (acc->children){
        int idx=0;
        for(AccessibleList *l=acc->children;l;l=l->next,idx++) if(l->acc) AppendCacheItemLive(reply,l->acc,acc,app_path,idx);
    }
}

/*
 *----------------------------------------------------------------------
 * dbus_method_cache_get_items --
 *
 *   D-Bus method handler for GetItems on the Cache interface.
 *   Returns a complete cache of all accessible objects in the application
 *   hierarchy, starting from the root accessible object.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Creates and sends a D-Bus reply message containing an array of
 *   accessible object cache entries.
 *----------------------------------------------------------------------
 */

static int
dbus_method_cache_get_items(
    sd_bus_message *m,
    void *userdata,
    sd_bus_error *ret_error)
{
    sd_bus_message *reply = NULL;
    int r;
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;
    sd_bus_message_open_container(reply, 'a', "((so)(so)(so)iiassusau)");
    if (atspi_conn && atspi_conn->root_accessible) {
        const char *app_path = atspi_conn->root_accessible->dbus_path;
        AppendCacheItemLive(reply, atspi_conn->root_accessible, NULL, app_path, 0);
    }
    sd_bus_message_close_container(reply);
    return sd_bus_send(NULL, reply, NULL);
}


/* D-Bus vtable definition for the Cache interface. */
static const sd_bus_vtable cache_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetItems", "", "a((so)(so)(so)iiassusau)", dbus_method_cache_get_items, SD_BUS_VTABLE_UNPRIVILEGED),
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

static bool
RegisterDbusObject(
    TkAccessible *acc)      /* Accessible object to register. */
{
    if (!atspi_conn || !atspi_conn->bus || !acc) {
        DEBUG_LOG("RegisterDbusObject: bailing early (atspi_conn=%p, bus=%p, acc=%p)",
                  (void *)atspi_conn,
                  (void *)(atspi_conn ? atspi_conn->bus : NULL),
                  (void *)acc);
        return false;
    }

    /* Generate a unique object path if not already set. */
    if (!acc->dbus_path) {
        static int counter = 0;
        char path[256];
        if (acc->tkwin && Tk_IsTopLevel(acc->tkwin)) {
            const char *pn = Tk_PathName(acc->tkwin);
            /* "." toplevel would become empty - use "main". */
            if (!pn || pn[0] == '\0' || (pn[0] == '.' && pn[1] == '\0')) {
                snprintf(path, sizeof(path), "/org/a11y/atspi/accessible/toplevel%d", counter++);
            } else {
                snprintf(path, sizeof(path), "/org/a11y/atspi/accessible/%s", pn);
            }
        } else {
            snprintf(path, sizeof(path), "/org/a11y/atspi/accessible/obj%d", counter++);
        }
        /* Sanitize for D-Bus: replace any char not [A-Za-z0-9_/] with '_'. */
        for (char *p = path; *p; p++) {
            if (*p == '.') *p = '_';
            else if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '/' || *p == '_')) {
                *p = '_';
            }
        }
        /* Collapse "//" and ensure no trailing slash. */
        acc->dbus_path = strdup(path);
        DEBUG_LOG("RegisterDbusObject: generated dbus_path=%s for widget path=%s",
                  acc->dbus_path, acc->path ? acc->path : "?");
    }

    /*
     * Register a single vtable and capture its slot in acc->vtable_slots
     * so it can be torn down later. Every sd_bus_add_object_vtable() call
     * below MUST go through this helper.
     */
#define ADD_VTABLE(bus_, path_, iface_, vtable_) \
    do { \
        if (acc->n_vtable_slots < TK_ACCESSIBLE_MAX_SLOTS) { \
            sd_bus_slot *_slot = NULL; \
            int _r = sd_bus_add_object_vtable((bus_), &_slot, (path_), \
                                               (iface_), (vtable_), acc); \
            if (_r >= 0 && _slot) { \
                acc->vtable_slots[acc->n_vtable_slots++] = _slot; \
            } \
        } \
    } while (0)

    /* Register main Accessible interface. */
    sd_bus_slot *slot = NULL;
    int r = sd_bus_add_object_vtable(atspi_conn->bus,
                                      &slot,
                                      acc->dbus_path,
                                      ATSPI_ACCESSIBLE_INTERFACE,
                                      accessible_vtable,
                                      acc);
    if (r < 0) {
        DEBUG_LOG("RegisterDbusObject: sd_bus_add_object_vtable failed for %s, r=%d (%s)",
                  acc->dbus_path, r, strerror(-r));
        return false;
    }
    acc->vtable_slots[acc->n_vtable_slots++] = slot;

    /* Register Component interface (all objects support it). */
    ADD_VTABLE(atspi_conn->bus, acc->dbus_path,
               ATSPI_COMPONENT_INTERFACE, component_vtable);

    /*
     * Register Application on the root.
     * Application is required for Orca to catalog
     * us as an application.
     */
    if (acc == atspi_conn->root_accessible) {
    
        ADD_VTABLE(atspi_conn->bus, acc->dbus_path,
                   "org.a11y.atspi.Application", application_vtable);
    }

    /* Conditionally register Action/Value interfaces based on role.
     * Also called later (e.g. on focus) once more may be known about the
     * widget's role -- see EnsureRoleVtables. */
    EnsureRoleVtables(acc, /* notifyIfChanged = */ 0);
    /* Text and Selection interfaces are intentionally omitted. */

#undef ADD_VTABLE
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
EmitObjectEventFull(
    TkAccessible *acc,      /* Object emitting the event. */
    const char *member,     /* D-Bus member name. */
    const char *type,       /* Event type string. */
    int32_t detail1,        /* First detail integer. */
    int32_t detail2,        /* Second detail integer. */
    TkAccessible *related)  /* Related object, or NULL. */
{
    const char *bus_unique = NULL;
    if (atspi_conn && atspi_conn->bus) {
        sd_bus_get_unique_name(atspi_conn->bus, &bus_unique);
    }
    DEBUG_LOG("EmitObjectEventFull: path=%s member=%s type=%s detail1=%d bus=%s dbus_path=%s",
              acc && acc->path ? acc->path : "?", member ? member : "?",
              type ? type : "?", detail1,
              bus_unique ? bus_unique : "(no bus)",
              acc && acc->dbus_path ? acc->dbus_path : "?");
    if (!atspi_conn || !atspi_conn->bus) {
        DEBUG_LOG("EmitObjectEventFull: no bus - dropped");
        return;
    }
    if (!acc || !acc->dbus_path) return;
    if (!member || !type) return;

    /*
     * (so) = (bus-name, object-path).
     */
    const char *rel_name = "";
    const char *rel_path = "/org/a11y/atspi/null";
    if (related && related->dbus_path) {
        rel_name = SelfBusName();
        rel_path = related->dbus_path;
    }

    /*
     * AT-SPI Event.Object signature is siiva{sv}, where v = (so) and the
     * trailing a{sv} is a (normally empty) properties dict.
     */
    sd_bus_message *m = NULL;
    int r = sd_bus_message_new_signal(atspi_conn->bus, &m,
                                      acc->dbus_path,
                                      "org.a11y.atspi.Event.Object",
                                      member);
    if (r < 0) {
        fprintf(stderr, "EmitObjectEvent %s/%s new_signal failed: %d\n", member, type, r);
        return;
    }

    r = sd_bus_message_append(m, "sii", type, detail1, detail2);
    if (r >= 0) {
        r = sd_bus_message_open_container(m, 'v', "(so)");
    }
    if (r >= 0) {
        r = sd_bus_message_append(m, "(so)", rel_name, rel_path);
    }
    if (r >= 0) {
        r = sd_bus_message_close_container(m); /* variant */
    }
    if (r >= 0) {
        r = sd_bus_message_open_container(m, 'a', "{sv}");
    }
    if (r >= 0) {
        r = sd_bus_message_close_container(m); /* empty a{sv} */
    }
    if (r >= 0) {
        r = sd_bus_send(atspi_conn->bus, m, NULL);
    }
    if (r < 0) {
        /* Don't crash, just debug. */
        fprintf(stderr, "EmitObjectEvent %s/%s failed: %d\n", member, type, r);
    } 
    sd_bus_message_unref(m);
}

/*
 *----------------------------------------------------------------------
 * EmitFocusEvent --
 *
 *   Emit a Focus event on the correct interface org.a11y.atspi.Event.Focus
 *   per AT-SPI2 spec. Orca subscribes to Event.Focus, not Event.Object.Focus.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Sends a D-Bus signal with focus event details.
 *----------------------------------------------------------------------
 */

static void
EmitFocusEvent(
    TkAccessible *acc)      /* Object gaining focus. */
{
    if (!atspi_conn || !atspi_conn->bus) return;
    if (!acc || !acc->dbus_path) return;

    DEBUG_LOG("EmitFocusEvent: path=%s dbus_path=%s", acc->path ? acc->path : "?", acc->dbus_path);

    /* AT-SPI Focus event signature is same as Object: siiva{sv}. */
    sd_bus_message *m = NULL;
    int r = sd_bus_message_new_signal(atspi_conn->bus, &m,
                                      acc->dbus_path,
                                      "org.a11y.atspi.Event.Focus",
                                      "Focus");
    if (r < 0) {
        fprintf(stderr, "EmitFocusEvent new_signal failed: %d\n", r);
        return;
    }

    r = sd_bus_message_append(m, "sii", "", 0, 0);
    if (r >= 0) {
        r = sd_bus_message_open_container(m, 'v', "(so)");
    }
    if (r >= 0) {
        r = sd_bus_message_append(m, "(so)", "", "/org/a11y/atspi/null");
    }
    if (r >= 0) {
        r = sd_bus_message_close_container(m);
    }
    if (r >= 0) {
        r = sd_bus_message_open_container(m, 'a', "{sv}");
    }
    if (r >= 0) {
        r = sd_bus_message_close_container(m);
    }
    if (r >= 0) {
        r = sd_bus_send(atspi_conn->bus, m, NULL);
    }
    if (r < 0) {
        fprintf(stderr, "EmitFocusEvent failed: %d\n", r);
    }
    sd_bus_message_unref(m);
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
EmitWindowEvent(
    TkAccessible *acc,      /* Window emitting the event. */
    const char *member,     /* D-Bus member name. */
    const char *type)       /* Event type string. */
{
    DEBUG_LOG("EmitWindowEvent: enter");
    if (!atspi_conn || !atspi_conn->bus) return;
    if (!acc || !acc->dbus_path) return;
    if (!member || !type) return;

    /* Same siiva{sv} shape as EmitObjectEventFull.  */
    sd_bus_message *m = NULL;
    int r = sd_bus_message_new_signal(atspi_conn->bus, &m,
                                      acc->dbus_path,
                                      "org.a11y.atspi.Event.Window",
                                      member);
    if (r < 0) return;

    r = sd_bus_message_append(m, "sii", type, 0, 0);
    if (r >= 0) {
        r = sd_bus_message_open_container(m, 'v', "(so)");
    }
    if (r >= 0) {
        r = sd_bus_message_append(m, "(so)", "", "/org/a11y/atspi/null");
    }
    if (r >= 0) {
        r = sd_bus_message_close_container(m); /* variant */
    }
    if (r >= 0) {
        r = sd_bus_message_open_container(m, 'a', "{sv}");
    }
    if (r >= 0) {
        r = sd_bus_message_close_container(m); /* empty a{sv} */
    }
    if (r >= 0) {
        r = sd_bus_send(atspi_conn->bus, m, NULL);
    }
    sd_bus_message_unref(m);
}

/*
 *----------------------------------------------------------------------
 * PostAccessibilityAnnouncement --
 *
 *   Post an accessibility announcement via AT-SPI's Announcement signal..
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Sends a D-Bus signal "Announcement" on the Event.Object interface.
 *----------------------------------------------------------------------
 */

static void
PostAccessibilityAnnouncement(
    TkAccessible *acc,
    const char *message,
    int priority)           /* 0 = low, 1 = medium, 2 = high (Orca uses 0). */
{
    if (!atspi_conn || !atspi_conn->bus || !acc || !acc->dbus_path || !message) {
        return;
    }
    /* Emit as an Object:PropertyChange with 'accessible-value' as the type,
     * and pass the announcement text in the detail1 field (string variant). */
    EmitObjectEventFull(acc, "PropertyChange", "accessible-value",
                        (int32_t)priority, 0, NULL);
    /* Additionally, some AT-SPI clients expect a dedicated "Announcement"
     * signal with the text in the variant. We'll emit both to be safe. */
    EmitObjectEventFull(acc, "Announcement", message,
                        (int32_t)priority, 0, NULL);
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

static void
SendAtspiEvent(
    TkAccessible *acc,      /* Object emitting the event. */
    const char *event_type, /* Event type string. */
    const char *detail)     /* Optional detail string. */
{
    if (!atspi_conn || !atspi_conn->bus || !acc || !acc->dbus_path) {
        return;
    }

    /*
     * event_type comes from ATSPI_EVENT_* constants like "focus",
     * "value-changed", "window:activate", etc.
     */
    if (strcmp(event_type, ATSPI_EVENT_FOCUS) == 0) {
        EmitFocusEvent(acc);
    } else if (strcmp(event_type, ATSPI_EVENT_VALUE_CHANGED) == 0) {
        EmitObjectEventFull(acc, "PropertyChange", "accessible-value", 0, 0, NULL);
    } else if (strcmp(event_type, ATSPI_EVENT_WINDOW_ACTIVATE) == 0) {
        EmitWindowEvent(acc, "Activate", "");
    } else if (strcmp(event_type, ATSPI_EVENT_WINDOW_DEACTIVATE) == 0) {
        EmitWindowEvent(acc, "Deactivate", "");
    } else if (strcmp(event_type, ATSPI_EVENT_WINDOW_CREATE) == 0) {
        EmitWindowEvent(acc, "Create", "");
    } else if (strncmp(event_type, "object:", 7) == 0) {
        /*
         * event_type is a fully-qualified libatspi listener string like
         * "object:state-changed:focused" -- that convention is for
         * *registering* listeners, not for the wire. Strip it down to
         * the bare trailing detail (text after the last ':'), which is
         * what the "s" field of siiv(a{sv}) actually expects.
         */
        const char *last_colon = strrchr(event_type, ':');
        const char *bare = last_colon ? last_colon + 1 : "";
        EmitObjectEventFull(acc, detail ? detail : "StateChanged", bare, 0, 0, NULL);
    } else {
        /* Fallback: treat event_type as member, detail (if any) as the bare type. */
        EmitObjectEventFull(acc, "StateChanged", detail ? detail : "", 0, 0, NULL);
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

static void
SendChildrenChanged(
    TkAccessible *parent,   /* Parent object. */
    int index,              /* Child index. */
    TkAccessible *child,    /* Child object. */
    int added)              /* 1 if child was added, 0 if removed. */
{
    if (!parent || !child) return;
    if (!parent->dbus_path || !child->dbus_path) return;
    if (!atspi_conn || !atspi_conn->bus) return;

    const char *type = added ? "add" : "remove";
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

static const char *
StateBitToName(
    uint64_t bit)           /* State bit flag. */
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

static void
SendStateChanged(
    TkAccessible *acc,      /* Object whose state changed. */
    uint64_t state,         /* State bit flag. */
    int value)              /* New state value (0 or 1). */
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

    EmitObjectEventFull(acc, "StateChanged", name, (int32_t)(value ? 1 : 0), 0, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * SetAccessibleFocus --
 *
 *     Set or clear the accessibility focus state for an object.
 *
 *     This function is the single point at which an accessible object's
 *     FOCUSED state is changed.  A Focus event is emitted only when the
 *     state actually changes.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Updates is_focused and states and emits the corresponding
 *     StateChanged and Focus events.
 *
 *----------------------------------------------------------------------
 */

static void
SetAccessibleFocus(
    TkAccessible *acc,      /* Accessible object. */
    int focused)            /* New focus state (0 or 1). */
{
    if (!acc || !acc->tkwin) {
        return;
    }

    focused = focused ? 1 : 0;

    uint64_t old_states = acc->states;
    int was_focused = acc->is_focused;

    DEBUG_LOG("SetAccessibleFocus: path=%s focused=%d was=%d",
              acc->path ? acc->path : "?",
              focused, was_focused);

    /*
     * is_focused is our authoritative logical focus state.
     * ComputeStateForWidget() derives ATSPI_STATE_FOCUSED from it.
     */
    if (was_focused != focused) {
        acc->is_focused = focused;
        acc->states = ComputeStateForWidget(acc);
    }

    /*
     * Orca needs both state-changed and focus every time focus is
     * (re-)asserted.
     */
    if (focused) {
        if ((old_states & ATSPI_STATE_FOCUSED) == 0 ||
            (acc->states & ATSPI_STATE_FOCUSED)) {
            if ((old_states & ATSPI_STATE_FOCUSED) == 0) {
                SendStateChanged(acc, ATSPI_STATE_FOCUSED, 1);
            }
        }
        /* Always re-emit Focus for Orca to re-announce */
        SendAtspiEvent(acc, ATSPI_EVENT_FOCUS, NULL);
    } else {
        if (old_states & ATSPI_STATE_FOCUSED) {
            SendStateChanged(acc, ATSPI_STATE_FOCUSED, 0);
        }
    }
}

/*
 *----------------------------------------------------------------------
 * TkAccessible_Reconcile --
 *
 *   Synchronizes and updates the accessibility state of a Tk widget 
 *   with its current actual state.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Updates the object's state.
 *----------------------------------------------------------------------
 */

static void
TkAccessible_Reconcile(
    TkAccessible *acc)
{
    if (!acc) return;
    if (!acc->tkwin) {
        if (atspi_conn && acc == atspi_conn->root_accessible) {
            acc->states = ATSPI_STATE_ENABLED | ATSPI_STATE_SENSITIVE | ATSPI_STATE_SHOWING | ATSPI_STATE_VISIBLE;
        }
        return;
    }

    int live_role = GetLiveRole(acc);
    if (live_role != ATSPI_ROLE_INVALID) {
        EnsureRoleVtables(acc, 0);
    }

    {
        char *newName = GetNameForWidget(acc->tkwin);
        int changed = (newName == NULL) != (acc->cached_name == NULL) ||
                      (newName && acc->cached_name && strcmp(newName, acc->cached_name) != 0);
        if (changed) {
            DEBUG_LOG("Reconcile: path=%s name changed '%s' -> '%s'",
                      acc->path ? acc->path : "?",
                      acc->cached_name ? acc->cached_name : "(null)",
                      newName ? newName : "(null)");
            if (acc->cached_name) free(acc->cached_name);
            acc->cached_name = newName;
            EmitObjectEventFull(acc, "PropertyChange", "accessible-name", 0, 0, NULL);
        } else if (newName) {
            free(newName);
        }
    }
    {
        char *newDesc = GetDescriptionForWidget(acc->tkwin);
        if (newDesc) {
            DEBUG_LOG("Reconcile: path=%s live desc='%s'", acc->path ? acc->path : "?", newDesc);
            free(newDesc);
        }
    }

    if (Tk_IsMapped(acc->tkwin)) {
        Tk_GetRootCoords(acc->tkwin, &acc->x, &acc->y);
        acc->width = Tk_Width(acc->tkwin);
        acc->height = Tk_Height(acc->tkwin);
    }

    uint64_t old_states = acc->states;
    uint64_t new_states = ComputeStateForWidget(acc);
    acc->states = new_states;
    if (old_states != new_states) {
        uint64_t changed = old_states ^ new_states;
        for (int bit = 0; bit < 64; bit++) {
            if (changed & (1ULL << bit)) {
                int now = (new_states & (1ULL << bit)) ? 1 : 0;
                if (bit == 12) continue;
                SendStateChanged(acc, (1ULL << bit), now);
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 * TkWaylandAtspiProcessEvents --
 *
 *   Drain pending AT-SPI D-Bus messages on atspi_bus. Called from the
 *   Wayland notifier's shared check proc (TkWaylandCheckProc in
 *   tkWaylandNotify.c) on every event-loop pass, exactly the way
 *   ibus_bus is drained in tkWaylandKey.c.
 *
 *   Guarded against re-entrancy.
 *   Calling sd_bus_process again on the same bus while already inside a
 *   drain is not safe, so a nested call here is a no-op; the next
 *   scheduled check-proc pass will pick up anything left pending.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Processes pending D-Bus messages; may invoke AT-SPI method handlers
 *   (GrabFocus, GetChildren, etc.) dispatched by Orca or the registry.
 *----------------------------------------------------------------------
 */

void
TkWaylandAtspiProcessEvents(void)
{
    if (!atspi_bus || atspi_draining) {
        return;
    }

    atspi_draining = 1;
    while (sd_bus_process(atspi_bus, NULL) > 0) {
        /* Drain all pending message.s */
    }
    atspi_draining = 0;
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

static TkAccessible *
CreateAccessible(
    Tcl_Interp *interp,     /* Tcl interpreter for the widget. */
    Tk_Window tkwin,        /* Tk window to create accessible for. */
    const char *path)       /* Path name of the widget. */
{
    DEBUG_LOG("CreateAccessible: enter path=%s", path ? path : (tkwin ? Tk_PathName(tkwin) : "?"));

    if (!interp || !tkwin) {
        DEBUG_LOG("CreateAccessible: bailing, interp=%p tkwin=%p", (void *)interp, (void *)tkwin);
        return NULL;
    }

    TkAccessible *acc = (TkAccessible *)Tcl_Alloc(sizeof(TkAccessible));
    if (!acc) return NULL;
    memset(acc, 0, sizeof(TkAccessible));

    acc->interp = interp;
    acc->tkwin = tkwin;
    acc->path = strdup(path ? path : Tk_PathName(tkwin));
    acc->ref_count = 1;
    acc->states = ComputeStateForWidget(acc);

    {
        char *ln = GetNameForWidget(tkwin);
        char *ld = GetDescriptionForWidget(tkwin);
        DEBUG_LOG("CreateAccessible: path=%s role=%d (%s) class=%s name='%s' desc='%s' (live read)",
                  acc->path ? acc->path : "?",
                  GetLiveRole(acc), RoleToString(GetLiveRole(acc)),
                  tkwin ? Tk_Class(tkwin) : "?",
                  ln ? ln : "(null)",
                  ld ? ld : "(null)");
        acc->cached_name = ln;  /* Seed the cache; ownership transfers here. */
        if (ld) free(ld);
    }

    if (!RegisterDbusObject(acc)) {
        DEBUG_LOG("CreateAccessible: RegisterDbusObject failed for %s, aborting creation", acc->path);
        if (acc->path)
                free(acc->path); 
                acc->path=NULL;
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

static void
FreeAccessible(
    TkAccessible *acc)      /* Accessible object to free. */
{
    if (!acc) return;

    if (acc->ref_count > 1) {
        acc->ref_count--;
        return;
    }

    /* Unregister D-Bus object - remove all vtables from the bus. */
    for (int i = 0; i < acc->n_vtable_slots; i++) {
        if (acc->vtable_slots[i]) {
            sd_bus_slot_unref(acc->vtable_slots[i]);
            acc->vtable_slots[i] = NULL;
        }
    }
    acc->n_vtable_slots = 0;

    /* Remove from the accessible map before freeing. */
    if (acc->tkwin && atspi_conn && atspi_conn->tk_to_accessible_map) {
        Tcl_HashEntry *entry = Tcl_FindHashEntry(atspi_conn->tk_to_accessible_map, (char *)acc->tkwin);
        if (entry && (TkAccessible *)Tcl_GetHashValue(entry) == acc) {
            Tcl_DeleteHashEntry(entry);
        }
    }

    if (acc->path) 
        free(acc->path);
        acc->path=NULL;
    if (acc->dbus_path) 
        free(acc->dbus_path);
        acc->dbus_path=NULL;
    if (acc->virtual_name) 
        free(acc->virtual_name);
        acc->virtual_name=NULL;
    if (acc->cached_name)
        free(acc->cached_name);
        acc->cached_name=NULL;
        
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

static void
RegisterAccessible(
    Tk_Window tkwin,        /* Tk window associated with accessible. */
    TkAccessible *acc)      /* Accessible object to register. */
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
GetAccessible(
    Tk_Window tkwin)        /* Tk window to look up. */
{
    if (!atspi_conn) {
        return NULL;
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

static void
UnregisterAccessible(
    Tk_Window tkwin)        /* Tk window to unregister. */
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
 *   Adds the toplevel to the list, emits ChildrenChanged event to the
 *   application root, and sends a window-create event.
 *----------------------------------------------------------------------
 */

static void
RegisterToplevel(
    TkAccessible *acc)      /* Toplevel accessible to register. */
{
    if (!acc) return;

    /* Check if already registered. */
    AccessibleList *l = atspi_conn->toplevel_accessibles;
    while (l) {
        if (l->acc == acc) return;
        l = l->next;
    }

    /* Add to list. */
    AccessibleList *node = (AccessibleList *)Tcl_Alloc(sizeof(AccessibleList));
    node->acc = acc;
    node->next = atspi_conn->toplevel_accessibles;
    atspi_conn->toplevel_accessibles = node;

    /* Compute index in parent (application root). */
    int idx = 0;
    for (l = atspi_conn->toplevel_accessibles; l && l->acc != acc; l = l->next) {
        idx++;
    }

    /* Set parent to root application. */
    if (atspi_conn->root_accessible) {
        acc->parent = atspi_conn->root_accessible;
        /* Announce to the application root. */
        SendChildrenChanged(atspi_conn->root_accessible, idx, acc, 1);
    }

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
 *   Removes the toplevel from the list and emits ChildrenChanged event.
 *----------------------------------------------------------------------
 */

static void
UnregisterToplevel(
    TkAccessible *acc)      /* Toplevel accessible to unregister. */
{
    if (!acc || !atspi_conn) return;

    /* Compute index before removal. */
    int idx = 0;
    AccessibleList *l = atspi_conn->toplevel_accessibles;
    while (l && l->acc != acc) {
        l = l->next;
        idx++;
    }

    if (l) {
        /* Announce removal to parent. */
        if (acc->parent) {
            SendChildrenChanged(acc->parent, idx, acc, 0);
        } else if (atspi_conn->root_accessible) {
            SendChildrenChanged(atspi_conn->root_accessible, idx, acc, 0);
        }
    }

    /* Remove from list. */
    AccessibleList *prev = NULL;
    l = atspi_conn->toplevel_accessibles;
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
 *   Creates TkAccessible objects for all widgets in the hierarchy and
 *   emits ChildrenChanged events.
 *----------------------------------------------------------------------
 */

static void
RegisterWidgetRecursive(
    Tcl_Interp *interp,     /* Tcl interpreter. */
    Tk_Window tkwin)        /* Root window to start from. */
{
    if (!tkwin) return;

    TkAccessible *acc = GetAccessible(tkwin);
    if (!acc) {
        acc = CreateAccessible(interp, tkwin, Tk_PathName(tkwin));
        if (!acc) {
            DEBUG_LOG("RegisterWidgetRecursive: CreateAccessible failed for %s", Tk_PathName(tkwin));
            return;
        }

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

        /* Emit ChildrenChanged to parent. */
        if (acc->parent) {
            /* Compute accessible-filtered index in parent. */
            int idx = 0;
            if (acc->parent->tkwin) {
                TkWindow *childPtr;
                int acc_idx = 0;
                for (childPtr = ((TkWindow*)acc->parent->tkwin)->childList;
                     childPtr != NULL;
                     childPtr = childPtr->nextPtr) {
                    TkAccessible *sib = GetAccessible((Tk_Window)childPtr);
                    if (!sib) continue;
                    if ((Tk_Window)childPtr == tkwin) {
                        idx = acc_idx;
                        break;
                    }
                    acc_idx++;
                }
            }
            SendChildrenChanged(acc->parent, idx, acc, 1);
        }
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

static void
EnsureAccessibleInHierarchy(
    Tcl_Interp *interp,     /* Tcl interpreter. */
    Tk_Window tkwin)        /* Window to ensure accessibility for. */
{
    if (!tkwin) return;

    /* Ensure all ancestors exist. */
    Tk_Window current = tkwin;
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
                
                /* Emit ChildrenChanged to parent. */
                if (acc->parent) {
                    int idx = 0;
                    if (acc->parent->tkwin) {
                        TkWindow *childPtr;
                        int acc_idx = 0;
                        for (childPtr = ((TkWindow*)acc->parent->tkwin)->childList;
                             childPtr != NULL;
                             childPtr = childPtr->nextPtr) {
                            TkAccessible *sib = GetAccessible((Tk_Window)childPtr);
                            if (!sib) continue;
                            if ((Tk_Window)childPtr == win) {
                                idx = acc_idx;
                                break;
                            }
                            acc_idx++;
                        }
                    }
                    SendChildrenChanged(acc->parent, idx, acc, 1);
                }
            } else {
                DEBUG_LOG("EnsureAccessibleInHierarchy: CreateAccessible failed for %s", Tk_PathName(win));
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateFocusChain --
 *
 *     Make the specified Tk window the single accessibility-focused
 *     object.
 *
 *     Tk's logical focus can change without producing an X FocusIn
 *     event on the widget.  Therefore this function must be callable
 *     directly from Tk's logical focus/activation paths.
 *
 * Results:
 *
 *     None.
 *
 * Side effects:
 *
 *     Clears accessibility focus from any previously focused object and
 *     emits the AT-SPI focus transition to the new object.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateFocusChain(
    Tk_Window focused)      /* Window that now has logical focus. */
{
    if (!focused || !atspi_conn || !atspi_conn->tk_to_accessible_map) {
        return;
    }

    Tcl_Interp *interp = Tk_Interp(focused);
    if (!interp) {
        return;
    }

    /*
     * Make absolutely sure the target exists in the accessibility tree.
     */
    EnsureAccessibleInHierarchy(interp, focused);

    TkAccessible *focused_acc = GetAccessible(focused);
    if (!focused_acc) {
        DEBUG_LOG("UpdateFocusChain: no accessible for %s",
                  Tk_PathName(focused));
        return;
    }

    /*
     * Reconcile the target before announcing it.  This makes sure Orca
     * sees the current name, role, state and geometry when it receives
     * the Focus event.
     */
    TkAccessible_Reconcile(focused_acc);
    EnsureRoleVtables(focused_acc, 1);

    /*
     * There must be exactly one logically focused accessible object.
     *
     * Walk the complete Tk->accessible table and clear FOCUSED from
     * every other object.
     */
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    for (hPtr = Tcl_FirstHashEntry(
             atspi_conn->tk_to_accessible_map, &search);
         hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&search)) {

        TkAccessible *acc =
            (TkAccessible *)Tcl_GetHashValue(hPtr);

        if (!acc || acc == focused_acc) {
            continue;
        }
        if (acc->is_focused) {
            DEBUG_LOG("UpdateFocusChain: clearing focus from %s",
                      acc->path ? acc->path : "?");

            SetAccessibleFocus(acc, 0);
        }
    }

    /*
     * Now announce the new accessibility focus.
     */
    SetAccessibleFocus(focused_acc, 1);

    /*
     * The containing toplevel must remain ACTIVE while one of its
     * descendants has focus.
     */
    Tk_Window topWin = focused;
    while (topWin && !Tk_IsTopLevel(topWin)) {
        topWin = Tk_Parent(topWin);
    }
    if (topWin) {
        TkAccessible *topAcc = GetAccessible(topWin);
        if (topAcc) {
            uint64_t old_states = topAcc->states;
            /*
             * ComputeStateForWidget() keeps a FRAME ACTIVE, but update
             * it now so Orca can immediately query the correct state.
             */
            topAcc->states = ComputeStateForWidget(topAcc);
            if ((old_states & ATSPI_STATE_ACTIVE) !=
                (topAcc->states & ATSPI_STATE_ACTIVE)) {

                SendStateChanged(topAcc,
                                 ATSPI_STATE_ACTIVE,
                                 (topAcc->states & ATSPI_STATE_ACTIVE) != 0);
            }
        }
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

Tk_Window
GetToplevelOfWidget(
    Tk_Window tkwin)        /* Widget to get toplevel for. */
{
    if (!tkwin) return NULL;
    Tk_Window current = tkwin;
    if (Tk_IsTopLevel(current)) return current;
    while (current != NULL) {
        if (Tk_IsTopLevel(current)) return current;
        Tk_Window parent = Tk_Parent(current);
        if (parent == NULL) break;
        current = parent;
    }
    return NULL;
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

static int
GetRoleForWidget(
    Tk_Window tkwin)        /* Tk widget to get role for. */
{
    if (!tkwin) return ATSPI_ROLE_PANEL;

    if (TkAccessibilityObject) {
        Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)tkwin);
        if (hPtr) {
            Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
            if (attrs) {
                Tcl_HashEntry *roleEntry = Tcl_FindHashEntry(attrs, "role");
                if (roleEntry) {
                    const char *result = Tcl_GetString((Tcl_Obj *)Tcl_GetHashValue(roleEntry));
                    if (result) {
                        for (int i = 0; roleMap[i].tkrole != NULL; i++) {
                            if (strcasecmp(roleMap[i].tkrole, result) == 0) {
                                DEBUG_LOG("GetRoleForWidget: path=%s explicit role attr '%s' -> %d (%s)", Tk_PathName(tkwin), result, roleMap[i].atspi_role, RoleToString(roleMap[i].atspi_role));
                                return roleMap[i].atspi_role;
                            }
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
                DEBUG_LOG("GetRoleForWidget: path=%s class=%s -> %d (%s) [class fallback]", Tk_PathName(tkwin), widgetClass, roleMap[i].atspi_role, RoleToString(roleMap[i].atspi_role));
                return roleMap[i].atspi_role;
            }
        }
    }

    if (Tk_IsTopLevel(tkwin)) {
        DEBUG_LOG("GetRoleForWidget: path=%s is toplevel -> frame", Tk_PathName(tkwin));
        return ATSPI_ROLE_FRAME;
    }

    DEBUG_LOG("GetRoleForWidget: path=%s class=%s -> PANEL fallback", Tk_PathName(tkwin), widgetClass ? widgetClass : "?", Tk_PathName(tkwin));
    return ATSPI_ROLE_PANEL;
}


/*
 *----------------------------------------------------------------------
 * GetLiveRole --
 *
 *   Resolve an accessible's current AT-SPI role. Always recomputes
 *   from the live Tk widget state rather than trusting any previously
 *   live value, so a role that only becomes known after creation
 *   (e.g. a script-level role attribute set on first focus) is never
 *   stuck at whatever it was when the accessible was first registered.
 *
 *   Accessibles with no backing Tk window (the synthetic root
 *   "application" object) have no widget to recompute from, so those
 *   keep whatever role was explicitly assigned to them.
 *
 * Results:
 *   Returns the AT-SPI role code for the accessible.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

static int
GetLiveRole(
    TkAccessible *acc)      /* Accessible to get the role for. */
{
    if (!acc) return ATSPI_ROLE_INVALID;
    if (!acc->tkwin) {
        return acc->role;
    }
    return GetRoleForWidget(acc->tkwin);
}

/*
 *----------------------------------------------------------------------
 * EnsureRoleVtables --
 *
 *   Add the Action and/or Value D-Bus interfaces for an accessible if
 *   its (live) role now calls for them and they haven't been added yet.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   May register new D-Bus vtables and emit ChildrenChanged signals.
 *----------------------------------------------------------------------
 */

static void
EnsureRoleVtables(
    TkAccessible *acc,      /* Accessible to check/update. */
    int notifyIfChanged)    /* If 1 and an interface was newly added,
                              * force clients to re-fetch this object. */
{
    if (!acc || !atspi_conn || !atspi_conn->bus || !acc->dbus_path) return;

    int role = GetLiveRole(acc);
    int added_any = 0;

    int wants_action = (role == ATSPI_ROLE_PUSH_BUTTON || role == ATSPI_ROLE_CHECK_BOX ||
                        role == ATSPI_ROLE_RADIO_BUTTON || role == ATSPI_ROLE_TOGGLE_BUTTON);
    int wants_value  = (role == ATSPI_ROLE_SPIN_BUTTON || role == ATSPI_ROLE_SLIDER ||
                        role == ATSPI_ROLE_PROGRESS_BAR || role == ATSPI_ROLE_SCROLL_BAR);

    if (wants_action && !acc->action_vtable_added &&
        acc->n_vtable_slots < TK_ACCESSIBLE_MAX_SLOTS) {
        sd_bus_slot *slot = NULL;
        int r = sd_bus_add_object_vtable(atspi_conn->bus, &slot, acc->dbus_path,
                                          ATSPI_ACTION_INTERFACE, action_vtable, acc);
        if (r >= 0 && slot) {
            acc->vtable_slots[acc->n_vtable_slots++] = slot;
            acc->action_vtable_added = 1;
            added_any = 1;
            DEBUG_LOG("EnsureRoleVtables: path=%s added Action interface (role=%d now known)",
                       acc->path ? acc->path : "?", role);
        } else {
            DEBUG_LOG("EnsureRoleVtables: path=%s failed to add Action interface, r=%d (%s)",
                       acc->path ? acc->path : "?", r, strerror(-r));
        }
    }

    if (wants_value && !acc->value_vtable_added &&
        acc->n_vtable_slots < TK_ACCESSIBLE_MAX_SLOTS) {
        sd_bus_slot *slot = NULL;
        int r = sd_bus_add_object_vtable(atspi_conn->bus, &slot, acc->dbus_path,
                                          ATSPI_VALUE_INTERFACE, value_vtable, acc);
        if (r >= 0 && slot) {
            acc->vtable_slots[acc->n_vtable_slots++] = slot;
            acc->value_vtable_added = 1;
            added_any = 1;
            DEBUG_LOG("EnsureRoleVtables: path=%s added Value interface (role=%d now known)",
                       acc->path ? acc->path : "?", role);
        } else {
            DEBUG_LOG("EnsureRoleVtables: path=%s failed to add Value interface, r=%d (%s)",
                       acc->path ? acc->path : "?", r, strerror(-r));
        }
    }

    if (added_any && notifyIfChanged && acc->parent) {
        SendChildrenChanged(acc->parent, 0, acc, 0);  /* remove */
        SendChildrenChanged(acc->parent, 0, acc, 1);  /* re-add, corrected */
    }
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

static uint64_t
ComputeStateForWidget(
    TkAccessible *acc)      /* Accessible object to compute state for. */
{
    uint64_t states = 0;
    if (!acc || !acc->tkwin) return states;

    /* Check explicit disabled state. */
    int is_disabled = 0;
    if (TkAccessibilityObject) {
        Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)acc->tkwin);
        if (hPtr) {
            Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
            if (attrs) {
                Tcl_HashEntry *stateEntry = Tcl_FindHashEntry(attrs, "state");
                if (stateEntry) {
                    Tcl_Obj *o = (Tcl_Obj *)Tcl_GetHashValue(stateEntry);
                    if (o) {
                        const char *state = Tcl_GetString(o);
                        if (state && strcmp(state, "disabled") == 0) is_disabled = 1;
                    }
                }
            }
        }
    }

    if (!is_disabled) {
        states |= ATSPI_STATE_ENABLED;
        states |= ATSPI_STATE_SENSITIVE;
    }

    /* Focusable based on role. */
    int role = GetLiveRole(acc);
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

    /* Focused. */
    if (acc->is_focused) {
        states |= ATSPI_STATE_FOCUSED;
    }

    /* Active: for FRAME (toplevels), stay ACTIVE if focused or any child is focused (Orca needs active window to read children.) */
    if (role == ATSPI_ROLE_FRAME) {
        int child_has_focus = 0;
        if (acc->tkwin) {
            for (TkWindow *c = ((TkWindow*)acc->tkwin)->childList; c; c = c->nextPtr) {
                TkAccessible *ch = GetAccessible((Tk_Window)c);
                if (ch && ch->is_focused) { child_has_focus = 1; break; }
            }
        }
        if (acc->is_focused || child_has_focus) {
            states |= ATSPI_STATE_ACTIVE;
        } else {
            /* Keep window ACTIVE once mapped. */
            states |= ATSPI_STATE_ACTIVE;
        }
    }

    /* VISIBLE/SHOWING - Wayland: if toplevel is mapped, children are visible even before child IsMapped.
     * Prevents Accerciser caching 0 state and filtering widgets as hidden. */
    if (Tk_IsMapped(acc->tkwin)) {
        states |= ATSPI_STATE_VISIBLE;
        states |= ATSPI_STATE_SHOWING;
    } else if (acc->tkwin && !Tk_IsTopLevel(acc->tkwin)) {
        Tk_Window top = acc->tkwin;
        while (top && !Tk_IsTopLevel(top)) top = Tk_Parent(top);
        if (top && Tk_IsMapped(top)) {
            states |= ATSPI_STATE_VISIBLE;
            states |= ATSPI_STATE_SHOWING;
        }
    }

    /* Editable for entries. */
    if (role == ATSPI_ROLE_ENTRY || role == ATSPI_ROLE_TEXT) {
        if (!is_disabled) {
            int is_editable = 1;
                states |= ATSPI_STATE_EDITABLE;
        }
    }

    /* Checked state. */
    if (role == ATSPI_ROLE_CHECK_BOX ||
        role == ATSPI_ROLE_RADIO_BUTTON ||
        role == ATSPI_ROLE_TOGGLE_BUTTON) {
        if (acc->interp) {
            char *value = GetValueForWidget(acc->tkwin);
            if (value) {
                if (strcmp(value, "selected") == 0 ||
                    strcmp(value, "1") == 0 ||
                    (value[0] != '0' && value[0] != '\0')) {
                    states |= ATSPI_STATE_CHECKED;
                }
                free(value);
            }
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
 *   None.
 *----------------------------------------------------------------------
 */

static char *
GetNameForWidget(
    Tk_Window tkwin)        /* Tk widget to get name for. */
{
    if (!tkwin) return NULL;

    if (TkAccessibilityObject) {
        Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)tkwin);
        if (hPtr) {
            Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
            if (attrs) {
                Tcl_HashEntry *nameEntry = Tcl_FindHashEntry(attrs, "name");
                if (nameEntry) {
                    const char *name = Tcl_GetString((Tcl_Obj *)Tcl_GetHashValue(nameEntry));
                    if (name && name[0] != '\0') return strdup(name);
                }
            }
        }
    }

    /* Fallback to explicit value hash. */
    {
        char *val = GetValueForWidget(tkwin);
        if (val && val[0] != '\0') {
            return val;
        }
        if (val) free(val);
    }

    if (Tk_IsTopLevel(tkwin)) {
        const char *pn = Tk_PathName(tkwin);
        if (pn && !(pn[0]=='.' && pn[1]=='\0')) {
            return strdup(pn);
        }
        return strdup("Tk Application");
    }

    /* Last resort: use widget path tail as name so it's never silent. */
    {
        const char *pn = Tk_PathName(tkwin);
        if (pn) {
            const char *tail = strrchr(pn, '.');
            if (tail && tail[1]) return strdup(tail+1);
            if (pn[0]) return strdup(pn);
        }
    }

    return NULL;
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
 *   None.
 *----------------------------------------------------------------------
 */

static char *
GetDescriptionForWidget(
    Tk_Window tkwin)        /* Tk widget to get description for. */
{
    if (!tkwin) return NULL;

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)tkwin);
    if (!hPtr) return NULL;

    Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
    if (!attrs) return NULL;

    Tcl_HashEntry *descEntry = Tcl_FindHashEntry(attrs, "description");
    if (!descEntry) return NULL;

    const char *desc = Tcl_GetString((Tcl_Obj *)Tcl_GetHashValue(descEntry));
    return desc ? strdup(desc) : NULL;
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
 *   None.
 *----------------------------------------------------------------------
 */

static char *
GetValueForWidget(
    Tk_Window tkwin)        /* Tk widget to get value for. */
{
    if (!tkwin) return NULL;

    if (TkAccessibilityObject) {
        Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)tkwin);
        if (hPtr) {
            Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
            if (attrs) {
                Tcl_HashEntry *valueEntry = Tcl_FindHashEntry(attrs, "value");
                if (valueEntry) {
                    Tcl_Obj *obj = (Tcl_Obj *)Tcl_GetHashValue(valueEntry);
                    if (obj) {
                        const char *value = Tcl_GetString(obj);
                        if (value && value[0]) return strdup(value);
                    }
                }
            }
        }
    }

    return NULL;
}


/*
 *----------------------------------------------------------------------
 * EnsureChildrenRegisteredRecursive --
 *
 *      Recursively ensure that all children of a window have accessible
 *      objects registered.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates TkAccessible objects for missing child windows and,
 *      when requested, emits children-changed events.
 *----------------------------------------------------------------------
 */

static void
EnsureChildrenRegisteredRecursive(
    Tk_Window tkwin,
    TkAccessible *parent_acc,
    int emitEvents)
{
    TkWindow *winPtr;
    TkWindow *childPtr;
    int index = 0;

    if (!tkwin) {
        return;
    }

    winPtr = (TkWindow *)tkwin;

    for (childPtr = winPtr->childList;
         childPtr != NULL;
         childPtr = childPtr->nextPtr, index++) {

        Tk_Window childWin = (Tk_Window)childPtr;
        TkAccessible *child_acc = GetAccessible(childWin);

        if (!child_acc) {
            Tcl_Interp *interp = Tk_Interp(tkwin);

            if (!interp && parent_acc) {
                interp = parent_acc->interp;
            }

            if (!interp) {
                continue;
            }

            child_acc = CreateAccessible(
                    interp, childWin, Tk_PathName(childWin));
            if (!child_acc) {
                continue;
            }

            if (parent_acc) {
                child_acc->parent = parent_acc;
            }

            RegisterAccessible(childWin, child_acc);
            TkAccessible_RegisterEventHandlers(childWin, child_acc);

            if (parent_acc && emitEvents) {
                SendChildrenChanged(parent_acc, index, child_acc, 1);
            }
        }
	
        EnsureChildrenRegisteredRecursive(
                childWin, child_acc, emitEvents);
    }
}


/*
 *----------------------------------------------------------------------
 * EnsureChildrenRegistered --
 *
 *      Ensure that all children of a window have accessible objects
 *      registered.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates TkAccessible objects for missing child windows and emits
 *      children-changed events.
 *----------------------------------------------------------------------
 */

static void
EnsureChildrenRegistered(
    Tk_Window tkwin,
    int emitEvents)
{
    TkAccessible *acc;
    Tk_Window parent;
    Tk_Window top;

    if (!tkwin) {
        return;
    }

    /*
     * Normally the accessible object belongs directly to tkwin.
     */
    acc = GetAccessible(tkwin);

    /*
     * If tkwin itself doesn't have one, try its parent.
     */
    if (!acc) {
        parent = Tk_Parent(tkwin);
        if (parent) {
            acc = GetAccessible(parent);
        }
    }

    /*
     * Finally, walk up to the toplevel.
     */
    if (!acc) {
        top = tkwin;

        while (top && !Tk_IsTopLevel(top)) {
            top = Tk_Parent(top);
        }

        if (top) {
            acc = GetAccessible(top);
        }
    }

    if (!acc) {
        return;
    }

    /*
     * If the accessible object points at a different window, use that
     * as the root of the scan.
     */
    if (acc->tkwin) {
        tkwin = acc->tkwin;
    }

    EnsureChildrenRegisteredRecursive(tkwin, acc, emitEvents);
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

static void
TkAccessible_RegisterEventHandlers(
    Tk_Window tkwin,        /* Tk window to register handlers for. */
    TkAccessible *acc)      /* Accessible object associated with window. */
{
    if (!tkwin || !acc) return;

    Tk_CreateEventHandler(tkwin, StructureNotifyMask,
                          TkAccessible_DestroyHandler, acc);
    Tk_CreateEventHandler(tkwin, FocusChangeMask,
                          TkAccessible_FocusHandler, acc);
    Tk_CreateEventHandler(tkwin, SubstructureNotifyMask,
                          TkAccessible_CreateHandler, acc);
    Tk_CreateEventHandler(tkwin, StructureNotifyMask,
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

static void
TkAccessible_DestroyHandler(
    void *clientData,       /* TkAccessible object pointer. */
    XEvent *eventPtr)       /* X event structure. */
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
            /* Compute accessible-filtered index from parent's child list. */
            TkWindow *childPtr;
            int acc_idx = 0;
            for (childPtr = ((TkWindow*)acc->parent->tkwin)->childList;
                 childPtr != NULL;
                 childPtr = childPtr->nextPtr) {
                TkAccessible *sib = GetAccessible((Tk_Window)childPtr);
                if (!sib) continue;
                if ((Tk_Window)childPtr == acc->tkwin) {
                    idx = acc_idx;
                    break;
                }
                acc_idx++;
            }
        }
        SendChildrenChanged(acc->parent, idx, acc, 0);
    }

    /*
     * Remove all event handlers registered on this window *before* the
     * accessible is unregistered/freed below.
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
 *
 * TkAccessible_FocusHandler --
 *
 *     Handle Tk/X focus events.
 *
 *     X FocusIn/FocusOut remains supported for environments where the
 *     Wayland/X compatibility layer generates those events.  However,
 *     logical Tk focus changes should normally call UpdateFocusChain()
 *     directly rather than depending on this handler.
 *
 * Results:
 *
 *     None.
 *
 * Side effects:
 *
 *     Updates the accessibility focus state and emits AT-SPI events.
 *
 *----------------------------------------------------------------------
 */

static void
TkAccessible_FocusHandler(
    void *clientData,       /* TkAccessible object pointer. */
    XEvent *eventPtr)       /* X event structure. */
{
    TkAccessible *acc = (TkAccessible *)clientData;

    if (!acc || !acc->tkwin || !eventPtr) {
        return;
    }

    if (eventPtr->type != FocusIn &&
        eventPtr->type != FocusOut) {
        return;
    }

    int focused = (eventPtr->type == FocusIn);

    DEBUG_LOG("TkAccessible_FocusHandler: path=%s event=%s",
              acc->path ? acc->path : "?",
              focused ? "FocusIn" : "FocusOut");

    if (focused) {
        /*
         * Use the same authoritative path as logical Tk focus changes.
         * This also clears any previously focused accessible.
         */
        UpdateFocusChain(acc->tkwin);
    } else {
        /*
         * Do not blindly clear focus here if Tk has already moved focus
         * to another widget.  Determine Tk's current focus before
	 * clearing this object.
         */
        TkWindow *focusPtr =
            TkGetFocusWin((TkWindow *)acc->tkwin);

        if (!focusPtr || focusPtr == (TkWindow *)acc->tkwin) {
            SetAccessibleFocus(acc, 0);
        }
    }

    /*
     * A toplevel becoming focused activates the window.  Do this only
     * for the actual toplevel object; widget focus is represented by
     * Event.Focus, not window:activate.
     */
    Tk_Window topWin = acc->tkwin;

    while (topWin && !Tk_IsTopLevel(topWin)) {
        topWin = Tk_Parent(topWin);
    }

    if (topWin) {
        TkAccessible *topAcc = GetAccessible(topWin);

        if (topAcc) {
            uint64_t old_states = topAcc->states;
            uint64_t new_states = ComputeStateForWidget(topAcc);

            topAcc->states = new_states;

            if ((old_states & ATSPI_STATE_ACTIVE) !=
                (new_states & ATSPI_STATE_ACTIVE)) {

                SendStateChanged(topAcc,
                                 ATSPI_STATE_ACTIVE,
                                 (new_states & ATSPI_STATE_ACTIVE) != 0);
            }

            /*
             * Only the actual toplevel produces Window.Activate.
             */
            if (topWin == acc->tkwin && focused &&
                !(old_states & ATSPI_STATE_ACTIVE)) {

                SendAtspiEvent(topAcc,
                               ATSPI_EVENT_WINDOW_ACTIVATE,
                               NULL);
            }
        }
    }

    /*
     * If the application has not yet completed the Socket.Embed
     * handshake, retry it when focus activity occurs.
     */
    if (focused && atspi_conn && !atspi_conn->is_embedded) {
        EmbedWithRegistry();
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
 *   Registers accessible objects for newly created windows and emits
 *   ChildrenChanged events.
 *----------------------------------------------------------------------
 */

static void
TkAccessible_CreateHandler(
    void *clientData,       /* TkAccessible parent object pointer. */
    XEvent *eventPtr)       /* X event structure. */
{
    if (!eventPtr || eventPtr->type != CreateNotify) return;

    TkAccessible *parentAcc = (TkAccessible *)clientData;
    if (!parentAcc) return;
    Tk_Window parentWin = parentAcc->tkwin;
    if (!parentWin) return;

    Tcl_Interp *interp = Tk_Interp(parentWin);
    if (!interp) {
        interp = parentAcc->interp;
    }
    if (!interp) return;

    Window childWindow = eventPtr->xcreatewindow.window;
    Tk_Window childWin = Tk_IdToWindow(Tk_Display(parentWin), childWindow);
    if (!childWin || GetAccessible(childWin)) return;

    TkAccessible *child_acc = CreateAccessible(interp, childWin, Tk_PathName(childWin));
    if (!child_acc) return;

    TkAccessible *parent_acc = GetAccessible(parentWin);
    if (!parent_acc) {
        parent_acc = parentAcc;
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
            idx = 0; /* Just count. */
            AccessibleList *l = parent_acc->children;
            while (l) { idx++; l = l->next; }
        } else if (parent_acc->tkwin) {
            /* Compute accessible-filtered index. */
            TkWindow *ptr;
            int acc_idx = 0;
            for (ptr = ((TkWindow*)parent_acc->tkwin)->childList; ptr; ptr = ptr->nextPtr) {
                TkAccessible *sib = GetAccessible((Tk_Window)ptr);
                if (!sib) {
                    /* The new child itself isn't yet counted as accessible in the map
                     * for this loop? It is, but we also count it if it is the target. */
                    if ((Tk_Window)ptr == childWin) {
                        idx = acc_idx;
                        break;
                    }
                    continue;
                }
                if ((Tk_Window)ptr == childWin) {
                    idx = acc_idx;
                    break;
                }
                acc_idx++;
            }
            /* If not found via filtered count (new child not yet in map), fall back to raw filtered position. */
            if (idx == -1) {
                int cnt = 0;
                for (ptr = ((TkWindow*)parent_acc->tkwin)->childList; ptr; ptr = ptr->nextPtr) {
                    if ((Tk_Window)ptr == childWin) { idx = cnt; break; }
                    if (GetAccessible((Tk_Window)ptr) || (Tk_Window)ptr == childWin) cnt++;
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

static void
TkAccessible_ConfigureHandler(
    void *clientData,       /* TkAccessible object pointer. */
    XEvent *eventPtr)       /* X event structure. */
{
    /*
     * This handler is registered under StructureNotifyMask, which
     * delivers MapNotify and UnmapNotify in addition to ConfigureNotify.
     * VISIBLE/SHOWING are derived purely from Tk_IsMapped(), so MapNotify/
     * UnmapNotify must  be handled here too.
     */
    if (!eventPtr ||
        (eventPtr->type != ConfigureNotify &&
         eventPtr->type != MapNotify &&
         eventPtr->type != UnmapNotify)) {
        return;
    }

    TkAccessible *handlerAcc = (TkAccessible *)clientData;
    if (!handlerAcc) return;
    Tk_Window tkwin = handlerAcc->tkwin;
    if (!tkwin) return;

    TkAccessible *acc = GetAccessible(tkwin);
    if (!acc) return;

    if (eventPtr->type == ConfigureNotify) {
        acc->width = Tk_Width(tkwin);
        acc->height = Tk_Height(tkwin);
        Tk_GetRootCoords(tkwin, &acc->x, &acc->y);
    }

    uint64_t old_states = acc->states;
    acc->states = ComputeStateForWidget(acc);

    DEBUG_LOG("ConfigureHandler: path=%s xevent=%d old_states=0x%llx new_states=0x%llx",
              acc->path?acc->path:"?", eventPtr->type,
              (unsigned long long)old_states, (unsigned long long)acc->states);

    if ((old_states & ATSPI_STATE_VISIBLE) != (acc->states & ATSPI_STATE_VISIBLE)) {
        SendStateChanged(acc, ATSPI_STATE_VISIBLE, (acc->states & ATSPI_STATE_VISIBLE) != 0);
    }
    if ((old_states & ATSPI_STATE_SHOWING) != (acc->states & ATSPI_STATE_SHOWING)) {
        SendStateChanged(acc, ATSPI_STATE_SHOWING, (acc->states & ATSPI_STATE_SHOWING) != 0);
    }

    if (eventPtr->type != ConfigureNotify) {
        /* MapNotify/UnmapNotify: state change above is all there is to do. */
        return;
    }

    /*
     * Reconcile picks up and pushes any name change (e.g. a label's
     * -text was updated, which normally also resizes it and lands us
     * here).
     */
    TkAccessible_Reconcile(acc);
    EnsureChildrenRegistered(tkwin, 1);
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

static int
IsScreenReaderActive(void)
{
    DEBUG_LOG("IsScreenReaderActive: checking Orca");
    FILE *fp = popen("pgrep -x orca", "r");
    if (!fp) return 0;
    char buffer[16];
    int running = (fgets(buffer, sizeof(buffer), fp) != NULL);
    pclose(fp);
    DEBUG_LOG("IsScreenReaderActive: %d", running);
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

static sd_bus *
ConnectToAtspiBus(void)
{
    sd_bus *a11y_bus = NULL;
    sd_bus *session = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *addr = NULL;
    int r;

    DEBUG_LOG("ConnectToAtspiBus: enter - attempting to locate AT-SPI bus (NOT session bus)");

    /* Ask org.a11y.Bus for its address. */
    DEBUG_LOG("ConnectToAtspiBus: querying org.a11y.Bus.GetAddress on session bus");
    r = sd_bus_default_user(&session);
    if (r < 0) {
        DEBUG_LOG("ConnectToAtspiBus: sd_bus_default_user failed r=%d (%s)", r, strerror(-r));
        return NULL;
    }

    r = sd_bus_call_method(session,
        "org.a11y.Bus",
        "/org/a11y/bus",
        "org.a11y.Bus",
        "GetAddress",
        &error,
        &reply,
        "");
    if (r < 0) {
        DEBUG_LOG("ConnectToAtspiBus: GetAddress call failed r=%d (%s) err_name=%s err_msg=%s -- a11y bus not available (headless/VirtualBox without full session?)",
                  r, strerror(-r),
                  error.name ? error.name : "(none)",
                  error.message ? error.message : "(none)");
        sd_bus_error_free(&error);
        if (reply) sd_bus_message_unref(reply);
        sd_bus_unref(session);
        DEBUG_LOG("ConnectToAtspiBus: FAIL - not falling back to plain session bus (Orca listens on a11y bus, not session bus). Accessibility will be disabled until a11y bus appears.");
        return NULL;
    }

    r = sd_bus_message_read(reply, "s", &addr);
    if (r < 0 || !addr || addr[0] == '\0') {
        DEBUG_LOG("ConnectToAtspiBus: GetAddress read addr failed r=%d addr=%s", r, addr ? addr : "(null)");
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        sd_bus_unref(session);
        return NULL;
    }

    DEBUG_LOG("ConnectToAtspiBus: GetAddress returned addr=%s", addr);
    r = sd_bus_new(&a11y_bus);
    if (r < 0) {
        DEBUG_LOG("ConnectToAtspiBus: sd_bus_new for a11y addr failed r=%d", r);
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        sd_bus_unref(session);
        return NULL;
    }
    r = sd_bus_set_address(a11y_bus, addr);
    if (r < 0) {
        DEBUG_LOG("ConnectToAtspiBus: sd_bus_set_address a11y failed r=%d (%s)", r, strerror(-r));
        sd_bus_unref(a11y_bus);
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        sd_bus_unref(session);
        return NULL;
    }
    r = sd_bus_set_bus_client(a11y_bus, 1);
    if (r < 0) {
        DEBUG_LOG("ConnectToAtspiBus: sd_bus_set_bus_client a11y failed r=%d", r);
        sd_bus_unref(a11y_bus);
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        sd_bus_unref(session);
        return NULL;
    }
    r = sd_bus_start(a11y_bus);
    if (r < 0) {
        DEBUG_LOG("ConnectToAtspiBus: sd_bus_start a11y failed r=%d (%s)", r, strerror(-r));
        sd_bus_unref(a11y_bus);
        sd_bus_error_free(&error);
        sd_bus_message_unref(reply);
        sd_bus_unref(session);
        return NULL;
    }

    {
        const char *unique = NULL;
        sd_bus_get_unique_name(a11y_bus, &unique);
        DEBUG_LOG("ConnectToAtspiBus: SUCCESS via GetAddress, unique=%s addr=%s", unique ? unique : "?", addr);
    }
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    sd_bus_unref(session);
    return a11y_bus;
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
    atspi_bus = bus;   /* Expose for TkWaylandAtspiProcessEvents() / the notifier. */
    {
        const char *unique = NULL;
        sd_bus_get_unique_name(bus, &unique);
        DEBUG_LOG("InitializeAtspiConnection: connected to AT-SPI bus unique=%s", unique ? unique : "?");
    }

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

    /*Create root accessible object (application). */
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
    atspi_conn->root_accessible->path       = strdup("application");
    atspi_conn->root_accessible->dbus_path  = strdup("/org/a11y/atspi/accessible/root");
    atspi_conn->root_accessible->ref_count  = 1;
    atspi_conn->root_accessible->states     = ATSPI_STATE_ENABLED | ATSPI_STATE_SENSITIVE | ATSPI_STATE_SHOWING | ATSPI_STATE_VISIBLE;
    

    RegisterDbusObject(atspi_conn->root_accessible);

    /* Re-register Cache at fixed path - LIVE version, no cached data.
     * Required for accerciser/Orca bootstrap, but implementation does live reads. */
    {
        sd_bus_slot *cache_slot = NULL;
        int r2 = sd_bus_add_object_vtable(atspi_conn->bus, &cache_slot,
                                          "/org/a11y/atspi/cache",
                                          "org.a11y.atspi.Cache",
                                          cache_vtable, NULL);
        if (r2 >= 0) {
            /* Keep slot for cleanup - reuse first free slot in root accessible */
            if (atspi_conn->root_accessible->n_vtable_slots < TK_ACCESSIBLE_MAX_SLOTS) {
                atspi_conn->root_accessible->vtable_slots[atspi_conn->root_accessible->n_vtable_slots++] = cache_slot;
            }
        }
    }

    /*
     * Register as an AT-SPI application via the real Socket.Embed
     * handshake. Not fatal if the registry isn't up yet - it can still
     * discover us later.
     */
    atspi_conn->is_initialized = 1;
    EmbedWithRegistry();

    return true;
}

/*
 *----------------------------------------------------------------------
 * EmbedWithRegistry --
 *
 *   Embed our application into the registry's accessible tree using
 *   the Socket.Embed method.
 *
 *   The Socket.Embed method must be called on the AT-SPI Registry daemon,
 *   specifically on its root object path. The destination service must be
 *   "org.a11y.atspi.Registry" and the object path must be
 *   "/org/a11y/atspi/accessible/root" (where the Socket interface is
 *   actually implemented). Using the wrong destination or path causes
 *   UnknownMethod errors and the application never becomes visible to
 *   Orca or Accerciser.
 *
 * Results:
 *   Returns true on success, false on failure.
 *
 * Side effects:
 *   Stores the desktop reference in the global connection state.
 *   On success, sets atspi_conn->is_embedded to true and re-emits
 *   toplevel events.
 *----------------------------------------------------------------------
 */

static bool
EmbedWithRegistry(void)
{
    DEBUG_LOG("EmbedWithRegistry: enter");
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *desktop_name = NULL;
    const char *desktop_path = NULL;
    int r;

    if (!atspi_conn || !atspi_conn->bus || !atspi_conn->root_accessible) {
        DEBUG_LOG("EmbedWithRegistry: no connection or root accessible");
        return false;
    }

    r = sd_bus_call_method(atspi_conn->bus,
        "org.a11y.atspi.Registry",          /* Correct destination */
        ATSPI_DBUS_PATH_ROOT,               /* "/org/a11y/atspi/accessible/root" */
        "org.a11y.atspi.Socket",            /* Interface */
        "Embed",                            /* Method */
        &error,
        &reply,
        "(so)", SelfBusName(), atspi_conn->root_accessible->dbus_path);

    if (r < 0) {
        DEBUG_LOG("EmbedWithRegistry: Embed failed r=%d (%s) err_name=%s err_msg=%s",
                  r, strerror(-r),
                  error.name ? error.name : "(none)",
                  error.message ? error.message : "(none)");
        sd_bus_error_free(&error);
        if (reply) sd_bus_message_unref(reply);
        atspi_conn->is_embedded = 0;
        return false;
    }

    r = sd_bus_message_read(reply, "(so)", &desktop_name, &desktop_path);
    if (r >= 0 && desktop_name && desktop_path) {
        if (atspi_conn->desktop_bus_name) Tcl_Free(atspi_conn->desktop_bus_name);
        if (atspi_conn->desktop_path) Tcl_Free(atspi_conn->desktop_path);
        atspi_conn->desktop_bus_name = strdup(desktop_name);
        atspi_conn->desktop_path = strdup(desktop_path);
        atspi_conn->is_embedded = 1;
        DEBUG_LOG("EmbedWithRegistry: SUCCESS desktop=%s %s", desktop_name, desktop_path);
    } else {
        DEBUG_LOG("EmbedWithRegistry: read reply failed r=%d", r);
        atspi_conn->is_embedded = 0;
    }

    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);

    /* If embed succeeded, re-emit toplevel creation events with ChildrenChanged. */
    if (atspi_conn->is_embedded) {
        int idx = 0;
        for (AccessibleList *l = atspi_conn->toplevel_accessibles; l != NULL; l = l->next, idx++) {
            if (l->acc) {
                /* Set parent to root. */
                l->acc->parent = atspi_conn->root_accessible;
                SendChildrenChanged(atspi_conn->root_accessible, idx, l->acc, 1);
                SendAtspiEvent(l->acc, ATSPI_EVENT_WINDOW_CREATE, NULL);
                EmitObjectEventFull(l->acc, "PropertyChange", "accessible-name", 0, 0, NULL);
            }
        }
    }

    return atspi_conn->is_embedded;
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

static int
AddAccessibleCmd(
    TCL_UNUSED(void *),     /* Client data (unused). */
    Tcl_Interp *interp,     /* Tcl interpreter. */
    int objc,               /* Number of arguments. */
    Tcl_Obj *const objv[])  /* Argument objects. */
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
 *   Sends a D-Bus announcement.
 *----------------------------------------------------------------------
 */

static int
EmitSelectionChangedCmd(
    TCL_UNUSED(void *),     /* Client data (unused). */
    Tcl_Interp *interp,     /* Tcl interpreter. */
    int objc,               /* Number of arguments. */
    Tcl_Obj *const objv[])  /* Argument objects. */
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
    
    PostAccessibilityAnnouncement(acc, "selection changed", 0);
    return TCL_OK;
}
/*
 *----------------------------------------------------------------------
 *
 * EmitFocusChangedCmd --
 *
 *     Tcl command implementation for ::tk::accessible::emit_focus_change.
 *
 *     This command is used by Tk widget code when logical focus changes
 *     without an X FocusIn event, including menu traversal and other
 *     Wayland-specific focus paths.
 *
 * Results:
 *
 *     Returns TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *
 *     Updates the accessibility focus chain and emits an AT-SPI Focus
 *     event for the newly focused object.
 *
 *----------------------------------------------------------------------
 */

static int
EmitFocusChangedCmd(
    TCL_UNUSED(void *),     /* Client data (unused). */
    Tcl_Interp *interp,     /* Tcl interpreter. */
    int objc,               /* Number of arguments. */
    Tcl_Obj *const objv[])  /* Argument objects. */
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "window");
        return TCL_ERROR;
    }

    const char *windowName = Tcl_GetString(objv[1]);

    Tk_Window tkwin =
        Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));

    if (!tkwin) {
        return TCL_OK;
    }

    /*
     * Make sure the widget and its hierarchy are registered before
     * attempting to announce focus.
     */
    RegisterWidgetRecursive(interp, tkwin);
    EnsureAccessibleInHierarchy(interp, tkwin);

    /*
     * This is deliberately the same path used by real FocusIn events.
     *
     * Do not call SetAccessibleFocus() directly here: UpdateFocusChain()
     * must first clear focus from the previous accessible object.
     */
    UpdateFocusChain(tkwin);

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

static int
IsScreenReaderRunningCmd(
    TCL_UNUSED(void *),     /* Client data (unused). */
    Tcl_Interp *interp,     /* Tcl interpreter. */
    TCL_UNUSED(int),        /* objc */
    TCL_UNUSED(Tcl_Obj *const *)) /* objv */
{
    bool result = IsScreenReaderActive();

    DEBUG_LOG("Screen reader active: %d\n", (int)result);

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

int
TkWaylandAccessibility_Init(
    Tcl_Interp *interp)     /* Tcl interpreter. */
{
    DEBUG_LOG("TkWaylandAccessibility_Init: enter");

    /* Initialize D-Bus connection to at-spi. */
    if (!InitializeAtspiConnection()) {
        Tcl_AppendResult(interp,
            "Warning: Could not connect to AT-SPI - accessibility disabled for now",
            (char *)NULL);
        /* Proceed anyway – don't block Tk init. */
    }

    /* Initialize main window. */
    Tk_Window mainWin = Tk_MainWindow(interp);

if (mainWin) {
    
    Tk_MakeWindowExist(mainWin);
    TkAccessible *main_acc =
        CreateAccessible(interp, mainWin, Tk_PathName(mainWin));

    if (main_acc) {
        RegisterAccessible(mainWin, main_acc);
        TkAccessible_RegisterEventHandlers(mainWin, main_acc);

        /* Register all existing widgets. */
        RegisterWidgetRecursive(interp, mainWin);
    }
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
