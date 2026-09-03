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
 
/* Debugging. */
#define DEBUG_CHANNEL stdout
#define DEBUG_LABEL "at-spi"

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
#define ATSPI_EVENT_INTERFACE     "org.a11y.atspi.Event"

/* at-spi D-Bus paths. */
#define ATSPI_DBUS_PATH_REGISTRY  "/org/a11y/atspi/registry"
#define ATSPI_DBUS_PATH_ROOT      "/org/a11y/atspi/accessible/root"

/*
 * at-spi role constants.
 */
#define ATSPI_ROLE_INVALID           0
#define ATSPI_ROLE_FRAME             23
#define ATSPI_ROLE_APPLICATION       75

/* 
 * at-spi state constants.
 */
#define ATSPI_STATE_ENABLED          (1ULL << 8)
#define ATSPI_STATE_FOCUSABLE        (1ULL << 11)
#define ATSPI_STATE_SHOWING          (1ULL << 25)
#define ATSPI_STATE_VISIBLE          (1ULL << 30)

/*
 * Core structures.
 */

/* Main accessible object structure - simplified. */
typedef struct TkAccessible {
    Tk_Window tkwin;
    Tcl_Interp *interp;
    char *dbus_path;
    int role;
    uint64_t states;
    int32_t application_id;          /* Added: Application.Id from registry */
    char *cached_name;       /* Last name seen for announcement dedup */
    char *cached_description;
    char *cached_value;
    
    /* D-Bus slots for cleanup. */
#define TK_ACCESSIBLE_MAX_SLOTS 8
    sd_bus_slot *vtable_slots[TK_ACCESSIBLE_MAX_SLOTS];
    int n_vtable_slots;
} TkAccessible;

/* Global connection state. */
typedef struct {
    sd_bus *bus;
    int is_initialized;
    Tcl_HashTable *tk_to_accessible_map;   /* key = Tk_Window, value = TkAccessible* */
    TkAccessible *root_accessible;
    TkAccessible *toplevel_accessibles[256];
    int num_toplevels;

    /* Desktop reference from Socket.Embed. */
    char *desktop_bus_name;
    char *desktop_path;
    int is_embedded;
} AtspiConnection;

/*
 * Forward declarations.
 */

static void RegisterAccessible(Tk_Window tkwin, TkAccessible *acc);
static TkAccessible *GetAccessible(Tk_Window tkwin);
static void UnregisterAccessible(Tk_Window tkwin);
static void FreeAccessible(TkAccessible *acc);
static int GetLiveRole(TkAccessible *acc);
static uint64_t ComputeStateForWidget(TkAccessible *acc);
static char *GetNameForWidget(Tk_Window tkwin);
static char *GetDescriptionForWidget(Tk_Window tkwin);
static char *GetValueForWidget(Tk_Window tkwin);

/* D-Bus vtables. */
static const sd_bus_vtable accessible_vtable[];
static const sd_bus_vtable application_vtable[];
static const sd_bus_vtable cache_vtable[];

/* Accessible-reference helpers. */
static const char *SelfBusName(void);
static int AppendAccessibleRef(sd_bus_message *reply, const char *path);
static bool EmbedWithRegistry(void);

/* Event emission. */
static void PostAccessibilityAnnouncement(TkAccessible *acc, const char *message);
static void EmitObjectEventFull(TkAccessible *acc, const char *member, const char *type,
                                int32_t detail1, int32_t detail2, TkAccessible *related);

/* Focus handling. */
static void UpdateFocusChain(Tk_Window focused);

/* Event handlers. */
static void TkAccessible_FocusHandler(void *clientData, XEvent *eventPtr);
static void TkAccessible_DestroyHandler(void *clientData, XEvent *eventPtr);
static void TkAccessible_RegisterEventHandlers(Tk_Window tkwin, TkAccessible *acc);

/* Screen reader detection. */
static int IsScreenReaderActive(void);

