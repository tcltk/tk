/*
 * tkMacOSXSysTray.c --
 *
 *	tkMacOSXSysTray.c implements a "systray" Tcl command which allows
 *      one to change the system tray/taskbar icon of a Tk toplevel
 *      window and a "sysnotify" command to post system notifications.
 *      In macOS the icon appears on the right hand side of the menu bar.
 *
 * Copyright © 2020 Kevin Walzer/WordTech Communications LLC.
 * Copyright © 2020 Jan Nijtmans.
 * Copyright © 2020 Marc Culler.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include <tkInt.h>
#include <tkMacOSXInt.h>
#include "tkMacOSXPrivate.h"

/*
 * Prior to macOS 10.14 user notifications were handled by the NSApplication's
 * NSUserNotificationCenter via a NSUserNotificationCenterDelegate object.
 * These classes were defined in the CoreFoundation framework.  In macOS 10.14
 * a separate UserNotifications framework was introduced which adds some
 * additional features, including custom controls on the notification window
 * but primarily a requirement that an application must be authorized before
 * being allowed to post a notification.  This framework uses a different
 * class, the UNUserNotificationCenter, and its delegate follows a different
 * protocol, named UNUserNotificationCenterDelegate.
 *
 * In macOS 11.0 the NSUserNotificationCenter and its delegate protocol were
 * deprecated.  To make matters more complicated, it turns out that there is a
 * secret undocumented additional requirement that an app which is not signed
 * can never be authorized to send notifications via the UNNotificationCenter.
 * (As of 11.0, it appears that it is sufficient to sign the app with a
 * self-signed certificate, however.)
 *
 * The workaround implemented here is to define two classes, TkNSNotifier and
 * TkUNNotifier, each of which provides one of these protocols on macOS 10.14
 * and newer.  If the TkUSNotifier is able to obtain authorization it is used.
 * Otherwise, TkNSNotifier is used.  Building TkNSNotifier on 11.0 or later
 * produces deprecation warnings which are suppressed by enclosing the
 * interface and implementation in #pragma blocks.  The first time that the tk
 * systray command in initialized in an interpreter an attempt is made to
 * obtain authorization for sending notifications with the UNNotificationCenter
 * on systems and the result is saved in a static variable.
 */

//#define DEBUG
#ifdef DEBUG

/*
 * This macro uses the do ... while(0) trick to swallow semicolons.  It logs to
 * a temp file because apps launched from an icon have no stdout or stderr and
 * because NSLog has a tendency to not produce any console messages at certain
 * stages of launching an app.
 */

