/*
 * tkTextUndo.c --
 *
 *	This module provides the implementation of an undo stack.
 *
 * Copyright (c) 2015-2017 Gregor Cramer.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkTextUndo.h"
#include "tkAlloc.h"
#include <assert.h>

#ifndef TK_C99_INLINE_SUPPORT
# define _TK_NEED_IMPLEMENTATION
# include "tkTextUndoPriv.h"
#endif

#ifndef MAX
# define MAX(a,b) ((a) < (b) ? b : a)
#endif

#ifndef MIN
# define MIN(a,b) ((a) < (b) ? a : b)
#endif


typedef TkTextUndoMyAtom MyUndoAtom;


/*
 * Our list of undo/redo atoms is a circular double linked list.
 * It's circular beause the "last" pointer is connected with the
 * "root" pointer. The list starts either with the oldest undo atom,
 * or with the newest redo atom if no undo atom exists.
 *
 * 'stack->last' is always pointing to the newest undo item, or
 * NULL if no undo item exists.
 *
 * 'stack->root' is always pointing either to the oldest undo item,
 * or to the oldest redo item if no undo item exists.
 *
 * 'stack->current' is the current atom which receives all pushed
 * items (TkTextUndoPushItem), and is not yet linked into the list.
 * 'stack->current' can be NULL, in this case it has to be created
 * when the user is pushing an item.
 *
 * last ------------------+
 * root --+               |
 *        V               V
 *      +---+   +---+   +---+   +---+   +---+
 *   +->| A |-->| B |-->| C |-->| d |-->| e |--+
 *   |  +---+   +---+   +---+   +---+   +---+  |
 *   ------------------------------------------+
 *      undo: 3	                redo: 2
 *
 * A = oldest undo item
 * B = second oldest undo item
 * C = newest undo item
 * d = newest redo item
 * e = oldest redo item
 */


#define ATOM_SIZE(n) (Tk_Offset(TkTextUndoMyAtom, data) \
	+ Tk_Offset(TkTextUndoAtom, array) + (n)*sizeof(TkTextUndoSubAtom))


enum { InitialCapacity = 20 };


static void
FreeItems(
    const TkTextUndoStack stack,
    const TkTextUndoAtom *atom)
{
    TkTextUndoFreeProc *freeProc = stack->freeProc;
    const TkTextUndoSubAtom *arr;
    unsigned i, n;

    assert(atom);

    if (!freeProc) {
	return;
    }

    arr = atom->array;
    n = atom->arraySize;

    for (i = 0; i < n; ++i) {
	freeProc(stack, &arr[i]);
    }
}


static void
Release(
    TkTextUndoStack stack,
    MyUndoAtom *atom)
{
    MyUndoAtom *first, *root, *prev;

    if (!atom) {
	return;
    }

    assert(stack->root);
    first = atom;
    root = stack->root;
    prev = atom->prev;

    /*
     * Now delete all atoms starting at 'atom' until we reach the end (inclusive).
     */

    do {
	MyUndoAtom *next = atom->next;
	FreeItems(stack, &atom->data);
	free(atom);
	atom = next;
    } while (atom != root);

    /*
     * Update the list pointers accordingly.
     */

    if (first == root) {
	stack->root = stack->last = NULL;
    } else {
	root->prev = prev;
	prev->next = root;
    }
}


static void
ResetCurrent(
    TkTextUndoStack stack,
    bool force)
{
    TkTextUndoMyAtom *current = stack->current;

    if (current) {
	FreeItems(stack, &current->data);
    }

    if (force || !current || current->capacity > InitialCapacity) {
	static unsigned Size = ATOM_SIZE(InitialCapacity);
	current = stack->current = realloc(current, Size);
	/* NOTE: MSVS 2010 throws internal compiler error when using memset(realloc()). */
	memset(current, 0, Size);
	current->capacity = InitialCapacity;
    }

    current->data.arraySize = 0;
    current->data.size = 0;
    current->undoSize = 0;
}


