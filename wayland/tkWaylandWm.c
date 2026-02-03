/*
 * tkWaylandWm.c --
 *
 *	This module takes care of the interactions between a Tk-based
 *	application and the window manager. Among other things, it implements
 *	the "wm" command and passes geometry information to the Wayland 
 *      compositor via GLFW. 
 *
 *
 * Copyright © 1991-1994 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkPort.h"
#include <GLFW/glfw3.h>
#include <string.h>
#include <stdlib.h>

/* Note: Many X11-specific includes removed (X11/Xlib.h, X11/Xutil.h, X11/Xatom.h, etc.) */

/*
 * Protocol identifiers - these replace X11 Atoms
 */

#define WM_DELETE_WINDOW    1
#define WM_TAKE_FOCUS      2
#define WM_SAVE_YOURSELF   3

/*
 * A data structure of the following type holds information for each window
 * manager protocol (such as WM_DELETE_WINDOW) for which a handler (i.e. a Tcl
 * command) has been defined for a particular top-level window.
 */

typedef struct ProtocolHandler {
    int protocol;		/* Protocol identifier (replaces Atom) */
    struct ProtocolHandler *nextPtr;
				/* Next in list of protocol handlers for the
				 * same top-level window, or NULL for end of
				 * list. */
    Tcl_Interp *interp;		/* Interpreter in which to invoke command. */
    char command[TKFLEXARRAY];	/* Tcl command to invoke when a client message
				 * for this protocol arrives. The actual size
				 * of the structure varies to accommodate the
				 * needs of the actual command. THIS MUST BE
				 * THE LAST FIELD OF THE STRUCTURE. */
} ProtocolHandler;

#define HANDLER_SIZE(cmdLength) \
    (offsetof(ProtocolHandler, command) + 1 + cmdLength)

/*
 * Data for [wm attributes] command:
 */

typedef struct {
    double alpha;		/* Transparency; 0.0=transparent, 1.0=opaque */
    int topmost;		/* Flag: true=>stay-on-top */
    int zoomed;			/* Flag: true=>maximized */
    int fullscreen;		/* Flag: true=>fullscreen */
} WmAttributes;

typedef enum {
    WMATT_ALPHA, WMATT_FULLSCREEN, WMATT_TOPMOST, WMATT_TYPE,
    WMATT_ZOOMED, _WMATT_LAST_ATTRIBUTE
} WmAttribute;

static const char *const WmAttributeNames[] = {
    "-alpha", "-fullscreen", "-topmost", "-type",
    "-zoomed", NULL
};

/*
 * A data structure of the following type holds window-manager-related
 * information for each top-level window in an application.
 */

typedef struct TkWmInfo {
    TkWindow *winPtr;		/* Pointer to main Tk information for this window. */
    GLFWwindow *glfwWindow;	/* GLFW window handle (replaces X Window) */
    char *title;		/* Title to display in window caption. Malloced. */
    char *iconName;		/* Name to display in icon. Malloced. */
    char *leaderName;		/* Path name of leader of window group. Malloc-ed. */
    TkWindow *containerPtr;	/* Container window for TRANSIENT_FOR, or NULL. */
    Tk_Window icon;		/* Window to use as icon, or NULL. */
    Tk_Window iconFor;		/* Window for which this is icon, or NULL. */
    int withdrawn;		/* Non-zero means window has been withdrawn. */

    /*
     * Wrapper and menubar support
     */

    TkWindow *wrapperPtr;	/* Wrapper window pointer. NULL means not created yet. */
    Tk_Window menubar;		/* Menubar window, or NULL. */
    int menuHeight;		/* Vertical space for menubar in pixels. */

    /*
     * Size hints information
     */

    int sizeHintsFlags;		/* Flags for size hints. */
    int minWidth, minHeight;	/* Minimum dimensions. */
    int maxWidth, maxHeight;	/* Maximum dimensions. 0 to default. */
    Tk_Window gridWin;		/* Window controlling gridding, or NULL. */
    int widthInc, heightInc;	/* Size change increments. */
    struct {
	int x;			/* numerator */
	int y;			/* denominator */
    } minAspect, maxAspect;	/* Min/max aspect ratios. */
    int reqGridWidth, reqGridHeight;
				/* Requested dimensions in grid units. */
    int gravity;		/* Desired window gravity. */

    /*
     * Size and location management
     */

    int width, height;		/* Desired dimensions in pixels or grid units. */
    int x, y;			/* Desired X and Y coordinates. */
    int parentWidth, parentHeight;
				/* Parent dimensions including border. */
    int xInParent, yInParent;	/* Offset within parent. */
    int configWidth, configHeight;
				/* Last requested wrapper dimensions. */

    /*
     * Virtual root information (kept for compatibility but mostly unused in Wayland)
     */

    int vRootX, vRootY;		/* Virtual root position (unused in Wayland). */
    int vRootWidth, vRootHeight;/* Virtual root/screen dimensions. */

    /*
     * Miscellaneous information
     */

    WmAttributes attributes;	/* Current [wm attributes] state. */
    WmAttributes reqState;	/* Requested [wm attributes] state. */
    ProtocolHandler *protPtr;	/* Protocol handlers list, or NULL. */
    Tcl_Size cmdArgc;		/* Number of command arguments. */
    const char **cmdArgv;	/* Command arguments array. */
    char *clientMachine;	/* Client machine name, or NULL. */
    int flags;			/* Miscellaneous flags. */
    int numTransients;		/* Number of transients. */
    int iconDataSize;		/* Icon image data size. */
    unsigned char *iconDataPtr;	/* Icon image data. */
    GLFWimage *glfwIcon;	/* GLFW icon images */
    int glfwIconCount;		/* Number of GLFW icon images */
    struct TkWmInfo *nextPtr;	/* Next in list of top-level windows. */
} WmInfo;

/*
 * Flag values for WmInfo structures (same as X11 version for compatibility)
 */

#define WM_NEVER_MAPPED			(1<<0)
#define WM_UPDATE_PENDING		(1<<1)
#define WM_NEGATIVE_X			(1<<2)
#define WM_NEGATIVE_Y			(1<<3)
#define WM_UPDATE_SIZE_HINTS		(1<<4)
#define WM_SYNC_PENDING			(1<<5)
#define WM_CREATE_PENDING		(1<<6)
#define WM_ABOUT_TO_MAP			(1<<9)
#define WM_MOVE_PENDING			(1<<10)
#define WM_COLORMAPS_EXPLICIT		(1<<11)
#define WM_ADDED_TOPLEVEL_COLORMAP	(1<<12)
#define WM_WIDTH_NOT_RESIZABLE		(1<<13)
#define WM_HEIGHT_NOT_RESIZABLE		(1<<14)
#define WM_WITHDRAWN			(1<<15)
#define WM_FULLSCREEN_PENDING		(1<<16)

/*
 * Size hint flags (from X11, kept for compatibility)
 */

#define USPosition	(1 << 0)
#define USSize		(1 << 1)
#define PPosition	(1 << 2)
#define PSize		(1 << 3)
#define PMinSize	(1 << 4)
#define PMaxSize	(1 << 5)
#define PResizeInc	(1 << 6)
#define PAspect		(1 << 7)
#define PBaseSize	(1 << 8)
#define PWinGravity	(1 << 9)

/*
 * Window states
 */

#define WithdrawnState 0
#define NormalState 1
#define IconicState 3

/*
 * Gravity constants (from X11, kept for compatibility)
 */

#define NorthWestGravity 1
#define NorthGravity 2
#define NorthEastGravity 3
#define WestGravity 4
#define CenterGravity 5
#define EastGravity 6
#define SouthWestGravity 7
#define SouthGravity 8
#define SouthEastGravity 9
#define StaticGravity 10

/*
 * Global list of all top-level windows
 */

static WmInfo *firstWmPtr = NULL;

/*
 * GLFW callback function pointers
 */

static void (*glfwCloseCallback)(GLFWwindow*);
static void (*glfwFocusCallback)(GLFWwindow*, int);
static void (*glfwIconifyCallback)(GLFWwindow*, int);
static void (*glfwMaximizeCallback)(GLFWwindow*, int);
static void (*glfwFramebufferSizeCallback)(GLFWwindow*, int, int);
static void (*glfwWindowPosCallback)(GLFWwindow*, int, int);

/*
 * Forward declarations
 */

static void		TopLevelEventProc(ClientData clientData, XEvent *eventPtr);
static void		TopLevelReqProc(ClientData clientData, Tk_Window tkwin);
static void		UpdateGeometryInfo(ClientData clientData);
static void		UpdateHints(TkWindow *winPtr);
static void		UpdateSizeHints(TkWindow *winPtr);
static void		UpdateTitle(TkWindow *winPtr);
static void		UpdatePhotoIcon(TkWindow *winPtr);
static void		UpdateVRootGeometry(WmInfo *wmPtr);
static void		WaitForMapNotify(TkWindow *winPtr, int mapped);
static void		MenubarReqProc(ClientData clientData, Tk_Window tkwin);
static int		ParseGeometry(Tcl_Interp *interp, char *string, TkWindow *winPtr);
static void		WmUpdateGeom(WmInfo *wmPtr, TkWindow *winPtr);
static int		WmAspectCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmAttributesCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmClientCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmColormapwindowsCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmCommandCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmDeiconifyCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmFocusmodelCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmForgetCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmFrameCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmGeometryCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmGridCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmGroupCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmIconbadgeCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmIconbitmapCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmIconifyCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmIconmaskCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmIconnameCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmIconphotoCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmIconpositionCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmIconwindowCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmManageCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmMaxsizeCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmMinsizeCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmOverrideredirectCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmPositionfromCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmProtocolCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmResizableCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmSizefromCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmStackorderCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmStateCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmTitleCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmTransientCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int		WmWithdrawCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

/*
 * New forward declarations for GLFW integration
 */
static void		GlfwCloseCallback(GLFWwindow *window);
static void		GlfwFocusCallback(GLFWwindow *window, int focused);
static void		GlfwIconifyCallback(GLFWwindow *window, int iconified);
static void		GlfwMaximizeCallback(GLFWwindow *window, int maximized);
static void		GlfwFramebufferSizeCallback(GLFWwindow *window, int width, int height);
static void		GlfwWindowPosCallback(GLFWwindow *window, int x, int y);
static WmInfo*		FindWmInfoByGlfwWindow(GLFWwindow *window);
static void		CreateGlfwWindow(TkWindow *winPtr);
static void		DestroyGlfwWindow(WmInfo *wmPtr);
static void		ConvertPhotoToGlfwIcon(TkWindow *winPtr, Tk_PhotoHandle photo, int index);
static void		ApplyWindowHints(TkWindow *winPtr);
static void		HandleProtocol(WmInfo *wmPtr, int protocol);
static void		ApplyFullscreenState(TkWindow *winPtr);

/*
 * Geometry manager type
 */

static Tk_GeomMgr wmMgrType = {
    "wm",			/* name */
    TopLevelReqProc,		/* requestProc */
    NULL,			/* lostSlaveProc */
};

/*
 *--------------------------------------------------------------
 *
 * TkWmNewWindow --
 *
 *	This function is invoked whenever a new top-level window is created.
 *	Its job is to initialize the WmInfo structure for the window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A WmInfo structure gets allocated and initialized.
 *
 *--------------------------------------------------------------
 */

void
TkWmNewWindow(
    TkWindow *winPtr)		/* Newly-created top-level window. */
{
    WmInfo *wmPtr;

    wmPtr = (WmInfo *)ckalloc(sizeof(WmInfo));
    memset(wmPtr, 0, sizeof(WmInfo));
    
    wmPtr->winPtr = winPtr;
    wmPtr->glfwWindow = NULL;
    wmPtr->title = NULL;
    wmPtr->iconName = NULL;
    wmPtr->leaderName = NULL;
    wmPtr->containerPtr = NULL;
    wmPtr->icon = NULL;
    wmPtr->iconFor = NULL;
    wmPtr->withdrawn = 1;
    
    wmPtr->wrapperPtr = NULL;
    wmPtr->menubar = NULL;
    wmPtr->menuHeight = 0;
    
    wmPtr->sizeHintsFlags = 0;
    wmPtr->minWidth = wmPtr->minHeight = 1;
    wmPtr->maxWidth = wmPtr->maxHeight = 0;
    wmPtr->gridWin = NULL;
    wmPtr->widthInc = wmPtr->heightInc = 1;
    wmPtr->minAspect.x = wmPtr->minAspect.y = 1;
    wmPtr->maxAspect.x = wmPtr->maxAspect.y = 1;
    wmPtr->reqGridWidth = wmPtr->reqGridHeight = -1;
    wmPtr->gravity = NorthWestGravity;
    
    wmPtr->width = -1;
    wmPtr->height = -1;
    wmPtr->x = winPtr->changes.x;
    wmPtr->y = winPtr->changes.y;
    wmPtr->parentWidth = winPtr->changes.width + 2*winPtr->changes.border_width;
    wmPtr->parentHeight = winPtr->changes.height + 2*winPtr->changes.border_width;
    wmPtr->xInParent = 0;
    wmPtr->yInParent = 0;
    wmPtr->configWidth = -1;
    wmPtr->configHeight = -1;
    
    wmPtr->vRootX = wmPtr->vRootY = 0;
    wmPtr->vRootWidth = 800;
    wmPtr->vRootHeight = 600;
    
    wmPtr->attributes.alpha = 1.0;
    wmPtr->attributes.topmost = 0;
    wmPtr->attributes.zoomed = 0;
    wmPtr->attributes.fullscreen = 0;
    wmPtr->reqState = wmPtr->attributes;
    
    wmPtr->protPtr = NULL;
    wmPtr->cmdArgc = 0;
    wmPtr->cmdArgv = NULL;
    wmPtr->clientMachine = NULL;
    wmPtr->flags = WM_NEVER_MAPPED;
    wmPtr->numTransients = 0;
    wmPtr->iconDataSize = 0;
    wmPtr->iconDataPtr = NULL;
    wmPtr->glfwIcon = NULL;
    wmPtr->glfwIconCount = 0;
    
    wmPtr->nextPtr = firstWmPtr;
    firstWmPtr = wmPtr;
    winPtr->wmInfoPtr = wmPtr;

    UpdateVRootGeometry(wmPtr);

    /*
     * Arrange for geometry requests to be reflected to the window manager.
     */

    Tk_ManageGeometry((Tk_Window) winPtr, &wmMgrType, (ClientData) 0);
}

/*
 *--------------------------------------------------------------
 *
 * CreateGlfwWindow --
 *
 *	Creates a GLFW window for a Tk top-level window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	GLFW window is created and callbacks are set up.
 *
 *--------------------------------------------------------------
 */

static void
CreateGlfwWindow(
    TkWindow *winPtr)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    TkWindow *parentPtr = (TkWindow *)Tk_Parent((Tk_Window)winPtr);
    int width, height;
    GLFWwindow *share = NULL;
    
    if (wmPtr->glfwWindow != NULL) {
	return;
    }
    
    /* Determine initial size */
    if (wmPtr->width > 0 && wmPtr->height > 0) {
	width = wmPtr->width;
	height = wmPtr->height;
    } else {
	width = winPtr->reqWidth;
	height = winPtr->reqHeight;
    }
    
    /* Find a window to share resources with */
    for (WmInfo *other = firstWmPtr; other != NULL; other = other->nextPtr) {
	if (other->glfwWindow != NULL && other != wmPtr) {
	    share = other->glfwWindow;
	    break;
	}
    }
    
    /* Apply window hints before creation */
    ApplyWindowHints(winPtr);
    
    /* Create the window */
    wmPtr->glfwWindow = glfwCreateWindow(width, height, "", NULL, share);
    if (wmPtr->glfwWindow == NULL) {
	Tcl_Panic("Failed to create GLFW window");
	return;
    }
    
    /* Set up callbacks */
    glfwSetWindowCloseCallback(wmPtr->glfwWindow, GlfwCloseCallback);
    glfwSetWindowFocusCallback(wmPtr->glfwWindow, GlfwFocusCallback);
    glfwSetWindowIconifyCallback(wmPtr->glfwWindow, GlfwIconifyCallback);
    glfwSetWindowMaximizeCallback(wmPtr->glfwWindow, GlfwMaximizeCallback);
    glfwSetFramebufferSizeCallback(wmPtr->glfwWindow, GlfwFramebufferSizeCallback);
    glfwSetWindowPosCallback(wmPtr->glfwWindow, GlfwWindowPosCallback);
    
    /* Set initial properties */
    UpdateTitle(winPtr);
    UpdateSizeHints(winPtr);
    
    if (wmPtr->attributes.alpha != 1.0) {
	glfwSetWindowOpacity(wmPtr->glfwWindow, (float)wmPtr->attributes.alpha);
    }
    
    /* Set window icon if available */
    if (wmPtr->glfwIcon != NULL) {
	glfwSetWindowIcon(wmPtr->glfwWindow, wmPtr->glfwIconCount, wmPtr->glfwIcon);
    }
    
    /* Apply initial state */
    if (wmPtr->attributes.zoomed) {
	glfwMaximizeWindow(wmPtr->glfwWindow);
    }
    
    /* Store user pointer for finding WmInfo later */
    glfwSetWindowUserPointer(wmPtr->glfwWindow, wmPtr);
}

/*
 *--------------------------------------------------------------
 *
 * DestroyGlfwWindow --
 *
 *	Destroys a GLFW window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	GLFW window is destroyed.
 *
 *--------------------------------------------------------------
 */

static void
DestroyGlfwWindow(
    WmInfo *wmPtr)
{
    if (wmPtr->glfwWindow == NULL) {
	return;
    }
    
    /* Remove callbacks first */
    glfwSetWindowCloseCallback(wmPtr->glfwWindow, NULL);
    glfwSetWindowFocusCallback(wmPtr->glfwWindow, NULL);
    glfwSetWindowIconifyCallback(wmPtr->glfwWindow, NULL);
    glfwSetWindowMaximizeCallback(wmPtr->glfwWindow, NULL);
    glfwSetFramebufferSizeCallback(wmPtr->glfwWindow, NULL);
    glfwSetWindowPosCallback(wmPtr->glfwWindow, NULL);
    
    glfwDestroyWindow(wmPtr->glfwWindow);
    wmPtr->glfwWindow = NULL;
}

/*
 *--------------------------------------------------------------
 *
 * TkWmMapWindow --
 *
 *	This function is invoked to map a top-level window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window may be mapped.
 *
 *--------------------------------------------------------------
 */

void
TkWmMapWindow(
    TkWindow *winPtr)		/* Top-level window to map. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    if (wmPtr->flags & WM_NEVER_MAPPED) {
	wmPtr->flags &= ~WM_NEVER_MAPPED;

	/* Create GLFW window if not embedded */
	if (!Tk_IsEmbedded(winPtr)) {
	    CreateGlfwWindow(winPtr);
	}

	UpdateHints(winPtr);
	UpdateTitle(winPtr);
	UpdatePhotoIcon(winPtr);
    }

    if (wmPtr->withdrawn) {
	return;
    }

    if (wmPtr->flags & WM_UPDATE_PENDING) {
	Tcl_CancelIdleCall(UpdateGeometryInfo, (ClientData) winPtr);
    }
    UpdateGeometryInfo((ClientData) winPtr);

    /* Show GLFW window */
    if (wmPtr->glfwWindow != NULL) {
	glfwShowWindow(wmPtr->glfwWindow);
    }

    winPtr->flags |= TK_MAPPED;
}

/*
 *--------------------------------------------------------------
 *
 * TkWmUnmapWindow --
 *
 *	Unmaps a top-level window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Window is unmapped.
 *
 *--------------------------------------------------------------
 */

void
TkWmUnmapWindow(
    TkWindow *winPtr)		/* Top-level window to unmap. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    if (wmPtr->glfwWindow != NULL) {
	glfwHideWindow(wmPtr->glfwWindow);
    }

    winPtr->flags &= ~TK_MAPPED;
}

/*
 *--------------------------------------------------------------
 *
 * TkWmDeadWindow --
 *
 *	Cleanup when a top-level window is deleted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	WmInfo structure is freed.
 *
 *--------------------------------------------------------------
 */

void
TkWmDeadWindow(
    TkWindow *winPtr)		/* Top-level window being deleted. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    WmInfo *wmPtr2;

    if (wmPtr == NULL) {
	return;
    }

    /* Clean up event handlers */
    Tk_DeleteEventHandler((Tk_Window) winPtr,
	    StructureNotifyMask | PropertyChangeMask,
	    TopLevelEventProc, (ClientData) winPtr);

    if (wmPtr->flags & WM_UPDATE_PENDING) {
	Tcl_CancelIdleCall(UpdateGeometryInfo, (ClientData) winPtr);
    }

    /* Destroy GLFW window */
    DestroyGlfwWindow(wmPtr);

    /* Destroy wrapper */
    if (wmPtr->wrapperPtr != NULL) {
	Tk_DestroyWindow((Tk_Window) wmPtr->wrapperPtr);
	wmPtr->wrapperPtr = NULL;
    }

    /* Free allocated resources */
    if (wmPtr->title != NULL) {
	ckfree(wmPtr->title);
    }
    if (wmPtr->iconName != NULL) {
	ckfree(wmPtr->iconName);
    }
    if (wmPtr->leaderName != NULL) {
	ckfree(wmPtr->leaderName);
    }
    if (wmPtr->menubar != NULL) {
	Tk_DestroyWindow(wmPtr->menubar);
    }
    if (wmPtr->icon != NULL) {
	Tk_DestroyWindow(wmPtr->icon);
    }
    if (wmPtr->iconDataPtr != NULL) {
	ckfree((char *) wmPtr->iconDataPtr);
    }
    
    /* Free GLFW icon data */
    if (wmPtr->glfwIcon != NULL) {
	for (int i = 0; i < wmPtr->glfwIconCount; i++) {
	    if (wmPtr->glfwIcon[i].pixels != NULL) {
		ckfree((char *)wmPtr->glfwIcon[i].pixels);
	    }
	}
	ckfree((char *)wmPtr->glfwIcon);
    }
    
    /* Free protocol handlers */
    while (wmPtr->protPtr != NULL) {
	ProtocolHandler *protPtr = wmPtr->protPtr;
	wmPtr->protPtr = protPtr->nextPtr;
	Tcl_EventuallyFree((ClientData) protPtr, TCL_DYNAMIC);
    }
    
    if (wmPtr->cmdArgv != NULL) {
	ckfree((char *) wmPtr->cmdArgv);
    }
    if (wmPtr->clientMachine != NULL) {
	ckfree((char *) wmPtr->clientMachine);
    }

    /* Remove from global list */
    if (wmPtr == firstWmPtr) {
	firstWmPtr = wmPtr->nextPtr;
    } else {
	for (wmPtr2 = firstWmPtr; wmPtr2 != NULL; wmPtr2 = wmPtr2->nextPtr) {
	    if (wmPtr2->nextPtr == wmPtr) {
		wmPtr2->nextPtr = wmPtr->nextPtr;
		break;
	    }
	}
    }

    winPtr->wmInfoPtr = NULL;
    ckfree((char *) wmPtr);
}

/*
 *--------------------------------------------------------------
 *
 * TkWmSetClass --
 *
 *	Sets the window class.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Window class is set (no-op in Wayland/GLFW).
 *
 *--------------------------------------------------------------
 */

void
TkWmSetClass(
    TkWindow *winPtr)		/* Top-level window. */
{
    /* 
     * Note: Wayland/GLFW handles window class differently.
     * This is kept as a no-op for API compatibility.
     */
}


/*
 *----------------------------------------------------------------------
 *
 * TkWmCleanup --
 *
 *	Cleanup remaining wm resources associated with a display.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All WmInfo structure resources are freed.
 *
 *----------------------------------------------------------------------
 */

