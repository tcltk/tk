/*
 * tkWinGDI.c --
 *
 *      This module implements access to the Win32 GDI API.
 *
 * Copyright © 1991-2018 Microsoft Corp.
 * Copyright © 2009, Michael I. Schwartz
 * Copyright © 1998-2019 Harald Oehlmann, Elmicron GmbH
 * Copyright © 2021 Kevin Walzer
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

#define DEG2RAD(x) (0.017453292519943295 * (x))
#define ROUND32(x) ((LONG)floor((x) + 0.5))

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
static Tcl_ObjCmdProc2 GdiArc;
static Tcl_ObjCmdProc2 GdiBitmap;
static Tcl_ObjCmdProc2 GdiCharWidths;
static Tcl_ObjCmdProc2 GdiImage;
static Tcl_ObjCmdProc2 GdiPhoto;
static Tcl_ObjCmdProc2 GdiLine;
static Tcl_ObjCmdProc2 GdiOval;
static Tcl_ObjCmdProc2 GdiPolygon;
static Tcl_ObjCmdProc2 GdiRectangle;
static Tcl_ObjCmdProc2 GdiText;
static Tcl_ObjCmdProc2 GdiTextPlain;
static Tcl_ObjCmdProc2 GdiMap;
static Tcl_ObjCmdProc2 GdiCopyBits;

/* Local copies of similar routines elsewhere in Tcl/Tk. */
static int GdiGetColor(Tcl_Obj *nameObj, COLORREF *color);

/*
 * Helper functions.
 */
static int		GdiMakeLogFont(Tcl_Interp *interp, Tcl_Obj *specPtr,
			    LOGFONTW *lf, HDC hDC);
static int		GdiMakePen(Tcl_Interp *interp, double dwidth,
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
static int		GdiParseFontWords(Tcl_Interp *interp, LOGFONTW *lf,
			    Tcl_Obj *const *objv, Tcl_Size argc);
static Tcl_ObjCmdProc2 PrintSelectPrinter;
static Tcl_ObjCmdProc2 PrintOpenPrinter;
static Tcl_ObjCmdProc2 PrintClosePrinter;
static Tcl_ObjCmdProc2 PrintOpenDoc;
static Tcl_ObjCmdProc2 PrintCloseDoc;
static Tcl_ObjCmdProc2 PrintOpenPage;
static Tcl_ObjCmdProc2 PrintClosePage;

/*
 * Global state.
 */
/*
static DOCINFOW di;
static HDC printDC;
static Tcl_DString jobNameW;
*/

typedef struct WinprintData {
    DOCINFOW di;
    HDC printDC;
    Tcl_DString jobNameW;
} WinprintData;

/*
 * To make the "subcommands" follow a standard convention, add them to this
 * array. The first element is the subcommand name, and the second a standard
 * Tcl command handler.
 */

static const struct gdi_command {
    const char *command_string;
    Tcl_ObjCmdProc2 *command;
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
    { "textplain",  GdiTextPlain },
    { "copybits",   GdiCopyBits },
};

/*
 * Helper functions for Tcl_ParseArgsObjv.
 * These are used in parsing "-option value" pairs in the different GDI
 * subcommands.
 */

/*
 * This structure represents a canvas color, which can be any color
 * accepted by Tk_GetColor, or the empty string to mean
 * "don't draw the element"
 */
typedef struct CanvasColor {
    COLORREF color; /* Color */
    char isempty;   /* 1 if color is {}, 0 otherwise */
} CanvasColor;

static Tcl_Size ParseColor (
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    CanvasColor *ccolor = (CanvasColor *)dstPtr;
    const char *colorname;

    if (objc == 0) {
	Tcl_AppendResult(interp, "option \"", Tcl_GetString(objv[-1]),
	    "\" needs an additional argument", (char *)NULL);
	return -1;
    }

    if (GdiGetColor(objv[0], &(ccolor->color))) {
	ccolor->isempty = 0;
	return 1;
    }

    colorname = Tcl_GetString(objv[0]);
    if (colorname[0] == '\0') {
	ccolor->color = 0;
	ccolor->isempty = 1;
	return 1;
    }

    Tcl_AppendResult(interp, "unknown color name \"", colorname, "\"", (char *)NULL);
    return -1;
}

static Tcl_Size ParseDash (
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    const char *dashspec;
    Tk_Dash dash;
    int staticsize = (int) sizeof(char *);

    if (objc == 0) {
	Tcl_AppendResult(interp,
	    "option \"-dash\" needs an additional argument", (char *)NULL);
	return -1;
    }

    dashspec = Tcl_GetString(objv[0]);
    /* Tk_GetDash might potentially call Tcl_Free() on dash.pattern.pt if
     * dash.number is not initialized to 0
     */
    dash.number = 0;
    if (Tk_GetDash(interp, dashspec, &dash) != TCL_OK) {
	return -1;
    }

    if (dash.number == 0) {
	/* empty string; do nothing */
	return 1;
    }

    /* free the possibly allocated space */
    if (dash.number > staticsize || dash.number < -staticsize) {
	Tcl_Free(dash.pattern.pt);
    }

    *(const char **)dstPtr = dashspec;
    return 1;
}

static Tcl_Size ParseAnchor (
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    Tk_Anchor anchor;

    if (objc == 0) {
	Tcl_AppendResult(interp,
	    "option \"-anchor\" needs an additional argument", (char *)NULL);
	return -1;
    }

    if (Tk_GetAnchorFromObj(interp, objv[0], &anchor) != TCL_OK) {
	return -1;
    }
    *(Tk_Anchor *)dstPtr = anchor;
    return 1;
}

static Tcl_Size ParseFont (
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    Tcl_Size fcount;
    Tcl_Obj **fobjs;
    const char *fstring;
    int size;

    if (objc == 0) {
	Tcl_AppendResult(interp,
	    "option \"-font\" needs an additional argument", (char *)NULL);
	return -1;
    }

    fstring = Tcl_GetString(objv[0]);
    /* Accept font description in the same format as provided by
     * Tk_FontGetDescription(), a list of
     * family size ?normal|bold? ?roman|italic? ?underline? ?overstrike?
     */
    if (Tcl_ListObjGetElements(NULL, objv[0], &fcount, &fobjs) != TCL_OK
	    || (fcount < 2 || fcount > 6)) {
	Tcl_AppendResult(interp, "bad font description \"", fstring,
	    "\"", (char *)NULL);
	return -1;
    }

    /* canvas font size should be in pixels (negative)
     * text font size should be in points (positive)
     */
    if (Tcl_GetIntFromObj(interp, fobjs[1], &size) != TCL_OK) {
	const char *value = Tcl_GetString(fobjs[1]);
	Tcl_AppendResult(interp, "bad size \"", value,
	    "\"; should be an integer", (char *)NULL);
	return -1;
    }

    *(Tcl_Obj **)dstPtr = objv[0];
    return 1;
}

static Tcl_Size ParseJoinStyle (
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    int join;

    if (objc == 0) {
	Tcl_AppendResult(interp,
	    "option \"-joinstyle\" needs an additional argument", (char *)NULL);
	return -1;
    }

    if (Tk_GetJoinStyle(interp, Tcl_GetString(objv[0]), &join) != TCL_OK) {
	return -1;
    }
    switch (join) {
	case JoinBevel:
	    *(int *)dstPtr = PS_JOIN_BEVEL;
	    break;
	case JoinMiter:
	    *(int *)dstPtr = PS_JOIN_MITER;
	    break;
	case JoinRound:
	    *(int *)dstPtr = PS_JOIN_ROUND;
	    break;
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * Arc specific: parse "-style"
 *
 *----------------------------------------------------------------------
 */
static Tcl_Size ParseStyle (
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    DrawFunc *func = (DrawFunc *)dstPtr;
    Tcl_Size index;

    static struct FuncMap {
	const char *name;
	DrawFunc gdifunc;
    } funcmap[] = {
	{"arc",      Arc  },
	{"chord",    Chord},
	{"pieslice", Pie  },
	{NULL, NULL}
    };

    if (objc == 0) {
	Tcl_AppendResult(interp,
	    "option \"-style\" needs an additional argument", (char *)NULL);
	return -1;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[0], funcmap,
	sizeof(struct FuncMap), "-style option", 0, &index) != TCL_OK) {
	return -1;
    }
    *func = funcmap[index].gdifunc;
    return 1;
}

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
    void *clientData,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }

    HDC hDC = dataPtr->printDC;
    double x1, y1, x2, y2;
    int xr0, yr0, xr1, yr1;

     /* canvas arc item defaults */
    double extent         = 90.0;
    double start          = 0.0;
    double width          = 1.0;
    CanvasColor outline   = {0, 0};
    CanvasColor fill      = {0, 1};
    const char *dash      = NULL;
    const char *stipple   = NULL;
    const char *olstipple = NULL;
    DrawFunc drawfunc     = Pie;

    LOGBRUSH lbrush;
    HGDIOBJ oldbrush = NULL, oldpen = NULL;

    /* Verrrrrry simple for now.... */
    if (objc < 6) {
	Tcl_WrongNumArgs(interp, 1, objv, "hdc x1 y1 x2 y2 ?option value ...?");
	return TCL_ERROR;
    }

    if ((Tcl_GetDoubleFromObj(interp, objv[2], &x1) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[3], &y1) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[4], &x2) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[5], &y2) != TCL_OK)) {
	return TCL_ERROR;
    }
    objv += 6;
    objc -= 6;

    if (objc > 0) {
	Tcl_Size argc = objc + 1;
	objv--;

	const Tcl_ArgvInfo arcArgvInfo[] = {
	    {TCL_ARGV_GENFUNC, "-dash",    ParseDash,  &dash,      NULL, NULL},
	    {TCL_ARGV_FLOAT,   "-extent",  NULL,       &extent,    NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-fill",    ParseColor, &fill,      NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-outline", ParseColor, &outline,   NULL, NULL},
	    {TCL_ARGV_STRING,  "-outlinestipple", NULL,&olstipple, NULL, NULL},
	    {TCL_ARGV_FLOAT,   "-start",   NULL,       &start,     NULL, NULL},
	    {TCL_ARGV_STRING,  "-stipple", NULL,       &stipple,   NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-style",   ParseStyle, &drawfunc,  NULL, NULL},
	    {TCL_ARGV_FLOAT,   "-width",   NULL,       &width,     NULL, NULL},
	    TCL_ARGV_TABLE_END
	};

	if (Tcl_ParseArgsObjv(interp, arcArgvInfo, &argc, objv, NULL) != TCL_OK) {
	    return TCL_ERROR;
	}
    }

    /* a simple check: if both -outline and -fill are empty, return */
    if (outline.isempty && fill.isempty) {
	return TCL_OK;
    }

    xr0 = xr1 = (x1 + x2) / 2;
    yr0 = yr1 = (y1 + y2) / 2;

    /*
     * The angle used by the arc must be "warped" by the eccentricity of the
     * ellipse.  Thanks to Nigel Dodd <nigel.dodd@avellino.com> for bringing a
     * nice example.
     */

    xr0 += (int)(100.0 * (x2 - x1) * cos(DEG2RAD(start)));
    yr0 -= (int)(100.0 * (y2 - y1) * sin(DEG2RAD(start)));
    xr1 += (int)(100.0 * (x2 - x1) * cos(DEG2RAD(start+extent)));
    yr1 -= (int)(100.0 * (y2 - y1) * sin(DEG2RAD(start+extent)));

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

    if (fill.isempty) {
	oldbrush = SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));
    } else {
	GdiMakeBrush(fill.color, 0, &lbrush, hDC, (HBRUSH *)&oldbrush);
    }

    if (outline.isempty) {
	oldpen = SelectObject(hDC, GetStockObject(NULL_PEN));
    } else {
	GdiMakePen(interp, width, (dash != NULL), dash,
	    PS_ENDCAP_FLAT, PS_JOIN_BEVEL, 0, 0,
	    outline.color, hDC, &oldpen);
    }

    (*drawfunc)(hDC, x1, y1, x2, y2, xr0, yr0, xr1, yr1);

    GdiFreePen(interp, hDC, oldpen);
    GdiFreeBrush(interp, hDC, oldbrush);

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
    TCL_UNUSED(Tcl_Size),
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
    TCL_UNUSED(Tcl_Size),
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
    void *clientData,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }
    HDC hDC = dataPtr->printDC;
    int hDC_x = 0, hDC_y = 0, hDC_w = 0, hDC_h = 0;
    int nx, ny, sll;
    const char *photoname = NULL;	/* For some reason Tk_FindPhoto takes a char *. */
    Tk_PhotoHandle photo_handle;
    Tk_PhotoImageBlock img_block;
    BITMAPINFO bitmapinfo;	/* Since we don't need the bmiColors table,
				 * there is no need for dynamic allocation. */
    int oldmode;		/* For saving the old stretch mode. */
    POINT pt;			/* For saving the brush org. */
    char *pbuf = NULL;
    int i, k;
    int retval = TCL_OK;
    double x, y;
    Tk_Anchor anchor = TK_ANCHOR_CENTER;

    /*
     * Parse the arguments.
     */

    if (objc < 4) {
	Tcl_WrongNumArgs(interp, 1, objv, "hdc x y ?option value ...?");
	return TCL_ERROR;
    }

    /*
     * Next, check to see if 'hDC' can support BitBlt.
     * If not, raise an error.
     */

    if ((GetDeviceCaps(hDC, RASTERCAPS) & RC_STRETCHDIB) == 0) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"::tk::print::_gdi photo not supported on device context (0x%s)",
		Tcl_GetString(objv[1])));
	return TCL_ERROR;
    }

    if ((Tcl_GetDoubleFromObj(interp, objv[2], &x) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[3], &y) != TCL_OK)) {
	return TCL_ERROR;
    }
    hDC_x = ROUND32(x);
    hDC_y = ROUND32(y);
    objc -= 4;
    objv += 4;

    if (objc > 0) {
	Tcl_Size argc = objc + 1;
	objv--;

	const Tcl_ArgvInfo photoArgvInfo[] = {
	    {TCL_ARGV_GENFUNC, "-anchor", ParseAnchor, &anchor,    NULL, NULL},
	    {TCL_ARGV_STRING,  "-photo",  NULL,        &photoname, NULL, NULL},
	    TCL_ARGV_TABLE_END
	};

	if (Tcl_ParseArgsObjv(interp, photoArgvInfo, &argc, objv, NULL)
		!= TCL_OK) {
	    return TCL_ERROR;
	}
    }

    if (! photoname) { /* No photo provided. */
	Tcl_AppendResult(interp, "no photo name provided", (char *)NULL);
	return TCL_ERROR;
    }

    photo_handle = Tk_FindPhoto(interp, photoname);
    if (! photo_handle) {
	Tcl_AppendResult(interp, "photo name \"", photoname,
	    "\" can't be located", (char *)NULL);
	return TCL_ERROR;
    }
    Tk_PhotoGetImage(photo_handle, &img_block);

    hDC_w = nx = img_block.width;
    hDC_h = ny = img_block.height;
    sll = ((3*nx + 3) / 4)*4; /* Must be multiple of 4. */

    /*
     * Buffer is potentially large enough that failure to allocate might be
     * recoverable.
     */

    pbuf = (char *)Tcl_AttemptAlloc(sll * ny * sizeof(char));
    if (! pbuf) { /* Memory allocation failure. */
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

    oldmode = SetStretchBltMode(hDC, HALFTONE);
    /*
     * According to the Win32 Programmer's Manual, we have to set the brush
     * org, now.
     */
    SetBrushOrgEx(hDC, 0, 0, &pt);

    /* adjust coords based on the anchor point */
    switch (anchor) {
	case TK_ANCHOR_N:
	    hDC_x -= hDC_w/2;
	    break;
	case TK_ANCHOR_NE:
	    hDC_x -= hDC_w;
	    break;
	case TK_ANCHOR_W:
	    hDC_y -= hDC_h/2;
	    break;
	case TK_ANCHOR_CENTER:
	    hDC_x -= hDC_w/2;
	    hDC_y -= hDC_h/2;
	    break;
	case TK_ANCHOR_E:
	    hDC_x -= hDC_w;
	    hDC_y -= hDC_h/2;
	    break;
	case TK_ANCHOR_SW:
	    hDC_y -= hDC_h;
	    break;
	case TK_ANCHOR_S:
	    hDC_x -= hDC_w/2;
	    hDC_y -= hDC_h;
	    break;
	case TK_ANCHOR_SE:
	    hDC_x -= hDC_w;
	    hDC_y -= hDC_h;
	    break;
	default:
	    /* nothing */
	    break;
    }

    if (StretchDIBits(hDC, hDC_x, hDC_y, hDC_w, hDC_h, 0, 0, nx, ny,
	    pbuf, &bitmapinfo, DIB_RGB_COLORS, SRCCOPY) == (int)GDI_ERROR) {
	int errcode = GetLastError();

	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"::tk::print::_gdi photo internal failure: "
		"StretchDIBits error code %d", errcode));
	retval = TCL_ERROR;
    }

    /* Clean up the hDC. */
    if (oldmode != 0) {
	SetStretchBltMode(hDC, oldmode);
	SetBrushOrgEx(hDC, pt.x, pt.y, &pt);
    }

    Tcl_Free(pbuf);

    if (retval == TCL_OK) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"%d %d %d %d", hDC_x, hDC_y, hDC_w, hDC_h));
    }

    return retval;
}

/*
 *----------------------------------------------------------------------
 *
 * Smoothize --
 *
 *	Interface to Tk's line smoother, used for lines and pollies.
 *	Provided by Jasper Taylor <jasper.taylor@ed.ac.uk>.
 *
 * Results:
 *	Smooths lines.
 *
 *----------------------------------------------------------------------
 */
