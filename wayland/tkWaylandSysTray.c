/*
 * tkWaylandSysTray.c --
 *
 *	System tray/notification icon support for Wayland using the
 *      StatusNotifierItem protocol via sd-bus
 *	with GLFW integration. Implements a "systray" Tcl command which 
 *      permits changing the system tray icon and posting system notifications.
 *
 * Copyright © 2005 Anton Kovalenko
 * Copyright © 2020-2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

/* SD-Bus includes. */
#include <systemd/sd-bus.h>
#include <systemd/sd-bus-protocol.h>

/* stb_image for image processing. */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_image.h"

/* Flags of widget configuration options. */
#define ICON_CONF_IMAGE         (1<<0)
#define ICON_CONF_REDISPLAY     (1<<1)
#define ICON_CONF_FIRST_TIME    (1<<4)

/* Widget states */
#define ICON_FLAG_REDRAW_PENDING    (1<<0)
#define ICON_FLAG_DIRTY_EDGES       (1<<2)

/* StatusDBus values. */
typedef enum {
    STATUS_PASSIVE = 0,
    STATUS_ACTIVE = 1,
    STATUS_NEEDS_ATTENTION = 2
} StatusDBus;

/* Category values. */
typedef enum {
    CATEGORY_APPLICATION_STATUS = 0,
    CATEGORY_COMMUNICATIONS = 1,
    CATEGORY_SYSTEM_SERVICES = 2,
    CATEGORY_HARDWARE = 3
} CategoryDBus;

/*
 * Data structure representing dock widget.
 */
typedef struct {
    /* Standard widget fields. */
    Tk_Window tkwin;
    Tk_OptionTable options;
    Tcl_Interp *interp;
    Tcl_Command widgetCmd;

    /* Image to be drawn. */
    Tk_Image image;
    int imageWidth, imageHeight;
    
    /* GLFW window for fallback/drawing support. */
    GLFWwindow* glfwWindow;
    Drawable drawable;
    
    /* SD-Bus connection. */
    sd_bus *bus;
    char *bus_name;
    char *object_path;
    Tcl_TimerToken busTimer;  /* Timer for processing DBus events */
    
    /* Cached icon information. */
    char *tempIconPath;  /* Path to temporary icon file */
    time_t lastIconUpdate; /* Last time icon was updated */
    
    /* Tcl bindings for mouse events */
    Tcl_Obj *b1Command;  /* Command for button-1 press */
    Tcl_Obj *b3Command;  /* Command for button-3 press */
    
    int flags;
    int msgid;
    int item_id;  /* Unique ID for this tray item */
    
    int width, height;
    int visible;
    int docked;
    Tcl_Obj *imageObj;
    Tcl_Obj *classObj;
    
    char* trayAppId;  /* App ID for Wayland */
    char* iconName;   /* Name of the icon (filename or themed icon name) */
    char* iconPath;   /* Path to icon file if using file-based icon */
    char* status;     /* Status: "active", "passive", "attention" */
    char* tooltip;    /* Tooltip text */
    char* title;      /* Title/name */
    
    /* Properties for StatusNotifierItem interface. */
    CategoryDBus category;
    StatusDBus dbus_status;
} DockIcon;

/* Forward declarations. */
static Tcl_ObjCmdProc2 TrayIconCreateCmd;
static Tcl_ObjCmdProc2 TrayIconObjectCmd;
static Tcl_CmdDeleteProc TrayIconDeleteProc;
static int TrayIconConfigureMethod(DockIcon *icon, Tcl_Interp *interp,
    Tcl_Size objc, Tcl_Obj *const objv[], int addflags);
static void TrayIconUpdate(DockIcon* icon, int mask);
static int CreateTrayIconWindow(DockIcon *icon);
static void RemoveTrayIconWindow(DockIcon *icon);
static int UpdateIndicatorIcon(DockIcon *icon);
static int UpdateIndicatorStatus(DockIcon *icon);
static int UpdateTooltip(DockIcon *icon);
static int SaveTkImageToFile(DockIcon *icon);
static int RegisterStatusNotifierItem(DockIcon *icon);
static int UnregisterStatusNotifierItem(DockIcon *icon);
static void ProcessDBusEvents(ClientData clientData);
static void InvokeButtonCommand(DockIcon *icon, int button, int x, int y);

/* Global item ID counter. */
static int global_item_id = 0;

/*
 *----------------------------------------------------------------------
 *
 * InvokeButtonCommand --
 *
 *	Invoke Tcl command bound to button press.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Executes Tcl script.
 *
 *----------------------------------------------------------------------
 */

static void
InvokeButtonCommand(
    DockIcon *icon,
    int button,
    int x,
    int y)
{
    Tcl_Obj *cmdObj = NULL;
    Tcl_Obj *script;
    int result;
    
    /* Select command based on button. */
    if (button == 1 && icon->b1Command) {
        cmdObj = icon->b1Command;
    } else if (button == 3 && icon->b3Command) {
        cmdObj = icon->b3Command;
    }
    
    if (!cmdObj) {
        return;
    }
    
    /* Build script with coordinates appended. */
    script = Tcl_DuplicateObj(cmdObj);
    Tcl_IncrRefCount(script);
    
    Tcl_ListObjAppendElement(icon->interp, script, Tcl_NewIntObj(x));
    Tcl_ListObjAppendElement(icon->interp, script, Tcl_NewIntObj(y));
    
    /* Evaluate script. */
    result = Tcl_EvalObjEx(icon->interp, script, TCL_EVAL_GLOBAL);
    
    if (result != TCL_OK) {
        Tcl_BackgroundError(icon->interp);
    }
    
    Tcl_DecrRefCount(script);
}

