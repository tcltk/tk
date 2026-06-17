/*
 * tkUnixBidiFont.c --
 *
 * Alternate implementation of tkUnixFont.c using Xft with proper
 * text shaping.
 *
 * Copyright © 2002-2003 Keith Packard
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkUnixInt.h"
#include "tkFont.h"
#include <X11/Xft/Xft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <hb.h>
#include <hb-ft.h>
#include <SheenBidi/SheenBidi.h>

#define MAX_CACHED_COLORS 16
#define MAX_GLYPHS 512
#define MAX_FONTS 200
#define MAX_BIDI_RUNS 32
#define MAX_STRING_CACHE 1024

#define CACHE_SLOTS 8

/*
 * ---------------------------------------------------------------
 * BidiRun --
 *
 *   Structure to hold bidirectional run information.
 * ---------------------------------------------------------------
 */

typedef struct {
    int offset;		 /* Byte offset in original UTF-8 string. */
    int len;		    /* Length in bytes. */
    bool isRTL;		  /* true if this run is RTL, false if LTR. */
} BidiRun;

/*
 * ---------------------------------------------------------------
 * UnixFtFace --
 *
 *   Structure representing a single font face from fontconfig,
 *   with cached Xft fonts for rotated and unrotated versions.
 * ---------------------------------------------------------------
 */

typedef struct {
    XftFont *ftFont;	   /* Rotated font. */
    XftFont *ft0Font;	  /* Unrotated font. */
    FcPattern *source;	 /* Fontconfig pattern. */
    FcCharSet *charset;	/* Supported characters. */
    double angle;	      /* Current rotation angle. */

    /* HarfBuzz font mapping. */
    hb_font_t *hbFont;	 /* Corresponding HarfBuzz font. */
    hb_blob_t *hbBlob;	 /* Font blob (kept alive for hbFont lifetime) */
    hb_face_t *hbFace;	 /* HarfBuzz face (kept alive for hbFont lifetime) */
    bool isLoaded;	      /* Whether hbFont was successfully loaded. */

    /* Font metrics for scaling. */
    double unitsPerEm;	 /* For scaling glyph positions. */
    double ascender;	   /* Font ascender in design units. */
    double descender;	  /* Font descender in design units. */
} UnixFtFace;

/*
 * ---------------------------------------------------------------
 * UnixFtColorList --
 *
 *   Cached XftColor entry with LRU next pointer.
 * ---------------------------------------------------------------
 */

typedef struct {
    XftColor color;
    int next;
} UnixFtColorList;

#define MAX_CLUSTER_BREAKS 512

/*
 * ShapedGlyphBuffer --
 *
 *   Result buffer from HarfBuzz shaping. Contains glyph positions,
 *   visual index for cursor placement, and cluster break boundaries
 *   for efficient line fitting.
 */
typedef struct {
    /* Shaped glyphs array. */
    struct {
	int fontIndex;      /* Which UnixFtFace produced this glyph. */
	unsigned int glyphId;
	int x, y;	   /* Position relative to run origin (pixels). */
	int advanceX;       /* Width of this glyph (pixels). */
	int byteOffset;     /* Byte offset in source string. */
	int clusterLen;     /* Length of cluster in bytes. */
	bool isRTL;	  /* Is this glyph part of RTL run? */
    } glyphs[MAX_GLYPHS];
    int glyphCount;

    /*
     * Visual index for cursor positioning.
     * Sorted by X coordinate (left-to-right screen order).
     */
	struct {
	    int x;	      /* Visual X position (pixels). */
	    int advanceX;       /* Width of this visual cluster. */

	    int byteStart;      /* Logical start byte of cluster. */
	    int byteEnd;	/* Logical end byte of cluster. */

	    bool isRTL;	  /* True if cluster belongs to RTL run. */
	} visualIndex[MAX_GLYPHS];
    int indexCount;

    /* Total width of the shaped run. */
    int totalAdvance;

    /*
     * Cluster break boundaries for line fitting.
     *
     * clusterBreaks[] contains sorted byte offsets where the run
     * can be broken without splitting a cluster (grapheme, ligature,
     * combining mark sequence, etc.).
     *
     * Example:
     *   source = "Hello مرحبا"
     *   clusterBreaks[] = [0, 1, 2, 3, 4, 5, 6, 7, 9, 12, 14, 15]
     *
     * Tk_MeasureCharsInContext uses clusterBreaks[] to fit text
     * incrementally without reshaping.
     */
    int clusterBreaks[MAX_CLUSTER_BREAKS];
    int clusterBreakCount;
} ShapedGlyphBuffer;

/*
 * ---------------------------------------------------------------
 * X11Shaper --
 *
 *   Persistent per-font shaping state. Owns the HarfBuzz context,
 *   font mapping, and string cache.
 * ---------------------------------------------------------------
 */

typedef struct {
    hb_buffer_t *buffer;
    /* Font mapping - bidirectional. */
    struct {
	hb_font_t *hbFont;
	int faceIndex;
    } fontMap[MAX_FONTS];
    int numFonts;

    /* Multi‑entry string cache (round‑robin). */
    struct {
	char text[MAX_STRING_CACHE];
	int len;
	ShapedGlyphBuffer buffer;
	int valid;
    } cache[CACHE_SLOTS];
    int cacheNext;	  /* Next slot to overwrite. */

    /* Fast character‑to‑face cache (direct‑mapped). */
    struct {
	FcChar32 uc;
	int faceIdx;
    } charCache[64];

    FcChar32 lastChar;
    int lastFace;

    /* Error tracking. */
    int shapeErrors;
    int lastError;
} X11Shaper;

/*
 * ---------------------------------------------------------------
 * UnixFtFont --
 *
 *   Main font structure - must have TkFont as first member.
 *   Contains all faces, shaper state, and cached resources.
 * ---------------------------------------------------------------
 */

