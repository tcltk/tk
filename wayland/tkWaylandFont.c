/*
 *
 * tkWaylandFont.c –
 *
 * This module implements the Wayland/GLFW platform-specific
 * features of fonts using stb_truetype.h directly.
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
#include <stb_truetype.h>
#include "noto_emoji.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Architecture note: this file now uses stb_truetype directly for font
 * rendering, eliminating the dependency on NanoVG. Each WaylandFont
 * maintains its own stbtt font info and a texture atlas for rendered
 * glyphs. When text is drawn, glyphs are rendered on demand into the
 * atlas and then drawn as textured quads.
 */

/* Maximum atlas size - could be made configurable */
#define ATLAS_WIDTH  1024
#define ATLAS_HEIGHT 1024
#define ATLAS_PADDING 2

/* Glyph cache entry */
typedef struct GlyphCacheEntry {
    int codepoint;              /* Unicode codepoint */
    int atlas_x;                /* X position in atlas */
    int atlas_y;                /* Y position in atlas */
    int width;                  /* Glyph width in pixels */
    int height;                 /* Glyph height in pixels */
    float s0, t0, s1, t1;       /* Texture coordinates */
    int advance;                /* Advance width in pixels */
    int bearing_x;              /* Left side bearing in pixels */
    int bearing_y;              /* Top side bearing (from baseline) */
    struct GlyphCacheEntry *next; /* Hash chain */
} GlyphCacheEntry;

/* Glyph cache hash table size */
#define GLYPH_CACHE_SIZE 1024

/*
 * Platform font structure:
 *
 * Extends the generic TkFont with stb_truetype data and glyph caching.
 */
typedef struct {
    TkFont      font;           /* Generic font data — MUST be first.        */
    char       *filePath;       /* Absolute path returned by Fontconfig.    */
    unsigned char *fontData;    /* Mapped font file data                     */
    stbtt_fontinfo stbInfo;     /* stb_truetype font info                    */
    float       scale;          /* Scale factor for pixel size               */
    
    /* Glyph atlas for this font */
    unsigned char *atlas;        /* RGBA texture atlas */
    int            atlas_width;  /* Current atlas width */
    int            atlas_height; /* Current atlas height */
    int            atlas_used_x; /* Current position in atlas */
    int            atlas_used_y;
    int            atlas_row_height;
    
    /* Glyph cache */
    GlyphCacheEntry *glyphCache[GLYPH_CACHE_SIZE];
    
    /* Metrics */
    int         pixelSize;      /* Resolved size in pixels.                  */
    int         underlinePos;   /* Pixels below baseline for underline.      */
    int         barHeight;      /* Thickness of under/overstrike bar.        */
    
    /* OpenGL texture ID for atlas */
    unsigned int textureId;
} WaylandFont;

/* Whether Fontconfig has been initialized for this process. */
static int fcInitialized = 0;

/* Emoji font data - loaded once and shared */
static stbtt_fontinfo emojiInfo;
static unsigned char *emojiFontData = NULL;
static int emojiInitialized = 0;

/* Forward declarations of file-local helpers. */
static char    *FindFontFile(
    const char *family,
    int bold,
    int italic,
    int pixelSize);
static void     InitFont(
    Tk_Window tkwin,
    const TkFontAttributes *fa,
    WaylandFont *fontPtr);
static void     DeleteFont(
    WaylandFont *fontPtr);
static int      EnsureFontLoaded(
    WaylandFont *fontPtr);
static int      GetGlyph(
    WaylandFont *fontPtr,
    int codepoint,
    GlyphCacheEntry **entryOut);
static void     RenderGlyphToAtlas(
    WaylandFont *fontPtr,
    int codepoint,
    GlyphCacheEntry *entry);
static void     EnsureAtlasTexture(
    WaylandFont *fontPtr);
static void     UploadAtlasToGPU(
    WaylandFont *fontPtr);
static uint32_t ColorFromGC(
    GC gc);
static void     DrawGlyphQuad(
    WaylandFont *fontPtr,
    GlyphCacheEntry *glyph,
    float x,
    float y,
    uint32_t color);

/*
 *---------------------------------------------------------------------------
 *
 * GlyphHash --
 *
 *	Hash function for glyph cache.
 *
 * Results:
 *	Returns hash index for given codepoint.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */
static unsigned int
GlyphHash(int codepoint)
{
    return (unsigned int)(codepoint) % GLYPH_CACHE_SIZE;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpFontPkgInit --
 *
 *	Initializes the platform font package for a new Tk application.
 *	Registers the standard Tk named fonts (TkDefaultFont, TkFixedFont,
 *	etc.) so that wish and other Tk applications can resolve them at
 *	startup without crashing.
 *
 *	Named fonts must be registered here, before any widget is created,
 *	because Tk's generic layer calls TkpFontPkgInit exactly once and
 *	then immediately tries to resolve TkDefaultFont for the root window.
 *	If the named fonts are absent at that point, Tk panics.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes Fontconfig; creates Tk named fonts via Tk_CreateFont.
 *
 *---------------------------------------------------------------------------
 */
void
TkpFontPkgInit(
    TkMainInfo *mainPtr)
{
    Tcl_Interp *interp = mainPtr->interp;

    if (!fcInitialized) {
        FcInit();
        fcInitialized = 1;
    }

    /* Initialize emoji font from bundled data */
    if (!emojiInitialized) {
        emojiFontData = (unsigned char *)NotoEmoji_Regular_ttf;
        if (stbtt_InitFont(&emojiInfo, emojiFontData, 
                           stbtt_GetFontOffsetForIndex(emojiFontData, 0))) {
            emojiInitialized = 1;
        }
    }

    /*
     * Register the standard Tk named fonts.
     *
     * Each entry is: { Tk name, FC family, size-in-points, bold, italic }.
     *
     * Sizes match the cross-platform defaults used on X11 and macOS so
     * that Tk's own test suite and typical application code see consistent
     * metrics.  We use point sizes (positive) here so that TkFontGetPoints
     * can apply any per-display DPI correction in InitFont.
     *
     * These calls go through Tcl_Eval so that the fonts are registered in
     * the interpreter's font table exactly as "font create" would do —
     * which is what Tk's generic layer expects to find.
     */
    static const struct {
        const char *tkName;     /* Name used by Tk internals & scripts.   */
        const char *family;     /* Fontconfig family preference.          */
        int         points;     /* Point size (positive).                 */
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
        /*
         * Build a "font create <name> -family ... -size ... ?-weight bold?"
         * command.  This is the same mechanism used by tkFont.c on all
         * other platforms and ensures the font is reachable by name from
         * both C (Tk_GetFont) and Tcl (font configure).
         *
         * We ignore errors deliberately: if a named font already exists
         * (e.g. because the application registered it before us) we do not
         * want to clobber it.
         */
        Tcl_Obj *cmd = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(NULL, cmd,
            Tcl_NewStringObj("font", -1));
        Tcl_ListObjAppendElement(NULL, cmd,
            Tcl_NewStringObj("create", -1));
        Tcl_ListObjAppendElement(NULL, cmd,
            Tcl_NewStringObj(namedFonts[i].tkName, -1));
        Tcl_ListObjAppendElement(NULL, cmd,
            Tcl_NewStringObj("-family", -1));
        Tcl_ListObjAppendElement(NULL, cmd,
            Tcl_NewStringObj(namedFonts[i].family, -1));
        Tcl_ListObjAppendElement(NULL, cmd,
            Tcl_NewStringObj("-size", -1));
        Tcl_ListObjAppendElement(NULL, cmd,
            Tcl_NewIntObj(namedFonts[i].points));
        if (namedFonts[i].bold) {
            Tcl_ListObjAppendElement(NULL, cmd,
                Tcl_NewStringObj("-weight", -1));
            Tcl_ListObjAppendElement(NULL, cmd,
                Tcl_NewStringObj("bold", -1));
        }
        if (namedFonts[i].italic) {
            Tcl_ListObjAppendElement(NULL, cmd,
                Tcl_NewStringObj("-slant", -1));
            Tcl_ListObjAppendElement(NULL, cmd,
                Tcl_NewStringObj("italic", -1));
        }

        Tcl_IncrRefCount(cmd);
        /* TCL_EVAL_GLOBAL so the command is not affected by any namespace. */
        Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL);
        Tcl_DecrRefCount(cmd);
        /* Clear any error from a duplicate-font-name attempt. */
        Tcl_ResetResult(interp);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetNativeFont --
 *
 *	Resolves a native platform font name (Fontconfig family) to a TkFont.
 *
 * Results:
 *	A new TkFont pointer, or NULL on failure.
 *
 * Side effects:
 *	Allocates a WaylandFont.
 *
 *---------------------------------------------------------------------------
 */
TkFont *
TkpGetNativeFont(
    Tk_Window tkwin,
    const char *name)
{
    TkFontAttributes fa;
    TkInitFontAttributes(&fa);
    fa.family = Tk_GetUid(name);
    fa.size   = -12.0;
    fa.weight = TK_FW_NORMAL;
    fa.slant  = TK_FS_ROMAN;

    return TkpGetFontFromAttributes(NULL, tkwin, &fa);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetFontFromAttributes --
 *
 *	Creates or updates a WaylandFont that matches the requested attributes.
 *
 * Results:
 *	Returns a TkFont pointer.
 *
 * Side effects:
 *	May allocate or reuse platform data; loads font file and initializes
 *	stb_truetype structures.
 *
 *---------------------------------------------------------------------------
 */
TkFont *
TkpGetFontFromAttributes(
    TkFont *tkFontPtr,
    Tk_Window tkwin,
    const TkFontAttributes *faPtr)
{
    WaylandFont *fontPtr;

    if (tkFontPtr == NULL) {
        fontPtr = (WaylandFont *) Tcl_Alloc(sizeof(WaylandFont));
        memset(fontPtr, 0, sizeof(WaylandFont));
        fontPtr->atlas_width = ATLAS_WIDTH;
        fontPtr->atlas_height = ATLAS_HEIGHT;
        fontPtr->atlas = (unsigned char *)Tcl_Alloc(ATLAS_WIDTH * ATLAS_HEIGHT * 4);
        memset(fontPtr->atlas, 0, ATLAS_WIDTH * ATLAS_HEIGHT * 4);
        fontPtr->textureId = 0;
    } else {
        fontPtr = (WaylandFont *) tkFontPtr;
        /*
         * Release only the platform-specific resources; the generic TkFont
         * base (hashed entries, etc.) is managed by the caller.
         */
        DeleteFont(fontPtr);
    }

    InitFont(tkwin, faPtr, fontPtr);
    return (TkFont *) fontPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpDeleteFont --
 *
 *	Releases platform-specific data for a TkFont.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees WaylandFont resources but not the TkFont struct itself.
 *
 *---------------------------------------------------------------------------
 */
void
TkpDeleteFont(
    TkFont *tkFontPtr)
{
    WaylandFont *fontPtr = (WaylandFont *) tkFontPtr;
    DeleteFont(fontPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetFontFamilies --
 *
 *	Returns the list of available font families via Fontconfig.
 *
 * Results:
 *	Sets the interpreter result to a Tcl list of family names.
 *
 * Side effects:
 *	Queries Fontconfig.
 *
 *---------------------------------------------------------------------------
 */
void
TkpGetFontFamilies(
    Tcl_Interp *interp,
    TCL_UNUSED(Tk_Window))
{
    Tcl_Obj    *resultPtr = Tcl_NewListObj(0, NULL);
    FcPattern  *pat       = FcPatternCreate();
    FcObjectSet*os        = FcObjectSetBuild(FC_FAMILY, NULL);
    FcFontSet  *fs        = FcFontList(NULL, pat, os);

    if (fs) {
        Tcl_Obj *seen = Tcl_NewDictObj();
        int i;
        for (i = 0; i < fs->nfont; i++) {
            FcChar8 *family = NULL;
            if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &family)
                    == FcResultMatch) {
                Tcl_Obj *key = Tcl_NewStringObj((char *) family, -1);
                Tcl_Obj *val;
                if (Tcl_DictObjGet(NULL, seen, key, &val) == TCL_OK
                        && val == NULL) {
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

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetSubFonts --
 *
 *	Returns the subfont names composing this font object.
 *
 * Results:
 *	Sets the interpreter result to a Tcl list.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */
void
TkpGetSubFonts(
    Tcl_Interp *interp,
    Tk_Font tkfont)
{
    WaylandFont *fontPtr   = (WaylandFont *) tkfont;
    Tcl_Obj     *resultPtr = Tcl_NewListObj(0, NULL);

    if (fontPtr->font.fa.family) {
        Tcl_ListObjAppendElement(NULL, resultPtr,
            Tcl_NewStringObj(fontPtr->font.fa.family, -1));
    }
    Tcl_SetObjResult(interp, resultPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetFontAttrsForChar --
 *
 *	Determines the effective font attributes used to render a given
 *	Unicode character.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May update the family in the provided attributes based on Fontconfig.
 *
 *---------------------------------------------------------------------------
 */
void
TkpGetFontAttrsForChar(
    TCL_UNUSED(Tk_Window),
    Tk_Font tkfont,
    int c,
    TkFontAttributes *faPtr)
{
    WaylandFont *fontPtr = (WaylandFont *) tkfont;
    *faPtr = fontPtr->font.fa;

    FcCharSet *cs  = FcCharSetCreate();
    FcCharSetAddChar(cs, (FcChar32) c);

    FcPattern  *pat = FcPatternCreate();
    FcPatternAddCharSet(pat, FC_CHARSET, cs);
    if (fontPtr->font.fa.family) {
        FcPatternAddString(pat, FC_FAMILY,
                           (FcChar8 *) fontPtr->font.fa.family);
    }
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult    result;
    FcPattern  *match = FcFontMatch(NULL, pat, &result);
    if (match) {
        FcChar8 *family = NULL;
        if (FcPatternGetString(match, FC_FAMILY, 0, &family) == FcResultMatch
                && family) {
            faPtr->family = Tk_GetUid((char *) family);
        }
        FcPatternDestroy(match);
    }

    FcPatternDestroy(pat);
    FcCharSetDestroy(cs);
}

/*
 *---------------------------------------------------------------------------
 *
 * Tk_MeasureChars --
 *
 *	Measures how many bytes of a UTF-8 string fit within a pixel width.
 *
 * Results:
 *	Returns the count of bytes that fit; sets *lengthPtr to the pixel width.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
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
    return Tk_MeasureCharsInContext(tkfont, source, numBytes,
        0, numBytes, maxLength, flags, lengthPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * Tk_MeasureCharsInContext --
 *
 *	Measures a substring using stb_truetype metrics for accurate layout.
 *
 *	Uses cached glyph metrics from the font's stbtt info. If a glyph
 *	hasn't been rendered yet, we still need its metrics, which we can
 *	get directly from stb_truetype without rendering.
 *
 * Results:
 *	Returns the count of bytes that fit; sets *lengthPtr with pixel width.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
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
    WaylandFont *fontPtr = (WaylandFont *) tkfont;

    if (rangeStart < 0 || rangeLength <= 0 ||
            rangeStart + rangeLength > numBytes ||
            (maxLength == 0 && !(flags & TK_AT_LEAST_ONE))) {
        *lengthPtr = 0;
        return 0;
    }
    if (maxLength > 32767) {
        maxLength = 32767;
    }

    /* Ensure font is loaded */
    if (!EnsureFontLoaded(fontPtr)) {
        /* Fallback to simple estimate */
        int width = 0;
        const char *p          = source + rangeStart;
        const char *end        = source + rangeStart + rangeLength;
        const char *lastBreak  = p;
        int         lastBreakWidth = 0;

        while (p < end) {
            int ch;
            const char *next = p + Tcl_UtfToUniChar(p, &ch);
            int adv = fontPtr->pixelSize / 2;

            if (maxLength >= 0 && width + adv > maxLength) {
                if ((flags & TK_WHOLE_WORDS) && lastBreak > p) {
                    *lengthPtr = lastBreakWidth;
                    return (int)(lastBreak - source - rangeStart);
                }
                if (!(flags & TK_PARTIAL_OK)) break;
            }
            if (ch == ' ' || ch == '\t') {
                lastBreak      = next;
                lastBreakWidth = width + adv;
            }
            width += adv;
            p = next;
        }
        if ((flags & TK_AT_LEAST_ONE) && p == source + rangeStart) {
            int ch;
            p += Tcl_UtfToUniChar(p, &ch);
            width += fontPtr->pixelSize / 2;
        }
        *lengthPtr = width;
        return (int)(p - source - rangeStart);
    }

    /* Measure using stb_truetype metrics */
    const char *p          = source + rangeStart;
    const char *end        = source + rangeStart + rangeLength;
    const char *lastBreak  = p;
    int         totalWidth = 0;
    int         lastBreakWidth = 0;
    
    while (p < end) {
        int ch;
        const char *next = p + Tcl_UtfToUniChar(p, &ch);
        
        /* Get glyph index */
        int glyph = stbtt_FindGlyphIndex(&fontPtr->stbInfo, ch);
        if (glyph == 0 && emojiInitialized) {
            /* Try emoji font as fallback */
            glyph = stbtt_FindGlyphIndex(&emojiInfo, ch);
        }
        
        int advance;
        if (glyph != 0) {
            stbtt_GetGlyphHMetrics(&fontPtr->stbInfo, glyph, &advance, NULL);
            advance = (int)(advance * fontPtr->scale + 0.5f);
        } else {
            /* Missing glyph - use average width */
            advance = fontPtr->pixelSize / 2;
        }
        
        if (maxLength >= 0 && totalWidth + advance > maxLength) {
            if ((flags & TK_WHOLE_WORDS) && lastBreak > p) {
                *lengthPtr = lastBreakWidth;
                return (int)(lastBreak - source - rangeStart);
            }
            if (!(flags & TK_PARTIAL_OK)) break;
        }
        
        if (ch == ' ' || ch == '\t') {
            lastBreak      = next;
            lastBreakWidth = totalWidth + advance;
        }
        
        totalWidth += advance;
        p = next;
    }
    
    if ((flags & TK_AT_LEAST_ONE) && p == source + rangeStart && p < end) {
        int ch;
        p += Tcl_UtfToUniChar(p, &ch);
        totalWidth += fontPtr->pixelSize / 2;
    }
    
    *lengthPtr = totalWidth;
    return (int)(p - source - rangeStart);
}

/*
 *---------------------------------------------------------------------------
 *
 * Tk_DrawChars --
 *
 *	Draws a UTF-8 string at the specified position using stb_truetype.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders text to the current OpenGL context.
 *
 *---------------------------------------------------------------------------
 */
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
    TkpDrawAngledCharsInContext(NULL, 0, gc, tkfont,
        source, numBytes, 0, numBytes,
        (double) x, (double) y, 0.0);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkDrawAngledChars --
 *
 *	Draws a UTF-8 string rotated by the given angle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders rotated text to the current OpenGL context.
 *
 *---------------------------------------------------------------------------
 */
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

/*
 *---------------------------------------------------------------------------
 *
 * Tk_DrawCharsInContext --
 *
 *	Draws a substring of a UTF-8 string.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders a substring to the current OpenGL context.
 *
 *---------------------------------------------------------------------------
 */
void
Tk_DrawCharsInContext(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Drawable),
    GC gc,
    Tk_Font tkfont,
    const char *source,
    Tcl_Size numBytes,
    Tcl_Size rangeStart,
    Tcl_Size rangeLength,
    int x,
    int y)
{
    TkpDrawAngledCharsInContext(NULL, 0, gc, tkfont,
        source, numBytes,
        rangeStart, rangeLength,
        (double) x, (double) y, 0.0);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpDrawAngledCharsInContext --
 *
 *	Canonical text rendering entry point; draws a (possibly rotated)
 *	substring.
 *
 *	Renders text by rasterizing glyphs on demand into a texture atlas
 *	using stb_truetype, then drawing them as textured quads with OpenGL.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders glyphs to atlas as needed; uploads atlas to GPU when full;
 *	draws text with optional underline/overstrike.
 *
 *---------------------------------------------------------------------------
 */
void
TkpDrawAngledCharsInContext(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Drawable),
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

    if (rangeStart < 0 || rangeLength <= 0 ||
            rangeStart + rangeLength > numBytes) {
        return;
    }

    /* Ensure font is loaded */
    if (!EnsureFontLoaded(fontPtr)) {
        return;
    }

    /* Ensure atlas texture exists */
    EnsureAtlasTexture(fontPtr);

    const char *p = source + rangeStart;
    const char *end = p + rangeLength;
    float drawX = (float)x;
    float drawY = (float)y;

    /* Handle rotation by translating/rotating the OpenGL matrix */
    if (angle != 0.0) {
        TkGlfwMatrixPush();
        TkGlfwMatrixTranslate(drawX, drawY, 0.0f);
        TkGlfwMatrixRotate((float)(angle * 3.14159265358979323846 / 180.0), 0.0f, 0.0f, 1.0f);
        TkGlfwMatrixTranslate(-drawX, -drawY, 0.0f);
    }

    /* Draw each character */
    while (p < end) {
        int ch;
        const char *next = p + Tcl_UtfToUniChar(p, &ch);
        
        GlyphCacheEntry *glyph = NULL;
        if (GetGlyph(fontPtr, ch, &glyph) && glyph) {
            /* Draw the glyph */
            float glyphX = drawX + glyph->bearing_x;
            float glyphY = drawY - glyph->bearing_y; /* Convert from baseline to top-left */
            
            DrawGlyphQuad(fontPtr, glyph, glyphX, glyphY, color);
            
            /* Advance position */
            drawX += glyph->advance;
        } else {
            /* Missing glyph - draw a placeholder box */
            float boxWidth = fontPtr->pixelSize / 2;
            TkGlfwDrawRect(drawX, drawY - fontPtr->pixelSize, 
                          boxWidth, fontPtr->pixelSize, color);
            drawX += boxWidth;
        }
        
        p = next;
    }

    /* Draw underline and overstrike if needed */
    if (fontPtr->font.fa.underline || fontPtr->font.fa.overstrike) {
        float runWidth = drawX - (float)x;
        
        if (fontPtr->font.fa.underline) {
            float uy = (float)(y + fontPtr->underlinePos);
            TkGlfwDrawLine((float)x, uy, (float)x + runWidth, uy, 
                          (float)fontPtr->barHeight, color);
        }
        if (fontPtr->font.fa.overstrike) {
            float oy = (float)(y - fontPtr->font.fm.ascent / 2);
            TkGlfwDrawLine((float)x, oy, (float)x + runWidth, oy, 
                          (float)fontPtr->barHeight, color);
        }
    }

    if (angle != 0.0) {
        TkGlfwMatrixPop();
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkPostscriptFontName --
 *
 *	Builds a PostScript font name for the given TkFont.
 *
 * Results:
 *	Returns 0 on success.
 *
 * Side effects:
 *	Appends font name to the dynamic string.
 *
 *---------------------------------------------------------------------------
 */
int
TkPostscriptFontName(
    Tk_Font tkfont,
    Tcl_DString *dsPtr)
{
    WaylandFont *fontPtr = (WaylandFont *) tkfont;
    const char  *family  = fontPtr->font.fa.family
        ? fontPtr->font.fa.family : "Helvetica";

    Tcl_DStringAppend(dsPtr, family, -1);
    if (fontPtr->font.fa.weight == TK_FW_BOLD) {
        Tcl_DStringAppend(dsPtr, "-Bold", -1);
    }
    if (fontPtr->font.fa.slant == TK_FS_ITALIC) {
        Tcl_DStringAppend(dsPtr, "-Italic", -1);
    }
    return 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkUnixSetXftClipRegion --
 *
 *	No-op stub; clipping is handled by OpenGL scissor test.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */
void
TkUnixSetXftClipRegion(
    TCL_UNUSED(Region))
{
    /* No-op: clipping handled through OpenGL scissor. */
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpDrawCharsInContext --
 *
 *	Simple delegating wrapper required by some Tk internal callers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Delegates to Tk_DrawCharsInContext.
 *
 *---------------------------------------------------------------------------
 */
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

/*
 *---------------------------------------------------------------------------
 *
 * TkpMeasureCharsInContext --
 *
 *	Simple delegating wrapper required by some Tk internal callers.
 *
 * Results:
 *	Returns count of bytes that fit.
 *
 * Side effects:
 *	Delegates to Tk_MeasureCharsInContext.
 *
 *---------------------------------------------------------------------------
 */
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

/*
 *---------------------------------------------------------------------------
 *
 * FindFontFile --
 *
 *	Ask Fontconfig for the best font file matching the given family
 *	and style attributes. Returns a malloc'd path string (caller must
 *	free with free()), or NULL if nothing matched.
 *
 * Results:
 *	Returns malloc'd path string or NULL.
 *
 * Side effects:
 *	Queries Fontconfig.
 *
 *---------------------------------------------------------------------------
 */
static char *
FindFontFile(
    const char *family,
    int bold,
    int italic,
    int pixelSize)
{
    FcPattern *pat = FcPatternCreate();
    if (!pat) return NULL;

    if (family) {
        FcPatternAddString(pat, FC_FAMILY, (FcChar8 *) family);
    }
    FcPatternAddInteger(pat, FC_WEIGHT,
                        bold   ? FC_WEIGHT_BOLD   : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pat, FC_SLANT,
                        italic ? FC_SLANT_ITALIC   : FC_SLANT_ROMAN);
    if (pixelSize > 0) {
        FcPatternAddDouble(pat, FC_PIXEL_SIZE, (double) pixelSize);
    }
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult   result;
    FcPattern *match = FcFontMatch(NULL, pat, &result);
    char      *path  = NULL;

    if (match) {
        FcChar8 *fcPath = NULL;
        if (FcPatternGetString(match, FC_FILE, 0, &fcPath) == FcResultMatch
                && fcPath) {
            path = strdup((char *) fcPath);
        }
        FcPatternDestroy(match);
    }

    FcPatternDestroy(pat);
    return path;
}

/*
 *---------------------------------------------------------------------------
 *
 * InitFont --
 *
 *	Populate a WaylandFont from TkFontAttributes. Resolves the font
 *	file via Fontconfig, computes metrics via stbtt, and stores
 *	everything needed for later rendering.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes font structure; resolves font file path.
 *
 *---------------------------------------------------------------------------
 */
static void
InitFont(
    Tk_Window tkwin,
    const TkFontAttributes *faPtr,
    WaylandFont *fontPtr)
{
    TkFontAttributes *fa = &fontPtr->font.fa;
    TkFontMetrics    *fm = &fontPtr->font.fm;

    *fa = *faPtr;

    double ptSize = faPtr->size;
    if (ptSize < 0.0) {
        fontPtr->pixelSize = (int)(-ptSize + 0.5);
    } else if (ptSize > 0.0) {
        fontPtr->pixelSize = (int)(TkFontGetPoints(tkwin, ptSize) + 0.5);
    } else {
        fontPtr->pixelSize = 12;
    }
    if (fontPtr->pixelSize < 1) fontPtr->pixelSize = 1;

    int bold   = (faPtr->weight == TK_FW_BOLD);
    int italic = (faPtr->slant  == TK_FS_ITALIC);

    fontPtr->filePath = FindFontFile(faPtr->family, bold, italic,
                                     fontPtr->pixelSize);
    
    /* Clear any existing font data */
    if (fontPtr->fontData) {
        Tcl_Free((char *)fontPtr->fontData);
        fontPtr->fontData = NULL;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * DeleteFont --
 *
 *	Release platform-specific resources inside a WaylandFont without
 *	freeing the struct itself.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees font data, atlas, texture, and glyph cache.
 *
 *---------------------------------------------------------------------------
 */
static void
DeleteFont(
    WaylandFont *fontPtr)
{
    if (fontPtr->filePath) {
        free(fontPtr->filePath);
        fontPtr->filePath = NULL;
    }
    if (fontPtr->fontData) {
        Tcl_Free((char *)fontPtr->fontData);
        fontPtr->fontData = NULL;
    }
    if (fontPtr->atlas) {
        Tcl_Free((char *)fontPtr->atlas);
        fontPtr->atlas = NULL;
    }
    if (fontPtr->textureId) {
        TkGlfwDeleteTexture(fontPtr->textureId);
        fontPtr->textureId = 0;
    }
    
    /* Free glyph cache */
    for (int i = 0; i < GLYPH_CACHE_SIZE; i++) {
        GlyphCacheEntry *entry = fontPtr->glyphCache[i];
        while (entry) {
            GlyphCacheEntry *next = entry->next;
            Tcl_Free((char *)entry);
            entry = next;
        }
        fontPtr->glyphCache[i] = NULL;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * EnsureFontLoaded --
 *
 *	Ensures the font file is loaded and stb_truetype is initialized.
 *
 * Results:
 *	1 on success, 0 on failure.
 *
 * Side effects:
 *	Loads font file, initializes stbtt info, computes scale.
 *
 *---------------------------------------------------------------------------
 */
static int
EnsureFontLoaded(
    WaylandFont *fontPtr)
{
    if (fontPtr->fontData != NULL) {
        return 1; /* Already loaded */
    }

    if (!fontPtr->filePath) {
        /* Fallback to DejaVu Sans */
        const char *fallback = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
        fontPtr->filePath = strdup(fallback);
    }

    FILE *fd = fopen(fontPtr->filePath, "rb");
    if (!fd) {
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

    fontPtr->underlinePos = fontPtr->font.fm.descent / 2;
    if (fontPtr->underlinePos < 1) fontPtr->underlinePos = 1;
    fontPtr->barHeight = (int)(fontPtr->pixelSize * 0.07 + 0.5);
    if (fontPtr->barHeight < 1) fontPtr->barHeight = 1;

    return 1;
}

/*
 *---------------------------------------------------------------------------
 *
 * RenderGlyphToAtlas --
 *
 *	Renders a glyph to the font's texture atlas using stb_truetype.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates atlas with glyph bitmap; may upload atlas to GPU if full.
 *
 *---------------------------------------------------------------------------
 */
static void
RenderGlyphToAtlas(
    WaylandFont *fontPtr,
    int codepoint,
    GlyphCacheEntry *entry)
{
    stbtt_fontinfo *info = &fontPtr->stbInfo;
    float scale = fontPtr->scale;
    
    /* Try to get glyph from main font */
    int glyph = stbtt_FindGlyphIndex(info, codepoint);
    stbtt_fontinfo *renderInfo = info;
    
    /* If not found, try emoji font */
    if (glyph == 0 && emojiInitialized) {
        glyph = stbtt_FindGlyphIndex(&emojiInfo, codepoint);
        if (glyph != 0) {
            renderInfo = &emojiInfo;
            /* Recalculate scale for emoji font */
            float emojiScale = stbtt_ScaleForPixelHeight(renderInfo, 
                                                          (float)fontPtr->pixelSize);
            scale = emojiScale;
        }
    }
    
    if (glyph == 0) {
        /* Missing glyph - create a placeholder box */
        entry->width = fontPtr->pixelSize / 2;
        entry->height = fontPtr->pixelSize;
        entry->bearing_x = 0;
        entry->bearing_y = fontPtr->pixelSize;
        entry->advance = entry->width;
        
        /* Allocate space in atlas */
        if (fontPtr->atlas_used_x + entry->width + ATLAS_PADDING > fontPtr->atlas_width) {
            fontPtr->atlas_used_x = 0;
            fontPtr->atlas_used_y += fontPtr->atlas_row_height + ATLAS_PADDING;
            fontPtr->atlas_row_height = 0;
        }
        
        entry->atlas_x = fontPtr->atlas_used_x;
        entry->atlas_y = fontPtr->atlas_used_y;
        
        /* Draw placeholder box in atlas */
        for (int py = 0; py < entry->height; py++) {
            for (int px = 0; px < entry->width; px++) {
                int atlas_idx = ((entry->atlas_y + py) * fontPtr->atlas_width + 
                                 (entry->atlas_x + px)) * 4;
                if (px == 0 || px == entry->width-1 || py == 0 || py == entry->height-1) {
                    /* Border */
                    fontPtr->atlas[atlas_idx] = 255;
                    fontPtr->atlas[atlas_idx+1] = 255;
                    fontPtr->atlas[atlas_idx+2] = 255;
                    fontPtr->atlas[atlas_idx+3] = 255;
                } else {
                    /* Interior - transparent */
                    fontPtr->atlas[atlas_idx+3] = 0;
                }
            }
        }
        
        fontPtr->atlas_used_x += entry->width + ATLAS_PADDING;
        if (entry->height > fontPtr->atlas_row_height) {
            fontPtr->atlas_row_height = entry->height;
        }
        
        /* Update texture coordinates */
        entry->s0 = (float)entry->atlas_x / fontPtr->atlas_width;
        entry->t0 = (float)entry->atlas_y / fontPtr->atlas_height;
        entry->s1 = (float)(entry->atlas_x + entry->width) / fontPtr->atlas_width;
        entry->t1 = (float)(entry->atlas_y + entry->height) / fontPtr->atlas_height;
        
        return;
    }

    /* Get glyph metrics */
    int advance, lsb;
    stbtt_GetGlyphHMetrics(renderInfo, glyph, &advance, &lsb);
    
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(renderInfo, glyph, scale, scale, &x0, &y0, &x1, &y1);
    
    entry->width = x1 - x0;
    entry->height = y1 - y0;
    entry->bearing_x = (int)(lsb * scale);
    entry->bearing_y = y1; /* Distance from baseline to top */
    entry->advance = (int)(advance * scale + 0.5f);

    if (entry->width <= 0 || entry->height <= 0) {
        /* Empty glyph (space, etc.) */
        entry->width = 0;
        entry->height = 0;
        return;
    }

    /* Check if atlas needs more space */
    if (fontPtr->atlas_used_x + entry->width + ATLAS_PADDING > fontPtr->atlas_width) {
        fontPtr->atlas_used_x = 0;
        fontPtr->atlas_used_y += fontPtr->atlas_row_height + ATLAS_PADDING;
        fontPtr->atlas_row_height = 0;
        
        /* If atlas is full, upload current contents and reset */
        if (fontPtr->atlas_used_y + entry->height + ATLAS_PADDING > fontPtr->atlas_height) {
            UploadAtlasToGPU(fontPtr);
            fontPtr->atlas_used_x = 0;
            fontPtr->atlas_used_y = 0;
            fontPtr->atlas_row_height = 0;
            memset(fontPtr->atlas, 0, fontPtr->atlas_width * fontPtr->atlas_height * 4);
        }
    }

    entry->atlas_x = fontPtr->atlas_used_x;
    entry->atlas_y = fontPtr->atlas_used_y;

    /* Render glyph to atlas */
    unsigned char *bitmap = (unsigned char *)Tcl_Alloc(entry->width * entry->height);
    if (bitmap) {
        stbtt_MakeGlyphBitmap(renderInfo, bitmap, entry->width, entry->height,
                              entry->width, scale, scale, glyph);
        
        /* Convert to RGBA and copy to atlas */
        for (int py = 0; py < entry->height; py++) {
            for (int px = 0; px < entry->width; px++) {
                int atlas_idx = ((entry->atlas_y + py) * fontPtr->atlas_width + 
                                 (entry->atlas_x + px)) * 4;
                unsigned char alpha = bitmap[py * entry->width + px];
                fontPtr->atlas[atlas_idx] = 255;     /* R */
                fontPtr->atlas[atlas_idx+1] = 255;   /* G */
                fontPtr->atlas[atlas_idx+2] = 255;   /* B */
                fontPtr->atlas[atlas_idx+3] = alpha; /* A */
            }
        }
        
        Tcl_Free((char *)bitmap);
    }

    /* Update atlas position */
    fontPtr->atlas_used_x += entry->width + ATLAS_PADDING;
    if (entry->height > fontPtr->atlas_row_height) {
        fontPtr->atlas_row_height = entry->height;
    }

    /* Calculate texture coordinates */
    entry->s0 = (float)entry->atlas_x / fontPtr->atlas_width;
    entry->t0 = (float)entry->atlas_y / fontPtr->atlas_height;
    entry->s1 = (float)(entry->atlas_x + entry->width) / fontPtr->atlas_width;
    entry->t1 = (float)(entry->atlas_y + entry->height) / fontPtr->atlas_height;
}

/*
 *---------------------------------------------------------------------------
 *
 * GetGlyph --
 *
 *	Retrieves a glyph from the cache, rendering it if necessary.
 *
 * Results:
 *	1 if glyph is available (entryOut set), 0 otherwise.
 *
 * Side effects:
 *	May render glyph to atlas if not already cached.
 *
 *---------------------------------------------------------------------------
 */
static int
GetGlyph(
    WaylandFont *fontPtr,
    int codepoint,
    GlyphCacheEntry **entryOut)
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
    
    /* Render glyph to atlas */
    RenderGlyphToAtlas(fontPtr, codepoint, entry);
    
    /* Add to cache */
    entry->next = fontPtr->glyphCache[hash];
    fontPtr->glyphCache[hash] = entry;
    
    *entryOut = entry;
    return 1;
}

/*
 *---------------------------------------------------------------------------
 *
 * EnsureAtlasTexture --
 *
 *	Ensures the font's atlas texture exists and is up to date.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates OpenGL texture if needed.
 *
 *---------------------------------------------------------------------------
 */
static void
EnsureAtlasTexture(
    WaylandFont *fontPtr)
{
    if (fontPtr->textureId == 0) {
        fontPtr->textureId = TkGlfwCreateTexture(fontPtr->atlas_width, 
                                                 fontPtr->atlas_height,
                                                 fontPtr->atlas);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * UploadAtlasToGPU --
 *
 *	Uploads the current atlas contents to the GPU texture.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates OpenGL texture with current atlas data.
 *
 *---------------------------------------------------------------------------
 */
static void
UploadAtlasToGPU(
    WaylandFont *fontPtr)
{
    if (fontPtr->textureId) {
        TkGlfwUpdateTexture(fontPtr->textureId, fontPtr->atlas_width,
                           fontPtr->atlas_height, fontPtr->atlas);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * DrawGlyphQuad --
 *
 *	Draws a single glyph as a textured quad.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders a quad with the glyph texture.
 *
 *---------------------------------------------------------------------------
 */
static void
DrawGlyphQuad(
    WaylandFont *fontPtr,
    GlyphCacheEntry *glyph,
    float x,
    float y,
    uint32_t color)
{
    if (glyph->width <= 0 || glyph->height <= 0) {
        return;
    }
    
    TkGlfwDrawTexturedQuad(fontPtr->textureId,
                           x, y,
                           (float)glyph->width,
                           (float)glyph->height,
                           glyph->s0, glyph->t0,
                           glyph->s1, glyph->t1,
                           color);
}

/*
 *---------------------------------------------------------------------------
 *
 * ColorFromGC --
 *
 *	Extract the foreground color from an X11 GC and convert it to a
 *	32-bit RGBA value.
 *
 * Results:
 *	Returns RGBA color value.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */
static uint32_t
ColorFromGC(GC gc)
{
    if (gc) {
        XGCValues vals;
        TkWaylandGetGCValues(gc, GCForeground, &vals);
        /* Convert from X11 pixel to RGBA */
        unsigned char r = (vals.foreground >> 16) & 0xFF;
        unsigned char g = (vals.foreground >> 8) & 0xFF;
        unsigned char b = vals.foreground & 0xFF;
        return (r << 24) | (g << 16) | (b << 8) | 0xFF;
    }
    return 0x000000FF; /* Black, full opacity */
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */