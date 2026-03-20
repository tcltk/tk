/*
 * tkUnixRFont.c --
 *
 * Alternate implementation of tkUnixFont.c using Xft with proper
 * text shaping via kb_text_shaper.
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
#define KB_TEXT_SHAPE_IMPLEMENTATION
#include <kb_text_shaper.h>
#include <SheenBidi/SheenBidi.h>

#define MAX_CACHED_COLORS 16
#define MAX_GLYPHS 2048
#define MAX_FONTS 64        /* Increased from 8 to support more fallback fonts */
#define MAX_BIDI_RUNS 32
#define MAX_STRING_CACHE 1024        /* Increased from 256 */
#define TK_DRAW_IN_CONTEXT

/*
 * Debugging support - enable with -DDEBUG_FONTSEL=1
 */

#ifndef DEBUG_FONTSEL
#define DEBUG_FONTSEL 0
#endif

#define DEBUG_PRINT(args) \
    do { if (DEBUG_FONTSEL) { printf args; fflush(stdout); } } while(0)

/*
 * ---------------------------------------------------------------
 * BidiRun --
 *
 *   Structure to hold bidirectional run information.
 * ---------------------------------------------------------------
 */

typedef struct {
    int offset;                 /* Byte offset in original UTF-8 string */
    int len;                    /* Length in bytes */
    int isRTL;                  /* 1 if this run is RTL, 0 if LTR */
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
    XftFont *ftFont;           /* Rotated font */
    XftFont *ft0Font;          /* Unrotated font */
    FcPattern *source;         /* Fontconfig pattern */
    FcCharSet *charset;        /* Supported characters */
    double angle;              /* Current rotation angle */

    /* kb_text_shaper font mapping */
    kbts_font *kbFont;         /* Corresponding kbts font */
    int isLoaded;              /* Whether kbFont was successfully loaded */

    /* Font metrics for scaling */
    double unitsPerEm;         /* For scaling glyph positions */
    double ascender;           /* Font ascender in design units */
    double descender;          /* Font descender in design units */
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
 *   Note on glyph IDs: kb_text_shaper operates directly on raw TrueType
 *   glyph index space (the same space as FT_Get_Char_Index / Xft internal
 *   indices). IDs are therefore directly usable with XftDrawGlyphFontSpec
 *   provided the same font file+face-index is used for both shaping and
 *   rendering. The fontIndex field tracks which UnixFtFace produced each
 *   glyph to ensure this invariant holds.
 * ---------------------------------------------------------------
 */

typedef struct {
    struct {
        int fontIndex;         /* Index into UnixFtFont->faces[] */
        unsigned int glyphId;  /* Raw TrueType glyph index (kbts == Xft space) */
        int x, y;              /* Pen position for this glyph */
        int advanceX;          /* Advance width in pixels */
        int byteOffset;        /* Byte offset of cluster start in source string */
        int clusterLen;        /* Length of cluster in bytes */
    } glyphs[MAX_GLYPHS];
    int glyphCount;
    int totalAdvance;          /* Total advance width in pixels */
} ShapedGlyphBuffer;

/*
 * ---------------------------------------------------------------
 * X11Shaper --
 *
 *   Persistent per-font shaping state. Owns the kbts context,
 *   bidirectional font mapping, and string cache.
 * ---------------------------------------------------------------
 */

typedef struct {
    kbts_shape_context *context;

    /* Font mapping - bidirectional */
    struct {
        kbts_font *kbFont;
        int faceIndex;
    } fontMap[MAX_FONTS];
    int numFonts;

    /* Simple string cache for common measurements */
    struct {
        char text[MAX_STRING_CACHE];
        int len;
        ShapedGlyphBuffer buffer;
        int valid;
    } cache;

    /* Error tracking */
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
    TkFont font;                /* Must be first */
    UnixFtFace *faces;
    int nfaces;
    FcFontSet *fontset;
    FcPattern *pattern;

    Display *display;
    int screen;
    Colormap colormap;
    Visual *visual;
    XftDraw *ftDraw;
    Drawable lastDrawable;      /* Last drawable used with ftDraw */
    int ncolors;
    int firstColor;
    UnixFtColorList colors[MAX_CACHED_COLORS];

    X11Shaper shaper;

    /* Precomputed scale factors */
    double pixelScale;          /* Global pixel scaling factor */
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
static int IsSimpleText(const char *source, int len);

/*
 * ---------------------------------------------------------------
 * utf8ToUcs4 --
 *
 *   Convert UTF-8 to UCS-4, using Tcl or Fontconfig depending on available
 *   bytes.
 *
 * Results:
 *   Number of bytes consumed. *cPtr filled with UCS-4 character.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

static int
utf8ToUcs4(
    const char *source,
    FcChar32 *cPtr,
    int numBytes)
{
    if (numBytes >= 6) {
        return Tcl_UtfToUniChar(source, (int *)cPtr);
    }
    return FcUtf8ToUcs4((const FcChar8 *)source, cPtr, numBytes);
}

/*
 * ---------------------------------------------------------------
 * IsSimpleText --
 *
 *   Determine whether a UTF-8 string consists only of characters
 *   from simple scripts (Latin, Greek, Cyrillic, etc.) that do not
 *   require complex shaping. This enables a fast path using
 *   XftDrawString32.
 *
 *   The simple range is defined as U+0000 - U+02FF, which includes:
 *     - Basic Latin, Latin-1 Supplement, Latin Extended-A
 *     - IPA Extensions, Spacing Modifier Letters
 *     - Combining Diacritical Marks, Greek, Cyrillic
 *
 * Results:
 *   1 if all characters are in the simple range, 0 otherwise.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

static int
IsSimpleText(
    const char *source,
    int len)
{
    const unsigned char *p = (const unsigned char *)source;
    int i = 0;

    while (i < len) {
        if (p[i] < 0x80) {
            i++;
            continue;
        }
        FcChar32 u;
        int clen = Tcl_UtfToUniChar(source + i, (int *)&u);
        if (clen <= 0) return 0;
        if (u > 0x02FF) return 0;  /* Outside simple range */
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
        return NULL;
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
                /* Fallback to basic sans font */
                LOCK;
                face->ft0Font = XftFontOpen(fontPtr->display, fontPtr->screen,
                                           FC_FAMILY, FcTypeString, "sans",
                                           FC_SIZE, FcTypeDouble, 12.0,
                                           NULL);
                UNLOCK;
            }
            
            /* Extract font metrics for proper scaling. */
            if (face->ft0Font && face->unitsPerEm == 0) {
                FT_Face ftFace = XftLockFace(face->ft0Font);
                if (ftFace) {
                    face->unitsPerEm = ftFace->units_per_EM;
                    face->ascender = ftFace->ascender;
                    face->descender = ftFace->descender;
                    XftUnlockFace(face->ft0Font);
                }
            }
        }
        return face->ft0Font;
    } else {
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
                /* Fallback. */
                LOCK;
                ftFont = XftFontOpen(fontPtr->display, fontPtr->screen,
                                     FC_FAMILY, FcTypeString, "sans",
                                     FC_SIZE, FcTypeDouble, 12.0,
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

    /* Find face containing this character */
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

    DEBUG_PRINT(("GetTkFontAttributes: family %s size %ld weight %d slant %d\n",
           family, lround(size), weight, slant));

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
 *   level runs with correct directionality.
 *
 * Results:
 *   Returns number of runs created (at least 1). Fills runs array.
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

    /* Determine base level from first strong character. */
    SBLevel baseLevel = SBLevelDefaultLTR;  /* Default to LTR */
    
    for (int i = 0; i < charCount; i++) {
        if (ucs4[i] >= 0x590 && ucs4[i] <= 0x5FF) {  /* Hebrew */
            baseLevel = SBLevelDefaultRTL;
            break;
        } else if (ucs4[i] >= 0x600 && ucs4[i] <= 0x6FF) {  /* Arabic */
            baseLevel = SBLevelDefaultRTL;
            break;
        } else if (ucs4[i] >= 0x700 && ucs4[i] <= 0x74F) {  /* Syriac */
            baseLevel = SBLevelDefaultRTL;
            break;
        } else if (ucs4[i] >= 0x590) {  /* Any other RTL script */
            baseLevel = SBLevelDefaultRTL;
            break;
        }
    }

    /* Full bidi analysis with proper base level. */
    SBCodepointSequence seq = {SBStringEncodingUTF32, ucs4, (SBUInteger)charCount};
    SBAlgorithmRef algo = SBAlgorithmCreate(&seq);

    SBParagraphRef para = SBAlgorithmCreateParagraph(algo, 0, (SBUInteger)charCount,
                                                     baseLevel);

    SBLineRef line = SBParagraphCreateLine(para, 0, (SBUInteger)charCount);

    SBUInteger runCount = SBLineGetRunCount(line);
    const SBRun *bidiRuns = SBLineGetRunsPtr(line);

    int outRuns = 0;
    for (int i = 0; i < (int)runCount && outRuns < maxRuns; i++) {
        runs[outRuns].offset = (int)bidiRuns[i].offset;
        runs[outRuns].len    = (int)bidiRuns[i].length;
        runs[outRuns].isRTL  = (bidiRuns[i].level & 1);  /* odd level = RTL */
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
 * InitFontErrorProc --
 *
 *   Error handler for font initialization.
 *
 * Results:
 *   Always returns 0.
 *
 * Side effects:
 *   Sets error flag.
 * ---------------------------------------------------------------
 */

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
    int i, errorFlag;
    Tk_ErrorHandler handler;

    if (!fontPtr) {
        fontPtr = (UnixFtFont *)Tcl_Alloc(sizeof(UnixFtFont));
        memset(fontPtr, 0, sizeof(UnixFtFont));
    }

    /* Configure font pattern */
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    XftDefaultSubstitute(Tk_Display(tkwin), Tk_ScreenNumber(tkwin), pattern);

    /* Sort fonts by quality */
    set = FcFontSort(NULL, pattern, FcTrue, NULL, &result);
    if (!set || set->nfont == 0) {
        if (fontPtr) Tcl_Free(fontPtr);
        return NULL;
    }

    fontPtr->fontset = set;
    fontPtr->pattern = pattern;
    fontPtr->faces = (UnixFtFace *)Tcl_Alloc(set->nfont * sizeof(UnixFtFace));
    memset(fontPtr->faces, 0, set->nfont * sizeof(UnixFtFace));
    fontPtr->nfaces = set->nfont;

    /* Initialize each face. */
    for (i = 0; i < set->nfont; i++) {
        fontPtr->faces[i].source = set->fonts[i];
        if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &charset) == FcResultMatch) {
            fontPtr->faces[i].charset = FcCharSetCopy(charset);
        }

        /* Will get actual unitsPerEm when font is opened. */
        fontPtr->faces[i].unitsPerEm = 0;
        fontPtr->faces[i].ascender = 0;
        fontPtr->faces[i].descender = 0;
    }

    fontPtr->display = Tk_Display(tkwin);
    fontPtr->screen = Tk_ScreenNumber(tkwin);
    fontPtr->colormap = Tk_Colormap(tkwin);
    fontPtr->visual = Tk_Visual(tkwin);
    fontPtr->firstColor = -1;
    fontPtr->lastDrawable = None;

    /* Get base font metrics. */
    errorFlag = 0;
    handler = Tk_CreateErrorHandler(Tk_Display(tkwin), -1, -1, -1,
                                    InitFontErrorProc, (void *)&errorFlag);

    ftFont = GetFont(fontPtr, 0, 0.0);
    if (!ftFont || errorFlag) {
        Tk_DeleteErrorHandler(handler);
        FinishedWithFont(fontPtr);
        return NULL;
    }

    fontPtr->font.fid = XLoadFont(Tk_Display(tkwin), "fixed");
    GetTkFontAttributes(tkwin, ftFont, &fontPtr->font.fa);
    GetTkFontMetrics(ftFont, &fontPtr->font.fm);

    /* Calculate pixel scale. */
    fontPtr->pixelScale = (double)ftFont->ascent / 2048.0; /* Approximate */

    Tk_DeleteErrorHandler(handler);

    if (errorFlag) {
        FinishedWithFont(fontPtr);
        return NULL;
    }

    /*
     * Fontconfig can't report any information about the position or thickness
     * of underlines or overstrikes. Thus, we use some defaults that are
     * hacked around from backup defaults in tkUnixFont.c.
     */
    {
        TkFont *fPtr = &fontPtr->font;
        int iWidth;

        fPtr->underlinePos = fPtr->fm.descent / 2;

        errorFlag = 0;
        handler = Tk_CreateErrorHandler(Tk_Display(tkwin), -1, -1, -1,
                                        InitFontErrorProc, (void *)&errorFlag);
        Tk_MeasureChars((Tk_Font)fPtr, "I", 1, -1, 0, &iWidth);
        Tk_DeleteErrorHandler(handler);

        if (!errorFlag) {
            fPtr->underlineHeight = iWidth / 3;
            if (fPtr->underlineHeight == 0) fPtr->underlineHeight = 1;

            if (fPtr->underlineHeight + fPtr->underlinePos > fPtr->fm.descent) {
                fPtr->underlineHeight = fPtr->fm.descent - fPtr->underlinePos;
                if (fPtr->underlineHeight == 0) {
                    fPtr->underlinePos--;
                    fPtr->underlineHeight = 1;
                }
            }
        } else {
            fPtr->underlineHeight = 1;
        }
    }

    /* Initialize shaper with this font's faces. */
    X11Shaper_Init(&fontPtr->shaper, fontPtr);

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
    Tk_ErrorHandler handler = Tk_CreateErrorHandler(display, -1, -1, -1, NULL, NULL);

    for (i = 0; i < fontPtr->nfaces; i++) {
        if (fontPtr->faces[i].ftFont) {
            LOCK;
            XftFontClose(display, fontPtr->faces[i].ftFont);
            UNLOCK;
        }
        if (fontPtr->faces[i].ft0Font) {
            LOCK;
            XftFontClose(display, fontPtr->faces[i].ft0Font);
            UNLOCK;
        }
        if (fontPtr->faces[i].charset) {
            FcCharSetDestroy(fontPtr->faces[i].charset);
        }
    }

    if (fontPtr->faces) Tcl_Free(fontPtr->faces);
    if (fontPtr->pattern) FcPatternDestroy(fontPtr->pattern);
    if (fontPtr->ftDraw) XftDrawDestroy(fontPtr->ftDraw);
    if (fontPtr->font.fid) XUnloadFont(display, fontPtr->font.fid);
    if (fontPtr->fontset) FcFontSetDestroy(fontPtr->fontset);

    X11Shaper_Destroy(&fontPtr->shaper);

    Tk_DeleteErrorHandler(handler);
}

