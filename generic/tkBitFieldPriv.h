/*
 * tkBitFieldPriv.h --
 *
 *	Private implementation for bit field.
 *
 * Copyright © 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKBITFIELD
# error "do not include this private header file"
#endif


#ifndef _TKBITFIELDPRIV
#define _TKBITFIELDPRIV

MODULE_SCOPE int TkBitNone_(const size_t *buf, unsigned words);

#endif /* _TKBITFIELDPRIV */


#ifdef _TK_NEED_IMPLEMENTATION

#include <assert.h>


#define TK_BIT_WORD_INDEX(n)	((n) >> ((TK_BIT_NBITS + 128) >> 5))
#define TK_BIT_INDEX(n)		((n) & (TK_BIT_NBITS - 1))
#define TK_BIT_MASK(n)		(((size_t) 1) << (n))
#define TK_BIT_COUNT_WORDS(n)	((n + TK_BIT_NBITS - 1)/TK_BIT_NBITS)


inline
const unsigned char *
TkBitData(
    const TkBitField *bf)
{
    assert(bf);
    return (const unsigned char *) bf->bits;
}


inline
unsigned
TkBitByteSize(
    const TkBitField *bf)
{
    assert(bf);
    return TK_BIT_COUNT_WORDS(bf->size);
}


inline
unsigned
TkBitAdjustSize(
    unsigned size)
{
    return ((size + (TK_BIT_NBITS - 1))/TK_BIT_NBITS)*TK_BIT_NBITS;
}


inline
TkBitField *
TkBitNew(
    unsigned size)
{
    TkBitField *bf = TkBitResize(NULL, size);
    bf->refCount = 0;
    return bf;
}


inline
unsigned
TkBitRefCount(
    const TkBitField *bf)
{
    assert(bf);
    return bf->refCount;
}


inline
void
TkBitIncrRefCount(
    TkBitField *bf)
{
    assert(bf);
    bf->refCount += 1;
}


inline
unsigned
TkBitDecrRefCount(
    TkBitField *bf)
{
    unsigned refCount;

    assert(bf);
    assert(TkBitRefCount(bf) > 0);

    if ((refCount = --bf->refCount) == 0) {
	TkBitDestroy(&bf);
    }
    return refCount;
}


inline
size_t
TkBitSize(
    const TkBitField *bf)
{
    assert(bf);
    return bf->size;
}


inline
int
TkBitIsEmpty(
    const TkBitField *bf)
{
    assert(bf);
    return bf->size == 0;
}


inline
int
TkBitNone(
    const TkBitField *bf)
{
    assert(bf);
    return bf->size == 0 || TkBitNone_(bf->bits, TK_BIT_COUNT_WORDS(bf->size));
}


inline
int
TkBitIntersects(
    const TkBitField *bf1,
    const TkBitField *bf2)
{
    return !TkBitDisjunctive(bf1, bf2);
}


inline
int
TkBitTest(
    const TkBitField *bf,
    unsigned n)
{
    assert(bf);
    assert(n < TkBitSize(bf));
    return !!(bf->bits[TK_BIT_WORD_INDEX(n)] & TK_BIT_MASK(TK_BIT_INDEX(n)));
}


inline
void
TkBitSet(
    TkBitField *bf,
    unsigned n)
{
    assert(bf);
    assert(n < TkBitSize(bf));
    bf->bits[TK_BIT_WORD_INDEX(n)] |= TK_BIT_MASK(TK_BIT_INDEX(n));
}


inline
void
TkBitUnset(
    TkBitField *bf,
    unsigned n)
{
    assert(bf);
    assert(n < TkBitSize(bf));
    bf->bits[TK_BIT_WORD_INDEX(n)] &= ~TK_BIT_MASK(TK_BIT_INDEX(n));
}


inline
void
TkBitPut(
    TkBitField *bf,
    unsigned n,
    int value)
{
    if (value) {
	TkBitSet(bf, n);
    } else {
	TkBitUnset(bf, n);
    }
}


#undef _TK_NEED_IMPLEMENTATION
#endif /* _TK_NEED_IMPLEMENTATION */
/* vi:set ts=8 sw=4: */
