/* 
 * tkUnixInit.c --
 *
 *        This file contains Unix-specific interpreter initialization
 *        functions.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright 2001, Apple Computer, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkMacOSXInit.c,v 1.1.2.1 2001/10/15 09:22:00 wolfsuit Exp $
 */

#include "tkInt.h"
#include "tkMacOSXInt.h"

/*
 * The Init script (common to Windows and Unix platforms) is
 * defined in tkInitScript.h
 */
#include "tkInitScript.h"


/*
 *----------------------------------------------------------------------
 *
 * TkpInit --
 *
 *        Performs Mac-specific interpreter initialization related to the
 *      tk_library variable.
 *
 * Results:
 *        Returns a standard Tcl result.  Leaves an error message or result
 *        in the interp's result.
 *
 * Side effects:
 *        Sets "tk_library" Tcl variable, runs "tk.tcl" script.
 *
 *----------------------------------------------------------------------
 */

int
TkpInit(interp)
    Tcl_Interp *interp;
{
    char tkLibPath[1024];
    int result;
    
    Tcl_SetVar2(interp, "tcl_platform", "windowingsystem", "aqua", TCL_GLOBAL_ONLY);
    
    /*
     * When Tk is in a framework, force tcl_findLibrary to look in the 
     * framework scripts directory.
     * FIXME: Should we come up with a more generic way of doing this?
     */
     
    result = Tk_MacOSXOpenBundleResources(interp, "com.tcltk.tklibrary", 
                tkLibPath, 1024, 1);
     
    if (result != TCL_ERROR) {
        Tcl_SetVar(interp, "tk_library", tkLibPath, TCL_GLOBAL_ONLY);
        Tcl_SetVar(interp, "auto_path", tkLibPath, 
            TCL_GLOBAL_ONLY | TCL_LIST_ELEMENT | TCL_APPEND_VALUE);
    }
    
    return Tcl_Eval(interp, initScript);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetAppName --
 *
 *        Retrieves the name of the current application from a platform
 *        specific location.  For Unix, the application name is the tail
 *        of the path contained in the tcl variable argv0.
 *
 * Results:
 *        Returns the application name in the given Tcl_DString.
 *
 * Side effects:
 *        None.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetAppName(interp, namePtr)
    Tcl_Interp *interp;
    Tcl_DString *namePtr;        /* A previously initialized Tcl_DString. */
{
    char *p, *name;

    name = Tcl_GetVar(interp, "argv0", TCL_GLOBAL_ONLY);
    if ((name == NULL) || (*name == 0)) {
        name = "tk";
    } else {
        p = strrchr(name, '/');
        if (p != NULL) {
            name = p+1;
        }
    }
    Tcl_DStringAppend(namePtr, name, -1);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayWarning --
 *
 *        This routines is called from Tk_Main to display warning
 *        messages that occur during startup.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        Generates messages on stdout.
 *
 *----------------------------------------------------------------------
 */

void
TkpDisplayWarning(msg, title)
    char *msg;                  /* Message to be displayed. */
    char *title;                /* Title of warning. */
{
    Tcl_Channel errChannel = Tcl_GetStdChannel(TCL_STDERR);
    if (errChannel) {
        Tcl_WriteChars(errChannel, title, -1);
        Tcl_WriteChars(errChannel, ": ", 2);
        Tcl_WriteChars(errChannel, msg, -1);
        Tcl_WriteChars(errChannel, "\n", 1);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MacOSXOpenBundleResources --
 *
 *	Given the bundle name for a shared library, this routine
 *	sets libraryVarName to the Resources/Scripts directory 
 *	in the framework package.  If hasResourceFile is
 *	true, it will also open the main resource file for the bundle.
 *
 *      FIXME: This should probably be in Tcl, but the resource stuff
 *      isn't in Darwin, and I haven't figured out how to sort out Darwin,
 *      the Unix side of Tcl, and the MacOS X side of Tcl.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	libraryVariableName may be set, and the resource file opened.
 *
 *----------------------------------------------------------------------
 */

int
Tk_MacOSXOpenBundleResources(Tcl_Interp *interp,
        char *bundleName,
        char *libraryPath,
        int maxPathLen,
        int hasResourceFile)
{
    CFBundleRef bundleRef;
    CFStringRef bundleNameRef;
    
    libraryPath[0] = '\0';
    
    bundleNameRef = CFStringCreateWithCString(NULL, 
            bundleName, kCFStringEncodingUTF8);
            
    bundleRef = CFBundleGetBundleWithIdentifier(bundleNameRef);
    CFRelease(bundleNameRef);
    
    if (bundleRef == 0) {
        return TCL_ERROR;
    } else {
        CFURLRef libURL;
        
        if (hasResourceFile) {
            short refNum;
            refNum = CFBundleOpenBundleResourceMap(bundleRef);
        }
        
	libURL = CFBundleCopyResourceURL(bundleRef, 
	        CFSTR("Scripts"), 
		NULL, 
		NULL);
        
        if (libURL != NULL) {
            /* 
             * FIXME: This is a quick fix, it is probably not right 
             * for internationalization. 
             */
            
            if (CFURLGetFileSystemRepresentation (libURL, true,
                    libraryPath, maxPathLen)) {
            }
            CFRelease(libURL);
        } else {
            return TCL_ERROR;
        }
    }
    
    return TCL_OK;
}
