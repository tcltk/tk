/*
 * tkWaylandFont.c --
 *
 *      Unix/Wayland implementation of the platform-independent font package
 *      interface using NanoVG + stb_truetype + Fontconfig.
 *
 *      Copyright © 1996-1997 Sun Microsystems, Inc.
 *      Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution.
 */

#include "tkFont.h"
#include "tkUnixInt.h"          /* If still needed for some defines */

#include <fontconfig/fontconfig.h>
#include <stb_truetype.h>
#include <nanovg.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
   Forward declarations & helpers
   ------------------------------------------------------------------------- */

static NVGcontext *GetNanoVGContext(Tk_Window tkwin);
static NVGcolor    GetColorFromGC(GC gc);

/* Assume these are provided by the higher-level integration layer */
extern NVGcontext *GetNanoVGContext(Tk_Window tkwin);
extern NVGcolor    GetColorFromGC(GC gc);

/* -------------------------------------------------------------------------
   Constants & structures
   ------------------------------------------------------------------------- */

#define FONTMAP_SHIFT       10
#define FONTMAP_BITSPERPAGE (1 << FONTMAP_SHIFT)
#define FONTMAP_NUMCHARS    0x40000
#define FONTMAP_PAGES       (FONTMAP_NUMCHARS / FONTMAP_BITSPERPAGE)

#define SUBFONT_SPACE       3
#define BASE_CHARS          256

typedef struct FontFamily {
    struct FontFamily  *nextPtr;
    size_t              refCount;
    Tk_Uid              faceName;
    char               *filePath;           /* full path from fontconfig */
    unsigned char      *fontBuffer;         /* owned TTF/OTF file content */
    int                 bufferSize;
    stbtt_fontinfo      fontInfo;
    char               *fontMap[FONTMAP_PAGES];
    int                 ascent, descent;    /* in pixels at nominal size */
} FontFamily;

typedef struct SubFont {
    char              **fontMap;            /* cached pointer to family->fontMap */
    FontFamily         *familyPtr;
} SubFont;

typedef struct UnixFont {
    TkFont              font;               /* generic part — must be first */
    SubFont             staticSubFonts[SUBFONT_SPACE];
    int                 numSubFonts;
    SubFont            *subFontArray;
    SubFont             controlSubFont;
    int                 pixelSize;          /* requested pixel size */
    int                 widths[BASE_CHARS]; /* fast path for ASCII */
    int                 underlinePos;
    int                 barHeight;
} UnixFont;

