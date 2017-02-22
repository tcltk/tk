/*
 * tkRangeList.c --
 *
 *	This module implements operations on a list of integer ranges.
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkRangeList.h"

#include <tk.h>
#include <string.h>
#include <assert.h>

#if !(__STDC_VERSION__ >= 199901L || (defined(_MSC_VER) && _MSC_VER >= 1900))
# define _TK_NEED_IMPLEMENTATION
# include "tkRangeListPriv.h"
#endif

#ifndef MIN
# define MIN(a,b) ((a) < (b) ? a : b)
#endif
#ifndef MAX
# define MAX(a,b) ((a) < (b) ? b : a)
#endif

#if TK_CHECK_ALLOCS
# define DEBUG_ALLOC(expr) expr
#else
# define DEBUG_ALLOC(expr)
#endif


#define MEM_SIZE(size) ((unsigned) (Tk_Offset(TkRangeList, items) + (size)*sizeof(TkRange)))


DEBUG_ALLOC(unsigned tkRangeListCountNew = 0);
DEBUG_ALLOC(unsigned tkRangeListCountDestroy = 0);


#if !NDEBUG

static int
ComputeRangeSize(
    const TkRangeList *ranges)
{
    unsigned i;
    int count = 0;

    for (i = 0; i < ranges->size; ++i) {
	count += TkRangeSpan(ranges->items + i);
    }

    return count;
}

#endif /* !NDEBUG */


static TkRange *
LowerBound(
    TkRange *first,
    TkRange *last,
    int low)
{
    /*
     * Note that we want to amalgamate adjacent ranges, and this binary
     * search is designed for this requirement.
     *
     * Example for ranges={{2,3}{6,7}}:
     *
     *   low < 5       -> {2,3}
     *   low = 5,6,7,8 -> {6,7}
     *   low > 8       -> last
     */

    if (first == last) {
	return first;
    }

    low -= 1;

    do {
	TkRange *mid = first + (last - first)/2;

	if (mid->high < low) {
	    first = mid + 1;
	} else {
	    last = mid;
	}
    } while (first != last);

    return first;
}


static TkRange *
Increase(
    TkRangeList **rangesPtr)
{
    TkRangeList *ranges = *rangesPtr;

    if (ranges->size == ranges->capacity) {
	ranges->capacity = MAX(1, 2*ranges->capacity);
	ranges = ckrealloc(ranges, MEM_SIZE(ranges->capacity));
	*rangesPtr = ranges;
    }

    return ranges->items + ranges->size++;
}


static TkRange *
Insert(
    TkRangeList **rangesPtr,
    TkRange *entry)
{
    TkRangeList *ranges = *rangesPtr;
    unsigned pos = entry - ranges->items;

    if (ranges->size == ranges->capacity) {
	TkRangeList *newRanges;
	TkRange *newEntry;

	ranges->capacity = MAX(1, 2*ranges->capacity);
	newRanges = ckalloc(MEM_SIZE(ranges->capacity));
	newRanges->capacity = ranges->capacity;
	newRanges->size = ranges->size + 1;
	newRanges->count = ranges->count;
	newEntry = newRanges->items + pos;
	memcpy(newRanges->items, ranges->items, pos*sizeof(TkRange));
	memcpy(newEntry + 1, entry, (ranges->size - pos)*sizeof(TkRange));
	ckfree(ranges);
	*rangesPtr = ranges = newRanges;
	entry = newEntry;
    } else {
	memmove(entry + 1, entry, (ranges->size - pos)*sizeof(TkRange));
	ranges->size += 1;
    }

    return entry;
}


static void
Amalgamate(
    TkRangeList *ranges,
    TkRange *curr)
{
    const TkRange *last = ranges->items + ranges->size;
    TkRange *next = curr + 1;
    int high = curr->high;

    while (next != last && high + 1 >= next->low) {
	if (high >= next->high) {
	    ranges->count -= TkRangeSpan(next);
	} else if (high >= next->low) {
	    ranges->count -= high - next->low + 1;
	}
	next += 1;
    }

    if (next != curr + 1) {
	curr->high = MAX((next - 1)->high, high);
	memmove(curr + 1, next, (last - next)*sizeof(TkRange));
	ranges->size -= (next - curr) - 1;
	assert(ComputeRangeSize(ranges) == ranges->count);
    }
}


