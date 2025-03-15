/*
 * tkMacOSXAccessibility.c --
 *
 *	This file implements the platform-native NSAccessibility API 
 *      for Tk on macOS.  
 *
 * Copyright © 2023 Apple Inc.
 * Copyright © 2024-2025 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */


#include <stdio.h>
#include <unistd.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <Cocoa/Cocoa.h>
#include <tkInt.h>
#include <tkMacOSXInt.h>
#include "tkMacOSXImage.h"
#include "tkMacOSXPrivate.h"

/* Data declarations and protoypes of functions used in this file. */
extern Tcl_HashTable *TkAccessibilityObject;
static NSPoint FlipY(NSPoint screenpoint, NSWindow *window);
void PostAccessibilityAnnouncement(NSString *message);
static int TkMacAccessibleObjCmd(TCL_UNUSED(void *),Tcl_Interp *ip,
			     int objc, Tcl_Obj *const objv[]);
static int EmitSelectionChanged(TCL_UNUSED(void *),Tcl_Interp *ip,
			     int objc, Tcl_Obj *const objv[]);
int TkMacOSXAccessibility_Init(Tcl_Interp * interp);
static int ActionEventProc(TCL_UNUSED(Tcl_Event *),
			   TCL_UNUSED(int));
char *callback_command;
const char *altlabel;

/* Map script-level roles to C roles. */
struct MacRoleMap {
    const char *tkrole;
    NSAccessibilityRole  macrole;
};

const struct MacRoleMap roleMap[] = {
    {"Button", @"NSAccessibilityButtonRole"},
    {"Canvas", @"NSAccessibilityUnknownRole"},
    {"Checkbutton", @"NSAccessibilityCheckBoxRole"},
    {"Combobox",  @"NSAccessibilityComboBoxRole"},
    {"Entry",  @"NSAccessibilityTextFieldRole"},
    {"Label", @"NSAccessibilityStaticTextRole"},
    {"Listbox", @"NSAccessibilityListRole"},
    {"Notebook", @"NSAccessibilityTabGroupRole"},
    {"Progressbar",  @"NSAccessibilityProgressIndicatorRole"},
    {"Radiobutton",  @"NSAccessibilityRadioButtonRole"},
    {"Scale", @"NSAccessibilitySliderRole"},
    {"Scrollbar",   @"NSAccessibilityScrollBarRole"},
    {"Spinbox", @"NSAccessibilityIncrementorRole"},
    {"Table",  @"NSAccessibilityTableRole"}, 
    {"Text",  @"NSAccessibilityTextAreaRole"},
    {"Tree",  @"NSAccessibilityTableRole"},
    {NULL, 0}
};

/*
 *----------------------------------------------------------------------
 *
 * FlipY --
 *
 *  Flips the y-coordinate for an NSRect in an NSWindow.
 *
 * Results:
 *	Y-coordinate is oriented to upper left-hand corner of screen.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static NSPoint FlipY(NSPoint screenpoint, NSWindow *window) {
    
    /*Convert screen coordinates to window base coordinates.*/
    NSPoint windowpoint= [window convertRectFromScreen:NSMakeRect(screenpoint.x, screenpoint.y, 0, 0)].origin;
    
    /*Flip the y-axis to make it top-left origin.*/
    CGFloat flipped = window.contentView.frame.size.height - windowpoint.y;
    
    return NSMakePoint(windowpoint.x, flipped);
}


/*
 *----------------------------------------------------------------------
 *
 * PostAccessibilityAnnouncement --
 *
 *  Customized accessibility message. 
 *
 * Results:
 *      Accessibilty API posts customized message.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */


void  PostAccessibilityAnnouncement( NSString *message) {
    NSDictionary *userInfo = @{ NSAccessibilityAnnouncementKey: message,
				NSAccessibilityPriorityKey : @(NSAccessibilityPriorityHigh),};
    NSAccessibilityPostNotificationWithUserInfo([NSApp mainWindow], 
						NSAccessibilityAnnouncementRequestedNotification,
						userInfo);
}
    
