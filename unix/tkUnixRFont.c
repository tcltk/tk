/*
 * tkUnixRFont.c --
 *
 *	Alternate implementation of tkUnixFont.c using Xft.
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
#include "kb_text_shaper.h"
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
 * Text-shaping functions and data.
 */

typedef struct {
    kbts_shape_context *context;

    /* Mapping between kb_text_shaper fonts and Tk font faces */
    struct {
	kbts_font *kbFont;    /* kb_text_shaper font */
	int faceIndex;        /* Index in UnixFtFont->faces[] */
    } fontMap[8];
    int numFonts;

    /* Store shaped glyph data */
    struct {
	kbts_font *font;      /* Which kb font this glyph came from */
	FT_UInt glyphId;      /* Glyph index in font */
	int x, y;             /* Pen position for this glyph */
	int advanceX;         /* Advance width */
    } glyphs[2048];
    int glyphCount;
    Font cachedFamilyId;
} X11Shape;

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
    X11Shape shape;		
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

void                    X11Shape_Init(X11Shape *s);
void                    X11Shape_AddFont(X11Shape *s, kbts_font *f);
void 		        X11Shape_Destroy(X11Shape *s);
void 		        UnixFontShapeString(UnixFtFont *fontPtr, const
							char *source,
							int numBytes, X11Shape *shapePtr);
static void 	        UnixFontDrawShapedText(Display *display,
							Drawable drawable, GC gc, UnixFtFont *fontPtr, 
							const char *source, int numBytes, double originX, 
							double originY, double angle_deg);
void		        Tk_DrawCharsRotated(Display *display, Drawable drawable, 
							GC gc,	Tk_Font tkfont,	const char *source, 
							int numBytes,int x, int y, double angle);
static int	        UnixFontGetShapedWidth(UnixFtFont *fontPtr, const char *source,Tcl_Size numBytes);

typedef struct {
    int offset;          /* Byte offset in original UTF-8 string */
    int length;          /* Length in bytes */
    SBLevel level;       /* Embedding level from SheenBidi */
    int isRTL;           /* 1 if this run is RTL, 0 if LTR */
} BidiRun;

static int              GetBidiRuns(const char *utf8, int len, BidiRun *runsOut, int maxRuns);



/*
 *----------------------------------------------------------------------
 *
 * TkpFontPkgInit --
 *
 *	This procedure is called when an application is created. It
 *	initializes all the structures that are used by the
 *	platform-dependant code on a per application basis.
 *	Note that this is called before TkpInit() !
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Size utf8ToUcs4(const char *source, FcChar32 *c, Tcl_Size numBytes)
{
    if (numBytes >= 6) {
	return Tcl_UtfToUniChar(source, (int *)c);
    }
    return FcUtf8ToUcs4((const FcChar8 *)source, c, numBytes);
}

void
TkpFontPkgInit(
    TCL_UNUSED(TkMainInfo *))	/* The application being created. */
{
}

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
 *----------------------------------------------------------------------
 *
 * GetTkFontAttributes --
 *
 *	Fill in TkFontAttributes from an XftFont.
 *----------------------------------------------------------------------
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
 *----------------------------------------------------------------------
 *
 * GetTkFontMetrics --
 *
 *	Fill in TkFontMetrics from an XftFont.
 *----------------------------------------------------------------------
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
 *----------------------------------------------------------------------
 *
 * GetBidiRuns --
 *
 *	Use SheenBidi to properly analyze text per UAX#9 and split it into
 *	level runs with correct directionality.
 *
 * Results:
 *	Returns number of runs created. Fills runsOut array (caller provides).
 *	Returns 0 on error or if text is simple LTR (single run created).
 *
 * Side effects:
 *	Caller must provide runsOut array with sufficient space (32 runs max).
 *
 *----------------------------------------------------------------------
 */

static int
GetBidiRuns(const char *utf8, int len, BidiRun *runsOut, int maxRuns)
{
    SBAlgorithmRef bidiAlg = NULL;
    SBParagraphRef paragraph = NULL;
    SBUInteger i;
    int runCount = 0;
    SBCodepointSequence codepointSeq;
    SBUInteger codepoints[2048];  /* Max codepoints */
    SBUInteger cpCount = 0;

    /* Convert UTF-8 to Unicode codepoints for SheenBidi. */
    int byteIdx = 0;
    while (byteIdx < len && cpCount < 2048) {
	FcChar32 c;
	int clen = utf8ToUcs4(utf8 + byteIdx, &c, len - byteIdx);
	if (clen <= 0) break;
	codepoints[cpCount++] = (SBUInteger)c;
	byteIdx += clen;
    }

    if (cpCount == 0) {
	/* Empty string - return single LTR run. */
	if (maxRuns > 0) {
	    runsOut[0].offset = 0;
	    runsOut[0].length = 0;
	    runsOut[0].level = 0;
	    runsOut[0].isRTL = 0;
	    return 1;
	}
	return 0;
    }

    /* Initialize SheenBidi codepoint sequence. */
    codepointSeq.stringEncoding = SBStringEncodingUTF32;
    codepointSeq.stringBuffer = (void *)codepoints;
    codepointSeq.stringLength = cpCount;

    /* Create bidi algorithm instance. */
    bidiAlg = SBAlgorithmCreate(&codepointSeq);
    if (!bidiAlg) {
	/* Fallback: single LTR run */
	if (maxRuns > 0) {
	    runsOut[0].offset = 0;
	    runsOut[0].length = len;
	    runsOut[0].level = 0;
	    runsOut[0].isRTL = 0;
	    return 1;
	}
	return 0;
    }

    /* Create paragraph with auto base direction detection. */
    paragraph = SBAlgorithmCreateParagraph(bidiAlg, 0, cpCount, SBLevelDefaultLTR);
    if (!paragraph) {
	SBAlgorithmRelease(bidiAlg);
	/* Fallback: single LTR run */
	if (maxRuns > 0) {
	    runsOut[0].offset = 0;
	    runsOut[0].length = len;
	    runsOut[0].level = 0;
	    runsOut[0].isRTL = 0;
	    return 1;
	}
	return 0;
    }

    /* Get the bidi line (entire paragraph as one line). */
    SBLineRef line = SBParagraphCreateLine(paragraph, 0, cpCount);
    if (!line) {
	SBParagraphRelease(paragraph);
	SBAlgorithmRelease(bidiAlg);
	/* Fallback: single LTR run */
	if (maxRuns > 0) {
	    runsOut[0].offset = 0;
	    runsOut[0].length = len;
	    runsOut[0].level = 0;
	    runsOut[0].isRTL = 0;
	    return 1;
	}
	return 0;
    }

    /* Extract runs from the line. */
    SBUInteger lineRunCount = SBLineGetRunCount(line);
    const SBRun *runs = SBLineGetRunsPtr(line);

    /* 
     * Convert SheenBidi runs to our BidiRun format.
     * Need to map codepoint offsets back to byte offsets.
     */
    int *cpToByte = (int *)malloc((cpCount + 1) * sizeof(int));
    if (!cpToByte) {
	SBLineRelease(line);
	SBParagraphRelease(paragraph);
	SBAlgorithmRelease(bidiAlg);
	if (maxRuns > 0) {
	    runsOut[0].offset = 0;
	    runsOut[0].length = len;
	    runsOut[0].level = 0;
	    runsOut[0].isRTL = 0;
	    return 1;
	}
	return 0;
    }

    /* Build codepoint-to-byte mapping. */
    byteIdx = 0;
    for (i = 0; i < cpCount; i++) {
	cpToByte[i] = byteIdx;
	FcChar32 c;
	int clen = utf8ToUcs4(utf8 + byteIdx, &c, len - byteIdx);
	if (clen > 0) byteIdx += clen;
    }
    cpToByte[cpCount] = len;  /* End position */

    for (i = 0; i < lineRunCount && runCount < maxRuns; i++) {
	SBUInteger runOffset = runs[i].offset;
	SBUInteger runLength = runs[i].length;
	SBLevel runLevel = runs[i].level;

	runsOut[runCount].offset = cpToByte[runOffset];
	runsOut[runCount].length = cpToByte[runOffset + runLength] - cpToByte[runOffset];
	runsOut[runCount].level = runLevel;
	runsOut[runCount].isRTL = (runLevel & 1) ? 1 : 0;  /* Odd levels are RTL */
	runCount++;
    }

    free(cpToByte);
    SBLineRelease(line);
    SBParagraphRelease(paragraph);
    SBAlgorithmRelease(bidiAlg);

    return runCount;
}



/*
 *----------------------------------------------------------------------
 *
 * InitFont --
 *
 *	Initializes the fields of a UnixFtFont structure. If fontPtr is NULL,
 *	also allocates a new UnixFtFont.
 *
 * Results:
 *	On error, frees fontPtr and returns NULL, otherwise returns fontPtr.
 *
 *----------------------------------------------------------------------
 */

