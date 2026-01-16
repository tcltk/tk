/*
 * tkAccessibility.c --
 *
 *	This file implements an accessibility API for Tk that can be accessed
 *	from the script level. We are tracking accessible traits per Tk_Window
 *      in hash tables that can be accessed on any platform. This core Tk API
 *      is backed by platform-specific implementations.
 *
 * Copyright (c) 2024-2025 Kevin Walzer
 * Copyright (c) 2024 Emiliano Gavilan
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "tkInt.h"

/* Data declarations and protoypes of functions used in this file. */

Tcl_HashTable *TkAccessibilityObject = NULL;

int     Tk_SetAccessibleRole(TCL_UNUSED(void *),Tcl_Interp *ip,
			     Tcl_Size objc, Tcl_Obj *const objv[]);
int     Tk_SetAccessibleName(TCL_UNUSED(void *),Tcl_Interp *ip,
			     Tcl_Size objc, Tcl_Obj *const objv[]);
int     Tk_SetAccessibleDescription(TCL_UNUSED(void *),Tcl_Interp *ip,
				    Tcl_Size objc, Tcl_Obj *const objv[]);
int     Tk_SetAccessibleState (TCL_UNUSED(void *),Tcl_Interp *ip,
			       Tcl_Size objc, Tcl_Obj *const objv[]);
int     Tk_SetAccessibleValue(TCL_UNUSED(void *),Tcl_Interp *ip,
			      Tcl_Size objc, Tcl_Obj *const objv[]);
int     Tk_SetAccessibleAction(TCL_UNUSED(void *),Tcl_Interp *ip,
			       Tcl_Size objc, Tcl_Obj *const objv[]);
int     Tk_SetAccessibleHelp(TCL_UNUSED(void *),Tcl_Interp *ip,
			     Tcl_Size objc, Tcl_Obj *const objv[]);
int     Tk_GetAccessibleRole(TCL_UNUSED(void *),Tcl_Interp *ip,
			     Tcl_Size objc, Tcl_Obj *const objv[]);
int     Tk_GetAccessibleName(TCL_UNUSED(void *),Tcl_Interp *ip,
			     Tcl_Size objc, Tcl_Obj *const objv[]);
int     Tk_GetAccessibleDescription(TCL_UNUSED(void *),Tcl_Interp *ip,
				    Tcl_Size objc, Tcl_Obj *const objv[]);
int     Tk_GetAccessibleState(TCL_UNUSED(void *),Tcl_Interp *ip,
			      Tcl_Size objc, Tcl_Obj *const objv[]);
int     Tk_GetAccessibleValue(TCL_UNUSED(void *),Tcl_Interp *ip,
			      Tcl_Size objc, Tcl_Obj *const objv[]);
int     Tk_GetAccessibleAction(TCL_UNUSED(void *),Tcl_Interp *ip,
			       Tcl_Size objc, Tcl_Obj *const objv[]);
int     Tk_GetAccessibleHelp(TCL_UNUSED(void *),Tcl_Interp *ip,
			     Tcl_Size objc, Tcl_Obj *const objv[]);
