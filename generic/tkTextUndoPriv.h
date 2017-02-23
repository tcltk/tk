/*
 * tkTextUndoPriv.h --
 *
 *	Private implementation for undo stack.
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKTEXTUNDO
# error "do not include this private header file"
#endif

#ifndef _TKTEXTUNDOPRIV
#define _TKTEXTUNDOPRIV

#include <stddef.h>

typedef struct TkTextUndoMyAtom {
    unsigned capacity;
    unsigned undoSize;
    struct TkTextUndoMyAtom* next;
    struct TkTextUndoMyAtom* prev;
    TkTextUndoAtom data;
} TkTextUndoMyAtom;

struct TkTextUndoStack {
    TkTextUndoPerformProc *undoProc;
    				/* Function for callback to perform undo/redo actions. */
    TkTextUndoFreeProc *freeProc;
    				/* Function which frees stack items, can be NULL. */
    TkTextUndoStackContentChangedProc *contentChangedProc;
    				/* Function which informs about stack changes. */
    TkTextUndoContext context;	/* User data. */
    TkTextUndoMyAtom *current;	/* Current undo atom (not yet pushed). */
    TkTextUndoMyAtom *root;	/* The root of the undo/redo stack. */
    TkTextUndoMyAtom *last;	/* Last added undo atom. */
    TkTextUndoMyAtom *iter;	/* Current atom in iteration loop. */
    TkTextUndoMyAtom *actual;	/* Current undo/redo atom in processing. */
    bool irreversible;		/* Whether undo actions has been released due to limited depth/size. */
    unsigned maxUndoDepth;	/* Maximal depth of the undo stack. */
    int maxRedoDepth;		/* Maximal depth of the redo stack. */
    unsigned maxSize;		/* Maximal size of the stack. */
    unsigned undoDepth;		/* Current depth of undo stack. */
    unsigned redoDepth;		/* Current depth of redo stack. */
    unsigned undoItems;		/* Current number of items on undo stack. */
    unsigned redoItems;		/* Current number of items on redo stack. */
    unsigned undoSize;		/* Total size of undo items. */
    unsigned redoSize;		/* Total size of redo items. */
    bool doingUndo;		/* Currently an undo action is performed? */
    bool doingRedo;		/* Currently a redo action is performed? */
    bool pushSeparator;		/* Push a separator before pushing a new item (iff true). */
};

#endif /* _TKTEXTUNDOPRIV */

#ifdef _TK_NEED_IMPLEMENTATION

#include <assert.h>

#ifndef _MSC_VER
# if __STDC_VERSION__ < 199901L
#  define inline /* we are not C99 conform */
# endif
#endif


inline unsigned
TkTextUndoGetMaxUndoDepth(const TkTextUndoStack stack)
{ assert(stack); return stack->maxUndoDepth; }

inline int
TkTextUndoGetMaxRedoDepth(const TkTextUndoStack stack)
{ assert(stack); return stack->maxRedoDepth; }

inline unsigned
TkTextUndoGetMaxSize(const TkTextUndoStack stack)
{ assert(stack); return stack->maxSize; }

inline bool
TkTextUndoContentIsModified(const TkTextUndoStack stack)
{ assert(stack); return stack->undoDepth > 0 || stack->irreversible; }

inline bool
TkTextUndoContentIsIrreversible(const TkTextUndoStack stack)
{ assert(stack); return stack->irreversible; }

inline bool
TkTextUndoIsPerformingUndo(const TkTextUndoStack stack)
{ assert(stack); return stack->doingUndo; }

inline bool
TkTextUndoIsPerformingRedo(const TkTextUndoStack stack)
{ assert(stack); return stack->doingRedo; }

inline bool
TkTextUndoIsPerformingUndoRedo(const TkTextUndoStack stack)
{ assert(stack); return stack->doingUndo || stack->doingRedo; }

inline bool
TkTextUndoUndoStackIsFull(const TkTextUndoStack stack)
{ return !stack || (stack->maxUndoDepth > 0 && stack->undoDepth >= stack->maxUndoDepth); }

