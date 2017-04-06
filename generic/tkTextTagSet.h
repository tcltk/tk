/*
 * tkTextTagSet.h --
 *
 *	This module implements a set for tagging information. The real type
 *	is either a bit field, or a set of integers, depending on the size
 *	of the tag set.
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKTEXTTAGSET
#define _TKTEXTTAGSET

#include "tkInt.h"
#include "tkBitField.h"
#include "tkIntSet.h"
#include "tkBool.h"

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
 *
 * We will use the compiler constant TK_TEXT_DONT_USE_BITFIELDS for the choice (with or
 * without bitfields).
 */

/* This is common to both implementations. */
# define TK_TEXT_TAG_SET_NPOS TK_SET_NPOS


#if !TK_TEXT_DONT_USE_BITFIELDS /* shared implementation ****************************/

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

inline bool TkTextTagSetIsEmpty(const TkTextTagSet *ts);
inline bool TkTextTagSetIsBitField(const TkTextTagSet *ts);

inline unsigned TkTextTagSetSize(const TkTextTagSet *ts);
inline unsigned TkTextTagSetCount(const TkTextTagSet *ts);

inline bool TkTextTagSetTest(const TkTextTagSet *ts, unsigned n);
inline bool TkTextTagSetNone(const TkTextTagSet *ts);
inline bool TkTextTagSetAny(const TkTextTagSet *ts);

inline bool TkTextTagSetIsEqual(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
inline bool TkTextTagSetContains(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
inline bool TkTextTagSetDisjunctive(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
inline bool TkTextTagSetIntersects(const TkTextTagSet *ts1, const TkTextTagSet *ts2);
/* (ts1 & bf) == (ts2 & bf) */
inline bool TkTextTagSetIntersectionIsEqual(const TkTextTagSet *ts1, const TkTextTagSet *ts2,
    const TkBitField *bf);
inline bool TkTextTagBitContainsSet(const TkBitField *bf, const TkTextTagSet *ts);

inline bool TkTextTagSetIsEqualBits(const TkTextTagSet *ts, const TkBitField *bf);
inline bool TkTextTagSetContainsBits(const TkTextTagSet *ts, const TkBitField *bf);
inline bool TkTextTagSetDisjunctiveBits(const TkTextTagSet *ts, const TkBitField *bf);
inline bool TkTextTagSetIntersectsBits(const TkTextTagSet *ts, const TkBitField *bf);

inline unsigned TkTextTagSetFindFirst(const TkTextTagSet *ts);
inline unsigned TkTextTagSetFindNext(const TkTextTagSet *ts, unsigned prev);
unsigned TkTextTagSetFindFirstInIntersection(const TkTextTagSet *ts, const TkBitField *bf);

TkTextTagSet *TkTextTagSetAdd(TkTextTagSet *ts, unsigned n) __warn_unused__;
TkTextTagSet *TkTextTagSetErase(TkTextTagSet *ts, unsigned n) __warn_unused__;
inline TkTextTagSet *TkTextTagSetAddOrErase(TkTextTagSet *ts, unsigned n, bool value)
    __warn_unused__;
TkTextTagSet *TkTextTagSetTestAndSet(TkTextTagSet *ts, unsigned n) __warn_unused__;
TkTextTagSet *TkTextTagSetTestAndUnset(TkTextTagSet *ts, unsigned n) __warn_unused__;
TkTextTagSet *TkTextTagSetClear(TkTextTagSet *ts) __warn_unused__;

inline TkTextTagSet *TkTextTagSetAddToThis(TkTextTagSet *ts, unsigned n);
inline TkTextTagSet *TkTextTagSetEraseFromThis(TkTextTagSet *ts, unsigned n);

inline unsigned TkTextTagSetRangeSize(const TkTextTagSet *ts);

inline const unsigned char *TkTextTagSetData(const TkTextTagSet *ts);
inline unsigned TkTextTagSetByteSize(const TkTextTagSet *ts);

# ifndef NDEBUG
void TkTextTagSetPrint(const TkTextTagSet *set);
# endif


# if 0

/*
 * These functions are not needed anymore, but shouldn't be removed, because sometimes
 * any of these functions might be useful.
 */

/* dst := (dst + (ts - sub)) & ts */
TkTextTagSet *TkTextTagSetInnerJoinDifference(TkTextTagSet *dst,
    const TkTextTagSet *ts, const TkTextTagSet *sub) __warn_unused__;
/* ((ts + (add - sub)) & add) == nil */
bool TkTextTagSetInnerJoinDifferenceIsEmpty(const TkTextTagSet *ts,
    const TkTextTagSet *add, const TkTextTagSet *sub);
/* ts1 == ts2 - sub2 */
bool TkTextTagSetIsEqualToDifference(const TkTextTagSet *ts1,
    const TkTextTagSet *ts2, const TkTextTagSet *sub2);
/* ts1 == ts2 + (add2 & ts2) */
bool TkTextTagSetIsEqualToInnerJoin(const TkTextTagSet *ts1, const TkTextTagSet *ts2,
    const TkTextTagSet *add2);
/* ts1 == ((ts2 + (add2 - sub2)) & add2) */
bool TkTextTagSetIsEqualToInnerJoinDifference(const TkTextTagSet *ts1, const TkTextTagSet *ts2,
    const TkTextTagSet *add2, const TkTextTagSet *sub2);
/* ((ts1 + (add - sub)) & add) == ((ts2 + (add - sub)) & add) */
bool TkTextTagSetInnerJoinDifferenceIsEqual(const TkTextTagSet *ts1, const TkTextTagSet *ts2,
    const TkTextTagSet *add, const TkTextTagSet *sub);

# endif /* 0 */

#else /* integer set only implementation **************************************/

# define TkTextTagSet TkIntSet

inline TkIntSet *TkTextTagSetNew(unsigned size) __warn_unused__;
inline TkIntSet *TkTextTagSetResize(TkIntSet *ts, unsigned newSize) __warn_unused__;
inline void TkTextTagSetDestroy(TkIntSet **tsPtr);

inline unsigned TkTextTagSetRefCount(const TkIntSet *ts);
inline void TkTextTagSetIncrRefCount(TkIntSet *ts);
inline unsigned TkTextTagSetDecrRefCount(TkIntSet *ts);

inline TkIntSet *TkTextTagSetCopy(const TkIntSet *src) __warn_unused__;
TkBitField *TkTextTagSetToBits(const TkTextTagSet *src, int size) __warn_unused__;

TkIntSet *TkTextTagSetJoin(TkIntSet *dst, const TkIntSet *src) __warn_unused__;
TkIntSet *TkTextTagSetIntersect(TkIntSet *dst, const TkIntSet *src) __warn_unused__;
TkIntSet *TkTextTagSetRemove(TkIntSet *dst, const TkIntSet *src) __warn_unused__;

TkIntSet *TkTextTagSetIntersectBits(TkIntSet *dst, const TkBitField *src) __warn_unused__;
TkIntSet *TkTextTagSetRemoveBits(TkIntSet *dst, const TkBitField *src) __warn_unused__;

/* dst := dst + ts1 + ts2 */
TkIntSet *TkTextTagSetJoin2(TkIntSet *dst, const TkIntSet *ts1, const TkIntSet *ts2) __warn_unused__;
/* dst := src - dst */
TkIntSet *TkTextTagSetComplementTo(TkIntSet *dst, const TkIntSet *src) __warn_unused__;
/* dst := dst + (bf2 - bf1) */
TkIntSet *TkTextTagSetJoinComplementTo(TkIntSet *dst, const TkIntSet *ts1, const TkIntSet *ts2)
    __warn_unused__;
/* dst := dst + (set1 - set2) + (set2 - set1) */
TkIntSet *TkTextTagSetJoinNonIntersection(TkIntSet *dst, const TkIntSet *ts1, const TkIntSet *ts2)
    __warn_unused__;
/* dst := dst + add + ((ts1 + ts2) - (ts1 & ts2)) */
TkIntSet *TkTextTagSetJoin2ComplementToIntersection(TkIntSet *dst, const TkIntSet *add,
    const TkIntSet *ts1, const TkIntSet *ts2) __warn_unused__;
/* dst := (dst - ts1) + (ts1 - ts2) */
TkIntSet *TkTextTagSetJoinOfDifferences(TkIntSet *dst, const TkIntSet *ts1, const TkIntSet *ts2)
    __warn_unused__;

inline bool TkTextTagSetIsEmpty(const TkIntSet *ts);
inline bool TkTextTagSetIsBitField(const TkIntSet *ts);

inline unsigned TkTextTagSetSize(const TkIntSet *ts);
inline unsigned TkTextTagSetCount(const TkIntSet *ts);

inline bool TkTextTagSetTest(const TkIntSet *ts, unsigned n);
inline bool TkTextTagSetNone(const TkIntSet *ts);
inline bool TkTextTagSetAny(const TkIntSet *ts);

inline bool TkTextTagSetIsEqual(const TkIntSet *ts1, const TkIntSet *ts2);
inline bool TkTextTagSetContains(const TkIntSet *ts1, const TkIntSet *ts2);
inline bool TkTextTagSetDisjunctive(const TkIntSet *ts1, const TkIntSet *ts2);
inline bool TkTextTagSetIntersects(const TkIntSet *ts1, const TkIntSet *ts2);
/* (ts1 & bf) == (ts2 & bf) */
inline bool TkTextTagSetIntersectionIsEqual(const TkIntSet *ts1, const TkIntSet *ts2,
    const TkBitField *bf);
inline bool TkTextTagBitContainsSet(const TkBitField *bf, const TkIntSet *ts);

inline bool TkTextTagSetIsEqualBits(const TkIntSet *ts, const TkBitField *bf);
inline bool TkTextTagSetContainsBits(const TkIntSet *ts, const TkBitField *bf);
inline bool TkTextTagSetDisjunctiveBits(const TkIntSet *ts, const TkBitField *bf);
inline bool TkTextTagSetIntersectsBits(const TkIntSet *ts, const TkBitField *bf);

inline unsigned TkTextTagSetFindFirst(const TkIntSet *ts);
inline unsigned TkTextTagSetFindNext(const TkIntSet *ts, unsigned prev);
inline unsigned TkTextTagSetFindFirstInIntersection(const TkIntSet *ts, const TkBitField *bf);

TkIntSet *TkTextTagSetAdd(TkIntSet *ts, unsigned n) __warn_unused__;
TkIntSet *TkTextTagSetErase(TkIntSet *ts, unsigned n) __warn_unused__;
inline TkIntSet *TkTextTagSetAddOrErase(TkIntSet *ts, unsigned n, bool value) __warn_unused__;
TkIntSet *TkTextTagSetTestAndSet(TkIntSet *ts, unsigned n) __warn_unused__;
TkIntSet *TkTextTagSetTestAndUnset(TkIntSet *ts, unsigned n) __warn_unused__;
inline TkIntSet *TkTextTagSetClear(TkIntSet *ts) __warn_unused__;

inline TkIntSet *TkTextTagSetAddToThis(TkIntSet *ts, unsigned n);
inline TkIntSet *TkTextTagSetEraseFromThis(TkIntSet *ts, unsigned n);

inline unsigned TkTextTagSetRangeSize(const TkIntSet *ts);

inline const unsigned char *TkTextTagSetData(const TkIntSet *ts);
inline unsigned TkTextTagSetByteSize(const TkIntSet *ts);

#endif /* !TK_TEXT_DONT_USE_BITFIELDS */


#undef __warn_unused__

#ifdef TK_C99_INLINE_SUPPORT
# define _TK_NEED_IMPLEMENTATION
# include "tkTextTagSetPriv.h"
#endif
#endif /* _TKTEXTTAGSET */
/* vi:set ts=8 sw=4: */
