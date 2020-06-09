/*
 * tkUnixAccessibility.c --
 *
 *	This file implements accessibility/screen-reader support 
 *      on Unix-like systems based on the Gnome Accessibility Toolkit, 
 *      the standard accessibility library for X11 systems. 
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 * Copyright (c) 2006, Marcus von Appen
 * Copyright (c) 2010-2019 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * Get main Tcl interpreter. 
 */
TkMainInfo *info = TkGetMainInfoList();
Tcl_Interp *ip = info->interp;


/*
 * atk-bridge interfaces, which take care of initializing the bridging
 * between Atk and AT-SPI.
 */
#ifndef BRIDGE_INIT
#define BRIDGE_INIT "gnome_accessibility_module_init"
#endif 
#ifndef BRIDGE_STOP
#define BRIDGE_STOP "gnome_accessibility_module_shutdown"
#endif

/*
 * Static name of the atk-bridge module to use for the atk<->at-spi
 * interaction.
 */
#ifndef ATK_MODULE_NAME
#define ATK_MODULE_NAME "atk-bridge"
#endif

/*
 * Avoid multiple initializations of the bridge.
 */
static int _bridge_initialized = 0;


/*
 * Module interfaces from the bridge, so we can initialize and stop it.
 */
static void (*_atk_init) (void);
static void (*_atk_stop) (void);


/*
 *---------------------------------------------------------------------
 *
 * ATKbridge_Init --
 *
 *  Loads the atk-bridge system, that hopefully exists as module using
 *  the GModule system and prepares the initialization/shutdown hooks.
 *
 * Results:
 *	Initializes the Atk bridge.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ATKbridge_Init (void)
{
  GModule *module;
  gchar *path = NULL;
  char *libenv = NULL;
  char *token = NULL;
  char *lib_path = NULL;
	
  if (_bridge_initialized)
    return TCL_OK;
	
  /* 
   * Check, if all pointers are satisfied. 
   */
  if (!atkutil_root_satisfied ())
    {
      return TCL_ERROR;
    }
	
  /*
   * Build a path to the GTK-2.0 modules.
   */
	
  char *gtk_path = "/gtk-2.0/modules/";
	
  libenv = getenv("LD_LIBRARY_PATH");
  if (!libenv) {
    libenv = "/usr/lib:/lib";
  }
  char *copy = (char *)malloc(strlen(libenv) + 1);
  if (copy == NULL) {
    return TCL_ERROR:
  }
	
  strcpy(copy, libenv);
  token = strtok(copy, ":");
	
  while (token !=NULL) {
		
    lib_path = (char *)malloc(strlen(token) + 1);
		
    strcpy(lib_path, token);
    strcat(lib_path, gtk_path);
		
    /* 
     * Try to build the correct module path and load it. 
     */
    path = g_module_build_path (lib_path, ATK_MODULE_NAME);
		
    module = g_module_open (path, G_MODULE_BIND_LAZY);
    g_free (path);
    if (!module)
      {
	return TCL_ERROR;
      }
		
    if (!g_module_symbol (module, BRIDGE_INIT, (gpointer *) &_atk_init) ||
	!g_module_symbol (module, BRIDGE_STOP, (gpointer *) &_atk_stop))
      {
	return TCL_ERROR;
      }
			
    token = strtok(NULL, ":");
  }
		
  free(copy);
  free(lib_path);
		
  _atk_init ();
		
  _bridge_initialized = 1;
  return TCL_OK;
}
	
/*
 *----------------------------------------------------------------------
 *
 * ATKbridge_Stop --
 *
 *  Stops the Atk bridge.
 *
 * Results:
 *	Stops the Atk bridge.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int*
ATKbridge_Stop (void)
{
  if (_bridge_initialized)
    {
      _bridge_initialized = 0;
      _atk_stop ();
    }
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ATKbridge_Iterate --
 *
 *  Iterates the main context in a non-blocking way, so that the AT-SPI *  bridge can interact with the ATK bindings.
 *
 * Results:
 *  Allows the AT-SPI bridge can interact with the ATK bindings.
 *
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ATKbridge_Iterate (void)
{
  if (g_main_context_iteration (g_main_context_default (), FALSE))
    return TCL_OK;
  else               
    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * ATKbridge_ExportFuncs --
 *
 *  Maps the ATK bridge functions to Tcl commands.
 *
 * Results:
 *  Allows the bridge to be called from Tcl.
 *
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
ATKbridge_ExportFuncs (void)
{
  Tcl_CreateObjCommand(ip, "::tk::AccessibilityInit", ATKbridge_Init, (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateObjCommand(ip, "::tk::AccessibilityStop", ATKbridge_Stop,(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateObjCommand(ip, "::tk::AccessibilityLoop", ATKbridge_Iterate,(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);

}
