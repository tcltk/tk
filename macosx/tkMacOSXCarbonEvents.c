/*
 * tkMacOSXCarbonEvents.c --
 *
 *	This file implements functions that register for and handle
 *      various Carbon Events.  The reason a separate set of handlers 
 *      is necessary is that not all interesting events get delivered
 *      directly to the event queue through ReceiveNextEvent.  Some only
 *      get delivered if you register a Carbon Event Handler for the event.
 *
 *      Copyright 2001, Apple Computer, Inc.
 *
 *      The following terms apply to all files originating from Apple
 *      Computer, Inc. ("Apple") and associated with the software
 *      unless explicitly disclaimed in individual files.
 *
 *
 *      Apple hereby grants permission to use, copy, modify,
 *      distribute, and license this software and its documentation
 *      for any purpose, provided that existing copyright notices are
 *      retained in all copies and that this notice is included
 *      verbatim in any distributions. No written agreement, license,
 *      or royalty fee is required for any of the authorized
 *      uses. Modifications to this software may be copyrighted by
 *      their authors and need not follow the licensing terms
 *      described here, provided that the new terms are clearly
 *      indicated on the first page of each file where they apply.
 *
 *
 *      IN NO EVENT SHALL APPLE, THE AUTHORS OR DISTRIBUTORS OF THE
 *      SOFTWARE BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT, SPECIAL,
 *      INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF
 *      THIS SOFTWARE, ITS DOCUMENTATION, OR ANY DERIVATIVES THEREOF,
 *      EVEN IF APPLE OR THE AUTHORS HAVE BEEN ADVISED OF THE
 *      POSSIBILITY OF SUCH DAMAGE.  APPLE, THE AUTHORS AND
 *      DISTRIBUTORS SPECIFICALLY DISCLAIM ANY WARRANTIES, INCLUDING,
 *      BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
 *      FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT.  THIS
 *      SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, AND APPLE,THE
 *      AUTHORS AND DISTRIBUTORS HAVE NO OBLIGATION TO PROVIDE
 *      MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 *      GOVERNMENT USE: If you are acquiring this software on behalf
 *      of the U.S. government, the Government shall have only
 *      "Restricted Rights" in the software and related documentation
 *      as defined in the Federal Acquisition Regulations (FARs) in
 *      Clause 52.227.19 (c) (2).  If you are acquiring the software
 *      on behalf of the Department of Defense, the software shall be
 *      classified as "Commercial Computer Software" and the
 *      Government shall have only "Restricted Rights" as defined in
 *      Clause 252.227-7013 (c) (1) of DFARs.  Notwithstanding the
 *      foregoing, the authors grant the U.S. Government and others
 *      acting in its behalf permission to use and distribute the
 *      software in accordance with the terms specified in this
 *      license.
 */

#include "tkInt.h"
#include "tkMacOSXInt.h"

static EventHandlerRef ApplicationCarbonEventHandler;

/* Definitions of functions used only in this file */
static OSStatus AppEventHandlerProc (
                              EventHandlerCallRef callRef,
                              EventRef inEvent,
                              void *userData);


/*
 *----------------------------------------------------------------------
 *
 * AppEventHandlerProc --
 *
 *        This procedure is the Application CarbonEvent
 *        handler.  Currently, it handles the Hide & Show
 *        events.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        Dispatches CarbonEvents.
 *
 *----------------------------------------------------------------------
 */

static OSStatus
AppEventHandlerProc (
        EventHandlerCallRef callRef,
        EventRef inEvent,
        void *inUserData)
{
    Tcl_CmdInfo dummy;
    Tcl_Interp *interp = (Tcl_Interp *) inUserData;
    
    /* 
     * This is a bit of a hack.  We get "show" events both when we come back
     * from being hidden, and whenever we are activated.  I only want to run the
     * "show" proc when we have been hidden already, not as a substitute for
     * <Activate>.  So I use this toggle...
     */
     
    static int toggleHide = 0;
    
    switch(GetEventKind (inEvent))
    {
        case kEventAppHidden:
        /*
         * Don't bother if we don't have an interp or
         * the show preferences procedure doesn't exist.
         */
            toggleHide = 1;
     
            if ((interp == NULL) || 
                    (Tcl_GetCommandInfo(interp, 
                    "::tk::mac::OnHide", &dummy)) == 0) {
                return eventNotHandledErr;
            }
            Tcl_GlobalEval(interp, "::tk::mac::OnHide");
            break;
        case kEventAppShown:
            if (toggleHide == 1) {
                toggleHide = 0;
                if ((interp == NULL) || 
                        (Tcl_GetCommandInfo(interp, 
                        "::tk::mac::OnShow", &dummy)) == 0) {
                    return eventNotHandledErr;
                }
                Tcl_GlobalEval(interp, "::tk::mac::OnShow");
            }
            break;
       default:
            break;
    }
    return eventNotHandledErr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXInitCarbonEvents --
 *
 *        This procedure initializes the Application CarbonEvent
 *        handler.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        A handler for Application targeted Carbon Events is registered.
 *
 *----------------------------------------------------------------------
 */

void
TkMacOSXInitCarbonEvents (
        Tcl_Interp *interp)
{
    const EventTypeSpec inAppEventTypes[] = {
            {kEventClassApplication, kEventAppHidden},
            {kEventClassApplication, kEventAppShown}};
    int inNumTypes = sizeof (inAppEventTypes) / sizeof (EventTypeSpec);

    InstallEventHandler(GetApplicationEventTarget(),
            NewEventHandlerUPP(AppEventHandlerProc),
            inNumTypes, inAppEventTypes, (void *) interp, 
            &ApplicationCarbonEventHandler);
    
}

