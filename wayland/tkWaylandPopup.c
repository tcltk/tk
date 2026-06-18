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
 *	  - Rendering is performed with wl_shm buffers that are filled
 *	    by the caller via glReadPixels from the main context.
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

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

/* Debug macro. */
#define POPUP_DEBUG(msg, ...) fprintf(stderr, "POPUP: " msg "\n", ##__VA_ARGS__)

/* SHM format - ARGB8888 (same as GL_RGBA). */
#define SHM_FORMAT WL_SHM_FORMAT_ARGB8888
#define SHM_BYTES_PER_PIXEL 4

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopup --
 *
 *	The popup instance structure. This structure must be defined here
 *	(not just in the header) because the header declares TkWaylandPopup
 *	as an opaque type.
 *
 *----------------------------------------------------------------------
 */

struct TkWaylandPopup {
    /* Wayland objects. */
    struct wl_surface    *surface;
    struct xdg_surface   *xdgSurface;
    struct xdg_popup     *xdgPopup;
    struct wl_subsurface *subsurface;   /* Non-NULL for subsurface-mode
                                         * popups (e.g. the menubar);
                                         * xdgSurface/xdgPopup are NULL
                                         * in that case. */
    struct wl_surface    *parentSurface; /* Parent surface for subsurface. */

    /* SHM buffer objects. */
    struct wl_shm_pool   *shmPool;
    struct wl_buffer     *shmBuffer;
    void                 *shmData;       /* mmap'd pointer to pixel data. */
    size_t                shmSize;       /* Total pool size in bytes. */
    int                   stride;        /* Bytes per row. */

    /* Geometry (logical pixels - compositor coordinates). */
    int x, y;                   /* Position confirmed by compositor. */
    int width, height;          /* Requested / confirmed size. */

    /* State flags. */
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
static struct wl_shm          *popupShm          = NULL;
static struct wl_seat         *popupSeat         = NULL;
static uint32_t                popupLastSerial   = 0;

static int popupModuleInitialized = 0;
static TkWaylandPopup *popupList = NULL;

/* Forward declarations. */
static int AllocateSHMBuffer(TkWaylandPopup *popup);
static void FreeSHMBuffer(TkWaylandPopup *popup);

/*
 *----------------------------------------------------------------------
 *
 * RegistryGlobal --
 *
 *	Wayland registry global object handler. Binds compositor,
 *	xdg_wm_base, shm, and seat interfaces when they appear in the registry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes popupCompositor, popupSubcompositor, popupWmBase,
 *	popupShm, and popupSeat.
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
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        popupShm = wl_registry_bind(registry, name,
            &wl_shm_interface, 1);
        POPUP_DEBUG("Bound wl_shm");
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
 *	Updates popup's x, y, width, and height fields. Reallocates SHM
 *	buffer if size changed.
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
    
    if (width > 0 && height > 0) {
        if (width != popup->width || height != popup->height) {
            /* Reallocate SHM buffer for new size. */
            FreeSHMBuffer(popup);
            popup->width = width;
            popup->height = height;
            if (!AllocateSHMBuffer(popup)) {
                POPUP_DEBUG("Failed to reallocate SHM buffer for new size");
            }
        }
    }
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
 * AllocateSHMBuffer --
 *
 *	Allocate a wl_shm buffer for the popup surface.
 *
 * Results:
 *	Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *	Creates shmPool, shmBuffer, and mmap's the memory.
 *
 *----------------------------------------------------------------------
 */

static int
AllocateSHMBuffer(
    TkWaylandPopup *popup)
{
    if (!popup->surface || popup->width <= 0 || popup->height <= 0) {
        return 0;
    }
    
    if (!popupShm) {
        POPUP_DEBUG("No wl_shm available");
        return 0;
    }

    /* Calculate buffer size and stride. */
    popup->stride = popup->width * SHM_BYTES_PER_PIXEL;
    /* Align stride to 4 bytes (common requirement). */
    int stride_align = 4;
    popup->stride = (popup->stride + stride_align - 1) & ~(stride_align - 1);
    popup->shmSize = popup->stride * popup->height;

    /* Create a temporary file for shared memory. */
    char filename[] = "/tmp/wl-shm-XXXXXX";
    int fd = mkstemp(filename);
    if (fd < 0) {
        POPUP_DEBUG("Failed to create temp file");
        return 0;
    }
    unlink(filename);

    /* Set the size of the file. */
    if (ftruncate(fd, popup->shmSize) < 0) {
        POPUP_DEBUG("Failed to truncate temp file");
        close(fd);
        return 0;
    }

    /* Map the memory. */
    popup->shmData = mmap(NULL, popup->shmSize, PROT_READ | PROT_WRITE,
                          MAP_SHARED, fd, 0);
    if (popup->shmData == MAP_FAILED) {
        POPUP_DEBUG("Failed to mmap shared memory");
        close(fd);
        popup->shmData = NULL;
        return 0;
    }

    /* Create the shm pool. */
    popup->shmPool = wl_shm_create_pool(popupShm, fd, popup->shmSize);
    close(fd);

    if (!popup->shmPool) {
        POPUP_DEBUG("Failed to create wl_shm_pool");
        munmap(popup->shmData, popup->shmSize);
        popup->shmData = NULL;
        return 0;
    }

    /* Create the buffer. */
    popup->shmBuffer = wl_shm_pool_create_buffer(popup->shmPool, 0,
                                                   popup->width, popup->height,
                                                   popup->stride, SHM_FORMAT);
    if (!popup->shmBuffer) {
        POPUP_DEBUG("Failed to create wl_buffer");
        wl_shm_pool_destroy(popup->shmPool);
        popup->shmPool = NULL;
        munmap(popup->shmData, popup->shmSize);
        popup->shmData = NULL;
        return 0;
    }

    /* Clear the buffer to transparent black. */
    memset(popup->shmData, 0, popup->shmSize);
    
    POPUP_DEBUG("Allocated SHM buffer: %dx%d, stride=%d, size=%zu",
                popup->width, popup->height, popup->stride, popup->shmSize);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeSHMBuffer --
 *
 *	Free the SHM buffer and associated resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys shmBuffer, shmPool, and unmaps memory.
 *
 *----------------------------------------------------------------------
 */

static void
FreeSHMBuffer(
    TkWaylandPopup *popup)
{
    if (popup->shmBuffer) {
        wl_buffer_destroy(popup->shmBuffer);
        popup->shmBuffer = NULL;
    }
    if (popup->shmPool) {
        wl_shm_pool_destroy(popup->shmPool);
        popup->shmPool = NULL;
    }
    if (popup->shmData) {
        munmap(popup->shmData, popup->shmSize);
        popup->shmData = NULL;
        popup->shmSize = 0;
    }
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
 *	Binds wl_compositor, xdg_wm_base, wl_shm, and wl_seat from the
 *	Wayland global registry. Must be called after glfwInit().
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

    if (!popupCompositor || !popupWmBase || !popupShm) {
        POPUP_DEBUG("Missing compositor, wm_base, or shm");
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

    /* Allocate SHM buffer. */
    if (!AllocateSHMBuffer(popup)) {
        POPUP_DEBUG("Failed to allocate SHM buffer");
        xdg_popup_destroy(popup->xdgPopup);
        xdg_surface_destroy(popup->xdgSurface);
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }

    /* Attach the initial buffer. */
    wl_surface_attach(popup->surface, popup->shmBuffer, 0, 0);
    wl_surface_damage(popup->surface, 0, 0, popup->width, popup->height);
    wl_surface_commit(popup->surface);
    wl_display_flush(popupDisplay);

    /* Wait for configure. */
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
    
    /* Place the subsurface ABOVE the parent surface. */
    wl_subsurface_place_above(popup->subsurface, parentSurface);
    POPUP_DEBUG("Subsurface placed above parent");

    /* Allocate SHM buffer. */
    if (!AllocateSHMBuffer(popup)) {
        POPUP_DEBUG("Failed to allocate SHM buffer");
        wl_subsurface_destroy(popup->subsurface);
        wl_surface_destroy(popup->surface);
        Tcl_Free(popup);
        return NULL;
    }

    /* Commit the parent surface. */
    wl_surface_commit(parentSurface);
    wl_display_flush(popupDisplay);

    /* Attach the initial buffer. */
    wl_surface_attach(popup->surface, popup->shmBuffer, 0, 0);
    wl_surface_damage(popup->surface, 0, 0, popup->width, popup->height);
    wl_surface_commit(popup->surface);
    wl_display_flush(popupDisplay);

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
 *	Resizes the SHM buffer and updates the subsurface position.
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
        FreeSHMBuffer(popup);
        popup->width  = width;
        popup->height = height;
        if (!AllocateSHMBuffer(popup)) {
            POPUP_DEBUG("Failed to reallocate SHM buffer");
            return;
        }
    }

    if (x != popup->x || y != popup->y) {
        popup->x = x;
        popup->y = y;
        wl_subsurface_set_position(popup->subsurface, x, y);
    }
    
    /* Attach the new buffer and damage the entire surface to force redraw. */
    wl_surface_attach(popup->surface, popup->shmBuffer, 0, 0);
    wl_surface_damage(popup->surface, 0, 0, popup->width, popup->height);
    wl_surface_commit(popup->surface);
    
    /* Also need to commit the parent surface. */
    if (popup->parentSurface) {
        wl_surface_commit(popup->parentSurface);
    }
    
    wl_display_flush(popupDisplay);
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
 *	Destroys all Wayland objects and frees SHM buffer.
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

    /* Free SHM buffer. */
    FreeSHMBuffer(popup);

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
    POPUP_DEBUG("Destroying all popups");
    while (popupList) {
        TkWaylandPopupDestroy(popupList);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupGetSHMData --
 *
 *	Return a pointer to the SHM buffer data for the popup.
 *
 * Results:
 *	Returns the SHM data pointer, or NULL if popup is invalid or no buffer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void *
TkWaylandPopupGetSHMData(
    TkWaylandPopup *popup)
{
    if (!popup || !popup->shmData) return NULL;
    return popup->shmData;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupGetStride --
 *
 *	Return the stride (bytes per row) of the SHM buffer.
 *
 * Results:
 *	Returns the stride in bytes, or 0 if popup is invalid.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandPopupGetStride(
    TkWaylandPopup *popup)
{
    if (!popup) return 0;
    return popup->stride;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupBeginDraw --
 *
 *	Begin a draw cycle for the popup. Returns a pointer to the SHM buffer.
 *
 * Results:
 *	Returns the SHM data pointer on success, NULL if popup is not ready.
 *
 * Side effects:
 *	None - no EGL context operations are performed.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE uint8_t *TkWaylandPopupBeginDraw(
    TkWaylandPopup *popup,
    TCL_UNUSED(int *)) /* stride */
{
    if (!popup || !popup->shmData || !popup->configured) {
        if (popup && !popup->configured) {
            POPUP_DEBUG("BeginDraw: popup not configured yet");
        }
        return NULL;
    }
    
    /* Return pointer to SHM buffer for caller to fill. */
    return popup->shmData;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupEndDraw --
 *
 *	Finish the draw cycle and commit the SHM buffer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Attaches the SHM buffer to the surface and commits it.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupEndDraw(
    TkWaylandPopup *popup)
{
    if (!popup || !popup->shmBuffer || !popup->surface) return;

    /* Attach the buffer and damage the entire surface. */
    wl_surface_attach(popup->surface, popup->shmBuffer, 0, 0);
    wl_surface_damage(popup->surface, 0, 0, popup->width, popup->height);
    wl_surface_commit(popup->surface);
    
    if (popupDisplay) {
        wl_display_flush(popupDisplay);
    }
    
    POPUP_DEBUG("EndDraw: committed surface");
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
 *----------------------------------------------------------------------
 *
 * TkWaylandPopupCaptureGLPixels --
 *
 *	Blits and reads back pixels from the parent window's active 
 *	GLES3 FBO directly into the popup's top-down SHM data buffer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies the SHM buffer contents. Saves and restores the current
 *	framebuffer binding.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupCaptureGLPixels(
    TkWaylandPopup *popup,
    void *parentTkWindow) /* Pass winPtr or menuPtr->tkwin */
{
    if (!popup || !popup->shmData || popup->width <= 0 || popup->height <= 0) {
        return;
    }

    TkWindow *winPtr = (TkWindow *)parentTkWindow;
    GLint previousFBO = 0;

    /* Save the active framebuffer binding so we don't break NanoVG state cycles. */
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousFBO);

    /* Bind the window's actual widget-filled FBO backing store */
    if (winPtr && winPtr->privatePtr && winPtr->privatePtr->fb) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, winPtr->privatePtr->fb->fbo);
    } else {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }

    /* Set pack alignment to 4 bytes to match ythe ARGB8888 stride metrics. */
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    /*
     * COORDINATE SYSTEM INVERSION (GL_FLIPY):
     * glReadPixels reads bottom-up. Wayland SHM data is mapped top-down.
     * We invert the rows scanline-by-scanline as we copy them.
     */
    unsigned char *shmDest = (unsigned char *)popup->shmData;
    int stride = popup->stride; /* Use the aligned stride calculated by AllocateSHMBuffer. */

    for (int y = 0; y < popup->height; y++) {
        /* Read rows from the bottom of the GL Framebuffer up into the top rows of SHM. */
        glReadPixels(0, (popup->height - 1 - y), popup->width, 1, 
                     GL_RGBA, GL_UNSIGNED_BYTE, shmDest + (y * stride));
    }

    /* Restore the previous framebuffer state. */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, previousFBO);
    
    POPUP_DEBUG("Captured %dx%d pixels from parent FBO to SHM", popup->width, popup->height);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */