/*
 * tkQTree.c --
 *
 * This module provides an implementation of a Q-Tree (Quartering Tree) for
 * fast search of rectangles containing a specific point. This provides very
 * fast mouse hovering lookup, even insertion/deletion/update is fast.
 *
 * The search algorithm is working with binary division on two dimensions
 * (in fact a quartering), so this is (in practice) the fastest possible
 * search algorithm for testing points in a set of rectangles.
 *
 * Complexity of search/insert/delete/update:
 *
 *   best case:     O(log n)
 *   average case:  O(log n)
 *   worst case:    O(n)
 *
 * Complexity of configuring the tree:
 *
 *   best case:     O(n*log n)
 *   average case:  O(n*log n)
 *   worst case:    O(sqr n)
 *
 * The worst case happens if most of the rectangles are overlapping, or even
 * equal. We could implement the Q-Tree with a worst case of O(log n) for search,
 * if we omit the spanning items. But then it may happen under certain conditions
 * that the tree will explode in memory, the spanning items are preventing this.
 * In practice the search will be super-fast, despite the worst case. It's a bit
 * similar to the heapsort/quicksort complexity, in theory heapsort is better
 * (worst case O(n*log n)), but in practice quicksort performs better (although
 * worst case is O(sqr n)).
 *
 * Note that the Q-Tree is far superior compared to the R-Tree, provided that we
 * know the overall bounding box of all rectangles in advance, and that we are
 * searching for points (this means we are searching in two-dimensional data).
 * The worst case of the search inside the R-Tree occurs when all rectangles are
 * disjoint - and this will mostly be the case when the text widget is using the
 * Q-Tree for the bounding boxes of the images - and the search inside the R-Tree
 * needs complex computations, but the Q-Tree doesn't need complex computations,
 * and is much faster.
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkQTree.h"

#if !(__STDC_VERSION__ >= 199901L || (defined(_MSC_VER) && _MSC_VER >= 1900))
# define _TK_NEED_IMPLEMENTATION
# include "tkQTreePriv.h"
#endif

#include <tcl.h>
#include <string.h>
#include <assert.h>

#if TK_CHECK_ALLOCS
# define DEBUG_ALLOC(expr) expr
#else
# define DEBUG_ALLOC(expr)
#endif


DEBUG_ALLOC(unsigned tkQTreeCountNewTree = 0);
DEBUG_ALLOC(unsigned tkQTreeCountDestroyTree = 0);
DEBUG_ALLOC(unsigned tkQTreeCountNewNode = 0);
DEBUG_ALLOC(unsigned tkQTreeCountDestroyNode = 0);
DEBUG_ALLOC(unsigned tkQTreeCountNewItem = 0);
DEBUG_ALLOC(unsigned tkQTreeCountDestroyItem = 0);
DEBUG_ALLOC(unsigned tkQTreeCountNewElement = 0);
DEBUG_ALLOC(unsigned tkQTreeCountDestroyElement = 0);


/* Based on tests this seems to provide the best performance. */
enum { MaxNodeItems = 20 };


typedef struct Element {
    struct Element *prev;
    struct Element *next;
    TkQTreeUid uid;
    TkQTreeRect bbox;
    TkQTreeState state;
    uint32_t epoch;
} Element;

typedef struct Item {
    struct Item *next;
    Element *elem;
} Item;

typedef struct Node {
    Item *spanningItem;
    int countSpanning;

    Item *partialItem;
    int countPartial;

    struct Node *ne;
    struct Node *nw;
    struct Node *se;
    struct Node *sw;
} Node;

struct TkQTree {
    Node *root;
    Element *elem;
    TkQTreeRect bbox;
    uint32_t epoch;
};


static void InsertElem(const TkQTreeRect *bbox, Node *node, Element *elem);


static Element *
NewElement(
    TkQTree tree,
    const TkQTreeRect *rect,
    const TkQTreeUid uid,
    TkQTreeState initialState)
{
    Element *elem;

    assert(tree);
    assert(rect);

    DEBUG_ALLOC(tkQTreeCountNewElement++);
    elem = ckalloc(sizeof(Element));
    elem->prev = NULL;
    if ((elem->next = tree->elem)) {
	elem->next->prev = elem;
    }
    tree->elem = elem;
    elem->bbox = *rect;
    elem->uid = uid;
    elem->state = initialState;
    elem->epoch = 0;
    return elem;
}


static Node *
NewNode(int initialPartialCount)
{
    Node *n;

    DEBUG_ALLOC(tkQTreeCountNewNode++);
    n = memset(ckalloc(sizeof(Node)), 0, sizeof(Node));
    n->countPartial = initialPartialCount;
    return n;
}


static void
FreeItems(
    Item *item)
{
    while (item) {
	Item *next = item->next;
	ckfree(item);
	item = next;
	DEBUG_ALLOC(tkQTreeCountDestroyItem++);
    }
}


static void
FreeNode(
    Node *node)
{
    if (node) {
	if (node->countPartial >= 0) {
	    FreeItems(node->partialItem);
	} else {
	    FreeNode(node->ne);
	    FreeNode(node->nw);
	    FreeNode(node->se);
	    FreeNode(node->sw);
	}
	FreeItems(node->spanningItem);
	ckfree(node);
	DEBUG_ALLOC(tkQTreeCountDestroyNode++);
    }
}


static void
FreeElements(
    Element *elem)
{
    while (elem) {
	Element *next = elem->next;
	ckfree(elem);
	elem = next;
	DEBUG_ALLOC(tkQTreeCountDestroyElement++);
    }
}


static void
AddItem(
    Item **itemPtr,
    Element *elem)
{
    Item *newItem;

    assert(itemPtr);
    assert(elem);

    DEBUG_ALLOC(tkQTreeCountNewItem++);
    newItem = ckalloc(sizeof(Item));
    newItem->elem = elem;
    newItem->next = *itemPtr;
    *itemPtr = newItem;
}


/* Helper for function Split. */
static Node *
FillQuarter(
    const TkQTreeRect *bbox,
    const Item *partialItem,
    Element *newElem)
{
    Node *node;

    assert(bbox);
    assert(newElem);
    assert(!TkQTreeRectIsEmpty(&newElem->bbox));

    if (TkQTreeRectIsEmpty(bbox)) {
	return NULL;
    }

    node = NULL;

    for ( ; partialItem; partialItem = partialItem->next) {
	if (TkQTreeRectIntersects(&partialItem->elem->bbox, bbox)) {
	    if (!node) { node = NewNode(0); }
	    if (TkQTreeRectContainsRect(&partialItem->elem->bbox, bbox)) {
		AddItem(&node->spanningItem, partialItem->elem);
		node->countSpanning += 1;
	    } else {
		AddItem(&node->partialItem, partialItem->elem);
		node->countPartial += 1;
	    }
	}
    }

    if (TkQTreeRectIntersects(&newElem->bbox, bbox)) {
	if (!node) { node = NewNode(0); }
	if (TkQTreeRectContainsRect(&newElem->bbox, bbox)) {
	    AddItem(&node->spanningItem, newElem);
	    node->countSpanning += 1;
	} else {
	    InsertElem(bbox, node, newElem);
	}
    }

    return node;
}


/* Helper for function InsertElem. */
static void
Split(
    const TkQTreeRect *bbox,
    Node *node,
    Element *elem)
{
    TkQTreeCoord xmin, ymin, xmax, ymax;
    TkQTreeCoord xh, yh;
    TkQTreeRect quart;

    assert(node);
    assert(bbox);
    assert(elem);
    assert(node->countPartial == MaxNodeItems);
    assert(!TkQTreeRectIsEmpty(bbox));
    assert(!TkQTreeRectIsEmpty(&elem->bbox));
    assert(!TkQTreeRectContainsRect(&elem->bbox, bbox));

    xmin = bbox->xmin;
    ymin = bbox->ymin;
    xmax = bbox->xmax;
    ymax = bbox->ymax;
    xh = (xmax - xmin)/2;
    yh = (ymax - ymin)/2;

    assert(xh > 0 || yh > 0);

    TkQTreeRectSet(&quart, xmin, ymin, xmin + xh, ymin + yh);
    node->ne = FillQuarter(&quart, node->partialItem, elem);

    TkQTreeRectSet(&quart, xmin + xh, ymin, xmax, ymin + yh);
    node->nw = FillQuarter(&quart, node->partialItem, elem);

    TkQTreeRectSet(&quart, xmin, ymin + yh, xmin + xh, ymax);
    node->se = FillQuarter(&quart, node->partialItem, elem);

    TkQTreeRectSet(&quart, xmin + xh, ymin + yh, xmax, ymax);
    node->sw = FillQuarter(&quart, node->partialItem, elem);

    FreeItems(node->partialItem);
    node->partialItem = NULL;
    node->countPartial = -1;
}


