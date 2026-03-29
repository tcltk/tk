/*
 * tkWaylandFont.c --
 *
 * This module implements the Wayland/GLFW platform-specific features of fonts
 * using stb_truetype.h for glyph rendering.
 *
 * Copyright © 1996-1998 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkFont.h"
#include "tkWaylandInt.h"

#include <fontconfig/fontconfig.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include "noto_emoji.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Enable debugging - remove for production */
#define DEBUG_FONT 1
#if DEBUG_FONT
#define FONT_DEBUG(...) fprintf(stderr, "FONT: " __VA_ARGS__)
#else
#define FONT_DEBUG(...)
#endif

/* ============================================================================
 * Font Structure
 * ============================================================================
 */

/* Glyph cache entry - for direct rendering we still cache rendered glyph
 * bitmaps to avoid re-rendering the same glyph multiple times */
typedef struct GlyphCacheEntry {
    int codepoint;               /* Unicode codepoint */
    int width;                   /* Glyph width in pixels */
    int height;                  /* Glyph height in pixels */
    int bearing_x;               /* Left side bearing in pixels */
    int bearing_y;               /* Top side bearing (from baseline) */
    int advance;                 /* Advance width in pixels */
    unsigned char *bitmap;       /* Rendered glyph bitmap (alpha only) */
    struct GlyphCacheEntry *next; /* Hash chain */
} GlyphCacheEntry;

/* Glyph cache hash table size */
#define GLYPH_CACHE_SIZE 1024

/* Platform font structure */
typedef struct {
    TkFont      font;           /* Generic font data — MUST be first. */
    char       *filePath;       /* Absolute path returned by Fontconfig. */
    unsigned char *fontData;    /* Mapped font file data */
    stbtt_fontinfo stbInfo;     /* stb_truetype font info */
    float       scale;          /* Scale factor for pixel size */
    
    /* Glyph cache for this font */
    GlyphCacheEntry *glyphCache[GLYPH_CACHE_SIZE];
    
    /* Metrics */
    int         pixelSize;      /* Resolved size in pixels. */
    int         underlinePos;   /* Pixels below baseline for underline. */
    int         barHeight;      /* Thickness of under/overstrike bar. */
} WaylandFont;

/* Whether Fontconfig has been initialized for this process. */
static int fcInitialized = 0;

/* Emoji font data - loaded once and shared */
static stbtt_fontinfo emojiInfo;
static unsigned char *emojiFontData = NULL;
static int emojiInitialized = 0;

/* Forward declarations */
static char    *FindFontFile(const char *family, int bold, int italic, int pixelSize);
static void     InitFont(Tk_Window tkwin, const TkFontAttributes *fa, WaylandFont *fontPtr);
static void     DeleteFont(WaylandFont *fontPtr);
static int      EnsureFontLoaded(WaylandFont *fontPtr);
static int      GetGlyphBitmap(WaylandFont *fontPtr, int codepoint, GlyphCacheEntry **entryOut);
static void     RenderGlyphToBitmap(WaylandFont *fontPtr, int codepoint, GlyphCacheEntry *entry);
static void     DrawGlyphDirect(struct cg_ctx_t *cg, WaylandFont *fontPtr, 
                                 GlyphCacheEntry *glyph, float x, float y, uint32_t color);
static void     DrawRectangle(struct cg_ctx_t *cg, float x, float y, float w, float h, uint32_t color);
static void     DrawLine(struct cg_ctx_t *cg, float x1, float y1, float x2, float y2, 
                         float thickness, uint32_t color);
static uint32_t ColorFromGC(GC gc);

/* ============================================================================
 * Glyph Hash Functions
 * ============================================================================
 */

static unsigned int
GlyphHash(int codepoint)
{
    return (unsigned int)(codepoint) % GLYPH_CACHE_SIZE;
}

/* ============================================================================
 * Tk Platform Font Functions
 * ============================================================================
 */

/*----------------------------------------------------------------------
 * TkpFontPkgInit --
 *
 *   Initialize the font package for Wayland/GLFW.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Fontconfig is initialized, emoji font is loaded, and standard
 *   Tk named fonts are created.
 *----------------------------------------------------------------------*/
void
TkpFontPkgInit(TkMainInfo *mainPtr)
{
    Tcl_Interp *interp = mainPtr->interp;

    if (!fcInitialized) {
        FcInit();
        fcInitialized = 1;
        FONT_DEBUG("Fontconfig initialized\n");
    }

    /* Initialize emoji font from bundled data */
    if (!emojiInitialized) {
        emojiFontData = (unsigned char *)NotoEmoji_Regular_ttf;
        if (stbtt_InitFont(&emojiInfo, emojiFontData, 
                           stbtt_GetFontOffsetForIndex(emojiFontData, 0))) {
            emojiInitialized = 1;
            FONT_DEBUG("Emoji font initialized\n");
        }
    }

    /* Register standard Tk named fonts */
    static const struct {
        const char *tkName;
        const char *family;
        int         points;
        int         bold;
        int         italic;
    } namedFonts[] = {
        { "TkDefaultFont",       "DejaVu Sans",       10, 0, 0 },
        { "TkTextFont",          "DejaVu Sans",       10, 0, 0 },
        { "TkFixedFont",         "DejaVu Sans Mono",  10, 0, 0 },
        { "TkHeadingFont",       "DejaVu Sans",       10, 1, 0 },
        { "TkCaptionFont",       "DejaVu Sans",       12, 1, 0 },
        { "TkSmallCaptionFont",  "DejaVu Sans",        8, 0, 0 },
        { "TkIconFont",          "DejaVu Sans",       10, 0, 0 },
        { "TkMenuFont",          "DejaVu Sans",       10, 0, 0 },
        { "TkTooltipFont",       "DejaVu Sans",        9, 0, 0 },
        { NULL, NULL, 0, 0, 0 }
    };

    int i;
    for (i = 0; namedFonts[i].tkName != NULL; i++) {
        Tcl_Obj *cmd = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj("font", -1));
        Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj("create", -1));
        Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj(namedFonts[i].tkName, -1));
        Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj("-family", -1));
        Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj(namedFonts[i].family, -1));
        Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj("-size", -1));
        Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewIntObj(namedFonts[i].points));
        if (namedFonts[i].bold) {
            Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj("-weight", -1));
            Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj("bold", -1));
        }
        if (namedFonts[i].italic) {
            Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj("-slant", -1));
            Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj("italic", -1));
        }

        Tcl_IncrRefCount(cmd);
        Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL);
        Tcl_DecrRefCount(cmd);
        Tcl_ResetResult(interp);
    }
    
    FONT_DEBUG("Font package initialized\n");
}

/*----------------------------------------------------------------------
 * TkpGetNativeFont --
 *
 *   Get a native font by name.
 *
 * Results:
 *   Returns a TkFont pointer for the named font.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------*/
TkFont *
TkpGetNativeFont(Tk_Window tkwin, const char *name)
{
    TkFontAttributes fa;
    TkInitFontAttributes(&fa);
    fa.family = Tk_GetUid(name);
    fa.size   = -12.0;
    fa.weight = TK_FW_NORMAL;
    fa.slant  = TK_FS_ROMAN;

    return TkpGetFontFromAttributes(NULL, tkwin, &fa);
}

/*----------------------------------------------------------------------
 * TkpGetFontFromAttributes --
 *
 *   Create or update a font from the given attributes.
 *
 * Results:
 *   Returns a TkFont pointer for the font.
 *
 * Side effects:
 *   Font data is loaded and cached.
 *----------------------------------------------------------------------*/
TkFont *
TkpGetFontFromAttributes(
    TkFont *tkFontPtr,
    Tk_Window tkwin,
    const TkFontAttributes *faPtr)
{
    WaylandFont *fontPtr;
    Screen *screen = Tk_Screen(tkwin);

    FONT_DEBUG("Getting font: family=%s, size=%f\n", 
               faPtr->family ? faPtr->family : "(null)", faPtr->size);

    if (tkFontPtr == NULL) {
        fontPtr = (WaylandFont *) Tcl_Alloc(sizeof(WaylandFont));
        memset(fontPtr, 0, sizeof(WaylandFont));
        
        /* Initialize the TkFont portion properly */
        fontPtr->font.fa = *faPtr;
        fontPtr->font.fm.ascent = 0;
        fontPtr->font.fm.descent = 0;
        fontPtr->font.fm.maxWidth = 0;
        fontPtr->font.fm.fixed = 0;
        fontPtr->font.objRefCount = 0;
        fontPtr->font.resourceRefCount = 1;
        fontPtr->font.cacheHashPtr = NULL;
        fontPtr->font.namedHashPtr = NULL;
        fontPtr->font.screen = screen;
        fontPtr->font.tabWidth = 0;
        fontPtr->font.underlinePos = 0;
        fontPtr->font.underlineHeight = 0;
        fontPtr->font.fid = None;
        fontPtr->font.colormap = None;
        fontPtr->font.visual = NULL;
        fontPtr->font.nextPtr = NULL;
    } else {
        fontPtr = (WaylandFont *) tkFontPtr;
        DeleteFont(fontPtr);
        memset(fontPtr, 0, sizeof(WaylandFont));
        
        /* Re-initialize the TkFont portion */
        fontPtr->font.fa = *faPtr;
        fontPtr->font.fm.ascent = 0;
        fontPtr->font.fm.descent = 0;
        fontPtr->font.fm.maxWidth = 0;
        fontPtr->font.fm.fixed = 0;
        fontPtr->font.objRefCount = 0;
        fontPtr->font.resourceRefCount = 1;
        fontPtr->font.cacheHashPtr = NULL;
        fontPtr->font.namedHashPtr = NULL;
        fontPtr->font.screen = screen;
        fontPtr->font.tabWidth = 0;
        fontPtr->font.underlinePos = 0;
        fontPtr->font.underlineHeight = 0;
        fontPtr->font.fid = None;
        fontPtr->font.colormap = None;
        fontPtr->font.visual = NULL;
        fontPtr->font.nextPtr = NULL;
    }

    InitFont(tkwin, faPtr, fontPtr);
    return (TkFont *) fontPtr;
}

/*----------------------------------------------------------------------
 * TkpDeleteFont --
 *
 *   Delete a font and free all associated resources.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Font data and glyph cache are freed.
 *----------------------------------------------------------------------*/
void
TkpDeleteFont(TkFont *tkFontPtr)
{
    WaylandFont *fontPtr = (WaylandFont *) tkFontPtr;
    
    if (fontPtr) {
        FONT_DEBUG("Deleting font\n");
        DeleteFont(fontPtr);
    }
}

/*----------------------------------------------------------------------
 * TkpGetFontFamilies --
 *
 *   Get a list of available font families.
 *
 * Results:
 *   Returns the list of font families as a Tcl result.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------*/
