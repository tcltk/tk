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

/* Declaration for functions used later in this file.*/
static HPALETTE WinGetSystemPalette(void);
static int WinCanvasPrint(void *, Tcl_Interp *, int, Tcl_Obj *const *);
static int WinTextPrint(void *, Tcl_Interp *, int, Tcl_Obj *const *);

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
    if (flags &RC_PALETTE) {
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
    /* Initialize print dialog. */
    ZeroMemory(&pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.Flags = PD_RETURNDC;
    pd.hwndOwner = GetDesktopWindow();

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
		      (LPBITMAPINFO) &ds.dsBmih, DIB_RGB_COLORS, SRCCOPY);
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

static int WinTextPrint(
			TCL_UNUSED(void * ),
			Tcl_Interp * interp,
			int objc,
			Tcl_Obj *
			const * objv) {
    PRINTDLG pd;
    HDC hDC;
    HWND hwndEdit;
    TEXTMETRIC tm;
    int result;
    DOCINFO di;
    HFONT hFont = NULL;
    char * data;
    const char * tmptxt;
    LPCTSTR printbuffer;
    LONG bufferlen;
    int yChar, chars_per_line, lines_per_page, total_lines,
	total_pages, page, line, line_number;
    PTSTR linebuffer;
    BOOL success;
    
    result = TCL_OK;
    success = TRUE;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "text");
	result = TCL_ERROR;
	return result;
    }

    /*
     *Initialize print dialog.
     */
    ZeroMemory( & pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = GetDesktopWindow();
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_ALLPAGES | PD_USEDEVMODECOPIESANDCOLLATE;

    if (PrintDlg( &pd) == TRUE) {
	hDC = pd.hDC;
	if (hDC == NULL) {
	    Tcl_AppendResult(interp, "can't allocate printer DC", NULL);
	    return TCL_ERROR;
	}

	ZeroMemory( &di, sizeof(di));
	di.cbSize = sizeof(di);
	di.lpszDocName = "Tk Output";

	/*
	 * Get text for printing.
	 */
	data = Tcl_GetString(objv[1]);

	/*
	 * Convert input text into a format Windows can use for printing.
	 */
	tmptxt = data;
	printbuffer = (LPCTSTR) tmptxt;
	bufferlen = lstrlen(printbuffer);

	/*
	 * Place text into a hidden Windows multi-line edit control
	 * to make it easier to parse for printing.
	 */

	hwndEdit = CreateWindowEx(
				  0, "EDIT",
				  NULL,
				  WS_POPUP | ES_MULTILINE,
				  0, 0, 0, 0,
				  NULL,
				  NULL,
				  NULL,
				  NULL);

	/*
	 * Add text to the window.
	 */
	SendMessage(hwndEdit, WM_SETTEXT, 0, (LPARAM) printbuffer);

	if (0 == (total_lines = SendMessage(hwndEdit, EM_GETLINECOUNT, 0, 0)))
	    return TCL_OK;

	/*
	 * Determine how text will fit on page.
	 */
	GetTextMetrics(hDC, &tm);
	yChar = tm.tmHeight + tm.tmExternalLeading;
	chars_per_line = GetDeviceCaps(hDC, HORZRES) / tm.tmAveCharWidth;
	lines_per_page = GetDeviceCaps(hDC, VERTRES) / yChar;
	total_pages = (total_lines + lines_per_page - 1) / lines_per_page;

	/*
	 * Allocate a buffer for each line of text.
	 */
	linebuffer = ckalloc(sizeof(TCHAR) * (chars_per_line + 1));

	if (StartDoc(pd.hDC, & di) > 0) {
	    for (page  = 0 ; page  < total_pages ; page++) {
		if (StartPage(hDC) < 0) {
		    success = FALSE;
		    result = TCL_ERROR;
		    return result;
		}

		/* 
		 * For each page, print the lines.
		 */
		for (line = 0; line < lines_per_page; line++) {
		    line_number = lines_per_page * page + line;
		    if (line_number > total_lines)
			break;
		    *(int * ) linebuffer = chars_per_line;
		    TextOut(hDC, 100, yChar * line, linebuffer,
			    (int) SendMessage(hwndEdit, EM_GETLINE,
					      (WPARAM) line_number, (LPARAM) linebuffer));
		}
		if (EndPage(hDC) < 0) {
		    success = FALSE;
		    result = TCL_ERROR;
		    return result;
		}
	    }
	    if (!success) {
		result = TCL_ERROR;
		return result;
	    }
	    if (success){
		EndDoc(hDC);
	        DestroyWindow(hwndEdit);
	    }
	}
	ckfree(linebuffer);
	DeleteDC(pd.hDC);
	result = TCL_OK;
	return result;
    }
    return result;
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
