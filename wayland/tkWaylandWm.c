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

/* Debugging.
#define DEBUG_CHANNEL stdout
#define DEBUG_LABEL "wm"
*/

#include "tkInt.h"
#include "tkPort.h"
#include "tkWaylandWm.h"
#include "tkWaylandInt.h"
#include "tkMenu.h"
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
 * Protocol identifiers - replace X11 Atoms for WM_DELETE_WINDOW etc.
 *
 *----------------------------------------------------------------------
 */

#define WM_DELETE_WINDOW    1
#define WM_TAKE_FOCUS       2
#define WM_SAVE_YOURSELF    3

/* General protocol name -> id table */
typedef struct ProtocolNameEntry {
    char *name;
    int   id;
    struct ProtocolNameEntry *nextPtr;
} ProtocolNameEntry;
static ProtocolNameEntry *protocolNameList = NULL;
static int nextProtocolId = 4;
static int WaylandGetProtocolId(const char *name){
    ProtocolNameEntry *e;
    for (e=protocolNameList;e;e=e->nextPtr) if (strcmp(e->name,name)==0) return e->id;
    if (strcmp(name,"WM_DELETE_WINDOW")==0) return WM_DELETE_WINDOW;
    if (strcmp(name,"WM_TAKE_FOCUS")==0) return WM_TAKE_FOCUS;
    if (strcmp(name,"WM_SAVE_YOURSELF")==0) return WM_SAVE_YOURSELF;
    e=(ProtocolNameEntry*)ckalloc(sizeof(ProtocolNameEntry));
    e->name=ckalloc(strlen(name)+1); strcpy(e->name,name); e->id=nextProtocolId++; e->nextPtr=protocolNameList; protocolNameList=e; return e->id;
}
static const char* WaylandGetProtocolName(int id){
    ProtocolNameEntry *e;
    if (id==WM_DELETE_WINDOW) return "WM_DELETE_WINDOW";
    if (id==WM_TAKE_FOCUS) return "WM_TAKE_FOCUS";
    if (id==WM_SAVE_YOURSELF) return "WM_SAVE_YOURSELF";
    for (e=protocolNameList;e;e=e->nextPtr) if (e->id==id) return e->name;
    return NULL;
}


/* Window-state constants (X11 compatible). */
#define WithdrawnState  0
#define NormalState     1
#define IconicState     3

/* Gravity constants. */
#define NorthWestGravity 1
#define StaticGravity   10

/* Global toplevel list. */
static WmInfo *firstWmPtr = NULL;

/* The positions of the top and bottom windows in the stacking order. */
static double stackTop = 0.0, stackBottom = 0.0;

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
static void CancelGeometryUpdate(TkWindow *winPtr);
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
static void             SendUnmapNotify(TkWindow *winPtr);
static void             RaiseTransients(TkWindow *winPtr);

/* GLFW integration helpers. */
static void InitializeGlfwWindow(TkWindow *winPtr);
static int IsPopupMenuWindow(TkWindow *winPtr);
static void DestroyGlfwWindow(TkWindow *winPtr);
static void ApplyFullscreenState(TkWindow *winPtr);
static void WmPixelSize(TkWindow *winPtr, int *widthPtr, int *heightPtr);
static void WmPixelLimits(TkWindow *winPtr, int *minWidthPtr,
			    int *minHeightPtr, int *maxWidthPtr,
			    int *maxHeightPtr);

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
 * IsPopupMenuWindow --
 *
 *	Checks whether a toplevel is a menu that is displayed in an
 *	xdg_popup surface (or in the menubar) rather than in a GLFW window
 *	of its own.  Tear-off menus are shown as ordinary toplevels.
 *
 * Results:
 *	1 for menu and menubar windows other than tear-off menus, 0
 *	otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
