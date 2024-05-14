/*
 * tkTextTagSet.h --
 *
 *	This module implements a set for tagging information. The real type
 *	is either a bit field, or a set of integers, depending on the size
 *	of the tag set.
 *
 * Copyright © 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKTEXTTAGSET
#define _TKTEXTTAGSET

#include "tkInt.h"
#include "tkBitField.h"
#include "tkIntSet.h"

#if defined(__GNUC__) || defined(__clang__)
# define __warn_unused__ __attribute__((warn_unused_result))
#else
# define __warn_unused__
#endif


/*
 * Currently our implementation is using a shared bitfield/integer set implementation.
 * Bitfields will be used as long as the number of tags is below a certain limit
 * (will be satisfied in most applications), but in some sophisticated applications
 * this limit will be exceeded, and in this case the integer set comes into play,
 * because a bitfield is too memory hungry with a large number of tags. Bitfields
 * are very, very fast, and integer sets are moderate in speed. So a bitfield will be
 * preferred. Nevertheless this implementation might be a bit over the top, probably
 * an implementation only with integer sets is already satisfactory.
 *
 * NOTE: The bit field implementation shouldn't be removed, even if this implementation
 * will not be used, because it is required for testing the integer set (TkIntSet).
 */

/* This is common to both implementations. */
# define TK_TEXT_TAG_SET_NPOS TK_SET_NPOS


/*
 * The struct below is using C inheritance, this is portable due to C99 section
 * 6.7.2.1 bullet point 13:
 *
 *	Within a structure object, the non-bit-field members and the units
 *	in which bit-fields reside have addresses that increase in the order
 *	in which they are declared. A pointer to a structure object, suitably
 *	converted, points to its initial member (or if that member is a
 *	bit-field, then to the unit in which it resides), and vice versa.
 *	There may be unnamed padding within a structure object, but not at
 *	beginning.
 *
 * This inheritance concept is also used in the portable GTK library.
 */

typedef struct TkTextTagSetBase {
    uint32_t refCount:31;
    uint32_t isSetFlag:1;
} TkTextTagSetBase;

typedef union TkTextTagSet {
    TkTextTagSetBase base;
    TkBitField bf;
    TkIntSet set;
} TkTextTagSet;


inline TkTextTagSet *TkTextTagSetNew(unsigned size) __warn_unused__;
TkTextTagSet *TkTextTagSetResize(TkTextTagSet *ts, unsigned newSize) __warn_unused__;
void TkTextTagSetDestroy(TkTextTagSet **tsPtr);

inline unsigned TkTextTagSetRefCount(const TkTextTagSet *ts);
inline void TkTextTagSetIncrRefCount(TkTextTagSet *ts);
inline unsigned TkTextTagSetDecrRefCount(TkTextTagSet *ts);

inline TkTextTagSet *TkTextTagSetCopy(const TkTextTagSet *src) __warn_unused__;
TkBitField *TkTextTagSetToBits(const TkTextTagSet *src, int size) __warn_unused__;

TkTextTagSet *TkTextTagSetJoin(TkTextTagSet *dst, const TkTextTagSet *src) __warn_unused__;
TkTextTagSet *TkTextTagSetIntersect(TkTextTagSet *dst, const TkTextTagSet *src) __warn_unused__;
TkTextTagSet *TkTextTagSetRemove(TkTextTagSet *dst, const TkTextTagSet *src) __warn_unused__;

TkTextTagSet *TkTextTagSetIntersectBits(TkTextTagSet *dst, const TkBitField *src) __warn_unused__;
TkTextTagSet *TkTextTagSetRemoveBits(TkTextTagSet *dst, const TkBitField *src) __warn_unused__;

/* dst := dst + ts1 + ts2 */
TkTextTagSet *TkTextTagSetJoin2(TkTextTagSet *dst, const TkTextTagSet *ts1, const TkTextTagSet *ts2)
    __warn_unused__;
/* dst := src - dst */
TkTextTagSet *TkTextTagSetComplementTo(TkTextTagSet *dst, const TkTextTagSet *src) __warn_unused__;
/* dst := dst + (ts2 - ts1) */
TkTextTagSet *TkTextTagSetJoinComplementTo(TkTextTagSet *dst,
    const TkTextTagSet *ts1, const TkTextTagSet *ts2) __warn_unused__;
/* dst := dst + (ts1 - ts2) + (ts2 - ts1) */
TkTextTagSet *TkTextTagSetJoinNonIntersection(TkTextTagSet *dst,
    const TkTextTagSet *ts1, const TkTextTagSet *ts2) __warn_unused__;
/* dst := dst + add + ((ts1 + ts2) - (ts1 & ts2)) */
TkTextTagSet *TkTextTagSetJoin2ComplementToIntersection(TkTextTagSet *dst,
    const TkTextTagSet *add, const TkTextTagSet *ts1, const TkTextTagSet *ts2) __warn_unused__;
/* dst := (dst - ts1) + (ts1 - ts2) */
TkTextTagSet *TkTextTagSetJoinOfDifferences(TkTextTagSet *dst, const TkTextTagSet *ts1,
    const TkTextTagSet *ts2) __warn_unused__;

inline int TkTextTagSetIsEmpty(const TkTextTagSet *ts);
inline int TkTextTagSetIsBitField(const TkTextTagSet *ts);

inline unsigned TkTextTagSetSize(const TkTextTagSet *ts);
inline unsigned TkTextTagSetCount(const TkTextTagSet *ts);

inline int TkTextTagSetTest(const TkTextTagSet *ts, unsigned n);
inline int TkTextTagSetNone(const TkTextTagSet *ts);
inline int TkTextTagSetAny(const TkTextTagSet *ts);

inline int TkTextTagSetIsEqual(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
inline int TkTextTagSetContains(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
inline int TkTextTagSetDisjunctive(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
inline int TkTextTagSetIntersects(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
/* (ts1 & bf) == (ts2 & bf) */
inline int TkTextTagSetIntersectionIsEqual(const TkTextTagSet *ts1, const TkTextTagSet *ts2,
    const TkBitField *bf);
inline int TkTextTagBitContainsSet(const TkBitField *bf, const TkTextTagSet *ts);

inline int TkTextTagSetIsEqualBits(const TkTextTagSet *ts, const TkBitField *bf);
inline int TkTextTagSetContainsBits(const TkTextTagSet *ts, const TkBitField *bf);
inline int TkTextTagSetDisjunctiveBits(const TkTextTagSet *ts, const TkBitField *bf);
inline int TkTextTagSetIntersectsBits(const TkTextTagSet *ts, const TkBitField *bf);

inline unsigned TkTextTagSetFindFirst(const TkTextTagSet *ts);
inline unsigned TkTextTagSetFindNext(const TkTextTagSet *ts, unsigned prev);
unsigned TkTextTagSetFindFirstInIntersection(const TkTextTagSet *ts, const TkBitField *bf);

TkTextTagSet *TkTextTagSetAdd(TkTextTagSet *ts, unsigned n) __warn_unused__;
TkTextTagSet *TkTextTagSetErase(TkTextTagSet *ts, unsigned n) __warn_unused__;
inline TkTextTagSet *TkTextTagSetAddOrErase(TkTextTagSet *ts, unsigned n, int value)
    __warn_unused__;
TkTextTagSet *TkTextTagSetTestAndSet(TkTextTagSet *ts, unsigned n) __warn_unused__;
TkTextTagSet *TkTextTagSetTestAndUnset(TkTextTagSet *ts, unsigned n) __warn_unused__;
TkTextTagSet *TkTextTagSetClear(TkTextTagSet *ts) __warn_unused__;

inline TkTextTagSet *TkTextTagSetAddToThis(TkTextTagSet *ts, unsigned n) __warn_unused__;
inline TkTextTagSet *TkTextTagSetEraseFromThis(TkTextTagSet *ts, unsigned n) __warn_unused__;
TkTextTagSet *TkTextTagSetRemoveFromThis(TkTextTagSet *dst, const TkTextTagSet *src) __warn_unused__;
TkTextTagSet *TkTextTagSetIntersectThis(TkTextTagSet *dst, const TkTextTagSet *src) __warn_unused__;

inline unsigned TkTextTagSetRangeSize(const TkTextTagSet *ts);

inline const unsigned char *TkTextTagSetData(const TkTextTagSet *ts);
inline unsigned TkTextTagSetByteSize(const TkTextTagSet *ts);

# ifndef NDEBUG
void TkTextTagSetPrint(const TkTextTagSet *set);
# endif

# ifdef TK_UNUSED_TAGSET_FUNCTIONS

/*
 * These functions are not needed anymore, but shouldn't be removed, because sometimes
 * any of these functions might be useful.
 */

/* dst := (dst + (ts - sub)) & ts */
TkTextTagSet *TkTextTagSetInnerJoinDifference(TkTextTagSet *dst,
    const TkTextTagSet *ts, const TkTextTagSet *sub) __warn_unused__;
/* ((ts + (add - sub)) & add) == nil */
int TkTextTagSetInnerJoinDifferenceIsEmpty(const TkTextTagSet *ts,
    const TkTextTagSet *add, const TkTextTagSet *sub);
/* ts1 == ts2 - sub2 */
int TkTextTagSetIsEqualToDifference(const TkTextTagSet *ts1,
    const TkTextTagSet *ts2, const TkTextTagSet *sub2);
/* ts1 == ts2 + (add2 & ts2) */
int TkTextTagSetIsEqualToInnerJoin(const TkTextTagSet *ts1, const TkTextTagSet *ts2,
    const TkTextTagSet *add2);
/* ts1 == ((ts2 + (add2 - sub2)) & add2) */
int TkTextTagSetIsEqualToInnerJoinDifference(const TkTextTagSet *ts1, const TkTextTagSet *ts2,
    const TkTextTagSet *add2, const TkTextTagSet *sub2);
/* ((ts1 + (add - sub)) & add) == ((ts2 + (add - sub)) & add) */
int TkTextTagSetInnerJoinDifferenceIsEqual(const TkTextTagSet *ts1, const TkTextTagSet *ts2,
    const TkTextTagSet *add, const TkTextTagSet *sub);

# endif /* TK_UNUSED_TAGSET_FUNCTIONS */

#undef __warn_unused__

#define _TK_NEED_IMPLEMENTATION
#include "tkTextTagSetPriv.h"
#endif /* _TKTEXTTAGSET */
/* vi:set ts=8 sw=4: */
