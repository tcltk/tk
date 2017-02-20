/*
 * tkIntSet.c --
 *
 *	This module implements an integer set.
 *
 *	NOTE: the current implementation is for TkTextTagSet, so in general these
 *	functions are not modifying the arguments, except if this is expected.
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkIntSet.h"
#include "tkBitField.h"
#include "tkAlloc.h"

#if !(__STDC_VERSION__ >= 199901L || (defined(_MSC_VER) && _MSC_VER >= 1900))
# define _TK_NEED_IMPLEMENTATION
# include "tkIntSetPriv.h"
#endif

#include <string.h>
#include <assert.h>

#ifndef MIN
# define MIN(a,b) (((int) a) < ((int) b) ? a : b)
#endif
#ifndef MAX
# define MAX(a,b) (((int) a) < ((int) b) ? b : a)
#endif

#if TK_CHECK_ALLOCS
# define DEBUG_ALLOC(expr) expr
#else
# define DEBUG_ALLOC(expr)
#endif


#define TestIfEqual TkIntSetIsEqual__

#define SET_SIZE(size) ((unsigned) (Tk_Offset(TkIntSet, buf) + (size)*sizeof(TkIntSetType)))


DEBUG_ALLOC(unsigned tkIntSetCountNew = 0);
DEBUG_ALLOC(unsigned tkIntSetCountDestroy = 0);


static bool IsPowerOf2(unsigned n) { return !(n & (n - 1)); }


static unsigned
NextPowerOf2(
    unsigned n)
{
    --n;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;

#if !(UINT_MAX <= 4294967295u)
    /* unsigned is 64 bit wide, this is unusual, but possible */
    n |= n >> 32;
#endif

    return ++n;
}


bool
TkIntSetIsEqual__(
    const TkIntSetType *set1, const TkIntSetType *end1,
    const TkIntSetType *set2, const TkIntSetType *end2)
{
    if (end1 - set1 != end2 - set2) {
	return false;
    }
    for ( ; set1 < end1; ++set1, ++set2) {
	if (*set1 != *set2) {
	    return false;
	}
    }
    return true;
}


#if !TK_TEXT_DONT_USE_BITFIELDS

unsigned
TkIntSetFindFirstInIntersection(
    const TkIntSet *set,
    const TkBitField *bf)
{
    unsigned size, i;

    assert(set);
    assert(bf);

    if (!TkBitNone(bf)) {
	size = TkIntSetSize(set);

	for (i = 0; i < size; ++i) {
	    TkIntSetType value = TkIntSetAccess(set, i);

	    if (TkBitTest(bf, value)) {
		return value;
	    }
	}
    }

    return TK_SET_NPOS;
}

#endif /* !TK_TEXT_DONT_USE_BITFIELDS */


TkIntSetType *
TkIntSetLowerBound(
    TkIntSetType *first,
    TkIntSetType *last,
    TkIntSetType value)
{
    while (first != last) {
	TkIntSetType *mid = first + (last - first)/2;

	if (*mid < value) {
	    first = mid + 1;
	} else {
	    last = mid;
	}
    }

    return first;
}


TkIntSet *
TkIntSetNew()
{
    TkIntSet *set = malloc(SET_SIZE(0));
    set->end = set->buf;
    set->refCount = 0;
    set->isSetFlag = true;
    DEBUG_ALLOC(tkIntSetCountNew++);
    return set;
}


#if !TK_TEXT_DONT_USE_BITFIELDS

TkIntSet *
TkIntSetFromBits(
    const TkBitField *bf)
{
    unsigned size;
    TkIntSet *set;
    unsigned index = 0, i;

    size = TkBitCount(bf);
    set = malloc(SET_SIZE(NextPowerOf2(size)));
    set->end = set->buf + size;
    set->refCount = 1;
    set->isSetFlag = true;
    DEBUG_ALLOC(tkIntSetCountNew++);

    for (i = TkBitFindFirst(bf); i != TK_BIT_NPOS; i = TkBitFindNext(bf, i)) {
	set->buf[index++] = i;
    }

    return set;
}

#endif /* !TK_TEXT_DONT_USE_BITFIELDS */


void
TkIntSetDestroy(
    TkIntSet **setPtr)
{
    assert(setPtr);

    if (*setPtr) {
	free(*setPtr);
	*setPtr = NULL;
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }
}


TkIntSet *
TkIntSetCopy(
    const TkIntSet *set)
{
    TkIntSet *newSet;
    unsigned size;

    assert(set);

    size = TkIntSetSize(set);
    newSet = malloc(SET_SIZE(NextPowerOf2(size)));
    newSet->end = newSet->buf + size;
    newSet->refCount = 1;
    newSet->isSetFlag = true;
    memcpy(newSet->buf, set->buf, size*sizeof(TkIntSetType));
    DEBUG_ALLOC(tkIntSetCountNew++);
    return newSet;
}


static TkIntSetType *
Join(
    TkIntSetType *dst,
    const TkIntSetType *src, const TkIntSetType *srcEnd,
    const TkIntSetType *add, const TkIntSetType *addEnd)
{
    unsigned size;

    while (src < srcEnd && add < addEnd) {
	if (*src < *add) {
	    *dst++ = *src++;
	} else {
	    if (*src == *add) {
		++src;
	    }
	    *dst++ = *add++;
	}
    }

    if ((size = srcEnd - src) > 0) {
	memcpy(dst, src, size*sizeof(TkIntSetType));
	dst += size;
    } else if ((size = addEnd - add) > 0) {
	memcpy(dst, add, size*sizeof(TkIntSetType));
	dst += size;
    }

    return dst;
}


TkIntSet *
TkIntSetJoin(
    TkIntSet *dst,
    const TkIntSet *src)
{
    TkIntSet *set;
    unsigned capacity1, capacity2;
    unsigned size;

    assert(src);
    assert(dst);
    assert(TkIntSetRefCount(dst) > 0);

    capacity1 = NextPowerOf2(TkIntSetSize(dst) + TkIntSetSize(src));
    set = malloc(SET_SIZE(capacity1));
    set->end = Join(set->buf, dst->buf, dst->end, src->buf, src->end);
    size = set->end - set->buf;
    capacity2 = NextPowerOf2(size);
    assert(capacity2 <= capacity1);
    DEBUG_ALLOC(tkIntSetCountNew++);

    if (capacity2 < capacity1) {
	set = realloc(set, SET_SIZE(capacity2));
	set->end = set->buf + size;
    }

    if (--dst->refCount == 0) {
	free(dst);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    set->refCount = 1;
    set->isSetFlag = true;
    return set;
}


static TkIntSetType *
JoinBits(
    TkIntSetType *dst,
    const TkIntSetType *src, const TkIntSetType *srcEnd,
    const TkBitField *bf)
{
    unsigned size, i;

    i = TkBitFindFirst(bf);

    while (src < srcEnd && i != TK_BIT_NPOS) {
	if (*src < i) {
	    *dst++ = *src++;
	} else {
	    if (*src == i) {
		++src;
	    }
	    *dst++ = i;
	    i = TkBitFindNext(bf, i);
	}
    }

    if ((size = srcEnd - src) > 0) {
	memcpy(dst, src, size*sizeof(TkIntSetType));
	dst += size;
    } else {
	for ( ; i != TK_BIT_NPOS; i = TkBitFindNext(bf, i)) {
	    *dst++ = i;
	}
    }

    return dst;
}


#if !TK_TEXT_DONT_USE_BITFIELDS

TkIntSet *
TkIntSetJoinBits(
    TkIntSet *dst,
    const TkBitField *src)
{
    TkIntSet *set;

    assert(src);
    assert(dst);
    assert(TkIntSetRefCount(dst) > 0);

    if (dst->buf == dst->end) {
	set = TkIntSetNew();
    } else {
	unsigned capacity1, capacity2, size;

	capacity1 = NextPowerOf2(TkIntSetSize(dst) + TkBitSize(src));
	set = malloc(SET_SIZE(capacity1));
	set->end = JoinBits(set->buf, dst->buf, dst->end, src);
	size = set->end - set->buf;
	capacity2 = NextPowerOf2(size);
	assert(capacity2 <= capacity1);
	DEBUG_ALLOC(tkIntSetCountNew++);

	if (capacity2 < capacity1) {
	    set = realloc(set, SET_SIZE(capacity2));
	    set->end = set->buf + size;
	}
    }

    if (--dst->refCount == 0) {
	free(dst);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    set->refCount = 1;
    set->isSetFlag = true;
    return set;
}

#endif /* !TK_TEXT_DONT_USE_BITFIELDS */


static TkIntSetType *
Join2(
    TkIntSetType *dst,
    const TkIntSetType *src, const TkIntSetType *srcEnd,
    const TkIntSetType *set1, const TkIntSetType *set1End,
    const TkIntSetType *set2, const TkIntSetType *set2End)
{
    unsigned size;

    while (src < srcEnd && set1 < set1End && set2 < set2End) {
	if (*set1 < *set2) {
	    if (*src < *set1) {
		*dst++ = *src++;
	    } else {
		if (*src == *set1) {
		    src++;
		}
		*dst++ = *set1++;
	    }
	} else {
	    if (*src < *set2) {
		*dst++ = *src++;
	    } else {
		if (*src == *set2) {
		    src++;
		}
		if (*set1 == *set2)
		    set1++;
		*dst++ = *set2++;
	    }
	}
    }

    if (src == srcEnd) {
	dst = Join(dst, set1, set1End, set2, set2End);
    } else if (set1 < set1End) {
	dst = Join(dst, src, srcEnd, set1, set1End);
    } else if (set2 < set2End) {
	dst = Join(dst, src, srcEnd, set2, set2End);
    } else if ((size = srcEnd - src) > 0) {
	memcpy(dst, src, size*sizeof(TkIntSetType));
	dst += size;
    }

    return dst;
}


TkIntSet *
TkIntSetJoin2(
    TkIntSet *dst,
    const TkIntSet *set1,
    const TkIntSet *set2)
{
    TkIntSet *set;
    unsigned capacity1, capacity2;
    unsigned size;

    assert(dst);
    assert(set1);
    assert(set2);
    assert(TkIntSetRefCount(dst) > 0);

    capacity1 = NextPowerOf2(TkIntSetSize(dst) + TkIntSetSize(set1) + TkIntSetSize(set2));
    set = malloc(SET_SIZE(capacity1));
    set->end = Join2(set->buf, dst->buf, dst->end, set1->buf, set1->end, set2->buf, set2->end);
    size = set->end - set->buf;
    capacity2 = NextPowerOf2(size);
    assert(capacity2 <= capacity1);
    DEBUG_ALLOC(tkIntSetCountNew++);

    if (capacity2 < capacity1) {
	set = realloc(set, SET_SIZE(capacity2));
	set->end = set->buf + size;
    }

    if (--dst->refCount == 0) {
	free(dst);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    set->refCount = 1;
    set->isSetFlag = true;
    return set;
}


static TkIntSetType *
Intersect(
    TkIntSetType *dst,
    const TkIntSetType *src, const TkIntSetType *srcEnd,
    const TkIntSetType *isc, const TkIntSetType *iscEnd)
{
    while (src < srcEnd && isc < iscEnd) {
	if (*src < *isc) {
	    ++src;
	} else {
	    if (*src == *isc) {
		*dst++ = *src++;
	    }
	    ++isc;
	}
    }

    return dst;
}


TkIntSet *
TkIntSetIntersect(
    TkIntSet *dst,
    const TkIntSet *src)
{
    TkIntSet *set;
    unsigned size;
    unsigned capacity1, capacity2;

    assert(src);
    assert(dst);
    assert(TkIntSetRefCount(dst) > 0);

    size = MIN(TkIntSetSize(src), TkIntSetSize(dst));
    capacity1 = NextPowerOf2(size);
    set = malloc(SET_SIZE(capacity1));
    set->end = Intersect(set->buf, dst->buf, dst->end, src->buf, src->end);
    size = set->end - set->buf;
    capacity2 = NextPowerOf2(size);
    assert(capacity2 <= capacity1);
    DEBUG_ALLOC(tkIntSetCountNew++);

    if (capacity2 < capacity1) {
	set = realloc(set, SET_SIZE(capacity2));
	set->end = set->buf + size;
    }

    if (--dst->refCount == 0) {
	free(dst);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    set->refCount = 1;
    set->isSetFlag = true;
    return set;
}


static TkIntSetType *
IntersectBits(
    TkIntSetType *dst,
    const TkIntSetType *src, const TkIntSetType *srcEnd,
    const TkBitField *isc)
{
    unsigned size = TkBitSize(isc);

    for ( ; src < srcEnd; ++src) {
	if (*src >= size) {
	    break;
	}
	if (TkBitTest(isc, *src)) {
	    *dst++ = *src;
	}
    }

    return dst;
}


#if !TK_TEXT_DONT_USE_BITFIELDS

TkIntSet *
TkIntSetIntersectBits(
    TkIntSet *dst,
    const TkBitField *src)
{
    TkIntSet *set;
    unsigned size;
    unsigned capacity1, capacity2;

    assert(src);
    assert(dst);
    assert(TkIntSetRefCount(dst) > 0);

    size = TkBitCount(src);

    size = MIN(TkIntSetSize(dst), TkBitCount(src));
    capacity1 = NextPowerOf2(size);
    set = malloc(SET_SIZE(capacity1));
    set->end = IntersectBits(set->buf, dst->buf, dst->end, src);
    size = set->end - set->buf;
    capacity2 = NextPowerOf2(size);
    assert(capacity2 <= capacity1);
    DEBUG_ALLOC(tkIntSetCountNew++);

    if (capacity2 < capacity1) {
	set = realloc(set, SET_SIZE(capacity2));
	set->end = set->buf + size;
    }

    if (--dst->refCount == 0) {
	free(dst);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    set->refCount = 1;
    set->isSetFlag = true;
    return set;
}

#endif /* !TK_TEXT_DONT_USE_BITFIELDS */


static TkIntSetType *
Remove(
    TkIntSetType *dst,
    const TkIntSetType *src, const TkIntSetType *srcEnd,
    const TkIntSetType *sub, const TkIntSetType *subEnd)
{
    while (src < srcEnd && sub < subEnd) {
	if (*src < *sub) {
	    *dst++ = *src++;
	} else {
	    if (*src == *sub) {
		++src;
	    }
	    ++sub;
	}
    }

    if (src < srcEnd) {
	unsigned size = srcEnd - src;
	memcpy(dst, src, size*sizeof(TkIntSetType));
	dst += size;
    }

    return dst;
}


TkIntSet *
TkIntSetRemove(
    TkIntSet *dst,
    const TkIntSet *src)
{
    TkIntSet *set;
    unsigned capacity1, capacity2;
    unsigned size;

    assert(src);
    assert(dst);
    assert(TkIntSetRefCount(dst) > 0);

    capacity1 = NextPowerOf2(TkIntSetSize(dst));
    set = malloc(SET_SIZE(capacity1));
    set->end = Remove(set->buf, dst->buf, dst->end, src->buf, src->end);
    size = set->end - set->buf;
    capacity2 = NextPowerOf2(size);
    assert(capacity2 <= capacity1);
    DEBUG_ALLOC(tkIntSetCountNew++);

    if (capacity2 < capacity1) {
	set = realloc(set, SET_SIZE(capacity2));
	set->end = set->buf + size;
    }

    if (--dst->refCount == 0) {
	free(dst);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    set->refCount = 1;
    set->isSetFlag = true;
    return set;
}


static TkIntSetType *
RemoveBits(
    TkIntSetType *dst,
    const TkIntSetType *src, const TkIntSetType *srcEnd,
    const TkBitField *sub)
{
    unsigned size = TkBitSize(sub);

    for ( ; src < srcEnd; ++src) {
	if (*src >= size) {
	    break;
	}
	if (!TkBitTest(sub, *src)) {
	    *dst++ = *src;
	}
    }

    if ((size = srcEnd - src) > 0) {
	memcpy(dst, src, size*sizeof(TkIntSetType));
	dst += size;
    }

    return dst;
}


#if !TK_TEXT_DONT_USE_BITFIELDS

TkIntSet *
TkIntSetRemoveBits(
    TkIntSet *dst,
    const TkBitField *src)
{
    TkIntSet *set;
    unsigned capacity1, capacity2;
    unsigned size;

    assert(src);
    assert(dst);
    assert(TkIntSetRefCount(dst) > 0);

    capacity1 = NextPowerOf2(TkIntSetSize(dst));
    set = malloc(SET_SIZE(capacity1));
    set->end = RemoveBits(set->buf, dst->buf, dst->end, src);
    size = set->end - set->buf;
    capacity2 = NextPowerOf2(size);
    assert(capacity2 <= capacity1);
    DEBUG_ALLOC(tkIntSetCountNew++);

    if (capacity2 < capacity1) {
	set = realloc(set, SET_SIZE(capacity2));
	set->end = set->buf + size;
    }

    if (--dst->refCount == 0) {
	free(dst);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    set->refCount = 1;
    set->isSetFlag = true;
    return set;
}

#endif /* !TK_TEXT_DONT_USE_BITFIELDS */


static TkIntSetType *
ComplementTo(
    TkIntSetType *dst,
    const TkIntSetType *sub, const TkIntSetType *subEnd,
    const TkIntSetType *src, const TkIntSetType *srcEnd)
{
    return Remove(dst, src, srcEnd, sub, subEnd);
}


TkIntSet *
TkIntSetComplementTo(
    TkIntSet *dst,
    const TkIntSet *src)
{
    TkIntSet *set;
    unsigned capacity1, capacity2;
    unsigned size;

    assert(src);
    assert(dst);
    assert(TkIntSetRefCount(dst) > 0);

    capacity1 = NextPowerOf2(TkIntSetSize(src));
    set = malloc(SET_SIZE(capacity1));
    set->end = ComplementTo(set->buf, dst->buf, dst->end, src->buf, src->end);
    size = set->end - set->buf;
    capacity2 = NextPowerOf2(size);
    assert(capacity2 <= capacity1);
    DEBUG_ALLOC(tkIntSetCountNew++);

    if (capacity2 < capacity1) {
	set = realloc(set, SET_SIZE(capacity2));
	set->end = set->buf + size;
    }

    if (--dst->refCount == 0) {
	free(dst);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    set->refCount = 1;
    set->isSetFlag = true;
    return set;
}


static TkIntSetType *
ComplementToBits(
    TkIntSetType *dst,
    const TkIntSetType *sub, const TkIntSetType *subEnd,
    const TkBitField *src)
{
    unsigned i = TkBitFindFirst(src);

    /* dst := src - sub */

    while (sub < subEnd && i != TK_BIT_NPOS) {
	if (*sub < i) {
	    ++sub;
	} else {
	    if (i < *sub) {
		*dst++ = i;
	    } else {
		++sub;
	    }
	    i = TkBitFindNext(src, i);
	}
    }
    for ( ; i != TK_BIT_NPOS; i = TkBitFindNext(src, i)) {
	*dst++ = i;
    }

    return dst;
}


#if !TK_TEXT_DONT_USE_BITFIELDS

TkIntSet *
TkIntSetComplementToBits(
    TkIntSet *dst,
    const TkBitField *src)
{
    TkIntSet *set;
    unsigned capacity1, capacity2;
    unsigned size;

    assert(src);
    assert(dst);
    assert(TkIntSetRefCount(dst) > 0);

    capacity1 = NextPowerOf2(TkBitSize(src));
    set = malloc(SET_SIZE(capacity1));
    set->end = ComplementToBits(set->buf, dst->buf, dst->end, src);
    size = set->end - set->buf;
    capacity2 = NextPowerOf2(size);
    assert(capacity2 <= capacity1);
    DEBUG_ALLOC(tkIntSetCountNew++);

    if (capacity2 < capacity1) {
	set = realloc(set, SET_SIZE(capacity2));
	set->end = set->buf + size;
    }

    if (--dst->refCount == 0) {
	free(dst);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    set->refCount = 1;
    set->isSetFlag = true;
    return set;
}

#endif /* !TK_TEXT_DONT_USE_BITFIELDS */


static TkIntSetType *
JoinComplementTo(
    TkIntSetType *dst,
    const TkIntSetType *src, const TkIntSetType *srcEnd,
    const TkIntSetType *set1, const TkIntSetType *set1End,
    const TkIntSetType *set2, const TkIntSetType *set2End)
{
    /* dst = src + (set2 - set1) */

    while (src < srcEnd && set1 < set1End && set2 < set2End) {
	if (*set2 < *set1) {
	    if (*src < *set2) {
		*dst++ = *src++;
	    } else if (*src == *set2) {
		*dst++ = *src++;
		set2++;
	    } else {
		if (*src == *set2) {
		    ++src;
		}
		*dst++ = *set2++;
	    }
	} else if (*src < *set1) {
	    *dst++ = *src++;
	} else {
	    if (*set2 == *set1) {
		set2++;
	    }
	    if (*src == *set1) {
		*dst++ = *src++;
	    }
	    set1++;
	}
    }

    if (src == srcEnd) {
	dst = ComplementTo(dst, set1, set1End, set2, set2End);
    } else if (set2 < set2End) {
	dst = Join(dst, src, srcEnd, set2, set2End);
    } else {
	unsigned size = srcEnd - src;
	memcpy(dst, src, size*sizeof(TkIntSetType));
	dst += size;
    }

    return dst;
}


TkIntSet *
TkIntSetJoinComplementTo(
    TkIntSet *dst,
    const TkIntSet *set1,
    const TkIntSet *set2)
{
    TkIntSet *set;
    unsigned capacity1, capacity2;
    unsigned size;

    assert(dst);
    assert(set1);
    assert(set2);
    assert(TkIntSetRefCount(dst) > 0);

    capacity1 = NextPowerOf2(TkIntSetSize(dst) + TkIntSetSize(set1));
    set = malloc(SET_SIZE(capacity1));
    set->end = JoinComplementTo(
	    set->buf, dst->buf, dst->end, set1->buf, set1->end, set2->buf, set2->end);
    size = set->end - set->buf;
    capacity2 = NextPowerOf2(size);
    assert(capacity2 <= capacity1);
    DEBUG_ALLOC(tkIntSetCountNew++);

    if (capacity2 < capacity1) {
	set = realloc(set, SET_SIZE(capacity2));
	set->end = set->buf + size;
    }

    if (--dst->refCount == 0) {
	free(dst);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    set->refCount = 1;
    set->isSetFlag = true;
    return set;
}


static TkIntSetType *
JoinNonIntersection(
    TkIntSetType *dst,
    const TkIntSetType *src, const TkIntSetType *srcEnd,
    const TkIntSetType *set1, const TkIntSetType *set1End,
    const TkIntSetType *set2, const TkIntSetType *set2End)
{
    unsigned size;

    /* dst += src + (set1 - set2) + (set2 - set1) */

    while (src != srcEnd && set1 != set1End && set2 != set2End) {
	if (*set1 < *set2) {
	    /* dst += src + set1 */
	    if (*set1 < *src) {
		*dst++ = *set1++;
	    } else {
		if (*src == *set1) {
		    ++set1;
		}
		*dst++ = *src++;
	    }
	} else if (*set2 < *set1) {
	    /* dst += src + set2 */
	    if (*set2 < *src) {
		*dst++ = *set2++;
	    } else {
		if (*src == *set2) {
		    ++set2;
		}
		*dst++ = *src++;
	    }
	} else {
	    ++set1;
	    ++set2;
	}
    }

    if (src == srcEnd) {
	/* dst += (set1 - set2) + (set2 - set1) */

	while (set1 != set1End && set2 != set2End) {
	    if (*set1 < *set2) {
		*dst++ = *set1++;
	    } else if (*set2 < *set1) {
		*dst++ = *set2++;
	    } else {
		++set1;
		++set2;
	    }
	}
	if (set1 == set1End) {
	    set1 = set2;
	    set1End = set2End;
	}

	/* dst += set1 */

	if ((size = set1End - set1)) {
	    memcpy(dst, set1, size*sizeof(TkIntSetType));
	    dst += size;
	}
    } else {
	if (set1 == set1End) {
	    set1 = set2;
	    set1End = set2End;
	}

	/* dst += src + set1 */

	dst = Join(dst, src, srcEnd, set1, set1End);
    }

    return dst;
}


TkIntSet *
TkIntSetJoinNonIntersection(
    TkIntSet *dst,
    const TkIntSet *set1,
    const TkIntSet *set2)
{
    TkIntSet *set;
    unsigned capacity1, capacity2;
    unsigned size;

    assert(dst);
    assert(set1);
    assert(set2);
    assert(TkIntSetRefCount(dst) > 0);

    capacity1 = NextPowerOf2(TkIntSetSize(dst) + TkIntSetSize(set1) + TkIntSetSize(set2));
    set = malloc(SET_SIZE(capacity1));
    set->end = JoinNonIntersection(
	    set->buf, dst->buf, dst->end, set1->buf, set1->end, set2->buf, set2->end);
    size = set->end - set->buf;
    capacity2 = NextPowerOf2(size);
    assert(capacity2 <= capacity1);
    DEBUG_ALLOC(tkIntSetCountNew++);

    if (capacity2 < capacity1) {
	set = realloc(set, SET_SIZE(capacity2));
	set->end = set->buf + size;
    }

    if (--dst->refCount == 0) {
	free(dst);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    set->refCount = 1;
    set->isSetFlag = true;
    return set;
}


TkIntSet *
TkIntSetJoin2ComplementToIntersection(
    TkIntSet *dst,
    const TkIntSet *add,
    const TkIntSet *set1,
    const TkIntSet *set2)
{
    TkIntSet *set;
    const TkIntSetType *set1P;
    const TkIntSetType *set2P;
    const TkIntSetType *set1End;
    const TkIntSetType *set2End;
    TkIntSetType *res1, *res2, *res3, *res1End, *res2End, *res3End;
    TkIntSetType buffer[512];
    unsigned capacity1, capacity2;
    unsigned size, size1, size2;

    assert(dst);
    assert(add);
    assert(set1);
    assert(set2);
    assert(TkIntSetRefCount(dst) > 0);

    set1P = set1->buf;
    set2P = set2->buf;
    set1End = set1->end;
    set2End = set2->end;

    size1 = TkIntSetSize(set1) + TkIntSetSize(set2);
    size2 = MIN(TkIntSetSize(set1), TkIntSetSize(set2));
    size = size1 + 2*size2;
    res1 = size <= sizeof(buffer)/sizeof(buffer[0]) ? buffer : malloc(size*sizeof(TkIntSetType));
    res2 = res1 + size1;
    res3 = res2 + size2;

    res1End = Join(res1, set1P, set1End, set2P, set2End);
    res2End = Intersect(res2, set1P, set1End, set2P, set2End);
    res3End = ComplementTo(res3, res1, res1End, res2, res2End);

    capacity1 = NextPowerOf2(TkIntSetSize(dst) + TkIntSetSize(add) + (res3End - res3));
    set = malloc(SET_SIZE(capacity1));
    set->end = Join2(set->buf, dst->buf, dst->end, add->buf, add->end, res3, res3End);
    size = set->end - set->buf;
    capacity2 = NextPowerOf2(size);
    assert(capacity2 <= capacity1);
    DEBUG_ALLOC(tkIntSetCountNew++);

    if (capacity2 < capacity1) {
	set = realloc(set, SET_SIZE(capacity2));
	set->end = set->buf + size;
    }

    if (--dst->refCount == 0) {
	free(dst);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    if (res1 != buffer) {
	free(res1);
    }
    set->refCount = 1;
    set->isSetFlag = true;
    return set;
}


TkIntSet *
TkIntSetJoinOfDifferences(
    TkIntSet *dst,
    const TkIntSet *set1,
    const TkIntSet *set2)
{
    TkIntSet *set;
    TkIntSetType *buf1, *buf2, *end1, *end2;
    unsigned capacity1, capacity2;
    unsigned size;

    assert(dst);
    assert(set1);
    assert(set2);
    assert(TkIntSetRefCount(dst) > 0);

    capacity2 = TkIntSetSize(dst) + TkIntSetSize(set1);
    capacity1 = NextPowerOf2(2*capacity2);
    set = malloc(SET_SIZE(capacity1));
    buf1 = set->buf + capacity2;
    buf2 = buf1 + TkIntSetSize(dst);
    end1 = Remove(buf1, dst->buf, dst->end, set1->buf, set1->end);
    end2 = Remove(buf2, set1->buf, set1->end, set2->buf, set2->end);
    set->end = Join(set->buf, buf1, end1, buf2, end2);
    size = set->end - set->buf;
    capacity2 = NextPowerOf2(size);
    assert(capacity2 <= capacity1);
    DEBUG_ALLOC(tkIntSetCountNew++);

    if (capacity2 < capacity1) {
	set = realloc(set, SET_SIZE(capacity2));
	set->end = set->buf + size;
    }

    if (--dst->refCount == 0) {
	free(dst);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    set->refCount = 1;
    set->isSetFlag = true;
    return set;
}


bool
TkIntSetDisjunctive__(
    const TkIntSetType *set1, const TkIntSetType *end1,
    const TkIntSetType *set2, const TkIntSetType *end2)
{
    while (set1 != end1 && set2 != end2) {
	if (*set1 == *set2) {
	    return false;
	}
	if (*set1 < *set2) {
	    ++set1;
	} else {
	    ++set2;
	}
    }

    return true;
}


bool
TkIntSetContains__(
    const TkIntSetType *set1, const TkIntSetType *end1,
    const TkIntSetType *set2, const TkIntSetType *end2)
{
    /*
     * a in set1, not in set2 -> skip
     * a in set1, and in set2 -> skip
     * a in set2, not in set1 -> false
     */

    if (end1 - set1 < end2 - set2) {
	return false;
    }

    while (set1 != end1 && set2 != end2) {
	if (*set2 < *set1) {
	    return false;
	} else if (*set1 == *set2) {
	    ++set2;
	}
	++set1;
    }

    return set2 == end2;
}


bool
TkIntSetIsContainedBits(
    const TkIntSet *set,
    const TkBitField *bf)
{
    unsigned setSize, bitSize, i;

    assert(bf);
    assert(set);

    setSize = TkIntSetSize(set);
    bitSize = TkBitSize(bf);

    for (i = 0; i < setSize; ++i) {
	TkIntSetType value = set->buf[i];
	if (value >= bitSize || !TkBitTest(bf, value)) {
	    return false;
	}
    }

    return true;
}


#if !TK_TEXT_DONT_USE_BITFIELDS

bool
TkIntSetIntersectionIsEqual(
    const TkIntSet *set1,
    const TkIntSet *set2,
    const TkBitField *del)
{
    const TkIntSetType *s1;
    const TkIntSetType *e1;
    const TkIntSetType *s2;
    const TkIntSetType *e2;

    assert(set1);
    assert(set2);
    assert(del);
    assert(TkIntSetMax(set1) < TkBitSize(del));
    assert(TkIntSetMax(set2) < TkBitSize(del));

    if (set1 == set2) {
	return true;
    }

    s1 = set1->buf; e1 = set1->end;
    s2 = set2->buf; e2 = set2->end;

    while (s1 != e1 && s2 != e2) {
	if (*s1 == *s2) {
	    ++s1;
	    ++s2;
	} else if (*s1 < *s2) {
	    if (!TkBitTest(del, *s1)) {
		return false;
	    }
	    ++s1;
	} else { /* if (*s2 < *s1) */
	    if (!TkBitTest(del, *s2)) {
		return false;
	    }
	    ++s2;
	}
    }
    for ( ; s1 != e1; ++s1) {
	if (!TkBitTest(del, *s1)) {
	    return false;
	}
    }
    for ( ; s2 != e2; ++s2) {
	if (!TkBitTest(del, *s2)) {
	    return false;
	}
    }

    return true;
}


bool
TkIntSetIntersectionIsEqualBits(
    const TkIntSet *set,
    const TkBitField *bf,
    const TkBitField *del)
{
    TkBitField *cp = TkBitCopy(del, -1);
    bool test;

    assert(set);
    assert(bf);
    assert(del);
    assert(TkIntSetMax(set) < TkBitSize(del));
    assert(TkBitSize(bf) <= TkBitSize(del));

    TkBitIntersect(cp, bf);
    test = TkIntSetIsEqualBits(set, cp);
    TkBitDestroy(&cp);
    return test;
}

#endif /* !TK_TEXT_DONT_USE_BITFIELDS */


static TkIntSet *
Add(
    TkIntSet *set,
    TkIntSetType *pos,
    unsigned n)
{
    unsigned size = set->end - set->buf;

    if (IsPowerOf2(size)) {
	TkIntSet *newSet = malloc(SET_SIZE(MAX(2*size, 1)));
	unsigned offs = pos - set->buf;

	assert(offs <= size);
	memcpy(newSet->buf, set->buf, offs*sizeof(TkIntSetType));
	memcpy(newSet->buf + offs + 1, pos, (size - offs)*sizeof(TkIntSetType));
	newSet->end = newSet->buf + size + 1;
	newSet->refCount = 1;
	newSet->isSetFlag = true;
	DEBUG_ALLOC(tkIntSetCountNew++);

	if (--set->refCount == 0) {
	    free(set);
	    DEBUG_ALLOC(tkIntSetCountDestroy++);
	}

	set = newSet;
	pos = set->buf + offs;
    } else {
	memmove(pos + 1, pos, (set->end - pos)*sizeof(TkIntSetType));
	set->end += 1;
    }

    *pos = n;
    return set;
}


TkIntSet *
TkIntSetAdd(
    TkIntSet *set,
    unsigned n)
{
    TkIntSetType *pos;

    assert(set);
    assert(TkIntSetRefCount(set) > 0);

    pos = TkIntSetLowerBound(set->buf, set->end, n);

    if (pos < set->end && *pos == n) {
	return set;
    }

    return Add(set, pos, n);
}


static TkIntSet *
Erase(
    TkIntSet *set,
    TkIntSetType *pos,
    unsigned n)
{
    unsigned size = set->end - set->buf - 1;

    if (IsPowerOf2(size)) {
	TkIntSet *newSet = malloc(SET_SIZE(size));
	unsigned offs = pos - set->buf;

	memcpy(newSet->buf, set->buf, offs*sizeof(TkIntSetType));
	memcpy(newSet->buf + offs, pos + 1, (size - offs)*sizeof(TkIntSetType));
	newSet->end = newSet->buf + size;
	newSet->refCount = 1;
	newSet->isSetFlag = true;
	DEBUG_ALLOC(tkIntSetCountNew++);

	if (--set->refCount == 0) {
	    free(set);
	    DEBUG_ALLOC(tkIntSetCountDestroy++);
	}

	set = newSet;
    } else {
	memmove(pos, pos + 1, (set->end - pos - 1)*sizeof(TkIntSetType));
	set->end -= 1;
    }

    return set;
}


TkIntSet *
TkIntSetErase(
    TkIntSet *set,
    unsigned n)
{
    TkIntSetType *pos;

    assert(set);
    assert(TkIntSetRefCount(set) > 0);

    pos = TkIntSetLowerBound(set->buf, set->end, n);

    if (pos == set->end || *pos != n) {
	return set;
    }

    return Erase(set, pos, n);
}


TkIntSet *
TkIntSetTestAndSet(
    TkIntSet *set,
    unsigned n)
{
    TkIntSetType *pos;

    assert(set);
    assert(TkIntSetRefCount(set) > 0);

    pos = TkIntSetLowerBound(set->buf, set->end, n);

    if (pos < set->end && *pos == n) {
	return NULL;
    }

    return Add(set, pos, n);
}


TkIntSet *
TkIntSetTestAndUnset(
    TkIntSet *set,
    unsigned n)
{
    TkIntSetType *pos;

    assert(set);
    assert(TkIntSetRefCount(set) > 0);

    pos = TkIntSetLowerBound(set->buf, set->end, n);

    if (pos == set->end || *pos != n) {
	return NULL;
    }

    return Erase(set, pos, n);
}


TkIntSet *
TkIntSetClear(
    TkIntSet *set)
{
    TkIntSet *newSet;

    assert(set);
    assert(TkIntSetRefCount(set) > 0);

    if (set->buf == set->end) {
	return set;
    }

    newSet = malloc(SET_SIZE(0));
    newSet->end = newSet->buf;
    newSet->refCount = 1;
    newSet->isSetFlag = true;
    DEBUG_ALLOC(tkIntSetCountNew++);

    if (--set->refCount == 0) {
	free(set);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    return newSet;
}


#if !TK_TEXT_DONT_USE_BITFIELDS

bool
TkIntSetIsEqualBits(
    const TkIntSet *set,
    const TkBitField *bf)
{
    unsigned sizeSet, sizeBf, i;

    assert(set);
    assert(bf);

    sizeSet = TkIntSetSize(set);

    if (sizeSet != TkBitCount(bf)) {
	return false;
    }

    sizeBf = TkBitSize(bf);

    for (i = 0; i < sizeSet; ++i) {
	TkIntSetType value = set->buf[i];

	if (value >= sizeBf || !TkBitTest(bf, value)) {
	    return false;
	}
    }

    return true;
}


bool
TkIntSetContainsBits(
    const TkIntSet *set,
    const TkBitField *bf)
{
    unsigned sizeSet, sizeBf, i;
    unsigned count = 0;

    assert(set);
    assert(bf);

    sizeSet = TkIntSetSize(set);
    sizeBf = TkBitSize(bf);

    for (i = 0; i < sizeSet; ++i) {
	TkIntSetType value = set->buf[i];

	if (value >= sizeBf) {
	    break;
	}

	if (TkBitTest(bf, value)) {
	    count += 1;
	}
    }

    return count == TkBitCount(bf);
}


bool
TkIntSetDisjunctiveBits(
    const TkIntSet *set,
    const TkBitField *bf)
{
    unsigned sizeSet, sizeBf, i;

    assert(set);
    assert(bf);

    sizeSet = TkIntSetSize(set);
    sizeBf = TkBitSize(bf);

    for (i = 0; i < sizeSet; ++i) {
	TkIntSetType value = set->buf[i];

	if (value >= sizeBf) {
	    return true;
	}
	if (TkBitTest(bf, value)) {
	    return false;
	}
    }

    return true;
}

#endif /* !TK_TEXT_DONT_USE_BITFIELDS */


#if !NDEBUG

# include <stdio.h>

void
TkIntSetPrint(
    const TkIntSet *set)
{
    unsigned i, n;
    const char *comma = "";

    assert(set);

    n = TkIntSetSize(set);
    printf("%d:{ ", n);
    for (i = 0; i < n; ++i) {
	printf("%s%d", comma, set->buf[i]);
	comma = ", ";
    }
    printf(" }\n");
}

#endif /* !NDEBUG */

#if TK_UNUSED_INTSET_FUNCTIONS

/*
 * These functions are not needed anymore, but shouldn't be removed, because sometimes
 * any of these functions might be useful.
 */

static TkIntSetType *
InnerJoinDifference(
    TkIntSetType *dst,
    const TkIntSetType *src, const TkIntSetType *srcEnd,
    const TkIntSetType *add, const TkIntSetType *addEnd,
    const TkIntSetType *sub, const TkIntSetType *subEnd)
{
    /* dst = (src & add) + (add - sub) */

    while (src != srcEnd && add != addEnd) {
	if (*src < *add) {
	    ++src;
	} else {
	    if (*src == *add) {
		*dst++ = *add;
		++src;
	    } else {
		for ( ; sub != subEnd && *sub < *add; ++sub) {
		    /* empty loop body */
		}
		if (sub == subEnd) {
		    break;
		}
		if (*add != *sub) {
		    *dst++ = *add;
		}
	    }
	    ++add;
	}
    }

    if (sub == subEnd) {
	/* dst += add */
	unsigned size = addEnd - add;
	memcpy(dst, add, size*sizeof(TkIntSetType));
	dst += size;
    } else if (src == srcEnd) {
	/* dst += add - sub */
	dst = Remove(dst, add, addEnd, sub, subEnd);
    } else { /* if (add == addEnd) */
	/* dst += nil */
    }

    return dst;
}


TkIntSet *
TkIntSetInnerJoinDifference(
    TkIntSet *dst,
    const TkIntSet *add,
    const TkIntSet *sub)
{
    TkIntSet *set;
    unsigned capacity1, capacity2;
    unsigned size;

    assert(dst);
    assert(add);
    assert(sub);
    assert(TkIntSetRefCount(dst) > 0);

    capacity1 = NextPowerOf2(TkIntSetSize(dst) + TkIntSetSize(add));
    set = malloc(SET_SIZE(capacity1));
    set->end = InnerJoinDifference(set->buf, dst->buf, dst->end, add->buf, add->end, sub->buf, sub->end);
    size = set->end - set->buf;
    capacity2 = NextPowerOf2(size);
    assert(capacity2 <= capacity1);
    DEBUG_ALLOC(tkIntSetCountNew++);

    if (capacity2 < capacity1) {
	set = realloc(set, SET_SIZE(capacity2));
	set->end = set->buf + size;
    }

    if (--dst->refCount == 0) {
	free(dst);
	DEBUG_ALLOC(tkIntSetCountDestroy++);
    }

    set->refCount = 1;
    set->isSetFlag = true;
    return set;
}


bool
TkIntSetInnerJoinDifferenceIsEmpty(
    const TkIntSet *set,
    const TkIntSet *add,
    const TkIntSet *sub)
{
    const TkIntSetType *setP, *setEnd;
    const TkIntSetType *addP, *addEnd;
    const TkIntSetType *subP, *subEnd;

    assert(set);
    assert(add);
    assert(sub);

    /* (set & add) + (add - sub) == nil */

    if (add->buf == add->end) {
	/* nil */
	return true;
    }

    if (add == set) {
	/* add == nil */
	return TkIntSetIsEmpty(add);
    }

    setP = set->buf; setEnd = set->end;
    addP = add->buf; addEnd = add->end;

    /* (set & add) == nil */

    while (setP != setEnd && addP < addEnd) {
	if (*setP == *addP) {
	    return false;
	} else if (*setP < *addP) {
	    ++setP;
	} else {
	    ++addP;
	}
    }

    /* (add - sub) == nil */

    addP = add->buf; addEnd = add->end;
    subP = sub->buf; subEnd = sub->end;

    while (addP != addEnd && subP != subEnd) {
	if (*addP < *subP) {
	    return false;
	} else if (*addP == *subP) {
	    ++addP;
	}
	++subP;
    }

    return addP == addEnd;
}


static bool
DifferenceIsEmpty(
    const TkIntSetType *set, const TkIntSetType *setEnd,
    const TkIntSetType *sub, const TkIntSetType *subEnd)
{
    while (set != setEnd && sub != subEnd) {
	if (*set < *sub) {
	    return false;
	} else {
	    if (*set == *sub) {
		++set;
	    }
	    ++sub;
	}
    }

    return set == setEnd;
}


bool
TkIntSetIsEqualToDifference(
    const TkIntSet *set1,
    const TkIntSet *set2,
    const TkIntSet *sub2)
{
    const TkIntSetType *set1P, *set1End;
    const TkIntSetType *set2P, *set2End;
    const TkIntSetType *sub2P, *sub2End;

    assert(set1);
    assert(set2);
    assert(sub2);

    if (set2->buf == set2->end) {
	return set1->buf == set1->end;
    }

    set1P = set1->buf; set1End = set1->end;
    set2P = set2->buf; set2End = set2->end;
    sub2P = sub2->buf; sub2End = sub2->end;

    if (set1P == set1End) {
	return DifferenceIsEmpty(set2P, set2End, sub2P, sub2End);
    }

    /* set1 == set2 - sub2 */

    while (set1P != set1End && set2P != set2End) {
	if (*set1P < *set2P) {
	    return false;
	}
	for ( ; sub2P != sub2End && *sub2P < *set2P; ++sub2P) {
	    /* empty loop body */
	}
	if (sub2P == sub2End) {
	    break;
	}
	if (*set1P == *set2P) {
	    if (*set2P == *sub2P) {
		return false;
	    }
	    ++set1P;
	} else {
	    if (*set2P != *sub2P) {
		return false;
	    }
	}
	++set2P;
    }

    if (set2P == set2End) {
	/* set1 == nil */
	return set1P == set1End;
    }

    if (sub2P == sub2End) {
	/* set1 == set2 */
	return TestIfEqual(set1P, set1End, set2P, set2End);
    }

    assert(set1P == set1End);
    /* set2 - sub2 == nil */

    return DifferenceIsEmpty(set2P, set2End, sub2P, sub2End);
}


bool
TkIntSetIsEqualToInnerJoin(
    const TkIntSet *set1,
    const TkIntSet *set2,
    const TkIntSet *add2)
{
    const TkIntSetType *set1P, *set1End;
    const TkIntSetType *set2P, *set2End;
    const TkIntSetType *add2P, *add2End;

    assert(set1);
    assert(set2);
    assert(add2);

    if (set1 == set2) {
	/* set1 == (set1 + (add2 & set1)) */
	return true;
    }

    set1P = set1->buf; set1End = set1->end;
    set2P = set2->buf; set2End = set2->end;

    if (set2P == set2End) {
	/* set1 == nil */
	return set1P == set1End;
    }

    if (set2 == add2) {
	/* set1 == set2 */
	return TestIfEqual(set1P, set1End, set2P, set2End);
    }

    add2P = add2->buf; add2End = add2->end;

    /* set1 == (set2 + (add2 & set2)) */

    while (set1P != set1End && set2P != set2End && add2P != add2End) {
	if (*set2P < *set1P) {
	    return false;
	} else if (*set1P == *set2P) {
	    ++set1P;
	    ++set2P;
	/* now: *set1P < *set2P */
	} else if (*add2P < *set2P) {
	    ++add2P;
	} else if (*set2P < *add2P) {
	    ++set2P;
	} else {
	    return false;
	}
    }

    if (add2P == add2End) {
	/* set1 == set2 */
	return TestIfEqual(set1P, set1End, set2P, set2End);
    }

    if (set1P == set1End) {
	/* set2 == nil */
	return set2P == set2End;
    }

    /* set2P == set2End: set1 == nil */
    return set1P == set1End;
}


static bool
EqualToJoin(
    const TkIntSetType *src, const TkIntSetType *send,
    const TkIntSetType *set1, const TkIntSetType *end1,
    const TkIntSetType *set2, const TkIntSetType *end2)
{
    /* src == set1 + set2 */

    assert(src != send);

    while (set1 != end1 && set2 != end2) {
	if (*src == *set1) {
	    if (*src == *set2) {
		++set2;
	    }
	    ++set1;
	} else if (*src == *set2) {
	    ++set2;
	} else {
	    return false;
	}
	if (++src == send) {
	    return set1 == end1 && set2 == end2;
	}
    }

    if (set1 == end1) {
	set1 = set2;
	end1 = end2;
    }

    return TestIfEqual(src, send, set1, end1);
}


bool
TkIntSetIsEqualToInnerJoinDifference(
    const TkIntSet *set1,
    const TkIntSet *set2,
    const TkIntSet *add2,
    const TkIntSet *sub2)
{
    TkIntSetType buf1[100];
    TkIntSetType buf2[100];
    TkIntSetType *inscBuf; /* set2 & add2 */
    TkIntSetType *diffBuf; /* add2 - sub2 */
    TkIntSetType *inscP, *inscEnd;
    TkIntSetType *diffP, *diffEnd;
    unsigned inscSize;
    unsigned diffSize;
    bool isEqual;

    assert(set1);
    assert(set2);
    assert(add2);
    assert(sub2);

    /* set1 == (set2 & add2) + (add2 - sub2) */

    if (add2->buf == add2->end) {
	/* set1 == nil */
	return TkIntSetIsEmpty(set1);
    }

    if (sub2->buf == sub2->end) {
	/* set1 == (set2 & add2) + add2 */
	return TkIntSetIsEqualToInnerJoin(set1, add2, set2);
    }

    if (set1->buf == set1->end) {
	/* (set2 & add2) + (add2 - sub2) == nil */
	return TkIntSetDisjunctive(set2, add2)
		&& DifferenceIsEmpty(add2->buf, add2->end, sub2->buf, sub2->end);
    }

    diffSize = TkIntSetSize(add2);
    inscSize = MIN(TkIntSetSize(set2), diffSize);
    inscBuf = inscSize <= sizeof(buf1)/sizeof(buf1[0]) ? buf1 : malloc(inscSize*sizeof(buf1[0]));
    inscEnd = Intersect(inscP = inscBuf, set2->buf, set2->end, add2->buf, add2->end);

    if (inscP == inscEnd) {
	/* set1 == (add2 - sub2) */
	isEqual = TkIntSetIsEqualToDifference(set1, add2, sub2);
    } else {
	diffBuf = diffSize <= sizeof(buf2)/sizeof(buf2[0]) ? buf2 : malloc(diffSize*sizeof(buf2[0]));
	diffEnd = Remove(diffP = diffBuf, add2->buf, add2->end, sub2->buf, sub2->end);

	if (diffP == diffEnd) {
	    /* set1 == inscP */
	    isEqual = TestIfEqual(set1->buf, set1->end, inscP, inscEnd);
	} else {
	    /* set1 == inscP + diffP */
	    isEqual = EqualToJoin(set1->buf, set1->end, inscP, inscEnd, diffP, diffEnd);
	}

	if (diffBuf != buf2) { free(diffBuf); }
    }

    if (inscBuf != buf1) { free(inscBuf); }

    return isEqual;
}


static bool
InnerJoinDifferenceIsEqual(
    const TkIntSetType *set, const TkIntSetType *setEnd,
    const TkIntSetType *add, const TkIntSetType *addEnd,
    const TkIntSetType *sub, const TkIntSetType *subEnd)
{
    /*
     * (add - sub) == (set & add) + (add - sub)
     *
     * This is equivalent to:
     * (add - sub) & add == (set + (add - sub)) & add
     *
     * This means we have to show:
     * For any x in add: x in (add - sub) <=> x in (set + (add - sub))
     *
     * So it's sufficient to show:
     * For any x in add: x in sub => x not in set
     * For any x in add: x in set => x not in sub
     *
     * But this is equivalent to:
     * (sub & add) & (set & add) == nil
     */

    if (add != addEnd) {
	while (set != setEnd && sub != subEnd) {
	    if (*set == *sub) {
		while (*add < *set) {
		    if (++add == addEnd) {
			return true;
		    }
		}
		if (*add == *set) {
		    return false;
		}
		++set;
		++sub;
	    } else if (*set < *sub) {
		++set;
	    } else {
		++sub;
	    }
	}
    }

    return true;
}


bool
TkIntSetInnerJoinDifferenceIsEqual(
    const TkIntSet *set1,
    const TkIntSet *set2,
    const TkIntSet *add,
    const TkIntSet *sub)
{
    const TkIntSetType *set1P, *set1End;
    const TkIntSetType *set2P, *set2End;
    const TkIntSetType *addP, *addEnd;
    const TkIntSetType *subP, *subEnd;

    if (add->buf == add->end) {
	return true;
    }

    set1P = set1->buf; set1End = set1->end;
    set2P = set2->buf; set2End = set2->end;
    addP = add->buf; addEnd = add->end;
    subP = sub->buf; subEnd = sub->end;

    if (set1P == set1End) {
	return InnerJoinDifferenceIsEqual(set2P, set2End, addP, addEnd, subP, subEnd);
    }
    if (set2P == set2End) {
	return InnerJoinDifferenceIsEqual(set1P, set1End, addP, addEnd, subP, subEnd);
    }

    /*
     * (set1 & add) + (add - sub) == (set2 & add) + (add - sub)
     *
     * This is equivalent to:
     * (set1 + (add - sub)) & add == (set2 + (add - sub)) & add
     *
     * This means we have to show:
     * For any x in add: x in (set1 + (add - sub)) <=> x in (set2 + (add - sub))
     *
     * x in (add & sub): Proof: x in set1 <=> x in set2.
     * x in (add - sub): Nothing to proof.
     */

    while (addP != addEnd && subP != subEnd) {
	if (*addP < *subP) {
	    ++addP;
	} else {
	    if (*addP == *subP) {
		/* x in (add & sub): Proof: x in set1 <=> x in set2. */
		for ( ; set1P != set1End && *set1P < *addP; ++set1P) {
		    /* empty loop body */
		}
		if (set1P == set1End) {
		    /* (add - sub) == (set2 & add) + (add - sub) */
		    return InnerJoinDifferenceIsEqual(set2P, set2End, addP, addEnd, subP, subEnd);
		}
		for ( ; set2P != set2End && *set2P < *addP; ++set2P) {
		    /* empty loop body */
		}
		if (set2P == set2End) {
		    /* (add - sub) == (set1 & add) + (add - sub) */
		    return InnerJoinDifferenceIsEqual(set1P, set1End, addP, addEnd, subP, subEnd);
		}
		if (*addP == *set1P) {
		    if (*addP != *set2P) {
			return false;
		    }
		    ++set1P;
		    ++set2P;
		} else if (*addP == *set2P) {
		    return false;
		}
		++addP;
	    }
	    ++subP;
	}
    }

    return true;
}

#endif /* TK_UNUSED_INTSET_FUNCTIONS */


#if __STDC_VERSION__ >= 199901L || (defined(_MSC_VER) && _MSC_VER >= 1900)
/* Additionally we need stand-alone object code. */
#define inline extern
inline unsigned TkIntSetByteSize(const TkIntSet *set);
inline const unsigned char *TkIntSetData(const TkIntSet *set);
inline bool TkIntSetIsEmpty(const TkIntSet *set);
inline unsigned TkIntSetSize(const TkIntSet *set);
inline unsigned TkIntSetMax(const TkIntSet *set);
inline unsigned TkIntSetRefCount(const TkIntSet *set);
inline void TkIntSetIncrRefCount(TkIntSet *set);
inline unsigned TkIntSetDecrRefCount(TkIntSet *set);
inline TkIntSetType TkIntSetAccess(const TkIntSet *set, unsigned index);
inline bool TkIntSetTest(const TkIntSet *set, unsigned n);
inline bool TkIntSetNone(const TkIntSet *set);
inline bool TkIntSetAny(const TkIntSet *set);
inline bool TkIntSetIsEqual(const TkIntSet *set1, const TkIntSet *set2);
inline bool TkIntSetContains(const TkIntSet *set1, const TkIntSet *set2);
inline bool TkIntSetDisjunctive(const TkIntSet *set1, const TkIntSet *set2);
inline bool TkIntSetIntersects(const TkIntSet *set1, const TkIntSet *set2);
inline unsigned TkIntSetFindFirst(const TkIntSet *set);
inline unsigned TkIntSetFindNext(const TkIntSet *set);
inline TkIntSet *TkIntSetAddOrErase(TkIntSet *set, unsigned n, bool add);
#endif /* __STDC_VERSION__ >= 199901L */

/* vi:set ts=8 sw=4: */
