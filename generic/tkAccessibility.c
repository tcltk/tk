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
 *
 */

#include "tkInt.h"

/* Data declarations and protoypes of functions used in this file. */

Tcl_HashTable *TkAccessibilityObject;

int     Tk_SetAccessibleRole(TCL_UNUSED(void *),Tcl_Interp *ip,
			     int objc, Tcl_Obj *const objv[]);
int     Tk_SetAccessibleName(TCL_UNUSED(void *),Tcl_Interp *ip,
			     int objc, Tcl_Obj *const objv[]);
int     Tk_SetAccessibleDescription(TCL_UNUSED(void *),Tcl_Interp *ip,
			     int objc, Tcl_Obj *const objv[]);
int     Tk_SetAccessibleState (TCL_UNUSED(void *),Tcl_Interp *ip,
			      int objc, Tcl_Obj *const objv[]);
int     Tk_SetAccessibleValue(TCL_UNUSED(void *),Tcl_Interp *ip,
			      int objc, Tcl_Obj *const objv[]);
int     Tk_SetAccessibleAction(TCL_UNUSED(void *),Tcl_Interp *ip,
			      int objc, Tcl_Obj *const objv[]);
int     Tk_GetAccessibleRole(TCL_UNUSED(void *),Tcl_Interp *ip,
			      int objc, Tcl_Obj *const objv[]);
int     Tk_GetAccessibleName(TCL_UNUSED(void *),Tcl_Interp *ip,
			      int objc, Tcl_Obj *const objv[]);
int     Tk_GetAccessibleDescription(TCL_UNUSED(void *),Tcl_Interp *ip,
				    int objc, Tcl_Obj *const objv[]);
int     Tk_GetAccessibleState(TCL_UNUSED(void *),Tcl_Interp *ip,
			      int objc, Tcl_Obj *const objv[]);
int     Tk_GetAccessibleValue(TCL_UNUSED(void *),Tcl_Interp *ip,
			      int objc, Tcl_Obj *const objv[]);
int     Tk_GetAccessibleAction(TCL_UNUSED(void *),Tcl_Interp *ip,
			       int objc, Tcl_Obj *const objv[]);

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetAccessibleRole --
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
Tk_SetAccessibleRole(
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
  Tcl_HashEntry *hPtr, *hPtr2;
  
  Tcl_HashTable *AccessibleAttributes;
  AccessibleAttributes = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
  Tcl_InitHashTable(AccessibleAttributes,TCL_STRING_KEYS);

  int isNew;
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }

  /* Set accessible role for window.  */
  hPtr=Tcl_CreateHashEntry(TkAccessibilityObject, win, &isNew);
   
  Tcl_SetHashValue(hPtr, AccessibleAttributes);

  hPtr2 =  Tcl_CreateHashEntry(AccessibleAttributes, "role", &isNew);
  if (!isNew) {
    Tcl_DecrRefCount(Tcl_GetHashValue(hPtr2));
  }
  Tcl_IncrRefCount(objv[2]);
  Tcl_SetHashValue(hPtr2, objv[2]);
  
  Tcl_SetObjResult(ip, Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}



