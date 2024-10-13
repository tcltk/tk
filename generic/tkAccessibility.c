/*
 * tkAccessibility.c --
 *
 *	This file implements an accessibility API for Tk that can be accessed 
 *	from the script level.
 *
 * Copyright © 2024 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"

/*
 *----------------------------------------------------------------------
 *
 * Tk_AccessibleRole --
 *
 *	This function assigns an accessibility role for a 
 *	specific widget. 
 *	
 *
 * Results:
 *	Assigns an accessibility role.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
Tk_AccessibleRole(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
{	
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? role?");
    return "";
  }
	
  const char * role;
  Tk_Window win;
  Tcl_Obj *obj;
  int arg_length;
  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return "";
  }
  
  /* Get accessibility role for window. */

  obj = objv[2];
  role  =  Tcl_GetStringFromObj(obj, &arg_length);
  return role;
}



/*
 *----------------------------------------------------------------------
 *
 * Tk_AccessibleName --
 *
 *	This function assigns an accessibility name for a 
 *	specific widget. 
 *	
 *
 * Results:
 *	Assigns an accessibility name.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
Tk_AccessibleName(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
{	
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? name?");
    return "";
  }
	
  const char * name;
  Tk_Window win;
  Tcl_Obj *obj;
  int arg_length;
  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return "";
  }
  
  /* Get accessibility name for window. */

  obj = objv[2];
  name  =  Tcl_GetStringFromObj(obj, &arg_length);
  return name;
}


/*
 *----------------------------------------------------------------------
 *
 * Tk_AccessibleDescription --
 *
 *	This function assigns a platform-neutral accessibility descrption for a 
 *	specific widget. 
 *	
 *
 * Results:
 *	Assigns an accessibility description.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
Tk_AccessibleDescription(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
{	
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? description?");
    return "";
  }
	
  const char * description;
  Tk_Window win;
  Tcl_Obj *obj;
  int arg_length;
 
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return "";
  }
  
  /* Get accessibility description for window. */

  obj = objv[2];
  description =  Tcl_GetStringFromObj(obj, &arg_length);
  return description;
}


/*
 *----------------------------------------------------------------------
 *
 * Tk_AccessibleValue  --
 *
 *	This function reads the current value/data of the widget for  
 *	the accessibility API.
 *	
 *
 * Results:
 *	Returns an accessibility value in string format. Platform-specific API's *      will convert to the required type, if needed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
Tk_AccessibleValue(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
	
{	
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? value?");
    return "";
  }

  Tk_Window win;
  Tcl_Obj *obj;
  const char *value;
  int arg_length;

  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return "";
  }

  /*Get accessibility value.*/
  obj = objv[2]; 
  value = Tcl_GetStringFromObj(obj, &arg_length);
  return value;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_AccessibleValue  --
 *
 *	This function reads the current value/data of the widget for  
 *	the accessibility API.
 *	
 *
 * Results:
 *	Returns an accessibility value in string format. Platform-specific API's *      will convert to the required type, if needed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
Tk_AccessibleState(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
	
{	
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? state?");
    return "";
  }

  Tk_Window win;
  Tcl_Obj *obj;
  const char *state;
  int arg_length;

  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return "";
  }


  /*Get accessibility state.*/
  obj = objv[2];
  value = Tcl_GetStringFromObj(obj, &arg_length);
  return value;
}


/*
 *----------------------------------------------------------------------
 *
 * Tk_AccessibleValue  --
 *
 *	This function reads the current value/data of the widget for  
 *	the accessibility API.
 *	
 *
 * Results:
 *	Returns an accessibility value in string format. Platform-specific API's *      will convert to the required type, if needed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
Tk_AccessibleAction(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
	
{	
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? action?");
    return "";
  }

  Tk_Window win;
  Tcl_Obj *obj;
  const char *action;
  int arg_length;

  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return "";
  }


  /*Get accessibility action.*/
  obj = objv[2];
  action = Tcl_GetStringFromObj(obj, &arg_length);
  return action;
}



/*
 * Register script-level commands to set accessibility attributes. 
 */

int 
TkAccessibility_Init(
   Tcl_Interp *interp)
{
  Tcl_CreateObjCommand(interp, "::tk::accessible::role", Tk_AccessibleRole, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::name", Tk_AccessibleName, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::description", Tk_AccessibleDescription, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::value", Tk_AccessibleValue, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::state", Tk_AccessibleState, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::action", Tk_AccessibleAction, NULL, NULL); 
    return TCL_OK;
}





