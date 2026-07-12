/*
 * tkWaylandSysTray.c --
 *
 *    Wayland system tray/notification icon support using the
 *    StatusNotifierItem protocol via sd-bus. Implements a Tcl command
 *    "::tk::systray::_systray" with subcommands "create", "configure", and
 *    "destroy" – modelled after the macOS implementation.
 *
 * Copyright © 2020-2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkWaylandInt.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>

/* SD-Bus includes. */
#include <systemd/sd-bus.h>
#include <systemd/sd-bus-protocol.h>

/* Status values. */
typedef enum {
    STATUS_PASSIVE = 0,
    STATUS_ACTIVE = 1,
    STATUS_NEEDS_ATTENTION = 2
} StatusDBus;

typedef enum {
    CATEGORY_APPLICATION_STATUS = 0,
    CATEGORY_COMMUNICATIONS = 1,
    CATEGORY_SYSTEM_SERVICES = 2,
    CATEGORY_HARDWARE = 3
} CategoryDBus;

/*
 * Data structure for the systray icon.
 */
typedef struct {
    Tcl_Interp *interp;
    Tk_Window tkwin;                  /* main window for image ops */

    /* D-Bus */
    sd_bus *bus;
    char *bus_name;
    char *object_path;
    sd_bus_slot *menuSlot;            /* slot for dbusmenu object (unused) */
    sd_bus_slot *watcherMatchSlot;    /* slot for watcher NameOwnerChanged match */
    Tcl_TimerToken busTimer;

    /* Icon data (ARGB32 network order for SNI) */
    unsigned char *iconArgb;
    int iconArgbW, iconArgbH;

    /* Tk image */
    Tk_Image image;
    int imageWidth, imageHeight;
    Tcl_Obj *imageObj;                /* name of the Tk image */

    /* Tooltip and title */
    char *tooltip;                    /* plain C string */
    char *title;                      /* application name */
    Tcl_Obj *tooltipObj;              /* for -text */

    /* Callbacks */
    Tcl_Obj *b1Command;
    Tcl_Obj *b3Command;

    /* Status */
    char *status;                     /* "active", "passive", "attention" */
    StatusDBus dbus_status;
    CategoryDBus category;

    int item_id;
    int redisplayPending;
    int item_is_menu;                 /* false - we don't implement a menu */
    Tcl_TimerToken refreshTimer;      /* timer for delayed property refresh */
} DockIcon;

/* Forward declarations. */
static int WaylandSystrayObjCmd(void *clientData, Tcl_Interp *interp,
                                Tcl_Size objc, Tcl_Obj *const objv[]);
static void WaylandSystrayDeleteProc(void *clientData);
static int CaptureIconPixmap(DockIcon *icon);
static void UpdateIndicatorIcon(DockIcon *icon);
static void UpdateIndicatorStatus(DockIcon *icon);
static void UpdateTooltip(DockIcon *icon);
static int RegisterStatusNotifierItem(DockIcon *icon);
static int RegisterWithWatcher(DockIcon *icon);
static int WatcherOwnerChanged(sd_bus_message *m, void *userdata,
                               sd_bus_error *ret_error);
static void UnregisterStatusNotifierItem(DockIcon *icon);
static void ProcessDBusEvents(void *clientData);
static void InvokeButtonCommand(DockIcon *icon, int button, int x, int y);
static void TrayIconRedisplayIdle(void *cd);
static void TrayIconImageChanged(void *cd, int x, int y, int w, int h,
                                 int imgw, int imgh);
static void RefreshProperties(void *clientData);

/* Global item ID counter. */
static int global_item_id = 0;

/*
 *----------------------------------------------------------------------
 *
 * SubstituteButtonPercents --
 *
 *    Expand %X, %Y, and %% in a button command string.
 *
 * Results:
 *    A new Tcl_Obj with the substituted command.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *
SubstituteButtonPercents(Tcl_Obj *cmdObj, int x, int y)
{
    const char *src = Tcl_GetString(cmdObj);
    const char *p = src, *span = src;
    Tcl_Obj *result = Tcl_NewObj();
    char buf[16];

    while (*p) {
        if (p[0] == '%' && (p[1] == 'X' || p[1] == 'Y' || p[1] == '%')) {
            if (p > span) Tcl_AppendToObj(result, span, (int)(p - span));
            switch (p[1]) {
                case 'X': snprintf(buf, sizeof(buf), "%d", x); break;
                case 'Y': snprintf(buf, sizeof(buf), "%d", y); break;
                case '%': strcpy(buf, "%"); break;
            }
            Tcl_AppendToObj(result, buf, -1);
            p += 2; span = p;
        } else p++;
    }
    if (p > span) Tcl_AppendToObj(result, span, (int)(p - span));
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * InvokeButtonCommand --
 *
 *    Execute the Tcl command bound to a button press.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    May evaluate a Tcl script; errors are backgrounded.
 *
 *----------------------------------------------------------------------
 */

