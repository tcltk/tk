/*
 * tkWaylandSysTray.c --
 *
 *	System tray/notification icon support for Wayland using libayatana-appindicator3
 *	with GLFW integration. Implements a "systray" Tcl command which permits changing
 *	the system tray icon and posting system notifications.
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
#include <libayatana-appindicator/app-indicator.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* Flags of widget configuration options. */
#define ICON_CONF_IMAGE         (1<<0)
#define ICON_CONF_REDISPLAY     (1<<1)
#define ICON_CONF_FIRST_TIME    (1<<4)

/* Widget states. */
#define ICON_FLAG_REDRAW_PENDING    (1<<0)
#define ICON_FLAG_DIRTY_EDGES       (1<<2)

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
    
    /* GLFW window for fallback/drawing support */
    GLFWwindow* glfwWindow;
    Drawable drawable;
    
    /* AppIndicator for system tray */
    AppIndicator *indicator;
    
    /* Cached icon information */
    char *tempIconPath;  /* Path to temporary icon file */
    time_t lastIconUpdate; /* Last time icon was updated */
    
    int flags;
    int msgid;
    
    int width, height;
    int visible;
    int docked;
    Tcl_Obj *imageObj;
    Tcl_Obj *classObj;
    
    char* trayAppId;  /* App ID for Wayland */
    char* iconName;   /* Name of the icon (filename or themed icon name) */
    char* iconPath;   /* Path to icon file if using file-based icon */
    char* status;     /* Status: "active", "passive", "attention" */
} DockIcon;

/* Forward declarations */
static Tcl_ObjCmdProc2 TrayIconCreateCmd;
static Tcl_ObjCmdProc2 TrayIconObjectCmd;
static Tcl_CmdDeleteProc TrayIconDeleteProc;
static int TrayIconConfigureMethod(DockIcon *icon, Tcl_Interp *interp,
    Tcl_Size objc, Tcl_Obj *const objv[], int addflags);
static void TrayIconUpdate(DockIcon* icon, int mask);
static void CreateTrayIconWindow(DockIcon *icon);
static void UpdateIndicatorIcon(DockIcon *icon);
static void UpdateIndicatorStatus(DockIcon *icon);
static int SaveTkImageToFile(DockIcon *icon);
static int WritePngHeader(FILE *fp, int width, int height);
static int WritePngData(FILE *fp, unsigned char *pixels, int width, int height);

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
        XWC_BBOX, XWC_DOCKED, XWC_ORIENTATION};
    const char *st_wcmd[] = {"configure", "cget", "balloon", "cancel",
        "bbox", "docked", "orientation", NULL};

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

    case XWC_BALLOON: {
        const char* title;
        const char* message;
        
        if ((objc != 3) && (objc != 4) && (objc != 5)) {
            Tcl_WrongNumArgs(interp, 2, objv, "message ?title? ?timeout?");
            return TCL_ERROR;
        }
        
        message = Tcl_GetString(objv[2]);
        title = (objc >= 4) ? Tcl_GetString(objv[3]) : "Notification";
        
        /* 
         * Update indicator status to "attention" to show notification.
         */
        if (icon->status) {
            ckfree(icon->status);
        }
        icon->status = ckalloc(strlen("attention") + 1);
        strcpy(icon->status, "attention");
        
        if (icon->indicator) {
            UpdateIndicatorStatus(icon);
        }
        
        Tcl_SetObjResult(interp, Tcl_NewIntObj(++icon->msgid));
        return TCL_OK;
    }

    case XWC_CANCEL:
        /* Cancel notifications by setting status back to active */
        if (icon->status) {
            ckfree(icon->status);
        }
        icon->status = ckalloc(strlen("active") + 1);
        strcpy(icon->status, "active");
        
        if (icon->indicator) {
            UpdateIndicatorStatus(icon);
        }
        return TCL_OK;

    case XWC_BBOX:
        /* Return bounding box of tray icon (estimated size) */
        if (icon->indicator) {
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
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(icon->docked && icon->indicator != NULL));
        return TCL_OK;

    case XWC_ORIENTATION:
        /* Orientation not meaningful in libayatana-appindicator3 */
        Tcl_SetObjResult(interp, Tcl_NewStringObj("horizontal", TCL_INDEX_NONE));
        return TCL_OK;
    }
    
    return TCL_OK;
}