/*
 *
 * TkAccessibilityElement --
 *
 *  Primary interaction between Tk and NSAccessibility API. Accessibility is 
 *  linked to a specific Tk_Window, and traits are set and retrieved from hash
 *  tables. 
 *
 */

@implementation TkAccessibilityElement : NSAccessibilityElement

- (id) init {
    self = [super init];
    return self;
}

/*Foundational method. All actions derive from the role returned here.*/
- (NSAccessibilityRole)accessibilityRole {

    NSAccessibilityRole macrole = nil;

    Tk_Window win = self.tk_win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;

    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	return nil;
    }

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "role");
    if (!hPtr2) {
	return nil;
    }
    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    for (NSUInteger i = 0; i < sizeof(roleMap); i++) {
	if(strcmp(roleMap[i].tkrole, result) != 0) {
	    continue;
	}
	macrole = roleMap[i].macrole;
	return macrole;
    }
    return macrole;
}

- (NSString *)accessibilityLabel {

      NSAccessibilityRole role = self.accessibilityRole;
 
      /* Listbox.*/ 
      if ((role = NSAccessibilityListRole)) {
	  return self.accessibilityHint;
      }

    Tk_Window win = self.tk_win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;

  
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	return nil;
    }

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "description");
    if (!hPtr2) {
	return nil;
    }
    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    NSString  *macdescription = [NSString stringWithUTF8String:result];
    return macdescription;
}
  
-(id) accessibilityValue {

    NSAccessibilityRole role = self.accessibilityRole;
 
    /* Listbox and Treeview/Table. */
    
    if ((role = NSAccessibilityListRole) || (role = NSAccessibilityTableRole)) {
        
	Tk_Window win = self.tk_win;
	Tcl_HashEntry *hPtr, *hPtr2;
	Tcl_HashTable *AccessibleAttributes;


	hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
	if (!hPtr) {
	    return nil;
	}

	AccessibleAttributes = Tcl_GetHashValue(hPtr);
	hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "value");
	if (!hPtr2) {
	    return nil;
	}
	char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
	NSString  *value = [NSString stringWithUTF8String:result];
	return value;
    }

    return nil;
}

- (NSString*)accessibilityTitle {

    Tk_Window win = self.tk_win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
       
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	return nil;
    }

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "name");
    if (!hPtr2) {
	return nil;
    }

    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    NSString *mactitle = [NSString stringWithUTF8String:result];
    return mactitle;
}

- (NSString*)accessibilityHint {

    Tk_Window win = self.tk_win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
       
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	return nil;
    }

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "help");
    if (!hPtr2) {
	return nil;
    }

    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    NSString *machelp = [NSString stringWithUTF8String:result];
    return machelp;
} 


- (NSRect)accessibilityFrame {
    Tk_Window path;
    path = self.tk_win;
    TkWindow *winPtr = (TkWindow *)path;
    CGRect bounds, screenrect, windowframe;
    NSPoint flippedorigin;
    CGFloat adjustedx;
    NSWindow *w = TkMacOSXGetNSWindowForDrawable(winPtr->window);

    /* Check to see if Tk_Window exists. */
    if (!winPtr || winPtr->flags & TK_ALREADY_DEAD) {
	return CGRectZero;
    }

    /* Get CGRect points for Tk widget.*/
     TkMacOSXWinCGBounds(winPtr, &bounds);

    /*
     *  Convert CGRect coordinates to screen coordinates as required 
     *  by NSAccessibility API.
     *
     */
    screenrect = [w convertRectToScreen:bounds];

    /*
     * Convert to window coordinates and flip coordinates to
     * Y-down orientation.
     */
    
    flippedorigin = FlipY(screenrect.origin, w);	
    
    /* Calculate the desired x-offset for the accessibility frame.*/
    windowframe = w.frame;
    adjustedx = screenrect.origin.x - windowframe.origin.x;

    screenrect = CGRectMake(adjustedx, flippedorigin.y - screenrect.size.height,screenrect.size.width, screenrect.size.height);
 
    /* Finally,convert back to screen coordinates. */	
    screenrect = [w convertRectToScreen:screenrect];

    
    /* Force focus on Tk widget to align with VoiceOver cursor/focus. */
    //  [self forceFocus];
 
    return screenrect;
}

