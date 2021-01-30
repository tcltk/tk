/*
 * tkWinPrint.c --
 * 
 *      This module implements Win32 printer access.
 * 
 * Copyright © 1998 Bell Labs Innovations for Lucent Technologies. 
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

/*Declaration for functions used later in this file.*/
HPALETTE WinGetSystemPalette(void);
static int WinCanvasPrint(TCL_UNUSED(void *), Tcl_Interp * interp, int objc,
		    Tcl_Obj * const *objv);
static int WinTextPrint(TCL_UNUSED(void *), Tcl_Interp * interp, int objc,
		     Tcl_Obj * const *objv);
int PrintInit(Tcl_Interp * interp);

/*
 * --------------------------------------------------------------------------
 * 
 * WinGetSystemPalette --
 * 
 *      Sets a default color palette for bitmap rendering on Win32. 
 * 
 * Results: 
 *
 *      Sets the palette.
 * 
 * -------------------------------------------------------------------------
 */

HPALETTE
WinGetSystemPalette(void)
{
    HDC		    hDC;
    HPALETTE	    hPalette;
    DWORD	    flags;

    hPalette = NULL;
    hDC = GetDC(NULL);		/* Get the desktop device context */
    flags = GetDeviceCaps(hDC, RASTERCAPS);
    if (flags & RC_PALETTE) {
	LOGPALETTE     *palettePtr;

	palettePtr = (LOGPALETTE *)
	    GlobalAlloc(GPTR, sizeof(LOGPALETTE) + 256 * sizeof(PALETTEENTRY));
	palettePtr->palVersion = 0x300;
	palettePtr->palNumEntries = 256;
	GetSystemPaletteEntries(hDC, 0, 256, palettePtr->palPalEntry);
	hPalette = CreatePalette(palettePtr);
	GlobalFree(palettePtr);
    }
    ReleaseDC(NULL, hDC);
    return hPalette;
}


/*
 * --------------------------------------------------------------------------
 * 
 * WinCanvasPrint --
 * 
 *      Prints a snapshot of a Tk_Window/canvas to the designated printer.
 * 
 * Results: 
 *      Returns a standard Tcl result.  
 * 
 * -------------------------------------------------------------------------
 */

static int
WinCanvasPrint(
	 TCL_UNUSED(void *),
	 Tcl_Interp * interp,
	 int objc,
	 Tcl_Obj * const *objv)
{
    BITMAPINFO	    bi;
    DIBSECTION	    ds;
    HBITMAP	    hBitmap;
    HPALETTE	    hPalette;
    HDC		    hDC, printDC, memDC;
    void           *data;
    Tk_Window	    tkwin;
    TkWinDCState    state;
    int		    result;
    PRINTDLG	    pd;
    DOCINFO	    di;
    double	    pageWidth, pageHeight;
    int		    jobId;
    Tcl_DString	    dString;
    char           *path;
    double          scale, sx, sy;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "window");
	return TCL_ERROR;
    }
    Tcl_DStringInit(&dString);
    path = Tcl_GetString(objv[1]);
    tkwin = Tk_NameToWindow(interp, path, Tk_MainWindow(interp));
    if (tkwin == NULL) {
	return TCL_ERROR;
    }
    if (Tk_WindowId(tkwin) == None) {
	Tk_MakeWindowExist(tkwin);
    }
    result = TCL_ERROR;
    hDC = TkWinGetDrawableDC(Tk_Display(tkwin), Tk_WindowId(tkwin), &state);


    /* Initialize bitmap to contain window contents/data. */
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = Tk_Width(tkwin);
    bi.bmiHeader.biHeight = Tk_Height(tkwin);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    hBitmap = CreateDIBSection(hDC, &bi, DIB_RGB_COLORS, &data, NULL, 0);
    memDC = CreateCompatibleDC(hDC);
    SelectObject(memDC, hBitmap);
    hPalette = WinGetSystemPalette();
    if (hPalette != NULL) {
	SelectPalette(hDC, hPalette, FALSE);
	RealizePalette(hDC);
	SelectPalette(memDC, hPalette, FALSE);
	RealizePalette(memDC);
    }
    /* Copy the window contents to the memory surface. */
    if (!BitBlt(memDC, 0, 0, Tk_Width(tkwin), Tk_Height(tkwin), hDC, 0, 0,
		SRCCOPY)) {
	Tcl_AppendResult(interp, "can't blit \"", Tk_PathName(tkwin), NULL);
	goto done;
    }
    /*
     * Now that the DIB contains the image of the window, get the databits
     * and write them to the printer device, stretching the image to the fit
     * the printer's resolution.
     */
    if (GetObject(hBitmap, sizeof(DIBSECTION), &ds) == 0) {
	Tcl_AppendResult(interp, "can't get DIB object", NULL);
	goto done;
    }
    /* Initialize print dialog.  */
    ZeroMemory(&pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.Flags = PD_RETURNDC;

    if (PrintDlg(&pd) == TRUE) {
	printDC = pd.hDC;

	if (printDC == NULL) {
	    Tcl_AppendResult(interp, "can't allocate printer DC", NULL);
	    goto done;
	}

	/* Get the resolution of the printer device. */
	sx = (double)GetDeviceCaps(printDC, HORZRES) / (double)Tk_Width(tkwin);
	sy = (double)GetDeviceCaps(printDC, VERTRES) / (double)Tk_Height(tkwin);
	scale = fmin(sx, sy);
	pageWidth = scale * Tk_Width(tkwin);
	pageHeight = scale * Tk_Height(tkwin);

	ZeroMemory(&di, sizeof(di));
	di.cbSize = sizeof(di);
	Tcl_DStringAppend(&dString, "Snapshot of \"", -1);
	Tcl_DStringAppend(&dString, Tk_PathName(tkwin), -1);
	Tcl_DStringAppend(&dString, "\"", -1);
	di.lpszDocName = Tcl_DStringValue(&dString);
	jobId = StartDoc(printDC, &di);
	if (jobId <= 0) {
	    Tcl_AppendResult(interp, "can't start document", NULL);
	    goto done;
	}
	if (StartPage(printDC) <= 0) {
	    Tcl_AppendResult(interp, "error starting page", NULL);
	    goto done;
	}
	StretchDIBits(printDC, 0, 0, pageWidth, pageHeight, 0, 0,
		      Tk_Width(tkwin), Tk_Height(tkwin), ds.dsBm.bmBits,
		      (LPBITMAPINFO) & ds.dsBmih, DIB_RGB_COLORS, SRCCOPY);
	EndPage(printDC);
	EndDoc(printDC);
	DeleteDC(printDC);
	result = TCL_OK;

done:
	Tcl_DStringFree(&dString);

	DeleteObject(hBitmap);
	DeleteDC(memDC);
	TkWinReleaseDrawableDC(Tk_WindowId(tkwin), hDC, &state);
	if (hPalette != NULL) {
	    DeleteObject(hPalette);
	}
    } else {
	return TCL_ERROR;
    }

    return result;
}


