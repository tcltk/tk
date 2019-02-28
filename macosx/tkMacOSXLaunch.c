/*
 * tkMacOSXLaunch.c --
 * Launches URL's using native API's on OS X without shelling out to "/usr/bin/open". Also gets and sets default app handlers.
 * Copyright (c) 2015-2019 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include <tcl.h>
#include <tk.h>
#undef panic
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <Carbon/Carbon.h>
#include <ApplicationServices/ApplicationServices.h>
#define panic Tcl_Panic


/*Tcl function to launch URL with default app.*/
int LaunchURL(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]) {

  if(objc != 2) {
    Tcl_WrongNumArgs(ip, 1, objv, "url");
    return TCL_ERROR;
  }


  /* Get url string, convert to CFURL. */
  CFStringRef url = CFStringCreateWithCString(NULL, Tcl_GetString(objv[1]),
					      kCFStringEncodingUTF8);
  CFURLRef launchurl = CFURLCreateWithString(kCFAllocatorDefault, url, NULL);
  CFRelease(url);

  /* Fire url in default app. */
  LSOpenCFURLRef(launchurl, NULL);

  CFRelease(launchurl);

  return TCL_OK;

}

/*Tcl function to launch file with default app.*/
int LaunchFile(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]) {

  if(objc != 2) {
    Tcl_WrongNumArgs(ip, 1, objv, "file");
    return TCL_ERROR;
  }

  /* Get url string, convert to CFURL. */
  CFStringRef url = CFStringCreateWithCString(NULL, Tcl_GetString(objv[1]),
					      kCFStringEncodingUTF8);
  CFRelease(url);

  CFURLRef launchurl = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, url, kCFURLPOSIXPathStyle, false);

  /* Fire url in default app. */
  LSOpenCFURLRef(launchurl, NULL);
  CFRelease(launchurl);

  return TCL_OK;

}


/*Tcl function to get path to app bundle.*/
int GetAppPath(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]) {

  CFURLRef mainBundleURL = CFBundleCopyBundleURL(CFBundleGetMainBundle());

  
  /* Convert the URL reference into a string reference. */
  CFStringRef appPath = CFURLCopyFileSystemPath(mainBundleURL, kCFURLPOSIXPathStyle);
 
  /* Get the system encoding method. */
  CFStringEncoding encodingMethod = CFStringGetSystemEncoding();
 
  /* Convert the string reference into a C string. */
  char *path = CFStringGetCStringPtr(appPath, encodingMethod);

  Tcl_SetResult(ip, path, NULL);

  CFRelease(mainBundleURL);
  CFRelease(appPath);
  return TCL_OK;

}

/*Tcl function to launch file with default app.*/
int GetDefaultApp(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]) {

  if(objc != 2) {
    Tcl_WrongNumArgs(ip, 1, objv, "url");
    return TCL_ERROR;
  }

  /* Get url string, convert to CFStringRef. */
  CFStringRef url = CFStringCreateWithCString(NULL, Tcl_GetString(objv[1]),
					      kCFStringEncodingUTF8);

  CFStringRef defaultApp;
  defaultApp =  LSCopyDefaultHandlerForURLScheme(url);

  OSStatus result;
  CFURLRef appURL = NULL;
  result = LSFindApplicationForInfo(kLSUnknownCreator, defaultApp, NULL, NULL, &appURL);

  /* Convert the URL reference into a string reference. */
  CFStringRef appPath = CFURLCopyFileSystemPath(appURL, kCFURLPOSIXPathStyle);
 
  /* Get the system encoding method. */
  CFStringEncoding encodingMethod = CFStringGetSystemEncoding();
 
  /* Convert the string reference into a C string. */
  char *path = CFStringGetCStringPtr(appPath, encodingMethod);

  Tcl_SetResult(ip, path, NULL);

  CFRelease(defaultApp);
  CFRelease(appPath);
  CFRelease(url);

  return TCL_OK;

}

/*Tcl function to set default app for URL.*/
int SetDefaultApp(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]) {

  if(objc != 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "url path");
    return TCL_ERROR;
  }
  
  /* Get url and path strings, convert to CFStringRef. */
  CFStringRef url = CFStringCreateWithCString(NULL, Tcl_GetString(objv[1]),
					      kCFStringEncodingUTF8);
  CFURLRef appURL = NULL;
  CFBundleRef bundle = NULL;

  CFStringRef apppath = CFStringCreateWithCString(NULL, Tcl_GetString(objv[2]),  kCFStringEncodingUTF8);

  /* Convert filepath to URL, create bundle object, get bundle ID. */
  appURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, apppath, kCFURLPOSIXPathStyle, false);
  bundle = CFBundleCreate(NULL, appURL);

  CFStringRef bundleID = CFBundleGetIdentifier(bundle); 

  /* Finally, set default app. */
  OSStatus err;
  err= LSSetDefaultHandlerForURLScheme(url, bundleID);

  /* Free memory.  */
  CFRelease(url);
  CFRelease(apppath);
  CFRelease(bundleID);

  return TCL_OK;

}


/*Initalize the package in the tcl interpreter, create Tcl commands. */
int  TkMacOSXLauncher_Init (Tcl_Interp *interp) {

 
  if (Tcl_InitStubs(interp, "8.5", 0) == NULL) {
    return TCL_ERROR;  
  }
  if (Tk_InitStubs(interp, "8.5", 0) == NULL) {
    return TCL_ERROR;
  }


  Tcl_CreateObjCommand(interp, "::tk::mac::LaunchURL", LaunchURL,(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateObjCommand(interp, "::tk::mac::LaunchFile", LaunchFile,(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateObjCommand(interp, "::tk::mac::GetAppPath", GetAppPath,(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateObjCommand(interp, "::tk::mac::GetDefaultApp", GetDefaultApp,(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateObjCommand(interp, "::tk::mac::SetDefaultApp",SetDefaultApp,(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);

 
  return TCL_OK;
	
}