/* Thread-specific data */
typedef struct {
    FontFamily         *fontFamilyList;
    FontFamily          controlFamily;
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/* -------------------------------------------------------------------------
   Forward declarations of static functions
   ------------------------------------------------------------------------- */

static void         FontPkgCleanup(void *clientData);
static FontFamily  *AllocFontFamily(const char *faceName, int pixelSize);
static void         FreeFontFamily(FontFamily *family);
static SubFont     *FindSubFontForChar(UnixFont *fontPtr, int ch, SubFont **fixPtr);
static int          FontMapLookup(SubFont *sf, int ch);
static void         FontMapInsert(SubFont *sf, int ch);
static void         FontMapLoadPage(SubFont *sf, int page);
static SubFont     *CanUseFallback(UnixFont *fontPtr, const char *face, int ch, SubFont **fix);
static int          SeenName(const char *name, Tcl_DString *ds);
static void         InitFont(Tk_Window tkwin, const TkFontAttributes *fa, UnixFont *uf);
static void         InitSubFont(SubFont *sf, FontFamily *family, int pixelSize);
static void         ReleaseFont(UnixFont *uf);
static void         ReleaseSubFont(SubFont *sf);
static char       **ListFonts(const char *pattern, int *countPtr);

/* -------------------------------------------------------------------------
   Package initialization / cleanup
   ------------------------------------------------------------------------- */

void
TkpFontPkgInit(TkMainInfo *mainPtr)
{
    ThreadSpecificData *tsd = Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    if (tsd->controlFamily.faceName != NULL) return;

    tsd->controlFamily.refCount   = 2;
    tsd->controlFamily.faceName   = Tk_GetUid("monospace");
    tsd->controlFamily.filePath   = NULL;
    tsd->controlFamily.fontBuffer = NULL;

    FcPattern *pat = FcPatternCreate();
    FcPatternAddString(pat, FC_FAMILY, (FcChar8*)"monospace");
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult result;
    FcPattern *match = FcFontMatch(NULL, pat, &result);
    if (match) {
        FcChar8 *path = NULL;
        if (FcPatternGetString(match, FC_FILE, 0, &path) == FcResultMatch) {
            tsd->controlFamily.filePath = strdup((char*)path);
        }
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pat);

    if (tsd->controlFamily.filePath) {
        FILE *f = fopen(tsd->controlFamily.filePath, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            tsd->controlFamily.bufferSize = ftell(f);
            fseek(f, 0, SEEK_SET);
            tsd->controlFamily.fontBuffer = Tcl_Alloc(tsd->controlFamily.bufferSize);
            fread(tsd->controlFamily.fontBuffer, 1, tsd->controlFamily.bufferSize, f);
            fclose(f);
            stbtt_InitFont(&tsd->controlFamily.fontInfo, tsd->controlFamily.fontBuffer, 0);
        }
    }

    /* Mark control chars + hex digits as present */
    SubFont dummy = { tsd->controlFamily.fontMap, &tsd->controlFamily };
    int i;
    for (i = 0; i < 0x20; i++)          FontMapInsert(&dummy, i);
    for (i = 0x80; i < 0xA0; i++)       FontMapInsert(&dummy, i);
    for (i = '0'; i <= '9';   i++)      FontMapInsert(&dummy, i);
    for (i = 'A'; i <= 'F';   i++)      FontMapInsert(&dummy, i);
    for (i = 'a'; i <= 'f';   i++)      FontMapInsert(&dummy, i);
    FontMapInsert(&dummy, '\\');

    Tcl_CreateThreadExitHandler(FontPkgCleanup, NULL);
}

static void
FontPkgCleanup(void *clientData)
{
    ThreadSpecificData *tsd = Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    FontFamily *f = tsd->fontFamilyList;
    while (f) {
        FontFamily *next = f->nextPtr;
        FreeFontFamily(f);
        f = next;
    }
    tsd->fontFamilyList = NULL;
    FreeFontFamily(&tsd->controlFamily);
}

/* -------------------------------------------------------------------------
   Core font creation
   ------------------------------------------------------------------------- */

TkFont *
TkpGetNativeFont(Tk_Window tkwin, const char *name)
{
    TkFontAttributes fa = {0};
    fa.family    = Tk_GetUid(name);
    fa.size      = -12.0;           /* reasonable default */
    fa.weight    = TK_FW_NORMAL;
    fa.slant     = TK_FS_ROMAN;

    return TkpGetFontFromAttributes(NULL, tkwin, &fa);
}

TkFont *
TkpGetFontFromAttributes(TkFont *existing, Tk_Window tkwin,
                         const TkFontAttributes *want)
{
    UnixFont *uf = (UnixFont *) existing;
    if (uf) ReleaseFont(uf);
    else    uf = Tcl_Alloc(sizeof(UnixFont));

    memset(uf, 0, sizeof(UnixFont));
    uf->pixelSize = (int) (-want->size + 0.5);
    if (uf->pixelSize <= 0) uf->pixelSize = 12;

    InitFont(tkwin, want, uf);

    /* Always have at least the control subfont */
    InitSubFont(&uf->controlSubFont,
                &((ThreadSpecificData*)Tcl_GetThreadData(&dataKey,sizeof(ThreadSpecificData)))->controlFamily,
                uf->pixelSize);

    return (TkFont *) uf;
}

static void
InitFont(Tk_Window tkwin, const TkFontAttributes *fa, UnixFont *uf)
{
    uf->font.fa = *fa;

    /* Create primary subfont */
    FontFamily *primary = AllocFontFamily(fa->family ? fa->family : "sans-serif", uf->pixelSize);
    if (primary) {
        uf->subFontArray = uf->staticSubFonts;
        InitSubFont(&uf->subFontArray[0], primary, uf->pixelSize);
        uf->numSubFonts = 1;
    }

    /* Calculate font metrics */
    if (primary && primary->fontInfo.userdata) {
        uf->font.fm.ascent  = primary->ascent;
        uf->font.fm.descent = primary->descent;
        uf->font.fm.maxWidth = uf->pixelSize * 2;  /* conservative estimate */
        uf->font.fm.fixed = 0;  /* TrueType fonts are generally not fixed-width */
    } else {
        /* Fallback metrics */
        uf->font.fm.ascent  = (int)(uf->pixelSize * 0.8 + 0.5);
        uf->font.fm.descent = (int)(uf->pixelSize * 0.2 + 0.5);
        uf->font.fm.maxWidth = uf->pixelSize;
        uf->font.fm.fixed = 0;
    }

    /* Underline / overstrike geometry (approximation) */
    uf->underlinePos = -2;
    uf->barHeight    = 1;
    if (primary) {
        uf->underlinePos = (int)(primary->descent * 0.4);
        uf->barHeight    = (int)(uf->pixelSize * 0.07 + 0.5);
        if (uf->barHeight < 1) uf->barHeight = 1;
    }

    /* Fast-path ASCII widths */
    memset(uf->widths, 0, sizeof(uf->widths));
    if (uf->numSubFonts > 0) {
        SubFont *sf = &uf->subFontArray[0];
        stbtt_fontinfo *info = &sf->familyPtr->fontInfo;
        if (info->userdata) {
            float scale = stbtt_ScaleForPixelHeight(info, (float)uf->pixelSize);
            int i;
            for (i = 32; i < 128; i++) {
                int adv, lsb;
                stbtt_GetCodepointHMetrics(info, i, &adv, &lsb);
                uf->widths[i] = (int)(adv * scale + 0.5f);
            }
        }
    }
}

static void
InitSubFont(SubFont *sf, FontFamily *family, int pixelSize)
{
    sf->familyPtr = family;
    sf->fontMap   = family->fontMap;
    family->refCount++;
}

/* -------------------------------------------------------------------------
   Font release
   ------------------------------------------------------------------------- */

void
TkpDeleteFont(TkFont *tkf)
{
    ReleaseFont((UnixFont *) tkf);
}

static void
ReleaseFont(UnixFont *uf)
{
    int i;
    for (i = 0; i < uf->numSubFonts; i++) {
        ReleaseSubFont(&uf->subFontArray[i]);
    }
    if (uf->subFontArray != uf->staticSubFonts) {
        Tcl_Free(uf->subFontArray);
    }
    ReleaseSubFont(&uf->controlSubFont);
    Tcl_Free(uf);
}

static void
ReleaseSubFont(SubFont *sf)
{
    if (sf->familyPtr) {
        FreeFontFamily(sf->familyPtr);
        sf->familyPtr = NULL;
    }
}

/* -------------------------------------------------------------------------
   Font family cache & loading
   ------------------------------------------------------------------------- */

static FontFamily *
AllocFontFamily(const char *faceName, int pixelSize)
{
    ThreadSpecificData *tsd = Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    Tk_Uid uid = Tk_GetUid(faceName);

    FontFamily *f;
    for (f = tsd->fontFamilyList; f; f = f->nextPtr) {
        if (f->faceName == uid) {
            f->refCount++;
            return f;
        }
    }

    f = Tcl_Alloc(sizeof(FontFamily));
    memset(f, 0, sizeof(FontFamily));
    f->faceName   = uid;
    f->refCount   = 2;
    f->nextPtr    = tsd->fontFamilyList;
    tsd->fontFamilyList = f;

    FcPattern *pat = FcPatternCreate();
    FcPatternAddString(pat, FC_FAMILY, (FcChar8*)faceName);
    FcPatternAddDouble(pat, FC_PIXEL_SIZE, (double)pixelSize);
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result;
    FcPattern *match = FcFontMatch(NULL, pat, &result);
    FcChar8 *path = NULL;
    if (match && FcPatternGetString(match, FC_FILE, 0, &path) == FcResultMatch) {
        f->filePath = strdup((char*)path);
    }
    if (match) FcPatternDestroy(match);
    FcPatternDestroy(pat);

    if (f->filePath) {
        FILE *fd = fopen(f->filePath, "rb");
        if (fd) {
            fseek(fd, 0, SEEK_END);
            f->bufferSize = ftell(fd);
            fseek(fd, 0, SEEK_SET);
            f->fontBuffer = Tcl_Alloc(f->bufferSize);
            fread(f->fontBuffer, 1, f->bufferSize, fd);
            fclose(fd);

            if (!stbtt_InitFont(&f->fontInfo, f->fontBuffer, stbtt_GetFontOffsetForIndex(f->fontBuffer, 0))) {
                Tcl_Free(f->fontBuffer);
                f->fontBuffer = NULL;
                f->bufferSize = 0;
            }
        }
    }

    if (f->fontInfo.userdata) {
        float scale = stbtt_ScaleForPixelHeight(&f->fontInfo, (float)pixelSize);
        int ascent, descent, linegap;
        stbtt_GetFontVMetrics(&f->fontInfo, &ascent, &descent, &linegap);
        f->ascent  = (int)(ascent  * scale + 0.5f);
        f->descent = (int)(-descent * scale + 0.5f);
    }

    return f;
}

static void
FreeFontFamily(FontFamily *f)
{
    if (!f || --f->refCount > 0) return;

    if (f->filePath)   free(f->filePath);
    if (f->fontBuffer) Tcl_Free(f->fontBuffer);

    int i;
    for (i = 0; i < FONTMAP_PAGES; i++) {
        if (f->fontMap[i]) Tcl_Free(f->fontMap[i]);
    }

    ThreadSpecificData *tsd = Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    FontFamily **pp = &tsd->fontFamilyList;
    while (*pp && *pp != f) pp = &(*pp)->nextPtr;
    if (*pp) *pp = f->nextPtr;

    Tcl_Free(f);
}

/* -------------------------------------------------------------------------
   Character → SubFont mapping & glyph existence cache
   ------------------------------------------------------------------------- */

static SubFont *
FindSubFontForChar(UnixFont *uf, int ch, SubFont **fixPtr)
{
    if (ch < 0 || ch >= FONTMAP_NUMCHARS) ch = 0xFFFD;

    ThreadSpecificData *tsd = Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (FontMapLookup(&uf->controlSubFont, ch)) {
        return &uf->controlSubFont;
    }

    int i;
    for (i = 0; i < uf->numSubFonts; i++) {
        if (FontMapLookup(&uf->subFontArray[i], ch)) {
            return &uf->subFontArray[i];
        }
    }

    Tcl_DString tried;
    Tcl_DStringInit(&tried);

    /* 1. Try same family different encoding/style variant */
    if (SeenName(uf->font.fa.family, &tried) == 0) {
        SubFont *sf = CanUseFallback(uf, uf->font.fa.family, ch, fixPtr);
        if (sf) {
            Tcl_DStringFree(&tried);
            return sf;
        }
    }

    /* 2. Try known fallback families */
    const char *const *fallbacks = TkFontGetFallbacks()[0]; /* simplistic */
    for (i = 0; fallbacks && fallbacks[i]; i++) {
        if (SeenName(fallbacks[i], &tried) == 0) {
            SubFont *sf = CanUseFallback(uf, fallbacks[i], ch, fixPtr);
            if (sf) {
                Tcl_DStringFree(&tried);
                return sf;
            }
        }
    }

    /* 3. Last resort — give up and use control font */
    FontMapInsert(&uf->controlSubFont, ch);
    Tcl_DStringFree(&tried);
    return &uf->controlSubFont;
}

static int
FontMapLookup(SubFont *sf, int ch)
{
    if (ch < 0 || ch >= FONTMAP_NUMCHARS) return 0;
    int page = ch >> FONTMAP_SHIFT;
    if (!sf->fontMap[page]) FontMapLoadPage(sf, page);
    int bit = ch & (FONTMAP_BITSPERPAGE - 1);
    return (sf->fontMap[page][bit >> 3] >> (bit & 7)) & 1;
}

static void
FontMapInsert(SubFont *sf, int ch)
{
    if (ch < 0 || ch >= FONTMAP_NUMCHARS) return;
    int page = ch >> FONTMAP_SHIFT;
    if (!sf->fontMap[page]) FontMapLoadPage(sf, page);
    int bit = ch & (FONTMAP_BITSPERPAGE - 1);
    sf->fontMap[page][bit >> 3] |= (1 << (bit & 7));
}

static void
FontMapLoadPage(SubFont *sf, int page)
{
    sf->fontMap[page] = Tcl_Alloc(FONTMAP_BITSPERPAGE / 8);
    memset(sf->fontMap[page], 0, FONTMAP_BITSPERPAGE / 8);

    stbtt_fontinfo *info = &sf->familyPtr->fontInfo;
    if (!info->userdata) return;

    int start = page << FONTMAP_SHIFT;
    int end   = start + FONTMAP_BITSPERPAGE;

    int i;
    for (i = start; i < end; i++) {
        int idx = stbtt_FindGlyphIndex(info, i);
        if (idx == 0) continue;        /* no glyph */
        sf->fontMap[page][(i-start)>>3] |= 1 << ((i-start)&7);
    }
}

/* -------------------------------------------------------------------------
   Fallback logic stubs (can be expanded later)
   ------------------------------------------------------------------------- */

static SubFont *
CanUseFallback(UnixFont *uf, const char *face, int ch, SubFont **fix)
{
    FontFamily *fam = AllocFontFamily(face, uf->pixelSize);
    if (!fam || !fam->fontInfo.userdata) {
        if (fam) FreeFontFamily(fam);
        return NULL;
    }

    /* Quick check if this font probably contains the glyph */
    if (stbtt_FindGlyphIndex(&fam->fontInfo, ch) == 0) {
        FreeFontFamily(fam);
        return NULL;
    }

    if (uf->numSubFonts >= SUBFONT_SPACE && uf->subFontArray == uf->staticSubFonts) {
        int n = uf->numSubFonts + 1;
        SubFont *newArray = Tcl_Alloc(n * sizeof(SubFont));
        memcpy(newArray, uf->staticSubFonts, SUBFONT_SPACE * sizeof(SubFont));
        uf->subFontArray = newArray;
        if (fix && *fix >= uf->staticSubFonts && *fix < uf->staticSubFonts + SUBFONT_SPACE) {
            *fix = newArray + (*fix - uf->staticSubFonts);
        }
    }

    SubFont *sf = &uf->subFontArray[uf->numSubFonts++];
    InitSubFont(sf, fam, uf->pixelSize);
    FontMapInsert(sf, ch);      /* mark it usable */
    return sf;
}

static int
SeenName(const char *name, Tcl_DString *ds)
{
    if (!name) return 1;
    
    const char *p = Tcl_DStringValue(ds);
    const char *end = p + Tcl_DStringLength(ds);
    while (p < end) {
        if (strcasecmp(p, name) == 0) return 1;
        p += strlen(p) + 1;
    }
    Tcl_DStringAppend(ds, name, -1);
    Tcl_DStringAppend(ds, "\0", 1);
    return 0;
}

/* -------------------------------------------------------------------------
   Drawing
   ------------------------------------------------------------------------- */

void
Tk_DrawChars(Display *display, Drawable d, GC gc, Tk_Font tkfont,
             const char *text, Tcl_Size numBytes, int x, int y)
{
    UnixFont *uf = (UnixFont *) tkfont;
    
    /* Get NanoVG context - we need a proper Tk_Window for this */
    /* In real implementation, this would be extracted from the drawable/display */
    NVGcontext *vg = GetNanoVGContext(NULL);
    if (!vg) return;

    nvgSave(vg);
    nvgFontSize(vg, (float)uf->pixelSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgFillColor(vg, GetColorFromGC(gc));

    float curX = (float)x;
    const char *p = text;
    const char *end = text + numBytes;
    const char *runStart = text;
    SubFont *last = uf->numSubFonts > 0 ? &uf->subFontArray[0] : &uf->controlSubFont;

    while (p < end) {
        int ch;
        const char *next = p + Tcl_UtfToUniChar(p, &ch);
        SubFont *sf = FindSubFontForChar(uf, ch, NULL);

        if (sf != last) {
            /* flush previous run */
            if (p > runStart && last->familyPtr && last->familyPtr->fontBuffer) {
                int fontId = nvgFindFont(vg, last->familyPtr->faceName);
                if (fontId < 0) {
                    fontId = nvgCreateFontMem(vg, last->familyPtr->faceName,
                                              last->familyPtr->fontBuffer,
                                              last->familyPtr->bufferSize, 0);
                }
                if (fontId >= 0) {
                    nvgFontFaceId(vg, fontId);
                    curX = nvgText(vg, curX, (float)y, runStart, p);
                }
            }
            last = sf;
            runStart = p;
        }
        p = next;
    }

    /* flush final run */
    if (p > runStart && last->familyPtr && last->familyPtr->fontBuffer) {
        int fontId = nvgFindFont(vg, last->familyPtr->faceName);
        if (fontId < 0) {
            fontId = nvgCreateFontMem(vg, last->familyPtr->faceName,
                                      last->familyPtr->fontBuffer,
                                      last->familyPtr->bufferSize, 0);
        }
        if (fontId >= 0) {
            nvgFontFaceId(vg, fontId);
            curX = nvgText(vg, curX, (float)y, runStart, p);
        }
    }

    /* underline */
    if (uf->font.fa.underline) {
        nvgBeginPath(vg);
        nvgStrokeColor(vg, GetColorFromGC(gc));
        nvgStrokeWidth(vg, (float)uf->barHeight);
        nvgMoveTo(vg, (float)x, (float)(y + uf->underlinePos));
        nvgLineTo(vg, curX, (float)(y + uf->underlinePos));
        nvgStroke(vg);
    }
    
    /* overstrike */
    if (uf->font.fa.overstrike) {
        int oy = y - (uf->font.fm.ascent / 2);
        nvgBeginPath(vg);
        nvgStrokeColor(vg, GetColorFromGC(gc));
        nvgStrokeWidth(vg, (float)uf->barHeight);
        nvgMoveTo(vg, (float)x, (float)oy);
        nvgLineTo(vg, curX, (float)oy);
        nvgStroke(vg);
    }

    nvgRestore(vg);
}

/* -------------------------------------------------------------------------
   Measurement
   ------------------------------------------------------------------------- */

int
Tk_MeasureChars(Tk_Font tkfont, const char *source, Tcl_Size numBytes,
                int maxLength, int flags, int *lengthPtr)
{
    UnixFont *uf = (UnixFont *) tkfont;
    int curX = 0;
    const char *p = source;
    const char *end = source + numBytes;
    const char *lastBreak = source;
    int lastBreakX = 0;
    int prev = 0;

    while (p < end) {
        int ch;
        const char *next = p + Tcl_UtfToUniChar(p, &ch);
        
        /* Check for line break opportunities */
        if (ch == ' ' || ch == '\t' || ch == '\n') {
            lastBreak = next;
            lastBreakX = curX;
        }
        
        SubFont *sf = FindSubFontForChar(uf, ch, NULL);
        if (!sf || !sf->familyPtr || !sf->familyPtr->fontInfo.userdata) {
            /* Use reasonable default width */
            curX += uf->pixelSize / 2;
            p = next;
            continue;
        }

        stbtt_fontinfo *info = &sf->familyPtr->fontInfo;
        float scale = stbtt_ScaleForPixelHeight(info, (float)uf->pixelSize);

        int adv, lsb;
        stbtt_GetCodepointHMetrics(info, ch, &adv, &lsb);
        int w = (int)(adv * scale + 0.5f);

        if (prev) {
            int kern = stbtt_GetCodepointKernAdvance(info, prev, ch);
            w += (int)(kern * scale + 0.5f);
        }

        if (maxLength >= 0 && curX + w > maxLength) {
            if (flags & TK_WHOLE_WORDS) {
                if (lastBreak > source) {
                    p = lastBreak;
                    curX = lastBreakX;
                }
            } else if (!(flags & TK_PARTIAL_OK)) {
                /* Don't include this character */
            } else {
                /* Include partial character */
                curX += w;
                p = next;
            }
            break;
        }

        curX += w;
        prev = ch;
        p = next;
    }

    if (lengthPtr) *lengthPtr = curX;
    return p - source;
}

/* -------------------------------------------------------------------------
   Text layout - more advanced measurement with context
   ------------------------------------------------------------------------- */

void
Tk_MeasureCharsInContext(Tk_Font tkfont, const char *source, Tcl_Size numBytes,
                         int rangeStart, Tcl_Size rangeLength, int maxLength,
                         int flags, int *lengthPtr)
{
    /* For simple implementation, delegate to Tk_MeasureChars on the range */
    if (rangeStart < 0) rangeStart = 0;
    if (rangeStart + rangeLength > numBytes) rangeLength = numBytes - rangeStart;
    
    Tk_MeasureChars(tkfont, source + rangeStart, rangeLength, maxLength, flags, lengthPtr);
}

void
Tk_DrawCharsInContext(Display *display, Drawable drawable, GC gc,
                      Tk_Font tkfont, const char *source, Tcl_Size numBytes,
                      int rangeStart, Tcl_Size rangeLength, int x, int y)
{
    /* For simple implementation, just draw the range */
    if (rangeStart < 0) rangeStart = 0;
    if (rangeStart + rangeLength > numBytes) rangeLength = numBytes - rangeStart;
    
    Tk_DrawChars(display, drawable, gc, tkfont, source + rangeStart, rangeLength, x, y);
}

/* -------------------------------------------------------------------------
   Text width calculation
   ------------------------------------------------------------------------- */

int
Tk_TextWidth(Tk_Font tkfont, const char *string, Tcl_Size numBytes)
{
    int length = 0;
    Tk_MeasureChars(tkfont, string, numBytes, -1, 0, &length);
    return length;
}

/* -------------------------------------------------------------------------
   Character position to X coordinate
   ------------------------------------------------------------------------- */

void
Tk_CharBbox(Tk_Font tkfont, const char *string, Tcl_Size index,
            int *xPtr, int *yPtr, int *widthPtr, int *heightPtr)
{
    UnixFont *uf = (UnixFont *) tkfont;
    
    /* Calculate X position up to index */
    int xPos = 0;
    if (index > 0) {
        Tk_MeasureChars(tkfont, string, index, -1, 0, &xPos);
    }
    
    /* Calculate width of character at index */
    int charWidth = 0;
    if (string[index] != '\0') {
        int ch;
        int len = Tcl_UtfToUniChar(string + index, &ch);
        Tk_MeasureChars(tkfont, string + index, len, -1, 0, &charWidth);
    }
    
    if (xPtr) *xPtr = xPos;
    if (yPtr) *yPtr = -uf->font.fm.ascent;
    if (widthPtr) *widthPtr = charWidth;
    if (heightPtr) *heightPtr = uf->font.fm.ascent + uf->font.fm.descent;
}

/* -------------------------------------------------------------------------
   X coordinate to character position
   ------------------------------------------------------------------------- */

Tcl_Size
Tk_PointToChar(Tk_Font tkfont, const char *string, Tcl_Size numBytes, int x)
{
    if (x <= 0) return 0;
    
    const char *p = string;
    const char *end = string + numBytes;
    int curX = 0;
    
    while (p < end) {
        int charWidth = 0;
        int ch;
        const char *next = p + Tcl_UtfToUniChar(p, &ch);
        Tk_MeasureChars(tkfont, p, next - p, -1, 0, &charWidth);
        
        if (curX + charWidth/2 >= x) {
            return p - string;
        }
        
        curX += charWidth;
        p = next;
    }
    
    return numBytes;
}

/* -------------------------------------------------------------------------
   Distance from character to X coordinate  
   ------------------------------------------------------------------------- */

int
Tk_DistanceToTextLayout(Tk_TextLayout layout, int x, int y)
{
    /* Simplified - would need full TextLayout implementation */
    if (x < 0) return -x;
    return x;
}

/* -------------------------------------------------------------------------
   Font enumeration
   ------------------------------------------------------------------------- */

void
TkpGetFontFamilies(Tcl_Interp *interp, Tk_Window tkwin)
{
    FcPattern *pat = FcPatternCreate();
    FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, NULL);
    FcFontSet *fs = FcFontList(NULL, pat, os);
    
    if (fs) {
        Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);
        Tcl_DString seen;
        Tcl_DStringInit(&seen);
        
        int i;
        for (i = 0; i < fs->nfont; i++) {
            FcChar8 *family = NULL;
            if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &family) == FcResultMatch) {
                if (!SeenName((char*)family, &seen)) {
                    Tcl_ListObjAppendElement(interp, resultObj, 
                                           Tcl_NewStringObj((char*)family, -1));
                }
            }
        }
        
        Tcl_DStringFree(&seen);
        FcFontSetDestroy(fs);
        Tcl_SetObjResult(interp, resultObj);
    }
    
    FcObjectSetDestroy(os);
    FcPatternDestroy(pat);
}

