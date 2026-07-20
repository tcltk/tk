/*
 * tkWaylandFont.c --
 *
 *   Wayland/GLFW/NanoVG platform font implementation with full HarfBuzz
 *   shaping, SheenBidi bidirectional analysis, and Fontconfig multi-face
 *   fallback.
 *
 *
 * Copyright © 1996-1998 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkWaylandInt.h"

#include <fontconfig/fontconfig.h>
#include <nanovg.h>
#include <hb.h>
#include <SheenBidi/SheenBidi.h>

#include "stb_truetype.h"

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

/* Forward declarations of static helper functions. */
static int        GetBidiRuns(FcChar32 *ucs4, int charCount,
			      BidiRun *runs, int maxRuns);
static bool       IsSimpleOnly(const char *str, int len);
static bool       IsEmoji(FcChar32 uc);
static int        GetEmojiFaceIndex(WaylandFont *fontPtr);
static int        GetRunFaceIndex(WaylandFont *fontPtr, FcChar32 *ucs4Chars,
				  int runStart, int runLen);
static int        FindFaceCoveringRange(WaylandFont *fontPtr, FcChar32 *ucs4,
					int start, int len);
static hb_font_t *GetHbFont(WaylandFont *fontPtr, int faceIndex);
static int        EnsureNvgFaceFont(WaylandFont *fontPtr, int faceIndex,
				    NVGcontext *vg);
static void       InitFont(Tk_Window tkwin, const TkFontAttributes *faPtr,
			   WaylandFont *fontPtr);
static void       DeleteFont(WaylandFont *fontPtr);
static NVGcolor   ColorFromGC(GC gc);
static bool       IsSerifFace(FcPattern *pat);
static bool       IsSansSerifFace(FcPattern *pat);
static char      *ComposeUTF8String(const char *source, int len);
static FcChar32   UnicodeCompose(FcChar32 base, FcChar32 mark);

/*
 *----------------------------------------------------------------------
 * IsSerifFace --
 *
 *   Check to see if a font has serifs.
 *
 * Results:
 *   True if the font face has serifs, false otherwise. 
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */
     
static bool
IsSerifFace(FcPattern *pat)
{
    if (!pat) return false;
    
    FcChar8 *family = NULL;
    if (FcPatternGetString(pat, FC_FAMILY, 0, &family) != FcResultMatch || !family) {
        return false;
    }
    
    const char *fam = (const char *)family;
    
    /* Check for explicit serif indicators. */
    if (strcasestr(fam, "serif") && !strcasestr(fam, "sans")) {
        return true;
    }
    
    /* Known serif font families. */
    const char *serifFamilies[] = {
        "times", "times new roman", "georgia", "garamond", "palatino",
        "bookman", "new century schoolbook", "utopia", "bitstream charter",
        "cambria", "didot", "bodoni", "caslon", "baskerville",
        "minion", "adobe caslon", "goudy", "hoefler text",
        NULL
    };
    
    for (int i = 0; serifFamilies[i]; i++) {
        if (strcasestr(fam, serifFamilies[i])) {
            return true;
        }
    }
    
    return false;
}

/*
 *----------------------------------------------------------------------
 * IsSansSerifFace --
 *
 *   Check to see if a font has no serifs.
 *
 * Results:
 *   True if the font face is sans serif, false otherwise. 
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

static bool
IsSansSerifFace(FcPattern *pat)
{
    if (!pat) return false;
    
    FcChar8 *family = NULL;
    if (FcPatternGetString(pat, FC_FAMILY, 0, &family) != FcResultMatch || !family) {
        return false;
    }
    
    const char *fam = (const char *)family;
    
    /* Check for explicit sans-serif indicators. */
    if (strcasestr(fam, "sans") || strcasestr(fam, "sans-serif") ||
        strcasestr(fam, "helvetica") || strcasestr(fam, "arial") ||
        strcasestr(fam, "verdana") || strcasestr(fam, "tahoma") ||
        strcasestr(fam, "franklin gothic") || strcasestr(fam, "futura") ||
        strcasestr(fam, "gill sans") || strcasestr(fam, "lucida sans") ||
        strcasestr(fam, "myriad") || strcasestr(fam, "proxima nova") ||
        strcasestr(fam, "open sans") || strcasestr(fam, "noto sans") ||
        strcasestr(fam, "dejavu sans") || strcasestr(fam, "liberation sans") ||
        strcasestr(fam, "roboto") || strcasestr(fam, "ubuntu")) {
        return true;
    }
    
    return false;
}

