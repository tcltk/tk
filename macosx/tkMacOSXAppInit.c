/* 
 * tkAppInit.c --
 *
 *        Provides a default version of the Tcl_AppInit procedure for
 *        use in wish and similar Tk-based applications.
 *
 * Copyright (c) 1993 The Regents of the University of California.
 * Copyright (c) 1994-1997 Sun Microsystems, Inc.
 * Copyright 2001, Apple Computer, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkMacOSXAppInit.c,v 1.1.2.3 2001/10/19 07:14:18 wolfsuit Exp $
 */
#include <pthread.h>
#include "tk.h"
#include "tclInt.h"
#include "locale.h"

#include <Carbon/Carbon.h>
#include "tkMacOSX.h"
#include "tkMacOSXEvent.h"

#ifndef MAX_PATH_LEN
    #define MAX_PATH_LEN 1024
#endif

extern void TkMacOSXInitAppleEvents(Tcl_Interp *interp);
extern void TkMacOSXInitMenus(Tcl_Interp *interp);

/*
 * The following variable is a special hack that is needed in order for
 * Sun shared libraries to be used for Tcl.
 */

extern int matherr();
int *tclDummyMathPtr = (int *) matherr;

/*
 * If the App is in an App package, then we want to add the Scripts
 * directory to the auto_path.  But we have to wait till after the
 * Tcl_Init is run, or it gets blown away.  This stores what we
 * figured out in main.
 */
 
char scriptPath[MAX_PATH_LEN + 1];

extern Tcl_Interp *gStdoutInterp;

#ifdef TK_TEST
extern int                Tcltest_Init _ANSI_ARGS_((Tcl_Interp *interp));
extern int                Tktest_Init _ANSI_ARGS_((Tcl_Interp *interp));
#endif /* TK_TEST */

/*
 *----------------------------------------------------------------------
 *
 * main --
 *
 *        This is the main program for the application.
 *
 * Results:
 *        None: Tk_Main never returns here, so this procedure never
 *        returns either.
 *
 * Side effects:
 *        Whatever the application does.
 *
 *----------------------------------------------------------------------
 */

