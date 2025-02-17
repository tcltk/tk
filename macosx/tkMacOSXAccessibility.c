/*
 * tkMacOSXAccessibility.c --
 *
 *	This file implements the platform-native NSAccessibility API 
 *      for Tk on macOS.  
 *
 * Copyright © 2023 Apple Inc.
 * Copyright © 2024 Kevin Walzer/WordTech Communications LLC.
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
NSArray *TclListToNSArray(char *listdata);
CGRect GetListBBox (Tk_Window win, Tcl_Size index);
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
    {"Frame", @"NSAccessibilityGroupRole"},
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
 * TclListToNSArray --
 *
 * Converts a Tcl list to an NSArray.
 *
 * Results:
 *	Tcl list data converted to an NSArray for use in 
 *      accessibility operations.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */


NSArray *TclListToNSArray (char *listdata) {

    TkMainInfo *info = TkGetMainInfoList();
    Tcl_Interp *interp = info->interp;
  

    Tcl_Obj  *listObj, *listObjItem;
    Tcl_Size listLength, i, newLength;
    char **elemPtrs;


    /* Extract elements from the Tcl list. */
    if (Tcl_SplitList(interp, listdata, &listLength, &elemPtrs) != TCL_OK) {
       	NSLog(@"Unable to convert list data.");
	return nil; 
    }

    /* Create empty list object. */

    listObj = Tcl_NewListObj(listLength, NULL);

    for (i = 0; i < listLength; i++) {
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj(elemPtrs[i], -1));
    }

    Tcl_Free((char *)elemPtrs);

    /* Get the number of elements in the list. */
    if (Tcl_ListObjLength(interp, listObj, &newLength) != TCL_OK) {
	NSLog(@"Error getting list length.");
        return nil;
    }

    /* Create an NSMutableArray to store the converted elements. */
    NSMutableArray *array = [[NSMutableArray alloc] initWithCapacity:newLength];
    for (i = 0; i < newLength; i++) {
	if (Tcl_ListObjIndex(interp, listObj, i, &listObjItem) != TCL_OK) {
	    NSLog(@"Unable to get list item.");
	    continue;
	}
	const char *liststring = Tcl_GetString(listObjItem); 
	NSString *arraystring = [NSString stringWithUTF8String:liststring];
	NSLog(@"adding new string: %s", liststring);
	    
	[array addObject:arraystring];
    }
    return [array copy]; 
}

/*
 *----------------------------------------------------------------------
 *
 * GetListBBox --
 *
 * Converts a Tcl listbox bounding box into a CGRect.
 *
 * Results:
 *	Bbox converted to a CGRect for use in accessibility operations.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */


CGRect GetListBBox (Tk_Window win, Tcl_Size index) {

    Tcl_Obj *intObj = Tcl_NewIntObj(index);

    /* Get item bounding box in listbox-relative coordinates.*/
    if (Tcl_VarEval(Tk_Interp(win), Tk_PathName(win), " bbox ", Tcl_GetString(intObj), NULL) != TCL_OK) {
	NSLog(@"Unable to parse bounding box");
	return CGRectZero;
    }
  
    int x, y, width, height;
    sscanf(Tcl_GetStringResult(Tk_Interp(win)), "%d %d %d %d", &x, &y, &width, &height);

    /* Get listbox position in window.*/
    int win_x = Tk_X(win);
    int win_y = Tk_Y(win);

    /* Convert to screen coordinates.*/
    CGRect screenFrame = [[NSScreen mainScreen] frame];
    CGRect rect = NSMakeRect(win_x + x, screenFrame.size.height - (win_y + y + height), width, height);
    
    return rect;
}
    
    
/*
 *----------------------------------------------------------------------
 *
 * TkAccessibilityElement class --
 *
 *  Primary interaction between Tk and NSAccessibility API.
 *
 * Results:
 *	Tk widgets are now accessible to screen readers on macOS.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

@implementation TkAccessibilityElement : NSAccessibilityElement

- (id) init {
    self = [super init];
    return self;
}

- (NSString *)accessibilityLabel {

    NSAccessibilityRole role = self.accessibilityRole;

    /*
      Hard code a label for Tk listbox items because they are not
      Tk windows and thus cannot be stored in the hash table.
    */
       
    if ((role = NSAccessibilityRowRole)) {
	NSString *label = [NSString stringWithUTF8String:altlabel];
	return label;
    }
        
    Tk_Window win = self.tk_win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;


    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	NSLog(@"No table found. You must set the accessibility role first.");
	return nil;
    }

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "description");
    if (!hPtr2) {
	NSLog(@"No label found.");
	return nil;
    }
    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    NSString  *macdescription = [NSString stringWithUTF8String:result];
    return macdescription;
  
}
  
-(id) accessibilityValue {
   return nil;
}

/*Action for button roles.*/
- (BOOL)accessibilityPerformPress {
 
    Tk_Window win = self.tk_win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
    Tcl_Event *event; 

    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	NSLog(@"No table found. You must set the accessibility role first.");
	return NO;
    }

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "action");
    if (!hPtr2) {
	NSLog(@"No action found.");
	return NO;
    }

    char *action= Tcl_GetString(Tcl_GetHashValue(hPtr2));
    callback_command = action;
    event = (Tcl_Event *)ckalloc(sizeof(Tcl_Event));
    event->proc = ActionEventProc;
    Tcl_QueueEvent((Tcl_Event *)event, TCL_QUEUE_TAIL);
    return YES;
}