static void
InsertElem(
    const TkQTreeRect *bbox,
    Node *node,
    Element *elem)
{
    assert(node);
    assert(elem);
    assert(bbox);
    assert(!TkQTreeRectIsEmpty(bbox));
    assert(!TkQTreeRectIsEmpty(&elem->bbox));
    assert(!TkQTreeRectContainsRect(&elem->bbox, bbox));

    if (node->countPartial == MaxNodeItems) {
	Split(bbox, node, elem);
    } else {
	AddItem(&node->partialItem, elem);
	node->countPartial += 1;
    }
}


static void
InsertRect(
    const TkQTreeRect *bbox,
    Node *node,
    Element *elem)
{
    assert(node);
    assert(bbox);
    assert(elem);
    assert(!TkQTreeRectIsEmpty(bbox));
    assert(!TkQTreeRectIsEmpty(&elem->bbox));

    if (TkQTreeRectContainsRect(&elem->bbox, bbox)) {
	AddItem(&node->spanningItem, elem);
	node->countSpanning += 1;
    } else if (node->countPartial >= 0) {
	InsertElem(bbox, node, elem);
    } else {
	TkQTreeRect quart;

	TkQTreeCoord xmin = bbox->xmin;
	TkQTreeCoord ymin = bbox->ymin;
	TkQTreeCoord xmax = bbox->xmax;
	TkQTreeCoord ymax = bbox->ymax;
	TkQTreeCoord xh = (xmax - xmin)/2;
	TkQTreeCoord yh = (ymax - ymin)/2;

	TkQTreeRectSet(&quart, xmin, ymin, xmin + xh, ymin + yh);
	if (!TkQTreeRectIsEmpty(&quart) && TkQTreeRectIntersects(&quart, &elem->bbox)) {
	    if (!node->ne) { node->ne = NewNode(-1); }
	    InsertRect(&quart, node->ne, elem);
	}

	TkQTreeRectSet(&quart, xmin + xh, ymin, xmax, ymin + yh);
	if (!TkQTreeRectIsEmpty(&quart) && TkQTreeRectIntersects(&quart, &elem->bbox)) {
	    if (!node->nw) { node->nw = NewNode(-1); }
	    InsertRect(&quart, node->nw, elem);
	}

	TkQTreeRectSet(&quart, xmin, ymin + yh, xmin + xh, ymax);
	if (!TkQTreeRectIsEmpty(&quart) && TkQTreeRectIntersects(&quart, &elem->bbox)) {
	    if (!node->se) { node->se = NewNode(-1); }
	    InsertRect(&quart, node->se, elem);
	}

	TkQTreeRectSet(&quart, xmin + xh, ymin + yh, xmax, ymax);
	assert(!TkQTreeRectIsEmpty(&quart));
	if (TkQTreeRectIntersects(&quart, &elem->bbox)) {
	    if (!node->sw) { node->sw = NewNode(-1); }
	    InsertRect(&quart, node->sw, elem);
	}
    }
}


/* Helper for functions CountPartialItems and CountSpanningItems. */
static unsigned
CountItems(
    const Item *item,
    uint32_t epoch)
{
    unsigned count = 0;

    for ( ; item; item = item->next) {
	if (item->elem->epoch != epoch) {
	    item->elem->epoch = epoch;
	    if (++count == MaxNodeItems + 1) {
		return count;
	    }
	}
    }

    return count;
}


