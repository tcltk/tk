/*
 * tkTextTagSet.c --
 *
 *	This module implements a set for tagging information.
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkTextTagSet.h"

#ifndef TK_C99_INLINE_SUPPORT
# define _TK_NEED_IMPLEMENTATION
# include "tkTextTagSetPriv.h"
#endif

#if !TK_TEXT_DONT_USE_BITFIELDS

# include <assert.h>
# include <string.h>

# ifndef MAX
#  define MAX(a,b) (((int) a) < ((int) b) ? b : a)
# endif

/*
 * Don't use expensive checks for speed improvements. But probably these "expensive"
 * checks aren't so much expensive? This needs more testing for a final decision.
 */
#define USE_EXPENSIVE_CHECKS 0


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


static TkTextTagSet *
ConvertToBitField(
    TkTextTagSet *ts,
    unsigned newSize)
{
    TkBitField *bf;
    assert(ts->base.isSetFlag);
    assert(ts->base.refCount > 0);
    bf = TkBitFromSet(&ts->set, newSize);
    TkIntSetDecrRefCount(&ts->set);
    return (TkTextTagSet *) bf;
}


static TkTextTagSet *
ConvertToIntSet(
    TkTextTagSet *ts)
{
    TkIntSet *set;
    assert(!ts->base.isSetFlag);
    assert(ts->base.refCount > 0);
    set = TkIntSetFromBits(&ts->bf);
    TkBitDecrRefCount(&ts->bf);
    return (TkTextTagSet *) set;
}


static TkTextTagSet *
ConvertToEmptySet(
    TkTextTagSet *ts)
{
    if (TkTextTagSetIsEmpty(ts)) {
	return ts;
    }
    if (ts->base.refCount > 1) {
	ts->base.refCount -= 1;
	return (TkTextTagSet *) TkBitResize(NULL, 0);
    }
    if (ts->base.isSetFlag) {
	return (TkTextTagSet *) TkIntSetClear(&ts->set);
    }
    TkBitClear(&ts->bf);
    return ts;
}


static TkTextTagSet *
Convert(
    TkTextTagSet *ts)
{
    if (ts->base.isSetFlag) {
	TkIntSetType size;

	if (TkIntSetIsEmpty(&ts->set)) {
	    return ts;
	}

	size = TkIntSetMax(&ts->set) + 1;

	if (size <= TK_TEXT_SET_MAX_BIT_SIZE) {
	    if (!IsPowerOf2(size)) {
		size = NextPowerOf2(size);
	    }
	    return ConvertToBitField(ts, size);
	}
    } else if (TkBitSize(&ts->bf) > TK_TEXT_SET_MAX_BIT_SIZE) {
	return ConvertToIntSet(ts);
    }
    return ts;
}


static TkBitField *
MakeBitCopy(
    TkTextTagSet *ts)
{
    assert(ts->base.refCount > 1);
    assert(!ts->base.isSetFlag);

    ts->base.refCount -= 1;
    return TkBitCopy(&ts->bf, -1);
}


static TkIntSet *
MakeIntSetCopy(
    TkTextTagSet *ts)
{
    assert(ts->base.refCount > 1);
    assert(ts->base.isSetFlag);

    ts->base.refCount -= 1;
    return TkIntSetCopy(&ts->set);
}


static TkTextTagSet *
MakeBitCopyIfNeeded(
    TkTextTagSet *ts)
{
    assert(ts->base.refCount > 0);
    assert(!ts->base.isSetFlag);

    return (TkTextTagSet *) (ts->base.refCount == 1 ? &ts->bf : MakeBitCopy(ts));
}


static TkIntSet *
MakeIntSetCopyIfNeeded(
    TkTextTagSet *ts)
{
    assert(ts->base.refCount > 0);
    assert(ts->base.isSetFlag);

    return ts->base.refCount == 1 ? &ts->set : MakeIntSetCopy(ts);
}


static TkIntSet *
ToIntSet(
    const TkTextTagSet *set)
{
    if (set->base.isSetFlag) {
	return (TkIntSet *) &set->set;
    }
    return (TkIntSet *) TkIntSetFromBits(&((TkTextTagSet *) set)->bf);
}


TkBitField *
TkTextTagSetToBits(
    const TkTextTagSet *src,
    int size)
{
    assert(src);

    if (src->base.isSetFlag) {
	return TkBitFromSet(&src->set, size < 0 ? (int) TkIntSetMax(&src->set) + 1 : size);
    }

    if (size < 0 || (int) TkBitSize(&src->bf) == size) {
	((TkTextTagSet *) src)->base.refCount += 1;
	return (TkBitField *) &src->bf;
    }

    return TkBitCopy(&src->bf, size);
}


void
TkTextTagSetDestroy(
    TkTextTagSet **tsPtr)
{
    assert(tsPtr);

    if (*tsPtr) {
	if ((*tsPtr)->base.isSetFlag) {
	    TkIntSetDestroy((TkIntSet **) tsPtr);
	} else {
	    TkBitDestroy((TkBitField **) tsPtr);
	}
    }
}


TkTextTagSet *
TkTextTagSetResize(
    TkTextTagSet *ts,
    unsigned newSize)
{
    assert(!ts || TkTextTagSetRefCount(ts) > 0);

    if (!ts) {
	ts = TkTextTagSetNew(newSize);
	ts->base.refCount = 1;
	return ts;
    }
    if (ts->base.isSetFlag) {
	if (newSize <= TK_TEXT_SET_MAX_BIT_SIZE) {
	    ts = ConvertToBitField(ts, newSize);
	}
    } else {
	if (newSize <= TK_TEXT_SET_MAX_BIT_SIZE) {
	    ts = (TkTextTagSet *) TkBitResize(&ts->bf, newSize);
	} else {
	    ts = ConvertToIntSet(ts);
	}
    }

    return ts;
}


bool
TkTextTagSetIsEqual_(
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2)
{
    assert(ts1);
    assert(ts2);
    assert(ts1->base.isSetFlag || ts2->base.isSetFlag);

    if (ts1->base.isSetFlag) {
	if (ts2->base.isSetFlag) {
	    return TkIntSetIsEqual(&ts1->set, &ts2->set);
	}
	return TkIntSetIsEqualBits(&ts1->set, &ts2->bf);
    }
    return TkIntSetIsEqualBits(&ts2->set, &ts1->bf);
}


bool
TkTextTagSetContains_(
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2)
{
    assert(ts1);
    assert(ts2);
    assert(ts1->base.isSetFlag || ts2->base.isSetFlag);

    if (ts1->base.isSetFlag) {
	if (ts2->base.isSetFlag) {
	    return TkIntSetContains(&ts1->set, &ts2->set);
	}
	return TkIntSetContainsBits(&ts1->set, &ts2->bf);
    }
    return TkIntSetIsContainedBits(&ts2->set, &ts1->bf);
}


bool
TkTextTagSetDisjunctive_(
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2)
{
    assert(ts1);
    assert(ts2);
    assert(ts1->base.isSetFlag || ts2->base.isSetFlag);

    if (ts1->base.isSetFlag) {
	if (ts2->base.isSetFlag) {
	    return TkIntSetDisjunctive(&ts1->set, &ts2->set);
	}
	return TkIntSetDisjunctiveBits(&ts1->set, &ts2->bf);
    }
    return TkIntSetDisjunctiveBits(&ts2->set, &ts1->bf);
}


bool
TkTextTagSetIntersectionIsEqual_(
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2,
    const TkBitField *bf)
{
    assert(ts1);
    assert(ts2);
    assert(bf);
    assert(ts1->base.isSetFlag || ts2->base.isSetFlag);

    if (ts1->base.isSetFlag) {
	if (ts2->base.isSetFlag) {
	    return TkIntSetIntersectionIsEqual(&ts1->set, &ts2->set, bf);
	}
	return TkIntSetIntersectionIsEqualBits(&ts1->set, &ts2->bf, bf);
    }
    return TkIntSetIntersectionIsEqualBits(&ts2->set, &ts1->bf, bf);
}


TkTextTagSet *
TkTextTagSetJoin(
    TkTextTagSet *dst,
    const TkTextTagSet *src)
{
    assert(src);
    assert(dst);

    if (src == dst || TkTextTagSetIsEmpty(src)) {
	return dst;
    }

    if (TkTextTagSetIsEmpty(dst)) {
	TkTextTagSetIncrRefCount((TkTextTagSet *) src); /* mutable by definition */
	TkTextTagSetDecrRefCount(dst);
	return Convert((TkTextTagSet *) src);
    }

#if USE_EXPENSIVE_CHECKS
    if (TkTextTagSetContains(src, dst) || TkTextTagSetContains(dst, src)) {
	return dst;
    }
#endif

    if (src->base.isSetFlag | dst->base.isSetFlag) {
	if (src->base.isSetFlag) {
	    if (dst->base.isSetFlag) {
		return (TkTextTagSet *) TkIntSetJoin(&dst->set, &src->set);
	    }
	    return (TkTextTagSet *) TkIntSetJoin(&ConvertToIntSet(dst)->set, &src->set);
	}
	return (TkTextTagSet *) TkIntSetJoinBits(&dst->set, &src->bf);
    }

    if (TkBitSize(&dst->bf) < TkBitSize(&src->bf)) {
	TkTextTagSet *set = dst;
	dst = (TkTextTagSet *) TkBitCopy(&src->bf, -1);
	TkBitJoin(&dst->bf, &set->bf);
	TkBitDecrRefCount(&set->bf);
	return dst;
    }

    dst = MakeBitCopyIfNeeded(dst);
    TkBitJoin(&dst->bf, &src->bf);
    return dst;
}


TkTextTagSet *
TkTextTagSetJoin2(
    TkTextTagSet *dst,
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2)
{
    assert(dst);
    assert(ts1);
    assert(ts2);

    if (ts2 == dst || TkTextTagSetIsEmpty(ts2)) {
	return TkTextTagSetJoin(dst, ts1);
    }
    if (ts1 == dst || ts1 == ts2 || TkTextTagSetIsEmpty(ts1)) {
	return TkTextTagSetJoin(dst, ts2);
    }
    if (TkTextTagSetIsEmpty(dst)) {
	TkTextTagSetIncrRefCount((TkTextTagSet *) ts1); /* mutable by definition */
	TkTextTagSetDecrRefCount(dst);
	return TkTextTagSetJoin((TkTextTagSet *) ts1, ts2);
    }

#if USE_EXPENSIVE_CHECKS
    if (TkTextTagSetContains(ts1, ts2) || TkTextTagSetContains(dst, ts2)) {
	return TkTextTagSetJoin(dst, ts1);
    }
    if (TkTextTagSetContains(ts2, ts1) || TkTextTagSetContains(dst, ts1)) {
	return TkTextTagSetJoin(dst, ts2);
    }
    if (TkTextTagSetContains(ts1, dst)) {
	TkTextTagSetDecrRefCount(dst);
	return TkTextTagSetJoin(TkTextTagSetCopy(ts1), ts2);
    }
    if (TkTextTagSetContains(ts2, dst)) {
	TkTextTagSetDecrRefCount(dst);
	return TkTextTagSetJoin(TkTextTagSetCopy(ts2), ts1);
    }
#endif

    if (ts1->base.isSetFlag | ts2->base.isSetFlag | dst->base.isSetFlag) {
	return TkTextTagSetJoin(TkTextTagSetJoin(dst, ts1), ts2);
    }

    if (TkBitSize(&ts1->bf) < TkBitSize(&ts2->bf)) {
	const TkTextTagSet *tmp = ts1;
	ts1 = ts2;
	ts2 = tmp;
    }
    if (TkBitSize(&dst->bf) < TkBitSize(&ts1->bf)) {
	TkTextTagSet *set = dst;
	dst = (TkTextTagSet *) TkBitCopy(&ts1->bf, -1);
	TkBitJoin2(&dst->bf, &set->bf, &ts2->bf);
	TkBitDecrRefCount(&set->bf);
	return dst;
    }

    dst = MakeBitCopyIfNeeded(dst);
    TkBitJoin2(&dst->bf, &ts1->bf, &ts2->bf);
    return dst;
}


TkTextTagSet *
TkTextTagSetIntersect(
    TkTextTagSet *dst,
    const TkTextTagSet *src)
{
    assert(src);
    assert(dst);

    if (src == dst || TkTextTagSetIsEmpty(dst)) {
	return dst;
    }
    if (TkTextTagSetIsEmpty(src)) {
	TkTextTagSetIncrRefCount((TkTextTagSet *) src); /* mutable by definition */
	TkTextTagSetDecrRefCount(dst);
	return (TkTextTagSet *) src;
    }

#if USE_EXPENSIVE_CHECKS
    if (TkTextTagSetContains(dst, src)) {
	return dst;
    }
    if (TkTextTagSetContains(src, dst)) {
	TkTextTagSetIncrRefCount((TkTextTagSet *) src); /* mutable by definition */
	TkTextTagSetDecrRefCount(dst);
	return (TkTextTagSet *) src;
    }
#endif

    if (src->base.isSetFlag | dst->base.isSetFlag) {
	TkBitField *bf, *tmp;

	assert(dst->base.refCount > 0);

	if (src->base.isSetFlag) {
	    if (dst->base.isSetFlag) {
		return (TkTextTagSet *) TkIntSetIntersect(&dst->set, &src->set);
	    }

	    tmp = TkBitFromSet(&src->set, TkBitSize(&dst->bf));
	    dst = MakeBitCopyIfNeeded(dst);
	    TkBitIntersect(&dst->bf, tmp);
	    TkBitDestroy(&tmp);
	    return dst;
	}

	bf = TkBitCopy(&src->bf, -1);
	tmp = TkBitFromSet(&dst->set, TkBitSize(&src->bf));
	TkBitIntersect(bf, tmp);
	TkBitDestroy(&tmp);
	TkIntSetDecrRefCount(&dst->set);
	return (TkTextTagSet *) bf;
    }

    dst = MakeBitCopyIfNeeded(dst);
    TkBitIntersect(&dst->bf, &src->bf);
    return dst;
}


TkTextTagSet *
TkTextTagSetIntersectThis(
    TkTextTagSet *dst,
    const TkTextTagSet *src)
{
    assert(src);
    assert(dst);

    if (src == dst || TkTextTagSetIsEmpty(dst)) {
	return dst;
    }
    if (src->base.isSetFlag | dst->base.isSetFlag) {
	TkBitField *bf, *tmp;

	assert(dst->base.refCount > 0);

	if (src->base.isSetFlag) {
	    if (dst->base.isSetFlag) {
		return (TkTextTagSet *) TkIntSetIntersect(&dst->set, &src->set);
	    }

	    tmp = TkBitFromSet(&src->set, TkBitSize(&dst->bf));
	    TkBitIntersect(&dst->bf, tmp);
	    TkBitDestroy(&tmp);
	    return dst;
	}

	bf = TkBitCopy(&src->bf, -1);
	tmp = TkBitFromSet(&dst->set, TkBitSize(&src->bf));
	TkBitIntersect(bf, tmp);
	TkBitDestroy(&tmp);
	TkIntSetDecrRefCount(&dst->set);
	return (TkTextTagSet *) bf;
    }

    TkBitIntersect(&dst->bf, &src->bf);
    return dst;
}


TkTextTagSet *
TkTextTagSetIntersectBits(
    TkTextTagSet *dst,
    const TkBitField *src)
{
    assert(src);
    assert(dst);

    if ((const TkTextTagSet *) src == dst || TkTextTagSetIsEmpty(dst)) {
	return dst;
    }
    if (TkBitNone(src)) {
	TkTextTagSetIncrRefCount((TkTextTagSet *) src); /* mutable by definition */
	TkTextTagSetDecrRefCount(dst);
	return Convert((TkTextTagSet *) src);
    }

#if USE_EXPENSIVE_CHECKS
    if (TkTextTagSetContainsBits(dst, src)) {
	return dst;
    }
    if (TkTextTagIsContainedInBits(dst, src)) {
	TkBitIncrRefCount((TkBitField *) src); /* mutable by definition */
	TkTextTagSetDecrRefCount(dst);
	return (TkTextTagSet *) src;
    }
#endif

    if (dst->base.isSetFlag) {
	return Convert((TkTextTagSet *) TkIntSetIntersectBits(&dst->set, src));
    }

    dst = MakeBitCopyIfNeeded(dst);
    TkBitIntersect(&dst->bf, src);
    return dst;
}


TkTextTagSet *
TkTextTagSetRemove(
    TkTextTagSet *dst,
    const TkTextTagSet *src)
{
    assert(src);
    assert(dst);

    if (TkTextTagSetIsEmpty(src) || TkTextTagSetIsEmpty(dst)) {
	return dst;
    }
    if (src == dst) {
	return ConvertToEmptySet(dst);
    }

#if USE_EXPENSIVE_CHECKS
    if (TkTextTagSetContains(src, dst)) {
	return ConvertToEmptySet(dst);
    }
#endif

    if (src->base.isSetFlag | dst->base.isSetFlag) {
	TkBitField *bf;

	assert(dst->base.refCount > 0);

	if (dst->base.isSetFlag) {
	    if (src->base.isSetFlag) {
		return Convert((TkTextTagSet *) TkIntSetRemove(&dst->set, &src->set));
	    }
	    return Convert((TkTextTagSet *) TkIntSetRemoveBits(&dst->set, &src->bf));
	}

	bf = TkBitFromSet(&src->set, TkBitSize(&dst->bf));
	dst = MakeBitCopyIfNeeded(dst);
	TkBitRemove(&dst->bf, bf);
	TkBitDestroy(&bf);
	return dst;
    }

    dst = MakeBitCopyIfNeeded(dst);
    TkBitRemove(&dst->bf, &src->bf);
    return dst;
}


TkTextTagSet *
TkTextTagSetRemoveFromThis(
    TkTextTagSet *dst,
    const TkTextTagSet *src)
{
    assert(src);
    assert(dst);

    if (TkTextTagSetIsEmpty(src) || TkTextTagSetIsEmpty(dst)) {
	return dst;
    }
    if (src == dst) {
	if (dst->base.isSetFlag) {
	    return (TkTextTagSet *) TkIntSetClear(&dst->set);
	}
	TkBitClear(&dst->bf);
	return dst;
    }

    if (src->base.isSetFlag | dst->base.isSetFlag) {
	TkBitField *bf;

	assert(dst->base.refCount > 0);

	if (dst->base.isSetFlag) {
	    if (src->base.isSetFlag) {
		return Convert((TkTextTagSet *) TkIntSetRemove(&dst->set, &src->set));
	    }
	    return Convert((TkTextTagSet *) TkIntSetRemoveBits(&dst->set, &src->bf));
	}

	bf = TkBitFromSet(&src->set, TkBitSize(&dst->bf));
	TkBitRemove(&dst->bf, bf);
	TkBitDestroy(&bf);
	return dst;
    }

    TkBitRemove(&dst->bf, &src->bf);
    return dst;
}


TkTextTagSet *
TkTextTagSetRemoveBits(
    TkTextTagSet *dst,
    const TkBitField *src)
{
    assert(src);
    assert(dst);

    if (TkBitNone(src) || TkTextTagSetIsEmpty(dst)) {
	return dst;
    }
    if ((const TkTextTagSet *) src == dst) {
	return ConvertToEmptySet(dst);
    }

#if USE_EXPENSIVE_CHECKS
    if (TkTextTagSetIsContainedBits(dst, src)) {
	return ConvertToEmptySet(dst);
    }
#endif

    if (dst->base.isSetFlag) {
	return Convert((TkTextTagSet *) TkIntSetRemoveBits(&dst->set, src));
    }

    dst = MakeBitCopyIfNeeded(dst);
    TkBitRemove(&dst->bf, src);
    return dst;
}


TkTextTagSet *
TkTextTagSetComplementTo(
    TkTextTagSet *dst,
    const TkTextTagSet *src)
{
    assert(src);
    assert(dst);

    if (src == dst) {
	return ConvertToEmptySet(dst);
    }
    if (TkTextTagSetIsEmpty(src) || TkTextTagSetIsEmpty(dst)) {
	TkTextTagSetIncrRefCount((TkTextTagSet *) src); /* mutable by definition */
	TkTextTagSetDecrRefCount(dst);
	return (TkTextTagSet *) src;
    }

#if USE_EXPENSIVE_CHECKS
    if (TkTextTagSetContains(dst, src)) {
	return ConvertToEmptySet(dst);
    }
#endif

    if (src->base.isSetFlag | dst->base.isSetFlag) {
	TkIntSet *set;

	if (dst->base.isSetFlag) {
	    if (src->base.isSetFlag) {
		return Convert((TkTextTagSet *) TkIntSetComplementTo(&dst->set, &src->set));
	    }
	    return Convert((TkTextTagSet *) TkIntSetComplementToBits(&dst->set, &src->bf));
	}

	TkIntSetIncrRefCount((TkIntSet *) &src->set);
	set = TkIntSetRemoveBits((TkIntSet *) &src->set, &dst->bf);
	TkBitDecrRefCount(&dst->bf);
	return Convert((TkTextTagSet *) set);
    }

    if (dst->base.refCount > 1 || TkBitSize(&dst->bf) < TkBitSize(&src->bf)) {
	TkBitField *bf;

	bf = TkBitCopy(&src->bf, -1);
	TkBitRemove(bf, &dst->bf);
	TkBitDecrRefCount(&dst->bf);
	return (TkTextTagSet *) bf;
    }
    TkBitComplementTo(&dst->bf, &src->bf);
    return dst;
}


TkTextTagSet *
TkTextTagSetJoinComplementTo(
    TkTextTagSet *dst,
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2)
{
    assert(dst);
    assert(ts1);
    assert(ts2);

    if (dst == ts2 || TkTextTagSetIsEmpty(ts2)) {
	return dst;
    }
    if (TkTextTagSetIsEmpty(ts1)) {
	return TkTextTagSetJoin(dst, ts2);
    }

#if USE_EXPENSIVE_CHECKS
    if (TkTextTagSetContains(dst, ts2) || TkTextTagSetContains(ts1, ts2)) {
	return dst;
    }
    if (TkTextTagSetContains(dst, ts1)) {
	return TkTextTagSetJoin(dst, ts2);
    }
    if (TkTextTagSetContains(ts2, dst)) {
	TkTextTagSetDecrRefCount(dst);
	return TkTextTagSetRemove(TkTextTagSetCopy(ts2), ts1);
    }
#endif

    if (dst->base.isSetFlag | ts1->base.isSetFlag | ts2->base.isSetFlag) {
	TkTextTagSet *tmp;

	if (!(dst->base.isSetFlag | ts1->base.isSetFlag)) {
	    TkBitField *bf2 = TkBitFromSet(&ts2->set, TkBitSize(&ts1->bf));

	    dst = MakeBitCopyIfNeeded(dst);
	    TkBitJoinComplementTo(&dst->bf, &ts1->bf, bf2);
	    TkBitDestroy(&bf2);
	    return dst;
	}

	tmp = TkTextTagSetRemove(TkTextTagSetCopy(ts2), ts1);
	dst = TkTextTagSetJoin(dst, tmp);
	TkTextTagSetDecrRefCount(tmp);
	return dst;
    }

    dst = MakeBitCopyIfNeeded(dst);
    TkBitJoinComplementTo(&dst->bf, &ts1->bf, &ts2->bf);
    return dst;
}


TkTextTagSet *
TkTextTagSetJoinNonIntersection(
    TkTextTagSet *dst,
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2)
{
    assert(dst);
    assert(ts1);
    assert(ts2);

    if (ts1 == ts2) {
	return dst;
    }
    if (TkTextTagSetIsEmpty(ts1) && TkTextTagSetIsEmpty(ts2)) {
	return dst;
    }
    if (dst == ts1 || TkTextTagSetIsEmpty(ts1)) {
	return TkTextTagSetJoin(dst, ts2);
    }
    if (dst == ts2 || TkTextTagSetIsEmpty(ts2)) {
	return TkTextTagSetJoin(dst, ts1);
    }

#if USE_EXPENSIVE_CHECKS
    if (TkTextTagSetIsEqual(ts1, ts2)) {
	return dst;
    }
    if (TkTextTagSetContains(dst, ts1)) {
	return TkTextTagSetJoin(dst, ts2);
    }
    if (TkTextTagSetContains(dst, ts2)) {
	return TkTextTagSetJoin(dst, ts1);
    }
#endif

    if (dst->base.isSetFlag | ts1->base.isSetFlag | ts2->base.isSetFlag) {
	TkIntSet *set1, *set2;

	dst = dst->base.isSetFlag ? (TkTextTagSet *) MakeIntSetCopyIfNeeded(dst) : ConvertToIntSet(dst);
	set1 = ToIntSet(ts1);
	set2 = ToIntSet(ts2);
	dst = (TkTextTagSet *) TkIntSetJoinNonIntersection(&dst->set, set1, set2);
	if (&ts1->set != set1) { TkIntSetDestroy(&set1); }
	if (&ts2->set != set2) { TkIntSetDestroy(&set2); }
	return Convert(dst);
    }

    dst = MakeBitCopyIfNeeded(dst);
    TkBitJoinNonIntersection(&dst->bf, &ts1->bf, &ts2->bf);
    return dst;
}


TkTextTagSet *
TkTextTagSetJoin2ComplementToIntersection(
    TkTextTagSet *dst,
    const TkTextTagSet *add,
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2)
{
    assert(dst);
    assert(add);
    assert(ts1);
    assert(ts2);

    if (ts1 == ts2) {
	return TkTextTagSetJoin(dst, add);
    }
    if (TkTextTagSetIsEmpty(ts1)) {
	return TkTextTagSetJoin2(dst, add, ts2);
    }
    if (TkTextTagSetIsEmpty(ts2)) {
	return TkTextTagSetJoin2(dst, add, ts1);
    }

#if USE_EXPENSIVE_CHECKS
    if (TkTextTagSetIsEqual(ts1, ts2)) {
	return TkTextTagSetJoin(dst, add);
    }
    if (TkTextTagSetContains(dst, ts1) && TkTextTagSetContains(dst, ts2)) {
	return TkTextTagSetJoin(dst, add);
    }
#endif

    if (dst->base.isSetFlag | add->base.isSetFlag | ts1->base.isSetFlag | ts2->base.isSetFlag) {
	TkIntSet *set1, *set2, *set3;

	dst = dst->base.isSetFlag ? (TkTextTagSet *) MakeIntSetCopyIfNeeded(dst) : ConvertToIntSet(dst);
	set1 = ToIntSet(add);
	set2 = ToIntSet(ts1);
	set3 = ToIntSet(ts2);
	dst = (TkTextTagSet *) TkIntSetJoin2ComplementToIntersection(&dst->set, set1, set2, set3);
	if (&add->set != set1) { TkIntSetDestroy(&set1); }
	if (&ts1->set != set2) { TkIntSetDestroy(&set2); }
	if (&ts2->set != set3) { TkIntSetDestroy(&set3); }
	return dst;
    }

    dst = MakeBitCopyIfNeeded(dst);
    TkBitJoin2ComplementToIntersection(&dst->bf, &add->bf, &ts1->bf, &ts2->bf);
    return dst;
}


TkTextTagSet *
TkTextTagSetJoinOfDifferences(
    TkTextTagSet *dst,
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2)
{
    assert(dst);
    assert(ts1);
    assert(ts2);

    if (ts1 == ts2) {
	return TkTextTagSetRemove(dst, ts1);
    }
    if (dst == ts1) {
	TkTextTagSetDecrRefCount(dst);
	TkTextTagSetIncrRefCount((TkTextTagSet *) ts1); /* mutable due to concept */
	return TkTextTagSetRemove((TkTextTagSet *) ts1, ts2);
    }

#if USE_EXPENSIVE_CHECKS
    if (TkTextTagSetIsEqual(ts1, ts2)) {
	return TkTextTagSetRemove(dst, ts1);
    }
    if (TkTextTagSetContains(ts1, dst)) {
	TkTextTagSetDecrRefCount(dst);
	TkTextTagSetIncrRefCount(ts1);
	return TkTextTagSetRemove(ts1, ts2);
    }
#endif

    if (dst->base.isSetFlag | ts1->base.isSetFlag | ts2->base.isSetFlag) {
	TkIntSet *set1, *set2;

	dst = dst->base.isSetFlag ? (TkTextTagSet *) MakeIntSetCopyIfNeeded(dst) : ConvertToIntSet(dst);
	set1 = ToIntSet(ts1);
	set2 = ToIntSet(ts2);
	dst = (TkTextTagSet *) TkIntSetJoinOfDifferences(&dst->set, set1, set2);
	if (&ts1->set != set1) { TkIntSetDestroy(&set1); }
	if (&ts2->set != set2) { TkIntSetDestroy(&set2); }
	return Convert(dst);
    }

    dst = MakeBitCopyIfNeeded(dst);
    TkBitJoinOfDifferences(&dst->bf, &ts1->bf, &ts2->bf);
    return dst;
}


