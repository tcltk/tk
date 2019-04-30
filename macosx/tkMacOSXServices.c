/*
 * tkMacOSXServices.c --
 *
 *	This file allows the integration of Tk and the Cocoa NSServices API.
 *
 * Copyright (c) 2010-2019 Kevin Walzer/WordTech Communications LLC.
 * Copyright (c) 2010 Adrian Robert.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include <CoreServices/CoreServices.h>
#include <tkInt.h>
#include <tkMacOSXInt.h>

static Tcl_Interp *ServicesInterp;

/*
 * These two assist with asynchronous Tcl proc calling.
 */

typedef struct Services_Event {
    Tcl_Event header;
    char script[50000];
} Services_Event;

int ServicesEventProc(
    Tcl_Event *event,
    int flags)
{
    Tcl_GlobalEval(ServicesInterp, ((Services_Event *)event)->script);
    return 1;
}

/*
 * Class declarations for TkService class.
 */

@interface TkService : NSView {

}

+ (void) initialize;
- (void)provideService:(NSPasteboard *)pboard userData:(NSString *)data error:(NSString **)error;
- (id)validRequestorForSendType:(NSString *)sendType returnType:(NSString *)returnType;
- (BOOL)writeSelectionToPasteboard:(NSPasteboard *)pboard  types:(NSArray *)types;

@end

/*
 * Class methods.
 */

@implementation TkService

+ (void) initialize {
    NSArray *sendTypes = [NSArray arrayWithObjects:NSStringPboardType, nil];

    [NSApp registerServicesMenuSendTypes:sendTypes returnTypes:nil];
    NSUpdateDynamicServices();
    return;
}


- (id)validRequestorForSendType:(NSString *)sendType
		     returnType:(NSString *)returnType
{
    if ([sendType isEqual:NSStringPboardType]) {
	return self;
    }
    return [super validRequestorForSendType:sendType returnType:returnType];
}

/*
 * Make sure the view accepts events.
 */

- (BOOL)acceptsFirstResponder
{
    return YES;
}
- (BOOL)becomeFirstResponder
{
    return YES;
}

/*
 * Get selected text, copy to pasteboard.
 */

- (BOOL)writeSelectionToPasteboard:(NSPasteboard *)pboard
			     types:(NSArray *)types
{
    NSArray *typesDeclared;
    if ([types containsObject:NSStringPboardType] == NO) {
	return NO;
    }

    Tcl_Eval(ServicesInterp,"selection get");
    char *copystring;
    copystring = Tcl_GetString(Tcl_GetObjResult(ServicesInterp));

    NSString *writestring = [NSString stringWithUTF8String:copystring];
    typesDeclared = [NSArray arrayWithObject:NSStringPboardType];
    [pboard declareTypes:typesDeclared owner:nil];
    return [pboard setString:writestring forType:NSStringPboardType];
}


/*
 * This is the method that actually calls the Tk service; this is the method
 * that must be defined in info.plist
 */

- (void)provideService:(NSPasteboard *)pboard
	      userData:(NSString *)data
		 error:(NSString **)error
{
    NSString *pboardString;
    NSArray *types = [pboard types];
    Services_Event *event;

    /*
     * Get string from private pasteboard, write to general pasteboard to make
     * available to Tcl service.
     */

    if ([types containsObject:NSStringPboardType] &&
	(pboardString = [pboard stringForType:NSStringPboardType])) {

	NSPasteboard *generalpasteboard = [NSPasteboard generalPasteboard];
	[generalpasteboard declareTypes:[NSArray
	     arrayWithObjects:NSStringPboardType, nil] owner:nil];
	[generalpasteboard setString:pboardString forType:NSStringPboardType];
	event = (Services_Event *)ckalloc(sizeof(Services_Event));
	event->header.proc = ServicesEventProc;
	strcpy(event->script, "::tk::mac::PerformService");
	Tcl_QueueEvent((Tcl_Event *)event, TCL_QUEUE_TAIL);
    } else {
	return;
    }
    return;
}

@end


/*
 * Register a specific widget to access the Services menu.
 */

int TkMacOSXRegisterServiceWidgetObjCmd (
					 ClientData cd,
					 Tcl_Interp *ip,
					 int objc,
					 Tcl_Obj *CONST objv[])
{

    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    /*
     * Need proper number of args.
     */

    if(objc != 2) {
	Tcl_WrongNumArgs(ip, 1, objv, "path?");
	return TCL_ERROR;
    }

    /*
     * Get the object that holds this Tk Window...
     */

    Rect bounds;
    NSRect frame;
    Tk_Window path = Tk_NameToWindow(ip, Tcl_GetString(objv[1]),
				     Tk_MainWindow(ip));

    if (path == NULL) {
	return TCL_ERROR;
    }

    Tk_MakeWindowExist(path);
    Tk_MapWindow(path);
    Drawable d = Tk_WindowId(path);

    /*
     * Get NSView from Tk window and add subview.
     */

    TkService *serviceview = [[TkService alloc] init];
    NSView *view = TkMacOSXGetRootControl(d);
    if ([serviceview superview] != view) {
	[view addSubview:serviceview];
    }
    TkMacOSXWinBounds((TkWindow*)path, &bounds);

    /*
     * Hack to make sure subview is set to take up entire geometry of window.
     */

    frame = NSMakeRect(bounds.left, bounds.top, 100000, 100000);
    frame.origin.y = 0;
    if (!NSEqualRects(frame, [serviceview frame])) {
	[serviceview setFrame:frame];
    }
    [serviceview release];
    [pool release];
    return TCL_OK;
}

/*
 * Initalize the package in the tcl interpreter, create tcl commands.
 */

int Tk_MacOSXServices_Init(
    Tcl_Interp *interp)
{
    /*
     * Set up an autorelease pool.
     */

    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    /*
     * Initialize instance of TclServices to provide service functionality.
     */

    TkService *service = [[TkService alloc] init];
    ServicesInterp = interp;
    [NSApp setServicesProvider:service];
    [pool drain];
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
