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
Tcl_HashTable *attribs;
static PRINTDLG pd;
static PAGESETUPDLG pgdlg;
static  DOCINFO di;
static int PrintSelectPrinter( Tcl_Interp *interp );

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

static int PrintSelectPrinter( Tcl_Interp *interp )
{

    HDC hDC;
    PDEVMODE returnedDevmode;
    PDEVMODE localDevmode;
    LPWSTR localPrinterName;
    int copies, paper_width, paper_height, dpi_x, dpi_y, new;
    Tcl_HashEntry *hPtr;

    returnedDevmode = NULL;
    localDevmode = NULL;
    localPrinterName = NULL;
    copies, paper_width, paper_height, dpi_x, dpi_y, new = 0;

    /* Set up print dialog and initalize property structure. */

    ZeroMemory( &pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = GetDesktopWindow();
    pd.Flags = PD_RETURNDC | PD_HIDEPRINTTOFILE  | PD_DISABLEPRINTTOFILE | PD_NOSELECTION;
	
    if (PrintDlg(&pd) == TRUE) {
	hDC = pd.hDC;
	if (hDC == NULL) {
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
                        
	if (NULL != localDevmode) 
	    {
		memcpy(
		       (LPVOID)localDevmode,
		       (LPVOID)returnedDevmode, 
		       returnedDevmode->dmSize);
		/* Get printer name and number of copies set by user. */
		localPrinterName = localDevmode->dmDeviceName;
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
     * Get printer resolution and paper size. 
     */
    dpi_x = GetDeviceCaps(hDC, LOGPIXELSX);
    dpi_y = GetDeviceCaps(hDC, LOGPIXELSY);
    paper_width = GetDeviceCaps(hDC, PHYSICALWIDTH);
    paper_height = GetDeviceCaps(hDC, PHYSICALHEIGHT);
	
    /* 
     * Store print properties in hash table and link variables 
     * so they can be accessed from script level.
     */
    hPtr = Tcl_CreateHashEntry (attribs, "hDC", &new);
    Tcl_SetHashValue (hPtr, &hDC);
    hPtr = Tcl_CreateHashEntry (attribs, "copies", &new);
    Tcl_SetHashValue (hPtr, &copies);
    Tcl_LinkVar(interp, "::tk::print::copies", &copies, TCL_LINK_INT);
    hPtr = Tcl_CreateHashEntry (attribs, "dpi_x", &new);
    Tcl_SetHashValue (hPtr, &dpi_x);
    Tcl_LinkVar(interp, "::tk::print::dpi_x", &dpi_x, TCL_LINK_INT);
    hPtr = Tcl_CreateHashEntry (attribs, "dpi_y", &new);
    Tcl_SetHashValue (hPtr, &dpi_y);
    Tcl_LinkVar(interp, "::tk::print::dpi_y", &dpi_y, TCL_LINK_INT);
    hPtr = Tcl_CreateHashEntry (attribs, "paper_width", &new);
    Tcl_SetHashValue (hPtr, &paper_width);
    Tcl_LinkVar(interp, "::tk::print::paper_width", &paper_width, TCL_LINK_INT);
    hPtr = Tcl_CreateHashEntry (attribs, "paper_height", &new);
    Tcl_SetHashValue (hPtr, &paper_height);
    Tcl_LinkVar(interp, "::tk::print::paper_height", &paper_height, TCL_LINK_INT);
    
    return TCL_OK;
}


/*
 * ----------------------------------------------------------------------
 *
 * WinprintInit  --
 *
 *      Initialize this package and create script-level commands.
 *
 * Results:
 *      Initialization of code.
 *
 * ----------------------------------------------------------------------
 */


int
Winprint_Init(
	  Tcl_Interp * interp)
{

    Tcl_InitHashTable(&attribs, TCL_ONE_WORD_KEYS);
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 *  End:
 */ 