static MyUndoAtom *
SwapCurrent(
    TkTextUndoStack stack,
    MyUndoAtom *atom)
{
    MyUndoAtom *current = stack->current;

    assert(atom != current);

    if (current->capacity != current->data.size) {
	current = stack->current = realloc(current, ATOM_SIZE(current->data.arraySize));
	current->capacity = current->data.arraySize;
    }

    if (!atom) {
	/*
	 * Just use the 'stack->current' item.
	 */
	stack->current = NULL;
	return current;
    }

    /*
     * Exchange given 'atom' with 'stack->current', this means that
     * 'stack->current' will be linked into the list replacing 'atom',
     * and 'atom' will become 'stack->current'.
     */

    if (atom->next == atom) {
	current->next = current;
	current->prev = current;
    } else {
	current->next = atom->next;
	current->prev = atom->prev;
	atom->next->prev = current;
	atom->prev->next = current;
    }

    stack->current = atom;
    atom->data.arraySize = 0;
    atom->data.size = 0;
    atom->undoSize = 0;
    atom->next = atom->prev = NULL;

    if (stack->root == atom) {
	stack->root = current;
    }
    if (stack->last == atom) {
	stack->last = current;
    }

    return current;
}


static bool
ClearRedoStack(
    TkTextUndoStack stack)
{
    MyUndoAtom *atom;

    if (stack->redoDepth == 0) {
	return false;
    }

    atom = stack->last ? stack->last->next : stack->root;

    assert(atom);
    stack->redoDepth = 0;
    stack->redoSize = 0;
    stack->redoItems = 0;
    Release(stack, atom);

    return true;
}


static void
InsertCurrentAtom(
    TkTextUndoStack stack)
{
    MyUndoAtom *atom;
    MyUndoAtom *current = stack->current;

    if (!current || current->data.arraySize == 0) {
	assert(!stack->doingUndo && !stack->doingRedo);
	return;
    }

    if (stack->maxSize > 0 && !stack->doingRedo) {
	unsigned newStackSize = current->data.size;

	if (stack->doingUndo) {
	    newStackSize = MAX(current->undoSize, newStackSize);
	}
	newStackSize += stack->undoSize + stack->redoSize;

	if (newStackSize > stack->maxSize) {
	    /*
	     * We do not push this atom, because the addtional size would
	     * exceed the maximal content size.
	     *
	     * Note that we must push an undo atom while performing a redo,
	     * but this case is already catched, and the size of this atom
	     * has already been taken into account (with the check of
	     * 'current->undoSize' when inserting the reverting redo atom;
	     * we assume that the new undo atom size is the same as the
	     * undo size before the redo).
	     */
	    if (stack->doingUndo) {
		/*
		 * We do not push this redo atom while peforming an undo, so all
		 * redoes are expired, we have to delete them.
		 */
		ClearRedoStack(stack);
	    } else {
		/*
		 * We do not push this undo atom, so the content becomes irreversible.
		 */
		stack->irreversible = true;
	    }
	    FreeItems(stack, &stack->current->data);
	    ResetCurrent(stack, false);
	    return;
	}
    }

    if (stack->doingRedo) {
	/*
	 * We'll push an undo atom while performing a redo.
	 */
	if (!stack->last) {
	    stack->last = stack->root;
	}
	atom = stack->last;
	SwapCurrent(stack, atom);
	stack->undoDepth += 1;
	stack->undoSize += atom->data.size;
	stack->undoItems += atom->data.arraySize;
    } else if (stack->doingUndo) {
	/*
	 * We'll push a redo atom while performing an undo.
	 */
	assert(stack->maxRedoDepth <= 0 || (int) stack->redoDepth < stack->maxRedoDepth);
	atom = stack->last ? stack->last->next : stack->root;
	SwapCurrent(stack, atom);
	stack->redoDepth += 1;
	stack->redoSize += atom->data.size;
	stack->redoItems += atom->data.arraySize;
    } else if (stack->last && stack->undoDepth == stack->maxUndoDepth) {
	/*
	 * We've reached the maximal stack limit, so delete the oldest undo
	 * before inserting the new item. The consequence is that now the content
	 * becomes irreversible. Furthermore all redo items will expire.
	 */
	ClearRedoStack(stack);
	assert(stack->last);
	atom = stack->last->next;
	stack->root = atom->next;
	stack->last = atom;
	stack->undoSize -= atom->data.size;
	stack->undoItems -= atom->data.arraySize;
	stack->irreversible = true;
	FreeItems(stack, &atom->data);
	SwapCurrent(stack, atom);
	stack->undoSize += atom->data.size;
	stack->undoItems += atom->data.arraySize;
    } else {
	/*
	 * Just insert the newly undo atom. Furthermore all redo items will expire.
	 */
	ClearRedoStack(stack);
	if (stack->last == NULL) {
	    stack->last = stack->root;
	}
	atom = SwapCurrent(stack, NULL);
	if ((atom->prev = stack->last)) {
	    atom->next = stack->last->next;
	    stack->last->next->prev = atom;
	    stack->last->next = atom;
	} else {
	    atom->next = atom->prev = stack->root = atom;
	}
	stack->last = atom;
	stack->undoDepth += 1;
	stack->undoSize += atom->data.size;
	stack->undoItems += atom->data.arraySize;
    }

    if (!stack->doingUndo) {
	/*
	 * Remember the size of this undo atom, probably we need it for the
	 * decision whether to push a redo atom when performing an undo.
	 */
	atom->undoSize = atom->data.size;
    }

    /*
     * Reset the buffer for next action.
     */
    ResetCurrent(stack, false);
}


