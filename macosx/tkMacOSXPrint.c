/*
 * tkMacOSXPrint.c --
 *
 *      This module implements native printing dialogs for macOS.
 *
 * Copyright © 2006 Apple Inc.
 * Copyright © 2011-2021 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include <tcl.h>
#include <tk.h>
#include <tkInt.h>
#include <Cocoa/Cocoa.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <ApplicationServices/ApplicationServices.h>
#include <tkMacOSXInt.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include "tkMacOSXImage.h"
#include "tkMacOSXPrivate.h"

NSString * fileName = nil;
CFStringRef urlFile = NULL;

/*Forward declaration of functions.*/
static Tcl_ObjCmdProc StartPrint;
static OSStatus	FinishPrint(NSString *file, int buttonValue);
static Tcl_ObjCmdProc MakePDF;
int			MacPrint_Init(Tcl_Interp * interp);

/* Delegate class for print dialogs. */
@interface PrintDelegate: NSObject
    - (id) init;
    - (void) printPanelDidEnd: (NSPrintPanel *) printPanel
		   returnCode: (int) returnCode
		  contextInfo: (void *) contextInfo;
@end

@implementation PrintDelegate
- (id) init {
    self = [super init];
    return self;
}

- (void) printPanelDidEnd: (NSPrintPanel *) printPanel
	       returnCode: (int) returnCode
	      contextInfo: (void *) contextInfo {
    (void) printPanel;
    (void) contextInfo;

    /*
     * Pass returnCode to FinishPrint function to determine how to
     * handle.
     */

    FinishPrint(fileName, returnCode);
}
@end

/*
 *----------------------------------------------------------------------
 *
 * StartPrint --
 *
 *	Launch native print dialog.
 *
 * Results:
 *	Configures values and starts print process.
 *
 *----------------------------------------------------------------------
 */

int
StartPrint(
    TCL_UNUSED(void *),
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj *const objv[])
{
    NSPrintInfo * printInfo = [NSPrintInfo sharedPrintInfo];
    NSPrintPanel * printPanel = [NSPrintPanel printPanel];
    int accepted;
    PMPrintSession printSession;
    PMPrintSettings printSettings;
    OSStatus status = noErr;

    /* Check for proper number of arguments. */
    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "file");
	return TCL_ERROR;
    }

    fileName = [NSString stringWithUTF8String: Tcl_GetString(objv[1])];
    urlFile = (CFStringRef) fileName;
    CFRetain(urlFile);

    /* Initialize the delegate for the callback from the page panel. */
    PrintDelegate * printDelegate = [[PrintDelegate alloc] init];

    status = PMCreateSession( & printSession);
    if (status != noErr) {
	NSLog(@ "Error creating print session.");
	return TCL_ERROR;
    }

    status = PMCreatePrintSettings( & printSettings);
    if (status != noErr) {
	NSLog(@ "Error creating print settings.");
	return TCL_ERROR;
    }

    status = PMSessionDefaultPrintSettings(printSession, printSettings);
    if (status != noErr) {
	NSLog(@ "Error creating default print settings.");
	return TCL_ERROR;
    }

    printSession = (PMPrintSession)[printInfo PMPrintSession];
    (void)(PMPageFormat)[printInfo PMPageFormat];
    printSettings = (PMPrintSettings)[printInfo PMPrintSettings];

    accepted = (int)[printPanel runModalWithPrintInfo: printInfo];
    [printDelegate printPanelDidEnd: printPanel
			 returnCode: accepted
			contextInfo: printInfo];

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * FinishPrint --
 *
 *	Handles print process based on input from dialog.
 *
 * Results:
 *	Completes print process.
 *
 *----------------------------------------------------------------------
 */

OSStatus
FinishPrint(
    NSString *file,
    int buttonValue)
{
    NSPrintInfo * printInfo = [NSPrintInfo sharedPrintInfo];
    PMPrintSession printSession;
    PMPageFormat pageFormat;
    PMPrintSettings printSettings;
    OSStatus status = noErr;
    CFStringRef mimeType = NULL;

    /*
     * If value passed here is NSCancelButton, return noErr;
     * otherwise printing will occur regardless of value.
     */
    if (buttonValue == NSModalResponseCancel) {
	return noErr;
    }

    status = PMCreateSession( & printSession);
    if (status != noErr) {
	NSLog(@ "Error creating print session.");
	return status;
    }

    status = PMCreatePrintSettings( & printSettings);
    if (status != noErr) {
	NSLog(@ "Error creating print settings.");
	return status;
    }

    status = PMSessionDefaultPrintSettings(printSession, printSettings);
    if (status != noErr) {
	NSLog(@ "Error creating default print settings.");
	return status;
    }

    printSession = (PMPrintSession)[printInfo PMPrintSession];
    pageFormat = (PMPageFormat)[printInfo PMPageFormat];
    printSettings = (PMPrintSettings)[printInfo PMPrintSettings];

    /*Handle print operation.*/
    if (buttonValue == NSModalResponseOK) {

	if (urlFile == NULL) {
	    NSLog(@ "Could not get file to print.");
	    return noErr;
	}

	fileName = file;

	CFURLRef printURL = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, urlFile, kCFURLPOSIXPathStyle, false);

	PMPrinter currentPrinter;
	PMDestinationType printDestination;

	/*Get the intended destination.*/
	status = PMSessionGetDestinationType(printSession, printSettings, & printDestination);

	/*Destination is printer. Send file to printer.*/
	if (status == noErr && printDestination == kPMDestinationPrinter) {

	    status = PMSessionGetCurrentPrinter(printSession, & currentPrinter);
	    if (status == noErr) {
		CFArrayRef mimeTypes;
		status = PMPrinterGetMimeTypes(currentPrinter, printSettings, & mimeTypes);
		if (status == noErr && mimeTypes != NULL) {
		    mimeType = CFSTR("application/pdf");
		    if (CFArrayContainsValue(mimeTypes, CFRangeMake(0, CFArrayGetCount(mimeTypes)), mimeType)) {
			status = PMPrinterPrintWithFile(currentPrinter, printSettings, pageFormat, mimeType, printURL);
			CFRelease(urlFile);
			return status;
		    }
		}
	    }
	}

	/* Destination is file. Determine how to handle. */
	if (status == noErr && printDestination == kPMDestinationFile) {
	    CFURLRef outputLocation = NULL;

	    status = PMSessionCopyDestinationLocation(printSession, printSettings, & outputLocation);
	    if (status == noErr) {
		/*Get the source file and target destination, convert to strings.*/
		CFStringRef sourceFile = CFURLCopyFileSystemPath(printURL, kCFURLPOSIXPathStyle);
		CFStringRef savePath = CFURLCopyFileSystemPath(outputLocation, kCFURLPOSIXPathStyle);
		NSString * sourcePath = (NSString * ) sourceFile;
		NSString * finalPath = (NSString * ) savePath;
		NSString * pathExtension = [finalPath pathExtension];
		NSFileManager * fileManager = [NSFileManager defaultManager];
		NSError * error = nil;

	/*
		 * Is the target file a PDF? If so, copy print file
		 * to output location.
		 */
		if ([pathExtension isEqualToString: @ "pdf"]) {

		    /*Make sure no file conflict exists.*/
		    if ([fileManager fileExistsAtPath: finalPath]) {
			[fileManager removeItemAtPath: finalPath error: &error];
		    }
		    if ([fileManager fileExistsAtPath: sourcePath]) {
			error = nil;
			[fileManager copyItemAtPath: sourcePath toPath: finalPath error: & error];
		    }
		    return status;
		}

		/*
		 * Is the target file PostScript? If so, run print file
		 * through CUPS filter to convert back to PostScript.
		 */

	      if ([pathExtension isEqualToString: @ "ps"]) {
		    char source[5012];
		    char target[5012];
		    [sourcePath getCString: source maxLength: (sizeof source) encoding: NSUTF8StringEncoding];
		    [finalPath getCString: target maxLength: (sizeof target) encoding: NSUTF8StringEncoding];
		    /*Make sure no file conflict exists.*/
		    if ([fileManager fileExistsAtPath: finalPath]) {
			[fileManager removeItemAtPath: finalPath error: &error];
		    }

		    /*
		     *  Fork and start new process with command string. Thanks to Peter da Silva
		     *  for assistance.
		     */
		    pid_t pid;
		    if ((pid = fork()) == -1) {
		      return -1;
		    } else if (pid == 0) {
		      /* Redirect output to file and silence debugging output.*/
		      dup2(open(target, O_RDWR | O_CREAT, 0777), 1);
		      dup2(open("/dev/null", O_WRONLY), 2);
		      execl("/usr/sbin/cupsfilter", "/usr/sbin/cupsfilter", "-m", "application/postscript", source, NULL);
		      exit(0);
		    }
	      return status;
	      }
	    }
	}

	/* Destination is preview. Open file in default application for PDF. */
	if ((status == noErr) && (printDestination == kPMDestinationPreview)) {
	    CFStringRef urlpath = CFURLCopyFileSystemPath(printURL, kCFURLPOSIXPathStyle);
	    NSString * path = (NSString * ) urlpath;
	    NSURL * url = [NSURL fileURLWithPath: path];
	    NSWorkspace * ws = [NSWorkspace sharedWorkspace];
	    [ws openURL: url];
	    status = noErr;
	    return status;
	}

	/*
	 * If destination is not printer, file or preview,
	 * we do not support it. Display alert.
	 */

	if (((status == noErr) && (printDestination != kPMDestinationPreview)) || ((status == noErr) && (printDestination != kPMDestinationFile)) || ((status == noErr) &&  (printDestination != kPMDestinationPrinter))) {

	    NSAlert * alert = [[[NSAlert alloc] init] autorelease];
	    [alert addButtonWithTitle: @ "OK"];

	    [alert setMessageText: @ "Unsupported Printing Operation"];
	    [alert setInformativeText: @ "This printing operation is not supported."];
	    [alert setAlertStyle: NSAlertStyleInformational];
	    [alert runModal];
	    return status;
	}
    }

    /* Return because cancel button was clicked. */
    if (buttonValue == NSModalResponseCancel) {
	PMRelease(printSession);
	return status;
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * MakePDF--
 *
 *	Converts a Tk canvas to PDF data.
 *
 * Results:
 *	Outputs PDF file.
 *
 *----------------------------------------------------------------------
 */

int MakePDF(
 TCL_UNUSED(void *),
 Tcl_Interp *ip,
 int objc,
 Tcl_Obj *const objv[])
{
    Tk_Window path;
    Drawable d;
    unsigned int width, height;
    CFDataRef pdfData;

    if (objc != 2) {
	Tcl_WrongNumArgs(ip, 1, objv, "path?");
	return TCL_ERROR;
    }

    /*Get window and render to PDF.*/

    path = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
    if (path == NULL) {
	return TCL_ERROR;
    }

    Tk_MakeWindowExist(path);
    Tk_MapWindow(path);
    d = Tk_WindowId(path);
    width = Tk_Width(path);
    height = Tk_Height(path);

    pdfData = CreatePDFFromDrawableRect(d, 0, 0, width, height);

    NSData *viewData = (NSData*)pdfData;
    [viewData writeToFile:@"/tmp/tk_canvas.pdf" atomically:YES];
    return TCL_OK;

}

/*
 *----------------------------------------------------------------------
 *
 * MacPrint_Init--
 *
 *	Initializes the printing module.
 *
 * Results:
 *	Printing module initialized.
 *
 *----------------------------------------------------------------------
 */

int MacPrint_Init(Tcl_Interp * interp) {
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
    Tcl_CreateObjCommand(interp, "::tk::print::_print", StartPrint, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::print::_printcanvas", MakePDF, NULL, NULL);
    [pool release];
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
