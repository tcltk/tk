/* 
 * tkMacInit.c --
 *
 *	This file contains Mac-specific interpreter initialization
 *	functions.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkMacInit.c,v 1.3.12.3 2002/05/20 12:29:37 das Exp $
 */

#include <Resources.h>
#include <Files.h>
#include <TextUtils.h>
#include <Strings.h>
#include "tkInt.h"
#include "tkMacInt.h"
#include "tclMacInt.h"

/*
 * The following global is used by various parts of Tk to access
 * information in the global qd variable.  It is provided as a pointer
 * in the AppInit because we don't assume that Tk is running as an
 * application.  For example, Tk could be a plugin and may not have
 * access to the qd variable.  This mechanism provides a way for the
 * container application to give a pointer to the qd variable.
 */

QDGlobalsPtr tcl_macQdPtr = NULL;

/*
 *----------------------------------------------------------------------
 *
 * TkpInit --
 *
 *	Performs Mac-specific interpreter initialization related to the
 *      tk_library variable.
 *
 * Results:
 *	A standard Tcl completion code (TCL_OK or TCL_ERROR).  Also
 *	leaves information in the interp's result.
 *
 * Side effects:
 *	Sets "tk_library" Tcl variable, runs initialization scripts
 *	for Tk.
 *
 *----------------------------------------------------------------------
 */

int
TkpInit(
    Tcl_Interp *interp)		/* Interp to initialize. */
{
    char *libDir, *tempPath;
    Tcl_DString path;
    int result;

    /*
     * The following does not work with
     * safe interps because file exists is restricted.
     * to be fixed using [interp issafe] like in Unix & Windows.
     */
    static char initCmd[] = "\
proc sourcePath {file} {\n\
  global tk_library\n\
  if {[catch {uplevel #0 [list source [file join $tk_library $file.tcl]]}] == 0} {\n\
    return\n\
  }\n\
  if {[catch {uplevel #0 [list source -rsrc $file]}] == 0} {\n\
    return\n\
  }\n\
  rename sourcePath {}\n\
  set msg \"can't find $file resource or a usable $file.tcl file\"\n\
  append msg \" perhaps you need to install Tk or set your \"\n\
  append msg \"TK_LIBRARY environment variable?\"\n\
  error $msg\n\
}\n\
sourcePath tk\n\
sourcePath button\n\
sourcePath dialog\n\
sourcePath entry\n\
sourcePath focus\n\
sourcePath listbox\n\
sourcePath menu\n\
sourcePath optMenu\n\
sourcePath palette\n\
sourcePath scale\n\
sourcePath scrlbar\n\
sourcePath tearoff\n\
sourcePath text\n\
if {[catch {package require msgcat}]} {sourcePath msgcat}\n\
sourcePath bgerror\n\
sourcePath msgbox\n\
sourcePath comdlg\n\
rename sourcePath {}";

    Tcl_DStringInit(&path);

    /*
     * The tk_library path can be found in several places.  Here is the order
     * in which the are searched.
     *		1) the variable may already exist
     *		2) env array
     *		3) System Folder:Extensions:Tool Command Language:
     */
     
    libDir = Tcl_GetVar(interp, "tk_library", TCL_GLOBAL_ONLY);
    if (libDir == NULL) {
	libDir = Tcl_GetVar2(interp, "env", "TK_LIBRARY", TCL_GLOBAL_ONLY);
    }
    if (libDir == NULL) {
	tempPath = Tcl_GetVar2(interp, "env", "EXT_FOLDER", TCL_GLOBAL_ONLY);
	if (tempPath != NULL) {
	    Tcl_DString libPath;
	    char *argv[3];
	    
	    argv[0] = tempPath;
	    argv[1] = "Tool Command Language";	    
	    Tcl_DStringInit(&libPath);
	    Tcl_DStringAppend(&libPath, "tk", -1);
	    Tcl_DStringAppend(&libPath, TK_VERSION, -1);
	    argv[2] = libPath.string;
	    Tcl_JoinPath(3, argv, &path);
	    Tcl_DStringFree(&libPath);
	    libDir = path.string;
	}
    }
    if (libDir == NULL) {
	libDir = "no library";
    }

    /*
     * Assign path to the global Tcl variable tcl_library.
     */
    Tcl_SetVar(interp, "tk_library", libDir, TCL_GLOBAL_ONLY);
    Tcl_DStringFree(&path);

	result = Tcl_Eval(interp, initCmd);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetAppName --
 *
 *	Retrieves the name of the current application from a platform
 *	specific location.  On the Macintosh we look to see if the
 *	App Name is specified in a resource.  If not, the application 
 *	name is the root of the tail of the path contained in the tcl
 *	variable argv0.
 *
 * Results:
 *	Returns the application name in the given Tcl_DString.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetAppName(
    Tcl_Interp *interp,		/* The main interpreter. */
    Tcl_DString *namePtr)	/* A previously initialized Tcl_DString. */
{
    int argc;
    char **argv = NULL, *name, *p;
    Handle h = NULL;

    h = GetNamedResource('STR ', "\pTk App Name");
    if (h != NULL) {
	HLock(h);
	Tcl_DStringAppend(namePtr, (*h)+1, **h);
	HUnlock(h);
	ReleaseResource(h);
	return;
    }
    
    name = Tcl_GetVar(interp, "argv0", TCL_GLOBAL_ONLY);
    if (name != NULL) {
	Tcl_SplitPath(name, &argc, &argv);
	if (argc > 0) {
	    name = argv[argc-1];
	    p = strrchr(name, '.');
	    if (p != NULL) {
		*p = '\0';
	    }
	} else {
	    name = NULL;
	}
    }
    if ((name == NULL) || (*name == 0)) {
	name = "tk";
    }
    Tcl_DStringAppend(namePtr, name, -1);
    if (argv != NULL) {
	ckfree((char *)argv);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayWarning --
 *
 *	This routines is called from Tk_Main to display warning
 *	messages that occur during startup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Displays a message box.
 *
 *----------------------------------------------------------------------
 */

void
TkpDisplayWarning(
    char *msg,			/* Message to be displayed. */
    char *title)		/* Title of warning. */
{
    Tcl_DString ds;
    Tcl_DStringInit(&ds);
    Tcl_DStringAppend(&ds, title, -1);
    Tcl_DStringAppend(&ds, ": ", -1);
    Tcl_DStringAppend(&ds, msg, -1);
    panic(Tcl_DStringValue(&ds));
    Tcl_DStringFree(&ds);
}
