/*
 * tkUnixBidiFont.c --
 *
 * Alternate implementation of tkUnixFont.c using Xft with proper
 * text shaping.
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

/*
 * ---------------------------------------------------------------
 * BidiRun --
 *
 *   Structure to hold bidirectional run information.
 * ---------------------------------------------------------------
 */

typedef struct {
    int offset;                 /* Byte offset in original UTF-8 string. */
    int len;                    /* Length in bytes. */
    int isRTL;                  /* 1 if this run is RTL, 0 if LTR. */
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
    XftFont *ftFont;           /* Rotated font. */
    XftFont *ft0Font;          /* Unrotated font. */
    FcPattern *source;         /* Fontconfig pattern. */
    FcCharSet *charset;        /* Supported characters. */
    double angle;              /* Current rotation angle. */

    /* HarfBuzz font mapping. */
    hb_font_t *hbFont;         /* Corresponding HarfBuzz font. */
    hb_blob_t *hbBlob;         /* Font blob (kept alive for hbFont lifetime) */
    hb_face_t *hbFace;         /* HarfBuzz face (kept alive for hbFont lifetime) */
    int isLoaded;              /* Whether hbFont was successfully loaded. */

    /* Font metrics for scaling. */
    double unitsPerEm;         /* For scaling glyph positions. */
    double ascender;           /* Font ascender in design units. */
    double descender;          /* Font descender in design units. */
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
        int x, y;           /* Position relative to run origin (pixels). */
        int advanceX;       /* Width of this glyph (pixels). */
        int byteOffset;     /* Byte offset in source string. */
        int clusterLen;     /* Length of cluster in bytes. */
        int isRTL;          /* Is this glyph part of RTL run? */
    } glyphs[MAX_GLYPHS];
    int glyphCount;

    /*
     * Visual index for cursor positioning.
     * Sorted by X coordinate (left-to-right screen order).
     */
    struct {
        int x;              /* Visual position (pixels). */
        int advanceX;       /* Width of this glyph/cluster. */
        int byteEnd;        /* Byte offset of end of cluster. */
    } visualIndex[MAX_GLYPHS];
    int indexCount;

    /* Total width of the shaped run. */
    int totalAdvance;

    /*
     * NEW: Cluster break boundaries for line fitting.
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
    /* Simple string cache for common measurements. */
    struct {
        char text[MAX_STRING_CACHE];
        int len;
        ShapedGlyphBuffer buffer;
        int valid;
    } cache;
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
    TkFont font;                /* Must be first. */
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
    double pixelScale;          /* Global pixel scaling factor. */
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
static int X11Shaper_ShapeString(X11Shaper *shaper, UnixFtFont *fontPtr,
                                 const char *source, int numBytes,
                                 ShapedGlyphBuffer *buffer);
static int GetBidiRuns(FcChar32 *ucs4, int charCount, BidiRun *runs, int maxRuns);
static XftFont * GetFont(UnixFtFont *fontPtr, FcChar32 ucs4, double angle);
static XftFont * GetFaceFont(UnixFtFont *fontPtr, int faceIndex, double angle);
static XftColor * LookUpColor(Display *display, UnixFtFont *fontPtr,
                             unsigned long pixel);
static int IsSimpleOnly(const char *str, int len);  /* fast-path helper */
static int GetRunFaceIndex(UnixFtFont *fontPtr, FcChar32 *ucs4Chars,
			   int runStart, int runLen);

/*
 * ---------------------------------------------------------------
 * IsSimpleOnly (Expanded for CJK and Emoji) --
 *
 *    Returns 1 if the string consists of codepoints that can be
 *    handled without shaping. This now includes Latin,
 *    CJK (Chinese, Japanese, Korean), and common Emoji ranges.
 *
 *    Note: Scripts requiring complex shaping (Arabic, Indic,
 *    Thai, etc.) return 0 to ensure HarfBuzz handling.
 * ---------------------------------------------------------------
 */

static int
IsSimpleOnly(const char *str, int len)
{
    int i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)str[i];

        if (c < 0x80) {
            /* ASCII always simple */
            i++;
            continue;
        }

        FcChar32 uc;
        int clen = FcUtf8ToUcs4((const FcChar8 *)(str + i), &uc, len - i);
        if (clen <= 0) {
            return 0;
        }

        /*
         * EXCLUDE Korean Hangul + Indic explicitly.
         * These MUST NOT go through the simple path.
         */
        if ((uc >= 0xAC00 && uc <= 0xD7AF) ||   /* Hangul syllables */
            (uc >= 0x1100 && uc <= 0x11FF) ||   /* Hangul Jamo */
            (uc >= 0x0900 && uc <= 0x097F)) {   /* Devanagari */
            return 0;
        }

        /*
         * ALLOW:
         *   - Latin (<= U+024F)
         *   - CJK punctuation (U+3000–U+303F)
         *   - Hiragana/Katakana (U+3040–U+30FF)
         *   - CJK Unified Ideographs (U+4E00–U+9FFF)
         *   - Emoji ranges
         */
        int isSafe =
            (uc <= 0x024F) ||                                 /* Latin */
            (uc >= 0x3000 && uc <= 0x30FF) ||                 /* JP punctuation + kana */
            (uc >= 0x4E00 && uc <= 0x9FFF) ||                 /* CJK ideographs */
            (uc >= 0x2600 && uc <= 0x27BF) ||                 /* misc symbols */
            (uc >= 0x1F300 && uc <= 0x1F9FF);                 /* emoji */

        if (!isSafe) {
            return 0;
        }

        i += clen;
    }
    return 1;
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

            XSync(fontPtr->display, False);   /* Helps PostScript tests. */
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

            XSync(fontPtr->display, False);   /* Helps stability. */
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
                fontPtr->firstColor         = i;
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
    fontPtr->firstColor        = last;

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
        runs[0].isRTL  = 0;
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
        runs[0].isRTL  = 0;
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
        runs[outRuns].isRTL  = (bidiRuns[i].level & 1);
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
        fontPtr->faces[i].isLoaded   = 0;
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

    /* Set a safe default pixel scale (will be overridden
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
	    /* Don't destroy - fonts/faces/blobs stay alive for program lifetime */
	    fontPtr->faces[i].hbFont = NULL;
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
    s->cache.valid = 0;
    s->shapeErrors = 0;

    /* Fonts are loaded lazily on first shape call.
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
 * GetRunFaceIndex --
 *
 *  Choose the best font face for a given run based on the first character.
 *
 *  Results:
 *    Font face; falls back to face 0 if no match is found.
 *
 *  Side effects:
 *    None.
 * ---------------------------------------------------------------
 */

