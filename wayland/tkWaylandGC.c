/*
 * tkWaylandGC.c --
 *
 *	Graphics Context and Pixmap implementation for the
 *	Wayland/GLFW/NanoVG backend.
 *
 *	This file provides the definitions of TkWaylandGC and
 *	TkWaylandPixmap and all TkWayland* entry points declared in
 *	tkGlfwInt.h.  The Xlib-compatible wrappers (XCreateGC, XFreeGC,
 *	XCreatePixmap, etc.) forward to these entry points and live here as
 *	well.
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

#include "tkGlfwInt.h"
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xlibint.h>

extern GLFWwindow *mainGlfwWindow;

/* -----------------------------------------------------------------------
 * Display / screen initialization.
 *
 *
 * TkpOpenDisplay is the Tk platform entry point that allocates a full
 * TkDisplay, aScreen, and a Visual.  GLFW is initialized here so that the primary
 * monitor dimensions can be queried immediately.

 * ----------------------------------------------------------------------- */

/*
 *----------------------------------------------------------------------
 *
 * TkpOpenDisplay --
 *
 *  Tk platform entry point: allocate a TkDisplay for Wayland/GLFW.
 *  Sets up Screen, Visual, and ensures mwidth/mheight are valid.
 *
 * Results:
 *  Pointer to newly allocated TkDisplay, or NULL on failure.
 *
 * Side effects:
 *  Allocates memory for Display, Screen, Visual, TkDisplay.
 *  Calls glfwInit().
 *
 *----------------------------------------------------------------------
 */
