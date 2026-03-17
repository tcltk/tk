/*
 * tkUnixShaper.c --
 *
 *      Implementation of bidirectional text analysis and glyph shaping.
 *		Uses SheenBidi for bidi levels and kb_text_shaper
 *      for glyph shaping and positioning.
 *
 *      Provides a 64-slot LRU cache of shaped glyph sequences keyed by exact
 *      UTF-8 string bytes.
 *
 *      Includes fast-path for simple LTR text to avoid expensive bidi/shaping.
 *
 * Copyright (c) 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#define KB_TEXT_SHAPE_IMPLEMENTATION
#include "kb_text_shaper.h"
#include <SheenBidi/SheenBidi.h>

#include "tkUnixInt.h"
#include "tkFont.h"
#include "tkUnixShaper.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#define X11SHAPE_CACHE_SIZE     64
#define KBTS_FONT_CACHE_SIZE    64


/* Data on bidi runs. */
typedef struct {
    int offset;          /* byte offset in original UTF-8 */
    int length;          /* length in bytes */
    SBLevel level;
    int     isRTL;       /* (level & 1) */
} BidiRun;

/* One slot in the LRU cache. */
typedef struct {
    char           *text;           /* owned copy of the UTF-8 key */
    int             textLen;
    X11ShapedGlyph *glyphs;         /* owned array, grows on demand */
    int             glyphCount;
    int             glyphCapacity;
    int             totalWidth;     /* sum of advanceX values */
    int             lruSeq;         /* for LRU eviction */
} X11ShapeCacheEntry;

/* Font data cache. */
typedef struct {
    char   path[PATH_MAX];
    int    index;
    void  *fontData;
    int    fontDataSize;
} KbtsFontDataCacheEntry;

static KbtsFontDataCacheEntry kbtsFontDataCache[KBTS_FONT_CACHE_SIZE];
static int                    kbtsFontDataCacheCount = 0;
TCL_DECLARE_MUTEX(kbtsFontCacheMutex);

/* Forward declarations */
static kbts_font *    TclpKbtsFontCacheLookupOrLoad(kbts_shape_context *ctx,
                          const char *path, int index);
static void           TclpKbtsFontCacheDestroy(void);
static int            TclpGetBidiRuns(const char *utf8, int len, BidiRun *runsOut, int maxRuns);
static int            TclpIsSimpleLTRText(const char *utf8, int len);
static void           TclpShaperShapeString(UnixFtFont *fontPtr, const char *source, int numBytes);
static X11ShapeCacheEntry * TclpShaperLastShaped(X11Shape *shapePtr);
static int            TclpUtf8ToUcs4(const char *source, FcChar32 *c, int numBytes);
static XftFont *      TclpGetFontForFace(UnixFtFont *fontPtr, int faceIndex);
static void           TclpEvictCacheSlot(X11Shape *shapePtr, int slot);
static int            TclpFindLRUSlot(X11Shape *shapePtr);
static void           TclpInitFontMap(UnixFtFont *fontPtr, X11Shape *shapePtr);
static void           TclpProcessSimpleLTR(UnixFtFont *fontPtr, X11Shape *shapePtr, 
                          const char *source, int numBytes, int slot);
static void           TclpProcessComplexText(UnixFtFont *fontPtr, X11Shape *shapePtr,
                          const char *source, int numBytes, int slot, 
                          BidiRun *bidiRuns, int numBidiRuns);
static XftFont *      TclpFindXftFontForKbFont(UnixFtFont *fontPtr, kbts_font *kbFont);
static void           TclpX11ShapeInit(X11Shape *s);
static void           TclpX11ShapeDestroy(X11Shape *s);

/*
 *----------------------------------------------------------------------
 *
 * Tk_ShapeText --
 *
 *	Shape text using the specified font, applying bidirectional
 *	analysis and glyph shaping as needed. Results are cached for
 *	performance.
 *
 * Results:
 *	Returns a pointer to a TkShapedTextBuffer structure containing
 *	the shaped glyphs, total width, and text length. Returns an
 *	empty buffer on error or if no shaping was performed.
 *
 * Side effects:
 *	Updates the shaping cache for the font.
 *
 *----------------------------------------------------------------------
 */