void    TkAccessibility_Cleanup(ClientData clientData);
/* Cleanup proc when the window is destroyed. */
static  Tk_EventProc WindowDestroyHandler;

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
		     Tcl_Size objc,			/* Number of arguments. */
		     Tcl_Obj *const objv[])	/* Argument objects. */
{
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? role?");
    return TCL_ERROR;
  }

  Tk_Window win;
  Tcl_HashEntry *hPtr, *hPtr2;

  Tcl_HashTable *AccessibleAttributes;

  int isNew;
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }

  /*
   * Create new hash table for widget attributes if none exists.
   * Ensure it is unique to that widget.
   */
  hPtr=Tcl_CreateHashEntry(TkAccessibilityObject, win, &isNew);
  if (isNew) {
    AccessibleAttributes = (Tcl_HashTable *)Tcl_Alloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(AccessibleAttributes,TCL_STRING_KEYS);
    Tcl_SetHashValue(hPtr, AccessibleAttributes);
    Tk_CreateEventHandler(win, StructureNotifyMask, WindowDestroyHandler, win);
  } else {
    AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
  }

  /* Set accessible role for window.  */
  hPtr2 =  Tcl_CreateHashEntry(AccessibleAttributes, "role", &isNew);
  if (!isNew) {
    Tcl_DecrRefCount((Tcl_Obj *)Tcl_GetHashValue(hPtr2));
  }
  Tcl_IncrRefCount(objv[2]);
  Tcl_SetHashValue(hPtr2, objv[2]);

  Tcl_SetObjResult(ip, (Tcl_Obj *)Tcl_GetHashValue(hPtr2));
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
		     Tcl_Size objc,			/* Number of arguments. */
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

  AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
  hPtr2 =  Tcl_CreateHashEntry(AccessibleAttributes, "name", &isNew);
  if (!isNew) {
    Tcl_DecrRefCount((Tcl_Obj *)Tcl_GetHashValue(hPtr2));
  }
  Tcl_IncrRefCount(objv[2]);
  Tcl_SetHashValue(hPtr2, objv[2]);

  Tcl_SetObjResult(ip, (Tcl_Obj *)Tcl_GetHashValue(hPtr2));
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
			    Tcl_Size objc,			/* Number of arguments. */
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
  AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
  hPtr2 =  Tcl_CreateHashEntry(AccessibleAttributes, "description", &isNew);
  if (!isNew) {
    Tcl_DecrRefCount((Tcl_Obj *)Tcl_GetHashValue(hPtr2));
  }
  Tcl_IncrRefCount(objv[2]);
  Tcl_SetHashValue(hPtr2, objv[2]);

  Tcl_SetObjResult(ip, (Tcl_Obj *)Tcl_GetHashValue(hPtr2));
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
		      Tcl_Size objc,			/* Number of arguments. */
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
  AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
  hPtr2 =  Tcl_CreateHashEntry(AccessibleAttributes, "value", &isNew);
  if (!isNew) {
    Tcl_DecrRefCount((Tcl_Obj *)Tcl_GetHashValue(hPtr2));
  }
  Tcl_IncrRefCount(objv[2]);
  Tcl_SetHashValue(hPtr2, objv[2]);

  Tcl_SetObjResult(ip, (Tcl_Obj *)Tcl_GetHashValue(hPtr2));
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
		      Tcl_Size objc,			/* Number of arguments. */
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
  AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
  hPtr2 =  Tcl_CreateHashEntry(AccessibleAttributes, "state", &isNew);
  if (!isNew) {
    Tcl_DecrRefCount((Tcl_Obj *)Tcl_GetHashValue(hPtr2));
  }
  Tcl_IncrRefCount(objv[2]);
  Tcl_SetHashValue(hPtr2, objv[2]);

  Tcl_SetObjResult(ip, (Tcl_Obj *)Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Tk_SetAccessibleAction  --
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
		       Tcl_Size objc,			/* Number of arguments. */
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
  AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
  hPtr2 =  Tcl_CreateHashEntry(AccessibleAttributes, "action", &isNew);
  if (!isNew) {
    Tcl_DecrRefCount((Tcl_Obj *)Tcl_GetHashValue(hPtr2));
  }
  Tcl_IncrRefCount(objv[2]);
  Tcl_SetHashValue(hPtr2, objv[2]);

  Tcl_SetObjResult(ip, (Tcl_Obj *)Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetAccessibleHelp  --
 *
 *	This function sets the accessibility help text for the widget.
 *
 *
 * Results:
 *	Sets help text for the widget.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_SetAccessibleHelp(
		     TCL_UNUSED(void *),
		     Tcl_Interp *ip,		/* Current interpreter. */
		     Tcl_Size objc,			/* Number of arguments. */
		     Tcl_Obj *const objv[])	/* Argument objects. */

{
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? help?");
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

  /* Set accessibility help for window. */

  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    Tcl_AppendResult(ip, "No table found. You must set the accessibility role first.", (char *) NULL);
    return TCL_ERROR;
  }
  AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
  hPtr2 =  Tcl_CreateHashEntry(AccessibleAttributes, "help", &isNew);
  if (!isNew) {
    Tcl_DecrRefCount((Tcl_Obj *)Tcl_GetHashValue(hPtr2));
  }
  Tcl_IncrRefCount(objv[2]);
  Tcl_SetHashValue(hPtr2, objv[2]);

  Tcl_SetObjResult(ip, (Tcl_Obj *)Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}



/*
 *----------------------------------------------------------------------
 *
 * Tk_GetAccessibleRole --
 *
 *	This function reads an accessibility role for a
 *	specific widget.
 *
 *
 * Results:
 *	Gets an accessibility role.
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
		     Tcl_Size objc,			/* Number of arguments. */
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
  AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "role");
  if (!hPtr2) {
    Tcl_AppendResult(ip, "No role found", (char *) NULL);
    return TCL_ERROR;
  }

  Tcl_SetObjResult(ip, (Tcl_Obj *)Tcl_GetHashValue(hPtr2));
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
		     Tcl_Size objc,			/* Number of arguments. */
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
  AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "name");
  if (!hPtr2) {
    Tcl_AppendResult(ip, "No name found", (char *) NULL);
    return TCL_ERROR;
  }

  Tcl_SetObjResult(ip, (Tcl_Obj *)Tcl_GetHashValue(hPtr2));
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
 *	Gets an accessibility description.
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
			    Tcl_Size objc,			/* Number of arguments. */
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
  AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "description");
  if (!hPtr2) {
    Tcl_AppendResult(ip, "No description found", (char *) NULL);
    return TCL_ERROR;
  }

  Tcl_SetObjResult(ip, (Tcl_Obj *)Tcl_GetHashValue(hPtr2));
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
		      Tcl_Size objc,			/* Number of arguments. */
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
  AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "value");
  if (!hPtr2) {
    Tcl_AppendResult(ip, "No value found", (char *) NULL);
    return TCL_ERROR;
  }

  Tcl_SetObjResult(ip, (Tcl_Obj *)Tcl_GetHashValue(hPtr2));
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
		      Tcl_Size objc,			/* Number of arguments. */
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
  AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "state");
  if (!hPtr2) {
    Tcl_AppendResult(ip, "No state found", (char *) NULL);
    return TCL_ERROR;
  }

  Tcl_SetObjResult(ip, (Tcl_Obj *)Tcl_GetHashValue(hPtr2));
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
		       Tcl_Size objc,			/* Number of arguments. */
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
  AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "action");
  if (!hPtr2) {
    Tcl_AppendResult(ip, "No action found", (char *) NULL);
    return TCL_ERROR;
  }

  Tcl_SetObjResult(ip, (Tcl_Obj *)Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetAccessibleHelp  --
 *
 *	This function gets the current accessibility help for the widget.
 *
 *
 * Results:
 *	Returns an accessibility help text for the widget.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_GetAccessibleHelp(
		     TCL_UNUSED(void *),
		     Tcl_Interp *ip,		/* Current interpreter. */
		     Tcl_Size objc,			/* Number of arguments. */
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

  /* Get accessible help for window.  */
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    Tcl_AppendResult(ip, "No table found. You must set the accessibility role first.", (char *) NULL);
    return TCL_ERROR;
  }
  AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "help");
  if (!hPtr2) {
    Tcl_AppendResult(ip, "No help found", (char *) NULL);
    return TCL_ERROR;
  }

  Tcl_SetObjResult(ip, (Tcl_Obj *)Tcl_GetHashValue(hPtr2));
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WindowDestroyHandler --
 *
 *	This function cleans up accessibility hash tables on window
 *      destruction.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Cleans up hash table structures.
 *
 *----------------------------------------------------------------------
 */

