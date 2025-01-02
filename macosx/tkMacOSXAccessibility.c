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
}
    

- (BOOL)isAccessibilityElement {
    NSLog(@"yes is accessible");
    return YES;
}

 - (NSRect)accessibilityFrame {
   Tk_Window path;
    Drawable d;
    unsigned int width, height, x, y;
    
    path = self.tk_win;
    d = Tk_WindowId(path);
    width = Tk_Width(path);
    height = Tk_Height(path);
    x=Tk_X(path);
    y=Tk_Y(path);
    Tk_Window win = self.tk_win;
    TkWindow *winPtr = (TkWindow *)win;
    NSWindow *w = nil;
    w = TkMacOSXGetNSWindowForDrawable(winPtr->window);
    NSRect windowFrame = [w frame];
    NSRect elementFrame = NSMakeRect(windowFrame.origin.x + x,
				     windowFrame.origin.y + y,
				     width,
				     height);
    return elementFrame;

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

- (void) updateAccessibilityElementFrame {

    Tk_Window path;
    Drawable d;
    unsigned int width, height, x, y;
    
    path = self.tk_win;
    d = Tk_WindowId(path);
    width = Tk_Width(path);
    height = Tk_Height(path);
    x=Tk_X(path);
    y=Tk_Y(path);
    Tk_Window win = self.tk_win;
    TkWindow *winPtr = (TkWindow *)win;
    NSWindow *w = nil;
    w = TkMacOSXGetNSWindowForDrawable(winPtr->window);
    NSRect windowFrame = [w frame];
    NSRect elementFrame = NSMakeRect(windowFrame.origin.x + x,
				     windowFrame.origin.y + y,
				     width,
				     height);
    self.accessibilityFrame = elementFrame;
    [self setAccessibilityFrameInParentSpace:elementFrame];

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

    [widget updateAccessibilityElementFrame];
    
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