/* Tcl command implementations. */
static int AddAccessibleCmd(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int EmitSelectionChangedCmd(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int EmitFocusChangedCmd(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int IsScreenReaderRunningCmd(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

/* D-Bus method handlers. */
static int dbus_method_get_role(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_children(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_child_at_index(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
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
static int dbus_method_cache_get_items(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);

/* Application interface property getters. */
static int dbus_prop_get_toolkit_name(sd_bus *bus, const char *path, const char *interface,
                                       const char *property, sd_bus_message *reply,
                                       void *userdata, sd_bus_error *ret_error);
static int dbus_prop_get_version(sd_bus *bus, const char *path, const char *interface,
                                  const char *property, sd_bus_message *reply,
                                  void *userdata, sd_bus_error *ret_error);
static int dbus_prop_get_toolkit_version(sd_bus *bus, const char *path, const char *interface,
                                          const char *property, sd_bus_message *reply,
                                          void *userdata, sd_bus_error *ret_error);
static int dbus_prop_get_atspi_version(sd_bus *bus, const char *path, const char *interface,
                                        const char *property, sd_bus_message *reply,
                                        void *userdata, sd_bus_error *ret_error);
static int dbus_prop_get_interface_version(sd_bus *bus, const char *path, const char *interface,
                                            const char *property, sd_bus_message *reply,
                                            void *userdata, sd_bus_error *ret_error);
static int dbus_prop_get_id(sd_bus *bus, const char *path, const char *interface,
                             const char *property, sd_bus_message *reply,
                             void *userdata, sd_bus_error *ret_error);
static int dbus_prop_set_id(sd_bus *bus, const char *path, const char *interface,
                             const char *property, sd_bus_message *value,
                             void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_locale(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_application_bus_address(sd_bus_message *m, void *userdata,
                                                    sd_bus_error *ret_error);

static AtspiConnection *atspi_conn = NULL;

/* Non-static handle to the AT-SPI bus. */
sd_bus *atspi_bus = NULL;

/* Re-entrancy guard. */
int atspi_draining = 0;

extern Tcl_HashTable *TkAccessibilityObject;  /* from tkAccessibility.c */

/*
 * D-Bus vtables.
 */

/*
 * org.a11y.atspi.Accessible interface - full version with hierarchy methods.
 */
static const sd_bus_vtable accessible_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Name", "s", dbus_prop_get_name, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Description", "s", dbus_prop_get_description, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Parent", "(so)", dbus_prop_get_parent, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ChildCount", "i", dbus_prop_get_child_count, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_METHOD("GetRole", "", "u", dbus_method_get_role, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetChildren", "", "a(so)", dbus_method_get_children, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetChildAtIndex", "i", "(so)", dbus_method_get_child_at_index, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

/*
 * org.a11y.atspi.Application interface - full version with Id property.
 */
static const sd_bus_vtable application_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("ToolkitName", "s", dbus_prop_get_toolkit_name, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Version", "s", dbus_prop_get_version, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ToolkitVersion", "s", dbus_prop_get_toolkit_version, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("AtspiVersion", "s", dbus_prop_get_atspi_version, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("InterfaceVersion", "u", dbus_prop_get_interface_version, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_WRITABLE_PROPERTY("Id", "i", dbus_prop_get_id, dbus_prop_set_id, 0, 0),
    SD_BUS_METHOD("GetLocale", "u", "s", dbus_method_get_locale, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetApplicationBusAddress", "", "s", dbus_method_get_application_bus_address, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

/*
 * org.a11y.atspi.Cache interface - minimal implementation returning empty list.
 */
static const sd_bus_vtable cache_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetItems", "", "a((so)(so)(so)iiassusau)", dbus_method_cache_get_items, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

/*
 *----------------------------------------------------------------------
 * SelfBusName --
 *
 *   Get our unique D-Bus name.
 *
 * Results:
 *   Returns static bus name string.
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
    DEBUG_LOG("SelfBusName: returning '%s'", name);
    return name;
}

/*
 *----------------------------------------------------------------------
 * AppendAccessibleRef --
 *
 *   Append an AT-SPI accessible reference ((so) tuple) to a message.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Appends data to the D-Bus message.
 *----------------------------------------------------------------------
 */

static int
AppendAccessibleRef(
    sd_bus_message *reply,
    const char *path)
{
    if (path && *path) {
        DEBUG_LOG("AppendAccessibleRef: appending (%s, %s)", SelfBusName(), path);
        return sd_bus_message_append(reply, "(so)", SelfBusName(), path);
    }
    DEBUG_LOG("AppendAccessibleRef: appending null reference");
    return sd_bus_message_append(reply, "(so)", "", "/org/a11y/atspi/null");
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_name --
 *
 *   D-Bus property getter for Name on the Accessible interface.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Appends the name string to the D-Bus reply message.
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
    char *live_name = NULL;
    
    if (!acc) {
        DEBUG_LOG("dbus_prop_get_name: no acc, returning empty");
        return sd_bus_message_append(reply, "s", "");
    }
    
    if (acc->cached_name && acc->cached_name[0] != '\0') {
        name = acc->cached_name;
        DEBUG_LOG("dbus_prop_get_name: using cached name '%s' for path %s", name, acc->dbus_path);
    } else if (acc->tkwin) {
        /* Only call GetNameForWidget if TkAccessibilityObject exists. */
        if (TkAccessibilityObject) {
            live_name = GetNameForWidget(acc->tkwin);
            if (live_name && live_name[0] != '\0') {
                name = live_name;
                DEBUG_LOG("dbus_prop_get_name: got live name '%s' for path %s", name, acc->dbus_path);
            }
        }
        if (!name || name[0] == '\0') {
            if (acc->dbus_path && strcmp(acc->dbus_path, "/org/a11y/atspi/accessible/root") == 0) {
                name = "Tk Application";
                DEBUG_LOG("dbus_prop_get_name: using default app name for root");
            } else if (acc->tkwin && Tk_IsTopLevel(acc->tkwin)) {
                /* Try wm title without Tcl_Eval to avoid reentrancy. */
                if (!atspi_draining) {
                    Tcl_Interp *interp = Tk_Interp(acc->tkwin);
                    if (interp) {
                        const char *path = Tk_PathName(acc->tkwin);
                        if (path) {
                            Tcl_Obj *cmd = Tcl_NewStringObj("wm title ", -1);
                            if (cmd) {
                                Tcl_Obj *pathObj = Tcl_NewStringObj(path, -1);
                                if (pathObj) {
                                    Tcl_AppendObjToObj(cmd, pathObj);
                                    Tcl_IncrRefCount(cmd);
                                    if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL) == TCL_OK) {
                                        const char *title = Tcl_GetStringResult(interp);
                                        if (title && title[0] != '\0') {
                                            name = title;
                                            DEBUG_LOG("dbus_prop_get_name: got wm title '%s' for path %s", name, acc->dbus_path);
                                        }
                                    }
                                    Tcl_DecrRefCount(cmd);
                                    Tcl_ResetResult(interp);
                                }
                            }
                        }
                    }
                }
            }
        }
    } else if (acc->dbus_path && strcmp(acc->dbus_path, "/org/a11y/atspi/accessible/root") == 0) {
        name = "Tk Application";
        DEBUG_LOG("dbus_prop_get_name: using default app name for root");
    }
    
    int ret = sd_bus_message_append(reply, "s", name ? name : "");
    if (live_name) free(live_name);
    DEBUG_LOG("dbus_prop_get_name: returning '%s' for path %s", name ? name : "", acc ? acc->dbus_path : "null");
    return ret;
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_description --
 *
 *   D-Bus property getter for Description on the Accessible interface.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Appends the description string to the D-Bus reply message.
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
    char *live_desc = NULL;
    
    if (acc && acc->tkwin) {
        live_desc = GetDescriptionForWidget(acc->tkwin);
        if (live_desc) desc = live_desc;
        DEBUG_LOG("dbus_prop_get_description: got '%s' for path %s", desc, acc->dbus_path);
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
 *   For the application root, returns the null parent ("" /org/a11y/atspi/null).
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
        DEBUG_LOG("dbus_prop_get_parent: no acc/conn, returning null");
        return AppendAccessibleRef(reply, NULL);
    }
    
    /* Root has null parent (desktop is above the application). */
    if (acc == atspi_conn->root_accessible) {
        DEBUG_LOG("dbus_prop_get_parent: root has null parent");
        return AppendAccessibleRef(reply, NULL);
    }
    
    /* Toplevels parent is root. */
    if (acc->tkwin && Tk_IsTopLevel(acc->tkwin) && atspi_conn->root_accessible) {
        DEBUG_LOG("dbus_prop_get_parent: toplevel parent is root for path %s", acc->dbus_path);
        return AppendAccessibleRef(reply, atspi_conn->root_accessible->dbus_path);
    }
    
    DEBUG_LOG("dbus_prop_get_parent: returning null for path %s", acc->dbus_path);
    return AppendAccessibleRef(reply, NULL);
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_child_count --
 *
 *   D-Bus property getter for ChildCount on the Accessible interface.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Appends an integer count to the D-Bus reply message.
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
        DEBUG_LOG("dbus_prop_get_child_count: no acc/conn, returning 0");
        return sd_bus_message_append(reply, "i", 0);
    }
    
    if (acc == atspi_conn->root_accessible) {
        cnt = atspi_conn->num_toplevels;
        DEBUG_LOG("dbus_prop_get_child_count: root has %d children", cnt);
    }
    
    return sd_bus_message_append(reply, "i", cnt);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_role --
 *
 *   D-Bus method handler for GetRole on the Accessible interface.
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
    sd_bus_message *m,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int role = acc ? GetLiveRole(acc) : ATSPI_ROLE_INVALID;
    DEBUG_LOG("dbus_method_get_role: returning role %d for path %s", role, acc ? acc->dbus_path : "null");
    return sd_bus_reply_method_return(m, "u", (uint32_t)role);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_children --
 *
 *   D-Bus method handler for GetChildren on the Accessible interface.
 *   Returns all toplevel children of the application root.
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
    sd_bus_message *m,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    TkAccessible *acc = (TkAccessible *)userdata;

    if (!acc || !atspi_conn) {
        DEBUG_LOG("dbus_method_get_children: no acc/conn, returning empty");
        return sd_bus_reply_method_return(m, "a(so)", 0);
    }

    DEBUG_LOG("dbus_method_get_children: called for path %s", acc->dbus_path);

    int r = sd_bus_message_open_container(m, 'a', "(so)");
    if (r < 0) {
        DEBUG_LOG("dbus_method_get_children: failed to open container: %d", r);
        return r;
    }

    if (acc == atspi_conn->root_accessible) {
        DEBUG_LOG("dbus_method_get_children: root has %d toplevels", atspi_conn->num_toplevels);
        for (int i = 0; i < atspi_conn->num_toplevels; i++) {
            TkAccessible *child = atspi_conn->toplevel_accessibles[i];

            if (!child || !child->dbus_path) {
                DEBUG_LOG("dbus_method_get_children: child %d is null or has no path", i);
                continue;
            }

            DEBUG_LOG("dbus_method_get_children: appending child %d: (%s, %s)", i, SelfBusName(), child->dbus_path);
            r = sd_bus_message_append(m, "(so)", SelfBusName(), child->dbus_path);
            if (r < 0) {
                DEBUG_LOG("dbus_method_get_children: failed to append child: %d", r);
                sd_bus_message_close_container(m);
                return r;
            }
        }
    }

    return sd_bus_message_close_container(m);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_child_at_index --
 *
 *   D-Bus method handler for GetChildAtIndex on the Accessible interface.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an (so) accessible reference.
 *----------------------------------------------------------------------
 */

static int
dbus_method_get_child_at_index(
    sd_bus_message *m,
    void *userdata,
    sd_bus_error *ret_error)
{
    TkAccessible *acc = (TkAccessible *)userdata;
    int32_t index;

    if (!acc || !atspi_conn) {
        DEBUG_LOG("dbus_method_get_child_at_index: no acc/conn, returning null");
        return sd_bus_reply_method_return(m, "(so)", "", "/org/a11y/atspi/null");
    }

    int r = sd_bus_message_read(m, "i", &index);
    if (r < 0) {
        DEBUG_LOG("dbus_method_get_child_at_index: failed to read index: %d", r);
        return r;
    }

    DEBUG_LOG("dbus_method_get_child_at_index: called for path %s, index %d", acc->dbus_path, index);

    if (acc == atspi_conn->root_accessible) {
        if (index < 0 || index >= atspi_conn->num_toplevels ||
            !atspi_conn->toplevel_accessibles[index]) {

            DEBUG_LOG("dbus_method_get_child_at_index: index %d out of range (max %d)", index, atspi_conn->num_toplevels);
            sd_bus_error_set_const(ret_error, SD_BUS_ERROR_INVALID_ARGS,
                                   "Child index out of range");
            return -EINVAL;
        }

        TkAccessible *child = atspi_conn->toplevel_accessibles[index];
        DEBUG_LOG("dbus_method_get_child_at_index: returning child at index %d: (%s, %s)", index, SelfBusName(), child->dbus_path);

        return sd_bus_reply_method_return(m, "(so)", SelfBusName(), child->dbus_path);
    }

    DEBUG_LOG("dbus_method_get_child_at_index: not root, returning null");
    return sd_bus_reply_method_return(m, "(so)", "", "/org/a11y/atspi/null");
}

/*
 *----------------------------------------------------------------------
 * dbus_method_cache_get_items --
 *
 *   D-Bus method handler for GetItems on the Cache interface.
 *   Returns an empty list since we don't maintain a cache.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an empty array.
 *----------------------------------------------------------------------
 */

static int
dbus_method_cache_get_items(
    sd_bus_message *m,
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    DEBUG_LOG("dbus_method_cache_get_items: returning empty cache");
    return sd_bus_reply_method_return(m, "a((so)(so)(so)iiassusau)", 0);
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_toolkit_name --
 *
 *   D-Bus property getter for ToolkitName on the Application interface.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
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
    DEBUG_LOG("dbus_prop_get_toolkit_name: returning 'Tk'");
    return sd_bus_message_append(reply, "s", "Tk");
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_version --
 *
 *   D-Bus property getter for Version on the Application interface.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
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
    DEBUG_LOG("dbus_prop_get_version: returning '%s'", TK_VERSION);
    return sd_bus_message_append(reply, "s", TK_VERSION);
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_toolkit_version --
 *
 *   D-Bus property getter for ToolkitVersion on the Application interface.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Appends the toolkit version string to the D-Bus reply message.
 *----------------------------------------------------------------------
 */

static int
dbus_prop_get_toolkit_version(
    TCL_UNUSED(sd_bus *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    sd_bus_message *reply,
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    DEBUG_LOG("dbus_prop_get_toolkit_version: returning '%s'", TK_VERSION);
    return sd_bus_message_append(reply, "s", TK_VERSION);
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_interface_version --
 *
 *   D-Bus property getter for InterfaceVersion on the Application interface.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Appends the interface version to the D-Bus reply message.
 *----------------------------------------------------------------------
 */

static int
dbus_prop_get_interface_version(
    TCL_UNUSED(sd_bus *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(const char *),
    sd_bus_message *reply,
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    DEBUG_LOG("dbus_prop_get_interface_version: returning 1");
    return sd_bus_message_append(reply, "u", 1U);
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_atspi_version --
 *
 *   D-Bus property getter for AtspiVersion on the Application interface.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
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
    DEBUG_LOG("dbus_prop_get_atspi_version: returning '2.1'");
    return sd_bus_message_append(reply, "s", "2.1");
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_get_id --
 *
 *   D-Bus property getter for Id on the Application interface.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Appends the application ID to the D-Bus reply message.
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

    if (!acc) {
        DEBUG_LOG("dbus_prop_get_id: no acc, returning 0");
        return sd_bus_message_append(reply, "i", 0);
    }

    DEBUG_LOG("dbus_prop_get_id: returning %d", acc->application_id);
    return sd_bus_message_append(reply, "i", acc->application_id);
}

/*
 *----------------------------------------------------------------------
 * dbus_prop_set_id --
 *
 *   D-Bus property setter for Id on the Application interface.
 *   The registry assigns the ID during Socket.Embed.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Stores the application ID in the TkAccessible.
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
    int32_t id;

    if (!acc) {
        DEBUG_LOG("dbus_prop_set_id: no acc, returning -EINVAL");
        return -EINVAL;
    }

    int r = sd_bus_message_read(value, "i", &id);
    if (r < 0) {
        DEBUG_LOG("dbus_prop_set_id: failed to read id: %d", r);
        return r;
    }

    acc->application_id = id;
    DEBUG_LOG("dbus_prop_set_id: AT-SPI Application.Id set to %d for path %s", id, acc->dbus_path);

    return 0;
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_locale --
 *
 *   D-Bus method handler for GetLocale on the Application interface.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with the current locale.
 *----------------------------------------------------------------------
 */

static int
dbus_method_get_locale(
    sd_bus_message *m,
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    const char *locale = getenv("LANG");

    if (!locale || !locale[0]) {
        locale = "C";
    }

    DEBUG_LOG("dbus_method_get_locale: returning '%s'", locale);
    return sd_bus_reply_method_return(m, "s", locale);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_application_bus_address --
 *
 *   D-Bus method handler for GetApplicationBusAddress on the Application
 *   interface.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with the bus address.
 *----------------------------------------------------------------------
 */

static int
dbus_method_get_application_bus_address(
    sd_bus_message *m,
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    const char *address = NULL;

    if (!atspi_conn || !atspi_conn->bus) {
        DEBUG_LOG("dbus_method_get_application_bus_address: no bus, returning empty");
        return sd_bus_reply_method_return(m, "s", "");
    }

    if (sd_bus_get_address(atspi_conn->bus, &address) < 0 || !address) {
        DEBUG_LOG("dbus_method_get_application_bus_address: failed to get address, returning empty");
        address = "";
    } else {
        DEBUG_LOG("dbus_method_get_application_bus_address: returning '%s'", address);
    }

    return sd_bus_reply_method_return(m, "s", address);
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
    TkAccessible *acc,
    const char *member,
    const char *type,
    int32_t detail1,
    int32_t detail2,
    TkAccessible *related)
{
    if (!atspi_conn || !atspi_conn->bus) {
        DEBUG_LOG("EmitObjectEventFull: no bus, skipping");
        return;
    }
    if (!acc || !acc->dbus_path) {
        DEBUG_LOG("EmitObjectEventFull: no acc/path, skipping");
        return;
    }
    if (!member || !type) {
        DEBUG_LOG("EmitObjectEventFull: missing member or type");
        return;
    }
    
    DEBUG_LOG("EmitObjectEventFull: emitting %s event on path %s (detail1=%d, detail2=%d)", 
              member, acc->dbus_path, detail1, detail2);
    
    const char *rel_name = "";
    const char *rel_path = "/org/a11y/atspi/null";
    if (related && related->dbus_path) {
        rel_name = SelfBusName();
        rel_path = related->dbus_path;
        DEBUG_LOG("EmitObjectEventFull: related object: (%s, %s)", rel_name, rel_path);
    }
    
    sd_bus_message *m = NULL;
    int r = sd_bus_message_new_signal(atspi_conn->bus, &m,
                                      acc->dbus_path,
                                      "org.a11y.atspi.Event.Object",
                                      member);
    if (r < 0) {
        DEBUG_LOG("EmitObjectEventFull: failed to create signal: %d", r);
        return;
    }
    
    r = sd_bus_message_append(m, "sii", type, detail1, detail2);
    if (r >= 0) r = sd_bus_message_open_container(m, 'v', "(so)");
    if (r >= 0) r = sd_bus_message_append(m, "(so)", rel_name, rel_path);
    if (r >= 0) r = sd_bus_message_close_container(m);
    if (r >= 0) r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r >= 0) r = sd_bus_message_close_container(m);
    if (r >= 0) r = sd_bus_send(atspi_conn->bus, m, NULL);
    
    if (r < 0) {
        DEBUG_LOG("EmitObjectEventFull: failed to send signal: %d", r);
    } else {
        DEBUG_LOG("EmitObjectEventFull: signal sent successfully");
    }
    
    sd_bus_message_unref(m);
}

/*
 *----------------------------------------------------------------------
 * PostAccessibilityAnnouncement --
 *
 *   Post an accessibility announcement via AT-SPI Announcement signal.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Sends a D-Bus signal with the announcement message.
 *----------------------------------------------------------------------
 */

static void
PostAccessibilityAnnouncement(
    TkAccessible *acc,
    const char *message)
{
    if (!atspi_conn || !atspi_conn->bus) {
        DEBUG_LOG("PostAccessibilityAnnouncement: no bus, skipping");
        return;
    }
    if (!acc || !acc->dbus_path) {
        DEBUG_LOG("PostAccessibilityAnnouncement: no acc/path, skipping");
        return;
    }
    if (!message) {
        DEBUG_LOG("PostAccessibilityAnnouncement: no message, skipping");
        return;
    }

    DEBUG_LOG("PostAccessibilityAnnouncement: posting '%s' on path %s", message, acc->dbus_path);

    /*
     * The AT-SPI "Announcement" signal carries its text in the any_data
     * variant as a plain string, with detail1 conventionally used for
     * politeness (0 = polite, 1 = assertive). It does NOT use the (so)
     * accessible-reference shape that EmitObjectEventFull() builds for
     * events like ChildrenChanged/StateChanged, so it can't be routed
     * through that helper -- doing so silently drops the message text
     * into the unused "type" argument and sends an empty (so) tuple as
     * the payload, which is well-formed D-Bus but has nothing for a
     * screen reader to read.
     */
    sd_bus_message *m = NULL;
    int r = sd_bus_message_new_signal(atspi_conn->bus, &m,
                                      acc->dbus_path,
                                      "org.a11y.atspi.Event.Object",
                                      "Announcement");
    if (r < 0) {
        DEBUG_LOG("PostAccessibilityAnnouncement: failed to create signal: %d", r);
        return;
    }

    r = sd_bus_message_append(m, "sii", "", 0, 0);
    if (r >= 0) r = sd_bus_message_open_container(m, 'v', "s");
    if (r >= 0) r = sd_bus_message_append(m, "s", message);
    if (r >= 0) r = sd_bus_message_close_container(m);
    if (r >= 0) r = sd_bus_message_open_container(m, 'a', "{sv}");
    if (r >= 0) r = sd_bus_message_close_container(m);
    if (r >= 0) r = sd_bus_send(atspi_conn->bus, m, NULL);

    if (r < 0) {
        DEBUG_LOG("PostAccessibilityAnnouncement: failed to send signal: %d", r);
    } else {
        DEBUG_LOG("PostAccessibilityAnnouncement: announcement sent successfully");
    }

    sd_bus_message_unref(m);
}

/*
 *----------------------------------------------------------------------
 * GetNameForWidget --
 *
 *   Get the accessible name for a widget from TkAccessibilityObject hash.
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
    Tk_Window tkwin)
{
    if (!tkwin) {
        DEBUG_LOG("GetNameForWidget: null tkwin");
        return NULL;
    }
    
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
                        if (name && name[0] != '\0') {
                            DEBUG_LOG("GetNameForWidget: found name '%s' in accessibility hash", name);
                            return strdup(name);
                        }
                    }
                }
            }
        }
    }
    
    if (Tk_IsTopLevel(tkwin)) {
        Tcl_Interp *interp = Tk_Interp(tkwin);
        if (interp) {
            const char *path = Tk_PathName(tkwin);
            if (path) {
                Tcl_Obj *cmd = Tcl_NewStringObj("wm title ", -1);
                Tcl_AppendObjToObj(cmd, Tcl_NewStringObj(path, -1));
                Tcl_IncrRefCount(cmd);
                if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL) == TCL_OK) {
                    const char *title = Tcl_GetStringResult(interp);
                    if (title && title[0] != '\0') {
                        char *dup = strdup(title);
                        DEBUG_LOG("GetNameForWidget: got wm title '%s' for toplevel", title);
                        Tcl_DecrRefCount(cmd);
                        Tcl_ResetResult(interp);
                        return dup;
                    }
                }
                Tcl_DecrRefCount(cmd);
                Tcl_ResetResult(interp);
            }
        }
    }
    
    DEBUG_LOG("GetNameForWidget: no name found");
    return NULL;
}

/*
 *----------------------------------------------------------------------
 * GetDescriptionForWidget --
 *
 *   Get the accessible description for a widget from TkAccessibilityObject hash.
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
    Tk_Window tkwin)
{
    if (!tkwin) {
        DEBUG_LOG("GetDescriptionForWidget: null tkwin");
        return NULL;
    }
    
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)tkwin);
    if (!hPtr) {
        DEBUG_LOG("GetDescriptionForWidget: no entry in accessibility hash");
        return NULL;
    }
    
    Tcl_HashTable *attrs = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
    if (!attrs) {
        DEBUG_LOG("GetDescriptionForWidget: no attributes");
        return NULL;
    }
    
    Tcl_HashEntry *descEntry = Tcl_FindHashEntry(attrs, "description");
    if (!descEntry) {
        DEBUG_LOG("GetDescriptionForWidget: no description attribute");
        return NULL;
    }
    
    const char *desc = Tcl_GetString((Tcl_Obj *)Tcl_GetHashValue(descEntry));
    if (desc && desc[0] != '\0') {
        DEBUG_LOG("GetDescriptionForWidget: found description '%s'", desc);
        return strdup(desc);
    }
    
    return NULL;
}

/*
 *----------------------------------------------------------------------
 * GetValueForWidget --
 *
 *   Get the accessible value for a widget from TkAccessibilityObject hash.
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
    Tk_Window tkwin)
{
    if (!tkwin) {
        DEBUG_LOG("GetValueForWidget: null tkwin");
        return NULL;
    }
    
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
                        if (value && value[0]) {
                            DEBUG_LOG("GetValueForWidget: found value '%s'", value);
                            return strdup(value);
                        }
                    }
                }
            }
        }
    }
    
    DEBUG_LOG("GetValueForWidget: no value found");
    return NULL;
}

/*
 *----------------------------------------------------------------------
 * GetLiveRole --
 *
 *   Resolve an accessible's current AT-SPI role.
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
    TkAccessible *acc)
{
    if (!acc) {
        DEBUG_LOG("GetLiveRole: null acc, returning invalid");
        return ATSPI_ROLE_INVALID;
    }
    if (acc == atspi_conn->root_accessible) {
        DEBUG_LOG("GetLiveRole: root, returning APPLICATION");
        return ATSPI_ROLE_APPLICATION;
    }
    if (acc->tkwin && Tk_IsTopLevel(acc->tkwin)) {
        DEBUG_LOG("GetLiveRole: toplevel, returning FRAME");
        return ATSPI_ROLE_FRAME;
    }
    DEBUG_LOG("GetLiveRole: default FRAME for path %s", acc->dbus_path);
    return ATSPI_ROLE_FRAME;
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
    TkAccessible *acc)
{
    uint64_t states = ATSPI_STATE_ENABLED | ATSPI_STATE_SHOWING | ATSPI_STATE_VISIBLE;
    
    if (!acc) {
        DEBUG_LOG("ComputeStateForWidget: null acc, returning default states");
        return states;
    }
    
    if (acc == atspi_conn->root_accessible) {
        states |= ATSPI_STATE_FOCUSABLE;
        DEBUG_LOG("ComputeStateForWidget: root, states = 0x%lx", states);
        return states;
    }
    
    if (acc->tkwin && Tk_IsTopLevel(acc->tkwin)) {
        states |= ATSPI_STATE_FOCUSABLE;
        DEBUG_LOG("ComputeStateForWidget: toplevel, states = 0x%lx", states);
        return states;
    }
    
    DEBUG_LOG("ComputeStateForWidget: default states = 0x%lx", states);
    return states;
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
    Tk_Window tkwin,
    TkAccessible *acc)
{
    if (!tkwin || !acc || !atspi_conn) {
        DEBUG_LOG("RegisterAccessible: invalid parameters (tkwin=%p, acc=%p, conn=%p)", tkwin, acc, atspi_conn);
        return;
    }
    
    int isNew;
    Tcl_HashEntry *entry = Tcl_CreateHashEntry(atspi_conn->tk_to_accessible_map, (char *)tkwin, &isNew);
    Tcl_SetHashValue(entry, acc);
    DEBUG_LOG("RegisterAccessible: registered path %s for window %s (isNew=%d)", 
              acc->dbus_path, Tk_PathName(tkwin), isNew);
    
    if (Tk_IsTopLevel(tkwin) && acc != atspi_conn->root_accessible) {
        if (atspi_conn->num_toplevels < 256) {
            atspi_conn->toplevel_accessibles[atspi_conn->num_toplevels++] = acc;
            DEBUG_LOG("RegisterAccessible: added toplevel, count now %d", atspi_conn->num_toplevels);
        } else {
            DEBUG_LOG("RegisterAccessible: WARNING - toplevel array full!");
        }
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
    Tk_Window tkwin)
{
    if (!atspi_conn || !atspi_conn->tk_to_accessible_map || !tkwin) {
        DEBUG_LOG("GetAccessible: invalid state");
        return NULL;
    }
    
    Tcl_HashEntry *entry = Tcl_FindHashEntry(atspi_conn->tk_to_accessible_map, (char *)tkwin);
    if (!entry) {
        DEBUG_LOG("GetAccessible: no entry for window %s", Tk_PathName(tkwin));
        return NULL;
    }
    TkAccessible *acc = (TkAccessible *)Tcl_GetHashValue(entry);
    DEBUG_LOG("GetAccessible: found path %s for window %s", acc ? acc->dbus_path : "null", Tk_PathName(tkwin));
    return acc;
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
    Tk_Window tkwin)
{
    if (!atspi_conn || !tkwin) {
        DEBUG_LOG("UnregisterAccessible: invalid state");
        return;
    }
    
    Tcl_HashEntry *entry = Tcl_FindHashEntry(atspi_conn->tk_to_accessible_map, (char *)tkwin);
    if (!entry) {
        DEBUG_LOG("UnregisterAccessible: no entry for window %s", Tk_PathName(tkwin));
        return;
    }
    
    TkAccessible *acc = (TkAccessible *)Tcl_GetHashValue(entry);
    if (acc) {
        DEBUG_LOG("UnregisterAccessible: unregistering path %s for window %s", acc->dbus_path, Tk_PathName(tkwin));
        if (Tk_IsTopLevel(tkwin) && acc != atspi_conn->root_accessible) {
            for (int i = 0; i < atspi_conn->num_toplevels; i++) {
                if (atspi_conn->toplevel_accessibles[i] == acc) {
                    atspi_conn->toplevel_accessibles[i] = NULL;
                    DEBUG_LOG("UnregisterAccessible: removed from toplevel array at index %d", i);
                    break;
                }
            }
        }
        Tcl_DeleteHashEntry(entry);
        FreeAccessible(acc);
    }
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
    TkAccessible *acc)
{
    if (!acc) {
        DEBUG_LOG("FreeAccessible: null acc");
        return;
    }
    
    DEBUG_LOG("FreeAccessible: freeing path %s", acc->dbus_path ? acc->dbus_path : "null");
    
    /* Unregister D-Bus vtables. */
    for (int i = 0; i < acc->n_vtable_slots; i++) {
        if (acc->vtable_slots[i]) {
            sd_bus_slot_unref(acc->vtable_slots[i]);
            acc->vtable_slots[i] = NULL;
        }
    }
    acc->n_vtable_slots = 0;
    
    if (acc->dbus_path) free(acc->dbus_path);
    if (acc->cached_name) free(acc->cached_name);
    if (acc->cached_description) free(acc->cached_description);
    if (acc->cached_value) free(acc->cached_value);
    
    Tcl_Free(acc);
    DEBUG_LOG("FreeAccessible: freed");
}

/*
 *----------------------------------------------------------------------
 * CreateToplevelAccessible --
 *
 *   Create an accessible object for a toplevel window.
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
CreateToplevelAccessible(
    Tcl_Interp *interp,
    Tk_Window tkwin)
{
    if (!interp || !tkwin || !atspi_conn) {
        DEBUG_LOG("CreateToplevelAccessible: invalid parameters");
        return NULL;
    }
    
    DEBUG_LOG("CreateToplevelAccessible: creating for window %s", Tk_PathName(tkwin));
    
    TkAccessible *acc = (TkAccessible *)Tcl_Alloc(sizeof(TkAccessible));
    if (!acc) {
        DEBUG_LOG("CreateToplevelAccessible: allocation failed");
        return NULL;
    }
    memset(acc, 0, sizeof(TkAccessible));
    
    acc->interp = interp;
    acc->tkwin = tkwin;
    acc->role = ATSPI_ROLE_FRAME;
    acc->states = ComputeStateForWidget(acc);
    
    /* Generate DBus path. */
    static int counter = 0;
    char path[256];
    const char *pn = Tk_PathName(tkwin);
    if (!pn || pn[0] == '\0' || (pn[0] == '.' && pn[1] == '\0')) {
        snprintf(path, sizeof(path), "/org/a11y/atspi/accessible/toplevel%d", counter++);
    } else {
        snprintf(path, sizeof(path), "/org/a11y/atspi/accessible/toplevel/%s", pn);
        /* Sanitize for DBus. */
        for (char *p = path; *p; p++) {
            if (*p == '.') *p = '_';
            else if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '/' || *p == '_')) *p = '_';
        }
    }
    acc->dbus_path = strdup(path);
    DEBUG_LOG("CreateToplevelAccessible: generated path %s", acc->dbus_path);
    
    /* Cache name. */
    char *name = GetNameForWidget(tkwin);
    if (name) {
        acc->cached_name = name;
        DEBUG_LOG("CreateToplevelAccessible: cached name '%s'", name);
    }
    
    /* Register Accessible vtable. */
    sd_bus_slot *slot = NULL;
    int r = sd_bus_add_object_vtable(atspi_conn->bus, &slot,
                                      acc->dbus_path,
                                      ATSPI_ACCESSIBLE_INTERFACE,
                                      accessible_vtable,
                                      acc);
    if (r < 0 || !slot) {
        DEBUG_LOG("CreateToplevelAccessible: failed to register vtable: %d", r);
        free(acc->dbus_path);
        if (acc->cached_name) free(acc->cached_name);
        Tcl_Free(acc);
        return NULL;
    }
    acc->vtable_slots[acc->n_vtable_slots++] = slot;
    DEBUG_LOG("CreateToplevelAccessible: vtable registered successfully");
    
    return acc;
}

/*
 *----------------------------------------------------------------------
 * RegisterToplevelWithAccessibility --
 *
 *   Create and register an accessible object for a toplevel window.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Creates a TkAccessible object and emits a window-opened announcement.
 *----------------------------------------------------------------------
 */

static void
RegisterToplevelWithAccessibility(
    Tcl_Interp *interp,
    Tk_Window tkwin)
{
    if (!tkwin || !Tk_IsTopLevel(tkwin)) {
        DEBUG_LOG("RegisterToplevelWithAccessibility: not a toplevel");
        return;
    }
    if (GetAccessible(tkwin)) {
        DEBUG_LOG("RegisterToplevelWithAccessibility: window %s already registered", Tk_PathName(tkwin));
        return;
    }
    
    DEBUG_LOG("RegisterToplevelWithAccessibility: registering toplevel %s", Tk_PathName(tkwin));
    
    TkAccessible *acc = CreateToplevelAccessible(interp, tkwin);
    if (!acc) {
        DEBUG_LOG("RegisterToplevelWithAccessibility: failed to create accessible");
        return;
    }
    
    RegisterAccessible(tkwin, acc);
    TkAccessible_RegisterEventHandlers(tkwin, acc);
    
    /* Emit announcement that window exists. */
    char *name = GetNameForWidget(tkwin);
    if (name) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Window opened: %s", name);
        DEBUG_LOG("RegisterToplevelWithAccessibility: posting announcement '%s'", msg);
        PostAccessibilityAnnouncement(acc, msg);
        free(name);
    }
    
    DEBUG_LOG("RegisterToplevelWithAccessibility: registration complete");
}

/*
 *----------------------------------------------------------------------
 * UpdateFocusChain --
 *
 *   Update accessibility focus and emit announcement for the focused widget.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Emits an AT-SPI announcement event from the toplevel accessible.
 *----------------------------------------------------------------------
 */

static void
UpdateFocusChain(
    Tk_Window focused)
{
    if (!focused || !atspi_conn) {
        DEBUG_LOG("UpdateFocusChain: no focus or no connection");
        return;
    }
    
    Tcl_Interp *interp = Tk_Interp(focused);
    if (!interp) {
        DEBUG_LOG("UpdateFocusChain: no interp");
        return;
    }
    
    DEBUG_LOG("UpdateFocusChain: focus changed to %s", Tk_PathName(focused));
    
    /* Ensure toplevel accessible exists. */
    Tk_Window topWin = focused;
    while (topWin && !Tk_IsTopLevel(topWin)) {
        topWin = Tk_Parent(topWin);
    }
    if (topWin) {
        TkAccessible *topAcc = GetAccessible(topWin);
        if (!topAcc) {
            DEBUG_LOG("UpdateFocusChain: toplevel %s not registered, registering", Tk_PathName(topWin));
            RegisterToplevelWithAccessibility(interp, topWin);
            topAcc = GetAccessible(topWin);
        }
        
        if (topAcc) {
            /* Build announcement string from name, description, and value. */
            char *name = GetNameForWidget(focused);
            char *desc = GetDescriptionForWidget(focused);
            char *value = GetValueForWidget(focused);
            
            char msg[1024] = "";
            int has_content = 0;
            
            if (name && name[0]) {
                strcat(msg, name);
                has_content = 1;
            }
            if (desc && desc[0]) {
                if (has_content) strcat(msg, ", ");
                strcat(msg, desc);
                has_content = 1;
            }
            if (value && value[0]) {
                if (has_content) strcat(msg, ": ");
                strcat(msg, value);
                has_content = 1;
            }
            
            if (has_content) {
                DEBUG_LOG("UpdateFocusChain: posting focus announcement '%s'", msg);
                PostAccessibilityAnnouncement(topAcc, msg);
            } else {
                DEBUG_LOG("UpdateFocusChain: no content for announcement");
            }
            
            if (name) free(name);
            if (desc) free(desc);
            if (value) free(value);
        }
    } else {
        DEBUG_LOG("UpdateFocusChain: no toplevel found");
    }
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
 *   Updates the accessibility focus and emits an announcement.
 *----------------------------------------------------------------------
 */

static void
TkAccessible_FocusHandler(
    void *clientData,
    XEvent *eventPtr)
{
    TkAccessible *acc = (TkAccessible *)clientData;
    
    if (!acc || !acc->tkwin || !eventPtr) {
        DEBUG_LOG("TkAccessible_FocusHandler: invalid parameters");
        return;
    }
    if (eventPtr->type != FocusIn) {
        DEBUG_LOG("TkAccessible_FocusHandler: event type %d, ignoring", eventPtr->type);
        return;
    }
    
    DEBUG_LOG("TkAccessible_FocusHandler: FocusIn event for window %s", Tk_PathName(acc->tkwin));
    UpdateFocusChain(acc->tkwin);
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
 *   Unregisters the accessible object.
 *----------------------------------------------------------------------
 */

static void
TkAccessible_DestroyHandler(
    void *clientData,
    XEvent *eventPtr)
{
    if (eventPtr->type != DestroyNotify) {
        DEBUG_LOG("TkAccessible_DestroyHandler: ignoring event type %d", eventPtr->type);
        return;
    }
    
    TkAccessible *acc = (TkAccessible *)clientData;
    if (!acc || !acc->tkwin) {
        DEBUG_LOG("TkAccessible_DestroyHandler: invalid acc/tkwin");
        return;
    }
    
    DEBUG_LOG("TkAccessible_DestroyHandler: DestroyNotify for window %s", Tk_PathName(acc->tkwin));
    
    Tk_DeleteEventHandler(acc->tkwin, FocusChangeMask,
                          TkAccessible_FocusHandler, acc);
    Tk_DeleteEventHandler(acc->tkwin, StructureNotifyMask,
                          TkAccessible_DestroyHandler, acc);
    
    UnregisterAccessible(acc->tkwin);
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
    Tk_Window tkwin,
    TkAccessible *acc)
{
    if (!tkwin || !acc) {
        DEBUG_LOG("TkAccessible_RegisterEventHandlers: invalid parameters");
        return;
    }
    
    DEBUG_LOG("TkAccessible_RegisterEventHandlers: registering for window %s", Tk_PathName(tkwin));
    
    Tk_CreateEventHandler(tkwin, FocusChangeMask,
                          TkAccessible_FocusHandler, acc);
    Tk_CreateEventHandler(tkwin, StructureNotifyMask,
                          TkAccessible_DestroyHandler, acc);
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
    
    DEBUG_LOG("ConnectToAtspiBus: connecting to AT-SPI bus");
    
    r = sd_bus_default_user(&session);
    if (r < 0) {
        DEBUG_LOG("ConnectToAtspiBus: sd_bus_default_user failed: %d", r);
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
        DEBUG_LOG("ConnectToAtspiBus: GetAddress failed: %d - %s", r, error.message);
        sd_bus_error_free(&error);
        if (reply) sd_bus_message_unref(reply);
        sd_bus_unref(session);
        return NULL;
    }
    
    r = sd_bus_message_read(reply, "s", &addr);
    if (r < 0 || !addr || addr[0] == '\0') {
        DEBUG_LOG("ConnectToAtspiBus: failed to read address: %d", r);
        sd_bus_message_unref(reply);
        sd_bus_unref(session);
        return NULL;
    }
    
    DEBUG_LOG("ConnectToAtspiBus: got address: %s", addr);
    
    r = sd_bus_new(&a11y_bus);
    if (r < 0) {
        DEBUG_LOG("ConnectToAtspiBus: sd_bus_new failed: %d", r);
        sd_bus_message_unref(reply);
        sd_bus_unref(session);
        return NULL;
    }
    r = sd_bus_set_address(a11y_bus, addr);
    if (r < 0) {
        DEBUG_LOG("ConnectToAtspiBus: sd_bus_set_address failed: %d", r);
        sd_bus_unref(a11y_bus);
        sd_bus_message_unref(reply);
        sd_bus_unref(session);
        return NULL;
    }
    r = sd_bus_set_bus_client(a11y_bus, 1);
    if (r < 0) {
        DEBUG_LOG("ConnectToAtspiBus: sd_bus_set_bus_client failed: %d", r);
        sd_bus_unref(a11y_bus);
        sd_bus_message_unref(reply);
        sd_bus_unref(session);
        return NULL;
    }
    r = sd_bus_start(a11y_bus);
    if (r < 0) {
        DEBUG_LOG("ConnectToAtspiBus: sd_bus_start failed: %d", r);
        sd_bus_unref(a11y_bus);
        sd_bus_message_unref(reply);
        sd_bus_unref(session);
        return NULL;
    }
    
    DEBUG_LOG("ConnectToAtspiBus: connected successfully");
    
    sd_bus_message_unref(reply);
    sd_bus_unref(session);
    return a11y_bus;
}

/*
 *----------------------------------------------------------------------
 * EmbedWithRegistry --
 *
 *   Embed our application into the registry's accessible tree using
 *   the Socket.Embed method on the registry object.
 *
 * Results:
 *   Returns true on success, false on failure.
 *
 * Side effects:
 *   Stores the desktop reference in the global connection state.
 *----------------------------------------------------------------------
 */

static bool
EmbedWithRegistry(void)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *desktop_name = NULL;
    const char *desktop_path = NULL;
    int r;

    if (!atspi_conn || !atspi_conn->bus || !atspi_conn->root_accessible ||
        !atspi_conn->root_accessible->dbus_path) {
        DEBUG_LOG("EmbedWithRegistry: invalid connection/root");
        return false;
    }

    DEBUG_LOG("EmbedWithRegistry: registering app bus=%s root=%s",
              SelfBusName(), atspi_conn->root_accessible->dbus_path);

    /*
     * Socket.Embed is exported by the AT-SPI registry at the
     * application/root accessible path.
     */
    r = sd_bus_call_method(
        atspi_conn->bus,
        "org.a11y.atspi.Registry",
        ATSPI_DBUS_PATH_ROOT,
        "org.a11y.atspi.Socket",
        "Embed",
        &error,
        &reply,
        "(so)",
        SelfBusName(),
        atspi_conn->root_accessible->dbus_path);

    if (r < 0) {
        DEBUG_LOG(
            "EmbedWithRegistry: Socket.Embed failed: r=%d name=%s message=%s",
            r,
            error.name ? error.name : "(none)",
            error.message ? error.message : "(none)");

        sd_bus_error_free(&error);

        if (reply) {
            sd_bus_message_unref(reply);
        }

        atspi_conn->is_embedded = 0;
        return false;
    }

    r = sd_bus_message_read(reply, "(so)",
                            &desktop_name, &desktop_path);

    if (r < 0) {
        DEBUG_LOG("EmbedWithRegistry: invalid Embed reply: %d", r);

        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);

        atspi_conn->is_embedded = 0;
        return false;
    }

    DEBUG_LOG("EmbedWithRegistry: registry returned desktop=%s path=%s",
              desktop_name ? desktop_name : "(null)",
              desktop_path ? desktop_path : "(null)");

    if (atspi_conn->desktop_bus_name) {
        free(atspi_conn->desktop_bus_name);
        atspi_conn->desktop_bus_name = NULL;
    }

    if (atspi_conn->desktop_path) {
        free(atspi_conn->desktop_path);
        atspi_conn->desktop_path = NULL;
    }

    if (desktop_name) {
        atspi_conn->desktop_bus_name = strdup(desktop_name);

        if (!atspi_conn->desktop_bus_name) {
            DEBUG_LOG(
                "EmbedWithRegistry: failed to allocate desktop bus name");

            sd_bus_message_unref(reply);
            sd_bus_error_free(&error);

            atspi_conn->is_embedded = 0;
            return false;
        }
    }

    if (desktop_path) {
        atspi_conn->desktop_path = strdup(desktop_path);

        if (!atspi_conn->desktop_path) {
            DEBUG_LOG(
                "EmbedWithRegistry: failed to allocate desktop path");

            free(atspi_conn->desktop_bus_name);
            atspi_conn->desktop_bus_name = NULL;

            sd_bus_message_unref(reply);
            sd_bus_error_free(&error);

            atspi_conn->is_embedded = 0;
            return false;
        }
    }

    atspi_conn->is_embedded = 1;

    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);

    DEBUG_LOG("EmbedWithRegistry: application successfully registered");

    return true;
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
    FILE *fp = popen("pgrep -x orca", "r");
    if (!fp) {
        DEBUG_LOG("IsScreenReaderActive: popen failed");
        return 0;
    }
    char buffer[16];
    int running = (fgets(buffer, sizeof(buffer), fp) != NULL);
    pclose(fp);
    DEBUG_LOG("IsScreenReaderActive: orca %s", running ? "running" : "not running");
    return running;
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
    sd_bus_slot *slot = NULL;
    int r;
    
    DEBUG_LOG("InitializeAtspiConnection: starting initialization");
    
    if (atspi_conn && atspi_conn->is_initialized) {
        DEBUG_LOG("InitializeAtspiConnection: already initialized");
        return true;
    }
    
    atspi_conn = (AtspiConnection *)Tcl_Alloc(sizeof(AtspiConnection));
    if (!atspi_conn) {
        DEBUG_LOG("InitializeAtspiConnection: allocation failed");
        return false;
    }
    memset(atspi_conn, 0, sizeof(AtspiConnection));
    
    bus = ConnectToAtspiBus();
    if (!bus) {
        DEBUG_LOG("InitializeAtspiConnection: ConnectToAtspiBus failed");
        Tcl_Free(atspi_conn);
        atspi_conn = NULL;
        return false;
    }
    atspi_conn->bus = bus;
    atspi_bus = bus;
    DEBUG_LOG("InitializeAtspiConnection: bus connected");
    
    atspi_conn->tk_to_accessible_map = (Tcl_HashTable *)Tcl_Alloc(sizeof(Tcl_HashTable));
    if (!atspi_conn->tk_to_accessible_map) {
        DEBUG_LOG("InitializeAtspiConnection: hash table allocation failed");
        sd_bus_unref(bus);
        Tcl_Free(atspi_conn);
        atspi_conn = NULL;
        return false;
    }
    Tcl_InitHashTable(atspi_conn->tk_to_accessible_map, TCL_ONE_WORD_KEYS);
    DEBUG_LOG("InitializeAtspiConnection: hash table initialized");
    
    /* Check if registry is running. */
    r = sd_bus_call_method(bus,
                           "org.freedesktop.DBus",
                           "/org/freedesktop/DBus",
                           "org.freedesktop.DBus",
                           "GetNameOwner",
                           &error,
                           &msg,
                           "s", "org.a11y.atspi.Registry");
    if (r < 0) {
        DEBUG_LOG("InitializeAtspiConnection: registry check failed: %d - %s", r, error.message);
        sd_bus_error_free(&error);
        if (msg) sd_bus_message_unref(msg);
    } else {
        DEBUG_LOG("InitializeAtspiConnection: registry is running");
    }
    if (msg) sd_bus_message_unref(msg);
    sd_bus_error_free(&error);
    
    /* Create root accessible object. */
    atspi_conn->root_accessible = (TkAccessible *)Tcl_Alloc(sizeof(TkAccessible));
    if (!atspi_conn->root_accessible) {
        DEBUG_LOG("InitializeAtspiConnection: root allocation failed");
        Tcl_DeleteHashTable(atspi_conn->tk_to_accessible_map);
        Tcl_Free(atspi_conn->tk_to_accessible_map);
        sd_bus_unref(bus);
        Tcl_Free(atspi_conn);
        atspi_conn = NULL;
        return false;
    }
    memset(atspi_conn->root_accessible, 0, sizeof(TkAccessible));
    
    atspi_conn->root_accessible->role = ATSPI_ROLE_APPLICATION;
    atspi_conn->root_accessible->dbus_path = strdup("/org/a11y/atspi/accessible/root");
    atspi_conn->root_accessible->states = ComputeStateForWidget(atspi_conn->root_accessible);
    DEBUG_LOG("InitializeAtspiConnection: root accessible created at %s", atspi_conn->root_accessible->dbus_path);
    
    /* Register Accessible vtable. */
    slot = NULL;
    r = sd_bus_add_object_vtable(atspi_conn->bus, &slot,
                                  atspi_conn->root_accessible->dbus_path,
                                  ATSPI_ACCESSIBLE_INTERFACE,
                                  accessible_vtable,
                                  atspi_conn->root_accessible);
    if (r < 0 || !slot) {
        DEBUG_LOG("InitializeAtspiConnection: failed to register root Accessible vtable: %d", r);
        FreeAccessible(atspi_conn->root_accessible);
        atspi_conn->root_accessible = NULL;
        Tcl_DeleteHashTable(atspi_conn->tk_to_accessible_map);
        Tcl_Free(atspi_conn->tk_to_accessible_map);
        sd_bus_unref(bus);
        Tcl_Free(atspi_conn);
        atspi_conn = NULL;
        return false;
    }
    atspi_conn->root_accessible->vtable_slots[
        atspi_conn->root_accessible->n_vtable_slots++] = slot;
    DEBUG_LOG("InitializeAtspiConnection: root Accessible vtable registered");
    
    /* Register Application vtable. */
    slot = NULL;
    r = sd_bus_add_object_vtable(atspi_conn->bus, &slot,
                                  atspi_conn->root_accessible->dbus_path,
                                  "org.a11y.atspi.Application",
                                  application_vtable,
                                  atspi_conn->root_accessible);
    if (r < 0 || !slot) {
        DEBUG_LOG("InitializeAtspiConnection: failed to register root Application vtable: %d", r);
        FreeAccessible(atspi_conn->root_accessible);
        atspi_conn->root_accessible = NULL;
        Tcl_DeleteHashTable(atspi_conn->tk_to_accessible_map);
        Tcl_Free(atspi_conn->tk_to_accessible_map);
        sd_bus_unref(bus);
        Tcl_Free(atspi_conn);
        atspi_conn = NULL;
        return false;
    }
    atspi_conn->root_accessible->vtable_slots[
        atspi_conn->root_accessible->n_vtable_slots++] = slot;
    DEBUG_LOG("InitializeAtspiConnection: root Application vtable registered");
    
    /* Register Cache interface at the fixed path. */
    sd_bus_slot *cache_slot = NULL;
    r = sd_bus_add_object_vtable(atspi_conn->bus, &cache_slot,
                                  "/org/a11y/atspi/cache",
                                  "org.a11y.atspi.Cache",
                                  cache_vtable, NULL);
    if (r >= 0 && cache_slot) {
        if (atspi_conn->root_accessible->n_vtable_slots < TK_ACCESSIBLE_MAX_SLOTS) {
            atspi_conn->root_accessible->vtable_slots[
                atspi_conn->root_accessible->n_vtable_slots++] = cache_slot;
        }
        DEBUG_LOG("InitializeAtspiConnection: Cache vtable registered");
    } else {
        DEBUG_LOG("InitializeAtspiConnection: Cache vtable registration failed: %d", r);
    }
    
    /* 
     * Registration must succeed for initialization to succeed.
     */
    if (!EmbedWithRegistry()) {
        DEBUG_LOG("InitializeAtspiConnection: EmbedWithRegistry failed - initialization aborted");
        FreeAccessible(atspi_conn->root_accessible);
        atspi_conn->root_accessible = NULL;
        Tcl_DeleteHashTable(atspi_conn->tk_to_accessible_map);
        Tcl_Free(atspi_conn->tk_to_accessible_map);
        sd_bus_unref(bus);
        atspi_bus = NULL;
        Tcl_Free(atspi_conn);
        atspi_conn = NULL;
        return false;
    }
    
    atspi_conn->is_initialized = 1;
    DEBUG_LOG("InitializeAtspiConnection: initialization complete");
    
    return true;
}

/*
 *----------------------------------------------------------------------
 * TkWaylandAtspiProcessEvents --
 *
 *   Drain pending AT-SPI D-Bus messages on atspi_bus.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Processes pending D-Bus messages.
 *----------------------------------------------------------------------
 */

void
TkWaylandAtspiProcessEvents(void)
{
    if (!atspi_bus || atspi_draining) {
        if (!atspi_bus) DEBUG_LOG("TkWaylandAtspiProcessEvents: no bus");
        return;
    }
    
    atspi_draining = 1;
    int count = 0;
    while (sd_bus_process(atspi_bus, NULL) > 0) {
        count++;
    }
    if (count > 0) {
        DEBUG_LOG("TkWaylandAtspiProcessEvents: processed %d messages", count);
    }
    atspi_draining = 0;
}

/*
 *----------------------------------------------------------------------
 * AddAccessibleCmd --
 *
 *   Tcl command implementation for ::tk::accessible::add_acc_object.
 *   Registers a toplevel window as an accessible object.
 *
 * Results:
 *   Returns TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *   Creates an accessible object for the toplevel window.
 *----------------------------------------------------------------------
 */

static int
AddAccessibleCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "window");
        return TCL_ERROR;
    }
    
    const char *windowName = Tcl_GetString(objv[1]);
    DEBUG_LOG("AddAccessibleCmd: called for window %s", windowName);
    
    Tk_Window tkwin = Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));
    if (!tkwin) {
        DEBUG_LOG("AddAccessibleCmd: invalid window name %s", windowName);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Invalid window name.", -1));
        return TCL_ERROR;
    }
    
    /* Register toplevel if not already. */
    if (Tk_IsTopLevel(tkwin)) {
        DEBUG_LOG("AddAccessibleCmd: registering toplevel %s", windowName);
        RegisterToplevelWithAccessibility(interp, tkwin);
    } else {
        DEBUG_LOG("AddAccessibleCmd: window %s is not a toplevel", windowName);
    }
    
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 * EmitSelectionChangedCmd --
 *
 *   Tcl command implementation for ::tk::accessible::emit_selection_change.
 *   Emits a selection changed announcement.
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
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "window");
        return TCL_ERROR;
    }
    
    const char *windowName = Tcl_GetString(objv[1]);
    DEBUG_LOG("EmitSelectionChangedCmd: called for window %s", windowName);
    
    Tk_Window tkwin = Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));
    if (!tkwin) {
        DEBUG_LOG("EmitSelectionChangedCmd: invalid window name %s", windowName);
        return TCL_OK;
    }
    
    Tk_Window topWin = tkwin;
    while (topWin && !Tk_IsTopLevel(topWin)) {
        topWin = Tk_Parent(topWin);
    }
    if (topWin) {
        TkAccessible *acc = GetAccessible(topWin);
        if (acc) {
            DEBUG_LOG("EmitSelectionChangedCmd: posting selection changed announcement");
            PostAccessibilityAnnouncement(acc, "selection changed");
        } else {
            DEBUG_LOG("EmitSelectionChangedCmd: no accessible for toplevel");
        }
    } else {
        DEBUG_LOG("EmitSelectionChangedCmd: no toplevel found");
    }
    
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 * EmitFocusChangedCmd --
 *
 *   Tcl command implementation for ::tk::accessible::emit_focus_change.
 *   Updates the accessibility focus and emits an announcement.
 *
 * Results:
 *   Returns TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *   Updates the accessibility focus chain and emits an announcement.
 *----------------------------------------------------------------------
 */

static int
EmitFocusChangedCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "window");
        return TCL_ERROR;
    }
    
    const char *windowName = Tcl_GetString(objv[1]);
    DEBUG_LOG("EmitFocusChangedCmd: called for window %s", windowName);
    
    Tk_Window tkwin = Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));
    if (!tkwin) {
        DEBUG_LOG("EmitFocusChangedCmd: invalid window name %s", windowName);
        return TCL_OK;
    }
    
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
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    TCL_UNUSED(int),
    TCL_UNUSED(Tcl_Obj *const *))
{
    bool result = IsScreenReaderActive();
    DEBUG_LOG("IsScreenReaderRunningCmd: returning %d", result);
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(result));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 * AtspiFileHandlerProc --
 *
 *   Tcl file handler invoked when the AT-SPI D-Bus socket is readable.
 *   Drains pending D-Bus traffic so incoming calls (e.g. Orca reading
 *   Name/Role on our accessible objects) actually get answered, and so
 *   queued outgoing messages get flushed.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Processes pending D-Bus messages.
 *----------------------------------------------------------------------
 */

static void
AtspiFileHandlerProc(
    TCL_UNUSED(void *),
    TCL_UNUSED(int))
{
    DEBUG_LOG("AtspiFileHandlerProc: socket readable");
    TkWaylandAtspiProcessEvents();
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
    Tcl_Interp *interp)
{
    DEBUG_LOG("TkWaylandAccessibility_Init: starting initialization");
    
    if (!InitializeAtspiConnection()) {
        DEBUG_LOG("TkWaylandAccessibility_Init: InitializeAtspiConnection failed");
        Tcl_AppendResult(interp,
            "Warning: Could not connect to AT-SPI - accessibility disabled for now",
            (char *)NULL);
    } else if (atspi_bus) {
        DEBUG_LOG("TkWaylandAccessibility_Init: AT-SPI connection established");
        /*
         * Without this, nothing ever calls sd_bus_process()/
         * TkWaylandAtspiProcessEvents() after the one synchronous
         * Socket.Embed call in InitializeAtspiConnection(), so incoming
         * D-Bus calls from Orca/the registry (and any queued outgoing
         * traffic) never get drained by the Tk event loop.
         */
        int fd = sd_bus_get_fd(atspi_bus);
        if (fd >= 0) {
            DEBUG_LOG("TkWaylandAccessibility_Init: creating file handler for fd %d", fd);
            Tcl_CreateFileHandler(fd, TCL_READABLE, AtspiFileHandlerProc, NULL);
        } else {
            DEBUG_LOG("TkWaylandAccessibility_Init: failed to get bus fd");
        }
    }
    
    /* Register main window. */
    Tk_Window mainWin = Tk_MainWindow(interp);
    if (mainWin && Tk_IsTopLevel(mainWin)) {
        DEBUG_LOG("TkWaylandAccessibility_Init: registering main window");
        RegisterToplevelWithAccessibility(interp, mainWin);
    } else {
        DEBUG_LOG("TkWaylandAccessibility_Init: no main window to register");
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
    
    DEBUG_LOG("TkWaylandAccessibility_Init: initialization complete");
    return TCL_OK;
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
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