/*
 *----------------------------------------------------------------------
 * UnicodeCompose --
 *
 *   Attempt to compose a base character and combining mark.
 *
 * Results:
 *   Composed Unicode character, or 0 if no composition is possible.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 * UnicodeCompose --
 *
 *   Attempt to compose a base character and combining mark.
 *   Extended to support Vietnamese horn letters, ring above, and their combinations.
 *
 * Results:
 *   Composed Unicode character, or 0 if no composition is possible.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 * UnicodeCompose --
 *
 *   Attempt to compose a base character and combining mark.
 *   Extended to support Vietnamese horn letters, ring above, and their combinations.
 *
 * Results:
 *   Composed Unicode character, or 0 if no composition is possible.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

static FcChar32
UnicodeCompose(FcChar32 base, FcChar32 mark)
{
    /* Common Latin composition table */
    struct {
        FcChar32 base;
        FcChar32 mark;
        FcChar32 composed;
    } compTable[] = {
        /* -------- Latin lowercase -------- */
        {0x0061, 0x0300, 0x00E0},  /* a + grave = à */
        {0x0061, 0x0301, 0x00E1},  /* a + acute = á */
        {0x0061, 0x0302, 0x00E2},  /* a + circumflex = â */
        {0x0061, 0x0303, 0x00E3},  /* a + tilde = ã */
        {0x0061, 0x0308, 0x00E4},  /* a + diaeresis = ä */
        {0x0061, 0x030A, 0x00E5},  /* a + ring = å */
        {0x0061, 0x0304, 0x0101},  /* a + macron = ā */
        {0x0061, 0x0306, 0x0103},  /* a + breve = ă */
        
        {0x0065, 0x0300, 0x00E8},  /* e + grave = è */
        {0x0065, 0x0301, 0x00E9},  /* e + acute = é */
        {0x0065, 0x0302, 0x00EA},  /* e + circumflex = ê */
        {0x0065, 0x0308, 0x00EB},  /* e + diaeresis = ë */
        {0x0065, 0x0304, 0x0113},  /* e + macron = ē */
        {0x0065, 0x0306, 0x0115},  /* e + breve = ĕ */
        
        {0x0069, 0x0300, 0x00EC},  /* i + grave = ì */
        {0x0069, 0x0301, 0x00ED},  /* i + acute = í */
        {0x0069, 0x0302, 0x00EE},  /* i + circumflex = î */
        {0x0069, 0x0308, 0x00EF},  /* i + diaeresis = ï */
        {0x0069, 0x0304, 0x012B},  /* i + macron = ī */
        {0x0069, 0x0306, 0x012D},  /* i + breve = ĭ */
        
        {0x006F, 0x0300, 0x00F2},  /* o + grave = ò */
        {0x006F, 0x0301, 0x00F3},  /* o + acute = ó */
        {0x006F, 0x0302, 0x00F4},  /* o + circumflex = ô */
        {0x006F, 0x0303, 0x00F5},  /* o + tilde = õ */
        {0x006F, 0x0308, 0x00F6},  /* o + diaeresis = ö */
        {0x006F, 0x030A, 0x00F8},  /* o + ring = ø (actually slash, but ring is close) */
        {0x006F, 0x030B, 0x0151},  /* o + double acute = ő */
        {0x006F, 0x0304, 0x014D},  /* o + macron = ō */
        {0x006F, 0x0306, 0x014F},  /* o + breve = ŏ */
        
        {0x0075, 0x0300, 0x00F9},  /* u + grave = ù */
        {0x0075, 0x0301, 0x00FA},  /* u + acute = ú */
        {0x0075, 0x0302, 0x00FB},  /* u + circumflex = û */
        {0x0075, 0x0308, 0x00FC},  /* u + diaeresis = ü */
        {0x0075, 0x030A, 0x016F},  /* u + ring = ů */
        {0x0075, 0x0304, 0x016B},  /* u + macron = ū */
        {0x0075, 0x0306, 0x016D},  /* u + breve = ŭ */
        {0x0075, 0x030B, 0x0171},  /* u + double acute = ű */
        
        {0x006E, 0x0303, 0x00F1},  /* n + tilde = ñ */
        
        /* -------- Latin uppercase -------- */
        {0x0041, 0x0300, 0x00C0},  /* A + grave = À */
        {0x0041, 0x0301, 0x00C1},  /* A + acute = Á */
        {0x0041, 0x0302, 0x00C2},  /* A + circumflex = Â */
        {0x0041, 0x0303, 0x00C3},  /* A + tilde = Ã */
        {0x0041, 0x0308, 0x00C4},  /* A + diaeresis = Ä */
        {0x0041, 0x030A, 0x00C5},  /* A + ring = Å */
        {0x0041, 0x0304, 0x0100},  /* A + macron = Ā */
        {0x0041, 0x0306, 0x0102},  /* A + breve = Ă */
        
        {0x0045, 0x0300, 0x00C8},  /* E + grave = È */
        {0x0045, 0x0301, 0x00C9},  /* E + acute = É */
        {0x0045, 0x0302, 0x00CA},  /* E + circumflex = Ê */
        {0x0045, 0x0308, 0x00CB},  /* E + diaeresis = Ë */
        {0x0045, 0x0304, 0x0112},  /* E + macron = Ē */
        {0x0045, 0x0306, 0x0114},  /* E + breve = Ĕ */
        
        {0x0049, 0x0300, 0x00CC},  /* I + grave = Ì */
        {0x0049, 0x0301, 0x00CD},  /* I + acute = Í */
        {0x0049, 0x0302, 0x00CE},  /* I + circumflex = Î */
        {0x0049, 0x0308, 0x00CF},  /* I + diaeresis = Ï */
        {0x0049, 0x0304, 0x012A},  /* I + macron = Ī */
        {0x0049, 0x0306, 0x012C},  /* I + breve = Ĭ */
        
        {0x004F, 0x0300, 0x00D2},  /* O + grave = Ò */
        {0x004F, 0x0301, 0x00D3},  /* O + acute = Ó */
        {0x004F, 0x0302, 0x00D4},  /* O + circumflex = Ô */
        {0x004F, 0x0303, 0x00D5},  /* O + tilde = Õ */
        {0x004F, 0x0308, 0x00D6},  /* O + diaeresis = Ö */
        {0x004F, 0x030A, 0x00D8},  /* O + ring = Ø (actually slash) */
        {0x004F, 0x030B, 0x0150},  /* O + double acute = Ő */
        {0x004F, 0x0304, 0x014C},  /* O + macron = Ō */
        {0x004F, 0x0306, 0x014E},  /* O + breve = Ŏ */
        
        {0x0055, 0x0300, 0x00D9},  /* U + grave = Ù */
        {0x0055, 0x0301, 0x00DA},  /* U + acute = Ú */
        {0x0055, 0x0302, 0x00DB},  /* U + circumflex = Û */
        {0x0055, 0x0308, 0x00DC},  /* U + diaeresis = Ü */
        {0x0055, 0x030A, 0x016E},  /* U + ring = Ů */
        {0x0055, 0x0304, 0x016A},  /* U + macron = Ū */
        {0x0055, 0x0306, 0x016C},  /* U + breve = Ŭ */
        {0x0055, 0x030B, 0x0170},  /* U + double acute = Ű */
        
        {0x004E, 0x0303, 0x00D1},  /* N + tilde = Ñ */

        /* -------- Vietnamese horn letters -------- */
        /* 
         * The "horn" is U+031B. The base characters are:
         *   U+01A0 = O with horn (Ơ)  - lowercase: U+01A1 (ơ)
         *   U+01AF = U with horn (Ư)  - lowercase: U+01B0 (ư)
         * 
         * These are then combined with Vietnamese tone marks:
         *   grave (U+0300), acute (U+0301), hook above (U+0309),
         *   tilde (U+0303), dot below (U+0323)
         */

        /* Lowercase: o with horn + tone marks */
        {0x01A1, 0x0301, 0x1EDB},  /* ơ + acute = ớ */
        {0x01A1, 0x0300, 0x1EDD},  /* ơ + grave = ờ */
        {0x01A1, 0x0309, 0x1EDF},  /* ơ + hook above = ở */
        {0x01A1, 0x0303, 0x1ED9},  /* ơ + tilde = ỗ */
        {0x01A1, 0x0323, 0x1EE1},  /* ơ + dot below = ợ */

        /* Lowercase: u with horn + tone marks */
        {0x01B0, 0x0301, 0x1EEB},  /* ư + acute = ứ */
        {0x01B0, 0x0300, 0x1EED},  /* ư + grave = ừ */
        {0x01B0, 0x0309, 0x1EEF},  /* ư + hook above = ử */
        {0x01B0, 0x0303, 0x1EF1},  /* ư + tilde = ữ */
        {0x01B0, 0x0323, 0x1EF3},  /* ư + dot below = ự */

        /* Uppercase: O with horn + tone marks */
        {0x01A0, 0x0301, 0x1EDA},  /* Ơ + acute = Ớ */
        {0x01A0, 0x0300, 0x1EDC},  /* Ơ + grave = Ờ */
        {0x01A0, 0x0309, 0x1EDE},  /* Ơ + hook above = Ở */
        {0x01A0, 0x0303, 0x1ED8},  /* Ơ + tilde = Ỗ */
        {0x01A0, 0x0323, 0x1EE0},  /* Ơ + dot below = Ợ */

        /* Uppercase: U with horn + tone marks */
        {0x01AF, 0x0301, 0x1EEA},  /* Ư + acute = Ứ */
        {0x01AF, 0x0300, 0x1EEC},  /* Ư + grave = Ừ */
        {0x01AF, 0x0309, 0x1EEE},  /* Ư + hook above = Ử */
        {0x01AF, 0x0303, 0x1EF0},  /* Ư + tilde = Ữ */
        {0x01AF, 0x0323, 0x1EF2},  /* Ư + dot below = Ự */

        {0, 0, 0}
    };

    for (int i = 0; compTable[i].base != 0; i++) {
        if (compTable[i].base == base && compTable[i].mark == mark) {
            return compTable[i].composed;
        }
    }

    return 0;
}

/*
 *----------------------------------------------------------------------
 * ComposeUTF8String --
 *
 *   Compose combining diacritical marks in a UTF-8 string.
 *   Uses a composition table for common Latin sequences.
 *
 * Results:
 *   Newly allocated UTF-8 string in NFC form, or NULL on failure.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

static char*
ComposeUTF8String(const char *source, int len)
{
    if (!source || len <= 0) return NULL;
    
    /* Check if there are any combining characters. */
    bool hasCombining = false;
    for (int i = 0; i < len; ) {
        FcChar32 uc;
        int clen = FcUtf8ToUcs4((const FcChar8 *)(source + i), &uc, len - i);
        if (clen <= 0) { i++; continue; }
        if (uc >= 0x0300 && uc <= 0x036F) {
            hasCombining = true;
            break;
        }
        i += clen;
    }
    
    if (!hasCombining) {
        char *result = (char *)malloc(len + 1);
        if (result) {
            memcpy(result, source, len);
            result[len] = '\0';
        }
        return result;
    }
    
    /* Walk through the string and compose clusters. */
    char *result = NULL;
    int resultLen = 0;
    int i = 0;
    
    while (i < len) {
        FcChar32 base = 0;
        int baseLen = 0;
        int baseStart = i;
        
        /* Read the base character. */
        if (i < len) {
            FcChar32 uc;
            int clen = FcUtf8ToUcs4((const FcChar8 *)(source + i), &uc, len - i);
            if (clen > 0) {
                /* Check if this is a combining mark with no base (unlikely). */
                if (uc >= 0x0300 && uc <= 0x036F) {
                    /* No base, copy the mark as-is. */
                    char *newResult = (char *)realloc(result, resultLen + clen + 1);
                    if (newResult) {
                        result = newResult;
                        memcpy(result + resultLen, source + i, clen);
                        resultLen += clen;
                    }
                    i += clen;
                    continue;
                }
                base = uc;
                baseLen = clen;
                i += clen;
            } else {
                i++;
                continue;
            }
        } else {
            break;
        }
        
        /* Look ahead for combining marks. */
        FcChar32 combined = base;
        bool hadCombining = false;
        
        while (i < len) {
            FcChar32 mark;
            int markLen = FcUtf8ToUcs4((const FcChar8 *)(source + i), &mark, len - i);
            if (markLen <= 0) { i++; continue; }
            if (mark >= 0x0300 && mark <= 0x036F) {
                /* Try to compose. */
                FcChar32 composed = UnicodeCompose(combined, mark);
                if (composed > 0) {
                    combined = composed;
                    hadCombining = true;
                    i += markLen;
                    continue;
                } else if (mark == 0x0303) { /* Tilde */
                    /* Try some common Vietnamese combinations */
                    if (combined == 0x01B0) { /* U with horn + tilde = Ữ */
                        combined = 0x1EEF;
                        hadCombining = true;
                        i += markLen;
                        continue;
                    } else if (combined == 0x01A0) { /* O with horn + tilde = Ỗ */
                        combined = 0x1ED6;
                        hadCombining = true;
                        i += markLen;
                        continue;
                    } else if (combined == 0x0041 || combined == 0x0061) {
                        /* Already handled by UnicodeCompose for A/a. */
                    }
                }
                /* Cannot compose this mark; stop and keep what we have. */
                break;
            } else {
                /* Not a combining mark, stop. */
                break;
            }
        }
        
        if (hadCombining && combined != base) {
            /* Encode composed character as UTF-8. */
            char utf8[8];
            int utf8Len = 0;
            if (combined <= 0x7F) {
                utf8[0] = (char)combined;
                utf8Len = 1;
            } else if (combined <= 0x7FF) {
                utf8[0] = (char)(0xC0 | ((combined >> 6) & 0x1F));
                utf8[1] = (char)(0x80 | (combined & 0x3F));
                utf8Len = 2;
            } else if (combined <= 0xFFFF) {
                utf8[0] = (char)(0xE0 | ((combined >> 12) & 0x0F));
                utf8[1] = (char)(0x80 | ((combined >> 6) & 0x3F));
                utf8[2] = (char)(0x80 | (combined & 0x3F));
                utf8Len = 3;
            } else {
                utf8[0] = (char)(0xF0 | ((combined >> 18) & 0x07));
                utf8[1] = (char)(0x80 | ((combined >> 12) & 0x3F));
                utf8[2] = (char)(0x80 | ((combined >> 6) & 0x3F));
                utf8[3] = (char)(0x80 | (combined & 0x3F));
                utf8Len = 4;
            }
            
            char *newResult = (char *)realloc(result, resultLen + utf8Len + 1);
            if (newResult) {
                result = newResult;
                memcpy(result + resultLen, utf8, utf8Len);
                resultLen += utf8Len;
            }
        } else {
            /* No composition happened; copy the base as-is. */
            char *newResult = (char *)realloc(result, resultLen + baseLen + 1);
            if (newResult) {
                result = newResult;
                memcpy(result + resultLen, source + baseStart, baseLen);
                resultLen += baseLen;
            }
        }
    }
    
    if (result) {
        result[resultLen] = '\0';
    }
    
    return result;
}

