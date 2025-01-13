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


extern Tcl_HashTable *TkAccessibilityObject;
NSPoint ConvertScreenToWindowCoordinates(NSPoint screenpoint, NSWindow *window);

NSPoint ConvertScreenToWindowCoordinates(NSPoint screenpoint, NSWindow *window) {
    
    /*Convert screen coordinates to window base coordinates.*/
    NSPoint windowpoint= [window convertRectFromScreen:NSMakeRect(screenpoint.x, screenpoint.y, 0, 0)].origin;
    
    /*Flip the y-axis to make it top-left origin.*/
    CGFloat flipped = window.contentView.frame.size.height - windowpoint.y;
    
    return NSMakePoint(windowpoint.x, flipped);
}


@implementation TkAccessibilityElement : NSAccessibilityElement

- (id) init {
    self = [super init];
    return self;
}

- (NSString *)accessibilityLabel {
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

- (BOOL)accessibilityPerformPress {
    // [super performPress];
    NSLog(@"press");
    return YES;
}

- (NSAccessibilityRole)accessibilityRole {

    NSAccessibilityRole macrole = nil;
    int i;

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
	NSLog(@"No label found.");
	return nil;
    }
    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    for (i = 0; roleMap[i].tkrole != NULL; i++) {
	if(strcmp(roleMap[i].tkrole, result) == 0) {
	    macrole = roleMap[i].macrole;
	}
	return macrole;
    }
    return macrole;
}
    

- (BOOL)isAccessibilityElement {
    return YES;
}


- (NSRect)accessibilityFrame {
    Tk_Window path;
    path = self.tk_win;
    TkWindow *winPtr = (TkWindow *)path;
    CGRect bounds, screenrect, windowframe;
    NSPoint flippedorigin, vieworigin;
    CGFloat titlebarheight, adjustedx;
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
     * Convert to window coordinates and flip coordinates to Y-down orientation.
     * Calculate height of titlebar.
     *
     */
    
    flippedorigin = ConvertScreenToWindowCoordinates(screenrect.origin, w);
    titlebarheight = w.frame.size.height - [w contentRectForFrameRect: w.frame].size.height;
    
    /* Calculate the desired x-offset for the accessibility frame.*/
    windowframe=w.frame;
    adjustedx = screenrect.origin.x - windowframe.origin.x;

    screenrect = CGRectMake(adjustedx, flippedorigin.y - titlebarheight, screenrect.size.width, screenrect.size.height);

    /*Finally,convert back to screen coordinates.*/	
     screenrect = [w convertRectToScreen:screenrect];

    return screenrect;
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


@end


/*
 *----------------------------------------------------------------------
 *
 * TkMacAccessibleObjCmd --
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
    

    [pool drain];
    return TCL_OK;

  
}


int TkMacOSXAccessibility_Init(Tcl_Interp * interp) {
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", TkMacAccessibleObjCmd, NULL, NULL);
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
