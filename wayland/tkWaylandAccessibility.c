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

/* Debugging */
#define DEBUG_CHANNEL stderr
#define DEBUG_LABEL "accessibility"


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

/* at-spi D-Bus paths. */
#define ATSPI_DBUS_PATH_REGISTRY  "/org/a11y/atspi/registry"
#define ATSPI_DBUS_PATH_ROOT      "/org/a11y/atspi/accessible/root"

/*
 * at-spi role constants.
 *
 * IMPORTANT: these MUST match the real AtspiRole wire values from
 * at-spi2-core's atspi-constants.h, since Role is sent over D-Bus as a
 * plain uint32 and decoded by libatspi/Orca/Accerciser against that
 * spec-frozen enum -- NOT against any local numbering scheme. Previously
 * this block used a compact, hand-numbered 0..22 sequence that was
 * internally self-consistent (matched GetRoleName()/roleMap()/every
 * role == ATSPI_ROLE_X check in this file) but did not match the real
 * enum at all, so every accessible was reported to AT-SPI as the wrong
 * kind of object (toplevels as ALERT, buttons as ANIMATION, etc.),
 * which is why Orca never announced anything and Accerciser's tree
 * stopped short of the real widgets. Verify these against
 * /usr/include/at-spi-2.0/atspi/atspi-constants.h or `pyatspi.ROLE_*`
 * on the target system if you add any additional roles beyond this set.
 */

 
#define ATSPI_ROLE_INVALID           0
#define ATSPI_ROLE_ACCELERATOR_LABEL 1
#define ATSPI_ROLE_ALERT             2
#define ATSPI_ROLE_ANIMATION         3
#define ATSPI_ROLE_ARROW             4
#define ATSPI_ROLE_CALENDAR          5
#define ATSPI_ROLE_CANVAS            6
#define ATSPI_ROLE_CHECK_BOX         7
#define ATSPI_ROLE_CHECK_MENU_ITEM   8
#define ATSPI_ROLE_COLOR_CHOOSER     9
#define ATSPI_ROLE_COLUMN_HEADER     10
#define ATSPI_ROLE_COMBO_BOX         11
#define ATSPI_ROLE_DATE_EDITOR       12
#define ATSPI_ROLE_DESKTOP_ICON      13
#define ATSPI_ROLE_DESKTOP_FRAME     14
#define ATSPI_ROLE_DIAL              15
#define ATSPI_ROLE_DIALOG            16
#define ATSPI_ROLE_DIRECTORY_PANE    17
#define ATSPI_ROLE_DRAWING_AREA      18
#define ATSPI_ROLE_FILE_CHOOSER      19
#define ATSPI_ROLE_FILLER            20
/* 21 = ATSPI_ROLE_FOCUS_TRAVERSABLE (reserved by spec, slot intentionally skipped) */
#define ATSPI_ROLE_FONT_CHOOSER      22
#define ATSPI_ROLE_FRAME             23
#define ATSPI_ROLE_GLASS_PANE        24
#define ATSPI_ROLE_HTML_CONTAINER    25
#define ATSPI_ROLE_ICON              26
#define ATSPI_ROLE_IMAGE             27
#define ATSPI_ROLE_INTERNAL_FRAME    28
#define ATSPI_ROLE_LABEL             29
#define ATSPI_ROLE_LAYERED_PANE      30
#define ATSPI_ROLE_LIST              31
#define ATSPI_ROLE_LIST_ITEM         32
#define ATSPI_ROLE_MENU              33
#define ATSPI_ROLE_MENU_BAR          34
#define ATSPI_ROLE_MENU_ITEM         35
#define ATSPI_ROLE_OPTION_PANE       36
#define ATSPI_ROLE_PAGE_TAB          37
#define ATSPI_ROLE_PAGE_TAB_LIST     38
#define ATSPI_ROLE_PANEL             39
#define ATSPI_ROLE_PASSWORD_TEXT     40
#define ATSPI_ROLE_POPUP_MENU        41
#define ATSPI_ROLE_PROGRESS_BAR      42
#define ATSPI_ROLE_PUSH_BUTTON       43
#define ATSPI_ROLE_RADIO_BUTTON      44
#define ATSPI_ROLE_RADIO_MENU_ITEM   45
#define ATSPI_ROLE_ROOT_PANE         46
#define ATSPI_ROLE_ROW_HEADER        47
#define ATSPI_ROLE_SCROLL_BAR        48
#define ATSPI_ROLE_SCROLL_PANE       49
#define ATSPI_ROLE_SEPARATOR         50
#define ATSPI_ROLE_SLIDER            51
#define ATSPI_ROLE_SPIN_BUTTON       52
#define ATSPI_ROLE_SPLIT_PANE        53
#define ATSPI_ROLE_STATUS_BAR        54
#define ATSPI_ROLE_TABLE             55
#define ATSPI_ROLE_TABLE_CELL        56
#define ATSPI_ROLE_TABLE_COLUMN_HEADER 57
#define ATSPI_ROLE_TABLE_ROW_HEADER  58
#define ATSPI_ROLE_TEAROFF_MENU_ITEM 59
#define ATSPI_ROLE_TERMINAL          60
#define ATSPI_ROLE_TEXT              61
#define ATSPI_ROLE_TOGGLE_BUTTON     62
#define ATSPI_ROLE_TOOL_BAR          63
#define ATSPI_ROLE_TOOL_TIP          64
#define ATSPI_ROLE_TREE              65
#define ATSPI_ROLE_TREE_TABLE        66
#define ATSPI_ROLE_UNKNOWN           67
#define ATSPI_ROLE_VIEWPORT          68
#define ATSPI_ROLE_WINDOW            69
/* 70 = ATSPI_ROLE_EXTENDED (deprecated/reserved, slot intentionally skipped) */
#define ATSPI_ROLE_HEADER            71
#define ATSPI_ROLE_FOOTER            72
#define ATSPI_ROLE_PARAGRAPH         73
#define ATSPI_ROLE_RULER             74
#define ATSPI_ROLE_APPLICATION       75
#define ATSPI_ROLE_AUTOCOMPLETE      76
#define ATSPI_ROLE_EDITBAR           77
#define ATSPI_ROLE_EMBEDDED          78
#define ATSPI_ROLE_ENTRY             79
#define ATSPI_ROLE_CHART             80
#define ATSPI_ROLE_CAPTION           81
#define ATSPI_ROLE_DOCUMENT_FRAME    82
#define ATSPI_ROLE_HEADING           83
#define ATSPI_ROLE_PAGE              84
#define ATSPI_ROLE_SECTION           85
#define ATSPI_ROLE_REDUNDANT_OBJECT  86
#define ATSPI_ROLE_FORM              87
#define ATSPI_ROLE_LINK              88
#define ATSPI_ROLE_INPUT_METHOD_WINDOW 89
#define ATSPI_ROLE_TABLE_ROW         90
#define ATSPI_ROLE_TREE_ITEM         91
#define ATSPI_ROLE_DOCUMENT_SPREADSHEET 92
#define ATSPI_ROLE_DOCUMENT_PRESENTATION 93
#define ATSPI_ROLE_DOCUMENT_TEXT     94
#define ATSPI_ROLE_DOCUMENT_WEB      95
#define ATSPI_ROLE_DOCUMENT_EMAIL    96
#define ATSPI_ROLE_COMMENT           97
#define ATSPI_ROLE_LIST_BOX          98
#define ATSPI_ROLE_GROUPING          99
#define ATSPI_ROLE_IMAGE_MAP         100
#define ATSPI_ROLE_NOTIFICATION      101
#define ATSPI_ROLE_INFO_BAR          102
/* Compatibility aliases for older code */
#define ATSPI_ROLE_BUTTON            ATSPI_ROLE_PUSH_BUTTON
#define ATSPI_ROLE_LIST_BOX_ALIAS    ATSPI_ROLE_LIST_BOX

