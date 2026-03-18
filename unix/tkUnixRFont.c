/*
 * tkUnixRFont.c --
 *
 * Alternate implementation of tkUnixFont.c using Xft.
 *
 * Copyright (c) 2002-2003 Keith Packard
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkUnixInt.h"
#include "tkFont.h"
#include <X11/Xft/Xft.h>
#include <math.h>
#include <stdlib.h>
#define KB_TEXT_SHAPE_IMPLEMENTATION
#include <kb_text_shaper.h>
#include <SheenBidi/SheenBidi.h>

#define MAX_CACHED_COLORS 16
#define TK_DRAW_IN_CONTEXT

/*
 * Debugging support...
 */

#define DEBUG_FONTSEL 0
#define DEBUG(arguments) \
    if (DEBUG_FONTSEL) { \
	printf arguments; fflush(stdout); \
    }

typedef struct {
    XftFont *ftFont;
    XftFont *ft0Font;
    FcPattern *source;
    FcCharSet *charset;
    double angle;
} UnixFtFace;

typedef struct {
    XftColor color;
    int next;
} UnixFtColorList;

/*
 * ---------------------------------------------------------------
 * X11Shape --
 *
 *   Pure output buffer for shaped glyphs. No shaper state; no cache.
 *   Callers stack-allocate this, pass it to UnixFontShapeString, then hand
 *   a const pointer to drawing functions.
 * ---------------------------------------------------------------
 */
typedef struct {
    struct {
	kbts_font *font;     /* Which kb font this glyph came from */
	FT_UInt    glyphId;  /* Glyph index in font */
	int        x, y;     /* Pen position for this glyph */
	int        advanceX; /* Advance width */
    } glyphs[2048];
    int glyphCount;
} X11Shape;

/*
 * ---------------------------------------------------------------
 * X11Shaper --
 *
 *   Persistent per-font shaping state.  Owns the kbts context, the font map
 *   that relates kbts_font pointers back to UnixFtFont face indices, and the
 *   string cache.  Lives on UnixFtFont.  Only UnixFontShapeString writes to
 *   this; drawing functions are read-only consumers.
 * ---------------------------------------------------------------
 */
typedef struct {
    kbts_shape_context *context;

    /* Mapping between kb_text_shaper fonts and Tk font faces */
    struct {
	kbts_font *kbFont;    /* kb_text_shaper font */
	int        faceIndex; /* Index in UnixFtFont->faces[] */
    } fontMap[8];
    int numFonts;

    /* String cache — avoids re-shaping identical back-to-back strings */
    char     lastSrc[1024];
    int      lastLen;
    X11Shape lastShape; /* Cached output corresponding to lastSrc */
} X11Shaper;

typedef struct {
    TkFont font;		/* Stuff used by generic font package. Must be
				 * first in structure. */
    UnixFtFace *faces;
    int nfaces;
    FcFontSet *fontset;
    FcPattern *pattern;

    Display *display;
    int screen;
    Colormap colormap;
    Visual *visual;
    XftDraw *ftDraw;
    int ncolors;
    int firstColor;
    UnixFtColorList colors[MAX_CACHED_COLORS];
    X11Shaper shaper;
} UnixFtFont;

/*
 * Used to describe the current clipping box. Can't be passed normally because
 * the information isn't retrievable from the GC.
 */