static void WindowDestroyHandler(
    void *clientData,
    XEvent *eventPtr)
{
    Tk_Window tkwin = (Tk_Window)clientData;
    Tcl_HashTable *AccessibleAttributes;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashSearch search;

    if (eventPtr->type != DestroyNotify) {
	return;
    }

    hPtr = Tcl_FindHashEntry(TkAccessibilityObject, tkwin);
    if (!hPtr) {
	/* shouldn't happen*/
	return;
    }
    AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
    for (hPtr2 = Tcl_FirstHashEntry(AccessibleAttributes, &search);
	    hPtr2 != NULL; hPtr2 = Tcl_NextHashEntry(&search)) {
	Tcl_Obj *objPtr = (Tcl_Obj *)Tcl_GetHashValue(hPtr2);
	Tcl_DecrRefCount(objPtr);
	Tcl_DeleteHashEntry(hPtr2);
    }
    Tcl_DeleteHashTable(AccessibleAttributes);
    Tcl_Free(AccessibleAttributes);
    Tcl_DeleteHashEntry(hPtr);
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * TkAccessibility_Cleanup --
 *
 *	This function cleans up the global accessibility hash table and
 *	all associated data structures. It should be called during Tk
 *	finalization to prevent memory leaks.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all memory allocated for accessibility attributes.
 *
 *----------------------------------------------------------------------
 */

void
TkAccessibility_Cleanup(
    TCL_UNUSED(void *))
{
    /* If nothing to do, return. */
    if (TkAccessibilityObject == NULL) {
	return;
    }

    /* Steal the pointer and immediately clear the global so other code can bail out. */
    Tcl_HashTable *table = TkAccessibilityObject;
    TkAccessibilityObject = NULL;

    /* Iterate windows in the captured table. Use `table` (not the global). */
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;

    hPtr = Tcl_FirstHashEntry(table, &search);
    while (hPtr != NULL) {
	/* GET THE KEY FROM 'table' (not the global). */
	Tk_Window tkwin = (Tk_Window) Tcl_GetHashKey(table, hPtr);
	Tcl_HashTable *perWin = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);

	if (tkwin) {
	    /* Unregister the destroy handler so it cannot run later and touch freed data. */
	    Tk_DeleteEventHandler(tkwin, StructureNotifyMask,
				  WindowDestroyHandler, tkwin);
	}

	if (perWin) {
	    /* Decref any stored Tcl_Objs. */
	    Tcl_HashEntry *h2;
	    Tcl_HashSearch s2;
	    h2 = Tcl_FirstHashEntry(perWin, &s2);
	    while (h2) {
		Tcl_Obj *obj = (Tcl_Obj *)Tcl_GetHashValue(h2);
		if (obj) {
		    Tcl_DecrRefCount(obj);
		}
		h2 = Tcl_NextHashEntry(&s2);
	    }

	    /* Delete the per-window hash table and free its memory. */
	    Tcl_DeleteHashTable(perWin);
	    Tcl_Free(perWin);
	}

	hPtr = Tcl_NextHashEntry(&search);
    }

    /* Now free the main table safely. */
    Tcl_DeleteHashTable(table);
    Tcl_Free(table);
}



