/*
 * tkMacOSXSysTray.c --
 *
 *	tkMacOSXSysTray.c implements a "systray" Tcl command which allows one to
 * 	change the system tray/taskbar icon of a Tk toplevel window and
 * 	a "sysnotify" command to post system notifications.
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

char * callbackproc;

/*
 * Class declaratons and implementations for TkStatusItem. 
 */

@interface TkStatusItem: NSObject {
    NSStatusItem * statusItem;
    NSStatusBar * statusBar;
    NSImage * icon;
    NSString * tooltip;


-  (id) init; 
-  (void) setImage: withImage: (NSImage * ) image; 
-  (void) setText: withString: (NSString * ) string; 
-  (void) clickOnStatusItem: (id) sender; 
-  (void) dealloc;

}

@end

@implementation TkStatusItem

- (id) init {
    [super init];
    statusBar = [NSStatusBar systemStatusBar];
    statusItem = [statusBar statusItemWithLength: NSSquareStatusItemLength];
    statusItem.button.target = self;
    statusItem.button.action = @selector(clickOnStatusItem: );
    return self;
}

- (void) setImage: withImage: (NSImage * ) image
{
    icon = image;
    icon.template = YES;
    statusItem.button.image = icon;
}

- (void) setText: withString: (NSString * ) string
{
    tooltip = string;
    statusItem.button.tooltip = tooltip;
}

- (void) clickOnStatusItem: (id) sender
{
    if (NSApp.currentEvent.clickCount == 1) {
	TkMainInfo * info = TkGetMainInfoList();
	Tcl_GlobalEval(info -> interp, callbackproc);
    }
}

- (void) dealloc
{
    [statusBar removeStatusItem: statusItem];
    [icon release];
    [tooltip release];
    [statusItem release];
    [statusBar release];

    [super dealloc];
}

@end

/*
 * Class declaratons and implementations for TkNotifyItem. 
 */

@interface TkNotifyItem: NSObject {

    NSUserNotification *tk_notification;
    NSString *header;
    NSString *info

- (id) init;
- (void) postNotification: withTitle: (NSString *) title andDetail: (NSString *) detail;
- (BOOL) userNotificationCenter:(NSUserNotificationCenter *)center
	  shouldPresentNotification:(NSUserNotification *)notification;
- (void) dealloc;
}

@end

@implementation: TkNotifyItem

-  (id) init 
{
    [super init];
    tk_notification = [[NSUserNotification alloc] init];
    return self;
}

-  (void) postNotification: withTitle: (NSString * ) title andDetail: (NSString * ) detail
{
    header = title;
    tk_notification.title = header;
    info = detail;
    tk_notification.informativeText = info;
    tk_notification.soundName = NSUserNotificationDefaultSoundName;

}

- (BOOL) userNotificationCenter: (NSUserNotificationCenter * ) center
      shouldPresentNotification: (NSUserNotification * ) notification 
{
    return YES;
}

-  (void) dealloc	
{
    [header release];
    [info release];
    [super dealloc];
}

@end


/*
 * Forward declarations for procedures defined in this file.
 */

static int
MacSystrayCmd(ClientData clientData, Tcl_Interp * interp,
	      int argc,
	      const char * argv[]);
static int
MacSystrayModifyCmd(ClientData clientData, Tcl_Interp * interp,
		    int argc,
		    const char * argv[]);
static void
MacSystrayDestroy(ClientData clientData);
static void SysNotifyDeleteCmd ( ClientData cd );
static int SysNotifyCmd (ClientData clientData, Tcl_Interp * interp,
			 int argc, const char * argv[]);
int
MacSystrayInit(Tcl_Interp * interp);

/*
 *----------------------------------------------------------------------
 *
 * MacSystrayCmd --
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
MacSystrayCmd(ClientData clientData, Tcl_Interp * interp,
	      int argc,
	      const char * argv[]) {

    if ((strcmp argv[1], "modify") == 0) {
	MacSystrayModifyCmd(clientData, interp, argc, argv);
	return TCL_OK;
    }

       if ((strcmp argv[1], "destroy") == 0) {
	MacSystrayDestroy(clientData);
	return TCL_OK;
    }

    if (argc < 5) {
	Tcl_AppendResult(interp, "wrong # args: should be \"systray create image ? text? callback?\"", (char * ) NULL);
	return TCL_ERROR;
    }

    /*
     * Create the icon.
     */

    Tk_Window tkwin = Tk_MainWindow(interp);
    TkWindow winPtr = (TkWindow) tkwin;
    Display * d;
    d = winPtr -> display;
    NSImage * icon;

    char * tk_imagename = Tcl_GetString(argv[2]);
    Tk_Image tk_image;
    tk_image = Tk_GetImage(interp, tkwin, tk_imagename, NULL, NULL);
    if (tk_image == NULL) {
	Tcl_AppendResult(interp, "unable to obtain image for systray icon", (char * ) NULL);
	return TCL_ERROR;
    }

    Tk_SizeOfImage(tk_image, & width, & height);
    if (width != 0 && height != 0) {
	icon = TkMacOSXGetNSImageFromTkImage(d, tk_icon,
					     width, height);
	[tk_item setImage: withImage: icon];
    }

    /*
     * Set the text for the tooltip.
     */

    NSString * tooltip = [NSString stringWithUTF8String: Tcl_GetString(argv[3])];
    if (tooltip == nil) {
	Tcl_AppendResult(interp, "unable to set tooltip for systray icon", (char * ) NULL);
	return TCL_ERROR;
    }

    [tk_item setText: withString: tooltip];

    /*
     * Set the proc for the callback.
     */

    callbackproc = Tcl_GetString(argv[4]);
    if (callbackproc == NULL) {
	Tcl_AppendResult(interp, "unable to get the callback for systray icon", (char * ) NULL);
	return TCL_ERROR;
    }

    return TCL_OK;
}