/*
 *----------------------------------------------------------------------
 *
 * DBus Method Callbacks.
 *
 *----------------------------------------------------------------------
 */

static int
method_activate(
    sd_bus_message *m,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    DockIcon *icon = (DockIcon *)userdata;
    int x, y;
    
    if (sd_bus_message_read(m, "ii", &x, &y) < 0) {
        return sd_bus_reply_method_return(m, "");
    }
    
    /* Invoke button-1 command. */
    InvokeButtonCommand(icon, 1, x, y);
    
    return sd_bus_reply_method_return(m, "");
}

static int
method_secondary_activate(
    sd_bus_message *m,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    DockIcon *icon = (DockIcon *)userdata;
    int x, y;
    
    if (sd_bus_message_read(m, "ii", &x, &y) < 0) {
        return sd_bus_reply_method_return(m, "");
    }
    
    /* Invoke button-3 command. */
    InvokeButtonCommand(icon, 3, x, y);
    
    return sd_bus_reply_method_return(m, "");
}

static int
method_context_menu(
    sd_bus_message *m,
    void *userdata,
    TCL_UNUSED(sd_bus_error *))
{
    DockIcon *icon = (DockIcon *)userdata;
    int x, y;
    
    if (sd_bus_message_read(m, "ii", &x, &y) < 0) {
        return sd_bus_reply_method_return(m, "");
    }
    
    /* Also invoke button-3 command for context menu. */
    InvokeButtonCommand(icon, 3, x, y);
    
    return sd_bus_reply_method_return(m, "");
}

static int
method_scroll(
    sd_bus_message *m,
    TCL_UNUSED(void *),
     TCL_UNUSED(sd_bus_error *))
{
    int delta;
    const char *orientation;
    
    if (sd_bus_message_read(m, "is", &delta, &orientation) < 0) {
        return sd_bus_reply_method_return(m, "");
    }
    
    /* TODO: Handle scroll event if needed */
    
    return sd_bus_reply_method_return(m, "");
}

/*
 *----------------------------------------------------------------------
 *
 * DBus Property Getters.
 *
 *----------------------------------------------------------------------
 */

static int
property_get_category(
    TCL_UNUSED(sd_bus *), /* bus */
    TCL_UNUSED(const char *), /* path */
    TCL_UNUSED(const char *), /*interface */
    TCL_UNUSED(const char *), /* property */
    sd_bus_message *reply,
    void *userdata,
    TCL_UNUSED(sd_bus_error *)) /*error */
{
    DockIcon *icon = userdata;
    const char *category_str;
    
    switch (icon->category) {
        case CATEGORY_APPLICATION_STATUS:
            category_str = "ApplicationStatus";
            break;
        case CATEGORY_COMMUNICATIONS:
            category_str = "Communications";
            break;
        case CATEGORY_SYSTEM_SERVICES:
            category_str = "SystemServices";
            break;
        case CATEGORY_HARDWARE:
            category_str = "Hardware";
            break;
        default:
            category_str = "ApplicationStatus";
    }
    
    return sd_bus_message_append(reply, "s", category_str);
}

static int
property_get_status(
    TCL_UNUSED(sd_bus *), /* bus */
    TCL_UNUSED(const char *), /* path */
    TCL_UNUSED(const char *), /*interface */
    TCL_UNUSED(const char *), /* property */
    sd_bus_message *reply,
    void *userdata,
    TCL_UNUSED(sd_bus_error *)) /*error */
{
    DockIcon *icon = userdata;
    const char *status_str;
    
    switch (icon->dbus_status) {
        case STATUS_PASSIVE:
            status_str = "Passive";
            break;
        case STATUS_ACTIVE:
            status_str = "Active";
            break;
        case STATUS_NEEDS_ATTENTION:
            status_str = "NeedsAttention";
            break;
        default:
            status_str = "Active";
    }
    
    return sd_bus_message_append(reply, "s", status_str);
}

static int
property_get_icon_name(
    TCL_UNUSED(sd_bus *), /* bus */
    TCL_UNUSED(const char *), /* path */
    TCL_UNUSED(const char *), /*interface */
    TCL_UNUSED(const char *), /* property */
    sd_bus_message *reply,
    void *userdata,
    TCL_UNUSED(sd_bus_error *)) /*error */
{
    DockIcon *icon = userdata;
    const char *name = "";
    
    /* Prefer temp icon path, then iconName. */
    if (icon->tempIconPath) {
        name = icon->tempIconPath;
    } else if (icon->iconName) {
        name = icon->iconName;
    }
    
    return sd_bus_message_append(reply, "s", name);
}

