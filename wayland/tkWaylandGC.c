/*
 * tkWaylandGC.c --
 *
 *	Graphics Context and Pixmap implementation for the
 *	Wayland/GLFW/libcg backend.
 *
 *	This file provides the central definitions of
 *	TkWaylandGCImpl and TkWaylandPixmapImpl and all TkWayland*
 *	entry points declared in tkWaylandInt.h.  The Xlib-compatible
 *	wrappers (XCreateGC, XFreeGC, XCreatePixmap, etc.) forward to
 *	these entry points and live here as well.
 *
 *	Also provides TkpOpenDisplay / TkpCloseDisplay (the Tk platform
 *	entry points) so that Tk can resolve screen information at startup.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2014 Marc Culler.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkWaylandInt.h"
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xlibint.h>

extern TkGlfwContext glfwContext;

/* GC magic number for validation */
#define TK_WAYLAND_GC_MAGIC 0x574C4743  /* "WLGC" */

/*
 *----------------------------------------------------------------------
 *
 * TkpOpenDisplay --
 *
 *	Tk platform entry point: allocate a TkDisplay for Wayland/GLFW.
 *	Sets up Screen, Visual, and queries primary monitor dimensions.
 *
 * Results:
 *	Pointer to newly allocated TkDisplay, or NULL on failure.
 *
 * Side effects:
 *	Allocates memory for Display, Screen, Visual, TkDisplay.
 *	Calls glfwInit().
 *
 *----------------------------------------------------------------------
 */

