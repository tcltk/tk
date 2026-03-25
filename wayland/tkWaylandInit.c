/*
 * tkWaylandInit.h --
 *
 *   GLFW/Wayland-specific interpreter initialization: context
 *   management, window mapping, drawing context lifecycle, color
 *   conversion, and platform init/cleanup. GLFW, libcg and libdecor
 *   provide the native platform on which Tk's widget set and event loop
 *   are deployed.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 2026  Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkWaylandInt.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

/*
 *----------------------------------------------------------------------
 *
 * Module-level state
 *
 *----------------------------------------------------------------------
 */

TkGlfwContext  glfwContext       = {NULL, NULL, 0, 0, NULL, 0, 0, NULL};
WindowMapping *windowMappingList = NULL;
static Drawable        nextDrawableId      = 1000;
static DrawableMapping *drawableMappingList = NULL;
static int shutdownInProgress = 0;

/*
 *----------------------------------------------------------------------
 *
 * Forward declarations
 *
 *----------------------------------------------------------------------
 */

extern int   TkWaylandGetGCValues(GC, unsigned long, XGCValues *);
extern void  TkWaylandMenuInit(void);
extern void  Tk_WaylandSetupTkNotifier(void);
extern int   Tktray_Init(Tcl_Interp *);
extern int   SysNotify_Init(Tcl_Interp *);
extern int   Cups_Init(Tcl_Interp *);
extern void  TkGlfwSetupCallbacks(GLFWwindow *, TkWindow *);

/*
 *----------------------------------------------------------------------
 *
 * Accessors
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkGlfwContext *
TkGlfwGetContext(void)
{
    return &glfwContext;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwErrorCallback --
 *
 *	GLFW error callback that prints errors to stderr.
 *	Silences errors during shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints error messages to stderr.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwErrorCallback(int error, const char *desc)
{
    if (shutdownInProgress) return;

    if (glfwContext.initialized && glfwContext.mainWindow) {
        fprintf(stderr, "GLFW Error %d: %s\n", error, desc);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwInitialize --
 *
 *	Initialize the GLFW library and create a shared context window.
 *	libcg contexts are surface-bound and are created per-window in
 *	TkGlfwCreateWindow; no global cg context is needed here.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initializes GLFW and creates a hidden shared GL context window.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwInitialize(void)
{
    if (glfwContext.initialized) return TCL_OK;

    glfwSetErrorCallback(TkGlfwErrorCallback);

#ifdef GLFW_PLATFORM_WAYLAND
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif

    if (!glfwInit()) {
        fprintf(stderr, "TkGlfwInitialize: glfwInit() failed\n");
        return TCL_ERROR;
    }

    glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);

    glfwContext.mainWindow =
        glfwCreateWindow(640, 480, "Tk Shared Context", NULL, NULL);
    if (!glfwContext.mainWindow) {
        fprintf(stderr, "TkGlfwInitialize: failed to create shared window\n");
        glfwTerminate();
        return TCL_ERROR;
    }

    glfwMakeContextCurrent(glfwContext.mainWindow);
    glfwSwapInterval(1);

    glfwContext.initialized = 1;
    shutdownInProgress = 0;

    Tcl_CreateExitHandler(TkGlfwShutdown, NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwShutdown --
 *
 *	Orderly cleanup of GLFW resources on app shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	GLFW terminated, all mappings freed.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwShutdown(TCL_UNUSED(void *))
{
    if (shutdownInProgress) return;
    shutdownInProgress = 1;

    if (!glfwContext.initialized) {
        shutdownInProgress = 0;
        return;
    }

    /* Clean up all window mappings (destroys GLFW windows and cg surfaces). */
    CleanupAllMappings();

    /* Destroy the global cg context if one was allocated. */
    if (glfwContext.cg) {
        cg_destroy(glfwContext.cg);
        glfwContext.cg = NULL;
    }

    if (glfwContext.mainWindow) {
        glfwDestroyWindow(glfwContext.mainWindow);
        glfwContext.mainWindow = NULL;
    }

    glfwPollEvents();

    if (glfwContext.initialized) {
        glfwTerminate();
        glfwContext.initialized = 0;
    }

    shutdownInProgress = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwCreateWindow --
 *
 *	Create a new GLFW window and allocate a libcg surface for it.
 *
 * Results:
 *	Returns the GLFWwindow pointer on success, NULL on failure.
 *
 * Side effects:
 *	Creates a new GLFW window, a cg_surface_t, and adds the window
 *	to the mapping list.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *
TkGlfwCreateWindow(
    TkWindow   *tkWin,
    int         width,
    int         height,
    const char *title,
    Drawable   *drawableOut)
{
    WindowMapping       *mapping;
    GLFWwindow          *window;
    struct cg_surface_t *surface;

    window = NULL;

    if (shutdownInProgress) return NULL;

    if (!glfwContext.initialized) {
        if (TkGlfwInitialize() != TCL_OK)
            return NULL;
    }

    /* Reuse existing mapping if already created for this TkWindow. */
    if (tkWin != NULL) {
        mapping = FindMappingByTk(tkWin);
        if (mapping != NULL) {
            if (drawableOut) *drawableOut = mapping->drawable;
            return mapping->glfwWindow;
        }
    }

    if (width  <= 0) width  = 200;
    if (height <= 0) height = 200;

    if (glfwContext.mainWindow != NULL) {
        window = glfwContext.mainWindow;
        glfwSetWindowSize(window, width, height);
        glfwSetWindowTitle(window, title ? title : "");
        glfwShowWindow(window);
        glfwContext.mainWindow = NULL;
    } else {
        glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE,             GLFW_TRUE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW,         GLFW_TRUE);
        glfwWindowHint(GLFW_AUTO_ICONIFY,          GLFW_FALSE);
        window = glfwCreateWindow(width, height, title ? title : "",
                                  NULL, NULL);
        if (!window) return NULL;
        glfwShowWindow(window);
    }

    /* Create the libcg pixel surface for this window. */
    surface = cg_surface_create(width, height);
    if (!surface) {
        fprintf(stderr, "TkGlfwCreateWindow: cg_surface_create() failed\n");
        glfwDestroyWindow(window);
        return NULL;
    }

    mapping = (WindowMapping *)ckalloc(sizeof(WindowMapping));
    memset(mapping, 0, sizeof(WindowMapping));
    mapping->tkWindow     = tkWin;
    mapping->glfwWindow   = window;
    mapping->drawable     = nextDrawableId++;
    mapping->width        = width;
    mapping->height       = height;
    mapping->clearPending = 1;
    mapping->surface      = surface;

    AddMapping(mapping);
    glfwSetWindowUserPointer(window, mapping);

    if (tkWin != NULL)
        TkGlfwSetupCallbacks(window, tkWin);

    /* Wait for the compositor to confirm real dimensions. */
    int timeout = 0;
    while ((mapping->width == 0 || mapping->height == 0) && timeout < 100) {
        glfwPollEvents();
        if (mapping->width == 0 || mapping->height == 0) {
            int w, h;
            glfwGetWindowSize(window, &w, &h);
            if (w > 0 && h > 0) {
                mapping->width  = w;
                mapping->height = h;
                break;
            }
        }
        timeout++;
    }

    if (mapping->width  == 0) mapping->width  = width;
    if (mapping->height == 0) mapping->height = height;

    if (tkWin != NULL) {
        tkWin->changes.width  = mapping->width;
        tkWin->changes.height = mapping->height;
    }

    if (drawableOut) *drawableOut = mapping->drawable;

    if (tkWin != NULL)
        TkWaylandQueueExposeEvent(tkWin, 0, 0, mapping->width, mapping->height);

    TkWaylandWakeupGLFW();

    return window;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwDestroyWindow --
 *
 *	Destroy a GLFW window and free the associated cg surface.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes the window from the mapping list, frees its cg surface,
 *	and destroys the GLFW window.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwDestroyWindow(GLFWwindow *glfwWindow)
{
    WindowMapping *mapping;

    if (!glfwWindow) return;
    if (shutdownInProgress) return;

    mapping = FindMappingByGLFW(glfwWindow);
    if (mapping) {
        if (mapping->surface) {
            cg_surface_destroy(mapping->surface);
            mapping->surface = NULL;
        }
        mapping->glfwWindow = NULL;
        RemoveMapping(mapping);
    }

    glfwDestroyWindow(glfwWindow);

    if (Tk_GetNumMainWindows() == 0 && !shutdownInProgress) {
        Tcl_DoWhenIdle(TkGlfwShutdown, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SyncWindowSize --
 *
 *	Synchronize Tk window size when framebuffer dimensions change.
 *	Recreates the cg surface if dimensions changed.
 *
 * Results:
 *	Tk window size updated.
 *
 * Side effects:
 *	May reallocate mapping->surface.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
SyncWindowSize(WindowMapping *m)
{
    int w, h;

    if (!m || !m->glfwWindow || shutdownInProgress) return;

    glfwGetWindowSize(m->glfwWindow, &w, &h);

    if (w <= 0 || h <= 0) return;

    if (w != m->width || h != m->height) {
        if (m->surface) {
            cg_surface_destroy(m->surface);
        }
        m->surface = cg_surface_create(w, h);
        if (!m->surface) {
            fprintf(stderr, "SyncWindowSize: cg_surface_create() failed\n");
        }
        m->width  = w;
        m->height = h;
    }

    if (m->tkWindow) {
        m->tkWindow->changes.width  = w;
        m->tkWindow->changes.height = h;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwBeginDraw --
 *
 *	Prepare a libcg context for drawing to the given drawable.
 *	Creates a cg_ctx_t bound to the window's cg_surface_t, saves
 *	drawing state, and applies child-window translation and GC settings.
 *
 * Results:
 *	TCL_OK if drawing can proceed, TCL_ERROR otherwise.
 *
 * Side effects:
 *	Allocates a cg_ctx_t stored in dcPtr->cg.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwBeginDraw(
    Drawable drawable,
    GC gc,
    TkWaylandDrawingContext *dcPtr)
{
    WindowMapping *m = FindMappingByDrawable(drawable);

    if (!m || !m->surface) {
        return TCL_ERROR;
    }

    dcPtr->cg          = cg_create(m->surface);
    dcPtr->drawable    = drawable;
    dcPtr->glfwWindow  = m->glfwWindow;
    dcPtr->width       = m->width;
    dcPtr->height      = m->height;
    dcPtr->offsetX     = 0;
    dcPtr->offsetY     = 0;
    dcPtr->nestedFrame = 0;

    if (!dcPtr->cg) {
        return TCL_ERROR;
    }

    cg_save(dcPtr->cg);

    /* Apply child-window translation. */
    if (m->tkWindow && !Tk_IsTopLevel(m->tkWindow)) {
        int x = 0, y = 0;
        TkWindow *winPtr = (TkWindow *)m->tkWindow;
        while (winPtr && !Tk_IsTopLevel(winPtr)) {
            x += winPtr->changes.x;
            y += winPtr->changes.y;
            winPtr = winPtr->parentPtr;
        }
        cg_translate(dcPtr->cg, (double)x, (double)y);
        dcPtr->offsetX = x;
        dcPtr->offsetY = y;
    }

    TkGlfwApplyGC(dcPtr->cg, gc);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwEndDraw --
 *
 *	End a drawing operation: restore cg state and release the context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys dcPtr->cg and marks the window dirty.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr)
{
    WindowMapping *m;

    if (!dcPtr || !dcPtr->cg) return;

    cg_restore(dcPtr->cg);
    cg_destroy(dcPtr->cg);
    dcPtr->cg = NULL;

    m = FindMappingByDrawable(dcPtr->drawable);
    if (m) {
        m->needsDisplay = 1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetCGContext --
 *
 *	Return the global libcg context, if one exists.
 *	cg contexts are normally per-window; this returns the global
 *	fallback stored in glfwContext.cg (may be NULL).
 *
 * Results:
 *	The cg_ctx_t pointer, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE struct cg_ctx_t *
TkGlfwGetCGContext(void)
{
    return (glfwContext.initialized && !shutdownInProgress) ?
            glfwContext.cg : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetCGContextForMeasure --
 *
 *	Return a cg context suitable for font measurement outside a draw
 *	frame.  Ensures the shared GL context is current.
 *
 * Results:
 *	Returns the cg_ctx_t or NULL on failure.
 *
 * Side effects:
 *	Makes the shared GL context current if no context is current.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE struct cg_ctx_t *
TkGlfwGetCGContextForMeasure(void)
{
    if (!glfwContext.initialized || shutdownInProgress)
        return NULL;
    if (!glfwGetCurrentContext() && glfwContext.mainWindow)
        glfwMakeContextCurrent(glfwContext.mainWindow);
    return glfwContext.cg;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwProcessEvents --
 *
 *	Process pending GLFW events. Called from the Tk event loop.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Polls and dispatches GLFW events.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwProcessEvents(void)
{
    if (glfwContext.initialized && !shutdownInProgress) {
        glfwPollEvents();
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Color / GC utilities
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwXColorToCG --
 *
 *	Convert an XColor structure to a cg_color_t.
 *
 * Results:
 *	Returns a cg_color_t value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE struct cg_color_t
TkGlfwXColorToCG(XColor *xcolor)
{
    struct cg_color_t c;
    if (!xcolor) {
        c.r = 0.0; c.g = 0.0; c.b = 0.0; c.a = 1.0;
        return c;
    }
    c.r = (xcolor->red   >> 8) / 255.0;
    c.g = (xcolor->green >> 8) / 255.0;
    c.b = (xcolor->blue  >> 8) / 255.0;
    c.a = 1.0;
    return c;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwPixelToCG --
 *
 *	Convert a 24-bit RGB pixel value to a cg_color_t.
 *
 * Results:
 *	Returns a cg_color_t value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE struct cg_color_t
TkGlfwPixelToCG(unsigned long pixel)
{
    struct cg_color_t c;
    c.r = ((pixel >> 16) & 0xFF) / 255.0;
    c.g = ((pixel >>  8) & 0xFF) / 255.0;
    c.b = ( pixel        & 0xFF) / 255.0;
    c.a = 1.0;
    return c;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwApplyGC --
 *
 *	Apply settings from a graphics context to a libcg context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets source color, line width, line cap, and line join on the
 *	cg context based on the GC values.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwApplyGC(struct cg_ctx_t *cg, GC gc)
{
    XGCValues         v;
    struct cg_color_t c;
    double            lw;

    if (!cg || !gc || shutdownInProgress) return;

    TkWaylandGetGCValues(gc,
        GCForeground|GCLineWidth|GCLineStyle|GCCapStyle|GCJoinStyle, &v);

    c  = TkGlfwPixelToCG(v.foreground);
    cg_set_source_rgba(cg, c.r, c.g, c.b, c.a);

    lw = (v.line_width > 0) ? (double)v.line_width : 1.0;
    cg_set_line_width(cg, lw);

    switch (v.cap_style) {
        case CapRound:      cg_set_line_cap(cg, CG_LINE_CAP_ROUND);  break;
        case CapProjecting: cg_set_line_cap(cg, CG_LINE_CAP_SQUARE); break;
        default:            cg_set_line_cap(cg, CG_LINE_CAP_BUTT);   break;
    }

    switch (v.join_style) {
        case JoinRound: cg_set_line_join(cg, CG_LINE_JOIN_ROUND); break;
        case JoinBevel: cg_set_line_join(cg, CG_LINE_JOIN_BEVEL); break;
        default:        cg_set_line_join(cg, CG_LINE_JOIN_MITER); break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk platform entry points
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * TkpInit --
 *
 *	Initialize the Tk platform-specific layer for Wayland/GLFW.
 *	Called during interpreter initialization.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initializes GLFW, Wayland protocols, and various Tk extensions
 *	(tray, system notifications, printing, accessibility).
 *
 *----------------------------------------------------------------------
 */

int
TkpInit(Tcl_Interp *interp)
{
    if (TkGlfwInitialize() != TCL_OK) return TCL_ERROR;
    TkWaylandMenuInit();
    Tk_WaylandSetupTkNotifier();
    Tktray_Init(interp);
    SysNotify_Init(interp);
    Cups_Init(interp);
    TkWaylandAccessibility_Init(interp);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetAppName --
 *
 *	Extract the application name from argv0 for use in window titles.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Appends the application name to the Tcl_DString.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetAppName(Tcl_Interp *interp, Tcl_DString *namePtr)
{
    const char *p, *name = Tcl_GetVar2(interp, "argv0", NULL, TCL_GLOBAL_ONLY);
    if (!name || !*name) name = "tk";
    else { p = strrchr(name, '/'); if (p) name = p+1; }
    Tcl_DStringAppend(namePtr, name, TCL_INDEX_NONE);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayWarning --
 *
 *	Display a warning message to stderr.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Writes the warning message to the standard error channel.
 *
 *----------------------------------------------------------------------
 */

void
TkpDisplayWarning(const char *msg, const char *title)
{
    Tcl_Channel ch = Tcl_GetStdChannel(TCL_STDERR);
    if (ch) {
        Tcl_WriteChars(ch, title, TCL_INDEX_NONE);
        Tcl_WriteChars(ch, ": ", 2);
        Tcl_WriteChars(ch, msg,  TCL_INDEX_NONE);
        Tcl_WriteChars(ch, "\n", 1);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FindMappingByGLFW --
 *
 *	Search the windowMappingList by native GLFW window handle.
 *
 * Results:
 *	Matching WindowMapping, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

WindowMapping *
FindMappingByGLFW(GLFWwindow *w)
{
    WindowMapping *c = windowMappingList;
    while (c) { if (c->glfwWindow == w) return c; c = c->nextPtr; }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FindMappingByTk --
 *
 *	Search the windowMappingList by Tk window pointer.
 *
 * Results:
 *	Matching WindowMapping, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

WindowMapping *
FindMappingByTk(TkWindow *w)
{
    WindowMapping *c = windowMappingList;
    while (c) { if (c->tkWindow == w) return c; c = c->nextPtr; }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FindMappingByDrawable --
 *
 *	Search the windowMappingList by Drawable.
 *
 * Results:
 *	Matching WindowMapping, or NULL.
 *
 * Side effects:
 *	May register an implicit drawable-to-mapping association.
 *
 *----------------------------------------------------------------------
 */

WindowMapping *
FindMappingByDrawable(Drawable d)
{
    DrawableMapping *dm;
    WindowMapping   *m;

    if (d == 0 || d == None) return NULL;

    /* Fast path: explicit registrations. */
    for (dm = drawableMappingList; dm; dm = dm->next) {
        if (dm->drawable == d)
            return dm->mapping;
    }

    /* Toplevel whose Tk window ID matches. */
    for (m = windowMappingList; m; m = m->nextPtr) {
        if (m->tkWindow && (Drawable)m->tkWindow->window == d)
            return m;
    }

    /* Drawable is a TkWindow* passed directly - walk the child tree. */
    for (m = windowMappingList; m; m = m->nextPtr) {
        if (!m->tkWindow) continue;
        TkWindow *stack[256];
        int top = 0;
        stack[top++] = m->tkWindow;
        while (top > 0) {
            TkWindow *cur = stack[--top];
            if ((Drawable)cur == d || (Drawable)cur->window == d) {
                RegisterDrawableForMapping(d, m);
                return m;
            }
            TkWindow *child;
            for (child = cur->childList; child && top < 255;
                 child = child->nextPtr)
                stack[top++] = child;
        }
    }

    /*
     * Drawable is a TkWaylandPixmapImpl* that was never registered.
     * Use the magic field to distinguish a real pixmap from a random
     * pointer before binding it to the first available mapping.
     */
    TkWaylandPixmapImpl *pix = (TkWaylandPixmapImpl *)d;
    if (pix != NULL && pix->magic == TK_WAYLAND_PIXMAP_MAGIC) {
        if (pix->width  >= 0 && pix->width  < 32768 &&
            pix->height >= 0 && pix->height < 32768) {
            m = TkGlfwGetMappingList();
            if (m) {
                RegisterDrawableForMapping(d, m);
                return m;
            }
        }
    }

    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetMappingList --
 *
 *	Return the head of the windowMappingList.
 *
 * Results:
 *	WindowMapping list head.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

WindowMapping *
TkGlfwGetMappingList(void)
{
    return windowMappingList;
}

/*
 *----------------------------------------------------------------------
 *
 * AddMapping --
 *
 *	Prepend an entry to the windowMappingList.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Entry prepended to windowMappingList.
 *
 *----------------------------------------------------------------------
 */

void
AddMapping(WindowMapping *m)
{
    if (!m) return;
    m->nextPtr    = windowMappingList;
    windowMappingList = m;
}

/*
 *----------------------------------------------------------------------
 *
 * RemoveMapping --
 *
 *	Remove an entry from the windowMappingList and free it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Entry removed and freed.
 *
 *----------------------------------------------------------------------
 */

void
RemoveMapping(WindowMapping *m)
{
    WindowMapping **pp = &windowMappingList;

    if (!m) {
        fprintf(stderr, "RemoveMapping: called with NULL mapping\n");
        return;
    }

    while (*pp) {
        if (*pp == m) {
            *pp = m->nextPtr;
            memset(m, 0, sizeof(WindowMapping));
            ckfree((char *)m);
            return;
        }
        pp = &(*pp)->nextPtr;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CleanupAllMappings --
 *
 *	Destroy all GLFW windows, free cg surfaces, and free mapping
 *	structures.  Called during shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All GLFW windows destroyed and mappings freed.
 *
 *----------------------------------------------------------------------
 */

void
CleanupAllMappings(void)
{
    WindowMapping *c = windowMappingList, *n;

    while (c) {
        n = c->nextPtr;
        if (c->surface) {
            cg_surface_destroy(c->surface);
            c->surface = NULL;
        }
        if (c->glfwWindow) {
            glfwDestroyWindow(c->glfwWindow);
        }
        memset(c, 0, sizeof(WindowMapping));
        ckfree((char *)c);
        c = n;
    }
    windowMappingList = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * RegisterDrawableForMapping --
 *
 *	Associate a Drawable with a WindowMapping in the drawable list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Entry prepended to drawableMappingList.
 *
 *----------------------------------------------------------------------
 */

void
RegisterDrawableForMapping(Drawable d, WindowMapping *m)
{
    DrawableMapping *dm = ckalloc(sizeof(DrawableMapping));
    dm->drawable = d;
    dm->mapping  = m;
    dm->next     = drawableMappingList;
    drawableMappingList = dm;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetGLFWWindow --
 *
 *	Retrieve the GLFW window associated with a Tk window.
 *
 * Results:
 *	GLFWwindow pointer, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *
TkGlfwGetGLFWWindow(Tk_Window tkwin)
{
    WindowMapping *m = FindMappingByTk((TkWindow *)tkwin);
    return m ? m->glfwWindow : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetDrawable --
 *
 *	Retrieve the Drawable associated with a GLFW window.
 *
 * Results:
 *	Drawable ID, or 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE Drawable
TkGlfwGetDrawable(GLFWwindow *w)
{
    WindowMapping *m = FindMappingByGLFW(w);
    return m ? m->drawable : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwResizeWindow --
 *
 *	Update cached dimensions for a GLFW window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	mapping->width and mapping->height updated.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwResizeWindow(GLFWwindow *w, int width, int height)
{
    WindowMapping *m = FindMappingByGLFW(w);
    if (m) { m->width = width; m->height = height; }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwUpdateWindowSize --
 *
 *	Update cached dimensions for a GLFW window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	mapping->width and mapping->height updated.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwUpdateWindowSize(GLFWwindow *glfwWindow, int width, int height)
{
    WindowMapping *m = FindMappingByGLFW(glfwWindow);
    if (m) { m->width = width; m->height = height; }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetWindowFromDrawable --
 *
 *	Retrieve the GLFW window associated with a Drawable.
 *
 * Results:
 *	GLFWwindow pointer, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *
TkGlfwGetWindowFromDrawable(Drawable drawable)
{
    WindowMapping *m = FindMappingByDrawable(drawable);
    return m ? m->glfwWindow : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetTkWindow --
 *
 *	Retrieve the Tk window associated with a GLFW window.
 *
 * Results:
 *	TkWindow pointer, or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkWindow *
TkGlfwGetTkWindow(GLFWwindow *glfwWindow)
{
    WindowMapping *m = FindMappingByGLFW(glfwWindow);
    return m ? m->tkWindow : NULL;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