IsPopupMenuWindow(
    TkWindow *winPtr)
{
    TkMenuReferences *menuRefPtr;

    if (winPtr->classUid != Tk_GetUid("Menu") &&
	    winPtr->classUid != Tk_GetUid("Menubar")) {
	return 0;
    }
    if (winPtr->mainPtr && winPtr->mainPtr->interp && winPtr->pathName) {
	menuRefPtr = TkFindMenuReferences(winPtr->mainPtr->interp,
		winPtr->pathName);
	if (menuRefPtr && menuRefPtr->menuPtr
		&& menuRefPtr->menuPtr->tkwin == (Tk_Window) winPtr
		&& menuRefPtr->menuPtr->menuType == TEAROFF_MENU) {
	    return 0;
	}
    }
    return 1;
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


/*
 * Helper to safely destroy a toplevel from an idle callback (avoids
 * re-entrancy inside GLFW callbacks).
 */
static void
WaylandDoDestroyWindow(void *clientData)
{
    TkWindow *winPtr = (TkWindow *)clientData;
    if (winPtr == NULL) {
        return;
    }
    if (winPtr->flags & TK_ALREADY_DEAD) {
        return;
    }
    Tk_DestroyWindow((Tk_Window)winPtr);
}

/*
 * Generic protocol dispatcher – finds the handler for 'protocol'
 * in wmPtr->protPtr and evaluates its Tcl command in its interpreter.
 * Returns 1 if a handler was found and evaluated, 0 otherwise.
 */
static int
TkWaylandHandleProtocol(WmInfo *wmPtr, int protocol)
{
    ProtocolHandler *protPtr;
    for (protPtr = wmPtr->protPtr; protPtr != NULL; protPtr = protPtr->nextPtr) {
        if (protPtr->protocol == protocol) {
            Tcl_Interp *interp = protPtr->interp;
            if (interp == NULL) {
                continue;
            }
            Tcl_Preserve((ClientData)interp);
            Tcl_Preserve((ClientData)wmPtr->winPtr);
            int result = Tcl_EvalEx(interp, protPtr->command, -1, TCL_EVAL_GLOBAL);
            if (result != TCL_OK) {
                Tcl_BackgroundError(interp);
            }
            Tcl_Release((ClientData)wmPtr->winPtr);
            Tcl_Release((ClientData)interp);
            return 1;
        }
    }
    return 0;
}

static void
InitializeGlfwWindow(TkWindow *winPtr)
{

    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    DEBUG_LOG("InitializeGlfwWindow: %s", Tk_PathName(winPtr));
    if (!glfwWindow) {
        /* Do not panic - menu/clipboard toplevels intentionally have no GLFW window,
         * and during destroy sequences (event-9.1) the window may already be dead.
         * Just return gracefully. */
        DEBUG_LOG("InitializeGlfwWindow: no platform window for %s, skipping", Tk_PathName(winPtr));
        return;
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

    /*
     * GLFW callbacks, including the close callback that dispatches
     * "wm protocol . WM_DELETE_WINDOW <cmd>" via TkWmProtocolEventProc,
     * are installed in tkWaylandNotify.c.
     */
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
 * QueueVisibilityNotify --
 *
 *	Synthesize a VisibilityNotify event for a window and, recursively,
 *	for each of its mapped descendants.  Wayland has no equivalent of
 *	the X11 VisibilityNotify event: the compositor never reports
 *	obscured/unobscured state to clients, and GLFW exposes no callback
 *	for it.  Tk's generic [tkwait visibility] command and any
 *	<Visibility> bindings therefore have nothing to latch onto unless
 *	we manufacture the event here.

 *	The synthesized event uses VisibilityUnobscured, which is what
 *	[tkwait visibility] accepts as "visible".  The event is queued
 *	with TCL_QUEUE_TAIL so that it lands after any events already
 *	pending at the time of the call.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues one VisibilityNotify XEvent per window in the
 *	subtree rooted at winPtr. Advances the display's request serial 
 *  once per event.
 *
 *----------------------------------------------------------------------
 */


static void
QueueVisibilityNotify(TkWindow *winPtr) {
    if (winPtr == NULL) return;
	if (!(winPtr->flags & TK_MAPPED)) return;
    XEvent event;
    memset(&event, 0, sizeof(XEvent));
    event.type = VisibilityNotify;
    event.xvisibility.serial = LastKnownRequestProcessed(winPtr->display)++;
    event.xvisibility.send_event = False;
    event.xvisibility.display = winPtr->display;
    event.xvisibility.window = Tk_WindowId((Tk_Window)winPtr);
    event.xvisibility.state = VisibilityUnobscured;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

    /* Recurse to mapped children safely. */
    TkWindow *child;
    for (child = winPtr->childList; child != NULL; child = child->nextPtr) {
        if (Tk_IsMapped((Tk_Window)child) || (child->flags & TK_MAPPED)) {
            QueueVisibilityNotify(child);
        }
    }
}

/*
 * Queue Expose for a window and all mapped descendants.
 * This is needed on Wayland because the first toplevel Expose is queued
 * in TkWmMapWindow before pack/grid has mapped the children.  Without this
 * the toplevel draws empty and never redraws children.
 */
static void
QueueExposeTree(TkWindow *winPtr)
{
    TkWindow *child;
    if (winPtr == NULL) return;
    TkWaylandQueueExposeEvent(winPtr, 0, 0,
        Tk_Width(winPtr), Tk_Height(winPtr));
    for (child = winPtr->childList; child != NULL; child = child->nextPtr) {
        if (child->flags & TK_MAPPED) {
            QueueExposeTree(child);
        }
    }
}

static void
ReExposeIdle(void *clientData)
{
    TkWindow *winPtr = (TkWindow *)clientData;
    if (winPtr == NULL) return;
    if (winPtr->flags & TK_ALREADY_DEAD) return;
    if (!(winPtr->flags & TK_TOP_LEVEL)) return;
    /* Re-queue exposes after geometry managers have run. */
    QueueExposeTree(winPtr);
    if (winPtr->privatePtr) {
        glfwTkInfo *infoPtr = winPtr->privatePtr->glfwWindow ?
            (glfwTkInfo*)glfwGetWindowUserPointer(winPtr->privatePtr->glfwWindow) : NULL;
        if (infoPtr) {
            infoPtr->flags |= TKWL_NEEDS_DISPLAY;
            /* Allow BeginDraw to queue another early expose if needed */
            infoPtr->flags &= ~TKWL_EARLY_EXPOSE_QUEUED;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Override-redirect toplevels --
 *
 *	GLFW cannot create a toplevel without compositor-managed
 *	placement, so a toplevel with the override-redirect attribute
 *	("wm overrideredirect $w 1") is shown as a wl_subsurface of its
 *	transient master (or, failing that, of the main window) using the
 *	API in tkWaylandPopup.c.
 *
 *	The hidden GLFW window created by Tk_MakeWindow is kept, never
 *	shown, as the owner of the window's backing store.  Widgets draw
 *	into that backing store exactly as in any other toplevel (so the
 *	clipping, font and NanoVG machinery is shared); the hidden window
 *	is kept the same size as the popup so that the backing store has
 *	the right size, and TkWaylandDisplayAllWindows copies the backing
 *	store to the popup with TkWaylandPopupPresentPixels.
 *
 *	The popup has an empty input region, so the pointer events over it
 *	are delivered to the parent window.  The pointer callbacks in
 *	tkWaylandNotify.c find the target widget with
 *	TkWaylandPointerTarget, which looks at the popups first.
 *
 *	Coordinates given with "wm geometry" are relative to the origin of
 *	the parent window, since a Wayland client does not know screen
 *	coordinates; as with menus, root coordinates are in the space of
 *	the parent.
 *
 *----------------------------------------------------------------------
 */

typedef struct OverrideInfo {
    TkWaylandPopup *popup;
    GLFWwindow *parent;		/* GLFW window the subsurface is attached to. */
} OverrideInfo;

static Tcl_HashTable overridePopupTable;
static int overridePopupTableReady = 0;

/*
 *----------------------------------------------------------------------
 *
 * GetOverrideInfo --
 *
 *	Returns the override-redirect bookkeeping record for a toplevel,
 *	or NULL if the window is not (yet) backed by a popup.
 *
 * Results:
 *	A pointer to the OverrideInfo for winPtr, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static OverrideInfo *
GetOverrideInfo(
    TkWindow *winPtr)
{
    Tcl_HashEntry *hPtr;

    if (!overridePopupTableReady) {
	return NULL;
    }
    hPtr = Tcl_FindHashEntry(&overridePopupTable, (char *) winPtr);
    return hPtr ? (OverrideInfo *) Tcl_GetHashValue(hPtr) : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * GetOverridePopup --
 *
 *	Returns the wl_subsurface popup backing an override-redirect
 *	toplevel.
 *
 * Results:
 *	The TkWaylandPopup for winPtr, or NULL if the window has no popup.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkWaylandPopup *
GetOverridePopup(
    TkWindow *winPtr)
{
    OverrideInfo *info = GetOverrideInfo(winPtr);

    return info ? info->popup : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * SetOverrideInfo --
 *
 *	Records, or forgets, the popup backing an override-redirect
 *	toplevel.  Passing NULL for popup removes the entry and frees the
 *	bookkeeping record.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May create the overridePopupTable hash table, allocate or free an
 *	OverrideInfo, and modify the hash table.
 *
 *----------------------------------------------------------------------
 */

static void
SetOverrideInfo(
    TkWindow *winPtr,
    TkWaylandPopup *popup,		/* NULL removes the entry. */
    GLFWwindow *parent)
{
    Tcl_HashEntry *hPtr;
    OverrideInfo *info;
    int isNew;

    if (!overridePopupTableReady) {
	Tcl_InitHashTable(&overridePopupTable, TCL_ONE_WORD_KEYS);
	overridePopupTableReady = 1;
    }
    if (popup == NULL) {
	hPtr = Tcl_FindHashEntry(&overridePopupTable, (char *) winPtr);
	if (hPtr) {
	    ckfree(Tcl_GetHashValue(hPtr));
	    Tcl_DeleteHashEntry(hPtr);
	}
	return;
    }
    hPtr = Tcl_CreateHashEntry(&overridePopupTable, (char *) winPtr, &isNew);
    if (isNew) {
	info = (OverrideInfo *) ckalloc(sizeof(OverrideInfo));
	Tcl_SetHashValue(hPtr, info);
    } else {
	info = (OverrideInfo *) Tcl_GetHashValue(hPtr);
    }
    info->popup = popup;
    info->parent = parent;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmGetOverridePopup --
 *
 *	Accessor for the drawing code (tkWaylandInit.c): the popup backing
 *	an override-redirect toplevel, or NULL for an ordinary window.
 *
 * Results:
 *	The TkWaylandPopup for winPtr, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkWaylandPopup *
TkWaylandWmGetOverridePopup(
    TkWindow *winPtr)
{
    return winPtr ? GetOverridePopup(winPtr) : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * UsesOverridePopup --
 *
 *	Returns 1 if the toplevel has the override-redirect attribute and
 *	is not a menu or an embedded window.
 *
 * Results:
 *	1 if winPtr should be shown as a popup, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
UsesOverridePopup(
    TkWindow *winPtr)
{
    if (!Tk_IsTopLevel(winPtr) || Tk_IsEmbedded(winPtr)
	    || winPtr->wmInfoPtr == NULL || IsPopupMenuWindow(winPtr)) {
	return 0;
    }
    return Tk_Attributes((Tk_Window) winPtr)->override_redirect ? 1 : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * OverridePopupParent --
 *
 *	Finds the GLFW window that the popup is attached to: the master of
 *	"wm transient" if it has a window on screen, otherwise the main
 *	window of the application.
 *
 * Results:
 *	The GLFW window to attach the popup to, or NULL if there is none.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static GLFWwindow *
OverridePopupParent(
    TkWindow *winPtr)
{
    WmInfo *wmPtr = (WmInfo *) winPtr->wmInfoPtr;
    TkWindow *candidate;
    GLFWwindow *gw = NULL;

    candidate = wmPtr->containerPtr;
    if (candidate != NULL && candidate != winPtr
	    && !UsesOverridePopup(candidate)) {
	gw = TkWaylandGetGLFWwindow(candidate);
	if (gw != NULL && !glfwGetWindowAttrib(gw, GLFW_VISIBLE)) {
	    gw = NULL;
	}
    }
    if (gw == NULL && winPtr->mainPtr != NULL) {
	candidate = winPtr->mainPtr->winPtr;
	if (candidate != NULL && candidate != winPtr
		&& !UsesOverridePopup(candidate)) {
	    gw = TkWaylandGetGLFWwindow(candidate);
	    if (gw != NULL && !glfwGetWindowAttrib(gw, GLFW_VISIBLE)) {
		gw = NULL;
	    }
	}
    }
    return gw;
}

/*
 *----------------------------------------------------------------------
 *
 * OverrideBacked --
 *
 *	True if the window is, or will be when mapped, shown as a popup.
 *	The Xlib emulation uses it to keep its hands off the hidden GLFW
 *	window (showing, focusing or iconifying it would map a real
 *	toplevel).
 *
 * Results:
 *	1 if winPtr is or will be backed by a popup, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
OverrideBacked(
    TkWindow *winPtr)
{
    return winPtr != NULL && (GetOverrideInfo(winPtr) != NULL
	    || (UsesOverridePopup(winPtr)
	    && OverridePopupParent(winPtr) != NULL));
}

/*
 *----------------------------------------------------------------------
 *
 * IsOverrideGLFW --
 *
 *	True if the GLFW window belongs to a Tk window that is backed by
 *	an override-redirect popup.
 *
 * Results:
 *	1 if gw is the hidden GLFW window of a popup-backed toplevel,
 *	0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
IsOverrideGLFW(
    GLFWwindow *gw)
{
    return gw != NULL && OverrideBacked(TkWaylandGetTkWindow(gw));
}

/*
 *----------------------------------------------------------------------
 *
 * OverridePopupRect --
 *
 *	Computes the position and size of the popup in the coordinates of
 *	its parent.  The size is the size set with "wm geometry", or else
 *	the requested size, within "wm minsize"/"wm maxsize"; negative
 *	positions ("-x-y") are measured from the right and bottom edges of
 *	the parent, as on X11.
 *
 * Results:
 *	Stores the parent-relative x and y position and the width and
 *	height through xPtr, yPtr, wPtr and hPtr.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
OverridePopupRect(
    TkWindow *winPtr,
    GLFWwindow *parent,
    int *xPtr,
    int *yPtr,
    int *wPtr,
    int *hPtr)
{
    WmInfo *wmPtr = (WmInfo *) winPtr->wmInfoPtr;
    int w, h, pw = 0, ph = 0;

    WmPixelSize(winPtr, &w, &h);
    if (w <= 0) {
        w = winPtr->reqWidth;
    }
    if (h <= 0) {
        h = winPtr->reqHeight;
    }
    if (wmPtr->maxWidth > 0 && w > wmPtr->maxWidth) {
        w = wmPtr->maxWidth;
    }
    if (wmPtr->maxHeight > 0 && h > wmPtr->maxHeight) {
        h = wmPtr->maxHeight;
    }
    if (w < wmPtr->minWidth) {
        w = wmPtr->minWidth;
    }
    if (h < wmPtr->minHeight) {
        h = wmPtr->minHeight;
    }
    if (w < 1) {
        w = 1;
    }
    if (h < 1) {
        h = 1;
    }
    glfwGetWindowSize(parent, &pw, &ph);

    /*
     * wm overrideredirect must ALWAYS be placed in local relative
     * coordinates, just like menus. See tkWaylandMenu.c:TkWaylandPostMenuAtAnchor
     * which takes anchorX/Y in toplevel-surface-local and does postX=anchorX.
     * On this port winfo rootx/pointerx already return local (parentRoot==0),
     * so wmPtr->x/y are local. Always use them directly.
     * ResizeOverridePopup() calls this, so moving mapped window via
     * "wm geometry +X+Y" will reconfigure subsurface.
     */

    if (wmPtr->flags & WM_NEGATIVE_X) {
        *xPtr = pw - w - wmPtr->x;
    } else {
        *xPtr = wmPtr->x;
    }
    if (wmPtr->flags & WM_NEGATIVE_Y) {
        *yPtr = ph - h - wmPtr->y;
    } else {
        *yPtr = wmPtr->y;
    }

    if (pw > 0 && ph > 0) {
        if (*xPtr + w > pw) {
            *xPtr = pw - w;
        }
        if (*xPtr < 0) {
            *xPtr = 0;
        }
        if (*yPtr + h > ph) {
            *yPtr = ph - h;
        }
        if (*yPtr < 0) {
            *yPtr = 0;
        }
    }

    *wPtr = w;
    *hPtr = h;

    DEBUG_LOG("OverridePopupRect: local=(%d,%d) size=%dx%d pw,ph=%dx%d wm=(%d,%d) neg=0x%x",
              *xPtr, *yPtr, w, h, pw, ph, wmPtr->x, wmPtr->y,
              wmPtr->flags & (WM_NEGATIVE_X|WM_NEGATIVE_Y));
}

/*
 *----------------------------------------------------------------------
 *
 * SyncBackingStoreSize --
 *
 *	Resizes the hidden GLFW window of a popup toplevel to the size of
 *	the popup.  GLFW's framebuffer-size callback then rebuilds the
 *	backing store and gives Tk its ConfigureNotify, exactly as for a
 *	resized ordinary toplevel.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May resize the hidden GLFW window and update winPtr's requested
 *	width and height.
 *
 *----------------------------------------------------------------------
 */

static void
SyncBackingStoreSize(
    TkWindow *winPtr,
    int width,
    int height)
{
    GLFWwindow *gw = TkWaylandGetGLFWwindow(winPtr);
    int cw = 0, ch = 0;

    if (gw == NULL) {
	return;
    }
    glfwGetWindowSize(gw, &cw, &ch);
    if (cw != width || ch != height) {
	TkWaylandFlushConfigures();
	glfwSetWindowSize(gw, width, height);
    }
    winPtr->changes.width = width;
    winPtr->changes.height = height;
}

/*
 *----------------------------------------------------------------------
 *
 * MarkNeedsDisplay --
 *
 *	Asks TkWaylandDisplayAllWindows to present the backing store.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the TKWL_NEEDS_DISPLAY and TKWL_SHOWN flags in the window's
 *	glfwTkInfo and clears TKWL_EARLY_EXPOSE_QUEUED.
 *
 *----------------------------------------------------------------------
 */

static void
MarkNeedsDisplay(
    TkWindow *winPtr)
{
    glfwTkInfo *infoPtr;

    if (winPtr->privatePtr && winPtr->privatePtr->glfwWindow) {
	infoPtr = (glfwTkInfo *)
		glfwGetWindowUserPointer(winPtr->privatePtr->glfwWindow);
	if (infoPtr) {
	    infoPtr->flags |= TKWL_NEEDS_DISPLAY | TKWL_SHOWN;
	    infoPtr->flags &= ~TKWL_EARLY_EXPOSE_QUEUED;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RaiseOverridePopup --
 *
 *	Stacks the popup above the other popups of the same parent, or
 *	directly above the parent if there are none.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Reorders the wl_subsurface stack through
 *	TkWaylandSubsurfacePlaceAbove.
 *
 *----------------------------------------------------------------------
 */

static void
RaiseOverridePopup(
    TkWindow *winPtr)
{
    OverrideInfo *info = GetOverrideInfo(winPtr), *other, *top = NULL;
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    double topPos = 0;

    if (info == NULL) {
	return;
    }
    for (hPtr = Tcl_FirstHashEntry(&overridePopupTable, &search);
	    hPtr != NULL; hPtr = Tcl_NextHashEntry(&search)) {
	TkWindow *w = (TkWindow *) Tcl_GetHashKey(&overridePopupTable, hPtr);

	other = (OverrideInfo *) Tcl_GetHashValue(hPtr);
	if (w == winPtr || other->parent != info->parent
		|| w->wmInfoPtr == NULL) {
	    continue;
	}
	if (top == NULL || ((WmInfo *) w->wmInfoPtr)->stackPos > topPos) {
	    top = other;
	    topPos = ((WmInfo *) w->wmInfoPtr)->stackPos;
	}
    }
    TkWaylandSubsurfacePlaceAbove(info->popup, top ? top->popup : NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPointerTarget --
 *
 *	Replacement for Tk_CoordsToWindow in the pointer callbacks.  The
 *	coordinates are in the space of the toplevel window tkwin; if a
 *	mapped override-redirect popup of that toplevel covers the point
 *	(the topmost one wins) the widget in the popup is found,
 *	otherwise the widget in tkwin itself.
 *
 * Results:
 *	The Tk_Window containing the given point.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE Tk_Window
TkWaylandPointerTarget(
    int x,
    int y,
    Tk_Window tkwin)
{
    TkWindow *top = (TkWindow *) tkwin, *best = NULL;
    GLFWwindow *gw = (top != NULL) ? TkWaylandGetGLFWwindow(top) : NULL;
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    double bestPos = 0;
    int bx = 0, by = 0;

    if (gw != NULL && overridePopupTableReady) {
	for (hPtr = Tcl_FirstHashEntry(&overridePopupTable, &search);
		hPtr != NULL; hPtr = Tcl_NextHashEntry(&search)) {
	    TkWindow *w = (TkWindow *) Tcl_GetHashKey(&overridePopupTable,
		    hPtr);
	    OverrideInfo *info = (OverrideInfo *) Tcl_GetHashValue(hPtr);
	    int px, py, pw, ph;

	    if (info->parent != gw || !(w->flags & TK_MAPPED)
		    || w->wmInfoPtr == NULL) {
		continue;
	    }
	    TkWaylandPopupGetPosition(info->popup, &px, &py);
	    TkWaylandPopupGetSize(info->popup, &pw, &ph);
	    if (x >= px && y >= py && x < px + pw && y < py + ph
		    && (best == NULL
		    || ((WmInfo *) w->wmInfoPtr)->stackPos > bestPos)) {
		best = w;
		bestPos = ((WmInfo *) w->wmInfoPtr)->stackPos;
		bx = px;
		by = py;
	    }
	}
    }
    if (best != NULL) {
	return Tk_CoordsToWindow(x - bx, y - by, (Tk_Window) best);
    }
    return Tk_CoordsToWindow(x, y, tkwin);
}

/*
 *----------------------------------------------------------------------
 *
 * MapOverridePopup --
 *
 *	Shows an override-redirect toplevel as a subsurface.
 *
 * Results:
 *	1 if the popup was created and the window is mapped, 0 if there is
 *	no parent window or the popup could not be created, in which case
 *	the caller falls back to the ordinary GLFW path.
 *
 * Side effects:
 *	Creates the popup, marks the window mapped, and queues the events
 *	and exposes that TkWmMapWindow produces for ordinary toplevels.
 *
 *----------------------------------------------------------------------
 */

static int
MapOverridePopup(
    TkWindow *winPtr)
{
    WmInfo *wmPtr = (WmInfo *) winPtr->wmInfoPtr;
    GLFWwindow *parent;
    TkWaylandPopup *popup;
    XEvent mapEvent;
    int x, y, w, h;

    if (GetOverridePopup(winPtr) != NULL) {
	return 1;
    }
    parent = OverridePopupParent(winPtr);
    if (parent == NULL) {
	DEBUG_LOG("MapOverridePopup: no parent for %s", Tk_PathName(winPtr));
	return 0;
    }
    OverridePopupRect(winPtr, parent, &x, &y, &w, &h);

    /*
     * Position first: the ConfigureNotify generated when the backing
     * store is resized reports changes.x/y, and the root coordinates of
     * the widgets include them.
     */
    winPtr->changes.x = x;
    winPtr->changes.y = y;
    SyncBackingStoreSize(winPtr, w, h);

    popup = TkWaylandSubsurfaceCreate(parent, x, y, w, h);
    if (popup == NULL) {
	DEBUG_LOG("MapOverridePopup: subsurface creation failed for %s",
		Tk_PathName(winPtr));
	return 0;
    }

    /* The widgets draw their own borders; no popup frame or shadow. */
    TkWaylandPopupSetBorder(popup, 0, 0, 0, 0, 0, 0);
    SetOverrideInfo(winPtr, popup, parent);

    winPtr->flags |= TK_MAPPED;
    wmPtr->stackPos = ++stackTop;
    RaiseOverridePopup(winPtr);

    memset(&mapEvent, 0, sizeof(XEvent));
    mapEvent.type = MapNotify;
    mapEvent.xmap.serial = LastKnownRequestProcessed(winPtr->display)++;
    mapEvent.xmap.send_event = False;
    mapEvent.xmap.display = winPtr->display;
    mapEvent.xmap.event = Tk_WindowId((Tk_Window) winPtr);
    mapEvent.xmap.window = Tk_WindowId((Tk_Window) winPtr);
    mapEvent.xmap.override_redirect = True;
    Tk_HandleEvent(&mapEvent);

    QueueExposeTree(winPtr);
    Tcl_DoWhenIdle(ReExposeIdle, (void *) winPtr);
    MarkNeedsDisplay(winPtr);
    QueueVisibilityNotify(winPtr);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * UnmapOverridePopup --
 *
 *	Hides an override-redirect toplevel.  A wl_subsurface cannot be
 *	hidden, so the popup is destroyed and created again on the next
 *	map.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Clears TK_MAPPED, destroys the popup, removes its bookkeeping
 *	record, and sends an UnmapNotify if the window was mapped.
 *
 *----------------------------------------------------------------------
 */

static void
UnmapOverridePopup(
    TkWindow *winPtr)
{
    TkWaylandPopup *popup = GetOverridePopup(winPtr);
    int wasMapped = (winPtr->flags & TK_MAPPED) != 0;

    winPtr->flags &= ~TK_MAPPED;
    if (popup != NULL) {
	SetOverrideInfo(winPtr, NULL, NULL);
	TkWaylandPopupDestroy(popup);
    }
    if (wasMapped && !(winPtr->flags & TK_ALREADY_DEAD)) {
	SendUnmapNotify(winPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyOverridePopup --
 *
 *	Releases the popup of a window which is being destroyed.  Also
 *	unmaps the popups attached to it, whose parent surface is about to
 *	go away.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Unmaps the window's own popup and all popups whose parent is the
 *	window's GLFW window.
 *
 *----------------------------------------------------------------------
 */

static void
DestroyOverridePopup(
    TkWindow *winPtr)
{
    GLFWwindow *gw = TkWaylandGetGLFWwindow(winPtr);
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    TkWindow *victim;

    UnmapOverridePopup(winPtr);
    while (gw != NULL && overridePopupTableReady) {
	victim = NULL;
	for (hPtr = Tcl_FirstHashEntry(&overridePopupTable, &search);
		hPtr != NULL; hPtr = Tcl_NextHashEntry(&search)) {
	    if (((OverrideInfo *) Tcl_GetHashValue(hPtr))->parent == gw) {
		victim = (TkWindow *) Tcl_GetHashKey(&overridePopupTable,
			hPtr);
		break;
	    }
	}
	if (victim == NULL) {
	    break;
	}
	UnmapOverridePopup(victim);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ResizeOverridePopup --
 *
 *	Moves and resizes the popup of a mapped override-redirect toplevel
 *	to the given size, at the position from "wm geometry".
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Reconfigures the wl_subsurface, resizes the backing store, and
 *	queues exposes and a display.  Generates a ConfigureNotify if the
 *	popup moved.
 *
 *----------------------------------------------------------------------
 */

static void
ResizeOverridePopup(
    TkWindow *winPtr,
    int width,
    int height)
{
    OverrideInfo *info = GetOverrideInfo(winPtr);
    int x, y, w, h, cx, cy, cw, ch;

    if (info == NULL) {
	return;
    }
    OverridePopupRect(winPtr, info->parent, &x, &y, &w, &h);
    w = (width < 1) ? 1 : width;
    h = (height < 1) ? 1 : height;

    TkWaylandPopupGetPosition(info->popup, &cx, &cy);
    TkWaylandPopupGetSize(info->popup, &cw, &ch);
    if (cx == x && cy == y && cw == w && ch == h) {
	return;
    }
    winPtr->changes.x = x;
    winPtr->changes.y = y;
    SyncBackingStoreSize(winPtr, w, h);
    TkWaylandSubsurfaceReconfigure(info->popup, x, y, w, h);
    if (cx != x || cy != y) {
	/* Moved: the root coordinates of the widgets changed. */
	TkDoConfigureNotify(winPtr);
    }
    QueueExposeTree(winPtr);
    MarkNeedsDisplay(winPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmMapWindow --
 *
 *	Called by Tk_MapWindow when mapping a toplevel.  Tk_MapWindow
 *  immediately handles a MapNotify event when this returns.
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
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    if (!wmPtr) {
        DEBUG_LOG("TkWmMapWindow: No WmInfo for %s, skipping", Tk_PathName(winPtr));
        return;
    }

    /* Do not map a transient for the first time if its container is not. */
    if ((wmPtr->flags & WM_NEVER_MAPPED) && wmPtr->containerPtr != NULL
	    && !Tk_IsMapped(wmPtr->containerPtr)) {
	wmPtr->withdrawn = 1;
	wmPtr->initialState = WithdrawnState;
    }

    /* Respect "wm withdraw ." — do not force visibility. */
    if (wmPtr->withdrawn || wmPtr->initialState == WithdrawnState) {
        DEBUG_LOG("TkWmMapWindow: %s is withdrawn, not showing",
                  Tk_PathName(winPtr));
	/* Tk tried to map it: it must be mapped when it is deiconified. */
	wmPtr->flags |= WM_MAP_DEFERRED;
        return;
    }
    wmPtr->flags &= ~WM_MAP_DEFERRED;

    wmPtr->initialState = NormalState;
    wmPtr->flags &= ~WM_NEVER_MAPPED;

    /*
     * Override-redirect toplevels are subsurfaces, not GLFW windows.  If
     * no popup can be made, fall through to the ordinary path.
     */
    if (UsesOverridePopup(winPtr) && MapOverridePopup(winPtr)) {
	return;
    }

    if (!Tk_IsEmbedded(winPtr)) {
        /* Menu/clipboard toplevels have no GLFW window - don't init. */
        if (IsPopupMenuWindow(winPtr)) {
            DEBUG_LOG("TkWmMapWindow: %s is menu/menubar, skipping GLFW init", Tk_PathName(winPtr));
        } else {
            InitializeGlfwWindow(winPtr);
            UpdateHints(winPtr);
            UpdateTitle(winPtr);
            UpdatePhotoIcon(winPtr);
        }
    }
    /* Re-fetch AFTER Initialize - first map has NULL before init (bug causing visibility hang). */
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    if (glfwWindow) {
        winPtr->flags |= TK_MAPPED;
	wmPtr->stackPos = ++stackTop;

        /*
         * Give the window its requested size before it is shown for the
         * first time.  The GLFW window may have been created with the
         * default size before the content requested its size (a ttk
         * widget creates the window of its toplevel at once), and a resize
         * after it has been shown can be overridden by the first configure
         * of the compositor.
         */
        if (!glfwGetWindowAttrib(glfwWindow, GLFW_VISIBLE)) {
            ApplyPendingGeometry(winPtr);
        }
        UpdateGeometryInfo(winPtr);
        DEBUG_LOG("TkWmMapWindow: Showing %s", Tk_PathName(winPtr));
        glfwShowWindow(glfwWindow);

        /*
         * glfwShowWindow() waits for the compositor to configure the window,
         * so a geometry change postponed while the window was hidden (e.g.
         * made while it was withdrawn) can be applied now, synchronously as
         * on X11.
         */
        if (wmPtr->flags & WM_UPDATE_PENDING) {
            CancelGeometryUpdate(winPtr);
            UpdateGeometryInfo(winPtr);
        }

        /*
         * Dispatch a MapNotify event synchronously so any
         * resulting cascade (e.g. pack mapping child frames, which in turn
         * generates their own MapNotify synchronously too) completes
         * before TkWmMapWindow returns -- matching generic Tk_MapWindow's
         * own synchronous behavior for non-toplevels.
         */
        XEvent mapEvent;
        memset(&mapEvent, 0, sizeof(XEvent));
        mapEvent.type = MapNotify;
        mapEvent.xmap.serial = LastKnownRequestProcessed(winPtr->display)++;
        mapEvent.xmap.send_event = False;
        mapEvent.xmap.display = winPtr->display;
        mapEvent.xmap.event = Tk_WindowId((Tk_Window)winPtr);
        mapEvent.xmap.window = Tk_WindowId((Tk_Window)winPtr);
        mapEvent.xmap.override_redirect = winPtr->atts.override_redirect;
        Tk_HandleEvent(&mapEvent);

        /*
         * Tk_MapWindow never calls XMapWindow for a toplevel, so nothing
         * else asks the toplevel itself to redraw after it is shown.  The
         * Expose queued at creation time is consumed while the window is
         * still unmapped (DisplayFrame drops it), so queue a fresh one now.
         * This also makes an empty root produce a first frame to present.
         *
         * Child widgets may not yet be mapped when this first Expose
         * runs (pack/grid runs as idle tasks after MapNotify).  Queue exposes
         * for the whole subtree now, and schedule a second re-expose idle
         * after the geometry managers have had a chance to map children.
         */
        QueueExposeTree(winPtr);
        /* Second pass after idle handlers (pack/grid) */
        Tcl_DoWhenIdle(ReExposeIdle, (void*)winPtr);
        /* Ensure we will swap buffers even if BeginDraw early-exits */
        if (winPtr->privatePtr && winPtr->privatePtr->glfwWindow) {
            glfwTkInfo *infoPtr = (glfwTkInfo*)glfwGetWindowUserPointer(winPtr->privatePtr->glfwWindow);
            if (infoPtr) {
                infoPtr->flags |= TKWL_NEEDS_DISPLAY | TKWL_SHOWN;
                infoPtr->flags &= ~TKWL_EARLY_EXPOSE_QUEUED;
            }
        }
    }
    /*
     * Wayland has no VisibilityNotify equivalent — the compositor never
     * reports obscured/unobscured state to clients, and GLFW has no
     * callback for it either. Since we already treat GLFW visibility as
     * synchronous (see WaitForMapNotify), synthesize the event here so
     * that [tkwait visibility] and any <Visibility> bindings behave as
     * they would on X11.
     */
    QueueVisibilityNotify(winPtr);
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
    if (GetOverridePopup(winPtr) != NULL) {
	UnmapOverridePopup(winPtr);
	return;
    }
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    int wasMapped = (winPtr->flags & TK_MAPPED) != 0;

    winPtr->flags &= ~TK_MAPPED;
    if (glfwWindow) {
        glfwHideWindow(glfwWindow);
    }
    if (wasMapped) {
	SendUnmapNotify(winPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SendUnmapNotify --
 *
 *	Dispatch an UnmapNotify event for a toplevel which was withdrawn or
 *	iconified, as an X server does, so that its event handlers (for
 *	example of its transients) and <Unmap> bindings see it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Handles the event.
 *
 *----------------------------------------------------------------------
 */

static void
SendUnmapNotify(
    TkWindow *winPtr)
{
    XEvent event;

    memset(&event, 0, sizeof(XEvent));
    event.type = UnmapNotify;
    event.xunmap.serial = LastKnownRequestProcessed(winPtr->display);
    event.xunmap.display = winPtr->display;
    event.xunmap.event = Tk_WindowId((Tk_Window)winPtr);
    event.xunmap.window = Tk_WindowId((Tk_Window)winPtr);
    Tk_HandleEvent(&event);
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

    /* A deferred setup of the menubar must not run for a dead window. */
    TkWaylandMenubarForget(winPtr);

    /* Release the subsurface of an override-redirect toplevel, and those
     * attached to this window. */
    DestroyOverridePopup(winPtr);

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
        
        /* Free scroll scratch if any */
        if (winPtr->privatePtr->scrollScratchFBO) {
            glDeleteFramebuffers(1, &winPtr->privatePtr->scrollScratchFBO);
        }
        if (winPtr->privatePtr->scrollScratchTex) {
            glDeleteTextures(1, &winPtr->privatePtr->scrollScratchTex);
        }
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

    /*
     * Cancel any pending UpdateGeometryInfo idle task or timer, even if
     * WM_UPDATE_PENDING is not set: a direct call of UpdateGeometryInfo
     * clears it while an idle call can still be pending.
     */
    CancelGeometryUpdate(winPtr);
    wmPtr->flags &= ~WM_UPDATE_PENDING;

    /* Destroy wrapper window if present. */
    if (wmPtr->wrapperPtr != NULL) {
        Tk_DestroyWindow((Tk_Window)wmPtr->wrapperPtr);
        wmPtr->wrapperPtr = NULL;
    }

    /* Forget the container of this window, and this window as container. */
    if (wmPtr->containerPtr != NULL) {
	WmInfo *wmPtr2 = (WmInfo *)wmPtr->containerPtr->wmInfoPtr;
	if (wmPtr2 != NULL) {
	    wmPtr2->numTransients--;
	}
	Tk_DeleteEventHandler((Tk_Window)wmPtr->containerPtr,
		StructureNotifyMask, WmWaitMapProc, winPtr);
	wmPtr->containerPtr = NULL;
    }
    for (WmInfo *wmPtr2 = firstWmPtr; wmPtr2 != NULL;
	    wmPtr2 = wmPtr2->nextPtr) {
	if (wmPtr2->containerPtr == winPtr) {
	    Tk_DeleteEventHandler((Tk_Window)winPtr, StructureNotifyMask,
		    WmWaitMapProc, wmPtr2->winPtr);
	    wmPtr2->containerPtr = NULL;
	}
    }

    /* Forget icon window relationships and icon bitmaps. */
    if (wmPtr->icon != NULL) {
	WmInfo *wmPtr2 = (WmInfo *)((TkWindow *)wmPtr->icon)->wmInfoPtr;
	if (wmPtr2 != NULL) {
	    wmPtr2->iconFor = NULL;
	}
	wmPtr->icon = NULL;
    }
    if (wmPtr->iconFor != NULL) {
	WmInfo *wmPtr2 = (WmInfo *)((TkWindow *)wmPtr->iconFor)->wmInfoPtr;
	if (wmPtr2 != NULL) {
	    wmPtr2->icon = NULL;
	}
	wmPtr->iconFor = NULL;
    }
    if (wmPtr->iconBitmap != None) {
	Tk_FreeBitmap(winPtr->display, wmPtr->iconBitmap);
	wmPtr->iconBitmap = None;
    }
    if (wmPtr->cmapList != NULL) {
	Tcl_DecrRefCount(wmPtr->cmapList);
	wmPtr->cmapList = NULL;
    }
    if (wmPtr->iconMask != None) {
	Tk_FreeBitmap(winPtr->display, wmPtr->iconMask);
	wmPtr->iconMask = None;
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

    /* Wayland: synthesize Enter for window now under pointer after toplevel destroy.
     * Uses last known root coords from TkpWarpPointer in tkWaylandMouseEvent.c
     */
    {
        if ((winPtr->flags & TK_TOP_HIERARCHY) && winPtr->dispPtr) {
            TkWindow *candidate = NULL;
            int targetRootX = 0, targetRootY = 0;
            /* These globals are defined in tkWaylandMouseEvent.c */
            extern int tkWaylandLastRootX;
            extern int tkWaylandLastRootY;
            extern void *tkWaylandLastPointerWinPtr;
            if ((void*)winPtr == tkWaylandLastPointerWinPtr) {
                targetRootX = tkWaylandLastRootX;
                targetRootY = tkWaylandLastRootY;
                for (WmInfo *iter = firstWmPtr; iter; iter = iter->nextPtr) {
                    if (!iter->winPtr) continue;
                    if (!(iter->winPtr->flags & TK_MAPPED)) continue;
                    if (!(iter->winPtr->flags & TK_TOP_HIERARCHY)) continue;
                    int x = iter->winPtr->changes.x;
                    int y = iter->winPtr->changes.y;
                    int w = iter->winPtr->changes.width;
                    int h = iter->winPtr->changes.height;
                    if (targetRootX >= x && targetRootX < x+w && targetRootY >= y && targetRootY < y+h) {
                        candidate = iter->winPtr;
                        break;
                    }
                }
            }
            if (!candidate) {
                for (WmInfo *iter = firstWmPtr; iter; iter = iter->nextPtr) {
                    if (!iter->winPtr) continue;
                    if (!(iter->winPtr->flags & TK_MAPPED)) continue;
                    if (iter->winPtr->flags & TK_TOP_HIERARCHY) {
                        candidate = iter->winPtr;
                        break;
                    }
                }
            }
            if (candidate) {
                XEvent ev;
                memset(&ev, 0, sizeof(XEvent));
                ev.type = EnterNotify;
                ev.xcrossing.serial = LastKnownRequestProcessed(candidate->display)++;
                ev.xcrossing.send_event = False;
                ev.xcrossing.display = candidate->display;
                ev.xcrossing.window = Tk_WindowId((Tk_Window)candidate);
                ev.xcrossing.root = RootWindow(candidate->display, candidate->screenNum);
                ev.xcrossing.subwindow = None;
                ev.xcrossing.time = CurrentTime;
                ev.xcrossing.x = 50;
                ev.xcrossing.y = 50;
                ev.xcrossing.x_root = targetRootX ? targetRootX : candidate->changes.x + 50;
                ev.xcrossing.y_root = targetRootY ? targetRootY : candidate->changes.y + 50;
                ev.xcrossing.mode = NotifyNormal;
                ev.xcrossing.detail = NotifyAncestor;
                ev.xcrossing.same_screen = True;
                ev.xcrossing.focus = False;
                ev.xcrossing.state = 0;
                Tk_QueueWindowEvent(&ev, TCL_QUEUE_TAIL);
                /* Note: no dispPtr->pointerWinPtr in Tk 9.1, so we don't set it */
            }
        }
    }
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
 * AllocPrivateData --
 *
 *	Allocate and initialize the private data of a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets winPtr->privatePtr.
 *
 *----------------------------------------------------------------------
 */

static void
AllocPrivateData(
    TkWindow *winPtr)
{
#define CLIPRECTBUFSIZE 8
    winPtr->privatePtr = (glfwData*) ckalloc(sizeof(glfwData));
    memset(winPtr->privatePtr, 0, sizeof(glfwData));
    Tcl_DStringInit(&winPtr->privatePtr->pendingText);
    winPtr->privatePtr->clipRectBufferSize = CLIPRECTBUFSIZE;
    winPtr->privatePtr->clipRectBuffer = ckalloc(
	CLIPRECTBUFSIZE * sizeof(clipRect));
    winPtr->privatePtr->clipDirty = 1;
#undef CLIPRECTBUFSIZE
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
	AllocPrivateData(winPtr);
    }
    if (Tk_IsTopLevel(winPtr)) {
		
        /*
         * Guard against internal Tk toplevels that have no mainPtr -
         * e.g. the clipboard owner window created by TkClipInit.
         * These need a valid window ID but no real GLFW surface.
         * Return the pre-allocated result token directly; the window
         * will never be mapped or rendered.
         */
        if (!winPtr->mainPtr || !winPtr->mainPtr->interp) {
            /* Clipboard/selection internal toplevel - keep Drawable but no GLFW surface */
            if (winPtr->privatePtr == NULL) {
                winPtr->privatePtr = (glfwData*) ckalloc(sizeof(glfwData));
                memset(winPtr->privatePtr, 0, sizeof(glfwData));
                Tcl_DStringInit(&winPtr->privatePtr->pendingText);
#define CLIPRECTBUFSIZE 8
                winPtr->privatePtr->clipRectBufferSize = CLIPRECTBUFSIZE;
                winPtr->privatePtr->clipRectBuffer = ckalloc(CLIPRECTBUFSIZE * sizeof(clipRect));
#undef CLIPRECTBUFSIZE
            }
            DEBUG_LOG("Tk_MakeWindow: clipboard owner %s - no GLFW surface", Tk_PathName(winPtr));
            return result;
        }
        
        /*
         * Check if this is a menu window by its class.
         * Menu windows should use subsurfaces, not full GLFW windows.
         * Skip GLFW window creation entirely for menu windows.
         */
        if (IsPopupMenuWindow(winPtr)) {
            
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
         * A size set with "wm geometry" before the window is mapped wins, as
         * on X11.  It cannot wait for UpdateGeometryInfo, which postpones
         * resizing until the first focus, and a compositor need not focus a
         * new window (or may have no keyboard).
         */
        if (winPtr->wmInfoPtr != NULL) {
            int w, h;

            WmPixelSize(winPtr, &w, &h);
            if (w > 0) {
                width = w;
            }
            if (h > 0) {
                height = h;
            }
        }

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
 * WmPixelSize --
 *
 *	Converts the size requested with "wm geometry" to pixels.  For a
 *	gridded window wmPtr->width and wmPtr->height are in grid units,
 *	as on X11.
 *
 * Results:
 *	Stores the width and height in pixels, or -1 for a dimension that
 *	was not requested.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
WmPixelSize(
    TkWindow *winPtr,
    int *widthPtr,
    int *heightPtr)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;

    *widthPtr = wmPtr->width;
    *heightPtr = wmPtr->height;
    if (wmPtr->gridWin != NULL) {
	if (*widthPtr >= 0) {
	    *widthPtr = winPtr->reqWidth
		    + (*widthPtr - wmPtr->reqGridWidth) * wmPtr->widthInc;
	}
	if (*heightPtr >= 0) {
	    *heightPtr = winPtr->reqHeight
		    + (*heightPtr - wmPtr->reqGridHeight) * wmPtr->heightInc;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * WmPixelLimits --
 *
 *	Converts the limits set with "wm minsize" and "wm maxsize" to
 *	pixels.  For a gridded window they are in grid units, as on X11.
 *
 * Results:
 *	Stores the limits in pixels; a maximum of 0 means no limit.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
WmPixelLimits(
    TkWindow *winPtr,
    int *minWidthPtr,
    int *minHeightPtr,
    int *maxWidthPtr,
    int *maxHeightPtr)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;

    *minWidthPtr = wmPtr->minWidth;
    *minHeightPtr = wmPtr->minHeight;
    *maxWidthPtr = (wmPtr->maxWidth > 0) ? wmPtr->maxWidth : 0;
    *maxHeightPtr = (wmPtr->maxHeight > 0) ? wmPtr->maxHeight : 0;
    if (wmPtr->gridWin != NULL) {
	*minWidthPtr = winPtr->reqWidth
		+ (wmPtr->minWidth - wmPtr->reqGridWidth) * wmPtr->widthInc;
	*minHeightPtr = winPtr->reqHeight
		+ (wmPtr->minHeight - wmPtr->reqGridHeight) * wmPtr->heightInc;
	if (*maxWidthPtr > 0) {
	    *maxWidthPtr = winPtr->reqWidth
		    + (wmPtr->maxWidth - wmPtr->reqGridWidth) * wmPtr->widthInc;
	}
	if (*maxHeightPtr > 0) {
	    *maxHeightPtr = winPtr->reqHeight
		    + (wmPtr->maxHeight - wmPtr->reqGridHeight) * wmPtr->heightInc;
	}
	if (*minWidthPtr < 1) *minWidthPtr = 1;
	if (*minHeightPtr < 1) *minHeightPtr = 1;
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
            CancelGeometryUpdate(winPtr);
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
    TkWindow *winPtr,
    int aboveBelow,
    TkWindow *otherPtr)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    WmInfo *wmPtr2;

    /*
     * The compositor controls the stacking, and does not tell it, so just
     * record the order requested by Tk ("raise", "lower").
     */
    if (wmPtr == NULL) {
	return;
    }
    if (otherPtr == NULL || otherPtr->wmInfoPtr == NULL) {
	wmPtr->stackPos = (aboveBelow == Above) ? ++stackTop : --stackBottom;
	RaiseTransients(winPtr);
	if (aboveBelow == Above) {
	    RaiseOverridePopup(winPtr);
	}
	return;
    }

    /* Put it between the other window and its neighbour. */
    double pos = ((WmInfo *)otherPtr->wmInfoPtr)->stackPos;
    double next = pos;
    int found = 0;

    for (wmPtr2 = firstWmPtr; wmPtr2 != NULL; wmPtr2 = wmPtr2->nextPtr) {
	if (wmPtr2 == wmPtr) {
	    continue;
	}
	if ((aboveBelow == Above) ? (wmPtr2->stackPos > pos
		&& (!found || wmPtr2->stackPos < next))
		: (wmPtr2->stackPos < pos
		&& (!found || wmPtr2->stackPos > next))) {
	    next = wmPtr2->stackPos;
	    found = 1;
	}
    }
    if (!found) {
	wmPtr->stackPos = (aboveBelow == Above) ? ++stackTop : --stackBottom;
    } else {
	wmPtr->stackPos = (pos + next) / 2;
    }
    RaiseTransients(winPtr);
    if (aboveBelow == Above) {
	RaiseOverridePopup(winPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RaiseTransients --
 *
 *	Put the transients of a window (recursively) above it in the
 *	stacking order, as a window manager keeps them.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes stackPos of the transients.
 *
 *----------------------------------------------------------------------
 */

static void
RaiseTransients(
    TkWindow *winPtr)
{
    WmInfo *wmPtr2;

    for (wmPtr2 = firstWmPtr; wmPtr2 != NULL; wmPtr2 = wmPtr2->nextPtr) {
	if (wmPtr2->containerPtr == winPtr) {
	    wmPtr2->stackPos = ++stackTop;
	    RaiseTransients(wmPtr2->winPtr);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmProtocolEventProc --
 *
 *	Dispatches a window manager protocol (WM_DELETE_WINDOW by default,
 *	or WM_TAKE_FOCUS / WM_SAVE_YOURSELF when identified by a synthesized
 *	ClientMessage) to the handler registered with [wm protocol].  Called
 *	from the GLFW close callback in tkWaylandNotify.c with a NULL event.
 *	If no handler is registered for WM_DELETE_WINDOW, the toplevel is
 *	destroyed, matching Tk's default behavior.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Evaluates the registered Tcl command, or schedules window destruction.
 *
 *----------------------------------------------------------------------
 */

void
TkWmProtocolEventProc(
		      TkWindow *winPtr,
		      XEvent *eventPtr)
{
    WmInfo *wmPtr;
    int protocol = -1;

    if (winPtr == NULL || winPtr->wmInfoPtr == NULL) {
        return;
    }
    wmPtr = (WmInfo *)winPtr->wmInfoPtr;

    /*
     * On X11 this proc receives ClientMessage events with an Atom
     * identifying the protocol. On Wayland we may receive synthesized
     * ClientMessage events from generic Tk code or from tests.
     * Decode eventPtr if present, otherwise attempt to dispatch
     * DELETE_WINDOW as fallback (preserves old comment's intent).
     */
    if (eventPtr && eventPtr->type == ClientMessage) {
        /* Try to map Atom/string to our internal id */
        const char *msg = NULL;
        /* XClientMessageEvent message_type may hold protocol name */
        /* For safety, if data.l[0] matches our constants, use it */
        long l0 = eventPtr->xclient.data.l[0];
        if (l0 == WM_DELETE_WINDOW || l0 == WM_TAKE_FOCUS || l0 == WM_SAVE_YOURSELF) {
            protocol = (int)l0;
        } else {
            /* Fallback: treat any ClientMessage as DELETE_WINDOW if handler exists */
            protocol = WM_DELETE_WINDOW;
        }
    } else {
        /* Direct invocation (e.g., from generic code) – treat as DELETE_WINDOW
         * if handler exists, but allow explicit protocol via event? */
        protocol = WM_DELETE_WINDOW;
    }

    if (protocol != -1) {
        if (!TkWaylandHandleProtocol(wmPtr, protocol)) {
            /* No handler: for DELETE_WINDOW emulate default destroy */
            if (protocol == WM_DELETE_WINDOW) {
                if (!(winPtr->flags & TK_ALREADY_DEAD)) {
                    Tcl_DoWhenIdle(WaylandDoDestroyWindow, (void *)winPtr);
                }
            }
        }
    }
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
    extern int tkWaylandLastRootX;
    extern int tkWaylandLastRootY;
    /*
     * Fix stale winfo pointerx: update from live cursor pos on Enter,
     * so tooltip position is current as soon as pointer enters window.
     * This is what TkWaylandCursorEnterCallback should do via
     * TkWaylandUpdatePointerState using glfwGetCursorPos.
     */
    if (tkwin) {
        TkWindow *winPtr = (TkWindow *)tkwin;
        while (winPtr && !(winPtr->flags & TK_TOP_LEVEL) && winPtr->parentPtr) {
            winPtr = winPtr->parentPtr;
        }
        GLFWwindow *gw = winPtr ? TkWaylandGetGLFWwindow(winPtr) : NULL;
        if (gw) {
            double cx, cy;
            glfwGetCursorPos(gw, &cx, &cy);
            if (cx >= 0 && cy >= 0 && cx < 10000 && cy < 10000) {
                tkWaylandLastRootX = (int)cx;
                tkWaylandLastRootY = (int)cy;
            }
        }
    }
    if (xPtr) {
        *xPtr = tkWaylandLastRootX;
    }
    if (yPtr) {
        *yPtr = tkWaylandLastRootY;
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
 * Tk_WmObjCmd â€“
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
        if (objc == 2) {
            Tcl_WrongNumArgs(interp, 2, objv, "option ?arg ...?");
            return TCL_ERROR;
        }
        if (Tcl_GetIndexFromObjStruct(interp, objv[2], optionStrings,
				      sizeof(char *), "option", 0, &index) != TCL_OK) {
            return TCL_ERROR;
        }
        if (!(winPtr->flags & TK_TOP_LEVEL)
		&& index != WMOPT_MANAGE && index != WMOPT_FORGET) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(
						   "window \"%s\" isn't a top-level window", argv1));
            Tcl_SetErrorCode(interp, "TK", "LOOKUP", "TOPLEVEL", argv1, NULL);
            return TCL_ERROR;
        }
        objc -= 3; objv += 3;
    } else {
        if (Tcl_GetIndexFromObjStruct(interp, objv[1], optionStrings,
				      sizeof(char *), "option", 0, &index) != TCL_OK) {
            return TCL_ERROR;
        }
        if (objc < 3) {
            Tcl_WrongNumArgs(interp, 1, objv, "option window ?arg ...?");
            return TCL_ERROR;
        }
        winPtr = (TkWindow *)Tk_NameToWindow(
					     interp, Tcl_GetString(objv[2]), tkwin);
        if (winPtr == NULL) return TCL_ERROR;
        if (!(winPtr->flags & TK_TOP_LEVEL)
		&& index != WMOPT_MANAGE && index != WMOPT_FORGET) {
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
			 "wm aspect window ?minNumer minDenom maxNumer maxDenom?");
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
			     Tcl_NewStringObj("aspect number can't be <= 0",-1));
            Tcl_SetErrorCode(interp,"TK","WM","ASPECT",NULL);
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

    /* No arguments - return all attributes. */
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
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    const char *name;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "wm client window ?name?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->clientMachine != NULL) {
	    Tcl_SetObjResult(interp,
		    Tcl_NewStringObj(wmPtr->clientMachine, TCL_INDEX_NONE));
	}
	return TCL_OK;
    }
    if (wmPtr->clientMachine != NULL) {
	ckfree(wmPtr->clientMachine);
	wmPtr->clientMachine = NULL;
    }
    name = Tcl_GetString(objv[0]);
    if (*name != '\0') {
	wmPtr->clientMachine = (char *)ckalloc(strlen(name) + 1);
	strcpy(wmPtr->clientMachine, name);
    }
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
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    Tcl_Size count, i;
    Tcl_Obj **elems;
    Tk_Window tkwin2;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv,
		"wm colormapwindows window ?windowList?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	Tcl_Obj *resultObj = Tcl_NewObj();

	if (wmPtr->cmapList != NULL
		&& Tcl_ListObjGetElements(NULL, wmPtr->cmapList, &count,
		&elems) == TCL_OK) {
	    for (i = 0; i < count; i++) {
		if (Tk_NameToWindow(NULL, Tcl_GetString(elems[i]), tkwin)) {
		    Tcl_ListObjAppendElement(NULL, resultObj, elems[i]);
		}
	    }
	}
	Tcl_SetObjResult(interp, resultObj);
	return TCL_OK;
    }

    /*
     * Colormaps are not used on Wayland: just check and remember the
     * windows.
     */
    if (Tcl_ListObjGetElements(interp, objv[0], &count, &elems) != TCL_OK) {
	return TCL_ERROR;
    }
    for (i = 0; i < count; i++) {
	if (TkGetWindowFromObj(interp, tkwin, elems[i], &tkwin2) != TCL_OK) {
	    return TCL_ERROR;
	}
    }
    if (wmPtr->cmapList != NULL) {
	Tcl_DecrRefCount(wmPtr->cmapList);
    }
    wmPtr->cmapList = Tcl_DuplicateObj(objv[0]);
    Tcl_IncrRefCount(wmPtr->cmapList);
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
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    Tcl_Size argc, i;
    Tcl_Obj **argv;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "wm command window ?value?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->cmdArgv != NULL) {
	    Tcl_SetObjResult(interp,
		    Tcl_NewListObj(wmPtr->cmdArgc, wmPtr->cmdArgv));
	}
	return TCL_OK;
    }
    if (*Tcl_GetString(objv[0]) != '\0' && Tcl_ListObjGetElements(interp,
	    objv[0], &argc, &argv) != TCL_OK) {
	return TCL_ERROR;
    }
    if (wmPtr->cmdArgv != NULL) {
	for (i = 0; i < wmPtr->cmdArgc; i++) {
	    Tcl_DecrRefCount(wmPtr->cmdArgv[i]);
	}
	ckfree((char *)wmPtr->cmdArgv);
	wmPtr->cmdArgv = NULL;
	wmPtr->cmdArgc = 0;
    }
    if (*Tcl_GetString(objv[0]) != '\0') {
	wmPtr->cmdArgv = (Tcl_Obj **)ckalloc((argc + 1) * sizeof(Tcl_Obj *));
	for (i = 0; i < argc; i++) {
	    wmPtr->cmdArgv[i] = argv[i];
	    Tcl_IncrRefCount(argv[i]);
	}
	wmPtr->cmdArgc = argc;
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
        Tcl_WrongNumArgs(interp, 0, objv, "wm deiconify window");
        return TCL_ERROR;
    }

    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;

    if (wmPtr->iconFor != NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't deiconify %s: it is an icon for %s",
		Tk_PathName(winPtr), Tk_PathName(wmPtr->iconFor)));
	Tcl_SetErrorCode(interp, "TK", "WM", "DEICONIFY", "ICON", (char *)NULL);
	return TCL_ERROR;
    }
    wmPtr->withdrawn = 0;
    wmPtr->initialState = NormalState;
    wmPtr->flags &= ~WM_WITHDRAWN;

    /*
     * Before the first map, only change the initial state: the window is
     * mapped at idle, unless Tk already tried to map it while it was
     * withdrawn.
     */
    if (!(wmPtr->flags & WM_NEVER_MAPPED) || (wmPtr->flags & WM_MAP_DEFERRED)) {
	TkWmMapWindow(winPtr);
    }
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
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    static const char *const optionStrings[] = {
	"active", "passive", NULL };
    int index;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv,
		"wm focusmodel window ?active|passive?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		wmPtr->activeFocus ? "active" : "passive", TCL_INDEX_NONE));
	return TCL_OK;
    }
    if (Tcl_GetIndexFromObjStruct(interp, objv[0], optionStrings,
	    sizeof(char *), "argument", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }
    wmPtr->activeFocus = (index == 0);
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
            TkWindow *winPtr,
            TCL_UNUSED(Tcl_Interp *),
            TCL_UNUSED(int),
            TCL_UNUSED(Tcl_Obj *const *))
{
    Tk_Window frameWin = (Tk_Window)winPtr;

    if (Tk_IsTopLevel(frameWin) && Tk_IsManageable(frameWin)) {
	TkFocusJoin(winPtr);
	Tk_UnmapWindow(frameWin);
	/*
	 * Destroys the GLFW window, the WmInfo and the private data, which
	 * the window still needs as a child window.
	 */
	TkWmDeadWindow(winPtr);
	if (winPtr->privatePtr == NULL) {
	    AllocPrivateData(winPtr);
	}
	winPtr->flags &=
		~(TK_TOP_HIERARCHY|TK_TOP_LEVEL|TK_HAS_WRAPPER|TK_WIN_MANAGED);
	Tk_ManageGeometry(frameWin, NULL, NULL);

	/* The flags (above) must be cleared before calling TkMapTopFrame. */
	TkMapTopFrame(frameWin);
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
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    char buf[TCL_INTEGER_SPACE];

    if (objc != 0) {
	Tcl_WrongNumArgs(interp, 0, objv, "wm frame window");
	return TCL_ERROR;
    }
    snprintf(buf, sizeof(buf), "0x%" TCL_Z_MODIFIER "x",
	    (size_t)Tk_WindowId((Tk_Window)winPtr));
    Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, TCL_INDEX_NONE));
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
        Tcl_WrongNumArgs(interp, 0, objv, "wm geometry window ?newGeometry?");
        return TCL_ERROR;
    }

    /* Return current geometry. */
    if (objc == 0) {
        int width, height;
        if (glfwWindow != NULL && !(wmPtr->flags & WM_NEVER_MAPPED)) {
            glfwGetWindowSize(glfwWindow, &width, &height);
        } else {
	    /* As on X11, the size of the window, not yet computed. */
            width = winPtr->changes.width;
            height = winPtr->changes.height;
        }
        if (wmPtr->gridWin != NULL) {
            /* As on X11, the size of a gridded window is in grid units. */
            if (glfwWindow == NULL || (wmPtr->flags & WM_NEVER_MAPPED)) {
                width = winPtr->changes.width;
                height = winPtr->changes.height;
            }
            width = wmPtr->reqGridWidth
                    + (width - winPtr->reqWidth) / wmPtr->widthInc;
            height = wmPtr->reqGridHeight
                    + (height - winPtr->reqHeight) / wmPtr->heightInc;
        }
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("%dx%d%c%d%c%d",
		 width, height,
		 (wmPtr->flags & WM_NEGATIVE_X) ? '-' : '+', wmPtr->x,
		 (wmPtr->flags & WM_NEGATIVE_Y) ? '-' : '+', wmPtr->y));
        return TCL_OK;
    }

    /* Handle empty string - reset to default. */
    if (*Tcl_GetString(objv[0]) == '\0') {
        wmPtr->width = wmPtr->height = -1;

        if (wmPtr->flags & WM_UPDATE_PENDING) {
            CancelGeometryUpdate(winPtr);
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
        /*
         * Set size only if positive values were provided.  The size of a
         * gridded window is in grid units and is set by UpdateGeometryInfo.
         */
        if (wmPtr->gridWin == NULL && wmPtr->width > 0 && wmPtr->height > 0) {
	    DEBUG_LOG("GeometryCmd setting window size %s -> %dx%d",
		Tk_PathName(winPtr), wmPtr->width, wmPtr->height);
	    TkWaylandFlushConfigures();
            glfwSetWindowSize(glfwWindow, wmPtr->width, wmPtr->height);
        }

        /* Cancel any pending idle callback. */
        if (wmPtr->flags & WM_UPDATE_PENDING) {
            CancelGeometryUpdate(winPtr);
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
        if (wmPtr->gridWin == NULL) {
            if (wmPtr->width > 0 && wmPtr->width != newWidth) {
                wmPtr->width = newWidth;
            }
            if (wmPtr->height > 0 && wmPtr->height != newHeight) {
                wmPtr->height = newHeight;
            }
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
			 "wm grid window ?baseWidth baseHeight widthInc heightInc?");
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
        if (wmPtr->gridWin == (Tk_Window) winPtr) {
            Tk_UnsetGrid((Tk_Window) winPtr);
        }
        wmPtr->sizeHintsFlags &= ~(WM_PBaseSize|WM_PResizeInc);
        wmPtr->widthInc = wmPtr->heightInc = 1;
        wmPtr->reqGridWidth = wmPtr->reqGridHeight = 0;
    } else {
        if (Tcl_GetIntFromObj(interp,objv[0],&rw) != TCL_OK
	    || Tcl_GetIntFromObj(interp,objv[1],&rh) != TCL_OK
	    || Tcl_GetIntFromObj(interp,objv[2],&wi) != TCL_OK
	    || Tcl_GetIntFromObj(interp,objv[3],&hi) != TCL_OK) return TCL_ERROR;
        const char *msg = NULL;
        if (rw < 0) {
            msg = "baseWidth can't be < 0";
        } else if (rh < 0) {
            msg = "baseHeight can't be < 0";
        } else if (wi <= 0) {
            msg = "widthInc can't be <= 0";
        } else if (hi <= 0) {
            msg = "heightInc can't be <= 0";
        }
        if (msg != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(msg, -1));
            Tcl_SetErrorCode(interp, "TK", "VALUE", "GRID", NULL);
            return TCL_ERROR;
        }
        /* As on X11, the toplevel becomes the gridded window. */
        Tk_SetGrid((Tk_Window) winPtr, rw, rh, wi, hi);
        wmPtr->sizeHintsFlags |= WM_PResizeInc;
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
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    Tk_Window leader;
    const char *name;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "wm group window ?pathName?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->leaderName != NULL) {
	    Tcl_SetObjResult(interp,
		    Tcl_NewStringObj(wmPtr->leaderName, TCL_INDEX_NONE));
	}
	return TCL_OK;
    }
    name = Tcl_GetString(objv[0]);
    if (*name != '\0' && TkGetWindowFromObj(interp, (Tk_Window)winPtr,
	    objv[0], &leader) != TCL_OK) {
	return TCL_ERROR;
    }
    if (wmPtr->leaderName != NULL) {
	ckfree(wmPtr->leaderName);
	wmPtr->leaderName = NULL;
    }
    if (*name != '\0') {
	wmPtr->leaderName = (char *)ckalloc(strlen(name) + 1);
	strcpy(wmPtr->leaderName, name);
    }
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
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    Tcl_Obj *cmd[3];
    int code;

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "wm iconbadge window badge");
	return TCL_ERROR;
    }

    /* As on X11: the badge is drawn on the icon photo. */
    cmd[0] = Tcl_NewStringObj("::tk::icons::IconBadge", TCL_INDEX_NONE);
    cmd[1] = Tk_NewWindowObj((Tk_Window)winPtr);
    cmd[2] = objv[0];
    for (int i = 0; i < 3; i++) {
	Tcl_IncrRefCount(cmd[i]);
    }
    code = Tcl_EvalObjv(interp, 3, cmd, TCL_EVAL_GLOBAL);
    for (int i = 0; i < 3; i++) {
	Tcl_DecrRefCount(cmd[i]);
    }
    return code;
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
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    Pixmap pixmap;
    const char *bitmapName;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "wm iconbitmap window ?bitmap?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->iconBitmap != None) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    Tk_NameOfBitmap(winPtr->display, wmPtr->iconBitmap),
		    TCL_INDEX_NONE));
	}
	return TCL_OK;
    }
    bitmapName = Tcl_GetString(objv[0]);
    if (*bitmapName == '\0') {
	pixmap = None;
    } else {
	pixmap = Tk_GetBitmap(interp, tkwin, bitmapName);
	if (pixmap == None) {
	    return TCL_ERROR;
	}
    }
    if (wmPtr->iconBitmap != None) {
	Tk_FreeBitmap(winPtr->display, wmPtr->iconBitmap);
    }
    wmPtr->iconBitmap = pixmap;
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
        Tcl_WrongNumArgs(interp, 0, objv, "wm iconify window");
        return TCL_ERROR;
    }
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    const char *reason = NULL;

    if (Tk_Attributes((Tk_Window)winPtr)->override_redirect) {
	reason = "override-redirect flag is set";
    } else if (wmPtr->containerPtr != NULL) {
	reason = "it is a transient";
    } else if (winPtr->flags & TK_EMBEDDED) {
	reason = "it is an embedded window";
    }
    if (wmPtr->iconFor != NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't iconify \"%s\": it is an icon for \"%s\"",
		Tk_PathName(winPtr), Tk_PathName(wmPtr->iconFor)));
	Tcl_SetErrorCode(interp, "TK", "WM", "ICONIFY", "ICON", (char *)NULL);
	return TCL_ERROR;
    }
    if (reason != NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't iconify \"%s\": %s", Tk_PathName(winPtr), reason));
	Tcl_SetErrorCode(interp, "TK", "WM", "ICONIFY", (char *)NULL);
	return TCL_ERROR;
    }
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);

    /* Update Tk's internal state to IconicState. */
    TkpWmSetState(winPtr, IconicState);

    /* If the window is mapped and has a GLFW window, actually iconify it. */
    if (Tk_IsMapped(winPtr) && glfwWindow != NULL
	    && !OverrideBacked(winPtr)) {
        glfwIconifyWindow(glfwWindow);
        winPtr->flags &= ~TK_MAPPED;
	SendUnmapNotify(winPtr);
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
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    Pixmap pixmap;
    const char *bitmapName;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "wm iconmask window ?bitmap?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->iconMask != None) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    Tk_NameOfBitmap(winPtr->display, wmPtr->iconMask),
		    TCL_INDEX_NONE));
	}
	return TCL_OK;
    }
    bitmapName = Tcl_GetString(objv[0]);
    if (*bitmapName == '\0') {
	pixmap = None;
    } else {
	pixmap = Tk_GetBitmap(interp, tkwin, bitmapName);
	if (pixmap == None) {
	    return TCL_ERROR;
	}
    }
    if (wmPtr->iconMask != None) {
	Tk_FreeBitmap(winPtr->display, wmPtr->iconMask);
    }
    wmPtr->iconMask = pixmap;
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
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    const char *name;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "wm iconname window ?newName?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->iconName != NULL) {
	    Tcl_SetObjResult(interp,
		    Tcl_NewStringObj(wmPtr->iconName, TCL_INDEX_NONE));
	}
	return TCL_OK;
    }
    if (wmPtr->iconName != NULL) {
	ckfree(wmPtr->iconName);
    }
    name = Tcl_GetString(objv[0]);
    wmPtr->iconName = (char *)ckalloc(strlen(name) + 1);
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
    int objc,
    Tcl_Obj *const objv[])
{
    int i, isDefault = 0;

    if (objc > 0 && strcmp(Tcl_GetString(objv[0]), "-default") == 0) {
	isDefault = 1;
    }
    if (objc < 1 + isDefault) {
	Tcl_WrongNumArgs(interp, 0, objv,
		"wm iconphoto window ?-default? image1 ?image2 ...?");
	return TCL_ERROR;
    }

    /*
     * The images are only checked: the compositor takes the icon of a
     * window from the .desktop file of its app_id.
     */
    for (i = isDefault; i < objc; i++) {
	if (Tk_FindPhoto(interp, Tcl_GetString(objv[i])) == NULL) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "can't use \"%s\" as iconphoto: not a photo image",
		    Tcl_GetString(objv[i])));
	    Tcl_SetErrorCode(interp, "TK", "WM", "ICONPHOTO", "PHOTO", (char *)NULL);
	    return TCL_ERROR;
	}
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
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    int x, y;

    if (objc != 0 && objc != 2) {
	Tcl_WrongNumArgs(interp, 0, objv, "wm iconposition window ?x y?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->iconPositionSet) {
	    Tcl_Obj *results[2];
	    results[0] = Tcl_NewWideIntObj(wmPtr->iconX);
	    results[1] = Tcl_NewWideIntObj(wmPtr->iconY);
	    Tcl_SetObjResult(interp, Tcl_NewListObj(2, results));
	}
	return TCL_OK;
    }
    if (TkObjIsEmpty(objv[0])) {
	wmPtr->iconPositionSet = 0;
	return TCL_OK;
    }
    if (Tcl_GetIntFromObj(interp, objv[0], &x) != TCL_OK
	    || Tcl_GetIntFromObj(interp, objv[1], &y) != TCL_OK) {
	return TCL_ERROR;
    }
    wmPtr->iconX = x;
    wmPtr->iconY = y;
    wmPtr->iconPositionSet = 1;
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
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    Tk_Window tkwin2;
    WmInfo *wmPtr2;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "wm iconwindow window ?pathName?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (wmPtr->icon != NULL) {
	    Tcl_SetObjResult(interp, Tk_NewWindowObj(wmPtr->icon));
	}
	return TCL_OK;
    }
    if (TkObjIsEmpty(objv[0])) {
	if (wmPtr->icon != NULL) {
	    wmPtr2 = (WmInfo *)((TkWindow *)wmPtr->icon)->wmInfoPtr;
	    wmPtr2->iconFor = NULL;
	    wmPtr2->withdrawn = 1;
	    wmPtr2->initialState = WithdrawnState;
	}
	wmPtr->icon = NULL;
	return TCL_OK;
    }
    if (TkGetWindowFromObj(interp, tkwin, objv[0], &tkwin2) != TCL_OK) {
	return TCL_ERROR;
    }
    if (!Tk_IsTopLevel(tkwin2)) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't use %s as icon window: not at top level",
		Tcl_GetString(objv[0])));
	Tcl_SetErrorCode(interp, "TK", "WM", "ICONWINDOW", "INNER", (char *)NULL);
	return TCL_ERROR;
    }
    wmPtr2 = (WmInfo *)((TkWindow *)tkwin2)->wmInfoPtr;
    if (wmPtr2->iconFor != NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"%s is already an icon for %s",
		Tcl_GetString(objv[0]), Tk_PathName(wmPtr2->iconFor)));
	Tcl_SetErrorCode(interp, "TK", "WM", "ICONWINDOW", "ICON", (char *)NULL);
	return TCL_ERROR;
    }
    if (wmPtr->icon != NULL) {
	WmInfo *wmPtr3 = (WmInfo *)((TkWindow *)wmPtr->icon)->wmInfoPtr;
	wmPtr3->iconFor = NULL;
	wmPtr3->withdrawn = 1;
	wmPtr3->initialState = WithdrawnState;
    }
    wmPtr->icon = tkwin2;
    wmPtr2->iconFor = (Tk_Window)winPtr;

    /*
     * Wayland has no icon windows: the window is just hidden, as a window
     * manager does with an icon window while its toplevel is not iconic.
     */
    if (!wmPtr2->withdrawn && Tk_IsMapped(tkwin2)) {
	TkWmUnmapWindow((TkWindow *)tkwin2);
    }
    return TCL_OK;
}

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
            TkWindow *winPtr,
            Tcl_Interp *interp,
            int objc,
            Tcl_Obj *const objv[])
{
    Tk_Window frameWin = (Tk_Window)winPtr;

    if (objc != 0) {
        Tcl_WrongNumArgs(interp,0,objv,"wm manage window"); return TCL_ERROR;
    }
    if (!Tk_IsTopLevel(frameWin)) {
	if (!Tk_IsManageable(frameWin)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "window \"%s\" is not manageable: must be a frame,"
		    " labelframe or toplevel", Tk_PathName(frameWin)));
	    Tcl_SetErrorCode(interp, "TK", "WM", "MANAGE", (char *)NULL);
	    return TCL_ERROR;
	}
	/* Create the window as a child first, with its private data. */
	Tk_MakeWindowExist(frameWin);
	TkFocusSplit(winPtr);
	Tk_UnmapWindow(frameWin);
	winPtr->flags |= TK_TOP_HIERARCHY|TK_TOP_LEVEL|TK_WIN_MANAGED;
	if (winPtr->wmInfoPtr == NULL) {
	    TkWmNewWindow(winPtr);
	}

	/*
	 * Tk_MakeWindow creates the GLFW window of a toplevel, but this
	 * window already exists.  Create its GLFW window here.
	 */
	if (TkWaylandGetGLFWwindow(winPtr) == NULL) {
	    Drawable drawable;
	    int width = (winPtr->reqWidth > 1) ? winPtr->reqWidth : 200;
	    int height = (winPtr->reqHeight > 1) ? winPtr->reqHeight : 200;

	    if (TkWaylandCreateWindow(winPtr, width, height,
		    Tk_Name(frameWin), &drawable) == NULL) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"cannot create a window for \"%s\"",
			Tk_PathName(frameWin)));
		Tcl_SetErrorCode(interp, "TK", "WM", "MANAGE", (char *)NULL);
		return TCL_ERROR;
	    }
	    createClipShaders(winPtr);
	    ((WmInfo *)winPtr->wmInfoPtr)->flags |= WM_NEVER_MAPPED;
	}
	winPtr->flags &= ~TK_MAPPED;

	/*
	 * The flags (above) must be set before calling TkMapTopFrame.  The
	 * GLFW window is created when the new toplevel is mapped.
	 */
	TkMapTopFrame(frameWin);
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
        Tcl_WrongNumArgs(interp,0,objv,"wm maxsize window ?width height?");
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
        Tcl_WrongNumArgs(interp,0,objv,"wm minsize window ?width height?");
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
        Tcl_WrongNumArgs(interp, 0, objv, "wm overrideredirect window ?boolean?");
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
    if ((boolean != 0) == (Tk_Attributes((Tk_Window) winPtr)->override_redirect != 0)) {
	return TCL_OK;
    }

    /*
     * The window is shown differently with and without the attribute
     * (subsurface vs. GLFW window), so a mapped window is unmapped and
     * mapped again around the change, as an X11 window manager needs.
     */
    atts.override_redirect = boolean ? True : False;
    if (Tk_IsMapped(winPtr)) {
	TkWmUnmapWindow(winPtr);
	Tk_ChangeWindowAttributes((Tk_Window) winPtr, CWOverrideRedirect, &atts);
	TkWmMapWindow(winPtr);
	return TCL_OK;
    }
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
        Tcl_WrongNumArgs(interp,0,objv,"wm positionfrom window ?user/program?");
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
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    ProtocolHandler *protPtr, *prevPtr;
    const char *cmd;
    Tcl_Size cmdLength;
    const char *protocolName;
    int protocol;

    if (objc > 2) {
	Tcl_WrongNumArgs(interp, 0, objv,
		"wm protocol window ?name? ?command?");
	return TCL_ERROR;
    }
    if (objc == 0) {
        Tcl_Obj *result = Tcl_NewObj();
        for (protPtr=wmPtr->protPtr; protPtr; protPtr=protPtr->nextPtr) {
            const char *name = protPtr->protocolName ? protPtr->protocolName : WaylandGetProtocolName(protPtr->protocol);
            if (name) Tcl_ListObjAppendElement(NULL,result,Tcl_NewStringObj(name,-1));
        }
        Tcl_SetObjResult(interp,result);
        return TCL_OK;
    }

    protocolName = Tcl_GetString(objv[0]);
    protocol = WaylandGetProtocolId(protocolName);

    if (objc == 1) {
        for (protPtr=wmPtr->protPtr; protPtr; protPtr=protPtr->nextPtr) {
            if (protPtr->protocol==protocol) {
                Tcl_SetObjResult(interp,Tcl_NewStringObj(protPtr->command,-1));
                return TCL_OK;
            }
        }
        Tcl_SetObjResult(interp,Tcl_NewStringObj("",-1));
        return TCL_OK;
    }

    cmd = Tcl_GetStringFromObj(objv[1],&cmdLength);
    if (cmdLength == 0) {
        for (protPtr=wmPtr->protPtr,prevPtr=NULL; protPtr; prevPtr=protPtr, protPtr=protPtr->nextPtr) {
            if (protPtr->protocol==protocol) {
                if (prevPtr) prevPtr->nextPtr=protPtr->nextPtr; else wmPtr->protPtr=protPtr->nextPtr;
                if (protPtr->protocolName) ckfree(protPtr->protocolName);
                Tcl_EventuallyFree((void *)protPtr,TCL_DYNAMIC);
                break;
            }
        }
    } else {
        for (protPtr=wmPtr->protPtr,prevPtr=NULL; protPtr; prevPtr=protPtr, protPtr=protPtr->nextPtr) {
            if (protPtr->protocol==protocol) break;
        }
        if (protPtr==NULL) {
            protPtr=(ProtocolHandler *)ckalloc(HANDLER_SIZE(cmdLength));
            protPtr->protocol=protocol;
            protPtr->protocolName=ckalloc(strlen(protocolName)+1);
            strcpy(protPtr->protocolName,protocolName);
            protPtr->nextPtr=wmPtr->protPtr; wmPtr->protPtr=protPtr; protPtr->interp=interp;
        } else {
            char *oldName = protPtr->protocolName;
            protPtr=(ProtocolHandler *)ckrealloc((char *)protPtr, HANDLER_SIZE(cmdLength));
            protPtr->protocolName = oldName;
            if (prevPtr) prevPtr->nextPtr=protPtr; else wmPtr->protPtr=protPtr;
            protPtr->interp=interp;
            if (protPtr->protocolName==NULL || strcmp(protPtr->protocolName,protocolName)!=0) {
                if (protPtr->protocolName) ckfree(protPtr->protocolName);
                protPtr->protocolName=ckalloc(strlen(protocolName)+1);
                strcpy(protPtr->protocolName,protocolName);
            }
        }
        strcpy(protPtr->command, cmd);
    }
    return TCL_OK;
}

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
        Tcl_WrongNumArgs(interp,0,objv,"wm resizable window ?width height?");
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
        Tcl_WrongNumArgs(interp,0,objv,"wm sizefrom window ?user|program?");
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
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    TkWindow **windows, **windowPtr;
    static const char *const optionStrings[] = {
	"isabove", "isbelow", NULL };
    int index;

    if (objc != 0 && objc != 2) {
	Tcl_WrongNumArgs(interp, 0, objv,
		"wm stackorder window ?isabove|isbelow window?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	Tcl_Obj *resultObj = Tcl_NewObj();

	windows = TkWmStackorderToplevel(winPtr);
	for (windowPtr = windows; *windowPtr; windowPtr++) {
	    Tcl_ListObjAppendElement(NULL, resultObj,
		    Tcl_NewStringObj((*windowPtr)->pathName, TCL_INDEX_NONE));
	}
	ckfree(windows);
	Tcl_SetObjResult(interp, resultObj);
	return TCL_OK;
    }

    Tk_Window relWin;
    TkWindow *winPtr2;
    int index1 = -1, index2 = -1;

    if (TkGetWindowFromObj(interp, tkwin, objv[1], &relWin) != TCL_OK) {
	return TCL_ERROR;
    }
    winPtr2 = (TkWindow *)relWin;
    if (!Tk_IsTopLevel(winPtr2)) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"window \"%s\" isn't a top-level window", winPtr2->pathName));
	Tcl_SetErrorCode(interp, "TK", "WM", "STACK", "TOPLEVEL", (char *)NULL);
	return TCL_ERROR;
    }
    if (!Tk_IsMapped(winPtr)) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"window \"%s\" isn't mapped", winPtr->pathName));
	Tcl_SetErrorCode(interp, "TK", "WM", "STACK", "MAPPED", (char *)NULL);
	return TCL_ERROR;
    }
    if (!Tk_IsMapped(winPtr2)) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"window \"%s\" isn't mapped", winPtr2->pathName));
	Tcl_SetErrorCode(interp, "TK", "WM", "STACK", "MAPPED", (char *)NULL);
	return TCL_ERROR;
    }
    windows = TkWmStackorderToplevel(winPtr->mainPtr->winPtr);
    for (windowPtr = windows; *windowPtr; windowPtr++) {
	if (*windowPtr == winPtr) {
	    index1 = windowPtr - windows;
	}
	if (*windowPtr == winPtr2) {
	    index2 = windowPtr - windows;
	}
    }
    ckfree(windows);
    if (Tcl_GetIndexFromObjStruct(interp, objv[0], optionStrings,
	    sizeof(char *), "argument", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(
	    (index == 0) ? index1 > index2 : index1 < index2));
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
    /* As on Aqua: "icon" is only reported, and "zoomed" can be set. */
    static const char *const opts[] = {
        "iconic", "normal", "withdrawn", "zoomed", NULL
    };
    enum { OPT_ICONIC, OPT_NORMAL, OPT_WITHDRAWN, OPT_ZOOMED };
    int idx;

    /*
     * The dispatcher in Tk_WmObjCmd has already shifted objv past
     * "wm state window": objc==0 is the query form, objv[0] is the
     * new state (same convention as WmTitleCmd et al).
     */
    if (objc > 1) {
        Tcl_WrongNumArgs(interp, 0, objv, "wm state window ?state?");
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
        else if (Tk_IsMapped((Tk_Window)winPtr)
		|| ((wmPtr->flags & WM_NEVER_MAPPED)
		&& wmPtr->initialState != IconicState)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("normal", -1));
        }
        else {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("iconic", -1));
        }
        return TCL_OK;
    }

    /* Get requested state. */
    if (Tcl_GetIndexFromObjStruct(interp, objv[0], opts, sizeof(char *),
                                  "argument", 0, &idx) != TCL_OK) {
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
            wmPtr->flags &= ~WM_WITHDRAWN;

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
            wmPtr->flags |= WM_WITHDRAWN;
            TkWmUnmapWindow(winPtr);
            break;

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
        Tcl_WrongNumArgs(interp,0,objv,"wm title window ?newTitle?"); return TCL_ERROR;
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
    Tk_Window tkwin,
    TkWindow *winPtr,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    TkWindow *containerPtr = wmPtr->containerPtr, *w;
    WmInfo *wmPtr2;
    Tk_Window container;

    if (objc > 1) {
	Tcl_WrongNumArgs(interp, 0, objv, "wm transient window ?window?");
	return TCL_ERROR;
    }
    if (objc == 0) {
	if (containerPtr != NULL) {
	    Tcl_SetObjResult(interp, Tk_NewWindowObj((Tk_Window)containerPtr));
	}
	return TCL_OK;
    }
    if (TkObjIsEmpty(objv[0])) {
	if (containerPtr != NULL) {
	    ((WmInfo *)containerPtr->wmInfoPtr)->numTransients--;
	    Tk_DeleteEventHandler((Tk_Window)containerPtr, StructureNotifyMask,
		    WmWaitMapProc, winPtr);
	}
	wmPtr->containerPtr = NULL;
	return TCL_OK;
    }
    if (TkGetWindowFromObj(interp, tkwin, objv[0], &container) != TCL_OK) {
	return TCL_ERROR;
    }
    containerPtr = (TkWindow *)container;
    while (!Tk_TopWinHierarchy(containerPtr)) {
	containerPtr = containerPtr->parentPtr;
    }
    Tk_MakeWindowExist((Tk_Window)containerPtr);
    if (wmPtr->iconFor != NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't make \"%s\" a transient: it is an icon for %s",
		Tk_PathName(winPtr), Tk_PathName(wmPtr->iconFor)));
	Tcl_SetErrorCode(interp, "TK", "WM", "TRANSIENT", "ICON", (char *)NULL);
	return TCL_ERROR;
    }
    wmPtr2 = (WmInfo *)containerPtr->wmInfoPtr;
    if (wmPtr2->iconFor != NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't make \"%s\" a container: it is an icon for %s",
		Tcl_GetString(objv[0]), Tk_PathName(wmPtr2->iconFor)));
	Tcl_SetErrorCode(interp, "TK", "WM", "TRANSIENT", "ICON", (char *)NULL);
	return TCL_ERROR;
    }
    for (w = containerPtr; w != NULL && w->wmInfoPtr != NULL;
	    w = ((WmInfo *)w->wmInfoPtr)->containerPtr) {
	if (w == winPtr) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "can't set \"%s\" as container: would cause management loop",
		    Tk_PathName(containerPtr)));
	    Tcl_SetErrorCode(interp, "TK", "WM", "TRANSIENT", "SELF", (char *)NULL);
	    return TCL_ERROR;
	}
    }
    if (containerPtr != wmPtr->containerPtr) {
	/*
	 * The event handler makes the transient follow the state of its
	 * container.
	 */
	if (wmPtr->containerPtr != NULL) {
	    ((WmInfo *)wmPtr->containerPtr->wmInfoPtr)->numTransients--;
	    Tk_DeleteEventHandler((Tk_Window)wmPtr->containerPtr,
		    StructureNotifyMask, WmWaitMapProc, winPtr);
	}
	wmPtr2->numTransients++;
	Tk_CreateEventHandler((Tk_Window)containerPtr, StructureNotifyMask,
		WmWaitMapProc, winPtr);
	wmPtr->containerPtr = containerPtr;
    }
    if (!(wmPtr->flags & WM_NEVER_MAPPED) && !Tk_IsMapped(containerPtr)
	    && !wmPtr->withdrawn) {
	wmPtr->withdrawn = 1;
	wmPtr->initialState = WithdrawnState;
	TkWmUnmapWindow(winPtr);
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
        Tcl_WrongNumArgs(interp,0,objv,"wm withdraw window"); return TCL_ERROR;
    }

    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;

    if (wmPtr->iconFor != NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't withdraw %s: it is an icon for %s",
		Tk_PathName(winPtr), Tk_PathName(wmPtr->iconFor)));
	Tcl_SetErrorCode(interp, "TK", "WM", "WITHDRAW", "ICON", (char *)NULL);
	return TCL_ERROR;
    }
    wmPtr->withdrawn = 1;
    wmPtr->initialState = WithdrawnState;
    wmPtr->flags |= WM_WITHDRAWN;

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
        if (wmPtr->gridWin != NULL) {
            wmPtr->width = wmPtr->reqGridWidth
                    + (wmPtr->width - winPtr->reqWidth) / wmPtr->widthInc;
            wmPtr->height = wmPtr->reqGridHeight
                    + (wmPtr->height - winPtr->reqHeight) / wmPtr->heightInc;
        }

        /* Switch to fullscreen on the primary monitor. */
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(glfwWindow, monitor,
            0, 0, mode->width, mode->height, mode->refreshRate);
    }
    else if (!desiredFullscreen && currentMonitor != NULL) {
        /* Transitioning from fullscreen to windowed: restore saved geometry. */
        int w, h;

        WmPixelSize(winPtr, &w, &h);
        if (w <= 0) {
            w = winPtr->reqWidth;
        }
        if (h <= 0) {
            h = winPtr->reqHeight;
        }
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
	/*
	 * Before the first map, only record the state; but map the window
	 * now if Tk already tried to map it while it was withdrawn.
	 */
	wmPtr->withdrawn = (state == WithdrawnState);
	if (state != WithdrawnState && (wmPtr->flags & WM_MAP_DEFERRED)) {
	    TkWmMapWindow(winPtr);
	}
	return true;
    }
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);

    if (state == WithdrawnState) {
        wmPtr->withdrawn = 1;
        if (wmPtr->flags & WM_NEVER_MAPPED) return 1;
        if (GetOverrideInfo(winPtr) != NULL) {
            UnmapOverridePopup(winPtr);
        } else {
            if (glfwWindow) glfwHideWindow(glfwWindow);
            WaitForMapNotify(winPtr, 0);
        }
    } else if (state == NormalState) {
        wmPtr->withdrawn = 0;
        if (wmPtr->flags & WM_NEVER_MAPPED) return 0;
        UpdateHints(winPtr);
        Tk_MapWindow((Tk_Window)winPtr);
        if (glfwWindow && GetOverrideInfo(winPtr) == NULL) {
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

static void
CollectMappedToplevels(
    TkWindow *winPtr,
    TkWindow **windows,
    int *countPtr)
{
    TkWindow *childPtr;

    if (Tk_IsMapped(winPtr) && Tk_IsTopLevel(winPtr) && winPtr->wmInfoPtr) {
	if (windows != NULL) {
	    windows[*countPtr] = winPtr;
	}
	(*countPtr)++;
    }
    for (childPtr = winPtr->childList; childPtr != NULL;
	    childPtr = childPtr->nextPtr) {
	CollectMappedToplevels(childPtr, windows, countPtr);
    }
}

static int
CompareStackPos(
    const void *a,
    const void *b)
{
    double pa = ((WmInfo *)(*(TkWindow *const *)a)->wmInfoPtr)->stackPos;
    double pb = ((WmInfo *)(*(TkWindow *const *)b)->wmInfoPtr)->stackPos;

    return (pa > pb) - (pa < pb);
}

TkWindow **
TkWmStackorderToplevel(
		       TkWindow *parentPtr)
{
    TkWindow **windows;
    int count = 0;

    /*
     * The compositor does not tell the stacking order of the windows, so it
     * is the order in which Tk mapped and restacked them (see
     * TkWmRestackToplevel), from the bottom to the top.
     */
    CollectMappedToplevels(parentPtr, NULL, &count);
    windows = (TkWindow **)ckalloc((count + 1) * sizeof(TkWindow *));
    count = 0;
    CollectMappedToplevels(parentPtr, windows, &count);
    qsort(windows, count, sizeof(TkWindow *), CompareStackPos);
    windows[count] = NULL;
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
	/*
	 * Keep a size set with "wm geometry": as on X11 it stays in force
	 * until "wm geometry" changes it, whatever the content requests.
	 * Tk sends this event itself, also when the window is first mapped.
	 */
	DEBUG_LOG("ConfigureNotify received for %s", Tk_PathName(winPtr));
        break;
    case MapNotify:
	DEBUG_LOG("MapNotify received for %s", Tk_PathName(winPtr));
        winPtr->flags |= TK_MAPPED;
        /* Ensure VisibilityNotify follows MapNotify on Wayland.*/
        {
            XEvent vev;
            memset(&vev, 0, sizeof(XEvent));
            vev.type = VisibilityNotify;
            vev.xvisibility.serial = LastKnownRequestProcessed(winPtr->display)++;
            vev.xvisibility.send_event = False;
            vev.xvisibility.display = winPtr->display;
            vev.xvisibility.window = Tk_WindowId((Tk_Window)winPtr);
            vev.xvisibility.state = VisibilityUnobscured;
            Tk_QueueWindowEvent(&vev, TCL_QUEUE_TAIL);
        }
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
	CancelGeometryUpdate(winPtr);
	//return;
    }

    /*
     * As on X11, also update a withdrawn window which has been mapped
     * before; see UpdateGeometryInfo.
     */
    if (Tk_IsMapped(winPtr) || !(wmPtr->flags & WM_NEVER_MAPPED)) {
	wmPtr->flags |= (WM_UPDATE_PENDING | WM_UPDATE_SIZE_HINTS);
	DEBUG_LOG("TopLevelReqProc: scheduling UpdateGeometryInfo %s",
	    Tk_PathName(winPtr));
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
 * 	called directly by that TkWmMapWindow when a toplevel is first mapped,
 * 	and used as idle task by UpdateGeometryInfo.  The size is set to
 *  wmPtr->width x wmPtr->height if those values are both positive, or
 *  to winPtr->reqWidth x winPtr->reqHeight if not.
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
     * wmPtr value is negative (no size set with "wm geometry") we use the
     * reqWidth or reqHeight stored in the TkWindow struct.  As on X11, a
     * size set with "wm geometry" overrides the requested size.
     */

    WmPixelSize(winPtr, &tw, &th);
    if (tw < 0) tw = winPtr->reqWidth;
    if (th < 0) th = winPtr->reqHeight;

    /* Ensure at least minimum size, and at most maximum size. */
    if (wmPtr->maxWidth > 0 && tw > wmPtr->maxWidth)  tw = wmPtr->maxWidth;
    if (wmPtr->maxHeight > 0 && th > wmPtr->maxHeight) th = wmPtr->maxHeight;
    if (tw < wmPtr->minWidth)  tw = wmPtr->minWidth;
    if (th < wmPtr->minHeight) th = wmPtr->minHeight;

    if (wmPtr->gridWin != NULL) {
	/*
	 * For a gridded window the size and its limits are in grid units, as
	 * on X11.
	 */

	int minW, minH, maxW, maxH;

	WmPixelSize(winPtr, &tw, &th);
	if (tw < 0) tw = winPtr->reqWidth;
	if (th < 0) th = winPtr->reqHeight;
	WmPixelLimits(winPtr, &minW, &minH, &maxW, &maxH);
	if (tw < minW) {
	    tw = minW;
	} else if (maxW > 0 && tw > maxW) {
	    tw = maxW;
	}
	if (th < minH) {
	    th = minH;
	} else if (maxH > 0 && th > maxH) {
	    th = maxH;
	}
	if (tw <= 0) tw = 1;
	if (th <= 0) th = 1;
    }

    /* An override-redirect toplevel is a subsurface: resize that. */
    if (GetOverridePopup(winPtr) != NULL) {
	ResizeOverridePopup(winPtr, tw, th);
	return;
    }

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
	 * that the window is always at least TK_WAYLAND_CSD_MIN_WIDTH
	 * logical pixels wide.
	 *
	 * This only happens with decorations drawn by the client (libdecor
	 * with GNOME).  If the compositor draws them, the window can have
	 * any width.
	 */

	if (!tkWaylandServerDecorations && tw < TK_WAYLAND_CSD_MIN_WIDTH) {
	    tw = TK_WAYLAND_CSD_MIN_WIDTH;
	}

	DEBUG_LOG("ApplyPendingGeometry: calling glfwSetWindowSize %s -> %dx%d",
	    Tk_PathName(winPtr), tw, th);
	TkWaylandFlushConfigures();
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

/*
 *----------------------------------------------------------------------
 *
 * CancelGeometryUpdate --
 *
 *	Cancel a pending call of UpdateGeometryInfo: the idle call, and the
 *	timer which repeats it while the window waits for its first focus.
 *	The timer must be deleted with the window, or it fires on freed
 *	memory.
 *
 *----------------------------------------------------------------------
 */

static void
CancelGeometryUpdate(
    TkWindow *winPtr)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;

    Tcl_CancelIdleCall(UpdateGeometryInfo, (void *)winPtr);
    if (wmPtr != NULL && wmPtr->updateTimer != NULL) {
	Tcl_DeleteTimerHandler(wmPtr->updateTimer);
	wmPtr->updateTimer = NULL;
    }
}

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
     * If this call is from the timer, the timer has fired.  Otherwise
     * delete the pending timer, or it would be lost and fire later,
     * possibly after the window was destroyed.
     */
    if (wmPtr->updateTimer != NULL) {
	Tcl_DeleteTimerHandler(wmPtr->updateTimer);
	wmPtr->updateTimer = NULL;
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

    if (GetOverridePopup(winPtr) != NULL) {
	/*
	 * The GLFW window of an override-redirect toplevel is never shown, so
	 * do not wait for it to become visible.
	 */
	wmPtr->flags &= ~WM_UPDATE_PENDING;
	ApplyPendingGeometry(winPtr);
	return;
    }
    if (!glfwGetWindowAttrib(glfwWindow, GLFW_VISIBLE)
	    && (infoPtr->flags & TKWL_SHOWN)) {
	/*
	 * A window which has been shown before and is now hidden (e.g.
	 * withdrawn) can be resized: GLFW keeps the size and uses it when the
	 * window is shown again.  As on X11, resize it now, so that its
	 * children are laid out for the new size.
	 */
	wmPtr->flags &= ~WM_UPDATE_PENDING;
	if (wmPtr->flags & WM_UPDATE_SIZE_HINTS) {
	    UpdateSizeHints(winPtr);
	    wmPtr->flags &= ~WM_UPDATE_SIZE_HINTS;
	}
	DEBUG_LOG("UpdateGeometryInfo: resizing hidden %s", Tk_PathName(winPtr));
	ApplyPendingGeometry(winPtr);
	return;
    }
    if (!glfwGetWindowAttrib(glfwWindow, GLFW_VISIBLE)) {
	/* Newly created windows are hidden until they are first shown.  If a
	 * window is resized before it has been shown, the missing window
	 * decorations trigger a wayland error which resets the size back to
	 * the last successful size, which will be its initial size, 200x200.
	 * So we wait until GLFW reports the window as visible, which happens
	 * when the compositor has configured it.  Do not wait for the focus:
	 * the compositor may never give it (no keyboard, or focus stealing
	 * prevention).
	 */
	wmPtr->flags |= WM_UPDATE_PENDING;
	DEBUG_LOG("UpdateGeometryInfo: waiting for the window to be shown.");
	wmPtr->updateTimer = Tcl_CreateTimerHandler(17, UpdateGeometryInfo,
		clientData);
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

    DEBUG_LOG("UpdateGeometryInfo: calling ApplyPendingGeometry");
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

    int minW, minH, maxW, maxH;

    WmPixelLimits(winPtr, &minW, &minH, &maxW, &maxH);
    glfwSetWindowSizeLimits(glfwWindow, minW, minH,
	 (maxW > 0) ? maxW : GLFW_DONT_CARE,
	 (maxH > 0) ? maxH : GLFW_DONT_CARE);

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

    /* The leading "=" is optional. */
    if (*p == '=') {
        p++;
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
        /*
         * As on X11, a negative offset is the distance from the right or
         * bottom edge of the screen, and is kept with its sign flag, so
         * that "-0" is reported back.
         */
        wmPtr->x = x;
        wmPtr->y = y;

        if (xNeg) wmPtr->flags |= WM_NEGATIVE_X;
        else      wmPtr->flags &= ~WM_NEGATIVE_X;
        if (yNeg) wmPtr->flags |= WM_NEGATIVE_Y;
        else      wmPtr->flags &= ~WM_NEGATIVE_Y;

        wmPtr->flags |= WM_MOVE_PENDING;
    }

    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;

    return TCL_OK;

 badGeom:
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "bad geometry specifier \"%s\"", string));
    Tcl_SetErrorCode(interp, "TK", "VALUE", "GEOMETRY", (char *)NULL);
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

    /*
     * If the update is only pending as the timer which waits for the window
     * to be shown, schedule it at idle too: the window may be shown by now,
     * and "update" does not wait for the timer.
     */
    if ((wmPtr->flags & WM_UPDATE_PENDING) && wmPtr->updateTimer == NULL) {
	return;
    }
    wmPtr->flags |= WM_UPDATE_PENDING;
    Tcl_CancelIdleCall(UpdateGeometryInfo, (void *)winPtr);
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

    /*
     * A transient follows the state of its container, unless it was
     * withdrawn explicitly.
     */
    if (wmPtr == NULL || wmPtr->containerPtr == NULL) {
	return;
    }
    if (eventPtr->type == MapNotify) {
	if (!(wmPtr->flags & WM_WITHDRAWN) && wmPtr->withdrawn) {
	    wmPtr->withdrawn = 0;
	    wmPtr->initialState = NormalState;
	    if (!(wmPtr->flags & WM_NEVER_MAPPED)
		    || (wmPtr->flags & WM_MAP_DEFERRED)) {
		TkWmMapWindow(winPtr);
	    }
	}
    } else if (eventPtr->type == UnmapNotify) {
	if (!wmPtr->withdrawn) {
	    wmPtr->withdrawn = 1;
	    wmPtr->initialState = WithdrawnState;
	    TkWmUnmapWindow(winPtr);
	}
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
 * ExposeVacatedArea --
 *
 *	A child window is being unmapped or destroyed.  Children are painted
 *	straight into the toplevel's backing FBO, so the pixels the child
 *	covered remain there until its parent repaints them -- which an X
 *	server would arrange by sending Expose to the parent.  Do the
 *	equivalent here: expose the parent (and, via
 *	TkWaylandQueueExposeEvent, its still-live mapped children) and
 *	invalidate the clip rects so the parent is no longer clipped around
 *	the vanished window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues Expose events and marks clip rects dirty.  Does nothing for
 *	toplevels, or when the parent is itself being destroyed (the whole
 *	subtree is going away, so there is nothing to repaint).
 *
 *----------------------------------------------------------------------
 */

static void
ExposeVacatedArea(
    TkWindow *winPtr)
{
    TkWindow *parentPtr, *topPtr;

    if (winPtr == NULL) {
	return;
    }
    parentPtr = winPtr->parentPtr;
    if (parentPtr == NULL || (winPtr->flags & TK_TOP_HIERARCHY)
	    || (parentPtr->flags & TK_ALREADY_DEAD)) {
	return;
    }

    TkWaylandQueueExposeEvent(parentPtr, 0, 0,
	    Tk_Width(parentPtr), Tk_Height(parentPtr));

    /*
     * As in XMapWindow, invalidate the clip rects after queuing the
     * exposes.
     */

    for (topPtr = parentPtr;
	    topPtr != NULL && !(topPtr->flags & TK_TOP_LEVEL);
	    topPtr = topPtr->parentPtr) {
	/* empty */
    }
    if (topPtr != NULL) {
	tkWaylandInvalidateClipRectsForTree(topPtr);
    } else {
	tkWaylandInvalidateClipRects(parentPtr);
    }
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
 *	Queues Expose events so the parent repaints the vacated area, and
 *	may clear the grab/restrict/last-window state in tkPointer.c.
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
	TkWindow *winPtr = (TkWindow *)window;

	/*
	 * Repaint whatever the window was covering; otherwise its pixels
	 * stay in the toplevel's backing store.
	 */

	ExposeVacatedArea(winPtr);
	TkPointerDeadWindow(winPtr);
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
    /* Child windows share the parent GLFW context - nothing to destroy. */
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
    TkWindow *winPtr = (TkWindow *) Tk_IdToWindow(display, window);
    TkWindow *topPtr;
    glfwTkInfo *infoPtr;

    DEBUG_LOG("XMapWindow: %s", Tk_PathName(winPtr));

    if (winPtr == NULL) {
        return Success;
    }

    TkWaylandQueueExposeEvent(winPtr, 0, 0,
        Tk_Width(winPtr), Tk_Height(winPtr));

    topPtr = winPtr;
    while (topPtr != NULL && !(topPtr->flags & TK_TOP_LEVEL)) {
        topPtr = topPtr->parentPtr;
    }
    if (topPtr != NULL) {
        if (topPtr->privatePtr != NULL
                && topPtr->privatePtr->glfwWindow != NULL) {
            infoPtr = (glfwTkInfo *)glfwGetWindowUserPointer(
                topPtr->privatePtr->glfwWindow);
            if (infoPtr != NULL) {
                infoPtr->flags |= TKWL_NEEDS_DISPLAY;
                infoPtr->flags &= ~TKWL_EARLY_EXPOSE_QUEUED;
            }
        }
    }
    if (winPtr->parentPtr != NULL) {
        TkWaylandQueueExposeEvent(winPtr->parentPtr, 0, 0,
            Tk_Width(winPtr->parentPtr),
            Tk_Height(winPtr->parentPtr));
    }

    /*
     * Must invalidate clip after queuing exposes, otherwise clip rebuild
     * may happen with stale geometry (widget demo single text case).
     */
    if (topPtr != NULL) {
        tkWaylandInvalidateClipRectsForTree(topPtr);
    } else {
        tkWaylandInvalidateClipRects(winPtr);
        if (winPtr->parentPtr != NULL) {
            tkWaylandInvalidateClipRects(winPtr->parentPtr);
        }
    }

    QueueVisibilityNotify(winPtr);

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

    if (IsOverrideGLFW(gw)) {
	/* Never show the hidden window of a popup; stack the popup. */
	RaiseOverridePopup(TkWaylandGetTkWindow(gw));
	return Success;
    }
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
 *	Called by Tk_UnmapWindow.  There is no native window to unmap, but
 *	the area the window covered must be repainted by its parent.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Invalidates clip rects and queues Expose events for the parent.
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
    tkWaylandInvalidateClipRects(winPtr);
    ExposeVacatedArea(winPtr);
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
    TkWindow *contPtr = TkWaylandGetContainer(winPtr);
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
    tkWaylandInvalidateClipRects(winPtr);
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
    TkWindow *contPtr = TkWaylandGetContainer(winPtr);
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
    tkWaylandInvalidateClipRects(winPtr);
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
    TkWindow *contPtr = TkWaylandGetContainer(winPtr);
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
    tkWaylandInvalidateClipRects(winPtr);
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

    if (IsOverrideGLFW(gw)) {
	RaiseOverridePopup(TkWaylandGetTkWindow(gw));
	return Success;
    }
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
        if (IsOverrideGLFW(gw)) {
            RaiseOverridePopup(TkWaylandGetTkWindow(gw));
        } else if (gw != NULL) {
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
     * CWBackPixel, CWBorderPixel, CWEventMask, CWColormap - 
     * all are maintained by Tk's own attribute tables; no GLFW action. 
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
    Display *display,
    Window focus,
    TCL_UNUSED(int),    /* revert_to */
    TCL_UNUSED(Time))   /* time      */
{
    GLFWwindow *gw;

    if (focus == None || focus == PointerRoot) {
        return Success;
    }

    gw = WindowToGLFW(focus);
    if (gw != NULL && !IsOverrideGLFW(gw)) {
        glfwFocusWindow(gw);
    }

    /* Wayland focus is async - synthesize so focus -force doesn't hang.
     * TkSetFocusWin already updates dispPtr->focusWinPtr and fires the
     * <FocusIn>/<FocusOut> binding sequence, so we must NOT also queue a
     * raw FocusIn XEvent here - doing so would cause a second, stale focus
     * transition to be delivered later by TkFocusFilterEvent. Real
     * compositor-driven activation is handled separately in
     * TkWaylandWindowFocusCallback. */
    Tk_Window focusPtr = Tk_IdToWindow(display, focus);
    TkWindow *winPtr = (TkWindow*)focusPtr;
    if (winPtr) {
        TkSetFocusWin(winPtr, 1);
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