- (NSString*) accessibilityIdentifier {

    int x = arc4random_uniform(1000);
    NSNumber *id = [NSNumber numberWithInt: x];
    NSString *identifier = [id stringValue];
    return identifier;
}


- (id)accessibilityParent {
    
    Tk_Window win = self.tk_win;
    NSLog(@"%s", Tk_PathName(win));
    TkWindow *winPtr = (TkWindow *)win;
    if ((winPtr->window) == NULL) {
	NSLog(@"winPtr is NULL");
	return nil;
    } else {
	TKContentView *view = TkMacOSXGetRootControl(winPtr->window);
	if (!view || ![view isKindOfClass:[TKContentView class]]) {
	    NSLog(@"view is nil");
	    return nil;
	}
	self.parentView = view;
	return self.parentView;
    }
    return nil;
}

- (BOOL) accessibilityIsFocused {
    return [self.accessibilityParent window] == [NSApp keyWindow];
}

- (BOOL)becomeFirstResponder {
    return TRUE;
}

- (BOOL)isAccessibilityElement {
    return YES;
}


- (BOOL)accessibilityIsIgnored {
    return NO;
}

/*Various actions for buttons, scrollbars, spinners, and scales. */
- (void)accessibilityPerformAction:(NSAccessibilityActionName)action {
    if ([action isEqualToString:NSAccessibilityPressAction]) {
        [self accessibilityPerformPress];
    }
    if ([action isEqualToString:NSAccessibilityIncrementAction]) {
        [self accessibilityIncrementValue];
    }
    if ([action isEqualToString:NSAccessibilityDecrementAction]) {
        [self accessibilityDecrementValue];
    }
   
}


- (void)accessibilitySetValue:(id)value {

    NSAccessibilityRole role = self.accessibilityRole;

    //pending for future implementation
}

- (NSNumber *)accessibilityMinimumValue {
    
    TkMainInfo *info = TkGetMainInfoList();
    NSString *widgetName = [NSString stringWithUTF8String:Tk_PathName(self.tk_win)];
    NSString *commandString = [NSString stringWithFormat:@"%@ cget -from", widgetName];

    Tcl_Obj *commandObj = Tcl_NewStringObj([commandString UTF8String], -1);
    Tcl_Obj *resultObj;

    if (Tcl_EvalObjEx(info->interp, commandObj, TCL_EVAL_GLOBAL) == TCL_OK) {
        resultObj = Tcl_GetObjResult(info->interp);
        double value = strtod(Tcl_GetString(resultObj), NULL);
        return @(value);
    }
    return nil;
}

- (NSNumber *)accessibilityMaximumValue {
    
    TkMainInfo *info = TkGetMainInfoList();
    NSString *widgetName = [NSString stringWithUTF8String:Tk_PathName(self.tk_win)];
    NSString *commandString = [NSString stringWithFormat:@"%@ cget -to", widgetName];

    Tcl_Obj *commandObj = Tcl_NewStringObj([commandString UTF8String], -1);
    Tcl_Obj *resultObj;

    if (Tcl_EvalObjEx(info->interp, commandObj, TCL_EVAL_GLOBAL) == TCL_OK) {
        resultObj = Tcl_GetObjResult(info->interp);
        double value = strtod(Tcl_GetString(resultObj), NULL);
        return @(value);
    }
    return nil;
}


- (void)accessibilityIncrementValue {
    
    NSAccessibilityRole role = self.accessibilityRole;
   
    //pending for future implementation
}

- (void)accessibilityDecrementValue {

    NSAccessibilityRole role = self.accessibilityRole;
    //pending for future implementation
}


