/*
 * tkWaylandPopup.c --
 *
 *	Native Wayland popup window primitive for the Tk/GLFW backend.
 *
 *	This module implements borderless transient popup surfaces using the
 *	xdg_popup / xdg_positioner protocol.  Each TkWaylandPopup is the
 *	base object from which menus, combobox dropdowns, tooltips, and any
 *	other override-redirect surface are built.
 *
 *	Architecture
 *	------------
 *	GLFW has no concept of popup windows; it only creates xdg_toplevels.
 *	We therefore bypass GLFW for the surface itself:
 *
 *	  - We bind our own wl_registry listener early in initialisation to
 *	    obtain a private xdg_wm_base, wl_compositor, and wl_seat handle.
 *	    (GLFW also binds these internally, but they are opaque to us.)
 *
 *	  - For each popup we create a plain wl_compositor-allocated
 *	    wl_surface, wrap it in an xdg_surface, and assign it the
 *	    xdg_popup role via xdg_surface_get_popup.
 *
 *	  - Rendering is performed with EGL + a per-popup GLES context that
 *	    shares object namespaces (textures, programs) with the main
 *	    GLFW context, plus a per-popup NanoVG context.
 *
 *	  - Positioning follows the xdg_positioner rules.  Because
 *	    glfwSetWindowPos is a no-op on Wayland, all placement is
 *	    expressed in terms of parent-surface-relative anchor rectangles
 *	    and gravity, which the compositor resolves.
 *
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#define GL_GLEXT_PROTOTYPES

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <stdio.h>  /* For debug output */

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WAYLAND
#define GLFW_EXPOSE_NATIVE_EGL
#include <GLFW/glfw3native.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "xdg-shell-client-protocol.h"

#define NANOVG_GLES3 1
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

/* Debug macro */
#define POPUP_DEBUG(msg, ...) fprintf(stderr, "POPUP: " msg "\n", ##__VA_ARGS__)

/*
 * The root GLFWwindow, defined in tkGlfwInit.c.  Used as the share-context
 * source for popup EGL contexts.
 */
extern GLFWwindow *mainGlfwWindow;

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopup -- the popup instance structure.
 *
 *	This structure must be defined here (not just in the header) because
 *	the header declares TkWaylandPopup as an opaque type.
 *
 *----------------------------------------------------------------------
 */

struct TkWaylandPopup {
    /* Wayland objects. */
    struct wl_surface    *surface;
    struct wl_egl_window *eglWindow;
    struct xdg_surface   *xdgSurface;
    struct xdg_popup     *xdgPopup;
    struct wl_subsurface *subsurface;   /* Non-NULL for subsurface-mode
                                         * popups (e.g. the menubar);
                                         * xdgSurface/xdgPopup are NULL
                                         * in that case. */
    struct wl_surface    *parentSurface; /* Parent surface for subsurface. */

    /* EGL objects. */
    EGLDisplay  eglDisplay;
    EGLSurface  eglSurface;
    EGLContext  eglContext;     /* Shares objects with mainGlfwWindow. */

    /* NanoVG. */
    NVGcontext *vg;

    /* Geometry (logical pixels - compositor coordinates). */
    int x, y;                   /* Position confirmed by compositor. */
    int width, height;          /* Requested / confirmed size */

    /* State flags */
    int configured;             /* 1 after first xdg_surface configure
                                 * OR for subsurfaces (always 1). */
    int mapped;
    int visible;                /* Track visibility state. */

    /* Optional dismiss callback. */
    void (*doneCallback)(void *clientData);
    void  *doneClientData;

    /* Linked list of all live popups. */
    struct TkWaylandPopup *nextPtr;
};

/*
 *----------------------------------------------------------------------
 *
 * Module-level Wayland globals
 *
 *	Populated once by TkWaylandPopupInit via a wl_registry listener.
 *
 *----------------------------------------------------------------------
 */

static struct wl_display      *popupDisplay      = NULL;
static struct wl_compositor   *popupCompositor   = NULL;
static struct wl_subcompositor *popupSubcompositor = NULL;
static struct xdg_wm_base     *popupWmBase       = NULL;
static struct wl_seat         *popupSeat         = NULL;
static uint32_t                popupLastSerial   = 0;

static int popupModuleInitialized = 0;
static TkWaylandPopup *popupList = NULL;

/*
 * Forward declarations.
 */
static int BuildEGLSurface(TkWaylandPopup *popup);
static void RestoreMainContext(void);
static void FrameCallbackDone(void *data, struct wl_callback *cb, uint32_t time);

/*
 *----------------------------------------------------------------------
 *
 * FrameCallbackDone --
 *
 *	Callback when a frame is actually displayed by the compositor.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys the callback.
 *
 *----------------------------------------------------------------------
 */

static void
FrameCallbackDone(
    void *data,
    struct wl_callback *cb,
    uint32_t time)
{
    POPUP_DEBUG("Frame callback: surface visible at time %u", time);
    wl_callback_destroy(cb);
}

