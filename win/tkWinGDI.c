/*
 * tkWinGDI.c --
 *
 *      This module implements access to the Win32 GDI API.
 *
 * Copyright © 1991-2018 Microsoft Corp.
 * Copyright © 2009, Michael I. Schwartz.
 * Copyright © 1998-2019 Harald Oehlmann, Elmicron GmbH
 * Copyright © 2021 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include <windows.h>
#include <math.h>
#include <wtypes.h>
#include <winspool.h>
#include <commdlg.h>
#include <wingdi.h>

#include <tcl.h>

#include "tkWinInt.h"

/*
 * Create a standard "DrawFunc" to make this more workable....
 */
#ifdef _MSC_VER
typedef BOOL (WINAPI *DrawFunc) (
	HDC, int, int, int, int, int, int, int, int); /* Arc, Chord, Pie. */
#else
typedef BOOL WINAPI (*DrawFunc) (
	HDC, int, int, int, int, int, int, int, int); /* Arc, Chord, Pie. */
#endif

/* Real functions. */
static Tcl_ObjCmdProc GdiArc;
static Tcl_ObjCmdProc GdiBitmap;
static Tcl_ObjCmdProc GdiCharWidths;
static Tcl_ObjCmdProc GdiImage;
static Tcl_ObjCmdProc GdiPhoto;
static Tcl_ObjCmdProc GdiLine;
static Tcl_ObjCmdProc GdiOval;
static Tcl_ObjCmdProc GdiPolygon;
static Tcl_ObjCmdProc GdiRectangle;
static Tcl_ObjCmdProc GdiText;
static Tcl_ObjCmdProc GdiMap;
static Tcl_ObjCmdProc GdiCopyBits;

/* Local copies of similar routines elsewhere in Tcl/Tk. */
static int		GdiGetColor(Tcl_Obj *nameObj, COLORREF *color);

/*
 * Helper functions.
 */
static int		GdiMakeLogFont(Tcl_Interp *interp, const char *str,
			    LOGFONTW *lf, HDC hDC);
static int		GdiMakePen(Tcl_Interp *interp, int width,
			    int dashstyle, const char *dashstyledata,
			    int capstyle, int joinstyle,
			    int stipplestyle, const char *stippledata,
			    unsigned long color, HDC hDC, HGDIOBJ *oldPen);
static int		GdiFreePen(Tcl_Interp *interp, HDC hDC, HGDIOBJ oldPen);
static int		GdiMakeBrush(unsigned long color, long hatch,
			    LOGBRUSH *lb, HDC hDC, HBRUSH *oldBrush);
static void		GdiFreeBrush(Tcl_Interp *interp, HDC hDC,
			    HGDIOBJ oldBrush);
static int		GdiGetHdcInfo(HDC hdc,
			    LPPOINT worigin, LPSIZE wextent,
			    LPPOINT vorigin, LPSIZE vextent);

/* Helper functions for printing the window client area. */
enum PrintType { PTWindow = 0, PTClient = 1, PTScreen = 2 };

static HANDLE		CopyToDIB(HWND wnd, enum PrintType type);
static HBITMAP		CopyScreenToBitmap(LPRECT lpRect);
static HANDLE		BitmapToDIB(HBITMAP hb, HPALETTE hp);
static HANDLE		CopyScreenToDIB(LPRECT lpRect);
static int		DIBNumColors(LPBITMAPINFOHEADER lpDIB);
static int		PalEntriesOnDevice(HDC hDC);
static HPALETTE		GetSystemPalette(void);
static void		GetDisplaySize(LONG *width, LONG *height);
static int		GdiWordToWeight(const char *str);
static int		GdiParseFontWords(Tcl_Interp *interp, LOGFONTW *lf,
			    const char *str[], int numargs);
static Tcl_ObjCmdProc PrintSelectPrinter;
static Tcl_ObjCmdProc PrintOpenPrinter;
static Tcl_ObjCmdProc PrintClosePrinter;
static Tcl_ObjCmdProc PrintOpenDoc;
static Tcl_ObjCmdProc PrintCloseDoc;
static Tcl_ObjCmdProc PrintOpenPage;
static Tcl_ObjCmdProc PrintClosePage;

/*
 * Global state.
 */

static PRINTDLGW pd;
static DOCINFOW di;
static WCHAR *localPrinterName = NULL;
static int copies, paper_width, paper_height, dpi_x, dpi_y;
static LPDEVNAMES devnames;
static HDC printDC;

/*
 * To make the "subcommands" follow a standard convention, add them to this
 * array. The first element is the subcommand name, and the second a standard
 * Tcl command handler.
 */

static const struct gdi_command {
    const char *command_string;
    Tcl_ObjCmdProc *command;
} gdi_commands[] = {
    { "arc",        GdiArc },
    { "bitmap",     GdiBitmap },
    { "characters", GdiCharWidths },
    { "image",      GdiImage },
    { "line",       GdiLine },
    { "map",        GdiMap },
    { "oval",       GdiOval },
    { "photo",      GdiPhoto },
    { "polygon",    GdiPolygon },
    { "rectangle",  GdiRectangle },
    { "text",       GdiText },
    { "copybits",   GdiCopyBits },
};

/*
 *----------------------------------------------------------------------
 *
 * GdiArc --
 *
 *	Map canvas arcs to GDI context.
 *
 * Results:
 *	Renders arcs.
 *
 *----------------------------------------------------------------------
 */

static int GdiArc(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int argc,
    Tcl_Obj *const *objv)
{
    static const char usage_message[] =
	"::tk::print::_gdi arc hdc x1 y1 x2 y2 "
	"-extent angle -start angle -style arcstyle "
	"-fill color -outline color "
	"-width dimension -dash dashrule "
	"-outlinestipple ignored -stipple ignored\n" ;
    int x1, y1, x2, y2;
    int xr0, yr0, xr1, yr1;
    HDC hDC;
    double extent = 0.0, start = 0.0;
    DrawFunc drawfunc;
    int width = 0;
    HPEN hPen;
    COLORREF linecolor = 0, fillcolor = BS_NULL;
    int dolinecolor = 0, dofillcolor = 0;
    HBRUSH hBrush = NULL;
    LOGBRUSH lbrush;
    HGDIOBJ oldobj = NULL;
    int dodash = 0;
    const char *dashdata = 0;

    drawfunc = Pie;

    /* Verrrrrry simple for now.... */
    if (argc < 6) {
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

    hDC = printDC;

    if ((Tcl_GetIntFromObj(interp, objv[2], &x1) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[3], &y1) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[4], &x2) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[5], &y2) != TCL_OK)) {
	return TCL_ERROR;
    }

    argc -= 6;
    objv += 6;
    while (argc >= 2) {
	if (strcmp(Tcl_GetString(objv[0]), "-extent") == 0) {
	    extent = atof(Tcl_GetString(objv[1]));
	} else if (strcmp(Tcl_GetString(objv[0]), "-start") == 0) {
	    start = atof(Tcl_GetString(objv[1]));
	} else if (strcmp(Tcl_GetString(objv[0]), "-style") == 0) {
	    if (strcmp(Tcl_GetString(objv[1]), "pieslice") == 0) {
		drawfunc = Pie;
	    } else if (strcmp(Tcl_GetString(objv[1]), "arc") == 0) {
		drawfunc = Arc;
	    } else if (strcmp(Tcl_GetString(objv[1]), "chord") == 0) {
		drawfunc = Chord;
	    }
	} else if (strcmp(Tcl_GetString(objv[0]), "-fill") == 0) {
	    /* Handle all args, even if we don't use them yet. */
	    if (GdiGetColor(objv[1], &fillcolor)) {
		dofillcolor = 1;
	    }
	} else if (strcmp(Tcl_GetString(objv[0]), "-outline") == 0) {
	    if (GdiGetColor(objv[1], &linecolor)) {
		dolinecolor = 1;
	    }
	} else if (strcmp(Tcl_GetString(objv[0]), "-outlinestipple") == 0) {
	    /* ignored */
	} else if (strcmp(Tcl_GetString(objv[0]), "-stipple") == 0) {
	    /* ignored */
	} else if (strcmp(Tcl_GetString(objv[0]), "-width") == 0) {
	    if (Tcl_GetIntFromObj(interp, objv[1], &width)) {
		return TCL_ERROR;
	    }
	} else if (strcmp(Tcl_GetString(objv[0]), "-dash") == 0) {
	    if (Tcl_GetString(objv[1])) {
		dodash = 1;
		dashdata = Tcl_GetString(objv[1]);
	    }
	} else {
	    /* Don't know that option! */
	    Tcl_AppendResult(interp, usage_message, (char *)NULL);
	    return TCL_ERROR;
	}
	argc -= 2;
	objv += 2;
    }
    xr0 = xr1 = (x1 + x2) / 2;
    yr0 = yr1 = (y1 + y2) / 2;

    /*
     * The angle used by the arc must be "warped" by the eccentricity of the
     * ellipse.  Thanks to Nigel Dodd <nigel.dodd@avellino.com> for bringing a
     * nice example.
     */

    xr0 += (int)(100.0 * (x2 - x1) * cos((start * 2.0 * 3.14159265) / 360.0));
    yr0 -= (int)(100.0 * (y2 - y1) * sin((start * 2.0 * 3.14159265) / 360.0));
    xr1 += (int)(100.0 * (x2 - x1) * cos(((start+extent) * 2.0 * 3.14159265) / 360.0));
    yr1 -= (int)(100.0 * (y2 - y1) * sin(((start+extent) * 2.0 * 3.14159265) / 360.0));

    /*
     * Under Win95, SetArcDirection isn't implemented--so we have to assume
     * that arcs are drawn counterclockwise (e.g., positive extent) So if it's
     * negative, switch the coordinates!
     */

    if (extent < 0) {
	int xr2 = xr0;
	int yr2 = yr0;

	xr0 = xr1;
	xr1 = xr2;
	yr0 = yr1;
	yr1 = yr2;
    }

    if (dofillcolor) {
	GdiMakeBrush(fillcolor, 0, &lbrush, hDC, &hBrush);
    } else {
	oldobj = SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));
    }

    if (width || dolinecolor) {
	GdiMakePen(interp, width, dodash, dashdata,
		0, 0, 0, 0, linecolor, hDC, (HGDIOBJ *)&hPen);
    }

    (*drawfunc)(hDC, x1, y1, x2, y2, xr0, yr0, xr1, yr1);

    if (width || dolinecolor) {
	GdiFreePen(interp, hDC, hPen);
    }
    if (hBrush) {
	GdiFreeBrush(interp, hDC, hBrush);
    } else {
	SelectObject(hDC, oldobj);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiBitmap --
 *
 *	Unimplemented for now. Should use the same techniques as
 *	CanvasPsBitmap (tkCanvPs.c).
 *
 * Results:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int GdiBitmap(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    TCL_UNUSED(int),
    Tcl_Obj *const *objv)
{
    /*
     * Skip this for now. Should be based on common code with the copybits
     * command.
     */

    Tcl_WrongNumArgs(interp, 1, objv, "hdc x y "
	    "-anchor [center|n|e|s|w] -background color "
	    "-bitmap bitmap -foreground color\n"
	    "Not implemented yet. Sorry!");
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiImage --
 *
 *	Unimplemented for now. Unimplemented for now. Should switch on image
 *	type and call either GdiPhoto or GdiBitmap. This code is similar to
 *	that in tkWinImage.c.
 *
 * Results:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int GdiImage(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    TCL_UNUSED(int),
    Tcl_Obj *const *objv)
{
    /* Skip this for now..... */
    /* Should be based on common code with the copybits command. */

    Tcl_WrongNumArgs(interp, 1, objv, "hdc x y -anchor [center|n|e|s|w] -image name\n"
	    "Not implemented yet. Sorry!");
    /* Normally, usage results in TCL_ERROR--but wait til' it's implemented. */
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiPhoto --
 *
 *	Contributed by Lukas Rosenthaler <lukas.rosenthaler@balcab.ch>
 *
 *	Note: The canvas doesn't directly support photos (only as images), so
 *	this is the first ::tk::print::_gdi command without an equivalent
 *	canvas command.  This code may be modified to support photo images on
 *	the canvas.
 *
 * Results:
 *	Renders a photo.
 *
 *----------------------------------------------------------------------
 */

static int GdiPhoto(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int argc,
    Tcl_Obj *const *objv)
{
    static const char usage_message[] =
	"::tk::print::_gdi photo hdc [-destination x y [w [h]]] -photo name\n";
    HDC dst;
    int dst_x = 0, dst_y = 0, dst_w = 0, dst_h = 0;
    int nx, ny, sll;
    const char *photoname = 0;	/* For some reason Tk_FindPhoto takes a char *. */
    Tk_PhotoHandle photo_handle;
    Tk_PhotoImageBlock img_block;
    BITMAPINFO bitmapinfo;	/* Since we don't need the bmiColors table,
				 * there is no need for dynamic allocation. */
    int oldmode;		/* For saving the old stretch mode. */
    POINT pt;			/* For saving the brush org. */
    char *pbuf = NULL;
    int i, j, k;
    int retval = TCL_OK;

    /*
     * Parse the arguments.
     */

    /* HDC is required. */
    if (argc < 2) {
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

    dst = printDC;

    /*
     * Next, check to see if 'dst' can support BitBlt.
     * If not, raise an error.
     */

    if ((GetDeviceCaps(dst, RASTERCAPS) & RC_STRETCHDIB) == 0) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"::tk::print::_gdi photo not supported on device context (0x%s)",
		Tcl_GetString(objv[1])));
	return TCL_ERROR;
    }

    /* Parse the command line arguments. */
    for (j = 2; j < argc; j++) {
	if (strcmp(Tcl_GetString(objv[j]), "-destination") == 0) {
	    double x, y, w, h;
	    int count = 0;
	    char dummy;

	    if (j < argc) {
		count = sscanf(Tcl_GetString(objv[++j]), "%lf%lf%lf%lf%c",
			&x, &y, &w, &h, &dummy);
	    }

	    if (count < 2 || count > 4) {
		/* Destination must provide at least 2 arguments. */
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"-destination requires a list of 2 to 4 numbers\n%s",
			usage_message));
		return TCL_ERROR;
	    }

	    dst_x = (int) x;
	    dst_y = (int) y;
	    if (count == 3) {
		dst_w = (int) w;
		dst_h = -1;
	    } else if (count == 4) {
		dst_w = (int) w;
		dst_h = (int) h;
	    }
	} else if (strcmp(Tcl_GetString(objv[j]), "-photo") == 0) {
	    photoname = Tcl_GetString(objv[++j]);
	}
    }

    if (photoname == 0) {	/* No photo provided. */
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"No photo name provided to ::tk::print::_gdi photo\n%s",
		usage_message));
	return TCL_ERROR;
    }

    photo_handle = Tk_FindPhoto(interp, photoname);
    if (photo_handle == 0) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"::tk::print::_gdi photo: Photo name %s can't be located\n%s",
		photoname, usage_message));
	return TCL_ERROR;
    }
    Tk_PhotoGetImage(photo_handle, &img_block);

    nx  = img_block.width;
    ny  = img_block.height;
    sll = ((3*nx + 3) / 4)*4; /* Must be multiple of 4. */

    /*
     * Buffer is potentially large enough that failure to allocate might be
     * recoverable.
     */

    pbuf = (char *)attemptckalloc(sll * ny * sizeof(char));
    if (pbuf == 0) { /* Memory allocation failure. */
	Tcl_AppendResult(interp,
		"::tk::print::_gdi photo failed--out of memory", (char *)NULL);
	return TCL_ERROR;
    }

    /* After this, all returns must go through retval. */

    /* BITMAP expects BGR; photo provides RGB. */
    for (k = 0; k < ny; k++) {
	for (i = 0; i < nx; i++) {
	    pbuf[k*sll + 3*i] = img_block.pixelPtr[
		    k*img_block.pitch + i*img_block.pixelSize + img_block.offset[2]];
	    pbuf[k*sll + 3*i + 1] = img_block.pixelPtr[
		    k*img_block.pitch + i*img_block.pixelSize + img_block.offset[1]];
	    pbuf[k*sll + 3*i + 2] = img_block.pixelPtr[
		    k*img_block.pitch + i*img_block.pixelSize + img_block.offset[0]];
	}
    }

    memset(&bitmapinfo, 0L, sizeof(BITMAPINFO));

    bitmapinfo.bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
    bitmapinfo.bmiHeader.biWidth         = nx;
    bitmapinfo.bmiHeader.biHeight        = -ny;
    bitmapinfo.bmiHeader.biPlanes        = 1;
    bitmapinfo.bmiHeader.biBitCount      = 24;
    bitmapinfo.bmiHeader.biCompression   = BI_RGB;
    bitmapinfo.bmiHeader.biSizeImage     = 0; /* sll*ny;. */
    bitmapinfo.bmiHeader.biXPelsPerMeter = 0;
    bitmapinfo.bmiHeader.biYPelsPerMeter = 0;
    bitmapinfo.bmiHeader.biClrUsed       = 0;
    bitmapinfo.bmiHeader.biClrImportant  = 0;

    oldmode = SetStretchBltMode(dst, HALFTONE);
    /*
     * According to the Win32 Programmer's Manual, we have to set the brush
     * org, now.
     */
    SetBrushOrgEx(dst, 0, 0, &pt);

    if (dst_w <= 0) {
	dst_w = nx;
	dst_h = ny;
    } else if (dst_h <= 0) {
	dst_h = ny*dst_w / nx;
    }

    if (StretchDIBits(dst, dst_x, dst_y, dst_w, dst_h, 0, 0, nx, ny,
	    pbuf, &bitmapinfo, DIB_RGB_COLORS, SRCCOPY) == (int)GDI_ERROR) {
	int errcode = GetLastError();

	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"::tk::print::_gdi photo internal failure: "
		"StretchDIBits error code %d", errcode));
	retval = TCL_ERROR;
    }

    /* Clean up the hDC. */
    if (oldmode != 0) {
	SetStretchBltMode(dst, oldmode);
	SetBrushOrgEx(dst, pt.x, pt.y, &pt);
    }

    ckfree(pbuf);

    if (retval == TCL_OK) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"%d %d %d %d", dst_x, dst_y, dst_w, dst_h));
    }

    return retval;
}

