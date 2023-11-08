/*
 * tkBitField.c --
 *
 *	This module implements bit field operations.
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkBitField.h"
#include "tkIntSet.h"
#include <assert.h>

#ifndef MAX
# define MAX(a,b) (((int) a) < ((int) b) ? b : a)
#endif
#ifndef MIN
# define MIN(a,b) (((int) a) < ((int) b) ? a : b)
#endif

#ifdef TK_CHECK_ALLOCS
# define DEBUG_ALLOC(expr) expr
#else
# define DEBUG_ALLOC(expr)
#endif


#define NBITS		TK_BIT_NBITS
#define NWORDS(size)	TK_BIT_COUNT_WORDS(size)
#define BIT_INDEX(n)	TK_BIT_INDEX(n)
#define WORD_INDEX(n)	TK_BIT_WORD_INDEX(n)

#define NBYTES(words)	((words)*sizeof(size_t))
#define BYTE_SIZE(size)	NBYTES(NWORDS(size))
#define BF_SIZE(size)	(offsetof(TkBitField, bits) + BYTE_SIZE(size))
#define BIT_SPAN(f,t)	((~((size_t) 0) << (f)) & (~((size_t) 0) >> ((NBITS - 1) - (t))))


DEBUG_ALLOC(unsigned tkBitCountNew = 0);
DEBUG_ALLOC(unsigned tkBitCountDestroy = 0);


#ifdef TK_IS_64_BIT_ARCH

/* ****************************************************************************/
/*                 64 bit implementation                                      */
/* ****************************************************************************/

# if defined(__GNUC__) || defined(__clang__)

#  define LsbIndex(x) __builtin_ctzll(x)
#  define MsbIndex(x) ((sizeof(unsigned long long)*8 - 1) - __builtin_clzll(x))

# else /* !(defined(__GNUC__) || defined(__clang__)) */

static unsigned
LsbIndex(uint64_t x)
{
    /* Source: http://chessprogramming.wikispaces.com/BitScan (adapted for MSVC) */
    static const unsigned MultiplyDeBruijnBitPosition[64] = {
	 0,  1, 48,  2, 57, 49, 28,  3, 61, 58, 50, 42, 38, 29, 17,  4,
	62, 55, 59, 36, 53, 51, 43, 22, 45, 39, 33, 30, 24, 18, 12,  5,
	63, 47, 56, 27, 60, 41, 37, 16, 54, 35, 52, 21, 44, 32, 23, 11,
	46, 26, 40, 15, 34, 20, 31, 10, 25, 14, 19,  9, 13,  8,  7,  6
    };
    uint64_t idx = ((uint64_t) ((x & -((int64_t) x))*UINT64_C(0x03f79d71b4cb0a89))) >> 58;
    return MultiplyDeBruijnBitPosition[idx];
}

static unsigned
MsbIndex(uint64_t x)
{
    /*
     * Source: http://stackoverflow.com/questions/671815/what-is-the-fastest-most-efficient-way-to-find-the-highest-set-bit-msb-in-an-i
     * (extended to 64 bit by GC)
     */

   static const uint8_t Table[16] = { -1, 0, 1,1, 2,2,2,2, 3,3,3,3,3,3,3,3 };

   unsigned r = 0;
   uint64_t xk;

   if ((xk = x >> 32)) { r =  32; x = xk; }
   if ((xk = x >> 16)) { r += 16; x = xk; }
   if ((xk = x >>  8)) { r +=  8; x = xk; }
   if ((xk = x >>  4)) { r +=  4; x = xk; }

   return r + Table[x];
}

# endif /* defined(__GNUC__) || defined(__clang__) */

static unsigned
PopCount(uint64_t x)
{
    /* Source: http://chessprogramming.wikispaces.com/Population+Count */
    x -=  (x >> 1) & UINT64_C(0x5555555555555555);
    x  = ((x >> 2) & UINT64_C(0x3333333333333333)) + (x & UINT64_C(0x3333333333333333));
    x  = ((x >> 4) + x) & UINT64_C(0x0F0F0F0F0F0F0F0F);
    return (x * UINT64_C(0x0101010101010101)) >> 56;
}

#else /* TK_IS_64_BIT_ARCH */

/* ****************************************************************************/
/*                 32 bit implementation                                      */
/* ****************************************************************************/

# if defined(__GNUC__) || defined(__clang__)

#  define LsbIndex(x) __builtin_ctz(x)
#  define MsbIndex(x) ((sizeof(unsigned)*8 - 1) - __builtin_clz(x))

# else /* defined(__GNUC__) || defined(__clang__) */

