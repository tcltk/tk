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

/*Utility procedures.*/
NSAccessibilityRole 	GetMacRole(Tk_Window win);
NSString 	*GetMacName(Tk_Window win);
NSString 	*GetMacTitle(Tk_Window win);
NSString 	*GetMacDescription(Tk_Window win);
NSString 	*GetMacValue(Tk_Window win);
NSString 	*GetMacState(Tk_Window win);

/*
 *----------------------------------------------------------------------
 *
 * GetMacRole  --
 *
 *	This function maps a widget accessibility role to an
 *      NSAccessibility role. 
 *	
 *
 * Results:
 *	Assigns the accessibility role.  
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

NSAccessibilityRole GetMacRole(
		      Tk_Window win)
{	
  char *scriptrole;
  char *pathname;
  TkMainInfo *info = TkGetMainInfoList();
  Tcl_Interp *acc_ip=info->interp;
  int i;
  pathname = Tk_PathName(win);
  Tcl_VarEval(acc_ip, "tk accessible", "get_acc_role", pathname, (char*)NULL);
  scriptrole =  Tcl_GetString(Tcl_GetObjResult(acc_ip));
  for (i = 0; roleMap[i].tkrole != NULL; i++) {
    if(strcmp(roleMap[i].tkrole, scriptrole) == 0) {
      return roleMap[i].macrole;
    }
  }
}

/*
 *----------------------------------------------------------------------
 *
 * GetMacName  --
 *
 *	This function maps a widget accessibility name to an
 *      NSAccessibility name. 
 *	
 *
 * Results:
 *	Assigns the accessibility name. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */


NSString *GetMacName(
		       Tk_Window win)
{
  char *scriptname;
  char *pathname;
  TkMainInfo *info = TkGetMainInfoList();
  Tcl_Interp *acc_ip=info->interp;
  pathname = Tk_PathName(win);
  Tcl_VarEval(acc_ip, "tk accessible", "get_acc_name", pathname, (char*)NULL);
  scriptname =  Tcl_GetString(Tcl_GetObjResult(acc_ip));
  NSString *macname = [NSString stringWithUTF8String:scriptname];
  return macname;
  
}

/*
 *----------------------------------------------------------------------
 *
 * GetMacTitle  --
 *
 *	This function maps a widget accessibility title to an
 *      NSAccessibility title. 
 *	
 *
 * Results:
 *	Assigns the accessibility title. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

NSString *GetMacTitle(
			Tk_Window win)
{
  char *scripttitle;
  char *pathname;
  TkMainInfo *info = TkGetMainInfoList();
  Tcl_Interp *acc_ip=info->interp;
  pathname = Tk_PathName(win);
  Tcl_VarEval(acc_ip, "tk accessible", "get_acc_title", pathname, (char*)NULL);
  scripttitle =  Tcl_GetString(Tcl_GetObjResult(acc_ip));
  NSString *mactitle = [NSString stringWithUTF8String:scripttitle];
  return mactitle; 
}


/*
 *----------------------------------------------------------------------
 *
 * GetMacDescription  --
 *
 *	This function maps a widget accessibility description to an
 *      NSAccessibility description. 
 *	
 *
 * Results:
 *	Assigns the accessibility description. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

NSString *GetMacDescription(
			      Tk_Window win)
{
  char *scriptdescription;
  char *pathname;
  TkMainInfo *info = TkGetMainInfoList();
  Tcl_Interp *acc_ip=info->interp;
  pathname = Tk_PathName(win);
  Tcl_VarEval(acc_ip, "tk accessible", "get_acc_description", pathname, (char*)NULL);
  scriptdescription =  Tcl_GetString(Tcl_GetObjResult(acc_ip));
  NSString *macdescription = [NSString stringWithUTF8String:scriptdescription];
  return macdescription;
}


/*----------------------------------------------------------------------
 *
 * GetMacValue  --
 *
 *	This function maps a widget accessibility value to an
 *      NSAccessibility value. 
 *	
 *
 * Results:
 *	Assigns the accessibility value. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */


NSString *GetMacValue(
			Tk_Window win)
{
  char *scriptvalue;
  char *pathname;
  TkMainInfo *info = TkGetMainInfoList();
  Tcl_Interp *acc_ip=info->interp;
  pathname = Tk_PathName(win);
  Tcl_VarEval(acc_ip, "tk accessible", "get_acc_value", pathname, (char*)NULL);
  scriptvalue =  Tcl_GetString(Tcl_GetObjResult(acc_ip));
  NSString *macvalue = [NSString stringWithUTF8String:scriptvalue];
  return macvalue;
}


/*
 *----------------------------------------------------------------------
 *
 * GetMacState  --
 *
 *	This function maps a widget accessibility state to an
 *      NSAccessibility state. 
 *	
 *
 * Results:
 *	Assigns the accessibility state. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

NSString *GetMacState(
			Tk_Window win)
{
  char *scriptstate;
  char *pathname;
  TkMainInfo *info = TkGetMainInfoList();
  Tcl_Interp *acc_ip=info->interp;
  pathname = Tk_PathName(win);
  Tcl_VarEval(acc_ip, "tk accessible", "get_acc_state", pathname, (char*)NULL);
  scriptstate =  Tcl_GetString(Tcl_GetObjResult(acc_ip));
  NSString *macstate = [NSString stringWithUTF8String:scriptstate];
  return macstate;
}

/*
 *----------------------------------------------------------------------
 *
 * GetMacAction  --
 *
 *	This function maps a widget accessibility action to an
 *      NSAccessibility action. 
 *	
 *
 * Results:
 *	Assigns the accessibility action. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

NSString *GetMacAction(
			 Tk_Window win)
{
  char *scriptaction;
  char *pathname;
  TkMainInfo *info = TkGetMainInfoList();
  Tcl_Interp *acc_ip=info->interp;
  pathname = Tk_PathName(win);
  Tcl_VarEval(acc_ip, "tk accessible", "get_acc_action", pathname, (char*)NULL);
  scriptaction =  Tcl_GetString(Tcl_GetObjResult(acc_ip));
  NSString *macaction = [NSString stringWithUTF8String:scriptaction];
  return macaction;
}



@interface TkAccessibleElement: NSView

@end

@implementation TkAccessibleElement

- (NSString *)accessibilityLabel {
}



- (BOOL)accessibilityPerformPress {
}

- (NSAccessibilityRole)accessibilityRole {

}

- (BOOL)isAccessibilityElement {
    return YES;
}


@end
