/*
 * rbcChain.c --
 *
 * The module implements a generic linked list package.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkoGraph.h"

#ifndef ALIGN
#define ALIGN(a) \
    (((size_t)a + (sizeof(double) - 1)) & (~(sizeof(double) - 1)))
#endif /* ALIGN */

/*
 *----------------------------------------------------------------------
 *
 * RbcChainCreate --
 *
 *      Creates a new linked list (chain) structure and initializes
 *      its pointers;
 *
 * Results:
 *      Returns a pointer to the newly created chain structure.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcChain *
RbcChainCreate(
    )
{
RbcChain *chainPtr;

    chainPtr = (RbcChain *) ckalloc(sizeof(RbcChain));
    if(chainPtr != NULL) {
        RbcChainInit(chainPtr);
    }
    return chainPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcChainInit --
 *
 *      Initializes a linked list.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcChainInit(
    RbcChain * chainPtr)
{              /* The chain to initialize */
    chainPtr->nLinks = 0;
    chainPtr->headPtr = chainPtr->tailPtr = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcChainLinkAfter --
 *
 *      Inserts an entry following a given entry.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcChainLinkAfter(
    RbcChain * chainPtr,
    RbcChainLink * linkPtr,
    RbcChainLink * afterPtr)
{
    if(chainPtr->headPtr == NULL) {
        chainPtr->tailPtr = chainPtr->headPtr = linkPtr;
    } else {
        if(afterPtr == NULL) {
            /* Prepend to the front of the chain */
            linkPtr->nextPtr = chainPtr->headPtr;
            linkPtr->prevPtr = NULL;
            chainPtr->headPtr->prevPtr = linkPtr;
            chainPtr->headPtr = linkPtr;
        } else {
            linkPtr->nextPtr = afterPtr->nextPtr;
            linkPtr->prevPtr = afterPtr;
            if(afterPtr == chainPtr->tailPtr) {
                chainPtr->tailPtr = linkPtr;
            } else {
                afterPtr->nextPtr->prevPtr = linkPtr;
            }
            afterPtr->nextPtr = linkPtr;
        }
    }
    chainPtr->nLinks++;
}

/*----------------------------------------------------------------------
 *
 * RbcChainLinkBefore --
 *
 *      Inserts a link preceding a given link.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcChainLinkBefore(
    RbcChain * chainPtr,       /* Chain to contain new entry */
    RbcChainLink * linkPtr,    /* New entry to be inserted */
    RbcChainLink * beforePtr)
{              /* Entry to link before */
    if(chainPtr->headPtr == NULL) {
        chainPtr->tailPtr = chainPtr->headPtr = linkPtr;
    } else {
        if(beforePtr == NULL) {
            /* Append to the end of the chain. */
            linkPtr->nextPtr = NULL;
            linkPtr->prevPtr = chainPtr->tailPtr;
            chainPtr->tailPtr->nextPtr = linkPtr;
            chainPtr->tailPtr = linkPtr;
        } else {
            linkPtr->prevPtr = beforePtr->prevPtr;
            linkPtr->nextPtr = beforePtr;
            if(beforePtr == chainPtr->headPtr) {
                chainPtr->headPtr = linkPtr;
            } else {
                beforePtr->prevPtr->nextPtr = linkPtr;
            }
            beforePtr->prevPtr = linkPtr;
        }
    }
    chainPtr->nLinks++;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcChainNewLink --
 *
 *      Creates a new link.
 *
 * Results:
 *      The return value is the pointer to the newly created link.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcChainLink *
RbcChainNewLink(
    )
{
RbcChainLink *linkPtr;

    linkPtr = (RbcChainLink *) ckalloc(sizeof(RbcChainLink));
    assert(linkPtr);
    linkPtr->clientData = NULL;
    linkPtr->nextPtr = linkPtr->prevPtr = NULL;
    return linkPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcChainReset --
 *
 *      Removes all the links from the chain, freeing the memory for
 *      each link.  Memory pointed to by the link (clientData) is not
 *      freed.  It's the caller's responsibility to deallocate it.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcChainReset(
    RbcChain * chainPtr)
{              /* Chain to clear */
    if(chainPtr != NULL) {
RbcChainLink *oldPtr;
RbcChainLink *linkPtr = chainPtr->headPtr;

        while(linkPtr != NULL) {
            oldPtr = linkPtr;
            linkPtr = linkPtr->nextPtr;
            ckfree((char *)oldPtr);
        }
        RbcChainInit(chainPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcChainDestroy
 *
 *      Frees all the nodes from the chain and deallocates the memory
 *      allocated for the chain structure itself.  It's assumed that
 *      the chain was previous allocated by RbcChainCreate.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
void
RbcChainDestroy(
    RbcChain * chainPtr)
{              /* The chain to destroy. */
    if(chainPtr != NULL) {
        RbcChainReset(chainPtr);
        ckfree((char *)chainPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcChainUnlinkLink --
 *
 *      Unlinks a link from the chain. The link is not deallocated,
 *      but only removed from the chain.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcChainUnlinkLink(
    RbcChain * chainPtr,
    RbcChainLink * linkPtr)
{
    /* Indicates if the link is actually removed from the chain. */
int unlinked;

    unlinked = FALSE;
    if(chainPtr->headPtr == linkPtr) {
        chainPtr->headPtr = linkPtr->nextPtr;
        unlinked = TRUE;
    }
    if(chainPtr->tailPtr == linkPtr) {
        chainPtr->tailPtr = linkPtr->prevPtr;
        unlinked = TRUE;
    }
    if(linkPtr->nextPtr != NULL) {
        linkPtr->nextPtr->prevPtr = linkPtr->prevPtr;
        unlinked = TRUE;
    }
    if(linkPtr->prevPtr != NULL) {
        linkPtr->prevPtr->nextPtr = linkPtr->nextPtr;
        unlinked = TRUE;
    }
    if(unlinked) {
        chainPtr->nLinks--;
    }
    linkPtr->prevPtr = linkPtr->nextPtr = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcChainDeleteLink --
 *
 *      Unlinks and also frees the given link.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcChainDeleteLink(
    RbcChain * chainPtr,
    RbcChainLink * linkPtr)
{
    RbcChainUnlinkLink(chainPtr, linkPtr);
    ckfree((char *)linkPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcChainAppend --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcChainLink *
RbcChainAppend(
    RbcChain * chainPtr,
    ClientData clientData)
{
RbcChainLink *linkPtr;

    linkPtr = RbcChainNewLink();
    RbcChainLinkBefore(chainPtr, linkPtr, (RbcChainLink *) NULL);
    RbcChainSetValue(linkPtr, clientData);
    return linkPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcChainPrepend --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcChainLink *
RbcChainPrepend(
    RbcChain * chainPtr,
    ClientData clientData)
{
RbcChainLink *linkPtr;

    linkPtr = RbcChainNewLink();
    RbcChainLinkAfter(chainPtr, linkPtr, (RbcChainLink *) NULL);
    RbcChainSetValue(linkPtr, clientData);
    return linkPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcChainAllocLink --
 *
 *      Creates a new chain link.  Unlink RbcChainNewLink, this
 *      routine also allocates extra memory in the node for data.
 *
 * Results:
 *      The return value is the pointer to the newly created entry.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcChainLink *
RbcChainAllocLink(
    unsigned int extraSize)
{
RbcChainLink *linkPtr;
unsigned int linkSize;

    linkSize = ALIGN(sizeof(RbcChainLink));
    linkPtr = RbcCalloc(1, linkSize + extraSize);
    assert(linkPtr);
    if(extraSize > 0) {
        /* Point clientData at the memory beyond the normal structure. */
        linkPtr->clientData = (ClientData) ((char *)linkPtr + linkSize);
    }
    return linkPtr;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