inline bool
TkTextUndoRedoStackIsFull(const TkTextUndoStack stack)
{ return !stack || (stack->maxRedoDepth >= 0 && (int) stack->redoDepth >= stack->maxRedoDepth); }

inline unsigned
TkTextUndoCountCurrentUndoItems(const TkTextUndoStack stack)
{ assert(stack); return stack->current && !stack->doingUndo ? stack->current->data.arraySize : 0; }

inline unsigned
TkTextUndoCountCurrentRedoItems(const TkTextUndoStack stack)
{ assert(stack); return stack->current && stack->doingUndo ? stack->current->data.arraySize : 0; }

inline unsigned
TkTextUndoGetCurrentUndoStackDepth(const TkTextUndoStack stack)
{ assert(stack); return stack->undoDepth + (TkTextUndoCountCurrentUndoItems(stack) ? 1 : 0); }

inline unsigned
TkTextUndoGetCurrentRedoStackDepth(const TkTextUndoStack stack)
{ assert(stack); return stack->redoDepth + (TkTextUndoCountCurrentRedoItems(stack) ? 1 : 0); }

inline unsigned
TkTextUndoCountUndoItems(const TkTextUndoStack stack)
{ assert(stack); return stack->undoItems + TkTextUndoCountCurrentUndoItems(stack); }

inline unsigned
TkTextUndoCountRedoItems(const TkTextUndoStack stack)
{ assert(stack); return stack->redoItems + TkTextUndoCountCurrentRedoItems(stack); }

inline void
TkTextUndoSetContext(TkTextUndoStack stack, TkTextUndoContext context)
{ assert(stack); stack->context = context; }

inline TkTextUndoContext
TkTextUndoGetContext(const TkTextUndoStack stack)
{ assert(stack); return stack->context; }

inline unsigned
TkTextUndoGetCurrentDepth(
    const TkTextUndoStack stack)
{
    assert(stack);
    return stack->undoDepth + stack->redoDepth +
	    (stack->current && stack->current->data.arraySize > 0 ? 1 : 0);
}

inline unsigned
TkTextUndoGetCurrentUndoSize(
    const TkTextUndoStack stack)
{
    assert(stack);
    return stack->undoSize + (!stack->doingUndo && stack->current ? stack->current->undoSize : 0);
}

inline unsigned
TkTextUndoGetCurrentRedoSize(
    const TkTextUndoStack stack)
{
    assert(stack);
    return stack->redoSize + (!stack->doingRedo && stack->current ? stack->current->undoSize : 0);
}

inline unsigned
TkTextUndoGetCurrentSize(
    const TkTextUndoStack stack)
{
    assert(stack);
    return stack->undoSize + stack->redoSize + (stack->current ? stack->current->undoSize : 0);
}

inline const TkTextUndoAtom *
TkTextUndoCurrentUndoAtom(
    const TkTextUndoStack stack)
{
    assert(stack);

    if (stack->doingUndo) {
	return NULL;
    }
    return stack->current && stack->current->data.arraySize > 0 ? &stack->current->data : NULL;
}

inline const TkTextUndoAtom *
TkTextUndoCurrentRedoAtom(
    const TkTextUndoStack stack)
{
    assert(stack);

    if (stack->doingRedo) {
	return NULL;
    }
    return stack->current && stack->current->data.arraySize > 0 ? &stack->current->data : NULL;
}

inline const TkTextUndoSubAtom *
TkTextUndoGetLastUndoSubAtom(
    const TkTextUndoStack stack)
{
    TkTextUndoAtom *last;

    assert(stack);

    if (stack->current) {
	last = &stack->current->data;
    } else if (stack->last) {
	last = &stack->last->data;
    } else {
	return NULL;
    }
    return last->arraySize > 0 ? last->array + (last->arraySize - 1) : NULL;
}

#undef _TK_NEED_IMPLEMENTATION
#endif /* _TK_NEED_IMPLEMENTATION */
/* vi:set ts=8 sw=4: */