/*
 *----------------------------------------------------------------------
 *
 * Bezierize --
 *
 *	Interface to Tk's line smoother, used for lines and pollies.
 *	Provided by Jasper Taylor <jasper.taylor@ed.ac.uk>.
 *
 * Results:
 *	Smooths lines.
 *
 *----------------------------------------------------------------------
 */

static int Bezierize(
    POINT* polypoints,
    int npoly,
    int nStep,
    POINT* bpointptr)
{
    /* First, translate my points into a list of doubles. */
    double *inPointList, *outPointList;
    int n;
    int nbpoints = 0;
    POINT* bpoints;

    inPointList = (double *)attemptckalloc(2 * sizeof(double) * npoly);
    if (inPointList == 0) {
	/* TODO: unreachable */
	return nbpoints; /* 0. */
    }

    for (n=0; n<npoly; n++) {
	inPointList[2*n] = polypoints[n].x;
	inPointList[2*n + 1] = polypoints[n].y;
    }

    nbpoints = 1 + npoly * nStep; /* this is the upper limit. */
    outPointList = (double *)attemptckalloc(2 * sizeof(double) * nbpoints);
    if (outPointList == 0) {
	/* TODO: unreachable */
	ckfree(inPointList);
	return 0;
    }

    nbpoints = TkMakeBezierCurve(NULL, inPointList, npoly, nStep,
	    NULL, outPointList);

    ckfree(inPointList);
    bpoints = (POINT *)attemptckalloc(sizeof(POINT)*nbpoints);
    if (bpoints == 0) {
	/* TODO: unreachable */
	ckfree(outPointList);
	return 0;
    }

    for (n=0; n<nbpoints; n++) {
	bpoints[n].x = (long)outPointList[2*n];
	bpoints[n].y = (long)outPointList[2*n + 1];
    }
    ckfree(outPointList);
    *bpointptr = *bpoints;
    return nbpoints;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiLine --
 *
 *	Maps lines to GDI context.
 *
 * Results:
 *	Renders lines.
 *
 *----------------------------------------------------------------------
 */

static int GdiLine(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int argc,
    Tcl_Obj *const *objv)
{
    static const char usage_message[] =
	"::tk::print::_gdi line hdc x1 y1 ... xn yn "
	"-arrow [first|last|both|none] -arrowshape {d1 d2 d3} "
	"-dash dashlist "
	"-capstyle [butt|projecting|round] -fill color "
	"-joinstyle [bevel|miter|round] -smooth [true|false|bezier] "
	"-splinesteps number -stipple bitmap -width linewid";
    char *strend;
    POINT *polypoints;
    int npoly;
    int x, y;
    HDC hDC;
    HPEN hPen;

    LOGBRUSH lbrush;
    HBRUSH hBrush = NULL;

    int width          = 0;
    COLORREF linecolor = 0;
    int dolinecolor    = 0;
    int dosmooth       = 0;
    int doarrow        = 0; /* 0=none; 1=end; 2=start; 3=both. */
    int arrowshape[3];

    int nStep = 12;

    int dodash = 0;
    const char *dashdata = 0;

    arrowshape[0] = 8;
    arrowshape[1] = 10;
    arrowshape[2] = 3;

    /* Verrrrrry simple for now.... */
    if (argc < 6) {
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

    hDC = printDC;

    polypoints = (POINT *)attemptckalloc((argc - 1) * sizeof(POINT));
    if (polypoints == 0) {
	Tcl_AppendResult(interp, "Out of memory in GdiLine", (char *)NULL);
	return TCL_ERROR;
    }
    if ((Tcl_GetIntFromObj(interp, objv[2], (int *)&polypoints[0].x) != TCL_OK)
	||	(Tcl_GetIntFromObj(interp, objv[3], (int *)&polypoints[0].y) != TCL_OK)
	||	(Tcl_GetIntFromObj(interp, objv[4], (int *)&polypoints[1].x) != TCL_OK)
	||	(Tcl_GetIntFromObj(interp, objv[5], (int *)&polypoints[1].y) != TCL_OK)
    ) {
	return TCL_ERROR;
    }
    argc -= 6;
    objv += 6;
    npoly = 2;

    while (argc >= 2) {
	/* Check for a number. */
	x = strtoul(Tcl_GetString(objv[0]), &strend, 0);
	if (strend > Tcl_GetString(objv[0])) {
	    /* One number.... */
	    y = strtoul(Tcl_GetString(objv[1]), &strend, 0);
	    if (strend > Tcl_GetString(objv[1])) {
		/* TWO numbers!. */
		polypoints[npoly].x = x;
		polypoints[npoly].y = y;
		npoly++;
		argc -= 2;
		objv += 2;
	    } else {
		/* Only one number... Assume a usage error. */
		ckfree(polypoints);
		Tcl_AppendResult(interp, usage_message, (char *)NULL);
		return TCL_ERROR;
	    }
	} else {
	    if (strcmp(Tcl_GetString(*objv), "-arrow") == 0) {
		if (strcmp(Tcl_GetString(objv[1]), "none") == 0) {
		    doarrow = 0;
		} else if (strcmp(Tcl_GetString(objv[1]), "both") == 0) {
		    doarrow = 3;
		} else if (strcmp(Tcl_GetString(objv[1]), "first") == 0) {
		    doarrow = 2;
		} else if (strcmp(Tcl_GetString(objv[1]), "last") == 0) {
		    doarrow = 1;
		}
		objv += 2;
		argc -= 2;
	    } else if (strcmp(Tcl_GetString(*objv), "-arrowshape") == 0) {
		/* List of 3 numbers--set arrowshape array. */
		int a1, a2, a3;
		char dummy;

		if (sscanf(Tcl_GetString(objv[1]), "%d%d%d%c", &a1, &a2, &a3, &dummy) == 3
			&& a1 > 0 && a2 > 0 && a3 > 0) {
		    arrowshape[0] = a1;
		    arrowshape[1] = a2;
		    arrowshape[2] = a3;
		}
		/* Else the argument was bad. */

		objv += 2;
		argc -= 2;
	    } else if (strcmp(Tcl_GetString(*objv), "-capstyle") == 0) {
		objv += 2;
		argc -= 2;
	    } else if (strcmp(Tcl_GetString(*objv), "-fill") == 0) {
		if (GdiGetColor(objv[1], &linecolor)) {
		    dolinecolor = 1;
		}
		objv += 2;
		argc -= 2;
	    } else if (strcmp(Tcl_GetString(*objv), "-joinstyle") == 0) {
		objv += 2;
		argc -= 2;
	    } else if (strcmp(Tcl_GetString(*objv), "-smooth") == 0) {
		/* Argument is true/false or 1/0 or bezier. */
		if (Tcl_GetString(objv[1])) {
		    switch (Tcl_GetString(objv[1])[0]) {
		    case 't': case 'T':
		    case '1':
		    case 'b': case 'B': /* bezier. */
			dosmooth = 1;
			break;
		    default:
			dosmooth = 0;
			break;
		    }
		    objv += 2;
		    argc -= 2;
		}
	    } else if (strcmp(Tcl_GetString(*objv), "-splinesteps") == 0) {
		if (Tcl_GetIntFromObj(interp, objv[1], &nStep) != TCL_OK) {
		    return TCL_ERROR;
		}
		objv += 2;
		argc -= 2;
	    } else if (strcmp(Tcl_GetString(*objv), "-dash") == 0) {
		if (Tcl_GetString(objv[1])) {
		    dodash = 1;
		    dashdata = Tcl_GetString(objv[1]);
		}
		objv += 2;
		argc -= 2;
	    } else if (strcmp(Tcl_GetString(*objv), "-dashoffset") == 0) {
		objv += 2;
		argc -= 2;
	    } else if (strcmp(Tcl_GetString(*objv), "-stipple") == 0) {
		objv += 2;
		argc -= 2;
	    } else if (strcmp(Tcl_GetString(*objv), "-width") == 0) {
		if (Tcl_GetIntFromObj(interp, objv[1], &width) != TCL_OK) {
		    return TCL_ERROR;
		}
		objv += 2;
		argc -= 2;
	    } else { /* It's an unknown argument!. */
		argc--;
		objv++;
	    }
	    /* Check for arguments
	     * Most of the arguments affect the "Pen"
	     */
	}
    }

    if (width || dolinecolor || dodash) {
	GdiMakePen(interp, width, dodash, dashdata,
		0, 0, 0, 0, linecolor, hDC, (HGDIOBJ *)&hPen);
    }
    if (doarrow != 0) {
	GdiMakeBrush(linecolor, 0, &lbrush, hDC, &hBrush);
    }

    if (dosmooth) { /* Use PolyBezier. */
	int nbpoints;
	POINT *bpoints = 0;

	nbpoints = Bezierize(polypoints,npoly,nStep,bpoints);
	if (nbpoints > 0) {
	    Polyline(hDC, bpoints, nbpoints);
	} else {
	    Polyline(hDC, polypoints, npoly); /* Out of memory? Just draw a regular line. */
	}
	if (bpoints != 0) {
	    ckfree(bpoints);
	}
    } else {
	Polyline(hDC, polypoints, npoly);
    }

    if (dodash && doarrow) {  /* Don't use dashed or thick pen for the arrows! */
	GdiFreePen(interp, hDC, hPen);
	GdiMakePen(interp, width, 0, 0, 0, 0, 0, 0,
		linecolor, hDC, (HGDIOBJ *)&hPen);
    }

    /* Now the arrowheads, if any. */
    if (doarrow & 1) {
	/* Arrowhead at end = polypoints[npoly-1].x, polypoints[npoly-1].y. */
	POINT ahead[6];
	double dx, dy, length;
	double sinTheta, cosTheta;
	double vertX, vertY, temp;
	double fracHeight;

	fracHeight = 2.0 / arrowshape[2];

	ahead[0].x = ahead[5].x = polypoints[npoly-1].x;
	ahead[0].y = ahead[5].y = polypoints[npoly-1].y;
	dx = ahead[0].x - polypoints[npoly-2].x;
	dy = ahead[0].y - polypoints[npoly-2].y;
	if ((length = hypot(dx, dy)) == 0) {
	    sinTheta = cosTheta = 0.0;
	} else {
	    sinTheta = dy / length;
	    cosTheta = dx / length;
	}
	vertX = ahead[0].x - arrowshape[0]*cosTheta;
	vertY = ahead[0].y - arrowshape[0]*sinTheta;
	temp = arrowshape[2]*sinTheta;
	ahead[1].x = (long)(ahead[0].x - arrowshape[1]*cosTheta + temp);
	ahead[4].x = (long)(ahead[1].x - 2 * temp);
	temp = arrowshape[2]*cosTheta;
	ahead[1].y = (long)(ahead[0].y - arrowshape[1]*sinTheta - temp);
	ahead[4].y = (long)(ahead[1].y + 2 * temp);
	ahead[2].x = (long)(ahead[1].x*fracHeight + vertX*(1.0-fracHeight));
	ahead[2].y = (long)(ahead[1].y*fracHeight + vertY*(1.0-fracHeight));
	ahead[3].x = (long)(ahead[4].x*fracHeight + vertX*(1.0-fracHeight));
	ahead[3].y = (long)(ahead[4].y*fracHeight + vertY*(1.0-fracHeight));

	Polygon(hDC, ahead, 6);
    }

    if (doarrow & 2) {
	/* Arrowhead at end = polypoints[0].x, polypoints[0].y. */
	POINT ahead[6];
	double dx, dy, length;
	double sinTheta, cosTheta;
	double vertX, vertY, temp;
	double fracHeight;

	fracHeight = 2.0 / arrowshape[2];

	ahead[0].x = ahead[5].x = polypoints[0].x;
	ahead[0].y = ahead[5].y = polypoints[0].y;
	dx = ahead[0].x - polypoints[1].x;
	dy = ahead[0].y - polypoints[1].y;
	if ((length = hypot(dx, dy)) == 0) {
	    sinTheta = cosTheta = 0.0;
	} else {
	    sinTheta = dy / length;
	    cosTheta = dx / length;
	}
	vertX = ahead[0].x - arrowshape[0]*cosTheta;
	vertY = ahead[0].y - arrowshape[0]*sinTheta;
	temp = arrowshape[2]*sinTheta;
	ahead[1].x = (long)(ahead[0].x - arrowshape[1]*cosTheta + temp);
	ahead[4].x = (long)(ahead[1].x - 2 * temp);
	temp = arrowshape[2]*cosTheta;
	ahead[1].y = (long)(ahead[0].y - arrowshape[1]*sinTheta - temp);
	ahead[4].y = (long)(ahead[1].y + 2 * temp);
	ahead[2].x = (long)(ahead[1].x*fracHeight + vertX*(1.0-fracHeight));
	ahead[2].y = (long)(ahead[1].y*fracHeight + vertY*(1.0-fracHeight));
	ahead[3].x = (long)(ahead[4].x*fracHeight + vertX*(1.0-fracHeight));
	ahead[3].y = (long)(ahead[4].y*fracHeight + vertY*(1.0-fracHeight));

	Polygon(hDC, ahead, 6);
    }

    if (width || dolinecolor || dodash) {
	GdiFreePen(interp, hDC, hPen);
    }
    if (hBrush) {
	GdiFreeBrush(interp, hDC, hBrush);
    }

    ckfree(polypoints);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiOval --
 *
 *	Maps ovals to GDI context.
 *
 * Results:
 *	Renders ovals.
 *
 *----------------------------------------------------------------------
 */

static int GdiOval(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int argc,
    Tcl_Obj *const *objv)
{
    static const char usage_message[] =
	"::tk::print::_gdi oval hdc x1 y1 x2 y2 -fill color -outline color "
	"-stipple bitmap -width linewid";
    int x1, y1, x2, y2;
    HDC hDC;
    HPEN hPen;
    int width = 0;
    COLORREF linecolor = 0, fillcolor = 0;
    int dolinecolor = 0, dofillcolor = 0;
    HBRUSH hBrush = NULL;
    LOGBRUSH lbrush;
    HGDIOBJ oldobj = NULL;

    int dodash = 0;
    const char *dashdata = 0;

    /* Verrrrrry simple for now.... */
    if (argc < 6) {
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

    hDC = printDC;

    if ((Tcl_GetIntFromObj(interp, objv[2], &x1) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[3], &y1) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[4], &x2) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[5], &y2) != TCL_OK)) {
	return TCL_ERROR;
    }
    if (x1 > x2) {
	int x3 = x1;
	x1 = x2;
	x2 = x3;
    }
    if (y1 > y2) {
	int y3 = y1;
	y1 = y2;
	y2 = y3;
    }
    argc -= 6;
    objv += 6;

    while (argc > 0) {
	/* Now handle any other arguments that occur. */
	if (strcmp(Tcl_GetString(objv[0]), "-fill") == 0) {
	    if (Tcl_GetString(objv[1]) && GdiGetColor(objv[1], &fillcolor)) {
		dofillcolor = 1;
	    }
	} else if (strcmp(Tcl_GetString(objv[0]), "-outline") == 0) {
	    if (Tcl_GetString(objv[1]) && GdiGetColor(objv[1], &linecolor)) {
		dolinecolor = 1;
	    }
	} else if (strcmp(Tcl_GetString(objv[0]), "-stipple") == 0) {
	    /* Not actually implemented */
	} else if (strcmp(Tcl_GetString(objv[0]), "-width") == 0) {
	    if (Tcl_GetString(objv[1])) {
		if (Tcl_GetIntFromObj(interp, objv[1], &width) != TCL_OK) {
		    return TCL_ERROR;
		}
	    }
	} else if (strcmp(Tcl_GetString(objv[0]), "-dash") == 0) {
	    if (Tcl_GetString(objv[1])) {
		dodash = 1;
		dashdata = Tcl_GetString(objv[1]);
	    }
	}
	objv += 2;
	argc -= 2;
    }

    if (dofillcolor) {
	GdiMakeBrush(fillcolor, 0, &lbrush, hDC, &hBrush);
    } else {
	oldobj = SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));
    }

    if (width || dolinecolor) {
	GdiMakePen(interp, width, dodash, dashdata,
		0, 0, 0, 0, linecolor, hDC, (HGDIOBJ *)&hPen);
    }
    /*
     * Per Win32, Rectangle includes lower and right edges--per Tcl8.3.2 and
     * earlier documentation, canvas rectangle does not. Thus, add 1 to right
     * and lower bounds to get appropriate behavior.
     */
    Ellipse(hDC, x1, y1, x2+1, y2+1);

    if (width || dolinecolor) {
	GdiFreePen(interp, hDC, hPen);
    }
    if (hBrush) {
	GdiFreeBrush(interp, hDC, hBrush);
    } else {
	SelectObject(hDC, oldobj);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiPolygon --
 *
 *	Maps polygons to GDI context.
 *
 * Results:
 *	Renders polygons.
 *
 *----------------------------------------------------------------------
 */

static int GdiPolygon(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int argc,
    Tcl_Obj *const *objv)
{
    static const char usage_message[] =
	"::tk::print::_gdi polygon hdc x1 y1 ... xn yn "
	"-fill color -outline color -smooth [true|false|bezier] "
	"-splinesteps number -stipple bitmap -width linewid";

    char *strend;
    POINT *polypoints;
    int npoly;
    int dosmooth = 0;
    int nStep = 12;
    int x, y;
    HDC hDC;
    HPEN hPen;
    int width = 0;
    COLORREF linecolor = 0, fillcolor = BS_NULL;
    int dolinecolor = 0, dofillcolor = 0;
    LOGBRUSH lbrush;
    HBRUSH hBrush = NULL;
    HGDIOBJ oldobj = NULL;

    int dodash = 0;
    const char *dashdata = 0;

    /* Verrrrrry simple for now.... */
    if (argc < 6) {
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

    hDC = printDC;

    polypoints = (POINT *)attemptckalloc((argc - 1) * sizeof(POINT));
    if (polypoints == 0) {
	/* TODO: unreachable */
	Tcl_AppendResult(interp, "Out of memory in GdiLine", (char *)NULL);
	return TCL_ERROR;
    }
    if ((Tcl_GetIntFromObj(interp, objv[2], (int *)&polypoints[0].x) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[3], (int *)&polypoints[0].y) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[4], (int *)&polypoints[1].x) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[5], (int *)&polypoints[1].y) != TCL_OK)) {
	return TCL_ERROR;
    }
    argc -= 6;
    objv += 6;
    npoly = 2;

    while (argc >= 2) {
	/* Check for a number */
	x = strtoul(Tcl_GetString(objv[0]), &strend, 0);
	if (strend > Tcl_GetString(objv[0])) {
	    /* One number.... */
	    y = strtoul(Tcl_GetString(objv[1]), &strend, 0);
	    if (strend > Tcl_GetString(objv[1])) {
		/* TWO numbers!. */
		polypoints[npoly].x = x;
		polypoints[npoly].y = y;
		npoly++;
		argc -= 2;
		objv += 2;
	    } else {
		/* Only one number... Assume a usage error. */
		ckfree(polypoints);
		Tcl_AppendResult(interp, usage_message, (char *)NULL);
		return TCL_ERROR;
	    }
	} else {
	    /*
	     * Check for arguments.
	     * Most of the arguments affect the "Pen" and "Brush".
	     */
	    if (strcmp(Tcl_GetString(objv[0]), "-fill") == 0) {
		if (Tcl_GetString(objv[1]) && GdiGetColor(objv[1], &fillcolor)) {
		    dofillcolor = 1;
		}
	    } else if (strcmp(Tcl_GetString(objv[0]), "-outline") == 0) {
		if (GdiGetColor(objv[1], &linecolor)) {
		    dolinecolor = 0;
		}
	    } else if (strcmp(Tcl_GetString(objv[0]), "-smooth") == 0) {
		if (Tcl_GetString(objv[1])) {
		    switch (Tcl_GetString(objv[1])[0]) {
		    case 't': case 'T':
		    case '1':
		    case 'b': case 'B': /* bezier. */
			dosmooth = 1;
			break;
		    default:
			dosmooth = 0;
			break;
		    }
		}
	    } else if (strcmp(Tcl_GetString(objv[0]), "-splinesteps") == 0) {
		if (Tcl_GetString(objv[1])) {
		    if (Tcl_GetIntFromObj(interp, objv[1], &nStep) != TCL_OK) {
			return TCL_ERROR;
		    }
		}
	    } else if (strcmp(Tcl_GetString(objv[0]), "-stipple") == 0) {
		/* Not supported */
	    } else if (strcmp(Tcl_GetString(objv[0]), "-width") == 0) {
		if (Tcl_GetString(objv[1])) {
		    if (Tcl_GetIntFromObj(interp, objv[1], &width) != TCL_OK) {
			return TCL_ERROR;
		    }
		}
	    } else if (strcmp(Tcl_GetString(objv[0]), "-dash") == 0) {
		if (Tcl_GetString(objv[1])) {
		    dodash = 1;
		    dashdata = Tcl_GetString(objv[1]);
		}
	    }
	    argc -= 2;
	    objv += 2;
	}
    }

    if (dofillcolor) {
	GdiMakeBrush(fillcolor, 0, &lbrush, hDC, &hBrush);
    } else {
	oldobj = SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));
    }

    if (width || dolinecolor) {
	GdiMakePen(interp, width, dodash, dashdata, 0, 0, 0, 0,
		linecolor, hDC, (HGDIOBJ *)&hPen);
    }

    if (dosmooth) {
	int nbpoints;
	POINT *bpoints = 0;
	nbpoints = Bezierize(polypoints,npoly,nStep,bpoints);
	if (nbpoints > 0) {
	    Polygon(hDC, bpoints, nbpoints);
	} else {
	    Polygon(hDC, polypoints, npoly);
	}
	if (bpoints != 0) {
	    ckfree(bpoints);
	}
    } else {
	Polygon(hDC, polypoints, npoly);
    }

    if (width || dolinecolor) {
	GdiFreePen(interp, hDC, hPen);
    }
    if (hBrush) {
	GdiFreeBrush(interp, hDC, hBrush);
    } else {
	SelectObject(hDC, oldobj);
    }

    ckfree(polypoints);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiRectangle --
 *
 *	Maps rectangles to GDI context.
 *
 * Results:
 *	Renders rectangles.
 *
 *----------------------------------------------------------------------
 */