TkShapedTextBuffer *
Tk_ShapeText(Tk_Font tkfont, const char *utf8, int numBytes)
{
    if (!tkfont || !utf8 || numBytes <= 0) {
        static TkShapedTextBuffer empty = {0, NULL, 0, 0};
        return &empty;
    }

    UnixFtFont *fontPtr = (UnixFtFont *) tkfont;

    TclpShaperShapeString(fontPtr, utf8, numBytes);

    X11ShapeCacheEntry *entry = TclpShaperLastShaped(&fontPtr->shape);
    if (!entry || entry->glyphCount == 0) {
        static TkShapedTextBuffer empty = {0, NULL, 0, 0};
        return &empty;
    }

    static TkShapedTextBuffer result;
    result.glyphCount   = entry->glyphCount;
    result.glyphs       = entry->glyphs;
    result.totalWidth   = entry->totalWidth;
    result.textLenBytes = entry->textLen;
    return &result;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_ShaperDestroy --
 *
 *	Clean up shaping resources associated with a font.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees shaping context and cache entries for the font.
 *
 *----------------------------------------------------------------------
 */
void
Tk_ShaperDestroy(Tk_Font tkfont)
{
    if (tkfont) {
        UnixFtFont *fontPtr = (UnixFtFont *) tkfont;
        TclpX11ShapeDestroy(&fontPtr->shape);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TclpX11ShapeInit --
 *
 *	Initialize a new X11Shape structure for text shaping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates shaping context and initializes cache entries.
 *
 *----------------------------------------------------------------------
 */
static void
TclpX11ShapeInit(X11Shape *s)
{
    int i;
    memset(s, 0, sizeof(*s));
    s->context = kbts_CreateShapeContext(NULL, NULL);
    for (i = 0; i < X11SHAPE_CACHE_SIZE; i++) {
        s->cache[i].textLen       = -1;
        s->cache[i].text          = NULL;
        s->cache[i].glyphs        = NULL;
        s->cache[i].glyphCapacity = 0;
        s->cache[i].glyphCount    = 0;
        s->cache[i].lruSeq        = 0;
    }
    s->lruCounter = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpX11ShapeDestroy --
 *
 *	Clean up and free all resources associated with an X11Shape structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees shaping context, cache entries, and associated memory.
 *
 *----------------------------------------------------------------------
 */
static void
TclpX11ShapeDestroy(X11Shape *s)
{
    int i;
    if (s->context) {
        kbts_DestroyShapeContext(s->context);
        s->context = NULL;
    }
    for (i = 0; i < X11SHAPE_CACHE_SIZE; i++) {
        if (s->cache[i].text) {
            Tcl_Free(s->cache[i].text);
            s->cache[i].text = NULL;
        }
        if (s->cache[i].glyphs) {
            Tcl_Free(s->cache[i].glyphs);
            s->cache[i].glyphs = NULL;
        }
        s->cache[i].textLen       = -1;
        s->cache[i].glyphCount    = 0;
        s->cache[i].glyphCapacity = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TclpKbtsFontCacheLookupOrLoad --
 *
 *	Look up a font in the font data cache, or load it from disk
 *	if not found.
 *
 * Results:
 *	Returns a pointer to a kbts_font structure, or NULL on error.
 *
 * Side effects:
 *	May load font data from disk and add it to the cache.
 *
 *----------------------------------------------------------------------
 */
static kbts_font *
TclpKbtsFontCacheLookupOrLoad(kbts_shape_context *ctx,
                          const char *path, int index)
{
    int i;
    void *fontData = NULL;
    int fontDataSize = 0;

    Tcl_MutexLock(&kbtsFontCacheMutex);
    for (i = 0; i < kbtsFontDataCacheCount; i++) {
        if (kbtsFontDataCache[i].index == index &&
            strcmp(kbtsFontDataCache[i].path, path) == 0) {
            fontData     = kbtsFontDataCache[i].fontData;
            fontDataSize = kbtsFontDataCache[i].fontDataSize;
            break;
        }
    }
    Tcl_MutexUnlock(&kbtsFontCacheMutex);

    if (!fontData) {
        FILE *fp = fopen(path, "rb");
        if (!fp) return NULL;

        fseek(fp, 0, SEEK_END);
        long fileSize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (fileSize <= 0) { fclose(fp); return NULL; }

        fontData = Tcl_Alloc((size_t)fileSize);
        if (fread(fontData, 1, (size_t)fileSize, fp) != (size_t)fileSize) {
            fclose(fp);
            Tcl_Free(fontData);
            return NULL;
        }
        fclose(fp);
        fontDataSize = (int)fileSize;

        Tcl_MutexLock(&kbtsFontCacheMutex);
        if (kbtsFontDataCacheCount < KBTS_FONT_CACHE_SIZE) {
            strncpy(kbtsFontDataCache[kbtsFontDataCacheCount].path, path, PATH_MAX-1);
            kbtsFontDataCache[kbtsFontDataCacheCount].path[PATH_MAX-1] = '\0';
            kbtsFontDataCache[kbtsFontDataCacheCount].index        = index;
            kbtsFontDataCache[kbtsFontDataCacheCount].fontData     = fontData;
            kbtsFontDataCache[kbtsFontDataCacheCount].fontDataSize = fontDataSize;
            kbtsFontDataCacheCount++;
            Tcl_MutexUnlock(&kbtsFontCacheMutex);
        } else {
            Tcl_MutexUnlock(&kbtsFontCacheMutex);
            Tcl_Free(fontData);
            return kbts_ShapePushFontFromFile(ctx, path, index);
        }
    }

    return kbts_ShapePushFontFromMemory(ctx, fontData, fontDataSize, index);
}

/*
 *----------------------------------------------------------------------
 *
 * TclpKbtsFontCacheDestroy --
 *
 *	Clean up and free all resources in the global font data cache.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all cached font data.
 *
 *----------------------------------------------------------------------
 */
static void
TclpKbtsFontCacheDestroy(void)
{
    int i;
    Tcl_MutexLock(&kbtsFontCacheMutex);
    for (i = 0; i < kbtsFontDataCacheCount; i++) {
        if (kbtsFontDataCache[i].fontData) {
            Tcl_Free(kbtsFontDataCache[i].fontData);
            kbtsFontDataCache[i].fontData = NULL;
        }
    }
    kbtsFontDataCacheCount = 0;
    Tcl_MutexUnlock(&kbtsFontCacheMutex);
}

/*
 *----------------------------------------------------------------------
 *
 * TclpIsSimpleLTRText --
 *
 *	Check if text is simple LTR (no codepoints ≥ U+0590).
 *	Used as a fast-path to avoid expensive bidi/shaping.
 *
 * Results:
 *	Returns 1 if text is simple LTR, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
TclpIsSimpleLTRText(const char *utf8, int len)
{
    const unsigned char *p = (const unsigned char *)utf8;
    int i;
    for (i = 0; i < len; i++) {
        if (p[i] > 0x7F) {
            FcChar32 u;
            Tcl_UtfToUniChar((const char *)(p + i), (int *)&u);
            if (u >= 0x0590) {  /* Hebrew block and higher → potential RTL/complex */
                return 0;
            }
        }
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpGetBidiRuns --
 *
 *	Perform full bidirectional analysis on text to determine
 *	directional runs. Only called when fast-path fails.
 *
 * Results:
 *	Returns the number of bidirectional runs found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
TclpGetBidiRuns(const char *utf8, int len, BidiRun *runsOut, int maxRuns)
{
    int needsBidi = 0;
    const unsigned char *p = (const unsigned char *)utf8;
    int i;
    for (i = 0; i < len; i++) {
        if (p[i] > 0x7F) {
            FcChar32 u;
            Tcl_UtfToUniChar((const char *)(p + i), (int *)&u);
            if (u >= 0x0590) {
                needsBidi = 1;
                break;
            }
        }
    }

    if (!needsBidi) {
        runsOut[0].offset = 0;
        runsOut[0].length = len;
        runsOut[0].level  = 0;
        runsOut[0].isRTL  = 0;
        return 1;
    }

    SBAlgorithmRef bidiAlg = NULL;
    SBParagraphRef paragraph = NULL;
    SBUInteger cpCount = 0;
    SBUInteger codepoints[1024];

    int byteIdx = 0;
    while (byteIdx < len && cpCount < 1024) {
        FcChar32 c;
        int clen = TclpUtf8ToUcs4(utf8 + byteIdx, &c, len - byteIdx);
        if (clen <= 0) break;
        codepoints[cpCount++] = (SBUInteger)c;
        byteIdx += clen;
    }

    SBCodepointSequence codepointSeq = {SBStringEncodingUTF32, codepoints, cpCount};
    bidiAlg = SBAlgorithmCreate(&codepointSeq);
    if (!bidiAlg) return 0;

    paragraph = SBAlgorithmCreateParagraph(bidiAlg, 0, cpCount, SBLevelDefaultLTR);
    if (!paragraph) {
        SBAlgorithmRelease(bidiAlg);
        return 0;
    }

    SBLineRef line = SBParagraphCreateLine(paragraph, 0, cpCount);
    if (!line) {
        SBParagraphRelease(paragraph);
        SBAlgorithmRelease(bidiAlg);
        return 0;
    }

    SBUInteger lineRunCount = SBLineGetRunCount(line);
    const SBRun *runs = SBLineGetRunsPtr(line);

    int cpToByteStack[1025];
    int *cpToByte = (cpCount <= 1024) ? cpToByteStack
                                      : (int *)malloc((cpCount + 1) * sizeof(int));

    byteIdx = 0;
    for (SBUInteger j = 0; j < cpCount; j++) {
        cpToByte[j] = byteIdx;
        FcChar32 dummy;
        byteIdx += TclpUtf8ToUcs4(utf8 + byteIdx, &dummy, len - byteIdx);
    }
    cpToByte[cpCount] = len;

    int runCount = 0;
    for (SBUInteger j = 0; j < lineRunCount && runCount < maxRuns; j++) {
        runsOut[runCount].offset  = cpToByte[runs[j].offset];
        runsOut[runCount].length  = cpToByte[runs[j].offset + runs[j].length]
                                  - runsOut[runCount].offset;
        runsOut[runCount].level   = runs[j].level;
        runsOut[runCount].isRTL   = (runs[j].level & 1);
        runCount++;
    }

    if (cpToByte != cpToByteStack) free(cpToByte);

    SBLineRelease(line);
    SBParagraphRelease(paragraph);
    SBAlgorithmRelease(bidiAlg);

    return runCount;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpUtf8ToUcs4 --
 *
 *	Convert UTF-8 to UCS-4 codepoint, with fallback to FcUtf8ToUcs4
 *	if Tcl_UtfToUniChar can't handle the byte count.
 *
 * Results:
 *	Returns the number of bytes consumed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
TclpUtf8ToUcs4(const char *source, FcChar32 *c, int numBytes)
{
    if (numBytes >= 6) {
        return Tcl_UtfToUniChar(source, (int *)c);
    }
    return FcUtf8ToUcs4((const FcChar8 *)source, c, numBytes);
}

/*
 *----------------------------------------------------------------------
 *
 * TclpGetFontForFace --
 *
 *	Get the XftFont for a given face index, with fallback handling.
 *
 * Results:
 *	Returns a pointer to an XftFont structure.
 *
 * Side effects:
 *	May call GetFont to load the font if not already loaded.
 *
 *----------------------------------------------------------------------
 */
static XftFont *
TclpGetFontForFace(UnixFtFont *fontPtr, int faceIndex)
{
    if (faceIndex >= 0 && faceIndex < fontPtr->nfaces) {
        XftFont *font = fontPtr->faces[faceIndex].ft0Font;
        if (font) return font;
    }
    return GetFont(fontPtr, 0, 0.0);  /* fallback to primary face */
}

/*
 *----------------------------------------------------------------------
 *
 * TclpFindLRUSlot --
 *
 *	Find the least recently used cache slot for eviction.
 *
 * Results:
 *	Returns the slot index to use.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static int
TclpFindLRUSlot(X11Shape *shapePtr)
{
    int i;
    int lruMin = shapePtr->cache[0].lruSeq;
    int lruMinSlot = 0;

    for (i = 1; i < X11SHAPE_CACHE_SIZE; i++) {
        if (shapePtr->cache[i].textLen < 0) {
            return i;  /* Found empty slot */
        }
        if (shapePtr->cache[i].lruSeq < lruMin) {
            lruMin = shapePtr->cache[i].lruSeq;
            lruMinSlot = i;
        }
    }
    return lruMinSlot;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpEvictCacheSlot --
 *
 *	Clean up and free resources for a cache slot being evicted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees text and glyph memory for the slot.
 *
 *----------------------------------------------------------------------
 */
static void
TclpEvictCacheSlot(X11Shape *shapePtr, int slot)
{
    if (shapePtr->cache[slot].text) {
        Tcl_Free(shapePtr->cache[slot].text);
        shapePtr->cache[slot].text = NULL;
    }
    if (shapePtr->cache[slot].glyphs) {
        Tcl_Free(shapePtr->cache[slot].glyphs);
        shapePtr->cache[slot].glyphs = NULL;
    }
    shapePtr->cache[slot].textLen = -1;
    shapePtr->cache[slot].glyphCount = 0;
    shapePtr->cache[slot].glyphCapacity = 0;
    shapePtr->cache[slot].totalWidth = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpInitFontMap --
 *
 *	Initialize the font mapping between kbts_font and face indices.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Populates the fontMap array in the X11Shape structure.
 *
 *----------------------------------------------------------------------
 */
static void
TclpInitFontMap(UnixFtFont *fontPtr, X11Shape *shapePtr)
{
    int i;
    
    if (!shapePtr->context) {
        TclpX11ShapeInit(shapePtr);
    }

    for (i = 0; i < fontPtr->nfaces && i < 8; i++) {
        FcPattern *facePattern = fontPtr->faces[i].source;
        FcChar8 *file = NULL;
        int index = 0;

        if (FcPatternGetString(facePattern, FC_FILE, 0, &file) != FcResultMatch || !file) {
            continue;
        }
        FcPatternGetInteger(facePattern, FC_INDEX, 0, &index);

        kbts_font *kbFont = TclpKbtsFontCacheLookupOrLoad(shapePtr->context,
                                                          (const char *)file, index);
        if (!kbFont) continue;

        if (shapePtr->numFonts < 8) {
            shapePtr->fontMap[shapePtr->numFonts].kbFont    = kbFont;
            shapePtr->fontMap[shapePtr->numFonts].faceIndex = i;
            shapePtr->numFonts++;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TclpFindXftFontForKbFont --
 *
 *	Find the XftFont corresponding to a kbts_font from the font map.
 *
 * Results:
 *	Returns a pointer to an XftFont structure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static XftFont *
TclpFindXftFontForKbFont(UnixFtFont *fontPtr, kbts_font *kbFont)
{
    int j;
    for (j = 0; j < fontPtr->shape.numFonts; j++) {
        if (fontPtr->shape.fontMap[j].kbFont == kbFont) {
            int fidx = fontPtr->shape.fontMap[j].faceIndex;
            if (fidx >= 0 && fidx < fontPtr->nfaces) {
                return fontPtr->faces[fidx].ft0Font;
            }
            break;
        }
    }
    return TclpGetFontForFace(fontPtr, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * TclpProcessSimpleLTR --
 *
 *	Process simple LTR text by creating a 1:1 glyph mapping without
 *	full bidirectional analysis or complex shaping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Populates the cache slot with glyphs for simple LTR text.
 *
 *----------------------------------------------------------------------
 */
static void
TclpProcessSimpleLTR(UnixFtFont *fontPtr, X11Shape *shapePtr, 
                     const char *source, int numBytes, int slot)
{
    XftFont *baseFont = TclpGetFontForFace(fontPtr, 0);
    int penX = 0;
    int glyphIdx = 0;
    int bytePos = 0;

    /* Allocate minimal glyph array (usually 1:1 with characters) */
    shapePtr->cache[slot].glyphCapacity = numBytes + 8;
    shapePtr->cache[slot].glyphs = Tcl_Alloc(shapePtr->cache[slot].glyphCapacity * sizeof(X11ShapedGlyph));

    while (bytePos < numBytes && glyphIdx < shapePtr->cache[slot].glyphCapacity) {
        FcChar32 ucs4;
        int clen = TclpUtf8ToUcs4(source + bytePos, &ucs4, numBytes - bytePos);
        if (clen <= 0) break;

        XGlyphInfo extents;
        XftTextExtents32(fontPtr->display, baseFont, &ucs4, 1, &extents);

        shapePtr->cache[slot].glyphs[glyphIdx].kbFont    = shapePtr->fontMap[0].kbFont;
        shapePtr->cache[slot].glyphs[glyphIdx].xftFont   = baseFont;
        shapePtr->cache[slot].glyphs[glyphIdx].glyphId   = XftCharIndex(fontPtr->display, baseFont, ucs4);
        shapePtr->cache[slot].glyphs[glyphIdx].x         = penX;
        shapePtr->cache[slot].glyphs[glyphIdx].y         = 0;  /* baseline */
        shapePtr->cache[slot].glyphs[glyphIdx].advanceX  = extents.xOff;

        penX += extents.xOff;
        bytePos += clen;
        glyphIdx++;
    }

    shapePtr->cache[slot].glyphCount  = glyphIdx;
    shapePtr->cache[slot].totalWidth  = penX;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpProcessComplexText --
 *
 *	Process complex text requiring full bidirectional analysis
 *	and glyph shaping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Populates the cache slot with shaped glyphs for complex text.
 *
 *----------------------------------------------------------------------
 */
static void
TclpProcessComplexText(UnixFtFont *fontPtr, X11Shape *shapePtr,
                       const char *source, int numBytes, int slot,
                       BidiRun *bidiRuns, int numBidiRuns)
{
    int globalPenX = 0;
    int globalPenY = 0;

    for (int r = 0; r < numBidiRuns; r++) {
        const char *runText  = source + bidiRuns[r].offset;
        int runLen   = bidiRuns[r].length;
        int runIsRTL = bidiRuns[r].isRTL;
        kbts_run run;
        kbts_glyph *glyph;

        if (runLen <= 0) continue;

        kbts_ShapeBegin(shapePtr->context,
                        runIsRTL ? KBTS_DIRECTION_RTL : KBTS_DIRECTION_LTR,
                        KBTS_LANGUAGE_DONT_KNOW);

        kbts_ShapeUtf8(shapePtr->context, runText, runLen,
                       KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);

        kbts_ShapeEnd(shapePtr->context);

        int runPenX = 0;
        int runPenY = 0;

        while (kbts_ShapeRun(shapePtr->context, &run)) {
            kbts_glyph_iterator it = run.Glyphs;

            XftFont *xftFont = TclpFindXftFontForKbFont(fontPtr, run.Font);

            while (kbts_GlyphIteratorNext(&it, &glyph)) {
                if (shapePtr->cache[slot].glyphCount >= shapePtr->cache[slot].glyphCapacity) {
                    int newCap = shapePtr->cache[slot].glyphCapacity == 0
                                 ? 64
                                 : shapePtr->cache[slot].glyphCapacity * 2;
                    shapePtr->cache[slot].glyphs = Tcl_Realloc(
                        shapePtr->cache[slot].glyphs,
                        newCap * sizeof(X11ShapedGlyph));
                    shapePtr->cache[slot].glyphCapacity = newCap;
                }

                int g = shapePtr->cache[slot].glyphCount++;
                shapePtr->cache[slot].glyphs[g].kbFont    = run.Font;
                shapePtr->cache[slot].glyphs[g].xftFont   = xftFont;
                shapePtr->cache[slot].glyphs[g].glyphId   = glyph->Id;
                shapePtr->cache[slot].glyphs[g].x         = globalPenX + runPenX + glyph->OffsetX;
                shapePtr->cache[slot].glyphs[g].y         = globalPenY + runPenY + glyph->OffsetY;
                shapePtr->cache[slot].glyphs[g].advanceX  = glyph->AdvanceX;

                runPenX += glyph->AdvanceX;
                runPenY += glyph->AdvanceY;
            }
        }

        globalPenX += runPenX;
        globalPenY += runPenY;
    }

    /* Calculate total width */
    for (int i = 0; i < shapePtr->cache[slot].glyphCount; i++) {
        shapePtr->cache[slot].totalWidth += shapePtr->cache[slot].glyphs[i].advanceX;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TclpShaperShapeString --
 *
 *	Core shaping function that processes a string and stores the
 *	result in the cache.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the shaping cache with the shaped glyphs for the string.
 *
 *----------------------------------------------------------------------
 */
static void
TclpShaperShapeString(UnixFtFont *fontPtr, const char *source, int numBytes)
{
    X11Shape *shapePtr = &fontPtr->shape;
    int i, slot;

    /* Lazy initialization of shaping context + font map */
    if (shapePtr->numFonts == 0) {
        TclpInitFontMap(fontPtr, shapePtr);
    }

    /* Check for cache hit. */
    for (i = 0; i < X11SHAPE_CACHE_SIZE; i++) {
        if (shapePtr->cache[i].textLen == numBytes &&
            shapePtr->cache[i].text &&
            memcmp(shapePtr->cache[i].text, source, numBytes) == 0) {
            shapePtr->cache[i].lruSeq = ++shapePtr->lruCounter;
            return;
        }
    }

    /* Find victim slot and evict */
    slot = TclpFindLRUSlot(shapePtr);
    TclpEvictCacheSlot(shapePtr, slot);

    /* Fast path: simple LTR text */
    if (TclpIsSimpleLTRText(source, numBytes)) {
        TclpProcessSimpleLTR(fontPtr, shapePtr, source, numBytes, slot);
    } else {
        /* Slow path: full bidi + complex shaping */
        BidiRun bidiRuns[32];
        int numBidiRuns = TclpGetBidiRuns(source, numBytes, bidiRuns, 32);
        TclpProcessComplexText(fontPtr, shapePtr, source, numBytes, slot, bidiRuns, numBidiRuns);
    }

    /* Store the text in the cache slot */
    shapePtr->cache[slot].text = Tcl_Alloc(numBytes);
    memcpy(shapePtr->cache[slot].text, source, numBytes);
    shapePtr->cache[slot].textLen = numBytes;
    shapePtr->cache[slot].lruSeq  = ++shapePtr->lruCounter;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpShaperLastShaped --
 *
 *	Find the most recently used cache entry.
 *
 * Results:
 *	Returns a pointer to the most recently used cache entry,
 *	or NULL if no entries exist.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static X11ShapeCacheEntry *
TclpShaperLastShaped(X11Shape *shapePtr)
{
    int i;
    int maxSeq  = -1;
    int maxSlot = -1;

    for (i = 0; i < X11SHAPE_CACHE_SIZE; i++) {
        if (shapePtr->cache[i].textLen >= 0 &&
            shapePtr->cache[i].lruSeq > maxSeq) {
            maxSeq  = shapePtr->cache[i].lruSeq;
            maxSlot = i;
        }
    }

    return (maxSlot >= 0) ? &shapePtr->cache[maxSlot] : NULL;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