static int
ResetStack(
    TkTextUndoStack stack,
    bool irreversible)
{
    bool contentChanged;

    assert(stack);

    if (stack->doingUndo || stack->doingRedo) {
	return TCL_ERROR;
    }

    contentChanged = stack->undoDepth > 0 || stack->redoDepth > 0 || stack->current;

    if (contentChanged) {
	Release(stack, stack->root);
	ResetCurrent(stack, true);
	stack->root = NULL;
	stack->last = NULL;
	stack->undoDepth = 0;
	stack->redoDepth = 0;
	stack->undoItems = 0;
	stack->redoItems = 0;
	stack->undoSize = 0;
	stack->redoSize = 0;
	stack->irreversible = irreversible;
	stack->pushSeparator = false;

	if (stack->contentChangedProc) {
	    stack->contentChangedProc(stack);
	}
    }

    return TCL_OK;
}


TkTextUndoStack
TkTextUndoCreateStack(
    unsigned maxUndoDepth,
    int maxRedoDepth,
    unsigned maxSize,
    TkTextUndoPerformProc undoProc,
    TkTextUndoFreeProc freeProc,
    TkTextUndoStackContentChangedProc contentChangedProc)
{
    TkTextUndoStack stack;

#ifndef _MSC_VER /* MSVC erroneously triggers warning C4550 */
    assert(undoProc);
#endif

    stack = memset(malloc(sizeof(*stack)), 0, sizeof(*stack));
    stack->undoProc = undoProc;
    stack->freeProc = freeProc;
    stack->contentChangedProc = contentChangedProc;
    stack->maxUndoDepth = maxUndoDepth;
    stack->maxRedoDepth = MAX(maxRedoDepth, -1);
    stack->maxSize = maxSize;

    return stack;
}


void
TkTextUndoDestroyStack(
    TkTextUndoStack *stackPtr)
{
    if (stackPtr) {
	TkTextUndoStack stack = *stackPtr;

	if (stack) {
	    assert(stack);
	    TkTextUndoClearStack(stack);
	    if (stack->current) {
		FreeItems(stack, &stack->current->data);
	    }
	    free(stack);
	    *stackPtr = NULL;
	}
    }
}


int
TkTextUndoResetStack(
    TkTextUndoStack stack)
{
    return stack ? ResetStack(stack, false) : TCL_ERROR;
}


int
TkTextUndoClearStack(
    TkTextUndoStack stack)
{
    return stack ? ResetStack(stack, stack->undoDepth > 0) : TCL_ERROR;
}


int
TkTextUndoClearUndoStack(
    TkTextUndoStack stack)
{
    if (!stack) {
	return TCL_OK;
    }

    if (stack->doingUndo || stack->doingRedo) {
	return TCL_ERROR;
    }

    if (stack->undoDepth > 0) {
	TkTextUndoMyAtom *atom;

	assert(stack->last);
	stack->undoDepth = 0;
	stack->undoSize = 0;
	stack->undoItems = 0;
	atom = stack->root;
	stack->root = stack->last->next;
	stack->last = NULL;
	Release(stack, atom);
	ResetCurrent(stack, true);
	stack->irreversible = true;

	if (stack->contentChangedProc) {
	    stack->contentChangedProc(stack);
	}
    }

    return TCL_OK;
}


