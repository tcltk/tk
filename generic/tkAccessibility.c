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


const char *role, *name, *description, *value, *state, *action;

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

int
Tk_AccessibleRole(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
{	
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? role?");
    return TCL_ERROR;
  }
	
  Tk_Window win;
  Tcl_Obj *obj;
  Tcl_Size arg_length;
  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }
  
  /* Get accessibility role for window. */

  obj = objv[2];
  role  =  Tcl_GetStringFromObj(obj, &arg_length);
  return TCL_OK;
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

int
Tk_AccessibleName(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
{	
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? name?");
    return TCL_ERROR;
  }
	
  Tk_Window win;
  Tcl_Obj *obj;
  Tcl_Size arg_length;
  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }
  
  /* Get accessibility name for window. */

  obj = objv[2];
  name  =  Tcl_GetStringFromObj(obj, &arg_length);
  return TCL_OK;
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

int
Tk_AccessibleDescription(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
{	
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? description?");
    return TCL_ERROR;
  }
	
  Tk_Window win;
  Tcl_Obj *obj;
  Tcl_Size arg_length;
 
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }
  
  /* Get accessibility description for window. */

  obj = objv[2];
  description =  Tcl_GetStringFromObj(obj, &arg_length);
  return TCL_OK;
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
 *	Returns an accessibility value in string format.
 *      Platform-specific API's will convert to the required type, if needed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_AccessibleValue(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
	
{	
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? value?");
    return TCL_ERROR;
  }

  Tk_Window win;
  Tcl_Obj *obj;
  Tcl_Size arg_length;

  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }

  /*Get accessibility value.*/
  obj = objv[2]; 
  value = Tcl_GetStringFromObj(obj, &arg_length);
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_AccessibleState  --
 *
 *	This function reads the current state of the widget for  
 *	the accessibility API.
 *	
 *
 * Results:
 *	Returns an accessibility state.
 *      
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_AccessibleState(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
	
{	
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? state?");
    return TCL_ERROR;
  }

  Tk_Window win;
  Tcl_Obj *obj;
  Tcl_Size arg_length;

  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }


  /*Get accessibility state.*/
  obj = objv[2];
  state = Tcl_GetStringFromObj(obj, &arg_length);
  return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Tk_AccessibleAction  --
 *
 *	This function sets the current accessibility action for the widget. 
 *	
 *
 * Results:
 *	Returns an accessibility action for the widget.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_AccessibleAction(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
	
{	
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? action?");
    return TCL_ERROR;
  }

  Tk_Window win;
  Tcl_Obj *obj;
  Tcl_Size arg_length;

  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }


  /*Get accessibility action.*/
  obj = objv[2];
  action = Tcl_GetStringFromObj(obj, &arg_length);
  return TCL_OK;
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





