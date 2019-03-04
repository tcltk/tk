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

/*Forward declarations of functions.*/
int TkMacOSXLaunchURL(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]);
int TkMacOSXLaunchFile(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]);
int TkMacOSXGetAppPath(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]);
int TkMacOSXGetDefaultApp(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]);
int TkMacOSXSetDefaultApp(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]);


/*Tcl function to launch URL with default app.*/
int TkMacOSXLaunchURL(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]) {

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
int TkMacOSXLaunchFile(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]) {

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
int TkMacOSXGetAppPath(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]) {

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

/*Tcl function to get default app for URL.*/
int TkMacOSXGetDefaultApp(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]) {

  if(objc != 2) {
    Tcl_WrongNumArgs(ip, 1, objv, "url");
    return TCL_ERROR;
  }

  /* Get url string, convert to CFStringRef. */
  CFStringRef url = CFStringCreateWithCString(NULL, Tcl_GetString(objv[1]),
					      kCFStringEncodingUTF8);

  /*Ensure arg is well-formed.*/
  NSString *testString = (NSString*) url;
  if ([testString rangeOfString:@"://"].location == NSNotFound) {
    NSLog(@"Error: please provide well-formed URL in url:// format.");
    return TCL_OK;
  }
     
  /*Get default app for URL.*/
  CFURLRef  defaultApp = CFURLCreateWithString(kCFAllocatorDefault, url, NULL);
  CFStringRef appURL = LSCopyDefaultApplicationURLForURL(defaultApp, kLSRolesAll, nil);

  /* Convert the URL reference into a string reference. */
  CFStringRef appPath = CFURLCopyFileSystemPath(appURL, kCFURLPOSIXPathStyle);

 
  /* Get the system encoding method. */
  CFStringEncoding encodingMethod = CFStringGetSystemEncoding();
 
  /* Convert the string reference into a C string. */
  char *path = CFStringGetCStringPtr(appPath, encodingMethod);

  Tcl_SetResult(ip, path, NULL);

  CFRelease(defaultApp);
  CFRelease(appPath);
  CFRelease(appURL);
  CFRelease(url);

  return TCL_OK;

}

/*Tcl function to set default app for URL.*/
int TkMacOSXSetDefaultApp(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *CONST objv[]) {

  if(objc != 3) {
    Tcl_WrongNumArgs(ip, 1, objv, "url path");
    return TCL_ERROR;
  }
  
  /* Get url and path strings, convert to CFStringRef. */
  CFStringRef url = CFStringCreateWithCString(NULL, Tcl_GetString(objv[1]),
					      kCFStringEncodingUTF8);

  
  /*Ensure arg is well-formed.*/
  NSString *testString = (NSString*) url;
  if ([testString rangeOfString:@"://"].location == NSNotFound) {
    NSLog(@"Error: please provide well-formed URL in url:// format.");
    return TCL_OK;
  }

  /*Strip colon and slashes because the API to set default handlers does not use them.*/
  NSString *setURL = [(NSString*)url stringByReplacingOccurrencesOfString:@"://" withString:@""];

  CFURLRef appURL = NULL;
  CFBundleRef bundle = NULL;

  CFStringRef apppath = CFStringCreateWithCString(NULL, Tcl_GetString(objv[2]),  kCFStringEncodingUTF8);

  /* Convert filepath to URL, create bundle object, get bundle ID. */
  appURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, apppath, kCFURLPOSIXPathStyle, false);
  bundle = CFBundleCreate(NULL, appURL);

  CFStringRef bundleID = CFBundleGetIdentifier(bundle); 

  /* Finally, set default app. */
  OSStatus err;
  err= LSSetDefaultHandlerForURLScheme((CFStringRef *)setURL, bundleID);

  /* Free memory.  */
  CFRelease(url);
  CFRelease(apppath);
  CFRelease(bundleID);

  return TCL_OK; 

}


/*Initalize the package in the tcl interpreter, create Tcl commands. */
int  TkMacOSXLaunch_Init (Tcl_Interp *interp) {


  Tcl_CreateObjCommand(interp, "::tk::mac::LaunchURL", TkMacOSXLaunchURL,(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateObjCommand(interp, "::tk::mac::LaunchFile", TkMacOSXLaunchFile,(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateObjCommand(interp, "::tk::mac::GetAppPath", TkMacOSXGetAppPath,(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateObjCommand(interp, "::tk::mac::GetDefaultApp",TkMacOSXGetDefaultApp,(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
  Tcl_CreateObjCommand(interp, "::tk::mac::SetDefaultApp",TkMacOSXSetDefaultApp,(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);

 
  return TCL_OK;
	
}