void
TkpGetFontFamilies(Tcl_Interp *interp, TCL_UNUSED(Tk_Window))
{
    Tcl_Obj    *resultPtr = Tcl_NewListObj(0, NULL);
    FcPattern  *pat       = FcPatternCreate();
    FcObjectSet *os       = FcObjectSetBuild(FC_FAMILY, NULL);
    FcFontSet  *fs        = FcFontList(NULL, pat, os);

    if (fs) {
        Tcl_Obj *seen = Tcl_NewDictObj();
        int i;
        for (i = 0; i < fs->nfont; i++) {
            FcChar8 *family = NULL;
            if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &family) == FcResultMatch) {
                Tcl_Obj *key = Tcl_NewStringObj((char *) family, -1);
                Tcl_Obj *val;
                if (Tcl_DictObjGet(NULL, seen, key, &val) == TCL_OK && val == NULL) {
                    Tcl_DictObjPut(NULL, seen, key, Tcl_NewIntObj(1));
                    Tcl_ListObjAppendElement(NULL, resultPtr, key);
                } else {
                    Tcl_DecrRefCount(key);
                }
            }
        }
        Tcl_DecrRefCount(seen);
        FcFontSetDestroy(fs);
    }

    FcObjectSetDestroy(os);
    FcPatternDestroy(pat);
    Tcl_SetObjResult(interp, resultPtr);
}

/*----------------------------------------------------------------------
 * TkpGetSubFonts --
 *
 *   Get the sub-fonts (family) for a given font.
 *
 * Results:
 *   Returns the font family as a Tcl list.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------*/
void
TkpGetSubFonts(Tcl_Interp *interp, Tk_Font tkfont)
{
    WaylandFont *fontPtr   = (WaylandFont *) tkfont;
    Tcl_Obj     *resultPtr = Tcl_NewListObj(0, NULL);

    if (fontPtr->font.fa.family) {
        Tcl_ListObjAppendElement(NULL, resultPtr,
            Tcl_NewStringObj(fontPtr->font.fa.family, -1));
    }
    Tcl_SetObjResult(interp, resultPtr);
}

/*----------------------------------------------------------------------
 * TkpGetFontAttrsForChar --
 *
 *   Get font attributes suitable for rendering a specific character.
 *
 * Results:
 *   Returns font attributes in faPtr.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------*/
void
TkpGetFontAttrsForChar(
    TCL_UNUSED(Tk_Window),
    Tk_Font tkfont,
    int c,
    TkFontAttributes *faPtr)
{
    WaylandFont *fontPtr = (WaylandFont *) tkfont;
    *faPtr = fontPtr->font.fa;

    /* Use Fontconfig to find a font that supports this character */
    FcCharSet *cs  = FcCharSetCreate();
    FcCharSetAddChar(cs, (FcChar32) c);

    FcPattern *pat = FcPatternCreate();
    FcPatternAddCharSet(pat, FC_CHARSET, cs);
    if (fontPtr->font.fa.family) {
        FcPatternAddString(pat, FC_FAMILY, (FcChar8 *) fontPtr->font.fa.family);
    }
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result;
    FcPattern *match = FcFontMatch(NULL, pat, &result);
    if (match) {
        FcChar8 *family = NULL;
        if (FcPatternGetString(match, FC_FAMILY, 0, &family) == FcResultMatch && family) {
            faPtr->family = Tk_GetUid((char *) family);
        }
        FcPatternDestroy(match);
    }

    FcPatternDestroy(pat);
    FcCharSetDestroy(cs);
}

/* ============================================================================
 * Text Measurement Functions
 * ============================================================================
 */

/*----------------------------------------------------------------------
 * Tk_MeasureChars --
 *
 *   Measure the width of characters from a string.
 *
 * Results:
 *   Returns the number of bytes measured, and sets *lengthPtr to the
 *   pixel width.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------*/
int
Tk_MeasureChars(
    Tk_Font tkfont,
    const char *source,
    Tcl_Size numBytes,
    int maxLength,
    int flags,
    int *lengthPtr)
{
    return Tk_MeasureCharsInContext(tkfont, source, numBytes,
        0, numBytes, maxLength, flags, lengthPtr);
}

/*----------------------------------------------------------------------
 * Tk_MeasureCharsInContext --
 *
 *   Measure the width of a substring of characters.
 *
 * Results:
 *   Returns the number of bytes measured, and sets *lengthPtr to the
 *   pixel width.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------*/
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
    WaylandFont *fontPtr = (WaylandFont *) tkfont;
    int totalWidth = 0;
    int bytesMeasured = 0;
    int lastBreakWidth = 0;
    int lastBreakBytes = 0;

    if (rangeStart < 0 || rangeLength <= 0 ||
            rangeStart + rangeLength > numBytes) {
        *lengthPtr = 0;
        return 0;
    }

    if (!EnsureFontLoaded(fontPtr)) {
        FONT_DEBUG("Failed to load font for measurement\n");
        *lengthPtr = 0;
        return 0;
    }

    const char *text = source + rangeStart;
    size_t len = rangeLength;
    
    /* Simple character-by-character measurement */
    for (size_t i = 0; i < len; ) {
        int codepoint;
        int bytes = Tcl_UtfToUniChar(text + i, &codepoint);
        
        if (bytes <= 0) {
            break;
        }
        
        /* Get glyph cache entry which includes advance */
        GlyphCacheEntry *glyphEntry = NULL;
        int advance;
        
        if (GetGlyphBitmap(fontPtr, codepoint, &glyphEntry) && glyphEntry) {
            advance = glyphEntry->advance;
            FONT_DEBUG("Char %c (0x%x) advance=%d\n", codepoint, codepoint, advance);
        } else {
            advance = fontPtr->pixelSize / 2;
            FONT_DEBUG("Char %c (0x%x) not found, using advance=%d\n", codepoint, codepoint, advance);
        }
        
        /* Check if we exceed maxLength */
        if (maxLength >= 0 && totalWidth + advance > maxLength) {
            if ((flags & TK_WHOLE_WORDS) && lastBreakBytes > 0) {
                *lengthPtr = lastBreakWidth;
                return lastBreakBytes;
            }
            if (!(flags & TK_PARTIAL_OK)) {
                break;
            }
        }
        
        /* Check for word boundaries (space or punctuation) */
        if ((flags & TK_WHOLE_WORDS) && (codepoint == ' ' || codepoint == '\t')) {
            lastBreakBytes = bytesMeasured + bytes;
            lastBreakWidth = totalWidth + advance;
        }
        
        totalWidth += advance;
        bytesMeasured += bytes;
        i += bytes;
    }
    
    if ((flags & TK_AT_LEAST_ONE) && bytesMeasured == 0 && rangeLength > 0) {
        /* Need at least one character - measure the first one */
        int codepoint;
        int bytes = Tcl_UtfToUniChar(text, &codepoint);
        GlyphCacheEntry *glyphEntry = NULL;
        int advance;
        
        if (GetGlyphBitmap(fontPtr, codepoint, &glyphEntry) && glyphEntry) {
            advance = glyphEntry->advance;
        } else {
            advance = fontPtr->pixelSize / 2;
        }
        
        totalWidth = advance;
        bytesMeasured = bytes;
    }
    
    *lengthPtr = totalWidth;
    return bytesMeasured;
}

/* ============================================================================
 * Text Drawing Functions
 * ============================================================================
 */

/*----------------------------------------------------------------------
 * Tk_DrawChars --
 *
 *   Draw a string of characters at the given coordinates.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Text is drawn to the drawable.
 *----------------------------------------------------------------------*/
void
Tk_DrawChars(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Drawable),
    GC gc,
    Tk_Font tkfont,
    const char *source,
    Tcl_Size numBytes,
    int x,
    int y)
{
    FONT_DEBUG("Tk_DrawChars: '%s' at (%d,%d)\n", source, x, y);
    TkpDrawAngledCharsInContext(NULL, 0, gc, tkfont,
        source, numBytes, 0, numBytes,
        (double) x, (double) y, 0.0);
}

/*----------------------------------------------------------------------
 * TkDrawAngledChars --
 *
 *   Draw a string of characters at an angle.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Text is drawn to the drawable.
 *----------------------------------------------------------------------*/
void
TkDrawAngledChars(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Drawable),
    GC gc,
    Tk_Font tkfont,
    const char *source,
    Tcl_Size numBytes,
    double x,
    double y,
    double angle)
{
    TkpDrawAngledCharsInContext(NULL, 0, gc, tkfont,
        source, numBytes, 0, numBytes,
        x, y, angle);
}

/*----------------------------------------------------------------------
 * Tk_DrawCharsInContext --
 *
 *   Draw a substring of characters with context.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Text is drawn to the drawable.
 *----------------------------------------------------------------------*/
void
Tk_DrawCharsInContext(
    TCL_UNUSED(Display *),
    Drawable drawable,
    GC gc,
    Tk_Font tkfont,
    const char *source,
    Tcl_Size numBytes,
    Tcl_Size rangeStart,
    Tcl_Size rangeLength,
    int x,
    int y)
{
    TkpDrawAngledCharsInContext(NULL, drawable, gc, tkfont,
        source, numBytes, rangeStart, rangeLength,
        (double)x, (double)y, 0.0);
}

/*----------------------------------------------------------------------
 * TkpDrawCharsInContext --
 *
 *   Draw a substring of characters (X11 compatibility).
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Text is drawn to the drawable.
 *----------------------------------------------------------------------*/
void
TkpDrawCharsInContext(
    Display *display,
    Drawable drawable,
    GC gc,
    Tk_Font tkfont,
    const char *source,
    Tcl_Size numBytes,
    int rangeStart,
    Tcl_Size rangeLength,
    int x,
    int y)
{
    Tk_DrawCharsInContext(display, drawable, gc, tkfont, source, numBytes,
        rangeStart, rangeLength, x, y);
}

/*----------------------------------------------------------------------
 * TkpMeasureCharsInContext --
 *
 *   Measure a substring of characters (X11 compatibility).
 *
 * Results:
 *   Returns the number of bytes measured.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------*/
int
TkpMeasureCharsInContext(
    Tk_Font tkfont,
    const char *source,
    Tcl_Size numBytes,
    int rangeStart,
    Tcl_Size rangeLength,
    int maxLength,
    int flags,
    int *lengthPtr)
{
    if (rangeStart < 0) rangeStart = 0;
    if (rangeStart + rangeLength > numBytes)
        rangeLength = numBytes - rangeStart;
    return Tk_MeasureCharsInContext(tkfont, source, numBytes,
        rangeStart, rangeLength, maxLength, flags, lengthPtr);
}

/* ============================================================================
 * Main Text Rendering Function with Direct Drawing
 * ============================================================================
 */

/*----------------------------------------------------------------------
 * TkpDrawAngledCharsInContext --
 *
 *   Draw a substring of characters at an angle using direct rendering.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Text is drawn to the drawable.
 *----------------------------------------------------------------------*/
void
TkpDrawAngledCharsInContext(
    TCL_UNUSED(Display *),
    Drawable drawable,
    GC gc,
    Tk_Font tkfont,
    const char *source,
    Tcl_Size numBytes,
    Tcl_Size rangeStart,
    Tcl_Size rangeLength,
    double x,
    double y,
    double angle)
{
    WaylandFont *fontPtr = (WaylandFont *) tkfont;
    uint32_t color = ColorFromGC(gc);
    TkWaylandDrawingContext dc;
    struct cg_ctx_t *cg;

    if (rangeStart < 0 || rangeLength <= 0 ||
            rangeStart + rangeLength > numBytes) {
        return;
    }

    FONT_DEBUG("Drawing text: '%.*s' at (%.1f,%.1f), color=0x%08x\n", 
               (int)rangeLength, source + rangeStart, x, y, color);

    /* Begin drawing - works for both window (direct) and pixmap (off-screen) */
    if (TkGlfwBeginDraw(drawable, gc, &dc) != TCL_OK) {
        FONT_DEBUG("Failed to begin drawing\n");
        return;
    }
    cg = dc.cg;

    /* Ensure font is loaded */
    if (!EnsureFontLoaded(fontPtr)) {
        FONT_DEBUG("Failed to load font\n");
        TkGlfwEndDraw(&dc);
        return;
    }

    const char *text = source + rangeStart;
    size_t len = rangeLength;
    
    FONT_DEBUG("Font loaded: pixelSize=%d, ascent=%d, descent=%d\n",
               fontPtr->pixelSize, fontPtr->font.fm.ascent, fontPtr->font.fm.descent);
    
    /* Handle rotation by using libcg matrix transforms */
    float drawX = (float)x;
    float drawY = (float)y;
    
    if (angle != 0.0) {
        cg_save(cg);
        cg_translate(cg, drawX, drawY);
        cg_rotate(cg, (float)(angle * M_PI / 180.0));
        cg_translate(cg, -drawX, -drawY);
    }

    /* Draw each character directly */
    for (size_t i = 0; i < len; ) {
        int codepoint;
        int bytes = Tcl_UtfToUniChar(text + i, &codepoint);
        
        if (bytes <= 0) {
            break;
        }
        
        /* Get the rendered glyph bitmap from cache */
        GlyphCacheEntry *glyphEntry = NULL;
        if (GetGlyphBitmap(fontPtr, codepoint, &glyphEntry) && glyphEntry && glyphEntry->bitmap) {
            /* Set color for this glyph */
            struct cg_color_t cg_color;
            cg_color.r = ((color >> 24) & 0xFF) / 255.0f;
            cg_color.g = ((color >> 16) & 0xFF) / 255.0f;
            cg_color.b = ((color >> 8) & 0xFF) / 255.0f;
            cg_color.a = (color & 0xFF) / 255.0f;
            cg_set_source_rgba(cg, cg_color.r, cg_color.g, cg_color.b, cg_color.a);
            
            /* Draw the glyph */
            float glyphX = drawX + glyphEntry->bearing_x;
            float glyphY = drawY - glyphEntry->bearing_y;
            
            FONT_DEBUG("Drawing glyph for 0x%x at (%.1f,%.1f), size=%dx%d, advance=%d\n",
                       codepoint, glyphX, glyphY, glyphEntry->width, glyphEntry->height, glyphEntry->advance);
            
            DrawGlyphDirect(cg, fontPtr, glyphEntry, glyphX, glyphY, color);
            
            /* Advance cursor by the glyph's advance */
            drawX += glyphEntry->advance;
        } else {
            /* Missing glyph - draw a placeholder rectangle */
            float boxWidth = fontPtr->pixelSize / 2;
            struct cg_color_t cg_color;
            cg_color.r = ((color >> 24) & 0xFF) / 255.0f;
            cg_color.g = ((color >> 16) & 0xFF) / 255.0f;
            cg_color.b = ((color >> 8) & 0xFF) / 255.0f;
            cg_color.a = (color & 0xFF) / 255.0f;
            cg_set_source_rgba(cg, cg_color.r, cg_color.g, cg_color.b, cg_color.a);
            cg_rectangle(cg, drawX, drawY - fontPtr->pixelSize,
                        boxWidth, fontPtr->pixelSize);
            cg_fill(cg);
            FONT_DEBUG("Missing glyph for 0x%x, drawing placeholder at (%.1f,%.1f)\n",
                       codepoint, drawX, drawY - fontPtr->pixelSize);
            drawX += boxWidth;
        }
        
        i += bytes;
    }

    /* Draw underline and overstrike if needed */
    if (fontPtr->font.fa.underline || fontPtr->font.fa.overstrike) {
        float runWidth = drawX - (float)x;
        struct cg_color_t cg_color;
        cg_color.r = ((color >> 24) & 0xFF) / 255.0f;
        cg_color.g = ((color >> 16) & 0xFF) / 255.0f;
        cg_color.b = ((color >> 8) & 0xFF) / 255.0f;
        cg_color.a = (color & 0xFF) / 255.0f;
        cg_set_source_rgba(cg, cg_color.r, cg_color.g, cg_color.b, cg_color.a);
        
        if (fontPtr->font.fa.underline) {
            float uy = (float)(y + fontPtr->underlinePos);
            cg_set_line_width(cg, (float)fontPtr->barHeight);
            cg_move_to(cg, (float)x, uy);
            cg_line_to(cg, (float)x + runWidth, uy);
            cg_stroke(cg);
        }
        if (fontPtr->font.fa.overstrike) {
            float oy = (float)(y - fontPtr->font.fm.ascent / 2);
            cg_set_line_width(cg, (float)fontPtr->barHeight);
            cg_move_to(cg, (float)x, oy);
            cg_line_to(cg, (float)x + runWidth, oy);
            cg_stroke(cg);
        }
    }

    if (angle != 0.0) {
        cg_restore(cg);
    }

    TkGlfwEndDraw(&dc);
    FONT_DEBUG("Text drawing complete\n");
}

/* ============================================================================
 * Font Loading and Glyph Rendering
 * ============================================================================
 */

/*----------------------------------------------------------------------
 * FindFontFile --
 *
 *   Use Fontconfig to find a font file matching the given criteria.
 *
 * Results:
 *   Returns a newly allocated string containing the font file path,
 *   or NULL if not found.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------*/
