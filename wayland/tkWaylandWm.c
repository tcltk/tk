/*
 * tkWaylandWm.c --
 *
 *	Window manager integration for the Wayland/GLFW/NanoVG Tk port.
 *	Implements the "wm" Tcl command and all platform window-management
 *	entry points required by Tk's generic layer.
 *
 * Copyright © 1991-1994 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 * Copyright © 2026      Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkPort.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * Undefine X11 macros that might conflict with our local definitions.
 */
#undef USPosition
#undef USSize
#undef PPosition
#undef PSize
#undef PMinSize
#undef PMaxSize
#undef PResizeInc
#undef PAspect
#undef PBaseSize
#undef PWinGravity

/*
 *----------------------------------------------------------------------
 *
 * Protocol identifiers – replace X11 Atoms for WM_DELETE_WINDOW etc.
 *
 *----------------------------------------------------------------------
 */

#define WM_DELETE_WINDOW    1
#define WM_TAKE_FOCUS       2
#define WM_SAVE_YOURSELF    3


/*
 * WmInfo flag bits.
 */
#define WM_NEVER_MAPPED             (1<<0)
#define WM_UPDATE_PENDING           (1<<1)
#define WM_NEGATIVE_X               (1<<2)
#define WM_NEGATIVE_Y               (1<<3)
#define WM_UPDATE_SIZE_HINTS        (1<<4)
#define WM_SYNC_PENDING             (1<<5)
#define WM_CREATE_PENDING           (1<<6)
#define WM_ABOUT_TO_MAP             (1<<9)
#define WM_MOVE_PENDING             (1<<10)
#define WM_COLORMAPS_EXPLICIT       (1<<11)
#define WM_ADDED_TOPLEVEL_COLORMAP  (1<<12)
#define WM_WIDTH_NOT_RESIZABLE      (1<<13)
#define WM_HEIGHT_NOT_RESIZABLE     (1<<14)
#define WM_WITHDRAWN                (1<<15)
#define WM_FULLSCREEN_PENDING       (1<<16)

/* Size-hint flags. */
#define WM_USPosition   (1<<0)
#define WM_USSize       (1<<1)
#define WM_PPosition    (1<<2)
#define WM_PSize        (1<<3)
#define WM_PMinSize     (1<<4)
#define WM_PMaxSize     (1<<5)
#define WM_PResizeInc   (1<<6)
#define WM_PAspect      (1<<7)
#define WM_PBaseSize    (1<<8)
#define WM_PWinGravity  (1<<9)

/* Window-state constants (X11 compatible). */
#define WithdrawnState  0
#define NormalState     1
#define IconicState     3

/* Gravity constants. */
#define NorthWestGravity 1
#define StaticGravity   10

/* Global toplevel list. */
static WmInfo *firstWmPtr = NULL;

/* Wm attribute names. */
const char *const WmAttributeNames[] = {
    "-alpha", "-fullscreen", "-topmost", "-type",
    "-zoomed", NULL
};

/*
 *----------------------------------------------------------------------
 * Forward declarations
 *----------------------------------------------------------------------
 */

static void TopLevelEventProc(ClientData clientData, XEvent *eventPtr);
static void TopLevelReqProc(ClientData clientData, Tk_Window tkwin);
static void UpdateGeometryInfo(ClientData clientData);
static void UpdateHints(TkWindow *winPtr);
static void UpdateSizeHints(TkWindow *winPtr);
static void UpdateTitle(TkWindow *winPtr);
static void UpdatePhotoIcon(TkWindow *winPtr);
static void UpdateVRootGeometry(WmInfo *wmPtr);
static void WaitForMapNotify(TkWindow *winPtr, int mapped);
static int  ParseGeometry(Tcl_Interp *interp, const char *string,
			  TkWindow *winPtr);
static void WmUpdateGeom(WmInfo *wmPtr, TkWindow *winPtr);


/* External window decoration functions. */
extern TkWaylandDecoration *TkWaylandGetDecoration(TkWindow *winPtr);
extern void TkWaylandSetDecorationTitle(TkWaylandDecoration *decor, const char *title);
extern void TkWaylandSetWindowMaximized(TkWaylandDecoration *decor, int maximized);
extern void TkWaylandConfigureWindowDecorations(void);
extern int TkWaylandShouldUseCSD(void);
extern TkWaylandDecoration *TkWaylandCreateDecoration(TkWindow *winPtr, GLFWwindow *glfwWindow); 


/* wm sub-command handlers. */
static int		WmAspectCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmAttributesCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmClientCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmColormapwindowsCmd(Tk_Window tkwin,
			    TkWindow *winPtr, Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmCommandCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmDeiconifyCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmFocusmodelCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmForgetCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmFrameCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmGeometryCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmGridCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmGroupCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmIconbadgeCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmIconbitmapCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmIconifyCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmIconmaskCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmIconnameCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmIconphotoCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmIconpositionCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmIconwindowCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmManageCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmMaxsizeCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmMinsizeCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmOverrideredirectCmd(Tk_Window tkwin,
			    TkWindow *winPtr, Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmPositionfromCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmProtocolCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmResizableCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmSizefromCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmStackorderCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmStateCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmTitleCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmTransientCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static int		WmWithdrawCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, int objc,
			    Tcl_Obj *const objv[]);
static void             WmWaitMapProc(ClientData clientData, XEvent *eventPtr);

/* GLFW integration helpers. */
static void CreateGlfwWindow(TkWindow *winPtr);
static void DestroyGlfwWindow(WmInfo *wmPtr);
static void ConvertPhotoToGlfwIcon(TkWindow *winPtr, Tk_PhotoHandle photo);
static void ApplyWindowHints(TkWindow *winPtr);
static void ApplyFullscreenState(TkWindow *winPtr);

/* Geometry manager. */
static Tk_GeomMgr wmMgrType = {
    "wm",
    TopLevelReqProc,
    NULL,
};

/*
 *----------------------------------------------------------------------
 *
 * TkWmNewWindow --
 *
 *	Initialize a new WmInfo structure for a toplevel window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates and initializes a WmInfo structure, links it into the
 *	global list, and sets up geometry management for the window.
 *
 *----------------------------------------------------------------------
 */

void
TkWmNewWindow(
	      TkWindow *winPtr)
{
    WmInfo *wmPtr;

    wmPtr = (WmInfo *)ckalloc(sizeof(WmInfo));
    memset(wmPtr, 0, sizeof(WmInfo));

    wmPtr->winPtr      = winPtr;
    wmPtr->glfwWindow  = NULL;
    wmPtr->withdrawn   = 0;
    wmPtr->initialState =  NormalState;
    wmPtr->minWidth    = wmPtr->minHeight = 1;
    wmPtr->widthInc    = wmPtr->heightInc = 1;
    wmPtr->minAspect.x = wmPtr->minAspect.y = 1;
    wmPtr->maxAspect.x = wmPtr->maxAspect.y = 1;
    wmPtr->reqGridWidth = wmPtr->reqGridHeight = -1;
    wmPtr->gravity     = NorthWestGravity;
    wmPtr->width       = wmPtr->height = -1;
    wmPtr->x           = winPtr->changes.x;
    wmPtr->y           = winPtr->changes.y;
    wmPtr->parentWidth = winPtr->changes.width
	+ 2 * winPtr->changes.border_width;
    wmPtr->parentHeight= winPtr->changes.height
	+ 2 * winPtr->changes.border_width;
    wmPtr->configWidth = wmPtr->configHeight = -1;
    wmPtr->vRootWidth  = 800;
    wmPtr->vRootHeight = 600;
    wmPtr->attributes.alpha = 1.0;
    wmPtr->reqState    = wmPtr->attributes;
    wmPtr->flags       = WM_NEVER_MAPPED;

    wmPtr->nextPtr = firstWmPtr;
    firstWmPtr     = wmPtr;
    winPtr->wmInfoPtr = wmPtr;

    UpdateVRootGeometry(wmPtr);

    Tk_ManageGeometry((Tk_Window)winPtr, &wmMgrType, (ClientData)0);
}

/*
 *----------------------------------------------------------------------
 *
 * CreateGlfwWindow --
 *
 *	Creates the GLFW window for a toplevel.  Sets up callbacks via
 *	the canonical TkGlfwSetupCallbacks so that the WindowMapping*
 *	stored by TkGlfwCreateWindow remains the sole GLFW user-pointer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates a new GLFW window, sets up callbacks, and applies
 *	initial window properties (title, size hints, etc.).
 *
 *----------------------------------------------------------------------
 */

static void
CreateGlfwWindow(TkWindow *winPtr)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;

    if (wmPtr->glfwWindow != NULL) {
        return;
    }

    /* Tk_MakeWindow already created the platform window. */
    if (winPtr->window == None) {
        Tcl_Panic("CreateGlfwWindow: Tk window has no platform window");
        return;
    }

    wmPtr->glfwWindow = (GLFWwindow *)winPtr->window;

    /* Apply wm properties that are valid AFTER creation */

    UpdateTitle(winPtr);
    UpdateSizeHints(winPtr);

    if (wmPtr->attributes.alpha != 1.0) {
        glfwSetWindowOpacity(wmPtr->glfwWindow,
                             (float)wmPtr->attributes.alpha);
    }

    if (wmPtr->glfwIcon != NULL) {
        glfwSetWindowIcon(wmPtr->glfwWindow,
                          wmPtr->glfwIconCount, wmPtr->glfwIcon);
    }

    if (wmPtr->x != 0 || wmPtr->y != 0) {
        glfwSetWindowPos(wmPtr->glfwWindow, wmPtr->x, wmPtr->y);
    }

    /* Register wm event handler */
    Tk_CreateEventHandler((Tk_Window)winPtr,
			  StructureNotifyMask | PropertyChangeMask,
			  TopLevelEventProc, (ClientData)winPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyGlfwWindow --
 *
 *	Destroys the GLFW window associated with a WmInfo structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The GLFW window is destroyed and the wmPtr->glfwWindow field
 *	is set to NULL.
 *
 *----------------------------------------------------------------------
 */

static void
DestroyGlfwWindow(
		  WmInfo *wmPtr)
{
    if (wmPtr->glfwWindow == NULL) {
        return;
    }
    TkGlfwDestroyWindow(wmPtr->glfwWindow);
    wmPtr->glfwWindow = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmMapWindow --
 *
 *	Maps the window (makes it visible). Fixed to ensure window
 *	is properly shown during initial startup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window becomes visible, and a MapNotify event is sent to
 *	Tk's event system.
 *
 *----------------------------------------------------------------------
 */

void 
TkWmMapWindow(TkWindow *winPtr) 
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    if (!wmPtr) Tcl_Panic("TkWmMapWindow: No WmInfo");

    wmPtr->withdrawn = 0;
    wmPtr->initialState = NormalState;

    if (!Tk_IsEmbedded(winPtr) && !wmPtr->glfwWindow) {
        CreateGlfwWindow(winPtr);
        UpdateHints(winPtr);
        UpdateTitle(winPtr);
        UpdatePhotoIcon(winPtr);
    }

    UpdateGeometryInfo((ClientData)winPtr);

    if (wmPtr->glfwWindow) {
        /* Wayland requires show first. */
        glfwShowWindow(wmPtr->glfwWindow);

        /* Now safe to draw - Wayland requires buffer to be primed. */
        GLFWwindow *prev = glfwGetCurrentContext();
        glfwMakeContextCurrent(wmPtr->glfwWindow);
        glClearColor(0.9f,0.9f,0.9f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glfwSwapBuffers(wmPtr->glfwWindow);
        if (prev) glfwMakeContextCurrent(prev);

	    /* Run event loop to force update. */
        TkGlfwProcessEvents();
        winPtr->flags |= TK_MAPPED;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmUnmapWindow --
 *
 *	Unmaps the window (hides it).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window becomes hidden, and an UnmapNotify event is sent to
 *	Tk's event system.
 *
 *----------------------------------------------------------------------
 */

void
TkWmUnmapWindow(TkWindow *winPtr)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    if (!wmPtr) return;

    if (wmPtr->glfwWindow) {
        glfwHideWindow(wmPtr->glfwWindow);
    }

    winPtr->flags &= ~TK_MAPPED;
}


/*
 *----------------------------------------------------------------------
 *
 * TkWmDeadWindow --
 *
 *	Clean up window manager information when a window is destroyed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all resources associated with the WmInfo structure,
 *	removes it from the global list, and clears winPtr->wmInfoPtr.
 *
 *----------------------------------------------------------------------
 */

void
TkWmDeadWindow(
	       TkWindow *winPtr)
{
    WmInfo *wmPtr  = (WmInfo *)winPtr->wmInfoPtr;
    WmInfo *wmPtr2;
    int     i;

    if (wmPtr == NULL) {
        return;
    }

    Tk_DeleteEventHandler((Tk_Window)winPtr,
			  StructureNotifyMask | PropertyChangeMask,
			  TopLevelEventProc, (ClientData)winPtr);

    if (wmPtr->flags & WM_UPDATE_PENDING) {
        Tcl_CancelIdleCall(UpdateGeometryInfo, (ClientData)winPtr);
    }

    DestroyGlfwWindow(wmPtr);

    if (wmPtr->wrapperPtr != NULL) {
        Tk_DestroyWindow((Tk_Window)wmPtr->wrapperPtr);
        wmPtr->wrapperPtr = NULL;
    }

    if (wmPtr->title)         ckfree(wmPtr->title);
    if (wmPtr->iconName)      ckfree(wmPtr->iconName);
    if (wmPtr->leaderName)    ckfree(wmPtr->leaderName);
    if (wmPtr->menubar)       Tk_DestroyWindow(wmPtr->menubar);
    if (wmPtr->icon)          Tk_DestroyWindow(wmPtr->icon);
    if (wmPtr->iconDataPtr)   ckfree((char *)wmPtr->iconDataPtr);

    if (wmPtr->glfwIcon != NULL) {
        for (i = 0; i < wmPtr->glfwIconCount; i++) {
            if (wmPtr->glfwIcon[i].pixels != NULL) {
                ckfree((char *)wmPtr->glfwIcon[i].pixels);
            }
        }
        ckfree((char *)wmPtr->glfwIcon);
    }

    while (wmPtr->protPtr != NULL) {
        ProtocolHandler *protPtr = wmPtr->protPtr;
        wmPtr->protPtr = protPtr->nextPtr;
        Tcl_EventuallyFree((ClientData)protPtr, TCL_DYNAMIC);
    }

    if (wmPtr->cmdArgv != NULL) {
        /* Release refcounts acquired in WmCommandCmd. */
        Tcl_Size j;
        for (j = 0; j < wmPtr->cmdArgc; j++) {
            Tcl_DecrRefCount(wmPtr->cmdArgv[j]);
        }
        ckfree((char *)wmPtr->cmdArgv);
    }
    if (wmPtr->clientMachine)  ckfree(wmPtr->clientMachine);

    /* Remove from global list. */
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
    ckfree((char *)wmPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmSetClass --
 *
 *	No-op on Wayland (class hints not supported).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkWmSetClass(
	     TCL_UNUSED(TkWindow *))
{
    /* No-op on Wayland. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmCleanup --
 *
 *	Clean up all window manager information during display cleanup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all WmInfo structures and associated resources.
 *
 *----------------------------------------------------------------------
 */

void
TkWmCleanup(
	    TCL_UNUSED(TkDisplay *))
{
    WmInfo *wmPtr, *nextPtr;
    int     i;

    for (wmPtr = firstWmPtr; wmPtr != NULL; wmPtr = nextPtr) {
        nextPtr = wmPtr->nextPtr;

        if (wmPtr->title)          ckfree(wmPtr->title);
        if (wmPtr->iconName)       ckfree(wmPtr->iconName);
        if (wmPtr->iconDataPtr)    ckfree((char *)wmPtr->iconDataPtr);
        if (wmPtr->leaderName)     ckfree(wmPtr->leaderName);
        if (wmPtr->menubar)        Tk_DestroyWindow(wmPtr->menubar);
        if (wmPtr->wrapperPtr)     Tk_DestroyWindow((Tk_Window)wmPtr->wrapperPtr);
        if (wmPtr->clientMachine)  ckfree(wmPtr->clientMachine);

        while (wmPtr->protPtr != NULL) {
            ProtocolHandler *p = wmPtr->protPtr;
            wmPtr->protPtr = p->nextPtr;
            Tcl_EventuallyFree((ClientData)p, TCL_DYNAMIC);
        }
        if (wmPtr->cmdArgv != NULL) {
            Tcl_Size j;
            for (j = 0; j < wmPtr->cmdArgc; j++) {
                Tcl_DecrRefCount(wmPtr->cmdArgv[j]);
            }
            ckfree((char *)wmPtr->cmdArgv);
        }
        if (wmPtr->glfwIcon != NULL) {
            for (i = 0; i < wmPtr->glfwIconCount; i++) {
                if (wmPtr->glfwIcon[i].pixels != NULL) {
                    ckfree((char *)wmPtr->glfwIcon[i].pixels);
                }
            }
            ckfree((char *)wmPtr->glfwIcon);
        }

        DestroyGlfwWindow(wmPtr);
        ckfree((char *)wmPtr);
    }
    firstWmPtr = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MakeWindow --
 *
 *	Platform-specific window creation called by Tk's generic layer.
 *	Uses TkGlfwCreateWindow so that the WindowMapping is properly
 *	registered and the GLFW user pointer is set to the mapping (not
 *	to the WmInfo, which would break TkGlfwGetTkWindow).
 *
 * Results:
 *	Returns a Window identifier (the GLFW window pointer cast to Window).
 *
 * Side effects:
 *	Creates a new GLFW window for toplevel windows, or generates a
 *	unique identifier for child windows.
 *
 *----------------------------------------------------------------------
 */

Window
Tk_MakeWindow(
    Tk_Window tkwin,
    TCL_UNUSED(Window))        /* parent – ignored for toplevels */
{
    TkWindow  *winPtr     = (TkWindow *)tkwin;
    TkWindow  *parentPtr;
    GLFWwindow *glfwWindow = NULL;
    int         width, height;
    Drawable    drawable;
    Window      window;

    if (winPtr->parentPtr == NULL) {
        /*
         * Toplevel: create a new GLFW window via TkGlfwCreateWindow.
         * Decorations and mapping are handled inside that function.
         */
        width  = (winPtr->changes.width  > 0) ? winPtr->changes.width  : 200;
        height = (winPtr->changes.height > 0) ? winPtr->changes.height : 200;

        glfwWindow = TkGlfwCreateWindow(winPtr, width, height,
                                        Tk_Name(tkwin), &drawable);
        if (!glfwWindow) {
            return None;
        }

        /* Store window handle. */
        window = (Window)glfwWindow;
        winPtr->window = window;

        /* Ensure WmInfo exists. */
        if (winPtr->wmInfoPtr == NULL) {
            TkWmNewWindow(winPtr);
        }

        /* Mark window as never mapped yet. */
        WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
        if (wmPtr) {
            wmPtr->glfwWindow = glfwWindow;
            wmPtr->flags |= WM_NEVER_MAPPED;
        }

    } else {
        /*
         * Child window: share parent's rendering context.
         * Generate a unique ID derived from pathName and toplevel window.
         */
        parentPtr = winPtr->parentPtr;
        while (parentPtr->parentPtr != NULL) {
            parentPtr = parentPtr->parentPtr;
        }

        const char *p;
        Window hash = 0;
        for (p = winPtr->pathName; *p; p++) {
            hash = hash * 131u + (unsigned char)*p;
        }
        window = parentPtr->window ^ hash;
        if (window == parentPtr->window || window == None) {
            window = parentPtr->window + 1;
        }
        winPtr->window = window;
    }

    return window;
}
/*
 *----------------------------------------------------------------------
 *
 * Tk_SetGrid --
 *
 *	Set grid-based resize increments for a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the window's size hints to reflect the grid dimensions.
 *
 *----------------------------------------------------------------------
 */

void
Tk_SetGrid(
	   Tk_Window tkwin,
	   int       reqWidth,
	   int       reqHeight,
	   int       widthInc,
	   int       heightInc)
{
    TkWindow *winPtr = (TkWindow *)tkwin;
    WmInfo   *wmPtr;

    if (widthInc  <= 0) widthInc  = 1;
    if (heightInc <= 0) heightInc = 1;

    while (!(winPtr->flags & TK_TOP_HIERARCHY)) {
        winPtr = winPtr->parentPtr;
        if (winPtr == NULL) return;
    }
    wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    if (wmPtr == NULL) return;

    if ((wmPtr->gridWin != NULL) && (wmPtr->gridWin != tkwin)) return;

    if ((wmPtr->reqGridWidth  == reqWidth)
	&& (wmPtr->reqGridHeight == reqHeight)
	&& (wmPtr->widthInc      == widthInc)
	&& (wmPtr->heightInc     == heightInc)
	&& ((wmPtr->sizeHintsFlags & WM_PBaseSize) == WM_PBaseSize)) {
        return;
    }

    if ((wmPtr->gridWin == NULL) && !(wmPtr->flags & WM_NEVER_MAPPED)) {
        wmPtr->width  = -1;
        wmPtr->height = -1;
    }

    wmPtr->gridWin       = tkwin;
    wmPtr->reqGridWidth  = reqWidth;
    wmPtr->reqGridHeight = reqHeight;
    wmPtr->widthInc      = widthInc;
    wmPtr->heightInc     = heightInc;
    wmPtr->sizeHintsFlags |= WM_PBaseSize;
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;

    if (!(wmPtr->flags & (WM_UPDATE_PENDING | WM_NEVER_MAPPED))) {
        Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData)winPtr);
        wmPtr->flags |= WM_UPDATE_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_UnsetGrid --
 *
 *	Remove grid-based resize increments for a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the window's size hints to remove grid constraints.
 *
 *----------------------------------------------------------------------
 */

void
Tk_UnsetGrid(
	     Tk_Window tkwin)
{
    TkWindow *winPtr = (TkWindow *)tkwin;
    WmInfo   *wmPtr;

    while (!(winPtr->flags & TK_TOP_HIERARCHY)) {
        winPtr = winPtr->parentPtr;
        if (winPtr == NULL) return;
    }
    wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    if (wmPtr == NULL || tkwin != wmPtr->gridWin) return;

    wmPtr->gridWin = NULL;
    wmPtr->sizeHintsFlags &= ~WM_PBaseSize;

    if (wmPtr->width != -1) {
        wmPtr->width  = winPtr->reqWidth
            + (wmPtr->width  - wmPtr->reqGridWidth)  * wmPtr->widthInc;
        wmPtr->height = winPtr->reqHeight
            + (wmPtr->height - wmPtr->reqGridHeight) * wmPtr->heightInc;
    }

    wmPtr->widthInc  = 1;
    wmPtr->heightInc = 1;
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;

    if (!(wmPtr->flags & (WM_UPDATE_PENDING | WM_NEVER_MAPPED))) {
        Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData)winPtr);
        wmPtr->flags |= WM_UPDATE_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetRootCoords --
 *
 *	Compute the root window coordinates of a Tk window.
 *
 * Results:
 *	Sets *xPtr and *yPtr to the window's root coordinates.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Tk_GetRootCoords(
		 Tk_Window tkwin,
		 int      *xPtr,
		 int      *yPtr)
{
    TkWindow *winPtr = (TkWindow *)tkwin;
    int       x = 0, y = 0;

    while (1) {
        x += winPtr->changes.x + winPtr->changes.border_width;
        y += winPtr->changes.y + winPtr->changes.border_width;

        if ((winPtr->wmInfoPtr != NULL)
	    && (((WmInfo *)winPtr->wmInfoPtr)->menubar == (Tk_Window)winPtr)) {
            y -= ((WmInfo *)winPtr->wmInfoPtr)->menuHeight;
            winPtr = ((WmInfo *)winPtr->wmInfoPtr)->winPtr;
            continue;
        }

        if (winPtr->flags & TK_TOP_LEVEL) {
            if (winPtr->flags & TK_EMBEDDED) {
                Tk_Window container = Tk_GetOtherWindow(tkwin);
                if (container == NULL) break;
                winPtr = (TkWindow *)container;
                continue;
            }
            break;
        }

        winPtr = winPtr->parentPtr;
        if (winPtr == NULL) break;
    }

    *xPtr = x;
    *yPtr = y;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_CoordsToWindow --
 *
 *	Find the window at the given root coordinates.
 *
 * Results:
 *	Returns the Tk_Window at the specified coordinates, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tk_Window
Tk_CoordsToWindow(
		  int        rootX,
		  int        rootY,
		  Tk_Window  tkwin)
{
    TkWindow *winPtr  = (TkWindow *)tkwin;
    TkWindow *nextPtr, *childPtr;
    int       x = rootX, y = rootY;

    while (winPtr != NULL) {
        nextPtr = NULL;

        for (childPtr = winPtr->childList; childPtr != NULL;
             childPtr = childPtr->nextPtr) {
            int tmpx, tmpy, bd;

            if (!Tk_IsMapped((Tk_Window)childPtr)
		|| (childPtr->flags & TK_TOP_HIERARCHY)) {
                continue;
            }

            tmpx = x - childPtr->changes.x;
            tmpy = y - childPtr->changes.y;
            bd   = childPtr->changes.border_width;

            if (tmpx >= -bd && tmpy >= -bd
		&& tmpx < (childPtr->changes.width  + bd)
		&& tmpy < (childPtr->changes.height + bd)) {
                nextPtr = childPtr;
            }
        }

        if (nextPtr == NULL) break;

        x -= nextPtr->changes.x;
        y -= nextPtr->changes.y;
        winPtr = nextPtr;
    }

    return (Tk_Window)winPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetVRootGeometry --
 *
 *	Get the geometry of the virtual root (screen) for a window.
 *
 * Results:
 *	Sets *xPtr, *yPtr, *widthPtr, *heightPtr to the virtual root
 *	geometry.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Tk_GetVRootGeometry(
		    Tk_Window tkwin,
		    int      *xPtr,
		    int      *yPtr,
		    int      *widthPtr,
		    int      *heightPtr)
{
    TkWindow *winPtr = (TkWindow *)tkwin;
    WmInfo   *wmPtr;

    while (!(winPtr->flags & TK_TOP_HIERARCHY) && winPtr->parentPtr != NULL) {
        winPtr = winPtr->parentPtr;
    }

    wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    if (wmPtr == NULL) {
        *xPtr = *yPtr = 0;
        *widthPtr = 1920; *heightPtr = 1080;
        return;
    }

    *xPtr      = wmPtr->vRootX;
    *yPtr      = wmPtr->vRootY;
    *widthPtr  = wmPtr->vRootWidth;
    *heightPtr = wmPtr->vRootHeight;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MoveToplevelWindow --
 *
 *	Move a toplevel window to a new position.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the window's position and schedules a geometry update.
 *
 *----------------------------------------------------------------------
 */

void
Tk_MoveToplevelWindow(
		      Tk_Window tkwin,
		      int       x,
		      int       y)
{
    TkWindow *winPtr = (TkWindow *)tkwin;
    WmInfo   *wmPtr  = (WmInfo *)winPtr->wmInfoPtr;

    if (!(winPtr->flags & TK_TOP_LEVEL)) {
        Tcl_Panic("Tk_MoveToplevelWindow called with non-toplevel window");
    }

    wmPtr->x = x;
    wmPtr->y = y;
    wmPtr->flags |= WM_MOVE_PENDING;
    wmPtr->flags &= ~(WM_NEGATIVE_X | WM_NEGATIVE_Y);

    if (!(wmPtr->sizeHintsFlags & (WM_USPosition | WM_PPosition))) {
        wmPtr->sizeHintsFlags |= WM_USPosition;
        wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    }

    if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
        if (wmPtr->flags & WM_UPDATE_PENDING) {
            Tcl_CancelIdleCall(UpdateGeometryInfo, (ClientData)winPtr);
        }
        UpdateGeometryInfo((ClientData)winPtr);
    }
}



/*
 *----------------------------------------------------------------------
 *
 * Stubs and no-ops
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * TkWmRestackToplevel --
 *
 *	No-op on Wayland (compositor controls stacking).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkWmRestackToplevel(
		    TCL_UNUSED(TkWindow *),
		    TCL_UNUSED(int),
		    TCL_UNUSED(TkWindow *))
{
    /* Compositor controls stacking in Wayland. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmProtocolEventProc --
 *
 *	No-op on Wayland (protocols handled via GLFW callbacks).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkWmProtocolEventProc(
		      TCL_UNUSED(TkWindow *),
		      TCL_UNUSED(XEvent *))
{
    /* Protocols handled via GLFW callbacks. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMakeMenuWindow --
 *
 *	No-op on Wayland (no special menu window configuration needed).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkpMakeMenuWindow(
		  TCL_UNUSED(Tk_Window),
		  TCL_UNUSED(int))
{
    /* No special configuration needed in Wayland. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmFocusToplevel --
 *
 *	Returns the toplevel window for a wrapper window.
 *
 * Results:
 *	Returns the toplevel TkWindow, or NULL if not applicable.
 *
 * Side effects:
 *	None.
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
    return ((WmInfo *)winPtr->wmInfoPtr)->winPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetPointerCoords --
 *
 *	Get mouse pointer coordinates (not implemented on Wayland).
 *
 * Results:
 *	Sets *xPtr and *yPtr to -1.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkGetPointerCoords(
		   TCL_UNUSED(Tk_Window),
		   int *xPtr,
		   int *yPtr)
{
    *xPtr = *yPtr = -1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmAddToColormapWindows --
 *
 *	No-op on Wayland (colormap windows not applicable).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkWmAddToColormapWindows(TCL_UNUSED(TkWindow *))   {}

/*
 *----------------------------------------------------------------------
 *
 * TkWmRemoveFromColormapWindows --
 *
 *	No-op on Wayland (colormap windows not applicable).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkWmRemoveFromColormapWindows(TCL_UNUSED(TkWindow *)) {}

/*
 *----------------------------------------------------------------------
 *
 * TkpUseWindowMenu --
 *
 *	No-op on Wayland (window menu not applicable).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkpUseWindowMenu(
		 TCL_UNUSED(TkWindow *),
		 TCL_UNUSED(int))
{
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetSystemDefault --
 *
 *	Get system default values for Tk options.
 *
 * Results:
 *	Returns a string constant for the requested default, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
TkpGetSystemDefault(
		    TCL_UNUSED(Tk_Window),
		    const char *dbClass,
		    const char *dbName)
{
    static const struct { const char *cls; const char *name; const char *val; }
    defaults[] = {
        {"*font",             "*Font",             "Sans 10"},
        {"*background",       "*Background",       "white"},
        {"*foreground",       "*Foreground",       "black"},
        {"*selectBackground", "*SelectBackground", "#000080"},
        {"*selectForeground", "*SelectForeground", "white"},
        {NULL, NULL, NULL}
    };
    int i;
    for (i = 0; defaults[i].cls != NULL; i++) {
        if (strcmp(dbClass, defaults[i].cls) == 0
	    && strcmp(dbName,  defaults[i].name) == 0) {
            return defaults[i].val;
        }
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_WmObjCmd –
 *
 *	Implementation of the "wm" Tcl command.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Depends on subcommand.
 *
 *----------------------------------------------------------------------
 */

int
Tk_WmObjCmd(
	    ClientData  clientData,
	    Tcl_Interp *interp,
	    Tcl_Size    objc,
	    Tcl_Obj *const objv[])
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
    int        index;
    Tcl_Size   length;
    const char *argv1;
    TkWindow   *winPtr;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option window ?arg ...?");
        return TCL_ERROR;
    }

    argv1 = Tcl_GetStringFromObj(objv[1], &length);
    if ((argv1[0] == '.') && (length > 1)) {
        winPtr = (TkWindow *)Tk_NameToWindow(interp, argv1, tkwin);
        if (winPtr == NULL) return TCL_ERROR;
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
        objc -= 3; objv += 3;
    } else {
        if (Tcl_GetIndexFromObjStruct(interp, objv[1], optionStrings,
				      sizeof(char *), "option", 0, &index) != TCL_OK) {
            return TCL_ERROR;
        }
        if (objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "window ?arg ...?");
            return TCL_ERROR;
        }
        winPtr = (TkWindow *)Tk_NameToWindow(
					     interp, Tcl_GetString(objv[2]), tkwin);
        if (winPtr == NULL) return TCL_ERROR;
        if (!(winPtr->flags & TK_TOP_LEVEL)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(
						   "window \"%s\" isn't a top-level window",
						   Tcl_GetString(objv[2])));
            Tcl_SetErrorCode(interp, "TK", "LOOKUP", "TOPLEVEL",
			     Tcl_GetString(objv[2]), NULL);
            return TCL_ERROR;
        }
        objc -= 3; objv += 3;
    }

    switch ((enum options)index) {
    case WMOPT_ASPECT:          return WmAspectCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_ATTRIBUTES:      return WmAttributesCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_CLIENT:          return WmClientCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_COLORMAPWINDOWS: return WmColormapwindowsCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_COMMAND:         return WmCommandCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_DEICONIFY:       return WmDeiconifyCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_FOCUSMODEL:      return WmFocusmodelCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_FORGET:          return WmForgetCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_FRAME:           return WmFrameCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_GEOMETRY:        return WmGeometryCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_GRID:            return WmGridCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_GROUP:           return WmGroupCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_ICONBADGE:       return WmIconbadgeCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_ICONBITMAP:      return WmIconbitmapCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_ICONIFY:         return WmIconifyCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_ICONMASK:        return WmIconmaskCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_ICONNAME:        return WmIconnameCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_ICONPHOTO:       return WmIconphotoCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_ICONPOSITION:    return WmIconpositionCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_ICONWINDOW:      return WmIconwindowCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_MANAGE:          return WmManageCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_MAXSIZE:         return WmMaxsizeCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_MINSIZE:         return WmMinsizeCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_OVERRIDEREDIRECT:return WmOverrideredirectCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_POSITIONFROM:    return WmPositionfromCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_PROTOCOL:        return WmProtocolCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_RESIZABLE:       return WmResizableCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_SIZEFROM:        return WmSizefromCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_STACKORDER:      return WmStackorderCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_STATE:           return WmStateCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_TITLE:           return WmTitleCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_TRANSIENT:       return WmTransientCmd(tkwin,winPtr,interp,objc,objv);
    case WMOPT_WITHDRAW:        return WmWithdrawCmd(tkwin,winPtr,interp,objc,objv);
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * wm sub-command implementations
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * WmAspectCmd --
 *
 *	Implements the "wm aspect" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Updates window aspect ratio hints.
 *
 *----------------------------------------------------------------------
 */

static int
WmAspectCmd(
	    TCL_UNUSED(Tk_Window),
	    TkWindow   *winPtr,
	    Tcl_Interp *interp,
	    int         objc,
	    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    int n1, n2, d1, d2;

    if (objc != 0 && objc != 4) {
        Tcl_WrongNumArgs(interp, 0, objv,
			 "pathName aspect ?minNumer minDenom maxNumer maxDenom?");
        return TCL_ERROR;
    }
    if (objc == 0) {
        if (wmPtr->sizeHintsFlags & WM_PAspect) {
            Tcl_Obj *r[4];
            r[0] = Tcl_NewIntObj(wmPtr->minAspect.x);
            r[1] = Tcl_NewIntObj(wmPtr->minAspect.y);
            r[2] = Tcl_NewIntObj(wmPtr->maxAspect.x);
            r[3] = Tcl_NewIntObj(wmPtr->maxAspect.y);
            Tcl_SetObjResult(interp, Tcl_NewListObj(4, r));
        }
        return TCL_OK;
    }
    if (*Tcl_GetString(objv[0]) == '\0') {
        wmPtr->sizeHintsFlags &= ~WM_PAspect;
    } else {
        if (Tcl_GetIntFromObj(interp,objv[0],&n1) != TCL_OK
	    || Tcl_GetIntFromObj(interp,objv[1],&d1) != TCL_OK
	    || Tcl_GetIntFromObj(interp,objv[2],&n2) != TCL_OK
	    || Tcl_GetIntFromObj(interp,objv[3],&d2) != TCL_OK) {
            return TCL_ERROR;
        }
        if (n1<=0||d1<=0||n2<=0||d2<=0) {
            Tcl_SetObjResult(interp,
			     Tcl_NewStringObj("aspect ratio values must be positive integers",-1));
            Tcl_SetErrorCode(interp,"TK","WM","ASPECT","POSITIVE",NULL);
            return TCL_ERROR;
        }
        wmPtr->minAspect.x = n1; wmPtr->minAspect.y = d1;
        wmPtr->maxAspect.x = n2; wmPtr->maxAspect.y = d2;
        wmPtr->sizeHintsFlags |= WM_PAspect;
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
 *	Implements the "wm attributes" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Updates window attributes (alpha, topmost, zoomed, fullscreen).
 *
 *----------------------------------------------------------------------
 */

static int
WmAttributesCmd(
    TCL_UNUSED(Tk_Window),
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    int i;

    /* No arguments: return all attributes as list. */
    if (objc == 1) {   /* wm attributes $win */
        Tcl_Obj *result = Tcl_NewListObj(0, NULL);

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

    /* One argument: query single attribute. */
    if (objc == 2) {
        int attribute;
        if (Tcl_GetIndexFromObjStruct(interp, objv[1], WmAttributeNames,
                                      sizeof(char *), "attribute", 0, &attribute) != TCL_OK) {
            return TCL_ERROR;
        }

        switch ((WmAttribute)attribute) {
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

    /* Odd number of arguments after window → error. */
    if (objc % 2 == 0) {
        Tcl_WrongNumArgs(interp, 1, objv, "pathName ?-attribute value ...?");
        return TCL_ERROR;
    }

    /* Set one or more attributes. */
    GLFWwindow *glfwWindow = TkGlfwGetGLFWWindow((Tk_Window)winPtr);
    TkWaylandDecoration *decor = TkWaylandGetDecoration(winPtr);

    for (i = 1; i < objc; i += 2) {
        int attribute;
        if (Tcl_GetIndexFromObjStruct(interp, objv[i], WmAttributeNames,
                                      sizeof(char *), "attribute", 0, &attribute) != TCL_OK) {
            return TCL_ERROR;
        }

        switch ((WmAttribute)attribute) {
            case WMATT_ALPHA: {
                double d;
                if (Tcl_GetDoubleFromObj(interp, objv[i+1], &d) != TCL_OK) {
                    return TCL_ERROR;
                }
                d = (d < 0.0) ? 0.0 : (d > 1.0) ? 1.0 : d;
                wmPtr->reqState.alpha = wmPtr->attributes.alpha = d;
                if (glfwWindow != NULL) {
                    glfwSetWindowOpacity(glfwWindow, (float)d);
                }
                /* TODO: Wayland opacity if supported via decor or compositor hint */
                break;
            }

            case WMATT_TOPMOST: {
                int b;
                if (Tcl_GetBooleanFromObj(interp, objv[i+1], &b) != TCL_OK) {
                    return TCL_ERROR;
                }
                wmPtr->reqState.topmost = wmPtr->attributes.topmost = b;
                if (glfwWindow != NULL) {
                    glfwSetWindowAttrib(glfwWindow, GLFW_FLOATING, b ? GLFW_TRUE : GLFW_FALSE);
                }

                break;
            }

            case WMATT_ZOOMED: {
                int zoomed;
                if (Tcl_GetBooleanFromObj(interp, objv[i+1], &zoomed) != TCL_OK) {
                    return TCL_ERROR;
                }
                wmPtr->reqState.zoomed = wmPtr->attributes.zoomed = zoomed;

                if (glfwWindow != NULL) {
                    if (zoomed) {
                        glfwMaximizeWindow(glfwWindow);
                    } else {
                        glfwRestoreWindow(glfwWindow);
                    }
                }
                if (decor != NULL) {
                    TkWaylandSetWindowMaximized(decor, zoomed);
                }

                /* Optional: if your WM needs it, you could also call TkpWmSetState()
                 * or similar here — but usually GLFW + Wayland decoration is enough */
                break;
            }

            case WMATT_FULLSCREEN: {
                int b;
                if (Tcl_GetBooleanFromObj(interp, objv[i+1], &b) != TCL_OK) {
                    return TCL_ERROR;
                }
                wmPtr->reqState.fullscreen = wmPtr->attributes.fullscreen = b;
                ApplyFullscreenState(winPtr);
                break;
            }

            case WMATT_TYPE:
                /* Usually ignored / placeholder */
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
 *	Implements the "wm client" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Stores the client machine name.
 *
 *----------------------------------------------------------------------
 */

static int
WmClientCmd(
	    TCL_UNUSED(Tk_Window),
	    TkWindow   *winPtr,
	    Tcl_Interp *interp,
	    int         objc,
	    Tcl_Obj *const objv[])
{
    WmInfo     *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    const char *name;

    if (objc > 1) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName client ?name?"); return TCL_ERROR;
    }
    if (objc == 0) {
        if (wmPtr->clientMachine)
            Tcl_SetObjResult(interp,Tcl_NewStringObj(wmPtr->clientMachine,-1));
        return TCL_OK;
    }
    name = Tcl_GetString(objv[0]);
    if (wmPtr->clientMachine) ckfree(wmPtr->clientMachine);
    wmPtr->clientMachine = ckalloc(strlen(name)+1);
    strcpy(wmPtr->clientMachine, name);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmColormapwindowsCmd --
 *
 *	Implements the "wm colormapwindows" subcommand (no-op on Wayland).
 *
 * Results:
 *	Standard Tcl result (empty list).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
WmColormapwindowsCmd(
		     TCL_UNUSED(Tk_Window),
		     TCL_UNUSED(TkWindow *),
		     Tcl_Interp *interp,
		     TCL_UNUSED(int),
		     TCL_UNUSED(Tcl_Obj *const *))
{
    Tcl_SetObjResult(interp, Tcl_NewObj());
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmCommandCmd --
 *
 *	Implements the "wm command" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Stores the command to restart the application.
 *
 *----------------------------------------------------------------------
 */

static int
WmCommandCmd(
	     TCL_UNUSED(Tk_Window),
	     TkWindow   *winPtr,
	     Tcl_Interp *interp,
	     int         objc,
	     Tcl_Obj *const objv[])
{
    WmInfo   *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    Tcl_Obj **elems;
    Tcl_Size  count, j;

    if (objc > 1) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName command ?value?"); return TCL_ERROR;
    }
    if (objc == 0) {
        if (wmPtr->cmdArgc > 0) {
            Tcl_Obj *list = Tcl_NewObj();
            for (j = 0; j < wmPtr->cmdArgc; j++) {
                Tcl_ListObjAppendElement(NULL,list,wmPtr->cmdArgv[j]);
            }
            Tcl_SetObjResult(interp, list);
        }
        return TCL_OK;
    }

    /* Release old command. */
    if (wmPtr->cmdArgv != NULL) {
        for (j = 0; j < wmPtr->cmdArgc; j++) {
            Tcl_DecrRefCount(wmPtr->cmdArgv[j]);
        }
        ckfree((char *)wmPtr->cmdArgv);
        wmPtr->cmdArgv = NULL;
        wmPtr->cmdArgc = 0;
    }

    /* Parse new command list. */
    if (Tcl_ListObjGetElements(interp, objv[0], &count, &elems) != TCL_OK) {
        return TCL_ERROR;
    }

    wmPtr->cmdArgc  = count;
    wmPtr->cmdArgv  = (Tcl_Obj **)ckalloc(count * sizeof(Tcl_Obj *));
    for (j = 0; j < count; j++) {
        wmPtr->cmdArgv[j] = elems[j];
        Tcl_IncrRefCount(elems[j]);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmDeiconifyCmd --
 *
 *	Implements the "wm deiconify" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Maps the window (makes it visible).
 *
 *----------------------------------------------------------------------
 */

static int
WmDeiconifyCmd(
	       TCL_UNUSED(Tk_Window),
	       TkWindow   *winPtr,
	       Tcl_Interp *interp,
	       int         objc,
	       Tcl_Obj *const objv[])
{
    if (objc != 0) {
        Tcl_WrongNumArgs(interp, 0, objv, "pathName deiconify");
        return TCL_ERROR;
    }
    
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;

    wmPtr->withdrawn = 0;
    wmPtr->initialState = NormalState;

    TkWmMapWindow(winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmFocusmodelCmd --
 *
 *	Implements the "wm focusmodel" subcommand.
 *
 * Results:
 *	Standard Tcl result (always "passive" on Wayland).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
WmFocusmodelCmd(
		TCL_UNUSED(Tk_Window),
		TCL_UNUSED(TkWindow *),
		Tcl_Interp *interp,
		int         objc,
		TCL_UNUSED(Tcl_Obj *const *))
{
    if (objc == 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("passive",-1));
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmForgetCmd --
 *
 *	Implements the "wm forget" subcommand (no-op on Wayland).
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
WmForgetCmd(
	    TCL_UNUSED(Tk_Window),
	    TCL_UNUSED(TkWindow *),
	    Tcl_Interp *interp,
	    int         objc,
	    Tcl_Obj *const objv[])
{
    if (objc != 0) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName forget"); return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmFrameCmd --
 *
 *	Implements the "wm frame" subcommand (returns dummy window ID).
 *
 * Results:
 *	Standard Tcl result (returns "0x0").
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
WmFrameCmd(
	   TCL_UNUSED(Tk_Window),
	   TCL_UNUSED(TkWindow *),
	   Tcl_Interp *interp,
	   int         objc,
	   Tcl_Obj *const objv[])
{
    if (objc != 0) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName frame"); return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj("0x0",-1));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmGeometryCmd --
 *
 *	Implements the "wm geometry" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Updates window geometry if new geometry is provided.
 *
 *----------------------------------------------------------------------
 */

static int
WmGeometryCmd(
	      TCL_UNUSED(Tk_Window),
	      TkWindow   *winPtr,
	      Tcl_Interp *interp,
	      int         objc,
	      Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    char    buf[64];

    if (objc > 1) {
        Tcl_WrongNumArgs(interp, 0, objv, "pathName geometry ?newGeometry?");
        return TCL_ERROR;
    }
    
    /* Return current geometry. */
    if (objc == 0) {
        int width, height;
        
        if (wmPtr->glfwWindow != NULL && !(wmPtr->flags & WM_NEVER_MAPPED)) {
            glfwGetWindowSize(wmPtr->glfwWindow, &width, &height);
        } else {
            width = (wmPtr->width >= 0) ? wmPtr->width : winPtr->reqWidth;
            height = (wmPtr->height >= 0) ? wmPtr->height : winPtr->reqHeight;
        }
        
        snprintf(buf, sizeof(buf), "%dx%d+%d+%d",
		 width, height,
		 wmPtr->x, wmPtr->y);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, -1));
        return TCL_OK;
    }
    
    /* Handle empty string - reset to default. */
    if (*Tcl_GetString(objv[0]) == '\0') {
        wmPtr->width = wmPtr->height = -1;
        
        if (wmPtr->flags & WM_UPDATE_PENDING) {
            Tcl_CancelIdleCall(UpdateGeometryInfo, (ClientData)winPtr);
            wmPtr->flags &= ~WM_UPDATE_PENDING;
        }
        
        if (wmPtr->glfwWindow != NULL && !(wmPtr->flags & WM_NEVER_MAPPED)) {
            UpdateGeometryInfo((ClientData)winPtr);
            TkGlfwProcessEvents();
        }
        
        return TCL_OK;
    }
    
    /* Parse and apply new geometry. */
    if (ParseGeometry(interp, Tcl_GetString(objv[0]), winPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    
    /* Immediately set GLFW window size and position. */
    if (wmPtr->glfwWindow != NULL && !(wmPtr->flags & WM_NEVER_MAPPED)) {
        /* Set size only if positive values were provided */
        if (wmPtr->width > 0 && wmPtr->height > 0) {
            glfwSetWindowSize(wmPtr->glfwWindow, wmPtr->width, wmPtr->height);
        }
        glfwSetWindowPos(wmPtr->glfwWindow, wmPtr->x, wmPtr->y);
    }
    
    /* Force immediate update instead of waiting for idle. */
    if (wmPtr->glfwWindow != NULL && !(wmPtr->flags & WM_NEVER_MAPPED)) {
        /* Cancel any pending idle callback */
        if (wmPtr->flags & WM_UPDATE_PENDING) {
            Tcl_CancelIdleCall(UpdateGeometryInfo, (ClientData)winPtr);
            wmPtr->flags &= ~WM_UPDATE_PENDING;
        }
        
        /* Update internal Tk/GLFW state. */
        UpdateGeometryInfo((ClientData)winPtr);
        
        /* Process events to ensure callback fires before command returns */
        TkGlfwProcessEvents();
        
        /* Verify the change actually took effect. */
        int newWidth, newHeight;
        glfwGetWindowSize(wmPtr->glfwWindow, &newWidth, &newHeight);
        
        /* If the size didn't change (e.g., constrained by min/max), update wmPtr. */
        if (wmPtr->width > 0 && wmPtr->width != newWidth) {
            wmPtr->width = newWidth;
        }
        if (wmPtr->height > 0 && wmPtr->height != newHeight) {
            wmPtr->height = newHeight;
        }
        
        /* Update Tk's changes structure. */
        winPtr->changes.width = newWidth;
        winPtr->changes.height = newHeight;
    }
    
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmGridCmd --
 *
 *	Implements the "wm grid" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Updates grid-based resize hints.
 *
 *----------------------------------------------------------------------
 */

static int
WmGridCmd(
	  TCL_UNUSED(Tk_Window),
	  TkWindow   *winPtr,
	  Tcl_Interp *interp,
	  int         objc,
	  Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    int     rw, rh, wi, hi;

    if (objc != 0 && objc != 4) {
        Tcl_WrongNumArgs(interp,0,objv,
			 "pathName grid ?baseWidth baseHeight widthInc heightInc?");
        return TCL_ERROR;
    }
    if (objc == 0) {
        if (wmPtr->sizeHintsFlags & WM_PBaseSize) {
            Tcl_Obj *r[4];
            r[0]=Tcl_NewIntObj(wmPtr->reqGridWidth);
            r[1]=Tcl_NewIntObj(wmPtr->reqGridHeight);
            r[2]=Tcl_NewIntObj(wmPtr->widthInc);
            r[3]=Tcl_NewIntObj(wmPtr->heightInc);
            Tcl_SetObjResult(interp,Tcl_NewListObj(4,r));
        }
        return TCL_OK;
    }
    if (*Tcl_GetString(objv[0]) == '\0') {
        wmPtr->sizeHintsFlags &= ~(WM_PBaseSize|WM_PResizeInc);
        wmPtr->widthInc = wmPtr->heightInc = 1;
        wmPtr->reqGridWidth = wmPtr->reqGridHeight = 0;
    } else {
        if (Tcl_GetIntFromObj(interp,objv[0],&rw) != TCL_OK
	    || Tcl_GetIntFromObj(interp,objv[1],&rh) != TCL_OK
	    || Tcl_GetIntFromObj(interp,objv[2],&wi) != TCL_OK
	    || Tcl_GetIntFromObj(interp,objv[3],&hi) != TCL_OK) return TCL_ERROR;
        if (rw < 0) rw = winPtr->reqWidth  + winPtr->internalBorderLeft*2;
        if (rh < 0) rh = winPtr->reqHeight + winPtr->internalBorderTop*2;
        if (wi<=0||hi<=0) {
            Tcl_SetObjResult(interp,
			     Tcl_NewStringObj("grid increments must be positive integers",-1));
            Tcl_SetErrorCode(interp,"TK","WM","GRID","POSITIVE",NULL);
            return TCL_ERROR;
        }
        wmPtr->sizeHintsFlags |= WM_PBaseSize|WM_PResizeInc;
        wmPtr->reqGridWidth=rw; wmPtr->reqGridHeight=rh;
        wmPtr->widthInc=wi;    wmPtr->heightInc=hi;
    }
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr,winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmGroupCmd --
 *
 *	Implements the "wm group" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Stores the group leader window name.
 *
 *----------------------------------------------------------------------
 */

static int
WmGroupCmd(
	   TCL_UNUSED(Tk_Window),
	   TkWindow   *winPtr,
	   Tcl_Interp *interp,
	   int         objc,
	   Tcl_Obj *const objv[])
{
    WmInfo     *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    const char *path;

    if (objc > 1) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName group ?pathName?"); return TCL_ERROR;
    }
    if (objc == 0) {
        if (wmPtr->leaderName)
            Tcl_SetObjResult(interp,Tcl_NewStringObj(wmPtr->leaderName,-1));
        return TCL_OK;
    }
    path = Tcl_GetString(objv[0]);
    if (wmPtr->leaderName) ckfree(wmPtr->leaderName);
    wmPtr->leaderName = ckalloc(strlen(path)+1);
    strcpy(wmPtr->leaderName, path);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconbadgeCmd --
 *
 *	Implements the "wm iconbadge" subcommand (no-op on Wayland).
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconbadgeCmd(
	       TCL_UNUSED(Tk_Window),
	       TCL_UNUSED(TkWindow *),
	       Tcl_Interp *interp,
	       int   objc,
	       Tcl_Obj *const objv[])
{
    if (objc < 4) {
        Tcl_WrongNumArgs(interp,2,objv,"window badge"); return TCL_ERROR;
    }
    return TCL_OK; /* No-op on Wayland. */
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconbitmapCmd --
 *
 *	Implements the "wm iconbitmap" subcommand (no-op on Wayland).
 *
 * Results:
 *	Standard Tcl result (always TCL_OK).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconbitmapCmd(
		TCL_UNUSED(Tk_Window),
		TCL_UNUSED(TkWindow *),
		TCL_UNUSED(Tcl_Interp *),
		TCL_UNUSED(int),
		TCL_UNUSED(Tcl_Obj *const *))
{
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconifyCmd --
 *
 *	Implements the "wm iconify" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Iconifies (minimizes) the window.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconifyCmd(
	     TCL_UNUSED(Tk_Window),
	     TkWindow   *winPtr,
	     Tcl_Interp *interp,
	     int         objc,
	     Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;

    if (objc != 0) {
        Tcl_WrongNumArgs(interp, 0, objv, "pathName iconify");
        return TCL_ERROR;
    }

    /* Update Tk's internal state to IconicState. */
    TkpWmSetState(winPtr, IconicState);

    /* If the window is mapped and has a GLFW window, actually iconify it. */
    if ((winPtr->flags & TK_MAPPED) && wmPtr->glfwWindow != NULL) {
        glfwIconifyWindow(wmPtr->glfwWindow);

        /* Optionally, update Tk's mapped flag if iconify implicitly hides the window. */
        winPtr->flags &= ~TK_MAPPED;  /* Some window managers unmap on iconify. */
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconmaskCmd --
 *
 *	Implements the "wm iconmask" subcommand (no-op on Wayland).
 *
 * Results:
 *	Standard Tcl result (always TCL_OK).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconmaskCmd(
	      TCL_UNUSED(Tk_Window), 
	      TCL_UNUSED(TkWindow *),
	      TCL_UNUSED(Tcl_Interp *), 
	      TCL_UNUSED(int),
	      TCL_UNUSED(Tcl_Obj *const *))
{ return TCL_OK; }

/*
 *----------------------------------------------------------------------
 *
 * WmIconnameCmd --
 *
 *	Implements the "wm iconname" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Stores the icon name (not currently used on Wayland).
 *
 *----------------------------------------------------------------------
 */

static int
WmIconnameCmd(
	      TCL_UNUSED(Tk_Window),
	      TkWindow   *winPtr,
	      Tcl_Interp *interp,
	      int         objc,
	      Tcl_Obj *const objv[])
{
    WmInfo     *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    const char *name;
    Tcl_Size    len;

    if (objc > 1) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName iconname ?newName?"); return TCL_ERROR;
    }
    if (objc == 0) {
        if (wmPtr->iconName)
            Tcl_SetObjResult(interp,Tcl_NewStringObj(wmPtr->iconName,-1));
        return TCL_OK;
    }
    name = Tcl_GetStringFromObj(objv[0],&len);
    if (wmPtr->iconName) ckfree(wmPtr->iconName);
    wmPtr->iconName = ckalloc(len+1);
    strcpy(wmPtr->iconName, name);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconphotoCmd --
 *
 *	Implements the "wm iconphoto" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Converts photo images to GLFW icons and sets the window icon.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconphotoCmd(
	       TCL_UNUSED(Tk_Window),
	       TkWindow   *winPtr,
	       Tcl_Interp *interp,
	       int         objc,
	       Tcl_Obj *const objv[])
{
    WmInfo          *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    Tk_PhotoHandle   photo;
    int              i;

    if (objc < 1) {
        Tcl_WrongNumArgs(interp,0,objv,
			 "pathName iconphoto ?-default? image ?image ...?");
        return TCL_ERROR;
    }
    if (strcmp(Tcl_GetString(objv[0]),"-default") == 0) { objv++; objc--; }
    if (objc < 1) {
        Tcl_WrongNumArgs(interp,0,objv,
			 "pathName iconphoto ?-default? image ?image ...?");
        return TCL_ERROR;
    }

    /* Clear old icons. */
    if (wmPtr->glfwIcon != NULL) {
        for (i = 0; i < wmPtr->glfwIconCount; i++) {
            if (wmPtr->glfwIcon[i].pixels)
                ckfree((char *)wmPtr->glfwIcon[i].pixels);
        }
        ckfree((char *)wmPtr->glfwIcon);
        wmPtr->glfwIcon = NULL; wmPtr->glfwIconCount = 0;
    }

    for (i = 0; i < objc; i++) {
        photo = Tk_FindPhoto(interp, Tcl_GetString(objv[i]));
        if (photo == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(
						   "can't use \"%s\" as iconphoto: not a photo image",
						   Tcl_GetString(objv[i])));
            Tcl_SetErrorCode(interp,"TK","WM","ICONPHOTO","PHOTO",NULL);
            return TCL_ERROR;
        }
        ConvertPhotoToGlfwIcon(winPtr, photo);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconpositionCmd --
 *
 *	Implements the "wm iconposition" subcommand (no-op on Wayland).
 *
 * Results:
 *	Standard Tcl result (always TCL_OK).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconpositionCmd(
		  TCL_UNUSED(Tk_Window), 
		  TCL_UNUSED(TkWindow *),
		  TCL_UNUSED(Tcl_Interp *), 
		  TCL_UNUSED(int),
		  TCL_UNUSED(Tcl_Obj *const *))
{ return TCL_OK; }

/*
 *----------------------------------------------------------------------
 *
 * WmIconwindowCmd --
 *
 *	Implements the "wm iconwindow" subcommand (no-op on Wayland).
 *
 * Results:
 *	Standard Tcl result (always TCL_OK).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconwindowCmd(
		TCL_UNUSED(Tk_Window), 
		TCL_UNUSED(TkWindow *),
		TCL_UNUSED(Tcl_Interp *), 
		TCL_UNUSED(int),
		TCL_UNUSED(Tcl_Obj *const *))
{ return TCL_OK; }

/*
 *----------------------------------------------------------------------
 *
 * WmManageCmd --
 *
 *	Implements the "wm manage" subcommand (no-op on Wayland).
 *
 * Results:
 *	Standard Tcl result (always TCL_OK).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
WmManageCmd(
	    TCL_UNUSED(Tk_Window), 
	    TCL_UNUSED(TkWindow *),
	    Tcl_Interp *interp, 
	    int objc, 
	    Tcl_Obj *const objv[])
{
    if (objc != 0) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName manage"); return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmMaxsizeCmd --
 *
 *	Implements the "wm maxsize" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Updates maximum size hints.
 *
 *----------------------------------------------------------------------
 */

static int
WmMaxsizeCmd(
	     TCL_UNUSED(Tk_Window),
	     TkWindow   *winPtr,
	     Tcl_Interp *interp,
	     int         objc,
	     Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    int     w, h;

    if (objc != 0 && objc != 2) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName maxsize ?width height?");
        return TCL_ERROR;
    }
    if (objc == 0) {
        Tcl_Obj *r[2];
        r[0]=Tcl_NewIntObj(wmPtr->maxWidth); r[1]=Tcl_NewIntObj(wmPtr->maxHeight);
        Tcl_SetObjResult(interp,Tcl_NewListObj(2,r)); return TCL_OK;
    }
    if (Tcl_GetIntFromObj(interp,objv[0],&w)!=TCL_OK
	|| Tcl_GetIntFromObj(interp,objv[1],&h)!=TCL_OK) return TCL_ERROR;
    wmPtr->maxWidth=w; wmPtr->maxHeight=h;
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr,winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmMinsizeCmd --
 *
 *	Implements the "wm minsize" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Updates minimum size hints.
 *
 *----------------------------------------------------------------------
 */

static int
WmMinsizeCmd(
	     TCL_UNUSED(Tk_Window),
	     TkWindow   *winPtr,
	     Tcl_Interp *interp,
	     int         objc,
	     Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    int     w, h;

    if (objc != 0 && objc != 2) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName minsize ?width height?");
        return TCL_ERROR;
    }
    if (objc == 0) {
        Tcl_Obj *r[2];
        r[0]=Tcl_NewIntObj(wmPtr->minWidth); r[1]=Tcl_NewIntObj(wmPtr->minHeight);
        Tcl_SetObjResult(interp,Tcl_NewListObj(2,r)); return TCL_OK;
    }
    if (Tcl_GetIntFromObj(interp,objv[0],&w)!=TCL_OK
	|| Tcl_GetIntFromObj(interp,objv[1],&h)!=TCL_OK) return TCL_ERROR;
    wmPtr->minWidth=w; wmPtr->minHeight=h;
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr,winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmOverrideredirectCmd --
 *
 *	Implements the "wm overrideredirect" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Updates override-redirect (undecorated) state.
 *
 *----------------------------------------------------------------------
 */

static int
WmOverrideredirectCmd(
		      Tk_Window   tkwin,
		      TkWindow   *winPtr,
		      Tcl_Interp *interp,
		      int         objc,
		      Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    int     value;

    if (objc > 1) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName overrideredirect ?boolean?");
        return TCL_ERROR;
    }
    if (objc == 0) {
        Tcl_SetObjResult(interp,
			 Tcl_NewBooleanObj(Tk_Attributes(tkwin)->override_redirect));
        return TCL_OK;
    }
    if (Tcl_GetBooleanFromObj(interp,objv[0],&value) != TCL_OK) return TCL_ERROR;
    if (value) {
        wmPtr->flags |= WM_WIDTH_NOT_RESIZABLE|WM_HEIGHT_NOT_RESIZABLE;
    } else {
        wmPtr->flags &= ~(WM_WIDTH_NOT_RESIZABLE|WM_HEIGHT_NOT_RESIZABLE);
    }
    if (wmPtr->glfwWindow) {
        glfwSetWindowAttrib(wmPtr->glfwWindow, GLFW_DECORATED,
                            value ? GLFW_FALSE : GLFW_TRUE);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmPositionfromCmd --
 *
 *	Implements the "wm positionfrom" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Updates position source hint.
 *
 *----------------------------------------------------------------------
 */

static int
WmPositionfromCmd(
		  TCL_UNUSED(Tk_Window),
		  TkWindow   *winPtr,
		  Tcl_Interp *interp,
		  int         objc,
		  Tcl_Obj *const objv[])
{
    WmInfo     *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    static const char *const src[] = { "program","user",NULL };
    int idx;

    if (objc > 1) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName positionfrom ?user|program?");
        return TCL_ERROR;
    }
    if (objc == 0) {
        if      (wmPtr->sizeHintsFlags & WM_USPosition)
            Tcl_SetObjResult(interp,Tcl_NewStringObj("user",-1));
        else if (wmPtr->sizeHintsFlags & WM_PPosition)
            Tcl_SetObjResult(interp,Tcl_NewStringObj("program",-1));
        return TCL_OK;
    }
    if (*Tcl_GetString(objv[0]) == '\0') {
        wmPtr->sizeHintsFlags &= ~(WM_USPosition|WM_PPosition);
    } else {
        if (Tcl_GetIndexFromObjStruct(interp,objv[0],src,sizeof(char *),"argument",0,&idx)!=TCL_OK)
            return TCL_ERROR;
        if (idx==0) { wmPtr->sizeHintsFlags&=~WM_USPosition; wmPtr->sizeHintsFlags|=WM_PPosition; }
        else        { wmPtr->sizeHintsFlags&=~WM_PPosition;  wmPtr->sizeHintsFlags|=WM_USPosition; }
    }
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr,winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmProtocolCmd --
 *
 *	Implements the "wm protocol" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Registers or removes protocol handlers.
 *
 *----------------------------------------------------------------------
 */

static int
WmProtocolCmd(
	      TCL_UNUSED(Tk_Window),
	      TkWindow   *winPtr,
	      Tcl_Interp *interp,
	      int         objc,
	      Tcl_Obj *const objv[])
{
    WmInfo          *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    ProtocolHandler *protPtr, *prevPtr;
    const char      *cmd;
    Tcl_Size         cmdLength;
    int              protocol;

    if (objc == 0) {
        Tcl_Obj *result = Tcl_NewObj();
        for (protPtr=wmPtr->protPtr; protPtr; protPtr=protPtr->nextPtr) {
            const char *name = NULL;
            if      (protPtr->protocol==WM_DELETE_WINDOW) name="WM_DELETE_WINDOW";
            else if (protPtr->protocol==WM_TAKE_FOCUS)    name="WM_TAKE_FOCUS";
            else if (protPtr->protocol==WM_SAVE_YOURSELF) name="WM_SAVE_YOURSELF";
            if (name) Tcl_ListObjAppendElement(NULL,result,Tcl_NewStringObj(name,-1));
        }
        Tcl_SetObjResult(interp,result);
        return TCL_OK;
    }

    cmd = Tcl_GetString(objv[0]);
    if      (strcmp(cmd,"WM_DELETE_WINDOW")==0) protocol=WM_DELETE_WINDOW;
    else if (strcmp(cmd,"WM_TAKE_FOCUS")==0)    protocol=WM_TAKE_FOCUS;
    else if (strcmp(cmd,"WM_SAVE_YOURSELF")==0) protocol=WM_SAVE_YOURSELF;
    else {
        Tcl_SetObjResult(interp,Tcl_ObjPrintf("unknown protocol \"%s\"",cmd));
        Tcl_SetErrorCode(interp,"TK","WM","PROTOCOL","UNKNOWN",NULL);
        return TCL_ERROR;
    }

    if (objc == 1) {
        for (protPtr=wmPtr->protPtr; protPtr; protPtr=protPtr->nextPtr) {
            if (protPtr->protocol==protocol) {
                Tcl_SetObjResult(interp,Tcl_NewStringObj(protPtr->command,-1));
                return TCL_OK;
            }
        }
        return TCL_OK;
    }

    cmd = Tcl_GetStringFromObj(objv[1],&cmdLength);
    if (cmdLength == 0) {
        for (protPtr=wmPtr->protPtr,prevPtr=NULL; protPtr;
             prevPtr=protPtr, protPtr=protPtr->nextPtr) {
            if (protPtr->protocol==protocol) {
                if (prevPtr) prevPtr->nextPtr=protPtr->nextPtr;
                else         wmPtr->protPtr  =protPtr->nextPtr;
                Tcl_EventuallyFree((ClientData)protPtr,TCL_DYNAMIC);
                break;
            }
        }
    } else {
        for (protPtr=wmPtr->protPtr,prevPtr=NULL; protPtr;
             prevPtr=protPtr, protPtr=protPtr->nextPtr) {
            if (protPtr->protocol==protocol) break;
        }
        if (protPtr==NULL) {
            protPtr=(ProtocolHandler *)ckalloc(HANDLER_SIZE(cmdLength));
            protPtr->protocol=protocol;
            protPtr->nextPtr =wmPtr->protPtr;
            wmPtr->protPtr   =protPtr;
            protPtr->interp  =interp;
        } else {
            protPtr=(ProtocolHandler *)ckrealloc((char *)protPtr,
						 HANDLER_SIZE(cmdLength));
            if (prevPtr) prevPtr->nextPtr=protPtr;
            else         wmPtr->protPtr  =protPtr;
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
 *	Implements the "wm resizable" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Updates resizability flags.
 *
 *----------------------------------------------------------------------
 */

static int
WmResizableCmd(
	       TCL_UNUSED(Tk_Window),
	       TkWindow   *winPtr,
	       Tcl_Interp *interp,
	       int         objc,
	       Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    int     w, h;

    if (objc != 0 && objc != 2) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName resizable ?width height?");
        return TCL_ERROR;
    }
    if (objc == 0) {
        Tcl_Obj *r[2];
        r[0]=Tcl_NewBooleanObj(!(wmPtr->flags&WM_WIDTH_NOT_RESIZABLE));
        r[1]=Tcl_NewBooleanObj(!(wmPtr->flags&WM_HEIGHT_NOT_RESIZABLE));
        Tcl_SetObjResult(interp,Tcl_NewListObj(2,r)); return TCL_OK;
    }
    if (Tcl_GetBooleanFromObj(interp,objv[0],&w)!=TCL_OK
	|| Tcl_GetBooleanFromObj(interp,objv[1],&h)!=TCL_OK) return TCL_ERROR;
    if (w) wmPtr->flags&=~WM_WIDTH_NOT_RESIZABLE;
    else   wmPtr->flags|= WM_WIDTH_NOT_RESIZABLE;
    if (h) wmPtr->flags&=~WM_HEIGHT_NOT_RESIZABLE;
    else   wmPtr->flags|= WM_HEIGHT_NOT_RESIZABLE;
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    if (wmPtr->glfwWindow)
        glfwSetWindowAttrib(wmPtr->glfwWindow,GLFW_RESIZABLE,
                            (w||h)?GLFW_TRUE:GLFW_FALSE);
    WmUpdateGeom(wmPtr,winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmSizefromCmd --
 *
 *	Implements the "wm sizefrom" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Updates size source hint.
 *
 *----------------------------------------------------------------------
 */

static int
WmSizefromCmd(
	      TCL_UNUSED(Tk_Window),
	      TkWindow   *winPtr,
	      Tcl_Interp *interp,
	      int         objc,
	      Tcl_Obj *const objv[])
{
    WmInfo     *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    static const char *const src[] = { "program","user",NULL };
    int idx;

    if (objc > 1) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName sizefrom ?user|program?");
        return TCL_ERROR;
    }
    if (objc == 0) {
        if      (wmPtr->sizeHintsFlags&WM_USSize) Tcl_SetObjResult(interp,Tcl_NewStringObj("user",-1));
        else if (wmPtr->sizeHintsFlags&WM_PSize)  Tcl_SetObjResult(interp,Tcl_NewStringObj("program",-1));
        return TCL_OK;
    }
    if (*Tcl_GetString(objv[0]) == '\0') {
        wmPtr->sizeHintsFlags &= ~(WM_USSize|WM_PSize);
    } else {
        if (Tcl_GetIndexFromObjStruct(interp,objv[0],src,sizeof(char *),"argument",0,&idx)!=TCL_OK)
            return TCL_ERROR;
        if (idx==0) { wmPtr->sizeHintsFlags&=~WM_USSize; wmPtr->sizeHintsFlags|=WM_PSize; }
        else        { wmPtr->sizeHintsFlags&=~WM_PSize;  wmPtr->sizeHintsFlags|=WM_USSize; }
    }
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr,winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmStackorderCmd --
 *
 *	Implements the "wm stackorder" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	None (returns placeholder result for isabove/isbelow).
 *
 *----------------------------------------------------------------------
 */

static int
WmStackorderCmd(
		TCL_UNUSED(Tk_Window),
		TkWindow   *winPtr,
		Tcl_Interp *interp,
		int         objc,
		Tcl_Obj *const objv[])
{
    TkWindow **windows, **wp;

    if (objc != 0 && objc != 2) {
        Tcl_WrongNumArgs(interp,0,objv,
			 "pathName stackorder ?isabove|isbelow window?");
        return TCL_ERROR;
    }
    if (objc == 0) {
        windows = TkWmStackorderToplevel(winPtr);
        if (windows != NULL) {
            Tcl_Obj *result = Tcl_NewObj();
            for (wp=windows; *wp; wp++)
                Tcl_ListObjAppendElement(NULL,result,
					 Tcl_NewStringObj((*wp)->pathName,-1));
            ckfree((char *)windows);
            Tcl_SetObjResult(interp,result);
        }
        return TCL_OK;
    }
    Tcl_SetObjResult(interp,Tcl_NewBooleanObj(0));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmStateCmd --
 *
 *	Implements the "wm state" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Changes window state (normal, iconic, withdrawn, zoomed).
 *
 *----------------------------------------------------------------------
 */

static int
WmStateCmd(
    TCL_UNUSED(Tk_Window),
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    static const char *const opts[] = {
        "normal", "iconic", "withdrawn", "icon", "zoomed", NULL
    };
    enum { OPT_NORMAL, OPT_ICONIC, OPT_WITHDRAWN, OPT_ICON, OPT_ZOOMED };
    int idx;

    /* Early argument check. */
    if (objc > 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "pathName ?state?");
        return TCL_ERROR;
    }

    /* Query current state. */
    if (objc == 1) {
        if (wmPtr->iconFor) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("icon", -1));
        }
        else if (wmPtr->withdrawn) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("withdrawn", -1));
        }
        else if (wmPtr->attributes.zoomed) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("zoomed", -1));
        }
        else if (Tk_IsMapped((Tk_Window)winPtr)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("normal", -1));
        }
        else {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("iconic", -1));
        }
        return TCL_OK;
    }

    /* Get requested state. */
    if (Tcl_GetIndexFromObjStruct(interp, objv[1], opts, sizeof(char *),
                                  "state", TCL_EXACT, &idx) != TCL_OK) {
        return TCL_ERROR;
    }

    /* Cannot change state of an icon-for window. */
    if (wmPtr->iconFor != NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf(
            "can't change state of %s: it is an icon for %s",
            Tk_PathName(winPtr), Tk_PathName(wmPtr->iconFor)));
        Tcl_SetErrorCode(interp, "TK", "WM", "STATE", "ICON", NULL);
        return TCL_ERROR;
    }

    /* Get platform-specific handles once. */
    GLFWwindow *glfwWindow = TkGlfwGetGLFWWindow((Tk_Window)winPtr);
    TkWaylandDecoration *decor = TkWaylandGetDecoration(winPtr);

    switch (idx) {
        case OPT_NORMAL:
            wmPtr->initialState = NormalState;
            wmPtr->attributes.zoomed = 0;

            if (glfwWindow != NULL) {
                glfwRestoreWindow(glfwWindow);
            }
            if (decor != NULL) {
                TkWaylandSetWindowMaximized(decor, 0);
            }
            TkpWmSetState(winPtr, NormalState);
            break;

        case OPT_ICONIC:
            wmPtr->initialState= IconicState;

            if (glfwWindow != NULL) {
                glfwIconifyWindow(glfwWindow);
            }
            winPtr->flags &= ~TK_MAPPED;

            TkpWmSetState(winPtr, IconicState);
            break;

        case OPT_WITHDRAWN:
            TkpWmSetState(winPtr, WithdrawnState);
            break;

        case OPT_ICON:
            Tcl_SetObjResult(interp, Tcl_NewStringObj(
                "can't change state to icon: not implemented", -1));
            Tcl_SetErrorCode(interp, "TK", "WM", "STATE", "ICON", NULL);
            return TCL_ERROR;

        case OPT_ZOOMED:
            wmPtr->attributes.zoomed = 1;

            if (glfwWindow != NULL) {
                glfwMaximizeWindow(glfwWindow);
            }
            if (decor != NULL) {
                TkWaylandSetWindowMaximized(decor, 1);
            }
            /* Note: many WMs ignore attempts to force zoom via hints,
               so we rely on the GLFW/Wayland calls above. */
            break;
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmTitleCmd --
 *
 *	Implements the "wm title" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Updates window title.
 *
 *----------------------------------------------------------------------
 */

static int
WmTitleCmd(
	   TCL_UNUSED(Tk_Window),
	   TkWindow   *winPtr,
	   Tcl_Interp *interp,
	   int         objc,
	   Tcl_Obj *const objv[])
{
    WmInfo     *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    const char *t;
    Tcl_Size    len;

    if (objc > 1) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName title ?newTitle?"); return TCL_ERROR;
    }
    if (objc == 0) {
        Tcl_SetObjResult(interp,Tcl_NewStringObj(
						 wmPtr->title ? wmPtr->title : winPtr->nameUid,-1));
        return TCL_OK;
    }
    t = Tcl_GetStringFromObj(objv[0],&len);
    if (wmPtr->title) ckfree(wmPtr->title);
    wmPtr->title = ckalloc(len+1);
    strcpy(wmPtr->title, t);
    if (!(wmPtr->flags & WM_NEVER_MAPPED)) UpdateTitle(winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmTransientCmd --
 *
 *	Implements the "wm transient" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Sets or clears transient-for relationship.
 *
 *----------------------------------------------------------------------
 */

static int
WmTransientCmd(
	       Tk_Window   tkwin,
	       TkWindow   *winPtr,
	       Tcl_Interp *interp,
	       int         objc,
	       Tcl_Obj *const objv[])
{
    WmInfo    *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    Tk_Window  container;
    WmInfo    *wmPtr2;

    if (objc > 1) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName transient ?window?"); return TCL_ERROR;
    }
    if (objc == 0) {
        if (wmPtr->containerPtr)
            Tcl_SetObjResult(interp,Tcl_NewStringObj(
						     Tk_PathName(wmPtr->containerPtr),-1));
        return TCL_OK;
    }
    if (Tcl_GetString(objv[0])[0] == '\0') {
        if (wmPtr->containerPtr) {
            wmPtr2 = (WmInfo *)wmPtr->containerPtr->wmInfoPtr;
            if (wmPtr2) wmPtr2->numTransients--;
            Tk_DeleteEventHandler((Tk_Window)wmPtr->containerPtr,
				  StructureNotifyMask, WmWaitMapProc, (ClientData)winPtr);
        }
        wmPtr->containerPtr = NULL;
    } else {
        container = Tk_NameToWindow(interp,Tcl_GetString(objv[0]),tkwin);
        if (container == NULL) return TCL_ERROR;
        while (!Tk_IsTopLevel(container)) container = Tk_Parent(container);
        wmPtr2 = (WmInfo *)((TkWindow *)container)->wmInfoPtr;
        if (wmPtr->containerPtr) {
            WmInfo *old = (WmInfo *)wmPtr->containerPtr->wmInfoPtr;
            if (old) old->numTransients--;
        }
        wmPtr->containerPtr = (TkWindow *)container;
        if (wmPtr2) wmPtr2->numTransients++;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmWithdrawCmd --
 *
 *	Implements the "wm withdraw" subcommand.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Withdraws the window (hides it and removes from taskbar).
 *
 *----------------------------------------------------------------------
 */

static int
WmWithdrawCmd(
	      TCL_UNUSED(Tk_Window),
	      TkWindow   *winPtr,
	      Tcl_Interp *interp,
	      int         objc,
	      Tcl_Obj *const objv[])
{
    if (objc != 0) {
        Tcl_WrongNumArgs(interp,0,objv,"pathName withdraw"); return TCL_ERROR;
    }
    
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
  
    wmPtr->withdrawn = 1;
    wmPtr->initialState = WithdrawnState;

    TkWmUnmapWindow(winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GLFW helper implementations
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * ConvertPhotoToGlfwIcon --
 *
 *	Convert a Tk photo image to a GLFW icon and add it to the icon list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates a new GLFWimage and adds it to wmPtr->glfwIcon array.
 *
 *----------------------------------------------------------------------
 */

static void
ConvertPhotoToGlfwIcon(
		       TkWindow        *winPtr,
		       Tk_PhotoHandle   photo)
{
    WmInfo              *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    Tk_PhotoImageBlock   block;
    int                  width, height, pixelCount, i;
    GLFWimage           *newIcons;
    GLFWimage           *icon;
    unsigned char       *pixels, *src, *dst;

    Tk_PhotoGetSize(photo, &width, &height);
    Tk_PhotoGetImage(photo, &block);

    /* Grow icon array. */
    newIcons = (GLFWimage *)ckalloc((wmPtr->glfwIconCount+1) * sizeof(GLFWimage));
    if (wmPtr->glfwIcon != NULL && wmPtr->glfwIconCount > 0) {
        memcpy(newIcons, wmPtr->glfwIcon,
               wmPtr->glfwIconCount * sizeof(GLFWimage));
        ckfree((char *)wmPtr->glfwIcon);
    }
    wmPtr->glfwIcon = newIcons;

    icon         = &wmPtr->glfwIcon[wmPtr->glfwIconCount];
    icon->width  = width;
    icon->height = height;

    pixelCount = width * height;
    pixels = (unsigned char *)ckalloc(pixelCount * 4);

    src = (unsigned char *)block.pixelPtr;
    dst = pixels;

    if (block.pixelSize == 4) {
        memcpy(pixels, src, pixelCount * 4);
    } else if (block.pixelSize == 3) {
        for (i = 0; i < pixelCount; i++) {
            dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2]; dst[3]=255;
            src+=3; dst+=4;
        }
    } else { /* greyscale */
        for (i = 0; i < pixelCount; i++) {
            dst[0]=dst[1]=dst[2]=src[0]; dst[3]=255;
            src+=1; dst+=4;
        }
    }

    icon->pixels = pixels;
    wmPtr->glfwIconCount++;

    if (wmPtr->glfwWindow != NULL) {
        glfwSetWindowIcon(wmPtr->glfwWindow,
                          wmPtr->glfwIconCount, wmPtr->glfwIcon);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ApplyWindowHints --
 *
 *	Apply GLFW window hints based on current WmInfo state.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls glfwWindowHint for various window attributes.
 *
 *----------------------------------------------------------------------
 */

static void
ApplyWindowHints(
		 TkWindow *winPtr)
{
    WmInfo    *wmPtr  = (WmInfo *)winPtr->wmInfoPtr;
    Tk_Window  tkwin  = (Tk_Window)winPtr;

    glfwWindowHint(GLFW_RESIZABLE,
		   (wmPtr->flags & (WM_WIDTH_NOT_RESIZABLE|WM_HEIGHT_NOT_RESIZABLE))
		   ? GLFW_FALSE : GLFW_TRUE);

    glfwWindowHint(GLFW_DECORATED,
		   Tk_Attributes(tkwin)->override_redirect ? GLFW_FALSE : GLFW_TRUE);

    glfwWindowHint(GLFW_FLOATING,
		   wmPtr->attributes.topmost ? GLFW_TRUE : GLFW_FALSE);

    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER,
		   (wmPtr->attributes.alpha < 1.0) ? GLFW_TRUE : GLFW_FALSE);

    glfwWindowHint(GLFW_FOCUS_ON_SHOW,  GLFW_TRUE);
    glfwWindowHint(GLFW_AUTO_ICONIFY,   GLFW_FALSE);
}

/*
 *----------------------------------------------------------------------
 *
 * ApplyFullscreenState --
 *
 *	Apply or remove fullscreen state for a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the window's monitor mode (fullscreen or windowed).
 *
 *----------------------------------------------------------------------
 */

static void
ApplyFullscreenState(TkWindow *winPtr)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    if (wmPtr->glfwWindow == NULL) return;

    GLFWmonitor *currentMonitor = glfwGetWindowMonitor(wmPtr->glfwWindow);
    int desiredFullscreen = wmPtr->attributes.fullscreen;

    if (desiredFullscreen && currentMonitor == NULL) {
        /* Transitioning from windowed to fullscreen: save current geometry. */
        glfwGetWindowPos(wmPtr->glfwWindow, &wmPtr->x, &wmPtr->y);
        glfwGetWindowSize(wmPtr->glfwWindow, &wmPtr->width, &wmPtr->height);

        /* Switch to fullscreen on the primary monitor. */
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(wmPtr->glfwWindow, monitor,
                             0, 0, mode->width, mode->height, mode->refreshRate);
    }
    else if (!desiredFullscreen && currentMonitor != NULL) {
        /* Transitioning from fullscreen to windowed: restore saved geometry. */
        int w = (wmPtr->width  > 0) ? wmPtr->width  : winPtr->reqWidth;
        int h = (wmPtr->height > 0) ? wmPtr->height : winPtr->reqHeight;
        glfwSetWindowMonitor(wmPtr->glfwWindow, NULL,
                             wmPtr->x, wmPtr->y, w, h, GLFW_DONT_CARE);
    }
     /* No action needed if already in the desired state. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetMainMenubar / related
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * TkpSetMainMenubar --
 *
 *	Set the main menubar for a toplevel window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores menubar reference and updates geometry hints.
 *
 *----------------------------------------------------------------------
 */

void
TkpSetMainMenubar(
		  TkWindow  *winPtr,
		  Tk_Window  menubar)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    if (wmPtr == NULL) return;

    wmPtr->menubar    = menubar;
    wmPtr->menuHeight = Tk_ReqHeight(menubar);
    if (wmPtr->menuHeight <= 0) wmPtr->menuHeight = 1;

    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpWmSetState --
 *
 *	Set the window state (withdrawn, normal, iconic).
 *
 * Results:
 *	Always returns 1.
 *
 * Side effects:
 *	Changes window visibility and state.
 *
 *----------------------------------------------------------------------
 */

int
TkpWmSetState(
	      TkWindow *winPtr,
	      int       state)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    wmPtr->initialState = state;
    if (wmPtr->flags & WM_NEVER_MAPPED) {
	return 1;
    }
    

    if (state == WithdrawnState) {
        wmPtr->withdrawn = 1;
        if (wmPtr->flags & WM_NEVER_MAPPED) return 1;
        if (wmPtr->glfwWindow) glfwHideWindow(wmPtr->glfwWindow);
        WaitForMapNotify(winPtr, 0);
    } else if (state == NormalState) {
        wmPtr->withdrawn = 0;
        if (wmPtr->flags & WM_NEVER_MAPPED) return 0;
        UpdateHints(winPtr);
        Tk_MapWindow((Tk_Window)winPtr);
        if (wmPtr->glfwWindow) {
            if (wmPtr->attributes.fullscreen) ApplyFullscreenState(winPtr);
            else                              glfwRestoreWindow(wmPtr->glfwWindow);
        }
    } else if (state == IconicState) {
        if (wmPtr->flags & WM_NEVER_MAPPED) return 1;
        if (wmPtr->withdrawn) {
            UpdateHints(winPtr);
            Tk_MapWindow((Tk_Window)winPtr);
            wmPtr->withdrawn = 0;
        } else if (wmPtr->glfwWindow) {
            glfwIconifyWindow(wmPtr->glfwWindow);
        }
        WaitForMapNotify(winPtr, 0);
    }
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * TkpGetWrapperWindow / TkWmStackorderToplevel
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * TkpGetWrapperWindow --
 *
 *	Get the wrapper window for a toplevel (menubar container).
 *
 * Results:
 *	Returns the wrapper TkWindow, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkWindow *
TkpGetWrapperWindow(
		    TkWindow *winPtr)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    return (winPtr && wmPtr) ? wmPtr->wrapperPtr : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmStackorderToplevel --
 *
 *	Return a list of all toplevel windows in stacking order.
 *
 * Results:
 *	Returns a NULL-terminated array of TkWindow pointers, or NULL.
 *
 * Side effects:
 *	Allocates memory that must be freed by the caller.
 *
 *----------------------------------------------------------------------
 */

TkWindow **
TkWmStackorderToplevel(
		       TkWindow *parentPtr)
{
    WmInfo    *wmPtr;
    TkWindow **windows, **wp;
    int        count = 0;

    for (wmPtr=firstWmPtr; wmPtr; wmPtr=wmPtr->nextPtr) {
        if (wmPtr->winPtr->mainPtr == parentPtr->mainPtr) count++;
    }
    if (count == 0) return NULL;

    windows = (TkWindow **)ckalloc((count+1) * sizeof(TkWindow *));
    wp = windows;
    for (wmPtr=firstWmPtr; wmPtr; wmPtr=wmPtr->nextPtr) {
        if (wmPtr->winPtr->mainPtr == parentPtr->mainPtr)
            *wp++ = wmPtr->winPtr;
    }
    *wp = NULL;
    return windows;
}

/*
 *----------------------------------------------------------------------
 *
 * Internal update / event helpers
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * TopLevelEventProc --
 *
 *	Event handler for toplevel windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates internal state based on X events.
 *
 *----------------------------------------------------------------------
 */

static void
TopLevelEventProc(
		  ClientData clientData,
		  XEvent    *eventPtr)
{
    TkWindow *winPtr = (TkWindow *)clientData;
    WmInfo   *wmPtr  = (WmInfo *)winPtr->wmInfoPtr;

    switch (eventPtr->type) {
    case ConfigureNotify:
        /* Update our internal state from Tk's changes. */
        if (wmPtr != NULL && wmPtr->glfwWindow != NULL) {
            wmPtr->x = winPtr->changes.x;
            wmPtr->y = winPtr->changes.y;
            wmPtr->width = winPtr->changes.width;
            wmPtr->height = winPtr->changes.height;
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
 *	Geometry request handler for toplevel windows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Schedules a geometry update if needed.
 *
 *----------------------------------------------------------------------
 */

static void
TopLevelReqProc(
		TCL_UNUSED(ClientData),
		Tk_Window tkwin)
{
    TkWindow *winPtr = (TkWindow *)tkwin;
    WmInfo   *wmPtr  = (WmInfo *)winPtr->wmInfoPtr;

    if ((wmPtr->width >= 0) && (wmPtr->height >= 0)) return;

    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    if (!(wmPtr->flags & (WM_UPDATE_PENDING | WM_NEVER_MAPPED))) {
        Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData)winPtr);
        wmPtr->flags |= WM_UPDATE_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateGeometryInfo --
 *
 *	Idle callback to apply pending geometry changes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates window size and position via GLFW.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateGeometryInfo(
		   ClientData clientData)
{
    TkWindow *winPtr = (TkWindow *)clientData;
    WmInfo   *wmPtr  = (WmInfo *)winPtr->wmInfoPtr;
    int       tw, th;

    if (wmPtr == NULL) return;
    
    wmPtr->flags &= ~WM_UPDATE_PENDING;

    /* Apply any pending size hint updates. */
    if (wmPtr->flags & WM_UPDATE_SIZE_HINTS) {
        UpdateSizeHints(winPtr);
        wmPtr->flags &= ~WM_UPDATE_SIZE_HINTS;
    }

    /* Don't proceed if window isn't ready. */
    if (wmPtr->glfwWindow == NULL || wmPtr->withdrawn) {
        return;
    }

    /* Calculate target size. */
    tw = (wmPtr->width  > 0) ? wmPtr->width  : winPtr->reqWidth;
    th = (wmPtr->height > 0) ? wmPtr->height : winPtr->reqHeight;
    
    /* Ensure minimum size. */
    if (tw < wmPtr->minWidth) tw = wmPtr->minWidth;
    if (th < wmPtr->minHeight) th = wmPtr->minHeight;

    /* Apply size change if needed. */
    if (tw != wmPtr->configWidth || th != wmPtr->configHeight) {
        glfwSetWindowSize(wmPtr->glfwWindow, tw, th);
        TkGlfwUpdateWindowSize(wmPtr->glfwWindow, tw, th);
        winPtr->changes.width = tw;
        winPtr->changes.height = th;
        
        wmPtr->configWidth  = tw;
        wmPtr->configHeight = th;
        TkGlfwProcessEvents();
    }

    /* Apply position change if needed. */
    if ((wmPtr->flags & WM_MOVE_PENDING) ||
        wmPtr->x != winPtr->changes.x ||
        wmPtr->y != winPtr->changes.y) {
        glfwSetWindowPos(wmPtr->glfwWindow, wmPtr->x, wmPtr->y);
        wmPtr->flags &= ~WM_MOVE_PENDING;
        TkGlfwProcessEvents();
    }
    
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateHints --
 *
 *	Update all window hints (size, title, etc.).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls UpdateSizeHints and UpdateTitle.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateHints(TkWindow *winPtr)
{
    UpdateSizeHints(winPtr);
    UpdateTitle(winPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateSizeHints --
 *
 *	Update size-related hints (min/max size, aspect ratio).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls glfwSetWindowSizeLimits and glfwSetWindowAspectRatio.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateSizeHints(TkWindow *winPtr)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;

    if (wmPtr->glfwWindow == NULL) return;

    glfwSetWindowSizeLimits(wmPtr->glfwWindow,
			    wmPtr->minWidth, wmPtr->minHeight,
			    (wmPtr->maxWidth  > 0) ? wmPtr->maxWidth  : GLFW_DONT_CARE,
			    (wmPtr->maxHeight > 0) ? wmPtr->maxHeight : GLFW_DONT_CARE);

    if (wmPtr->sizeHintsFlags & WM_PAspect) {
        glfwSetWindowAspectRatio(wmPtr->glfwWindow,
				 wmPtr->minAspect.x, wmPtr->minAspect.y);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateTitle --
 *
 *	Update window title.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calls glfwSetWindowTitle.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateTitle(TkWindow *winPtr)
{
    WmInfo     *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    const char *title = wmPtr->title ? wmPtr->title : winPtr->nameUid;

    /* Update GLFW window title, which also updates server-side decorations if active. */
    if (wmPtr->glfwWindow != NULL) {
	glfwSetWindowTitle(wmPtr->glfwWindow, title);
    }

    /* Update CSD title if client-side decorations are active. */
    TkWaylandDecoration *decor = TkWaylandGetDecoration(winPtr);
    if (decor != NULL) {
        TkWaylandSetDecorationTitle(decor, title);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdatePhotoIcon --
 *
 *	No-op (icon updates handled by WmIconphotoCmd).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
UpdatePhotoIcon(TCL_UNUSED(TkWindow *))
{
    /* Real work done in WmIconphotoCmd → ConvertPhotoToGlfwIcon. */
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateVRootGeometry --
 *
 *	Update virtual root geometry from the primary monitor.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates wmPtr->vRootX, vRootY, vRootWidth, vRootHeight.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateVRootGeometry(WmInfo *wmPtr)
{
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    if (monitor != NULL) {
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        if (mode != NULL) {
            wmPtr->vRootWidth  = mode->width;
            wmPtr->vRootHeight = mode->height;
            glfwGetMonitorPos(monitor, &wmPtr->vRootX, &wmPtr->vRootY);
            return;
        }
    }
    wmPtr->vRootX = wmPtr->vRootY = 0;
    wmPtr->vRootWidth  = 1920;
    wmPtr->vRootHeight = 1080;
}

/*
 *----------------------------------------------------------------------
 *
 * WaitForMapNotify --
 *
 *	No-op on Wayland (GLFW visibility is synchronous).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
WaitForMapNotify(
		 TCL_UNUSED(TkWindow *),
		 TCL_UNUSED(int))
{
    /* No-op: GLFW visibility is synchronous. */
}

/*
 *----------------------------------------------------------------------
 *
 * ParseGeometry --
 *
 *	Parse a standard X geometry string of the form [WxH][{+-}X{+-}Y].
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *	Updates wmPtr geometry fields and schedules an idle update.
 *
 *----------------------------------------------------------------------
 */

static int
ParseGeometry(
	      Tcl_Interp *interp,
	      const char *string,
	      TkWindow   *winPtr)
{
    WmInfo     *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    int         width = -1, height = -1, x = 0, y = 0;
    int         hasSize = 0, hasPos = 0;
    int         xNeg = 0, yNeg = 0;
    const char *p = string;
    char       *end;

    if (*p == '\0') {
        wmPtr->width = wmPtr->height = -1;
        return TCL_OK;
    }

    /* Optional WxH part. */
    if (*p != '+' && *p != '-') {
        width = (int)strtol(p, &end, 10);
        if (end == p || *end != 'x') {
            goto badGeom;
        }
        p = end + 1; /* skip 'x' */
        
        height = (int)strtol(p, &end, 10);
        if (end == p) {
            goto badGeom;
        }
        p = end;
        hasSize = 1;
    }

    /* Optional ±X±Y part. */
    if (*p == '+' || *p == '-') {
        xNeg = (*p == '-');
        p++;
        x = (int)strtol(p, &end, 10);
        if (end == p) goto badGeom;
        p = end;

        if (*p == '+' || *p == '-') {
            yNeg = (*p == '-');
            p++;
            y = (int)strtol(p, &end, 10);
            if (end == p) goto badGeom;
            p = end;
        }
        hasPos = 1;
    }

    if (*p != '\0') {
        goto badGeom;
    }

    /* Apply size if specified. */
    if (hasSize) {
        /* Ensure size is within min/max constraints. */
        if (width < wmPtr->minWidth) width = wmPtr->minWidth;
        if (height < wmPtr->minHeight) height = wmPtr->minHeight;
        if (wmPtr->maxWidth > 0 && width > wmPtr->maxWidth) width = wmPtr->maxWidth;
        if (wmPtr->maxHeight > 0 && height > wmPtr->maxHeight) height = wmPtr->maxHeight;
        
        wmPtr->width = width;
        wmPtr->height = height;
    }

    /* Apply position if specified */
    if (hasPos) {
        wmPtr->x = xNeg ? -x : x;
        wmPtr->y = yNeg ? -y : y;

        if (xNeg) wmPtr->flags |= WM_NEGATIVE_X;
        else      wmPtr->flags &= ~WM_NEGATIVE_X;
        if (yNeg) wmPtr->flags |= WM_NEGATIVE_Y;
        else      wmPtr->flags &= ~WM_NEGATIVE_Y;

        wmPtr->flags |= WM_MOVE_PENDING;
    }

    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;

    return TCL_OK;

 badGeom:
    Tcl_SetObjResult(interp,
		     Tcl_ObjPrintf("bad geometry specifier \"%s\"", string));
    Tcl_SetErrorCode(interp, "TK", "WM", "GEOMETRY", "FORMAT", NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * WmUpdateGeom --
 *
 *	Schedule a geometry update if not already pending.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May schedule an idle callback.
 *
 *----------------------------------------------------------------------
 */

static void
WmUpdateGeom(WmInfo *wmPtr, TkWindow *winPtr)
{
    if (!(wmPtr->flags & (WM_UPDATE_PENDING | WM_NEVER_MAPPED))) {
        Tcl_DoWhenIdle(UpdateGeometryInfo, (ClientData)winPtr);
        wmPtr->flags |= WM_UPDATE_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * WmWaitMapProc --
 *
 *	Event handler for waiting for a transient's container to map.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Maps the transient when its container is mapped.
 *
 *----------------------------------------------------------------------
 */

static void
WmWaitMapProc(
	      ClientData clientData,
	      XEvent    *eventPtr)
{
    TkWindow *winPtr = (TkWindow *)clientData;
    WmInfo   *wmPtr  = (WmInfo *)winPtr->wmInfoPtr;

    if (eventPtr->type == MapNotify) {
        Tk_MapWindow((Tk_Window)winPtr);
        Tk_DeleteEventHandler((Tk_Window)wmPtr->containerPtr,
			      StructureNotifyMask, WmWaitMapProc, clientData);
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
