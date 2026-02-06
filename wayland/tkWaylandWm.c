/*

- tkWaylandWm.c –
- 
- This module takes care of the interactions between a Tk-based
- application and the window manager. Among other things, it implements
- the “wm” command and passes geometry information to the Wayland
- ```
   compositor via GLFW. Also generates X events from GLFW callbacks.
  ```
- 
- Copyright © 1991-1994 The Regents of the University of California.
- Copyright © 1994-1997 Sun Microsystems, Inc.
- Copyright © 2026 Kevin Walzer
- 
- See the file “license.terms” for information on usage and redistribution of
- this file, and for a DISCLAIMER OF ALL WARRANTIES.
  */

#include “tkInt.h”
#include “tkPort.h”
#include <GLFW/glfw3.h>
#include <string.h>
#include <stdlib.h>

/*

- Protocol identifiers - these replace X11 Atoms
  */

#define WM_DELETE_WINDOW    1
#define WM_TAKE_FOCUS      2
#define WM_SAVE_YOURSELF   3

/*

- A data structure of the following type holds information for each window
- manager protocol (such as WM_DELETE_WINDOW) for which a handler (i.e. a Tcl
- command) has been defined for a particular top-level window.
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

#define HANDLER_SIZE(cmdLength)   
(offsetof(ProtocolHandler, command) + 1 + cmdLength)

/*

- Data for [wm attributes] command:
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
“-alpha”, “-fullscreen”, “-topmost”, “-type”,
“-zoomed”, NULL
};

/*

- A data structure of the following type holds window-manager-related
- information for each top-level window in an application.
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

```
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

/*
 * Window state tracking for event generation
 */
int lastX, lastY;		/* Last reported position */
int lastWidth, lastHeight;	/* Last reported size */
int isMapped;		/* Is window currently mapped? */
int hasFocus;		/* Does window have focus? */

struct TkWmInfo *nextPtr;	/* Next in list of top-level windows. */
```

} WmInfo;

/*

- Flag values for WmInfo structures
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

- Size hint flags (from X11, kept for compatibility)
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

- Window states
  */

#define WithdrawnState 0
#define NormalState 1
#define IconicState 3

/*

- Gravity constants
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

- Global list of all top-level windows
  */

static WmInfo *firstWmPtr = NULL;

/*

- Forward declarations for internal functions
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

/*

- Forward declarations for GLFW integration
  */
  static void		GlfwCloseCallback(GLFWwindow *window);
  static void		GlfwFocusCallback(GLFWwindow *window, int focused);
  static void		GlfwIconifyCallback(GLFWwindow *window, int iconified);
  static void		GlfwMaximizeCallback(GLFWwindow *window, int maximized);
  static void		GlfwFramebufferSizeCallback(GLFWwindow *window, int width, int height);
  static void		GlfwWindowPosCallback(GLFWwindow *window, int x, int y);
  static void		GlfwWindowSizeCallback(GLFWwindow *window, int width, int height);
  static void		GlfwWindowRefreshCallback(GLFWwindow *window);
  static WmInfo*		FindWmInfoByGlfwWindow(GLFWwindow *window);
  static void		CreateGlfwWindow(TkWindow *winPtr);
  static void		DestroyGlfwWindow(WmInfo *wmPtr);
  static void		ConvertPhotoToGlfwIcon(TkWindow *winPtr, Tk_PhotoHandle photo, int index);
  static void		ApplyWindowHints(TkWindow *winPtr);
  static void		HandleProtocol(WmInfo *wmPtr, int protocol);
  static void		ApplyFullscreenState(TkWindow *winPtr);

/*

- Forward declarations for X event generation
  */
  static void		GenerateConfigureEvent(TkWindow *winPtr, int x, int y,
  int width, int height, int flags);
  static void		GenerateFocusEvent(TkWindow *winPtr, int focusIn);
  static void		GenerateExposeEvent(TkWindow *winPtr, int x, int y,
  int width, int height);
  static void		GenerateMapEvent(TkWindow *winPtr);
  static void		GenerateUnmapEvent(TkWindow *winPtr);
  static void		GenerateActivateEvents(TkWindow *winPtr, int active);

/*

- Forward declarations for “wm” subcommands
  */
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

- Geometry manager type
  */

static Tk_GeomMgr wmMgrType = {
“wm”,			/* name */
TopLevelReqProc,		/* requestProc */
NULL,			/* lostSlaveProc */
};

/*
*–––––––––––––––––––––––––––––––––––
*

- X Event Generation Functions
- 
- These functions generate X events from GLFW callbacks and queue
- them into Tk’s event system.
- 

*–––––––––––––––––––––––––––––––––––
*/

/*
*–––––––––––––––––––––––––––––––––––
*

- GenerateConfigureEvent –
- 
- Generates a ConfigureNotify event for window geometry changes.
- 
- Results:
- None.
- 
- Side effects:
- ConfigureNotify event is queued.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
GenerateConfigureEvent(
TkWindow *winPtr,
int x, int y,
int width, int height,
int flags)
{
XEvent event;
WmInfo *wmPtr = winPtr->wmInfoPtr;

```
if (winPtr == NULL || wmPtr == NULL) {
return;
}

