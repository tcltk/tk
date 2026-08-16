/*
 * tkTextGrapheme.c --
 *
 *      Implements line breaks (UAX #14) and word/sentence breaks (UAX #29)
 *      for Unicode extended grapheme clusters utilizing the mojibake Unicode
 *      library.  Also, for scripts without spaces (Thai, Lao, Khmer),
 *      implements a forward maximum-matching word segmenter using
 *      ICU-derived dictionaries.
 *	
 * Copyright (c) 2026 Kevin Walzer.
 *
 * See the file "license.terms" for information on usage and redistribution.
 */

#include "tkInt.h"
#include "tkText.h"
#include <mojibake.h>
#include <string.h>
#include <stdlib.h>
#include "dictionaries.h"

/*
 * ---------------------------------------------------------------
 * Utf8EncodedBytes --
 *
 *   Determine the number of bytes required to encode a Unicode
 *   codepoint in UTF-8.
 *
 * Results:
 *   Returns 1, 2, 3, or 4 bytes depending on the codepoint value.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
 
static size_t
Utf8EncodedBytes(mjb_codepoint cp)
{
    if (cp < 0x80) return 1;
    if (cp < 0x800) return 2;
    if (cp < 0x10000) return 3;
    return 4;
}

/*
 * ---------------------------------------------------------------
 * BoundaryFromState --
 *
 *   Compute the actual byte boundary position from a mojibake
 *   next-state structure. All mojibake next_* states have index =
 *   byte offset of lookahead codepoint, not the boundary itself.
 *   True boundary = index - encoded_len(current).
 *
 * Results:
 *   Returns the boundary byte offset.
 *
 * Side effects:
 *   Updates lastBoundary to the computed boundary value.
 * ---------------------------------------------------------------
 */
 
static size_t
BoundaryFromState(size_t index,
		  mjb_codepoint curCp,
		  size_t byteLen,
		  size_t *lastBoundary)
{
    size_t idx = (index > byteLen) ? byteLen : index;
    size_t enc = Utf8EncodedBytes(curCp);
    size_t boundary = (enc > idx) ? 0 : (idx - enc);
    if (boundary < *lastBoundary) boundary = *lastBoundary;
    *lastBoundary = boundary;
    return boundary;
}


/*
 * ---------------------------------------------------------------
 * GraphemeBoundaryFromState --
 *
 *   Wrapper around BoundaryFromState for grapheme break states.
 *
 * Results:
 *   Returns the grapheme cluster boundary byte offset.
 *
 * Side effects:
 *   Updates lastBoundary.
 * ---------------------------------------------------------------
 */

static size_t
GraphemeBoundaryFromState(const mjb_next_state *state,
			  size_t byteLen,
			  size_t *lastBoundary)
{
    return BoundaryFromState(state->index, state->current_codepoint, byteLen, lastBoundary);
}

