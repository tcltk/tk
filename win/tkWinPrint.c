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
#include <commdlg.h>
#include <wingdi.h>
#include <tcl.h>
#include <tk.h>
#include "tkWinInt.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>   /* For floor(), used later */

HPALETTE
WinGetSystemPalette(void)
{
    HDC hDC;
    HPALETTE hPalette;
    DWORD flags;

    hPalette = NULL;
    hDC = GetDC(NULL);		/* Get the desktop device context */
    flags = GetDeviceCaps(hDC, RASTERCAPS);
    if (flags & RC_PALETTE) {
	LOGPALETTE *palettePtr;

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
 * WinPrint --
 *
 *	Prints a snapshot of a Tk_Window to the designated printer.
 *
 * Results:
 *	Returns a standard Tcl result.  If an error occurred
 *	TCL_ERROR is returned and interp->result will contain an
 *	error message.
 *
 * -------------------------------------------------------------------------
 */
static int
WinPrint(
    ClientData clientData,	/* Interpreter-specific data. */
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *CONST *objv)
{
    BITMAPINFO bi;
    DIBSECTION ds;
    HBITMAP hBitmap;
    HPALETTE hPalette;
    HDC hDC, printDC, memDC;
    void *data;
    Tk_Window tkwin;
    TkWinDCState state;
    int result;
    PRINTDLG pd; 
    DOCINFO di;
    double pageWidth, pageHeight;
    int jobId;
    DEVMODE *dmPtr;
    HGLOBAL hMem;
    Tcl_DString dString;
    char *path;

    Tcl_DStringInit(&dString);
    path = Tcl_GetString(objv[3]);
    tkwin = Tk_NameToWindow(interp, path, Tk_MainWindow(interp));
    if (tkwin == NULL) {
	return TCL_ERROR;
    }
    if (Tk_WindowId(tkwin) == None) {
	Tk_MakeWindowExist(tkwin);
    }
    
    result = TCL_ERROR;
    hDC = TkWinGetDrawableDC(Tk_Display(tkwin), Tk_WindowId(tkwin), &state);

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
	Tcl_AppendResult(interp, "can't blit \"", Tk_PathName(tkwin),
			 NULL, (char *)NULL);
	goto done;
    }
    /* Now that the DIB contains the image of the window, get the
     * databits and write them to the printer device, stretching the
     * image to the fit the printer's resolution.  */
    if (GetObject(hBitmap, sizeof(DIBSECTION), &ds) == 0) {
      Tcl_AppendResult(interp, "can't get DIB object", NULL,
			 (char *)NULL);
	goto done;
    }
       if (PrintDlg(&pd) == FALSE) {
      return TCL_ERROR;
       } else {
    printDC = pd.hDC;
    //   GlobalUnlock(hMem);
    //    GlobalFree(hMem);
    if (printDC == NULL) {
	Tcl_AppendResult(interp, "can't allocate printer DC",
			 NULL, (char *)NULL);
	goto done;
    }
	double scale, sx, sy;

	/* Get the resolution of the printer device. */
	sx = (double)GetDeviceCaps(printDC, HORZRES)/(double)Tk_Width(tkwin);
	sy = (double)GetDeviceCaps(printDC, VERTRES)/(double)Tk_Height(tkwin);
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
      Tcl_AppendResult(interp, "can't start document", 
		(char *)NULL);
	goto done;
    }
    if (StartPage(printDC) <= 0) {
      Tcl_AppendResult(interp, "error starting page", 
		(char *)NULL);
	goto done;
    }
    StretchDIBits(printDC, 0, 0, (int) pageWidth, (int) pageHeight, 0, 0, 
	Tk_Width(tkwin), Tk_Height(tkwin), ds.dsBm.bmBits, 
	(LPBITMAPINFO)&ds.dsBmih, DIB_RGB_COLORS, SRCCOPY);
    EndPage(printDC);
    EndDoc(printDC);
    DeleteDC(printDC);
    //   Tcl_SetResult(interp, Blt_Itoa(jobId), TCL_VOLATILE);
    result = TCL_OK;

  done:
    Tcl_DStringFree(&dString);
 
    DeleteObject(hBitmap);
    //    DeleteDC(memDC);
    TkWinReleaseDrawableDC(Tk_WindowId(tkwin), hDC, &state);
    if (hPalette != NULL) {
	DeleteObject(hPalette);
    }
       }

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * PrintInit  --
 *
 * 	Initialize this package and create script-level commands.
 *
 * Results:
 *	Initialization of code.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */


int
PrintInit(
    Tcl_Interp *interp)
{
     Tcl_CreateObjCommand(interp, "winprint", WinPrint, NULL, NULL);
    return TCL_OK;
    
}