#define DEBUG_LOG(format, ...)                \
    do {				      \
    FILE* logfile = fopen("/tmp/tklog", "a"); \
    fprintf(logfile, format, ##__VA_ARGS__);  \
    fflush(logfile);                          \
    fclose(logfile); } while (0)
#else
#define DEBUG_LOG(format, ...)
#endif

#define BUILD_TARGET_HAS_NOTIFICATION (MAC_OS_X_VERSION_MAX_ALLOWED >= 101000)
#define BUILD_TARGET_HAS_UN_FRAMEWORK (MAC_OS_X_VERSION_MAX_ALLOWED >= 101400)
#if MAC_OS_X_VERSION_MAX_ALLOWED > 101500
#define ALERT_OPTION  UNNotificationPresentationOptionList | \
    		      UNNotificationPresentationOptionBanner
#else
#define ALERT_OPTION  UNNotificationPresentationOptionAlert
#endif

#if BUILD_TARGET_HAS_UN_FRAMEWORK
#import <UserNotifications/UserNotifications.h>
static NSString *TkNotificationCategory;
#endif

#if BUILD_TARGET_HAS_NOTIFICATION

/*
 * Class declaration for TkStatusItem.
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
- (void) clickOnStatusItem;
- (void) dealloc;

@end

/*
 * Class declaration for TkNSNotifier. A TkNSNotifier object has no attributes
 * but implements the NSUserNotificationCenterDelegate protocol.  It also has
 * one additional method which posts a user notification. There is one
 * TkNSNotifier for the application, shared by all interpreters.
 */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
@interface TkNSNotifier: NSObject {
}

/*
 * Post a notification.
 */

- (void) postNotificationWithTitle : (NSString *) title message: (NSString *) detail;

/*
 * The following methods comprise the NSUserNotificationCenterDelegate protocol.
 */

- (void) userNotificationCenter:(NSUserNotificationCenter *)center
    didDeliverNotification:(NSUserNotification *)notification;

- (void) userNotificationCenter:(NSUserNotificationCenter *)center
    didActivateNotification:(NSUserNotification *)notification;

- (BOOL) userNotificationCenter:(NSUserNotificationCenter *)center
    shouldPresentNotification:(NSUserNotification *)notification;

@end
#pragma clang diagnostic pop

/*
 * The singleton instance of TkNSNotifier shared by all interpreters in this
 * application.
 */

static TkNSNotifier *NSnotifier = nil;

#if BUILD_TARGET_HAS_UN_FRAMEWORK

/*
 * Class declaration for TkUNNotifier. A TkUNNotifier object has no attributes
 * but implements the UNUserNotificationCenterDelegate protocol It also has two
 * additional methods.  One requests authorization to post notification via the
 * UserNotification framework and the other posts a user notification. There is
 * at most one TkUNNotifier for the application, shared by all interpreters.
 */

@interface TkUNNotifier: NSObject {
}

 /*
 * Request authorization to post a notification.
 */

- (void) requestAuthorization;

/*
 * Post a notification.
 */

- (void) postNotificationWithTitle : (NSString *) title message: (NSString *) detail;

/*
 * The following methods comprise the UNNotificationCenterDelegate protocol:
 */

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
    didReceiveNotificationResponse:(UNNotificationResponse *)response
    withCompletionHandler:(void (^)(void))completionHandler;

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
    willPresentNotification:(UNNotification *)notification
    withCompletionHandler:(void (^)(UNNotificationPresentationOptions options))completionHandler;

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
   openSettingsForNotification:(UNNotification *)notification;

@end

/*
 * The singleton instance of TkUNNotifier shared by all interpeters is stored
 * in this static variable.
 */

static TkUNNotifier *UNnotifier = nil;

#endif

/*
 * Class declaration for TkStatusItem. A TkStatusItem represents an icon posted
 * on the status bar located on the right side of the MenuBar.  Each interpreter
 * may have at most one TkStatusItem.  A pointer to the TkStatusItem belonging
 * to an interpreter is stored as the clientData of the MacSystrayObjCmd instance
 * in that interpreter.  It will be NULL until the tk systray command is executed
 * by the interpreter.
 */

@implementation TkStatusItem : NSObject

- (id) init : (Tcl_Interp *) interpreter {
    [super init];
    statusBar = [NSStatusBar systemStatusBar];
    statusItem = [[statusBar statusItemWithLength:NSVariableStatusItemLength] retain];
    statusItem.button.target = self;
    statusItem.button.action = @selector(clickOnStatusItem);
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

- (void) clickOnStatusItem
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
    [statusBar removeStatusItem: statusItem];
    if (b1_callback != NULL) {
	Tcl_DecrRefCount(b1_callback);
    }
    if (b3_callback != NULL) {
	Tcl_DecrRefCount(b3_callback);
    }
    [super dealloc];
}

@end

/*
 * Type used for the ClientData of a MacSystrayObjCmd instance.
 */

typedef TkStatusItem** StatusItemInfo;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
@implementation TkNSNotifier : NSObject

-  (void) postNotificationWithTitle : (NSString * ) title
			     message: (NSString * ) detail
{
    NSUserNotification *notification;
    NSUserNotificationCenter *center;

    center = [NSUserNotificationCenter defaultUserNotificationCenter];
    notification = [[NSUserNotification alloc] init];
    notification.title = title;
    notification.informativeText = detail;
    notification.soundName = NSUserNotificationDefaultSoundName;
    DEBUG_LOG("Sending NSNotification.\n");
    [center deliverNotification:notification];
}

/*
 * Implementation of the NSUserNotificationDelegate protocol.
 */

- (BOOL) userNotificationCenter: (NSUserNotificationCenter *) center
         shouldPresentNotification: (NSUserNotification *)notification
{
    (void) center;
    (void) notification;

    return YES;
}

- (void) userNotificationCenter:(NSUserNotificationCenter *)center
         didDeliverNotification:(NSUserNotification *)notification
{
    (void) center;
    (void) notification;
}

- (void) userNotificationCenter:(NSUserNotificationCenter *)center
	 didActivateNotification:(NSUserNotification *)notification
{
    (void) center;
    (void) notification;
}

@end
#pragma clang diagnostic pop

/*
 * Static variable which records whether the app is authorized to send
 * notifications via the UNUserNotificationCenter.
 */

#if BUILD_TARGET_HAS_UN_FRAMEWORK

@implementation TkUNNotifier : NSObject

- (void) requestAuthorization
{
    UNUserNotificationCenter *center;
    UNAuthorizationOptions options = UNAuthorizationOptionAlert |
				     UNAuthorizationOptionSound |
				     UNAuthorizationOptionBadge |
	    UNAuthorizationOptionProvidesAppNotificationSettings;
    if (![NSApp isSigned]) {

	/*
	 * No point in even asking.
	 */

	DEBUG_LOG("Unsigned app: UNUserNotifications are not available.\n");
	return;
    }

    center = [UNUserNotificationCenter currentNotificationCenter];
    [center requestAuthorizationWithOptions: options
	  completionHandler: ^(BOOL granted, NSError* error)
	    {
		if (error || granted == NO) {
		    DEBUG_LOG("Authorization for UNUserNotifications denied\n");
		}
	    }];
}

-  (void) postNotificationWithTitle: (NSString * ) title
			     message: (NSString * ) detail
{
    UNUserNotificationCenter *center;
    UNMutableNotificationContent* content;
    UNNotificationRequest *request;
    center = [UNUserNotificationCenter currentNotificationCenter];
    center.delegate = (id) self;
    content = [[UNMutableNotificationContent alloc] init];
    content.title = title;
    content.body = detail;
    content.sound = [UNNotificationSound defaultSound];
    content.categoryIdentifier = TkNotificationCategory;
    request = [UNNotificationRequest
		  requestWithIdentifier:[[NSUUID UUID] UUIDString]
				content:content
				trigger:nil
	       ];
    [center addNotificationRequest: request
	withCompletionHandler: ^(NSError* error) {
	    if (error) {
		DEBUG_LOG("addNotificationRequest: error = %s\n", \
			  [NSString stringWithFormat:@"%@", \
				    error.userInfo].UTF8String);
	    }
	}];
}

/*
 * Implementation of the UNUserNotificationDelegate protocol.
 */

- (void) userNotificationCenter:(UNUserNotificationCenter *)center
         didReceiveNotificationResponse:(UNNotificationResponse *)response
	 withCompletionHandler:(void (^)(void))completionHandler
{
    /*
     * Called when the user dismisses a notification.
     */

    DEBUG_LOG("didReceiveNotification\n");
    completionHandler();
}

- (void) userNotificationCenter:(UNUserNotificationCenter *)center
    willPresentNotification:(UNNotification *)notification
    withCompletionHandler:(void (^)(UNNotificationPresentationOptions options))completionHandler
{

    /*
     * This is called before presenting a notification, even when the user has
     * turned off notifications.
     */

    DEBUG_LOG("willPresentNotification\n");
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
    if (@available(macOS 11.0, *)) {
	completionHandler(ALERT_OPTION);
    }
#endif
}

- (void) userNotificationCenter:(UNUserNotificationCenter *)center
   openSettingsForNotification:(UNNotification *)notification
{
    DEBUG_LOG("openSettingsForNotification\n");
    // Does something need to be done here?
}

@end

#endif

/*
 *----------------------------------------------------------------------
 *
 * MacSystrayDestroy --
 *
 * 	Removes an intepreters icon from the status bar.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The icon is removed and memory is freed.
 *
 *----------------------------------------------------------------------
 */

static void
MacSystrayDestroy(
    ClientData clientData,
    TCL_UNUSED(Tcl_Interp *))
{
    StatusItemInfo info = (StatusItemInfo)clientData;
    if (info) {
	[*info release];
	ckfree(info);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MacSystrayObjCmd --
 *
 * 	Main command for creating, displaying, and removing icons from the
 * 	status bar.
 *
 * Results:
 *
 *      A standard Tcl result.
 *
 * Side effects:
 *
 *	Management of icon display in the status bar.
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
    int result, idx;
    static const char *options[] =
	{"create", "modify", "destroy", NULL};
    typedef enum {TRAY_CREATE, TRAY_MODIFY, TRAY_DESTROY} optionsEnum;
    static const char *modifyOptions[] =
	{"image", "text", "b1_callback", "b3_callback", NULL};
    typedef enum {TRAY_IMAGE, TRAY_TEXT, TRAY_B1_CALLBACK, TRAY_B3_CALLBACK
        } modifyOptionsEnum;

    if ([NSApp macOSVersion] < 101000) {
	Tcl_AppendResult(interp,
	    "StatusItem icons not supported on macOS versions lower than 10.10",
	    NULL);
	return TCL_OK;
    }

    StatusItemInfo info = (StatusItemInfo)clientData;
    TkStatusItem *statusItem = *info;

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
	    Tcl_WrongNumArgs(interp, 1, objv, "create -image -text -button1 -button3");
	    return TCL_ERROR;
	}

	if (statusItem == NULL) {
	    statusItem = [[TkStatusItem alloc] init: interp];
	    *info = statusItem;
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
	    [statusItem setImagewithImage: icon];
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

	[statusItem setTextwithString: tooltip];

	/*
	 * Set the proc for the callback.
	 */

	[statusItem setB1Callback : (objc > 4) ? objv[4] : NULL];
	[statusItem setB3Callback : (objc > 5) ? objv[5] : NULL];
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
		Tcl_AppendResult(interp, " unable to obtain image for systray icon",
				 NULL);
		return TCL_ERROR;
	    }

	    Tk_SizeOfImage(tk_image, &width, &height);
	    if (width != 0 && height != 0) {
		icon = TkMacOSXGetNSImageFromTkImage(d, tk_image,
						     width, height);
		[statusItem setImagewithImage: icon];
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
		Tcl_AppendResult(interp, "unable to set tooltip for systray icon",
				 NULL);
		return TCL_ERROR;
	    }

	    [statusItem setTextwithString: tooltip];
	    break;
	}

        /*
         * Modify the proc for the callback.
         */

	case TRAY_B1_CALLBACK: {
	    [statusItem setB1Callback : objv[3]];
	    break;
	}
	case TRAY_B3_CALLBACK: {
	    [statusItem setB3Callback : objv[3]];
	    break;
	}
    }
    break;
    }

    case TRAY_DESTROY: {
	/*
	 * Set all properties to nil, and release statusItem.
	 */
        [statusItem setImagewithImage: nil];
        [statusItem setTextwithString: nil];
        [statusItem setB1Callback : NULL];
        [statusItem setB3Callback : NULL];
        [statusItem release];
        *info = NULL;
        statusItem = NULL;
        break;
    }
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * SysNotifyObjCmd --
 *
 *      Create system notification.
 *
 * Results:
 *
 *      A standard Tcl result.
 *
 * Side effects:
 *
 *      System notifications are posted.
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

    if ([NSApp macOSVersion] < 101000) {
	Tcl_AppendResult(interp,
	    "Notifications not supported on macOS versions lower than 10.10",
	     NULL);
	return TCL_OK;
    }

    NSString *title = [NSString stringWithUTF8String: Tcl_GetString(objv[1])];
    NSString *message = [NSString stringWithUTF8String: Tcl_GetString(objv[2])];

    /*
     * Update the authorization status in case the user enabled or disabled
     * notifications after the app started up.
     */

