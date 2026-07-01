/*
 * tkWaylandPopup.c --
 *
 *	Native Wayland popup/sub-surface primitive for Tk.
 *
 *	This module implements a lightweight wrapper around xdg_popup and
 *	wl_subsurface objects. All rendering uses the shared GL context
 *	owned by GLFW - no per-popup EGL contexts or surfaces are created.
 *
 * Copyright © 2026 Kevin Walzer
 * Copyright © 2026 Marc Culler
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include <tcl.h>
#include "tkInt.h"
#include "tkWaylandInt.h"
#include <GLFW/glfw3.h>
#ifndef GLFW_EXPOSE_NATIVE_WAYLAND
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#include <GLFW/glfw3native.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-shell-client-protocol.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* NanoVG includes. */
#define NANOVG_GLES3 1
#include "nanovg.h"
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

/* Debug macro. */
#define POPUP_DEBUG 1
#if POPUP_DEBUG
#define POPUP_LOG(fmt, ...) fprintf(stderr, "POPUP: " fmt "\n", ##__VA_ARGS__)
#else
#define POPUP_LOG(fmt, ...) ((void)0)
#endif

/*
 * Global Wayland objects from tkWaylandInit.c.
 */
extern struct wl_display *waylandDisplay;
extern struct wl_compositor *waylandCompositor;
extern struct wl_subcompositor *waylandSubcompositor;
extern struct xdg_wm_base *waylandWmBase;
extern struct wl_seat *waylandSeat;

/*
 * REMOVED: globalNanoVGContext - no longer used
 * Each window owns its own context via glfwTkInfo
 * We now get the context from the main window's glfwTkInfo
 */

/*
 * Internal popup structure (opaque in tkWaylandInt.h).
 */
struct TkWaylandPopup {
    /* Wayland objects. */
    struct wl_surface     *surface;
    struct xdg_surface    *xdgSurface;
    struct xdg_popup      *xdgPopup;
    struct wl_subsurface  *subsurface;
    struct wl_region      *inputRegion;
    struct wl_surface     *parentSurface;
    
    /* Geometry. */
    int                    width;
    int                    height;
    int                    x, y;
    
    /* State. */
    int                    drawing;
    uint32_t               serial;
    GLFWwindow            *parentGlfw;
    int                    isSubsurface;
    int                    configured;
    
    /* Callback. */
    void                  (*doneCallback)(void *clientData);
    void                   *doneClientData;
    
    /* NanoVG context - points to the main window's context. */
    NVGcontext            *vg;
};

/* Global state for this module. */
static struct {
    struct wl_compositor *compositor;
    struct wl_subcompositor *subcompositor;
    struct xdg_wm_base   *wmBase;
    struct wl_seat       *seat;
    GLFWwindow           *mainWindow;
    struct wl_display    *wlDisplay;
    int                   initialized;
    uint32_t              lastSerial;
} popupGlobals = {
    .compositor = NULL,
    .subcompositor = NULL,
    .wmBase = NULL,
    .seat = NULL,
    .mainWindow = NULL,
    .wlDisplay = NULL,
    .initialized = 0,
    .lastSerial = 0
};

/*
 * Forward declarations for static functions.
 */
static void popup_xdg_popup_configure(void *data, struct xdg_popup *xdg_popup,
    int32_t x, int32_t y, int32_t width, int32_t height);
static void popup_xdg_popup_done(void *data, struct xdg_popup *xdg_popup);
static void popup_xdg_surface_configure(void *data,
    struct xdg_surface *xdg_surface, uint32_t serial);
static void popup_wm_base_ping(void *data, struct xdg_wm_base *wm_base,
    uint32_t serial);
static void popup_registry_global(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version);
static void popup_registry_global_remove(void *data,
    struct wl_registry *registry, uint32_t name);
static struct wl_surface *TkWaylandPopupGetWLSurface(GLFWwindow *window);
static int TkWaylandPopupBindGlobals(void);
static NVGcontext* TkWaylandPopupGetMainContext(void);

/* xdg_popup listener. */
static const struct xdg_popup_listener popup_xdg_popup_listener = {
    .configure = popup_xdg_popup_configure,
    .popup_done = popup_xdg_popup_done,
};