int
TkTextUndoClearRedoStack(
    TkTextUndoStack stack)
{
    if (!stack) {
	return TCL_OK;
    }

    if (stack->doingUndo || stack->doingRedo) {
	return TCL_ERROR;
    }

    if (ClearRedoStack(stack) && stack->contentChangedProc) {
	stack->contentChangedProc(stack);
    }

    return TCL_OK;
}


int
TkTextUndoSetMaxStackDepth(
    TkTextUndoStack stack,
    unsigned maxUndoDepth,
    int maxRedoDepth)
{
    assert(stack);

    if (stack->doingUndo || stack->doingRedo) {
	return TCL_ERROR;
    }

    if (maxUndoDepth > 0 || maxRedoDepth >= 0) {
	unsigned depth = stack->maxUndoDepth;

	if (depth == 0) {
	    depth = stack->undoDepth + stack->redoDepth;
	}

	if ((0 < maxUndoDepth && maxUndoDepth < depth)
		|| (0 <= maxRedoDepth && (unsigned) maxRedoDepth < (unsigned) stack->maxRedoDepth)) {
	    unsigned deleteRedos = MIN(stack->redoDepth, depth - maxUndoDepth);

	    if (0 <= maxRedoDepth && maxRedoDepth < stack->maxRedoDepth) {
		deleteRedos = MIN(stack->redoDepth,
			MAX(deleteRedos, (unsigned) stack->maxRedoDepth - maxRedoDepth));
	    }

	    stack->redoDepth -= deleteRedos;
	    depth = maxUndoDepth - deleteRedos;

	    if (deleteRedos > 0) {
		MyUndoAtom *atom = stack->root;

		/*
		 * We have to reduce the stack size until the depth will not
		 * exceed the given limit anymore. Start with oldest redoes,
		 * and continue with oldest undoes if necessary.
		 */

		for ( ; deleteRedos > 0; --deleteRedos) {
		    atom = atom->prev;
		    stack->redoSize -= atom->data.size;
		    stack->redoItems -= atom->data.arraySize;
		}

		Release(stack, atom);
	    }

	    if (maxUndoDepth > 0 && stack->undoDepth > depth) {
		MyUndoAtom *root = stack->root;
		MyUndoAtom *atom = stack->root;
		unsigned deleteUndos = stack->undoDepth - depth;

		stack->undoDepth -= deleteUndos;

		for ( ; deleteUndos > 0; --deleteUndos) {
		    stack->undoSize -= root->data.size;
		    stack->undoItems -= root->data.arraySize;
		    root = root->next;
		}

		stack->root = root;

		/* We have to delete undoes, so the content becomes irreversible. */
		stack->irreversible = true;

		Release(stack, atom);
	    }

	    if (stack->contentChangedProc) {
		stack->contentChangedProc(stack);
	    }
	}
    }

    stack->maxUndoDepth = maxUndoDepth;
    stack->maxRedoDepth = MAX(maxRedoDepth, -1);
    return TCL_OK;
}


