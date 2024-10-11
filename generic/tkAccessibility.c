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


/* Build list of standard accessibility roles. */
typedef enum {
  TK_ROLE_APPLICATION, 
  TK_ROLE_CELL, 
  TK_ROLE_CHECK_BOX, 
  TK_ROLE_CHECK_MENU_ITEM, 
  TK_ROLE_COLOR_CHOOSER, 
  TK_ROLE_COMBOBOX, 
  TK_ROLE_DIALOG, 
  TK_ROLE_DIRECTORY_PANE, 
  TK_ROLE_ENTRY, 
  TK_ROLE_FILE_CHOOSER, 
  TK_ROLE_FONT_CHOOSER, 
  TK_ROLE_GROUPING, 
  TK_ROLE_IMAGE, 
  TK_ROLE_LABEL, 
  TK_ROLE_LIST, 
  TK_ROLE_LIST_ITEM, 
  TK_ROLE_MENU, 
  TK_ROLE_MENUBAR, 
  TK_ROLE_MENUITEM, 
  TK_ROLE_OUTLINE, 
  TK_ROLE_OUTLINEITEM, 
  TK_ROLE_PAGE_TAB, 
  TK_ROLE_PAGETABLIST, 
  TK_ROLE_PANE, 
  TK_ROLE_PUSH_BUTTON, 
  TK_ROLE_RADIOBUTTON, 
  TK_ROLE_ROWHEADER, 
  TK_ROLE_SCROLL_BAR, 
  TK_ROLE_SLIDER, 
  TK_ROLE_SPINBUTTON, 
  TK_ROLE_TABLE_CELL, 
  TK_ROLE_TABLE_COLUMN_HEADER, 
  TK_ROLE_TABLE_ROW_HEADER, 
  TK_ROLE_TEXT, 
  TK_ROLE_TOGGLE_BUTTON, 
  TK_ROLE_TOOL_TIP , 
  TK_ROLE_TREE_TABLE, 
  TK_ROLE_WINDOW
} TkAccessibleRole;

/* Map script-level roles to C roles. */
struct TkRoleMap {
  const char *scriptRole;
  TkAccessibleRole role;
};


const struct TkRoleMap roleMap[] = {
  {"acc_application", TK_ROLE_APPLICATION},
  {"acc_cell", TK_ROLE_CELL}, 	
  {"acc_checkbutton", TK_ROLE_CHECK_BOX},
  {"acc_menucheck", TK_ROLE_CHECK_MENU_ITEM},
  {"acc_choosecolor", TK_ROLE_COLOR_CHOOSER},
  {"acc_combobox", TK_ROLE_COMBOBOX}, 
  {"acc_dialog", TK_ROLE_DIALOG},
  {"acc_opendir", TK_ROLE_DIRECTORY_PANE}, 
  {"acc_entry", TK_ROLE_ENTRY}, 
  {"acc_choosefile", TK_ROLE_FILE_CHOOSER},
  {"acc_choosefont", TK_ROLE_FONT_CHOOSER},
  {"acc_labelframe", TK_ROLE_GROUPING},
  {"acc_image", TK_ROLE_IMAGE},
  {"acc_label", TK_ROLE_LABEL},
  {"acc_listbox", TK_ROLE_LIST}, 
  {"acc_listitem",TK_ROLE_LIST_ITEM}, 
  {"acc_menu", TK_ROLE_MENU},
  {"acc_menu", TK_ROLE_MENUBAR},
  {"acc_menuentry", TK_ROLE_MENUITEM}, 
  {"acc_treeview", TK_ROLE_OUTLINE}, 
  {"acc_treeitem", TK_ROLE_OUTLINEITEM}, 
  {"acc_notebooktab", TK_ROLE_PAGE_TAB}, 
  {"acc_notebook", TK_ROLE_PAGETABLIST}, 
  {"acc_panedwindow", TK_ROLE_PANE}, 
  {"acc_button", TK_ROLE_PUSH_BUTTON}, 
  {"acc_radiobutton", TK_ROLE_RADIOBUTTON}, 
  {"acc_scrollbar", TK_ROLE_SCROLL_BAR}, 
  {"acc_scale", TK_ROLE_SLIDER}, 
  {"acc_spinbutton", TK_ROLE_SPINBUTTON}, 
  {"acc_tablecell", TK_ROLE_TABLE_CELL},
  {"acc_columnhead", TK_ROLE_TABLE_COLUMN_HEADER}, 
  {"acc_text", TK_ROLE_TEXT}, 
  {"acc_tooltip", TK_ROLE_TOOL_TIP}, 
  {"acc_treeview", TK_ROLE_TREE_TABLE}, 
  {"acc_window", TK_ROLE_WINDOW},
  {NULL, 0}
};

/* Build list of standard accessibility states. */
typedef enum {
TK_STATE_IS_ACTIVE,
TK_STATE_CHECKED,
TK_STATE_EDITABLE,
TK_STATE_ENABLED,
TK_STATE_FOCUSABLE,
TK_STATE_FOCUSED,
TK_STATE_HORIZONTAL,
TK_STATE_ICONIFIED,
TK_STATE_MODAL,
TK_STATE_MULTI_LINE,
TK_STATE_MULTISELECTABLE,
TK_STATE_PRESSED,
TK_STATE_RESIZABLE,
TK_STATE_SELECTABLE,
TK_STATE_SELECTED,
TK_STATE_SINGLE_LINE,
TK_STATE_VERTICAL,
TK_STATE_VISIBLE,
TK_STATE_SELECTABLE_TEXT,
TK_STATE_DEFAULT,
TK_STATE_READ_ONLY,
TK_STATE_COLLAPSED,
TK_STATE_HAS_TOOLTIP
} TkAccessibleState;

/* Map script-level accessibility states to C states. */

struct TkAccessibleStateMap {
  const char *scriptState;
  TkAccessibleState state;
};

const struct TkAccessibleStateMap stateMap[] = {
  {"acc_active", 	TK_STATE_IS_ACTIVE},
  {"acc_checked", 	TK_STATE_CHECKED},
  {"acc_editable",	TK_STATE_EDITABLE},
  {"acc_enabled",	TK_STATE_ENABLED},
  {"acc_focusable",	TK_STATE_FOCUSABLE},
  {"acc_focused",	TK_STATE_FOCUSED},
  {"acc_horizontal",	TK_STATE_HORIZONTAL},
  {"acc_iconified", 	TK_STATE_ICONIFIED},
  {"acc_modal",	TK_STATE_MODAL},
  {"acc_multiline",	TK_STATE_MULTI_LINE},
  {"acc_multiselectable",	TK_STATE_MULTISELECTABLE},
  {"acc_pressed",	TK_STATE_PRESSED},
  {"acc_resizable", 	TK_STATE_RESIZABLE},
  {"acc_selectable", 	TK_STATE_SELECTABLE},
  {"acc_selected",	TK_STATE_SELECTED},
  {"acc_single_line",	TK_STATE_SINGLE_LINE},
  {"acc_vertical", 	TK_STATE_VERTICAL},
  {"acc_visible",	TK_STATE_VISIBLE},
  {"acc_selectable_text",	TK_STATE_SELECTABLE_TEXT},
  {"acc_default",	TK_STATE_DEFAULT},
  {"acc_read_only",	TK_STATE_READ_ONLY},
  {"acc_collapsed",	TK_STATE_COLLAPSED},
  {"acc_tooltip",	TK_STATE_HAS_TOOLTIP},
   {NULL,	0}
};

/* Hash table for storing role assignments.*/
Tcl_HashTable WindowAccessibleRole;

/* Hash table for storing window states.*/
Tcl_HashTable WindowAccessibleState;

/* Hash table for storing window accessible name.*/
Tcl_HashTable WindowAccessibleName;

/* Hash table for storing window accessible description.*/
Tcl_HashTable WindowAccessibleDescription;

/* Hash table for storing window action.*/
Tcl_HashTable WindowAccessibleAction;