static int
property_get_icon_pixmap(
    TCL_UNUSED(sd_bus *), /* bus */
    TCL_UNUSED(const char *), /* path */
    TCL_UNUSED(const char *), /*interface */
    TCL_UNUSED(const char *), /* property */
    sd_bus_message *reply,
    void *userdata,
    TCL_UNUSED(sd_bus_error *)) /*error */
{
    DockIcon *icon = userdata;
    int r;
    
    /* Open array of pixmaps: a(iiay). */
    r = sd_bus_message_open_container(reply, 'a', "(iiay)");
    if (r < 0) return r;
    
    if (icon->tempIconPath) {
        /* Load PNG and send pixmap data. */
        int width, height, channels;
        unsigned char *data = stbi_load(icon->tempIconPath, &width, &height, 
                                        &channels, 4);
        
        if (data) {
            /* Open pixmap struct: (iiay). */
            r = sd_bus_message_open_container(reply, 'r', "iiay");
            if (r < 0) {
                stbi_image_free(data);
                return r;
            }
            
            /* Width and height. */
            r = sd_bus_message_append(reply, "ii", width, height);
            if (r < 0) {
                stbi_image_free(data);
                return r;
            }
            
            /* Convert RGBA to ARGB and append as byte array. */
            r = sd_bus_message_open_container(reply, 'a', "y");
            if (r < 0) {
                stbi_image_free(data);
                return r;
            }
            
            /* Write ARGB data. */
            unsigned char *argb = (unsigned char *)ckalloc(width * height * 4);
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int idx = (y * width + x) * 4;
                    int out_idx = idx;
                    argb[out_idx + 0] = data[idx + 3];  /* A */
                    argb[out_idx + 1] = data[idx + 0];  /* R */
                    argb[out_idx + 2] = data[idx + 1];  /* G */
                    argb[out_idx + 3] = data[idx + 2];  /* B */
                }
            }
            
            r = sd_bus_message_append_array(reply, 'y', argb, width * height * 4);
            ckfree((char *)argb);
            stbi_image_free(data);
            
            if (r < 0) return r;
            
            sd_bus_message_close_container(reply);  /* Close byte array */
            sd_bus_message_close_container(reply);  /* Close struct */
        }
    }
    
    sd_bus_message_close_container(reply);  /* Close array */
    return 0;
}

static int
property_get_tooltip(
    TCL_UNUSED(sd_bus *), /* bus */
    TCL_UNUSED(const char *), /* path */
    TCL_UNUSED(const char *), /*interface */
    TCL_UNUSED(const char *), /* property */
    sd_bus_message *reply,
    void *userdata,
    TCL_UNUSED(sd_bus_error *)) /*error */
{
    DockIcon *icon = userdata;
    int r;
    
    /* ToolTip is (sa(iiay)ss): icon_name, icon_pixmap, title, description. */
    r = sd_bus_message_open_container(reply, 'r', "sa(iiay)ss");
    if (r < 0) return r;
    
    /* Icon name. */
    r = sd_bus_message_append(reply, "s", icon->iconName ? icon->iconName : "");
    if (r < 0) return r;
    
    /* Icon pixmap (empty array). */
    r = sd_bus_message_open_container(reply, 'a', "(iiay)");
    if (r < 0) return r;
    sd_bus_message_close_container(reply);
    
    /* Title and description. */
    r = sd_bus_message_append(reply, "ss",
                              icon->title ? icon->title : "",
                              icon->tooltip ? icon->tooltip : "");
    if (r < 0) return r;
    
    sd_bus_message_close_container(reply);
    return 0;
}

static int
property_get_menu(
    TCL_UNUSED(sd_bus *), /* bus */
    TCL_UNUSED(const char *), /* path */
    TCL_UNUSED(const char *), /*interface */
    TCL_UNUSED(const char *), /* property */
    sd_bus_message *reply,
    TCL_UNUSED(void *), /* userdat */
    TCL_UNUSED(sd_bus_error *)) /*error */
{
    /* No menu support for now - return empty object path. */
    return sd_bus_message_append(reply, "o", "/");
}

