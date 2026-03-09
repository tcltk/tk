/*
 * tkWaylandGC.c --
 *
 *	Graphics Context and Pixmap implementation for the
 *	Wayland/GLFW/NanoVG backend.
 *
 *	This file provides the central definitions of
 *	TkWaylandGCImpl and TkWaylandPixmapImpl and all TkWayland*
 *	entry points declared in tkGlfwInt.h.  The Xlib-compatible
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

#include "tkGlfwInt.h"
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xlibint.h>

extern TkGlfwContext  glfwContext;

/* -----------------------------------------------------------------------
 * Display / screen initialization.
 *
 *
 * TkpOpenDisplay is the Tk platform entry point that allocates a full
 * TkDisplay together with a TkWaylandDisplay (our Display subtype), a
 * Screen, and a Visual.  GLFW is initialized here so that the primary
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
    screen->root        = 1;
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

    /* Allocate TkDisplay once. */
    TkDisplay *dispPtr = (TkDisplay *)ckalloc(sizeof(TkDisplay));
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
    TkWaylandGCImpl *gc;

    gc = (TkWaylandGCImpl *)ckalloc(sizeof(TkWaylandGCImpl));
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
    TkWaylandGCImpl *impl = (TkWaylandGCImpl *)gc;

    if (impl == NULL || values == NULL) {
        return 0;
    }

    if (valuemask & GCForeground)  values->foreground  = impl->foreground;
    if (valuemask & GCBackground)  values->background  = impl->background;
    if (valuemask & GCLineWidth)   values->line_width  = impl->line_width;
    if (valuemask & GCLineStyle)   values->line_style  = impl->line_style;
    if (valuemask & GCCapStyle)    values->cap_style   = impl->cap_style;
    if (valuemask & GCJoinStyle)   values->join_style  = impl->join_style;
    if (valuemask & GCFillRule)    values->fill_rule   = impl->fill_rule;
    if (valuemask & GCArcMode)     values->arc_mode    = impl->arc_mode;
    if (valuemask & GCFont)        values->font        = (Font)(uintptr_t)impl->font;

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
    TkWaylandGCImpl *impl = (TkWaylandGCImpl *)gc;

    if (impl == NULL || values == NULL) {
        return 0;
    }

    if (valuemask & GCForeground)  impl->foreground = values->foreground;
    if (valuemask & GCBackground)  impl->background = values->background;
    if (valuemask & GCLineWidth)   impl->line_width = values->line_width;
    if (valuemask & GCLineStyle)   impl->line_style = values->line_style;
    if (valuemask & GCCapStyle)    impl->cap_style  = values->cap_style;
    if (valuemask & GCJoinStyle)   impl->join_style = values->join_style;
    if (valuemask & GCFillRule)    impl->fill_rule  = values->fill_rule;
    if (valuemask & GCArcMode)     impl->arc_mode   = values->arc_mode;
    if (valuemask & GCFont)        impl->font       = (void *)(uintptr_t)values->font;

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
 *----------------------------------------------------------------------
 *
 * Tk_GetPixmap --
 *
 *      Create an off-screen drawable (pixmap) using an OpenGL FBO.
 *      This allows NanoVG to render to the pixmap just like a window.
 *
 * Results:
 *      Returns a Pixmap (Drawable) identifier.
 *
 * Side effects:
 *      Allocates FBO, texture, and stencil buffer.
 *
 *----------------------------------------------------------------------
 */