void
TkWmCleanup(
    TkDisplay *dispPtr)
{
    WmInfo *wmPtr, *nextPtr;

    for (wmPtr = firstWmPtr; wmPtr != NULL; wmPtr = nextPtr) {
        nextPtr = wmPtr->nextPtr;
        
        if (wmPtr->title != NULL) {
            ckfree(wmPtr->title);
        }
        if (wmPtr->iconName != NULL) {
            ckfree(wmPtr->iconName);
        }
        if (wmPtr->iconDataPtr != NULL) {
            ckfree((char *)wmPtr->iconDataPtr);
        }
        if (wmPtr->leaderName != NULL) {
            ckfree(wmPtr->leaderName);
        }
        if (wmPtr->menubar != NULL) {
            Tk_DestroyWindow(wmPtr->menubar);
        }
        if (wmPtr->wrapperPtr != NULL) {
            Tk_DestroyWindow((Tk_Window) wmPtr->wrapperPtr);
        }
        while (wmPtr->protPtr != NULL) {
            ProtocolHandler *protPtr = wmPtr->protPtr;
            wmPtr->protPtr = protPtr->nextPtr;
            Tcl_EventuallyFree((ClientData) protPtr, TCL_DYNAMIC);
        }
        if (wmPtr->cmdArgv != NULL) {
            ckfree((char *)wmPtr->cmdArgv);
        }
        if (wmPtr->clientMachine != NULL) {
            ckfree(wmPtr->clientMachine);
        }
        if (wmPtr->glfwIcon != NULL) {
            for (int i = 0; i < wmPtr->glfwIconCount; i++) {
                if (wmPtr->glfwIcon[i].pixels != NULL) {
                    ckfree((char *)wmPtr->glfwIcon[i].pixels);
                }
            }
            ckfree((char *)wmPtr->glfwIcon);
        }
        
        DestroyGlfwWindow(wmPtr);
        ckfree((char *)wmPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetGrid --
 *
 *	Set grid coordinate system for top-level window.
 *
 *----------------------------------------------------------------------
 */

void
Tk_SetGrid(
    Tk_Window tkwin,
    int reqWidth,
    int reqHeight,
    int widthInc,
    int heightInc)
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    WmInfo *wmPtr;

    if (widthInc <= 0) {
        widthInc = 1;
    }
    if (heightInc <= 0) {
        heightInc = 1;
    }

    while (!(winPtr->flags & TK_TOP_HIERARCHY)) {
        winPtr = winPtr->parentPtr;
        if (winPtr == NULL) {
            return;
        }
    }
    wmPtr = winPtr->wmInfoPtr;
    if (wmPtr == NULL) {
        return;
    }

    if ((wmPtr->gridWin != NULL) && (wmPtr->gridWin != tkwin)) {
        return;
    }

    if ((wmPtr->reqGridWidth == reqWidth)
            && (wmPtr->reqGridHeight == reqHeight)
            && (wmPtr->widthInc == widthInc)
            && (wmPtr->heightInc == heightInc)
            && ((wmPtr->sizeHintsFlags & PBaseSize) == PBaseSize)) {
        return;
    }

    if ((wmPtr->gridWin == NULL) && !(wmPtr->flags & WM_NEVER_MAPPED)) {
        wmPtr->width = -1;
        wmPtr->height = -1;
    }

    wmPtr->gridWin = tkwin;
    wmPtr->reqGridWidth = reqWidth;
    wmPtr->reqGridHeight = reqHeight;
    wmPtr->widthInc = widthInc;
    wmPtr->heightInc = heightInc;
    wmPtr->sizeHintsFlags |= PBaseSize;
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    
    if (!(wmPtr->flags & (WM_UPDATE_PENDING|WM_NEVER_MAPPED))) {
        Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData) winPtr);
        wmPtr->flags |= WM_UPDATE_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_UnsetGrid --
 *
 *	Cancel gridding for top-level window.
 *
 *----------------------------------------------------------------------
 */

void
Tk_UnsetGrid(
    Tk_Window tkwin)
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    WmInfo *wmPtr;

    while (!(winPtr->flags & TK_TOP_HIERARCHY)) {
        winPtr = winPtr->parentPtr;
        if (winPtr == NULL) {
            return;
        }
    }
    wmPtr = winPtr->wmInfoPtr;
    if (wmPtr == NULL) {
        return;
    }

    if (tkwin != wmPtr->gridWin) {
        return;
    }

    wmPtr->gridWin = NULL;
    wmPtr->sizeHintsFlags &= ~PBaseSize;
    if (wmPtr->width != -1) {
        wmPtr->width = winPtr->reqWidth + (wmPtr->width
                - wmPtr->reqGridWidth)*wmPtr->widthInc;
        wmPtr->height = winPtr->reqHeight + (wmPtr->height
                - wmPtr->reqGridHeight)*wmPtr->heightInc;
    }
    wmPtr->widthInc = 1;
    wmPtr->heightInc = 1;

    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    if (!(wmPtr->flags & (WM_UPDATE_PENDING|WM_NEVER_MAPPED))) {
        Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData) winPtr);
        wmPtr->flags |= WM_UPDATE_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetRootCoords --
 *
 *	Get root window coordinates of a point in tkwin.
 *
 *----------------------------------------------------------------------
 */

void
Tk_GetRootCoords(
    Tk_Window tkwin,
    int *xPtr,
    int *yPtr)
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    int x = 0, y = 0;

    while (1) {
        x += winPtr->changes.x + winPtr->changes.border_width;
        y += winPtr->changes.y + winPtr->changes.border_width;
        
        if ((winPtr->wmInfoPtr != NULL) &&
            (winPtr->wmInfoPtr->menubar == (Tk_Window) winPtr)) {
            y -= winPtr->wmInfoPtr->menuHeight;
            winPtr = winPtr->wmInfoPtr->winPtr;
            continue;
        }
        
        if (winPtr->flags & TK_TOP_LEVEL) {
            if (winPtr->flags & TK_EMBEDDED) {
                Tk_Window container = Tk_GetOtherWindow(tkwin);
                if (container == NULL) {
                    break;
                }
                winPtr = (TkWindow *) container;
                continue;
            }
            break;
        }
        
        winPtr = winPtr->parentPtr;
        if (winPtr == NULL) {
            break;
        }
    }
    
    *xPtr = x;
    *yPtr = y;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_CoordsToWindow --
 *
 *	Find top-most window covering a point.
 *
 *----------------------------------------------------------------------
 */

Tk_Window
Tk_CoordsToWindow(
    int rootX,
    int rootY,
    Tk_Window tkwin)
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    TkWindow *nextPtr, *childPtr;
    int x = rootX, y = rootY;

    /* Find topmost window at coordinates */
    while (winPtr != NULL) {
        nextPtr = NULL;
        
        for (childPtr = winPtr->childList; childPtr != NULL;
                childPtr = childPtr->nextPtr) {
            if (!Tk_IsMapped((Tk_Window) childPtr) ||
                    (childPtr->flags & TK_TOP_HIERARCHY)) {
                continue;
            }
            
            int tmpx = x - childPtr->changes.x;
            int tmpy = y - childPtr->changes.y;
            int bd = childPtr->changes.border_width;
            
            if ((tmpx >= -bd) && (tmpy >= -bd) &&
                    (tmpx < (childPtr->changes.width + bd)) &&
                    (tmpy < (childPtr->changes.height + bd))) {
                nextPtr = childPtr;
            }
        }
        
        if (nextPtr == NULL) {
            break;
        }
        
        x -= nextPtr->changes.x;
        y -= nextPtr->changes.y;
        winPtr = nextPtr;
    }
    
    return (Tk_Window) winPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetVRootGeometry --
 *
 *	Get virtual root geometry (screen dimensions in Wayland).
 *
 *----------------------------------------------------------------------
 */

void
Tk_GetVRootGeometry(
    Tk_Window tkwin,
    int *xPtr,
    int *yPtr,
    int *widthPtr,
    int *heightPtr)
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    WmInfo *wmPtr;

    while (!(winPtr->flags & TK_TOP_HIERARCHY) && (winPtr->parentPtr != NULL)) {
        winPtr = winPtr->parentPtr;
    }
    
    wmPtr = winPtr->wmInfoPtr;
    if (wmPtr == NULL) {
        *xPtr = 0;
        *yPtr = 0;
        *widthPtr = 1920;
        *heightPtr = 1080;
        return;
    }

    *xPtr = wmPtr->vRootX;
    *yPtr = wmPtr->vRootY;
    *widthPtr = wmPtr->vRootWidth;
    *heightPtr = wmPtr->vRootHeight;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MoveToplevelWindow --
 *
 *	Move a top-level window.
 *
 *----------------------------------------------------------------------
 */

void
Tk_MoveToplevelWindow(
    Tk_Window tkwin,
    int x,
    int y)
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    if (!(winPtr->flags & TK_TOP_LEVEL)) {
        Tcl_Panic("Tk_MoveToplevelWindow called with non-toplevel window");
    }
    
    wmPtr->x = x;
    wmPtr->y = y;
    wmPtr->flags |= WM_MOVE_PENDING;
    wmPtr->flags &= ~(WM_NEGATIVE_X|WM_NEGATIVE_Y);
    
    if (!(wmPtr->sizeHintsFlags & (USPosition|PPosition))) {
        wmPtr->sizeHintsFlags |= USPosition;
        wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    }

    if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
        if (wmPtr->flags & WM_UPDATE_PENDING) {
            Tcl_CancelIdleCall(UpdateGeometryInfo, (ClientData) winPtr);
        }
        UpdateGeometryInfo((ClientData) winPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmRestackToplevel --
 *
 *	Restack a top-level window (no-op in Wayland).
 *
 *----------------------------------------------------------------------
 */

void
TkWmRestackToplevel(
    TkWindow *winPtr,
    int aboveBelow,
    TkWindow *otherPtr)
{
    /* Window stacking is controlled by compositor in Wayland */
    /* This is a no-op for compatibility */
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmProtocolEventProc --
 *
 *	Handle WM protocol events.
 *
 *----------------------------------------------------------------------
 */

void
TkWmProtocolEventProc(
    TkWindow *winPtr,
    XEvent *eventPtr)
{
    /* In GLFW/Wayland, protocols are handled via callbacks */
    /* This is kept as a no-op for API compatibility */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMakeMenuWindow --
 *
 *	Configure window as a menu (no-op in Wayland).
 *
 *----------------------------------------------------------------------
 */

void
TkpMakeMenuWindow(
    Tk_Window tkwin,
    int transient)
{
    /* Menu windows handled differently in Wayland */
    /* No special configuration needed */
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmFocusToplevel --
 *
 *	Return toplevel for focus events.
 *
 *----------------------------------------------------------------------
 */

TkWindow *
TkWmFocusToplevel(
    TkWindow *winPtr)
{
    if (!(winPtr->flags & TK_WRAPPER)) {
        return NULL;
    }
    return winPtr->wmInfoPtr->winPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetPointerCoords --
 *
 *	Get mouse pointer coordinates (no-op in GLFW/Wayland).
 *
 *----------------------------------------------------------------------
 */

void
TkGetPointerCoords(
    Tk_Window tkwin,
    int *xPtr,
    int *yPtr)
{
    /* GLFW doesn't provide global cursor position */
    /* Return -1 to indicate unavailable */
    *xPtr = -1;
    *yPtr = -1;
}



/*
 *----------------------------------------------------------------------
 *
 * Tk_WmObjCmd --
 *
 *	This function is invoked to process the "wm" Tcl command.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

int
Tk_WmObjCmd(
    ClientData clientData,	/* Main window. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Tk_Window tkwin = (Tk_Window)clientData;
    static const char *const optionStrings[] = {
	"aspect", "attributes", "client", "colormapwindows",
	"command", "deiconify", "focusmodel", "forget",
	"frame", "geometry", "grid", "group", "iconbadge",
	"iconbitmap", "iconify", "iconmask", "iconname",
	"iconphoto", "iconposition", "iconwindow",
	"manage", "maxsize", "minsize", "overrideredirect",
	"positionfrom", "protocol", "resizable", "sizefrom",
	"stackorder", "state", "title", "transient",
	"withdraw", NULL
    };
    enum options {
	WMOPT_ASPECT, WMOPT_ATTRIBUTES, WMOPT_CLIENT, WMOPT_COLORMAPWINDOWS,
	WMOPT_COMMAND, WMOPT_DEICONIFY, WMOPT_FOCUSMODEL, WMOPT_FORGET,
	WMOPT_FRAME, WMOPT_GEOMETRY, WMOPT_GRID, WMOPT_GROUP, WMOPT_ICONBADGE, 
	WMOPT_ICONBITMAP, WMOPT_ICONIFY, WMOPT_ICONMASK, WMOPT_ICONNAME,
	WMOPT_ICONPHOTO, WMOPT_ICONPOSITION, WMOPT_ICONWINDOW,
	WMOPT_MANAGE, WMOPT_MAXSIZE, WMOPT_MINSIZE, WMOPT_OVERRIDEREDIRECT,
	WMOPT_POSITIONFROM, WMOPT_PROTOCOL, WMOPT_RESIZABLE, WMOPT_SIZEFROM,
	WMOPT_STACKORDER, WMOPT_STATE, WMOPT_TITLE, WMOPT_TRANSIENT,
	WMOPT_WITHDRAW
    };
    int index;
    Tcl_Size length;
    char *argv1;
    TkWindow *winPtr;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option window ?arg ...?");
	return TCL_ERROR;
    }

    argv1 = Tcl_GetStringFromObj(objv[1], &length);
    if ((argv1[0] == '.') && (length > 1)) {
	winPtr = (TkWindow *) Tk_NameToWindow(interp, argv1, tkwin);
	if (winPtr == NULL) {
	    return TCL_ERROR;
	}
	if (!(winPtr->flags & TK_TOP_LEVEL)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "window \"%s\" isn't a top-level window", argv1));
	    Tcl_SetErrorCode(interp, "TK", "LOOKUP", "TOPLEVEL", argv1, NULL);
	    return TCL_ERROR;
	}
	if (objc == 2) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option ?arg ...?");
	    return TCL_ERROR;
	}
	if (Tcl_GetIndexFromObjStruct(interp, objv[2], optionStrings,
		sizeof(char *), "option", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	objc -= 3;
	objv += 3;
    } else {
	if (Tcl_GetIndexFromObjStruct(interp, objv[1], optionStrings,
		sizeof(char *), "option", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "window ?arg ...?");
	    return TCL_ERROR;
	}
	winPtr = (TkWindow *) Tk_NameToWindow(interp, Tcl_GetString(objv[2]), tkwin);
	if (winPtr == NULL) {
	    return TCL_ERROR;
	}
	if (!(winPtr->flags & TK_TOP_LEVEL)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "window \"%s\" isn't a top-level window", Tcl_GetString(objv[2])));
	    Tcl_SetErrorCode(interp, "TK", "LOOKUP", "TOPLEVEL", Tcl_GetString(objv[2]), NULL);
	    return TCL_ERROR;
	}
	objc -= 3;
	objv += 3;
    }

    switch ((enum options) index) {
    case WMOPT_ASPECT:
	return WmAspectCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ATTRIBUTES:
	return WmAttributesCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_CLIENT:
	return WmClientCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_COLORMAPWINDOWS:
	return WmColormapwindowsCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_COMMAND:
	return WmCommandCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_DEICONIFY:
	return WmDeiconifyCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_FOCUSMODEL:
	return WmFocusmodelCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_FORGET:
	return WmForgetCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_FRAME:
	return WmFrameCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_GEOMETRY:
	return WmGeometryCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_GRID:
	return WmGridCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_GROUP:
	return WmGroupCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONBADGE:
	return WmIconbadgeCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONBITMAP:
	return WmIconbitmapCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONIFY:
	return WmIconifyCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONMASK:
	return WmIconmaskCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONNAME:
	return WmIconnameCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONPHOTO:
	return WmIconphotoCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONPOSITION:
	return WmIconpositionCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONWINDOW:
	return WmIconwindowCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_MANAGE:
	return WmManageCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_MAXSIZE:
	return WmMaxsizeCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_MINSIZE:
	return WmMinsizeCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_OVERRIDEREDIRECT:
	return WmOverrideredirectCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_POSITIONFROM:
	return WmPositionfromCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_PROTOCOL:
	return WmProtocolCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_RESIZABLE:
	return WmResizableCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_SIZEFROM:
	return WmSizefromCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_STACKORDER:
	return WmStackorderCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_STATE:
	return WmStateCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_TITLE:
	return WmTitleCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_TRANSIENT:
	return WmTransientCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_WITHDRAW:
	return WmWithdrawCmd(tkwin, winPtr, interp, objc, objv);
    }

    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * WmAspectCmd --
 *
 *	Processes the "wm aspect" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmAspectCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int numer1, numer2, denom1, denom2;

    if ((objc != 0) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 0, objv,
		"pathName aspect ?minNumer minDenom maxNumer maxDenom?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->sizeHintsFlags & PAspect) {
	    Tcl_Obj *results[4];
	    results[0] = Tcl_NewIntObj(wmPtr->minAspect.x);
	    results[1] = Tcl_NewIntObj(wmPtr->minAspect.y);
	    results[2] = Tcl_NewIntObj(wmPtr->maxAspect.x);
	    results[3] = Tcl_NewIntObj(wmPtr->maxAspect.y);
	    Tcl_SetObjResult(interp, Tcl_NewListObj(4, results));
	}
	return TCL_OK;
    }
    if (*Tcl_GetString(objv[0]) == '\0') {
	wmPtr->sizeHintsFlags &= ~PAspect;
    } else {
	if ((Tcl_GetIntFromObj(interp, objv[0], &numer1) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[1], &denom1) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[2], &numer2) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[3], &denom2) != TCL_OK)) {
	    return TCL_ERROR;
	}
	if ((numer1 <= 0) || (denom1 <= 0) || (numer2 <= 0) || (denom2 <= 0)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "aspect ratio values must be positive integers"));
	    Tcl_SetErrorCode(interp, "TK", "WM", "ASPECT", "POSITIVE", NULL);
	    return TCL_ERROR;
	}
	wmPtr->minAspect.x = numer1;
	wmPtr->minAspect.y = denom1;
	wmPtr->maxAspect.x = numer2;
	wmPtr->maxAspect.y = denom2;
	wmPtr->sizeHintsFlags |= PAspect;
    }
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmAttributesCmd --
 *
 *	Processes the "wm attributes" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmAttributesCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int attribute = 0;

    if (objc == 0) {
	Tcl_Obj *result = Tcl_NewObj();
	Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj("-alpha", -1));
	Tcl_ListObjAppendElement(NULL, result, Tcl_NewDoubleObj(wmPtr->attributes.alpha));
	Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj("-topmost", -1));
	Tcl_ListObjAppendElement(NULL, result, Tcl_NewIntObj(wmPtr->attributes.topmost));
	Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj("-zoomed", -1));
	Tcl_ListObjAppendElement(NULL, result, Tcl_NewIntObj(wmPtr->attributes.zoomed));
	Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj("-fullscreen", -1));
	Tcl_ListObjAppendElement(NULL, result, Tcl_NewIntObj(wmPtr->attributes.fullscreen));
	Tcl_SetObjResult(interp, result);
	return TCL_OK;
    }

    if (objc == 1) {
	if (Tcl_GetIndexFromObjStruct(interp, objv[0], WmAttributeNames,
		sizeof(char *), "attribute", 0, &attribute) != TCL_OK) {
	    return TCL_ERROR;
	}
	switch ((enum WmAttribute) attribute) {
	case WMATT_ALPHA:
	    Tcl_SetObjResult(interp, Tcl_NewDoubleObj(wmPtr->attributes.alpha));
	    break;
	case WMATT_TOPMOST:
	    Tcl_SetObjResult(interp, Tcl_NewIntObj(wmPtr->attributes.topmost));
	    break;
	case WMATT_ZOOMED:
	    Tcl_SetObjResult(interp, Tcl_NewIntObj(wmPtr->attributes.zoomed));
	    break;
	case WMATT_FULLSCREEN:
	    Tcl_SetObjResult(interp, Tcl_NewIntObj(wmPtr->attributes.fullscreen));
	    break;
	case WMATT_TYPE:
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("", -1));
	    break;
	default:
	    return TCL_ERROR;
	}
	return TCL_OK;
    }

    if (objc % 2 == 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName attributes ?-attribute value ...?");
	return TCL_ERROR;
    }

    for (int i = 0; i < objc; i += 2) {
	if (Tcl_GetIndexFromObjStruct(interp, objv[i], WmAttributeNames,
		sizeof(char *), "attribute", 0, &attribute) != TCL_OK) {
	    return TCL_ERROR;
	}

	switch ((enum WmAttribute) attribute) {
	case WMATT_ALPHA: {
	    double dval;
	    if (Tcl_GetDoubleFromObj(interp, objv[i+1], &dval) != TCL_OK) {
		return TCL_ERROR;
	    }
	    wmPtr->reqState.alpha = (dval < 0.0) ? 0.0 : (dval > 1.0) ? 1.0 : dval;
	    if (wmPtr->glfwWindow != NULL) {
		glfwSetWindowOpacity(wmPtr->glfwWindow, (float)wmPtr->reqState.alpha);
	    }
	    wmPtr->attributes.alpha = wmPtr->reqState.alpha;
	    break;
	}
	case WMATT_TOPMOST: {
	    int bval;
	    if (Tcl_GetBooleanFromObj(interp, objv[i+1], &bval) != TCL_OK) {
		return TCL_ERROR;
	    }
	    wmPtr->reqState.topmost = bval;
	    if (wmPtr->glfwWindow != NULL) {
		glfwSetWindowAttrib(wmPtr->glfwWindow, GLFW_FLOATING,
			bval ? GLFW_TRUE : GLFW_FALSE);
	    }
	    wmPtr->attributes.topmost = bval;
	    break;
	}
	case WMATT_ZOOMED: {
	    int bval;
	    if (Tcl_GetBooleanFromObj(interp, objv[i+1], &bval) != TCL_OK) {
		return TCL_ERROR;
	    }
	    wmPtr->reqState.zoomed = bval;
	    if (wmPtr->glfwWindow != NULL) {
		if (bval) {
		    glfwMaximizeWindow(wmPtr->glfwWindow);
		} else {
		    glfwRestoreWindow(wmPtr->glfwWindow);
		}
	    }
	    wmPtr->attributes.zoomed = bval;
	    break;
	}
	case WMATT_FULLSCREEN: {
	    int bval;
	    if (Tcl_GetBooleanFromObj(interp, objv[i+1], &bval) != TCL_OK) {
		return TCL_ERROR;
	    }
	    wmPtr->reqState.fullscreen = bval;
	    wmPtr->attributes.fullscreen = bval;
	    ApplyFullscreenState(winPtr);
	    break;
	}
	case WMATT_TYPE:
	    break;
	default:
	    return TCL_ERROR;
	}
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmClientCmd --
 *
 *	Processes the "wm client" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmClientCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    const char *name;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName client ?name?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->clientMachine != NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(wmPtr->clientMachine, -1));
	}
	return TCL_OK;
    }
    name = Tcl_GetString(objv[0]);
    if (wmPtr->clientMachine != NULL) {
	ckfree(wmPtr->clientMachine);
    }
    wmPtr->clientMachine = (char *)ckalloc(strlen(name) + 1);
    strcpy(wmPtr->clientMachine, name);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmColormapwindowsCmd --
 *
 *	Processes the "wm colormapwindows" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmColormapwindowsCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    /* Colormaps are an X11 concept - no-op in Wayland */
    Tcl_SetObjResult(interp, Tcl_NewObj());
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmCommandCmd --
 *
 *	Processes the "wm command" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmCommandCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    Tcl_Obj *listObj;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName command ?value?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->cmdArgc > 0) {
	    listObj = Tcl_NewObj();
	    Tcl_SetObjLength(listObj, wmPtr->cmdArgc);
	    for (Tcl_Size i = 0; i < wmPtr->cmdArgc; i++) {
		Tcl_ListObjAppendElement(NULL, listObj, Tcl_NewStringObj(wmPtr->cmdArgv[i], -1));
	    }
	    Tcl_SetObjResult(interp, listObj);
	}
	return TCL_OK;
    }
    /* Set command */
    if (wmPtr->cmdArgv != NULL) {
	ckfree((char *) wmPtr->cmdArgv);
    }
    if (Tcl_ListObjGetElements(interp, objv[0], &wmPtr->cmdArgc, (Tcl_Obj ***) &wmPtr->cmdArgv) != TCL_OK) {
	return TCL_ERROR;
    }
    wmPtr->cmdArgv = (const char **)ckalloc(wmPtr->cmdArgc * sizeof(const char *));
    for (Tcl_Size i = 0; i < wmPtr->cmdArgc; i++) {
	wmPtr->cmdArgv[i] = Tcl_GetString(wmPtr->cmdArgv[i]);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmDeiconifyCmd --
 *
 *	Processes the "wm deiconify" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmDeiconifyCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    if (objc != 0) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName deiconify");
	return TCL_ERROR;
    }
    TkpWmSetState(winPtr, NormalState);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmFocusmodelCmd --
 *
 *	Processes the "wm focusmodel" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmFocusmodelCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    /* Focus models are X11-specific - return "passive" for compatibility */
    if (objc == 0) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("passive", -1));
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmForgetCmd --
 *
 *	Processes the "wm forget" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmForgetCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    if (objc != 0) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName forget");
	return TCL_ERROR;
    }
    /* No-op in Wayland, as there's no equivalent */
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmFrameCmd --
 *
 *	Processes the "wm frame" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmFrameCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    if (objc != 0) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName frame");
	return TCL_ERROR;
    }
    /* Return dummy frame ID for compatibility */
    Tcl_SetObjResult(interp, Tcl_NewStringObj("0x0", -1));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmGeometryCmd --
 *
 *	Processes the "wm geometry" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmGeometryCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    char buf[100];

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName geometry ?newGeometry?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	snprintf(buf, sizeof(buf), "%dx%d+%d+%d",
		(wmPtr->width >= 0) ? wmPtr->width : winPtr->reqWidth,
		(wmPtr->height >= 0) ? wmPtr->height : winPtr->reqHeight,
		wmPtr->x, wmPtr->y);
	Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, -1));
	return TCL_OK;
    }
    if (*Tcl_GetString(objv[0]) == '\0') {
	wmPtr->width = -1;
	wmPtr->height = -1;
	WmUpdateGeom(wmPtr, winPtr);
	return TCL_OK;
    }
    return ParseGeometry(interp, Tcl_GetString(objv[0]), winPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * WmGridCmd --
 *
 *	Processes the "wm grid" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmGridCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int reqGridWidth, reqGridHeight, widthInc, heightInc;

    if ((objc != 0) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 0, objv,
		"pathName grid ?baseWidth baseHeight widthInc heightInc?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->sizeHintsFlags & PBaseSize) {
	    Tcl_Obj *results[4];
	    results[0] = Tcl_NewIntObj(wmPtr->reqGridWidth);
	    results[1] = Tcl_NewIntObj(wmPtr->reqGridHeight);
	    results[2] = Tcl_NewIntObj(wmPtr->widthInc);
	    results[3] = Tcl_NewIntObj(wmPtr->heightInc);
	    Tcl_SetObjResult(interp, Tcl_NewListObj(4, results));
	}
	return TCL_OK;
    }
    if (*Tcl_GetString(objv[0]) == '\0') {
	wmPtr->sizeHintsFlags &= ~(PBaseSize|PResizeInc);
	wmPtr->widthInc = 1;
	wmPtr->heightInc = 1;
	wmPtr->reqGridWidth = wmPtr->reqGridHeight = 0;
    } else {
	if ((Tcl_GetIntFromObj(interp, objv[0], &reqGridWidth) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[1], &reqGridHeight) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[2], &widthInc) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[3], &heightInc) != TCL_OK)) {
	    return TCL_ERROR;
	}
	if (reqGridWidth < 0) {
	    reqGridWidth = winPtr->reqWidth + (winPtr->internalBorderLeft * 2);
	}
	if (reqGridHeight < 0) {
	    reqGridHeight = winPtr->reqHeight + (winPtr->internalBorderTop * 2);
	}
	if ((widthInc <= 0) || (heightInc <= 0)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "grid increments must be positive integers"));
	    Tcl_SetErrorCode(interp, "TK", "WM", "GRID", "POSITIVE", NULL);
	    return TCL_ERROR;
	}
	wmPtr->sizeHintsFlags |= PBaseSize|PResizeInc;
	wmPtr->reqGridWidth = reqGridWidth;
	wmPtr->reqGridHeight = reqGridHeight;
	wmPtr->widthInc = widthInc;
	wmPtr->heightInc = heightInc;
    }
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmGroupCmd --
 *
 *	Processes the "wm group" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmGroupCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    const char *path;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName group ?pathName?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->leaderName != NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(wmPtr->leaderName, -1));
	}
	return TCL_OK;
    }
    path = Tcl_GetString(objv[0]);
    if (wmPtr->leaderName != NULL) {
	ckfree(wmPtr->leaderName);
    }
    wmPtr->leaderName = (char *)ckalloc(strlen(path) + 1);
    strcpy(wmPtr->leaderName, path);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconbadgeCmd --
 *
 *	This function is invoked to process the "wm iconbadge" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconbadgeCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *tkWin,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    (void) tkWin;
    char cmd[4096];

    if (objc < 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "window badge");
	return TCL_ERROR;
    }

    /* No-op on Wayland. *

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconbitmapCmd --
 *
 *	Processes the "wm iconbitmap" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconbitmapCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    /* Icon bitmaps are X11-specific - no-op in Wayland */
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconifyCmd --
 *
 *	Processes the "wm iconify" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconifyCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    if (objc != 0) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName iconify");
	return TCL_ERROR;
    }
    TkpWmSetState(winPtr, IconicState);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconmaskCmd --
 *
 *	Processes the "wm iconmask" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconmaskCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    /* Icon masks are X11-specific - no-op in Wayland */
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconnameCmd --
 *
 *	Processes the "wm iconname" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconnameCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    const char *argv3;
    Tcl_Size length;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName iconname ?newName?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->iconName != NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(wmPtr->iconName, -1));
	}
	return TCL_OK;
    }
    argv3 = Tcl_GetStringFromObj(objv[0], &length);
    if (wmPtr->iconName != NULL) {
	ckfree(wmPtr->iconName);
    }
    wmPtr->iconName = (char *)ckalloc(length + 1);
    strcpy(wmPtr->iconName, argv3);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconphotoCmd --
 *
 *	Processes the "wm iconphoto" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconphotoCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    Tk_PhotoHandle photo;
    int i, isDefault = 0;

    if (objc < 1) {
	Tcl_WrongNumArgs(interp, 0, objv,
		"pathName iconphoto ?-default? image ?image ...?");
	return TCL_ERROR;
    }

    if (strcmp(Tcl_GetString(objv[0]), "-default") == 0) {
	isDefault = 1;
	objv++;
	objc--;
    }

    if (objc < 1) {
	Tcl_WrongNumArgs(interp, 0, objv,
		"pathName iconphoto ?-default? image ?image ...?");
	return TCL_ERROR;
    }

    /* Clear existing icons */
    if (wmPtr->glfwIcon != NULL) {
	for (i = 0; i < wmPtr->glfwIconCount; i++) {
	    if (wmPtr->glfwIcon[i].pixels != NULL) {
		ckfree((char *)wmPtr->glfwIcon[i].pixels);
	    }
	}
	ckfree((char *)wmPtr->glfwIcon);
	wmPtr->glfwIcon = NULL;
	wmPtr->glfwIconCount = 0;
    }

    /* Convert each photo to GLFW icon */
    for (i = 0; i < objc; i++) {
	photo = Tk_FindPhoto(interp, Tcl_GetString(objv[i]));
	if (photo == NULL) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "can't use \"%s\" as iconphoto: not a photo image",
		    Tcl_GetString(objv[i])));
	    Tcl_SetErrorCode(interp, "TK", "WM", "ICONPHOTO", "PHOTO", NULL);
	    return TCL_ERROR;
	}
	
	ConvertPhotoToGlfwIcon(winPtr, photo, i);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconpositionCmd --
 *
 *	Processes the "wm iconposition" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconpositionCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    /* Icon positions are X11-specific - no-op in Wayland */
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconwindowCmd --
 *
 *	Processes the "wm iconwindow" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconwindowCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    /* Icon windows are X11-specific - no-op in Wayland */
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmManageCmd --
 *
 *	Processes the "wm manage" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmManageCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    if (objc != 0) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName manage");
	return TCL_ERROR;
    }
    /* No-op in Wayland, as all windows are managed */
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmMaxsizeCmd --
 *
 *	Processes the "wm maxsize" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmMaxsizeCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int width, height;

    if ((objc != 0) && (objc != 2)) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName maxsize ?width height?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	Tcl_Obj *results[2];
	results[0] = Tcl_NewIntObj(wmPtr->maxWidth);
	results[1] = Tcl_NewIntObj(wmPtr->maxHeight);
	Tcl_SetObjResult(interp, Tcl_NewListObj(2, results));
	return TCL_OK;
    }
    if ((Tcl_GetIntFromObj(interp, objv[0], &width) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[1], &height) != TCL_OK)) {
	return TCL_ERROR;
    }
    wmPtr->maxWidth = width;
    wmPtr->maxHeight = height;
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmMinsizeCmd --
 *
 *	Processes the "wm minsize" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmMinsizeCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int width, height;

    if ((objc != 0) && (objc != 2)) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName minsize ?width height?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	Tcl_Obj *results[2];
	results[0] = Tcl_NewIntObj(wmPtr->minWidth);
	results[1] = Tcl_NewIntObj(wmPtr->minHeight);
	Tcl_SetObjResult(interp, Tcl_NewListObj(2, results));
	return TCL_OK;
    }
    if ((Tcl_GetIntFromObj(interp, objv[0], &width) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[1], &height) != TCL_OK)) {
	return TCL_ERROR;
    }
    wmPtr->minWidth = width;
    wmPtr->minHeight = height;
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmOverrideredirectCmd --
 *
 *	Processes the "wm overrideredirect" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmOverrideredirectCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int value;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName overrideredirect ?boolean?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(
		Tk_Attributes(tkwin)->override_redirect));
	return TCL_OK;
    }
    if (Tcl_GetBooleanFromObj(interp, objv[0], &value) != TCL_OK) {
	return TCL_ERROR;
    }
    if (value) {
	wmPtr->flags |= WM_WIDTH_NOT_RESIZABLE | WM_HEIGHT_NOT_RESIZABLE;
    } else {
	wmPtr->flags &= ~(WM_WIDTH_NOT_RESIZABLE | WM_HEIGHT_NOT_RESIZABLE);
    }
    /* Update GLFW decorated hint */
    if (wmPtr->glfwWindow != NULL) {
	glfwSetWindowAttrib(wmPtr->glfwWindow, GLFW_DECORATED, value ? GLFW_FALSE : GLFW_TRUE);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmPositionfromCmd --
 *
 *	Processes the "wm positionfrom" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmPositionfromCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    static const char *const sourceStrings[] = {
	"program", "user", NULL
    };

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName positionfrom ?user|program?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->sizeHintsFlags & USPosition) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("user", -1));
	} else if (wmPtr->sizeHintsFlags & PPosition) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("program", -1));
	}
	return TCL_OK;
    }
    if (*Tcl_GetString(objv[0]) == '\0') {
	wmPtr->sizeHintsFlags &= ~(USPosition|PPosition);
    } else {
	int index;
	if (Tcl_GetIndexFromObjStruct(interp, objv[0], sourceStrings,
		sizeof(char *), "argument", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (index == 0) {
	    wmPtr->sizeHintsFlags &= ~USPosition;
	    wmPtr->sizeHintsFlags |= PPosition;
	} else {
	    wmPtr->sizeHintsFlags &= ~PPosition;
	    wmPtr->sizeHintsFlags |= USPosition;
	}
    }
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmProtocolCmd --
 *
 *	Processes the "wm protocol" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmProtocolCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    ProtocolHandler *protPtr, *prevPtr;
    Tcl_Obj *resultObj;
    const char *cmd;
    Tcl_Size cmdLength;
    int protocol;

    if (objc == 0) {
	/* List all protocols */
	resultObj = Tcl_NewObj();
	for (protPtr = wmPtr->protPtr; protPtr != NULL; protPtr = protPtr->nextPtr) {
	    const char *name = NULL;
	    if (protPtr->protocol == WM_DELETE_WINDOW) {
		name = "WM_DELETE_WINDOW";
	    } else if (protPtr->protocol == WM_TAKE_FOCUS) {
		name = "WM_TAKE_FOCUS";
	    } else if (protPtr->protocol == WM_SAVE_YOURSELF) {
		name = "WM_SAVE_YOURSELF";
	    }
	    if (name != NULL) {
		Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(name, -1));
	    }
	}
	Tcl_SetObjResult(interp, resultObj);
	return TCL_OK;
    }

    /* Map protocol name to identifier */
    cmd = Tcl_GetString(objv[0]);
    if (strcmp(cmd, "WM_DELETE_WINDOW") == 0) {
	protocol = WM_DELETE_WINDOW;
    } else if (strcmp(cmd, "WM_TAKE_FOCUS") == 0) {
	protocol = WM_TAKE_FOCUS;
    } else if (strcmp(cmd, "WM_SAVE_YOURSELF") == 0) {
	protocol = WM_SAVE_YOURSELF;
    } else {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"unknown protocol \"%s\"", cmd));
	Tcl_SetErrorCode(interp, "TK", "WM", "PROTOCOL", "UNKNOWN", NULL);
	return TCL_ERROR;
    }

    if (objc == 1) {
	/* Return command for this protocol */
	for (protPtr = wmPtr->protPtr; protPtr != NULL; protPtr = protPtr->nextPtr) {
	    if (protPtr->protocol == protocol) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(protPtr->command, -1));
		return TCL_OK;
	    }
	}
	return TCL_OK;
    }

    /* Set or delete protocol handler */
    cmd = Tcl_GetStringFromObj(objv[1], &cmdLength);
    if (cmdLength == 0) {
	/* Delete protocol handler */
	for (protPtr = wmPtr->protPtr, prevPtr = NULL; protPtr != NULL;
		prevPtr = protPtr, protPtr = protPtr->nextPtr) {
	    if (protPtr->protocol == protocol) {
		if (prevPtr == NULL) {
		    wmPtr->protPtr = protPtr->nextPtr;
		} else {
		    prevPtr->nextPtr = protPtr->nextPtr;
		}
		Tcl_EventuallyFree((ClientData) protPtr, TCL_DYNAMIC);
		break;
	    }
	}
    } else {
	/* Add or replace protocol handler */
	for (protPtr = wmPtr->protPtr, prevPtr = NULL; protPtr != NULL;
		prevPtr = protPtr, protPtr = protPtr->nextPtr) {
	    if (protPtr->protocol == protocol) {
		break;
	    }
	}
	if (protPtr == NULL) {
	    protPtr = (ProtocolHandler *)ckalloc(HANDLER_SIZE(cmdLength));
	    protPtr->protocol = protocol;
	    protPtr->nextPtr = wmPtr->protPtr;
	    wmPtr->protPtr = protPtr;
	    protPtr->interp = interp;
	} else {
	    protPtr = (ProtocolHandler *)ckrealloc((char *)protPtr,
		    HANDLER_SIZE(cmdLength));
	    if (prevPtr == NULL) {
		wmPtr->protPtr = protPtr;
	    } else {
		prevPtr->nextPtr = protPtr;
	    }
	}
	strcpy(protPtr->command, cmd);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmResizableCmd --
 *
 *	Processes the "wm resizable" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmResizableCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int width, height;

    if ((objc != 0) && (objc != 2)) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName resizable ?width height?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	Tcl_Obj *results[2];
	results[0] = Tcl_NewBooleanObj(!(wmPtr->flags & WM_WIDTH_NOT_RESIZABLE));
	results[1] = Tcl_NewBooleanObj(!(wmPtr->flags & WM_HEIGHT_NOT_RESIZABLE));
	Tcl_SetObjResult(interp, Tcl_NewListObj(2, results));
	return TCL_OK;
    }
    if ((Tcl_GetBooleanFromObj(interp, objv[0], &width) != TCL_OK)
	    || (Tcl_GetBooleanFromObj(interp, objv[1], &height) != TCL_OK)) {
	return TCL_ERROR;
    }
    if (width) {
	wmPtr->flags &= ~WM_WIDTH_NOT_RESIZABLE;
    } else {
	wmPtr->flags |= WM_WIDTH_NOT_RESIZABLE;
    }
    if (height) {
	wmPtr->flags &= ~WM_HEIGHT_NOT_RESIZABLE;
    } else {
	wmPtr->flags |= WM_HEIGHT_NOT_RESIZABLE;
    }
    
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    
    /* Update GLFW window resizability */
    if (wmPtr->glfwWindow != NULL) {
	glfwSetWindowAttrib(wmPtr->glfwWindow, GLFW_RESIZABLE, 
		(width || height) ? GLFW_TRUE : GLFW_FALSE);
    }
    
    WmUpdateGeom(wmPtr, winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmSizefromCmd --
 *
 *	Processes the "wm sizefrom" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmSizefromCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    static const char *const sourceStrings[] = {
	"program", "user", NULL
    };

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName sizefrom ?user|program?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->sizeHintsFlags & USSize) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("user", -1));
	} else if (wmPtr->sizeHintsFlags & PSize) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("program", -1));
	}
	return TCL_OK;
    }
    if (*Tcl_GetString(objv[0]) == '\0') {
	wmPtr->sizeHintsFlags &= ~(USSize|PSize);
    } else {
	int index;
	if (Tcl_GetIndexFromObjStruct(interp, objv[0], sourceStrings,
		sizeof(char *), "argument", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (index == 0) {
	    wmPtr->sizeHintsFlags &= ~USSize;
	    wmPtr->sizeHintsFlags |= PSize;
	} else {
	    wmPtr->sizeHintsFlags &= ~PSize;
	    wmPtr->sizeHintsFlags |= USSize;
	}
    }
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmStackorderCmd --
 *
 *	Processes the "wm stackorder" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmStackorderCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    TkWindow **windows, **windowPtr;
    static const char *const optionStrings[] = {
	"isabove", "isbelow", NULL
    };
    enum options {
	OPT_ISABOVE, OPT_ISBELOW
    };
    Tcl_Obj *resultObj;
    int index;

    if ((objc != 0) && (objc != 2)) {
	Tcl_WrongNumArgs(interp, 0, objv,
		"pathName stackorder ?isabove|isbelow window?");
	return TCL_ERROR;
    }

    if (objc == 0) {
	windows = TkWmStackorderToplevel(winPtr);
	if (windows != NULL) {
	    resultObj = Tcl_NewObj();
	    for (windowPtr = windows; *windowPtr ; windowPtr++) {
		Tcl_ListObjAppendElement(NULL, resultObj,
			Tcl_NewStringObj((*windowPtr)->pathName, -1));
	    }
	    ckfree((char *) windows);
	    Tcl_SetObjResult(interp, resultObj);
	}
	return TCL_OK;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[0], optionStrings,
	    sizeof(char *), "argument", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }
    
    /* Note: Proper implementation would check actual stacking order */
    /* This is a simplified placeholder */
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmStateCmd --
 *
 *	Processes the "wm state" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmStateCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    static const char *const optionStrings[] = {
	"normal", "iconic", "withdrawn", "icon", "zoomed", NULL
    };
    enum options {
	OPT_NORMAL, OPT_ICONIC, OPT_WITHDRAWN, OPT_ICON, OPT_ZOOMED
    };
    int index;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName state ?state?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->iconFor != NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("icon", -1));
	} else if (wmPtr->withdrawn) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("withdrawn", -1));
	} else if (wmPtr->attributes.zoomed) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("zoomed", -1));
	} else if (Tk_IsMapped((Tk_Window) winPtr)) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("normal", -1));
	} else {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("iconic", -1));
	}
	return TCL_OK;
    }
    if (Tcl_GetIndexFromObjStruct(interp, objv[0], optionStrings,
	    sizeof(char *), "argument", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }

    if (wmPtr->iconFor != NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't change state of %s: it is an icon for %s",
		Tk_PathName(winPtr), Tk_PathName(wmPtr->iconFor)));
	Tcl_SetErrorCode(interp, "TK", "WM", "STATE", "ICON", NULL);
	return TCL_ERROR;
    }

    if (index == OPT_NORMAL) {
	TkpWmSetState(winPtr, NormalState);
    } else if (index == OPT_ICONIC) {
	TkpWmSetState(winPtr, IconicState);
    } else if (index == OPT_WITHDRAWN) {
	TkpWmSetState(winPtr, WithdrawnState);
    } else if (index == OPT_ICON) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't change state to icon: not implemented"));
	Tcl_SetErrorCode(interp, "TK", "WM", "STATE", "ICON", NULL);
	return TCL_ERROR;
    } else {  /* OPT_ZOOMED */
	wmPtr->attributes.zoomed = 1;
	if (wmPtr->glfwWindow != NULL) {
	    glfwMaximizeWindow(wmPtr->glfwWindow);
	}
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmTitleCmd --
 *
 *	Processes the "wm title" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmTitleCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    const char *argv3;
    Tcl_Size length;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName title ?newTitle?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		(wmPtr->title != NULL) ? wmPtr->title : winPtr->nameUid, -1));
	return TCL_OK;
    }
    argv3 = Tcl_GetStringFromObj(objv[0], &length);
    if (wmPtr->title != NULL) {
	ckfree(wmPtr->title);
    }
    wmPtr->title = (char *)ckalloc(length + 1);
    strcpy(wmPtr->title, argv3);

    if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
	UpdateTitle(winPtr);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmTransientCmd --
 *
 *	Processes the "wm transient" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmTransientCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    Tk_Window container;
    WmInfo *wmPtr2;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName transient ?window?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->containerPtr != NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    Tk_PathName(wmPtr->containerPtr), -1));
	}
	return TCL_OK;
    }
    if (Tcl_GetString(objv[0])[0] == '\0') {
	if (wmPtr->containerPtr != NULL) {
	    wmPtr2 = wmPtr->containerPtr->wmInfoPtr;
	    if (wmPtr2 != NULL) {
		wmPtr2->numTransients--;
	    }
	    Tk_DeleteEventHandler((Tk_Window) wmPtr->containerPtr,
		    StructureNotifyMask, WmWaitMapProc, (ClientData) winPtr);
	}
	wmPtr->containerPtr = NULL;
    } else {
	container = Tk_NameToWindow(interp, Tcl_GetString(objv[0]), tkwin);
	if (container == NULL) {
	    return TCL_ERROR;
	}
	while (!Tk_IsTopLevel(container)) {
	    container = Tk_Parent(container);
	}
	wmPtr2 = ((TkWindow *) container)->wmInfoPtr;
	if (wmPtr->containerPtr != NULL) {
	    WmInfo *wmPtr3 = wmPtr->containerPtr->wmInfoPtr;
	    if (wmPtr3 != NULL) {
		wmPtr3->numTransients--;
	    }
	}
	wmPtr->containerPtr = (TkWindow *) container;
	if (wmPtr2 != NULL) {
	    wmPtr2->numTransients++;
	}
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmWithdrawCmd --
 *
 *	Processes the "wm withdraw" Tcl command.
 *
 *----------------------------------------------------------------------
 */

static int
WmWithdrawCmd(
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    if (objc != 0) {
	Tcl_WrongNumArgs(interp, 0, objv, "pathName withdraw");
	return TCL_ERROR;
    }
    
    TkpWmSetState(winPtr, WithdrawnState);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * GlfwCloseCallback --
 *
 *	Called when a GLFW window is requested to close.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Handles WM_DELETE_WINDOW protocol.
 *
 *--------------------------------------------------------------
 */

static void
GlfwCloseCallback(
    GLFWwindow *window)
{
    WmInfo *wmPtr = FindWmInfoByGlfwWindow(window);
    
    if (wmPtr != NULL) {
	HandleProtocol(wmPtr, WM_DELETE_WINDOW);
    }
}

/*
 *--------------------------------------------------------------
 *
 * GlfwFocusCallback --
 *
 *	Called when a GLFW window gains or loses focus.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Handles WM_TAKE_FOCUS protocol if focused.
 *
 *--------------------------------------------------------------
 */

static void
GlfwFocusCallback(
    GLFWwindow *window,
    int focused)
{
    WmInfo *wmPtr = FindWmInfoByGlfwWindow(window);
    
    if (wmPtr != NULL && focused) {
	HandleProtocol(wmPtr, WM_TAKE_FOCUS);
    }
}

/*
 *--------------------------------------------------------------
 *
 * GlfwIconifyCallback --
 *
 *	Called when a GLFW window is iconified or restored.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates window state.
 *
 *--------------------------------------------------------------
 */

static void
GlfwIconifyCallback(
    GLFWwindow *window,
    int iconified)
{
    WmInfo *wmPtr = FindWmInfoByGlfwWindow(window);
    
    if (wmPtr != NULL) {
	wmPtr->attributes.zoomed = 0;
	if (iconified) {
	    TkpWmSetState(wmPtr->winPtr, IconicState);
	} else {
	    TkpWmSetState(wmPtr->winPtr, NormalState);
	}
    }
}

/*
 *--------------------------------------------------------------
 *
 * GlfwMaximizeCallback --
 *
 *	Called when a GLFW window is maximized or restored.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates window state.
 *
 *--------------------------------------------------------------
 */

static void
GlfwMaximizeCallback(
    GLFWwindow *window,
    int maximized)
{
    WmInfo *wmPtr = FindWmInfoByGlfwWindow(window);
    
    if (wmPtr != NULL) {
	wmPtr->attributes.zoomed = maximized;
    }
}

/*
 *--------------------------------------------------------------
 *
 * GlfwFramebufferSizeCallback --
 *
 *	Called when the framebuffer size changes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates window geometry.
 *
 *--------------------------------------------------------------
 */

static void
GlfwFramebufferSizeCallback(
    GLFWwindow *window,
    int width,
    int height)
{
    WmInfo *wmPtr = FindWmInfoByGlfwWindow(window);
    
    if (wmPtr != NULL) {
	TkWindow *winPtr = wmPtr->winPtr;
	winPtr->changes.width = width;
	winPtr->changes.height = height;
	
	/* Update Tk's internal geometry */
	Tk_GeometryRequest((Tk_Window)winPtr, width, height);
    }
}

/*
 *--------------------------------------------------------------
 *
 * GlfwWindowPosCallback --
 *
 *	Called when the window position changes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates window position.
 *
 *--------------------------------------------------------------
 */

static void
GlfwWindowPosCallback(
    GLFWwindow *window,
    int x,
    int y)
{
    WmInfo *wmPtr = FindWmInfoByGlfwWindow(window);
    
    if (wmPtr != NULL) {
	TkWindow *winPtr = wmPtr->winPtr;
	winPtr->changes.x = x;
	winPtr->changes.y = y;
	wmPtr->x = x;
	wmPtr->y = y;
    }
}

/*
 *--------------------------------------------------------------
 *
 * FindWmInfoByGlfwWindow --
 *
 *	Finds the WmInfo structure for a GLFW window.
 *
 * Results:
 *	Pointer to WmInfo, or NULL if not found.
 *
 *--------------------------------------------------------------
 */

static WmInfo*
FindWmInfoByGlfwWindow(
    GLFWwindow *window)
{
    /* First try the user pointer */
    WmInfo *wmPtr = (WmInfo *)glfwGetWindowUserPointer(window);
    if (wmPtr != NULL && wmPtr->glfwWindow == window) {
	return wmPtr;
    }
    
    /* Fall back to searching the list */
    for (WmInfo *w = firstWmPtr; w != NULL; w = w->nextPtr) {
	if (w->glfwWindow == window) {
	    return w;
	}
    }
    
    return NULL;
}

/*
 *--------------------------------------------------------------
 *
 * ConvertPhotoToGlfwIcon --
 *
 *	Converts a Tk photo image to GLFW icon format.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates memory for GLFW icon.
 *
 *--------------------------------------------------------------
 */

static void
ConvertPhotoToGlfwIcon(
    TkWindow *winPtr,
    Tk_PhotoHandle photo,
    int index)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    Tk_PhotoImageBlock block;
    int width, height;
    
    Tk_PhotoGetSize(photo, &width, &height);
    Tk_PhotoGetImage(photo, &block);
    
    /* Allocate or reallocate icon array */
    GLFWimage *newIcons = (GLFWimage *)ckalloc((wmPtr->glfwIconCount + 1) * sizeof(GLFWimage));
    if (wmPtr->glfwIcon != NULL && wmPtr->glfwIconCount > 0) {
	memcpy(newIcons, wmPtr->glfwIcon, wmPtr->glfwIconCount * sizeof(GLFWimage));
	ckfree((char *)wmPtr->glfwIcon);
    }
    wmPtr->glfwIcon = newIcons;
    
    /* Set up the new icon */
    GLFWimage *icon = &wmPtr->glfwIcon[wmPtr->glfwIconCount];
    icon->width = width;
    icon->height = height;
    
    /* Convert Tk photo data to RGBA format expected by GLFW */
    int pixelCount = width * height;
    unsigned char *pixels = (unsigned char *)ckalloc(pixelCount * 4);
    
    if (block.pixelSize == 4) {
	/* Already RGBA or similar 4-byte format */
	memcpy(pixels, block.pixelPtr, pixelCount * 4);
    } else if (block.pixelSize == 3) {
	/* RGB to RGBA conversion */
	unsigned char *src = (unsigned char *)block.pixelPtr;
	unsigned char *dst = pixels;
	for (int i = 0; i < pixelCount; i++) {
	    dst[0] = src[0];
	    dst[1] = src[1];
	    dst[2] = src[2];
	    dst[3] = 255;  /* Fully opaque */
	    src += 3;
	    dst += 4;
	}
    } else if (block.pixelSize == 1) {
	/* Grayscale to RGBA conversion */
	unsigned char *src = (unsigned char *)block.pixelPtr;
	unsigned char *dst = pixels;
	for (int i = 0; i < pixelCount; i++) {
	    dst[0] = src[0];
	    dst[1] = src[0];
	    dst[2] = src[0];
	    dst[3] = 255;
	    src += 1;
	    dst += 4;
	}
    }
    
    icon->pixels = pixels;
    wmPtr->glfwIconCount++;
    
    /* Apply icon to window if it exists */
    if (wmPtr->glfwWindow != NULL) {
	glfwSetWindowIcon(wmPtr->glfwWindow, wmPtr->glfwIconCount, wmPtr->glfwIcon);
    }
}

/*
 *--------------------------------------------------------------
 *
 * ApplyWindowHints --
 *
 *	Applies window hints based on Tk window attributes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	GLFW window hints are set.
 *
 *--------------------------------------------------------------
 */

static void
ApplyWindowHints(
    TkWindow *winPtr)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    Tk_Window tkwin = (Tk_Window)winPtr;
    
    /* Set resizable hint */
    int resizable = GLFW_TRUE;
    if (wmPtr->flags & (WM_WIDTH_NOT_RESIZABLE | WM_HEIGHT_NOT_RESIZABLE)) {
	resizable = GLFW_FALSE;
    }
    glfwWindowHint(GLFW_RESIZABLE, resizable);
    
    /* Set decorated hint based on override-redirect */
    int decorated = GLFW_TRUE;
    if (Tk_Attributes(tkwin)->override_redirect) {
	decorated = GLFW_FALSE;
    }
    glfwWindowHint(GLFW_DECORATED, decorated);
    
    /* Set floating hint for topmost */
    glfwWindowHint(GLFW_FLOATING, wmPtr->attributes.topmost ? GLFW_TRUE : GLFW_FALSE);
    
    /* Set transparency hint */
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, 
		   (wmPtr->attributes.alpha < 1.0) ? GLFW_TRUE : GLFW_FALSE);
    
    /* Set focus on show hint */
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    
    /* Disable auto-iconify for multi-monitor setups */
    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
}

