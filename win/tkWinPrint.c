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
static PAGESETUPDLG psd;
static  DOCINFO di;
int copies, paper_width, paper_height, dpi_x, dpi_y;
char *localPrinterName;
static int PrintSelectPrinter( ClientData clientData,Tcl_Interp *interp,
			       int objc,Tcl_Obj *const objv[]);
static int PrintPageSetup( ClientData clientData, Tcl_Interp *interp,
			   int objc,Tcl_Obj *const objv[]);
BOOL CALLBACK PaintHook(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
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

static int PrintSelectPrinter(ClientData clientData,
				Tcl_Interp *interp,
				int objc,
				Tcl_Obj *const objv[])
{

    (void) clientData;
    (void) objc;
    (void) objv;
    HDC hDC;
    PDEVMODE returnedDevmode;
    PDEVMODE localDevmode;

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
	  
    Tcl_LinkVar(interp, "::tk::print::hDC", (char*)varlink2, TCL_LINK_STRING | TCL_LINK_READ_ONLY);
    Tcl_LinkVar(interp, "::tk::print::copies", (char *)&copies, TCL_LINK_INT |  TCL_LINK_READ_ONLY);
    Tcl_LinkVar(interp, "::tk::print::dpi_x", (char *)&dpi_x, TCL_LINK_INT | TCL_LINK_READ_ONLY);
    Tcl_LinkVar(interp, "::tk::print::dpi_y", (char *)&dpi_y, TCL_LINK_INT |  TCL_LINK_READ_ONLY);
    Tcl_LinkVar(interp, "::tk::print::paper_width", (char *)&paper_width, TCL_LINK_INT |  TCL_LINK_READ_ONLY);
    Tcl_LinkVar(interp, "::tk::print::paper_height", (char *)&paper_height, TCL_LINK_INT |  TCL_LINK_READ_ONLY);
 
    return TCL_OK;
}


/*
 * --------------------------------------------------------------------------
 *
 * PrintPageSetup--
 *
 *     Show the page setup dialogue box.
 *
 * Results:
 *      Returns the complete page setup.
 *
 * -------------------------------------------------------------------------
 */

static int PrintPageSetup( ClientData clientData,
			   Tcl_Interp *interp,
			   int objc,
			   Tcl_Obj *const objv[])
{

    (void) clientData;
    (void) objc;
    (void) objv;
    
    /* Initialize PAGESETUPDLG. */
    ZeroMemory(&psd, sizeof(psd));
    psd.lStructSize = sizeof(psd);
    psd.hwndOwner = GetDesktopWindow();
    returnedDevmode = NULL;
    localDevmode = NULL;


    psd.Flags =  PSD_ENABLEPAGEPAINTHOOK | PSD_MARGINS;

    /*Set default margins.*/
    psd.rtMargin.top = 1000;
    psd.rtMargin.left = 1250;
    psd.rtMargin.right = 1250;
    psd.rtMargin.bottom = 1000;

    /*Callback for displaying print preview.*/
    psd.lpfnPagePaintHook = (LPPAGEPAINTHOOK)PaintHook;

    if (PageSetupDlg(&psd)=TRUE)
	{
	    /* Copy print attributes to local structure. */ 
	    returnedDevmode = (PDEVMODE)GlobalLock(psd.hDevMode);	
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
		}
	    else
		{
		    localDevmode = NULL;
		}
	    if (psd.hDevMode !=NULL) 
		{
		    GlobalFree(psd.hDevMode);
		}
	    return TCL_OK;
  
	} else {
	Tcl_AppendResult(interp, "can't display page setup dialog", NULL);
	return TCL_ERROR;
    }
}

/*
 * --------------------------------------------------------------------------
 *
 * PaintHook--
 *
 *     Callback for displaying page margins/print preview.
 *
 * Results:
 *      Returns visual thumbnail of page margins.
 *
 * -------------------------------------------------------------------------
 */


BOOL CALLBACK PaintHook(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) 
{ 
    LPRECT lprc; 
    COLORREF crMargRect; 
    HDC hdc, hdcOld; 
 
    switch (uMsg) 
    { 
        /* Draw the margin rectangle.  */
        case WM_PSD_MARGINRECT: 
            hdc = (HDC) wParam; 
            lprc = (LPRECT) lParam; 
 
            /* Get the system highlight color. */ 
            crMargRect = GetSysColor(COLOR_HIGHLIGHT); 
 
            /* 
	     * Create a dash-dot pen of the system highlight color and  
             * select it into the DC of the sample page. 
	     */ 
            hdcOld = SelectObject(hdc, CreatePen(PS_DASHDOT, .5, crMargRect)); 
 
            /* Draw the margin rectangle.  */
            Rectangle(hdc, lprc->left, lprc->top, lprc->right, lprc->bottom); 
 
            /* Restore the previous pen to the DC.  */
            SelectObject(hdc, hdcOld); 
            return TRUE; 
 
        default: 
            return FALSE; 
    } 
    return TRUE; 
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


int Winprint_Init(Tcl_Interp * interp)
{

    Tcl_CreateObjCommand(interp, "::tk::print::_selectprinter", PrintSelectPrinter, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::print::_pagesetup", PrintPageSetup,
			 NULL, NULL);
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 *  End:
 */ 