static void
InvokeButtonCommand(DockIcon *icon, int button, int x, int y)
{
    Tcl_Obj *cmdObj = (button == 1) ? icon->b1Command : icon->b3Command;
    if (!cmdObj) return;

    /* Exclude mouse wheel / scroll events. */
    if (button == 0) return;

    Tcl_Obj *script = SubstituteButtonPercents(cmdObj, x, y);
    Tcl_IncrRefCount(script);
    int result = Tcl_EvalObjEx(icon->interp, script, TCL_EVAL_GLOBAL);
    if (result != TCL_OK) Tcl_BackgroundError(icon->interp);
    Tcl_DecrRefCount(script);
}

/*
 *----------------------------------------------------------------------
 *
 * method_activate --
 *
 *    D-Bus method handler for "Activate" (primary button click).
 *
 * Results:
 *    Standard sd-bus return code.
 *
 * Side effects:
 *    Invokes the button-1 command.
 *
 *----------------------------------------------------------------------
 */
static int method_activate(sd_bus_message *m, void *userdata,
                           sd_bus_error *ret_error) {
    DockIcon *icon = (DockIcon *)userdata;
    int x = 0, y = 0;
    sd_bus_message_read(m, "ii", &x, &y);
    fprintf(stderr, "[tkSNI] Activate (left-click) at %d,%d\n", x, y);
    InvokeButtonCommand(icon, 1, x, y);
    return sd_bus_reply_method_return(m, "");
}

/*
 *----------------------------------------------------------------------
 *
 * method_secondary_activate --
 *
 *    D-Bus method handler for "SecondaryActivate" (right‑click or middle‑click).
 *
 * Results:
 *    Standard sd-bus return code.
 *
 * Side effects:
 *    Invokes the button-3 command.
 *
 *----------------------------------------------------------------------
 */

static int method_secondary_activate(sd_bus_message *m, void *userdata,
                                     sd_bus_error *ret_error) {
    DockIcon *icon = (DockIcon *)userdata;
    int x = 0, y = 0;
    sd_bus_message_read(m, "ii", &x, &y);
    fprintf(stderr, "[tkSNI] SecondaryActivate (right/middle) at %d,%d\n", x, y);
    InvokeButtonCommand(icon, 3, x, y);
    return sd_bus_reply_method_return(m, "");
}

/*
 *----------------------------------------------------------------------
 *
 * method_context_menu --
 *
 *    D-Bus method handler for "ContextMenu" (request to show context menu).
 *    Treat as SecondaryActivate per user request.
 *
 * Results:
 *    Standard sd-bus return code.
 *
 * Side effects:
 *    Invokes the button-3 command.
 *
 *----------------------------------------------------------------------
 */

static int method_context_menu(sd_bus_message *m, void *userdata,
                               sd_bus_error *ret_error) {
    fprintf(stderr, "[tkSNI] ContextMenu called\n");
    return method_secondary_activate(m, userdata, ret_error);
}

/*
 *----------------------------------------------------------------------
 *
 * method_scroll --
 *
 *    D-Bus method handler for "Scroll" (mouse wheel events).
 *    Explicitly ignore wheel events.
 *
 * Results:
 *    Standard sd-bus return code (currently not implemented).
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static int method_scroll(sd_bus_message *m, void *userdata,
                         sd_bus_error *ret_error) {
    /* Scroll not implemented. */
    return sd_bus_reply_method_return(m, "");
}

/*
 *----------------------------------------------------------------------
 *
 * method_provide_xdg_activation --
 *
 *    D-Bus method handler for "ProvideXdgActivationToken" (no-op).
 *    This satisfies the GNOME extension's modern activation requirements.
 *
 * Results:
 *    Standard sd-bus return code.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static int method_provide_xdg_activation(sd_bus_message *m, void *userdata,
                                         sd_bus_error *ret_error) {
    const char *token = NULL;
    sd_bus_message_read(m, "s", &token);
    fprintf(stderr, "[tkSNI] ProvideXdgActivationToken (no-op) token: %s\n", token ? token : "(null)");
    return sd_bus_reply_method_return(m, "");
}

/*
 *----------------------------------------------------------------------
 *
 * property_get_category --
 *
 *    D-Bus property getter for "Category".
 *
 * Results:
 *    Standard sd-bus return code; appends the category string.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static int property_get_category(sd_bus *bus, const char *path,
                                 const char *interface, const char *property,
                                 sd_bus_message *reply, void *userdata,
                                 sd_bus_error *ret_error) {
    DockIcon *icon = (DockIcon *)userdata;
    const char *cat = "ApplicationStatus";
    switch (icon->category) {
        case CATEGORY_COMMUNICATIONS: cat = "Communications"; break;
        case CATEGORY_SYSTEM_SERVICES: cat = "SystemServices"; break;
        case CATEGORY_HARDWARE: cat = "Hardware"; break;
        default: break;
    }
    return sd_bus_message_append(reply, "s", cat);
}

/*
 *----------------------------------------------------------------------
 *
 * property_get_status --
 *
 *    D-Bus property getter for "Status".
 *
 * Results:
 *    Standard sd-bus return code; appends the status string.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static int property_get_status(sd_bus *bus, const char *path,
                               const char *interface, const char *property,
                               sd_bus_message *reply, void *userdata,
                               sd_bus_error *ret_error) {
    DockIcon *icon = (DockIcon *)userdata;
    const char *status = "Active";
    switch (icon->dbus_status) {
        case STATUS_PASSIVE: status = "Passive"; break;
        case STATUS_NEEDS_ATTENTION: status = "NeedsAttention"; break;
        default: break;
    }
    return sd_bus_message_append(reply, "s", status);
}

/*
 *----------------------------------------------------------------------
 *
 * property_get_icon_name --
 *
 *    D-Bus property getter for "IconName" (returns empty string).
 *
 * Results:
 *    Standard sd-bus return code; appends an empty string.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static int property_get_icon_name(sd_bus *bus, const char *path,
                                  const char *interface, const char *property,
                                  sd_bus_message *reply, void *userdata,
                                  sd_bus_error *ret_error) {
    return sd_bus_message_append(reply, "s", "");
}

/*
 *----------------------------------------------------------------------
 *
 * property_get_icon_pixmap --
 *
 *    D-Bus property getter for "IconPixmap" – returns ARGB32 pixmap data.
 *
 * Results:
 *    Standard sd-bus return code; appends the pixmap array if available.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static int property_get_icon_pixmap(sd_bus *bus, const char *path,
                                    const char *interface, const char *property,
                                    sd_bus_message *reply, void *userdata,
                                    sd_bus_error *ret_error) {
    DockIcon *icon = (DockIcon *)userdata;
    int r;
    r = sd_bus_message_open_container(reply, 'a', "(iiay)");
    if (r < 0) return r;
    if (icon->iconArgb && icon->iconArgbW > 0 && icon->iconArgbH > 0) {
        r = sd_bus_message_open_container(reply, 'r', "iiay");
        if (r < 0) return r;
        r = sd_bus_message_append(reply, "ii", icon->iconArgbW, icon->iconArgbH);
        if (r < 0) return r;
        r = sd_bus_message_append_array(reply, 'y', icon->iconArgb,
                                        icon->iconArgbW * icon->iconArgbH * 4);
        if (r < 0) return r;
        sd_bus_message_close_container(reply);
    }
    sd_bus_message_close_container(reply);
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * property_get_tooltip --
 *
 *    D-Bus property getter for "ToolTip".
 *
 * Results:
 *    Standard sd-bus return code; appends the tooltip structure.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static int property_get_tooltip(sd_bus *bus, const char *path,
                                const char *interface, const char *property,
                                sd_bus_message *reply, void *userdata,
                                sd_bus_error *ret_error) {
    DockIcon *icon = (DockIcon *)userdata;
    const char *title = icon->title ? icon->title : "";
    const char *tip = icon->tooltip ? icon->tooltip : "";
    int r;

    r = sd_bus_message_open_container(reply, 'r', "sa(iiay)ss");
    if (r < 0) return r;
    r = sd_bus_message_append(reply, "s", "");
    if (r < 0) return r;

    r = sd_bus_message_open_container(reply, 'a', "(iiay)");
    if (r < 0) return r;
    sd_bus_message_close_container(reply);

    r = sd_bus_message_append(reply, "ss", title, tip);
    if (r < 0) return r;
    sd_bus_message_close_container(reply);
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * property_get_menu --
 *
 *    D-Bus property getter for "Menu". Returns special value
 *    "/NO_DBUSMENU" to tell the GNOME extension that we support
 *    ContextMenu/SecondaryActivate but don't implement a full DBusMenu.
 *
 * Results:
 *    Standard sd-bus return code; appends the object path.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static int property_get_menu(sd_bus *bus, const char *path,
                             const char *interface, const char *property,
                             sd_bus_message *reply, void *userdata,
                             sd_bus_error *ret_error) {
    /* Tell GNOME extension: no DBusMenu, but we still support
     * ContextMenu/SecondaryActivate */
    return sd_bus_message_append(reply, "o", "/NO_DBUSMENU");
}

/*
 *----------------------------------------------------------------------
 *
 * property_get_item_is_menu --
 *
 *    D-Bus property getter for "ItemIsMenu".
 *
 * Results:
 *    Standard sd-bus return code; appends false.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static int property_get_item_is_menu(sd_bus *bus, const char *path,
                                     const char *interface, const char *property,
                                     sd_bus_message *reply, void *userdata,
                                     sd_bus_error *ret_error) {
    DockIcon *icon = (DockIcon *)userdata;
    return sd_bus_message_append(reply, "b", icon->item_is_menu);
}

/* VTable for StatusNotifierItem with Menu stub */
static const sd_bus_vtable status_notifier_item_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Activate", "ii", "", method_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SecondaryActivate", "ii", "", method_secondary_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ContextMenu", "ii", "", method_context_menu, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Scroll", "is", "", method_scroll, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ProvideXdgActivationToken", "s", "", method_provide_xdg_activation, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Category", "s", property_get_category, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Id", "s", NULL, offsetof(DockIcon, title), SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Title", "s", NULL, offsetof(DockIcon, title), SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Status", "s", property_get_status, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("WindowId", "i", NULL, offsetof(DockIcon, item_id), SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("IconName", "s", property_get_icon_name, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("IconPixmap", "a(iiay)", property_get_icon_pixmap, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("ToolTip", "(sa(iiay)ss)", property_get_tooltip, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Menu", "o", property_get_menu, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ItemIsMenu", "b", property_get_item_is_menu, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END
};

/*
 *----------------------------------------------------------------------
 *
 * RefreshProperties --
 *
 *    Timer callback to re-emit property change signals after registration.
 *    This helps ensure the GNOME extension picks up all properties
 *    after its proxy initialization is complete.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Re-emits NewIcon, NewStatus, and NewToolTip signals.
 *
 *----------------------------------------------------------------------
 */

