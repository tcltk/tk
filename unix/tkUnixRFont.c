/*
 * tkUnixRFont.c --
 *
 *	Alternate implementation of tkUnixFont.c using Xft. Supports text shaping
 *  and bidirectional rendering for complex text and RTL languages. 
 *
 * Copyright (c) 2002-2003 Keith Packard
 * Copyright (c) 2026 Kevin Walzer
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

/*
 * Process-wide cache of raw font file bytes, keyed by path + index.
 * Each UnixFtFont context parses its own kbts_font from the cached bytes,
 * avoiding repeated disk I/O while keeping all kbts_font pointers
 * strictly private to their owning context.
 */
#define KBTS_FONT_CACHE_SIZE 64

typedef struct {
    char   path[PATH_MAX];
    int    index;
    void  *fontData;
    int    fontDataSize;
} KbtsFontDataCacheEntry;

static KbtsFontDataCacheEntry kbtsFontDataCache[KBTS_FONT_CACHE_SIZE];
static int                    kbtsFontDataCacheCount = 0;
TCL_DECLARE_MUTEX(kbtsFontCacheMutex);

/*
 * One shaped glyph stored in the LRU cache.
 * xftFont is resolved at shape time so the draw loop needs no secondary lookup.
 * kbFont is retained only for the rotated-text face-index lookup.
 */
typedef struct {
    kbts_font *kbFont;   /* Used only at shape time / rotated draw. */
    XftFont   *xftFont;  /* Resolved at shape time, used directly at draw time. */
    FT_UInt    glyphId;
    int        x, y;
    int        advanceX;
} X11ShapedGlyph;

/*
 * One slot in the 16-entry LRU shaped-glyph cache.
 * glyphs is heap-allocated on demand, starting at 64 and doubling.
 * textLen == -1 means the slot is empty.
 */
typedef struct {
    char          *text;
    int            textLen;
    X11ShapedGlyph *glyphs;
    int            glyphCount;
    int            glyphCapacity;
    int            totalWidth;
    int            lruSeq;
} X11ShapeCacheEntry;

#define X11SHAPE_CACHE_SIZE 16

/*
 * Per-UnixFtFont shaping engine.
 * context is the kb_text_shaper instance.
 * fontMap maps kbts_font * to Xft face indices.
 * cache is the 16-slot LRU of shaped results.
 */
typedef struct {
    kbts_shape_context *context;

    struct {
        kbts_font *kbFont;
        int        faceIndex;
    } fontMap[8];
    int numFonts;

    X11ShapeCacheEntry cache[X11SHAPE_CACHE_SIZE];
    int                lruCounter;
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
static X11ShapeCacheEntry *X11Shape_LastShaped(X11Shape *shapePtr);
static XftFont *        GetFontForFace(UnixFtFont *fontPtr, int faceIndex, double angle);
static kbts_font *      KbtsFontCacheLookupOrLoad(kbts_shape_context *ctx, const char *path, int index);
static void             KbtsFontCacheDestroy(void);

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

/*
 *----------------------------------------------------------------------
 *
 * KbtsFontCacheLookupOrLoad --
 *
 *	Process-wide font data cache lookup. Given a file path and face
 *	index, returns a kbts_font * freshly parsed into the caller's
 *	kbts_shape_context from cached bytes. On the first call for a given
 *	path+index, reads the file from disk and stores the bytes. Subsequent
 *	calls skip disk I/O. If the cache is full, falls back to
 *	kbts_ShapePushFontFromFile. Protected by kbtsFontCacheMutex.
 *
 * Results:
 *	A kbts_font * owned by ctx, or NULL on failure.
 *
 * Side effects:
 *	May read font file from disk and allocate cache entry on first call
 *	for a given path+index.
 *
 *----------------------------------------------------------------------
 */

static kbts_font *
KbtsFontCacheLookupOrLoad(kbts_shape_context *ctx,
                           const char *path, int index)
{
    int i;
    void *fontData     = NULL;
    int   fontDataSize = 0;

    Tcl_MutexLock(&kbtsFontCacheMutex);
    for (i = 0; i < kbtsFontDataCacheCount; i++) {
        if (kbtsFontDataCache[i].index == index &&
                strcmp(kbtsFontDataCache[i].path, path) == 0) {
            fontData     = kbtsFontDataCache[i].fontData;
            fontDataSize = kbtsFontDataCache[i].fontDataSize;
            break;
        }
    }
    Tcl_MutexUnlock(&kbtsFontCacheMutex);

    if (!fontData) {
        /* Cold miss — read from disk. */
        FILE *fp = fopen(path, "rb");
        if (!fp) return NULL;
        fseek(fp, 0, SEEK_END);
        long fileSize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (fileSize <= 0) { fclose(fp); return NULL; }

        fontData = Tcl_Alloc((size_t)fileSize);
        if (fread(fontData, 1, (size_t)fileSize, fp) != (size_t)fileSize) {
            fclose(fp);
            Tcl_Free(fontData);
            return NULL;
        }
        fclose(fp);
        fontDataSize = (int)fileSize;

        Tcl_MutexLock(&kbtsFontCacheMutex);
        if (kbtsFontDataCacheCount < KBTS_FONT_CACHE_SIZE) {
            strncpy(kbtsFontDataCache[kbtsFontDataCacheCount].path, path, PATH_MAX - 1);
            kbtsFontDataCache[kbtsFontDataCacheCount].path[PATH_MAX - 1] = '\0';
            kbtsFontDataCache[kbtsFontDataCacheCount].index        = index;
            kbtsFontDataCache[kbtsFontDataCacheCount].fontData     = fontData;
            kbtsFontDataCache[kbtsFontDataCacheCount].fontDataSize = fontDataSize;
            kbtsFontDataCacheCount++;
            Tcl_MutexUnlock(&kbtsFontCacheMutex);
        } else {
            /* Cache full — fall back to file-based load, library owns the data. */
            Tcl_MutexUnlock(&kbtsFontCacheMutex);
            Tcl_Free(fontData);
            return kbts_ShapePushFontFromFile(ctx, path, index);
        }
    }

    /*
     * Parse a fresh kbts_font from the (possibly cached) bytes into ctx.
     * This pointer is owned by ctx and is only valid until ctx is destroyed.
     */
    return kbts_ShapePushFontFromMemory(ctx, fontData, fontDataSize, index);
}

/*
 *----------------------------------------------------------------------
 *
 * KbtsFontCacheDestroy --
 *
 *	Frees all entries in the process-wide font data cache.
 *	Intended to be called at interpreter teardown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All cached font data buffers are freed.
 *
 *----------------------------------------------------------------------
 */

static void
KbtsFontCacheDestroy(void)
{
    int i;
    Tcl_MutexLock(&kbtsFontCacheMutex);
    for (i = 0; i < kbtsFontDataCacheCount; i++) {
        if (kbtsFontDataCache[i].fontData) {
            Tcl_Free(kbtsFontDataCache[i].fontData);
            kbtsFontDataCache[i].fontData = NULL;
        }
    }
    kbtsFontDataCacheCount = 0;
    Tcl_MutexUnlock(&kbtsFontCacheMutex);
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
    /* Scan for characters that might require Bidi reordering.
     * U+0590 is the start of the Hebrew block; everything 
     * below is LTR. 
     */
    int needsBidi = 0;
    const unsigned char *p = (const unsigned char *)utf8;
    for (int i = 0; i < len; i++) {
        if (p[i] > 0x7F) { /* Check for non-ASCII */
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
        runsOut[0].level = 0;
        runsOut[0].isRTL = 0;
        return 1;
    }

    SBAlgorithmRef bidiAlg = NULL;
    SBParagraphRef paragraph = NULL;
    SBUInteger cpCount = 0;
    SBUInteger codepoints[1024]; /* Sufficient for most UI strings */

    int byteIdx = 0;
    while (byteIdx < len && cpCount < 1024) {
        FcChar32 c;
        int clen = utf8ToUcs4(utf8 + byteIdx, &c, len - byteIdx);
        if (clen <= 0) break;
        codepoints[cpCount++] = (SBUInteger)c;
        byteIdx += clen;
    }

    SBCodepointSequence codepointSeq = {SBStringEncodingUTF32, codepoints, cpCount};
    bidiAlg = SBAlgorithmCreate(&codepointSeq);
    paragraph = SBAlgorithmCreateParagraph(bidiAlg, 0, cpCount, SBLevelDefaultLTR);
    SBLineRef line = SBParagraphCreateLine(paragraph, 0, cpCount);

    SBUInteger lineRunCount = SBLineGetRunCount(line);
    const SBRun *runs = SBLineGetRunsPtr(line);

    /* OPTIMIZATION: Use stack memory for the mapping to avoid malloc latency. */
    int cpToByteStack[1025];
    int *cpToByte = (cpCount < 1024) ? cpToByteStack : (int *)malloc((cpCount + 1) * sizeof(int));

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
        runsOut[runCount].length = cpToByte[runs[i].offset + runs[i].length] - runsOut[runCount].offset;
        runsOut[runCount].level = runs[i].level;
        runsOut[runCount].isRTL = (runs[i].level & 1);
        runCount++;
    }

    if (cpToByte != cpToByteStack) free(cpToByte);
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
     * Load all font faces into the shaper context via the process-wide
     * font data cache. KbtsFontCacheLookupOrLoad avoids repeated disk I/O
     * when multiple UnixFtFont objects resolve to the same physical file.
     * Fallback faces have their ft0Font opened here so xftFont pointers
     * can be resolved at shape time.
     */
    for (i = 0; i < fontPtr->nfaces && i < 8; i++) {
	FcPattern *facePattern = fontPtr->faces[i].source;
	FcChar8 *file = NULL;
	int index = 0;

	if (FcPatternGetString(facePattern, FC_FILE, 0, &file) != FcResultMatch || !file) {
	    continue;
	}
	FcPatternGetInteger(facePattern, FC_INDEX, 0, &index);

	kbts_font *kbFont = KbtsFontCacheLookupOrLoad(fontPtr->shape.context,
						       (const char *)file, index);
	if (!kbFont) continue;

	/*
	 * Ensure the Xft face is open at angle 0 for all fallback faces.
	 * Face 0 was already opened by GetFont earlier in InitFont.
	 */
	if (i > 0 && !fontPtr->faces[i].ft0Font) {
	    FcPattern *pat = FcFontRenderPrepare(0, fontPtr->pattern, facePattern);
	    LOCK;
	    fontPtr->faces[i].ft0Font = XftFontOpenPattern(fontPtr->display, pat);
	    UNLOCK;
	}

	if (fontPtr->shape.numFonts < 8) {
	    fontPtr->shape.fontMap[fontPtr->shape.numFonts].kbFont    = kbFont;
	    fontPtr->shape.fontMap[fontPtr->shape.numFonts].faceIndex = i;
	    fontPtr->shape.numFonts++;
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

    /*
     * Shape once and reuse. The LRU cache inside UnixFontShapeString handles
     * the common case where Tk measures the same string multiple times before
     * drawing it. Do not zero glyphCount here — that defeats the cache.
     */
    UnixFontShapeString(fontPtr, source, (int)numBytes, &fontPtr->shape);

    X11ShapeCacheEntry *entry = X11Shape_LastShaped(&fontPtr->shape);
    if (!entry || entry->glyphCount == 0) {
	*lengthPtr = 0;
	return 0;
    }

    for (i = 0; i < entry->glyphCount; i++) {
	int next = total + entry->glyphs[i].advanceX;
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

    /*
     * Compute total shaped width directly from already-shaped glyphs.
     * This avoids calling UnixFontGetShapedWidth which would re-shape.
     */
    int byteCount;
    if (total >= entry->totalWidth) {
	byteCount = (int)numBytes;
    } else {
	byteCount = (int)(numBytes * (double)total / (entry->totalWidth + 1) + 0.5);
	if (byteCount < 1 && (flags & TK_AT_LEAST_ONE)) byteCount = 1;
	if (byteCount > (int)numBytes) byteCount = (int)numBytes;
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
    UnixFontShapeString(fontPtr, source, (int)rangeStart + (int)rangeLength,
                        &fontPtr->shape);

    X11ShapeCacheEntry *entry = X11Shape_LastShaped(&fontPtr->shape);
    if (!entry || entry->glyphCount == 0) return;

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

    /* Draw glyphs in range using xftFont resolved at shape time. */
    XftGlyphFontSpec specs[NUM_SPEC];
    int nspec = 0;
    XftFont *prevFont = NULL;

    int glyphStart = (int)rangeStart;
    int glyphEnd   = glyphStart + (int)rangeLength;
    if (glyphEnd > entry->glyphCount) glyphEnd = entry->glyphCount;

    for (i = glyphStart; i < glyphEnd; i++) {
        XftFont *ftFont = entry->glyphs[i].xftFont;
        if (!ftFont) ftFont = fontPtr->faces[0].ft0Font;
        if (!ftFont) continue;

        if (ftFont != prevFont && nspec > 0) {
            LOCK;
            XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
            UNLOCK;
            nspec = 0;
        }
        prevFont = ftFont;

        specs[nspec].font  = ftFont;
        specs[nspec].glyph = entry->glyphs[i].glyphId;
        specs[nspec].x     = (int)(x + entry->glyphs[i].x + 0.5);
        specs[nspec].y     = (int)(y + entry->glyphs[i].y + 0.5);

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

    UnixFontShapeString(fontPtr, source, (int)rangeStart + (int)rangeLength,
                        &fontPtr->shape);

    X11ShapeCacheEntry *entry = X11Shape_LastShaped(&fontPtr->shape);
    if (!entry || entry->glyphCount == 0) return;

    /* Compute x offset to the start of the requested range. */
    for (i = 0; i < entry->glyphCount; i++) {
        if (offsetX >= rangeStart) break;
        offsetX += (double)entry->glyphs[i].advanceX;
    }

    double rad   = angle * M_PI / 180.0;
    double cosA  = cos(rad);
    double sinA  = sin(rad);

    double drawOriginX = x + offsetX * cosA;
    double drawOriginY = y - offsetX * sinA;

    UnixFontDrawShapedText(
        display, drawable, gc, fontPtr,
        source + rangeStart, (int)rangeLength,
        drawOriginX, drawOriginY, angle
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
    int i;

    /* Shape the string — cheap if already cached. */
    UnixFontShapeString(fontPtr, source, numBytes, &fontPtr->shape);

    X11ShapeCacheEntry *entry = X11Shape_LastShaped(&fontPtr->shape);
    if (!entry || entry->glyphCount == 0) return;

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

    double rad  = angle * M_PI / 180.0;
    double cosA = cos(rad);
    double sinA = sin(rad);

    /* Track total logical advance for underline/overstrike. */
    int penX = 0;
    int penY = 0;

    XftGlyphFontSpec specs[NUM_SPEC];
    int nspec = 0;
    XftFont *currentFont = NULL;

    for (i = 0; i < entry->glyphCount; i++) {
	/*
	 * For rotated text, find the face index via kbFont then get the
	 * angled variant through GetFontForFace.
	 */
	XftFont *drawFont = NULL;
	for (int j = 0; j < fontPtr->shape.numFonts; j++) {
	    if (fontPtr->shape.fontMap[j].kbFont == entry->glyphs[i].kbFont) {
		drawFont = GetFontForFace(fontPtr,
					  fontPtr->shape.fontMap[j].faceIndex,
					  angle);
		break;
	    }
	}
	if (!drawFont) drawFont = GetFontForFace(fontPtr, 0, angle);
	if (!drawFont) continue;

	if (drawFont != currentFont && nspec > 0) {
	    LOCK;
	    XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	    nspec = 0;
	}
	currentFont = drawFont;

	double gx = (double)entry->glyphs[i].x;
	double gy = (double)entry->glyphs[i].y;

	specs[nspec].font  = drawFont;
	specs[nspec].glyph = entry->glyphs[i].glyphId;
	specs[nspec].x     = x + (int)(gx * cosA - gy * sinA + 0.5);
	specs[nspec].y     = y + (int)(gx * sinA + gy * cosA + 0.5);

	if (++nspec >= NUM_SPEC) {
	    LOCK;
	    XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	    nspec       = 0;
	    currentFont = NULL;
	}

	penX += entry->glyphs[i].advanceX;
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
 * UnixFontShapeString --
 *
 *	Shape a UTF-8 string using SheenBidi for directional analysis and
 *	kb_text_shaper for glyph shaping. Results are stored in a 16-slot
 *	LRU cache inside shapePtr. On a cache hit the function returns
 *	immediately without reshaping. XftFont pointers are resolved once
 *	per run at shape time so the draw path needs no secondary lookups.
 *
 * Results:
 *	None. Caller retrieves results via X11Shape_LastShaped().
 *
 * Side effects:
 *	May allocate or grow per-slot glyph buffers. Lazy-initializes the
 *	shaper context on first call.
 *
 *----------------------------------------------------------------------
 */

void
UnixFontShapeString(
    UnixFtFont *fontPtr,
    const char *source,
    int numBytes,
    X11Shape *shapePtr)
{
    int r, i;
    BidiRun bidiRuns[32];
    int numBidiRuns;
    int globalPenX = 0;
    int globalPenY = 0;
    int slot;
    int lruMin;
    int lruMinSlot;

    if (!shapePtr || !source || numBytes <= 0) {
        return;
    }

    /*
     * Search the LRU cache. On a hit, bump the sequence number and return
     * immediately — no shaping needed.
     */
    for (i = 0; i < X11SHAPE_CACHE_SIZE; i++) {
        if (shapePtr->cache[i].textLen == numBytes &&
                shapePtr->cache[i].text &&
                memcmp(shapePtr->cache[i].text, source, numBytes) == 0) {
            shapePtr->cache[i].lruSeq = ++shapePtr->lruCounter;
            return;
        }
    }

    /*
     * Cache miss. Find the least-recently-used slot to evict.
     * Prefer empty slots (textLen < 0) over occupied ones.
     */
    lruMin     = shapePtr->cache[0].lruSeq;
    lruMinSlot = 0;
    for (i = 1; i < X11SHAPE_CACHE_SIZE; i++) {
        if (shapePtr->cache[i].textLen < 0) {
            lruMinSlot = i;
            lruMin     = -1;
            break;
        }
        if (shapePtr->cache[i].lruSeq < lruMin) {
            lruMin     = shapePtr->cache[i].lruSeq;
            lruMinSlot = i;
        }
    }
    slot = lruMinSlot;

    /*
     * Lazy-init the shaper context if needed. Uses the process-wide font
     * data cache so font files are only read from disk once per process.
     */
    if (!shapePtr->context) {
        X11Shape_Init(shapePtr);
        for (i = 0; i < fontPtr->nfaces && i < 8; i++) {
            FcPattern *facePattern = fontPtr->faces[i].source;
            FcChar8 *file = NULL;
            int index = 0;

            if (FcPatternGetString(facePattern, FC_FILE, 0, &file) != FcResultMatch || !file) {
                continue;
            }
            FcPatternGetInteger(facePattern, FC_INDEX, 0, &index);

            kbts_font *kbFont = KbtsFontCacheLookupOrLoad(shapePtr->context,
                                                           (const char *)file, index);
            if (!kbFont) continue;

            if (i > 0 && !fontPtr->faces[i].ft0Font) {
                FcPattern *pat = FcFontRenderPrepare(0, fontPtr->pattern, facePattern);
                LOCK;
                fontPtr->faces[i].ft0Font = XftFontOpenPattern(fontPtr->display, pat);
                UNLOCK;
            }

            if (shapePtr->numFonts < 8) {
                shapePtr->fontMap[shapePtr->numFonts].kbFont    = kbFont;
                shapePtr->fontMap[shapePtr->numFonts].faceIndex = i;
                shapePtr->numFonts++;
            }
        }
    }

    /*
     * Evict old entry text key. Keep the glyph buffer allocated — reuse
     * and grow it as needed rather than free/realloc on every miss.
     */
    if (shapePtr->cache[slot].text) {
        Tcl_Free(shapePtr->cache[slot].text);
        shapePtr->cache[slot].text    = NULL;
        shapePtr->cache[slot].textLen = -1;
    }
    shapePtr->cache[slot].glyphCount = 0;
    shapePtr->cache[slot].totalWidth = 0;

    /* Bidi analysis — includes fast path for all-LTR Latin text. */
    numBidiRuns = GetBidiRuns(source, numBytes, bidiRuns, 32);

    /* Shaping loop over bidi runs. */
    for (r = 0; r < numBidiRuns; r++) {
        const char *runText  = source + bidiRuns[r].offset;
        int         runLen   = bidiRuns[r].length;
        int         runIsRTL = bidiRuns[r].isRTL;
        kbts_run    run;
        kbts_glyph *glyph;
        int         runPenX = 0;
        int         runPenY = 0;

        if (runLen <= 0) continue;

        kbts_ShapeBegin(shapePtr->context,
                        runIsRTL ? KBTS_DIRECTION_RTL : KBTS_DIRECTION_LTR,
                        KBTS_LANGUAGE_DONT_KNOW);

        kbts_ShapeUtf8(shapePtr->context, runText, runLen,
                       KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);

        kbts_ShapeEnd(shapePtr->context);

        while (kbts_ShapeRun(shapePtr->context, &run)) {
            kbts_glyph_iterator it = run.Glyphs;

            /*
             * Resolve XftFont for this run once, outside the glyph loop.
             * All glyphs in a run come from the same kbts_font.
             */
            XftFont *xftFont = NULL;
            for (int j = 0; j < shapePtr->numFonts; j++) {
                if (shapePtr->fontMap[j].kbFont == run.Font) {
                    int fIdx = shapePtr->fontMap[j].faceIndex;
                    if (fIdx >= 0 && fIdx < fontPtr->nfaces) {
                        xftFont = fontPtr->faces[fIdx].ft0Font;
                    }
                    break;
                }
            }
            if (!xftFont) xftFont = fontPtr->faces[0].ft0Font;

            while (kbts_GlyphIteratorNext(&it, &glyph)) {
                /* Grow glyph buffer on demand, starting at 64, doubling each time. */
                if (shapePtr->cache[slot].glyphCount >=
                        shapePtr->cache[slot].glyphCapacity) {
                    int newCap = shapePtr->cache[slot].glyphCapacity == 0
                                 ? 64
                                 : shapePtr->cache[slot].glyphCapacity * 2;
                    shapePtr->cache[slot].glyphs = Tcl_Realloc(
                        shapePtr->cache[slot].glyphs,
                        newCap * sizeof(X11ShapedGlyph));
                    shapePtr->cache[slot].glyphCapacity = newCap;
                }

                int g = shapePtr->cache[slot].glyphCount;
                shapePtr->cache[slot].glyphs[g].kbFont   = run.Font;
                shapePtr->cache[slot].glyphs[g].xftFont  = xftFont;
                shapePtr->cache[slot].glyphs[g].glyphId  = glyph->Id;
                shapePtr->cache[slot].glyphs[g].x        = globalPenX + runPenX + glyph->OffsetX;
                shapePtr->cache[slot].glyphs[g].y        = globalPenY + runPenY + glyph->OffsetY;
                shapePtr->cache[slot].glyphs[g].advanceX = glyph->AdvanceX;

                runPenX += glyph->AdvanceX;
                runPenY += glyph->AdvanceY;
                shapePtr->cache[slot].glyphCount++;
            }
        }
        globalPenX += runPenX;
        globalPenY += runPenY;
    }

    /* Compute total width. */
    for (i = 0; i < shapePtr->cache[slot].glyphCount; i++) {
        shapePtr->cache[slot].totalWidth += shapePtr->cache[slot].glyphs[i].advanceX;
    }

    /* Store cache key and update LRU sequence number. */
    shapePtr->cache[slot].text = Tcl_Alloc(numBytes);
    memcpy(shapePtr->cache[slot].text, source, numBytes);
    shapePtr->cache[slot].textLen = numBytes;
    shapePtr->cache[slot].lruSeq  = ++shapePtr->lruCounter;
}

/*
 *----------------------------------------------------------------------
 *
 * X11Shape_LastShaped --
 *
 *	Returns a pointer to the most recently shaped cache entry by
 *	finding the slot with the highest lruSeq. Callers use this
 *	immediately after UnixFontShapeString to access shaped results
 *	without needing to know which cache slot was used.
 *
 * Results:
 *	Pointer to the hot X11ShapeCacheEntry, or NULL if cache is empty.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static X11ShapeCacheEntry *
X11Shape_LastShaped(X11Shape *shapePtr)
{
    int i;
    int seqMax  = -1;
    int seqSlot = -1;

    for (i = 0; i < X11SHAPE_CACHE_SIZE; i++) {
        if (shapePtr->cache[i].textLen >= 0 &&
                shapePtr->cache[i].lruSeq > seqMax) {
            seqMax  = shapePtr->cache[i].lruSeq;
            seqSlot = i;
        }
    }
    return (seqSlot >= 0) ? &shapePtr->cache[seqSlot] : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * GetFontForFace --
 *
 *	Retrieves the XftFont * for a specific face index at a specific
 *	rotation angle. For angle 0 returns ft0Font directly. For non-zero
 *	angles, checks whether the face's ftFont is already loaded at the
 *	requested angle; if not, constructs an FcMatrix rotation and opens
 *	the font via XftFontOpenPattern, closing any previously loaded
 *	angled variant. Falls back to face 0 if the requested index is out
 *	of range or the open fails.
 *
 * Results:
 *	An XftFont * for the face at the given angle, or NULL on failure.
 *
 * Side effects:
 *	May open and cache a new angled XftFont in fontPtr->faces[faceIndex].
 *
 *----------------------------------------------------------------------
 */

static XftFont *
GetFontForFace(UnixFtFont *fontPtr, int faceIndex, double angle)
{
    if (faceIndex < 0 || faceIndex >= fontPtr->nfaces) {
        faceIndex = 0;
    }
    if (angle == 0.0) {
        if (!fontPtr->faces[faceIndex].ft0Font) {
            GetFont(fontPtr, 0, 0.0);
        }
        return fontPtr->faces[faceIndex].ft0Font
               ? fontPtr->faces[faceIndex].ft0Font
               : fontPtr->faces[0].ft0Font;
    }
    /* For rotated text: load angled variant if not already at this angle. */
    if (!fontPtr->faces[faceIndex].ftFont ||
            fontPtr->faces[faceIndex].angle != angle) {
        FcPattern *pat = FcFontRenderPrepare(0, fontPtr->pattern,
                                              fontPtr->faces[faceIndex].source);
        double s = sin(angle * M_PI / 180.0);
        double c = cos(angle * M_PI / 180.0);
        FcMatrix mat;
        mat.xx = mat.yy = c;
        mat.xy = -(mat.yx = s);
        FcPatternAddMatrix(pat, FC_MATRIX, &mat);
        LOCK;
        XftFont *ftFont = XftFontOpenPattern(fontPtr->display, pat);
        UNLOCK;
        if (ftFont) {
            if (fontPtr->faces[faceIndex].ftFont) {
                LOCK;
                XftFontClose(fontPtr->display, fontPtr->faces[faceIndex].ftFont);
                UNLOCK;
            }
            fontPtr->faces[faceIndex].ftFont = ftFont;
            fontPtr->faces[faceIndex].angle  = angle;
        }
    }
    return fontPtr->faces[faceIndex].ftFont
           ? fontPtr->faces[faceIndex].ftFont
           : fontPtr->faces[0].ft0Font;
}

/*
 * Unified drawing function for shaped + bidirectional text.
 * Handles both horizontal (angle == 0.0) and rotated cases.
 * Shaping never happens here — results are read from the LRU cache.
 * XftFont pointers were resolved at shape time so no lookup is needed
 * for the horizontal path. Rotated path uses GetFontForFace.
 */
static void
UnixFontDrawShapedText(
    Display *display, Drawable drawable, GC gc, UnixFtFont *fontPtr,
    const char *source, int numBytes, double originX, double originY,
    double angle_deg)
{
    XftColor *xftcolor;
    XGCValues values;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
        Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    /*
     * Shape the string — cheap if already cached for this string+font.
     */
    UnixFontShapeString(fontPtr, source, numBytes, &fontPtr->shape);

    X11ShapeCacheEntry *entry = X11Shape_LastShaped(&fontPtr->shape);
    if (!entry || entry->glyphCount <= 0) return;

    /* Setup XftDraw. */
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
    if (!xftcolor) return;

    if (tsdPtr->clipRegion != NULL) {
        XftDrawSetClip(fontPtr->ftDraw, tsdPtr->clipRegion);
    }

    double rad  = (angle_deg != 0.0) ? angle_deg * M_PI / 180.0 : 0.0;
    double cosA = (angle_deg != 0.0) ? cos(rad) : 1.0;
    double sinA = (angle_deg != 0.0) ? sin(rad) : 0.0;

    XftGlyphFontSpec specs[NUM_SPEC];
    int nspec    = 0;
    XftFont *lastFont = NULL;

    for (int i = 0; i < entry->glyphCount; i++) {
        /*
         * Horizontal path: xftFont was stored at shape time — no lookup.
         * Rotated path: use GetFontForFace to get the angled variant.
         * kbFont is only read here for the rotated face-index lookup.
         */
        XftFont *drawFont = entry->glyphs[i].xftFont;
        if (angle_deg != 0.0) {
            for (int j = 0; j < fontPtr->shape.numFonts; j++) {
                if (fontPtr->shape.fontMap[j].kbFont == entry->glyphs[i].kbFont) {
                    drawFont = GetFontForFace(fontPtr,
                                             fontPtr->shape.fontMap[j].faceIndex,
                                             angle_deg);
                    break;
                }
            }
        }
        if (!drawFont) drawFont = GetFont(fontPtr, 0, angle_deg);
        if (!drawFont) continue;

        if (lastFont && drawFont != lastFont && nspec > 0) {
            LOCK;
            XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
            UNLOCK;
            nspec = 0;
        }
        lastFont = drawFont;

        specs[nspec].font  = drawFont;
        specs[nspec].glyph = entry->glyphs[i].glyphId;

        if (angle_deg == 0.0) {
            specs[nspec].x = (int)(originX + entry->glyphs[i].x + 0.5);
            specs[nspec].y = (int)(originY + entry->glyphs[i].y + 0.5);
        } else {
            double gx = (double)entry->glyphs[i].x;
            double gy = (double)entry->glyphs[i].y;
            specs[nspec].x = (int)(originX + gx * cosA - gy * sinA + 0.5);
            specs[nspec].y = (int)(originY + gx * sinA + gy * cosA + 0.5);
        }

        if (++nspec >= NUM_SPEC) {
            LOCK;
            XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
            UNLOCK;
            nspec    = 0;
            lastFont = NULL;
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
 *----------------------------------------------------------------------
 *
 * X11Shape_Init --
 *
 *	Initializes an X11Shape struct. Creates the kbts_shape_context
 *	and zeroes all 16 LRU cache slots.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates a kbts_shape_context.
 *
 *----------------------------------------------------------------------
 */

void X11Shape_Init(X11Shape *s)
{
    int i;
    memset(s, 0, sizeof(*s));
    s->context = kbts_CreateShapeContext(NULL, NULL);
    for (i = 0; i < X11SHAPE_CACHE_SIZE; i++) {
        s->cache[i].textLen       = -1;
        s->cache[i].text          = NULL;
        s->cache[i].glyphs        = NULL;
        s->cache[i].glyphCapacity = 0;
        s->cache[i].glyphCount    = 0;
        s->cache[i].lruSeq        = 0;
    }
    s->lruCounter = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * X11Shape_AddFont --
 *
 *	Pushes a pre-parsed kbts_font onto the shaper context's font stack.
 *	Used when a font is already available and does not need to be loaded
 *	from file or memory.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the font stack of s->context.
 *
 *----------------------------------------------------------------------
 */

void X11Shape_AddFont(X11Shape *s, kbts_font *f)
{
    if (s->context) {
	kbts_ShapePushFont(s->context, f);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * X11Shape_Destroy --
 *
 *	Tears down an X11Shape. Destroys the kbts_shape_context and frees
 *	all cache slot text keys and glyph buffers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all heap memory owned by s.
 *
 *----------------------------------------------------------------------
 */

void X11Shape_Destroy(X11Shape *s)
{
    int i;
    if (s->context) {
	kbts_DestroyShapeContext(s->context);
	s->context = NULL;
    }
    for (i = 0; i < X11SHAPE_CACHE_SIZE; i++) {
        if (s->cache[i].text) {
            Tcl_Free(s->cache[i].text);
            s->cache[i].text    = NULL;
            s->cache[i].textLen = -1;
        }
        if (s->cache[i].glyphs) {
            Tcl_Free(s->cache[i].glyphs);
            s->cache[i].glyphs        = NULL;
            s->cache[i].glyphCapacity = 0;
            s->cache[i].glyphCount    = 0;
        }
    }
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
