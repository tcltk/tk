/*
 * tkWinPrint.c --
 *
 *      This module implements Win32 printer access.
 *
 * Copyright © 1998-2019 Harald Oehlmann, Elmicron GmbH
 * Copyright © 2018 Microsoft Corporation.
 * Copyright © 2021 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#if defined(_MSC_VER)
#pragma warning(disable: 4201 4214 4514)
#endif
#define STRICT
#define UNICODE
#define _UNICODE
/* Taget WIndows Server 2003 */
#define WINVER 0x0502
#define _WIN32_WINNT 0x0502
/* TCL Defines */
#define DLL_BUILD

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <tchar.h>

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <tcl.h>
#include <tkInt.h>

/* Helper defines. */

/*
* Values of the Res variable.
*/

/* Success, result value not set */
#define RET_OK_NO_RESULT_SET 2
/* Succes, result value set or not necessary. */
#define RET_OK 0
/* Error and result set. */
#define RET_ERROR -1
/* Printer i/o error. */
#define RET_ERROR_PRINTER_IO -2
/* Out of memory error. */
#define RET_ERROR_MEMORY -3
/* Parameter error. */
#define RET_ERROR_PARAMETER -4
/* User abort. */
#define RET_ERROR_USER -5
/* Printer not open. */
#define RET_ERROR_PRINTER_NOT_OPEN -6
/* Printer driver answered with an error. */
#define RET_ERROR_PRINTER_DRIVER -7

/* Flag parameter of GetDeviceName function. */
#define F_FREE_MEM (1)
#define F_RETURN_LIST (2)


/*
 * File Global Constants.
 */

/* Version information. */
static const char usage_string[] =
	"Windows printing (c) Elmicron GmbH, Harald Oehlmann, 2019-01-23\n"
	"Preparation:\n"
	"  ::tk::print::_print getattr option: possible options:\n"
	"    printers, defaultprinter, copies, firstpage, lastpage, mapmode*,\n"
	"    avecharheight*, avecharwidth*, horzres*, vertres*, dpi*,\n"
	"    physicaloffsetx*, physicaloffsety*, printer, orientation, papersize,\n"
	"    papertypes, mapmodes, fontweights, fontcharsets, fontpitchvalues,\n"
	"    fontfamilies, fontunicoderanges: lists option\n"
	"    fonts*: returns list of unique font name, weight, charset, variable/fixed\n"
	"    fontnames*: returns list of unique font names\n"
	"    fontunicoderanges: returns list of alternating start len unicode point ints\n"
	"    *: requires open printer\n"
	"  ::tk::print::_print pagesetup ?printer? ?Orientation? ?PaperSize? "
	"?left? ?top? ?right? ?bottom?\n"
	"    returns a list of identical parameters reflecting the users choice\n"
	"    Margin unit is millimeter. Default values also by empty string\n"
	"  ::tk::print::_print selectprinter: select a printer\n"
	"  ::tk::print::_print printersetup ?printer? ? Orientation? ?PageSize?\n"
	"    Sets up the printer options and returns them.\n"
	"    Not exposed printer settings are editable.\n"
	"Open printer: use one of:\n"
	"  ::tk::print::_print openjobdialog ?printer? ?Orientation? ?PaperSize? ?Maxpage?\n"
	"  ::tk::print::_print openprinter ?printer? ?Orientation? ?PaperSize?\n"
	"Get information about the print job and user selections:\n"
	"  ::tk::print::_print getattr {copies firstpage lastpage avecharheight avecharwidth"
		"horzres\n"
	"    vertres dpi physicaloffsetx physicaloffsety printer orientation "
		"papersize}\n"
	"  The dpi value is used to transform from paint units (pixel) to mm:\n"
	"    Size/[mm] = [::tk::print::_print getattr horzres]/[::tk::print::_print getattr dpi]*2.54\n"
	"Start document and page\n"
	"  ::tk::print::_print opendoc jobname\n"
	"  ::tk::print::_print openpage\n"
	"Configure and select drawing tools\n"
	"  ::tk::print::_print setmapmode mapmode\n"
	"    Define the coordinate system. 'Text' is in device units origin "
		"top-up.\n"
	"  ::tk::print::_print pen width ?r g b?: r,g,b is 16 bit color value (internal / 256)\n"
	"    No rgb values uses black color.\n"
	"  ::tk::print::_print brushcolor r g b: filling for rectangle\n"
	"  winfo bkcolor r g b: text background\n"
	"  ::tk::print::_print fontcreate Fontnumber Fontname Points/10 ?Weight? ?Italic? "
		"?Charset?\n"
		"    ?Pitch? ?Family? : use getattr font* to get possible values.\n"
	"  ::tk::print::_print fontselect Fontnumber\n"
	"Create printed items:\n"
	"  ::tk::print::_print ruler x0 y0 width height\n"
	"  ::tk::print::_print rectangle x0 y0 x1 y1\n"
	"  ::tk::print::_print text X0 Y0 Text ?r g b?: no rgb uses black text\n"
	"  ::tk::print::_print getfirstfontnochar Text: -1 or first index with no glyph\n"
	"  ::tk::print::_print gettextsize Text\n"
	"  ::tk::print::_print photo tkimage X0 Y0 ?Width? ?Height?\n"
	"Close page and printjob\n"
	"  ::tk::print::_print closepage    Close a page\n"
	"  ::tk::print::_print closedoc     Close the document\n"
	"  ::tk::print::_print close ?option?\n"
	"    Close and cleanup the printing interface.\n"
	"    If the option -eraseprinterstate is given, also the printer settings "
		"not passed\n"
	"    to the script level are deleted."
	"";