/* Helper for function DeleteRect. */
static unsigned
CountPartialItems(
    const Node *node,
    uint32_t epoch)
{
    unsigned count;

    if (!node) {
	count = 0;
    } else if (node->countPartial == -1) {
	count = MaxNodeItems + 1;
    } else {
	count = CountItems(node->partialItem, epoch);
    }

    return count;
}


/* Helper for function DeleteRect. */
static unsigned
CountSpanningItems(
    const Node *node,
    uint32_t epoch)
{
    return node ? CountItems(node->spanningItem, epoch) : 0;
}


/* Helper for function TransferItems. */
static void
MoveItems(
    Item **srcPtr,
    Node *dst,
    uint32_t epoch)
{
    Item *item;
    Item *prevItem;

    assert(srcPtr);
    assert(dst);

    item = *srcPtr;

    if (!item) {
	return;
    }

    prevItem = NULL;

    while (item) {
	Item *nextItem = item->next;
	if (item->elem->epoch != epoch) {
	    if (prevItem) {
		prevItem->next = item->next;
	    } else {
		*srcPtr = item->next;
	    }
	    item->next = dst->partialItem;
	    dst->partialItem = item;
	    item->elem->epoch = epoch;
	} else {
	    prevItem = item;
	}
	item = nextItem;
    }
}


/* Helper for function DeleteRect. */
static void
TransferItems(
    Node *src,
    Node *dst,
    uint32_t epoch)
{
    if (!src) {
	return;
    }

    assert(dst);

    MoveItems(&src->partialItem, dst, epoch);
    MoveItems(&src->spanningItem, dst, epoch);
    FreeNode(src);
}


/* Helper for function DeleteRect. */
static void
RemoveItem(
    Item **itemPtr,
    int *count,
    TkQTreeUid uid,
    Element **elem)
{
    Item *item, *prevItem;

    assert(itemPtr);
    assert(count);

    for (item = *itemPtr, prevItem = NULL; item; prevItem = item, item = item->next) {
	if (item->elem->uid == uid) {
	    *elem = item->elem;
	    if (prevItem) {
		prevItem->next = item->next;
	    } else {
		*itemPtr = item->next;
	    }
	    ckfree(item);
	    *count -= 1;
	    DEBUG_ALLOC(tkQTreeCountDestroyItem++);
	    return;
	}
    }
}