typedef struct {
    Region clipRegion;		/* The clipping region, or None. */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

TCL_DECLARE_MUTEX(xftMutex);
#define LOCK Tcl_MutexLock(&xftMutex)
#define UNLOCK Tcl_MutexUnlock(&xftMutex)

/* One-time warning so we don't spam the console when a font is in a bad state */
static int warnedNullShaper = 0;

void                    X11Shaper_Init(X11Shaper *s);
void                    X11Shaper_Destroy(X11Shaper *s);
void                    UnixFontShapeString(UnixFtFont *fontPtr,
					    const char *source,
					    int numBytes,
					    X11Shape *shapeOut);
static void             UnixFontDrawShapedText(Display *display,
					       Drawable drawable,
					       GC gc,
					       UnixFtFont *fontPtr,
					       const X11Shape *shape,
					       double originX,
					       double originY,
					       double angle_deg);
void                    Tk_DrawCharsRotated(Display *display,
					    Drawable drawable,
					    GC gc,
					    Tk_Font tkfont,
					    const char *source,
					    int numBytes,
					    int x, int y,
					    double angle);
static int              UnixFontGetShapedWidth(UnixFtFont *fontPtr,
					       const char *source,
					       Tcl_Size numBytes);

typedef struct {
    int offset;          /* Byte offset in original UTF-8 string */
    int length;          /* Length in bytes */
    SBLevel level;       /* Embedding level from SheenBidi */
    int isRTL;           /* 1 if this run is RTL, 0 if LTR */
} BidiRun;

static int              GetBidiRuns(const char *utf8, int len,
				    BidiRun *runsOut, int maxRuns);

/*
 * ---------------------------------------------------------------
 * utf8ToUcs4 --
 *
 *   Convert UTF-8 to UCS-4, using Tcl or Fontconfig depending on available
 *   bytes.
 *
 * Results:
 *   Number of bytes consumed.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
static Tcl_Size utf8ToUcs4(const char *source, FcChar32 *c, Tcl_Size numBytes)
{
    if (numBytes >= 6) {
	return Tcl_UtfToUniChar(source, (int *)c);
    }
    return FcUtf8ToUcs4((const FcChar8 *)source, c, numBytes);
}

/*
 * ---------------------------------------------------------------
 * TkpFontPkgInit --
 *
 *   This procedure is called when an application is created. It
 *   initializes all the structures that are used by the
 *   platform-dependant code on a per application basis.
 *   Note that this is called before TkpInit() !
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
void
TkpFontPkgInit(
    TCL_UNUSED(TkMainInfo *))	/* The application being created. */
{
}

/*
 * ---------------------------------------------------------------
 * GetFont --
 *
 *   Retrieve an XftFont for a given UCS-4 character and rotation angle.
 *   Caches rotated fonts per face.
 *
 * Results:
 *   Pointer to XftFont.
 *
 * Side effects:
 *   May create new XftFont if not already cached.
 * ---------------------------------------------------------------
 */
static XftFont *
GetFont(
    UnixFtFont *fontPtr,
    FcChar32 ucs4,
    double angle)
{
    int i;

    if (ucs4) {
	for (i = 0; i < fontPtr->nfaces; i++) {
	    FcCharSet *charset = fontPtr->faces[i].charset;

	    if (charset && FcCharSetHasChar(charset, ucs4)) {
		break;
	    }
	}
	if (i == fontPtr->nfaces) {
	    i = 0;
	}
    } else {
	i = 0;
    }
    if ((angle == 0.0 && !fontPtr->faces[i].ft0Font) || (angle != 0.0 &&
	    (!fontPtr->faces[i].ftFont || fontPtr->faces[i].angle != angle))){
	FcPattern *pat = FcFontRenderPrepare(0, fontPtr->pattern,
					      fontPtr->faces[i].source);
	double s = sin(angle*PI/180.0), c = cos(angle*PI/180.0);
	FcMatrix mat;
	XftFont *ftFont;

	/*
	 * Initialize the matrix manually so this can compile with HP-UX cc
	 * (which does not allow non-constant structure initializers). [Bug
	 * 2978410]
	 */

	mat.xx = mat.yy = c;
	mat.xy = -(mat.yx = s);

	if (angle != 0.0) {
	    FcPatternAddMatrix(pat, FC_MATRIX, &mat);
	}
	LOCK;
	ftFont = XftFontOpenPattern(fontPtr->display, pat);
	UNLOCK;
	if (!ftFont) {
	    /*
	     * The previous call to XftFontOpenPattern() should not fail, but
	     * sometimes does anyway. Usual cause appears to be a
	     * misconfigured fontconfig installation; see [Bug 1090382]. Try a
	     * fallback:
	     */

	    LOCK;
	    ftFont = XftFontOpen(fontPtr->display, fontPtr->screen,
				  FC_FAMILY, FcTypeString, "sans",
				  FC_SIZE, FcTypeDouble, 12.0,
				  FC_MATRIX, FcTypeMatrix, &mat,
				  NULL);
	    UNLOCK;
	}
	if (!ftFont) {
	    /*
	     * The previous call should definitely not fail. Impossible to
	     * proceed at this point.
	     */

	    Tcl_Panic("Cannot find a usable font");
	}

	if (angle == 0.0) {
	    fontPtr->faces[i].ft0Font = ftFont;
	} else {
	    if (fontPtr->faces[i].ftFont) {
		LOCK;
		XftFontClose(fontPtr->display, fontPtr->faces[i].ftFont);
		UNLOCK;
	    }
	    fontPtr->faces[i].ftFont = ftFont;
	    fontPtr->faces[i].angle = angle;
	}
    }
    return (angle==0.0? fontPtr->faces[i].ft0Font : fontPtr->faces[i].ftFont);
}

/*
 * ---------------------------------------------------------------
 * GetTkFontAttributes --
 *
 *   Fill in TkFontAttributes from an XftFont.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   The TkFontAttributes structure is updated.
 * ---------------------------------------------------------------
 */
static void
GetTkFontAttributes(
    Tk_Window tkwin,
    XftFont *ftFont,
    TkFontAttributes *faPtr)
{
    const char *family = "Unknown";
    const char *const *familyPtr = &family;
    double ptSize, dblPxSize, size;
    int intPxSize, weight, slant;

    (void) XftPatternGetString(ftFont->pattern, XFT_FAMILY, 0, familyPtr);
    if (XftPatternGetDouble(ftFont->pattern, XFT_SIZE, 0,
			     &ptSize) == XftResultMatch) {
	size = ptSize;
    } else if (XftPatternGetDouble(ftFont->pattern, XFT_PIXEL_SIZE, 0,
				    &dblPxSize) == XftResultMatch) {
	size = -dblPxSize;
    } else if (XftPatternGetInteger(ftFont->pattern, XFT_PIXEL_SIZE, 0,
				     &intPxSize) == XftResultMatch) {
	size = (double)-intPxSize;
    } else {
	size = 12.0;
    }
    if (XftPatternGetInteger(ftFont->pattern, XFT_WEIGHT, 0,
			      &weight) != XftResultMatch) {
	weight = XFT_WEIGHT_MEDIUM;
    }
    if (XftPatternGetInteger(ftFont->pattern, XFT_SLANT, 0,
			      &slant) != XftResultMatch) {
	slant = XFT_SLANT_ROMAN;
    }

    DEBUG(("GetTkFontAttributes: family %s size %ld weight %d slant %d\n",
	   family, lround(size), weight, slant));

    faPtr->family = Tk_GetUid(family);
    /*
     * Make sure that faPtr->size will be > 0 even
     * in the very unprobable case that size < 0
     */
    faPtr->size = TkFontGetPoints(tkwin, size);
    faPtr->weight = (weight > XFT_WEIGHT_MEDIUM) ? TK_FW_BOLD : TK_FW_NORMAL;
    faPtr->slant = (slant > XFT_SLANT_ROMAN) ? TK_FS_ITALIC : TK_FS_ROMAN;
    faPtr->underline = 0;
    faPtr->overstrike = 0;
}

/*
 * ---------------------------------------------------------------
 * GetTkFontMetrics --
 *
 *   Fill in TkFontMetrics from an XftFont.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   The TkFontMetrics structure is updated.
 * ---------------------------------------------------------------
 */
static void
GetTkFontMetrics(
    XftFont *ftFont,
    TkFontMetrics *fmPtr)
{
    int spacing;

    if (XftPatternGetInteger(ftFont->pattern, XFT_SPACING, 0,
			      &spacing) != XftResultMatch) {
	spacing = XFT_PROPORTIONAL;
    }

    fmPtr->ascent = ftFont->ascent;
    fmPtr->descent = ftFont->descent;
    fmPtr->maxWidth = ftFont->max_advance_width;
    fmPtr->fixed = spacing != XFT_PROPORTIONAL;
}

/*
 * ---------------------------------------------------------------
 * GetBidiRuns --
 *
 *   Use SheenBidi to properly analyze text per UAX#9 and split it into
 *   level runs with correct directionality.
 *
 * Results:
 *   Returns number of runs created. Fills runsOut array (caller provides).
 *
 * Side effects:
 *   Caller must provide runsOut array with sufficient space (32 runs max).
 * ---------------------------------------------------------------
 */
static int
GetBidiRuns(const char *utf8, int len, BidiRun *runsOut, int maxRuns)
{
    /* Scan for characters that might require Bidi reordering.
     * U+0590 is the start of the Hebrew block; everything
     * below is LTR.
     */
    int needsBidi = 0;
    const unsigned char *p = (const unsigned char *)utf8;
    for (int i = 0; i < len; i++) {
	if (p[i] > 0x7F) {
	    FcChar32 u;
	    Tcl_UtfToUniChar((const char *)(p + i), (int *)&u);
	    if (u >= 0x0590) {
		needsBidi = 1;
		break;
	    }
	}
    }

    if (!needsBidi) {
	runsOut[0].offset = 0;
	runsOut[0].length = len;
	runsOut[0].level  = 0;
	runsOut[0].isRTL  = 0;
	return 1;
    }

    SBAlgorithmRef bidiAlg  = NULL;
    SBParagraphRef paragraph = NULL;
    SBUInteger cpCount = 0;
    SBUInteger codepoints[1024];

    int byteIdx = 0;
    while (byteIdx < len && cpCount < 1024) {
	FcChar32 c;
	int clen = utf8ToUcs4(utf8 + byteIdx, &c, len - byteIdx);
	if (clen <= 0) break;
	codepoints[cpCount++] = (SBUInteger)c;
	byteIdx += clen;
    }

    SBCodepointSequence codepointSeq = {SBStringEncodingUTF32, codepoints, cpCount};
    bidiAlg   = SBAlgorithmCreate(&codepointSeq);
    paragraph = SBAlgorithmCreateParagraph(bidiAlg, 0, cpCount, SBLevelDefaultLTR);
    SBLineRef line = SBParagraphCreateLine(paragraph, 0, cpCount);

    SBUInteger       lineRunCount = SBLineGetRunCount(line);
    const SBRun     *runs         = SBLineGetRunsPtr(line);

    /* OPTIMIZATION: Use stack memory for the mapping to avoid malloc latency. */
    int  cpToByteStack[1025];
    int *cpToByte = (cpCount < 1024) ? cpToByteStack
	                              : (int *)malloc((cpCount + 1) * sizeof(int));

    byteIdx = 0;
    for (SBUInteger i = 0; i < cpCount; i++) {
	cpToByte[i] = byteIdx;
	FcChar32 c;
	byteIdx += utf8ToUcs4(utf8 + byteIdx, &c, len - byteIdx);
    }
    cpToByte[cpCount] = len;

    int runCount = 0;
    for (SBUInteger i = 0; i < lineRunCount && runCount < maxRuns; i++) {
	runsOut[runCount].offset = cpToByte[runs[i].offset];
	runsOut[runCount].length = cpToByte[runs[i].offset + runs[i].length]
	                           - runsOut[runCount].offset;
	runsOut[runCount].level  = runs[i].level;
	runsOut[runCount].isRTL  = (runs[i].level & 1);
	runCount++;
    }

    if (cpToByte != cpToByteStack) free(cpToByte);
    SBLineRelease(line);
    SBParagraphRelease(paragraph);
    SBAlgorithmRelease(bidiAlg);

    return runCount;
}

/*
 * ---------------------------------------------------------------
 * InitFont --
 *
 *   Initializes the fields of a UnixFtFont structure. If fontPtr is NULL,
 *   also allocates a new UnixFtFont.
 *
 * Results:
 *   On error, frees fontPtr and returns NULL, otherwise returns fontPtr.
 *
 * Side effects:
 *   Allocates memory, loads fonts, initializes shaper.
 * ---------------------------------------------------------------
 */
static void FinishedWithFont(UnixFtFont *fontPtr);

static int
InitFontErrorProc(
    void *clientData,
    TCL_UNUSED(XErrorEvent *))
{
    int *errorFlagPtr = (int *)clientData;

    if (errorFlagPtr != NULL) {
	*errorFlagPtr = 1;
    }
    return 0;
}

static UnixFtFont *
InitFont(
    Tk_Window tkwin,
    FcPattern *pattern,
    UnixFtFont *fontPtr)
{
    FcFontSet *set;
    FcCharSet *charset;
    FcResult result;
    XftFont *ftFont;
    int i, iWidth, errorFlag;
    Tk_ErrorHandler handler;

    if (!fontPtr) {
	fontPtr = (UnixFtFont *)Tcl_Alloc(sizeof(UnixFtFont));
    }

    FcConfigSubstitute(0, pattern, FcMatchPattern);
    XftDefaultSubstitute(Tk_Display(tkwin), Tk_ScreenNumber(tkwin), pattern);

    /*
     * Generate the list of fonts
     */
    set = FcFontSort(0, pattern, FcTrue, NULL, &result);
    if (!set || set->nfont == 0) {
	Tcl_Free(fontPtr);
	return NULL;
    }

    fontPtr->fontset = set;
    fontPtr->pattern = pattern;
    fontPtr->faces   = (UnixFtFace *)Tcl_Alloc(set->nfont * sizeof(UnixFtFace));
    fontPtr->nfaces  = set->nfont;

    /*
     * Fill in information about each returned font
     */
    for (i = 0; i < set->nfont; i++) {
	fontPtr->faces[i].ftFont  = 0;
	fontPtr->faces[i].ft0Font = 0;
	fontPtr->faces[i].source  = set->fonts[i];
	if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0,
				&charset) == FcResultMatch) {
	    fontPtr->faces[i].charset = FcCharSetCopy(charset);
	} else {
	    fontPtr->faces[i].charset = 0;
	}
	fontPtr->faces[i].angle = 0.0;
    }

    fontPtr->display    = Tk_Display(tkwin);
    fontPtr->screen     = Tk_ScreenNumber(tkwin);
    fontPtr->colormap   = Tk_Colormap(tkwin);
    fontPtr->visual     = Tk_Visual(tkwin);
    fontPtr->ftDraw     = 0;
    fontPtr->ncolors    = 0;
    fontPtr->firstColor = -1;

    /*
     * Initialize shaper to zero — X11Shaper_Init called after metrics are set.
     */
    memset(&fontPtr->shaper, 0, sizeof(fontPtr->shaper));

    /*
     * Fill in platform-specific fields of TkFont.
     */
    errorFlag = 0;
    handler = Tk_CreateErrorHandler(Tk_Display(tkwin),
				    -1, -1, -1, InitFontErrorProc, (void *)&errorFlag);
    ftFont = GetFont(fontPtr, 0, 0.0);
    if ((ftFont == NULL) || errorFlag) {
	Tk_DeleteErrorHandler(handler);
	FinishedWithFont(fontPtr);
	Tcl_Free(fontPtr);
	return NULL;
    }

    fontPtr->font.fid = XLoadFont(Tk_Display(tkwin), "fixed");
    GetTkFontAttributes(tkwin, ftFont, &fontPtr->font.fa);
    GetTkFontMetrics(ftFont, &fontPtr->font.fm);
    Tk_DeleteErrorHandler(handler);

    if (errorFlag) {
	FinishedWithFont(fontPtr);
	Tcl_Free(fontPtr);
	return NULL;
    }

    /*
     * Fontconfig can't report any information about the position or thickness
     * of underlines or overstrikes. Thus, we use some defaults that are
     * hacked around from backup defaults in tkUnixFont.c, which are in turn
     * based on recommendations in the X manual. The comments from that file
     * leading to these computations were:
     *
     *	    If the XA_UNDERLINE_POSITION property does not exist, the X manual
     *	    recommends using half the descent.
     *
     *	    If the XA_UNDERLINE_THICKNESS property does not exist, the X
     *	    manual recommends using the width of the stem on a capital letter.
     *	    I don't know of a way to get the stem width of a letter, so guess
     *	    and use 1/3 the width of a capital I.
     *
     * Note that nothing corresponding to *either* property is reported by
     * Fontconfig at all. [Bug 1961455]
     */
    {
	TkFont *fPtr = &fontPtr->font;
	fPtr->underlinePos = fPtr->fm.descent / 2;
	handler = Tk_CreateErrorHandler(Tk_Display(tkwin),
					-1, -1, -1, InitFontErrorProc, (void *)&errorFlag);
	errorFlag = 0;
	Tk_MeasureChars((Tk_Font) fPtr, "I", 1, -1, 0, &iWidth);
	Tk_DeleteErrorHandler(handler);
	if (errorFlag) {
	    FinishedWithFont(fontPtr);
	    Tcl_Free(fontPtr);
	    return NULL;
	}
	fPtr->underlineHeight = iWidth / 3;
	if (fPtr->underlineHeight == 0) {
	    fPtr->underlineHeight = 1;
	}
	if (fPtr->underlineHeight + fPtr->underlinePos > fPtr->fm.descent) {
	    fPtr->underlineHeight = fPtr->fm.descent - fPtr->underlinePos;
	    if (fPtr->underlineHeight == 0) {
		fPtr->underlinePos--;
		fPtr->underlineHeight = 1;
	    }
	}
    }

    /*
     * Now that font metrics are established, initialize the persistent
     * shaping context for this font and load all faces into it.
     * This is the only place fonts are pushed into the shaper.
     */
    X11Shaper_Init(&fontPtr->shaper);

    for (i = 0; i < fontPtr->nfaces && i < 8; i++) {
	FcPattern *facePattern = fontPtr->faces[i].source;
	FcChar8 *file;
	int index;

	if (FcPatternGetString(facePattern, FC_FILE, 0, &file) == FcResultMatch &&
	    FcPatternGetInteger(facePattern, FC_INDEX, 0, &index) == FcResultMatch) {

	    kbts_font *kbFont = kbts_ShapePushFontFromFile(fontPtr->shaper.context,
							   (const char *)file,
							   index);
	    if (kbFont && fontPtr->shaper.numFonts < 8) {
		fontPtr->shaper.fontMap[fontPtr->shaper.numFonts].kbFont    = kbFont;
		fontPtr->shaper.fontMap[fontPtr->shaper.numFonts].faceIndex = i;
		fontPtr->shaper.numFonts++;
	    }
	}
    }

    return fontPtr;
}

/*
 * ---------------------------------------------------------------
 * FinishedWithFont --
 *
 *   Release all resources associated with a UnixFtFont.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Font data is freed, Xft fonts closed, shaper destroyed.
 * ---------------------------------------------------------------
 */
static void
FinishedWithFont(
    UnixFtFont *fontPtr)
{
    Display *display = fontPtr->display;
    int i;
    Tk_ErrorHandler handler =
	Tk_CreateErrorHandler(display, -1, -1, -1, NULL, NULL);

    for (i = 0; i < fontPtr->nfaces; i++) {
	if (fontPtr->faces[i].ftFont) {
	    LOCK;
	    XftFontClose(fontPtr->display, fontPtr->faces[i].ftFont);
	    UNLOCK;
	}
	if (fontPtr->faces[i].ft0Font) {
	    LOCK;
	    XftFontClose(fontPtr->display, fontPtr->faces[i].ft0Font);
	    UNLOCK;
	}
	if (fontPtr->faces[i].charset) {
	    FcCharSetDestroy(fontPtr->faces[i].charset);
	}
    }
    if (fontPtr->faces) {
	Tcl_Free(fontPtr->faces);
    }
    if (fontPtr->pattern) {
	FcPatternDestroy(fontPtr->pattern);
    }
    if (fontPtr->ftDraw) {
	XftDrawDestroy(fontPtr->ftDraw);
    }
    if (fontPtr->font.fid) {
	XUnloadFont(fontPtr->display, fontPtr->font.fid);
    }
    if (fontPtr->fontset) {
	FcFontSetDestroy(fontPtr->fontset);
    }

    X11Shaper_Destroy(&fontPtr->shaper);

    Tk_DeleteErrorHandler(handler);
}

