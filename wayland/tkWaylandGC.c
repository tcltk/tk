/*
 * tkWaylandGC.c --
 *
 *	Graphics Context and Pixmap implementation for the
 *	Wayland/GLFW/NanoVG backend.
 *
 *	This file provides the definitions of TkWaylandGC and
 *	TkWaylandPixmap and all TkWayland* entry points declared in
 *	tkWaylandInt.h.  The Xlib-compatible wrappers (XCreateGC, XFreeGC,
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
/* Debugging
#define DEBUG_CHANNEL stdout
#define DEBUG_LABEL "GC"
*/


#include "tkWaylandInt.h"
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xlibint.h>
#include <tcl.h>

#define NANOVG_GLES3 1
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

extern GLFWwindow *mainGlfwWindow;

/* Hash table for pixmap ID to pointer mapping */
static unsigned long nextPixmapId = 1;
static Tcl_HashTable pixmapTable;
static int pixmapTableInitialized = 0;

/* 
 * Display / screen initialization.
 *
 * TkpOpenDisplay is the Tk platform entry point that allocates a full
 * TkDisplay, aScreen, and a Visual.  GLFW is initialized here so that the primary
 * monitor dimensions can be queried immediately.
 */

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
    _XPrivDisplay display = (_XPrivDisplay)Tcl_Alloc(sizeof(Display));
    if (!display) return NULL;
    bzero(display, sizeof(Display));

    /* Allocate Screen. */
    Screen *screen = (Screen *)Tcl_Alloc(sizeof(Screen));
    if (!screen) {
        Tcl_Free(display);
        return NULL;
    }

    /* Allocate Visual. */
    Visual *visual = (Visual *)Tcl_Alloc(sizeof(Visual));
    if (!visual) {
        Tcl_Free(screen);
        Tcl_Free(display);
        return NULL;
    }
    bzero(visual, sizeof(Visual));

    /* Initialize GLFW (Wayland support). */
    if (!glfwInit()) {
        Tcl_Free(visual);
        Tcl_Free(screen);
        Tcl_Free(display);
        return NULL;
    }

    /* Fill screen dimensions. */
    /* Plausible defaults for screen size in logical pixels and mm. */
    int width_px = 1920, height_px = 1080;
    int width_mm = 25.4 * width_px / 96.0;
    int height_mm = 25.4 * height_px / 96.0;
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    if (monitor) {
	float scale;
	glfwGetMonitorContentScale(monitor, &scale, NULL);
	glfwGetMonitorPhysicalSize(monitor, &width_mm, &height_mm);
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        if (mode) {
	    width_px = (int) (float) mode->width / scale;
	    height_px = (int) (float) mode->height / scale;
	}
    }
    screen->width  = width_px;
    screen->height = height_px;
    screen->mwidth  = width_mm;
    screen->mheight = height_mm;

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
    dispPtr = (TkDisplay *)Tcl_Alloc(sizeof(TkDisplay));
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