static int
GetRunFaceIndex(UnixFtFont *fontPtr, FcChar32 *ucs4Chars, int runStart, int runLen)
{
    if (runLen <= 0 || runStart < 0) {
        return 0;
    }

    FcChar32 uc = ucs4Chars[runStart];

    for (int fi = 0; fi < fontPtr->nfaces; fi++) {
        if (fontPtr->faces[fi].charset &&
            FcCharSetHasChar(fontPtr->faces[fi].charset, uc)) {
            return fi;
        }
    }
    return 0;   /* Fallback. */
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
 
static int
X11Shaper_ShapeString(
		      X11Shaper *shaper,
		      UnixFtFont *fontPtr,
		      const char *source,
		      int numBytes,
		      ShapedGlyphBuffer *buffer)
{
    /* Validate input parameters to ensure we have everything needed for shaping. */
    if (!shaper->buffer || !source || numBytes <= 0 || !buffer) {
        return 0;
    }
    
    /* Initialize the output buffer with default values. */
    buffer->glyphCount = 0;
    buffer->indexCount = 0;
    buffer->totalAdvance = 0;
    buffer->clusterBreakCount = 0;
    
    /* 
     * Fast path for simple scripts (latin, cjk, monochrome emoji).
     * This avoids the overhead of harfbuzz shaping for straightforward text.
     */
    if (IsSimpleOnly(source, numBytes)) {
	int penX = 0;
	int i = 0;

	/*
	 * Build a fallback fontset inline by sorting fontconfig patterns.
	 * This creates an ordered list of fonts that can supply missing glyphs.
	 */
	FcResult result;
	FcFontSet *set = FcFontSort(
				    NULL,               /* display parameter was fixed - previously used fontPtr->display incorrectly */
				    fontPtr->pattern,
				    FcTrue,             /* trim to remove duplicate fonts */
				    NULL,
				    &result
				    );

	if (!set || set->nfont <= 0) {
	    if (set) FcFontSetDestroy(set);
	    return 0;
	}

	int numFonts = set->nfont;
	XftFont **fallbackFonts = ckalloc(sizeof(XftFont*) * numFonts);

	/* Convert each fontconfig pattern to an actual Xft font for rendering. */
	for (int f = 0; f < numFonts; f++) {
	    FcPattern *render = FcFontRenderPrepare(
						    NULL,            /* display parameter was fixed */
						    fontPtr->pattern,
						    set->fonts[f]
						    );

	    fallbackFonts[f] =
		XftFontOpenPattern(fontPtr->display, render);
	}


	/*
	 * Fast rendering loop that attempts to use the primary font and falls back
	 * through the fontset when glyphs are missing.
	 */
	while (i < numBytes && buffer->glyphCount < MAX_GLYPHS) {
	    FcChar32 uc;
	    int clen = FcUtf8ToUcs4(
				    (const FcChar8 *)(source + i),
				    &uc,
				    numBytes - i
				    );

	    if (clen <= 0) {
		i++;
		continue;
	    }

	    /* Process each character, attempting to find a suitable font. */
	    while (i < numBytes && buffer->glyphCount < MAX_GLYPHS) {
		FcChar32 uc;
		int clen = FcUtf8ToUcs4(
					(const FcChar8 *)(source + i),
					&uc,
					numBytes - i
					);

		if (clen <= 0) {
		    i++;
		    continue;
		}

		/*
		 * Multi-font fallback lookup.
		 * Search through the fontset in priority order until we find a
		 * font that contains the current character.
		 */
		unsigned int glyphId = 0;
		int fontIndex = -1;

		for (int f = 0; f < numFonts; f++) {
		    XftFont *xf = fallbackFonts[f];
		    if (!xf) continue;

		    glyphId = XftCharIndex(fontPtr->display, xf, uc);
		    if (glyphId != 0) {
			fontIndex = f;
			break;
		    }
		}

		/*
		 * Skip characters that have no glyph representation in any
		 * available font.
		 */
		if (glyphId == 0 || fontIndex < 0) {
		    i += clen;
		    continue;
		}

		/*
		 * Retrieve metrics for the glyph and record it in the buffer.
		 * The metrics tell us how much space the glyph occupies.
		 */
		XGlyphInfo metrics;
		XftGlyphExtents(
				fontPtr->display,
				fallbackFonts[fontIndex],
				&glyphId,
				1,
				&metrics
				);

		int g = buffer->glyphCount;

		buffer->glyphs[g].fontIndex  = fontIndex;
		buffer->glyphs[g].glyphId    = glyphId;
		buffer->glyphs[g].x          = penX;
		buffer->glyphs[g].y          = 0;
		buffer->glyphs[g].advanceX   = metrics.xOff;
		buffer->glyphs[g].byteOffset = i;
		buffer->glyphs[g].clusterLen = clen;
		buffer->glyphs[g].isRTL      = 0;

		penX += metrics.xOff;
		buffer->glyphCount++;

		/* Advance the input pointer past the processed character. */
		i += clen;
	    }


	    /*
	     * Finalize the buffer by computing the total advance width and
	     * building the visual index mapping for cursor placement.
	     */
	    buffer->totalAdvance = penX;
	    buffer->indexCount   = buffer->glyphCount;

	    for (int j = 0; j < buffer->glyphCount; j++) {
		buffer->visualIndex[j].x        = buffer->glyphs[j].x;
		buffer->visualIndex[j].advanceX = buffer->glyphs[j].advanceX;
		buffer->visualIndex[j].byteEnd  =
		    buffer->glyphs[j].byteOffset +
		    buffer->glyphs[j].clusterLen;
	    }

	    /*
	     * Clean up fallback fonts to prevent memory leaks.
	     * Close each font and free the associated memory structures.
	     */
	    for (int f = 0; f < numFonts; f++) {
		if (fallbackFonts[f]) {
		    XftFontClose(fontPtr->display, fallbackFonts[f]);
		}
	    }
	    ckfree(fallbackFonts);
	    FcFontSetDestroy(set);

	    return 1;
	}
    }

    /* 
     * Check if we have a cached result for this exact string.
     * This optimization avoids repeated shaping of the same text.
     */
    if (shaper->cache.valid &&
        shaper->cache.len == numBytes &&
        numBytes <= MAX_STRING_CACHE &&
        memcmp(source, shaper->cache.text, numBytes) == 0) {
        *buffer = shaper->cache.buffer;
        return 1;
    }

    /* Convert the utf-8 input to ucs-4 for easier processing. */
    int stackCharBounds[256];
    FcChar32 stackUcs4Chars[256];
    int *charBounds = stackCharBounds;
    FcChar32 *ucs4Chars = stackUcs4Chars;
    int needFree = 0;

    /* For large strings, allocate heap memory instead of using the stack. */
    if (numBytes >= 256) {
        charBounds = (int *)malloc((numBytes + 1) * sizeof(int));
        ucs4Chars = (FcChar32 *)malloc(numBytes * sizeof(FcChar32));
        if (!charBounds || !ucs4Chars) {
            free(charBounds); free(ucs4Chars);
            return 0;
        }
        needFree = 1;
    }

    /* Perform utf-8 to ucs-4 conversion, filtering out control characters. */
    charBounds[0] = 0;
    int bytePos = 0, charCount = 0;
    while (bytePos < numBytes && charCount < MAX_GLYPHS) {
        FcChar32 uc;
        int clen = FcUtf8ToUcs4((const FcChar8 *)(source + bytePos), &uc, numBytes - bytePos);
        if (clen <= 0) { bytePos++; continue; }

        /* Skip control characters except tab, newline, and carriage return. */
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

    /* Handle empty strings after filtering. */
    if (charCount == 0) {
        if (needFree) { free(charBounds); free(ucs4Chars); }
        buffer->clusterBreaks[0] = 0;
        buffer->clusterBreakCount = 1;
        return 1;
    }

    /* 
     * Determine bidirectional text runs for proper left-to-right and
     * right-to-left text layout.
     */
    BidiRun bidiRuns[MAX_BIDI_RUNS];
    int numRuns = GetBidiRuns(ucs4Chars, charCount, bidiRuns, MAX_BIDI_RUNS);

    int globalPenX = 0;

    /* Process each bidirectional run independently. */
    for (int r = 0; r < numRuns; r++) {
        int runStart = bidiRuns[r].offset;
        int runLen   = bidiRuns[r].len;
        int runIsRTL = bidiRuns[r].isRTL;

        if (runLen <= 0) continue;

        int runByteStart = charBounds[runStart];
        int runByteEnd   = charBounds[runStart + runLen];
        int runByteLen   = runByteEnd - runByteStart;
        if (runByteLen <= 0) continue;

        /* Check if the run contains any visible characters worth shaping. */
        int hasVisibleChars = 0;
        for (int ci = runStart; ci < runStart + runLen; ci++) {
            if (ucs4Chars[ci] >= 0x0020 || ucs4Chars[ci] == 0x0009 ||
                ucs4Chars[ci] == 0x000A || ucs4Chars[ci] == 0x000D) {
                hasVisibleChars = 1;
                break;
            }
        }
        if (!hasVisibleChars) continue;

        /* Find the best font face for this run of characters. */
        int runFaceIndex = GetRunFaceIndex(fontPtr, ucs4Chars, runStart, runLen);

        /* 
         * Initialize harfbuzz font objects for each face if this is the
         * first time we're shaping text.
         */
        if (shaper->numFonts == 0) {
            for (int fi = 0; fi < fontPtr->nfaces && shaper->numFonts < MAX_FONTS; fi++) {
                FcPattern *facePattern = fontPtr->faces[fi].source;
                FcChar8 *fontFile = NULL;
                int fontIndex = 0;
                if (FcPatternGetString(facePattern, FC_FILE, 0, &fontFile) != FcResultMatch) continue;
                FcPatternGetInteger(facePattern, FC_INDEX, 0, &fontIndex);

                /* Create harfbuzz objects from the font file. */
                hb_blob_t *blob = hb_blob_create_from_file_or_fail((const char *)fontFile);
                if (!blob) continue;
                hb_face_t *face = hb_face_create(blob, fontIndex);
                if (!face) { hb_blob_destroy(blob); continue; }
                hb_font_t *font = hb_font_create(face);
                if (!font) { hb_face_destroy(face); hb_blob_destroy(blob); continue; }

                /* Set the font scale based on the pixel size from the Xft font. */
                XftFont *xftFont = GetFaceFont(fontPtr, fi, 0.0);
                if (xftFont) {
                    int pixelSize = 0;
                    FT_Face ftFace = XftLockFace(xftFont);
                    if (ftFace && ftFace->size) {
                        pixelSize = (int)ftFace->size->metrics.x_ppem;
                        XftUnlockFace(xftFont);
                    }
                    if (pixelSize <= 0) pixelSize = xftFont->ascent + xftFont->descent;
                    if (pixelSize <= 0) pixelSize = 12;
                    hb_font_set_scale(font, pixelSize * 64, pixelSize * 64);
                    hb_font_set_ppem(font, (unsigned)pixelSize, (unsigned)pixelSize);
                }

                /* Store the harfbuzz objects for later use. */
                shaper->fontMap[shaper->numFonts].hbFont = font;
                shaper->fontMap[shaper->numFonts].faceIndex = fi;
                fontPtr->faces[fi].hbFont = font;
                fontPtr->faces[fi].hbBlob = blob;
                fontPtr->faces[fi].hbFace = face;
                fontPtr->faces[fi].isLoaded = 1;
                shaper->numFonts++;
            }
        }

        /* 
         * Prepare the harfbuzz buffer with the current run of text.
         * Set the direction and cluster level for proper shaping.
         */
        hb_buffer_clear_contents(shaper->buffer);
        hb_buffer_add_utf8(shaper->buffer, source, numBytes, runByteStart, runByteLen);
        hb_buffer_set_direction(shaper->buffer, runIsRTL ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        hb_buffer_set_cluster_level(shaper->buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);

        /* Select the appropriate harfbuzz font for shaping. */
        hb_font_t *runHbFont = NULL;
        int shapeFaceIndex = runFaceIndex;
        if (shapeFaceIndex < fontPtr->nfaces && fontPtr->faces[shapeFaceIndex].isLoaded)
            runHbFont = fontPtr->faces[shapeFaceIndex].hbFont;
        else if (shaper->numFonts > 0) {
            runHbFont = shaper->fontMap[0].hbFont;
            shapeFaceIndex = shaper->fontMap[0].faceIndex;
        }
        if (!runHbFont) continue;

        /* Perform the actual text shaping with harfbuzz. */
        hb_shape(runHbFont, shaper->buffer, NULL, 0);

        /* Retrieve the shaped glyph information and positions. */
        unsigned int glyphCount = hb_buffer_get_length(shaper->buffer);
        hb_glyph_info_t *glyphInfo = hb_buffer_get_glyph_infos(shaper->buffer, NULL);
        hb_glyph_position_t *glyphPos = hb_buffer_get_glyph_positions(shaper->buffer, NULL);
        if (!glyphInfo || !glyphPos) continue;

        /* 
         * Temporary storage for shaped glyphs within this run.
         * We'll convert harfbuzz's 26.6 fixed-point coordinates to integers.
         */
        struct {
            int fontIndex;
            unsigned int glyphId;
            int x, y;
            int advanceX;
            int byteOffset;
            int clusterLen;
        } tempGlyphs[MAX_GLYPHS];
        int tempCount = 0;

        int runPenX = 0;
        XftFont *xftRunFont = GetFaceFont(fontPtr, shapeFaceIndex, 0.0);
        for (unsigned int i = 0; i < glyphCount && tempCount < MAX_GLYPHS; i++) {
            tempGlyphs[tempCount].fontIndex  = shapeFaceIndex;
            tempGlyphs[tempCount].glyphId    = glyphInfo[i].codepoint;
            tempGlyphs[tempCount].x          = globalPenX + runPenX + (int)(glyphPos[i].x_offset / 64.0 + 0.5);
            tempGlyphs[tempCount].y          = -(int)(glyphPos[i].y_offset / 64.0 + 0.5);
            tempGlyphs[tempCount].byteOffset = glyphInfo[i].cluster;

            /* Prefer Xft metrics for advance width when available. */
            unsigned int gid = glyphInfo[i].codepoint;
            if (xftRunFont && gid != 0) {
                XGlyphInfo gmetrics;
                XftGlyphExtents(fontPtr->display, xftRunFont, &gid, 1, &gmetrics);
                tempGlyphs[tempCount].advanceX = gmetrics.xOff;
            } else {
                tempGlyphs[tempCount].advanceX = (int)(glyphPos[i].x_advance / 64.0 + 0.5);
            }

            runPenX += tempGlyphs[tempCount].advanceX;
            tempCount++;
        }

        /* 
         * Correct cluster lengths by looking at the next glyph's byte offset.
         * This ensures each glyph knows how many original bytes it represents.
         */
        for (int i = 0; i < tempCount; i++) {
            int start = tempGlyphs[i].byteOffset;
            int end = runByteEnd;
            for (int j = i + 1; j < tempCount; j++) {
                if (tempGlyphs[j].byteOffset != start) {
                    end = tempGlyphs[j].byteOffset;
                    break;
                }
            }
            tempGlyphs[i].clusterLen = end - start;
            if (tempGlyphs[i].clusterLen <= 0)
                tempGlyphs[i].clusterLen = 1;
        }

        /* Copy the temporary glyphs to the final output buffer. */
        for (int i = 0; i < tempCount; i++) {
            int idx = buffer->glyphCount;
            if (idx >= MAX_GLYPHS) break;

            buffer->glyphs[idx].fontIndex  = tempGlyphs[i].fontIndex;
            buffer->glyphs[idx].glyphId    = tempGlyphs[i].glyphId;
            buffer->glyphs[idx].x          = tempGlyphs[i].x;
            buffer->glyphs[idx].y          = tempGlyphs[i].y;
            buffer->glyphs[idx].advanceX   = tempGlyphs[i].advanceX;
            buffer->glyphs[idx].byteOffset = tempGlyphs[i].byteOffset;
            buffer->glyphs[idx].clusterLen = tempGlyphs[i].clusterLen;
            buffer->glyphs[idx].isRTL      = runIsRTL;

            buffer->glyphCount++;
        }

        globalPenX += runPenX;
    }

    /* Set the total advance width of the entire shaped string. */
    buffer->totalAdvance = globalPenX;

    /* 
     * Build the visual index mapping for cursor and selection handling.
     * This maps from visual positions back to character indices.
     */
    buffer->indexCount = buffer->glyphCount;
    for (int i = 0; i < buffer->glyphCount; i++) {
        buffer->visualIndex[i].x        = buffer->glyphs[i].x;
        buffer->visualIndex[i].advanceX = buffer->glyphs[i].advanceX;
        int byteEnd = buffer->glyphs[i].byteOffset + buffer->glyphs[i].clusterLen;
        if (byteEnd < 0) byteEnd = 0;
        if (byteEnd > numBytes) byteEnd = numBytes;
        buffer->visualIndex[i].byteEnd = byteEnd;
    }

    /* Sort the visual index by x-coordinate to ensure correct cursor positioning. */
    for (int i = 0; i < buffer->indexCount - 1; i++) {
        for (int j = i + 1; j < buffer->indexCount; j++) {
            if (buffer->visualIndex[j].x < buffer->visualIndex[i].x) {
                int tx = buffer->visualIndex[i].x;
                int ta = buffer->visualIndex[i].advanceX;
                int tb = buffer->visualIndex[i].byteEnd;

                buffer->visualIndex[i].x = buffer->visualIndex[j].x;
                buffer->visualIndex[i].advanceX = buffer->visualIndex[j].advanceX;
                buffer->visualIndex[i].byteEnd = buffer->visualIndex[j].byteEnd;

                buffer->visualIndex[j].x = tx;
                buffer->visualIndex[j].advanceX = ta;
                buffer->visualIndex[j].byteEnd = tb;
            }
        }
    }

    /* 
     * Determine cluster break positions for text selection.
     * Cluster breaks indicate where the text can be safely split between graphemes.
     */
    buffer->clusterBreaks[0] = 0;
    buffer->clusterBreakCount = 1;
    char seen[1024] = {0};
    seen[0] = 1;

    for (int i = 0; i < buffer->glyphCount && buffer->clusterBreakCount < MAX_CLUSTER_BREAKS; i++) {
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

    /* Sort the cluster breaks in ascending order. */
    for (int i = 0; i < buffer->clusterBreakCount - 1; i++) {
        for (int j = i + 1; j < buffer->clusterBreakCount; j++) {
            if (buffer->clusterBreaks[i] > buffer->clusterBreaks[j]) {
                int tmp = buffer->clusterBreaks[i];
                buffer->clusterBreaks[i] = buffer->clusterBreaks[j];
                buffer->clusterBreaks[j] = tmp;
            }
        }
    }

    /* Ensure the final cluster break points to the end of the string. */
    if (buffer->clusterBreaks[buffer->clusterBreakCount - 1] != numBytes) {
        if (buffer->clusterBreakCount < MAX_CLUSTER_BREAKS) {
            buffer->clusterBreaks[buffer->clusterBreakCount++] = numBytes;
        }
    }

    /* 
     * Cache the shaped result for small strings to avoid repeated
     * expensive shaping operations.
     */
    if (numBytes <= MAX_STRING_CACHE) {
        memcpy(shaper->cache.text, source, numBytes);
        shaper->cache.len = numBytes;
        shaper->cache.buffer = *buffer;
        shaper->cache.valid = 1;
    }

    /* Free heap-allocated memory if we used it. */
    if (needFree) {
        free(charBounds);
        free(ucs4Chars);
    }

    return 1;
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
    /* Nothing to initialize. */
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
    int rangeEnd = (int)(rangeStart + rangeLength);

    if (rangeLength <= 0) {
        *lengthPtr = 0;
        return 0;
    }

     /* Latin fast path. */
    if (IsSimpleOnly(source, (int)numBytes)) {
        XftFont *ftFont = GetFaceFont(fontPtr, 0, 0.0);
        if (!ftFont) {
            *lengthPtr = 0;
            return (int)rangeLength;
        }

        const char *sub = source + rangeStart;
        int subLen = (int)rangeLength;
        XGlyphInfo extents;

        if (maxLength < 0) {
            XftTextExtentsUtf8(fontPtr->display, ftFont,
                               (const FcChar8 *)sub, subLen, &extents);
            *lengthPtr = extents.xOff;
            return subLen;
        }

        int best = 0, bestWidth = 0;
        int pos = 0;
        while (pos < subLen) {
            FcChar32 uc;
            int clen = FcUtf8ToUcs4((const FcChar8 *)(sub + pos), &uc, subLen - pos);
            if (clen <= 0) clen = 1;
            int next = pos + clen;

            XftTextExtentsUtf8(fontPtr->display, ftFont,
                               (const FcChar8 *)sub, next, &extents);
            if (extents.xOff > maxLength) break;

            best = next;
            bestWidth = extents.xOff;
            pos = next;
        }

        if ((flags & TK_AT_LEAST_ONE) && best == 0 && subLen > 0) {
            best = 1;
            XftTextExtentsUtf8(fontPtr->display, ftFont,
                               (const FcChar8 *)sub, 1, &extents);
            bestWidth = extents.xOff;
        }

        *lengthPtr = bestWidth;
        return best;
    }

    ShapedGlyphBuffer buffer;
    if (!X11Shaper_ShapeString(&fontPtr->shaper, fontPtr, source, (int)numBytes, &buffer) 
        || buffer.glyphCount == 0) {
        *lengthPtr = 0;
        return (int)rangeLength;
    }

    if (maxLength < 0) {
        int total = 0;
        for (int i = 0; i < buffer.glyphCount; i++) {
            if (buffer.glyphs[i].byteOffset >= (int)rangeStart && buffer.glyphs[i].byteOffset < rangeEnd)
                total += buffer.glyphs[i].advanceX;
        }
        *lengthPtr = total;
        return (int)rangeLength;
    }

    int bestBytes = 0;
    int currentWidth = 0;

    /*
     * Iterate through cluster breaks in LOGICAL order.
     * For each candidate wrap point, we sum the advances of ALL glyphs 
     * that belong to that byte range.
     */
    for (int b = 0; b < buffer.clusterBreakCount; b++) {
        int candEnd = buffer.clusterBreaks[b];
        if (candEnd <= (int)rangeStart) continue;
        if (candEnd > rangeEnd) break;

        int trialWidth = 0;
        for (int i = 0; i < buffer.glyphCount; i++) {
            /* Logic: In Bidi, bytes might be scattered. We sum any glyph 
             * whose source byte falls within [rangeStart, candEnd). */
            if (buffer.glyphs[i].byteOffset >= (int)rangeStart && 
                buffer.glyphs[i].byteOffset < candEnd) {
                trialWidth += buffer.glyphs[i].advanceX;
            }
        }

        if (trialWidth <= maxLength) {
            bestBytes = candEnd - (int)rangeStart;
            currentWidth = trialWidth;
        } else {
            break; 
        }
    }

    /* Word wrap logic. */
    if ((flags & TK_WHOLE_WORDS) && bestBytes < (int)rangeLength) {
        for (int i = bestBytes - 1; i > 0; i--) {
            int pos = (int)rangeStart + i;
            if (source[pos] == ' ' || source[pos] == '\t') {
                bestBytes = i + 1;
                /* Re-sum for precision. */
                currentWidth = 0;
                int target = (int)rangeStart + bestBytes;
                for (int j = 0; j < buffer.glyphCount; j++) {
                    if (buffer.glyphs[j].byteOffset >= (int)rangeStart && 
                        buffer.glyphs[j].byteOffset < target) {
                        currentWidth += buffer.glyphs[j].advanceX;
                    }
                }
                break;
            }
        }
    }

    /* Final fallback to prevent redraw loops. */
    if (bestBytes == 0 && (flags & TK_AT_LEAST_ONE) && rangeLength > 0) {
        bestBytes = (buffer.clusterBreakCount > 1) ? (buffer.clusterBreaks[1] - (int)rangeStart) : 1;
        currentWidth = 0;
        int target = (int)rangeStart + bestBytes;
        for (int i = 0; i < buffer.glyphCount; i++) {
            if (buffer.glyphs[i].byteOffset >= (int)rangeStart && buffer.glyphs[i].byteOffset < target)
                currentWidth += buffer.glyphs[i].advanceX;
        }
    }

    *lengthPtr = currentWidth;
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
    GC gc,                 /* Graphics context for drawing characters. */
    Tk_Font tkfont,        /* Font in which characters will be drawn. */
    const char *source,    /* UTF-8 string to be displayed. */
    Tcl_Size numBytes,     /* Number of bytes in string. */
    int x, int y)          /* Coordinates at which to place origin. */
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

if (IsSimpleOnly(source, (int)numBytes)) {

	/* Simple path with fontconfig fallback. */
    FcResult result;
    FcFontSet *set = FcFontSort(
        NULL,               /* Use global config. */
        fontPtr->pattern,
        FcTrue,
        NULL,
        &result
    );

    if (!set || set->nfont <= 0) {
        if (set) FcFontSetDestroy(set);
        goto done;
    }

    int numFonts = set->nfont;
    XftFont **fallbackFonts = ckalloc(sizeof(XftFont*) * numFonts);

    for (int f = 0; f < numFonts; f++) {
        FcPattern *render = FcFontRenderPrepare(
            NULL,
            fontPtr->pattern,
            set->fonts[f]
        );
        fallbackFonts[f] = XftFontOpenPattern(fontPtr->display, render);
    }

    /* Compute x offset for rangeStart. */
    int offsetX = x;
    if (rangeStart > 0) {
        int tmpX = 0;
        Tcl_Size i = 0;
        while (i < rangeStart) {
            FcChar32 uc;
            int clen = FcUtf8ToUcs4(
                (const FcChar8 *)(source + i),
                &uc,
                (int)(rangeStart - i)
            );
            if (clen <= 0) {
                i++;
                continue;
            }

            /* Find font for this char. */
            unsigned int glyphId = 0;
            int fontIndex = -1;
            for (int f = 0; f < numFonts; f++) {
                if (!fallbackFonts[f]) continue;
                glyphId = XftCharIndex(fontPtr->display, fallbackFonts[f], uc);
                if (glyphId != 0) {
                    fontIndex = f;
                    break;
                }
            }
            if (glyphId == 0 || fontIndex < 0) {
                i += clen;
                continue;
            }

            XGlyphInfo ext;
            XftGlyphExtents(fontPtr->display,
                            fallbackFonts[fontIndex],
                            &glyphId, 1, &ext);
            tmpX += ext.xOff;
            i += clen;
        }
        offsetX = x + tmpX;
    }

    /* Build specs for the visible range. */
    XftGlyphFontSpec specs[MAX_GLYPHS];
    int nspec = 0;
    int penX = offsetX;

    Tcl_Size i = rangeStart;
    Tcl_Size end = rangeStart + rangeLength;

    while (i < end && nspec < MAX_GLYPHS) {
        FcChar32 uc;
        int clen = FcUtf8ToUcs4(
            (const FcChar8 *)(source + i),
            &uc,
            (int)(end - i)
        );
        if (clen <= 0) {
            i++;
            continue;
        }

        unsigned int glyphId = 0;
        int fontIndex = -1;
        for (int f = 0; f < numFonts; f++) {
            if (!fallbackFonts[f]) continue;
            glyphId = XftCharIndex(fontPtr->display, fallbackFonts[f], uc);
            if (glyphId != 0) {
                fontIndex = f;
                break;
            }
        }

        if (glyphId == 0 || fontIndex < 0) {
            i += clen;
            continue;
        }

        XGlyphInfo ext;
        XftGlyphExtents(fontPtr->display,
                        fallbackFonts[fontIndex],
                        &glyphId, 1, &ext);

        specs[nspec].font  = fallbackFonts[fontIndex];
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

    /* Cleanup. */
    for (int f = 0; f < numFonts; f++) {
        if (fallbackFonts[f]) {
            XftFontClose(fontPtr->display, fallbackFonts[f]);
        }
    }
    ckfree(fallbackFonts);
    FcFontSetDestroy(set);

    goto done;
}


    /* Complex text path. */
    {
        ShapedGlyphBuffer buffer;
        if (!X11Shaper_ShapeString(&fontPtr->shaper, fontPtr, source,
                                   (int)numBytes, &buffer)) {
            goto done;
        }

        /* Find X offset where rangeStart begins in the shaped output. */
        int rangeStartX = 0;
        if (rangeStart > 0) {
            for (int i = 0; i < buffer.glyphCount; i++) {
                if (buffer.glyphs[i].byteOffset >= (int)rangeStart) {
                    rangeStartX = buffer.glyphs[i].x;
                    break;
                }
            }
        }

        XftGlyphFontSpec specs[MAX_GLYPHS];
        int nspec = 0;

        for (int i = 0; i < buffer.glyphCount && nspec < MAX_GLYPHS; i++) {
            int bo  = buffer.glyphs[i].byteOffset;
            int boe = bo + buffer.glyphs[i].clusterLen;

            /* Draw glyphs whose cluster starts within the range. */
            if (bo < (int)rangeStart || bo >= rangeEnd) continue;

            int faceIdx = buffer.glyphs[i].fontIndex;
            if (faceIdx < 0 || faceIdx >= fontPtr->nfaces) faceIdx = 0;

            XftFont *ftFont = GetFaceFont(fontPtr, faceIdx, 0.0);
            if (!ftFont) continue;

            unsigned int glyph = buffer.glyphs[i].glyphId;
            if (glyph == 0) continue;

            specs[nspec].font  = ftFont;
            specs[nspec].glyph = glyph;
            specs[nspec].x     = x + (buffer.glyphs[i].x - rangeStartX);
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
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
        Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (numBytes == 0) return;

    /* Create XftDraw context. */
    XftDraw *ftDraw = XftDrawCreate(display, drawable,
				    fontPtr->visual, fontPtr->colormap);
    if (!ftDraw) return;

    /* Get foreground color. */
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
     * Fast path for Latin-only strings: use XftDrawStringUtf8 directly
     * with a rotated font.
     */
    if (IsSimpleOnly(source, (int)numBytes)) {
        XftFont *ftFont = GetFaceFont(fontPtr, 0, angle);
        if (ftFont) {
            XftDrawStringUtf8(ftDraw, xftcolor, ftFont,
                              (int)x, (int)y,
                              (const FcChar8 *)source, (int)numBytes);
        }
        goto done;
    }
    
    /*
     * Complex text: use full shaping + bidi pipeline,
     * then rotate glyph positions.
     */
    
    {
	ShapedGlyphBuffer buffer;
	if (!X11Shaper_ShapeString(&fontPtr->shaper, fontPtr, source,
				   (int)numBytes, &buffer)) {
	    goto done;
	}

	/* Rotation setup. */
	double radians = angle * (M_PI / 180.0);
	double cosA = cos(radians);
	double sinA = sin(radians);

	XftGlyphFontSpec specs[MAX_GLYPHS];
	int nspec = 0;

	for (int i = 0; i < buffer.glyphCount && nspec < MAX_GLYPHS; i++) {
	    int faceIdx = buffer.glyphs[i].fontIndex;
	    if (faceIdx < 0 || faceIdx >= fontPtr->nfaces) faceIdx = 0;

	    XftFont *ftFont = GetFaceFont(fontPtr, faceIdx, angle);
	    if (!ftFont) continue;

	    unsigned int glyph = buffer.glyphs[i].glyphId;
	    if (glyph == 0) continue;

	    /* Original (unrotated) glyph position. */
	    double gx = buffer.glyphs[i].x;
	    double gy = buffer.glyphs[i].y;

	    /*
	     * Rotate around (0,0), then translate to (x,y).
	     * Y is inverted for X11 coordinates.
	     */
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