/*
 *----------------------------------------------------------------------
 *
 * Tk_SetAccessibleName --
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
Tk_SetAccessibleName(
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
  Tcl_HashEntry *hPtr, *hPtr2;
  int isNew;
  Tcl_HashTable *AccessibleAttributes;
	
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }

  /* Set accessible name for window.  */

  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    Tcl_AppendResult(ip, "No table found. You must set the accessibility role first.", (char *) NULL);
    return TCL_ERROR;
  }

  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2 =  Tcl_CreateHashEntry(AccessibleAttributes, "name", &isNew);
  if (!isNew) {
    Tcl_DecrRefCount(Tcl_GetHashValue(hPtr2));
  }
  Tcl_IncrRefCount(objv[2]);
  Tcl_SetHashValue(hPtr2, objv[2]);
    
  Tcl_SetObjResult(ip, Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Tk_SetAccessibleDescription --
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
Tk_SetAccessibleDescription(
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
  Tcl_HashEntry *hPtr, *hPtr2;
  int isNew;
  Tcl_HashTable *AccessibleAttributes;
	
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }

  /* Set accessibility description for window. */

  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    Tcl_AppendResult(ip, "No table found. You must set the accessibility role first.", (char *) NULL);
    return TCL_ERROR;
  }
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2 =  Tcl_CreateHashEntry(AccessibleAttributes, "description", &isNew);
  if (!isNew) {
    Tcl_DecrRefCount(Tcl_GetHashValue(hPtr2));
  }
  Tcl_IncrRefCount(objv[2]);
  Tcl_SetHashValue(hPtr2, objv[2]);
    
  Tcl_SetObjResult(ip, Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Tk_SetAccessibleValue  --
 *
 *	This function sets the current value/data of the widget for  
 *	the accessibility API.
 *	
 *
 * Results:
 *	Assigns  an accessibility value in string format.
 *      Platform-specific API's will convert to the required type, if needed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_SetAccessibleValue(
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
  Tcl_HashEntry *hPtr, *hPtr2;
  int isNew;
  Tcl_HashTable *AccessibleAttributes;
	
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }

  /* Set accessibility value for window. */

  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    Tcl_AppendResult(ip, "No table found. You must set the accessibility role first.", (char *) NULL);
    return TCL_ERROR;
  }
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2 =  Tcl_CreateHashEntry(AccessibleAttributes, "value", &isNew);
  if (!isNew) {
    Tcl_DecrRefCount(Tcl_GetHashValue(hPtr2));
  }
  Tcl_IncrRefCount(objv[2]);
  Tcl_SetHashValue(hPtr2, objv[2]);
    
  Tcl_SetObjResult(ip, Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetAccessibleState  --
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
Tk_SetAccessibleState(
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
  Tcl_HashEntry *hPtr, *hPtr2;
  int isNew;
  Tcl_HashTable *AccessibleAttributes;
	
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }

  /* Set accessibility state for window. */

  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    Tcl_AppendResult(ip, "No table found. You must set the accessibility role first.", (char *) NULL);
    return TCL_ERROR;
  }
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2 =  Tcl_CreateHashEntry(AccessibleAttributes, "state", &isNew);
  if (!isNew) {
    Tcl_DecrRefCount(Tcl_GetHashValue(hPtr2));
  }
  Tcl_IncrRefCount(objv[2]);
  Tcl_SetHashValue(hPtr2, objv[2]);
    
  Tcl_SetObjResult(ip, Tcl_GetHashValue(hPtr2));
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
 *	Sets an accessibility action for the widget.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_SetAccessibleAction(
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
  Tcl_HashEntry *hPtr, *hPtr2;
  int isNew;
  Tcl_HashTable *AccessibleAttributes;
	
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }

  /* Set accessibility action for window. */

  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    Tcl_AppendResult(ip, "No table found. You must set the accessibility role first.", (char *) NULL);
    return TCL_ERROR;
  }
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2 =  Tcl_CreateHashEntry(AccessibleAttributes, "action", &isNew);
  if (!isNew) {
    Tcl_DecrRefCount(Tcl_GetHashValue(hPtr2));
  }
  Tcl_IncrRefCount(objv[2]);
  Tcl_SetHashValue(hPtr2, objv[2]);
    
  Tcl_SetObjResult(ip, Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}

 

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetAccessibleRole --
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
Tk_GetAccessibleRole(
		     TCL_UNUSED(void *),
		     Tcl_Interp *ip,		/* Current interpreter. */
		     int objc,			/* Number of arguments. */
		     Tcl_Obj *const objv[])	/* Argument objects. */
{	
  if (objc < 2) {
    Tcl_WrongNumArgs(ip, 1, objv, "window?");
    return TCL_ERROR;
  }
	
  Tk_Window win;
  Tcl_HashEntry *hPtr, *hPtr2;
  
  Tcl_HashTable *AccessibleAttributes;

  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }

  /* Get accessible role for window.  */
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    Tcl_AppendResult(ip, "No table found. You must set the accessibility role first.", (char *) NULL);
    return TCL_ERROR;
  }
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "role");
  if (!hPtr2) {
    Tcl_AppendResult(ip, "No role found", (char *) NULL);
    return TCL_ERROR;
  }
  
  Tcl_SetObjResult(ip, Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}



/*
 *----------------------------------------------------------------------
 *
 * Tk_GetAccessibleName --
 *
 *	This function reads an accessibility name for a 
 *	specific widget. 
 *	
 *
 * Results:
 *	Gets an accessibility name.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_GetAccessibleName(
		     TCL_UNUSED(void *),
		     Tcl_Interp *ip,		/* Current interpreter. */
		     int objc,			/* Number of arguments. */
		     Tcl_Obj *const objv[])	/* Argument objects. */
{	
  if (objc < 2) {
    Tcl_WrongNumArgs(ip, 1, objv, "window?");
    return TCL_ERROR;
  }

  Tk_Window win;
  Tcl_HashEntry *hPtr, *hPtr2;
  
  Tcl_HashTable *AccessibleAttributes;

  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }

  /* Get accessible name for window.  */
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    Tcl_AppendResult(ip, "No table found. You must set the accessibility role first.", (char *) NULL);
    return TCL_ERROR;
  }
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "name");
  if (!hPtr2) {
    Tcl_AppendResult(ip, "No name found", (char *) NULL);
    return TCL_ERROR;
  }

  Tcl_SetObjResult(ip, Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}



