/*
 * tkSet.h --
 *
 *	This module implements an integer set.
 *
 * Copyright © 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKINTSET
#define _TKINTSET

#include "tkInt.h" /* required for inline support */

#if defined(__GNUC__) || defined(__clang__)
# define __warn_unused__ __attribute__((warn_unused_result))
#else
# define __warn_unused__
#endif

struct TkBitField;


typedef uint32_t TkIntSetType;

/*
 * The struct below will be shared with the struct TkBitField, so the first two
 * members must exactly match the first two members in struct TkBitField. In this
 * way we have a struct inheritance, based on the first two members. This
 * is portable due to C99 section 6.7.2.1 bullet point 13:
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

typedef struct TkIntSet {
    uint32_t refCount:31;
    uint32_t isSetFlag:1;
    TkIntSetType *end;
    TkIntSetType *curr; /* mutable */
    TkIntSetType buf[1];
} TkIntSet;


#define TK_SET_NPOS ((unsigned) -1)


TkIntSet *TkIntSetNew();
TkIntSet *TkIntSetFromBits(const struct TkBitField *bf);
void TkIntSetDestroy(TkIntSet **setPtr);

inline unsigned TkIntSetByteSize(const TkIntSet *set);
inline const unsigned char *TkIntSetData(const TkIntSet *set);

TkIntSet *TkIntSetCopy(const TkIntSet *set) __warn_unused__;

TkIntSet *TkIntSetJoin(TkIntSet *dst, const TkIntSet *src) __warn_unused__;
TkIntSet *TkIntSetIntersect(TkIntSet *dst, const TkIntSet *src) __warn_unused__;
TkIntSet *TkIntSetRemove(TkIntSet *dst, const TkIntSet *src) __warn_unused__;

TkIntSet *TkIntSetJoinBits(TkIntSet *dst, const struct TkBitField *src) __warn_unused__;
TkIntSet *TkIntSetIntersectBits(TkIntSet *dst, const struct TkBitField *src) __warn_unused__;
TkIntSet *TkIntSetRemoveBits(TkIntSet *dst, const struct TkBitField *src) __warn_unused__;
/* dst := src - dst */
TkIntSet *TkIntSetComplementToBits(TkIntSet *dst, const struct TkBitField *src) __warn_unused__;

/* dst := dst + set1 + set2 */
TkIntSet *TkIntSetJoin2(TkIntSet *dst, const TkIntSet *set1, const TkIntSet *set2) __warn_unused__;
/* dst := src - dst */
TkIntSet *TkIntSetComplementTo(TkIntSet *dst, const TkIntSet *src) __warn_unused__;
/* dst := dst + (set2 - set1) */
TkIntSet *TkIntSetJoinComplementTo(TkIntSet *dst, const TkIntSet *set1, const TkIntSet *set2)
    __warn_unused__;
/* dst := dst + (set1 - set2) + (set2 - set1) */
TkIntSet *TkIntSetJoinNonIntersection(TkIntSet *dst, const TkIntSet *set1, const TkIntSet *set2)
    __warn_unused__;
/* dst := dst + add + ((set1 + set2) - (set1 & set2)) */
TkIntSet *TkIntSetJoin2ComplementToIntersection(TkIntSet *dst,
    const TkIntSet *add, const TkIntSet *set1, const TkIntSet *set2) __warn_unused__;
/* dst := (dst - set1) + (set1 - set2) */
TkIntSet *TkIntSetJoinOfDifferences(TkIntSet *dst, const TkIntSet *set1, const TkIntSet *set2)
    __warn_unused__;

inline int TkIntSetIsEmpty(const TkIntSet *set);
inline unsigned TkIntSetSize(const TkIntSet *set);
inline unsigned TkIntSetMax(const TkIntSet *set);

inline unsigned TkIntSetRefCount(const TkIntSet *set);
inline void TkIntSetIncrRefCount(TkIntSet *set);
inline unsigned TkIntSetDecrRefCount(TkIntSet *set);

inline TkIntSetType TkIntSetAccess(const TkIntSet *set, unsigned index);

inline int TkIntSetTest(const TkIntSet *set, unsigned n);
inline int TkIntSetNone(const TkIntSet *set);
inline int TkIntSetAny(const TkIntSet *set);

inline int TkIntSetIsEqual(const TkIntSet *set1, const TkIntSet *set2);
inline int TkIntSetContains(const TkIntSet *set1, const TkIntSet *set2);
inline int TkIntSetDisjunctive(const TkIntSet *set1, const TkIntSet *set2);
inline int TkIntSetIntersects(const TkIntSet *set1, const TkIntSet *set2);

int TkIntSetIntersectionIsEqual(const TkIntSet *set1, const TkIntSet *set2,
    const struct TkBitField *del);
int TkIntSetIsEqualBits(const TkIntSet *set, const struct TkBitField *bf);
int TkIntSetContainsBits(const TkIntSet *set, const struct TkBitField *bf);
int TkIntSetDisjunctiveBits(const TkIntSet *set, const struct TkBitField *bf);
int TkIntSetIntersectionIsEqualBits(const TkIntSet *set, const struct TkBitField *bf,
    const struct TkBitField *del);
int TkIntSetIsContainedBits(const TkIntSet *set, const struct TkBitField *bf);

inline unsigned TkIntSetFindFirst(const TkIntSet *set);
inline unsigned TkIntSetFindNext(const TkIntSet *set);

unsigned TkIntSetFindFirstInIntersection(const TkIntSet *set, const struct TkBitField *bf);

TkIntSet *TkIntSetAdd(TkIntSet *set, unsigned n) __warn_unused__;
TkIntSet *TkIntSetErase(TkIntSet *set, unsigned n) __warn_unused__;
TkIntSet *TkIntSetTestAndSet(TkIntSet *set, unsigned n) __warn_unused__;
TkIntSet *TkIntSetTestAndUnset(TkIntSet *set, unsigned n) __warn_unused__;
inline TkIntSet *TkIntSetAddOrErase(TkIntSet *set, unsigned n, int add) __warn_unused__;
TkIntSet *TkIntSetClear(TkIntSet *set) __warn_unused__;

#ifndef NDEBUG
void TkIntSetPrint(const TkIntSet *set);
#endif


#ifdef TK_UNUSED_INTSET_FUNCTIONS

/*
 * These functions are not needed anymore, but shouldn't be removed, because sometimes
 * any of these functions might be useful.
 */

/* dst := (dst + (set - sub)) & set */
TkIntSet *TkIntSetInnerJoinDifference(TkIntSet *dst, const TkIntSet *set, const TkIntSet *sub)
    __warn_unused__;
/* ((set + (add - sub)) & add) == nil */
int TkIntSetInnerJoinDifferenceIsEmpty(const TkIntSet *set, const TkIntSet *add, const TkIntSet *sub);
/* set1 == set2 - sub2 */
int TkIntSetIsEqualToDifference(const TkIntSet *set1, const TkIntSet *set2, const TkIntSet *sub2);
/* set1 == set2 + (add2 & set2) */
int TkIntSetIsEqualToInnerJoin(const TkIntSet *set1, const TkIntSet *set2, const TkIntSet *add2);
/* set1 == ((set2 + (add2 - sub2)) & add2) */
int TkIntSetIsEqualToInnerJoinDifference(const TkIntSet *set1, const TkIntSet *set2,
    const TkIntSet *add2, const TkIntSet *sub2);
/* ((set1 + (add - sub)) & add) == ((set2 + (add - sub)) & add) */
int TkIntSetInnerJoinDifferenceIsEqual(const TkIntSet *set1, const TkIntSet *set2,
    const TkIntSet *add, const TkIntSet *sub);

#endif /* TK_UNUSED_INTSET_FUNCTIONS */


#undef __warn_unused__

#define _TK_NEED_IMPLEMENTATION
#include "tkIntSetPriv.h"
#endif /* _TKINTSET */
/* vi:set ts=8 sw=4: */