/*
 * ---------------------------------------------------------------
 * TkpGetNativeFont --
 *
 *   Create a Tk font from a platform-specific font name (XLFD).
 *
 * Results:
 *   Returns a pointer to a TkFont, or NULL on failure.
 *
 * Side effects:
 *   Allocates font resources.
 * ---------------------------------------------------------------
 */
TkFont *
TkpGetNativeFont(
    Tk_Window tkwin,		/* For display where font will be used. */
    const char *name)		/* Platform-specific font name. */
{
    UnixFtFont *fontPtr;
    FcPattern *pattern;

    DEBUG(("TkpGetNativeFont: %s\n", name));

    pattern = XftXlfdParse(name, FcFalse, FcFalse);
    if (!pattern) {
	return NULL;
    }

    /*
     * Should also try: pattern = FcNameParse(name); but generic/tkFont.c
     * expects TkpGetNativeFont() to only work on XLFD names under Unix.
     */

    fontPtr = InitFont(tkwin, pattern, NULL);
    if (!fontPtr) {
	FcPatternDestroy(pattern);
	return NULL;
    }
    return &fontPtr->font;
}

/*
 * ---------------------------------------------------------------
 * TkpGetFontFromAttributes --
 *
 *   Create a Tk font matching the given attributes.
 *
 * Results:
 *   Returns a pointer to a TkFont, or NULL on failure.
 *
 * Side effects:
 *   Allocates font resources; may reuse an existing font structure.
 * ---------------------------------------------------------------
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
    XftPattern *pattern;
    int weight, slant;
    UnixFtFont *fontPtr;

    DEBUG(("TkpGetFontFromAttributes: %s %ld %d %d\n", faPtr->family,
	   lround(faPtr->size), faPtr->weight, faPtr->slant));

    pattern = XftPatternCreate();
    if (faPtr->family) {
	XftPatternAddString(pattern, XFT_FAMILY, faPtr->family);
    }
    if (faPtr->size > 0.0) {
	XftPatternAddDouble(pattern, XFT_SIZE, faPtr->size);
    } else if (faPtr->size < 0.0) {
	XftPatternAddDouble(pattern, XFT_SIZE, TkFontGetPoints(tkwin, faPtr->size));
    } else {
	XftPatternAddDouble(pattern, XFT_SIZE, 12.0);
    }
    switch (faPtr->weight) {
	case TK_FW_NORMAL:
	default:
	    weight = XFT_WEIGHT_MEDIUM;
	    break;
	case TK_FW_BOLD:
	    weight = XFT_WEIGHT_BOLD;
	    break;
    }
    XftPatternAddInteger(pattern, XFT_WEIGHT, weight);
    switch (faPtr->slant) {
	case TK_FS_ROMAN:
	default:
	    slant = XFT_SLANT_ROMAN;
	    break;
	case TK_FS_ITALIC:
	    slant = XFT_SLANT_ITALIC;
	    break;
	case TK_FS_OBLIQUE:
	    slant = XFT_SLANT_OBLIQUE;
	    break;
    }
    XftPatternAddInteger(pattern, XFT_SLANT, slant);

    fontPtr = (UnixFtFont *) tkFontPtr;
    if (fontPtr != NULL) {
	FinishedWithFont(fontPtr);
    }
    fontPtr = InitFont(tkwin, pattern, fontPtr);

    /*
     * Hack to work around issues with weird issues with Xft/Xrender
     * connection. For details, see comp.lang.tcl thread starting from
     * <adcc99ed-c73e-4efc-bb5d-e57a57a051e8@l35g2000pra.googlegroups.com>
     */

    if (!fontPtr) {
	XftPatternAddBool(pattern, XFT_RENDER, FcFalse);
	fontPtr = InitFont(tkwin, pattern, fontPtr);
    }

    if (!fontPtr) {
	FcPatternDestroy(pattern);
	return NULL;
    }

    fontPtr->font.fa.underline   = faPtr->underline;
    fontPtr->font.fa.overstrike  = faPtr->overstrike;
    return &fontPtr->font;
}

/*
 * ---------------------------------------------------------------
 * TkpDeleteFont --
 *
 *   Release all resources associated with a font.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Font data is freed.
 * ---------------------------------------------------------------
 */
void
TkpDeleteFont(
    TkFont *tkFontPtr)		/* Token of font to be deleted. */
{
    UnixFtFont *fontPtr = (UnixFtFont *) tkFontPtr;

    FinishedWithFont(fontPtr);
    /* XXX tkUnixFont.c doesn't free tkFontPtr... */
}

/*
 * ---------------------------------------------------------------
 * TkpGetFontFamilies --
 *
 *   Return information about the font families that are available on the
 *   display of the given window.
 *
 * Results:
 *   Modifies interp's result object to hold a list of all the available
 *   font families.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
void
TkpGetFontFamilies(
    Tcl_Interp *interp,		/* Interp to hold result. */
    Tk_Window tkwin)		/* For display to query. */
{
    Tcl_Obj *resultPtr;
    XftFontSet *list;
    int i;

    resultPtr = Tcl_NewListObj(0, NULL);

    list = XftListFonts(Tk_Display(tkwin), Tk_ScreenNumber(tkwin),
			(char *) 0,		/* pattern elements */
			XFT_FAMILY, (char*) 0);	/* fields */
    for (i = 0; i < list->nfont; i++) {
	char *family, **familyPtr = &family;

	if (XftPatternGetString(list->fonts[i], XFT_FAMILY, 0, familyPtr)
		== XftResultMatch) {
	    Tcl_Obj *strPtr = Tcl_NewStringObj(family, TCL_INDEX_NONE);

	    Tcl_ListObjAppendElement(NULL, resultPtr, strPtr);
	}
    }
    XftFontSetDestroy(list);

    Tcl_SetObjResult(interp, resultPtr);
}

/*
 * ---------------------------------------------------------------
 * TkpGetSubFonts --
 *
 *   Called by [testfont subfonts] in the Tk testing package.
 *
 * Results:
 *   Sets interp's result to a list of the faces used by tkfont.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
void
TkpGetSubFonts(
    Tcl_Interp *interp,
    Tk_Font tkfont)
{
    Tcl_Obj *objv[3], *listPtr, *resultPtr;
    UnixFtFont *fontPtr = (UnixFtFont *) tkfont;
    FcPattern *pattern;
    const char *family = "Unknown";
    const char *const *familyPtr = &family;
    const char *foundry = "Unknown";
    const char *const *foundryPtr = &foundry;
    const char *encoding = "Unknown";
    const char *const *encodingPtr = &encoding;
    int i;

    resultPtr = Tcl_NewListObj(0, NULL);

    for (i = 0; i < fontPtr->nfaces ; ++i) {
	pattern = FcFontRenderPrepare(0, fontPtr->pattern,
				      fontPtr->faces[i].source);

	XftPatternGetString(pattern, XFT_FAMILY, 0, familyPtr);
	XftPatternGetString(pattern, XFT_FOUNDRY, 0, foundryPtr);
	XftPatternGetString(pattern, XFT_ENCODING, 0, encodingPtr);
	objv[0] = Tcl_NewStringObj(family, TCL_INDEX_NONE);
	objv[1] = Tcl_NewStringObj(foundry, TCL_INDEX_NONE);
	objv[2] = Tcl_NewStringObj(encoding, TCL_INDEX_NONE);
	listPtr = Tcl_NewListObj(3, objv);
	Tcl_ListObjAppendElement(NULL, resultPtr, listPtr);
    }
    Tcl_SetObjResult(interp, resultPtr);
}

/*
 * ---------------------------------------------------------------
 * TkpGetFontAttrsForChar --
 *
 *   Retrieve the font attributes of the actual font used to render a given
 *   character.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   The TkFontAttributes structure is filled.
 * ---------------------------------------------------------------
 */
void
TkpGetFontAttrsForChar(
    Tk_Window tkwin,		/* Window on the font's display */
    Tk_Font tkfont,		/* Font to query */
    int c,			/* Character of interest */
    TkFontAttributes *faPtr)	/* Output: Font attributes */
{
    UnixFtFont *fontPtr = (UnixFtFont *) tkfont;
    /* Structure describing the logical font */
    FcChar32 ucs4 = (FcChar32) c;
    /* UCS-4 character to map */
    XftFont *ftFont = GetFont(fontPtr, ucs4, 0.0);
    /* Actual font used to render the character */

    GetTkFontAttributes(tkwin, ftFont, faPtr);
    faPtr->underline  = fontPtr->font.fa.underline;
    faPtr->overstrike = fontPtr->font.fa.overstrike;
}

/*
 * ---------------------------------------------------------------
 * Tk_MeasureChars --
 *
 *   Measure the width of a string when drawn in the given font.
 *
 * Results:
 *   Returns number of bytes consumed; *lengthPtr filled with pixel width.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
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
				 * which only partially fit on this line.
				 * TK_WHOLE_WORDS means stop on a word
				 * boundary, if possible. TK_AT_LEAST_ONE
				 * means return at least one character even if
				 * no characters fit. */
    int *lengthPtr)		/* Filled with x-location just after the
				 * terminating character. */
{
    UnixFtFont *fontPtr = (UnixFtFont *) tkfont;
    X11Shape shape;
    int total = 0;
    int i;

    UnixFontShapeString(fontPtr, source, (int)numBytes, &shape);

    for (i = 0; i < shape.glyphCount; i++) {
	int next = total + shape.glyphs[i].advanceX;
	if (maxLength >= 0 && next > maxLength) {
	    if (flags & TK_PARTIAL_OK) {
		total = next;
	    } else if ((flags & TK_AT_LEAST_ONE) && total == 0) {
		total = next;
	    }
	    break;
	}
	total = next;
    }
    *lengthPtr = total;

    /* Very conservative byte estimate when truncating. */
    int byteCount;
    if (total >= UnixFontGetShapedWidth(fontPtr, source, numBytes)) {
	byteCount = (int)numBytes;
    } else {
	byteCount = (int)(numBytes * (double)total / (total + 1) + 0.5);
	if (byteCount < 1 && (flags & TK_AT_LEAST_ONE)) byteCount = 1;
	if (byteCount > numBytes) byteCount = (int)numBytes;
    }

    return byteCount;
}

/*
 * ---------------------------------------------------------------
 * Tk_MeasureCharsInContext --
 *
 *   Measure a substring of a larger string.
 *
 * Results:
 *   Returns number of bytes consumed; *lengthPtr filled with pixel width.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
int
Tk_MeasureCharsInContext(
    Tk_Font tkfont,
    const char *source,
    TCL_UNUSED(Tcl_Size),
    Tcl_Size rangeStart,
    Tcl_Size rangeLength,
    int maxLength,
    int flags,
    int *lengthPtr)
{
    return Tk_MeasureChars(tkfont, source + rangeStart, rangeLength,
			   maxLength, flags, lengthPtr);
}

/*
 * ---------------------------------------------------------------
 * LookUpColor --
 *
 *   Convert a pixel value to an XftColor. Uses a small LRU cache.
 *
 * Results:
 *   Pointer to XftColor.
 *
 * Side effects:
 *   May update the color cache.
 * ---------------------------------------------------------------
 */
static XftColor *
LookUpColor(
    Display *display,
    UnixFtFont *fontPtr,
    unsigned long pixel)
{
    int i, last = -1, last2 = -1;
    XColor xcolor;

    for (i = fontPtr->firstColor;
	 i >= 0; last2 = last, last = i, i = fontPtr->colors[i].next) {

	if (pixel == fontPtr->colors[i].color.pixel) {
	    if (last >= 0) {
		fontPtr->colors[last].next  = fontPtr->colors[i].next;
		fontPtr->colors[i].next     = fontPtr->firstColor;
		fontPtr->firstColor         = i;
	    }
	    return &fontPtr->colors[i].color;
	}
    }

    if (fontPtr->ncolors < MAX_CACHED_COLORS) {
	last2 = -1;
	last  = fontPtr->ncolors++;
    }

    xcolor.pixel = pixel;
    XQueryColor(display, fontPtr->colormap, &xcolor);

    fontPtr->colors[last].color.color.red   = xcolor.red;
    fontPtr->colors[last].color.color.green = xcolor.green;
    fontPtr->colors[last].color.color.blue  = xcolor.blue;
    fontPtr->colors[last].color.color.alpha = 0xFFFF;
    fontPtr->colors[last].color.pixel       = pixel;

    if (last2 >= 0) {
	fontPtr->colors[last2].next = fontPtr->colors[last].next;
    }
    fontPtr->colors[last].next = fontPtr->firstColor;
    fontPtr->firstColor        = last;

    return &fontPtr->colors[last].color;
}

#define NUM_SPEC    1024

