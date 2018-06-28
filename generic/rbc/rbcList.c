/*
 * rbcList.c --
 *
 *      The module implements generic linked lists.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

/* ClientData RbcListGetValue((RbcListNode *node); */
#define RbcListGetValue(node)  	((node)->clientData)
/* void RbcListSetValue((RbcListNode *node, ClientData value); */
#define RbcListSetValue(node, value) \
	((node)->clientData = (ClientData)(value))

#define RbcListAppendNode(list, node) \
	(RbcListLinkBefore((list), (node), (RbcListNode *)NULL))

#define RbcListPrependNode(list, node) \
	(RbcListLinkAfter((list), (node), (RbcListNode *)NULL))

static RbcListNode *FindString(
    RbcList * listPtr,
    const char *key);
static RbcListNode *FindOneWord(
    RbcList * listPtr,
    const char *key);
static RbcListNode *FindArray(
    RbcList * listPtr,
    const char *key);
static void     FreeNode(
    RbcListNode * nodePtr);

/*
 *--------------------------------------------------------------
 *
 * FindString --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static RbcListNode *
FindString(
    RbcList * listPtr,          /* List to search */
    const char *key)
{                               /* Key to match */
    register RbcListNode *nodePtr;
    char            c;

    c = key[0];
    for (nodePtr = listPtr->headPtr; nodePtr != NULL;
        nodePtr = nodePtr->nextPtr) {
        if ((c == nodePtr->key.string[0]) &&
            (strcmp(key, nodePtr->key.string) == 0)) {
            return nodePtr;
        }
    }
    return NULL;
}

/*
 *--------------------------------------------------------------
 *
 * FindOneWord --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static RbcListNode *
FindOneWord(
    RbcList * listPtr,          /* List to search */
    const char *key)
{                               /* Key to match */
    register RbcListNode *nodePtr;

    for (nodePtr = listPtr->headPtr; nodePtr != NULL;
        nodePtr = nodePtr->nextPtr) {
        if (key == nodePtr->key.oneWordValue) {
            return nodePtr;
        }
    }
    return NULL;
}