static int GdiRectangle(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int argc,
    Tcl_Obj *const *objv)
{
    static const char usage_message[] =
	"::tk::print::_gdi rectangle hdc x1 y1 x2 y2 "
	"-fill color -outline color "
	"-stipple bitmap -width linewid";

    int x1, y1, x2, y2;
    HDC hDC;
    HPEN hPen;
    int width = 0;
    COLORREF linecolor = 0, fillcolor = BS_NULL;
    int dolinecolor = 0, dofillcolor = 0;
    LOGBRUSH lbrush;
    HBRUSH hBrush = NULL;
    HGDIOBJ oldobj = NULL;

    int dodash = 0;
    const char *dashdata = 0;

    /* Verrrrrry simple for now.... */
    if (argc < 6) {
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

    hDC = printDC;

    if ((Tcl_GetIntFromObj(interp, objv[2], &x1) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[3], &y1) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[4], &x2) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[5], &y2) != TCL_OK)) {
	return TCL_ERROR;
    }
    if (x1 > x2) {
	int x3 = x1;
	x1 = x2;
	x2 = x3;
    }
    if (y1 > y2) {
	int y3 = y1;
	y1 = y2;
	y2 = y3;
    }
    argc -= 6;
    objv += 6;

    /* Now handle any other arguments that occur. */
    while (argc > 1) {
	if (strcmp(Tcl_GetString(objv[0]), "-fill") == 0) {
	    if (Tcl_GetString(objv[1]) && GdiGetColor(objv[1], &fillcolor)) {
		dofillcolor = 1;
	    }
	} else if (strcmp(Tcl_GetString(objv[0]), "-outline") == 0) {
	    if (Tcl_GetString(objv[1]) && GdiGetColor(objv[1], &linecolor)) {
		dolinecolor = 1;
	    }
	} else if (strcmp(Tcl_GetString(objv[0]), "-stipple") == 0) {
	    /* Not supported; ignored */
	} else if (strcmp(Tcl_GetString(objv[0]), "-width") == 0) {
	    if (Tcl_GetString(objv[1])) {
		if (Tcl_GetIntFromObj(interp, objv[1], &width) != TCL_OK) {
		    return TCL_ERROR;
		}
	    }
	} else if (strcmp(Tcl_GetString(objv[0]), "-dash") == 0) {
	    if (Tcl_GetString(objv[1])) {
		dodash = 1;
		dashdata = Tcl_GetString(objv[1]);
	    }
	}

	argc -= 2;
	objv += 2;
    }

    /*
     * Note: If any fill is specified, the function must create a brush and
     * put the coordinates in a RECTANGLE structure, and call FillRect.
     * FillRect requires a BRUSH / color.
     * If not, the function Rectangle must be called.
     */
    if (dofillcolor) {
	GdiMakeBrush(fillcolor, 0, &lbrush, hDC, &hBrush);
    } else {
	oldobj = SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));
    }

    if (width || dolinecolor) {
	GdiMakePen(interp, width, dodash, dashdata,
		0, 0, 0, 0, linecolor, hDC, (HGDIOBJ *)&hPen);
    }
    /*
     * Per Win32, Rectangle includes lower and right edges--per Tcl8.3.2 and
     * earlier documentation, canvas rectangle does not. Thus, add 1 to
     * right and lower bounds to get appropriate behavior.
     */
    Rectangle(hDC, x1, y1, x2+1, y2+1);

    if (width || dolinecolor) {
	GdiFreePen(interp, hDC, hPen);
    }
    if (hBrush) {
	GdiFreeBrush(interp, hDC, hBrush);
    } else {
	SelectObject(hDC, oldobj);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiCharWidths --
 *
 *	Computes /character widths. This is completely inadequate for
 *	typesetting, but should work for simple text manipulation.
 *
 * Results:
 *	Returns character width.
 *
 *----------------------------------------------------------------------
 */


static int GdiCharWidths(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int argc,
    Tcl_Obj *const *objv)
{
    static const char usage_message[] =
	"::tk::print::_gdi characters hdc [-font fontname] [-array ary]";
    /*
     * Returns widths of characters from font in an associative array.
     * Font is currently selected font for HDC if not specified.
     * Array name is GdiCharWidths if not specified.
     * Widths should be in the same measures as all other values (1/1000 inch).
     */

    HDC hDC;
    LOGFONTW lf;
    HFONT hfont;
    HGDIOBJ oldfont;
    int made_font = 0;
    const char *aryvarname = "GdiCharWidths";
    /* For now, assume 256 characters in the font.... */
    int widths[256];
    int retval;

    if (argc < 2) {
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

    hDC = printDC;

    argc -= 2;
    objv += 2;

    while (argc > 0) {
	if (strcmp(Tcl_GetString(objv[0]), "-font") == 0) {
	    argc--;
	    objv++;
	    if (GdiMakeLogFont(interp, Tcl_GetString(objv[0]), &lf, hDC)) {
		if ((hfont = CreateFontIndirectW(&lf)) != NULL) {
		    made_font = 1;
		    oldfont = SelectObject(hDC, hfont);
		}
	    }
	    /* Else leave the font alone!. */
	} else if (strcmp(Tcl_GetString(objv[0]), "-array") == 0) {
	    objv++;
	    argc--;
	    if (argc > 0) {
		aryvarname = Tcl_GetString(objv[0]);
	    }
	}
	objv++;
	argc--;
    }

    /* Now, get the widths using the correct function for font type. */
    if ((retval = GetCharWidth32W(hDC, 0, 255, widths)) == FALSE) {
	retval = GetCharWidthW(hDC, 0, 255, widths);
    }

    /*
     * Retval should be 1 (TRUE) if the function succeeded. If the function
     * fails, get the "extended" error code and return. Be sure to deallocate
     * the font if necessary.
     */
    if (retval == FALSE) {
	DWORD val = GetLastError();

	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"::tk::print::_gdi character failed with code %ld", val));
	if (made_font) {
	    SelectObject(hDC, oldfont);
	    DeleteObject(hfont);
	}
	return TCL_ERROR;
    }

    {
	int i;
	char ind[2];
	ind[1] = '\0';

	for (i = 0; i < 255; i++) {
	    /* TODO: use a bytearray for the index name so NUL works */
	    ind[0] = i;
	    Tcl_SetVar2Ex(interp, aryvarname, ind, Tcl_NewIntObj(widths[i]),
		    TCL_GLOBAL_ONLY);
	}
    }
    /* Now, remove the font if we created it only for this function. */
    if (made_font) {
	SelectObject(hDC, oldfont);
	DeleteObject(hfont);
    }

    /* The return value should be the array name(?). */
    Tcl_AppendResult(interp, aryvarname, (char *)NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiText --
 *
 *	Maps text to GDI context.
 *
 * Results:
 *	Renders text.
 *
 *----------------------------------------------------------------------
 */

int GdiText(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int argc,
    Tcl_Obj *const *objv)
{
    static const char usage_message[] =
	"::tk::print::_gdi text hdc x y -anchor [center|n|e|s|w] "
	"-fill color -font fontname "
	"-justify [left|right|center] "
	"-stipple bitmap -text string -width linelen "
	"-single -backfill";

    HDC hDC;
    int x, y;
    const char *string = 0;
    RECT sizerect;
    UINT format_flags = DT_EXPANDTABS|DT_NOPREFIX; /* Like the canvas. */
    Tk_Anchor anchor = TK_ANCHOR_N;
    LOGFONTW lf;
    HFONT hfont;
    HGDIOBJ oldfont;
    int made_font = 0;
    int retval;
    int dotextcolor = 0;
    int dobgmode = 0;
    int bgmode;
    COLORREF textcolor = 0;
    int usesingle = 0;
    WCHAR *wstring;
    Tcl_DString tds;

    if (argc < 4) {
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

    /* Parse the command. */

    hDC = printDC;

    if ((Tcl_GetIntFromObj(interp, objv[2], &x) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[3], &y) != TCL_OK)) {
	return TCL_ERROR;
    }
    argc -= 4;
    objv += 4;

    sizerect.left = sizerect.right = x;
    sizerect.top = sizerect.bottom = y;

    while (argc > 0) {
	if (strcmp(Tcl_GetString(objv[0]), "-anchor") == 0) {
	    argc--;
	    objv++;
	    if (argc > 0) {
		Tk_GetAnchor(interp, Tcl_GetString(objv[0]), &anchor);
	    }
	} else if (strcmp(Tcl_GetString(objv[0]), "-justify") == 0) {
	    argc--;
	    objv++;
	    if (argc > 0) {
		if (strcmp(Tcl_GetString(objv[0]), "left") == 0) {
		    format_flags |= DT_LEFT;
		} else if (strcmp(Tcl_GetString(objv[0]), "center") == 0) {
		    format_flags |= DT_CENTER;
		} else if (strcmp(Tcl_GetString(objv[0]), "right") == 0) {
		    format_flags |= DT_RIGHT;
		}
	    }
	} else if (strcmp(Tcl_GetString(objv[0]), "-text") == 0) {
	    argc--;
	    objv++;
	    if (argc > 0) {
		string = Tcl_GetString(objv[0]);
	    }
	} else if (strcmp(Tcl_GetString(objv[0]), "-font") == 0) {
	    argc--;
	    objv++;
	    if (GdiMakeLogFont(interp, Tcl_GetString(objv[0]), &lf, hDC)) {
		if ((hfont = CreateFontIndirectW(&lf)) != NULL) {
		    made_font = 1;
		    oldfont = SelectObject(hDC, hfont);
		}
	    }
	    /* Else leave the font alone! */
	} else if (strcmp(Tcl_GetString(objv[0]), "-stipple") == 0) {
	    argc--;
	    objv++;
	    /* Not implemented yet. */
	} else if (strcmp(Tcl_GetString(objv[0]), "-fill") == 0) {
	    argc--;
	    objv++;
	    /* Get text color. */
	    if (GdiGetColor(objv[0], &textcolor)) {
		dotextcolor = 1;
	    }
	} else if (strcmp(Tcl_GetString(objv[0]), "-width") == 0) {
	    argc--;
	    objv++;
	    if (argc > 0) {
		int value;
		if (Tcl_GetIntFromObj(interp, objv[0], &value) != TCL_OK) {
		    return TCL_ERROR;
		}
		sizerect.right += value;
	    }
	    /* If a width is specified, break at words. */
	    format_flags |= DT_WORDBREAK;
	} else if (strcmp(Tcl_GetString(objv[0]), "-single") == 0) {
	    usesingle = 1;
	} else if (strcmp(Tcl_GetString(objv[0]), "-backfill") == 0) {
	    dobgmode = 1;
	}

	argc--;
	objv++;
    }

    if (string == 0) {
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

    /* Set the format flags for -single: Overrides -width. */
    if (usesingle == 1) {
	format_flags |= DT_SINGLELINE;
	format_flags |= DT_NOCLIP;
	format_flags &= ~DT_WORDBREAK;
    }

    Tcl_DStringInit(&tds);
    /* Just for fun, let's try translating string to Unicode. */
    wstring = Tcl_UtfToWCharDString(string, TCL_INDEX_NONE, &tds);
    DrawTextW(hDC, wstring, Tcl_DStringLength(&tds)/2, &sizerect,
	    format_flags | DT_CALCRECT);

    /* Adjust the rectangle according to the anchor. */
    x = y = 0;
    switch (anchor) {
    case TK_ANCHOR_N:
	x = (sizerect.right - sizerect.left) / 2;
	break;
    case TK_ANCHOR_S:
	x = (sizerect.right - sizerect.left) / 2;
	y = (sizerect.bottom - sizerect.top);
	break;
    case TK_ANCHOR_E:
	x = (sizerect.right - sizerect.left);
	y = (sizerect.bottom - sizerect.top) / 2;
	break;
    case TK_ANCHOR_W:
	y = (sizerect.bottom - sizerect.top) / 2;
	break;
    case TK_ANCHOR_NE:
	x = (sizerect.right - sizerect.left);
	break;
    case TK_ANCHOR_NW:
	break;
    case TK_ANCHOR_SE:
	x = (sizerect.right - sizerect.left);
	y = (sizerect.bottom - sizerect.top);
	break;
    case TK_ANCHOR_SW:
	y = (sizerect.bottom - sizerect.top);
	break;
    default:
	x = (sizerect.right - sizerect.left) / 2;
	y = (sizerect.bottom - sizerect.top) / 2;
	break;
    }
    sizerect.right  -= x;
    sizerect.left   -= x;
    sizerect.top    -= y;
    sizerect.bottom -= y;

    /* Get the color right. */
    if (dotextcolor) {
	textcolor = SetTextColor(hDC, textcolor);
    }

    if (dobgmode) {
	bgmode = SetBkMode(hDC, OPAQUE);
    } else {
	bgmode = SetBkMode(hDC, TRANSPARENT);
    }

    /* Print the text. */
    retval = DrawTextW(hDC, wstring,
	    Tcl_DStringLength(&tds)/2, &sizerect, format_flags);
    Tcl_DStringFree(&tds);

    /* Get the color set back. */
    if (dotextcolor) {
	textcolor = SetTextColor(hDC, textcolor);
    }
    SetBkMode(hDC, bgmode);
    if (made_font) {
	SelectObject(hDC, oldfont);
	DeleteObject(hfont);
    }

    /* In this case, the return value is the height of the text. */
    Tcl_SetObjResult(interp, Tcl_NewIntObj(retval));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiGetHdcInfo --
 *
 *	Gets salient characteristics of the CTM.
 *
 * Results:
 *	The return value is 0 if any failure occurs--in which case none of the
 *	other values are meaningful.  Otherwise the return value is the
 *	current mapping mode.
 *
 *----------------------------------------------------------------------
 */

static int GdiGetHdcInfo(
    HDC hdc,
    LPPOINT worigin,
    LPSIZE wextent,
    LPPOINT vorigin,
    LPSIZE vextent)
{
    int mapmode;
    int retval;

    memset(worigin, 0, sizeof(POINT));
    memset(vorigin, 0, sizeof(POINT));
    memset(wextent, 0, sizeof(SIZE));
    memset(vextent, 0, sizeof(SIZE));

    if ((mapmode = GetMapMode(hdc)) == 0) {
	/* Failed! */
	retval = 0;
    } else {
	retval = mapmode;
    }

    if (GetWindowExtEx(hdc, wextent) == FALSE) {
	/* Failed! */
	retval = 0;
    }
    if (GetViewportExtEx(hdc, vextent) == FALSE) {
	/* Failed! */
	retval = 0;
    }
    if (GetWindowOrgEx(hdc, worigin) == FALSE) {
	/* Failed! */
	retval = 0;
    }
    if (GetViewportOrgEx(hdc, vorigin) == FALSE) {
	/* Failed! */
	retval = 0;
    }

    return retval;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiNameToMode --
 *
 *	Converts Windows mapping mode names.
 *
 * Results:
 *	Mapping modes are delineated.
 *
 *----------------------------------------------------------------------
 */

static int GdiNameToMode(
    const char *name)
{
    static const struct gdimodes {
	int mode;
	const char *name;
    } modes[] = {
	{ MM_ANISOTROPIC, "MM_ANISOTROPIC" },
	{ MM_HIENGLISH,   "MM_HIENGLISH" },
	{ MM_HIMETRIC,    "MM_HIMETRIC" },
	{ MM_ISOTROPIC,   "MM_ISOTROPIC" },
	{ MM_LOENGLISH,   "MM_LOENGLISH" },
	{ MM_LOMETRIC,    "MM_LOMETRIC" },
	{ MM_TEXT,        "MM_TEXT" },
	{ MM_TWIPS,       "MM_TWIPS" }
    };

    size_t i;
    for (i=0; i < sizeof(modes) / sizeof(struct gdimodes); i++) {
	if (strcmp(modes[i].name, name) == 0) {
	    return modes[i].mode;
	}
    }
    return atoi(name);
}

/*
 *----------------------------------------------------------------------
 *
 * GdiModeToName --
 *
 *	Converts the mode number to a printable form.
 *
 * Results:
 *	Mapping numbers are delineated.
 *
 *----------------------------------------------------------------------
 */

static const char *GdiModeToName(
    int mode)
{
    static const struct gdi_modes {
	int mode;
	const char *name;
    } modes[] = {
	{ MM_ANISOTROPIC, "Anisotropic" },
	{ MM_HIENGLISH,   "1/1000 inch" },
	{ MM_HIMETRIC,    "1/100 mm" },
	{ MM_ISOTROPIC,   "Isotropic" },
	{ MM_LOENGLISH,   "1/100 inch" },
	{ MM_LOMETRIC,    "1/10 mm" },
	{ MM_TEXT,        "1 to 1" },
	{ MM_TWIPS,       "1/1440 inch" }
    };

    size_t i;
    for (i=0; i < sizeof(modes) / sizeof(struct gdi_modes); i++) {
	if (modes[i].mode == mode) {
	    return modes[i].name;
	}
    }
    return "Unknown";
}

/*
 *----------------------------------------------------------------------
 *
 * GdiMap --
 *
 *	Sets mapping mode between logical and physical device space.
 *
 * Results:
 *	Bridges map modes.
 *
 *----------------------------------------------------------------------
 */

static int GdiMap(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int argc,
    Tcl_Obj *const *objv)
{
    static const char usage_message[] =
	"::tk::print::_gdi map hdc "
	"[-logical x[y]] [-physical x[y]] "
	"[-offset {x y} ] [-default] [-mode mode]";
    HDC hdc;
    int mapmode;	/* Mapping mode. */
    SIZE wextent;	/* Device extent. */
    SIZE vextent;	/* Viewport extent. */
    POINT worigin;	/* Device origin. */
    POINT vorigin;	/* Viewport origin. */
    int argno;

    /* Keep track of what parts of the function need to be executed. */
    int need_usage   = 0;
    int use_logical  = 0;
    int use_physical = 0;
    int use_offset   = 0;
    int use_default  = 0;
    int use_mode     = 0;

    /* Required parameter: HDC for printer. */
    if (argc < 2) {
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

    hdc = printDC;

    if ((mapmode = GdiGetHdcInfo(hdc, &worigin, &wextent, &vorigin, &vextent)) == 0) {
	/* Failed!. */
	Tcl_AppendResult(interp, "Cannot get current HDC info", (char *)NULL);
	return TCL_ERROR;
    }

    /* Parse remaining arguments. */
    for (argno = 2; argno < argc; argno++) {
	if (strcmp(Tcl_GetString(objv[argno]), "-default") == 0) {
	    vextent.cx = vextent.cy = wextent.cx = wextent.cy = 1;
	    vorigin.x = vorigin.y = worigin.x = worigin.y = 0;
	    mapmode = MM_TEXT;
	    use_default = 1;
	} else if (strcmp(Tcl_GetString(objv[argno]), "-mode") == 0) {
	    if (argno + 1 >= argc) {
		need_usage = 1;
	    } else {
		mapmode = GdiNameToMode(Tcl_GetString(objv[argno + 1]));
		use_mode = 1;
		argno++;
	    }
	} else if (strcmp(Tcl_GetString(objv[argno]), "-offset") == 0) {
	    if (argno + 1 >= argc) {
		need_usage = 1;
	    } else {
		/* It would be nice if this parsed units as well.... */
		if (sscanf(Tcl_GetString(objv[argno + 1]), "%ld%ld",
			&vorigin.x, &vorigin.y) == 2) {
		    use_offset = 1;
		} else {
		    need_usage = 1;
		}
		argno++;
	    }
	} else if (strcmp(Tcl_GetString(objv[argno]), "-logical") == 0) {
	    if (argno + 1 >= argc) {
		need_usage = 1;
	    } else {
		int count;

		argno++;
		/* In "real-life", this should parse units as well.. */
		if ((count = sscanf(Tcl_GetString(objv[argno]), "%ld%ld",
			&wextent.cx, &wextent.cy)) != 2) {
		    if (count == 1) {
			mapmode = MM_ISOTROPIC;
			use_logical = 1;
			wextent.cy = wextent.cx;  /* Make them the same. */
		    } else {
			need_usage = 1;
		    }
		} else {
		    mapmode = MM_ANISOTROPIC;
		    use_logical = 2;
		}
	    }
	} else if (strcmp(Tcl_GetString(objv[argno]), "-physical") == 0) {
	    if (argno + 1 >= argc) {
		need_usage = 1;
	    } else {
		int count;

		argno++;
		/* In "real-life", this should parse units as well.. */
		if ((count = sscanf(Tcl_GetString(objv[argno]), "%ld%ld",
			&vextent.cx, &vextent.cy)) != 2) {
		    if (count == 1) {
			mapmode = MM_ISOTROPIC;
			use_physical = 1;
			vextent.cy = vextent.cx;  /* Make them the same. */
		    } else {
			need_usage = 1;
		    }
		} else {
		    mapmode = MM_ANISOTROPIC;
		    use_physical = 2;
		}
	    }
	}
    }

    /* Check for any impossible combinations. */
    if (use_logical != use_physical) {
	need_usage = 1;
    }
    if (use_default && (use_logical || use_offset || use_mode)) {
	need_usage = 1;
    }
    if (use_mode && use_logical &&
	    (mapmode != MM_ISOTROPIC && mapmode != MM_ANISOTROPIC)) {
	need_usage = 1;
    }

    if (need_usage) {
	Tcl_AppendResult(interp, usage_message, NULL);
	return TCL_ERROR;
    }

    /* Call Windows CTM functions. */
    if (use_logical || use_default || use_mode) { /* Don't call for offset only. */
	SetMapMode(hdc, mapmode);
    }

    if (use_offset || use_default) {
	POINT oldorg;
	SetViewportOrgEx(hdc, vorigin.x, vorigin.y, &oldorg);
	SetWindowOrgEx(hdc, worigin.x, worigin.y, &oldorg);
    }

    if (use_logical) {  /* Same as use_physical. */
	SIZE oldsiz;
	SetWindowExtEx(hdc, wextent.cx, wextent.cy, &oldsiz);
	SetViewportExtEx(hdc, vextent.cx, vextent.cy, &oldsiz);
    }

    /*
     * Since we may not have set up every parameter, get them again for the
     * report.
     */
    mapmode = GdiGetHdcInfo(hdc, &worigin, &wextent, &vorigin, &vextent);

    /*
     * Output current CTM info.
     * Note: This should really be in terms that can be used in a
     * ::tk::print::_gdi map command!
     */
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "Transform: \"(%ld, %ld) -> (%ld, %ld)\" "
	    "Origin: \"(%ld, %ld)\" "
	    "MappingMode: \"%s\"",
	    vextent.cx, vextent.cy, wextent.cx, wextent.cy,
	    vorigin.x, vorigin.y,
	    GdiModeToName(mapmode)));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiCopyBits --
 *
 *	Copies window bits from source to destination.
 *
 * Results:
 *	Copies window bits.
 *
 *----------------------------------------------------------------------
 */

static int GdiCopyBits(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int argc,
    Tcl_Obj *const *objv)
{
    /* Goal: get the Tk_Window from the top-level
     * convert it to an HWND
     * get the HDC
     * Do a bitblt to the given hdc
     * Use an optional parameter to point to an arbitrary window instead of
     * the main
     * Use optional parameters to map to the width and height required for the
     * dest.
     */
    static const char usage_message[] =
	"::tk::print::_gdi copybits hdc [-window w|-screen] [-client] "
	"[-source \"a b c d\"] "
	"[-destination \"a b c d\"] [-scale number] [-calc]";

    Tk_Window mainWin;
    Tk_Window workwin;
    Window wnd;
    HDC src;
    HDC dst;
    HWND hwnd = 0;

    HANDLE hDib;    /* Handle for device-independent bitmap. */
    LPBITMAPINFOHEADER lpDIBHdr;
    LPSTR lpBits;
    enum PrintType wintype = PTWindow;

    int hgt, wid;
    char *strend;
    long errcode;
    int k;

    /* Variables to remember what we saw in the arguments. */
    int do_window = 0;
    int do_screen = 0;
    int do_scale = 0;
    int do_print = 1;

    /* Variables to remember the values in the arguments. */
    const char *window_spec;
    double scale = 1.0;
    int src_x = 0, src_y = 0, src_w = 0, src_h = 0;
    int dst_x = 0, dst_y = 0, dst_w = 0, dst_h = 0;
    int is_toplevel = 0;

    /*
     * The following steps are peculiar to the top level window.
     * There is likely a clever way to do the mapping of a widget pathname to
     * the proper window, to support the idea of using a parameter for this
     * purpose.
     */
    if ((workwin = mainWin = Tk_MainWindow(interp)) == 0) {
	Tcl_AppendResult(interp, "Can't find main Tk window", (char *)NULL);
	return TCL_ERROR;
    }

    /*
     * Parse the arguments.
     */
    /* HDC is required. */
    if (argc < 2) {
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

    dst = printDC;

    /*
     * Next, check to see if 'dst' can support BitBlt.  If not, raise an
     * error.
     */
    if ((GetDeviceCaps(dst, RASTERCAPS) & RC_BITBLT) == 0) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"Can't do bitmap operations on device context\n"));
	return TCL_ERROR;
    }

    /* Loop through the remaining arguments. */
    for (k=2; k<argc; k++) {
	if (strcmp(Tcl_GetString(objv[k]), "-window") == 0) {
	    if (Tcl_GetString(objv[k+1]) && Tcl_GetString(objv[k+1])[0] == '.') {
		do_window = 1;
		workwin = Tk_NameToWindow(interp, window_spec = Tcl_GetString(objv[++k]), mainWin);
		if (workwin == NULL) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			    "Can't find window %s in this application",
			    window_spec));
		    return TCL_ERROR;
		}
	    } else {
		/* Use strtoul() so octal or hex representations will be
		 * parsed. */
		hwnd = (HWND) INT2PTR(strtoul(Tcl_GetString(objv[++k]), &strend, 0));
		if (strend == 0 || strend == Tcl_GetString(objv[k])) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			    "Can't understand window id %s", Tcl_GetString(objv[k])));
		    return TCL_ERROR;
		}
	    }
	} else if (strcmp(Tcl_GetString(objv[k]), "-screen") == 0) {
	    do_screen = 1;
	    wintype = PTScreen;
	} else if (strcmp(Tcl_GetString(objv[k]), "-client") == 0) {
	    wintype = PTClient;
	} else if (strcmp(Tcl_GetString(objv[k]), "-source") == 0) {
	    float a, b, c, d;
	    int count = sscanf(Tcl_GetString(objv[++k]), "%f%f%f%f", &a, &b, &c, &d);

	    if (count < 2) { /* Can't make heads or tails of it.... */
		Tcl_AppendResult(interp, usage_message, (char *)NULL);
		return TCL_ERROR;
	    }
	    src_x = (int)a;
	    src_y = (int)b;
	    if (count == 4) {
		src_w = (int)c;
		src_h = (int)d;
	    }
	} else if (strcmp(Tcl_GetString(objv[k]), "-destination") == 0) {
	    float a, b, c, d;
	    int count;

	    count = sscanf(Tcl_GetString(objv[++k]), "%f%f%f%f", &a, &b, &c, &d);
	    if (count < 2) { /* Can't make heads or tails of it.... */
		Tcl_AppendResult(interp, usage_message, (char *)NULL);
		return TCL_ERROR;
	    }
	    dst_x = (int)a;
	    dst_y = (int)b;
	    if (count == 3) {
		dst_w = (int)c;
		dst_h = -1;
	    } else if (count == 4) {
		dst_w = (int)c;
		dst_h = (int)d;
	    }
	} else if (strcmp(Tcl_GetString(objv[k]), "-scale") == 0) {
	    if (Tcl_GetString(objv[++k])) {
		if (Tcl_GetDouble(interp, Tcl_GetString(objv[k]), &scale) != TCL_OK) {
		    return TCL_ERROR;
		}
		if (scale <= 0.01 || scale >= 100.0) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			    "Unreasonable scale specification %s", Tcl_GetString(objv[k])));
		    return TCL_ERROR;
		}
		do_scale = 1;
	    }
	} else if (strcmp(Tcl_GetString(objv[k]), "-noprint") == 0
		|| strncmp(Tcl_GetString(objv[k]), "-calc", 5) == 0) {
	    /* This option suggested by Pascal Bouvier to get sizes without
	     * printing. */
	    do_print = 0;
	}
    }

    /*
     * Check to ensure no incompatible arguments were used.
     */
    if (do_window && do_screen) {
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

    /*
     * Get the MS Window we want to copy.  Given the HDC, we can get the
     * "Window".
     */
    if (hwnd == 0) {
	if (Tk_IsTopLevel(workwin)) {
	    is_toplevel = 1;
	}

	if ((wnd = Tk_WindowId(workwin)) == 0) {
	    Tcl_AppendResult(interp, "Can't get id for Tk window", (char *)NULL);
	    return TCL_ERROR;
	}

	/* Given the "Window" we can get a Microsoft Windows HWND. */

	if ((hwnd = Tk_GetHWND(wnd)) == 0) {
	    Tcl_AppendResult(interp, "Can't get Windows handle for Tk window",
		    (char *)NULL);
	    return TCL_ERROR;
	}

	/*
	 * If it's a toplevel, give it special treatment: Get the top-level
	 * window instead.  If the user only wanted the client, the -client
	 * flag will take care of it.  This uses "windows" tricks rather than
	 * Tk since the obvious method of getting the wrapper window didn't
	 * seem to work.
	 */
	if (is_toplevel) {
	    HWND tmpWnd = hwnd;
	    while ((tmpWnd = GetParent(tmpWnd)) != 0) {
		hwnd = tmpWnd;
	    }
	}
    }

    /* Given the HWND, we can get the window's device context. */
    if ((src = GetWindowDC(hwnd)) == 0) {
	Tcl_AppendResult(interp, "Can't get device context for Tk window", (char *)NULL);
	return TCL_ERROR;
    }

    if (do_screen) {
	LONG w, h;
	GetDisplaySize(&w, &h);
	wid = w;
	hgt = h;
    } else if (is_toplevel) {
	RECT tl;
	GetWindowRect(hwnd, &tl);
	wid = tl.right - tl.left;
	hgt = tl.bottom - tl.top;
    } else {
	if ((hgt = Tk_Height(workwin)) <= 0) {
	    Tcl_AppendResult(interp, "Can't get height of Tk window", (char *)NULL);
	    ReleaseDC(hwnd,src);
	    return TCL_ERROR;
	}

	if ((wid = Tk_Width(workwin)) <= 0) {
	    Tcl_AppendResult(interp, "Can't get width of Tk window", (char *)NULL);
	    ReleaseDC(hwnd,src);
	    return TCL_ERROR;
	}
    }

    /*
     * Ensure all the widths and heights are set up right
     * A: No dimensions are negative
     * B: No dimensions exceed the maximums
     * C: The dimensions don't lead to a 0 width or height image.
     */
    if (src_x < 0) {
	src_x = 0;
    }
    if (src_y < 0) {
	src_y = 0;
    }
    if (dst_x < 0) {
	dst_x = 0;
    }
    if (dst_y < 0) {
	dst_y = 0;
    }

    if (src_w > wid || src_w <= 0) {
	src_w = wid;
    }

    if (src_h > hgt || src_h <= 0) {
	src_h = hgt;
    }

    if (do_scale && dst_w == 0) {
	/* Calculate destination width and height based on scale. */
	dst_w = (int)(scale * src_w);
	dst_h = (int)(scale * src_h);
    }

    if (dst_h == -1) {
	dst_h = (int) (((long)src_h * dst_w) / (src_w + 1)) + 1;
    }

    if (dst_h == 0 || dst_w == 0) {
	dst_h = src_h;
	dst_w = src_w;
    }

    if (do_print) {
	/*
	 * Based on notes from Heiko Schock and Arndt Roger Schneider, create
	 * this as a DIBitmap, to allow output to a greater range of devices.
	 * This approach will also allow selection of
	 *   a) Whole screen
	 *   b) Whole window
	 *   c) Client window only
	 * for the "grab"
	 */
	hDib = CopyToDIB(hwnd, wintype);

	/* GdiFlush();. */

	if (!hDib) {
	    Tcl_AppendResult(interp, "Can't create DIB", (char *)NULL);
	    ReleaseDC(hwnd,src);
	    return TCL_ERROR;
	}

	lpDIBHdr = (LPBITMAPINFOHEADER) GlobalLock(hDib);
	if (!lpDIBHdr) {
	    Tcl_AppendResult(interp, "Can't get DIB header", (char *)NULL);
	    ReleaseDC(hwnd,src);
	    return TCL_ERROR;
	}

	lpBits = (LPSTR) lpDIBHdr + lpDIBHdr->biSize + DIBNumColors(lpDIBHdr) * sizeof(RGBQUAD);

	/* stretch the DIBbitmap directly in the target device. */

	if (StretchDIBits(dst,
		dst_x, dst_y, dst_w, dst_h,
		src_x, src_y, src_w, src_h,
		lpBits, (LPBITMAPINFO)lpDIBHdr, DIB_RGB_COLORS,
		SRCCOPY) == (int)GDI_ERROR) {
	    errcode = GetLastError();
	    GlobalUnlock(hDib);
	    GlobalFree(hDib);
	    ReleaseDC(hwnd,src);
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "StretchDIBits failed with code %ld", errcode));
	    return TCL_ERROR;
	}

	/* free allocated memory. */
	GlobalUnlock(hDib);
	GlobalFree(hDib);
    }

    ReleaseDC(hwnd,src);

    /*
     * The return value should relate to the size in the destination space.
     * At least the height should be returned (for page layout purposes).
     */
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "%d %d %d %d", dst_x, dst_y, dst_w, dst_h));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DIBNumColors --
 *
 *	Computes the number of colors required for a DIB palette.
 *
 * Results:
 *	Returns number of colors.

 *
 *----------------------------------------------------------------------
 */