/*
 * ---------------------------------------------------------------
 * Tk_DrawChars --
 *
 *   Draw a UTF-8 string using the given font.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Draws text on the specified drawable.
 * ---------------------------------------------------------------
 */
void
Tk_DrawChars(
    Display *display,		/* Display on which to draw. */
    Drawable drawable,		/* Window or pixmap in which to draw. */
    GC gc,			/* Graphics context for drawing characters. */
    Tk_Font tkfont,		/* Font in which characters will be drawn;
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
    UnixFtFont *fontPtr = (UnixFtFont *) tkfont;
    X11Shape shape;

    UnixFontShapeString(fontPtr, source, (int)numBytes, &shape);
    UnixFontDrawShapedText(display, drawable, gc, fontPtr,
			   &shape, (double)x, (double)y, 0.0);
}

/*
 * ---------------------------------------------------------------
 * TkDrawAngledChars --
 *
 *   Draw some characters at an angle.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Draws rotated text.
 * ---------------------------------------------------------------
 */
void
TkDrawAngledChars(
    Display *display,		/* Display on which to draw. */
    Drawable drawable,		/* Window or pixmap in which to draw. */
    GC gc,			/* Graphics context for drawing characters. */
    Tk_Font tkfont,		/* Font in which characters will be drawn;
				 * must be the same as font used in GC. */
    const char *source,		/* UTF-8 string to be displayed. */
    Tcl_Size numBytes,		/* Number of bytes in string. */
    double x, double y,		/* Coordinates at which to place origin of
				 * string when drawing. */
    double angle)		/* What angle to put text at, in degrees. */
{
    UnixFtFont *fontPtr = (UnixFtFont *) tkfont;
    X11Shape shape;

    UnixFontShapeString(fontPtr, source, (int)numBytes, &shape);
    UnixFontDrawShapedText(display, drawable, gc, fontPtr,
			   &shape, x, y, angle);
}

/*
 * ---------------------------------------------------------------
 * Tk_DrawCharsInContext -- 
 *
 *   Draws a substring of text using full shaping + bidi logic.
 *   Shapes ONLY the requested substring (source + rangeStart) and
 *   draws ALL resulting glyphs at the caller-provided (x,y). This eliminates
 *   the old bogus byte-as-glyph-index slicing that caused OOB access on RTL
 *   text, NULL-context spam, and the X_CreatePixmap(0) crash.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Draws the specified range of characters.
 * ---------------------------------------------------------------
 */
void
Tk_DrawCharsInContext(
    Display *display,
    Drawable drawable,
    GC gc,
    Tk_Font tkfont,
    const char *source,
    TCL_UNUSED(Tcl_Size),
    Tcl_Size rangeStart,
    Tcl_Size rangeLength,
    int x, int y)
{
    UnixFtFont *fontPtr = (UnixFtFont *) tkfont;
    X11Shape shape;

    /* Shape ONLY the requested substring (correct for bidi within the range) */
    UnixFontShapeString(fontPtr, source + rangeStart, (int)rangeLength, &shape);

    /* Setup drawing target. */
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (fontPtr->ftDraw == NULL) {
	fontPtr->ftDraw = XftDrawCreate(display, drawable,
					fontPtr->visual, fontPtr->colormap);
    } else {
	Tk_ErrorHandler handler =
	    Tk_CreateErrorHandler(display, -1, -1, -1, NULL, NULL);
	XftDrawChange(fontPtr->ftDraw, drawable);
	Tk_DeleteErrorHandler(handler);
    }

    XGCValues values;
    XftColor *xftcolor;
    XGetGCValues(display, gc, GCForeground, &values);
    xftcolor = LookUpColor(display, fontPtr, values.foreground);

    if (tsdPtr->clipRegion != NULL) {
	XftDrawSetClip(fontPtr->ftDraw, tsdPtr->clipRegion);
    }

    /* Draw ALL glyphs of the substring (no more bogus slicing) */
    XftGlyphFontSpec specs[NUM_SPEC];
    int nspec = 0;
    XftFont *prevFont = NULL;

    for (int i = 0; i < shape.glyphCount; i++) {
	/* Map kbFont pointer to face index via shaper's font map. */
	int faceIndex = 0;
	for (int j = 0; j < fontPtr->shaper.numFonts; j++) {
	    if (fontPtr->shaper.fontMap[j].kbFont == shape.glyphs[i].font) {
		faceIndex = fontPtr->shaper.fontMap[j].faceIndex;
		break;
	    }
	}

	XftFont *ftFont = GetFont(fontPtr, 0, 0.0);
	if (faceIndex < fontPtr->nfaces && fontPtr->faces[faceIndex].ft0Font) {
	    ftFont = fontPtr->faces[faceIndex].ft0Font;
	}
	if (!ftFont) continue;

	if (ftFont != prevFont && nspec > 0) {
	    LOCK;
	    XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	    nspec = 0;
	}
	prevFont = ftFont;

	int sx = (int)(x + shape.glyphs[i].x + 0.5);
	int sy = (int)(y + shape.glyphs[i].y + 0.5);

	specs[nspec].font  = ftFont;
	specs[nspec].glyph = shape.glyphs[i].glyphId;
	specs[nspec].x     = sx;
	specs[nspec].y     = sy;

	if (++nspec >= NUM_SPEC) {
	    LOCK;
	    XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	    nspec    = 0;
	    prevFont = NULL;
	}
    }

    if (nspec > 0) {
	LOCK;
	XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	UNLOCK;
    }

    if (tsdPtr->clipRegion != NULL) {
	XftDrawSetClip(fontPtr->ftDraw, NULL);
    }
}

/*
 * ---------------------------------------------------------------
 * TkpDrawAngledCharsInContext --
 *
 *   Draw a substring of rotated text.
 *   Shapes ONLY the requested substring and draws it directly at (x,y).
 *   Removed the broken pixel-vs-byte offset accumulation that caused crashes
 *   on bidirectional text.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Draws rotated characters.
 * ---------------------------------------------------------------
 */
void
TkpDrawAngledCharsInContext(
    Display *display,
    Drawable drawable,
    GC gc,
    Tk_Font tkfont,
    const char *source,
    TCL_UNUSED(Tcl_Size),
    Tcl_Size rangeStart,
    Tcl_Size rangeLength,
    double x, double y,
    double angle)
{
    UnixFtFont *fontPtr = (UnixFtFont *) tkfont;
    X11Shape shape;

    /* Shape ONLY the requested substring */
    UnixFontShapeString(fontPtr, source + rangeStart, (int)rangeLength, &shape);

    /* Draw at the caller-provided origin (no bogus offset calculation) */
    UnixFontDrawShapedText(display, drawable, gc, fontPtr,
			   &shape, x, y, angle);
}

/*
 * ---------------------------------------------------------------
 * TkUnixSetXftClipRegion --
 *
 *   Set the clipping region for subsequent Xft drawing.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Updates thread-local clipping region.
 * ---------------------------------------------------------------
 */
void
TkUnixSetXftClipRegion(
    Region clipRegion)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    tsdPtr->clipRegion = clipRegion;
}

/*
 * ---------------------------------------------------------------
 * Tk_DrawCharsRotated --
 *
 *   Draw rotated text with proper glyph shaping and positioning,
 *   including RTL support via character range detection.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Draws rotated text and any underline/overstrike decorations.
 * ---------------------------------------------------------------
 */
