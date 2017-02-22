/*
 * tkQTreePriv.h --
 *
 *	Private implementation for Q-Tree.
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKQTREE
# error "do not include this private header file"
#endif


#ifdef _TK_NEED_IMPLEMENTATION

#include <assert.h>

#ifndef _MSC_VER
# if __STDC_VERSION__ < 199901L
#  define inline /* we are not C99 conform */
# endif
#endif


inline
bool
TkQTreeRectIsEmpty(
    const TkQTreeRect *rect)
{
    assert(rect);

    /*
     * Normally this natural statement will be used here:
     *
     *     return rect->xmin >= rect->xmax || rect->ymin >= rect->ymax;
     *
     * GCC gives the following warning when optimization is enabled:
     *
     *     assuming signed overflow does not occur when assuming that (X + c) < X
     *     is always false [-Wstrict-overflow]
     *
     * I guess that GCC is converting (optimizing) this statement into:
     *
     *     return rect->xmin - rect->xmax >= 0 || rect->ymin - rect->ymax >= 0;
     *
     * So I will use the latter statement to avoid this warning. Unfortunately
     * this has the drawback that on machines with different architectures
     * the transformed statement can be a bit slower than the natural statement,
     * but I will assume that on such architectures GCC will transform it (back)
     * to the natural one (when optimization is enabled), without giving a warning,
     * because the natural statement doesn't use arithmetic.
     *
     * Note that this problem only happen with signed values.
     */

    return rect->xmin - rect->xmax >= 0 || rect->ymin - rect->ymax >= 0;
}


inline
bool
TkQTreeRectIsEqual(
    const TkQTreeRect *rect1,
    const TkQTreeRect *rect2)
{
    assert(rect1);
    assert(rect2);

    return rect1->xmin == rect2->xmin
    	&& rect1->xmax == rect2->xmax
	&& rect1->ymin == rect2->ymin
	&& rect1->ymax == rect2->ymax;
}


inline
bool
TkQTreeRectContainsPoint(
    const TkQTreeRect *rect,
    TkQTreeCoord x,
    TkQTreeCoord y)
{
    assert(rect);

    return y < rect->ymax && rect->ymin <= y && x < rect->xmax && rect->xmin <= x;
}


inline
bool
TkQTreeRectContainsRect(
    const TkQTreeRect *rect1,
    const TkQTreeRect *rect2)
{
    assert(rect1);
    assert(rect2);

    return rect1->xmin <= rect2->xmin
    	&& rect2->xmax <= rect1->xmax
	&& rect1->ymin <= rect2->ymin
	&& rect2->ymax <= rect1->ymax;
}


inline
bool
TkQTreeRectIntersects(
    const TkQTreeRect *rect1,
    const TkQTreeRect *rect2)
{
    assert(rect1);
    assert(rect2);

    return rect1->xmin < rect2->xmax
	&& rect2->xmin < rect1->xmax
	&& rect1->ymin < rect2->ymax
	&& rect2->ymin < rect1->ymax;
}


inline
TkQTreeRect *
TkQTreeRectSet(
    TkQTreeRect *rect,
    TkQTreeCoord xmin,
    TkQTreeCoord ymin,
    TkQTreeCoord xmax,
    TkQTreeCoord ymax)
{
    assert(rect);
    assert(xmin <= xmax);
    assert(ymin <= ymax);

    rect->xmin = xmin;
    rect->ymin = ymin;
    rect->xmax = xmax;
    rect->ymax = ymax;

    return rect;
}


inline
TkQTreeRect *
TkQTreeRectTranslate(
    TkQTreeRect *rect,
    TkQTreeCoord dx,
    TkQTreeCoord dy)
{
    assert(rect);

    rect->xmin += dx;
    rect->xmax += dx;
    rect->ymin += dy;
    rect->ymax += dy;

    return rect;
}

#undef _TK_NEED_IMPLEMENTATION
#endif /* _TK_NEED_IMPLEMENTATION */
/* vi:set ts=8 sw=4: */
