/*
 * tkMacOSXSysTray.c --
 *
 *	tkMacOSXSysTray.c implements a "systray" Tcl command which allows
 *      one to change the system tray/taskbar icon of a Tk toplevel
 *      window and a "sysnotify" command to post system notifications.
 *
 * Copyright (c) 2020 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include <tkInt.h>
#include <tkMacOSXInt.h>
#include "tkMacOSXPrivate.h"

static const char ASSOC_KEY[] = "tk::tktray";

/*
 * Class declarations and implementations for TkStatusItem.
 */

@interface TkStatusItem: NSObject {
    NSStatusItem * statusItem;
    NSStatusBar * statusBar;
    NSImage * icon;
    NSString * tooltip;
    Tcl_Interp * interp;
    Tcl_Obj * b1_callback;
    Tcl_Obj * b3_callback;
}

- (id) init : (Tcl_Interp *) interp;
- (void) setImagewithImage : (NSImage *) image;
- (void) setTextwithString : (NSString *) string;
- (void) setB1Callback : (Tcl_Obj *) callback;
- (void) setB3Callback : (Tcl_Obj *) callback;
- (void) clickOnStatusItem: (id) sender;
- (void) dealloc;


@end

@implementation TkStatusItem : NSObject

- (id) init : (Tcl_Interp *) interpreter {
    [super init];
    statusBar = [NSStatusBar systemStatusBar];
    statusItem = [[statusBar statusItemWithLength:NSVariableStatusItemLength] retain];
    statusItem.button.target = self;
    statusItem.button.action = @selector(clickOnStatusItem: );
    [statusItem.button sendActionOn : NSEventMaskLeftMouseUp | NSEventMaskRightMouseUp];
    statusItem.visible = YES;
    interp = interpreter;
    b1_callback = NULL;
    b3_callback = NULL;
    return self;
}

- (void) setImagewithImage : (NSImage *) image
{
    icon = nil;
    icon = image;
    statusItem.button.image = icon;
}

- (void) setTextwithString : (NSString *) string
{
    tooltip = nil;
    tooltip = string;
    statusItem.button.toolTip = tooltip;
}

- (void) setB1Callback : (Tcl_Obj *) obj
{
    if (obj != NULL) {
	Tcl_IncrRefCount(obj);
    }
    if (b1_callback != NULL) {
	Tcl_DecrRefCount(b1_callback);
    }
    b1_callback = obj;
}

- (void) setB3Callback : (Tcl_Obj *) obj
{
    if (obj != NULL) {
	Tcl_IncrRefCount(obj);
    }
    if (b3_callback != NULL) {
	Tcl_DecrRefCount(b3_callback);
    }
    b3_callback = obj;
}

- (void) clickOnStatusItem: (id) sender
{
    NSEvent *event = [NSApp currentEvent];
    if (([event type] == NSEventTypeLeftMouseUp) && (b1_callback != NULL)) {
	int result = Tcl_EvalObjEx(interp, b1_callback, TCL_EVAL_GLOBAL);
	if (result != TCL_OK) {
	    Tcl_BackgroundException(interp, result);
	}
    } else {
	if (([event type] == NSEventTypeRightMouseUp) && (b3_callback != NULL)) {
	int result = Tcl_EvalObjEx(interp, b3_callback, TCL_EVAL_GLOBAL);
	if (result != TCL_OK) {
	    Tcl_BackgroundException(interp, result);
	}
    }
}
}
- (void) dealloc
{
    /*
     * We are only doing the minimal amount of deallocation that
     * the superclass cannot handle when it is deallocated, per
     * https://developer.apple.com/documentation/objectivec/nsobject/
     * 1571947-dealloc. The compiler may offer warnings, but disregard.
     * Putting too much here can cause unpredictable crashes, especially
     * in the Tk test suite.
     */
    [statusBar removeStatusItem: statusItem];
    if (b1_callback != NULL) {
	Tcl_DecrRefCount(b1_callback);
    }
    if (b3_callback != NULL) {
	Tcl_DecrRefCount(b3_callback);
    }
}

@end

/*
 * Class declarations and implementations for TkNotifyItem.
 */

@interface TkNotifyItem: NSObject {

    NSUserNotification *tk_notification;
    NSString *header;
    NSString *info;
}

- (id) init;
- (void) postNotificationWithTitle : (NSString *) title message: (NSString *) detail;
- (BOOL) userNotificationCenter:(NSUserNotificationCenter *)center
      shouldPresentNotification:(NSUserNotification *)notification;
- (void) dealloc;


@end

@implementation TkNotifyItem : NSObject

-  (id) init
{
    [super init];
    tk_notification = [[NSUserNotification alloc] init];
    return self;
}

-  (void) postNotificationWithTitle : (NSString * ) title message: (NSString * ) detail
{
    header = title;
    tk_notification.title = header;
    info = detail;
    tk_notification.informativeText = info;
    tk_notification.soundName = NSUserNotificationDefaultSoundName;
    NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];

    [center setDelegate: (id) self];
    [center deliverNotification:tk_notification];
}

- (BOOL) userNotificationCenter: (NSUserNotificationCenter * ) center
      shouldPresentNotification: (NSUserNotification * ) notification
{
    return YES;
}


-  (void) dealloc
{
    /*
     * We are only doing the minimal amount of deallocation that
     * the superclass cannot handle when it is deallocated, per
     * https://developer.apple.com/documentation/objectivec/nsobject/
     * 1571947-dealloc. The compiler may offer warnings, but disregard.
     * Putting too much here can cause unpredictable crashes, especially
     * in the Tk test suite.
     */
    tk_notification = nil;
}

@end

/*
 * Main objects of this file.
 */

typedef struct {
    TkStatusItem *tk_item;
    TkNotifyItem *notify_item;
} TrayInfo;

/*
 *----------------------------------------------------------------------
 *
 * MacSystrayObjCmd --
 *
 * 	Main command for creating, displaying, and removing icons from status menu.
 *
 * Results:
 *	Management of icon display in status menu.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */


static int
MacSystrayObjCmd(
		 void *clientData,
		 Tcl_Interp * interp,
		 int objc,
		 Tcl_Obj *const *objv)
{
    Tk_Image tk_image;
    TrayInfo *info = (TrayInfo *)clientData;
    int result, idx;
    static const char *options[] =
	{"create",	"modify", "destroy", NULL};
    typedef enum {TRAY_CREATE, TRAY_MODIFY, TRAY_DESTROY} optionsEnum;

    static const char *modifyOptions[] =
	{"image",  "text", "b1_callback", "b3_callback", NULL};
    typedef enum {TRAY_IMAGE, TRAY_TEXT, TRAY_B1_CALLBACK, TRAY_B3_CALLBACK} modifyOptionsEnum;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "create | modify | destroy");
	return TCL_ERROR;
    }

    result = Tcl_GetIndexFromObjStruct(interp, objv[1], options,
				       sizeof(char *), "command", 0, &idx);

    if (result != TCL_OK) {
    	return TCL_ERROR;
    }
    switch((optionsEnum)idx) {
    case TRAY_CREATE: {

	if (objc < 3 ||  objc > 6) {
	    Tcl_WrongNumArgs(interp, 1, objv, "create image ?text? ?b1_callback? b3_callback?");
	    return TCL_ERROR;
	}

	if (info->tk_item == NULL) {
	    info->tk_item = [[TkStatusItem alloc] init: interp];
	} else {
	    Tcl_AppendResult(interp, "Only one system tray icon supported per interpreter", NULL);
	    return TCL_ERROR;
	}

	/*
	 * Create the icon.
	 */

	int width, height;
	Tk_Window tkwin = Tk_MainWindow(interp);
	TkWindow *winPtr = (TkWindow *)tkwin;
	Display *d = winPtr->display;
	NSImage *icon;

	tk_image = Tk_GetImage(interp, tkwin, Tcl_GetString(objv[2]), NULL, NULL);
	if (tk_image == NULL) {
	    return TCL_ERROR;
	}

	Tk_SizeOfImage(tk_image, &width, &height);
	if (width != 0 && height != 0) {
	    icon = TkMacOSXGetNSImageFromTkImage(d, tk_image,
						 width, height);
	    [info->tk_item setImagewithImage: icon];
	    Tk_FreeImage(tk_image);
	}

	/*
	 * Set the text for the tooltip.
	 */

	NSString *tooltip = [NSString stringWithUTF8String: Tcl_GetString(objv[3])];
	if (tooltip == nil) {
	    Tcl_AppendResult(interp, " unable to set tooltip for systray icon", NULL);
	    return TCL_ERROR;
	}

	[info->tk_item setTextwithString: tooltip];

	/*
	 * Set the proc for the callback.
	 */

	[info->tk_item setB1Callback : (objc > 4) ? objv[4] : NULL];
	[info->tk_item setB3Callback : (objc > 5) ? objv[5] : NULL];
	break;

    }
    case TRAY_MODIFY: {
	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 1, objv, "modify object item");
	    return TCL_ERROR;
	}

	/*
	 * Modify the icon.
	 */

	result = Tcl_GetIndexFromObjStruct(interp, objv[2], modifyOptions,
					   sizeof(char *), "option", 0, &idx);

	if (result != TCL_OK) {
	    return TCL_ERROR;
	}
	switch ((modifyOptionsEnum)idx) {
	case TRAY_IMAGE: {
	    Tk_Window tkwin = Tk_MainWindow(interp);
	    TkWindow *winPtr = (TkWindow *)tkwin;
	    Display *d = winPtr -> display;
	    NSImage *icon;
	    int width, height;

	    tk_image = Tk_GetImage(interp, tkwin, Tcl_GetString(objv[3]), NULL, NULL);
	    if (tk_image == NULL) {
		Tcl_AppendResult(interp, " unable to obtain image for systray icon", (char * ) NULL);
		return TCL_ERROR;
	    }

	    Tk_SizeOfImage(tk_image, &width, &height);
	    if (width != 0 && height != 0) {
		icon = TkMacOSXGetNSImageFromTkImage(d, tk_image,
						     width, height);
		[info->tk_item setImagewithImage: icon];
	    }
	    Tk_FreeImage(tk_image);
	    break;
	}

	    /*
	     * Modify the text for the tooltip.
	     */

	case TRAY_TEXT: {
	    NSString *tooltip = [NSString stringWithUTF8String:Tcl_GetString(objv[3])];
	    if (tooltip == nil) {
		Tcl_AppendResult(interp, "unable to set tooltip for systray icon", NULL);
		return TCL_ERROR;
	    }

	    [info->tk_item setTextwithString: tooltip];
	    break;
	}

	    /*
	     * Modify the proc for the callback.
	     */

	case TRAY_B1_CALLBACK: {
	    [info->tk_item setB1Callback : objv[3]];
	    break;
	}
	case TRAY_B3_CALLBACK: {
	    [info->tk_item setB3Callback : objv[3]];
	    break;
	}
    }
    break;
    }

    case TRAY_DESTROY: {
	    /* we don't really distroy, just reset the image, text and callback */
	    [info->tk_item setImagewithImage: nil];
	    [info->tk_item setTextwithString: nil];
	    [info->tk_item setB1Callback : NULL];
	    [info->tk_item setB3Callback : NULL];
	    break;
	}
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * MacSystrayDestroy --
 *
 * 	Deletes icon from display.
 *
 * Results:
 *	Icon/window removed and memory freed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
