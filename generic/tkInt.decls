# tkInt.decls --
#
#	This file contains the declarations for all unsupported
#	functions that are exported by the Tk library.  This file
#	is used to generate the tkIntDecls.h, tkIntPlatDecls.h,
#	tkIntStub.c, and tkPlatStub.c files.
#
# Copyright (c) 1998-1999 by Scriptics Corporation.
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.
# 
# RCS: @(#) $Id: tkInt.decls,v 1.1.2.2 1999/03/06 01:54:58 redman Exp $

library tk

# Define the unsupported generic interfaces.

interface tkInt

# Declare each of the functions in the unsupported internal Tcl
# interface.  These interfaces are allowed to changed between versions.
# Use at your own risk.  Note that the position of functions should not
# be changed between versions to avoid gratuitous incompatibilities.

declare 0 generic {
    TkWindow * TkAllocWindow (TkDisplay *dispPtr, int screenNum, \
	    TkWindow *parentPtr)
}

declare 1 generic {
    void TkBezierPoints (double control[], int numSteps, double *coordPtr)
}

declare 2 generic {
    void TkBezierScreenPoints (Tk_Canvas canvas, double control[], \
	    int numSteps, XPoint *xPointPtr)
}

declare 3 generic {
    void TkBindDeadWindow (TkWindow *winPtr)
}

declare 4 generic {
    void TkBindEventProc (TkWindow *winPtr, XEvent *eventPtr)
}

declare 5 generic {
    void TkBindFree (TkMainInfo *mainPtr)
}

declare 6 generic {
    void TkBindInit (TkMainInfo *mainPtr)
}

declare 7 generic {
    void TkChangeEventWindow (XEvent *eventPtr, TkWindow *winPtr)
}

declare 8 generic {
    int TkClipInit (Tcl_Interp *interp, TkDisplay *dispPtr)
}

declare 9 generic {
    void TkComputeAnchor (Tk_Anchor anchor, Tk_Window tkwin, \
	    int padX, int padY, int innerWidth, int innerHeight, \
	    int *xPtr, int *yPtr)
}

declare 10 generic {
    int TkCopyAndGlobalEval (Tcl_Interp *interp, char *script)
}

declare 11 generic {
    unsigned long TkCreateBindingProcedure (Tcl_Interp *interp, \
	    Tk_BindingTable bindingTable, \
	    ClientData object, char *eventString, \
	    TkBindEvalProc *evalProc, TkBindFreeProc *freeProc, \
	    ClientData clientData)
}

declare 12 generic {
    TkCursor * TkCreateCursorFromData (Tk_Window tkwin, \
	    char *source, char *mask, int width, int height, \
	    int xHot, int yHot, XColor fg, XColor bg)
}

declare 13 generic {
    int TkCreateFrame (ClientData clientData, \
	    Tcl_Interp *interp, int argc, char **argv, \
	    int toplevel, char *appName)
}

declare 14 generic {
    Tk_Window TkCreateMainWindow (Tcl_Interp *interp, \
	    char *screenName, char *baseName)
}

declare 15 generic {
    Time TkCurrentTime (TkDisplay *dispPtr)
}

declare 16 generic {
    void TkDeleteAllImages (TkMainInfo *mainPtr)
}

declare 17 generic {
    void TkDoConfigureNotify (TkWindow *winPtr)
}

declare 18 generic {
    void TkDrawInsetFocusHighlight (Tk_Window tkwin, GC gc, int width, \
	    Drawable drawable, int padding)
}

declare 19 generic {
    void TkEventDeadWindow (TkWindow *winPtr)
}

declare 20 generic {
    void TkFillPolygon (Tk_Canvas canvas, \
	    double *coordPtr, int numPoints, Display *display, \
	    Drawable drawable, GC gc, GC outlineGC)
}

declare 21 generic {
    int TkFindStateNum (Tcl_Interp *interp, \
	    CONST char *option, CONST TkStateMap *mapPtr, \
	    CONST char *strKey)
}

declare 22 generic {
    char * TkFindStateString (CONST TkStateMap *mapPtr, int numKey)
}

declare 23 generic {
    void TkFocusDeadWindow (TkWindow *winPtr)
}

declare 24 generic {
    int TkFocusFilterEvent (TkWindow *winPtr, XEvent *eventPtr)
}

declare 25 generic {
    TkWindow * TkFocusKeyEvent (TkWindow *winPtr, XEvent *eventPtr)
}

declare 26 generic {
    void TkFontPkgInit (TkMainInfo *mainPtr)
}

declare 27 generic {
    void TkFontPkgFree (TkMainInfo *mainPtr)
}

declare 28 generic {
    void TkFreeBindingTags (TkWindow *winPtr)
}

declare 29 generic {
    void TkFreeCursor (TkCursor *cursorPtr)
}

declare 30 generic {
    char * TkGetBitmapData (Tcl_Interp *interp, \
	    char *string, char *fileName, int *widthPtr, \
	    int *heightPtr, int *hotXPtr, int *hotYPtr)
}

declare 31 generic {
    void TkGetButtPoints (double p1[], double p2[], \
	    double width, int project, double m1[], double m2[])
}

declare 32 generic {
    TkCursor * TkGetCursorByName (Tcl_Interp *interp, \
	    Tk_Window tkwin, Tk_Uid string)
}

declare 33 generic {
    char * TkGetDefaultScreenName (Tcl_Interp *interp, char *screenName)
}

declare 34 generic {
    TkDisplay * TkGetDisplay (Display *display)
}

declare 35 generic {
    int TkGetDisplayOf (Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[], \
	    Tk_Window *tkwinPtr)
}

declare 36 generic {
    TkWindow * TkGetFocusWin (TkWindow *winPtr)
}

declare 37 generic {
    int TkGetInterpNames (Tcl_Interp *interp, Tk_Window tkwin)
}

declare 38 generic {
    int TkGetMiterPoints (double p1[], double p2[], double p3[], \
	    double width, double m1[],double m2[])
}

declare 39 generic {
    void TkGetPointerCoords (Tk_Window tkwin, int *xPtr, int *yPtr)
}

declare 40 generic {
    void TkGetServerInfo (Tcl_Interp *interp, Tk_Window tkwin)
}

declare 41 generic {
    void TkGrabDeadWindow (TkWindow *winPtr)
}

declare 42 generic {
    int TkGrabState (TkWindow *winPtr)
}

declare 43 generic {
    void TkIncludePoint (Tk_Item *itemPtr, double *pointPtr)
}

declare 44 generic {
    void TkInOutEvents (XEvent *eventPtr, TkWindow *sourcePtr, \
	    TkWindow *destPtr, int leaveType, int enterType, \
	    Tcl_QueuePosition position)
}

declare 45 generic {
    void TkInstallFrameMenu (Tk_Window tkwin)
}

declare 46 generic {
    char * TkKeysymToString (KeySym keysym)
}

declare 47 generic {
    int TkLineToArea (TkDouble2 end1Ptr, TkDouble2 end2Ptr, TkDouble4 rectPtr)
}

declare 48 generic {
    double TkLineToPoint (double end1Ptr[], \
	    TkDouble2 end2Ptr, TkDouble2 pointPtr)
}

declare 49 generic {
    int TkMakeBezierCurve (Tk_Canvas canvas, \
	    double *pointPtr, int numPoints, int numSteps, \
	    XPoint xPoints[], double dblPoints[])
}

declare 50 generic {
    void TkMakeBezierPostscript (Tcl_Interp *interp, \
	    Tk_Canvas canvas, double *pointPtr, int numPoints)
}

declare 51 generic {
    void TkOptionClassChanged (TkWindow *winPtr)
}

declare 52 generic {
    void TkOptionDeadWindow (TkWindow *winPtr)
}

declare 53 generic {
    int TkOvalToArea (double *ovalPtr, double *rectPtr)
}

declare 54 generic {
    double TkOvalToPoint (TkDouble4 ovalPtr, \
	    double width, int filled, TkDouble2 pointPtr)
}

declare 55 generic {
    int TkpChangeFocus (TkWindow *winPtr, int force)
}

declare 56 generic {
    void TkpCloseDisplay (TkDisplay *dispPtr)
}

declare 57 generic {
    void TkpClaimFocus (TkWindow *topLevelPtr, int force)
}

declare 58 generic {
    void TkpDisplayWarning (char *msg, char *title)
}

declare 59 generic {
    void TkpGetAppName (Tcl_Interp *interp, Tcl_DString *name)
}

declare 60 generic {
    TkWindow * TkpGetOtherWindow (TkWindow *winPtr)
}

declare 61 generic {
    TkWindow * TkpGetWrapperWindow (TkWindow *winPtr)
}

declare 62 generic {
    int TkpInit (Tcl_Interp *interp)
}

declare 63 generic {
    void TkpInitializeMenuBindings (Tcl_Interp *interp, \
	    Tk_BindingTable bindingTable)
}

declare 64 generic {
    void TkpMakeContainer (Tk_Window tkwin)
}

declare 65 generic {
    void TkpMakeMenuWindow (Tk_Window tkwin, int transient)
}

declare 66 generic {
    Window TkpMakeWindow (TkWindow *winPtr, Window parent)
}

declare 67 generic {
    void TkpMenuNotifyToplevelCreate (Tcl_Interp *interp1, char *menuName)
}

declare 68 generic {
    TkDisplay * TkpOpenDisplay (char *display_name)
}

declare 69 generic {
    int TkPointerEvent (XEvent *eventPtr, TkWindow *winPtr)
}

declare 70 generic {
    int TkPolygonToArea (double *polyPtr, int numPoints, double *rectPtr)
}

declare 71 generic {
    double TkPolygonToPoint (double *polyPtr, int numPoints, double *pointPtr)
}

declare 72 generic {
    int TkPositionInTree (TkWindow *winPtr, TkWindow *treePtr)
}

declare 73 generic {
    void TkpRedirectKeyEvent (TkWindow *winPtr, XEvent *eventPtr)
}

declare 74 generic {
    void TkpSetMainMenubar (Tcl_Interp *interp, \
	    Tk_Window tkwin, char *menuName)
}

declare 75 generic {
    int TkpUseWindow (Tcl_Interp *interp, Tk_Window tkwin, char *string)
}

declare 76 generic {
    int TkpWindowWasRecentlyDeleted (Window win, TkDisplay *dispPtr)
}

declare 77 generic {
    void TkQueueEventForAllChildren (TkWindow *winPtr, XEvent *eventPtr)
}

declare 78 generic {
    int TkReadBitmapFile (Display* display, Drawable d, CONST char* filename, \
	    unsigned int* width_return, unsigned int* height_return, \
	    Pixmap* bitmap_return, int* x_hot_return, int* y_hot_return)
}

declare 79 generic {
    int TkScrollWindow (Tk_Window tkwin, GC gc, \
	    int x, int y, int width, int height, int dx, \
	    int dy, TkRegion damageRgn)
}

declare 80 generic {
    void TkSelDeadWindow (TkWindow *winPtr)
}

declare 81 generic {
    void TkSelEventProc (Tk_Window tkwin, XEvent *eventPtr)
}

declare 82 generic {
    void TkSelInit (Tk_Window tkwin)
}

declare 83 generic {
    void TkSelPropProc (XEvent *eventPtr)
}

