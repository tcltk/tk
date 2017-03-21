/*
 * tkBitField.h --
 *
 *	This module implements bit field operations.
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKBITFIELD
#define _TKBITFIELD

#include "tk.h"
#include "tkInt.h" /* needed for inline support */
#include "tkBool.h"

#ifdef TCL_WIDE_INT_IS_LONG
typedef uint64_t TkBitWord;
#else
typedef uint32_t TkBitWord;
#endif

#define TK_BIT_NBITS (sizeof(TkBitWord)*8) /* Number of bits in one word. */

struct TkIntSet;


/*
 * The struct below will be shared with the struct TkIntSet, so the first two
 * members must exactly match the first two members in struct TkIntSet. In this
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

typedef struct TkBitField {
    uint32_t refCount:31;
    uint32_t isSetFlag:1;
    uint32_t size;
#if TK_CHECK_ALLOCS
    struct TkBitField *next;
    struct TkBitField *prev;
    unsigned number;
#endif
    TkBitWord bits[1];
} TkBitField;


/*
 * This value will be returned in case of end of iteration.
 */
#define TK_BIT_NPOS ((unsigned) -1)


inline TkBitField *TkBitNew(unsigned size);
TkBitField *TkBitResize(TkBitField *bf, unsigned newSize);
TkBitField *TkBitFromSet(const struct TkIntSet *set, unsigned size);
void TkBitDestroy(TkBitField **bfPtr);

inline const unsigned char *TkBitData(const TkBitField *bf);
inline unsigned TkBitByteSize(const TkBitField *bf);

inline unsigned TkBitRefCount(const TkBitField *bf);
inline void TkBitIncrRefCount(TkBitField *bf);
inline unsigned TkBitDecrRefCount(TkBitField *bf);

TkBitField *TkBitCopy(const TkBitField *bf, int size);

void TkBitJoin(TkBitField *dst, const TkBitField *src);
void TkBitIntersect(TkBitField *dst, const TkBitField *src);
void TkBitRemove(TkBitField *dst, const TkBitField *src);

/* dst := dst + bf1 + bf2 */
void TkBitJoin2(TkBitField *dst, const TkBitField *bf1, const TkBitField *bf2);
/* dst := src - dst */
void TkBitComplementTo(TkBitField *dst, const TkBitField *src);
/* dst := dst + (bf2 - bf1) */
void TkBitJoinComplementTo(TkBitField *dst, const TkBitField *bf1, const TkBitField *bf2);
/* dst := dst + (bf1 - bf2) + (bf2 - bf1) */
void TkBitJoinNonIntersection(TkBitField *dst, const TkBitField *bf1, const TkBitField *bf2);
/* dst := dst + add + ((bf1 + bf2) - (bf1 & bf2)) */
void TkBitJoin2ComplementToIntersection(TkBitField *dst,
    const TkBitField *add, const TkBitField *bf1, const TkBitField *bf2);
/* dst := (dst - bf1) + (bf1 - bf2) */
void TkBitJoinOfDifferences(TkBitField *dst, const TkBitField *bf1, const TkBitField *bf2);

inline bool TkBitIsEmpty(const TkBitField *bf);
inline unsigned TkBitSize(const TkBitField *bf);
unsigned TkBitCount(const TkBitField *bf);

inline bool TkBitTest(const TkBitField *bf, unsigned n);
inline bool TkBitNone(const TkBitField *bf);
bool TkBitAny(const TkBitField *bf);
bool TkBitComplete(const TkBitField *bf);

bool TkBitIsEqual(const TkBitField *bf1, const TkBitField *bf2);
bool TkBitContains(const TkBitField *bf1, const TkBitField *bf2);
bool TkBitDisjunctive(const TkBitField *bf1, const TkBitField *bf2);
inline bool TkBitIntersects(const TkBitField *bf1, const TkBitField *bf2);
bool TkBitIntersectionIsEqual(const TkBitField *bf1, const TkBitField *bf2, const TkBitField *del);

unsigned TkBitFindFirst(const TkBitField *bf);
unsigned TkBitFindLast(const TkBitField *bf);
unsigned TkBitFindFirstNot(const TkBitField *bf);
unsigned TkBitFindLastNot(const TkBitField *bf);
unsigned TkBitFindNext(const TkBitField *bf, unsigned prev);
unsigned TkBitFindNextNot(const TkBitField *bf, unsigned prev);
unsigned TkBitFindPrev(const TkBitField *bf, unsigned prev);
unsigned TkBitFindFirstInIntersection(const TkBitField *bf1, const TkBitField *bf2);

inline void TkBitSet(TkBitField *bf, unsigned n);
inline void TkBitUnset(TkBitField *bf, unsigned n);
inline void TkBitPut(TkBitField *bf, unsigned n, bool value);
bool TkBitTestAndSet(TkBitField *bf, unsigned n);
bool TkBitTestAndUnset(TkBitField *bf, unsigned n);
void TkBitFill(TkBitField *bf);
void TkBitClear(TkBitField *bf);

/* Return nearest multiple of TK_BIT_NBITS which is greater or equal to given argument. */
inline unsigned TkBitAdjustSize(unsigned size);

#ifndef NDEBUG
void TkBitPrint(const TkBitField *bf);
#endif

#if TK_CHECK_ALLOCS
void TkBitCheckAllocs();
#endif


#if TK_UNUSED_BITFIELD_FUNCTIONS

/*
 * These functions are not needed anymore, but shouldn't be removed, because sometimes
 * any of these functions might be useful.
 */

/* dst := (dst + (add - sub)) & add */
void TkBitInnerJoinDifference(TkBitField *dst, const TkBitField *add, const TkBitField *sub);
/* ((bf + (add - sub)) & add) == nil */
bool TkBitInnerJoinDifferenceIsEmpty(const TkBitField *bf, const TkBitField *add, const TkBitField *sub);
/* bf1 == bf2 - sub2 */
bool TkBitIsEqualToDifference(const TkBitField *bf1, const TkBitField *bf2, const TkBitField *sub2);
/* bf1 == ((bf2 + add2) & bf2) */
bool TkBitIsEqualToInnerJoin(const TkBitField *bf1, const TkBitField *bf2, const TkBitField *add2);
/* bf1 == ((bf2 + (add2 - sub2) & add) */
bool TkBitIsEqualToInnerJoinDifference(const TkBitField *bf1, const TkBitField *bf2,
    const TkBitField *add2, const TkBitField *sub2);
/* ((bf1 + (add - sub)) & add) == ((bf2 + (add - sub)) & add) */
bool TkBitInnerJoinDifferenceIsEqual(const TkBitField *bf1, const TkBitField *bf2,
    const TkBitField *add, const TkBitField *sub);

#endif /* TK_UNUSED_BITFIELD_FUNCTIONS */

#ifdef TK_C99_INLINE_SUPPORT
# define _TK_NEED_IMPLEMENTATION
# include "tkBitFieldPriv.h"
#endif
#endif /* _TKBITFIELD */
/* vi:set ts=8 sw=4: */
