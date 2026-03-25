/*
 * tkWaylandXId.c --
 *
 *	Window-ID scanning and legacy compatibility shims for the
 *	Wayland/GLFW/libcg Tk port.
 *
 * Copyright © 1993 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 * Copyright © 2026      Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkWaylandInt.h"
#include <string.h>

/*
 *----------------------------------------------------------------------
 *
 * Color conversion helpers
 *
 *	Convert between Tk colors and libcg colors.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Tk_PixelToCGColor --
 *
 *	Convert a pixel value (RGB encoded as 0xRRGGBB) to a cg_color_t.
 *
 * Results:
 *	Returns a cg_color_t.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
struct cg_color_t
Tk_PixelToCGColor(unsigned long pixel)
{
    return TkGlfwPixelToCG(pixel);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GCToCGColor --
 *
 *	Extract the foreground color from a GC and return as cg_color_t.
 *
 * Results:
 *	Returns a cg_color_t.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
struct cg_color_t
Tk_GCToCGColor(GC gc)
{
    if (gc) {
        XGCValues v;
        if (TkWaylandGetGCValues(gc, GCForeground, &v) == 0) {
            return TkGlfwPixelToCG(v.foreground);
        }
    }
    /* Default to black */
    struct cg_color_t black = {0.0, 0.0, 0.0, 1.0};
    return black;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GCToCGBackground --
 *
 *	Extract the background color from a GC and return as cg_color_t.
 *
 * Results:
 *	Returns a cg_color_t.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
struct cg_color_t
Tk_GCToCGBackground(GC gc)
{
    if (gc) {
        XGCValues v;
        if (TkWaylandGetGCValues(gc, GCBackground, &v) == 0) {
            return TkGlfwPixelToCG(v.background);
        }
    }
    /* Default to white */
    struct cg_color_t white = {1.0, 1.0, 1.0, 1.0};
    return white;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
