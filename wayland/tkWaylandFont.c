/*
 * tkWaylandFont.c --
 *
 *   Wayland/GLFW/NanoVG platform font implementation with full HarfBuzz
 *   shaping, SheenBidi bidirectional analysis, and Fontconfig multi-face
 *   fallback. Replaces the previous single-face, unshaped implementation.
 *
 *
 * Copyright © 1996-1998 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkGlfwInt.h"

#include <fontconfig/fontconfig.h>
#include <nanovg.h>
#include <hb.h>
#include <SheenBidi/SheenBidi.h>

#include "stb_truetype.h"
#include "noto_emoji.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Defines for tkTextDisp.c and tkFont.c
 */

#define TK_LAYOUT_WITH_BASE_CHUNKS	1
#define TK_DRAW_IN_CONTEXT		1

/* Module-level state */

static int  fcInitialized = 0;
static int  emojiFontId   = -1;    /* NanoVG id for the bundled emoji font. */

/* Forward declarations of static helper functions. */
static int        GetBidiRuns(FcChar32 *ucs4, int charCount,
			      BidiRun *runs, int maxRuns);
static bool       IsSimpleOnly(const char *str, int len);
static int        GetRunFaceIndex(WaylandFont *fontPtr, FcChar32 *ucs4Chars,
				  int runStart, int runLen);
static hb_font_t *GetHbFont(WaylandFont *fontPtr, int faceIndex);
static int        EnsureNvgFaceFont(WaylandFont *fontPtr, int faceIndex,
				    NVGcontext *vg);
static void       InitFont(Tk_Window tkwin, const TkFontAttributes *faPtr,
			   WaylandFont *fontPtr);
static void       DeleteFont(WaylandFont *fontPtr);
static NVGcolor   ColorFromGC(GC gc);

/*
 *----------------------------------------------------------------------
 * IsSimpleOnly --
 *
 *   Returns 1 if the string consists entirely of codepoints that do not
 *   require complex shaping: ASCII, Latin extended, basic CJK punctuation,
 *   Hiragana, and Katakana.  All RTL scripts, Indic, Thai, emoji, and any
 *   supplementary-plane characters return 0 to force HarfBuzz/SheenBidi.
 *
 *   Mirrors the identical function in tkUnixBidiFont.c.
 *
 * Results:
 *   true if the string is simple (fast path), false otherwise.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

static bool
IsSimpleOnly(const char *str, int len)
{
    int i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)str[i];
        if (c < 0x80) { i++; continue; }

        FcChar32 uc;
        int clen = FcUtf8ToUcs4((const FcChar8 *)(str + i), &uc, len - i);
        if (clen <= 0) return 0;

        /* Force complex shaper for scripts that require it. */
        if (
            /* Hebrew / Arabic / Arabic supplements / presentation forms. */
            (uc >= 0x0590 && uc <= 0x05FF) ||
            (uc >= 0x0600 && uc <= 0x06FF) ||
            (uc >= 0x0750 && uc <= 0x077F) ||
            (uc >= 0xFB50 && uc <= 0xFDFF) ||
            (uc >= 0xFE70 && uc <= 0xFEFF) ||
            /* Other RTL scripts. */
            (uc >= 0x0700 && uc <= 0x074F) ||  /* Syriac        */
            (uc >= 0x0780 && uc <= 0x07BF) ||  /* Thaana        */
            (uc >= 0x07C0 && uc <= 0x07FF) ||  /* N'Ko          */
            (uc >= 0x0800 && uc <= 0x083F) ||  /* Samaritan     */
            (uc >= 0x0840 && uc <= 0x085F) ||  /* Mandaic       */
            /* CJK ideographs, Hangul, Jamo. */
            (uc >= 0x4E00 && uc <= 0x9FFF) ||
            (uc >= 0xAC00 && uc <= 0xD7AF) ||
            (uc >= 0x1100 && uc <= 0x11FF) ||
            /* Indic / Thai / Lao. */
            (uc >= 0x0900 && uc <= 0x0DFF) ||
            (uc >= 0x0E00 && uc <= 0x0E7F) ||
            (uc >= 0x0E80 && uc <= 0x0EFF) ||
            /* Emoji and supplementary plane. */
            (uc >= 0x2600 && uc <= 0x27BF) ||
            (uc >= 0x1F000 && uc <= 0x1FAFF) ||
            (uc >= 0x1F300 && uc <= 0x1F9FF) ||
            uc > 0xFFFF
	    ) {
            return false;
        }

        /* Safe for fast path: Latin extended, CJK punctuation + Kana. */
        int isSafe =
            (uc <= 0x024F) ||
            (uc >= 0x3000 && uc <= 0x30FF);
        if (!isSafe) return false;

        i += clen;
    }
    return true;
}

/*
 *----------------------------------------------------------------------
 * GetBidiRuns --
 *
 *   Analyse a UCS-4 array with SheenBidi and return level runs in visual
 *   order.  Mirrors the identical function in tkUnixBidiFont.c.
 *
 * Results:
 *   Number of runs written into 'runs' array (at most maxRuns).
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
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

    /* Fast path: no strong RTL characters. */
    int needsBidi = 0;
    for (int i = 0; i < charCount; i++) {
        if (ucs4[i] >= 0x0590) { needsBidi = 1; break; }
    }
    if (!needsBidi) {
        runs[0].offset = 0;
        runs[0].len    = charCount;
        runs[0].isRTL  = 0;
        return 1;
    }

    SBCodepointSequence seq = {
        SBStringEncodingUTF32, ucs4, (SBUInteger)charCount
    };
    SBAlgorithmRef algo = SBAlgorithmCreate(&seq);
    SBParagraphRef para = SBAlgorithmCreateParagraph(algo, 0,
						     (SBUInteger)charCount, SBLevelDefaultLTR);
    SBLineRef      line = SBParagraphCreateLine(para, 0, (SBUInteger)charCount);

    SBUInteger       runCount = SBLineGetRunCount(line);
    const SBRun     *bidiRuns = SBLineGetRunsPtr(line);

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

    return outRuns > 0 ? outRuns : 1;
}

/*
 *----------------------------------------------------------------------
 * GetRunFaceIndex --
 *
 *   Choose the best WaylandFtFace for the first character of a run.
 *   Uses the direct-mapped 64-slot cache from WaylandShaper.
 *
 * Results:
 *   Index of the face (0..nfaces-1) that supports the character.
 *
 * Side effects:
 *   Updates the character-to-face cache.
 *----------------------------------------------------------------------
 */

static int
GetRunFaceIndex(
		WaylandFont *fontPtr,
		FcChar32 *ucs4Chars,
                int runStart,
		int runLen)
{
    if (runLen <= 0 || runStart < 0) return 0;
    FcChar32      uc       = ucs4Chars[runStart];
    WaylandShaper *shaper  = &fontPtr->shaper;
    int            cacheIdx = uc & 63;

    if (shaper->charCache[cacheIdx].uc == uc) {
        return shaper->charCache[cacheIdx].faceIdx;
    }

    for (int fi = 0; fi < fontPtr->nfaces; fi++) {
        if (fontPtr->faces[fi].charset &&
	    FcCharSetHasChar(fontPtr->faces[fi].charset, uc)) {
            shaper->charCache[cacheIdx].uc      = uc;
            shaper->charCache[cacheIdx].faceIdx = fi;
            return fi;
        }
    }

    shaper->charCache[cacheIdx].uc      = uc;
    shaper->charCache[cacheIdx].faceIdx = 0;
    return 0;
}

/*
 *----------------------------------------------------------------------
 * GetHbFont --
 *
 *   Lazily create a HarfBuzz font for face faceIndex.
 *   The result is cached in WaylandFtFace.hbFont.
 *
 * Results:
 *   Pointer to hb_font_t, or NULL on failure.
 *
 * Side effects:
 *   Loads font face and creates HarfBuzz font if not already done.
 *----------------------------------------------------------------------
 */

static hb_font_t *
GetHbFont(
	  WaylandFont *fontPtr,
	  int faceIndex) {
    if (faceIndex < 0 || faceIndex >= fontPtr->nfaces) return NULL;
    WaylandFtFace *face = &fontPtr->faces[faceIndex];

    if (face->isLoaded) return face->hbFont;

    if (!face->filePath) {
        face->isLoaded = 1;
        return NULL;
    }

    face->hbBlob = hb_blob_create_from_file(face->filePath);
    if (!face->hbBlob || hb_blob_get_length(face->hbBlob) == 0) {
        if (face->hbBlob) { hb_blob_destroy(face->hbBlob); face->hbBlob = NULL; }
        face->isLoaded = 1;
        return NULL;
    }

    face->hbFace = hb_face_create(face->hbBlob, (unsigned)face->faceIndex);
    if (!face->hbFace) {
        hb_blob_destroy(face->hbBlob);
        face->hbBlob   = NULL;
        face->isLoaded = 1;
        return NULL;
    }

    face->hbFont = hb_font_create(face->hbFace);
    if (!face->hbFont) {
        hb_face_destroy(face->hbFace);
        hb_blob_destroy(face->hbBlob);
        face->hbFace   = NULL;
        face->hbBlob   = NULL;
        face->isLoaded = 1;
        return NULL;
    }

    /*
     * Scale is in HarfBuzz 26.6 fixed-point units (1/64 px).
     * This tells HarfBuzz what pixel size to use when computing
     * advances and offsets from the font's OpenType tables.
     */
    int scale = fontPtr->pixelSize * 64;
    hb_font_set_scale(face->hbFont, scale, scale);

    face->isLoaded = 1;
    return face->hbFont;
}


/*
 *----------------------------------------------------------------------
 * WaylandShaper_Init --
 *
 *   Initializes a WaylandShaper structure.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Allocates HarfBuzz buffer and clears caches.
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
WaylandShaper_Init(WaylandShaper *s)
{
    memset(s, 0, sizeof(*s));
    s->buffer = hb_buffer_create();
    for (int i = 0; i < CACHE_SLOTS; i++) s->cache[i].valid = 0;
    for (int i = 0; i < 64; i++) {
        s->charCache[i].uc      = 0;
        s->charCache[i].faceIdx = 0;
    }
}

/*
 *----------------------------------------------------------------------
 * WaylandShaper_Destroy --
 *
 *   Frees resources held by a WaylandShaper.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Destroys HarfBuzz buffer.
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
WaylandShaper_Destroy(WaylandShaper *s)
{
    if (s->buffer) {
        hb_buffer_destroy(s->buffer);
        s->buffer = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 * WaylandShaper_ShapeString --
 *
 *   Shape a UTF-8 string using HarfBuzz (with SheenBidi bidi analysis) and
 *   fill a ShapedGlyphBuffer.  For each glyph the cluster's UTF-8 bytes are
 *   stored so that NanoVG can render them cluster-by-cluster at the correct
 *   HarfBuzz-derived positions.
 *
 *   Fast path (IsSimpleOnly): skips SheenBidi and HarfBuzz; builds the
 *   glyph buffer by walking UTF-8 codepoints and recording cluster metadata.
 *   Advances are left at 0 and filled in by NanoVG during measurement/draw.
 *
 * Results:
 *   true on success, false on failure.
 *
 * Side effects:
 *   Updates the shaper's string cache.
 *----------------------------------------------------------------------
 */