/* Build list of standard accessibility events. */

typedef enum {
  TK_ALTUNDERLINED,
  TK_INVOKE,
  TK_LISTBOXSELECT,
  TK_MENUSELECT,
  TK_MODIFIED,
  TK_SELECTION,
  TK_TRAVERSEIN,
  TK_TRAVERSEOUT,
  TK_UNDOSTACK,
  TK_WIDGETVIEWSYNC,
  TK_CLEAR,
  TK_COPY,
  TK_CUT,
  TK_LINEEND,
  TK_LINESTART,
  TK_NEXTCHAR,
  TK_NEXTLINE,
  TK_NEXTPARA,
  TK_NEXTWORD,
  TK_PASTE,
  TK_PASTESELECTION,
  TK_PREVCHAR,
  TK_PREVLINE,
  TK_PREVPARA,
  TK_PREVWINDOW,
  TK_PREVWORD,
  TK_REDO,
  TK_SELECTALL,
  TK_SELECTLINEEND,
  TK_SELECTLINESTART,
  TK_SELECTNEXTCHAR,
  TK_SELECTNEXTLINE,
  TK_SELECTNEXTPARA,
  TK_SELECTNEXTWORD,
  TK_SELECTNONE,
  TK_SELECTPREVCHAR,
  TK_SELECTPREVLINE,
  TK_SELECTPREVPARA,
  TK_SELECTPREVWORD,
  TK_TOGGLESELECTION,
  TK_UNDO
} TkAccessibleVirtualEvent;

/* Map script-level accessibility events to C events. */

struct TkAccessibleVirtualEventMap {
  const char *scriptEvent;
  TkAccessibleVirtualEvent vEvent;
};

const struct TkAccessibleVirtualEventMap eventMap[] = {
  {"acc_altunderlined",	TK_ALTUNDERLINED},
  {"acc_invoke",	TK_INVOKE},
  {"acc_listboxselect",	TK_LISTBOXSELECT},
  {"acc_menuselect",	TK_MENUSELECT},
  {"acc_modified",	TK_MODIFIED},
  {"acc_selection",	TK_SELECTION},
  {"acc_traversein",	TK_TRAVERSEIN},
  {"acc_traverseout",	TK_TRAVERSEOUT},
  {"acc_undostack",	TK_UNDOSTACK},
  {"acc_widgetviewsync",	TK_WIDGETVIEWSYNC},
  {"acc_clear",	TK_CLEAR},
  {"acc_copy",	TK_COPY},
  {"acc_cut",	TK_CUT},
  {"acc_lineend",	TK_LINEEND},
  {"acc_linestart",	TK_LINESTART},
  {"acc_nextchar",	TK_NEXTCHAR},
  {"acc_nextline",	TK_NEXTLINE},
  {"acc_nextpara",	TK_NEXTPARA},
  {"acc_nextword",	TK_NEXTWORD},
  {"acc_paste",	TK_PASTE},
  {"acc_pasteselection",	TK_PASTESELECTION},
  {"acc_prevchar",	TK_PREVCHAR},
  {"acc_prevline",	TK_PREVLINE},
  {"acc_prevpara",	TK_PREVPARA},
  {"acc_prevwindow",	TK_PREVWINDOW},
  {"acc_prevword",	TK_PREVWORD},
  {"acc_redo",	TK_REDO},
  {"acc_selectall",	TK_SELECTALL},
  {"acc_selectlineend",	TK_SELECTLINEEND},
  {"acc_selectlinestart",	TK_SELECTLINESTART},
  {"acc_selectnextchar",	TK_SELECTNEXTCHAR},
  {"acc_selectnextline",	TK_SELECTNEXTLINE},
  {"acc_selectnextpara",	TK_SELECTNEXTPARA},
  {"acc_selectnextword",	TK_SELECTNEXTWORD},
  {"acc_selectnone",	TK_SELECTNONE},
  {"acc_selectprevchar",	TK_SELECTPREVCHAR},
  {"acc_selectprevline",	TK_SELECTPREVLINE},
  {"acc_selectprevpara",	TK_SELECTPREVPARA},
  {"acc_selectprevword",	TK_SELECTPREVWORD},
  {"acc_toggleselection",	TK_TOGGLESELECTION},
  {"acc_undo",	TK_UNDO},
  {NULL, 0}	
};


/*
 *----------------------------------------------------------------------
 *
 * InitAccessibilityStorage --
 *
 *	Creates hash tables used by some of the functions in this file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates memory & creates some hash tables.
 *
 *----------------------------------------------------------------------

 */


static void
InitAccessibilityStorage()

{
 
  Tcl_InitHashTable(&WindowAccessibleRole, TCL_STRING_KEYS);
  Tcl_InitHashTable(&WindowAccessibleState, TCL_STRING_KEYS);
  Tcl_InitHashTable(&WindowAccessibleName, TCL_STRING_KEYS);
  Tcl_InitHashTable(&WindowAccessibleDescription, TCL_STRING_KEYS);
  Tcl_InitHashTable(&WindowAccessibleAction, TCL_STRING_KEYS);
  
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetAccessibleRole --
 *
 *	This function assigns a platform-neutral accessibility role for a 
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
	
  Tcl_Size i;
  int isNew = 0;
  const char *widgetrole;
  Tk_Window win;
  Tcl_HashEntry *hPtr = NULL;
  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }
  
  /* Set accessibility role for window, add to hash table. */

  widgetrole =  Tcl_GetString(objv[2]);
  for (i = 0; roleMap[i].scriptRole != NULL; i++) {
    if(strcmp(roleMap[i].scriptRole, widgetrole) == 0) {
      hPtr = Tcl_CreateHashEntry(&WindowAccessibleRole, 
				 Tk_PathName(win), &isNew);
      Tcl_SetHashValue(hPtr, roleMap[i].role);
    } else {
      continue;
    }
    return TCL_OK;
  }
  
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetAccessibleRole --
 *
 *	This function retrieves a platform-neutral accessibility role for a 
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


TkAccessibleRole
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
  Tcl_HashEntry *hPtr;
  int *accessiblerole;

  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }
  
  /* Retrieve accessibility role for window from hash table. */
  
  hPtr = Tcl_FindHashEntry(&WindowAccessibleRole, Tk_PathName(win));
  accessiblerole = Tcl_GetHashValue(hPtr);
  if (accessiblerole == TCL_OK) {
    accessiblerole = Tcl_GetHashValue(hPtr);
    return accessiblerole;
  } else {
    return TCL_OK;
  }
}

  
/*
 *----------------------------------------------------------------------
 *
 * Tk_SignalAccessibleEvent --
 *
 *	This function assigns a platform-neutral accessibility event name to a 
 *	specfic widget. 
 *
 * Results:
 *	Assigns an event and command to a widget in a struct.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkAccessibleEvent
Tk_SignalAccessibleEvent(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
{	
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? event?");
    return TCL_ERROR;
  }
	
  Tcl_Size i;
  const char * winevent;


  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }
  
  winevent = Tcl_GetString(objv[2]);
  
  /* Set accessibility event. */

  for (i = 0; eventMap[i].scriptEvent != NULL; i++) {
    if(strcmp(eventMap[i].scriptEvent, winevent) == 0) {
      return eventMap[i].vEvent;
    } else {
      continue;
    }
  }
  return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Tk_SetAccessibleState --
 *
 *	This function assigns a platform-neutral accessibility state for a 
 *	specific widget. 
 *	
 *
 * Results:
 *	Assigns an accessibility state.
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
	
  Tcl_Size i;
  int isNew = 0;
  const char * widgetstate;
  Tk_Window win;
  Tcl_HashEntry *hPtr = NULL;
  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }
  
  /* Set accessibility state for window, add to hash table. */

  widgetstate =  Tcl_GetString(objv[2]);
  for (i = 0; stateMap[i].scriptState != NULL; i++) {
    if(strcmp(stateMap[i].scriptState, widgetstate) == 0) {
      hPtr = Tcl_CreateHashEntry(&WindowAccessibleState, 
				 Tk_PathName(win), &isNew);
      Tcl_SetHashValue(hPtr, stateMap[i].state);
    } else {
      continue;
    }
    return TCL_OK;
  }
}


/*
 *----------------------------------------------------------------------
 *
 * Tk_GetAccessibleState --
 *
 *	This function retrieves a platform-neutral accessibility state for a 
 *	specific widget. 
 *	
 *
 * Results:
 *	Gets an accessibility state.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */


TkAccessibleState
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
  Tcl_HashEntry *hPtr;
  int *accessiblestate;

  /* Retrieve accessibility state for window from hash table. */
  
  hPtr = Tcl_FindHashEntry(&WindowAccessibleState, Tk_PathName(win));
  accessiblestate = Tcl_GetHashValue(hPtr);
  if (accessiblestate == TCL_OK) {
    accessiblestate = Tcl_GetHashValue(hPtr);
    return accessiblestate;
  }
  return TCL_OK;
}



/*
 *----------------------------------------------------------------------
 *
 * Tk_SetAccessibleName --
 *
 *	This function assigns a platform-neutral accessibility name for a 
 *	specific widget. 
 *	
 *
 * Results:
 *	Assigns an accessibility state.
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
	
  Tcl_Size i;
  int isNew = 0;
  const char * name;
  Tk_Window win;
  Tcl_HashEntry *hPtr = NULL;
  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }
  
  /* Set accessibility name for window, add to hash table. */

  name  =  Tcl_GetString(objv[2]);
  hPtr = Tcl_CreateHashEntry(&WindowAccessibleName, 
			     Tk_PathName(win), &isNew);
  Tcl_SetHashValue(hPtr, name);
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetAccessibleName --
 *
 *	This function returns the accessibility name for a 
 *	specific widget. 
 *	
 *
 * Results:
 *	Returns an accessibility name.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
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
	
  const char * name;
  Tk_Window win;
  Tcl_HashEntry *hPtr = NULL;
  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }
  
  /* Get accessibility name for window from  hash table. */
  hPtr = Tcl_FindHashEntry(&WindowAccessibleName, Tk_PathName(win));

  name = Tcl_GetHashValue(hPtr);
  if (name == TCL_OK) {
    name = Tcl_GetHashValue(hPtr);
    return name;
  } else {
    return "";
  }
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
	
  Tcl_Size i;
  int isNew = 0;
  const char * descrption;
  Tk_Window win;
  Tcl_HashEntry *hPtr = NULL;
  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }
  
  /* Set accessibility name for window, add to hash table. */

  name  =  Tcl_GetString(objv[2]);
  hPtr = Tcl_CreateHashEntry(&WindowAccessibleDescription, 
			     Tk_PathName(win), &isNew);
  Tcl_SetHashValue(hPtr, name);
  return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Tk_GetAccessibleDescription  --
 *
 *	This function returns the accessibility description  for a 
 *	specific widget. 
 *	
 *
 * Results:
 *	Returns an accessibility description.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
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
	
  const char * description;
  Tk_Window win;
  Tcl_HashEntry *hPtr = NULL;
  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }
  
  /* Get accessibility name for window from  hash table. */
  hPtr = Tcl_FindHashEntry(&WindowAccessibleDescription, Tk_PathName(win));

  description = Tcl_GetHashValue(hPtr);
  if (description == TCL_OK) {
    description = Tcl_GetHashValue(hPtr);
    return description;
  } else {
    return "";
  }
}


/*
 * Register script-level commands to set accessibility attributes. 
 */

int
TkAccessibility_Init(
   Tcl_Interp *interp)
{
  InitAccessibilityStorage();
  Tcl_CreateObjCommand(interp, "::tk::accessible::setrole", Tk_SetAccessibleRole, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::setstate", Tk_SetAccessibleState, NULL, NULL);
    return TCL_OK;
}