static char *
FindFontFile(const char *family, int bold, int italic, int pixelSize)
{
    FcPattern *pat = FcPatternCreate();
    if (!pat) return NULL;

    if (family) {
        FcPatternAddString(pat, FC_FAMILY, (FcChar8 *) family);
        FONT_DEBUG("Looking for font family: %s\n", family);
    }
    FcPatternAddInteger(pat, FC_WEIGHT,
                        bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pat, FC_SLANT,
                        italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    if (pixelSize > 0) {
        FcPatternAddDouble(pat, FC_PIXEL_SIZE, (double) pixelSize);
    }
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result;
    FcPattern *match = FcFontMatch(NULL, pat, &result);
    char *path = NULL;

    if (match) {
        FcChar8 *fcPath = NULL;
        if (FcPatternGetString(match, FC_FILE, 0, &fcPath) == FcResultMatch && fcPath) {
            path = strdup((char *) fcPath);
            FONT_DEBUG("Found font file: %s\n", path);
        }
        FcPatternDestroy(match);
    } else {
        FONT_DEBUG("No font found for family: %s\n", family ? family : "(null)");
    }

    FcPatternDestroy(pat);
    return path;
}

/*----------------------------------------------------------------------
 * InitFont --
 *
 *   Initialize a font structure with the given attributes.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Font attributes are set, and a file path is resolved.
 *----------------------------------------------------------------------*/
static void
InitFont(Tk_Window tkwin, const TkFontAttributes *faPtr, WaylandFont *fontPtr)
{
    double ptSize = faPtr->size;
    
    /* Store the font attributes - they should already be set, but ensure */
    fontPtr->font.fa = *faPtr;
    
    /* Calculate pixel size */
    if (ptSize < 0.0) {
        fontPtr->pixelSize = (int)(-ptSize + 0.5);
    } else if (ptSize > 0.0) {
        fontPtr->pixelSize = (int)(TkFontGetPoints(tkwin, ptSize) + 0.5);
    } else {
        fontPtr->pixelSize = 12;
    }
    if (fontPtr->pixelSize < 1) fontPtr->pixelSize = 1;

    int bold = (faPtr->weight == TK_FW_BOLD);
    int italic = (faPtr->slant == TK_FS_ITALIC);

    /* Find the font file, but don't load it yet - lazy loading */
    if (fontPtr->filePath) {
        free(fontPtr->filePath);
        fontPtr->filePath = NULL;
    }
    
    fontPtr->filePath = FindFontFile(faPtr->family, bold, italic, fontPtr->pixelSize);
    
    /* Don't load font data here - let EnsureFontLoaded do it lazily */
    if (fontPtr->fontData) {
        Tcl_Free((char *)fontPtr->fontData);
        fontPtr->fontData = NULL;
    }
    
    /* Initialize glyph cache to NULL */
    for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
        fontPtr->glyphCache[i] = NULL;
    }
    
    FONT_DEBUG("InitFont: pixelSize=%d, filePath=%s\n", 
               fontPtr->pixelSize, fontPtr->filePath ? fontPtr->filePath : "(none)");
}

/*----------------------------------------------------------------------
 * DeleteFont --
 *
 *   Free all resources associated with a font.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Font data and glyph cache are freed.
 *----------------------------------------------------------------------*/
static void
DeleteFont(WaylandFont *fontPtr)
{
    if (fontPtr->filePath) {
        free(fontPtr->filePath);
        fontPtr->filePath = NULL;
    }
    if (fontPtr->fontData) {
        Tcl_Free((char *)fontPtr->fontData);
        fontPtr->fontData = NULL;
    }
    
    /* Free glyph cache */
    for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
        GlyphCacheEntry *entry = fontPtr->glyphCache[i];
        while (entry) {
            GlyphCacheEntry *next = entry->next;
            if (entry->bitmap) {
                Tcl_Free((char *)entry->bitmap);
            }
            Tcl_Free((char *)entry);
            entry = next;
        }
        fontPtr->glyphCache[i] = NULL;
    }
}

/*----------------------------------------------------------------------
 * EnsureFontLoaded --
 *
 *   Ensure the font data is loaded and metrics are computed.
 *
 * Results:
 *   Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *   Font data is loaded and metrics are set.
 *----------------------------------------------------------------------*/
static int
EnsureFontLoaded(WaylandFont *fontPtr)
{
    if (fontPtr->fontData != NULL) {
        return 1;
    }

    if (!fontPtr->filePath) {
        /* Try a fallback font */
        const char *fallback = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
        fontPtr->filePath = strdup(fallback);
        FONT_DEBUG("Using fallback font: %s\n", fallback);
    }

    FONT_DEBUG("Loading font from: %s\n", fontPtr->filePath);
    
    FILE *fd = fopen(fontPtr->filePath, "rb");
    if (!fd) {
        FONT_DEBUG("Failed to open font file: %s\n", fontPtr->filePath);
        return 0;
    }

    fseek(fd, 0, SEEK_END);
    long sz = ftell(fd);
    fseek(fd, 0, SEEK_SET);

    fontPtr->fontData = (unsigned char *) Tcl_Alloc((int) sz);
    if (!fontPtr->fontData) {
        fclose(fd);
        return 0;
    }

    if ((long) fread(fontPtr->fontData, 1, sz, fd) != sz) {
        Tcl_Free((char *)fontPtr->fontData);
        fontPtr->fontData = NULL;
        fclose(fd);
        return 0;
    }
    fclose(fd);

    if (!stbtt_InitFont(&fontPtr->stbInfo, fontPtr->fontData,
                        stbtt_GetFontOffsetForIndex(fontPtr->fontData, 0))) {
        FONT_DEBUG("Failed to initialize stb_truetype font\n");
        Tcl_Free((char *)fontPtr->fontData);
        fontPtr->fontData = NULL;
        return 0;
    }

    /* Compute scale and metrics */
    fontPtr->scale = stbtt_ScaleForPixelHeight(&fontPtr->stbInfo, 
                                                (float)fontPtr->pixelSize);
    
    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&fontPtr->stbInfo, &ascent, &descent, &linegap);
    fontPtr->font.fm.ascent  = (int)(ascent * fontPtr->scale + 0.5f);
    fontPtr->font.fm.descent = (int)(-descent * fontPtr->scale + 0.5f);
   
    int adv_W, adv_dot, lsb;
    stbtt_GetCodepointHMetrics(&fontPtr->stbInfo, 'W', &adv_W, &lsb);
    stbtt_GetCodepointHMetrics(&fontPtr->stbInfo, '.', &adv_dot, &lsb);
    fontPtr->font.fm.maxWidth = (int)(adv_W * fontPtr->scale + 0.5f);
    fontPtr->font.fm.fixed = (adv_W == adv_dot);

    /* Set underline fields that Tk expects in the TkFont structure */
    fontPtr->underlinePos = fontPtr->font.fm.descent / 2;
    if (fontPtr->underlinePos < 1) fontPtr->underlinePos = 1;
    fontPtr->barHeight = (int)(fontPtr->pixelSize * 0.07 + 0.5);
    if (fontPtr->barHeight < 1) fontPtr->barHeight = 1;
    
    /* Store underline info in the TkFont structure as well */
    fontPtr->font.underlinePos = fontPtr->underlinePos;
    fontPtr->font.underlineHeight = fontPtr->barHeight;
    
    /* Set tab width to 8 spaces (typical default) */
    fontPtr->font.tabWidth = fontPtr->font.fm.maxWidth * 8;
    
    FONT_DEBUG("Font loaded: ascent=%d, descent=%d, maxWidth=%d, scale=%f\n",
               fontPtr->font.fm.ascent, fontPtr->font.fm.descent,
               fontPtr->font.fm.maxWidth, fontPtr->scale);

    return 1;
}

