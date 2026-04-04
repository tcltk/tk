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
#include <signal.h>
#include <setjmp.h>
#include <hb.h>
#include <hb-ft.h>
#include <SheenBidi/SheenBidi.h>

#define MAX_CACHED_COLORS 16
#define MAX_GLYPHS 512
#define MAX_FONTS 100
#define MAX_BIDI_RUNS 32
#define MAX_STRING_CACHE 1024

#define TK_DRAW_IN_CONTEXT

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

/*
 * ---------------------------------------------------------------
 * ShapedGlyphBuffer --
 *
 *   Pure output buffer for shaped glyphs. No shaper state; no cache.
 *   Callers stack-allocate this, pass it to shaping functions.
 *
 *   Note on glyph IDs: HarfBuzz operates on FreeType glyph indices,
 *   which match Xft's internal glyph space. IDs are directly usable
 *   with XftDrawGlyphFontSpec provided the same font file+face is used
 *   for both shaping and rendering. fontIndex tracks which UnixFtFace
 *   produced each glyph to ensure this invariant.
 *
 *   byteOffset records the cluster start AND clusterLen records
 *   the cluster length in bytes. This enables accurate cursor placement and
 *   text selection by mapping pixel positions back to byte ranges.
 * ---------------------------------------------------------------
 */

typedef struct {
    struct {
        int fontIndex;         /* Index into UnixFtFont->faces[]. */
        unsigned int glyphId;  /* FreeType glyph index (HarfBuzz == Xft space). */
        int x, y;              /* Pen position for this glyph. */
        int advanceX;          /* Advance width in pixels. */
        int byteOffset;        /* Byte offset of cluster start in source string. */
        int clusterLen;        /* Length of cluster in bytes. */
        int isRTL;             /* 1 if this glyph is part of an RTL run. */
    } glyphs[MAX_GLYPHS];
    int glyphCount;
    int totalAdvance;          /* Total advance width in pixels. */
    
    /*
     * Visual index for cursor positioning.
     * 
     * Parallel structure that maps visual (screen) positions back to logical 
     * (source string) byte offsets. Built in sync with word reversal so that
     * cursor positioning works correctly for LTR, RTL, and mixed text.
     * 
     * visualIndex[i] corresponds to glyphs[i] and contains:
     * - x: visual X position of this glyph
     * - advanceX: width of this glyph
     * - byteEnd: logical byte offset after this glyph (byteOffset + clusterLen)
     * 
     * For cursor positioning: binary search to find glyph at visual position X,
     * then return the corresponding byteEnd.
     */
    struct {
        int x;              /* Visual X position of glyph */
        int advanceX;       /* Glyph width */
        int byteEnd;        /* Logical byte end (byteOffset + clusterLen) */
    } visualIndex[MAX_GLYPHS];
    int indexCount;         /* Should equal glyphCount; kept separate for clarity */
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
static int IsLatinOnly(const char *str, int len);  /* fast-path helper */
static int
GetRunFaceIndex(UnixFtFont *fontPtr, FcChar32 *ucs4Chars, int runStart, int runLen);

/*
 * ---------------------------------------------------------------
 * IsLatinOnly --
 *
 *   Returns 1 if every codepoint in the UTF-8 string falls within
 *   the Latin "simple" range: Basic Latin (U+0000-U+007F), Latin-1
 *   Supplement (U+0080-U+00FF), Latin Extended-A (U+0100-U+017F),
 *   or Latin Extended-B (U+0180-U+024F).
 *
 *   All codepoints in this range (U+0000-U+024F) are precomposed
 *   or trivially handled by a single Xft font face with no BiDi
 *   reordering, no ligature shaping, and no multi-face fallback.
 *   Using XftTextExtentsUtf8 / XftDrawStringUtf8 directly is both
 *   correct and significantly faster for these scripts.
 *
 *   Returns 0 as soon as any out-of-range codepoint is found.
 * ---------------------------------------------------------------
 */

static int
IsLatinOnly(const char *str, int len)
{
    int i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)str[i];

        if (c < 0x80) {
            /* Single-byte ASCII — always fine. */
            i++;
            continue;
        }

        /* Decode the leading byte to determine codepoint range. */
        FcChar32 uc;
        int clen = FcUtf8ToUcs4((const FcChar8 *)(str + i), &uc, len - i);
        if (clen <= 0) {
            /* Invalid UTF-8 — route to shaper for safe handling. */
            return 0;
        }

        /* Accept U+0080 through U+024F (Latin-1 Supplement through
         * Latin Extended-B).  Everything above U+024F needs the shaper.
         */
        if (uc > 0x024F) {
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

/*
 * GetRunFaceIndex --
 *
 *  Choose the best font face for a given run based on the first character.
 *
 *  Results:
 *    Font face; falls back to face 0 if no match is found.
 *
 *  Side effects:
 *    None. 
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
 *   Shape a UTF-8 string using HarfBuzz and produce glyph buffer.
 *
 *   Computes actual clusterLen for each glyph.
 *   Clamps byteOffset to valid range to prevent buffer overruns.
 *   Shapes all runs LTR, then reverses RTL runs manually to fix
 *   word-level BiDi (was reversing glyphs within words incorrectly).
 *   Stores isRTL flag per-glyph for cursor movement logic.
 *
 *   For each output glyph, glyphs[i].fontIndex identifies which
 *   UnixFtFace produced the glyph ID. Since HarfBuzz operates in raw
 *   FreeType glyph index space (same as Xft), the ID is valid for
 *   XftDrawGlyphFontSpec as long as the same font file+face is used
 *   for rendering. fontIndex is the key that enforces this.
 *
 *   glyphs[i].byteOffset records the byte offset of the cluster's
 *   start in the original source string, and glyphs[i].clusterLen
 *   records the length in bytes. Together these enable accurate
 *   byte-count returns from Tk_MeasureChars and pixel-to-byte
 *   mapping for cursor placement.
 *
 * Results:
 *   1 on success, 0 on failure.
 *
 * Side effects:
 *   Updates the shaper cache; buffer is filled.
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
    if (!shaper->buffer || !source || numBytes <= 0 || !buffer) {
        return 0;
    }

    /* Latin-only fast path. */
    if (IsLatinOnly(source, numBytes)) {
        XftFont *ftFont = GetFaceFont(fontPtr, 0, 0.0);
        if (ftFont) {
            int penX = 0;
            buffer->glyphCount = 0;
            int i = 0;
            while (i < numBytes && buffer->glyphCount < MAX_GLYPHS) {
                FcChar32 uc;
                int clen = FcUtf8ToUcs4((const FcChar8 *)(source + i), &uc, numBytes - i);
                if (clen <= 0) { i++; continue; }

                unsigned int glyphId = XftCharIndex(fontPtr->display, ftFont, uc);
                XGlyphInfo metrics;
                XftGlyphExtents(fontPtr->display, ftFont, &glyphId, 1, &metrics);

                buffer->glyphs[buffer->glyphCount].fontIndex  = 0;
                buffer->glyphs[buffer->glyphCount].glyphId    = glyphId;
                buffer->glyphs[buffer->glyphCount].x          = penX;
                buffer->glyphs[buffer->glyphCount].y          = 0;
                buffer->glyphs[buffer->glyphCount].advanceX   = metrics.xOff;
                buffer->glyphs[buffer->glyphCount].byteOffset = i;
                buffer->glyphs[buffer->glyphCount].clusterLen = clen;
                buffer->glyphs[buffer->glyphCount].isRTL      = 0;

                penX += metrics.xOff;
                buffer->glyphCount++;
                i += clen;
            }
            buffer->totalAdvance = penX;
            return 1;
        }
    }
    
    /* Cache check. */
    if (shaper->cache.valid &&
        shaper->cache.len == numBytes &&
        numBytes <= MAX_STRING_CACHE &&
        memcmp(source, shaper->cache.text, numBytes) == 0) {
        *buffer = shaper->cache.buffer;
        return 1;
    }

    buffer->glyphCount   = 0;
    buffer->totalAdvance = 0;

    /* UCS-4 conversion. */
    int stackCharBounds[256];
    FcChar32 stackUcs4Chars[256];
    int *charBounds = stackCharBounds;
    FcChar32 *ucs4Chars = stackUcs4Chars;
    int needFree = 0;

    if (numBytes >= 256) {
        charBounds = (int *)malloc((numBytes + 1) * sizeof(int));
        ucs4Chars = (FcChar32 *)malloc(numBytes * sizeof(FcChar32));
        if (!charBounds || !ucs4Chars) {
            free(charBounds); free(ucs4Chars);
            return 0;
        }
        needFree = 1;
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
        buffer->glyphCount = 0;
        buffer->totalAdvance = 0;
        return 1;
    }

    BidiRun bidiRuns[MAX_BIDI_RUNS];
    int numRuns = GetBidiRuns(ucs4Chars, charCount, bidiRuns, MAX_BIDI_RUNS);

    int globalPenX = 0;

    for (int r = 0; r < numRuns; r++) {
        int runStart = bidiRuns[r].offset;
        int runLen   = bidiRuns[r].len;
        int runIsRTL = bidiRuns[r].isRTL;

        if (runLen <= 0) continue;

        int runByteStart = charBounds[runStart];
        int runByteEnd   = charBounds[runStart + runLen];
        int runByteLen   = runByteEnd - runByteStart;
        if (runByteLen <= 0) continue;

        int hasVisibleChars = 0;
        for (int ci = runStart; ci < runStart + runLen; ci++) {
            if (ucs4Chars[ci] >= 0x0020 || ucs4Chars[ci] == 0x0009 ||
                ucs4Chars[ci] == 0x000A || ucs4Chars[ci] == 0x000D) {
                hasVisibleChars = 1;
                break;
            }
        }
        if (!hasVisibleChars) continue;

        int runFaceIndex = GetRunFaceIndex(fontPtr, ucs4Chars, runStart, runLen);

        /* Lazy load HarfBuzz fonts if needed. */
        if (shaper->numFonts == 0) {
            for (int fi = 0; fi < fontPtr->nfaces && shaper->numFonts < MAX_FONTS; fi++) {
                FcPattern *facePattern = fontPtr->faces[fi].source;
                FcChar8 *fontFile = NULL;
                int fontIndex = 0;
                if (FcPatternGetString(facePattern, FC_FILE, 0, &fontFile) != FcResultMatch) continue;
                FcPatternGetInteger(facePattern, FC_INDEX, 0, &fontIndex);

                hb_blob_t *blob = hb_blob_create_from_file_or_fail((const char *)fontFile);
                if (!blob) continue;
                hb_face_t *face = hb_face_create(blob, fontIndex);
                if (!face) { hb_blob_destroy(blob); continue; }
                hb_font_t *font = hb_font_create(face);
                if (!font) { hb_face_destroy(face); hb_blob_destroy(blob); continue; }

                XftFont *xftFont = GetFaceFont(fontPtr, fi, 0.0);
                if (xftFont) {
                    int pixelSize = 12;
                    if (XftPatternGetInteger(xftFont->pattern, XFT_PIXEL_SIZE, 0, &pixelSize) != XftResultMatch) {
                        double ptSize = 12.0;
                        XftPatternGetDouble(xftFont->pattern, XFT_SIZE, 0, &ptSize);
                        pixelSize = (int)(ptSize * 96.0 / 72.0 + 0.5);
                    }
                    hb_font_set_scale(font, pixelSize * 64, pixelSize * 64);
                }

                shaper->fontMap[shaper->numFonts].hbFont = font;
                shaper->fontMap[shaper->numFonts].faceIndex = fi;
                fontPtr->faces[fi].hbFont = font;
                fontPtr->faces[fi].hbBlob = blob;
                fontPtr->faces[fi].hbFace = face;
                fontPtr->faces[fi].isLoaded = 1;
                shaper->numFonts++;
            }
        }

        hb_buffer_clear_contents(shaper->buffer);
        hb_buffer_add_utf8(shaper->buffer, source + runByteStart, runByteLen, 0, runByteLen);
        hb_buffer_set_direction(shaper->buffer, runIsRTL ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        hb_buffer_set_cluster_level(shaper->buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);

        hb_font_t *runHbFont = NULL;
        int shapeFaceIndex = runFaceIndex;
        if (shapeFaceIndex < fontPtr->nfaces && fontPtr->faces[shapeFaceIndex].isLoaded)
            runHbFont = fontPtr->faces[shapeFaceIndex].hbFont;
        else if (shaper->numFonts > 0) {
            runHbFont = shaper->fontMap[0].hbFont;
            shapeFaceIndex = shaper->fontMap[0].faceIndex;
        }
        if (!runHbFont) continue;

        hb_shape(runHbFont, shaper->buffer, NULL, 0);

        unsigned int glyphCount = hb_buffer_get_length(shaper->buffer);
        hb_glyph_info_t *glyphInfo = hb_buffer_get_glyph_infos(shaper->buffer, NULL);
        hb_glyph_position_t *glyphPos = hb_buffer_get_glyph_positions(shaper->buffer, NULL);
        if (!glyphInfo || !glyphPos) continue;

        /* Use temp buffer for safety and correct positioning. */
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
        for (unsigned int i = 0; i < glyphCount && tempCount < MAX_GLYPHS; i++) {
            tempGlyphs[tempCount].fontIndex  = shapeFaceIndex;
            tempGlyphs[tempCount].glyphId    = glyphInfo[i].codepoint;
            tempGlyphs[tempCount].x          = globalPenX + runPenX + (int)(glyphPos[i].x_offset / 64.0 + 0.5);
            tempGlyphs[tempCount].y          = -(int)(glyphPos[i].y_offset / 64.0 + 0.5);
            tempGlyphs[tempCount].advanceX   = (int)(glyphPos[i].x_advance / 64.0 + 0.5);
            tempGlyphs[tempCount].byteOffset = runByteStart + glyphInfo[i].cluster;

            runPenX += tempGlyphs[tempCount].advanceX;
            tempCount++;
        }

        /* Correct cluster lengths (handles RTL). */
        for (int i = 0; i < tempCount; i++) {
            int start = tempGlyphs[i].byteOffset;
            int end = runByteEnd;
            for (int j = i + 1; j < tempCount; j++) {
                if (tempGlyphs[j].byteOffset > start) {
                    end = tempGlyphs[j].byteOffset;
                    break;
                }
            }
            tempGlyphs[i].clusterLen = end - start;
            if (tempGlyphs[i].clusterLen <= 0)
                tempGlyphs[i].clusterLen = 1;
        }

        /* Copy to final buffer. */
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

    buffer->totalAdvance = globalPenX;

    /* Visual index for cursor positioning. */
    buffer->indexCount = buffer->glyphCount;
    for (int i = 0; i < buffer->glyphCount; i++) {
        buffer->visualIndex[i].x        = buffer->glyphs[i].x;
        buffer->visualIndex[i].advanceX = buffer->glyphs[i].advanceX;
        int byteEnd = buffer->glyphs[i].byteOffset + buffer->glyphs[i].clusterLen;
        if (byteEnd < 0) byteEnd = 0;
        if (byteEnd > numBytes) byteEnd = numBytes;
        buffer->visualIndex[i].byteEnd = byteEnd;
    }

    /* Update cache. */
    if (numBytes <= MAX_STRING_CACHE) {
        shaper->cache.valid = 0;
        memcpy(shaper->cache.text, source, numBytes);
        shaper->cache.len = numBytes;
        shaper->cache.buffer = *buffer;
        shaper->cache.valid = 1;
    }

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
 *   FIX: Track minimum and maximum logical byte boundaries to ensure
 *   returned byte counts are monotonically increasing as maxLength increases.
 *   This prevents infinite loops in the text widget's cursor-position search.
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
	Tk_Font tkfont,
	const char *source,
	Tcl_Size numBytes,
	int maxLength,
	int flags,
	int *lengthPtr)
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;

    /*
     * Fast path for Latin-only strings (ASCII through Latin Extended-B,
     * U+0000-U+024F).  XftTextExtentsUtf8 handles the full UTF-8 encoding
     * of these codepoints correctly with a single font face.
     *
     * IMPORTANT: check only the bytes we are asked to measure, not any
     * larger surrounding buffer.  The caller may pass a slice of a
     * mixed-script string whose other portions contain non-Latin text.
     */
    if (IsLatinOnly(source, (int)numBytes)) {
        XftFont *ftFont = GetFaceFont(fontPtr, 0, 0.0);
        if (!ftFont) {
            *lengthPtr = 0;
            return 0;
        }

        /* Total width of the entire string. */
        XGlyphInfo extents;
        XftTextExtentsUtf8(fontPtr->display, ftFont,
                           (const FcChar8 *)source, (int)numBytes, &extents);
        int totalWidth = extents.xOff;

        if (maxLength < 0) {
            *lengthPtr = totalWidth;
            return (int)numBytes;
        }

        /*
         * Linear scan for the longest prefix that fits, advancing one
         * full codepoint at a time.
         *
         * A binary search on raw byte offsets is incorrect for multi-byte
         * UTF-8: the midpoint can land inside a sequence, making
         * XftTextExtentsUtf8 measure a truncated, invalid sequence.
         */
        int best = 0, bestWidth = 0;
        int pos = 0;
        while (pos < (int)numBytes) {
            FcChar32 uc;
            int clen = FcUtf8ToUcs4((const FcChar8 *)(source + pos), &uc,
                                    (int)numBytes - pos);
            if (clen <= 0) clen = 1;  /* skip invalid byte */
            int next = pos + clen;

            XftTextExtentsUtf8(fontPtr->display, ftFont,
                               (const FcChar8 *)source, next, &extents);
            if (extents.xOff > maxLength) break;

            best = next;
            bestWidth = extents.xOff;
            pos = next;
        }

        /* Whole-word break if requested. */
        if ((flags & TK_WHOLE_WORDS) && best < (int)numBytes) {
            int spacePos = -1;
            for (int i = 0; i < best; i++) {
                if (source[i] == ' ' || source[i] == '\t')
                    spacePos = i;
            }
            if (spacePos >= 0) {
                best = spacePos + 1;
                XftTextExtentsUtf8(fontPtr->display, ftFont,
                                   (const FcChar8 *)source, best, &extents);
                bestWidth = extents.xOff;
            }
        }

        /* At least one full codepoint if requested and nothing fit yet. */
        if ((flags & TK_AT_LEAST_ONE) && best == 0 && (int)numBytes > 0) {
            FcChar32 uc;
            int clen = FcUtf8ToUcs4((const FcChar8 *)source, &uc, (int)numBytes);
            if (clen <= 0) clen = 1;
            best = clen;
            XftTextExtentsUtf8(fontPtr->display, ftFont,
                               (const FcChar8 *)source, best, &extents);
            bestWidth = extents.xOff;
        }

        *lengthPtr = bestWidth;
        return best;
    }

    /* Shaping path for non-Latin. */
    ShapedGlyphBuffer buffer;
    if (!X11Shaper_ShapeString(&fontPtr->shaper, fontPtr, source, (int)numBytes, &buffer)) {
        *lengthPtr = 0;
        return (int)numBytes;
    }

    int curX = 0;
    int lastBreakByte = 0;
    int lastBreakX = 0;

    /*
     * Cursor positioning with visual index:
     * 
     * The visualIndex maps visual (screen) positions to logical (source) byte
     * offsets, enabling accurate cursor positioning for all text directions.
     */
    int prevByteEnd = 0;
    
    for (int i = 0; i < buffer.indexCount; i++) {
        int glyphX = buffer.visualIndex[i].x;
        int glyphAdvance = buffer.visualIndex[i].advanceX;
        int glyphXEnd = glyphX + glyphAdvance;
        int byteEnd = buffer.visualIndex[i].byteEnd;
        
        if (byteEnd < 0) byteEnd = 0;
        if (byteEnd > (int)numBytes) byteEnd = (int)numBytes;

        /* Record word-break opportunities based on source character. */
        if (byteEnd > 0 && byteEnd <= (int)numBytes) {
            if (source[byteEnd - 1] == ' ' || source[byteEnd - 1] == '\t') {
                lastBreakByte = byteEnd;
                lastBreakX = glyphXEnd;
            }
        }

        if (maxLength >= 0 && glyphXEnd > maxLength) {
            if (lastBreakByte > 0 && (flags & TK_WHOLE_WORDS)) {
                *lengthPtr = lastBreakX;
                return lastBreakByte;
            }
            
            /* Ensure we return at least one character if TK_AT_LEAST_ONE is set. */
            if (prevByteEnd == 0 && (flags & TK_AT_LEAST_ONE)) {
                *lengthPtr = glyphXEnd;
                return byteEnd;
            }
            
            *lengthPtr = curX;
            return prevByteEnd;
        }

        curX = glyphXEnd;
        prevByteEnd = byteEnd;
    }

    /* All glyphs fit - return total. */
    if (prevByteEnd == 0 && (int)numBytes > 0) {
        prevByteEnd = (int)numBytes;
    }

    *lengthPtr = (maxLength < 0) ? buffer.totalAdvance : curX;
    return prevByteEnd;
}

/*
 * ---------------------------------------------------------------
 * Tk_MeasureCharsInContext --
 *
 *   Measure a substring of a larger string, preserving shaping context.
 *
 *   Shapes the FULL string and extracts metrics for the
 *   requested range. This preserves ligatures, kerning, and BiDi
 *   analysis across substring boundaries.
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

    /*
     * Fast path for Latin-only strings (U+0000-U+024F).
     *
     * Check only the substring [rangeStart, rangeStart+rangeLength).
     */
    if (IsLatinOnly(source + rangeStart, (int)rangeLength)) {
        XftFont *ftFont = GetFaceFont(fontPtr, 0, 0.0);
        if (!ftFont) {
            *lengthPtr = 0;
            return 0;
        }

        const char *sub = source + rangeStart;
        int subLen = (int)rangeLength;

        XGlyphInfo extents;
        XftTextExtentsUtf8(fontPtr->display, ftFont,
                           (const FcChar8 *)sub, subLen, &extents);
        int totalWidth = extents.xOff;

        if (maxLength < 0) {
            *lengthPtr = totalWidth;
            return subLen;
        }

        /*
         * Linear codepoint scan — same reasoning as Tk_MeasureChars above.
         */
        int best = 0, bestWidth = 0;
        int pos = 0;
        while (pos < subLen) {
            FcChar32 uc;
            int clen = FcUtf8ToUcs4((const FcChar8 *)(sub + pos), &uc,
                                    subLen - pos);
            if (clen <= 0) clen = 1;
            int next = pos + clen;

            XftTextExtentsUtf8(fontPtr->display, ftFont,
                               (const FcChar8 *)sub, next, &extents);
            if (extents.xOff > maxLength) break;

            best = next;
            bestWidth = extents.xOff;
            pos = next;
        }

        /* Whole-word break if requested (within substring). */
        if ((flags & TK_WHOLE_WORDS) && best < subLen) {
            int spacePos = -1;
            for (int i = 0; i < best; i++) {
                if (sub[i] == ' ' || sub[i] == '\t')
                    spacePos = i;
            }
            if (spacePos >= 0) {
                best = spacePos + 1;
                XftTextExtentsUtf8(fontPtr->display, ftFont,
                                   (const FcChar8 *)sub, best, &extents);
                bestWidth = extents.xOff;
            }
        }

        /* At least one full codepoint if requested. */
        if ((flags & TK_AT_LEAST_ONE) && best == 0 && subLen > 0) {
            FcChar32 uc;
            int clen = FcUtf8ToUcs4((const FcChar8 *)sub, &uc, subLen);
            if (clen <= 0) clen = 1;
            best = clen;
            XftTextExtentsUtf8(fontPtr->display, ftFont,
                               (const FcChar8 *)sub, best, &extents);
            bestWidth = extents.xOff;
        }

        *lengthPtr = bestWidth;
        return best;
    }

    /* Shaping path for non-Latin. */
    ShapedGlyphBuffer buffer;
    if (!X11Shaper_ShapeString(&fontPtr->shaper, fontPtr, source,
                                (int)numBytes, &buffer)) {
        *lengthPtr = 0;
        return (int)rangeLength;
    }

    int totalWidth = 0;
    int rangeEnd = (int)(rangeStart + rangeLength);
    int lastBreakPos = (int)rangeStart;
    int lastBreakWidth = 0;
    int lastBreakGlyph = -1;
    int bytesConsumed = (int)rangeStart;

    /*
     * Cursor positioning in range with visual index.
     */
    for (int i = 0; i < buffer.indexCount; i++) {
        int glyphAdvance = buffer.visualIndex[i].advanceX;
        int byteEnd = buffer.visualIndex[i].byteEnd;

        if (byteEnd < 0) byteEnd = 0;
        if (byteEnd > (int)numBytes) byteEnd = (int)numBytes;

        /* Skip glyphs outside the range. */
        if (byteEnd <= (int)rangeStart || 
            (i == 0 && buffer.glyphs[i].byteOffset >= rangeEnd)) {
            continue;
        }

        int nextWidth = totalWidth + glyphAdvance;

        /* Record word-break opportunities. */
        if (byteEnd > 0 && byteEnd <= (int)numBytes) {
            if (source[byteEnd - 1] == ' ' || 
                source[byteEnd - 1] == '\t' || 
                source[byteEnd - 1] == '\n') {
                lastBreakPos   = byteEnd;
                lastBreakWidth = nextWidth;
                lastBreakGlyph = i;
            }
        }

        if (maxLength >= 0 && nextWidth > maxLength) {
            if (lastBreakGlyph >= 0 && (flags & TK_WHOLE_WORDS)) {
                totalWidth = lastBreakWidth;
                bytesConsumed = lastBreakPos;
            } else if (bytesConsumed == (int)rangeStart) {
                /* Nothing consumed yet - include this glyph. */
                totalWidth = nextWidth;
                bytesConsumed = byteEnd;
            }
            break;
        }

        totalWidth = nextWidth;
        if (byteEnd > bytesConsumed) {
            bytesConsumed = byteEnd;
        }
    }

    if (bytesConsumed < (int)rangeStart) bytesConsumed = (int)rangeStart;
    if (bytesConsumed > rangeEnd) bytesConsumed = rangeEnd;

    /* If we consumed nothing and the range is non-empty, skip to end. */
    if (bytesConsumed == (int)rangeStart && rangeLength > 0) {
        bytesConsumed = rangeEnd;
    }

    *lengthPtr = totalWidth;
    return (bytesConsumed - (int)rangeStart);
}

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
    Display *display,      /* Display on which to draw. */
    Drawable drawable,     /* Window or pixmap in which to draw. */
    GC gc,                 /* Graphics context for drawing characters. */
    Tk_Font tkfont,        /* Font in which characters will be drawn. */
    const char *source,    /* UTF-8 string to be displayed. */
    Tcl_Size numBytes,     /* Number of bytes in string. */
    int x, int y)          /* Coordinates at which to place origin. */
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
        Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    XftDraw *ftDraw = NULL;

    if (numBytes <= 0) return;

    /* Create a temporary XftDraw for this draw operation. */
    ftDraw = XftDrawCreate(display, drawable,
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
     * Fast path for Latin-only strings (U+0000-U+024F).
     * Check only the bytes being drawn, not any larger buffer.
     */
    if (IsLatinOnly(source, (int)numBytes)) {
        XftFont *ftFont = GetFaceFont(fontPtr, 0, 0.0);
        if (ftFont) {
            XftDrawStringUtf8(ftDraw, xftcolor, ftFont,
                              x, y, (const FcChar8 *)source, (int)numBytes);
        }
        goto done;
    }

    /* Full shaping path. */
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

            XftFont *ftFont = GetFaceFont(fontPtr, faceIdx, 0.0);
            if (!ftFont) continue;

            unsigned int actualGlyph = buffer.glyphs[i].glyphId;

            /* Fallback logic for missing glyphs in primary font. */
            if (actualGlyph == 0) {
                FcChar32 ucs4 = 0;
                int byteOff = buffer.glyphs[i].byteOffset;
                if (byteOff >= 0 && byteOff < (int)numBytes) {
                    FcUtf8ToUcs4((const FcChar8 *)(source + byteOff), &ucs4,
                                 (int)numBytes - byteOff);
                }

                if (ucs4 != 0) {
                    XftFont *fallbackFont = GetFont(fontPtr, ucs4, 0.0);
                    if (fallbackFont) {
                        actualGlyph = XftCharIndex(display, fallbackFont, ucs4);
                        if (actualGlyph != 0) ftFont = fallbackFont;
                    }
                }
                if (actualGlyph == 0) continue;
            }

            specs[nspec].font  = ftFont;
            specs[nspec].glyph = actualGlyph;
            specs[nspec].x     = x + (int)buffer.glyphs[i].x;
            specs[nspec].y     = y + (int)buffer.glyphs[i].y;
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
    
    /* Always destroy the temporary XftDraw. */
    XftDrawDestroy(ftDraw);
}