MacSystrayDestroy(
		  void *clientData,
		  TCL_UNUSED(Tcl_Interp *))
{
    TrayInfo *info = (TrayInfo *)clientData;

    if (info->tk_item != NULL) {
	[info->tk_item dealloc];
	info->tk_item = NULL;
    }
    if (info->notify_item != NULL) {
	[info->notify_item dealloc];
	info->notify_item = NULL;
    }
    ckfree(info);
}


/*
 *----------------------------------------------------------------------
 *
 * SysNotifyCreateCmd --
 *
 *      Create tray command and (unreal) window.
 *
 * Results:
 *	Icon tray and hidden window created.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------z---------------------------------------
 */


static int SysNotifyObjCmd(
			   void *clientData,
			   Tcl_Interp * interp,
			   int objc,
			   Tcl_Obj *const *objv)
{
    TrayInfo *info = (TrayInfo *) clientData;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "title message");
	return TCL_ERROR;
    }

    if (info->notify_item == NULL) {
	info->notify_item = [[TkNotifyItem alloc] init];
    }

    NSString *title = [NSString stringWithUTF8String: Tcl_GetString(objv[1])];
    NSString *message = [NSString stringWithUTF8String: Tcl_GetString(objv[2])];
    [info->notify_item postNotificationWithTitle : title message: message];

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * MacSystrayInit --
 *
 * 	Initialize this package and create script-level commands.
 *
 * Results:
 *	Initialization of code.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
MacSystrayInit(Tcl_Interp *interp)
{

    /*
     * Initialize TkStatusItem and TkNotifyItem.
     */

    TrayInfo *info = (TrayInfo *)ckalloc(sizeof(TrayInfo));

    memset(info, 0, sizeof(TrayInfo));
    Tcl_SetAssocData(interp, ASSOC_KEY, MacSystrayDestroy, info);

    if ([NSApp macOSVersion] < 101000) {
	Tcl_AppendResult(interp, "Statusitem icons not supported on versions of macOS lower than 10.10", NULL);
	return TCL_OK;
    }

    Tcl_CreateObjCommand(interp, "_systray", MacSystrayObjCmd, info, NULL);
    Tcl_CreateObjCommand(interp, "_sysnotify", SysNotifyObjCmd, info, NULL);

    return TCL_OK;
}

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