TkDisplay *
TkpOpenDisplay(TCL_UNUSED(const char *))
{
    _XPrivDisplay display;
    Screen *screen;
    Visual *visual;
    TkDisplay *dispPtr;
    int sw = 1920, sh = 1080;
    GLFWmonitor *mon;

    display = (_XPrivDisplay)ckalloc(sizeof(Display));
    if (!display) {
	return NULL;
    }
    bzero(display, sizeof(Display));

    screen = (Screen *)ckalloc(sizeof(Screen));
    if (!screen) {
	ckfree(display);
	return NULL;
    }

    visual = (Visual *)ckalloc(sizeof(Visual));
    if (!visual) {
	ckfree(screen);
	ckfree(display);
	return NULL;
    }
    bzero(visual, sizeof(Visual));

    if (!glfwInit()) {
	ckfree(visual);
	ckfree(screen);
	ckfree(display);
	return NULL;
    }

    mon = glfwGetPrimaryMonitor();
    if (mon) {
	const GLFWvidmode *mode = glfwGetVideoMode(mon);
	if (mode) {
	    sw = mode->width;
	    sh = mode->height;
	}
    }

    screen->width = sw;
    screen->height = sh;
    screen->mwidth = (sw * 254.0) / 720.0;
    screen->mheight = (sh * 254.0) / 720.0;

    display->screens = screen;
    display->nscreens = 1;
    display->default_screen = 0;
    display->display_name = (char *)"wayland-0";

    screen->display = (Display *)display;
    screen->root = 1;
    screen->root_visual = visual;
    screen->root_depth = 24;

    visual->visualid = 1;
    visual->class = TrueColor;
    visual->bits_per_rgb = 8;
    visual->map_entries = 256;
    visual->red_mask = 0xFF0000;
    visual->green_mask = 0x00FF00;
    visual->blue_mask = 0x0000FF;

    dispPtr = (TkDisplay *)ckalloc(sizeof(TkDisplay));
    bzero(dispPtr, sizeof(TkDisplay));
    dispPtr->display = (Display *)display;

    return dispPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpCloseDisplay --
 *
 *	Tk platform entry point: close and free a TkDisplay.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None (no-op; resources freed at shutdown).
 *
 *----------------------------------------------------------------------
 */

void
TkpCloseDisplay(TCL_UNUSED(TkDisplay *))
{
    /* no-op */
}

/*
 *----------------------------------------------------------------------
 *
 * Graphics Context functions
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCreateGC --
 *
 *	Allocate a new GC, optionally initialising fields from values/mask.
 *
 * Results:
 *	A freshly allocated GC, or NULL.
 *
 * Side effects:
 *	Allocates heap memory.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GC
TkWaylandCreateGC(
    unsigned long valuemask,
    XGCValues *values)
{
    TkWaylandGCImpl *gc;

    gc = (TkWaylandGCImpl *)ckalloc(sizeof(TkWaylandGCImpl));
    if (gc == NULL) {
	return NULL;
    }

    gc->magic = TK_WAYLAND_GC_MAGIC;
    gc->foreground = 0x000000;
    gc->background = 0xFFFFFF;
    gc->line_width = 1;
    gc->line_style = LineSolid;
    gc->cap_style = CapButt;
    gc->join_style = JoinMiter;
    gc->fill_rule = WindingRule;
    gc->arc_mode = ArcPieSlice;
    gc->font = NULL;

    if (values != NULL) {
	TkWaylandChangeGC((GC)gc, valuemask, values);
    }

    return (GC)gc;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandFreeGC --
 *
 *	Release a GC created by TkWaylandCreateGC.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees heap memory.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandFreeGC(GC gc)
{
    TkWaylandGCImpl *impl = (TkWaylandGCImpl *)gc;

    if (impl != NULL && impl->magic == TK_WAYLAND_GC_MAGIC) {
	impl->magic = 0;
	ckfree((char *)gc);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetGCValues --
 *
 *	Read fields out of a GC into an XGCValues struct.
 *
 * Results:
 *	1 on success, 0 if gc or values is NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandGetGCValues(
    GC gc,
    unsigned long valuemask,
    XGCValues *values)
{
    TkWaylandGCImpl *impl = (TkWaylandGCImpl *)gc;

    if (impl == NULL || values == NULL || impl->magic != TK_WAYLAND_GC_MAGIC) {
	return 0;
    }

    if (valuemask & GCForeground) {
	values->foreground = impl->foreground;
    }
    if (valuemask & GCBackground) {
	values->background = impl->background;
    }
    if (valuemask & GCLineWidth) {
	values->line_width = impl->line_width;
    }
    if (valuemask & GCLineStyle) {
	values->line_style = impl->line_style;
    }
    if (valuemask & GCCapStyle) {
	values->cap_style = impl->cap_style;
    }
    if (valuemask & GCJoinStyle) {
	values->join_style = impl->join_style;
    }
    if (valuemask & GCFillRule) {
	values->fill_rule = impl->fill_rule;
    }
    if (valuemask & GCArcMode) {
	values->arc_mode = impl->arc_mode;
    }
    if (valuemask & GCFont) {
	values->font = (Font)(uintptr_t)impl->font;
    }

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandChangeGC --
 *
 *	Write fields into a GC from an XGCValues struct.
 *
 * Results:
 *	1 on success, 0 if gc or values is NULL.
 *
 * Side effects:
 *	Updates GC fields in place.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandChangeGC(
    GC gc,
    unsigned long valuemask,
    XGCValues *values)
{
    TkWaylandGCImpl *impl = (TkWaylandGCImpl *)gc;

    if (impl == NULL || values == NULL || impl->magic != TK_WAYLAND_GC_MAGIC) {
	return 0;
    }

    if (valuemask & GCForeground) {
	impl->foreground = values->foreground;
    }
    if (valuemask & GCBackground) {
	impl->background = values->background;
    }
    if (valuemask & GCLineWidth) {
	impl->line_width = values->line_width;
    }
    if (valuemask & GCLineStyle) {
	impl->line_style = values->line_style;
    }
    if (valuemask & GCCapStyle) {
	impl->cap_style = values->cap_style;
    }
    if (valuemask & GCJoinStyle) {
	impl->join_style = values->join_style;
    }
    if (valuemask & GCFillRule) {
	impl->fill_rule = values->fill_rule;
    }
    if (valuemask & GCArcMode) {
	impl->arc_mode = values->arc_mode;
    }
    if (valuemask & GCFont) {
	impl->font = (void *)(uintptr_t)values->font;
    }

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCopyGC --
 *
 *	Copy selected fields from one GC to another.
 *
 * Results:
 *	1 on success, 0 if either gc is NULL.
 *
 * Side effects:
 *	Updates dst GC fields in place.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandCopyGC(
    GC src,
    unsigned long valuemask,
    GC dst)
{
    XGCValues tmp;

    if (src == NULL || dst == NULL) {
	return 0;
    }

    TkWaylandGetGCValues(src, valuemask, &tmp);
    TkWaylandChangeGC(dst, valuemask, &tmp);

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * Pixmap functions
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetPixmap --
 *
 *	Create an off-screen drawable (pixmap) backed by a libcg surface.
 *
 * Results:
 *	Returns a Pixmap (Drawable) identifier, or None on failure.
 *
 * Side effects:
 *	Allocates a TkWaylandPixmapImpl, a cg_surface_t, and a cg_ctx_t.
 *
 *----------------------------------------------------------------------
 */

Pixmap
Tk_GetPixmap(
    TCL_UNUSED(Display *),
    Drawable d,
    int width,
    int height,
    TCL_UNUSED(int))
{
    TkWaylandPixmapImpl *pixmap;
    WindowMapping *mapping = NULL;

    if (width <= 0 || height <= 0) {
        return None;
    }

    /* Try direct mapping lookup first. */
    mapping = FindMappingByDrawable(d);

    /*
     * If that failed, the drawable may be a TkWindow* passed before
     * winPtr->window was assigned. Validate via display pointer.
     */
    if (!mapping) {
        TkWindow *candidate = (TkWindow *)d;
        Display  *ourDisp   = TkGetDisplayList()->display;
        if (candidate != NULL && candidate->display == ourDisp) {
            /* Walk up to toplevel to find the mapping. */
            TkWindow *top = candidate;
            while (top && !Tk_IsTopLevel((Tk_Window)top)) {
                top = top->parentPtr;
            }
            if (top) {
                mapping = FindMappingByTk(top);
            }
        }
    }

    if (!mapping || !mapping->glfwWindow) {
        return None;
    }

    pixmap = (TkWaylandPixmapImpl *)ckalloc(sizeof(TkWaylandPixmapImpl));
    memset(pixmap, 0, sizeof(TkWaylandPixmapImpl));

    pixmap->magic         = TK_WAYLAND_PIXMAP_MAGIC;
    pixmap->type          = 1;
    pixmap->width         = width;
    pixmap->height        = height;
    pixmap->drawable      = (Drawable)pixmap;
    pixmap->windowMapping = mapping;
    pixmap->frameOpen     = 0;

    pixmap->surface = cg_surface_create(width, height);
    if (!pixmap->surface) {
        ckfree((char *)pixmap);
        return None;
    }

    pixmap->cg = cg_create(pixmap->surface);
    if (!pixmap->cg) {
        cg_surface_destroy(pixmap->surface);
        ckfree((char *)pixmap);
        return None;
    }

    /* Clear to transparent rather than white — compositing onto
     * the window surface is additive so opaque white would obscure
     * whatever is behind the pixmap. */
    cg_set_source_rgba(pixmap->cg, 0.0, 0.0, 0.0, 0.0);
    cg_set_operator(pixmap->cg, CG_OPERATOR_SRC);
    cg_rectangle(pixmap->cg, 0, 0, (double)width, (double)height);
    cg_fill(pixmap->cg);

    return pixmap->drawable;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_FreePixmap --
 *
 *	Destroy a pixmap and free its libcg resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees cg_ctx_t, cg_surface_t, and the pixmap struct.
 *
 *----------------------------------------------------------------------
 */

void
Tk_FreePixmap(
    TCL_UNUSED(Display *),
    Pixmap pixmap)
{
    TkWaylandPixmapImpl *impl = (TkWaylandPixmapImpl *)pixmap;

    if (!impl || impl->type != 1 || impl->magic != TK_WAYLAND_PIXMAP_MAGIC) {
	return;
    }

    if (impl->cg) {
	cg_destroy(impl->cg);
	impl->cg = NULL;
    }
    if (impl->surface) {
	cg_surface_destroy(impl->surface);
	impl->surface = NULL;
    }

    impl->magic = 0;
    ckfree((char *)impl);
}

/*
 *----------------------------------------------------------------------
 *
 * IsPixmap --
 *
 *	Check whether a Drawable is a libcg-backed pixmap.
 *
 * Results:
 *	1 if it is a pixmap, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
IsPixmap(Drawable drawable)
{
    TkWaylandPixmapImpl *impl;

    if (!drawable) {
        return 0;
    }

    /*
     * All integer window IDs (toplevel from ~1000, child from ~100000)
     * are below 2^20. Pointers on 64-bit systems are above 2^32.
     * Use 2^24 as the threshold — safe on all supported platforms.
     */
    if ((uintptr_t)drawable < (1u << 24)) {
        return 0;
    }

    impl = (TkWaylandPixmapImpl *)drawable;

    /*
     * Validate ALL fields before trusting the cast — a misidentified
     * TkWindow* would have garbage in magic/type.
     */
    if (impl->magic  != TK_WAYLAND_PIXMAP_MAGIC) return 0;
    if (impl->type   != 1)                        return 0;
    if (impl->width  <= 0 || impl->width  > 32767) return 0;
    if (impl->height <= 0 || impl->height > 32767) return 0;
    if (impl->surface == NULL)                    return 0;

    return 1;
}
/*
 *----------------------------------------------------------------------
 *
 * Xlib-compatible wrappers
 *
 *----------------------------------------------------------------------
 */

GC
XCreateGC(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Drawable),
    unsigned long valuemask,
    XGCValues *values)
{
    return TkWaylandCreateGC(valuemask, values);
}