TkRangeList *
TkRangeListCreate(unsigned capacity)
{
    TkRangeList *ranges;

    ranges = ckalloc(MEM_SIZE(capacity));
    ranges->size = 0;
    ranges->capacity = capacity;
    ranges->count = 0;
    DEBUG_ALLOC(tkRangeListCountNew++);
    return ranges;
}


TkRangeList *
TkRangeListCopy(
    const TkRangeList *ranges)
{
    TkRangeList *copy;
    unsigned memSize;

    assert(ranges);

    copy = ckalloc(memSize = MEM_SIZE(ranges->size));
    memcpy(copy, ranges, memSize);
    DEBUG_ALLOC(tkRangeListCountNew++);
    return copy;
}


void
TkRangeListDestroy(
    TkRangeList **rangesPtr)
{
    assert(rangesPtr);

    if (*rangesPtr) {
	ckfree(*rangesPtr);
	*rangesPtr = NULL;
	DEBUG_ALLOC(tkRangeListCountDestroy++);
    }
}


void
TkRangeListClear(
    TkRangeList *ranges)
{
    assert(ranges);

    ranges->size = 0;
    ranges->count = 0;
}


bool
TkRangeListContainsAny(
    const TkRangeList *ranges,
    int low,
    int high)
{
    const TkRange *last;
    const TkRange *entry;

    assert(ranges);

    last = ranges->items + ranges->size;
    entry = LowerBound((TkRange *) ranges->items, (TkRange *) last, low);

    if (entry == last) {
	return false;
    }

    if (entry->high == low + 1 && ++entry == last) {
	return false;
    }

    return high >= entry->low;
}


void
TkRangeListTruncateAtFront(
    TkRangeList *ranges,
    int untilThisValue)
{
    TkRange *last;
    TkRange *curr;
    TkRange *r;

    assert(ranges);

    last = ranges->items + ranges->size;
    curr = LowerBound(ranges->items, last, untilThisValue);

    if (curr == last) {
	return;
    }

    if (curr->low <= untilThisValue) {
	if (untilThisValue < curr->high) {
	    ranges->count -= untilThisValue - curr->low + 1;
	    curr->low = untilThisValue + 1;
	} else {
	    curr += 1;
	}
    }

    if (curr != ranges->items) {
	for (r = ranges->items; r != curr; ++r) {
	    ranges->count -= TkRangeSpan(r);
	}
	memmove(ranges->items, curr, (last - curr)*sizeof(TkRange));
	ranges->size -= curr - ranges->items;
    }

    assert(ComputeRangeSize(ranges) == ranges->count);
}


void
TkRangeListTruncateAtEnd(
    TkRangeList *ranges,
    int maxValue)
{
    TkRange *last;
    TkRange *curr;

    assert(ranges);

    last = ranges->items + ranges->size;
    curr = LowerBound(ranges->items, last, maxValue);

    if (curr == last) {
	return;
    }

    if (curr->low <= maxValue) {
	if (curr->high > maxValue) {
	    ranges->count -= curr->high - maxValue;
	    curr->high = maxValue;
	}
	curr += 1;
    }

    ranges->size -= last - curr;

    for ( ; curr != last; ++curr) {
	ranges->count -= TkRangeSpan(curr);
    }

    assert(ComputeRangeSize(ranges) == ranges->count);
}


const TkRange *
TkRangeListFind(
    const TkRangeList *ranges,
    int value)
{
    const TkRange *last;
    const TkRange *entry;

    assert(ranges);

    last = ranges->items + ranges->size;
    entry = LowerBound((TkRange *) ranges->items, (TkRange *) last, value);

    if (entry == last || entry->low > value || value > entry->high) {
	return NULL;
    }
    return entry;
}


const TkRange *
TkRangeListFindNearest(
    const TkRangeList *ranges,
    int value)
{
    const TkRange *last;
    const TkRange *entry;

    assert(ranges);

    last = ranges->items + ranges->size;
    entry = LowerBound((TkRange *) ranges->items, (TkRange *) last, value);

    if (entry == last) {
	return NULL;
    }
    if (value > entry->high) {
	if (++entry == last) {
	    return NULL;
	}
    }
    return entry;
}


TkRangeList *
TkRangeListAdd(
    TkRangeList *ranges,
    int low,
    int high)
{
    TkRange *last;
    TkRange *curr;

    assert(low <= high);

    last = ranges->items + ranges->size;

    if (ranges->size == 0) {
	curr = last;
    } else if (low >= (last - 1)->low) {
	/* catch a frequent case */
	curr = (low > (last - 1)->high + 1) ? last : last - 1;
    } else {
	curr = LowerBound(ranges->items, last, low);
    }

    if (curr == last) {
	/* append new entry */
	curr = Increase(&ranges);
	curr->low = low;
	curr->high = high;
	ranges->count += high - low + 1;
    } else if (low + 1 < curr->low) {
	if (curr->low <= high + 1) {
	    /* new lower bound of current range */
	    ranges->count += curr->low - low;
	    curr->low = low;
	} else {
	    /* insert new entry before current */
	    curr = Insert(&ranges, curr);
	    curr->low = low;
	    curr->high = high;
	    ranges->count += high - low + 1;
	}
    } else {
	if (low + 1 == curr->low) {
	    /* new lower bound of current range */
	    ranges->count += 1;
	    curr->low = low;
	}
	if (last - 1 != curr && (last - 1)->high <= high) {
	    /* catch a frequent case: we don't need the succeeding items */
	    for (--last; last > curr; --last) {
		ranges->count -= TkRangeSpan(last);
	    }
	    ranges->count += high - curr->high;
	    ranges->size = (curr + 1) - ranges->items;
	    curr->high = high;
	} else if (curr->high < high) {
	    /* new upper bound of current range */
	    ranges->count += high - curr->high;
	    curr->high = high;
	    /* possibly we have to amalgamate succeeding items */
	    Amalgamate(ranges, curr);
	}
    }

    return ranges;
}


TkRangeList *
TkRangeListInsert(
    TkRangeList *ranges,
    int low,
    int high)
{
    TkRange *curr;
    TkRange *last;
    int span = high - low + 1;

    assert(ranges);
    assert(low <= high);

    last = ranges->items + ranges->size;
    curr = LowerBound(ranges->items, last, low);

    /* {2,2} : insert {0,0} -> {0,0}{3,3} */
    /* {2,2} : insert {1,1} -> {1,1}{3,3} */
    /* {2,2} : insert {2,2} -> {2,3} */
    /* {2,2} : insert {3,3} -> {2,3} */
    /* {2,4} : insert {3,3} -> {2,5} */
    /* {2,4} : insert {4,4} -> {2,5} */
    /* {2,4} : insert {5,5} -> {2,5} */

    if (curr == last || low > curr->high + 1) {
	/* append new entry */
	curr = Increase(&ranges);
	curr->low = low;
	curr->high = high;
    } else {
	if (low >= curr->low) {
	    /* new upper bound of current range */
	    curr->high += span;
	} else {
	    /* insert new entry before current */
	    curr = Insert(&ranges, curr);
	    curr->low = low;
	    curr->high = high;
	}
	/* adjust all successors */
	last = ranges->items + ranges->size;
	for (++curr; curr != last; ++curr) {
	    curr->low += span;
	    curr->high += span;
	}
    }

    ranges->count += span;
    assert(ComputeRangeSize(ranges) == ranges->count);
    return ranges;
}


TkRangeList *
TkRangeListRemove(
    TkRangeList *ranges,
    int low,
    int high)
{
    TkRange *curr;
    TkRange *last;
    int span;

    assert(ranges);
    assert(low <= high);

    if (ranges->size == 0) {
	return ranges;
    }

    last = ranges->items + ranges->size;
    low = MAX(low, ranges->items[0].low);
    high = MIN(high, (last - 1)->high);

    if (low > high) {
	return ranges;
    }

    span = high - low + 1;
    curr = LowerBound(ranges->items, last, low);

    if (curr != last) {
	TkRange *next;
	unsigned size;

	if (high < curr->high) {
	    if (curr->low < low) {
		/* Example: cur:{1,4} - arg:{2,3} -> {1,1}{4,4} */
		int h = curr->high;
		ranges->count -= span;
		curr->high = low - 1;
		low = high + 1;
		curr = (curr == last) ? Increase(&ranges) : Insert(&ranges, curr + 1);
		curr->low = low;
		curr->high = h;
	    } else if (curr->low <= high) {
		/* Example: cur:{1,4} - arg:{1,3} -> {4,4} */
		int low = high + 1;
		ranges->count -= low - curr->low;
		curr->low = low;
	    }
	} else {
	    if (curr->low < low && low <= curr->high) {
		/* Example: cur:{1,7} - arg:{2,5} -> {1,1} */
		/* Example: cur:{1,3} - arg:{3,6} -> {1,2} */
		/* Example: cur:{1,1} - arg:{2,5} -> {1,1} */
		int high = low - 1;
		ranges->count -= curr->high - high;
		curr->high = high;
		curr += 1;
	    } else if (curr->high < low) {
		curr += 1;
	    }

	    for (next = curr; next != last && next->high <= high; ++next) {
		ranges->count -= TkRangeSpan(next);
	    }

	    memmove(curr, next, (last - next)*sizeof(TkRange));
	    ranges->size -= (size = next - curr);
	    last -= size;

	    if (curr != last) {
		if (curr->low <= high) {
		    ranges->count -= high + 1 - curr->low;
		    curr->low = high + 1;
		}
	    }
	}
    }

    assert(ComputeRangeSize(ranges) == ranges->count);
    return ranges;
}