#  if 1
/* On my system this is the fastest method, only about 5% slower than __builtin_ctz(). */
static unsigned
LsbIndex(uint32_t x)
{
    /*
     * Source: http://graphics.stanford.edu/~seander/bithacks.html#ZerosOnRightLinear
     * (adapted for MSVC)
     */
    static const unsigned MultiplyDeBruijnBitPosition[32] = {
	 0,  1, 28,  2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17,  4, 8,
	31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18,  6, 11,  5, 10, 9
    };
    return MultiplyDeBruijnBitPosition[((uint32_t) ((x & -((int32_t) x))*0x077cb531)) >> 27];
}
#  else
/* The "classical" method, but about 20% slower than the DeBruijn method on my system. */
static unsigned
LsbIndex(uint32_t x)
{
    unsigned ctz = 32;
    x &= -((int32_t ) x);
    if (x) --ctz;
    if (x & 0x0000ffff) ctz -= 16;
    if (x & 0x00ff00ff) ctz -= 8;
    if (x & 0x0f0f0f0f) ctz -= 4;
    if (x & 0x33333333) ctz -= 2;
    if (x & 0x55555555) ctz -= 1;
    return ctz;
}
#  endif

static unsigned
MsbIndex(uint32_t x)
{
    /* Source: http://stackoverflow.com/questions/671815/what-is-the-fastest-most-efficient-way-to-find-the-highest-set-bit-msb-in-an-i */
   static const uint8_t Table[16] = { -1 ,0, 1,1, 2,2,2,2, 3,3,3,3,3,3,3,3 };

   unsigned r = 0;
   uint32_t xk;

   if ((xk = x >> 16)) { r = 16; x = xk; }
   if ((xk = x >>  8)) { r += 8; x = xk; }
   if ((xk = x >>  4)) { r += 4; x = xk; }

   return r + Table[x];
}

# endif /* defined(__GNUC__) || defined(__clang__) */

static unsigned
PopCount(uint32_t x)
{
    /* Source: http://graphics.stanford.edu/~seander/bithacks.html */
    /* NOTE: the GCC function __builtin_popcount() is slower on my system. */
    x -=  (x >> 1) & 0x55555555;
    x  = ((x >> 2) & 0x33333333) + (x & 0x33333333);
    x  = ((x >> 4) + x) & 0x0f0f0f0f;
    return (x*0x01010101) >> 24;
}

#endif /* !TK_IS_64_BIT_ARCH */


#ifdef TK_CHECK_ALLOCS
/*
 * Some useful functions for finding memory leaks.
 */

static TkBitField *Used = NULL;
static TkBitField *Last = NULL;


static void
Use(
    TkBitField *bf)
{
    static int N = 0;
    if (!Used) { Used = bf; }
    if (Last) { Last->next = bf; }
    bf->number = N++;
    bf->next = NULL;
    bf->prev = Last;
    Last = bf;
}


static void
Free(
    TkBitField *bf)
{
    assert(bf->prev || Used == bf);
    assert(bf->next || Last == bf);
    if (Last == bf) { Last = bf->prev; }
    if (Used == bf) { Used = bf->next; }
    if (bf->prev) { bf->prev->next = bf->next; }
    if (bf->next) { bf->next->prev = bf->prev; }
    bf->prev = NULL;
    bf->next = NULL;
}


void
TkBitCheckAllocs()
{
    for ( ; Used; Used = Used->next) {
	printf("TkBitField(number): %d\n", Used->number);
    }
}

#endif /* TK_CHECK_ALLOCS */


static int
IsEqual(
    const size_t *s,
    const size_t *t,
    unsigned numBytes)
{
    const size_t *e = s + numBytes;

    for ( ; s < e; ++s, ++t) {
	if (*s != *t) {
	    return 0;
	}
    }
    return 1;
}


static void
ResetUnused(
    TkBitField *bf)
{
    unsigned bitIndex = BIT_INDEX(bf->size);

    if (bitIndex) {
	bf->bits[NWORDS(bf->size) - 1] &= ~BIT_SPAN(bitIndex, NBITS - 1);
    }
}


void
TkBitDestroy(
    TkBitField **bfPtr)
{
    assert(bfPtr);

    if (*bfPtr) {
	DEBUG_ALLOC(Free(*bfPtr));
	ckfree(*bfPtr);
	*bfPtr = NULL;
	DEBUG_ALLOC(tkBitCountDestroy++);
    }
}


