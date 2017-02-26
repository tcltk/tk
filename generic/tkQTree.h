/*
 * tkQTree.h --
 *
 *	Declarations for the Q-Tree implementation.
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKQTREE
#define _TKQTREE

#include "tkInt.h" /* required for inline support */
#include "tkBool.h"

#if HAVE_STDINT_H
# include <stdint.h>
#elif defined(_MSC_VER) && _MSC_VER < 1600
/* work around for the support of ancient compilers */
# include "tkWinStdInt.h"
#else
/* this is not expected with compilers from this century, except MSVC (handled above) */
# error "C99 support is required - can't find stdint.h"
#endif


/* =========================================================================
 * Definitions for rectangle support.
 * ========================================================================= */

typedef int32_t TkQTreeCoord;

typedef struct TkQTreeRect {
    TkQTreeCoord xmin, ymin, xmax, ymax;
} TkQTreeRect;

/* Return whether rectangle is empty? */
inline bool TkQTreeRectIsEmpty(const TkQTreeRect *rect);

/* Return whether both rectangles are equal. */
inline bool TkQTreeRectIsEqual(const TkQTreeRect *rect1, const TkQTreeRect *rect2);

/* Return whether this rectangle contains the specified point. */
inline bool TkQTreeRectContainsPoint(const TkQTreeRect *rect, TkQTreeCoord x, TkQTreeCoord y);

/* Return whether the first rectangle contains the second one. */
inline bool TkQTreeRectContainsRect(const TkQTreeRect *rect1, const TkQTreeRect *rect2);

/* Return whether both rectangles are overlapping. */
inline bool TkQTreeRectIntersects(const TkQTreeRect *rect1, const TkQTreeRect *rect2);

/* Setup a rectangle. */
inline TkQTreeRect *TkQTreeRectSet(TkQTreeRect *rect,
    TkQTreeCoord xmin, TkQTreeCoord ymin, TkQTreeCoord xmax, TkQTreeCoord ymax);

/* Translate a rectangle. */
inline TkQTreeRect *TkQTreeRectTranslate(TkQTreeRect *rect, TkQTreeCoord dx, TkQTreeCoord dy);

/* =========================================================================
 * Definitions for the Q-Tree (Quartering Tree).
 * ========================================================================= */

typedef int32_t TkQTreeState;
typedef uintptr_t TkQTreeUid;
typedef void *TkQTreeClientData;

struct TkQTree;
typedef struct TkQTree *TkQTree;

/*
 * This is defining the callback function for searching and traversing the tree.
 * Note that argument 'rect' will contain the coordinates according to the current
 * scroll position. When returning false the search will be terminated, otherwise
 * the search continues.
 */
typedef bool (*TkQTreeCallback)(
    TkQTreeUid uid, const TkQTreeRect *rect, TkQTreeState *state, TkQTreeClientData arg);

/*
 * Configure the dimensions of the given Q-Tree. A new tree will be created if
 * given tree (derefernced treePtr) is NULL. This function returns false if the
 * specified bounding box is empty, in this case the tree cannot be used.
 */
bool TkQTreeConfigure(TkQTree *treePtr, const TkQTreeRect *rect);

/*
 * Destroy the given tree. Will do nothing if given tree (derefernced treePtr)
 * is NULL.
 */
void TkQTreeDestroy(TkQTree *treePtr);

/*
 * Return the bounding box of given tree.
 */
const TkQTreeRect *TkQTreeGetBoundingBox(const TkQTree tree);

/*
 * Insert a rectangle into the tree. Each rectangle must be associated with an
 * unique ID (argument 'uid'). This function returns whether the insertion was
 * successful. The insertion fails if the given rectangle is empty, or if it
 * does not overlap with the bounding box of the tree, in these case the
 * return value will be false.
 */
bool TkQTreeInsertRect(TkQTree tree, const TkQTreeRect *rect, TkQTreeUid uid, TkQTreeState initialState);

/*
 * Update the rectangle which belongs to the given unique ID. The argument
 * 'oldRect' must exactly match the last provided rectangle (with
 * TkQTreeInsertRect, or TkQTreeUpdateRect). For convenience it is allowed
 * to insert a new rectangle (this means new unique ID), in this case
 * argument 'oldRect' must be NULL. This function returns whether the
 * insertion/update was successful. The insertion/update fails if the new
 * rectangle is empty, or if it does not overlap with the bounding box of
 * the tree, in these cases the return value will be false.
 */
bool TkQTreeUpdateRect(TkQTree tree, const TkQTreeRect *oldRect,
    const TkQTreeRect *newRect, TkQTreeUid uid, TkQTreeState newState);

/*
 * Delete the specified rectangle from the tree. It is mandatory that the specified
 * rectangle is exactly matching the last provided rectangle for given uid (with
 * TkQTreeInsertRect, or TkQTreeUpdateRect). This function returns whether the
 * deletion was successful.
 */
bool TkQTreeDeleteRect(TkQTree tree, const TkQTreeRect *rect, TkQTreeUid uid);

/*
 * Search for all rectangles containing the given point. For each hit the given
 * function will be triggered. This function returns the number of hits. It is
 * allowed to use NULL for argument 'cbHit'.
 */
unsigned TkQTreeSearch(const TkQTree tree, TkQTreeCoord x, TkQTreeCoord y,
    TkQTreeCallback cbHit, TkQTreeClientData cbArg);

/*
 * Trigger the given callback function for any rectangle in this tree.
 */
void TkQTreeTraverse(const TkQTree tree, TkQTreeCallback cbHit, TkQTreeClientData cbArg);

/*
 * Find the current state of the specified rectangle. This functions returns true
 * if and only if the specified rectangle has been found. It is allowed to use
 * NULL for argument 'state', in this case only the existence of the specified
 * rectangle will be tested.
 */
bool TkQTreeFindState(const TkQTree tree, const TkQTreeRect *rect, TkQTreeUid uid, TkQTreeState *state);

/*
 * Set the current state of the specified rectangle. This functions returns true
 * if successful, otherwise, if the specified rectangle is not exisiting, false
 * will be returned.
 */
bool TkQTreeSetState(const TkQTree tree, const TkQTreeRect *rect, TkQTreeUid uid, TkQTreeState state);

#if QTREE_SEARCH_RECTS_CONTAINING

/*
 * Search for all rectangles which are containing the given rectangle. Note that
 * here the user will receive NULL for parameter 'state' in callback function,
 * this means that this parameter cannot be used with the use of this function.
 * This function returns the number of hits. It is allowed to use NULL for
 * argument 'cbHit'.
 */

unsigned TkQTreeSearchRectsContaining(const TkQTree tree, const TkQTreeRect *rect,
    TkQTreeCallback cbHit, TkQTreeClientData cbArg);

#endif /* QTREE_SEARCH_RECTS_CONTAINING */


#ifdef TK_C99_INLINE_SUPPORT
# define _TK_NEED_IMPLEMENTATION
# include "tkQTreePriv.h"
#endif
#endif /* _TKQTREE */
/* vi:set ts=8 sw=4: */