/* File Global Variables */
static BOOL fPDLGInitialised = FALSE;
static PRINTDLG pdlg;
static HPEN hPen = NULL;
static HFONT hFont[10] =
	{NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
/* Index of the actually selected font, -1:None */
static int SelectedFont = -1;

/*
 *  Interpreter pointer to return automatic errors from the EnumerateFontsEx
 *  callback and the ListFontsEx function.
 */
static Tcl_Interp *fg_interp;

/* Subcommand "getattr" option list and indexes. */
static const char *fg_getattr_sub_cmds[] = {
	"printers", "defaultprinter", "copies", "firstpage", "lastpage",
	"mapmode", "avecharheight", "avecharwidth", "horzres", "vertres",
	"dpi", "physicaloffsetx", "physicaloffsety",
	"printer", "orientation", "papersize",
	"papertypes", "mapmodes",
	"fontweights", "fontcharsets", "fontpitchvalues", "fontfamilies", "fonts",
	"fontnames", "fontunicoderanges", NULL};
typedef enum {
	iPrinters, iDefaultPrinter, iCopies, iFirstPage, iLastPage,
	iMapMode, iAveCharHeight, iAveCharWidth, iHorzRes, iVertRes,
	iDPI, iPhysicalOffsetX, iPhysicalOffsetY,
	iPrinter, iOrientation, iPaperSize,
	iPaperTypes, iMapModes,
	iFontWeights, iFontCharsets, iFontPitchValues, iFontFamilies, iFonts,
	iFontNames, iFontUnicodeRanges} fg_getattr_i_command;

/* Subcommand "pagesetup" orientation option list and indexes. */
static const char *fg_orient_sub_cmds[] = {"portrait", "landscape", "", NULL};
static short fg_orient_i_command[] = {
	DMORIENT_PORTRAIT,
	DMORIENT_LANDSCAPE,
	-1};

/* Subcommand "pagesetup" pagesize. */
static const char *fg_papersize_sub_cmds[] = {
	"Letter", "LetterSmall", "Tabloid", "Ledger", "Legal", "Statement",
	"Executive", "A3", "A4", "A4Small", "A5", "B4", "B5", "Folio", "Quarto",
	"10X14", "11X17", "Note", "Env_9", "Env_10", "Env_11", "Env_12", "Env_14",
	"CSheet", "DSheet", "ESheet", "Env_Dl", "Env_C5", "Env_C3", "Env_C4",
	"Env_C6", "Env_C65", "Env_B4", "Env_B5", "Env_B6", "Env_Italy",
	"Env_Monarch", "Env_Personal", "Fanfold_Us", "Fanfold_Std_German",
	"Fanfold_Lgl_German", "Iso_B4", "Japanese_Postcard", "9X11", "10X11",
	"15X11", "Env_Invite", "Reserved_48", "Reserved_49", "Letter_Extra",
	"Legal_Extra", "Tabloid_Extra", "A4_Extra", "Letter_Transverse",
	"A4_Transverse", "Letter_Extra_Transverse", "A_Plus", "B_Plus",
	"Letter_Plus", "A4_Plus", "A5_Transverse", "B5_Transverse", "A3_Extra",
	"A5_Extra", "B5_Extra", "A2", "A3_Transverse", "A3_Extra_Transverse",
	"Dbl_Japanese_Postcard", "A6", "JEnv_Kaku2", "JEnv_Kaku3", "JEnv_Chou3",
	"JEnv_Chou4", "Letter_Rotated", "A3_Rotated", "A4_Rotated", "A5_Rotated",
	"B4_JIS_Rotated", "B5_JIS_Rotated", "Japanese_Postcard_Rotated",
	"Dbl_Japanese_Postcard_Rotated", "A6_Rotated", "JEnv_Kaku2_Rotated",
	"JEnv_Kaku3_Rotated", "JEnv_Chou3_Rotated", "JEnv_Chou4_Rotated", "B6_JIS",
	"B6_Jis_Rotated", "12X11", "Jenv_You4", "Jenv_You4_Rotated", "P16K", "P32K",
	"P32Kbig", "PEnv_1", "PEnv_2", "PEnv_3", "PEnv_4", "PEnv_5", "PEnv_6",
	"PEnv_7", "PEnv_8", "PEnv_9", "PEnv_10", "P16K_Rotated", "P32K_Rotated",
	"P32Kbig_Rotated", "PEnv_1_Rotated", "PEnv_2_Rotated", "PEnv_3_Rotated",
	"PEnv_4_Rotated", "PEnv_5_Rotated", "PEnv_6_Rotated", "PEnv_7_Rotated",
	"PEnv_8_Rotated", "PEnv_9_Rotated", "PEnv_10_Rotated",
	"User",
	"", NULL };
static short fg_papersize_i_command[] = {
	 DMPAPER_LETTER,
	 DMPAPER_LETTERSMALL,
	 DMPAPER_TABLOID,
	 DMPAPER_LEDGER,
	 DMPAPER_LEGAL,
	 DMPAPER_STATEMENT,
	 DMPAPER_EXECUTIVE,
	 DMPAPER_A3,
	 DMPAPER_A4,
	 DMPAPER_A4SMALL,
	 DMPAPER_A5,
	 DMPAPER_B4,
	 DMPAPER_B5,
	 DMPAPER_FOLIO,
	 DMPAPER_QUARTO,
	 DMPAPER_10X14,
	 DMPAPER_11X17,
	 DMPAPER_NOTE,
	 DMPAPER_ENV_9,
	 DMPAPER_ENV_10,
	 DMPAPER_ENV_11,
	 DMPAPER_ENV_12,
	 DMPAPER_ENV_14,
	 DMPAPER_CSHEET,
	 DMPAPER_DSHEET,
	 DMPAPER_ESHEET,
	 DMPAPER_ENV_DL,
	 DMPAPER_ENV_C5,
	 DMPAPER_ENV_C3,
	 DMPAPER_ENV_C4,
	 DMPAPER_ENV_C6,
	 DMPAPER_ENV_C65,
	 DMPAPER_ENV_B4,
	 DMPAPER_ENV_B5,
	 DMPAPER_ENV_B6,
	 DMPAPER_ENV_ITALY,
	 DMPAPER_ENV_MONARCH,
	 DMPAPER_ENV_PERSONAL,
	 DMPAPER_FANFOLD_US,
	 DMPAPER_FANFOLD_STD_GERMAN,
	 DMPAPER_FANFOLD_LGL_GERMAN,
	 DMPAPER_ISO_B4,
	 DMPAPER_JAPANESE_POSTCARD,
	 DMPAPER_9X11,
	 DMPAPER_10X11,
	 DMPAPER_15X11,
	 DMPAPER_ENV_INVITE,
	 DMPAPER_RESERVED_48,
	 DMPAPER_RESERVED_49,
	 DMPAPER_LETTER_EXTRA,
	 DMPAPER_LEGAL_EXTRA,
	 DMPAPER_TABLOID_EXTRA,
	 DMPAPER_A4_EXTRA,
	 DMPAPER_LETTER_TRANSVERSE,
	 DMPAPER_A4_TRANSVERSE,
	 DMPAPER_LETTER_EXTRA_TRANSVERSE,
	 DMPAPER_A_PLUS,
	 DMPAPER_B_PLUS,
	 DMPAPER_LETTER_PLUS,
	 DMPAPER_A4_PLUS,
	 DMPAPER_A5_TRANSVERSE,
	 DMPAPER_B5_TRANSVERSE,
	 DMPAPER_A3_EXTRA,
	 DMPAPER_A5_EXTRA,
	 DMPAPER_B5_EXTRA,
	 DMPAPER_A2,
	 DMPAPER_A3_TRANSVERSE,
	 DMPAPER_A3_EXTRA_TRANSVERSE,
	 DMPAPER_DBL_JAPANESE_POSTCARD,
	 DMPAPER_A6,
	 DMPAPER_JENV_KAKU2,
	 DMPAPER_JENV_KAKU3,
	 DMPAPER_JENV_CHOU3,
	 DMPAPER_JENV_CHOU4,
	 DMPAPER_LETTER_ROTATED,
	 DMPAPER_A3_ROTATED,
	 DMPAPER_A4_ROTATED,
	 DMPAPER_A5_ROTATED,
	 DMPAPER_B4_JIS_ROTATED,
	 DMPAPER_B5_JIS_ROTATED,
	 DMPAPER_JAPANESE_POSTCARD_ROTATED,
	 DMPAPER_DBL_JAPANESE_POSTCARD_ROTATED,
	 DMPAPER_A6_ROTATED,
	 DMPAPER_JENV_KAKU2_ROTATED,
	 DMPAPER_JENV_KAKU3_ROTATED,
	 DMPAPER_JENV_CHOU3_ROTATED,
	 DMPAPER_JENV_CHOU4_ROTATED,
	 DMPAPER_B6_JIS,
	 DMPAPER_B6_JIS_ROTATED,
	 DMPAPER_12X11,
	 DMPAPER_JENV_YOU4,
	 DMPAPER_JENV_YOU4_ROTATED,
	 DMPAPER_P16K,
	 DMPAPER_P32K,
	 DMPAPER_P32KBIG,
	 DMPAPER_PENV_1,
	 DMPAPER_PENV_2,
	 DMPAPER_PENV_3,
	 DMPAPER_PENV_4,
	 DMPAPER_PENV_5,
	 DMPAPER_PENV_6,
	 DMPAPER_PENV_7,
	 DMPAPER_PENV_8,
	 DMPAPER_PENV_9,
	 DMPAPER_PENV_10,
	 DMPAPER_P16K_ROTATED,
	 DMPAPER_P32K_ROTATED,
	 DMPAPER_P32KBIG_ROTATED,
	 DMPAPER_PENV_1_ROTATED,
	 DMPAPER_PENV_2_ROTATED,
	 DMPAPER_PENV_3_ROTATED,
	 DMPAPER_PENV_4_ROTATED,
	 DMPAPER_PENV_5_ROTATED,
	 DMPAPER_PENV_6_ROTATED,
	 DMPAPER_PENV_7_ROTATED,
	 DMPAPER_PENV_8_ROTATED,
	 DMPAPER_PENV_9_ROTATED,
	 DMPAPER_PENV_10_ROTATED,
	 DMPAPER_USER,
	 -1
	};

/* Map modes */
static const char *fg_map_modes_sub_cmds[] = {
	"Text",
	"LoMetric",
	"HiMetric",
	"LoEnglish",
	"HiEnglish",
	"Twips",
	"Isotropic",
	"Anisotropic",
	NULL
};
static int fg_map_modes_i_command[] = {
	MM_TEXT,
	MM_LOMETRIC,
	MM_HIMETRIC,
	MM_LOENGLISH,
	MM_HIENGLISH,
	MM_TWIPS,
	MM_ISOTROPIC,
	MM_ANISOTROPIC
};

/*
 * Font weights.
  */
/* Map modes */
static const char *fg_font_weight_sub_cmds[] = {
	"Dontcare",
	"Thin",
	"Extralight",
	"Light",
	"Normal",
	"Medium",
	"Semibold",
	"Bold",
	"Extrabold",
	"Heavy",
	NULL
};
static int fg_font_weight_i_command[] = {
	FW_DONTCARE,
	FW_THIN,
	FW_EXTRALIGHT,
	FW_LIGHT,
	FW_NORMAL,
	FW_MEDIUM,
	FW_SEMIBOLD,
	FW_BOLD,
	FW_EXTRABOLD,
	FW_HEAVY
};

static const char *fg_font_charset_sub_cmds[] = {
	"Default",
	"ANSI",
	"Symbol",
	"ShiftJIS",
	"Hangeul",
	"Hangul",
	"GB2312",
	"ChineseBig5",
	"OEM",
	"Johab",
	"Hebrew",
	"Arabic",
	"Greek",
	"Turkish",
	"Vietnamese",
	"Thai",
	"Easteurope",
	"Russian",
	"Mac",
	"Baltic",
	NULL
};
static int fg_font_charset_i_command[] = {
	DEFAULT_CHARSET,
	ANSI_CHARSET,
	SYMBOL_CHARSET,
	SHIFTJIS_CHARSET,
	HANGEUL_CHARSET,
	HANGUL_CHARSET,
	GB2312_CHARSET,
	CHINESEBIG5_CHARSET,
	OEM_CHARSET,
	HEBREW_CHARSET,
	ARABIC_CHARSET,
	GREEK_CHARSET,
	TURKISH_CHARSET,
	VIETNAMESE_CHARSET,
	THAI_CHARSET,
	EASTEUROPE_CHARSET,
	RUSSIAN_CHARSET,
	MAC_CHARSET,
	BALTIC_CHARSET
};

static const char *fg_font_pitch_sub_cmds[] = {
	"Default",
	"Fixed",
	"Variable",
	"Mono",
	NULL
};

static int fg_font_pitch_i_command[] = {
	DEFAULT_PITCH,
	FIXED_PITCH,
	VARIABLE_PITCH
	,MONO_FONT
};

static const char *fg_font_family_sub_cmds[] = {
	"Dontcare",
	"Roman",
	"Swiss",
	"Modern",
	"Script",
	"Decorative",
	NULL
};

static int fg_font_family_i_command[] = {
	FF_DONTCARE,
	FF_ROMAN,
	FF_SWISS,
	FF_MODERN,
	FF_SCRIPT,
	FF_DECORATIVE
};

/* Declaration for functions used later in this file.*/
static int WinPrintCmd(ClientData clientData, Tcl_Interp *interp,
			    int objc, Tcl_Obj *const objv[]);
static TCHAR * ReturnLockedDeviceName( HGLOBAL hDevNames );
static char GetDeviceName(
	Tcl_Interp *interp,
	HGLOBAL hDevNames,
	char Flags );
static char PrintSelectPrinter( Tcl_Interp *interp );
static Tcl_Obj * GetOrientation( DEVMODE * pDevMode );
static Tcl_Obj * GetPaperSize( DEVMODE * pDevMode );
static char AppendOrientPaperSize(  Tcl_Interp *interp, DEVMODE * pDevMode );
static char PrintPrinterSetup( Tcl_Interp *interp, TCHAR *Printer,
	short Orientation, short PaperSize);
static char PrintPageSetup( Tcl_Interp *interp, TCHAR *pPrinter,
	short Orientation, short PaperSize,
	int Left, int Top, int Right, int Bottom );
static char CreateDevMode( TCHAR * pPrinter, short Orientation, short PaperSize,
	char fShowPropertySheet );
static char PrintOpenPrinter(
	TCHAR * pPrinter, short Orientation, short PaperSize);
static char PrintReset( char fPreserveDeviceData );
static char PrintOpenJobDialog(
	TCHAR * pPrinter,
	short Orientation,
	short PaperSize,
	unsigned short MaxPage
	);
static char PrintOpenDoc(Tcl_Obj *resultPtr, TCHAR *DocName);
static char PrintCloseDoc();
static char PrintOpenPage();
static char PrintClosePage();
static char PrintGetAttr(Tcl_Interp *interp, int Index);
static char PrintSetAttr(Tcl_Interp *interp, int Index, Tcl_Obj *oParam);
static char DefaultPrinterGet( Tcl_Interp *interp );
static char ListPrinters(Tcl_Interp *interp);
static char ListChoices(Tcl_Interp *interp, const char *ppChoiceList[]);
static char PrintSetMapMode( int MapMode);
static char LoadDefaultPrinter( );
static char DefaultPrinterGet( Tcl_Interp *interp );
static char PrintPen(int Width, COLORREF Color);
static char PrintBrushColor(COLORREF Color);
static char PrintBkColor(COLORREF Color);
static char PrintRuler(int X0, int Y0, int LenX, int LenY);
static char PrintRectangle(int X0, int Y0, int X1, int Y1);
static char PrintFontCreate(int FontNumber,
	TCHAR *Name, double PointSize, int Weight, int Italic, int Charset,
	int Pitch, int Family);
static char PrintFontSelect(int FontNumber);
static char PrintText(int X0, int Y0, TCHAR *pText, COLORREF Color );
static char PrintGetTextSize( Tcl_Interp *interp, TCHAR *pText);
static char ListFonts(Tcl_Interp *interp, HDC hDC, int fFontNameOnly);
static char ListFontUnicodeRanges(Tcl_Interp *interp, HDC hDC);
static char GetFirstTextNoChar(Tcl_Interp *interp, TCHAR *pText);
static int CALLBACK EnumFontFamExProc(
	ENUMLOGFONTEX *lpelfe,    /* logical-font data */
	NEWTEXTMETRICEX *lpntme,  /* physical-font data */
	DWORD FontType,           /* type of font */
	LPARAM lParam             /* application-defined data */
);
static char PaintPhoto( Tcl_Interp *interp, Tcl_Obj *const oImageName,
	int PosX, int PosY, int Width, int Height);


/*DLL entry point */

#if 0
BOOL __declspec(dllexport) WINAPI DllEntryPoint(
	HINSTANCE hInstance,
	DWORD seginfo,
	LPVOID lpCmdLine)
{
  /* Don't do anything, so just return true */
  return TRUE;
}
#endif

/*Initialisation Function,*/

int Winprint_Init (Tcl_Interp *Interp)
{
	if (Tcl_InitStubs(Interp, "8.6-", 0) == NULL
		|| Tk_InitStubs(Interp, TK_VERSION, 0) == NULL)
	{
		return RET_ERROR;
	}
	Tcl_CreateObjCommand(Interp, "::tk::print::_print", WinPrintCmd, (ClientData)NULL,
		(Tcl_CmdDeleteProc *)NULL);
	
	return RET_OK;
}

/*Called routine */

/*
 * --------------------------------------------------------------------------
 *
 * WinPrintCmd --
 *
 *      Provides core interface to Win32 printing API from Tcl.
 *
 * Results:
 *      Returns a standard Tcl result.
 *
 * -------------------------------------------------------------------------
 */

int WinPrintCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
	/* Option list and indexes */
	const char *subCmds[] = {
		"help", "selectprinter", "printersetup", "pagesetup",
		"openjobdialog",
		"openprinter", "close", "closedoc", "openpage",
		"closepage", "version", "getattr", "setattr", "opendoc",
		"pen", "brushcolor", "bkcolor",
		"fontselect", "gettextsize", "ruler", "rectangle", "fontcreate",
		"text", "textuni", "getfirstfontnochar",
		"photo",
		NULL};
	enum iCommand {
		iHelp, iSelectPrinter, iPrinterSetup, iPageSetup,
		iOpenjobdialog,
		iOpenPrinter, iClose, iClosedoc, iOpenpage,
		iClosepage, iGetattr, iSetAttr, iOpendoc,
		iPen, iBrushColor, iBkColor,
		iFontselect, iGetTextSize, iRuler, iRectangle, iFontCreate,
		iText, iTextuni, iGetFirstFontNochar,
		iPhoto
		};

	/*
	 * State variables.
	 */

	/* Choice of option. */
	int Index;
	/* Result flag. */
	char Res;
	/* Result of Tcl functions. */
	int TclResult;
	/* Store the parameters in strings. */
	int iPar[8];
	Tcl_Obj *resultPtr = Tcl_GetObjResult(interp);
	int ParCur;
	Tcl_DString	sPar1;
	int PositionSPar;
	/*
	 * Check if option argument is given and decode it.
	 */
	if (objc > 1)
	{
		if (RET_ERROR ==
			Tcl_GetIndexFromObj(interp, objv[1], subCmds, "subcmd", 0, &Index))
			return RET_ERROR;
	} else {
		Tcl_WrongNumArgs(interp, 1, objv, "subcmd");
		return RET_ERROR;
	}

	/* Check parameters and give usage messages. */
	switch (Index) {
	case iGetattr:
	case iOpendoc:
	case iFontselect:
	case iGetTextSize:
	case iGetFirstFontNochar:
		if (objc != 3)
		{
			Tcl_WrongNumArgs(interp, 2, objv, "argument");
			return RET_ERROR;
		}
		break;
	case iSetAttr:
		if (objc != 4)
		{
			Tcl_WrongNumArgs(interp, 3, objv, "argument");
			return RET_ERROR;
		}
		break;
	case iText:
	case iTextuni:
		if (objc != 5 && objc != 8) {
			Tcl_WrongNumArgs(interp, 2, objv, "X0 Y0 text ?red green blue?");
			return RET_ERROR;
		}
		break;
	case iRuler:
	case iRectangle:
		if (objc != 6)
		{
			Tcl_WrongNumArgs(interp, 2, objv, "X0 Y0 Width Height");
			return RET_ERROR;
		}
		break;
	case iFontCreate:
		if (objc < 5 || objc > 10)
		{
			Tcl_WrongNumArgs(interp, 2, objv,
				"Fontnumber Fontname Points ?Weight? ?Italic? ?Charset?"
				" ?Pitch? ?Family?");
			return RET_ERROR;
		}
		break;
	case iPhoto:
		if (objc < 5 || objc > 7)
		{
			Tcl_WrongNumArgs(interp, 2, objv,
				"imagename x0 y0 ?width? ?height?");
			return RET_ERROR;
		}
		break;
	case iPen:
		/* width and optionally red green blue together */
		if (objc != 3 && objc != 6) {
			Tcl_WrongNumArgs(interp, 2, objv, "width ?red green blue?");
			return RET_ERROR;
		}
		break;
	case iBrushColor:
	case iBkColor:
		if (objc != 5) {
			Tcl_WrongNumArgs(interp, 2, objv, "red green blue");
			return RET_ERROR;
		}
		break;
	}

	/* Default result. */
	Res = RET_OK;

	/*
	 * One string parameter.
     * if this option is not given, a 0 pointer
     * is present.
     */
	Tcl_DStringInit(& sPar1);
	switch (Index) {
	case iPrinterSetup:
	case iPageSetup:
	case iOpendoc:
	case iOpenPrinter:
	case iOpenjobdialog:
	case iGetTextSize:
	case iGetFirstFontNochar:
		PositionSPar = 2;
		break;
	case iFontCreate:
		PositionSPar = 3;
		break;
	case iText:
	case iTextuni:
		PositionSPar = 4;
		break;
	default:
		PositionSPar = -1;
	}
	if ( -1 != PositionSPar )
	{
		if ( objc > PositionSPar )
		{
			char *pStr;
			int lStr;
			pStr = Tcl_GetStringFromObj(objv[PositionSPar],&lStr);
			Tcl_WinUtfToTChar( pStr, lStr, &sPar1);
		}
	}
	/*
	 * Decode parameters and invoke.
	*/
	switch (Index) {
	case iHelp:
		Tcl_SetStringObj(resultPtr, usage_string,-1);
		break;
	case iSelectPrinter:
		Res = PrintSelectPrinter( interp );
		break;
	case iClose:
		{
			const char *close_subCmds[] = {
				"-eraseprinterstate",
				NULL
			};
			enum iCloseCommand {
				iErasePrinterState
			};
			char fPreserveState;
			/* Decode argument. */
			if ( objc > 2 )
			{
				int OptionIndex;
				if (RET_ERROR ==
					Tcl_GetIndexFromObj(
						interp, objv[2], close_subCmds, "option", 0,
						&OptionIndex))
				{
					Res = RET_ERROR;
				} else {
					switch (OptionIndex)
					{
					case iErasePrinterState:
						fPreserveState = 0;
						break;
					default:
						fPreserveState = 1;
						break;
					}
				}
			} else {
				fPreserveState = 1;
			}
			if ( Res == RET_OK )
			{
				Res = PrintReset( fPreserveState );
			}
		}
		break;
	case iClosedoc:
		Res=PrintCloseDoc();
		break;
	case iOpenpage:
		Res=PrintOpenPage();
		break;
	case iClosepage:
		Res=PrintClosePage();
		break;
	case iGetTextSize:
		Res = PrintGetTextSize( interp, (TCHAR *)Tcl_DStringValue(& sPar1) );
		break;
	case iGetattr:
	case iSetAttr:
		/* One Index parameter. */
		{
			int IndexAttr;
			if (RET_ERROR ==
				Tcl_GetIndexFromObj(
					interp, objv[2], fg_getattr_sub_cmds, "getattr", 0,
					&IndexAttr))
			{
				return RET_ERROR;
			}
			if ( Index == iGetattr )
			{
				Res = PrintGetAttr( interp, IndexAttr );
			} else {
				Res = PrintSetAttr( interp, IndexAttr, objv[3] );
			}
		}
		break;
	case iOpendoc:
		Res = PrintOpenDoc( resultPtr, (TCHAR *)Tcl_DStringValue(& sPar1));
		break;
	case iPageSetup:
	case iPrinterSetup:
	case iOpenPrinter:
	case iOpenjobdialog:
		{
			short Orientation = -1;
			short PaperSize;
			unsigned short MaxPage;
			double Double;
			/*
			 * Argument 2: Printer is already in sPar or NULL.
			 */

			/* Orientation */
			if ( objc > 3 )
			{
				int ParInt;
				if (RET_ERROR ==
					Tcl_GetIndexFromObj(
						interp, objv[3], fg_orient_sub_cmds, "orient", 0,
						&ParInt))
				{
					Res = RET_ERROR;
				} else {
					Orientation = fg_orient_i_command[ParInt];
				}
			}
			/* Paper Size */
			if ( objc > 4 )
			{
				int ParInt;
				if (RET_ERROR ==
					Tcl_GetIndexFromObj(
						interp, objv[4], fg_papersize_sub_cmds, "papersize", 0,
						&ParInt))
				{
					Res = RET_ERROR;
				} else {
					PaperSize = fg_papersize_i_command[ParInt];
				}
			} else {
				PaperSize = -1;
			}
			switch (Index)
			{
			case iPrinterSetup:
				if ( Res == RET_OK )
				{
					Res = PrintPrinterSetup(
						interp, (TCHAR *)Tcl_DStringValue(& sPar1),
						Orientation,PaperSize );
				}
				break;
			case iPageSetup:
				/* Margins: Left, Top, Right, Bottom. */
				if ( objc <= 5
					|| RET_OK != Tcl_GetDoubleFromObj(interp,objv[5], &Double) )
				{
					iPar[0] = -1;
				} else {
					iPar[0] = (int) (Double * 100);
				}
				if ( objc <= 6
					|| RET_OK != Tcl_GetDoubleFromObj(interp,objv[6], &Double) )
				{
					iPar[1] = -1;
				} else {
					iPar[1] = (int) (Double * 100);
				}
				if ( objc <= 7
					|| RET_OK != Tcl_GetDoubleFromObj(interp,objv[7], &Double) )
				{
					iPar[2] = -1;
				} else {
					iPar[2] = (int) (Double * 100);
				}
				if ( objc <= 8
					|| RET_OK != Tcl_GetDoubleFromObj(interp,objv[8], &Double) )
				{
					iPar[3] = -1;
				} else {
					iPar[3] = (int) (Double * 100);
				}
				if ( Res == RET_OK )
				{
					Res = PrintPageSetup(
						interp, (TCHAR *)Tcl_DStringValue(& sPar1),
						Orientation,PaperSize,
						iPar[0], iPar[1], iPar[2],
						iPar[3]);
				}
				break;
			case iOpenPrinter:
				if ( Res == RET_OK )
				{
					Res = PrintOpenPrinter(
						(TCHAR *) Tcl_DStringValue(& sPar1),
						Orientation, PaperSize );
				}
				break;
			case iOpenjobdialog:
			default:
				/* MaxPage */
				if ( objc > 5 )
				{
					int ParInt;
					if (RET_ERROR ==
						Tcl_GetIntFromObj( interp, objv[5], &ParInt))
					{
						Res = RET_ERROR;
					}
					MaxPage = (unsigned short) ParInt;
				} else {
					MaxPage = 0;
				}
				if ( Res == RET_OK )
				{
					Res = PrintOpenJobDialog(
						(TCHAR *)Tcl_DStringValue(& sPar1),
						Orientation,
						PaperSize,
						MaxPage );
				}
				break;
			}
		}
		break;
	case iFontCreate:
		/* | Type	| name			| ParCur	| objv	| iParCur */
		/* +--------+---------------+-----------+-------+-------- */
		/* | int	| Font number 	| 0			| 2		| 0 */
		/* | string	| font name 	| 1			| 3		| % */
		/* | double	| points 		| 2			| 4		| % */
		/* | choice	| Weight		| 3			| 5		| 3 */
		/* | int0/1	| Italic		| 4			| 6		| 4 */
		/* | choice	| Charset		| 5			| 7		| 5 */
		/* | choice	| Pitch			| 6			| 8		| 6 */
		/* | choice | Family		| 7			| 9		| 7 */
		{
			double dPointSize;
			int IndexOut;
			const char ** pTable;
			const char * pMsg;
			const int *pValue;

			/* Set default values. */
			iPar[3] = FW_DONTCARE; /* Weight */
			iPar[4] = 0; /* Default Italic: off */
			iPar[5] = DEFAULT_CHARSET; /* Character set */
			iPar[6] = FW_DONTCARE; /* Pitch */
			iPar[7] = FF_DONTCARE; /* Family */

			for ( ParCur = 0 ; ParCur < objc-2 && Res != RET_ERROR ; ParCur++)
			{
				switch (ParCur)
				{
				case 1:
					/* Font name: Char parameter was already decoded */
					break;
				case 2:
					/* Point Size: double parameter */
					if (RET_ERROR ==
						Tcl_GetDoubleFromObj(
							interp,
							objv[ParCur+2],& dPointSize ) )
					{
						Res = RET_ERROR;
					}
					break;
				case 3:
					/* Weight */
				case 5:
					/* CharSet */
				case 6:
					/* Pitch */
				case 7:
					/* Family */
					switch (ParCur)
					{
					case 3:
						pTable = fg_font_weight_sub_cmds;
						pValue = fg_font_weight_i_command;
						pMsg = "font weight";
						break;
					case 5:
						pTable = fg_font_charset_sub_cmds;
						pValue = fg_font_charset_i_command;
						pMsg = "font charset";
						break;
					case 6:
						pTable = fg_font_pitch_sub_cmds;
						pValue = fg_font_pitch_i_command;
						pMsg = "font pitch";
						break;
					case 7:
					default:
						pTable = fg_font_family_sub_cmds;
						pValue = fg_font_family_i_command;
						pMsg = "font family";
						break;
					}
					if (RET_ERROR ==
						Tcl_GetIndexFromObj(
							interp, objv[ParCur+2], pTable,
							pMsg, 0, & IndexOut ) )
					{
						Res = RET_ERROR;
					} else {
						iPar[ParCur] = pValue[IndexOut];
					}
					break;
				case 0:
					/* Font Number */
				case 4:
					/* Italic */
				default:
					/* Int parameter */
					if (RET_ERROR ==
						Tcl_GetIntFromObj(
							interp,
							objv[ParCur+2],& (iPar[ParCur])) )
					{
						Res = RET_ERROR;
					}
					break;
				}
			}
			if (Res != RET_ERROR)
			{
				Res = PrintFontCreate(
					iPar[0], (TCHAR *)Tcl_DStringValue(& sPar1),
					dPointSize, iPar[3],
					iPar[4], iPar[5], iPar[6], iPar[7]);
			}
		}
		break;
	case iFontselect:
		/* One int parameter */
		TclResult = Tcl_GetIntFromObj(interp, objv[2], & (iPar[0]));
		if (TclResult == RET_OK) {
			Res = PrintFontSelect( iPar[0]);
		} else {
			Res = RET_ERROR;
		}
		break;
	case iPen:
		/* One int parameter and 3 optional color parameter. */
		if (RET_OK != Tcl_GetIntFromObj(interp, objv[2], & (iPar[0]))) {
			Res = RET_ERROR;
		} else {
			COLORREF Color = 0;
			if (objc > 3) {
				int r,g,b;
				if (RET_OK != Tcl_GetIntFromObj(interp, objv[3], &r)) {
					Res = RET_ERROR;
				} else if (RET_OK != Tcl_GetIntFromObj(interp, objv[4], &g)) {
					Res = RET_ERROR;
				} else if (RET_OK != Tcl_GetIntFromObj(interp, objv[5], &b)) {
					Res = RET_ERROR;
				} else {
					Color = RGB(r/256,g/256,b/256);
				}
			}
			Res = PrintPen( iPar[0],Color);
		}
		break;
	case iBrushColor:
	case iBkColor:
		/* 3 color parameter. */
		{
			COLORREF Color = 0;
			int r,g,b;
			if (RET_OK != Tcl_GetIntFromObj(interp, objv[2], &r)) {
				Res = RET_ERROR;
			} else if (RET_OK != Tcl_GetIntFromObj(interp, objv[3], &g)) {
				Res = RET_ERROR;
			} else if (RET_OK != Tcl_GetIntFromObj(interp, objv[4], &b)) {
				Res = RET_ERROR;
			} else {
				Color = RGB(r/256,g/256,b/256);
			}
			if (Index == iBrushColor)
				Res = PrintBrushColor(Color);
			else
				Res = PrintBkColor(Color);
		}
		break;
	case iText:
	case iTextuni:
		/* Two int, one string and optional 3 color parameters. */
		if ( RET_OK != Tcl_GetIntFromObj(interp,objv[2],& (iPar[0])) ) {
			Res = RET_ERROR;
		} else if ( RET_OK != Tcl_GetIntFromObj(interp,objv[3],& (iPar[1])) ) {
			Res = RET_ERROR;
		} else {
			COLORREF Color = 0;
			if (objc > 5) {
				int r,g,b;
				if (RET_OK != Tcl_GetIntFromObj(interp, objv[5], &r)) {
					Res = RET_ERROR;
				} else if (RET_OK != Tcl_GetIntFromObj(interp, objv[6], &g)) {
					Res = RET_ERROR;
				} else if (RET_OK != Tcl_GetIntFromObj(interp, objv[7], &b)) {
					Res = RET_ERROR;
				} else {
					Color = RGB(r/256,g/256,b/256);
				}
			}
			Res = PrintText( iPar[0], iPar[1],
					(TCHAR *)Tcl_DStringValue(& sPar1), Color );
		}
		break;
	case iGetFirstFontNochar:
		/* One string. */
		Res = GetFirstTextNoChar( interp, (TCHAR *)Tcl_DStringValue(& sPar1));
		break;
	case iRuler:
	case iRectangle:
		/* 4 int */
		for ( ParCur=0 ; ParCur < 4 ; ParCur++ )
		{
			if ( RET_ERROR == Tcl_GetIntFromObj(interp,
				objv[ParCur+2],& (iPar[ParCur])) )
			{
				Res = RET_ERROR;
				break;
			}
		}
		if (Res != RET_ERROR)
		{
			if (Index == iRuler)
				Res = PrintRuler(iPar[0], iPar[1], iPar[2], iPar[3]);
			else
				Res = PrintRectangle(iPar[0], iPar[1], iPar[2], iPar[3]);
		}
		break;

	case iPhoto:
		/* tkImg + 2..4 int: X0, Y0, Width, Height */
		/* initialize optional parameters */
		iPar[2] = 0;
		iPar[3] = 0;
		for ( ParCur=0 ; ParCur < objc-3 ; ParCur++ )
		{
			if ( RET_ERROR == Tcl_GetIntFromObj(interp,
				objv[ParCur+3],& (iPar[ParCur])) )
			{
				Res = RET_ERROR;
				break;
			}
		}
		if (Res != RET_ERROR) {
			Res = PaintPhoto(interp, objv[2], iPar[0], iPar[1], iPar[2],
				iPar[3]);
		}
		break;
	}
	/*
	 * Free any intermediated strings.
	 */

	/* String parameter. */
	Tcl_DStringFree(& sPar1);

	/*
	 * Format return value.
	*/
	switch (Res)
	{
	case RET_OK_NO_RESULT_SET:
		Tcl_SetStringObj( resultPtr, "", -1);
		/* FALLTHRU */
	case RET_OK:
		return RET_OK;
	case RET_ERROR_PRINTER_IO:
		Tcl_SetStringObj( resultPtr, "Printer I/O error",-1);
		return RET_ERROR;
	case RET_ERROR_MEMORY:
		Tcl_SetStringObj( resultPtr, "Out of memory",-1);
		return RET_ERROR;
	case RET_ERROR_PARAMETER:
		Tcl_SetStringObj( resultPtr, "Wrong parameter",-1);
		return RET_ERROR;
	case RET_ERROR_USER:
		Tcl_SetStringObj( resultPtr, "User abort",-1);
		return RET_ERROR;
	case RET_ERROR_PRINTER_NOT_OPEN:
		Tcl_SetStringObj( resultPtr, "Printer not open",-1);
		return RET_ERROR;
	case RET_ERROR_PRINTER_DRIVER:
		Tcl_SetStringObj( resultPtr, "Printer driver error",-1);
		return RET_ERROR;
	default:
	case RET_ERROR:
		return RET_ERROR;
	}
}