TkBitField *
TkBitResize(
    TkBitField *bf,
    unsigned newSize)
{
    if (!bf) {
	bf = (TkBitField *)ckalloc(BF_SIZE(newSize));
	DEBUG_ALLOC(Use(bf));
	bf->size = newSize;
	bf->refCount = 1;
	bf->isSetFlag = 0;
	memset(bf->bits, 0, BYTE_SIZE(newSize));
	DEBUG_ALLOC(tkBitCountNew++);
    } else {
	unsigned newWords;
	unsigned oldWords;

	newWords = NWORDS(newSize);
	oldWords = NWORDS(bf->size);

	if (newWords == oldWords) {
	    bf->size = newSize;
	    ResetUnused(bf);
	    return bf;
	}

	if (bf->refCount <= 1) {
	    DEBUG_ALLOC(Free(bf));
	    bf = (TkBitField *)ckrealloc((char *) bf, BF_SIZE(newSize));
	    DEBUG_ALLOC(Use(bf));
	} else {
	    TkBitField *newBF = (TkBitField *)ckalloc(BF_SIZE(newSize));
	    DEBUG_ALLOC(Use(newBF));
	    memcpy(newBF->bits, bf->bits, NBYTES(MIN(oldWords, newWords)));
	    newBF->refCount = 1;
	    newBF->isSetFlag = 0;
	    bf->refCount -= 1;
	    bf = newBF;
	    DEBUG_ALLOC(tkBitCountNew++);
	}

	bf->size = newSize;

	if (oldWords < newWords) {
	    memset(bf->bits + oldWords, 0, NBYTES(newWords - oldWords));
	} else {
	    ResetUnused(bf);
	}
    }

    return bf;
}


TkBitField *
TkBitFromSet(
    const TkIntSet *set,
    unsigned size)
{
    unsigned numEntries = TkIntSetSize(set);
    TkBitField *bf = TkBitResize(NULL, size);
    unsigned i;

    for (i = 0; i < numEntries; ++i) {
	TkIntSetType value = TkIntSetAccess(set, i);

	if (value >= size) {
	    break;
	}
	TkBitSet(bf, value);
    }

    return bf;
}


size_t
TkBitCount(
    const TkBitField *bf)
{
    size_t words, i;
    size_t count = 0;

    assert(bf);

    words = NWORDS(bf->size);

    for (i = 0; i < words; ++i) {
	count += PopCount(bf->bits[i]);
    }

    return count;
}


TkBitField *
TkBitCopy(
    const TkBitField *bf,
    int size)
{
    TkBitField *copy;
    unsigned oldWords, newWords;

    assert(bf);

    if (size < 0) {
	size = bf->size;
    }

    copy = (TkBitField *)ckalloc(BF_SIZE(size));
    DEBUG_ALLOC(Use(copy));
    oldWords = NWORDS(bf->size);
    newWords = NWORDS(size);
    memcpy(copy->bits, bf->bits, NBYTES(MIN(oldWords, newWords)));
    if (newWords > oldWords) {
	memset(copy->bits + oldWords, 0, NBYTES(newWords - oldWords));
    }
    copy->size = size;
    copy->refCount = 1;
    copy->isSetFlag = 0;
    ResetUnused(copy);
    DEBUG_ALLOC(tkBitCountNew++);
    return copy;
}


void
TkBitJoin(
    TkBitField *dst,
    const TkBitField *src)
{
    unsigned words, i;

    assert(dst);
    assert(src);
    assert(TkBitSize(src) <= TkBitSize(dst));

    if (src != dst && src->size > 0) {
	for (i = 0, words = NWORDS(src->size); i < words; ++i) {
	    dst->bits[i] |= src->bits[i];
	}
    }
}


void
TkBitJoin2(
    TkBitField *dst,
    const TkBitField *bf1,
    const TkBitField *bf2)
{
    unsigned words1, words2, words, i;

    assert(dst);
    assert(bf1);
    assert(bf2);
    assert(TkBitSize(dst) >= TkBitSize(bf1));
    assert(TkBitSize(dst) >= TkBitSize(bf2));

    words1 = NWORDS(bf1->size);
    words2 = NWORDS(bf2->size);
    words = MIN(words1, words2);

    for (i = 0; i < words; ++i) {
	dst->bits[i] |= bf1->bits[i] | bf2->bits[i];
    }
    for ( ; i < words1; ++i) {
	dst->bits[i] |= bf1->bits[i];
    }
    for ( ; i < words2; ++i) {
	dst->bits[i] |= bf2->bits[i];
    }
}


void
TkBitIntersect(
    TkBitField *dst,
    const TkBitField *src)
{
    unsigned srcWords, dstWords, i;

    assert(dst);
    assert(src);

    if (src == dst || dst->size == 0) {
	return;
    }

    srcWords = NWORDS(src->size);
    dstWords = NWORDS(dst->size);

    if (dstWords > srcWords) {
	memset(dst->bits + srcWords, 0, NBYTES(dstWords - srcWords));
	dstWords = srcWords;
    }

    for (i = 0; i < dstWords; ++i) {
	dst->bits[i] &= src->bits[i];
    }

    return;
}


void
TkBitRemove(
    TkBitField *dst,
    const TkBitField *src)
{
    unsigned dstWords;

    assert(dst);
    assert(src);

    if (dst->size == 0 || src->size == 0) {
	return;
    }

    dstWords = NWORDS(dst->size);

    if (src == dst) {
	memset(dst->bits, 0, NBYTES(dstWords));
    } else {
	unsigned words = MIN(NWORDS(src->size), dstWords);
	unsigned i;

	for (i = 0; i < words; ++i) {
	    dst->bits[i] &= ~src->bits[i];
	}
    }
}