/*----------------------------------------------------------------------
 * GetGlyphBitmap --
 *
 *   Get a cached glyph bitmap for the given codepoint.
 *
 * Results:
 *   Returns 1 on success, 0 on failure, and sets entryOut.
 *
 * Side effects:
 *   May render and cache a new glyph.
 *----------------------------------------------------------------------*/
static int
GetGlyphBitmap(WaylandFont *fontPtr, int codepoint, GlyphCacheEntry **entryOut)
{
    unsigned int hash = GlyphHash(codepoint);
    GlyphCacheEntry *entry = fontPtr->glyphCache[hash];
    
    /* Search cache */
    while (entry) {
        if (entry->codepoint == codepoint) {
            *entryOut = entry;
            return 1;
        }
        entry = entry->next;
    }
    
    /* Not found - create new entry */
    entry = (GlyphCacheEntry *)Tcl_Alloc(sizeof(GlyphCacheEntry));
    if (!entry) return 0;
    
    memset(entry, 0, sizeof(GlyphCacheEntry));
    entry->codepoint = codepoint;
    
    /* Render glyph to bitmap */
    RenderGlyphToBitmap(fontPtr, codepoint, entry);
    
    /* Add to cache */
    entry->next = fontPtr->glyphCache[hash];
    fontPtr->glyphCache[hash] = entry;
    
    *entryOut = entry;
    return 1;
}

/*----------------------------------------------------------------------
 * RenderGlyphToBitmap --
 *
 *   Render a glyph for the given codepoint to a bitmap.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   The entry's bitmap and metrics are set.
 *----------------------------------------------------------------------*/
static void
RenderGlyphToBitmap(WaylandFont *fontPtr, int codepoint, GlyphCacheEntry *entry)
{
    stbtt_fontinfo *info = &fontPtr->stbInfo;
    float scale = fontPtr->scale;
    
    /* Try to get glyph from main font */
    int glyph = stbtt_FindGlyphIndex(info, codepoint);
    stbtt_fontinfo *renderInfo = info;
    float renderScale = scale;
    
    /* If not found in main font, try emoji font */
    if (glyph == 0 && emojiInitialized) {
        glyph = stbtt_FindGlyphIndex(&emojiInfo, codepoint);
        if (glyph != 0) {
            renderInfo = &emojiInfo;
            renderScale = stbtt_ScaleForPixelHeight(renderInfo,
                                                      (float)fontPtr->pixelSize);
        }
    }
    
    if (glyph == 0) {
        /* Missing glyph - create placeholder box metrics */
        entry->width = fontPtr->pixelSize / 2;
        entry->height = fontPtr->pixelSize;
        entry->bearing_x = 0;
        entry->bearing_y = fontPtr->pixelSize;
        entry->advance = entry->width;
        entry->bitmap = NULL;
        FONT_DEBUG("Missing glyph for codepoint 0x%x\n", codepoint);
        return;
    }
    
    /* Get glyph metrics */
    int advance, lsb;
    stbtt_GetGlyphHMetrics(renderInfo, glyph, &advance, &lsb);
    entry->advance = (int)(advance * renderScale + 0.5f);
    
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(renderInfo, glyph, renderScale, renderScale, &x0, &y0, &x1, &y1);
    
    entry->width = x1 - x0;
    entry->height = y1 - y0;
    entry->bearing_x = (int)(lsb * renderScale);
    entry->bearing_y = -y0;
    
    if (entry->width <= 0 || entry->height <= 0) {
        /* Empty glyph (space, etc.) - just store advance */
        entry->width = 0;
        entry->height = 0;
        entry->bitmap = NULL;
        FONT_DEBUG("Empty glyph for codepoint 0x%x, advance=%d\n", codepoint, entry->advance);
        return;
    }
    
    /* Render glyph to bitmap */
    entry->bitmap = (unsigned char *)Tcl_Alloc(entry->width * entry->height);
    if (entry->bitmap) {
        stbtt_MakeGlyphBitmap(renderInfo, entry->bitmap, entry->width, entry->height,
                              entry->width, renderScale, renderScale, glyph);
        FONT_DEBUG("Rendered glyph for codepoint 0x%x: size=%dx%d, advance=%d, bearing=(%d,%d)\n",
                   codepoint, entry->width, entry->height, entry->advance,
                   entry->bearing_x, entry->bearing_y);
    }
}

