/*
 * tkTextUndo.h --
 *
 * The implementation of an undo/redo stack. The design and implementation
 * of tkUndo is not useful for our purposes:
 *
 * 1. We are not pushing an undo/redo pair on the stack. Our stack is only
 *    pushing the undo item, and applying this undo item will replace this
 *    item by a redo item (and vice versa when performing the redo; in fact
 *    there is no difference between an undo and redo item - the undo of
 *    insert is delete, the undo of delete is insert, and the same applies
 *    to redo). The advantage is that our undo (or redo) item for insert
 *    contains exact zero characters, contrary to the undo/redo pairs in
 *    tkUndo, one of the undo/redo items in this pair always contains a
 *    copy of the text content (a waste of memory).
 *
 * 2. tkUndo expects a script, our stack expects memory addresses (it's also
 *    an general implementation which can be shared).
 *
 * 3. Our stack allows to control the undo and redo stacks separately.
 *
 * 4. Moreover our stack supports to limit the size, not only the depth.
 *
 * Copyright (c) 2015-2017 Gregor Cramer.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKTEXTUNDO
#define _TKTEXTUNDO

#include "tkBool.h"
#include <stdint.h>

#ifndef _MSC_VER
# if __STDC_VERSION__ < 199901L
#  define inline /* we are not C99 conform */
# endif
#endif


/*
 * Our (private) stack type.
 */

struct TkTextUndoStack;
typedef struct TkTextUndoStack * TkTextUndoStack;

/*
 * The generic context type.
 */

typedef void * TkTextUndoContext;

/*
 * Basic type of an undo/redo item, the user has to know the real type.
 */

typedef void * TkTextUndoItem;

/*
 * Struct defining a single action, one or more of which may be defined (and
 * stored in a linked list) separately for each undo and redo action of an
 * undo atom.
 */

typedef struct TkTextUndoSubAtom {
    TkTextUndoItem item;	/* The data of this undo/redo item. */
    uint32_t size:31;		/* Size info for this item. */
    uint32_t redo:1;		/* Is redo item? */
} TkTextUndoSubAtom;

/*
 * Struct representing a single undo+redo atom to be placed in the stack.
 */

typedef struct TkTextUndoAtom {
    uint32_t arraySize;		/* Number of elements in this array. */
    uint32_t size:31;		/* Total size of all items. */
    uint32_t redo:1;		/* Is redo atom? */
    const TkTextUndoSubAtom array[1];
				/* Array of undo/redo actions. */
} TkTextUndoAtom;

/*
 * Callback to carry out undo or redo actions. This function may
 * push redo (undo) items for this undo (redo) onto the stack.
 * The user should push the reverting items while this action will
 * be performed.
 *
 * Note that the atom is given as a const value, but it's allowed
 * to change/reset the items of the sub-atoms.
 */

typedef void (TkTextUndoPerformProc)(TkTextUndoStack stack, const TkTextUndoAtom *atom);

/*
 * Callback proc type to free undo/redo items. This function will be
 * called when the user is clearing the stack (destroying the stack
 * is implicitly clearing the stack), or when the push operation
 * is deleting the oldest undo atom (for keeping the max. depth and
 * max. size).
 */

typedef void (TkTextUndoFreeProc)(const TkTextUndoStack stack, const TkTextUndoSubAtom *atom);

/*
 * Callback proc type for stack changes. Every time when the stack is
 * changing this callback function will be triggered.
 */

typedef void (TkTextUndoStackContentChangedProc)(const TkTextUndoStack stack);

/*
 * Functions for constructing/destructing the stack. Use zero for unlimited
 * stack depth, also use zero for unlimited size. 'freeProc' can be NULL,
 * but normally this function is required. It's clear that 'undoProc' is
 * mandatory. 'informStackChangeProc' and pushSeparatorProc can also be NULL.
 */

TkTextUndoStack TkTextUndoCreateStack(
    unsigned maxUndoDepth, int maxRedoDepth, unsigned maxSize,
    TkTextUndoPerformProc undoProc, TkTextUndoFreeProc freeProc,
    TkTextUndoStackContentChangedProc contentChangedProc);
void TkTextUndoDestroyStack(TkTextUndoStack *stackPtr);

/*
 * Managing the stack. Use zero for unlimited stack depth, also use zero
 * for unlimited size. Setting a lower limit than the current depth is
 * reducing the stack immediately. Setting a lower limit than the current
 * size is also reducing the stack immediately iff 'applyImmediately' is
 * 'true', otherwise the size will shrink when performing undo actions.
 * It is not allowed to use these functions while an undo/redo action is
 * performed, TCL_ERROR will be returned in this case.
 *
 * For convenience the functions TkTextUndoResetStack, TkTextUndoClearStack,
 * TkTextUndoClearUndoStack, and TkTextUndoClearRedoStack, are allowing
 * NULL arguments.
 */

int TkTextUndoSetMaxStackDepth(TkTextUndoStack stack,
    unsigned maxUndoDepth, int maxRedoDepth);
int TkTextUndoSetMaxStackSize(TkTextUndoStack stack,
    unsigned maxSize, bool applyImmediately);
int TkTextUndoResetStack(TkTextUndoStack stack);
int TkTextUndoClearStack(TkTextUndoStack stack);
int TkTextUndoClearUndoStack(TkTextUndoStack stack);
int TkTextUndoClearRedoStack(TkTextUndoStack stack);