MODULE_SCOPE bool
WaylandShaper_ShapeString(
			  WaylandShaper     *shaper,
			  WaylandFont       *fontPtr,
			  const char        *source,
			  int                numBytes,
			  ShapedGlyphBuffer *buffer)
{
    if (!shaper->buffer || !source || numBytes <= 0 || !buffer) return 0;

    buffer->glyphCount        = 0;
    buffer->indexCount        = 0;
    buffer->totalAdvance      = 0;
    buffer->clusterBreakCount = 0;

    /* Fast path: simple LTR text (Latin, Kana, basic punctuation). */
    if (IsSimpleOnly(source, numBytes)) {
        int i = 0;
        while (i < numBytes && buffer->glyphCount < MAX_GLYPHS) {
            FcChar32 uc;
            int clen = FcUtf8ToUcs4((const FcChar8 *)(source + i), &uc,
                                    numBytes - i);
            if (clen <= 0) { i++; continue; }

            int cacheIdx = uc & 63;
            int fi       = 0;
            if (shaper->charCache[cacheIdx].uc == uc) {
                fi = shaper->charCache[cacheIdx].faceIdx;
            } else {
                for (fi = 0; fi < fontPtr->nfaces; fi++) {
                    if (fontPtr->faces[fi].charset &&
			FcCharSetHasChar(fontPtr->faces[fi].charset, uc))
                        break;
                }
                if (fi >= fontPtr->nfaces) fi = 0;
                shaper->charCache[cacheIdx].uc      = uc;
                shaper->charCache[cacheIdx].faceIdx = fi;
            }

            int g = buffer->glyphCount++;
            buffer->glyphs[g].faceIndex  = fi;
            buffer->glyphs[g].glyphId    = uc;
            buffer->glyphs[g].x          = 0;   /* NVG computes position. */
            buffer->glyphs[g].y          = 0;
            buffer->glyphs[g].advanceX   = 0;
            buffer->glyphs[g].byteOffset = i;
            buffer->glyphs[g].clusterLen = clen;
            buffer->glyphs[g].isRTL      = 0;

            int cpLen = clen < 15 ? clen : 15;
            memcpy(buffer->glyphs[g].clusterUtf8, source + i, cpLen);
            buffer->glyphs[g].clusterUtf8[cpLen] = '\0';
            buffer->glyphs[g].clusterUtf8Len      = cpLen;

            i += clen;
        }

        /* Trivial visualIndex. */
        for (int j = 0; j < buffer->glyphCount; j++) {
            int v = buffer->indexCount++;
            buffer->visualIndex[v].x         = 0;
            buffer->visualIndex[v].advanceX  = 0;
            buffer->visualIndex[v].byteStart = buffer->glyphs[j].byteOffset;
            buffer->visualIndex[v].byteEnd   =
                buffer->glyphs[j].byteOffset + buffer->glyphs[j].clusterLen;
            buffer->visualIndex[v].isRTL     = 0;
        }

        /* Cluster break list. */
        buffer->clusterBreaks[0]  = 0;
        buffer->clusterBreakCount = 1;
        for (int j = 0; j < buffer->glyphCount &&
		 buffer->clusterBreakCount < MAX_CLUSTER_BREAKS; j++) {
            buffer->clusterBreaks[buffer->clusterBreakCount++] =
                buffer->glyphs[j].byteOffset + buffer->glyphs[j].clusterLen;
        }
        return true;
    }

    /* Cache lookup for complex/RTL text. */
    for (int slot = 0; slot < CACHE_SLOTS; slot++) {
        if (shaper->cache[slot].valid &&
	    shaper->cache[slot].len == numBytes &&
	    numBytes <= MAX_STRING_CACHE &&
	    memcmp(source, shaper->cache[slot].text, numBytes) == 0) {
            *buffer = shaper->cache[slot].buffer;
            return true;
        }
    }

    /* Decode UTF-8 → UCS-4. */
    int       stackCharBounds[256];
    FcChar32  stackUcs4[256];
    int      *charBounds = stackCharBounds;
    FcChar32 *ucs4Chars  = stackUcs4;
    bool       needFree   = false;

    if (numBytes >= 256) {
        charBounds = (int *)    malloc((numBytes + 1) * sizeof(int));
        ucs4Chars  = (FcChar32*)malloc( numBytes       * sizeof(FcChar32));
        if (!charBounds || !ucs4Chars) {
            free(charBounds); free(ucs4Chars);
            return false;
        }
        needFree = true;
    }

    charBounds[0] = 0;
    int bytePos   = 0;
    int charCount = 0;
    while (bytePos < numBytes && charCount < MAX_GLYPHS) {
        FcChar32 uc;
        int clen = FcUtf8ToUcs4((const FcChar8 *)(source + bytePos), &uc,
				numBytes - bytePos);
        if (clen <= 0) { bytePos++; continue; }

        /* Skip C0/C1 control characters except whitespace. */
        if ((uc < 0x0020 && uc != 0x0009 && uc != 0x000A && uc != 0x000D) ||
	    (uc >= 0x0080 && uc <= 0x009F) || uc == 0xFFFD) {
            bytePos += clen;
            continue;
        }

        ucs4Chars[charCount]  = uc;
        charCount++;
        bytePos += clen;
        charBounds[charCount] = bytePos;
    }

    if (charCount == 0) {
        if (needFree) { free(charBounds); free(ucs4Chars); }
        buffer->clusterBreaks[0]  = 0;
        buffer->clusterBreakCount = 1;
        return true;
    }

    /* BiDi analysis. */
    BidiRun bidiRuns[MAX_BIDI_RUNS];
    int     numRuns = GetBidiRuns(ucs4Chars, charCount, bidiRuns, MAX_BIDI_RUNS);

    int globalPenX = 0;

    /* Shape each bidi run, further subdivided by script and face. */
    for (int r = 0; r < numRuns; r++) {
        int runStart = bidiRuns[r].offset;
        int runLen   = bidiRuns[r].len;
        int runIsRTL = bidiRuns[r].isRTL;

        if (runLen <= 0) continue;

        int runByteStart = charBounds[runStart];
        int runByteEnd   = charBounds[runStart + runLen];
        if (runByteEnd <= runByteStart) continue;

        /* Skip invisible-only runs. */
        int hasVisible = 0;
        for (int ci = runStart; ci < runStart + runLen; ci++) {
            if (ucs4Chars[ci] >= 0x0020 ||
		ucs4Chars[ci] == 0x0009 ||
		ucs4Chars[ci] == 0x000A ||
		ucs4Chars[ci] == 0x000D) {
                hasVisible = 1; break;
            }
        }
        if (!hasVisible) continue;

        int subrunStart = runStart;

        while (subrunStart < runStart + runLen) {

            /* Find first real (non-INHERITED/COMMON) script. */
            hb_script_t subrunScript = HB_SCRIPT_INVALID;
            for (int ci = subrunStart; ci < runStart + runLen; ci++) {
                hb_script_t s = hb_unicode_script(
						  hb_unicode_funcs_get_default(), ucs4Chars[ci]);
                if (s != HB_SCRIPT_INHERITED && s != HB_SCRIPT_COMMON) {
                    subrunScript = s; break;
                }
            }

            int anchorChar = subrunStart;
            if (subrunScript != HB_SCRIPT_INVALID) {
                for (int ci = subrunStart; ci < runStart + runLen; ci++) {
                    hb_script_t s = hb_unicode_script(
						      hb_unicode_funcs_get_default(), ucs4Chars[ci]);
                    if (s != HB_SCRIPT_INHERITED && s != HB_SCRIPT_COMMON) {
                        anchorChar = ci; break;
                    }
                }
            } else {
                subrunScript = HB_SCRIPT_COMMON;
            }

            int runFaceIndex = GetRunFaceIndex(fontPtr, ucs4Chars, anchorChar, 1);

            /* Extend subrun while script and face are consistent. */
            int subrunEnd = subrunStart + 1;
            while (subrunEnd < runStart + runLen) {
                hb_script_t s = hb_unicode_script(
						  hb_unicode_funcs_get_default(), ucs4Chars[subrunEnd]);

                if (s == HB_SCRIPT_INHERITED || s == HB_SCRIPT_COMMON) {
                    int nf = GetRunFaceIndex(fontPtr, ucs4Chars, subrunEnd, 1);
                    if (nf != runFaceIndex) break;
                    subrunEnd++;
                    continue;
                }
                if (s != subrunScript) break;
                int nf = GetRunFaceIndex(fontPtr, ucs4Chars, subrunEnd, 1);
                if (nf != runFaceIndex) break;
                subrunEnd++;
            }

            int shapeByteStart = charBounds[subrunStart];
            int shapeByteEnd   = charBounds[
					    subrunEnd < runStart + runLen ? subrunEnd : runStart + runLen];
            int shapeByteLen   = shapeByteEnd - shapeByteStart;

            if (shapeByteLen <= 0) { subrunStart = subrunEnd; continue; }

            hb_font_t *runHbFont = GetHbFont(fontPtr, runFaceIndex);
            if (!runHbFont) { subrunStart = subrunEnd; continue; }

            /* Shape. */
            hb_buffer_clear_contents(shaper->buffer);
            hb_buffer_add_utf8(shaper->buffer, source, numBytes,
                               shapeByteStart, shapeByteLen);
            hb_buffer_set_direction(shaper->buffer,
                                    runIsRTL ? HB_DIRECTION_RTL
				    : HB_DIRECTION_LTR);
            hb_buffer_set_script(shaper->buffer, subrunScript);
            hb_buffer_set_language(shaper->buffer,
                                   hb_language_from_string("", -1));
            hb_buffer_set_cluster_level(shaper->buffer,
					HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);

            hb_shape(runHbFont, shaper->buffer, NULL, 0);

            unsigned int         glyphCount = hb_buffer_get_length(shaper->buffer);
            hb_glyph_info_t     *glyphInfo  =
                hb_buffer_get_glyph_infos(shaper->buffer, NULL);
            hb_glyph_position_t *glyphPos   =
                hb_buffer_get_glyph_positions(shaper->buffer, NULL);

            if (!glyphInfo || !glyphPos) { subrunStart = subrunEnd; continue; }

            int runPenX = 0;
            for (unsigned int gi = 0;
                 gi < glyphCount && buffer->glyphCount < MAX_GLYPHS;
                 gi++) {

                int byteOff = (int)glyphInfo[gi].cluster;
                int advX    = (int)(glyphPos[gi].x_advance / 64.0 + 0.5);

                /* Cluster length. */
                int clusterLen;
                if (!runIsRTL) {
                    int nextOff = shapeByteEnd;
                    for (unsigned int gj = gi + 1; gj < glyphCount; gj++) {
                        if ((int)glyphInfo[gj].cluster != byteOff) {
                            nextOff = (int)glyphInfo[gj].cluster; break;
                        }
                    }
                    clusterLen = nextOff - byteOff;
                } else {
                    int prevOff = shapeByteEnd;
                    for (int gj = (int)gi - 1; gj >= 0; gj--) {
                        if ((int)glyphInfo[gj].cluster != byteOff) {
                            prevOff = (int)glyphInfo[gj].cluster; break;
                        }
                    }
                    clusterLen = abs(prevOff - byteOff);
                }
                if (clusterLen <= 0) clusterLen = 1;

                int cpLen = clusterLen < 15 ? clusterLen : 15;

                int g = buffer->glyphCount++;
                buffer->glyphs[g].faceIndex  = runFaceIndex;
                buffer->glyphs[g].glyphId    = glyphInfo[gi].codepoint;
                buffer->glyphs[g].x          = globalPenX + runPenX +
                    (int)(glyphPos[gi].x_offset / 64.0 + 0.5);
                buffer->glyphs[g].y          =
                    -(int)(glyphPos[gi].y_offset / 64.0 + 0.5);
                buffer->glyphs[g].advanceX   = advX;
                buffer->glyphs[g].byteOffset = byteOff;
                buffer->glyphs[g].clusterLen = clusterLen;
                buffer->glyphs[g].isRTL      = runIsRTL;

                memcpy(buffer->glyphs[g].clusterUtf8, source + byteOff, cpLen);
                buffer->glyphs[g].clusterUtf8[cpLen] = '\0';
                buffer->glyphs[g].clusterUtf8Len      = cpLen;

                runPenX += advX;
            }

            globalPenX  += runPenX;
            subrunStart  = subrunEnd;
        }
    }

    buffer->totalAdvance = globalPenX;

    /* Build visualIndex. */
    buffer->indexCount = 0;
    for (int i = 0; i < buffer->glyphCount; i++) {
        int byteStart = buffer->glyphs[i].byteOffset;
        int byteEnd   = byteStart + buffer->glyphs[i].clusterLen;

        if (buffer->indexCount > 0 &&
	    buffer->visualIndex[buffer->indexCount - 1].byteStart == byteStart)
            continue;

        int v = buffer->indexCount++;
        buffer->visualIndex[v].x         = buffer->glyphs[i].x;
        buffer->visualIndex[v].advanceX  = buffer->glyphs[i].advanceX;
        buffer->visualIndex[v].isRTL     = buffer->glyphs[i].isRTL;
        if (buffer->glyphs[i].isRTL) {
            buffer->visualIndex[v].byteStart = byteEnd;
            buffer->visualIndex[v].byteEnd   = byteStart;
        } else {
            buffer->visualIndex[v].byteStart = byteStart;
            buffer->visualIndex[v].byteEnd   = byteEnd;
        }
    }

    /* Build sorted cluster break table. */
    {
        char seen[1024] = {0};
        buffer->clusterBreaks[0]  = 0;
        buffer->clusterBreakCount = 1;
        seen[0] = 1;

        for (int i = 0;
             i < buffer->glyphCount &&
		 buffer->clusterBreakCount < MAX_CLUSTER_BREAKS - 1;
             i++) {
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

        if (buffer->clusterBreaks[buffer->clusterBreakCount - 1] != numBytes &&
	    buffer->clusterBreakCount < MAX_CLUSTER_BREAKS) {
            buffer->clusterBreaks[buffer->clusterBreakCount++] = numBytes;
        }

        /* Insertion sort. */
        int n = buffer->clusterBreakCount;
        for (int i = 1; i < n; i++) {
            int key = buffer->clusterBreaks[i], j = i - 1;
            while (j >= 0 && buffer->clusterBreaks[j] > key) {
                buffer->clusterBreaks[j + 1] = buffer->clusterBreaks[j]; j--;
            }
            buffer->clusterBreaks[j + 1] = key;
        }
        /* Deduplicate. */
        int w = 1;
        for (int i = 1; i < n; i++) {
            if (buffer->clusterBreaks[i] != buffer->clusterBreaks[w - 1])
                buffer->clusterBreaks[w++] = buffer->clusterBreaks[i];
        }
        buffer->clusterBreakCount = w;
    }

    /* Cache result. */
    if (numBytes <= MAX_STRING_CACHE) {
        int slot = shaper->cacheNext;
        memcpy(shaper->cache[slot].text, source, numBytes);
        shaper->cache[slot].len    = numBytes;
        shaper->cache[slot].buffer = *buffer;
        shaper->cache[slot].valid  = 1;
        shaper->cacheNext = (slot + 1) % CACHE_SLOTS;
    }

    if (needFree) { free(charBounds); free(ucs4Chars); }
    return true;
}

/*
 *----------------------------------------------------------------------
 * EnsureNvgFaceFont --
 *
 *   Load a single WaylandFtFace into the NanoVG context on demand.
 *   Each face gets a unique name (e.g. "__wlfont_0", "__wlfont_1") so
 *   that NanoVG can look them up independently.
 *
 * Results:
 *   NanoVG font ID, or -1 on failure.
 *
 * Side effects:
 *   Registers the font with the NanoVG context.
 *----------------------------------------------------------------------
 */

static int
EnsureNvgFaceFont(
		  WaylandFont *fontPtr,
		  int faceIndex,
		  NVGcontext *vg)
{
    if (faceIndex < 0 || faceIndex >= fontPtr->nfaces || !vg) return -1;
    WaylandFtFace *face = &fontPtr->faces[faceIndex];

    if (face->nvgName[0] == '\0') {
        snprintf(face->nvgName, sizeof(face->nvgName),
                 "__wlfont_%d", faceIndex);
    }

    int id = nvgFindFont(vg, face->nvgName);
    if (id >= 0) { face->nvgFontId = id; return id; }

    if (face->filePath) id = nvgCreateFont(vg, face->nvgName, face->filePath);

    face->nvgFontId = id;
    return id;
}

/*
 *----------------------------------------------------------------------
 * EnsureNvgFont --
 *
 *   Ensure all faces are loaded into the NanoVG context and wire up the
 *   fallback chain: face[0] → face[1] → … → emoji.
 *
 * Results:
 *   NanoVG ID of the primary (face[0]) font, or -1 on failure.
 *
 * Side effects:
 *   Loads fonts into NanoVG and sets up fallback relationships.
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
EnsureNvgFont(
	      WaylandFont *fontPtr,
	      NVGcontext *vg)
{
    if (!vg) return -1;

    /* Bundled emoji font. */
    int emojiId = nvgFindFont(vg, "emoji");
    if (emojiId < 0) {
        emojiId = nvgCreateFontMem(vg, "emoji",
                                   NotoEmoji_Regular_ttf,
                                   NotoEmoji_Regular_ttf_len, 0);
        if (emojiId < 0)
            fprintf(stderr, "tkWaylandFont: failed to load bundled emoji\n");
    }
    emojiFontId = emojiId;

    /* Load each face. */
    int primaryId = -1;
    for (int i = 0; i < fontPtr->nfaces; i++) {
        int id = EnsureNvgFaceFont(fontPtr, i, vg);
        if (i == 0) primaryId = id;
    }

    /* Wire fallback chain on the primary face. */
    if (primaryId >= 0) {
        for (int i = 1; i < fontPtr->nfaces; i++) {
            int fb = fontPtr->faces[i].nvgFontId;
            if (fb >= 0) nvgAddFallbackFontId(vg, primaryId, fb);
        }
        if (emojiId >= 0) nvgAddFallbackFontId(vg, primaryId, emojiId);
    }

    fontPtr->nvgFontId = primaryId;
    return primaryId;
}

/*
 *----------------------------------------------------------------------
 * TkpGetFontPixelSize --
 *
 *   Get the pixel size of a Tk_Font for use by menu drawing.
 *
 * Results:
 *   Pixel size in pixels.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkpGetFontPixelSize(Tk_Font tkfont)
{
    WaylandFont *fontPtr = (WaylandFont *)tkfont;
    return fontPtr ? fontPtr->pixelSize : 12;
}

/*
 *----------------------------------------------------------------------
 * InitFont --
 *
 *   Populate a WaylandFont from TkFontAttributes using FcFontSort for
 *   multi-face fallback, then extract metrics from the primary face via
 *   stb_truetype.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Allocates font structures, queries Fontconfig, initialises shaper.
 *----------------------------------------------------------------------
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

    /* Resolve pixel size with improved scaling. */
    double ptSize = faPtr->size;
    int    basePixels;

    if (ptSize < 0.0) {
        /* Explicit pixel size (Tk convention: -12 means 12px). */
        basePixels = (int)(-ptSize + 0.5);
    } else if (ptSize > 0.0) {
        /* Try Tk's conversion first. */
        basePixels = (int)(TkFontGetPoints(tkwin, ptSize) + 0.5);
        /* Strong fallback for Wayland/GLFW if conversion gives tiny/no-op result. */
        if (basePixels <= 0 || basePixels == (int)ptSize || basePixels < 8) {
            /* Standard 96 DPI scaling: 12pt → ~16px. */
            basePixels = (int)(ptSize * 4.0 / 3.0 + 0.5);
        }
    } else {
        basePixels = 12;
    }
    if (basePixels < 1) basePixels = 1;

    /* Optional: gentle boost for modern displays (can be tuned). */
    if (basePixels < 14) {
        basePixels = (int)(basePixels * 1.15 + 0.5);  /* ~15% boost for readability */
    }

    fontPtr->pixelSize = basePixels;

    int bold   = (faPtr->weight == TK_FW_BOLD);
    int italic = (faPtr->slant  == TK_FS_ITALIC);

    /* Build Fontconfig pattern and obtain ordered font-set. */
    const char *family = faPtr->family;
    if (!family || family[0] == '\0') family = "sans-serif";

    FcPattern *pat = FcPatternCreate();
    if (!pat) return;

    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)family);
    FcPatternAddInteger(pat, FC_WEIGHT,
                        bold   ? FC_WEIGHT_BOLD    : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pat, FC_SLANT,
                        italic ? FC_SLANT_ITALIC    : FC_SLANT_ROMAN);
    FcPatternAddDouble(pat, FC_PIXEL_SIZE, (double)fontPtr->pixelSize);

    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult   result;
    FcFontSet *set = FcFontSort(NULL, pat, FcTrue, NULL, &result);

    if (!set || set->nfont == 0) {
        /* Last-resort fallback. */
        FcPatternDestroy(pat);
        pat = FcPatternCreate();
        FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"sans-serif");
        FcPatternAddDouble(pat, FC_PIXEL_SIZE, (double)fontPtr->pixelSize);
        FcConfigSubstitute(NULL, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);
        set = FcFontSort(NULL, pat, FcTrue, NULL, &result);
    }

    fontPtr->pattern = pat;
    fontPtr->fontset = set;

    int nfaces = (set && set->nfont > 0) ? set->nfont : 0;
    if (nfaces > MAX_FACES) nfaces = MAX_FACES;

    fontPtr->faces  = (WaylandFtFace *)Tcl_Alloc(
						 (nfaces > 0 ? nfaces : 1) * sizeof(WaylandFtFace));
    fontPtr->nfaces = nfaces;
    memset(fontPtr->faces, 0,
           (nfaces > 0 ? nfaces : 1) * sizeof(WaylandFtFace));

    for (int i = 0; i < nfaces; i++) {
        WaylandFtFace *face = &fontPtr->faces[i];
        face->source     = set->fonts[i];
        face->nvgFontId  = -1;
        face->isLoaded   = 0;
        face->nvgName[0] = '\0';

        /* Per-face charset. */
        FcCharSet *cs = NULL;
        if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &cs)
	    == FcResultMatch)
            face->charset = FcCharSetCopy(cs);

        /* File path and face index. */
        FcChar8 *fcPath = NULL;
        if (FcPatternGetString(set->fonts[i], FC_FILE, 0, &fcPath)
	    == FcResultMatch && fcPath)
            face->filePath = strdup((char *)fcPath);

        int fcIdx = 0;
        FcPatternGetInteger(set->fonts[i], FC_INDEX, 0, &fcIdx);
        face->faceIndex = fcIdx;
    }

    /* Metrics from primary face via stb_truetype. */
    if (nfaces > 0 && fontPtr->faces[0].filePath) {
        FILE *fd = fopen(fontPtr->faces[0].filePath, "rb");
        if (fd) {
            fseek(fd, 0, SEEK_END);
            long sz = ftell(fd);
            fseek(fd, 0, SEEK_SET);
            unsigned char *buf = (unsigned char *)Tcl_Alloc((int)sz);
            if (buf && (long)fread(buf, 1, sz, fd) == sz) {
                stbtt_fontinfo info;
                if (stbtt_InitFont(&info, buf,
                                   stbtt_GetFontOffsetForIndex(
							       buf, fontPtr->faces[0].faceIndex))) {
                    float scale = stbtt_ScaleForPixelHeight(
							    &info, (float)fontPtr->pixelSize);
                    int asc, desc, linegap;
                    stbtt_GetFontVMetrics(&info, &asc, &desc, &linegap);
                    fm->ascent   = (int)( asc  * scale + 0.5f);
                    fm->descent  = (int)(-desc * scale + 0.5f);   /* Note: positive descent */
                    int adv_W, adv_dot, lsb;
                    stbtt_GetCodepointHMetrics(&info, 'W',  &adv_W,   &lsb);
                    stbtt_GetCodepointHMetrics(&info, '.',  &adv_dot, &lsb);
                    fm->maxWidth = (int)(adv_W * scale + 0.5f);
                    fm->fixed    = (adv_W == adv_dot);
                    fa->size     = (double)(-fontPtr->pixelSize);
                }
            }
            if (buf) Tcl_Free(buf);
            fclose(fd);
        }
    }

    /* Improved fallback metrics — consistent with real fonts. */
    if (fm->ascent == 0 && fm->descent == 0) {
        fm->ascent   = (int)(fontPtr->pixelSize * 0.72 + 0.5);   /* tighter than 0.80 */
        fm->descent  = (int)(fontPtr->pixelSize * 0.28 + 0.5);   /* more realistic */
        fm->maxWidth = fontPtr->pixelSize;
        fm->fixed    = 0;
    }

    fontPtr->underlinePos = fm->descent / 2;
    if (fontPtr->underlinePos < 1) fontPtr->underlinePos = 1;
    fontPtr->barHeight    = (int)(fontPtr->pixelSize * 0.07 + 0.5);
    if (fontPtr->barHeight < 1) fontPtr->barHeight = 1;

    fontPtr->nvgFontId = -1;
    fontPtr->font.fid  = (Font)(uintptr_t)fontPtr;

    WaylandShaper_Init(&fontPtr->shaper);
}