declare 84 generic {
    void TkSetClassProcs (Tk_Window tkwin, \
	    TkClassProcs *procs, ClientData instanceData)
}

declare 85 generic {
    void TkSetWindowMenuBar (Tcl_Interp *interp, \
	    Tk_Window tkwin, char *oldMenuName, char *menuName)
}

declare 86 generic {
    KeySym TkStringToKeysym (char *name)
}

declare 87 generic {
    int TkThickPolyLineToArea (double *coordPtr, \
	    int numPoints, double width, int capStyle, \
	    int joinStyle, double *rectPtr)
}

declare 88 generic {
    void TkWmAddToColormapWindows (TkWindow *winPtr)
}

declare 89 generic {
    void TkWmDeadWindow (TkWindow *winPtr)
}

declare 90 generic {
    TkWindow * TkWmFocusToplevel (TkWindow *winPtr)
}

declare 91 generic {
    void TkWmMapWindow (TkWindow *winPtr)
}

declare 92 generic {
    void TkWmNewWindow (TkWindow *winPtr)
}

declare 93 generic {
    void TkWmProtocolEventProc (TkWindow *winPtr, XEvent *evenvPtr)
}

declare 94 generic {
    void TkWmRemoveFromColormapWindows (TkWindow *winPtr)
}

declare 95 generic {
    void TkWmRestackToplevel (TkWindow *winPtr, int aboveBelow, \
	    TkWindow *otherPtr)
}

declare 96 generic {
    void TkWmSetClass (TkWindow *winPtr)
}

declare 97 generic {
    void TkWmUnmapWindow (TkWindow *winPtr)
}


##############################################################################

# Define the platform specific internal Tcl interface. These functions are
# only available on the designated platform.

interface tkIntPlat

#########################
# Unix specific functions

declare 0 unix {
    void TkCreateXEventSource (void)
}

declare 1 unix {
    void TkFreeWindowId (TkDisplay *dispPtr, Window w)
}

declare 2 unix {
    void TkInitXId (TkDisplay *dispPtr)
}

declare 3 unix {
    int TkpCmapStressed (Tk_Window tkwin, Colormap colormap)}


declare 4 unix {
    void TkpSync (Display *display)
}

declare 5 unix {
    Window TkUnixContainerId (TkWindow *winPtr)
}

declare 6 unix {
    int TkUnixDoOneXEvent (Tcl_Time *timePtr)
}

declare 7 unix {
    void TkUnixSetMenubar (Tk_Window tkwin, Tk_Window menubar)
}
	


############################
# Windows specific functions

declare 0 win {
    char * TkAlignImageData (XImage *image, int alignment, int bitOrder)
}

declare 1 win {
    void TkClipBox (TkRegion rgn, XRectangle* rect_return)
}

declare 2 win {
    TkRegion TkCreateRegion (void)
}

declare 3 win {
    void TkDestroyRegion (TkRegion rgn)
}

declare 4 win {
    void TkGenerateActivateEvents (TkWindow *winPtr, int active)
}

declare 5 win {
    void TkIntersectRegion (TkRegion sra, TkRegion srcb, TkRegion dr_return)
}

declare 6 win {
    unsigned long TkpGetMS (void)
}

declare 7 win {
    void TkPointerDeadWindow (TkWindow *winPtr)
}

declare 8 win {
    void TkpPrintWindowId (char *buf, Window window)
}

declare 9 win {
    int TkpScanWindowId (Tcl_Interp *interp, char *string, int *idPtr)
}

declare 11 win {
    void TkpSetCapture (TkWindow *winPtr)
}

declare 12 win {
    void TkpSetCursor (TkpCursor cursor)
}

declare 13 win {
    void TkpWmSetState (TkWindow *winPtr, int state)
}

declare 14 win {
    int TkRectInRegion (TkRegion rgn, int x, int y, unsigned int width, \
	    unsigned int height)
}

declare 15 win {
    void TkSetPixmapColormap (Pixmap pixmap, Colormap colormap)
}

declare 16 win {
    void TkSetRegion (Display* display, GC gc, TkRegion rgn)
}

declare 17 win {
    void TkUnionRectWithRegion (XRectangle* rect, \
	    TkRegion src, TkRegion dr_return)
}

declare 18 win {
    void  TkWinCancelMouseTimer (void)
}

declare 20 win {
    void  TkWinClipboardRender (TkDisplay *dispPtr, UINT format)
}

declare 21 win {
    LRESULT  TkWinEmbeddedEventProc (HWND hwnd, UINT message, \
	    WPARAM wParam, LPARAM lParam)
}

declare 22 win {
    void  TkWinFillRect (HDC dc, int x, int y, int width, int height, \
	    int pixel)
}

declare 23 win {
    COLORREF  TkWinGetBorderPixels (Tk_Window tkwin, Tk_3DBorder border, \
	    int which)
}

declare 24 win {
    HDC  TkWinGetDrawableDC (Display *display, Drawable d, TkWinDCState* state)
}

declare 25 win {
    int  TkWinGetModifierState (void)
}

declare 26 win {
    HPALETTE  TkWinGetSystemPalette (void)
}

declare 27 win {
    HWND  TkWinGetWrapperWindow (Tk_Window tkwin)
}

declare 28 win {
    int  TkWinHandleMenuEvent (HWND *phwnd, \
	    UINT *pMessage, WPARAM *pwParam, LPARAM *plParam, \
	    LRESULT *plResult)
}

declare 29 win {
    int  TkWinIndexOfColor (XColor *colorPtr)
}

declare 30 win {
    void  TkWinPointerDeadWindow (TkWindow *winPtr)
}

declare 31 win {
    void  TkWinPointerEvent (HWND hwnd, int x, int y)
}

declare 32 win {
    void  TkWinPointerInit (void)
}

