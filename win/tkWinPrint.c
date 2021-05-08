/*
 * tkWinPrint.c --
 *
 *      This module implements Win32 printer access.
 *
 * Copyright © 1998-2019 Harald Oehlmann, Elmicron GmbH
 * Copyright © 2009 Michael I. Schwartz.
 * Copyright © 2018 Microsoft Corporation.
 * Copyright © 2021 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include <windows.h>
#include <winspool.h>
#include <commdlg.h>
#include <wingdi.h>
#include <tcl.h>
#include <tk.h>
#include "tkWinInt.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "tkWinHDC.h"

/* Initialize variables for later use.  */
static PRINTDLG pd;
static  DOCINFO di;
int copies, paper_width, paper_height, dpi_x, dpi_y;
char *localPrinterName;
PDEVMODE returnedDevmode;
PDEVMODE localDevmode;
static HDC hDC;

/*
 * Prototypes for functions used only in this file.
 */

static int PrintSelectPrinter(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[]);
int PrintOpenPrinter(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[]);
int PrintClosePrinter(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[]);
static int PrintOpenDoc(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[]);
static int PrintCloseDoc(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[]);
static int PrintOpenPage(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[]);
static int PrintClosePage(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[]);
int PrintGetHDC(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[]);
HDC get_hdc(void);
int Winprint_Init(Tcl_Interp * interp);

/*----------------------------------------------------------------------
 *
 * PrintSelectPrinter--
 *
 *  Main dialog for selecting printer and initializing data for print job.
 *
 * Results:
 *  Printer selected.
 *
 *----------------------------------------------------------------------
 */

