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

    Tcl_Channel chan;
    Tcl_Obj * printbuffer;
    PRINTDLG pd;
    HDC hDC;
    TEXTMETRIC tm;
    HANDLE printhandle;
    int i, countlines;
    int dpi_x, dpi_y, margin_left, margin_right, margin_top, margin_bottom;
    int printarea_horz, printarea_vert, phys_height, phys_width;
    int digital_margin_left, digital_margin_top, digital_margin_right, digital_margin_bottom;
    int left_adjust_margin, top_adjust_margin, right_adjust_margin, bottom_adjust_margin;
    int page_height, page_width;
    int yChar;
    int page, lines_per_page, total_pages, header_height;
    int result;

    DOCINFO di;
    HFONT hFont;
    char * cline;
    int clen;
    const char * mode;

    result = TCL_OK;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "text");
        result = TCL_ERROR;
        return result;
    }

    mode = "r";

    /* Initialize print dialog. */
    ZeroMemory( &pd, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = GetDesktopWindow();
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS;

    if (PrintDlg( &pd) == TRUE) {
        hDC = pd.hDC;
        if (hDC == NULL) {
            Tcl_AppendResult(interp, "can't allocate printer DC", NULL);
            return TCL_ERROR;
        }

        ZeroMemory( &di, sizeof(di));
        di.cbSize = sizeof(di);
        di.lpszDocName = "Tk Output";

        /* Read file for printing and count number of lines in file.*/
        chan = Tcl_FSOpenFileChannel(interp, objv[1], mode, 0);
        if (chan == NULL) {
            Tcl_AppendResult(interp, "unable to open channel to file", NULL);
            result = TCL_ERROR;
            return result;
        }
        printbuffer = Tcl_NewObj();
        Tcl_IncrRefCount(printbuffer);
        Tcl_ReadChars(chan, printbuffer, -1, 1);
        cline = Tcl_GetStringFromObj(printbuffer, &clen);
        countlines = 0;
        for (i = 0; i < strlen(cline); i++) {
            if (cline[i] == '\n') {
                countlines++;
            }
        }

        Tcl_Close(interp, chan);
        Tcl_DecrRefCount(printbuffer);

        /* Get printer resolution. */
        dpi_x = GetDeviceCaps(hDC, LOGPIXELSX);
        dpi_y = GetDeviceCaps(hDC, LOGPIXELSY);

        /* Compute physical area and margins. */
        margin_left = GetDeviceCaps(hDC, PHYSICALOFFSETX);
        margin_top = GetDeviceCaps(hDC, PHYSICALOFFSETY);
        printarea_horz = GetDeviceCaps(hDC, HORZRES);
        printarea_vert = GetDeviceCaps(hDC, VERTRES);
        phys_width = GetDeviceCaps(hDC, PHYSICALWIDTH);
        phys_height = GetDeviceCaps(hDC, PHYSICALHEIGHT);
        margin_right = phys_width - printarea_horz - margin_left;
        margin_bottom = phys_height - printarea_vert - margin_top;

        /* Convert margins into pixel values the printer understands. */
        digital_margin_left = MulDiv(margin_left, dpi_x, 1000);
        digital_margin_top = MulDiv(margin_top, dpi_y, 1000);
        digital_margin_right = MulDiv(margin_right, dpi_x, 1000);
        digital_margin_bottom = MulDiv(margin_bottom, dpi_y, 1000);

        /* Compute adjusted printer margins in pixels. */
        left_adjust_margin = digital_margin_left - margin_left;
        top_adjust_margin = digital_margin_top - margin_top;
        right_adjust_margin = digital_margin_right - margin_right;
        bottom_adjust_margin = digital_margin_bottom - margin_bottom;

        /* Finally, here is our print area. */
        page_width = printarea_horz - (left_adjust_margin + right_adjust_margin);
        page_height = printarea_vert - (top_adjust_margin + bottom_adjust_margin);

        hFont = (HFONT) GetStockObject(ANSI_FIXED_FONT);

        /* Set up the current device context. */
        SetMapMode(hDC, MM_TEXT);
        SelectObject(hDC, hFont);

        /* Work out the character dimensions for the current font. */
        GetTextMetrics(hDC, &tm);
        yChar = tm.tmHeight;

        /* Work out how much data can be printed onto each page. */
        header_height = 0;
        lines_per_page = (page_height - header_height / yChar);
        total_pages = (countlines + lines_per_page - 1) / lines_per_page;

        if (StartDoc(hDC, &di) > 0) {
            for (page = 0; page < total_pages; page++) {
                printhandle = SelectObject(hDC, hFont);
                RECT r = {
                    10,
                    10,
                    page_width,
                    page_height
                };
                DrawText(hDC, cline, -1, &r, DT_NOPREFIX | DT_WORDBREAK);
            }

            EndPage(hDC);
            EndDoc(hDC);
            DeleteDC(hDC);

            result = TCL_OK;
            return result;
        }
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