/*
 * ---------------------------------------------------------------
 * mojibake_grapheme_breaks --
 *
 *   Compute all grapheme cluster break boundaries in a UTF-8 buffer.
 *   Implements Unicode UAX #29 extended grapheme cluster boundaries.
 *
 * Results:
 *   Returns 1 on success. The breaks array is filled with 1 at each
 *   grapheme boundary byte offset (including 0 and byteLen).
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_grapheme_breaks(const char *buf,
			 size_t byteLen,
			 unsigned char *breaks)
{
    mjb_next_state state;
    mjb_break_type bt;
    size_t lastBoundary = 0;
    if (buf == NULL || breaks == NULL) return 0;
    memset(breaks, 0, byteLen + 1);
    if (byteLen == 0) { breaks[0]=1; return 1; }
    state.index = 0;
    breaks[0]=1;
    while ((bt = mjb_next_grapheme_break(buf, byteLen, MJB_ENC_UTF_8, &state)) != MJB_BT_NOT_SET) {
        if (bt == MJB_BT_ALLOWED) {
            size_t pos = GraphemeBoundaryFromState(&state, byteLen, &lastBoundary);
            breaks[pos]=1;
        }
    }
    breaks[byteLen]=1;
    return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_grapheme_next --
 *
 *   Find the next grapheme cluster boundary after a given byte offset.
 *
 * Results:
 *   Returns 1 on success. nextOffset is set to the byte offset of
 *   the next grapheme boundary, or byteLen if none exists.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
int
mojibake_grapheme_next(const char *buf,
		       size_t byteLen,
		       size_t byteOffset,
		       size_t *nextOffset)
{
    mjb_next_state state;
    mjb_break_type bt;
    size_t lastBoundary=0;
    if (nextOffset==NULL) return 0;
    if (buf==NULL || byteOffset>=byteLen) { *nextOffset=byteLen; return buf!=NULL; }
    state.index=0;
    while ((bt=mjb_next_grapheme_break(buf,byteLen,MJB_ENC_UTF_8,&state))!=MJB_BT_NOT_SET) {
        if (bt==MJB_BT_ALLOWED) {
            size_t pos=GraphemeBoundaryFromState(&state,byteLen,&lastBoundary);
            if (pos>byteOffset) { *nextOffset=pos; return 1; }
        }
    }
    *nextOffset=byteLen; return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_grapheme_prev --
 *
 *   Find the previous grapheme cluster boundary before a given byte offset.
 *
 * Results:
 *   Returns 1 on success. prevOffset is set to the byte offset of
 *   the previous grapheme boundary, or 0 if none exists.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
int
mojibake_grapheme_prev(const char *buf,
		       size_t byteLen,
		       size_t byteOffset,
		       size_t *prevOffset)
{
    mjb_next_state state;
    mjb_break_type bt;
    size_t lastBoundary=0, last=0;
    if (prevOffset==NULL) return 0;
    if (buf==NULL || byteOffset==0) { *prevOffset=0; return buf!=NULL; }
    state.index=0;
    while ((bt=mjb_next_grapheme_break(buf,byteLen,MJB_ENC_UTF_8,&state))!=MJB_BT_NOT_SET) {
        if (bt==MJB_BT_ALLOWED) {
            size_t pos=GraphemeBoundaryFromState(&state,byteLen,&lastBoundary);
            if (pos>=byteOffset) break;
            last=pos;
        }
    }
    *prevOffset=last; return 1;
}

/*
 * ---------------------------------------------------------------
 * LineBoundaryFromState --
 *
 *   Wrapper around BoundaryFromState for line break states.
 *
 * Results:
 *   Returns the line break boundary byte offset.
 *
 * Side effects:
 *   Updates lastBoundary.
 * ---------------------------------------------------------------
 */

static size_t
LineBoundaryFromState(const mjb_next_line_state *state,
		      size_t byteLen,
		      size_t *lastBoundary)
{
    return BoundaryFromState(state->index, state->current_codepoint, byteLen, lastBoundary);
}

/*
 * ---------------------------------------------------------------
 * mojibake_line_breaks --
 *
 *   Compute all line break boundaries in a UTF-8 buffer.
 *   Implements Unicode UAX #14 line breaking rules.
 *
 * Results:
 *   Returns 1 on success. The breaks array is filled with 1 at each
 *   line break boundary byte offset (including 0 and byteLen).
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_line_breaks(const char *buf,
		     size_t byteLen,
		     unsigned char *breaks)
{
    mjb_next_line_state state;
    mjb_break_type bt;
    size_t lastBoundary=0;
    if (!buf || !breaks) return 0;
    memset(breaks,0,byteLen+1);
    if (byteLen==0) { breaks[0]=1; return 1; }
    breaks[0]=1;
    state.index=0;
    while ((bt=mjb_next_line_break(buf,byteLen,MJB_ENC_UTF_8,&state))!=MJB_BT_NOT_SET) {
        if (bt==MJB_BT_ALLOWED) {
            size_t pos=LineBoundaryFromState(&state,byteLen,&lastBoundary);
            if (pos<=byteLen) breaks[pos]=1;
        }
    }
    breaks[byteLen]=1;
    return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_line_next --
 *
 *   Find the next line break boundary after a given byte offset.
 *
 * Results:
 *   Returns 1 on success. nextOffset is set to the byte offset of
 *   the next line break boundary, or byteLen if none exists.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_line_next(const char *buf,
		   size_t byteLen,
		   size_t byteOffset,
		   size_t *nextOffset)
{
    mjb_next_line_state state;
    mjb_break_type bt;
    size_t lastBoundary=0;
    if (!nextOffset) return 0;
    if (!buf || byteOffset>=byteLen) { *nextOffset=byteLen; return buf!=NULL; }
    state.index=0;
    while ((bt=mjb_next_line_break(buf,byteLen,MJB_ENC_UTF_8,&state))!=MJB_BT_NOT_SET) {
        if (bt==MJB_BT_ALLOWED) {
            size_t pos=LineBoundaryFromState(&state,byteLen,&lastBoundary);
            if (pos>byteOffset) { *nextOffset=pos; return 1; }
        }
    }
    *nextOffset=byteLen; return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_line_prev --
 *
 *   Find the previous line break boundary before a given byte offset.
 *
 * Results:
 *   Returns 1 on success. prevOffset is set to the byte offset of
 *   the previous line break boundary, or 0 if none exists.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

int
mojibake_line_prev(const char *buf,
		   size_t byteLen,
		   size_t byteOffset,
		   size_t *prevOffset)
{
    mjb_next_line_state state;
    mjb_break_type bt;
    size_t lastBoundary=0, last=0;
    if (!prevOffset) return 0;
    if (!buf || byteOffset==0) { *prevOffset=0; return buf!=NULL; }
    state.index=0;
    while ((bt=mjb_next_line_break(buf,byteLen,MJB_ENC_UTF_8,&state))!=MJB_BT_NOT_SET) {
        if (bt==MJB_BT_ALLOWED) {
            size_t pos=LineBoundaryFromState(&state,byteLen,&lastBoundary);
            if (pos>=byteOffset) break;
            last=pos;
        }
    }
    *prevOffset=last; return 1;
}

/*
 * ---------------------------------------------------------------
 * WordBoundaryFromState --
 *
 *   Wrapper around BoundaryFromState for word break states.
 *
 * Results:
 *   Returns the word break boundary byte offset.
 *
 * Side effects:
 *   Updates lastBoundary.
 * ---------------------------------------------------------------
 */

static size_t
WordBoundaryFromState(const mjb_next_word_state *state,
		      size_t byteLen,
		      size_t *lastBoundary)
{
    return BoundaryFromState(state->index, state->current_codepoint, byteLen, lastBoundary);
}

/*
 * ---------------------------------------------------------------
 * mojibake_word_breaks --
 *
 *   Compute all word break boundaries in a UTF-8 buffer.
 *   Implements Unicode UAX #29 word breaking rules.
 *
 * Results:
 *   Returns 1 on success. The breaks array is filled with 1 at each
 *   word break boundary byte offset (including 0 and byteLen).
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
int
mojibake_word_breaks(const char *buf,
		     size_t byteLen,
		     unsigned char *breaks)
{
    mjb_next_word_state state;
    mjb_break_type bt;
    size_t lastBoundary=0;
    if (!buf || !breaks) return 0;
    memset(breaks,0,byteLen+1);
    if (byteLen==0) { breaks[0]=1; return 1; }
    breaks[0]=1;
    state.index=0;
    while ((bt=mjb_next_word_break(buf,byteLen,MJB_ENC_UTF_8,&state))!=MJB_BT_NOT_SET) {
        if (bt==MJB_BT_ALLOWED) {
            size_t pos=WordBoundaryFromState(&state,byteLen,&lastBoundary);
            if (pos<=byteLen) breaks[pos]=1;
        }
    }
    breaks[byteLen]=1;
    return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_word_next --
 *
 *   Find the next word break boundary after a given byte offset.
 *
 * Results:
 *   Returns 1 on success. nextOffset is set to the byte offset of
 *   the next word break boundary, or byteLen if none exists.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
int
mojibake_word_next(const char *buf,
		   size_t byteLen,
		   size_t byteOffset,
		   size_t *nextOffset)
{
    mjb_next_word_state state;
    mjb_break_type bt;
    size_t lastBoundary=0;
    if (!nextOffset) return 0;
    if (!buf || byteOffset>=byteLen) { *nextOffset=byteLen; return buf!=NULL; }
    state.index=0;
    while ((bt=mjb_next_word_break(buf,byteLen,MJB_ENC_UTF_8,&state))!=MJB_BT_NOT_SET) {
        if (bt==MJB_BT_ALLOWED) {
            size_t pos=WordBoundaryFromState(&state,byteLen,&lastBoundary);
            if (pos>byteOffset) { *nextOffset=pos; return 1; }
        }
    }
    *nextOffset=byteLen; return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_word_prev --
 *
 *   Find the previous word break boundary before a given byte offset.
 *
 * Results:
 *   Returns 1 on success. prevOffset is set to the byte offset of
 *   the previous word break boundary, or 0 if none exists.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
int
mojibake_word_prev(const char *buf,
		   size_t byteLen,
		   size_t byteOffset,
		   size_t *prevOffset)
{
    mjb_next_word_state state;
    mjb_break_type bt;
    size_t lastBoundary=0, last=0;
    if (!prevOffset) return 0;
    if (!buf || byteOffset==0) { *prevOffset=0; return buf!=NULL; }
    state.index=0;
    while ((bt=mjb_next_word_break(buf,byteLen,MJB_ENC_UTF_8,&state))!=MJB_BT_NOT_SET) {
        if (bt==MJB_BT_ALLOWED) {
            size_t pos=WordBoundaryFromState(&state,byteLen,&lastBoundary);
            if (pos>=byteOffset) break;
            last=pos;
        }
    }
    *prevOffset=last; return 1;
}

/*
 * ---------------------------------------------------------------
 * SentenceBoundaryFromState --
 *
 *   Wrapper around BoundaryFromState for sentence break states.
 *
 * Results:
 *   Returns the sentence break boundary byte offset.
 *
 * Side effects:
 *   Updates lastBoundary.
 * ---------------------------------------------------------------
 */