void
Tk_DrawCharsRotated(
    Display *display,
    Drawable drawable,
    GC gc,
    Tk_Font tkfont,
    const char *source,
    int numBytes,
    int x, int y,
    double angle)
{
    UnixFtFont *fontPtr = (UnixFtFont *) tkfont;
    X11Shape shape;
    XGCValues values;
    XftColor *xftcolor;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    int i, j;

    /* Shape first; drawing is a pure read of the resulting buffer. */
    UnixFontShapeString(fontPtr, source, numBytes, &shape);

    if (fontPtr->ftDraw == NULL) {
	fontPtr->ftDraw = XftDrawCreate(display, drawable,
					fontPtr->visual, fontPtr->colormap);
    } else {
	Tk_ErrorHandler handler =
	    Tk_CreateErrorHandler(display, -1, -1, -1, NULL, NULL);
	XftDrawChange(fontPtr->ftDraw, drawable);
	Tk_DeleteErrorHandler(handler);
    }

    XGetGCValues(display, gc, GCForeground, &values);
    xftcolor = LookUpColor(display, fontPtr, values.foreground);

    if (tsdPtr->clipRegion != NULL) {
	XftDrawSetClip(fontPtr->ftDraw, tsdPtr->clipRegion);
    }

    double rad  = angle * M_PI / 180.0;
    double cosA = cos(rad);
    double sinA = sin(rad);

    /* Accumulated pen advance in unrotated space, used for decorations. */
    int penX = 0;

    XftGlyphFontSpec specs[NUM_SPEC];
    int nspec = 0;
    XftFont *currentFont = NULL;

    for (i = 0; i < shape.glyphCount; i++) {
	/* Find the Xft face that corresponds to this glyph's kbts font. */
	int faceIndex = 0;
	for (j = 0; j < fontPtr->shaper.numFonts; j++) {
	    if (fontPtr->shaper.fontMap[j].kbFont == shape.glyphs[i].font) {
		faceIndex = fontPtr->shaper.fontMap[j].faceIndex;
		break;
	    }
	}

	XftFont *drawFont = NULL;
	if (faceIndex < fontPtr->nfaces) {
	    if (angle == 0.0) {
		drawFont = fontPtr->faces[faceIndex].ft0Font;
	    } else if (fontPtr->faces[faceIndex].ftFont &&
		       fontPtr->faces[faceIndex].angle == angle) {
		drawFont = fontPtr->faces[faceIndex].ftFont;
	    }
	}
	if (!drawFont) {
	    drawFont = GetFont(fontPtr, 0, angle);
	}
	if (!drawFont) {
	    continue;
	}

	if (drawFont != currentFont && nspec > 0) {
	    LOCK;
	    XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	    nspec = 0;
	}
	currentFont = drawFont;

	int gx = shape.glyphs[i].x;
	int gy = shape.glyphs[i].y;

	specs[nspec].font  = drawFont;
	specs[nspec].glyph = shape.glyphs[i].glyphId;
	specs[nspec].x     = x + (int)(gx * cosA - gy * sinA + 0.5);
	specs[nspec].y     = y + (int)(gx * sinA + gy * cosA + 0.5);

	if (++nspec >= NUM_SPEC) {
	    LOCK;
	    XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	    nspec        = 0;
	    currentFont  = NULL;
	}

	penX += shape.glyphs[i].advanceX;
    }

    if (nspec > 0) {
	LOCK;
	XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	UNLOCK;
    }

    if (tsdPtr->clipRegion != NULL) {
	XftDrawSetClip(fontPtr->ftDraw, NULL);
    }

    /* Underline / overstrike — rotated. */
    if (fontPtr->font.fa.underline || fontPtr->font.fa.overstrike) {
	double totalAdvanceX = (double)penX * cosA;
	double totalAdvanceY = (double)penX * sinA;

	XPoint points[5];
	double barHeight = (double) fontPtr->font.underlineHeight;
	double dy;

	if (fontPtr->font.fa.underline) {
	    dy = (double) fontPtr->font.underlinePos;
	    if (fontPtr->font.underlineHeight <= 1) {
		dy += 1.0;
	    }

	    points[0].x = x + (int)(dy * sinA + 0.5);
	    points[0].y = y + (int)(dy * cosA + 0.5);
	    points[1].x = x + (int)(dy * sinA + totalAdvanceX * cosA + 0.5);
	    points[1].y = y + (int)(dy * cosA + totalAdvanceY * sinA + 0.5);

	    if (barHeight <= 1.0) {
		XDrawLines(display, drawable, gc, points, 2, CoordModeOrigin);
	    } else {
		points[2].x = points[1].x + (int)(barHeight * sinA + 0.5);
		points[2].y = points[1].y + (int)(-barHeight * cosA + 0.5);
		points[3].x = points[0].x + (int)(barHeight * sinA + 0.5);
		points[3].y = points[0].y + (int)(-barHeight * cosA + 0.5);
		points[4]   = points[0];
		XFillPolygon(display, drawable, gc, points, 5, Complex,
			     CoordModeOrigin);
		XDrawLines(display, drawable, gc, points, 5, CoordModeOrigin);
	    }
	}

	if (fontPtr->font.fa.overstrike) {
	    dy = - (double)fontPtr->font.fm.descent
		 - ((double)fontPtr->font.fm.ascent / 10.0);

	    points[0].x = x + (int)(dy * sinA + 0.5);
	    points[0].y = y + (int)(dy * cosA + 0.5);
	    points[1].x = x + (int)(dy * sinA + totalAdvanceX * cosA + 0.5);
	    points[1].y = y + (int)(dy * cosA + totalAdvanceY * sinA + 0.5);

	    if (barHeight <= 1.0) {
		XDrawLines(display, drawable, gc, points, 2, CoordModeOrigin);
	    } else {
		points[2].x = points[1].x + (int)(barHeight * sinA + 0.5);
		points[2].y = points[1].y + (int)(-barHeight * cosA + 0.5);
		points[3].x = points[0].x + (int)(barHeight * sinA + 0.5);
		points[3].y = points[0].y + (int)(-barHeight * cosA + 0.5);
		points[4]   = points[0];
		XFillPolygon(display, drawable, gc, points, 5, Complex,
			     CoordModeOrigin);
		XDrawLines(display, drawable, gc, points, 5, CoordModeOrigin);
	    }
	}
    }
}

/*
 * ---------------------------------------------------------------
 * UnixFontShapeString --
 *
 *   Shape source (UTF-8, numBytes long) using the font's persistent
 *   X11Shaper and write results into the caller-provided shapeOut buffer.
 *
 *   Cache: if the string matches the shaper's lastSrc, shapeOut is
 *   populated from lastShape and the function returns immediately without
 *   calling into kbts or SheenBidi.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Updates the shaper cache; shapeOut is filled.
 * ---------------------------------------------------------------
 */