/* xdg_surface listener. */
static const struct xdg_surface_listener popup_xdg_surface_listener = {
    .configure = popup_xdg_surface_configure,
};

/* xdg_wm_base listener. */
static const struct xdg_wm_base_listener popup_wm_base_listener = {
    .ping = popup_wm_base_ping,
};

/* Registry listener. */
static const struct wl_registry_listener popup_registry_listener = {
    .global = popup_registry_global,
    .global_remove = popup_registry_global_remove,
};

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupGetMainContext --
 *
 *	Get the NanoVG context from the main GLFW window.
 *
 * Results:
 *	NVGcontext pointer or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static NVGcontext*
TkWaylandPopupGetMainContext(void)
{
    if (!popupGlobals.mainWindow) {
        POPUP_LOG("TkWaylandPopupGetMainContext: no main window");
        return NULL;
    }
    
    /* Get the glfwTkInfo from the main window's user pointer */
    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(popupGlobals.mainWindow);
    if (!infoPtr) {
        POPUP_LOG("TkWaylandPopupGetMainContext: no glfwTkInfo for main window");
        return NULL;
    }
    
    if (!infoPtr->context.vg) {
        POPUP_LOG("TkWaylandPopupGetMainContext: main window has no NanoVG context");
        return NULL;
    }
    
    return infoPtr->context.vg;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupBindGlobals --
 *
 *	Bind the global Wayland objects from the main module or from
 *	the Wayland registry.
 *
 * Results:
 *	1 on success, 0 on failure.
 *
 * Side effects:
 *	Sets up popupGlobals with the shared objects.
 *
 *----------------------------------------------------------------------
 */

static int
TkWaylandPopupBindGlobals(void)
{
    struct wl_display *display;
    struct wl_registry *registry;
    
    POPUP_LOG("TkWaylandPopupBindGlobals: binding globals");
    
    /* First try to get globals from the main module. */
    if (waylandDisplay) {
        popupGlobals.wlDisplay = waylandDisplay;
        POPUP_LOG("Got waylandDisplay from main module");
    }
    
    if (waylandCompositor) {
        popupGlobals.compositor = waylandCompositor;
        POPUP_LOG("Got waylandCompositor from main module");
    }
    if (waylandSubcompositor) {
        popupGlobals.subcompositor = waylandSubcompositor;
        POPUP_LOG("Got waylandSubcompositor from main module");
    }
    if (waylandWmBase) {
        popupGlobals.wmBase = waylandWmBase;
        POPUP_LOG("Got waylandWmBase from main module");
    }
    if (waylandSeat) {
        popupGlobals.seat = waylandSeat;
        POPUP_LOG("Got waylandSeat from main module");
    }
    
    /* If we have all needed objects, we're done */
    if (popupGlobals.wlDisplay && popupGlobals.compositor &&
        popupGlobals.subcompositor && popupGlobals.wmBase &&
        popupGlobals.seat) {
        POPUP_LOG("All globals bound successfully");
        return 1;
    }
    
    /* If we have a display but missing globals, bind from registry. */
    if (popupGlobals.wlDisplay) {
        display = popupGlobals.wlDisplay;
        POPUP_LOG("Binding globals from registry");
        
        registry = wl_display_get_registry(display);
        if (!registry) {
            POPUP_LOG("TkWaylandPopupBindGlobals: failed to get registry");
            return 0;
        }
        
        wl_registry_add_listener(registry, &popup_registry_listener, NULL);
        wl_display_roundtrip(display);
        wl_display_roundtrip(display);
        
        if (popupGlobals.compositor && popupGlobals.subcompositor &&
            popupGlobals.wmBase && popupGlobals.seat) {
            POPUP_LOG("Globals bound from registry");
            return 1;
        }
    }
    
    POPUP_LOG("TkWaylandPopupBindGlobals: failed to bind all globals");
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * popup_registry_global --
 *
 *	Callback for wl_registry global events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Binds Wayland globals to popupGlobals.
 *
 *----------------------------------------------------------------------
 */

static void
popup_registry_global(
    void *data,
    struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version)
{
    POPUP_LOG("Registry global: %s", interface);
    
    if (strcmp(interface, "wl_compositor") == 0) {
        popupGlobals.compositor = (struct wl_compositor *)
            wl_registry_bind(registry, name, &wl_compositor_interface,
                version > 4 ? 4 : version);
        POPUP_LOG("Bound wl_compositor");
    } else if (strcmp(interface, "wl_subcompositor") == 0) {
        popupGlobals.subcompositor = (struct wl_subcompositor *)
            wl_registry_bind(registry, name, &wl_subcompositor_interface,
                version > 1 ? 1 : version);
        POPUP_LOG("Bound wl_subcompositor");
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        popupGlobals.wmBase = (struct xdg_wm_base *)
            wl_registry_bind(registry, name, &xdg_wm_base_interface,
                version > 3 ? 3 : version);
        POPUP_LOG("Bound xdg_wm_base");
    } else if (strcmp(interface, "wl_seat") == 0) {
        popupGlobals.seat = (struct wl_seat *)
            wl_registry_bind(registry, name, &wl_seat_interface,
                version > 7 ? 7 : version);
        POPUP_LOG("Bound wl_seat");
    }
}

/*
 *----------------------------------------------------------------------
 *
 * popup_registry_global_remove --
 *
 *	Callback for wl_registry global remove events.
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
popup_registry_global_remove(
    void *data,
    struct wl_registry *registry,
    uint32_t name)
{
    /* Nothing to do */
}

/*
 *----------------------------------------------------------------------
 *
 * popup_xdg_popup_configure --
 *
 *	Callback when an xdg_popup is configured.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores position and size.
 *
 *----------------------------------------------------------------------
 */

static void
popup_xdg_popup_configure(
    void *data,
    struct xdg_popup *xdg_popup,
    int32_t x,
    int32_t y,
    int32_t width,
    int32_t height)
{
    TkWaylandPopup *popup = (TkWaylandPopup *)data;
    
    POPUP_LOG("xdg_popup configure: x=%d, y=%d, w=%d, h=%d", x, y, width, height);
    popup->x = x;
    popup->y = y;
    popup->width = width;
    popup->height = height;
    popup->configured = 1;
}

/*
 *----------------------------------------------------------------------
 *
 * popup_xdg_popup_done --
 *
 *	Callback when an xdg_popup is dismissed by the compositor.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Triggers the done callback if set.
 *
 *----------------------------------------------------------------------
 */

static void
popup_xdg_popup_done(
    void *data,
    struct xdg_popup *xdg_popup)
{
    TkWaylandPopup *popup = (TkWaylandPopup *)data;
    
    POPUP_LOG("xdg_popup popup_done callback");
    
    if (popup && popup->doneCallback) {
        popup->doneCallback(popup->doneClientData);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * popup_xdg_surface_configure --
 *
 *	Callback when an xdg_surface is configured.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Acknowledges the configure.
 *
 *----------------------------------------------------------------------
 */

static void
popup_xdg_surface_configure(
    void *data,
    struct xdg_surface *xdg_surface,
    uint32_t serial)
{
    TkWaylandPopup *popup = (TkWaylandPopup *)data;
    
    POPUP_LOG("xdg_surface configure serial=%u", serial);
    popup->configured = 1;
    xdg_surface_ack_configure(xdg_surface, serial);
}

/*
 *----------------------------------------------------------------------
 *
 * popup_wm_base_ping --
 *
 *	Callback for xdg_wm_base ping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Replies to the ping.
 *
 *----------------------------------------------------------------------
 */

static void
popup_wm_base_ping(
    void *data,
    struct xdg_wm_base *wm_base,
    uint32_t serial)
{
    POPUP_LOG("xdg_wm_base ping");
    xdg_wm_base_pong(wm_base, serial);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupGetWLSurface --
 *
 *	Get the wl_surface associated with a GLFW window.
 *
 * Results:
 *	struct wl_surface* or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static struct wl_surface *
TkWaylandPopupGetWLSurface(
    GLFWwindow *window)
{
    if (!window) return NULL;

    /*
     * On Wayland, GLFW exposes the native wl_surface via glfwGetWaylandWindow().
     * We must NOT use glfwGetWindowUserPointer here, because Tk uses that
     * for its own glfwTkInfo bookkeeping.
     */
#ifdef GLFW_EXPOSE_NATIVE_WAYLAND
    struct wl_surface *surf = glfwGetWaylandWindow(window);
    return surf;
#else
    return NULL;
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupInit --
 *
 *	Initialize the popup module with global Wayland objects.
 *
 * Results:
 *	1 on success, 0 on failure.
 *
 * Side effects:
 *	Stores Wayland global objects.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int TkWaylandPopupInit(void) {
    if (popupGlobals.initialized) {
        return 1;
    }
    
    POPUP_LOG("Initializing Wayland popup module");
    
    /* Attempt to bind only if globals are already available. */
    if (TkWaylandPopupBindGlobals()) {
        popupGlobals.initialized = 1;
        return 1;
    }
    
    return 1; /* Return success but mark not initialized. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupSetMainWindow --
 *
 *	Set the main GLFW window for context sharing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores the main window.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupSetMainWindow(
    GLFWwindow *window)
{
    if (!window) {
        POPUP_LOG("TkWaylandPopupSetMainWindow: NULL window");
        return;
    }
    
    popupGlobals.mainWindow = window;
    POPUP_LOG("TkWaylandPopupSetMainWindow: main window set");
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupCreate --
 *
 *	Create a new xdg_popup (transient popup with compositor dismissal).
 *
 * Results:
 *	Pointer to TkWaylandPopup, or NULL on failure.
 *
 * Side effects:
 *	Creates Wayland objects for the popup.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkWaylandPopup *
TkWaylandPopupCreate(
    GLFWwindow *parentGlfw,
    int anchorX, int anchorY,
    int anchorW, int anchorH,
    int popupW, int popupH,
    uint32_t anchor,
    uint32_t gravity,
    int grabInput,
    uint32_t serial)
{
    TkWaylandPopup *popup;
    struct wl_surface *parentSurface;
    struct wl_display *display;
    struct xdg_positioner *positioner;
    struct xdg_surface *parentXdgSurface;
    
    if (!popupGlobals.initialized || !popupGlobals.wmBase) {
        POPUP_LOG("TkWaylandPopupCreate: popup module not initialized");
        return NULL;
    }
    
    if (!parentGlfw) {
        POPUP_LOG("TkWaylandPopupCreate: no parent window");
        return NULL;
    }
    
    /* Get the parent surface from the GLFW window. */
    parentSurface = TkWaylandPopupGetWLSurface(parentGlfw);
    if (!parentSurface) {
        POPUP_LOG("TkWaylandPopupCreate: no parent surface");
        return NULL;
    }
    
    display = popupGlobals.wlDisplay;
    if (!display) {
        POPUP_LOG("TkWaylandPopupCreate: no Wayland display");
        return NULL;
    }
    
    /* Create the xdg_surface for the parent. */
    parentXdgSurface = xdg_wm_base_get_xdg_surface(popupGlobals.wmBase, parentSurface);
    if (!parentXdgSurface) {
        POPUP_LOG("TkWaylandPopupCreate: failed to create parent xdg_surface");
        return NULL;
    }
    
    popup = (TkWaylandPopup *)calloc(1, sizeof(TkWaylandPopup));
    if (!popup) {
        xdg_surface_destroy(parentXdgSurface);
        POPUP_LOG("TkWaylandPopupCreate: malloc failed");
        return NULL;
    }
    
    popup->parentGlfw = parentGlfw;
    popup->parentSurface = parentSurface;
    popup->width = popupW;
    popup->height = popupH;
    popup->x = anchorX;
    popup->y = anchorY;
    popup->isSubsurface = 0;
    popup->configured = 0;
    popup->drawing = 0;
    popup->serial = serial;
    
    /* Get the main window's NanoVG context */
    popup->vg = TkWaylandPopupGetMainContext();
    if (popup->vg) {
        POPUP_LOG("TkWaylandPopupCreate: using main window's NanoVG context");
    } else {
        POPUP_LOG("TkWaylandPopupCreate: WARNING - no main window context available");
    }
    
    /* Create wl_surface. */
    popup->surface = wl_compositor_create_surface(popupGlobals.compositor);
    if (!popup->surface) {
        POPUP_LOG("TkWaylandPopupCreate: failed to create wl_surface");
        xdg_surface_destroy(parentXdgSurface);
        free(popup);
        return NULL;
    }
    
    /* Create xdg_surface for the popup. */
    popup->xdgSurface = xdg_wm_base_get_xdg_surface(
        popupGlobals.wmBase, popup->surface);
    if (!popup->xdgSurface) {
        POPUP_LOG("TkWaylandPopupCreate: failed to create xdg_surface");
        wl_surface_destroy(popup->surface);
        xdg_surface_destroy(parentXdgSurface);
        free(popup);
        return NULL;
    }
    
    xdg_surface_add_listener(popup->xdgSurface, &popup_xdg_surface_listener, popup);
    
    /* Create positioner. */
    positioner = xdg_wm_base_create_positioner(popupGlobals.wmBase);
    if (!positioner) {
        POPUP_LOG("TkWaylandPopupCreate: failed to create positioner");
        xdg_surface_destroy(popup->xdgSurface);
        wl_surface_destroy(popup->surface);
        xdg_surface_destroy(parentXdgSurface);
        free(popup);
        return NULL;
    }
    
    xdg_positioner_set_size(positioner, popupW, popupH);
    xdg_positioner_set_anchor_rect(positioner, anchorX, anchorY, anchorW, anchorH);
    xdg_positioner_set_anchor(positioner, anchor);
    xdg_positioner_set_gravity(positioner, gravity);
    xdg_positioner_set_constraint_adjustment(positioner,
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y |
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X |
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y);
    
    /* Create xdg_popup - parent is the xdg_surface. */
    popup->xdgPopup = xdg_surface_get_popup(popup->xdgSurface,
        parentXdgSurface, positioner);
    
    xdg_positioner_destroy(positioner);
    
    if (!popup->xdgPopup) {
        POPUP_LOG("TkWaylandPopupCreate: failed to create xdg_popup");
        xdg_surface_destroy(popup->xdgSurface);
        wl_surface_destroy(popup->surface);
        xdg_surface_destroy(parentXdgSurface);
        free(popup);
        return NULL;
    }
    
    xdg_popup_add_listener(popup->xdgPopup, &popup_xdg_popup_listener, popup);
    
    if (grabInput && serial != 0) {
        xdg_popup_grab(popup->xdgPopup, popupGlobals.seat, serial);
    }
    
    /* Set input region (empty = no input). */
    popup->inputRegion = wl_compositor_create_region(popupGlobals.compositor);
    if (popup->inputRegion) {
        wl_surface_set_input_region(popup->surface, popup->inputRegion);
        wl_region_destroy(popup->inputRegion);
        popup->inputRegion = NULL;
    }
    
    wl_surface_commit(popup->surface);
    
    POPUP_LOG("TkWaylandPopupCreate: xdg_popup created (grab=%d, serial=%u)",
        grabInput, serial);
    
    return popup;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSubsurfaceCreate --
 *
 *	Create a wl_subsurface (permanent child surface).
 *
 * Results:
 *	Pointer to TkWaylandPopup, or NULL on failure.
 *
 * Side effects:
 *	Creates Wayland objects for the subsurface.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkWaylandPopup *
TkWaylandSubsurfaceCreate(
    GLFWwindow *parentGlfw,
    int x,
    int y,
    int width,
    int height)
{
    TkWaylandPopup *popup;
    struct wl_surface *parentSurface;
    struct wl_display *display;
    
    if (!popupGlobals.initialized || !popupGlobals.compositor ||
        !popupGlobals.subcompositor) {
            if (TkWaylandPopupBindGlobals()) {
                popupGlobals.initialized = 1;
            } else {
                POPUP_LOG("TkWaylandSubsurfaceCreate: popup module not initialized");
                return NULL;
            }
    }
    
    if (!parentGlfw) {
        POPUP_LOG("TkWaylandSubsurfaceCreate: no parent window");
        return NULL;
    }
    
    parentSurface = TkWaylandPopupGetWLSurface(parentGlfw);
    if (!parentSurface) {
        POPUP_LOG("TkWaylandSubsurfaceCreate: no parent surface");
        return NULL;
    }
    
    display = popupGlobals.wlDisplay;
    if (!display) {
        POPUP_LOG("TkWaylandSubsurfaceCreate: no Wayland display");
        return NULL;
    }
    
    popup = (TkWaylandPopup *)calloc(1, sizeof(TkWaylandPopup));
    if (!popup) {
        POPUP_LOG("TkWaylandSubsurfaceCreate: malloc failed");
        return NULL;
    }
    
    popup->parentGlfw = parentGlfw;
    popup->parentSurface = parentSurface;
    popup->width = width;
    popup->height = height;
    popup->x = x;
    popup->y = y;
    popup->isSubsurface = 1;
    popup->configured = 1;
    popup->drawing = 0;
    
    /* Get the main window's NanoVG context */
    popup->vg = TkWaylandPopupGetMainContext();
    if (popup->vg) {
        POPUP_LOG("TkWaylandSubsurfaceCreate: using main window's NanoVG context");
    } else {
        POPUP_LOG("TkWaylandSubsurfaceCreate: WARNING - no main window context available");
    }
    
    /* Create wl_surface. */
    popup->surface = wl_compositor_create_surface(popupGlobals.compositor);
    if (!popup->surface) {
        POPUP_LOG("TkWaylandSubsurfaceCreate: failed to create wl_surface");
        free(popup);
        return NULL;
    }
    
    /* Create wl_subsurface. */
    popup->subsurface = wl_subcompositor_get_subsurface(
        popupGlobals.subcompositor, popup->surface, parentSurface);
    if (!popup->subsurface) {
        POPUP_LOG("TkWaylandSubsurfaceCreate: failed to create subsurface");
        wl_surface_destroy(popup->surface);
        free(popup);
        return NULL;
    }
    
    wl_subsurface_set_position(popup->subsurface, x, y);
    wl_subsurface_set_sync(popup->subsurface);
    
    /* Set input region (empty = no input). */
    popup->inputRegion = wl_compositor_create_region(popupGlobals.compositor);
    if (popup->inputRegion) {
        wl_surface_set_input_region(popup->surface, popup->inputRegion);
        wl_region_destroy(popup->inputRegion);
        popup->inputRegion = NULL;
    }
    
    wl_surface_commit(popup->surface);
    
    POPUP_LOG("Creating subsurface popup: pos=(%d,%d) size=%dx%d",
        x, y, width, height);
    
    return popup;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupDestroy --
 *
 *	Destroy a popup and free all resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys Wayland objects.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupDestroy(
    TkWaylandPopup *popup)
{
    if (!popup) return;
    
    POPUP_LOG("Destroying popup");
    
    if (popup->drawing) {
        TkWaylandPopupEndDraw(popup);
    }
    
    /* Note: NanoVG context is shared, not destroyed here. */
    popup->vg = NULL;
    
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
    
    free(popup);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandPopupBeginDraw --
 *
 *	Prepare the popup for drawing using the shared NanoVG context.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Sets up the NanoVG frame for drawing.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandPopupBeginDraw(TkWaylandPopup *popup)
{
    if (!popup) {
        return TCL_ERROR;
    }
    
    /* Ensure we have a NanoVG context */
    if (!popup->vg) {
        /* Try to get it from the main window */
        popup->vg = TkWaylandPopupGetMainContext();
        if (!popup->vg) {
            POPUP_LOG("TkWaylandPopupBeginDraw: no NanoVG context available");
            return TCL_ERROR;
        }
        POPUP_LOG("TkWaylandPopupBeginDraw: obtained context from main window");
    }
    
    /* Use GLFW to make the main context current */
    if (popupGlobals.mainWindow) {
        glfwMakeContextCurrent(popupGlobals.mainWindow);
        POPUP_LOG("TkWaylandPopupBeginDraw: made GLFW context current");
    } else {
        POPUP_LOG("TkWaylandPopupBeginDraw: no main window for context");
        return TCL_ERROR;
    }
    
    /* Begin the NanoVG frame. */
    nvgBeginFrame(popup->vg, popup->width, popup->height, 1.0);
    popup->drawing = 1;
    
    POPUP_LOG("TkWaylandPopupBeginDraw: began drawing %dx%d", 
             popup->width, popup->height);
    
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandPopupEndDraw --
 *
 *	Finish drawing and update the popup surface.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Ends the NanoVG frame and swaps buffers.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupEndDraw(TkWaylandPopup *popup)
{
    if (!popup || !popup->drawing) {
        return;
    }
    
    if (popup->vg) {
        /* End the NanoVG frame. */
        nvgEndFrame(popup->vg);
    }
    
    /* Swap buffers using GLFW. */
    if (popupGlobals.mainWindow) {
        glfwSwapBuffers(popupGlobals.mainWindow);
        POPUP_LOG("TkWaylandPopupEndDraw: swapped buffers via GLFW");
    }
    
    /* Commit the surface so the compositor shows the new content. */
    if (popup->surface) {
        wl_surface_commit(popup->surface);
    }
    
    popup->drawing = 0;
    POPUP_LOG("TkWaylandPopupEndDraw: ended drawing");
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandPopupGetNVGContext --
 *
 *	Return the popup's NanoVG context (the main window's context).
 *
 * Results:
 *	The NVGcontext pointer, or NULL.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext*
TkWaylandPopupGetNVGContext(TkWaylandPopup *popup)
{
    if (!popup) {
        return NULL;
    }
    
    /* If we don't have a context, try to get it from the main window */
    if (!popup->vg) {
        popup->vg = TkWaylandPopupGetMainContext();
    }
    
    return popup->vg;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupGetSize --
 *
 *	Get the size of a popup.
 *
 * Results:
 *	Sets widthOut and heightOut to the popup's dimensions.
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
    if (!popup) {
        if (widthOut) *widthOut = 0;
        if (heightOut) *heightOut = 0;
        return;
    }
    if (widthOut) *widthOut = popup->width;
    if (heightOut) *heightOut = popup->height;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupGetPosition --
 *
 *	Get the position of a popup.
 *
 * Results:
 *	Sets xOut and yOut to the popup's position.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupGetPosition(
    TkWaylandPopup *popup,
    int *xOut,
    int *yOut)
{
    if (!popup) {
        if (xOut) *xOut = 0;
        if (yOut) *yOut = 0;
        return;
    }
    if (xOut) *xOut = popup->x;
    if (yOut) *yOut = popup->y;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupSetDoneCallback --
 *
 *	Set the callback for when an xdg_popup is dismissed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores the callback.
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
    popup->doneCallback = callback;
    popup->doneClientData = clientData;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupSetSerial --
 *
 *	Set the serial for the next popup grab.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores the serial.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupSetSerial(
    uint32_t serial)
{
    popupGlobals.lastSerial = serial;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupLastSerial --
 *
 *	Get the last stored serial.
 *
 * Results:
 *	uint32_t serial.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE uint32_t
TkWaylandPopupLastSerial(void)
{
    return popupGlobals.lastSerial;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupGetSeat --
 *
 *	Get the Wayland seat object.
 *
 * Results:
 *	struct wl_seat* or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE struct wl_seat *
TkWaylandPopupGetSeat(void)
{
    return popupGlobals.seat;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSubsurfacePlaceAbove --
 *
 *	Place a subsurface above another in the stacking order.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes stacking order.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandSubsurfacePlaceAbove(
    TkWaylandPopup *popup,
    TkWaylandPopup *sibling)
{
    if (!popup || !popup->subsurface) return;
    
    if (sibling && sibling->surface) {
        wl_subsurface_place_above(popup->subsurface, sibling->surface);
    } else {
        wl_subsurface_place_above(popup->subsurface, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSubsurfaceReconfigure --
 *
 *	Reconfigure a subsurface (move/resize).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates position and size of the subsurface.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandSubsurfaceReconfigure(
    TkWaylandPopup *popup,
    int x,
    int y,
    int width,
    int height)
{
    if (!popup) return;
    
    popup->x = x;
    popup->y = y;
    popup->width = width;
    popup->height = height;
    
    if (popup->subsurface) {
        wl_subsurface_set_position(popup->subsurface, x, y);
    }
    
    if (popup->surface) {
        wl_surface_commit(popup->surface);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupDestroyAll --
 *
 *	Destroy all popups (cleanup on shutdown).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None (popups are managed by their owners).
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupDestroyAll(void)
{
    POPUP_LOG("TkWaylandPopupDestroyAll called");
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandPopupResize --
 *
 *	Attempt to resize an existing popup.
 *	Currently unimplemented - returns TCL_ERROR to force recreation.
 *
 * Results:
 *	Always returns TCL_ERROR.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandPopupResize(
    TkWaylandPopup *popup,
    int width,
    int height)
{
    /* Always return error - caller will recreate. */
    return TCL_ERROR;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
