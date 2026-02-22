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
 *	entry points) and thin Xlib wrappers (XOpenDisplay, XCloseDisplay,
 *	XDefaultScreen, XScreenCount, XScreenOfDisplay) so that Tk can
 *	resolve screen information at startup.
 *
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkGlfwInt.h"
#include <stdlib.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Display / screen initialisation.
 *
 * Tk interrogates the Display * returned by XOpenDisplay for screen count
 * and geometry before any window is created.  If those fields are zero or
 * the pointer is NULL, Tk rejects screen "0" and aborts with:
 *
 *   application-specific initialization failed: bad screen number "0"
 *
 * TkpOpenDisplay is the Tk platform entry point that allocates a full
 * TkDisplay together with a TkWaylandDisplay (our Display subtype), a
 * Screen, and a Visual.  GLFW is initialized here so that the primary
 * monitor dimensions can be queried immediately.
 *
 * XOpenDisplay delegates to TkpOpenDisplay; XCloseDisplay delegates to
 * TkpCloseDisplay.  A module-level pointer (tkWaylandDispPtr) lets the
 * thin Xlib wrappers find the live TkDisplay without a separate lookup.
 * ----------------------------------------------------------------------- */

static TkDisplay *tkWaylandDispPtr = NULL;  /* Set by TkpOpenDisplay. */

/*
 *----------------------------------------------------------------------
 *
 * TkpOpenDisplay --
 *
 *	Tk platform entry point: allocate a TkDisplay and synthesise an
 *	X Display structure backed by a TkWaylandDisplay.  GLFW is
 *	initialized here so that primary-monitor dimensions are available
 *	immediately for the Screen struct.
 *
 * Results:
 *	Pointer to a newly allocated TkDisplay, or NULL on failure.
 *
 * Side effects:
 *	Calls glfwInit().  Allocates memory for TkDisplay,
 *	TkWaylandDisplay, Screen, and Visual.  Sets tkWaylandDispPtr.
 *
 *----------------------------------------------------------------------
 */