/*
 * Write PNG header (simplified version)
 */
static int
WritePngHeader(
    FILE *fp,
    int width,
    int height)
{
    /* Simple PNG header for 32-bit RGBA */
    unsigned char header[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,  /* PNG signature */
        0x00, 0x00, 0x00, 0x0D,                          /* IHDR chunk length */
        0x49, 0x48, 0x44, 0x52,                          /* "IHDR" */
        (width >> 24) & 0xFF, (width >> 16) & 0xFF,     /* Width */
        (width >> 8) & 0xFF, width & 0xFF,
        (height >> 24) & 0xFF, (height >> 16) & 0xFF,   /* Height */
        (height >> 8) & 0xFF, height & 0xFF,
        0x08,                                            /* Bit depth = 8 */
        0x06,                                            /* Color type = RGBA */
        0x00,                                            /* Compression = deflate */
        0x00,                                            /* Filter = adaptive */
        0x00,                                            /* Interlace = none */
        0x00, 0x00, 0x00, 0x00,                          /* CRC (placeholder) */
    };
    
    /* Calculate CRC (simplified - in real implementation use proper CRC) */
    unsigned long crc = 0;
    for (int i = 12; i < 29; i++) {
        crc += header[i];
    }
    header[29] = (crc >> 24) & 0xFF;
    header[30] = (crc >> 16) & 0xFF;
    header[31] = (crc >> 8) & 0xFF;
    header[32] = crc & 0xFF;
    
    return fwrite(header, 1, sizeof(header), fp) == sizeof(header);
}

/*
 * Write PNG image data (simplified)
 */
static int
WritePngData(
    FILE *fp,
    unsigned char *pixels,
    int width,
    int height)
{
    int stride = width * 4;
    unsigned long data_size = height * (stride + 1); /* +1 for filter byte per row */
    
    /* IDAT chunk header */
    unsigned char idat_header[] = {
        0x00, 0x00, 0x00, 0x00,  /* Chunk length (placeholder) */
        0x49, 0x44, 0x41, 0x54,  /* "IDAT" */
    };
    
    /* Simple deflate header */
    unsigned char deflate_header[] = {0x78, 0x01};
    
    /* Write IDAT header */
    if (fwrite(idat_header, 1, sizeof(idat_header), fp) != sizeof(idat_header)) {
        return 0;
    }
    
    /* Write deflate header */
    if (fwrite(deflate_header, 1, sizeof(deflate_header), fp) != sizeof(deflate_header)) {
        return 0;
    }
    
    /* Write image data (simplified - no actual compression) */
    for (int y = 0; y < height; y++) {
        unsigned char filter_byte = 0; /* No filter */
        fputc(filter_byte, fp);
        if (fwrite(pixels + y * stride, 1, stride, fp) != stride) {
            return 0;
        }
    }
    
    /* Write deflate checksum and IEND chunk */
    unsigned char trailer[] = {
        0x00, 0x00, 0xFF, 0xFF,  /* Deflate end block */
        0x00, 0x00, 0x00, 0x00,  /* Adler32 checksum (placeholder) */
        0x00, 0x00, 0x00, 0x00,  /* IEND chunk length */
        0x49, 0x45, 0x4E, 0x44,  /* "IEND" */
        0xAE, 0x42, 0x60, 0x82,  /* IEND CRC */
    };
    
    return fwrite(trailer, 1, sizeof(trailer), fp) == sizeof(trailer);
}

/*
 * Save Tk image to PNG file
 */
static int
SaveTkImageToFile(
    DockIcon *icon)
{
    if (!icon->image) {
        return 0;
    }
    
    /* Get image dimensions */
    Tk_SizeOfImage(icon->image, &icon->imageWidth, &icon->imageHeight);
    
    if (icon->imageWidth <= 0 || icon->imageHeight <= 0) {
        return 0;
    }
    
    /* Allocate buffer for image data */
    int stride = icon->imageWidth * 4;  /* RGBA */
    unsigned char *pixels = (unsigned char *)ckalloc(icon->imageHeight * stride);
    if (!pixels) {
        return 0;
    }
    
    /* Get image data from Photo image */
    Tk_PhotoHandle photo = Tk_FindPhoto(icon->interp, Tcl_GetString(icon->imageObj));
    if (!photo) {
        ckfree((char *)pixels);
        return 0;
    }
    
    Tk_PhotoImageBlock block;
    block.pixelPtr = pixels;
    block.width = icon->imageWidth;
    block.height = icon->imageHeight;
    block.pitch = stride;
    block.pixelSize = 4;
    block.offset[0] = 0;  /* Red */
    block.offset[1] = 1;  /* Green */
    block.offset[2] = 2;  /* Blue */
    block.offset[3] = 3;  /* Alpha */
    
    if (Tk_PhotoGetImage(photo, &block) != TCL_OK) {
        ckfree((char *)pixels);
        return 0;
    }
    
    /* Clean up old temp file */
    if (icon->tempIconPath) {
        unlink(icon->tempIconPath);
        ckfree(icon->tempIconPath);
        icon->tempIconPath = NULL;
    }
    
    /* Create temporary file */
    char template[] = "/tmp/tktray_XXXXXX.png";
    int fd = mkstemps(template, 4);  /* 4 for ".png" */
    if (fd < 0) {
        ckfree((char *)pixels);
        return 0;
    }
    
    FILE *fp = fdopen(fd, "wb");
    if (!fp) {
        close(fd);
        ckfree((char *)pixels);
        return 0;
    }
    
    /* Write PNG file */
    int success = WritePngHeader(fp, icon->imageWidth, icon->imageHeight) &&
                  WritePngData(fp, pixels, icon->imageWidth, icon->imageHeight);
    
    fclose(fp);
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
 * UpdateIndicatorIcon --
 *
 *	Update the icon displayed in the system tray.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the AppIndicator icon.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateIndicatorIcon(
    DockIcon *icon)
{
    if (!icon->indicator) {
        return;
    }
    
    /* Try to use Tk image if available */
    if (icon->image && icon->tempIconPath) {
        /* Check if we need to update the icon file */
        time_t now = time(NULL);
        if (now - icon->lastIconUpdate > 1) {  /* Update if older than 1 second */
            SaveTkImageToFile(icon);
        }
        app_indicator_set_icon_full(icon->indicator, icon->tempIconPath, "Tray Icon");
    }
    /* Use specified icon name */
    else if (icon->iconName) {
        app_indicator_set_icon_full(icon->indicator, icon->iconName, "Tray Icon");
    }
    /* Use specified icon path */
    else if (icon->iconPath) {
        app_indicator_set_icon_full(icon->indicator, icon->iconPath, "Tray Icon");
    }
    /* Default fallback */
    else {
        app_indicator_set_icon_full(icon->indicator, "image-missing", "Tray Icon");
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateIndicatorStatus --
 *
 *	Update the status of the indicator.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the AppIndicator status.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateIndicatorStatus(
    DockIcon *icon)
{
    if (!icon->indicator) {
        return;
    }
    
    if (icon->status) {
        if (strcmp(icon->status, "active") == 0) {
            app_indicator_set_status(icon->indicator, APP_INDICATOR_STATUS_ACTIVE);
        } else if (strcmp(icon->status, "passive") == 0) {
            app_indicator_set_status(icon->indicator, APP_INDICATOR_STATUS_PASSIVE);
        } else if (strcmp(icon->status, "attention") == 0) {
            app_indicator_set_status(icon->indicator, APP_INDICATOR_STATUS_ATTENTION);
        } else {
            app_indicator_set_status(icon->indicator, APP_INDICATOR_STATUS_ACTIVE);
        }
    } else {
        app_indicator_set_status(icon->indicator, APP_INDICATOR_STATUS_ACTIVE);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CreateTrayIconWindow --
 *
 *	Create and configure the AppIndicator for the system tray.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates an AppIndicator and adds it to the system tray.
 *
 *----------------------------------------------------------------------
 */

static void
CreateTrayIconWindow(
    DockIcon *icon)
{
    const char *title;
    
    if (!icon->tkwin) {
        return;
    }
    
    title = icon->trayAppId ? icon->trayAppId : "TrayIcon";
    
    /* Create AppIndicator - no GTK initialization needed */
    icon->indicator = app_indicator_new(
        icon->trayAppId,                    /* Unique ID */
        "image-missing",                    /* Default icon */
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS
    );
    
    if (!icon->indicator) {
        fprintf(stderr, "Failed to create AppIndicator for tray icon\n");
        return;
    }
    
    /* Set initial properties */
    app_indicator_set_title(icon->indicator, title);
    app_indicator_set_label(icon->indicator, NULL, NULL);
    
    /* Update icon from Tk image if available */
    if (icon->image) {
        SaveTkImageToFile(icon);
        UpdateIndicatorIcon(icon);
    }
    
    /* Update status */
    UpdateIndicatorStatus(icon);
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
 *	May create/destroy AppIndicator, update image.
 *
 *----------------------------------------------------------------------
 */

static void
TrayIconUpdate(
    DockIcon *icon,
    int mask)
{
    if (mask & ICON_CONF_IMAGE) {
        /* Handle image updates */
        if (icon->image) {
            /* Save image to file and update indicator */
            SaveTkImageToFile(icon);
            if (icon->indicator) {
                UpdateIndicatorIcon(icon);
            }
        }
        
        /* Also update GLFW window for fallback/drawing */
        if (icon->glfwWindow) {
            /* Trigger redraw of GLFW window */
            glfwPostEmptyEvent();
        }
    }
    
    /* Create or destroy indicator based on docked state */
    if (mask & ICON_CONF_REDISPLAY) {
        if (icon->docked && !icon->indicator) {
            CreateTrayIconWindow(icon);
            
            /* Also create GLFW window for compatibility */
            if (!icon->glfwWindow) {
                TkWindow *winPtr = (TkWindow *)icon->tkwin;
                icon->glfwWindow = TkGlfwCreateWindow(winPtr, 
                    icon->imageWidth > 0 ? icon->imageWidth : 64,
                    icon->imageHeight > 0 ? icon->imageHeight : 64,
                    icon->trayAppId, &icon->drawable);
                if (icon->glfwWindow) {
                    glfwHideWindow(icon->glfwWindow);  /* Hide it, we use AppIndicator */
                }
            }
        } else if (!icon->docked && icon->indicator) {
            /* Destroy the AppIndicator */
            g_object_unref(icon->indicator);
            icon->indicator = NULL;
            
            /* Destroy GLFW window */
            if (icon->glfwWindow) {
                TkGlfwDestroyWindow(icon->glfwWindow);
                icon->glfwWindow = NULL;
                icon->drawable = None;
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TrayIconConfigureMethod --
 *
 *      Configure tray icon options.
 *
 * Results:
 *      TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *      Updates tray icon configuration.
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
    
    /* Handle image changes */
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
 *      Clean up tray icon resources.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Frees all resources associated with the tray icon.
 *
 *----------------------------------------------------------------------
 */

static void
TrayIconDeleteProc(
    void *cd)
{
    DockIcon *icon = (DockIcon *)cd;
    
    if (icon->indicator) {
        g_object_unref(icon->indicator);
        icon->indicator = NULL;
    }
    
    if (icon->glfwWindow) {
        TkGlfwDestroyWindow(icon->glfwWindow);
        icon->glfwWindow = NULL;
    }
    
    if (icon->image) {
        Tk_FreeImage(icon->image);
    }
    
    if (icon->tempIconPath) {
        unlink(icon->tempIconPath);
        ckfree(icon->tempIconPath);
        icon->tempIconPath = NULL;
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
    
    ckfree((char *)icon);
}

/*
 * Option specifications.
 */
static const Tk_OptionSpec IconOptionSpec[] = {
    {TK_OPTION_STRING,"-image","image","Image",
        NULL, offsetof(DockIcon, imageObj), TCL_INDEX_NONE,
        TK_OPTION_NULL_OK, NULL,
        ICON_CONF_IMAGE | ICON_CONF_REDISPLAY},
    {TK_OPTION_STRING,"-class","class","Class",
        "TrayIcon", offsetof(DockIcon, classObj), TCL_INDEX_NONE,
        0, NULL, 0},
    {TK_OPTION_BOOLEAN,"-docked","docked","Docked",
        "1", TCL_INDEX_NONE, offsetof(DockIcon, docked), 0, NULL,
        ICON_CONF_REDISPLAY},
    {TK_OPTION_BOOLEAN,"-visible","visible","Visible",
        "1", TCL_INDEX_NONE, offsetof(DockIcon, visible), 0, NULL,
        0},
    {TK_OPTION_STRING,"-status","status","Status",
        "active", TCL_INDEX_NONE, TCL_INDEX_NONE, 0, NULL,
        ICON_CONF_REDISPLAY},
    {TK_OPTION_STRING,"-iconname","iconName","IconName",
        NULL, TCL_INDEX_NONE, TCL_INDEX_NONE, TK_OPTION_NULL_OK, NULL,
        ICON_CONF_IMAGE | ICON_CONF_REDISPLAY},
    {TK_OPTION_STRING,"-iconpath","iconPath","IconPath",
        NULL, TCL_INDEX_NONE, TCL_INDEX_NONE, TK_OPTION_NULL_OK, NULL,
        ICON_CONF_IMAGE | ICON_CONF_REDISPLAY},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0}
};

/*
 *----------------------------------------------------------------------
 *
 * TrayIconCreateCmd --
 *
 *      Create tray command and window.
 *
 * Results:
 *      TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *      Creates new tray icon widget and command.
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
    icon->lastIconUpdate = 0;
    
    /* Create Tk window */
    icon->tkwin = Tk_CreateWindowFromPath(interp, mainWindow,
        Tcl_GetString(objv[1]), "");
    if (icon->tkwin == NULL) {
        ckfree((char *)icon);
        return TCL_ERROR;
    }
    
    Tk_SetClass(icon->tkwin, Tk_GetUid("TrayIcon"));
    
    /* Initialize options */
    icon->options = Tk_CreateOptionTable(interp, IconOptionSpec);
    if (Tk_InitOptions(interp, (char*)icon, icon->options, icon->tkwin) != TCL_OK) {
        Tk_DestroyWindow(icon->tkwin);
        ckfree((char *)icon);
        return TCL_ERROR;
    }
    
    icon->interp = interp;
    
    /* Generate app ID for Wayland */
    windowName = Tk_Name(icon->tkwin);
    nameLen = strlen(windowName);
    icon->trayAppId = (char *)ckalloc(nameLen + 1);
    strcpy(icon->trayAppId, windowName);
    
    /* Set default status */
    icon->status = ckalloc(strlen("active") + 1);
    strcpy(icon->status, "active");
    
    /* Configure initial options */
    if (objc > 3) {
        if (TrayIconConfigureMethod(icon, interp, objc-2, objv+2,
            ICON_CONF_FIRST_TIME) != TCL_OK) {
            TrayIconDeleteProc(icon);
            return TCL_ERROR;
        }
    }
    
    /* Create command */
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
 *      Initialize the systray command.
 *
 * Results:
 *      TCL_OK.
 *
 * Side effects:
 *      Registers the ::tk::systray::_systray command.
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
