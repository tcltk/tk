/* 
 * tkMacOSXEvent.c --
 *
 * This file contains most of the X calls called by Tk.  Many of
 * these calls are just stubs and either don't make sense on the
 * Macintosh or thier implamentation just doesn't do anything.  Other
 * calls will eventually be moved into other files.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright 2001, Apple Computer, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkMacOSXEvent.c,v 1.1.2.2 2002/02/05 02:25:17 wolfsuit Exp $
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include "tkMacOSXInt.h"
#include "tkMacOSXEvent.h"
#include "tkMacOSXDebug.h"

#define TK_MAC_DEBUG 1

/*
 * The following are undocumented event classes
 *
 */
enum {
    kEventClassUser = 'user',
    kEventClassCgs  = 'cgs ',
};
 
/*  
 * The following are undocumented event kinds 
 * 
 */ 
enum {
    kEventMouse8 = 8,
    kEventMouse9 = 9, 
    kEventApp103 = 103
};   

EventRef TkMacOSXCreateFakeEvent ();

/*   
 * Forward declarations of procedures used in this file.
 */ 
static int ReceiveAndProcessEvent _ANSI_ARGS_(());

static EventTargetRef targetRef;

/*
 *----------------------------------------------------------------------
 *
 * tkMacOSXFlushWindows --
 *
 *      This routine flushes all the Carbon windows of the application
 *      It is called by the setup procedure for the Tcl/Carbon event source
 * Results:
 *      None.
 *
 * Side effects:
 *      Flushes all Carbon windows
 *
 *----------------------------------------------------------------------
 */

void
tkMacOSXFlushWindows ()
{
    WindowRef wRef = GetWindowList();
    
    while (wRef) {
        CGrafPtr portPtr = GetWindowPort(wRef);
        if (QDIsPortBuffered(portPtr)) {
            QDFlushPortBuffer(portPtr, NULL);
        }
        wRef=GetNextWindow(wRef);
    }
}
/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXCountAndProcessMacEvents --
 *
 *      This routine receives any Carbon events that aare in the 
 *      queue and converts them to tk events
 *      It is called by the event set-up and check routines
 * Results:
 *      The number of events in the queue.
 *
 * Side effects:
 *      Tells the Window Manager to deliver events to the event 
 *      queue of the current thread.
 *      Receives any Carbon events on the queue and converts them to tk events
 *
 *----------------------------------------------------------------------
 */

int
TkMacOSXCountAndProcessMacEvents()
{
    EventQueueRef qPtr;
    int           eventCount;
    qPtr = GetMainEventQueue();
    eventCount = GetNumEventsInQueue(qPtr);
    if (eventCount) {
        int n, err;
        for (n = 0, err = 0;n<eventCount && !err;n++) {
            err = ReceiveAndProcessEvent();
        }
    }
    return eventCount;
}
/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXProcessAppleEvent --
 *
 *      This processes Apple events
 *
 * Results:
 *               0 on success
 *               -1 on failure
 *
 * Side effects:
 *               Calls the Tk high-level event handler
 *
 *----------------------------------------------------------------------
 */

static int
TkMacOSXProcessAppleEvent(TkMacOSXEvent * eventPtr, MacEventStatus * statusPtr)
{
    int  err;
    EventRecord eventRecord;
    if (ConvertEventRefToEventRecord(eventPtr->eventRef,
        &eventRecord )) {
        err=TkMacOSXDoHLEvent(&eventRecord);
        if (err!=noErr) {
            char buf1 [ 256 ];
            char buf2 [ 256 ];
            fprintf(stderr,
                "TkMacOSXDoHLEvent failed : %s,%s,%d\n",
                CarbonEventToAscii(eventPtr->eventRef, buf1),
                ClassicEventToAscii(&eventRecord,buf2), err);
            statusPtr->err = 1;
        } else {
            statusPtr->handledByTk = 1;
        }
    } else {
        statusPtr->err = 1;
        fprintf(stderr,"ConvertEventRefToEventRecord failed\n");
    }
    return 0;
}

/*      
 *----------------------------------------------------------------------
 *   
 * TkMacOSXProcessEvent --
 *   
 *      This dispatches a filtered Carbon event to the appropriate handler
 *
 * Results: 
 *      0 on success
 *      -1 on failure
 *
 * Side effects:
 *      Converts a Carbon event to a Tk event
 *   
 *----------------------------------------------------------------------
 */

static int  
TkMacOSXProcessEvent(TkMacOSXEvent * eventPtr, MacEventStatus * statusPtr)
{
    switch (eventPtr->eClass) {
        case kEventClassMouse:
            TkMacOSXProcessMouseEvent(eventPtr, statusPtr);
            break;
        case kEventClassWindow:
            TkMacOSXProcessWindowEvent(eventPtr, statusPtr);
            break;  
        case kEventClassKeyboard:
            TkMacOSXProcessKeyboardEvent(eventPtr, statusPtr);
            break;
        case kEventClassApplication:
            TkMacOSXProcessApplicationEvent(eventPtr, statusPtr);
            break;
        case kEventClassAppleEvent:
            TkMacOSXProcessAppleEvent(eventPtr, statusPtr);
            break;  
        case kEventClassCgs:
        case kEventClassUser:
        case kEventClassWish: 
            statusPtr->handledByTk = 1;
            break;  
        default:
#ifdef TK_MAC_DEBUG
            if (0)
            {
                char buf [ 256 ];
                fprintf(stderr,
                    "Unrecognised event : %s\n",
                    CarbonEventToAscii(eventPtr->eventRef, buf));
            }
#endif
            break;
    }   
    return 0;
}   

/*
 *----------------------------------------------------------------------
 *
 * ReceiveAndProcessEvent --
 *
 *      This receives a carbon event and converts it to a tk event
 *
 * Results:
 *      0 on success
 *      Mac OS error number on failure
 *
 * Side effects:
 *      This receives the next Carbon event
 *      and converts it to the appropriate tk event
 *
 *----------------------------------------------------------------------
 */

static int
ReceiveAndProcessEvent()
{
    TkMacOSXEvent       macEvent;
    MacEventStatus   eventStatus;
    int              err;
    char             buf [ 256 ];

    /*
     * This is a poll, since we have already counted the events coming
     * into this routine, and are guaranteed to have one waiting.
     */
     
    err=ReceiveNextEvent(0, NULL, kEventDurationNoWait, 
            true, &macEvent.eventRef);
    if (err != noErr) {
        return err;
    } else {
        macEvent.eClass = GetEventClass(macEvent.eventRef);
        macEvent.eKind = GetEventKind(macEvent.eventRef);
        bzero(&eventStatus, sizeof(eventStatus));
        TkMacOSXProcessEvent(&macEvent,&eventStatus);
        if (!eventStatus.handledByTk) {
            if (!targetRef) {
                targetRef=GetEventDispatcherTarget();
            }
            
            err= SendEventToEventTarget(macEvent.eventRef,targetRef);
            if (err != noErr) {
                fprintf(stderr,
                        "RCNE SendEventToEventTarget (%s) failed, %d\n",
                        CarbonEventToAscii(macEvent.eventRef,buf ),err);
            }
         }
         ReleaseEvent(macEvent.eventRef);
         return 0;
     }
}