void
TkpGetSubFonts(Tcl_Interp *interp, Tk_Font tkfont)
{
    UnixFont *uf = (UnixFont *) tkfont;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);
    
    int i;
    for (i = 0; i < uf->numSubFonts; i++) {
        if (uf->subFontArray[i].familyPtr) {
            Tcl_ListObjAppendElement(interp, resultObj,
                Tcl_NewStringObj(uf->subFontArray[i].familyPtr->faceName, -1));
        }
    }
    
    Tcl_SetObjResult(interp, resultObj);
}

void
TkpGetFontAttrsForChar(Tk_Window tkwin, Tk_Font tkfont, int c, TkFontAttributes *faPtr)
{
    UnixFont *uf = (UnixFont *) tkfont;
    *faPtr = uf->font.fa;
    
    /* Optionally find which subfont would handle this character */
    SubFont *sf = FindSubFontForChar(uf, c, NULL);
    if (sf && sf->familyPtr && sf->familyPtr->faceName) {
        faPtr->family = sf->familyPtr->faceName;
    }
}

/* -------------------------------------------------------------------------
   Font listing (legacy X11 pattern matching)
   ------------------------------------------------------------------------- */

static char **
ListFonts(const char *pattern, int *countPtr)
{
    /* Simplified implementation - return empty list */
    char **result = Tcl_Alloc(sizeof(char*));
    result[0] = NULL;
    *countPtr = 0;
    return result;
}