static void
FinishedWithFont(
    UnixFtFont *fontPtr);

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
    fontPtr->faces = (UnixFtFace *)Tcl_Alloc(set->nfont * sizeof(UnixFtFace));
    fontPtr->nfaces = set->nfont;
    
    /*
     * Fill in information about each returned font
     */
    for (i = 0; i < set->nfont; i++) {
	fontPtr->faces[i].ftFont = 0;
	fontPtr->faces[i].ft0Font = 0;
	fontPtr->faces[i].source = set->fonts[i];
	if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0,
		&charset) == FcResultMatch) {
	    fontPtr->faces[i].charset = FcCharSetCopy(charset);
	} else {
	    fontPtr->faces[i].charset = 0;
	}
	fontPtr->faces[i].angle = 0.0;
    }
    
    fontPtr->display = Tk_Display(tkwin);
    fontPtr->screen = Tk_ScreenNumber(tkwin);
    fontPtr->colormap = Tk_Colormap(tkwin);
    fontPtr->visual = Tk_Visual(tkwin);
    fontPtr->ftDraw = 0;
    fontPtr->ncolors = 0;
    fontPtr->firstColor = -1;
    
    /*
     * Initialize shape context to zero - will be initialized lazily on first use.
     */
    memset(&fontPtr->shape, 0, sizeof(fontPtr->shape));
    
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
     * shaping context for this font.
     */
    X11Shape_Init(&fontPtr->shape);
    
    /*
     * Load all font faces into the shaper context.
     */
    for (i = 0; i < fontPtr->nfaces && i < 8; i++) {
	FcPattern *facePattern = fontPtr->faces[i].source;
	FcChar8 *file;
	int index;
	
	if (FcPatternGetString(facePattern, FC_FILE, 0, &file) == FcResultMatch &&
	    FcPatternGetInteger(facePattern, FC_INDEX, 0, &index) == FcResultMatch) {
	    
	    kbts_font *kbFont = kbts_ShapePushFontFromFile(fontPtr->shape.context,
						       (const char *)file,
						       index);
	    
	    if (kbFont && fontPtr->shape.numFonts < 8) {
		fontPtr->shape.fontMap[fontPtr->shape.numFonts].kbFont = kbFont;
		fontPtr->shape.fontMap[fontPtr->shape.numFonts].faceIndex = i;
		fontPtr->shape.numFonts++;
	    }
	}
    }
    
    return fontPtr;
}


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
    
    /* Clean up the persistent shaper context.*/
    X11Shape_Destroy(&fontPtr->shape);
    
    Tk_DeleteErrorHandler(handler);
}

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

    fontPtr->font.fa.underline = faPtr->underline;
    fontPtr->font.fa.overstrike = faPtr->overstrike;
    return &fontPtr->font;
}

