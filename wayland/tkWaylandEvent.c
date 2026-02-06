/*
 * tkWaylandEvent.c --
 *
 *	This file implements an event management functionality for the Wayland backend 
 *	of Tk.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 *
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkUnixInt.h"
#include <GLFW/glfw3.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

int
XSync(
    Display *display,
    TCL_UNUSED(Bool))
{
    /*
     *  The main use of XSync is by the update command, which alternates
     *  between running an event loop to process all events without waiting and
     *  calling XSync on all displays until no events are left.  On X11 the
     *  call to XSync might cause the window manager to generate more events
     *  which would then get processed. Apparently this process stabilizes on
     *  X11, leaving the window manager in a state where all events have been
     *  generated and no additional events can be genereated by updating widgets.
     *
     *  It is not clear what the Wayland port should do when XSync is called, but
     *  currently the best option seems to be to do nothing.  (See ticket
     *  [da5f2266df].)
     */

    LastKnownRequestProcessed(display)++;
    return 0;
}


/* Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