TkDisplay *
TkpOpenDisplay(TCL_UNUSED(const char *)) /* displayName */
{
    /*
     * Singleton: Tk_Display(tkwin) must return the same Display* for every
     * window so that the cursor hash-table comparison in tkCursor.c:
     *
     *   Tk_Display(tkwin) == cursorPtr->display
     *
     * is always true for windows on this display.  Without this guard,
     * multiple calls to TkpOpenDisplay (e.g. from multiple interpreters)
     * each allocate a fresh Display*, making the comparison fail and causing
     * tkCursor.c to walk a stale hash-chain pointer — segfault.
     */
    static TkDisplay *dispPtr = NULL;
    if (dispPtr != NULL) {
        return dispPtr;
    }

    /* Allocate Display. */
    _XPrivDisplay display = (_XPrivDisplay)ckalloc(sizeof(Display));
    if (!display) return NULL;
    bzero(display, sizeof(Display));

    /* Allocate Screen. */
    Screen *screen = (Screen *)ckalloc(sizeof(Screen));
    if (!screen) {
        ckfree(display);
        return NULL;
    }

    /* Allocate Visual. */
    Visual *visual = (Visual *)ckalloc(sizeof(Visual));
    if (!visual) {
        ckfree(screen);
        ckfree(display);
        return NULL;
    }
    bzero(visual, sizeof(Visual));

    /* Initialize GLFW (Wayland support). */
    if (!glfwInit()) {
        ckfree(visual);
        ckfree(screen);
        ckfree(display);
        return NULL;
    }

    /* Fill screen dimensions. */
    int sw = 1920, sh = 1080;
    GLFWmonitor *mon = glfwGetPrimaryMonitor();
    if (mon) {
        const GLFWvidmode *mode = glfwGetVideoMode(mon);
        if (mode) { sw = mode->width; sh = mode->height; }
    }
    screen->width  = sw;
    screen->height = sh;
    screen->mwidth  = (sw * 254.0) / 720.0;
    screen->mheight = (sh * 254.0) / 720.0;

    /* Display. */
    display->screens        = screen;
    display->nscreens       = 1;
    display->default_screen = 0;
    display->display_name   = (char *)"wayland-0";

    screen->display     = (Display *)display;
    /*
     * This is passed as a drawable to Tk_GetPixmap by photoimages!
     * So it cannot be odd!!! Zero means use the mainGlfwWindow.
     * XXXX This is an issue if we want to support high-dpi pixmaps.
     */
    screen->root        = 0;
    screen->root_visual = visual;
    screen->root_depth  = 24;

    /* Visual. */
    visual->visualid     = 1;
    visual->class        = TrueColor;
    visual->bits_per_rgb = 8;
    visual->map_entries  = 256;
    visual->red_mask     = 0xFF0000;
    visual->green_mask   = 0x00FF00;
    visual->blue_mask    = 0x0000FF;

    /* Allocate TkDisplay. */
    dispPtr = (TkDisplay *)ckalloc(sizeof(TkDisplay));
    bzero(dispPtr, sizeof(TkDisplay));
    dispPtr->display = (Display *)display;
    /*
     * dispPtr->name must be set: tkBind.c passes it to ChangeScreen as
     * the display name component of "::tk::ScreenChanged <name>.<screen>".
     * A NULL name causes Tcl_ObjPrintf to format "(null).0", corrupting
     * the interp result and crashing Tcl_RestoreInterpState.
     */
    dispPtr->name = (char *)"wayland-0";

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
 *	Frees all memory allocated by TkpOpenDisplay.  Resets
 *	tkWaylandDispPtr to NULL.
 *
 *----------------------------------------------------------------------
 */

void
TkpCloseDisplay(TCL_UNUSED(TkDisplay*)) /* dispPtr */
{
	/* no-op */
}

/* Graphics context functions. */

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCreateGC --
 *
 *	Allocate a new GC, optionally initialising fields from values/mask.
 *
 * Results:
 *	A freshly allocated GC cast to the opaque GC type, or NULL.
 *
 * Side effects:
 *	Allocates heap memory.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GC
TkWaylandCreateGC(
    unsigned long  valuemask,
    XGCValues     *values)
{
    TkWaylandGC *gc;

    gc = ckalloc(sizeof(TkWaylandGC));
    if (gc == NULL) {
        return NULL;
    }

    /* Apply defaults. */
    gc->foreground = 0x000000;  /* Black   */
    gc->background = 0xFFFFFF;  /* White   */
    gc->line_width = 1;
    gc->line_style = LineSolid;
    gc->cap_style  = CapButt;
    gc->join_style = JoinMiter;
    gc->fill_rule  = WindingRule;
    gc->arc_mode   = ArcPieSlice;
    gc->font       = NULL;

    /* Override with caller-supplied values. */
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
TkWaylandFreeGC(
    GC gc)
{
    if (gc != NULL) {
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
    GC             gc,
    unsigned long  valuemask,
    XGCValues     *values)
{
    TkWaylandGC *gcPtr = (TkWaylandGC*)gc;

    if (gcPtr == NULL || values == NULL) {
        return 0;
    }

    if (valuemask & GCForeground)  values->foreground  = gcPtr->foreground;
    if (valuemask & GCBackground)  values->background  = gcPtr->background;
    if (valuemask & GCLineWidth)   values->line_width  = gcPtr->line_width;
    if (valuemask & GCLineStyle)   values->line_style  = gcPtr->line_style;
    if (valuemask & GCCapStyle)    values->cap_style   = gcPtr->cap_style;
    if (valuemask & GCJoinStyle)   values->join_style  = gcPtr->join_style;
    if (valuemask & GCFillRule)    values->fill_rule   = gcPtr->fill_rule;
    if (valuemask & GCArcMode)     values->arc_mode    = gcPtr->arc_mode;
    if (valuemask & GCFont)        values->font        = (Font)(uintptr_t)gcPtr->font;

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
    GC             gc,
    unsigned long  valuemask,
    XGCValues     *values)
{
    TkWaylandGC *gcPtr = (TkWaylandGC*)gc;

    if (gcPtr == NULL || values == NULL) {
        return 0;
    }

    if (valuemask & GCForeground)  gcPtr->foreground = values->foreground;
    if (valuemask & GCBackground)  gcPtr->background = values->background;
    if (valuemask & GCLineWidth)   gcPtr->line_width = values->line_width;
    if (valuemask & GCLineStyle)   gcPtr->line_style = values->line_style;
    if (valuemask & GCCapStyle)    gcPtr->cap_style  = values->cap_style;
    if (valuemask & GCJoinStyle)   gcPtr->join_style = values->join_style;
    if (valuemask & GCFillRule)    gcPtr->fill_rule  = values->fill_rule;
    if (valuemask & GCArcMode)     gcPtr->arc_mode   = values->arc_mode;
    if (valuemask & GCFont)        gcPtr->font       = (void *)(uintptr_t)values->font;

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
    GC            src,
    unsigned long valuemask,
    GC            dst)
{
    XGCValues tmp;

    if (src == NULL || dst == NULL) {
        return 0;
    }

    /* Read from src, write to dst via the canonical helpers. */
    TkWaylandGetGCValues(src, valuemask, &tmp);
    TkWaylandChangeGC(dst, valuemask, &tmp);

    return 1;
}

/* Pixmap functions. */

/*
 * The Pixmap XID is the unsgined int value of a pointer to a
 * TkWaylandPixmap.
 */

static inline TkWaylandPixmap* TkWaylandPixmapFromPixmap(
    Pixmap pixmap)
{
    return (TkWaylandPixmap*)pixmap;
}

static inline Pixmap PixmapFromTkWaylandPixmap(
    TkWaylandPixmap *pixmapPtr)
{
    return (Pixmap)pixmapPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetPixmap --
 *
 *      Create an off-screen drawable (pixmap), which is associated with an
 *      NVGLUframebuffer.  The drawable should be a Tk window or None.  If a
 *      drawable is provided, the FBO is created in the GL context of the
 *      associated GLFWwindow.  Otherwise the context of the root window is
 *      used.  Note that the GL context is shared between windows.
 *
 * Results:
 *      Returns a Drawable associated to a Pixmap.
 *
 * Side effects:
 *      Allocates an NVGLUframebuffer.
 *
 *----------------------------------------------------------------------
 */

Pixmap
Tk_GetPixmap(
    TCL_UNUSED(Display *),
    Drawable drawable,
    int      width,
    int      height,
    TCL_UNUSED(int)) /* depth */
{
    TkWaylandPixmap *pixmapPtr;
    GLenum           status;
    GLFWwindow      *glfwWindow;

    if (width <= 0 || height <= 0) {
        return None;
    }

    if (drawable && TkWaylandDrawableIsPixmap(drawable)) {
	glfwWindow = TkWaylandGetGLFWwindowFromDrawable(drawable);
    } else {
	glfwWindow = mainGlfwWindow;
    }
    if (!glfwWindow) {
	printf("No GLFW window!\n");
	return None;
    }

    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(glfwWindow);
    pixmapPtr = ckalloc(sizeof(TkWaylandPixmap));
    memset(pixmapPtr, 0, sizeof(TkWaylandPixmap));
    pixmapPtr->glfwWindow = glfwWindow;
    pixmapPtr->width = width;
    pixmapPtr->height = height;
    //// Figure out how to specify high-dpi pixmaps!!!!
    //// Maybe use the pixel ratio of the drawable?
    //// That could change if the drawable moves to a different display.
    pixmapPtr->pixelRatio = 1.0;

    /* The GL context must be current when creating the FBO. */
    glfwMakeContextCurrent(glfwWindow);
    int fbWidth = (int) pixmapPtr->pixelRatio * width;
    int fbHeight = (int) pixmapPtr->pixelRatio * height;
    pixmapPtr->fb = nvgluCreateFramebuffer(infoPtr->context.vg,
					 fbWidth, fbHeight, 0);

    /* Check FBO completeness. */
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Tk_GetPixmap: FBO incomplete (status=0x%x)\n", status);
    }

    /* Clear pixmap to white. */
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    return PixmapFromTkWaylandPixmap(pixmapPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_FreePixmap --
 *
 *      Destroy a pixmap and free its OpenGL resources.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Deletes FBO, texture, and stencil buffer.
 *
 *----------------------------------------------------------------------
 */

void
Tk_FreePixmap(
    TCL_UNUSED(Display *),
    Pixmap pixmap)
{
    TkWaylandPixmap *pixmapPtr = TkWaylandPixmapFromPixmap(pixmap);
    if (pixmapPtr->fb) {
	glfwMakeContextCurrent(pixmapPtr->glfwWindow);
	nvgluDeleteFramebuffer(pixmapPtr->fb);
    }
    ckfree(pixmapPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateGC --
 *
 *	Stub function called by Tk_GetGC. This calls TkWaylandCreateGC.
 *
 * Results:
 *	A newly created Graphics Context.
 *
 * Side effects:
 *	Allocates memory for a new GC via TkWaylandCreateGC.
 *
 *----------------------------------------------------------------------
 */

GC
XCreateGC(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Drawable),
    unsigned long  valuemask,
    XGCValues     *values)
{
    return TkWaylandCreateGC(valuemask, values);
}

/*
 *----------------------------------------------------------------------
 *
 * XFreeGC --
 *
 *	Stub function called by Tk_FreeGC. Calls TkWaylandFreeGC.
 *
 * Results:
 *	Always returns Success.
 *
 * Side effects:
 *	Frees memory associated with the GC via TkWaylandFreeGC.
 *
 *----------------------------------------------------------------------
 */

int
XFreeGC(
    TCL_UNUSED(Display *),
    GC gc)
{
    TkWaylandFreeGC(gc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetForeground --
 *
 *	Xlib-compatible wrapper to set the foreground color in a GC.
 *
 * Results:
 *	Success on success, BadGC on failure.
 *
 * Side effects:
 *	Updates the GC's foreground value via TkWaylandChangeGC.
 *
 *----------------------------------------------------------------------
 */

int
XSetForeground(
    TCL_UNUSED(Display *),
    GC            gc,
    unsigned long foreground)
{
    XGCValues v;
    v.foreground = foreground;
    return TkWaylandChangeGC(gc, GCForeground, &v) ? Success : BadGC;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetBackground --
 *
 *	Xlib-compatible wrapper to set the background color in a GC.
 *
 * Results:
 *	Success on success, BadGC on failure.
 *
 * Side effects:
 *	Updates the GC's background value via TkWaylandChangeGC.
 *
 *----------------------------------------------------------------------
 */

int
XSetBackground(
    TCL_UNUSED(Display *),
    GC            gc,
    unsigned long background)
{
    XGCValues v;
    v.background = background;
    return TkWaylandChangeGC(gc, GCBackground, &v) ? Success : BadGC;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetLineAttributes --
 *
 *	Xlib-compatible wrapper to set line drawing attributes in a GC.
 *
 * Results:
 *	Success on success, BadGC on failure.
 *
 * Side effects:
 *	Updates the GC's line attributes via TkWaylandChangeGC.
 *
 *----------------------------------------------------------------------
 */

int
XSetLineAttributes(
    TCL_UNUSED(Display *),
    GC           gc,
    unsigned int line_width,
    int          line_style,
    int          cap_style,
    int          join_style)
{
    XGCValues v;
    v.line_width = (int)line_width;
    v.line_style = line_style;
    v.cap_style  = cap_style;
    v.join_style = join_style;
    return TkWaylandChangeGC(
        gc,
        GCLineWidth | GCLineStyle | GCCapStyle | GCJoinStyle,
        &v) ? Success : BadGC;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetGCValues --
 *
 *	Xlib-compatible wrapper for TkWaylandGetGCValues.
 *
 * Results:
 *	1 on success, 0 on failure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XGetGCValues(
    TCL_UNUSED(Display *),
    GC             gc,
    unsigned long  valuemask,
    XGCValues     *values)
{
    return TkWaylandGetGCValues(gc, valuemask, values);
}

/*
 *----------------------------------------------------------------------
 *
 * XChangeGC --
 *
 *	Xlib-compatible wrapper for TkWaylandChangeGC.
 *
 * Results:
 *	1 on success, 0 on failure.
 *
 * Side effects:
 *	Updates GC fields via TkWaylandChangeGC.
 *
 *----------------------------------------------------------------------
 */

int
XChangeGC(
    TCL_UNUSED(Display *),
    GC             gc,
    unsigned long  valuemask,
    XGCValues     *values)
{
    return TkWaylandChangeGC(gc, valuemask, values);
}

/*
 *----------------------------------------------------------------------
 *
 * XCopyGC --
 *
 *	Xlib-compatible wrapper for TkWaylandCopyGC.
 *
 * Results:
 *	Success on success, BadGC on failure.
 *
 * Side effects:
 *	Copies GC attributes from source to destination via TkWaylandCopyGC.
 *
 *----------------------------------------------------------------------
 */

int
XCopyGC(
    TCL_UNUSED(Display *),
    GC            src,
    unsigned long valuemask,
    GC            dst)
{
    return TkWaylandCopyGC(src, valuemask, dst) ? Success : BadGC;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreatePixmap --
 *
 *	Xlib-compatible wrapper for Tk_GetPixmap.
 *
 * Results:
 *	A newly created Pixmap handle.
 *
 * Side effects:
 *	Allocates pixmap resources via Tk_GetPixmap.
 *
 *----------------------------------------------------------------------
 */

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

/*
 *----------------------------------------------------------------------
 *
 * XFreePixmap --
 *
 *	Xlib-compatible wrapper for Tk_FreePixmap.
 *
 * Results:
 *	Always returns Success.
 *
 * Side effects:
 *	Frees pixmap resources via Tk_FreePixmap.
 *
 *----------------------------------------------------------------------
 */

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
