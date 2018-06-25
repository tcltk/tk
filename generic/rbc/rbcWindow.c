/*
 * rbcWindow.c --
 *
 *      This module implements additional window functionality for
 *      the rbc toolkit, such as transparent Tk windows,
 *      and reparenting Tk windows.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

#include <X11/Xlib.h>

typedef struct TkIdStackStruct TkIdStack;
typedef struct TkSelectionInfoStruct TkSelectionInfo;
typedef struct TkClipboardTargetStruct TkClipboardTarget;

typedef struct TkWindowEventStruct TkWindowEvent;
typedef struct TkSelHandlerStruct TkSelHandler;
typedef struct TkWinInfoStruct TkWinInfo;
typedef struct TkClassProcsStruct TkClassProcs;
typedef struct TkWindowPrivateStruct TkWindowPrivate;
typedef struct TkWmInfoStruct TkWmInfo;

#ifdef XNQueryInputStyle
#define TK_USE_INPUT_METHODS
#endif

/*
 * This defines whether we should try to use XIM over-the-spot style
 * input.  Allow users to override it.  It is a much more elegant use
 * of XIM, but uses a bit more memory.
 */
#ifndef TK_XIM_SPOT
#   define TK_XIM_SPOT	1
#endif

#ifndef TK_REPARENTED
#define TK_REPARENTED 	0
#endif

struct TkWindowStruct {
    Display        *display;
    TkDisplay      *dispPtr;
    int             screenNum;
    Visual         *visual;
    int             depth;
    Window          window;
    TkWindow       *childList;
    TkWindow       *lastChildPtr;
    TkWindow       *parentPtr;
    TkWindow       *nextPtr;
    TkMainInfo     *infoPtr;
    char           *pathName;
    Tk_Uid          nameUid;
    Tk_Uid          classUid;
    XWindowChanges  changes;
    unsigned int    dirtyChanges;
    XSetWindowAttributes atts;
    unsigned long   dirtyAtts;
    unsigned int    flags;
    TkEventHandler *handlerList;
#ifdef TK_USE_INPUT_METHODS
    XIC             inputContext;
#endif                          /* TK_USE_INPUT_METHODS */
    ClientData     *tagPtr;
    int             nTags;
    int             optionLevel;
    TkSelHandler   *selHandlerList;
    Tk_GeomMgr     *geomMgrPtr;
    ClientData      geomData;
    int             reqWidth, reqHeight;
    int             internalBorderWidth;
    TkWinInfo      *wmInfoPtr;
    TkClassProcs   *classProcsPtr;
    ClientData      instanceData;
    TkWindowPrivate *privatePtr;
};

static void     DoConfigureNotify(
    Tk_FakeWin * winPtr);
static void     UnlinkWindow(
    TkWindow * winPtr);
static int      GetWindowSize(
    Tcl_Interp * interp,
    Window window,
    int *widthPtr,
    int *heightPtr);

#ifdef _WIN32

/*
 *----------------------------------------------------------------------
 *
 * GetWindowHandle --
 *
 *      Returns the XID for the Tk_Window given.  Starting in Tk 8.0,
 *      the toplevel widgets are wrapped by another window.
 *      Currently there's no way to get at that window, other than
 *      what is done here: query the X window hierarchy and grab the
 *      parent.
 *
 * Results:
 *      Returns the X Window ID of the widget.  If it's a toplevel,
 *      then the XID of the wrapper is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static          HWND
GetWindowHandle(
    Tk_Window tkwin)
{
    HWND            hWnd;
    Window          window;

    window = Tk_WindowId(tkwin);
    if (window == None) {
        Tk_MakeWindowExist(tkwin);
    }
    hWnd = Tk_GetHWND(Tk_WindowId(tkwin));
    if (Tk_IsTopLevel(tkwin)) {
        hWnd = GetParent(hWnd);
    }
    return hWnd;
}

#else

/*
 *--------------------------------------------------------------
 *
 * RbcGetParent --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Window
RbcGetParent(
    Display * display,
    Window window)
{
    Window          root, parent;
    Window         *dummy;
    unsigned int    count;

    if (XQueryTree(display, window, &root, &parent, &dummy, &count) > 0) {
        XFree(dummy);
        return parent;
    }
    return None;
}

/*
 *--------------------------------------------------------------
 *
 * GetWindowId --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static          Window
GetWindowId(
    Tk_Window tkwin)
{
    Window          window;

    Tk_MakeWindowExist(tkwin);
    window = Tk_WindowId(tkwin);

    if (Tk_IsTopLevel(tkwin)) {
        Window          parent;

        parent = RbcGetParent(Tk_Display(tkwin), window);
        if (parent != None) {
            window = parent;
        }
        window = parent;
    }
    return window;
}

#endif /* _WIN32 */

/*
 *----------------------------------------------------------------------
 *
 * DoConfigureNotify --
 *
 *      Generate a ConfigureNotify event describing the current
 *      configuration of a window.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      An event is generated and processed by Tk_HandleEvent.
 *
 *----------------------------------------------------------------------
 */
static void
DoConfigureNotify(
    Tk_FakeWin * winPtr)
{                               /* Window whose configuration was just
                                 * changed. */
    XEvent          event;

    event.type = ConfigureNotify;
    event.xconfigure.serial = LastKnownRequestProcessed(winPtr->display);
    event.xconfigure.send_event = False;
    event.xconfigure.display = winPtr->display;
    event.xconfigure.event = winPtr->window;
    event.xconfigure.window = winPtr->window;
    event.xconfigure.x = winPtr->changes.x;
    event.xconfigure.y = winPtr->changes.y;
    event.xconfigure.width = winPtr->changes.width;
    event.xconfigure.height = winPtr->changes.height;
    event.xconfigure.border_width = winPtr->changes.border_width;
    if (winPtr->changes.stack_mode == Above) {
        event.xconfigure.above = winPtr->changes.sibling;
    } else {
        event.xconfigure.above = None;
    }
    event.xconfigure.override_redirect = winPtr->atts.override_redirect;
    Tk_HandleEvent(&event);
}

/*
 *--------------------------------------------------------------
 *
 * RbcMakeTransparentWindowExist --
 *
 *      Similar to Tk_MakeWindowExist but instead creates a
 *      transparent window to block for user events from
 *      sibling windows.
 *
 *      Differences from Tk_MakeWindowExist.
 *
 *        1. This is always a "busy" window. There's never a
 *           platform-specific class procedure to execute
 *           instead.
 *        2. The window is transparent and never will contain
 *           children, so colormap information is irrelevant.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      When the procedure returns, the internal window
 *      associated with tkwin is guaranteed to exist.  This
 *      may require the window's ancestors to be created too.
 *
 *--------------------------------------------------------------
 */
void
RbcMakeTransparentWindowExist(
    Tk_Window tkwin,            /* Token for window. */
    Window parent,              /* Parent window. */
    int isBusy)
{                               /*  */
    TkWindow       *winPtr = (TkWindow *) tkwin;
    TkWindow       *winPtr2;
    Tcl_HashEntry  *hPtr;
    int             notUsed;
    TkDisplay      *dispPtr;
#ifdef _WIN32
    HWND            hParent;
    int             style;
    DWORD           exStyle;
    HWND            hWnd;
#else
    long int        mask;
#endif /* _WIN32 */

    if (winPtr->window != None) {
        return;                 /* Window already exists. */
    }
#ifdef notdef
    if ((winPtr->parentPtr == NULL) || (winPtr->flags & TK_TOP_LEVEL)) {
        parent = XRootWindow(winPtr->display, winPtr->screenNum);
        /* TODO: Make the entire screen busy */
    } else {
        if (Tk_WindowId(winPtr->parentPtr) == None) {
            Tk_MakeWindowExist((Tk_Window) winPtr->parentPtr);
        }
    }
#endif

    /* Create a transparent window and put it on top.  */

#ifdef _WIN32
    hParent = (HWND) parent;
    style = (WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
    exStyle = (WS_EX_TRANSPARENT | WS_EX_TOPMOST);
#ifndef TK_WIN_CHILD_CLASS_NAME /* from tkWinInt.h */
#define TK_WIN_CHILD_CLASS_NAME "TkChild"
#endif
    hWnd = CreateWindowEx(exStyle, TK_WIN_CHILD_CLASS_NAME, NULL, style,
        Tk_X(tkwin), Tk_Y(tkwin), Tk_Width(tkwin), Tk_Height(tkwin),
        hParent, NULL, (HINSTANCE) Tk_GetHINSTANCE(), NULL);
    winPtr->window = Tk_AttachHWND(tkwin, hWnd);
#else
    mask = (!isBusy) ? 0 : (CWDontPropagate | CWEventMask);
    /* Ignore the important events while the window is mapped.  */
#define USER_EVENTS  (EnterWindowMask | LeaveWindowMask | KeyPressMask | \
	KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask)
#define PROP_EVENTS  (KeyPressMask | KeyReleaseMask | ButtonPressMask | \
	ButtonReleaseMask | PointerMotionMask)

    winPtr->atts.do_not_propagate_mask = PROP_EVENTS;
    winPtr->atts.event_mask = USER_EVENTS;
    winPtr->changes.border_width = 0;
    winPtr->depth = 0;

    winPtr->window = XCreateWindow(winPtr->display, parent, winPtr->changes.x, winPtr->changes.y, (unsigned) winPtr->changes.width,     /* width */
        (unsigned) winPtr->changes.height,      /* height */
        (unsigned) winPtr->changes.border_width,        /* border_width */
        winPtr->depth,          /* depth */
        InputOnly,              /* class */
        winPtr->visual,         /* visual */
        mask,                   /* valuemask */
        &(winPtr->atts) /* attributes */ );