/*
 * Functions to set/get the context. This is an additional information
 * for the user.
 */

inline void TkTextUndoSetContext(TkTextUndoStack stack, TkTextUndoContext context);
inline TkTextUndoContext TkTextUndoGetContext(const TkTextUndoStack stack);

/*
 * Accessing attributes.
 *
 * The content is irreversible if:
 * 1. The stack has exceeded the depth/size limit when adding undo atoms.
 * 2. Setting a lower limit has caused the deletion of undo atoms.
 * 3. Performing an redo did not push undo items.
 * Clearing the stack is resetting this state to false.
 *
 * The content is modified if undo stack is not empty, or the content
 * is irreversible.
 */

inline unsigned TkTextUndoGetMaxUndoDepth(const TkTextUndoStack stack);
inline int TkTextUndoGetMaxRedoDepth(const TkTextUndoStack stack);
inline unsigned TkTextUndoGetMaxSize(const TkTextUndoStack stack);
inline unsigned TkTextUndoGetCurrentDepth(const TkTextUndoStack stack);
inline unsigned TkTextUndoGetCurrentSize(const TkTextUndoStack stack);
inline unsigned TkTextUndoGetCurrentUndoStackDepth(const TkTextUndoStack stack);
inline unsigned TkTextUndoGetCurrentRedoStackDepth(const TkTextUndoStack stack);
inline unsigned TkTextUndoCountUndoItems(const TkTextUndoStack stack);
inline unsigned TkTextUndoCountRedoItems(const TkTextUndoStack stack);
inline unsigned TkTextUndoGetCurrentUndoSize(const TkTextUndoStack stack);
inline unsigned TkTextUndoGetCurrentRedoSize(const TkTextUndoStack stack);
inline unsigned TkTextUndoCountCurrentUndoItems(const TkTextUndoStack stack);
inline unsigned TkTextUndoCountCurrentRedoItems(const TkTextUndoStack stack);
inline bool TkTextUndoContentIsIrreversible(const TkTextUndoStack stack);
inline bool TkTextUndoContentIsModified(const TkTextUndoStack stack);
inline bool TkTextUndoIsPerformingUndo(const TkTextUndoStack stack);
inline bool TkTextUndoIsPerformingRedo(const TkTextUndoStack stack);
inline bool TkTextUndoIsPerformingUndoRedo(const TkTextUndoStack stack);
inline const TkTextUndoAtom *TkTextUndoCurrentUndoAtom(const TkTextUndoStack stack);
inline const TkTextUndoAtom *TkTextUndoCurrentRedoAtom(const TkTextUndoStack stack);
inline const TkTextUndoSubAtom *TkTextUndoGetLastUndoSubAtom(const TkTextUndoStack stack);

/*
 * Stack iterator functions.
 */

const TkTextUndoAtom *TkTextUndoFirstUndoAtom(TkTextUndoStack stack);
const TkTextUndoAtom *TkTextUndoNextUndoAtom(TkTextUndoStack stack);
const TkTextUndoAtom *TkTextUndoFirstRedoAtom(TkTextUndoStack stack);
const TkTextUndoAtom *TkTextUndoNextRedoAtom(TkTextUndoStack stack);

/* For convenience these functions are allowing NULL for the stack argument. */
inline bool TkTextUndoUndoStackIsFull(const TkTextUndoStack stack);
inline bool TkTextUndoRedoStackIsFull(const TkTextUndoStack stack);
bool TkTextUndoStackIsFull(const TkTextUndoStack stack);

/*
 * Push the items. Pushing a separator will group items into compound edit
 * actions. Pushing a separator without existing items will be ignored.
 *
 * While an undo/redo action is still in progress pushing separators will be
 * ignored, in this case the undo/action will push automatically a single
 * separator after the action is completed.
 */

int TkTextUndoPushItem(TkTextUndoStack stack, TkTextUndoItem item, unsigned size);
void TkTextUndoPushSeparator(TkTextUndoStack stack, bool immediately);

/*
 * Normally redo items will be pushed while undo will be performed. The next function
 * is only useful for the reconstruction of the stack.
 */

int TkTextUndoPushRedoItem(TkTextUndoStack stack, TkTextUndoItem item, unsigned size);

/*
 * Swap newest undo item with given item. Returns the old item. Note that this
 * function does not check whether the maximal undo byte size will be exceeded.
 *
 * Parameter 'size':
 * In: size of given item.
 * Out: size of returned item.
 */

TkTextUndoItem TkTextUndoSwapLastItem(TkTextUndoStack stack, TkTextUndoItem item, unsigned *size);

/*
 * Perform undo/redo operations. Before the action starts a separator will be
 * pushed. Returns an error (TCL_ERROR) if no undo (redo) action is possible.
 */

int TkTextUndoDoUndo(TkTextUndoStack stack);
int TkTextUndoDoRedo(TkTextUndoStack stack);


#if __STDC_VERSION__ >= 199901L
# define _TK_NEED_IMPLEMENTATION
# include "tkTextUndoPriv.h"
# undef _TK_NEED_IMPLEMENTATION
#endif

#endif /* _TKTEXTUNDO */
/* vi:set ts=8 sw=4: */
