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
 * the macOS font implementation. Font *discovery* is delegated to Fontconfig
 * (just as the macOS file delegates it to NSFontManager), and font *rendering*
 * is delegated entirely to NanoVG (just as the macOS file delegates it to
 * CoreText). We do NOT maintain our own glyph-coverage bitmaps, SubFont
 * linked lists, or stbtt rasterization state. NanoVG already wraps
 * stb_truetype internally and handles fallback at draw time; prior
 * implementations of Wayland fonts used Tk's Unix font implementation as the
 * source, but removing the X11 mechanisms made font management unstable
 * and crash-prone.
 */

/*
 *------------------------------------------------------------------------
 *
 * Platform font structure:
 *
 * Mirrors the simplicity of the macOS implementation: the generic TkFont
 * base is first, followed only by the platform handles we actually need.
 * NanoVG manages all the rasterization state; we just keep the resolved
 * file path and the NanoVG font id so we can load the font lazily on
 * first draw.
 *
 *------------------------------------------------------------------------
 */

typedef struct {
    TkFont      font;           /* Generic font data — MUST be first.        */
    char       *filePath;       /* Absolute path returned by Fontconfig.
                                 * Owned by this struct; freed on delete.    */
    int         nvgFontId;      /* Handle returned by nvgCreateFont / -1.    */
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
    WaylandFont *fontPtr,
    NVGcontext  *vg);
static NVGcolor ColorFromGC(
    GC gc);