/*
 * ---------------------------------------------------------------
 * X11Shaper_Init --
 *
 *   Initialize persistent shaping context and load all font faces.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Allocates shaping context and loads fonts.
 * ---------------------------------------------------------------
 */

static void
X11Shaper_Init(
    X11Shaper *s,
    UnixFtFont *fontPtr)
{
    memset(s, 0, sizeof(*s));

    s->context = kbts_CreateShapeContext(0, 0);
    if (!s->context) {
        DEBUG_PRINT(("Failed to create kb_text_shaper context\n"));
        return;
    }

    s->numFonts = 0;
    s->cache.valid = 0;
    s->shapeErrors = 0;

    /*
     * Load fonts into shaper. To avoid initialization hangs, we load
     * a limited set initially. kb_text_shaper will automatically load
     * additional fonts as needed during shaping (on-demand fallback).
     * We cap the initial load at 32 fonts to balance coverage vs. speed.
     */
    int maxInitialFonts = (fontPtr->nfaces < 32) ? fontPtr->nfaces : 32;
    
    /* Load all faces into shaper. */
    for (int i = 0; i < maxInitialFonts && s->numFonts < MAX_FONTS; i++) {
        FcPattern *facePattern = fontPtr->faces[i].source;
        FcChar8 *file;
        int index;

        if (FcPatternGetString(facePattern, FC_FILE, 0, &file) == FcResultMatch &&
            FcPatternGetInteger(facePattern, FC_INDEX, 0, &index) == FcResultMatch) {

            kbts_font *kbFont = kbts_ShapePushFontFromFile(s->context,
                                                          (const char *)file,
                                                          index);
            if (kbFont) {
                s->fontMap[s->numFonts].kbFont    = kbFont;
                s->fontMap[s->numFonts].faceIndex = i;
                fontPtr->faces[i].kbFont   = kbFont;
                fontPtr->faces[i].isLoaded = 1;
                s->numFonts++;
                DEBUG_PRINT(("Loaded font face %d: %s\n", i, file));
            } else {
                DEBUG_PRINT(("Failed to load font face %d: %s\n", i, file));
                fontPtr->faces[i].isLoaded = 0;
            }
        }
    }
    
    DEBUG_PRINT(("X11Shaper_Init: loaded %d of %d available fonts\n", 
                 s->numFonts, fontPtr->nfaces));
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
    if (s->context) {
        kbts_DestroyShapeContext(s->context);
        s->context = NULL;
    }
}