typedef struct {
    TkFont font;		/* Must be first. */
    UnixFtFace *faces;
    int nfaces;
    FcFontSet *fontset;
    FcPattern *pattern;
    Display *display;
    int screen;
    Colormap colormap;
    Visual *visual;
    int ncolors;
    int firstColor;
    UnixFtColorList colors[MAX_CACHED_COLORS];
    X11Shaper shaper;
    /* Precomputed scale factors. */
    double pixelScale;	  /* Global pixel scaling factor. */
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

/* Function prototypes. */
static void X11Shaper_Init(X11Shaper *s, UnixFtFont *fontPtr);
static void X11Shaper_Destroy(X11Shaper *s);
static bool X11Shaper_ShapeString(X11Shaper *shaper, UnixFtFont *fontPtr,
				 const char *source, int numBytes,
				 ShapedGlyphBuffer *buffer);
static int GetBidiRuns(FcChar32 *ucs4, int charCount, BidiRun *runs, int maxRuns);
static XftFont * GetFont(UnixFtFont *fontPtr, FcChar32 ucs4, double angle);
static XftFont * GetFaceFont(UnixFtFont *fontPtr, int faceIndex, double angle);
static XftColor * LookUpColor(Display *display, UnixFtFont *fontPtr,
			     unsigned long pixel);
static bool IsSimpleOnly(const char *str, int len);
static int GetRunFaceIndex(UnixFtFont *fontPtr, FcChar32 *ucs4Chars,
			   int runStart, int runLen);
static hb_font_t *GetHbFont(UnixFtFont *fontPtr, int faceIndex);

/*
 * ---------------------------------------------------------------
 * IsSimpleOnly --
 *
 *    Returns 1 if the string consists of codepoints that can be
 *    handled without shaping. This includes primarily Latin
 *    and kana text from the CJK (Chinese, Japanese, Korean) ranges.
 *
 *    Note: All other scripts requiring complex shaping (Arabic, Indic,
 *    Thai, CJK, etc.) return 0 to ensure HarfBuzz handling.
 * ---------------------------------------------------------------
 */

static bool
IsSimpleOnly(const char *str, int len)
{
    int i = 0;
    while (i < len) {
	unsigned char c = (unsigned char)str[i];

	if (c < 0x80) {
	    /* ASCII always simple. */
	    i++;
	    continue;
	}

	FcChar32 uc;
	int clen = FcUtf8ToUcs4((const FcChar8 *)(str + i), &uc, len - i);
	if (clen <= 0) {
	    return false;
	}

	/*
	 * FORCE through HarfBuzz + Bidi for scripts that need it:
	 */
	if (
	    /* Right-to-Left scripts. */
	    (uc >= 0x0590 && uc <= 0x05FF) ||	   /* Hebrew */
	    (uc >= 0x0600 && uc <= 0x06FF) ||	   /* Arabic */
	    (uc >= 0x0750 && uc <= 0x077F) ||	   /* Arabic Supplement */
	    (uc >= 0xFB50 && uc <= 0xFDFF) ||	   /* Arabic Presentation Forms */
	    (uc >= 0xFE70 && uc <= 0xFEFF) ||	   /* Arabic Presentation Forms-B */

	    /* Other RTL scripts */
	    (uc >= 0x0700 && uc <= 0x074F) ||	   /* Syriac */
	    (uc >= 0x0780 && uc <= 0x07BF) ||	   /* Thaana */
	    (uc >= 0x07C0 && uc <= 0x07FF) ||	   /* N'Ko */
	    (uc >= 0x0800 && uc <= 0x083F) ||	   /* Samaritan */
	    (uc >= 0x0840 && uc <= 0x085F) ||	   /* Mandaic */

	    /* CJK and others. */
	    (uc >= 0x4E00 && uc <= 0x9FFF) ||	   /* CJK Unified Ideographs */
	    (uc >= 0xAC00 && uc <= 0xD7AF) ||	   /* Hangul (Korean) */
	    (uc >= 0x1100 && uc <= 0x11FF) ||	   /* Jamo */
	    (uc >= 0x0900 && uc <= 0x0DFF) ||	   /* Indic */
	    (uc >= 0x0E00 && uc <= 0x0E7F) ||	   /* Thai */
	    (uc >= 0x0E80 && uc <= 0x0EFF) ||	   /* Lao */

	    /* Emoji and supplementary. */
	    (uc >= 0x1F000 && uc <= 0x1FAFF) ||
	    (uc >= 0x2600 && uc <= 0x27BF) ||
	    (uc >= 0x1F300 && uc <= 0x1F9FF) ||
	    uc > 0xFFFF) {

	    return false;   /* Use complex shaper. */
	}

	/*
	 * Safe scripts that can use the fast path:
	 * Latin, CJK punctuation, Hiragana, Katakana.
	 */
	int isSafe =
	    (uc <= 0x024F) ||				 /* Latin + extended */
	    (uc >= 0x3000 && uc <= 0x30FF);		   /* CJK punct + Kana */

	if (!isSafe) {
	    return false;
	}

	i += clen;
    }
    return true;
}

/*
 * ---------------------------------------------------------------
 * GetVisualXForByteOffset --
 *
 *    Proper visual-position lookup using the visualIndex.
 *
 *    Result:
 *      Returns index.
 *
 *    Side effects:
 *      None.
 * ---------------------------------------------------------------
 */

static int
GetVisualXForByteOffset(const ShapedGlyphBuffer *buffer, int byteOffset)
{
    if (byteOffset <= 0 || buffer->indexCount == 0) {
	return 0;
    }

    /* Primary lookup. */
    for (int i = 0; i < buffer->indexCount; i++) {
	if (byteOffset >= buffer->visualIndex[i].byteStart &&
	    byteOffset <  buffer->visualIndex[i].byteEnd) {
	    return buffer->visualIndex[i].x;
	}
    }

    /* Fallback: nearest cluster (handles clicks between characters). */
    int bestX = buffer->visualIndex[0].x;
    int minDist = abs(byteOffset - buffer->visualIndex[0].byteStart);

    for (int i = 1; i < buffer->indexCount; i++) {
	int dist = abs(byteOffset - buffer->visualIndex[i].byteStart);
	if (dist < minDist) {
	    minDist = dist;
	    bestX = buffer->visualIndex[i].x;
	} else if (dist > minDist * 2) {
	    break;   /* Early exit - positions are sorted. */
	}
    }
    return bestX;
}

/*
 * ---------------------------------------------------------------
 * GetFaceFont --
 *
 *   Retrieve or create an XftFont for a specific face index and rotation
 *   angle.
 *
 * Results:
 *   Pointer to XftFont, or NULL if not available.
 *
 * Side effects:
 *   May create new XftFont if not already cached.
 * ---------------------------------------------------------------
 */

static XftFont *
GetFaceFont(
    UnixFtFont *fontPtr,
    int faceIndex,
    double angle)
{
    if (faceIndex < 0 || faceIndex >= fontPtr->nfaces) {
	faceIndex = 0;
    }

    UnixFtFace *face = &fontPtr->faces[faceIndex];

    if (angle == 0.0) {
	if (!face->ft0Font) {
	    FcPattern *pat = FcFontRenderPrepare(NULL, fontPtr->pattern,
						 face->source);
	    LOCK;
	    face->ft0Font = XftFontOpenPattern(fontPtr->display, pat);
	    UNLOCK;

	    if (!face->ft0Font) {
		/* Safer fallback using size from original pattern. */
		double size = 12.0;
		FcPatternGetDouble(fontPtr->pattern, FC_SIZE, 0, &size);

		LOCK;
		face->ft0Font = XftFontOpen(fontPtr->display, fontPtr->screen,
					   FC_FAMILY, FcTypeString, "sans-serif",
					   FC_SIZE, FcTypeDouble, size, NULL);
		UNLOCK;

		if (!face->ft0Font) {
		    LOCK;
		    face->ft0Font = XftFontOpen(fontPtr->display, fontPtr->screen,
					       FC_FAMILY, FcTypeString, "fixed",
					       FC_SIZE, FcTypeDouble, size, NULL);
		    UNLOCK;
		}
	    }

	    /* Extract metrics. */
	    if (face->ft0Font && face->unitsPerEm == 0) {
		FT_Face ftFace = XftLockFace(face->ft0Font);
		if (ftFace) {
		    face->unitsPerEm = ftFace->units_per_EM;
		    face->ascender   = ftFace->ascender;
		    face->descender  = ftFace->descender;
		    XftUnlockFace(face->ft0Font);
		}
	    }
	}
	return face->ft0Font;
    }
    else {
	/* Rotated font handling. */
	if (!face->ftFont || face->angle != angle) {
	    FcPattern *pat = FcFontRenderPrepare(NULL, fontPtr->pattern,
						 face->source);

	    double rad = angle * M_PI / 180.0;
	    double s = sin(rad), c = cos(rad);
	    FcMatrix mat;
	    mat.xx = c;
	    mat.xy = -s;
	    mat.yx = s;
	    mat.yy = c;
	    FcPatternAddMatrix(pat, FC_MATRIX, &mat);

	    LOCK;
	    XftFont *ftFont = XftFontOpenPattern(fontPtr->display, pat);
	    UNLOCK;

	    if (!ftFont) {
		/* Fallback with correct size. */
		double size = 12.0;
		FcPatternGetDouble(fontPtr->pattern, FC_SIZE, 0, &size);

		LOCK;
		ftFont = XftFontOpen(fontPtr->display, fontPtr->screen,
				     FC_FAMILY, FcTypeString, "sans-serif",
				     FC_SIZE, FcTypeDouble, size,
				     FC_MATRIX, FcTypeMatrix, &mat,
				     NULL);
		UNLOCK;
	    }

	    if (face->ftFont) {
		LOCK;
		XftFontClose(fontPtr->display, face->ftFont);
		UNLOCK;
	    }
	    face->ftFont = ftFont;
	    face->angle = angle;
	}
	return face->ftFont;
    }
}

/*
 * ---------------------------------------------------------------
 * GetFont --
 *
 *   Retrieve an XftFont for a given character and rotation angle.
 *   Selects the face that contains the character, falling back to first face.
 *
 * Results:
 *   Pointer to XftFont, or NULL if not available.
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

    /* Find face containing this character. */
    if (ucs4) {
	for (i = 0; i < fontPtr->nfaces; i++) {
	    FcCharSet *charset = fontPtr->faces[i].charset;
	    if (charset && FcCharSetHasChar(charset, ucs4)) {
		break;
	    }
	}
	if (i == fontPtr->nfaces) {
	    i = 0;  /* Fallback to first face. */
	}
    } else {
	i = 0;
    }

    return GetFaceFont(fontPtr, i, angle);
}