Pixmap
Tk_GetPixmap(
    TCL_UNUSED(Display *),  
    Drawable d,
    int      width,
    int      height,
    TCL_UNUSED(int)) /* depth */
{
    TkWaylandPixmapImpl *pixmap;
    WindowMapping       *mapping;
    GLint                oldFBO;
    GLenum               status;
    
    
    if (width <= 0 || height <= 0) {
        return None;
    }
    
    /* Find the window mapping to get GL context. */
    mapping = FindMappingByDrawable(d);
    if (!mapping || !mapping->glfwWindow) {
        return None;
    }
    
    /* Allocate pixmap structure. */
    pixmap = (TkWaylandPixmapImpl *)ckalloc(sizeof(TkWaylandPixmapImpl));
    memset(pixmap, 0, sizeof(TkWaylandPixmapImpl));
    pixmap->magic 		  = TK_WAYLAND_PIXMAP_MAGIC; 
    pixmap->type          = 1;  /* Pixmap, not window */
    pixmap->width         = width;
    pixmap->height        = height;
    pixmap->drawable      = (Drawable)pixmap;  /* Use pointer as ID */
    pixmap->windowMapping = mapping;
    pixmap->frameOpen     = 0;
    
    /* Make GL context current for FBO creation. */
    glfwMakeContextCurrent(mapping->glfwWindow);
    
    /* Save current FBO binding */
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFBO);
    
    /* Create texture for color attachment */
    glGenTextures(1, &pixmap->texture);
    glBindTexture(GL_TEXTURE_2D, pixmap->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    /* Create stencil buffer (required by NanoVG). */
    glGenRenderbuffers(1, &pixmap->stencil);
    glBindRenderbuffer(GL_RENDERBUFFER, pixmap->stencil);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8,
                         width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    
    /* Create and configure FBO. */
    glGenFramebuffers(1, &pixmap->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, pixmap->fbo);
    
    /* Attach texture as color buffer. */
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, pixmap->texture, 0);
    
    /* Attach stencil buffer. */
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                             GL_RENDERBUFFER, pixmap->stencil);
    
    /* Check FBO completeness. */
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Tk_GetPixmap: FBO incomplete (status=0x%x)\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
        
        /* Cleanup on failure. */
        glDeleteFramebuffers(1, &pixmap->fbo);
        glDeleteTextures(1, &pixmap->texture);
        glDeleteRenderbuffers(1, &pixmap->stencil);
        ckfree((char *)pixmap);
        return None;
    }
    
    /* Clear pixmap to white. */
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    
    /* Restore previous FBO binding. */
    glBindFramebuffer(GL_FRAMEBUFFER, oldFBO);
    
    /* Register pixmap with drawable mapping system. */
    RegisterDrawableForMapping(pixmap->drawable, mapping);
    
    return pixmap->drawable;
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
Tk_FreePixmap(TCL_UNUSED(Display *),
	      Pixmap pixmap)
{
    TkWaylandPixmapImpl *impl = (TkWaylandPixmapImpl *)pixmap;
    
    if (!impl || impl->type != 1) return;
    
    /* Make context current for GL cleanup. */
    if (impl->windowMapping && impl->windowMapping->glfwWindow) {
        glfwMakeContextCurrent(impl->windowMapping->glfwWindow);
    }
    
    /* Close any open NanoVG frame on this pixmap. */
    if (impl->frameOpen) {
        nvgEndFrame(glfwContext.vg);
        impl->frameOpen = 0;
    }
    
    /* Delete OpenGL resources. */
    if (impl->fbo) {
        glDeleteFramebuffers(1, &impl->fbo);
    }
    if (impl->texture) {
        glDeleteTextures(1, &impl->texture);
    }
    if (impl->stencil) {
        glDeleteRenderbuffers(1, &impl->stencil);
    }
    
    ckfree((char *)impl);
}

/*
 *----------------------------------------------------------------------
 *
 * IsPixmap --
 *
 *      Check if a drawable is a pixmap (FBO-backed).
 *
 * Results:
 *      Returns 1 if pixmap, 0 if window or invalid.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
IsPixmap(Drawable drawable)
{
    if (!drawable) return 0;
    
    /* Window IDs are small integers - cast to uintptr_t for comparison. */
    if ((uintptr_t)drawable < 0x1000000) {
        return 0;
    }
    
    TkWaylandPixmapImpl *impl = (TkWaylandPixmapImpl *)drawable;
    
    /* Check type field and validate dimensions */
    if (impl->type == 1 &&
        impl->width > 0 && impl->width < 32768 &&
        impl->height > 0 && impl->height < 32768 &&
        impl->fbo != 0) {
        return 1;
    }
    
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * XCreateGC --
 *
 *	Xlib-compatible wrapper for TkWaylandCreateGC.
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
 *	Xlib-compatible wrapper for TkWaylandFreeGC.
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