TkTextTagSet *
TkTextTagSetAdd(
    TkTextTagSet *dst,
    unsigned n)
{
    assert(dst);

    if (dst->base.isSetFlag) {
	return (TkTextTagSet *) TkIntSetAdd(MakeIntSetCopyIfNeeded(dst), n);
    }
    dst = MakeBitCopyIfNeeded(dst);
    TkBitSet(&dst->bf, n);
    return dst;
}


TkTextTagSet *
TkTextTagSetErase(
    TkTextTagSet *dst,
    unsigned n)
{
    assert(dst);

    if (dst->base.isSetFlag) {
	return (TkTextTagSet *) TkIntSetErase(MakeIntSetCopyIfNeeded(dst), n);
    }
    dst = MakeBitCopyIfNeeded(dst);
    TkBitUnset(&dst->bf, n);
    return dst;
}


TkTextTagSet *
TkTextTagSetTestAndSet(
    TkTextTagSet *dst,
    unsigned n)
{
    assert(dst);

    if (dst->base.isSetFlag) {
	if (dst->base.refCount <= 1) {
	    return (TkTextTagSet *) TkIntSetTestAndSet(&dst->set, n);
	}
	if (TkIntSetTest(&dst->set, n)) {
	    return NULL;
	}
	return (TkTextTagSet *) TkIntSetAdd(MakeIntSetCopy(dst), n);
    }
    if (dst->base.refCount <= 1) {
	return TkBitTestAndSet(&dst->bf, n) ? dst : NULL;
    }
    if (TkBitTest(&dst->bf, n)) {
	return NULL;
    }
    dst = (TkTextTagSet *) MakeBitCopy(dst);
    TkBitSet(&dst->bf, n);
    return dst;
}


TkTextTagSet *
TkTextTagSetTestAndUnset(
    TkTextTagSet *dst,
    unsigned n)
{
    assert(dst);

    if (dst->base.isSetFlag) {
	if (dst->base.refCount <= 1) {
	    return (TkTextTagSet *) TkIntSetTestAndUnset(&dst->set, n);
	}
	if (!TkIntSetTest(&dst->set, n)) {
	    return NULL;
	}
	return (TkTextTagSet *) TkIntSetErase(MakeIntSetCopy(dst), n);
    }
    if (dst->base.refCount <= 1) {
	return TkBitTestAndUnset(&dst->bf, n) ? dst : NULL;
    }
    if (!TkBitTest(&dst->bf, n)) {
	return NULL;
    }
    dst = (TkTextTagSet *) MakeBitCopy(dst);
    TkBitUnset(&dst->bf, n);
    return dst;
}


TkTextTagSet *
TkTextTagSetClear(
    TkTextTagSet *dst)
{
    assert(dst);

    if (dst->base.isSetFlag) {
	TkIntSetDecrRefCount(&dst->set);
	return (TkTextTagSet *) TkBitResize(NULL, 0);
    }
    return ConvertToEmptySet(dst);
}


unsigned
TkTextTagSetFindFirstInIntersection(
    const TkTextTagSet *ts,
    const TkBitField *bf)
{
    unsigned size, i;

    assert(ts);
    assert(bf);

    if (!ts->base.isSetFlag) {
	return TkBitFindFirstInIntersection(&ts->bf, bf);
    }

    if (!TkBitNone(bf)) {
	size = TkIntSetSize(&ts->set);

	for (i = 0; i < size; ++i) {
	    TkIntSetType value = TkIntSetAccess(&ts->set, i);

	    if (TkBitTest(bf, value)) {
		return value;
	    }
	}
    }

    return TK_TEXT_TAG_SET_NPOS;
}

#ifndef NDEBUG

void
TkTextTagSetPrint(
    const TkTextTagSet *set)
{
    if (!set) {
	printf("<null>\n");
    } else if (TkTextTagSetIsEmpty(set)) {
	printf("<empty>\n");
    } else if (set->base.isSetFlag) {
	TkIntSetPrint(&set->set);
    } else {
	TkBitPrint(&set->bf);
    }
}

# endif /* NDEBUG */
# if 0

/*
 * These functions are not needed anymore, but shouldn't be removed, because sometimes
 * any of these functions might be useful.
 */

static unsigned
MaxSize3(
    const TkTextTagSet *ts1, const TkTextTagSet *ts2, const TkTextTagSet *ts3)
{
    return TkBitAdjustSize(MAX(TkTextTagSetRangeSize(ts1),
	    MAX(TkTextTagSetRangeSize(ts2), TkTextTagSetRangeSize(ts3))));
}


static unsigned
MaxSize4(
    const TkTextTagSet *ts1, const TkTextTagSet *ts2,
    const TkTextTagSet *ts3, const TkTextTagSet *ts4)
{
    return TkBitAdjustSize(MAX(TkTextTagSetRangeSize(ts1), MAX(TkTextTagSetRangeSize(ts2),
	    MAX(TkTextTagSetRangeSize(ts3), TkTextTagSetRangeSize(ts4)))));
}


static TkBitField *
GetBitField(
    const TkTextTagSet *ts,
    int size)
{
    assert(size == -1 || size >= TkTextTagSetRangeSize(ts));
    assert(size == -1 || size == TkBitAdjustSize(size));

    if (ts->base.isSetFlag) {
	return TkBitFromSet(&ts->set, size);
    }

    if (size >= 0 && TkBitSize(&ts->bf) != size) {
	return TkBitCopy(&ts->bf, size);
    }

    /* mutable due to concept */
    TkBitIncrRefCount((TkBitField *) &ts->bf);
    return (TkBitField *) &ts->bf;
}


TkTextTagSet *
TkTextTagSetInnerJoinDifference(
    TkTextTagSet *dst,
    const TkTextTagSet *add,
    const TkTextTagSet *sub)
{
    assert(dst);
    assert(add);
    assert(sub);

    /* dst := (dst & add) + (add - sub) */

    if (add == dst) {
	return dst;
    }
    if (TkTextTagSetIsEmpty(add)) {
	return TkTextTagSetClear(dst);
    }
    if (TkTextTagSetIsEqual(add, dst)) {
	return dst;
    }

#if USE_EXPENSIVE_CHECKS
    if (TkTextTagSetContains(dst, add)) {
	return dst;
    }
    if (TkTextTagSetContains(add, dst) || TkTextTagSetContains(dst, sub)) {
	TkTextTagSetIncrRefCount((TkTextTagSet *) add); /* mutable by definition */
	TkTextTagSetDecrRefCount(dst);
	return (TkTextTagSet *) add;
    }
    if (TkTextTagSetContains(sub, add) || TkTextTagSetContains(add, sub)) {
	return TkTextTagSetIntersect(dst, add);
    }
#endif

    if (dst->base.isSetFlag | add->base.isSetFlag | sub->base.isSetFlag) {
	TkIntSet *set1, *set2, *res;

	res = dst->base.isSetFlag ? TkIntSetCopy(&dst->set) : ConvertToIntSet(dst);
	set1 = ToIntSet(add);
	set2 = ToIntSet(sub);
	res = TkIntSetInnerJoinDifference(res, set1, set2);
	if (&add->set != set1) { TkIntSetDestroy(&set1); }
	if (&sub->set != set2) { TkIntSetDestroy(&set2); }
	return (TkTextTagSet *) res;
    }

    dst = MakeBitCopyIfNeeded(dst);
    TkBitInnerJoinDifference(&dst->bf, &add->bf, &sub->bf);
    return dst;
}


bool
TkTextTagSetInnerJoinDifferenceIsEmpty(
    const TkTextTagSet *ts,
    const TkTextTagSet *add,
    const TkTextTagSet *sub)
{
    TkBitField *bf, *bfAdd, *bfSub;
    bool isEmpty;
    unsigned n, size;

    assert(ts);
    assert(add);
    assert(sub);

    if (ts == add) {
	return TkTextTagSetIsEmpty(ts);
    }
    if ((n = ts->base.isSetFlag + add->base.isSetFlag + sub->base.isSetFlag) == 0) {
	return TkBitInnerJoinDifferenceIsEmpty(&ts->bf, &add->bf, &sub->bf);
    }
    if (n == 3) {
	return TkIntSetInnerJoinDifferenceIsEmpty(&ts->set, &add->set, &sub->set);
    }
    if (TkTextTagSetIsEmpty(add)) {
	return true;
    }
    if (TkTextTagSetIsEqual(ts, add)) {
	return TkTextTagSetIsEmpty(add);
    }

    size = MaxSize3(ts, add, sub);
    bf = GetBitField(ts, size);
    bfAdd = GetBitField(add, size);
    bfSub = GetBitField(sub, size);
    isEmpty = TkBitInnerJoinDifferenceIsEmpty(bf, bfAdd, bfSub);
    TkBitDecrRefCount(bf);
    TkBitDecrRefCount(bfAdd);
    TkBitDecrRefCount(bfSub);

    return isEmpty;
}


bool
TkTextTagSetIsEqualToDifference(
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2,
    const TkTextTagSet *sub2)
{
    TkBitField *bf1, *bf2, *bfSub;
    bool isEqual;
    unsigned n, size;

    assert(ts1);
    assert(ts2);
    assert(sub2);

    if ((n = ts1->base.isSetFlag + ts2->base.isSetFlag + sub2->base.isSetFlag) == 0) {
	return TkBitIsEqualToDifference(&ts1->bf, &ts2->bf, &sub2->bf);
    }
    if (n == 3) {
	return TkIntSetIsEqualToDifference(&ts1->set, &ts2->set, &sub2->set);
    }
    if (TkTextTagSetIsEmpty(ts2)) {
	return TkTextTagSetIsEmpty(ts1);
    }
    if (TkTextTagSetIsEmpty(ts1)) {
	return TkTextTagSetContains(sub2, ts2);
    }

    size = MaxSize3(ts1, ts2, sub2);
    bf1 = GetBitField(ts1, size);
    bf2 = GetBitField(ts2, size);
    bfSub = GetBitField(sub2, size);
    isEqual = TkBitIsEqualToDifference(bf1, bf2, bfSub);
    TkBitDecrRefCount(bf1);
    TkBitDecrRefCount(bf2);
    TkBitDecrRefCount(bfSub);

    return isEqual;
}


bool
TkTextTagSetIsEqualToInnerJoin(
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2,
    const TkTextTagSet *add2)
{
    TkBitField *bf1, *bf2, *bfAdd;
    bool isEqual;
    unsigned n, size;

    assert(ts1);
    assert(ts2);
    assert(add2);

    if (ts1 == ts2) {
	return true;
    }
    if ((n = ts1->base.isSetFlag + ts2->base.isSetFlag + add2->base.isSetFlag) == 0) {
	return TkBitIsEqualToInnerJoin(&ts1->bf, &ts2->bf, &add2->bf);
    }
    if (n == 3) {
	return TkIntSetIsEqualToInnerJoin(&ts1->set, &ts2->set, &add2->set);
    }
    if (TkTextTagSetIsEqual(ts2, add2)) {
	return TkTextTagSetIsEqual(ts1, ts2);
    }
    if (TkTextTagSetIsEmpty(ts2)) {
	return TkTextTagSetIsEmpty(ts1);
    }
    if (TkTextTagSetIsEqual(ts1, ts2)) {
	return true;
    }

    size = MaxSize3(ts1, ts2, add2);
    bf1 = GetBitField(ts1, size);
    bf2 = GetBitField(ts2, size);
    bfAdd = GetBitField(add2, size);
    isEqual = TkBitIsEqualToInnerJoin(bf1, bf2, bfAdd);
    TkBitDecrRefCount(bf1);
    TkBitDecrRefCount(bf2);
    TkBitDecrRefCount(bfAdd);

    return isEqual;
}


bool
TkTextTagSetIsEqualToInnerJoinDifference(
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2,
    const TkTextTagSet *add2,
    const TkTextTagSet *sub2)
{
    TkBitField *bf1, *bf2, *bfAdd, *bfSub;
    bool isEqual;
    unsigned n, size;

    assert(ts1);
    assert(ts2);
    assert(add2);
    assert(sub2);

    n = ts1->base.isSetFlag + ts2->base.isSetFlag + add2->base.isSetFlag + sub2->base.isSetFlag;

    if (n == 0) {
	return TkBitIsEqualToInnerJoinDifference(&ts1->bf, &ts2->bf, &add2->bf, &sub2->bf);
    }
    if (n == 4) {
	return TkIntSetIsEqualToInnerJoinDifference(&ts1->set, &ts2->set, &add2->set, &sub2->set);
    }
    if (TkTextTagSetIsEmpty(add2)) {
	return TkTextTagSetIsEmpty(ts1);
    }
    if (TkTextTagSetIsEmpty(sub2)) {
	return TkTextTagSetIsEqualToInnerJoin(ts1, add2, ts2);
    }

    size = MaxSize4(ts1, ts2, add2, sub2);
    bf1 = GetBitField(ts1, size);
    bf2 = GetBitField(ts2, size);
    bfAdd = GetBitField(add2, size);
    bfSub = GetBitField(sub2, size);
    isEqual = TkBitIsEqualToInnerJoinDifference(bf1, bf2, bfAdd, bfSub);
    TkBitDecrRefCount(bf1);
    TkBitDecrRefCount(bf2);
    TkBitDecrRefCount(bfAdd);
    TkBitDecrRefCount(bfSub);

    return isEqual;
}


bool
TkTextTagSetInnerJoinDifferenceIsEqual(
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2,
    const TkTextTagSet *add,
    const TkTextTagSet *sub)
{
    TkBitField *bf1, *bf2, *bfAdd, *bfSub;
    bool isEqual;
    unsigned n, size;

    assert(ts1);
    assert(ts2);
    assert(add);
    assert(sub);

    n = ts1->base.isSetFlag + ts2->base.isSetFlag + add->base.isSetFlag + sub->base.isSetFlag;

    if (n == 0) {
	return TkBitInnerJoinDifferenceIsEqual(&ts1->bf, &ts2->bf, &add->bf, &sub->bf);
    }
    if (n == 4) {
	return TkIntSetInnerJoinDifferenceIsEqual(&ts1->set, &ts2->set, &add->set, &sub->set);
    }
    if (TkTextTagSetIsEmpty(add)) {
	return true;
    }

    size = MaxSize4(ts1, ts2, add, sub);
    bf1 = GetBitField(ts1, size);
    bf2 = GetBitField(ts2, size);
    bfAdd = GetBitField(add, size);
    bfSub = GetBitField(sub, size);
    isEqual = TkBitInnerJoinDifferenceIsEqual(bf1, bf2, bfAdd, bfSub);
    TkBitDecrRefCount(bf1);
    TkBitDecrRefCount(bf2);
    TkBitDecrRefCount(bfAdd);
    TkBitDecrRefCount(bfSub);

    return isEqual;
}

# endif /* 0 */
#else /* integer set only implementation **************************************/

static TkIntSet *
ConvertToEmptySet(
    TkIntSet *ts)
{
    if (TkIntSetIsEmpty(ts)) {
	return ts;
    }
    if (ts->refCount == 1) {
	return TkIntSetClear(ts);
    }
    ts->refCount -= 1;
    (ts = TkIntSetNew())->refCount = 1;
    return ts;
}


static TkIntSet *
MakeCopyIfNeeded(
    TkIntSet *ts)
{
    assert(ts->refCount > 0);

    if (ts->refCount == 1) {
	return ts;
    }
    ts->refCount -= 1;
    return TkIntSetCopy(ts);
}


TkBitField *
TkTextTagSetToBits(
    const TkTextTagSet *src,
    int size)
{
    assert(src);
    return TkBitFromSet(&src->set, size < 0 ? TkIntSetMax(&src->set) + 1 : size);
}


TkIntSet *
TkTextTagSetJoin(
    TkIntSet *dst,
    const TkIntSet *src)
{
    assert(src);
    assert(dst);

    if (src == dst || TkIntSetIsEmpty(src)) {
	return dst;
    }
    if (TkIntSetIsEmpty(dst)) {
	TkIntSetIncrRefCount((TkIntSet *) src); /* mutable by definition */
	TkIntSetDecrRefCount(dst);
	return (TkIntSet *) src;
    }
    return TkIntSetJoin(MakeCopyIfNeeded(dst), src);
}


TkIntSet *
TkTextTagSetIntersect(
    TkIntSet *dst,
    const TkIntSet *src)
{
    assert(src);
    assert(dst);

    if (src == dst || TkIntSetIsEmpty(dst)) {
	return dst;
    }
    if (TkIntSetIsEmpty(src)) {
	TkIntSetIncrRefCount((TkIntSet *) src); /* mutable by definition */
	TkIntSetDecrRefCount(dst);
	return (TkIntSet *) src;
    }
    return TkIntSetIntersect(MakeCopyIfNeeded(dst), src);
}


TkIntSet *
TkTextTagSetRemove(
    TkIntSet *dst,
    const TkIntSet *src)
{
    assert(src);
    assert(dst);

    if (TkIntSetIsEmpty(src) || TkIntSetIsEmpty(dst)) {
	return dst;
    }
    if (src == dst) {
	return ConvertToEmptySet(dst);
    }
    return TkIntSetRemove(MakeCopyIfNeeded(dst), src);
}


TkIntSet *
TkTextTagSetIntersectBits(
    TkIntSet *dst,
    const TkBitField *src)
{
    assert(src);
    assert(dst);

    if (TkIntSetIsEmpty(dst)) {
	return dst;
    }
    if (TkBitNone(src)) {
	TkIntSetIncrRefCount((TkIntSet *) src); /* mutable by definition */
	TkIntSetDecrRefCount(dst);
	return (TkIntSet *) src;
    }
    return TkIntSetIntersectBits(MakeCopyIfNeeded(dst), src);
}


TkIntSet *
TkTextTagSetRemoveBits(
    TkIntSet *dst,
    const TkBitField *src)
{
    assert(src);
    assert(dst);

    if (TkBitNone(src) || TkIntSetIsEmpty(dst)) {
	return dst;
    }
    return TkIntSetRemoveBits(MakeCopyIfNeeded(dst), src);
}


TkIntSet *
TkTextTagSetJoin2(
    TkIntSet *dst,
    const TkIntSet *ts1,
    const TkIntSet *ts2)
{
    assert(dst);
    assert(ts1);
    assert(ts2);

    if (ts2 == dst || TkIntSetIsEmpty(ts2)) {
	return TkIntSetJoin(dst, ts1);
    }
    if (ts1 == dst || ts1 == ts2 || TkIntSetIsEmpty(ts1)) {
	return TkIntSetJoin(dst, ts2);
    }
    if (TkIntSetIsEmpty(dst)) {
	TkIntSetIncrRefCount((TkIntSet *) ts1); /* mutable by definition */
	TkIntSetDecrRefCount(dst);
	return TkIntSetJoin((TkIntSet *) ts1, ts2);
    }
    return TkIntSetJoin2(MakeCopyIfNeeded(dst), ts1, ts2);
}


TkIntSet *
TkTextTagSetComplementTo(
    TkIntSet *dst,
    const TkIntSet *src)
{
    assert(src);
    assert(dst);

    if (src == dst) {
	return ConvertToEmptySet(dst);
    }
    if (TkIntSetIsEmpty(src) || TkIntSetIsEmpty(dst)) {
	TkIntSetIncrRefCount((TkIntSet *) src); /* mutable by definition */
	TkIntSetDecrRefCount(dst);
	return (TkIntSet *) src;
    }
    return TkIntSetComplementTo(MakeCopyIfNeeded(dst), src);
}


TkIntSet *
TkTextTagSetJoinComplementTo(
    TkIntSet *dst,
    const TkIntSet *ts1,
    const TkIntSet *ts2)
{
    assert(dst);
    assert(ts1);
    assert(ts2);

    if (dst == ts2 || TkIntSetIsEmpty(ts2)) {
	return dst;
    }
    if (TkIntSetIsEmpty(ts1)) {
	return TkIntSetJoin(dst, ts2);
    }
    return TkIntSetJoinComplementTo(MakeCopyIfNeeded(dst), ts1, ts2);
}


TkIntSet *
TkTextTagSetJoinNonIntersection(
    TkIntSet *dst,
    const TkIntSet *ts1,
    const TkIntSet *ts2)
{
    assert(dst);
    assert(ts1);
    assert(ts2);

    if (ts1 == ts2) {
	return dst;
    }
    if (dst == ts1 || TkIntSetIsEmpty(ts1)) {
	return TkIntSetJoin(dst, ts2);
    }
    if (dst == ts2 || TkIntSetIsEmpty(ts2)) {
	return TkIntSetJoin(dst, ts1);
    }
    return TkIntSetJoinNonIntersection(MakeCopyIfNeeded(dst), ts1, ts2);
}


TkIntSet *
TkTextTagSetJoin2ComplementToIntersection(
    TkIntSet *dst,
    const TkIntSet *add,
    const TkIntSet *ts1,
    const TkIntSet *ts2)
{
    assert(dst);
    assert(add);
    assert(ts1);
    assert(ts2);

    if (ts1 == ts2) {
	return TkIntSetJoin(dst, add);
    }
    if (TkIntSetIsEmpty(ts1)) {
	return TkIntSetJoin2(dst, add, ts2);
    }
    if (TkIntSetIsEmpty(ts2)) {
	return TkIntSetJoin2(dst, add, ts1);
    }
    return TkIntSetJoin2ComplementToIntersection(MakeCopyIfNeeded(dst), add, ts1, ts2);
}


TkIntSet *
TkTextTagSetAdd(
    TkIntSet *dst,
    unsigned n)
{
    assert(dst);
    return TkIntSetAdd(MakeCopyIfNeeded(dst), n);
}


TkIntSet *
TkTextTagSetErase(
    TkIntSet *dst,
    unsigned n)
{
    assert(dst);
    return TkIntSetErase(MakeCopyIfNeeded(dst), n);
}


TkIntSet *
TkTextTagSetTestAndSet(
    TkIntSet *dst,
    unsigned n)
{
    assert(dst);

    if (dst->refCount <= 1) {
	return TkIntSetTestAndSet(dst, n);
    }
    if (TkIntSetTest(dst, n)) {
	return NULL;
    }
    dst->refCount -= 1;
    return TkIntSetAdd(TkIntSetCopy(dst), n);
}


TkIntSet *
TkTextTagSetTestAndUnset(
    TkIntSet *dst,
    unsigned n)
{
    assert(dst);

    if (dst->refCount <= 1) {
	return TkIntSetTestAndUnset(dst, n);
    }
    if (!TkIntSetTest(dst, n)) {
	return NULL;
    }
    dst->refCount -= 1;
    return TkIntSetErase(TkIntSetCopy(dst), n);
}


TkIntSet *
TkTextTagSetJoinOfDifferences(
    TkIntSet *dst,
    const TkIntSet *ts1,
    const TkIntSet *ts2)
{
    assert(dst);
    assert(ts1);
    assert(ts2);

    if (ts1 == ts2) {
	return TkIntSetRemove(dst, ts1);
    }
    if (dst == ts1) {
	TkIntSetDecrRefCount(dst);
	TkIntSetIncrRefCount((TkIntSet *) ts1); /* mutable due to concept */
	return TkIntSetRemove((TkIntSet *) ts1, ts2);
    }
    return TkIntSetJoinOfDifferences(MakeCopyIfNeeded(dst), ts1, ts2);
}

#endif  /* !TK_TEXT_USE_BITFIELDS */


#ifdef TK_C99_INLINE_SUPPORT
/* Additionally we need stand-alone object code. */
#if !TK_TEXT_DONT_USE_BITFIELDS
extern TkTextTagSet *TkTextTagSetNew(unsigned size);
extern unsigned TkTextTagSetRefCount(const TkTextTagSet *ts);
extern void TkTextTagSetIncrRefCount(TkTextTagSet *ts);
extern unsigned TkTextTagSetDecrRefCount(TkTextTagSet *ts);
extern TkTextTagSet *TkTextTagSetCopy(const TkTextTagSet *src);
extern bool TkTextTagSetIsEmpty(const TkTextTagSet *ts);
extern bool TkTextTagSetIsBitField(const TkTextTagSet *ts);
extern unsigned TkTextTagSetSize(const TkTextTagSet *ts);
extern unsigned TkTextTagSetCount(const TkTextTagSet *ts);
extern bool TkTextTagSetTest(const TkTextTagSet *ts, unsigned n);
extern bool TkTextTagSetNone(const TkTextTagSet *ts);
extern bool TkTextTagSetAny(const TkTextTagSet *ts);
extern bool TkTextTagSetIsEqual(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
extern bool TkTextTagSetContains(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
extern bool TkTextTagSetDisjunctive(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
extern bool TkTextTagSetIntersects(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
extern bool TkTextTagSetIntersectionIsEqual(const TkTextTagSet *ts1, const TkTextTagSet *ts2,
    const TkBitField *bf);
extern bool TkTextTagBitContainsSet(const TkBitField *bf, const TkTextTagSet *ts);
extern bool TkTextTagSetIsEqualBits(const TkTextTagSet *ts, const TkBitField *bf);
extern bool TkTextTagSetContainsBits(const TkTextTagSet *ts, const TkBitField *bf);
extern bool TkTextTagSetDisjunctiveBits(const TkTextTagSet *ts, const TkBitField *bf);
extern bool TkTextTagSetIntersectsBits(const TkTextTagSet *ts, const TkBitField *bf);
extern unsigned TkTextTagSetFindFirst(const TkTextTagSet *ts);
extern unsigned TkTextTagSetFindNext(const TkTextTagSet *ts, unsigned prev);
extern TkTextTagSet *TkTextTagSetAddOrErase(TkTextTagSet *ts, unsigned n, bool value);
extern TkTextTagSet *TkTextTagSetAddToThis(TkTextTagSet *ts, unsigned n);
extern TkTextTagSet *TkTextTagSetEraseFromThis(TkTextTagSet *ts, unsigned n);
extern unsigned TkTextTagSetRangeSize(const TkTextTagSet *ts);
extern const unsigned char *TkTextTagSetData(const TkTextTagSet *ts);
extern unsigned TkTextTagSetByteSize(const TkTextTagSet *ts);
#else /* integer set only implementation **************************************/
extern TkIntSet *TkTextTagSetNew(unsigned size);
extern TkIntSet *TkTextTagSetResize(TkIntSet *ts, unsigned newSize);
extern void TkTextTagSetDestroy(TkIntSet **tsPtr);
extern unsigned TkTextTagSetRefCount(const TkIntSet *ts);
extern void TkTextTagSetIncrRefCount(TkIntSet *ts);
extern unsigned TkTextTagSetDecrRefCount(TkIntSet *ts);
extern TkIntSet *TkTextTagSetCopy(const TkIntSet *src);
extern bool TkTextTagSetIsEmpty(const TkIntSet *ts);
extern bool TkTextTagSetIsBitField(const TkIntSet *ts);
extern unsigned TkTextTagSetSize(const TkIntSet *ts);
extern unsigned TkTextTagSetCount(const TkIntSet *ts);
extern bool TkTextTagSetTest(const TkIntSet *ts, unsigned n);
extern bool TkTextTagSetNone(const TkIntSet *ts);
extern bool TkTextTagSetAny(const TkIntSet *ts);
extern bool TkTextTagSetIsEqual(const TkIntSet *ts1, const TkIntSet *ts2);
extern bool TkTextTagSetContains(const TkIntSet *ts1, const TkIntSet *ts2);
extern bool TkTextTagSetDisjunctive(const TkIntSet *ts1, const TkIntSet *ts2);
extern bool TkTextTagSetIntersects(const TkIntSet *ts1, const TkIntSet *ts2);
extern bool TkTextTagSetIntersectionIsEqual(const TkIntSet *ts1, const TkIntSet *ts2,
    const TkBitField *bf);
extern bool TkTextTagBitContainsSet(const TkBitField *bf, const TkIntSet *ts);
extern bool TkTextTagSetIsEqualBits(const TkIntSet *ts, const TkBitField *bf);
extern bool TkTextTagSetContainsBits(const TkIntSet *ts, const TkBitField *bf);
extern bool TkTextTagSetDisjunctiveBits(const TkIntSet *ts, const TkBitField *bf);
extern bool TkTextTagSetIntersectsBits(const TkIntSet *ts, const TkBitField *bf);
extern unsigned TkTextTagSetFindFirst(const TkIntSet *ts);
extern unsigned TkTextTagSetFindNext(const TkIntSet *ts, unsigned prev);
extern unsigned TkTextTagSetFindFirstInIntersection(const TkIntSet *ts, const TkBitField *bf);
extern TkIntSet *TkTextTagSetAddOrErase(TkIntSet *ts, unsigned n, bool value);
extern TkIntSet *TkTextTagSetClear(TkIntSet *ts);
extern TkIntSet *TkTextTagSetAddToThis(TkTextTagSet *ts, unsigned n);
extern TkIntSet *TkTextTagSetEraseFromThis(TkTextTagSet *ts, unsigned n);
extern unsigned TkTextTagSetRangeSize(const TkIntSet *ts);
extern const unsigned char *TkTextTagSetData(const TkIntSet *ts);
extern unsigned TkTextTagSetByteSize(const TkIntSet *ts);
#endif /* !TK_TEXT_DONT_USE_BITFIELDS */
#endif /* __STDC_VERSION__ >= 199901L */

/* vi:set ts=8 sw=4: */
