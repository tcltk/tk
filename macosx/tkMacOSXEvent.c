/* 
 * tkMacOSXEvent.c --
 *
 * This file contains the basic Mac OS X Event handling routines.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright 2001, Apple Computer, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkMacOSXEvent.c,v 1.7 2005/09/10 14:53:20 das Exp $
 */

#include <stdio.h>

#include "tkMacOSXInt.h"
#include "tkMacOSXEvent.h"
#include "tkMacOSXDebug.h"

/*   
 * Forward declarations of procedures used in this file.
 */ 

static int TkMacOSXProcessAppleEvent(
        TkMacOSXEvent * eventPtr, MacEventStatus * statusPtr);

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXFlushWindows --
 *
 *      This routine flushes all the Carbon windows of the application.  It
 *      is called by the setup procedure for the Tcl/Carbon event source.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Flushes all Carbon windows
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkMacOSXFlushWindows ()
{
    WindowRef wRef = GetWindowList();
    
    while (wRef) {
        CGrafPtr portPtr = GetWindowPort(wRef);
        if (QDIsPortBuffered(portPtr)) {
            QDFlushPortBuffer(portPtr, NULL);
        }
        wRef = GetNextWindow(wRef);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXProcessAppleEvent --
 *
 *      This processes Apple events
 *
 * Results:
 *      0 on success
 *      -1 on failure
 *
 * Side effects:
 *      Calls the Tk high-level event handler
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
        err = TkMacOSXDoHLEvent(&eventRecord);
        if (err != noErr) {
#ifdef TK_MAC_DEBUG
            char buf1 [ 256 ];
            char buf2 [ 256 ];
            fprintf(stderr,
                "TkMacOSXDoHLEvent failed : %s,%s,%d\n",
                CarbonEventToAscii(eventPtr->eventRef, buf1),
                ClassicEventToAscii(&eventRecord,buf2), err);
#endif
            statusPtr->err = 1;
        }
    } else {
#ifdef TK_MAC_DEBUG
        fprintf(stderr,"ConvertEventRefToEventRecord failed\n");
#endif
        statusPtr->err = 1;
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
 *      Note on MacEventStatus.stopProcessing: Please be conservative in the
 *      individual handlers and don't assume the event is fully handled
 *      unless you *really* need to ensure that other handlers don't see the
 *      event anymore.  Some OS manager or library might be interested in
 *      events even after they are already handled on the Tk level.
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

MODULE_SCOPE int  
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
        default:
#ifdef TK_MAC_DEBUG
            {
                char buf [256];
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
 * TkMacOSXReceiveAndProcessEvent --
 *
 *      This receives a carbon event and converts it to a Tk event
 *
 * Results:
 *      0 on success
 *      Mac OS error number on failure
 *
 * Side effects:
 *      This receives the next Carbon event and converts it to the
 *      appropriate Tk event
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE OSStatus
TkMacOSXReceiveAndProcessEvent()
{
    static EventTargetRef targetRef = NULL;
    EventRef eventRef;
    OSStatus err;

    /*
     * This is a poll, since we have already counted the events coming
     * into this routine, and are guaranteed to have one waiting.
     */
     
    err = ReceiveNextEvent(0, NULL, kEventDurationNoWait, true, &eventRef);
    if (err == noErr) {
        if (!targetRef) {
            targetRef = GetEventDispatcherTarget();
        }
        err = SendEventToEventTarget(eventRef,targetRef);
#ifdef TK_MAC_DEBUG
        if (err != noErr && err != eventLoopTimedOutErr
                && err != eventNotHandledErr
        ) {
            char buf [256];
            fprintf(stderr,
                    "RCNE SendEventToEventTarget (%s) failed, %d\n",
                    CarbonEventToAscii(eventRef, buf), (int)err);
        }
#endif
        ReleaseEvent(eventRef);
    }
    return err;
}