/* at-spi state constants (bit flags).
 * Bit POSITIONS must match the real AtspiStateType enum index from
 * atspi-constants.h -- verified against the published AT-SPI2 D-Bus
 * spec (org.a11y.atspi.Accessible.GetState). VISIBLE (30) and SHOWING
 * (25) are the two bits every AT-SPI client checks before treating an
 * object as onscreen at all; per spec "the absence of VISIBLE and
 * SHOWING is semantically equivalent to saying an object is hidden." */
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
    char *cached_name;
    char *cached_description;
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

    /*
     * D-Bus slots for this object (for cleanup). An accessible can have
     * several interfaces registered on the same dbus_path (Accessible,
     * Component, and one of Action/Value/Text/Selection depending on
     * role, plus Application for the root). Cache lives at its own fixed
     * path /org/a11y/atspi/cache, not on any accessible's path. Every
     * sd_bus_add_object_vtable() call must have its slot captured here so
     * FreeAccessible() can unref all of them -- a slot that is never
     * captured (NULL passed as the ret_slot argument) is "floating" and
     * stays registered, pointing at this object's memory, for the
     * lifetime of the bus even after the object is freed.
     */
#define TK_ACCESSIBLE_MAX_SLOTS 8
    sd_bus_slot *vtable_slots[TK_ACCESSIBLE_MAX_SLOTS];
    int n_vtable_slots;
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
static char *TryCgetString(Tk_Window tkwin, const char *option);
static uint64_t ComputeStateForWidget(TkAccessible *acc);
static char *GetNameForWidget(Tk_Window tkwin);
static char *GetDescriptionForWidget(Tk_Window tkwin);
static char *GetValueForWidget(Tk_Window tkwin);
static void RegisterToplevel(TkAccessible *acc);
static void UnregisterToplevel(TkAccessible *acc);
static void RegisterWidgetRecursive(Tcl_Interp *interp, Tk_Window tkwin);
static void EnsureChildrenRegistered(Tk_Window tkwin);
static void EnsureChildrenRegisteredEx(Tk_Window tkwin, int emitEvents);
static void EnsureChildrenRegisteredRecursive(Tk_Window tkwin, TkAccessible *parent_acc);
static void EnsureChildrenRegisteredRecursiveEx(Tk_Window tkwin, TkAccessible *parent_acc, int emitEvents);
static void UpdateFocusChain(Tk_Window focused);
static char *Tcl_Strdup(const char *s);
static void SetAccessibleFocus(TkAccessible *acc, int focused);

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
/*
 * TkWaylandAtspiProcessEvents is exported (not static) -- see its
 * definition below -- so tkWaylandNotify.c's CheckProc can drain
 * atspi_bus on the same cadence it already uses for ibus_bus.
 */
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
static int atspi_draining = 0;

/*
 * D-Bus vtables - these map functions to the ati-spi API.
 */

/*
 * org.a11y.atspi.Accessible interface.
 *
 * Name, Description, and Parent are PROPERTIES per at-spi2-core's
 * Accessible.xml.  State is read via a method called GetState (singular)
 * returning "au" (two packed uint32s).
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
Tcl_Strdup(
    const char *s)          /* String to duplicate. */
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
	 *  listbox rows.
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
    /* No custom attributes for now. */
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
 *   AT-SPI's Accessible.GetState is declared "" -> "au" in
 *   at-spi2-core/xml/Accessible.xml: an array of two packed uint32s
 *   (low 32 bits, then high 32 bits of the 64-bit state bitfield), not
 *   a single "t". A client asking for "GetState" and finding only a
 *   "GetStates" method with a "t" return never gets a state at all.
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
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    /* Use cached states to avoid Tcl_Eval re-entrancy from D-Bus thread. */
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
    sd_bus_message *reply = NULL;
    int r;

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;
    sd_bus_message_append(reply, "u", (uint32_t)(acc ? acc->role : ATSPI_ROLE_INVALID));
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
    const char *name = "";
    if (acc) {
        if (acc->is_virtual && acc->virtual_name) {
            name = acc->virtual_name;
        } else if (acc->cached_name) {
            name = acc->cached_name;
        } else if (acc->path) {
            name = acc->path;
        }
    }
    return sd_bus_message_append(reply, "s", name);
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
    const char *desc = "";
    if (acc && acc->cached_description) {
        desc = acc->cached_description;
    }
    return sd_bus_message_append(reply, "s", desc);
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
 * dbus_method_grab_focus --
 *
 *   D-Bus method handler for GrabFocus on the Accessible interface.
 *   Attempts to set focus to the accessible object.
 *
 *   Important: This method should NOT emit a focus event directly.
 *   The Tk focus change (focus -force) will generate a FocusIn event
 *   that triggers TkAccessible_FocusHandler, which is the authoritative
 *   source of focus notifications.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Changes focus to the specified widget. The focus event is emitted
 *   by the resulting FocusIn handler.
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

    /* Actually give Tk focus to the widget. */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "focus -force %s", Tk_PathName(acc->tkwin));
    Tcl_Eval(acc->interp, cmd);

    /*
     * Do NOT update is_focused or emit Focus here.
     * The resulting Tk FocusIn event is the authoritative source of
     * the accessibility focus notification.
     */

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