/*
 *----------------------------------------------------------------------
 * IsSimpleOnly --
 *
 *   Fast-path classifier for text that does NOT require HarfBuzz shaping
 *   or SheenBidi analysis. This function now reflects the modern Wayland
 *   shaping pipeline: only pure ASCII, Latin-1/Latin Extended, and
 *   Hiragana/Katakana are considered "simple". Everything else—including
 *   CJK ideographs, Hangul, Jamo, all RTL scripts, Indic/Thai/Lao, emoji,
 *   and any supplementary-plane characters—forces the full HarfBuzz +
 *   SheenBidi shaping path.
 *
 *   The fast path is intentionally narrow. It exists only to avoid the
 *   overhead of HarfBuzz for trivially shaped, strictly LTR text where
 *   glyph IDs map 1:1 to Unicode codepoints and no OpenType features,
 *   fallback segmentation, or cluster-level logic are required.
 *
 * Results:
 *   true if the string is simple (eligible for the fast path),
 *   false if the string must be shaped by HarfBuzz.
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

        /* 
         * Combining diacritical marks MUST go through HarfBuzz shaping.
         * Without HarfBuzz, NanoVG will render them as separate glyphs
         * with full advances, breaking mark positioning.
         */
        if (uc >= 0x0300 && uc <= 0x036F) {  /* Combining diacritics */
            return false;
        }

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

        /* Safe for fast path: Latin extended. */
        int isSafe = (uc <= 0x024F);
        if (!isSafe) return false;

        i += clen;
    }
    return true;
}

/*
 *----------------------------------------------------------------------
 * IsEmoji --
 *
 *   Determine whether a Unicode codepoint is an emoji character.
 *   This covers the full emoji ranges including ZWJ, flags, and skin
 *   tone modifiers.
 *
 * Results:
 *   true if the codepoint is an emoji, false otherwise.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

static bool
IsEmoji(FcChar32 uc)
{
    /* Extended emoji ranges - more comprehensive */
    return
        (uc >= 0x1F000 && uc <= 0x1FAFF) ||   /* Main emoji blocks */
        (uc >= 0x1F300 && uc <= 0x1F9FF) ||
        (uc >= 0x2600  && uc <= 0x27BF)  ||   /* Misc symbols */
        (uc >= 0x2300  && uc <= 0x23FF)  ||   /* Misc technical */
        (uc >= 0x2B00  && uc <= 0x2BFF)  ||   /* Misc symbols arrows */
        (uc >= 0xFE00  && uc <= 0xFE0F)  ||   /* Variation selectors */
        (uc == 0x200D) ||                     /* Zero-width joiner */
        (uc >= 0x1F1E6 && uc <= 0x1F1FF) ||   /* Regional indicators (flags) */
        (uc >= 0x1F3FB && uc <= 0x1F3FF) ||   /* Skin tone modifiers */
        (uc >= 0x00A9 && uc <= 0x00AE)  ||   /* Copyright/registered */
        (uc == 0x2122) ||                     /* TM */
        (uc == 0x2139) ||                     /* Information */
        (uc == 0x2194) || (uc == 0x2195) ||  /* Arrows */
        (uc >= 0x2196 && uc <= 0x2199) ||
        (uc >= 0x21A9 && uc <= 0x21AA) ||
        (uc == 0x231A) || (uc == 0x231B) ||
        (uc == 0x2328) ||
        (uc >= 0x23CF && uc <= 0x23FF) ||
        (uc >= 0x24C2 && uc <= 0x24FF) ||
        (uc >= 0x25A0 && uc <= 0x25FF) ||
        (uc >= 0x2600 && uc <= 0x26FF) ||
        (uc >= 0x2700 && uc <= 0x27BF) ||
        (uc >= 0x2934 && uc <= 0x2935) ||
        (uc >= 0x2B05 && uc <= 0x2B07) ||
        (uc == 0x2B1B) || (uc == 0x2B1C) ||
        (uc == 0x2B50) || (uc == 0x2B55) ||
        (uc == 0x3030) || (uc == 0x303D) ||
        (uc == 0x3297) || (uc == 0x3299) ||
        (uc >= 0x1F004 && uc <= 0x1F0CF) ||   /* Mahjong/playing cards */
        (uc >= 0x1F170 && uc <= 0x1F251) ||   /* Enclosed alphanum */
        (uc >= 0x1F600 && uc <= 0x1F64F) ||   /* Emoticons */
        (uc >= 0x1F680 && uc <= 0x1F6C0) ||   /* Transport */
        (uc >= 0x1F6C0 && uc <= 0x1F6FF) ||   /* Transport symbols */
        (uc >= 0x1F700 && uc <= 0x1F77F) ||   /* Alchemical */
        (uc >= 0x1F780 && uc <= 0x1F7FF) ||   /* Geometric */
        (uc >= 0x1F800 && uc <= 0x1F8FF) ||   /* Supplemental arrows */
        (uc >= 0x1F900 && uc <= 0x1F9FF) ||   /* Supplemental symbols */
        (uc >= 0x1FA00 && uc <= 0x1FA6F) ||   /* Chess/symbols */
        (uc >= 0x1FA70 && uc <= 0x1FAFF);     /* More symbols */
}