/*
 * ----------------------------------------------------------------------
 * 
 * WinTextPrint  --
 * 
 *      Prints a character buffer to the designated printer.
 * 
 * Results: 
 *      Returns a standard Tcl result.  
 * 
 * ----------------------------------------------------------------------
 */


static int WinTextPrint(TCL_UNUSED(void*),
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const *objv)
{

    BOOL bStatus;
    HANDLE hPrinter;
    BOOL printDlgReturn;
    PRINTDLG printDlgInfo = { 0 };
    PDEVMODE returnedDevmode = NULL;
    PDEVMODE localDevmode = NULL;
    DOC_INFO_1 DocInfo;
    DWORD dwJob;
    DWORD dwBytesWritten;
    LPWSTR localPrinterName;
    LPBYTE lpData;
    DWORD dwCount;

     if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "text");
	return TCL_ERROR;
    }

    char *data = Tcl_GetString(objv[1]);
    int *len = strlen(data);

    lpData = (LPBYTE) data;
    dwCount = (DWORD) len;

    /* Initialize the print dialog box's data structure. */
    printDlgInfo.lStructSize = sizeof(printDlgInfo);

    /* Display the printer dialog and retrieve the printer DC. */
    printDlgReturn = PrintDlg(&printDlgInfo);

    /* Lock the handle to get a pointer to the DEVMODE structure. */
    returnedDevmode = (PDEVMODE) GlobalLock(printDlgInfo.hDevMode);

    localDevmode = (LPDEVMODE) HeapAlloc(        GetProcessHeap(),
        HEAP_ZERO_MEMORY | HEAP_GENERATE_EXCEPTIONS,
        returnedDevmode->dmSize);

    if (NULL != localDevmode)
    {
        memcpy(            (LPVOID) localDevmode,
            (LPVOID) returnedDevmode,
            returnedDevmode->dmSize);

        /* 
         * Save the printer name from the DEVMODE structure. 
         * This is done here just to illustrate how to access 
         * the name field. The printer name can also be accessed
         * by referring to the dmDeviceName in the local 
         * copy of the DEVMODE structure. 
	 */
        localPrinterName = localDevmode->dmDeviceName;
    }

    bStatus = OpenPrinter(localPrinterName, &hPrinter, NULL);

    DocInfo.pDocName = (LPTSTR) _T("Tk Output");
    DocInfo.pOutputFile = NULL;
    DocInfo.pDatatype = (LPTSTR) _T("RAW");

    /* Inform the spooler the document is beginning. */
    dwJob = StartDocPrinter(hPrinter, 1, (LPBYTE) &DocInfo);
    if (dwJob > 0)
    {
        /* Start a page.  */
        bStatus = StartPagePrinter(hPrinter);
        if (bStatus)
        {
            /*Send the data to the printer.  */
            bStatus = WritePrinter(hPrinter, lpData, dwCount, &dwBytesWritten);
            EndPagePrinter(hPrinter);
        }
        /* Inform the spooler that the document is ending.  */
        EndDocPrinter(hPrinter);
    }
    /* Close the printer handle. */
    ClosePrinter(hPrinter);

    /* Check to see if correct number of bytes were written.  */
    if (!bStatus || (dwBytesWritten != dwCount))
    {
        bStatus = FALSE;
        return TCL_ERROR;
    } else {
        bStatus = TRUE;
        return TCL_OK;
    }
    return TCL_OK;
}


/*
 * ----------------------------------------------------------------------
 * 
 * PrintInit  --
 * 
 *      Initialize this package and create script-level commands.
 * 
 * Results: 
 *      Initialization of code.
 * 
 * ----------------------------------------------------------------------
 */


int
PrintInit(
	  Tcl_Interp * interp)
{
    Tcl_CreateObjCommand(interp, "::tk::print::_printcanvas", WinCanvasPrint, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::print::_printtext", WinTextPrint, NULL, NULL);
    return TCL_OK;
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
