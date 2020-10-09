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

/*
 * Script callback when status icon is clicked.
 */

Tcl_Obj *callbackproc;

/*
 * Class declarations and implementations for TkStatusItem.
 */

@interface TkStatusItem: NSObject {
    NSStatusItem * statusItem;
    NSStatusBar * statusBar;
    NSImage * icon;
    NSString * tooltip;
}

- (id) init;
- (void) setImagewithImage : (NSImage * ) image;
- (void) setTextwithString : (NSString * ) string;
- (void) clickOnStatusItem: (id) sender;
- (void) dealloc;


@end

@implementation TkStatusItem : NSObject

- (id) init {
    [super init];
    statusBar = [NSStatusBar systemStatusBar];
    statusItem = [[statusBar statusItemWithLength:NSVariableStatusItemLength] retain];
    statusItem.button.target = self;
    statusItem.button.action = @selector(clickOnStatusItem: );
    statusItem.visible = YES;
    return self;
}

- (void) setImagewithImage : (NSImage * ) image
{
    icon = nil;
    icon = image;
    statusItem.button.image = icon;
}

- (void) setTextwithString : (NSString * ) string
{
    tooltip = nil;
    tooltip = string;
    statusItem.button.toolTip = tooltip;
}

- (void) clickOnStatusItem: (id) sender
{
    if (NSApp.currentEvent.clickCount == 1) {
	TkMainInfo *info = TkGetMainInfoList();
	Tcl_EvalObjEx(info -> interp, callbackproc, TCL_EVAL_GLOBAL);
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
    
    /*
     * This API requires an app delegate to function correctly. 
     * The compiler may complain that setting TkNotificationItem is an 
     * incompatible type, but disregard. Setting to something else will
     * either cause Wish not to build, or the notification not to display.
     */
    [center setDelegate: self];
    
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
TkStatusItem *tk_item;
TkNotifyItem *notify_item;

/*
 * Forward declarations for procedures defined in this file.
 */

static void MacSystrayDestroy();
static void SysNotifyDeleteCmd(void *);
static int MacSystrayObjCmd(void *, Tcl_Interp *, int, Tcl_Obj *const *);
static int SysNotifyObjCmd(void *, Tcl_Interp *, int, Tcl_Obj *const *);

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
    TCL_UNUSED(void *),
    Tcl_Interp * interp,
    int objc,
	Tcl_Obj *const *objv)
{
	Tk_Image tk_image;
    TkSizeT length;
    const char *arg = TkGetStringFromObj(objv[1], &length);
    if ((strncmp(arg, "create", length) == 0) && (length >= 2)) {

	if (objc < 5) {
	    Tcl_WrongNumArgs(interp, 1, objv, "create image ?text? ?callback?");
	    return TCL_ERROR;
	}

	/*
	 * Create the icon.
	 */

	int width, height;
	Tk_Window tkwin = Tk_MainWindow(interp);
	TkWindow *winPtr = (TkWindow *)tkwin;
	Display *d = winPtr -> display;
	NSImage *icon;

	arg = TkGetStringFromObj(objv[2], &length);
	tk_image = Tk_GetImage(interp, tkwin, arg, NULL, NULL);
	if (tk_image == NULL) {
	    return TCL_ERROR;
	}

	Tk_SizeOfImage(tk_image, &width, &height);
	if (width != 0 && height != 0) {
	    icon = TkMacOSXGetNSImageFromTkImage(d, tk_image,
						 width, height);
	    [tk_item setImagewithImage: icon];
	    Tk_FreeImage(tk_image);
	}

	/*
	 * Set the text for the tooltip.
	 */

	NSString *tooltip = [NSString stringWithUTF8String: Tcl_GetString(objv[3])];
	if (tooltip == nil) {
	    Tcl_AppendResult(interp, " unable to set tooltip for systray icon", (char * ) NULL);
	    return TCL_ERROR;
	}

	[tk_item setTextwithString: tooltip];

	/*
	 * Set the proc for the callback.
	 */

	callbackproc = objv[4];
	Tcl_IncrRefCount(callbackproc);
	if (callbackproc == NULL) {
	    Tcl_AppendResult(interp, " unable to get the callback for systray icon", (char * ) NULL);
	    return TCL_ERROR;
	}

    } else if ((strncmp(arg, "modify",  length) == 0) &&
	       (length >= 2)) {
	if (objc < 4) {
	    Tcl_WrongNumArgs(interp, 1, objv, "modify object item");
	    return TCL_ERROR;
	}

	const char *modifyitem = Tcl_GetString(objv[2]);

	/*
	 * Modify the icon.
	 */

	if (strcmp (modifyitem, "image") == 0) {

	    Tk_Window tkwin = Tk_MainWindow(interp);
	    TkWindow *winPtr = (TkWindow *)tkwin;
	    Display *d = winPtr -> display;
	    NSImage *icon;
	    int width, height;

	    arg = Tcl_GetString(objv[3]);
	    tk_image = Tk_GetImage(interp, tkwin, arg, NULL, NULL);
	    if (tk_image == NULL) {
		Tcl_AppendResult(interp, " unable to obtain image for systray icon", (char * ) NULL);
		return TCL_ERROR;
	    }

	    Tk_SizeOfImage(tk_image, &width, &height);
	    if (width != 0 && height != 0) {
		icon = TkMacOSXGetNSImageFromTkImage(d, tk_image,
						     width, height);
		[tk_item setImagewithImage: icon];
	    }
	    Tk_FreeImage(tk_image);
	}

	/*
	 * Modify the text for the tooltip.
	 */

	if (strcmp (modifyitem, "text") == 0) {

	    NSString *tooltip = [NSString stringWithUTF8String:Tcl_GetString(objv[3])];
	    if (tooltip == nil) {
		Tcl_AppendResult(interp, " unable to set tooltip for systray icon", NULL);
		return TCL_ERROR;
	    }

	    [tk_item setTextwithString: tooltip];
	}

	/*
	 * Modify the proc for the callback.
	 */

	if (strcmp (modifyitem, "callback") == 0) {
	    callbackproc = objv[3];
	    Tcl_IncrRefCount(callbackproc);
	    if (callbackproc == NULL) {
		Tcl_AppendResult(interp, " unable to get the callback for systray icon", NULL);
		return TCL_ERROR;
	    }
	}

    } else if ((strncmp(arg, "destroy", length) == 0) && (length >= 2)) {
	[tk_item dealloc];
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
MacSystrayDestroy() {
    [tk_item dealloc];
}


/*
 *----------------------------------------------------------------------
 *
 * SysNotifyDeleteCmd --
 *
 *      Delete notification and clean up.
 *
 * Results:
 *	Window destroyed.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------z---------------------------------------
 */


static void SysNotifyDeleteCmd (
    TCL_UNUSED(void *))
{
    [notify_item dealloc];
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
    TCL_UNUSED(void *),
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj *const *objv)
{
    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "title message");
	return TCL_ERROR;
    }

    NSString *title = [NSString stringWithUTF8String: Tcl_GetString(objv[1])];
    NSString *message = [NSString stringWithUTF8String: Tcl_GetString(objv[2])];
    [notify_item postNotificationWithTitle : title message: message];

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
MacSystrayInit(Tcl_Interp *interp) {

    /*
     * Initialize TkStatusItem and TkNotifyItem.
     */

    tk_item = [[TkStatusItem alloc] init];
    notify_item = [[TkNotifyItem alloc] init];

    if ([NSApp macOSVersion] < 101000) {
	Tcl_AppendResult(interp, "Statusitem icons not supported on versions of macOS lower than 10.10", NULL);
	return TCL_OK;
    }

    Tcl_CreateObjCommand(interp, "_systray", MacSystrayObjCmd, interp,
		      (Tcl_CmdDeleteProc *) MacSystrayDestroy);
    Tcl_CreateObjCommand(interp, "_sysnotify", SysNotifyObjCmd, NULL, SysNotifyDeleteCmd);

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