/*
 *----------------------------------------------------------------------
 * GetEmojiFaceIndex --
 *
 *   Find the best face for rendering emoji characters by scanning
 *   the font's face list for a face that contains typical emoji
 *   codepoints, with a preference for actual emoji fonts.
 *
 *   This function tries multiple strategies to find an emoji font:
 *   1. Check font family names for emoji indicators
 *   2. Check for high coverage of emoji codepoints
 *   3. Check for specific emoji fonts by name
 *
 * Results:
 *   Face index (0..nfaces-1) that supports emoji, or 0 as fallback.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

static int
GetEmojiFaceIndex(WaylandFont *fontPtr)
{
    if (!fontPtr || fontPtr->nfaces <= 0) return 0;

    /* Common emoji test codepoints - more extensive set. */
    FcChar32 emojiTestPoints[] = {
        0x1F600, /* Grinning face */
        0x1F602, /* Face with tears of joy */
        0x1F60D, /* Smiling face with heart-eyes */
        0x1F44D, /* Thumbs up */
        0x1F64F, /* Folded hands */
        0x1F680, /* Rocket */
        0x2600,  /* Sun */
        0x260E,  /* Telephone */
        0x261D,  /* Pointing hand */
        0x270A,  /* Raised fist */
        0x2728,  /* Sparkles */
        0x2B50,  /* Star */
        0x1F3A8, /* Artist palette */
        0x1F4A9, /* Pile of poo */
        0x1F525, /* Fire */
        0x1F4B0, /* Money bag */
        0x1F3C6, /* Trophy */
        0x1F3C8, /* Football */
        0x1F3E0, /* House */
        0x1F4BB  /* Laptop */
    };
    int numTestPoints = sizeof(emojiTestPoints) / sizeof(emojiTestPoints[0]);

    /*
     * First pass: look for a monochrome emoji font by family name.
     * NanoVG rasterizes via stb_truetype, which only reads classic
     * 'glyf' outlines - it cannot draw color bitmap (CBDT/CBLC) or
     * COLR/CPAL glyphs at all, so color emoji fonts are never usable
     * here and are explicitly skipped even if one happens to be
     * installed on the system.
     */
    for (int fi = 0; fi < fontPtr->nfaces; fi++) {
        FcPattern *pat = fontPtr->faces[fi].source;
        if (!pat) continue;

        FcChar8 *family = NULL;
        if (FcPatternGetString(pat, FC_FAMILY, 0, &family) != FcResultMatch ||
            !family) {
            continue;
        }
        const char *fam = (const char *)family;
        bool looksLikeEmoji =
            strcasestr(fam, "emoji") ||
            strcasestr(fam, "twemoji") ||
            strcasestr(fam, "emojione") ||
            strcasestr(fam, "symbola");
        if (!looksLikeEmoji) continue;
        if (strcasestr(fam, "color")) continue;

        /* Verify it actually has emoji coverage */
        FcCharSet *cs = fontPtr->faces[fi].charset;
        if (cs) {
            int score = 0;
            for (int i = 0; i < numTestPoints && i < 8; i++) {
                if (FcCharSetHasChar(cs, emojiTestPoints[i])) {
                    score++;
                }
            }
            if (score >= 2) {
                return fi;
            }
        }
    }

    /*
     * Second pass: try to find any face that covers many emoji codepoints,
     * but still require the family name to look emoji/symbol-related so
     * an ordinary sans/serif font that merely happens to carry a couple of
     * dingbat glyphs can't be mistaken for a dedicated emoji font (that
     * produces the wrong, non-color, badly-metriced glyphs).
     */
    int bestFace = -1;
    int bestScore = 0;

    for (int fi = 0; fi < fontPtr->nfaces; fi++) {
        FcCharSet *cs = fontPtr->faces[fi].charset;
        if (!cs) continue;

        FcPattern *pat = fontPtr->faces[fi].source;
        FcChar8 *family = NULL;
        if (pat) FcPatternGetString(pat, FC_FAMILY, 0, &family);
        bool nameHint = family &&
            (strcasestr((const char *)family, "emoji") ||
             strcasestr((const char *)family, "symbol") ||
             strcasestr((const char *)family, "dingbat"));
        if (!nameHint) continue;

        int score = 0;
        for (int i = 0; i < numTestPoints; i++) {
            if (FcCharSetHasChar(cs, emojiTestPoints[i])) {
                score++;
            }
        }

        if (score > bestScore) {
            bestScore = score;
            bestFace = fi;
        }
    }

    /* Require a majority of the test codepoints, not just a handful. */
    if (bestFace >= 0 && bestScore >= (numTestPoints / 2)) {
        return bestFace;
    }

    /* 
     * Third pass: try specific monochrome emoji font paths as a last
     * resort. Color emoji files (NotoColorEmoji.ttf etc.) are deliberately
     * not listed here - stb_truetype can't rasterize their glyph tables,
     * so matching one would just produce blank glyphs again. 
     */
    static const char *emojiFontPaths[] = {
        "/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/noto/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/google-noto/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/truetype/ancient-scripts/Symbola.ttf",
        "/usr/share/fonts/truetype/symbola/Symbola.ttf",
        NULL
    };

    /* Check if any of the faces already loaded match these paths. */
    for (int fi = 0; fi < fontPtr->nfaces; fi++) {
        if (fontPtr->faces[fi].filePath) {
            for (int p = 0; emojiFontPaths[p]; p++) {
                if (strstr(fontPtr->faces[fi].filePath, emojiFontPaths[p]) ||
                    strstr(emojiFontPaths[p], fontPtr->faces[fi].filePath)) {
                    return fi;
                }
            }
        }
    }

    /* Fallback: primary face. */
    return 0;
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
 *   Emoji characters are always routed to the emoji face to ensure
 *   proper rendering of emoji sequences and to prevent partial
 *   coverage from non-emoji fonts.
 *
 *   This function prioritizes sans-serif faces over serif
 *   faces when multiple fonts cover the same character.
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

    /* Emoji always uses the emoji face. */
    if (IsEmoji(uc)) {
        int emojiFace = GetEmojiFaceIndex(fontPtr);
        if (emojiFace > 0) {
            return emojiFace;
        }
        return 0;
    }

    WaylandShaper *shaper  = &fontPtr->shaper;
    int            cacheIdx = uc & 63;

    if (shaper->charCache[cacheIdx].uc == uc) {
        return shaper->charCache[cacheIdx].faceIdx;
    }

    /* First pass: find a sans-serif face that covers this character. */
    int bestSerifFace = -1;
    for (int fi = 0; fi < fontPtr->nfaces; fi++) {
        if (!fontPtr->faces[fi].charset ||
            !FcCharSetHasChar(fontPtr->faces[fi].charset, uc)) {
            continue;
        }
        
        FcPattern *pat = fontPtr->faces[fi].source;
        if (!pat) continue;
        
        /* Check if this is a serif face. */
        if (IsSerifFace(pat)) {
            if (bestSerifFace < 0) bestSerifFace = fi;
            continue;
        }
        
        /* Found a sans-serif face. */
        shaper->charCache[cacheIdx].uc      = uc;
        shaper->charCache[cacheIdx].faceIdx = fi;
        return fi;
    }
    
    /* If we found a serif face, use it as a fallback. */
    if (bestSerifFace >= 0) {
        shaper->charCache[cacheIdx].uc      = uc;
        shaper->charCache[cacheIdx].faceIdx = bestSerifFace;
        return bestSerifFace;
    }

    shaper->charCache[cacheIdx].uc      = uc;
    shaper->charCache[cacheIdx].faceIdx = 0;
    return 0;
}

/*
 *----------------------------------------------------------------------
 * FindFaceCoveringRange --
 *
 *   Find a face index that covers all characters in the given UCS-4 range.
 *   This is used to select a single font for an entire sub-run, avoiding
 *   face changes that would break combining marks (which have INHERITED script).
 *
 *   If any character in the range is an emoji, the emoji face is used
 *   for the entire range to ensure that emoji sequences (ZWJ, skin tones,
 *   flags) remain in a single font and shape correctly.
 *
 *   This function prioritizes sans-serif faces over serif
 *   faces when multiple fonts cover the same characters.
 *
 * Results:
 *   Face index (0..nfaces-1) that covers all characters, or 0 as fallback.
 *
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */
 
static int
FindFaceCoveringRange(
    WaylandFont *fontPtr,
    FcChar32 *ucs4,
    int start,
    int len)
{
    if (len <= 0) return 0;

    /* First, check if this is a combining character sequence that should be kept together. */
    bool hasEmoji = false;
    bool hasNonEmoji = false;
    for (int i = start; i < start + len; i++) {
        if (IsEmoji(ucs4[i])) {
            hasEmoji = true;
        } else {
            hasNonEmoji = true;
        }
    }

    /* If the range has both emoji and non-emoji, try to find a sans-serif face that covers non-emoji. */
    if (hasEmoji && hasNonEmoji) {
        for (int fi = 0; fi < fontPtr->nfaces; fi++) {
            FcPattern *pat = fontPtr->faces[fi].source;
            if (!pat) continue;
            
            /* Skip serif fonts. */
            if (IsSerifFace(pat)) continue;
            
            FcCharSet *cs = fontPtr->faces[fi].charset;
            if (!cs) continue;
            
            int ok = 1;
            for (int i = start; i < start + len; i++) {
                if (!IsEmoji(ucs4[i]) && !FcCharSetHasChar(cs, ucs4[i])) {
                    ok = 0;
                    break;
                }
            }
            if (ok) return fi;
        }
        /* Fall through to emoji-only handling. */
    }

    /* If any character in the range is an emoji, use the emoji face for emoji-only runs. */
    if (hasEmoji && !hasNonEmoji) {
        return GetEmojiFaceIndex(fontPtr);
    }

    /* First pass: find any sans-serif face that covers all characters. */
    for (int fi = 0; fi < fontPtr->nfaces; fi++) {
        FcPattern *pat = fontPtr->faces[fi].source;
        if (!pat) continue;
        
        /* Skip serif fonts unless absolutely necessary. */
        if (IsSerifFace(pat)) continue;
        
        FcCharSet *cs = fontPtr->faces[fi].charset;
        if (!cs) continue;
        int ok = 1;
        for (int i = start; i < start + len; i++) {
            if (!FcCharSetHasChar(cs, ucs4[i])) {
                ok = 0;
                break;
            }
        }
        if (ok) return fi;
    }
    
    /* Second pass: any face that covers all characters (including serif). */
    for (int fi = 0; fi < fontPtr->nfaces; fi++) {
        FcCharSet *cs = fontPtr->faces[fi].charset;
        if (!cs) continue;
        int ok = 1;
        for (int i = start; i < start + len; i++) {
            if (!FcCharSetHasChar(cs, ucs4[i])) {
                ok = 0;
                break;
            }
        }
        if (ok) return fi;
    }
    
    return 0; /* Fallback to primary face. */
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
 *   True on success, false on failure.
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
                /* Find a sans-serif face first */
                int bestSerifFace = -1;
                for (fi = 0; fi < fontPtr->nfaces; fi++) {
                    if (fontPtr->faces[fi].charset &&
                        FcCharSetHasChar(fontPtr->faces[fi].charset, uc)) {
                        FcPattern *pat = fontPtr->faces[fi].source;
                        if (pat && IsSerifFace(pat)) {
                            if (bestSerifFace < 0) bestSerifFace = fi;
                            continue;
                        }
                        break;
                    }
                }
                if (fi >= fontPtr->nfaces) {
                    if (bestSerifFace >= 0) fi = bestSerifFace;
                    else fi = 0;
                }
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

        /*
         * Pass 1: determine subrun boundaries (script/face groups).
         *
         * This is always a forward walk in LOGICAL character order,
         * since deciding where one subrun ends and the next begins
         * depends on what follows each character logically (script
         * changes, face/fallback-font changes, etc). Shaping and
         * placement are deferred to pass 2 below.
         *
         * FIX: For characters with HB_SCRIPT_INHERITED or HB_SCRIPT_COMMON
         * we do NOT break on face mismatch; they are kept with the base
         * character to ensure combining marks are shaped together.
         */
        typedef struct {
            int         start;   /* Logical char offset of subrun. */
            int         len;     /* Length in chars. */
            hb_script_t script;
            int         faceIndex;
        } SubRunInfo;

        SubRunInfo subrunList[MAX_GLYPHS];
        int        subrunCount = 0;

        {
            int subrunStart = runStart;

            while (subrunStart < runStart + runLen &&
		   subrunCount < MAX_GLYPHS) {

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

                /* Extend subrun while script is consistent.
                 * For INHERITED/COMMON, do NOT break on face mismatch.
                 * Only break when we encounter a different script.
                 */
                int subrunEnd = subrunStart + 1;
                while (subrunEnd < runStart + runLen) {
                    hb_script_t s = hb_unicode_script(
						      hb_unicode_funcs_get_default(), ucs4Chars[subrunEnd]);

                    if (s == HB_SCRIPT_INHERITED || s == HB_SCRIPT_COMMON) {
                        /* Keep extending; do not break on face mismatch. */
                        subrunEnd++;
                        continue;
                    }
                    if (s != subrunScript) break;
                    /* If it's a real script, ensure face covers it? We'll handle face selection later. */
                    subrunEnd++;
                }

                /* Now find a face that covers the entire subrun. */
                int runFaceIndex = FindFaceCoveringRange(fontPtr, ucs4Chars,
                                                         subrunStart,
                                                         subrunEnd - subrunStart);

                subrunList[subrunCount].start     = subrunStart;
                subrunList[subrunCount].len       = subrunEnd - subrunStart;
                subrunList[subrunCount].script    = subrunScript;
                subrunList[subrunCount].faceIndex = runFaceIndex;
                subrunCount++;

                subrunStart = subrunEnd;
            }
        }

        /*
         * Pass 2: shape and place each subrun.
         *
         * For an LTR bidi run, subruns are placed left-to-right in the
         * same order they occur logically (index 0 .. subrunCount-1).
         *
         * For an RTL bidi run, the logically *first* subrun is read
         * LAST and therefore must end up visually rightmost; the
         * logically *last* subrun is read FIRST and must end up
         * visually leftmost. Since placement below always advances
         * the pen left to right, an RTL run's subruns must be visited
         * in REVERSE logical order so the pen reaches the logically
         * earlier (visually rightmost) subrun last.
         *
         * Processing subruns in logical order regardless of run
         * direction silently scrambles any RTL run that needs more
         * than one subrun (e.g. Arabic text combined with a name or
         * digits that require a different face) - this was the
         * source of the Arabic/Hebrew reordering bug.
         */
        for (int si = 0; si < subrunCount; si++) {
            int listIdx = runIsRTL ? (subrunCount - 1 - si) : si;

            int         subrunStart = subrunList[listIdx].start;
            int         subrunEnd   = subrunStart + subrunList[listIdx].len;
            hb_script_t subrunScript = subrunList[listIdx].script;
            int         runFaceIndex;

            /* If this subrun contains any emoji, force emoji face. */
            bool subrunHasEmoji = false;
            bool subrunHasNonEmoji = false;
            for (int ci = subrunStart; ci < subrunEnd; ci++) {
                if (IsEmoji(ucs4Chars[ci])) {
                    subrunHasEmoji = true;
                } else {
                    subrunHasNonEmoji = true;
                }
            }

            if (subrunHasEmoji && !subrunHasNonEmoji) {
                runFaceIndex = GetEmojiFaceIndex(fontPtr);
            } else if (subrunHasEmoji && subrunHasNonEmoji) {
                /* Mixed run - try to find a sans-serif face for non-emoji parts. */
                int mixedFace = FindFaceCoveringRange(fontPtr, ucs4Chars,
                                                      subrunStart,
                                                      subrunEnd - subrunStart);
                if (mixedFace >= 0) {
                    runFaceIndex = mixedFace;
                } else {
                    runFaceIndex = subrunList[listIdx].faceIndex;
                }
            } else {
                runFaceIndex = subrunList[listIdx].faceIndex;
            }

            int shapeByteStart = charBounds[subrunStart];
            int shapeByteEnd   = charBounds[
						subrunEnd < runStart + runLen ? subrunEnd : runStart + runLen];
            int shapeByteLen   = shapeByteEnd - shapeByteStart;

            if (shapeByteLen <= 0) { continue; }

            hb_font_t *runHbFont = GetHbFont(fontPtr, runFaceIndex);
            if (!runHbFont) { continue; }

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

            if (!glyphInfo || !glyphPos) { continue; }

            int runPenX = 0;
            int lastClusterByteOff  = -1;
            bool lastClusterWasEmoji = false;
            for (unsigned int gi = 0;
                 gi < glyphCount && buffer->glyphCount < MAX_GLYPHS;
                 gi++) {

                int byteOff = (int)glyphInfo[gi].cluster;

                /*
                 * Emoji glyphs are tightly advanced by the fallback face's
                 * own metrics and read as cramped when placed next to
                 * Latin text or other emoji. Add a proportional gap once
                 * per finished emoji grapheme cluster, applied here at the
                 * start of the *next* cluster so it always lands strictly
                 * between clusters and never inside one - multi-codepoint
                 * sequences (flags, ZWJ combos, skin-tone modifiers) share
                 * a single byteOff/cluster and must not be pulled apart.
                 */
                if (byteOff != lastClusterByteOff) {
                    if (lastClusterWasEmoji) {
                        runPenX += (int)(fontPtr->pixelSize * 0.18 + 0.5);
                    }
                    FcChar32 startUc;
                    lastClusterWasEmoji =
                        (FcUtf8ToUcs4((const FcChar8 *)(source + byteOff),
                                     &startUc, numBytes - byteOff) > 0) &&
                        IsEmoji(startUc);
                    lastClusterByteOff = byteOff;
                }

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
 * gNvgFontRegistry --
 *
 *   Cross-font side table recording, for every (WaylandFont, NVGcontext)
 *   pair cached in a WaylandFont's own nvgContexts list, which WaylandFont
 *   owns that cache entry. This lets TkWaylandFontContextDestroyed() purge
 *   every stale cache entry for a given NVGcontext* across ALL fonts right
 *   before that context's address can be recycled by a later
 *   nvgCreateGLES3() call (see comment there for why this matters).
 *
 *   This does not require any changes to the WaylandFont/NvgFontContext
 *   struct layout in tkWaylandInt.h - it is a purely additive, self
 *   contained registry local to this file.
 */
typedef struct NvgFontRegEntry {
    NVGcontext *vg;
    WaylandFont *owner;
    struct NvgFontRegEntry *next;
} NvgFontRegEntry;

static NvgFontRegEntry *gNvgFontRegistry = NULL;

/*
 *----------------------------------------------------------------------
 * TkWaylandFontContextDestroyed --
 *
 *   Must be called BEFORE an NVGcontext created via nvgCreateGLES3() is
 *   handed to nvgDeleteGLES3()/freed. Purges every cached NvgFontContext
 *   entry (across every WaylandFont) that refers to this context pointer.
 *
 *   Why this is necessary: EnsureNvgFont() caches loaded font IDs keyed
 *   by raw NVGcontext* pointer identity. Popup menus destroy and
 *   recreate their NVGcontext on essentially every open/close/resize
 *   (TkWaylandPopupCreateRenderer/DestroyRenderer). Because these
 *   allocations are all the same size and happen in a tight churn, a
 *   freed context's heap address is frequently reused by the very next
 *   nvgCreateGLES3() call. Without this purge, EnsureNvgFont() sees a
 *   pointer match against the stale cache entry and returns a fontId
 *   that was never loaded into the NEW (visually unrelated) context's
 *   font atlas. nvgFontFaceId() then silently selects nothing, so text
 *   stops rendering - reproducing exactly as "works the first time,
 *   blank on every subsequent open."
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Frees the matching NvgFontContext node(s) and registry entries.
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandFontContextDestroyed(
    NVGcontext *vg)
{
    if (!vg) return;

    NvgFontRegEntry **pp = &gNvgFontRegistry;
    while (*pp) {
        NvgFontRegEntry *entry = *pp;
        if (entry->vg == vg) {
            WaylandFont *fontPtr = entry->owner;
            NvgFontContext **fpp = &fontPtr->nvgContexts;
            while (*fpp) {
                if ((*fpp)->vg == vg) {
                    NvgFontContext *dead = *fpp;
                    *fpp = dead->next;
                    free(dead);
                    if (fontPtr->nvgContextCount > 0) {
                        fontPtr->nvgContextCount--;
                    }
                    break;
                }
                fpp = &(*fpp)->next;
            }
            *pp = entry->next;
            free(entry);
            continue;
        }
        pp = &entry->next;
    }
}

/*
 *----------------------------------------------------------------------
 * EnsureNvgFont --
 *
 *   Ensure all faces are loaded into the NanoVG context and wire up the
 *   fallback chain: face[0] → face[1] → … → face[n] (Fontconfig-discovered,
 *   including emoji coverage if a system emoji font is available).
 *
 *   Font IDs are per-NVG-context. We must track which
 *   fonts have been loaded for each context separately.
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
    if (!vg || !fontPtr) return -1;

    /* Check if already loaded for this specific NVG context. */
    NvgFontContext *ctx = fontPtr->nvgContexts;
    while (ctx) {
        if (ctx->vg == vg) {
            fontPtr->nvgFontId = ctx->fontId;
            return ctx->fontId;
        }
        ctx = ctx->next;
    }

    /* Load each face into this context. */
    int primaryId = -1;
    for (int i = 0; i < fontPtr->nfaces; i++) {
        int id = EnsureNvgFaceFont(fontPtr, i, vg);
        if (i == 0) primaryId = id;
    }

    /* If primary failed, try system fallbacks. */
    if (primaryId < 0) {
        const char *fallback_paths[] = {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf"
        };
        for (int i = 0; i < sizeof(fallback_paths)/sizeof(fallback_paths[0]); i++) {
            if (fallback_paths[i] && access(fallback_paths[i], R_OK) == 0) {
                char name[64];
                snprintf(name, sizeof(name), "__fallback_%p", (void*)vg);
                primaryId = nvgCreateFont(vg, name, fallback_paths[i]);
                if (primaryId >= 0) {
                    break;
                }
            }
        }
        if (primaryId < 0) {
            primaryId = nvgFindFont(vg, "sans");
            if (primaryId < 0) {
                primaryId = nvgCreateFont(vg, "sans", 
                                          "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
            }
        }
    }

    /*
     * Wire fallback chain on the primary face.
     *
     * Emoji coverage is intentionally NOT special-cased here. Emoji
     * codepoints are always forced through the HarfBuzz shaping path
     * (see IsSimpleOnly), where each glyph's font comes from
     * GetRunFaceIndex() walking fontPtr->faces[] - a chain built purely
     * from Fontconfig (see InitFont/FcFontSort). A NanoVG-level fallback
     * font registered here would only ever be reachable by NanoVG's own
     * internal nvgText() fallback, which the shaped path never calls, so
     * it would sit dead. This now matches the X11 backend
     * (tkUnixBidiFont.c), which relies solely on Fontconfig-discovered
     * faces for emoji and has no bundled emoji font at all.
     */
    if (primaryId >= 0) {
        for (int i = 1; i < fontPtr->nfaces; i++) {
            int fb = fontPtr->faces[i].nvgFontId;
            if (fb >= 0) nvgAddFallbackFontId(vg, primaryId, fb);
        }
    }

    /* Store the loaded font ID for this context. */
    if (primaryId >= 0) {
        NvgFontContext *newCtx = (NvgFontContext*)calloc(1, sizeof(NvgFontContext));
        if (newCtx) {
            newCtx->vg = vg;
            newCtx->fontId = primaryId;
            newCtx->next = fontPtr->nvgContexts;
            fontPtr->nvgContexts = newCtx;
            fontPtr->nvgContextCount++;

            /*
	     * Register in the cross-font registry so a future
             * TkWaylandFontContextDestroyed(vg) call can find and
             * purge this entry before its address gets recycled.
	     */
            NvgFontRegEntry *reg = (NvgFontRegEntry*)malloc(sizeof(NvgFontRegEntry));
            if (reg) {
                reg->vg = vg;
                reg->owner = fontPtr;
                reg->next = gNvgFontRegistry;
                gNvgFontRegistry = reg;
            }
        }
        fontPtr->nvgFontId = primaryId;
    }

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
    TkFontMetrics *fm = &fontPtr->font.fm;
    *fa = *faPtr;

    /* Resolve pixel size with improved scaling. */
    double ptSize = faPtr->size;
    int basePixels;
    if (ptSize < 0.0) {
        basePixels = (int)(-ptSize + 0.5);
    } else if (ptSize > 0.0) {
        basePixels = (int)(TkFontGetPoints(tkwin, ptSize) + 0.5);
        if (basePixels <= 0 || basePixels == (int)ptSize || basePixels < 8) {
            basePixels = (int)(ptSize * 4.0 / 3.0 + 0.5);
        }
    } else {
        basePixels = 12;
    }
    if (basePixels < 1) basePixels = 1;

    if (basePixels < 14) {
        basePixels = (int)(basePixels * 1.15 + 0.5);
    }
    fontPtr->pixelSize = basePixels;

    int bold = (faPtr->weight == TK_FW_BOLD);
    int italic = (faPtr->slant == TK_FS_ITALIC);

    const char *family = faPtr->family;

    /*
     * Strict sans-serif default: 
     * If the user did NOT explicitly request a family,
     * we do NOT add their family to the pattern.
     */
    bool useSansDefault = (!family || family[0] == '\0' ||
                           strcmp(family, "sans") == 0 ||
                           strcmp(family, "TkDefaultFont") == 0 ||
                           strcmp(family, "TkTextFont") == 0 ||
                           strcmp(family, "TkMenuFont") == 0 ||
                           strcmp(family, "TkHeadingFont") == 0 ||
                           strcmp(family, "TkCaptionFont") == 0 ||
                           strcmp(family, "TkSmallCaptionFont") == 0 ||
                           strcmp(family, "TkIconFont") == 0 ||
                           strcmp(family, "TkTooltipFont") == 0);

    FcPattern *pat = FcPatternCreate();
    if (!pat) return;

    /* 
     * Force sans-serif by adding FC_FAMILY with strong preference. 
     * Only honor explicit non-default family.
     */
    if (!useSansDefault && family && family[0] != '\0') {
        FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)family);
    } else {
        /* Default: force sans-serif. */
        FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"sans-serif");
    }

    /*
     * Font stack - order matters for fallback.
     * Put sans-serif first to ensure default is sans-serif, not serif.
     */
    /* PRIMARY: sans-serif fonts with explicit preference */
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"sans-serif");
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"sans");
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"Noto Sans");
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"DejaVu Sans");
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"Liberation Sans");
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"Arial");
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"Helvetica");
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"Verdana");
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"Tahoma");
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"Roboto");
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"Ubuntu");
    
    /* Emoji fonts - must come after sans to allow fallback */
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"Noto Emoji");
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"emojione");
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"twemoji");
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"Symbola");
    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"emoji");

    /* No FC_STYLE hints — they cause serif pollution. */

    FcPatternAddInteger(pat, FC_WEIGHT,
                        bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pat, FC_SLANT,
                        italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcPatternAddDouble(pat, FC_PIXEL_SIZE, (double)fontPtr->pixelSize);

    /* Set FC_HINTING and FC_AUTOHINT to improve rendering. */
    FcPatternAddBool(pat, FC_HINTING, FcTrue);
    FcPatternAddBool(pat, FC_AUTOHINT, FcTrue);
    
    /*Set FC_ANTIALIAS to ensure smooth text. */
    FcPatternAddBool(pat, FC_ANTIALIAS, FcTrue);

    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result;
    FcFontSet *set = FcFontSort(NULL, pat, FcTrue, NULL, &result);

    /* If the first font is serif, explicitly filter it out and find a sans-serif. */
    if (set && set->nfont > 0) {
        FcPattern *firstPat = set->fonts[0];
        FcChar8 *firstFamily = NULL;
        FcPatternGetString(firstPat, FC_FAMILY, 0, &firstFamily);
        
        /* Check if the first font is serif. */
        bool isSerif = false;
        if (firstFamily) {
            const char *fam = (const char *)firstFamily;
            if (strcasestr(fam, "serif") && !strcasestr(fam, "sans")) {
                isSerif = true;
            }
            const char *serifNames[] = {
                "times", "times new roman", "georgia", "garamond", 
                "palatino", "bookman", "cambria", "didot", "bodoni",
                "caslon", "baskerville", "minion", "goudy", NULL
            };
            for (int i = 0; serifNames[i]; i++) {
                if (strcasestr(fam, serifNames[i])) {
                    isSerif = true;
                    break;
                }
            }
        }
        
        /* If the primary font is serif, try to find a sans-serif font in the set. */
        if (isSerif && set->nfont > 1) {
            int sansIndex = -1;
            for (int i = 1; i < set->nfont; i++) {
                FcChar8 *fam = NULL;
                FcPatternGetString(set->fonts[i], FC_FAMILY, 0, &fam);
                if (fam) {
                    const char *f = (const char *)fam;
                    if (strcasestr(f, "sans") || strcasestr(f, "helvetica") ||
                        strcasestr(f, "arial") || strcasestr(f, "verdana") ||
                        strcasestr(f, "tahoma") || strcasestr(f, "dejavu") ||
                        strcasestr(f, "liberation") || strcasestr(f, "noto") ||
                        strcasestr(f, "roboto") || strcasestr(f, "ubuntu")) {
                        sansIndex = i;
                        break;
                    }
                }
            }
            /* Swap the sans-serif font to the front. */
            if (sansIndex > 0) {
                FcPattern *tmp = set->fonts[0];
                set->fonts[0] = set->fonts[sansIndex];
                set->fonts[sansIndex] = tmp;
            }
        }
    }

    /* Last-resort fallback: manually find a sans-serif font. */
    if (!set || set->nfont == 0) {
        FcPatternDestroy(pat);
        pat = FcPatternCreate();
        if (!pat) return;

        FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"sans-serif");
        FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)"sans");
        FcPatternAddDouble(pat, FC_PIXEL_SIZE, (double)fontPtr->pixelSize);
        FcPatternAddBool(pat, FC_HINTING, FcTrue);
        FcPatternAddBool(pat, FC_AUTOHINT, FcTrue);
        FcPatternAddBool(pat, FC_ANTIALIAS, FcTrue);

        FcConfigSubstitute(NULL, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);
        set = FcFontSort(NULL, pat, FcTrue, NULL, &result);
        
        /* If still no font, try specific paths. */
        if (!set || set->nfont == 0) {
            FcPatternDestroy(pat);
            /* Try to load a specific font file. */
            const char *fontPaths[] = {
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                "/usr/share/fonts/TTF/DejaVuSans.ttf",
                "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/truetype/arial/Arial.ttf",
                NULL
            };
            
            FcPattern *pat2 = FcPatternCreate();
            for (int i = 0; fontPaths[i]; i++) {
                if (access(fontPaths[i], R_OK) == 0) {
                    FcPatternAddString(pat2, FC_FILE, (FcChar8 *)fontPaths[i]);
                    FcPatternAddDouble(pat2, FC_PIXEL_SIZE, (double)fontPtr->pixelSize);
                    break;
                }
            }
            if (pat2) {
                set = FcFontSort(NULL, pat2, FcTrue, NULL, &result);
                FcPatternDestroy(pat2);
            }
        }
    }

    fontPtr->pattern = pat;
    fontPtr->fontset = set;

    /*
     * FcFontSort's trim=FcTrue keeps only fonts that add Unicode coverage
     * not already present in an earlier-ranked font. Ordinary sans/serif
     * fonts frequently already carry a handful of glyphs from the same
     * blocks emoji live in (Misc Symbols, Dingbats, etc. - see IsEmoji()),
     * which is enough for trim to judge a dedicated emoji font "redundant"
     * and drop it from the sorted set before GetEmojiFaceIndex() ever gets
     * a chance to see it. Which ordinary font wins the sort (and therefore
     * what trim keeps or discards) shifts with the requested family, which
     * is why switching the default family flips emoji between "wrong
     * glyphs" and "nothing at all". Explicitly check for an emoji-named
     * face and splice one in via FcFontMatch if the sort didn't keep one.
     */
    FcPattern *guaranteedEmojiFont = NULL;
    {
        bool haveEmojiFace = false;
        if (set) {
            for (int i = 0; i < set->nfont; i++) {
                FcChar8 *fam = NULL;
                if (FcPatternGetString(set->fonts[i], FC_FAMILY, 0, &fam)
                        == FcResultMatch && fam &&
                    strcasestr((const char *)fam, "emoji") &&
                    !strcasestr((const char *)fam, "color")) {
                    haveEmojiFace = true;
                    break;
                }
            }
        }
        if (!haveEmojiFace) {
            FcPattern *ep = FcPatternCreate();
            if (ep) {
                /*
                 * NanoVG rasterizes via stb_truetype, which only reads
                 * classic 'glyf' outlines - it cannot draw color bitmap
                 * (CBDT/CBLC, e.g. Noto Color Emoji) or COLR/CPAL (e.g.
                 * Segoe UI/Apple Color Emoji) glyphs at all, so color
                 * emoji fonts are never requested here.
                 */
                FcPatternAddString(ep, FC_FAMILY, (FcChar8 *)"Noto Emoji");
                FcPatternAddString(ep, FC_FAMILY, (FcChar8 *)"Symbola");
                FcPatternAddString(ep, FC_FAMILY, (FcChar8 *)"emoji");
                FcPatternAddDouble(ep, FC_PIXEL_SIZE,
                                   (double)fontPtr->pixelSize);
                FcConfigSubstitute(NULL, ep, FcMatchPattern);
                FcDefaultSubstitute(ep);

                FcResult eresult;
                FcPattern *matched = FcFontMatch(NULL, ep, &eresult);
                FcPatternDestroy(ep);

                if (matched) {
                    FcChar8 *mfam = NULL;
                    if (FcPatternGetString(matched, FC_FAMILY, 0, &mfam)
                            == FcResultMatch && mfam &&
                        strcasestr((const char *)mfam, "emoji") &&
                        !strcasestr((const char *)mfam, "color")) {
                        if (!set) {
                            set = FcFontSetCreate();
                            fontPtr->fontset = set;
                        }
                        if (set && FcFontSetAdd(set, matched)) {
                            guaranteedEmojiFont = matched;
                        } else {
                            FcPatternDestroy(matched);
                        }
                    } else {
                        FcPatternDestroy(matched);
                    }
                }
            }
        }
    }

    int nfaces = (set && set->nfont > 0) ? set->nfont : 0;
    if (nfaces > MAX_FACES) nfaces = MAX_FACES;

    /*
     * If the guaranteed emoji font above got pushed past the MAX_FACES
     * cutoff by everything ranked ahead of it, force it into the last
     * surviving slot instead of letting it get truncated away again.
     */
    if (set && guaranteedEmojiFont && nfaces > 0) {
        int emojiPos = -1;
        for (int i = 0; i < set->nfont; i++) {
            if (set->fonts[i] == guaranteedEmojiFont) { emojiPos = i; break; }
        }
        if (emojiPos >= nfaces) {
            set->fonts[nfaces - 1] = guaranteedEmojiFont;
        }
    }

    fontPtr->faces = (WaylandFtFace *)Tcl_Alloc(
        (nfaces > 0 ? nfaces : 1) * sizeof(WaylandFtFace));
    fontPtr->nfaces = nfaces;
    memset(fontPtr->faces, 0,
           (nfaces > 0 ? nfaces : 1) * sizeof(WaylandFtFace));

    for (int i = 0; i < nfaces; i++) {
        WaylandFtFace *face = &fontPtr->faces[i];
        face->source = set->fonts[i];
        face->nvgFontId = -1;
        face->isLoaded = 0;
        face->nvgName[0] = '\0';

        FcCharSet *cs = NULL;
        if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &cs)
            == FcResultMatch)
            face->charset = FcCharSetCopy(cs);

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
                    fm->ascent = (int)(asc * scale + 0.5f);
                    fm->descent = (int)(-desc * scale + 0.5f);

                    int adv_W, adv_dot, lsb;
                    stbtt_GetCodepointHMetrics(&info, 'W', &adv_W, &lsb);
                    stbtt_GetCodepointHMetrics(&info, '.', &adv_dot, &lsb);
                    fm->maxWidth = (int)(adv_W * scale + 0.5f);
                    fm->fixed = (adv_W == adv_dot);
                    fa->size = (double)(-fontPtr->pixelSize);
                }
            }
            if (buf) Tcl_Free(buf);
            fclose(fd);
        }
    }

    if (fm->ascent == 0 && fm->descent == 0) {
        fm->ascent = (int)(fontPtr->pixelSize * 0.72 + 0.5);
        fm->descent = (int)(fontPtr->pixelSize * 0.28 + 0.5);
        fm->maxWidth = fontPtr->pixelSize;
        fm->fixed = 0;
    }

    fontPtr->underlinePos = fm->descent / 2;
    if (fontPtr->underlinePos < 1) fontPtr->underlinePos = 1;
    fontPtr->barHeight = (int)(fontPtr->pixelSize * 0.07 + 0.5);
    if (fontPtr->barHeight < 1) fontPtr->barHeight = 1;

    fontPtr->nvgFontId = -1;
    fontPtr->font.fid = (Font)(uintptr_t)fontPtr;
    
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

    /* Remove registry entries for this font. */
    NvgFontRegEntry **pp = &gNvgFontRegistry;
    while (*pp) {
        if ((*pp)->owner == fontPtr) {
            NvgFontRegEntry *dead = *pp;
            *pp = dead->next;
            free(dead);
            continue;
        }
        pp = &(*pp)->next;
    }

    /* Free per-context font IDs. */
    NvgFontContext *ctx = fontPtr->nvgContexts;
    while (ctx) {
        NvgFontContext *dead = ctx;
        ctx = ctx->next;
        free(dead);
    }
    fontPtr->nvgContexts = NULL;
    fontPtr->nvgContextCount = 0;

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
        return TkWaylandPixelToNVG(vals.foreground);
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

    /* 
     * Check if the range contains combining characters.
     * If so, we need to use the complex HarfBuzz path.
     */
    bool hasCombining = false;
    bool hasEmoji = false;
    for (int i = start; i < end; ) {
        FcChar32 uc;
        int clen = FcUtf8ToUcs4((const FcChar8 *)(source + i), &uc, end - i);
        if (clen <= 0) { i++; continue; }
        if (uc >= 0x0300 && uc <= 0x036F) {
            hasCombining = true;
            break;
        }
        if (IsEmoji(uc)) {
            hasEmoji = true;
        }
        i += clen;
    }

    /* 
     * Simple LTR path: only for strings WITHOUT combining characters or emoji.
     * Combining characters must go through HarfBuzz for proper positioning.
     */
    if (IsSimpleOnly(source + rangeStart, (int)rangeLength) && !hasCombining && !hasEmoji) {
        NVGcontext *vg = TkWaylandGetNVGContextForMeasure();
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

    /* 
     * Complex / RTL / Combining path: ShapedGlyphBuffer cluster table.
     * This path uses HarfBuzz which correctly positions combining marks.
     */
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
 *   The NanoVG fallback chain (primary → fallback faces) is wired by
 *   EnsureNvgFont and handles any codepoint the primary face lacks in
 *   the simple LTR path. (Emoji never take this path - see IsSimpleOnly -
 *   so their coverage instead comes from GetRunFaceIndex() over the same
 *   Fontconfig-discovered face list in the complex/RTL path below.)
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

    NVGcontext *vg = TkWaylandGetNVGContext(drawable);
    if (!vg) return;

    int primaryId = EnsureNvgFont(fontPtr, vg);
    if (primaryId < 0) return;

    nvgFontSize(vg, (float)fontPtr->pixelSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgFillColor(vg, ColorFromGC(gc));

    const char *rangePtr = source + rangeStart;
    const char *rangeEnd = rangePtr + rangeLength;

    /* Starting X for partial range — more robust. */
    double drawX = x;
    if (rangeStart > 0) {
        float bounds[4];
        nvgFontFaceId(vg, primaryId);
        nvgTextBounds(vg, 0, 0, source, source + rangeStart, bounds);
        drawX += (double)bounds[2];
    }

    nvgSave(vg);
    nvgTranslate(vg, (float)drawX, (float)y);
    if (angle != 0.0) {
        nvgRotate(vg, (float)(-angle * NVG_PI / 180.0));
    }

    /*
     * Check if the range being drawn contains combining characters.
     * If so, compose them to NFC form for proper rendering.
     */
    bool hasCombining = false;
    for (int i = (int)rangeStart; i < (int)(rangeStart + rangeLength); ) {
        FcChar32 uc;
        int clen = FcUtf8ToUcs4((const FcChar8 *)(source + i), &uc,
                                (int)(rangeStart + rangeLength) - i);
        if (clen <= 0) { i++; continue; }
        if (uc >= 0x0300 && uc <= 0x036F) {
            hasCombining = true;
            break;
        }
        i += clen;
    }

    char *composedSource = NULL;
    const char *renderPtr = rangePtr;
    const char *renderEnd = rangeEnd;
    bool didCompose = false;

    if (hasCombining) {
        composedSource = ComposeUTF8String(source + rangeStart, (int)rangeLength);
        if (composedSource) {
            renderPtr = composedSource;
            renderEnd = composedSource + strlen(composedSource);
            didCompose = true;
        }
    }

    bool hasEmoji = false;
    for (int i = (int)rangeStart; i < (int)(rangeStart + rangeLength); ) {
        FcChar32 uc;
        int clen = FcUtf8ToUcs4((const FcChar8 *)(source + i), &uc,
                                (int)(rangeStart + rangeLength) - i);
        if (clen <= 0) { i++; continue; }
        if (IsEmoji(uc)) {
            hasEmoji = true;
            break;
        }
        i += clen;
    }

    /* For text with combining characters, use the simple path with composed text. */
    if (IsSimpleOnly(renderPtr, renderEnd - renderPtr) && !hasEmoji) {
        nvgFontFaceId(vg, primaryId);
        nvgText(vg, 0.0f, 0.0f, renderPtr, renderEnd);
        nvgRestore(vg);
        if (composedSource) free(composedSource);
        goto decorations;
    }

    /* 
     * Complex path with per-cluster rendering.
     * For clusters that couldn't be composed, use HarfBuzz positioning
     * to place combining marks correctly.
     */
    {
        ShapedGlyphBuffer sbuf;
        const char *shapeSource = composedSource ? composedSource : source;
        int shapeLen = composedSource ? (int)strlen(composedSource) : (int)numBytes;
        
        if (!WaylandShaper_ShapeString(&fontPtr->shaper, fontPtr,
                                       shapeSource, shapeLen, &sbuf)) {
            nvgRestore(vg);
            if (composedSource) free(composedSource);
            return;
        }

        int lastFaceId = -1;

        typedef struct {
            int  start_byte;
            int  end_byte;
            int  face_idx;
            int  pen_x;
            int  pen_y;
            int  advance_x;
            char text[32];
        } ClusterRenderInfo;

        ClusterRenderInfo clusters[MAX_GLYPHS];
        int cluster_count = 0;

        for (int i = 0; i < sbuf.glyphCount && cluster_count < MAX_GLYPHS; i++) {
            int bo  = sbuf.glyphs[i].byteOffset;
            int boe = bo + sbuf.glyphs[i].clusterLen;

            if (boe <= (int)rangeStart || bo >= (int)(rangeStart + rangeLength))
                continue;

            int found = -1;
            for (int j = 0; j < cluster_count; j++) {
                if (clusters[j].start_byte == bo && clusters[j].end_byte == boe) {
                    found = j;
                    break;
                }
            }

            if (found < 0) {
                found = cluster_count++;
                clusters[found].start_byte = bo;
                clusters[found].end_byte = boe;
                clusters[found].face_idx = sbuf.glyphs[i].faceIndex;
                clusters[found].pen_x = sbuf.glyphs[i].x;
                clusters[found].pen_y = sbuf.glyphs[i].y;
                clusters[found].advance_x = sbuf.glyphs[i].advanceX;

                int len = boe - bo;
                if (len > 31) len = 31;
                memcpy(clusters[found].text, shapeSource + bo, len);
                clusters[found].text[len] = '\0';
            }
        }

        for (int i = 0; i < cluster_count; i++) {
            int faceIdx = clusters[i].face_idx;
            if (faceIdx < 0 || faceIdx >= fontPtr->nfaces) faceIdx = 0;

            /* For emoji, use emoji face.*/
            FcChar32 uc;
            if (FcUtf8ToUcs4((const FcChar8 *)clusters[i].text, &uc,
                             strlen(clusters[i].text)) > 0) {
                if (IsEmoji(uc)) {
                    int emojiFace = GetEmojiFaceIndex(fontPtr);
                    if (emojiFace >= 0 && emojiFace < fontPtr->nfaces) {
                        faceIdx = emojiFace;
                    }
                } else {
                    /* Find a sans-serif face. */
                    int sansFace = -1;
                    for (int fi = 0; fi < fontPtr->nfaces; fi++) {
                        if (fontPtr->faces[fi].charset &&
                            FcCharSetHasChar(fontPtr->faces[fi].charset, uc)) {
                            FcPattern *pat = fontPtr->faces[fi].source;
                            if (pat && !IsSerifFace(pat)) {
                                sansFace = fi;
                                break;
                            }
                        }
                    }
                    if (sansFace >= 0) {
                        faceIdx = sansFace;
                    }
                }
            }

            int faceId = fontPtr->faces[faceIdx].nvgFontId;
            if (faceId < 0) faceId = primaryId;

            if (faceId != lastFaceId) {
                nvgFontFaceId(vg, faceId);
                lastFaceId = faceId;
            }

            float gx = (float)clusters[i].pen_x;
            float gy = (float)clusters[i].pen_y;

            /* 
             * If the cluster contains a combining mark and couldn't be composed,
             * use HarfBuzz's x_offset to position the mark correctly.
             * The offset is already in the glyph's x position.
             */
            bool hasMark = false;
            for (int j = 0; j < (int)strlen(clusters[i].text); ) {
                FcChar32 uc2;
                int clen = FcUtf8ToUcs4((const FcChar8 *)(clusters[i].text + j), 
                                        &uc2, strlen(clusters[i].text) - j);
                if (clen > 0 && uc2 >= 0x0300 && uc2 <= 0x036F) {
                    hasMark = true;
                    break;
                }
                j += clen;
            }

            if (hasMark && didCompose) {
                /* 
                 * The mark is in a cluster that WAS composed by ComposeUTF8String,
                 * but the font doesn't have the precomposed glyph. HarfBuzz 
                 * will have positioned it with a negative x_offset. The pen_x
                 * already includes that offset from the shaping pass.
                 */
                nvgText(vg, gx, gy, clusters[i].text, 
                        clusters[i].text + strlen(clusters[i].text));
            } else {
                nvgText(vg, gx, gy, clusters[i].text, 
                        clusters[i].text + strlen(clusters[i].text));
            }
        }
    }

    nvgRestore(vg);
    if (composedSource) free(composedSource);

decorations:
    if (fontPtr->font.fa.underline || fontPtr->font.fa.overstrike) {
        float bounds[4];
        float runWidth;

        nvgFontFaceId(vg, primaryId);
        nvgFontSize(vg, (float)fontPtr->pixelSize);
        nvgTextBounds(vg, 0, 0, renderPtr, renderEnd, bounds);
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

/* Stub function for compatibility. */
void
TkUnixSetXftClipRegion(
    TCL_UNUSED(Region)) /* clipRegion */
{
 /* no-op */
}

/*
 *----------------------------------------------------------------------
 * TkWaylandLoadNamedFontIntoContext --
 *
 *   Loads a Tk named font (or creates one on demand) into a specific
 *   NVG context. This is the unified entry point for all drawing code.
 *
 * Results:
 *   NanoVG font ID (>=0) on success.
 * 
 * Side effects:
 *   None.
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandLoadNamedFontIntoContext(
    NVGcontext *vg,
    const char *tkFontName)   /* e.g. "TkMenuFont", "TkDefaultFont", "sans" */
{
    if (!vg || !tkFontName) return -1;

    TkFontAttributes fa;
    TkInitFontAttributes(&fa);
    fa.family = Tk_GetUid(tkFontName);
    fa.size   = -12.0;   /* reasonable default */

    TkFont *tkfont = TkpGetFontFromAttributes(NULL, NULL, &fa);
    if (!tkfont) return -1;

    WaylandFont *fontPtr = (WaylandFont *)tkfont;
    int id = EnsureNvgFont(fontPtr, vg);

    /* Don't delete the font — it's cached by Tk. */
    return id;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