int
main(argc, argv)
    int argc;                        /* Number of command-line arguments. */
    char **argv;                /* Values of command-line arguments. */
{
    Tcl_Interp *interp;
    int textEncoding; /* 
                       * Variable used to take care of
                       * lazy font initialization
                       */
    CFBundleRef bundleRef;

    /*
     * The following #if block allows you to change the AppInit
     * function by using a #define of TCL_LOCAL_APPINIT instead
     * of rewriting this entire file.  The #if checks for that
     * #define and uses Tcl_AppInit if it doesn't exist.
     */
    
#ifndef TK_LOCAL_APPINIT
#define TK_LOCAL_APPINIT Tcl_AppInit    
#endif
    extern int TK_LOCAL_APPINIT _ANSI_ARGS_((Tcl_Interp *interp));


    /*
     * NB - You have to swap in the Tk Notifier BEFORE you start up the
     * Tcl interpreter for now.  It probably should work to do this
     * in the other order, but for now it doesn't seem to.
     */
     
    scriptPath[0] = '\0';
    
    Tk_MacOSXSetupTkNotifier();

    /*
     * The following #if block allows you to change how Tcl finds the startup
     * script, prime the library or encoding paths, fiddle with the argv,
     * etc., without needing to rewrite Tk_Main().  Note, if you use this
     * hook, then I won't do the CFBundle lookup, since if you are messing
     * around at this level, you probably don't want me to do this for you...
     */
    
#ifdef TK_LOCAL_MAIN_HOOK
    extern int TK_LOCAL_MAIN_HOOK _ANSI_ARGS_((int *argc, char ***argv));
    TK_LOCAL_MAIN_HOOK(&argc, &argv);
#else

    /*
     * On MacOS X, we look for a file in the Resources/Scripts directory
     * called AppMain.tcl and if found, we set argv[1] to that, so that
     * the rest of the code will find it, and add the Scripts folder to
     * the auto_path.  If we don't find the startup script, we just bag
     * it, assuming the user is starting up some other way.
     */
    
    bundleRef = CFBundleGetMainBundle();
    
    if (bundleRef != NULL) {
        CFURLRef appMainURL;
        appMainURL = CFBundleCopyResourceURL(bundleRef, 
                CFSTR("AppMain"), 
                CFSTR("tcl"), 
                CFSTR("Scripts"));

        if (appMainURL != NULL) {
            CFURLRef scriptFldrURL;
            char *startupScript = malloc(MAX_PATH_LEN + 1);
                            
            if (CFURLGetFileSystemRepresentation (appMainURL, true,
                    startupScript, MAX_PATH_LEN)) {
                TclSetStartupScriptFileName(startupScript);
                scriptFldrURL = CFBundleCopyResourceURL(bundleRef,
                        CFSTR("Scripts"),
                        NULL,
                        NULL);
                CFURLGetFileSystemRepresentation(scriptFldrURL, 
                        true, scriptPath, MAX_PATH_LEN);
                CFRelease(scriptFldrURL);
            } else {
                free(startupScript);
            }
            CFRelease(appMainURL);
        }
    }

#endif
         
    textEncoding=GetApplicationTextEncoding();
    
    /*
     * Now add the scripts folder to the auto_path.
     */
     
    Tk_Main(argc,argv,TK_LOCAL_APPINIT);
    return 0;                        /* Needed only to prevent compiler warning. */
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_AppInit --
 *
 *        This procedure performs application-specific initialization.
 *        Most applications, especially those that incorporate additional
 *        packages, will have their own version of this procedure.
 *
 * Results:
 *        Returns a standard Tcl completion code, and leaves an error
 *        message in the interp's result if an error occurs.
 *
 * Side effects:
 *        Depends on the startup script.
 *
 *----------------------------------------------------------------------
 */

int
Tcl_AppInit(interp)
    Tcl_Interp *interp;                /* Interpreter for application. */
{        
    SInt16 refNum;
    char tclLibPath[MAX_PATH_LEN], tkLibPath[MAX_PATH_LEN];
    Tcl_Obj *pathPtr;
    
    Tk_MacOSXOpenBundleResources (interp, "com.tcltk.tcllibrary", 
        tclLibPath, MAX_PATH_LEN, 0);

    if (tclLibPath[0] != '\0') {
        Tcl_SetVar(interp, "tcl_library", tclLibPath, TCL_GLOBAL_ONLY);
        Tcl_SetVar(interp, "tclDefaultLibrary", tclLibPath,
            TCL_GLOBAL_ONLY);
        Tcl_SetVar(interp, "tcl_pkgPath", tclLibPath, TCL_GLOBAL_ONLY);
    }
    
    if (Tcl_Init(interp) == TCL_ERROR) {
        return TCL_ERROR;
    }    

    Tk_MacOSXOpenBundleResources (interp, "com.tcltk.tklibrary", 
            tkLibPath, MAX_PATH_LEN, 1);

    /* 
     * FIXME: This is currently a hack...  I set the tcl_library, and
     * tk_library, but apparently that's not enough to get slave interpreters,
     * even unsafe ones, to find the library code.  They seem to ignore 
     * this and look at the var set in TclGetLibraryPath.  So I override
     * that here.  There must be a better way to do this!
     */
         
    if (tclLibPath[0] != '\0') {
        pathPtr = Tcl_NewStringObj(tclLibPath, -1);
    } else {
        Tcl_Obj *pathPtr = TclGetLibraryPath();
    }

    if (tkLibPath[0] != '\0') {
        Tcl_Obj *objPtr;
            
        Tcl_SetVar(interp, "tk_library", tkLibPath, TCL_GLOBAL_ONLY);
        objPtr = Tcl_NewStringObj(tkLibPath, -1);
        Tcl_ListObjAppendElement(NULL, pathPtr, objPtr);
    }
    
    TclSetLibraryPath(pathPtr);

    if (Tk_Init(interp) == TCL_ERROR) {
        return TCL_ERROR;
    }
    Tcl_StaticPackage(interp, "Tk", Tk_Init, Tk_SafeInit);

    if (scriptPath[0] != '\0') {
        Tcl_SetVar(interp, "auto_path", scriptPath,
                TCL_GLOBAL_ONLY|TCL_LIST_ELEMENT|TCL_APPEND_VALUE);
    }
    
    TkMacOSXInitAppleEvents(interp);
    TkMacOSXInitMenus(interp);
    
#ifdef TK_TEST
    if (Tcltest_Init(interp) == TCL_ERROR) {
        return TCL_ERROR;
    }
    Tcl_StaticPackage(interp, "Tcltest", Tcltest_Init,
            (Tcl_PackageInitProc *) NULL);
    if (Tktest_Init(interp) == TCL_ERROR) {
        return TCL_ERROR;
    }
    Tcl_StaticPackage(interp, "Tktest", Tktest_Init,
            (Tcl_PackageInitProc *) NULL);
#endif /* TK_TEST */

    /*
     * If we don't have a TTY, then use the Tk based console
     * interpreter instead.
     */

    if (ttyname(0) == NULL) {
        Tk_InitConsoleChannels(interp);
        Tcl_RegisterChannel(interp, Tcl_GetStdChannel(TCL_STDIN));
        Tcl_RegisterChannel(interp, Tcl_GetStdChannel(TCL_STDOUT));
        Tcl_RegisterChannel(interp, Tcl_GetStdChannel(TCL_STDERR));
        if (Tk_CreateConsoleWindow(interp) == TCL_ERROR) {
            goto error;
        }
        Tcl_Eval(interp, "console show");
    }
    
    /*
     * Call the init procedures for included packages.  Each call should
     * look like this:
     *
     * if (Mod_Init(interp) == TCL_ERROR) {
     *     return TCL_ERROR;
     * }
     *
     * where "Mod" is the name of the module.
     */

    /*
     * Call Tcl_CreateCommand for application-specific commands, if
     * they weren't already created by the init procedures called above.
     */

    
    /*
     * Specify a user-specific startup file to invoke if the application
     * is run interactively.  Typically the startup file is "~/.apprc"
     * where "app" is the name of the application.  If this line is deleted
     * then no user-specific startup file will be run under any conditions.
     */
     
    Tcl_SetVar(interp, "tcl_rcFileName", "~/.wishrc", TCL_GLOBAL_ONLY);

    return TCL_OK;

    error:
    return TCL_ERROR;
}

