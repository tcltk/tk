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


/* Initialize variables for later use.  */
static PRINTDLG pd;
static  DOCINFO di;
int copies, paper_width, paper_height, dpi_x, dpi_y;
char *localPrinterName;
PDEVMODE returnedDevmode;
PDEVMODE localDevmode;
static HDC hDC;

static int PrintSelectPrinter(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[]);
int PrintOpenPrinter(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[]);
int PrintClosePrinter(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const objv[]);
int Winprint_Init(Tcl_Interp * interp);

/*----------------------------------------------------------------------
 *
 * PrintSelectPrinter--
 *
 *  Main dialog for selecting printer and initializing data for print job.
 *
 * Results:
 *	Printer selected.
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
	di.lpszDocName = "Tk Output";
    

	/* Copy print attributes to local structure. */ 
	returnedDevmode = (PDEVMODE)GlobalLock(pd.hDevMode);	
	localDevmode = (LPDEVMODE)HeapAlloc(
					    GetProcessHeap(), 
					    HEAP_ZERO_MEMORY | HEAP_GENERATE_EXCEPTIONS, 
					    returnedDevmode->dmSize);
                        
	if (localDevmode !=NULL) 
	    {
		memcpy(
		       (LPVOID)localDevmode,
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
 * Winprint_Init--
 *
 *    Initializes printing module on Windows..
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
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 *  End:
 */ 