static int DIBNumColors(
    LPBITMAPINFOHEADER lpDIB)
{
    WORD wBitCount;	/* DIB bit count. */
    DWORD dwClrUsed;

    /*
     * If this is a Windows-style DIB, the number of colors in the color table
     * can be less than the number of bits per pixel allows for (i.e.
     * lpbi->biClrUsed can be set to some value).  If this is the case, return
     * the appropriate value..
     */

    dwClrUsed = lpDIB->biClrUsed;
    if (dwClrUsed) {
	return (WORD) dwClrUsed;
    }

    /*
     * Calculate the number of colors in the color table based on.
     * The number of bits per pixel for the DIB.
     */

    wBitCount = lpDIB->biBitCount;

    /* Return number of colors based on bits per pixel. */

    switch (wBitCount) {
    case 1:
	return 2;
    case 4:
	return 16;
    case 8:
	return 256;
    default:
	return 0;
    }
}

/*
 * Helper functions
 */

/*
 * ParseFontWords converts various keywords to modifyers of a
 * font specification.
 * For all words, later occurrences override earlier occurrences.
 * Overstrike and underline cannot be "undone" by other words
 */

/*
 *----------------------------------------------------------------------
 *
 * GdiParseFontWords --
 *
 *	Converts various keywords to modifiers of a font specification.  For
 *	all words, later occurrences override earlier occurrences.  Overstrike
 *	and underline cannot be "undone" by other words
 *
 * Results:
 *	 Keywords converted to modifiers.
 *
 *----------------------------------------------------------------------
 */