/*
 *---------------------------------------------------------------------------
 *
 * TkpFontPkgInit --
 *
 *     Initializes the platform font package for a new Tk application.
 *     Registers the standard Tk named fonts (TkDefaultFont, TkFixedFont,
 *     etc.) so that wish and other Tk applications can resolve them at
 *     startup without crashing.
 *
 *     Named fonts must be registered here, before any widget is created,
 *     because Tk's generic layer calls TkpFontPkgInit exactly once and
 *     then immediately tries to resolve TkDefaultFont for the root window.
 *     If the named fonts are absent at that point, Tk panics.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Initializes Fontconfig; creates Tk named fonts via Tk_CreateFont.
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
         * base (hashed entries, etc.) is managed by the caller.
         */
        DeleteFont(fontPtr);
    }

    /*
     * nvgFontId == -1 signals "not yet loaded into any NVG context".
     * We defer the actual nvgCreateFont call to first draw so that
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
 *     Determines the effective font attributes used to render a given
 *     Unicode character.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     May update the family in the provided attributes based on Fontconfig.
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
 *     Measures how many bytes of a UTF-8 string fit within a pixel width.
 *
 * Results:
 *     Returns the count of bytes that fit; sets *lengthPtr to the pixel width.
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
 *     Measures a substring using NanoVG metrics for accurate layout.
 *
 *     Uses TkGlfwGetNVGContextForMeasure() which does not require an
 *     active NanoVG frame, allowing measurement during geometry computation
 *     outside of expose handling.
 *
 *     The per-context lazy font load (same pattern as DrawTitleBar) is
 *     applied here: nvgFindFont() checks whether the font atlas is present
 *     in the *current* GL context before attempting a load.
 *
 * Results:
 *     Returns the count of bytes that fit; sets *lengthPtr with pixel width.
 *
 * Side effects:
 *     May load the font into the current NanoVG context on first call.
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

    NVGcontext *vg = TkGlfwGetNVGContextForMeasure();

    if (!vg || EnsureNvgFont(fontPtr, vg) < 0) {
        /*
         * No NVG context yet (startup before GLFW is initialised) — fall
         * back to a per-character advance estimate from stored metrics.
         */
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

    /* Measure using NanoVG. */
    nvgSave(vg);
    nvgFontFaceId(vg, fontPtr->nvgFontId);
    nvgFontSize(vg, (float) fontPtr->pixelSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

    const char *rangePtr = source + rangeStart;
    const char *rangeEnd = rangePtr + rangeLength;

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

    NVGglyphPosition  stackPos[256];
    NVGglyphPosition *positions = stackPos;
    if (nchars > 256) {
        positions = (NVGglyphPosition *)
                Tcl_Alloc(nchars * sizeof(NVGglyphPosition));
    }

    int npos = nvgTextGlyphPositions(vg, 0, 0, rangePtr, rangeEnd,
                                     positions, nchars);

    float bounds[4];
    nvgTextBounds(vg, 0, 0, rangePtr, rangeEnd, bounds);
    float totalWidth = bounds[2];

    int         pixelWidth     = 0;
    const char *lastBreak      = rangePtr;
    int         lastBreakWidth = 0;
    const char *p              = rangePtr;
    int         pi             = 0;

    while (p < rangeEnd && pi < npos) {
        int ch;
        const char *next = p + Tcl_UtfToUniChar(p, &ch);

        float glyphRight = positions[pi].maxx;
        int   glyphWidth = (int) ceil(glyphRight - positions[pi].x);

        if (maxLength >= 0 && pixelWidth + glyphWidth > maxLength) {
            if ((flags & TK_WHOLE_WORDS) && lastBreak > rangePtr) {
                p          = lastBreak;
                pixelWidth = lastBreakWidth;
            } else if (flags & TK_PARTIAL_OK) {
                pixelWidth += glyphWidth;
                p = next;
            }
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
 *     Applies the same per-context lazy font load as DrawTitleBar:
 *     nvgFindFont() is called on the *active* vg context before use, and
 *     nvgCreateFont() is called if the atlas is not yet populated for
 *     this context.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Loads font into active NVG context on first call per context;
 *     renders text and optional underline/overstrike.
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

    if (EnsureNvgFont(fontPtr, vg) < 0) return;

    nvgSave(vg);
    nvgFontFaceId(vg, fontPtr->nvgFontId);
    nvgFontSize(vg, (float) fontPtr->pixelSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgFillColor(vg, ColorFromGC(gc));

    double drawX = x;

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

    if (fontPtr->font.fa.underline || fontPtr->font.fa.overstrike) {
        float bounds[4];
        float runWidth;

        if (angle != 0.0) {
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
 *     No-op stub; clipping is handled by NanoVG.
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
 *     Ask Fontconfig for the best font file matching the given family
 *     and style attributes. Returns a malloc'd path string (caller must
 *     free with free()), or NULL if nothing matched.
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
 *     Populate a WaylandFont from TkFontAttributes. Resolves the font
 *     file via Fontconfig, computes metrics via stbtt, and stores
 *     everything needed for later rendering. The NanoVG font handle
 *     is created lazily (EnsureNvgFont) because the NVG context may
 *     not exist yet at startup.
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

    if (fontPtr->filePath) {
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
                    fm->ascent  = (int)(ascent   * scale + 0.5f);
                    fm->descent = (int)(-descent * scale + 0.5f);

                    int adv_W, adv_dot, lsb;
                    stbtt_GetCodepointHMetrics(&info, 'W', &adv_W, &lsb);
                    stbtt_GetCodepointHMetrics(&info, '.', &adv_dot, &lsb);
                    fm->maxWidth = (int)(adv_W * scale + 0.5f);
                    fm->fixed    = (adv_W == adv_dot);

                    fa->size = (double)(-fontPtr->pixelSize);
                }
            }
            if (buf) Tcl_Free((char *) buf);
            fclose(fd);
        }
    }

    if (fm->ascent == 0 && fm->descent == 0) {
        fm->ascent   = (int)(fontPtr->pixelSize * 0.80 + 0.5);
        fm->descent  = (int)(fontPtr->pixelSize * 0.20 + 0.5);
        fm->maxWidth = fontPtr->pixelSize;
        fm->fixed    = 0;
    }

    fontPtr->underlinePos = fm->descent / 2;
    if (fontPtr->underlinePos < 1) fontPtr->underlinePos = 1;
    fontPtr->barHeight    = (int)(fontPtr->pixelSize * 0.07 + 0.5);
    if (fontPtr->barHeight < 1) fontPtr->barHeight = 1;

    fontPtr->nvgFontId    = -1;
    fontPtr->font.fid     = (Font)(uintptr_t) fontPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * DeleteFont --
 *
 *     Release platform-specific resources inside a WaylandFont without
 *     freeing the struct itself.
 *
 *     The NanoVG font handle is intentionally NOT destroyed here: NanoVG
 *     owns the font atlas and there is no nvgDeleteFont() API.  The handle
 *     remains valid until the NVG context itself is destroyed.
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
 *     Load the font into the *provided* NanoVG context if it has not been
 *     loaded yet for that context.
 *
 *     Per-context lazy loading (same fix as DrawTitleBar in tkWaylandDecor.c):
 *
 *       1. nvgFindFont(vg, name) — O(1) name lookup in the current context's
 *          atlas.  If it returns >= 0 the font is already present and we are
 *          done.  This is called on every draw and measurement call so it
 *          must be cheap.
 *
 *       2. If not found, nvgCreateFont(vg, name, filePath) is called NOW,
 *          while the correct GL context is current (guaranteed by the callers
 *          TkGlfwGetNVGContext / TkGlfwGetNVGContextForMeasure which both
 *          call glfwMakeContextCurrent before returning).  The resulting
 *          atlas texture is therefore owned by that context.
 *
 *       3. The id is stored in fontPtr->nvgFontId as a fast-path cache for
 *          the *next* call in the same context.  Because all windows share
 *          one NVGcontext* but may have different GL contexts, the id value
 *          returned by NanoVG is stable across contexts for the same named
 *          font — only the *texture* upload differs per context, which is
 *          handled transparently by NanoVG's atlas dirty-flag mechanism when
 *          it sees a new GL context.
 *
 *     Falls back to DejaVu Sans if the resolved file cannot be loaded.
 *
 * Results:
 *     NVG font ID (>= 0) on success, -1 on failure.
 *
 * Side effects:
 *     May call nvgCreateFont() to upload the atlas for this GL context.
 *
 *---------------------------------------------------------------------------
 */

static int
EnsureNvgFont(
    WaylandFont *fontPtr,
    NVGcontext  *vg)
{
    const char *name;
    int         id;

    if (!vg) return -1;

    name = fontPtr->font.fa.family ? fontPtr->font.fa.family : "default";

    /*
     * Step 1: check whether the atlas already contains this font name in
     * the current GL context.  This is the hot path — no file I/O.
     */
    id = nvgFindFont(vg, name);
    if (id >= 0) {
        fontPtr->nvgFontId = id;
        return id;
    }

    /*
     * Step 2: font not yet loaded into this context — do it now.
     * The GL context is guaranteed current by the caller.
     */
    if (fontPtr->filePath) {
        id = nvgCreateFont(vg, name, fontPtr->filePath);
    }

    /*
     * Step 3: if the resolved file failed (missing, wrong format, etc.)
     * fall back to the system DejaVu Sans so something always renders.
     */
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
 *     NVGcolor.
 *
 *---------------------------------------------------------------------------
 */

static NVGcolor
ColorFromGC(GC gc)
{
    if (gc) {
        XGCValues vals;
        TkWaylandGetGCValues(gc, GCForeground, &vals);
        return TkGlfwPixelToNVG(vals.foreground);
    }
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