static size_t
SentenceBoundaryFromState(const mjb_next_sentence_state *state,
			  size_t byteLen,
			  size_t *lastBoundary)
{
    return BoundaryFromState(state->index, state->current_codepoint, byteLen, lastBoundary);
}

/*
 * ---------------------------------------------------------------
 * mojibake_sentence_breaks --
 *
 *   Compute all sentence break boundaries in a UTF-8 buffer.
 *   Implements Unicode UAX #29 sentence breaking rules.
 *
 * Results:
 *   Returns 1 on success. The breaks array is filled with 1 at each
 *   sentence break boundary byte offset (including 0 and byteLen).
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
int
mojibake_sentence_breaks(const char *buf,
			 size_t byteLen,
			 unsigned char *breaks)
{
    mjb_next_sentence_state state;
    mjb_break_type bt;
    size_t lastBoundary=0;
    if (!buf || !breaks) return 0;
    memset(breaks,0,byteLen+1);
    if (byteLen==0) { breaks[0]=1; return 1; }
    breaks[0]=1;
    state.index=0;
    while ((bt=mjb_next_sentence_break(buf,byteLen,MJB_ENC_UTF_8,&state))!=MJB_BT_NOT_SET) {
        if (bt==MJB_BT_ALLOWED) {
            size_t pos=SentenceBoundaryFromState(&state,byteLen,&lastBoundary);
            if (pos<=byteLen) breaks[pos]=1;
        }
    }
    breaks[byteLen]=1;
    return 1;
}

/*
 * ---------------------------------------------------------------
 * ComplexScript --
 *
 *   Enumeration of complex scripts that require dictionary-based
 *   word segmentation (scripts without spaces between words).
 * ---------------------------------------------------------------
 */

typedef enum {
    SCRIPT_OTHER = 0,
    SCRIPT_THAI,
    SCRIPT_LAO,
    SCRIPT_KHMER,
    SCRIPT_MYANMAR
} ComplexScript;

/*
 * ---------------------------------------------------------------
 * ScriptFromCodepoint --
 *
 *   Determine the complex script category of a Unicode codepoint.
 *
 * Results:
 *   Returns SCRIPT_THAI, SCRIPT_LAO, SCRIPT_KHMER, SCRIPT_MYANMAR,
 *   or SCRIPT_OTHER if the codepoint is not in a dictionary-supported
 *   script.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
static ComplexScript
ScriptFromCodepoint(mjb_codepoint cp)
{
    if (cp >= 0x0E00 && cp <= 0x0E7F) return SCRIPT_THAI;
    if (cp >= 0x0E80 && cp <= 0x0EFF) return SCRIPT_LAO;
    if ((cp >= 0x1780 && cp <= 0x17FF) || (cp >= 0x19E0 && cp <= 0x19FF)) return SCRIPT_KHMER;
    if ((cp >= 0x1000 && cp <= 0x109F) || (cp >= 0xAA60 && cp <= 0xAA7F) || (cp >= 0xA9E0 && cp <= 0xA9FF)) return SCRIPT_MYANMAR;
    return SCRIPT_OTHER;
}

/*
 * ---------------------------------------------------------------
 * DictCache --
 *
 *   Cached sorted dictionary structure for fast binary search
 *   lookups during maximum-matching word segmentation.
 * ---------------------------------------------------------------
 */