/* -------------------------------------------------------------------------
   Angle support (rotated text)
   ------------------------------------------------------------------------- */

int
TkDrawAngledChars(Display *display, Drawable drawable, GC gc, Tk_Font tkfont,
                  const char *source, Tcl_Size numBytes, double x, double y,
                  double angle)
{
    UnixFont *uf = (UnixFont *) tkfont;
    NVGcontext *vg = GetNanoVGContext(NULL);
    if (!vg) return 0;

    nvgSave(vg);
    
    /* Apply rotation transform */
    nvgTranslate(vg, (float)x, (float)y);
    nvgRotate(vg, (float)(angle * NVG_PI / 180.0));
    
    /* Draw at origin after transform */
    Tk_DrawChars(display, drawable, gc, tkfont, source, numBytes, 0, 0);
    
    nvgRestore(vg);
    return 1;
}

int
TkDrawAngledTextLayout(Display *display, Drawable drawable, GC gc,
                       Tk_TextLayout layout, int x, int y, double angle,
                       int firstChar, int lastChar)
{
    /* Would need full TextLayout support */
    return 0;
}

/* -------------------------------------------------------------------------
   PostScript output (stub)
   ------------------------------------------------------------------------- */

int
TkPostscriptFontName(Tk_Font tkfont, Tcl_DString *dsPtr)
{
    UnixFont *uf = (UnixFont *) tkfont;
    
    /* Generate a PostScript-compatible font name */
    const char *family = uf->font.fa.family ? uf->font.fa.family : "Helvetica";
    Tcl_DStringAppend(dsPtr, family, -1);
    
    if (uf->font.fa.weight == TK_FW_BOLD) {
        Tcl_DStringAppend(dsPtr, "-Bold", -1);
    }
    if (uf->font.fa.slant == TK_FS_ITALIC) {
        Tcl_DStringAppend(dsPtr, "-Italic", -1);
    }
    
    return 0;
}

