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

#include "tkInt.h"
#include "tkFont.h"
#include "tkUnixInt.h"          

#include <fontconfig/fontconfig.h>
#include <stb_truetype.h>
#include <nanovg.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Forward declarations and helpers. */
static NVGcontext *GetNanoVGContext(Tk_Window tkwin);
static NVGcolor    GetColorFromGC(GC gc);


/* Constants and structures. */
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
    char               *filePath;           /* Full path from fontconfig. */
    unsigned char      *fontBuffer;         /* Owned TTF/OTF file content. */
    int                 bufferSize;
    stbtt_fontinfo      fontInfo;
    char               *fontMap[FONTMAP_PAGES];
    int                 ascent, descent;    /* In pixels at nominal size. */
} FontFamily;

typedef struct SubFont {
    char              **fontMap;            /* Cached pointer to family->fontMap. */
    FontFamily         *familyPtr;
} SubFont;

typedef struct UnixFont {
    TkFont              font;               /* Generic part — must be first. */
    SubFont             staticSubFonts[SUBFONT_SPACE];
    int                 numSubFonts;
    SubFont            *subFontArray;
    SubFont             controlSubFont;
    int                 pixelSize;          /* Requested pixel size. */
    int                 widths[BASE_CHARS]; /* Fast path for ASCII */
    int                 underlinePos;
    int                 barHeight;
} UnixFont;

/* Thread-specific data. */
typedef struct {
    FontFamily         *fontFamilyList;
    FontFamily          controlFamily;
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/* Forward declaration of static functions. */

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

/*----------------------------------------------------------------------
 *
 * TkpFontPkgInit --
 *
 *	Initialize the platform-specific font package for Wayland.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes fontconfig and creates control font family.
 *
 *----------------------------------------------------------------------
 */
 
void
TkpFontPkgInit(TCL_UNUSED(TkMainInfo*)) /* mainPtr */
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

    /* Mark control chars + hex digits as present. */
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

/*----------------------------------------------------------------------
 *
 * FontPkgCleanup --
 *
 *	Clean up thread-specific font data on exit.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all allocated font family resources.
 *
 *----------------------------------------------------------------------
 */
 
static void
FontPkgCleanup(TCL_UNUSED(void*)) /* clientData */
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

/*----------------------------------------------------------------------
 *
 * TkpGetNativeFont --
 *
 *	Create a font from a native font name string.
 *
 * Results:
 *	Returns a TkFont pointer.
 *
 * Side effects:
 *	Allocates font resources.
 *
 *----------------------------------------------------------------------
 */
 
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

/*----------------------------------------------------------------------
 *
 * TkpGetFontFromAttributes --
 *
 *	Create or modify a font from font attributes.
 *
 * Results:
 *	Returns a TkFont pointer.
 *
 * Side effects:
 *	Allocates or reallocates font resources.
 *
 *----------------------------------------------------------------------
 */
 
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

    /* Always have at least the control subfont. */
    InitSubFont(&uf->controlSubFont,
                &((ThreadSpecificData*)Tcl_GetThreadData(&dataKey,sizeof(ThreadSpecificData)))->controlFamily,
                uf->pixelSize);

    return (TkFont *) uf;
}

/*----------------------------------------------------------------------
 *
 * InitFont --
 *
 *	Initialize a UnixFont structure with the given attributes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets up font metrics, subfonts, and ASCII width cache.
 *
 *----------------------------------------------------------------------
 */
 