#define SMOOTH_NONE   0
#define SMOOTH_BEZIER 1
#define SMOOTH_RAW    2
static int Smoothize(
    POINT* polypoints,
    int npoly,
    int nStep,
    int smooth, /* either SMOOTH_BEZIER or SMOOTH_RAW */
    POINT** bpointptr)
{
    /* First, translate my points into a list of doubles. */
    double *inPointList, *outPointList;
    int n;
    int nbpoints = 0;
    POINT* bpoints;

    inPointList = (double *)Tcl_AttemptAlloc(2 * sizeof(double) * npoly);
    if (inPointList == 0) {
	/* TODO: unreachable */
	return nbpoints; /* 0. */
    }

    for (n=0; n<npoly; n++) {
	inPointList[2*n] = polypoints[n].x;
	inPointList[2*n + 1] = polypoints[n].y;
    }

    nbpoints = 1 + npoly * nStep; /* this is the upper limit. */
    outPointList = (double *)Tcl_AttemptAlloc(2 * sizeof(double) * nbpoints);
    if (outPointList == 0) {
	/* TODO: unreachable */
	Tcl_Free(inPointList);
	return 0;
    }

    if (smooth == SMOOTH_BEZIER) {
	nbpoints = TkMakeBezierCurve(NULL, inPointList, npoly, nStep,
		NULL, outPointList);
    } else {   /* SMOOTH_RAW */
	nbpoints = TkMakeRawCurve(NULL, inPointList, npoly, nStep,
		NULL, outPointList);
    }

    Tcl_Free(inPointList);
    bpoints = (POINT *)Tcl_AttemptAlloc(sizeof(POINT)*nbpoints);
    if (bpoints == 0) {
	/* TODO: unreachable */
	Tcl_Free(outPointList);
	return 0;
    }

    for (n=0; n<nbpoints; n++) {
	bpoints[n].x = (long)outPointList[2*n];
	bpoints[n].y = (long)outPointList[2*n + 1];
    }
    Tcl_Free(outPointList);
    *bpointptr = bpoints;
    return nbpoints;
}

/*
 *----------------------------------------------------------------------
 *
 * Line specific: parse "-arrow"
 *
 *----------------------------------------------------------------------
 */
#define ARROW_NONE  0
#define ARROW_FIRST 1
#define ARROW_LAST  2
static Tcl_Size ParseArrow (
    TCL_UNUSED(void *), /* clientData */
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    int index;

    static struct ArrowMap {
	const char *name;
	int arrow;
    } arrowmap[] = {
	{"none",  ARROW_NONE},
	{"first", ARROW_FIRST},
	{"last",  ARROW_LAST},
	{"both",  (ARROW_FIRST | ARROW_LAST)},
	{NULL, 0},
    };

    if (objc == 0) {
	Tcl_AppendResult(interp,
	    "option \"-arrow\" needs an additional argument", (char *)NULL);
	return -1;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[0], arrowmap,
	sizeof(struct ArrowMap), "-arrow option", 0, &index) != TCL_OK) {
	return -1;
    }

    *(int *)dstPtr = arrowmap[index].arrow;
    return 1;
}
/*
 *----------------------------------------------------------------------
 *
 * Line specific: parse "-arrowshape"
 *
 *----------------------------------------------------------------------
 */
static Tcl_Size ParseArrShp(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    int *arrowShape = (int *)dstPtr;
    Tcl_Obj **shpObjs;
    double a0, a1, a2;
    Tcl_Size count;

    if (objc == 0) {
	Tcl_AppendResult(interp,
	    "option \"-arrowshape\" requires an additional argument", (char *)NULL);
	return -1;
    }
    if (Tcl_ListObjGetElements(interp, objv[0], &count, &shpObjs) != TCL_OK) {
	return -1;
    }
    if (count != 3 ||
	    Tcl_GetDoubleFromObj(NULL, shpObjs[0], &a0) != TCL_OK ||
	    Tcl_GetDoubleFromObj(NULL, shpObjs[1], &a1) != TCL_OK ||
	    Tcl_GetDoubleFromObj(NULL, shpObjs[2], &a2) != TCL_OK) {
	Tcl_AppendResult(interp, "arrow shape should be a list ",
	    "with three numbers", (char *)NULL);
	return -1;
    }
    arrowShape[0] = ROUND32(a0);
    arrowShape[1] = ROUND32(a1);
    arrowShape[2] = ROUND32(a2);
    return 1;
}
/*
 *----------------------------------------------------------------------
 *
 * Line specific: parse "-capstyle"
 *
 *----------------------------------------------------------------------
 */
static Tcl_Size ParseCapStyle (
    TCL_UNUSED(void *), /* clientData */
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    int cap;

    if (objc == 0) {
	Tcl_AppendResult(interp,
	    "option \"-capstyle\" needs an additional argument", (char *)NULL);
	return -1;
    }

    if (Tk_GetCapStyle(interp, Tcl_GetString(objv[0]), &cap) != TCL_OK) {
	return -1;
    }
    switch (cap) {
	case CapButt:
	    *(int *)dstPtr = PS_ENDCAP_FLAT;
	    break;
	case CapProjecting:
	    *(int *)dstPtr = PS_ENDCAP_SQUARE;
	    break;
	case CapRound:
	    *(int *)dstPtr = PS_ENDCAP_ROUND;
	    break;
    }
    return 1;
}
/*
 *----------------------------------------------------------------------
 *
 * Line specific: parse "-smooth"
 *
 *----------------------------------------------------------------------
 */