int
TkTextUndoSetMaxStackSize(
    TkTextUndoStack stack,
    unsigned maxSize,
    bool applyImmediately)
{
    assert(stack);

    if (stack->doingUndo || stack->doingRedo) {
	return TCL_ERROR;
    }

    if (applyImmediately
	    && 0 < maxSize
	    && maxSize < stack->undoSize + stack->redoSize) {
	unsigned size = stack->undoSize + stack->redoSize;
	MyUndoAtom *atom = stack->root;
	unsigned depth = stack->redoDepth;

	/*
	 * We have to reduce the stack size until the size will not exceed
	 * the given limit anymore. Start with oldest redoes, and continue
	 * with oldest undoes if necessary.
	 */

	while (depth > 0 && maxSize < size) {
	    atom = atom->prev;
	    size -= atom->data.size;
	    stack->redoSize -= atom->data.size;
	    stack->redoItems -= atom->data.arraySize;
	    depth -= 1;
	}

	while (atom->data.size == 0 && atom != stack->root) {
	    stack->redoItems += atom->data.arraySize;
	    atom = atom->next;
	    depth += 1;
	}

	if (depth < stack->redoDepth) {
	    stack->redoDepth = depth;
	    Release(stack, atom);
	}

	if (maxSize < size && stack->last) {
	    MyUndoAtom *root = stack->root;

	    depth = stack->undoDepth;

	    while (depth > 0 && maxSize < size) {
		size -= root->data.size;
		stack->undoSize -= root->data.size;
		stack->undoItems -= root->data.arraySize;
		depth -= 1;
		root = root->next;
	    }

	    while (root->data.size == 0 && depth < stack->undoDepth) {
		stack->undoItems += root->data.arraySize;
		depth += 1;
		root = root->prev;
	    }

	    if (depth < stack->undoDepth) {
		stack->undoDepth = depth;
		atom = stack->root;
		stack->root = root;
		/*
		 * We have to delete undoes, so the content becomes irreversible.
		 */
		stack->irreversible = true;
		Release(stack, atom);
	    }
	}

	if (stack->contentChangedProc) {
	    stack->contentChangedProc(stack);
	}
    }

    stack->maxSize = maxSize;
    return TCL_OK;
}


static void
PushSeparator(
    TkTextUndoStack stack,
    bool force)
{
    assert(stack);

    if (force || stack->pushSeparator) {
	/*
	 * When performing an undo/redo, exact one reverting undo/redo atom has
	 * to be inserted, not more. So we do not allow the push of separators
	 * as long as an undo/redo action is in progress.
	 */

	if (!stack->doingUndo && !stack->doingRedo) {
	    /*
	     * Do not trigger stack->contentChangedProc here, because this has been
	     * already done via TkTextUndoPushItem/TkTextUndoPushRedoItem.
	     */
	    InsertCurrentAtom(stack);
	}
    }

    stack->pushSeparator = false;
}


void
TkTextUndoPushSeparator(
    TkTextUndoStack stack,
    bool immediately)
{
    assert(stack);

    if (immediately) {
	PushSeparator(stack, true);
    } else {
	/* Postpone pushing a separator until next item will be pushed. */
	stack->pushSeparator = true;
    }
}


int
TkTextUndoPushItem(
    TkTextUndoStack stack,
    TkTextUndoItem item,
    unsigned size)
{
    MyUndoAtom *atom;
    TkTextUndoSubAtom *subAtom;

    assert(stack);
    assert(item);

    PushSeparator(stack, false);

    if (stack->doingUndo && TkTextUndoRedoStackIsFull(stack)) {
	if (stack->freeProc) {
	    stack->freeProc(stack, item);
	}
	return TCL_ERROR;
    }

    atom = stack->current;

    if (!atom) {
	ResetCurrent(stack, true);
	atom = stack->current;
    } else if (atom->data.arraySize == atom->capacity) {
	atom->capacity *= 2;
	atom = stack->current = realloc(atom, ATOM_SIZE(atom->capacity));
    }

    subAtom = ((TkTextUndoSubAtom *) atom->data.array) + atom->data.arraySize++;
    subAtom->item = item;
    subAtom->size = size;
    subAtom->redo = stack->doingUndo;
    atom->data.size += size;
    atom->data.redo = stack->doingUndo;

    if (stack->contentChangedProc && !stack->doingUndo && !stack->doingRedo) {
	stack->contentChangedProc(stack);
    }

    return TCL_OK;
}


int
TkTextUndoPushRedoItem(
    TkTextUndoStack stack,
    TkTextUndoItem item,
    unsigned size)
{
    int rc;

    assert(stack);
    assert(!TkTextUndoIsPerformingUndoRedo(stack));

    PushSeparator(stack, true);
    stack->doingUndo = true;
    rc = TkTextUndoPushItem(stack, item, size);
    stack->doingUndo = false;

    return rc;
}


TkTextUndoItem
TkTextUndoSwapLastItem(
    TkTextUndoStack stack,
    TkTextUndoItem item,
    unsigned *size)
{
    TkTextUndoAtom *last;
    TkTextUndoSubAtom *subAtom;
    TkTextUndoItem oldItem;
    unsigned oldSize;

    assert(stack);
    assert(TkTextUndoGetLastUndoSubAtom(stack));

    last = stack->current ? &stack->current->data : &stack->last->data;
    subAtom = (TkTextUndoSubAtom *) last->array + (last->arraySize - 1);
    oldSize = subAtom->size;
    last->size -= oldSize;
    last->size += *size;
    stack->undoSize -= oldSize;
    stack->undoSize += *size;
    oldItem = subAtom->item;
    subAtom->item = item;
    subAtom->size = *size;
    *size = oldSize;

    return oldItem;
}


int
TkTextUndoDoUndo(
    TkTextUndoStack stack)
{
    int rc;

    assert(stack);

    if (stack->doingUndo || stack->doingRedo) {
	return TCL_ERROR;
    }

    InsertCurrentAtom(stack);

    if (stack->undoDepth == 0) {
	rc = TCL_ERROR;
    } else {
	MyUndoAtom *atom;

	assert(stack->last);

	stack->actual = atom = stack->last;
	stack->doingUndo = true;
	stack->undoDepth -= 1;
	stack->undoSize -= stack->actual->data.size;
	stack->undoItems -= stack->actual->data.arraySize;
	stack->undoProc(stack, &stack->actual->data);
	stack->last = stack->undoDepth ? stack->last->prev : NULL;
	stack->actual = NULL;

	if (!stack->current || stack->current->data.arraySize == 0) {
	    /*
	     * We didn't receive reverting items while performing an undo.
	     * So all redo items are expired, we have to delete them.
	     */
	    stack->redoDepth = 0;
	    stack->redoSize = 0;
	    stack->redoItems = 0;
	    Release(stack, stack->last ? stack->last->next : stack->root);
	} else {
	    FreeItems(stack, &atom->data);
	    InsertCurrentAtom(stack);
	}

	stack->doingUndo = false;
	rc = TCL_OK;

	if (stack->contentChangedProc) {
	    stack->contentChangedProc(stack);
	}
    }
    return rc;
}


int
TkTextUndoDoRedo(
    TkTextUndoStack stack)
{
    int rc;

    assert(stack);

    if (stack->doingUndo || stack->doingRedo) {
	return TCL_ERROR;
    }

    InsertCurrentAtom(stack);

    if (stack->redoDepth == 0) {
	rc = TCL_ERROR;
    } else {
	MyUndoAtom *atom;

	stack->actual = atom = stack->last ? stack->last->next : stack->root;
	stack->doingRedo = true;
	stack->redoDepth -= 1;
	stack->redoSize -= atom->data.size;
	stack->redoItems -= atom->data.arraySize;
	stack->undoProc(stack, &atom->data);
	stack->last = atom;
	stack->actual = NULL;

	if (!stack->current || stack->current->data.arraySize == 0) {
	    /*
	     * Oops, we did not receive reverting items while performing a redo.
	     * So we cannot apply the preceding undoes, we have to remove them.
	     * Now the content will become irreversible.
	     */
	    if (stack->undoDepth > 0) {
		stack->undoDepth = 0;
		stack->undoItems = 0;
		stack->undoSize = 0;
		atom = stack->root;
		stack->root = stack->last->next;
		stack->last = NULL;
	    } else {
		stack->root = atom->next;
	    }
	    Release(stack, atom);
	    stack->irreversible = true;
	} else {
	    FreeItems(stack, &atom->data);
	    InsertCurrentAtom(stack);
	}

	stack->doingRedo = false;
	rc = TCL_OK;

	if (stack->contentChangedProc) {
	    stack->contentChangedProc(stack);
	}
    }

    return rc;
}

bool
TkTextUndoStackIsFull(
    const TkTextUndoStack stack)
{
    if (!stack) {
	return true;
    }
    if (stack->doingUndo) {
	return stack->maxRedoDepth >= 0 && (int) stack->redoDepth >= stack->maxRedoDepth;
    }
    return stack->maxUndoDepth > 0 && stack->undoDepth >= stack->maxUndoDepth;
}


const TkTextUndoAtom *
TkTextUndoFirstUndoAtom(
    TkTextUndoStack stack)
{
    assert(stack);

    if (stack->current && stack->current->data.arraySize && !stack->doingUndo) {
	return &(stack->iter = stack->current)->data;
    }

    if (stack->undoDepth > 0 && stack->last != stack->actual) {
	return &(stack->iter = stack->last)->data;
    }

    stack->iter = NULL;
    return NULL;
}