/*
 * --------------------------------------------------------------------------
 *
 * ReturnLockedDeviceName --
 *
 *      Extract the locked device name from the hDevNames structure and returns
 *      its pointer. hDevNames must be unlocked on success (which captures
 *      the return value).

 * Results:
 *      Returns the device name.
 *
 * -------------------------------------------------------------------------
 */

static TCHAR * ReturnLockedDeviceName( HGLOBAL hDevNames )
{
	LPDEVNAMES pDevNames;
	pDevNames = (LPDEVNAMES) GlobalLock( hDevNames );
	if ( NULL == pDevNames )
		return NULL;
	if ( pDevNames->wDeviceOffset == 0)
	{
		GlobalUnlock( hDevNames );
		return NULL;
	}
	return ( (TCHAR *) pDevNames ) + ( pDevNames->wDeviceOffset );
}


/*
 * --------------------------------------------------------------------------
 *
 * GetDeviceName  --
 *
 *      Extract the device name from the hDevNames structure and put it in the
 * 		interpreter result.
 *
 * Results:
 *      Returns the device name.
 *
 * -------------------------------------------------------------------------
 */


static char GetDeviceName(
	Tcl_Interp *interp,
	HGLOBAL hDevNames,
	char Flags )
{
	char Ret;
	TCHAR * pPrinter;
	Tcl_DString	Printer;

	pPrinter = ReturnLockedDeviceName( hDevNames );
	if ( pPrinter == NULL )
		return RET_ERROR_PRINTER_IO;

	Tcl_DStringInit( &Printer );
	Tcl_WinTCharToUtf( pPrinter, -1, &Printer);
	Ret = RET_OK;
	if ( Flags & F_RETURN_LIST )
	{
		Tcl_Obj *PrinterObj;
		Tcl_Obj	*lResult;

		PrinterObj = Tcl_NewStringObj(
			Tcl_DStringValue( &Printer ),
			Tcl_DStringLength( &Printer ) );
		Tcl_DStringFree( &Printer );

		lResult = Tcl_GetObjResult( interp );
		if ( RET_OK !=
			Tcl_ListObjAppendElement( interp, lResult, PrinterObj ))
		{
			/* Error already set in interp */
			Ret = RET_ERROR;
		}
	} else {
		Tcl_DStringResult( interp, &Printer );
	}
	GlobalUnlock( hDevNames );

	if ( Flags & F_FREE_MEM )
	{
		GlobalFree(hDevNames);
	}
	return Ret;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintSelectPrinter --
 *
 *      Return the selected printer using the printer selection box.
 *
 * Results:
 *      Returns the selected printer.
 *
 * -------------------------------------------------------------------------
 */

static char PrintSelectPrinter( Tcl_Interp *interp )
{
	PrintReset( 1 );
	pdlg.Flags = 0
		| PD_DISABLEPRINTTOFILE
		| PD_HIDEPRINTTOFILE
		| PD_NOPAGENUMS
		| PD_NOSELECTION
		| PD_USEDEVMODECOPIESANDCOLLATE
		;
	if ( PrintDlg( &pdlg ) == FALSE)
		return RET_ERROR_USER;
	/* Return the selected printer name. */
	if ( NULL == pdlg.hDevNames )
		return RET_ERROR_USER;
	/* Get device names. */
	return GetDeviceName( interp, pdlg.hDevNames, 0 );
}

/*
 * --------------------------------------------------------------------------
 *
 * GetOrientation --
 *
 *      Search the DevMode structure for an orientation value and return
 *      it as a Tcl object. If not found, NULL is returned.
 *
 * Results:
 *      Returns the selected orientation.
 *
 * -------------------------------------------------------------------------
 */

static Tcl_Obj * GetOrientation( DEVMODE * pDevMode )
{
	const char * pText;
	int IndexCur;

	if ( pDevMode == NULL)
		return NULL;

	pText = NULL;
	for (IndexCur = 0; fg_orient_sub_cmds[IndexCur] != NULL ; IndexCur++)
	{
		if ( pDevMode->dmOrientation == fg_orient_i_command[IndexCur] )
		{
			pText = fg_orient_sub_cmds[IndexCur];
			break;
		}
	}
	if ( NULL == pText )
		return NULL;

	return Tcl_NewStringObj( pText, -1 );
}

/*
 * --------------------------------------------------------------------------
 *
 * GetPaperSize--
 *
 *      Search the DevMode structure for a paper size value and return
 *      it as a Tcl object. If not found, NULL is returned.
 *
 * Results:
 *      Returns the paper size.
 *
 * -------------------------------------------------------------------------
 */

static Tcl_Obj * GetPaperSize( DEVMODE * pDevMode )
{
	const char * pText;
	int IndexCur;

	if ( pDevMode == NULL)
		return NULL;

	pText = NULL;
	for (IndexCur = 0; fg_papersize_sub_cmds[IndexCur] != NULL ; IndexCur++)
	{
		if ( pDevMode->dmPaperSize == fg_papersize_i_command[IndexCur] )
		{
			pText = fg_papersize_sub_cmds[IndexCur];
			break;
		}
	}
	if ( NULL == pText )
		return NULL;

	return Tcl_NewStringObj( pText, -1 );
}

/*
 * --------------------------------------------------------------------------
 *
 * AppendOrientPaperSize--
 *
 *      Append orientation and paper size to the configuration.
 *
 * Results:
 *      Returns the paper size.
 *
 * -------------------------------------------------------------------------
 */

static char AppendOrientPaperSize(  Tcl_Interp *interp, DEVMODE * pDevMode )
{
	Tcl_Obj	*lResult;
	Tcl_Obj	*pObj;

	lResult = Tcl_GetObjResult( interp );

	/* Orientation */
	pObj = GetOrientation( pDevMode );
	if ( pObj == NULL )
		return RET_ERROR_PRINTER_IO;

	if ( RET_OK !=
		Tcl_ListObjAppendElement( interp, lResult, pObj ))
	{
		return RET_ERROR;
	}

	/* PaperSize */
	pObj = GetPaperSize( pDevMode );
	if ( pObj == NULL )
		return RET_ERROR_PRINTER_IO;

	if ( RET_OK !=
		Tcl_ListObjAppendElement( interp, lResult, pObj ))
	{
		return RET_ERROR;
	}
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintPrinterSetup--
 *
 *     Show the page setup dialogue box and for paper size and orientation
 *     and return the users selection as Tcl variables.
 *
 * Results:
 *     Returns the paper size and orientation.
 *
 * -------------------------------------------------------------------------
 */

static char PrintPrinterSetup( Tcl_Interp *interp, TCHAR *pPrinter,
	short Orientation, short PaperSize)
{
	char Res;
	DEVMODE *pDevMode;

	PrintReset( 1 );
	Res = CreateDevMode( pPrinter, Orientation, PaperSize, 1 );
	if ( RET_OK != Res )
		return Res;
	if ( pdlg.hDevMode == NULL )
	{
		return RET_ERROR_PRINTER_IO;
	}
	pDevMode = GlobalLock( pdlg.hDevMode );
	if ( NULL == pDevMode )
		return RET_ERROR_MEMORY;

	/* Orientation and paper size */
	if ( Res == RET_OK )
	{
		Res = AppendOrientPaperSize( interp, pDevMode );
	}

	GlobalUnlock( pdlg.hDevMode );

	return Res;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintPageSetup--
 *
 *     Show the page setup dialogue box and return the users selection
*      as Tcl variables.
 *
 * Results:
 *      Returns the complete page setup.
 *
 * -------------------------------------------------------------------------
 */

static char PrintPageSetup( Tcl_Interp *interp, TCHAR *pPrinter,
	short Orientation, short PaperSize,
	int Left, int Top, int Right, int Bottom
	)
{
	PAGESETUPDLG sPageSetupDlg;
	char Res;
	Tcl_Obj *pObj;
	Tcl_Obj	*lResult;

	PrintReset( 1 );

	ZeroMemory( & sPageSetupDlg, sizeof( sPageSetupDlg ) );
	sPageSetupDlg.lStructSize = sizeof( sPageSetupDlg );

	/* Get old device names */
	sPageSetupDlg.hDevNames = pdlg.hDevNames;

	Res = CreateDevMode( pPrinter, Orientation, PaperSize, 0);
	if (Res != RET_OK || pdlg.hDevMode == NULL )
		return Res;

	/* Copy devmode pointer */
	sPageSetupDlg.hDevMode = pdlg.hDevMode;

	/* Initialise with current values */
	sPageSetupDlg.Flags = 0
		| PSD_INHUNDREDTHSOFMILLIMETERS
		| PSD_MARGINS
		;
	sPageSetupDlg.rtMargin.left = ( Left != -1) ? Left : 2500;
	sPageSetupDlg.rtMargin.top = ( Top != -1) ? Top : 2500;
	sPageSetupDlg.rtMargin.right = ( Right != -1) ? Right : 2500;
	sPageSetupDlg.rtMargin.bottom = ( Bottom != -1) ? Bottom : 2500;

	/* Show page setup dialog box. */
	if ( FALSE == PageSetupDlg( & sPageSetupDlg ) )
	{
		DWORD Err;
		Err = CommDlgExtendedError();
		if ( Err == 0 )
		{
			/* User cancel. */
			return RET_ERROR_USER;
		} else {
			/* Printer error. */
			return RET_ERROR_PRINTER_IO;
		}
	}

	/* Get device name. */
	Res = GetDeviceName( interp, sPageSetupDlg.hDevNames, F_RETURN_LIST );

	if ( sPageSetupDlg.hDevNames != pdlg.hDevNames
		&& sPageSetupDlg.hDevNames != NULL)
	{
		if ( pdlg.hDevNames != NULL )
			GlobalFree( pdlg.hDevNames );

		pdlg.hDevNames = sPageSetupDlg.hDevNames;
	}

	/* Get device mode data. */
	if ( sPageSetupDlg.hDevMode != NULL )
	{
		DEVMODE *pDevMode;
		pDevMode = GlobalLock( sPageSetupDlg.hDevMode );
		if ( NULL == pDevMode )
			return RET_ERROR_MEMORY;

		/* Orientation and paper size. */
		if ( Res == RET_OK )
		{
			Res = AppendOrientPaperSize( interp, pDevMode );
		}

		/* Save the DevMode structure handle */
		if ( pdlg.hDevMode != sPageSetupDlg.hDevMode )
		{
			if ( pdlg.hDevMode != NULL )
				GlobalFree( pdlg.hDevMode );
			pdlg.hDevMode = sPageSetupDlg.hDevMode;
		}
		GlobalUnlock( sPageSetupDlg.hDevMode );
	}

	/* Get and treat margin rectangle. */

	lResult = Tcl_GetObjResult( interp );

	if ( Res == RET_OK )
	{
		pObj = Tcl_NewDoubleObj( sPageSetupDlg.rtMargin.left / 100.0 );
		if ( RET_OK != Tcl_ListObjAppendElement( interp, lResult, pObj ))
			Res = RET_ERROR;
	}
	if ( Res == RET_OK )
	{
		pObj = Tcl_NewDoubleObj( sPageSetupDlg.rtMargin.top / 100.0 );
		if ( RET_OK != Tcl_ListObjAppendElement( interp, lResult, pObj ))
			Res = RET_ERROR;
	}
	if ( Res == RET_OK )
	{
		pObj = Tcl_NewDoubleObj( sPageSetupDlg.rtMargin.right / 100.0 );
		if ( RET_OK != Tcl_ListObjAppendElement( interp, lResult, pObj ))
			Res = RET_ERROR;
	}
	if ( Res == RET_OK )
	{
		pObj = Tcl_NewDoubleObj( sPageSetupDlg.rtMargin.bottom / 100.0 );
		if ( RET_OK != Tcl_ListObjAppendElement( interp, lResult, pObj ))
			Res = RET_ERROR;
	}
	return Res;
}


/*
 * --------------------------------------------------------------------------
 *
 * CreateDevMode--
 *
 *     Create a DevMode structure for the given settings. The devmode
 *     structure is put in a moveable memory object. The handle is placed
 *     in pdlg.hDevMode.
 *
 * Results:
 *      Creates a DevMode structure for the printer.
 *
 * -------------------------------------------------------------------------
 */
char CreateDevMode( TCHAR * pPrinter, short Orientation, short PaperSize,
	char fShowPropertySheet )
{
	HANDLE hPrinter;
	DEVMODE* lpDevMode;
	LONG Size;
	DWORD fMode;
	char fDevNamesLocked;
	char Res;

	Res = RET_OK;
	/* If no printer given use last or default printer. */
	if ( pPrinter == NULL || pPrinter[0] == '\0' )
	{
		if ( pdlg.hDevNames == NULL )
		{
			Res = LoadDefaultPrinter( );
			if ( Res != RET_OK )
				return Res;
		}
		pPrinter = ReturnLockedDeviceName( pdlg.hDevNames );
		fDevNamesLocked = 1;
	} else {
		fDevNamesLocked = 0;
	}
	/* Get Printer handle. */
	if ( FALSE == OpenPrinter( pPrinter, &hPrinter, NULL) )
	{
		hPrinter = NULL;
		Res = RET_ERROR_PRINTER_IO;
	}
	/* Get DevMode structure size. */
	if (Res == RET_OK )
	{
		Size = DocumentProperties( NULL, hPrinter, pPrinter, NULL, NULL, 0 );
		if ( Size < 0 )
		{
			Res = RET_ERROR_PRINTER_IO;
		}
	}

	/* Adjust or get new memory. */
	lpDevMode = NULL;
	if (Res == RET_OK )
	{
		if ( pdlg.hDevMode != NULL )
			pdlg.hDevMode = GlobalReAlloc( pdlg.hDevMode, Size, GMEM_ZEROINIT);
		else
			pdlg.hDevMode = GlobalAlloc( GMEM_MOVEABLE | GMEM_ZEROINIT, Size);
		lpDevMode = GlobalLock( pdlg.hDevMode );
		if ( pdlg.hDevMode == NULL || lpDevMode == NULL)
		{
			Res = RET_ERROR_MEMORY;
		}
	}

	/* Initialise if new. */
	if ( Res == RET_OK && lpDevMode->dmSize == 0 )
	{
		/* Get default values */
		if ( IDOK != DocumentProperties(
			NULL,
			hPrinter,
			pPrinter,
			lpDevMode,
			NULL,
			DM_OUT_BUFFER ) )
		{
			Res = RET_ERROR_PRINTER_IO;
		}
	}

	if (Res == RET_OK )
	{
		/* Set values. */
		if (Orientation != -1 )
		{

			lpDevMode->dmFields |= DM_ORIENTATION;
			lpDevMode->dmOrientation = Orientation;
		}
		if ( PaperSize != -1 )
		{
			lpDevMode->dmFields |= DM_PAPERSIZE;
			lpDevMode->dmPaperSize = PaperSize;
		}
		/* ---------------------------------------------------------------------- */
		/* Modify present and eventually show property dialogue */
		fMode = DM_IN_BUFFER | DM_OUT_BUFFER;
		if ( fShowPropertySheet )
			fMode |= DM_IN_PROMPT;

		Size = DocumentProperties(
			NULL,
			hPrinter,
			pPrinter,
			lpDevMode,
			lpDevMode,
			fMode );

		if ( Size < 0 )
		{
					Res = RET_ERROR_PRINTER_IO;
		}
	}
	if ( fDevNamesLocked )
		GlobalUnlock( pdlg.hDevNames );
	if ( hPrinter != NULL )
		ClosePrinter( hPrinter );
	if ( lpDevMode != NULL )
		GlobalUnlock( pdlg.hDevMode );
	if ( Res != RET_OK )
	{
		GlobalFree( pdlg.hDevMode );
		pdlg.hDevMode = NULL;
	}
	/* User may pres the cancel button when interactive. */
	if ( Res == RET_OK && fShowPropertySheet && Size == IDCANCEL )
		return RET_ERROR_USER;
	return Res;
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

char PrintOpenPrinter(
	TCHAR * pPrinter, short Orientation, short PaperSize)
{
	DEVMODE* lpInitData;
	char Res;
	char fDevNamesLocked;

	PrintReset( 1 );

	Res = CreateDevMode( pPrinter, Orientation, PaperSize, 0 );
	if ( RET_OK != Res )
		return Res;
	if ( pdlg.hDevMode == NULL
		|| NULL == ( lpInitData = GlobalLock( pdlg.hDevMode ) ) )
	{
		return RET_ERROR_MEMORY;
	}

	/*
	 * If no printer given, it was loaded by CreateDevMode in
	 * pdlg.hDeviceNames.
	 */
	if ( pPrinter == NULL || pPrinter[0] == '\0' )
	{
		if (pdlg.hDevNames == NULL
			|| NULL == (pPrinter = ReturnLockedDeviceName( pdlg.hDevNames ) ) )
		{
			return RET_ERROR_PRINTER_IO;
		}
		fDevNamesLocked = 1;
	} else {
		fDevNamesLocked = 0;
	}

	pdlg.hDC = CreateDC(
		/* "WINSPOOL", */
		NULL,
		pPrinter,
		NULL,
		lpInitData);

	GlobalUnlock( pdlg.hDevMode );
	if ( fDevNamesLocked )
		GlobalUnlock( pdlg.hDevNames );
	if ( pdlg.hDC == NULL)
		return RET_ERROR_PRINTER_IO;
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintOpenJobDialog--
 *
 *     Open the print job dialog.
 *
 * Results:
 *      Opens the job dialog.
 *
 * -------------------------------------------------------------------------
 */

char PrintOpenJobDialog(
	TCHAR * pPrinter,
	short Orientation,
	short PaperSize,
	unsigned short MaxPage
	)
{
	char Res;

	PrintReset( 1 );

	Res = CreateDevMode( pPrinter, Orientation, PaperSize, 0 );
	if ( RET_OK != Res )
		return Res;

	if (MaxPage == 0)
	{
		pdlg.nFromPage = 0;
		pdlg.nToPage = 0;
		pdlg.nMinPage = 0;
		pdlg.nMaxPage = 0;
	} else {
		if (pdlg.nFromPage < 1)
			pdlg.nFromPage = 1;
		if (pdlg.nToPage > MaxPage)
			pdlg.nToPage = MaxPage;
		pdlg.nMinPage = 1;
		pdlg.nMaxPage = MaxPage;
	}

	pdlg.Flags = PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE | PD_RETURNDC ;

	if ( PrintDlg( &pdlg ) == FALSE)
		return RET_ERROR_USER;

	return RET_OK;
}


/*
 * --------------------------------------------------------------------------
 *
 * PrintReset--
 *
 *      Free any resource which might be opened by a print command.
 *      Initialise the print dialog structure.
 *
 * Results:
 *      Free print resources and re-start the print dialog structure.
 *
 * -------------------------------------------------------------------------
 */

char PrintReset( char fPreserveDeviceData )
{
	int i;
	if (hPen != NULL)
	{
		SelectObject(pdlg.hDC, GetStockObject (BLACK_PEN));
		DeleteObject(hPen);
		hPen = NULL;
	}
	if (SelectedFont != -1)
	{
		SelectObject(pdlg.hDC, GetStockObject(SYSTEM_FONT));
		SelectedFont = -1;
	}
	for (i = 0; i < 10 ; i++)
	{
		if (hFont[i] != NULL)
		{
			DeleteObject(hFont[i]);
			hFont[i] = NULL;
		}
	}
	/*
	 * Free members of the pdlg structure.
     */
	if ( fPDLGInitialised )
	{
		if (pdlg.hDC != NULL)
		{
			DeleteDC(pdlg.hDC);
			pdlg.hDC = NULL;
		}
		if ( ! fPreserveDeviceData )
		{

			/* Free any Device mode data */
			if ( pdlg.hDevMode != NULL )
			{
				GlobalFree( pdlg.hDevMode );
				pdlg.hDevMode = NULL;
			}

			/* Free any Device Names data. */
			if ( pdlg.hDevNames != NULL )
			{
				GlobalFree( pdlg.hDevNames );
				pdlg.hDevNames = NULL;
			}
		}
	} else {
		/*
		 * Initialise pdlg structure.
		 */
		memset( &pdlg, 0, sizeof( PRINTDLG ) );
		pdlg.lStructSize = sizeof( PRINTDLG );
		fPDLGInitialised = TRUE;
	}
	return RET_OK;
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

char PrintOpenDoc(Tcl_Obj *resultPtr, TCHAR *DocName)
{
	int JobID;
    DOCINFO di;

	if (pdlg.hDC == NULL)
		return RET_ERROR_PRINTER_NOT_OPEN;

	memset( &di, 0, sizeof( DOCINFO ) );
    di.cbSize = sizeof( DOCINFO );
    di.lpszDocName = DocName;
    JobID = StartDoc(pdlg.hDC, &di);
	if ( JobID > 0 )
	{
		Tcl_SetIntObj(resultPtr, JobID);
		return RET_OK;
	}
	return RET_ERROR_PRINTER_IO;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintCloseDoc--
 *
 *     Closes the document for printing.
 *
 * Results:
 *      Closes the print document.
 *
 * -------------------------------------------------------------------------
 */


char PrintCloseDoc()
{
	if ( EndDoc(pdlg.hDC) > 0)
		return RET_OK;
	return RET_ERROR_PRINTER_IO;
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

char PrintOpenPage()
{

/*
 * Here we have to (re)set the mapping mode and select all objects
 * because StartPage starts with default values.
 */
	if ( StartPage(pdlg.hDC) <= 0)
		return RET_ERROR_PRINTER_IO;
	else {
		if (0 == SetMapMode(pdlg.hDC, MM_LOMETRIC))
			return RET_ERROR_PRINTER_IO;
		if (hPen != NULL)
		{
			if (NULL == SelectObject(pdlg.hDC, hPen))
				return RET_ERROR_PRINTER_IO;
		}
		if (SelectedFont != -1)
		{
			if ( RET_OK != PrintFontSelect(SelectedFont))
				return RET_ERROR_PRINTER_IO;
		}
		/* Activate Brush where we can set the color. */
		SelectObject(pdlg.hDC, GetStockObject(DC_BRUSH));
	}
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintClosePage--
 *
 *    Closes the printed page.
 *
 * Results:
 *    Closes the page.
 *
 * -------------------------------------------------------------------------
 */

char PrintClosePage()
{
	if ( EndPage(pdlg.hDC) > 0)
		return RET_OK;
	return RET_ERROR_PRINTER_IO;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintGetAttr--
 *
 *    Get the printer attributes.
 *
 * Results:
 *    Returns the printer attributes.
 *
 * -------------------------------------------------------------------------
 */

char PrintGetAttr(Tcl_Interp *interp, int Index)
{
	char Res;
	DEVMODE * pDevMode;

	/*
	 * State variables.
	 */

	/* Check for open printer when hDC is required. */
	switch ( Index )
	{
	case iMapMode:
	case iAveCharHeight:
	case iAveCharWidth:
	case iHorzRes:
	case iVertRes:
	case iDPI:
	case iPhysicalOffsetX:
	case iPhysicalOffsetY:
	case iFonts:
	case iFontNames:
	case iFontUnicodeRanges:
		if (pdlg.hDC == NULL)
			return RET_ERROR_PRINTER_NOT_OPEN;
	}

	/* Check for Allocated DeviceMode structure. */
	switch ( Index )
	{
	case iOrientation:
	case iPaperSize:
		if (pdlg.hDevMode == NULL)
			return RET_ERROR_PRINTER_NOT_OPEN;
		pDevMode = GlobalLock( pdlg.hDevMode );
		if ( pDevMode == NULL )
			return RET_ERROR_MEMORY;
		break;
	default:
		pDevMode = NULL;
		break;
	}

	/* Choice of option. */
	Res = RET_OK;
	switch ( Index )
	{
	case iCopies:
		Tcl_SetIntObj(Tcl_GetObjResult(interp), pdlg.nCopies);
		return RET_OK;
	case iFirstPage:
		Tcl_SetIntObj(Tcl_GetObjResult(interp),
			0 != (pdlg.Flags & PD_PAGENUMS) ? pdlg.nFromPage : pdlg.nMinPage);
		return RET_OK;
	case iLastPage:
		Tcl_SetIntObj(Tcl_GetObjResult(interp),
			0 != (pdlg.Flags & PD_PAGENUMS) ? pdlg.nToPage : pdlg.nMaxPage);
		return RET_OK;
	case iMapMode:
		{
			int MapMode;
			int Pos;
			MapMode = GetMapMode(pdlg.hDC);
			if ( 0 == MapMode )
				return RET_ERROR_PRINTER_IO;
			for ( Pos = 0 ; NULL != fg_map_modes_sub_cmds[Pos] ; Pos++ )
			{
				if ( MapMode == fg_map_modes_i_command[Pos] )
				{
					Tcl_SetStringObj(Tcl_GetObjResult(interp),
						fg_map_modes_sub_cmds[Pos], -1);
					return RET_OK;
				}
			}
			return RET_ERROR_PARAMETER;
		}
	case iAveCharHeight:
		{
			TEXTMETRIC tm;
			if( TRUE==GetTextMetrics(pdlg.hDC, &tm))
			{
				Tcl_SetIntObj(
					Tcl_GetObjResult(interp),
					tm.tmHeight + tm.tmExternalLeading);
				return RET_OK;
			}
			return RET_ERROR_PRINTER_IO;
		}
	case iAveCharWidth:
		{
			TEXTMETRIC tm;
			if( TRUE==GetTextMetrics(pdlg.hDC, &tm))
			{
				Tcl_SetIntObj(Tcl_GetObjResult( interp ), tm.tmAveCharWidth);
				return RET_OK;
			}
			return RET_ERROR_PRINTER_IO;
		}
	case iHorzRes:
		Tcl_SetIntObj(
			Tcl_GetObjResult( interp ),
			GetDeviceCaps(pdlg.hDC, HORZRES));
		return RET_OK;
	case iVertRes:
		Tcl_SetIntObj(
			Tcl_GetObjResult( interp ),
			GetDeviceCaps(pdlg.hDC, VERTRES));
		return RET_OK;
	case iDPI:
		Tcl_SetIntObj(
			Tcl_GetObjResult( interp ),
			GetDeviceCaps(pdlg.hDC, LOGPIXELSX));
		return RET_OK;
	case iPhysicalOffsetX:
		Tcl_SetIntObj(
			Tcl_GetObjResult( interp ),
			GetDeviceCaps(pdlg.hDC, PHYSICALOFFSETX));
		return RET_OK;
	case iPhysicalOffsetY:
		Tcl_SetIntObj(
			Tcl_GetObjResult( interp ),
			GetDeviceCaps(pdlg.hDC, PHYSICALOFFSETY));
		return RET_OK;
	case iPrinter:
		if ( fPDLGInitialised
			&& pdlg.hDevNames != NULL)
		{
			return GetDeviceName( interp, pdlg.hDevNames, FALSE );
		} else {
			return RET_ERROR_PRINTER_IO;
		}
	case iOrientation:
		{
			Tcl_Obj * pObj;
			pObj = GetOrientation( pDevMode );
			if ( pObj != NULL )
			{
				Tcl_SetObjResult( interp, pObj );
			} else {
				Res = RET_ERROR_PRINTER_IO;
			}
		}
		break;
	case iPaperSize:
		{
			Tcl_Obj * pObj;
			pObj = GetPaperSize( pDevMode );
			if ( pObj != NULL )
			{
				Tcl_SetObjResult( interp, pObj );
			} else {
				Res = RET_ERROR_PRINTER_IO;
			}
		}
		break;
	case iDefaultPrinter:
		return DefaultPrinterGet( interp );
	case iPrinters:
		return ListPrinters( interp );
	case iPaperTypes:
		return ListChoices( interp, fg_papersize_sub_cmds );
	case iMapModes:
		return ListChoices( interp, fg_map_modes_sub_cmds );
	case iFontWeights:
		return ListChoices( interp, fg_font_weight_sub_cmds );
	case iFontCharsets:
		return ListChoices( interp, fg_font_charset_sub_cmds );
	case iFontPitchValues:
		return ListChoices( interp, fg_font_pitch_sub_cmds );
	case iFontFamilies:
		return ListChoices( interp, fg_font_family_sub_cmds );
	case iFonts:
		return ListFonts( interp, pdlg.hDC, 0 );
	case iFontNames:
		return ListFonts( interp, pdlg.hDC, 1 );
	case iFontUnicodeRanges:
		return ListFontUnicodeRanges( interp, pdlg.hDC);
	default:
		Res = RET_ERROR_PARAMETER;
		break;
	}

	/* Unlock pDevMode. */
	if ( NULL != pDevMode )
		GlobalUnlock( pdlg.hDevMode );

	return Res;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintSetAttr--
 *
 *    Set the printer attributes.
 *
 * Results:
 *    Returns the printer attributes.
 *
 * -------------------------------------------------------------------------
 */

char PrintSetAttr(Tcl_Interp *interp, int Index, Tcl_Obj *oParam)
{
	switch ( Index )
	{
	case iMapMode:
		{
			int IndexMapMode;
			if (RET_ERROR ==
				Tcl_GetIndexFromObj(
					interp, oParam, fg_map_modes_sub_cmds,
					"setmapmode", 1, &IndexMapMode))
			{
				return RET_ERROR;
			}
			return PrintSetMapMode( fg_map_modes_i_command[IndexMapMode] );
		}
	default:
		return RET_ERROR_PARAMETER;
	}
}

/*
 * --------------------------------------------------------------------------
 *
 * LoadDefaultPrinter--
 *
 *    Loads the default printer in the pdlg structure.
 *
 * Results:
 *   Loads the default printer.
 *
 * -------------------------------------------------------------------------
 */

char LoadDefaultPrinter( )
{
	PrintReset( 1 );
	pdlg.Flags = PD_RETURNDEFAULT ;
	if ( PrintDlg( &pdlg ) == FALSE)
		return RET_ERROR_PRINTER_IO;
	if (  pdlg.hDevNames == NULL)
		return RET_ERROR_PRINTER_IO;
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * DefaultPrinterGet--
 *
 *    Gets the default printer in the pdlg structure.
 *
 * Results:
 *    Returns the default printer.
 *
 * -------------------------------------------------------------------------
 */


char DefaultPrinterGet( Tcl_Interp *interp )
{
	char Res;
	Res = LoadDefaultPrinter();
	if ( Res == RET_OK )
		Res = GetDeviceName( interp, pdlg.hDevNames, FALSE );
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * ListPrinters--
 *
 *   Lists all available printers on the system.
 *
 * Results:
 *    Returns the printer list.
 *
 * -------------------------------------------------------------------------
 */


char ListPrinters(Tcl_Interp *interp)
{
	DWORD dwSize = 0;
	DWORD dwPrinters = 0;
	PRINTER_INFO_5* pInfo;
	char Res;

	/* Initialise result value. */
	Res = RET_OK;

	/* Find required buffer size. */
	if (! EnumPrinters(PRINTER_ENUM_LOCAL|PRINTER_ENUM_CONNECTIONS,
		   NULL, 5, NULL, 0, &dwSize, &dwPrinters))
	{
		/*
		 * Check for ERROR_INSUFFICIENT_BUFFER.
		 * If something else, then quit.
		 */
		if ( GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		{
			/* No printer. */
			return RET_ERROR_PRINTER_IO;
		}
		/* Fall through */
	}

	/* Allocate the buffer memory */
	pInfo = (PRINTER_INFO_5 *) GlobalAlloc(GMEM_FIXED, dwSize);
	if (pInfo == NULL)
	{
		/* Out of memory */
		return RET_ERROR_MEMORY;
	}

	/*
	 * Fill the buffer. Again,
	 * this depends on the O/S.
	  */
	if (EnumPrinters(PRINTER_ENUM_LOCAL|PRINTER_ENUM_CONNECTIONS,
		  NULL, 5, (unsigned char *)pInfo, dwSize, &dwSize, &dwPrinters))
	{
	    /* We have got the list of printers. */
		DWORD PrinterCur;
		Tcl_Obj	*lPrinter;

		/* Initialise return list.*/
		lPrinter = Tcl_GetObjResult( interp );

		/* Loop adding the printers to the list. */
		for ( PrinterCur = 0; PrinterCur < dwPrinters; PrinterCur++, pInfo++)
		{
			Tcl_DString	Printer;
			Tcl_Obj *PrinterObj;
			Tcl_DStringInit( &Printer );
			Tcl_WinTCharToUtf(pInfo->pPrinterName, -1, &Printer);
			PrinterObj = Tcl_NewStringObj(
				Tcl_DStringValue( &Printer ),
				Tcl_DStringLength( &Printer ) );
			Tcl_DStringFree( &Printer );
			if ( RET_OK != Tcl_ListObjAppendElement( interp, lPrinter, PrinterObj ))
			{
				/* Error already set in interp. */
				Res = RET_ERROR;
				break;
			}
		}
	} else {
		/* Error - unlikely though as first call to EnumPrinters succeeded! */
		return RET_ERROR_PRINTER_IO;
	}

	GlobalFree( pInfo );

	return Res;
}

/*
 * --------------------------------------------------------------------------
 *
 * ListChoices--
 *
 *   Presents a list of printer choices.
 *
 * Results:
 *    Returns the printer choices.
 *
 * -------------------------------------------------------------------------
 */


char ListChoices(Tcl_Interp *interp, const char *ppChoiceList[])
{
	int Index;
	Tcl_Obj	*lResult;

	/* Initialise return list. */
	lResult = Tcl_GetObjResult( interp );

	/* Loop adding the printers to the list */
	for ( Index = 0; ppChoiceList[Index] != NULL; Index++)
	{
		Tcl_Obj	*ChoiceText;
		ChoiceText = Tcl_NewStringObj( ppChoiceList[Index], -1 );
		if ( RET_OK != Tcl_ListObjAppendElement( interp, lResult, ChoiceText))
		{
			/* Error already set in interp. */
			return RET_ERROR;
		}
	}
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * ListFonts--
 *
 *   List fonts on system.
 *
 * Results:
 *    Returns the font list.
 *
 * -------------------------------------------------------------------------
 */

char ListFonts(Tcl_Interp *interp, HDC hDC, int fFontNameOnly)
{

/* This function is used by getattr fonts and getattr fontnamestyle.
 * getattr fonts: lParam is passed as 0 to EnumFontFamExProc.
 * getattr fontnames: lParam is passed with an initialized last fontname
 * to EnumFontFamExProc.
 * This value is used to check for duplicate listed font names.
 */
	LOGFONT LogFont;
	TCHAR *pCompareFont;

	/* Initialise LogFont */
	LogFont.lfCharSet		= DEFAULT_CHARSET;
	LogFont.lfPitchAndFamily	= 0;
	LogFont.lfFaceName[0]	= '\0';

	/*> Save interpreter ptr in global variable to use it for automatic */
	/*> error feedback. */
	fg_interp = interp;
	if (fFontNameOnly) {
		pCompareFont = _alloca(sizeof(TCHAR) * LF_FULLFACESIZE);
		pCompareFont[0] = 0;
	} else {
		pCompareFont = 0;
	}

	/* Initialise return list */
	if ( EnumFontFamiliesEx(
		hDC,
		&LogFont,
		(FONTENUMPROC) EnumFontFamExProc, /* callback function */
		(LPARAM) pCompareFont,
		0
	) )
		return RET_OK;
	else
		return RET_ERROR;
}

/*
 * --------------------------------------------------------------------------
 *
 * EnumFontFamExProc --
 *
 *   Enumerate font families and styles.
 *
 * Results:
 *    Returns font families and styles.
 *
 * -------------------------------------------------------------------------
 */

int CALLBACK EnumFontFamExProc(
	ENUMLOGFONTEX *lpelfe,    /* logical-font data */
	TCL_UNUSED(NEWTEXTMETRICEX *),  /* physical-font data */
	TCL_UNUSED(DWORD),           /* type of font */
	LPARAM lParam             /* application-defined data */
)
{

/*
 * This function is used by getattr fonts and getattr fontnamestyle.
 *
 * getattr fonts: the font attributes name, style, charset and normal/fixed are
 * added. In this case, the parameter lParam is 0.
 *
 * getattr fontnamestyle: it is checked if the current font has different name
 * or style as the last font. If yes, name and style is added.
 * If not, nothing is added. In this case, the parameter lParam contains a pointer
 * to a ENUMLOGFONTEX variable. On a change, the current content is copied into
 * that variable for the next comparison round.
 */
	Tcl_Obj *AppendObj;
	Tcl_Obj *pResultObj;
	Tcl_DString	dStr;

	if (lParam != 0) {
		TCHAR *pCompareFont = (TCHAR *)lParam;
		if ( 0 == _tcscmp(pCompareFont, lpelfe->elfFullName) ) {
			return TRUE;
		} else {
			_tcscpy( pCompareFont, lpelfe->elfFullName );
		}
	}

	pResultObj = Tcl_GetObjResult(fg_interp);

	/*> Add font name */
	Tcl_DStringInit(& dStr);
	Tcl_WinTCharToUtf(lpelfe->elfFullName,-1, &dStr);
	AppendObj = Tcl_NewStringObj(Tcl_DStringValue(&dStr),-1);
	Tcl_DStringFree(& dStr);
	if (RET_OK != Tcl_ListObjAppendElement(fg_interp, pResultObj, AppendObj))
		return FALSE;

	/*> For getattr fontnames, end here */
	if (lParam != 0) {
		return TRUE;
	}

	/*
	 * Transform style to weight.
	 *
	 * Style may have other words like condensed etc, so map all unknown weights
	 * to "Normal".
	 */

	if (	0 == _tcscmp(lpelfe->elfStyle, TEXT("Thin"))
			|| 0 == _tcscmp(lpelfe->elfStyle, TEXT("Light"))
			|| 0 == _tcscmp(lpelfe->elfStyle, TEXT("Medium"))
			|| 0 == _tcscmp(lpelfe->elfStyle, TEXT("Bold")) )
	{
		Tcl_DStringInit(& dStr);
		Tcl_WinTCharToUtf(lpelfe->elfStyle,-1, &dStr);
		AppendObj = Tcl_NewStringObj(Tcl_DStringValue(&dStr),-1);
		Tcl_DStringFree(& dStr);
	} else if ( 0 == _tcscmp(lpelfe->elfStyle, TEXT("Extralight"))
			|| 0 == _tcscmp(lpelfe->elfStyle, TEXT("Ultralight")) ) {
		AppendObj = Tcl_NewStringObj("Extralight",-1);
	} else if ( 0 == _tcscmp(lpelfe->elfStyle, TEXT("Semibold"))
			|| 0 == _tcscmp(lpelfe->elfStyle, TEXT("Demibold")) ) {
		AppendObj = Tcl_NewStringObj("Semibold",-1);
	} else if ( 0 == _tcscmp(lpelfe->elfStyle, TEXT("Extrabold"))
			|| 0 == _tcscmp(lpelfe->elfStyle, TEXT("Ultrabold")) ) {
		AppendObj = Tcl_NewStringObj("Extrabold",-1);
	} else if ( 0 == _tcscmp(lpelfe->elfStyle, TEXT("Heavy"))
			|| 0 == _tcscmp(lpelfe->elfStyle, TEXT("Black")) ) {
		AppendObj = Tcl_NewStringObj("Heavy",-1);
	} else {
		AppendObj = Tcl_NewStringObj("Normal",-1);
	}
	if (RET_OK != Tcl_ListObjAppendElement(fg_interp, pResultObj, AppendObj))
		return FALSE;

	/* Add script. */
	Tcl_DStringInit(& dStr);
	Tcl_WinTCharToUtf(lpelfe->elfScript,-1, &dStr);
	AppendObj = Tcl_NewStringObj(Tcl_DStringValue(&dStr),-1);
	Tcl_DStringFree(& dStr);
	if (RET_OK != Tcl_ListObjAppendElement(fg_interp, pResultObj, AppendObj))
		return FALSE;

	/* Pitch. */
	switch ( (lpelfe->elfLogFont.lfPitchAndFamily) & 0xf )
	{
	case FIXED_PITCH:
		AppendObj = Tcl_NewStringObj("fixed",-1);
		break;
	default:
		AppendObj = Tcl_NewStringObj("variable",-1);
		break;
	}
	if (RET_OK != Tcl_ListObjAppendElement(fg_interp, pResultObj, AppendObj))
		return FALSE;

	/* Continue enumeration. */
	return TRUE;
}

/*
 * --------------------------------------------------------------------------
 *
 * ListFontUnicodeRanges --
 *
 *   Get the unicode ranges of the current font.
 *
 * Results:
 *    Returns unicode range.
 *
 * -------------------------------------------------------------------------
 */

char ListFontUnicodeRanges(Tcl_Interp *interp, HDC hDC)
{
	size_t StructSize;
	LPGLYPHSET pGlyphSet;
	int PosCur;
	Tcl_Obj	*oList;

	/* Get structure size. */
	StructSize = GetFontUnicodeRanges(hDC,NULL);
	if (StructSize == 0) {
		return RET_ERROR_PRINTER_IO;
	}
	/* Alloc return memory on the stack */
	pGlyphSet = _alloca(StructSize);

	/* Get glyph set structure */
	if (0 == GetFontUnicodeRanges(hDC,pGlyphSet)) {
		return RET_ERROR_PRINTER_IO;
	}

	/* Prepare result list. */
	oList = Tcl_NewListObj(0,NULL);

	for (PosCur = 0 ; PosCur < (int)(pGlyphSet->cRanges) ; PosCur++) {
		/* Starting glyph */
		if (RET_OK != Tcl_ListObjAppendElement(interp, oList,
			Tcl_NewWideIntObj(pGlyphSet->ranges[PosCur].wcLow))) {
			return RET_ERROR;
		}
		/* Length of range */
		if (RET_OK != Tcl_ListObjAppendElement(interp, oList,
			Tcl_NewWideIntObj(pGlyphSet->ranges[PosCur].cGlyphs))) {
			return RET_ERROR;
		}
	}

	Tcl_SetObjResult(interp,oList);
	return RET_OK;
}


/*
 * --------------------------------------------------------------------------
 *
 * GetFirstTextNoChar --
 *
 *   Get data on glyph structure.
 *
 * Results:
 *    Returns glyph structure.
 *
 * -------------------------------------------------------------------------
 */

char GetFirstTextNoChar(Tcl_Interp *interp, TCHAR *pText)
{
	size_t StructSize;
	LPGLYPHSET pGlyphSet;
	int PosCur;
	int IndexCur;
	Tcl_Obj	*oList;

	/* Get structure size. */
	StructSize = GetFontUnicodeRanges(pdlg.hDC,NULL);
	if (StructSize == 0) {
		return RET_ERROR_PRINTER_IO;
	}
	/* Alloc return memory on the stack. */
	pGlyphSet = _alloca(StructSize);

	/* Get glyph set structure. */
	if (0 == GetFontUnicodeRanges(pdlg.hDC,pGlyphSet)) {
		return RET_ERROR_PRINTER_IO;
	}

	/* Prepare result list. */
	oList = Tcl_NewListObj(0,NULL);

	/*> Loop over characters. */
	for (IndexCur = 0;;IndexCur++) {
		int fFound = 0;
		/*> Check for end of string */
		if (pText[IndexCur] == 0) {
			break;
		}
		/* Loop over glyph ranges. */
		for (PosCur = 0 ; PosCur < (int)(pGlyphSet->cRanges) ; PosCur++) {
			if ( pText[IndexCur] >= pGlyphSet->ranges[PosCur].wcLow
					&& pText[IndexCur] < pGlyphSet->ranges[PosCur].wcLow
						+ pGlyphSet->ranges[PosCur].cGlyphs )
			{
				/* Glyph found. */
				fFound = 1;
				break;
			}
		}
		if (!fFound) {
			Tcl_SetObjResult(interp,Tcl_NewWideIntObj(IndexCur));
			return RET_OK;
		}
	}

	Tcl_SetObjResult(interp,Tcl_NewWideIntObj(-1));
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintSetMapMode --
 *
 *   Set the map mode for the printer.
 *
 * Results:
 *    Returns the map mode.
 *
 * -------------------------------------------------------------------------
 */

char PrintSetMapMode( int MapMode )
{
	/* Check for open printer when hDC is required. */
	if (pdlg.hDC == NULL)
		return RET_ERROR_PRINTER_NOT_OPEN;
	if ( 0 == SetMapMode( pdlg.hDC, MapMode ) )
	{
		return RET_ERROR_PRINTER_IO;
	}
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintPen --
 *
 *   Set the pen for rendering lines.
 *
 * Results:
 *    Returns the pen.
 *
 * -------------------------------------------------------------------------
 */

char PrintPen(int Width, COLORREF Color)
{
	if (hPen != NULL)
		DeleteObject(hPen);
	if (Width == 0) {
		/* Solid Pen */
		hPen = CreatePen(PS_NULL, 1, 0);
	} else {
		/* Solid pen. */
		LOGBRUSH lb;
		lb.lbStyle = BS_SOLID;
        lb.lbColor = Color;
        lb.lbHatch = 0;
		hPen = ExtCreatePen(PS_GEOMETRIC|PS_SOLID|PS_ENDCAP_SQUARE|PS_JOIN_MITER
			, Width, &lb, 0, NULL);
	}
	if (NULL == hPen || NULL == SelectObject(pdlg.hDC, hPen) )
		return RET_ERROR_PRINTER_IO;
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintBrushColor --
 *
 *   Set the brush color for the printer.
 *
 * Results:
 *    Returns the brush color.
 *
 * -------------------------------------------------------------------------
 */

char PrintBrushColor(COLORREF Color)
{
	if (CLR_INVALID == SetDCBrushColor(pdlg.hDC, Color) )
		return RET_ERROR_PRINTER_IO;
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintBkColor --
 *
 *   Set the background color for the printer.
 *
 * Results:
 *    Returns the background color.
 *
 * -------------------------------------------------------------------------
 */

char PrintBkColor(COLORREF Color)
{
	if (CLR_INVALID == SetBkColor(pdlg.hDC, Color) )
		return RET_ERROR_PRINTER_IO;
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintRuler --
 *
 *   Set the ruler for the printer.
 *
 * Results:
 *    Returns the ruler.
 *
 * -------------------------------------------------------------------------
 */

char PrintRuler(int X0, int Y0, int LenX, int LenY)
{
	POINT pt[2];
	pt[0].x = X0;
	pt[0].y = Y0;
	pt[1].x = X0+LenX;
	pt[1].y = Y0+LenY;
	if (FALSE == Polyline(pdlg.hDC, pt, 2))
		return RET_ERROR_PRINTER_IO;
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintRectangle --
 *
 *   Set the print rectangle.
 *
 * Results:
 *    Returns the print rectangle.
 *
 * -------------------------------------------------------------------------
 */

char PrintRectangle(int X0, int Y0, int X1, int Y1)
{
	if (FALSE == Rectangle(pdlg.hDC, X0,Y0,X1,Y1))
		return RET_ERROR_PRINTER_IO;
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintFontCreate --
 *
 *   Set the print font.
 *
 * Results:
 *    Returns the print font.
 *
 * -------------------------------------------------------------------------
 */

char PrintFontCreate(int FontNumber,
	TCHAR *Name, double dPointSize, int Weight, int Italic, int Charset,
	int Pitch, int Family)
{

/*
 * Charset:
 * ANSI 0
 * DEFAULT_ 1
 * GREEK_ 161 (0xA1)
 * Italic
 * 	0	No
 * 	1	Yes
 * Pitch
 * 	0	Default
 * 	1	Fixed
 * 	2	Variable
 * Family
 * 	0	FF_DONTCARE
 * 	1	FF_ROMAN	Variable stroke width, serifed. Times Roman, Century Schoolbook, etc.
 * 	2	FF_SWISS	Variable stroke width, sans-serifed. Helvetica, Swiss, etc.
 * 	3	FF_MODERN	Constant stroke width, serifed or sans-serifed. Pica, Elite, Courier, etc.
 * 	4	FF_SCRIPT	Cursive, etc.
 * 	5	FF_DECORATIVE	Old English, etc.
 */

	POINT	pt;	/* To convert to logical scale. */
	LOGFONT lf;

	if (FontNumber < 0 || FontNumber > 9)
		return RET_ERROR_PARAMETER;
	if (hFont[FontNumber] != NULL)
	{
		if (SelectedFont == FontNumber)
		{
			SelectObject(pdlg.hDC, GetStockObject(SYSTEM_FONT));
		}
		DeleteObject (hFont[FontNumber]);
	}

	/* Convert decipoints to the logical device points. */
	pt.x = 0;
	pt.y = (int) (dPointSize * GetDeviceCaps(pdlg.hDC, LOGPIXELSY) / 72.0);
	DPtoLP (pdlg.hDC, &pt, 1);

	lf.lfHeight			= - abs(pt.y);
	lf.lfWidth			= 0;
	lf.lfEscapement		= 0;
	lf.lfOrientation	= 0;
	lf.lfWeight			= Weight;
	lf.lfItalic			= (unsigned char) Italic;
	lf.lfUnderline		= 0;
	lf.lfStrikeOut		= 0;
	lf.lfCharSet		= (unsigned char) Charset;
	lf.lfOutPrecision	= OUT_DEVICE_PRECIS;
	lf.lfClipPrecision	= 0;
	lf.lfQuality		= DEFAULT_QUALITY;
	lf.lfPitchAndFamily	= (unsigned char) (Pitch + (Family<<4));
	_tccpy(lf.lfFaceName, Name);

	hFont[FontNumber] = CreateFontIndirect(&lf);
	if (NULL == hFont[FontNumber])
		return RET_ERROR_PRINTER_IO;
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintFontCreate --
 *
 *   Set the print font.
 *
 * Results:
 *    Returns the print font.
 *
 * -------------------------------------------------------------------------
 */
char PrintFontSelect(int FontNumber)
{
	if (FontNumber < 0 || FontNumber > 9 || hFont[FontNumber] == NULL)
		return RET_ERROR_PARAMETER;

	if (NULL == SelectObject (pdlg.hDC, hFont[FontNumber]))
		return RET_ERROR_PRINTER_IO;

	SelectedFont = FontNumber;
	return RET_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * PrintText --
 *
 *   Prints a page of text.
 *
 * Results:
 *    Returns the printed text.
 *
 * -------------------------------------------------------------------------
 */

char PrintText(int X0, int Y0, TCHAR *pText, COLORREF Color )
{
	if (CLR_INVALID == SetTextColor(pdlg.hDC, Color ) )
		return RET_ERROR_PRINTER_IO;

	if (FALSE == ExtTextOut(pdlg.hDC, X0, Y0,
			0,						/* Options */
			NULL,					/* Clipping rectangle */
			pText, _tcslen(pText),	/* Text and length */
			NULL ) )				/* Distance array */
	{
		return RET_ERROR_PRINTER_IO;
	}
	return RET_OK;
}


/*
 * --------------------------------------------------------------------------
 *
 * PrintGetTextSize --
 *
 *   Gets the text size.
 *
 * Results:
 *    Returns the text side.
 *
 * -------------------------------------------------------------------------
 */

char PrintGetTextSize( Tcl_Interp *interp, TCHAR *pText )
{
	SIZE Size;

	int Res = RET_OK;
	Tcl_Obj	*lResult;
	Tcl_Obj *IntObj;

	if ( FALSE == GetTextExtentPoint32(
		pdlg.hDC,
		pText, _tcslen(pText),
		&Size ) )
	{
		return RET_ERROR_PRINTER_IO;
	}

	/*
	 * We have got the size values.
	 * Initialise return list.
	*/
	lResult = Tcl_GetObjResult( interp );

	/* X Size */
	IntObj = Tcl_NewWideIntObj( Size.cx );
	if ( RET_OK != Tcl_ListObjAppendElement( interp, lResult, IntObj ))
	{
		/* Error already set in interp. */
		Res = RET_ERROR;
	}

	/* Y Size */
	IntObj = Tcl_NewWideIntObj( Size.cy );
	if ( RET_OK != Tcl_ListObjAppendElement( interp, lResult, IntObj ))
	{
		/* Error already set in interp */
		Res = RET_ERROR;
	}
	return Res;
}

/* Paint a photo image to the printer DC */
/* @param interp tcl interpreter */
/* @param oImageName tcl object with tk imsge name */
/* @param DestPosX Destination X position */
/* @param DestPosY Destination Y position */
/* @param DestWidth Width of destination image, or 0 to use original size */
/* @param DestHeight Height of destination image or 0 to use original size */
char PaintPhoto(
    Tcl_Interp *interp,
    Tcl_Obj *const oImageName,
	int DestPosX,
    int DestPosY,
    int DestWidth,
    int DestHeight)
{
#if 0
	Tk_PhotoImageBlock sImageBlock;
	Tk_PhotoHandle hPhoto;
	HBITMAP hDIB;
	int IndexCur;
	/* Access bgraPixel as void ptr or unsigned char ptr */
	union {unsigned char *ptr; void *voidPtr;} bgraPixel;
	BITMAPINFO bmInfo;

	if (pdlg.hDC == NULL)
		return RET_ERROR_PRINTER_NOT_OPEN;

	/* The creation of the DIP is from */
	/* tk8.6.9 win/tkWinWm.c, proc WmIconphotoCmd */
	if ( NULL == (hPhoto = Tk_FindPhoto(interp, Tcl_GetString(oImageName)))) {
		return RET_ERROR;
	}
	Tk_PhotoGetImage(hPhoto, &sImageBlock);
	/* pixelSize = 4 */
	/* pitch = width * 4 */
	/* offset = 0:0,1:1,2:2,3:3 */

	/* Create device-independant color bitmap. */
	ZeroMemory(&bmInfo, sizeof bmInfo);
	bmInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmInfo.bmiHeader.biWidth = sImageBlock.width;
	bmInfo.bmiHeader.biHeight = -sImageBlock.height;
	bmInfo.bmiHeader.biPlanes = 1;
	bmInfo.bmiHeader.biBitCount = 32;
	bmInfo.bmiHeader.biCompression = BI_RGB;

	/* the first parameter is the dc, which may be 0. */
	/* no difference to specify it */
	hDIB = CreateDIBSection(NULL, &bmInfo, DIB_RGB_COLORS,
		&bgraPixel.voidPtr, NULL, 0);
	if (!hDIB) {
		return RET_ERROR_MEMORY;
	}
	/* Convert the photo image data into BGRA format (RGBQUAD). */
	for (IndexCur = 0 ;
		IndexCur < sImageBlock.height * sImageBlock.width * 4 ;
		IndexCur += 4)
	{
		bgraPixel.ptr[IndexCur] = sImageBlock.pixelPtr[IndexCur+2];
		bgraPixel.ptr[IndexCur+1] = sImageBlock.pixelPtr[IndexCur+1];
		bgraPixel.ptr[IndexCur+2] = sImageBlock.pixelPtr[IndexCur+0];
		bgraPixel.ptr[IndexCur+3] = sImageBlock.pixelPtr[IndexCur+3];
	}
	/* Use original width and height if not given. */
	if (DestWidth == 0) { DestWidth = sImageBlock.width; }
	if (DestHeight == 0) { DestHeight = sImageBlock.height; }
	/* Use StretchDIBits with full image. */
	/* The printer driver may use additional color info to do better */
	/* interpolation */
	if (GDI_ERROR == StretchDIBits(
		pdlg.hDC,				/* handle to DC */
		DestPosX,				/* x-coord of destination upper-left corner */
		DestPosY,				/* y-coord of destination upper-left corner */
		DestWidth,				/* width of destination rectangle */
		DestHeight,				/* height of destination rectangle */
		0,						/* x-coord of source upper-left corner */
		0,						/* y-coord of source upper-left corner */
		sImageBlock.width,		/* width of source rectangle */
		sImageBlock.height,		/* height of source rectangle */
		bgraPixel.voidPtr,		/* bitmap bits */
		&bmInfo,				/* bitmap data */
		DIB_RGB_COLORS,			/* usage options */
		SRCCOPY					/* raster operation code */
		) )
	{
		DeleteObject(hDIB);
		/* As this is invoked within the driver, return a driver error */
		return RET_ERROR_PRINTER_DRIVER;
	}
	DeleteObject(hDIB);
#else
    (void)interp;
    (void)oImageName;
    (void)DestPosX;
    (void)DestPosY;
    (void)DestWidth;
    (void)DestHeight;
#endif
	return RET_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */

