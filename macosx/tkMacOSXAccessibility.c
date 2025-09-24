/*
 * tkMacOSXAccessibility.c --
 *
 *	This file implements the platform-native NSAccessibility API 
 *  for Tk on macOS.  
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
#include <AppKit/NSAccessibility.h>


/* Data declarations and protoypes of functions used in this file. */
extern Tcl_HashTable *TkAccessibilityObject;
static NSPoint FlipY(NSPoint screenpoint, NSWindow *window);
void PostAccessibilityAnnouncement(NSString *message);
static void InitAccessibilityHashTables(void);
void TkAccessibility_LinkWindowToElement(Tk_Window tkwin, TkAccessibilityElement *element);
TkAccessibilityElement *TkAccessibility_GetElementForWindow(Tk_Window tkwin);
Tk_Window TkAccessibility_GetWindowForElement(TkAccessibilityElement *element);
void TkAccessibility_UnlinkWindowAndElement(Tk_Window tkwin, TkAccessibilityElement *element);
void TkAccessibility_CleanupHashTables(void);
static int TkMacOSXAccessibleObjCmd(ClientData clientData,Tcl_Interp *ip, int objc, Tcl_Obj *const objv[]);
static void TkMacOSXAccessibility_DestroyHandler(ClientData clientData, XEvent *eventPtr);
void TkMacOSXAccessibility_RegisterForCleanup(Tk_Window tkwin, void *accessibilityElement);
int IsVoiceOverRunning(ClientData clientData, Tcl_Interp *ip, int objc, Tcl_Obj *const objv[]);
int TkMacOSXAccessibility_Init(Tcl_Interp * interp);
static int EmitSelectionChanged(ClientData clientData,Tcl_Interp *ip, int objc, Tcl_Obj *const objv[]);
int TkMacOSXAccessibility_Init(Tcl_Interp * interp);
static int ActionEventProc(Tcl_Event *ev, int flags);

char *callback_command;
static Tcl_HashTable *TkWindowToElementTable = NULL;
static Tcl_HashTable *ElementToTkWindowTable = NULL;
static int accessibilityTablesInitialized = 0;

/* 
 * Map script-level roles to AX role strings, which are drawn from the macOS
 * Accessibility API used by client-side apps. NSAccessibilityRole constants
 * will not compile in C code, but these are equivalent. The NSStrings,
 * e.g. NSAccessibilityListRole, can be used for comparison in methods. 
 */

struct MacRoleMap {
    const char *tkrole;           /* Tk role string */
    NSAccessibilityRole macrole;  /* AX role constant (NSString *) */
};