void
TkpDeleteFont(
    TkFont *tkFontPtr)		/* Token of font to be deleted. */
{
    UnixFtFont *fontPtr = (UnixFtFont *) tkFontPtr;

    FinishedWithFont(fontPtr);
    /* XXX tkUnixFont.c doesn't free tkFontPtr... */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetFontFamilies --
 *
 *	Return information about the font families that are available on the
 *	display of the given window.
 *
 * Results:
 *	Modifies interp's result object to hold a list of all the available
 *	font families.
 *
 *----------------------------------------------------------------------
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
 *----------------------------------------------------------------------
 *
 * TkpGetSubFonts --
 *
 *	Called by [testfont subfonts] in the Tk testing package.
 *
 * Results:
 *	Sets interp's result to a list of the faces used by tkfont
 *
 *----------------------------------------------------------------------
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
 *----------------------------------------------------------------------
 *
 * TkpGetFontAttrsForChar --
 *
 *	Retrieve the font attributes of the actual font used to render a given
 *	character.
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
    UnixFtFont *fontPtr = (UnixFtFont *) tkfont;
				/* Structure describing the logical font */
    FcChar32 ucs4 = (FcChar32) c;
				/* UCS-4 character to map */
    XftFont *ftFont = GetFont(fontPtr, ucs4, 0.0);
				/* Actual font used to render the character */

    GetTkFontAttributes(tkwin, ftFont, faPtr);
    faPtr->underline = fontPtr->font.fa.underline;
    faPtr->overstrike = fontPtr->font.fa.overstrike;
}

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
	int total = 0;
	int i;
	
	/* Use the persistent shape context stored in the font */
	fontPtr->shape.glyphCount = 0;  /* Reset glyph buffer */
	UnixFontShapeString(fontPtr, source, (int)numBytes, &fontPtr->shape);
	
	for (i = 0; i < fontPtr->shape.glyphCount; i++) {
	    int next = total + fontPtr->shape.glyphs[i].advanceX;
	    if (maxLength >= 0 && next > maxLength) {
	        if (flags & TK_PARTIAL_OK) {
	            total = next;
	        } else if ((flags & TK_AT_LEAST_ONE) && total == 0) {
	            total = next;   /* must take at least something */
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
 *----------------------------------------------------------------------
 *
 * LookUpColor --
 *
 *	Convert a pixel value to an XftColor.  This can be slow due to the
 *	need to call XQueryColor, which involves a server round-trip.  To
 *	avoid that, a least-recently-used cache of up to MAX_CACHED_COLORS
 *	is kept, in the form of a linked list.  The returned color is moved
 *	to the front of the list, so repeatedly asking for the same one
 *	should be fast.
 *
 * Results:
 *	A pointer to the XftColor structure for the requested color is
 *	returned.
 *
 * Side effects:
 *	The converted color is stored in a cache in the UnixFtFont structure.  The cache
 *	can hold at most MAX_CACHED_COLORS colors.  If no more slots are available, the least
 *	recently used color is replaced with the new one.
 *----------------------------------------------------------------------
 */

static XftColor *
LookUpColor(Display *display,      /* Display to lookup colors on */
    UnixFtFont *fontPtr,   /* Font to search for cached colors */
    unsigned long pixel)   /* Pixel value to translate to XftColor */
{
    int i, last = -1, last2 = -1;
    XColor xcolor;

    for (i = fontPtr->firstColor;
	 i >= 0; last2 = last, last = i, i = fontPtr->colors[i].next) {

	if (pixel == fontPtr->colors[i].color.pixel) {
	    /*
	     * Color found in cache.  Move it to the front of the list and return it.
	     */
	    if (last >= 0) {
		fontPtr->colors[last].next = fontPtr->colors[i].next;
		fontPtr->colors[i].next = fontPtr->firstColor;
		fontPtr->firstColor = i;
	    }

	    return &fontPtr->colors[i].color;
	}
    }

    /*
     * Color wasn't found, so it needs to be added to the cache.
     * If a spare slot is available, it can be put there.  If not, last
     * will now point to the least recently used color, so replace that one.
     */

    if (fontPtr->ncolors < MAX_CACHED_COLORS) {
	last2 = -1;
	last = fontPtr->ncolors++;
    }

    /*
     * Translate the pixel value to a color.  Needs a server round-trip.
     */
    xcolor.pixel = pixel;
    XQueryColor(display, fontPtr->colormap, &xcolor);

    fontPtr->colors[last].color.color.red = xcolor.red;
    fontPtr->colors[last].color.color.green = xcolor.green;
    fontPtr->colors[last].color.color.blue = xcolor.blue;
    fontPtr->colors[last].color.color.alpha = 0xFFFF;
    fontPtr->colors[last].color.pixel = pixel;

    /*
     * Put at the front of the list.
     */
    if (last2 >= 0) {
	fontPtr->colors[last2].next = fontPtr->colors[last].next;
    }
    fontPtr->colors[last].next = fontPtr->firstColor;
    fontPtr->firstColor = last;

    return &fontPtr->colors[last].color;
}

#define NUM_SPEC    1024

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
    UnixFontDrawShapedText(display, drawable, gc, fontPtr,
                           source, numBytes,
                           (double)x, (double)y,
                           0.0);
}


/*
 *----------------------------------------------------------------------
 *
 * TkDrawAngledChars --
 *
 *	Draw some characters at an angle. This would be simple code, except
 *	Xft has bugs with cumulative errors in character positioning which are
 *	caused by trying to perform all calculations internally with integers.
 *	So we have to do the work ourselves with floating-point math.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Target drawable is updated.
 *
 *----------------------------------------------------------------------
 */

void
TkDrawAngledChars(
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
    double x, double y,		/* Coordinates at which to place origin of
				 * string when drawing. */
    double angle)		/* What angle to put text at, in degrees. */
{
    UnixFtFont *fontPtr = (UnixFtFont *) tkfont;
    UnixFontDrawShapedText(display, drawable, gc, fontPtr,
                           source, numBytes,
                           x, y, angle);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_DrawCharsInContext --
 *
 *	 Draws a substring of text using full shaping + bidi logic.
 * 	 The coordinates (x,y) are for the start of the **whole line**, not just the range.
 * 	 Only draws characters from rangeStart to rangeStart+rangeLength.
 *
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information gets drawn on the screen.
 *
 *----------------------------------------------------------------------
 */

void
Tk_DrawCharsInContext(
    Display *display,      /* Display on which to draw. */
    Drawable drawable,     /* Window or pixmap in which to draw. */
    GC gc,                 /* Graphics context for drawing characters. */
    Tk_Font tkfont,        /* Font in which characters will be drawn. */
    const char *source,    /* UTF-8 string to be displayed. */
    TCL_UNUSED(Tcl_Size),  /* Number of bytes in string. */
    Tcl_Size rangeStart,   /* Index of first glyph to draw (approx). */
    Tcl_Size rangeLength,  /* Number of glyphs to draw (approx). */
    int x, int y)          /* Coordinates for the start of the line. */
{
    UnixFtFont *fontPtr = (UnixFtFont *) tkfont;
    
    int i;

    /* Shape the entire line to get correct glyph positions. */
    UnixFontShapeString(fontPtr, source, (int)rangeStart + (int)rangeLength, &fontPtr->shape);

    /* Setup drawing target. */
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
        Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (fontPtr->ftDraw == NULL) {
        fontPtr->ftDraw = XftDrawCreate(display, drawable,
                                        fontPtr->visual, fontPtr->colormap);
    } else {
        Tk_ErrorHandler handler = Tk_CreateErrorHandler(display, -1, -1, -1, NULL, NULL);
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

    /* Draw glyphs in range. */
    XftGlyphFontSpec specs[NUM_SPEC];
    int nspec = 0;
    XftFont *prevFont = NULL;

    int glyphStart = (int)rangeStart;
    int glyphEnd   = glyphStart + (int)rangeLength;
    if (glyphEnd > fontPtr->shape.glyphCount) glyphEnd = fontPtr->shape.glyphCount;

    for (i = glyphStart; i < glyphEnd; i++) {
        /* Map kbFont to face index. */
        int faceIndex = 0;
        for (int j = 0; j < fontPtr->shape.numFonts; j++) {
            if (fontPtr->shape.fontMap[j].kbFont == fontPtr->shape.glyphs[i].font) {
                faceIndex = fontPtr->shape.fontMap[j].faceIndex;
                break;
            }
        }

        /* Pick the XftFont for this glyph. */
        XftFont *ftFont = GetFont(fontPtr, 0, 0.0);
        if (faceIndex < fontPtr->nfaces && fontPtr->faces[faceIndex].ft0Font) {
            ftFont = fontPtr->faces[faceIndex].ft0Font;
        }
        if (!ftFont) continue;

        /* Flush batch if font changed. */
        if (ftFont != prevFont && nspec > 0) {
            LOCK;
            XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
            UNLOCK;
            nspec = 0;
        }
        prevFont = ftFont;

        /* Glyph position. */
        int sx = (int)(x + fontPtr->shape.glyphs[i].x + 0.5);
        int sy = (int)(y + fontPtr->shape.glyphs[i].y + 0.5);

        specs[nspec].font  = ftFont;
        specs[nspec].glyph = fontPtr->shape.glyphs[i].glyphId;
        specs[nspec].x     = sx;
        specs[nspec].y     = sy;

        if (++nspec >= NUM_SPEC) {
            LOCK;
            XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
            UNLOCK;
            nspec = 0;
            prevFont = NULL;
        }
    }

    /* Flush remaining glyphs. */
    if (nspec > 0) {
        LOCK;
        XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
        UNLOCK;
    }

    if (tsdPtr->clipRegion != NULL) {
        XftDrawSetClip(fontPtr->ftDraw, NULL);
    }
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
    UnixFtFont *fontPtr = (UnixFtFont *) tkfont;
    
    double offsetX = 0.0;
    int i;

    UnixFontShapeString(fontPtr, source, (int)rangeStart + (int)rangeLength, &fontPtr->shape);

    /* Compute offset to the start of the requested range */
    for (i = 0; i < fontPtr->shape.glyphCount; i++) {
        /* TODO: proper cluster check — this is approximate */
        if (offsetX >= rangeStart) break;
        offsetX += (double)fontPtr->shape.glyphs[i].advanceX;
    }

    double rad   = angle * M_PI / 180.0;
    double cosA  = cos(rad);
    double sinA  = sin(rad);

    /* Adjust origin so that the drawn part starts at correct visual position */
    double drawOriginX = x + offsetX * cosA;
    double drawOriginY = y - offsetX * sinA;   /* note sign — y grows downward */

    /* Now draw the shaped text starting from adjusted origin */
    UnixFontDrawShapedText(
        display,
        drawable,
        gc,
        fontPtr,
        source + rangeStart,           /* pass only the substring */
        (int)rangeLength,
        drawOriginX,
        drawOriginY,
        angle
    );
}


void
TkUnixSetXftClipRegion(
    Region clipRegion)	/* The clipping region to install. */
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    tsdPtr->clipRegion = clipRegion;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_DrawCharsRotated --
 *
 *	Draw rotated text with proper glyph shaping and positioning,
 *	including RTL support via character range detection.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information gets drawn on the screen.
 *
 *----------------------------------------------------------------------
 */

void
Tk_DrawCharsRotated(
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
    int numBytes,		/* Number of bytes in string. */
    int x, int y,		/* Anchor point (origin of the string). */
    double angle)		/* Rotation angle in degrees (positive = counterclockwise). */
{
    UnixFtFont *fontPtr = (UnixFtFont *) tkfont;
    
    XGCValues values;
    XftColor *xftcolor;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    int i, j;

    /* Shape the string with primary + fallback fonts. */
    UnixFontShapeString(fontPtr, source, numBytes, &fontPtr->shape);

    /* Setup Xft drawing target. */
    if (fontPtr->ftDraw == NULL) {
	fontPtr->ftDraw = XftDrawCreate(display, drawable,
					fontPtr->visual, fontPtr->colormap);
    } else {
	Tk_ErrorHandler handler = Tk_CreateErrorHandler(display, -1, -1, -1, NULL, NULL);
	XftDrawChange(fontPtr->ftDraw, drawable);
	Tk_DeleteErrorHandler(handler);
    }

    XGetGCValues(display, gc, GCForeground, &values);
    xftcolor = LookUpColor(display, fontPtr, values.foreground);

    if (tsdPtr->clipRegion != NULL) {
	XftDrawSetClip(fontPtr->ftDraw, tsdPtr->clipRegion);
    }

    /* Rotation parameters. */
    double rad  = angle * M_PI / 180.0;
    double cosA = cos(rad);
    double sinA = sin(rad);

    /* Logical pen position in unshaped (horizontal) space */
    int penX = 0;
    int penY = 0;

    /* Batch buffer for Xft. */
    XftGlyphFontSpec specs[NUM_SPEC];
    int nspec = 0;
    XftFont *currentFont = NULL;

    for (i = 0; i < fontPtr->shape.glyphCount; i++) {
	FT_UInt glyphIndex = fontPtr->shape.glyphs[i].glyphId;

	/* Find which Xft font to use based on which kb_text_shaper font was selected. */
	int faceIndex = 0; /* default to primary font */
	for (j = 0; j < fontPtr->shape.numFonts; j++) {
	    if (fontPtr->shape.fontMap[j].kbFont == fontPtr->shape.glyphs[i].font) {
		faceIndex = fontPtr->shape.fontMap[j].faceIndex;
		break;
	    }
	}

	/* Get the appropriate rotated Xft font for this face. */
	XftFont *drawFont = NULL;
	if (faceIndex < fontPtr->nfaces) {
	    if (angle == 0.0) {
		drawFont = fontPtr->faces[faceIndex].ft0Font;
	    } else {
		if (fontPtr->faces[faceIndex].ftFont && 
		    fontPtr->faces[faceIndex].angle == angle) {
		    drawFont = fontPtr->faces[faceIndex].ftFont;
		}
	    }
	}

	/* If font not loaded for this face/angle, use GetFont to load it. */
	if (!drawFont) {
	    drawFont = GetFont(fontPtr, 0, angle);
	}

	if (!drawFont) {
	    continue; /* Skip this glyph if we can't get a font. */
	}

	/* If font changed, flush the batch */
	if (drawFont != currentFont && nspec > 0) {
	    LOCK;
	    XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	    nspec = 0;
	}
	currentFont = drawFont;

	/* Position in logical (horizontal) shaping space. */
	int gx = fontPtr->shape.glyphs[i].x;
	int gy = fontPtr->shape.glyphs[i].y;

	/* Rotate coordinates around the origin (x,y). */
	int rx = x + (int)(gx * cosA - gy * sinA + 0.5);
	int ry = y + (int)(gx * sinA + gy * cosA + 0.5);

	specs[nspec].font  = drawFont;
	specs[nspec].glyph = glyphIndex;
	specs[nspec].x     = rx;
	specs[nspec].y     = ry;

	if (++nspec >= NUM_SPEC) {
	    LOCK;
	    XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	    nspec = 0;
	    currentFont = NULL; /* Force font check on next iteration. */
	}

	/* Advance pen in logical space. */
	penX += fontPtr->shape.glyphs[i].advanceX;
	penY += 0; /* No Y advance in horizontal text. */
    }

    /* Flush any remaining glyphs. */
    if (nspec > 0) {
	LOCK;
	XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	UNLOCK;
    }

    /* Restore clipping. */
    if (tsdPtr->clipRegion != NULL) {
	XftDrawSetClip(fontPtr->ftDraw, NULL);
    }

    /* Underline / overstrike — rotated. */
    if (fontPtr->font.fa.underline || fontPtr->font.fa.overstrike) {
	/* Total advance vector after rotation. */
	double totalAdvanceX = (double)penX * cosA - (double)penY * sinA;
	double totalAdvanceY = (double)penX * sinA + (double)penY * cosA;

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
		points[4] = points[0];

		XFillPolygon(display, drawable, gc, points, 5, Complex, CoordModeOrigin);
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
		points[4] = points[0];

		XFillPolygon(display, drawable, gc, points, 5, Complex, CoordModeOrigin);
		XDrawLines(display, drawable, gc, points, 5, CoordModeOrigin);
	    }
	}
    }

}
/*
 *----------------------------------------------------------------------
 *
 * Functions to support text shaping and bi-directional rendering.
 *
 *----------------------------------------------------------------------
 */


/* Common shaping helper: uses SheenBidi for bidi, then kb_text_shaper for shaping */
void
UnixFontShapeString(
    UnixFtFont *fontPtr,
    const char *source,
    int numBytes,
    X11Shape *shapePtr)
{
    int i, r;
    BidiRun bidiRuns[32];
    int numBidiRuns;
    int globalPenX = 0;
    int globalPenY = 0;

    if (!shapePtr || !source || numBytes <= 0) {
        return;
    }

    if (!shapePtr->context) {
        X11Shape_Init(shapePtr);
        
        if (fontPtr->nfaces > 0) {
            FcPattern *pattern = fontPtr->faces[0].source;
            FcChar8 *file;
            int index;
            
            if (FcPatternGetString(pattern, FC_FILE, 0, &file) == FcResultMatch &&
                FcPatternGetInteger(pattern, FC_INDEX, 0, &index) == FcResultMatch) {
                
                fprintf(stderr, "Loading font: %s index %d\n", file, index);
                kbts_font *kbFont = kbts_ShapePushFontFromFile(shapePtr->context,
                                                   (const char *)file,
                                                   index);
                
                if (kbFont) {
                    shapePtr->fontMap[0].kbFont = kbFont;
                    shapePtr->fontMap[0].faceIndex = 0;
                    shapePtr->numFonts = 1;
                    fprintf(stderr, "Font loaded successfully\n");
                } else {
                    fprintf(stderr, "Font load FAILED\n");
                }
            }
        }
    }

    if (shapePtr->numFonts == 0) {
        fprintf(stderr, "No fonts loaded - returning 0 glyphs\n");
        shapePtr->glyphCount = 0;
        return;
    }

    shapePtr->glyphCount = 0;

    fprintf(stderr, "Shaping string: '%.*s' (%d bytes)\n", numBytes, source, numBytes);
    
    numBidiRuns = GetBidiRuns(source, numBytes, bidiRuns, 32);

    if (numBidiRuns <= 0) {
        bidiRuns[0].offset = 0;
        bidiRuns[0].length = numBytes;
        bidiRuns[0].level = 0;
        bidiRuns[0].isRTL = 0;
        numBidiRuns = 1;
    }

    fprintf(stderr, "Got %d bidi runs\n", numBidiRuns);

    for (r = 0; r < numBidiRuns; r++) {
        const char *runText = source + bidiRuns[r].offset;
        int runLen = bidiRuns[r].length;
        int runIsRTL = bidiRuns[r].isRTL;
        kbts_run run;
        kbts_glyph *glyph;
        int runPenX = 0;
        int runPenY = 0;
        int runSafety = 0;

        if (runLen <= 0) continue;

        fprintf(stderr, "  Run %d: len=%d RTL=%d\n", r, runLen, runIsRTL);

        kbts_ShapeBegin(shapePtr->context, 
                        runIsRTL ? KBTS_DIRECTION_RTL : KBTS_DIRECTION_LTR, 
                        KBTS_LANGUAGE_DONT_KNOW);

        kbts_ShapeUtf8(shapePtr->context, runText, runLen, 
                       KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);

        kbts_ShapeEnd(shapePtr->context);

        fprintf(stderr, "  Starting kbts_ShapeRun loop\n");
        while (kbts_ShapeRun(shapePtr->context, &run) && 
               shapePtr->glyphCount < 2048 && 
               runSafety++ < 100) {
            
            fprintf(stderr, "    ShapeRun iteration %d\n", runSafety);
            kbts_glyph_iterator it = run.Glyphs;
            int glyphsFoundInThisRun = 0;

            while (kbts_GlyphIteratorNext(&it, &glyph)) {
                if (shapePtr->glyphCount >= 2048) break;

                shapePtr->glyphs[shapePtr->glyphCount].glyphId = glyph->Id;
                shapePtr->glyphs[shapePtr->glyphCount].font = run.Font;
                shapePtr->glyphs[shapePtr->glyphCount].x = globalPenX + runPenX + glyph->OffsetX;
                shapePtr->glyphs[shapePtr->glyphCount].y = globalPenY + runPenY + glyph->OffsetY;
                shapePtr->glyphs[shapePtr->glyphCount].advanceX = glyph->AdvanceX;

                runPenX += glyph->AdvanceX;
                runPenY += glyph->AdvanceY;
                shapePtr->glyphCount++;
                glyphsFoundInThisRun++;
            }

            fprintf(stderr, "    Found %d glyphs, total now %d\n", glyphsFoundInThisRun, shapePtr->glyphCount);

            if (glyphsFoundInThisRun == 0) break;
        }

        fprintf(stderr, "  Finished ShapeRun loop after %d iterations\n", runSafety);

        globalPenX += runPenX;
        globalPenY += runPenY;

        if (shapePtr->glyphCount >= 2048) break;
    }

    fprintf(stderr, "Shaping complete: %d glyphs, total width=%d\n", shapePtr->glyphCount, globalPenX);
}

 /*
 * Unified drawing function for shaped + bidirectional text.
 * Handles both horizontal (angle == 0.0) and rotated cases.
 * Uses integer math when angle == 0 for speed & exactness.
 */
static void
UnixFontDrawShapedText(
    Display *display,
    Drawable drawable,
    GC gc,
    UnixFtFont *fontPtr,
    const char *source,
    int numBytes, 
    double originX,
    double originY,
    double angle_deg)
{
    XGCValues values;
    XftColor *xftcolor;
    ThreadSpecificData *tsdPtr;
    double rad, cosA, sinA;
    int i;

    tsdPtr = (ThreadSpecificData *) Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    rad   = angle_deg * M_PI / 180.0;
    cosA  = cos(rad);
    sinA  = sin(rad);

    UnixFontShapeString(fontPtr, source, numBytes, &fontPtr->shape);

    /* Prepare Xft drawing target. */
    if (fontPtr->ftDraw == NULL) {
        fontPtr->ftDraw = XftDrawCreate(display, drawable,
                                        fontPtr->visual, fontPtr->colormap);
    } else {
        Tk_ErrorHandler handler = Tk_CreateErrorHandler(display, -1, -1, -1, NULL, NULL);
        XftDrawChange(fontPtr->ftDraw, drawable);
        Tk_DeleteErrorHandler(handler);
    }

    XGetGCValues(display, gc, GCForeground, &values);
    xftcolor = LookUpColor(display, fontPtr, values.foreground);

    if (tsdPtr->clipRegion != NULL) {
        XftDrawSetClip(fontPtr->ftDraw, tsdPtr->clipRegion);
    }

    /* Draw batch. */
    XftGlyphFontSpec specs[NUM_SPEC];
    int nspec = 0;
    XftFont *prevFont = NULL;

    for (i = 0; i < fontPtr->shape.glyphCount; i++) {
        int faceIndex = 0;
        int j;

        /* Map kb_text_shaper font back to our face index. */
        for (j = 0; j < fontPtr->shape.numFonts; j++) {
            if (fontPtr->shape.fontMap[j].kbFont == fontPtr->shape.glyphs[i].font) {
                faceIndex = fontPtr->shape.fontMap[j].faceIndex;
                break;
            }
        }

        /* Get appropriate rotated or normal Xft font. */
        XftFont *ftFont = GetFont(fontPtr, 0, angle_deg);
        if (faceIndex < fontPtr->nfaces) {
            if (angle_deg == 0.0) {
                if (fontPtr->faces[faceIndex].ft0Font) {
                    ftFont = fontPtr->faces[faceIndex].ft0Font;
                }
            } else {
                if (fontPtr->faces[faceIndex].ftFont &&
                    fontPtr->faces[faceIndex].angle == angle_deg) {
                    ftFont = fontPtr->faces[faceIndex].ftFont;
                }
            }
        }

        if (!ftFont) continue;

        /* Flush batch on font change. */
        if (ftFont != prevFont && nspec > 0) {
            LOCK;
            XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
            UNLOCK;
            nspec = 0;
        }
        prevFont = ftFont;

        double gx = fontPtr->shape.glyphs[i].x;
        double gy = fontPtr->shape.glyphs[i].y;

        int sx, sy;
        if (angle_deg == 0.0) {
            sx = (int)(originX + gx + 0.5);
            sy = (int)(originY + gy + 0.5);
        } else {
            sx = (int)(originX + gx * cosA - gy * sinA + 0.5);
            sy = (int)(originY + gx * sinA + gy * cosA + 0.5);
        }

        specs[nspec].font  = ftFont;
        specs[nspec].glyph = fontPtr->shape.glyphs[i].glyphId;
        specs[nspec].x     = sx;
        specs[nspec].y     = sy;

        if (++nspec >= NUM_SPEC) {
            LOCK;
            XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
            UNLOCK;
            nspec = 0;
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

    /* Underline / overstrike. */
    if (fontPtr->font.fa.underline || fontPtr->font.fa.overstrike) {
        int totalAdv = 0;
        for (i = 0; i < fontPtr->shape.glyphCount; i++) {
            totalAdv += fontPtr->shape.glyphs[i].advanceX;
        }

        double barWidth  = (double) totalAdv;
        double barHeight = (double) fontPtr->font.underlineHeight;
        XPoint pts[5];
        double dy;

        if (fontPtr->font.fa.underline) {
            dy = (double) fontPtr->font.underlinePos;
            if (barHeight <= 1.0) dy += 1.0;

            pts[0].x = (int)(originX + dy * sinA + 0.5);
            pts[0].y = (int)(originY + dy * cosA + 0.5);
            pts[1].x = (int)(originX + dy * sinA + barWidth * cosA + 0.5);
            pts[1].y = (int)(originY + dy * cosA - barWidth * sinA + 0.5);

            if (barHeight <= 1.0) {
                XDrawLines(display, drawable, gc, pts, 2, CoordModeOrigin);
            } else {
                pts[2].x = pts[1].x + (int)( barHeight * sinA + 0.5);
                pts[2].y = pts[1].y + (int)(-barHeight * cosA + 0.5);
                pts[3].x = pts[0].x + (int)( barHeight * sinA + 0.5);
                pts[3].y = pts[0].y + (int)(-barHeight * cosA + 0.5);
                pts[4]   = pts[0];
                XFillPolygon(display, drawable, gc, pts, 5, Complex, CoordModeOrigin);
                XDrawLines (display, drawable, gc, pts, 5, CoordModeOrigin);
            }
        }

        if (fontPtr->font.fa.overstrike) {
            dy = - (double)fontPtr->font.fm.descent - ((double)fontPtr->font.fm.ascent / 10.0);

            pts[0].x = (int)(originX + dy * sinA + 0.5);
            pts[0].y = (int)(originY + dy * cosA + 0.5);
            pts[1].x = (int)(originX + dy * sinA + barWidth * cosA + 0.5);
            pts[1].y = (int)(originY + dy * cosA - barWidth * sinA + 0.5);

            if (barHeight <= 1.0) {
                XDrawLines(display, drawable, gc, pts, 2, CoordModeOrigin);
            } else {
                pts[2].x = pts[1].x + (int)( barHeight * sinA + 0.5);
                pts[2].y = pts[1].y + (int)(-barHeight * cosA + 0.5);
                pts[3].x = pts[0].x + (int)( barHeight * sinA + 0.5);
                pts[3].y = pts[0].y + (int)(-barHeight * cosA + 0.5);
                pts[4]   = pts[0];
                XFillPolygon(display, drawable, gc, pts, 5, Complex, CoordModeOrigin);
                XDrawLines (display, drawable, gc, pts, 5, CoordModeOrigin);
            }
        }
    }

}

/*
 * Returns the total advance width (in pixels) after full shaping + bidi.
 * This is the correct way to measure complex text.
 */
static int
UnixFontGetShapedWidth(
    UnixFtFont *fontPtr,
    const char *source,
    Tcl_Size numBytes)
{
    
    int totalWidth = 0;
    int i;

    UnixFontShapeString(fontPtr, source, (int)numBytes, &fontPtr->shape);

    for (i = 0; i < fontPtr->shape.glyphCount; i++) {
        totalWidth += fontPtr->shape.glyphs[i].advanceX;
    }

    return totalWidth;
}

/* Initialize shaping context. */
void X11Shape_Init(X11Shape *s)
{
    memset(s, 0, sizeof(*s));
    s->context = kbts_CreateShapeContext(NULL, NULL);
    s->cachedFamilyId = None;
}

/* Add a fallback font. */
void X11Shape_AddFont(X11Shape *s, kbts_font *f)
{
    if (s->context) {
	kbts_ShapePushFont(s->context, f);
    }
}


/* Clean up shaping context. */
void X11Shape_Destroy(X11Shape *s)
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