memset(&event, 0, sizeof(XEvent));
event.type = ConfigureNotify;
event.xconfigure.serial = LastKnownRequestProcessed(Tk_Display(winPtr));
event.xconfigure.send_event = False;
event.xconfigure.display = Tk_Display(winPtr);
event.xconfigure.event = Tk_WindowId(winPtr);
event.xconfigure.window = Tk_WindowId(winPtr);
event.xconfigure.x = x;
event.xconfigure.y = y;
event.xconfigure.width = width;
event.xconfigure.height = height;
event.xconfigure.border_width = winPtr->changes.border_width;
event.xconfigure.above = None;
event.xconfigure.override_redirect = winPtr->atts.override_redirect;

Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

/* Update internal state */
winPtr->changes.x = x;
winPtr->changes.y = y;
winPtr->changes.width = width;
winPtr->changes.height = height;

wmPtr->lastX = x;
wmPtr->lastY = y;
wmPtr->lastWidth = width;
wmPtr->lastHeight = height;
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- GenerateFocusEvent –
- 
- Generates FocusIn or FocusOut events.
- 
- Results:
- None.
- 
- Side effects:
- Focus event is queued.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
GenerateFocusEvent(
TkWindow *winPtr,
int focusIn)
{
XEvent event;

```
if (winPtr == NULL) {
return;
}

memset(&event, 0, sizeof(XEvent));
event.type = focusIn ? FocusIn : FocusOut;
event.xfocus.serial = LastKnownRequestProcessed(Tk_Display(winPtr));
event.xfocus.send_event = False;
event.xfocus.display = Tk_Display(winPtr);
event.xfocus.window = Tk_WindowId(winPtr);
event.xfocus.mode = NotifyNormal;
event.xfocus.detail = NotifyAncestor;

Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- GenerateExposeEvent –
- 
- Generates an Expose event for a damaged region.
- 
- Results:
- None.
- 
- Side effects:
- Expose event is queued.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
GenerateExposeEvent(
TkWindow *winPtr,
int x, int y,
int width, int height)
{
XEvent event;

```
if (winPtr == NULL || !Tk_IsMapped((Tk_Window)winPtr)) {
return;
}

memset(&event, 0, sizeof(XEvent));
event.type = Expose;
event.xexpose.serial = LastKnownRequestProcessed(Tk_Display(winPtr));
event.xexpose.send_event = False;
event.xexpose.display = Tk_Display(winPtr);
event.xexpose.window = Tk_WindowId(winPtr);
event.xexpose.x = x;
event.xexpose.y = y;
event.xexpose.width = width;
event.xexpose.height = height;
event.xexpose.count = 0;

Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- GenerateMapEvent –
- 
- Generates a MapNotify event.
- 
- Results:
- None.
- 
- Side effects:
- MapNotify event is queued, window marked as mapped.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
GenerateMapEvent(
TkWindow *winPtr)
{
XEvent event;
WmInfo *wmPtr = winPtr->wmInfoPtr;

```
if (winPtr == NULL || wmPtr == NULL) {
return;
}

if (wmPtr->isMapped) {
return;  /* Already mapped */
}

memset(&event, 0, sizeof(XEvent));
event.type = MapNotify;
event.xmap.serial = LastKnownRequestProcessed(Tk_Display(winPtr));
event.xmap.send_event = False;
event.xmap.display = Tk_Display(winPtr);
event.xmap.event = Tk_WindowId(winPtr);
event.xmap.window = Tk_WindowId(winPtr);
event.xmap.override_redirect = winPtr->atts.override_redirect;

Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

wmPtr->isMapped = 1;
winPtr->flags |= TK_MAPPED;
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- GenerateUnmapEvent –
- 
- Generates an UnmapNotify event.
- 
- Results:
- None.
- 
- Side effects:
- UnmapNotify event is queued, window marked as unmapped.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
GenerateUnmapEvent(
TkWindow *winPtr)
{
XEvent event;
WmInfo *wmPtr = winPtr->wmInfoPtr;

```
if (winPtr == NULL || wmPtr == NULL) {
return;
}

if (!wmPtr->isMapped) {
return;  /* Already unmapped */
}

memset(&event, 0, sizeof(XEvent));
event.type = UnmapNotify;
event.xunmap.serial = LastKnownRequestProcessed(Tk_Display(winPtr));
event.xunmap.send_event = False;
event.xunmap.display = Tk_Display(winPtr);
event.xunmap.event = Tk_WindowId(winPtr);
event.xunmap.window = Tk_WindowId(winPtr);
event.xunmap.from_configure = False;

Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

wmPtr->isMapped = 0;
winPtr->flags &= ~TK_MAPPED;
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- GenerateActivateEvents –
- 
- Generates ActivateNotify and DeactivateNotify events.
- These are Tk-specific virtual events.
- 
- Results:
- None.
- 
- Side effects:
- Activate/Deactivate events are generated via TkGenerateActivateEvents.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
GenerateActivateEvents(
TkWindow *winPtr,
int active)
{
if (winPtr == NULL) {
return;
}

```
/* Use Tk's built-in activate event generator */
TkGenerateActivateEvents(winPtr, active);
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- GLFW Callback Functions
- 
- These callbacks are invoked by GLFW when window events occur.
- They translate GLFW events into X events and queue them.
- 

*–––––––––––––––––––––––––––––––––––
*/

/*
*–––––––––––––––––––––––––––––––––––
*

- GlfwWindowPosCallback –
- 
- Called when window position changes.
- 
- Results:
- None.
- 
- Side effects:
- ConfigureNotify event generated if position changed.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
GlfwWindowPosCallback(
GLFWwindow *window,
int x, int y)
{
WmInfo *wmPtr = FindWmInfoByGlfwWindow(window);

```
if (wmPtr == NULL || wmPtr->winPtr == NULL) {
return;
}

TkWindow *winPtr = wmPtr->winPtr;

/* Only generate event if position actually changed */
if (wmPtr->lastX != x || wmPtr->lastY != y) {
GenerateConfigureEvent(winPtr, x, y, 
		      wmPtr->lastWidth, wmPtr->lastHeight,
		      TK_LOCATION_CHANGED);
wmPtr->x = x;
wmPtr->y = y;
}
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- GlfwWindowSizeCallback –
- 
- Called when window size changes.
- 
- Results:
- None.
- 
- Side effects:
- ConfigureNotify event generated if size changed.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
GlfwWindowSizeCallback(
GLFWwindow *window,
int width, int height)
{
WmInfo *wmPtr = FindWmInfoByGlfwWindow(window);

```
if (wmPtr == NULL || wmPtr->winPtr == NULL) {
return;
}

TkWindow *winPtr = wmPtr->winPtr;

/* Get current position */
int x, y;
glfwGetWindowPos(window, &x, &y);

/* Only generate event if size actually changed */
if (wmPtr->lastWidth != width || wmPtr->lastHeight != height) {
GenerateConfigureEvent(winPtr, x, y, width, height,
		      TK_SIZE_CHANGED | TK_LOCATION_CHANGED);

wmPtr->configWidth = width;
wmPtr->configHeight = height;
}
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- GlfwFramebufferSizeCallback –
- 
- Called when framebuffer size changes (for HiDPI).
- This triggers a redraw but rendering is handled by nanovg elsewhere.
- 
- Results:
- None.
- 
- Side effects:
- Expose event generated to trigger redraw.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
GlfwFramebufferSizeCallback(
GLFWwindow *window,
int fbWidth, int fbHeight)
{
WmInfo *wmPtr = FindWmInfoByGlfwWindow(window);

```
if (wmPtr == NULL || wmPtr->winPtr == NULL) {
return;
}

TkWindow *winPtr = wmPtr->winPtr;

/* Get window size (not framebuffer size) for event */
int width, height;
glfwGetWindowSize(window, &width, &height);

/* Generate expose event to trigger redraw with new scale */
GenerateExposeEvent(winPtr, 0, 0, width, height);
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- GlfwWindowRefreshCallback –
- 
- Called when window needs to be redrawn.
- 
- Results:
- None.
- 
- Side effects:
- Expose event generated.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
GlfwWindowRefreshCallback(
GLFWwindow *window)
{
WmInfo *wmPtr = FindWmInfoByGlfwWindow(window);

```
if (wmPtr == NULL || wmPtr->winPtr == NULL) {
return;
}

TkWindow *winPtr = wmPtr->winPtr;

/* Get window size */
int width, height;
glfwGetWindowSize(window, &width, &height);

/* Generate expose event for entire window */
GenerateExposeEvent(winPtr, 0, 0, width, height);
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- GlfwWindowCloseCallback –
- 
- Called when window close is requested (X button, etc.).
- 
- Results:
- None.
- 
- Side effects:
- WM_DELETE_WINDOW protocol handler invoked.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
GlfwCloseCallback(
GLFWwindow *window)
{
WmInfo *wmPtr = FindWmInfoByGlfwWindow(window);

```
if (wmPtr != NULL) {
/* Don't let GLFW close the window - let Tk handle it */
glfwSetWindowShouldClose(window, GLFW_FALSE);

/* Handle WM_DELETE_WINDOW protocol */
HandleProtocol(wmPtr, WM_DELETE_WINDOW);
}
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- GlfwFocusCallback –
- 
- Called when window gains or loses focus.
- 
- Results:
- None.
- 
- Side effects:
- FocusIn/FocusOut and Activate/Deactivate events generated.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
GlfwFocusCallback(
GLFWwindow *window,
int focused)
{
WmInfo *wmPtr = FindWmInfoByGlfwWindow(window);

```
if (wmPtr == NULL || wmPtr->winPtr == NULL) {
return;
}

TkWindow *winPtr = wmPtr->winPtr;

/* Avoid duplicate events */
if (wmPtr->hasFocus == focused) {
return;
}

wmPtr->hasFocus = focused;

/* Generate focus events */
GenerateFocusEvent(winPtr, focused);

/* Generate activate/deactivate events */
if (Tk_IsMapped((Tk_Window)winPtr)) {
GenerateActivateEvents(winPtr, focused);
}

/* Handle WM_TAKE_FOCUS protocol when gaining focus */
if (focused) {
HandleProtocol(wmPtr, WM_TAKE_FOCUS);
}
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- GlfwIconifyCallback –
- 
- Called when window is iconified or restored.
- 
- Results:
- None.
- 
- Side effects:
- UnmapNotify (iconify) or MapNotify (restore) event generated.
- Window state updated.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
GlfwIconifyCallback(
GLFWwindow *window,
int iconified)
{
WmInfo *wmPtr = FindWmInfoByGlfwWindow(window);

```
if (wmPtr == NULL || wmPtr->winPtr == NULL) {
return;
}

TkWindow *winPtr = wmPtr->winPtr;

if (iconified) {
/* Window is being iconified */
GenerateUnmapEvent(winPtr);
wmPtr->attributes.zoomed = 0;

/* Update Tk state */
TkpWmSetState(winPtr, IconicState);
} else {
/* Window is being restored from iconified state */
GenerateMapEvent(winPtr);

/* Check if it's also maximized */
int maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
if (maximized) {
    wmPtr->attributes.zoomed = 1;
}

/* Update Tk state */
TkpWmSetState(winPtr, NormalState);
}
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- GlfwMaximizeCallback –
- 
- Called when window is maximized or unmaximized.
- 
- Results:
- None.
- 
- Side effects:
- Window zoomed attribute updated.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
GlfwMaximizeCallback(
GLFWwindow *window,
int maximized)
{
WmInfo *wmPtr = FindWmInfoByGlfwWindow(window);

```
if (wmPtr == NULL) {
return;
}

wmPtr->attributes.zoomed = maximized;
wmPtr->reqState.zoomed = maximized;
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- FindWmInfoByGlfwWindow –
- 
- Finds the WmInfo structure for a GLFW window.
- 
- Results:
- Pointer to WmInfo, or NULL if not found.
- 
- Side effects:
- None.
- 

*–––––––––––––––––––––––––––––––––––
*/

static WmInfo*
FindWmInfoByGlfwWindow(
GLFWwindow *window)
{
WmInfo *wmPtr;

```
/* First try the user pointer (fastest) */
wmPtr = (WmInfo *)glfwGetWindowUserPointer(window);
if (wmPtr != NULL && wmPtr->glfwWindow == window) {
return wmPtr;
}

/* Fall back to searching the list */
for (wmPtr = firstWmPtr; wmPtr != NULL; wmPtr = wmPtr->nextPtr) {
if (wmPtr->glfwWindow == window) {
    return wmPtr;
}
}

return NULL;
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- CreateGlfwWindow –
- 
- Creates a GLFW window for a Tk top-level window.
- Registers all GLFW callbacks for event handling.
- 
- Results:
- None.
- 
- Side effects:
- GLFW window is created and callbacks are set up.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
CreateGlfwWindow(
TkWindow *winPtr)
{
WmInfo *wmPtr = winPtr->wmInfoPtr;
int width, height;
GLFWwindow *share = NULL;

```
if (wmPtr->glfwWindow != NULL) {
return;  /* Already created */
}

/* Determine initial size */
if (wmPtr->width > 0 && wmPtr->height > 0) {
width = wmPtr->width;
height = wmPtr->height;
} else {
width = winPtr->reqWidth > 0 ? winPtr->reqWidth : 200;
height = winPtr->reqHeight > 0 ? winPtr->reqHeight : 200;
}

/* Find a window to share resources with (for OpenGL context) */
for (WmInfo *other = firstWmPtr; other != NULL; other = other->nextPtr) {
if (other->glfwWindow != NULL && other != wmPtr) {
    share = other->glfwWindow;
    break;
}
}

/* Apply window hints before creation */
ApplyWindowHints(winPtr);

/* Create the window */
const char *title = wmPtr->title ? wmPtr->title : 
	       (winPtr->nameUid ? winPtr->nameUid : "Tk");
wmPtr->glfwWindow = glfwCreateWindow(width, height, title, NULL, share);

if (wmPtr->glfwWindow == NULL) {
Tcl_Panic("Failed to create GLFW window");
return;
}

/* Store user pointer for finding WmInfo later */
glfwSetWindowUserPointer(wmPtr->glfwWindow, wmPtr);

/* Register ALL window event callbacks */
glfwSetWindowPosCallback(wmPtr->glfwWindow, GlfwWindowPosCallback);
glfwSetWindowSizeCallback(wmPtr->glfwWindow, GlfwWindowSizeCallback);
glfwSetWindowCloseCallback(wmPtr->glfwWindow, GlfwCloseCallback);
glfwSetWindowRefreshCallback(wmPtr->glfwWindow, GlfwWindowRefreshCallback);
glfwSetWindowFocusCallback(wmPtr->glfwWindow, GlfwFocusCallback);
glfwSetWindowIconifyCallback(wmPtr->glfwWindow, GlfwIconifyCallback);
glfwSetWindowMaximizeCallback(wmPtr->glfwWindow, GlfwMaximizeCallback);
glfwSetFramebufferSizeCallback(wmPtr->glfwWindow, GlfwFramebufferSizeCallback);

/* Note: Mouse and keyboard callbacks are handled elsewhere */

/* Set initial properties */
UpdateTitle(winPtr);
UpdateSizeHints(winPtr);

if (wmPtr->attributes.alpha != 1.0) {
glfwSetWindowOpacity(wmPtr->glfwWindow, (float)wmPtr->attributes.alpha);
}

/* Set window icon if available */
if (wmPtr->glfwIcon != NULL && wmPtr->glfwIconCount > 0) {
glfwSetWindowIcon(wmPtr->glfwWindow, wmPtr->glfwIconCount, wmPtr->glfwIcon);
}

/* Apply initial state */
if (wmPtr->attributes.zoomed) {
glfwMaximizeWindow(wmPtr->glfwWindow);
}

if (wmPtr->attributes.fullscreen) {
ApplyFullscreenState(winPtr);
}

/* Initialize tracking variables */
glfwGetWindowPos(wmPtr->glfwWindow, &wmPtr->lastX, &wmPtr->lastY);
glfwGetWindowSize(wmPtr->glfwWindow, &wmPtr->lastWidth, &wmPtr->lastHeight);
wmPtr->isMapped = 0;
wmPtr->hasFocus = 0;
```

}

/*
*–––––––––––––––––––––––––––––––––––
*

- DestroyGlfwWindow –
- 
- Destroys a GLFW window.
- 
- Results:
- None.
- 
- Side effects:
- GLFW window is destroyed.
- 

*–––––––––––––––––––––––––––––––––––
*/

static void
DestroyGlfwWindow(
WmInfo *wmPtr)
{
if (wmPtr->glfwWindow == NULL) {
return;
}

```
/* Remove callbacks first */
glfwSetWindowPosCallback(wmPtr->glfwWindow, NULL);
glfwSetWindowSizeCallback(wmPtr->glfwWindow, NULL);
glfwSetWindowCloseCallback(wmPtr->glfwWindow, NULL);
glfwSetWindowRefreshCallback(wmPtr->glfwWindow, NULL);
glfwSetWindowFocusCallback(wmPtr->glfwWindow, NULL);
glfwSetWindowIconifyCallback(wmPtr->glfwWindow, NULL);
glfwSetWindowMaximizeCallback(wmPtr->glfwWindow, NULL);
glfwSetFramebufferSizeCallback(wmPtr->glfwWindow, NULL);
glfwSetWindowUserPointer(wmPtr->glfwWindow, NULL);

glfwDestroyWindow(wmPtr->glfwWindow);
wmPtr->glfwWindow = NULL;
```

}

/*

- Remaining functions from original file would go here:
- - TkWmNewWindow
- - TkWmMapWindow
- - TkWmUnmapWindow
- - TkWmDeadWindow
- - All wm subcommand implementations
- - Helper functions
- etc.
- 
- (Due to length limits, showing the critical new parts above)
  */

/*

- Local Variables:
- mode: c
- c-basic-offset: 4
- fill-column: 78
- End:
  */