/*
 * ---------------------------------------------------------------
 * Tk_DrawCharsInContext --
 *
 *   Draws a substring of text using full shaping + bidi logic,
 *   preserving context from surrounding text for proper ligatures
 *   and BiDi reordering.
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
    Tcl_Size numBytes,        /* Total bytes in source string. */
    Tcl_Size rangeStart,      /* Byte offset of substring start. */
    Tcl_Size rangeLength,     /* Byte length of substring. */
    int x, int y)              /* Coordinates at which to place origin. */
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
        Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (rangeLength <= 0) return;
    
    XftDraw *ftDraw = NULL;

    /* Create a temporary XftDraw for this draw operation. */
    ftDraw = XftDrawCreate(display, drawable,
                          fontPtr->visual, fontPtr->colormap);
    if (!ftDraw) return;

    XGCValues values;
    XGetGCValues(display, gc, GCForeground, &values);
    XftColor *xftcolor = LookUpColor(display, fontPtr, values.foreground);
    if (!xftcolor) return;

    if (tsdPtr->clipRegion) {
        XftDrawSetClip(ftDraw, tsdPtr->clipRegion);
    }

    /*
     * Fast path for Latin-only strings (U+0000-U+024F).
     * Check only the substring being drawn.
     */
    if (IsLatinOnly(source + rangeStart, (int)rangeLength)) {
        XftFont *ftFont = GetFaceFont(fontPtr, 0, 0.0);
        if (ftFont) {
            XftDrawStringUtf8(ftDraw, xftcolor, ftFont,
                              x, y,
                              (const FcChar8 *)(source + rangeStart),
                              (int)rangeLength);
        }
        goto done;
    }

    /* Shape the FULL string to preserve context for ligatures and BiDi. */
    ShapedGlyphBuffer fullBuffer;
    if (!X11Shaper_ShapeString(&fontPtr->shaper, fontPtr, source,
                                (int)numBytes, &fullBuffer)) {
        goto done;
    }

    /* Find the glyphs that overlap our requested range. */
    XftGlyphFontSpec specs[MAX_GLYPHS];
    int nspec = 0;
    int rangeEnd = rangeStart + rangeLength;
    int firstGlyphFound = 0;

    /*
     * Find the X position of the first glyph that starts
     * at or after rangeStart.
     */
    int rangeStartX = 0;
    int rangeStartFound = 0;

    for (int i = 0; i < fullBuffer.glyphCount; i++) {
        int glyphStart = fullBuffer.glyphs[i].byteOffset;
        int glyphEnd = glyphStart + fullBuffer.glyphs[i].clusterLen;

        if (glyphEnd <= rangeStart) {
            /* Glyph is entirely before our range. */
            continue;
        }

        if (!rangeStartFound) {
            /* This is the first glyph that overlaps our range. */
            rangeStartX = fullBuffer.glyphs[i].x;
            rangeStartFound = 1;
            break;
        }
    }

    /* Second pass: Draw the glyphs that overlap our range. */
    for (int i = 0; i < fullBuffer.glyphCount && nspec < MAX_GLYPHS; i++) {
        int glyphStart = fullBuffer.glyphs[i].byteOffset;
        int glyphEnd = glyphStart + fullBuffer.glyphs[i].clusterLen;

        /* Skip glyphs entirely before our range. */
        if (glyphEnd <= rangeStart) {
            continue;
        }

        /* Stop when we've passed our range. */
        if (glyphStart >= rangeEnd) {
            break;
        }

        /* This glyph overlaps our range - draw it. */
        int faceIdx = fullBuffer.glyphs[i].fontIndex;
        if (faceIdx < 0 || faceIdx >= fontPtr->nfaces) faceIdx = 0;

        XftFont *ftFont = GetFaceFont(fontPtr, faceIdx, 0.0);
        if (!ftFont) continue;

        unsigned int actualGlyph = fullBuffer.glyphs[i].glyphId;

        /* Fallback logic for missing glyphs in primary font. */
        if (actualGlyph == 0) {
            FcChar32 ucs4 = 0;
            int byteOff = fullBuffer.glyphs[i].byteOffset;
            if (byteOff >= 0 && byteOff < (int)numBytes) {
                FcUtf8ToUcs4((const FcChar8 *)(source + byteOff), &ucs4,
                             (int)numBytes - byteOff);
            }

            if (ucs4 != 0) {
                XftFont *fallbackFont = GetFont(fontPtr, ucs4, 0.0);
                if (fallbackFont) {
                    actualGlyph = XftCharIndex(display, fallbackFont, ucs4);
                    if (actualGlyph != 0) ftFont = fallbackFont;
                }
            }
            if (actualGlyph == 0) continue;
        }

        /*
	 * Calculate screen position: baseX + (glyph X - rangeStartX).
	 */
        int glyphScreenX = x + (fullBuffer.glyphs[i].x - rangeStartX);

        specs[nspec].font  = ftFont;
        specs[nspec].glyph = actualGlyph;
        specs[nspec].x     = glyphScreenX;
        specs[nspec].y     = y + (int)fullBuffer.glyphs[i].y;
        nspec++;

        if (!firstGlyphFound) {
            firstGlyphFound = 1;
        }
    }

    /* Draw the glyphs. */
    if (nspec > 0) {
        LOCK;
        XftDrawGlyphFontSpec(ftDraw, xftcolor, specs, nspec);
        UNLOCK;
    }

done:
    if (tsdPtr->clipRegion) {
        XftDrawSetClip(ftDraw, NULL);
    }
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
    XftFont *ftFont = GetFont(fontPtr, 0, angle);

    if (!ftFont) return;

    XftDraw *draw = XftDrawCreate(display, drawable,
                                   fontPtr->visual, fontPtr->colormap);
    XGCValues values;
    XGetGCValues(display, gc, GCForeground, &values);

    XftColor color;
    XColor xcolor;
    xcolor.pixel = values.foreground;
    XQueryColor(display, fontPtr->colormap, &xcolor);
    color.color.red   = xcolor.red;
    color.color.green = xcolor.green;
    color.color.blue  = xcolor.blue;
    color.color.alpha = 0xFFFF;
    color.pixel       = values.foreground;

    /* Simple UCS-4 conversion and drawing. */
    FcChar32 ucs4[1024];
    int count = 0;
    int i = 0;

    while (i < numBytes && count < 1024) {
        i += Tcl_UtfToUniChar(source + i, (int *)&ucs4[count]);
        count++;
    }

    XftDrawString32(draw, &color, ftFont, (int)x, (int)y, ucs4, count);
    XftDrawDestroy(draw);
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