/*
 *--------------------------------------------------------------
 *
 * HandleProtocol --
 *
 *	Invokes a protocol handler command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Tcl command is executed.
 *
 *--------------------------------------------------------------
 */

static void
HandleProtocol(
    WmInfo *wmPtr,
    int protocol)
{
    ProtocolHandler *protPtr;
    
    /* Find the protocol handler */
    for (protPtr = wmPtr->protPtr; protPtr != NULL; protPtr = protPtr->nextPtr) {
	if (protPtr->protocol == protocol) {
	    break;
	}
    }
    
    if (protPtr == NULL) {
	/* No handler - perform default action */
	if (protocol == WM_DELETE_WINDOW) {
	    Tk_DestroyWindow((Tk_Window)wmPtr->winPtr);
	}
	return;
    }
    
    /* Execute the handler command */
    Tcl_Interp *interp = protPtr->interp;
    if (Tcl_GlobalEval(interp, protPtr->command) != TCL_OK) {
	Tcl_BackgroundError(interp);
    }
}

/*
 *--------------------------------------------------------------
 *
 * ApplyFullscreenState --
 *
 *	Applies fullscreen state to a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Window enters or exits fullscreen mode.
 *
 *--------------------------------------------------------------
 */

static void
ApplyFullscreenState(
    TkWindow *winPtr)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    
    if (wmPtr->glfwWindow == NULL) {
	return;
    }
    
    if (wmPtr->attributes.fullscreen) {
	/* Get primary monitor */
	GLFWmonitor *monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode *mode = glfwGetVideoMode(monitor);
	
	/* Go fullscreen */
	glfwSetWindowMonitor(wmPtr->glfwWindow, monitor, 0, 0, 
			     mode->width, mode->height, mode->refreshRate);
    } else {
	/* Restore windowed mode */
	int width = (wmPtr->width > 0) ? wmPtr->width : winPtr->reqWidth;
	int height = (wmPtr->height > 0) ? wmPtr->height : winPtr->reqHeight;
	
	glfwSetWindowMonitor(wmPtr->glfwWindow, NULL, wmPtr->x, wmPtr->y,
			     width, height, GLFW_DONT_CARE);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetMainMenubar --
 *
 *	Sets the main menubar for a toplevel window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Menubar is associated with the window.
 *
 *----------------------------------------------------------------------
 */

void
TkpSetMainMenubar(
    TkWindow *winPtr,
    Tk_Window menubar)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    
    if (wmPtr == NULL) {
	return;
    }
    
    wmPtr->menubar = menubar;
    wmPtr->menuHeight = Tk_ReqHeight(menubar);
    if (wmPtr->menuHeight <= 0) {
	wmPtr->menuHeight = 1;
    }
    
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpUseWindowMenu --
 *
 *	Enables or disables the window menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Window menu state is updated.
 *
 *----------------------------------------------------------------------
 */

void
TkpUseWindowMenu(
    TkWindow *winPtr,
    int useWindowMenu)
{
    /* Window menus are handled by the compositor in Wayland */
    /* This is a no-op for compatibility */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetSystemDefault --
 *
 *	Gets a system-dependent default value.
 *
 * Results:
 *	The default value, or NULL if not found.
 *
 *----------------------------------------------------------------------
 */

const char *
TkpGetSystemDefault(
    Tk_Window tkwin,
    const char *dbClass,
    const char *dbName)
{
    /* For Wayland/GLFW, we can provide some sensible defaults */
    static struct {
	const char *dbClass;
	const char *dbName;
	const char *value;
    } defaults[] = {
	{"*font", "*Font", "Sans 10"},
	{"*background", "*Background", "white"},
	{"*foreground", "*Foreground", "black"},
	{"*selectBackground", "*SelectBackground", "#000080"},
	{"*selectForeground", "*SelectForeground", "white"},
	{NULL, NULL, NULL}
    };
    
    for (int i = 0; defaults[i].dbClass != NULL; i++) {
	if (strcmp(dbClass, defaults[i].dbClass) == 0 &&
	    strcmp(dbName, defaults[i].dbName) == 0) {
	    return defaults[i].value;
	}
    }
    
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmAddToColormapWindows --
 *
 *	Adds a window to the WM_COLORMAP_WINDOWS property.
 *	In Wayland, this is a no-op for compatibility.
 *
 * Results:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkWmAddToColormapWindows(
    TkWindow *winPtr)
{
    /* Colormaps are an X11 concept - no-op in Wayland */
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmRemoveFromColormapWindows --
 *
 *	Removes a window from the WM_COLORMAP_WINDOWS property.
 *	In Wayland, this is a no-op for compatibility.
 *
 * Results:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkWmRemoveFromColormapWindows(
    TkWindow *winPtr)
{
    /* Colormaps are an X11 concept - no-op in Wayland */
}

/*
 *----------------------------------------------------------------------
 *
 * WmWaitMapProc --
 *
 *	Waits for a transient window to be mapped.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May delay window mapping.
 *
 *----------------------------------------------------------------------
 */

static void
WmWaitMapProc(
    ClientData clientData,
    XEvent *eventPtr)
{
    TkWindow *winPtr = (TkWindow *)clientData;
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    
    if (eventPtr->type == MapNotify) {
	/* Transient's parent is now mapped */
	Tk_MapWindow((Tk_Window)winPtr);
	Tk_DeleteEventHandler((Tk_Window)wmPtr->containerPtr,
		StructureNotifyMask, WmWaitMapProc, clientData);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpWmGetWindowParams --
 *
 *	Gets window parameters for embedding.
 *
 * Results:
 *	1 if successful, 0 otherwise.
 *
 *----------------------------------------------------------------------
 */

int
TkpWmGetWindowParams(
    TkWindow *winPtr,
    TkWindow *parentPtr,
    int *x, int *y,
    int *width, int *height,
    int *borderWidth,
    int *overrideRedirect)
{
    /* For embedded windows, get geometry from parent */
    if (parentPtr != NULL) {
	*x = winPtr->changes.x;
	*y = winPtr->changes.y;
	*width = winPtr->changes.width;
	*height = winPtr->changes.height;
	*borderWidth = winPtr->changes.border_width;
	*overrideRedirect = Tk_Attributes((Tk_Window)winPtr)->override_redirect;
	return 1;
    }
    
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpWmSetWindowParams --
 *
 *	Sets window parameters for embedding.
 *
 * Results:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkpWmSetWindowParams(
    TkWindow *winPtr,
    int x, int y,
    int width, int height,
    int borderWidth,
    int overrideRedirect)
{
    /* Set window attributes for embedded windows */
    winPtr->changes.x = x;
    winPtr->changes.y = y;
    winPtr->changes.width = width;
    winPtr->changes.height = height;
    winPtr->changes.border_width = borderWidth;
    
    if (overrideRedirect) {
	XSetWindowAttributes atts;
	atts.override_redirect = True;
	Tk_ChangeWindowAttributes((Tk_Window)winPtr, CWOverrideRedirect, &atts);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpWmSetState --
 *
 *	Sets the window manager state for the wrapper window.
 *
 * Results:
 *	0 on error, 1 otherwise.
 *
 * Side effects:
 *	May minimize, restore, or withdraw a window.
 *
 *----------------------------------------------------------------------
 */

int
TkpWmSetState(
    TkWindow *winPtr,
    int state)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    if (state == WithdrawnState) {
	wmPtr->withdrawn = 1;
	if (wmPtr->flags & WM_NEVER_MAPPED) {
	    return 1;
	}
	if (wmPtr->glfwWindow != NULL) {
	    glfwHideWindow(wmPtr->glfwWindow);
	}
	WaitForMapNotify(winPtr, 0);
    } else if (state == NormalState) {
	wmPtr->withdrawn = 0;
	if (wmPtr->flags & WM_NEVER_MAPPED) {
	    return 1;
	}
	UpdateHints(winPtr);
	Tk_MapWindow((Tk_Window) winPtr);
	if (wmPtr->glfwWindow != NULL) {
	    if (wmPtr->attributes.fullscreen) {
		ApplyFullscreenState(winPtr);
	    } else {
		glfwRestoreWindow(wmPtr->glfwWindow);
	    }
	}
    } else if (state == IconicState) {
	if (wmPtr->flags & WM_NEVER_MAPPED) {
	    return 1;
	}
	if (wmPtr->withdrawn) {
	    UpdateHints(winPtr);
	    Tk_MapWindow((Tk_Window) winPtr);
	    wmPtr->withdrawn = 0;
	} else if (wmPtr->glfwWindow != NULL) {
	    glfwIconifyWindow(wmPtr->glfwWindow);
	}
	WaitForMapNotify(winPtr, 0);
    }

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetWrapperWindow --
 *
 *	Returns the wrapper window for a toplevel.
 *
 * Results:
 *	The wrapper window, or NULL.
 *
 *----------------------------------------------------------------------
 */

TkWindow *
TkpGetWrapperWindow(
    TkWindow *winPtr)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    if ((winPtr == NULL) || (wmPtr == NULL)) {
	return NULL;
    }

    return wmPtr->wrapperPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmStackorderToplevel --
 *
 *	Returns an array of all toplevel windows in stacking order.
 *
 * Results:
 *	Array of TkWindow pointers, terminated by NULL.
 *
 *----------------------------------------------------------------------
 */

TkWindow **
TkWmStackorderToplevel(
    TkWindow *parentPtr)
{
    WmInfo *wmPtr;
    TkWindow **windows, **windowPtr;
    int count = 0;

    /* Count toplevels */
    for (wmPtr = firstWmPtr; wmPtr != NULL; wmPtr = wmPtr->nextPtr) {
	if (wmPtr->winPtr->mainPtr == parentPtr->mainPtr) {
	    count++;
	}
    }

    if (count == 0) {
	return NULL;
    }

    /* Allocate array */
    windows = (TkWindow **)ckalloc((count + 1) * sizeof(TkWindow *));
    windowPtr = windows;

    /* Fill array */
    for (wmPtr = firstWmPtr; wmPtr != NULL; wmPtr = wmPtr->nextPtr) {
	if (wmPtr->winPtr->mainPtr == parentPtr->mainPtr) {
	    *windowPtr++ = wmPtr->winPtr;
	}
    }
    *windowPtr = NULL;

    return windows;
}

/*
 *----------------------------------------------------------------------
 *
 * TopLevelEventProc --
 *
 *	Handles events for top-level windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates window geometry and state.
 *
 *----------------------------------------------------------------------
 */

static void
TopLevelEventProc(
    ClientData clientData,
    XEvent *eventPtr)
{
    TkWindow *winPtr = (TkWindow *) clientData;
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    switch (eventPtr->type) {
    case ConfigureNotify:
	/* Update window geometry from GLFW */
	if (wmPtr->glfwWindow != NULL) {
	    int width, height;
	    glfwGetWindowSize(wmPtr->glfwWindow, &width, &height);
	    winPtr->changes.width = width;
	    winPtr->changes.height = height;
	    
	    int x, y;
	    glfwGetWindowPos(wmPtr->glfwWindow, &x, &y);
	    winPtr->changes.x = x;
	    winPtr->changes.y = y;
	}
	break;
    case MapNotify:
	winPtr->flags |= TK_MAPPED;
	break;
    case UnmapNotify:
	winPtr->flags &= ~TK_MAPPED;
	break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TopLevelReqProc --
 *
 *	Handles geometry requests for top-level windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May update size hints and geometry.
 *
 *----------------------------------------------------------------------
 */

static void
TopLevelReqProc(
    ClientData clientData,
    Tk_Window tkwin)
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    if ((wmPtr->width >= 0) && (wmPtr->height >= 0)) {
	return;
    }

    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    if (!(wmPtr->flags & (WM_UPDATE_PENDING|WM_NEVER_MAPPED))) {
	Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData    wmPtr->flags |= WM_UPDATE_PENDING;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateGeometryInfo --
 *
 *	Called as an idle handler to update geometry information.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Window size/position is updated via GLFW if needed.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateGeometryInfo(
    ClientData clientData)
{
    TkWindow *winPtr = (TkWindow *) clientData;
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    wmPtr->flags &= ~WM_UPDATE_PENDING;

    if (wmPtr->flags & WM_UPDATE_SIZE_HINTS) {
        UpdateSizeHints(winPtr);
        wmPtr->flags &= ~WM_UPDATE_SIZE_HINTS;
    }

    if (wmPtr->glfwWindow == NULL || wmPtr->withdrawn) {
        return;
    }

    int targetWidth  = (wmPtr->width  > 0) ? wmPtr->width  : winPtr->reqWidth;
    int targetHeight = (wmPtr->height > 0) ? wmPtr->height : winPtr->reqHeight;

    /* Resize if needed */
    if (targetWidth != wmPtr->configWidth || targetHeight != wmPtr->configHeight) {
        glfwSetWindowSize(wmPtr->glfwWindow, targetWidth, targetHeight);
        wmPtr->configWidth  = targetWidth;
        wmPtr->configHeight = targetHeight;
    }

    /* Move if needed */
    if (wmPtr->flags & WM_MOVE_PENDING ||
        wmPtr->x != winPtr->changes.x ||
        wmPtr->y != winPtr->changes.y) {
        glfwSetWindowPos(wmPtr->glfwWindow, wmPtr->x, wmPtr->y);
        wmPtr->flags &= ~WM_MOVE_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateHints --
 *
 *	Update WM hints (mostly size/position/resizable hints).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	GLFW window attributes may be changed.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateHints(
    TkWindow *winPtr)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    if (wmPtr == NULL) {
        return;
    }

    UpdateSizeHints(winPtr);
    UpdateTitle(winPtr);

    /* Most other hints are applied at creation time or via wm attributes */
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateSizeHints --
 *
 *	Apply min/max size, aspect ratio, etc. to the GLFW window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	GLFW size limits / aspect ratio set.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateSizeHints(
    TkWindow *winPtr)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    if (wmPtr->glfwWindow == NULL) {
        return;
    }

    glfwSetWindowSizeLimits(wmPtr->glfwWindow,
        wmPtr->minWidth, wmPtr->minHeight,
        (wmPtr->maxWidth  > 0) ? wmPtr->maxWidth  : GLFW_DONT_CARE,
        (wmPtr->maxHeight > 0) ? wmPtr->maxHeight : GLFW_DONT_CARE);

    /* Aspect ratio support (GLFW 3.3+) */
    if (wmPtr->sizeHintsFlags & PAspect) {
        glfwSetWindowAspectRatio(wmPtr->glfwWindow,
            wmPtr->minAspect.x, wmPtr->minAspect.y);
            /* Note: GLFW only supports one aspect ratio constraint */
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateTitle --
 *
 *	Set the window title using glfwSetWindowTitle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Window title updated.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateTitle(
    TkWindow *winPtr)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    const char *title = wmPtr->title ? wmPtr->title : winPtr->nameUid;

    if (wmPtr->glfwWindow != NULL) {
        glfwSetWindowTitle(wmPtr->glfwWindow, title);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdatePhotoIcon --
 *
 *	Placeholder — real work is done in WmIconphotoCmd → ConvertPhotoToGlfwIcon
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None (kept for API compatibility).
 *
 *----------------------------------------------------------------------
 */

static void
UpdatePhotoIcon(
    TkWindow *winPtr)
{
    /* Most work already done when wm iconphoto is called */
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateVRootGeometry --
 *
 *	Update virtual root geometry (mostly for compatibility).
 *	In practice we just use primary monitor size.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	vRootWidth/vRootHeight updated.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateVRootGeometry(
    WmInfo *wmPtr)
{
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    if (monitor) {
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        if (mode) {
            wmPtr->vRootWidth  = mode->width;
            wmPtr->vRootHeight = mode->height;
            glfwGetMonitorPos(monitor, &wmPtr->vRootX, &wmPtr->vRootY);
            return;
        }
    }

    /* fallback */
    wmPtr->vRootX      = 0;
    wmPtr->vRootY      = 0;
    wmPtr->vRootWidth  = 1920;
    wmPtr->vRootHeight = 1080;
}

/*
 *----------------------------------------------------------------------
 *
 * WaitForMapNotify --
 *
 *	Compatibility no-op (GLFW visibility is synchronous).
 *
 *----------------------------------------------------------------------
 */

static void
WaitForMapNotify(
    TkWindow *winPtr,
    int mapped)
{
    /* No-op on GLFW/Wayland — visibility state is immediate */
}

/*
 *----------------------------------------------------------------------
 *
 * MenubarReqProc --
 *
 *	Called when the menubar geometry changes.
 *
 *----------------------------------------------------------------------
 */

static void
MenubarReqProc(
    ClientData clientData,
    Tk_Window tkwin)
{
    WmInfo *wmPtr = (WmInfo *) clientData;

    wmPtr->menuHeight = Tk_ReqHeight(tkwin);
    if (wmPtr->menuHeight <= 0) {
        wmPtr->menuHeight = 1;
    }

    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, wmPtr->winPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * ParseGeometry --
 *
 *	Parse geometry string of the form WxH±X±Y
 *
 * Results:
 *	TCL_OK or TCL_ERROR
 *
 *----------------------------------------------------------------------
 */

static int
ParseGeometry(
    Tcl_Interp *interp,
    char *string,
    TkWindow *winPtr)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int width, height, x, y, result;
    char *end;
    char xSign = '+', ySign = '+';

    result = Tk_GetPixels(interp, (Tk_Window)winPtr, string, &width);
    if (result != TCL_OK) {
        return result;
    }

    string = Tcl_UtfNext(string);   /* skip possible 'x' */
    result = Tk_GetPixels(interp, (Tk_Window)winPtr, string, &height);
    if (result != TCL_OK) {
        return result;
    }

    /* Optional position */
    while (*string && (*string == ' ' || *string == '\t')) {
        string++;
    }

    if (*string == '\0') {
        wmPtr->width  = width;
        wmPtr->height = height;
        wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
        WmUpdateGeom(wmPtr, winPtr);
        return TCL_OK;
    }

    if (*string == '+' || *string == '-') {
        xSign = *string++;
    }
    x = strtol(string, &end, 10);
    string = end;

    if (*string == '+' || *string == '-') {
        ySign = *string++;
    }
    y = strtol(string, &end, 10);

    if (*end != '\0') {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf(
            "bad geometry specifier \"%s\"", string));
        return TCL_ERROR;
    }

    wmPtr->width  = width;
    wmPtr->height = height;
    wmPtr->x      = (xSign == '-') ? -x : x;
    wmPtr->y      = (ySign == '-') ? -y : y;

    if (xSign == '-') wmPtr->flags |= WM_NEGATIVE_X;
    else              wmPtr->flags &= ~WM_NEGATIVE_X;

    if (ySign == '-') wmPtr->flags |= WM_NEGATIVE_Y;
    else              wmPtr->flags &= ~WM_NEGATIVE_Y;

    wmPtr->flags |= WM_MOVE_PENDING | WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmUpdateGeom --
 *
 *	Arrange for geometry info to be updated later (idle callback).
 *
 *----------------------------------------------------------------------
 */

static void
WmUpdateGeom(
    WmInfo *wmPtr,
    TkWindow *winPtr)
{
    if (!(wmPtr->flags & (WM_UPDATE_PENDING | WM_NEVER_MAPPED))) {
        Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData) winPtr);
        wmPtr->flags |= WM_UPDATE_PENDING;
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