static void
InitFont(TCL_UNUSED(Tk_Window), const TkFontAttributes *fa, UnixFont *uf)
{
    uf->font.fa = *fa;

    /* Create primary subfont. */
    FontFamily *primary = AllocFontFamily(fa->family ? fa->family : "sans-serif", uf->pixelSize);
    if (primary) {
        uf->subFontArray = uf->staticSubFonts;
        InitSubFont(&uf->subFontArray[0], primary, uf->pixelSize);
        uf->numSubFonts = 1;
    }

    /* Calculate font metrics. */
    if (primary && primary->fontInfo.userdata) {
        uf->font.fm.ascent  = primary->ascent;
        uf->font.fm.descent = primary->descent;
        uf->font.fm.maxWidth = uf->pixelSize * 2;  /* Conservative estimate. */
        uf->font.fm.fixed = 0;  /* TrueType fonts are generally not fixed-width. */
    } else {
        /* Fallback metrics. */
        uf->font.fm.ascent  = (int)(uf->pixelSize * 0.8 + 0.5);
        uf->font.fm.descent = (int)(uf->pixelSize * 0.2 + 0.5);
        uf->font.fm.maxWidth = uf->pixelSize;
        uf->font.fm.fixed = 0;
    }

    /* Underline / overstrike geometry (approximation). */
    uf->underlinePos = -2;
    uf->barHeight    = 1;
    if (primary) {
        uf->underlinePos = (int)(primary->descent * 0.4);
        uf->barHeight    = (int)(uf->pixelSize * 0.07 + 0.5);
        if (uf->barHeight < 1) uf->barHeight = 1;
    }

    /* Fast-path ASCII widths. */
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

/*----------------------------------------------------------------------
 *
 * InitSubFont --
 *
 *	Initialize a SubFont structure with a font family.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Increments the font family reference count.
 *
 *----------------------------------------------------------------------
 */
 
static void
InitSubFont(SubFont *sf, FontFamily *family, TCL_UNUSED(int) /*pixelSize*/)
{
    sf->familyPtr = family;
    sf->fontMap   = family->fontMap;
    family->refCount++;
}

/*----------------------------------------------------------------------
 *
 * TkpDeleteFont --
 *
 *	Delete a font and free its resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees font memory.
 *
 *----------------------------------------------------------------------
 */
 
void
TkpDeleteFont(TkFont *tkf)
{
    ReleaseFont((UnixFont *) tkf);
}

/*----------------------------------------------------------------------
 *
 * ReleaseFont --
 *
 *	Release all resources associated with a UnixFont.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees font memory and decrements reference counts.
 *
 *----------------------------------------------------------------------
 */
 
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

/*----------------------------------------------------------------------
 *
 * ReleaseSubFont --
 *
 *	Release a SubFont and decrement its family reference count.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May free the font family if reference count reaches zero.
 *
 *----------------------------------------------------------------------
 */
 
static void
ReleaseSubFont(SubFont *sf)
{
    if (sf->familyPtr) {
        FreeFontFamily(sf->familyPtr);
        sf->familyPtr = NULL;
    }
}

/*----------------------------------------------------------------------
 *
 * AllocFontFamily --
 *
 *	Allocate and load a FontFamily structure for a given face name.
 *
 * Results:
 *	Returns a FontFamily pointer, or NULL on failure.
 *
 * Side effects:
 *	Loads font file, parses font data, caches the family.
 *
 *----------------------------------------------------------------------
 */
 
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

/*----------------------------------------------------------------------
 *
 * FreeFontFamily --
 *
 *	Free a FontFamily and its resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees memory and removes from cache.
 *
 *----------------------------------------------------------------------
 */
 
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

/*----------------------------------------------------------------------
 *
 * FindSubFontForChar --
 *
 *	Find a subfont that can render the given character.
 *
 * Results:
 *	Returns a SubFont pointer that can render the character.
 *
 * Side effects:
 *	May load new fallback fonts.
 *
 *----------------------------------------------------------------------
 */
 
static SubFont *
FindSubFontForChar(UnixFont *uf, int ch, SubFont **fixPtr)
{
    if (ch < 0 || ch >= FONTMAP_NUMCHARS) ch = 0xFFFD;
    
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

    /* Try same family different encoding/style variant. */
    if (SeenName(uf->font.fa.family, &tried) == 0) {
        SubFont *sf = CanUseFallback(uf, uf->font.fa.family, ch, fixPtr);
        if (sf) {
            Tcl_DStringFree(&tried);
            return sf;
        }
    }

    /* Try known fallback families. */
    const char *const *fallbacks = TkFontGetFallbacks()[0]; /* Simplistic. */
    for (i = 0; fallbacks && fallbacks[i]; i++) {
        if (SeenName(fallbacks[i], &tried) == 0) {
            SubFont *sf = CanUseFallback(uf, fallbacks[i], ch, fixPtr);
            if (sf) {
                Tcl_DStringFree(&tried);
                return sf;
            }
        }
    }

    /* Last resort — give up and use control font. */
    FontMapInsert(&uf->controlSubFont, ch);
    Tcl_DStringFree(&tried);
    return &uf->controlSubFont;
}

/*----------------------------------------------------------------------
 *
 * FontMapLookup --
 *
 *	Check if a character is present in a subfont's glyph map.
 *
 * Results:
 *	Returns 1 if character is present, 0 otherwise.
 *
 * Side effects:
 *	May load font map page if not already loaded.
 *
 *----------------------------------------------------------------------
 */
 
static int
FontMapLookup(SubFont *sf, int ch)
{
    if (ch < 0 || ch >= FONTMAP_NUMCHARS) return 0;
    int page = ch >> FONTMAP_SHIFT;
    if (!sf->fontMap[page]) FontMapLoadPage(sf, page);
    int bit = ch & (FONTMAP_BITSPERPAGE - 1);
    return (sf->fontMap[page][bit >> 3] >> (bit & 7)) & 1;
}

/*----------------------------------------------------------------------
 *
 * FontMapInsert --
 *
 *	Mark a character as present in a subfont's glyph map.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates font map bitmap.
 *
 *----------------------------------------------------------------------
 */
 
static void
FontMapInsert(SubFont *sf, int ch)
{
    if (ch < 0 || ch >= FONTMAP_NUMCHARS) return;
    int page = ch >> FONTMAP_SHIFT;
    if (!sf->fontMap[page]) FontMapLoadPage(sf, page);
    int bit = ch & (FONTMAP_BITSPERPAGE - 1);
    sf->fontMap[page][bit >> 3] |= (1 << (bit & 7));
}

/*----------------------------------------------------------------------
 *
 * FontMapLoadPage --
 *
 *	Load a page of glyph presence information from font data.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates and initializes font map page.
 *
 *----------------------------------------------------------------------
 */
 
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

/*----------------------------------------------------------------------
 *
 * CanUseFallback --
 *
 *	Check if a fallback font can render a character.
 *
 * Results:
 *	Returns SubFont pointer if successful, NULL otherwise.
 *
 * Side effects:
 *	May allocate new subfont and add to font.
 *
 *----------------------------------------------------------------------
 */
 
static SubFont *
CanUseFallback(UnixFont *uf, const char *face, int ch, SubFont **fix)
{
    FontFamily *fam = AllocFontFamily(face, uf->pixelSize);
    if (!fam || !fam->fontInfo.userdata) {
        if (fam) FreeFontFamily(fam);
        return NULL;
    }

    /* Quick check if this font probably contains the glyph. */
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
    FontMapInsert(sf, ch);      /* Mark it usable. */
    return sf;
}

/*----------------------------------------------------------------------
 *
 * SeenName --
 *
 *	Check if a font name has already been tried for fallback.
 *
 * Results:
 *	Returns 1 if name has been seen, 0 otherwise.
 *
 * Side effects:
 *	Adds name to tried list if not already present.
 *
 *----------------------------------------------------------------------
 */
 
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

/*----------------------------------------------------------------------
 *
 * Tk_DrawChars --
 *
 *	Draw characters to a drawable using NanoVG.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders text to the display.
 *
 *----------------------------------------------------------------------
 */
 
void
Tk_DrawChars(
	TCL_UNUSED(Display *), 
	TCL_UNUSED(Drawable), 
	GC gc, 
	Tk_Font tkfont,
    const char *text, 
    Tcl_Size numBytes, 
    int x, 
    int y)
{
    UnixFont *uf = (UnixFont *) tkfont;
    
    /* 
     * Get NanoVG context. TO DO: We need a proper Tk_Window for this.
     * This should be extracted from the drawable/display.
     */
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
            /* Flush previous run. */
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

    /* Flush final run. */
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

    /* Underline */
    if (uf->font.fa.underline) {
        nvgBeginPath(vg);
        nvgStrokeColor(vg, GetColorFromGC(gc));
        nvgStrokeWidth(vg, (float)uf->barHeight);
        nvgMoveTo(vg, (float)x, (float)(y + uf->underlinePos));
        nvgLineTo(vg, curX, (float)(y + uf->underlinePos));
        nvgStroke(vg);
    }
    
    /* Overstrike. */
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

/*----------------------------------------------------------------------
 *
 * Tk_MeasureChars --
 *
 *	Measure the pixel width of characters up to a maximum length.
 *
 * Results:
 *	Returns the number of bytes that fit within maxLength.
 *
 * Side effects:
 *	Sets lengthPtr to the pixel width of the measured text.
 *
 *----------------------------------------------------------------------
 */
 
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
        
        /* Check for line break opportunities. */
        if (ch == ' ' || ch == '\t' || ch == '\n') {
            lastBreak = next;
            lastBreakX = curX;
        }
        
        SubFont *sf = FindSubFontForChar(uf, ch, NULL);
        if (!sf || !sf->familyPtr || !sf->familyPtr->fontInfo.userdata) {
            /* Use reasonable default width. */
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
                /* Don't include this character. */
            } else {
                /* Include partial character. */
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

/*----------------------------------------------------------------------
 *
 * Tk_MeasureCharsInContext --
 *
 *	Measure characters within a specific byte range.
 *
 * Results:
 *	Returns number of bytes that fit within maxLength.
 *
 * Side effects:
 *	Sets lengthPtr to pixel width.
 *
 *----------------------------------------------------------------------
 */
 
int
Tk_MeasureCharsInContext(Tk_Font tkfont, const char *source, Tcl_Size numBytes,
                         Tcl_Size rangeStart, Tcl_Size rangeLength,
                         int maxLength, int flags, int *lengthPtr)
{
    /* Clip range. */
    if (rangeStart < 0) {
        rangeStart = 0;
    }
    if (rangeStart > numBytes) {
        rangeStart = numBytes;
    }
    if (rangeLength < 0 || rangeStart + rangeLength > numBytes) {
        rangeLength = numBytes - rangeStart;
    }

    return Tk_MeasureChars(tkfont, source + rangeStart, rangeLength,
                           maxLength, flags, lengthPtr);
}

/*----------------------------------------------------------------------
 *
 * Tk_DrawCharsInContext --
 *
 *	Draw characters within a specific byte range.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders text to display.
 *
 *----------------------------------------------------------------------
 */
 
void
Tk_DrawCharsInContext(Display *display, Drawable drawable, GC gc,
                      Tk_Font tkfont, const char *source, Tcl_Size numBytes,
                      Tcl_Size rangeStart, Tcl_Size rangeLength,
                      int x, int y)
{
    if (rangeStart < 0) rangeStart = 0;
    if (rangeStart > numBytes) rangeStart = numBytes;
    if (rangeLength < 0 || rangeStart + rangeLength > numBytes) {
        rangeLength = numBytes - rangeStart;
    }

    Tk_DrawChars(display, drawable, gc, tkfont,
                 source + rangeStart, rangeLength, x, y);
}

/*----------------------------------------------------------------------
 *
 * TkpGetFontFamilies --
 *
 *	Get list of available font families from fontconfig.
 *
 * Results:
 *	Sets interp result to list of font family names.
 *
 * Side effects:
 *	Queries fontconfig database.
 *
 *----------------------------------------------------------------------
 */
 
void
TkpGetFontFamilies(
	Tcl_Interp *interp, 
	TCL_UNUSED(Tk_Window)) /* tkwin */
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

/*----------------------------------------------------------------------
 *
 * TkpGetSubFonts --
 *
 *	Get list of subfonts used by a font.
 *
 * Results:
 *	Sets interp result to list of subfont family names.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
 
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

/*----------------------------------------------------------------------
 *
 * TkpGetFontAttrsForChar --
 *
 *	Get font attributes that would be used to render a character.
 *
 * Results:
 *	Sets faPtr to appropriate font attributes.
 *
 * Side effects:
 *	May trigger font fallback lookup.
 *
 *----------------------------------------------------------------------
 */
 
void
TkpGetFontAttrsForChar(
	TCL_UNUSED(Tk_Window), 
	Tk_Font tkfont, 
	int c, 
	TkFontAttributes *faPtr)
{
    UnixFont *uf = (UnixFont *) tkfont;
    *faPtr = uf->font.fa;
    
    /* Optionally find which subfont would handle this character. */
    SubFont *sf = FindSubFontForChar(uf, c, NULL);
    if (sf && sf->familyPtr && sf->familyPtr->faceName) {
        faPtr->family = sf->familyPtr->faceName;
    }
}

/*----------------------------------------------------------------------
 *
 * ListFonts --
 *
 *	List fonts matching a pattern (stub implementation).
 *
 * Results:
 *	Returns empty font list.
 *
 * Side effects:
 *	Allocates empty result array.
 *
 *----------------------------------------------------------------------
 */
 
static char **
ListFonts(
	TCL_UNUSED(const char *), /* pattern */
	int *countPtr)
{
    /* Simplified implementation - return empty list. */
    char **result = Tcl_Alloc(sizeof(char*));
    result[0] = NULL;
    *countPtr = 0;
    return result;
}

/*----------------------------------------------------------------------
 *
 * TkDrawAngledChars --
 *
 *	Draw characters at an angle using NanoVG.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders rotated text to display.
 *
 *----------------------------------------------------------------------
 */
 
void
TkDrawAngledChars(Display *display, Drawable drawable, GC gc, Tk_Font tkfont,
                  const char *source, Tcl_Size numBytes,
                  double x, double y, double angle)
{
    NVGcontext *vg = GetNanoVGContext(NULL);
    if (!vg) {
        return;
    }

    nvgSave(vg);

    /* Apply rotation transform. */
    nvgTranslate(vg, (float)x, (float)y);
    nvgRotate(vg, (float)(angle * NVG_PI / 180.0));

    /* Draw at origin after transform .*/
    Tk_DrawChars(display, drawable, gc, tkfont, source, numBytes, 0, 0);

    nvgRestore(vg);
}

/*----------------------------------------------------------------------
 *
 * TkPostscriptFontName --
 *
 *	Generate PostScript-compatible font name.
 *
 * Results:
 *	Returns 0 on success.
 *
 * Side effects:
 *	Appends font name to DString.
 *
 *----------------------------------------------------------------------
 */
 
int
TkPostscriptFontName(Tk_Font tkfont, Tcl_DString *dsPtr)
{
    UnixFont *uf = (UnixFont *) tkfont;
    
    /* Generate a PostScript-compatible font name. */
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

/*----------------------------------------------------------------------
 *
 * TkpDrawCharsInContext --
 *
 *	Draw characters in context (range-based drawing).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders text to display.
 *
 *----------------------------------------------------------------------
 */
 
void
TkpDrawCharsInContext(Display *display, Drawable drawable, GC gc,
                      Tk_Font tkfont, const char *source, Tcl_Size numBytes,
                      int rangeStart, Tcl_Size rangeLength, int x, int y)
{
    /* Simple delegation to DrawChars with range. */
    Tk_DrawCharsInContext(display, drawable, gc, tkfont, source, numBytes,
                         rangeStart, rangeLength, x, y);
}

/*----------------------------------------------------------------------
 *
 * TkpMeasureCharsInContext --
 *
 *	Measure characters in context (range-based measurement).
 *
 * Results:
 *	Returns number of bytes that fit.
 *
 * Side effects:
 *	Sets lengthPtr to pixel width.
 *
 *----------------------------------------------------------------------
 */
 
int
TkpMeasureCharsInContext(Tk_Font tkfont, const char *source, Tcl_Size numBytes,
                         int rangeStart, Tcl_Size rangeLength, int maxLength,
                         int flags, int *lengthPtr)
{
    /* Simple delegation. */
    if (rangeStart < 0) rangeStart = 0;
    if (rangeStart + rangeLength > numBytes) rangeLength = numBytes - rangeStart;
    
    return Tk_MeasureChars(tkfont, source + rangeStart, rangeLength, 
                          maxLength, flags, lengthPtr);
}

/*----------------------------------------------------------------------
 *
 * TkUnixSetXftClipRegion --
 *
 *	Set clip region (stub for NanoVG compatibility).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
 
void
TkUnixSetXftClipRegion(Region clipRegion)
{
    /* No-op for NanoVG - clipping handled differently */
}

/*----------------------------------------------------------------------
 *
 * GetNanoVGContext --
 *
 *	Get NanoVG context for a Tk window (stub implementation).
 *
 * Results:
 *	Returns NULL (needs proper implementation).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
 
static NVGcontext *
GetNanoVGContext(Tk_Window tkwin)
{
    /* TO DO: Implement proper way to get NanoVG context from Tk window */
    return NULL;
}

/*----------------------------------------------------------------------
 *
 * GetColorFromGC --
 *
 *	Extract color from X11 GC (stub implementation).
 *
 * Results:
 *	Returns black color (needs proper implementation).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
 
static NVGcolor
GetColorFromGC(GC gc)
{
    /* TO DO: Extract actual color from GC */
    NVGcolor color = {0, 0, 0, 1};  /* Black */
    return color;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
