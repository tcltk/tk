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
 * Legacy GC forwarders
 *
 *	Tk_CreateGC, Tk_FreeGC and friends were declared in this file
 *	in the original version.  They are now thin forwarders to the
 *	canonical TkWayland* functions so that any remaining callers
 *	continue to link.
 *
 *----------------------------------------------------------------------
 */

GC
Tk_CreateGC(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Drawable),
    unsigned long  valuemask,
    XGCValues     *values)
{
    return TkWaylandCreateGC(valuemask, values);
}

int
Tk_FreeGC(
    TCL_UNUSED(Display *),
    GC gc)
{
    TkWaylandFreeGC(gc);
    return Success;
}

int
Tk_SetGCForeground(
    TCL_UNUSED(Display *),
    GC            gc,
    unsigned long foreground)
{
    XGCValues v;
    v.foreground = foreground;
    return TkWaylandChangeGC(gc, GCForeground, &v) ? Success : BadGC;
}

int
Tk_SetGCBackground(
    TCL_UNUSED(Display *),
    GC            gc,
    unsigned long background)
{
    XGCValues v;
    v.background = background;
    return TkWaylandChangeGC(gc, GCBackground, &v) ? Success : BadGC;
}

int
Tk_SetGCLineWidth(
    TCL_UNUSED(Display *),
    GC  gc,
    int line_width)
{
    XGCValues v;
    v.line_width = line_width;
    return TkWaylandChangeGC(gc, GCLineWidth, &v) ? Success : BadGC;
}

int
Tk_SetGCLineAttributes(
    TCL_UNUSED(Display *),
    GC  gc,
    int line_style,
    int cap_style,
    int join_style)
{
    XGCValues v;
    v.line_style = line_style;
    v.cap_style  = cap_style;
    v.join_style = join_style;
    return TkWaylandChangeGC(
        gc, GCLineStyle | GCCapStyle | GCJoinStyle, &v) ? Success : BadGC;
}

int
Tk_GetGCValues(
    TCL_UNUSED(Display *),
    GC             gc,
    unsigned long  valuemask,
    XGCValues     *values)
{
    return TkWaylandGetGCValues(gc, valuemask, values);
}

int
Tk_ChangeGC(
    TCL_UNUSED(Display *),
    GC             gc,
    unsigned long  valuemask,
    XGCValues     *values)
{
    return TkWaylandChangeGC(gc, valuemask, values);
}

int
Tk_CopyGC(
    TCL_UNUSED(Display *),
    GC            src,
    unsigned long valuemask,
    GC            dst)
{
    return TkWaylandCopyGC(src, valuemask, dst) ? Success : BadGC;
}

int
Tk_GetGCLineWidth(
    GC gc)
{
    XGCValues v;
    if (TkWaylandGetGCValues(gc, GCLineWidth, &v)) {
        return v.line_width;
    }
    return 1;
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