#if BUILD_TARGET_HAS_UN_FRAMEWORK

    if (UNnotifier && [NSApp isSigned]) {
    	UNUserNotificationCenter *center;

    	center = [UNUserNotificationCenter currentNotificationCenter];
        [center getNotificationSettingsWithCompletionHandler:
    	    ^(UNNotificationSettings *settings)
    	    {
#if !defined(DEBUG)
		(void) settings;
#endif
    		DEBUG_LOG("Reported authorization status is %ld\n",
			  settings.authorizationStatus);
    	    }];
           }

#endif

    if ([NSApp macOSVersion] < 101400 || ![NSApp isSigned]) {
	DEBUG_LOG("Using the NSUserNotificationCenter\n");
	[NSnotifier postNotificationWithTitle : title message: message];
    } else {

#if BUILD_TARGET_HAS_UN_FRAMEWORK

	DEBUG_LOG("Using the UNUserNotificationCenter\n");
	[UNnotifier postNotificationWithTitle : title message: message];
#endif
    }

    return TCL_OK;
}

#endif // if BUILD_TARGET_HAS_NOTIFICATION

/*
 *----------------------------------------------------------------------
 *
 * MacSystrayInit --
 *
 * 	Initialize this package and create script-level commands.
 *      This is called from TkpInit for each interpreter.
 *
 * Results:
 *
 *      A standard Tcl result.
 *
 * Side effects:
 *
 *	The tk systray and tk sysnotify commands are installed in an
 *	interpreter
 *
 *----------------------------------------------------------------------
 */

