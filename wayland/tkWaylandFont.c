/*
 *
 * tkWaylandFont.c –
 *
 * This module implements the Wayland/GLFW/NanoVG platform-specific
 * features of fonts.
 *
 * Copyright © 1996-1998 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include "tkInt.h"
#include "tkFont.h"
#include "tkGlfwInt.h"

#include <fontconfig/fontconfig.h>
#include <nanovg.h>

#include "stb_truetype.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 
 * Architecture note: this file is intentionally modelled after 
 * the macOS font implmentation. Font *discovery* is delegated to Fontconfig 
 * (just as the macOS file delegates it to NSFontManager), and font *rendering* 
 * is delegated entirely to NanoVG (just as the macOS file delegates it to 
 * CoreText). We do NOT maintain our own glyph-coverage bitmaps, SubFont 
 * linked lists, or stbtt rasterization state. NanoVG already wraps 
 * stb_truetype internally and handles fallback at draw time; prior 
 * implemenations of Wayland fonts used Tk's Unix font implementation as the 
 * source, but removing the X11 mechanisms made font management unstable 
 * and crash-prone. 
 * 
 */ 

/* 
 *------------------------------------------------------------------------ 
 * 
 * Platform font structure: 
 * 
 * Mirrors the simplicity of macOS implementation: the generic TkFont base 
 * is first, followed only by the platform handles we actually need. 
 * NanoVG manages all the rasteriztion state; we just keep the resolved file 
 * path and the NanoVG font id so we can load the font lazily on first draw. 
 * 
 *------------------------------------------------------------------------ 
 */ 

typedef struct {
    TkFont      font;           /* Generic font data — MUST be first. */
    char       *filePath;       /* Absolute path returned by Fontconfig.
                                  * Owned by this struct; freed on delete. */
    int         nvgFontId;      /* Handle returned by nvgCreateFontMem / -1. */
    int         pixelSize;      /* Resolved size in pixels.                  */
    int         underlinePos;   /* Pixels below baseline for underline.      */
    int         barHeight;      /* Thickness of under/overstrike bar.        */
} WaylandFont;


/* Whether Fontconfig has been initialized for this process. */
static int fcInitialized = 0;

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
static int      EnsureNvgFont(
    WaylandFont *fontPtr);
static NVGcolor ColorFromGC(
    GC gc);

/*
 *---------------------------------------------------------------------------
 *
 * TkpFontPkgInit --
 *
 *     Initializes the platform font package for a new Tk application and
 *     registers standard named fonts.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Initializes Fontconfig and creates Tk named fonts.
 *
 *---------------------------------------------------------------------------
 */ 

