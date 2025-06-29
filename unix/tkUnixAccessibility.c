/*
 * tkUnixAccessibility.c --
 *
 *	This file implements accessibility/screen-reader support 
 *      on Unix-like systems based on the Gnome Accessibility Toolkit, 
 *      the standard accessibility library for X11 systems. 
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 2006, Marcus von Appen
 * Copyright (c) 2019-2025 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <tcl.h>
#include <tk.h>

#ifdef USE_ATK
#include <atk/atkobject.h>
#include <atk/atk.h>
#include <atk-bridge.h> 
#include <gtk/gtk.h>
#include <dbus/dbus.h>

/* Data declarations and protoypes of functions used in this file. */

/* Core Atk/Tk accessible struct. */
typedef struct {
  AtkObject parent;
  Tk_Window tkwin;
  Tcl_Interp *interp;
  char *path;
} TkAtkAccessible;

typedef struct {
  AtkObjectClass parent_class;
} TkAtkAccessibleClass;

G_DEFINE_TYPE(TkAtkAccessible, tk_atk_accessible, ATK_TYPE_OBJECT)

static GList *global_accessible_objects = NULL;
static GHashTable *tk_to_atk_map = NULL;

static void GetWidgetExtents(Tk_Window tkwin, int *x, int *y, int *w, int *h);
static void tk_get_extents(AtkComponent *component, gint *x, gint *y, gint *width, gint *height, AtkCoordType coord_type);
static gint tk_get_n_children(AtkObject *obj);
static AtkObject *tk_ref_child(AtkObject *obj, gint i);
static AtkRole GetAtkRoleForWidget(Tk_Window win);
static AtkRole tk_get_role(AtkObject *obj);
static const gchar *tk_get_name(AtkObject *obj);
static const gchar *tk_get_description(AtkObject *obj);
static const gchar *tk_get_description(AtkObject *obj);
static const gchar *tk_get_value(AtkObject *obj);
static const gchar *tk_get_help_text(AtkObject *obj);
static AtkStateSet *tk_ref_state_set(AtkObject *obj);
static gboolean tk_action_do_action(AtkAction *action, gint i);
static gint tk_action_get_n_actions(AtkAction *action);
static const gchar *tk_action_get_name(AtkAction *action, gint i);
static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass);
AtkObject *TkCreateAccessibleAtkObject(Tcl_Interp *interp, Tk_Window tkwin, const char *path);
static int GtkEventLoop(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]); 
void InstallGtkEventLoop();
void InitAtkTkMapping(void);
void RegisterAtkObjectForTkWindow(Tk_Window tkwin, AtkObject *atkobj);
AtkObject *GetAtkObjectForTkWindow(Tk_Window tkwin);
void UnregisterAtkObjectForTkWindow(Tk_Window tkwin);
static int EmitSelectionChanged(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
void TkAtkAccessible_RegisterForCleanup(Tk_Window tkwin, void *tkAccessible);
static void TkAtkAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr);
int TkAtkAccessibleObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int TkAtkAccessibility_Init(Tcl_Interp *interp);

/*
 * Struct to map Tk roles into Atk roles.
 */

struct AtkRoleMap {
  const char *tkrole;
  AtkRole atkrole;
}
  
static const struct AtkRoleMap roleMap[] = {
    {"Button", ATK_ROLE_PUSH_BUTTON},
    {"Checkbox", ATK_ROLE_CHECK_BOX},
    {"Menuitem", ATK_ROLE_CHECK_MENU_ITEM},
    {"Combobox", ATK_ROLE_COMBOBOX},
    {"Entry", ATK_ROLE_ENTRY},		
    {"Label", ATK_ROLE_LABEL},
    {"Listbox", ATK_ROLE_LIST},
    {"Menu", ATK_ROLE_MENU},
    {"Tree", ATK_ROLE_OUTLINE},
    {"Notebook", ATK_ROLE_PAGE_TAB},
    {"Progressbar", ATK_ROLE_PROGRESS_BAR},
    {"Radiobutton",ATK_ROLE_RADIOBUTTON},				      
    {"Scale", ATK_ROLE_SLIDER},
    {"Spinbox", ATK_ROLE_SPINBUTTON},
    {"Table", ATK_ROLE_TABLE},
    {NULL, 0}
};

/* Hash table for managing accessibility attributes. */
extern Tcl_HashTable *TkAccessibilityObject;


/* 
 * Functions to get accessible frame to Atk. 
 */

