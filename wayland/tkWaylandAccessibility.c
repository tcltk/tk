/*
 * tkWaylandAccessibility.c --
 *
 * Minimal Tk accessibility on Wayland.
 * Registers one AT‑SPI application/root object via sd‑bus so Orca and
 * Accerciser can detect Tk. No accessible tree, no children, no cache.
 * Widget name/description/value announcements are spoken directly via
 * libspeechd, bypassing AT‑SPI events. This "single static object"
 * design avoids the D‑Bus traffic and tree‑management overhead that a
 * full AT‑SPI hierarchy would require and that Tk cannot sustain.
 
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 2006, Marcus von Appen
 * Copyright (c) 2019-2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */
 
/* Debugging.
#define DEBUG_CHANNEL stdout
#define DEBUG_LABEL "at-spi"
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <tcl.h>
#include <tk.h>
#include <systemd/sd-bus.h>
#include <wayland-client.h>
#include <libspeechd.h>
#include "tkInt.h"
#include "tkWaylandInt.h"
#include "tkWaylandWm.h"

/* at-spi D-Bus constants. */
#define ATSPI_DBUS_NAME           "org.a11y.Bus"
#define ATSPI_DBUS_PATH           "/org/a11y/bus"
#define ATSPI_REGISTRY_INTERFACE  "org.a11y.atspi.Registry"
#define ATSPI_ACCESSIBLE_INTERFACE "org.a11y.atspi.Accessible"
#define ATSPI_APPLICATION_INTERFACE "org.a11y.atspi.Application"
#define ATSPI_EVENT_INTERFACE     "org.a11y.atspi.Event"
#define ATSPI_SOCKET_INTERFACE    "org.a11y.atspi.Socket"

/* at-spi D-Bus paths. */
#define ATSPI_DBUS_PATH_REGISTRY  "/org/a11y/atspi/registry"
#define ATSPI_DBUS_PATH_ROOT      "/org/a11y/atspi/accessible/root"
#define ATSPI_DBUS_PATH_NULL      "/org/a11y/atspi/null"

/*
 * at-spi role constants.
 */
#define ATSPI_ROLE_INVALID           0
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

/*
 * Main accessible object structure: atspi_conn->root_accessible, 
 * the application object.
 */
typedef struct TkAccessible {
    char *dbus_path;
    int role;
    uint64_t states;
    int32_t application_id;   /* Application.Id, assigned by the registry. */
    char *cached_name;

    /* D-Bus slots for cleanup. */
#define TK_ACCESSIBLE_MAX_SLOTS 8
    sd_bus_slot *vtable_slots[TK_ACCESSIBLE_MAX_SLOTS];
    int n_vtable_slots;
} TkAccessible;

/* Global connection state. */
typedef struct {
    sd_bus *bus;
    int is_initialized;
    TkAccessible *root_accessible;

    /* Desktop reference from Socket.Embed. */
    char *desktop_bus_name;
    char *desktop_path;
    int is_embedded;
} AtspiConnection;

/*
 * Forward declarations.
 */

static void FreeAccessible(TkAccessible *acc);
static int GetLiveRole(TkAccessible *acc);
static uint64_t ComputeStateForWidget(TkAccessible *acc);
static const char *GetNameForWidget(Tk_Window tkwin);
static char *GetDescriptionForWidget(Tk_Window tkwin);
static char *GetValueForWidget(Tk_Window tkwin);
static const char *GetWmTitleForToplevel(Tk_Window tkwin);

/* D-Bus vtables. */
static const sd_bus_vtable accessible_vtable[];
static const sd_bus_vtable application_vtable[];

/* Accessible-reference helpers. */
static const char *SelfBusName(void);
static int AppendAccessibleRef(sd_bus_message *reply, const char *path);
static bool EmbedWithRegistry(void);

/* Speech (libspeechd) helpers. */
static void PostAccessibilityAnnouncement(TkAccessible *acc, const char *message);
static void StopSpeech(void);

/* Focus handling -- speech-only, not routed through AT-SPI. */
static void UpdateFocusChain(Tk_Window focused);

/* Screen reader detection. */
static int IsScreenReaderActive(void);

/* Shutdown. */
void TkWaylandAccessibility_Finalize(void);
static void AtspiExitProc(void *clientData);

/* Tcl command implementations. */
static int AddAccessibleCmd(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int EmitSelectionChangedCmd(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int EmitFocusChangedCmd(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int IsScreenReaderRunningCmd(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

/* D-Bus method handlers. */
static int dbus_method_get_role(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_state(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_children(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_child_at_index(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_interfaces(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_role_name(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_localized_role_name(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_attributes(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_get_relation_set(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbus_method_cache_get_items(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);

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

/* 
 * External reference to TkAccessibilityObject hash table from tkAccessibility.c.
 * This is used to read accessible attributes without invoking Tcl.
 */
extern Tcl_HashTable *TkAccessibilityObject;

/*
 * D-Bus vtables.
 */

/*
 * org.a11y.atspi.Accessible interface -- minimal implementation.
 * Exposes only what Accerciser needs to recognize the application
 * without hanging. Hierarchy methods return empty/null responses.
 * GetInterfaces explicitly advertises what this object supports.
 */
static const sd_bus_vtable accessible_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Name", "s", dbus_prop_get_name, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Description", "s", dbus_prop_get_description, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Parent", "(so)", dbus_prop_get_parent, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ChildCount", "i", dbus_prop_get_child_count, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("GetRole", "", "u", dbus_method_get_role, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetState", "", "t", dbus_method_get_state, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetChildren", "", "a(so)", dbus_method_get_children, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetChildAtIndex", "i", "(so)", dbus_method_get_child_at_index, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetInterfaces", "", "as", dbus_method_get_interfaces, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetRoleName", "", "s", dbus_method_get_role_name, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetLocalizedRoleName", "", "s", dbus_method_get_localized_role_name, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetAttributes", "", "a{ss}", dbus_method_get_attributes, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetRelationSet", "", "a(ua(so))", dbus_method_get_relation_set, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

static const sd_bus_vtable cache_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetItems", "", "a((so)(so)a(so)assusau)", dbus_method_cache_get_items, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};


/*
 * org.a11y.atspi.Application interface - minimal version with Id property.
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
 * No Cache interface is registered. The Cache interface exists to make
 * accessible trees more efficient when there are many accessibles.
 * Since Tk has only one accessible (the application root), registering
 * Cache would only add unnecessary complexity and D-Bus traffic.
 * Accerciser will simply not find the interface and continue normally.
 */

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
    return sd_bus_message_append(reply, "(so)", "", ATSPI_DBUS_PATH_NULL);
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_children --
 *
 *   D-Bus method handler for GetChildren on the Accessible interface.
 *   Returns an empty array of accessible references because this object
 *   has no children. Accerciser expects this response immediately so its
 *   UI thread doesn't block waiting for a reply.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an empty array a(so).
 *----------------------------------------------------------------------
 */

static int
dbus_method_get_children(
    sd_bus_message *m,
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    sd_bus_message *reply = NULL;
    int r;
    DEBUG_LOG("dbus_method_get_children: returning empty children array");
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;
    r = sd_bus_message_open_container(reply, 'a', "(so)");
    if (r < 0) { sd_bus_message_unref(reply); return r; }
    r = sd_bus_message_close_container(reply);
    if (r < 0) { sd_bus_message_unref(reply); return r; }
    r = sd_bus_send(atspi_conn && atspi_conn->bus ? atspi_conn->bus : atspi_bus, reply, NULL);
    sd_bus_message_unref(reply);
    return r;
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_role_name --
 *
 *   D-Bus method handler for GetRoleName on the Accessible interface.
 *   Returns the localized role name string for the application role.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a string role name.
 *----------------------------------------------------------------------
 */

static int
dbus_method_get_role_name(
    sd_bus_message *m,
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    return sd_bus_reply_method_return(m, "s", "application");
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_localized_role_name --
 *
 *   D-Bus method handler for GetLocalizedRoleName on the Accessible interface.
 *   Returns the localized role name string for the application role.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a localized string role name.
 *----------------------------------------------------------------------
 */

static int
dbus_method_get_localized_role_name(
    sd_bus_message *m,
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    return sd_bus_reply_method_return(m, "s", "application");
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_attributes --
 *
 *   D-Bus method handler for GetAttributes on the Accessible interface.
 *   Returns an empty attribute dictionary because the application root
 *   has no accessible attributes.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an empty dictionary a{ss}.
 *----------------------------------------------------------------------
 */

static int
dbus_method_get_attributes(
    sd_bus_message *m,
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    sd_bus_message *reply = NULL;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;
    r = sd_bus_message_open_container(reply, 'a', "{ss}");
    if (r < 0) { sd_bus_message_unref(reply); return r; }
    r = sd_bus_message_close_container(reply);
    if (r < 0) { sd_bus_message_unref(reply); return r; }
    r = sd_bus_send(atspi_conn && atspi_conn->bus ? atspi_conn->bus : atspi_bus, reply, NULL);
    sd_bus_message_unref(reply);
    return r;
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_relation_set --
 *
 *   D-Bus method handler for GetRelationSet on the Accessible interface.
 *   Returns an empty relation set because the application root has no
 *   relations to other accessibles.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an empty array of relations.
 *----------------------------------------------------------------------
 */

static int
dbus_method_get_relation_set(
    sd_bus_message *m,
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    sd_bus_message *reply = NULL;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;
    r = sd_bus_message_open_container(reply, 'a', "(ua(so))");
    if (r < 0) { sd_bus_message_unref(reply); return r; }
    r = sd_bus_message_close_container(reply);
    if (r < 0) { sd_bus_message_unref(reply); return r; }
    r = sd_bus_send(atspi_conn && atspi_conn->bus ? atspi_conn->bus : atspi_bus, reply, NULL);
    sd_bus_message_unref(reply);
    return r;
}

/*
 *----------------------------------------------------------------------
 * dbus_method_cache_get_items --
 *
 *   D-Bus method handler for GetItems on the Cache interface.
 *   Returns an empty array of cache items. Accerciser calls this during
 *   initialization to batch-fetch the accessible tree; returning an
 *   empty array prevents the inspector's tree-building worker thread
 *   from hanging or blocking the main loop.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with an empty array of cache items.
 *----------------------------------------------------------------------
 */

static int
dbus_method_cache_get_items(
    sd_bus_message *m,
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    sd_bus_message *reply = NULL;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;
    r = sd_bus_message_open_container(reply, 'a', "((so)(so)a(so)assusau)");
    if (r < 0) { sd_bus_message_unref(reply); return r; }
    r = sd_bus_message_close_container(reply);
    if (r < 0) { sd_bus_message_unref(reply); return r; }
    r = sd_bus_send(atspi_conn && atspi_conn->bus ? atspi_conn->bus : atspi_bus, reply, NULL);
    sd_bus_message_unref(reply);
    return r;
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_child_at_index --
 *
 *   D-Bus method handler for GetChildAtIndex on the Accessible interface.
 *   Returns a null accessible reference because this object has no children.
 *   Accerciser queries this method when ChildCount returns 0; returning a
 *   null reference immediately prevents the inspector from hanging.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a null (so) tuple.
 *----------------------------------------------------------------------
 */

static int
dbus_method_get_child_at_index(
    sd_bus_message *m,
    TCL_UNUSED(void *),
    sd_bus_error *ret_error)
{
    return sd_bus_error_setf(ret_error,
        "org.a11y.atspi.Accessible.IndexOutOfBounds",
        "No child at index");
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_interfaces --
 *
 *   D-Bus method handler for GetInterfaces on the Accessible interface.
 *   Explicitly reports the interfaces supported by this accessible
 *   object. This lets libatspi know exactly what this object supports
 *   without needing to probe for each interface.
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
    sd_bus_message *m,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    sd_bus_message *reply = NULL;
    int r;

    DEBUG_LOG("dbus_method_get_interfaces: returning supported interfaces");

    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) {
        DEBUG_LOG("dbus_method_get_interfaces: new_method_return failed: %d", r);
        return r;
    }

    r = sd_bus_message_open_container(reply, 'a', "s");
    if (r < 0) {
        DEBUG_LOG("dbus_method_get_interfaces: open_container failed: %d", r);
        sd_bus_message_unref(reply);
        return r;
    }

    r = sd_bus_message_append(reply, "s", ATSPI_ACCESSIBLE_INTERFACE);
    if (r < 0) {
        DEBUG_LOG("dbus_method_get_interfaces: append Accessible failed: %d", r);
        sd_bus_message_unref(reply);
        return r;
    }

    r = sd_bus_message_append(reply, "s", ATSPI_APPLICATION_INTERFACE);
    if (r < 0) {
        DEBUG_LOG("dbus_method_get_interfaces: append Application failed: %d", r);
        sd_bus_message_unref(reply);
        return r;
    }

    r = sd_bus_message_close_container(reply);
    if (r < 0) {
        DEBUG_LOG("dbus_method_get_interfaces: close_container failed: %d", r);
        sd_bus_message_unref(reply);
        return r;
    }

    r = sd_bus_send(atspi_conn && atspi_conn->bus ? atspi_conn->bus : atspi_bus, reply, NULL);
    if (r < 0) {
        DEBUG_LOG("dbus_method_get_interfaces: sd_bus_send failed: %d", r);
    }

    sd_bus_message_unref(reply);
    return r;
}

/*
 *----------------------------------------------------------------------
 * dbus_method_get_state --
 *
 *   D-Bus method handler for GetState on the Accessible interface.
 *   Returns the state bitmask for the application root object.
 *
 * Results:
 *   Returns 0 on success, or a negative error code.
 *
 * Side effects:
 *   Sends a D-Bus reply message with a uint64 state mask.
 *----------------------------------------------------------------------
 */

static int
dbus_method_get_state(
    sd_bus_message *m,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    TkAccessible *acc = (TkAccessible *)userdata;
    uint64_t states = acc ? ComputeStateForWidget(acc) : 0;
    DEBUG_LOG("dbus_method_get_state: returning states 0x%lx", states);
    return sd_bus_reply_method_return(m, "t", states);
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
    const char *name = "Tk Application";

    if (!acc) {
        DEBUG_LOG("dbus_prop_get_name: no acc, returning default");
        return sd_bus_message_append(reply, "s", name);
    }

    if (acc->cached_name && acc->cached_name[0] != '\0') {
        name = acc->cached_name;
    }

    DEBUG_LOG("dbus_prop_get_name: returning '%s' for path %s", name, acc->dbus_path);
    return sd_bus_message_append(reply, "s", name);
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
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    /* The application object has no description; nothing to describe. */
    return sd_bus_message_append(reply, "s", "");
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
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    if (atspi_conn && atspi_conn->is_embedded &&
        atspi_conn->desktop_bus_name && atspi_conn->desktop_path) {
        return sd_bus_message_append(reply, "(so)",
            atspi_conn->desktop_bus_name, atspi_conn->desktop_path);
    }
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
    TCL_UNUSED(void *),
    TCL_UNUSED(sd_bus_error *))
{
    /* This object never has children. */
    int cnt = 0;
    
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
 * Speech-dispatcher connection used for all widget/focus announcements.
 * This is deliberately file-scope (rather than a local static inside
 * PostAccessibilityAnnouncement) so StopSpeech() can reach it too.
 */
static SPDConnection *spd_conn = NULL;
static char *pending_speech_msg = NULL;
static Tcl_TimerToken speech_timer = NULL;

/*
 *----------------------------------------------------------------------
 * PostAccessibilityAnnouncement --
 *
 *   Speak an announcement via speechd. This is the only channel Tk
 *   uses to tell a screen reader about widget names/focus/selection --
 *   it does not go through AT-SPI at all.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Opens the speechd connection on first use; speaks the message.
 *----------------------------------------------------------------------
 */

static void DelayedSpeechProc(TCL_UNUSED(ClientData)) {
    speech_timer = NULL;
    if (!pending_speech_msg) return;
    char *msg = pending_speech_msg;
    pending_speech_msg = NULL;
    if (!spd_conn) {
        spd_conn = spd_open("tk", "announce", NULL, SPD_MODE_THREADED);
        if (!spd_conn) {
            free(msg);
            return;
        }
    }
    spd_say(spd_conn, SPD_MESSAGE, msg);
    free(msg);
}

static void
PostAccessibilityAnnouncement(TCL_UNUSED(TkAccessible *),
                              const char *message)
{
    if (!message || !*message) return;
    if (pending_speech_msg) free(pending_speech_msg);
    pending_speech_msg = strdup(message);
    if (speech_timer) Tcl_DeleteTimerHandler(speech_timer);
    speech_timer = Tcl_CreateTimerHandler(1, DelayedSpeechProc, NULL);
}

/*
 *----------------------------------------------------------------------
 * StopSpeech --
 *
 *   Cut off any speech currently being spoken/queued and close the
 *   speechd connection. Called on shutdown -- either ours (Tk exiting)
 *   or the AT stack's (Orca/the a11y bus going away) -- so we never
 *   leave the screen reader talking about a Tk window that is gone,
 *   or leave a connection open that spd_say would otherwise still be
 *   able to speak through after nothing is listening.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Cancels pending/active speech and closes spd_conn.
 *----------------------------------------------------------------------
 */

static void
StopSpeech(void)
{
    if (pending_speech_msg) {
        free(pending_speech_msg);
        pending_speech_msg = NULL;
    }
    if (speech_timer) {
        Tcl_DeleteTimerHandler(speech_timer);
        speech_timer = NULL;
    }
    if (!spd_conn) {
        return;
    }
    spd_cancel(spd_conn);
    spd_close(spd_conn);
    spd_conn = NULL;
}

/*
 *----------------------------------------------------------------------
 * GetWmTitleForToplevel --
 *
 *   Get the window-manager title for a toplevel, read directly from
 *   the toplevel's internal WmInfo record via the TkWindow structure.
 *   Deliberately does not go through the "wm title" Tcl command or any
 *   other interp-based path, since callers here (accessible name/
 *   description/value lookups) run without -- and shouldn't need -- an
 *   active Tcl_Interp.
 *
 * Results:
 *   Returns a pointer to the title string owned by the toplevel's
 *   WmInfo (or, if no title was explicitly set, the toplevel's own Tk
 *   path name, mirroring what "wm title" itself falls back to). Returns
 *   NULL if tkwin is not a toplevel or has no WmInfo yet. The returned
 *   pointer is borrowed -- callers must not free it.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

static const char *
GetWmTitleForToplevel(
    Tk_Window tkwin)
{
    if (!tkwin || !Tk_IsTopLevel(tkwin)) {
        return NULL;
    }

    TkWindow *winPtr = (TkWindow *)tkwin;
    if (!winPtr->wmInfoPtr) {
        DEBUG_LOG("GetWmTitleForToplevel: toplevel has no wmInfoPtr yet");
        return NULL;
    }

    if (winPtr->wmInfoPtr->title && winPtr->wmInfoPtr->title[0] != '\0') {
        return winPtr->wmInfoPtr->title;
    }

    /* No explicit title set -- fall back to the toplevel's own path
     * name, same as "wm title" does internally. */
    return Tk_PathName(tkwin);
}

/*
 *----------------------------------------------------------------------
 * GetNameForWidget --
 *
 *   Get the accessible name for a widget.
 *
 * Results:
 *   Returns a pointer to a static string with the widget's name, or an
 *   empty string if no name is available.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

static const char *
GetNameForWidget(Tk_Window tkwin)
{
    if (!tkwin) {
        return "";
    }

    /* Toplevels report their wm title as the accessible name, ahead of
     * any explicitly-assigned accessibility name. */
    if (Tk_IsTopLevel(tkwin)) {
        const char *wmTitle = GetWmTitleForToplevel(tkwin);
        if (wmTitle) {
            DEBUG_LOG("GetNameForWidget: toplevel, using wm title '%s'", wmTitle);
            return wmTitle;
        }
    }

    /* First check TkAccessibilityObject hash for explicitly assigned name. */
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
                            DEBUG_LOG("GetNameForWidget: found explicit name '%s' in accessibility hash", name);
                            return name;
                        }
                    }
                }
            }
        }
    }

    /* Fall back to Tk path name */
    const char *pathName = Tk_PathName(tkwin);
    if (pathName && pathName[0] != '\0') {
        DEBUG_LOG("GetNameForWidget: using path name '%s'", pathName);
        return pathName;
    }

    return "Widget";
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

    /* Toplevels report their wm title as the accessible description.
     * strdup here, not a borrowed pointer -- UpdateFocusChain always
     * free()s whatever this function returns. */
    if (Tk_IsTopLevel(tkwin)) {
        const char *wmTitle = GetWmTitleForToplevel(tkwin);
        if (wmTitle) {
            DEBUG_LOG("GetDescriptionForWidget: toplevel, using wm title '%s'", wmTitle);
            return strdup(wmTitle);
        }
    }

    /* Guard against NULL TkAccessibilityObject to prevent crash. */
    if (!TkAccessibilityObject) {
        DEBUG_LOG("GetDescriptionForWidget: TkAccessibilityObject is NULL");
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

    /* Toplevels report their wm title as the accessible value. strdup
     * here, not a borrowed pointer -- UpdateFocusChain always free()s
     * whatever this function returns. */
    if (Tk_IsTopLevel(tkwin)) {
        const char *wmTitle = GetWmTitleForToplevel(tkwin);
        if (wmTitle) {
            DEBUG_LOG("GetValueForWidget: toplevel, using wm title '%s'", wmTitle);
            return strdup(wmTitle);
        }
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
    /* Only the root/application accessible exists, so this is trivial. */
    if (!acc) {
        DEBUG_LOG("GetLiveRole: null acc, returning invalid");
        return ATSPI_ROLE_INVALID;
    }
    DEBUG_LOG("GetLiveRole: returning APPLICATION for path %s", acc->dbus_path);
    return ATSPI_ROLE_APPLICATION;
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
    TCL_UNUSED(TkAccessible *))
{
    /* Static states for the one accessible we ever register. */
    uint64_t states = ATSPI_STATE_ENABLED | ATSPI_STATE_SHOWING |
        ATSPI_STATE_VISIBLE | ATSPI_STATE_FOCUSABLE;
    DEBUG_LOG("ComputeStateForWidget: states = 0x%lx", states);
    return states;
}

/*
 *----------------------------------------------------------------------
 * FreeAccessible --
 *
 *   Free the root TkAccessible object and release its resources.
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
    
    Tcl_Free(acc);
    DEBUG_LOG("FreeAccessible: freed");
}

/*
 *----------------------------------------------------------------------
 * UpdateFocusChain --
 *
 *   Speak an announcement for the newly focused widget via speechd.
 *   This has nothing to do with the AT-SPI accessible tree -- there is
 *   only ever the one root/application object on the bus -- it is
 *   purely a trigger for PostAccessibilityAnnouncement.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Speaks an announcement for the focused widget.
 *----------------------------------------------------------------------
 */

static void
UpdateFocusChain(
    Tk_Window focused)
{
    if (!focused) {
        DEBUG_LOG("UpdateFocusChain: no focus");
        return;
    }
    
    DEBUG_LOG("UpdateFocusChain: focus changed to %s", Tk_PathName(focused));
    
    /* Build announcement string from name, description, and value. */
    const char *name = GetNameForWidget(focused);

    /*
     * For a toplevel, GetNameForWidget/GetDescriptionForWidget/
     * GetValueForWidget all report the wm title -- that's correct
     * when each is queried independently, but concatenating all three
     * here would just repeat the same title three times in a single
     * spoken announcement. Skip desc/value for toplevels and announce
     * the title once.
     */
    char *desc = NULL;
    char *value = NULL;
    if (!Tk_IsTopLevel(focused)) {
        desc = GetDescriptionForWidget(focused);
        value = GetValueForWidget(focused);
    }
    
    char msg[5120] = "";
    int has_content = 0;
    
    /* Use name if available, otherwise fall back to widget class or path. */
    if (name && name[0]) {
        strcat(msg, name);
        has_content = 1;
    } else {
        /* Fallback: use widget class or path. */
        const char *class_name = Tk_Class(focused);
        if (class_name && class_name[0]) {
            strcat(msg, class_name);
            has_content = 1;
        } else {
            const char *path = Tk_PathName(focused);
            if (path && path[0]) {
                strcat(msg, path);
                has_content = 1;
            }
        }
    }
    
    /* 
     * This works best with static values like label text.
     * Dynamic data such as entry and text widget buffers
     * do not work well here, so we will process that data
     * at the script level by execing out to the command line
     * interface. 
     */
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
        PostAccessibilityAnnouncement(NULL, msg);
    } 
    
    if (desc) free(desc);
    if (value) free(value);
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
    
    sd_bus_set_method_call_timeout(session, 2 * 1000000ULL);
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
 *   the Socket.Embed method on the registry's Socket object.
 *   The registry object implements Socket.Embed, and the root accessible
 *   is passed as the argument (plug) to Embed.
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
     * Socket.Embed is exported by the AT-SPI registry's Socket object.
     * The registry object is at ATSPI_DBUS_PATH_REGISTRY, and the
     * Socket interface is ATSPI_SOCKET_INTERFACE.
     * The Embed method takes a (so) tuple: the application's bus name
     * and the root accessible object path.
     */
    sd_bus_set_method_call_timeout(atspi_conn->bus, 2 * 1000000ULL);
    r = sd_bus_call_method(
        atspi_conn->bus,
        "org.a11y.atspi.Registry",
        ATSPI_DBUS_PATH_REGISTRY,
        ATSPI_SOCKET_INTERFACE,
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
        sd_bus_unref(bus);
        Tcl_Free(atspi_conn);
        atspi_conn = NULL;
        return false;
    }
    memset(atspi_conn->root_accessible, 0, sizeof(TkAccessible));
    
    atspi_conn->root_accessible->role = ATSPI_ROLE_APPLICATION;
    atspi_conn->root_accessible->dbus_path = strdup(ATSPI_DBUS_PATH_ROOT);
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
                                  ATSPI_APPLICATION_INTERFACE,
                                  application_vtable,
                                  atspi_conn->root_accessible);
    if (r < 0 || !slot) {
        DEBUG_LOG("InitializeAtspiConnection: failed to register root Application vtable: %d", r);
        FreeAccessible(atspi_conn->root_accessible);
        atspi_conn->root_accessible = NULL;
        sd_bus_unref(bus);
        Tcl_Free(atspi_conn);
        atspi_conn = NULL;
        return false;
    }
    atspi_conn->root_accessible->vtable_slots[
        atspi_conn->root_accessible->n_vtable_slots++] = slot;
    DEBUG_LOG("InitializeAtspiConnection: root Application vtable registered");
    slot = NULL;
    r = sd_bus_add_object_vtable(atspi_conn->bus, &slot,
                                  atspi_conn->root_accessible->dbus_path,
                                  "org.a11y.atspi.Cache",
                                  cache_vtable,
                                  atspi_conn->root_accessible);
    if (r >= 0 && slot) {
        atspi_conn->root_accessible->vtable_slots[
            atspi_conn->root_accessible->n_vtable_slots++] = slot;
    }
    
    /* 
     * Registration must succeed for initialization to succeed.
     * Socket.Embed is called on the registry's Socket object, passing
     * our root accessible as the plug argument.
     */
    if (!EmbedWithRegistry()) {
        DEBUG_LOG("InitializeAtspiConnection: EmbedWithRegistry failed - initialization aborted");
        FreeAccessible(atspi_conn->root_accessible);
        atspi_conn->root_accessible = NULL;
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
 *   Drain pending AT-SPI D-Bus messages on atspi_bus and flush outgoing
 *   queue.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Processes pending D-Bus messages and flushes outgoing queue.
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
    int r;
    while ((r = sd_bus_process(atspi_bus, NULL)) > 0) {
        count++;
        if (count > 100) break;
    }
    if (r < 0) {
        atspi_draining = 0;
        if (!sd_bus_is_open(atspi_bus)) {
            StopSpeech();
            if (atspi_conn) atspi_conn->is_initialized = 0;
        }
        return;
    }

    
    if (count > 0) {
        DEBUG_LOG("TkWaylandAtspiProcessEvents: processed %d messages", count);
    }
    atspi_draining = 0;

    /*
     * AT-SPI gives us no direct "Orca exited" signal, but the a11y bus
     * itself going away is the best available proxy for the AT stack
     * (including Orca) having shut down -- the bus is provided for the
     * session's accessibility stack as a whole, not owned by us. If it
     * has dropped, there is no one left to hear us, so cut off any
     * speech immediately rather than leaving it queued/playing.
     */
    if (!sd_bus_is_open(atspi_bus)) {
        DEBUG_LOG("TkWaylandAtspiProcessEvents: a11y bus no longer open, stopping speech");
        StopSpeech();
        if (atspi_conn) {
            atspi_conn->is_initialized = 0;
        }
    }
}

/*
 *----------------------------------------------------------------------
 * AddAccessibleCmd --
 *
 *   Tcl command implementation for ::tk::accessible::add_acc_object.
 *   Individual toplevels are no longer given their own AT-SPI
 *   accessible object (only the application/root object is ever
 *   registered), so this is now a deliberate no-op -- kept only so
 *   accessibility.tcl's existing call sites don't need to change.
 *
 * Results:
 *   Returns TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *   None.
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
    
    DEBUG_LOG("AddAccessibleCmd: no-op (only the application object is registered with AT-SPI)");
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 * EmitSelectionChangedCmd --
 *
 *   Tcl command implementation for ::tk::accessible::emit_selection_change.
 *   Speaks a selection-changed announcement via speechd. 
 *
 * Results:
 *   Returns TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *   Speaks an announcement.
 *----------------------------------------------------------------------
 */

static int
EmitSelectionChangedCmd(
    void* clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "window");
        return TCL_ERROR;
    }
    
    Tk_Window tkwin = (Tk_Window)clientData;
    char *announcement = GetValueForWidget(tkwin);
    
    DEBUG_LOG("EmitSelectionChangedCmd: delegating to external tool like X11");
    PostAccessibilityAnnouncement(NULL, announcement);
    ckfree(announcement);
    
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
    int mask)
{
    if (mask & TCL_READABLE) {
        TkWaylandAtspiProcessEvents();
    }
    if (mask & TCL_WRITABLE) {
        if (atspi_bus) sd_bus_flush(atspi_bus);
    }
}

/*
 *----------------------------------------------------------------------
 * TkWaylandAccessibility_Finalize --
 *
 *   Tear down the AT-SPI connection and cut off any in-progress
 *   speech. Called automatically when Tk/the interpreter shuts down
 *   (via the Tcl exit handler registered in TkWaylandAccessibility_Init),
 *   and also reached indirectly when TkWaylandAtspiProcessEvents
 *   detects that the a11y bus itself has closed (the best available
 *   signal that the AT stack, including Orca, has gone away).
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Stops/closes the speechd connection, unregisters the root
 *   accessible's D-Bus vtables, closes the AT-SPI bus connection, and
 *   frees the connection state.
 *----------------------------------------------------------------------
 */

void
TkWaylandAccessibility_Finalize(void)
{
    DEBUG_LOG("TkWaylandAccessibility_Finalize: starting shutdown");

    /*
     * Always cut off speech first and unconditionally, regardless of
     * whether the AT-SPI side ever finished initializing -- a partially
     * initialized connection can still have spoken something via
     * PostAccessibilityAnnouncement.
     */
    StopSpeech();

    if (!atspi_conn) {
        DEBUG_LOG("TkWaylandAccessibility_Finalize: no connection to tear down");
        return;
    }

    if (atspi_bus) {
        int fd = sd_bus_get_fd(atspi_bus);
        if (fd >= 0) {
            Tcl_DeleteFileHandler(fd);
        }
    }

    if (atspi_conn->root_accessible) {
        FreeAccessible(atspi_conn->root_accessible);
        atspi_conn->root_accessible = NULL;
    }

    if (atspi_conn->desktop_bus_name) {
        free(atspi_conn->desktop_bus_name);
        atspi_conn->desktop_bus_name = NULL;
    }
    if (atspi_conn->desktop_path) {
        free(atspi_conn->desktop_path);
        atspi_conn->desktop_path = NULL;
    }

    if (atspi_conn->bus) {
        sd_bus_flush_close_unref(atspi_conn->bus);
        atspi_conn->bus = NULL;
    }
    atspi_bus = NULL;

    Tcl_Free(atspi_conn);
    atspi_conn = NULL;

    DEBUG_LOG("TkWaylandAccessibility_Finalize: shutdown complete");
}

/*
 *----------------------------------------------------------------------
 * AtspiExitProc --
 *
 *   Tcl exit-handler wrapper around TkWaylandAccessibility_Finalize,
 *   so the AT-SPI connection and any in-progress speech are torn down
 *   when Tk/the interpreter shuts down.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   See TkWaylandAccessibility_Finalize.
 *----------------------------------------------------------------------
 */

static void
AtspiExitProc(TCL_UNUSED(void *))
{
    DEBUG_LOG("AtspiExitProc: Tcl exit handler firing");
    TkWaylandAccessibility_Finalize();
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
 *   Initializes D-Bus connection, registers the application accessible
 *   object, and creates Tcl commands.
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
            Tcl_CreateFileHandler(fd, TCL_READABLE | TCL_WRITABLE | TCL_EXCEPTION, AtspiFileHandlerProc, NULL);
        } else {
            DEBUG_LOG("TkWaylandAccessibility_Init: failed to get bus fd");
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
    
    /*
     * Ensure speech is cut off and the AT-SPI connection torn down
     * cleanly when Tk/the interpreter exits.
     */
    Tcl_CreateExitHandler(AtspiExitProc, NULL);
    
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