- (NSAccessibilityRole)accessibilityRole {

    NSAccessibilityRole macrole = nil;

    Tk_Window win = self.tk_win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;

    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	NSLog(@"No table found. You must set the accessibility role first.");
	return nil;
    }

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "role");
    if (!hPtr2) {
	NSLog(@"No role found.");
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

- (NSString*)accessibilityTitle {

    Tk_Window win = self.tk_win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;

    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	NSLog(@"No table found. You must set the accessibility role first.");
	return nil;
    }

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "name");
    if (!hPtr2) {
	NSLog(@"No title found.");
	return nil;
    }

    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    NSString *mactitle = [NSString stringWithUTF8String:result];
    return mactitle;
} 

- (BOOL)isAccessibilityElement {
    return YES;
}


- (NSRect)accessibilityFrame {
    Tk_Window path;
    path = self.tk_win;
    TkWindow *winPtr = (TkWindow *)path;
    CGRect bounds, screenrect, windowframe;
    NSPoint flippedorigin;
    CGFloat adjustedx;
    NSWindow *w = TkMacOSXGetNSWindowForDrawable(winPtr->window);

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
    TkWindow *winPtr = (TkWindow *)win;
    NSWindow *w = nil;
    w = TkMacOSXGetNSWindowForDrawable(winPtr->window);
    TKContentView *view = [w contentView];
    self.parentView = view;
    return self.parentView;
}

- (BOOL)becomeFirstResponder {
    return TRUE;
}

- (BOOL)accessibilityIsIgnored {
    return NO;
}

/* NSAccessibilityTableRole methods. */

- (NSArray *)  accessibilityColumnHeaderUIElements {
  return @[]; // Modify as needed if there are column headers  return nil;
}

- (NSArray *) accessibilityRowHeaderUIElements {
    return @[]; // Modify as needed if there are column headers  return nil;
}

- (NSArray<id<NSAccessibilityRow>> *)  accessibilityRows {

    NSAccessibilityRole role = self.accessibilityRole;
    
    if ((role = NSAccessibilityListRole)) {
	
	/*Get items in the Tk listbox. */
	TkMainInfo *info = TkGetMainInfoList();
	char *win = Tk_PathName(self.tk_win);
	Tcl_VarEval(info->interp, win,  " get 0 end", NULL);  
	char *data = Tcl_GetString(Tcl_GetObjResult(info->interp));
	
	/* 
	 * Extract elements from the Tcl list 
	 * and build array of accessibility row items.
	 */
	 
	NSArray *listrows = TclListToNSArray(data);
	NSMutableArray *rows = [listrows mutableCopy];
	    
	    /*
	     * Each row must be set up as an individual accessibility
	     * element.
	     */
	    
	    for (NSUInteger i=0; i < [rows count]; i++) {
		role = NSAccessibilityRowRole;
		TkAccessibilityElement *rowObject =  [[TkAccessibilityElement alloc] init];
		rowObject.accessibilityElement = YES;
		rowObject.accessibilityParent = self;
		rowObject.accessibilityFrame = GetListBBox(self.tk_win, i);

		/*
		 * Get row text and pass to value that is called
		 * directly in accessibilityLabel.
		 */
		
		NSString *string = [rows objectAtIndex:i];
		altlabel = [string UTF8String];
	
		NSLog(@"the label is %@", rowObject.accessibilityLabel);
		[rows addObject: rowObject];
	    }
	return [rows copy];
    }
}

- (NSArray *)accessibilityChildren {
    
 	NSLog(@"Accessing children.");
	return [self accessibilityRows];
}

- (NSInteger)accessibilityRowCount {
    return [[self accessibilityRows] count];
}

- (NSArray *)accessibilitySelectedRows {

    NSAccessibilityRole role = self.accessibilityRole;
    
    if ((role = NSAccessibilityListRole)) {

	/*Get selected rows in the Tk listbox. */
	TkMainInfo *info = TkGetMainInfoList();
	char *win = Tk_PathName(self.tk_win);
	Tcl_VarEval(info->interp, win,  " curselection ", NULL);  
	char *data = Tcl_GetString(Tcl_GetObjResult(info->interp));
	NSArray *selected  = TclListToNSArray(data);
	NSMutableArray *selectedRows = [selected mutableCopy];
	
	/*
	 * Each row must be set up as an individual accessibility
	 * element.
	 */
	    
	for (NSUInteger i=0; i < [selectedRows count]; i++) {
	    role = NSAccessibilityRowRole;
	    TkAccessibilityElement *selectedObject =  [[TkAccessibilityElement alloc] init];
	    selectedObject.accessibilityElement = YES;
	    selectedObject.accessibilityParent = self;
	    selectedObject.accessibilityFrame = GetListBBox(self.tk_win, i);

	    /*
	     * Get row text and pass to value that is called
	     * directly in accessibilityLabel.
	     */
		
	    NSString *string = [selectedRows objectAtIndex:i];
	    altlabel = [string UTF8String];
	
	    NSLog(@"the label is %@", selectedObject.accessibilityLabel);
	    [selectedRows addObject: selectedObject];
	}
	return selectedRows;

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

    if ((role = NSAccessibilityRowRole)) {
	NSAccessibilityPostNotification(widget, NSAccessibilitySelectedRowsChangedNotification);
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