/*
 * Register script-level commands to set accessibility attributes.
 */

int
TkAccessibility_Init(
		     Tcl_Interp *interp)
{
  Tcl_CreateObjCommand2(interp, "::tk::accessible::set_acc_role", Tk_SetAccessibleRole, NULL, NULL);
  Tcl_CreateObjCommand2(interp, "::tk::accessible::set_acc_name", Tk_SetAccessibleName, NULL,NULL);
  Tcl_CreateObjCommand2(interp, "::tk::accessible::set_acc_description", Tk_SetAccessibleDescription, NULL, NULL);
  Tcl_CreateObjCommand2(interp, "::tk::accessible::set_acc_value", Tk_SetAccessibleValue, NULL, NULL);
  Tcl_CreateObjCommand2(interp, "::tk::accessible::set_acc_state", Tk_SetAccessibleState, NULL, NULL);
  Tcl_CreateObjCommand2(interp, "::tk::accessible::set_acc_action", Tk_SetAccessibleAction, NULL, NULL);
  Tcl_CreateObjCommand2(interp, "::tk::accessible::set_acc_help", Tk_SetAccessibleHelp, NULL, NULL);
  Tcl_CreateObjCommand2(interp, "::tk::accessible::get_acc_role", Tk_GetAccessibleRole, NULL, NULL);
  Tcl_CreateObjCommand2(interp, "::tk::accessible::get_acc_name", Tk_GetAccessibleName, NULL, NULL);
  Tcl_CreateObjCommand2(interp, "::tk::accessible::get_acc_description", Tk_GetAccessibleDescription, NULL, NULL);
  Tcl_CreateObjCommand2(interp, "::tk::accessible::get_acc_value", Tk_GetAccessibleValue, NULL, NULL);
  Tcl_CreateObjCommand2(interp, "::tk::accessible::get_acc_state", Tk_GetAccessibleState, NULL, NULL);
  Tcl_CreateObjCommand2(interp, "::tk::accessible::get_acc_action", Tk_GetAccessibleAction, NULL, NULL);
  Tcl_CreateObjCommand2(interp, "::tk::accessible::get_acc_help", Tk_GetAccessibleHelp, NULL, NULL);

  if (!TkAccessibilityObject) {
      TkAccessibilityObject = (Tcl_HashTable *)Tcl_Alloc(sizeof(Tcl_HashTable));
      Tcl_InitHashTable(TkAccessibilityObject, TCL_ONE_WORD_KEYS);
  }


  /* Register cleanup function. */
  TkCreateExitHandler(TkAccessibility_Cleanup, NULL);

  return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */


