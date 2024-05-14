/*
 * tkRangeList.h --
 *
 *	This module implements operations on a list of integer ranges.
 *	Note that the current implementation expects short lists of
 *	ranges, it is quite slow for large list sizes (large number of
 *	range items).
 *
 * Copyright © 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKRANGELIST
#define _TKRANGELIST

#include "tkInt.h" /* required for inline support */

#if defined(__GNUC__) || defined(__clang__)
# define __warn_unused__ __attribute__((warn_unused_result))
#else
# define __warn_unused__
#endif


typedef struct TkRange {
    int low;
    int high;
} TkRange;

/*
 * Return the span of given range.
 */
inline int TkRangeSpan(const TkRange *range);

/*
 * Test whether given range contains the specified value.
 */
inline int TkRangeTest(const TkRange *range, int value);


typedef struct TkRangeList {
    unsigned size;
    unsigned capacity;
    unsigned count;
    TkRange items[1];
} TkRangeList;

/*
 * Create a range list, and reserve some space for range entries.
 */
TkRangeList *TkRangeListCreate(unsigned capacity) __warn_unused__;

/*
 * Make a copy of given list.
 */
TkRangeList *TkRangeListCopy(const TkRangeList *ranges) __warn_unused__;

/*
 * Destroy the range list, the derefenced pointer can be NULL (in this case the
 * list is already destroyed).
 */
void TkRangeListDestroy(TkRangeList **rangesPtr);

/*
 * Clear the given list of ranges.
 */
void TkRangeListClear(TkRangeList *ranges);

/*
 * Truncate this list at front until given value (inclusive). This means that after
 * truncation the lowest value in this list will be larger than 'untilThisValue'.
 */
void TkRangeListTruncateAtFront(TkRangeList *ranges, int untilThisValue);

/*
 * Truncate this list at end with given value (exclusive). This means that after
 * truncation the highest value in this list will be less or equal to 'maxValue'.
 */
void TkRangeListTruncateAtEnd(TkRangeList *ranges, int maxValue);

/*
 * Return the lower value of the entry with lowest order (ths lowest value inside
 * the whole list).
 */
inline int TkRangeListLow(const TkRangeList *ranges);

/*
 * Return the upper value of the entry with highest order (ths highest value inside
 * the whole list).
 */
inline int TkRangeListHigh(const TkRangeList *ranges);

/*
 * Return the span of the whole list (= TkRangeListHigh(ranges) - TkRangeListLow(ranges) + 1).
 */
inline unsigned TkRangeListSpan(const TkRangeList *ranges);

/*
 * Return the number of integers contained in this list.
 */
inline unsigned TkRangeListCount(const TkRangeList *ranges);

/*
 * Return the number of entries (pairs) in this list.
 */
inline unsigned TkRangeListSize(const TkRangeList *ranges);

/*
 * Return a specific entry (pair), the index must not exceed the size of this list.
 */
inline const TkRange *TkRangeListAccess(const TkRangeList *ranges, unsigned index);

/*
 * Find entry (pair) which contains the given value. NULL will be returned if
 * this value is not contained in this list.
 */
const TkRange *TkRangeListFind(const TkRangeList *ranges, int value);

/*
 * Find entry (pair) which contains the given value. If this value is not contained
 * in given list then return the item with a low value nearest to specified value.
 * But it never returns an item with a high value less than given value, so it's
 * possible that NULL we returned.
 */
const TkRange *TkRangeListFindNearest(const TkRangeList *ranges, int value);

/*
 * Return the first item in given list, can be NULL if list is empty.
 */
inline const TkRange *TkRangeListFirst(const TkRangeList *ranges);

/*
 * Return the next item in given list, can be NULL if at end of list.
 */
inline const TkRange *TkRangeListNext(const TkRangeList *ranges, const TkRange *item);

/*
 * Return whether this list is empty.
 */
inline int TkRangeListIsEmpty(const TkRangeList *ranges);

/*
 * Return whether the given value is contained in this list.
 */
inline int TkRangeListContains(const TkRangeList *ranges, int value);

/*
 * Return whether the given range is contained in this list.
 */
inline int TkRangeListContainsRange(const TkRangeList *ranges, int low, int high);

/*
 * Return whether any value of the given range is contained in this list.
 */
int TkRangeListContainsAny(const TkRangeList *ranges, int low, int high);

/*
 * Add given range to this list. Adjacent entries (pairs) will be amalgamated
 * automatically.
 */
TkRangeList *TkRangeListAdd(TkRangeList *ranges, int low, int high) __warn_unused__;

/*
 * Remove given range from list. Adjacent entries (pairs) will be amalgamated
 * automatically.
 */
TkRangeList *TkRangeListRemove(TkRangeList *ranges, int low, int high);

/*
 * Insert given range to this list. This method has the side effect that all contained
 * integers with a value higher than the 'high' value will be increased by the span of
 * the given range (high - low + 1). Adjacent entries (pairs) will be amalgamated
 * automatically.
 *
 * Example: TkRangeListInsert({{5,6} {8,9}}, 1, 5) -> {{1,5} {10,11} {13,14}}
 */
TkRangeList *TkRangeListInsert(TkRangeList *ranges, int low, int high) __warn_unused__;

/*
 * Delete given range from list. This method has the side effect that all contained
 * integers with a value higher than the 'low' value will be decreased by the span of
 * the given range (high - low + 1). Adjacent entries (pairs) will be amalgamated
 * automatically.
 *
 * Example: TkRangeListDelete({{5,6} {8,9}}, 1, 5) -> {{1} {3,4}}
 */
TkRangeList *TkRangeListDelete(TkRangeList *ranges, int low, int high);

#ifndef NDEBUG
void TkRangeListPrint(const TkRangeList *ranges);
#endif


#define _TK_NEED_IMPLEMENTATION
#include "tkRangeListPriv.h"
#endif /* _TKRANGELIST */
/* vi:set ts=8 sw=4: */