declare 33 win {
    LRESULT   TkWinReflectMessage (HWND hwnd, UINT message, \
	    WPARAM wParam, LPARAM lParam)
}

declare 34 win {
    void  TkWinReleaseDrawableDC (Drawable d, HDC hdc, TkWinDCState* state)
}

declare 35 win {
    LRESULT  TkWinResendEvent (WNDPROC wndproc, HWND hwnd, XEvent *eventPtr)
}

declare 36 win {
    HPALETTE  TkWinSelectPalette (HDC dc, Colormap colormap)
}

declare 37 win {
    void  TkWinSetMenu (Tk_Window tkwin, HMENU hMenu)
}

declare 38 win {
    void  TkWinSetWindowPos (HWND hwnd, HWND siblingHwnd, int pos)
}

declare 39 win {
    void  TkWinUpdateCursor (TkWindow *winPtr)
}

declare 40 win {
    void  TkWinWmCleanup (HINSTANCE hInstance)
}

declare 41 win {
    HWND  TkWinWmFindEmbedAssociation (TkWindow *winPtr)
}

declare 42 win {
    void  TkWinWmStoreEmbedAssociation (TkWindow *winPtr, HWND hwnd)
}

declare 43 win {
    void  TkWinXCleanup (HINSTANCE hInstance)
}

declare 44 win {
    void   TkWinXInit (HINSTANCE hInstance)
}


########################
# Mac specific functions

declare 0 mac {
    void TkClipBox (TkRegion rgn, XRectangle* rect_return)
}

declare 1 mac {
    TkRegion TkCreateRegion (void)
}

declare 2 mac {
    void TkDestroyRegion (TkRegion rgn)
}

declare 3 mac {
    void TkGenerateActivateEvents (TkWindow *winPtr, int active)
}

declare 4 mac {
    void TkIntersectRegion (TkRegion sra, TkRegion srcb, TkRegion dr_return)
}

declare 5 mac {
    Pixmap TkpCreateNativeBitmap (Display *display, char * source)
}

declare 6 mac {
    void TkpDefineNativeBitmaps (void)
}

declare 7 mac {
    unsigned long TkpGetMS (void)
}

declare 8 mac {
    Pixmap TkpGetNativeAppBitmap (Display *display, \
	    char *name, int *width, int *height)
}

declare 9 mac {
    void TkPointerDeadWindow (TkWindow *winPtr)
}

declare 10 mac {
    void TkpSetCapture (TkWindow *winPtr)
}

declare 11 mac {
    void TkpSetCursor (TkpCursor cursor)
}

declare 13 mac {
    void TkpWmSetState (TkWindow *winPtr, int state)
}

declare 14 mac {
    int TkRectInRegion (TkRegion rgn, int x, int y, unsigned int width, \
	    unsigned int height)
}

declare 15 mac {
    void TkSetRegion (Display* display, GC gc, TkRegion rgn)
}

declare 16 mac {
    void TkUnionRectWithRegion (XRectangle* rect, \
	    TkRegion src, TkRegion dr_return)
}

declare 17 mac {
    int  HandleWMEvent (EventRecord *theEvent)
}

declare 18 mac {
    void   TkAboutDlg (void)
}

declare 19 mac {
    void  TkCreateMacEventSource (void)
}

declare 20 mac {
    void   TkFontList (Tcl_Interp *interp, Display *display)
}

declare 21 mac {
    Window  TkGetTransientMaster (TkWindow *winPtr)
}

declare 22 mac {
    int  TkGenerateButtonEvent (int x, int y, \
	    Window window, unsigned int state)
}

declare 23 mac {
    int   TkGetCharPositions (XFontStruct *font_struct, char *string, \
	    int count, short *buffer)
}

declare 24 mac {
    void  TkGenWMDestroyEvent (Tk_Window tkwin)
}

declare 25 mac {
    void  TkGenWMConfigureEvent (Tk_Window tkwin, int x, int y, \
	    int width, int height, int flags)
}

declare 26 mac {
    unsigned int TkMacButtonKeyState (void)
}

declare 27 mac {
    void  TkMacClearMenubarActive (void)
}

declare 28 mac {
    int  TkMacConvertEvent (EventRecord *eventPtr)
}

declare 29 mac {
    int  TkMacDispatchMenuEvent (int menuID, int index)
}

declare 30 mac {
    void  TkMacInstallCursor (int resizeOverride)
}

declare 31 mac {
    int  TkMacConvertTkEvent (EventRecord *eventPtr, Window window)
}

declare 32 mac {
    void  TkMacHandleTearoffMenu (void)
}

declare 33 mac {
    void  tkMacInstallMWConsole (Tcl_Interp *interp)
}

declare 34 mac {
    void  TkMacInvalClipRgns (TkWindow *winPtr)
}

declare 35 mac {
    void  TkMacDoHLEvent (EventRecord *theEvent)
}

declare 36 mac {
    void   TkMacFontInfo (Font fontId, short *family, \
	    short *style, short *size)
}

declare 37 mac {
    Time  TkMacGenerateTime (void)
}

declare 38 mac {
    GWorldPtr  TkMacGetDrawablePort (Drawable drawable)
}

declare 39 mac {
    TkWindow *  TkMacGetScrollbarGrowWindow (TkWindow *winPtr)
}

declare 40 mac {
    Window   TkMacGetXWindow (WindowRef macWinPtr)
}

declare 41 mac {
    int  TkMacGrowToplevel (WindowRef whichWindow, Point start)
}

declare 42 mac {
    void   TkMacHandleMenuSelect (long mResult, int optionKeyPressed)
}

declare 43 mac {
    int  TkMacHaveAppearance (void)
}

declare 44 mac {
    void  TkMacInitAppleEvents (Tcl_Interp *interp)
}

declare 45 mac {
    void   TkMacInitMenus (Tcl_Interp  *interp)
}

declare 46 mac {
    void  TkMacInvalidateWindow (MacDrawable *macWin, int flag)
}

declare 47 mac {
    int  TkMacIsCharacterMissing (Tk_Font tkfont, unsigned int searchChar)
}

declare 48 mac {
    void  TkMacMakeRealWindowExist (TkWindow *winPtr)
}

declare 49 mac {
    BitMapPtr TkMacMakeStippleMap(Drawable d1, Drawable d2)
}

declare 50 mac {
    void  TkMacMenuClick (void)
}

declare 51 mac {
    void  TkMacRegisterOffScreenWindow (Window window, GWorldPtr portPtr)
}

declare 52 mac {
    int  TkMacResizable (TkWindow *winPtr)
}

declare 53 mac {
    void  TkMacSetEmbedRgn (TkWindow *winPtr, RgnHandle rgn)
}

declare 54 mac {
    void  TkMacSetHelpMenuItemCount (void)
}

declare 55 mac {
    void  TkMacSetScrollbarGrow (TkWindow *winPtr, int flag)
}

declare 56 mac {
    void  TkMacSetUpClippingRgn (Drawable drawable)
}

declare 57 mac {
    void  TkMacSetUpGraphicsPort (GC gc)
}

declare 58 mac {
    void   TkMacUpdateClipRgn (TkWindow *winPtr)
}

declare 59 mac {
    void  TkMacUnregisterMacWindow (GWorldPtr portPtr)
}

declare 60 mac {
    int  TkMacUseMenuID (short macID)
}

declare 61 mac {
    RgnHandle  TkMacVisableClipRgn (TkWindow *winPtr)
}

declare 62 mac {
    void  TkMacWinBounds (TkWindow *winPtr, Rect *geometry)
}

declare 63 mac {
    void  TkMacWindowOffset (WindowRef wRef, int *xOffset, int *yOffset)
}

declare 64 mac {
    void  TkResumeClipboard (void)
}

declare 65 mac {
    int   TkSetMacColor (unsigned long pixel, RGBColor *macColor)
}

declare 66 mac {
    void   TkSetWMName (TkWindow *winPtr, Tk_Uid titleUid)
}

declare 67 mac {
    void  TkSuspendClipboard (void)
}

declare 68 mac {
    int  TkWMGrowToplevel (WindowRef whichWindow, Point start)
}

declare 69 mac {
    int  TkMacZoomToplevel (WindowPtr whichWindow, Point where, short zoomPart)
}

declare 70 mac {
    Tk_Window Tk_TopCoordsToWindow (Tk_Window tkwin, \
	    int rootX, int rootY, int *newX, int *newY)
}

declare 71 mac {
    MacDrawable * TkMacContainerId (TkWindow *winPtr)
}

declare 72 mac {
    MacDrawable * TkMacGetHostToplevel  (TkWindow *winPtr)
}


##############################################################################

# Define the platform specific internal Xlib interfaces. These functions are
# only available on the designated platform.

interface tkIntXlib

# X functions for Windows

declare 0 win {
    XModifierKeymap* XGetModifierMapping (Display* d)
}

declare 1 win {
    XImage * XCreateImage (Display* d, Visual* v, unsigned int ui1, int i1, \
	    int i2, char* cp, unsigned int ui2, unsigned int ui3, int i3, \
	    int i4)

}

declare 2 win {
    XImage *XGetImage (Display* d, Drawable dr, int i1, int i2, \
	    unsigned int ui1, unsigned int ui2, unsigned long ul, int i3)
}

declare 3 win {
    char *XGetAtomName (Display* d,Atom a)

}

declare 4 win {
    char *XKeysymToString (KeySym k)
}

declare 5 win {
    Colormap XCreateColormap (Display* d, Window w, Visual* v, int i)

}

declare 6 win {
    Cursor XCreatePixmapCursor (Display* d, Pixmap p1, Pixmap p2, \
	    XColor* x1, XColor* x2, \
	    unsigned int ui1, unsigned int ui2)
}

declare 7 win {
    Cursor XCreateGlyphCursor (Display* d, Font f1, Font f2, \
	    unsigned int ui1, unsigned int ui2, XColor* x1, XColor* x2)
}

declare 8 win {
    GContext XGContextFromGC (GC g)
}

declare 9 win {
    XHostAddress *XListHosts (Display* d, int* i, Bool* b)
}

# second parameter was of type KeyCode
declare 10 win {
    KeySym XKeycodeToKeysym (Display* d, unsigned int k, int i)
}

declare 11 win {
    KeySym XStringToKeysym (_Xconst char* c)
}

declare 12 win {
    Window XRootWindow (Display* d, int i)
}

declare 13 win {
    XErrorHandler XSetErrorHandler  (XErrorHandler x)
}

declare 14 win {
    Status XIconifyWindow (Display* d, Window w, int i)
}

declare 15 win {
    Status XWithdrawWindow (Display* d, Window w, int i)
}

declare 16 win {
    Status XGetWMColormapWindows (Display* d, Window w, Window** wpp, int* ip)
}

declare 17 win {
    Status XAllocColor (Display* d, Colormap c, XColor* xp)
}

declare 18 win {
    void XBell (Display* d, int i)
}

declare 19 win {
    void XChangeProperty (Display* d, Window w, Atom a1, Atom a2, int i1, \
	    int i2, _Xconst unsigned char* c, int i3)
}

declare 20 win {
    void XChangeWindowAttributes (Display* d, Window w, unsigned long ul, \
	    XSetWindowAttributes* x)
}

declare 21 win {
    void XClearWindow (Display* d, Window w)
}