void
UnixFontShapeString(
    UnixFtFont *fontPtr,
    const char *source,
    int numBytes,
    X11Shape *shapeOut)
{
    X11Shaper *shaper = &fontPtr->shaper;

    if (!shaper || !shaper->context) {
	if (!warnedNullShaper) {
	    fprintf(stderr, "WARNING: kb_text_shaper context is NULL - "
	                    "bidi/complex script support disabled for this font\n");
	    warnedNullShaper = 1;
	}
	if (shapeOut) shapeOut->glyphCount = 0;
	return;
    }
	
    if (!shapeOut || !source || numBytes <= 0) {
	if (shapeOut) shapeOut->glyphCount = 0;
	return;
    }

    /*
     * Cache check: hit → copy cached output into caller's buffer and return.
     * Tk calls measurement functions many times before drawing; avoiding
     * repeated shaping of the same string is the primary performance lever.
     */
    if (numBytes == shaper->lastLen && numBytes < 1024) {
	if (memcmp(source, shaper->lastSrc, numBytes) == 0) {
	    *shapeOut = shaper->lastShape;
	    return;
	}
    }

    shapeOut->glyphCount = 0;

    /* Bidi analysis (fast LTR path is inside GetBidiRuns). */
    BidiRun bidiRuns[32];
    int numBidiRuns = GetBidiRuns(source, numBytes, bidiRuns, 32);

    int globalPenX = 0;
    int globalPenY = 0;

    for (int r = 0; r < numBidiRuns; r++) {
	const char *runText  = source + bidiRuns[r].offset;
	int         runLen   = bidiRuns[r].length;
	int         runIsRTL = bidiRuns[r].isRTL;
	kbts_run    run;
	kbts_glyph *glyph;
	int         runPenX  = 0;
	int         runPenY  = 0;

	if (runLen <= 0) continue;

	kbts_ShapeBegin(shaper->context,
			runIsRTL ? KBTS_DIRECTION_RTL : KBTS_DIRECTION_LTR,
			KBTS_LANGUAGE_DONT_KNOW);
	kbts_ShapeUtf8(shaper->context, runText, runLen,
		       KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
	kbts_ShapeEnd(shaper->context);

	while (kbts_ShapeRun(shaper->context, &run) &&
	       shapeOut->glyphCount < 2048) {
	    kbts_glyph_iterator it = run.Glyphs;
	    while (kbts_GlyphIteratorNext(&it, &glyph) &&
		   shapeOut->glyphCount < 2048) {
		shapeOut->glyphs[shapeOut->glyphCount].glyphId  = glyph->Id;
		shapeOut->glyphs[shapeOut->glyphCount].font     = run.Font;
		shapeOut->glyphs[shapeOut->glyphCount].x        =
		    globalPenX + runPenX + glyph->OffsetX;
		shapeOut->glyphs[shapeOut->glyphCount].y        =
		    globalPenY + runPenY + glyph->OffsetY;
		shapeOut->glyphs[shapeOut->glyphCount].advanceX = glyph->AdvanceX;

		runPenX += glyph->AdvanceX;
		runPenY += glyph->AdvanceY;
		shapeOut->glyphCount++;
	    }
	}

	globalPenX += runPenX;
	globalPenY += runPenY;

	if (shapeOut->glyphCount >= 2048) break;
    }

    /* Update shaper cache. */
    shaper->lastLen = numBytes;
    if (numBytes < 1024) {
	memcpy(shaper->lastSrc, source, numBytes);
	shaper->lastShape = *shapeOut;
    }
}

/*
 * ---------------------------------------------------------------
 * UnixFontDrawShapedText --
 *
 *   Unified drawing function for shaped + bidirectional text.
 *   Handles both horizontal (angle == 0.0) and rotated cases.
 *   Pure consumer: never calls UnixFontShapeString; caller owns shaping.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Draws the shaped glyphs.
 * ---------------------------------------------------------------
 */
static void
UnixFontDrawShapedText(
    Display *display, Drawable drawable, GC gc, UnixFtFont *fontPtr,
    const X11Shape *shape,
    double originX, double originY, double angle_deg)
{
    XftColor *xftcolor;
    XGCValues values;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (!shape || shape->glyphCount <= 0) return;

    if (fontPtr->ftDraw == NULL) {
	fontPtr->ftDraw = XftDrawCreate(display, drawable,
					fontPtr->visual, fontPtr->colormap);
    } else {
	XftDrawChange(fontPtr->ftDraw, drawable);
    }

    XGetGCValues(display, gc, GCForeground, &values);
    xftcolor = LookUpColor(display, fontPtr, values.foreground);
    if (!xftcolor) return;

    if (tsdPtr->clipRegion != NULL) {
	XftDrawSetClip(fontPtr->ftDraw, tsdPtr->clipRegion);
    }

    XftGlyphFontSpec specs[NUM_SPEC];
    int nspec    = 0;
    XftFont *lastFont = NULL;

    for (int i = 0; i < shape->glyphCount; i++) {
	XftFont *drawFont = NULL;

	for (int j = 0; j < fontPtr->shaper.numFonts; j++) {
	    if (fontPtr->shaper.fontMap[j].kbFont == shape->glyphs[i].font) {
		int fIdx = fontPtr->shaper.fontMap[j].faceIndex;
		if (fIdx >= 0 && fIdx < fontPtr->nfaces) {
		    drawFont = (angle_deg == 0.0)
			? fontPtr->faces[fIdx].ft0Font
			: fontPtr->faces[fIdx].ftFont;
		}
		break;
	    }
	}
	if (!drawFont) {
	    drawFont = GetFont(fontPtr, 0, angle_deg);
	}

	if (lastFont && drawFont != lastFont && nspec > 0) {
	    XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	    nspec = 0;
	}
	lastFont = drawFont;

	specs[nspec].font  = drawFont;
	specs[nspec].glyph = shape->glyphs[i].glyphId;

	if (angle_deg == 0.0) {
	    specs[nspec].x = (int)(originX + shape->glyphs[i].x + 0.5);
	    specs[nspec].y = (int)(originY + shape->glyphs[i].y + 0.5);
	} else {
	    double rad  = angle_deg * M_PI / 180.0;
	    double cosA = cos(rad), sinA = sin(rad);
	    double gx   = shape->glyphs[i].x;
	    double gy   = shape->glyphs[i].y;
	    specs[nspec].x = (int)(originX + gx * cosA - gy * sinA + 0.5);
	    specs[nspec].y = (int)(originY + gx * sinA + gy * cosA + 0.5);
	}

	nspec++;
	if (nspec >= NUM_SPEC) {
	    XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	    nspec = 0;
	}
    }

    if (nspec > 0) {
	XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
    }

    if (tsdPtr->clipRegion != NULL) {
	XftDrawSetClip(fontPtr->ftDraw, NULL);
    }
}

/*
 * ---------------------------------------------------------------
 * UnixFontGetShapedWidth --
 *
 *   Returns the total advance width (in pixels) after full shaping + bidi.
 *
 * Results:
 *   Width in pixels.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
static int
UnixFontGetShapedWidth(
    UnixFtFont *fontPtr,
    const char *source,
    Tcl_Size numBytes)
{
    X11Shape shape;
    int totalWidth = 0;
    int i;

    UnixFontShapeString(fontPtr, source, (int)numBytes, &shape);

    for (i = 0; i < shape.glyphCount; i++) {
	totalWidth += shape.glyphs[i].advanceX;
    }

    return totalWidth;
}

/*
 * ---------------------------------------------------------------
 * X11Shaper_Init --
 *
 *   Initialise a persistent shaping context. Called once from InitFont.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Allocates shaping context.
 * ---------------------------------------------------------------
 */
void
X11Shaper_Init(X11Shaper *s)
{
    memset(s, 0, sizeof(*s));
    s->context = kbts_CreateShapeContext(0, 0);
}

/*
 * ---------------------------------------------------------------
 * X11Shaper_Destroy --
 *
 *   Release the kbts context. Called from FinishedWithFont.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Frees shaping context.
 * ---------------------------------------------------------------
 */
void
X11Shaper_Destroy(X11Shaper *s)
{
    if (s->context) {
	kbts_DestroyShapeContext(s->context);
	s->context = NULL;
    }
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
