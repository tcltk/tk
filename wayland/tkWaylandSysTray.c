/*
 * tkWaylandSysTray.c --
 *
 *	Wayland system tray/notification icon support using the
 *	StatusNotifierItem protocol via sd-bus. Implements a Tcl command
 *	"::tk::systray::_systray" with subcommands "create", "modify", and
 *	"destroy" – modelled after the macOS implementation.
 *
 * Copyright © 2020-2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkWaylandInt.h"
#include "tkMenu.h"          /* for TkMenu/TkMenuEntry definitions */
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
    char *menu_path;
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

    /* dbusmenu support */
    TkMenu *menuPtr;                  /* resolved Tk menu, if any */
    int item_id;

    int redisplayPending;
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
static void UnregisterStatusNotifierItem(DockIcon *icon);
static void ProcessDBusEvents(void *clientData);
static void InvokeButtonCommand(DockIcon *icon, int button, int x, int y);
static void DetectMenuFromCallback(DockIcon *icon, Tcl_Obj *cmdObj);
static void TrayIconRedisplayIdle(void *cd);
static void TrayIconImageChanged(void *cd, int x, int y, int w, int h,
                                 int imgw, int imgh);

/* Global item ID counter. */
static int global_item_id = 0;

/*
 *----------------------------------------------------------------------
 *
 * SubstituteButtonPercents --
 *
 *	Expand %X, %Y, and %% in a button command string.
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
 *	Execute the Tcl command bound to a button press.
 *
 *----------------------------------------------------------------------
 */
static void
InvokeButtonCommand(DockIcon *icon, int button, int x, int y)
{
    Tcl_Obj *cmdObj = (button == 1) ? icon->b1Command : icon->b3Command;
    if (!cmdObj) return;

    /* Skip local execution if it's a tk_popup (menu handled by dbusmenu) */
    if (strstr(Tcl_GetString(cmdObj), "tk_popup") != NULL) {
        return;
    }

    Tcl_Obj *script = SubstituteButtonPercents(cmdObj, x, y);
    Tcl_IncrRefCount(script);
    int result = Tcl_EvalObjEx(icon->interp, script, TCL_EVAL_GLOBAL);
    if (result != TCL_OK) Tcl_BackgroundError(icon->interp);
    Tcl_DecrRefCount(script);
}

/*
 *----------------------------------------------------------------------
 *
 * DetectMenuFromCallback --
 *
 *	Heuristically detect if a callback contains a tk_popup command;
 *	if so, resolve the menu and store it in icon->menuPtr for dbusmenu.
 *
 *----------------------------------------------------------------------
 */