/* -------------------------------------------------------------------------
   Utility functions
   ------------------------------------------------------------------------- */

void
TkpDrawCharsInContext(Display *display, Drawable drawable, GC gc,
                      Tk_Font tkfont, const char *source, Tcl_Size numBytes,
                      int rangeStart, Tcl_Size rangeLength, int x, int y)
{
    /* Simple delegation to DrawChars with range */
    Tk_DrawCharsInContext(display, drawable, gc, tkfont, source, numBytes,
                         rangeStart, rangeLength, x, y);
}

int
TkpMeasureCharsInContext(Tk_Font tkfont, const char *source, Tcl_Size numBytes,
                         int rangeStart, Tcl_Size rangeLength, int maxLength,
                         int flags, int *lengthPtr)
{
    /* Simple delegation */
    if (rangeStart < 0) rangeStart = 0;
    if (rangeStart + rangeLength > numBytes) rangeLength = numBytes - rangeStart;
    
    return Tk_MeasureChars(tkfont, source + rangeStart, rangeLength, 
                          maxLength, flags, lengthPtr);
}

/* -------------------------------------------------------------------------
   Compatibility stubs
   ------------------------------------------------------------------------- */

void
TkUnixSetXftClipRegion(Region clipRegion)
{
    /* No-op for NanoVG - clipping handled differently */
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