void
TkBitComplementTo(
    TkBitField *dst,
    const TkBitField *src)
{
    unsigned srcWords, dstWords;

    assert(dst);
    assert(src);
    assert(TkBitSize(src) <= TkBitSize(dst));

    if (dst->size == 0) {
	return;
    }

    dstWords = NWORDS(dst->size);

    if (src == dst || src->size == 0) {
	srcWords = 0;
    } else {
	unsigned i;

	srcWords = NWORDS(src->size);

	for (i = 0; i < srcWords; ++i) {
	    dst->bits[i] = src->bits[i] & ~dst->bits[i];
	}
    }

    memset(dst->bits + srcWords, 0, NBYTES(dstWords - srcWords));
}


void
TkBitJoinComplementTo(
    TkBitField *dst,
    const TkBitField *bf1,
    const TkBitField *bf2)
{
    unsigned i, words, words2;

    assert(dst);
    assert(bf1);
    assert(bf2);
    assert(TkBitSize(dst) >= TkBitSize(bf1));
    assert(TkBitSize(dst) >= TkBitSize(bf2));

    if (dst == bf2 || bf2->size == 0) {
	return;
    }

    assert(TkBitSize(bf2) >= TkBitSize(bf1));

    words2 = NWORDS(bf2->size);
    words = MIN(NWORDS(bf1->size), words2);

    for (i = 0; i < words; ++i) {
	dst->bits[i] |= bf2->bits[i] & ~bf1->bits[i];
    }
    for ( ; i < words2; ++i) {
	dst->bits[i] |= bf2->bits[i];
    }
}


void
TkBitJoinNonIntersection(
    TkBitField *dst,
    const TkBitField *bf1,
    const TkBitField *bf2)
{
    assert(dst);
    assert(bf1);
    assert(bf2);
    assert(TkBitSize(dst) >= TkBitSize(bf1));
    assert(TkBitSize(dst) >= TkBitSize(bf2));

    if (bf1 == bf2) {
	return;
    }

    if (bf1->size == 0) {
	TkBitJoin(dst, bf2);
    } else if (bf2->size == 0) {
	TkBitJoin(dst, bf1);
    } else {
	unsigned i, words = MIN(NWORDS(bf1->size), NWORDS(bf2->size));

	for (i = 0; i < words; ++i) {
	    size_t bf1Bits = bf1->bits[i];
	    size_t bf2Bits = bf2->bits[i];

	    dst->bits[i] |= (bf1Bits & ~bf2Bits) | (bf2Bits & ~bf1Bits);
	}
    }
}


void
TkBitJoin2ComplementToIntersection(
    TkBitField *dst,
    const TkBitField *add,
    const TkBitField *bf1,
    const TkBitField *bf2)
{
    assert(dst);
    assert(add);
    assert(bf1);
    assert(bf2);
    assert(TkBitSize(dst) >= TkBitSize(add));
    assert(TkBitSize(dst) >= TkBitSize(bf1));
    assert(TkBitSize(bf1) == TkBitSize(bf2));

    /* dst := dst + add + ((bf1 + bf2) - (bf1 & bf2)) */

    if (bf1 == bf2) {
	TkBitJoin(dst, add);
    } else {
	unsigned words1 = NWORDS(add->size);
	unsigned words2 = NWORDS(bf1->size);
	unsigned words = MIN(words1, words2);
	unsigned i;

	for (i = 0; i < words; ++i) {
	    size_t bf1Bits = bf1->bits[i];
	    size_t bf2Bits = bf2->bits[i];

	    dst->bits[i] |= add->bits[i] | ((bf1Bits | bf2Bits) & ~(bf1Bits & bf2Bits));
	}
	for ( ; i < words2; ++i) {
	    size_t bf1Bits = bf1->bits[i];
	    size_t bf2Bits = bf2->bits[i];

	    dst->bits[i] |= (bf1Bits | bf2Bits) & ~(bf1Bits & bf2Bits);
	}
	for ( ; i < words1; ++i) {
	    dst->bits[i] |= add->bits[i];
	}
    }
}


void
TkBitJoinOfDifferences(
    TkBitField *dst,
    const TkBitField *bf1,
    const TkBitField *bf2)
{
    unsigned words, words1, words2, i;

    assert(dst);
    assert(bf1);
    assert(bf2);
    assert(TkBitSize(dst) >= TkBitSize(bf1));

    words1 = NWORDS(bf1->size);
    words2 = NWORDS(bf2->size);

    words = MIN(words1, words2);

    for (i = 0; i < words; ++i) {
	size_t bf1Bits = bf1->bits[i];
	size_t bf2Bits = bf2->bits[i];

	/* dst := (dst - bf1) + (bf1 - bf2) */
	dst->bits[i] = (dst->bits[i] & ~bf1Bits) | (bf1Bits & ~bf2Bits);
    }

    for ( ; i < words1; ++i) {
	/* dst := dst + bf1 */
	dst->bits[i] |= bf1->bits[i];
    }
}