static void
RefreshProperties(void *clientData) {
    DockIcon *icon = (DockIcon *)clientData;
    icon->refreshTimer = NULL;
    if (icon->bus) {
        UpdateIndicatorIcon(icon);
        UpdateIndicatorStatus(icon);
        UpdateTooltip(icon);
        fprintf(stderr, "[tkSNI] Refreshed properties after registration delay\n");
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ProcessDBusEvents --
 *
 *    Timer callback to process pending D‑Bus messages.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Processes all pending D‑Bus messages; reschedules itself.
 *
 *----------------------------------------------------------------------
 */

static void
ProcessDBusEvents(void *clientData) {
    DockIcon *icon = (DockIcon *)clientData;
    if (icon->bus) {
        while (sd_bus_process(icon->bus, NULL) > 0) { /* process all */ }
        icon->busTimer = Tcl_CreateTimerHandler(50, ProcessDBusEvents, icon);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CaptureIconPixmap --
 *
 *    Capture the Tk photo pixels into the icon's ARGB32 buffer.
 *
 * Results:
 *    Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *    Allocates and fills icon->iconArgb; frees any previous buffer.
 *
 *----------------------------------------------------------------------
 */

static int
CaptureIconPixmap(DockIcon *icon) {
    if (!icon->image || !icon->imageObj) return 0;
    Tk_SizeOfImage(icon->image, &icon->imageWidth, &icon->imageHeight);
    if (icon->imageWidth <= 0 || icon->imageHeight <= 0) return 0;

    Tk_PhotoHandle photo = Tk_FindPhoto(icon->interp, Tcl_GetString(icon->imageObj));
    if (!photo) return 0;

    Tk_PhotoImageBlock block;
    Tk_PhotoGetImage(photo, &block);
    int w = icon->imageWidth, h = icon->imageHeight;
    unsigned char *argb = (unsigned char *)Tcl_Alloc(w * h * 4);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned char *src = block.pixelPtr + y * block.pitch + x * block.pixelSize;
            unsigned char *dst = argb + (y * w + x) * 4;
            dst[0] = (block.pixelSize >= 4) ? src[block.offset[3]] : 255; /* A */
            dst[1] = src[block.offset[0]]; /* R */
            dst[2] = src[block.offset[1]]; /* G */
            dst[3] = src[block.offset[2]]; /* B */
        }
    }

    if (icon->iconArgb) Tcl_Free((char *)icon->iconArgb);
    icon->iconArgb = argb;
    icon->iconArgbW = w;
    icon->iconArgbH = h;
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateIndicatorIcon --
 *
 *    Emit the "NewIcon" D‑Bus signal to notify that the icon image changed.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Sends a D‑Bus signal.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateIndicatorIcon(DockIcon *icon) {
    if (!icon->bus) return;
    sd_bus_emit_signal(icon->bus, icon->object_path,
                       "org.kde.StatusNotifierItem", "NewIcon", "");
    sd_bus_flush(icon->bus);
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateIndicatorStatus --
 *
 *    Emit the "NewStatus" D‑Bus signal to notify that the status changed.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Sends a D‑Bus signal.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateIndicatorStatus(DockIcon *icon) {
    if (!icon->bus) return;
    const char *status_str = "Active";
    if (icon->status) {
        if (strcmp(icon->status, "passive") == 0) {
            icon->dbus_status = STATUS_PASSIVE; status_str = "Passive";
        } else if (strcmp(icon->status, "attention") == 0) {
            icon->dbus_status = STATUS_NEEDS_ATTENTION; status_str = "NeedsAttention";
        } else {
            icon->dbus_status = STATUS_ACTIVE; status_str = "Active";
        }
    } else {
        icon->dbus_status = STATUS_ACTIVE;
    }
    sd_bus_emit_signal(icon->bus, icon->object_path,
                       "org.kde.StatusNotifierItem", "NewStatus", "s", status_str);
    sd_bus_flush(icon->bus);
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateTooltip --
 *
 *    Emit the "NewToolTip" D‑Bus signal to notify that the tooltip changed,
 *    and also emit a properties changed signal for GNOME.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Sends D‑Bus signals.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateTooltip(DockIcon *icon) {
    if (!icon->bus) return;
    sd_bus_emit_signal(icon->bus, icon->object_path,
                       "org.kde.StatusNotifierItem", "NewToolTip", "");
    sd_bus_emit_properties_changed(icon->bus, icon->object_path,
                                   "org.kde.StatusNotifierItem", "ToolTip", NULL);
    sd_bus_flush(icon->bus);
}

/*
 *----------------------------------------------------------------------
 *
 * RegisterWithWatcher --
 *
 *    Call RegisterStatusNotifierItem on org.kde.StatusNotifierWatcher to
 *    (re-)announce our item to whichever host currently owns that name.
 *    Split out from RegisterStatusNotifierItem so it can be re-run
 *    whenever the watcher's bus ownership changes -- see
 *    WatcherOwnerChanged. On GNOME, the watcher is provided by a
 *    Shell extension (e.g. AppIndicator/KStatusNotifierItem Support)
 *    rather than a stable system service, and that extension's
 *    component can be reloaded (screen lock, extension updates,
 *    suspend/resume, etc.), silently forgetting any items registered
 *    with the previous instance. Re-registering when a new owner
 *    appears is what keeps the icon from vanishing in that case.
 *
 * Results:
 *    Returns 0 on success, negative error code on failure.
 *
 * Side effects:
 *    Sends a D-Bus method call; may log to stderr on failure.
 *
 *----------------------------------------------------------------------
 */

static int
RegisterWithWatcher(DockIcon *icon) {
    if (!icon->bus || !icon->bus_name) return -EINVAL;

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    int r = sd_bus_call_method(icon->bus,
                               "org.kde.StatusNotifierWatcher",
                               "/StatusNotifierWatcher",
                               "org.kde.StatusNotifierWatcher",
                               "RegisterStatusNotifierItem",
                               &error, &m, "s", icon->bus_name);
    if (r < 0) {
        fprintf(stderr, "[tkSNI] Failed to register with watcher: %s\n", error.message);
        sd_bus_error_free(&error);
        return r;
    }
    sd_bus_message_unref(m);
    sd_bus_flush(icon->bus);
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * WatcherOwnerChanged --
 *
 *    Signal handler for org.freedesktop.DBus's NameOwnerChanged,
 *    matched to fire only for org.kde.StatusNotifierWatcher. When a
 *    new (non-empty) owner takes that name -- i.e. the watcher just
 *    started or restarted -- re-announce our item to it, since a new
 *    watcher instance has no memory of previous registrations.
 *
 * Results:
 *    Standard sd-bus return code.
 *
 * Side effects:
 *    May re-send the RegisterStatusNotifierItem call to the watcher.
 *
 *----------------------------------------------------------------------
 */

static int
WatcherOwnerChanged(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    DockIcon *icon = (DockIcon *)userdata;
    const char *name = NULL, *old_owner = NULL, *new_owner = NULL;
    int r = sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner);
    if (r < 0) return r;

    if (new_owner && new_owner[0] != '\0') {
        fprintf(stderr, "[tkSNI] Watcher restarted, re-registering\n");
        RegisterWithWatcher(icon);
        /* Refresh properties after watcher restart */
        if (!icon->refreshTimer) {
            icon->refreshTimer = Tcl_CreateTimerHandler(300, (Tcl_TimerProc *)RefreshProperties, icon);
        }
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * RegisterStatusNotifierItem --
 *
 *    Register the StatusNotifierItem on the session bus.
 *
 * Results:
 *    Returns 0 on success, negative error code on failure.
 *
 * Side effects:
 *    Acquires a D‑Bus name, adds the SNI vtable, registers with the
 *    watcher, and subscribes to watcher restarts so we can re-register.
 *
 *----------------------------------------------------------------------
 */

static int
RegisterStatusNotifierItem(DockIcon *icon) {
    int r;
    if (!icon->bus) {
        r = sd_bus_open_user(&icon->bus);
        if (r < 0) {
            fprintf(stderr, "[tkSNI] Failed to connect to session bus: %s\n", strerror(-r));
            return r;
        }
    }

    if (!icon->object_path) {
        icon->object_path = (char *)Tcl_Alloc(64);
        snprintf(icon->object_path, 64, "/StatusNotifierItem");
    }
    if (!icon->bus_name) {
        icon->bus_name = (char *)Tcl_Alloc(128);
        snprintf(icon->bus_name, 128, "org.kde.StatusNotifierItem-%d-%d",
                 getpid(), icon->item_id);
    }

    r = sd_bus_request_name(icon->bus, icon->bus_name,
                            SD_BUS_NAME_REPLACE_EXISTING);
    if (r < 0) {
        fprintf(stderr, "[tkSNI] Failed to request bus name %s: %s\n",
                icon->bus_name, strerror(-r));
        return r;
    }

    /* Export the StatusNotifierItem vtable. */
    r = sd_bus_add_object_vtable(icon->bus, NULL, icon->object_path,
                                 "org.kde.StatusNotifierItem",
                                 status_notifier_item_vtable, icon);
    if (r < 0) {
        fprintf(stderr, "[tkSNI] Failed to add SNI vtable: %s\n", strerror(-r));
        return r;
    }

    /* Watch for the watcher restarting (common on GNOME, where it's
     * implemented by a Shell extension rather than a system service)
     * so we can re-register with whichever instance owns the name.
     */
    r = sd_bus_add_match(icon->bus, &icon->watcherMatchSlot,
                         "type='signal',"
                         "sender='org.freedesktop.DBus',"
                         "path='/org/freedesktop/DBus',"
                         "interface='org.freedesktop.DBus',"
                         "member='NameOwnerChanged',"
                         "arg0='org.kde.StatusNotifierWatcher'",
                         WatcherOwnerChanged, icon);
    if (r < 0) {
        fprintf(stderr, "[tkSNI] Failed to add watcher-restart match: %s\n", strerror(-r));
        /* Not fatal -- we still work, just won't self-heal if the
         * watcher restarts. */
    }

    /* Initial registration with the watcher. */
    r = RegisterWithWatcher(icon);
    if (r < 0) {
        return r;
    }

    /* Start the event processing timer. */
    icon->busTimer = Tcl_CreateTimerHandler(50, ProcessDBusEvents, icon);

    /* Schedule property refreshes at staggered intervals to ensure the
     * GNOME extension picks up all properties after proxy init.
     */
    icon->refreshTimer = Tcl_CreateTimerHandler(300, (Tcl_TimerProc *)RefreshProperties, icon);
    /* Extra refreshes for tooltip which is particularly finicky on GNOME */
    Tcl_CreateTimerHandler(1200, (Tcl_TimerProc *)UpdateTooltip, icon);
    Tcl_CreateTimerHandler(1500, (Tcl_TimerProc *)UpdateTooltip, icon);

    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * UnregisterStatusNotifierItem --
 *
 *    Unregister and clean up D‑Bus resources.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Releases the bus name, closes and unrefs the bus connection.
 *
 *----------------------------------------------------------------------
 */

static void
UnregisterStatusNotifierItem(DockIcon *icon) {
    if (icon->refreshTimer) {
        Tcl_DeleteTimerHandler(icon->refreshTimer);
        icon->refreshTimer = NULL;
    }
    if (icon->busTimer) {
        Tcl_DeleteTimerHandler(icon->busTimer);
        icon->busTimer = NULL;
    }
    if (icon->menuSlot) {
        sd_bus_slot_unref(icon->menuSlot);
        icon->menuSlot = NULL;
    }
    if (icon->watcherMatchSlot) {
        sd_bus_slot_unref(icon->watcherMatchSlot);
        icon->watcherMatchSlot = NULL;
    }
    if (!icon->bus) return;
    sd_bus_release_name(icon->bus, icon->bus_name);
    sd_bus_flush(icon->bus);
    sd_bus_close(icon->bus);
    sd_bus_unref(icon->bus);
    icon->bus = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TrayIconRedisplayIdle --
 *
 *    Idle callback to update the icon after a change in the Tk image.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Captures the new pixmap and emits a signal.
 *
 *----------------------------------------------------------------------
 */

static void
TrayIconRedisplayIdle(void *cd) {
    DockIcon *icon = (DockIcon *)cd;
    icon->redisplayPending = 0;
    if (icon->image && icon->bus) {
        CaptureIconPixmap(icon);
        UpdateIndicatorIcon(icon);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TrayIconImageChanged --
 *
 *    Callback invoked when the Tk image is modified or resized.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Schedules an idle update of the tray icon.
 *
 *----------------------------------------------------------------------
 */

static void
TrayIconImageChanged(void *cd, int x, int y, int w, int h, int imgw, int imgh) {
    DockIcon *icon = (DockIcon *)cd;
    if (imgw <= 0 || imgh <= 0) return;
    icon->imageWidth = imgw;
    icon->imageHeight = imgh;
    if (!icon->redisplayPending) {
        icon->redisplayPending = 1;
        Tcl_DoWhenIdle(TrayIconRedisplayIdle, icon);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * WaylandSystrayObjCmd --
 *
 *    Implements the _systray command with subcommands:
 *      create -image -text -button1 -button3
 *      configure option value
 *      destroy
 *
 * Results:
 *    Standard Tcl command return value (TCL_OK or TCL_ERROR).
 *
 * Side effects:
 *    Creates, modifies, or destroys a systray icon; may evaluate Tcl scripts.
 *
 *----------------------------------------------------------------------
 */

static int
WaylandSystrayObjCmd(void *clientData, Tcl_Interp *interp,
                     Tcl_Size objc, Tcl_Obj *const objv[]) {
    DockIcon **iconPtr = (DockIcon **)clientData;
    DockIcon *icon = *iconPtr;

    static const char *subcmds[] = {"create", "configure", "destroy", NULL};
    enum {CMD_CREATE, CMD_CONFIGURE, CMD_DESTROY} idx;
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "create | configure | destroy");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], subcmds, "subcommand", 0,
                            (int *)&idx) != TCL_OK)
        return TCL_ERROR;

    switch (idx) {
    case CMD_CREATE: {
        if (icon) {
            Tcl_AppendResult(interp, "only one systray icon per interpreter", NULL);
            return TCL_ERROR;
        }
        if (objc != 6) {
            Tcl_WrongNumArgs(interp, 2, objv, "image text button1 button3");
            return TCL_ERROR;
        }

        icon = (DockIcon *)Tcl_Alloc(sizeof(DockIcon));
        memset(icon, 0, sizeof(DockIcon));
        *iconPtr = icon;
        icon->interp = interp;
        icon->tkwin = Tk_MainWindow(interp);
        if (!icon->tkwin) {
            Tcl_AppendResult(interp, "no main window", NULL);
            Tcl_Free(icon); *iconPtr = NULL;
            return TCL_ERROR;
        }

        /* Set default title from window name. */
        const char *winName = Tk_Name(icon->tkwin);
        icon->title = (char *)Tcl_Alloc(strlen(winName) + 1);
        strcpy(icon->title, winName);

        icon->status = (char *)Tcl_Alloc(strlen("active") + 1);
        strcpy(icon->status, "active");
        icon->dbus_status = STATUS_ACTIVE;
        icon->category = CATEGORY_APPLICATION_STATUS;
        icon->item_id = ++global_item_id;
        icon->menuSlot = NULL;
        icon->item_is_menu = 0;  /* we don't implement a menu */
        icon->refreshTimer = NULL;

        /* Store image name. */
        icon->imageObj = objv[2];
        Tcl_IncrRefCount(icon->imageObj);
        icon->image = Tk_GetImage(interp, icon->tkwin,
                                  Tcl_GetString(icon->imageObj),
                                  TrayIconImageChanged, icon);
        if (!icon->image) {
            Tcl_AppendResult(interp, "invalid image name", NULL);
            WaylandSystrayDeleteProc(iconPtr);
            return TCL_ERROR;
        }
        CaptureIconPixmap(icon);

        /* Tooltip. */
        icon->tooltip = (char *)Tcl_Alloc(strlen(Tcl_GetString(objv[3])) + 1);
        strcpy(icon->tooltip, Tcl_GetString(objv[3]));

        /* Callbacks. */
        if (objv[4] && Tcl_GetString(objv[4])[0] != '\0') {
            icon->b1Command = objv[4];
            Tcl_IncrRefCount(icon->b1Command);
        }
        if (objv[5] && Tcl_GetString(objv[5])[0] != '\0') {
            icon->b3Command = objv[5];
            Tcl_IncrRefCount(icon->b3Command);
        }

        /* Now register the StatusNotifierItem. */
        if (RegisterStatusNotifierItem(icon) < 0) {
            Tcl_AppendResult(interp, "failed to register StatusNotifierItem", NULL);
            WaylandSystrayDeleteProc(iconPtr);
            return TCL_ERROR;
        }

        return TCL_OK;
    }

    case CMD_CONFIGURE: {
        if (!icon) {
            Tcl_AppendResult(interp, "systray icon does not exist", NULL);
            return TCL_ERROR;
        }
        if (objc != 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "option value");
            return TCL_ERROR;
        }
        const char *option = Tcl_GetString(objv[2]);
        Tcl_Obj *value = objv[3];
        if (strcmp(option, "-image") == 0) {
            /* Release old image. */
            if (icon->image) {
                Tk_FreeImage(icon->image);
                icon->image = NULL;
            }
            if (icon->imageObj) {
                Tcl_DecrRefCount(icon->imageObj);
                icon->imageObj = NULL;
            }
            icon->imageObj = value;
            Tcl_IncrRefCount(icon->imageObj);
            icon->image = Tk_GetImage(icon->interp, icon->tkwin,
                                      Tcl_GetString(icon->imageObj),
                                      TrayIconImageChanged, icon);
            if (!icon->image) {
                Tcl_AppendResult(interp, "invalid image name", NULL);
                return TCL_ERROR;
            }
            CaptureIconPixmap(icon);
            UpdateIndicatorIcon(icon);
        } else if (strcmp(option, "-text") == 0) {
            if (icon->tooltip) Tcl_Free(icon->tooltip);
            icon->tooltip = (char *)Tcl_Alloc(strlen(Tcl_GetString(value)) + 1);
            strcpy(icon->tooltip, Tcl_GetString(value));
            UpdateTooltip(icon);
        } else if (strcmp(option, "-button1") == 0) {
            if (icon->b1Command) Tcl_DecrRefCount(icon->b1Command);
            icon->b1Command = value;
            Tcl_IncrRefCount(icon->b1Command);
        } else if (strcmp(option, "-button3") == 0) {
            if (icon->b3Command) Tcl_DecrRefCount(icon->b3Command);
            icon->b3Command = value;
            Tcl_IncrRefCount(icon->b3Command);
        } else {
            Tcl_AppendResult(interp, "unknown option: must be -image, -text, -button1, or -button3", NULL);
            return TCL_ERROR;
        }
        return TCL_OK;
    }

    case CMD_DESTROY: {
        if (!icon) {
            Tcl_AppendResult(interp, "systray icon does not exist", NULL);
            return TCL_ERROR;
        }
        WaylandSystrayDeleteProc(iconPtr);
        *iconPtr = NULL;
        return TCL_OK;
    }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WaylandSystrayDeleteProc --
 *
 *    Free all resources held by the systray icon.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Releases all allocated memory and D‑Bus resources.
 *
 *----------------------------------------------------------------------
 */

static void
WaylandSystrayDeleteProc(void *clientData) {
    DockIcon **iconPtr = (DockIcon **)clientData;
    DockIcon *icon = *iconPtr;
    if (!icon) return;

    UnregisterStatusNotifierItem(icon);

    if (icon->image) Tk_FreeImage(icon->image);
    if (icon->imageObj) Tcl_DecrRefCount(icon->imageObj);
    if (icon->iconArgb) Tcl_Free((char *)icon->iconArgb);
    if (icon->b1Command) Tcl_DecrRefCount(icon->b1Command);
    if (icon->b3Command) Tcl_DecrRefCount(icon->b3Command);
    if (icon->tooltip) Tcl_Free(icon->tooltip);
    if (icon->title) Tcl_Free(icon->title);
    if (icon->status) Tcl_Free(icon->status);
    if (icon->bus_name) Tcl_Free(icon->bus_name);
    if (icon->object_path) Tcl_Free(icon->object_path);

    Tcl_Free(icon);
    *iconPtr = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Tktray_Init --
 *
 *    Create the ::tk::systray::_systray command.
 *
 * Results:
 *    Standard Tcl initialization return value.
 *
 * Side effects:
 *    Creates a new Tcl command.
 *
 *----------------------------------------------------------------------
 */

int
Tktray_Init(Tcl_Interp *interp) {
    DockIcon **iconPtr = (DockIcon **)Tcl_Alloc(sizeof(DockIcon *));
    *iconPtr = NULL;
    Tcl_CreateObjCommand2(interp, "::tk::systray::_systray",
                          WaylandSystrayObjCmd, iconPtr,
                          WaylandSystrayDeleteProc);
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
