/*
 * tkWaylandXId.c --
 *
 *	Window-ID scanning and legacy compatibility shims for the
 *	Wayland/GLFW/NanoVG Tk port.
 *
 * Copyright © 1993 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 * Copyright © 2026      Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <string.h>

/*
 *----------------------------------------------------------------------
 *
 * NVGcolor helper forwarders
 *
 *	Previously defined here; now just forward to the centralized
 *	helpers in tkGlfwInit.c / tkWaylandGC.c.
 *
 *----------------------------------------------------------------------
 */

NVGcolor
Tk_PixelToNVGColor(
    unsigned long pixel)
{
    return TkGlfwPixelToNVG(pixel);
}

NVGcolor
Tk_GCToNVGColor(
    GC gc)
{
    if (gc) {
        XGCValues v;
        if (TkWaylandGetGCValues(gc, GCForeground, &v)) {
            return TkGlfwPixelToNVG(v.foreground);
        }
    }
    return nvgRGBA(0, 0, 0, 255);
}

NVGcolor
Tk_GCToNVGBackground(
    GC gc)
{
    if (gc) {
        XGCValues v;
        if (TkWaylandGetGCValues(gc, GCBackground, &v)) {
            return TkGlfwPixelToNVG(v.background);
        }
    }
    return nvgRGBA(255, 255, 255, 255);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