/*
 * ---------------------------------------------------------------
 * X11Shaper_ShapeString --
 *
 *   Shape a UTF-8 string and produce glyph buffer.
 *
 *   For each output glyph, glyphs[i].fontIndex identifies which
 *   UnixFtFace produced the glyph ID. Since kbts operates in raw
 *   TrueType glyph index space (same as Xft), the ID is valid for
 *   XftDrawGlyphFontSpec as long as the same font file+face is used
 *   for rendering. fontIndex is the key that enforces this.
 *
 *   glyphs[i].byteOffset records the byte offset of the cluster's
 *   start in the original source string, enabling accurate byte-count
 *   returns from Tk_MeasureChars.
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
    if (!shaper->context || !source || numBytes <= 0 || !buffer) {
        return 0;
    }
    
    /* Early exit for shaper with no fonts loaded - can't shape anything. */
    if (shaper->numFonts == 0) {
        return 0;
    }

    /* Check cache first. */
    if (shaper->cache.valid &&
        shaper->cache.len == numBytes &&
        numBytes <= MAX_STRING_CACHE &&
        memcmp(source, shaper->cache.text, numBytes) == 0) {
        *buffer = shaper->cache.buffer;
        return 1;
    }

    buffer->glyphCount   = 0;
    buffer->totalAdvance = 0;

    int shapedAny = 0;

    /* Use stack allocation for common case (small strings). */
    int stackCharBounds[256];
    FcChar32 stackUcs4Chars[256];
    int *charBounds;
    FcChar32 *ucs4Chars;
    int needFree = 0;

    if (numBytes < 256) {
        charBounds = stackCharBounds;
        ucs4Chars = stackUcs4Chars;
    } else {
        charBounds = (int *)malloc((numBytes + 1) * sizeof(int));
        ucs4Chars = (FcChar32 *)malloc(numBytes * sizeof(FcChar32));
        if (!charBounds || !ucs4Chars) {
            free(charBounds);
            free(ucs4Chars);
            return 0;
        }
        needFree = 1;
    }

    /* Convert UTF-8 to UCS-4 and record byte boundaries. */
    charBounds[0] = 0;
    int bytePos   = 0;
    int charCount = 0;

    while (bytePos < numBytes && charCount < MAX_GLYPHS) {
        FcChar32 uc;
        int clen = FcUtf8ToUcs4((const FcChar8 *)(source + bytePos), &uc,
                                numBytes - bytePos);

        if (clen <= 0) {
            /* Invalid sequence → replacement char + skip 1 byte. */
            uc   = 0xFFFD;
            clen = 1;
        }

        ucs4Chars[charCount] = uc;
        charCount++;
        bytePos += clen;
        charBounds[charCount] = bytePos;
    }

    if (charCount == 0) {
        if (needFree) {
            free(charBounds);
            free(ucs4Chars);
        }
        return 0;
    }

    /* Get bidi runs with proper base level detection. */
    BidiRun bidiRuns[MAX_BIDI_RUNS];
    int numRuns = GetBidiRuns(ucs4Chars, charCount, bidiRuns, MAX_BIDI_RUNS);

    /* Track pen position globally. */
    int globalPenX = 0;
    int globalPenY = 0;

    /* For RTL runs, we need to process in reverse visual order. */
    for (int r = 0; r < numRuns; r++) {
        /* Process runs in visual order: RTL runs from right to left */
        int runIdx;
        if (bidiRuns[r].isRTL) {
            /* For RTL runs, we need to process from rightmost to leftmost.
             * But SheenBidi gives us runs in logical order. To get visual
             * order, we should process RTL runs from the end of the string
             * towards the beginning. However, since we're accumulating
             * positions sequentially, we'll shape each run and then adjust
             * positioning for RTL runs by accumulating their widths first. */
            runIdx = r;
        } else {
            runIdx = r;
        }
        
        int runStart = bidiRuns[runIdx].offset;
        int runLen   = bidiRuns[runIdx].len;
        int runIsRTL = bidiRuns[runIdx].isRTL;

        if (runLen <= 0) continue;

        int runByteStart = charBounds[runStart];
        int runByteEnd   = charBounds[runStart + runLen];
        int runByteLen   = runByteEnd - runByteStart;

        if (runByteLen <= 0) continue;

        /* For RTL runs, we need to know the width before we start drawing.
         * We'll shape the run first to get its width, then position glyphs
         * from right to left. */
        
        /* First, shape the run to get glyphs and total width. */
        struct {
            int fontIndex;
            unsigned int glyphId;
            int x, y;
            int advanceX;
            int offsetX;
            int byteOffset;
            int clusterLen;
        } tempGlyphs[MAX_GLYPHS];
        int tempCount = 0;
        int runTotalWidth = 0;

        /* Shape the run with kb_text_shaper. */
        kbts_ShapeBegin(shaper->context,
                        runIsRTL ? KBTS_DIRECTION_RTL : KBTS_DIRECTION_LTR,
                        KBTS_LANGUAGE_DONT_KNOW);

        kbts_ShapeUtf8(shaper->context, source + runByteStart, runByteLen,
                       KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);

        kbts_ShapeEnd(shaper->context);

        /* Collect glyphs from all runs within this bidi run. */
        kbts_run run;
        while (kbts_ShapeRun(shaper->context, &run) == 1) {
            /*
             * Map kbts_font* back to the face index.
             */
            int faceIndex = 0;
            int faceFound = 0;
            
            /* First, check if we already have this font. */
            for (int f = 0; f < shaper->numFonts; f++) {
                if (shaper->fontMap[f].kbFont == run.Font) {
                    faceIndex = shaper->fontMap[f].faceIndex;
                    faceFound = 1;
                    break;
                }
            }
            
            /* If not found, try to load it on-demand. */
            if (!faceFound && shaper->numFonts < MAX_FONTS) {
                for (int i = 0; i < fontPtr->nfaces; i++) {
                    if (fontPtr->faces[i].kbFont == run.Font) {
                        shaper->fontMap[shaper->numFonts].kbFont = run.Font;
                        shaper->fontMap[shaper->numFonts].faceIndex = i;
                        faceIndex = i;
                        faceFound = 1;
                        shaper->numFonts++;
                        DEBUG_PRINT(("Dynamically mapped kbts font %p to face %d\n",
                                     (void *)run.Font, i));
                        break;
                    }
                }
            }
            
            if (!faceFound) {
                DEBUG_PRINT(("WARNING: kbts run.Font %p not found in any face — "
                             "skipping run\n", (void *)run.Font));
                /* Skip this run's glyphs. */
                kbts_glyph_iterator it = run.Glyphs;
                kbts_glyph *g;
                while (kbts_GlyphIteratorNext(&it, &g) == 1) {
                    /* Just advance, don't store. */
                }
                continue;
            }
            
            if (faceIndex >= fontPtr->nfaces) {
                faceIndex = 0;
            }

            /* Get scaling factor. */
            double scale = fontPtr->pixelScale;
            
            XftFont *faceFont = GetFaceFont(fontPtr, faceIndex, 0.0);
            if (faceFont && fontPtr->faces[faceIndex].unitsPerEm > 0) {
                scale = (double)faceFont->ascent / 
                        (double)fontPtr->faces[faceIndex].ascender;
            }

            /* Process glyphs in this sub-run. */
            kbts_glyph_iterator it = run.Glyphs;
            kbts_glyph *glyph;
            while (kbts_GlyphIteratorNext(&it, &glyph) == 1 &&
                   tempCount < MAX_GLYPHS) {

                tempGlyphs[tempCount].fontIndex = faceIndex;
                tempGlyphs[tempCount].glyphId   = glyph->Id;
                tempGlyphs[tempCount].offsetX = (int)(glyph->OffsetX * scale + 0.5);
                tempGlyphs[tempCount].advanceX = (int)(glyph->AdvanceX * scale + 0.5);

                /* Map back to byte offset. */
                int cpIndex = runStart + glyph->UserIdOrCodepointIndex;
                if (cpIndex >= 0 && cpIndex <= charCount) {
                    tempGlyphs[tempCount].byteOffset = charBounds[cpIndex];
                } else {
                    tempGlyphs[tempCount].byteOffset = runByteStart;
                }
                tempGlyphs[tempCount].clusterLen = 0;

                /* Accumulate width for positioning. */
                runTotalWidth += tempGlyphs[tempCount].advanceX;
                tempCount++;
            }
        }

        /* Now position the glyphs based on direction. */
        if (runIsRTL) {
            /* RTL: Position glyphs from right to left.
             * Start at the right edge of the run (globalPenX + runTotalWidth)
             * and move leftwards for each glyph. */
            int currentX = globalPenX + runTotalWidth;
            
            for (int i = 0; i < tempCount && buffer->glyphCount < MAX_GLYPHS; i++) {
                /* For RTL, glyphs are stored in logical order but should be
                 * drawn from rightmost to leftmost. So we use the stored
                 * positions but offset them appropriately. */
                int idx = buffer->glyphCount;
                
                /* Position this glyph: start at currentX, add its offset,
                 * then subtract its advance for the next glyph. */
                buffer->glyphs[idx].fontIndex = tempGlyphs[i].fontIndex;
                buffer->glyphs[idx].glyphId   = tempGlyphs[i].glyphId;
                buffer->glyphs[idx].x = currentX + tempGlyphs[i].offsetX;
                buffer->glyphs[idx].y = globalPenY;  /* Y position unchanged */
                buffer->glyphs[idx].advanceX = tempGlyphs[i].advanceX;
                buffer->glyphs[idx].byteOffset = tempGlyphs[i].byteOffset;
                buffer->glyphs[idx].clusterLen = tempGlyphs[i].clusterLen;
                
                /* Move current position left by advance. */
                currentX -= tempGlyphs[i].advanceX;
                buffer->glyphCount++;
                shapedAny = 1;
            }
            
            /* After RTL run, advance global pen by total width. */
            globalPenX += runTotalWidth;
        } else {
            /* LTR: Position glyphs from left to right. */
            int currentX = globalPenX;
            
            for (int i = 0; i < tempCount && buffer->glyphCount < MAX_GLYPHS; i++) {
                int idx = buffer->glyphCount;
                
                buffer->glyphs[idx].fontIndex = tempGlyphs[i].fontIndex;
                buffer->glyphs[idx].glyphId   = tempGlyphs[i].glyphId;
                buffer->glyphs[idx].x = currentX + tempGlyphs[i].offsetX;
                buffer->glyphs[idx].y = globalPenY;
                buffer->glyphs[idx].advanceX = tempGlyphs[i].advanceX;
                buffer->glyphs[idx].byteOffset = tempGlyphs[i].byteOffset;
                buffer->glyphs[idx].clusterLen = tempGlyphs[i].clusterLen;
                
                currentX += tempGlyphs[i].advanceX;
                buffer->glyphCount++;
                shapedAny = 1;
            }
            
            globalPenX += runTotalWidth;
        }
    }

    buffer->totalAdvance = globalPenX;

    /* Update cache. */
    if (numBytes <= MAX_STRING_CACHE) {
        memcpy(shaper->cache.text, source, numBytes);
        shaper->cache.len    = numBytes;
        shaper->cache.buffer = *buffer;
        shaper->cache.valid  = 1;
    }

    if (needFree) {
        free(charBounds);
        free(ucs4Chars);
    }

    return shapedAny ? 1 : 0;
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
    /* Nothing to initialize */
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
    DEBUG_PRINT(("TkpGetNativeFont: %s\n", name));

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
    XftPattern *pattern = XftPatternCreate();
    int weight, slant;

    DEBUG_PRINT(("TkpGetFontFromAttributes: %s %ld %d %d\n", faPtr->family,
           lround(faPtr->size), faPtr->weight, faPtr->slant));

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

    weight = (faPtr->weight == TK_FW_BOLD) ? XFT_WEIGHT_BOLD : XFT_WEIGHT_MEDIUM;
    XftPatternAddInteger(pattern, XFT_WEIGHT, weight);

    switch (faPtr->slant) {
        case TK_FS_ITALIC:  slant = XFT_SLANT_ITALIC;  break;
        case TK_FS_OBLIQUE: slant = XFT_SLANT_OBLIQUE; break;
        default:            slant = XFT_SLANT_ROMAN;   break;
    }
    XftPatternAddInteger(pattern, XFT_SLANT, slant);

    UnixFtFont *fontPtr = (UnixFtFont *)tkFontPtr;
    if (fontPtr != NULL) {
        FinishedWithFont(fontPtr);
    }

    fontPtr = InitFont(tkwin, pattern, fontPtr);

    /* Fallback if Xft rendering fails. */
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
    Tk_Window tkwin,		/* Window on the font's display */
    Tk_Font tkfont,		/* Font to query */
    int c,			/* Character of interest */
    TkFontAttributes *faPtr)	/* Output: Font attributes */
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
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;

    /* Fast path for simple text (unbounded measure only). */
    if (maxLength < 0 && IsSimpleText(source, (int)numBytes)) {
        XftFont *ftFont = GetFaceFont(fontPtr, 0, 0.0);
        if (ftFont) {
            FcChar32 ucs4[MAX_GLYPHS];
            int count = 0;
            int i = 0;
            while (i < numBytes && count < MAX_GLYPHS) {
                i += Tcl_UtfToUniChar(source + i, (int *)&ucs4[count]);
                count++;
            }
            XGlyphInfo extents;
            LOCK;
            XftTextExtents32(fontPtr->display, ftFont, ucs4, count, &extents);
            UNLOCK;
            *lengthPtr = extents.xOff;
            return (int)numBytes;
        }
    }

    /* Full shaping path. */
    ShapedGlyphBuffer buffer;

    if (!X11Shaper_ShapeString(&fontPtr->shaper, fontPtr, source,
                                (int)numBytes, &buffer)) {
        *lengthPtr = 0;
        return 0;
    }

    int total = 0;
    int bytes = 0;

    for (int i = 0; i < buffer.glyphCount; i++) {
        int next = total + buffer.glyphs[i].advanceX;

        if (maxLength >= 0 && next > maxLength) {
            if (flags & TK_PARTIAL_OK) {
                total = next;
                bytes = buffer.glyphs[i].byteOffset + 1;
            } else if ((flags & TK_AT_LEAST_ONE) && total == 0) {
                total = next;
                bytes = buffer.glyphs[i].byteOffset + 1;
            }
            break;
        }

        total = next;
        /*
         * bytes tracks the byte-end of the last fully-included cluster.
         * byteOffset is the start of this glyph's cluster; the end of
         * this cluster is the start of the next, or numBytes if this is
         * the last glyph.  Using byteOffset+1 would be wrong for multi-
         * byte sequences, so instead we use the next glyph's byteOffset
         * (or numBytes) as the exclusive end of this cluster.
         */
        if (i + 1 < buffer.glyphCount) {
            bytes = buffer.glyphs[i + 1].byteOffset;
        } else {
            bytes = (int)numBytes;
        }
    }

    *lengthPtr = total;

    if (bytes == 0 && buffer.glyphCount > 0) {
        bytes = (int)numBytes;
    }

    return bytes;
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
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
        Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    /* Ensure XftDraw is ready */
    if (!fontPtr->ftDraw) {
        fontPtr->ftDraw = XftDrawCreate(display, drawable,
                                        fontPtr->visual, fontPtr->colormap);
        fontPtr->lastDrawable = drawable;
    } else if (fontPtr->lastDrawable != drawable) {
        XftDrawChange(fontPtr->ftDraw, drawable);
        fontPtr->lastDrawable = drawable;
    }

    XGCValues values;
    XGetGCValues(display, gc, GCForeground, &values);

    XftColor *xftcolor = LookUpColor(display, fontPtr, values.foreground);
    if (!xftcolor) return;

    if (tsdPtr->clipRegion) {
        XftDrawSetClip(fontPtr->ftDraw, tsdPtr->clipRegion);
    }

    /* Fast path for simple text. */
    if (IsSimpleText(source, (int)numBytes)) {
        XftFont *ftFont = GetFaceFont(fontPtr, 0, 0.0);
        if (ftFont) {
            FcChar32 ucs4[MAX_GLYPHS];
            int count = 0;
            int i = 0;
            while (i < numBytes && count < MAX_GLYPHS) {
                i += Tcl_UtfToUniChar(source + i, (int *)&ucs4[count]);
                count++;
            }
            LOCK;
            XftDrawString32(fontPtr->ftDraw, xftcolor, ftFont, x, y, ucs4, count);
            UNLOCK;
            goto done;
        }
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
            if (faceIdx >= fontPtr->nfaces) faceIdx = 0;

            XftFont *ftFont = GetFaceFont(fontPtr, faceIdx, 0.0);
            if (!ftFont) continue;

            /*
             * kb_text_shaper provides shaped glyph positions and clusters.
             * When it returns glyph ID 0, it means the current font doesn't have
             * this glyph, but the shaped position/advance is still valid.
             *
             * Strategy: Keep the shaped position from kb_text_shaper, but look up
             * the glyph ID from a fallback font. This preserves complex shaping
             * (Arabic ligatures, Devanagari conjuncts) while supporting font fallback.
             */
            unsigned int actualGlyph = buffer.glyphs[i].glyphId;
            
            if (actualGlyph == 0) {
                /* Try to find this glyph in a fallback font. */
                FcChar32 ucs4 = 0;
                
                /* Extract the codepoint from the source string. */
                int byteOff = buffer.glyphs[i].byteOffset;
                if (byteOff >= 0 && byteOff < numBytes) {
                    FcUtf8ToUcs4((const FcChar8 *)(source + byteOff), &ucs4, 
                                 numBytes - byteOff);
                }
                
                if (ucs4 != 0) {
                    /* Find a font that has this character. */
                    XftFont *fallbackFont = GetFont(fontPtr, ucs4, 0.0);
                    if (fallbackFont) {
                        actualGlyph = XftCharIndex(fontPtr->display, fallbackFont, ucs4);
                        if (actualGlyph != 0) {
                            ftFont = fallbackFont;
                        }
                    }
                }
                
                /* If still no glyph, skip this position. */
                if (actualGlyph == 0) {
                    continue;
                }
            }

            /* Use the shaped position from kb_text_shaper, even if we used a fallback glyph. */
            specs[nspec].font  = ftFont;
            specs[nspec].glyph = actualGlyph;
            specs[nspec].x     = x + buffer.glyphs[i].x;
            specs[nspec].y     = y + buffer.glyphs[i].y;
            nspec++;
        }

        if (nspec > 0) {
            LOCK;
            XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
            UNLOCK;
        }
    }

done:
    if (tsdPtr->clipRegion) {
        XftDrawSetClip(fontPtr->ftDraw, NULL);
    }
}

/*
 * ---------------------------------------------------------------
 * Tk_DrawCharsInContext --
 *
 *   Draws a substring of text using full shaping + bidi logic.
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
    Tk_DrawChars(display, drawable, gc, tkfont,
                 source + rangeStart, rangeLength, x, y);
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

    /* Simple UCS-4 conversion and drawing */
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
