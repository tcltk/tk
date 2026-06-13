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
    /* Wayland objects */
    struct wl_surface    *surface;
    struct wl_egl_window *eglWindow;
    struct xdg_surface   *xdgSurface;
    struct xdg_popup     *xdgPopup;
    struct wl_subsurface *subsurface;  /* non-NULL for subsurface-mode
                                         * popups (e.g. the menubar);
                                         * xdgSurface/xdgPopup are NULL
                                         * in that case. */

    /* EGL objects */
    EGLDisplay  eglDisplay;
    EGLSurface  eglSurface;
    EGLContext  eglContext;     /* shares objects with mainGlfwWindow */

    /* NanoVG */
    NVGcontext *vg;

    /* Geometry (logical pixels - compositor coordinates) */
    int x, y;                   /* position confirmed by compositor */
    int width, height;          /* requested / confirmed size */

    /* State flags */
    int configured;             /* 1 after first xdg_surface configure */
    int mapped;

    /* Optional dismiss callback */
    void (*doneCallback)(void *clientData);
    void  *doneClientData;

    /* Linked list of all live popups */
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
 * Forward declarations
 */
static int BuildEGLSurface(TkWaylandPopup *popup);

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
    void *data,
    struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version)
{
    (void)data; (void)version;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        popupCompositor = wl_registry_bind(registry, name,
            &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
        popupSubcompositor = wl_registry_bind(registry, name,
            &wl_subcompositor_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        popupWmBase = wl_registry_bind(registry, name,
            &xdg_wm_base_interface, 2);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        if (!popupSeat) {
            popupSeat = wl_registry_bind(registry, name,
                &wl_seat_interface, 4);
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
    void *data,
    struct wl_registry *registry,
    uint32_t name)
{
    (void)data; (void)registry; (void)name;
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
    void *data,
    struct xdg_wm_base *wmBase,
    uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wmBase, serial);
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

    xdg_surface_ack_configure(xdgSurface, serial);
    popup->configured = 1;

    if (popup->eglWindow && popup->width > 0 && popup->height > 0) {
        wl_egl_window_resize(popup->eglWindow, popup->width, popup->height,
                              0, 0);
    }
    wl_surface_commit(popup->surface);
    wl_display_flush(popupDisplay);
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
    struct xdg_popup *xdgPopup,
    int32_t x, int32_t y,
    int32_t width, int32_t height)
{
    TkWaylandPopup *popup = (TkWaylandPopup *)data;
    (void)xdgPopup;

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
    struct xdg_popup *xdgPopup)
{
    TkWaylandPopup *popup = (TkWaylandPopup *)data;
    (void)xdgPopup;

    if (popup->doneCallback) {
        popup->doneCallback(popup->doneClientData);
    }

    /*
     * Destroy the popup. The consumer's doneCallback is responsible for
     * any Tk-level cleanup.
     */
    TkWaylandPopupDestroy(popup);
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
        return 0;
    }

    popup->eglWindow = wl_egl_window_create(popup->surface,
                                             popup->width, popup->height);
    if (!popup->eglWindow) {
        return 0;
    }

    popup->eglSurface = eglCreateWindowSurface(popup->eglDisplay, eglConfig,
        (EGLNativeWindowType)popup->eglWindow, NULL);
    if (popup->eglSurface == EGL_NO_SURFACE) {
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
        eglDestroySurface(popup->eglDisplay, popup->eglSurface);
        wl_egl_window_destroy(popup->eglWindow);
        popup->eglSurface = EGL_NO_SURFACE;
        popup->eglWindow  = NULL;
        return 0;
    }

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

    popupDisplay = glfwGetWaylandDisplay();
    if (!popupDisplay) {
        return TCL_ERROR;
    }

    struct wl_registry *registry = wl_display_get_registry(popupDisplay);
    wl_registry_add_listener(registry, &registryListener, NULL);
    wl_display_roundtrip(popupDisplay);
    wl_display_roundtrip(popupDisplay);

    if (!popupCompositor || !popupWmBase) {
        return TCL_ERROR;
    }

    xdg_wm_base_add_listener(popupWmBase, &wmBaseListener, NULL);

    popupModuleInitialized = 1;
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
    GLFWwindow *parentGlfw,
    int anchorX, int anchorY,
    int anchorW, int anchorH,
    int popupW,  int popupH,
    uint32_t anchor,
    uint32_t gravity,
    int grabInput,
    uint32_t serial)
{
    (void)parentGlfw;

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

    popup->surface = wl_compositor_create_surface(popupCompositor);
    if (!popup->surface) {
        Tcl_Free(popup);
        return NULL;
    }

    popup->xdgSurface = xdg_wm_base_get_xdg_surface(popupWmBase, popup->surface);
    if (!popup->xdgSurface) {
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }
    xdg_surface_add_listener(popup->xdgSurface, &xdgSurfaceListener, popup);

    struct xdg_positioner *pos = xdg_wm_base_create_positioner(popupWmBase);
    if (!pos) {
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
        xdg_surface_destroy(popup->xdgSurface);
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }
    xdg_popup_add_listener(popup->xdgPopup, &xdgPopupListener, popup);

    if (grabInput && popupSeat && serial != 0) {
        xdg_popup_grab(popup->xdgPopup, popupSeat, serial);
    }

    if (!BuildEGLSurface(popup)) {
        xdg_popup_destroy(popup->xdgPopup);
        xdg_surface_destroy(popup->xdgSurface);
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }

    eglMakeCurrent(popup->eglDisplay, popup->eglSurface,
                   popup->eglSurface, popup->eglContext);
    popup->vg = nvgCreateGLES3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!popup->vg) {
        eglMakeCurrent(popup->eglDisplay, EGL_NO_SURFACE,
                       EGL_NO_SURFACE, EGL_NO_CONTEXT);
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
    popup->nextPtr = popupList;
    popupList = popup;

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
    if (!popupModuleInitialized) {
        if (TkWaylandPopupInit() != TCL_OK) return NULL;
    }
    if (!popupSubcompositor) {
        return NULL;
    }
    if (width  <= 0) width  = 1;
    if (height <= 0) height = 1;

    struct wl_surface *parentSurface = glfwGetWaylandWindow(parentGlfw);
    if (!parentSurface) {
        return NULL;
    }

    TkWaylandPopup *popup = Tcl_Alloc(sizeof(TkWaylandPopup));
    memset(popup, 0, sizeof(TkWaylandPopup));
    popup->width  = width;
    popup->height = height;
    popup->x      = x;
    popup->y      = y;

    popup->surface = wl_compositor_create_surface(popupCompositor);
    if (!popup->surface) {
        Tcl_Free(popup);
        return NULL;
    }

    popup->subsurface = wl_subcompositor_get_subsurface(
        popupSubcompositor, popup->surface, parentSurface);
    if (!popup->subsurface) {
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }

    wl_subsurface_set_position(popup->subsurface, x, y);
    wl_subsurface_set_desync(popup->subsurface);

    /* Make subsurface input-transparent */
    {
        struct wl_region *emptyRegion =
            wl_compositor_create_region(popupCompositor);
        if (emptyRegion) {
            wl_surface_set_input_region(popup->surface, emptyRegion);
            wl_region_destroy(emptyRegion);
        }
    }

    if (!BuildEGLSurface(popup)) {
        wl_subsurface_destroy(popup->subsurface);
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }

    eglMakeCurrent(popup->eglDisplay, popup->eglSurface,
                   popup->eglSurface, popup->eglContext);
    popup->vg = nvgCreateGLES3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!popup->vg) {
        eglMakeCurrent(popup->eglDisplay, EGL_NO_SURFACE,
                       EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(popup->eglDisplay, popup->eglContext);
        eglDestroySurface(popup->eglDisplay, popup->eglSurface);
        wl_egl_window_destroy(popup->eglWindow);
        wl_subsurface_destroy(popup->subsurface);
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }

    popup->configured = 1;
    popup->mapped     = 1;

    wl_surface_commit(popup->surface);
    wl_surface_commit(parentSurface);
    wl_display_flush(popupDisplay);

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
        wl_surface_commit(popup->surface);
    }
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
    wl_subsurface_place_above(popup->subsurface, sibling->surface);
    wl_surface_commit(popup->surface);
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

    /* Remove from linked list */
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

    if (popup->vg) {
        eglMakeCurrent(popup->eglDisplay, popup->eglSurface,
                       popup->eglSurface, popup->eglContext);
        nvgDeleteGLES3(popup->vg);
        popup->vg = NULL;
    }

    if (popup->eglContext != EGL_NO_CONTEXT) {
        eglMakeCurrent(popup->eglDisplay, EGL_NO_SURFACE,
                       EGL_NO_SURFACE, EGL_NO_CONTEXT);
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
    eglMakeCurrent(popup->eglDisplay, popup->eglSurface,
                   popup->eglSurface, popup->eglContext);
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
    if (!popup || !popup->vg || !popup->configured) return TCL_ERROR;

    eglMakeCurrent(popup->eglDisplay, popup->eglSurface,
                   popup->eglSurface, popup->eglContext);

    glViewport(0, 0, popup->width, popup->height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    nvgBeginFrame(popup->vg, (float)popup->width, (float)popup->height, 1.0f);
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
 *	Swaps EGL buffers and flushes the Wayland display.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupEndDraw(
    TkWaylandPopup *popup)
{
    if (!popup || !popup->vg) return;

    nvgEndFrame(popup->vg);
    eglSwapBuffers(popup->eglDisplay, popup->eglSurface);
    if (popupDisplay) {
        wl_display_flush(popupDisplay);
    }
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