/*
 *----------------------------------------------------------------------
 *
 * XOpenDisplay --
 *
 *	Connect to X server and build internal Display.  Emulated in Wayland port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Display *
XOpenDisplay(TCL_UNUSED(const char *)) /* display_name */
{
    static Display d = {0};  /* Zero-init ensures d.screens == NULL */
    d.display_name = (char *)"wayland-0";
    return &d;
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

    gc = (TkWaylandGC *)Tcl_Alloc(sizeof(TkWaylandGC));
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
        Tcl_Free(gc);
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

MODULE_SCOPE bool
TkWaylandGetGCValues(
    GC             gc,
    unsigned long  valuemask,
    XGCValues     *values)
{
    TkWaylandGC *gcPtr = (TkWaylandGC*)gc;

    if (gcPtr == NULL || values == NULL) {
        return false;
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

    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandChangeGC --
 *
 *	Write fields into a GC from an XGCValues struct.
 *
 * Results:
 *	true on success, false if gc or values is NULL.
 *
 * Side effects:
 *	Updates GC fields in place.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE bool
TkWaylandChangeGC(
    GC             gc,
    unsigned long  valuemask,
    XGCValues     *values)
{
    TkWaylandGC *gcPtr = (TkWaylandGC*)gc;

    if (gcPtr == NULL || values == NULL) {
        return false;
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

    return true;
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

MODULE_SCOPE bool
TkWaylandCopyGC(
    GC            src,
    unsigned long valuemask,
    GC            dst)
{
    XGCValues tmp;

    if (src == NULL || dst == NULL) {
        return false;
    }

    /* Read from src, write to dst via the canonical helpers. */
    TkWaylandGetGCValues(src, valuemask, &tmp);
    TkWaylandChangeGC(dst, valuemask, &tmp);

    return true;
}

/* Pixmap functions. */

/*
 * The Pixmap XID is the unsgined int value of a pointer to a
 * TkWaylandPixmap.
 */

TkWaylandPixmap* TkWaylandPixmapFromPixmap(
    Pixmap pixmap)
{
    return (TkWaylandPixmap*)(pixmap & ~3UL);
}

/*
 * A Pixmap is a Drawable, so it must carry the low-bit tag that
 * TkWaylandDrawableIsPixmap tests; see the XID scheme in tkWaylandWm.c.
 */

static inline Pixmap PixmapFromTkWaylandPixmap(
    TkWaylandPixmap *pixmapPtr)
{
    return pixmapPtr ? 3 + (Pixmap)pixmapPtr : None;
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
    GLint            prevFbo;

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
    if (!infoPtr) {
        /*
         * glfwWindow exists (it may be the bootstrap mainGlfwWindow from
         * TkWaylandInitialize) but its glfwTkInfo hasn't been attached
         * yet -- that only happens once TkWaylandCreateWindow() runs for
         * the corresponding Tk window. Tk_GetPixmap() can be reached
         * this early (e.g. a button's default bitmap during
         * ButtonCreate/ConfigureButton), so bail out the same way the
         * missing-glfwWindow case above does rather than dereferencing
         * infoPtr->vg on a NULL pointer.
         */
        printf("No GLFW window info (not yet initialized)!\n");
        return None;
    }
    pixmapPtr = ckalloc(sizeof(TkWaylandPixmap));
    memset(pixmapPtr, 0, sizeof(TkWaylandPixmap));
    pixmapPtr->glfwWindow = glfwWindow;
    pixmapPtr->width = width;
    pixmapPtr->height = height;
    pixmapPtr->depth = 24;  /* Default depth */

    /*
     * nvgluCreateFramebuffer leaves the newly created FBO bound as the
     * active GL_FRAMEBUFFER. Tk_GetPixmap can be called well before any
     * window is actually redrawn (e.g. `image create bitmap` at script
     * load time), so nothing else is guaranteed to rebind the real
     * on-screen framebuffer afterward. Without saving/restoring here,
     * whatever draws next -- for any window -- can silently land in
     * this throwaway off-screen FBO instead of on screen.
     */
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    /* The GL context must be current when creating the FBO. */
    glfwMakeContextCurrent(glfwWindow);
    DEBUG_LOG("Tk_GetPixmap: Creating a framebuffer.");
    pixmapPtr->fb = nvgluCreateFramebuffer(infoPtr->vg,
					 width, height, 0);

    /* Check FBO completeness. */
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Tk_GetPixmap: FBO incomplete (status=0x%x)\n", status);
    }

    /* Restore whatever framebuffer was bound before this call. */
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);

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
    TkWaylandPixmap *pixmapPtr;

    if (pixmap == None || pixmap == 0) {
        return;
    }

    pixmapPtr = TkWaylandPixmapFromPixmap(pixmap);
    if (!pixmapPtr) {
        return;
    }

    /* Already freed. */
    if (pixmapPtr->fb == NULL && pixmapPtr->bitmapData == NULL) {
        return;
    }

    if (pixmapPtr->fb) {
        GLFWwindow *win = pixmapPtr->glfwWindow
                          ? pixmapPtr->glfwWindow
                          : mainGlfwWindow;

        /*
         * Only attempt NanoVG / GL cleanup when we still have a live
         * window *and* its NanoVG context is the one that owns the FBO.
         * Otherwise just free the C structures; the GL objects are
         * already gone (or will be destroyed with the context).
         */
        int canDelete = 0;
        if (win && !glfwWindowShouldClose(win)) {
            glfwMakeContextCurrent(win);
            glfwTkInfo *info = glfwGetWindowUserPointer(win);
            if (info && info->vg &&
                pixmapPtr->fb->ctx == info->vg) {
                canDelete = 1;
            }
        }

        if (canDelete) {
            nvgluDeleteFramebuffer(pixmapPtr->fb);
        } else {
            /* Context is gone or mismatched – free only the wrapper. */
            if (pixmapPtr->fb->fbo) {
                /* May already be invalid; ignore GL errors. */
                glDeleteFramebuffers(1, &pixmapPtr->fb->fbo);
            }
            if (pixmapPtr->fb->rbo) {
                glDeleteRenderbuffers(1, &pixmapPtr->fb->rbo);
            }
            free(pixmapPtr->fb);
        }
        pixmapPtr->fb = NULL;
    }

    if (pixmapPtr->bitmapData) {
        ckfree(pixmapPtr->bitmapData);
        pixmapPtr->bitmapData = NULL;
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
    return TkWaylandGetGCValues(gc, valuemask, values) ? 1 : 0;
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
 * XGetVisualInfo --
 *
 *	Returns information about available visuals matching a template mask.
 *	Dynamically maps structure definitions to survive strict mask filtering
 *	from core Tk image layout pipelines.
 *
 * Results:
 *	An allocated array of XVisualInfo structures, or NULL on failure.
 *
 * Side effects:
 *	Allocates memory that must be freed using XFree().
 *
 *----------------------------------------------------------------------
 */

XVisualInfo *
XGetVisualInfo(
    Display *display,
    long vinfo_mask,
    XVisualInfo *vinfo_template,
    int *nitems_return)
{
    static Visual *cachedVisual = NULL;
    XVisualInfo *heapInfo;

    if (nitems_return == NULL) {
        return NULL;
    }

    /* Allocate the shared underlying Visual instance once. */
    if (cachedVisual == NULL) {
        cachedVisual = (Visual *)Tcl_Alloc(sizeof(Visual));
        if (cachedVisual != NULL) {
            memset(cachedVisual, 0, sizeof(Visual));
            cachedVisual->visualid     = 1;
            cachedVisual->class        = TrueColor;
            cachedVisual->bits_per_rgb = 8;
            cachedVisual->map_entries  = 256;
            cachedVisual->red_mask     = 0x00FF0000;
            cachedVisual->green_mask   = 0x0000FF00;
            cachedVisual->blue_mask    = 0x000000FF;
        }
    }

    /* Dynamically allocate the XVisualInfo wrapper container. */
    heapInfo = (XVisualInfo *)Tcl_Alloc(sizeof(XVisualInfo));
    if (heapInfo == NULL) {
        *nitems_return = 0;
        return NULL;
    }

    /* Populate defaults matching baseline initialization values. */
    memset(heapInfo, 0, sizeof(XVisualInfo));
    heapInfo->visual        = cachedVisual;
    heapInfo->visualid      = (vinfo_template && (vinfo_mask & VisualIDMask)) ? vinfo_template->visualid : 1;
    heapInfo->screen        = (display && display->screens) ? display->default_screen : 0;
    heapInfo->depth         = (vinfo_template && (vinfo_mask & VisualDepthMask)) ? vinfo_template->depth : 24;
    heapInfo->class         = TrueColor;
    heapInfo->red_mask      = 0x00FF0000;
    heapInfo->green_mask    = 0x0000FF00;
    heapInfo->blue_mask     = 0x000000FF;
    heapInfo->colormap_size = 256;
    heapInfo->bits_per_rgb  = 8;

    /* Handle criteria filters safely by mirroring incoming requirements. */
    if (vinfo_mask != 0 && vinfo_template != NULL) {
        if ((vinfo_mask & VisualClassMask) &&
            vinfo_template->class != heapInfo->class) {
            goto match_failed;
        }

        /* If Tk requests a specific visual ID, dynamically mirror it into
         * our response structure to pass the filtering check smoothly.
         */
        if (vinfo_mask & VisualIDMask) {
            heapInfo->visualid = vinfo_template->visualid;
            if (heapInfo->visual) {
                heapInfo->visual->visualid = vinfo_template->visualid;
            }
        }

        /* Accept standard 24-bit RGB or composited 32-bit RGBA depths. */
        if (vinfo_mask & VisualDepthMask) {
            if (vinfo_template->depth != 24 && vinfo_template->depth != 32) {
                goto match_failed;
            }
            heapInfo->depth = vinfo_template->depth;
        }
    }

    *nitems_return = 1;
    return heapInfo;

match_failed:
    Tcl_Free(heapInfo);
    *nitems_return = 0;
    return NULL;
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
 *----------------------------------------------------------------------
 *
 * XGetGeometry --
 *
 *	Xlib-compatible wrapper to get geometry information about a drawable.
 *	Since this is a Wayland backend, we only support pixmaps; windows
 *	return their current size from the GLFW window.
 *
 * Results:
 *	Success on success, BadDrawable on failure.
 *
 * Side effects:
 *	Fills in the provided geometry parameters.
 *
 *----------------------------------------------------------------------
 */

int
XGetGeometry(
    TCL_UNUSED(Display *),
    Drawable drawable,
    Window   *root_return,
    int      *x_return,
    int      *y_return,
    unsigned int *width_return,
    unsigned int *height_return,
    unsigned int *border_width_return,
    unsigned int *depth_return)
{
    GLFWwindow *glfwWindow;
    int width, height;

    /* Check for invalid parameters. */
    if (!drawable) {
        return BadDrawable;
    }

    /* For pixmaps, get geometry from the pixmap structure. */
    if (TkWaylandDrawableIsPixmap(drawable)) {
        TkWaylandPixmap *pixmapPtr = TkWaylandPixmapFromPixmap(drawable);
        if (!pixmapPtr) {
            return BadDrawable;
        }
        
        if (root_return) *root_return = (Window)0;
        if (x_return) *x_return = 0;
        if (y_return) *y_return = 0;
        if (width_return) *width_return = pixmapPtr->width;
        if (height_return) *height_return = pixmapPtr->height;
        if (border_width_return) *border_width_return = 0;
        if (depth_return) *depth_return = pixmapPtr->depth;
        
        return Success;
    }
    
    /* For windows, get geometry from the GLFW window. */
    glfwWindow = TkWaylandGetGLFWwindowFromDrawable(drawable);
    if (!glfwWindow) {
        return BadDrawable;
    }
    
    glfwGetWindowSize(glfwWindow, &width, &height);
    
    if (root_return) *root_return = (Window)0;
    if (x_return) *x_return = 0;
    if (y_return) *y_return = 0;
    if (width_return) *width_return = (unsigned int)width;
    if (height_return) *height_return = (unsigned int)height;
    if (border_width_return) *border_width_return = 0;
    if (depth_return) *depth_return = 24; /* Always 24-bit for Wayland */
    
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSetBitmapData --
 *
 *      Store bitmap data in a pixmap for later use by XCopyPlane and XGetImage.
 *      This function is called by XCreateBitmapFromData to associate the
 *      1-bit bitmap data with the pixmap.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Allocates and copies bitmap data into the pixmap structure.
 *      The data is stored in the standard X bitmap format (LSB-first,
 *      packed bits, with each row padded to a byte boundary).
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandSetBitmapData(
    Pixmap pixmap,
    const char *data,
    int width,
    int height)
{
    TkWaylandPixmap *pixmapPtr = TkWaylandPixmapFromPixmap(pixmap);
    if (!pixmapPtr || !data || width <= 0 || height <= 0) {
        return;
    }

    /* Free any existing bitmap data. */
    if (pixmapPtr->bitmapData) {
        ckfree(pixmapPtr->bitmapData);
        pixmapPtr->bitmapData = NULL;
    }

    /* Calculate bytes per line (packed bits, padded to byte boundary). */
    int bytesPerLine = (width + 7) / 8;
    size_t dataSize = (size_t)bytesPerLine * height;
    
    /* Allocate and copy the bitmap data. */
    pixmapPtr->bitmapData = (unsigned char *)ckalloc(dataSize);
    if (!pixmapPtr->bitmapData) {
        return;
    }
    memcpy(pixmapPtr->bitmapData, data, dataSize);
    pixmapPtr->bitmapBytesPerLine = bytesPerLine;
    pixmapPtr->isBitmap = 1;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