/* VTable for StatusNotifierItem interface. */
static const sd_bus_vtable status_notifier_item_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Activate", "ii", "", method_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SecondaryActivate", "ii", "", method_secondary_activate, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ContextMenu", "ii", "", method_context_menu, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Scroll", "is", "", method_scroll, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Category", "s", property_get_category, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Id", "s", NULL, offsetof(DockIcon, trayAppId), SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Title", "s", NULL, offsetof(DockIcon, title), SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Status", "s", property_get_status, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("WindowId", "i", NULL, offsetof(DockIcon, item_id), SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("IconThemePath", "s", NULL, 0, SD_BUS_VTABLE_PROPERTY_CONST | SD_BUS_VTABLE_HIDDEN),
    SD_BUS_PROPERTY("IconName", "s", property_get_icon_name, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("IconPixmap", "a(iiay)", property_get_icon_pixmap, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("OverlayIconPixmap", "a(iiay)", NULL, 0, SD_BUS_VTABLE_HIDDEN),
    SD_BUS_PROPERTY("AttentionIconPixmap", "a(iiay)", NULL, 0, SD_BUS_VTABLE_HIDDEN),
    SD_BUS_PROPERTY("AttentionMovieName", "s", NULL, 0, SD_BUS_VTABLE_PROPERTY_CONST | SD_BUS_VTABLE_HIDDEN),
    SD_BUS_PROPERTY("ToolTip", "(sa(iiay)ss)", property_get_tooltip, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Menu", "o", property_get_menu, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END
};

/*
 *----------------------------------------------------------------------
 *
 * ProcessDBusEvents --
 *
 *	Timer callback to process DBus events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Processes pending DBus messages.
 *
 *----------------------------------------------------------------------
 */

static void
ProcessDBusEvents(
    ClientData clientData)
{
    DockIcon *icon = (DockIcon *)clientData;
    
    if (icon->bus) {
        /* Process pending DBus messages. */
        while (sd_bus_process(icon->bus, NULL) > 0) {
            /* Keep processing. */
        }
        
        /* Re-schedule timer. */
        icon->busTimer = Tcl_CreateTimerHandler(50, ProcessDBusEvents, icon);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TrayIconObjectCmd --
 *
 *	Manage attributes of tray icon.
 *
 * Results:
 *	Various values of the tray icon are set and retrieved.
 *
 * Side effects:
 *	May update tray icon appearance or state.
 *
 *----------------------------------------------------------------------
 */

static int
TrayIconObjectCmd(
    void *cd,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    DockIcon *icon = (DockIcon*)cd;
    int wcmd;
    int i;
    int bbox[4] = {0, 0, 24, 24};  /* Standard tray icon size */
    Tcl_Obj* bboxObj;
    
    enum {XWC_CONFIGURE = 0, XWC_CGET, XWC_BALLOON, XWC_CANCEL,
        XWC_BBOX, XWC_DOCKED, XWC_ORIENTATION, XWC_BIND};
    const char *st_wcmd[] = {"configure", "cget", "balloon", "cancel",
        "bbox", "docked", "orientation", "bind", NULL};

    if (objc<2) {
        Tcl_WrongNumArgs(interp, 1, objv, "subcommand ?args?");
        return TCL_ERROR;
    }
    
    if (Tcl_GetIndexFromObj(interp, objv[1], st_wcmd,
            "subcommand", TCL_EXACT, &wcmd) != TCL_OK) {
        return TCL_ERROR;
    }

    switch (wcmd) {
    case XWC_CONFIGURE:
        return TrayIconConfigureMethod(icon,interp,objc-2,objv+2,0);

    case XWC_CGET: {
        Tcl_Obj* optionValue;
        
        if (objc != 3) {
            Tcl_WrongNumArgs(interp,2,objv,"option");
            return TCL_ERROR;
        }
        
        optionValue = Tk_GetOptionValue(interp,(char*)icon,
            icon->options,objv[2],icon->tkwin);
        if (optionValue) {
            Tcl_SetObjResult(interp,optionValue);
            return TCL_OK;
        } else {
            return TCL_ERROR;
        }
    }

    case XWC_BIND: {
        int button;
        const char *sequence;
        
        if (objc < 3 || objc > 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "sequence ?command?");
            return TCL_ERROR;
        }
        
        sequence = Tcl_GetString(objv[2]);
        
        /* Parse button sequence (e.g., "<Button-1>" or "<Button-3>"). */
        if (strcmp(sequence, "<Button-1>") == 0 || 
            strcmp(sequence, "<1>") == 0 ||
            strcmp(sequence, "1") == 0) {
            button = 1;
        } else if (strcmp(sequence, "<Button-3>") == 0 || 
                   strcmp(sequence, "<3>") == 0 ||
                   strcmp(sequence, "3") == 0) {
            button = 3;
        } else {
            Tcl_SetObjResult(interp, 
                Tcl_NewStringObj("only <Button-1> and <Button-3> supported", -1));
            return TCL_ERROR;
        }
        
        if (objc == 3) {
            /* Query binding. */
            Tcl_Obj *cmd = (button == 1) ? icon->b1Command : icon->b3Command;
            if (cmd) {
                Tcl_SetObjResult(interp, cmd);
            }
            return TCL_OK;
        }
        
        /* Set binding. */
        if (button == 1) {
            if (icon->b1Command) {
                Tcl_DecrRefCount(icon->b1Command);
            }
            icon->b1Command = objv[3];
            Tcl_IncrRefCount(icon->b1Command);
        } else {
            if (icon->b3Command) {
                Tcl_DecrRefCount(icon->b3Command);
            }
            icon->b3Command = objv[3];
            Tcl_IncrRefCount(icon->b3Command);
        }
        
        return TCL_OK;
    }

    case XWC_BALLOON: {
        const char* title;
        const char* message;
        
        if ((objc != 3) && (objc != 4) && (objc != 5)) {
            Tcl_WrongNumArgs(interp, 2, objv, "message ?title? ?timeout?");
            return TCL_ERROR;
        }
        
        message = Tcl_GetString(objv[2]);
        title = (objc >= 4) ? Tcl_GetString(objv[3]) : "Notification";
        
		/* Free old strings if they exist to prevent memory leaks. */
		if (icon->title) ckfree(icon->title);
		if (icon->tooltip) ckfree(icon->tooltip);
		
		/* Allocate and copy new values.*/
		icon->title = ckalloc(strlen(title) + 1);
		strcpy(icon->title, title);
		
		icon->tooltip = ckalloc(strlen(message) + 1);
		strcpy(icon->tooltip, message);
        
        /* Update indicator status to "attention". */
        if (icon->status) {
            ckfree(icon->status);
        }
        icon->status = ckalloc(strlen("attention") + 1);
        strcpy(icon->status, "attention");
        
        /* Update DBus status */
        UpdateIndicatorStatus(icon);
        
        Tcl_SetObjResult(interp, Tcl_NewIntObj(++icon->msgid));
        return TCL_OK;
    }

    case XWC_CANCEL:
        /* Set status back to active. */
        if (icon->status) {
            ckfree(icon->status);
        }
        icon->status = ckalloc(strlen("active") + 1);
        strcpy(icon->status, "active");
        
        UpdateIndicatorStatus(icon);
        return TCL_OK;

    case XWC_BBOX:
        /* Return bounding box of tray icon. */
        if (icon->docked) {
            bbox[2] = icon->imageWidth > 0 ? icon->imageWidth : 24;
            bbox[3] = icon->imageHeight > 0 ? icon->imageHeight : 24;
        }
        bboxObj = Tcl_NewObj();
        for (i = 0; i < 4; ++i) {
            Tcl_ListObjAppendElement(interp, bboxObj, Tcl_NewIntObj(bbox[i]));
        }
        Tcl_SetObjResult(interp, bboxObj);
        return TCL_OK;

    case XWC_DOCKED:
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(icon->docked && icon->bus != NULL));
        return TCL_OK;

    case XWC_ORIENTATION:
        Tcl_SetObjResult(interp, Tcl_NewStringObj("horizontal", -1));
        return TCL_OK;
    }
    
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * SaveTkImageToFile --
 *
 *	Save Tk image to PNG file using stb_image_write.
 *
 * Results:
 *	1 on success, 0 on failure.
 *
 * Side effects:
 *	Creates temporary PNG file.
 *
 *----------------------------------------------------------------------
 */

static int
SaveTkImageToFile(
    DockIcon *icon)
{
    Tk_PhotoHandle photo;
    Tk_PhotoImageBlock block;
    unsigned char *pixels;
    int stride;
    char template[] = "/tmp/tktray_XXXXXX.png";
    int fd;
    int success;
    
    if (!icon->image || !icon->imageObj) {
        return 0;
    }
    
    /* Get image dimensions. */
    Tk_SizeOfImage(icon->image, &icon->imageWidth, &icon->imageHeight);
    
    if (icon->imageWidth <= 0 || icon->imageHeight <= 0) {
        return 0;
    }
    
    /* Get Photo handle. */
    photo = Tk_FindPhoto(icon->interp, Tcl_GetString(icon->imageObj));
    if (!photo) {
        return 0;
    }
    
    /* Allocate buffer for image data. */
    stride = icon->imageWidth * 4;  /* RGBA */
    pixels = (unsigned char *)ckalloc(icon->imageHeight * stride);
    if (!pixels) {
        return 0;
    }
    
    /* Set up image block. */
    Tk_PhotoGetImage(photo, &block);
    
    /* Copy image data. */
    memcpy(pixels, block.pixelPtr, icon->imageHeight * stride);
    
    /* Clean up old temp file. */
    if (icon->tempIconPath) {
        unlink(icon->tempIconPath);
        ckfree(icon->tempIconPath);
        icon->tempIconPath = NULL;
    }
    
    /* Create temporary file. */
    fd = mkstemps(template, 4);  /* 4 for ".png" */
    if (fd < 0) {
        ckfree((char *)pixels);
        return 0;
    }
    close(fd);
    
    /* Write PNG file. */
    success = stbi_write_png(template, icon->imageWidth, icon->imageHeight, 
                             4, pixels, stride);
    
    ckfree((char *)pixels);
    
    if (!success) {
        unlink(template);
        return 0;
    }
    
    icon->tempIconPath = ckalloc(strlen(template) + 1);
    strcpy(icon->tempIconPath, template);
    icon->lastIconUpdate = time(NULL);
    
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * RegisterStatusNotifierItem --
 *
 *	Register StatusNotifierItem on DBus.
 *
 * Results:
 *	0 on success, negative on error.
 *
 * Side effects:
 *	Registers with DBus and StatusNotifierWatcher.
 *
 *----------------------------------------------------------------------
 */

static int
RegisterStatusNotifierItem(
    DockIcon *icon)
{
    int r;
    sd_bus_message *m = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    
    if (!icon->bus) {
        /* Connect to session bus. */
        r = sd_bus_open_user(&icon->bus);
        if (r < 0) {
            fprintf(stderr, "Failed to connect to session bus: %s\n", strerror(-r));
            return r;
        }
    }
    
    /* Generate unique object path. */
    if (!icon->object_path) {
        icon->object_path = ckalloc(64);
        snprintf(icon->object_path, 64, "/TrayIcon/%d", icon->item_id);
    }
    
    /* Generate unique bus name. */
    if (!icon->bus_name) {
        icon->bus_name = ckalloc(64);
        snprintf(icon->bus_name, 64, "org.tk.TrayIcon%d", icon->item_id);
    }
    
    /* Request bus name. */
    r = sd_bus_request_name(icon->bus, icon->bus_name, 0);
    if (r < 0) {
        fprintf(stderr, "Failed to request bus name: %s\n", strerror(-r));
        return r;
    }
    
    /* Add object with StatusNotifierItem interface. */
    r = sd_bus_add_object_vtable(icon->bus,
                                 NULL,
                                 icon->object_path,
                                 "org.kde.StatusNotifierItem",
                                 status_notifier_item_vtable,
                                 icon);
    if (r < 0) {
        fprintf(stderr, "Failed to add object vtable: %s\n", strerror(-r));
        return r;
    }
    
    /* Register on StatusNotifierWatcher. */
    r = sd_bus_call_method(icon->bus,
                          "org.kde.StatusNotifierWatcher",
                          "/StatusNotifierWatcher",
                          "org.kde.StatusNotifierWatcher",
                          "RegisterStatusNotifierItem",
                          &error,
                          &m,
                          "s",
                          icon->bus_name);
    
    if (r < 0) {
        fprintf(stderr, "Failed to register StatusNotifierItem: %s\n", 
                error.message);
        sd_bus_error_free(&error);
        return r;
    }
    
    sd_bus_message_unref(m);
    
    /* Start DBus event processing. */
    icon->busTimer = Tcl_CreateTimerHandler(50, ProcessDBusEvents, icon);
    
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * UnregisterStatusNotifierItem --
 *
 *	Unregister StatusNotifierItem from DBus.
 *
 * Results:
 *	0 on success.
 *
 * Side effects:
 *	Unregisters from DBus.
 *
 *----------------------------------------------------------------------
 */

static int
UnregisterStatusNotifierItem(
    DockIcon *icon)
{
    sd_bus_message *m = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r;
    
    /* Cancel timer. */
    if (icon->busTimer) {
        Tcl_DeleteTimerHandler(icon->busTimer);
        icon->busTimer = NULL;
    }
    
    if (!icon->bus) {
        return 0;
    }
    
    r = sd_bus_call_method(icon->bus,
                          "org.kde.StatusNotifierWatcher",
                          "/StatusNotifierWatcher",
                          "org.kde.StatusNotifierWatcher",
                          "UnregisterStatusNotifierItem",
                          &error,
                          &m,
                          "s",
                          icon->bus_name);
    
    if (r < 0) {
        fprintf(stderr, "Failed to unregister: %s\n", error.message);
        sd_bus_error_free(&error);
    }
    
    if (m) {
        sd_bus_message_unref(m);
    }
    
    /* Release bus name. */
    sd_bus_release_name(icon->bus, icon->bus_name);
    
    /* Close bus connection. */
    sd_bus_flush(icon->bus);
    sd_bus_close(icon->bus);
    sd_bus_unref(icon->bus);
    icon->bus = NULL;
    
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateIndicatorIcon --
 *
 *	Update the icon displayed in the system tray.
 *
 * Results:
 *	1 on success.
 *
 * Side effects:
 *	Emits NewIcon signal on DBus.
 *
 *----------------------------------------------------------------------
 */

static int
UpdateIndicatorIcon(
    DockIcon *icon)
{
    if (!icon->bus) {
        return 0;
    }
    
    /* Emit NewIcon signal. */
    sd_bus_emit_signal(icon->bus,
                       icon->object_path,
                       "org.kde.StatusNotifierItem",
                       "NewIcon",
                       "");
    
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateIndicatorStatus --
 *
 *	Update the status of the indicator.
 *
 * Results:
 *	1 on success.
 *
 * Side effects:
 *	Emits NewStatus signal on DBus.
 *
 *----------------------------------------------------------------------
 */

static int
UpdateIndicatorStatus(
    DockIcon *icon)
{
    const char *status_str;
    
    if (!icon->bus) {
        return 0;
    }
    
    /* Map status string to DBus status. */
    if (icon->status) {
        if (strcmp(icon->status, "active") == 0) {
            icon->dbus_status = STATUS_ACTIVE;
            status_str = "Active";
        } else if (strcmp(icon->status, "passive") == 0) {
            icon->dbus_status = STATUS_PASSIVE;
            status_str = "Passive";
        } else if (strcmp(icon->status, "attention") == 0) {
            icon->dbus_status = STATUS_NEEDS_ATTENTION;
            status_str = "NeedsAttention";
        } else {
            icon->dbus_status = STATUS_ACTIVE;
            status_str = "Active";
        }
    } else {
        icon->dbus_status = STATUS_ACTIVE;
        status_str = "Active";
    }
    
    /* Emit NewStatus signal. */
    sd_bus_emit_signal(icon->bus,
                       icon->object_path,
                       "org.kde.StatusNotifierItem",
                       "NewStatus",
                       "s",
                       status_str);
    
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateTooltip --
 *
 *	Update tooltip.
 *
 * Results:
 *	1 on success.
 *
 * Side effects:
 *	Emits NewToolTip signal on DBus.
 *
 *----------------------------------------------------------------------
 */

static int
UpdateTooltip(
    DockIcon *icon)
{
    if (!icon->bus) {
        return 0;
    }
    
    /* Emit NewToolTip signal. */
    sd_bus_emit_signal(icon->bus,
                       icon->object_path,
                       "org.kde.StatusNotifierItem",
                       "NewToolTip",
                       "");
    
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateTrayIconWindow --
 *
 *	Create and configure the StatusNotifierItem.
 *
 * Results:
 *	1 on success, 0 on failure.
 *
 * Side effects:
 *	Registers with DBus, creates GLFW window.
 *
 *----------------------------------------------------------------------
 */

static int
CreateTrayIconWindow(
    DockIcon *icon)
{
    TkWindow *winPtr;
    
    if (!icon->tkwin) {
        return 0;
    }
    
    /* Assign unique ID. */
    icon->item_id = ++global_item_id;
    
    /* Set default title if not set. */
    if (!icon->title) {
        const char *window_name = Tk_Name(icon->tkwin);
        icon->title = ckalloc(strlen(window_name) + 1);
        strcpy(icon->title, window_name);
    }
    
    /* Set default category. */
    icon->category = CATEGORY_APPLICATION_STATUS;
    
    /* Register on DBus. */
    if (RegisterStatusNotifierItem(icon) < 0) {
        fprintf(stderr, "Failed to register StatusNotifierItem\n");
        return 0;
    }
    
    /* Update icon from Tk image if available. */
    if (icon->image) {
        SaveTkImageToFile(icon);
        UpdateIndicatorIcon(icon);
    }
    
    /* Update status. */
    UpdateIndicatorStatus(icon);
    
    /* Update tooltip. */
    UpdateTooltip(icon);
    
    /* Create GLFW window for compatibility. */
    if (!icon->glfwWindow) {
        winPtr = (TkWindow *)icon->tkwin;
        icon->glfwWindow = TkGlfwCreateWindow(winPtr, 
            icon->imageWidth > 0 ? icon->imageWidth : 64,
            icon->imageHeight > 0 ? icon->imageHeight : 64,
            icon->trayAppId, &icon->drawable);
        if (icon->glfwWindow) {
            glfwHideWindow(icon->glfwWindow);
        }
    }
    
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * RemoveTrayIconWindow --
 *
 *	Remove tray icon.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Unregisters from DBus, destroys GLFW window.
 *
 *----------------------------------------------------------------------
 */

static void
RemoveTrayIconWindow(
    DockIcon *icon)
{
    UnregisterStatusNotifierItem(icon);
    
    if (icon->glfwWindow) {
        TkGlfwDestroyWindow(icon->glfwWindow);
        icon->glfwWindow = NULL;
        icon->drawable = None;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TrayIconUpdate --
 *
 *	Update tray icon based on configuration changes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May update icon, create/destroy windows.
 *
 *----------------------------------------------------------------------
 */

static void
TrayIconUpdate(
    DockIcon *icon,
    int mask)
{
    if (mask & ICON_CONF_IMAGE) {
        if (icon->image) {
            SaveTkImageToFile(icon);
            if (icon->bus) {
                UpdateIndicatorIcon(icon);
            }
        }
        
        if (icon->glfwWindow) {
            glfwPostEmptyEvent();
        }
    }
    
    if (mask & ICON_CONF_REDISPLAY) {
        if (icon->docked && !icon->bus) {
            CreateTrayIconWindow(icon);
        } else if (!icon->docked && icon->bus) {
            RemoveTrayIconWindow(icon);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TrayIconConfigureMethod --
 *
 *	Configure tray icon options.
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *	Updates configuration.
 *
 *----------------------------------------------------------------------
 */

static int
TrayIconConfigureMethod(
    DockIcon *icon,
    Tcl_Interp* interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[],
    int addflags)
{
    Tk_SavedOptions saved;
    Tk_Image newImage = NULL;
    int mask = 0;
    Tcl_Obj* info;

    if (objc <= 1 && !(addflags & ICON_CONF_FIRST_TIME)) {
        info = Tk_GetOptionInfo(interp, (char*)icon, icon->options,
            objc? objv[0] : NULL, icon->tkwin);
        if (info) {
            Tcl_SetObjResult(interp,info);
            return TCL_OK;
        } else {
            return TCL_ERROR;
        }
    }

    if (Tk_SetOptions(interp, icon, icon->options, objc, objv,
            icon->tkwin, &saved, &mask) != TCL_OK) {
        return TCL_ERROR;
    }
    
    mask |= addflags;
    
    if (mask & ICON_CONF_IMAGE) {
        if (icon->imageObj) {
            newImage = Tk_GetImage(interp, icon->tkwin, 
                Tcl_GetString(icon->imageObj), NULL, icon);
            if (!newImage) {
                Tk_RestoreSavedOptions(&saved);
                return TCL_ERROR;
            }
        }
        
        if (icon->image) {
            Tk_FreeImage(icon->image);
            icon->image = NULL;
        }
        icon->image = newImage;
    }
    
    Tk_FreeSavedOptions(&saved);
    TrayIconUpdate(icon, mask);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TrayIconDeleteProc --
 *
 *	Clean up tray icon resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all resources.
 *
 *----------------------------------------------------------------------
 */

static void
TrayIconDeleteProc(
    void *cd)
{
    DockIcon *icon = (DockIcon *)cd;
    
    RemoveTrayIconWindow(icon);
    
    if (icon->image) {
        Tk_FreeImage(icon->image);
    }
    
    if (icon->tempIconPath) {
        unlink(icon->tempIconPath);
        ckfree(icon->tempIconPath);
    }
    
    if (icon->b1Command) {
        Tcl_DecrRefCount(icon->b1Command);
    }
    
    if (icon->b3Command) {
        Tcl_DecrRefCount(icon->b3Command);
    }
    
    if (icon->options) {
        Tk_DeleteOptionTable(icon->options);
    }
    
    if (icon->trayAppId) {
        ckfree(icon->trayAppId);
    }
    
    if (icon->iconName) {
        ckfree(icon->iconName);
    }
    
    if (icon->iconPath) {
        ckfree(icon->iconPath);
    }
    
    if (icon->status) {
        ckfree(icon->status);
    }
    
    if (icon->tooltip) {
        ckfree(icon->tooltip);
    }
    
    if (icon->title) {
        ckfree(icon->title);
    }
    
    if (icon->bus_name) {
        ckfree(icon->bus_name);
    }
    
    if (icon->object_path) {
        ckfree(icon->object_path);
    }
    
    ckfree((char *)icon);
}

/* Option specifications. */
static const Tk_OptionSpec IconOptionSpec[] = {
    {TK_OPTION_STRING,"-image","image","Image",
        NULL, offsetof(DockIcon, imageObj), -1,
        TK_OPTION_NULL_OK, NULL,
        ICON_CONF_IMAGE | ICON_CONF_REDISPLAY},
    {TK_OPTION_STRING,"-class","class","Class",
        "TrayIcon", offsetof(DockIcon, classObj), -1,
        0, NULL, 0},
    {TK_OPTION_BOOLEAN,"-docked","docked","Docked",
        "1", -1, offsetof(DockIcon, docked), 0, NULL,
        ICON_CONF_REDISPLAY},
    {TK_OPTION_BOOLEAN,"-visible","visible","Visible",
        "1", -1, offsetof(DockIcon, visible), 0, NULL,
        0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0}
};

/*
 *----------------------------------------------------------------------
 *
 * TrayIconCreateCmd --
 *
 *	Create tray command and window.
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *	Creates new tray icon.
 *
 *----------------------------------------------------------------------
 */

static int
TrayIconCreateCmd(
    void *cd,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    Tk_Window mainWindow = (Tk_Window)cd;
    DockIcon *icon;
    const char* windowName;
    size_t nameLen;

    if (objc < 2 || (objc % 2)) {
        Tcl_WrongNumArgs(interp, 1, objv, "pathName ?option value ...?");
        return TCL_ERROR;
    }

    icon = (DockIcon*)ckalloc(sizeof(DockIcon));
    if (!icon) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("out of memory", -1));
        return TCL_ERROR;
    }
    
    memset(icon, 0, sizeof(*icon));
    icon->dbus_status = STATUS_ACTIVE;
    icon->category = CATEGORY_APPLICATION_STATUS;
    
    /* Create Tk window. */
    icon->tkwin = Tk_CreateWindowFromPath(interp, mainWindow,
        Tcl_GetString(objv[1]), "");
    if (icon->tkwin == NULL) {
        ckfree((char *)icon);
        return TCL_ERROR;
    }
    
    Tk_SetClass(icon->tkwin, Tk_GetUid("TrayIcon"));
    
    /* Initialize options. */
    icon->options = Tk_CreateOptionTable(interp, IconOptionSpec);
    if (Tk_InitOptions(interp, (char*)icon, icon->options, icon->tkwin) != TCL_OK) {
        Tk_DestroyWindow(icon->tkwin);
        ckfree((char *)icon);
        return TCL_ERROR;
    }
    
    icon->interp = interp;
    
    /* Generate app ID. */
    windowName = Tk_Name(icon->tkwin);
    nameLen = strlen(windowName);
    icon->trayAppId = (char *)ckalloc(nameLen + 1);
    strcpy(icon->trayAppId, windowName);
    
    /* Set default status */
    icon->status = ckalloc(strlen("active") + 1);
    strcpy(icon->status, "active");
    
    /* Configure initial options. */
    if (objc > 3) {
        if (TrayIconConfigureMethod(icon, interp, objc-2, objv+2,
            ICON_CONF_FIRST_TIME) != TCL_OK) {
            TrayIconDeleteProc(icon);
            return TCL_ERROR;
        }
    }
    
    /* Create command. */
    icon->widgetCmd = Tcl_CreateObjCommand2(interp, Tcl_GetString(objv[1]),
        TrayIconObjectCmd, icon, TrayIconDeleteProc);
    
    if (!icon->widgetCmd) {
        TrayIconDeleteProc(icon);
        return TCL_ERROR;
    }
    
    Tcl_SetObjResult(interp, objv[1]);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Tktray_Init --
 *
 *	Initialize the systray command.
 *
 * Results:
 *	TCL_OK.
 *
 * Side effects:
 *	Registers systray command.
 *
 *----------------------------------------------------------------------
 */

int
Tktray_Init(
    Tcl_Interp *interp)
{
    Tcl_CreateObjCommand2(interp, "::tk::systray::_systray",
        TrayIconCreateCmd, Tk_MainWindow(interp), NULL);
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