static int PrintSelectPrinter(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[])
{

    (void) clientData;
    (void) argc;
    (void) objv;

    returnedDevmode = NULL;
    localDevmode = NULL;
    localPrinterName = NULL;
    copies = 0;
    paper_width = 0;
    paper_height = 0;
    dpi_x = 0;
    dpi_y = 0;

    /* Set up print dialog and initalize property structure. */

    ZeroMemory( &pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = GetDesktopWindow();
    pd.Flags = PD_RETURNDC | PD_HIDEPRINTTOFILE  | PD_DISABLEPRINTTOFILE | PD_NOSELECTION;
	
    if (PrintDlg(&pd) == TRUE) {
	hDC = pd.hDC;
	if (hDC = NULL) {
	    Tcl_AppendResult(interp, "can't allocate printer DC", NULL);
	    return TCL_ERROR;
	} 	

	/*Get document info.*/
	ZeroMemory( &di, sizeof(di));
	di.cbSize = sizeof(di);
	di.lpszDocName = "Tk Print Output";
    

	/* Copy print attributes to local structure. */ 
	returnedDevmode = (PDEVMODE)GlobalLock(pd.hDevMode);	
	localDevmode = (LPDEVMODE)HeapAlloc(GetProcessHeap(), 
					    HEAP_ZERO_MEMORY | HEAP_GENERATE_EXCEPTIONS, 
					    returnedDevmode->dmSize);
                        
	if (localDevmode !=NULL) 
	    {
		memcpy((LPVOID)localDevmode,
		       (LPVOID)returnedDevmode, 
		       returnedDevmode->dmSize);
		
		/* Get values from user-set and built-in properties. */
		localPrinterName = (char*) localDevmode->dmDeviceName;
		dpi_y = localDevmode->dmYResolution;
		dpi_x =  localDevmode->dmPrintQuality;
		paper_height = (int) localDevmode->dmPaperLength;
		paper_width = (int) localDevmode->dmPaperWidth;
		copies = pd.nCopies;
	    }
	else
	    {
		localDevmode = NULL;
	    }
	if (pd.hDevMode !=NULL) 
	    {
		GlobalFree(pd.hDevMode);
	    }
    }

        
    /* 
     * Store print properties and link variables 
     * so they can be accessed from script level.
     */
 
    char *varlink1 = Tcl_Alloc(100 * sizeof(char));
    char **varlink2 =  (char **)Tcl_Alloc(sizeof(char *));
    *varlink2 = varlink1;
    strcpy (varlink1, localPrinterName);		
	  
    Tcl_LinkVar(interp, "::tk::print::printer_name", (char*)varlink2, TCL_LINK_STRING | TCL_LINK_READ_ONLY);
    Tcl_LinkVar(interp, "::tk::print::copies", (char *)&copies, TCL_LINK_INT |  TCL_LINK_READ_ONLY);
    Tcl_LinkVar(interp, "::tk::print::dpi_x", (char *)&dpi_x, TCL_LINK_INT | TCL_LINK_READ_ONLY);
    Tcl_LinkVar(interp, "::tk::print::dpi_y", (char *)&dpi_y, TCL_LINK_INT |  TCL_LINK_READ_ONLY);
    Tcl_LinkVar(interp, "::tk::print::paper_width", (char *)&paper_width, TCL_LINK_INT |  TCL_LINK_READ_ONLY);
    Tcl_LinkVar(interp, "::tk::print::paper_height", (char *)&paper_height, TCL_LINK_INT | TCL_LINK_READ_ONLY);
   
    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintOpenPrinter--
 *
 *     Open the given printer.
 *
 * Results:
 *      Opens the selected printer.
 *
 * -------------------------------------------------------------------------
 */

int PrintOpenPrinter(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[])
{
    (void) clientData;
    
    if (argc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "printer");
	return TCL_ERROR;
    }

    char *printer = Tcl_GetString(objv[2]);
    if (hDC == NULL) {
	return TCL_ERROR;
    }
    OpenPrinter(printer, &hDC, NULL);
    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintClosePrinter--
 *
 *    Closes the given printer.
 *
 * Results:
 *    Printer closed.
 *
 * -------------------------------------------------------------------------
 */

int PrintClosePrinter(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[])
{
    (void) clientData;
    (void) argc;
    (void) objv;
    
    ClosePrinter(hDC);
    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintOpenDoc--
 *
 *     Opens the document for printing.
 *
 * Results:
 *      Opens the print document.
 *
 * -------------------------------------------------------------------------
 */

int PrintOpenDoc(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[])
{

    (void) clientData;
    (void) argc;
    (void) objv;

    int output = 0;

    if (hDC == NULL) {
	return TCL_ERROR;
    }

    /* 
     * Start printing. 
     */
    output = StartDoc(hDC, &di);
    if (output <= 0) {
	Tcl_AppendResult(interp, "unable to start document", NULL);
	return TCL_ERROR;		
    } 
   
    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintCloseDoc--
 *
 *     Closes the document for printing.
 *
 * Results:
 *      Closes the print document.
 *
 * -------------------------------------------------------------------------
 */


int PrintCloseDoc(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[])
{

    (void) clientData;
    (void) argc;
    (void) objv;
    
    if ( EndDoc(hDC) <= 0) {
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintOpenPage--
 *
 *    Opens a page for printing.
 *
 * Results:
 *      Opens the print page.
 *
 * -------------------------------------------------------------------------
 */

int PrintOpenPage(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[])
{

    (void) clientData;
    (void) argc;
    (void) objv;

    /*Start an individual page.*/
    if ( StartPage(hDC) <= 0) {
	Tcl_AppendResult(interp, "unable to start page", NULL);
	return TCL_ERROR;
    }
	
    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintClosePage--
 *
 *    Closes the printed page.
 *
 * Results:
 *    Closes the page.
 *
 * -------------------------------------------------------------------------
 */

int PrintClosePage(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[])
{

    (void) clientData;
    (void) argc;
    (void) objv;
    
    if ( EndPage(hDC) <= 0) {
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 * --------------------------------------------------------------------------
 *
 * PrintGetHDC--
 *
 *    Gets the device context for the printer.
 *
 * Results:
 *    Returns HDC.
 *
 * -------------------------------------------------------------------------
 */

int PrintGetHDC(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[])
{

    (void) clientData;
    (void) argc;
    (void) objv;

   hDC = CreateDC( L"WINSPOOL", localPrinterName, NULL, NULL);
    
    if ( hDC == NULL) {
	return TCL_ERROR;
    }

    // get_hdc();
    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintGetHDC--
 *
 *    Gets the device context for the printer.
 *
 * Results:
 *    Returns HDC.
 *
 * -------------------------------------------------------------------------
 */


HDC get_hdc(void) {

    return hDC;

}

/*
 * --------------------------------------------------------------------------
 *
 * Winprint_Init--
 *
 *    Initializes printing module on Windows.
 *
 * Results:
 *    Module initialized.
 *
 * -------------------------------------------------------------------------
 */

int Winprint_Init(Tcl_Interp * interp)
{
    Tcl_CreateObjCommand(interp, "::tk::print::_selectprinter", PrintSelectPrinter, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::print::_openprinter", PrintOpenPrinter, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::print::_closeprinter", PrintClosePrinter, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::print::_opendoc", PrintOpenDoc, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::print::_closedoc", PrintCloseDoc, NULL, NULL); 
    Tcl_CreateObjCommand(interp, "::tk::print::_openpage", PrintOpenPage, NULL, NULL); 
    Tcl_CreateObjCommand(interp, "::tk::print::_closepage", PrintClosePage, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::print::_gethdc", PrintGetHDC, NULL, NULL); 
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 *  End:
 */ 