static void GetWidgetExtents(Tk_Window tkwin, int *x,int *y, int *w,int *h)
{
  if (tkwin) {
    *x = Tk_X(tkwin);
    *y = Tk_Y(tkwin);
    *w = Tk_Width(tkwin);
    *h = Tk_Height(tkwin);
  } else {
    *x = *y = *w = *h = 0;
  }
}

static void tk_get_extents(AtkComponent *component, gint *x, gint *y, gint *width, gint *height, AtkCoordType coord_type)
{
  TkAtkAccessible *acc = (TkAtkAccessible *)component;
  GetWidgetExtents(acc->tkwin, x, y, width, height);
}


/* Limit children of widget. Only the toplevel should return children. */
static gint tk_get_n_children(AtkObject *obj)
{
  return 0;
}

/* Limit children of widget. Only the toplevel should return children. */
static AtkObject *tk_ref_child(AtkObject *obj, gint i)
{
  return NULL;
}

/* 
 * Functions to map accessible role to Atk.
 */

static AtkRole GetAtkRoleForWidget(Tk_Window win)
{
 
  AtkRole role;
  Tcl_HashEntry *hPtr, *hPtr2;
  Tcl_HashTable *AccessibleAttributes;
		
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    role = NULL;	  
  }
		
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "role");
  if (!hPtr2) {
    role = NULL;
  }
  char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  for (int i = 0; i < sizeof(roleMap); i++) {
    if (strcmp(roleMap[i].tkrole, result) != 0) {
      continue;
    }
    role = roleMap[i].atkrole;
  }
  return role;
}

static AtkRole tk_get_role(AtkObject *obj)
{
  TkAtkAccessible *acc = (TkAtkAccessible *)obj;
  Tk_Window win = acc->tkwin;
  return GetAtkRoleForWidget(win);
}


/* Function to map accessible name to Atk.*/
static const gchar *tk_get_name(AtkObject *obj)
{
  TkAtkAccessible *acc = (TkAtkAccessible *)obj;
  Tk_Window win = acc->tkwin;
  Tcl_HashEntry *hPtr, *hPtr2;
  Tcl_HashTable *AccessibleAttributes;
	
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    return NULL;
  }
	
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "name");
  if (!hPtr2) {
    return NULL;
  }
	
  char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  return result;
}

/* Function to map accessible description to Atk. */
static const gchar *tk_get_description(AtkObject *obj)
{
  TkAtkAccessible *acc = (TkAtkAccessible *)obj;

  Tk_Window win = acc->tkwin;
  Tcl_HashEntry *hPtr, *hPtr2;
  Tcl_HashTable *AccessibleAttributes;

  
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    return NULL;
  }

  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "description");
  if (!hPtr2) {
    return NULL;
  }
	
  char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  return result;
}

/* Function to map accessible description to Atk. */
static const gchar *tk_get_description(AtkObject *obj)
{
  TkAtkAccessible *acc = (TkAtkAccessible *)obj;

  Tk_Window win = acc->tkwin;
  Tcl_HashEntry *hPtr, *hPtr2;
  Tcl_HashTable *AccessibleAttributes;

  
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    return NULL;
  }

  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "description");
  if (!hPtr2) {
    return NULL;
  }
	
  char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  return result;
}

/* Function to map accessible value to Atk. */
static const gchar *tk_get_value(AtkObject *obj)
{
  TkAtkAccessible *acc = (TkAtkAccessible *)obj;

  Tk_Window win = acc->tkwin;
  Tcl_HashEntry *hPtr, *hPtr2;
  Tcl_HashTable *AccessibleAttributes;

  
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    return NULL;
  }

  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "value");
  if (!hPtr2) {
    return NULL;
  }
	
  char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  return result;
}

/* Function to map accessible help text to Atk. */
static const gchar *tk_get_help_text(AtkObject *obj)
{
  TkAtkAccessible *acc = (TkAtkAccessible *)obj;

  Tk_Window win = acc->tkwin;
  Tcl_HashEntry *hPtr, *hPtr2;
  Tcl_HashTable *AccessibleAttributes;

  
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    return NULL;
  }

  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "help");
  if (!hPtr2) {
    return NULL;
  }
	
  char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  return result;
}

/* Function to map accessible state to Atk.*/
static AtkStateSet *tk_ref_state_set(AtkObject *obj)
{
  AtkStateSet *set = atk_state_set_new();
  atk_state_set_add_state(set, ATK_STATE_ENABLED);
  atk_state_set_add_state(set, ATK_STATE_SENSITIVE);
  return set;
}

/* 
 * Functions to get button press action to Atk. 
 */
static gboolean tk_action_do_action(AtkAction *action, gint i)
{
  TkAtkAccessible *acc = (TkAtkAccessible *)action;
  if (i == 0) { 
    Tcl_CmdInfo cmdInfo;
    if (Tcl_GetCommandInfo(acc->interp, acc->path &cmdInfo)) {
      Tcl_EvalEx(tk_object->interp, "event generate . <ButtonRelease-1>", -1, TCL_EVAL_GLOBAL);
      Tcl_EvalEx(tk_object->interp, "event generate . <ButtonPress-1>", -1, TCL_EVAL_GLOBAL);
    } else {
      /* Try to invoke the command directly if it exists. */
      char command[256];
      snprintf(command, sizeof(command), "%s invoke", acc->tkwin);
      Tcl_EvalEx(acc->interp, command, -1, TCL_EVAL_GLOBAL);
    }
  }

  return TCL_OK;
}

static gint tk_action_get_n_actions(AtkAction *action)
{
  return 1;
}

static const gchar *tk_action_get_name(AtkAction *action, gint i)
{
  return "click";
}

/* Function to map Tk window to Atk class attributes. */
static void tk_atk_accessible_class_init(TkAtkAccessibleClass *klass) {
  AtkObjectClass *atk_class = ATK_OBJECT_CLASS(klass);
  atk_class->get_name = tk_get_name;
  atk_class->get_description = tk_get_description;
  atk_class->get_role = tk_get_role;
  atk_class->ref_state_set = tk_ref_state_set;
  atk_class->get_help_text = tk_get_help_text;

  AtkComponentIface *component_iface = g_type_interface_peek(klass, ATK_TYPE_COMPONENT);
  component_iface->get_extents = tk_get_extents;
   
  AtkValueIface *value_iface = g_type_interface_peek(klass, ATK_TYPE_VALUE);
  value_iface->get_current_value = tk_get_value;

  AtkActionIface *action_iface = g_type_interface_peek(klass, ATK_TYPE_ACTION);
  action_iface->do_action = tk_action_do_action;
  action_iface->get_n_actions = tk_action_get_n_actions;
  action_iface->get_name = tk_action_get_name;
}


/* Function to map Tk window to Atk attributes. */
AtkObject *TkCreateAccessibleAtkObject(Tcl_Interp *interp, Tk_Window tkwin,const char *path)
{
  TkAtkAccessible *acc = g_object_new(tk_atk_accessible_get_type(), NULL);
  acc->interp = interp;
  acc->tkwin = tkwin;
  acc->path = g_strdup(path);

  atk_object_set_role(ATK_OBJECT(acc), GetAtkRoleForWidget(acc->tkwin));
  atk_object_set_name(ATK_OBJECT(acc), path);

  global_accessible_objects = g_list_prepend(global_accessible_objects, acc);
  return ATK_OBJECT(acc);
}


/* 
 * Functions to integrate Tk and Gtk event loops. 
 */

static int GtkEventLoop(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) 
{

  /* Let GTK process its events. */

  while (g_main_context_iteration(NULL, FALSE)) {

  }
  Tcl_DoWhenIdle((Tcl_IdleProc *)GtkEventLoop, NULL); 
  return TCL_OK;
}

void InstallGtkEventLoop() {
  Tcl_DoWhenIdle((Tcl_IdleProc *)GtkEventLoop, NULL);
}

/* 
 * Functions to map Tk window to its corresponding Atk object. 
 */
void InitAtkTkMapping(void)
{
  if (!tk_to_atk_map) {
    tk_to_atk_map = g_hash_table_new(g_direct_hash, g_direct_equal);
  }
}

void RegisterAtkObjectForTkWindow(Tk_Window tkwin, AtkObject *atkobj)
{
  /* Make sure the hash table exists. */
  InitAtkTkMapping();  
  g_hash_table_insert(tk_to_atk_map, tkwin, atkobj);
}

AtkObject *GetAtkObjectForTkWindow(Tk_Window tkwin)
{
  if (!tk_to_atk_map) return NULL;
  return g_hash_table_lookup(tk_to_atk_map, tkwin);
}

void UnregisterAtkObjectForTkWindow(Tk_Window tkwin)
{
  if (tk_to_atk_map) {
    g_hash_table_remove(tk_to_atk_map, tkwin);
  }
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

static int
EmitSelectionChanged(
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
	
  AtkObject *acc = GetAtkObjectForTkWindow(path);
  AtkValue new_value = tk_get_value(acc);
 
  g_signal_emit_by_name(G_OBJECT(acc), "value-changed", new_value);
	
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * IsScreenReaderRunning --
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

int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) 
{
	
  DBusError error;
  DBusConnection *connection;
  dbus_bool_t has_owner = FALSE;
  BOOL result = true;

  dbus_error_init(&error);

  /* Connect to the session bus. */
  BOOL connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
  if (dbus_error_is_set(&error)) {
    dbus_error_free(&error);
    result = false;	
  }

  if (!connection) {
    result = false;
  }

  /* Check if org.a11y.Bus is owned (required for AT-SPI to work). */
  has_owner = dbus_bus_name_has_owner(connection, "org.a11y.Bus", &error);
  if (dbus_error_is_set(&error)) {
    dbus_error_free(&error);
    result = false;
  }

  if (!has_owner) {
    result = false;
  }

  /* Now check for a screen reader on the accessibility bus. */
  has_owner = dbus_bus_name_has_owner(connection, "org.a11y.atspi.Registry", &error);
  dbus_connection_unref(connection);

  if (dbus_error_is_set(&error)) {
    dbus_error_free(&error);
    result = false;
  }

  Tcl_SetObjResult(interp, Tcl_NewBooleanObj(result));
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkAtkAccessible_RegisterForCleanup --
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

void TkAtkAccessible_RegisterForCleanup(Tk_Window tkwin, void *tkAccessible) {
  Tk_CreateEventHandler(tkwin, StructureNotifyMask, 
			TkAtkAccessible_DestroyHandler, tkAccessible);
}

/*
 *----------------------------------------------------------------------
 *
 * TkAtkAccessible_DestroyHandler --
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

static void TkAtkAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr) 
{
  if (eventPtr->type == DestroyNotify) {
    TkAtkAccessible *tkAccessible = (TkAtkAccessible *)clientData;
    if (tkAccessible) {
      UnregisterAtkObjectForTkWindow(tkaccessible->tkwin);	  
    }
  }
}

/*
 *----------------------------------------------------------------------
 *
 * TkAtkAccessibleObjCmd --
 *
 *	Main command for adding and managing accessibility objects to Tk
 *      widgets on Linux using the Atk accessibility API.
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


int TkAtkAccessibleObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) 
{
  if (objc != 2) {
    Tcl_WrongNumArgs(interp, 1, objv, "window");
    return TCL_ERROR;
  }

  char *windowName = Tcl_GetString(objv[1]);
  Tk_Window tkwin = Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));

  if (tkwin == NULL) {
    Tcl_SetResult(interp, "Invalid window name.", TCL_STATIC);
    return TCL_ERROR;
  }


  TkAtkAccessible *accessible = TkCreateAccessibleAtkObject(interp, tkwin, windowName);
  TkAtkAccessible_RegisterForCleanup(tkwin, accessible);
  RegisterAtkObjectForTkWindow(tkwin, accessible);

	
  if (accessible == NULL) {		
    Tcl_SetResult(interp, "Failed to create accessible object.", TCL_STATIC);
    return TCL_ERROR;
  }

  return TCL_OK;
}

#endif /*USE_ATK*/
/*
 *----------------------------------------------------------------------
 *
 * TkAtkAccessibility_Init --
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

int 
#ifdef USE_ATK
TkAtkAccessibility_Init(Tcl_Interp *interp) {
	
  /* Handle Atk initialization. */
    
  if (atk_get_major_version() < 2) {
    Tcl_SetResult(interp, "ATK version 2.0 or higher is required.", TCL_STATIC);
    return TCL_ERROR;
  }

  atk_init();
  atk_bridge_adaptor_init(NULL, NULL);

  g_type_ensure(TK_ATK_TYPE_OBJECT);
	
  InstallGtkEventLoop();
	
  Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", TkAtkAccessibleObjCmd, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", EmitSelectionChanged, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader", IsScreenReaderRunning, NULL, NULL);
  
#else
  TkAtkAccessibility_Init(Tcl_Interp *interp)
    {
      /*Create empty commands if Atk not available. */
      Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", NULL, NULL, NULL);
      Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", NULL, NULL, NULL);
      Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader", NULL, NULL, NULL);
#endif
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
