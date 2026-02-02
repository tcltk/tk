/*
 * tkUnixSysTray.c -- Wayland/GLFW version
 *
 *	tkUnixSysTray.c implements a "systray" Tcl command which permits to
 *	change the system tray/taskbar icon of a Tk toplevel window and
 *	to post system notifications.
 *
 * Copyright © 2005 Anton Kovalenko
 * Copyright © 2020 Kevin Walzer
 * Copyright © 2024 Wayland/GLFW Port
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* GLFW for Wayland support */
#include <GLFW/glfw3.h>

#define SYSTEM_TRAY_REQUEST_DOCK    0
#define SYSTEM_TRAY_BEGIN_MESSAGE   1
#define SYSTEM_TRAY_CANCEL_MESSAGE  2

/* Flags of widget configuration options */
#define ICON_CONF_IMAGE         (1<<0)
#define ICON_CONF_REDISPLAY     (1<<1)
#define ICON_CONF_FIRST_TIME    (1<<4)

/* Widget states */
#define ICON_FLAG_REDRAW_PENDING    (1<<0)
#define ICON_FLAG_DIRTY_EDGES       (1<<2)

/* Data structure representing dock widget */
typedef struct {
    /* standard for widget */
    Tk_Window tkwin;
    Tk_OptionTable options;
    Tcl_Interp *interp;
    Tcl_Command widgetCmd;

    Tk_Image image; /* image to be drawn */
    int imageWidth, imageHeight;
    
    GLFWwindow* glfwWindow; /* GLFW window for system tray */
    void* waylandTraySurface; /* Wayland surface handle */
    
    int flags;
    int msgid;
    
    int width, height;
    int visible;
    int docked;
    Tcl_Obj *imageObj;
    Tcl_Obj *classObj;
    
    char* trayAppId; /* App ID for Wayland */
} DockIcon;

/* Forward declarations */
static Tcl_ObjCmdProc2 TrayIconCreateCmd;
static Tcl_ObjCmdProc2 TrayIconObjectCmd;
static Tcl_CmdDeleteProc TrayIconDeleteProc;
static int TrayIconConfigureMethod(DockIcon *icon, Tcl_Interp *interp,
    Tcl_Size objc, Tcl_Obj *const objv[], int addflags);
static void TrayIconUpdate(DockIcon* icon, int mask);

/* GLFW callbacks */
static void glfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "GLFW Error: %s\n", description);
}

static void glfwWindowCloseCallback(GLFWwindow* window) {
    /* Handle window close */
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
        if (objc != 3) {
            Tcl_WrongNumArgs(interp,2,objv,"option");
            return TCL_ERROR;
        }
        Tcl_Obj* optionValue = Tk_GetOptionValue(interp,(char*)icon,
            icon->options,objv[2],icon->tkwin);
        if (optionValue) {
            Tcl_SetObjResult(interp,optionValue);
            return TCL_OK;
        } else {
            return TCL_ERROR;
        }
    }

    case XWC_BALLOON: {
        if ((objc != 3) && (objc != 4)) {
            Tcl_WrongNumArgs(interp, 2, objv, "message ?timeout?");
            return TCL_ERROR;
        }
        
        /* Wayland notification - notifications will be handled by libnotify binding */
        const char* message = Tcl_GetString(objv[2]);
        
        /* The actual notification will be sent through the libnotify binding */
        Tcl_SetObjResult(interp, Tcl_NewIntObj(++icon->msgid));
        return TCL_OK;
    }

    case XWC_CANCEL: {
        /* Cancel notifications - handled by libnotify binding */
        return TCL_OK;
    }

    case XWC_BBOX: {
        /* Return dummy bounding box */
        Tcl_Obj* bboxObj = Tcl_NewObj();
        int bbox[4] = {0, 0, 100, 100}; /* Default size */
        for (int i = 0; i < 4; ++i) {
            Tcl_ListObjAppendElement(interp, bboxObj, Tcl_NewIntObj(bbox[i]));
        }
        Tcl_SetObjResult(interp, bboxObj);
        return TCL_OK;
    }

    case XWC_DOCKED:
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(icon->docked && icon->glfwWindow != NULL));
        return TCL_OK;

    case XWC_ORIENTATION:
        /* Orientation not supported in simplified Wayland version */
        Tcl_SetObjResult(interp, Tcl_NewStringObj("horizontal", TCL_INDEX_NONE));
        return TCL_OK;
    }
    
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateTrayIconWindow --
 *
 *	Create and configure the window for the icon tray using GLFW.
 *
 *----------------------------------------------------------------------
 */

