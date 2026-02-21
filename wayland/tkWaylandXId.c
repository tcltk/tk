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
 * Tk_SetNanoVGContext --
 *
 *	Public entry point – registers the NanoVG context that will be
 *	used for pixmap operations. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the module-level NVGcontext pointer in tkWaylandGC.c.
 *
 *----------------------------------------------------------------------
 */

void
Tk_SetNanoVGContext(
    NVGcontext *vg)
{
    TkWaylandSetNVGContext(vg);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetPixmap -- legacy forwarder
 *
 *	Preserved for binary / source compatibility.  New code should
 *	call TkWaylandCreatePixmap directly.
 *
 *----------------------------------------------------------------------
 */

Pixmap
Tk_GetPixmap(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Drawable),
    int width,
    int height,
    int depth)
{
    return TkWaylandCreatePixmap(width, height, depth);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_FreePixmap -- legacy forwarder
 *
 *----------------------------------------------------------------------
 */

void
Tk_FreePixmap(
    TCL_UNUSED(Display *),
    Pixmap pixmap)
{
    TkWaylandFreePixmap(pixmap);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetPixmapImageId -- legacy forwarder
 *
 *----------------------------------------------------------------------
 */

int
Tk_GetPixmapImageId(
    Pixmap pixmap)
{
    return TkWaylandGetPixmapImageId(pixmap);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetPixmapPaint -- legacy forwarder
 *
 *----------------------------------------------------------------------
 */

NVGpaint *
Tk_GetPixmapPaint(
    Pixmap pixmap)
{
    return TkWaylandGetPixmapPaint(pixmap);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetPixmapType -- legacy forwarder
 *
 *----------------------------------------------------------------------
 */

int
Tk_GetPixmapType(
    Pixmap pixmap)
{
    return TkWaylandGetPixmapType(pixmap);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetPixmapDimensions -- legacy forwarder
 *
 *----------------------------------------------------------------------
 */

void
Tk_GetPixmapDimensions(
    Pixmap pixmap,
    int   *width,
    int   *height,
    int   *depth)
{
    TkWaylandGetPixmapDimensions(pixmap, width, height, depth);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_UpdatePixmapImage -- legacy forwarder
 *
 *----------------------------------------------------------------------
 */

int
Tk_UpdatePixmapImage(
    Pixmap               pixmap,
    const unsigned char *data)
{
    return TkWaylandUpdatePixmapImage(pixmap, data);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_CleanupPixmapStore -- legacy forwarder
 *
 *----------------------------------------------------------------------
 */

void
Tk_CleanupPixmapStore(void)
{
    TkWaylandCleanupPixmapStore();
}


/*
 *----------------------------------------------------------------------
 *
 * NVGcolor helper forwarders
 *
 *	Previously defined here; now just forward to the centralised
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