declare 22 win {
    void XConfigureWindow (Display* d, Window w, unsigned int i, \
	    XWindowChanges* x)
}

declare 23 win {
    void XCopyArea (Display* d, Drawable dr1, Drawable dr2, GC g, int i1, \
	    int i2, unsigned int ui1, \
	    unsigned int ui2, int i3, int i4)
}

declare 24 win {
    void XCopyPlane (Display* d, Drawable dr1, Drawable dr2, GC g, int i1, \
	    int i2, unsigned int ui1, \
	    unsigned int ui2, int i3, int i4, unsigned long ul)
}

declare 25 win {
    void XDefineCursor (Display* d, Window w, Cursor c)
}

declare 26 win {
    void XDeleteProperty (Display* d, Window w, Atom a)
}

declare 27 win {
    void XDestroyWindow (Display* d, Window w)
}

declare 28 win {
    void XDrawArc (Display* d, Drawable dr, GC g, int i1, int i2, \
	    unsigned int ui1, unsigned int ui2, int i3, int i4)
}

declare 29 win {
    void XDrawLines (Display* d, Drawable dr, GC g, XPoint* x, int i1, int i2)
}

declare 30 win {
    void XDrawRectangle (Display* d, Drawable dr, GC g, int i1, int i2,\
	    unsigned int ui1, unsigned int ui2)
}

declare 31 win {
    void XFillArc (Display* d, Drawable dr, GC g, int i1, int i2, \
	    unsigned int ui1, unsigned int ui2, int i3, int i4)
}

declare 32 win {
    void XFillPolygon (Display* d, Drawable dr, GC g, XPoint* x, \
	    int i1, int i2, int i3)
}

declare 33 win {
    void XFillRectangles (Display* d, Drawable dr, GC g, XRectangle* x, int i)
}

declare 34 win {
    void XForceScreenSaver (Display* d, int i)
}

declare 35 win {
    void XFreeColormap (Display* d, Colormap c)
}

declare 36 win {
    void XFreeColors (Display* d, Colormap c, \
	    unsigned long* ulp, int i, unsigned long ul)
}

declare 37 win {
    void XFreeCursor (Display* d, Cursor c)
}

declare 38 win {
    void XFreeModifiermap (XModifierKeymap* x)
}

declare 39 win {
    Status XGetGeometry (Display* d, Drawable dr, Window* w, int* i1, \
	    int* i2, unsigned int* ui1, unsigned int* ui2, unsigned int* ui3, \
	    unsigned int* ui4)
}

declare 40 win {
    void XGetInputFocus (Display* d, Window* w, int* i)
}

declare 41 win {
    int XGetWindowProperty (Display* d, Window w, Atom a1, long l1, long l2, \
	    Bool b, Atom a2, Atom* ap, int* ip, unsigned long* ulp1, \
	    unsigned long* ulp2, unsigned char** cpp)
}

declare 42 win {
    Status XGetWindowAttributes (Display* d, Window w, XWindowAttributes* x)
}

declare 43 win {
    int XGrabKeyboard (Display* d, Window w, Bool b, int i1, int i2, Time t)
}

declare 44 win {
    int XGrabPointer (Display* d, Window w1, Bool b, unsigned int ui, \
	    int i1, int i2, Window w2, Cursor c, Time t)
}

declare 45 win {
    KeyCode XKeysymToKeycode (Display* d, KeySym k)
}

declare 46 win {
    Status XLookupColor (Display* d, Colormap c1, _Xconst char* c2, \
	    XColor* x1, XColor* x2)
}

declare 47 win {
    void XMapWindow (Display* d, Window w)
}

declare 48 win {
    void XMoveResizeWindow (Display* d, Window w, int i1, int i2, \
	    unsigned int ui1, unsigned int ui2)
}

declare 49 win {
    void XMoveWindow (Display* d, Window w, int i1, int i2)
}

declare 50 win {
    void XNextEvent (Display* d, XEvent* x)
}

declare 51 win {
    void XPutBackEvent (Display* d, XEvent* x)
}

declare 52 win {
    void XQueryColors (Display* d, Colormap c, XColor* x, int i)
}

declare 53 win {
    Bool XQueryPointer (Display* d, Window w1, Window* w2, Window* w3, \
	    int* i1, int* i2, int* i3, int* i4, unsigned int* ui)
}

declare 54 win {
    Status XQueryTree (Display* d, Window w1, Window* w2, Window* w3, \
	    Window** w4, unsigned int* ui)
}

declare 55 win {
    void XRaiseWindow (Display* d, Window w)
}

declare 56 win {
    void XRefreshKeyboardMapping (XMappingEvent* x)
}

declare 57 win {
    void XResizeWindow (Display* d, Window w, unsigned int ui1, \
	    unsigned int ui2)
}

declare 58 win {
    void XSelectInput (Display* d, Window w, long l)
}

declare 59 win {
    Status XSendEvent (Display* d, Window w, Bool b, long l, XEvent* x)
}

declare 60 win {
    void XSetCommand (Display* d, Window w, char** c, int i)
}

declare 61 win {
    void XSetIconName (Display* d, Window w, _Xconst char* c)
}

declare 62 win {
    void XSetInputFocus (Display* d, Window w, int i, Time t)
}

declare 63 win {
    void XSetSelectionOwner (Display* d, Atom a, Window w, Time t)
}

declare 64 win {
    void XSetWindowBackground (Display* d, Window w, unsigned long ul)
}

declare 65 win {
    void XSetWindowBackgroundPixmap (Display* d, Window w, Pixmap p)
}

declare 66 win {
    void XSetWindowBorder (Display* d, Window w, unsigned long ul)
}

declare 67 win {
    void XSetWindowBorderPixmap (Display* d, Window w, Pixmap p)
}