static int
dbus_method_text_get_text(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t start, end;
    int r = sd_bus_message_read(m, "ii", &start, &end);
    if (r < 0) return r;

    if (!acc || !acc->tkwin || !acc->interp) {
        return sd_bus_reply_method_return(m, "s", "");
    }

    char cmd[512];
    const char *path = Tk_PathName(acc->tkwin);
    /* Try to get full text. */
    snprintf(cmd, sizeof(cmd), "%s get", path);
    if (Tcl_Eval(acc->interp, cmd) != TCL_OK) {
        /* Try text widget: get 1.0 end-1c */
        snprintf(cmd, sizeof(cmd), "%s get 1.0 {end -1c}", path);
        if (Tcl_Eval(acc->interp, cmd) != TCL_OK) {
            Tcl_ResetResult(acc->interp);
            return sd_bus_reply_method_return(m, "s", "");
        }
    }
    const char *full = Tcl_GetStringResult(acc->interp);
    if (!full) full = "";
    int len = (int)strlen(full);
    if (start < 0) start = 0;
    if (end < 0 || end > len) end = len;
    if (start > len) start = len;
    if (end < start) end = start;
    int sublen = end - start;
    char *sub = (char *)Tcl_Alloc(sublen + 1);
    if (sublen > 0) memcpy(sub, full + start, sublen);
    sub[sublen] = '\0';
    Tcl_ResetResult(acc->interp);
    int ret = sd_bus_reply_method_return(m, "s", sub);
    Tcl_Free(sub);
    return ret;
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

static int
dbus_method_text_get_caret_offset(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    if (!acc || !acc->tkwin || !acc->interp) {
        return sd_bus_reply_method_return(m, "i", 0);
    }
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s index insert", Tk_PathName(acc->tkwin));
    if (Tcl_Eval(acc->interp, cmd) == TCL_OK) {
        Tcl_Obj *resObj = Tcl_GetObjResult(acc->interp);
        long iv;
        if (Tcl_GetLongFromObj(acc->interp, resObj, &iv) == TCL_OK) {
            Tcl_ResetResult(acc->interp);
            return sd_bus_reply_method_return(m, "i", (int)iv);
        }
        /* Text widget returns "line.char" */
        const char *s = Tcl_GetString(resObj);
        int line=1, ch=0;
        if (s && sscanf(s, "%d.%d", &line, &ch) == 2) {
            /* Approximate linear offset: need full text to compute, for now return char */
            Tcl_ResetResult(acc->interp);
            return sd_bus_reply_method_return(m, "i", ch);
        }
        Tcl_ResetResult(acc->interp);
    } else {
        Tcl_ResetResult(acc->interp);
    }
    return sd_bus_reply_method_return(m, "i", 0);
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

static int
dbus_method_text_get_character_count(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    if (!acc || !acc->tkwin || !acc->interp) {
        return sd_bus_reply_method_return(m, "i", 0);
    }
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s get", Tk_PathName(acc->tkwin));
    if (Tcl_Eval(acc->interp, cmd) != TCL_OK) {
        snprintf(cmd, sizeof(cmd), "%s get 1.0 {end -1c}", Tk_PathName(acc->tkwin));
        if (Tcl_Eval(acc->interp, cmd) != TCL_OK) {
            Tcl_ResetResult(acc->interp);
            return sd_bus_reply_method_return(m, "i", 0);
        }
    }
    const char *txt = Tcl_GetStringResult(acc->interp);
    int len = txt ? (int)strlen(txt) : 0;
    Tcl_ResetResult(acc->interp);
    return sd_bus_reply_method_return(m, "i", len);
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

static int
dbus_method_selection_get_n_selections(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    if (!acc || !acc->tkwin || !acc->interp) {
        return sd_bus_reply_method_return(m, "i", 0);
    }
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s curselection", Tk_PathName(acc->tkwin));
    if (Tcl_Eval(acc->interp, cmd) != TCL_OK) {
        Tcl_ResetResult(acc->interp);
        return sd_bus_reply_method_return(m, "i", 0);
    }
    Tcl_Obj *listObj = Tcl_GetObjResult(acc->interp);
    Tcl_Size len = 0;
    Tcl_ListObjLength(acc->interp, listObj, &len);
    Tcl_ResetResult(acc->interp);
    return sd_bus_reply_method_return(m, "i", (int)len);
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

static int
dbus_method_selection_get_selection(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t idx;
    int r = sd_bus_message_read(m, "i", &idx);
    if (r < 0) return r;
    if (!acc || !acc->tkwin) {
        return sd_bus_reply_method_return(m, "(so)", "", "/org/a11y/atspi/null");
    }
    /* For listbox/tree, children are items; return child at selection index. */
    AccessibleList *l = acc->children;
    int i = 0;
    while (l && i < idx) { l = l->next; i++; }
    if (l && l->acc && l->acc->dbus_path) {
        return sd_bus_reply_method_return(m, "(so)", SelfBusName(), l->acc->dbus_path);
    }
    /* Fallback: if real widget, try to map curselection index to child. */
    if (acc->interp) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "%s curselection", Tk_PathName(acc->tkwin));
        if (Tcl_Eval(acc->interp, cmd) == TCL_OK) {
            Tcl_Obj *listObj = Tcl_GetObjResult(acc->interp);
            Tcl_Size len;
            Tcl_ListObjLength(acc->interp, listObj, &len);
            if (idx >=0 && idx < len) {
                Tcl_Obj *elem;
                Tcl_ListObjIndex(acc->interp, listObj, idx, &elem);
                if (elem) {
                    long selIdx;
                    if (Tcl_GetLongFromObj(acc->interp, elem, &selIdx) == TCL_OK) {
                        /* Return self with index? For simplicity return self's child path if virtual */
                        /* If no virtual children, return parent ref (Orca will still work with n_selections) */
                    }
                }
            }
            Tcl_ResetResult(acc->interp);
        } else {
            Tcl_ResetResult(acc->interp);
        }
    }
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

static int
dbus_method_selection_is_selected(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* TkAccessible object pointer. */
    TCL_UNUSED(sd_bus_error *)) /* ret_error */
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t childIdx;
    int r = sd_bus_message_read(m, "i", &childIdx);
    if (r < 0) return r;
    if (!acc || !acc->tkwin || !acc->interp) {
        return sd_bus_reply_method_return(m, "b", 0);
    }
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s selection includes %d", Tk_PathName(acc->tkwin), childIdx);
    if (Tcl_Eval(acc->interp, cmd) == TCL_OK) {
        Tcl_Obj *res = Tcl_GetObjResult(acc->interp);
        int b = 0;
        Tcl_GetBooleanFromObj(acc->interp, res, &b);
        Tcl_ResetResult(acc->interp);
        return sd_bus_reply_method_return(m, "b", b);
    }
    Tcl_ResetResult(acc->interp);
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

static int
dbus_method_selection_select_all(
    sd_bus_message *m,      /* D-Bus method call message. */
    TCL_UNUSED(void *),     /* userdata */
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

static int
dbus_method_selection_clear_selection(
    sd_bus_message *m,      /* D-Bus method call message. */
    TCL_UNUSED(void *),     /* userdata */
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

static int
dbus_method_selection_add_selection(
    sd_bus_message *m,      /* D-Bus method call message. */
    TCL_UNUSED(void *),     /* userdata */
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

static int
dbus_method_selection_remove_selection(
    sd_bus_message *m,      /* D-Bus method call message. */
    TCL_UNUSED(void *),     /* userdata */
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

static void
AppendCacheItem(
    sd_bus_message *reply,
    TkAccessible *acc,
    TkAccessible *parent_acc,
    const char *app_path,
    int index_in_parent)
{
    if (!acc || !acc->dbus_path) return;
    if (parent_acc && parent_acc == acc) parent_acc = NULL;
    if (parent_acc && parent_acc->dbus_path && strcmp(parent_acc->dbus_path, acc->dbus_path)==0) parent_acc = NULL;
    int childcnt = 0;
    if (acc->tkwin && !acc->is_virtual) {
        int _cntIter = 0;
        TkWindow *_slow = ((TkWindow*)acc->tkwin)->childList;
        TkWindow *_fast = _slow ? _slow->nextPtr : NULL;
        for (TkWindow *c = ((TkWindow*)acc->tkwin)->childList; c; c = c->nextPtr) {
            if (_fast && _slow && _fast == _slow) break;
            if (_cntIter > 10000) break;
            if (GetAccessible((Tk_Window)c)) childcnt++;
            _cntIter++;
            if (_cntIter % 2 == 0) {
                _slow = _slow ? _slow->nextPtr : NULL;
                _fast = _fast ? _fast->nextPtr : NULL;
                if (_fast) _fast = _fast->nextPtr;
            }
        }
    } else if (acc->children) {
        for (AccessibleList *l = acc->children; l; l = l->next) if (l->acc) childcnt++;
    }
    sd_bus_message_open_container(reply, 'r', "(so)(so)(so)iiassusau");
    AppendAccessibleRef(reply, acc->dbus_path);
    AppendAccessibleRef(reply, app_path);
    if (parent_acc && parent_acc->dbus_path) AppendAccessibleRef(reply, parent_acc->dbus_path);
    else AppendAccessibleRef(reply, app_path);
    sd_bus_message_append(reply, "i", index_in_parent);
    sd_bus_message_append(reply, "i", childcnt);
    sd_bus_message_open_container(reply, 'a', "s");
    sd_bus_message_append(reply, "s", ATSPI_ACCESSIBLE_INTERFACE);
    sd_bus_message_append(reply, "s", ATSPI_COMPONENT_INTERFACE);
    if (acc->role == ATSPI_ROLE_PUSH_BUTTON || acc->role == ATSPI_ROLE_CHECK_BOX ||
        acc->role == ATSPI_ROLE_RADIO_BUTTON || acc->role == ATSPI_ROLE_TOGGLE_BUTTON) {
        sd_bus_message_append(reply, "s", ATSPI_ACTION_INTERFACE);
    } else if (acc->role == ATSPI_ROLE_ENTRY || acc->role == ATSPI_ROLE_TEXT) {
        sd_bus_message_append(reply, "s", ATSPI_TEXT_INTERFACE);
    } else if (acc->role == ATSPI_ROLE_SPIN_BUTTON || acc->role == ATSPI_ROLE_SLIDER ||
               acc->role == ATSPI_ROLE_PROGRESS_BAR || acc->role == ATSPI_ROLE_SCROLL_BAR) {
        sd_bus_message_append(reply, "s", ATSPI_VALUE_INTERFACE);
    } else if (acc->role == ATSPI_ROLE_LIST_BOX || acc->role == ATSPI_ROLE_TREE ||
               acc->role == ATSPI_ROLE_TREE_TABLE) {
        sd_bus_message_append(reply, "s", ATSPI_SELECTION_INTERFACE);
    }
    sd_bus_message_close_container(reply);
    const char *nm = acc->cached_name ? acc->cached_name : (acc->path ? acc->path : "");
    const char *ds = acc->cached_description ? acc->cached_description : "";
    sd_bus_message_append(reply, "s", nm);
    sd_bus_message_append(reply, "u", (uint32_t)(acc->role ? acc->role : ATSPI_ROLE_INVALID));
    sd_bus_message_append(reply, "s", ds);
    sd_bus_message_open_container(reply, 'a', "u");
    uint64_t states = acc->states;
    uint32_t slo = (uint32_t)(states & 0xffffffffu);
    uint32_t shi = (uint32_t)((states >> 32) & 0xffffffffu);
    sd_bus_message_append(reply, "u", slo);
    sd_bus_message_append(reply, "u", shi);
    sd_bus_message_close_container(reply);
    sd_bus_message_close_container(reply);
    if (acc->tkwin && !acc->is_virtual) {
        int emit_idx = 0;
        int _emitIter = 0;
        TkWindow *_slow2 = ((TkWindow*)acc->tkwin)->childList;
        TkWindow *_fast2 = _slow2 ? _slow2->nextPtr : NULL;
        for (TkWindow *c = ((TkWindow*)acc->tkwin)->childList; c; c = c->nextPtr) {
            if (_fast2 && _slow2 && _fast2 == _slow2) break;
            if (_emitIter > 10000) break;
            TkAccessible *child_acc = GetAccessible((Tk_Window)c);
            if (child_acc) {
                AppendCacheItem(reply, child_acc, acc, app_path, emit_idx);
                emit_idx++;
            }
            _emitIter++;
            if (_emitIter % 2 == 0) {
                _slow2 = _slow2 ? _slow2->nextPtr : NULL;
                _fast2 = _fast2 ? _fast2->nextPtr : NULL;
                if (_fast2) _fast2 = _fast2->nextPtr;
            }
        }
    } else if (acc->children) {
        int idx = 0;
        for (AccessibleList *l = acc->children; l; l = l->next, idx++) {
            if (l->acc) AppendCacheItem(reply, l->acc, acc, app_path, idx);
        }
    }
}


/*
 *----------------------------------------------------------------------
 * dbus_method_cache_get_items --
 *
 *   D-Bus method handler for GetItems on the Cache interface.
 *   Returns a list of all accessible objects in the cache tree
 *   including the root application and all toplevel windows.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Appends an array of cache items (each containing accessible reference,
 *   application, parent, child count, index, interfaces, name, role, states)
 *   to the D-Bus reply message.
 *----------------------------------------------------------------------
 */

static int
dbus_method_cache_get_items(
    sd_bus_message *m,      /* D-Bus method call message. */
    void *userdata,         /* Unused. */
    sd_bus_error *ret_error)/* Error object. */
{
    DEBUG_LOG("dbus_method_cache_get_items: enter");
    sd_bus_message *reply = NULL;
    int r;
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;

    sd_bus_message_open_container(reply, 'a', "((so)(so)(so)iiassusau)");

    if (atspi_conn && atspi_conn->root_accessible && atspi_conn->root_accessible->dbus_path) {
        TkAccessible *root = atspi_conn->root_accessible;
        const char *app_path = root->dbus_path;

        for (AccessibleList *l=atspi_conn->toplevel_accessibles; l; l=l->next) {
            TkAccessible *top=l->acc;
            if (top && top->tkwin) {
                EnsureChildrenRegisteredEx(top->tkwin, 0);
            }
        }

        int topcount=0; for (AccessibleList *l=atspi_conn->toplevel_accessibles; l; l=l->next) topcount++;

        sd_bus_message_open_container(reply, 'r', "(so)(so)(so)iiassusau");
        AppendAccessibleRef(reply, root->dbus_path);
        /*
         * Field order per the AT-SPI2 Cache spec is
         * (accessible)(application)(parent).
	 */
        AppendAccessibleRef(reply, root->dbus_path);
        sd_bus_message_append(reply, "(so)", "", "/org/a11y/atspi/null");
        sd_bus_message_append(reply, "i", -1);
        sd_bus_message_append(reply, "i", topcount);
        sd_bus_message_open_container(reply, 'a', "s");
        /*
         * Must match exactly what RegisterDbusObject() registers on the
         * root accessible: Accessible and Component unconditionally, plus
         * Application since acc == atspi_conn->root_accessible. Cache is
         * NOT a per-object interface - it lives at its own fixed path
         * /org/a11y/atspi/cache and must not appear here.
         */
        sd_bus_message_append(reply, "s", ATSPI_ACCESSIBLE_INTERFACE);
        sd_bus_message_append(reply, "s", ATSPI_COMPONENT_INTERFACE);
        sd_bus_message_append(reply, "s", "org.a11y.atspi.Application");
        sd_bus_message_close_container(reply);
        sd_bus_message_append(reply, "s", "");
        sd_bus_message_append(reply, "u", (uint32_t)ATSPI_ROLE_APPLICATION);
        sd_bus_message_append(reply, "s", "");
        {
            uint64_t root_states=root->states;
            uint32_t rlo=(uint32_t)(root_states&0xffffffffu);
            uint32_t rhi=(uint32_t)((root_states>>32)&0xffffffffu);
            sd_bus_message_open_container(reply, 'a', "u");
            sd_bus_message_append(reply, "u", rlo);
            sd_bus_message_append(reply, "u", rhi);
            sd_bus_message_close_container(reply);
        }
        sd_bus_message_close_container(reply);

        {
            int toplevel_idx = 0;
            for (AccessibleList *l=atspi_conn->toplevel_accessibles; l; l=l->next) {
                TkAccessible *top=l->acc;
                if (!top||!top->dbus_path) continue;
                AppendCacheItem(reply, top, root, app_path, toplevel_idx);
                toplevel_idx++;
            }
        }
    }

    sd_bus_message_close_container(reply);
    return sd_bus_send(NULL, reply, NULL);
}

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
        acc->dbus_path = Tcl_Strdup(path);
        DEBUG_LOG("RegisterDbusObject: generated dbus_path=%s for widget path=%s",
                  acc->dbus_path, acc->path ? acc->path : "?");
    }

    /*
     * Register a single vtable and capture its slot in acc->vtable_slots
     * so it can be torn down later. Every sd_bus_add_object_vtable() call
     * below MUST go through this helper -- passing NULL for the slot
     * leaks a floating registration that outlives the object.
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
     * Register Application on the root and Cache at its own fixed path.
     * Cache is NOT a per-object interface on the root's path - it lives at
     * /org/a11y/atspi/cache. Application is required for Orca to catalog
     * us as an application.
     */
    if (acc == atspi_conn->root_accessible) {
        ADD_VTABLE(atspi_conn->bus, "/org/a11y/atspi/cache",
                   "org.a11y.atspi.Cache", cache_vtable);
        ADD_VTABLE(atspi_conn->bus, acc->dbus_path,
                   "org.a11y.atspi.Application", application_vtable);
    }

    /* Conditionally register other interfaces based on role. */
    int role = acc->role;
    if (role == ATSPI_ROLE_PUSH_BUTTON || role == ATSPI_ROLE_CHECK_BOX ||
        role == ATSPI_ROLE_RADIO_BUTTON || role == ATSPI_ROLE_TOGGLE_BUTTON) {
        ADD_VTABLE(atspi_conn->bus, acc->dbus_path,
                   ATSPI_ACTION_INTERFACE, action_vtable);
    }
    if (role == ATSPI_ROLE_SPIN_BUTTON || role == ATSPI_ROLE_SLIDER ||
        role == ATSPI_ROLE_PROGRESS_BAR || role == ATSPI_ROLE_SCROLL_BAR) {
        ADD_VTABLE(atspi_conn->bus, acc->dbus_path,
                   ATSPI_VALUE_INTERFACE, value_vtable);
    }
    if (role == ATSPI_ROLE_ENTRY || role == ATSPI_ROLE_TEXT) {
        ADD_VTABLE(atspi_conn->bus, acc->dbus_path,
                   ATSPI_TEXT_INTERFACE, text_vtable);
    }
    if (role == ATSPI_ROLE_LIST_BOX || role == ATSPI_ROLE_TREE ||
        role == ATSPI_ROLE_TREE_TABLE) {
        ADD_VTABLE(atspi_conn->bus, acc->dbus_path,
                   ATSPI_SELECTION_INTERFACE, selection_vtable);
    }

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
     * trailing a{sv} is a (normally empty) properties dict. This matches
     * current at-spi2-core/xml/Event.xml.
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
        EmitObjectEventFull(acc, "Focus", "", 0, 0, NULL);
    } else if (strcmp(event_type, ATSPI_EVENT_VALUE_CHANGED) == 0) {
        EmitObjectEventFull(acc, "PropertyChange", "accessible-value", 0, 0, NULL);
    } else if (strcmp(event_type, ATSPI_EVENT_TEXT_CHANGED) == 0) {
        EmitObjectEventFull(acc, "TextChanged", detail ? detail : "", 0, 0, NULL);
    } else if (strcmp(event_type, ATSPI_EVENT_SELECTION_CHANGED) == 0) {
        EmitObjectEventFull(acc, "SelectionChanged", "", 0, 0, NULL);
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

static void
SendActiveDescendantChanged(
    TkAccessible *container,    /* Container object. */
    TkAccessible *descendant)   /* New active descendant. */
{
    if (!container || !descendant) return;
    if (!container->dbus_path || !descendant->dbus_path) return;
    if (!atspi_conn || !atspi_conn->bus) return;

    DEBUG_LOG("SendActiveDescendantChanged: container=%s descendant=%s",
              container->path ? container->path : "?", descendant->path ? descendant->path : "?");
    /*
     * Wire format: member=ActiveDescendantChanged, type="" (bare, not
     * "object:active-descendant-changed" listener string). The child reference
     * is carried in the (so) variant. This matches how Focus is emitted.
     */
    EmitObjectEventFull(container, "ActiveDescendantChanged",
                        "", 0, 0, descendant);
}

/*
 *----------------------------------------------------------------------
 * SetAccessibleFocus --
 *
 *   Set the focus state for an accessible object and emit the appropriate
 *   events. This is the authoritative source for focus state changes.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Updates the object's focus state and emits D-Bus focus events.
 *----------------------------------------------------------------------
 */

static void
SetAccessibleFocus(
    TkAccessible *acc,      /* Accessible object. */
    int focused)            /* New focus state (0 or 1). */
{
    if (!acc) return;

    uint64_t old_states = acc->states;
    acc->is_focused = focused;
    acc->states = ComputeStateForWidget(acc);

    if ((old_states & ATSPI_STATE_FOCUSED) != (acc->states & ATSPI_STATE_FOCUSED)) {
        SendStateChanged(acc, ATSPI_STATE_FOCUSED, focused);
        SendAtspiEvent(acc, ATSPI_EVENT_FOCUS, NULL);
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
 *   Guarded against re-entrancy: a dispatched AT-SPI method call (e.g.
 *   Orca invoking GrabFocus on one of our accessible objects) runs from
 *   inside sd_bus_process, and dbus_method_grab_focus calls back into Tk
 *   via Tcl_Eval("focus -force ..."), which can synchronously trigger
 *   ::tk::accessible::emit_focus_change and further AT-SPI traffic.
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
        /* drain all pending messages */
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
    acc->path = Tcl_Strdup(path ? path : Tk_PathName(tkwin));
    acc->role = GetRoleForWidget(tkwin);
    acc->ref_count = 1;
    acc->states = ComputeStateForWidget(acc);
    acc->cached_name = GetNameForWidget(tkwin);
    acc->cached_description = GetDescriptionForWidget(tkwin);

    DEBUG_LOG("CreateAccessible: path=%s role=%d (%s) class=%s name='%s' desc='%s'",
              acc->path ? acc->path : "?",
              acc->role, RoleToString(acc->role),
              tkwin ? Tk_Class(tkwin) : "?",
              acc->cached_name ? acc->cached_name : "(null)",
              acc->cached_description ? acc->cached_description : "(null)");

    if (!RegisterDbusObject(acc)) {
        DEBUG_LOG("CreateAccessible: RegisterDbusObject failed for %s, aborting creation", acc->path);
        if (acc->path) Tcl_Free(acc->path);
        if (acc->cached_name) Tcl_Free(acc->cached_name);
        if (acc->cached_description) Tcl_Free(acc->cached_description);
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

    if (acc->path) Tcl_Free(acc->path);
    if (acc->cached_name) Tcl_Free(acc->cached_name);
    if (acc->cached_description) Tcl_Free(acc->cached_description);
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
 * UpdateFocusChain --
 *
 *   Update the focus state for a window and its ancestors.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Sends focus events.
 *----------------------------------------------------------------------
 */

static void
UpdateFocusChain(
    Tk_Window focused)      /* Window that now has focus. */
{
    if (!focused) return;

    Tcl_Interp *interp = Tk_Interp(focused);
    if (!interp) return;

    EnsureAccessibleInHierarchy(interp, focused);

    TkAccessible *focused_acc = GetAccessible(focused);
    if (!focused_acc) return;

    SetAccessibleFocus(focused_acc, 1);
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
                            DEBUG_LOG("GetRoleForWidget: path=%s explicit role attr '%s' -> %d (%s)", Tk_PathName(tkwin), result, roleMap[i].atspi_role, RoleToString(roleMap[i].atspi_role));
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
                DEBUG_LOG("GetRoleForWidget: path=%s class='%s' -> %d (%s)", Tk_PathName(tkwin), widgetClass, roleMap[i].atspi_role, RoleToString(roleMap[i].atspi_role));
                return roleMap[i].atspi_role;
            }
        }
        DEBUG_LOG("GetRoleForWidget: path=%s class='%s' no match in roleMap", Tk_PathName(tkwin), widgetClass);
    }

    if (Tk_IsTopLevel(tkwin)) {
        DEBUG_LOG("GetRoleForWidget: path=%s is toplevel -> window", Tk_PathName(tkwin));
        return ATSPI_ROLE_WINDOW;
    }

    DEBUG_LOG("GetRoleForWidget: path=%s -> INVALID", Tk_PathName(tkwin));
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
    if (!is_disabled && acc->interp) {
        char *stateStr = TryCgetString(acc->tkwin, "-state");
        if (stateStr) {
            if (strcmp(stateStr, "disabled") == 0 || strcmp(stateStr, "readonly") == 0) {
                if (strcmp(stateStr, "disabled") == 0) is_disabled = 1;
            }
            Tcl_Free(stateStr);
        }
    }

    if (!is_disabled) {
        states |= ATSPI_STATE_ENABLED;
        states |= ATSPI_STATE_SENSITIVE;
    }

    /* Focusable based on role. */
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

    /* Focused. */
    if (acc->is_focused) {
        states |= ATSPI_STATE_FOCUSED;
    }

    /*
     * Active applies to the toplevel window itself, not individual
     * widgets -- it mirrors is_focused for a WINDOW/FRAME-role accessible.
     */
    if ((role == ATSPI_ROLE_WINDOW || role == ATSPI_ROLE_FRAME) && acc->is_focused) {
        states |= ATSPI_STATE_ACTIVE;
    }

    /* Visible/Showing. */
    if (Tk_IsMapped(acc->tkwin)) {
        states |= ATSPI_STATE_VISIBLE;
        states |= ATSPI_STATE_SHOWING;
    }

    /* Editable for entries. */
    if (role == ATSPI_ROLE_ENTRY || role == ATSPI_ROLE_TEXT) {
        if (!is_disabled) {
            int is_editable = 1;
            char *st = TryCgetString(acc->tkwin, "-state");
            if (st) {
                if (strcmp(st, "readonly") == 0 || strcmp(st, "disabled") == 0) is_editable = 0;
                Tcl_Free(st);
            }
            if (is_editable) states |= ATSPI_STATE_EDITABLE;
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
                Tcl_Free(value);
            }
        }
    }

    return states;
}

/*
 *----------------------------------------------------------------------
 * TryCgetString --
 *
 *   Try to get a widget option as a string.
 *
 * Results:
 *   Returns a newly allocated string with the option value, or NULL
 *   if the option is not set.
 *
 * Side effects:
 *   Memory is allocated via Tcl_Strdup.
 *----------------------------------------------------------------------
 */

static char *
TryCgetString(
    Tk_Window tkwin,        /* Tk widget. */
    const char *option)     /* Option name. */
{
    if (!tkwin || !option) return NULL;
    Tcl_Interp *interp = Tk_Interp(tkwin);
    if (!interp) return NULL;
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s cget %s", Tk_PathName(tkwin), option);
    if (Tcl_Eval(interp, cmd) != TCL_OK) {
        Tcl_ResetResult(interp);
        return NULL;
    }
    const char *res = Tcl_GetStringResult(interp);
    if (!res || res[0] == '\0') {
        Tcl_ResetResult(interp);
        return NULL;
    }
    char *dup = Tcl_Strdup(res);
    Tcl_ResetResult(interp);
    return dup;
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

static char *
GetNameForWidget(
    Tk_Window tkwin)        /* Tk widget to get name for. */
{
    if (!tkwin) return NULL;

    const char *widgetClass = Tk_Class(tkwin);
    const char *pathName = Tk_PathName(tkwin);

    /* Helper: is a name generic (equals class or role name like Button, TButton)? */
    auto int IsGenericName(const char *nm) {
        if (!nm || !nm[0]) return 1;
        if (widgetClass && strcasecmp(nm, widgetClass) == 0) return 1;
        /* role names list */
        const char *generics[] = {"Button","TButton","Checkbutton","Radiobutton","Label","Entry","TLabel","Frame","TFrame","Toplevel",NULL};
        for (int i=0; generics[i]; i++) {
            if (strcasecmp(nm, generics[i])==0) return 1;
        }
        return 0;
    }

    /* Try widget's -text / -label first - this is what Orca actually needs for buttons. */
    char *s = NULL;
    s = TryCgetString(tkwin, "-text");
    if (s && s[0]) {
        if (!IsGenericName(s)) {
            DEBUG_LOG("GetNameForWidget: path=%s -text='%s' (primary)", pathName, s);
            return s;
        }
        /* Generic text like 'Button' - drop and continue. */
        DEBUG_LOG("GetNameForWidget: path=%s -text='%s' ignored as generic", pathName, s);
        Tcl_Free(s);
    } else if (s) { Tcl_Free(s); }

    s = TryCgetString(tkwin, "-label");
    if (s && s[0] && !IsGenericName(s)) { DEBUG_LOG("GetNameForWidget: path=%s -label='%s'", pathName, s); return s; }
    if (s) Tcl_Free(s);

    /* Explicit accessibility name, but ignore if generic. */
    if (TkAccessibilityObject) {
        Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)tkwin);
        if (hPtr) {
            Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
            if (attrs) {
                Tcl_HashEntry *nameEntry = Tcl_FindHashEntry(attrs, "name");
                if (nameEntry) {
                    Tcl_Obj *obj = (Tcl_Obj *)Tcl_GetHashValue(nameEntry);
                    if (obj) {
                        const char *name = Tcl_GetString(obj);
                        if (name && name[0] && !IsGenericName(name)) {
                            DEBUG_LOG("GetNameForWidget: path=%s explicit a11y name='%s'", pathName, name);
                            return Tcl_Strdup(name);
                        } else if (name && name[0]) {
                            DEBUG_LOG("GetNameForWidget: path=%s explicit a11y name='%s' ignored as generic", pathName, name);
                        }
                    }
                }
                /* Fallback: if description holds the real label,  use it. */
                Tcl_HashEntry *descEntry = Tcl_FindHashEntry(attrs, "description");
                if (descEntry) {
                    Tcl_Obj *obj = (Tcl_Obj *)Tcl_GetHashValue(descEntry);
                    if (obj) {
                        const char *desc = Tcl_GetString(obj);
                        if (desc && desc[0] && !IsGenericName(desc)) {
                            DEBUG_LOG("GetNameForWidget: path=%s using description as name fallback='%s'", pathName, desc);
                            return Tcl_Strdup(desc);
                        }
                    }
                }
            }
        }
    }

    int role = GetRoleForWidget(tkwin);
    if (role == ATSPI_ROLE_LABEL) {
        char *v = GetValueForWidget(tkwin);
        if (v && v[0]) return v;
        if (v) Tcl_Free(v);
    }

    /* Other fallbacks. */
    s = TryCgetString(tkwin, "-title");
    if (s && s[0]) { DEBUG_LOG("GetNameForWidget: path=%s -title='%s'", pathName, s); return s; }
    if (s) Tcl_Free(s);
    s = TryCgetString(tkwin, "-value");
    if (s && s[0]) { DEBUG_LOG("GetNameForWidget: path=%s -value='%s'", pathName, s); return s; }
    if (s) Tcl_Free(s);

    DEBUG_LOG("GetNameForWidget: path=%s no name found (class=%s)", pathName, widgetClass ? widgetClass : "?");

    /* For toplevel, try WM title, then Tk class, then path as last resort. */
    if (Tk_IsTopLevel(tkwin)) {
        /* Try wm title via Tcl eval - most reliable for toplevels.
         * Get interp from the window itself; Tk_Interp returns the interp that created it.
         */
        Tcl_Interp *interp = NULL;
        if (tkwin) {
            interp = Tk_Interp((Tk_Window)tkwin);
            if (!interp && atspi_conn && atspi_conn->root_accessible && atspi_conn->root_accessible->interp) {
                interp = atspi_conn->root_accessible->interp;
            }
        }
        if (interp) {
            Tcl_Obj *cmd = Tcl_ObjPrintf("wm title %s", pathName);
            Tcl_IncrRefCount(cmd);
            if (Tcl_EvalObjEx(interp, cmd, 0) == TCL_OK) {
                const char *t = Tcl_GetStringResult(interp);
                if (t && t[0] && strcmp(t, ".") != 0) {
                    char *ret = Tcl_Strdup(t);
                    Tcl_DecrRefCount(cmd);
                    Tcl_ResetResult(interp);
                    DEBUG_LOG("GetNameForWidget: path=%s wm title='%s'", pathName, ret);
                    return ret;
                }
                Tcl_ResetResult(interp);
            }
            Tcl_DecrRefCount(cmd);
        }
        /* Fallback to Tk class name. */
        if (widgetClass && widgetClass[0]) {
            return Tcl_Strdup(widgetClass);
        }
        const char *pn = Tk_PathName(tkwin);
        if (pn && pn[0] && strcmp(pn, ".") != 0) return Tcl_Strdup(pn);
        /* Last resort for "." root - use "Tk" */
        if (pn && strcmp(pn, ".") == 0) {
            return Tcl_Strdup("Tk Application");
        }
        if (pn && pn[0]) return Tcl_Strdup(pn);
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
 *   Memory is allocated via Tcl_Strdup.
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
                        if (value && value[0]) return Tcl_Strdup(value);
                    }
                }
            }
        }
    }
    /* Fallback to Tk options. */
    char *s = TryCgetString(tkwin, "-text");
    if (s) return s;
    s = TryCgetString(tkwin, "-value");
    if (s) return s;
    s = TryCgetString(tkwin, "-label");
    if (s) return s;
    return NULL;
}