#endif /* _WIN32 */

    dispPtr = winPtr->dispPtr;
    hPtr = Tcl_CreateHashEntry(&(dispPtr->winTable), (char *) winPtr->window,
        &notUsed);
    Tcl_SetHashValue(hPtr, winPtr);
    winPtr->dirtyAtts = 0;
    winPtr->dirtyChanges = 0;
#ifdef TK_USE_INPUT_METHODS
    winPtr->inputContext = NULL;
#endif /* TK_USE_INPUT_METHODS */
    if (!(winPtr->flags & TK_TOP_LEVEL)) {
        /*
         * If any siblings higher up in the stacking order have already
         * been created then move this window to its rightful position
         * in the stacking order.
         *
         * NOTE: this code ignores any changes anyone might have made
         * to the sibling and stack_mode field of the window's attributes,
         * so it really isn't safe for these to be manipulated except
         * by calling Tk_RestackWindow.
         */
        for (winPtr2 = winPtr->nextPtr; winPtr2 != NULL;
            winPtr2 = winPtr2->nextPtr) {
            if ((winPtr2->window != None) && !(winPtr2->flags & TK_TOP_LEVEL)) {
                XWindowChanges  changes;
                changes.sibling = winPtr2->window;
                changes.stack_mode = Below;
                XConfigureWindow(winPtr->display, winPtr->window,
                    CWSibling | CWStackMode, &changes);
                break;
            }
        }
    }

    /*
     * Issue a ConfigureNotify event if there were deferred configuration
     * changes (but skip it if the window is being deleted;  the
     * ConfigureNotify event could cause problems if we're being called
     * from Tk_DestroyWindow under some conditions).
     */
    if ((winPtr->flags & TK_NEED_CONFIG_NOTIFY)
        && !(winPtr->flags & TK_ALREADY_DEAD)) {
        winPtr->flags &= ~TK_NEED_CONFIG_NOTIFY;
        DoConfigureNotify((Tk_FakeWin *) tkwin);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcFindChild --
 *
 *      Performs a linear search for the named child window in a given
 *      parent window.
 *
 *      This can be done via Tcl, but not through Tk's C API.  It's
 *      simple enough, if you peek into the Tk_Window structure.
 *
 * Results:
 *      The child Tk_Window. If the named child can't be found, NULL
 *      is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Tk_Window
RbcFindChild(
    Tk_Window parent,
    char *name)
{
    register TkWindow *winPtr;
    TkWindow       *parentPtr = (TkWindow *) parent;

    for (winPtr = parentPtr->childList; winPtr != NULL;
        winPtr = winPtr->nextPtr) {
        if (strcmp(name, winPtr->nameUid) == 0) {
            return (Tk_Window) winPtr;
        }
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcFirstChildWindow --
 *
 *      Performs a linear search for the named child window in a given
 *      parent window.
 *
 *      This can be done via Tcl, but not through Tk's C API.  It's
 *      simple enough, if you peek into the Tk_Window structure.
 *
 * Results:
 *      The child Tk_Window. If the named child can't be found, NULL
 *      is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Tk_Window
RbcFirstChild(
    Tk_Window parent)
{
    TkWindow       *parentPtr = (TkWindow *) parent;
    return (Tk_Window) parentPtr->childList;
}

/*
 *--------------------------------------------------------------
 *
 * RbcNextChild --
 *
 *      TODO: Description
 *
 * Results:
 *      The child Tk_Window. If the named child can't be found, NULL
 *      is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Tk_Window
RbcNextChild(
    Tk_Window tkwin)
{
    TkWindow       *winPtr = (TkWindow *) tkwin;

    if (winPtr == NULL) {
        return NULL;
    }
    return (Tk_Window) winPtr->nextPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * UnlinkWindow --
 *
 *      This procedure removes a window from the childList of its
 *      parent.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The window is unlinked from its childList.
 *
 *----------------------------------------------------------------------
 */
static void
UnlinkWindow(
    TkWindow * winPtr)
{                               /* Child window to be unlinked. */
    TkWindow       *prevPtr;

    prevPtr = winPtr->parentPtr->childList;
    if (prevPtr == winPtr) {
        winPtr->parentPtr->childList = winPtr->nextPtr;
        if (winPtr->nextPtr == NULL) {
            winPtr->parentPtr->lastChildPtr = NULL;
        }
    } else {
        while (prevPtr->nextPtr != winPtr) {
            prevPtr = prevPtr->nextPtr;
            if (prevPtr == NULL) {
                Tcl_Panic("UnlinkWindow couldn't find child in parent");
            }
        }
        prevPtr->nextPtr = winPtr->nextPtr;
        if (winPtr->nextPtr == NULL) {
            winPtr->parentPtr->lastChildPtr = prevPtr;
        }
    }
}

/*
 *--------------------------------------------------------------
 *
 * RbcRootCoordinates --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcRootCoordinates(
    Tk_Window tkwin,
    int x,
    int y,
    int *rootXPtr,
    int *rootYPtr)
{
    int             vx, vy, vw, vh;
    int             rootX, rootY;

    Tk_GetRootCoords(tkwin, &rootX, &rootY);
    x += rootX;
    y += rootY;
    Tk_GetVRootGeometry(tkwin, &vx, &vy, &vw, &vh);
    x += vx;
    y += vy;
    *rootXPtr = x;
    *rootYPtr = y;
}

/*
 *--------------------------------------------------------------
 *
 * RbcRootX --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int
RbcRootX(
    Tk_Window tkwin)
{
    int             x;

    for (x = 0; tkwin != NULL; tkwin = Tk_Parent(tkwin)) {
        x += Tk_X(tkwin) + Tk_Changes(tkwin)->border_width;
        if (Tk_IsTopLevel(tkwin)) {
            break;
        }
    }
    return x;
}

/*
 *--------------------------------------------------------------
 *
 * RbcRootY --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int
RbcRootY(
    Tk_Window tkwin)
{
    int             y;

    for (y = 0; tkwin != NULL; tkwin = Tk_Parent(tkwin)) {
        y += Tk_Y(tkwin) + Tk_Changes(tkwin)->border_width;
        if (Tk_IsTopLevel(tkwin)) {
            break;
        }
    }
    return y;
}

#ifdef _WIN32

/*
 *----------------------------------------------------------------------
 *
 * RbcGetRealWindowId --
 *
 *      Returns the XID for the Tk_Window given.  Starting in Tk 8.0,
 *      the toplevel widgets are wrapped by another window.
 *      Currently there's no way to get at that window, other than
 *      what is done here: query the X window hierarchy and grab the
 *      parent.
 *
 * Results:
 *      Returns the X Window ID of the widget.  If it's a toplevel,
 *      then the XID of the wrapper is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Window
RbcGetRealWindowId(
    Tk_Window tkwin)
{
    return (Window) GetWindowHandle(tkwin);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcGetToplevel --
 *
 *      Retrieves the toplevel window which is the nearest ancestor of
 *      of the specified window.
 *
 * Results:
 *      Returns the toplevel window or NULL if the window has no
 *      ancestor which is a toplevel.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
Tk_Window
RbcGetToplevel(
    Tk_Window tkwin)
{                               /* Window for which the toplevel
                                 * should be deterined. */
    while (!Tk_IsTopLevel(tkwin)) {
        tkwin = Tk_Parent(tkwin);
        if (tkwin == NULL) {
            return NULL;
        }
    }
    return tkwin;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcRaiseToLevelWindow --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcRaiseToplevel(
    Tk_Window tkwin)
{
    SetWindowPos(GetWindowHandle(tkwin), HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcMapToplevel --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcMapToplevel(
    Tk_Window tkwin)
{
    ShowWindow(GetWindowHandle(tkwin), SW_SHOWNORMAL);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcUnmapToplevel --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcUnmapToplevel(
    Tk_Window tkwin)
{
    ShowWindow(GetWindowHandle(tkwin), SW_HIDE);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcMoveResizeToplevel --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcMoveResizeToplevel(
    Tk_Window tkwin,
    int x,
    int y,
    int width,
    int height)
{
    SetWindowPos(GetWindowHandle(tkwin), HWND_TOP, x, y, width, height, 0);
}

/*
 *--------------------------------------------------------------
 *
 * RbcReparentWindow --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int
RbcReparentWindow(
    Display * display,
    Window window,
    Window newParent,
    int x,
    int y)
{
    XReparentWindow(display, window, newParent, x, y);
    return TCL_OK;
}

#else /* _WIN32 */

/*
 *----------------------------------------------------------------------
 *
 * RbcGetRealWindowId --
 *
 *      Returns the XID for the Tk_Window given.  Starting in Tk 8.0,
 *      the toplevel widgets are wrapped by another window.
 *      Currently there's no way to get at that window, other than
 *      what is done here: query the X window hierarchy and grab the
 *      parent.
 *
 * Results:
 *      Returns the X Window ID of the widget.  If it's a toplevel, then
 *      the XID of the wrapper is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Window
RbcGetRealWindowId(
    Tk_Window tkwin)
{
    return GetWindowId(tkwin);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcRaiseToplevel --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcRaiseToplevel(
    Tk_Window tkwin)
{
    XRaiseWindow(Tk_Display(tkwin), GetWindowId(tkwin));
}

/*
 *----------------------------------------------------------------------
 *
 * RbcLowerToplevel --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcLowerToplevel(
    Tk_Window tkwin)
{
    XLowerWindow(Tk_Display(tkwin), GetWindowId(tkwin));
}

/*
 *----------------------------------------------------------------------
 *
 * RbcResizeToplevel --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcResizeToplevel(
    Tk_Window tkwin,
    int width,
    int height)
{
    XResizeWindow(Tk_Display(tkwin), GetWindowId(tkwin), width, height);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcMoveResizeToplevel --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcMoveResizeToplevel(
    Tk_Window tkwin,
    int x,
    int y,
    int width,
    int height)
{
    XMoveResizeWindow(Tk_Display(tkwin), GetWindowId(tkwin), x, y,
        width, height);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcMoveToplevel --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcMoveToplevel(
    Tk_Window tkwin,
    int x,
    int y)
{
    XMoveWindow(Tk_Display(tkwin), GetWindowId(tkwin), x, y);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcMapToplevel --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcMapToplevel(
    Tk_Window tkwin)
{
    XMapWindow(Tk_Display(tkwin), GetWindowId(tkwin));
}

/*
 *----------------------------------------------------------------------
 *
 * RbcUnmapToplevel --
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcUnmapToplevel(
    Tk_Window tkwin)
{
    XUnmapWindow(Tk_Display(tkwin), GetWindowId(tkwin));
}

/*
 *----------------------------------------------------------------------
 *
 * XReparentWindowErrorProc --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
XReparentWindowErrorProc(
    ClientData clientData,
    XErrorEvent * errEventPtr)
{
    int            *errorPtr = clientData;

    *errorPtr = TCL_ERROR;
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcReparentWindow --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int
RbcReparentWindow(
    Display * display,
    Window window,
    Window newParent,
    int x,
    int y)
{
    Tk_ErrorHandler handler;
    int             result;
    int             any = -1;

    result = TCL_OK;
    handler = Tk_CreateErrorHandler(display, any, X_ReparentWindow, any,
        XReparentWindowErrorProc, &result);
    XReparentWindow(display, window, newParent, x, y);
    Tk_DeleteErrorHandler(handler);
    XSync(display, False);
    return result;
}

#endif /* _WIN32 */

/*
 *--------------------------------------------------------------
 *
 * RbcSetWindowInstanceData --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcSetWindowInstanceData(
    Tk_Window tkwin,
    ClientData instanceData)
{
    TkWindow       *winPtr = (TkWindow *) tkwin;

    winPtr->instanceData = instanceData;
}

/*
 *--------------------------------------------------------------
 *
 * RbcGetWindowInstanceData --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
ClientData
RbcGetWindowInstanceData(
    Tk_Window tkwin)
{
    TkWindow       *winPtr = (TkWindow *) tkwin;

    return winPtr->instanceData;
}

/*
 *--------------------------------------------------------------
 *
 * RbcDeleteWindowInstanceData --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcDeleteWindowInstanceData(
    Tk_Window tkwin)
{
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_SnapWindow --
 *
 *      Snaps a picture of a window and stores it in a
 *      designated photo image.  The window must be completely
 *      visible or the snap will fail.
 *
 * Results:
 *      Returns a standard Tcl result.  interp->result contains
 *      the list of the graph coordinates. If an error occurred
 *      while parsing the window positions, TCL_ERROR is
 *      returned, then interp->result will contain an error
 *      message.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int
Rbc_SnapWindow(
    Tcl_Interp * interp,        /* Current interpreter. */
    Tk_Window tkmain,           /* Window of topelevel window */
    const char *pathName,       /* Window path of window to snap */
    const char *photoImage,     /* Name of exisiting photo image to save to */
    int destWidth,              /* used if >0 */
    int destHeight)
{                               /* used if >0 */
    Tk_Window       tkwin;
    int             width, height;
    Window          window;

    tkwin = Tk_NameToWindow(interp, pathName, tkmain);
    if (tkwin == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("window \"%s\" not found",
                pathName));
        return TCL_ERROR;
    }
    if (Tk_WindowId(tkwin) == None) {
        Tk_MakeWindowExist(tkwin);
    }

    if (Tk_IsTopLevel(tkwin)) {
        Tcl_SetObjResult(interp,
            Tcl_ObjPrintf("window \"%s\" is not supported toplvel", pathName));
        return TCL_ERROR;

/*TODO        window = RbcGetRealWindowId(tkwin);*/
    } else {
        window = Tk_WindowId(tkwin);
    }

    if (GetWindowSize(interp, window, &width, &height) != TCL_OK) {
        Tcl_SetObjResult(interp,
            Tcl_ObjPrintf("can't get window geometry of \"%s\"", pathName));
        return TCL_ERROR;
    }
    if (destWidth <= 0) {
        destWidth = width;
    }
    if (destHeight <= 0) {
        destHeight = height;
    }
    return RbcSnapPhoto(interp, tkwin, window, 0, 0, width, height, destWidth,
        destHeight, photoImage, 1.0);
}

#ifdef WIN32

/*
 *--------------------------------------------------------------
 *
 * GetWindowSize --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int
GetWindowSize(
    Tcl_Interp * interp,
    Window window,
    int *widthPtr,
    int *heightPtr)
{
    int             result;
    RECT            region;
    TkWinWindow    *winPtr = (TkWinWindow *) window;

    result = GetWindowRect(winPtr->handle, &region);
    if (result) {
        *widthPtr = region.right - region.left;
        *heightPtr = region.bottom - region.top;
        return TCL_OK;
    }
    return TCL_ERROR;
}
#else

/*
 *----------------------------------------------------------------------
 *
 * XGeometryErrorProc --
 *
 *	Flags errors generated from XGetGeometry calls to the X server.
 *
 * Results:
 *	Always returns 0.
 *
 * Side Effects:
 *	Sets a flag, indicating an error occurred.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
static int
XGeometryErrorProc(
    clientData,
    errEventPtr)
     ClientData      clientData;
     XErrorEvent    *errEventPtr;
{
    int            *errorPtr = clientData;

    *errorPtr = TCL_ERROR;
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * GetWindowSize --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int
GetWindowSize(
    Tcl_Interp * interp,
    Window window,
    int *widthPtr,
    int *heightPtr)
{
    int             result;
    int             any = -1;
    int             x, y, borderWidth, depth;
    Window          root;
    Tk_ErrorHandler handler;
    Tk_Window       tkwin;

    tkwin = Tk_MainWindow(interp);
    handler = Tk_CreateErrorHandler(Tk_Display(tkwin), any, X_GetGeometry,
        any, XGeometryErrorProc, &result);
    result = XGetGeometry(Tk_Display(tkwin), window, &root, &x, &y,
        (unsigned int *) widthPtr, (unsigned int *) heightPtr,
        (unsigned int *) &borderWidth, (unsigned int *) &depth);
    Tk_DeleteErrorHandler(handler);
    XSync(Tk_Display(tkwin), False);
    if (result) {
        return TCL_OK;
    }
    return TCL_ERROR;
}
#endif

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
