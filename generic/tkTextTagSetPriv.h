/*
 * tkTextTagSetPriv.h --
 *
 *	Private implementation.
 *
 * Copyright © 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKTEXTTAGSET
# error "do not include this private header file"
#endif


#ifndef _TKTEXTTAGSETPRIV
#define _TKTEXTTAGSETPRIV

/*
 * The constant TK_TEXT_SET_MAX_BIT_SIZE is defining the upper bound of
 * the bit size in bit fields. This means that if more than TK_TEXT_SET_MAX_BIT_SIZE
 * tags are in usage, the tag set is using integer sets instead of bit fields,
 * because large bit fields are exploding the memory usage.
 *
 * The constant TK_TEXT_SET_MAX_BIT_SIZE must be a multiple of TK_BIT_NBITS.
 */

#ifdef TK_IS_64_BIT_ARCH

/*
 * On 64 bit systems this is the optimal size and it is not recommended to
 * choose a lower size.
 */
# define TK_TEXT_SET_MAX_BIT_SIZE (((512 + TK_BIT_NBITS - 1)/TK_BIT_NBITS)*TK_BIT_NBITS)

#else /* TK_IS_64_BIT_ARCH */

/*
 * On 32 bit systems the current size (512) might be too large. If so it should
 * be reduced to 256, but it is not recommended to define a lower constant than
 * 256.
 */
# define TK_TEXT_SET_MAX_BIT_SIZE (((512 + TK_BIT_NBITS - 1)/TK_BIT_NBITS)*TK_BIT_NBITS)

#endif /* TK_IS_64_BIT_ARCH */


MODULE_SCOPE int TkTextTagSetIsEqual_(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
MODULE_SCOPE int TkTextTagSetContains_(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
MODULE_SCOPE int TkTextTagSetDisjunctive_(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
MODULE_SCOPE int TkTextTagSetIntersectionIsEqual_(const TkTextTagSet *ts1, const TkTextTagSet *ts2,
		    const TkBitField *bf);

#endif /* _TKTEXTTAGSETPRIV */


#ifdef _TK_NEED_IMPLEMENTATION

#ifndef _TK
#include "tk.h"
#endif

#include <assert.h>


inline
TkTextTagSet *
TkTextTagSetNew(
    unsigned size)
{
    if (size <= TK_TEXT_SET_MAX_BIT_SIZE) {
	return (TkTextTagSet *) TkBitNew(size);
    }
    return (TkTextTagSet *) TkIntSetNew();
}


inline
unsigned
TkTextTagSetRefCount(
    const TkTextTagSet *ts)
{
    assert(ts);
    return ts->base.refCount;
}


inline
void
TkTextTagSetIncrRefCount(
    TkTextTagSet *ts)
{
    assert(ts);
    ts->base.refCount += 1;
}


inline
unsigned
TkTextTagSetDecrRefCount(
    TkTextTagSet *ts)
{
    unsigned refCount;

    assert(ts);
    assert(TkTextTagSetRefCount(ts) > 0);

    if ((refCount = --ts->base.refCount) == 0) {
	TkTextTagSetDestroy(&ts);
    }
    return refCount;
}


inline
int
TkTextTagSetIsEmpty(
    const TkTextTagSet *ts)
{
    assert(ts);
    return ts->base.isSetFlag ? TkIntSetIsEmpty(&ts->set) : TkBitNone(&ts->bf);
}


inline
int
TkTextTagSetIsBitField(
    const TkTextTagSet *ts)
{
    assert(ts);
    return !ts->base.isSetFlag;
}


inline
unsigned
TkTextTagSetSize(
    const TkTextTagSet *ts)
{
    assert(ts);
    return ts->base.isSetFlag ? TK_TEXT_TAG_SET_NPOS - 1 : TkBitSize(&ts->bf);
}


inline
unsigned
TkTextTagSetRangeSize(
    const TkTextTagSet *ts)
{
    assert(ts);

    if (!ts->base.isSetFlag) {
	return TkBitSize(&ts->bf);
    }
    return TkIntSetIsEmpty(&ts->set) ? 0 : TkIntSetMax(&ts->set) + 1;
}


inline
unsigned
TkTextTagSetCount(
    const TkTextTagSet *ts)
{
    assert(ts);
    return ts->base.isSetFlag ? TkIntSetSize(&ts->set) : TkBitCount(&ts->bf);
}


inline
int
TkTextTagSetIsEqual(
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2)
{
    assert(ts1);
    assert(ts2);

    if (ts1->base.isSetFlag || ts2->base.isSetFlag) {
	return TkTextTagSetIsEqual_(ts1, ts2);
    }
    return TkBitIsEqual(&ts1->bf, &ts2->bf);
}


inline
int
TkTextTagSetContains(
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2)
{
    assert(ts1);
    assert(ts2);

    if (ts1->base.isSetFlag || ts2->base.isSetFlag) {
	return TkTextTagSetContains_(ts1, ts2);
    }
    return TkBitContains(&ts1->bf, &ts2->bf);
}


inline
int
TkTextTagSetDisjunctive(
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2)
{
    assert(ts1);
    assert(ts2);

    if (ts1->base.isSetFlag || ts2->base.isSetFlag) {
	return TkTextTagSetDisjunctive_(ts1, ts2);
    }
    return TkBitDisjunctive(&ts1->bf, &ts2->bf);
}


inline
int
TkTextTagSetIntersects(
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2)
{
    return !TkTextTagSetDisjunctive(ts1, ts2);
}


inline
int
TkTextTagSetIntersectionIsEqual(
    const TkTextTagSet *ts1,
    const TkTextTagSet *ts2,
    const TkBitField *bf)
{
    assert(ts1);
    assert(ts2);

    if (ts1->base.isSetFlag || ts2->base.isSetFlag) {
	return TkTextTagSetIntersectionIsEqual_(ts1, ts2, bf);
    }
    return TkBitIntersectionIsEqual(&ts1->bf, &ts2->bf, bf);
}


inline
int
TkTextTagBitContainsSet(
    const TkBitField *bf,
    const TkTextTagSet *ts)
{
    return ts->base.isSetFlag ? TkIntSetIsContainedBits(&ts->set, bf) : TkBitContains(bf, &ts->bf);
}


inline
int
TkTextTagSetIsEqualBits(
    const TkTextTagSet *ts,
    const TkBitField *bf)
{
    assert(ts);
    assert(bf);
    return ts->base.isSetFlag ? TkIntSetIsEqualBits(&ts->set, bf) : TkBitIsEqual(&ts->bf, bf);
}


inline
int
TkTextTagSetContainsBits(
    const TkTextTagSet *ts,
    const TkBitField *bf)
{
    assert(ts);
    assert(bf);
    return ts->base.isSetFlag ? TkIntSetContainsBits(&ts->set, bf) : TkBitContains(&ts->bf, bf);
}


inline
int
TkTextTagSetDisjunctiveBits(
    const TkTextTagSet *ts,
    const TkBitField *bf)
{
    assert(ts);
    assert(bf);
    return ts->base.isSetFlag ? TkIntSetDisjunctiveBits(&ts->set, bf) : TkBitDisjunctive(&ts->bf, bf);
}


inline
int
TkTextTagSetIntersectsBits(
    const TkTextTagSet *ts,
    const TkBitField *bf)
{
    return !TkTextTagSetDisjunctiveBits(ts, bf);
}


inline
int
TkTextTagSetTest(
    const TkTextTagSet *ts,
    unsigned n)
{
    assert(ts);

    if (ts->base.isSetFlag) {
	return TkIntSetTest(&ts->set, n);
    }
    return n < TkBitSize(&ts->bf) && TkBitTest(&ts->bf, n);
}


inline
int
TkTextTagSetNone(
    const TkTextTagSet *ts)
{
    assert(ts);
    return ts->base.isSetFlag ? TkIntSetNone(&ts->set) : TkBitNone(&ts->bf);
}


inline
int
TkTextTagSetAny(
    const TkTextTagSet *ts)
{
    assert(ts);
    return ts->base.isSetFlag ? TkIntSetAny(&ts->set) : TkBitAny(&ts->bf);
}


inline
TkTextTagSet *
TkTextTagSetCopy(
    const TkTextTagSet *src)
{
    assert(src);

    if (src->base.isSetFlag) {
	return (TkTextTagSet *) TkIntSetCopy(&src->set);
    }
    return (TkTextTagSet *) TkBitCopy(&src->bf, -1);
}


inline
unsigned
TkTextTagSetFindFirst(
    const TkTextTagSet *ts)
{
    assert(ts);
    return ts->base.isSetFlag ? TkIntSetFindFirst(&ts->set) : TkBitFindFirst(&ts->bf);
}


inline
unsigned
TkTextTagSetFindNext(
    const TkTextTagSet *ts,
    unsigned prev)
{
    assert(ts);
    return ts->base.isSetFlag ? TkIntSetFindNext(&ts->set) :  TkBitFindNext(&ts->bf, prev);
}


inline
TkTextTagSet *
TkTextTagSetAddOrErase(
    TkTextTagSet *ts,
    unsigned n,
    int value)
{
    assert(ts);
    return value ? TkTextTagSetAdd(ts, n) : TkTextTagSetErase(ts, n);
}


inline
TkTextTagSet *
TkTextTagSetAddToThis(
    TkTextTagSet *ts,
    unsigned n)
{
    assert(ts);
    assert(n < TkTextTagSetSize(ts));

    if (ts->base.isSetFlag) {
	ts = (TkTextTagSet *) TkIntSetAdd(&ts->set, n);
    } else {
	TkBitSet(&ts->bf, n);
    }
    return ts;
}


inline
TkTextTagSet *
TkTextTagSetEraseFromThis(
    TkTextTagSet *ts,
    unsigned n)
{
    assert(ts);
    assert(n < TkTextTagSetSize(ts));

    if (ts->base.isSetFlag) {
	ts = (TkTextTagSet *) TkIntSetErase(&ts->set, n);
    } else {
	TkBitUnset(&ts->bf, n);
    }
    return ts;
}


inline
const unsigned char *
TkTextTagSetData(
    const TkTextTagSet *ts)
{
    assert(ts);
    return ts->base.isSetFlag ? TkIntSetData(&ts->set) : TkBitData(&ts->bf);
}


inline
unsigned
TkTextTagSetByteSize(
    const TkTextTagSet *ts)
{
    assert(ts);
    return ts->base.isSetFlag ? TkIntSetByteSize(&ts->set) : TkBitByteSize(&ts->bf);
}

#undef _TK_NEED_IMPLEMENTATION
#endif /* _TK_NEED_IMPLEMENTATION */
/* vi:set ts=8 sw=4: */