/*
 *----------------------------------------------------------------------
 * EnsureChildrenRegisteredRecursive --
 *
 *   Recursively ensure that all children of a window have accessible
 *   objects registered.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Creates TkAccessible objects for missing child windows and emits
 *   children-changed events.
 *----------------------------------------------------------------------
 */

static void
EnsureChildrenRegisteredRecursiveEx(
    Tk_Window tkwin,
    TkAccessible *parent_acc,
    int emitEvents)
{
    if (!tkwin) return;
    TkWindow *winPtr = (TkWindow *)tkwin;
    if (!winPtr) return;
    int idx = 0;
    int _iter = 0;
    TkWindow *_slow = winPtr->childList;
    TkWindow *_fast = _slow ? _slow->nextPtr : NULL;
    for (TkWindow *childPtr = winPtr->childList; childPtr; childPtr = childPtr->nextPtr, idx++) {
        if (_fast && _slow && _fast == _slow) {
            DEBUG_LOG("EnsureChildrenRegistered: CYCLE DETECTED at iter=%d -- BREAKING", _iter);
            break;
        }
        if (_iter > 10000) {
            DEBUG_LOG("EnsureChildrenRegistered: CAP EXCEEDED -- BREAKING");
            break;
        }
        _iter++;
        if (_iter % 2 == 0) {
            _slow = _slow ? _slow->nextPtr : NULL;
            _fast = _fast ? _fast->nextPtr : NULL;
            if (_fast) _fast = _fast->nextPtr;
        }
        Tk_Window childWin = (Tk_Window)childPtr;
        TkAccessible *child_acc = GetAccessible(childWin);
        if (!child_acc) {
            Tcl_Interp *interp = Tk_Interp(tkwin);
            if (!interp && parent_acc) interp = parent_acc->interp;
            if (!interp) continue;
            child_acc = CreateAccessible(interp, childWin, Tk_PathName(childWin));
            if (!child_acc) continue;
            if (parent_acc) child_acc->parent = parent_acc;
            RegisterAccessible(childWin, child_acc);
            TkAccessible_RegisterEventHandlers(childWin, child_acc);
            if (parent_acc && emitEvents) {
                SendChildrenChanged(parent_acc, idx, child_acc, 1);
            }
        }
        EnsureChildrenRegisteredRecursiveEx(childWin, child_acc, emitEvents);
    }
}