static Element *
DeleteRect(
    TkQTree tree,
    const TkQTreeRect *bbox,
    Node *node,
    const TkQTreeRect *rect,
    TkQTreeUid uid,
    uint32_t *epoch)
{
    Element *elem, *e;

    assert(tree);
    assert(rect);
    assert(!TkQTreeRectIsEmpty(rect));
    assert(epoch);

    if (!node) {
	return NULL;
    }

    elem = NULL;

    RemoveItem(&node->spanningItem, &node->countSpanning, uid, &elem);

    if (node->countPartial >= 0) {
	RemoveItem(&node->partialItem, &node->countPartial, uid, &elem);
    } else {
	TkQTreeRect quart;

	TkQTreeCoord xmin = bbox->xmin;
	TkQTreeCoord ymin = bbox->ymin;
	TkQTreeCoord xmax = bbox->xmax;
	TkQTreeCoord ymax = bbox->ymax;
	TkQTreeCoord xh = (xmax - xmin)/2;
	TkQTreeCoord yh = (ymax - ymin)/2;

	if (node->ne) {
	    TkQTreeRectSet(&quart, xmin, ymin, xmin + xh, ymin + yh);
	    if (TkQTreeRectIntersects(&quart, rect)) {
		if ((e = DeleteRect(tree, &quart, node->ne, rect, uid, epoch))) {
		    elem = e;
		    if (node->ne->countPartial == 0 && node->ne->countSpanning == 0) {
			FreeNode(node->ne);
			node->ne = NULL;
		    }
		}
	    }
	}
	if (node->nw) {
	    TkQTreeRectSet(&quart, xmin + xh, ymin, xmax, ymin + yh);
	    if (TkQTreeRectIntersects(&quart, rect)) {
		if ((e = DeleteRect(tree, &quart, node->nw, rect, uid, epoch))) {
		    elem = e;
		    if (node->nw->countPartial == 0 && node->nw->countSpanning == 0) {
			FreeNode(node->nw);
			node->nw = NULL;
		    }
		}
	    }
	}
	if (node->se) {
	    TkQTreeRectSet(&quart, xmin, ymin + yh, xmin + xh, ymax);
	    if (TkQTreeRectIntersects(&quart, rect)) {
		if ((e = DeleteRect(tree, &quart, node->se, rect, uid, epoch))) {
		    elem = e;
		    if (node->se->countPartial == 0 && node->se->countSpanning == 0) {
			FreeNode(node->se);
			node->se = NULL;
		    }
		}
	    }
	}
	if (node->sw) {
	    TkQTreeRectSet(&quart, xmin + xh, ymin + yh, xmax, ymax);
	    if (TkQTreeRectIntersects(&quart, rect)) {
		if ((e = DeleteRect(tree, &quart, node->sw, rect, uid, epoch))) {
		    elem = e;
		    if (node->sw->countPartial == 0 && node->sw->countSpanning == 0) {
			FreeNode(node->sw);
			node->sw = NULL;
		    }
		}
	    }
	}
    }

    if (elem && node->countPartial == -1) {
	unsigned total = 0;

	*epoch += 1;

	total += CountPartialItems(node->ne, *epoch);
	total += CountPartialItems(node->nw, *epoch);
	total += CountPartialItems(node->se, *epoch);
	total += CountPartialItems(node->sw, *epoch);

	if (total <= MaxNodeItems) {
	    total += CountSpanningItems(node->ne, *epoch);
	    total += CountSpanningItems(node->nw, *epoch);
	    total += CountSpanningItems(node->se, *epoch);
	    total += CountSpanningItems(node->sw, *epoch);

	    if (total <= MaxNodeItems) {
		*epoch += 1;
		TransferItems(node->ne, node, *epoch);
		TransferItems(node->nw, node, *epoch);
		TransferItems(node->se, node, *epoch);
		TransferItems(node->sw, node, *epoch);
		node->ne = node->nw = node->se = node->sw = NULL;
		node->countPartial = total;
	    }
	}
    }

    return elem;
}


static Element *
FindElem(
    const TkQTreeRect *bbox,
    const Node *node,
    const TkQTreeRect *rect,
    TkQTreeUid uid)
{
    const Item *item;

    assert(node);
    assert(rect);
    assert(!TkQTreeRectIsEmpty(rect));

    if ((item = node->spanningItem)) {
	for ( ; item; item = item->next) {
	    if (item->elem->uid == uid) {
		return item->elem;
	    }
	}
    }
    if (node->countPartial >= 0) {
	for (item = node->partialItem; item; item = item->next) {
	    if (item->elem->uid == uid) {
		return item->elem;
	    }
	}
    } else {
	Element *elem;
	TkQTreeRect quart;

	TkQTreeCoord xmin = bbox->xmin;
	TkQTreeCoord ymin = bbox->ymin;
	TkQTreeCoord xmax = bbox->xmax;
	TkQTreeCoord ymax = bbox->ymax;
	TkQTreeCoord xh = (xmax - xmin)/2;
	TkQTreeCoord yh = (ymax - ymin)/2;

	if (node->ne) {
	    TkQTreeRectSet(&quart, xmin, ymin, xmin + xh, ymin + yh);
	    if (TkQTreeRectIntersects(&quart, rect)) {
		if ((elem = FindElem(&quart, node->ne, rect, uid))) { return elem; }
	    }
	}
	if (node->nw) {
	    TkQTreeRectSet(&quart, xmin + xh, ymin, xmax, ymin + yh);
	    if (TkQTreeRectIntersects(&quart, rect)) {
		if ((elem = FindElem(&quart, node->nw, rect, uid))) { return elem; }
	    }
	}
	if (node->se) {
	    TkQTreeRectSet(&quart, xmin, ymin + yh, xmin + xh, ymax);
	    if (TkQTreeRectIntersects(&quart, rect)) {
		if ((elem = FindElem(&quart, node->se, rect, uid))) { return elem; }
	    }
	}
	if (node->sw) {
	    TkQTreeRectSet(&quart, xmin + xh, ymin + yh, xmax, ymax);
	    if (TkQTreeRectIntersects(&quart, rect)) {
		if ((elem = FindElem(&quart, node->sw, rect, uid))) { return elem; }
	    }
	}
    }

    return NULL;
}