declare 68 win {
    void XSetWindowBorderWidth (Display* d, Window w, unsigned int ui)
}

declare 69 win {
    void XSetWindowColormap (Display* d, Window w, Colormap c)
}

declare 70 win {
    Bool XTranslateCoordinates (Display* d, Window w1, Window w2, int i1,\
	    int i2, int* i3, int* i4, Window* w3)
}

declare 71 win {
    void XUngrabKeyboard (Display* d, Time t)
}

declare 72 win {
    void XUngrabPointer (Display* d, Time t) 
}

declare 73 win {
    void XUnmapWindow (Display* d, Window w)
}

declare 74 win {
    void XWindowEvent (Display* d, Window w, long l, XEvent* x)
}

declare 75 win {
    void XDestroyIC (XIC x)
}

declare 76 win {
    Bool XFilterEvent (XEvent* x, Window w)
}

declare 77 win {
    int XmbLookupString (XIC xi, XKeyPressedEvent* xk, \
	    char* c, int i, KeySym* k, Status* s)
}

declare 78 win {
    void TkPutImage (unsigned long *colors, \
	    int ncolors, Display* display, Drawable d, \
	    GC gc, XImage* image, int src_x, int src_y, \
	    int dest_x, int dest_y, unsigned int width, \
	    unsigned int height)
}

# X functions for Mac

declare 78 mac {
    XModifierKeymap* XGetModifierMapping (Display* d)
}

declare 79 mac {
    XImage * XCreateImage (Display* d, Visual* v, unsigned int ui1, int i1, \
	    int i2, char* cp, unsigned int ui2, unsigned int ui3, int i3, \
	    int i4)

}

declare 80 mac {
    XImage *XGetImage (Display* d, Drawable dr, int i1, int i2, \
	    unsigned int ui1, unsigned int ui2, unsigned long ul, int i3)
}

declare 81 mac {
    char *XGetAtomName (Display* d,Atom a)

}

declare 82 mac {
    char *XKeysymToString (KeySym k)
}

declare 83 mac {
    Colormap XCreateColormap (Display* d, Window w, Visual* v, int i)

}

#  declare 84 mac {
#      Cursor XCreatePixmapCursor (Display* d, Pixmap p1, Pixmap p2, \
#  	    XColor* x1, XColor* x2, \
#  	    unsigned int ui1, unsigned int ui2)
#  }

#  declare 85 mac {
#      Cursor XCreateGlyphCursor (Display* d, Font f1, Font f2, \
#  	    unsigned int ui1, unsigned int ui2, XColor* x1, XColor* x2)
#  }

declare 86 mac {
    GContext XGContextFromGC (GC g)
}

#  declare 87 mac {
#      XHostAddress *XListHosts (Display* d, int* i, Bool* b)
#  }

declare 88 mac {
    KeySym XKeycodeToKeysym (Display* d, KeyCode k, int i)
}

declare 89 mac {
    KeySym XStringToKeysym (_Xconst char* c)
}

declare 90 mac {
    Window XRootWindow (Display* d, int i)
}

declare 91 mac {
    XErrorHandler XSetErrorHandler  (XErrorHandler x)
}

#  declare 92 mac {
#      Status XIconifyWindow (Display* d, Window w, int i)
#  }

#  declare 93 mac {
#      Status XWithdrawWindow (Display* d, Window w, int i)
#  }

#  declare 94 mac {
#      Status XGetWMColormapWindows (Display* d, Window w, Window** wpp, int* ip)
#  }

declare 95 mac {
    Status XAllocColor (Display* d, Colormap c, XColor* xp)
}

declare 96 mac {
    void XBell (Display* d, int i)
}

declare 97 mac {
    void XChangeProperty (Display* d, Window w, Atom a, Atom a, int i1, \
	    int i2, _Xconst unsigned char* c, int i3)
}

declare 98 mac {
    void XChangeWindowAttributes (Display* d, Window w, unsigned long ul, \
	    XSetWindowAttributes* x)
}

#  declare 99 mac {
#      void XClearWindow (Display* d, Window w)
#  }

declare 100 mac {
    void XConfigureWindow (Display* d, Window w, unsigned int i, \
	    XWindowChanges* x)
}

declare 101 mac {
    void XCopyArea (Display* d, Drawable dr1, Drawable dr2, GC g, int i1, \
	    int i2, unsigned int ui1, \
	    unsigned int ui2, int i3, int i4)
}

declare 102 mac {
    void XCopyPlane (Display* d, Drawable dr1, Drawable dr2, GC g, int i1, \
	    int i2, unsigned int ui1, \
	    unsigned int ui2, int i3, int i4, unsigned long ul)
}

declare 103 mac {
    void XDefineCursor (Display* d, Window w, Cursor c)
}

#  declare 104 mac {
#      void XDeleteProperty (Display* d, Window w, Atom a)
#  }

declare 105 mac {
    void XDestroyWindow (Display* d, Window w)
}

declare 106 mac {
    void XDrawArc (Display* d, Drawable dr, GC g, int i1, int i2, \
	    unsigned int ui1, unsigned int ui2, int i3, int i4)
}

declare 107 mac {
    void XDrawLines (Display* d, Drawable dr, GC g, XPoint* x, int i1, int i2)
}

declare 108 mac {
    void XDrawRectangle (Display* d, Drawable dr, GC g, int i1, int i2,\
	    unsigned int ui1, unsigned int ui2)
}

declare 109 mac {
    void XFillArc (Display* d, Drawable dr, GC g, int i1, int i2, \
	    unsigned int ui1, unsigned int ui2, int i3, int i4)
}

declare 110 mac {
    void XFillPolygon (Display* d, Drawable dr, GC g, XPoint* x, \
	    int i1, int i2, int i3)
}

declare 111 mac {
    void XFillRectangles (Display* d, Drawable dr, GC g, XRectangle* x, int i)
}

#  declare 112 mac {
#      void XForceScreenSaver (Display* d, int i)
#  }

declare 113 mac {
    void XFreeColormap (Display* d, Colormap c)
}

declare 114 mac {
    void XFreeColors (Display* d, Colormap c, \
	    unsigned long* ulp, int i, unsigned long ul)
}

#  declare 115 mac {
#      void XFreeCursor (Display* d, Cursor c)
#  }

declare 116 mac {
    void XFreeModifiermap (XModifierKeymap* x)
}

declare 117 mac {
    Status XGetGeometry (Display* d, Drawable dr, Window* w, int* i1, \
	    int* i2, unsigned int* ui1, unsigned int* ui2, unsigned int* ui3, \
	    unsigned int* ui4)
}

#  declare 118 mac {
#      void XGetInputFocus (Display* d, Window* w, int* i)
#  }

declare 119 mac {
    int XGetWindowProperty (Display* d, Window w, Atom a1, long l1, long l2, \
	    Bool b, Atom a2, Atom* ap, int* ip, unsigned long* ulp1, \
	    unsigned long* ulp2, unsigned char** cpp)
}

#  declare 120 mac {
#      Status XGetWindowAttributes (Display* d, Window w, XWindowAttributes* x)
#  }

declare 121 mac {
    int XGrabKeyboard (Display* d, Window w, Bool b, int i1, int i2, Time t)
}

declare 122 mac {
    int XGrabPointer (Display* d, Window w1, Bool b, unsigned int ui, \
	    int i1, int i2, Window w2, Cursor c, Time t)
}

declare 123 mac {
    KeyCode XKeysymToKeycode (Display* d, KeySym k)
}

#  declare 124 mac {
#      Status XLookupColor (Display* d, Colormap c1, _Xconst char* c2, \
#  	    XColor* x1, XColor* x2)
#  }

declare 125 mac {
    void XMapWindow (Display* d, Window w)
}

declare 126 mac {
    void XMoveResizeWindow (Display* d, Window w, int i1, int i2, \
	    unsigned int ui1, unsigned int ui2)
}

declare 127 mac {
    void XMoveWindow (Display* d, Window w, int i1, int i2)
}

#  declare 128 mac {
#      void XNextEvent (Display* d, XEvent* x)
#  }

#  declare 129 mac {
#      void XPutBackEvent (Display* d, XEvent* x)
#  }

#  declare 130 mac {
#      void XQueryColors (Display* d, Colormap c, XColor* x, int i)
#  }

declare 131 mac {
    Bool XQueryPointer (Display* d, Window w1, Window* w2, Window* w3, \
	    int* i1, int* i2, int* i3, int* i4, unsigned int* ui)
}

#  declare 132 mac {
#      Status XQueryTree (Display* d, Window w1, Window* w2, Window* w3, \
#  	    Window** w4, unsigned int* ui)
#  }

declare 133 mac {
    void XRaiseWindow (Display* d, Window w)
}

declare 134 mac {
    void XRefreshKeyboardMapping (XMappingEvent* x)
}

declare 135 mac {
    void XResizeWindow (Display* d, Window w, unsigned int ui1, \
	    unsigned int ui2)
}

declare 136 mac {
    void XSelectInput (Display* d, Window w, long l)
}

declare 137 mac {
    Status XSendEvent (Display* d, Window w, Bool b, long l, XEvent* x)
}

#  declare 138 mac {
#      void XSetCommand (Display* d, Window w, char** c, int i)
#  }

declare 139 mac {
    void XSetIconName (Display* d, Window w, _Xconst char* c)
}

declare 140 mac {
    void XSetInputFocus (Display* d, Window w, int i, Time t)
}

declare 141 mac {
    void XSetSelectionOwner (Display* d, Atom a, Window w, Time t)
}

declare 142 mac {
    void XSetWindowBackground (Display* d, Window w, unsigned long ul)
}

declare 143 mac {
    void XSetWindowBackgroundPixmap (Display* d, Window w, Pixmap p)
}

declare 144 mac {
    void XSetWindowBorder (Display* d, Window w, unsigned long ul)
}

declare 145 mac {
    void XSetWindowBorderPixmap (Display* d, Window w, Pixmap p)
}

declare 146 mac {
    void XSetWindowBorderWidth (Display* d, Window w, unsigned int ui)
}

declare 147 mac {
    void XSetWindowColormap (Display* d, Window w, Colormap c)
}

#  declare 148 mac {
#      Bool XTranslateCoordinates (Display* d, Window w1, Window w2, int i1,\
#  	    int i2, int* i3, int* i4, Window* w3)
#  }

declare 149 mac {
    void XUngrabKeyboard (Display* d, Time t)
}

declare 150 mac {
    void XUngrabPointer (Display* d, Time t) 
}

declare 151 mac {
    void XUnmapWindow (Display* d, Window w)
}

#  declare 152 mac {
#      void XWindowEvent (Display* d, Window w, long l, XEvent* x)
#  }

#  declare 153 mac {
#      void XDestroyIC (XIC x)
#  }

#  declare 154 mac {
#      Bool XFilterEvent (XEvent* x, Window w)
#  }

#  declare 155 mac {
#      int XmbLookupString (XIC xi, XKeyPressedEvent* xk, \
#  	    char* c, int i, KeySym* k, Status* s)
#  }

declare 156 mac {
    void TkPutImage (unsigned long *colors, \
	    int ncolors, Display* display, Drawable d, \
	    GC gc, XImage* image, int src_x, int src_y, \
	    int dest_x, int dest_y, unsigned int width, \
	    unsigned int height)
}

