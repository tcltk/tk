/*
 * tkIntSetPriv.h --
 *
 *	Private implementation for integer set.
 *
 * Copyright © 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKINTSET
# error "do not include this private header file"
#endif


#ifndef _TKINTSETPRIV
#define _TKINTSETPRIV

MODULE_SCOPE int TkIntSetContains__(
    const TkIntSetType *set1, const TkIntSetType *end1,
    const TkIntSetType *set2, const TkIntSetType *end2);
MODULE_SCOPE int TkIntSetDisjunctive__(
    const TkIntSetType *set1, const TkIntSetType *end1,
    const TkIntSetType *set2, const TkIntSetType *end2);
MODULE_SCOPE int TkIntSetIsEqual__(
    const TkIntSetType *set1, const TkIntSetType *end1,
    const TkIntSetType *set2, const TkIntSetType *end2);

#endif /* _TKINTSETPRIV */


#ifdef _TK_NEED_IMPLEMENTATION

#include <assert.h>


extern TkIntSetType *
TkIntSetLowerBound(
    TkIntSetType *first,
    TkIntSetType *last,
    TkIntSetType value);


inline
const unsigned char *
TkIntSetData(
    const TkIntSet *set)
{
    assert(set);
    return (const unsigned char *) set->buf;
}


inline
unsigned
TkIntSetByteSize(
    const TkIntSet *set)
{
    assert(set);
    return (set->end - set->buf)*sizeof(TkIntSetType);
}


inline
int
TkIntSetIsEmpty(
    const TkIntSet *set)
{
    assert(set);
    return set->end == set->buf;
}


inline
int
TkIntSetIsEqual(
    const TkIntSet *set1,
    const TkIntSet *set2)
{
    assert(set1);
    assert(set2);

    return set1 == set2 || TkIntSetIsEqual__(set1->buf, set1->end, set2->buf, set2->end);
}


inline
int
TkIntSetContains(
    const TkIntSet *set1,
    const TkIntSet *set2)
{
    assert(set1);
    assert(set2);

    return set1 == set2 || TkIntSetContains__(set1->buf, set1->end, set2->buf, set2->end);
}


inline
int
TkIntSetDisjunctive(
    const TkIntSet *set1,
    const TkIntSet *set2)
{
    assert(set1);
    assert(set2);

    if (set1 == set2) {
	return TkIntSetIsEmpty(set1);
    }
    return TkIntSetDisjunctive__(set1->buf, set1->end, set2->buf, set2->end);
}


inline
unsigned
TkIntSetSize(
    const TkIntSet *set)
{
    assert(set);
    return set->end - set->buf;
}


inline
unsigned
TkIntSetMax(
    const TkIntSet *set)
{
    assert(!TkIntSetIsEmpty(set));
    return set->end[-1];
}


inline
unsigned
TkIntSetRefCount(
    const TkIntSet *set)
{
    assert(set);
    return set->refCount;
}


inline
void
TkIntSetIncrRefCount(TkIntSet *set)
{
    assert(set);
    set->refCount += 1;
}


inline
unsigned
TkIntSetDecrRefCount(TkIntSet *set)
{
    unsigned refCount;

    assert(set);
    assert(set->refCount > 0);

    if ((refCount = --set->refCount) == 0) {
	TkIntSetDestroy(&set);
    }
    return refCount;
}


inline
TkIntSetType
TkIntSetAccess(
    const TkIntSet *set,
    unsigned index)
{
    assert(set);
    assert(index < TkIntSetSize(set));
    return set->buf[index];
}


inline
void
TkIntSetChange(
    TkIntSet *set,
    unsigned index,
    unsigned n)
{
    assert(set);
    assert(index < TkIntSetSize(set));
    set->buf[index] = n;
}


inline
int
TkIntSetTest(
    const TkIntSet *set,
    unsigned n)
{
    const TkIntSetType *pos;

    assert(set);

    pos = TkIntSetLowerBound(((TkIntSet *) set)->buf, ((TkIntSet *) set)->end, n);
    return pos < set->end && *pos == n;
}


inline
int
TkIntSetNone(
    const TkIntSet *set)
{
    assert(set);
    return set->buf == set->end;
}


inline
int
TkIntSetAny(
    const TkIntSet *set)
{
    assert(set);
    return set->buf < set->end;
}


inline
int
TkIntSetIntersects(
    const TkIntSet *set1,
    const TkIntSet *set2)
{
    return !TkIntSetDisjunctive(set1, set2);
}


inline
unsigned
TkIntSetFindNext(
    const TkIntSet *set)
{
    assert(set);
    return set->curr == set->end ? TK_SET_NPOS : *(((TkIntSet *) set)->curr++); /* 'curr' is mutable */
}


inline
unsigned
TkIntSetFindFirst(
    const TkIntSet *set)
{
    assert(set);
    ((TkIntSet *) set)->curr = ((TkIntSet *) set)->buf; /* 'curr' is mutable */
    return TkIntSetFindNext(set);
}


inline
TkIntSet *
TkIntSetAddOrErase(
    TkIntSet *set,
    unsigned n,
    int add)
{
    assert(set);
    return add ? TkIntSetAdd(set, n) : TkIntSetErase(set, n);
}


#undef _TK_NEED_IMPLEMENTATION
#endif /* _TK_NEED_IMPLEMENTATION */
/* vi:set ts=8 sw=4: */