#if BUILD_TARGET_HAS_NOTIFICATION

int
MacSystrayInit(Tcl_Interp *interp)
{

    /*
     * Initialize the TkStatusItem for this interpreter and, if necessary,
     * the shared TkNSNotifier and TkUNNotifier.
     */

    StatusItemInfo info = (StatusItemInfo) ckalloc(sizeof(StatusItemInfo));
    *info = 0;

    if (NSnotifier == nil) {
	NSnotifier = [[TkNSNotifier alloc] init];
    }

#if BUILD_TARGET_HAS_UN_FRAMEWORK

    if (@available(macOS 10.14, *)) {
	UNUserNotificationCenter *center;
	UNNotificationCategory *category;
	NSSet *categories;

	if (UNnotifier == nil) {
	    UNnotifier = [[TkUNNotifier alloc] init];

	    /*
	     * Request authorization to use the UserNotification framework.  If
	     * the app code is signed and there are no notification preferences
	     * settings for this app, a dialog will be opened to prompt the
	     * user to choose settings.  Note that the request is asynchronous,
	     * so even if the preferences setting exists the result is not
	     * available immediately.
	     */

	    [UNnotifier requestAuthorization];
	}
	TkNotificationCategory = @"Basic Tk Notification";
	center = [UNUserNotificationCenter currentNotificationCenter];
	center = [UNUserNotificationCenter currentNotificationCenter];
	category = [UNNotificationCategory
		       categoryWithIdentifier:TkNotificationCategory
		       actions:@[]
		       intentIdentifiers:@[]
		       options: UNNotificationCategoryOptionNone];
	categories = [NSSet setWithObjects:category, nil];
	[center setNotificationCategories: categories];
    }
#endif

    Tcl_CreateObjCommand(interp, "::tk::systray::_systray", MacSystrayObjCmd, info,
            (Tcl_CmdDeleteProc *)MacSystrayDestroy);
    Tcl_CreateObjCommand(interp, "::tk::sysnotify::_sysnotify", SysNotifyObjCmd, NULL, NULL);
    return TCL_OK;
}

#else

int
MacSystrayInit(TCL_UNUSED(Tcl_Interp *))
{
    return TCL_OK;
}

#endif // BUILD_TARGET_HAS_NOTIFICATION

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