/*----------------------------------------------------------------------
 * DrawGlyphDirect --
 *
 *   Draw a glyph bitmap directly to the drawing context.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Glyph is drawn to the context.
 *----------------------------------------------------------------------*/
static void
DrawGlyphDirect(
    struct cg_ctx_t *cg,
    WaylandFont *fontPtr,
    GlyphCacheEntry *glyph,
    float x,
    float y,
    uint32_t color)
{
    if (!glyph || glyph->width <= 0 || glyph->height <= 0 || !glyph->bitmap) {
        return;
    }
    
    /* Create a temporary RGBA surface from the alpha bitmap */
    unsigned char *rgba = (unsigned char *)Tcl_Alloc(glyph->width * glyph->height * 4);
    if (!rgba) return;
    
    unsigned char r = ((color >> 24) & 0xFF);
    unsigned char g = ((color >> 16) & 0xFF);
    unsigned char b = ((color >> 8) & 0xFF);
    unsigned char a = (color & 0xFF);
    
    /* Convert alpha bitmap to RGBA with the specified color */
    for (int i = 0; i < glyph->width * glyph->height; i++) {
        unsigned char alpha = glyph->bitmap[i];
        unsigned char final_alpha = (alpha * a) / 255;
        rgba[i*4]   = r;
        rgba[i*4+1] = g;
        rgba[i*4+2] = b;
        rgba[i*4+3] = final_alpha;
    }
    
    /* Create a libcg surface and blit it */
    struct cg_surface_t *glyphSurface = cg_surface_create_for_data(glyph->width, glyph->height, rgba);
    if (glyphSurface) {
        cg_set_source_surface(cg, glyphSurface, x, y);
        cg_rectangle(cg, x, y, (float)glyph->width, (float)glyph->height);
        cg_fill(cg);
        cg_surface_destroy(glyphSurface);
    }
    
    Tcl_Free((char *)rgba);
}

/*----------------------------------------------------------------------
 * DrawRectangle --
 *
 *   Draw a filled rectangle with the given color.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Rectangle is drawn to the context.
 *----------------------------------------------------------------------*/
static void
DrawRectangle(
    struct cg_ctx_t *cg,
    float x, float y, float w, float h,
    uint32_t color)
{
    struct cg_color_t cg_color;
    cg_color.r = ((color >> 24) & 0xFF) / 255.0f;
    cg_color.g = ((color >> 16) & 0xFF) / 255.0f;
    cg_color.b = ((color >> 8) & 0xFF) / 255.0f;
    cg_color.a = (color & 0xFF) / 255.0f;
    
    cg_set_source_rgba(cg, cg_color.r, cg_color.g, cg_color.b, cg_color.a);
    cg_rectangle(cg, x, y, w, h);
    cg_fill(cg);
}

/*----------------------------------------------------------------------
 * DrawLine --
 *
 *   Draw a line with the given thickness and color.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Line is drawn to the context.
 *----------------------------------------------------------------------*/
static void
DrawLine(
    struct cg_ctx_t *cg,
    float x1, float y1, float x2, float y2,
    float thickness,
    uint32_t color)
{
    struct cg_color_t cg_color;
    cg_color.r = ((color >> 24) & 0xFF) / 255.0f;
    cg_color.g = ((color >> 16) & 0xFF) / 255.0f;
    cg_color.b = ((color >> 8) & 0xFF) / 255.0f;
    cg_color.a = (color & 0xFF) / 255.0f;
    
    cg_set_source_rgba(cg, cg_color.r, cg_color.g, cg_color.b, cg_color.a);
    cg_set_line_width(cg, thickness);
    cg_move_to(cg, x1, y1);
    cg_line_to(cg, x2, y2);
    cg_stroke(cg);
}

/*----------------------------------------------------------------------
 * ColorFromGC --
 *
 *   Convert a GC to a 32-bit ARGB color.
 *
 * Results:
 *   Returns the color as a 32-bit ARGB value.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------*/
static uint32_t
ColorFromGC(GC gc)
{
    if (gc) {
        XGCValues vals;
        TkWaylandGetGCValues(gc, GCForeground, &vals);
        unsigned char r = (vals.foreground >> 16) & 0xFF;
        unsigned char g = (vals.foreground >> 8) & 0xFF;
        unsigned char b = vals.foreground & 0xFF;
        return (r << 24) | (g << 16) | (b << 8) | 0xFF;
    }
    return 0x000000FF;
}


/* ============================================================================
 * PostScript and Other Stubs
 * ============================================================================
 */

/*----------------------------------------------------------------------
 * TkPostscriptFontName --
 *
 *   Get the PostScript name for a font.
 *
 * Results:
 *   Returns 0, and appends the font name to dsPtr.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------*/
int
TkPostscriptFontName(Tk_Font tkfont, Tcl_DString *dsPtr)
{
    WaylandFont *fontPtr = (WaylandFont *) tkfont;
    const char *family = fontPtr->font.fa.family ? fontPtr->font.fa.family : "Helvetica";

    Tcl_DStringAppend(dsPtr, family, -1);
    if (fontPtr->font.fa.weight == TK_FW_BOLD) {
        Tcl_DStringAppend(dsPtr, "-Bold", -1);
    }
    if (fontPtr->font.fa.slant == TK_FS_ITALIC) {
        Tcl_DStringAppend(dsPtr, "-Italic", -1);
    }
    return 0;
}

/*----------------------------------------------------------------------
 * TkUnixSetXftClipRegion --
 *
 *   Set clipping region for Xft (no-op for Wayland).
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------*/
void
TkUnixSetXftClipRegion(TCL_UNUSED(Region))
{
    /* No-op: clipping handled through OpenGL scissor. */
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
