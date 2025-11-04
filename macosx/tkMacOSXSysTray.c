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



/*
 *----------------------------------------------------------------------
 *
 * MacSystrayDestroy --
 *
 *	Removes an intepreters icon from the status bar.
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
    void *clientData)
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
 *	Main command for creating, displaying, and removing icons from the
 *	status bar.
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
    Tcl_Size objc,
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
    Tcl_Size objc,
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

    /*
     * Using NSAppleScript API here allows us to use a single API rather
     * than multiple, some deprecated, API's, and also allows notifications
     * to work correctly without requiring Wish to be code-signed - an
     * undocumented but apparently consistent requirement. And by calling
     * NSAppleScript inline rather than shelling to out osascript,
     * Wish shows correctly as the calling app rather than Script Editor.
     */

    NSString *title = [NSString stringWithUTF8String: Tcl_GetString(objv[1])];
    NSString *message = [NSString stringWithUTF8String: Tcl_GetString(objv[2])];
    NSMutableString *notify = [NSMutableString new];
    [notify appendString: @"display notification "];
    [notify appendString:@"\""];
    [notify appendString:message];
    [notify appendString:@"\""];
    [notify appendString:@" with title \""];
    [notify appendString:title];
    [notify appendString:@"\""];
    NSAppleScript *scpt = [[[NSAppleScript alloc] initWithSource:notify] autorelease];
    NSDictionary *errorInfo;
    NSAppleEventDescriptor *result = [scpt executeAndReturnError:&errorInfo];
    NSString *info = [result stringValue];
    const char* output = [info UTF8String];

    Tcl_AppendResult(interp,
		     output,
		     NULL);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * MacSystrayInit --
 *
 *	Initialize this package and create script-level commands.
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

int
MacSystrayInit(Tcl_Interp *interp)
{

    /*
     * Initialize the TkStatusItem for this interpreter.
     */

    StatusItemInfo info = (StatusItemInfo) ckalloc(sizeof(StatusItemInfo));
    *info = 0;

    Tcl_CreateObjCommand2(interp, "::tk::systray::_systray", MacSystrayObjCmd, info,
	    MacSystrayDestroy);
    Tcl_CreateObjCommand2(interp, "::tk::sysnotify::_sysnotify", SysNotifyObjCmd, NULL, NULL);
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