static const struct wl_callback_listener frameListener = {
    FrameCallbackDone
};

/*
 *----------------------------------------------------------------------
 *
 * RestoreMainContext --
 *
 *	Restore the main GLFW window's EGL context as current.
 *	This must be called after any popup operation that changed
 *	the current context to avoid leaving no context current or
 *	a popup context current when the main window needs to draw.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Makes the main GLFW window's EGL context current.
 *
 *----------------------------------------------------------------------
 */

static void
RestoreMainContext(void)
{
    if (!mainGlfwWindow) return;

    EGLDisplay display = glfwGetEGLDisplay();
    EGLSurface surface = glfwGetEGLSurface(mainGlfwWindow);
    EGLContext context = glfwGetEGLContext(mainGlfwWindow);

    if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE &&
        context != EGL_NO_CONTEXT) {
        eglMakeCurrent(display, surface, surface, context);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RegistryGlobal --
 *
 *	Wayland registry global object handler. Binds compositor,
 *	xdg_wm_base, and seat interfaces when they appear in the registry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes popupCompositor, popupSubcompositor, popupWmBase,
 *	and popupSeat.
 *
 *----------------------------------------------------------------------
 */

static void
RegistryGlobal(
    TCL_UNUSED(void *), /* data */
    struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    TCL_UNUSED(uint32_t)) /* version */
{

    POPUP_DEBUG("Registry global: %s", interface);

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        popupCompositor = wl_registry_bind(registry, name,
            &wl_compositor_interface, 4);
        POPUP_DEBUG("Bound wl_compositor");
    } else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
        popupSubcompositor = wl_registry_bind(registry, name,
            &wl_subcompositor_interface, 1);
        POPUP_DEBUG("Bound wl_subcompositor");
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        popupWmBase = wl_registry_bind(registry, name,
            &xdg_wm_base_interface, 2);
        POPUP_DEBUG("Bound xdg_wm_base");
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        if (!popupSeat) {
            popupSeat = wl_registry_bind(registry, name,
                &wl_seat_interface, 4);
            POPUP_DEBUG("Bound wl_seat");
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RegistryGlobalRemove --
 *
 *	Wayland registry global removal handler. Required for registry
 *	listener but does nothing in this implementation.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
RegistryGlobalRemove(
    TCL_UNUSED(void  *), /* data */
    TCL_UNUSED(struct wl_registry *), /* registry */
    TCL_UNUSED(uint32_t)) /* name */
{

}

static const struct wl_registry_listener registryListener = {
    RegistryGlobal,
    RegistryGlobalRemove,
};

/*
 *----------------------------------------------------------------------
 *
 * WmBasePing --
 *
 *	xdg_wm_base ping handler for compositor keepalive. Responds to
 *	compositor pings to maintain the connection.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sends a pong response to the compositor.
 *
 *----------------------------------------------------------------------
 */

static void
WmBasePing(
    TCL_UNUSED(void *), /* data */
    struct xdg_wm_base *wmBase,
    uint32_t serial)
{
    xdg_wm_base_pong(wmBase, serial);
    POPUP_DEBUG("Ping/pong");
}

static const struct xdg_wm_base_listener wmBaseListener = {
    WmBasePing
};

/*
 *----------------------------------------------------------------------
 *
 * XdgSurfaceConfigure --
 *
 *	xdg_surface configure event handler. The compositor sends this to
 *	confirm our popup geometry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Acknowledges the configure, marks popup as configured, and commits
 *	the surface.
 *
 *----------------------------------------------------------------------
 */

static void
XdgSurfaceConfigure(
    void *data,
    struct xdg_surface *xdgSurface,
    uint32_t serial)
{
    TkWaylandPopup *popup = (TkWaylandPopup *)data;

    POPUP_DEBUG("XdgSurfaceConfigure serial=%u", serial);
    xdg_surface_ack_configure(xdgSurface, serial);
    popup->configured = 1;

    if (popup->eglWindow && popup->width > 0 && popup->height > 0) {
        wl_egl_window_resize(popup->eglWindow, popup->width, popup->height,
                              0, 0);
        POPUP_DEBUG("Resized EGL window to %dx%d", popup->width, popup->height);
    }
    wl_surface_commit(popup->surface);
    wl_display_flush(popupDisplay);

    /*
     * Do NOT call RestoreMainContext here.  XdgSurfaceConfigure is a Wayland
     * protocol callback; it involves no EGL operations and therefore cannot
     * corrupt the current EGL context.  Calling RestoreMainContext here
     * would clobber whatever context BeginDraw set if the compositor delivers
     * a configure event during an active draw cycle, which is the primary
     * cause of the "EGL context corrupted on window event" failure mode.
     */
}

static const struct xdg_surface_listener xdgSurfaceListener = {
    XdgSurfaceConfigure
};

/*
 *----------------------------------------------------------------------
 *
 * XdgPopupConfigure --
 *
 *	xdg_popup configure event handler. Updates the popup's geometry
 *	based on compositor feedback.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates popup's x, y, width, and height fields.
 *
 *----------------------------------------------------------------------
 */

static void
XdgPopupConfigure(
    void *data,
    TCL_UNUSED(struct xdg_popup *), /* xdgPopup */
    int32_t x, int32_t y,
    int32_t width, int32_t height)
{
    TkWaylandPopup *popup = (TkWaylandPopup *)data;

    POPUP_DEBUG("XdgPopupConfigure: pos=(%d,%d) size=%dx%d", x, y, width, height);
    popup->x = x;
    popup->y = y;
    if (width  > 0) popup->width  = width;
    if (height > 0) popup->height = height;
}

/*
 *----------------------------------------------------------------------
 *
 * XdgPopupDone --
 *
 *	xdg_popup done event handler. Called when the compositor dismisses
 *	the popup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Invokes the done callback (if set) and destroys the popup.
 *
 *----------------------------------------------------------------------
 */

static void
XdgPopupDone(
    void *data,
    TCL_UNUSED(struct xdg_popup *)) /* xdgPopup */
{
    TkWaylandPopup *popup = (TkWaylandPopup *)data;

    POPUP_DEBUG("XdgPopupDone - popup dismissed by compositor");
    
    /* Cache properties locally in case the callback alters the popup structure. */
    void (*localCallback)(void *) = popup->doneCallback;
    void *localData = popup->doneClientData;

    /*
     * Perform local native resource teardown safely first to avoid
     * double-free or use-after-free patterns if the callback touches the popup.
     */
    TkWaylandPopupDestroy(popup);

    /* Invoke the high-level toolkit lifecycle notification afterward. */
    if (localCallback) {
        localCallback(localData);
    }
}

static const struct xdg_popup_listener xdgPopupListener = {
    XdgPopupConfigure,
    XdgPopupDone
};

/*
 *----------------------------------------------------------------------
 *
 * BuildEGLSurface --
 *
 *	Create an EGL surface for the popup's wl_surface, sharing object
 *	namespaces with the GLES context that GLFW created for the main
 *	window.
 *
 * Results:
 *	Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *	Allocates popup->eglWindow, popup->eglSurface, popup->eglContext.
 *
 *----------------------------------------------------------------------
 */

static int
BuildEGLSurface(
    TkWaylandPopup *popup)
{
    popup->eglDisplay = glfwGetEGLDisplay();
    if (popup->eglDisplay == EGL_NO_DISPLAY) {
        POPUP_DEBUG("No EGL display");
        return 0;
    }

    static const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      0,
        EGL_STENCIL_SIZE,    8,
        EGL_NONE
    };

    EGLConfig eglConfig;
    EGLint    numConfigs;
    if (!eglChooseConfig(popup->eglDisplay, configAttribs,
                         &eglConfig, 1, &numConfigs) || numConfigs == 0) {
        POPUP_DEBUG("eglChooseConfig failed");
        return 0;
    }

    popup->eglWindow = wl_egl_window_create(popup->surface,
                                             popup->width, popup->height);
    if (!popup->eglWindow) {
        POPUP_DEBUG("wl_egl_window_create failed");
        return 0;
    }

    popup->eglSurface = eglCreateWindowSurface(popup->eglDisplay, eglConfig,
        (EGLNativeWindowType)popup->eglWindow, NULL);
    if (popup->eglSurface == EGL_NO_SURFACE) {
        POPUP_DEBUG("eglCreateWindowSurface failed");
        wl_egl_window_destroy(popup->eglWindow);
        popup->eglWindow = NULL;
        return 0;
    }

    EGLContext shareCtx = glfwGetEGLContext(mainGlfwWindow);

    static const EGLint ctx3Attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    popup->eglContext = eglCreateContext(popup->eglDisplay, eglConfig,
                                          shareCtx, ctx3Attribs);
    if (popup->eglContext == EGL_NO_CONTEXT) {
        static const EGLint ctx2Attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
        };
        popup->eglContext = eglCreateContext(popup->eglDisplay, eglConfig,
                                              shareCtx, ctx2Attribs);
    }
    if (popup->eglContext == EGL_NO_CONTEXT) {
        POPUP_DEBUG("eglCreateContext failed");
        eglDestroySurface(popup->eglDisplay, popup->eglSurface);
        wl_egl_window_destroy(popup->eglWindow);
        popup->eglSurface = EGL_NO_SURFACE;
        popup->eglWindow  = NULL;
        return 0;
    }

    POPUP_DEBUG("EGL surface built successfully");
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupInit --
 *
 *	Initialize the Wayland popup module.
 *
 * Results:
 *	Returns TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Binds wl_compositor, xdg_wm_base, and wl_seat from the Wayland
 *	global registry. Must be called after glfwInit().
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandPopupInit(void)
{
    if (popupModuleInitialized) return TCL_OK;

    POPUP_DEBUG("Initializing Wayland popup module");

    popupDisplay = glfwGetWaylandDisplay();
    if (!popupDisplay) {
        POPUP_DEBUG("No Wayland display");
        return TCL_ERROR;
    }

    struct wl_registry *registry = wl_display_get_registry(popupDisplay);
    wl_registry_add_listener(registry, &registryListener, NULL);
    wl_display_roundtrip(popupDisplay);
    wl_display_roundtrip(popupDisplay);

    if (!popupCompositor || !popupWmBase) {
        POPUP_DEBUG("Missing compositor or wm_base");
        return TCL_ERROR;
    }

    xdg_wm_base_add_listener(popupWmBase, &wmBaseListener, NULL);

    popupModuleInitialized = 1;
    POPUP_DEBUG("Popup module initialized");
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupCreate --
 *
 *	Create and map a native xdg_popup.
 *
 * Results:
 *	Returns a newly allocated TkWaylandPopup, or NULL on failure.
 *
 * Side effects:
 *	A new Wayland surface is created and committed to the compositor.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkWaylandPopup *
TkWaylandPopupCreate(
    TCL_UNUSED(GLFWwindow *), /* parentGlfw */
    int anchorX, int anchorY,
    int anchorW, int anchorH,
    int popupW,  int popupH,
    uint32_t anchor,
    uint32_t gravity,
    int grabInput,
    uint32_t serial)
{

    POPUP_DEBUG("Creating xdg_popup: anchor=(%d,%d,%d,%d) popup=%dx%d",
                anchorX, anchorY, anchorW, anchorH, popupW, popupH);

    if (!popupModuleInitialized) {
        if (TkWaylandPopupInit() != TCL_OK) return NULL;
    }
    if (popupW  <= 0) popupW  = 1;
    if (popupH  <= 0) popupH  = 1;
    if (anchorW <= 0) anchorW = 1;
    if (anchorH <= 0) anchorH = 1;

    TkWaylandPopup *popup = Tcl_Alloc(sizeof(TkWaylandPopup));
    memset(popup, 0, sizeof(TkWaylandPopup));
    popup->width  = popupW;
    popup->height = popupH;
    popup->visible = 0;

    popup->surface = wl_compositor_create_surface(popupCompositor);
    if (!popup->surface) {
        POPUP_DEBUG("Failed to create wl_surface");
        Tcl_Free(popup);
        return NULL;
    }

    popup->xdgSurface = xdg_wm_base_get_xdg_surface(popupWmBase, popup->surface);
    if (!popup->xdgSurface) {
        POPUP_DEBUG("Failed to create xdg_surface");
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }
    xdg_surface_add_listener(popup->xdgSurface, &xdgSurfaceListener, popup);

    struct xdg_positioner *pos = xdg_wm_base_create_positioner(popupWmBase);
    if (!pos) {
        POPUP_DEBUG("Failed to create positioner");
        xdg_surface_destroy(popup->xdgSurface);
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }

    xdg_positioner_set_size(pos, popupW, popupH);
    xdg_positioner_set_anchor_rect(pos, anchorX, anchorY, anchorW, anchorH);
    xdg_positioner_set_anchor(pos, anchor);
    xdg_positioner_set_gravity(pos, gravity);
    xdg_positioner_set_constraint_adjustment(pos,
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y |
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X  |
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y);

    popup->xdgPopup = xdg_surface_get_popup(popup->xdgSurface, NULL, pos);
    xdg_positioner_destroy(pos);

    if (!popup->xdgPopup) {
        POPUP_DEBUG("Failed to get xdg_popup");
        xdg_surface_destroy(popup->xdgSurface);
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }
    xdg_popup_add_listener(popup->xdgPopup, &xdgPopupListener, popup);

    if (grabInput && popupSeat && serial != 0) {
        xdg_popup_grab(popup->xdgPopup, popupSeat, serial);
        POPUP_DEBUG("Grab input with serial %u", serial);
    }

    if (!BuildEGLSurface(popup)) {
        POPUP_DEBUG("BuildEGLSurface failed");
        xdg_popup_destroy(popup->xdgPopup);
        xdg_surface_destroy(popup->xdgSurface);
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }

    /* Make the popup context current to create NanoVG context. */
    eglMakeCurrent(popup->eglDisplay, popup->eglSurface,
                   popup->eglSurface, popup->eglContext);
    popup->vg = nvgCreateGLES3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    RestoreMainContext();

    if (!popup->vg) {
        POPUP_DEBUG("nvgCreateGLES3 failed");
        eglDestroyContext(popup->eglDisplay, popup->eglContext);
        eglDestroySurface(popup->eglDisplay, popup->eglSurface);
        wl_egl_window_destroy(popup->eglWindow);
        xdg_popup_destroy(popup->xdgPopup);
        xdg_surface_destroy(popup->xdgSurface);
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }

    wl_surface_commit(popup->surface);
    wl_display_flush(popupDisplay);

    int waitIter = 0;
    while (!popup->configured && waitIter < 20) {
        wl_display_dispatch(popupDisplay);
        waitIter++;
    }

    popup->mapped = 1;
    popup->visible = 1;
    popup->nextPtr = popupList;
    popupList = popup;

    POPUP_DEBUG("xdg_popup created successfully, configured=%d", popup->configured);
    return popup;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSubsurfaceCreate --
 *
 *	Create a wl_subsurface-backed popup without xdg_popup role.
 *
 * Results:
 *	Returns a newly allocated TkWaylandPopup, or NULL on failure.
 *
 * Side effects:
 *	Creates a subsurface that is positioned relative to its parent
 *	surface and does not receive input events.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkWaylandPopup *
TkWaylandSubsurfaceCreate(
    GLFWwindow *parentGlfw,
    int x, int y,
    int width, int height)
{
    POPUP_DEBUG("Creating subsurface popup: pos=(%d,%d) size=%dx%d", x, y, width, height);

    if (!popupModuleInitialized) {
        if (TkWaylandPopupInit() != TCL_OK) return NULL;
    }
    if (!popupSubcompositor) {
        POPUP_DEBUG("No subcompositor available");
        return NULL;
    }
    if (width  <= 0) width  = 1;
    if (height <= 0) height = 1;

    struct wl_surface *parentSurface = glfwGetWaylandWindow(parentGlfw);
    if (!parentSurface) {
        POPUP_DEBUG("No parent surface from GLFW window");
        return NULL;
    }

    TkWaylandPopup *popup = Tcl_Alloc(sizeof(TkWaylandPopup));
    memset(popup, 0, sizeof(TkWaylandPopup));
    popup->width  = width;
    popup->height = height;
    popup->x      = x;
    popup->y      = y;
    popup->visible = 0;
    popup->parentSurface = parentSurface;

    popup->surface = wl_compositor_create_surface(popupCompositor);
    if (!popup->surface) {
        POPUP_DEBUG("Failed to create surface");
        Tcl_Free(popup);
        return NULL;
    }

    popup->subsurface = wl_subcompositor_get_subsurface(
        popupSubcompositor, popup->surface, parentSurface);
    if (!popup->subsurface) {
        POPUP_DEBUG("Failed to create subsurface");
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }

    /* Position the subsurface. */
    wl_subsurface_set_position(popup->subsurface, x, y);
    
    /* Use desync mode for better performance. */
    wl_subsurface_set_desync(popup->subsurface);
    
    /* IMPORTANT: Place the subsurface ABOVE the parent surface. */
    wl_subsurface_place_above(popup->subsurface, parentSurface);
    POPUP_DEBUG("Subsurface placed above parent");

    if (!BuildEGLSurface(popup)) {
        POPUP_DEBUG("BuildEGLSurface failed");
        wl_subsurface_destroy(popup->subsurface);
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }

    /*
     * Commit the parent surface NOW - after the EGL window is created but
     * before we ever bind the popup's EGL context.
     *
     * This is the only safe window to do this.  The ordering constraint is:
     *
     *   TOO EARLY: before wl_egl_window_create - the EGL surface doesn't
     *              exist yet, Mesa will later see a surface committed without
     *              its wl_egl_window and reject it (EGL_BAD_MATCH).
     *
     *   THIS WINDOW: after wl_egl_window_create, before eglMakeCurrent.
     *              Mesa has not yet allocated a back buffer for this surface
     *              (that happens on first eglMakeCurrent or eglSwapBuffers).
     *              The compositor can safely register the wl_subsurface in
     *              its scene tree.  It will wait for the subsurface's own
     *              wl_surface_commit (from eglSwapBuffers) before compositing
     *              any content from it.
     *
     *   TOO LATE: after eglMakeCurrent (nvgCreateGLES3) - Mesa has now
     *              allocated a back buffer and tracks the surface as "dirty"
     *              with unflushed GL state.  The intervening main-window
     *              eglSwapBuffers (which fires during the expose storm between
     *              SubsurfaceCreate and MenuDrawIntoPopup) causes the
     *              compositor to process this parent commit and encounter the
     *              subsurface with a dirty-but-uncommitted EGL surface.
     *              Mesa then rejects the next eglSwapBuffers with EGL_BAD_MATCH.
     */
    wl_surface_commit(parentSurface);
    wl_display_flush(popupDisplay);

    /* Make the popup context current to create NanoVG context. */
    eglMakeCurrent(popup->eglDisplay, popup->eglSurface,
                   popup->eglSurface, popup->eglContext);
    popup->vg = nvgCreateGLES3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    RestoreMainContext();

    if (!popup->vg) {
        POPUP_DEBUG("nvgCreateGLES3 failed");
        eglDestroyContext(popup->eglDisplay, popup->eglContext);
        eglDestroySurface(popup->eglDisplay, popup->eglSurface);
        wl_egl_window_destroy(popup->eglWindow);
        wl_subsurface_destroy(popup->subsurface);
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }

    /* For subsurfaces, we are always configured - no xdg_surface handshake. */
    popup->configured = 1;
    popup->mapped     = 1;
    popup->visible    = 1;

    POPUP_DEBUG("Subsurface popup created and committed, configured=1");

    popup->nextPtr = popupList;
    popupList = popup;

    return popup;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSubsurfaceReconfigure --
 *
 *	Resize and/or reposition an existing subsurface-mode popup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Resizes the EGL window and updates the subsurface position.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandSubsurfaceReconfigure(
    TkWaylandPopup *popup,
    int x, int y,
    int width, int height)
{
    if (!popup || !popup->subsurface) return;

    POPUP_DEBUG("Reconfigure subsurface: pos=(%d,%d) size=%dx%d", x, y, width, height);

    if (width  <= 0) width  = 1;
    if (height <= 0) height = 1;

    if (width != popup->width || height != popup->height) {
        popup->width  = width;
        popup->height = height;
        if (popup->eglWindow) {
            wl_egl_window_resize(popup->eglWindow, width, height, 0, 0);
        }
    }

    if (x != popup->x || y != popup->y) {
        popup->x = x;
        popup->y = y;
        wl_subsurface_set_position(popup->subsurface, x, y);
    }
    
    /* Damage the entire surface to force redraw. */
    wl_surface_damage(popup->surface, 0, 0, popup->width, popup->height);
    wl_surface_commit(popup->surface);
    
    /* Also need to commit the parent surface. */
    if (popup->parentSurface) {
        wl_surface_commit(popup->parentSurface);
    }
    
    wl_display_flush(popupDisplay);
    /* No EGL operations performed; no context restore needed. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSubsurfacePlaceAbove --
 *
 *	Reorder a subsurface-mode popup to be stacked above a sibling.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Reorders the surface stack.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandSubsurfacePlaceAbove(
    TkWaylandPopup *popup,
    TkWaylandPopup *sibling)
{
    if (!popup || !popup->subsurface || !sibling || !sibling->surface) {
        return;
    }
    POPUP_DEBUG("Place subsurface above sibling");
    wl_subsurface_place_above(popup->subsurface, sibling->surface);
    wl_surface_commit(popup->surface);
    
    /* Commit parent to ensure stacking takes effect. */
    if (popup->parentSurface) {
        wl_surface_commit(popup->parentSurface);
    }
    
    wl_display_flush(popupDisplay);
    /* No EGL operations performed here; no context restore needed. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupDestroy --
 *
 *	Destroy a popup and free all associated resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys all Wayland objects, EGL resources, and the NanoVG context.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupDestroy(
    TkWaylandPopup *popup)
{
    if (!popup) return;

    POPUP_DEBUG("Destroying popup");

    /* Remove from linked list. */
    if (popupList == popup) {
        popupList = popup->nextPtr;
    } else {
        for (TkWaylandPopup *p = popupList; p; p = p->nextPtr) {
            if (p->nextPtr == popup) {
                p->nextPtr = popup->nextPtr;
                break;
            }
        }
    }

    /* 
     * Unconditionally unbind whatever context is current.  This must happen
     * before any EGL resource destruction so the driver does not try to flush
     * pending work through surfaces/contexts we are about to delete.
     */
    eglMakeCurrent(popup->eglDisplay != EGL_NO_DISPLAY
                       ? popup->eglDisplay
                       : glfwGetEGLDisplay(),
                   EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    if (popup->vg) {
        /*
         * Re-bind the popup context just long enough to delete the NanoVG
         * context, then immediately unbind again.  Only attempt this if both
         * the surface and context are still valid; if either has been torn
         * down already (e.g. by a concurrent resize) we leak the NanoVG
         * object rather than corrupting the main context.
         */
        if (popup->eglContext != EGL_NO_CONTEXT &&
            popup->eglSurface != EGL_NO_SURFACE &&
            popup->eglDisplay != EGL_NO_DISPLAY) {
            EGLBoolean ok = eglMakeCurrent(popup->eglDisplay,
                                           popup->eglSurface,
                                           popup->eglSurface,
                                           popup->eglContext);
            if (ok) {
                nvgDeleteGLES3(popup->vg);
                eglMakeCurrent(popup->eglDisplay,
                               EGL_NO_SURFACE, EGL_NO_SURFACE,
                               EGL_NO_CONTEXT);
            } else {
                POPUP_DEBUG("Warning: cannot bind popup ctx for NVG delete "
                            "(err 0x%x) — leaking NVG object", eglGetError());
            }
        } else {
            POPUP_DEBUG("Warning: Cannot delete NanoVG - context or surface invalid");
        }
        popup->vg = NULL;
    }

    if (popup->eglContext != EGL_NO_CONTEXT) {
        eglDestroyContext(popup->eglDisplay, popup->eglContext);
        popup->eglContext = EGL_NO_CONTEXT;
    }
    if (popup->eglSurface != EGL_NO_SURFACE) {
        eglDestroySurface(popup->eglDisplay, popup->eglSurface);
        popup->eglSurface = EGL_NO_SURFACE;
    }
    if (popup->eglWindow) {
        wl_egl_window_destroy(popup->eglWindow);
        popup->eglWindow = NULL;
    }

    if (popup->xdgPopup) {
        xdg_popup_destroy(popup->xdgPopup);
        popup->xdgPopup = NULL;
    }
    if (popup->xdgSurface) {
        xdg_surface_destroy(popup->xdgSurface);
        popup->xdgSurface = NULL;
    }
    if (popup->subsurface) {
        wl_subsurface_destroy(popup->subsurface);
        popup->subsurface = NULL;
    }
    if (popup->surface) {
        wl_surface_destroy(popup->surface);
        popup->surface = NULL;
    }

    if (popupDisplay) {
        wl_display_flush(popupDisplay);
    }
    Tcl_Free(popup);

    /*
     * Leave the EGL context unbound.  Callers that need the main context
     * restored after a Destroy (e.g. TkWaylandMenubarCreateOrResize) call
     * RestoreMainContext themselves.  Doing it here would clobber the popup
     * context of any sibling popup that is mid-draw when this Destroy runs,
     * which is the "context corrupted on window event" failure mode for
     * cascade/menubar interleaving.
     */
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupDestroyAll --
 *
 *	Destroy every live popup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Called from module shutdown to clean up all popup resources.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupDestroyAll(void)
{
    POPUP_DEBUG("Destroying all popups");
    while (popupList) {
        TkWaylandPopupDestroy(popupList);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupGetNVGContext --
 *
 *	Return the NanoVG context for a popup, making its EGL surface
 *	current first.
 *
 * Results:
 *	Returns the NVGcontext pointer, or NULL if popup is invalid.
 *
 * Side effects:
 *	Makes the popup's EGL context current.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext *
TkWaylandPopupGetNVGContext(
    TkWaylandPopup *popup)
{
    if (!popup || !popup->vg) return NULL;

    /*
     * Do NOT call eglMakeCurrent here.  The caller must have already called
     * TkWaylandPopupBeginDraw, which owns the context switch for the entire
     * BeginDraw → (draw commands) → EndDraw frame.  A second eglMakeCurrent
     * in the middle of a frame forces the driver to flush pending work and
     * can leave the surface in an inconsistent state on some Mesa versions,
     * which is the root cause of the context-corruption-on-window-event bug.
     */
    return popup->vg;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupBeginDraw --
 *
 *	Begin a NanoVG frame for the popup.
 *
 * Results:
 *	Returns TCL_OK on success, TCL_ERROR if the popup is not ready.
 *
 * Side effects:
 *	Makes the popup's EGL context current and calls nvgBeginFrame.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandPopupBeginDraw(
    TkWaylandPopup *popup)
{
    /* For subsurfaces, configured is always 1.
     * For xdg_popup, we must wait for configure event. */
    if (!popup || !popup->vg || !popup->configured) {
        if (popup && !popup->configured) {
            POPUP_DEBUG("BeginDraw: popup not configured yet");
        }
        return TCL_ERROR;
    }

    /* Always make the popup's EGL context current.
     * This is necessary because:
     * 1. The context may have been unbound during destruction of old surfaces.
     * 2. The main context may have been restored after previous operations.
     * 3. We need to ensure we're using the correct EGL surface.
     */
    if (popup->eglDisplay == EGL_NO_DISPLAY ||
        popup->eglSurface == EGL_NO_SURFACE ||
        popup->eglContext == EGL_NO_CONTEXT) {
        POPUP_DEBUG("BeginDraw: Invalid EGL state");
        return TCL_ERROR;
    }

    EGLBoolean madeCurrent = eglMakeCurrent(popup->eglDisplay, popup->eglSurface,
                                            popup->eglSurface, popup->eglContext);
    if (!madeCurrent) {
        EGLint error = eglGetError();
        POPUP_DEBUG("BeginDraw: eglMakeCurrent failed: 0x%x", error);
        return TCL_ERROR;
    }

    glViewport(0, 0, popup->width, popup->height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    /*
     * The device pixel ratio must match the compositor's expectation for
     * this output.  Hard-coding 1.0 produces blurry rendering on HiDPI
     * displays and can cause the compositor to silently discard buffer
     * content on some implementations.  Query the scale from GLFW using
     * the main window (popup subsurfaces inherit the parent output's scale).
     */
    float xscale = 1.0f, yscale = 1.0f;
    if (mainGlfwWindow) {
        glfwGetWindowContentScale(mainGlfwWindow, &xscale, &yscale);
    }
    if (xscale <= 0.0f) xscale = 1.0f;

    nvgBeginFrame(popup->vg, (float)popup->width, (float)popup->height, xscale);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupEndDraw --
 *
 *	Finish the NanoVG frame and swap buffers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Swaps EGL buffers, which automatically damages and commits the
 *	surface with the rendered content. Restores the main context
 *	after drawing is complete.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupEndDraw(
    TkWaylandPopup *popup)
{
    if (!popup || !popup->vg) return;

    nvgEndFrame(popup->vg);
    
    /* 
     * eglSwapBuffers on a wl_egl_window:
     *   - Attaches the rendered buffer to the surface.
     *   - Damages the entire surface area.
     *   - Commits the surface atomically.
     * 
     * DO NOT add additional wl_surface_damage or wl_surface_commit
     * here as that would overwrite the pending state with a blank
     * buffer, causing no visible content.
     */
    EGLBoolean swapped = eglSwapBuffers(popup->eglDisplay, popup->eglSurface);
    if (!swapped) {
        EGLint error = eglGetError();
        POPUP_DEBUG("eglSwapBuffers failed: 0x%x", error);
        
        /* Common recovery for BAD_MATCH. */
        if (error == EGL_BAD_MATCH && popup->eglWindow) {
            int w, h;
            TkWaylandPopupGetSize(popup, &w, &h);
            if (w > 0 && h > 0) {
                wl_egl_window_resize(popup->eglWindow, w, h, 0, 0);
                swapped = eglSwapBuffers(popup->eglDisplay, popup->eglSurface);
                if (swapped) {
                    POPUP_DEBUG("eglSwapBuffers succeeded after resize recovery");
                } else {
                    POPUP_DEBUG("eglSwapBuffers still failed after resize recovery");
                }
            }
        }
    } else {
        POPUP_DEBUG("eglSwapBuffers succeeded");
    }

    if (popupDisplay) {
        wl_display_flush(popupDisplay);
    }

    /* Restore main context after popup drawing. */
    RestoreMainContext();
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupMove --
 *
 *	Reposition an existing popup by destroying and re-creating it.
 *
 * Results:
 *	Returns a new TkWaylandPopup pointer, or NULL on failure. The
 *	original popup is always freed.
 *
 * Side effects:
 *	Destroys the old popup and creates a new one at the new position.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkWaylandPopup *
TkWaylandPopupMove(
    TkWaylandPopup *popup,
    GLFWwindow     *parentGlfw,
    int anchorX, int anchorY,
    int anchorW, int anchorH,
    uint32_t anchor,
    uint32_t gravity)
{
    if (!popup) return NULL;

    int w = popup->width;
    int h = popup->height;
    void (*doneCb)(void *) = popup->doneCallback;
    void *doneCd           = popup->doneClientData;

    TkWaylandPopupDestroy(popup);

    TkWaylandPopup *newPopup = TkWaylandPopupCreate(
        parentGlfw,
        anchorX, anchorY, anchorW, anchorH,
        w, h,
        anchor, gravity,
        0, 0);

    if (newPopup) {
        newPopup->doneCallback   = doneCb;
        newPopup->doneClientData = doneCd;
    }
    return newPopup;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupSetDoneCallback --
 *
 *	Register a callback invoked when the compositor dismisses the popup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The callback will be invoked from XdgPopupDone. The callback must
 *	NOT call TkWaylandPopupDestroy() as it is called automatically.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupSetDoneCallback(
    TkWaylandPopup *popup,
    void (*callback)(void *clientData),
    void *clientData)
{
    if (!popup) return;
    popup->doneCallback   = callback;
    popup->doneClientData = clientData;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupSetSerial / TkWaylandPopupLastSerial --
 *
 *	Module-level storage for the most recent wl_pointer.button serial.
 *
 * Results:
 *	SetSerial returns nothing. LastSerial returns the current serial.
 *
 * Side effects:
 *	The serial is used by xdg_popup_grab to establish input grabs.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupSetSerial(
    uint32_t serial)
{
    popupLastSerial = serial;
}

MODULE_SCOPE uint32_t
TkWaylandPopupLastSerial(void)
{
    return popupLastSerial;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupGetSize / TkWaylandPopupGetPosition --
 *
 *	Query the current (compositor-confirmed) geometry of a popup.
 *
 * Results:
 *	None (output via pointer parameters).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupGetSize(
    TkWaylandPopup *popup,
    int *widthOut,
    int *heightOut)
{
    if (popup) {
        if (widthOut)  *widthOut  = popup->width;
        if (heightOut) *heightOut = popup->height;
    } else {
        if (widthOut)  *widthOut  = 0;
        if (heightOut) *heightOut = 0;
    }
}

MODULE_SCOPE void
TkWaylandPopupGetPosition(
    TkWaylandPopup *popup,
    int *xOut,
    int *yOut)
{
    if (popup) {
        if (xOut) *xOut = popup->x;
        if (yOut) *yOut = popup->y;
    } else {
        if (xOut) *xOut = 0;
        if (yOut) *yOut = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupGetSeat --
 *
 *	Return the wl_seat bound during TkWaylandPopupInit.
 *
 * Results:
 *	Returns the wl_seat pointer, or NULL if not bound.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE struct wl_seat *
TkWaylandPopupGetSeat(void)
{
    return popupSeat;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
