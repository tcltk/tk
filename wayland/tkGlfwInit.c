/*
 * tkGlfwInit.c --
 *
 *   GLFW/Wayland-specific interpreter initialization: context
 *   management, window mapping, drawing context lifecycle, color
 *   conversion, and platform init/cleanup. GLFW, NanoVG and libdecor
 *   provide the native platform on which Tk's widget set and event loop
1*   are deployed.  
 *  
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 2026  Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#define GL_GLEXT_PROTOTYPES

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <GLES2/gl2.h>

/*
 *----------------------------------------------------------------------
 *
 * Module-level state
 *
 *----------------------------------------------------------------------
 */

TkGlfwContext  glfwContext       = {NULL, NULL, 0, 0, NULL, 0, 0, NULL};
WindowMapping *windowMappingList = NULL;
static Drawable       nextDrawableId   = 1000;
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
    /* Don't print errors during shutdown. */
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
 *	Initialize the GLFW library, create a shared context window,
 *	initialize Wayland protocols, and create the global NanoVG context.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initializes GLFW, creates a hidden shared GL context window,
 *	and creates the global NanoVG context.
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

    /*
     * Shared context window - hidden. All application windows
     * share its GL context so textures and shaders are visible across them.
     */
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

    /* Create NanoVG context once, here, while the shared context is current. */
    glfwContext.vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!glfwContext.vg) {
        fprintf(stderr, "TkGlfwInitialize: nvgCreateGLES2() failed\n");
        glfwDestroyWindow(glfwContext.mainWindow);
        glfwContext.mainWindow = NULL;
        glfwTerminate();
        return TCL_ERROR;
    }

    /* Load core fonts for decoration text rendering. */
    nvgCreateFont(glfwContext.vg, "sans",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    nvgCreateFont(glfwContext.vg, "sans-bold", 
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
    nvgCreateFont(glfwContext.vg, "mono", 
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");

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
 *	Now safely handles both exit command and root window closure.
 *
 * Results:
 *	GLFW is closed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwShutdown(TCL_UNUSED(void *))
{
    /* Prevent recursive shutdown. */
    if (shutdownInProgress) return;
    shutdownInProgress = 1;
    
    if (!glfwContext.initialized) {
        shutdownInProgress = 0;
        return;
    }

    /* First, clean up all window mappings (this destroys GLFW windows). */
    CleanupAllMappings();

    /* Delete NanoVG while a context still exists. */
    if (glfwContext.vg) {
        /* Make the shared context current if it still exists. */
        if (glfwContext.mainWindow) {
            glfwMakeContextCurrent(glfwContext.mainWindow);
            nvgDeleteGLES2(glfwContext.vg);
        }
        glfwContext.vg = NULL;
    }

    /* Destroy the original hidden shared window. */
    if (glfwContext.mainWindow) {
        glfwDestroyWindow(glfwContext.mainWindow);
        glfwContext.mainWindow = NULL;
    }

    /* Poll one last time to let GLFW clean up internal state. */
    glfwPollEvents();
    
    /* Terminate GLFW. */
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
 *	Create a new GLFW window sharing the global GL context.
 *	Waits for the compositor's first configure event before returning
 *	so that BeginDraw always has valid dimensions.
 *
 * Results:
 *	Returns the GLFWwindow pointer on success, NULL on failure.
 *
 * Side effects:
 *	Creates a new GLFW window, adds it to the window mapping list.
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
    WindowMapping *mapping;
    GLFWwindow    *window;
    
    window = NULL;

    /* Don't create windows during shutdown. */
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

    /*
     * Reuse mainWindow for the first visible window.  NanoVG was created
     * on mainWindow so its GL objects are already present on that context.
     */
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
                                  NULL, glfwContext.mainWindow); /* Share context */
        if (!window) return NULL;
        glfwShowWindow(window);
    }

    /* Allocate and initialize mapping. */
    mapping = (WindowMapping *)ckalloc(sizeof(WindowMapping));
    memset(mapping, 0, sizeof(WindowMapping));
    mapping->tkWindow     = tkWin;
    mapping->glfwWindow   = window;
    mapping->drawable     = nextDrawableId++;
    mapping->width        = width;
    mapping->height       = height;
    mapping->clearPending = 1;
    
    mapping->fbo = nvgluCreateFramebuffer(glfwContext.vg, width, height, NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY);
	if (mapping->fbo == NULL) {
		fprintf(stderr, "Could not create NanoVG framebuffer\n");
	}

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

    /* Wake up the event loop to process the expose event. */
    TkWaylandWakeupGLFW();

    return window;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwDestroyWindow --
 *
 *	Destroy a GLFW window and clean up associated resources.
 *	Now safely handles destruction during shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes the window from the mapping list and destroys the GLFW window.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwDestroyWindow(GLFWwindow *glfwWindow)
{
    WindowMapping *mapping;
    
    if (!glfwWindow) return;
    if (shutdownInProgress) return;  /* Let CleanupAllMappings handle it */

    mapping = FindMappingByGLFW(glfwWindow);
    if (mapping) {
        /* Mark window as being destroyed */
        mapping->glfwWindow = NULL;
        RemoveMapping(mapping);
    }

    glfwDestroyWindow(glfwWindow);
    
    /* Check if this was the last window. */
    if (Tk_GetNumMainWindows() == 0 && !shutdownInProgress) {
        /* Schedule shutdown via idle callback. */
        Tcl_DoWhenIdle(TkGlfwShutdown, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SyncWindowSize--
 *
 * Helper function to synchronize Tk window size when
 * the framebuffer dimensions change.
 * 
 * Results:
 *	Tk window size updated. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */


MODULE_SCOPE 
void SyncWindowSize(WindowMapping *m)
{

    if (!m || !m->glfwWindow || shutdownInProgress) return;
    int w, h;
    int fbw, fbh;
    glfwGetWindowSize(m->glfwWindow, &w, &h);
    glfwGetFramebufferSize(m->glfwWindow, &fbw, &fbh);

    m->width  = w;
    m->height = h;

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
 *	Prepares the NanoVG context for drawing. Uses the provided 
 * dcPtr to store context-specific state.
 *
 * Results:
 *	TCL_OK if drawing can proceed, TCL_ERROR otherwise.
 *
 * Side effects:
 *	Saves NanoVG state and applies translations.
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

    if (!m || !m->fbo) {
        return TCL_ERROR;
    }

    /* Redirect all subsequent GL commands to the off-screen FBO. */
    nvgluBindFramebuffer(m->fbo);
    
    /* Set viewport to the FBO size. */
    glViewport(0, 0, m->width, m->height);

    /* Start a NanoVG frame targeting the FBO. */
    nvgBeginFrame(glfwContext.vg, (float)m->width, (float)m->height, 1.0f);

    dcPtr->vg = glfwContext.vg;

    /* Save state for this specific primitive. */
    nvgSave(dcPtr->vg);

    /* Handle coordinates (Child window offsets). */
    if (m->tkWindow && !Tk_IsTopLevel(m->tkWindow)) {
        int x = 0, y = 0;
        TkWindow *winPtr = (TkWindow *)m->tkWindow;
        while (winPtr && !Tk_IsTopLevel(winPtr)) {
            x += winPtr->changes.x;
            y += winPtr->changes.y;
            winPtr = winPtr->parentPtr;
        }
        nvgTranslate(dcPtr->vg, (float)x, (float)y);
    }

    TkGlfwApplyGC(dcPtr->vg, gc);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwEndDraw --
 *
 *      End a drawing operation.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Pops NanoVG state.
 *      Unbinds pixmap FBO if drawing to pixmap.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr)
{
    if (dcPtr && dcPtr->vg) {
        nvgRestore(dcPtr->vg);
        nvgEndFrame(dcPtr->vg);
        
        /* Unbind FBO to return to the default backbuffer. */
        nvgluBindFramebuffer(NULL);
        
        /* Signal that the screen needs to be updated with the FBO's content. */
        WindowMapping *m = FindMappingByDrawable(dcPtr->drawable);
        m->needsDisplay = 1;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetNVGContext --
 *
 *	Return the global NanoVG context.
 *
 * Results:
 *	The NVGcontext pointer, or NULL if not yet initialized.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext *
TkGlfwGetNVGContext(void)
{
    return (glfwContext.initialized && !shutdownInProgress) ? 
            glfwContext.vg : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetNVGContextForMeasure --
 *
 *	Return the NanoVG context with the shared GL context current,
 *	suitable for font measurement outside a draw frame.
 *
 * Results:
 *	Returns the NanoVG context or NULL on failure.
 *
 * Side effects:
 *	Makes the shared GL context current if no context is current.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext *
TkGlfwGetNVGContextForMeasure(void)
{
    if (!glfwContext.initialized || !glfwContext.vg || shutdownInProgress) 
        return NULL;
    if (!glfwGetCurrentContext())
        glfwMakeContextCurrent(glfwContext.mainWindow);
    return glfwContext.vg;
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
 * TkGlfwXColorToNVG --
 *
 *	Convert an XColor structure to an NVGcolor.
 *
 * Results:
 *	Returns an NVGcolor value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcolor
TkGlfwXColorToNVG(XColor *xcolor)
{
    if (!xcolor) return nvgRGBA(0, 0, 0, 255);
    return nvgRGBA(xcolor->red >> 8, xcolor->green >> 8, xcolor->blue >> 8, 255);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwPixelToNVG --
 *
 *	Convert a 24-bit RGB pixel value to an NVGcolor.
 *
 * Results:
 *	Returns an NVGcolor value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcolor
TkGlfwPixelToNVG(unsigned long pixel)
{
    return nvgRGBA((pixel>>16)&0xFF, (pixel>>8)&0xFF, pixel&0xFF, 255);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwApplyGC --
 *
 *	Apply settings from a graphics context to the NanoVG context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the NanoVG context's fill color, stroke color, line
 *	width, line caps, and line joins based on the GC values.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwApplyGC(NVGcontext *vg, GC gc)
{
    XGCValues v;
    NVGcolor  c;
    
    if (!vg || !gc || shutdownInProgress) return;
      
    TkWaylandGetGCValues(gc,
                         GCForeground|GCLineWidth|GCLineStyle|GCCapStyle|GCJoinStyle, &v);
    c = TkGlfwPixelToNVG(v.foreground);
    nvgFillColor(vg, c);
    nvgFillColor(vg, c);
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, v.line_width > 0 ? (float)v.line_width : 1.0f);
    switch (v.cap_style) {
        case CapRound:      nvgLineCap(vg, NVG_ROUND);  break;
        case CapProjecting: nvgLineCap(vg, NVG_SQUARE); break;
        default:            nvgLineCap(vg, NVG_BUTT);   break;
    }
    switch (v.join_style) {
        case JoinRound: nvgLineJoin(vg, NVG_ROUND); break;
        case JoinBevel: nvgLineJoin(vg, NVG_BEVEL); break;
        default:        nvgLineJoin(vg, NVG_MITER); break;
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
 *	Initializes GLFW, Wayland protocols, NanoVG, and various
 *	Tk extensions (tray, system notifications, printing, accessibility).
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
 * FindMappingbyGLFW --
 *
 *	Searches the windowMappingList by native GLFW window handle.
 *
 * Results:
 *	Retrieves mapping entry. 
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
 * FindMappingbyTk --
 *
 *	Searches the windowMappingList by Tk window pointer.
 *
 * Results:
 *	Retrieves mapping entry. 
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
 * FindMappingbyDrawable --
 *
 *	Searches the windowMappingList by Drawable.
 *
 * Results:
 *	Retrieves mapping entry. 
 *
 * Side effects:
 *	None.
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

    /* Drawable is a TkWindow* passed directly. */
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
     * Drawable is a TkWaylandPixmapImpl* that was never
     * registered. Validate by checking that its fields look sane,
     * then bind it to the first available mapping. 
     */
    TkWaylandPixmapImpl *pix = (TkWaylandPixmapImpl *)d;
    if (pix != NULL) {
        /* Sanity check: type must be 0 or 1, dimensions must be
         * plausible. This guards against random pointer misinterpretation. */
        if ((pix->type == 0 || pix->type == 1) &&
            pix->width  >= 0 && pix->width  < 32768 &&
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
 *	Retrieves the entire windowMappingList. 
 *
 * Results:
 *	Returns list. 
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
 * AddMapping  --
 *
 *	Add an entry to the windowMappingList.
 *
 * Results:
 *	Entry is added. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

void
AddMapping(WindowMapping *m)
{
    if (!m) return;
    m->nextPtr  = windowMappingList;
    windowMappingList = m;
}


/*
 *----------------------------------------------------------------------
 *
 * RemoveMappings --
 *
 *  Remove an entry from the windowMappingList. 
 *
 * Results:
 *	Entry is added. 
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
void
RemoveMapping(WindowMapping *m)
{
    WindowMapping **pp = &windowMappingList;
    
    if (!m) {
        fprintf(stderr, "RemoveMapping: Called with NULL mapping\n");
        return;
    }
    
    while (*pp) {
        if (*pp == m) { 
            *pp = m->nextPtr; 
            
            /* Clear the mapping before freeing to detect use-after-free. */
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
 *	Destroy all GLFW windows and free mapping structures.
 *	Called during shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All GLFW windows are destroyed and mappings freed.
 *
 *----------------------------------------------------------------------
 */

void
CleanupAllMappings(void)
{
    WindowMapping *c = windowMappingList, *n;
    
    while (c) {
        n = c->nextPtr;
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
 *	Adds a Drawable to the windowMappingList.
 *
 * Results:
 *	Entry added. 
 *
 * Side effects:
 *	None.
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
 *	Retrieves the GLFW window associated with a Tk window.
 *
 * Results:
 *	Window pointer returned.
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
 *	Retrieves the Drawable associated with a GLFW window.
 *
 * Results:
 *	Window pointer returned.
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
 *	Resizes a GLFW window.
 *
 * Results:
 *	Window dimensions updated.
 *
 * Side effects:
 *	None.
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
 *	Updates the side of a GLFW window.
 *
 * Results:
 *	Window dimensions updated.
 *
 * Side effects:
 *	None.
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
 *	Retrieves the GLFW window associated with a Drawable.
 *
 * Results:
 *	Window pointer returned.
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
 *	Retrieves the Tk window associated with a GLFW window.
 *
 * Results:
 *	Window pointer returned.
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
