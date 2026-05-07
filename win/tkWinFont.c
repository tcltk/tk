/*
 * tkWinFont.c --
 *
 *	Contains the Windows implementation of the platform-independent font
 *	package interface with support for shaping and RTL support with complex
 *  	script languages like Arabic.
 *
 * Copyright © 1994 Software Research Associates, Inc.
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 1998-1999 Scriptics Corporation.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkWinInt.h"
#include "tkFont.h"
#include <usp10.h>      /* Uniscribe */

/*
 * The following structure represents a font family. It is assumed that all
 * screen fonts constructed from the same "font family" share certain
 * properties; all screen fonts with the same "font family" point to a shared
 * instance of this structure. The most important shared property is the
 * character existence metrics, used to determine if a screen font can display
 * a given Unicode character.
 *
 * Under Windows, a "font family" is uniquely identified by its face name.
 */

#define FONTMAP_SHIFT	    10

#define FONTMAP_BITSPERPAGE	(1 << FONTMAP_SHIFT)
/* Cover the full Unicode range (0x110000) */
#define FONTMAP_NUMCHARS	0x110000
#define FONTMAP_PAGES		(FONTMAP_NUMCHARS / FONTMAP_BITSPERPAGE)

typedef struct FontFamily {
    struct FontFamily *nextPtr;	/* Next in list of all known font families. */
    size_t refCount;		/* How many SubFonts are referring to this
				 * FontFamily. When the refCount drops to
				 * zero, this FontFamily may be freed. */
    /*
     * Key.
     */

    Tk_Uid faceName;		/* Face name key for this FontFamily. */

    /*
     * Derived properties.
     */

    Tcl_Encoding encoding;	/* Encoding for this font family. */
    int isSymbolFont;		/* Non-zero if this is a symbol font. */
    int isWideFont;		/* 1 if this is a double-byte font, 0
				 * otherwise. */
    BOOL (WINAPI *textOutProc)(HDC hdc, int x, int y, WCHAR *str, int len);
				/* The procedure to use to draw text after it
				 * has been converted from UTF-8 to the
				 * encoding of this font. */
    BOOL (WINAPI *getTextExtentPoint32Proc)(HDC, WCHAR *, int, LPSIZE);
				/* The procedure to use to measure text after
				 * it has been converted from UTF-8 to the
				 * encoding of this font. */

    char *fontMap[FONTMAP_PAGES];
				/* Two-level sparse table used to determine
				 * quickly if the specified character exists.
				 * As characters are encountered, more pages
				 * in this table are dynamically added. The
				 * contents of each page is a bitmask
				 * consisting of FONTMAP_BITSPERPAGE bits,
				 * representing whether this font can be used
				 * to display the given character at the
				 * corresponding bit position. The high bits
				 * of the character are used to pick which
				 * page of the table is used. */

    /*
     * Cached Truetype font info.
     */

    int segCount;		/* The length of the following arrays. */
    USHORT *startCount;		/* Truetype information about the font, */
    USHORT *endCount;		/* indicating which characters this font can
				 * display (malloced). The format of this
				 * information is (relatively) compact, but
				 * would take longer to search than indexing
				 * into the fontMap[][] table. */

    /*
     * cmap format-12 groups: cover supplementary-plane characters
     * (U+10000 and above) including color emoji.  Both arrays are
     * parallel and have groupCount entries, malloced.
     */
    int    groupCount;		/* Number of format-12 groups, or 0. */
    ULONG *startGroup;		/* Inclusive start codepoint of each group. */
    ULONG *endGroup;		/* Inclusive end codepoint of each group. */
} FontFamily;

/*
 * The following structure encapsulates an individual screen font. A font
 * object is made up of however many SubFonts are necessary to display a
 * stream of multilingual characters.
 */

typedef struct SubFont {
    char **fontMap;		/* Pointer to font map from the FontFamily,
				 * cached here to save a dereference. */
    HFONT hFont0;		/* The specific screen font that will be used
				 * when displaying/measuring chars belonging
				 * to the FontFamily. */
    FontFamily *familyPtr;	/* The FontFamily for this SubFont. */
    HFONT hFontAngled;
    double angle;
} SubFont;

/*
 * The following structure represents Windows' implementation of a font
 * object.
 */

#define SUBFONT_SPACE		3
#define BASE_CHARS		128

typedef struct WinFont {
    TkFont font;		/* Stuff used by generic font package. Must be
				 * first in structure. */
    SubFont staticSubFonts[SUBFONT_SPACE];
				/* Builtin space for a limited number of
				 * SubFonts. */
    int numSubFonts;		/* Length of following array. */
    SubFont *subFontArray;	/* Array of SubFonts that have been loaded in
				 * order to draw/measure all the characters
				 * encountered by this font so far. All fonts
				 * start off with one SubFont initialized by
				 * InitFont() from the original set of font
				 * attributes. Usually points to
				 * staticSubFonts, but may point to malloced
				 * space if there are lots of SubFonts. */
    SCRIPT_CACHE staticScriptCaches[SUBFONT_SPACE];
				/* Inline SCRIPT_CACHE storage parallel to
				 * staticSubFonts.  Each slot corresponds to
				 * the SubFont at the same index and is passed
				 * to ScriptShape/ScriptPlace so that Uniscribe
				 * can amortise per-font analysis work across
				 * calls.  Must be zero-initialised; freed with
				 * ScriptFreeCache() in ReleaseFont(). */
    SCRIPT_CACHE *scriptCacheArray;
				/* Points to staticScriptCaches normally, or
				 * to a malloced array when subFontArray
				 * overflows SUBFONT_SPACE.  Always kept in
				 * sync with subFontArray. */
    HWND hwnd;			/* Toplevel window of application that owns
				 * this font, used for getting HDC for
				 * offscreen measurements. */
    int pixelSize;		/* Original pixel size used when font was
				 * constructed. */
    int widths[BASE_CHARS];	/* Widths of first 128 chars in the base font,
				 * for handling common case. The base font is
				 * always used to draw characters between
				 * 0x0000 and 0x007f. */
} WinFont;

/*
 * The following structure is passed as the LPARAM when calling the font
 * enumeration procedure to determine if a font can support the given
 * character.
 */

typedef struct CanUse {
    HDC hdc;
    WinFont *fontPtr;
    Tcl_DString *nameTriedPtr;
    int ch;
    SubFont *subFontPtr;
    SubFont **subFontPtrPtr;
} CanUse;

/*
 * The following structure represents a single fully-shaped, bidi-reordered
 * run ready for drawing or advance-width summation.  The shaping layer
 * produces an array of these; the drawing layer consumes them without any
 * further Unicode processing.
 *
 * Ownership: glyphs, advances, offsets, logClust, and visualX are allocated by
 * TkWinShapeString() and freed by TkWinFreeShapedRuns().
 *
 * The scriptCache field points into the owning WinFont's scriptCacheArray at
 * the slot that was used for ScriptShape and ScriptPlace.  ScriptTextOut
 * MUST receive the same cache pointer; passing NULL causes Uniscribe to
 * discard all shaping work and produce no visible output.  Do not free this
 * pointer here — it is owned by the WinFont.
 */
typedef struct TkWinShapedRun {
    HFONT        hFont;         /* Font selected when ScriptTextOut is called.
				 * Owned by the WinFont subfont; do not delete
				 * here. */
    SubFont     *subFontPtr;    /* The SubFont that owns hFont.  Used by the
				 * draw layer to retrieve the face name for
				 * constructing rotated variants of this run's
				 * font.  Points into fontPtr->subFontArray;
				 * do not free. */
    SCRIPT_CACHE *scriptCache;  /* Points to fontPtr->scriptCacheArray[i] for
				 * the subfont that shaped this run.  Must be
				 * the same slot passed to ScriptShape and
				 * ScriptPlace, and must be passed to
				 * ScriptTextOut.  Never NULL after a
				 * successful shape+place. Do not free here. */
    SCRIPT_ANALYSIS sa;         /* Uniscribe analysis for this run (carries
				 * bidi level, script tag, etc.). */
    int          glyphCount;    /* Number of entries in glyphs[]. */
    WORD        *glyphs;        /* Glyph index array (malloced). */
    int         *advances;      /* Glyph advance widths in pixels (malloced).*/
    GOFFSET     *offsets;       /* Per-glyph x/y offsets (malloced). */
    ABC          abc;           /* Total run A+B+C metrics from ScriptPlace. */
    int          charStart;     /* UTF-16 character index in the full string
				 * where this run begins. */
    int          charLen;       /* Number of UTF-16 characters in this run. */
    WORD        *logClust;      /* Logical cluster map: logClust[i] is the
				 * index of the first glyph for UTF-16 char i
				 * within this run.  Length = charLen.
				 * (malloced). */
    int         *visualX;       /* Length charLen+1: visual X offsets from run
				 * start. visualX[0] = 0, visualX[charLen] =
				 * run width. malloced, freed in
				 * TkWinFreeShapedRuns. */
} TkWinShapedRun;

/*
 * The following structure is used to map between the Tcl strings that
 * represent the system fonts and the numbers used by Windows.
 */

static const TkStateMap systemMap[] = {
    {ANSI_FIXED_FONT,	    "ansifixed"},
    {ANSI_FIXED_FONT,	    "fixed"},
    {ANSI_VAR_FONT,	    "ansi"},
    {DEVICE_DEFAULT_FONT,   "device"},
    {DEFAULT_GUI_FONT,	    "defaultgui"},
    {OEM_FIXED_FONT,	    "oemfixed"},
    {SYSTEM_FIXED_FONT,	    "systemfixed"},
    {SYSTEM_FONT,	    "system"},
    {-1,		    NULL}
};

