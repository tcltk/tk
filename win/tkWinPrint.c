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

/* Declaration for functions used later in this file.*/
static HPALETTE WinGetSystemPalette(void);
static int WinCanvasPrint(void *, Tcl_Interp *, int, Tcl_Obj *const *);
static int WinTextPrint(void *, Tcl_Interp *, int, Tcl_Obj *const *);

/*Utility functions and definitions.*/

#define SCALE_FACTOR        100    

/*Convert milimiters to points.*/
 static int MM_TO_PIXELS (int mm, int dpi)
{
    return MulDiv (mm * 100, dpi, 2540);
}

/* Calculate the wrapped height of a string.  */
static int calculate_wrapped_string_height (HDC hDC, int width, char *s)
{
    RECT r = { 0, 0, width, 16384 };
    DrawText (hDC, s, strlen(s), &r, DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK);
    return (r.bottom == 16384) ? calculate_wrapped_string_height (hDC, width, " ") : r.bottom;
}

/* Print a string in the width provided. */
static void print_string (HDC hDC, int x, int y, int width, const char* s)
{
    RECT r = { x, y, x + width, 16384 };
    DrawText (hDC, s, strlen(s), &r, DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK);
}



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

static int WinTextPrint(
			TCL_UNUSED(void * ),
			Tcl_Interp * interp,
			int objc,
			Tcl_Obj *
			const * objv)
{

    Tcl_Channel chan;
    Tcl_Obj * printbuffer;
    PRINTDLG pd;
    HDC hDC;

    int dpi, lpx, lpy, res_x, res_y, left_margin, top_margin, right_margin, bottom_margin, width, y_max, job_id, x, y, pagenum, err, space_needed, max_page, clen;
    DOCINFO di;
    LOGFONT lf;
    HFONT hTextFont, hOldFont;
    char * cline;
    const char * mode;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "text");
	return TCL_ERROR;
    }

    mode = "r";


    /* Initialize print dialog. */
    ZeroMemory( & pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = GetDesktopWindow();
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS;

    if (PrintDlg(&pd) == TRUE) {
	hDC = pd.hDC;

	if (hDC== NULL) {
	    Tcl_AppendResult(interp, "can't allocate printer DC", NULL);
	    return TCL_ERROR;
		}

	dpi = 96 * 100 / SCALE_FACTOR;
	lpx = GetDeviceCaps(hDC, LOGPIXELSX);
	lpy = GetDeviceCaps(hDC, LOGPIXELSX);
	res_x = GetDeviceCaps(hDC, HORZRES);
	res_y = GetDeviceCaps(hDC, VERTRES);

	/* Margins */   
	left_margin = MM_TO_PIXELS(10, dpi);
	top_margin = MM_TO_PIXELS(20, dpi);
	right_margin = MM_TO_PIXELS(20, dpi);
	bottom_margin = MM_TO_PIXELS(20, dpi);

	width = MulDiv(res_x, dpi, lpx) - (left_margin + right_margin);
	y_max = MulDiv(res_y, dpi, lpy) - bottom_margin;

	/* Set up for SCALE_FACTOR. */
	SetMapMode(hDC, MM_ANISOTROPIC);
	SetWindowExtEx(hDC, dpi, dpi, NULL);
	SetViewportExtEx(hDC, lpx, lpy, NULL);
	SetStretchBltMode(hDC, HALFTONE);

	ZeroMemory(&di, sizeof(di));
	di.cbSize = sizeof(di);
	di.lpszDocName = "Tk Output";
	job_id = StartDoc(hDC, & di);

	if (job_id <= 0) {
	    Tcl_AppendResult(interp, "unable to start document", NULL);
	    DeleteDC(hDC);
	    return TCL_ERROR;
	}

	SetBkMode(hDC, TRANSPARENT);
	ZeroMemory( & lf, sizeof(lf));
	lf.lfWeight = FW_NORMAL;
	lf.lfHeight = 12;
	hTextFont = CreateFontIndirect(&lf);
	hOldFont = (HFONT) GetCurrentObject(hDC, OBJ_FONT);
	SelectObject(hDC, hTextFont);

	x = left_margin;
	y = top_margin;
	pagenum = 0;
	err = StartPage(hDC);

	if (err <= 0) {
	    Tcl_AppendResult(interp, "unable to start page", NULL);
	    DeleteDC(hDC);
	    return TCL_ERROR;
	}

	/* Printing loop, per line.*/
	chan = Tcl_FSOpenFileChannel(interp, objv[1], mode, 0);
	if (chan == NULL) {
	    Tcl_AppendResult(interp, "unable to open channel to file", NULL);
	    return TCL_ERROR;
	}
	printbuffer = Tcl_NewObj();
	Tcl_IncrRefCount(printbuffer);
	while (Tcl_GetsObj(chan, printbuffer) != -1) {
            max_page = 0;
	    cline = Tcl_GetStringFromObj(printbuffer, &clen);
	    space_needed = calculate_wrapped_string_height(hDC, width, cline);
	    if (space_needed > y_max - y) {
		if (pagenum >= max_page)
		    break;

		if (EndPage(hDC) < 0 || StartPage(hDC) < 0)
		    break;
	    }

	    print_string(hDC, x, y, width, cline);
	    y += space_needed;
	}
	Tcl_Close(interp, chan);
	Tcl_DecrRefCount(printbuffer);

	EndPage(hDC);
	EndDoc(hDC);
	SelectObject(hDC, hOldFont);
	DeleteObject(hTextFont);
	DeleteDC(hDC);

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