const struct MacRoleMap roleMap[] = {
    {"Button",        @"AXButton"},
    {"Canvas",        @"AXScrollArea"},
    {"Checkbutton",   @"AXCheckBox"},
    {"Combobox",      @"AXComboBox"},
    {"Entry",         @"AXTextField"},
    {"Label",         @"AXStaticText"},
    {"Listbox",       @"AXGroup"},
    {"Notebook",      @"AXTabGroup"},
    {"Progressbar",   @"AXProgressIndicator"},
    {"Radiobutton",   @"AXRadioButton"},
    {"Scale",         @"AXSlider"},
    {"Scrollbar",     @"AXScrollBar"},
    {"Spinbox",       @"AXIncrementor"},
    {"Table",         @"AXGroup"},
    {"Text",          @"AXTextArea"},
    {"Tree",          @"AXGroup"},
    {NULL,            nil}
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

static NSPoint FlipY(NSPoint screenpoint, NSWindow *window)
{
    
    /* Convert screen coordinates to window base coordinates. */
    NSPoint windowpoint= [window convertRectFromScreen:NSMakeRect(screenpoint.x, screenpoint.y, 0, 0)].origin;
    
    /* Flip the y-axis to make it top-left origin. */
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


void PostAccessibilityAnnouncement(NSString *message)
{
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

- (id) init
{
    self = [super init];
    return self;
}

/* Foundational method. All actions derive from the role returned here. */
- (NSAccessibilityRole) accessibilityRole
{
    Tk_Window win = self.tk_win;
    if (!win) {
        return nil;
    }

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
        return nil;
    }

    Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
    Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "role");
    if (!hPtr2) {
        return nil;
    }

    const char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    if (!result) {
        return nil;
    }

    for (NSUInteger i = 0; roleMap[i].tkrole != NULL; i++) {
        if (strcmp(roleMap[i].tkrole, result) == 0) {
            return roleMap[i].macrole;
        }
    }

    return nil;
}


- (NSString *) accessibilityLabel
{

    NSAccessibilityRole role = self.accessibilityRole;

    /* 
     * Special handling for listboxes, trees and tables. They are defined 
     * as NSAccessibilityGroupRole to suppress the "empty content" phrasing
     * that comes with tables/trees/listboxes without an array of child widgets.
     * We are using the accessibilityChildren array only for actual widgets 
     * that are children of toplevels, not virtual elements such as listbox 
     * rows. We are using script-level bindings and a custom announcement
     * in VoiceOver to notfiy the user of the selected data. Here,
     * we include the number of elements in the row and a hint on navigation as 
     * the accessibility label for these roles. 
     */
    if ([role isEqualToString:NSAccessibilityGroupRole]) {
	NSInteger rowCount = [self accessibilityRowCount];
	NSString *count = [NSString stringWithFormat:@"Table with %ld items. ", (long)rowCount];
	NSString *interact = self.accessibilityHint;
	NSString *groupLabel = [NSString stringWithFormat:@"%@%@", count, interact];
	return groupLabel;				
    }

    /* Retrieve the label for all other widget roles. */
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


- (id)accessibilityValue
{
    NSAccessibilityRole role = self.accessibilityRole;
    Tk_Window win = self.tk_win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;

    if (!win) {
        return nil;
    }

    hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
        return nil;
    }

    AccessibleAttributes = Tcl_GetHashValue(hPtr);

    /* 
     * Special handling for checkbuttons and radio buttons.
     */
    if ([role isEqualToString:NSAccessibilityCheckBoxRole] ||
        [role isEqualToString:NSAccessibilityRadioButtonRole]) {

        int stateValue = 0; /* Default off. */
        Tcl_Interp *interp = Tk_Interp(win);

        if (interp) {
            const char *path = Tk_PathName(win);

            /* Get the variable name bound to this widget. */
            Tcl_Obj *varCmd = Tcl_ObjPrintf("%s cget -variable", path);
            Tcl_IncrRefCount(varCmd);
            const char *varName = NULL;

            if (Tcl_EvalObjEx(interp, varCmd, TCL_EVAL_GLOBAL) == TCL_OK) {
                varName = Tcl_GetStringResult(interp);
            }
            Tcl_DecrRefCount(varCmd);

            if (varName && *varName) {
                const char *varVal = Tcl_GetVar(interp, varName, TCL_GLOBAL_ONLY);

                if ([role isEqualToString:NSAccessibilityCheckBoxRole]) {
                    if (varVal && strcmp(varVal, "1") == 0) {
                        stateValue = 1;
                    }
                } else {
                    /* Radiobutton: need to compare with -value. */
                    Tcl_Obj *valueCmd = Tcl_ObjPrintf("%s cget -value", path);
                    Tcl_IncrRefCount(valueCmd);
                    if (Tcl_EvalObjEx(interp, valueCmd, TCL_EVAL_GLOBAL) == TCL_OK) {
                        const char *rbValue = Tcl_GetStringResult(interp);
                        if (varVal && rbValue && strcmp(varVal, rbValue) == 0) {
                            stateValue = 1;
                        }
                    }
                    Tcl_DecrRefCount(valueCmd);
                }
            }
        }

        /* Update the Tcl hash table for caching. */
        char buf[2];
        snprintf(buf, sizeof(buf), "%d", stateValue);
        int newEntry;
        hPtr2 = Tcl_CreateHashEntry(AccessibleAttributes, "value", &newEntry);
        Tcl_Obj *valObj = Tcl_NewStringObj(buf, -1);
        Tcl_IncrRefCount(valObj);
        Tcl_SetHashValue(hPtr2, valObj);

        /* Return NSNumber for VoiceOver. */
        NSControlStateValue cocoaValue =
            (stateValue == 1) ? NSControlStateValueOn : NSControlStateValueOff;

        /* Notify VoiceOver that value changed. */
        NSAccessibilityPostNotification(self, NSAccessibilityValueChangedNotification);

        return [NSNumber numberWithInteger:cocoaValue];
    }

    /* Fallback: return cached string value for other widget types */
    hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "value");
    if (!hPtr2) {
        return nil;
    }

    Tcl_Obj *valObj = (Tcl_Obj *)Tcl_GetHashValue(hPtr2);
    const char *result = Tcl_GetString(valObj);
    return [NSString stringWithUTF8String:result];
}


- (NSString*) accessibilityTitle
{

    NSAccessibilityRole role = self.accessibilityRole;

    /*
     * Return the value data for labels and text widgets as the accessibility
     * title, because VoiceOver does not seem to pick up the accessibiility
     * value for these widgets otherwise.
    */ 
    if ([role isEqualToString:NSAccessibilityStaticTextRole] || [role isEqualToString:NSAccessibilityTextAreaRole]) {
	return self.accessibilityValue;
    }
    
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

- (NSString*) accessibilityHint
{

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


- (NSRect) accessibilityFrame
{
    
    Tk_Window path;
    path = self.tk_win;
    TkWindow *winPtr = (TkWindow *)path;
    CGRect bounds, screenrect, windowframe;
    NSPoint flippedorigin;
    CGFloat adjustedx;
    NSWindow *w;

    NSAccessibilityRole role = self.accessibilityRole;

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
     
    w  = TkMacOSXGetNSWindowForDrawable(winPtr->window);
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

    /*
     *  Force focus on Tk widget to align with VoiceOver cursor/focus
     *  if standard keyboard navigation of non-accessible widget elements
     *  is required.
     */
    
    if ([role isEqualToString:NSAccessibilitySliderRole] || [role isEqualToString:NSAccessibilityIncrementorRole] || [role isEqualToString:NSAccessibilityListRole] || [role isEqualToString:NSAccessibilityTableRole] || [role isEqualToString:NSAccessibilityProgressIndicatorRole]) {
	[self forceFocus];
    }
 
    return screenrect;
}

- (NSString*) accessibilityIdentifier
{

    int x = arc4random_uniform(1000);
    NSNumber *id = [NSNumber numberWithInt: x];
    NSString *identifier = [id stringValue];
    return identifier;
}


- (id) accessibilityParent
{
    
    Tk_Window win = self.tk_win;
    TkWindow *winPtr = (TkWindow *)win;

    
    /* Ensure Tk window exists. */
    if (!winPtr) {
	[self invalidateAndRelease];
	return nil;
    }
    
    if (!winPtr->window) {
	return nil;
    }

    /* 
     * Tk window exists. Set the TKContentView as the 
     * accessibility parent. 
     */
    if (winPtr->window) {
	TKContentView *view = TkMacOSXGetRootControl(winPtr->window);
	if (!view || ![view isKindOfClass:[TKContentView class]]) {
	    return nil;
	}
	self.parentView = view;
	return self.parentView;
    }
    return nil;
}

- (BOOL) accessibilityIsFocused
{
    return [self.accessibilityParent window] == [NSApp keyWindow];
}

- (BOOL)becomeFirstResponder
{
    return TRUE;
}


- (BOOL)isAccessibilityElement
{
    return YES;
}

- (BOOL)accessibilityIsIgnored
{

    /* Check if Tk state is disabled. If so, ignore accessible atribute. */
    Tk_Window win = self.tk_win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
       
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	return NO;
    }

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "state");
    if (!hPtr2) {
	return NO;
    }

    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    if (strcmp(result, "disabled") == 0) {
	return YES;
    }

    
    /* If state is NOT disabled, leave accessibility on. */ 
    return NO;
}

/* Various actions for buttons. */
- (void) accessibilityPerformAction: (NSAccessibilityActionName)action
{
    if ([action isEqualToString:NSAccessibilityPressAction]) {
        BOOL success = [self accessibilityPerformPress];
        
        if (success) {
            /* Post notification AFTER the action completes */
            NSAccessibilityRole role = self.accessibilityRole;
            if ([role isEqualToString:NSAccessibilityCheckBoxRole] || 
                [role isEqualToString:NSAccessibilityRadioButtonRole]) {
                
                /* Delay the notification to ensure the value has actually changed. */
                dispatch_async(dispatch_get_main_queue(), ^{
                    NSAccessibilityPostNotification(self, NSAccessibilityValueChangedNotification);
                });
            }
        }
    }  
}


/* Action for button roles. */
- (BOOL) accessibilityPerformPress
{
    Tk_Window win = self.tk_win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
    Tcl_Event *event;
    
    /* Standard button press. */
    hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	return FALSE;
    }

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "action");
    if (!hPtr2) {
	return FALSE;
    }
    char *action = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  
   
    callback_command = action;
    event = (Tcl_Event *)ckalloc(sizeof(Tcl_Event));
    event->proc = ActionEventProc;
    Tcl_QueueEvent((Tcl_Event *)event, TCL_QUEUE_TAIL);

    return TRUE;
}

- (NSInteger)accessibilityRowCount
{
    Tk_Window win = self.tk_win;
    if (!win) return 0;

    Tcl_Interp *interp = Tk_Interp(win);
    if (!interp) return 0;

    const char *widgetPath = Tk_PathName(win);
    if (!widgetPath) return 0;

    NSString *widgetName = [NSString stringWithUTF8String:widgetPath];

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
        return 0;
    }

    Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
    Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "role");
    if (!hPtr2) {
        return 0;
    }

    const char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    if (!result) {
        return 0;
    }

    NSString *commandString = nil;

    if (result && strcmp(result, "Listbox") == 0) {
        commandString = [NSString stringWithFormat:@"%@ size", widgetName];
    } else if (result && 
               (strcmp(result, "Table") == 0 || strcmp(result, "Tree") == 0)) {
        /* For ttk::treeview, use llength [tree children {}]. */
        commandString = [NSString stringWithFormat:@"llength [%@ children {}]", widgetName];
    } else {
	return 0;
    }

    Tcl_Obj *commandObj = Tcl_NewStringObj([commandString UTF8String], -1);
    if (Tcl_EvalObjEx(interp, commandObj, TCL_EVAL_GLOBAL) != TCL_OK) return 0;

    Tcl_Obj *resultObj = Tcl_GetObjResult(interp);
    int rowCount = 0;
    Tcl_GetIntFromObj(interp, resultObj, &rowCount);

    return rowCount;
}


- (void) forceFocus
{

    TkMainInfo *info = TkGetMainInfoList();
    NSString *widgetName = [NSString stringWithUTF8String:Tk_PathName(self.tk_win)];
    NSString *commandString = [NSString stringWithFormat:@"::tk::accessible::_forceTkFocus %@", widgetName];
    Tcl_Obj *commandObj = Tcl_NewStringObj([commandString UTF8String], -1);

    if (Tcl_EvalObjEx(info->interp, commandObj, TCL_EVAL_GLOBAL) == TCL_OK) {
        Tcl_GetObjResult(info->interp);
    }
}

- (id)invalidateAndRelease
{

    if (!self.tk_win) {
	return nil;	
    }
    
    /* Notify macOS that this element is being destroyed. */
    NSAccessibilityPostNotification(self, NSAccessibilityUIElementDestroyedNotification);

    /* Break any strong references to avoid accessing stale memory. */
    self.tk_win = NULL; 
    self.accessibilityParent = nil;

    /* Finally, release the object.*/
    [self release];
    self = nil;
    
    return TCL_OK; 
}

- (void)dealloc {
    [super dealloc];
}

@end


/*
 * Event proc which calls the ActionEventProc procedure.
 */

static int ActionEventProc(Tcl_Event *ev, int flags)
{
    (void) ev;
    (void) flags;
    
    TkMainInfo *info = TkGetMainInfoList();
    Tcl_GlobalEval(info->interp, callback_command);
    return 1;
}

/* 
 * Hash table functions to link Tk windows with their associated 
 * NSAccessibilityElement objects. 
 */

static void InitAccessibilityHashTables(void)
{
    if (!accessibilityTablesInitialized) {
        TkWindowToElementTable = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
        ElementToTkWindowTable = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
        
        Tcl_InitHashTable(TkWindowToElementTable, TCL_ONE_WORD_KEYS);
        Tcl_InitHashTable(ElementToTkWindowTable, TCL_ONE_WORD_KEYS);
        
        accessibilityTablesInitialized = 1;
    }
}

void TkAccessibility_LinkWindowToElement(Tk_Window tkwin, TkAccessibilityElement *element)
{
    if (!tkwin || !element) {
        return;
    }
    
    InitAccessibilityHashTables();
    
    Tcl_HashEntry *hPtr;
    int isNew;
    
    /* Link Tk_Window -> TkAccessibilityElement. */
    hPtr = Tcl_CreateHashEntry(TkWindowToElementTable, (char *)tkwin, &isNew);
    if (hPtr) {
        /* Retain the element to ensure it stays alive. */
        [element retain];
        Tcl_SetHashValue(hPtr, element);
    }
    
    /* Link TkAccessibilityElement -> Tk_Window. */
    hPtr = Tcl_CreateHashEntry(ElementToTkWindowTable, (char *)element, &isNew);
    if (hPtr) {
        Tcl_SetHashValue(hPtr, tkwin);
    }
}

TkAccessibilityElement *TkAccessibility_GetElementForWindow(Tk_Window tkwin)
{
    if (!tkwin || !accessibilityTablesInitialized) {
        return nil;
    }
    
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkWindowToElementTable, (char *)tkwin);
    if (hPtr) {
        return (TkAccessibilityElement *)Tcl_GetHashValue(hPtr);
    }
    
    return nil;
}

Tk_Window TkAccessibility_GetWindowForElement(TkAccessibilityElement *element)
{
    if (!element || !accessibilityTablesInitialized) {
        return NULL;
    }
    
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(ElementToTkWindowTable, (char *)element);
    if (hPtr) {
        return (Tk_Window)Tcl_GetHashValue(hPtr);
    }
    
    return NULL;
}

void TkAccessibility_UnlinkWindowAndElement(Tk_Window tkwin, TkAccessibilityElement *element)
{
    if (!accessibilityTablesInitialized) {
        return;
    }
    
    Tcl_HashEntry *hPtr;
    
    /* Remove Tk_Window -> TkAccessibilityElement mapping. */
    if (tkwin) {
        hPtr = Tcl_FindHashEntry(TkWindowToElementTable, (char *)tkwin);
        if (hPtr) {
            TkAccessibilityElement *storedElement = (TkAccessibilityElement *)Tcl_GetHashValue(hPtr);
            [storedElement release]; /* Release the retained element. */
            Tcl_DeleteHashEntry(hPtr);
        }
    }
    
    /* Remove TkAccessibilityElement -> Tk_Window mapping. */
    if (element) {
        hPtr = Tcl_FindHashEntry(ElementToTkWindowTable, (char *)element);
        if (hPtr) {
            Tcl_DeleteHashEntry(hPtr);
        }
    }
}

void TkAccessibility_CleanupHashTables(void)
{
    if (!accessibilityTablesInitialized) {
        return;
    }
    
    /* Clean up TkWindowToElementTable and release all elements. */
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr = Tcl_FirstHashEntry(TkWindowToElementTable, &search);
    while (hPtr) {
        TkAccessibilityElement *element = (TkAccessibilityElement *)Tcl_GetHashValue(hPtr);
        [element release];
        hPtr = Tcl_NextHashEntry(&search);
    }
    
    Tcl_DeleteHashTable(TkWindowToElementTable);
    Tcl_DeleteHashTable(ElementToTkWindowTable);
    
    ckfree((char *)TkWindowToElementTable);
    ckfree((char *)ElementToTkWindowTable);
    
    TkWindowToElementTable = NULL;
    ElementToTkWindowTable = NULL;
    accessibilityTablesInitialized = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * IsVoiceOverRunning --
 *
 * Runtime check to see if screen reader is running. 
 *
 * Results:
 *	Returns if screen reader is active or not. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int IsVoiceOverRunning(ClientData clientData,Tcl_Interp *ip, int objc, Tcl_Obj *const objv[])	
{

    (void) clientData;
    (void) objc;
    (void) objv;

    int result = 0;
    FILE *fp = popen("pgrep -x VoiceOver", "r");
    if (fp == NULL) {
	result = 0;
    }

    char buffer[16];
    /* If output exists, VoiceOver is running. */
    int running = (fgets(buffer, sizeof(buffer), fp) != NULL); 

    pclose(fp);
    if (running) {
	result = 1;
    }
    Tcl_SetObjResult(ip, Tcl_NewIntObj(result));
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

static int EmitSelectionChanged(ClientData clientData, Tcl_Interp *ip, int objc, Tcl_Obj *const objv[])	
{
    (void) clientData;
    
    if (objc < 2) {
	Tcl_WrongNumArgs(ip, 1, objv, "window?");
	return TCL_ERROR;
    }
    Tk_Window path;
   
    path = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
    if (path == NULL) {
	Tk_MakeWindowExist(path);
    }

    TkAccessibilityElement *widget = TkAccessibility_GetElementForWindow(path);
    
    widget.tk_win = path;

    NSAccessibilityPostNotification(widget, NSAccessibilityValueChangedNotification);
    NSAccessibilityPostNotification(widget, NSAccessibilitySelectedChildrenChangedNotification);

    PostAccessibilityAnnouncement(widget.accessibilityValue);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXAccessibility_RegisterForCleanup --
 *
 * Register event handler for destroying accessibility element.
 *
 * Results:
 *      Event handler is registered.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void TkMacOSXAccessibility_RegisterForCleanup(Tk_Window tkwin, void *accessibilityElement)
{
    Tk_CreateEventHandler(tkwin, StructureNotifyMask, TkMacOSXAccessibility_DestroyHandler, accessibilityElement);
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXAccessibility_DestroyHandler --
 *
 * Clean up accessibility element structures when window is destroyed.
 *
 * Results:
 *	Accessibility element is deallocated. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void TkMacOSXAccessibility_DestroyHandler(ClientData clientData, XEvent *eventPtr)
{
      if (eventPtr->type == DestroyNotify) {
        TkAccessibilityElement *element = (TkAccessibilityElement *)clientData;
        if (element) {
            Tk_Window tkwin = TkAccessibility_GetWindowForElement(element);
            
            /* Remove from hash tables before invalidating */
            TkAccessibility_UnlinkWindowAndElement(tkwin, element);
            
            [element invalidateAndRelease];
        }
    }
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

static int TkMacOSXAccessibleObjCmd(ClientData clientData,Tcl_Interp *ip, int objc, Tcl_Obj *const objv[])	
{
    (void) clientData;
    
    if (objc < 2) {
        Tcl_WrongNumArgs(ip, 1, objv, "window?");
        return TCL_ERROR;
    }
    
    Tk_Window path = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
    if (path == NULL) {
        return TCL_ERROR;
    }
    
    /* Check if element already exists for this window. */
    TkAccessibilityElement *existingElement = TkAccessibility_GetElementForWindow(path);
    if (existingElement) {
        /* Element already exists, no need to create a new one. */
        return TCL_OK;
    }
    
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    TkAccessibilityElement *widget = [[TkAccessibilityElement alloc] init];
    widget.tk_win = path;
    
    /* Create the bidirectional link, */
    TkAccessibility_LinkWindowToElement(path, widget);
    
    [widget.accessibilityParent accessibilityAddChildElement:widget];
    TkMacOSXAccessibility_RegisterForCleanup(widget.tk_win, widget);
    NSAccessibilityPostNotification(widget.parentView, NSAccessibilityLayoutChangedNotification);
   
    [pool drain];
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

int TkMacOSXAccessibility_Init(Tcl_Interp * interp)
{

    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", TkMacOSXAccessibleObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", EmitSelectionChanged, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader", IsVoiceOverRunning, NULL, NULL);
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