/*
 *--------------------------------------------------------------
 *
 * FindArray --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static RbcListNode *
FindArray(
    RbcList * listPtr,          /* List to search */
    const char *key)
{                               /* Key to match */
    register RbcListNode *nodePtr;
    int             nBytes;

    nBytes = sizeof(int) * listPtr->type;
    for (nodePtr = listPtr->headPtr; nodePtr != NULL;
        nodePtr = nodePtr->nextPtr) {
        if (memcmp(key, nodePtr->key.words, nBytes) == 0) {
            return nodePtr;
        }
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeNode --
 *
 *      Free the memory allocated for the node.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void
FreeNode(
    RbcListNode * nodePtr)
{
    ckfree((char *) nodePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcListCreate --
 *
 *      Creates a new linked list structure and initializes its pointers
 *
 * Results:
 *      Returns a pointer to the newly created list structure.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcList        *
RbcListCreate(
    int type)
{
    RbcList        *listPtr;

    listPtr = (RbcList *) ckalloc(sizeof(RbcList));
    if (listPtr != NULL) {
        RbcListInit(listPtr, type);
    }
    return listPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcListCreateNode --
 *
 *      Creates a list node holder.  This routine does not insert
 *      the node into the list, nor does it no attempt to maintain
 *      consistency of the keys.  For example, more than one node
 *      may use the same key.
 *
 * Results:
 *      The return value is the pointer to the newly created node.
 *
 * Side Effects:
 *      The key is not copied, only the Uid is kept.  It is assumed
 *      this key will not change in the life of the node.
 *
 *----------------------------------------------------------------------
 */
RbcListNode    *
RbcListCreateNode(
    RbcList * listPtr,
    const char *key)
{                               /* Unique key to reference object */
    register RbcListNode *nodePtr;
    int             keySize;

    if (listPtr->type == TCL_STRING_KEYS) {
        keySize = strlen(key) + 1;
    } else if (listPtr->type == TCL_ONE_WORD_KEYS) {
        keySize = sizeof(int);
    } else {
        keySize = sizeof(int) * listPtr->type;
    }
    nodePtr = RbcCalloc(1, sizeof(RbcListNode) + keySize - 4);
    assert(nodePtr);
    nodePtr->clientData = NULL;
    nodePtr->nextPtr = nodePtr->prevPtr = NULL;
    nodePtr->listPtr = listPtr;
    switch (listPtr->type) {
    case TCL_STRING_KEYS:
        strcpy(nodePtr->key.string, key);
        break;
    case TCL_ONE_WORD_KEYS:
        nodePtr->key.oneWordValue = key;
        break;
    default:
        memcpy(nodePtr->key.words, key, keySize);
        break;
    }
    return nodePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcListReset --
 *
 *      Removes all the entries from a list, removing pointers to the
 *      objects and keys (not the objects or keys themselves).  The
 *      node counter is reset to zero.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcListReset(
    RbcList * listPtr)
{                               /* List to clear */
    if (listPtr != NULL) {
        register RbcListNode *oldPtr;
        register RbcListNode *nodePtr = listPtr->headPtr;

        while (nodePtr != NULL) {
            oldPtr = nodePtr;
            nodePtr = nodePtr->nextPtr;
            FreeNode(oldPtr);
        }
        RbcListInit(listPtr, listPtr->type);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcListDestroy
 *
 *     Frees all list structures
 *
 * Results:
 *      Returns a pointer to the newly created list structure.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcListDestroy(
    RbcList * listPtr)
{
    if (listPtr != NULL) {
        RbcListReset(listPtr);
        ckfree((char *) listPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcListInit --
 *
 *      Initializes a linked list.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcListInit(
    RbcList * listPtr,
    int type)
{
    listPtr->nNodes = 0;
    listPtr->headPtr = listPtr->tailPtr = NULL;
    listPtr->type = type;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcListLinkAfter --
 *
 *      Inserts an node following a given node.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcListLinkAfter(
    RbcList * listPtr,
    RbcListNode * nodePtr,
    RbcListNode * afterPtr)
{
    if (listPtr->headPtr == NULL) {
        listPtr->tailPtr = listPtr->headPtr = nodePtr;
    } else {
        if (afterPtr == NULL) {
            /* Prepend to the front of the list */
            nodePtr->nextPtr = listPtr->headPtr;
            nodePtr->prevPtr = NULL;
            listPtr->headPtr->prevPtr = nodePtr;
            listPtr->headPtr = nodePtr;
        } else {
            nodePtr->nextPtr = afterPtr->nextPtr;
            nodePtr->prevPtr = afterPtr;
            if (afterPtr == listPtr->tailPtr) {
                listPtr->tailPtr = nodePtr;
            } else {
                afterPtr->nextPtr->prevPtr = nodePtr;
            }
            afterPtr->nextPtr = nodePtr;
        }
    }
    nodePtr->listPtr = listPtr;
    listPtr->nNodes++;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcListLinkBefore --
 *
 *      Inserts an node preceding a given node.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcListLinkBefore(
    RbcList * listPtr,          /* List to contain new node */
    RbcListNode * nodePtr,      /* New node to be inserted */
    RbcListNode * beforePtr)
{                               /* Node to link before */
    if (listPtr->headPtr == NULL) {
        listPtr->tailPtr = listPtr->headPtr = nodePtr;
    } else {
        if (beforePtr == NULL) {
            /* Append onto the end of the list */
            nodePtr->nextPtr = NULL;
            nodePtr->prevPtr = listPtr->tailPtr;
            listPtr->tailPtr->nextPtr = nodePtr;
            listPtr->tailPtr = nodePtr;
        } else {
            nodePtr->prevPtr = beforePtr->prevPtr;
            nodePtr->nextPtr = beforePtr;
            if (beforePtr == listPtr->headPtr) {
                listPtr->headPtr = nodePtr;
            } else {
                beforePtr->prevPtr->nextPtr = nodePtr;
            }
            beforePtr->prevPtr = nodePtr;
        }
    }
    nodePtr->listPtr = listPtr;
    listPtr->nNodes++;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcListUnlinkNode --
 *
 *      Unlinks an node from the given list. The node itself is
 *      not deallocated, but only removed from the list.
 *
 * Results:
 *      None.
 *
 *----------------------------------------------------------------------
 */
void
RbcListUnlinkNode(
    RbcListNode * nodePtr)
{
    RbcList        *listPtr;

    listPtr = nodePtr->listPtr;
    if (listPtr != NULL) {
        if (listPtr->headPtr == nodePtr) {
            listPtr->headPtr = nodePtr->nextPtr;
        }
        if (listPtr->tailPtr == nodePtr) {
            listPtr->tailPtr = nodePtr->prevPtr;
        }
        if (nodePtr->nextPtr != NULL) {
            nodePtr->nextPtr->prevPtr = nodePtr->prevPtr;
        }
        if (nodePtr->prevPtr != NULL) {
            nodePtr->prevPtr->nextPtr = nodePtr->nextPtr;
        }
        nodePtr->listPtr = NULL;
        listPtr->nNodes--;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcListGetNode --
 *
 *      Find the first node matching the key given.
 *
 * Results:
 *      Returns the pointer to the node.  If no node matching
 *      the key given is found, then NULL is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcListNode    *
RbcListGetNode(
    RbcList * listPtr,          /* List to search */
    const char *key)
{                               /* Key to match */
    if (listPtr != NULL) {
        switch (listPtr->type) {
        case TCL_STRING_KEYS:
            return FindString(listPtr, key);
        case TCL_ONE_WORD_KEYS:
            return FindOneWord(listPtr, key);
        default:
            return FindArray(listPtr, key);
        }
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcListDeleteNode --
 *
 *      Unlinks and deletes the given node.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcListDeleteNode(
    RbcListNode * nodePtr)
{
    RbcListUnlinkNode(nodePtr);
    FreeNode(nodePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rb_ListDeleteNodeByKey --
 *
 *      Find the node and free the memory allocated for the node.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
Rb_ListDeleteNodeByKey(
    RbcList * listPtr,
    const char *key)
{
    RbcListNode    *nodePtr;

    nodePtr = RbcListGetNode(listPtr, key);
    if (nodePtr != NULL) {
        RbcListDeleteNode(nodePtr);
    }
}

/*
 *--------------------------------------------------------------
 *
 * RbcListAppend --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
RbcListNode    *
RbcListAppend(
    RbcList * listPtr,
    const char *key,
    ClientData clientData)
{
    RbcListNode    *nodePtr;

    nodePtr = RbcListCreateNode(listPtr, key);
    RbcListSetValue(nodePtr, clientData);
    RbcListAppendNode(listPtr, nodePtr);
    return nodePtr;
}

/*
 *--------------------------------------------------------------
 *
 * RbcListPrepend --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
RbcListNode    *
RbcListPrepend(
    RbcList * listPtr,
    const char *key,
    ClientData clientData)
{
    RbcListNode    *nodePtr;

    nodePtr = RbcListCreateNode(listPtr, key);
    RbcListSetValue(nodePtr, clientData);
    RbcListPrependNode(listPtr, nodePtr);
    return nodePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcListGetNthNode --
 *
 *      Find the node based upon a given position in list.
 *
 * Results:
 *      Returns the pointer to the node, if that numbered element
 *      exists. Otherwise NULL.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcListNode    *
RbcListGetNthNode(
    RbcList * listPtr,          /* List to traverse */
    int position,               /* Index of node to select from front
                                 * or back of the list. */
    int direction)
{
    register RbcListNode *nodePtr;

    if (listPtr != NULL) {
        if (direction > 0) {
            for (nodePtr = listPtr->headPtr; nodePtr != NULL;
                nodePtr = nodePtr->nextPtr) {
                if (position == 0) {
                    return nodePtr;
                }
                position--;
            }
        } else {
            for (nodePtr = listPtr->tailPtr; nodePtr != NULL;
                nodePtr = nodePtr->prevPtr) {
                if (position == 0) {
                    return nodePtr;
                }
                position--;
            }
        }
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcListSort --
 *
 *      Find the node based upon a given position in list.
 *
 * Results:
 *      Returns the pointer to the node, if that numbered element
 *      exists. Otherwise NULL.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcListSort(
    RbcList * listPtr,          /* List to traverse */
    RbcListCompareProc * proc)
{
    RbcListNode   **nodeArr;
    register RbcListNode *nodePtr;
    register int    i;

    if (listPtr->nNodes < 2) {
        return;
    }
    nodeArr =
        (RbcListNode **) ckalloc(sizeof(RbcList *) * (listPtr->nNodes + 1));
    if (nodeArr == NULL) {
        return;                 /* Out of memory. */
    }
    i = 0;
    for (nodePtr = listPtr->headPtr; nodePtr != NULL;
        nodePtr = nodePtr->nextPtr) {
        nodeArr[i++] = nodePtr;
    }
    qsort((char *) nodeArr, listPtr->nNodes,
        sizeof(RbcListNode *), (QSortCompareProc *) proc);

    /* Rethread the list. */
    nodePtr = nodeArr[0];
    listPtr->headPtr = nodePtr;
    nodePtr->prevPtr = NULL;
    for (i = 1; i < listPtr->nNodes; i++) {
        nodePtr->nextPtr = nodeArr[i];
        nodePtr->nextPtr->prevPtr = nodePtr;
        nodePtr = nodePtr->nextPtr;
    }
    listPtr->tailPtr = nodePtr;
    nodePtr->nextPtr = NULL;
    ckfree((char *) nodeArr);
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