static void
EnsureChildrenRegisteredRecursive(
    Tk_Window tkwin,
    TkAccessible *parent_acc)
{
    EnsureChildrenRegisteredRecursiveEx(tkwin, parent_acc, 1);
}


/*
 *----------------------------------------------------------------------
 * EnsureChildrenRegistered --
 *
 *   Ensure that all children of a window have accessible objects registered.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Creates TkAccessible objects for missing child windows.
 *----------------------------------------------------------------------
 */

static void
EnsureChildrenRegisteredEx(
    Tk_Window tkwin,
    int emitEvents)
{
    if (!tkwin) return;
    TkAccessible *acc = GetAccessible(tkwin);
    if (!acc) {
        Tk_Window parent = Tk_Parent(tkwin);
        if (parent) acc = GetAccessible(parent);
    }
    if (!acc) {
        Tk_Window top = tkwin;
        while (top && !Tk_IsTopLevel(top)) top = Tk_Parent(top);
        if (top) acc = GetAccessible(top);
    }
    if (!acc) return;
    Tk_Window scanWin = acc->tkwin ? acc->tkwin : tkwin;
    EnsureChildrenRegisteredRecursiveEx(scanWin, acc, emitEvents);
}

static void
EnsureChildrenRegistered(
    Tk_Window tkwin)
{
    EnsureChildrenRegisteredEx(tkwin, 1);
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
     * accessible is unregistered/freed below. Without this, a FocusOut
     * (or ConfigureNotify) generated for the same window during teardown
     * can still be dispatched to TkAccessible_FocusHandler/
     * TkAccessible_ConfigureHandler with a stale 'acc' pointer that
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
 *   This is the authoritative source for focus state changes from real
 *   X focus events. It uses SetAccessibleFocus() to update state and
 *   emit events.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Updates focus state and sends focus events.
 *----------------------------------------------------------------------
 */

static void
TkAccessible_FocusHandler(
    void *clientData,       /* TkAccessible object pointer. */
    XEvent *eventPtr)       /* X event structure. */
{
    TkAccessible *acc = (TkAccessible *)clientData;
    if (!acc || !acc->tkwin) return;

    int focused = (eventPtr->type == FocusIn);
    DEBUG_LOG("TkAccessible_FocusHandler: path=%s eventPtr->type=%d focused=%d",
              acc->path ? acc->path : "?", eventPtr->type, focused);

    /* Use the authoritative focus setter. */
    SetAccessibleFocus(acc, focused);

    /* 
     * ActiveDescendantChanged is ONLY for composite controls (menus, lists,
     * trees, combo boxes). Ordinary widgets (buttons, entries) should NOT
     * emit ActiveDescendantChanged to their window parent.
     */
    if (focused && acc->parent) {
        int prow = acc->parent->role;
        if (prow == ATSPI_ROLE_MENU ||
            prow == ATSPI_ROLE_MENU_BAR ||
            prow == ATSPI_ROLE_LIST_BOX ||
            prow == ATSPI_ROLE_TREE ||
            prow == ATSPI_ROLE_TREE_TABLE ||
            prow == ATSPI_ROLE_COMBO_BOX) {
            DEBUG_LOG("TkAccessible_FocusHandler: ActiveDescendantChanged parent=%s child=%s",
                      acc->parent->path ? acc->parent->path : "?",
                      acc->path ? acc->path : "?");
            SendActiveDescendantChanged(acc->parent, acc);
        }
    }

    /* Emit window:activate/deactivate for toplevel so Orca knows active window */
    if (Tk_IsTopLevel(acc->tkwin)) {
        if (focused) {
            SendAtspiEvent(acc, ATSPI_EVENT_WINDOW_ACTIVATE, NULL);
            SendStateChanged(acc, ATSPI_STATE_ACTIVE, 1);
        } else {
            SendAtspiEvent(acc, ATSPI_EVENT_WINDOW_DEACTIVATE, NULL);
            SendStateChanged(acc, ATSPI_STATE_ACTIVE, 0);
        }
    }

    /* If we never successfully embedded with registry, retry now. */
    if (!atspi_conn->is_embedded) {
        DEBUG_LOG("TkAccessible_FocusHandler: not embedded, retrying EmbedWithRegistry");
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
            /* If not found via filtered count (new child not yet in map), fall back to raw filtered position */
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

    /* Refresh cached name/description. */
    {
        char *newName = GetNameForWidget(tkwin);
        if (newName) {
            if (acc->cached_name) Tcl_Free(acc->cached_name);
            acc->cached_name = newName;
        }
        char *newDesc = GetDescriptionForWidget(tkwin);
        if (newDesc) {
            if (acc->cached_description) Tcl_Free(acc->cached_description);
            acc->cached_description = newDesc;
        }
    }
    DEBUG_LOG("ConfigureHandler: path=%s new size %dx%d name='%s' - scanning for new children (hash table walk)",
              acc->path?acc->path:"?", acc->width, acc->height,
              acc->cached_name?acc->cached_name:"(null)");
    EnsureChildrenRegistered(tkwin);
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
    /* Give application a meaningful name for Orca - otherwise it appears nameless and may be filtered. */
    atspi_conn->root_accessible->cached_name = Tcl_Strdup("Tk");
    atspi_conn->root_accessible->cached_description = Tcl_Strdup("Tk Application");

    RegisterDbusObject(atspi_conn->root_accessible);

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
        atspi_conn->desktop_bus_name = Tcl_Strdup(desktop_name);
        atspi_conn->desktop_path = Tcl_Strdup(desktop_path);
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
 *   Sends a D-Bus event signal.
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

    int role = GetRoleForWidget(tkwin);
    if (role == ATSPI_ROLE_CHECK_BOX || role == ATSPI_ROLE_RADIO_BUTTON) {
        SendStateChanged(acc, ATSPI_STATE_CHECKED, 1);
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
 *   This command is used for logical Tk focus changes (e.g., menu
 *   selection via arrow keys) that do not generate real X FocusIn events.
 *   It uses SetAccessibleFocus() for consistent state management.
 *
 * Results:
 *   Returns TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *   Sends a D-Bus event signal.
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
     * command exists for cases where the Tcl layer drives "focus" itself
     * without a corresponding X event - most notably active menu entries,
     * which change via <<MenuSelect>>/arrow-key navigation rather than
     * real window focus.
     *
     * Use SetAccessibleFocus() for consistent state management.
     */
    SetAccessibleFocus(acc, 1);

    /*
     * For composite containers (menu, listbox, tree, etc.), notify the
     * parent of the new active descendant.
     */
    if (acc->parent) {
        int prow = acc->parent->role;
        if (prow == ATSPI_ROLE_MENU || prow == ATSPI_ROLE_MENU_BAR ||
            prow == ATSPI_ROLE_LIST_BOX || prow == ATSPI_ROLE_TREE ||
            prow == ATSPI_ROLE_TREE_TABLE || prow == ATSPI_ROLE_COMBO_BOX) {
            SendActiveDescendantChanged(acc->parent, acc);
        }
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
    DEBUG_LOG("BUILD-CHECK: ROLE_WINDOW=%d ROLE_PUSH_BUTTON=%d ROLE_APPLICATION=%d "
              "STATE_VISIBLE_BIT=%d STATE_SHOWING_BIT=%d STATE_ENABLED_BIT=%d "
              "(expect 69 43 75 30 25 8 -- if you see anything else, this binary "
              "does NOT contain the role/state fix)",
              ATSPI_ROLE_WINDOW, ATSPI_ROLE_PUSH_BUTTON, ATSPI_ROLE_APPLICATION,
              __builtin_ctzll(ATSPI_STATE_VISIBLE), __builtin_ctzll(ATSPI_STATE_SHOWING),
              __builtin_ctzll(ATSPI_STATE_ENABLED));
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