static void
RemoveElem(
    TkQTree tree,
    Element *elem)
{
    assert(tree);
    assert(elem);

    if (elem->prev) {
	elem->prev->next = elem->next;
    } else {
	tree->elem = elem->next;
    }
    if (elem->next) {
	elem->next->prev = elem->prev;
    }
    ckfree(elem);
    DEBUG_ALLOC(tkQTreeCountDestroyElement++);
}


void
TkQTreeTraverse(
    TkQTree tree,
    TkQTreeCallback cbHit,
    TkQTreeClientData cbArg)
{
    Element *elem;

    assert(tree);
    assert(cbHit);

    for (elem = tree->elem; elem; elem = elem->next) {
	if (!cbHit(elem->uid, &elem->bbox, &elem->state, cbArg)) {
	    return;
	}
    }
}


bool
TkQTreeFindState(
    const TkQTree tree,
    const TkQTreeRect *rect,
    TkQTreeUid uid,
    TkQTreeState *state)
{
    const Element *elem;

    assert(tree);
    assert(rect);

    if (!tree->elem
	    || TkQTreeRectIsEmpty(rect)
	    || !(elem = FindElem(&tree->bbox, tree->root, rect, uid))) {
	return false;
    }
    if (state) {
	*state = elem->state;
    }
    return true;
}


bool
TkQTreeSetState(
    const TkQTree tree,
    const TkQTreeRect *rect,
    TkQTreeUid uid,
    TkQTreeState state)
{
    Element *elem;

    assert(tree);
    assert(rect);

    if (!tree->elem
	    || TkQTreeRectIsEmpty(rect)
	    || !(elem = FindElem(&tree->bbox, tree->root, rect, uid))) {
	return false;
    }
    elem->state = state;
    return true;
}


unsigned
TkQTreeSearch(
    const TkQTree tree,
    TkQTreeCoord x,
    TkQTreeCoord y,
    TkQTreeCallback cbHit,
    TkQTreeClientData cbArg)
{
    const Node *node;
    unsigned hitCount;
    TkQTreeRect bbox;

    assert(tree);

    if (!tree->elem) {
	return 0;
    }

    if (!TkQTreeRectContainsPoint(&tree->bbox, x, y)) {
	return 0;
    }

    node = tree->root;
    bbox = tree->bbox;
    hitCount = 0;

    while (node) {
	const Item *item;

	for (item = node->spanningItem; item; item = item->next) {
	    Element *elem = item->elem;

	    hitCount += 1;
	    if (cbHit && !cbHit(elem->uid, &elem->bbox, &elem->state, cbArg)) {
		return hitCount;
	    }
	}

	if (node->countPartial >= 0) {
	    for (item = node->partialItem; item; item = item->next) {
		if (TkQTreeRectContainsPoint(&item->elem->bbox, x, y)) {
		    Element *elem = item->elem;

		    hitCount += 1;
		    if (cbHit && !cbHit(elem->uid, &elem->bbox, &elem->state, cbArg)) {
			return hitCount;
		    }
		}
	    }
	    node = NULL;
	} else {
	    TkQTreeCoord xh = (bbox.xmax - bbox.xmin)/2;
	    TkQTreeCoord yh = (bbox.ymax - bbox.ymin)/2;

	    if (y < yh + bbox.ymin) {
		if (x < xh + bbox.xmin) {
		    node = node->ne;
		    bbox.xmax = bbox.xmin + xh;
		    bbox.ymax = bbox.ymin + yh;
		} else {
		    node = node->nw;
		    bbox.xmin += xh;
		    bbox.ymax = bbox.ymin + yh;
		}
	    } else {
		if (x < xh + bbox.xmin) {
		    node = node->se;
		    bbox.xmax = bbox.xmin + xh;
		    bbox.ymin += yh;
		} else {
		    node = node->sw;
		    bbox.xmin += xh;
		    bbox.ymin += yh;
		}
	    }
	}
    }

    return hitCount;
}


bool
TkQTreeInsertRect(
    TkQTree tree,
    const TkQTreeRect *rect,
    TkQTreeUid uid,
    TkQTreeState initialState)
{
    assert(tree);
    assert(rect);

    if (TkQTreeRectIsEmpty(rect) || !TkQTreeRectIntersects(&tree->bbox, rect)) {
	return false;
    }

    InsertRect(&tree->bbox, tree->root, NewElement(tree, rect, uid, initialState));
    return true;
}


bool
TkQTreeDeleteRect(
    TkQTree tree,
    const TkQTreeRect *rect,
    TkQTreeUid uid)
{
    Element *elem;

    assert(tree);
    assert(rect);

    if (TkQTreeRectIsEmpty(rect)) {
	return false;
    }

    elem = DeleteRect(tree, &tree->bbox, tree->root, rect, uid, &tree->epoch);

    if (!elem) {
	return false;
    }

    RemoveElem(tree, elem);
    return true;
}


bool
TkQTreeUpdateRect(
    TkQTree tree,
    const TkQTreeRect *oldRect,
    const TkQTreeRect *newRect,
    TkQTreeUid uid,
    TkQTreeState newState)
{
    Element *elem;

    assert(tree);
    assert(newRect);

    if (oldRect && TkQTreeRectIsEqual(oldRect, newRect)) {
	return true;
    }

    elem = NULL;

    if (oldRect && !TkQTreeRectIsEmpty(oldRect) && TkQTreeRectIntersects(&tree->bbox, oldRect)) {
	if ((elem = DeleteRect(tree, &tree->bbox, tree->root, oldRect, uid, &tree->epoch))) {
	    elem->state = newState;
	    elem->bbox = *newRect;
	}
    }

    if (TkQTreeRectIsEmpty(newRect) || !TkQTreeRectIntersects(&tree->bbox, newRect)) {
	return false;
    }

    InsertRect(&tree->bbox, tree->root, elem ? elem : NewElement(tree, newRect, uid, newState));
    return true;
}


void
TkQTreeDestroy(
    TkQTree *treePtr)
{
    assert(treePtr);

    if (*treePtr) {
	FreeNode((*treePtr)->root);
	FreeElements((*treePtr)->elem);
	ckfree(*treePtr);
	DEBUG_ALLOC(tkQTreeCountDestroyTree++);
	*treePtr = NULL;
    }
}


bool
TkQTreeConfigure(
    TkQTree *treePtr,
    const TkQTreeRect *rect)
{
    TkQTree tree;
    Element *elem;

    assert(treePtr);
    assert(rect);

    if (TkQTreeRectIsEmpty(rect)) {
	TkQTreeDestroy(treePtr);
	return false;
    }

    if ((tree = *treePtr)) {
	if (TkQTreeRectIsEqual(&tree->bbox, rect)) {
	    return true;
	}
	FreeNode(tree->root);
    } else {
	DEBUG_ALLOC(tkQTreeCountNewTree++);
	*treePtr = tree = ckalloc(sizeof(struct TkQTree));
	tree->elem = NULL;
	tree->epoch = 0;
    }

    tree->root = NewNode(0);
    tree->bbox = *rect;

    for (elem = tree->elem; elem; ) {
	Element *next = elem->next;

	if (TkQTreeRectIntersects(&elem->bbox, &tree->bbox)) {
	    InsertRect(&tree->bbox, tree->root, elem);
	}

	elem = next;
    }

    return true;
}