typedef struct {
    FontFamily *fontFamilyList; /* The list of font families that are
				 * currently loaded. As screen fonts are
				 * loaded, this list grows to hold information
				 * about what characters exist in each font
				 * family. */
    Tcl_HashTable uidTable;
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * Procedures used only in this file.
 */

static FontFamily *	AllocFontFamily(HDC hdc, HFONT hFont, int base);
static SubFont *	CanUseFallback(HDC hdc, WinFont *fontPtr,
			    const char *fallbackName, int ch,
			    SubFont **subFontPtrPtr);
static SubFont *	CanUseFallbackWithAliases(HDC hdc, WinFont *fontPtr,
			    const char *faceName, int ch,
			    Tcl_DString *nameTriedPtr,
			    SubFont **subFontPtrPtr);
static int		FamilyExists(HDC hdc, const char *faceName);
static const char *	FamilyOrAliasExists(HDC hdc, const char *faceName);
static SubFont *	FindSubFontForChar(WinFont *fontPtr, int ch,
			    SubFont **subFontPtrPtr);
static void		FontMapInsert(SubFont *subFontPtr, int ch);
static void		FontMapLoadPage(SubFont *subFontPtr, int row);
static int		FontMapLookup(SubFont *subFontPtr, int ch);
static void		FreeFontFamily(FontFamily *familyPtr);
static HFONT		GetScreenFont(const TkFontAttributes *faPtr,
			    const char *faceName, int pixelSize,
			    double angle);
static void		InitFont(Tk_Window tkwin, HFONT hFont,
			    int overstrike, WinFont *tkFontPtr);
static inline void	InitSubFont(HDC hdc, HFONT hFont, int base,
			    SubFont *subFontPtr);
static int		CreateNamedSystemLogFont(Tcl_Interp *interp,
			    Tk_Window tkwin, const char* name,
			    LOGFONTW* logFontPtr);
static int		CreateNamedSystemFont(Tcl_Interp *interp,
			    Tk_Window tkwin, const char* name, HFONT hFont);
static int		LoadFontRanges(HDC hdc, HFONT hFont,
			    USHORT **startCount, USHORT **endCount,
			    int *symbolPtr,
			    ULONG **startGroup, ULONG **endGroup,
			    int *groupCount);
static void		MultiFontTextOut(HDC hdc, WinFont *fontPtr,
			    const char *source, int numBytes,
			    double x, double y, double angle);
static void		ReleaseFont(WinFont *fontPtr);
static inline void	ReleaseSubFont(SubFont *subFontPtr);
static int		SeenName(const char *name, Tcl_DString *dsPtr);
static inline void	SwapLong(PULONG p);
static inline void	SwapShort(USHORT *p);
static int CALLBACK	WinFontCanUseProc(ENUMLOGFONTW *lfPtr,
			    NEWTEXTMETRIC *tmPtr, int fontType,
			    LPARAM lParam);
static int CALLBACK	WinFontExistProc(ENUMLOGFONTW *lfPtr,
			    NEWTEXTMETRIC *tmPtr, int fontType,
			    LPARAM lParam);
static int CALLBACK	WinFontFamilyEnumProc(ENUMLOGFONTW *lfPtr,
			    NEWTEXTMETRIC *tmPtr, int fontType,
			    LPARAM lParam);

/* Uniscribe shaping layer - declared here, defined before first use site. */
static int		TkWinShapeString(HDC hdc, WinFont *fontPtr,
			    const WCHAR *wstr, int wlen,
			    TkWinShapedRun **runsOut, int *runCountOut);
static void		TkWinFreeShapedRuns(TkWinShapedRun *runs, int nRuns);
static int		TkWinShapedRunsWidth(const TkWinShapedRun *runs,
			    int nRuns);
static int		GetVisualXForLogicalIndex(const TkWinShapedRun *runs,
			    int nRuns, int logicalIdx);
static BYTE             *TkWinComputeClusterBoundaries(const WCHAR *str,
						       int len,
						       const WORD *logClust);
static int              *TkWinComputeClusterWidths(const BYTE
						   *clusterBoundaries,
						   int charLen,
						   const int *advances,
						   int glyphCount,
						   const WORD *logClust);
static int               TkWinCountClusters(const BYTE *clusterBoundaries,
					    int charLen);
void                     TkWinFreeShapedRuns(TkWinShapedRun *runs,
					     int runCount);
int                      TkWinLinebreakAwareMeasure(Tk_Font tkfont,
						    const char *source,
						    Tcl_Size numBytes,
						    Tcl_Size rangeStart,
						    Tcl_Size rangeLength,
						    int maxLength,
						    int *lengthPtr);

/*
 *-------------------------------------------------------------------------
 *
 * TkpFontPkgInit --
 *
 *	This procedure is called when an application is created. It
 *	initializes all the structures that are used by the platform-dependent
 *	code on a per application basis.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *
 *	None.
 *
 *-------------------------------------------------------------------------
 */

void
TkpFontPkgInit(
    TkMainInfo *mainPtr)	/* The application being created. */
{
    TkWinSetupSystemFonts(mainPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetNativeFont --
 *
 *	Map a platform-specific native font name to a TkFont.
 *
 * Results:
 *	The return value is a pointer to a TkFont that represents the native
 *	font. If a native font by the given name could not be found, the
 *	return value is NULL.
 *
 *	Every call to this procedure returns a new TkFont structure, even if
 *	the name has already been seen before. The caller should call
 *	TkpDeleteFont() when the font is no longer needed.
 *
 *	The caller is responsible for initializing the memory associated with
 *	the generic TkFont when this function returns and releasing the
 *	contents of the generic TkFont before calling TkpDeleteFont().
 *
 * Side effects:
 *	Memory allocated.
 *
 *---------------------------------------------------------------------------
 */

TkFont *
TkpGetNativeFont(
    Tk_Window tkwin,		/* For display where font will be used. */
    const char *name)		/* Platform-specific font name. */
{
    int object;
    WinFont *fontPtr;

    object = TkFindStateNum(NULL, NULL, systemMap, name);
    if (object < 0) {
	return NULL;
    }

    tkwin = (Tk_Window) ((TkWindow *) tkwin)->mainPtr->winPtr;
    fontPtr = (WinFont *)Tcl_Alloc(sizeof(WinFont));
    InitFont(tkwin, (HFONT)GetStockObject(object), 0, fontPtr);

    return (TkFont *) fontPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * CreateNamedSystemFont --
 *
 *	This function registers a Windows logical font description with the Tk
 *	named font mechanism.
 *
 * Side effects:
 *	A new named font is added to the Tk font registry.
 *
 *---------------------------------------------------------------------------
 */

static int
CreateNamedSystemLogFont(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    const char* name,
    LOGFONTW* logFontPtr)
{
    HFONT hFont;
    int r;

    hFont = CreateFontIndirectW(logFontPtr);
    r = CreateNamedSystemFont(interp, tkwin, name, hFont);
    DeleteObject((HGDIOBJ)hFont);
    return r;
}

/*
 *---------------------------------------------------------------------------
 *
 * CreateNamedSystemFont --
 *
 *	This function registers a Windows font with the Tk named font
 *	mechanism.
 *
 * Side effects:
 *	A new named font is added to the Tk font registry.
 *
 *---------------------------------------------------------------------------
 */

static int
CreateNamedSystemFont(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    const char* name,
    HFONT hFont)
{
    WinFont winfont;
    int r;

    TkDeleteNamedFont(NULL, tkwin, name);
    InitFont(tkwin, hFont, 0, &winfont);
    r = TkCreateNamedFont(interp, tkwin, name, &winfont.font.fa);
    TkpDeleteFont((TkFont *)&winfont);
    return r;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWinSystemFonts --
 *
 *	Create some platform specific named fonts that to give access to the
 *	system fonts. These are all defined for the Windows desktop
 *	parameters.
 *
 *---------------------------------------------------------------------------
 */

void
TkWinSetupSystemFonts(
    TkMainInfo *mainPtr)
{
    Tcl_Interp *interp;
    Tk_Window tkwin;
    const TkStateMap *mapPtr;
    NONCLIENTMETRICSW ncMetrics;
    ICONMETRICSW iconMetrics;
    HFONT hFont;

    interp = (Tcl_Interp *) mainPtr->interp;
    tkwin = (Tk_Window) mainPtr->winPtr;

    /* force this for now */
    if (((TkWindow *) tkwin)->mainPtr == NULL) {
	((TkWindow *) tkwin)->mainPtr = mainPtr;
    }

    /*
     * If this API call fails then we will fallback to setting these named
     * fonts from script in ttk/fonts.tcl. So far I've only seen it fail when
     * WINVER has been defined for a higher platform than we are running on.
     * (i.e. WINVER=0x0600 and running on XP).
     */

    memset(&ncMetrics, 0, sizeof(ncMetrics));
    ncMetrics.cbSize = sizeof(ncMetrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,
	    sizeof(ncMetrics), &ncMetrics, 0)) {
	CreateNamedSystemLogFont(interp, tkwin, "TkDefaultFont",
		&ncMetrics.lfMessageFont);
	CreateNamedSystemLogFont(interp, tkwin, "TkHeadingFont",
		&ncMetrics.lfMessageFont);
	CreateNamedSystemLogFont(interp, tkwin, "TkTextFont",
		&ncMetrics.lfMessageFont);
	CreateNamedSystemLogFont(interp, tkwin, "TkMenuFont",
		&ncMetrics.lfMenuFont);
	CreateNamedSystemLogFont(interp, tkwin, "TkTooltipFont",
		&ncMetrics.lfStatusFont);
	CreateNamedSystemLogFont(interp, tkwin, "TkCaptionFont",
		&ncMetrics.lfCaptionFont);
	CreateNamedSystemLogFont(interp, tkwin, "TkSmallCaptionFont",
		&ncMetrics.lfSmCaptionFont);
    }

    iconMetrics.cbSize = sizeof(iconMetrics);
    if (SystemParametersInfoW(SPI_GETICONMETRICS, sizeof(iconMetrics),
	    &iconMetrics, 0)) {
	CreateNamedSystemLogFont(interp, tkwin, "TkIconFont",
		&iconMetrics.lfFont);
    }

    /*
     * Identify an available fixed font. Equivalent to ANSI_FIXED_FONT but
     * more reliable on Russian Windows.
     */

    {
	LOGFONTW lfFixed = {
	    0, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
	    0, 0, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L""
	};
	long pointSize, dpi;
	HDC hdc = GetDC(NULL);
	dpi = GetDeviceCaps(hdc, LOGPIXELSY);
	pointSize = -MulDiv(ncMetrics.lfMessageFont.lfHeight, 72, dpi);
	lfFixed.lfHeight = -MulDiv(pointSize+1, dpi, 72);
	ReleaseDC(NULL, hdc);
	CreateNamedSystemLogFont(interp, tkwin, "TkFixedFont", &lfFixed);
    }

    /*
     * Setup the remaining standard Tk font names as named fonts.
     */

    for (mapPtr = systemMap; mapPtr->strKey != NULL; mapPtr++) {
	hFont = (HFONT) GetStockObject(mapPtr->numKey);
	CreateNamedSystemFont(interp, tkwin, mapPtr->strKey, hFont);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetFontFromAttributes --
 *
 *	Given a desired set of attributes for a font, find a font with the
 *	closest matching attributes.
 *
 * Results:
 *	The return value is a pointer to a TkFont that represents the font
 *	with the desired attributes. If a font with the desired attributes
 *	could not be constructed, some other font will be substituted
 *	automatically. NULL is never returned.
 *
 *	Every call to this procedure returns a new TkFont structure, even if
 *	the specified attributes have already been seen before. The caller
 *	should call TkpDeleteFont() to free the platform- specific data when
 *	the font is no longer needed.
 *
 *	The caller is responsible for initializing the memory associated with
 *	the generic TkFont when this function returns and releasing the
 *	contents of the generic TkFont before calling TkpDeleteFont().
 *
 * Side effects:
 *	Memory allocated.
 *
 *---------------------------------------------------------------------------
 */

TkFont *
TkpGetFontFromAttributes(
    TkFont *tkFontPtr,		/* If non-NULL, store the information in this
				 * existing TkFont structure, rather than
				 * allocating a new structure to hold the
				 * font; the existing contents of the font
				 * will be released. If NULL, a new TkFont
				 * structure is allocated. */
    Tk_Window tkwin,		/* For display where font will be used. */
    const TkFontAttributes *faPtr)
				/* Set of attributes to match. */
{
    int i, j;
    HDC hdc;
    HWND hwnd;
    HFONT hFont;
    Window window;
    WinFont *fontPtr;
    const char *const *const *fontFallbacks;
    Tk_Uid faceName, fallback, actualName;

    tkwin = (Tk_Window) ((TkWindow *) tkwin)->mainPtr->winPtr;
    window = Tk_WindowId(tkwin);
    hwnd = (window == None) ? NULL : TkWinGetHWND(window);
    hdc = GetDC(hwnd);

    /*
     * Algorithm to get the closest font name to the one requested.
     *
     * try fontname
     * try all aliases for fontname
     * foreach fallback for fontname
     *	    try the fallback
     *	    try all aliases for the fallback
     */

    faceName = faPtr->family;
    if (faceName != NULL) {
	actualName = FamilyOrAliasExists(hdc, faceName);
	if (actualName != NULL) {
	    faceName = actualName;
	    goto found;
	}
	fontFallbacks = TkFontGetFallbacks();
	for (i = 0; fontFallbacks[i] != NULL; i++) {
	    for (j = 0; (fallback = fontFallbacks[i][j]) != NULL; j++) {
		if (strcasecmp(faceName, fallback) == 0) {
		    break;
		}
	    }
	    if (fallback != NULL) {
		for (j = 0; (fallback = fontFallbacks[i][j]) != NULL; j++) {
		    actualName = FamilyOrAliasExists(hdc, fallback);
		    if (actualName != NULL) {
			faceName = actualName;
			goto found;
		    }
		}
	    }
	}
    }

  found:
    ReleaseDC(hwnd, hdc);

    hFont = GetScreenFont(faPtr, faceName,
	    (int)(TkFontGetPixels(tkwin, faPtr->size) + 0.5), 0.0);
    if (tkFontPtr == NULL) {
	fontPtr = (WinFont *)Tcl_Alloc(sizeof(WinFont));
    } else {
	fontPtr = (WinFont *) tkFontPtr;
	ReleaseFont(fontPtr);
    }
    InitFont(tkwin, hFont, faPtr->overstrike, fontPtr);

    return (TkFont *) fontPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpDeleteFont --
 *
 *	Called to release a font allocated by TkpGetNativeFont() or
 *	TkpGetFontFromAttributes(). The caller should have already released
 *	the fields of the TkFont that are used exclusively by the generic
 *	TkFont code.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	TkFont is deallocated.
 *
 *---------------------------------------------------------------------------
 */

void
TkpDeleteFont(
    TkFont *tkFontPtr)		/* Token of font to be deleted. */
{
    WinFont *fontPtr;

    fontPtr = (WinFont *) tkFontPtr;
    ReleaseFont(fontPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetFontFamilies, WinFontFamilyEnumProc --
 *
 *	Return information about the font families that are available on the
 *	display of the given window.
 *
 * Results:
 *	Modifies interp's result object to hold a list of all the available
 *	font families.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

void
TkpGetFontFamilies(
    Tcl_Interp *interp,		/* Interp to hold result. */
    Tk_Window tkwin)		/* For display to query. */
{
    HDC hdc;
    HWND hwnd;
    Window window;
    Tcl_Obj *resultObj;

    window = Tk_WindowId(tkwin);
    hwnd = (window == None) ? NULL : TkWinGetHWND(window);
    hdc = GetDC(hwnd);
    resultObj = Tcl_NewObj();

    /*
     * On any version NT, there may fonts with international names. Use the
     * NT-only Unicode version of EnumFontFamilies to get the font names. If
     * we used the ANSI version on a non-internationalized version of NT, we
     * would get font names with '?' replacing all the international
     * characters.
     *
     * On a non-internationalized verson of 95, fonts with international names
     * are not allowed, so the ANSI version of EnumFontFamilies will work. On
     * an internationalized version of 95, there may be fonts with
     * international names; the ANSI version will work, fetching the name in
     * the system code page. Can't use the Unicode version of EnumFontFamilies
     * because it only exists under NT.
     */

    EnumFontFamiliesW(hdc, NULL, (FONTENUMPROCW) WinFontFamilyEnumProc,
	    (LPARAM) resultObj);
    ReleaseDC(hwnd, hdc);
    Tcl_SetObjResult(interp, resultObj);
}

static int CALLBACK
WinFontFamilyEnumProc(
    ENUMLOGFONTW *lfPtr,		/* Logical-font data. */
    TCL_UNUSED(NEWTEXTMETRIC *),	/* Physical-font data (not used). */
    TCL_UNUSED(int),		/* Type of font (not used). */
    LPARAM lParam)		/* Result object to hold result. */
{
    WCHAR *faceName = lfPtr->elfLogFont.lfFaceName;
    Tcl_Obj *resultObj = (Tcl_Obj *) lParam;
    Tcl_DString faceString;

    Tcl_DStringInit(&faceString);
    Tcl_WCharToUtfDString(faceName, wcslen(faceName), &faceString);
    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(
	    Tcl_DStringValue(&faceString), Tcl_DStringLength(&faceString)));
    Tcl_DStringFree(&faceString);
    return 1;
}

/*
 *-------------------------------------------------------------------------
 *
 * TkpGetSubFonts --
 *
 *	A function used by the testing package for querying the actual screen
 *	fonts that make up a font object.
 *
 * Results:
 *	Modifies interp's result object to hold a list containing the names of
 *	the screen fonts that make up the given font object.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

void
TkpGetSubFonts(
    Tcl_Interp *interp,		/* Interp to hold result. */
    Tk_Font tkfont)		/* Font object to query. */
{
    int i;
    WinFont *fontPtr;
    FontFamily *familyPtr;
    Tcl_Obj *resultPtr, *strPtr;

    resultPtr = Tcl_NewObj();
    fontPtr = (WinFont *) tkfont;
    for (i = 0; i < fontPtr->numSubFonts; i++) {
	familyPtr = fontPtr->subFontArray[i].familyPtr;
	strPtr = Tcl_NewStringObj(familyPtr->faceName, TCL_INDEX_NONE);
	Tcl_ListObjAppendElement(NULL, resultPtr, strPtr);
    }
    Tcl_SetObjResult(interp, resultPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetFontAttrsForChar --
 *
 *	Retrieve the font attributes of the actual font used to render a given
 *	character.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The font attributes are stored in *faPtr.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetFontAttrsForChar(
    Tk_Window tkwin,		/* Window on the font's display */
    Tk_Font tkfont,		/* Font to query */
    int c,			/* Character of interest */
    TkFontAttributes *faPtr)	/* Output: Font attributes */
{
    WinFont *fontPtr = (WinFont *) tkfont;
				/* Structure describing the logical font */
    HDC hdc = GetDC(fontPtr->hwnd);
				/* GDI device context */
    SubFont *lastSubFontPtr = &fontPtr->subFontArray[0];
				/* Pointer to subfont array in case
				 * FindSubFontForChar needs to fix up the
				 * memory allocation */
    SubFont *thisSubFontPtr =
	    FindSubFontForChar(fontPtr, c, &lastSubFontPtr);
				/* Pointer to the subfont to use for the given
				 * character */
    FontFamily *familyPtr = thisSubFontPtr->familyPtr;
    HFONT oldfont;		/* Saved font from the device context */
    TEXTMETRICW tm;		/* Font metrics of the selected subfont */

    /*
     * Get the font attributes.
     */

    oldfont = (HFONT)SelectObject(hdc, thisSubFontPtr->hFont0);
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, oldfont);
    ReleaseDC(fontPtr->hwnd, hdc);
    faPtr->family = familyPtr->faceName;
    faPtr->size = TkFontGetPoints(tkwin,
	    (double)(tm.tmInternalLeading - tm.tmHeight));
    faPtr->weight = (tm.tmWeight > FW_MEDIUM) ? TK_FW_BOLD : TK_FW_NORMAL;
    faPtr->slant = tm.tmItalic ? TK_FS_ITALIC : TK_FS_ROMAN;
    faPtr->underline = (tm.tmUnderlined != 0);
    faPtr->overstrike = fontPtr->font.fa.overstrike;
}

/*
 * UNISCRIBE SHAPING LAYER
 *
 * The three functions below form a strict boundary:
 *
 *   TkWinShapeString()        - UTF-8 in, TkWinShapedRun[] out.
 *   TkWinFreeShapedRuns()     - Release memory owned by the run array.
 *   TkWinShapedRunsWidth()    - Sum advance widths across all runs.
 *
 * No code below this boundary may call Tcl_UtfToUniChar, ScriptItemize,
 * ScriptShape, or ScriptPlace.  The drawing and measuring functions receive
 * fully composed, bidi-reordered glyph buffers and call only ScriptTextOut /
 * the advance arrays.
 */

/*
 *---------------------------------------------------------------------------
 *
 * TkWinShapeString --
 *
 *	Convert a UTF-8 string to a bidi-reordered, shaped array of glyph
 *	runs using Uniscribe.
 *
 *	Pipeline:
 *	  1. UTF-8 -> UTF-16 (Tcl_UtfToWCharDString)
 *	  2. ScriptItemize  -- split into script/bidi items
 *	  3. ScriptLayout   -- compute visual order of items
 *	  4. For each item in visual order:
 *	       a. Select the appropriate WinFont subfont (with fallback).
 *	       b. ScriptShape  -- map chars -> glyphs
 *	       c. ScriptPlace  -- compute advance widths and offsets
 *	       d. Compute visualX offsets for logical character boundaries.
 *	  5. Return the run array to the caller.
 *
 *	Subfont fallback: if ScriptShape returns a missing-glyph for any
 *	position we retry with every subfont in fontPtr->subFontArray (and
 *	load new ones via FindSubFontForChar) until a match is found or we
 *	give up and leave the .notdef glyph in place.
 *
 * Results:
 *	Returns the number of runs stored in *runsOut on success, or -1 on a
 *	hard failure (OOM). *runsOut is set to a Tcl_Alloc'd array that the
 *	caller must release with TkWinFreeShapedRuns().
 *
 * Side effects:
 *	May load additional SubFonts into fontPtr->subFontArray.
 *
 *---------------------------------------------------------------------------
 */

static int
TkWinShapeString(
    HDC hdc,
    WinFont *fontPtr,
    const WCHAR *wstr,          /* UTF-16 input string. */
    int wlen,                   /* Length in WCHARs (not bytes). */
    TkWinShapedRun **runsOut,   /* OUT: caller frees with TkWinFreeShapedRuns*/
    int *runCountOut)           /* OUT: number of runs in *runsOut. */
{
    /*
     * ScriptItemize:
     * Ask Uniscribe to split the string into script/bidi items.  We start
     * with a modest stack buffer and re-try with a heap buffer if the string
     * is unusually complex.
     */
#define ITEM_STACK 64
    SCRIPT_ITEM  stackItems[ITEM_STACK + 1];
    SCRIPT_ITEM *items     = stackItems;
    int          itemCount = 0;
    HRESULT      hr;

    hr = ScriptItemize(wstr, wlen, ITEM_STACK, NULL, NULL,
	    items, &itemCount);
    if (hr == E_OUTOFMEMORY) {
	/* String has more than ITEM_STACK items -- allocate on heap. */
	int maxItems = wlen + 1;
	items = (SCRIPT_ITEM *)Tcl_Alloc(sizeof(SCRIPT_ITEM) * (maxItems + 1));
	hr = ScriptItemize(wstr, wlen, maxItems, NULL, NULL,
		items, &itemCount);
    }
    if (FAILED(hr)) {
	if (items != stackItems) {
	Tcl_Free(items);
	}
	*runsOut = NULL;
	*runCountOut = 0;
	return -1;
    }

    /*
     * ScriptLayout:
     * Compute the visual (left-to-right) order of the logical items so we
     * can iterate them in the correct paint order.
     */
    int *visualToLogical = (int *)Tcl_Alloc(sizeof(int) * itemCount);
    {
	BYTE *levels = (BYTE *)Tcl_Alloc(sizeof(BYTE) * itemCount);
	int   i;
	for (i = 0; i < itemCount; i++) {
	    levels[i] = (BYTE)items[i].a.s.uBidiLevel;
	}
	hr = ScriptLayout(itemCount, levels, visualToLogical, NULL);
	Tcl_Free(levels);
	if (FAILED(hr)) {
	    /* Fallback: identity order. */
	    for (i = 0; i < itemCount; i++) visualToLogical[i] = i;
	}
    }

    /*
     * Shape and place each item in visual order.
     */
    TkWinShapedRun *runs = (TkWinShapedRun *)
	    Tcl_Alloc(sizeof(TkWinShapedRun) * itemCount);
    int nRuns = 0;

    for (int vi = 0; vi < itemCount; vi++) {
	int li = visualToLogical[vi];   /* logical item index */
	SCRIPT_ITEM *item = &items[li];
	int itemStart = item->iCharPos;
	int itemLen   = items[li + 1].iCharPos - itemStart;

	/*
	 * Allocate worst-case glyph buffer: Uniscribe may produce more glyphs
	 * than characters (e.g. ligature decomposition).  The recommended
	 * ceiling is 3/2 * charCount + 16.
	 */
	int    maxGlyphs = (itemLen * 3) / 2 + 16;
	WORD  *glyphs    = (WORD *)Tcl_Alloc(sizeof(WORD) * maxGlyphs);
	WORD  *logClust  = (WORD *)Tcl_Alloc(sizeof(WORD) * itemLen);
	SCRIPT_VISATTR *visAttr =
		(SCRIPT_VISATTR *)Tcl_Alloc(sizeof(SCRIPT_VISATTR) * maxGlyphs);
	int    glyphCount = 0;

	/*
	 * Pick the initial subfont from the first character of this item,
	 * decoding surrogates to the full codepoint so FindSubFontForChar
	 * can look up the correct fallback.
	 */
	int firstCh;
	if (IS_HIGH_SURROGATE(wstr[itemStart]) &&
		(itemStart + 1) < wlen &&
		IS_LOW_SURROGATE(wstr[itemStart + 1])) {
	    firstCh = 0x10000
		+ ((wstr[itemStart]     - 0xD800) << 10)
		+  (wstr[itemStart + 1] - 0xDC00);
	} else {
	    firstCh = (int)(unsigned)wstr[itemStart];
	}

	SubFont *subFontPtr = &fontPtr->subFontArray[0];
	if (firstCh >= BASE_CHARS) {
	    /*
	     * Pass &subFontPtr (not a dummy) so that if FindSubFontForChar
	     * reallocates subFontArray, our local pointer gets fixed up too.
	     */
	    subFontPtr = FindSubFontForChar(fontPtr, firstCh, &subFontPtr);
	}

	/*
	 * Find which index in subFontArray this subfont occupies so we can
	 * pass the matching SCRIPT_CACHE slot to ScriptShape/ScriptPlace.
	 * FindSubFontForChar always returns a pointer into subFontArray.
	 */
	int subFontIdx = (int)(subFontPtr - fontPtr->subFontArray);

	HFONT hFont = subFontPtr->hFont0;
	SelectObject(hdc, hFont);

	/*
	 * ScriptShape: map characters to glyphs.
	 * Pass the per-subfont SCRIPT_CACHE so Uniscribe can reuse shaping
	 * tables across calls on the same font.
	 */
	hr = ScriptShape(hdc, &fontPtr->scriptCacheArray[subFontIdx],
		wstr + itemStart, itemLen,
		maxGlyphs, &item->a,
		glyphs, logClust, visAttr, &glyphCount);

	if (hr == E_OUTOFMEMORY) {
	    /* Grow glyph buffer and retry once. */
	    maxGlyphs *= 2;
	    Tcl_Free(glyphs);
	    Tcl_Free(visAttr);
	    glyphs  = (WORD *)Tcl_Alloc(sizeof(WORD) * maxGlyphs);
	    visAttr = (SCRIPT_VISATTR *)
		    Tcl_Alloc(sizeof(SCRIPT_VISATTR) * maxGlyphs);
	    hr = ScriptShape(hdc, &fontPtr->scriptCacheArray[subFontIdx],
		    wstr + itemStart, itemLen,
		    maxGlyphs, &item->a,
		    glyphs, logClust, visAttr, &glyphCount);
	}

	if (FAILED(hr)) {
	    /*
	     * Shaping failed – try to find a fallback font for the whole
	     * item.  Pass &subFontPtr so the pointer is kept valid across
	     * any reallocation of subFontArray.
	     */
	    SubFont *fb = FindSubFontForChar(fontPtr, firstCh, &subFontPtr);
	    if (fb != subFontPtr) {
		subFontPtr = fb;
		subFontIdx = (int)(subFontPtr - fontPtr->subFontArray);
		hFont = subFontPtr->hFont0;
		SelectObject(hdc, hFont);
		hr = ScriptShape(hdc, &fontPtr->scriptCacheArray[subFontIdx],
			wstr + itemStart, itemLen,
			maxGlyphs, &item->a,
			glyphs, logClust, visAttr, &glyphCount);
	    }
	    if (FAILED(hr)) {
		/* Still failing – skip this item. */
		Tcl_Free(glyphs); Tcl_Free(logClust); Tcl_Free(visAttr);
		continue;
	    }
	}

	/*
	 * Subfont fallback for missing glyphs.
	 * Uniscribe places the font's .notdef glyph (index 0) for any
	 * character it cannot map.  Walk the cluster map: for each character
	 * position whose cluster glyph is 0, decode the codepoint (handling
	 * surrogates) and ask FindSubFontForChar for a better subfont.
	 * Re-shape the whole item with the new font.  One retry only.
	 *
	 * Pass &subFontPtr — not a dummy — to
	 * FindSubFontForChar.  If CanUseFallback reallocates subFontArray,
	 * the fixup path inside FindSubFontForChar updates *subFontPtrPtr,
	 * which is our live subFontPtr variable.  Using a dummy local means
	 * the realloc goes undetected and the subsequent pointer subtraction
	 * produces a garbage subFontIdx.
	 */
	{
	    int ci, foundMissing = 0;
	    for (ci = 0; ci < itemLen && !foundMissing; ci++) {
		int gi = logClust[ci];
		if (glyphs[gi] == 0) {
		    /* Decode surrogate pair if present. */
		    int ch;
		    if (IS_HIGH_SURROGATE(wstr[itemStart + ci]) &&
			    (ci + 1) < itemLen &&
			    IS_LOW_SURROGATE(wstr[itemStart + ci + 1])) {
			ch = 0x10000
			    + ((wstr[itemStart + ci]     - 0xD800) << 10)
			    +  (wstr[itemStart + ci + 1] - 0xDC00);
		    } else {
			ch = (int)(unsigned)wstr[itemStart + ci];
		    }

		    SubFont *fb = FindSubFontForChar(fontPtr, ch, &subFontPtr);
		    if (fb != subFontPtr) {
			subFontPtr  = fb;
			subFontIdx  = (int)(subFontPtr - fontPtr->subFontArray);
			hFont       = subFontPtr->hFont0;
			SelectObject(hdc, hFont);
			/* Re-shape with the new font and its cache slot. */
			ScriptShape(hdc, &fontPtr->scriptCacheArray[subFontIdx],
				wstr + itemStart, itemLen,
				maxGlyphs, &item->a,
				glyphs, logClust, visAttr, &glyphCount);
			foundMissing = 1; /* one retry only */
		    }
		}
	    }
	}

	/*
	 * ScriptPlace: compute advance widths and glyph offsets.
	 * Use the same cache slot that was used for shaping.
	 */
	int     *advances = (int *)Tcl_Alloc(sizeof(int) * glyphCount);
	GOFFSET *offsets  = (GOFFSET *)Tcl_Alloc(sizeof(GOFFSET) * glyphCount);
	ABC      abc;
	hr = ScriptPlace(hdc, &fontPtr->scriptCacheArray[subFontIdx],
		glyphs, glyphCount, visAttr, &item->a,
		advances, offsets, &abc);
	if (FAILED(hr)) {
	    Tcl_Free(glyphs); Tcl_Free(logClust); Tcl_Free(visAttr);
	    Tcl_Free(advances); Tcl_Free(offsets);
	    continue;
	}

	/* logClust and visAttr are not needed past this point. */
	Tcl_Free(visAttr);

	/*
	 * Compute visualX offsets for logical character boundaries.
	 *
	 * visualX[i] is the visual X distance from the run's left (visual)
	 * edge to the inter-character boundary before logical character i.
	 * visualX[0] == 0 always; visualX[itemLen] == total run width.
	 *
	 * For LTR runs glyph order matches logical order, so we accumulate
	 * advances left-to-right.
	 *
	 * For RTL runs Uniscribe stores glyphs in visual left-to-right order
	 * but logClust[] maps in reverse: logClust[0] is the highest glyph
	 * index (the rightmost cluster) and logClust[itemLen-1] is the lowest
	 * (the leftmost cluster).  visualX[ci] therefore equals the sum of
	 * advances for glyphs 0 .. logClust[ci]-1, which are the glyphs that
	 * appear visually to the left of logical character ci's cluster.
	 * No array reversal is required.
	 */
	int *visualX = (int *)Tcl_Alloc(sizeof(int) * (itemLen + 1));
	if (item->a.fRTL) {
	    /*
	     * RTL: compute total run width first, then for each logical
	     * character position sum the advances of glyphs that are
	     * visually to its left (i.e. glyph indices < logClust[ci]).
	     */
	    int totalWidth = 0;
	    for (int g = 0; g < glyphCount; g++) totalWidth += advances[g];

	    for (int ci = 0; ci < itemLen; ci++) {
		int clusterGlyph = (int)logClust[ci];
		int preAdv = 0;
		for (int g = 0; g < clusterGlyph; g++) preAdv += advances[g];
		visualX[ci] = preAdv;
	    }
	    visualX[itemLen] = totalWidth;
	} else {
	    /* LTR: simple left-to-right accumulation. */
	    int curX = 0;
	    visualX[0] = 0;
	    for (int ci = 0; ci < itemLen; ci++) {
		int firstGlyph = (int)logClust[ci];
		int clusterAdv = 0;
		int nextGlyph = glyphCount;
		for (int j = ci + 1; j < itemLen; j++) {
		    if (logClust[j] != firstGlyph) {
			nextGlyph = (int)logClust[j];
			break;
		    }
		}
		for (int g = firstGlyph; g < nextGlyph; g++) clusterAdv += advances[g];
		curX += clusterAdv;
		visualX[ci + 1] = curX;
	    }
	}

	/*
	 * Store the completed run.
	 *
	 * Carry scriptCache in the run so MultiFontTextOut
	 * can pass the correct SCRIPT_CACHE* to ScriptTextOut.  Passing NULL
	 * there causes Uniscribe to discard all shaping work and produce no
	 * visible output.
	 *
	 * Keep logClust so that Tk_MeasureCharsInContext and
	 * Tk_DrawCharsInContext can map UTF-16 character positions to
	 * glyph indices for range-filtered drawing.
	 */
	runs[nRuns].hFont       = hFont;
	runs[nRuns].subFontPtr  = subFontPtr;
	runs[nRuns].scriptCache = &fontPtr->scriptCacheArray[subFontIdx];
	runs[nRuns].sa          = item->a;
	runs[nRuns].glyphCount  = glyphCount;
	runs[nRuns].glyphs      = glyphs;
	runs[nRuns].advances    = advances;
	runs[nRuns].offsets     = offsets;
	runs[nRuns].abc         = abc;
	runs[nRuns].charStart   = itemStart;
	runs[nRuns].charLen     = itemLen;
	runs[nRuns].logClust    = logClust;
	runs[nRuns].visualX     = visualX;
	nRuns++;
    }

    if (items != stackItems) Tcl_Free(items);
    Tcl_Free(visualToLogical);

    *runsOut     = runs;
    *runCountOut = nRuns;
    return nRuns;
#undef ITEM_STACK
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWinFreeShapedRuns --
 *
 *	Release all memory owned by a TkWinShapedRun array produced by
 *	TkWinShapeString().
 *
 *---------------------------------------------------------------------------
 */

static void
TkWinFreeShapedRuns(
    TkWinShapedRun *runs,
    int nRuns)
{
    int i;
    if (runs == NULL) return;
    for (i = 0; i < nRuns; i++) {
	Tcl_Free(runs[i].glyphs);
	Tcl_Free(runs[i].advances);
	Tcl_Free(runs[i].offsets);
	Tcl_Free(runs[i].logClust);
	Tcl_Free(runs[i].visualX);
    }
    Tcl_Free(runs);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWinShapedRunsWidth --
 *
 *	Return the total advance width in pixels of a shaped run array.  This
 *	is the measure path: no drawing occurs.
 *
 *---------------------------------------------------------------------------
 */

static int
TkWinShapedRunsWidth(
    const TkWinShapedRun *runs,
    int nRuns)
{
    int total = 0, i, g;
    for (i = 0; i < nRuns; i++) {
	for (g = 0; g < runs[i].glyphCount; g++) {
	    total += runs[i].advances[g];
	}
    }
    return total;
}

/*
 *---------------------------------------------------------------------------
 *
 * GetVisualXForLogicalIndex --
 *
 *	Given shaped runs (in visual order) and a logical character index
 *	(0‑based from the start of the full string), returns the visual X
 *	coordinate (from the line origin) of the boundary after that character.
 *	If index == total characters, returns total line width.
 *
 *---------------------------------------------------------------------------
 */
static int
GetVisualXForLogicalIndex(
    const TkWinShapedRun *runs,
    int nRuns,
    int logicalIdx)
{
    int i, result, found;

    /*
     * Runs are stored in visual (paint) left-to-right order by
     * TkWinShapeString, but their charStart values are logical UTF-16
     * indices and are NOT monotonically increasing in mixed BiDi text
     * (e.g. an RTL run may have charStart < the LTR run before it
     * visually).  We therefore cannot use a simple accumulate-and-early-
     * exit loop: the first run whose charStart+charLen >= logicalIdx may
     * not be the run that actually owns that logical position.
     *
     * Two-pass approach:
     *   Pass 1 — build runOriginX[i], the visual-left pixel offset of
     *            run i, by linearly accumulating full run widths across
     *            the visual array (this part is still left-to-right).
     *   Pass 2 — search all runs for the one that owns logicalIdx by
     *            checking [charStart, charStart+charLen], then return
     *            that run's origin plus its intra-run visualX offset.
     */

    /* Pass 1: compute visual-left origin of every run. */
    int *runOriginX = (int *)Tcl_Alloc(sizeof(int) * (nRuns ? nRuns : 1));
    {
	int x = 0;
	for (i = 0; i < nRuns; i++) {
	    runOriginX[i] = x;
	    x += runs[i].visualX[runs[i].charLen];
	}
    }

    /* Pass 2: find the run that contains logicalIdx. */
    result = 0;
    found  = 0;
    for (i = 0; i < nRuns && !found; i++) {
	const TkWinShapedRun *run = &runs[i];
	int runStart = run->charStart;
	int runEnd   = runStart + run->charLen;
	if (logicalIdx >= runStart && logicalIdx <= runEnd) {
	    result = runOriginX[i] + run->visualX[logicalIdx - runStart];
	    found  = 1;
	}
    }

    if (!found) {
	/* logicalIdx is beyond the last character — return total width. */
	result = (nRuns > 0)
	    ? runOriginX[nRuns - 1] + runs[nRuns - 1].visualX[runs[nRuns - 1].charLen]
	    : 0;
    }

    Tcl_Free(runOriginX);
    return result;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWinComputeClusterBoundaries --
 *
 * Compute grapheme cluster boundaries for a shaped run using UAX#29.
 *
 * A grapheme cluster boundary occurs at:
 * 1. The start of the string
 * 2. Any position where logClust[] changes (different glyph), UNLESS
 * 3. The character at that position is a combining mark (category Mn/Mc/Me)
 *
 * This ensures ligatures, base+diacritic sequences, and emoji+modifier
 * sequences are never split across line boundaries.
 *
 * Results:
 * Returns a BYTE array (bitmap) where boundaries[i] = 1 means position i
 * starts a new grapheme cluster. The array is heap-allocated and must be
 * freed by the caller with Tcl_Free.
 *
 * Side effects:
 * None.
 *
 *---------------------------------------------------------------------------
 */
static BYTE *
TkWinComputeClusterBoundaries(
    const WCHAR *str,      /* UTF-16 string for this run */
    int len,               /* Length in WCHARs (not bytes) */
    const WORD *logClust,  /* Uniscribe cluster map: char index -> glyph index */
    int glyphCount UNUSED) /* Total glyphs (informational) */
{
    BYTE *boundaries = (BYTE *)Tcl_Alloc(len);
    memset(boundaries, 0, len);
    
    /* First character always starts a cluster */
    boundaries[0] = 1;
    
    for (int i = 1; i < len; i++) {
        int prevGlyph = logClust[i - 1];
        int currGlyph = logClust[i];
        WCHAR ch = str[i];
        
        /* 
         * Check if current character is a combining mark.
         * UAX#29 rules GB3-GB4: combining marks (category Mn, Mc, Me)
         * combine with the preceding base character.
         * 
         * BMP combining mark ranges (most common):
         * - U+0300–036F: Combining Diacritical Marks
         * - U+1AB0–1AFF: Combining Diacritical Marks Extended
         * - U+1DC0–1DFF: Combining Diacritical Marks Supplement
         * - U+20D0–20FF: Combining Diacritical Marks for Symbols
         * - U+FE20–FE2F: Combining Half Marks
         * 
         * For supplementary plane (emoji modifiers, etc.), we conservatively
         * treat them as cluster-starting. Full support requires Unicode
         * property lookup (not practical without external library).
         */
        int isCombining = 0;
        if ((ch >= 0x0300 && ch <= 0x036F) ||  /* Combining Diacritical Marks */
            (ch >= 0x1AB0 && ch <= 0x1AFF) ||  /* Combining Diacritical Marks Extended */
            (ch >= 0x1DC0 && ch <= 0x1DFF) ||  /* Combining Diacritical Marks Supplement */
            (ch >= 0x20D0 && ch <= 0x20FF) ||  /* Combining Diacritical Marks for Symbols */
            (ch >= 0xFE20 && ch <= 0xFE2F)) {  /* Combining Half Marks */
            isCombining = 1;
        }
        
        /* 
         * Mark a cluster boundary if:
         * - The glyph index changed (Uniscribe groups these together), OR
         * - The current character is NOT a combining mark
         * 
         * In other words: no boundary if glyph is the same AND char is combining.
         */
        if (prevGlyph != currGlyph || !isCombining) {
            boundaries[i] = 1;
        }
    }
    
    return boundaries;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWinComputeClusterWidths --
 *
 * Compute the visual pixel width of each grapheme cluster.
 *
 * Given cluster boundaries and per-glyph advance widths, computes the total
 * width (in pixels) of all glyphs that belong to each cluster. This enables
 * fast O(1) width lookups during line breaking.
 *
 * Results:
 * Returns an int array where clusterWidths[i] is the visual width (in pixels)
 * of the cluster starting at position i. Non-boundary positions have
 * clusterWidths[i] = 0. The array is heap-allocated and must be freed by
 * the caller with Tcl_Free.
 *
 * Side effects:
 * None.
 *
 *---------------------------------------------------------------------------
 */
static int *
TkWinComputeClusterWidths(
    const BYTE *clusterBoundaries,  /* Bitmap: position i is cluster start if bit set */
    int charLen,                     /* Total characters in run */
    const int *advances,             /* Per-glyph advance width array */
    int glyphCount UNUSED,           /* Total glyphs (for bounds checking) */
    const WORD *logClust)            /* Character-to-glyph mapping */
{
    int *clusterWidths = (int *)Tcl_Alloc(sizeof(int) * charLen);
    memset(clusterWidths, 0, sizeof(int) * charLen);
    
    for (int ci = 0; ci < charLen; ci++) {
        if (!clusterBoundaries[ci]) {
            continue;  /* Only compute width for cluster boundaries */
        }
        
        /* Find the position of the next cluster boundary (or end of run) */
        int nextBoundary = ci + 1;
        while (nextBoundary < charLen && !clusterBoundaries[nextBoundary]) {
            nextBoundary++;
        }
        
        /* 
         * Find the range of glyph indices that belong to this cluster.
         * All characters [ci, nextBoundary) map to glyphs in [minGlyph, maxGlyph].
         */
        int minGlyph = INT_MAX;
        int maxGlyph = -1;
        
        for (int j = ci; j < nextBoundary; j++) {
            int glyph = (int)logClust[j];
            if (glyph < minGlyph) minGlyph = glyph;
            if (glyph > maxGlyph) maxGlyph = glyph;
        }
        
        /* Sum the advance widths of all glyphs in this cluster */
        int width = 0;
        if (minGlyph >= 0 && minGlyph <= maxGlyph) {
            for (int g = minGlyph; g <= maxGlyph && g < glyphCount; g++) {
                width += advances[g];
            }
        }
        
        clusterWidths[ci] = width;
    }
    
    return clusterWidths;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWinCountClusters --
 *
 * Count the number of grapheme clusters in a run (number of positions
 * where clusterBoundaries[i] = 1).
 *
 * Results:
 * Returns the cluster count.
 *
 * Side effects:
 * None.
 *
 *---------------------------------------------------------------------------
 */
static int
TkWinCountClusters(const BYTE *clusterBoundaries, int charLen)
{
    int count = 0;
    for (int i = 0; i < charLen; i++) {
        if (clusterBoundaries[i]) {
            count++;
        }
    }
    return count;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWinFreeShapedRuns --
 *
 * Free memory allocated by TkWinShapeString().
 *
 * Updated to free the new cluster boundary and width arrays.
 *
 * Results:
 * None.
 *
 * Side effects:
 * All memory associated with the shaped runs is freed.
 *
 *---------------------------------------------------------------------------
 */
void
TkWinFreeShapedRuns(
    TkWinShapedRun *runs,
    int runCount)
{
    if (runs == NULL) {
        return;
    }
    
    for (int i = 0; i < runCount; i++) {
        TkWinShapedRun *run = &runs[i];
        
        if (run->glyphs != NULL) {
            Tcl_Free(run->glyphs);
            run->glyphs = NULL;
        }
        if (run->logClust != NULL) {
            Tcl_Free(run->logClust);
            run->logClust = NULL;
        }
        if (run->advances != NULL) {
            Tcl_Free(run->advances);
            run->advances = NULL;
        }
        if (run->offsets != NULL) {
            Tcl_Free(run->offsets);
            run->offsets = NULL;
        }
        if (run->visualX != NULL) {
            Tcl_Free(run->visualX);
            run->visualX = NULL;
        }
        /* NEW: free cluster arrays */
        if (run->clusterBoundaries != NULL) {
            Tcl_Free(run->clusterBoundaries);
            run->clusterBoundaries = NULL;
        }
        if (run->clusterWidths != NULL) {
            Tcl_Free(run->clusterWidths);
            run->clusterWidths = NULL;
        }
    }
    
    Tcl_Free(runs);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWinLinebreakAwareMeasure --
 *
 * Measure a substring with both cluster-aware and linebreak-aware wrapping.
 *
 * This is a high-level convenience function that:
 * 1. Calls Tk_MeasureCharsInContext() to get the longest cluster-aligned
 *    prefix that fits within maxLength pixels
 * 2. If the entire range fits, returns immediately
 * 3. Otherwise, scans backward to find the last "safe" linebreak opportunity
 *    (space, hyphen, CJK ideograph, etc.) and re-measures to that point
 *
 * This implements a simplified subset of UAX#14 (Unicode Line Breaking
 * Algorithm) without requiring ICU integration.
 *
 * Results:
 * Returns the byte count of the chosen prefix. *lengthPtr is filled with
 * the pixel width of that prefix.
 *
 * Side effects:
 * None.
 *
 *---------------------------------------------------------------------------
 */
int
TkWinLinebreakAwareMeasure(
    Tk_Font tkfont,           /* Font to use for measurement */
    const char *source,       /* Full UTF-8 source string */
    Tcl_Size numBytes,        /* Total bytes in source */
    Tcl_Size rangeStart,      /* Byte offset of range start */
    Tcl_Size rangeLength,     /* Byte length of range to measure */
    int maxLength,            /* Max pixel width, or -1 for unbounded */
    int *lengthPtr)           /* OUT: pixel width of returned prefix */
{
    /* 
     * Measure with cluster awareness. We don't pass any flags here because
     * we handle TK_AT_LEAST_ONE and TK_PARTIAL_OK ourselves if needed.
     */
    int fitBytes = Tk_MeasureCharsInContext(tkfont, source, numBytes,
                                            rangeStart, rangeLength,
                                            maxLength, 0, lengthPtr);
    
    /* If the whole range fits, we're done */
    if (fitBytes >= (int)rangeLength) {
        return fitBytes;
    }
    
    /* 
     * Scan backward to find the last linebreak opportunity.
     * We start from the beginning of the range and scan up to the fit point.
     */
    const char *p = source + rangeStart;
    const char *end = source + rangeStart + fitBytes;
    const char *lastBreak = NULL;
    int ch, prevCh = ' ';
    
    while (p < end) {
        int charBytes = Tcl_UtfToUniChar(p, &ch);
        const char *next = p + charBytes;
        
        /* 
         * Simplified UAX#14 linebreak detection.
         * We consider the following characters as "break after" opportunities:
         * 
         * - U+0020 SPACE (most common)
         * - U+0009 TAB
         * - U+00AD SOFT HYPHEN (breaks without showing hyphen)
         * - U+2000–U+200B General Punctuation spaces
         * - U+2010–U+2015 Hyphen variants
         * - U+4E00–U+9FFF CJK Unified Ideographs (always breakable)
         * - U+3040–U+309F Hiragana (always breakable)
         * 
         * Additional ranges can be added for other scripts (Hangul, etc.).
         */
        int isBreakAfter = 0;
        
        if (ch == ' ' || ch == '\t') {
            isBreakAfter = 1;
        } else if (ch >= 0x2000 && ch <= 0x200B) {
            /* General Punctuation spaces and zero-width joiners */
            isBreakAfter = 1;
        } else if (ch >= 0x2010 && ch <= 0x2015) {
            /* Hyphen-like characters */
            isBreakAfter = 1;
        } else if (ch == 0x00AD) {
            /* Soft hyphen */
            isBreakAfter = 1;
        } else if (ch >= 0x4E00 && ch <= 0x9FFF) {
            /* CJK Unified Ideographs */
            isBreakAfter = 1;
        } else if (ch >= 0x3040 && ch <= 0x309F) {
            /* Hiragana */
            isBreakAfter = 1;
        } else if (ch >= 0xAC00 && ch <= 0xD7AF) {
            /* Hangul Syllables */
            isBreakAfter = 1;
        }
        
        if (isBreakAfter) {
            lastBreak = next;
        }
        
        p = next;
        prevCh = ch;
    }
    
    /* 
     * If we found a linebreak opportunity and it's before the fit point,
     * re-measure to that point to get its exact width.
     */
    if (lastBreak != NULL && lastBreak < end) {
        int breakBytes = lastBreak - (source + rangeStart);
        return Tk_MeasureCharsInContext(tkfont, source, numBytes,
                                       rangeStart, breakBytes,
                                       -1,  /* Unbounded: measure the whole prefix */
                                       0,   /* No flags */
                                       lengthPtr);
    }
    
    /* No linebreak found; return the cluster-aligned fit */
    return fitBytes;
}


/*
 *---------------------------------------------------------------------------
 *
 * Tk_MeasureChars --
 *
 *	Determine the number of bytes from the string that will fit in the
 *	given horizontal span. 
 *
 * Results:
 *	Calls Tk_MeasureCharsInContext.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

int
Tk_MeasureChars(
    Tk_Font tkfont,		/* Font in which characters will be drawn. */
    const char *source,		/* UTF-8 string to be displayed. Need not be
				 * '\0' terminated. */
    Tcl_Size numBytes,		/* Maximum number of bytes to consider from
				 * source string. */
    int maxLength,		/* If >= 0, maxLength specifies the longest
				 * permissible line length in pixels; don't
				 * consider any character that would cross
				 * this x-position. If < 0, then line length
				 * is unbounded and the flags argument is
				 * ignored. */
    int flags,			/* Various flag bits OR-ed together:
				 * TK_PARTIAL_OK means include the last char
				 * which only partially fits on this line.
				 * TK_WHOLE_WORDS means stop on a word
				 * boundary, if possible. TK_AT_LEAST_ONE
				 * means return at least one character (or at
				 * least the first partial word in case
				 * TK_WHOLE_WORDS is also set) even if no
				 * characters (words) fit. */
    int *lengthPtr)		/* Filled with x-location just after the
				 * terminating character. */
{
        return Tk_MeasureCharsInContext(tkfont, source, numBytes, 0, numBytes, 
			maxLength, flags, lengthPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * RunGlyphRange --
 *
 *	Given a TkWinShapedRun and a UTF-16 character sub-range [charFirst,
 *	charLast) relative to the start of that run, return the contiguous
 *	glyph index range [*glyphFirstOut, *glyphLastOut) (exclusive end)
 *	that covers those characters.
 *
 *	logClust[i] is the index of the first glyph for UTF-16 character i
 *	within the run.  Clusters may be in ascending (LTR) or descending
 *	(RTL) glyph order.  We collect the min/max glyph index touched by the
 *	character range, then extend the end to the next cluster boundary.
 *
 * Results:
 *	Returns 1 and sets *glyphFirstOut/*glyphLastOut on success, 0 if the
 *	range maps to no glyphs.
 *
 *---------------------------------------------------------------------------
 */
static int
RunGlyphRange(
    const TkWinShapedRun *run,
    int charFirst,	/* First char index within the run (0-based, clamped). */
    int charLast,	/* One past the last char index (clamped). */
    int *glyphFirstOut,
    int *glyphLastOut)
{
    int i, gMin, gMax;

    if (charFirst < 0)		 charFirst = 0;
    if (charLast  > run->charLen) charLast = run->charLen;
    if (charFirst >= charLast)	 return 0;

    gMin = run->glyphCount;
    gMax = -1;
    for (i = charFirst; i < charLast; i++) {
	int g = (int)run->logClust[i];
	if (g < gMin) gMin = g;
	if (g > gMax) gMax = g;
    }
    if (gMax < 0) return 0;

    /* Extend gMax to the end of the last cluster (exclusive). */
    {
	int nextCluster = run->glyphCount;
	for (i = 0; i < run->charLen; i++) {
	    int g = (int)run->logClust[i];
	    if (g > gMax && g < nextCluster) nextCluster = g;
	}
	gMax = nextCluster;
    }

    *glyphFirstOut = gMin;
    *glyphLastOut  = gMax;
    return (gMax > gMin) ? 1 : 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * Tk_MeasureCharsInContext --
 *
 *	Determine the number of bytes from the string that will fit in the
 *	given horizontal span, with access to the full source string for
 *	shaping context.
 *
 *	This implementation shapes the full string (up to the end of the range)
 *	and then uses the visualX offsets stored in each shaped run to map
 *	logical character positions to visual X coordinates.  This ensures
 *	that the measured width of a logical prefix matches the visual layout
 *	used for drawing, eliminating the cursor “snap” in RTL scripts.
 *
 * Results:
 *	The return value is the number of bytes from rangeStart that fit.
 *	*lengthPtr is filled with the pixel width of those bytes.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

/*
 *---------------------------------------------------------------------------
 *
 * Tk_MeasureCharsInContext --
 *
 *	Determine the number of bytes from the string that will fit in the
 *	given horizontal span, with access to the full source string for
 *	shaping context.
 *
 *	This implementation shapes the full string (up to the end of the range)
 *	and then uses the visualX offsets stored in each shaped run to map
 *	logical character positions to visual X coordinates.  This ensures
 *	that the measured width of a logical prefix matches the visual layout
 *	used for drawing, eliminating the cursor “snap” in RTL scripts.
 *
 * Results:
 *	The return value is the number of bytes from rangeStart that fit.
 *	*lengthPtr is filled with the pixel width of those bytes.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

int
Tk_MeasureCharsInContext(
    Tk_Font tkfont,                 /* Font to use for measurement */
    const char *source,             /* Full UTF-8 string (entire text) */
    Tcl_Size numBytes,              /* Total length of source in bytes */
    Tcl_Size rangeStart,            /* Byte offset in source of substring to measure */
    Tcl_Size rangeLength,           /* Byte length of substring to measure */
    int maxLength,                  /* Max pixel width, or -1 for unbounded */
    int flags,                      /* TK_* flags (AT_LEAST_ONE, PARTIAL_OK, WHOLE_WORDS) */
    int *lengthPtr)                 /* OUT: pixel width of the chosen prefix */
{
    WinFont *fontPtr = (WinFont *) tkfont;  /* Windows font internal structure */
    HDC hdc;                                /* Device context for shaping */
    Tcl_DString fullUni;                    /* Buffer for UTF‑16 conversion of the whole string */
    WCHAR *wfull;                           /* Pointer to UTF‑16 string */
    int wfullLen;                           /* Length of wfull in WCHARs */
    TkWinShapedRun *runs = NULL;            /* Array of shaped runs (result of ScriptShape/ScriptPlace) */
    int nRuns = 0;                          /* Number of shaped runs */
    int resultBytes = 0;                    /* Bytes that fit (to be returned) */
    int resultWidth = 0;                    /* Pixel width of those bytes (to store in *lengthPtr) */
    int i;                                  /* Loop index */

    /* If the measured range is empty, we trivially fit zero bytes. */
    if (rangeLength == 0) {
        *lengthPtr = 0;
        return 0;
    }

    /*
     * Obtain a device context and convert the entire source string from
     * UTF‑8 to UTF‑16. The conversion is performed on the whole source
     * because shaping may need context beyond the measured range.
     */
    hdc = GetDC(fontPtr->hwnd);
    Tcl_DStringInit(&fullUni);
    Tcl_UtfToWCharDString(source, numBytes, &fullUni);
    wfull = (WCHAR *) Tcl_DStringValue(&fullUni);
    wfullLen = (int)(Tcl_DStringLength(&fullUni) / sizeof(WCHAR));

    /*
     * Ask the Windows Uniscribe subsystem to shape the entire UTF‑16 string.
     * This produces one or more runs, each with logical‑to‑visual mapping,
     * glyph advances, and precomputed visualX offsets for every character
     * position inside the run.
     *
     * If shaping fails (e.g., no runs produced), fall back to the classic
     * Tk_MeasureChars which works correctly only for LTR text.
     */
    if (TkWinShapeString(hdc, fontPtr, wfull, wfullLen, &runs, &nRuns) < 0
        || nRuns == 0) {
        ReleaseDC(fontPtr->hwnd, hdc);
        Tcl_DStringFree(&fullUni);
        return Tk_MeasureChars(tkfont, source + rangeStart, rangeLength,
               maxLength, flags, lengthPtr);
    }

    /*
     * Build an array runOriginX[] that holds the absolute visual X coordinate
     * of the left edge of each shaped run (relative to the shaping origin).
     * We iterate over runs and sum the total visual width of each previous run.
     * The total width of a run is runs[i].visualX[runs[i].charLen] (the visual
     * X offset after the last character in that run).
     */
    int *runOriginX = (int *)Tcl_Alloc(sizeof(int) * nRuns);
    {
        int x = 0;
        for (i = 0; i < nRuns; i++) {
            runOriginX[i] = x;
            x += runs[i].visualX[runs[i].charLen];
        }
    }

    /*
     * Convert the UTF‑8 byte range [rangeStart, rangeStart+rangeLength) into
     * UTF‑16 code unit indices (wRangeStart, wRangeEnd). This is necessary
     * because the shaped runs work with UTF‑16 positions, while the caller
     * works with UTF‑8 byte offsets into the source string.
     *
     * We convert two points: the start of the range, and the end of the range.
     * The temporary DStrings are freed immediately after conversion.
     */
    int wRangeStart, wRangeEnd;
    {
        Tcl_DString tmp;
        Tcl_DStringInit(&tmp);
        Tcl_UtfToWCharDString(source, rangeStart, &tmp);
        wRangeStart = (int)(Tcl_DStringLength(&tmp) / sizeof(WCHAR));
        Tcl_DStringFree(&tmp);

        Tcl_DStringInit(&tmp);
        Tcl_UtfToWCharDString(source, rangeStart + rangeLength, &tmp);
        wRangeEnd = (int)(Tcl_DStringLength(&tmp) / sizeof(WCHAR));
        Tcl_DStringFree(&tmp);
    }

    /*
     * Compute rangeVisualOrigin – the leftmost visual X coordinate of any
     * character inside the measured range. This acts as a virtual origin
     * for the range, so that later we can compute widths relative to the
     * range's start, not the absolute shaping origin.
     *
     * For a pure LTR range this will be the X of the first character.
     * For a range that starts in the middle of an RTL run, the leftmost
     * visual point might be at the *end* of the logical range (since RTL
     * characters extend leftwards from the logical start).
     */
    int rangeVisualOrigin = INT_MAX;
    for (i = 0; i < nRuns; i++) {
        int runStart = runs[i].charStart;          /* UTF‑16 index where this run starts in wfull */
        int runEnd = runStart + runs[i].charLen;   /* UTF‑16 index after this run */
        /* Intersection of this run with the measured [wRangeStart, wRangeEnd) interval */
        int lo = (wRangeStart > runStart) ? wRangeStart : runStart;
        int hi = (wRangeEnd < runEnd) ? wRangeEnd : runEnd;
        if (lo >= hi) continue;                    /* No overlap with this run */

        /* Visual X of the leftmost boundary of this intersection within the run */
        int xA = runOriginX[i] + runs[i].visualX[lo - runStart];
        int xB = runOriginX[i] + runs[i].visualX[hi - runStart];
        int xMin = (xA < xB) ? xA : xB;
        if (xMin < rangeVisualOrigin) rangeVisualOrigin = xMin;
    }
    if (rangeVisualOrigin == INT_MAX) rangeVisualOrigin = 0;  /* No characters? Should not happen */

    if (maxLength < 0) {
        /*
         * Unbounded measurement: the whole range fits regardless of width.
         * Compute the overall visual extent (maxX) of the range relative to
         * rangeVisualOrigin, i.e., the width of the entire measured substring
         * as it would be drawn.
         */
        int maxX = 0;
        for (i = 0; i < nRuns; i++) {
            int runStart = runs[i].charStart;
            int runEnd = runStart + runs[i].charLen;
            int lo = (wRangeStart > runStart) ? wRangeStart : runStart;
            int hi = (wRangeEnd < runEnd) ? wRangeEnd : runEnd;
            if (lo >= hi) continue;

            /* Rightmost and leftmost X positions inside the intersection, relative to rangeVisualOrigin */
            int rightEdge = runOriginX[i] + runs[i].visualX[hi - runStart] - rangeVisualOrigin;
            int leftEdge  = runOriginX[i] + runs[i].visualX[lo - runStart]  - rangeVisualOrigin;
            if (rightEdge > maxX) maxX = rightEdge;
            if (leftEdge  > maxX) maxX = leftEdge;
        }
        resultBytes = (int)rangeLength;   /* All bytes fit */
        resultWidth = maxX;               /* Total visual width of the range */
    } else {
        /*
         * Bounded measurement: we need to find the longest logical prefix
         * of the measured range whose visual width does not exceed maxLength.
         * We iterate character by character (UTF‑8 to Unicode, then to
         * potentially 1 or 2 UTF‑16 code units per character). For each
         * candidate length, we compute the visual width by consulting the
         * shaped runs (or using ScriptCPtoX for higher precision).
         */
        const char *p = source + rangeStart;         /* Current position in UTF‑8 source */
        const char *end = source + rangeStart + rangeLength; /* End of measured range */
        int wCount = 0;                     /* Number of UTF‑16 code units consumed so far */
        int byteCount = 0;                  /* Number of UTF‑8 bytes consumed so far */
        int lastFitBytes = 0;               /* Bytes of the last prefix that fits */
        int lastFitWidth = 0;               /* Width of that last fitting prefix */
        int firstChar = 1;                  /* Flag to handle TK_AT_LEAST_ONE */

        /* Walk the measured range one Unicode character at a time */
        while (p < end) {
            int ch;                                         /* Unicode code point */
            int charBytes = (int)Tcl_UtfToUniChar(p, &ch);  /* UTF‑8 bytes for this character */
            int charW = (ch > 0xFFFF) ? 2 : 1;              /* UTF‑16 code units needed: 2 for supplementary, 1 for BMP */
            int nextWCount = wCount + charW;                /* UTF‑16 position after appending this character */

            /* Compute the visual width of the prefix that includes this new character */
            int relMaxX = 0;
            for (i = 0; i < nRuns; i++) {
                TkWinShapedRun *run = &runs[i];
                int runStart = run->charStart;
                int runEnd = runStart + run->charLen;

                /* Overlap of the prefix [wRangeStart, wRangeStart+nextWCount) with this run */
                int lo = (wRangeStart > runStart) ? wRangeStart : runStart;
                int hi = (wRangeStart + nextWCount < runEnd) ? wRangeStart + nextWCount : runEnd;
                if (lo >= hi) continue;

                /*
                 * For the exact insertion point (where the cursor would be
                 * after the current prefix), we can ask Uniscribe to map the
                 * logical character index to a visual X coordinate using
                 * ScriptCPtoX. This is more accurate than the precomputed
                 * visualX table when characters have been reordered due to
                 * bidirectional layout.
                 *
                 * However, ScriptCPtoX requires a character index inside the run.
                 * cpInRun is the offset from runStart to the logical position
                 * right after the prefix (i.e., the number of characters consumed
                 * from this run, which may be less than nextWCount if the prefix
                 * spans multiple runs).
                 */
                int cpInRun = (wRangeStart + wCount) - runStart;
                int xPos = 0;
                HRESULT hr = ScriptCPtoX(cpInRun, FALSE, run->charLen,
                                       run->glyphCount, run->logClust,
                                       NULL, run->advances, &run->sa, &xPos);

                if (hr == S_OK) {
                    /* Uniscribe gave us the exact visual X for the cursor position */
                    int visualX = runOriginX[i] + xPos - rangeVisualOrigin;
                    if (visualX > relMaxX) relMaxX = visualX;
                } else {
                    /*
                     * Fallback to the precomputed visualX array (which is always valid,
                     * though it might not be as precise for the cursor position inside
                     * a complex cluster). We take the maximum X coordinate among the
                     * left and right edges of the overlapping slice – this is safe
                     * because the visual X of any character inside the slice cannot
                     * exceed the max of its boundaries.
                     */
                    int xA = runOriginX[i] + run->visualX[lo - runStart] - rangeVisualOrigin;
                    int xB = runOriginX[i] + run->visualX[hi - runStart] - rangeVisualOrigin;
                    if (xA > relMaxX) relMaxX = xA;
                    if (xB > relMaxX) relMaxX = xB;
                }
            }

            /* Check if the extended prefix fits within maxLength */
            if (relMaxX <= maxLength) {
                /* It fits – remember this as the last fitting prefix and continue */
                lastFitBytes = byteCount + charBytes;
                lastFitWidth = relMaxX;
                wCount = nextWCount;
                byteCount += charBytes;
                p += charBytes;
                firstChar = 0;
            } else {
                /*
                 * The extended prefix is too wide. Decide what to return
                 * based on the TK_AT_LEAST_ONE and TK_PARTIAL_OK flags.
                 */
                if (firstChar && (flags & TK_AT_LEAST_ONE)) {
                    /* Even the first character doesn't fit, but we must return at least one character */
                    lastFitBytes = charBytes;
                    lastFitWidth = relMaxX;
                }
                if (flags & TK_PARTIAL_OK) {
                    /*
                     * Partial characters are allowed: return the current prefix
                     * (which includes the character we just tried) even though
                     * it exceeds the width. This is used for measuring how much
                     * of a multi‑byte character fits (rare).
                     */
                    lastFitBytes = byteCount + charBytes;
                    lastFitWidth = relMaxX;
                }
                break;
            }
        }
        resultBytes = lastFitBytes;
        resultWidth = lastFitWidth;
    }

    /*
     * TK_WHOLE_WORDS handling: if the caller requests whole‑word boundaries,
     * and we did not fit the entire range, we need to back up to the last
     * space (word break) that fits within the width.
     *
     * The algorithm scans the UTF‑8 text from the start of the range up to
     * the currently chosen fit point (resultBytes). It remembers the position
     * of the last space character (prevCh != ' ' && ch == ' ') before the end.
     * If such a word break exists, we recursively measure that prefix without
     * width limit (maxLength = -1) to obtain its exact width.
     * If no word break is found and TK_AT_LEAST_ONE is not set, we return 0.
     */
    if ((flags & TK_WHOLE_WORDS) && (resultBytes < (int)rangeLength)) {
        const char *p2 = source + rangeStart;
        const char *end2 = source + rangeStart + resultBytes;
        const char *lastWordBreak = NULL;
        int ch, prevCh = ' ';
        while (p2 < end2) {
            const char *next = p2 + Tcl_UtfToUniChar(p2, &ch);
            if (prevCh != ' ' && ch == ' ') {
                lastWordBreak = p2;   /* p2 points to the space character itself */
            }
            p2 = next;
            prevCh = ch;
        }
        if (lastWordBreak != NULL) {
            /* Re‑measure the substring up to the word break, without any width limit */
            resultBytes = Tk_MeasureCharsInContext(tkfont, source, numBytes,
                rangeStart, lastWordBreak - (source + rangeStart),
                -1, 0, &resultWidth);
        } else if (!(flags & TK_AT_LEAST_ONE)) {
            /* No word break and we are not forced to return at least one character */
            resultBytes = 0;
            resultWidth = 0;
        }
    }

    /* Clean up: free the run origin array, the shaped runs, the UTF‑16 buffer, and release the DC */
    Tcl_Free(runOriginX);
    TkWinFreeShapedRuns(runs, nRuns);
    Tcl_DStringFree(&fullUni);
    ReleaseDC(fontPtr->hwnd, hdc);

    *lengthPtr = resultWidth;
    return resultBytes;
}
/*
 *---------------------------------------------------------------------------
 *
 * Tk_DrawChars --
 *
 *	Draw a string of characters on the screen.
 *
 *
 * Results:
 *	Calls Tk_DrawCharsInContext.
 *
 * Side effects:
 *	Information gets drawn on the screen.
 *
 *---------------------------------------------------------------------------
 */

void
Tk_DrawChars(
    Display *display,		/* Display on which to draw. */
    Drawable drawable,		/* Window or pixmap in which to draw. */
    GC gc,			/* Graphics context for drawing characters. */
    TCL_UNUSED(Tk_Font),	/* Font in which characters will be drawn;
				 * must be the same as font used in GC. */
    const char *source,		/* UTF-8 string to be displayed. Need not be
				 * '\0' terminated. All Tk meta-characters
				 * (tabs, control characters, and newlines)
				 * should be stripped out of the string that
				 * is passed to this function. If they are not
				 * stripped out, they will be displayed as
				 * regular printing characters. */
    Tcl_Size numBytes,		/* Number of bytes in string. */
    int x, int y)		/* Coordinates at which to place origin of
				 * string when drawing. */
{
    if (numBytes <= 0 || source == NULL) {
        return;
    }

    /*
     * Delegate everything to the context-aware renderer.
     * We draw the full string as a single logical range.
     */
    Tk_DrawCharsInContext(display, drawable, gc, NULL ,source,numBytes,0, numBytes,x, y);
}

void
TkDrawAngledChars(
    Display *display,		/* Display on which to draw. */
    Drawable drawable,		/* Window or pixmap in which to draw. */
    GC gc,			/* Graphics context for drawing characters. */
    TCL_UNUSED(Tk_Font),	/* Font in which characters will be drawn;
				 * must be the same as font used in GC. */
    const char *source,		/* UTF-8 string to be displayed. Need not be
				 * '\0' terminated. All Tk meta-characters
				 * (tabs, control characters, and newlines)
				 * should be stripped out of the string that
				 * is passed to this function. If they are not
				 * stripped out, they will be displayed as
				 * regular printing characters. */
    Tcl_Size numBytes,		/* Number of bytes in string. */
    double x, double y,		/* Coordinates at which to place origin of
				 * string when drawing. */
    double angle)
{
    HDC dc;
    WinFont *fontPtr;
    TkWinDCState state;

    fontPtr = (WinFont *) gc->font;
    LastKnownRequestProcessed(display)++;

    if (drawable == None) {
	return;
    }

    dc = TkWinGetDrawableDC(display, drawable, &state);

    SetROP2(dc, tkpWinRopModes[gc->function]);

    if ((gc->clip_mask != None) &&
	    ((TkpClipMask *) gc->clip_mask)->type == TKP_CLIP_REGION) {
	SelectClipRgn(dc, (HRGN)((TkpClipMask *)gc->clip_mask)->value.region);
    }

    if ((gc->fill_style == FillStippled
	    || gc->fill_style == FillOpaqueStippled)
	    && gc->stipple != None) {
	TkWinDrawable *twdPtr = (TkWinDrawable *)gc->stipple;
	HBRUSH oldBrush, stipple;
	HBITMAP oldBitmap, bitmap;
	HDC dcMem;
	TEXTMETRICW tm;
	SIZE size;

	if (twdPtr->type != TWD_BITMAP) {
	    Tcl_Panic("unexpected drawable type in stipple");
	}

	/*
	 * Select stipple pattern into destination dc.
	 */

	dcMem = CreateCompatibleDC(dc);

	stipple = CreatePatternBrush(twdPtr->bitmap.handle);
	SetBrushOrgEx(dc, gc->ts_x_origin, gc->ts_y_origin, NULL);
	oldBrush = (HBRUSH)SelectObject(dc, stipple);

	SetTextAlign(dcMem, TA_LEFT | TA_BASELINE);
	SetTextColor(dcMem, gc->foreground);
	SetBkMode(dcMem, TRANSPARENT);
	SetBkColor(dcMem, RGB(0, 0, 0));

	/*
	 * Compute the bounding box and create a compatible bitmap.
	 */

	GetTextExtentPointA(dcMem, source, (int)numBytes, &size);
	GetTextMetricsW(dcMem, &tm);
	size.cx -= tm.tmOverhang;
	bitmap = CreateCompatibleBitmap(dc, size.cx, size.cy);
	oldBitmap = (HBITMAP)SelectObject(dcMem, bitmap);

	/*
	 * The following code is tricky because fonts are rendered in multiple
	 * colors. First we draw onto a black background and copy the white
	 * bits. Then we draw onto a white background and copy the black bits.
	 * Both the foreground and background bits of the font are ANDed with
	 * the stipple pattern as they are copied.
	 */

	PatBlt(dcMem, 0, 0, size.cx, size.cy, BLACKNESS);
	MultiFontTextOut(dc, fontPtr, source, (int)numBytes, x, y, angle);
	BitBlt(dc, (int)x, (int)y - tm.tmAscent, size.cx, size.cy, dcMem,
		0, 0, 0xEA02E9);
	PatBlt(dcMem, 0, 0, size.cx, size.cy, WHITENESS);
	MultiFontTextOut(dc, fontPtr, source, (int)numBytes, x, y, angle);
	BitBlt(dc, (int)x, (int)y - tm.tmAscent, size.cx, size.cy, dcMem,
		0, 0, 0x8A0E06);

	/*
	 * Destroy the temporary bitmap and restore the device context.
	 */

	SelectObject(dcMem, oldBitmap);
	DeleteObject(bitmap);
	DeleteDC(dcMem);
	SelectObject(dc, oldBrush);
	DeleteObject(stipple);
    } else if (gc->function == GXcopy) {
	SetTextAlign(dc, TA_LEFT | TA_BASELINE);
	SetTextColor(dc, gc->foreground);
	SetBkMode(dc, TRANSPARENT);
	MultiFontTextOut(dc, fontPtr, source, (int)numBytes, x, y, angle);
    } else {
	HBITMAP oldBitmap, bitmap;
	HDC dcMem;
	TEXTMETRICW tm;
	SIZE size;

	dcMem = CreateCompatibleDC(dc);

	SetTextAlign(dcMem, TA_LEFT | TA_BASELINE);
	SetTextColor(dcMem, gc->foreground);
	SetBkMode(dcMem, TRANSPARENT);
	SetBkColor(dcMem, RGB(0, 0, 0));

	/*
	 * Compute the bounding box and create a compatible bitmap.
	 */

	GetTextExtentPointA(dcMem, source, (int)numBytes, &size);
	GetTextMetricsW(dcMem, &tm);
	size.cx -= tm.tmOverhang;
	bitmap = CreateCompatibleBitmap(dc, size.cx, size.cy);
	oldBitmap = (HBITMAP)SelectObject(dcMem, bitmap);

	MultiFontTextOut(dcMem, fontPtr, source, (int)numBytes, 0, tm.tmAscent,
		angle);
	BitBlt(dc, (int)x, (int)y - tm.tmAscent, size.cx, size.cy, dcMem,
		0, 0, (DWORD) tkpWinBltModes[gc->function]);

	/*
	 * Destroy the temporary bitmap and restore the device context.
	 */

	SelectObject(dcMem, oldBitmap);
	DeleteObject(bitmap);
	DeleteDC(dcMem);
    }
    TkWinReleaseDrawableDC(drawable, dc, &state);
}

/*
 *---------------------------------------------------------------------------
 *
 * Tk_DrawCharsInContext --
 *
 *	Draw a substring of text using the FULL source string as Uniscribe
 *	shaping context, so that Arabic joining forms, ligatures, and bidi
 *	reordering are computed across the entire base-chunk line rather than
 *	for the sub-range alone.
 *
 *	source[0..numBytes) is the full base-chunk string (from tkTextDisp.c's
 *	baseChars DString).  rangeStart/rangeLength select the bytes to draw.
 *	x/y is the screen origin of byte 0 of the full string.
 *
 *	We convert the full string to UTF-16, call TkWinShapeString once for
 *	the complete context, then for each shaped run call ScriptTextOut for
 *	only the glyphs whose character positions fall within the range.  This
 *	eliminates the cursor shimmer caused by shaping Arabic/Hebrew runs in
 *	isolation when the insert cursor mark splits the text segment.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information gets drawn on the screen.
 *
 *---------------------------------------------------------------------------
 */

void
Tk_DrawCharsInContext(
    Display *display,
    Drawable drawable,
    GC gc,
    Tk_Font tkfont,
    const char *source,
    Tcl_Size numBytes,		/* Total bytes in the full base-chunk string. */
    Tcl_Size rangeStart,	/* Byte offset of the substring to draw. */
    Tcl_Size rangeLength,	/* Byte length of the substring to draw. */
    int x, int y)		/* Screen origin of byte 0 of the full string. */
{
    WinFont *fontPtr = (WinFont *) tkfont;
    HDC dc;
    TkWinDCState dcState;
    Tcl_DString uniStr;
    WCHAR *wstr;
    int wlen, wRangeStart, wRangeEnd;
    TkWinShapedRun *runs = NULL;
    int nRuns = 0, i;

    if (rangeLength <= 0 || drawable == None) return;

    LastKnownRequestProcessed(display)++;

    dc = TkWinGetDrawableDC(display, drawable, &dcState);

    /*
     * Convert the FULL source string to UTF-16 so Uniscribe sees the
     * complete context across the cursor-mark chunk boundary.
     */
    Tcl_DStringInit(&uniStr);
    Tcl_UtfToWCharDString(source, numBytes, &uniStr);
    wstr = (WCHAR *)Tcl_DStringValue(&uniStr);
    wlen = (int)(Tcl_DStringLength(&uniStr) / sizeof(WCHAR));

    /*
     * Map the UTF-8 byte range to UTF-16 character positions.
     */
    {
	Tcl_DString tmp;
	Tcl_DStringInit(&tmp);
	Tcl_UtfToWCharDString(source, rangeStart, &tmp);
	wRangeStart = (int)(Tcl_DStringLength(&tmp) / sizeof(WCHAR));
	Tcl_DStringFree(&tmp);
	Tcl_DStringInit(&tmp);
	Tcl_UtfToWCharDString(source, rangeStart + rangeLength, &tmp);
	wRangeEnd = (int)(Tcl_DStringLength(&tmp) / sizeof(WCHAR));
	Tcl_DStringFree(&tmp);
    }

    if (TkWinShapeString(dc, fontPtr, wstr, wlen, &runs, &nRuns) < 0
	    || nRuns == 0) {
	/*
	 * Shaping failed — fall back to prefix-offset draw of the range.
	 */
	int widthUntilStart;
	Tcl_DStringFree(&uniStr);
	TkWinReleaseDrawableDC(drawable, dc, &dcState);
	Tk_MeasureChars(tkfont, source, rangeStart, -1, 0, &widthUntilStart);
	Tk_DrawChars(display, drawable, gc, tkfont,
		source + rangeStart, rangeLength, x + widthUntilStart, y);
	return;
    }

    SetROP2(dc, tkpWinRopModes[gc->function]);
    if ((gc->clip_mask != None) &&
	    ((TkpClipMask *) gc->clip_mask)->type == TKP_CLIP_REGION) {
	SelectClipRgn(dc, (HRGN)((TkpClipMask *)gc->clip_mask)->value.region);
    }
    SetTextAlign(dc, TA_LEFT | TA_BASELINE);
    SetTextColor(dc, gc->foreground);
    int oldBkMode = SetBkMode(dc, TRANSPARENT);
    HFONT oldFont = (HFONT)GetCurrentObject(dc, OBJ_FONT);

    /*
     * Walk runs in visual (paint) order.  For each run compute the pen
     * position (x + sum of all preceding run widths), then draw only the
     * glyph slice that falls within [wRangeStart, wRangeEnd).
     */
    int penX = x;
    for (i = 0; i < nRuns; i++) {
	TkWinShapedRun *run = &runs[i];
	int runWidth = 0, g;
	for (g = 0; g < run->glyphCount; g++) runWidth += run->advances[g];

	int runEnd = run->charStart + run->charLen;

	if (runEnd <= wRangeStart || run->charStart >= wRangeEnd) {
	    /* Run entirely outside the draw range — advance pen only. */
	    penX += runWidth;
	    continue;
	}

	/*
	 * Find the glyph slice within this run that corresponds to the
	 * character range.
	 */
	int charFirst = wRangeStart - run->charStart;
	int charLast  = wRangeEnd   - run->charStart;
	int gFirst, gLast;

	if (!RunGlyphRange(run, charFirst, charLast, &gFirst, &gLast)) {
	    penX += runWidth;
	    continue;
	}

	/* Pixel offset to the first in-range glyph within this run. */
	int preWidth = 0;
	for (g = 0; g < gFirst; g++) preWidth += run->advances[g];

	SelectObject(dc, run->hFont);
	ScriptTextOut(
	    dc,
	    run->scriptCache,
	    penX + preWidth, y,
	    0, NULL,
	    &run->sa,
	    NULL, 0,
	    run->glyphs   + gFirst,
	    gLast - gFirst,
	    run->advances  + gFirst,
	    NULL,
	    run->offsets   + gFirst);

	penX += runWidth;
    }

    SetBkMode(dc, oldBkMode);
    SelectObject(dc, oldFont);
    TkWinFreeShapedRuns(runs, nRuns);
    Tcl_DStringFree(&uniStr);
    TkWinReleaseDrawableDC(drawable, dc, &dcState);
}

void
TkpDrawAngledCharsInContext(
    Display *display,		/* Display on which to draw. */
    Drawable drawable,		/* Window or pixmap in which to draw. */
    GC gc,			/* Graphics context for drawing characters. */
    Tk_Font tkfont,		/* Font in which characters will be drawn; must
				 * be the same as font used in GC. */
    const char * source,	/* UTF-8 string to be displayed. Need not be
				 * '\0' terminated. All Tk meta-characters
				 * (tabs, control characters, and newlines)
				 * should be stripped out of the string that is
				 * passed to this function. If they are not
				 * stripped out, they will be displayed as
				 * regular printing characters. */
    TCL_UNUSED(Tcl_Size),		/* Number of bytes in string. */
    Tcl_Size rangeStart,		/* Index of first byte to draw. */
    Tcl_Size rangeLength,		/* Length of range to draw in bytes. */
    double x, double y,		/* Coordinates at which to place origin of the
				 * whole (not just the range) string when
				 * drawing. */
    double angle)		/* What angle to put text at, in degrees. */
{
    int widthUntilStart;
    double sinA = sin(angle * PI/180.0), cosA = cos(angle * PI/180.0);

    Tk_MeasureChars(tkfont, source, rangeStart, -1, 0, &widthUntilStart);
    TkDrawAngledChars(display, drawable, gc, tkfont, source + rangeStart,
	    rangeLength, x+cosA*widthUntilStart, y-sinA*widthUntilStart, angle);
}

/*
 *-------------------------------------------------------------------------
 *
 * MultiFontTextOut --
 *
 *	Render a UTF-8 string using the Uniscribe shaping layer.
 *
 *	This function is the sole drawing entry point.  It calls
 *	TkWinShapeString() to obtain fully shaped, bidi-reordered
 *	TkWinShapedRun buffers, then dispatches each run to the appropriate
 *	GDI rendering path, ScriptTextOut.
 *
 *	If shaping fails entirely we fall back to plain GDI drawing with the
 *	base font so that text always appears.
 *
 *	The 'angle' parameter rotates the escapement of the font before
 *	shaping so that TrueType rotation is applied correctly per run.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information gets drawn on the screen. Contents of fontPtr may be
 *	modified if more subfonts were loaded in order to draw all the
 *	multilingual characters in the given string.
 *
 *-------------------------------------------------------------------------
 */

static void
MultiFontTextOut(
    HDC hdc,
    WinFont *fontPtr,
    const char *source,
    int numBytes,
    double x, double y,
    double angle)
{
    Tcl_DString uniStr;
    WCHAR *wstr;
    int wlen;
    int i;
    HFONT oldFont;

    if (numBytes == 0) return;

    Tcl_DStringInit(&uniStr);
    Tcl_UtfToWCharDString(source, numBytes, &uniStr);
    wstr = (WCHAR *)Tcl_DStringValue(&uniStr);
    wlen = (int)(Tcl_DStringLength(&uniStr) / sizeof(WCHAR));

    /*
     * Normal Uniscribe path -
     *
     * Used for all text: Arabic, Hebrew, Indic, Thai, emoji, etc.
     * FindSubFontForChar now correctly selects Segoe UI Emoji for
     * supplementary-plane codepoints via the format-12 group table,
     * so the per-run color-glyph ETO_GLYPH_INDEX path below handles
     * emoji rendering without bypassing Uniscribe.
     */

    TkWinShapedRun *runs = NULL;
    int nRuns = 0;

    if (TkWinShapeString(hdc, fontPtr, wstr, wlen, &runs, &nRuns) < 0 || nRuns == 0) {
	HFONT old = (HFONT)SelectObject(hdc, fontPtr->subFontArray[0].hFont0);
	SetBkMode(hdc, TRANSPARENT);
	TextOutW(hdc, (int)x, (int)y, wstr, wlen);
	SelectObject(hdc, old);
	Tcl_DStringFree(&uniStr);
	return;
    }

    oldFont = (HFONT)GetCurrentObject(hdc, OBJ_FONT);
    int oldBkMode = SetBkMode(hdc, TRANSPARENT);

    double sinA = sin(angle * PI / 180.0);
    double cosA = cos(angle * PI / 180.0);

    for (i = 0; i < nRuns; i++) {
	TkWinShapedRun *run = &runs[i];
	HFONT hDrawFont = run->hFont;
	HFONT hAngled = NULL;
	int runWidth = 0;

	/* Rotation support. */
	if (angle != 0.0) {
	    hAngled = GetScreenFont(&fontPtr->font.fa,
				    run->subFontPtr->familyPtr->faceName,
				    fontPtr->pixelSize,
				    angle);
	    if (hAngled) {
		hDrawFont = hAngled;
	    }
	}

	SelectObject(hdc, hDrawFont);

	ScriptTextOut(
	    hdc,
	    run->scriptCache,
	    (int)(x + 0.5), (int)(y + 0.5),
	    0,
	    NULL,
	    &run->sa,
	    NULL, 0,
	    run->glyphs,
	    run->glyphCount,
	    run->advances,
	    NULL,
	    run->offsets);

	for (int g = 0; g < run->glyphCount; g++) {
	    runWidth += run->advances[g];
	}

	x += cosA * runWidth;
	y -= sinA * runWidth;

	if (hAngled) {
	    DeleteObject(hAngled);
	}
    }

    SetBkMode(hdc, oldBkMode);
    SelectObject(hdc, oldFont);
    TkWinFreeShapedRuns(runs, nRuns);
    Tcl_DStringFree(&uniStr);
}



/*
 *---------------------------------------------------------------------------
 *
 * InitFont --
 *
 *	Helper for TkpGetNativeFont() and TkpGetFontFromAttributes().
 *	Initializes the memory for a new WinFont that wraps the
 *	platform-specific data.
 *
 *	The caller is responsible for initializing the fields of the WinFont
 *	that are used exclusively by the generic TkFont code, and for
 *	releasing those fields before calling TkpDeleteFont().
 *
 * Results:
 *	Fills the WinFont structure.
 *
 * Side effects:
 *	Memory allocated.
 *
 *---------------------------------------------------------------------------
 */

static void
InitFont(
    Tk_Window tkwin,		/* Main window of interp in which font will be
				 * used, for getting HDC. */
    HFONT hFont,		/* Windows token for font. */
    int overstrike,		/* The overstrike attribute of logfont used to
				 * allocate this font. For some reason, the
				 * TEXTMETRICWs may contain incorrect info in
				 * the tmStruckOut field. */
    WinFont *fontPtr)		/* Filled with information constructed from
				 * the above arguments. */
{
    HDC hdc;
    HWND hwnd;
    HFONT oldFont;
    TEXTMETRICW tm;
    Window window;
    TkFontMetrics *fmPtr;
    Tcl_Encoding encoding;
    Tcl_DString faceString;
    TkFontAttributes *faPtr;
    WCHAR buf[LF_FACESIZE];

    window = Tk_WindowId(tkwin);
    hwnd = (window == None) ? NULL : TkWinGetHWND(window);
    hdc = GetDC(hwnd);
    oldFont = (HFONT)SelectObject(hdc, hFont);

    GetTextMetricsW(hdc, &tm);

    GetTextFaceW(hdc, LF_FACESIZE, buf);
    Tcl_DStringInit(&faceString);
    Tcl_WCharToUtfDString(buf, wcslen(buf), &faceString);

    fontPtr->font.fid	= (Font) fontPtr;
    fontPtr->hwnd	= hwnd;
    fontPtr->pixelSize	= tm.tmHeight - tm.tmInternalLeading;

    faPtr		= &fontPtr->font.fa;
    faPtr->family	= Tk_GetUid(Tcl_DStringValue(&faceString));

    faPtr->size =
	TkFontGetPoints(tkwin,  (double)-(fontPtr->pixelSize));
    faPtr->weight =
	    (tm.tmWeight > FW_MEDIUM) ? TK_FW_BOLD : TK_FW_NORMAL;
    faPtr->slant	= (tm.tmItalic != 0) ? TK_FS_ITALIC : TK_FS_ROMAN;
    faPtr->underline	= (tm.tmUnderlined != 0) ? 1 : 0;
    faPtr->overstrike	= overstrike;

    fmPtr		= &fontPtr->font.fm;
    fmPtr->ascent	= tm.tmAscent;
    fmPtr->descent	= tm.tmDescent;
    fmPtr->maxWidth	= tm.tmMaxCharWidth;
    fmPtr->fixed	= !(tm.tmPitchAndFamily & TMPF_FIXED_PITCH);

    fontPtr->numSubFonts	= 1;
    fontPtr->subFontArray	= fontPtr->staticSubFonts;
    memset(fontPtr->staticScriptCaches, 0, sizeof(fontPtr->staticScriptCaches));
    fontPtr->scriptCacheArray	= fontPtr->staticScriptCaches;
    InitSubFont(hdc, hFont, 1, &fontPtr->subFontArray[0]);

    encoding = fontPtr->subFontArray[0].familyPtr->encoding;
    if (encoding == TkWinGetUnicodeEncoding()) {
	GetCharWidthW(hdc, 0, BASE_CHARS - 1, fontPtr->widths);
    } else {
	GetCharWidthA(hdc, 0, BASE_CHARS - 1, fontPtr->widths);
    }
    Tcl_DStringFree(&faceString);

    SelectObject(hdc, oldFont);
    ReleaseDC(hwnd, hdc);
}

/*
 *-------------------------------------------------------------------------
 *
 * ReleaseFont --
 *
 *	Called to release the windows-specific contents of a TkFont. The
 *	caller is responsible for freeing the memory used by the font itself.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is freed.
 *
 *---------------------------------------------------------------------------
 */

static void
ReleaseFont(
    WinFont *fontPtr)		/* The font to delete. */
{
    int i;

    for (i = 0; i < fontPtr->numSubFonts; i++) {
	ReleaseSubFont(&fontPtr->subFontArray[i]);
	ScriptFreeCache(&fontPtr->scriptCacheArray[i]);
    }
    if (fontPtr->subFontArray != fontPtr->staticSubFonts) {
	Tcl_Free(fontPtr->subFontArray);
    }
    if (fontPtr->scriptCacheArray != fontPtr->staticScriptCaches) {
	Tcl_Free(fontPtr->scriptCacheArray);
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * InitSubFont --
 *
 *	Wrap a screen font and load the FontFamily that represents it. Used to
 *	prepare a SubFont so that characters can be mapped from UTF-8 to the
 *	charset of the font.
 *
 * Results:
 *	The subFontPtr is filled with information about the font.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

/*
 * FOURCC_TAG --
 *
 *   Build a big-endian DWORD from four characters, as required by
 *   GetFontData() for OpenType table tags.  The Windows GDI expects
 *   the tag in the byte order used in the font file, which is big-endian.
 *   So "COLR" becomes 0x434F4C52.
 */
#ifndef FOURCC_TAG
#define FOURCC_TAG(a,b,c,d) \
    ((DWORD)(BYTE)(a) << 24 | (DWORD)(BYTE)(b) << 16 | \
     (DWORD)(BYTE)(c) << 8  | (DWORD)(BYTE)(d))
#endif

static inline void
InitSubFont(
    HDC hdc,			/* HDC in which font can be selected. */
    HFONT hFont,		/* The screen font. */
    int base,			/* Non-zero if this SubFont is being used as
				 * the base font for a font object. */
    SubFont *subFontPtr)	/* Filled with SubFont constructed from above
				 * attributes. */
{

    subFontPtr->hFont0	    = hFont;
    subFontPtr->familyPtr   = AllocFontFamily(hdc, hFont, base);
    subFontPtr->fontMap	    = subFontPtr->familyPtr->fontMap;
    subFontPtr->hFontAngled = NULL;
    subFontPtr->angle	    = 0.0;

    SelectObject(hdc, hFont);
}

/*
 *-------------------------------------------------------------------------
 *
 * ReleaseSubFont --
 *
 *	Called to release the contents of a SubFont. The caller is responsible
 *	for freeing the memory used by the SubFont itself.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory and resources are freed.
 *
 *---------------------------------------------------------------------------
 */

static inline void
ReleaseSubFont(
    SubFont *subFontPtr)	/* The SubFont to delete. */
{
    DeleteObject(subFontPtr->hFont0);
    if (subFontPtr->hFontAngled) {
	DeleteObject(subFontPtr->hFontAngled);
    }
    FreeFontFamily(subFontPtr->familyPtr);
}

/*
 *-------------------------------------------------------------------------
 *
 * AllocFontFamily --
 *
 *	Find the FontFamily structure associated with the given font name. The
 *	information should be stored by the caller in a SubFont and used when
 *	determining if that SubFont supports a character.
 *
 *	Cannot use the string name used to construct the font as the key,
 *	because the capitalization may not be canonical. Therefore use the
 *	face name actually retrieved from the font metrics as the key.
 *
 * Results:
 *	A pointer to a FontFamily. The reference count in the FontFamily is
 *	automatically incremented. When the SubFont is released, the reference
 *	count is decremented. When no SubFont is using this FontFamily, it may
 *	be deleted.
 *
 * Side effects:
 *	A new FontFamily structure will be allocated if this font family has
 *	not been seen. TrueType character existence metrics are loaded into
 *	the FontFamily structure.
 *
 *-------------------------------------------------------------------------
 */

static FontFamily *
AllocFontFamily(
    HDC hdc,			/* HDC in which font can be selected. */
    HFONT hFont,		/* Screen font whose FontFamily is to be
				 * returned. */
    TCL_UNUSED(int))			/* Non-zero if this font family is to be used
				 * in the base font of a font object. */
{
    Tk_Uid faceName;
    FontFamily *familyPtr;
    Tcl_DString faceString;
    Tcl_Encoding encoding;
    WCHAR buf[LF_FACESIZE];
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    hFont = (HFONT)SelectObject(hdc, hFont);
    GetTextFaceW(hdc, LF_FACESIZE, buf);
    Tcl_DStringInit(&faceString);
    Tcl_WCharToUtfDString(buf, wcslen(buf), &faceString);
    faceName = Tk_GetUid(Tcl_DStringValue(&faceString));
    Tcl_DStringFree(&faceString);
    hFont = (HFONT)SelectObject(hdc, hFont);

    familyPtr = tsdPtr->fontFamilyList;
    for ( ; familyPtr != NULL; familyPtr = familyPtr->nextPtr) {
	if (familyPtr->faceName == faceName) {
	    familyPtr->refCount++;
	    return familyPtr;
	}
    }

    familyPtr = (FontFamily *)Tcl_Alloc(sizeof(FontFamily));
    memset(familyPtr, 0, sizeof(FontFamily));
    familyPtr->nextPtr = tsdPtr->fontFamilyList;
    tsdPtr->fontFamilyList = familyPtr;

    /*
     * Set key for this FontFamily.
     */

    familyPtr->faceName = faceName;

    /*
     * An initial refCount of 2 means that FontFamily information will persist
     * even when the SubFont that loaded the FontFamily is released. Change it
     * to 1 to cause FontFamilies to be unloaded when not in use.
     */

    familyPtr->refCount = 2;

    familyPtr->segCount = LoadFontRanges(hdc, hFont, &familyPtr->startCount,
	    &familyPtr->endCount, &familyPtr->isSymbolFont,
	    &familyPtr->startGroup, &familyPtr->endGroup,
	    &familyPtr->groupCount);

    encoding = NULL;
    if (familyPtr->isSymbolFont) {
	/*
	 * Symbol fonts are handled specially. For instance, Unicode 0393
	 * (GREEK CAPITAL GAMMA) must be mapped to Symbol character 0047
	 * (GREEK CAPITAL GAMMA), because the Symbol font doesn't have a GREEK
	 * CAPITAL GAMMA at location 0393. If Tk interpreted the Symbol font
	 * using the Unicode encoding, it would decide that the Symbol font
	 * has no GREEK CAPITAL GAMMA, because the Symbol encoding (of course)
	 * reports that character 0393 doesn't exist.
	 *
	 * With non-symbol Windows fonts, such as Times New Roman, if the font
	 * has a GREEK CAPITAL GAMMA, it will be found in the correct Unicode
	 * location (0393); the GREEK CAPITAL GAMMA will not be off hiding at
	 * some other location.
	 */

	encoding = Tcl_GetEncoding(NULL, faceName);
    }

    if (encoding == NULL) {
	encoding = TkWinGetUnicodeEncoding();
	familyPtr->textOutProc =
	    (BOOL (WINAPI *)(HDC, int, int, WCHAR *, int)) TextOutW;
	familyPtr->getTextExtentPoint32Proc =
	    (BOOL (WINAPI *)(HDC, WCHAR *, int, LPSIZE)) GetTextExtentPoint32W;
	familyPtr->isWideFont = 1;
    } else {
	familyPtr->textOutProc =
	    (BOOL (WINAPI *)(HDC, int, int, WCHAR *, int)) TextOutA;
	familyPtr->getTextExtentPoint32Proc =
	    (BOOL (WINAPI *)(HDC, WCHAR *, int, LPSIZE)) GetTextExtentPoint32A;
	familyPtr->isWideFont = 0;
    }

    familyPtr->encoding = encoding;

    return familyPtr;
}

/*
 *-------------------------------------------------------------------------
 *
 * FreeFontFamily --
 *
 *	Called to free a FontFamily when the SubFont is finished using it.
 *	Frees the contents of the FontFamily and the memory used by the
 *	FontFamily itself.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

static void
FreeFontFamily(
    FontFamily *familyPtr)	/* The FontFamily to delete. */
{
    int i;
    FontFamily **familyPtrPtr;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (familyPtr == NULL) {
	return;
    }
    if (familyPtr->refCount-- > 1) {
	return;
    }
    for (i = 0; i < FONTMAP_PAGES; i++) {
	if (familyPtr->fontMap[i] != NULL) {
	    Tcl_Free(familyPtr->fontMap[i]);
	}
    }
    if (familyPtr->startCount != NULL) {
	Tcl_Free(familyPtr->startCount);
    }
    if (familyPtr->endCount != NULL) {
	Tcl_Free(familyPtr->endCount);
    }
    if (familyPtr->startGroup != NULL) {
	Tcl_Free(familyPtr->startGroup);
    }
    if (familyPtr->endGroup != NULL) {
	Tcl_Free(familyPtr->endGroup);
    }
    if (familyPtr->encoding != TkWinGetUnicodeEncoding()) {
	Tcl_FreeEncoding(familyPtr->encoding);
    }

    /*
     * Delete from list.
     */

    for (familyPtrPtr = &tsdPtr->fontFamilyList; ; ) {
	if (*familyPtrPtr == familyPtr) {
	    *familyPtrPtr = familyPtr->nextPtr;
	    break;
	}
	familyPtrPtr = &(*familyPtrPtr)->nextPtr;
    }

    Tcl_Free(familyPtr);
}

/*
 *-------------------------------------------------------------------------
 *
 * FindSubFontForChar --
 *
 *	Determine which screen font is necessary to use to display the given
 *	character. If the font object does not have a screen font that can
 *	display the character, another screen font may be loaded into the font
 *	object, following a set of preferred fallback rules.
 *
 *	Note: For characters below BASE_CHARS, we now check the base font's
 *	coverage via FontMapLookup. If the base font cannot display the
 *	character, we continue to the fallback search. This ensures that even
 *	basic ASCII can be provided by a fallback font if the base font lacks
 *	it.
 *
 * Results:
 *	The return value is the SubFont to use to display the given character.
 *
 * Side effects:
 *	The contents of fontPtr are modified to cache the results of the
 *	lookup and remember any SubFonts that were dynamically loaded.
 *
 *-------------------------------------------------------------------------
 */

static SubFont *
FindSubFontForChar(
    WinFont *fontPtr,		/* The font object with which the character
				 * will be displayed. */
    int ch,			/* The Unicode character to be displayed. */
    SubFont **subFontPtrPtr)	/* Pointer to var to be fixed up if we
				 * reallocate the subfont table. */
{
    HDC hdc;
    int i, j, k;
    CanUse canUse;
    const char *const *aliases;
    const char *const *anyFallbacks;
    const char *const *const *fontFallbacks;
    const char *fallbackName;
    SubFont *subFontPtr;
    Tcl_DString ds;

    /* For characters >= FONTMAP_NUMCHARS, just use base font. */
    if (ch >= FONTMAP_NUMCHARS) {
	return &fontPtr->subFontArray[0];
    }

    /* For characters below BASE_CHARS, check if base font can display them. */
    if (ch < BASE_CHARS) {
	if (FontMapLookup(&fontPtr->subFontArray[0], ch)) {
	    return &fontPtr->subFontArray[0];
	}
	/* Otherwise fall through to fallback search. */
    }

    /* First, see if any already-loaded subfont can display the character. */
    for (i = 0; i < fontPtr->numSubFonts; i++) {
	if (FontMapLookup(&fontPtr->subFontArray[i], ch)) {
	    return &fontPtr->subFontArray[i];
	}
    }

    /*
     * Keep track of all face names that we check, so we don't check some name
     * multiple times if it can be reached by multiple paths.
     */

    Tcl_DStringInit(&ds);
    hdc = GetDC(fontPtr->hwnd);

    aliases = TkFontGetAliasList(fontPtr->font.fa.family);

    fontFallbacks = TkFontGetFallbacks();
    for (i = 0; fontFallbacks[i] != NULL; i++) {
	for (j = 0; fontFallbacks[i][j] != NULL; j++) {
	    fallbackName = fontFallbacks[i][j];
	    if (strcasecmp(fallbackName, fontPtr->font.fa.family) == 0) {
		/*
		 * If the base font has a fallback...
		 */

		goto tryfallbacks;
	    } else if (aliases != NULL) {
		/*
		 * Or if an alias for the base font has a fallback...
		 */

		for (k = 0; aliases[k] != NULL; k++) {
		    if (strcasecmp(aliases[k], fallbackName) == 0) {
			goto tryfallbacks;
		    }
		}
	    }
	}
	continue;

	/*
	 * ...then see if we can use one of the fallbacks, or an alias for one
	 * of the fallbacks.
	 */

    tryfallbacks:
	for (j = 0; fontFallbacks[i][j] != NULL; j++) {
	    fallbackName = fontFallbacks[i][j];
	    subFontPtr = CanUseFallbackWithAliases(hdc, fontPtr, fallbackName,
		    ch, &ds, subFontPtrPtr);
	    if (subFontPtr != NULL) {
		goto end;
	    }
	}
    }

    /*
     * See if we can use something from the global fallback list.
     */

    anyFallbacks = TkFontGetGlobalClass();
    for (i = 0; anyFallbacks[i] != NULL; i++) {
	fallbackName = anyFallbacks[i];
	subFontPtr = CanUseFallbackWithAliases(hdc, fontPtr, fallbackName,
		ch, &ds, subFontPtrPtr);
	if (subFontPtr != NULL) {
	    goto end;
	}
    }

    /*
     * Try all face names available in the whole system until we find one that
     * can be used.
     */

    canUse.hdc = hdc;
    canUse.fontPtr = fontPtr;
    canUse.nameTriedPtr = &ds;
    canUse.ch = ch;
    canUse.subFontPtr = NULL;
    canUse.subFontPtrPtr = subFontPtrPtr;
    EnumFontFamiliesW(hdc, NULL, (FONTENUMPROCW) WinFontCanUseProc,
	    (LPARAM) &canUse);
    subFontPtr = canUse.subFontPtr;

  end:
    Tcl_DStringFree(&ds);

    if (subFontPtr == NULL) {
	/*
	 * No font can display this character. We will use the base font and
	 * have it display the "unknown" character.
	 */

	subFontPtr = &fontPtr->subFontArray[0];
	FontMapInsert(subFontPtr, ch);
    }
    ReleaseDC(fontPtr->hwnd, hdc);
    return subFontPtr;
}

static int CALLBACK
WinFontCanUseProc(
    ENUMLOGFONTW *lfPtr,		/* Logical-font data. */
    TCL_UNUSED(NEWTEXTMETRIC *),	/* Physical-font data (not used). */
    TCL_UNUSED(int),		/* Type of font (not used). */
    LPARAM lParam)		/* Result object to hold result. */
{
    int ch;
    HDC hdc;
    WinFont *fontPtr;
    CanUse *canUsePtr;
    char *fallbackName;
    SubFont *subFontPtr;
    Tcl_DString faceString;
    Tcl_DString *nameTriedPtr;

    canUsePtr	    = (CanUse *) lParam;
    ch		    = canUsePtr->ch;
    hdc		    = canUsePtr->hdc;
    fontPtr	    = canUsePtr->fontPtr;
    nameTriedPtr    = canUsePtr->nameTriedPtr;

    fallbackName = (char *) lfPtr->elfLogFont.lfFaceName;
    Tcl_DStringInit(&faceString);
    Tcl_WCharToUtfDString((WCHAR *)fallbackName, wcslen((WCHAR *)fallbackName), &faceString);
    fallbackName = Tcl_DStringValue(&faceString);

    if (SeenName(fallbackName, nameTriedPtr) == 0) {
	subFontPtr = CanUseFallback(hdc, fontPtr, fallbackName, ch,
		canUsePtr->subFontPtrPtr);
	if (subFontPtr != NULL) {
	    canUsePtr->subFontPtr = subFontPtr;
	    Tcl_DStringFree(&faceString);
	    return 0;
	}
    }
    Tcl_DStringFree(&faceString);
    return 1;
}

/*
 *-------------------------------------------------------------------------
 *
 * FontMapLookup --
 *
 *	See if the screen font can display the given character.
 *
 * Results:
 *	The return value is 0 if the screen font cannot display the character,
 *	non-zero otherwise.
 *
 * Side effects:
 *	New pages are added to the font mapping cache whenever the character
 *	belongs to a page that hasn't been seen before. When a page is loaded,
 *	information about all the characters on that page is stored, not just
 *	for the single character in question.
 *
 *-------------------------------------------------------------------------
 */

static int
FontMapLookup(
    SubFont *subFontPtr,	/* Contains font mapping cache to be queried
				 * and possibly updated. */
    int ch)			/* Character to be tested. */
{
    int row, bitOffset;

    if (ch < 0 || ch >= FONTMAP_NUMCHARS) {
	return 0;
    }

    row = ch >> FONTMAP_SHIFT;
    if (subFontPtr->fontMap[row] == NULL) {
	FontMapLoadPage(subFontPtr, row);
    }
    bitOffset = ch & (FONTMAP_BITSPERPAGE - 1);
    return (subFontPtr->fontMap[row][bitOffset >> 3] >> (bitOffset & 7)) & 1;
}

/*
 *-------------------------------------------------------------------------
 *
 * FontMapInsert --
 *
 *	Tell the font mapping cache that the given screen font should be used
 *	to display the specified character. This is called when no font on the
 *	system can be be found that can display that character; we lie to the
 *	font and tell it that it can display the character, otherwise we would
 *	end up re-searching the entire fallback hierarchy every time that
 *	character was seen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	New pages are added to the font mapping cache whenever the character
 *	belongs to a page that hasn't been seen before. When a page is loaded,
 *	information about all the characters on that page is stored, not just
 *	for the single character in question.
 *
 *-------------------------------------------------------------------------
 */

static void
FontMapInsert(
    SubFont *subFontPtr,	/* Contains font mapping cache to be
				 * updated. */
    int ch)			/* Character to be added to cache. */
{
    int row, bitOffset;

    if (ch >= 0 && ch < FONTMAP_NUMCHARS) {
	row = ch >> FONTMAP_SHIFT;
	if (subFontPtr->fontMap[row] == NULL) {
	    FontMapLoadPage(subFontPtr, row);
	}
	bitOffset = ch & (FONTMAP_BITSPERPAGE - 1);
	subFontPtr->fontMap[row][bitOffset >> 3] |= 1 << (bitOffset & 7);
    }
}

/*
 *-------------------------------------------------------------------------
 *
 * FontMapLoadPage --
 *
 *	Load information about all the characters on a given page. This
 *	information consists of one bit per character that indicates whether
 *	the associated HFONT can (1) or cannot (0) display the characters on
 *	the page.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory allocated.
 *
 *-------------------------------------------------------------------------
 */

static void
FontMapLoadPage(
    SubFont *subFontPtr,	/* Contains font mapping cache to be
				 * updated. */
    int row)			/* Index of the page to be loaded into the
				 * cache. */
{
    FontFamily *familyPtr;
    Tcl_Encoding encoding;
    int i, j, bitOffset, end, segCount;
    USHORT *startCount, *endCount;
    char buf[16], src[6];

    subFontPtr->fontMap[row] = (char *)Tcl_Alloc(FONTMAP_BITSPERPAGE / 8);
    memset(subFontPtr->fontMap[row], 0, FONTMAP_BITSPERPAGE / 8);

    familyPtr = subFontPtr->familyPtr;
    encoding = familyPtr->encoding;

    if (familyPtr->encoding == TkWinGetUnicodeEncoding()) {
	/*
	 * Font is Unicode. Few fonts are going to have all characters, so
	 * examine the TrueType character existence metrics to determine what
	 * characters actually exist in this font.
	 *
	 * For rows in the supplementary planes (codepoints >= U+10000) use
	 * the format-12 group table which carries full 32-bit codepoints.
	 * The format-4 USHORT arrays are clamped to U+FFFF and cannot
	 * represent emoji or any other supplementary-plane character.
	 */

	int rowStart = row << FONTMAP_SHIFT;
	int rowEnd   = rowStart + FONTMAP_BITSPERPAGE; /* exclusive */

	if (rowStart >= 0x10000 && familyPtr->groupCount > 0) {
	    /*
	     * Supplementary plane row: walk format-12 groups.
	     * Each group covers [startGroup[g], endGroup[g]] inclusive.
	     * We only care about the intersection with [rowStart, rowEnd).
	     */
	    int gc = familyPtr->groupCount;
	    ULONG *sg = familyPtr->startGroup;
	    ULONG *eg = familyPtr->endGroup;

	    for (j = 0; j < gc; j++) {
		if ((int)eg[j] < rowStart || (int)sg[j] >= rowEnd) {
		    continue;
		}
		int lo = ((int)sg[j] > rowStart) ? (int)sg[j] : rowStart;
		int hi = ((int)eg[j] < rowEnd)   ? (int)eg[j] : rowEnd - 1;
		for (i = lo; i <= hi; i++) {
		    bitOffset = i & (FONTMAP_BITSPERPAGE - 1);
		    subFontPtr->fontMap[row][bitOffset >> 3] |=
			    1 << (bitOffset & 7);
		}
	    }
	} else {
	    /*
	     * BMP row (codepoints U+0000–U+FFFF): use the format-4
	     * USHORT segment arrays as before.
	     */
	    segCount    = familyPtr->segCount;
	    startCount  = familyPtr->startCount;
	    endCount    = familyPtr->endCount;

	    j = 0;
	    end = rowEnd;
	    for (i = rowStart; i < end; i++) {
		for ( ; j < segCount; j++) {
		    if (endCount[j] >= i) {
			if (startCount[j] <= i) {
			    bitOffset = i & (FONTMAP_BITSPERPAGE - 1);
			    subFontPtr->fontMap[row][bitOffset >> 3] |=
				    1 << (bitOffset & 7);
			}
			break;
		    }
		}
	    }
	}
    } else if (familyPtr->isSymbolFont) {
	/*
	 * Assume that a symbol font with a known encoding has all the
	 * characters that its encoding claims it supports.
	 *
	 * The test for "encoding == unicodeEncoding" must occur before this
	 * case, to catch all symbol fonts (such as {Comic Sans MS} or
	 * Wingdings) for which we don't have encoding information; those
	 * symbol fonts are treated as if they were in the Unicode encoding
	 * and their symbolic character existence metrics are treated as if
	 * they were Unicode character existence metrics. This way, although
	 * we don't know the proper Unicode -> symbol font mapping, we can
	 * install the symbol font as the base font and access its glyphs.
	 */

	end = (row + 1) << FONTMAP_SHIFT;
	for (i = row << FONTMAP_SHIFT; i < end; i++) {
	    if (Tcl_UtfToExternal(NULL, encoding, src,
		    Tcl_UniCharToUtf(i, src), TCL_ENCODING_PROFILE_STRICT, NULL,
		    buf, sizeof(buf), NULL, NULL, NULL) != TCL_OK) {
		continue;
	    }
	    bitOffset = i & (FONTMAP_BITSPERPAGE - 1);
	    subFontPtr->fontMap[row][bitOffset >> 3] |= 1 << (bitOffset & 7);
	}
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * CanUseFallbackWithAliases --
 *
 *	Helper function for FindSubFontForChar. Determine if the specified
 *	face name (or an alias of the specified face name) can be used to
 *	construct a screen font that can display the given character.
 *
 * Results:
 *	See CanUseFallback().
 *
 * Side effects:
 *	If the name and/or one of its aliases was rejected, the rejected
 *	string is recorded in nameTriedPtr so that it won't be tried again.
 *
 *---------------------------------------------------------------------------
 */

static SubFont *
CanUseFallbackWithAliases(
    HDC hdc,			/* HDC in which font can be selected. */
    WinFont *fontPtr,		/* The font object that will own the new
				 * screen font. */
    const char *faceName,	/* Desired face name for new screen font. */
    int ch,			/* The Unicode character that the new screen
				 * font must be able to display. */
    Tcl_DString *nameTriedPtr,	/* Records face names that have already been
				 * tried. It is possible for the same face
				 * name to be queried multiple times when
				 * trying to find a suitable screen font. */
    SubFont **subFontPtrPtr)	/* Variable to fixup if we reallocate the
				 * array of subfonts. */
{
    int i;
    const char *const *aliases;
    SubFont *subFontPtr;

    if (SeenName(faceName, nameTriedPtr) == 0) {
	subFontPtr = CanUseFallback(hdc, fontPtr, faceName, ch, subFontPtrPtr);
	if (subFontPtr != NULL) {
	    return subFontPtr;
	}
    }
    aliases = TkFontGetAliasList(faceName);
    if (aliases != NULL) {
	for (i = 0; aliases[i] != NULL; i++) {
	    if (SeenName(aliases[i], nameTriedPtr) == 0) {
		subFontPtr = CanUseFallback(hdc, fontPtr, aliases[i], ch,
			subFontPtrPtr);
		if (subFontPtr != NULL) {
		    return subFontPtr;
		}
	    }
	}
    }
    return NULL;
}

/*
 *---------------------------------------------------------------------------
 *
 * SeenName --
 *
 *	Used to determine we have already tried and rejected the given face
 *	name when looking for a screen font that can support some Unicode
 *	character.
 *
 * Results:
 *	The return value is 0 if this face name has not already been seen,
 *	non-zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static int
SeenName(
    const char *name,		/* The name to check. */
    Tcl_DString *dsPtr)		/* Contains names that have already been
				 * seen. */
{
    const char *seen, *end;

    seen = Tcl_DStringValue(dsPtr);
    end = seen + Tcl_DStringLength(dsPtr);
    while (seen < end) {
	if (strcasecmp(seen, name) == 0) {
	    return 1;
	}
	seen += strlen(seen) + 1;
    }
    Tcl_DStringAppend(dsPtr, name, (int) (strlen(name) + 1));
    return 0;
}

/*
 *-------------------------------------------------------------------------
 *
 * CanUseFallback --
 *
 *	If the specified screen font has not already been loaded into the font
 *	object, determine if it can display the given character.
 *
 * Results:
 *	The return value is a pointer to a newly allocated SubFont, owned by
 *	the font object. This SubFont can be used to display the given
 *	character. The SubFont represents the screen font with the base set of
 *	font attributes from the font object, but using the specified font
 *	name. NULL is returned if the font object already holds a reference to
 *	the specified physical font or if the specified physical font cannot
 *	display the given character.
 *
 * Side effects:
 *	The font object's subFontArray is updated to contain a reference to
 *	the newly allocated SubFont.
 *
 *-------------------------------------------------------------------------
 */

static SubFont *
CanUseFallback(
    HDC hdc,			/* HDC in which font can be selected. */
    WinFont *fontPtr,		/* The font object that will own the new
				 * screen font. */
    const char *faceName,	/* Desired face name for new screen font. */
    int ch,			/* The Unicode character that the new screen
				 * font must be able to display. */
    SubFont **subFontPtrPtr)	/* Variable to fix-up if we realloc the array
				 * of subfonts. */
{
    int i;
    HFONT hFont;
    SubFont subFont;

    if (FamilyExists(hdc, faceName) == 0) {
	return NULL;
    }

    /*
     * Skip all fonts we've already used.
     */

    for (i = 0; i < fontPtr->numSubFonts; i++) {
	if (faceName == fontPtr->subFontArray[i].familyPtr->faceName) {
	    return NULL;
	}
    }

    /*
     * Load this font and see if it has the desired character.
     */

    hFont = GetScreenFont(&fontPtr->font.fa, faceName, fontPtr->pixelSize,
	    0.0);
    InitSubFont(hdc, hFont, 0, &subFont);
    if (((ch < 256) && (subFont.familyPtr->isSymbolFont))
	    || (FontMapLookup(&subFont, ch) == 0)) {
	/*
	 * Don't use a symbol font as a fallback font for characters below
	 * 256.
	 */

	ReleaseSubFont(&subFont);
	return NULL;
    }

    if (fontPtr->numSubFonts >= SUBFONT_SPACE) {
	SubFont *newPtr;
	SCRIPT_CACHE *newCachePtr;
	int newCount = fontPtr->numSubFonts + 1;

	newPtr = (SubFont *)Tcl_Alloc(sizeof(SubFont) * newCount);
	memcpy(newPtr, fontPtr->subFontArray,
		fontPtr->numSubFonts * sizeof(SubFont));
	if (fontPtr->subFontArray != fontPtr->staticSubFonts) {
	    Tcl_Free(fontPtr->subFontArray);
	}

	newCachePtr = (SCRIPT_CACHE *)Tcl_Alloc(sizeof(SCRIPT_CACHE) * newCount);
	memcpy(newCachePtr, fontPtr->scriptCacheArray,
		fontPtr->numSubFonts * sizeof(SCRIPT_CACHE));
	if (fontPtr->scriptCacheArray != fontPtr->staticScriptCaches) {
	    Tcl_Free(fontPtr->scriptCacheArray);
	}
	/* Zero the new slot so Uniscribe treats it as uninitialised. */
	memset(&newCachePtr[fontPtr->numSubFonts], 0, sizeof(SCRIPT_CACHE));

	/*
	 * Fix up the variable pointed to by subFontPtrPtr so it still points
	 * into the live array. [Bug 618872]
	 */

	*subFontPtrPtr = newPtr + (*subFontPtrPtr - fontPtr->subFontArray);
	fontPtr->subFontArray    = newPtr;
	fontPtr->scriptCacheArray = newCachePtr;
    } else {
	/* Still within inline storage; zero the next cache slot. */
	memset(&fontPtr->scriptCacheArray[fontPtr->numSubFonts], 0,
		sizeof(SCRIPT_CACHE));
    }
    fontPtr->subFontArray[fontPtr->numSubFonts] = subFont;
    fontPtr->numSubFonts++;
    return &fontPtr->subFontArray[fontPtr->numSubFonts - 1];
}

/*
 *---------------------------------------------------------------------------
 *
 * GetScreenFont --
 *
 *	Given the name and other attributes, construct an HFONT. This is where
 *	all the alias and fallback substitution bottoms out.
 *
 * Results:
 *	The screen font that corresponds to the attributes.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static HFONT
GetScreenFont(
    const TkFontAttributes *faPtr,
				/* Desired font attributes for new HFONT. */
    const char *faceName,	/* Overrides font family specified in font
				 * attributes. */
    int pixelSize,		/* Overrides size specified in font
				 * attributes. */
    double angle)		/* What is the desired orientation of the
				 * font. */
{
    HFONT hFont;
    LOGFONTW lf;

    memset(&lf, 0, sizeof(lf));
    lf.lfHeight		= -pixelSize;
    lf.lfWidth		= 0;
    lf.lfEscapement	= ROUND16(angle * 10);
    lf.lfOrientation	= ROUND16(angle * 10);
    lf.lfWeight = (faPtr->weight == TK_FW_NORMAL) ? FW_NORMAL : FW_BOLD;
    lf.lfItalic		= (BYTE)faPtr->slant;
    lf.lfUnderline	= (BYTE)faPtr->underline;
    lf.lfStrikeOut	= (BYTE)faPtr->overstrike;
    lf.lfCharSet	= DEFAULT_CHARSET;
    lf.lfOutPrecision	= OUT_TT_PRECIS;
    lf.lfClipPrecision	= CLIP_DEFAULT_PRECIS;
    lf.lfQuality	= DEFAULT_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;

    MultiByteToWideChar(CP_UTF8, 0, faceName, -1, lf.lfFaceName, LF_FACESIZE);
    lf.lfFaceName[LF_FACESIZE-1] = 0;
    hFont = CreateFontIndirectW(&lf);
    return hFont;
}

/*
 *-------------------------------------------------------------------------
 *
 * FamilyExists, FamilyOrAliasExists, WinFontExistsProc --
 *
 *	Determines if any physical screen font exists on the system with the
 *	given family name. If the family exists, then it should be possible to
 *	construct some physical screen font with that family name.
 *
 * Results:
 *	The return value is 0 if the specified font family does not exist,
 *	non-zero otherwise.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

static int
FamilyExists(
    HDC hdc,			/* HDC in which font family will be used. */
    const char *faceName)	/* Font family to query. */
{
    int result;
    Tcl_DString faceString;

    Tcl_DStringInit(&faceString);
    Tcl_UtfToWCharDString(faceName, TCL_INDEX_NONE, &faceString);

    /*
     * If the family exists, WinFontExistProc() will be called and
     * EnumFontFamilies() will return whatever WinFontExistProc() returns. If
     * the family doesn't exist, EnumFontFamilies() will just return a
     * non-zero value.
     */

    result = EnumFontFamiliesW(hdc, (WCHAR *)Tcl_DStringValue(&faceString),
	    (FONTENUMPROCW) WinFontExistProc, 0);
    Tcl_DStringFree(&faceString);
    return (result == 0);
}

static const char *
FamilyOrAliasExists(
    HDC hdc,
    const char *faceName)
{
    const char *const *aliases;
    int i;

    if (FamilyExists(hdc, faceName) != 0) {
	return faceName;
    }
    aliases = TkFontGetAliasList(faceName);
    if (aliases != NULL) {
	for (i = 0; aliases[i] != NULL; i++) {
	    if (FamilyExists(hdc, aliases[i]) != 0) {
		return aliases[i];
	    }
	}
    }
    return NULL;
}

static int CALLBACK
WinFontExistProc(
    TCL_UNUSED(ENUMLOGFONTW *),		/* Logical-font data. */
    TCL_UNUSED(NEWTEXTMETRIC *),	/* Physical-font data (not used). */
    TCL_UNUSED(int),		/* Type of font (not used). */
    TCL_UNUSED(LPARAM))		/* EnumFontData to hold result. */
{
    return 0;
}

/*
 * The following data structures are used when querying a TrueType font file
 * to determine which characters the font supports.
 */

#pragma pack(1)			/* Structures are byte aligned in file. */

#define CMAPHEX 0x636d6170	/* Key for character map resource. */

typedef struct CMAPTABLE {
    USHORT version;		/* Table version number (0). */
    USHORT numTables;		/* Number of encoding tables following. */
} CMAPTABLE;

typedef struct ENCODINGTABLE {
    USHORT platform;		/* Platform for which data is targeted. 3
				 * means data is for Windows. */
    USHORT encoding;		/* How characters in font are encoded. 1 means
				 * that the following subtable is keyed based
				 * on Unicode. */
    ULONG offset;		/* Byte offset from beginning of CMAPTABLE to
				 * the subtable for this encoding. */
} ENCODINGTABLE;

typedef struct ANYTABLE {
    USHORT format;		/* Format number. */
    USHORT length;		/* The actual length in bytes of this
				 * subtable. */
    USHORT version;		/* Version number (starts at 0). */
} ANYTABLE;

typedef struct BYTETABLE {
    USHORT format;		/* Format number is set to 0. */
    USHORT length;		/* The actual length in bytes of this
				 * subtable. */
    USHORT version;		/* Version number (starts at 0). */
    BYTE glyphIdArray[256];	/* Array that maps up to 256 single-byte char
				 * codes to glyph indices. */
} BYTETABLE;

typedef struct SUBHEADER {
    USHORT firstCode;		/* First valid low byte for subHeader. */
    USHORT entryCount;		/* Number valid low bytes for subHeader. */
    SHORT idDelta;		/* Constant adder to get base glyph index. */
    USHORT idRangeOffset;	/* Byte offset from here to appropriate
				 * glyphIndexArray. */
} SUBHEADER;

typedef struct HIBYTETABLE {
    USHORT format;		/* Format number is set to 2. */
    USHORT length;		/* The actual length in bytes of this
				 * subtable. */
    USHORT version;		/* Version number (starts at 0). */
    USHORT subHeaderKeys[256];	/* Maps high bytes to subHeaders: value is
				 * subHeader index * 8. */
#if 0
    SUBHEADER subHeaders[];	/* Variable-length array of SUBHEADERs. */
    USHORT glyphIndexArray[];	/* Variable-length array containing subarrays
				 * used for mapping the low byte of 2-byte
				 * characters. */
#endif
} HIBYTETABLE;

typedef struct SEGMENTTABLE {
    USHORT format;		/* Format number is set to 4. */
    USHORT length;		/* The actual length in bytes of this
				 * subtable. */
    USHORT version;		/* Version number (starts at 0). */
    USHORT segCountX2;		/* 2 x segCount. */
    USHORT searchRange;		/* 2 x (2**floor(log2(segCount))). */
    USHORT entrySelector;	/* log2(searchRange/2). */
    USHORT rangeShift;		/* 2 x segCount - searchRange. */
#if 0
    USHORT endCount[segCount]	/* End characterCode for each segment. */
    USHORT reservedPad;		/* Set to 0. */
    USHORT startCount[segCount];/* Start character code for each segment. */
    USHORT idDelta[segCount];	/* Delta for all character in segment. */
    USHORT idRangeOffset[segCount]; /* Offsets into glyphIdArray or 0. */
    USHORT glyphIdArray[]	/* Glyph index array. */
#endif
} SEGMENTTABLE;

typedef struct TRIMMEDTABLE {
    USHORT format;		/* Format number is set to 6. */
    USHORT length;		/* The actual length in bytes of this
				 * subtable. */
    USHORT version;		/* Version number (starts at 0). */
    USHORT firstCode;		/* First character code of subrange. */
    USHORT entryCount;		/* Number of character codes in subrange. */
#if 0
    USHORT glyphIdArray[];	/* Array of glyph index values for
				 * character codes in the range. */
#endif
} TRIMMEDTABLE;

typedef union SUBTABLE {
    ANYTABLE any;
    BYTETABLE byte;
    HIBYTETABLE hiByte;
    SEGMENTTABLE segment;
    TRIMMEDTABLE trimmed;
} SUBTABLE;

#pragma pack()

/*
 *-------------------------------------------------------------------------
 *
 * LoadFontRanges --
 *
 *	Given an HFONT, get the information about the characters that this
 *	font can display.
 *
 * Results:
 *	If the font has no Unicode character information, the return value is
 *	0 and *startCountPtr and *endCountPtr are filled with NULL. Otherwise,
 *	*startCountPtr and *endCountPtr are set to pointers to arrays of
 *	TrueType character existence information and the return value is the
 *	length of the arrays (the two arrays are always the same length as
 *	each other).
 *
 * Side effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

static int
LoadFontRanges(
    HDC hdc,			/* HDC into which font can be selected. */
    HFONT hFont,		/* HFONT to query. */
    USHORT **startCountPtr,	/* Filled with malloced pointer to character
				 * range information. */
    USHORT **endCountPtr,	/* Filled with malloced pointer to character
				 * range information. */
    int *symbolPtr,
    ULONG **startGroupPtr,	/* Filled with malloced format-12 group
				 * start codepoints (supplementary plane). */
    ULONG **endGroupPtr,	/* Filled with malloced format-12 group
				 * end codepoints. */
    int *groupCountPtr)		/* Number of format-12 groups. */
 {
    int n, i, j, k, swapped, segCount;
    size_t cbData, offset;
    DWORD cmapKey;
    USHORT *startCount, *endCount;
    CMAPTABLE cmapTable;
    ENCODINGTABLE encTable;
    SUBTABLE subTable;
    char *s;

    segCount = 0;
    startCount = NULL;
    endCount = NULL;
    *symbolPtr = 0;
    *startGroupPtr = NULL;
    *endGroupPtr   = NULL;
    *groupCountPtr = 0;

    hFont = (HFONT)SelectObject(hdc, hFont);

    i = 0;
    s = (char *) &i;
    *s = '\1';
    swapped = 0;

    if (i == 1) {
	swapped = 1;
    }

    cmapKey = CMAPHEX;
    if (swapped) {
	SwapLong(&cmapKey);
    }

    n = GetFontData(hdc, cmapKey, 0, &cmapTable, sizeof(cmapTable));
    if (n != (int) GDI_ERROR) {
	if (swapped) {
	    SwapShort(&cmapTable.numTables);
	}
	for (i = 0; i < cmapTable.numTables; i++) {
	    offset = sizeof(cmapTable) + i * sizeof(encTable);
	    GetFontData(hdc, cmapKey, (DWORD) offset, &encTable,
		    sizeof(encTable));
	    if (swapped) {
		SwapShort(&encTable.platform);
		SwapShort(&encTable.encoding);
		SwapLong(&encTable.offset);
	    }
	    if (encTable.platform != 3) {
		/*
		 * Not Microsoft encoding.
		 */

		continue;
	    }
	    if (encTable.encoding == 0) {
		*symbolPtr = 1;
	    } else if (encTable.encoding == 10) {
		/*
		 * Platform 3, encoding 10: cmap format 12.
		 * This subtable covers the full Unicode range including
		 * supplementary planes (emoji, historic scripts, etc.).
		 * The header at encTable.offset is:
		 *   USHORT format   (== 12, but stored as ULONG in the
		 *                    "fixed" 16.16 representation — high
		 *                    USHORT is 12, low USHORT is 0)
		 *   ULONG  length
		 *   ULONG  language
		 *   ULONG  nGroups
		 * Each group is three ULONGs: startCode, endCode, startGlyphID.
		 */
		ULONG fmt12hdr[4];  /* format/reserved, length, language, nGroups */
		if (GetFontData(hdc, cmapKey, (DWORD)encTable.offset,
			fmt12hdr, sizeof(fmt12hdr)) == (DWORD)GDI_ERROR) {
		    continue;
		}
		if (swapped) {
		    SwapLong(&fmt12hdr[0]);
		    SwapLong(&fmt12hdr[3]);
		}
		/* High 16 bits of fmt12hdr[0] hold the format number. */
		if ((fmt12hdr[0] >> 16) != 12) {
		    continue;
		}
		ULONG nGroups = fmt12hdr[3];
		if (nGroups == 0 || nGroups > 0x10000) {
		    continue;   /* sanity */
		}
		ULONG *sg = (ULONG *)Tcl_Alloc(nGroups * sizeof(ULONG));
		ULONG *eg = (ULONG *)Tcl_Alloc(nGroups * sizeof(ULONG));
		size_t groupBase = encTable.offset + sizeof(fmt12hdr);
		int ok = 1;
		for (ULONG g = 0; g < nGroups; g++) {
		    ULONG rec[3];    /* startCode, endCode, startGlyphID */
		    if (GetFontData(hdc, cmapKey,
			    (DWORD)(groupBase + g * 12),
			    rec, 12) == (DWORD)GDI_ERROR) {
			ok = 0;
			break;
		    }
		    if (swapped) {
			SwapLong(&rec[0]);
			SwapLong(&rec[1]);
		    }
		    sg[g] = rec[0];
		    eg[g] = rec[1];
		}
		if (!ok) {
		    Tcl_Free(sg);
		    Tcl_Free(eg);
		    continue;
		}
		/* Keep only the first (most complete) format-12 subtable. */
		if (*groupCountPtr == 0) {
		    *startGroupPtr = sg;
		    *endGroupPtr   = eg;
		    *groupCountPtr = (int)nGroups;
		} else {
		    Tcl_Free(sg);
		    Tcl_Free(eg);
		}
		continue;
	    } else if (encTable.encoding != 1) {
		continue;
	    }

	    GetFontData(hdc, cmapKey, (DWORD) encTable.offset, &subTable,
		    sizeof(subTable));
	    if (swapped) {
		SwapShort(&subTable.any.format);
	    }
	    if (subTable.any.format == 4) {
		if (swapped) {
		    SwapShort(&subTable.segment.segCountX2);
		}
		segCount = subTable.segment.segCountX2 / 2;
		cbData = segCount * sizeof(USHORT);

		startCount = (USHORT *)Tcl_Alloc(cbData);
		endCount = (USHORT *)Tcl_Alloc(cbData);

		offset = encTable.offset + sizeof(subTable.segment);
		GetFontData(hdc, cmapKey, (DWORD) offset, endCount, (DWORD)cbData);
		offset += cbData + sizeof(USHORT);
		GetFontData(hdc, cmapKey, (DWORD) offset, startCount, (DWORD)cbData);
		if (swapped) {
		    for (j = 0; j < segCount; j++) {
			SwapShort(&endCount[j]);
			SwapShort(&startCount[j]);
		    }
		}
		if (*symbolPtr != 0) {
		    /*
		     * Empirically determined: When a symbol font is loaded,
		     * the character existence metrics obtained from the
		     * system are mildly wrong. If the real range of the
		     * symbol font is from 0020 to 00FE, then the metrics are
		     * reported as F020 to F0FE. When we load a symbol font,
		     * we must fix the character existence metrics.
		     *
		     * Symbol fonts should only use the symbol encoding for
		     * 8-bit characters [note Bug: 2406]
		     */

		    for (k = 0; k < segCount; k++) {
			if (((startCount[k] & 0xff00) == 0xf000)
				&& ((endCount[k] & 0xff00) == 0xf000)) {
			    startCount[k] &= 0xff;
			    endCount[k] &= 0xff;
			}
		    }
		}
	    }
	}
    } else if (GetTextCharset(hdc) == ANSI_CHARSET) {
	/*
	 * Bitmap font. We should also support ranges for the other *_CHARSET
	 * values.
	 */

	segCount = 1;
	cbData = segCount * sizeof(USHORT);
	startCount = (USHORT *)Tcl_Alloc(cbData);
	endCount = (USHORT *)Tcl_Alloc(cbData);
	startCount[0] = 0x0000;
	endCount[0] = 0x00ff;
    }
    SelectObject(hdc, hFont);

    *startCountPtr = startCount;
    *endCountPtr = endCount;
    return segCount;
}

/*
 *-------------------------------------------------------------------------
 *
 * SwapShort, SwapLong --
 *
 *	Helper functions to convert the data loaded from TrueType font files
 *	to Intel byte ordering.
 *
 * Results:
 *	Bytes of input value are swapped and stored back in argument.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

static inline void
SwapShort(
    PUSHORT p)
{
    *p = (SHORT)(HIBYTE(*p) + (LOBYTE(*p) << 8));
}

static inline void
SwapLong(
    PULONG p)
{
    ULONG temp;

    temp = (LONG) ((BYTE) *p);
    temp <<= 8;
    *p >>=8;

    temp += (LONG) ((BYTE) *p);
    temp <<= 8;
    *p >>=8;

    temp += (LONG) ((BYTE) *p);
    temp <<= 8;
    *p >>=8;

    temp += (LONG) ((BYTE) *p);
    *p = temp;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
