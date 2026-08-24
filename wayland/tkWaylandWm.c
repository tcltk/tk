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

/* Debugging
#define DEBUG_CHANNEL stdout
#define DEBUG_LABEL "wm"
*/

#include "tkInt.h"
#include "tkPort.h"
#include "tkWaylandWm.h"
#include "tkWaylandInt.h"
#include <GLFW/glfw3.h>
#include <GLES2/gl2.h>
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
 * XIDs, Drawables, Windows, and Pixmaps
 *----------------------------------------------------------------------
 */

/*
 * The file X.h in the xlib directory defines a type XID which is a 64 bit
 * integer on all platforms supported by Tk.  (Note that the original X11
 * specification used a 32 bit XID).  The X.h file also defines types
 * Drawable, Window, and Pixmap, all of which are typedef'ed to XID.  The
 * Window type is meant to be a unique identifier for an instance of a struct
 * TkWindow, defined in the generic code.  The Pixmap type is meant to be a
 * unique identifier for an instance of a struct TkWaylandPixmap.  The
 * Drawable type is meant to uniquely identify something which is either a
 * Window or a Pixmap.  As such, platform specific code needs to be able to
 * determine whether a Drawable is a window or a pixmap.
 *
 * Obviously, the simplest way to uniquely identify an instance of
 * a certain struct by a 64 bit integer is to use its address, cast
 * to a 64 bit integer.  However, that does not solve the problem
 * of how to distinguish a pixmap Drawable from a window Drawable.
 * It also encourages questionable behavior such as casting a Drawable
 * to a TkWindow*.
 *
 * Our solution to these problems for the Wayland port depends
 * on knowing that both the address of a struct TkWindow and the
 * address of a struct TkWaylandPixmap will be aligned to at least
 * 4 bytes.  In particular, if the address is cast to a 64 bit integer
 * then it will be even.  That means that, in constructing a
 * Drawable, as long as a Drawable is never cast to a pointer,
 * the last bit is available for use as a flag.  So we construct
 * the Drawable for a TkWindow by converting its address to a
 * 64 bit integer, and we construct the Drawable for a TkWaylandPixmap
 * by converting the address to a 64 bit integer and then adding 1.
 * To avoid the need for casting we provide functions for converting
 * between a Drawable and a pointer to one of these structs.  These
 * are functions, rather than macros, to enable the C compiler to
 * check the types of their argument.
 *
 */

inline bool TkWaylandDrawableIsPixmap(Drawable drawable) 
{
    /* A valid pixmap has the low bit set and is not None. */
    if (drawable == None || drawable == 0) {
        return 0;
    }
    /* Low bit set indicates pixmap (window drawables use even values). */
    return (drawable & 1) != 0;
}

inline Drawable TkWaylandDrawableForTkWindow(TkWindow *winPtr) {
     return (Drawable) winPtr;
 }

/*
 * This returns a NULL pointer if passed None.
 */

inline TkWindow* TkWaylandTkWindowFromDrawable(Drawable drawable) {
    if (drawable && TkWaylandDrawableIsPixmap(drawable)) {
	DEBUG_LOG("Attempt to convert a pixmap drawable %lx to a window.",
	       drawable);
    }
    return (TkWindow *) drawable;
}

inline Drawable TkWaylandDrawableForPixmap(TkWaylandPixmap *pixmapPtr) {
    DEBUG_LOG("~~~~~~~~~~~~~~~~~~~~~~~~ Generating drawable for %p", pixmapPtr);
    if (pixmapPtr != NULL) {
	DEBUG_LOG("~~~~~~~~~~~~ returning drawable %lx for pixmapPtr %p",
	       3 + (Drawable) pixmapPtr, pixmapPtr);
	return 3 + (Drawable) pixmapPtr;
    } else {
	return None;
    }
 }

inline TkWaylandPixmap* TkWaylandPixmapFromDrawable(Drawable drawable) {
    if (!TkWaylandDrawableIsPixmap(drawable)) {
	Tcl_Panic("Attempt to convert a window drawable to a pixmap");
    }
    return (TkWaylandPixmap *) (drawable & ~3UL);
}

/* Declarations of static functions defined in this module. */

static void TopLevelEventProc(void *clientData, XEvent *eventPtr);
static void TopLevelReqProc(void *clientData, Tk_Window tkwin);
static void ApplyPendingGeometry(TkWindow *winPtr);
static void UpdateGeometryInfo(void *clientData);
static void UpdateHints(TkWindow *winPtr);
static void UpdateSizeHints(TkWindow *winPtr);
static void UpdateTitle(TkWindow *winPtr);
static void UpdatePhotoIcon(TkWindow *winPtr);
static void UpdateVRootGeometry(WmInfo *wmPtr);
static void WaitForMapNotify(TkWindow *winPtr, int mapped);
static int  ParseGeometry(Tcl_Interp *interp, const char *string,
			  TkWindow *winPtr);
static void WmUpdateGeom(TkWindow *winPtr);

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
static void             WmWaitMapProc(void *clientData, XEvent *eventPtr);

/* GLFW integration helpers. */
static void InitializeGlfwWindow(TkWindow *winPtr);
static void DestroyGlfwWindow(TkWindow *winPtr);
static void ApplyFullscreenState(TkWindow *winPtr);

/*
 * This defines the geometry manager used by the window manager, as the
 * container of all toplevel windows.  The reqProc of this geometry manager,
 * TopLevelReqProc, is called whenever the geometry manager of a toplevel
 * window requests a size change for the toplevel.
 */

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
    wmPtr->withdrawn   = 0;
    wmPtr->initialState =  NormalState;
    wmPtr->minWidth    = wmPtr->minHeight = 1;
    wmPtr->widthInc    = wmPtr->heightInc = 1;
    wmPtr->minAspect.x = wmPtr->minAspect.y = 1;
    wmPtr->maxAspect.x = wmPtr->maxAspect.y = 1;
    wmPtr->reqGridWidth = wmPtr->reqGridHeight = -1;
    wmPtr->gravity     = NorthWestGravity;
    wmPtr->width = wmPtr->height = -1;
    wmPtr->x           = winPtr->changes.x;
    wmPtr->y           = winPtr->changes.y;
    //wmPtr->configWidth = wmPtr->configHeight = -1;
    wmPtr->vRootWidth  = 800;
    wmPtr->vRootHeight = 600;
    wmPtr->attributes.alpha = 1.0;
    wmPtr->reqState    = wmPtr->attributes;
    wmPtr->flags       = WM_NEVER_MAPPED;

    wmPtr->nextPtr = firstWmPtr;
    firstWmPtr     = wmPtr;
    winPtr->wmInfoPtr = wmPtr;

    UpdateVRootGeometry(wmPtr);

    /* Assign the wmMgrType to manage the geometry of this toplevel. */
    Tk_ManageGeometry((Tk_Window)winPtr, &wmMgrType, (void *)0);
}

/*
 *----------------------------------------------------------------------
 *
 * InitializeGlfwWindow --
 *
 *      Applies attributes specified in the WmInfo struct to the GLFWwindow,
 *      which should have been created by Tk_MakeWindow, then creates an
 *      event handler for the window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Applies initial window properties (title, size hints, etc.).
 *      Creates event handler.
 *
 *----------------------------------------------------------------------
 */

static void
InitializeGlfwWindow(TkWindow *winPtr)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    DEBUG_LOG("InitializeGlfwWindow: %s", Tk_PathName(winPtr));
    if (!glfwWindow) {
	Tcl_Panic("InitializeGlfwWindow: Tk window has no platform window");
    }

    /* Apply wm properties that are valid AFTER creation. */

    UpdateTitle(winPtr);
    UpdateSizeHints(winPtr);

    if (wmPtr->attributes.alpha != 1.0) {
        glfwSetWindowOpacity(glfwWindow,
                             (float)wmPtr->attributes.alpha);
    }

    /*
     * If override-redirect was set on this window before its GLFW
     * counterpart existed (e.g. via [wm overrideredirect] on a window
     * that hadn't been mapped yet), XChangeWindowAttributes had no
     * GLFWwindow to act on at the time and silently dropped the
     * request. Tk core still recorded the flag in winPtr->atts
     * regardless, so re-apply it here now that the GLFW window exists.
     */
    if (Tk_Attributes((Tk_Window) winPtr)->override_redirect) {
        glfwSetWindowAttrib(glfwWindow, GLFW_DECORATED, GLFW_FALSE);
    }

    /* Register wm event handler */
    Tk_CreateEventHandler((Tk_Window)winPtr,
			  StructureNotifyMask | PropertyChangeMask,
			  TopLevelEventProc, (void *)winPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyGlfwWindow --
 *
 *	Destroys the GLFW window associated with a TkWindow.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The GLFW window is destroyed and the glfwWindow field
 *	in the the TkWindowPrivate struct is set to NULL.
 *
 *----------------------------------------------------------------------
 */

static void DestroyGlfwWindow(TkWindow *winPtr) {
    if (!winPtr || !winPtr->privatePtr) {
        return;
    }
    
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    
    /* Clear the clip rect buffer. */
    winPtr->privatePtr->clipRectBufferSize = 0;
    winPtr->privatePtr->clipRectCount = 0;
    if (winPtr->privatePtr->clipRectBuffer) {
        DEBUG_LOG("Freeing clipRects for %s", Tk_PathName(winPtr));
        ckfree(winPtr->privatePtr->clipRectBuffer);
        winPtr->privatePtr->clipRectBuffer = NULL;
    }
    
    if (glfwWindow) {
        /* 
         * IMPORTANT: Detach the user pointer to prevent double-free
         * when the GLFW window is destroyed.
         */
        glfwSetWindowUserPointer(glfwWindow, NULL);
        TkWaylandClearCallbacks(glfwWindow);
        TkWaylandDestroyWindow(glfwWindow);
        winPtr->privatePtr->glfwWindow = NULL;
    }
}

/*
 * Declared MODULE_SCOPE and defined in tkWaylandMenu.c. Not in a shared
 * header (tkWaylandNotify.c declares it the same way locally), so we
 * follow the same convention here.
 */
extern void TkWaylandMenubarResize(TkWindow *winPtr);

/*
 *----------------------------------------------------------------------
 *
 * TkWmMapWindow --
 *
 *	Called by Tk_MapWindow when mapping a toplevel.  Tk_MapWindow
 *      immediately handles a MapNotify event when this returns.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *      Calls InitializeGlfwWindow and UpdatePendingGeometry to set up the new
 *      toplevel, then calls glfwShowWindow to make the toplevel visible on
 *      the screen
 *
 *----------------------------------------------------------------------
 */

void
TkWmMapWindow(TkWindow *winPtr)
{
    DEBUG_LOG("TkWmMapWindow: %s", Tk_PathName(winPtr));
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    if (!wmPtr) Tcl_Panic("TkWmMapWindow: No WmInfo");
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);

    wmPtr->withdrawn   = 0;
    wmPtr->initialState = NormalState;
    wmPtr->flags &= ~WM_NEVER_MAPPED;

    if (!Tk_IsEmbedded(winPtr)) {
        InitializeGlfwWindow(winPtr);
        UpdateHints(winPtr);
        UpdateTitle(winPtr);
        UpdatePhotoIcon(winPtr);
    }
    if (glfwWindow) {
        winPtr->flags |= TK_MAPPED;
	UpdateGeometryInfo(winPtr);
	DEBUG_LOG("TkWmMapWindow: Showing %s", Tk_PathName(winPtr));
        glfwShowWindow(glfwWindow);
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
    DEBUG_LOG("TkWmUnmapWindow: %s", Tk_PathName(winPtr));
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    winPtr->flags &= ~TK_MAPPED;
    if (glfwWindow) {
        glfwHideWindow(glfwWindow);
    }
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
    WmInfo *wmPtr;
    WmInfo *wmPtr2;

    if (winPtr == NULL) {
        return;
    }

    DEBUG_LOG("TkWmDeadWindow: %s", Tk_PathName(winPtr));

    /*
     * Clean up the private data. We need to be very careful here
     * because the GLFW window might already be destroyed or invalid.
     */
    if (winPtr->privatePtr) {
        GLFWwindow *glfwWindow = winPtr->privatePtr->glfwWindow;
        
        /* Free clip rect buffer if present. */
        winPtr->privatePtr->clipRectBufferSize = 0;
        winPtr->privatePtr->clipRectCount = 0;
        if (winPtr->privatePtr->clipRectBuffer) {
            DEBUG_LOG("Freeing clipRects for %s", Tk_PathName(winPtr));
            ckfree(winPtr->privatePtr->clipRectBuffer);
            winPtr->privatePtr->clipRectBuffer = NULL;
        }
        
        /*
         * Only try to destroy the GLFW window if it exists and is valid.
         * Check if the window pointer is non-NULL and seems valid.
         */
        if (glfwWindow != NULL) {
            /*
             * Try to detach the user pointer first, but this might fail
             * if the window is already destroyed. Use a try/catch style
             * approach by checking if glfwWindow is valid.
             */
            DEBUG_LOG("Destroying GLFW window for %s", Tk_PathName(winPtr));
            
            /* Clear callbacks before destroying. */
            TkWaylandClearCallbacks(glfwWindow);
            
            /* Destroy the GLFW window. */
            TkWaylandDestroyWindow(glfwWindow);
            
            /* Clear the pointer to prevent use-after-free. */
            winPtr->privatePtr->glfwWindow = NULL;
        }
        
        /* Free the pendingText DString. */
        Tcl_DStringFree(&winPtr->privatePtr->pendingText);
        
        /* Free the privatePtr itself. */
        ckfree(winPtr->privatePtr);
        winPtr->privatePtr = NULL;
    }

    wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    if (wmPtr == NULL) {
        DEBUG_LOG("TkWmDeadWindow: No WmInfo for %s", Tk_PathName(winPtr));
        return;
    }

    /* Delete event handlers. */
    Tk_DeleteEventHandler((Tk_Window)winPtr,
                          StructureNotifyMask | PropertyChangeMask,
                          TopLevelEventProc, (void *)winPtr);

    /* Cancel any pending UpdateGeometryInfo idle tasks  */
    if (wmPtr->flags & WM_UPDATE_PENDING) {
        Tcl_CancelIdleCall(UpdateGeometryInfo, (void *)winPtr);
	wmPtr->flags &= ~WM_UPDATE_PENDING;
    }

    /* Destroy wrapper window if present. */
    if (wmPtr->wrapperPtr != NULL) {
        Tk_DestroyWindow((Tk_Window)wmPtr->wrapperPtr);
        wmPtr->wrapperPtr = NULL;
    }

    /* Free string resources. */
    if (wmPtr->title) {
        ckfree(wmPtr->title);
        wmPtr->title = NULL;
    }
    if (wmPtr->iconName) {
        ckfree(wmPtr->iconName);
        wmPtr->iconName = NULL;
    }
    if (wmPtr->leaderName) {
        ckfree(wmPtr->leaderName);
        wmPtr->leaderName = NULL;
    }
    if (wmPtr->clientMachine) {
        ckfree(wmPtr->clientMachine);
        wmPtr->clientMachine = NULL;
    }

    /* Destroy child windows. */
    if (wmPtr->menubar) {
        Tk_DestroyWindow(wmPtr->menubar);
        wmPtr->menubar = NULL;
    }
    if (wmPtr->icon) {
        Tk_DestroyWindow(wmPtr->icon);
        wmPtr->icon = NULL;
    }
    if (wmPtr->iconDataPtr) {
        ckfree((char *)wmPtr->iconDataPtr);
        wmPtr->iconDataPtr = NULL;
    }

    /* Free protocol handlers. */
    while (wmPtr->protPtr != NULL) {
        ProtocolHandler *protPtr = wmPtr->protPtr;
        wmPtr->protPtr = protPtr->nextPtr;
        Tcl_EventuallyFree((void *)protPtr, TCL_DYNAMIC);
    }

    /* Free command argv. */
    if (wmPtr->cmdArgv != NULL) {
        Tcl_Size j;
        for (j = 0; j < wmPtr->cmdArgc; j++) {
            Tcl_DecrRefCount(wmPtr->cmdArgv[j]);
        }
        ckfree((char *)wmPtr->cmdArgv);
        wmPtr->cmdArgv = NULL;
        wmPtr->cmdArgc = 0;
    }

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

    /* Clear the window's pointer to this WmInfo and free it. */
    winPtr->wmInfoPtr = NULL;
    ckfree((char *)wmPtr);
    
    DEBUG_LOG("TkWmDeadWindow: Done cleaning up %s", Tk_PathName(winPtr));
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
            Tcl_EventuallyFree((void *)p, TCL_DYNAMIC);
        }
        if (wmPtr->cmdArgv != NULL) {
            Tcl_Size j;
            for (j = 0; j < wmPtr->cmdArgc; j++) {
                Tcl_DecrRefCount(wmPtr->cmdArgv[j]);
            }
            ckfree((char *)wmPtr->cmdArgv);
        }
        ckfree((char *)wmPtr);
    }
    firstWmPtr = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MakeWindow --
 *
 *      Platform-specific window creation called by Tk's generic layer.
 *      For toplevels, it creates a GLFW window.
 *      For menu windows, it uses the subsurface path instead.
 *
 * Results:
 *      Returns a Window identifier which is assigned to the window
 *      field of the TkWindow structure.
 *
 * Side effects:
 *      Creates a new GLFW window for toplevels, or subsurface for menus.
 *
 *----------------------------------------------------------------------
 */

Window
Tk_MakeWindow(
    Tk_Window tkwin,
    TCL_UNUSED(Window))        /* parent */
{
    TkWindow   *winPtr     = (TkWindow *)tkwin;
    GLFWwindow *glfwWindow = NULL;
    int         width, height;
    Drawable    drawable;
    Window      result;

    DEBUG_LOG("Tk_MakeWindow: %s", Tk_PathName(tkwin));
    result = TkWaylandDrawableForTkWindow(winPtr);

    if (winPtr->privatePtr == NULL) {
	winPtr->privatePtr = (glfwData*) ckalloc(sizeof(glfwData));
	Tcl_DStringInit(&winPtr->privatePtr->pendingText);
	memset(winPtr->privatePtr, 0, sizeof(glfwData));
#define CLIPRECTBUFSIZE 8
	winPtr->privatePtr->clipRectBufferSize = CLIPRECTBUFSIZE;
	winPtr->privatePtr->clipRectBuffer = ckalloc(
	    CLIPRECTBUFSIZE * sizeof(clipRect));
#undef CLIPRECTBUFSIZE    
    }
    if (Tk_IsTopLevel(winPtr)) {
		
        /*
         * Guard against internal Tk toplevels that have no mainPtr —
         * e.g. the clipboard owner window created by TkClipInit.
         * These need a valid window ID but no real GLFW surface.
         * Return the pre-allocated result token directly; the window
         * will never be mapped or rendered.
         */
        if (!winPtr->mainPtr || !winPtr->mainPtr->interp) {
            return result;
        }
        
        /*
         * Check if this is a menu window by its class.
         * Menu windows should use subsurfaces, not full GLFW windows.
         * Skip GLFW window creation entirely for menu windows.
         */
        if (winPtr->classUid == Tk_GetUid("Menu") ||
            winPtr->classUid == Tk_GetUid("Menubar")) {
            
            DEBUG_LOG("Tk_MakeWindow: %s is a menu (class=%s), skipping GLFW window creation", 
                    Tk_PathName(tkwin), Tk_GetUid(winPtr->classUid));
            
            /* Ensure private data exists. */
            if (winPtr->privatePtr == NULL) {
                winPtr->privatePtr = (glfwData*) ckalloc(sizeof(glfwData));
                Tcl_DStringInit(&winPtr->privatePtr->pendingText);
                winPtr->privatePtr->glfwWindow = NULL;
                winPtr->privatePtr->fb = NULL;
            }
            
            /* No GLFW window for menu - will use subsurface via menu system. */
            return result;
        }

        /*
         * Toplevel window - non-menu.
         */

        /*
         * Prefer the geometry manager's requested size (reqWidth/reqHeight)
         * over winPtr->changes, which is often still at Tk's uninitialized
         * default (1x1) at window-creation time -- pack/grid have usually
         * already run by now, even though changes hasn't been updated to
         * match yet.  Seeding the GLFW window with the real target size here
         * avoids a visible/racy resize-after-show later in TkWmMapWindow.
         */
        width  = (winPtr->reqWidth  > 1) ? winPtr->reqWidth  :
                 (winPtr->changes.width  > 1) ? winPtr->changes.width  : 200;
        height = (winPtr->reqHeight > 1) ? winPtr->reqHeight :
                 (winPtr->changes.height > 1) ? winPtr->changes.height : 200;

        /*
         * Create the GLFW window and get a drawable ID.
         * drawable is ignored; we use winPtr->window instead.
         */

	DEBUG_LOG("Creating glfwWindow %s at size %dx%d",
	       Tk_PathName(tkwin), width, height);
	glfwWindow = TkWaylandCreateWindow(winPtr, width, height,
                                        Tk_Name(tkwin), &drawable);
        if (!glfwWindow) {
            return None;
        }

        /*
         * Ensure WmInfo exists.
         */
        if (!winPtr->wmInfoPtr) {
            TkWmNewWindow(winPtr);
        }

        WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
        if (wmPtr) {
            wmPtr->flags |= WM_NEVER_MAPPED;
        }
    }
    createClipShaders(winPtr);
    return result;
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
        wmPtr->flags |= WM_UPDATE_PENDING;
	DEBUG_LOG("Tk_SetGrid: scheduling UpdateGeometryInfo");
        Tcl_DoWhenIdle(UpdateGeometryInfo, (void *)winPtr);
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
        wmPtr->flags |= WM_UPDATE_PENDING;
	DEBUG_LOG("Tk_UnsetGrid: scheduling UpdateGeometryInfo");
        Tcl_DoWhenIdle(UpdateGeometryInfo, (void *)winPtr);
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
            Tcl_CancelIdleCall(UpdateGeometryInfo, (void *)winPtr);
        }
	DEBUG_LOG("Tk_MoveToplevelWindow: scheduling UpdateGeometryInfo");
        UpdateGeometryInfo((void *)winPtr);
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
 *      When using GLFW / Wayland, that is the toplevel itself.
 *
 * Results:
 *	Returns the toplevel TkWindow.
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
    return winPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetPointerCoords --
 *
 *	Get the root coordinates of the mouse pointer, as reported by the
 *	GLFW window of the given window's toplevel.
 *
 * Results:
 *	Sets *xPtr and *yPtr to the pointer position, or to -1 if it
 *	cannot be determined.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkGetPointerCoords(
    Tk_Window tkwin,
    int *xPtr,
    int *yPtr)
{
    /* Window XIDs are TkWindow pointers in this port. */
    if (tkwin == NULL || !XQueryPointer(NULL, (Window)(TkWindow *)tkwin,
	    NULL, NULL, xPtr, yPtr, NULL, NULL, NULL)) {
	*xPtr = *yPtr = -1;
    }
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
	    void * clientData,
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
    WmUpdateGeom(winPtr);
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

    /* No arguments → return all attributes */
    if (objc == 0) {
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

    /* Query single attribute. */
    if (objc == 1) {
        int attribute;
        if (Tcl_GetIndexFromObjStruct(interp, objv[0], WmAttributeNames,
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

    /* Must be attribute/value pairs. */
    if (objc % 2 != 0) {
        Tcl_WrongNumArgs(interp, 0, objv, "?-attribute value ...?");
        return TCL_ERROR;
    }

    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);

    /* Set attributes. */
    for (i = 0; i < objc; i += 2) {
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

                if (glfwWindow) {
                    glfwSetWindowOpacity(glfwWindow, (float)d);
                }
                break;
            }

            case WMATT_TOPMOST: {
                int b;
                if (Tcl_GetBooleanFromObj(interp, objv[i+1], &b) != TCL_OK) {
                    return TCL_ERROR;
                }
                wmPtr->reqState.topmost = wmPtr->attributes.topmost = b;

                if (glfwWindow) {
                    glfwSetWindowAttrib(glfwWindow, GLFW_FLOATING,
                        b ? GLFW_TRUE : GLFW_FALSE);
                }
                break;
            }

            case WMATT_ZOOMED: {
                int zoomed;
                if (Tcl_GetBooleanFromObj(interp, objv[i+1], &zoomed) != TCL_OK) {
                    return TCL_ERROR;
                }
                wmPtr->reqState.zoomed = wmPtr->attributes.zoomed = zoomed;

                if (glfwWindow) {
                    if (zoomed) {
                        glfwMaximizeWindow(glfwWindow);
                    } else {
                        glfwRestoreWindow(glfwWindow);
                    }
                }

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
                /* Placeholder / ignored. */
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
 *  No-op on Wayland.
 *
 * Side effects:
 *	Stores the client machine name.
 *
 *----------------------------------------------------------------------
 */


static int
WmClientCmd(
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
 * WmColormapwindowsCmd --
 *
 *	Implements the "wm colormapwindows" subcommand (no-op on Wayland).
 *
 * Results:
 *	 No-op on Wayland. 
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
 *	No-op on Wayland. 
 *
 * Side effects:
 *	Stores the command to restart the application.
 *
 *----------------------------------------------------------------------
 */

static int
WmCommandCmd(
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
 *	 No-op on Wayland. 
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
            TCL_UNUSED(int),
            TCL_UNUSED(Tcl_Obj *const *))
{
    Tcl_SetObjResult(interp, Tcl_NewObj());
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
            TCL_UNUSED(int),
            TCL_UNUSED(Tcl_Obj *const *))
{
    Tcl_SetObjResult(interp, Tcl_NewObj());
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
 *  No-op on Wayland. 
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
            TCL_UNUSED(int),
            TCL_UNUSED(Tcl_Obj *const *))
{
    Tcl_SetObjResult(interp, Tcl_NewObj());
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
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;

    if (objc > 1) {
        Tcl_WrongNumArgs(interp, 0, objv, "pathName geometry ?newGeometry?");
        return TCL_ERROR;
    }

    /* Return current geometry. */
    if (objc == 0) {
        int width, height;
        if (glfwWindow != NULL && !(wmPtr->flags & WM_NEVER_MAPPED)) {
            glfwGetWindowSize(glfwWindow, &width, &height);
        } else {
            width = (wmPtr->width >= 0) ? wmPtr->width : winPtr->reqWidth;
            height = (wmPtr->height >= 0) ? wmPtr->height : winPtr->reqHeight;
        }
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("%dx%d+%d+%d",
		 width, height, wmPtr->x, wmPtr->y));
        return TCL_OK;
    }

    /* Handle empty string - reset to default. */
    if (*Tcl_GetString(objv[0]) == '\0') {
        wmPtr->width = wmPtr->height = -1;

        if (wmPtr->flags & WM_UPDATE_PENDING) {
            Tcl_CancelIdleCall(UpdateGeometryInfo, (void *)winPtr);
            wmPtr->flags &= ~WM_UPDATE_PENDING;
        }
	DEBUG_LOG("WmGeometryCmd: calling UpdateGeometryInfo");
        UpdateGeometryInfo((void *)winPtr);
        return TCL_OK;
    }

    /* Parse and apply new geometry. */
    if (ParseGeometry(interp, Tcl_GetString(objv[0]), winPtr) != TCL_OK) {
        return TCL_ERROR;
    }

    /* Immediately set GLFW window size and position. */
    if (glfwWindow != NULL && !(wmPtr->flags & WM_NEVER_MAPPED)) {
        /* Set size only if positive values were provided. */
        if (wmPtr->width > 0 && wmPtr->height > 0) {
	    DEBUG_LOG("GeometryCmd setting window size %s -> %dx%d",
		Tk_PathName(winPtr), wmPtr->width, wmPtr->height);
            glfwSetWindowSize(glfwWindow, wmPtr->width, wmPtr->height);
        }

        /* Cancel any pending idle callback. */
        if (wmPtr->flags & WM_UPDATE_PENDING) {
            Tcl_CancelIdleCall(UpdateGeometryInfo, (void *)winPtr);
            wmPtr->flags &= ~WM_UPDATE_PENDING;
        }

        /* Update internal Tk/GLFW state. */
	DEBUG_LOG("WmGeometryCmd: calling UpdateGeometryInfo");
        UpdateGeometryInfo((void *)winPtr);

        /* Verify the change actually took effect. */
        int newWidth, newHeight;
        glfwGetWindowSize(glfwWindow, &newWidth, &newHeight);

        /* 
         * If the size didn't change (e.g., constrained by min/max),
	     * update wmPtr. 
	     */
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
    WmUpdateGeom(winPtr);
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
 *	 No-op on Wayland. 
 *
 * Side effects:
 *	Stores the group leader window name.
 *
 *----------------------------------------------------------------------
 */

static int
WmGroupCmd(
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
 * WmIconbadgeCmd --
 *
 *	Implements the "wm iconbadge" subcommand.
 *
 * Results:
 *	Standard Tcl result. No-op on Wayland - GLFW does not support
 *  setting window icons on Wayland.
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
			TCL_UNUSED(int),
			TCL_UNUSED(Tcl_Obj *const *))
{
    Tcl_SetObjResult(interp, Tcl_NewObj());
    return TCL_OK;
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
    if (objc != 0) {
        Tcl_WrongNumArgs(interp, 0, objv, "pathName iconify");
        return TCL_ERROR;
    }
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);

    /* Update Tk's internal state to IconicState. */
    TkpWmSetState(winPtr, IconicState);

    /* If the window is mapped and has a GLFW window, actually iconify it. */
    if (Tk_IsMapped(winPtr) && glfwWindow != NULL) {
        glfwIconifyWindow(glfwWindow);
        winPtr->flags &= ~TK_MAPPED;
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
 * WmIconnameCmd --
 *
 *	Implements the "wm iconname" subcommand (no-op on Wayland).
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
WmIconnameCmd(
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
 * WmIconphotoCmd --
 *
 *	Implements the "wm iconphoto" subcommand. 
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	No-op on Wayland. Toplevel icons on Wayland are instead set via the 
 *  wl app_id, which the compositor resolves against a matching .desktop 
 *  file's Icon=key - see TkWaylandSetAppId() / TkpSetAppName() in tkWaylandInit.c.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconphotoCmd(	
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
            Tcl_Interp *interp,
            TCL_UNUSED(int),
            TCL_UNUSED(Tcl_Obj *const *))
{ Tcl_SetObjResult(interp, Tcl_NewObj()); return TCL_OK; }

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
    Tcl_SetObjResult(interp, Tcl_NewObj());
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
    Tk_Window tkwin = (Tk_Window)winPtr;
    if ((Tk_GetPixelsFromObj(interp, tkwin, objv[0], &w) != TCL_OK)
	|| (Tk_GetPixelsFromObj(interp, tkwin, objv[1], &h) != TCL_OK)) {
	return TCL_ERROR;
    }
    wmPtr->maxWidth=w; wmPtr->maxHeight=h;
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(winPtr);
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
    Tk_Window tkwin = (Tk_Window)winPtr;
    if ((Tk_GetPixelsFromObj(interp, tkwin, objv[0], &w) != TCL_OK)
	|| (Tk_GetPixelsFromObj(interp, tkwin, objv[1], &h) != TCL_OK)) {
	return TCL_ERROR;
    }
    wmPtr->minWidth=w; wmPtr->minHeight=h;
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(winPtr);
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
    TCL_UNUSED(Tk_Window),
    TkWindow   *winPtr,
    Tcl_Interp *interp,
    int         objc,
    Tcl_Obj *const objv[])
{
    int boolean;
    XSetWindowAttributes atts;

    if (objc > 1) {
        Tcl_WrongNumArgs(interp, 0, objv, "pathName overrideredirect ?boolean?");
        return TCL_ERROR;
    }
    if (objc == 0) {
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(
            Tk_Attributes((Tk_Window) winPtr)->override_redirect));
        return TCL_OK;
    }
    if (Tcl_GetBooleanFromObj(interp, objv[0], &boolean) != TCL_OK) {
        return TCL_ERROR;
    }
    atts.override_redirect = boolean ? True : False;
    Tk_ChangeWindowAttributes((Tk_Window) winPtr, CWOverrideRedirect, &atts);
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
 *	None - no-op on Wayland.
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
        if (Tcl_GetIndexFromObjStruct(interp,objv[0],src,sizeof(char *),
		"argument",0,&idx)!=TCL_OK) {
            return TCL_ERROR;
	}
        if (idx==0) {
	    wmPtr->sizeHintsFlags&=~WM_USPosition;
	    wmPtr->sizeHintsFlags|=WM_PPosition;
	} else {
	    wmPtr->sizeHintsFlags&=~WM_PPosition;
	    wmPtr->sizeHintsFlags|=WM_USPosition;
	}
    }
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(winPtr);
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
                Tcl_EventuallyFree((void *)protPtr,TCL_DYNAMIC);
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
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    if (glfwWindow)
        glfwSetWindowAttrib(glfwWindow, GLFW_RESIZABLE,
                            (w || h) ? GLFW_TRUE : GLFW_FALSE);
    WmUpdateGeom(winPtr);
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
 *	None - no-op on Wayland. 
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
        if (wmPtr->sizeHintsFlags&WM_USSize) {
	    Tcl_SetObjResult(interp,Tcl_NewStringObj("user",-1));
	} else if (wmPtr->sizeHintsFlags&WM_PSize) {
	    Tcl_SetObjResult(interp,Tcl_NewStringObj("program",-1));
	}
        return TCL_OK;
    }
    if (*Tcl_GetString(objv[0]) == '\0') {
        wmPtr->sizeHintsFlags &= ~(WM_USSize|WM_PSize);
    } else {
        if (Tcl_GetIndexFromObjStruct(interp,objv[0], src,
		sizeof(char *),"argument",0,&idx) != TCL_OK) {
            return TCL_ERROR;
	}
        if (idx==0) {
	    wmPtr->sizeHintsFlags&=~WM_USSize;
	    wmPtr->sizeHintsFlags|=WM_PSize;
	} else {
	    wmPtr->sizeHintsFlags&=~WM_PSize;
	    wmPtr->sizeHintsFlags|=WM_USSize;
	}
    }
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(winPtr);
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
 *	None - no-op on Wayland. 
 *
 * Side effects:
 *	None (returns placeholder result for isabove/isbelow).
 *
 *----------------------------------------------------------------------
 */

static int
WmStackorderCmd(
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

    /*
     * The dispatcher in Tk_WmObjCmd has already shifted objv past
     * "wm state window": objc==0 is the query form, objv[0] is the
     * new state (same convention as WmTitleCmd et al).
     */
    if (objc > 1) {
        Tcl_WrongNumArgs(interp, 0, objv, "pathName state ?state?");
        return TCL_ERROR;
    }

    /* Query current state. */
    if (objc == 0) {
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
    if (Tcl_GetIndexFromObjStruct(interp, objv[0], opts, sizeof(char *),
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
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);

    switch (idx) {
        case OPT_NORMAL:
            wmPtr->initialState = NormalState;
            wmPtr->attributes.zoomed = 0;

            if (glfwWindow != NULL) {
                glfwRestoreWindow(glfwWindow);
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
            /* Same path as "wm withdraw" (WmWithdrawCmd). */
            wmPtr->withdrawn = 1;
            wmPtr->initialState = WithdrawnState;
            TkWmUnmapWindow(winPtr);
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
            /* 
             * Note: many WMs ignore attempts to force zoom via hints,
             * so we rely on the GLFW/Wayland calls above. 
             */
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
 *	None - no-ope on Wayland. 
 *
 * Side effects:
 *	Sets or clears transient-for relationship.
 *
 *----------------------------------------------------------------------
 */

static int
WmTransientCmd(
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
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    if (glfwWindow == NULL) return;

    GLFWmonitor *currentMonitor = glfwGetWindowMonitor(glfwWindow);
    int desiredFullscreen = wmPtr->attributes.fullscreen;

    if (desiredFullscreen && currentMonitor == NULL) {
        /* Transitioning from windowed to fullscreen: save current geometry. */
        glfwGetWindowSize(glfwWindow, &wmPtr->width, &wmPtr->height);

        /* Switch to fullscreen on the primary monitor. */
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(glfwWindow, monitor,
            0, 0, mode->width, mode->height, mode->refreshRate);
    }
    else if (!desiredFullscreen && currentMonitor != NULL) {
        /* Transitioning from fullscreen to windowed: restore saved geometry. */
        int w = (wmPtr->width  > 0) ? wmPtr->width  : winPtr->reqWidth;
        int h = (wmPtr->height > 0) ? wmPtr->height : winPtr->reqHeight;
        glfwSetWindowMonitor(glfwWindow, NULL, wmPtr->x, wmPtr->y,
			     w, h, GLFW_DONT_CARE);
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
    WmUpdateGeom(winPtr);
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

bool
TkpWmSetState(
    TkWindow *winPtr,
    int       state)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    wmPtr->initialState = state;
    if (wmPtr->flags & WM_NEVER_MAPPED) {
	return true;
    }
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);

    if (state == WithdrawnState) {
        wmPtr->withdrawn = 1;
        if (wmPtr->flags & WM_NEVER_MAPPED) return 1;
        if (glfwWindow) glfwHideWindow(glfwWindow);
        WaitForMapNotify(winPtr, 0);
    } else if (state == NormalState) {
        wmPtr->withdrawn = 0;
        if (wmPtr->flags & WM_NEVER_MAPPED) return 0;
        UpdateHints(winPtr);
        Tk_MapWindow((Tk_Window)winPtr);
        if (glfwWindow) {
            if (wmPtr->attributes.fullscreen) {
		ApplyFullscreenState(winPtr);
	    }
            else {
		glfwRestoreWindow(glfwWindow);
	    }
        }
    } else if (state == IconicState) {
        if (wmPtr->flags & WM_NEVER_MAPPED) return 1;
        if (wmPtr->withdrawn) {
            UpdateHints(winPtr);
            Tk_MapWindow((Tk_Window)winPtr);
            wmPtr->withdrawn = 0;
        } else if (glfwWindow) {
            glfwIconifyWindow(glfwWindow);
        }
        WaitForMapNotify(winPtr, 0);
    }
    return true;
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
 *	This is called by the generic focus code.  On X11 it returns the X11
 *      wrapper window, but GLFW on Wayland has no such thing.  We return the
 *      window itself if it is a toplevel, otherwise we return NULL.
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
    if (Tk_IsTopLevel((Tk_Window) winPtr)) {
	return winPtr;
    }
    return NULL;
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
		  void *clientData,
		  XEvent    *eventPtr)
{
    TkWindow *winPtr = (TkWindow *)clientData;
    WmInfo   *wmPtr  = (WmInfo *)winPtr->wmInfoPtr;
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);

    switch (eventPtr->type) {
    case ConfigureNotify:
        /* Update our internal state from Tk's changes. */
	DEBUG_LOG("ConfigureNotify received for %s", Tk_PathName(winPtr));
	wmPtr->width = wmPtr->height = -1;
        break;
    case MapNotify:
	DEBUG_LOG("MapNotify received for %s", Tk_PathName(winPtr));
        winPtr->flags |= TK_MAPPED;
        break;
    case UnmapNotify:
	DEBUG_LOG("UnmapNotify received for %s", Tk_PathName(winPtr));;
        winPtr->flags &= ~TK_MAPPED;
        break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TopLevelReqProc --
 *
 *	Geometry request handler for toplevel windows.  This proc
 *      needs to resize the toplevel to its requested size, if a
 *      change has been requested.
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
    TCL_UNUSED(void *),
    Tk_Window tkwin)
{
    TkWindow *winPtr = (TkWindow *)tkwin;
    WmInfo   *wmPtr  = (WmInfo *)winPtr->wmInfoPtr;
    DEBUG_LOG("TopLevelReqProc %s requesting size %dx%d",
	Tk_PathName(tkwin), winPtr->reqWidth, winPtr->reqHeight);

    if (wmPtr->flags & WM_UPDATE_PENDING) {
	DEBUG_LOG("TopLevelReqProc: Cancelling pending UpdateGeometryInfo");
	Tcl_CancelIdleCall(UpdateGeometryInfo, (void *)winPtr);
	//return;
    }

    if (Tk_IsMapped(winPtr)) {
	wmPtr->flags |= (WM_UPDATE_PENDING | WM_UPDATE_SIZE_HINTS);
	DEBUG_LOG("TopLevelReqProc: scheduling UpdateGeometryInfo %s to -1x-1",
	    Tk_PathName(winPtr));
	/* Signals to UpdateGeometryInfo to use reqWidth and reqHeight. */
	winPtr->flags |= TKWL_USE_REQUESTED;
	/* Schedule a size update. */
	Tcl_DoWhenIdle(UpdateGeometryInfo, (void *)winPtr);
    } else {
	DEBUG_LOG("TopLevelReqProc: %s is not mapped", Tk_PathName(winPtr));
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ApplyPendingGeometry --
 *
 *	Sets the size of the toplevel by calling glfwSetWindowSize.  This is
 *      called directly by that TkWmMapWindow when a toplevel is first mapped,
 *      and used as idle task by UpdateGeometryInfo.  The size is set to
 *      wmPtr->width x wmPtr->height if those values are both positive, or
 *      to winPtr->reqWidth x winPtr->reqHeight if not.
 *
 *	Caller is responsible for checking that glfwWindow is non-NULL and
 *	that the window isn't withdrawn before calling this.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May call glfwSetWindowSize and update wmPtr->configWidth/Height
 *	and winPtr->changes.width/height.
 *
 *----------------------------------------------------------------------
 */

static void
ApplyPendingGeometry(
    TkWindow *winPtr)
{
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    WmInfo   *wmPtr  = (WmInfo *)winPtr->wmInfoPtr;
    int tw, th;

    /*
     * Look up the target size for this window in the wmPtr.  If the
     * TKWL_USE_REQUESTED flag is set or if the wmPtr value is negative
     * we use the reqWidth or reqHeight stored in the TkWindow struct.
     */

    int useReq = winPtr->flags & TKWL_USE_REQUESTED;
    tw = useReq || wmPtr->width < 0 ? winPtr->reqWidth  : wmPtr->width;
    th = useReq || wmPtr->height < 0 ? winPtr->reqHeight : wmPtr->height;
    winPtr->flags &= ~TKWL_USE_REQUESTED;

    /* Ensure at least minimum size. */
    if (tw < wmPtr->minWidth)  tw = wmPtr->minWidth;
    if (th < wmPtr->minHeight) th = wmPtr->minHeight;

    /* Apply size change if the target size is different from the
       configured. */
    //if (tw != wmPtr->configWidth || th != wmPtr->configHeight) {
    {
	/*
	 * Wayland won't allow a window to be so narrow that the title bar
	 * can't display all of the standard controls.  If a size change is
	 * requested that is smaller than that, the width will be increased,
	 * but GLFW will not know about the increase, so it won't allocate a
	 * correctly sized back buffer or pass the correct size to the
	 * FramebufferSizeCallback.  This causes our backing store framebuffer
	 * to be too small for the window, which causes part of the window to
	 * not be drawn.  There seems to be no way for us to detect the size
	 * increase.  So as a last resort / shameless hack we just make sure
	 * that the window is always at least 180 logical pixels wide.
	 */

	if (tw < 180) {
	    tw = 180;
	}

	DEBUG_LOG("ApplyPendingGeometry: calling glfwSetWindowSize %s -> %dx%d",
	    Tk_PathName(winPtr), tw, th);
        glfwSetWindowSize(glfwWindow, tw, th);
	
	/* Update the window data. */
        winPtr->changes.width = tw;
        winPtr->changes.height = th;
	//        wmPtr->configWidth  = tw;
	//        wmPtr->configHeight = th;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateGeometryInfo --
 *
 *	Run as an idle task to set the size of a toplevel's glfwWindow
 *      to match the size expected by Tk.  Calls ApplyPendingGeometry. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates window size and position via GLFW. (Although GLFW
 *      is not allowed to actually change the position.)
 *
 *----------------------------------------------------------------------
 */

static void
UpdateGeometryInfo(
    void *clientData)
{
    TkWindow *winPtr = (TkWindow *)clientData;
    WmInfo   *wmPtr;
    GLFWwindow *glfwWindow;
    glfwTkInfo *infoPtr;

    /*
     * This idle call can fire re-entrantly: TkWaylandSyncMenubarGeometry()
     * pumps the idle queue with a nested Tcl_DoOneEvent() loop while a
     * menu is being torn down (see TkpSetWindowMenuBar()).  If that
     * happens while this same toplevel is itself mid-destruction, bail
     * out immediately rather than touching any of its state.
     */
    if (winPtr->flags & TK_ALREADY_DEAD) {
	DEBUG_LOG("UpdateGeometryInfo: window already dead, skipping");
	return;
    }

    wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    if (wmPtr == NULL) {
	DEBUG_LOG("Cannot update geometry for a window with no WmInfo");
	return;
    }

    /*
     * Likewise, the toplevel's GLFW window (and glfwTkInfo) may already
     * have been torn down by the time we get here via the reentrant path
     * described above, even if TK_ALREADY_DEAD hasn't been observed yet.
     * Guard against a NULL infoPtr before dereferencing it.
     */
    glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    infoPtr = glfwWindow ? glfwGetWindowUserPointer(glfwWindow) : NULL;
    if (infoPtr == NULL) {
	DEBUG_LOG("UpdateGeometryInfo: no glfwTkInfo (window torn down), skipping");
	return;
    }

    if (infoPtr->flags & TKWL_NEVER_FOCUSED) {
	/* Newly created windows are hidden and set to be focused when they
	 * are first shown.  If a window is resized before it is has been
	 * shown, the missing window decorations trigger a wayland error which
	 * resets the size back to the last successful size, which will be its
	 * initial size, 200x200.  So we wait for the first call to the
	 * WindowFocusCallback before resizing it.
	 */
	wmPtr->flags |= WM_UPDATE_PENDING;
	DEBUG_LOG("UpdateGeometryInfo: waiting for focus.");
	Tcl_CreateTimerHandler(17, UpdateGeometryInfo, clientData);
	return;
    }
    if (wmPtr == NULL) {
	DEBUG_LOG("UpdateGeometryInfo: "
	    "Cannot update geometry for a window with no WmInfo");
	return;
    }
    DEBUG_LOG("UpdateGeometryInfo: %s to %dx%d", Tk_PathName(winPtr),
	   wmPtr->width, wmPtr->height);
    wmPtr->flags &= ~WM_UPDATE_PENDING;

    /* Apply any pending size hint updates. */
    if (wmPtr->flags & WM_UPDATE_SIZE_HINTS) {
        UpdateSizeHints(winPtr);
        wmPtr->flags &= ~WM_UPDATE_SIZE_HINTS;
    }

    /* Don't proceed if window isn't ready. */
    if (glfwWindow == NULL || wmPtr->withdrawn) {
	DEBUG_LOG("UpdateGeometryInfo: No glfw window");
        return;
    }

    DEBUG_LOG("UpdateGeometryInfo: calling ApplyPendingGeometry with flag %d",
	winPtr->flags & TKWL_USE_REQUESTED);
    ApplyPendingGeometry(winPtr);
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
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    if (glfwWindow == NULL) {
	return;
    }

    glfwSetWindowSizeLimits(glfwWindow,
	 wmPtr->minWidth, wmPtr->minHeight,
	 (wmPtr->maxWidth  > 0) ? wmPtr->maxWidth  : GLFW_DONT_CARE,
	 (wmPtr->maxHeight > 0) ? wmPtr->maxHeight : GLFW_DONT_CARE);

    if (wmPtr->sizeHintsFlags & WM_PAspect) {
        glfwSetWindowAspectRatio(glfwWindow,
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
    WmInfo     *wmPtr   = (WmInfo *)winPtr->wmInfoPtr;
    const char *title   = wmPtr->title ? wmPtr->title : winPtr->nameUid;
    GLFWwindow *glfwWin = TkWaylandGetGLFWwindow(winPtr);
    if (glfwWin)
        glfwSetWindowTitle(glfwWin, title);
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
    /*
     * wm iconphoto only records the icon name (see WmIconphotoCmd); it
     * doesn't set a compositor-visible icon, so there's nothing to
     * apply here. Toplevel icons are set via app_id - see
     * TkWaylandSetAppId() in tkWaylandInit.c.
     */
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
    ////XXXX FIX THIS!
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
WmUpdateGeom(TkWindow *winPtr)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    if (wmPtr->flags & WM_UPDATE_PENDING) {
	return;
    }
    wmPtr->flags |= WM_UPDATE_PENDING;
    Tcl_DoWhenIdle(UpdateGeometryInfo, (void *)winPtr);
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
	      void *clientData,
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
 *----------------------------------------------------------------------
 *
 * WindowToGLFW --
 *
 *	Helper function to find a GLFWwindow from an opaque Window handle.
 *	XXXXXA Window in this port is either:
 *	XXXXX  (a) a GLFWwindow pointer cast to Window (toplevel), or
 *	XXXXX  (b) a synthetic child-window ID produced in Tk_MakeWindow.
 *
 * Results:
 *	Pointer to the associated GLFWwindow, or NULL if not found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static GLFWwindow *
WindowToGLFW(
    Window window)
{
    if (window == None) {
        return NULL;
    }
    return TkWaylandGetGLFWwindowFromDrawable((Drawable)window);
}

/*
 *======================================================================
 *
 * Xlib window management functions, wrapping the Tk Wayland API.
 *
 *======================================================================
 */

/*
 *----------------------------------------------------------------------
 *
 * XCreateWindow --
 *
 *	Full Xlib window-creation entry point.
 *	In this port every window ultimately corresponds to a GLFW
 *	window (toplevel) or shares a parent's GLFW window (child).
 *
 * Results:
 *	No-op on Wayland.
 *
 * Side effects:
 *	Creates a GLFW window when parent is the root window.
 *
 *----------------------------------------------------------------------
 */

Window
XCreateWindow(
    TCL_UNUSED(Display *),          /* display */
    TCL_UNUSED(Window),             /* parent drawable */
    TCL_UNUSED(int),                /* x */
    TCL_UNUSED(int),                /* y */
    TCL_UNUSED(unsigned int),       /* width */
    TCL_UNUSED(unsigned int),       /* height */
    TCL_UNUSED(unsigned int),       /* border_width */
    TCL_UNUSED(int),                /* depth */
    TCL_UNUSED(unsigned int),       /* class */
    TCL_UNUSED(Visual *),           /* visual */
    TCL_UNUSED(unsigned long),      /* valuemask */
    TCL_UNUSED(XSetWindowAttributes *) /* attributes */
){
    return None;
}



/*
 *----------------------------------------------------------------------
 *
 * XCreateSimpleWindow --
 *
 *	Simplified Xlib window-creation entry point.
 *	Delegates to XCreateWindow with a minimal attribute set.
 *
 * Results:
 *	New Window handle, or None on failure.
 *
 * Side effects:
 *	See XCreateWindow.
 *
 *----------------------------------------------------------------------
 */

Window
XCreateSimpleWindow(
    TCL_UNUSED(Display *),          /* display */
    TCL_UNUSED(Window),             /* parent drawable */
    TCL_UNUSED(int),                /* x */
    TCL_UNUSED(int),                /* y */
    TCL_UNUSED(unsigned int),       /* width */
    TCL_UNUSED(unsigned int),       /* height */
    TCL_UNUSED(unsigned int),       /* border_width */
    TCL_UNUSED(unsigned long),      /* border */
    TCL_UNUSED(unsigned long))      /* background */
{
    return None;
}

/*
 *----------------------------------------------------------------------
 *
 * XDestroyWindow --
 *
 *	Destroy a window. The GLFW surface is torn down elsewhere; here we
 *	only clean up the generic pointer module's per-window state.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	May clear the grab/restrict/last-window state in tkPointer.c.
 *
 *----------------------------------------------------------------------
 */

int
XDestroyWindow(
    TCL_UNUSED(Display *), /* display */
    Window window)
{
    /* Window XIDs are TkWindow pointers in this port. */
    if (window != None) {
	TkPointerDeadWindow((TkWindow *)window);
    }
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDestroySubwindows --
 *
 *	Destroy all direct subwindows of window.
 *	In this port child windows do not own independent GLFW windows,
 *	so this is a no-op that still returns Success for compatibility.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None (child windows share the parent's GLFW window).
 *
 *----------------------------------------------------------------------
 */

int
XDestroySubwindows(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window))
{
    /* Child windows share the parent GLFW context – nothing to destroy. */
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XMapWindow --
 *
 *	Called by Tk_MapWindow.  Just generates an Expose event, to try
 *      to make sure the newly mapped window gets drawn.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
XMapWindow(
    Display *display,
    Window window) 
{
    TkWindow* winPtr = (TkWindow*) Tk_IdToWindow(display, window);
    DEBUG_LOG("XMapWindow: %s", Tk_PathName(winPtr));
    TkWaylandQueueExposeEvent(winPtr, 0, 0,
	Tk_Width(winPtr), Tk_Height(winPtr));
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XMapRaised --
 *
 *	Make a window visible and raise it to the top of the stack.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Shows and focuses the GLFW window.
 *
 *----------------------------------------------------------------------
 */

int
XMapRaised(
    TCL_UNUSED(Display *),
    Window window)
{
    GLFWwindow *gw = WindowToGLFW(window);

    if (gw != NULL) {
        glfwShowWindow(gw);
	/* This does nothing in Wayland, except generate an error message. */
        glfwFocusWindow(gw);
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XUnmapWindow --
 *
 *	Called by Tk_UnmapWindow.  But there is nothing we need to do.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XUnmapWindow(
    Display *display,
    Window window)
{
    TkWindow* winPtr = (TkWindow*) Tk_IdToWindow(display, window);
    DEBUG_LOG("XUnmapWindow: %s", Tk_PathName(winPtr));
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XUnmapSubwindows --
 *
 *	Unmap all mapped subwindows.  No-op for the same reason as
 *	XMapSubwindows.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XUnmapSubwindows(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window))
{
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XResizeWindow --
 *
 *	Change the size of a window.  Position and size themselves are
 *	tracked purely in Tk's own bookkeeping (winPtr->changes) and read
 *	from there at composite time, so there's no separate native
 *	window to resize here -- but since this window's on-screen extent
 *	just changed, queue an expose so the toplevel's backing FBO
 *	actually gets repainted to reflect it (see the analogous case in
 *	XMapWindow, and the fuller explanation on XMoveResizeWindow below).
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Queues an expose event for the window.
 *
 *----------------------------------------------------------------------
 */

int
XResizeWindow(
    Display *display,        /* display */
    Window window,           /* window */
    unsigned int width,      /* new width */
    unsigned int height)     /* new height */
{
    TkWindow *winPtr = (TkWindow *)Tk_IdToWindow(display, window);
    TkWindow *contPtr = winPtr->privatePtr->container;
    if (contPtr) {
	DEBUG_LOG("XResizeWindow: Exposing container %s", Tk_PathName(contPtr));
        TkWaylandQueueExposeEvent(contPtr, 0, 0,
	  Tk_Width(contPtr), Tk_Height(contPtr));
    } else if (winPtr->parentPtr) {
	DEBUG_LOG("XResizeWindow: Exposing parent %s",
	    Tk_PathName(winPtr->parentPtr));
        TkWaylandQueueExposeEvent(winPtr->parentPtr, 0, 0,
	  Tk_Width(winPtr->parentPtr), Tk_Height(winPtr->parentPtr));
    }	
    DEBUG_LOG("XResizeWindow: Exposing content %s", Tk_PathName(winPtr));
    TkWaylandQueueExposeEvent(winPtr, 0, 0,
	Tk_Width(winPtr), Tk_Height(winPtr));
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XMoveWindow --
 *
 *	Change the position of a window.  As with XResizeWindow, there is
 *	no separate native window to move on this backend -- child
 *	windows are composited directly into their toplevel's backing FBO
 *	at whatever offset Tk's own winPtr->changes.x/y record, and that's
 *	already been updated by the generic Tk_MoveResizeWindow caller by
 *	the time this runs.  What's missing without this call is any
 *	signal to actually repaint: nothing else marks the toplevel dirty
 *	just because a child's logical position changed, so queue an
 *	expose here.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Queues an expose event for the window.
 *
 *----------------------------------------------------------------------
 */

int
XMoveWindow(
    Display *display, /* display */
    Window window,    /* window */
    TCL_UNUSED(int),  /* x */
    TCL_UNUSED(int))  /* y */
{
    TkWindow *winPtr = (TkWindow *)Tk_IdToWindow(display, window);
    TkWindow *contPtr = winPtr->privatePtr->container;
    if (contPtr) {
	DEBUG_LOG("XMoveWindow: Exposing container %s", Tk_PathName(contPtr));
        TkWaylandQueueExposeEvent(contPtr, 0, 0,
	  Tk_Width(contPtr), Tk_Height(contPtr));
    } else if (winPtr->parentPtr) {
	DEBUG_LOG("XResizeWindow: Exposing parent %s",
	    Tk_PathName(winPtr->parentPtr));
        TkWaylandQueueExposeEvent(winPtr->parentPtr, 0, 0,
	  Tk_Width(winPtr->parentPtr), Tk_Height(winPtr->parentPtr));
    }
    DEBUG_LOG("XMoveWindow: Exposing content %s", Tk_PathName(winPtr));
    TkWaylandQueueExposeEvent(winPtr, 0, 0,
	Tk_Width(winPtr), Tk_Height(winPtr));
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XMoveResizeWindow --
 *
 *	Change position and size atomically.
 *
 *	This is the call that matters for embedded windows in the text
 *	widget: TkTextEmbWinDisplayProc calls Tk_MoveResizeWindow to
 *	reposition an embedded window after a scroll that TkScrollWindow
 *	handled as a pixel blit (see the MAC_OSX_TK/TK_USE_WAYLAND branch
 *	in tkTextDisp.c's DisplayText).  Without queuing an expose here,
 *	that reposition would only update winPtr->changes -- correct for
 *	future hit-testing and layout math, but with nothing to force the
 *	toplevel's backing FBO to actually be repainted at the new
 *	location in the meantime.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Queues an expose event for the window.
 *
 *----------------------------------------------------------------------
 */

int
XMoveResizeWindow(
    Display *display,        /* display */
    Window window,           /* window */
    TCL_UNUSED(int),         /* x */
    TCL_UNUSED(int),         /* y */
    unsigned int width,      /* new width */
    unsigned int height)     /* new height */
{
    TkWindow *winPtr = (TkWindow *)Tk_IdToWindow(display, window);
    TkWindow *contPtr = winPtr->privatePtr->container;
    if (contPtr) {
	DEBUG_LOG("XMoveResizeWindow: Exposing container %s",
	    Tk_PathName(contPtr));
        TkWaylandQueueExposeEvent(contPtr, 0, 0,
	  Tk_Width(contPtr), Tk_Height(contPtr));
    } else if (winPtr->parentPtr) {
	DEBUG_LOG("XResizeWindow: Exposing parent %s",
	    Tk_PathName(winPtr->parentPtr));
        TkWaylandQueueExposeEvent(winPtr->parentPtr, 0, 0,
	  Tk_Width(winPtr->parentPtr), Tk_Height(winPtr->parentPtr));
    }
    DEBUG_LOG("XMoveResizeWindow: Exposing content %s", Tk_PathName(winPtr));
    TkWaylandQueueExposeEvent(winPtr, 0, 0,
	Tk_Width(winPtr), Tk_Height(winPtr));
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XConfigureWindow --
 *
 *	General-purpose window configuration.
 *	Handles CWX, CWY, CWWidth, CWHeight, CWBorderWidth from the
 *	value_mask; stacking-related bits (CWSibling, CWStackMode) are
 *	silently ignored because the Wayland compositor controls the
 *	window stack.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	May move and/or resize the GLFW window.
 *
 *----------------------------------------------------------------------
 */

int
XConfigureWindow(
    TCL_UNUSED(Display *),
    Window          window,
    unsigned int    value_mask,
    XWindowChanges *values)
{
    GLFWwindow *gw = WindowToGLFW(window);
    int         w  = -1, h  = -1;
    int         resizeNeeded = 0;

    if (gw == NULL || values == NULL) {
        return Success;
    }

    /* Collect the current GLFW state to fill in un-specified fields. */
    glfwGetWindowSize(gw, &w, &h);

    if (value_mask & CWWidth)  { w = values->width;  resizeNeeded = 1; }
    if (value_mask & CWHeight) { h = values->height; resizeNeeded = 1; }

    /* CWBorderWidth: recorded for Tk bookkeeping; no GLFW equivalent. */
    /* CWSibling / CWStackMode: compositor-controlled; ignore. */

    if (resizeNeeded) {
	DEBUG_LOG("XConfigureWindow: calling glfwSetWindowSize -> %dx%d", w, h);
        glfwSetWindowSize(gw, w, h);
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWindowBorderWidth --
 *
 *	Change a window's border width.
 *	NanoVG handles border drawing; the GLFW border is the window
 *	decoration managed by the compositor.  We accept this call for
 *	Xlib compatibility but take no action.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetWindowBorderWidth(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(unsigned int))
{
    /* Border drawing is done by NanoVG / the compositor. */
    return Success;
}


/*
 *----------------------------------------------------------------------
 *
 * XRaiseWindow --
 *
 *	Raise a window to the top of the stack.
 *	GLFW exposes glfwFocusWindow which brings the window to the
 *	front on most Wayland compositors.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Focuses / raises the GLFW window.
 *
 *----------------------------------------------------------------------
 */

int
XRaiseWindow(
    TCL_UNUSED(Display *),
    Window window)
{
    GLFWwindow *gw = WindowToGLFW(window);

    if (gw != NULL) {
        glfwFocusWindow(gw);
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XLowerWindow --
 *
 *	Lower a window to the bottom of the stack.
 *	Wayland compositors do not expose a portable "lower" operation.
 *	We accept the call and return Success for compatibility.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XLowerWindow(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window))
{
    /* No-op: the compositor controls window stacking in Wayland. */
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XCirculateSubwindowsUp --
 *
 *	Raise the bottom-most subwindow to the top.
 *	No-op in Wayland.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XCirculateSubwindowsUp(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window))
{
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XCirculateSubwindowsDown --
 *
 *	Lower the top-most subwindow to the bottom.
 *	No-op in Wayland.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XCirculateSubwindowsDown(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window))
{
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XRestackWindows --
 *
 *	Restack multiple windows.
 *	The Wayland compositor owns the global window stack; individual
 *	applications cannot reorder top-level surfaces relative to each
 *	other.  We attempt to raise each window in the given order
 *	(best-effort) and return Success.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	May focus the first window in the array.
 *
 *----------------------------------------------------------------------
 */

int
XRestackWindows(
    TCL_UNUSED(Display *),
    Window *windows,
    int     nwindows)
{
    int i;

    if (windows == NULL || nwindows <= 0) {
        return Success;
    }

    /* Raise each window in order; the compositor may or may not honor this. */
    for (i = 0; i < nwindows; i++) {
        GLFWwindow *gw = WindowToGLFW(windows[i]);
        if (gw != NULL) {
            glfwFocusWindow(gw);
        }
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XChangeWindowAttributes --
 *
 *	Change one or more window attributes.
 *	We handle override-redirect (GLFW DECORATED hint) and the
 *	always-on-top semantic (GLFW FLOATING hint).  Other attributes
 *	such as background pixel, event mask, etc. are accepted
 *	silently; they are managed by Tk's own machinery or are not
 *	meaningful in Wayland.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	May change GLFW window attributes.
 *
 *----------------------------------------------------------------------
 */

int
XChangeWindowAttributes(
    TCL_UNUSED(Display *),
    Window                window,
    unsigned long         valuemask,
    XSetWindowAttributes *attributes)
{

    DEBUG_LOG("XChangeWindowAttributes: valuemask=0x%lx", valuemask);
    fflush(stderr);
    GLFWwindow *gw;

    if (attributes == NULL) {
        return Success;
    }

    gw = WindowToGLFW(window);
    if (gw == NULL) {
        return Success;
    }

    if (valuemask & CWOverrideRedirect) {
        glfwSetWindowAttrib(gw, GLFW_DECORATED,
            attributes->override_redirect ? GLFW_FALSE : GLFW_TRUE);
    }

    /* 
     * CWCursor is handled by UpdateCursor in tkPointer.c via TkpSetCursor.
     * CWBackPixel, CWBorderPixel, CWEventMask, CWColormap, …
     * All are maintained by Tk's own attribute tables; no GLFW action. 
     */

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWindowBackground --
 *
 *	Set the window background pixel.
 *	Background painting is done by NanoVG during drawing; we store
 *	no per-window background in GLFW.  Accept and return Success.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetWindowBackground(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(unsigned long))
{
    /* Background is drawn via NanoVG; no GLFW action needed. */
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWindowBackgroundPixmap --
 *
 *	Set the window background from a pixmap.
 *	Accepts ParentRelative and None values per Xlib semantics; actual
 *	background rendering goes through NanoVG.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetWindowBackgroundPixmap(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Pixmap))
{
    /* No-op; background is drawn via NanoVG. */
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWindowBorder --
 *
 *	Set the border color of a window.
 *	Borders are drawn by NanoVG; this call is a no-op.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetWindowBorder(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(unsigned long))
{
    /* Border painting is done via NanoVG. */
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWindowBorderPixmap --
 *
 *	Set the border from a pixmap.  No-op.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetWindowBorderPixmap(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Pixmap))
{
    /* Border painting is done via NanoVG. */
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetInputFocus --
 *
 *	Set keyboard input focus to a window.
 *	GLFW's glfwFocusWindow requests focus from the compositor; the
 *	Wayland protocol makes no guarantee that the compositor will
 *	honour the request.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Requests GLFW window focus.
 *
 *----------------------------------------------------------------------
 */

int
XSetInputFocus(
    TCL_UNUSED(Display *),
    Window focus,
    TCL_UNUSED(int),    /* revert_to */
    TCL_UNUSED(Time))   /* time      */
{
    GLFWwindow *gw;

    if (focus == None || focus == PointerRoot) {
        return Success;
    }

    gw = WindowToGLFW(focus);
    if (gw != NULL) {
        glfwFocusWindow(gw);
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWMName --
 *
 *	Set the WM_NAME property (window title) via an XTextProperty.
 *	We decode the text value and forward it to GLFW.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the GLFW window title.
 *
 *----------------------------------------------------------------------
 */

void
XSetWMName(
    TCL_UNUSED(Display *),
    Window        window,
    XTextProperty *text_prop)
{
    GLFWwindow *gw;
    const char *title;

    if (text_prop == NULL || text_prop->value == NULL) {
        return;
    }

    gw = WindowToGLFW(window);
    if (gw == NULL) {
        return;
    }

    title = (const char *)text_prop->value;
    glfwSetWindowTitle(gw, title);
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWMIconName --
 *
 *	Set the WM_ICON_NAME property.
 *	Wayland compositors do not expose a portable icon-name API;
 *	we accept the call for ICCCM compliance and do nothing.
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
XSetWMIconName(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(XTextProperty *))
{
    /* Icon names are not exposed via Wayland protocols; no-op. */
}

/*
 *----------------------------------------------------------------------
 *
 * XGetWindowAttributes --
 *
 *	Fills an XWindowAttributes structure with visual, depth, and
 *	screen settings matching the default display configuration.
 *
 *	This function is critical for core Tk operations (such as
 *	TkImgPhotoGet) which retrieve the geometry, visual properties,
 *	and depths of a window before drawing images. Returning an
 *	uninitialized or empty structure causes a crash in photo layout.
 *
 * Results:
 *	Returns 1 (True) on success, 0 (False) if attributes_return is NULL.
 *
 * Side effects:
 *	Populates the attributes_return structure with pointer references
 *	to the display's root visual structure and dimensions.
 *
 *----------------------------------------------------------------------
 */

int
XGetWindowAttributes(
    Display *display,
    TCL_UNUSED(Window), /* window */
    XWindowAttributes *attributes_return)
{
    if (attributes_return == NULL) {
        return 0;
    }

    /* Clear structure memory to avoid garbage pointer data. */
    memset(attributes_return, 0, sizeof(XWindowAttributes));

    /* Populate fields using screen definitions initialized in TkpOpenDisplay. */
    if (display != NULL && display->screens != NULL) {
        Screen *screen = &display->screens[display->default_screen];

        attributes_return->visual     = screen->root_visual;
        attributes_return->depth      = screen->root_depth;
        attributes_return->screen     = screen;
        attributes_return->width      = screen->width;
        attributes_return->height     = screen->height;
        attributes_return->root       = screen->root;
    } else {
        /* Safe fallback constants mirroring tkWaylandGC.c defaults. */
        static Visual fallbackVisual = {
            .visualid     = 1,
            .class        = TrueColor,
            .bits_per_rgb = 8,
            .map_entries  = 256,
            .red_mask     = 0xFF0000,
            .green_mask   = 0x00FF00,
            .blue_mask    = 0x0000FF
        };
        attributes_return->visual = &fallbackVisual;
        attributes_return->depth  = 24;
    }

    /*
     * Mock common default settings standard widgets look for
     * to prevent initialization failures.
     */
    attributes_return->map_state        = IsViewable;
    attributes_return->backing_store    = NotUseful;
    attributes_return->all_event_masks  = 0;
    attributes_return->your_event_mask = 0;

    return 1; /* Success */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpWindowIsDark --
 *
 *      Tests whether the given window is in "dark mode".
 *
 * Results:
 *      Returns a standard Tcl result code.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
TkpWindowIsDark(
    TCL_UNUSED(Tk_Window),
    bool *isdark)
{
    *isdark = false;
    return TCL_OK;
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