TkDisplay *
TkpOpenDisplay(
    TCL_UNUSED(const char *))   /* display_name */
{
    TkDisplay        *dispPtr;
    TkWaylandDisplay *display;
    Screen           *screen;
    Visual           *visual;

    if (!glfwInit()) {
        return NULL;
    }

    dispPtr = (TkDisplay *)ckalloc(sizeof(TkDisplay));
    memset(dispPtr, 0, sizeof(TkDisplay));

    display = (TkWaylandDisplay *)ckalloc(sizeof(TkWaylandDisplay));
    memset(display, 0, sizeof(TkWaylandDisplay));

    screen = (Screen *)ckalloc(sizeof(Screen));
    memset(screen, 0, sizeof(Screen));

    visual = (Visual *)ckalloc(sizeof(Visual));
    memset(visual, 0, sizeof(Visual));

    /* Screen setup – use primary monitor dimensions when available. */
    {
        int sw = 1920, sh = 1080;
        GLFWmonitor *mon = glfwGetPrimaryMonitor();
        if (mon != NULL) {
            const GLFWvidmode *mode = glfwGetVideoMode(mon);
            if (mode != NULL) { sw = mode->width; sh = mode->height; }
        }
        screen->width   = sw;
        screen->height  = sh;
        screen->mwidth  = (sw * 254) / 720;  /* approx 96 dpi */
        screen->mheight = (sh * 254) / 720;
    }

    screen->display     = (Display *)display;
    screen->root        = 1;            /* Must not be None. */
    screen->root_visual = visual;
    screen->root_depth  = 24;
    screen->ndepths     = 1;

    visual->visualid     = 1;
    visual->class        = TrueColor;
    visual->bits_per_rgb = 8;
    visual->map_entries  = 256;
    visual->red_mask     = 0xFF0000;
    visual->green_mask   = 0x00FF00;
    visual->blue_mask    = 0x0000FF;

    display->screens        = screen;
    display->nscreens       = 1;
    display->default_screen = 0;

    dispPtr->display = (Display *)display;
    dispPtr->name    = (char *)ckalloc(strlen("wayland-0") + 1);
    strcpy(dispPtr->name, "wayland-0");
    display->display_name = dispPtr->name;

    tkWaylandDispPtr = dispPtr;
    return dispPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpCloseDisplay --
 *
 *	Tk platform entry point: close and free a TkDisplay together with
 *	its associated TkWaylandDisplay, Screen, and Visual.
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
TkpCloseDisplay(
    TkDisplay *dispPtr)
{
    if (dispPtr == NULL) {
        return;
    }

    if (dispPtr->name != NULL) {
        ckfree(dispPtr->name);
    }

    if (dispPtr->display != NULL) {
        TkWaylandDisplay *wd = (TkWaylandDisplay *)dispPtr->display;
        if (wd->screens != NULL) {
            if (wd->screens->root_visual != NULL) {
                ckfree((char *)wd->screens->root_visual);
            }
            ckfree((char *)wd->screens);
        }
        ckfree((char *)wd);
    }

    ckfree((char *)dispPtr);

    if (tkWaylandDispPtr == dispPtr) {
        tkWaylandDispPtr = NULL;
    }
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

static TkWaylandPixmapImpl *pixmapStore    = NULL;
static int                   pixmapCount    = 0;
static int                   pixmapCapacity = 0;
static NVGcontext           *nvgCtx         = NULL;

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSetNVGContext --
 *
 *	Record the NanoVG context that pixmap operations should use.
 *	Must be called before creating any pixmaps.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets internal module-level pointer.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandSetNVGContext(
    NVGcontext *vg)
{
    nvgCtx = vg;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetPixmapNVGContext --
 *
 *	Return the NanoVG context registered for pixmap operations.
 *
 * Results:
 *	NVGcontext pointer, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext *
TkWaylandGetPixmapNVGContext(void)
{
    return nvgCtx;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCreatePixmap --
 *
 *	Create a pixmap of the given dimensions and depth.
 *	Type 0 (image-backed) is used when NanoVG is available and
 *	width/height > 0; otherwise type 1 (paint) is used.
 *
 * Results:
 *	An opaque Pixmap handle.
 *
 * Side effects:
 *	Allocates heap memory and (when possible) a NanoVG texture.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE Pixmap
TkWaylandCreatePixmap(
    int width,
    int height,
    int depth)
{
    TkWaylandPixmapImpl *pix;

    /* Grow the store if needed. */
    if (pixmapCount >= pixmapCapacity) {
        int newCap = (pixmapCapacity == 0) ? 16 : pixmapCapacity * 2;
        pixmapStore = (TkWaylandPixmapImpl *)ckrealloc(
            (char *)pixmapStore,
            newCap * sizeof(TkWaylandPixmapImpl));
        pixmapCapacity = newCap;
    }

    pix = &pixmapStore[pixmapCount];
    memset(pix, 0, sizeof(TkWaylandPixmapImpl));

    pix->width  = width;
    pix->height = height;
    pix->depth  = depth;

    if (nvgCtx != NULL && width > 0 && height > 0) {
        unsigned char *data = (unsigned char *)ckalloc(width * height * 4);
        if (data != NULL) {
            memset(data, 0, width * height * 4);
            pix->imageId = nvgCreateImageRGBA(
                nvgCtx, width, height, NVG_IMAGE_NEAREST, data);
            ckfree(data);
            pix->type = 0; /* image-backed */
        }
    }

    if (pix->type != 0) {
        /* Fallback: zero-size or failed allocation → transparent paint. */
        pix->paint = nvgLinearGradient(
            nvgCtx != NULL ? nvgCtx : TkGlfwGetNVGContext(),
            0, 0, 1, 1,
            nvgRGBA(0, 0, 0, 0),
            nvgRGBA(0, 0, 0, 0));
        pix->type = 1; /* paint */
    }

    pixmapCount++;
    return (Pixmap)pix;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandFreePixmap --
 *
 *	Release resources held by a pixmap.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deletes NanoVG image if present; zeroes the struct for reuse.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandFreePixmap(
    Pixmap pixmap)
{
    TkWaylandPixmapImpl *pix = (TkWaylandPixmapImpl *)pixmap;

    if (pix == NULL) {
        return;
    }

    if (pix->type == 0 && pix->imageId != 0 && nvgCtx != NULL) {
        nvgDeleteImage(nvgCtx, pix->imageId);
    }

    memset(pix, 0, sizeof(TkWaylandPixmapImpl));
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetPixmapImageId --
 *
 *	Return the NanoVG image ID for an image-backed pixmap.
 *
 * Results:
 *	NanoVG image ID, or 0 if not image-backed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandGetPixmapImageId(
    Pixmap pixmap)
{
    TkWaylandPixmapImpl *pix = (TkWaylandPixmapImpl *)pixmap;
    return (pix && pix->type == 0) ? pix->imageId : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetPixmapPaint --
 *
 *	Return a pointer to the NVGpaint for a paint-backed pixmap.
 *
 * Results:
 *	NVGpaint pointer, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGpaint *
TkWaylandGetPixmapPaint(
    Pixmap pixmap)
{
    TkWaylandPixmapImpl *pix = (TkWaylandPixmapImpl *)pixmap;
    return (pix && pix->type == 1) ? &pix->paint : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetPixmapType --
 *
 *	Return the pixmap type: 0 = image, 1 = paint, -1 = invalid.
 *
 * Results:
 *	See above.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandGetPixmapType(
    Pixmap pixmap)
{
    TkWaylandPixmapImpl *pix = (TkWaylandPixmapImpl *)pixmap;
    return pix ? pix->type : -1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetPixmapDimensions --
 *
 *	Fill in the optional out-pointers with the pixmap's dimensions.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandGetPixmapDimensions(
    Pixmap pixmap,
    int   *width,
    int   *height,
    int   *depth)
{
    TkWaylandPixmapImpl *pix = (TkWaylandPixmapImpl *)pixmap;
    if (pix == NULL) {
        return;
    }
    if (width)  *width  = pix->width;
    if (height) *height = pix->height;
    if (depth)  *depth  = pix->depth;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandUpdatePixmapImage --
 *
 *	Replace the image data for an image-backed pixmap.
 *
 * Results:
 *	1 on success, 0 on failure.
 *
 * Side effects:
 *	Deletes old NanoVG image and creates a new one.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandUpdatePixmapImage(
    Pixmap               pixmap,
    const unsigned char *data)
{
    TkWaylandPixmapImpl *pix = (TkWaylandPixmapImpl *)pixmap;

    if (pix == NULL || nvgCtx == NULL || pix->type != 0) {
        return 0;
    }

    if (pix->imageId != 0) {
        nvgDeleteImage(nvgCtx, pix->imageId);
        pix->imageId = 0;
    }

    if (data != NULL) {
        pix->imageId = nvgCreateImageRGBA(
            nvgCtx, pix->width, pix->height, NVG_IMAGE_NEAREST, data);
    }

    return (pix->imageId != 0) ? 1 : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCleanupPixmapStore --
 *
 *	Release all pixmaps and reset the store.  Call at shutdown,
 *	before TkWaylandCleanupDisplay.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all NanoVG images and the store array itself.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandCleanupPixmapStore(void)
{
    int i;

    if (nvgCtx == NULL) {
        return;
    }

    for (i = 0; i < pixmapCount; i++) {
        TkWaylandPixmapImpl *pix = &pixmapStore[i];
        if (pix->type == 0 && pix->imageId != 0) {
            nvgDeleteImage(nvgCtx, pix->imageId);
        }
    }

    if (pixmapStore != NULL) {
        ckfree((char *)pixmapStore);
        pixmapStore    = NULL;
    }
    pixmapCount    = 0;
    pixmapCapacity = 0;
    nvgCtx         = NULL;
}

/* -----------------------------------------------------------------------
 * Xlib wrapper functions.
 *
 * Drawing wrappers (XCreateGC, XFreeGC, XCreatePixmap, etc.) accept the
 * Display * for API compatibility but do not need to dereference it for
 * their own purposes — they delegate to the TkWayland* helpers above.
 *
 * Display/screen discovery wrappers (XOpenDisplay, XCloseDisplay,
 * XDefaultScreen, XScreenCount, XScreenOfDisplay) delegate to
 * TkpOpenDisplay / TkpCloseDisplay and cast through TkWaylandDisplay so
 * that Tk can resolve screen "0" at startup.
 * ----------------------------------------------------------------------- */

/*
 *----------------------------------------------------------------------
 *
 * XOpenDisplay --
 *
 *	Xlib entry point called by Tk at startup to obtain a Display.
 *	Delegates to TkpOpenDisplay so that GLFW is initialised and
 *	screen metadata is fully populated before Tk validates screen "0".
 *
 * Results:
 *	The Display * embedded in the TkDisplay returned by TkpOpenDisplay,
 *	or NULL if initialisation fails.
 *
 * Side effects:
 *	See TkpOpenDisplay.
 *
 *----------------------------------------------------------------------
 */

Display *
XOpenDisplay(
    const char *displayName)
{
    TkDisplay *dispPtr = TkpOpenDisplay(displayName);
    return (dispPtr != NULL) ? dispPtr->display : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XCloseDisplay --
 *
 *	Xlib entry point called at shutdown.  Locates the owning TkDisplay
 *	via the module-level pointer and delegates to TkpCloseDisplay.
 *
 * Results:
 *	Always 0.
 *
 * Side effects:
 *	See TkpCloseDisplay.
 *
 *----------------------------------------------------------------------
 */

int
XCloseDisplay(
    TCL_UNUSED(Display *))
{
    if (tkWaylandDispPtr != NULL) {
        TkpCloseDisplay(tkWaylandDispPtr);
        /* tkWaylandDispPtr is reset to NULL inside TkpCloseDisplay. */
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XDefaultScreen --
 *
 *	Return the index of the default screen for a Display.
 *	Casts through TkWaylandDisplay to read the field set by
 *	TkpOpenDisplay.
 *
 * Results:
 *	0 (the only screen this backend exposes), or 0 on NULL input.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XDefaultScreen(
    Display *display)
{
    TkWaylandDisplay *wd = (TkWaylandDisplay *)display;
    return (wd != NULL) ? wd->default_screen : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XScreenCount --
 *
 *	Return the number of screens available on a Display.
 *	Casts through TkWaylandDisplay to read the field set by
 *	TkpOpenDisplay.
 *
 * Results:
 *	1, or 0 if display is NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XScreenCount(
    Display *display)
{
    TkWaylandDisplay *wd = (TkWaylandDisplay *)display;
    return (wd != NULL) ? wd->nscreens : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XScreenOfDisplay --
 *
 *	Return the Screen struct for screen index scr on display.
 *	Only screen 0 is valid; returns NULL for any other index.
 *	Casts through TkWaylandDisplay to access the Screen array
 *	allocated by TkpOpenDisplay.
 *
 * Results:
 *	Pointer to the Screen, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Screen *
XScreenOfDisplay(
    Display *display,
    int      scr)
{
    TkWaylandDisplay *wd = (TkWaylandDisplay *)display;
    if (wd == NULL || scr < 0 || scr >= wd->nscreens) {
        return NULL;
    }
    return &wd->screens[scr];
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
 *	Xlib-compatible wrapper for TkWaylandCreatePixmap.
 *
 * Results:
 *	A newly created Pixmap handle.
 *
 * Side effects:
 *	Allocates pixmap resources via TkWaylandCreatePixmap.
 *
 *----------------------------------------------------------------------
 */

Pixmap
XCreatePixmap(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Drawable),
    unsigned int width,
    unsigned int height,
    unsigned int depth)
{
    return TkWaylandCreatePixmap((int)width, (int)height, (int)depth);
}

/*
 *----------------------------------------------------------------------
 *
 * XFreePixmap --
 *
 *	Xlib-compatible wrapper for TkWaylandFreePixmap.
 *
 * Results:
 *	Always returns Success.
 *
 * Side effects:
 *	Frees pixmap resources via TkWaylandFreePixmap.
 *
 *----------------------------------------------------------------------
 */

int
XFreePixmap(
    TCL_UNUSED(Display *),
    Pixmap pixmap)
{
    TkWaylandFreePixmap(pixmap);
    return Success;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
