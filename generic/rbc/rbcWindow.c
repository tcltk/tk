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

static int      GetWindowSize(
    Tcl_Interp * interp,
    Window window,
    int *widthPtr,
    int *heightPtr);

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