const TkQTreeRect *
TkQTreeGetBoundingBox(
    const TkQTree tree)
{
    assert(tree);
    return &tree->bbox;
}


#if QTREE_SEARCH_RECTS_CONTAINING

/* *****************************************************************
 * This is an example how to detect whether a rectangle is contained
 * in at least one of the rectangles in the tree.
 *
 * We assume that TkQTreeInsertRect has been called with intitial
 * state 0.
 * *****************************************************************/

typedef struct {
    TkQTreeCallback cbHit;
    TkQTreeClientData cbArg;
    unsigned count;
    unsigned epoch;
} MyClientData;

bool HitRectContaining1(
    TkQTreeUid uid,
    const TkQTreeRect *rect,
    TkQTreeState *state,
    TkQTreeClientData arg)
{
    MyClientData *cd = (MyClientData *) arg;
    *state = arg->epoch;
    return true;
}

bool HitRectContaining2(
    TkQTreeUid uid,
    const TkQTreeRect *rect,
    TkQTreeState *state,
    TkQTreeClientData arg)
{
    *state += 1;
    return true;
}

bool HitRectContaining3(
    TkQTreeUid uid,
    const TkQTreeRect *rect,
    TkQTreeState *state,
    TkQTreeClientData arg)
{
    MyClientData *cd = (MyClientData *) arg;

    if (++*state == arg->epoch + 3) {
	if (cd->cbHit) {
	    cd->cbHit(uid, rect, NULL, cd->cbArg);
	}
	cd->count += 1;
    }

    return true;
}

unsigned
TkQTreeSearchRectsContaining(
    const TkQTree tree,
    const TkQTreeRect *rect,
    TkQTreeCallback cbHit,
    TkQTreeClientData cbArg)
{
    static unsigned epoch = 0;

    const Node *node;
    unsigned hitCount;
    TkQTreeRect bbox;
    MyClientData cd;

    assert(tree);

    if (!tree->elem) {
	return 0;
    }

    if (!TkQTreeRectContainsRect(&tree->bbox, rect)) {
	return 0;
    }

    cd.cbHit = cbHit;
    cd.cbArg = cbArg;
    cd.count = 0;
    cd.epoch = (epoch += 4);

    if (TkQTreeSearch(tree, rect->xmin, rect->ymin, HitRectContaining1, NULL) > 0) {
	if (TkQTreeSearch(tree, rect->xmax, rect->ymin, HitRectContaining2, NULL) > 0) {
	    if (TkQTreeSearch(tree, rect->xmin, rect->ymax, HitRectContaining2, NULL) > 0) {
		TkQTreeSearch(tree, rect->xmax, rect->ymax, HitRectContaining3, (TkQTreeClientData) cd);
	    }
	}
    }

    return cd.count;
}

#endif /* QTREE_SEARCH_RECTS_CONTAINING */


#if __STDC_VERSION__ >= 199901L || (defined(_MSC_VER) && _MSC_VER >= 1900)
/* Additionally we need stand-alone object code. */
#define inline extern
inline bool TkQTreeRectIsEmpty(const TkQTreeRect *rect);
inline bool TkQTreeRectIsEqual(const TkQTreeRect *rect1, const TkQTreeRect *rect2);
inline bool TkQTreeRectContainsPoint(const TkQTreeRect *rect, TkQTreeCoord x, TkQTreeCoord y);
inline bool TkQTreeRectContainsRect(const TkQTreeRect *rect1, const TkQTreeRect *rect2);
inline bool TkQTreeRectIntersects(const TkQTreeRect *rect1, const TkQTreeRect *rect2);
inline TkQTreeRect *TkQTreeRectSet(TkQTreeRect *rect,
    TkQTreeCoord xmin, TkQTreeCoord ymin, TkQTreeCoord xmax, TkQTreeCoord ymax);
inline TkQTreeRect *TkQTreeRectTranslate(TkQTreeRect *rect, TkQTreeCoord dx, TkQTreeCoord dy);
#endif /* __STDC_VERSION__ >= 199901L */

/* vi:set ts=8 sw=4: */