/*
 *----------------------------------------------------------------------
 *
 * Tk_GetAccessibleDescription --
 *
 *	This function reads a platform-neutral accessibility descrption for a 
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
Tk_GetAccessibleDescription(
			    TCL_UNUSED(void *),
			    Tcl_Interp *ip,		/* Current interpreter. */
			    int objc,			/* Number of arguments. */
			    Tcl_Obj *const objv[])	/* Argument objects. */
{	
  if (objc < 2) {
    Tcl_WrongNumArgs(ip, 1, objv, "window?");
    return TCL_ERROR;
  }
	
  Tk_Window win;
  Tcl_HashEntry *hPtr, *hPtr2;
  
  Tcl_HashTable *AccessibleAttributes;

  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }

  /* Get accessible description for window.  */
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    Tcl_AppendResult(ip, "No table found. You must set the accessibility role first.", (char *) NULL);
    return TCL_ERROR;
  }
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "description");
  if (!hPtr2) {
    Tcl_AppendResult(ip, "No description found", (char *) NULL);
    return TCL_ERROR;
  }
  
  Tcl_SetObjResult(ip, Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Tk_GetAccessibleValue  --
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
Tk_GetAccessibleValue(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
	
{	
  if (objc < 2) {
    Tcl_WrongNumArgs(ip, 1, objv, "window?");
    return TCL_ERROR;
  }

  Tk_Window win;
  Tcl_HashEntry *hPtr, *hPtr2;
  
  Tcl_HashTable *AccessibleAttributes;

  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }

  /* Get accessible value for window.  */
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    Tcl_AppendResult(ip, "No table found. You must set the accessibility role first.", (char *) NULL);
    return TCL_ERROR;
  }
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "value");
  if (!hPtr2) {
    Tcl_AppendResult(ip, "No value found", (char *) NULL);
    return TCL_ERROR;
  }
  
  Tcl_SetObjResult(ip, Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetAccessibleState  --
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
Tk_GetAccessibleState(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
	
{	
  if (objc < 2) {
    Tcl_WrongNumArgs(ip, 1, objv, "window?");
    return TCL_ERROR;
  }

  Tk_Window win;
  Tcl_HashEntry *hPtr, *hPtr2;
  
  Tcl_HashTable *AccessibleAttributes;

  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }

  /* Get accessible state for window.  */
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    Tcl_AppendResult(ip, "No table found. You must set the accessibility role first.", (char *) NULL);
    return TCL_ERROR;
  }
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "state");
  if (!hPtr2) {
    Tcl_AppendResult(ip, "No state found", (char *) NULL);
    return TCL_ERROR;
  }
  
  Tcl_SetObjResult(ip, Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}



/*
 *----------------------------------------------------------------------
 *
 * Tk_GetAccessibleAction  --
 *
 *	This function gets the current accessibility action for the widget. 
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
Tk_GetAccessibleAction(
		       TCL_UNUSED(void *),
		       Tcl_Interp *ip,		/* Current interpreter. */
		       int objc,			/* Number of arguments. */
		       Tcl_Obj *const objv[])	/* Argument objects. */
	
{	
  if (objc < 2) {
    Tcl_WrongNumArgs(ip, 1, objv, "window?");
    return TCL_ERROR;
  }

  Tk_Window win;
  Tcl_HashEntry *hPtr, *hPtr2;
  
  Tcl_HashTable *AccessibleAttributes;

  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }

  /* Get accessible action for window.  */
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    Tcl_AppendResult(ip, "No table found. You must set the accessibility role first.", (char *) NULL);
    return TCL_ERROR;
  }
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "action");
  if (!hPtr2) {
    Tcl_AppendResult(ip, "No action found", (char *) NULL);
    return TCL_ERROR;
  }
  
  Tcl_SetObjResult(ip, Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}


/*
 * Register script-level commands to set accessibility attributes. 
 */

int 
TkAccessibility_Init(
		     Tcl_Interp *interp)
{
 
  Tcl_CreateObjCommand(interp, "::tk::accessible::acc_role", Tk_SetAccessibleRole, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::acc_name", Tk_SetAccessibleName, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::acc_description", Tk_SetAccessibleDescription, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::acc_value", Tk_SetAccessibleValue, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::acc_state", Tk_SetAccessibleState, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::acc_action", Tk_SetAccessibleAction, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::get_acc_role", Tk_GetAccessibleRole, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::get_acc_name", Tk_GetAccessibleName, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::get_acc_description", Tk_GetAccessibleDescription, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::get_acc_value", Tk_GetAccessibleValue, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::get_acc_state", Tk_GetAccessibleState, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::get_acc_action", Tk_GetAccessibleAction, NULL, NULL);
  TkAccessibilityObject =   (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
  Tcl_InitHashTable(TkAccessibilityObject, TCL_STRING_KEYS);
  return TCL_OK;
}