/*Action for button roles.*/
- (BOOL)accessibilityPerformPress {
 
    Tk_Window win = self.tk_win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
    Tcl_Event *event; 

    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	return NO;
    }

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "action");
    if (!hPtr2) {
	return NO;
    }

    char *action= Tcl_GetString(Tcl_GetHashValue(hPtr2));
    callback_command = action;
    event = (Tcl_Event *)ckalloc(sizeof(Tcl_Event));
    event->proc = ActionEventProc;
    Tcl_QueueEvent((Tcl_Event *)event, TCL_QUEUE_TAIL);
    return YES;
}

- (void) forceFocus {

    TkMainInfo *info = TkGetMainInfoList();
    NSString *widgetName = [NSString stringWithUTF8String:Tk_PathName(self.tk_win)];
    NSString *commandString = [NSString stringWithFormat:@"::tk::accessible::_forceTkFocus %@", widgetName];
    Tcl_Obj *commandObj = Tcl_NewStringObj([commandString UTF8String], -1);
    Tcl_Obj *resultObj;
    if (Tcl_EvalObjEx(info->interp, commandObj, TCL_EVAL_GLOBAL) == TCL_OK) {
        resultObj = Tcl_GetObjResult(info->interp);
    }
}

@end


/*
 * Event proc which calls the ActionEventProc procedure.
 */

static int
ActionEventProc(TCL_UNUSED(Tcl_Event *),
    TCL_UNUSED(int))
{
    TkMainInfo *info = TkGetMainInfoList();
    Tcl_GlobalEval(info->interp, callback_command);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacAccessibleObjCmd --
 *
 *	Main command for adding and managing accessibility objects to Tk
 *      widgets on macOS using the NSAccessibilty API.
 *
 * Results:
 *
 *      A standard Tcl result.
 *
 * Side effects:
 *
 *	Tk widgets are now accessible to screen readers.
 *
 *----------------------------------------------------------------------
 */

static int
TkMacAccessibleObjCmd(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
{	
    if (objc < 2) {
	Tcl_WrongNumArgs(ip, 1, objv, "window?");
	return TCL_ERROR;
    }
    Tk_Window path;
   
    path = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
    if (path == NULL) {
	Tk_MakeWindowExist(path);
    }

    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    TkAccessibilityElement *widget =  [[TkAccessibilityElement alloc] init];
    widget.tk_win = path;
    [widget.accessibilityParent accessibilityAddChildElement: widget];
    NSAccessibilityPostNotification(widget.parentView, NSAccessibilityLayoutChangedNotification);
   
    [pool drain];
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * EmitSelectionChanged --
 *
 * Accessibility system notification when selection changed.
 *
 * Results:
 *	Accessibility system is made aware when a selection is changed. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
EmitSelectionChanged(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
{	
    if (objc < 2) {
	Tcl_WrongNumArgs(ip, 1, objv, "window?");
	return TCL_ERROR;
    }
    Tk_Window path;
   
    path = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
    if (path == NULL) {
	Tk_MakeWindowExist(path);
    }

    TkAccessibilityElement *widget =  [[TkAccessibilityElement alloc] init];
    widget.tk_win = path;
    NSAccessibilityRole role = widget.accessibilityRole;


    if ((role = NSAccessibilityListRole)) {

	/*
	 * We access listbox data through the <<ListboxSelect>> event at the
	 * script level and send notifications from the C level to update the
	 * value read by VoiceOver of the widget based on the listbox data's
	 * selected value. The accessibility design is tightly tied to a
	 * Tk_Window and mapping this API to elements that are not actual
	 * windows, like listbox rows, introduces too much complexity.
	 */

	NSAccessibilityPostNotification(widget, NSAccessibilityValueChangedNotification);
	PostAccessibilityAnnouncement(widget.accessibilityValue);
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXAccessibility_Init --
 *
 *	Initializes the accessibility module.
 *
 * Results:
 *
 *      A standard Tcl result.
 *
 * Side effects:
 *
 *	Accessibility module is now activated.
 *
 *----------------------------------------------------------------------
 */

int TkMacOSXAccessibility_Init(Tcl_Interp * interp) {
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", TkMacAccessibleObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", EmitSelectionChanged, NULL, NULL);
    [pool release];
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