int
XFreeGC(
    TCL_UNUSED(Display *),
    GC gc)
{
    TkWaylandFreeGC(gc);
    return Success;
}

int
XSetForeground(
    TCL_UNUSED(Display *),
    GC gc,
    unsigned long foreground)
{
    XGCValues v;

    v.foreground = foreground;
    return TkWaylandChangeGC(gc, GCForeground, &v) ? Success : BadGC;
}

int
XSetBackground(
    TCL_UNUSED(Display *),
    GC gc,
    unsigned long background)
{
    XGCValues v;

    v.background = background;
    return TkWaylandChangeGC(gc, GCBackground, &v) ? Success : BadGC;
}

int
XSetLineAttributes(
    TCL_UNUSED(Display *),
    GC gc,
    unsigned int line_width,
    int line_style,
    int cap_style,
    int join_style)
{
    XGCValues v;

    v.line_width = (int)line_width;
    v.line_style = line_style;
    v.cap_style = cap_style;
    v.join_style = join_style;
    return TkWaylandChangeGC(gc, GCLineWidth | GCLineStyle | GCCapStyle | GCJoinStyle,
			     &v) ? Success : BadGC;
}

int
XGetGCValues(
    TCL_UNUSED(Display *),
    GC gc,
    unsigned long valuemask,
    XGCValues *values)
{
    return TkWaylandGetGCValues(gc, valuemask, values) ? Success : BadGC;
}

int
XChangeGC(
    TCL_UNUSED(Display *),
    GC gc,
    unsigned long valuemask,
    XGCValues *values)
{
    return TkWaylandChangeGC(gc, valuemask, values) ? Success : BadGC;
}

int
XCopyGC(
    TCL_UNUSED(Display *),
    GC src,
    unsigned long valuemask,
    GC dst)
{
    return TkWaylandCopyGC(src, valuemask, dst) ? Success : BadGC;
}

Pixmap
XCreatePixmap(
    Display *display,
    Drawable parent,
    unsigned int width,
    unsigned int height,
    unsigned int depth)
{
    return Tk_GetPixmap(display, parent, (int)width, (int)height, (int)depth);
}

int
XFreePixmap(
    TCL_UNUSED(Display *),
    Pixmap pixmap)
{
    Tk_FreePixmap(NULL, pixmap);
    return Success;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