void
TkpFontPkgInit(
	       TCL_UNUSED(TkMainInfo *)) /* mainPtr */
{

    if (!fcInitialized) {
        FcInit();
        fcInitialized = 1;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetNativeFont --
 *
 *     Resolves a native platform font name (Fontconfig family) to a TkFont.
 *
 * Results:
 *     A new TkFont pointer, or NULL on failure.
 *
 * Side effects:
 *     Allocates a WaylandFont.
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
 *     Creates or updates a WaylandFont that matches the requested attributes.
 *
 * Results:
 *     Returns a TkFont pointer.
 *
 * Side effects:
 *     May allocate or reuse platform data; defers NVG font creation.
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
    } else {
        fontPtr = (WaylandFont *) tkFontPtr;
        /*
         * Release only the platform-specific resources; the generic TkFont
         * base (hashed entries, etc.) is managed by the caller and must not
         * be touched here. 
         */
        DeleteFont(fontPtr);
    }

    /*
     * nvgFontId == -1 signals "not yet loaded into a NVG context".
     * We defer the actual nvgCreateFontMem call to first draw so that
     * this function never needs to touch the NVG context (which may not
     * exist yet when fonts are created at startup).
     */
    fontPtr->nvgFontId = -1;

    InitFont(tkwin, faPtr, fontPtr);
    return (TkFont *) fontPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpDeleteFont --
 *
 *     Releases platform-specific data for a TkFont.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Frees WaylandFont resources but not the TkFont struct itself.
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
 *     Returns the list of available font families via Fontconfig.
 *
 * Results:
 *     Sets the interpreter result to a Tcl list of family names.
 *
 * Side effects:
 *     Queries Fontconfig.
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
        /* Deduplicate with a simple hash table via a Tcl dict. */
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
 *     Returns the subfont names composing this font object.
 *
 * Results:
 *     Sets the interpreter result to a Tcl list.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */ 
 
void
TkpGetSubFonts(
    Tcl_Interp *interp,
    Tk_Font tkfont)
{
    WaylandFont *fontPtr  = (WaylandFont *) tkfont;
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
 *     Determines the effective font attributes used to render a given Unicode
 *     character.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     May update the family in the provided attributes based on Fontconfig
 *     matching.
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
    *faPtr = fontPtr->font.fa;   /* Start with current attributes. */


    /*
     * Ask Fontconfig for a font that covers codepoint c.  If the result
     * differs from the primary family, update faPtr->family so callers
     * know which family would actually be used.
     */
    FcCharSet *cs  = FcCharSetCreate();
    FcCharSetAddChar(cs, (FcChar32) c);

    FcPattern  *pat = FcPatternCreate();
    FcPatternAddCharSet(pat, FC_CHARSET, cs);
    if (fontPtr->font.fa.family) {
        /* Prefer the current family if it covers the char. */
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
 *     Measures how many bytes of a UTF-8 string fit within a pixel width.
 *
 * Results:
 *     Returns the count of bytes that fit; sets *lengthPtr to the pixel width.
 *
 * Side effects:
 *     Delegates to Tk_MeasureCharsInContext.
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
 *     Measures a substring in context using NanoVG metrics for accurate
 *     layout. Uses the measurement context which does not require an
 *     active NanoVG frame, allowing this function to be called during
 *     geometry computation outside of expose handling.
 *
 * Results:
 *     Returns the count of bytes that fit; sets *lengthPtr with the
 *     pixel width.
 *
 * Side effects:
 *     May load the font into the NanoVG context on first call.
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

    /* Argument validation. */
    if (rangeStart < 0 || rangeLength <= 0 ||
            rangeStart + rangeLength > numBytes ||
            (maxLength == 0 && !(flags & TK_AT_LEAST_ONE))) {
        *lengthPtr = 0;
        return 0;
    }
    if (maxLength > 32767) {
        maxLength = 32767;
    }

    /*
     * Use the measurement context — this bypasses the nvgFrameActive
     * check so measurement can happen during geometry computation,
     * widget configuration, and any other path outside expose handling.
     */
    NVGcontext *vg = TkGlfwGetNVGContextForMeasure();

    /*
     * If no NVG context is available yet (e.g. measuring during startup
     * before GLFW is initialized) fall back to a simple per-character
     * advance estimate based on the stored font metrics.
     */
    if (!vg || EnsureNvgFont(fontPtr) < 0) {
        int width = 0;
        const char *p        = source + rangeStart;
        const char *end      = source + rangeStart + rangeLength;
        const char *lastBreak      = p;
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

    /* Measure using NanoVG. */
    nvgSave(vg);
    nvgFontFaceId(vg, fontPtr->nvgFontId);
    nvgFontSize(vg, (float) fontPtr->pixelSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

    const char *rangePtr = source + rangeStart;
    const char *rangeEnd = rangePtr + rangeLength;

    /*
     * Count codepoints in range so we can size the glyph positions
     * array appropriately.
     */
    int nchars = 0;
    {
        const char *p = rangePtr;
        while (p < rangeEnd) {
            int ch;
            p += Tcl_UtfToUniChar(p, &ch);
            nchars++;
        }
    }
    if (nchars == 0) {
        *lengthPtr = 0;
        nvgRestore(vg);
        return 0;
    }

    /*
     * Stack-allocate positions for runs up to 256 codepoints;
     * heap-allocate for longer runs.
     */
    NVGglyphPosition  stackPos[256];
    NVGglyphPosition *positions = stackPos;
    if (nchars > 256) {
        positions = (NVGglyphPosition *)
                Tcl_Alloc(nchars * sizeof(NVGglyphPosition));
    }

    int npos = nvgTextGlyphPositions(vg, 0, 0, rangePtr, rangeEnd,
                                     positions, nchars);

    /*
     * Measure the full range to get the right edge of the last glyph,
     * which nvgTextGlyphPositions does not directly expose.
     */
    float bounds[4];
    nvgTextBounds(vg, 0, 0, rangePtr, rangeEnd, bounds);
    float totalWidth = bounds[2];

    int         pixelWidth       = 0;
    const char *lastBreak        = rangePtr;
    int         lastBreakWidth   = 0;
    const char *p                = rangePtr;
    int         pi               = 0;

    while (p < rangeEnd && pi < npos) {
        int ch;
        const char *next = p + Tcl_UtfToUniChar(p, &ch);

        /*
         * Width of this glyph is the distance from its x position to
         * the next glyph's x position, or to the total width for the
         * last glyph.
         */
        float glyphRight = (pi + 1 < npos)
                ? positions[pi + 1].x
                : totalWidth;
        int glyphWidth = (int) ceil(glyphRight - positions[pi].x);

        if (maxLength >= 0 && pixelWidth + glyphWidth > maxLength) {
            if ((flags & TK_WHOLE_WORDS) && lastBreak > rangePtr) {
                p          = lastBreak;
                pixelWidth = lastBreakWidth;
            } else if (flags & TK_PARTIAL_OK) {
                pixelWidth += glyphWidth;
                p = next;
            }
            /* else: stop before this character */
            break;
        }

        pixelWidth += glyphWidth;
        if (ch == ' ' || ch == '\t') {
            lastBreak      = next;
            lastBreakWidth = pixelWidth;
        }

        p = next;
        pi++;
    }

    /*
     * Guarantee at least one character when TK_AT_LEAST_ONE is set,
     * even if that character exceeds maxLength.
     */
    if ((flags & TK_AT_LEAST_ONE) && p == rangePtr && rangePtr < rangeEnd) {
        int ch;
        const char *next = rangePtr + Tcl_UtfToUniChar(rangePtr, &ch);
        float glyphRight = (npos > 1) ? positions[1].x : totalWidth;
        pixelWidth = (int) ceil(glyphRight);
        p = next;
    }

    if (positions != stackPos) {
        Tcl_Free((char *) positions);
    }

    nvgRestore(vg);

    *lengthPtr = pixelWidth;
    return (int)(p - rangePtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * Tk_DrawChars --
 *
 *     Draws a UTF-8 string at the specified position using NanoVG.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Renders text via NanoVG through the common angled-draw entry point.
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
 *     Draws a UTF-8 string rotated by the given angle.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Delegates to TkpDrawAngledCharsInContext.
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
 *     Draws a substring of a UTF-8 string.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Delegates to TkpDrawAngledCharsInContext.
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
 *     Canonical text rendering entry point; draws a (possibly rotated)
 *     substring.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Uses NanoVG transforms and renders; draws underline/overstrike when set.
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


    if (rangeStart < 0 || rangeLength <= 0 ||
            rangeStart + rangeLength > numBytes) {
        return;
    }

    NVGcontext *vg = TkGlfwGetNVGContext();
    if (!vg) return;
    if (EnsureNvgFont(fontPtr) < 0) return;

    nvgSave(vg);
    nvgFontFaceId(vg, fontPtr->nvgFontId);
    nvgFontSize(vg, (float) fontPtr->pixelSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgFillColor(vg, ColorFromGC(gc));

    double drawX = x;

    /*
     * When drawing a sub-range, compute the pixel offset of the range
     * start by measuring the prefix.  
     */
    if (rangeStart > 0) {
        float bounds[4];
        nvgTextBounds(vg, 0, 0, source, source + rangeStart, bounds);
        drawX += (double) bounds[2];
    }

    const char *rangePtr = source + rangeStart;
    const char *rangeEnd = rangePtr + rangeLength;

    if (angle != 0.0) {
        nvgTranslate(vg, (float) drawX, (float) y);
        nvgRotate(vg, (float)(angle * NVG_PI / 180.0));
        nvgText(vg, 0.0f, 0.0f, rangePtr, rangeEnd);
    } else {
        nvgText(vg, (float) drawX, (float) y, rangePtr, rangeEnd);
    }

    /*
     * Underline and overstrike — same geometry as the previous
     * implementation but now computed after the NVG draw so we can reuse
     * the color already set.
     */
    if (fontPtr->font.fa.underline || fontPtr->font.fa.overstrike) {
        float bounds[4];
        float runWidth;

        if (angle != 0.0) {
            /* Width at origin, before rotation. */
            nvgTextBounds(vg, 0, 0, rangePtr, rangeEnd, bounds);
            runWidth = bounds[2];
        } else {
            nvgTextBounds(vg, (float) drawX, (float) y, rangePtr, rangeEnd,
                          bounds);
            runWidth = bounds[2] - (float) drawX;
        }

        nvgStrokeColor(vg, ColorFromGC(gc));
        nvgStrokeWidth(vg, (float) fontPtr->barHeight);

        if (fontPtr->font.fa.underline) {
            float uy = (angle != 0.0) ? (float) fontPtr->underlinePos
                                      : (float)(y + fontPtr->underlinePos);
            float ux = (angle != 0.0) ? 0.0f : (float) drawX;
            nvgBeginPath(vg);
            nvgMoveTo(vg, ux, uy);
            nvgLineTo(vg, ux + runWidth, uy);
            nvgStroke(vg);
        }
        if (fontPtr->font.fa.overstrike) {
            float oy = (angle != 0.0)
                    ? -(float)(fontPtr->font.fm.ascent / 2)
                    : (float)(y - fontPtr->font.fm.ascent / 2);
            float ox = (angle != 0.0) ? 0.0f : (float) drawX;
            nvgBeginPath(vg);
            nvgMoveTo(vg, ox, oy);
            nvgLineTo(vg, ox + runWidth, oy);
            nvgStroke(vg);
        }
    }

    nvgRestore(vg);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkPostscriptFontName --
 *
 *     Builds a PostScript font name for the given TkFont.
 *
 * Results:
 *     Returns 0 on success.
 *
 * Side effects:
 *     Appends family and style suffixes to the Tcl_DString.
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
 *     No-op stub for Xft clipping; clipping is handled by NanoVG.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */ 

void
TkUnixSetXftClipRegion(
    TCL_UNUSED(Region))
{
    /* No-op: NanoVG handles clipping through its scissor API. */
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpDrawCharsInContext --
 *
 *     Simple delegating wrapper required by some Tk internal callers.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Delegates to Tk_DrawCharsInContext.
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
 *     Simple delegating wrapper required by some Tk internal callers.
 *
 * Results:
 *     Returns the count of bytes that fit; sets *lengthPtr with the pixel width.
 *
 * Side effects:
 *     Delegates to Tk_MeasureChars.
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
    if (rangeStart + rangeLength > numBytes) {
        rangeLength = numBytes - rangeStart;
    }
    return Tk_MeasureChars(tkfont, source + rangeStart, rangeLength,
        maxLength, flags, lengthPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * FindFontFile --
 *
 *     Ask Fontconfig for the best font file matching the given family
 *     and style attributes. Returns a malloc'd path string (caller must
 *     free with free()), or NULL if nothing matched.
 *
 * Results:
 *     Malloc'd path string or NULL.
 *
 * Side effects:
 *     Queries Fontconfig.
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
                        bold   ? FC_WEIGHT_BOLD  : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pat, FC_SLANT,
                        italic ? FC_SLANT_ITALIC  : FC_SLANT_ROMAN);
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
 *     Populate a WaylandFont from TkFontAttributes. Resolves the
 *     font file via Fontconfig, stores all resolved attributes back into
 *     font.fa, and computes the font metrics.
 *     We compute metrics by asking Fontconfig/stbtt once, here, rather
 *     than at every draw call. The NanoVG font handle is created lazily
 *     (EnsureNvgFont) because the NVG context may not exist yet.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Initializes font metrics and file path.
 *
 *---------------------------------------------------------------------------
 */

static void
InitFont(
    Tk_Window tkwin,
    const TkFontAttributes *faPtr,
    WaylandFont *fontPtr)
{
    TkFontAttributes *fa  = &fontPtr->font.fa;
    TkFontMetrics    *fm  = &fontPtr->font.fm;


    /* Copy requested attributes into the generic base. */
    *fa = *faPtr;

    /* Resolve pixel size from the requested point/pixel size. */
    double ptSize = faPtr->size;
    if (ptSize < 0.0) {
        /* Negative means pixels already. */
        fontPtr->pixelSize = (int)(-ptSize + 0.5);
    } else if (ptSize > 0.0) {
        fontPtr->pixelSize = (int)(TkFontGetPoints(tkwin, ptSize) + 0.5);
    } else {
        fontPtr->pixelSize = 12;   /* Default, as on macOS. */
    }
    if (fontPtr->pixelSize < 1) fontPtr->pixelSize = 1;

    /* Resolve the font file through Fontconfig. */
    int bold   = (faPtr->weight == TK_FW_BOLD);
    int italic = (faPtr->slant  == TK_FS_ITALIC);

    fontPtr->filePath = FindFontFile(faPtr->family, bold, italic,
                                     fontPtr->pixelSize);

    /*
     * If Fontconfig returned a file, use stbtt (via NanoVG's internal
     * calls) to get accurate metrics. Otherwise estimate from pixelSize.
     * This mirrors the macOS use of CTFontGetBoundingRectsForGlyphs for
     * metric correction.
     */
    if (fontPtr->filePath) {
        /*
         * Open the file and parse metrics with stbtt directly. We only
         * need the metrics here; NanoVG will re-read the file on first
         * draw via EnsureNvgFont.
         */
        FILE *fd = fopen(fontPtr->filePath, "rb");
        if (fd) {
            fseek(fd, 0, SEEK_END);
            long sz = ftell(fd);
            fseek(fd, 0, SEEK_SET);

            unsigned char *buf = (unsigned char *) Tcl_Alloc((int) sz);
            if (buf && (long) fread(buf, 1, sz, fd) == sz) {
                stbtt_fontinfo info;
                if (stbtt_InitFont(&info, buf,
                                   stbtt_GetFontOffsetForIndex(buf, 0))) {
                    float scale = stbtt_ScaleForPixelHeight(
                                      &info, (float) fontPtr->pixelSize);
                    int ascent, descent, linegap;
                    stbtt_GetFontVMetrics(&info, &ascent, &descent, &linegap);
                    fm->ascent  = (int)(ascent  * scale + 0.5f);
                    fm->descent = (int)(-descent * scale + 0.5f);

                    /* Measure 'W' for maxWidth, check fixed pitch with '.' vs 'i'. */
                    int adv_W, adv_dot, lsb;
                    stbtt_GetCodepointHMetrics(&info, 'W', &adv_W, &lsb);
                    stbtt_GetCodepointHMetrics(&info, '.', &adv_dot, &lsb);
                    fm->maxWidth = (int)(adv_W * scale + 0.5f);
                    fm->fixed    = (adv_W == adv_dot);

                    /* Reflect resolved attributes back into fa. */
                    fa->size = (double)(-fontPtr->pixelSize);
                }
            }
            if (buf) Tcl_Free((char *) buf);
            fclose(fd);
        }
    }

    /* Fallback metrics when the file couldn't be parsed. */
    if (fm->ascent == 0 && fm->descent == 0) {
        fm->ascent   = (int)(fontPtr->pixelSize * 0.80 + 0.5);
        fm->descent  = (int)(fontPtr->pixelSize * 0.20 + 0.5);
        fm->maxWidth = fontPtr->pixelSize;
        fm->fixed    = 0;
    }

    /* Underline/overstrike geometry. */
    fontPtr->underlinePos = fm->descent / 2;
    if (fontPtr->underlinePos < 1) fontPtr->underlinePos = 1;
    fontPtr->barHeight    = (int)(fontPtr->pixelSize * 0.07 + 0.5);
    if (fontPtr->barHeight < 1) fontPtr->barHeight = 1;

    /* The NVG font id is resolved lazily on first draw. */
    fontPtr->nvgFontId = -1;

    /* Required by the generic font layer: fid must be non-zero and unique. */
    fontPtr->font.fid = (Font)(uintptr_t) fontPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * DeleteFont --
 *
 *     Release platform-specific resources inside a WaylandFont without
 *     freeing the struct itself. Called from TkpDeleteFont and from
 *     TkpGetFontFromAttributes when reusing an existing struct. The NanoVG 
 *     font handle is intentionally NOT destroyed here: NanoVG owns the font 
 *     atlas and there is no nvgDeleteFont() API. The handle remains valid 
 *     until the NVG context itself is destroyed.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Frees filePath string.
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
    fontPtr->nvgFontId = -1;
}

/*
 *---------------------------------------------------------------------------
 *
 * EnsureNvgFont --
 *
 *     Load the font into the current NanoVG context if it has not been 
 *     loaded yet. Returns the nvgFontId (>= 0) on success, -1 on failure.
 *     We use nvgCreateFont (file path) rather than nvgCreateFontMem so
 *     that NanoVG manages the memory lifetime, matching the approach
 *     used by the NanoVG examples and avoiding double-ownership issues.
 *     NanoVG deduplicates fonts by name, so calling this on the same
 *     font from multiple WaylandFont structs is safe.
 *
 * Results:
 *     NVG font ID or -1 on failure.
 *
 * Side effects:
 *     Loads font into NanoVG context.
 *
 *---------------------------------------------------------------------------
 */

static int
EnsureNvgFont(WaylandFont *fontPtr)
{
    NVGcontext *vg;
    const char *name;
    int id;

    if (fontPtr->nvgFontId >= 0) return fontPtr->nvgFontId;

    /* Font loading only needs an initialized NVG context and a current
     * GL context. It does NOT need an active frame. Bypass the frame
     * check by going directly to glfwContext. */
    TkGlfwContext *ctx = TkGlfwGetContext();
    if (!ctx || !ctx->initialized || !ctx->vg) return -1;

    /* Ensure some GL context is current so nvgCreateFont can upload
     * the font atlas. Use the main shared window if nothing else is. */
    if (glfwGetCurrentContext() == NULL) {
        glfwMakeContextCurrent(ctx->mainWindow);
    }

    vg   = ctx->vg;
    name = fontPtr->font.fa.family ? fontPtr->font.fa.family : "default";

    id = nvgFindFont(vg, name);
    if (id >= 0) {
        fontPtr->nvgFontId = id;
        return id;
    }

    if (fontPtr->filePath) {
        id = nvgCreateFont(vg, name, fontPtr->filePath);
    }
    if (id < 0) {
        id = nvgCreateFont(vg, name,
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    }

    fontPtr->nvgFontId = id;
    return id;
}
/*
 *---------------------------------------------------------------------------
 *
 * ColorFromGC --
 *
 *     Extract the foreground color from an X11 GC and convert it to an 
 *     NVGcolor. Delegates the pixel-to-NVG conversion to the GLFW 
 *     compatibility layer.
 *
 * Results:
 *     NVGcolor value.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */

static NVGcolor
ColorFromGC(
    GC gc)
{
    if (gc) {
        XGCValues vals;
        if (XGetGCValues(NULL, gc, GCForeground, &vals)) {
            return TkGlfwPixelToNVG(vals.foreground);
        }
    }
    /* Fallback: opaque black. */
    return nvgRGBA(0, 0, 0, 255);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
