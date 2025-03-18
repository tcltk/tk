/*
 * tkUnixSysTray.c --
 *
 *	tkUnixSysTray.c implements a "systray" Tcl command which permits to
 *	change the system tray/taskbar icon of a Tk toplevel window and
 *	to post system notifications.
 *
 * Copyright © 2005 Anton Kovalenko.
 * Copyright © 2020 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkUnixInt.h"

/*
 * Based extensively on the tktray extension package. Here we are removing
 * non-essential parts of tktray.
 */

#include <time.h>
#include <string.h>
#include <stdio.h>

#include <X11/X.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

/* XEmbed definitions
 * See http://www.freedesktop.org/wiki/Standards_2fxembed_2dspec
 * */
#define XEMBED_MAPPED           (1<<0)
/* System tray opcodes
 * See http://www.freedesktop.org/wiki/Standards_2fsystemtray_2dspec
 * */
#define SYSTEM_TRAY_REQUEST_DOCK    0
#define SYSTEM_TRAY_BEGIN_MESSAGE   1
#define SYSTEM_TRAY_CANCEL_MESSAGE  2

/* Flags of widget configuration options */
#define ICON_CONF_IMAGE         (1<<0)  /* Image changed */
#define ICON_CONF_REDISPLAY     (1<<1)  /* Redisplay required */
#define ICON_CONF_XEMBED        (1<<2)  /* Remapping or unmapping required */
#define ICON_CONF_CLASS         (1<<3)   /* TODO WM_CLASS update required */
#define ICON_CONF_FIRST_TIME    (1<<4)  /* For IconConfigureMethod invoked by the constructor */

/* Widget states */
#define ICON_FLAG_REDRAW_PENDING    (1<<0)
#define ICON_FLAG_ARGB32            (1<<1)
#define ICON_FLAG_DIRTY_EDGES       (1<<2)

#define TKU_NO_BAD_WINDOW_BEGIN(display) \
    { Tk_ErrorHandler error__handler = \
	    Tk_CreateErrorHandler(display, BadWindow, -1, -1, NULL, NULL);
#define TKU_NO_BAD_WINDOW_END Tk_DeleteErrorHandler(error__handler); }

/*Declaration for utility functions.*/
static void TKU_WmWithdraw(Tk_Window winPtr, Tcl_Interp* interp);
static Tk_Window TKU_GetWrapper(Tk_Window winPtr);
void TKU_AddInput(Display* dpy, Window win, long add_to_mask);
static Tk_Window TKU_Wrapper(Tk_Window w, Tcl_Interp* interp);
static Window TKU_XID(Tk_Window w);

/* Customized window withdraw */
static void
TKU_WmWithdraw(
    Tk_Window winPtr,
    TCL_UNUSED(Tcl_Interp *))
{
    TkpWmSetState((TkWindow*)winPtr, WithdrawnState);
}

/* The wrapper should exist */
static Tk_Window
TKU_GetWrapper(
    Tk_Window winPtr)
{
    return (Tk_Window)
	    TkpGetWrapperWindow((TkWindow*)winPtr);
}

/* Subscribe for extra X11 events (needed for MANAGER selection) */
void
TKU_AddInput(
    Display* dpy,
    Window win,
    long add_to_mask)
{
    XWindowAttributes xswa;
    TKU_NO_BAD_WINDOW_BEGIN(dpy)
	XGetWindowAttributes(dpy,win,&xswa);
	XSelectInput(dpy,win,xswa.your_event_mask|add_to_mask);
    TKU_NO_BAD_WINDOW_END
}

/* Get Tk Window wrapper (make it exist if ny) */
static Tk_Window
TKU_Wrapper(
    Tk_Window w,
    Tcl_Interp* interp)
{
    Tk_Window wrapper = TKU_GetWrapper(w);
    if (!wrapper) {
	Tk_MakeWindowExist(w);
	TKU_WmWithdraw(w, interp);
	Tk_MapWindow(w);
	wrapper = (Tk_Window) TKU_GetWrapper(w);
    }
    return wrapper;
}

/* Return X window id for Tk window (make it exist if ny) */
static Window
TKU_XID(
    Tk_Window w)
{
    Window xid = Tk_WindowId(w);
    if (xid == None) {
	Tk_MakeWindowExist(w);
	xid = Tk_WindowId(w);
    }
    return xid;
}

/* Data structure representing dock widget */
typedef struct {
    /* standard for widget */
    Tk_Window tkwin, drawingWin;
    Window wrapper;
    Window myManager;
    Window trayManager;

    Tk_OptionTable options;
    Tcl_Interp *interp;
    Tcl_Command widgetCmd;

    Tk_Image image; /* image to be drawn */

    /* Only one of imageVisualInstance and photo is needed for argb32
     * operations. Unless imageString changes, imageVisualInstance is
     * always valid for the same drawingWin instance, but photo is
     * invalidated by any "whole image" type change. */

    Tk_Image imageVisualInstance; /* image instance for use with argb32 */
    Tk_PhotoHandle photo;	  /* !null if it's really a photo */

    /* Offscreen pixmap is created for a given imageWidth,
     * imageHeight, drawingWin, and invalidated (and freed) on image
     * resize or drawingWin destruction.

     * Contents of this pixmap is synced on demand; when image changes
     * but is not resized, pixmap is marked as out-of-sync. Next time
     * when redisplay is needed, pixmap is updated before drawing
     * operation.
     */

    Pixmap offscreenPixmap;
    /* There is no need to recreate GC ever; it remains valid once
     * created */

    GC offscreenGC;

    /* XImage for drawing ARGB32 photo on offscreenPixmap.  Should be
     * freed and nullified each time when a pixmap is freed.  Needed
     * (and created) when redrawing an image being a photo on ARGB32
     * offscreen pixmap. */
    XImage *offscreenImage;	/* for photo (argb32) drawing code */

    Visual *bestVisual;		/* Visual, when it's specified by tray
				 * manager AND is guessed to be
				 * ARGB32 */
    Colormap bestColormap;	/* Colormap for bestVisual */

    Atom aMANAGER;
    Atom a_NET_SYSTEM_TRAY_Sn;
    Atom a_XEMBED_INFO;
    Atom a_NET_SYSTEM_TRAY_MESSAGE_DATA;
    Atom a_NET_SYSTEM_TRAY_OPCODE;
    Atom a_NET_SYSTEM_TRAY_ORIENTATION;
    Atom a_NET_SYSTEM_TRAY_VISUAL;

    int flags; /* ICON_FLAG_ - see defines above */
    int msgid; /* Last balloon message ID */
    int useShapeExt;

    int x,y,width,height;
    int imageWidth, imageHeight;
    int requestedWidth, requestedHeight;
    int visible; /* whether XEMBED_MAPPED should be set */
    int docked;	 /* whether an icon should be docked */
    Tcl_Obj *imageObj; /* option: -image */
    Tcl_Obj *classObj; /* option: -class */
} DockIcon;

/*
 * Forward declarations for procedures defined in this file.
 */

static Tcl_ObjCmdProc2 TrayIconCreateCmd;
static Tcl_ObjCmdProc2 TrayIconObjectCmd;
static int TrayIconConfigureMethod(DockIcon *icon, Tcl_Interp *interp,
	Tcl_Size objc, Tcl_Obj *const objv[], int addflags);
static int PostBalloon(DockIcon* icon, const char *utf8msg,
	long timeout);
static void CancelBalloon(DockIcon* icon, int msgid);
static int QueryTrayOrientation(DockIcon* icon);

static Tcl_CmdDeleteProc TrayIconDeleteProc;
static Atom DockSelectionAtomFor(Tk_Window tkwin);
static void DockToManager(DockIcon *icon);
static void CreateTrayIconWindow(DockIcon *icon);

static void TrayIconRequestSize(DockIcon* icon, int w, int h);
static void TrayIconForceImageChange(DockIcon* icon);
static void TrayIconUpdate(DockIcon* icon, int mask);

static void EventuallyRedrawIcon(DockIcon* icon);
static void DisplayIcon(void *cd);

static void RetargetEvent(DockIcon *icon, XEvent *ev);

static void TrayIconEvent(void *cd, XEvent* ev);
static void UserIconEvent(void *cd, XEvent* ev);
static void TrayIconWrapperEvent(void *cd, XEvent* ev);
static int IconGenericHandler(void *cd, XEvent *ev);

int Tktray_Init (Tcl_Interp* interp );

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
 *	None.
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
    int bbox[4] = {0,0,0,0};
    Tcl_Obj * bboxObj;
    int wcmd;
    int i;
    XWindowAttributes xwa;
    Window bogus;
    int msgid;

    enum {XWC_CONFIGURE = 0, XWC_CGET, XWC_BALLOON, XWC_CANCEL,
	    XWC_BBOX, XWC_DOCKED, XWC_ORIENTATION};
    const char *st_wcmd[] = {"configure", "cget", "balloon", "cancel",
	    "bbox", "docked", "orientation", NULL};

    long timeout = 0;
    Tcl_Obj* optionValue;

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

    case XWC_CGET:
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

    case XWC_BALLOON:
	if ((objc != 3) && (objc != 4)) {
	    Tcl_WrongNumArgs(interp, 2, objv, "message ?timeout?");
	    return TCL_ERROR;
	}
	if (objc == 4) {
	    if (Tcl_GetLongFromObj(interp,objv[3],&timeout) != TCL_OK)
		return TCL_ERROR;
	}
	msgid = PostBalloon(icon,Tcl_GetString(objv[2]), timeout);
	Tcl_SetObjResult(interp,Tcl_NewIntObj(msgid));
	return TCL_OK;

    case XWC_CANCEL:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "messageId");
	    return TCL_ERROR;
	}
	if (Tcl_GetIntFromObj(interp,objv[2],&msgid) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (msgid)
	    CancelBalloon(icon,msgid);
	return TCL_OK;

    case XWC_BBOX:
	if (icon->drawingWin) {
	    XGetWindowAttributes(Tk_Display(icon->drawingWin),
		    TKU_XID(icon->drawingWin), &xwa);

	    XTranslateCoordinates(Tk_Display(icon->drawingWin),
		    TKU_XID(icon->drawingWin), xwa.root, 0,0,
		    &icon->x, &icon->y, &bogus);
	    bbox[0] = icon->x;
	    bbox[1] = icon->y;
	    bbox[2] = bbox[0] + icon->width - 1;
	    bbox[3] = bbox[1] + icon->height - 1;
	}
	bboxObj = Tcl_NewObj();
	for (i = 0; i < 4; ++i) {
	    Tcl_ListObjAppendElement(interp, bboxObj, Tcl_NewIntObj(bbox[i]));
	}
	Tcl_SetObjResult(interp, bboxObj);
	return TCL_OK;

    case XWC_DOCKED:
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(icon->myManager != None));
	return TCL_OK;

    case XWC_ORIENTATION:
	if (icon->myManager == None || icon->wrapper == None) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("none", TCL_INDEX_NONE));
	} else {
	    switch(QueryTrayOrientation(icon)) {
	    case 0:
		Tcl_SetObjResult(interp, Tcl_NewStringObj("horizontal", TCL_INDEX_NONE));
		break;
	    case 1:
		Tcl_SetObjResult(interp, Tcl_NewStringObj("vertical", TCL_INDEX_NONE));
		break;
	    default:
		Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown", TCL_INDEX_NONE));
		break;
	    }
	}
	return TCL_OK;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * QueryTrayOrientation --
 *
 *	Obtain the orientation of the tray icon.
 *
 * Results:
 *	Orientation is returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
QueryTrayOrientation(
    DockIcon* icon)
{
    Atom retType = None;
    int retFormat = 32;
    unsigned long retNitems, retBytesAfter;
    unsigned char *retProp = NULL;
    int result = -1;

    if (icon->wrapper != None && icon->myManager != None) {
	XGetWindowProperty(Tk_Display(icon->tkwin),
			   icon->myManager,
			   icon->a_NET_SYSTEM_TRAY_ORIENTATION,
			   /* offset */ 0,
			   /* length */ 1,
			   /* delete */ False,
			   /* type */ XA_CARDINAL,
			   &retType, &retFormat, &retNitems,
			   &retBytesAfter, &retProp);
	if (retType == XA_CARDINAL && retFormat == 32 && retNitems == 1) {
	    result = (int) *(long*)retProp;
	}
	if (retProp) {
	    XFree(retProp);
	}
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * DockSelectionAtomFor --
 *
 *	Obtain the dock selection atom.
 *
 * Results:
 *	Selection returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Atom
DockSelectionAtomFor(
    Tk_Window tkwin)
{
    char buf[256];
    snprintf(buf,256,"_NET_SYSTEM_TRAY_S%d",Tk_ScreenNumber(tkwin));
    return Tk_InternAtom(tkwin,buf);
}

/*
 *----------------------------------------------------------------------
 *
 * XembedSetState --
 *
 *	Set the xembed state.
 *
 * Results:
 *	Updates the xembed state.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
XembedSetState(
    DockIcon *icon,
    long xembedState)
{
    long info[] = { 0, 0 };
    info[1] = xembedState;
    if (icon->drawingWin) {
	XChangeProperty(Tk_Display(icon->drawingWin),
			icon->wrapper,
			icon->a_XEMBED_INFO,
			icon->a_XEMBED_INFO, 32,
			PropModeReplace, (unsigned char*)info, 2);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XembedRequestDock --
 *
 *	Obtain the docking window.
 *
 * Results:
 *	The dock window is requested.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
XembedRequestDock(
    DockIcon *icon)
{
    Tk_Window tkwin = icon->drawingWin;
    XEvent ev;
    Display *dpy = Tk_Display(tkwin);

    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = icon->myManager;
    ev.xclient.message_type = icon->a_NET_SYSTEM_TRAY_OPCODE;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 0;
    ev.xclient.data.l[1] = SYSTEM_TRAY_REQUEST_DOCK;
    ev.xclient.data.l[2] = icon->wrapper;
    ev.xclient.data.l[3] = 0;
    ev.xclient.data.l[4] = 0;
    XSendEvent(dpy, icon->myManager, True, StructureNotifyMask|SubstructureNotifyMask, &ev);
 }

/*
 *----------------------------------------------------------------------
 *
 * CheckArgbVisual --
 *
 *	Find out if a visual is recommended and if it looks like argb32.
 *
 * Results:
 *	Render the visual as needed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
CheckArgbVisual(
    DockIcon *icon)
{
    /* Find out if a visual is recommended and if it looks like argb32.
     * For such visuals we should:
     * Recreate a window if it's created but the depth is wrong;
     * Don't use ParentRelative but blank background.
     * For photo images, draw into a window by XPutImage.
     */
    Atom retType = None;
    int retFormat = 32;
    unsigned long retNitems, retBytesAfter;
    unsigned char *retProp = NULL;
    Visual *match = NULL;
    int depth = 0;
    Colormap cmap = None;

    TKU_NO_BAD_WINDOW_BEGIN(Tk_Display(icon->tkwin))
	XGetWindowProperty(Tk_Display(icon->tkwin),
		icon->trayManager,
		icon->a_NET_SYSTEM_TRAY_VISUAL,
		/* offset */ 0,
		/* length */ 1,
		/* delete */ False,
		/* type */ XA_VISUALID,
		&retType, &retFormat, &retNitems,
		&retBytesAfter, &retProp);
    TKU_NO_BAD_WINDOW_END
    if (retType == XA_VISUALID &&
	    retNitems == 1 &&
	    retFormat == 32) {
	char numeric[256];
	snprintf(numeric,256,"%ld",*(long*)retProp);
	XFree(retProp);
	match = Tk_GetVisual(icon->interp, icon->tkwin,
		numeric, &depth, &cmap);
    }
    if (match&& depth == 32 &&
	    match->red_mask == 0xFF0000UL &&
	    match->green_mask == 0x00FF00UL &&
	    match->blue_mask == 0x0000FFUL) {
	icon->bestVisual = match;
	icon->bestColormap = cmap;
    } else {
	icon->bestVisual = NULL;
	icon->bestColormap = None;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CreateTrayIconWindow --
 *
 *	Create and configure the window for the icon tray.
 *
 * Results:
 *	The window is created and displayed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
CreateTrayIconWindow(
    DockIcon *icon)
{
    Tcl_InterpState saved;
    Tk_Window tkwin;
    Tk_Window wrapper;
    XSetWindowAttributes attr;

    saved = Tcl_SaveInterpState(icon->interp, TCL_OK);
    /* Use the same name (tail) as the widget name, to enable
     * name-based icon management for supporting trays, as promised by
     * the docs.
     */
    tkwin = icon->drawingWin = Tk_CreateWindow(icon->interp, icon->tkwin,
	    Tk_Name(icon->tkwin), "");
    if (tkwin) {
	Tk_SetClass(icon->drawingWin, Tcl_GetString(icon->classObj));
	Tk_CreateEventHandler(icon->drawingWin,ExposureMask|StructureNotifyMask|
		ButtonPressMask|ButtonReleaseMask|
		EnterWindowMask|LeaveWindowMask|PointerMotionMask,
		TrayIconEvent, icon);
	if(icon->bestVisual) {
	    Tk_SetWindowVisual(icon->drawingWin,icon->bestVisual,
		    32,icon->bestColormap);
	    icon->flags |= ICON_FLAG_ARGB32;
	    Tk_SetWindowBackground(tkwin, 0);
	} else {
	    Tk_SetWindowBackgroundPixmap(tkwin, ParentRelative);
	    icon->flags &= ~ICON_FLAG_ARGB32;
	}
	Tk_MakeWindowExist(tkwin);
	TKU_WmWithdraw(tkwin,icon->interp);
	wrapper = TKU_Wrapper(tkwin,icon->interp);

	attr.override_redirect = True;
	Tk_ChangeWindowAttributes(wrapper,CWOverrideRedirect,&attr);
	Tk_CreateEventHandler(wrapper,StructureNotifyMask,TrayIconWrapperEvent, icon);
	if (!icon->bestVisual) {
	    Tk_SetWindowBackgroundPixmap(wrapper, ParentRelative);
	} else {
	    Tk_SetWindowBackground(tkwin, 0);
	}
	icon->wrapper = TKU_XID(wrapper);
	TrayIconForceImageChange(icon);
    } else {
	Tcl_BackgroundError(icon->interp);
    }
    Tcl_RestoreInterpState(icon->interp, saved);
}

/*
 *----------------------------------------------------------------------
 *
 * DockToManager --
 *
 *	Helper function to manage icon in display.
 *
 * Results:
 *	Icon is created and displayed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
DockToManager(
    DockIcon *icon)
{
    icon->myManager = icon->trayManager;
    Tk_SendVirtualEvent(icon->tkwin,Tk_GetUid("IconCreate"), NULL);
    XembedSetState(icon, icon->visible ? XEMBED_MAPPED : 0);
    XembedRequestDock(icon);
}

static const
Tk_OptionSpec IconOptionSpec[] = {
    {TK_OPTION_STRING,"-image","image","Image",
	NULL, offsetof(DockIcon, imageObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, NULL,
	ICON_CONF_IMAGE | ICON_CONF_REDISPLAY},
    {TK_OPTION_STRING,"-class","class","Class",
	"TrayIcon", offsetof(DockIcon, classObj), TCL_INDEX_NONE,
	0, NULL, ICON_CONF_CLASS},
    {TK_OPTION_BOOLEAN,"-docked","docked","Docked",
	"1", TCL_INDEX_NONE, offsetof(DockIcon, docked), 0, NULL,
	ICON_CONF_XEMBED | ICON_CONF_REDISPLAY},
    {TK_OPTION_BOOLEAN,"-shape","shape","Shape",
	"0", TCL_INDEX_NONE, offsetof(DockIcon, useShapeExt), 0, NULL,
	ICON_CONF_IMAGE | ICON_CONF_REDISPLAY},
    {TK_OPTION_BOOLEAN,"-visible","visible","Visible",
	"1", TCL_INDEX_NONE, offsetof(DockIcon, visible), 0, NULL,
	ICON_CONF_XEMBED | ICON_CONF_REDISPLAY},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0}
};

/*
 *----------------------------------------------------------------------
 *
 * TrayIconRequestSize --
 *
 *	Set icon size.
 *
 * Results:
 *	Icon size is obtained/set.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
TrayIconRequestSize(
    DockIcon* icon,
    int w,
    int h)
{
    if (icon->drawingWin) {
	if (icon->requestedWidth != w ||
		icon->requestedHeight != h) {
	    Tk_SetMinimumRequestSize(icon->drawingWin,w,h);
	    Tk_GeometryRequest(icon->drawingWin,w,h);
	    Tk_SetGrid(icon->drawingWin,1,1,w,h);
	    icon->requestedWidth = w;
	    icon->requestedHeight = h;
	}
    } else {
	/* Sign that no size is requested yet */
	icon->requestedWidth = 0;
	icon->requestedHeight = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TrayIconImageChanged --
 *
 *	Fires when icon state changes.
 *
 * Results:
 *	Icon changes are rendered.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
TrayIconImageChanged(
    void *cd,
    int x,
    int y,
    int w,
    int h,
    int imgw,
    int imgh)
{
    DockIcon *icon = (DockIcon*) cd;
    if (imgw != icon->imageWidth || imgh != icon->imageHeight) {
	if (icon->offscreenImage) {
	    XDestroyImage(icon->offscreenImage);
	    icon->offscreenImage = NULL;
	}
	if (icon->offscreenPixmap) {
	    /* its size is bad */
	    Tk_FreePixmap(Tk_Display(icon->tkwin), icon->offscreenPixmap);
	    icon->offscreenPixmap = None;
	}
	/* if some image dimension decreases,
	 * empty areas around the image should be cleared */
	if (imgw < icon->imageWidth || imgh < icon->imageHeight) {
	    icon->flags |= ICON_FLAG_DIRTY_EDGES;
	}
    }
    icon->imageWidth = imgw;
    icon->imageHeight = imgh;
    if (imgw == w && imgh == h && x == 0 && y == 0) {
	icon->photo = NULL;	/* invalidate */
    }
    TrayIconRequestSize(icon,imgw,imgh);
    EventuallyRedrawIcon(icon);
}

/*
 *----------------------------------------------------------------------
 *
 * IgnoreImageChange --
 *
 *	Currently no-op.
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
IgnoreImageChange(
    TCL_UNUSED(void *),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
}

/*
 *----------------------------------------------------------------------
 *
 * ForceImageChange --
 *
 *	Push icon changes through.
 *
 * Results:
 *	Icon image is updated.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
TrayIconForceImageChange(
    DockIcon* icon)
{
    if (icon->image) {
	int w,h;
	Tk_SizeOfImage(icon->image,&w,&h);
	TrayIconImageChanged(icon, 0, 0, w, h, w, h);
    }
}

/*
 *----------------------------------------------------------------------
 *
 *  EventuallyRedrawIcon --
 *
 *	Update image icon.
 *
 * Results:
 *	Icon image is updated.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
EventuallyRedrawIcon(
    DockIcon* icon)
{
    if (icon->drawingWin && icon->myManager) {	/* don't redraw invisible icon */
	if (!(icon->flags & ICON_FLAG_REDRAW_PENDING)) { /* don't schedule multiple redraw ops */
	    icon->flags |= ICON_FLAG_REDRAW_PENDING;
	    Tcl_DoWhenIdle(DisplayIcon, icon);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 *  DisplayIcon --
 *
 *	Main function for displaying icon.
 *
 * Results:
 *	Icon image is displayed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
DisplayIcon(
    void *cd)
{
    DockIcon *icon = (DockIcon*)cd;
    int w = icon->imageWidth, h = icon->imageHeight;
    int imgx, imgy, outx, outy, outw, outh;
    imgx = (icon->width >= w) ? 0 : -(icon->width - w)/2;
    imgy = (icon->height >= h) ? 0 : -(icon->height - h)/2;
    outx = (icon->width >= w) ? (icon->width - w)/2 : 0;
    outy = (icon->height >= h) ? (icon->height - h)/2 : 0;
    outw = (icon->width >= w) ? w : icon->width;
    outh = (icon->height >= h) ? h : icon->height;

    icon->flags &= (~ICON_FLAG_REDRAW_PENDING);

    if (icon->drawingWin && icon->docked) {
	if (icon->flags & ICON_FLAG_ARGB32) {
	    /* ARGB32 redraw: never use a ParentRelative method, and
	       no need to clear window except FIXME when its size changed.
	       Draw on the offscreen pixmap instead, then copy to the window.
	     */
	    if (icon->offscreenPixmap == None) {
		icon->offscreenPixmap = Tk_GetPixmap(Tk_Display(icon->drawingWin),
			Tk_WindowId(icon->drawingWin), w, h, 32);
	    }
	    if (!icon->photo) {
		icon->photo = Tk_FindPhoto(icon->interp, Tcl_GetString(icon->imageObj));
	    }
	    if (!icon->photo && !icon->imageVisualInstance) {
		Tcl_InterpState saved
			= Tcl_SaveInterpState(icon->interp, TCL_OK);
		icon->imageVisualInstance = Tk_GetImage(icon->interp,icon->drawingWin,
			Tcl_GetString(icon->imageObj), IgnoreImageChange, NULL);
		Tcl_RestoreInterpState(icon->interp,saved);
	    }
	    if (icon->photo && !icon->offscreenImage) {
		icon->offscreenImage = XGetImage(Tk_Display(icon->drawingWin),
			icon->offscreenPixmap, 0, 0, w, h, AllPlanes, ZPixmap);
	    }
	    if (icon->offscreenGC == NULL) {
		XGCValues gcv;
		gcv.function = GXcopy;
		gcv.plane_mask = AllPlanes;
		gcv.foreground = 0;
		gcv.background = 0;
		icon->offscreenGC = Tk_GetGC(icon->drawingWin,
			GCFunction|GCPlaneMask|GCForeground|GCBackground, &gcv);
	    }
	    if (icon->flags & ICON_FLAG_DIRTY_EDGES) {
		XClearWindow(Tk_Display(icon->drawingWin), TKU_XID(icon->drawingWin));
		icon->flags &= ~ICON_FLAG_DIRTY_EDGES;
	    }
	    if (icon->photo) {
		Tk_PhotoImageBlock pib;
		int cx,cy;
		XImage *xim = icon->offscreenImage;
		/* redraw photo using raw data */
		Tk_PhotoGetImage(icon->photo,&pib);
		for (cy = 0; cy < h; ++cy) {
		    for (cx = 0; cx < w; ++cx) {
			XPutPixel(xim,cx,cy,
				  (*(pib.pixelPtr +
				     pib.pixelSize*cx +
				     pib.pitch*cy +
				     pib.offset[0])<<16) |
				  (*(pib.pixelPtr +
				     pib.pixelSize*cx +
				     pib.pitch*cy +
				     pib.offset[1])<<8) |
				  (*(pib.pixelPtr +
				     pib.pixelSize*cx +
				     pib.pitch*cy +
				     pib.offset[2])) |
				  (pib.offset[3] ?
				   (*(pib.pixelPtr +
				      pib.pixelSize*cx +
				      pib.pitch*cy +
				      pib.offset[3])<<24) : 0));
		    }
		}
		XPutImage(Tk_Display(icon->drawingWin),
			icon->offscreenPixmap,
			icon->offscreenGC,
			icon->offscreenImage,
			0,0,0,0,w,h);
	    } else {
		XFillRectangle(Tk_Display(icon->drawingWin),
			icon->offscreenPixmap,
			icon->offscreenGC,
			0,0,w,h);
		if (icon->imageVisualInstance) {
		    Tk_RedrawImage(icon->imageVisualInstance,
			    0,0,w,h,
			    icon->offscreenPixmap,
			    0,0);
		}
	    }
	    XCopyArea(Tk_Display(icon->drawingWin),
		    icon->offscreenPixmap,
		    TKU_XID(icon->drawingWin),
		    icon->offscreenGC,
		    imgx,imgy,outw,outh,outx,outy);
	} else {
	    /* Non-argb redraw: clear window and draw an image over it.
	       For photos it gives a correct alpha blending with a parent
	       window background, even if it's a fancy pixmap (proved to
	       work with lxpanel fancy backgrounds).
	    */
	    XClearWindow(Tk_Display(icon->drawingWin),
		    TKU_XID(icon->drawingWin));
	    if (icon->image && icon->visible) {
		Tk_RedrawImage(icon->image,imgx,imgy,outw,outh,
			TKU_XID(icon->drawingWin), outx, outy);
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 *  RetargetEvent --
 *
 *	Redirect X events to widgets.
 *
 * Results:
 *	Icon image is displayed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
RetargetEvent(
    DockIcon *icon,
    XEvent *ev)
{
    int send = 0;
    Window* saveWin1 = NULL, *saveWin2 = NULL;
    if (!icon->visible)
	return;
    switch (ev->type) {
    case MotionNotify:
	send = 1;
	saveWin1 = &ev->xmotion.subwindow;
	saveWin2 = &ev->xmotion.window;
	break;
    case LeaveNotify:
    case EnterNotify:
	send = 1;
	saveWin1 = &ev->xcrossing.subwindow;
	saveWin2 = &ev->xcrossing.window;
	break;
    case ButtonPress:
    case ButtonRelease:
	send = 1;
	saveWin1 = &ev->xbutton.subwindow;
	saveWin2 = &ev->xbutton.window;
	break;
    case MappingNotify:
	send = 1;
	saveWin1 = &ev->xmapping.window;
    }
    if (saveWin1) {
	Tk_MakeWindowExist(icon->tkwin);
	*saveWin1 = Tk_WindowId(icon->tkwin);
	if (saveWin2) *saveWin2 = *saveWin1;
    }
    if (send) {
	ev->xany.send_event = 0x147321ac;
	Tk_HandleEvent(ev);
    }
}

/*
 *----------------------------------------------------------------------
 *
 *  TrayIconWrapperEvent --
 *
 *	Ensure automapping in root window is done in withdrawn state.
 *
 * Results:
 *	Icon image is displayed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
TrayIconWrapperEvent(
    void *cd,
    XEvent* ev)
{
  /* Some embedders, like Docker, add icon windows to save set
   * (XAddToSaveSet), so when they crash the icon is reparented to root.
   * We have to make sure that automatic mapping in root is done in
   * withdrawn state (no way to prevent it entirely)
   */
    DockIcon *icon = (DockIcon*)cd;
    XWindowAttributes attr;
    if (icon->drawingWin) {
	switch(ev->type) {
	case ReparentNotify:
	    /* With virtual roots and screen roots etc, the only way
	       to check for reparent-to-root is to ask for this root
	       first */
	    XGetWindowAttributes(ev->xreparent.display,
		    ev->xreparent.window, &attr);
	    if (attr.root == ev->xreparent.parent) {
		/* upon reparent to root, */
		if (icon->drawingWin) {
		    /* we were sent away to root */
		    TKU_WmWithdraw(icon->drawingWin,icon->interp);
		    if (icon->myManager)
			Tk_SendVirtualEvent(icon->tkwin,Tk_GetUid("IconDestroy"), NULL);
		    icon->myManager = None;
		}
	    } /* Reparenting into some other embedder is theoretically possible,
	       * and everything would just work in this case.
	       */
	    break;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TrayIconEvent --
 *
 *	Handle X events.
 *
 * Results:
 *	Events are handled and processed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
TrayIconEvent(
    void *cd,
    XEvent* ev)
{
    DockIcon *icon = (DockIcon*)cd;

    switch (ev->type) {
    case Expose:
	if (!ev->xexpose.count)
	    EventuallyRedrawIcon(icon);
	break;

    case DestroyNotify:
	/* If anonymous window is destroyed first, then either
	 * something went wrong with a tray (if -visible) or we just
	 * reconfigured to invisibility: nothing to be done in both
	 * cases.
	 * If unreal window is destroyed first, freeing the data structures
	 * is the only thing to do.
	 */
	if (icon->myManager) {
	    Tk_SendVirtualEvent(icon->tkwin,Tk_GetUid("IconDestroy"), NULL);
	}
	Tcl_CancelIdleCall(DisplayIcon, icon);
	icon->flags &= ~ICON_FLAG_REDRAW_PENDING;
	icon->drawingWin = NULL;
	icon->requestedWidth = 0; /* trigger re-request on recreation */
	icon->requestedHeight = 0;
	icon->wrapper = None;
	icon->myManager = None;
	break;

    case ConfigureNotify:
	Tk_SendVirtualEvent(icon->tkwin,Tk_GetUid("IconConfigure"), NULL);
	if (icon->width != ev->xconfigure.width ||
		icon->height != ev->xconfigure.height) {
	    icon->width = ev->xconfigure.width;
	    icon->height = ev->xconfigure.height;
	    icon->flags |= ICON_FLAG_DIRTY_EDGES;
	    EventuallyRedrawIcon(icon);
	}
	RetargetEvent(icon,ev);
	break;

    case MotionNotify:  /* fall through */
    case ButtonPress:   /* fall through */
    case ButtonRelease: /* fall through */
    case EnterNotify:   /* fall through */
    case LeaveNotify:
	RetargetEvent(icon,ev);
	break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UserIconEvent --
 *
 *	Handle user events.
 *
 * Results:
 *	Events are handled and processed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
UserIconEvent(
    void *cd,
    XEvent* ev)
{
    DockIcon *icon = (DockIcon*)cd;

    switch (ev->type) {

    case DestroyNotify:
	Tk_DeleteGenericHandler(IconGenericHandler, icon);
	if(icon->drawingWin) {
	    icon->visible = 0;
	    Tcl_CancelIdleCall(DisplayIcon, icon);
	    icon->flags &= ~ICON_FLAG_REDRAW_PENDING;
	    Tk_DestroyWindow(icon->drawingWin);
	}
	if(icon->imageVisualInstance) {
	    Tk_FreeImage(icon->imageVisualInstance);
	    icon->image = NULL;
	}
	if(icon->offscreenImage) {
	    XDestroyImage(icon->offscreenImage);
	    icon->offscreenImage = NULL;
	}
	if(icon->offscreenGC) {
	    Tk_FreeGC(Tk_Display(icon->tkwin),icon->offscreenGC);
	    icon->offscreenGC = NULL;
	}
	if(icon->offscreenPixmap) {
	    Tk_FreePixmap(Tk_Display(icon->tkwin),icon->offscreenPixmap);
	}
	if(icon->image) {
	    Tk_FreeImage(icon->image);
	    icon->image = NULL;
	}
	if(icon->widgetCmd)
	    Tcl_DeleteCommandFromToken(icon->interp,icon->widgetCmd);
	Tk_FreeConfigOptions((char*)icon, icon->options, icon->tkwin);
	break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PostBalloon --
 *
 *	Display tooltip/balloon window over tray icon.
 *
 * Results:
 *	Window is displayed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
PostBalloon(
    DockIcon *icon,
    const char *utf8msg,
    long timeout)
{
    Tk_Window tkwin = icon -> tkwin;
    Display* dpy = Tk_Display(tkwin);
    int length = strlen(utf8msg);
    XEvent ev;

    if (!(icon->drawingWin) || (icon->myManager == None))
	return 0;

    /* overflow protection */
    if (icon->msgid < 0)
	icon->msgid = 0;

    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = icon->wrapper;
    ev.xclient.message_type = icon->a_NET_SYSTEM_TRAY_OPCODE;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = CurrentTime;
    ev.xclient.data.l[1] = SYSTEM_TRAY_BEGIN_MESSAGE;
    ev.xclient.data.l[2] = timeout;
    ev.xclient.data.l[3] = length;
    ev.xclient.data.l[4] = ++icon->msgid;
    TKU_NO_BAD_WINDOW_BEGIN(Tk_Display(icon->tkwin))
	XSendEvent(dpy, icon->myManager , True, StructureNotifyMask|SubstructureNotifyMask, &ev);
	XSync(dpy, False);

	/* Sending message elements */
	while (length>0) {
	    ev.type = ClientMessage;
	    ev.xclient.window = icon->wrapper;
	    ev.xclient.message_type = icon->a_NET_SYSTEM_TRAY_MESSAGE_DATA;
	    ev.xclient.format = 8;
	    memset(ev.xclient.data.b,0,20);
	    strncpy(ev.xclient.data.b,utf8msg,length<20?length:20);
	    XSendEvent(dpy, icon->myManager, True, StructureNotifyMask|SubstructureNotifyMask, &ev);
	    XSync(dpy,False);
	    utf8msg += 20;
	    length -= 20;
	}
    TKU_NO_BAD_WINDOW_END;
    return icon->msgid;
}

/*
 *----------------------------------------------------------------------
 *
 * CancelBalloon --
 *
 *	Remove balloon from display over tray icon.
 *
 * Results:
 *	Window is destroyed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
CancelBalloon(
    DockIcon *icon,
    int msgid)
{
    Tk_Window tkwin = icon -> tkwin;
    Display* dpy = Tk_Display(tkwin);
    XEvent ev;

    if (!(icon->drawingWin) || (icon->myManager == None))
	return;
    /* overflow protection */
    if (icon->msgid < 0)
	icon->msgid = 0;

    memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.xclient.window = icon->wrapper;
    ev.xclient.message_type = icon->a_NET_SYSTEM_TRAY_OPCODE;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = CurrentTime;
    ev.xclient.data.l[1] = SYSTEM_TRAY_CANCEL_MESSAGE;
    ev.xclient.data.l[2]  =msgid;
    TKU_NO_BAD_WINDOW_BEGIN(Tk_Display(icon->tkwin))
	XSendEvent(dpy, icon->myManager , True,
		StructureNotifyMask|SubstructureNotifyMask, &ev);
    TKU_NO_BAD_WINDOW_END
}

/*
 *----------------------------------------------------------------------
 *
 * IconGenericHandler --
 *
 *	Process non-tk events.
 *
 * Results:
 *	Events are processed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
IconGenericHandler(
    void *cd,
    XEvent *ev)
{
    DockIcon *icon = (DockIcon*)cd;

    if ((ev->type == ClientMessage) &&
	    (ev->xclient.message_type == icon->aMANAGER) &&
	    ((Atom)ev->xclient.data.l[1] == icon->a_NET_SYSTEM_TRAY_Sn)) {
	icon->trayManager = (Window)ev->xclient.data.l[2];
	XSelectInput(ev->xclient.display,icon->trayManager,StructureNotifyMask);
	if (icon->myManager == None)
	    TrayIconUpdate(icon, ICON_CONF_XEMBED);
	return 1;
    }
    if (ev->type == DestroyNotify) {
	if (ev->xdestroywindow.window == icon->trayManager) {
	    icon->trayManager = None;
	}
	if (ev->xdestroywindow.window == icon->myManager) {
	    icon->myManager = None;
	    icon->wrapper = None;
	    if (icon->drawingWin) {
		Tk_DestroyWindow(icon->drawingWin);
		icon->drawingWin = NULL;
	    }
	}
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TrayIconUpdate --
 *
 *	Get in touch with new options that are certainly valid.
 *
 * Results:
 *	Options updated.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
TrayIconUpdate(
    DockIcon *icon,
    int mask)
{
    /* why should someone need this option?
     * anyway, let's handle it if we provide it.
     */
    if (mask & ICON_CONF_CLASS) {
	if (icon->drawingWin)
	    Tk_SetClass(icon->drawingWin,Tk_GetUid(Tcl_GetString(icon->classObj)));
    }
    /*
     * First, ensure right icon visibility.
     * If should be visible and not yet managed,
     * we have to get the tray or wait for it.
     * If should be invisible and managed,
     * real-window is simply destroyed.
     * If should be invisible and not managed,
     * generic handler should be abandoned.
     */
    if (mask & ICON_CONF_XEMBED) {
	if (icon->myManager == None &&
		icon->trayManager != None &&
		icon->docked) {
	    CheckArgbVisual(icon);
	    if (icon->drawingWin &&
		    ((icon->bestVisual && !(icon->flags & ICON_FLAG_ARGB32)) ||
		     (!icon->bestVisual && (icon->flags & ICON_FLAG_ARGB32)))) {
		icon->myManager = None;
		icon->wrapper = None;
		icon->requestedWidth = icon->requestedHeight = 0;
		Tk_DestroyWindow(icon->drawingWin);
		icon->drawingWin = NULL;
	    }
	    if (!icon->drawingWin) {
		CreateTrayIconWindow(icon);
	    }
	    if (icon->drawingWin) {
		DockToManager(icon);
	    }
	}
	if (icon->myManager != None &&
		icon->drawingWin != NULL &&
		!icon->docked) {
	    Tk_DestroyWindow(icon->drawingWin);
	    icon->drawingWin = NULL;
	    icon->myManager = None;
	    icon->wrapper = None;
	}
	if (icon->drawingWin) {
	    XembedSetState(icon, icon->visible ? XEMBED_MAPPED : 0);
	}
    }
    if (mask & ICON_CONF_IMAGE) {
	TrayIconForceImageChange(icon);
    }
    if (mask & ICON_CONF_REDISPLAY) {
	EventuallyRedrawIcon(icon);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TrayIconConfigureMethod --
 *
 *      Returns TCL_ERROR if some option is invalid,
 *      or else retrieve resource references and free old resources.
 *
 * Results:
 *	Widget configured.
 *
 * Side effects:
 *	None.
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
	    return TCL_ERROR; /* msg by Tk_GetOptionInfo */
	}
    }

    if (Tk_SetOptions(interp, icon,icon->options,objc,objv,
	    icon->tkwin,&saved,&mask) != TCL_OK) {
	return TCL_ERROR; /* msg by Tk_SetOptions */
    }
    mask |= addflags;
    /* now check option validity */
    if (mask & ICON_CONF_IMAGE) {
	if (icon->imageObj) {
	    newImage = Tk_GetImage(interp, icon->tkwin, Tcl_GetString(icon->imageObj),
		    TrayIconImageChanged, icon);
	    if (!newImage) {
		Tk_RestoreSavedOptions(&saved);
		return TCL_ERROR; /* msg by Tk_GetImage */
	    }
	}
	if (icon->image) {
	    Tk_FreeImage(icon->image);
	    icon->image = NULL;
	}
	if (icon->imageVisualInstance) {
	    Tk_FreeImage(icon->imageVisualInstance);
	    icon->imageVisualInstance = NULL;
	}
	icon->image = newImage; /* may be null, as intended */
	icon->photo = NULL; /* invalidate photo reference */
    }
    Tk_FreeSavedOptions(&saved);
    /* Now as we are reconfigured... */
    TrayIconUpdate(icon,mask);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TrayIconDeleteProc --
 *
 *      Delete tray window and clean up.
 *
 * Results:
 *	Window destroyed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
TrayIconDeleteProc(
    void *cd )
{
    DockIcon *icon = (DockIcon *)cd;
    Tk_DestroyWindow(icon->tkwin);
}

/*
 *----------------------------------------------------------------------
 *
 * TrayIconCreateCmd --
 *
 *      Create tray command and (unreal) window.
 *
 * Results:
 *	Icon tray and hidden window created.
 *
 * Side effects:
 *	None.
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

    icon = (DockIcon*)attemptckalloc(sizeof(DockIcon));
    if (!icon) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("running out of memory", TCL_INDEX_NONE));
	goto handleErrors;
    }
    memset(icon,0,sizeof(*icon));

    if (objc < 2||(objc%2)) {
	Tcl_WrongNumArgs(interp, 1, objv, "pathName ?option value ...?");
	goto handleErrors;
    }

    /* It's not a toplevel window by now. It really doesn't matter,
     * because it's not really shown.
     */
    icon->tkwin = Tk_CreateWindowFromPath(interp, mainWindow,
	    Tcl_GetString(objv[1]),"");
    if (icon->tkwin == NULL) {
	goto handleErrors;
    }

    /* Subscribe to StructureNotify */
    TKU_AddInput(Tk_Display(icon->tkwin),
	    RootWindowOfScreen(Tk_Screen(icon->tkwin)),StructureNotifyMask);
    TKU_AddInput(Tk_Display(icon->tkwin),
	    RootWindow(Tk_Display(icon->tkwin),0),StructureNotifyMask);
    /* Spec says "screen 0" not "default", but... */
    TKU_AddInput(Tk_Display(icon->tkwin),
	    DefaultRootWindow(Tk_Display(icon->tkwin)),StructureNotifyMask);

    /* Early tracking of DestroyNotify is essential */
    Tk_CreateEventHandler(icon->tkwin,StructureNotifyMask,
	    UserIconEvent, icon);

    /* Now try setting options */
    icon->options = Tk_CreateOptionTable(interp,IconOptionSpec);
    /* Class name is used for retrieving defaults, so... */
    Tk_SetClass(icon->tkwin, Tk_GetUid("TrayIcon"));
    if (Tk_InitOptions(interp,(char*)icon,icon->options,icon->tkwin) != TCL_OK) {
	goto handleErrors;
    }

    icon->a_NET_SYSTEM_TRAY_Sn = DockSelectionAtomFor(icon->tkwin);
    icon->a_NET_SYSTEM_TRAY_OPCODE = Tk_InternAtom(icon->tkwin,"_NET_SYSTEM_TRAY_OPCODE");
    icon->a_NET_SYSTEM_TRAY_MESSAGE_DATA = Tk_InternAtom(icon->tkwin,"_NET_SYSTEM_TRAY_MESSAGE_DATA");
    icon->a_NET_SYSTEM_TRAY_ORIENTATION = Tk_InternAtom(icon->tkwin,"_NET_SYSTEM_TRAY_ORIENTATION");
    icon->a_NET_SYSTEM_TRAY_VISUAL = Tk_InternAtom(icon->tkwin,"_NET_SYSTEM_TRAY_VISUAL");
    icon->a_XEMBED_INFO = Tk_InternAtom(icon->tkwin,"_XEMBED_INFO");
    icon->aMANAGER = Tk_InternAtom(icon->tkwin,"MANAGER");

    icon->interp = interp;

    icon->trayManager = XGetSelectionOwner(Tk_Display(icon->tkwin), icon->a_NET_SYSTEM_TRAY_Sn);
    if (icon->trayManager) {
	XSelectInput(Tk_Display(icon->tkwin),icon->trayManager, StructureNotifyMask);
    }

    Tk_CreateGenericHandler(IconGenericHandler, icon);

    if (objc>3) {
	if (TrayIconConfigureMethod(icon, interp, objc-2, objv+2,
		ICON_CONF_XEMBED|ICON_CONF_IMAGE|ICON_CONF_FIRST_TIME) != TCL_OK) {
	    goto handleErrors;
	}
    }

    icon->widgetCmd = Tcl_CreateObjCommand2(interp, Tcl_GetString(objv[1]),
	    TrayIconObjectCmd, icon, TrayIconDeleteProc);

    /* Sometimes a command just can't be created... */
    if (!icon->widgetCmd) {
	goto handleErrors;
    }

    Tcl_SetObjResult(interp,objv[1]);
    return TCL_OK;

handleErrors:
    /* Rolling back */
    if (icon) {
	if (icon->options) {
	    Tk_DeleteOptionTable(icon->options);
	    icon->options = NULL;
	}
	if (icon->tkwin) {
	    /* Resources will be freed by DestroyNotify handler */
	    Tk_DestroyWindow(icon->tkwin);
	}
	ckfree(icon);
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Tktray_Init --
 *
 *      Initialize the command.
 *
 * Results:
 *	Command initialized.
 *
 * Side effects:
 *	None.
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