/*
 * ---------------------------------------------------------------
 * LookUpColor --
 *
 *   Convert a pixel value to an XftColor. Uses a small LRU cache.
 *
 * Results:
 *   Pointer to XftColor, or NULL if cache is full.
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
		fontPtr->firstColor	 = i;
	    }
	    return &fontPtr->colors[i].color;
	}
    }

    if (fontPtr->ncolors < MAX_CACHED_COLORS) {
	last2 = -1;
	last  = fontPtr->ncolors++;
    } else {
	return NULL;
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
    fontPtr->firstColor	= last;

    return &fontPtr->colors[last].color;
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

    if (XftPatternGetDouble(ftFont->pattern, XFT_SIZE, 0, &ptSize) == XftResultMatch) {
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

    if (XftPatternGetInteger(ftFont->pattern, XFT_WEIGHT, 0, &weight) != XftResultMatch) {
	weight = XFT_WEIGHT_MEDIUM;
    }
    if (XftPatternGetInteger(ftFont->pattern, XFT_SLANT, 0, &slant) != XftResultMatch) {
	slant = XFT_SLANT_ROMAN;
    }

    faPtr->family = Tk_GetUid(family);
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
    fmPtr->fixed = (spacing != XFT_PROPORTIONAL);
}

/*
 * ---------------------------------------------------------------
 * GetBidiRuns --
 *
 *   Use SheenBidi to properly analyze text per UAX#9 and split it into
 *   level runs with correct directionality in visual order.
 *
 * Results:
 *   Returns number of runs created (at least 1). Fills runs array
 *   in visual display order (left-to-right on screen).
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

static int
GetBidiRuns(
    FcChar32 *ucs4,
    int charCount,
    BidiRun *runs,
    int maxRuns)
{
    if (charCount <= 0) {
	runs[0].offset = 0;
	runs[0].len    = 0;
	runs[0].isRTL  = false;
	return 1;
    }

    /* Fast path: check if any strong RTL characters exist. */
    int needsBidi = 0;
    for (int i = 0; i < charCount; i++) {
	if (ucs4[i] >= 0x0590) {  /* Hebrew/Arabic/Thaana/etc. start. */
	    needsBidi = 1;
	    break;
	}
    }

    if (!needsBidi) {
	runs[0].offset = 0;
	runs[0].len    = charCount;
	runs[0].isRTL  = false;
	return 1;
    }

    /* Full bidi analysis. */
    SBCodepointSequence seq = {SBStringEncodingUTF32, ucs4, (SBUInteger)charCount};
    SBAlgorithmRef algo = SBAlgorithmCreate(&seq);

    /* Use explicit LTR base level (common default for Tk apps). */
    SBParagraphRef para = SBAlgorithmCreateParagraph(algo, 0, (SBUInteger)charCount,
						     SBLevelDefaultLTR);

    SBLineRef line = SBParagraphCreateLine(para, 0, (SBUInteger)charCount);

    SBUInteger runCount = SBLineGetRunCount(line);
    const SBRun *bidiRuns = SBLineGetRunsPtr(line);

    int outRuns = 0;
    for (int i = 0; i < (int)runCount && outRuns < maxRuns; i++) {
	runs[outRuns].offset = (int)bidiRuns[i].offset;
	runs[outRuns].len    = (int)bidiRuns[i].length;
	runs[outRuns].isRTL  = (bidiRuns[i].level & 1) != 0;
	outRuns++;
    }

    SBLineRelease(line);
    SBParagraphRelease(para);
    SBAlgorithmRelease(algo);

    /* Always return at least one run. */
    return outRuns > 0 ? outRuns : 1;
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
	if (!fontPtr) {
	    return NULL;
	}
	memset(fontPtr, 0, sizeof(UnixFtFont));
    }

    FcConfigSubstitute(0, pattern, FcMatchPattern);
    XftDefaultSubstitute(Tk_Display(tkwin), Tk_ScreenNumber(tkwin), pattern);

    /*
     * Generate the list of fonts.
     */
    set = FcFontSort(0, pattern, FcTrue, NULL, &result);
    if (!set || set->nfont == 0) {
	if (!fontPtr->font.fid) {
	    Tcl_Free(fontPtr);
	}
	FcPatternDestroy(pattern);
	return NULL;
    }

    fontPtr->fontset = set;
    fontPtr->pattern = pattern;
    fontPtr->faces = (UnixFtFace *)Tcl_Alloc(set->nfont * sizeof(UnixFtFace));
    if (!fontPtr->faces) {
	FcFontSetDestroy(set);
	Tcl_Free(fontPtr);
	FcPatternDestroy(pattern);
	return NULL;
    }
    fontPtr->nfaces = set->nfont;

    /*
     * Fill in information about each returned font.
     */
    for (i = 0; i < set->nfont; i++) {
	fontPtr->faces[i].ftFont     = NULL;
	fontPtr->faces[i].ft0Font    = NULL;
	fontPtr->faces[i].source     = set->fonts[i];
	fontPtr->faces[i].angle      = 0.0;
	fontPtr->faces[i].hbFont     = NULL;
	fontPtr->faces[i].hbBlob     = NULL;
	fontPtr->faces[i].hbFace     = NULL;
	fontPtr->faces[i].isLoaded   = false;
	fontPtr->faces[i].unitsPerEm = 0.0;
	fontPtr->faces[i].ascender   = 0.0;
	fontPtr->faces[i].descender  = 0.0;

	if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &charset) == FcResultMatch) {
	    fontPtr->faces[i].charset = FcCharSetCopy(charset);
	} else {
	    fontPtr->faces[i].charset = NULL;
	}
    }

    /*
     * Initialize the shaper before calling GetFont() or Tk_MeasureChars()
     * because both can trigger shaping operations.
     */
    X11Shaper_Init(&fontPtr->shaper, fontPtr);

    /*
     * Set a safe default pixel scale (will be overridden
     * per-face when metrics are loaded).
     */
    fontPtr->pixelScale = 1.0;

    fontPtr->display   = Tk_Display(tkwin);
    fontPtr->screen    = Tk_ScreenNumber(tkwin);
    fontPtr->colormap  = Tk_Colormap(tkwin);
    fontPtr->visual    = Tk_Visual(tkwin);
    fontPtr->ncolors   = 0;
    fontPtr->firstColor = -1;

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
	if (!fontPtr->font.fid) {
	    Tcl_Free(fontPtr);
	}
	return NULL;
    }

    fontPtr->font.fid = XLoadFont(Tk_Display(tkwin), "fixed");

    GetTkFontAttributes(tkwin, ftFont, &fontPtr->font.fa);
    GetTkFontMetrics(ftFont, &fontPtr->font.fm);

    Tk_DeleteErrorHandler(handler);
    if (errorFlag) {
	FinishedWithFont(fontPtr);
	if (!fontPtr->font.fid) {
	    Tcl_Free(fontPtr);
	}
	return NULL;
    }

    /*
     * Compute underline position and thickness.
     */
    {
	TkFont *fPtr = &fontPtr->font;

	fPtr->underlinePos = fPtr->fm.descent / 2;

	errorFlag = 0;
	handler = Tk_CreateErrorHandler(Tk_Display(tkwin),
			-1, -1, -1, InitFontErrorProc, (void *)&errorFlag);

	Tk_MeasureChars((Tk_Font)fPtr, "I", 1, -1, 0, &iWidth);

	Tk_DeleteErrorHandler(handler);
	if (errorFlag) {
	    FinishedWithFont(fontPtr);
	    if (!fontPtr->font.fid) {
		Tcl_Free(fontPtr);
	    }
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
	if (fontPtr->faces[i].hbFont) {
	    hb_font_destroy(fontPtr->faces[i].hbFont);
	    fontPtr->faces[i].hbFont = NULL;
	}
	if (fontPtr->faces[i].hbFace) {
	    hb_face_destroy(fontPtr->faces[i].hbFace);
	    fontPtr->faces[i].hbFace = NULL;
	}
	if (fontPtr->faces[i].hbBlob) {
	    hb_blob_destroy(fontPtr->faces[i].hbBlob);
	    fontPtr->faces[i].hbBlob = NULL;
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
 * X11Shaper_Init --
 *
 *   Initialize persistent shaping context.
 *   Fonts are loaded lazily on first use (see X11Shaper_ShapeString).
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Allocates shaping buffer.
 * ---------------------------------------------------------------
 */

static void
X11Shaper_Init(
    X11Shaper *s,
    UnixFtFont *fontPtr)
{
    memset(s, 0, sizeof(*s));

    /* Create a HarfBuzz buffer for shaping. */
    s->buffer = hb_buffer_create();
    if (!s->buffer) {
	return;
    }

    s->numFonts = 0;
    s->cacheNext = 0;
    /* Clear string cache slots. */
    for (int i = 0; i < CACHE_SLOTS; i++) {
	s->cache[i].valid = 0;
    }
    /* Clear character cache. */
    for (int i = 0; i < 64; i++) {
	s->charCache[i].uc = 0;
	s->charCache[i].faceIdx = 0;
    }

    s->shapeErrors = 0;
    s->lastChar = 0;
    s->lastFace = -1;

    /*
     * Fonts are loaded lazily on first shape call.
     * This avoids issues with XftFace locking during font initialization.
     */
}

/*
 * ---------------------------------------------------------------
 * X11Shaper_Destroy --
 *
 *   Release shaping context.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Frees shaping context.
 * ---------------------------------------------------------------
 */

static void
X11Shaper_Destroy(
    X11Shaper *s)
{
    if (s->buffer) {
	hb_buffer_destroy(s->buffer);
	s->buffer = NULL;
    }
}

/* ---------------------------------------------------------------
 * GetHbFont --
 *
 *  Lazily create and return a HarfBuzz font for a given face index.
 *  The font is cached in the UnixFtFace structure.
 *
 *  Results:
 *    hb_font_t pointer, or NULL on failure.
 *
 *  Side effects:
 *    May open font file and create hb_font_t, hb_face_t, hb_blob_t.
 * ---------------------------------------------------------------
 */

static hb_font_t *
GetHbFont(
	  UnixFtFont *fontPtr,
	  int faceIndex)
{
    if (faceIndex < 0 || faceIndex >= fontPtr->nfaces) {
	return NULL;
    }
    UnixFtFace *face = &fontPtr->faces[faceIndex];

    if (face->isLoaded) {
	return face->hbFont;
    }

    /* Load font file. */
    FcChar8 *fontFile = NULL;
    int fontIdx = 0;
    if (FcPatternGetString(face->source, FC_FILE, 0, &fontFile) != FcResultMatch) {
	return NULL;
    }
    FcPatternGetInteger(face->source, FC_INDEX, 0, &fontIdx);

    hb_blob_t *blob = hb_blob_create_from_file_or_fail((const char *)fontFile);
    if (!blob) {
	return NULL;
    }

    hb_face_t *hbFace = hb_face_create(blob, fontIdx);
    if (!hbFace) {
	hb_blob_destroy(blob);
	return NULL;
    }

    hb_font_t *hbFont = hb_font_create(hbFace);
    if (!hbFont) {
	hb_face_destroy(hbFace);
	hb_blob_destroy(blob);
	return NULL;
    }

    /* Set font scale based on Xft size. */
    XftFont *xft = GetFaceFont(fontPtr, faceIndex, 0.0);
    if (xft) {
	int pixelSize = xft->ascent + xft->descent;
	hb_font_set_scale(hbFont, pixelSize * 64, pixelSize * 64);
	hb_font_set_ppem(hbFont, (unsigned)pixelSize, (unsigned)pixelSize);
    }

    face->hbBlob = blob;
    face->hbFace = hbFace;
    face->hbFont = hbFont;
    face->isLoaded = true;

    return hbFont;
}

/* ---------------------------------------------------------------
 * GetRunFaceIndex --
 *
 *  Choose the best font face for a given run based on the first character.
 *  Uses a small direct-mapped cache for speed.
 *
 *  Results:
 *    Font face; falls back to face 0 if no match is found.
 *
 *  Side effects:
 *    Updates the character cache.
 * ---------------------------------------------------------------
 */

static int
GetRunFaceIndex(
		UnixFtFont *fontPtr,
		FcChar32 *ucs4Chars,
		int runStart,
		int runLen)
{
    if (runLen <= 0 || runStart < 0) return 0;
    FcChar32 uc = ucs4Chars[runStart];
    X11Shaper *shaper = &fontPtr->shaper;

    /* Direct-mapped hash index. */
    int cacheIdx = uc & 63;

    /* Check cache first. */
    if (shaper->charCache[cacheIdx].uc == uc) {
	return shaper->charCache[cacheIdx].faceIdx;
    }

    /* Check existing loaded faces. */
    for (int fi = 0; fi < fontPtr->nfaces; fi++) {
	if (fontPtr->faces[fi].charset && FcCharSetHasChar(fontPtr->faces[fi].charset, uc)) {
	    shaper->charCache[cacheIdx].uc = uc;
	    shaper->charCache[cacheIdx].faceIdx = fi;
	    return fi;
	}
    }

    /* Fallback to face 0. */
    shaper->charCache[cacheIdx].uc = uc;
    shaper->charCache[cacheIdx].faceIdx = 0;
    return 0;
}
/*
 * ---------------------------------------------------------------
 * X11Shaper_ShapeString --
 *
 *   Shape a UTF-8 string using HarfBuzz and produce glyph buffer
 *   WITH cluster boundary metadata for proper line wrapping (including RTL).
 *
 * Results:
 *   1 on success, 0 on failure.
 *
 * Side effects:
 *   Updates the shaper cache; buffer is filled with glyphs and clusters.
 * ---------------------------------------------------------------
 */

static bool
X11Shaper_ShapeString(
		      X11Shaper *shaper,
		      UnixFtFont *fontPtr,
		      const char *source,
		      int numBytes,
		      ShapedGlyphBuffer *buffer)
{
    if (!shaper->buffer || !source || numBytes <= 0 || !buffer) {
	return false;
    }

    buffer->glyphCount = 0;
    buffer->indexCount = 0;
    buffer->totalAdvance = 0;
    buffer->clusterBreakCount = 0;

    /*
     * Fast path for simple scripts (Latin, CJK, etc.).
     *
     * Use cached faces from fontPtr->faces instead of
     * opening new fallback fonts every call.
     */

    if (IsSimpleOnly(source, numBytes)) {
	int penX = 0;
	int i = 0;
	while (i < numBytes && buffer->glyphCount < MAX_GLYPHS) {
	    FcChar32 uc;
	    int clen = FcUtf8ToUcs4((const FcChar8 *)(source + i), &uc, numBytes - i);
	    if (clen <= 0) { i++; continue; }

	    unsigned int glyphId = 0;
	    int fontIndex = -1;

	    int cacheIdx = uc & 63;
	    if (shaper->charCache[cacheIdx].uc == uc) {
		fontIndex = shaper->charCache[cacheIdx].faceIdx;
		XftFont *ft = GetFaceFont(fontPtr, fontIndex, 0.0);
		if (ft) glyphId = XftCharIndex(fontPtr->display, ft, uc);
	    }

	    if (glyphId == 0) {
		for (int f = 0; f < fontPtr->nfaces; f++) {
		    if (fontPtr->faces[f].charset && !FcCharSetHasChar(fontPtr->faces[f].charset, uc))
			continue;
		    XftFont *ft = GetFaceFont(fontPtr, f, 0.0);
		    if (!ft) continue;
		    glyphId = XftCharIndex(fontPtr->display, ft, uc);
		    if (glyphId != 0) {
			fontIndex = f;
			shaper->charCache[cacheIdx].uc = uc;
			shaper->charCache[cacheIdx].faceIdx = f;
			break;
		    }
		}
	    }

	    if (glyphId == 0) {
		fontIndex = 0;
		XftFont *ft = GetFaceFont(fontPtr, 0, 0.0);
		if (ft) glyphId = XftCharIndex(fontPtr->display, ft, uc);
	    }

	    XGlyphInfo metrics;
	    XftGlyphExtents(fontPtr->display, GetFaceFont(fontPtr, fontIndex, 0.0), &glyphId, 1, &metrics);

	    int g = buffer->glyphCount;
	    buffer->glyphs[g].fontIndex = fontIndex;
	    buffer->glyphs[g].glyphId = glyphId;
	    buffer->glyphs[g].x = penX;
	    buffer->glyphs[g].y = 0;
	    buffer->glyphs[g].advanceX = metrics.xOff;
	    buffer->glyphs[g].byteOffset = i;
	    buffer->glyphs[g].clusterLen = clen;
	    buffer->glyphs[g].isRTL = false;

	    penX += metrics.xOff;
	    buffer->glyphCount++;
	    i += clen;
	}
	buffer->totalAdvance = penX;

	buffer->indexCount = 0;
	int prevByteOffset = -1;

	/*
	 * Build visualIndex properly for CJK (Chinese/Japanese) + Latin.
	 * This is critical for correct cursor movement and selection.
	 */

	for (int i = 0; i < buffer->glyphCount && buffer->indexCount < MAX_GLYPHS; i++) {
	    int bo = buffer->glyphs[i].byteOffset;

	    if (bo == prevByteOffset) {
		if (buffer->indexCount > 0) {
		    buffer->visualIndex[buffer->indexCount-1].advanceX +=
			buffer->glyphs[i].advanceX;
		}
		continue;
	    }

	    int v = buffer->indexCount++;
	    buffer->visualIndex[v].x	 = buffer->glyphs[i].x;
	    buffer->visualIndex[v].advanceX  = buffer->glyphs[i].advanceX;
	    buffer->visualIndex[v].byteStart = bo;
	    buffer->visualIndex[v].byteEnd   = bo + buffer->glyphs[i].clusterLen;
	    buffer->visualIndex[v].isRTL     = false;

	    prevByteOffset = bo;
	}

	return true;
    }

    /*
     * Check cache for complex shaped/RTL text (multi-entry round-robin).
     */
    for (int slot = 0; slot < CACHE_SLOTS; slot++) {
	if (shaper->cache[slot].valid &&
	    shaper->cache[slot].len == numBytes &&
	    numBytes <= MAX_STRING_CACHE &&
	    memcmp(source, shaper->cache[slot].text, numBytes) == 0) {
	    *buffer = shaper->cache[slot].buffer;
	    return true;
	}
    }

    int stackCharBounds[256];
    FcChar32 stackUcs4Chars[256];
    int *charBounds = stackCharBounds;
    FcChar32 *ucs4Chars = stackUcs4Chars;
    bool needFree = false;

    if (numBytes >= 256) {
	charBounds = (int *)malloc((numBytes + 1) * sizeof(int));
	ucs4Chars = (FcChar32 *)malloc(numBytes * sizeof(FcChar32));
	if (!charBounds || !ucs4Chars) {
	    free(charBounds); free(ucs4Chars);
	    return false;
	}
	needFree = true;
    }

    charBounds[0] = 0;
    int bytePos = 0, charCount = 0;
    while (bytePos < numBytes && charCount < MAX_GLYPHS) {
	FcChar32 uc;
	int clen = FcUtf8ToUcs4((const FcChar8 *)(source + bytePos), &uc, numBytes - bytePos);
	if (clen <= 0) { bytePos++; continue; }

	if ((uc < 0x0020 && uc != 0x0009 && uc != 0x000A && uc != 0x000D) ||
	    (uc >= 0x0080 && uc <= 0x009F) || uc == 0xFFFD) {
	    bytePos += clen;
	    continue;
	}

	ucs4Chars[charCount] = uc;
	charCount++;
	bytePos += clen;
	charBounds[charCount] = bytePos;
    }

    if (charCount == 0) {
	if (needFree) { free(charBounds); free(ucs4Chars); }
	buffer->clusterBreaks[0] = 0;
	buffer->clusterBreakCount = 1;
	return true;
    }

	/* Bidi analysis. */
    BidiRun bidiRuns[MAX_BIDI_RUNS];
    int numRuns = GetBidiRuns(ucs4Chars, charCount, bidiRuns, MAX_BIDI_RUNS);

    int globalPenX = 0;

    /* Process each bidi run. */
    for (int r = 0; r < numRuns; r++) {
	int runStart = bidiRuns[r].offset;
	int runLen   = bidiRuns[r].len;
	bool runIsRTL = bidiRuns[r].isRTL;

	if (runLen <= 0) continue;

	int runByteStart = charBounds[runStart];
	int runByteEnd   = charBounds[runStart + runLen];
	int runByteLen   = runByteEnd - runByteStart;
	if (runByteLen <= 0) continue;

	bool hasVisibleChars = false;
	for (int ci = runStart; ci < runStart + runLen; ci++) {
	    if (ucs4Chars[ci] >= 0x0020 || ucs4Chars[ci] == 0x0009 ||
		ucs4Chars[ci] == 0x000A || ucs4Chars[ci] == 0x000D) {
		hasVisibleChars = true;
		break;
	    }
	}
	if (!hasVisibleChars) continue;

	int subrunStart = runStart;

	while (subrunStart < runStart + runLen) {

	    /*
	     * Detect the concrete script for this subrun.
	     *
	     * Walk forward skipping INHERITED/COMMON to find the first
	     * character with a real script assignment.  If the entire
	     * remaining run is INHERITED/COMMON (e.g. a run of emoji,
	     * which are all HB_SCRIPT_COMMON) leave subrunScript as
	     * HB_SCRIPT_INVALID; it is resolved below.
	     */
	    hb_script_t subrunScript = HB_SCRIPT_INVALID;
	    for (int ci = subrunStart; ci < runStart + runLen; ci++) {
		hb_script_t s = hb_unicode_script(
		    hb_unicode_funcs_get_default(), ucs4Chars[ci]);
		if (s != HB_SCRIPT_INHERITED && s != HB_SCRIPT_COMMON) {
		    subrunScript = s;
		    break;
		}
	    }

	    /*
	     * Resolve the script and select the anchor face.
	     *
	     * For a run that starts with real-script characters the anchor
	     * is the first such character (skipping leading COMMON/INHERITED
	     * punctuation).  For a run that is entirely COMMON (emoji, math
	     * symbols, general punctuation without a preceding context) the
	     * anchor is subrunStart itself – GetRunFaceIndex will pick the
	     * face that has charset coverage for the first codepoint, which
	     * is exactly what we want (e.g. Noto Color Emoji for U+1F600).
	     *
	     * Use HB_SCRIPT_COMMON rather than HB_SCRIPT_LATIN for the
	     * all-COMMON case so that HarfBuzz activates the correct feature
	     * set (in particular the 'CBDT'/'CBLC' and 'COLR' lookups used
	     * by colour-emoji fonts).
	     */
	    int anchorChar = subrunStart;
	    if (subrunScript != HB_SCRIPT_INVALID) {
		for (int ci = subrunStart; ci < runStart + runLen; ci++) {
		    hb_script_t s = hb_unicode_script(
			hb_unicode_funcs_get_default(), ucs4Chars[ci]);
		    if (s != HB_SCRIPT_INHERITED && s != HB_SCRIPT_COMMON) {
			anchorChar = ci;
			break;
		    }
		}
	    } else {
		/* All-COMMON run (emoji, symbols, punctuation). */
		subrunScript = HB_SCRIPT_COMMON;
	    }

	    int runFaceIndex = GetRunFaceIndex(fontPtr, ucs4Chars, anchorChar, 1);

	    /*
	     * Extend the subrun while both script and face remain
	     * consistent.
	     *
	     * Key rule for INHERITED/COMMON characters: absorb them only when
	     * they map to the *same* fallback face as the current subrun.
	     * If the face differs (e.g. a Unicode emoji following a Latin
	     * word, where the emoji face is different from the Latin face)
	     * break immediately so the emoji gets its own subrun with the
	     * correct face and HB_SCRIPT_COMMON.  Without this check the
	     * emoji would be shaped with the Latin face, producing .notdef
	     * or the wrong monochrome glyph.
	     */
	    int subrunEnd = subrunStart + 1;
	    while (subrunEnd < runStart + runLen) {
		hb_script_t s = hb_unicode_script(
		    hb_unicode_funcs_get_default(), ucs4Chars[subrunEnd]);

		if (s == HB_SCRIPT_INHERITED || s == HB_SCRIPT_COMMON) {
		    /*
		     * Face-change check for neutral characters.
		     * Combining marks (INHERITED) virtually always share the
		     * base character's face, so this rarely fires for them.
		     * Emoji (COMMON) frequently require a different face and
		     * must be split out.
		     */
		    int neutralFace = GetRunFaceIndex(fontPtr, ucs4Chars, subrunEnd, 1);
		    if (neutralFace != runFaceIndex) {
			break;
		    }
		    subrunEnd++;
		    continue;
		}

		/* Real script change → always break. */
		if (s != subrunScript) {
		    break;
		}

		/* Same script, different face → break. */
		int nextFace = GetRunFaceIndex(fontPtr, ucs4Chars, subrunEnd, 1);
		if (nextFace != runFaceIndex) {
		    break;
		}

		subrunEnd++;
	    }

	    int shapeRunStart = subrunStart;
	    int shapeRunLen   = subrunEnd - subrunStart;

	    int shapeByteStart = charBounds[shapeRunStart];
	    int shapeByteEnd   = charBounds[shapeRunStart + shapeRunLen];
	    int shapeByteLen   = shapeByteEnd - shapeByteStart;

	    if (shapeByteLen <= 0) {
		subrunStart = subrunEnd;
		continue;
	    }

	    hb_font_t *runHbFont = GetHbFont(fontPtr, runFaceIndex);
	    if (!runHbFont) {
		subrunStart = subrunEnd;
		continue;
	    }

	    hb_buffer_clear_contents(shaper->buffer);
	    hb_buffer_add_utf8(shaper->buffer, source, numBytes,
			       shapeByteStart, shapeByteLen);
	    hb_buffer_set_direction(shaper->buffer,
				    runIsRTL ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
	    hb_buffer_set_script(shaper->buffer, subrunScript);
	    hb_buffer_set_language(shaper->buffer,
				   hb_language_from_string("", -1));
	    hb_buffer_set_cluster_level(shaper->buffer,
					HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);

	    hb_shape(runHbFont, shaper->buffer, NULL, 0);

	    unsigned int glyphCount = hb_buffer_get_length(shaper->buffer);
	    hb_glyph_info_t *glyphInfo = hb_buffer_get_glyph_infos(shaper->buffer, NULL);
	    hb_glyph_position_t *glyphPos = hb_buffer_get_glyph_positions(shaper->buffer, NULL);
	    if (!glyphInfo || !glyphPos) {
		subrunStart = subrunEnd;
		continue;
	    }

	    struct {
		int fontIndex; unsigned int glyphId; int x, y; int advanceX;
		int byteOffset; int clusterLen;
	    } tempGlyphs[MAX_GLYPHS];
	    int tempCount = 0;

	    int runPenX = 0;
	    XftFont *xftRunFont = GetFaceFont(fontPtr, runFaceIndex, 0.0);

	    for (unsigned int i = 0;
		 i < glyphCount && tempCount < MAX_GLYPHS;
		 i++) {

		tempGlyphs[tempCount].fontIndex  = runFaceIndex;
		tempGlyphs[tempCount].glyphId    = glyphInfo[i].codepoint;

		tempGlyphs[tempCount].x =
		    runPenX +
		    (int)(glyphPos[i].x_offset / 64.0 + 0.5);

		tempGlyphs[tempCount].y =
		    -(int)(glyphPos[i].y_offset / 64.0 + 0.5);

		tempGlyphs[tempCount].byteOffset =
		    glyphInfo[i].cluster;

		unsigned int gid = glyphInfo[i].codepoint;

		if (xftRunFont && gid != 0) {
		    XGlyphInfo gmetrics;
		    XftGlyphExtents(
			fontPtr->display,
			xftRunFont,
			&gid,
			1,
			&gmetrics
		    );
		    tempGlyphs[tempCount].advanceX = gmetrics.xOff;
		} else {
		    tempGlyphs[tempCount].advanceX =
			(int)(glyphPos[i].x_advance / 64.0 + 0.5);
		}

		runPenX += tempGlyphs[tempCount].advanceX;
		tempCount++;
	    }

	    for (int i = 0; i < tempCount; i++) {
		tempGlyphs[i].x += globalPenX;
	    }

	    /* Fix cluster lengths. */
	    if (!runIsRTL) {
		for (int i = 0; i < tempCount; i++) {
		    int start = tempGlyphs[i].byteOffset;
		    int end = shapeByteEnd;
		    for (int j = i + 1; j < tempCount; j++) {
			if (tempGlyphs[j].byteOffset != start) {
			    end = tempGlyphs[j].byteOffset;
			    break;
			}
		    }
		    tempGlyphs[i].clusterLen = end - start;
		    if (tempGlyphs[i].clusterLen <= 0) {
			tempGlyphs[i].clusterLen = 1;
		    }
		}
	    } else {
		for (int i = 0; i < tempCount; i++) {
		    int start = tempGlyphs[i].byteOffset;
		    int end = shapeByteEnd;
		    for (int j = i - 1; j >= 0; j--) {
			if (tempGlyphs[j].byteOffset != start) {
			    end = tempGlyphs[j].byteOffset;
			    break;
			}
		    }
		    tempGlyphs[i].clusterLen = abs(end - start);
		    if (tempGlyphs[i].clusterLen <= 0) {
			tempGlyphs[i].clusterLen = 1;
		    }
		}
	    }

	    /* Copy to main buffer. */
	    for (int i = 0; i < tempCount; i++) {
		int idx = buffer->glyphCount;
		if (idx >= MAX_GLYPHS) break;
		buffer->glyphs[idx] = (typeof(buffer->glyphs[0])) {
		    .fontIndex  = tempGlyphs[i].fontIndex,
		    .glyphId    = tempGlyphs[i].glyphId,
		    .x	  = tempGlyphs[i].x,
		    .y	  = tempGlyphs[i].y,
		    .advanceX   = tempGlyphs[i].advanceX,
		    .byteOffset = tempGlyphs[i].byteOffset,
		    .clusterLen = tempGlyphs[i].clusterLen,
		    .isRTL      = runIsRTL
		};
		buffer->glyphCount++;
	    }

	    globalPenX += runPenX;
	    subrunStart = subrunEnd;
	}
    }

    buffer->totalAdvance = globalPenX;

    /* Build visualIndex. */
    buffer->indexCount = 0;

    for (int i = 0; i < buffer->glyphCount; i++) {
	int byteStart = buffer->glyphs[i].byteOffset;
	int byteEnd = byteStart + buffer->glyphs[i].clusterLen;

	if (buffer->indexCount > 0) {
	    int last = buffer->indexCount - 1;
	    if (buffer->visualIndex[last].byteStart == byteStart) {
		continue;
	    }
	}

	int vIdx = buffer->indexCount++;
	buffer->visualIndex[vIdx].x = buffer->glyphs[i].x;
	buffer->visualIndex[vIdx].advanceX = buffer->glyphs[i].advanceX;
	buffer->visualIndex[vIdx].isRTL = buffer->glyphs[i].isRTL;

	if (buffer->glyphs[i].isRTL) {
	    buffer->visualIndex[vIdx].byteStart = byteEnd;
	    buffer->visualIndex[vIdx].byteEnd = byteStart;
	} else {
	    buffer->visualIndex[vIdx].byteStart = byteStart;
	    buffer->visualIndex[vIdx].byteEnd = byteEnd;
	}
    }

    /* Cluster breaks. */
    buffer->clusterBreaks[0] = 0;
    buffer->clusterBreakCount = 1;

    char seen[1024] = {0};
    seen[0] = 1;

    for (int i = 0; i < buffer->glyphCount && buffer->clusterBreakCount < MAX_CLUSTER_BREAKS-1; i++) {
	int pos = buffer->glyphs[i].byteOffset;
	int end = pos + buffer->glyphs[i].clusterLen;

	if (pos > 0 && pos < numBytes && !seen[pos]) {
	    buffer->clusterBreaks[buffer->clusterBreakCount++] = pos;
	    seen[pos] = 1;
	}
	if (end > 0 && end <= numBytes && !seen[end]) {
	    buffer->clusterBreaks[buffer->clusterBreakCount++] = end;
	    seen[end] = 1;
	}
    }

    if (buffer->clusterBreaks[buffer->clusterBreakCount-1] != numBytes) {
	if (buffer->clusterBreakCount < MAX_CLUSTER_BREAKS) {
	    buffer->clusterBreaks[buffer->clusterBreakCount++] = numBytes;
	}
    }

    int n = buffer->clusterBreakCount;
    for (int i = 1; i < n; i++) {
	int key = buffer->clusterBreaks[i];
	int j = i - 1;
	while (j >= 0 && buffer->clusterBreaks[j] > key) {
	    buffer->clusterBreaks[j+1] = buffer->clusterBreaks[j];
	    j--;
	}
	buffer->clusterBreaks[j+1] = key;
    }

    int write = 1;
    for (int i = 1; i < n; i++) {
	if (buffer->clusterBreaks[i] != buffer->clusterBreaks[write-1]) {
	    buffer->clusterBreaks[write++] = buffer->clusterBreaks[i];
	}
    }
    buffer->clusterBreakCount = write;

    /* Cache result. */
    if (numBytes <= MAX_STRING_CACHE) {
	int slot = shaper->cacheNext;
	memcpy(shaper->cache[slot].text, source, numBytes);
	shaper->cache[slot].len = numBytes;
	shaper->cache[slot].buffer = *buffer;
	shaper->cache[slot].valid = 1;
	shaper->cacheNext = (slot + 1) % CACHE_SLOTS;
    }

    if (needFree) {
	free(charBounds);
	free(ucs4Chars);
    }

    return true;
}

/*
 * ---------------------------------------------------------------
 * TkpFontPkgInit --
 *
 *   This procedure is called when an application is created. It
 *   initializes all the structures that are used by the
 *   platform-dependant code on a per application basis.
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
    FcInit();
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

    FcPattern *pattern = XftXlfdParse(name, FcFalse, FcFalse);
    if (!pattern) {
	return NULL;
    }

    UnixFtFont *fontPtr = InitFont(tkwin, pattern, NULL);
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
			 TkFont *tkFontPtr,
			 Tk_Window tkwin,
			 const TkFontAttributes *faPtr)
{
    XftPattern *pattern = XftPatternCreate();
    const char *family = faPtr->family;

    if (!family || family[0] == '\0') {
	family = "sans-serif";
    }

    /* Force Fontconfig to treat this as a preferred family. */
    XftPatternAddString(pattern, XFT_FAMILY, family);

    /* Explicitly tell Xft we want a sans-serif style if the family is generic. */
    if (strcmp(family, "sans-serif") == 0) {
	XftPatternAddInteger(pattern, XFT_SPACING, XFT_PROPORTIONAL);
	XftPatternAddString(pattern, FC_STYLE, "Regular");
    }

    double size = (faPtr->size > 0.0) ? faPtr->size :
		 ((faPtr->size < 0.0) ? TkFontGetPoints(tkwin, faPtr->size) : 12.0);
    XftPatternAddDouble(pattern, XFT_SIZE, size);

    int weight = (faPtr->weight == TK_FW_BOLD) ? XFT_WEIGHT_BOLD : XFT_WEIGHT_MEDIUM;
    XftPatternAddInteger(pattern, XFT_WEIGHT, weight);

    int slant = (faPtr->slant == TK_FS_ROMAN) ? XFT_SLANT_ROMAN : XFT_SLANT_ITALIC;
    XftPatternAddInteger(pattern, XFT_SLANT, slant);

    /* Perform system substitution. */
    XftDefaultSubstitute(Tk_Display(tkwin), Tk_ScreenNumber(tkwin), pattern);
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);

    UnixFtFont *fontPtr = (UnixFtFont *)tkFontPtr;
    fontPtr = InitFont(tkwin, pattern, fontPtr);

    if (!fontPtr) {
	/* Emergency Fallback: If "sans-serif" failed, try "sans." */
	XftPatternDestroy(pattern);
	pattern = XftPatternBuild(NULL, XFT_FAMILY, XftTypeString, "sans",
				  XFT_SIZE, XftTypeDouble, size, NULL);
	fontPtr = InitFont(tkwin, pattern, (UnixFtFont *)tkFontPtr);
    }

    return (TkFont *)fontPtr;
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
    UnixFtFont *fontPtr = (UnixFtFont *)tkFontPtr;
    FinishedWithFont(fontPtr);
    /* Note: tkFontPtr itself is freed by generic code. */
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
    Tcl_Obj *resultPtr = Tcl_NewListObj(0, NULL);
    XftFontSet *list = XftListFonts(Tk_Display(tkwin), Tk_ScreenNumber(tkwin),
				    NULL, XFT_FAMILY, NULL);

    for (int i = 0; i < list->nfont; i++) {
	char *family, **familyPtr = &family;
	if (XftPatternGetString(list->fonts[i], XFT_FAMILY, 0, familyPtr) == XftResultMatch) {
	    Tcl_ListObjAppendElement(NULL, resultPtr,
				     Tcl_NewStringObj(family, TCL_INDEX_NONE));
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
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    Tcl_Obj *resultPtr = Tcl_NewListObj(0, NULL);

    for (int i = 0; i < fontPtr->nfaces; i++) {
	FcPattern *pat = FcFontRenderPrepare(NULL, fontPtr->pattern,
					     fontPtr->faces[i].source);
	const char *family  = "Unknown";
	const char *const *familyPtr  = &family;
	const char *foundry = "Unknown";
	const char *const *foundryPtr = &foundry;

	XftPatternGetString(pat, XFT_FAMILY,  0, familyPtr);
	XftPatternGetString(pat, XFT_FOUNDRY, 0, foundryPtr);

	Tcl_Obj *listObj = Tcl_NewListObj(0, NULL);
	Tcl_ListObjAppendElement(NULL, listObj,
				 Tcl_NewStringObj(family,  TCL_INDEX_NONE));
	Tcl_ListObjAppendElement(NULL, listObj,
				 Tcl_NewStringObj(foundry, TCL_INDEX_NONE));
	Tcl_ListObjAppendElement(NULL, resultPtr, listObj);

	FcPatternDestroy(pat);
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
    Tk_Window tkwin,		/* Window on the font's display. */
    Tk_Font tkfont,		/* Font to query. */
    int c,			/* Character of interest. */
    TkFontAttributes *faPtr)	/* Output: Font attributes. */
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    XftFont *ftFont = GetFont(fontPtr, (FcChar32)c, 0.0);

    if (ftFont) {
	GetTkFontAttributes(tkwin, ftFont, faPtr);
    } else {
	*faPtr = fontPtr->font.fa;
    }

    faPtr->underline   = fontPtr->font.fa.underline;
    faPtr->overstrike  = fontPtr->font.fa.overstrike;
}

/*
 * ---------------------------------------------------------------
 * Tk_MeasureChars --
 *
 *   Measure the width of a string when drawn in the given font.
 *
 *
 * Results:
 *   Calls Tk_MeasureCharsinContext.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
Tk_MeasureChars(
	Tk_Font tkfont,
	const char *source,
	Tcl_Size numBytes,
	int maxLength,
	int flags,
	int *lengthPtr)
{
       return Tk_MeasureCharsInContext(tkfont, source, numBytes, 0, numBytes,
		maxLength, flags, lengthPtr);
}

/*
 * ---------------------------------------------------------------
 * Tk_MeasureCharsInContext --
 *
 *   Measure a substring of a larger string, preserving shaping context.
 *
 *   Uses cluster boundaries to do width-aware fitting WITHOUT
 *   reshaping. This prevents RTL runs from being passed to LayoutLine
 *   as single monolithic units.
 *
 *   Algorithm:
 *   1. Shape the full source string once (HarfBuzz shaping with full context).
 *   2. Use cluster boundaries from the shaped buffer to find the longest
 *      fitting subrange that starts at rangeStart.
 *   3. Return bytes consumed and pixel width.
 *
 *   No reshaping happens during the fitting loop — we use the glyph
 *   positions and cluster boundaries already computed by shaping.
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
    Tcl_Size numBytes,
    Tcl_Size rangeStart,
    Tcl_Size rangeLength,
    int maxLength,
    int flags,
    int *lengthPtr)
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;

    int start = (int)rangeStart;
    int end   = (int)(rangeStart + rangeLength);

    if (rangeLength <= 0) {
	*lengthPtr = 0;
	return 0;
    }

    /*
     * Fast path: simple LTR/Latin/non-shaped text.
     */

    if (IsSimpleOnly(source, (int)numBytes)) {

	XftFont *ftFont = GetFaceFont(fontPtr, 0, 0.0);

	if (!ftFont) {
	    *lengthPtr = 0;
	    return 0;
	}

	const char *sub = source + start;
	int subLen = (int)rangeLength;

	XGlyphInfo extents;

	/*
	 * Unlimited measurement.
	 */

	if (maxLength < 0) {
	    XftTextExtentsUtf8(
		fontPtr->display,
		ftFont,
		(const FcChar8 *)sub,
		subLen,
		&extents);

	    *lengthPtr = extents.xOff;
	    return subLen;
	}

	/*
	 * Incrementally measure UTF-8 character boundaries.
	 */

	int bestBytes = 0;
	int bestWidth = 0;

	int pos = 0;

	while (pos < subLen) {

	    FcChar32 uc;
	    int clen = FcUtf8ToUcs4(
		(const FcChar8 *)(sub + pos),
		&uc,
		subLen - pos);

	    if (clen <= 0) {
		clen = 1;
	    }

	    int next = pos + clen;

	    XftTextExtentsUtf8(
		fontPtr->display,
		ftFont,
		(const FcChar8 *)sub,
		next,
		&extents);

	    if (extents.xOff > maxLength) {
		break;
	    }

	    bestBytes = next;
	    bestWidth = extents.xOff;

	    pos = next;
	}

	/*
	 * Whole-word wrapping.
	 */

	if ((flags & TK_WHOLE_WORDS)
	    && bestBytes > 0
	    && bestBytes < subLen) {

	    int rollback = -1;

	    for (int i = bestBytes - 1; i >= 0; i--) {
		unsigned char c = (unsigned char)sub[i];
		if (c == ' '
		    || c == '\t'
		    || c == '\n'
		    || c == '\r') {

		    rollback = i + 1;
		    break;
		}
	    }

	    if (rollback > 0 && rollback < bestBytes) {
		XftTextExtentsUtf8(
		    fontPtr->display,
		    ftFont,
		    (const FcChar8 *)sub,
		    rollback,
		    &extents);
		bestBytes = rollback;
		bestWidth = extents.xOff;
	    }
	}

	/*
	 * AT_LEAST_ONE support.
	 */

	if ((flags & TK_AT_LEAST_ONE)
	    && bestBytes == 0
	    && subLen > 0) {

	    FcChar32 uc;

	    int clen = FcUtf8ToUcs4(
		(const FcChar8 *)sub,
		&uc,
		subLen);

	    if (clen <= 0) {
		clen = 1;
	    }

	    XftTextExtentsUtf8(
		fontPtr->display,
		ftFont,
		(const FcChar8 *)sub,
		clen,
		&extents);
	    bestBytes = clen;
	    bestWidth = extents.xOff;
	}

	*lengthPtr = bestWidth;
	return bestBytes;
    }

    /*
     * Complex shaped RTL text.
     */

    ShapedGlyphBuffer buffer;

    if (!X11Shaper_ShapeString(
	    &fontPtr->shaper,
	    fontPtr,
	    source,
	    (int)numBytes,
	    &buffer)
	|| buffer.glyphCount <= 0) {

	*lengthPtr = 0;
	return 0;
    }

    /*
     * Unlimited measurement.
     */

    if (maxLength < 0) {

	int width = 0;

	for (int i = 0; i < buffer.glyphCount; i++) {
	    int bo  = buffer.glyphs[i].byteOffset;
	    int boe = bo + buffer.glyphs[i].clusterLen;
	    if (boe > start && bo < end) {
		width += buffer.glyphs[i].advanceX;
	    }
	}

	*lengthPtr = width;
	return (int)rangeLength;
    }

    /*
     * Build stable cluster table.
     */

    typedef struct {
	int start;
	int end;
	int advance;
    } ClusterInfo;

    ClusterInfo clusters[MAX_GLYPHS];

    int clusterCount = 0;

    for (int i = 0; i < buffer.glyphCount; i++) {

	int bo  = buffer.glyphs[i].byteOffset;
	int len = buffer.glyphs[i].clusterLen;

	if (len <= 0) {
	    continue;
	}

	int boe = bo + len;

	/*
	 * Ignore clusters outside requested range.
	 */

	if (boe <= start || bo >= end) {
	    continue;
	}

	/*
	 * Merge glyphs belonging to same cluster.
	 */

	int found = -1;
	for (int j = 0; j < clusterCount; j++) {
	    if (clusters[j].start == bo
		&& clusters[j].end == boe) {
		found = j;
		break;
	    }
	}

	if (found < 0) {
	    if (clusterCount >= MAX_GLYPHS) {
		break;
	    }

	    found = clusterCount++;

	    clusters[found].start   = bo;
	    clusters[found].end     = boe;
	    clusters[found].advance = 0;
	}

	clusters[found].advance += buffer.glyphs[i].advanceX;
    }

    /*
     * Sort clusters logically by byte offset.
     */

    for (int i = 0; i < clusterCount - 1; i++) {
	for (int j = i + 1; j < clusterCount; j++) {
	    if (clusters[j].start < clusters[i].start) {
		ClusterInfo tmp = clusters[i];
		clusters[i] = clusters[j];
		clusters[j] = tmp;
	    }
	}
    }

    /*
     * Measure fitting clusters.
     */

    int width = 0;
    int bestBytes = 0;

    for (int i = 0; i < clusterCount; i++) {

	int nextWidth = width + clusters[i].advance;
	if (nextWidth > maxLength) {
	    break;
	}
	width = nextWidth;
	bestBytes = clusters[i].end - start;
    }

    /*
     * Whole-word rollback.
     */

    if ((flags & TK_WHOLE_WORDS)
	&& bestBytes > 0
	&& bestBytes < (int)rangeLength) {

	int rollback = -1;

	for (int i = bestBytes - 1; i >= 0; i--) {
	    unsigned char c =
		(unsigned char)source[start + i];
	    if (c == ' '
		|| c == '\t'
		|| c == '\n'
		|| c == '\r') {

		rollback = i + 1;
		break;
	    }
	}

	if (rollback > 0 && rollback < bestBytes) {
	    width = 0;
	    int target = start + rollback;
	    for (int i = 0; i < clusterCount; i++) {
		if (clusters[i].end <= target) {
		    width += clusters[i].advance;
		}
	    }
	    bestBytes = rollback;
	}
    }

    /*
     * AT_LEAST_ONE safety.
     */

    if ((flags & TK_AT_LEAST_ONE)
	&& bestBytes == 0
	&& clusterCount > 0) {
	bestBytes = clusters[0].end - start;
	width = clusters[0].advance;
    }

    *lengthPtr = width;

    return bestBytes;
}

/*
 * ---------------------------------------------------------------
 * Tk_DrawChars --
 *
 *   Draw a UTF-8 string using the given font.
 *
 * Results:
 *   Calls Tk_DrawCharsinContext.
 *
 * Side effects:
 *   Draws text on the specified drawable.
 * ---------------------------------------------------------------
 */


void
Tk_DrawChars(
    Display *display,      /* Display on which to draw. */
    Drawable drawable,     /* Window or pixmap in which to draw. */
    GC gc,		 /* Graphics context for drawing characters. */
    Tk_Font tkfont,	/* Font in which characters will be drawn. */
    const char *source,    /* UTF-8 string to be displayed. */
    Tcl_Size numBytes,     /* Number of bytes in string. */
    int x, int y)	  /* Coordinates at which to place origin. */
{
    if (numBytes <= 0 || source == NULL) {
	return;
    }

    /*
     * Delegate everything to the context-aware renderer.
     * We draw the full string as a single logical range.
     */
    Tk_DrawCharsInContext(display, drawable, gc,tkfont,source,numBytes,0, numBytes,x, y);
}

/*
 * ---------------------------------------------------------------
 * Tk_DrawCharsInContext --
 *
 *   Draw a substring of text with full shaping context.
 *
 *   source/numBytes is the full base-chunk string (from baseChars).
 *   rangeStart/rangeLength selects which bytes to draw.
 *   x/y is the screen origin of the full base-chunk string (position 0
 *   of the shaped buffer maps to this point).
 *
 *   We shape the full string for correct joining/ligatures, then draw
 *   only the glyphs whose clusters fall within [rangeStart, rangeEnd).
 *   No clip rectangle is needed: we simply skip out-of-range glyphs.
 * ---------------------------------------------------------------
 */

void
Tk_DrawCharsInContext(
    Display *display,
    Drawable drawable,
    GC gc,
    Tk_Font tkfont,
    const char *source,
    Tcl_Size numBytes,
    Tcl_Size rangeStart,
    Tcl_Size rangeLength,
    int x, int y)
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    int rangeEnd = (int)(rangeStart + rangeLength);

    if (rangeLength <= 0) return;

    XftDraw *ftDraw = XftDrawCreate(display, drawable,
				    fontPtr->visual, fontPtr->colormap);
    if (!ftDraw) return;

    XGCValues values;
    XGetGCValues(display, gc, GCForeground, &values);
    XftColor *xftcolor = LookUpColor(display, fontPtr, values.foreground);
    if (!xftcolor) {
	XftDrawDestroy(ftDraw);
	return;
    }

    if (tsdPtr->clipRegion) {
	XftDrawSetClip(ftDraw, tsdPtr->clipRegion);
    }

    /*
     * Here we render simple text that does not need to go through the
     * Harfbuzz shaper and SheenBidi re-ordering.
     */
    if (IsSimpleOnly(source, (int)numBytes)) {

	/* Simple path with cached fallback fonts. */
	XftGlyphFontSpec specs[MAX_GLYPHS];
	int nspec = 0;
	int penX = x;

	/* Compute x offset for rangeStart (if any). */
	int offsetX = 0;
	if (rangeStart > 0) {
	    int tmpX = 0;
	    Tcl_Size i = 0;
	    while (i < rangeStart) {
		FcChar32 uc;
		int clen = FcUtf8ToUcs4((const FcChar8 *)(source + i), &uc,
					(int)(rangeStart - i));
		if (clen <= 0) { i++; continue; }

		int fontIndex = -1;
		for (int f = 0; f < fontPtr->nfaces; f++) {
		    if (fontPtr->faces[f].charset &&
			!FcCharSetHasChar(fontPtr->faces[f].charset, uc))
			continue;
		    XftFont *ft = GetFaceFont(fontPtr, f, 0.0);
		    if (!ft) continue;
		    if (XftCharIndex(display, ft, uc) != 0) {
			fontIndex = f;
			break;
		    }
		}
		if (fontIndex >= 0) {
		    XGlyphInfo ext;
		    XftFont *ftFont = GetFaceFont(fontPtr, fontIndex, 0.0);
		    FT_UInt x = XftCharIndex(display, ftFont, uc);
		    XftGlyphExtents(display, ftFont, &x, 1, &ext);
		    tmpX += ext.xOff;
		}
		i += clen;
	    }
	    offsetX = tmpX;
	}
	penX = x + offsetX;

	/* Build specs for visible range. */
	Tcl_Size i = rangeStart;
	Tcl_Size end = rangeStart + rangeLength;
	while (i < end && nspec < MAX_GLYPHS) {
	    FcChar32 uc;
	    int clen = FcUtf8ToUcs4((const FcChar8 *)(source + i), &uc,
				    (int)(end - i));
	    if (clen <= 0) { i++; continue; }

	    int fontIndex = -1;
	    for (int f = 0; f < fontPtr->nfaces; f++) {
		if (fontPtr->faces[f].charset &&
		    !FcCharSetHasChar(fontPtr->faces[f].charset, uc))
		    continue;
		XftFont *ft = GetFaceFont(fontPtr, f, 0.0);
		if (!ft) continue;
		if (XftCharIndex(display, ft, uc) != 0) {
		    fontIndex = f;
		    break;
		}
	    }
	    if (fontIndex < 0) {
		i += clen;
		continue;
	    }

	    XftFont *ftFont = GetFaceFont(fontPtr, fontIndex, 0.0);
	    unsigned int glyphId = XftCharIndex(display, ftFont, uc);
	    XGlyphInfo ext;
	    XftGlyphExtents(display, ftFont, &glyphId, 1, &ext);

	    specs[nspec].font  = ftFont;
	    specs[nspec].glyph = glyphId;
	    specs[nspec].x     = penX;
	    specs[nspec].y     = y;
	    nspec++;

	    penX += ext.xOff;
	    i += clen;
	}

	if (nspec > 0) {
	    LOCK;
	    XftDrawGlyphFontSpec(ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	}
	goto done;
    }

     /*
      * Complex script text that has gone through
      * Harfbuzz shaping and SheenBidi re-ordering.
      */
    {
	ShapedGlyphBuffer buffer;
	if (!X11Shaper_ShapeString(&fontPtr->shaper, fontPtr, source,
				   (int)numBytes, &buffer)) {
	    goto done;
	}

	int rangeEnd = (int)(rangeStart + rangeLength);

	XftGlyphFontSpec specs[MAX_GLYPHS];
	int nspec = 0;

	/*
	 * Correct approach for bidi:
	 * Glyphs already have correct absolute visual X positions
	 * from the start of the full string. Just filter and draw.
	 */
	for (int i = 0; i < buffer.glyphCount && nspec < MAX_GLYPHS; i++) {
	    int bo  = buffer.glyphs[i].byteOffset;
	    int boe = bo + buffer.glyphs[i].clusterLen;

	    /* Include glyph if its cluster overlaps the requested range. */
	    if (boe <= (int)rangeStart || bo >= rangeEnd)
		continue;

	    int faceIdx = buffer.glyphs[i].fontIndex;
	    if (faceIdx < 0 || faceIdx >= fontPtr->nfaces) faceIdx = 0;

	    XftFont *ftFont = GetFaceFont(fontPtr, faceIdx, 0.0);
	    if (!ftFont) continue;

	    unsigned int glyphId = buffer.glyphs[i].glyphId;
	    if (glyphId == 0) continue;

	    specs[nspec].font  = ftFont;
	    specs[nspec].glyph = glyphId;
	    specs[nspec].x     = x + buffer.glyphs[i].x;   /* Absolute visual position. */
	    specs[nspec].y     = y + buffer.glyphs[i].y;
	    nspec++;
	}

	if (nspec > 0) {
	    LOCK;
	    XftDrawGlyphFontSpec(ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	}
    }
 done:
    XftDrawDestroy(ftDraw);
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
    Display *display,
    Drawable drawable,
    GC gc,
    Tk_Font tkfont,
    const char *source,
    Tcl_Size numBytes,
    double x, double y,
    double angle)
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (numBytes == 0) return;

    XftDraw *ftDraw = XftDrawCreate(display, drawable,
				    fontPtr->visual, fontPtr->colormap);
    if (!ftDraw) return;

    XGCValues values;
    XGetGCValues(display, gc, GCForeground, &values);
    XftColor *xftcolor = LookUpColor(display, fontPtr, values.foreground);
    if (!xftcolor) {
	XftDrawDestroy(ftDraw);
	return;
    }

    if (tsdPtr->clipRegion) {
	XftDrawSetClip(ftDraw, tsdPtr->clipRegion);
    }

    /* Rotation parameters. */
    double radians = angle * (M_PI / 180.0);
    double cosA = cos(radians);
    double sinA = sin(radians);

    /* Simple text path (Latin text, etc. */
    if (IsSimpleOnly(source, (int)numBytes)) {
	XftGlyphFontSpec specs[MAX_GLYPHS];
	int nspec = 0;
	double penX = 0.0;

	int i = 0;
	while (i < numBytes && nspec < MAX_GLYPHS) {
	    FcChar32 uc;
	    int clen = FcUtf8ToUcs4((const FcChar8 *)(source + i), &uc, numBytes - i);
	    if (clen <= 0) { i++; continue; }

	    int fontIndex = -1;
	    for (int f = 0; f < fontPtr->nfaces; f++) {
		if (fontPtr->faces[f].charset &&
		    !FcCharSetHasChar(fontPtr->faces[f].charset, uc))
		    continue;
		XftFont *ft = GetFaceFont(fontPtr, f, angle);
		if (!ft) continue;
		if (XftCharIndex(display, ft, uc) != 0) {
		    fontIndex = f;
		    break;
		}
	    }
	    if (fontIndex < 0) {
		i += clen;
		continue;
	    }

	    XftFont *ftFont = GetFaceFont(fontPtr, fontIndex, angle);
	    unsigned int glyphId = XftCharIndex(display, ftFont, uc);
	    XGlyphInfo ext;
	    XftGlyphExtents(display, ftFont, &glyphId, 1, &ext);

	    /* Rotate position. */
	    double gx = penX;
	    double gy = 0.0;
	    double rx = gx * cosA - gy * sinA;
	    double ry = gx * sinA + gy * cosA;

	    specs[nspec].font  = ftFont;
	    specs[nspec].glyph = glyphId;
	    specs[nspec].x     = (int)(x + rx);
	    specs[nspec].y     = (int)(y - ry);
	    nspec++;

	    penX += ext.xOff;
	    i += clen;
	}

	if (nspec > 0) {
	    LOCK;
	    XftDrawGlyphFontSpec(ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	}
	goto done;
    }

	/* Complex script path (RTL text). */
    {
	ShapedGlyphBuffer buffer;
	if (!X11Shaper_ShapeString(&fontPtr->shaper, fontPtr, source,
				   (int)numBytes, &buffer)) {
	    goto done;
	}

	XftGlyphFontSpec specs[MAX_GLYPHS];
	int nspec = 0;

	for (int i = 0; i < buffer.glyphCount && nspec < MAX_GLYPHS; i++) {
	    int faceIdx = buffer.glyphs[i].fontIndex;
	    if (faceIdx < 0 || faceIdx >= fontPtr->nfaces) faceIdx = 0;

	    XftFont *ftFont = GetFaceFont(fontPtr, faceIdx, angle);
	    if (!ftFont) continue;

	    unsigned int glyph = buffer.glyphs[i].glyphId;
	    if (glyph == 0) continue;

	    double gx = buffer.glyphs[i].x;
	    double gy = buffer.glyphs[i].y;

	    double rx = gx * cosA - gy * sinA;
	    double ry = gx * sinA + gy * cosA;

	    specs[nspec].font  = ftFont;
	    specs[nspec].glyph = glyph;
	    specs[nspec].x     = (int)(x + rx);
	    specs[nspec].y     = (int)(y - ry);
	    nspec++;
	}

	if (nspec > 0) {
	    LOCK;
	    XftDrawGlyphFontSpec(ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	}
    }

 done:
    if (tsdPtr->clipRegion) {
	XftDrawSetClip(ftDraw, NULL);
    }
    XftDrawDestroy(ftDraw);
}

/*
 * ---------------------------------------------------------------
 * TkpDrawAngledCharsInContext --
 *
 *   Draw a substring of rotated text.
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
    TkDrawAngledChars(display, drawable, gc, tkfont,
		      source + rangeStart, rangeLength, x, y, angle);
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
 * Local Variables:
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