static int
MacSystrayModifyCmd(ClientData clientData, Tcl_Interp * interp,
		    int argc,
		    const char * argv[]) {

   if (argc < 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"systray modify object item?\"", (char * ) NULL);
	return TCL_ERROR;
    }

    char * modifyitem = Tcl_GetString(argv[2]);

    /*
     * Modify the icon.
     */

    if ((strcmp modifyitem, "image") == 0) {

	Tk_Window tkwin = Tk_MainWindow(interp);
	TkWindow winPtr = (TkWindow) tkwin;
	Display * d;
	d = winPtr -> display;
	NSImage * icon;

	char * tk_imagename = Tcl_GetString(argv[3]);
	Tk_Image tk_image;
	tk_image = Tk_GetImage(interp, tkwin, tk_imagename, NULL, NULL);
	if (tk_image == NULL) {
	    Tcl_AppendResult(interp, "unable to obtain image for systray icon", (char * ) NULL);
	    return TCL_ERROR;
	}

	Tk_SizeOfImage(tk_image, & width, & height);
	if (width != 0 && height != 0) {
	    icon = TkMacOSXGetNSImageFromTkImage(d, tk_icon,
						 width, height);
	    [tk_item setImage: withImage: icon];
	}
    }

    /*
     * Modify the text for the tooltip.
     */

    if ((strcmp modifyitem, "text") == 0) {

	NSString * tooltip = [NSString stringWithUTF8String: Tcl_GetString(argv[2])];
	if (tooltip == nil) {
	    Tcl_AppendResult(interp, "unable to set tooltip for systray icon", (char * ) NULL);
	    return TCL_ERROR;
	}

	[tk_item setText: withString: tooltip];
    }

    /*
     * Modify the proc for the callback.
     */

    if ((strcmp modifyitem, "callback") == 0) {
	callbackproc = Tcl_GetString(argv[2]);
	if (callbackproc == NULL) {
	    Tcl_AppendResult(interp, "unable to get the callback for systray icon", (char * ) NULL);
	    return TCL_ERROR;
	}
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * MacSystrayDestroy --
 *
 * 	Deletes icon and hidden window from display.
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
MacSystrayDestroy(ClientData clientData) {

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


static void SysNotifyDeleteCmd ( ClientData cd )
{
    (void) cd;
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


static int SysNotifyCmd(ClientData clientData, Tcl_Interp * interp,
			int argc, const char * argv[])
{
    (void)clientData;

    if (argc < 3) {
	Tcl_AppendResult(interp, "wrong # args,must be:",
			 argv[0], " title  message ", (char * ) NULL);
	return TCL_ERROR;
    }

    NSString *title = [NSString stringWithUTF8String: Tcl_GetString(argv[1])];
    NSString *message = [NSString stringWithUTF8String: Tcl_GetString(argv[2])];
    [notify_item postNotification: withTitle: title andDetail: message];

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
MacSystrayInit(Tcl_Interp * interp) {

    /*
     * Initialize TkStatusItem and TkNotifyItem.
     */

    if ([NSApp macOSVersion] < 101000) {
	Tcl_AppendResult(interp, "Statusitem icons not supported on versions of macOS lower than 10.10",  (char * ) NULL);
	return TCL_OK;
    }
    
    TkStatusItem *tk_item = [[TkStatusItem alloc] init];
    TkNotifyItem *notify_item = [[TkNotifyItem alloc] init];
    

    Tcl_CreateCommand(interp, "_systray", MacSystrayCmd, (ClientData)interp,
		      (Tcl_CmdDeleteProc *) MacSystrayDestroy);
    Tcl_CreateCommand(interp, "_sysnotify", SysNotifyCmd, NULL, (Tcl_CmdDeleteProc *) SysNotifyDeleteCmd);
    
    return TCL_OK;
}
     
/

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