const TkTextUndoAtom *
TkTextUndoNextUndoAtom(
    TkTextUndoStack stack)
{
    assert(stack);

    if (stack->iter) {
	if (stack->iter == stack->current) {
	    if (stack->undoDepth > 0 && stack->last != stack->actual) {
		return &(stack->iter = stack->last)->data;
	    }
	    stack->iter = NULL;
	} else if (stack->iter != stack->root && (stack->iter = stack->iter->prev) != stack->actual) {
	    return &stack->iter->data;
	} else {
	    stack->iter = NULL;
	}
    }

    return NULL;
}


const TkTextUndoAtom *
TkTextUndoFirstRedoAtom(
    TkTextUndoStack stack)
{
    assert(stack);

    if (stack->redoDepth > 0 && stack->root->prev != stack->actual) {
	return &(stack->iter = stack->root->prev)->data;
    }

    stack->iter = NULL;

    if (stack->current && stack->current->data.arraySize && stack->doingUndo) {
	return &stack->current->data;
    }

    return NULL;
}


const TkTextUndoAtom *
TkTextUndoNextRedoAtom(
    TkTextUndoStack stack)
{
    assert(stack);

    if (stack->iter) {
	if (stack->iter != stack->root
		&& (stack->iter = stack->iter->prev) != stack->last
		&& stack->iter != stack->actual) {
	    return &stack->iter->data;
	}

	stack->iter = NULL;

	if (stack->current && stack->current->data.arraySize && stack->doingUndo) {
	    return &stack->current->data;
	}
    }

    return NULL;
}


#ifdef TK_C99_INLINE_SUPPORT
/* Additionally we need stand-alone object code. */
extern void TkTextUndoSetContext(TkTextUndoStack stack, TkTextUndoContext context);
extern TkTextUndoContext TkTextUndoGetContext(const TkTextUndoStack stack);
extern unsigned TkTextUndoGetMaxUndoDepth(const TkTextUndoStack stack);
extern int TkTextUndoGetMaxRedoDepth(const TkTextUndoStack stack);
extern unsigned TkTextUndoGetMaxSize(const TkTextUndoStack stack);
extern unsigned TkTextUndoGetCurrentDepth(const TkTextUndoStack stack);
extern unsigned TkTextUndoGetCurrentSize(const TkTextUndoStack stack);
extern unsigned TkTextUndoGetCurrentUndoStackDepth(const TkTextUndoStack stack);
extern unsigned TkTextUndoGetCurrentRedoStackDepth(const TkTextUndoStack stack);
extern unsigned TkTextUndoCountUndoItems(const TkTextUndoStack stack);
extern unsigned TkTextUndoCountRedoItems(const TkTextUndoStack stack);
extern unsigned TkTextUndoGetCurrentUndoSize(const TkTextUndoStack stack);
extern unsigned TkTextUndoGetCurrentRedoSize(const TkTextUndoStack stack);
extern unsigned TkTextUndoCountCurrentUndoItems(const TkTextUndoStack stack);
extern unsigned TkTextUndoCountCurrentRedoItems(const TkTextUndoStack stack);
extern bool TkTextUndoContentIsIrreversible(const TkTextUndoStack stack);
extern bool TkTextUndoContentIsModified(const TkTextUndoStack stack);
extern bool TkTextUndoIsPerformingUndo(const TkTextUndoStack stack);
extern bool TkTextUndoIsPerformingRedo(const TkTextUndoStack stack);
extern bool TkTextUndoIsPerformingUndoRedo(const TkTextUndoStack stack);
extern const TkTextUndoAtom *TkTextUndoCurrentUndoAtom(const TkTextUndoStack stack);
extern const TkTextUndoAtom *TkTextUndoCurrentRedoAtom(const TkTextUndoStack stack);
extern const TkTextUndoSubAtom *TkTextUndoGetLastUndoSubAtom(const TkTextUndoStack stack);
extern bool TkTextUndoUndoStackIsFull(const TkTextUndoStack stack);
extern bool TkTextUndoRedoStackIsFull(const TkTextUndoStack stack);
#endif /* TK_C99_INLINE_SUPPORT */

/* vi:set ts=8 sw=4: */