static Tcl_Size ParseSmooth(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    bool boolValue;
    Tcl_Size index;

    static const struct SmoothMethod {
	const char *name;
	int method;
    } smoothmethods[] = {
	{"bezier", SMOOTH_BEZIER},
	{"raw",    SMOOTH_RAW},
	{NULL,     0}
    };

    if (objc == 0) {
	Tcl_AppendResult(interp,
	    "option \"-smooth\" requires an additional argument", (char *)NULL);
	return -1;
    }
    /* Argument is a boolean value, "bezier" or "raw". */
    if (Tcl_GetBooleanFromObj(NULL, objv[0], &boolValue) == TCL_OK) {
	*(int *)dstPtr = boolValue;
	return 1;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[0], smoothmethods,
	    sizeof(struct SmoothMethod), "smooth method", 0, &index) != TCL_OK) {
	Tcl_AppendResult(interp, " or a boolean value", (char *)NULL);
	return -1;
    }
    *(int *)dstPtr = smoothmethods[index].method;
    return 1;
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
    void *clientData,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }
    HDC hDC = dataPtr->printDC;
    double p1x, p1y, p2x, p2y;
    POINT *polypoints;
    int npoly;

     /* canvas line item defaults */
    double width      = 1.0;
    CanvasColor fill  = {0, 0};
    int smooth        = SMOOTH_NONE;
    int arrow         = ARROW_NONE; /* 0=none; 1=end; 2=start; 3=both. */
    int arrowshape[3] = {8, 10, 3};
    int nStep         = 12;
    const char *dash  = NULL;
    int capstyle      = PS_ENDCAP_FLAT;
    int joinstyle     = PS_JOIN_ROUND;
    /* ignored for now */
    const char *stipple = NULL;
    const char *dashoffset = NULL;

    LOGBRUSH lbrush;
    HGDIOBJ oldpen = NULL, oldbrush = NULL;
    double shapeA = 0, shapeB = 0, shapeC = 0, fracHeight = 0, backup = 0;

    /* Verrrrrry simple for now.... */
    if (objc < 6) {
	Tcl_WrongNumArgs(interp, 1, objv, "hdc x1 y1... xn yn ?option value ...?");
	return TCL_ERROR;
    }

    if ((Tcl_GetDoubleFromObj(interp, objv[2], &p1x) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[3], &p1y) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[4], &p2x) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[5], &p2y) != TCL_OK)) {
	return TCL_ERROR;
    }
    polypoints = (POINT *)Tcl_AttemptAlloc((objc - 2)/2 * sizeof(POINT));
    if (polypoints == NULL) {
	Tcl_AppendResult(interp, "Out of memory in GdiLine", (char *)NULL);
	return TCL_ERROR;
    }
    polypoints[0].x = ROUND32(p1x);
    polypoints[0].y = ROUND32(p1y);
    polypoints[1].x = ROUND32(p2x);
    polypoints[1].y = ROUND32(p2y);
    objc -= 6;
    objv += 6;
    npoly = 2;

    while (objc >= 2 &&
	    Tcl_GetDoubleFromObj(NULL, objv[0], &p1x) == TCL_OK &&
	    Tcl_GetDoubleFromObj(NULL, objv[1], &p1y) == TCL_OK) {
	polypoints[npoly].x = ROUND32(p1x);
	polypoints[npoly].y = ROUND32(p1y);
	npoly++;
	objc -= 2;
	objv += 2;
    }

    if (objc > 0) {
	Tcl_Size argc = objc + 1;
	objv--;

	const Tcl_ArgvInfo lineArgvInfo[] = {
	    {TCL_ARGV_GENFUNC, "-arrow",      ParseArrow,    &arrow,     NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-arrowshape", ParseArrShp,   arrowshape, NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-capstyle",   ParseCapStyle, &capstyle,  NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-dash",       ParseDash,     &dash,      NULL, NULL},
	    {TCL_ARGV_STRING,  "-dashoffset", NULL,          &dashoffset,NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-fill",       ParseColor,    &fill,      NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-joinstyle",  ParseJoinStyle,&joinstyle, NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-smooth",     ParseSmooth,   &smooth,    NULL, NULL},
	    {TCL_ARGV_INT,     "-splinesteps",NULL,          &nStep,     NULL, NULL},
	    {TCL_ARGV_STRING,  "-stipple",    NULL,          &stipple,   NULL, NULL},
	    {TCL_ARGV_FLOAT,   "-width",      NULL,          &width,     NULL, NULL},
	    TCL_ARGV_TABLE_END
	};

	if (Tcl_ParseArgsObjv(interp, lineArgvInfo, &argc, objv, NULL )
		!= TCL_OK) {
	    Tcl_Free(polypoints);
	    return TCL_ERROR;
	}
    }

    if (fill.isempty) {
	Tcl_Free(polypoints);
	return TCL_OK;
    }
    if (arrow != ARROW_NONE) {
	/* if -arrow is specified, capstyle is ignored */
	capstyle = PS_ENDCAP_FLAT;
    }

    if (smooth != SMOOTH_NONE) { /* Use Smoothize. */
	int nspoints;
	POINT *spoints;

	nspoints = Smoothize(polypoints, npoly, nStep, smooth, &spoints);
	if (nspoints > 0) {
	    /* replace the old point list with the new one */
	    Tcl_Free(polypoints);
	    polypoints = spoints;
	    npoly = nspoints;
	}
    }

    if (arrow != ARROW_NONE) {
	GdiMakeBrush(fill.color, 0, &lbrush, hDC, (HBRUSH *)&oldbrush);
	GdiMakePen(interp, 1, 0, 0, 0, PS_JOIN_MITER, 0, 0,
	    fill.color, hDC, &oldpen);
	shapeA = arrowshape[0] + 0.001;
	shapeB = arrowshape[1] + 0.001;
	shapeC = arrowshape[2] + width/2.0 + 0.001;
	fracHeight = (width/2.0)/shapeC;
	backup = fracHeight*shapeB + shapeA*(1.0 - fracHeight)/2.0;
    }

    /* draw the arrowheads, if any. */
    if (arrow & ARROW_LAST) {
	/* Arrowhead at end = polypoints[npoly-1].x, polypoints[npoly-1].y. */
	POINT ahead[6];
	double dx, dy, length;
	double sinTheta, cosTheta;
	double vertX, vertY, temp;

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
	vertX = ahead[0].x - shapeA*cosTheta;
	vertY = ahead[0].y - shapeC*sinTheta;
	temp = shapeC*sinTheta;
	ahead[1].x = ROUND32(ahead[0].x - shapeB*cosTheta + temp);
	ahead[4].x = ROUND32(ahead[1].x - 2 * temp);
	temp = shapeC*cosTheta;
	ahead[1].y = ROUND32(ahead[0].y - shapeB*sinTheta - temp);
	ahead[4].y = ROUND32(ahead[1].y + 2 * temp);
	ahead[2].x = ROUND32(ahead[1].x*fracHeight + vertX*(1.0-fracHeight));
	ahead[2].y = ROUND32(ahead[1].y*fracHeight + vertY*(1.0-fracHeight));
	ahead[3].x = ROUND32(ahead[4].x*fracHeight + vertX*(1.0-fracHeight));
	ahead[3].y = ROUND32(ahead[4].y*fracHeight + vertY*(1.0-fracHeight));

	Polygon(hDC, ahead, 6);
	polypoints[npoly-1].x = ROUND32(ahead[0].x - backup*cosTheta);
	polypoints[npoly-1].y = ROUND32(ahead[0].y - backup*sinTheta);
    }

    if (arrow & ARROW_FIRST) {
	/* Arrowhead at beginning = polypoints[0].x, polypoints[0].y. */
	POINT ahead[6];
	double dx, dy, length;
	double sinTheta, cosTheta;
	double vertX, vertY, temp;

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
	vertX = ahead[0].x - shapeA*cosTheta;
	vertY = ahead[0].y - shapeA*sinTheta;
	temp = shapeC*sinTheta;
	ahead[1].x = ROUND32(ahead[0].x - shapeB*cosTheta + temp);
	ahead[4].x = ROUND32(ahead[1].x - 2 * temp);
	temp = shapeC*cosTheta;
	ahead[1].y = ROUND32(ahead[0].y - shapeB*sinTheta - temp);
	ahead[4].y = ROUND32(ahead[1].y + 2 * temp);
	ahead[2].x = ROUND32(ahead[1].x*fracHeight + vertX*(1.0-fracHeight));
	ahead[2].y = ROUND32(ahead[1].y*fracHeight + vertY*(1.0-fracHeight));
	ahead[3].x = ROUND32(ahead[4].x*fracHeight + vertX*(1.0-fracHeight));
	ahead[3].y = ROUND32(ahead[4].y*fracHeight + vertY*(1.0-fracHeight));

	Polygon(hDC, ahead, 6);
	polypoints[0].x = ROUND32(ahead[0].x - backup*cosTheta);
	polypoints[0].y = ROUND32(ahead[0].y - backup*sinTheta);
    }

    /* free arrow's pen and brush (if any) */
    if (arrow != ARROW_NONE) {
	GdiFreePen(interp, hDC, oldpen);
	GdiFreeBrush(interp, hDC, oldbrush);
    }

    /* draw the line */
    GdiMakePen(interp, width, (dash != NULL), dash,
	capstyle, joinstyle, 0, 0, fill.color, hDC, &oldpen);
    Polyline(hDC, polypoints, npoly);
    GdiFreePen(interp, hDC, oldpen);

    Tcl_Free(polypoints);
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
    void *clientData,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }
    HDC hDC = dataPtr->printDC;

     /* canvas oval item defaults */
    double width = 1.0;
    CanvasColor outline = {0, 0};
    CanvasColor fill    = {0, 1};
    const char *dash    = NULL;
    const char *stipple = NULL;

    LOGBRUSH lbrush;
    HGDIOBJ oldpen = NULL, oldbrush = NULL;
    double x1, y1, x2, y2;

    /* Verrrrrry simple for now.... */
    if (objc < 6) {
	Tcl_WrongNumArgs(interp, 1, objv, "hdc x1 y1 x2 y2 ?option value ...?");
	return TCL_ERROR;
    }

    if ((Tcl_GetDoubleFromObj(interp, objv[2], &x1) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[3], &y1) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[4], &x2) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[5], &y2) != TCL_OK)) {
	return TCL_ERROR;
    }
    if (x1 > x2) {
	double x3 = x1;
	x1 = x2;
	x2 = x3;
    }
    if (y1 > y2) {
	double y3 = y1;
	y1 = y2;
	y2 = y3;
    }
    objc -= 6;
    objv += 6;

    if (objc > 0) {
	Tcl_Size argc = objc + 1;
	objv--;

	const Tcl_ArgvInfo ovalArgvInfo[] = {
	    {TCL_ARGV_GENFUNC, "-dash",    ParseDash,  &dash,    NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-fill",    ParseColor, &fill,    NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-outline", ParseColor, &outline, NULL, NULL},
	    {TCL_ARGV_STRING,  "-stipple", NULL,       &stipple, NULL, NULL},
	    {TCL_ARGV_FLOAT,   "-width",   NULL,       &width,   NULL, NULL},
	    TCL_ARGV_TABLE_END
	};

	if (Tcl_ParseArgsObjv(interp, ovalArgvInfo, &argc, objv, NULL )
		!= TCL_OK) {
	    return TCL_ERROR;
	}
    }
    if (outline.isempty && fill.isempty) {
	return TCL_OK;
    }

    if (fill.isempty) {
	oldbrush = SelectObject(hDC, GetStockObject(NULL_BRUSH));
    } else {
	GdiMakeBrush(fill.color, 0, &lbrush, hDC, (HBRUSH *)&oldbrush);
    }

    if (outline.isempty) {
	oldpen = SelectObject(hDC, GetStockObject(NULL_PEN));
    } else {
	GdiMakePen(interp, width, (dash != NULL), dash,
	    0, 0, 0, 0, outline.color, hDC, &oldpen);
    }
    /*
     * Per Win32, Ellipse includes lower and right edges--per Tcl8.3.2 and
     * earlier documentation, canvas oval does not. Thus, add 1 to right
     * and lower bounds to get appropriate behavior.
     */
    Ellipse(hDC, ROUND32(x1), ROUND32(y1), ROUND32(x2+1), ROUND32(y2+1));

    GdiFreePen(interp, hDC, oldpen);
    GdiFreeBrush(interp, hDC, oldbrush);

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
    void *clientData,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }
    HDC hDC = dataPtr->printDC;
    POINT *polypoints;
    int npoly;

     /* canvas polygon item defaults */
    double width        = 1.0;
    CanvasColor outline = {0, 0};
    CanvasColor fill    = {0, 1};
    int joinstyle       = PS_JOIN_ROUND;
    int smooth          = SMOOTH_NONE;
    int nStep           = 12;
    const char *dash    = NULL;
    const char *stipple = NULL;

    LOGBRUSH lbrush;
    HGDIOBJ oldpen = NULL, oldbrush = NULL;
    double p1x, p1y, p2x, p2y;

    /* Verrrrrry simple for now.... */
    if (objc < 6) {
	Tcl_WrongNumArgs(interp, 1, objv, "hdc x1 y1 ... xn yn ?option value ...?");
	return TCL_ERROR;
    }

    if ((Tcl_GetDoubleFromObj(interp, objv[2], &p1x) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[3], &p1y) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[4], &p2x) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[5], &p2y) != TCL_OK)) {
	return TCL_ERROR;
    }
    polypoints = (POINT *)Tcl_AttemptAlloc((objc - 2)/2 * sizeof(POINT));
    if (polypoints == NULL) {
	/* TODO: unreachable */
	Tcl_AppendResult(interp, "Out of memory in GdiPolygon", (char *)NULL);
	return TCL_ERROR;
    }
    polypoints[0].x = ROUND32(p1x);
    polypoints[0].y = ROUND32(p1y);
    polypoints[1].x = ROUND32(p2x);
    polypoints[1].y = ROUND32(p2y);
    objc -= 6;
    objv += 6;
    npoly = 2;

    while (objc >= 2 &&
	    Tcl_GetDoubleFromObj(NULL, objv[0], &p1x) == TCL_OK &&
	    Tcl_GetDoubleFromObj(NULL, objv[1], &p1y) == TCL_OK) {
	polypoints[npoly].x = ROUND32(p1x);
	polypoints[npoly].y = ROUND32(p1y);
	npoly++;
	objc -= 2;
	objv += 2;
    }

    if (objc > 0) {
	Tcl_Size argc = objc + 1;
	objv--;

	const Tcl_ArgvInfo polyArgvInfo[] = {
	    {TCL_ARGV_GENFUNC, "-dash",       ParseDash,     &dash,      NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-fill",       ParseColor,    &fill,      NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-joinstyle",  ParseJoinStyle,&joinstyle, NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-outline",    ParseColor,    &outline,   NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-smooth",     ParseSmooth,   &smooth,    NULL, NULL},
	    {TCL_ARGV_INT,     "-splinesteps",NULL,          &nStep,     NULL, NULL},
	    {TCL_ARGV_STRING,  "-stipple",    NULL,          &stipple,   NULL, NULL},
	    {TCL_ARGV_FLOAT,   "-width",      NULL,          &width,     NULL, NULL},
	    TCL_ARGV_TABLE_END
	};

	if (Tcl_ParseArgsObjv(interp, polyArgvInfo, &argc, objv, NULL )
		!= TCL_OK) {
	    Tcl_Free(polypoints);
	    return TCL_ERROR;
	}
    }

    if (outline.isempty && fill.isempty) {
	return TCL_OK;
    }

    if (outline.isempty) {
	oldpen = SelectObject(hDC, GetStockObject(NULL_PEN));
    } else {
	GdiMakePen(interp, width, (dash != NULL), dash, 0, joinstyle, 0, 0,
	    outline.color, hDC, &oldpen);
    }

    if (fill.isempty) {
	oldbrush = SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));
    } else {
	GdiMakeBrush(fill.color, 0, &lbrush, hDC, (HBRUSH *)&oldbrush);
    }

    if (smooth) { /* Use Smoothize. */
	int nspoints;
	POINT *spoints;

	nspoints = Smoothize(polypoints, npoly, nStep, smooth, &spoints);
	if (nspoints > 0) {
	    /* replace the old point list with the new one */
	    Tcl_Free(polypoints);
	    polypoints = spoints;
	    npoly = nspoints;
	}
    }

    Polygon(hDC, polypoints, npoly);

    GdiFreePen(interp, hDC, oldpen);
    GdiFreeBrush(interp, hDC, oldbrush);

    Tcl_Free(polypoints);
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
    void *clientData,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }
    HDC hDC = dataPtr->printDC;

     /* canvas rectangle item defaults */
    double width = 1.0;
    CanvasColor outline = {0, 0};
    CanvasColor fill    = {0, 1};
    const char *dash    = NULL;
    const char *stipple = NULL;

    LOGBRUSH lbrush;
    HGDIOBJ oldpen = NULL, oldbrush = NULL;

    double x1, y1, x2, y2;

    /* Verrrrrry simple for now.... */
    if (objc < 6) {
	Tcl_WrongNumArgs(interp, 1, objv, "hdc x1 y1 x2 y2 ?option value ...?");
	return TCL_ERROR;
    }

    if ((Tcl_GetDoubleFromObj(interp, objv[2], &x1) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[3], &y1) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[4], &x2) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[5], &y2) != TCL_OK)) {
	return TCL_ERROR;
    }
    if (x1 > x2) {
	double x3 = x1;
	x1 = x2;
	x2 = x3;
    }
    if (y1 > y2) {
	double y3 = y1;
	y1 = y2;
	y2 = y3;
    }
    objc -= 6;
    objv += 6;

    if (objc > 0) {
	Tcl_Size argc = objc + 1;
	objv--;

	const Tcl_ArgvInfo rectArgvInfo[] = {
	    {TCL_ARGV_GENFUNC, "-dash",    ParseDash,  &dash,    NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-fill",    ParseColor, &fill,    NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-outline", ParseColor, &outline, NULL, NULL},
	    {TCL_ARGV_STRING,  "-stipple", NULL,       &stipple, NULL, NULL},
	    {TCL_ARGV_FLOAT,   "-width",   NULL,       &width,   NULL, NULL},
	    TCL_ARGV_TABLE_END
	};

	if (Tcl_ParseArgsObjv(interp, rectArgvInfo, &argc, objv, NULL )
		!= TCL_OK) {
	    return TCL_ERROR;
	}
    }
    if (outline.isempty && fill.isempty) {
	return TCL_OK;
    }

    if (fill.isempty) {
	oldbrush = SelectObject(hDC, GetStockObject(NULL_BRUSH));
    } else {
	GdiMakeBrush(fill.color, 0, &lbrush, hDC, (HBRUSH *)&oldbrush);
    }

    if (outline.isempty) {
	oldpen = SelectObject(hDC, GetStockObject(NULL_PEN));
    } else {
	GdiMakePen(interp, width, (dash != NULL), dash,
	    0, PS_JOIN_MITER, 0, 0, outline.color, hDC, &oldpen);
    }

    /*
     * Per Win32, Rectangle includes lower and right edges--per Tcl8.3.2 and
     * earlier documentation, canvas rectangle does not. Thus, add 1 to
     * right and lower bounds to get appropriate behavior.
     */
    Rectangle(hDC, ROUND32(x1), ROUND32(y1), ROUND32(x2+1), ROUND32(y2+1));

    GdiFreePen(interp, hDC, oldpen);
    GdiFreeBrush(interp, hDC, oldbrush);

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
    void *clientData,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    /*
     * Returns widths of characters from font in an associative array.
     * Font is currently selected font for HDC if not specified.
     * Array name is GdiCharWidths if not specified.
     * Widths should be in the same measures as all other values (1/1000 inch).
     */
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }
    HDC hDC = dataPtr->printDC;

    const char *aryvarname = "GdiCharWidths";
    Tcl_Obj *fontobj       = NULL;

    LOGFONTW lf;
    HFONT hfont;
    HGDIOBJ oldfont = NULL;
    /* For now, assume 256 characters in the font.... */
    int widths[256];
    int retval;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "hdc ?opton value ...?");
	return TCL_ERROR;
    }

    objc -= 2;
    objv += 2;

    if (objc > 0) {
	Tcl_Size argc = objc + 1;
	objv--;

	const Tcl_ArgvInfo cwArgvInfo[] = {
	    {TCL_ARGV_STRING,  "-array", NULL,      &aryvarname, NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-font",  ParseFont, &fontobj,    NULL, NULL},
	    TCL_ARGV_TABLE_END
	};

	if (Tcl_ParseArgsObjv(interp, cwArgvInfo, &argc, objv, NULL )
		!= TCL_OK) {
	    return TCL_ERROR;
	}
    }

    /* is an error not providing a font */
    if (! fontobj) {
	Tcl_AppendResult(interp, "error: font must be specified", (char *)NULL);
	return TCL_ERROR;
    }

    if (GdiMakeLogFont(interp, fontobj, &lf, hDC)) {
	if ((hfont = CreateFontIndirectW(&lf)) != NULL) {
	    oldfont = SelectObject(hDC, hfont);
	}
    } else {
	return TCL_ERROR;
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
	if (oldfont) {
	    SelectObject(hDC, oldfont);
	    DeleteObject(hfont);
	}
	return TCL_ERROR;
    }

    {
	unsigned char i;
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
    if (oldfont) {
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
typedef struct LayoutChunk {
    const char *start;		/* Pointer to simple string to be displayed.
		 * This is a pointer into the TkTextLayout's
		 * string. */
    Tcl_Size numBytes;		/* The number of bytes in this chunk. */
    Tcl_Size numChars;		/* The number of characters in this chunk. */
    Tcl_Size numDisplayChars;	/* The number of characters to display when
		 * this chunk is displayed. Can be less than
		 * numChars if extra space characters were
		 * absorbed by the end of the chunk. This will
		 * be < 0 if this is a chunk that is holding a
		 * tab or newline. */
    int x, y;			/* The origin of the first character in this
		 * chunk with respect to the upper-left hand
		 * corner of the TextLayout. */
    int totalWidth;		/* Width in pixels of this chunk. Used when
		 * hit testing the invisible spaces at the end
		 * of a chunk. */
    int displayWidth;		/* Width in pixels of the displayable
		 * characters in this chunk. Can be less than
		 * width if extra space characters were
		 * absorbed by the end of the chunk. */
} LayoutChunk;

typedef struct TextLayout {
    Tk_Font tkfont;		/* The font used when laying out the text. */
    const char *string;		/* The string that was layed out. */
    int width;			/* The maximum width of all lines in the text
		 * layout. */
    Tcl_Size numChunks;		/* Number of chunks actually used in following
		 * array. */
    LayoutChunk chunks[TKFLEXARRAY];/* Array of chunks. The actual size will be
		 * maxChunks. THIS FIELD MUST BE THE LAST IN
		 * THE STRUCTURE. */
} TextLayout;


static Tcl_Size ParseJustify (
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    Tk_Justify justify;

    if (objc == 0) {
	Tcl_AppendResult(interp,
	    "option \"-justify\" needs an additional argument", (char *)NULL);
	return -1;
    }

    if (Tk_GetJustifyFromObj(interp, objv[0], &justify) != TCL_OK) {
	return -1;
    }

    *(Tk_Justify *)dstPtr = justify;
    return 1;
}

static int GdiText(
    void *clientData,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }
    HDC hDC = dataPtr->printDC;

    double x0, y0;

    Tk_Anchor anchor    = TK_ANCHOR_NW;
    double angle        = 0.0;
    CanvasColor fill    = {0, 0};
    Tcl_Obj *fontobj    = NULL; /* -font shall be provided */
    Tk_Justify justify  = TK_JUSTIFY_LEFT;
    const char *string  = NULL;
    int wraplen         = 0;
    const char *stipple = NULL;

    LOGFONTW lf;
    HFONT hfont;
    HGDIOBJ oldfont;
    int made_font = 0;
    COLORREF oldtextcolor = 0;
    int bgmode;

    if (objc < 4) {
	Tcl_WrongNumArgs(interp, 1, objv, "hdc x y ?option value ...?");
	return TCL_ERROR;
    }

    /* Parse the command. */
    if ((Tcl_GetDoubleFromObj(interp, objv[2], &x0) != TCL_OK)
	    || (Tcl_GetDoubleFromObj(interp, objv[3], &y0) != TCL_OK)) {
	return TCL_ERROR;
    }
    objc -= 4;
    objv += 4;

    if (objc > 0) {
	Tcl_Size argc = objc + 1;
	objv--;

	const Tcl_ArgvInfo textArgvInfo[] = {
	    {TCL_ARGV_GENFUNC, "-anchor",  ParseAnchor,  &anchor,  NULL, NULL},
	    {TCL_ARGV_FLOAT,   "-angle",   NULL,         &angle,   NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-fill",    ParseColor,   &fill,    NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-font",    ParseFont,    &fontobj, NULL, NULL},
	    {TCL_ARGV_GENFUNC, "-justify", ParseJustify, &justify, NULL, NULL},
	    {TCL_ARGV_STRING,  "-stipple", NULL,         &stipple, NULL, NULL},
	    {TCL_ARGV_STRING,  "-text",    NULL,         &string,  NULL, NULL},
	    {TCL_ARGV_INT,     "-width",   NULL,         &wraplen, NULL, NULL},
	    TCL_ARGV_TABLE_END
	};

	if (Tcl_ParseArgsObjv(interp, textArgvInfo, &argc, objv, NULL )
		!= TCL_OK) {
	    return TCL_ERROR;
	}
    }

    /* if we have no text, nothing to do */
    if (! string) {
	return TCL_OK;
    }
    /* if we got empty color, nothing to do */
    if (fill.isempty) {
	return TCL_OK;
    }
    /* is an error not providing a font */
    if (! fontobj) {
	Tcl_AppendResult(interp, "error: font must be specified", (char *)NULL);
	return TCL_ERROR;
    }

    if (GdiMakeLogFont(interp, fontobj, &lf, hDC)) {
	lf.lfEscapement = lf.lfOrientation = 10.0 * angle;
	if ((hfont = CreateFontIndirectW(&lf)) != NULL) {
	    made_font = 1;
	    oldfont = SelectObject(hDC, hfont);
	}
    }

    oldtextcolor = SetTextColor(hDC, fill.color);
    bgmode = SetBkMode(hDC, TRANSPARENT);

    /* Recreate the text layout here, so we get the same width and
     * line breaks.
     */
    Tk_Window tkwin = Tk_MainWindow(interp);
    Tk_Font tkfont = Tk_AllocFontFromObj(interp, tkwin, fontobj);
    if (!tkfont) {
	return TCL_ERROR;
    }
    int width, height;
    TextLayout *layout = (TextLayout *)Tk_ComputeTextLayout(tkfont,
	string, TCL_INDEX_NONE, wraplen, justify, 0, &width, &height);

    /* Calculate the anchor position in local coordinates
     * Origin point is x0, y0
     */
    int xa = 0;
    int ya = 0;
    /* values for the default anchor nw */
    switch (anchor) {
	case TK_ANCHOR_NULL:
	case TK_ANCHOR_NW:
	    xa = 0;
	    ya = 0;
	    break;
	case TK_ANCHOR_N:
	    xa = -width / 2;
	    ya = 0;
	    break;
	case TK_ANCHOR_NE:
	    xa = -width;
	    ya = 0;
	    break;
	case TK_ANCHOR_W:
	    xa = 0;
	    ya = -height / 2;
	    break;
	case TK_ANCHOR_CENTER:
	    xa = -width / 2;
	    ya = -height / 2;
	    break;
	case TK_ANCHOR_E:
	    xa = -width;
	    ya = -height / 2;
	    break;
	case TK_ANCHOR_SW:
	    xa = 0;
	    ya = -height;
	    break;
	case TK_ANCHOR_S:
	    xa = -width / 2;
	    ya = -height;
	    break;
	case TK_ANCHOR_SE:
	    xa = -width;
	    ya = -height;
	    break;
    }
    /* Set the align and adjust the x anchor point accordingly */
    UINT align = TA_TOP;
    switch (justify) {
	case TK_JUSTIFY_NULL:
	case TK_JUSTIFY_LEFT:
	    align |= TA_LEFT;
	    break;
	case TK_JUSTIFY_CENTER:
	    align |= TA_CENTER;
	    xa += width / 2;
	    break;
	case TK_JUSTIFY_RIGHT:
	    align |= TA_RIGHT;
	    xa += width;
	    break;
    }
    SetTextAlign(hDC, align);

    Tk_FontMetrics fm;
    Tk_GetFontMetrics(tkfont, &fm);
    /* Our coordinate system has the y axis inverted.
     * Invert the angle to get the values right.
     */
    const double sinA = sin(DEG2RAD(-angle));
    const double cosA = cos(DEG2RAD(-angle));
    /* now, print each text chunk adjusting the anchor point */
    int retval = 1;
    int nlseen = 0;
    for (Tcl_Size i = 0; (i < layout->numChunks) && retval; i++) {
	WCHAR *wstring;
	Tcl_DString ds;
	int xi, yi;

	if (layout->chunks[i].start[0] == '\n') {
	    if (nlseen) {
		ya += fm.linespace;
	    } else {
		nlseen = 1;
	    }
	    continue;
	}
	xi = floor(x0 + (xa * cosA - ya * sinA) + 0.5);
	yi = floor(y0 + (xa * sinA + ya * cosA) + 0.5);
	Tcl_DStringInit(&ds);
	wstring = Tcl_UtfToWCharDString(layout->chunks[i].start,
	    layout->chunks[i].numBytes, &ds);
	retval = TextOutW(hDC, xi, yi, wstring, (int)Tcl_DStringLength(&ds)/2);
	Tcl_DStringFree(&ds);
	ya += fm.linespace;
	nlseen = 0;
    }

    /* All done. Cleanup */
    Tk_FreeTextLayout((Tk_TextLayout) layout);
    Tk_FreeFont(tkfont);

    /* Get the color set back. */
    SetTextColor(hDC, oldtextcolor);
    SetBkMode(hDC, bgmode);

    if (made_font) {
	SelectObject(hDC, oldfont);
	DeleteObject(hfont);
    }
    return TCL_OK;
}

static int GdiTextPlain(
    void *clientData,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }
    HDC hDC = dataPtr->printDC;
    int x0, y0, retval;
    WCHAR *wstring;
    const char *string;
    Tcl_Size strlen;
    Tcl_DString ds;

    if (objc != 5) {
	Tcl_WrongNumArgs(interp, 1, objv, "hdc x y text");
	return TCL_ERROR;
    }

    if ((Tcl_GetIntFromObj(interp, objv[2], &x0) != TCL_OK)
	    || (Tcl_GetIntFromObj(interp, objv[3], &y0) != TCL_OK)) {
	return TCL_ERROR;
    }

    string = Tcl_GetStringFromObj(objv[4], &strlen);
    Tcl_DStringInit(&ds);
    wstring = Tcl_UtfToWCharDString(string, strlen, &ds);
    retval = TextOutW(hDC, x0, y0, wstring, (int)Tcl_DStringLength(&ds)/2);
    Tcl_DStringFree(&ds);
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
    void *clientData,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }
    HDC hDC = dataPtr->printDC;

    static const char usage_message[] =
	"::tk::print::_gdi map hdc "
	"[-logical x[y]] [-physical x[y]] "
	"[-offset {x y} ] [-default] [-mode mode]";
    int mapmode;	/* Mapping mode. */
    SIZE wextent;	/* Device extent. */
    SIZE vextent;	/* Viewport extent. */
    POINT worigin;	/* Device origin. */
    POINT vorigin;	/* Viewport origin. */
    Tcl_Size argno;

    /* Keep track of what parts of the function need to be executed. */
    int need_usage   = 0;
    int use_logical  = 0;
    int use_physical = 0;
    int use_offset   = 0;
    int use_default  = 0;
    int use_mode     = 0;

    /* Required parameter: HDC for printer. */
    if (objc < 2) {
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

    if ((mapmode = GdiGetHdcInfo(hDC, &worigin, &wextent, &vorigin, &vextent)) == 0) {
	/* Failed!. */
	Tcl_AppendResult(interp, "Cannot get current HDC info", (char *)NULL);
	return TCL_ERROR;
    }

    /* Parse remaining arguments. */
    for (argno = 2; argno < objc; argno++) {
	if (strcmp(Tcl_GetString(objv[argno]), "-default") == 0) {
	    vextent.cx = vextent.cy = wextent.cx = wextent.cy = 1;
	    vorigin.x = vorigin.y = worigin.x = worigin.y = 0;
	    mapmode = MM_TEXT;
	    use_default = 1;
	} else if (strcmp(Tcl_GetString(objv[argno]), "-mode") == 0) {
	    if (argno + 1 >= objc) {
		need_usage = 1;
	    } else {
		mapmode = GdiNameToMode(Tcl_GetString(objv[argno + 1]));
		use_mode = 1;
		argno++;
	    }
	} else if (strcmp(Tcl_GetString(objv[argno]), "-offset") == 0) {
	    if (argno + 1 >= objc) {
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
	    if (argno + 1 >= objc) {
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
	    if (argno + 1 >= objc) {
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
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

    /* Call Windows CTM functions. */
    if (use_logical || use_default || use_mode) { /* Don't call for offset only. */
	SetMapMode(hDC, mapmode);
    }

    if (use_offset || use_default) {
	POINT oldorg;
	SetViewportOrgEx(hDC, vorigin.x, vorigin.y, &oldorg);
	SetWindowOrgEx(hDC, worigin.x, worigin.y, &oldorg);
    }

    if (use_logical) {  /* Same as use_physical. */
	SIZE oldsiz;
	SetWindowExtEx(hDC, wextent.cx, wextent.cy, &oldsiz);
	SetViewportExtEx(hDC, vextent.cx, vextent.cy, &oldsiz);
    }

    /*
     * Since we may not have set up every parameter, get them again for the
     * report.
     */
    mapmode = GdiGetHdcInfo(hDC, &worigin, &wextent, &vorigin, &vextent);

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
    void *clientData,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }
    HDC dst = dataPtr->printDC;
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
    HWND hwnd = 0;

    HANDLE hDib;    /* Handle for device-independent bitmap. */
    LPBITMAPINFOHEADER lpDIBHdr;
    LPSTR lpBits;
    enum PrintType wintype = PTWindow;

    int hgt, wid;
    char *strend;
    long errcode;
    Tcl_Size k;

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
    if (objc < 2) {
	Tcl_AppendResult(interp, usage_message, (char *)NULL);
	return TCL_ERROR;
    }

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
    for (k=2; k<objc; k++) {
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
    Tcl_Obj *const *objv,
    Tcl_Size argc)
{
    Tcl_Size i;
    int retval = 0; /* Number of words that could not be parsed. */

    enum fontStyles {
	STY_BOLD, STY_ITALIC, STY_NORMAL, STY_OVERSTRIKE, STY_ROMAN,
	STY_UNDERLINE
    };
    const char *const styleNames[] = {
	"bold", "italic", "normal", "overstrike", "roman", "underline", NULL
    };
    enum fontStyles sty;

    for (i = 0; i < argc; i++) {
	if (Tcl_GetIndexFromObj(NULL, objv[i], styleNames, NULL, 0, &sty)
		!= TCL_OK) {
	    retval++;
	    continue;
	}
	switch (sty) {
	    case STY_BOLD:
		lf->lfWeight = FW_BOLD;
		break;
	    case STY_ITALIC:
		lf->lfItalic = TRUE;
		break;
	    case STY_NORMAL:
		lf->lfWeight = FW_NORMAL;
		break;
	    case STY_OVERSTRIKE:
		lf->lfStrikeOut = TRUE;
		break;
	    case STY_ROMAN:
		lf->lfItalic = FALSE;
		break;
	    case STY_UNDERLINE:
		lf->lfUnderline = TRUE;
	}
    }
    return retval;
}

/*
 *----------------------------------------------------------------------
 *
 * MakeLogFont --
 *
 *	Takes the font description Tcl_Obj and converts this into a logical
 *	font spec.
 *      The expected font format is a list of {family size ?style ...?}
 *
 * Results:
 *	 Sets font weight.
 *
 *----------------------------------------------------------------------
 */

static int GdiMakeLogFont(
    Tcl_Interp *interp,
    Tcl_Obj *specPtr,
    LOGFONTW *lf,
    HDC hDC)
{
    Tcl_Obj **listPtr;
    Tcl_Size count;

    /* Set up defaults for logical font. */
    memset(lf, 0, sizeof(*lf));
    lf->lfWeight  = FW_NORMAL;
    lf->lfCharSet = DEFAULT_CHARSET;
    lf->lfOutPrecision = OUT_DEFAULT_PRECIS;
    lf->lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf->lfQuality = DEFAULT_QUALITY;
    lf->lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;

    if (!specPtr ||
	    Tcl_ListObjGetElements(interp, specPtr, &count, &listPtr) != TCL_OK) {
	return 0;
    }

    /* Now we have the font structure broken into name, size, weight. */
    if (count >= 1) {
	Tcl_DString ds;
	const char *str = Tcl_GetString(listPtr[0]);

	Tcl_DStringInit(&ds);
	wcsncpy(lf->lfFaceName, Tcl_UtfToWCharDString(str, TCL_INDEX_NONE, &ds),
		LF_FACESIZE);
	lf->lfFaceName[LF_FACESIZE-1] = 0;
	Tcl_DStringFree(&ds);
    } else {
	return 0;
    }

    if (count >= 2) {
	int siz;

	/*
	 * Assumptions:
	 * 1) Like canvas, if a positive number is specified, it's in points.
	 * 2) Like canvas, if a negative number is specified, it's in pixels.
	 */
	if (Tcl_GetIntFromObj(NULL, listPtr[1], &siz) == TCL_OK) { /* If it looks like a number, it is a number.... */
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
	    /*  what, no size ?? */
	    GdiParseFontWords(interp, lf, listPtr+1, count-1);
	}
    }

    if (count >= 3) {
	GdiParseFontWords(interp, lf, listPtr+2, count-2);
    }

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
    double dwidth,
    int dashstyle,
    const char *dashstyledata,
    int endStyle,
    int joinStyle,
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
    int width = ROUND32(dwidth);
    HPEN hPen;
    LOGBRUSH lBrush;
    DWORD pStyle = PS_SOLID;           /* -dash should override*/
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
	char *dup = (char *)Tcl_Alloc(strlen(dashstyledata) + 1);
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
	    Tcl_Free(dup);
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

    dwLen = (DWORD)(bi.biSize + DIBNumColors(&bi) * sizeof(RGBQUAD));

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

    dwLen = (DWORD)(bi.biSize + DIBNumColors(&bi) * sizeof(RGBQUAD) + bi.biSizeImage);

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
    lpLogPal->palNumEntries = (WORD)nColors;

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
 * WinprintDeleted--
 *
 *	Free interp's print resources.
 *
 * -------------------------------------------------------------------------
 */
static void WinprintDeleted(
    void *clientData,
    TCL_UNUSED(Tcl_Interp *))
{
    WinprintData *dataPtr = (WinprintData *)clientData;

    if (dataPtr->printDC != NULL) {
	DeleteDC(dataPtr->printDC);
	Tcl_DStringFree(&dataPtr->jobNameW);
    }
    Tcl_Free(dataPtr);
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
    WinprintData *dataPtr = (WinprintData *)Tcl_Alloc(sizeof(WinprintData));

    /*
     * Set up the low-level [_gdi] command.
     */
    namespacePtr = Tcl_CreateNamespace(interp, gdiName,
	    NULL, (Tcl_NamespaceDeleteProc *) NULL);
    for (i=0; i<numCommands; i++) {
	char buffer[100];

	snprintf(buffer, sizeof(buffer), "%s::%s", gdiName, gdi_commands[i].command_string);
	Tcl_CreateObjCommand2(interp, buffer, gdi_commands[i].command,
	    dataPtr, NULL);
	Tcl_Export(interp, namespacePtr, gdi_commands[i].command_string, 0);
    }
    Tcl_CreateEnsemble(interp, gdiName, namespacePtr, 0);

    /*
     * The other printing-related commands.
     */

    Tcl_CreateObjCommand2(interp, "::tk::print::_selectprinter",
	    PrintSelectPrinter, dataPtr, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::print::_openprinter",
	    PrintOpenPrinter, dataPtr, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::print::_closeprinter",
	    PrintClosePrinter, dataPtr, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::print::_opendoc",
	    PrintOpenDoc, dataPtr, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::print::_closedoc",
	    PrintCloseDoc, dataPtr, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::print::_openpage",
	    PrintOpenPage, dataPtr, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::print::_closepage",
	    PrintClosePage, dataPtr, NULL);

    dataPtr->printDC = NULL;
    Tcl_CallWhenDeleted(interp, WinprintDeleted, dataPtr);
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
    void *clientData,
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size),
    TCL_UNUSED(Tcl_Obj* const*))
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC != NULL) {
	Tcl_AppendResult(interp, "device context still in use: call "
	    "_closedoc first", (char *)NULL);
	return TCL_ERROR;
    }
    PRINTDLGW pd;
    LPCWSTR printerName = NULL;
    PDEVMODEW devmode = NULL;
    LPDEVNAMES devnames = NULL;

    int copies = 0;
    int paper_width = 0;
    int paper_height = 0;
    int dpi_x = 0;
    int dpi_y = 0;
    int returnVal = TCL_OK;

    /* Set up print dialog and initalize property structure. */
    memset(&pd, 0, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = GetDesktopWindow();
    pd.Flags = PD_HIDEPRINTTOFILE | PD_DISABLEPRINTTOFILE | PD_NOSELECTION |
	PD_RETURNDC;

    if (! PrintDlgW(&pd)) {
	unsigned int errorcode = CommDlgExtendedError();
	/*
	 * The user cancelled, or there was an error
	 * The code on the Tcl side checks if the variable
	 * ::tk::print::printer_name is defined to determine
	 * that a valid selection was made.
	 * So we better unset this here, unconditionally.
	 */
	Tcl_UnsetVar(interp, "::tk::print::printer_name", 0);
	if (errorcode != 0) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf("print failed: error %04x",
		    errorcode));
	    Tcl_SetErrorCode(interp, "TK", "PRINT", "DIALOG", NULL);
	    return TCL_ERROR;
	}
	return TCL_OK;
    }

    devmode = (PDEVMODEW) GlobalLock(pd.hDevMode);
    devnames = (LPDEVNAMES) GlobalLock(pd.hDevNames);
    if (! devmode) {
	Tcl_AppendResult(interp, "selected printer doesn't have extended info",
	    (char *)NULL);
	return TCL_ERROR;
    }
    if (! devnames) {
	Tcl_AppendResult(interp, "can't get device names", (char *)NULL);
	return TCL_ERROR;
    }

    printerName = (LPCWSTR) devnames + devnames->wDeviceOffset;
    /* Get values from user-set and built-in properties. */
    dpi_y = devmode->dmYResolution;
    dpi_x = devmode->dmPrintQuality;
    /* Convert height and width to logical points. */
    paper_height = (int) devmode->dmPaperLength / 0.254;
    paper_width = (int) devmode->dmPaperWidth / 0.254;
    copies = pd.nCopies;
    /* Set device context here for all GDI printing operations. */
    dataPtr->printDC = pd.hDC;

    /*
     * Store print properties in variables so they can be accessed from
     * script level.
     */
    if (printerName != NULL) {
	Tcl_DString prname;

	Tcl_DStringInit(&prname);
	Tcl_WCharToUtfDString((WCHAR *)printerName, TCL_INDEX_NONE, &prname);
	Tcl_SetVar2Ex(interp, "::tk::print::printer_name", NULL,
		Tcl_DStringToObj(&prname), 0);
	Tcl_SetVar2Ex(interp, "::tk::print::copies", NULL,
		Tcl_NewIntObj(copies), 0);
	Tcl_SetVar2Ex(interp, "::tk::print::dpi_x", NULL,
		Tcl_NewIntObj(dpi_x), 0);
	Tcl_SetVar2Ex(interp, "::tk::print::dpi_y", NULL,
		Tcl_NewIntObj(dpi_y), 0);
	Tcl_SetVar2Ex(interp, "::tk::print::paper_width", NULL,
		Tcl_NewIntObj(paper_width), 0);
	Tcl_SetVar2Ex(interp, "::tk::print::paper_height", NULL,
		Tcl_NewIntObj(paper_height), 0);
    } else {
	Tcl_UnsetVar(interp, "::tk::print::printer_name", 0);
	Tcl_AppendResult(interp, "selected printer doesn't have name", (char *)NULL);
	DeleteDC(dataPtr->printDC);
	dataPtr->printDC = NULL;
	returnVal = TCL_ERROR;
    }

    GlobalUnlock(devmode);
    GlobalFree(devmode);
    GlobalUnlock(devnames);
    GlobalFree(devnames);
    return returnVal;
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
    void *clientData,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }
    Tcl_DString ds;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "printer");
	return TCL_ERROR;
    }

    /*Start an individual page.*/
    if (StartPage(dataPtr->printDC) <= 0) {
	return TCL_ERROR;
    }

    const char *printer = Tcl_GetString(objv[1]);

    Tcl_DStringInit(&ds);
    if ((OpenPrinterW(Tcl_UtfToWCharDString(printer, -1, &ds),
	    (LPHANDLE)&dataPtr->printDC, NULL)) == FALSE) {
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
    void *clientData,
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size),
    TCL_UNUSED(Tcl_Obj *const *))
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }

    ClosePrinter(dataPtr->printDC);
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
    void *clientData,
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }
    int output = 0;
    const char *jobname;
    Tcl_Size len;

    if (objc < 2 || objc > 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "jobname ?font?");
	return TCL_ERROR;
    }

    jobname = Tcl_GetStringFromObj(objv[1], &len);
    Tcl_DStringInit(&dataPtr->jobNameW);

    /*Get document info.*/
    memset(&dataPtr->di, 0, sizeof(dataPtr->di));
    dataPtr->di.cbSize = sizeof(dataPtr->di);
    dataPtr->di.lpszDocName = Tcl_UtfToWCharDString(jobname, len,
	&dataPtr->jobNameW);

    /*
     * Start printing.
     */
    output = StartDocW(dataPtr->printDC, &dataPtr->di);
    if (output <= 0) {
	Tcl_AppendResult(interp, "unable to start document", (char *)NULL);
	return TCL_ERROR;
    }

    /* the optional argument "font" is useful for plain text documents.
     * we set the font and other defaults here, and return the font width
     * and height just once
     */
    if (objc == 3) {
	LOGFONTW lf;
	HFONT hfont;
	TEXTMETRICW tmw;

	if (GdiMakeLogFont(interp, objv[2], &lf, dataPtr->printDC)) {
	    if ((hfont = CreateFontIndirectW(&lf)) != NULL) {
		SelectObject(dataPtr->printDC, hfont);
	    }
	}
	SetTextAlign(dataPtr->printDC, TA_LEFT);
	SetTextColor(dataPtr->printDC, 0);
	SetBkMode(dataPtr->printDC, TRANSPARENT);

	if (GetTextMetricsW(dataPtr->printDC, &tmw) != 0) {
	    Tcl_Obj *ret[2];

	    ret[0] = Tcl_NewIntObj((int)tmw.tmAveCharWidth);
	    ret[1] = Tcl_NewIntObj((int)tmw.tmHeight);
	    Tcl_SetObjResult(interp, Tcl_NewListObj(2, ret));
	} else {
	    Tcl_AppendResult(interp, "_opendoc: can't determine font ",
		"width and height", (char *)NULL);
	    return TCL_ERROR;
	}
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
    void *clientData,
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size),
    TCL_UNUSED(Tcl_Obj *const *))
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }

    if (EndDoc(dataPtr->printDC) <= 0) {
	Tcl_AppendResult(interp, "unable to close document", (char *)NULL);
	return TCL_ERROR;
    }
    /* delete the font object that might be created as default */
    DeleteObject(SelectObject (dataPtr->printDC,
	GetStockObject(DEVICE_DEFAULT_FONT)));
    DeleteDC(dataPtr->printDC);
    dataPtr->printDC = NULL;
    Tcl_DStringFree(&dataPtr->jobNameW);
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
    void *clientData,
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size),
    TCL_UNUSED(Tcl_Obj *const *))
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }

    /*Start an individual page.*/
    if (StartPage(dataPtr->printDC) <= 0) {
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
    void *clientData,
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size),
    TCL_UNUSED(Tcl_Obj *const *))
{
    WinprintData *dataPtr = (WinprintData *)clientData;
    if (dataPtr->printDC == NULL) {
	Tcl_AppendResult(interp, "device context not initialized", (char *)NULL);
	return TCL_ERROR;
    }

    if (EndPage(dataPtr->printDC) <= 0) {
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