static void
DetectMenuFromCallback(DockIcon *icon, Tcl_Obj *cmdObj)
{
    if (!cmdObj) return;
    const char *script = Tcl_GetString(cmdObj);
    const char *p = strstr(script, "tk_popup");
    if (!p) return;

    p += strlen("tk_popup");
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) return;

    const char *q = p;
    while (*q && (isalnum((unsigned char)*q) || *q == '.' || *q == '_' ||
                  *q == ':' || *q == '-')) q++;
    if (q == p) return;

    char *menuName = (char *)Tcl_Alloc(q - p + 1);
    memcpy(menuName, p, q - p);
    menuName[q - p] = '\0';

    TkWindow *winPtr = (TkWindow *)Tk_NameToWindow(icon->interp, menuName,
                                                    icon->tkwin);
    Tcl_Free(menuName);
    if (winPtr && winPtr->instanceData) {
        icon->menuPtr = (TkMenu *)winPtr->instanceData;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DBus Method Callbacks (SNI)
 *
 *----------------------------------------------------------------------
 */
static int method_activate(sd_bus_message *m, void *userdata,
                           sd_bus_error *ret_error) {
    DockIcon *icon = (DockIcon *)userdata;
    int x, y;
    int r = sd_bus_message_read(m, "ii", &x, &y);
    if (r < 0) return sd_bus_reply_method_return(m, "");
    InvokeButtonCommand(icon, 1, x, y);
    return sd_bus_reply_method_return(m, "");
}

static int method_secondary_activate(sd_bus_message *m, void *userdata,
                                     sd_bus_error *ret_error) {
    DockIcon *icon = (DockIcon *)userdata;
    int x, y;
    int r = sd_bus_message_read(m, "ii", &x, &y);
    if (r < 0) return sd_bus_reply_method_return(m, "");
    InvokeButtonCommand(icon, 3, x, y);
    return sd_bus_reply_method_return(m, "");
}

static int method_context_menu(sd_bus_message *m, void *userdata,
                               sd_bus_error *ret_error) {
    DockIcon *icon = (DockIcon *)userdata;
    int x, y;
    int r = sd_bus_message_read(m, "ii", &x, &y);
    if (r < 0) return sd_bus_reply_method_return(m, "");
    InvokeButtonCommand(icon, 3, x, y);
    return sd_bus_reply_method_return(m, "");
}

static int method_scroll(sd_bus_message *m, void *userdata,
                         sd_bus_error *ret_error) {
    /* Scroll not implemented */
    return sd_bus_reply_method_return(m, "");
}

/*
 *----------------------------------------------------------------------
 *
 * DBus Property Getters (SNI)
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

static int property_get_icon_name(sd_bus *bus, const char *path,
                                  const char *interface, const char *property,
                                  sd_bus_message *reply, void *userdata,
                                  sd_bus_error *ret_error) {
    return sd_bus_message_append(reply, "s", "");
}

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

static int property_get_menu(sd_bus *bus, const char *path,
                             const char *interface, const char *property,
                             sd_bus_message *reply, void *userdata,
                             sd_bus_error *ret_error) {
    DockIcon *icon = (DockIcon *)userdata;
    const char *menu_path = icon->menu_path ? icon->menu_path : "/";
    return sd_bus_message_append(reply, "o", menu_path);
}

/* VTable for StatusNotifierItem */
static const sd_bus_vtable status_notifier_item_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Activate", "ii", "", method_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SecondaryActivate", "ii", "", method_secondary_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ContextMenu", "ii", "", method_context_menu, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Scroll", "is", "", method_scroll, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Category", "s", property_get_category, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Id", "s", NULL, offsetof(DockIcon, title), SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Title", "s", NULL, offsetof(DockIcon, title), SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Status", "s", property_get_status, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("WindowId", "i", NULL, offsetof(DockIcon, item_id), SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("IconName", "s", property_get_icon_name, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("IconPixmap", "a(iiay)", property_get_icon_pixmap, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("ToolTip", "(sa(iiay)ss)", property_get_tooltip, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Menu", "o", property_get_menu, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END
};

/*
 *----------------------------------------------------------------------
 *
 * dbusmenu support (com.canonical.dbusmenu)
 *
 *----------------------------------------------------------------------
 */
static int dbusmenu_get_layout(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbusmenu_about_to_show(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbusmenu_event(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbusmenu_get_group_properties(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
static int dbusmenu_property_get_version(sd_bus *bus, const char *path, const char *interface,
                                         const char *property, sd_bus_message *reply,
                                         void *userdata, sd_bus_error *ret_error);

static const sd_bus_vtable dbusmenu_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("GetLayout", "iias", "u(ia{sv}av)", dbusmenu_get_layout, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("AboutToShow", "i", "", dbusmenu_about_to_show, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Event", "isvu", "", dbusmenu_event, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetGroupProperties", "aas", "aa{sv}", dbusmenu_get_group_properties, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Version", "u", dbusmenu_property_get_version, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END
};

/* Helper: find a menu entry by its D-Bus ID (address) */
static TkMenuEntry *
FindMenuEntryByID(TkMenu *menu, int id) {
    if (!menu || !menu->entries) return NULL;
    for (Tcl_Size i = 0; i < menu->numEntries; i++) {
        TkMenuEntry *mePtr = menu->entries[i];
        if ((int)(intptr_t)mePtr == id) return mePtr;
        if (mePtr->type == CASCADE_ENTRY && mePtr->childMenuRefPtr &&
            mePtr->childMenuRefPtr->menuPtr) {
            TkMenuEntry *found = FindMenuEntryByID(mePtr->childMenuRefPtr->menuPtr, id);
            if (found) return found;
        }
    }
    return NULL;
}

static int
AppendEntryProperties(sd_bus_message *reply, TkMenuEntry *mePtr) {
    int r;
    const char *type = "standard", *label = "", *toggle_type = NULL;
    int enabled = 1, sensitive = 1, toggle_state = 0;
    const char *children_display = NULL;

    if (mePtr) {
        if (mePtr->type == SEPARATOR_ENTRY) type = "separator";
        else if (mePtr->type == CASCADE_ENTRY) {
            if (mePtr->labelPtr) label = Tcl_GetString(mePtr->labelPtr);
            if (mePtr->childMenuRefPtr && mePtr->childMenuRefPtr->menuPtr)
                children_display = "submenu";
        } else {
            if (mePtr->labelPtr) label = Tcl_GetString(mePtr->labelPtr);
            if (mePtr->type == CHECK_BUTTON_ENTRY) {
                toggle_type = "checkmark";
                toggle_state = (mePtr->entryFlags & ENTRY_SELECTED) ? 1 : 0;
            } else if (mePtr->type == RADIO_BUTTON_ENTRY) {
                toggle_type = "radio";
                toggle_state = (mePtr->entryFlags & ENTRY_SELECTED) ? 1 : 0;
            }
        }
        if (mePtr->state & ENTRY_DISABLED) { enabled = 0; sensitive = 0; }
    }

    r = sd_bus_message_open_container(reply, 'a', "{sv}");
    if (r < 0) return r;
    r = sd_bus_message_append(reply, "{sv}", "type", "s", type);
    if (r < 0) return r;
    if (label && *label) {
        r = sd_bus_message_append(reply, "{sv}", "label", "s", label);
        if (r < 0) return r;
    }
    r = sd_bus_message_append(reply, "{sv}", "enabled", "b", enabled);
    if (r < 0) return r;
    r = sd_bus_message_append(reply, "{sv}", "sensitive", "b", sensitive);
    if (r < 0) return r;
    if (toggle_type) {
        r = sd_bus_message_append(reply, "{sv}", "toggle-type", "s", toggle_type);
        if (r < 0) return r;
        r = sd_bus_message_append(reply, "{sv}", "toggle-state", "i", toggle_state);
        if (r < 0) return r;
    }
    if (children_display) {
        r = sd_bus_message_append(reply, "{sv}", "children-display", "s", children_display);
        if (r < 0) return r;
    }
    return sd_bus_message_close_container(reply);
}

static int
BuildMenuChildren(TkMenu *childMenu, int depth, sd_bus_message *reply) {
    int r;
    r = sd_bus_message_open_container(reply, 'a', "v");
    if (r < 0) return r;
    if (childMenu && childMenu->entries && depth != 0) {
        int nextDepth = (depth > 0) ? depth - 1 : depth;
        for (Tcl_Size i = 0; i < childMenu->numEntries; i++) {
            TkMenuEntry *childPtr = childMenu->entries[i];
            int childId = (int)(intptr_t)childPtr;
            TkMenu *grandchildMenu = NULL;
            if (childPtr->type == CASCADE_ENTRY && childPtr->childMenuRefPtr &&
                childPtr->childMenuRefPtr->menuPtr)
                grandchildMenu = childPtr->childMenuRefPtr->menuPtr;

            r = sd_bus_message_open_container(reply, 'v', "(ia{sv}av)");
            if (r < 0) return r;
            r = sd_bus_message_open_container(reply, 'r', "ia{sv}av");
            if (r < 0) return r;
            r = sd_bus_message_append(reply, "i", childId);
            if (r < 0) return r;
            r = AppendEntryProperties(reply, childPtr);
            if (r < 0) return r;
            r = BuildMenuChildren(grandchildMenu, nextDepth, reply);
            if (r < 0) return r;
            r = sd_bus_message_close_container(reply);
            if (r < 0) return r;
            r = sd_bus_message_close_container(reply);
            if (r < 0) return r;
        }
    }
    return sd_bus_message_close_container(reply);
}

static int
dbusmenu_get_layout(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    DockIcon *icon = (DockIcon *)userdata;
    int parentId, depth;
    int r = sd_bus_message_read(m, "ii", &parentId, &depth);
    if (r < 0) return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_INVALID_ARGS,
                                                  "Failed to read int arguments");
    char **property_names = NULL;
    r = sd_bus_message_read_strv(m, &property_names);
    if (r < 0) return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_INVALID_ARGS,
                                                  "Failed to read property names");
    free(property_names);

    TkMenu *childMenu = NULL;
    if (parentId == 0) {
        childMenu = icon->menuPtr;
    } else {
        TkMenuEntry *targetEntry = FindMenuEntryByID(icon->menuPtr, parentId);
        if (targetEntry && targetEntry->type == CASCADE_ENTRY &&
            targetEntry->childMenuRefPtr && targetEntry->childMenuRefPtr->menuPtr)
            childMenu = targetEntry->childMenuRefPtr->menuPtr;
    }

    sd_bus_message *reply = NULL;
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;
    r = sd_bus_message_append(reply, "u", 0);
    if (r < 0) return r;
    r = sd_bus_message_open_container(reply, 'r', "ia{sv}av");
    if (r < 0) return r;
    r = sd_bus_message_append(reply, "i", parentId);
    if (r < 0) return r;
    r = AppendEntryProperties(reply, NULL); /* root has no properties */
    if (r < 0) return r;
    r = BuildMenuChildren(childMenu, depth, reply);
    if (r < 0) return r;
    r = sd_bus_message_close_container(reply);
    if (r < 0) return r;
    r = sd_bus_send(NULL, reply, NULL);
    if (r < 0) return r;
    return 0;
}

static int
dbusmenu_about_to_show(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    int id;
    int r = sd_bus_message_read(m, "i", &id);
    if (r < 0) return r;
    /* Could invoke -postcommand here if needed */
    return sd_bus_reply_method_return(m, "");
}

static int
dbusmenu_event(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    DockIcon *icon = (DockIcon *)userdata;
    int id;
    const char *eventId;
    uint32_t timestamp;
    int r = sd_bus_message_read(m, "is", &id, &eventId);
    if (r < 0) return r;
    r = sd_bus_message_skip(m, "v");
    if (r < 0) return r;
    r = sd_bus_message_read(m, "u", &timestamp);
    if (r < 0) return r;

    if (strcmp(eventId, "clicked") == 0) {
        TkMenuEntry *mePtr = FindMenuEntryByID(icon->menuPtr, id);
        if (mePtr && mePtr->commandPtr) {
            int result = Tcl_EvalObjEx(icon->interp, mePtr->commandPtr, TCL_EVAL_GLOBAL);
            if (result != TCL_OK) Tcl_BackgroundError(icon->interp);
        }
    }
    return sd_bus_reply_method_return(m, "");
}

static int
dbusmenu_get_group_properties(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    sd_bus_message *reply = NULL;
    int r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) return r;
    r = sd_bus_message_open_container(reply, 'a', "a{sv}");
    if (r < 0) return r;
    r = sd_bus_message_close_container(reply);
    if (r < 0) return r;
    r = sd_bus_send(NULL, reply, NULL);
    if (r < 0) return r;
    return 0;
}

static int
dbusmenu_property_get_version(sd_bus *bus, const char *path, const char *interface,
                              const char *property, sd_bus_message *reply,
                              void *userdata, sd_bus_error *ret_error) {
    return sd_bus_message_append(reply, "u", 3);
}

/*
 *----------------------------------------------------------------------
 *
 * ProcessDBusEvents --
 *
 *	Timer callback to process pending D‑Bus messages.
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
 *	Capture the Tk photo pixels into the icon's ARGB32 buffer.
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
 * UpdateIndicatorIcon / Status / Tooltip --
 *
 *	Emit the corresponding D‑Bus signals.
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

static void
UpdateTooltip(DockIcon *icon) {
    if (!icon->bus) return;
    sd_bus_emit_signal(icon->bus, icon->object_path,
                       "org.kde.StatusNotifierItem", "NewToolTip", "");
    sd_bus_flush(icon->bus);
}

/*
 *----------------------------------------------------------------------
 *
 * RegisterStatusNotifierItem --
 *
 *	Register the item on the session bus.
 *
 *----------------------------------------------------------------------
 */
static int
RegisterStatusNotifierItem(DockIcon *icon) {
    int r;
    if (!icon->bus) {
        r = sd_bus_open_user(&icon->bus);
        if (r < 0) {
            fprintf(stderr, "Failed to connect to session bus: %s\n", strerror(-r));
            return r;
        }
    }

    if (!icon->object_path) {
        icon->object_path = (char *)Tcl_Alloc(64);
        snprintf(icon->object_path, 64, "/StatusNotifierItem");
    }
    if (!icon->bus_name) {
        icon->bus_name = (char *)Tcl_Alloc(64);
        snprintf(icon->bus_name, 64, "org.tk.TrayIcon%d", icon->item_id);
    }

    r = sd_bus_request_name(icon->bus, icon->bus_name, 0);
    if (r < 0) {
        fprintf(stderr, "Failed to request bus name: %s\n", strerror(-r));
        return r;
    }

    r = sd_bus_add_object_vtable(icon->bus, NULL, icon->object_path,
                                 "org.kde.StatusNotifierItem",
                                 status_notifier_item_vtable, icon);
    if (r < 0) {
        fprintf(stderr, "Failed to add SNI vtable: %s\n", strerror(-r));
        return r;
    }

    if (!icon->menu_path) {
        icon->menu_path = (char *)Tcl_Alloc(64);
        snprintf(icon->menu_path, 64, "/Menu%d", icon->item_id);
    }
    r = sd_bus_add_object_vtable(icon->bus, NULL, icon->menu_path,
                                 "com.canonical.dbusmenu",
                                 dbusmenu_vtable, icon);
    if (r < 0) {
        fprintf(stderr, "Failed to add dbusmenu vtable: %s\n", strerror(-r));
        /* non‑fatal */
    }

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    r = sd_bus_call_method(icon->bus,
                           "org.kde.StatusNotifierWatcher",
                           "/StatusNotifierWatcher",
                           "org.kde.StatusNotifierWatcher",
                           "RegisterStatusNotifierItem",
                           &error, &m, "s", icon->bus_name);
    if (r < 0) {
        fprintf(stderr, "Failed to register with watcher: %s\n", error.message);
        sd_bus_error_free(&error);
        return r;
    }
    sd_bus_message_unref(m);

    sd_bus_flush(icon->bus);
    icon->busTimer = Tcl_CreateTimerHandler(50, ProcessDBusEvents, icon);
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * UnregisterStatusNotifierItem --
 *
 *	Unregister and clean up D‑Bus resources.
 *
 *----------------------------------------------------------------------
 */
static void
UnregisterStatusNotifierItem(DockIcon *icon) {
    if (icon->busTimer) {
        Tcl_DeleteTimerHandler(icon->busTimer);
        icon->busTimer = NULL;
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
 * TrayIconImageChanged / RedisplayIdle --
 *
 *	Callback when the Tk image changes; update the icon pixmap.
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
 *	Implements the _systray command with subcommands:
 *	  create imageName tooltipText button1Callback button3Callback
 *	  modify option value
 *	  destroy
 *
 *----------------------------------------------------------------------
 */
static int
WaylandSystrayObjCmd(void *clientData, Tcl_Interp *interp,
                     Tcl_Size objc, Tcl_Obj *const objv[]) {
    DockIcon **iconPtr = (DockIcon **)clientData;
    DockIcon *icon = *iconPtr;

    static const char *subcmds[] = {"create", "modify", "destroy", NULL};
    enum {CMD_CREATE, CMD_MODIFY, CMD_DESTROY} idx;
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "create | modify | destroy");
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
            Tcl_WrongNumArgs(interp, 2, objv, "imageName tooltipText button1Callback button3Callback");
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

        /* Set default title from window name */
        const char *winName = Tk_Name(icon->tkwin);
        icon->title = (char *)Tcl_Alloc(strlen(winName) + 1);
        strcpy(icon->title, winName);

        icon->status = (char *)Tcl_Alloc(strlen("active") + 1);
        strcpy(icon->status, "active");
        icon->dbus_status = STATUS_ACTIVE;
        icon->category = CATEGORY_APPLICATION_STATUS;
        icon->item_id = ++global_item_id;

        /* Store image name */
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

        /* Tooltip */
        icon->tooltip = (char *)Tcl_Alloc(strlen(Tcl_GetString(objv[3])) + 1);
        strcpy(icon->tooltip, Tcl_GetString(objv[3]));

        /* Callbacks */
        if (objv[4] && Tcl_GetString(objv[4])[0] != '\0') {
            icon->b1Command = objv[4];
            Tcl_IncrRefCount(icon->b1Command);
            DetectMenuFromCallback(icon, icon->b1Command);
        }
        if (objv[5] && Tcl_GetString(objv[5])[0] != '\0') {
            icon->b3Command = objv[5];
            Tcl_IncrRefCount(icon->b3Command);
            DetectMenuFromCallback(icon, icon->b3Command);
        }

        /* Register on D‑Bus */
        if (RegisterStatusNotifierItem(icon) < 0) {
            Tcl_AppendResult(interp, "failed to register StatusNotifierItem", NULL);
            WaylandSystrayDeleteProc(iconPtr);
            return TCL_ERROR;
        }
        UpdateIndicatorIcon(icon);
        UpdateIndicatorStatus(icon);
        UpdateTooltip(icon);

        return TCL_OK;
    }

    case CMD_MODIFY: {
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
        if (strcmp(option, "image") == 0) {
            /* Release old image */
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
        } else if (strcmp(option, "text") == 0) {
            if (icon->tooltip) Tcl_Free(icon->tooltip);
            icon->tooltip = (char *)Tcl_Alloc(strlen(Tcl_GetString(value)) + 1);
            strcpy(icon->tooltip, Tcl_GetString(value));
            UpdateTooltip(icon);
        } else if (strcmp(option, "b1_callback") == 0) {
            if (icon->b1Command) Tcl_DecrRefCount(icon->b1Command);
            icon->b1Command = value;
            Tcl_IncrRefCount(icon->b1Command);
            DetectMenuFromCallback(icon, icon->b1Command);
        } else if (strcmp(option, "b3_callback") == 0) {
            if (icon->b3Command) Tcl_DecrRefCount(icon->b3Command);
            icon->b3Command = value;
            Tcl_IncrRefCount(icon->b3Command);
            DetectMenuFromCallback(icon, icon->b3Command);
        } else {
            Tcl_AppendResult(interp, "unknown option: must be image, text, b1_callback, or b3_callback", NULL);
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
 *	Free all resources held by the systray icon.
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
    if (icon->menu_path) Tcl_Free(icon->menu_path);

    Tcl_Free(icon);
    *iconPtr = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Tktray_Init --
 *
 *	Create the ::tk::systray::_systray command.
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