static void
CreateTrayIconWindow(
    DockIcon *icon)
{
    /* Initialize GLFW if not already initialized */
    static int glfwInitialized = 0;
    if (!glfwInitialized) {
        if (!glfwInit()) {
            fprintf(stderr, "Failed to initialize GLFW\n");
            return;
        }
        glfwSetErrorCallback(glfwErrorCallback);
        glfwInitialized = 1;
    }
    
    /* Create a hidden GLFW window for the tray icon */
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    
    icon->glfwWindow = glfwCreateWindow(64, 64, 
                                       icon->trayAppId ? icon->trayAppId : "TrayIcon",
                                       NULL, NULL);
    
    if (!icon->glfwWindow) {
        fprintf(stderr, "Failed to create GLFW window for tray icon\n");
        return;
    }
    
    glfwSetWindowCloseCallback(icon->glfwWindow, glfwWindowCloseCallback);
    
    /* Set window position (usually bottom-right corner) */
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primary);
    glfwSetWindowPos(icon->glfwWindow, mode->width - 100, mode->height - 100);
}

/*
 *----------------------------------------------------------------------
 *
 * TrayIconUpdate --
 *
 *	Update tray icon based on configuration changes.
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
            Tk_SizeOfImage(icon->image, &icon->imageWidth, &icon->imageHeight);
        }
    }
    
    /* Create or destroy tray window based on docked state */
    if (mask & ICON_CONF_REDISPLAY) {
        if (icon->docked && !icon->glfwWindow) {
            CreateTrayIconWindow(icon);
        } else if (!icon->docked && icon->glfwWindow) {
            glfwDestroyWindow(icon->glfwWindow);
            icon->glfwWindow = NULL;
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

    if (objc <= 1 && !(addflags & ICON_CONF_FIRST_TIME)) {
        Tcl_Obj* info = Tk_GetOptionInfo(interp, (char*)icon, icon->options,
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
 *----------------------------------------------------------------------
 */

static void
TrayIconDeleteProc(
    void *cd)
{
    DockIcon *icon = (DockIcon *)cd;
    
    if (icon->glfwWindow) {
        glfwDestroyWindow(icon->glfwWindow);
        icon->glfwWindow = NULL;
    }
    
    if (icon->image) {
        Tk_FreeImage(icon->image);
    }
    
    if (icon->options) {
        Tk_DeleteOptionTable(icon->options);
    }
    
    if (icon->trayAppId) {
        Tcl_Free(icon->trayAppId);
    }
    
    Tcl_Free(icon);
}

/* Option specifications */
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
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0}
};

/*
 *----------------------------------------------------------------------
 *
 * TrayIconCreateCmd --
 *
 *      Create tray command and window.
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

    if (objc < 2 || (objc % 2)) {
        Tcl_WrongNumArgs(interp, 1, objv, "pathName ?option value ...?");
        return TCL_ERROR;
    }

    icon = (DockIcon*)Tcl_AttemptAlloc(sizeof(DockIcon));
    if (!icon) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("out of memory", TCL_INDEX_NONE));
        return TCL_ERROR;
    }
    
    memset(icon, 0, sizeof(*icon));
    
    /* Create Tk window */
    icon->tkwin = Tk_CreateWindowFromPath(interp, mainWindow,
        Tcl_GetString(objv[1]), "");
    if (icon->tkwin == NULL) {
        Tcl_Free(icon);
        return TCL_ERROR;
    }
    
    Tk_SetClass(icon->tkwin, Tk_GetUid("TrayIcon"));
    
    /* Initialize options */
    icon->options = Tk_CreateOptionTable(interp, IconOptionSpec);
    if (Tk_InitOptions(interp, (char*)icon, icon->options, icon->tkwin) != TCL_OK) {
        Tk_DestroyWindow(icon->tkwin);
        Tcl_Free(icon);
        return TCL_ERROR;
    }
    
    icon->interp = interp;
    
    /* Generate app ID for Wayland */
    const char* windowName = Tk_Name(icon->tkwin);
    icon->trayAppId = Tcl_Alloc(strlen(windowName) + 1);
    strcpy(icon->trayAppId, windowName);
    
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
 *      Initialize the command.
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