static int GdiParseFontWords(
    TCL_UNUSED(Tcl_Interp *),
    LOGFONTW *lf,
    const char *str[],
    int numargs)
{
    int i;
    int retval = 0; /* Number of words that could not be parsed. */

    for (i=0; i<numargs; i++) {
	if (str[i]) {
	    int wt;
	    if ((wt = GdiWordToWeight(str[i])) != -1) {
		lf->lfWeight = wt;
	    } else if (strcmp(str[i], "roman") == 0) {
		lf->lfItalic = FALSE;
	    } else if (strcmp(str[i], "italic") == 0) {
		lf->lfItalic = TRUE;
	    } else if (strcmp(str[i], "underline") == 0) {
		lf->lfUnderline = TRUE;
	    } else if (strcmp(str[i], "overstrike") == 0) {
		lf->lfStrikeOut = TRUE;
	    } else {
		retval++;
	    }
	}
    }
    return retval;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiWordToWeight --
 *
 *	Converts keywords to font weights.
 *
 * Results:
 *	Helps set the proper font for GDI rendering.
 *
 *----------------------------------------------------------------------
 */

static int GdiWordToWeight(
    const char *str)
{
    int retval = -1;
    size_t i;
    static const struct font_weight {
	const char *name;
	int weight;
    } font_weights[] = {
	{ "thin", FW_THIN },
	{ "extralight", FW_EXTRALIGHT },
	{ "ultralight", FW_EXTRALIGHT },
	{ "light", FW_LIGHT },
	{ "normal", FW_NORMAL },
	{ "regular", FW_NORMAL },
	{ "medium", FW_MEDIUM },
	{ "semibold", FW_SEMIBOLD },
	{ "demibold", FW_SEMIBOLD },
	{ "bold", FW_BOLD },
	{ "extrabold", FW_EXTRABOLD },
	{ "ultrabold", FW_EXTRABOLD },
	{ "heavy", FW_HEAVY },
	{ "black", FW_HEAVY },
    };

    if (str == 0) {
	return -1;
    }

    for (i=0; i<sizeof(font_weights) / sizeof(struct font_weight); i++) {
	if (strcmp(str, font_weights[i].name) == 0) {
	    retval = font_weights[i].weight;
	    break;
	}
    }

    return retval;
}

/*
 *----------------------------------------------------------------------
 *
 * MakeLogFont --
 *
 *	Takes the font description string and converts this into a logical
 *	font spec.
 *
 * Results:
 *	 Sets font weight.
 *
 *----------------------------------------------------------------------
 */

static int GdiMakeLogFont(
    Tcl_Interp *interp,
    const char *str,
    LOGFONTW *lf,
    HDC hDC)
{
    const char **list;
    Tcl_Size count;

    /* Set up defaults for logical font. */
    memset(lf, 0, sizeof(*lf));
    lf->lfWeight  = FW_NORMAL;
    lf->lfCharSet = DEFAULT_CHARSET;
    lf->lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf->lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf->lfQuality = DEFAULT_QUALITY;
    lf->lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;

    /* The cast to (char *) is silly, based on prototype of Tcl_SplitList. */
    if (Tcl_SplitList(interp, str, &count, &list) != TCL_OK) {
	return 0;
    }

    /* Now we have the font structure broken into name, size, weight. */
    if (count >= 1) {
	Tcl_DString ds;

	Tcl_DStringInit(&ds);
	wcsncpy(lf->lfFaceName, Tcl_UtfToWCharDString(list[0], TCL_INDEX_NONE, &ds),
		LF_FACESIZE-1);
	Tcl_DStringFree(&ds);
	lf->lfFaceName[LF_FACESIZE-1] = 0;
    } else {
	return 0;
    }

    if (count >= 2) {
	int siz;
	char *strend;
	siz = strtol(list[1], &strend, 0);

	/*
	 * Assumptions:
	 * 1) Like canvas, if a positive number is specified, it's in points.
	 * 2) Like canvas, if a negative number is specified, it's in pixels.
	 */
	if (strend > list[1]) { /* If it looks like a number, it is a number.... */
	    if (siz > 0) {  /* Size is in points. */
		SIZE wextent, vextent;
		POINT worigin, vorigin;
		double factor;

		switch (GdiGetHdcInfo(hDC, &worigin, &wextent, &vorigin, &vextent)) {
		case MM_ISOTROPIC:
		    if (vextent.cy < -1 || vextent.cy > 1) {
			factor = (double)wextent.cy / vextent.cy;
			if (factor < 0.0) {
			    factor = -factor;
			}
			lf->lfHeight = (int)(-siz * GetDeviceCaps(hDC, LOGPIXELSY) * factor / 72.0);
		    } else if (vextent.cx < -1 || vextent.cx > 1) {
			factor = (double)wextent.cx / vextent.cx;
			if (factor < 0.0) {
			    factor = -factor;
			}
			lf->lfHeight = (int)(-siz * GetDeviceCaps(hDC, LOGPIXELSY) * factor / 72.0);
		    } else {
			lf->lfHeight = -siz; /* This is bad news.... */
		    }
		    break;
		case MM_ANISOTROPIC:
		    if (vextent.cy != 0) {
			factor = (double)wextent.cy / vextent.cy;
			if (factor < 0.0) {
			    factor = -factor;
			}
			lf->lfHeight = (int)(-siz * GetDeviceCaps(hDC, LOGPIXELSY) * factor / 72.0);
		    } else {
			lf->lfHeight = -siz; /* This is bad news.... */
		    }
		    break;
		case MM_TEXT:
		default:
		    /* If mapping mode is MM_TEXT, use the documented
		     * formula. */
		    lf->lfHeight = -MulDiv(siz, GetDeviceCaps(hDC, LOGPIXELSY), 72);
		    break;
		case MM_HIENGLISH:
		    lf->lfHeight = -MulDiv(siz, 1000, 72);
		    break;
		case MM_LOENGLISH:
		    lf->lfHeight = -MulDiv(siz, 100, 72);
		    break;
		case MM_HIMETRIC:
		    lf->lfHeight = -MulDiv(siz, (int)(1000*2.54), 72);
		    break;
		case MM_LOMETRIC:
		    lf->lfHeight = -MulDiv(siz, (int)(100*2.54), 72);
		    break;
		case MM_TWIPS:
		    lf->lfHeight = -MulDiv(siz, 1440, 72);
		    break;
		}
	    } else if (siz == 0) {   /* Use default size of 12 points. */
		lf->lfHeight = -MulDiv(12, GetDeviceCaps(hDC, LOGPIXELSY), 72);
	    } else {                 /* Use pixel size. */
		lf->lfHeight = siz;  /* Leave this negative. */
	    }
	} else {
	    GdiParseFontWords(interp, lf, list+1, count-1);
	}
    }

    if (count >= 3) {
	GdiParseFontWords(interp, lf, list+2, count-2);
    }

    ckfree(list);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiMakePen --
 *
 *	Creates a logical pen based on input parameters and selects it into
 *	the hDC.
 *
 * Results:
 *	Sets rendering pen.
 *
 *----------------------------------------------------------------------
 */

static int GdiMakePen(
    Tcl_Interp *interp,
    int width,
    int dashstyle,
    const char *dashstyledata,
    TCL_UNUSED(int),		/* Ignored for now. */
    TCL_UNUSED(int),		/* Ignored for now. */
    TCL_UNUSED(int),
    TCL_UNUSED(const char *),	/* Ignored for now. */
    unsigned long color,
    HDC hDC,
    HGDIOBJ *oldPen)
{
    /*
     * The LOGPEN structure takes the following dash options:
     * PS_SOLID: a solid pen
     * PS_DASH:  a dashed pen
     * PS_DOT:   a dotted pen
     * PS_DASHDOT: a pen with a dash followed by a dot
     * PS_DASHDOTDOT: a pen with a dash followed by 2 dots
     *
     * It seems that converting to ExtCreatePen may be more advantageous, as
     * it matches the Tk canvas pens much better--but not for Win95, which
     * does not support PS_USERSTYLE. An explicit test (or storage in a static
     * after first failure) may suffice for working around this. The
     * ExtCreatePen is not supported at all under Win32.
     */

    HPEN hPen;
    LOGBRUSH lBrush;
    DWORD pStyle = PS_SOLID;           /* -dash should override*/
    DWORD endStyle = PS_ENDCAP_ROUND;  /* -capstyle should override. */
    DWORD joinStyle = PS_JOIN_ROUND;   /* -joinstyle should override. */
    DWORD styleCount = 0;
    DWORD *styleArray = 0;

    /*
     * To limit the propagation of allocated memory, the dashes will have a
     * maximum here.  If one wishes to remove the static allocation, please be
     * sure to update GdiFreePen and ensure that the array is NOT freed if the
     * LOGPEN option is used.
     */
    static DWORD pStyleData[24];
    if (dashstyle != 0 && dashstyledata != 0) {
	const char *cp;
	size_t i;
	char *dup = (char *) ckalloc(strlen(dashstyledata) + 1);
	strcpy(dup, dashstyledata);
	/* DEBUG. */
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"DEBUG: Found a dash spec of |%s|\n",
		dashstyledata));

	/* Parse the dash spec. */
	if (isdigit(dashstyledata[0])) {
	    cp = strtok(dup, " \t,;");
	    for (i = 0; cp && i < sizeof(pStyleData) / sizeof(DWORD); i++) {
		pStyleData[styleCount++] = atoi(cp);
		cp = strtok(NULL, " \t,;");
	    }
	} else {
	    for (i=0; dashstyledata[i] != '\0' && i< sizeof(pStyleData) / sizeof(DWORD); i++) {
		switch (dashstyledata[i]) {
		case ' ':
		    pStyleData[styleCount++] = 8;
		    break;
		case ',':
		    pStyleData[styleCount++] = 4;
		    break;
		case '_':
		    pStyleData[styleCount++] = 6;
		    break;
		case '-':
		    pStyleData[styleCount++] = 4;
		    break;
		case '.':
		    pStyleData[styleCount++] = 2;
		    break;
		default:
		    break;
		}
	    }
	}
	if (styleCount > 0) {
	    styleArray = pStyleData;
	} else {
	    dashstyle = 0;
	}
	if (dup) {
	    ckfree(dup);
	}
    }

    if (dashstyle != 0) {
	pStyle = PS_USERSTYLE;
    }

    /* -stipple could affect this.... */
    lBrush.lbStyle = BS_SOLID;
    lBrush.lbColor = color;
    lBrush.lbHatch = 0;

    /* We only use geometric pens, even for 1-pixel drawing. */
    hPen = ExtCreatePen(PS_GEOMETRIC|pStyle|endStyle|joinStyle,
	    width, &lBrush, styleCount, styleArray);

    if (hPen == 0) { /* Failed for some reason...Fall back on CreatePenIndirect. */
	LOGPEN lf;
	lf.lopnWidth.x = width;
	lf.lopnWidth.y = 0;		/* Unused in LOGPEN. */
	if (dashstyle == 0) {
	    lf.lopnStyle = PS_SOLID;	/* For now...convert 'style' in the future. */
	} else {
	    lf.lopnStyle = PS_DASH;	/* REALLLLY simple for now. */
	}
	lf.lopnColor = color;		/* Assume we're getting a COLORREF. */
	/* Now we have a logical pen. Create the "real" pen and put it in the
	 * hDC. */
	hPen = CreatePenIndirect(&lf);
    }

    *oldPen = SelectObject(hDC, hPen);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiFreePen --
 *
 *	Wraps the protocol to delete a created pen.
 *
 * Results:
 *	Deletes pen.
 *
 *----------------------------------------------------------------------
 */

static int GdiFreePen(
    TCL_UNUSED(Tcl_Interp *),
    HDC hDC,
    HGDIOBJ oldPen)
{
    HGDIOBJ gonePen = SelectObject(hDC, oldPen);

    DeleteObject(gonePen);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiMakeBrush--
 *
 *	Creates a logical brush based on input parameters, and selects it into
 *	the hdc.
 *
 * Results:
 *	 Creates brush.
 *
 *----------------------------------------------------------------------
 */

static int GdiMakeBrush(
    unsigned long color,
    long hatch,
    LOGBRUSH *lb,
    HDC hDC,
	HBRUSH *oldBrush)
{
    HBRUSH hBrush;
    lb->lbStyle = BS_SOLID; /* Support other styles later. */
    lb->lbColor = color;    /* Assume this is a COLORREF. */
    lb->lbHatch = hatch;    /* Ignored for now, given BS_SOLID in the Style. */

    /* Now we have the logical brush. Create the "real" brush and put it in
     * the hDC. */
    hBrush = CreateBrushIndirect(lb);
    *oldBrush = (HBRUSH)SelectObject(hDC, hBrush);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * GdiFreeBrush --
 *
 *	Wraps the protocol to delete a created brush.
 *
 * Results:
 *	 Deletes brush.
 *
 *----------------------------------------------------------------------
 */
static void GdiFreeBrush(
    TCL_UNUSED(Tcl_Interp *),
    HDC hDC,
    HGDIOBJ oldBrush)
{
    HGDIOBJ goneBrush;

    goneBrush = SelectObject(hDC, oldBrush);
    DeleteObject(goneBrush);
}

/*
 * Utility functions from elsewhere in Tcl.
 * Functions have removed reliance on X and Tk libraries, as well as removing
 * the need for TkWindows.
 * GdiGetColor is a copy of a TkpGetColor from tkWinColor.c
 */
typedef struct {
    const char *name;
    int index;
} SystemColorEntry;

static const SystemColorEntry sysColors[] = {
    {"3dDarkShadow",		COLOR_3DDKSHADOW},
    {"3dLight",			COLOR_3DLIGHT},
    {"ActiveBorder",		COLOR_ACTIVEBORDER},
    {"ActiveCaption",		COLOR_ACTIVECAPTION},
    {"AppWorkspace",		COLOR_APPWORKSPACE},
    {"Background",		COLOR_BACKGROUND},
    {"ButtonFace",		COLOR_BTNFACE},
    {"ButtonHighlight",		COLOR_BTNHIGHLIGHT},
    {"ButtonShadow",		COLOR_BTNSHADOW},
    {"ButtonText",		COLOR_BTNTEXT},
    {"CaptionText",		COLOR_CAPTIONTEXT},
    {"DisabledText",		COLOR_GRAYTEXT},
    {"GrayText",		COLOR_GRAYTEXT},
    {"Highlight",		COLOR_HIGHLIGHT},
    {"HighlightText",		COLOR_HIGHLIGHTTEXT},
    {"InactiveBorder",		COLOR_INACTIVEBORDER},
    {"InactiveCaption",		COLOR_INACTIVECAPTION},
    {"InactiveCaptionText",	COLOR_INACTIVECAPTIONTEXT},
    {"InfoBackground",		COLOR_INFOBK},
    {"InfoText",		COLOR_INFOTEXT},
    {"Menu",			COLOR_MENU},
    {"MenuText",		COLOR_MENUTEXT},
    {"Scrollbar",		COLOR_SCROLLBAR},
    {"Window",			COLOR_WINDOW},
    {"WindowFrame",		COLOR_WINDOWFRAME},
    {"WindowText",		COLOR_WINDOWTEXT}
};

static const size_t numsyscolors = sizeof(sysColors) / sizeof(SystemColorEntry);

/*
 *----------------------------------------------------------------------
 *
 * GdiGetColor --
 *
 *	Convert color name to color specification.
 *
 * Results:
 *	 Color name converted.
 *
 *----------------------------------------------------------------------
 */

static int GdiGetColor(
    Tcl_Obj *nameObj,
    COLORREF *color)
{
    const char *name = Tcl_GetString(nameObj);

    if (_strnicmp(name, "system", 6) == 0) {
	size_t i, l, u;
	int r;

	l = 0;
	u = numsyscolors;
	while (l <= u) {
	    i = (l + u) / 2;
	    if ((r = _strcmpi(name+6, sysColors[i].name)) == 0) {
		break;
	    }
	    if (r < 0) {
		u = i - 1;
	    } else {
		l = i + 1;
	    }
	}
	if (l > u) {
	    return 0;
	}
	*color = GetSysColor(sysColors[i].index);
	return 1;
    } else {
    int result;
    XColor xcolor;
	result = XParseColor(NULL, 0, name, &xcolor);
	*color = ((xcolor.red & 0xFF00)>>8) | (xcolor.green & 0xFF00)
		| ((xcolor.blue & 0xFF00)<<8);
    return result;
    }
}

/*
 * Beginning of functions for screen-to-dib translations.
 *
 * Several of these functions are based on those in the WINCAP32 program
 * provided as a sample by Microsoft on the VC++ 5.0 disk. The copyright on
 * these functions is retained, even for those with significant changes.
 */

/*
 *----------------------------------------------------------------------
 *
 * CopyToDIB --
 *
 *	Copy window bits to a DIB.
 *
 * Results:
 *	 Color specification converted.
 *
 *----------------------------------------------------------------------
 */

static HANDLE CopyToDIB(
    HWND hWnd,
    enum PrintType type)
{
    HANDLE hDIB;
    HBITMAP hBitmap;
    HPALETTE hPalette;

    /* Check for a valid window handle. */

    if (!hWnd) {
	return NULL;
    }

    switch (type) {
    case PTWindow: {	/* Copy entire window. */
	RECT rectWnd;

	/* Get the window rectangle. */

	GetWindowRect(hWnd, &rectWnd);

	/*
	 * Get the DIB of the window by calling CopyScreenToDIB and passing it
	 * the window rect.
	 */

	hDIB = CopyScreenToDIB(&rectWnd);
	break;
    }

    case PTClient: {	/* Copy client area. */
	RECT rectClient;
	POINT pt1, pt2;

	/* Get the client area dimensions. */

	GetClientRect(hWnd, &rectClient);

	/* Convert client coords to screen coords. */

	pt1.x = rectClient.left;
	pt1.y = rectClient.top;
	pt2.x = rectClient.right;
	pt2.y = rectClient.bottom;
	ClientToScreen(hWnd, &pt1);
	ClientToScreen(hWnd, &pt2);
	rectClient.left = pt1.x;
	rectClient.top = pt1.y;
	rectClient.right = pt2.x;
	rectClient.bottom = pt2.y;

	/*
	 * Get the DIB of the client area by calling CopyScreenToDIB and
	 * passing it the client rect.
	 */

	hDIB = CopyScreenToDIB(&rectClient);
	break;
    }

    case PTScreen: { /* Entire screen. */
	RECT Rect;

	/*
	 * Get the device-dependent bitmap in lpRect by calling
	 * CopyScreenToBitmap and passing it the rectangle to grab.
	 */
	Rect.top = Rect.left = 0;
	GetDisplaySize(&Rect.right, &Rect.bottom);

	hBitmap = CopyScreenToBitmap(&Rect);

	/* Check for a valid bitmap handle. */

	if (!hBitmap) {
	    return NULL;
	}

	/* Get the current palette. */

	hPalette = GetSystemPalette();

	/* Convert the bitmap to a DIB. */

	hDIB = BitmapToDIB(hBitmap, hPalette);

	/* Clean up. */

	DeleteObject(hPalette);
	DeleteObject(hBitmap);

	/* Return handle to the packed-DIB. */
	break;
    }
    default:	/* Invalid print area. */
	return NULL;
    }

    /* Return the handle to the DIB. */
    return hDIB;
}

/*
 *----------------------------------------------------------------------
 *
 * GetDisplaySize--
 *
 *	GetDisplaySize does just that.  There may be an easier way, but it is
 *	not apparent.
 *
 * Results:
 *	Returns display size.
 *
 *----------------------------------------------------------------------
 */

static void GetDisplaySize(
    LONG *width,
    LONG *height)
{
    HDC hDC;

    hDC = CreateDCW(L"DISPLAY", 0, 0, 0);
    *width = GetDeviceCaps(hDC, HORZRES);
    *height = GetDeviceCaps(hDC, VERTRES);
    DeleteDC(hDC);
}

/*
 *----------------------------------------------------------------------
 *
 * CopyScreenToBitmap--
 *
 *	Copies screen to bitmap.
 *
 * Results:
 *	Screen is copied.
 *
 *----------------------------------------------------------------------
 */

static HBITMAP CopyScreenToBitmap(
    LPRECT lpRect)
{
    HDC     hScrDC, hMemDC;	/* Screen DC and memory DC. */
    HGDIOBJ hBitmap, hOldBitmap; /* Handles to deice-dependent bitmaps. */
    int     nX, nY, nX2, nY2;	/* Coordinates of rectangle to grab. */
    int     nWidth, nHeight;	/* DIB width and height */
    int     xScrn, yScrn;	/* Screen resolution. */

    /* Check for an empty rectangle. */

    if (IsRectEmpty(lpRect)) {
	return NULL;
    }

    /*
     * Create a DC for the screen and create a memory DC compatible to screen
     * DC.
     */

    hScrDC = CreateDCW(L"DISPLAY", NULL, NULL, NULL);
    hMemDC = CreateCompatibleDC(hScrDC);

    /* Get points of rectangle to grab. */

    nX = lpRect->left;
    nY = lpRect->top;
    nX2 = lpRect->right;
    nY2 = lpRect->bottom;

    /* Get screen resolution. */

    xScrn = GetDeviceCaps(hScrDC, HORZRES);
    yScrn = GetDeviceCaps(hScrDC, VERTRES);

    /* Make sure bitmap rectangle is visible. */

    if (nX < 0) {
	nX = 0;
    }
    if (nY < 0) {
	nY = 0;
    }
    if (nX2 > xScrn) {
	nX2 = xScrn;
    }
    if (nY2 > yScrn) {
	nY2 = yScrn;
    }

    nWidth = nX2 - nX;
    nHeight = nY2 - nY;

    /* Create a bitmap compatible with the screen DC. */
    hBitmap = CreateCompatibleBitmap(hScrDC, nWidth, nHeight);

    /* Select new bitmap into memory DC. */
    hOldBitmap = SelectObject(hMemDC, hBitmap);

    /* Bitblt screen DC to memory DC. */
    BitBlt(hMemDC, 0, 0, nWidth, nHeight, hScrDC, nX, nY, SRCCOPY);

    /*
     * Select old bitmap back into memory DC and get handle to bitmap of the
     * screen.
     */

    hBitmap = SelectObject(hMemDC, hOldBitmap);

    /* Clean up. */

    DeleteDC(hScrDC);
    DeleteDC(hMemDC);

    /* Return handle to the bitmap. */

    return (HBITMAP)hBitmap;
}

/*
 *----------------------------------------------------------------------
 *
 * BitmapToDIB--
 *
 *	Converts bitmap to DIB.
 *
 * Results:
 *	Bitmap converted.
 *
 *----------------------------------------------------------------------
 */

static HANDLE BitmapToDIB(
    HBITMAP hBitmap,
    HPALETTE hPal)
{
    BITMAP              bm;
    BITMAPINFOHEADER    bi;
    LPBITMAPINFOHEADER  lpbi;
    DWORD               dwLen;
    HANDLE              hDIB;
    HANDLE              h;
    HDC                 hDC;
    WORD                biBits;

    /* Check if bitmap handle is valid. */

    if (!hBitmap) {
	return NULL;
    }

    /* Fill in BITMAP structure, return NULL if it didn't work. */

    if (!GetObjectW(hBitmap, sizeof(bm), (LPWSTR)&bm)) {
	return NULL;
    }

    /* Ff no palette is specified, use default palette. */

    if (hPal == NULL) {
	hPal = (HPALETTE)GetStockObject(DEFAULT_PALETTE);
    }

    /* Calculate bits per pixel. */

    biBits = bm.bmPlanes * bm.bmBitsPixel;

    /* Make sure bits per pixel is valid. */

    if (biBits <= 1) {
	biBits = 1;
    } else if (biBits <= 4) {
	biBits = 4;
    } else if (biBits <= 8) {
	biBits = 8;
    } else { /* If greater than 8-bit, force to 24-bit. */
	biBits = 24;
    }

    /* Initialize BITMAPINFOHEADER. */

    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bm.bmWidth;
    bi.biHeight = bm.bmHeight;
    bi.biPlanes = 1;
    bi.biBitCount = biBits;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    /* Calculate size of memory block required to store BITMAPINFO. */

    dwLen = bi.biSize + DIBNumColors(&bi) * sizeof(RGBQUAD);

    /* Get a DC. */

    hDC = GetDC(NULL);

    /* Select and realize our palette. */

    hPal = SelectPalette(hDC, hPal, FALSE);
    RealizePalette(hDC);

    /* Alloc memory block to store our bitmap. */

    hDIB = GlobalAlloc(GHND, dwLen);

    /* If we couldn't get memory block. */

    if (!hDIB) {
	/* clean up and return NULL. */

	SelectPalette(hDC, hPal, TRUE);
	RealizePalette(hDC);
	ReleaseDC(NULL, hDC);
	return NULL;
    }

    /* Lock memory and get pointer to it. */

    lpbi = (LPBITMAPINFOHEADER)GlobalLock(hDIB);

    /* Use our bitmap info. to fill BITMAPINFOHEADER. */

    *lpbi = bi;

    /* Call GetDIBits with a NULL lpBits param, so it will calculate the
     * biSizeImage field for us
     */

    GetDIBits(hDC, hBitmap, 0, (UINT)bi.biHeight, NULL, (LPBITMAPINFO)lpbi,
	    DIB_RGB_COLORS);

    /* get the info. returned by GetDIBits and unlock memory block. */

    bi = *lpbi;
    GlobalUnlock(hDIB);

    /* If the driver did not fill in the biSizeImage field, make one up. */
    if (bi.biSizeImage == 0) {
	bi.biSizeImage = (((((DWORD)bm.bmWidth * biBits) + 31) / 32) * 4)
		* bm.bmHeight;
    }

    /* Realloc the buffer big enough to hold all the bits. */

    dwLen = bi.biSize + DIBNumColors(&bi) * sizeof(RGBQUAD) + bi.biSizeImage;

    if ((h = GlobalReAlloc(hDIB, dwLen, 0)) != 0) {
	hDIB = h;
    } else {
	/* Clean up and return NULL. */

	GlobalFree(hDIB);
	SelectPalette(hDC, hPal, TRUE);
	RealizePalette(hDC);
	ReleaseDC(NULL, hDC);
	return NULL;
    }

    /* Lock memory block and get pointer to it. */

    lpbi = (LPBITMAPINFOHEADER)GlobalLock(hDIB);

    /* Call GetDIBits with a NON-NULL lpBits param, and actualy get the
     * bits this time.
     */

    if (GetDIBits(hDC, hBitmap, 0, (UINT)bi.biHeight, (LPSTR)lpbi +
	    (WORD)lpbi->biSize + DIBNumColors(lpbi) * sizeof(RGBQUAD),
	    (LPBITMAPINFO)lpbi, DIB_RGB_COLORS) == 0) {
	/* Clean up and return NULL. */

	GlobalUnlock(hDIB);
	SelectPalette(hDC, hPal, TRUE);
	RealizePalette(hDC);
	ReleaseDC(NULL, hDC);
	return NULL;
    }

    bi = *lpbi;

    /* Clean up. */
    GlobalUnlock(hDIB);
    SelectPalette(hDC, hPal, TRUE);
    RealizePalette(hDC);
    ReleaseDC(NULL, hDC);

    /* Return handle to the DIB. */
    return hDIB;
}

/*
 *----------------------------------------------------------------------
 *
 * CopyScreenToDIB--
 *
 *	Copies screen to DIB.
 *
 * Results:
 *	Screen copied.
 *
 *----------------------------------------------------------------------
 */

static HANDLE CopyScreenToDIB(
    LPRECT lpRect)
{
    HBITMAP     hBitmap;
    HPALETTE    hPalette;
    HANDLE      hDIB;

    /*
     * Get the device-dependent bitmap in lpRect by calling CopyScreenToBitmap
     * and passing it the rectangle to grab.
     */

    hBitmap = CopyScreenToBitmap(lpRect);

    /* Check for a valid bitmap handle. */

    if (!hBitmap) {
	return NULL;
    }

    /* Get the current palette. */

    hPalette = GetSystemPalette();

    /* convert the bitmap to a DIB. */

    hDIB = BitmapToDIB(hBitmap, hPalette);

    /* Clean up. */

    DeleteObject(hPalette);
    DeleteObject(hBitmap);

    /* Return handle to the packed-DIB. */
    return hDIB;
}

/*
 *----------------------------------------------------------------------
 *
 * GetSystemPalette--
 *
 *	Obtains the system palette.
 *
 * Results:
 *	Returns palette.
 *
 *----------------------------------------------------------------------
 */

static HPALETTE GetSystemPalette(void)
{
    HDC hDC;                /* Handle to a DC. */
    static HPALETTE hPal = NULL;   /* Handle to a palette. */
    HANDLE hLogPal;         /* Handle to a logical palette. */
    LPLOGPALETTE lpLogPal;  /* Pointer to a logical palette. */
    int nColors;            /* Number of colors. */

    /* Find out how many palette entries we want.. */

    hDC = GetDC(NULL);
    if (!hDC) {
	return NULL;
    }

    nColors = PalEntriesOnDevice(hDC);   /* Number of palette entries. */

    /* Allocate room for the palette and lock it.. */

    hLogPal = GlobalAlloc(GHND, sizeof(LOGPALETTE) + nColors *
	    sizeof(PALETTEENTRY));
    if (!hLogPal) {
	/* If we didn't get a logical palette, return NULL. */

	return NULL;
    }

    /* get a pointer to the logical palette. */

    lpLogPal = (LPLOGPALETTE)GlobalLock(hLogPal);

    /* Set some important fields. */

    lpLogPal->palVersion = 0x300;
    lpLogPal->palNumEntries = nColors;

    /* Copy the current system palette into our logical palette. */

    GetSystemPaletteEntries(hDC, 0, nColors,
	    (LPPALETTEENTRY) lpLogPal->palPalEntry);

    /*
     * Go ahead and create the palette.  Once it's created, we no longer need
     * the LOGPALETTE, so free it.
     */

    hPal = CreatePalette(lpLogPal);

    /* Clean up. */

    GlobalUnlock(hLogPal);
    GlobalFree(hLogPal);
    ReleaseDC(NULL, hDC);

    return hPal;
}

/*
 *----------------------------------------------------------------------
 *
 * PalEntriesOnDevice--
 *
 *	Returns the palettes on the device.
 *
 * Results:
 *	Returns palettes.
 *
 *----------------------------------------------------------------------
 */

static int PalEntriesOnDevice(
    HDC hDC)
{
    return (1 << (GetDeviceCaps(hDC, BITSPIXEL) * GetDeviceCaps(hDC, PLANES)));
}

/*
 * --------------------------------------------------------------------------
 *
 * Winprint_Init--
 *
 *	Initializes printing module on Windows.
 *
 * Results:
 *	Module initialized.
 *
 * -------------------------------------------------------------------------
 */

int Winprint_Init(
    Tcl_Interp * interp)
{
    size_t i;
    Tcl_Namespace *namespacePtr;
    static const char *gdiName = "::tk::print::_gdi";
    static const size_t numCommands =
	    sizeof(gdi_commands) / sizeof(struct gdi_command);

    /*
     * Set up the low-level [_gdi] command.
     */

    namespacePtr = Tcl_CreateNamespace(interp, gdiName,
	    NULL, (Tcl_NamespaceDeleteProc *) NULL);
    for (i=0; i<numCommands; i++) {
	char buffer[100];

	snprintf(buffer, sizeof(buffer), "%s::%s", gdiName, gdi_commands[i].command_string);
	Tcl_CreateObjCommand(interp, buffer, gdi_commands[i].command,
		NULL, (Tcl_CmdDeleteProc *) 0);
	Tcl_Export(interp, namespacePtr, gdi_commands[i].command_string, 0);
    }
    Tcl_CreateEnsemble(interp, gdiName, namespacePtr, 0);

    /*
     * The other printing-related commands.
     */

    Tcl_CreateObjCommand(interp, "::tk::print::_selectprinter",
	    PrintSelectPrinter, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::print::_openprinter",
	    PrintOpenPrinter, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::print::_closeprinter",
	    PrintClosePrinter, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::print::_opendoc",
	    PrintOpenDoc, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::print::_closedoc",
	    PrintCloseDoc, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::print::_openpage",
	    PrintOpenPage, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::print::_closepage",
	    PrintClosePage, NULL, NULL);
    return TCL_OK;
}

/* Print API functions. */

/*----------------------------------------------------------------------
 *
 * PrintSelectPrinter--
 *
 *	Main dialog for selecting printer and initializing data for print job.
 *
 * Results:
 *	Printer selected.
 *
 *----------------------------------------------------------------------
 */

static int PrintSelectPrinter(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    TCL_UNUSED(int),
    TCL_UNUSED(Tcl_Obj* const*))
{
    LPCWSTR printerName = NULL;
    PDEVMODEW returnedDevmode = NULL;
    PDEVMODEW localDevmode = NULL;

    copies = 0;
    paper_width = 0;
    paper_height = 0;
    dpi_x = 0;
    dpi_y = 0;

    /* Set up print dialog and initalize property structure. */

    memset(&pd, 0, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = GetDesktopWindow();
    pd.Flags = PD_HIDEPRINTTOFILE | PD_DISABLEPRINTTOFILE | PD_NOSELECTION;

    if (PrintDlgW(&pd) == TRUE) {

	/*Get document info.*/
	memset(&di, 0, sizeof(di));
	di.cbSize = sizeof(di);
	di.lpszDocName = L"Tk Print Output";

	/* Copy print attributes to local structure. */
	returnedDevmode = (PDEVMODEW) GlobalLock(pd.hDevMode);
	devnames = (LPDEVNAMES) GlobalLock(pd.hDevNames);
	printerName = (LPCWSTR) devnames + devnames->wDeviceOffset;
	localDevmode = (LPDEVMODEW) HeapAlloc(GetProcessHeap(),
		HEAP_ZERO_MEMORY | HEAP_GENERATE_EXCEPTIONS,
		returnedDevmode->dmSize);

	if (localDevmode != NULL) {
	    memcpy((LPVOID)localDevmode, (LPVOID)returnedDevmode,
		    returnedDevmode->dmSize);

	    /* Get values from user-set and built-in properties. */
	    localPrinterName = localDevmode->dmDeviceName;
	    dpi_y = localDevmode->dmYResolution;
	    dpi_x = localDevmode->dmPrintQuality;
	    /* Convert height and width to logical points. */
	    paper_height = (int) localDevmode->dmPaperLength / 0.254;
	    paper_width = (int) localDevmode->dmPaperWidth / 0.254;
	    copies = pd.nCopies;
	    /* Set device context here for all GDI printing operations. */
	    printDC = CreateDCW(L"WINSPOOL", printerName, NULL, localDevmode);
	} else {
	    localDevmode = NULL;
	}
    }

    if (pd.hDevMode != NULL) {
	GlobalFree(pd.hDevMode);
    }

    /*
     * Store print properties and link variables so they can be accessed from
     * script level.
     */
    if (localPrinterName != NULL) {
	char* varlink1 = (char*)ckalloc(100 * sizeof(char));
	char** varlink2 = (char**)ckalloc(sizeof(char*));
	*varlink2 = varlink1;
	WideCharToMultiByte(CP_UTF8, 0, localPrinterName, -1, varlink1, 0, NULL, NULL);

	Tcl_LinkVar(interp, "::tk::print::printer_name", varlink2,
	    TCL_LINK_STRING | TCL_LINK_READ_ONLY);
	Tcl_LinkVar(interp, "::tk::print::copies", &copies,
	    TCL_LINK_INT | TCL_LINK_READ_ONLY);
	Tcl_LinkVar(interp, "::tk::print::dpi_x", &dpi_x,
	    TCL_LINK_INT | TCL_LINK_READ_ONLY);
	Tcl_LinkVar(interp, "::tk::print::dpi_y", &dpi_y,
	    TCL_LINK_INT | TCL_LINK_READ_ONLY);
	Tcl_LinkVar(interp, "::tk::print::paper_width", &paper_width,
	    TCL_LINK_INT | TCL_LINK_READ_ONLY);
	Tcl_LinkVar(interp, "::tk::print::paper_height", &paper_height,
	    TCL_LINK_INT | TCL_LINK_READ_ONLY);
    }

    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintOpenPrinter--
 *
 *	Open the given printer.
 *
 * Results:
 *	Opens the selected printer.
 *
 * -------------------------------------------------------------------------
 */

int PrintOpenPrinter(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    Tcl_DString ds;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "printer");
	return TCL_ERROR;
    }

    /*Start an individual page.*/
    if (StartPage(printDC) <= 0) {
	return TCL_ERROR;
    }

    const char *printer = Tcl_GetString(objv[1]);

    if (printDC == NULL) {
	Tcl_AppendResult(interp, "unable to establish device context", (char *)NULL);
	return TCL_ERROR;
    }

    Tcl_DStringInit(&ds);
    if ((OpenPrinterW(Tcl_UtfToWCharDString(printer, -1, &ds),
	    (LPHANDLE)&printDC, NULL)) == FALSE) {
	Tcl_AppendResult(interp, "unable to open printer", (char *)NULL);
	Tcl_DStringFree(&ds);
	return TCL_ERROR;
    }

    Tcl_DStringFree(&ds);
    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintClosePrinter--
 *
 *	Closes the given printer.
 *
 * Results:
 *	Printer closed.
 *
 * -------------------------------------------------------------------------
 */

int PrintClosePrinter(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    TCL_UNUSED(int),
    TCL_UNUSED(Tcl_Obj *const *))
{
    if (printDC == NULL) {
	Tcl_AppendResult(interp, "unable to establish device context", (char *)NULL);
	return TCL_ERROR;
    }

    ClosePrinter(printDC);
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

int PrintOpenDoc(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    TCL_UNUSED(int),
    TCL_UNUSED(Tcl_Obj *const *))
{
    int output = 0;

    if (printDC == NULL) {
	Tcl_AppendResult(interp, "unable to establish device context", (char *)NULL);
	return TCL_ERROR;
    }

    /*
     * Start printing.
     */
    output = StartDocW(printDC, &di);
    if (output <= 0) {
	Tcl_AppendResult(interp, "unable to start document", (char *)NULL);
	return TCL_ERROR;
    }

    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintCloseDoc--
 *
 *	Closes the document for printing.
 *
 * Results:
 *	Closes the print document.
 *
 * -------------------------------------------------------------------------
 */

int PrintCloseDoc(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    TCL_UNUSED(int),
    TCL_UNUSED(Tcl_Obj *const *))
{
    if (printDC == NULL) {
	Tcl_AppendResult(interp, "unable to establish device context", (char *)NULL);
	return TCL_ERROR;
    }

    if (EndDoc(printDC) <= 0) {
	Tcl_AppendResult(interp, "unable to establish close document", (char *)NULL);
	return TCL_ERROR;
    }
    DeleteDC(printDC);
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

int PrintOpenPage(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    TCL_UNUSED(int),
    TCL_UNUSED(Tcl_Obj *const *))
{
    if (printDC == NULL) {
	Tcl_AppendResult(interp, "unable to establish device context", (char *)NULL);
	return TCL_ERROR;
    }

    /*Start an individual page.*/
    if (StartPage(printDC) <= 0) {
	Tcl_AppendResult(interp, "unable to start page", (char *)NULL);
	return TCL_ERROR;
    }

    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintClosePage--
 *
 *	Closes the printed page.
 *
 * Results:
 *	Closes the page.
 *
 * -------------------------------------------------------------------------
 */

int PrintClosePage(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    TCL_UNUSED(int),
    TCL_UNUSED(Tcl_Obj *const *))
{
    if (printDC == NULL) {
	Tcl_AppendResult(interp, "unable to establish device context", (char *)NULL);
	return TCL_ERROR;
    }

    if (EndPage(printDC) <= 0) {
	Tcl_AppendResult(interp, "unable to close page", (char *)NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