void
TkBitClear(
    TkBitField *bf)
{
    assert(bf);
    memset(bf->bits, 0, BYTE_SIZE(bf->size));
}


int
TkBitNone_(
    const size_t *bits,
    unsigned words)
{
    unsigned i;

    assert(bits);

    for (i = 0; i < words; ++i) {
	if (bits[i]) {
	    return 0;
	}
    }
    return 1;
}


int
TkBitAny(
    const TkBitField *bf)
{
    unsigned words, i;

    assert(bf);

    words = NWORDS(bf->size);

    for (i = 0; i < words; ++i) {
	if (bf->bits[i]) {
	    return 1;
	}
    }

    return 0;
}


int
TkBitComplete(
    const TkBitField *bf)
{
    unsigned words;

    assert(bf);

    words = NWORDS(bf->size);

    if (words)
    {
	unsigned i, n = words - 1;

	for (i = 0; i < n; ++i) {
	    if (bf->bits[i] != ~((size_t) 0)) {
		return 0;
	    }
	}

	if (bf->bits[words - 1] != BIT_SPAN(0, BIT_INDEX(bf->size - 1))) {
	    return 0;
	}
    }

    return 1;
}


int
TkBitIsEqual(
    const TkBitField *bf1,
    const TkBitField *bf2)
{
    unsigned words1;

    assert(bf1);
    assert(bf2);

    if (bf1 == bf2) {
	return 1;
    }

    if (bf1->size > bf2->size) {
	const TkBitField *bf = bf1;
	bf1 = bf2;
	bf2 = bf;
    }

    words1 = NWORDS(bf1->size);

    if (!IsEqual(bf1->bits, bf2->bits, words1)) {
	return 0;
    }

    return TkBitNone_(bf2->bits + words1, NWORDS(bf2->size) - words1);
}


int
TkBitContains(
    const TkBitField *bf1,
    const TkBitField *bf2)
{
    unsigned words1, words2, i;

    assert(bf1);
    assert(bf2);

    if (bf1 == bf2) {
	return 1;
    }

    words1 = NWORDS(bf1->size);
    words2 = NWORDS(bf2->size);

    if (words1 < words2) {
	if (!TkBitNone_(bf2->bits + words1, words2 - words1)) {
	    return 0;
	}
	words2 = words1;
    }

    for (i = 0; i < words2; ++i) {
	size_t bits2 = bf2->bits[i];

	if (bits2 != (bf1->bits[i] & bits2)) {
	    return 0;
	}
    }

    return 1;
}


int
TkBitDisjunctive(
    const TkBitField *bf1,
    const TkBitField *bf2)
{
    unsigned words, i;

    assert(bf1);
    assert(bf2);

    if (bf1 == bf2) {
	return TkBitNone(bf1);
    }

    words = MIN(NWORDS(bf1->size), NWORDS(bf2->size));

    for (i = 0; i < words; ++i) {
	if (bf1->bits[i] & bf2->bits[i]) {
	    return 0;
	}
    }

    return 1;
}


int
TkBitIntersectionIsEqual(
    const TkBitField *bf1,
    const TkBitField *bf2,
    const TkBitField *del)
{
    unsigned words, words1, words2, i;

    assert(bf1);
    assert(bf2);
    assert(del);
    assert(TkBitSize(bf1) <= TkBitSize(del));
    assert(TkBitSize(bf2) <= TkBitSize(del));

    if (bf1 == bf2) {
	return 1;
    }
    if (bf1->size == 0) {
	return TkBitNone(bf2);
    }
    if (bf2->size == 0) {
	return TkBitNone(bf1);
    }

    words1 = NWORDS(bf1->size);
    words2 = NWORDS(bf2->size);
    words = MIN(words1, words2);

    for (i = 0; i < words; ++i) {
	size_t bits = del->bits[i];
	if ((bf1->bits[i] & bits) != (bf2->bits[i] & bits)) {
	    return 0;
	}
    }

    for (i = words; i < words1; ++i) {
	if (bf1->bits[i] & del->bits[i]) {
	    return 0;
	}
    }

    for (i = words; i < words2; ++i) {
	if (bf2->bits[i] & del->bits[i]) {
	    return 0;
	}
    }

    return 1;
}


unsigned
TkBitFindFirst(
    const TkBitField *bf)
{
    unsigned words, i;

    assert(bf);

    words = NWORDS(bf->size);

    for (i = 0; i < words; ++i) {
	size_t bits = bf->bits[i];

	if (bits) {
	    return NBITS*i + LsbIndex(bits);
	}
    }

    return TK_BIT_NPOS;
}


unsigned
TkBitFindLast(
    const TkBitField *bf)
{
    int i;

    assert(bf);

    for (i = NWORDS(bf->size) - 1; i >= 0; --i) {
	size_t bits = bf->bits[i];

	if (bits) {
	    return NBITS*i + MsbIndex(bits);
	}
    }

    return TK_BIT_NPOS;
}


unsigned
TkBitFindFirstNot(
    const TkBitField *bf)
{
    Tcl_Size words, mask, bits, i;

    assert(bf);

    if (bf->size > 0) {
	words = NWORDS(bf->size) - 1;

	for (i = 0; i < words; ++i) {
	    bits = bf->bits[i];

	    if (bits != ~((Tcl_Size) 0)) {
		return NBITS*i + LsbIndex(~bits);
	    }
	}

	mask = BIT_SPAN(0, BIT_INDEX(bf->size - 1));
	bits = bf->bits[words];

	if (bits != mask) {
	    return NBITS*words + LsbIndex(~bits & mask);
	}
    }

    return TK_BIT_NPOS;
}


unsigned
TkBitFindLastNot(
    const TkBitField *bf)
{
    assert(bf);

    if (bf->size > 0) {
	size_t bits,mask;
	unsigned words;
	int i;

	words = NWORDS(bf->size) - 1;
	mask = BIT_SPAN(0, BIT_INDEX(bf->size - 1));
	bits = bf->bits[words];

	if (bits != mask) {
	    return NBITS*words + MsbIndex(~bits & mask);
	}

	for (i = words - 1; i >= 0; --i) {
	    if ((bits = bf->bits[i]) != ~((size_t) 0)) {
		return NBITS*i + MsbIndex(~bits);
	    }
	}
    }

    return TK_BIT_NPOS;
}


unsigned
TkBitFindNext(
    const TkBitField *bf,
    unsigned prev)
{
    size_t bits;
    unsigned i, words;

    assert(bf);
    assert(prev < TkBitSize(bf));

    i = WORD_INDEX(prev);
    bits = bf->bits[i] & ~BIT_SPAN(0, BIT_INDEX(prev));

    if (bits) {
	return NBITS*i + LsbIndex(bits);
    }

    words = NWORDS(bf->size);

    for (++i; i < words; ++i) {
	if ((bits = bf->bits[i])) {
	    return NBITS*i + LsbIndex(bits);
	}
    }

    return TK_BIT_NPOS;
}


unsigned
TkBitFindNextNot(
    const TkBitField *bf,
    unsigned prev)
{
    size_t bits;
    unsigned i, words;

    assert(bf);
    assert(prev < TkBitSize(bf));

    i = WORD_INDEX(prev);
    bits = bf->bits[i] & ~BIT_SPAN(0, BIT_INDEX(prev));

    if (~bits != ~((size_t) 0)) {
	return NBITS*i + LsbIndex(bits);
    }

    words = NWORDS(bf->size);

    for (++i; i < words; ++i) {
	if (bits != ~((size_t) 0)) {
	    return NBITS*i + LsbIndex(~bits);
	}
    }

    return TK_BIT_NPOS;
}


unsigned
TkBitFindPrev(
    const TkBitField *bf,
    unsigned next)
{
    size_t bits;
    int i;

    assert(bf);
    assert(next < TkBitSize(bf));

    i = WORD_INDEX(next);
    bits = bf->bits[i] & ~BIT_SPAN(BIT_INDEX(next), NBITS - 1);

    if (bits) {
	return NBITS*i + MsbIndex(bits);
    }

    for (--i; i >= 0; --i) {
	if ((bits = bf->bits[i])) {
	    return NBITS*i + MsbIndex(bits);
	}
    }

    return TK_BIT_NPOS;
}


unsigned
TkBitFindFirstInIntersection(
    const TkBitField *bf1,
    const TkBitField *bf2)
{
    unsigned words, i;

    assert(bf1);
    assert(bf2);

    words = NWORDS(MIN(bf1->size, bf2->size));

    for (i = 0; i < words; ++i) {
	size_t bits = bf1->bits[i] & bf2->bits[i];

	if (bits) {
	    return LsbIndex(bits);
	}
    }

    return TK_BIT_NPOS;
}


int
TkBitTestAndSet(
    TkBitField *bf,
    unsigned n)
{
    size_t *word;
    size_t mask;

    assert(bf);
    assert(n < TkBitSize(bf));

    word = bf->bits + WORD_INDEX(n);
    mask = TK_BIT_MASK(BIT_INDEX(n));

    if (*word & mask) {
	return 0;
    }
    *word |= mask;
    return 1;
}


int
TkBitTestAndUnset(
    TkBitField *bf,
    unsigned n)
{
    size_t *word;
    size_t mask;

    assert(bf);
    assert(n < TkBitSize(bf));

    word = bf->bits + WORD_INDEX(n);
    mask = TK_BIT_MASK(BIT_INDEX(n));

    if (!(*word & mask)) {
	return 0;
    }
    *word &= ~mask;
    return 1;
}


void
TkBitFill(
    TkBitField *bf)
{
    memset(bf->bits, 0xff, BYTE_SIZE(bf->size));
    ResetUnused(bf);
}


#ifndef NDEBUG

# include <stdio.h>

void
TkBitPrint(
    const TkBitField *bf)
{
    unsigned i;
    const char *comma = "";

    assert(bf);

    printf("%" TCL_Z_MODIFIER "d:{ ", TkBitCount(bf));
    for (i = TkBitFindFirst(bf); i != TK_BIT_NPOS; i = TkBitFindNext(bf, i)) {
	printf("%s%d", comma, i);
	comma = ", ";
    }
    printf(" }\n");
}

#endif /* NDEBUG */

#ifdef TK_UNUSED_BITFIELD_FUNCTIONS

/*
 * These functions are not needed anymore, but shouldn't be removed, because sometimes
 * any of these functions might be useful.
 */

void
TkBitInnerJoinDifference(
    TkBitField *dst,
    const TkBitField *add,
    const TkBitField *sub)
{
    unsigned words1, words2, i;

    assert(dst);
    assert(add);
    assert(sub);
    assert(TkBitSize(add) <= TkBitSize(dst));

    words2 = NWORDS(add->size);
    words1 = MIN(words2, NWORDS(sub->size));

    for (i = 0; i < words1; ++i) {
	size_t addBits = add->bits[i];
	dst->bits[i] = (dst->bits[i] & addBits) | (addBits & ~sub->bits[i]);
    }

    for ( ; i < words2; ++i) {
	size_t addBits = add->bits[i];
	dst->bits[i] = (dst->bits[i] & addBits) | addBits;
    }
}


int
TkBitInnerJoinDifferenceIsEmpty(
    const TkBitField *bf,
    const TkBitField *add,
    const TkBitField *sub)
{
    unsigned words, i;
    unsigned bfWords, addWords, subWords;

    assert(bf);
    assert(add);
    assert(sub);

    /* (bf & add) + (add - sub) == nil */

    if (add->size == 0) {
	/* nil */
	return 1;
    }

    if (add == bf) {
	/* add == nil */
	return TkBitNone(add);
    }

    bfWords = NWORDS(bf->size);
    addWords = NWORDS(add->size);
    subWords = NWORDS(sub->size);

    words = MIN(bfWords, MIN(addWords, subWords));

    for (i = 0; i < words; ++i) {
	size_t addBits = add->bits[i];
	if ((bf->bits[i] & addBits) | (addBits & ~sub->bits[i])) {
	    return 0;
	}
    }

    if (addWords == words) {
	/* nil */
	return 1;
    }

    if (bfWords > words) {
	assert(subWords == words);
	/* add == nil */

	for ( ; i < addWords; ++i) {
	    if (add->bits[i]) {
		return 0;
	    }
	}
    } else {
	assert(bfWords == words);
	words = MIN(addWords, subWords);

	/* (add - sub) == nil */

	for ( ; i < words; ++i) {
	    if (add->bits[i] & ~sub->bits[i]) {
		return 0;
	    }
	}
    }

    return 1;
}


int
TkBitIsEqualToDifference(
    const TkBitField *bf1,
    const TkBitField *bf2,
    const TkBitField *sub2)
{
    unsigned words0, words1, words2, i;

    assert(bf1);
    assert(bf2);
    assert(sub2);
    assert(TkBitSize(bf2) == TkBitSize(sub2));

    if (bf2->size == 0) {
	return TkBitNone(bf1);
    }
    if (bf1->size == 0) {
	return TkBitContains(sub2, bf2);
    }

    words1 = NWORDS(bf1->size);
    words2 = NWORDS(bf2->size);
    words0 = MIN(words1, words2);

    /* bf1 == bf2 - sub2 */

    for (i = 0; i < words0; ++i) {
	if (bf1->bits[i] != (bf2->bits[i] & ~sub2->bits[i])) {
	    return 0;
	}
    }

    if (words1 > words2) {
	return TkBitNone_(bf1->bits + words2, words1 - words2);
    }

    for ( ; i < words2; ++i) {
	if (bf2->bits[i] & ~sub2->bits[i]) {
	    return 0;
	}
    }

    return 1;
}


int
TkBitIsEqualToInnerJoin(
    const TkBitField *bf1,
    const TkBitField *bf2,
    const TkBitField *add2)
{
    unsigned words0, words1, words2, i;

    assert(bf1);
    assert(bf2);
    assert(add2);
    assert(TkBitSize(bf2) == TkBitSize(add2));

    if (bf1 == bf2) {
	return 1;
    }
    if (bf2 == add2) {
	return TkBitIsEqual(bf1, bf2);
    }
    if (bf1->size == 0) {
	return TkBitNone(bf2);
    }
    if (bf2->size == 0) {
	return TkBitNone(bf1);
    }

    words1 = NWORDS(bf1->size);
    words2 = NWORDS(bf2->size);
    words0 = MIN(words1, words2);

    for (i = 0; i < words0; ++i) {
	size_t bf2Bits = bf2->bits[i];
	if (bf1->bits[i] != (bf2Bits | (add2->bits[i] & bf2Bits))) {
	    return 0;
	}
    }

    if (words1 > words2) {
	return TkBitNone_(bf1->bits + words2, words1 - words2);
    }

    for ( ; i < words2; ++i) {
	size_t bf2Bits = bf2->bits[i];
	if (bf2Bits | (add2->bits[i] & bf2Bits)) {
	    return 0;
	}
    }

    return 1;
}


int
TkBitIsEqualToInnerJoinDifference(
    const TkBitField *bf1,
    const TkBitField *bf2,
    const TkBitField *add2,
    const TkBitField *sub2)
{
    unsigned words0, words1, words2, i;

    assert(bf1);
    assert(bf2);
    assert(add2);
    assert(sub2);
    assert(TkBitSize(bf2) == TkBitSize(add2));
    assert(TkBitSize(bf2) == TkBitSize(sub2));

    if (add2->size == 0) {
	return TkBitNone(bf1);
    }
    if (sub2->size == 0) {
	return TkBitIsEqual(bf1, add2);
    }

    words1 = NWORDS(bf1->size);
    words2 = NWORDS(bf2->size);
    words0 = MIN(words1, words2);

    for (i = 0; i < words0; ++i) {
	size_t addBits = add2->bits[i];
	if (bf1->bits[i] != ((bf2->bits[i] & addBits) | (addBits & ~sub2->bits[i]))) {
	    return 0;
	}
    }

    if (words1 > words2) {
	return TkBitNone_(bf1->bits + words2, words1 - words2);
    }

    for ( ; i < words2; ++i) {
	size_t addBits = add2->bits[i];
	if ((bf2->bits[i] & addBits) | (addBits & ~sub2->bits[i])) {
	    return 0;
	}
    }

    return 1;
}


static int
IntersectionIsDisjunctive(
    const TkBitField *bf1,
    const TkBitField *bf2,
    const TkBitField *del)
{
    unsigned words = NWORDS(bf1->size);

    assert(TkBitSize(bf1) == TkBitSize(bf2));
    assert(TkBitSize(bf1) == TkBitSize(del));

    for (i = 0; i < words; ++i) {
	size_t delBits = del->bits[i];

	if ((bf1->bits[i] & delBits) != (bf2->bits[i] & delBits)) {
	    return 0;
	}
    }

    return 1;
}


int
TkBitInnerJoinDifferenceIsEqual(
    const TkBitField *bf1,
    const TkBitField *bf2,
    const TkBitField *add,
    const TkBitField *sub)
{
    unsigned words, i;

    assert(bf1);
    assert(bf2);
    assert(add);
    assert(sub);
    assert(TkBitSize(bf1) == TkBitSize(bf2));
    assert(TkBitSize(bf1) == TkBitSize(add));
    assert(TkBitSize(bf1) == TkBitSize(sub));

    if (add->size == 0) {
	return 1;
    }

    if (bf1->size == 0) {
	 /*
	  * We have to show: sub & add == bf1 & add
	  * (see InnerJoinDifferenceIsEqual [tkIntSet.c]).
	  */
	 return IntersectionIsDisjunctive(bf1, sub, add);
    }

    if (bf2->size == 0) {
	 return IntersectionIsDisjunctive(bf2, sub, add);
    }

    words = NWORDS(bf1->size);

    for (i = 0; i < words; ++i) {
	size_t addBits = add->bits[i];
	size_t sumBits = addBits & ~sub->bits[i];

	if (((bf1->bits[i] & addBits) | sumBits) != ((bf2->bits[i] & addBits) | sumBits)) {
	    return 0;
	}
    }

    return 1;
}

#endif /* TK_UNUSED_BITFIELD_FUNCTIONS */


/* Additionally we need stand-alone object code. */
extern TkBitField *TkBitNew(unsigned size);
extern const unsigned char *TkBitData(const TkBitField *bf);
extern unsigned TkBitByteSize(const TkBitField *bf);
extern unsigned TkBitRefCount(const TkBitField *bf);
extern void TkBitIncrRefCount(TkBitField *bf);
extern unsigned TkBitDecrRefCount(TkBitField *bf);
extern int TkBitIsEmpty(const TkBitField *bf);
extern size_t TkBitSize(const TkBitField *bf);
extern int TkBitTest(const TkBitField *bf, unsigned n);
extern int TkBitNone(const TkBitField *bf);
extern int TkBitIntersects(const TkBitField *bf1, const TkBitField *bf2);
extern void TkBitSet(TkBitField *bf, unsigned n);
extern void TkBitUnset(TkBitField *bf, unsigned n);
extern void TkBitPut(TkBitField *bf, unsigned n, int value);
extern unsigned TkBitAdjustSize(unsigned size);

/* vi:set ts=8 sw=4: */