TkRangeList *
TkRangeListDelete(
    TkRangeList *ranges,
    int low,
    int high)
{
    TkRange *curr;
    TkRange *last;
    int span;
    int lower;

    assert(ranges);
    assert(low <= high);

    if (ranges->size == 0 || low > TkRangeListHigh(ranges)) {
	return ranges;
    }

    last = ranges->items + ranges->size;
    span = high - low + 1;
    low = MAX(low, TkRangeListLow(ranges));
    high = MIN(high, TkRangeListHigh(ranges));
    curr = LowerBound(ranges->items, last, low);
    lower = high;

    if (curr != last) {
	TkRange *next;
	unsigned size;

	if (curr->low < low && low <= curr->high) {
	    /* Example: cur:{1,7} - arg:{2,5} -> {1,3} */
	    /* Example: cur:{1,3} - arg:{3,6} -> {1,2} */
	    /* Example: cur:{1,1} - arg:{2,5} -> {1,1} */
	    int high = MAX(low - 1, curr->high - span);
	    ranges->count -= curr->high - high;
	    curr->high = high;
	    next = curr + 1;
	    if (next != last && curr->high + 1 >= next->low - span) {
		/* Example: curr:{0,3}{8,9}{29,33} - arg:{1,30} -> {0,3} */
		lower = curr->low;
	    } else {
		curr = next;
	    }
	} else if (curr->high < low) {
	    curr += 1;
	}

	for (next = curr; next != last && next->high <= high; ++next) {
	    ranges->count -= TkRangeSpan(next);
	}

	memmove(curr, next, (last - next)*sizeof(TkRange));
	ranges->size -= (size = next - curr);
	last -= size;

	if (curr != last) {
	    if (lower < curr->low) {
		lower += span;
		ranges->count -= lower - curr->low;
		curr->low = lower;
	    } else if (curr->low <= high) {
		ranges->count -= high + 1 - curr->low;
		curr->low = high + 1;
	    }
	    for (next = curr; next != last; next += 1) {
		next->low -= span;
		next->high -= span;
	    }
	}
    }

    assert(ComputeRangeSize(ranges) == ranges->count);
    return ranges;
}


#if !NDEBUG

void
TkRangeListPrint(
    const TkRangeList *ranges)
{
    unsigned i;

    for (i = 0; i < ranges->size; ++i) {
	printf("{%d,%d} ", ranges->items[i].low, ranges->items[i].high);
    }
    printf("(%d)\n", ranges->count);
}

#endif /* !NDEBUG */


#if __STDC_VERSION__ >= 199901L || (defined(_MSC_VER) && _MSC_VER >= 1900)
/* Additionally we need stand-alone object code. */
#define inline extern
inline int TkRangeSpan(const TkRange *range);
inline bool TkRangeTest(const TkRange *range, int value);
inline int TkRangeListLow(const TkRangeList *ranges);
inline int TkRangeListHigh(const TkRangeList *ranges);
inline unsigned TkRangeListSpan(const TkRangeList *ranges);
inline unsigned TkRangeListCount(const TkRangeList *ranges);
inline unsigned TkRangeListSize(const TkRangeList *ranges);
inline const TkRange *TkRangeListAccess(const TkRangeList *ranges, unsigned index);
inline const TkRange *TkRangeListFirst(const TkRangeList *ranges);
inline const TkRange *TkRangeListNext(const TkRangeList *ranges, const TkRange *item);
inline bool TkRangeListIsEmpty(const TkRangeList *ranges);
inline bool TkRangeListContains(const TkRangeList *ranges, int value);
inline bool TkRangeListContainsRange(const TkRangeList *ranges, int low, int high);
#endif /* __STDC_VERSION__ >= 199901L */

/* vi:set ts=8 sw=4: */