typedef struct {
    const char* const* orig_words;
    size_t count;
    const char** sorted;      /* malloced array of pointers sorted */
    size_t* lens;             /* byte lengths */
    size_t max_len;           /* max byte length */
    int initialized;
} DictCache;

static DictCache cache_thai = { NULL,0,NULL,NULL,0,0 };
static DictCache cache_lao  = { NULL,0,NULL,NULL,0,0 };
static DictCache cache_khmer= { NULL,0,NULL,NULL,0,0 };
static DictCache cache_myanmar={NULL,0,NULL,NULL,0,0};

/*
 * ---------------------------------------------------------------
 * cmp_strptr --
 *
 *   Comparison function for qsort on string pointers.
 *
 * Results:
 *   Returns strcmp result of the two strings.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

static int cmp_strptr(const void *a, const void *b)
{
    const char * const *sa = (const char* const*)a;
    const char * const *sb = (const char* const*)b;
    return strcmp(*sa,*sb);
}

/*
 * ---------------------------------------------------------------
 * InitDictCache --
 *
 *   Initialize a dictionary cache by sorting the word list and
 *   computing lengths. Does nothing if already initialized.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Allocates memory for sorted pointers and length arrays.
 * ---------------------------------------------------------------
 */

static void
InitDictCache(DictCache *c, const char* const* words, size_t count)
{
    if (c->initialized) return;
    c->orig_words = words;
    c->count = count;
    c->sorted = (const char**)malloc(count * sizeof(char*));
    c->lens = (size_t*)malloc(count * sizeof(size_t));
    if (!c->sorted || !c->lens) return;
    size_t max=0;
    for (size_t i=0;i<count;i++) {
        c->sorted[i]=words[i];
        size_t l = strlen(words[i]);
        c->lens[i]=l; /* Temporary, will recompute after sort. */
        if (l>max) max=l;
    }
    qsort(c->sorted, count, sizeof(char*), cmp_strptr);
    /* Recompute lens after sort and max. */
    max=0;
    for (size_t i=0;i<count;i++) {
        size_t l = strlen(c->sorted[i]);
        c->lens[i]=l;
        if (l>max) max=l;
    }
    c->max_len = max;
    c->initialized=1;
}

/*
 * ---------------------------------------------------------------
 * EnsureDictsInitialized --
 *
 *   Initialize all dictionary caches. Called once at first use.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Initializes the Thai, Lao, Khmer, and Myanmar dictionary caches.
 * ---------------------------------------------------------------
 */

static void
EnsureDictsInitialized(void)
{
    static int once=0;
    if (once) return;
    once=1;
    InitDictCache(&cache_thai, thai_words, thai_word_count);
    InitDictCache(&cache_lao, lao_words, lao_word_count);
    InitDictCache(&cache_khmer, khmer_words, khmer_word_count);
}

/*
 * ---------------------------------------------------------------
 * CacheForScript --
 *
 *   Get the dictionary cache for a given complex script type.
 *
 * Results:
 *   Returns a pointer to the DictCache for the script, or NULL
 *   if the script has no dictionary.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

static DictCache*
CacheForScript(ComplexScript s)
{
    switch(s){
    case SCRIPT_THAI: return &cache_thai;
    case SCRIPT_LAO: return &cache_lao;
    case SCRIPT_KHMER: return &cache_khmer;
    case SCRIPT_MYANMAR: return &cache_myanmar;
    default: return NULL;
    }
}

/*
 * ---------------------------------------------------------------
 * DictContains --
 *
 *   Binary search for an exact match of a query string in a
 *   sorted dictionary.
 *
 * Results:
 *   Returns 1 if the query is found in the dictionary, 0 otherwise.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */

static int
DictContains(DictCache *c, const char *query, size_t qlen)
{
    if (!c || !c->initialized || qlen==0 || qlen>c->max_len) return 0;
    size_t lo=0, hi=c->count;
    while (lo < hi) {
        size_t mid = lo + (hi-lo)/2;
        size_t mlen = c->lens[mid];
        size_t cmpLen = mlen < qlen ? mlen : qlen;
        int cmp = strncmp(c->sorted[mid], query, cmpLen);
        if (cmp==0) {
            if (mlen < qlen) cmp = -1;
            else if (mlen > qlen) cmp = 1;
            else return 1;
        }
        if (cmp < 0) lo = mid+1;
        else hi = mid;
    }
    return 0;
}

/*
 * ---------------------------------------------------------------
 * ForwardMaxMatchRun --
 *
 *   Forward maximum matching word segmentation inside a single
 *   script run. Finds the longest dictionary word at each position,
 *   falling back to single grapheme clusters for unknown sequences.
 *
 * Results:
 *   Returns the number of words found. Updates wBreaks and optionally
 *   lBreaks with word boundaries.
 *
 * Side effects:
 *   Modifies the break arrays.
 * ---------------------------------------------------------------
 */

static size_t
ForwardMaxMatchRun(const char *buf, size_t runStart, size_t runEnd,
                   const unsigned char *gBreaks,
                   unsigned char *wBreaks,
                   unsigned char *lBreaks,
                   DictCache *dc)
{
    size_t pos = runStart;
    size_t words = 0;

    while (pos < runEnd) {

        /* Find next grapheme boundary after pos. */
        size_t nextG = pos + 1;
        while (nextG < runEnd && !gBreaks[nextG]) nextG++;
        if (nextG > runEnd) nextG = runEnd;

        /* Ensure forward progress. */
        if (nextG == pos) {
            nextG = pos + 1;
            while (nextG < runEnd && (buf[nextG] & 0xC0) == 0x80) nextG++;
        }

        size_t bestEnd = 0;

        /* Try longest dictionary match first. */
        size_t maxTry = dc->max_len;
        size_t remaining = runEnd - pos;
        if (maxTry > remaining) maxTry = remaining;

        size_t tryEnd = pos + maxTry;

        /* Snap tryEnd to a grapheme boundary. */
        while (tryEnd > pos && tryEnd < runEnd && !gBreaks[tryEnd]) tryEnd--;

        for (; tryEnd > pos; ) {
            size_t tryLen = tryEnd - pos;
            if (tryLen > 0 && DictContains(dc, buf + pos, tryLen)) {
                bestEnd = tryEnd;
                break;
            }

            /* Move tryEnd to previous grapheme boundary. */
            size_t prev = tryEnd - 1;
            while (prev > pos && !gBreaks[prev]) prev--;
            if (prev == pos) break;
            tryEnd = prev;
        }

        if (bestEnd) {
            /* Dictionary hit → real word boundary. */
            wBreaks[bestEnd] = 1;
            if (lBreaks) lBreaks[bestEnd] = 1;
            pos = bestEnd;
        } else {
            /*
             * Fallback: break at grapheme boundary.
             * This prevents giant unbreakable Thai runs.
             */
            wBreaks[nextG] = 1;
            if (lBreaks) lBreaks[nextG] = 1;
            pos = nextG;
        }

        words++;
    }

    return words;
}

/*
 * ---------------------------------------------------------------
 * mojibake_word_breaks_with_dict --
 *
 *   Compute word breaks with dictionary enhancement for complex
 *   scripts (Thai, Lao, Khmer, Myanmar). Uses forward maximum
 *   matching to insert word boundaries where UAX #29 would not.
 *
 * Results:
 *   Returns 1 on success. wBreaks is filled with break boundaries
 *   including dictionary-derived word boundaries.
 *
 * Side effects:
 *   May allocate temporary grapheme break array if gBreaks_tmp is NULL.
 * ---------------------------------------------------------------
 */
int
mojibake_word_breaks_with_dict(const char *buf,
			       size_t byteLen,
                               unsigned char *wBreaks,
                               unsigned char *gBreaks_tmp)
{
    if (!buf || !wBreaks) return 0;
    EnsureDictsInitialized();

    unsigned char *gBreaks = gBreaks_tmp;
    unsigned char *localG = NULL;
    if (!gBreaks) {
        localG = (unsigned char*)malloc(byteLen+1);
        if (!localG) return 0;
        gBreaks = localG;
    }
    mojibake_grapheme_breaks(buf, byteLen, gBreaks);
    mojibake_word_breaks(buf, byteLen, wBreaks);

    /* Now walk the buffer codepoint by codepoint to find runs of complex script. */
    size_t i=0;
    while (i < byteLen) {
        /* Skip if not at grapheme start. */
        if (!gBreaks[i]) { i++; continue; }
        /* decode codepoint at i */
        mjb_codepoint cp=0;
        int clen=0;
        unsigned char b = (unsigned char)buf[i];
        if (b < 0x80) { cp=b; clen=1; }
        else if ((b>>5)==0x6) { if (i+1<byteLen){ cp=((b&0x1F)<<6)|(buf[i+1]&0x3F); clen=2; } else clen=1; }
        else if ((b>>4)==0xE) { if (i+2<byteLen){ cp=((b&0x0F)<<12)|((buf[i+1]&0x3F)<<6)|(buf[i+2]&0x3F); clen=3; } else clen=1; }
        else if ((b>>3)==0x1E) { if (i+3<byteLen){ cp=((b&0x07)<<18)|((buf[i+1]&0x3F)<<12)|((buf[i+2]&0x3F)<<6)|(buf[i+3]&0x3F); clen=4; } else clen=1; }
        else { cp=b; clen=1; }

        ComplexScript sc = ScriptFromCodepoint(cp);
        if (sc==SCRIPT_OTHER) { i+=clen; continue; }

        /*
	 * Start of run: continue while same script and not space
	 * and not word break from base? Actually we merge until script
	 *changes or space/newline.
	 */
        size_t runStart=i;
        size_t runEnd=runStart;
        ComplexScript curSc=sc;
        size_t j=i;
        while (j < byteLen) {
            if (!gBreaks[j]) { j++; continue; }
            if (j>=byteLen) break;
            /* Decode at j. */
            unsigned char bj = (unsigned char)buf[j];
            mjb_codepoint cpj=0; int lj=1;
            if (bj < 0x80) { cpj=bj; lj=1; }
            else if ((bj>>5)==0x6) { if (j+1<byteLen){ cpj=((bj&0x1F)<<6)|(buf[j+1]&0x3F); lj=2; } }
            else if ((bj>>4)==0xE) { if (j+2<byteLen){ cpj=((bj&0x0F)<<12)|((buf[j+1]&0x3F)<<6)|(buf[j+2]&0x3F); lj=3; } }
            else if ((bj>>3)==0x1E) { if (j+3<byteLen){ cpj=((bj&0x07)<<18)|((buf[j+1]&0x3F)<<12)|((buf[j+2]&0x3F)<<6)|(buf[j+3]&0x3F); lj=4; } }
            else { cpj=bj; lj=1; }

            ComplexScript scj = ScriptFromCodepoint(cpj);
            if (scj!=curSc) break;
            /* Break run on ASCII space or line break already? Keep spaces as separators, so stop before space */
            if (cpj==0x20 || cpj==0x09 || cpj==0x0A || cpj==0x0D) break;
            runEnd = j+lj;
            j += lj;
        }
        if (runEnd>runStart+1) {
            /*
             * The baseline mojibake_word_breaks() call above has no
             * knowledge of Thai/Lao/Khmer/Myanmar word boundaries, so
             * per UAX #29's documented fallback for scripts without
             * spaces, it marks a word break after every single grapheme
             * cluster in this run. Left in place, those per-character
             * breaks would sit alongside the real boundaries found by
             * the dictionary matcher below, and every downstream
             * consumer (line-break promotion, the line-wrap search)
             * would see "break allowed" almost everywhere -- effectively
             * degrading word-wrap to character-wrap for this run. Clear
             * them so only genuine dictionary-derived boundaries (plus
             * ForwardMaxMatchRun's own fallback breaks for real
             * dictionary misses) remain.
             */
            memset(wBreaks + runStart + 1, 0, runEnd - runStart - 1);
            DictCache *dc = CacheForScript(curSc);
            if (dc) {
                ForwardMaxMatchRun(buf, runStart, runEnd, gBreaks, wBreaks, NULL, dc);
            }
        }
        i = runEnd ? runEnd : i+clen;
    }

    if (localG) free(localG);
    return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_line_breaks_with_dict --
 *
 *   Compute line breaks with dictionary enhancement. UAX #14
 *   line breaks are computed first, then dictionary word boundaries
 *   from complex scripts are promoted to line break opportunities.
 *
 * Results:
 *   Returns 1 on success. lBreaks is filled with break boundaries
 *   including dictionary-derived line break opportunities.
 *
 * Side effects:
 *   May allocate temporary grapheme and word break arrays if
 *   gBreaks_tmp or wBreaks_tmp is NULL.
 * ---------------------------------------------------------------
 */
int
mojibake_line_breaks_with_dict(const char *buf,
			       size_t byteLen,
                               unsigned char *lBreaks,
                               unsigned char *gBreaks_tmp,
                               unsigned char *wBreaks_tmp)
{
    if (!buf || !lBreaks) return 0;
    EnsureDictsInitialized();

    unsigned char *gBreaks = gBreaks_tmp;
    unsigned char *localG = NULL;
    if (!gBreaks) {
        localG = (unsigned char*)malloc(byteLen+1);
        if (!localG) return 0;
        gBreaks = localG;
        mojibake_grapheme_breaks(buf, byteLen, gBreaks);
    }

    unsigned char *wBreaks = wBreaks_tmp;
    unsigned char *localW = NULL;
    if (!wBreaks) {
        localW = (unsigned char*)malloc(byteLen+1);
        if (!localW) { if(localG) free(localG); return 0; }
        wBreaks = localW;
    }

    /* Base line breaks. */
    mojibake_line_breaks(buf, byteLen, lBreaks);
    /* Enhanced word breaks. */
    mojibake_word_breaks_with_dict(buf, byteLen, wBreaks, gBreaks);

    /* For complex scripts, every dictionary word boundary is an allowed line break. */
    for (size_t i=0;i<=byteLen;i++) {
        if (wBreaks[i]) {
            /*
	     * Only promote if inside complex script run - check surrounding cp.
	     *  Simple: if i>0, look at cp before i.
	     */
            if (i>0 && i<byteLen) {
                /* Find start of cluster ending at i: previous grapheme start. */
                size_t prev = i-1;
                while (prev>0 && !gBreaks[prev]) prev--;
                /* Decode cp at prev. */
                unsigned char b = (unsigned char)buf[prev];
                mjb_codepoint cp=0;
                if (b < 0x80) cp=b;
                else if ((b>>5)==0x6 && prev+1<byteLen) cp=((b&0x1F)<<6)|(buf[prev+1]&0x3F);
                else if ((b>>4)==0xE && prev+2<byteLen) cp=((b&0x0F)<<12)|((buf[prev+1]&0x3F)<<6)|(buf[prev+2]&0x3F);
                else if ((b>>3)==0x1E && prev+3<byteLen) cp=((b&0x07)<<18)|((buf[prev+1]&0x3F)<<12)|((buf[prev+2]&0x3F)<<6)|(buf[prev+3]&0x3F);
                if (ScriptFromCodepoint(cp)!=SCRIPT_OTHER) {
                    lBreaks[i]=1;
                }
            }
        }
    }

    if (localG) free(localG);
    if (localW) free(localW);
    return 1;
}

/*
 * ---------------------------------------------------------------
 * mojibake_grapheme_breaks_wrapper --
 *
 *   Convenience wrapper for mojibake_grapheme_breaks.
 *
 * Results:
 *   Returns 1 on success.
 *
 * Side effects:
 *   None.
 * ---------------------------------------------------------------
 */
int
mojibake_grapheme_breaks_wrapper(const char *buf,
				 size_t len,
				 unsigned char *br) {
    return mojibake_grapheme_breaks(buf,len,br);
}

/*
 * ---------------------------------------------------------------
 * mojibake_dict_init --
 *
 *   Expose dictionary initialization for Tk initialization.
 *   Ensures all dictionary caches are initialized.
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   Initializes all dictionary caches.
 * ---------------------------------------------------------------
 */

void
mojibake_dict_init(void) {
    EnsureDictsInitialized();
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
