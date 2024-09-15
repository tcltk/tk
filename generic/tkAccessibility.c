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



TkMainInfo *accessibleinterp = TkGetMainInfoList();


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
  {"acc_notebook", TK_ROLE_PAGE_TAB_LIST}, 
  {"acc_panedwindow", TK_ROLE_SPLITPANE}, 
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

/* Hash table for storing role assignments.*/
Tcl_HashTable WindowAccessibleRole;
Tcl_InitHashTable(&WindowAccessibleRole, TCL_STRING_KEYS);

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
  if (objc < 2) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? role?");
    return TCL_ERROR;
  }
	
  Tcl_Size i;
  int isNew = 0;
  const * char widgetrole;
  Tk_Window win;
  Tcl_HashEntry *hPtr = NULL;
  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }
  
  /* Set accessibility role for window, add to hash table. */

  if (objc == 3) {
    widgetrole =  Tcl_GetString(objv[2]);
    for (i = 0; roleMap[i].scriptRole != NULL; i++) {
      if(strcmp(roleMap[i].scriptRole, widgetrole) == 0) {
	Tcl_HashEntry *hPtr = Tcl_CreateHashEntry(&WindowAccessibleRole, 
						  win, &isNew);
	Tcl_SetHashValue(hPtr, roleMap[i].role);
      } else {
	continue;
      }
      return TCL_OK;
    }
  
    /* Retrieve current accessibility role for window. */
    if (objc == 2) {
      hPtr = Tcl_FindHashEntry(&WindowAccessibleRole, Tk_PathName(win));
      widgetrole = Tcl_GetHashValue(hPtr);
      Tcl_SetObjResult(ip, widgetrole);
      return TCL_OK;
    }
    return TCL_OK;
  }
}

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

/* Data storage for linking windows, events and commands. */
struct TkAccessibleBindingTable {
  Tk_Window *win;
  TkAccessibleVirtualEvent vEvent;
  const * char cmd;
}


  struct TkAccessibleBindingTable bindTable, *tblPtr;
tablPtr = &bindTable;

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetAccessibleEvent --
 *
 *	This function links a platform-neutral event name and command to a 
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

int
Tk_SetAccessibleEvent(
		      TCL_UNUSED(void *),
		      Tcl_Interp *ip,		/* Current interpreter. */
		      int objc,			/* Number of arguments. */
		      Tcl_Obj *const objv[])	/* Argument objects. */
{	
  if (objc < 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "window? event? cmd?");
    return TCL_ERROR;
  }
	
  Tcl_Size i;
  int isNew = 0;
  const * char winevent;
  const * char cmd;
  Tk_Window win;
  
  win = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (win == NULL) {
    return TCL_ERROR;
  }
  
  winevent = Tcl_GetString(objv[2]);
  cmd = Tcl_GetString(objv[3]);
  
  /* Set accessibility event table for window, add to table pointer. */

  for (i = 0; eventMap[i].scriptEvent != NULL; i++) {
    if(strcmp(eventMap[i].scriptEvent, winevent) == 0) {
      tblPtr->win = win;
      tblPtr->vEvent=eventMap[i].vEvent;
      tblPtr->cmd = cmd;
    } else {
      continue;
    }
    return TCL_OK:
  }
  return TCL_OK;
}



/*
 * Event proc which retrieves the Tk accessibility event to map to native API's.
 */

TkAccessibleVirtualEvent 
AccessibleEventProc(
		    TCL_UNUSED(Tcl_Event *),
		    TCL_UNUSED(int))
{
  return tblPtr->vEvent;
}
