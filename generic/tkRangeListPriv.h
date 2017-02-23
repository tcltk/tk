/*
 * tkRangeListPriv.h --
 *
 *	Private implementation for range list.
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKRANGELIST
# error "do not include this private header file"
#endif


#ifdef _TK_NEED_IMPLEMENTATION

#include <stddef.h>
#include <assert.h>


inline
int
TkRangeSpan(
    const TkRange *range)
{
    assert(range);
    return range->high - range->low + 1;
}


inline
bool
TkRangeTest(
    const TkRange *range,
    int value)
{
    assert(range);
    return range->low <= value && value <= range->high;
}


inline
bool
TkRangeListIsEmpty(
    const TkRangeList *ranges)
{
    assert(ranges);
    return ranges->size == 0;
}


inline
int
TkRangeListLow(
    const TkRangeList *ranges)
{
    assert(ranges);
    assert(!TkRangeListIsEmpty(ranges));
    return ranges->items[0].low;
}


inline
int
TkRangeListHigh(
    const TkRangeList *ranges)
{
    assert(ranges);
    assert(!TkRangeListIsEmpty(ranges));
    return ranges->items[ranges->size - 1].high;
}


inline
unsigned
TkRangeListSpan(
    const TkRangeList *ranges)
{
    assert(ranges);
    return ranges->size ? TkRangeListHigh(ranges) - TkRangeListLow(ranges) + 1 : 0;
}


inline
unsigned
TkRangeListSize(
    const TkRangeList *ranges)
{
    assert(ranges);
    return ranges->size;
}


inline
unsigned
TkRangeListCount(
    const TkRangeList *ranges)
{
    assert(ranges);
    return ranges->count;
}


inline
const TkRange *
TkRangeListAccess(
    const TkRangeList *ranges,
    unsigned index)
{
    assert(ranges);
    assert(index < TkRangeListSize(ranges));
    return &ranges->items[index];
}


inline
bool
TkRangeListContains(
    const TkRangeList *ranges,
    int value)
{
    return !!TkRangeListFind(ranges, value);
}


inline
bool
TkRangeListContainsRange(
    const TkRangeList *ranges,
    int low,
    int high)
{
    const TkRange *range = TkRangeListFind(ranges, low);
    return range && range->high <= high;
}


inline
const TkRange *
TkRangeListFirst(
    const TkRangeList *ranges)
{
    assert(ranges);
    return ranges->size == 0 ? NULL : ranges->items;
}


inline
const TkRange *
TkRangeListNext(
    const TkRangeList *ranges,
    const TkRange *item)
{
    assert(item);
    assert(ranges);
    assert(ranges->items <= item && item < ranges->items + ranges->size);
    return ++item == ranges->items + ranges->size ? NULL : item;
}


#undef _TK_NEED_IMPLEMENTATION
#endif /* _TK_NEED_IMPLEMENTATION */
/* vi:set ts=8 sw=4: */