/*
 *----------------------------------------------------------------------
 * DeleteFont --
 *
 *   Frees all resources associated with a WaylandFont.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Releases Fontconfig, HarfBuzz, FreeType, and Tcl-allocated memory.
 *----------------------------------------------------------------------
 */

static void
DeleteFont(WaylandFont *fontPtr)
{
    WaylandShaper_Destroy(&fontPtr->shaper);

    for (int i = 0; i < fontPtr->nfaces; i++) {
        WaylandFtFace *face = &fontPtr->faces[i];
        if (face->hbFont) { hb_font_destroy(face->hbFont); face->hbFont = NULL; }
        if (face->hbFace) { hb_face_destroy(face->hbFace); face->hbFace = NULL; }
        if (face->hbBlob) { hb_blob_destroy(face->hbBlob); face->hbBlob = NULL; }
        if (face->charset)  { FcCharSetDestroy(face->charset); face->charset = NULL; }
        if (face->filePath) { free(face->filePath);            face->filePath = NULL; }
    }
    if (fontPtr->faces) {
        Tcl_Free(fontPtr->faces);
        fontPtr->faces  = NULL;
        fontPtr->nfaces = 0;
    }
    if (fontPtr->fontset) { FcFontSetDestroy(fontPtr->fontset); fontPtr->fontset = NULL; }
    if (fontPtr->pattern) { FcPatternDestroy(fontPtr->pattern); fontPtr->pattern = NULL; }
}

/*
 *----------------------------------------------------------------------
 * ColorFromGC --
 *
 *   Convert an X GC foreground colour to an NVGcolor.
 *
 * Results:
 *   NVGcolor value.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
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
 *----------------------------------------------------------------------
 * TkpFontPkgInit --
 *
 *   Initialise the font subsystem and create standard Tk named fonts.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Initialises Fontconfig, defines global Tk fonts.
 *----------------------------------------------------------------------
 */

void
TkpFontPkgInit(TkMainInfo *mainPtr)
{
    Tcl_Interp *interp = mainPtr->interp;

    if (!fcInitialized) {
        FcInit();
        fcInitialized = 1;
    }

    /*
     * Register standard Tk named fonts.  Use generic Fontconfig family
     * names so Fontconfig selects the best available system font rather
     * than requiring a specific family to be installed.
     */
    static const struct {
        const char *tkName;
        const char *family;
        int         points;
        int         bold;
        int         italic;
    } namedFonts[] = {
        { "TkDefaultFont",      "sans-serif", 10, 0, 0 },
        { "TkTextFont",         "sans-serif", 10, 0, 0 },
        { "TkFixedFont",        "monospace",  10, 0, 0 },
        { "TkHeadingFont",      "sans-serif", 10, 1, 0 },
        { "TkCaptionFont",      "sans-serif", 12, 1, 0 },
        { "TkSmallCaptionFont", "sans-serif",  8, 0, 0 },
        { "TkIconFont",         "sans-serif", 10, 0, 0 },
        { "TkMenuFont",         "sans-serif", 10, 0, 0 },
        { "TkTooltipFont",      "sans-serif",  9, 0, 0 },
        { NULL, NULL, 0, 0, 0 }
    };

    for (int i = 0; namedFonts[i].tkName != NULL; i++) {
        Tcl_Obj *cmd = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj("font",   -1));
        Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj("create", -1));
        Tcl_ListObjAppendElement(NULL, cmd,
				 Tcl_NewStringObj(namedFonts[i].tkName, -1));
        Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj("-family", -1));
        Tcl_ListObjAppendElement(NULL, cmd,
				 Tcl_NewStringObj(namedFonts[i].family, -1));
        Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj("-size", -1));
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
        Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL);
        Tcl_DecrRefCount(cmd);
        Tcl_ResetResult(interp);
    }
}

/*
 *----------------------------------------------------------------------
 * TkpGetNativeFont --
 *
 *   Create a TkFont from a native font name (not used extensively on Wayland).
 *
 * Results:
 *   Pointer to a TkFont structure.
 *
 * Side effects:
 *   Allocates and initialises a new font.
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 * TkpGetFontFromAttributes --
 *
 *   Create or reuse a TkFont from the given attributes.
 *
 * Results:
 *   Pointer to a TkFont structure.
 *
 * Side effects:
 *   Allocates new font or reinitialises an existing one.
 *----------------------------------------------------------------------
 */

TkFont *
TkpGetFontFromAttributes(
			 TkFont *tkFontPtr,
			 Tk_Window tkwin,
			 const TkFontAttributes *faPtr)
{
    WaylandFont *fontPtr;

    if (tkFontPtr == NULL) {
        fontPtr = (WaylandFont *)Tcl_Alloc(sizeof(WaylandFont));
        memset(fontPtr, 0, sizeof(WaylandFont));
    } else {
        fontPtr = (WaylandFont *)tkFontPtr;
        DeleteFont(fontPtr);
    }

    InitFont(tkwin, faPtr, fontPtr);
    return (TkFont *)fontPtr;
}

/*
 *----------------------------------------------------------------------
 * TkpDeleteFont --
 *
 *   Delete a TkFont and free all associated resources.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Frees memory and releases system font resources.
 *----------------------------------------------------------------------
 */

void
TkpDeleteFont(TkFont *tkFontPtr)
{
    WaylandFont *fontPtr = (WaylandFont *)tkFontPtr;
    DeleteFont(fontPtr);
}

/*
 *----------------------------------------------------------------------
 * TkpGetFontFamilies --
 *
 *   Return a list of available font family names.
 *
 * Results:
 *   None (sets interpreter result).
 *
 * Side effects:
 *   Queries Fontconfig.
 *----------------------------------------------------------------------
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
        for (int i = 0; i < fs->nfont; i++) {
            FcChar8 *family = NULL;
            if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &family)
		== FcResultMatch) {
                Tcl_Obj *key = Tcl_NewStringObj((char *)family, -1);
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
 *----------------------------------------------------------------------
 * TkpGetSubFonts --
 *
 *   Return a list of fallback font families used by this logical font.
 *
 * Results:
 *   None (sets interpreter result).
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

void
TkpGetSubFonts(
	       Tcl_Interp *interp,
	       Tk_Font tkfont)
{
    WaylandFont *fontPtr   = (WaylandFont *)tkfont;
    Tcl_Obj     *resultPtr = Tcl_NewListObj(0, NULL);

    for (int i = 0; i < fontPtr->nfaces; i++) {
        FcChar8 *family = NULL;
        if (fontPtr->faces[i].source &&
	    FcPatternGetString(fontPtr->faces[i].source,
			       FC_FAMILY, 0, &family) == FcResultMatch
	    && family) {
            Tcl_ListObjAppendElement(NULL, resultPtr,
				     Tcl_NewStringObj((char *)family, -1));
        }
    }
    Tcl_SetObjResult(interp, resultPtr);
}

/*
 *----------------------------------------------------------------------
 * TkpGetFontAttrsForChar --
 *
 *   Return the font attributes (family, weight, slant) that would be used
 *   to render the given character.
 *
 * Results:
 *   None (faPtr is filled).
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

void
TkpGetFontAttrsForChar(
		       TCL_UNUSED(Tk_Window),
		       Tk_Font tkfont,
		       int c,
		       TkFontAttributes *faPtr)
{
    WaylandFont *fontPtr = (WaylandFont *)tkfont;
    *faPtr = fontPtr->font.fa;

    FcChar32 uc = (FcChar32)c;
    for (int i = 0; i < fontPtr->nfaces; i++) {
        if (fontPtr->faces[i].charset &&
	    FcCharSetHasChar(fontPtr->faces[i].charset, uc)) {
            FcChar8 *family = NULL;
            if (fontPtr->faces[i].source &&
		FcPatternGetString(fontPtr->faces[i].source,
				   FC_FAMILY, 0, &family) == FcResultMatch
		&& family) {
                faPtr->family = Tk_GetUid((char *)family);
            }
            return;
        }
    }
}

/*
 *----------------------------------------------------------------------
 * Tk_MeasureChars --
 *
 *   Measure the width of a substring, with optional line‑breaking.
 *
 * Results:
 *   Number of bytes consumed, and *lengthPtr = pixel width.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
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
 *----------------------------------------------------------------------
 * Tk_MeasureCharsInContext --
 *
 *   Measure a substring preserving full shaping context.
 *
 *   Simple LTR text: delegates to nvgTextGlyphPositions (cheap, exact).
 *   Complex/RTL text: uses ShapedGlyphBuffer cluster table from
 *   WaylandShaper_ShapeString with HarfBuzz advances for accurate layout.
 *
 * Results:
 *   Number of bytes consumed, and *lengthPtr = pixel width.
 *
 * Side effects:
 *   May shape the string and update internal caches.
 *----------------------------------------------------------------------
 */

int
Tk_MeasureCharsInContext(
			 Tk_Font     tkfont,
			 const char *source,
			 Tcl_Size    numBytes,
			 Tcl_Size    rangeStart,
			 Tcl_Size    rangeLength,
			 int         maxLength,
			 int         flags,
			 int        *lengthPtr)
{
    WaylandFont *fontPtr = (WaylandFont *)tkfont;

    if (rangeStart < 0 || rangeLength <= 0 ||
	rangeStart + rangeLength > numBytes ||
	(maxLength == 0 && !(flags & TK_AT_LEAST_ONE))) {
        *lengthPtr = 0;
        return 0;
    }
    if (maxLength > 32767) maxLength = 32767;

    int start = (int)rangeStart;
    int end   = (int)(rangeStart + rangeLength);

    /* Simple LTR path: nvgTextGlyphPositions. */
    if (IsSimpleOnly(source, (int)numBytes)) {
        NVGcontext *vg = TkGlfwGetNVGContextForMeasure();
        if (!vg || EnsureNvgFont(fontPtr, vg) < 0) {
            /* No NVG context: rough per-character estimate. */
            int         width          = 0;
            const char *p              = source + rangeStart;
            const char *endPtr         = source + rangeStart + rangeLength;
            const char *lastBreak      = p;
            int         lastBreakWidth = 0;
            while (p < endPtr) {
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
                int ch; p += Tcl_UtfToUniChar(p, &ch);
                width += fontPtr->pixelSize / 2;
            }
            *lengthPtr = width;
            return (int)(p - source - rangeStart);
        }

        nvgSave(vg);
        nvgFontFaceId(vg, fontPtr->nvgFontId);
        nvgFontSize(vg, (float)fontPtr->pixelSize);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);

        const char *rangePtr = source + rangeStart;
        const char *rangeEnd = rangePtr + rangeLength;

        int nchars = 0;
        for (const char *p = rangePtr; p < rangeEnd; ) {
            int ch; p += Tcl_UtfToUniChar(p, &ch); nchars++;
        }
        if (nchars == 0) {
            *lengthPtr = 0;
            nvgRestore(vg);
            return 0;
        }

        NVGglyphPosition  stackPos[256];
        NVGglyphPosition *positions = stackPos;
        if (nchars > 256)
            positions = (NVGglyphPosition *)
		Tcl_Alloc(nchars * sizeof(NVGglyphPosition));

        int   npos = nvgTextGlyphPositions(vg, 0, 0, rangePtr, rangeEnd,
                                           positions, nchars);
        float bounds[4];
        nvgTextBounds(vg, 0, 0, rangePtr, rangeEnd, bounds);
        float totalWidth = bounds[2];

        int         pixelWidth     = 0;
        const char *lastBreak      = rangePtr;
        int         lastBreakWidth = 0;
        const char *p              = rangePtr;
        int         pos            = 0;

        while (p < rangeEnd && pos < npos) {
            int ch;
            const char *next = p + Tcl_UtfToUniChar(p, &ch);
            float glyphRight = positions[pos].maxx;
            if (maxLength >= 0 && glyphRight > maxLength) {
                if ((flags & TK_WHOLE_WORDS) && lastBreak > rangePtr) {
                    p = lastBreak; pixelWidth = lastBreakWidth;
                } else if (flags & TK_PARTIAL_OK) {
                    pixelWidth = (int)ceil(glyphRight); p = next;
                }
                break;
            }
            pixelWidth = (int)ceil(glyphRight);
            if (ch == ' ' || ch == '\t') {
                lastBreak = next; lastBreakWidth = pixelWidth;
            }
            p = next; pos++;
        }

        if ((flags & TK_AT_LEAST_ONE) && p == rangePtr && rangePtr < rangeEnd) {
            int ch;
            const char *next = rangePtr + Tcl_UtfToUniChar(rangePtr, &ch);
            float glyphRight = (npos > 1) ? positions[1].x : totalWidth;
            pixelWidth = (int)ceil(glyphRight);
            p = next;
        }

        if (positions != stackPos) Tcl_Free(positions);
        nvgRestore(vg);
        *lengthPtr = pixelWidth;
        return (int)(p - rangePtr);
    }

    /* Complex / RTL path: ShapedGlyphBuffer cluster table. */
    ShapedGlyphBuffer sbuf;
    if (!WaylandShaper_ShapeString(&fontPtr->shaper, fontPtr, source,
                                   (int)numBytes, &sbuf)
	|| sbuf.glyphCount <= 0) {
        *lengthPtr = 0;
        return 0;
    }

    if (maxLength < 0) {
        int width = 0;
        for (int i = 0; i < sbuf.glyphCount; i++) {
            int bo  = sbuf.glyphs[i].byteOffset;
            int boe = bo + sbuf.glyphs[i].clusterLen;
            if (boe > start && bo < end) width += sbuf.glyphs[i].advanceX;
        }
        *lengthPtr = width;
        return (int)rangeLength;
    }

    typedef struct { int start; int end; int advance; } ClusterInfo;
    ClusterInfo clusters[MAX_GLYPHS];
    int clusterCount = 0;

    for (int i = 0; i < sbuf.glyphCount; i++) {
        int bo  = sbuf.glyphs[i].byteOffset;
        int len = sbuf.glyphs[i].clusterLen;
        if (len <= 0) continue;
        int boe = bo + len;
        if (boe <= start || bo >= end) continue;

        int found = -1;
        for (int j = 0; j < clusterCount; j++) {
            if (clusters[j].start == bo && clusters[j].end == boe)
                { found = j; break; }
        }
        if (found < 0) {
            if (clusterCount >= MAX_GLYPHS) break;
            found = clusterCount++;
            clusters[found].start   = bo;
            clusters[found].end     = boe;
            clusters[found].advance = 0;
        }
        clusters[found].advance += sbuf.glyphs[i].advanceX;
    }

    /* Sort by logical byte offset. */
    for (int i = 0; i < clusterCount - 1; i++) {
        for (int j = i + 1; j < clusterCount; j++) {
            if (clusters[j].start < clusters[i].start) {
                ClusterInfo tmp = clusters[i];
                clusters[i] = clusters[j];
                clusters[j] = tmp;
            }
        }
    }

    int width     = 0;
    int bestBytes = 0;
    for (int i = 0; i < clusterCount; i++) {
        int nw = width + clusters[i].advance;
        if (nw > maxLength) break;
        width     = nw;
        bestBytes = clusters[i].end - start;
    }

    if ((flags & TK_WHOLE_WORDS) && bestBytes > 0
	&& bestBytes < (int)rangeLength) {
        int rollback = -1;
        for (int i = bestBytes - 1; i >= 0; i--) {
            unsigned char c = (unsigned char)source[start + i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                { rollback = i + 1; break; }
        }
        if (rollback > 0 && rollback < bestBytes) {
            width = 0;
            int target = start + rollback;
            for (int i = 0; i < clusterCount; i++)
                if (clusters[i].end <= target) width += clusters[i].advance;
            bestBytes = rollback;
        }
    }

    if ((flags & TK_AT_LEAST_ONE) && bestBytes == 0 && clusterCount > 0) {
        bestBytes = clusters[0].end - start;
        width     = clusters[0].advance;
    }

    *lengthPtr = width;
    return bestBytes;
}

/*
 *----------------------------------------------------------------------
 * Tk_DrawChars --
 *
 *   Draw a string at the given coordinates (no rotation).
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Renders text.
 *----------------------------------------------------------------------
 */

void
Tk_DrawChars(
	     TCL_UNUSED(Display *),
	     Drawable drawable,
	     GC gc,
             Tk_Font tkfont,
	     const char *source,
	     Tcl_Size numBytes,
             int x,
	     int y)
{
    TkpDrawAngledCharsInContext(NULL, drawable, gc, tkfont,
                                source, numBytes, 0, numBytes,
                                (double)x, (double)y, 0.0);
}

/*
 *----------------------------------------------------------------------
 * TkDrawAngledChars --
 *
 *   Draw a string with an arbitrary rotation angle.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Renders rotated text.
 *----------------------------------------------------------------------
 */

void
TkDrawAngledChars(
		  TCL_UNUSED(Display *),
		  Drawable drawable,
		  GC gc,
                  Tk_Font tkfont,
		  const char *source,
		  Tcl_Size numBytes,
                  double x,
		  double y,
		  double angle)
{
    TkpDrawAngledCharsInContext(NULL, drawable, gc, tkfont,
                                source, numBytes, 0, numBytes,
                                x, y, angle);
}

/*
 *----------------------------------------------------------------------
 * Tk_DrawCharsInContext --
 *
 *   Draw a substring of a string at integer coordinates (no rotation).
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Renders text.
 *----------------------------------------------------------------------
 */

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
                                source, numBytes,
                                rangeStart, rangeLength,
                                (double)x, (double)y, 0.0);
}

/*
 *----------------------------------------------------------------------
 * TkpDrawAngledCharsInContext --
 *
 *   Canonical rendering entry point.
 *
 *   Simple LTR: single nvgText call per visible range (unchanged speed).
 *   Complex/RTL: cluster-by-cluster nvgText calls driven by the
 *   ShapedGlyphBuffer so that HarfBuzz advances and RTL reordering are
 *   reflected in the final pixel positions.
 *
 *   The NanoVG fallback chain (primary → fallback faces → emoji) is wired
 *   by EnsureNvgFont and handles any codepoint the primary face lacks.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Renders text, may load fonts into NanoVG.
 *----------------------------------------------------------------------
 */


void
TkpDrawAngledCharsInContext(
    TCL_UNUSED(Display *),
    Drawable   drawable,
    GC         gc,
    Tk_Font    tkfont,
    const char *source,
    Tcl_Size   numBytes,
    Tcl_Size   rangeStart,
    Tcl_Size   rangeLength,
    double     x,
    double     y,
    double     angle)
{
    WaylandFont *fontPtr = (WaylandFont *)tkfont;

    if (rangeStart < 0 || rangeLength <= 0 ||
        rangeStart + rangeLength > numBytes) return;

    NVGcontext *vg = TkGlfwGetNVGContext(drawable);
    if (!vg) return;

    int primaryId = EnsureNvgFont(fontPtr, vg);
    if (primaryId < 0) return;

    nvgFontFaceId(vg, primaryId);
    nvgFontSize(vg, (float)fontPtr->pixelSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgFillColor(vg, ColorFromGC(gc));

    const char *rangePtr = source + rangeStart;
    const char *rangeEnd = rangePtr + rangeLength;

    /* Starting X for partial range — more robust. */
    double drawX = x;
    if (rangeStart > 0) {
        float bounds[4];
        nvgTextBounds(vg, 0, 0, source, source + rangeStart, bounds);
        drawX += (double)bounds[2];
    }

    nvgSave(vg);
    nvgTranslate(vg, (float)drawX, (float)y);
    if (angle != 0.0) {
        nvgRotate(vg, (float)(-angle * NVG_PI / 180.0));
    }

    if (IsSimpleOnly(source, (int)numBytes)) {
        nvgText(vg, 0.0f, 0.0f, rangePtr, rangeEnd);
        nvgRestore(vg);
        goto decorations;
    }

    /* Complex / RTL path. */
    {
        ShapedGlyphBuffer sbuf;
        if (!WaylandShaper_ShapeString(&fontPtr->shaper, fontPtr,
                                       source, (int)numBytes, &sbuf)) {
            nvgRestore(vg);
            return;
        }

        int lastFaceId = primaryId;

        for (int i = 0; i < sbuf.glyphCount; i++) {
            int bo  = sbuf.glyphs[i].byteOffset;
            int boe = bo + sbuf.glyphs[i].clusterLen;

            if (boe <= (int)rangeStart || bo >= (int)(rangeStart + rangeLength))
                continue;

            if (i > 0 && sbuf.glyphs[i].byteOffset == sbuf.glyphs[i-1].byteOffset)
                continue;

            int faceIdx = sbuf.glyphs[i].faceIndex;
            if (faceIdx < 0 || faceIdx >= fontPtr->nfaces) faceIdx = 0;

            int faceId = fontPtr->faces[faceIdx].nvgFontId;
            if (faceId < 0) faceId = primaryId;

            if (faceId != lastFaceId) {
                nvgFontFaceId(vg, faceId);
                lastFaceId = faceId;
            }

            float gx = (float)sbuf.glyphs[i].x;
            float gy = (float)sbuf.glyphs[i].y;

            const char *cl    = sbuf.glyphs[i].clusterUtf8;
            const char *clEnd = cl + sbuf.glyphs[i].clusterUtf8Len;

            nvgText(vg, gx, gy, cl, clEnd);
        }
    }

    nvgRestore(vg);

decorations:
    if (fontPtr->font.fa.underline || fontPtr->font.fa.overstrike) {
        float bounds[4];
        float runWidth;

        nvgFontFaceId(vg, primaryId);
        nvgFontSize(vg, (float)fontPtr->pixelSize);
        nvgTextBounds(vg, 0, 0, rangePtr, rangeEnd, bounds);
        runWidth = bounds[2];

        nvgStrokeColor(vg, ColorFromGC(gc));
        nvgStrokeWidth(vg, (float)fontPtr->barHeight);

        if (fontPtr->font.fa.underline) {
            float uy = (float)(y + fontPtr->underlinePos);
            nvgBeginPath(vg);
            nvgMoveTo(vg, (float)x, uy);
            nvgLineTo(vg, (float)(x + runWidth), uy);
            nvgStroke(vg);
        }

        if (fontPtr->font.fa.overstrike) {
            float oy = (float)(y - fontPtr->font.fm.ascent / 2);
            nvgBeginPath(vg);
            nvgMoveTo(vg, (float)x, oy);
            nvgLineTo(vg, (float)(x + runWidth), oy);
            nvgStroke(vg);
        }
    }
}

/*
 *----------------------------------------------------------------------
 * TkPostscriptFontName --
 *
 *   Return a PostScript‑compatible font name for this logical font.
 *
 * Results:
 *   0 (success), and appends the name to dsPtr.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

int
TkPostscriptFontName(
		     Tk_Font tkfont,
		     Tcl_DString *dsPtr)
{
    WaylandFont *fontPtr = (WaylandFont *)tkfont;
    const char  *family  = fontPtr->font.fa.family
	? fontPtr->font.fa.family : "Helvetica";
    Tcl_DStringAppend(dsPtr, family, -1);
    if (fontPtr->font.fa.weight == TK_FW_BOLD)
        Tcl_DStringAppend(dsPtr, "-Bold", -1);
    if (fontPtr->font.fa.slant == TK_FS_ITALIC)
        Tcl_DStringAppend(dsPtr, "-Italic", -1);
    return 0;
}


/*
 *----------------------------------------------------------------------
 * TkpDrawCharsInContext --
 *
 *   Delegating wrapper for Tk_DrawCharsInContext.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Renders text.
 *----------------------------------------------------------------------
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
 *----------------------------------------------------------------------
 * TkpMeasureCharsInContext --
 *
 *   Delegating wrapper for Tk_MeasureCharsInContext.
 *
 * Results:
 *   Number of bytes consumed.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
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
				    rangeStart, rangeLength,
				    maxLength, flags, lengthPtr);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
